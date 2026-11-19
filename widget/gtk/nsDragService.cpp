/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDragService.h"

#include "nsDragServiceGtk.h"
#include "nsDragSessionSource.h"
#ifdef MOZ_WAYLAND
#  include "nsDragServiceWayland.h"
#endif
#include <dlfcn.h>
#include <gtk/gtk.h>

#include <mutex>

#include "mozilla/AutoRestore.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/WidgetUtils.h"
#include "mozilla/WidgetUtilsGtk.h"
#include "mozilla/widget/nsGtkHtmlUtils.h"
#include "nsArrayUtils.h"
#include "nsGtkKeyUtils.h"
#include "nsIFileURL.h"
#include "nsPrimitiveHelpers.h"
#include "nsString.h"
#include "nsStringStream.h"
#include "nsWindow.h"

using namespace mozilla;
using namespace mozilla::widget;
using namespace mozilla::gfx;

#undef LOGDRAG
#undef LOGDRAGSTATIC
#ifdef MOZ_LOGGING
extern mozilla::LazyLogModule gWidgetDragLog;
#  define LOGDRAG(str, ...)                                                    \
    MOZ_LOG(                                                                   \
        gWidgetDragLog, mozilla::LogLevel::Debug,                              \
        ("[D %d]%s %*s" str, nsDragSession::GetLoopDepth(),                    \
         GetDebugTag().get(),                                                  \
         nsDragSession::GetLoopDepth() > 1 ? nsDragSession::GetLoopDepth() * 2 \
                                           : 0,                                \
         "", ##__VA_ARGS__))
#  define LOGDRAGSTATIC(str, ...) \
    MOZ_LOG(gWidgetDragLog, mozilla::LogLevel::Debug, (str, ##__VA_ARGS__))
#else
#  define LOGDRAG(...)
#  define LOGDRAGSTATIC(...)
#endif

const char nsDragSession::gMozUrlType[] = "_NETSCAPE_URL";
const char nsDragSession::gMimeListType[] =
    "application/x-moz-internal-item-list";
const char nsDragSession::gTextUriListType[] = "text/uri-list";
const char nsDragSession::gTextPlainUTF8Type[] = "text/plain; charset=utf-8";
const char nsDragSession::gXdndDirectSaveType[] = "XdndDirectSave0";
const char nsDragSession::gTabDropType[] = "application/x-moz-tabbrowser-tab";
const char nsDragSession::gPortalFile[] = "application/vnd.portal.files";
const char nsDragSession::gPortalFileTransfer[] =
    "application/vnd.portal.filetransfer";
const char nsDragSession::gUTF8STRINGType[] = "UTF8_STRING";
const char nsDragSession::gSTRINGType[] = "STRING";

GdkAtom nsDragSession::sJPEGImageMimeAtom;
GdkAtom nsDragSession::sJPGImageMimeAtom;
GdkAtom nsDragSession::sPNGImageMimeAtom;
GdkAtom nsDragSession::sGIFImageMimeAtom;
GdkAtom nsDragSession::sCustomTypesMimeAtom;
GdkAtom nsDragSession::sURLMimeAtom;
GdkAtom nsDragSession::sRTFMimeAtom;
GdkAtom nsDragSession::sTextMimeAtom;
GdkAtom nsDragSession::sMozUrlTypeAtom;
GdkAtom nsDragSession::sMimeListTypeAtom;
GdkAtom nsDragSession::sTextUriListTypeAtom;
GdkAtom nsDragSession::sTextPlainUTF8TypeAtom;
GdkAtom nsDragSession::sXdndDirectSaveTypeAtom;
GdkAtom nsDragSession::sTabDropTypeAtom;
GdkAtom nsDragSession::sFileMimeAtom;
GdkAtom nsDragSession::sPortalFileAtom;
GdkAtom nsDragSession::sPortalFileTransferAtom;
GdkAtom nsDragSession::sFilePromiseURLMimeAtom;
GdkAtom nsDragSession::sFilePromiseMimeAtom;
GdkAtom nsDragSession::sNativeImageMimeAtom;
GdkAtom nsDragSession::sUTF8STRINGMimeAtom;
GdkAtom nsDragSession::sSTRINGMimeAtom;

static nsString UTF8ToNewString(const char* aUTF8, uint32_t aUTF8DataLen = 0) {
  nsDependentCSubstring utf8(aUTF8,
                             aUTF8DataLen ? aUTF8DataLen : strlen(aUTF8));
  nsString ret;
  uint32_t convertedTextLen = 0;
  char16_t* convertedText = UTF8ToNewUnicode(utf8, &convertedTextLen);
  if (!convertedText) {
    return ret;
  }
  convertedTextLen *= 2;

  // Strip CRLF which might be present in the string.
  // Not sure where it's added, maybe Gtk?
  nsLinebreakHelpers::ConvertPlatformToDOMLinebreaks(
      /*  aIsSingleByteChars */ false, (void**)&convertedText,
      (int32_t*)&convertedTextLen);
  ret.Adopt(convertedText, convertedTextLen / 2);
  return ret;
}

static bool GetFileFromUri(const nsCString& aUri, nsCOMPtr<nsIFile>& aFile) {
  nsresult rv;
  nsCOMPtr<nsIIOService> ioService = do_GetIOService(&rv);
  nsCOMPtr<nsIURI> fileURI;
  if (NS_SUCCEEDED(
          ioService->NewURI(aUri, nullptr, nullptr, getter_AddRefs(fileURI)))) {
    nsCOMPtr<nsIFileURL> fileURL = do_QueryInterface(fileURI, &rv);
    if (NS_SUCCEEDED(rv)) {
      if (NS_SUCCEEDED(fileURL->GetFile(getter_AddRefs(aFile)))) {
        return true;
      }
    }
  }

  LOGDRAGSTATIC("GetFileFromUri() failed");
  return false;
}

bool DragData::IsImageFlavor() const {
  return mDataFlavor == nsDragSession::sJPEGImageMimeAtom ||
         mDataFlavor == nsDragSession::sJPGImageMimeAtom ||
         mDataFlavor == nsDragSession::sPNGImageMimeAtom ||
         mDataFlavor == nsDragSession::sGIFImageMimeAtom;
}

bool DragData::IsFileFlavor() const {
  return mDataFlavor == nsDragSession::sFileMimeAtom ||
         mDataFlavor == nsDragSession::sPortalFileAtom ||
         mDataFlavor == nsDragSession::sPortalFileTransferAtom;
}

bool DragData::IsTextFlavor() const {
  return nsDragSession::IsTextFlavor(mDataFlavor);
}

bool DragData::IsURIFlavor() const {
  // We support x-moz-url URL MIME type only
  return mDataFlavor == nsDragSession::sURLMimeAtom;
}

int DragData::GetURIsNum() const {
  int urlNum = 1;
  if (mDragUris) {
    urlNum = g_strv_length(mDragUris.get());
  } else if (IsURIFlavor()) {
    urlNum = mUris.Length();
  }
  LOGDRAGSTATIC("DragData::GetURIsNum() %d", urlNum);
  return urlNum;
}

bool DragData::Export(nsITransferable* aTransferable, uint32_t aItemIndex) {
  GUniquePtr<gchar> flavorName(gdk_atom_name(mDataFlavor));

  LOGDRAGSTATIC("DragData::Export() MIME %s index %d", flavorName.get(),
                aItemIndex);

  if (IsFileFlavor()) {
    MOZ_ASSERT(mDragUris.get());

    char** list = mDragUris.get();
    if (aItemIndex >= g_strv_length(list)) {
      NS_WARNING(
          nsPrintfCString(
              "DragData::Export(): Index %d is overflow file list len %d",
              aItemIndex, g_strv_length(list))
              .get());
      return false;
    }
    bool fileExists = false;
    nsCOMPtr<nsIFile> file;
    if (GetFileFromUri(nsDependentCString(list[aItemIndex]), file)) {
      file->Exists(&fileExists);
    }
    if (!fileExists) {
      LOGDRAGSTATIC("  uri %s not reachable/not found\n", list[aItemIndex]);
      return false;
    }
    LOGDRAGSTATIC("  export file %s (flavor: %s) as %s", list[aItemIndex],
                  flavorName.get(), kFileMime);
    aTransferable->SetTransferData(kFileMime, file);
    return true;
  }

  if (IsURIFlavor()) {
    MOZ_ASSERT(mAsURIData);
    if (aItemIndex >= mUris.Length()) {
      NS_WARNING(nsPrintfCString(
                     "DragData::Export(): Index %d is overflow uri list len %d",
                     aItemIndex, (int)mUris.Length())
                     .get());
      return false;
    }

    LOGDRAGSTATIC("%d URI:\n%s", (int)aItemIndex,
                  NS_ConvertUTF16toUTF8(mUris[aItemIndex]).get());

    // put it into the transferable.
    nsCOMPtr<nsISupports> genericDataWrapper;
    nsPrimitiveHelpers::CreatePrimitiveForData(
        nsAutoCString(kURLMime), mUris[aItemIndex].get(),
        mUris[aItemIndex].Length() * 2, getter_AddRefs(genericDataWrapper));

    return NS_SUCCEEDED(
        aTransferable->SetTransferData(kURLMime, genericDataWrapper));
  }

  if (IsImageFlavor()) {
    LOGDRAGSTATIC("  export image %s", flavorName.get());
    nsCOMPtr<nsIInputStream> byteStream;
    NS_NewByteInputStream(getter_AddRefs(byteStream),
                          mozilla::Span((char*)mDragData.get(), mDragDataLen),
                          NS_ASSIGNMENT_COPY);
    return NS_SUCCEEDED(
        aTransferable->SetTransferData(flavorName.get(), byteStream));
  }

  if (IsTextFlavor()) {
    LOGDRAGSTATIC("  export text %s", kTextMime);

    // We get text flavors as UTF8 but we export them as UTF16.
    if (mData.IsEmpty() && mDragDataLen) {
      mData = UTF8ToNewString(static_cast<const char*>(mDragData.get()),
                              mDragDataLen);
    }

    // put it into the transferable.
    nsCOMPtr<nsISupports> genericDataWrapper;
    nsPrimitiveHelpers::CreatePrimitiveForData(
        nsAutoCString(kTextMime), mData.get(), mData.Length() * 2,
        getter_AddRefs(genericDataWrapper));

    return NS_SUCCEEDED(
        aTransferable->SetTransferData(kTextMime, genericDataWrapper));
  }

  // text/html from external apps arrives as raw bytes in a charset that may
  // not be UTF-16.  Detect and decode it just as the clipboard code does.
  if (nsDependentCString(flavorName.get()).EqualsLiteral(kHTMLMime) &&
      mDragData && mDragDataLen) {
    LOGDRAGSTATIC("  export HTML, decoding charset");
    mozilla::Span<const char> span(static_cast<const char*>(mDragData.get()),
                                   mDragDataLen);
    nsAutoString unicodeData;
    if (!mozilla::widget::DecodeHTMLData(span, unicodeData)) {
      LOGDRAGSTATIC("  failed to decode HTML data");
      return false;
    }
    nsCOMPtr<nsISupports> genericDataWrapper;
    nsPrimitiveHelpers::CreatePrimitiveForData(
        nsLiteralCString(kHTMLMime), (const char*)unicodeData.BeginReading(),
        unicodeData.Length() * sizeof(char16_t),
        getter_AddRefs(genericDataWrapper));
    return NS_SUCCEEDED(
        aTransferable->SetTransferData(kHTMLMime, genericDataWrapper));
  }

  // We export obtained data directly from Gtk. In such case only
  // update line endings to DOM format.
  if (!mDragDataDOMEndings &&
      mDataFlavor != nsDragSession::sCustomTypesMimeAtom) {
    mDragDataDOMEndings = true;
    void* tmpData = mDragData.release();
    nsLinebreakHelpers::ConvertPlatformToDOMLinebreaks(
        mDataFlavor == nsDragSession::sRTFMimeAtom, &tmpData,
        (int32_t*)&mDragDataLen);
    mDragData.reset(tmpData);
  }

  // put it into the transferable.
  nsCOMPtr<nsISupports> genericDataWrapper;
  nsPrimitiveHelpers::CreatePrimitiveForData(
      nsDependentCString(flavorName.get()), mDragData.get(), mDragDataLen,
      getter_AddRefs(genericDataWrapper));

  return NS_SUCCEEDED(
      aTransferable->SetTransferData(flavorName.get(), genericDataWrapper));
}

RefPtr<DragData> DragData::ConvertToMozURL() const {
  // "text/uri-list" is exported as URI mime type by Gtk, perhaps in UTF8.
  // We convert it to "text/x-moz-url" which is UTF16 with line breaks.
  if (mDataFlavor == nsDragSession::sTextUriListTypeAtom) {
    MOZ_ASSERT(mAsURIData && mDragUris);
    LOGDRAGSTATIC("ConvertToMozURL(): text/uri-list => text/x-moz-url");

    RefPtr<DragData> data = new DragData(nsDragSession::sURLMimeAtom);
    data->mAsURIData = true;

    int len = g_strv_length(mDragUris.get());
    for (int i = 0; i < len; i++) {
      data->mUris.AppendElement(UTF8ToNewString(mDragUris.get()[i]));
    }
    return data;
  }

  // MozUrlType (_NETSCAPE_URL) MIME is not registered as URI MIME byt Gtk
  // is it exports it as plain data.  We convert it to "text/x-moz-url"
  // which is UTF16 with line breaks.
  if (mDataFlavor == nsDragSession::sMozUrlTypeAtom) {
    MOZ_ASSERT(mDragData);
    LOGDRAGSTATIC("ConvertToMozURL(): _NETSCAPE_URL => text/x-moz-url");

    RefPtr<DragData> data = new DragData(nsDragSession::sURLMimeAtom);
    data->mAsURIData = true;
    data->mUris.AppendElement(
        UTF8ToNewString((const char*)mDragData.get(), mDragDataLen));
    return data;
  }

  LOGDRAGSTATIC("ConvertToMozURL(): failed, wrong MIME %s to convert!",
                GUniquePtr<gchar>(gdk_atom_name(mDataFlavor)).get());
  return nullptr;
}

RefPtr<DragData> DragData::ConvertToFile() const {
  // "text/uri-list" is exported as URI mime type by Gtk, perhaps in UTF8.
  // We convert it to application/x-moz-file.
  if (mDataFlavor != nsDragSession::sTextUriListTypeAtom) {
    return nullptr;
  }
  MOZ_ASSERT(mAsURIData && mDragUris);

  // We can use text/uri-list directly as application/x-moz-file
  return new DragData(nsDragSession::sFileMimeAtom,
                      GUniquePtr<char*>(g_strdupv(mDragUris.get())));
}

static int CopyURI(const nsAString& aSourceURL, nsAString& aTargetURL,
                   int aOffset, bool aRequestNewLine) {
  int32_t uriEnd = aSourceURL.FindChar(u'\n', aOffset);
  if (uriEnd == aOffset) {
    return aOffset + 1;
  }
  if (uriEnd < 0) {
    if (aRequestNewLine) {
      return uriEnd;
    }
    // We may miss newline ending on URL title which is correct
    uriEnd = aSourceURL.Length();
  }

  int32_t newOffset = uriEnd + 1;

  if (aSourceURL[uriEnd - 1] == u'\r') {
    uriEnd--;
  }

  nsDependentSubstring url(aSourceURL, aOffset, uriEnd - aOffset);
  if (aRequestNewLine) {
    url.AppendLiteral("\n");
  }
  aTargetURL.Append(url);

  return newOffset;
}

// It holds the URLs of links followed by their titles,
// separated by a linebreak.
void DragData::ConvertToMozURIList() {
  if (mDataFlavor != nsDragSession::sURLMimeAtom) {
    return;
  }
  mAsURIData = true;

  const nsDependentSubstring uris((char16_t*)mDragData.get(), mDragDataLen / 2);

  LOGDRAGSTATIC("DragData::ConvertToMozURIList(), data %s",
                NS_ConvertUTF16toUTF8(uris).get());

  int32_t uriBegin = 0;
  do {
    nsAutoString uri;
    // First line contains URL and is terminated by newline
    if ((uriBegin = CopyURI(uris, uri, uriBegin, /* aRequestNewLine */ true)) <
        0) {
      break;
    }
    // Second line is URL title and may be terminated by newline
    if ((uriBegin = CopyURI(uris, uri, uriBegin, /* aRequestNewLine */ false)) <
        0) {
      break;
    }

    LOGDRAGSTATIC("  URI: %s", NS_ConvertUTF16toUTF8(uri).get());
    mUris.AppendElement(uri);
  } while (uriBegin < (int32_t)uris.Length());

  mDragData = nullptr;
  mDragDataLen = 0;
}

DragData::DragData(GdkAtom aDataFlavor, GUniquePtr<char*> aDragUris)
    : mDataFlavor(aDataFlavor),
      mAsURIData(true),
      mDragUris(std::move(aDragUris)) {}

bool DragData::IsDataValid() const {
  if (mDragData) {
    return mDragData.get() && mDragDataLen;
  } else if (mDragUris) {
    return !!(mDragUris.get()[0]);
  } else {
    return mUris.Length();
  }
}

#ifdef MOZ_LOGGING
void DragData::Print() const {
  if (mDragData) {
    if (IsTextFlavor()) {
      nsCString text((char*)mDragData.get(), mDragDataLen);
      LOGDRAGSTATIC("DragData() plain data MIME: %s : %s",
                    GUniquePtr<gchar>(gdk_atom_name(mDataFlavor)).get(),
                    (char*)text.get());
    }
    if (IsURIFlavor()) {
      nsString text((char16_t*)mDragData.get(), mDragDataLen / 2);
      LOGDRAGSTATIC("DragData() plain data MIME: %s : %s",
                    GUniquePtr<gchar>(gdk_atom_name(mDataFlavor)).get(),
                    NS_ConvertUTF16toUTF8(text).get());
    }
  } else if (mDragUris) {
    LOGDRAGSTATIC("DragData() URI MIME %s",
                  GUniquePtr<gchar>(gdk_atom_name(mDataFlavor)).get());
    if (MOZ_LOG_TEST(gWidgetDragLog, mozilla::LogLevel::Debug)) {
      int i = 0;
      for (gchar** uri = mDragUris.get(); uri && *uri; uri++, i++) {
        LOGDRAGSTATIC("%d URI %s", i, *uri);
      }
    }
  } else if (mUris.Length()) {
    LOGDRAGSTATIC("DragData() URI MIME: %s len %d",
                  GUniquePtr<gchar>(gdk_atom_name(mDataFlavor)).get(),
                  (int)mUris.Length());
    for (size_t i = 0; i < mUris.Length(); i++) {
      LOGDRAGSTATIC("%d URI:\n%s", (int)i,
                    NS_ConvertUTF16toUTF8(mUris[i]).get());
    }
  } else {
    LOGDRAGSTATIC("DragData() MIME %s is missing data",
                  GUniquePtr<gchar>(gdk_atom_name(mDataFlavor)).get());
  }
}
#endif

/* static */ int nsDragSession::sEventLoopDepth = 0;

nsDragSession::nsDragSession() {
  LOGDRAG("nsDragSession::nsDragSession()");

  static std::once_flag onceFlag;
  std::call_once(onceFlag, [] {
    sJPEGImageMimeAtom = gdk_atom_intern(kJPEGImageMime, FALSE);
    sJPGImageMimeAtom = gdk_atom_intern(kJPGImageMime, FALSE);
    sPNGImageMimeAtom = gdk_atom_intern(kPNGImageMime, FALSE);
    sGIFImageMimeAtom = gdk_atom_intern(kGIFImageMime, FALSE);
    sCustomTypesMimeAtom = gdk_atom_intern(kCustomTypesMime, FALSE);
    sURLMimeAtom = gdk_atom_intern(kURLMime, FALSE);
    sRTFMimeAtom = gdk_atom_intern(kRTFMime, FALSE);
    sTextMimeAtom = gdk_atom_intern(kTextMime, FALSE);
    sMozUrlTypeAtom = gdk_atom_intern(gMozUrlType, FALSE);
    sMimeListTypeAtom = gdk_atom_intern(gMimeListType, FALSE);
    sTextUriListTypeAtom = gdk_atom_intern(gTextUriListType, FALSE);
    sTextPlainUTF8TypeAtom = gdk_atom_intern(gTextPlainUTF8Type, FALSE);
    sXdndDirectSaveTypeAtom = gdk_atom_intern(gXdndDirectSaveType, FALSE);
    sTabDropTypeAtom = gdk_atom_intern(gTabDropType, FALSE);
    sFileMimeAtom = gdk_atom_intern(kFileMime, FALSE);
    sPortalFileAtom = gdk_atom_intern(gPortalFile, FALSE);
    sPortalFileTransferAtom = gdk_atom_intern(gPortalFileTransfer, FALSE);
    sFilePromiseURLMimeAtom = gdk_atom_intern(kFilePromiseURLMime, FALSE);
    sFilePromiseMimeAtom = gdk_atom_intern(kFilePromiseMime, FALSE);
    sNativeImageMimeAtom = gdk_atom_intern(kNativeImageMime, FALSE);
    sUTF8STRINGMimeAtom = gdk_atom_intern(gUTF8STRINGType, FALSE);
    sSTRINGMimeAtom = gdk_atom_intern(gSTRINGType, FALSE);
  });
}

nsDragSession::~nsDragSession() {
  LOGDRAG("nsDragSession::~nsDragSession");
  if (mTaskSource) g_source_remove(mTaskSource);
}

NS_IMPL_ISUPPORTS_INHERITED0(nsDragSession, nsBaseDragSession)

mozilla::StaticRefPtr<nsDragService> sDragServiceInstance;
/* static */
already_AddRefed<nsDragService> nsDragService::GetInstance() {
  if (gfxPlatform::IsHeadless()) {
    return nullptr;
  }
  if (!sDragServiceInstance) {
    sDragServiceInstance = MakeRefPtr<nsDragService>();
    ClearOnShutdown(&sDragServiceInstance);
  }

  RefPtr<nsDragService> service = sDragServiceInstance.get();
  return service.forget();
}

nsDragService::nsDragService() = default;

already_AddRefed<nsIDragSession> nsDragService::CreateDragSession() {
#ifdef MOZ_WAYLAND
  if (StaticPrefs::widget_wayland_native_data_session_AtStartup() &&
      widget::GdkIsWaylandDisplay()) {
    return MakeAndAddRef<nsDragSessionWayland>();
  }
#endif
  return MakeAndAddRef<nsDragSessionGtk>();
}

nsIDragSession* nsDragService::StartDragSession(nsISupports* aWidgetProvider) {
  return nsBaseDragService::StartDragSession(aWidgetProvider);
}

nsresult nsDragSession::EndDragSessionImpl(bool aDoneDrag,
                                           uint32_t aKeyModifiers) {
  LOGDRAG("nsDragSession::EndDragSessionImpl() %d", aDoneDrag);

  SetDragAction(nsIDragService::DRAGDROP_ACTION_NONE);
  mCachedDragContextID = 0;
  mNextScheduledTask = nullptr;
  mRecentTask->Reset();

  EndDragSessionImplBackend();

  return nsBaseDragSession::EndDragSessionImpl(aDoneDrag, aKeyModifiers);
}

// nsIDragSession
NS_IMETHODIMP
nsDragSession::SetCanDrop(bool aCanDrop) {
  LOGDRAG("nsDragSession::SetCanDrop %d", aCanDrop);
  mCanDrop = aCanDrop;
  return NS_OK;
}

NS_IMETHODIMP
nsDragSession::GetCanDrop(bool* aCanDrop) {
  LOGDRAG("nsDragSession::GetCanDrop");
  *aCanDrop = mCanDrop;
  return NS_OK;
}

// Spins event loop, called from JS.
// Can lead to another round of drag_motion events.
NS_IMETHODIMP
nsDragSession::GetNumDropItems(uint32_t* aNumItems) {
  LOGDRAG("nsDragSession::GetNumDropItems");

  GtkWidget* widget =
      mRecentTask->mWindow ? mRecentTask->mWindow->GetGtkWidget() : nullptr;
  if (!widget) {
    LOGDRAG(
        "*** warning: GetNumDropItems \
               called without a valid target widget!\n");
    *aNumItems = 0;
    return NS_OK;
  }

  if (IsTargetContextList()) {
    if (!mSourceDataItems) {
      *aNumItems = 0;
      return NS_OK;
    }
    mSourceDataItems->GetLength(aNumItems);
    LOGDRAG("GetNumDropItems(): TargetContextList items %d", *aNumItems);
    return NS_OK;
  }

  // Put text/uri-list first, text/x-moz-url tends to be poorly supported
  // by third party apps, we got only one file instead of file list
  // for instance (Bug 1908196).
  //
  // We're getting the data to only get number of items here,
  // actual data will be received at nsDragSession::GetData().
  const GdkAtom fileListFlavors[] = {sTextUriListTypeAtom,  // text/uri-list
                                     sPortalFileAtom, sPortalFileTransferAtom,
                                     sURLMimeAtom};  // text/x-moz-url

  for (auto fileFlavour : fileListFlavors) {
    RefPtr<DragData> data = GetDragData(fileFlavour);
    if (data) {
      *aNumItems = data->GetURIsNum();
      LOGDRAG("GetNumDropItems(): Found MIME %s items %d",
              GUniquePtr<gchar>(gdk_atom_name(fileFlavour)).get(), *aNumItems);
      return NS_OK;
    }
  }

  // We're missing any file list MIME, return only one item.
  *aNumItems = 1;
  LOGDRAG("GetNumDropItems(): no list available");
  return NS_OK;
}

// Spins event loop, called from JS.
// Can lead to another round of drag_motion events.
NS_IMETHODIMP
nsDragSession::GetData(nsITransferable* aTransferable, uint32_t aItemIndex) {
  LOGDRAG("nsDragSession::GetData(), index %d", aItemIndex);

  // make sure that we have a transferable
  if (!aTransferable) {
    return NS_ERROR_INVALID_ARG;
  }

  GtkWidget* widget =
      mRecentTask->mWindow ? mRecentTask->mWindow->GetGtkWidget() : nullptr;
  if (!widget) {
    LOGDRAG("*** failed: GetData called without a valid target widget!\n");
    return NS_ERROR_FAILURE;
  }

  // get flavor list that includes all acceptable flavors (including
  // ones obtained through conversion).
  nsTArray<nsCString> flavors;
  nsresult rv = aTransferable->FlavorsTransferableCanImport(flavors);
  if (NS_FAILED(rv)) {
    LOGDRAG("  failed to get flavors, quit.");
    return rv;
  }

  // check to see if this is an internal list
  if (IsTargetContextList()) {
    LOGDRAG("  Process as a list...");
    // find a matching flavor
    for (uint32_t i = 0; i < flavors.Length(); ++i) {
      nsCString& flavorStr = flavors[i];
      LOGDRAG("  [%d] flavor is %s\n", i, flavorStr.get());
      // get the item with the right index
      nsCOMPtr<nsITransferable> item =
          do_QueryElementAt(mSourceDataItems, aItemIndex);
      if (!item) continue;

      nsCOMPtr<nsISupports> data;
      LOGDRAG("  trying to get transfer data for %s\n", flavorStr.get());
      rv = item->GetTransferData(flavorStr.get(), getter_AddRefs(data));
      if (NS_FAILED(rv)) {
        LOGDRAG("  failed.\n");
        continue;
      }
      rv = aTransferable->SetTransferData(flavorStr.get(), data);
      if (NS_FAILED(rv)) {
        LOGDRAG("  fail to set transfer data into transferable!\n");
        continue;
      }
      LOGDRAG("  succeeded\n");
      // ok, we got the data
      return NS_OK;
    }
    // if we got this far, we failed
    LOGDRAG("  failed to match flavors\n");
    return NS_ERROR_FAILURE;
  }

  // Now walk down the list of flavors. When we find one that is
  // actually present, copy out the data into the transferable in that
  // format. SetTransferData() implicitly handles conversions.
  for (uint32_t i = 0; i < flavors.Length(); ++i) {
    nsCString& flavorStr = flavors[i];

    GdkAtom requestedFlavor = gdk_atom_intern(flavorStr.get(), FALSE);
    if (!requestedFlavor) {
      continue;
    }

    LOGDRAG("  we're getting data %s\n", flavorStr.get());

    RefPtr<DragData> dragData;

    // Let's do conversions first. We may be asked for some kind of MIME
    // type data but we rather try to get something different and
    // convert to desider MIME type.

    // We're asked to get text data. Try to get UTF-8 variant first.
    if (requestedFlavor == sTextMimeAtom) {
      dragData = GetDragData(sTextPlainUTF8TypeAtom);
    }

    // Try portals first since text/uri-list URIs in sandboxed environments
    // (Flatpak/Snap) may point to inaccessible file paths.
    if (requestedFlavor == sURLMimeAtom || requestedFlavor == sFileMimeAtom) {
      LOGDRAG("  try portals first\n");
      dragData = GetDragData(sPortalFileAtom);
      if (!dragData) {
        dragData = GetDragData(sPortalFileTransferAtom);
      }
    }

    // We are looking for text/x-moz-url. That format may be poorly supported,
    // try first with text/uri-list, and then _NETSCAPE_URL
    if (!dragData && requestedFlavor == sURLMimeAtom) {
      LOGDRAG("  conversion %s => %s", gTextUriListType, kURLMime);
      dragData = GetDragData(sTextUriListTypeAtom);
      if (dragData) {
        dragData = dragData->ConvertToMozURL();
        mCachedDragData.InsertOrUpdate(dragData->GetFlavor(), dragData);
      }
      if (!dragData) {
        LOGDRAG("  conversion %s => %s", gMozUrlType, kURLMime);
        dragData = GetDragData(sMozUrlTypeAtom);
        if (dragData) {
          dragData = dragData->ConvertToMozURL();
          if (dragData) {
            mCachedDragData.InsertOrUpdate(dragData->GetFlavor(), dragData);
          }
        }
      }
    }

    // Try to get requested MIME directly
    if (!dragData) {
      dragData = GetDragData(requestedFlavor);
    }

    // We're asked to get file mime type but we failed.
    // Try text/uri-list conversion.
    if (!dragData && requestedFlavor == sFileMimeAtom) {
      LOGDRAG("  file not found, proceed with conversion %s => %s flavor\n",
              gTextUriListType, kFileMime);
      // Conversion text/uri-list => application/x-moz-file
      dragData = GetDragData(sTextUriListTypeAtom);
      if (dragData) {
        dragData = dragData->ConvertToFile();
        if (dragData) {
          mCachedDragData.InsertOrUpdate(dragData->GetFlavor(), dragData);
        }
      }
    }

    if (dragData && dragData->Export(aTransferable, aItemIndex)) {
      // We usually want to also get URL for images so continue
      if (dragData->IsImageFlavor()) {
        continue;
      }
      return NS_OK;
    }
  }

  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsDragSession::IsDataFlavorSupported(const char* aDataFlavor, bool* _retval) {
  LOGDRAG("nsDragSession::IsDataFlavorSupported() %s", aDataFlavor);
  if (!_retval) {
    return NS_ERROR_INVALID_ARG;
  }

  // set this to no by default
  *_retval = false;

  // check to make sure that we have a drag object set, here
  GtkWidget* widget =
      mRecentTask->mWindow ? mRecentTask->mWindow->GetGtkWidget() : nullptr;
  if (!widget) {
    LOGDRAG(
        "*** warning: IsDataFlavorSupported called without a valid target "
        "widget!\n");
    return NS_OK;
  }

  // check to see if the target context is a list.
  // if it is, just look in the internal data since we are the source
  // for it.
  if (IsTargetContextList()) {
    LOGDRAG("  It's a list");
    uint32_t numDragItems = 0;
    // if we don't have mDataItems we didn't start this drag so it's
    // an external client trying to fool us.
    if (!mSourceDataItems) {
      LOGDRAG("  quit");
      return NS_OK;
    }
    mSourceDataItems->GetLength(&numDragItems);
    LOGDRAG("  drag items %d", numDragItems);
    for (uint32_t itemIndex = 0; itemIndex < numDragItems; ++itemIndex) {
      nsCOMPtr<nsITransferable> currItem =
          do_QueryElementAt(mSourceDataItems, itemIndex);
      if (currItem) {
        nsTArray<nsCString> flavors;
        currItem->FlavorsTransferableCanExport(flavors);
        for (uint32_t i = 0; i < flavors.Length(); ++i) {
          LOGDRAG("  checking %s against %s\n", flavors[i].get(), aDataFlavor);
          if (flavors[i].Equals(aDataFlavor)) {
            LOGDRAG("  found.\n");
            *_retval = true;
          }
        }
      }
    }
    return NS_OK;
  }

  GdkAtom requestedFlavor = gdk_atom_intern(aDataFlavor, FALSE);
  if (IsDragFlavorAvailable(requestedFlavor)) {
    LOGDRAG("  %s is supported", aDataFlavor);
    *_retval = true;
    return NS_OK;
  }

  // GetData can convert text/plain;charset=utf-8 to text/plain, so report it
  // as supported here too.
  if (requestedFlavor == sTextMimeAtom &&
      IsDragFlavorAvailable(sTextPlainUTF8TypeAtom)) {
    LOGDRAG("  %s supported with conversion from %s", aDataFlavor,
            gTextPlainUTF8Type);
    *_retval = true;
    return NS_OK;
  }

  // Check for file/url conversion from uri list
  if ((requestedFlavor == sURLMimeAtom || requestedFlavor == sFileMimeAtom) &&
      IsDragFlavorAvailable(sTextUriListTypeAtom)) {
    LOGDRAG("  %s supported with conversion from %s", aDataFlavor,
            gTextUriListType);
    *_retval = true;
    return NS_OK;
  }

  // check for automatic _NETSCAPE_URL -> text/x-moz-url mapping
  if (requestedFlavor == sURLMimeAtom &&
      IsDragFlavorAvailable(sMozUrlTypeAtom)) {
    LOGDRAG("  %s supported with conversion from %s", aDataFlavor, gMozUrlType);
    *_retval = true;
    return NS_OK;
  }

  // If we're asked for kURLMime/kFileMime we can get it from PortalFile
  // or PortalFileTransfer flavors.
  if ((requestedFlavor == sURLMimeAtom || requestedFlavor == sFileMimeAtom) &&
      (IsDragFlavorAvailable(sPortalFileAtom) ||
       IsDragFlavorAvailable(sPortalFileTransferAtom))) {
    LOGDRAG("  %s supported with conversion from %s/%s", aDataFlavor,
            gPortalFile, gPortalFileTransfer);
    *_retval = true;
    return NS_OK;
  }

  LOGDRAG("  %s is not supported", aDataFlavor);
  return NS_OK;
}

void nsDragSession::SetCachedDragContext(uintptr_t aDragContextID) {
  LOGDRAG("nsDragSession::SetCachedDragContext(): [drag %p / cached %p]",
          (void*)aDragContextID, (void*)mCachedDragContextID);
  // Clear cache data if we're going to D&D with different drag context.
  if (aDragContextID && aDragContextID != mCachedDragContextID) {
    LOGDRAG("  cache clear, new context %p", (void*)aDragContextID);
    mCachedDragContextID = aDragContextID;
    mCachedDragData.Clear();
    mCachedDragFlavors.Clear();
  }
}

// Spins event loop, called from eDragTaskMotion handler by
// DispatchMotionEvents().
// Can lead to another round of drag_motion events.
RefPtr<DragData> nsDragSession::GetDragData(GdkAtom aRequestedFlavor) {
  LOGDRAG("nsDragSession::GetDragData() requested '%s'\n",
          GUniquePtr<gchar>(gdk_atom_name(aRequestedFlavor)).get());

  // Return early when requested MIME is not offered by D&D.
  if (!IsDragFlavorAvailable(aRequestedFlavor)) {
    LOGDRAG("  %s is missing",
            GUniquePtr<gchar>(gdk_atom_name(aRequestedFlavor)).get());
    return nullptr;
  }

  {
    auto data = mCachedDragData.MaybeGet(GDK_ATOM_TO_POINTER(aRequestedFlavor));
    if (data) {
      LOGDRAG("  MIME %s found in cache, %s",
              GUniquePtr<gchar>(gdk_atom_name(aRequestedFlavor)).get(),
              *data ? "got correctly" : "failed to get");
      return *data;
    }
  }

  if (!GetDragDataImpl(aRequestedFlavor)) {
    LOGDRAG("  %s failed to get from system",
            GUniquePtr<gchar>(gdk_atom_name(aRequestedFlavor)).get());
    return nullptr;
  }

  RefPtr<DragData> data =
      mCachedDragData.Get(GDK_ATOM_TO_POINTER(aRequestedFlavor));
  if (!data) {
    NS_WARNING(nsPrintfCString(
                   "nsDragSession::GetDragData() %s failed to get from cache",
                   GUniquePtr<gchar>(gdk_atom_name(aRequestedFlavor)).get())
                   .get());
    return nullptr;
  }

  LOGDRAG("  %s received",
          GUniquePtr<gchar>(gdk_atom_name(aRequestedFlavor)).get());
  return data;
}

#ifdef MOZ_LOGGING
const char* nsDragSession::GetDragServiceTaskName(DragTaskType aTask) {
  static const char* taskNames[] = {"eDragTaskNone", "eDragTaskMotion",
                                    "eDragTaskLeave", "eDragTaskDrop"};
  MOZ_ASSERT(size_t(aTask) < std::size(taskNames));
  return taskNames[aTask];
}
#endif

void nsDragSession::DispatchMotionEvents() {
  if (mSourceWindow) {
    FireDragEventAtSource(eDrag, GetCurrentModifiers());
  }
  if (mRecentTask->mWindow) {
    mRecentTask->mWindow->DispatchDragEvent(
        eDragOver, mRecentTask->mWindowPoint, mRecentTask->mTime);
  }
}

void nsDragSession::DispatchDropEvent() {
  // We need to check IsDestroyed here because the nsRefPtr
  // only protects this from being deleted, it does NOT protect
  // against nsView::~nsView() calling Destroy() on it, bug 378273.
  if (!mRecentTask->mWindow || mRecentTask->mWindow->IsDestroyed()) {
    return;
  }

  EventMessage msg = mCanDrop ? eDrop : eDragExit;

  mRecentTask->mWindow->DispatchDragEvent(msg, mRecentTask->mWindowPoint,
                                          mRecentTask->mTime);

  // Deliver message to OS
  DropFinish(mCanDrop);
}

int nsDragSession::RunScheduledTaskCallback(void* aData) {
  RefPtr<nsDragSession> dragSession = static_cast<nsDragSession*>(aData);
  nsDragSession::AutoEventLoop loop(dragSession);
  return dragSession->RunScheduledTask();
}

gboolean nsDragSession::Schedule(UniquePtr<DragTask> aTask) {
  LOGDRAG("nsDragSession::Schedule()");
  if (mDragTaskSourceFinished) {
    LOGDRAG("  already finished, quit.");
    return FALSE;
  }

  // If there is an existing leave or motion task scheduled, then that
  // will be replaced.  When the new task is run, it will dispatch
  // any necessary leave or motion events.
  // We don't replace eDragTaskDrop as it's final D&D event.
  if (mNextScheduledTask && mNextScheduledTask->mType == eDragTaskDrop) {
    LOGDRAG("   eDragTaskDrop is a final one, can't be replaced by %s",
            GetDragServiceTaskName(aTask->mType));
    return FALSE;
  }

  // A leave task carries no window, and mSourceWindow is only set on Wayland,
  // so the null test is not redundant. Set here and not in RunScheduledTask(),
  // which runs from a timeout that may still be pending when the drag ends.
  mDragIsOverSourceWindow = aTask->mWindow && aTask->mWindow == mSourceWindow;

  mNextScheduledTask = std::move(aTask);

  if (!mTaskSource) {
    // High priority is used here because we want to process motion events
    // right after drag_motion event handler which is called by Gtk.
    // An ideal scenario is to call RunScheduledTask() directly here
    // but we can't do that. RunScheduledTask() spins gtk event loop
    // while nsDragSession::Schedule() is already called from event loop
    // (by drag_motion* gtk_widget events) so that direct call will cause
    // nested recursion.
    // RunScheduledTask returns false to stop getting callbacks
    // from mTaskSource.
    mTaskSource = g_timeout_add_full(G_PRIORITY_HIGH, 0,
                                     RunScheduledTaskCallback, this, nullptr);
  }

  // We need to reply to drag_motion event on Wayland immediately,
  // see Bug 1730203.
  if (widget::GdkIsWaylandDisplay() &&
      mNextScheduledTask->mType == eDragTaskMotion) {
    UpdateDragAction();
    ReplyToDragMotion();
  }

  return TRUE;
}

gboolean nsDragSession::RunScheduledTask() {
  // No task is present - remove the timer.
  if (!mNextScheduledTask || mDragTaskSourceFinished) {
    LOGDRAG(
        "nsDragSession::RunScheduledTask(): no task is scheduled or it's "
        "finished [%d], quit.",
        mDragTaskSourceFinished);
    mTaskSource = 0;
    return false;
  }

  // Don't run RunScheduledTask() twice. As we use it in main thread only
  // we don't need to be thread safe here. But don't remove mTaskSource -
  // call the RunScheduledTask() again later when recent task is finished.
  if (mScheduledTaskIsRunning) {
    return true;
  }

  AutoRestore<bool> guard(mScheduledTaskIsRunning);
  mScheduledTaskIsRunning = true;

  LOGDRAG("nsDragSession::RunScheduledTask() begin");

  RunScheduledTask(std::move(mNextScheduledTask));

  LOGDRAG("nsDragSession::RunScheduledTask() end");

  // We have pending task (may be added by nested loops
  // at RunScheduledTask()) - ask for next timer callback.
  if (mNextScheduledTask) {
    return true;
  }

  // We don't have any additional task in queue so terminate the timer.
  mTaskSource = 0;
  return false;
}

void nsDragSession::RunScheduledTask(
    mozilla::UniquePtr<DragTask> aScheduledTask) {
  MOZ_DIAGNOSTIC_ASSERT(
      mScheduledTaskIsRunning,
      "Running outside of nsDragSession::RunScheduledTask()?");

  LOGDRAG(
      "nsDragSession::RunScheduledTask() task %s recent Window %p scheduled "
      "Window %p\n",
      GetDragServiceTaskName(aScheduledTask->mType), mRecentTask->mWindow.get(),
      aScheduledTask->mWindow.get());

  if (mRecentTask->mWindow && mRecentTask->mWindow != aScheduledTask->mWindow) {
    LOGDRAG("  window changed, dispatch eDragExit to leaved window (%p)\n",
            mRecentTask->mWindow.get());
    mRecentTask->mWindow->DispatchDragEvent(eDragExit,
                                            aScheduledTask->mWindowPoint, 0);

    if (!mSourceNode) {
      // The drag that was initiated in a different app. End the drag
      // session, since we're done with it for now (until the user drags
      // back into this app).
      EndDragSession(false, GetCurrentModifiers());
    }
  }

  if (aScheduledTask->mType == eDragTaskLeave) {
    LOGDRAG("  quit, selected task %s\n",
            GetDragServiceTaskName(aScheduledTask->mType));
    // Nothing more to do
    // Returning false removes the task source from the event loop.
    mRecentTask->Reset();
    return;
  }
  // We process eDragTaskMotion or eDragTaskDrop from now.

  // It is possible that the pending state has been updated during dispatch
  // of the leave event.  That's fine.

  // Now we collect the pending state because, from this point on, we want
  // to use the same state for all events dispatched.  All state is updated
  // so that when other tasks are scheduled during dispatch here, this
  // task is considered to have already been run.
  bool positionHasChanged =
      aScheduledTask->mWindow != mRecentTask->mWindow ||
      aScheduledTask->mWindowPoint != mRecentTask->mWindowPoint;

  mRecentTask = std::move(aScheduledTask);

  LOGDRAG(
      "  start drag session Window %p GtkWidget %p", mRecentTask->mWindow.get(),
      mRecentTask->mWindow ? mRecentTask->mWindow->GetGtkWidget() : nullptr);

  SetCachedDragContext(mRecentTask->GetContextID());

  // http://www.whatwg.org/specs/web-apps/current-work/multipage/dnd.html#drag-and-drop-processing-model
  // (as at 27 December 2010) indicates that a "drop" event should only be
  // fired (at the current target element) if the current drag operation is
  // not none.  The current drag operation will only be set to a non-none
  // value during a "dragover" event.
  //
  // If the user has ended the drag before any dragover events have been
  // sent, then the spec recommends skipping the drop (because the current
  // drag operation is none).  However, here we assume that, by releasing
  // the mouse button, the user has indicated that they want to drop, so we
  // proceed with the drop where possible.
  //
  // In order to make the events appear to content in the same way as if the
  // spec is being followed we make sure to dispatch a "dragover" event with
  // appropriate coordinates and check canDrop before the "drop" event.
  //
  // When the Xdnd protocol is used for source/destination communication (as
  // should be the case with GTK source applications) a dragover event
  // should have already been sent during the drag-motion signal, which
  // would have already been received because XdndDrop messages do not
  // contain a position.  However, we can't assume the same when the Motif
  // protocol is used.
  if (mRecentTask->mType == eDragTaskMotion || positionHasChanged) {
    LOGDRAG("  process motion event\n");
    UpdateDragAction();
    TakeDragEventDispatchedToChildProcess();  // Clear the old value.
    DispatchMotionEvents();
    if (mRecentTask->mType == eDragTaskMotion) {
      if (TakeDragEventDispatchedToChildProcess()) {
        SetRemoteContext();
      } else {
        // Reply to tell the source whether we can drop and what
        // action would be taken.
        ReplyToDragMotion();
      }
    }
  }

  if (mRecentTask->mType == eDragTaskDrop) {
    LOGDRAG("  process drop task\n");
    DispatchDropEvent();

    // Make sure to end the drag session. If this drag started in a
    // different app, we won't get a drag_end signal to end it from.
    EndDragSession(true, GetCurrentModifiers());
  }
}

void nsDragSession::SetDragActionGtk(GdkDragAction aGdkAction) {
  LOGDRAG("nsDragSession::SetDragActionGtk() action [%d]", aGdkAction);

  // default is to do nothing
  int action = nsIDragService::DRAGDROP_ACTION_NONE;

  // set the default just in case nothing matches below
  if (aGdkAction & GDK_ACTION_DEFAULT) {
    LOGDRAG("nsDragSession::UpdateDragActionGtk(): set default move");
    action = nsIDragService::DRAGDROP_ACTION_MOVE;
  }
  // first check to see if move is set
  if (aGdkAction & GDK_ACTION_MOVE) {
    LOGDRAG("nsDragSession::UpdateDragActionGtk(): set explicit move");
    action = nsIDragService::DRAGDROP_ACTION_MOVE;
  } else if (aGdkAction & GDK_ACTION_LINK) {
    // then fall to the others
    LOGDRAG("nsDragSession::UpdateDragActionGtk(): set explicit link");
    action = nsIDragService::DRAGDROP_ACTION_LINK;
  } else if (aGdkAction & GDK_ACTION_COPY) {
    // copy is ctrl
    LOGDRAG("nsDragSession::UpdateDragActionGtk(): set explicit copy");
    action = nsIDragService::DRAGDROP_ACTION_COPY;
  }

  // update the drag information
  SetDragAction(action);
}

GdkDragAction nsDragSession::GetDragActionGtk() {
  GdkDragAction action = (GdkDragAction)0;
  if (mCanDrop) {
    // notify the dragger if we can drop
    switch (mDragAction) {
      case nsIDragService::DRAGDROP_ACTION_COPY:
        LOGDRAG("  set explicit action copy");
        action = GDK_ACTION_COPY;
        break;
      case nsIDragService::DRAGDROP_ACTION_LINK:
        LOGDRAG("  set explicit action link");
        action = GDK_ACTION_LINK;
        break;
      // if mCanDrop we switch DRAGDROP_ACTION_NONE to default.
      case nsIDragService::DRAGDROP_ACTION_NONE:
        LOGDRAG("  set explicit action none");
        action = (GdkDragAction)0;
        break;
      default:
        LOGDRAG("  set explicit action move");
        action = GDK_ACTION_MOVE;
        break;
    }
  }
  LOGDRAG(
      "nsDragSession::GetDragActionGtk() can drop %d mDragAction %d GdkAction "
      "%d",
      mCanDrop, mDragAction, action);
  return action;
}

/* static */
uint32_t nsDragSession::GetCurrentModifiers() {
  return mozilla::widget::KeymapWrapper::ComputeCurrentKeyModifiers();
}

nsAutoCString nsDragSession::GetDebugTag() const {
  nsAutoCString tag;
  tag.AppendPrintf("[%p]", this);
  return tag;
}

/* static */
bool nsDragSession::IsTextFlavor(GdkAtom aFlavor) {
  return aFlavor == nsDragSession::sTextMimeAtom ||
         aFlavor == nsDragSession::sTextPlainUTF8TypeAtom ||
         aFlavor == nsDragSession::sUTF8STRINGMimeAtom ||
         aFlavor == nsDragSession::sSTRINGMimeAtom;
}

nsDragSession::DragTask::DragTask(
    DragTaskType aType, nsWindow* aWindow,
    const mozilla::LayoutDeviceIntPoint& aWindowPoint, guint aTime)
    : mType(aType),
      mWindow(aWindow),
      mWindowPoint(aWindowPoint),
      mTime(aTime) {}

nsDragSession::DragTask::~DragTask() = default;

#undef LOGDRAG
#undef LOGDRAGSTATIC
