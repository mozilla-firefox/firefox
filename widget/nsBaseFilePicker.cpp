/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCOMPtr.h"
#include "nsPIDOMWindow.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIWidget.h"

#include "nsArrayEnumerator.h"
#include "nsIStringBundle.h"
#include "nsString.h"
#include "nsCOMArray.h"
#include "nsIFile.h"
#include "nsEnumeratorUtils.h"
#include "mozilla/dom/Directory.h"
#include "mozilla/dom/File.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Components.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/StaticPrefs_widget.h"
#include "WidgetUtils.h"
#include "nsSimpleEnumerator.h"
#include "nsContentUtils.h"
#include "nsThreadUtils.h"
#ifdef MOZ_WIDGET_GTK
#  include "mozilla/WidgetUtilsGtk.h"
#endif

#include "nsBaseFilePicker.h"

using namespace mozilla::widget;
using namespace mozilla::dom;
using mozilla::ErrorResult;

#define FILEPICKER_TITLES "chrome://global/locale/filepicker.properties"
#define FILEPICKER_FILTERS "chrome://global/content/filepicker.properties"

namespace {

nsresult LocalFileToDirectoryOrBlob(nsIGlobalObject* aGlobal, bool aIsDirectory,
                                    nsIFile* aFile, nsISupports** aResult) {
  MOZ_ASSERT(aGlobal);

  if (aIsDirectory) {
#ifdef DEBUG
    bool isDir;
    aFile->IsDirectory(&isDir);
    MOZ_ASSERT(isDir);
#endif

    RefPtr<Directory> directory = Directory::Create(aGlobal, aFile);
    MOZ_ASSERT(directory);

    directory.forget(aResult);
    return NS_OK;
  }

  RefPtr<File> file = File::CreateFromFile(aGlobal, aFile);
  if (NS_WARN_IF(!file)) {
    return NS_ERROR_FAILURE;
  }

  file.forget(aResult);
  return NS_OK;
}

}  // anonymous namespace

class nsBaseFilePickerEnumerator : public nsSimpleEnumerator {
 public:
  nsBaseFilePickerEnumerator(nsIGlobalObject* aGlobal,
                             nsISimpleEnumerator* iterator,
                             nsIFilePicker::Mode aMode)
      : mIterator(iterator), mGlobal(aGlobal), mMode(aMode) {}

  const nsID& DefaultInterface() override { return NS_GET_IID(nsIFile); }

  NS_IMETHOD
  GetNext(nsISupports** aResult) override {
    nsCOMPtr<nsISupports> tmp;
    nsresult rv = mIterator->GetNext(getter_AddRefs(tmp));
    NS_ENSURE_SUCCESS(rv, rv);

    if (!tmp) {
      return NS_OK;
    }

    nsCOMPtr<nsIFile> localFile = do_QueryInterface(tmp);
    if (!localFile) {
      return NS_ERROR_FAILURE;
    }

    if (!mGlobal) {
      return NS_ERROR_FAILURE;
    }

    return LocalFileToDirectoryOrBlob(
        mGlobal, mMode == nsIFilePicker::modeGetFolder, localFile, aResult);
  }

  NS_IMETHOD
  HasMoreElements(bool* aResult) override {
    return mIterator->HasMoreElements(aResult);
  }

 private:
  nsCOMPtr<nsISimpleEnumerator> mIterator;
  nsCOMPtr<nsIGlobalObject> mGlobal;
  nsIFilePicker::Mode mMode;
};

nsBaseFilePicker::nsBaseFilePicker() = default;

nsBaseFilePicker::~nsBaseFilePicker() = default;

