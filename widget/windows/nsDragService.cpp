/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <ole2.h>
#include <oleidl.h>
#include <shlobj.h>
#include <shlwapi.h>

// shellapi.h is needed to build with WIN32_LEAN_AND_MEAN
#include <shellapi.h>

#include "KeyboardLayout.h"
#include "WinUtils.h"
#include "gfxContext.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/DataSurfaceHelpers.h"
#include "mozilla/gfx/Tools.h"
#include "mozilla/widget/WidgetLogging.h"
#include "nsArrayUtils.h"
#include "nsCRT.h"
#include "nsClipboard.h"
#include "nsDataObj.h"
#include "nsDataObjCollection.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDragService.h"
#include "nsEscape.h"
#include "nsIScreenManager.h"
#include "nsITransferable.h"
#include "nsMathUtils.h"
#include "nsNativeDragSource.h"
#include "nsNativeDragTarget.h"
#include "nsRect.h"
#include "nsString.h"
#include "nsToolkit.h"
#include "nsUnicharUtils.h"
#include "nsWidgetsCID.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::widget;

#define LOGD DRAGSERVICE_LOGD
#define LOGI DRAGSERVICE_LOGI
#define LOGE DRAGSERVICE_LOGE

nsDragSession::~nsDragSession() { NS_IF_RELEASE(mDataObject); }

already_AddRefed<nsIDragSession> nsDragService::CreateDragSession() {
  auto session = MakeRefPtr<nsDragSession>();
  return session.forget();
}

bool nsDragSession::CreateDragImage(nsINode* aDOMNode,
                                    const Maybe<CSSIntRegion>& aRegion,
                                    SHDRAGIMAGE* psdi) {
  auto logDidNotDraw = MakeScopeExit([&]() {
    LOGD("[%p] %s | aDOMNode: %p | psdi: %p | Did not draw image", this,
         __FUNCTION__, aDOMNode, psdi);
  });

  if (!psdi) return false;

  memset(psdi, 0, sizeof(SHDRAGIMAGE));
  if (!aDOMNode) return false;

  // Prepare the drag image
  LayoutDeviceIntRect dragRect;
  RefPtr<SourceSurface> surface;
  nsPresContext* pc;
  DrawDrag(aDOMNode, aRegion, mScreenPosition, &dragRect, &surface, &pc);
  if (!surface) return false;

  uint32_t bmWidth = dragRect.Width(), bmHeight = dragRect.Height();

  if (bmWidth == 0 || bmHeight == 0) return false;

  psdi->crColorKey = CLR_NONE;

  RefPtr<DataSourceSurface> dataSurface = Factory::CreateDataSourceSurface(
      IntSize(bmWidth, bmHeight), SurfaceFormat::B8G8R8A8);
  NS_ENSURE_TRUE(dataSurface, false);

  DataSourceSurface::MappedSurface map;
  if (!dataSurface->Map(DataSourceSurface::MapType::READ_WRITE, &map)) {
    return false;
  }

  RefPtr<DrawTarget> dt = Factory::CreateDrawTargetForData(
      BackendType::SKIA, map.mData, dataSurface->GetSize(), map.mStride,
      dataSurface->GetFormat());
  if (!dt) {
    dataSurface->Unmap();
    return false;
  }

  dt->DrawSurface(
      surface,
      Rect(0, 0, dataSurface->GetSize().width, dataSurface->GetSize().height),
      Rect(0, 0, surface->GetSize().width, surface->GetSize().height),
      DrawSurfaceOptions(), DrawOptions(1.0f, CompositionOp::OP_SOURCE));
  dt->Flush();

  BITMAPV5HEADER bmih;
  memset((void*)&bmih, 0, sizeof(BITMAPV5HEADER));
  bmih.bV5Size = sizeof(BITMAPV5HEADER);
  bmih.bV5Width = bmWidth;
  bmih.bV5Height = -(int32_t)bmHeight;  // flip vertical
  bmih.bV5Planes = 1;
  bmih.bV5BitCount = 32;
  bmih.bV5Compression = BI_BITFIELDS;
  bmih.bV5RedMask = 0x00FF0000;
  bmih.bV5GreenMask = 0x0000FF00;
  bmih.bV5BlueMask = 0x000000FF;
  bmih.bV5AlphaMask = 0xFF000000;

  HDC hdcSrc = CreateCompatibleDC(nullptr);
  void* lpBits = nullptr;
  if (hdcSrc) {
    psdi->hbmpDragImage =
        ::CreateDIBSection(hdcSrc, (BITMAPINFO*)&bmih, DIB_RGB_COLORS,
                           (void**)&lpBits, nullptr, 0);
    if (psdi->hbmpDragImage && lpBits) {
      CopySurfaceDataToPackedArray(map.mData, static_cast<uint8_t*>(lpBits),
                                   dataSurface->GetSize(), map.mStride,
                                   BytesPerPixel(dataSurface->GetFormat()));
    }

    psdi->sizeDragImage.cx = bmWidth;
    psdi->sizeDragImage.cy = bmHeight;

    const auto screenPoint =
        LayoutDeviceIntPoint::Round(mScreenPosition * pc->CSSToDevPixelScale());
    psdi->ptOffset.x = screenPoint.x - dragRect.X();
    psdi->ptOffset.y = screenPoint.y - dragRect.Y();

    DeleteDC(hdcSrc);
    LOGD(
        "[%p] %s | aDOMNode: %p | psdi: %p | bm dims: (%u, %u) | "
        "screenPoint: (%d, %d) | Drew drag image",
        this, __FUNCTION__, aDOMNode, psdi, bmWidth, bmHeight,
        static_cast<int>(screenPoint.x), static_cast<int>(screenPoint.y));
    logDidNotDraw.release();
  }

  dataSurface->Unmap();

  return psdi->hbmpDragImage != nullptr;
}

