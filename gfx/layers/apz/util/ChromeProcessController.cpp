/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ChromeProcessController.h"

#include "MainThreadUtils.h"  // for NS_IsMainThread()
#include "base/task.h"
#include "mozilla/PresShell.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/PointerEventHandler.h"
#include "mozilla/layers/APZCCallbackHelper.h"
#include "mozilla/layers/APZEventState.h"
#include "mozilla/layers/APZThreadUtils.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/DoubleTapToZoom.h"
#include "mozilla/layers/IAPZCTreeManager.h"
#include "mozilla/layers/InputAPZContext.h"
#include "mozilla/layers/RepaintRequest.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsLayoutUtils.h"

static mozilla::LazyLogModule sApzChromeLog("apz.cc.chrome");

using namespace mozilla;
using namespace mozilla::layers;
using namespace mozilla::widget;

ChromeProcessController::ChromeProcessController(
    nsIWidget* aWidget, APZEventState* aAPZEventState,
    IAPZCTreeManager* aAPZCTreeManager)
    : mWidget(aWidget),
      mAPZEventState(aAPZEventState),
      mAPZCTreeManager(aAPZCTreeManager),
      mUIThread(NS_GetCurrentThread()) {
  // Otherwise we're initializing mUIThread incorrectly.
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aAPZEventState);
  MOZ_ASSERT(aAPZCTreeManager);

  mUIThread->Dispatch(
      NewRunnableMethod("layers::ChromeProcessController::InitializeRoot", this,
                        &ChromeProcessController::InitializeRoot));
}

ChromeProcessController::~ChromeProcessController() = default;

void ChromeProcessController::InitializeRoot() {
  nsIFrame* widgetFrame = GetWidgetFrame();
  if (widgetFrame && widgetFrame->IsMenuPopupFrame()) {
    // For popup window, the menu frame should be the root and have
    // the display port.
    APZCCallbackHelper::InitializeRootDisplayport(widgetFrame);
    return;
  }

  APZCCallbackHelper::InitializeRootDisplayport(GetPresShell());
}

void ChromeProcessController::NotifyLayerTransforms(
    nsTArray<MatrixMessage>&& aTransforms) {
  if (!mUIThread->IsOnCurrentThread()) {
    mUIThread->Dispatch(
        NewRunnableMethod<StoreCopyPassByRRef<nsTArray<MatrixMessage>>>(
            "layers::ChromeProcessController::NotifyLayerTransforms", this,
            &ChromeProcessController::NotifyLayerTransforms,
            std::move(aTransforms)));
    return;
  }

  APZCCallbackHelper::NotifyLayerTransforms(aTransforms);
}

void ChromeProcessController::RequestContentRepaint(
    const RepaintRequest& aRequest) {
  MOZ_RELEASE_ASSERT(IsRepaintThread());

  if (aRequest.IsRootContent()) {
    APZCCallbackHelper::UpdateRootFrame(aRequest);
  } else {
    APZCCallbackHelper::UpdateSubFrame(aRequest);
  }
}

bool ChromeProcessController::IsRepaintThread() { return NS_IsMainThread(); }

void ChromeProcessController::DispatchToRepaintThread(
    already_AddRefed<Runnable> aTask) {
  NS_DispatchToMainThread(std::move(aTask));
}

void ChromeProcessController::Destroy() {
  if (!mUIThread->IsOnCurrentThread()) {
    mUIThread->Dispatch(
        NewRunnableMethod("layers::ChromeProcessController::Destroy", this,
                          &ChromeProcessController::Destroy));
    return;
  }

  MOZ_RELEASE_ASSERT(mUIThread->IsOnCurrentThread());
  mWidget = nullptr;
  if (mAPZEventState) {
    mAPZEventState->Destroy();
  }
  mAPZEventState = nullptr;
}

PresShell* ChromeProcessController::GetPresShell() const {
  if (!mWidget) {
    return nullptr;
  }
  auto* frame = mWidget->GetFrame();
  return frame ? frame->PresShell() : nullptr;
}

