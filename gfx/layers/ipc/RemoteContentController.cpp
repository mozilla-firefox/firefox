/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/RemoteContentController.h"

#include "CompositorThread.h"
#include "MainThreadUtils.h"
#include "Units.h"
#include "ipc/RemoteContentController.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/layers/APZCCallbackHelper.h"
#include "mozilla/layers/APZThreadUtils.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/DoubleTapToZoom.h"
#include "mozilla/layers/MatrixMessage.h"
#include "nsThreadUtils.h"
#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/jni/Utils.h"
#endif

static mozilla::LazyLogModule sApzRemoteLog("apz.cc.remote");

namespace mozilla {
namespace layers {

using namespace mozilla::gfx;

RemoteContentController::RemoteContentController(const LayersId& aLayersId)
    : mLayersId(aLayersId),
      mCompositorThread(NS_GetCurrentThread()),
      mCanSend(true) {
  MOZ_RELEASE_ASSERT(CompositorThread()->IsOnCurrentThread());
}

RemoteContentController::~RemoteContentController() = default;

// Note APZBridgeParent is bound to the main thread (See
// PAPZBridge.ipdl).
void RemoteContentController::NotifyLayerTransforms(
    nsTArray<MatrixMessage>&& aTransforms) {
  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(
        NewRunnableMethod<StoreCopyPassByRRef<nsTArray<MatrixMessage>>>(
            "layers::RemoteContentController::NotifyLayerTransforms", this,
            &RemoteContentController::NotifyLayerTransforms,
            std::move(aTransforms)));
    return;
  }

  auto apzib = CompositorBridgeParent::GetApzBridgeParentForRoot(mLayersId);
  if (apzib) {
    (void)apzib->SendLayerTransforms(aTransforms);
  }
}

void RemoteContentController::RequestContentRepaint(
    const RepaintRequest& aRequest) {
  MOZ_RELEASE_ASSERT(IsRepaintThread());

  if (mCanSend) {
    (void)SendRequestContentRepaint(aRequest);
  }
}

void RemoteContentController::HandleTapOnParentProcessMainThread(
    TapType aTapType, LayoutDevicePoint aPoint, Modifiers aModifiers,
    ScrollableLayerGuid aGuid, uint64_t aInputBlockId,
    const Maybe<DoubleTapToZoomMetrics>& aDoubleTapToZoomMetrics) {
  MOZ_LOG(sApzRemoteLog, LogLevel::Debug,
          ("HandleTapOnMainThread(%d)", (int)aTapType));
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  // Hold strong reference to BrowserParent because SendHandleTap
  // can run script via SetFocus.
  RefPtr<dom::BrowserParent> tab =
      dom::BrowserParent::GetBrowserParentFromLayersId(aGuid.mLayersId);
  if (tab) {
    tab->SendHandleTap(aTapType, aPoint, aModifiers, aGuid, aInputBlockId,
                       aDoubleTapToZoomMetrics);
  }
}

void RemoteContentController::HandleTapOnGPUProcessMainThread(
    TapType aTapType, LayoutDevicePoint aPoint, Modifiers aModifiers,
    ScrollableLayerGuid aGuid, uint64_t aInputBlockId,
    const Maybe<DoubleTapToZoomMetrics>& aDoubleTapToZoomMetrics) {
  MOZ_RELEASE_ASSERT(XRE_IsGPUProcess());
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  // Send a message to the controller thread to handle the single-tap gesture.
  auto apzib =
      CompositorBridgeParent::GetApzBridgeParentForRoot(aGuid.mLayersId);
  if (apzib) {
    (void)apzib->SendHandleTap(aTapType, aPoint, aModifiers, aGuid,
                               aInputBlockId, aDoubleTapToZoomMetrics);
  }
}

void RemoteContentController::HandleTap(
    TapType aTapType, const LayoutDevicePoint& aPoint, Modifiers aModifiers,
    const ScrollableLayerGuid& aGuid, uint64_t aInputBlockId,
    const Maybe<DoubleTapToZoomMetrics>& aDoubleTapToZoomMetrics) {
  MOZ_LOG(sApzRemoteLog, LogLevel::Debug, ("HandleTap(%d)", (int)aTapType));
  APZThreadUtils::AssertOnControllerThread();

  if (XRE_GetProcessType() == GeckoProcessType_GPU) {
    if (NS_IsMainThread()) {
      HandleTapOnGPUProcessMainThread(aTapType, aPoint, aModifiers, aGuid,
                                      aInputBlockId, aDoubleTapToZoomMetrics);
    } else {
      NS_DispatchToMainThread(NewRunnableMethod<TapType, LayoutDevicePoint,
                                                Modifiers, ScrollableLayerGuid,
                                                uint64_t,
                                                Maybe<DoubleTapToZoomMetrics>>(
          "layers::RemoteContentController::HandleTapOnGPUProcessMainThread",
          this, &RemoteContentController::HandleTapOnGPUProcessMainThread,
          aTapType, aPoint, aModifiers, aGuid, aInputBlockId,
          aDoubleTapToZoomMetrics));
    }
    return;
  }

  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());

