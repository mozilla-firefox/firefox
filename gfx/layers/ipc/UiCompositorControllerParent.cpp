/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "UiCompositorControllerParent.h"

#if defined(MOZ_WIDGET_ANDROID)
#  include "apz/src/APZCTreeManager.h"
#  include "mozilla/layers/AndroidHardwareBuffer.h"
#  include "mozilla/widget/AndroidCompositorWidget.h"
#endif
#include <utility>

#include "FrameMetrics.h"
#include "SynchronousTask.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/layers/Compositor.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/UiCompositorControllerMessageTypes.h"
#include "mozilla/layers/WebRenderBridgeParent.h"

namespace mozilla {
namespace layers {

typedef CompositorBridgeParent::LayerTreeState LayerTreeState;

/* static */
RefPtr<UiCompositorControllerParent>
UiCompositorControllerParent::GetFromRootLayerTreeId(
    const LayersId& aRootLayerTreeId) {
  RefPtr<UiCompositorControllerParent> controller;
  CompositorBridgeParent::CallWithLayerTreeState(
      aRootLayerTreeId, [&](LayerTreeState& aState) -> void {
        controller = aState.mUiControllerParent;
      });
  return controller;
}

/* static */
RefPtr<UiCompositorControllerParent> UiCompositorControllerParent::Start(
    const LayersId& aRootLayerTreeId,
    Endpoint<PUiCompositorControllerParent>&& aEndpoint) {
  RefPtr<UiCompositorControllerParent> parent =
      new UiCompositorControllerParent(aRootLayerTreeId);

  RefPtr<Runnable> task =
      NewRunnableMethod<Endpoint<PUiCompositorControllerParent>&&>(
          "layers::UiCompositorControllerParent::Open", parent,
          &UiCompositorControllerParent::Open, std::move(aEndpoint));
  CompositorThread()->Dispatch(task.forget());

  return parent;
}

mozilla::ipc::IPCResult UiCompositorControllerParent::RecvPause() {
  CompositorBridgeParent* parent =
      CompositorBridgeParent::GetCompositorBridgeParentFromLayersId(
          mRootLayerTreeId);
  if (parent) {
    parent->PauseComposition();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult UiCompositorControllerParent::RecvResume(
    bool* aOutResumed) {
  *aOutResumed = false;
  CompositorBridgeParent* parent =
      CompositorBridgeParent::GetCompositorBridgeParentFromLayersId(
          mRootLayerTreeId);
  if (parent) {
    *aOutResumed = parent->ResumeComposition();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult UiCompositorControllerParent::RecvResumeAndResize(
    const int32_t& aX, const int32_t& aY, const int32_t& aWidth,
    const int32_t& aHeight, bool* aOutResumed) {
  *aOutResumed = false;
  CompositorBridgeParent* parent =
      CompositorBridgeParent::GetCompositorBridgeParentFromLayersId(
          mRootLayerTreeId);
  if (parent) {
    // Front-end expects a first paint callback upon resume/resize.
    parent->ForceIsFirstPaint();
#if defined(MOZ_WIDGET_ANDROID)
    parent->GetWidget()->AsAndroid()->NotifyClientSizeChanged(
        LayoutDeviceIntSize(aWidth, aHeight));
#endif
    *aOutResumed = parent->ResumeCompositionAndResize(aX, aY, aWidth, aHeight);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult
UiCompositorControllerParent::RecvInvalidateAndRender() {
  CompositorBridgeParent* parent =
      CompositorBridgeParent::GetCompositorBridgeParentFromLayersId(
          mRootLayerTreeId);
  if (parent) {
    parent->ScheduleComposition(wr::RenderReasons::OTHER);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult UiCompositorControllerParent::RecvMaxToolbarHeight(
    const int32_t& aHeight) {
  mMaxToolbarHeight = aHeight;

  return IPC_OK();
}

mozilla::ipc::IPCResult UiCompositorControllerParent::RecvFixedBottomOffset(
    const int32_t& aOffset) {
#if defined(MOZ_WIDGET_ANDROID)
  CompositorBridgeParent* parent =
      CompositorBridgeParent::GetCompositorBridgeParentFromLayersId(
          mRootLayerTreeId);
  if (parent) {
    parent->SetFixedLayerMargins(0, aOffset);
  }
#endif  // defined(MOZ_WIDGET_ANDROID)

  return IPC_OK();
}

mozilla::ipc::IPCResult UiCompositorControllerParent::RecvDefaultClearColor(
    const uint32_t& aColor) {
  LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(mRootLayerTreeId);

  if (state && state->mWrBridge) {
    state->mWrBridge->SetClearColor(gfx::DeviceColor::UnusualFromARGB(aColor));
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult UiCompositorControllerParent::RecvRequestScreenPixels(
    uint64_t aRequestId, gfx::IntRect aSourceRect, gfx::IntSize aDestSize) {
#if defined(MOZ_WIDGET_ANDROID)
  LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(mRootLayerTreeId);

  if (state && state->mWrBridge) {
    state->mWrBridge->RequestScreenPixels(aSourceRect, aDestSize)
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [target = RefPtr{this},
             aRequestId](RefPtr<AndroidHardwareBuffer> aHardwareBuffer) {
              UniqueFileHandle bufferFd =
                  aHardwareBuffer->SerializeToFileDescriptor();
              UniqueFileHandle fenceFd =
                  aHardwareBuffer->GetAndResetAcquireFence();
              target
                  ->SendScreenPixels(
                      aRequestId,
                      aHardwareBuffer
                          ? Some(ipc::FileDescriptor(std::move(bufferFd)))
                          : Nothing(),
                      fenceFd ? Some(ipc::FileDescriptor(std::move(fenceFd)))
                              : Nothing())
                  // Ensure the hardware buffer remains alive until child side
                  // has finished using it.
                  ->Then(GetCurrentSerialEventTarget(), __func__,
                         [aHardwareBuffer](
                             ScreenPixelsPromise::ResolveOrRejectValue&&) {});
            },
            [target = RefPtr{this}, aRequestId](nsresult aError) {
              (void)target->SendScreenPixels(aRequestId, Nothing(), Nothing());
            });
    state->mWrBridge->ScheduleForcedGenerateFrame(wr::RenderReasons::OTHER);
  }
#endif  // defined(MOZ_WIDGET_ANDROID)

  return IPC_OK();
}

mozilla::ipc::IPCResult
UiCompositorControllerParent::RecvEnableLayerUpdateNotifications(
    const bool& aEnable) {
#if defined(MOZ_WIDGET_ANDROID)
  // Layers updates are need by Robocop test which enables them
  mCompositorLayersUpdateEnabled = aEnable;
#endif  // defined(MOZ_WIDGET_ANDROID)

  return IPC_OK();
}

void UiCompositorControllerParent::ActorDestroy(ActorDestroyReason aWhy) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  Shutdown();
}

void UiCompositorControllerParent::ToolbarAnimatorMessageFromCompositor(
    int32_t aMessage) {
  // This function can be call from ether compositor or controller thread.
  if (!CompositorThreadHolder::IsInCompositorThread()) {
    CompositorThread()->Dispatch(NewRunnableMethod<int32_t>(
        "layers::UiCompositorControllerParent::"
        "ToolbarAnimatorMessageFromCompositor",
        this,
        &UiCompositorControllerParent::ToolbarAnimatorMessageFromCompositor,
        aMessage));
    return;
  }

  (void)SendToolbarAnimatorMessageFromCompositor(aMessage);
}

void UiCompositorControllerParent::NotifyLayersUpdated() {
#ifdef MOZ_WIDGET_ANDROID
  if (mCompositorLayersUpdateEnabled) {
    ToolbarAnimatorMessageFromCompositor(LAYERS_UPDATED);
  }
#endif
}

void UiCompositorControllerParent::NotifyFirstPaint() {
  ToolbarAnimatorMessageFromCompositor(FIRST_PAINT);
}

void UiCompositorControllerParent::NotifyCompositorScrollUpdate(
    const CompositorScrollUpdate& aUpdate) {
  CompositorThread()->Dispatch(NewRunnableMethod<CompositorScrollUpdate>(
      "UiCompositorControllerParent::SendNotifyCompositorScrollUpdate", this,
      &UiCompositorControllerParent::SendNotifyCompositorScrollUpdate,
      aUpdate));
}

UiCompositorControllerParent::UiCompositorControllerParent(
    const LayersId& aRootLayerTreeId)
    : mRootLayerTreeId(aRootLayerTreeId)
#ifdef MOZ_WIDGET_ANDROID
      ,
      mCompositorLayersUpdateEnabled(false)
#endif
      ,
      mMaxToolbarHeight(0) {
  MOZ_COUNT_CTOR(UiCompositorControllerParent);
}

UiCompositorControllerParent::~UiCompositorControllerParent() {
  MOZ_COUNT_DTOR(UiCompositorControllerParent);
}

void UiCompositorControllerParent::InitializeForSameProcess() {
  // This function is called by UiCompositorControllerChild in the main thread.
  // So dispatch to the compositor thread to Initialize.
  if (!CompositorThreadHolder::IsInCompositorThread()) {
    SetOtherEndpointProcInfo(ipc::EndpointProcInfo::Current());
    SynchronousTask task(
        "UiCompositorControllerParent::InitializeForSameProcess");

    CompositorThread()->Dispatch(NS_NewRunnableFunction(
        "UiCompositorControllerParent::InitializeForSameProcess", [&]() {
          AutoCompleteTask complete(&task);
          InitializeForSameProcess();
        }));

    task.Wait();
    return;
  }

  Initialize();
}

void UiCompositorControllerParent::InitializeForOutOfProcess() {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  Initialize();
}

void UiCompositorControllerParent::Initialize() {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(mRootLayerTreeId);
  MOZ_ASSERT(state);
  MOZ_ASSERT(state->mParent);
  if (!state || !state->mParent) {
    return;
  }
  state->mUiControllerParent = this;
}

void UiCompositorControllerParent::Open(
    Endpoint<PUiCompositorControllerParent>&& aEndpoint) {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  if (!aEndpoint.Bind(this)) {
    // We can't recover from this.
    MOZ_CRASH("Failed to bind UiCompositorControllerParent to endpoint");
  }
  InitializeForOutOfProcess();
}

void UiCompositorControllerParent::Shutdown() {
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  LayerTreeState* state =
      CompositorBridgeParent::GetLayerTreeState(mRootLayerTreeId);
  if (state) {
    state->mUiControllerParent = nullptr;
  }
}

}  // namespace layers
}  // namespace mozilla