NS_IMETHODIMP nsBaseFilePicker::Init(BrowsingContext* aBrowsingContext,
                                     const nsAString& aTitle,
                                     nsIFilePicker::Mode aMode,
                                     nsISupports* aGlobal) {
  MOZ_ASSERT(XRE_IsParentProcess());
  NS_ENSURE_ARG_POINTER(aBrowsingContext);

  nsCOMPtr<nsIWidget> widget =
      aBrowsingContext->Canonical()->GetParentProcessWidgetContaining();
  NS_ENSURE_TRUE(widget, NS_ERROR_FAILURE);

  mBrowsingContext = aBrowsingContext;
  mGlobal = do_QueryInterface(aGlobal);
  mMode = aMode;
  InitNative(widget, aTitle);

  return NS_OK;
}

NS_IMETHODIMP
nsBaseFilePicker::IsModeSupported(nsIFilePicker::Mode aMode, JSContext* aCx,
                                  Promise** aPromise) {
  MOZ_ASSERT(aCx);
  MOZ_ASSERT(aPromise);

  nsIGlobalObject* globalObject = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!globalObject)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult result;
  RefPtr<Promise> promise = Promise::Create(globalObject, result);
  if (NS_WARN_IF(result.Failed())) {
    return result.StealNSResult();
  }

  promise->MaybeResolve(true);
  promise.forget(aPromise);

  return NS_OK;
}

NS_IMETHODIMP
nsBaseFilePicker::AppendFilters(int32_t aFilterMask) {
  nsCOMPtr<nsIStringBundleService> stringService =
      mozilla::components::StringBundle::Service();
  if (!stringService) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIStringBundle> titleBundle, filterBundle;

  nsresult rv = stringService->CreateBundle(FILEPICKER_TITLES,
                                            getter_AddRefs(titleBundle));
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  rv = stringService->CreateBundle(FILEPICKER_FILTERS,
                                   getter_AddRefs(filterBundle));
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  nsAutoString title;
  nsAutoString filter;

  if (aFilterMask & filterAll) {
    titleBundle->GetStringFromName("allTitle", title);
    filterBundle->GetStringFromName("allFilter", filter);
    AppendFilter(title, filter);
  }
  if (aFilterMask & filterHTML) {
    titleBundle->GetStringFromName("htmlTitle", title);
    filterBundle->GetStringFromName("htmlFilter", filter);
    AppendFilter(title, filter);
  }
  if (aFilterMask & filterText) {
    titleBundle->GetStringFromName("textTitle", title);
    filterBundle->GetStringFromName("textFilter", filter);
    AppendFilter(title, filter);
  }
  if (aFilterMask & filterImages) {
    titleBundle->GetStringFromName("imageTitle", title);
    filterBundle->GetStringFromName("imageFilter", filter);
    AppendFilter(title, filter);
  }
  if (aFilterMask & filterAudio) {
    titleBundle->GetStringFromName("audioTitle", title);
    filterBundle->GetStringFromName("audioFilter", filter);
    AppendFilter(title, filter);
  }
  if (aFilterMask & filterVideo) {
    titleBundle->GetStringFromName("videoTitle", title);
    filterBundle->GetStringFromName("videoFilter", filter);
    AppendFilter(title, filter);
  }
  if (aFilterMask & filterXML) {
    titleBundle->GetStringFromName("xmlTitle", title);
    filterBundle->GetStringFromName("xmlFilter", filter);
    AppendFilter(title, filter);
  }
  if (aFilterMask & filterXUL) {
    titleBundle->GetStringFromName("xulTitle", title);
    filterBundle->GetStringFromName("xulFilter", filter);
    AppendFilter(title, filter);
  }
  if (aFilterMask & filterApps) {
    titleBundle->GetStringFromName("appsTitle", title);
    // Pass the magic string "..apps" to the platform filepicker, which it
    // should recognize and do the correct platform behavior for.
    AppendFilter(title, u"..apps"_ns);
  }
  if (aFilterMask & filterPDF) {
    titleBundle->GetStringFromName("pdfTitle", title);
    filterBundle->GetStringFromName("pdfFilter", filter);
    AppendFilter(title, filter);
  }
  return NS_OK;
}

NS_IMETHODIMP nsBaseFilePicker::AppendRawFilter(const nsAString& aFilter) {
  mRawFilters.AppendElement(aFilter);
  return NS_OK;
}