  if (NS_IsMainThread()) {
    HandleTapOnParentProcessMainThread(aTapType, aPoint, aModifiers, aGuid,
                                       aInputBlockId, aDoubleTapToZoomMetrics);
  } else {
    // We must be on Android, running on the Java UI thread
#ifndef MOZ_WIDGET_ANDROID
    MOZ_RELEASE_ASSERT(false);
#else
    // We don't want to get the BrowserParent or call
    // BrowserParent::SendHandleTap() from a non-main thread, so we need to
    // redispatch to the main thread. However, we should use the same
    // mechanism that the Android widget uses when dispatching input events
    // to Gecko, which is nsAppShell::PostEvent. Note in particular that
    // using NS_DispatchToMainThread would post to a different message loop,
    // and introduces the possibility of this tap event getting processed
    // out of order with respect to the touch events that synthesized it.
    mozilla::jni::DispatchToGeckoPriorityQueue(NewRunnableMethod<
                                               TapType, LayoutDevicePoint,
                                               Modifiers, ScrollableLayerGuid,
                                               uint64_t,
                                               Maybe<DoubleTapToZoomMetrics>>(
        "layers::RemoteContentController::HandleTapOnParentProcessMainThread",
        this, &RemoteContentController::HandleTapOnParentProcessMainThread,
        aTapType, aPoint, aModifiers, aGuid, aInputBlockId,
        aDoubleTapToZoomMetrics));
#endif
  }
}

// Note APZBridgeParent is bound to the main thread (See
// PAPZBridge.ipdl).
void RemoteContentController::NotifyPinchGestureOnGPUProcessMainThread(
    PinchGestureInput::PinchGestureType aType, const ScrollableLayerGuid& aGuid,
    const LayoutDevicePoint& aFocusPoint, LayoutDeviceCoord aSpanChange,
    Modifiers aModifiers) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  auto apzib =
      CompositorBridgeParent::GetApzBridgeParentForRoot(aGuid.mLayersId);
  if (apzib) {
    (void)apzib->SendNotifyPinchGesture(aType, aGuid, aFocusPoint, aSpanChange,
                                        aModifiers);
  }
}