dom::Document* ChromeProcessController::GetRootDocument() const {
  if (PresShell* presShell = GetPresShell()) {
    return presShell->GetDocument();
  }
  return nullptr;
}

dom::Document* ChromeProcessController::GetRootContentDocument(
    const ScrollableLayerGuid::ViewID& aScrollId) const {
  nsIContent* content = nsLayoutUtils::FindContentFor(aScrollId);
  if (!content) {
    return nullptr;
  }
  if (PresShell* presShell =
          APZCCallbackHelper::GetRootContentDocumentPresShellForContent(
              content)) {
    return presShell->GetDocument();
  }
  return nullptr;
}

void ChromeProcessController::HandleDoubleTap(
    const mozilla::CSSPoint& aPoint, Modifiers aModifiers,
    const ScrollableLayerGuid& aGuid,
    const DoubleTapToZoomMetrics& aDoubleTapToZoomMetrics) {
  MOZ_LOG(sApzChromeLog, LogLevel::Debug, ("HandleDoubleTap\n"));
  MOZ_RELEASE_ASSERT(mUIThread->IsOnCurrentThread());

  RefPtr<dom::Document> document = GetRootContentDocument(aGuid.mScrollId);
  if (!document) {
    return;
  }

  ZoomTarget zoomTarget =
      CalculateRectToZoomTo(document, aPoint, aDoubleTapToZoomMetrics);

  mAPZCTreeManager->ZoomToRect(aGuid, zoomTarget,
                               ZoomToRectBehavior::DEFAULT_BEHAVIOR);
}

void ChromeProcessController::HandleTap(
    TapType aType, const mozilla::LayoutDevicePoint& aPoint,
    Modifiers aModifiers, const ScrollableLayerGuid& aGuid,
    uint64_t aInputBlockId,
    const Maybe<DoubleTapToZoomMetrics>& aDoubleTapToZoomMetrics) {
  MOZ_LOG(sApzChromeLog, LogLevel::Debug,
          ("HandleTap called with %d\n", (int)aType));
  if (!mUIThread->IsOnCurrentThread()) {
    MOZ_LOG(sApzChromeLog, LogLevel::Debug, ("HandleTap redispatching\n"));
    mUIThread->Dispatch(
        NewRunnableMethod<TapType, mozilla::LayoutDevicePoint, Modifiers,
                          ScrollableLayerGuid, uint64_t,
                          Maybe<DoubleTapToZoomMetrics>>(
            "layers::ChromeProcessController::HandleTap", this,
            &ChromeProcessController::HandleTap, aType, aPoint, aModifiers,
            aGuid, aInputBlockId, aDoubleTapToZoomMetrics));
    return;
  }

  if (!mAPZEventState) {
    return;
  }

  RefPtr<PresShell> presShell = GetPresShell();
  if (!presShell) {
    return;
  }
  if (!presShell->GetPresContext()) {
    return;
  }
  CSSToLayoutDeviceScale scale(
      presShell->GetPresContext()->CSSToDevPixelScale());

  CSSPoint point = aPoint / scale;

  // Stash the guid in InputAPZContext so that when the visual-to-layout
  // transform is applied to the event's coordinates, we use the right transform
  // based on the scroll frame being targeted.
  // The other values don't really matter.
  InputAPZContext context(aGuid, aInputBlockId, nsEventStatus_eSentinel);

  switch (aType) {
    case TapType::eSingleTap: {
      RefPtr<APZEventState> eventState(mAPZEventState);
      eventState->ProcessSingleTap(point, scale, aModifiers, 1, aInputBlockId);
      break;
    }
    case TapType::eDoubleTap:
      MOZ_ASSERT(aDoubleTapToZoomMetrics);
      HandleDoubleTap(point, aModifiers, aGuid, *aDoubleTapToZoomMetrics);
      break;
    case TapType::eSecondTap: {
      RefPtr<APZEventState> eventState(mAPZEventState);
      eventState->ProcessSingleTap(point, scale, aModifiers, 2, aInputBlockId);
      break;
    }
    case TapType::eLongTap: {
      RefPtr<APZEventState> eventState(mAPZEventState);
      eventState->ProcessLongTap(presShell, point, scale, aModifiers,
                                 aInputBlockId);
      break;
    }
    case TapType::eLongTapUp: {
      RefPtr<APZEventState> eventState(mAPZEventState);
      eventState->ProcessLongTapUp(presShell, point, scale, aModifiers);
      break;
    }
  }

  // mAPZEventState may not dispatch the compatibility mouse events.  Therefore,
  // we should release the pointer capturing element at the last ePointerUp
  // here.
  PointerEventHandler::ReleasePointerCapturingElementAtLastPointerUp();
}