//-------------------------------------------------------------------------
nsresult nsDragSession::InvokeDragSessionImpl(
    nsIWidget* aWidget, nsIArray* anArrayTransferables,
    const Maybe<CSSIntRegion>& aRegion, uint32_t aActionType) {
  // Try and get source URI of the items that are being dragged
  nsIURI* uri = nullptr;

  RefPtr<dom::Document> doc(mSourceDocument);
  if (doc) {
    uri = doc->GetDocumentURI();
  }

  uint32_t numItemsToDrag = 0;
  nsresult rv = anArrayTransferables->GetLength(&numItemsToDrag);
  if (!numItemsToDrag) {
    LOGD("[%p] %s | No items in drag", this, __FUNCTION__);
    return NS_ERROR_FAILURE;
  }

  // The clipboard class contains some static utility methods that we
  // can use to create an IDataObject from the transferable

  // if we're dragging more than one item, we need to create a
  // "collection" object to fake out the OS. This collection contains
  // one |IDataObject| for each transferable. If there is just the one
  // (most cases), only pass around the native |IDataObject|.
  RefPtr<IDataObject> itemToDrag;
  if (numItemsToDrag > 1) {
    nsDataObjCollection* dataObjCollection = new nsDataObjCollection();
    if (!dataObjCollection) return NS_ERROR_OUT_OF_MEMORY;
    itemToDrag = dataObjCollection;
    for (uint32_t i = 0; i < numItemsToDrag; ++i) {
      nsCOMPtr<nsITransferable> trans =
          do_QueryElementAt(anArrayTransferables, i);
      if (trans) {
        RefPtr<IDataObject> dataObj;
        rv = nsClipboard::CreateNativeDataObject(trans, getter_AddRefs(dataObj),
                                                 uri);
        NS_ENSURE_SUCCESS(rv, rv);
        // Add the flavors to the collection object too
        rv = nsClipboard::SetupNativeDataObject(trans, dataObjCollection);
        NS_ENSURE_SUCCESS(rv, rv);

        dataObjCollection->AddDataObject(dataObj);
      }
    }
    LOGD("[%p] %s | Created nsDataObjCollection with %d items", this,
         __FUNCTION__, dataObjCollection->GetNumDataObjects());
  }  // if dragging multiple items
  else {
    nsCOMPtr<nsITransferable> trans =
        do_QueryElementAt(anArrayTransferables, 0);
    if (trans) {
      rv = nsClipboard::CreateNativeDataObject(trans,
                                               getter_AddRefs(itemToDrag), uri);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }  // else dragging a single object

  LOGD("[%p] %s | numItemsToDrag: %u | itemToDrag: %p", this, __FUNCTION__,
       numItemsToDrag, itemToDrag.get());

  // Create a drag image if support is available
  IDragSourceHelper* pdsh;
  if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr,
                                 CLSCTX_INPROC_SERVER, IID_IDragSourceHelper,
                                 (void**)&pdsh))) {
    SHDRAGIMAGE sdi;
    if (CreateDragImage(mSourceNode, aRegion, &sdi)) {
      if (FAILED(pdsh->InitializeFromBitmap(&sdi, itemToDrag))) {
        DeleteObject(sdi.hbmpDragImage);
      }
    }
    pdsh->Release();
  }

  // Kick off the native drag session
  return StartInvokingDragSession(aWidget, itemToDrag, aActionType);
}