NS_IMETHODIMP nsBaseFilePicker::GetCapture(
    nsIFilePicker::CaptureTarget* aCapture) {
  *aCapture = nsIFilePicker::CaptureTarget::captureNone;
  return NS_OK;
}

NS_IMETHODIMP nsBaseFilePicker::SetCapture(
    nsIFilePicker::CaptureTarget aCapture) {
  return NS_OK;
}

// Set the filter index
NS_IMETHODIMP nsBaseFilePicker::GetFilterIndex(int32_t* aFilterIndex) {
  *aFilterIndex = 0;
  return NS_OK;
}

NS_IMETHODIMP nsBaseFilePicker::SetFilterIndex(int32_t aFilterIndex) {
  return NS_OK;
}

NS_IMETHODIMP nsBaseFilePicker::GetFiles(nsISimpleEnumerator** aFiles) {
  NS_ENSURE_ARG_POINTER(aFiles);
  nsCOMArray<nsIFile> files;
  nsresult rv;

  // if we get into the base class, the platform
  // doesn't implement GetFiles() yet.
  // so we fake it.
  nsCOMPtr<nsIFile> file;
  rv = GetFile(getter_AddRefs(file));
  NS_ENSURE_SUCCESS(rv, rv);

  files.AppendObject(file);

  return NS_NewArrayEnumerator(aFiles, files, NS_GET_IID(nsIFile));
}

// Set the display directory
NS_IMETHODIMP nsBaseFilePicker::SetDisplayDirectory(nsIFile* aDirectory) {
  // if displaySpecialDirectory has been previously called, let's abort this
  // operation.
  if (!mDisplaySpecialDirectory.IsEmpty()) {
    return NS_OK;
  }

  if (!aDirectory) {
    mDisplayDirectory = nullptr;
    return NS_OK;
  }

  if (NS_WARN_IF(!IsPotentiallyReadableDirectory(*aDirectory))) {
    return NS_OK;
  }

  nsCOMPtr<nsIFile> directory;
  nsresult rv = aDirectory->Clone(getter_AddRefs(directory));
  if (NS_FAILED(rv)) {
    return rv;
  }

  mDisplayDirectory = std::move(directory);
  return NS_OK;
}

// Get the display directory
NS_IMETHODIMP nsBaseFilePicker::GetDisplayDirectory(nsIFile** aDirectory) {
  *aDirectory = nullptr;

  // if displaySpecialDirectory has been previously called, let's abort this
  // operation.
  if (!mDisplaySpecialDirectory.IsEmpty()) {
    return NS_OK;
  }

  if (!mDisplayDirectory) {
    return NS_OK;
  }

  nsCOMPtr<nsIFile> directory;
  nsresult rv = mDisplayDirectory->Clone(getter_AddRefs(directory));
  if (NS_FAILED(rv)) {
    return rv;
  }
  directory.forget(aDirectory);
  return NS_OK;
}

// Set the display special directory
NS_IMETHODIMP nsBaseFilePicker::SetDisplaySpecialDirectory(
    const nsAString& aDirectory) {
  // if displayDirectory has been previously called, let's abort this operation.
  if (mDisplayDirectory && mDisplaySpecialDirectory.IsEmpty()) {
    return NS_OK;
  }

  mDisplaySpecialDirectory = aDirectory;
  if (mDisplaySpecialDirectory.IsEmpty()) {
    mDisplayDirectory = nullptr;
    return NS_OK;
  }

  return ResolveSpecialDirectory(aDirectory);
}

