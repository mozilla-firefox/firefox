/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsNativeDragTarget.h"

#include "KeyboardLayout.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/widget/WidgetLogging.h"
#include "nsCOMPtr.h"
#include "nsClipboard.h"
#include "nsDragService.h"
#include "nsIDragService.h"
#include "nsINode.h"
#include "nsIWidget.h"
#include "nsWidgetsCID.h"
#include "nsWindow.h"

using namespace mozilla;
using namespace mozilla::widget;

#define LOGD DRAGSERVICE_LOGD
#define LOGI DRAGSERVICE_LOGI
#define LOGE DRAGSERVICE_LOGE

// This is cached for Leave notification
static POINTL gDragLastPoint;

bool nsNativeDragTarget::gDragImageChanged = false;

/*
 * class nsNativeDragTarget
 */
nsNativeDragTarget::nsNativeDragTarget(nsIWidget* aWidget)
    : m_cRef(0),
      mEffectsAllowed(DROPEFFECT_MOVE | DROPEFFECT_COPY | DROPEFFECT_LINK),
      mEffectsPreferred(DROPEFFECT_NONE),
      mTookOwnRef(false),
      mWidget(aWidget),
      mDropTargetHelper(nullptr) {
  LOGD("[%p] %s", this, __FUNCTION__);
  mHWnd = (HWND)mWidget->GetNativeData(NS_NATIVE_WINDOW);

  mDragService = do_GetService("@mozilla.org/widget/dragservice;1");
}

nsNativeDragTarget::~nsNativeDragTarget() {
  LOGD("[%p] %s", this, __FUNCTION__);
  if (mDropTargetHelper) {
    mDropTargetHelper->Release();
    mDropTargetHelper = nullptr;
  }
}