static HWND GetSourceWindow(dom::Document* aSourceDocument) {
  if (!aSourceDocument) {
    return nullptr;
  }

  auto* pc = aSourceDocument->GetPresContext();
  if (!pc) {
    return nullptr;
  }

  nsCOMPtr<nsIWidget> widget = pc->GetRootWidget();
  if (!widget) {
    return nullptr;
  }

  return (HWND)widget->GetNativeData(NS_NATIVE_WINDOW);
}

//-------------------------------------------------------------------------
nsresult nsDragSession::StartInvokingDragSession(nsIWidget* aWidget,
                                                 IDataObject* aDataObj,
                                                 uint32_t aActionType) {
  LOGD("[%p] %s | aDataObj: %p | aActionType: %u", this, __FUNCTION__, aDataObj,
       aActionType);

  // To do the drag we need to create an object that
  // implements the IDataObject interface (for OLE)
  auto nativeDragSrc = MakeRefPtr<nsNativeDragSource>(mDataTransfer);

  // Now figure out what the native drag effect should be
  DWORD winDropRes;
  DWORD effects = DROPEFFECT_SCROLL;
  if (aActionType & nsIDragService::DRAGDROP_ACTION_COPY) {
    effects |= DROPEFFECT_COPY;
  }
  if (aActionType & nsIDragService::DRAGDROP_ACTION_MOVE) {
    effects |= DROPEFFECT_MOVE;
  }
  if (aActionType & nsIDragService::DRAGDROP_ACTION_LINK) {
    effects |= DROPEFFECT_LINK;
  }

  // XXX not sure why we bother to cache this, it can change during
  // the drag
  mDragAction = aActionType;
  mSentLocalDropEvent = false;

  // Start dragging
  OpenDragPopup();

  RefPtr<IDataObjectAsyncCapability> pAsyncOp;
  // Offer to do an async drag
  if (SUCCEEDED(aDataObj->QueryInterface(IID_IDataObjectAsyncCapability,
                                         getter_AddRefs(pAsyncOp)))) {
    pAsyncOp->SetAsyncMode(VARIANT_TRUE);
  } else {
    MOZ_ASSERT_UNREACHABLE("When did our data object stop being async");
  }

  // Call the native D&D method
  LOGI(
      "[%p] %s | Starting nested event loop that will run for the duration of "
      "the drag",
      this, __FUNCTION__);
  HRESULT res = ::DoDragDrop(aDataObj, nativeDragSrc, effects, &winDropRes);

  // In cases where the drop operation completed outside the application,
  // update the source node's DataTransfer dropEffect value so it is up to date.
  if (!mSentLocalDropEvent) {
    uint32_t dropResult;
    // Order is important, since multiple flags can be returned.
    if (winDropRes & DROPEFFECT_COPY)
      dropResult = nsIDragService::DRAGDROP_ACTION_COPY;
    else if (winDropRes & DROPEFFECT_LINK)
      dropResult = nsIDragService::DRAGDROP_ACTION_LINK;
    else if (winDropRes & DROPEFFECT_MOVE)
      dropResult = nsIDragService::DRAGDROP_ACTION_MOVE;
    else
      dropResult = nsIDragService::DRAGDROP_ACTION_NONE;

    if (mDataTransfer) {
      if (!mozilla::StaticPrefs::widget_windows_allow_external_tab_drag()) {
        // Special case: if we're dropping a browser tab onto another
        // application, assume that application is lying if they say they're
        // prepared to handle it. (This is sadly common. See bug 1598915.)
        if (mDataTransfer->HasType(u"application/x-moz-tabbrowser-tab"_ns)) {
          dropResult = nsIDragService::DRAGDROP_ACTION_NONE;
        }
      }

      if (res == DRAGDROP_S_DROP)  // Success
        mDataTransfer->SetDropEffectInt(dropResult);
      else
        mDataTransfer->SetDropEffectInt(nsIDragService::DRAGDROP_ACTION_NONE);
    }
  }

  mUserCancelled = nativeDragSrc->UserCancelled();

  // We're done dragging, get the cursor position and end the drag
  // Use GetMessagePos to get the position of the mouse at the last message
  // seen by the event loop. (Bug 489729)
  // Note that we must convert this from device pixels back to Windows logical
  // pixels (bug 818927).
  DWORD pos = ::GetMessagePos();
  POINT cpos;
  cpos.x = GET_X_LPARAM(pos);
  cpos.y = GET_Y_LPARAM(pos);
  if (auto wnd = GetSourceWindow(mSourceDocument)) {
    // Convert from screen to client coordinates like nsWindow::InitEvent does.
    ::ScreenToClient(wnd, &cpos);
  }
  SetDragEndPoint(LayoutDeviceIntPoint(cpos.x, cpos.y));

  ModifierKeyState modifierKeyState;
  LOGI(
      "[%p] %s | mSentLocalDropEvent: %s | mDataTransfer->DropEffectInt: %d | "
      "mUserCancelled: %s | dragEndPoint: (%ld,%ld) | modifier-keys: %u | "
      "Exited nested drag event loop.  Ending Gecko-initiated drag session.",
      this, __FUNCTION__, TrueOrFalse(mSentLocalDropEvent),
      mDataTransfer ? mDataTransfer->DropEffectInt() : INT_MAX,
      TrueOrFalse(mUserCancelled), cpos.x, cpos.y,
      modifierKeyState.GetModifiers());
  EndDragSession(true, modifierKeyState.GetModifiers());

  mDoingDrag = false;

  return DRAGDROP_S_DROP == res ? NS_OK : NS_ERROR_FAILURE;
}