void ChromeProcessController::NotifyPinchGesture(
    PinchGestureInput::PinchGestureType aType, const ScrollableLayerGuid& aGuid,
    const LayoutDevicePoint& aFocusPoint, LayoutDeviceCoord aSpanChange,
    Modifiers aModifiers) {
  if (!mUIThread->IsOnCurrentThread()) {
    mUIThread->Dispatch(
        NewRunnableMethod<PinchGestureInput::PinchGestureType,
                          ScrollableLayerGuid, LayoutDevicePoint,
                          LayoutDeviceCoord, Modifiers>(
            "layers::ChromeProcessController::NotifyPinchGesture", this,
            &ChromeProcessController::NotifyPinchGesture, aType, aGuid,
            aFocusPoint, aSpanChange, aModifiers));
    return;
  }

  if (mWidget) {
    // Dispatch the call to APZCCallbackHelper::NotifyPinchGesture to the main
    // thread so that it runs asynchronously from the current call. This is
    // because the call can run arbitrary JS code, which can also spin the event
    // loop and cause undesirable re-entrancy in APZ.
    mUIThread->Dispatch(NewRunnableFunction(
        "layers::ChromeProcessController::NotifyPinchGestureAsync",
        &APZCCallbackHelper::NotifyPinchGesture, aType, aFocusPoint,
        aSpanChange, aModifiers, mWidget));
  }
}

void ChromeProcessController::NotifyAPZStateChange(
    const ScrollableLayerGuid& aGuid, APZStateChange aChange, int aArg,
    Maybe<uint64_t> aInputBlockId) {
  if (!mUIThread->IsOnCurrentThread()) {
    mUIThread->Dispatch(NewRunnableMethod<ScrollableLayerGuid, APZStateChange,
                                          int, Maybe<uint64_t>>(
        "layers::ChromeProcessController::NotifyAPZStateChange", this,
        &ChromeProcessController::NotifyAPZStateChange, aGuid, aChange, aArg,
        aInputBlockId));
    return;
  }

  if (!mAPZEventState) {
    return;
  }

  mAPZEventState->ProcessAPZStateChange(aGuid.mScrollId, aChange, aArg,
                                        aInputBlockId);
}

void ChromeProcessController::NotifyMozMouseScrollEvent(
    const ScrollableLayerGuid::ViewID& aScrollId, const nsString& aEvent) {
  if (!mUIThread->IsOnCurrentThread()) {
    mUIThread->Dispatch(
        NewRunnableMethod<ScrollableLayerGuid::ViewID, nsString>(
            "layers::ChromeProcessController::NotifyMozMouseScrollEvent", this,
            &ChromeProcessController::NotifyMozMouseScrollEvent, aScrollId,
            aEvent));
    return;
  }

  APZCCallbackHelper::NotifyMozMouseScrollEvent(aScrollId, aEvent);
}

void ChromeProcessController::NotifyFlushComplete() {
  MOZ_RELEASE_ASSERT(IsRepaintThread());

  APZCCallbackHelper::NotifyFlushComplete(GetPresShell());
}

void ChromeProcessController::NotifyAsyncScrollbarDragInitiated(
    uint64_t aDragBlockId, const ScrollableLayerGuid::ViewID& aScrollId,
    ScrollDirection aDirection) {
  if (!mUIThread->IsOnCurrentThread()) {
    mUIThread->Dispatch(NewRunnableMethod<uint64_t, ScrollableLayerGuid::ViewID,
                                          ScrollDirection>(
        "layers::ChromeProcessController::NotifyAsyncScrollbarDragInitiated",
        this, &ChromeProcessController::NotifyAsyncScrollbarDragInitiated,
        aDragBlockId, aScrollId, aDirection));
    return;
  }

  APZCCallbackHelper::NotifyAsyncScrollbarDragInitiated(aDragBlockId, aScrollId,
                                                        aDirection);
}