bool nsBaseFilePicker::MaybeBlockFilePicker(
    nsIFilePickerShownCallback* aCallback) {
  MOZ_ASSERT(mBrowsingContext);
  MOZ_ASSERT(XRE_IsParentProcess());

  if (mozilla::StaticPrefs::widget_disable_file_pickers()) {
    if (aCallback) {
      // File pickers are disabled, so we answer the callback with returnCancel.
      NS_DispatchToCurrentThread(
          mozilla::NewRunnableMethod<nsIFilePicker::ResultCode>(
              "nsBaseFilePicker::CallbackWithCancelResult", aCallback,
              &nsIFilePickerShownCallback::Done, nsIFilePicker::returnCancel));
    }

    RefPtr<Element> topFrameElement = mBrowsingContext->GetTopFrameElement();
    if (topFrameElement) {
      // Dispatch an event that the frontend may use.
      nsContentUtils::DispatchEventOnlyToChrome(
          topFrameElement->OwnerDoc(), topFrameElement, u"FilePickerBlocked"_ns,
          mozilla::CanBubble::eYes, mozilla::Cancelable::eNo);
    }

    return true;
  }

  if (mBrowsingContext->Canonical()->CanOpenModalPicker()) {
    return false;
  }

  if (aCallback) {
    // File pickers are not allowed to open, so we respond to the callback with
    // returnCancel.
    NS_DispatchToCurrentThread(
        mozilla::NewRunnableMethod<nsIFilePicker::ResultCode>(
            "nsBaseFilePicker::CallbackWithCancelResult", aCallback,
            &nsIFilePickerShownCallback::Done, nsIFilePicker::returnCancel));
  }

  return true;
}

// static
bool nsBaseFilePicker::IsWithinInputProtectionTimeRange(
    mozilla::TimeStamp aShowTime, mozilla::TimeStamp aNow,
    uint32_t aProtectionMs) {
  if (!aProtectionMs || aShowTime.IsNull()) {
    return false;
  }
  // TimeStamp only moves forward, so the current time should never be earlier
  // than the shown time. We do not expect this to happen; if it ever does we
  // want to find out and decide how to handle it, so assert on debug and
  // Nightly builds. On other builds, fall back to "not protected" so we never
  // block the user from confirming.
  MOZ_DIAGNOSTIC_ASSERT(aNow >= aShowTime,
                        "file picker shown time is in the future");
  if (aNow < aShowTime) {
    return false;
  }
  return (aNow - aShowTime).ToMilliseconds() < double(aProtectionMs);
}

// static
bool nsBaseFilePicker::IsPotentiallyReadableDirectory(nsIFile& aDirectory) {
  if (XRE_IsContentProcess()) {
    // The content process sandbox may block access to directories that the
    // file picker, which runs in the parent process, can read.
    return true;
  }

#ifdef XP_MACOSX
  // On macOS, the file picker can read directories that our own process can't.
  return true;
#else
#  ifdef MOZ_WIDGET_GTK
  if (mozilla::widget::IsRunningUnderFlatpakOrSnap()) {
    // On Flatpak / snap, the file picker can read directories that our own
    // process can't.
    return true;
  }
#  endif
  bool isDirectory = false;
  bool isReadable = false;
  return NS_SUCCEEDED(aDirectory.IsDirectory(&isDirectory)) && isDirectory &&
         NS_SUCCEEDED(aDirectory.IsReadable(&isReadable)) && isReadable;
#endif
}

bool nsBaseFilePicker::IsContentInitiated() const {
  if (!mBrowsingContext || !mBrowsingContext->IsContent()) {
    return false;
  }
  // Only web content is subject to input protection. Exempt trusted documents
  // (system principal and about: pages), which run in a content browsing
  // context but are not untrusted web content.
  if (mozilla::dom::WindowGlobalParent* wgp =
          mBrowsingContext->Canonical()->GetCurrentWindowGlobal()) {
    if (nsIPrincipal* principal = wgp->DocumentPrincipal()) {
      return !principal->IsSystemPrincipal() && !principal->SchemeIs("about");
    }
  }
  return true;
}

bool nsBaseFilePicker::IsPickerInputProtected() const {
  return IsContentInitiated() &&
         IsWithinInputProtectionTimeRange(
             mShowTime, mozilla::TimeStamp::Now(),
             mozilla::StaticPrefs::security_notification_enable_delay());
}