//-------------------------------------------------------------------------
// Make Sure we have the right kind of object
/* static */
nsDataObjCollection* nsDragSession::GetDataObjCollection(
    IDataObject* aDataObj) {
  nsDataObjCollection* dataObjCol = nullptr;
  if (aDataObj) {
    nsIDataObjCollection* dataObj;
    if (aDataObj->QueryInterface(IID_IDataObjCollection, (void**)&dataObj) ==
        S_OK) {
      dataObjCol = static_cast<nsDataObjCollection*>(aDataObj);
      dataObj->Release();
    }
  }

  return dataObjCol;
}

//-------------------------------------------------------------------------
NS_IMETHODIMP
nsDragSession::GetNumDropItems(uint32_t* aNumItems) {
  if (!mDataObject) {
    *aNumItems = 0;
    return NS_OK;
  }

  if (IsCollectionObject(mDataObject)) {
    nsDataObjCollection* dataObjCol = GetDataObjCollection(mDataObject);
    // If the count cannot be determined just return 0.
    // This can happen if we have collection data of type
    // MULTI_MIME ("Mozilla/IDataObjectCollectionFormat") on the clipboard
    // from another process but we can't obtain an IID_IDataObjCollection
    // from this process.
    *aNumItems = dataObjCol ? dataObjCol->GetNumDataObjects() : 0;
    return NS_OK;
  }
  // Next check if we have a file drop. Return the number of files in
  // the file drop as the number of items we have, pretending like we
  // actually have > 1 drag item.
  FORMATETC fe2;
  SET_FORMATETC(fe2, CF_HDROP, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL);
  if (SUCCEEDED(mDataObject->QueryGetData(&fe2))) {
    STGMEDIUM stm{};
    if (FAILED(mDataObject->GetData(&fe2, &stm))) {
      *aNumItems = 1;
      return NS_OK;
    }
    HDROP hdrop = static_cast<HDROP>(GlobalLock(stm.hGlobal));
    MOZ_ASSERT(hdrop != NULL);
    // Let the per-item read fail in nsClipboard::GetNativeDataOffClipboard
    // instead of returning an error and failing the drag session.
    *aNumItems = nsClipboard::IsValidDropFilesData(stm.hGlobal)
                     ? ::DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0)
                     : 0;
    ::GlobalUnlock(stm.hGlobal);
    ::ReleaseStgMedium(&stm);
    // Data may be provided later, so assume we have 1 item
    if (*aNumItems == 0) {
      *aNumItems = 1;
    }
    return NS_OK;
  }
  // Next check if we have a virtual file drop.
  SET_FORMATETC(fe2, nsClipboard::GetClipboardFileDescriptorFormatW(), 0,
                DVASPECT_CONTENT, -1, TYMED_HGLOBAL);
  STGMEDIUM stm{};

  if (SUCCEEDED(mDataObject->GetData(&fe2, &stm))) {
    *aNumItems = 0;
    LPFILEGROUPDESCRIPTOR pDesc =
        static_cast<LPFILEGROUPDESCRIPTOR>(GlobalLock(stm.hGlobal));
    // Validate that pDesc actualy has the contents it claims.
    if (pDesc && nsClipboard::FileGroupDescriptorHasItems<FILEGROUPDESCRIPTORW>(
                     stm.hGlobal, pDesc->cItems)) {
      *aNumItems = pDesc->cItems;
    }
    GlobalUnlock(stm.hGlobal);
    ReleaseStgMedium(&stm);
    return NS_OK;
  }
  *aNumItems = 1;
  return NS_OK;
}