// IUnknown methods - see iunknown.h for documentation
STDMETHODIMP
nsNativeDragTarget::QueryInterface(REFIID riid, void** ppv) {
  *ppv = nullptr;

  if (IID_IUnknown == riid || IID_IDropTarget == riid) *ppv = this;

  if (nullptr != *ppv) {
    ((LPUNKNOWN)*ppv)->AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
nsNativeDragTarget::AddRef(void) {
  ++m_cRef;
  NS_LOG_ADDREF(this, m_cRef, "nsNativeDragTarget", sizeof(*this));
  return m_cRef;
}

STDMETHODIMP_(ULONG) nsNativeDragTarget::Release(void) {
  --m_cRef;
  NS_LOG_RELEASE(this, m_cRef, "nsNativeDragTarget");
  if (0 != m_cRef) return m_cRef;

  delete this;
  return 0;
}

void nsNativeDragTarget::GetGeckoDragAction(DWORD grfKeyState,
                                            LPDWORD pdwEffect,
                                            uint32_t* aGeckoAction) {
  // If a window is disabled or a modal window is on top of it
  // (which implies it is disabled), then we should not allow dropping.
  if (!mWidget->IsEnabled()) {
    LOGD("[%p] %s | window was not enabled", this, __FUNCTION__);
    *pdwEffect = DROPEFFECT_NONE;
    *aGeckoAction = nsIDragService::DRAGDROP_ACTION_NONE;
    return;
  }

  // If the user explicitly uses a modifier key, they want the associated action
  // Shift + Control -> LINK, Shift -> MOVE, Ctrl -> COPY
  DWORD desiredEffect = DROPEFFECT_NONE;
  if ((grfKeyState & MK_CONTROL) && (grfKeyState & MK_SHIFT)) {
    desiredEffect = DROPEFFECT_LINK;
  } else if (grfKeyState & MK_SHIFT) {
    desiredEffect = DROPEFFECT_MOVE;
  } else if (grfKeyState & MK_CONTROL) {
    desiredEffect = DROPEFFECT_COPY;
  }

  // Determine the desired effect from what is allowed and preferred.
  if (!(desiredEffect &= mEffectsAllowed)) {
    // No modifier key effect is set which is also allowed, check
    // the preference of the data.
    desiredEffect = mEffectsPreferred & mEffectsAllowed;
    if (!desiredEffect) {
      // No preference is set, so just fall back to the allowed effect itself
      desiredEffect = mEffectsAllowed;
    }
  }

  // Otherwise we should specify the first available effect
  // from MOVE, COPY, or LINK.
  if (desiredEffect & DROPEFFECT_MOVE) {
    *pdwEffect = DROPEFFECT_MOVE;
    *aGeckoAction = nsIDragService::DRAGDROP_ACTION_MOVE;
  } else if (desiredEffect & DROPEFFECT_COPY) {
    *pdwEffect = DROPEFFECT_COPY;
    *aGeckoAction = nsIDragService::DRAGDROP_ACTION_COPY;
  } else if (desiredEffect & DROPEFFECT_LINK) {
    *pdwEffect = DROPEFFECT_LINK;
    *aGeckoAction = nsIDragService::DRAGDROP_ACTION_LINK;
  } else {
    *pdwEffect = DROPEFFECT_NONE;
    *aGeckoAction = nsIDragService::DRAGDROP_ACTION_NONE;
  }

  LOGD(
      "[%p] %s | grfKeyState: %lu | desiredEffect: %lu | mEffectsAllowed: %lu "
      "| mEffectsPreferred: %lu | *pdwEffect: %lu | *aGeckoAction: %u",
      this, __FUNCTION__, grfKeyState, desiredEffect, mEffectsAllowed,
      mEffectsPreferred, *pdwEffect, *aGeckoAction);
}

inline bool IsKeyDown(char key) { return GetKeyState(key) < 0; }

void nsNativeDragTarget::DispatchDragDropEvent(EventMessage aEventMessage,
                                               const POINTL& aPT) {
  WidgetDragEvent event(true, aEventMessage, mWidget);

  nsWindow* win = static_cast<nsWindow*>(mWidget);
  win->InitEvent(event);
  POINT cpos;

  cpos.x = aPT.x;
  cpos.y = aPT.y;

  if (mHWnd != nullptr) {
    ::ScreenToClient(mHWnd, &cpos);
  } else {
    cpos = {0, 0};
  }
  event.mRefPoint = LayoutDeviceIntPoint(cpos.x, cpos.y);

  ModifierKeyState modifierKeyState;
  modifierKeyState.InitInputEvent(event);

  nsDragSession* currSession =
      static_cast<nsDragSession*>(mDragService->GetCurrentSession(mWidget));
  if (currSession) {
    event.mInputSource = currSession->GetInputSource();
  } else {
    event.mInputSource = dom::MouseEvent_Binding::MOZ_SOURCE_MOUSE;
  }

  LOGI("[%p] %s | Dispatching %s event at client pos (%ld, %ld)", this,
       __FUNCTION__,
       NS_ConvertUTF16toUTF8(dom::Event::GetEventName(aEventMessage)).get(),
       cpos.x, cpos.y);
  mWidget->DispatchInputEvent(&event);
}

void nsNativeDragTarget::ProcessDrag(EventMessage aEventMessage,
                                     DWORD grfKeyState, POINTL ptl,
                                     DWORD* pdwEffect) {
  // Before dispatching the event make sure we have the correct drop action set
  uint32_t geckoAction;
  GetGeckoDragAction(grfKeyState, pdwEffect, &geckoAction);

  // Set the current action to the Gecko specific type
  RefPtr<nsDragSession> currSession =
      static_cast<nsDragSession*>(mDragService->GetCurrentSession(mWidget));
  if (!currSession) {
    LOGD("[%p] %s | No active drag session", this, __FUNCTION__);
    return;
  }

  currSession->SetDragAction(geckoAction);

  // Dispatch the event into Gecko
  DispatchDragDropEvent(aEventMessage, ptl);

  // If TakeChildProcessDragAction returns something other than
  // DRAGDROP_ACTION_UNINITIALIZED, it means that the last event was sent
  // to the child process and this event is also being sent to the child
  // process. In this case, use the last event's action instead.
  currSession->GetDragAction(&geckoAction);

  int32_t childDragAction = currSession->TakeChildProcessDragAction();
  if (childDragAction != nsIDragService::DRAGDROP_ACTION_UNINITIALIZED) {
    geckoAction = childDragAction;
  }

  if (nsIDragService::DRAGDROP_ACTION_LINK & geckoAction) {
    *pdwEffect = DROPEFFECT_LINK;
  } else if (nsIDragService::DRAGDROP_ACTION_COPY & geckoAction) {
    *pdwEffect = DROPEFFECT_COPY;
  } else if (nsIDragService::DRAGDROP_ACTION_MOVE & geckoAction) {
    *pdwEffect = DROPEFFECT_MOVE;
  } else {
    *pdwEffect = DROPEFFECT_NONE;
  }

  if (aEventMessage != eDrop) {
    // Get the cached drag effect from the drag service, the data member should
    // have been set by whoever handled the WidgetGUIEvent or nsIDOMEvent on
    // drags.
    bool canDrop;
    currSession->GetCanDrop(&canDrop);
    if (!canDrop) {
      *pdwEffect = DROPEFFECT_NONE;
    }
  }

  LOGD(
      "[%p] %s | grfKeyState: %lu | mEffectsAllowed: %lu | "
      "mEffectsPreferred: %lu | *pdwEffect: %lu | post dispatch geckoAction: "
      "%u",
      this, __FUNCTION__, grfKeyState, mEffectsAllowed, mEffectsPreferred,
      *pdwEffect, geckoAction);

  // Clear the cached value
  currSession->SetCanDrop(false);
}

// IDropTarget methods
STDMETHODIMP
nsNativeDragTarget::DragEnter(LPDATAOBJECT pIDataSource, DWORD grfKeyState,
                              POINTL ptl, DWORD* pdwEffect) {
  if (!mDragService) {
    LOGD("[%p] %s | No drag service", this, __FUNCTION__);
    return E_FAIL;
  }

  mEffectsAllowed = *pdwEffect;
  AddLinkSupportIfCanBeGenerated(pIDataSource);

  // Drag and drop image helper
  if (GetDropTargetHelper()) {
    // We get a lot of crashes (often uncaught by our handler) later on during
    // DragOver calls, see bug 1465513. It looks like this might be because
    // we're not cleaning up previous drags fully and now released resources get
    // used. Calling IDropTargetHelper::DragLeave before DragEnter seems to fix
    // this for at least one reproduction of this crash.
    GetDropTargetHelper()->DragLeave();
    POINT pt = {ptl.x, ptl.y};
    GetDropTargetHelper()->DragEnter(mHWnd, pIDataSource, &pt, *pdwEffect);
  }

  // save a ref to this, in case the window is destroyed underneath us
  NS_ASSERTION(!mTookOwnRef, "own ref already taken!");
  this->AddRef();
  mTookOwnRef = true;

  // tell the drag service about this drag (it may have come from an
  // outside app).
  RefPtr<nsDragSession> session =
      static_cast<nsDragSession*>(mDragService->StartDragSession(mWidget));
  MOZ_ASSERT(session);

  void* tempOutData = nullptr;
  uint32_t tempDataLen = 0;
  nsresult loadResult = nsClipboard::GetNativeDataOffClipboard(
      pIDataSource, 0, ::RegisterClipboardFormat(CFSTR_PREFERREDDROPEFFECT),
      nullptr, &tempOutData, &tempDataLen);
  if (NS_SUCCEEDED(loadResult) && tempOutData && tempDataLen >= sizeof(DWORD)) {
    mEffectsPreferred = *((DWORD*)tempOutData);
  } else {
    // We have no preference if we can't obtain it
    mEffectsPreferred = DROPEFFECT_NONE;
  }
  free(tempOutData);
  LOGD(
      "[%p] %s | grfKeyState: %lu | ptl: (%ld, %ld) | mEffectsAllowed: %lu"
      " | mEffectsPreferred: %lu",
      this, __FUNCTION__, grfKeyState, ptl.x, ptl.y, mEffectsAllowed,
      mEffectsPreferred);

  // Set the native data object into drag session
  session->SetIDataObject(pIDataSource);

  // Now process the native drag state and then dispatch the event
  ProcessDrag(eDragEnter, grfKeyState, ptl, pdwEffect);

  return S_OK;
}

void nsNativeDragTarget::AddLinkSupportIfCanBeGenerated(
    LPDATAOBJECT aIDataSource) {
  // If we don't have a link effect, but we can generate one, fix the
  // drop effect to include it.
  if (!(mEffectsAllowed & DROPEFFECT_LINK) && aIDataSource) {
    if (S_OK == ::OleQueryLinkFromData(aIDataSource)) {
      mEffectsAllowed |= DROPEFFECT_LINK;
    }
  }
}

STDMETHODIMP
nsNativeDragTarget::DragOver(DWORD grfKeyState, POINTL ptl, LPDWORD pdwEffect) {
  if (!mDragService) {
    LOGD("[%p] %s | No drag service", this, __FUNCTION__);
    return E_FAIL;
  }

  bool dragImageChanged = gDragImageChanged;
  gDragImageChanged = false;

  // If a LINK effect could be generated previously from a DragEnter(),
  // then we should include it as an allowed effect.
  mEffectsAllowed = (*pdwEffect) | (mEffectsAllowed & DROPEFFECT_LINK);

  RefPtr<nsDragSession> currentDragSession =
      static_cast<nsDragSession*>(mDragService->GetCurrentSession(mWidget));
  if (!currentDragSession) {
    LOGD("[%p] %s | No drag session", this, __FUNCTION__);
    return S_OK;  // Drag was canceled.
  }

  // without the AddRef() |this| can get destroyed in an event handler
  this->AddRef();

  // Drag and drop image helper
  if (GetDropTargetHelper()) {
    if (dragImageChanged) {
      // See comment in nsNativeDragTarget::DragEnter.
      GetDropTargetHelper()->DragLeave();
      // The drop helper only updates the image during DragEnter, so emulate
      // a DragEnter if the image was changed.
      POINT pt = {ptl.x, ptl.y};
      GetDropTargetHelper()->DragEnter(
          mHWnd, currentDragSession->GetDataObject(), &pt, *pdwEffect);
    }
    POINT pt = {ptl.x, ptl.y};
    GetDropTargetHelper()->DragOver(&pt, *pdwEffect);
  }

  LOGD(
      "[%p] %s | grfKeyState: %lu | ptl: (%ld, %ld) | *pdwEffect: %lu | "
      "mEffectsAllowed: %lu | dragImageChanged: %s",
      this, __FUNCTION__, grfKeyState, ptl.x, ptl.y, *pdwEffect,
      mEffectsAllowed, TrueOrFalse(dragImageChanged));
  LOGI("[%p] %s | Firing 'drag' event at drag source, if available", this,
       __FUNCTION__);

  ModifierKeyState modifierKeyState;
  currentDragSession->FireDragEventAtSource(eDrag,
                                            modifierKeyState.GetModifiers());
  // Now process the native drag state and then dispatch the event
  LOGI("[%p] %s | Firing 'dragover' event at drag target", this, __FUNCTION__);
  ProcessDrag(eDragOver, grfKeyState, ptl, pdwEffect);

  this->Release();

  return S_OK;
}

STDMETHODIMP
nsNativeDragTarget::DragLeave() {
  if (!mDragService) {
    LOGD("[%p] %s | No drag service", this, __FUNCTION__);
    return E_FAIL;
  }

  // Drag and drop image helper
  if (GetDropTargetHelper()) {
    GetDropTargetHelper()->DragLeave();
  }

  // dispatch the event into Gecko
  LOGI("[%p] %s | dispatching dragexit event", this, __FUNCTION__);
  DispatchDragDropEvent(eDragExit, gDragLastPoint);

  nsCOMPtr<nsIDragSession> currentDragSession =
      mDragService->GetCurrentSession(mWidget);

  if (currentDragSession) {
    nsCOMPtr<nsINode> sourceNode;
    currentDragSession->GetSourceNode(getter_AddRefs(sourceNode));

    if (!sourceNode) {
      // We're leaving a window while doing a drag that was
      // initiated in a different app. End the drag session, since
      // we're done with it for now (until the user drags back into
      // mozilla).
      LOGI("[%p] %s | ending drag session with external (non-Gecko) source",
           this, __FUNCTION__);
      ModifierKeyState modifierKeyState;
      currentDragSession->EndDragSession(false,
                                         modifierKeyState.GetModifiers());
    }
  }

  // release the ref that was taken in DragEnter
  NS_ASSERTION(mTookOwnRef, "want to release own ref, but not taken!");
  if (mTookOwnRef) {
    this->Release();
    mTookOwnRef = false;
  }

  return S_OK;
}

void nsNativeDragTarget::DragCancel() {
  LOGD("[%p] %s", this, __FUNCTION__);
  // Cancel the drag session if we did DragEnter.
  if (mTookOwnRef) {
    if (GetDropTargetHelper()) {
      GetDropTargetHelper()->DragLeave();
    }
    if (mDragService) {
      ModifierKeyState modifierKeyState;
      RefPtr<nsIDragSession> session = mDragService->GetCurrentSession(mWidget);
      if (session) {
        LOGI("[%p] %s | modifier-keys: %u | ending canceled drag session", this,
             __FUNCTION__, modifierKeyState.GetModifiers());
        session->EndDragSession(false, modifierKeyState.GetModifiers());
      }
    }
    this->Release();  // matching the AddRef in DragEnter
    mTookOwnRef = false;
  }
}

STDMETHODIMP
nsNativeDragTarget::Drop(LPDATAOBJECT pData, DWORD grfKeyState, POINTL aPT,
                         LPDWORD pdwEffect) {
  if (!mDragService) {
    LOGD("[%p] %s | No drag service", this, __FUNCTION__);
    return E_FAIL;
  }

  mEffectsAllowed = *pdwEffect;
  AddLinkSupportIfCanBeGenerated(pData);

  // Drag and drop image helper
  if (GetDropTargetHelper()) {
    POINT pt = {aPT.x, aPT.y};
    GetDropTargetHelper()->Drop(pData, &pt, *pdwEffect);
  }

  // Set the native data object into the drag service
  RefPtr<nsDragSession> currentDragSession =
      static_cast<nsDragSession*>(mDragService->GetCurrentSession(mWidget));
  if (!currentDragSession) {
    LOGD("[%p] %s | No drag session", this, __FUNCTION__);
    return S_OK;
  }
  currentDragSession->SetIDataObject(pData);

  // NOTE: ProcessDrag spins the event loop which may destroy arbitrary objects.
  // We use strong refs to prevent it from destroying these:
  RefPtr<nsNativeDragTarget> kungFuDeathGrip = this;

  LOGI(
      "[%p] %s | grfKeyState: %lu | aPT: (%ld, %ld) | *pdwEffect: %lu | "
      "dispatching drop event",
      this, __FUNCTION__, grfKeyState, aPT.x, aPT.y, *pdwEffect);

  // Now process the native drag state and then dispatch the event
  ProcessDrag(eDrop, grfKeyState, aPT, pdwEffect);

  currentDragSession =
      static_cast<nsDragSession*>(mDragService->GetCurrentSession(mWidget));
  if (!currentDragSession) {
    LOGD("[%p] %s | Drag session was canceled during drop event", this,
         __FUNCTION__);
    return S_OK;  // DragCancel() was called.
  }

  // Let the win drag session know whether it experienced
  // a drop event within the application. Drop will not oocur if the
  // drop landed outside the app. (used in tab tear off, bug 455884)
  currentDragSession->SetDroppedLocal();

  // Tell the drag session we're done with it.
  // Use GetMessagePos to get the position of the mouse at the last message
  // seen by the event loop. (Bug 489729)
  DWORD pos = ::GetMessagePos();
  POINT cpos;
  cpos.x = GET_X_LPARAM(pos);
  cpos.y = GET_Y_LPARAM(pos);
  currentDragSession->SetDragEndPoint(cpos.x, cpos.y);
  ModifierKeyState modifierKeyState;
  LOGI(
      "[%p] %s | position: (%lu, %lu) | modifier-keys: %u | ending completed "
      "drag session",
      this, __FUNCTION__, cpos.x, cpos.y, modifierKeyState.GetModifiers());
  currentDragSession->EndDragSession(true, modifierKeyState.GetModifiers());

  // release the ref that was taken in DragEnter
  NS_ASSERTION(mTookOwnRef, "want to release own ref, but not taken!");
  if (mTookOwnRef) {
    this->Release();
    mTookOwnRef = false;
  }

  return S_OK;
}

/**
 * By lazy loading mDropTargetHelper we save 50-70ms of startup time
 * which is ~5% of startup time.
 */
IDropTargetHelper* nsNativeDragTarget::GetDropTargetHelper() {
  if (!mDropTargetHelper) {
    CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER,
                     IID_IDropTargetHelper, (LPVOID*)&mDropTargetHelper);
  }

  return mDropTargetHelper;
}