void RemoteContentController::NotifyPinchGesture(
    PinchGestureInput::PinchGestureType aType, const ScrollableLayerGuid& aGuid,
    const LayoutDevicePoint& aFocusPoint, LayoutDeviceCoord aSpanChange,
    Modifiers aModifiers) {
  APZThreadUtils::AssertOnControllerThread();

  // For now we handle this NotifyPinchGesture message only in
  // the parent process.

  // If we're in the GPU process, try to find a handle to the parent process
  // and send it there.
  if (XRE_IsGPUProcess()) {
    if (NS_IsMainThread()) {
      NotifyPinchGestureOnGPUProcessMainThread(aType, aGuid, aFocusPoint,
                                               aSpanChange, aModifiers);
    } else {
      NS_DispatchToMainThread(NewRunnableMethod<
                              PinchGestureInput::PinchGestureType,
                              ScrollableLayerGuid, LayoutDevicePoint,
                              LayoutDeviceCoord, Modifiers>(
          "layers::RemoteContentController::"
          "NotifyPinchGestureOnGPUProcessMainThread",
          this,
          &RemoteContentController::NotifyPinchGestureOnGPUProcessMainThread,
          aType, aGuid, aFocusPoint, aSpanChange, aModifiers));
    }
    return;
  }

  // If we're in the parent process, handle it directly. We don't have a
  // handle to the widget though, so we fish out the ChromeProcessController
  // and delegate to that instead.
  if (XRE_IsParentProcess()) {
    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    RefPtr<GeckoContentController> rootController =
        CompositorBridgeParent::GetGeckoContentControllerForRoot(
            aGuid.mLayersId);
    if (rootController) {
      rootController->NotifyPinchGesture(aType, aGuid, aFocusPoint, aSpanChange,
                                         aModifiers);
    }
  }
}

bool RemoteContentController::IsRepaintThread() {
  return mCompositorThread->IsOnCurrentThread();
}

void RemoteContentController::DispatchToRepaintThread(
    already_AddRefed<Runnable> aTask) {
  mCompositorThread->Dispatch(std::move(aTask));
}

void RemoteContentController::NotifyAPZStateChange(
    const ScrollableLayerGuid& aGuid, APZStateChange aChange, int aArg,
    Maybe<uint64_t> aInputBlockId) {
  if (!mCompositorThread->IsOnCurrentThread()) {
    // We have to send messages from the compositor thread
    mCompositorThread->Dispatch(
        NewRunnableMethod<ScrollableLayerGuid, APZStateChange, int,
                          Maybe<uint64_t>>(
            "layers::RemoteContentController::NotifyAPZStateChange", this,
            &RemoteContentController::NotifyAPZStateChange, aGuid, aChange,
            aArg, aInputBlockId));
    return;
  }

  if (mCanSend) {
    (void)SendNotifyAPZStateChange(aGuid, aChange, aArg, aInputBlockId);
  }
}

void RemoteContentController::UpdateOverscrollVelocity(
    const ScrollableLayerGuid& aGuid, float aX, float aY, bool aIsRootContent) {
  if (XRE_IsParentProcess()) {
#ifdef MOZ_WIDGET_ANDROID
    // We always want these to go to the parent process on Android
    if (!NS_IsMainThread()) {
      mozilla::jni::DispatchToGeckoPriorityQueue(
          NewRunnableMethod<ScrollableLayerGuid, float, float, bool>(
              "layers::RemoteContentController::UpdateOverscrollVelocity", this,
              &RemoteContentController::UpdateOverscrollVelocity, aGuid, aX, aY,
              aIsRootContent));
      return;
    }
#endif

    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    RefPtr<GeckoContentController> rootController =
        CompositorBridgeParent::GetGeckoContentControllerForRoot(
            aGuid.mLayersId);
    if (rootController) {
      rootController->UpdateOverscrollVelocity(aGuid, aX, aY, aIsRootContent);
    }
  } else if (XRE_IsGPUProcess()) {
    // Note APZBridgeParent is bound to the main thread (See
    // PAPZBridge.ipdl).
    if (!NS_IsMainThread()) {
      NS_DispatchToMainThread(
          NewRunnableMethod<ScrollableLayerGuid, float, float, bool>(
              "layers::RemoteContentController::UpdateOverscrollVelocity", this,
              &RemoteContentController::UpdateOverscrollVelocity, aGuid, aX, aY,
              aIsRootContent));
      return;
    }

    auto apzib =
        CompositorBridgeParent::GetApzBridgeParentForRoot(aGuid.mLayersId);
    if (apzib) {
      (void)apzib->SendUpdateOverscrollVelocity(aGuid, aX, aY, aIsRootContent);
    }
  }
}