//-------------------------------------------------------------------------
NS_IMETHODIMP
nsDragSession::GetData(nsITransferable* aTransferable, uint32_t anItem) {
  // This typcially happens on a drop, the target would be asking
  // for it's transferable to be filled in
  // Use a static clipboard utility method for this
  if (!mDataObject) return NS_ERROR_FAILURE;

  nsresult dataFound = NS_ERROR_FAILURE;

  if (IsCollectionObject(mDataObject)) {
    // multiple items, use |anItem| as an index into our collection
    nsDataObjCollection* dataObjCol = GetDataObjCollection(mDataObject);
    NS_ENSURE_TRUE(dataObjCol, NS_ERROR_FAILURE);
    uint32_t cnt = dataObjCol->GetNumDataObjects();
    if (anItem < cnt) {
      IDataObject* dataObj = dataObjCol->GetDataObjectAt(anItem);
      dataFound = nsClipboard::GetDataFromDataObject(dataObj, 0, nullptr,
                                                     aTransferable);
    } else
      NS_WARNING("Index out of range!");
  } else {
    // If they are asking for item "0", we can just get it...
    if (anItem == 0) {
      dataFound = nsClipboard::GetDataFromDataObject(mDataObject, anItem,
                                                     nullptr, aTransferable);
    } else {
      // It better be a file drop, or else non-zero indexes are invalid!
      FORMATETC fe2;
      FORMATETC fe3;
      SET_FORMATETC(fe2, CF_HDROP, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL);
      SET_FORMATETC(fe3, nsClipboard::GetClipboardFileDescriptorFormatW(), 0,
                    DVASPECT_CONTENT, -1, TYMED_HGLOBAL);
      if (SUCCEEDED(mDataObject->QueryGetData(&fe2)) ||
          SUCCEEDED(mDataObject->QueryGetData(&fe3)))
        dataFound = nsClipboard::GetDataFromDataObject(mDataObject, anItem,
                                                       nullptr, aTransferable);
      else
        NS_WARNING(
            "Reqesting non-zero index, but clipboard data is not a "
            "collection!");
    }
  }
  return dataFound;
}