void ChromeProcessController::NotifyAsyncScrollbarDragRejected(
    const ScrollableLayerGuid::ViewID& aScrollId) {
  if (!mUIThread->IsOnCurrentThread()) {
    mUIThread->Dispatch(NewRunnableMethod<ScrollableLayerGuid::ViewID>(
        "layers::ChromeProcessController::NotifyAsyncScrollbarDragRejected",
        this, &ChromeProcessController::NotifyAsyncScrollbarDragRejected,
        aScrollId));
    return;
  }

  APZCCallbackHelper::NotifyAsyncScrollbarDragRejected(aScrollId);
}

void ChromeProcessController::NotifyAsyncAutoscrollRejected(
    const ScrollableLayerGuid::ViewID& aScrollId) {
  if (!mUIThread->IsOnCurrentThread()) {
    mUIThread->Dispatch(NewRunnableMethod<ScrollableLayerGuid::ViewID>(
        "layers::ChromeProcessController::NotifyAsyncAutoscrollRejected", this,
        &ChromeProcessController::NotifyAsyncAutoscrollRejected, aScrollId));
    return;
  }

  APZCCallbackHelper::NotifyAsyncAutoscrollRejected(aScrollId);
}

void ChromeProcessController::CancelAutoscroll(
    const ScrollableLayerGuid& aGuid) {
  mUIThread->Dispatch(NewRunnableFunction("layers::CancelAutoscroll",
                                          &APZCCallbackHelper::CancelAutoscroll,
                                          aGuid.mScrollId));
}

void ChromeProcessController::NotifyScaleGestureComplete(
    const ScrollableLayerGuid& aGuid, float aScale) {
  if (!mUIThread->IsOnCurrentThread()) {
    mUIThread->Dispatch(NewRunnableMethod<ScrollableLayerGuid, float>(
        "layers::ChromeProcessController::NotifyScaleGestureComplete", this,
        &ChromeProcessController::NotifyScaleGestureComplete, aGuid, aScale));
    return;
  }

  if (mWidget) {
    // Dispatch the call to APZCCallbackHelper::NotifyScaleGestureComplete
    // to the main thread so that it runs asynchronously from the current call.
    // This is because the call can run arbitrary JS code, which can also spin
    // the event loop and cause undesirable re-entrancy in APZ.
    mUIThread->Dispatch(NewRunnableFunction(
        "layers::ChromeProcessController::NotifyScaleGestureComplete",
        &APZCCallbackHelper::NotifyScaleGestureComplete, mWidget, aScale));
  }
}

void ChromeProcessController::NotifyTouchSwipableDirections(
    uint64_t aInputBlockId, SideBits aSwipableDirections) {
  // mWidget is cleared on mUIThread in Destroy(), so it must only be read on
  // mUIThread.
  if (!mUIThread->IsOnCurrentThread()) {
    mUIThread->Dispatch(NewRunnableMethod<uint64_t, SideBits>(
        "layers::ChromeProcessController::NotifyTouchSwipableDirections", this,
        &ChromeProcessController::NotifyTouchSwipableDirections, aInputBlockId,
        aSwipableDirections));
    return;
  }

  if (mWidget) {
    // Dispatch asynchronously, because this can be called from within
    // InputQueue and the widget may call back into APZ (e.g.
    // SetBrowserGestureResponse) while handling it.
    mUIThread->Dispatch(NewRunnableMethod<uint64_t, SideBits>(
        "nsIWidget::ReceiveTouchSwipableDirections", mWidget,
        &nsIWidget::ReceiveTouchSwipableDirections, aInputBlockId,
        aSwipableDirections));
  }
}

nsIFrame* ChromeProcessController::GetWidgetFrame() const {
  return mWidget ? mWidget->GetFrame() : nullptr;
}