void RemoteContentController::UpdateOverscrollOffset(
    const ScrollableLayerGuid& aGuid, float aX, float aY, bool aIsRootContent) {
  if (XRE_IsParentProcess()) {
#ifdef MOZ_WIDGET_ANDROID
    // We always want these to go to the parent process on Android
    if (!NS_IsMainThread()) {
      mozilla::jni::DispatchToGeckoPriorityQueue(
          NewRunnableMethod<ScrollableLayerGuid, float, float, bool>(
              "layers::RemoteContentController::UpdateOverscrollOffset", this,
              &RemoteContentController::UpdateOverscrollOffset, aGuid, aX, aY,
              aIsRootContent));
      return;
    }
#endif

    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    RefPtr<GeckoContentController> rootController =
        CompositorBridgeParent::GetGeckoContentControllerForRoot(
            aGuid.mLayersId);
    if (rootController) {
      rootController->UpdateOverscrollOffset(aGuid, aX, aY, aIsRootContent);
    }
  } else if (XRE_IsGPUProcess()) {
    // Note APZBridgeParent is bound to the main thread (See
    // PAPZBridge.ipdl).
    if (!NS_IsMainThread()) {
      NS_DispatchToMainThread(
          NewRunnableMethod<ScrollableLayerGuid, float, float, bool>(
              "layers::RemoteContentController::UpdateOverscrollOffset", this,
              &RemoteContentController::UpdateOverscrollOffset, aGuid, aX, aY,
              aIsRootContent));
      return;
    }

    auto apzib =
        CompositorBridgeParent::GetApzBridgeParentForRoot(aGuid.mLayersId);
    if (apzib) {
      (void)apzib->SendUpdateOverscrollOffset(aGuid, aX, aY, aIsRootContent);
    }
  }
}

void RemoteContentController::HideDynamicToolbar(
    const ScrollableLayerGuid& aGuid) {
  // We always want these to go to the parent process
  if (XRE_IsParentProcess()) {
#ifdef MOZ_WIDGET_ANDROID
    if (!NS_IsMainThread()) {
      mozilla::jni::DispatchToGeckoPriorityQueue(
          NewRunnableMethod<ScrollableLayerGuid>(
              "layers::RemoteContentController::HideDynamicToolbar", this,
              &RemoteContentController::HideDynamicToolbar, aGuid));
      return;
    }
#endif

    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    RefPtr<GeckoContentController> rootController =
        CompositorBridgeParent::GetGeckoContentControllerForRoot(
            aGuid.mLayersId);
    if (rootController) {
      rootController->HideDynamicToolbar(aGuid);
    }
  } else if (XRE_IsGPUProcess()) {
    // Note APZBridgeParent is bound to the main thread (See
    // PAPZBridge.ipdl).
    if (!NS_IsMainThread()) {
      NS_DispatchToMainThread(NewRunnableMethod<ScrollableLayerGuid>(
          "layers::RemoteContentController::HideDynamicToolbar", this,
          &RemoteContentController::HideDynamicToolbar, aGuid));
      return;
    }

    auto apzib =
        CompositorBridgeParent::GetApzBridgeParentForRoot(aGuid.mLayersId);
    if (apzib) {
      (void)apzib->SendHideDynamicToolbar();
    }
  }
}