//---------------------------------------------------------
void nsDragSession::SetIDataObject(IDataObject* aDataObj) {
  // When the native drag starts the DragService gets
  // the IDataObject that is being dragged
  NS_IF_RELEASE(mDataObject);
  mDataObject = aDataObj;
  NS_IF_ADDREF(mDataObject);

  if (MOZ_DRAGSERVICE_LOG_ENABLED()) {
    MOZ_DRAGSERVICE_LOG("nsDragSession::SetIDataObject (%p)", mDataObject);
    IEnumFORMATETC* pEnum = nullptr;
    if (mDataObject &&
        S_OK == mDataObject->EnumFormatEtc(DATADIR_GET, &pEnum)) {
      MOZ_DRAGSERVICE_LOG("    formats in DataObject:");

      FORMATETC fEtc;
      while (S_OK == pEnum->Next(1, &fEtc, nullptr)) {
        nsAutoString format;
        WinUtils::GetClipboardFormatAsString(fEtc.cfFormat, format);
        MOZ_DRAGSERVICE_LOG("        FORMAT %s",
                            NS_ConvertUTF16toUTF8(format).get());
      }
      pEnum->Release();
    }
  }
}

//---------------------------------------------------------
void nsDragSession::SetDroppedLocal() {
  // Sent from the native drag handler, letting us know
  // a drop occurred within the application vs. outside of it.
  mSentLocalDropEvent = true;
  return;
}

//-------------------------------------------------------------------------
NS_IMETHODIMP
nsDragSession::IsDataFlavorSupported(const char* aDataFlavor, bool* _retval) {
  if (!aDataFlavor || !mDataObject || !_retval) {
    LOGE("[%p] %s | aDataFlavor: %s | mDataObject: %p | invalid args", this,
         __FUNCTION__, aDataFlavor ? aDataFlavor : "<null>", mDataObject);
    return NS_ERROR_FAILURE;
  }

  *_retval = false;
  auto logging = MakeScopeExit([&] {
    LOGD("[%p] %s | %s was%s found", this, __FUNCTION__, aDataFlavor,
         *_retval ? "" : " not");
  });

  FORMATETC fe;
  UINT format = 0;

  if (IsCollectionObject(mDataObject)) {
    // We know we have one of our special collection objects.
    format = nsClipboard::GetFormat(aDataFlavor);
    SET_FORMATETC(fe, format, 0, DVASPECT_CONTENT, -1,
                  TYMED_HGLOBAL | TYMED_FILE | TYMED_GDI);

    // See if any one of the IDataObjects in the collection supports
    // this data type
    nsDataObjCollection* dataObjCol = GetDataObjCollection(mDataObject);
    if (dataObjCol) {
      uint32_t cnt = dataObjCol->GetNumDataObjects();
      for (uint32_t i = 0; i < cnt; ++i) {
        IDataObject* dataObj = dataObjCol->GetDataObjectAt(i);
        if (S_OK == dataObj->QueryGetData(&fe)) {
          *_retval = true;  // found it!
        }
      }
    }
    return NS_OK;
  }

  // Ok, so we have a single object. Check to see if has the correct
  // data type. Since this can come from an outside app, we also
  // need to see if we need to perform text->unicode conversion if
  // the client asked for unicode and it wasn't available.
  format = nsClipboard::GetFormat(aDataFlavor);
  SET_FORMATETC(fe, format, 0, DVASPECT_CONTENT, -1,
                TYMED_HGLOBAL | TYMED_FILE | TYMED_GDI);
  if (mDataObject->QueryGetData(&fe) == S_OK) {
    *_retval = true;  // found it!
    return NS_OK;
  }

  // We haven't found the exact flavor the client asked for, but
  // maybe we can still find it from something else that's in the
  // data object.
  if (strcmp(aDataFlavor, kTextMime) == 0) {
    // If unicode wasn't there, it might exist as CF_TEXT, client asked
    // for unicode and it wasn't present, check if we
    // have CF_TEXT.  We'll handle the actual data substitution in
    // the data object.
    SET_FORMATETC(fe, CF_TEXT, 0, DVASPECT_CONTENT, -1,
                  TYMED_HGLOBAL | TYMED_FILE | TYMED_GDI);
    if (mDataObject->QueryGetData(&fe) == S_OK) {
      *_retval = true;  // found it!
    }
    return NS_OK;
  }

  if (strcmp(aDataFlavor, kURLMime) == 0) {
    // client asked for a url and it wasn't present, but if we
    // have a file, then we have a URL to give them (the path, or
    // the internal URL if an InternetShortcut).
    format = nsClipboard::GetFormat(kFileMime);
    SET_FORMATETC(fe, format, 0, DVASPECT_CONTENT, -1,
                  TYMED_HGLOBAL | TYMED_FILE | TYMED_GDI);
    if (mDataObject->QueryGetData(&fe) == S_OK) {
      *_retval = true;  // found it!
    }
    return NS_OK;
  }

  if (format == CF_HDROP) {
    // Dragging a link from browsers creates both a URL and a FILE which is a
    // *.url shortcut in the data object. The file is useful when dropping in
    // Windows Explorer to create a internet shortcut. But when dropping in the
    // browser, users do not expect to have this file. So do not try to look up
    // virtal file if there is a URL in the data object.
    format = nsClipboard::GetFormat(kURLMime);
    SET_FORMATETC(fe, format, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL);
    if (mDataObject->QueryGetData(&fe) == S_OK) {
      return NS_OK;
    }

    // If the client wants a file, maybe we find a virtual file.
    format = nsClipboard::GetClipboardFileDescriptorFormatW();
    SET_FORMATETC(fe, format, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL);
    if (mDataObject->QueryGetData(&fe) == S_OK) {
      *_retval = true;  // found it!
    }

    // XXX should we fall back to CFSTR_FILEDESCRIPTORA?
    return NS_OK;
  }

  return NS_OK;
}