nsresult nsBaseFilePicker::ResolveSpecialDirectory(
    const nsAString& aSpecialDirectory) {
  // Only perform special-directory name resolution in the parent process.
  // (Subclasses of `nsBaseFilePicker` used in other processes must override
  // this function.)
  MOZ_ASSERT(XRE_IsParentProcess());
  return NS_GetSpecialDirectory(NS_ConvertUTF16toUTF8(aSpecialDirectory).get(),
                                getter_AddRefs(mDisplayDirectory));
}

// Get the display special directory
NS_IMETHODIMP nsBaseFilePicker::GetDisplaySpecialDirectory(
    nsAString& aDirectory) {
  aDirectory = mDisplaySpecialDirectory;
  return NS_OK;
}

NS_IMETHODIMP
nsBaseFilePicker::GetAddToRecentDocs(bool* aFlag) {
  *aFlag = mAddToRecentDocs;
  return NS_OK;
}

NS_IMETHODIMP
nsBaseFilePicker::SetAddToRecentDocs(bool aFlag) {
  mAddToRecentDocs = aFlag;
  return NS_OK;
}

NS_IMETHODIMP
nsBaseFilePicker::GetMode(nsIFilePicker::Mode* aMode) {
  *aMode = mMode;
  return NS_OK;
}

NS_IMETHODIMP
nsBaseFilePicker::SetOkButtonLabel(const nsAString& aLabel) {
  mOkButtonLabel = aLabel;
  return NS_OK;
}

NS_IMETHODIMP
nsBaseFilePicker::GetOkButtonLabel(nsAString& aLabel) {
  aLabel = mOkButtonLabel;
  return NS_OK;
}

nsIGlobalObject* nsBaseFilePicker::GetRelevantGlobal() const {
  if (mGlobal) {
    return mGlobal;
  }
  if (mBrowsingContext) {
    if (auto* win = mBrowsingContext->GetDOMWindow()) {
      if (auto* inner = win->GetCurrentInnerWindow()) {
        return inner->AsGlobal();
      }
    }
  }
  return nullptr;
}

NS_IMETHODIMP
nsBaseFilePicker::GetDomFileOrDirectory(nsISupports** aValue) {
  MOZ_ASSERT(XRE_IsParentProcess());
  NS_ENSURE_ARG_POINTER(mBrowsingContext);
  nsCOMPtr<nsIFile> localFile;
  nsresult rv = GetFile(getter_AddRefs(localFile));
  NS_ENSURE_SUCCESS(rv, rv);

  if (!localFile) {
    *aValue = nullptr;
    return NS_OK;
  }

  auto* innerParent = GetRelevantGlobal();
  if (!innerParent) {
    return NS_ERROR_FAILURE;
  }

  return LocalFileToDirectoryOrBlob(
      innerParent, mMode == nsIFilePicker::modeGetFolder, localFile, aValue);
}

NS_IMETHODIMP
nsBaseFilePicker::GetDomFileOrDirectoryEnumerator(
    nsISimpleEnumerator** aValue) {
  nsCOMPtr<nsISimpleEnumerator> iter;
  MOZ_ASSERT(XRE_IsParentProcess());
  NS_ENSURE_ARG_POINTER(mBrowsingContext);
  nsresult rv = GetFiles(getter_AddRefs(iter));
  NS_ENSURE_SUCCESS(rv, rv);

  auto* global = GetRelevantGlobal();
  if (!global) {
    return NS_ERROR_FAILURE;
  }

  auto retIter =
      mozilla::MakeRefPtr<nsBaseFilePickerEnumerator>(global, iter, mMode);
  retIter.forget(aValue);
  return NS_OK;
}

NS_IMETHODIMP
nsBaseFilePicker::GetDomFilesInWebKitDirectory(nsISimpleEnumerator** aValue) {
  return NS_ERROR_NOT_IMPLEMENTED;
}