void RemoteContentController::NotifyTouchSwipableDirections(
    uint64_t aInputBlockId, SideBits aSwipableDirections) {
  // This is only called on the controller for the root layers id, which is a
  // RemoteContentController only in the GPU process. In the parent process,
  // the root controller is a ChromeProcessController.
  MOZ_ASSERT(XRE_IsGPUProcess());

  // Note APZBridgeParent is bound to the main thread (See PAPZBridge.ipdl).
  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NewRunnableMethod<uint64_t, SideBits>(
        "layers::RemoteContentController::NotifyTouchSwipableDirections", this,
        &RemoteContentController::NotifyTouchSwipableDirections, aInputBlockId,
        aSwipableDirections));
    return;
  }

  auto bridge = CompositorBridgeParent::GetApzBridgeParentForRoot(mLayersId);
  if (bridge) {
    (void)bridge->SendNotifyTouchSwipableDirections(aInputBlockId,
                                                    aSwipableDirections);
  }
}

void RemoteContentController::NotifyMozMouseScrollEvent(
    const ScrollableLayerGuid::ViewID& aScrollId, const nsString& aEvent) {
  if (!mCompositorThread->IsOnCurrentThread()) {
    // We have to send messages from the compositor thread
    mCompositorThread->Dispatch(
        NewRunnableMethod<ScrollableLayerGuid::ViewID, nsString>(
            "layers::RemoteContentController::NotifyMozMouseScrollEvent", this,
            &RemoteContentController::NotifyMozMouseScrollEvent, aScrollId,
            aEvent));
    return;
  }

  if (mCanSend) {
    (void)SendNotifyMozMouseScrollEvent(aScrollId, aEvent);
  }
}

void RemoteContentController::NotifyFlushComplete() {
  MOZ_RELEASE_ASSERT(IsRepaintThread());

  if (mCanSend) {
    (void)SendNotifyFlushComplete();
  }
}

void RemoteContentController::NotifyAsyncScrollbarDragInitiated(
    uint64_t aDragBlockId, const ScrollableLayerGuid::ViewID& aScrollId,
    ScrollDirection aDirection) {
  if (!mCompositorThread->IsOnCurrentThread()) {
    // We have to send messages from the compositor thread
    mCompositorThread->Dispatch(
        NewRunnableMethod<uint64_t, ScrollableLayerGuid::ViewID,
                          ScrollDirection>(
            "layers::RemoteContentController::"
            "NotifyAsyncScrollbarDragInitiated",
            this, &RemoteContentController::NotifyAsyncScrollbarDragInitiated,
            aDragBlockId, aScrollId, aDirection));
    return;
  }

  if (mCanSend) {
    (void)SendNotifyAsyncScrollbarDragInitiated(aDragBlockId, aScrollId,
                                                aDirection);
  }
}

void RemoteContentController::NotifyAsyncScrollbarDragRejected(
    const ScrollableLayerGuid::ViewID& aScrollId) {
  if (!mCompositorThread->IsOnCurrentThread()) {
    // We have to send messages from the compositor thread
    mCompositorThread->Dispatch(NewRunnableMethod<ScrollableLayerGuid::ViewID>(
        "layers::RemoteContentController::NotifyAsyncScrollbarDragRejected",
        this, &RemoteContentController::NotifyAsyncScrollbarDragRejected,
        aScrollId));
    return;
  }

  if (mCanSend) {
    (void)SendNotifyAsyncScrollbarDragRejected(aScrollId);
  }
}

void RemoteContentController::NotifyAsyncAutoscrollRejected(
    const ScrollableLayerGuid::ViewID& aScrollId) {
  if (!mCompositorThread->IsOnCurrentThread()) {
    // We have to send messages from the compositor thread
    mCompositorThread->Dispatch(NewRunnableMethod<ScrollableLayerGuid::ViewID>(
        "layers::RemoteContentController::NotifyAsyncAutoscrollRejected", this,
        &RemoteContentController::NotifyAsyncAutoscrollRejected, aScrollId));
    return;
  }

  if (mCanSend) {
    (void)SendNotifyAsyncAutoscrollRejected(aScrollId);
  }
}

void RemoteContentController::CancelAutoscroll(
    const ScrollableLayerGuid& aGuid) {
  if (XRE_GetProcessType() == GeckoProcessType_GPU) {
    CancelAutoscrollCrossProcess(aGuid);
  } else {
    CancelAutoscrollInProcess(aGuid);
  }
}