//
// IsCollectionObject
//
// Determine if this is a single |IDataObject| or one of our private
// collection objects. We know the difference because our collection
// object will respond to supporting the private |MULTI_MIME| format.
//
/* static */
bool nsDragSession::IsCollectionObject(IDataObject* inDataObj) {
  bool isCollection = false;

  // setup the format object to ask for the MULTI_MIME format. We only
  // need to do this once
  static UINT sFormat = 0;
  static FORMATETC sFE;
  if (!sFormat) {
    sFormat = nsClipboard::GetFormat(MULTI_MIME);
    SET_FORMATETC(sFE, sFormat, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL);
  }

  // ask the object if it supports it. If yes, we have a collection
  // object
  if (inDataObj->QueryGetData(&sFE) == S_OK) isCollection = true;

  return isCollection;

}  // IsCollectionObject

//
// EndDragSession
//
// Override the default to make sure that we release the data object
// when the drag ends. It seems that OLE doesn't like to let apps quit
// w/out crashing when we're still holding onto their data
//
nsresult nsDragSession::EndDragSessionImpl(bool aDoneDrag,
                                           uint32_t aKeyModifiers) {
  // Bug 100180: If we've got mouse events captured, make sure we release it -
  // that way, if we happen to call EndDragSession before diving into a nested
  // event loop, we can still respond to mouse events.
  if (::GetCapture()) {
    ::ReleaseCapture();
  }

  nsBaseDragSession::EndDragSessionImpl(aDoneDrag, aKeyModifiers);
  NS_IF_RELEASE(mDataObject);

  return NS_OK;
}

NS_IMETHODIMP
nsDragSession::UpdateDragImage(nsINode* aImage, int32_t aImageX,
                               int32_t aImageY) {
  if (!mDataObject) {
    return NS_OK;
  }

  nsBaseDragSession::UpdateDragImage(aImage, aImageX, aImageY);

  IDragSourceHelper* pdsh;
  if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr,
                                 CLSCTX_INPROC_SERVER, IID_IDragSourceHelper,
                                 (void**)&pdsh))) {
    SHDRAGIMAGE sdi;
    if (CreateDragImage(mSourceNode, Nothing(), &sdi)) {
      nsNativeDragTarget::DragImageChanged();
      if (FAILED(pdsh->InitializeFromBitmap(&sdi, mDataObject))) {
        DeleteObject(sdi.hbmpDragImage);
      } else {
        LOGD("[%p] %s | created new drag image and set DragImageChanged", this,
             __FUNCTION__);
      }
    }
    pdsh->Release();
  }

  return NS_OK;
}

#undef LOGD
#undef LOGI
#undef LOGE