void RemoteContentController::CancelAutoscrollInProcess(
    const ScrollableLayerGuid& aGuid) {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
  NS_DispatchToMainThread(NewRunnableFunction(
      "layers::CancelAutoScroll", &APZCCallbackHelper::CancelAutoscroll,
      aGuid.mScrollId));
}

// Note APZBridgeParent is bound to the main thread (See
// PAPZBridge.ipdl).
void RemoteContentController::CancelAutoscrollCrossProcess(
    const ScrollableLayerGuid& aGuid) {
  MOZ_RELEASE_ASSERT(XRE_IsGPUProcess());

  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NewRunnableMethod<ScrollableLayerGuid>(
        "layers::RemoteContentController::CancelAutoscrollCrossProcess", this,
        &RemoteContentController::CancelAutoscrollCrossProcess, aGuid));
    return;
  }

  auto apzib =
      CompositorBridgeParent::GetApzBridgeParentForRoot(aGuid.mLayersId);
  if (apzib) {
    (void)apzib->SendCancelAutoscroll(aGuid.mScrollId);
  }
}

void RemoteContentController::NotifyScaleGestureComplete(
    const ScrollableLayerGuid& aGuid, float aScale) {
  if (XRE_GetProcessType() == GeckoProcessType_GPU) {
    NotifyScaleGestureCompleteCrossProcess(aGuid, aScale);
  } else {
    NotifyScaleGestureCompleteInProcess(aGuid, aScale);
  }
}

void RemoteContentController::NotifyScaleGestureCompleteInProcess(
    const ScrollableLayerGuid& aGuid, float aScale) {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());

  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NewRunnableMethod<ScrollableLayerGuid, float>(
        "layers::RemoteContentController::"
        "NotifyScaleGestureCompleteInProcess",
        this, &RemoteContentController::NotifyScaleGestureCompleteInProcess,
        aGuid, aScale));
    return;
  }

  RefPtr<GeckoContentController> rootController =
      CompositorBridgeParent::GetGeckoContentControllerForRoot(aGuid.mLayersId);
  if (rootController) {
    MOZ_ASSERT(rootController != this);
    if (rootController != this) {
      rootController->NotifyScaleGestureComplete(aGuid, aScale);
    }
  }
}

// Note APZBridgeParent is bound to the main thread (See
// PAPZBridge.ipdl).
void RemoteContentController::NotifyScaleGestureCompleteCrossProcess(
    const ScrollableLayerGuid& aGuid, float aScale) {
  MOZ_RELEASE_ASSERT(XRE_IsGPUProcess());

  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NewRunnableMethod<ScrollableLayerGuid, float>(
        "layers::RemoteContentController::"
        "NotifyScaleGestureCompleteCrossProcess",
        this, &RemoteContentController::NotifyScaleGestureCompleteCrossProcess,
        aGuid, aScale));
    return;
  }

  auto apzib =
      CompositorBridgeParent::GetApzBridgeParentForRoot(aGuid.mLayersId);
  if (apzib) {
    (void)apzib->SendNotifyScaleGestureComplete(aGuid.mScrollId, aScale);
  }
}

void RemoteContentController::ActorDestroy(ActorDestroyReason aWhy) {
  // This controller could possibly be kept alive longer after this
  // by a RefPtr, but it is no longer valid to send messages.
  mCanSend = false;
}

void RemoteContentController::Destroy() {
  if (mCanSend) {
    mCanSend = false;
    (void)SendDestroy();
  }
}

mozilla::ipc::IPCResult RemoteContentController::RecvDestroy() {
  // The actor on the other side is about to get destroyed, so let's not
  // send it any more messages.
  mCanSend = false;
  return IPC_OK();
}

bool RemoteContentController::IsRemote() { return true; }

}  // namespace layers
}  // namespace mozilla
