/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/UiCompositorControllerChild.h"

#include "mozilla/dom/ContentChild.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/SynchronousTask.h"
#include "mozilla/layers/UiCompositorControllerMessageTypes.h"
#include "mozilla/layers/UiCompositorControllerParent.h"
#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/StaticPtr.h"
#include "nsBaseWidget.h"
#include "nsProxyRelease.h"
#include "nsThreadUtils.h"

#if defined(MOZ_WIDGET_ANDROID)
#  include "mozilla/widget/AndroidUiThread.h"

static RefPtr<nsThread> GetUiThread() { return mozilla::GetAndroidUiThread(); }
#else
static RefPtr<nsThread> GetUiThread() {
  MOZ_CRASH("Platform does not support UiCompositorController");
  return nullptr;
}
#endif  // defined(MOZ_WIDGET_ANDROID)

namespace mozilla {
namespace layers {

// public:
/* static */
RefPtr<UiCompositorControllerChild>
UiCompositorControllerChild::CreateForSameProcess(
    const LayersId& aRootLayerTreeId, nsBaseWidget* aWidget) {
  RefPtr<UiCompositorControllerChild> child =
      new UiCompositorControllerChild(0, aWidget);
  child->mParent = new UiCompositorControllerParent(aRootLayerTreeId);
  GetUiThread()->Dispatch(
      NewRunnableMethod(
          "layers::UiCompositorControllerChild::OpenForSameProcess", child,
          &UiCompositorControllerChild::OpenForSameProcess),
      nsIThread::DISPATCH_NORMAL);
  return child;
}

/* static */
RefPtr<UiCompositorControllerChild>
UiCompositorControllerChild::CreateForGPUProcess(
    const uint64_t& aProcessToken,
    Endpoint<PUiCompositorControllerChild>&& aEndpoint, nsBaseWidget* aWidget) {
  RefPtr<UiCompositorControllerChild> child =
      new UiCompositorControllerChild(aProcessToken, aWidget);

  RefPtr<nsIRunnable> task =
      NewRunnableMethod<Endpoint<PUiCompositorControllerChild>&&>(
          "layers::UiCompositorControllerChild::OpenForGPUProcess", child,
          &UiCompositorControllerChild::OpenForGPUProcess,
          std::move(aEndpoint));

  GetUiThread()->Dispatch(task.forget(), nsIThread::DISPATCH_NORMAL);
  return child;
}

bool UiCompositorControllerChild::Pause() {
  if (!mIsOpen) {
    return false;
  }
  return SendPause();
}

bool UiCompositorControllerChild::Resume() {
  if (!mIsOpen) {
    return false;
  }
  bool resumed = false;
  return SendResume(&resumed) && resumed;
}

bool UiCompositorControllerChild::ResumeAndResize(const int32_t& aX,
                                                  const int32_t& aY,
                                                  const int32_t& aWidth,
                                                  const int32_t& aHeight) {
  if (!mIsOpen) {
    mResize = Some(gfx::IntRect(aX, aY, aWidth, aHeight));
    // Since we are caching these values, pretend the call succeeded.
    return true;
  }
  bool resumed = false;
  return SendResumeAndResize(aX, aY, aWidth, aHeight, &resumed) && resumed;
}

bool UiCompositorControllerChild::InvalidateAndRender() {
  if (!mIsOpen) {
    return false;
  }
  return SendInvalidateAndRender();
}

bool UiCompositorControllerChild::SetMaxToolbarHeight(const int32_t& aHeight) {
  if (!mIsOpen) {
    mMaxToolbarHeight = Some(aHeight);
    // Since we are caching this value, pretend the call succeeded.
    return true;
  }
  return SendMaxToolbarHeight(aHeight);
}

bool UiCompositorControllerChild::SetFixedBottomOffset(int32_t aOffset) {
  return SendFixedBottomOffset(aOffset);
}

bool UiCompositorControllerChild::ToolbarAnimatorMessageFromUI(
    const int32_t& aMessage) {
  if (!mIsOpen) {
    return false;
  }

  if (aMessage == IS_COMPOSITOR_CONTROLLER_OPEN) {
    RecvToolbarAnimatorMessageFromCompositor(COMPOSITOR_CONTROLLER_OPEN);
  }

  return true;
}

bool UiCompositorControllerChild::SetDefaultClearColor(const uint32_t& aColor) {
  if (!mIsOpen) {
    mDefaultClearColor = Some(aColor);
    // Since we are caching this value, pretend the call succeeded.
    return true;
  }

  return SendDefaultClearColor(aColor);
}

bool UiCompositorControllerChild::RequestScreenPixels() {
  if (!mIsOpen) {
    return false;
  }

  return SendRequestScreenPixels();
}

bool UiCompositorControllerChild::EnableLayerUpdateNotifications(
    const bool& aEnable) {
  if (!mIsOpen) {
    mLayerUpdateEnabled = Some(aEnable);
    // Since we are caching this value, pretend the call succeeded.
    return true;
  }

  return SendEnableLayerUpdateNotifications(aEnable);
}

void UiCompositorControllerChild::Destroy() {
  MOZ_ASSERT(NS_IsMainThread());

  layers::SynchronousTask task("UiCompositorControllerChild::Destroy");
  GetUiThread()->Dispatch(NS_NewRunnableFunction(
      "layers::UiCompositorControllerChild::Destroy", [&]() {
        MOZ_ASSERT(GetUiThread()->IsOnCurrentThread());
        AutoCompleteTask complete(&task);

        // Clear the process token so that we don't notify the GPUProcessManager
        // about an abnormal shutdown, thereby tearing down the GPU process.
        mProcessToken = 0;

        if (mWidget) {
          // Dispatch mWidget to main thread to prevent it from being destructed
          // by the ui thread.
          RefPtr<nsIWidget> widget = std::move(mWidget);
          NS_ReleaseOnMainThread("UiCompositorControllerChild::mWidget",
                                 widget.forget());
        }

        if (mIsOpen) {
          // Close the underlying IPC channel.
          PUiCompositorControllerChild::Close();
          mIsOpen = false;
        }
      }));

  task.Wait();
}

bool UiCompositorControllerChild::DeallocPixelBuffer(Shmem& aMem) {
  return DeallocShmem(aMem);
}

// protected:
void UiCompositorControllerChild::ActorDestroy(ActorDestroyReason aWhy) {
  mIsOpen = false;
  mParent = nullptr;

  if (mProcessToken) {
    gfx::GPUProcessManager::Get()->NotifyRemoteActorDestroyed(mProcessToken);
    mProcessToken = 0;
  }
}

void UiCompositorControllerChild::ProcessingError(Result aCode,
                                                  const char* aReason) {
  if (aCode != MsgDropped) {
    gfxDevCrash(gfx::LogReason::ProcessingError)
        << "Processing error in UiCompositorControllerChild: " << int(aCode);
  }
}

void UiCompositorControllerChild::HandleFatalError(const char* aMsg) {
  dom::ContentChild::FatalErrorIfNotUsingGPUProcess(aMsg, OtherChildID());
}

mozilla::ipc::IPCResult
UiCompositorControllerChild::RecvToolbarAnimatorMessageFromCompositor(
    const int32_t& aMessage) {
#if defined(MOZ_WIDGET_ANDROID)
  if (mWidget) {
    mWidget->RecvToolbarAnimatorMessageFromCompositor(aMessage);
  }
#endif  // defined(MOZ_WIDGET_ANDROID)

  return IPC_OK();
}

mozilla::ipc::IPCResult
UiCompositorControllerChild::RecvNotifyCompositorScrollUpdate(
    const CompositorScrollUpdate& aUpdate) {
  if (mWidget) {
    mWidget->NotifyCompositorScrollUpdate(aUpdate);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult UiCompositorControllerChild::RecvScreenPixels(
    ipc::Shmem&& aMem, const ScreenIntSize& aSize, bool aNeedsYFlip) {
#if defined(MOZ_WIDGET_ANDROID)
  if (mWidget) {
    mWidget->RecvScreenPixels(std::move(aMem), aSize, aNeedsYFlip);
  }
#endif  // defined(MOZ_WIDGET_ANDROID)

  return IPC_OK();
}

// private:
UiCompositorControllerChild::UiCompositorControllerChild(
    const uint64_t& aProcessToken, nsBaseWidget* aWidget)
    : mIsOpen(false), mProcessToken(aProcessToken), mWidget(aWidget) {}

UiCompositorControllerChild::~UiCompositorControllerChild() = default;

void UiCompositorControllerChild::OpenForSameProcess() {
  MOZ_ASSERT(GetUiThread()->IsOnCurrentThread());

  mIsOpen = Open(mParent, mozilla::layers::CompositorThread(),
                 mozilla::ipc::ChildSide);

  if (!mIsOpen) {
    mParent = nullptr;
    return;
  }

  mParent->InitializeForSameProcess();
  SendCachedValues();
  // Let Ui thread know the connection is open;
  RecvToolbarAnimatorMessageFromCompositor(COMPOSITOR_CONTROLLER_OPEN);
}

void UiCompositorControllerChild::OpenForGPUProcess(
    Endpoint<PUiCompositorControllerChild>&& aEndpoint) {
  MOZ_ASSERT(GetUiThread()->IsOnCurrentThread());

  mIsOpen = aEndpoint.Bind(this);

  if (!mIsOpen) {
    // The GPU Process Manager might be gone if we receive ActorDestroy very
    // late in shutdown.
    if (gfx::GPUProcessManager* gpm = gfx::GPUProcessManager::Get()) {
      gpm->NotifyRemoteActorDestroyed(mProcessToken);
    }
    return;
  }

  SetReplyTimeout();

  SendCachedValues();
  // Let Ui thread know the connection is open;
  RecvToolbarAnimatorMessageFromCompositor(COMPOSITOR_CONTROLLER_OPEN);
}

void UiCompositorControllerChild::SendCachedValues() {
  MOZ_ASSERT(mIsOpen);
  if (mResize) {
    bool resumed;
    SendResumeAndResize(mResize.ref().x, mResize.ref().y, mResize.ref().width,
                        mResize.ref().height, &resumed);
    mResize.reset();
  }
  if (mMaxToolbarHeight) {
    SendMaxToolbarHeight(mMaxToolbarHeight.ref());
    mMaxToolbarHeight.reset();
  }
  if (mDefaultClearColor) {
    SendDefaultClearColor(mDefaultClearColor.ref());
    mDefaultClearColor.reset();
  }
  if (mLayerUpdateEnabled) {
    SendEnableLayerUpdateNotifications(mLayerUpdateEnabled.ref());
    mLayerUpdateEnabled.reset();
  }
}

#ifdef MOZ_WIDGET_ANDROID
void UiCompositorControllerChild::SetCompositorSurfaceManager(
    java::CompositorSurfaceManager::Param aCompositorSurfaceManager) {
  MOZ_ASSERT(!mCompositorSurfaceManager,
             "SetCompositorSurfaceManager must only be called once.");
  MOZ_ASSERT(mProcessToken != 0,
             "SetCompositorSurfaceManager must only be called for GPU process "
             "controllers.");
  mCompositorSurfaceManager = aCompositorSurfaceManager;
};

void UiCompositorControllerChild::OnCompositorSurfaceChanged(
    int32_t aWidgetId, java::sdk::Surface::Param aSurface) {
  // If mCompositorSurfaceManager is not set then there is no GPU process and
  // we do not need to do anything.
  if (mCompositorSurfaceManager == nullptr) {
    return;
  }

  nsresult result =
      mCompositorSurfaceManager->OnSurfaceChanged(aWidgetId, aSurface);

  // If our remote binder has died then notify the GPU process manager.
  if (NS_FAILED(result)) {
    if (mProcessToken) {
      gfx::GPUProcessManager::Get()->NotifyRemoteActorDestroyed(mProcessToken);
      mProcessToken = 0;
    }
  }
}
#endif

void UiCompositorControllerChild::SetReplyTimeout() {
#ifndef DEBUG
  // Add a timeout for release builds to kill GPU process when it hangs.
  const int32_t timeout =
      StaticPrefs::layers_gpu_process_ipc_reply_timeout_ms_AtStartup();
  SetReplyTimeoutMs(timeout);
#endif
}

bool UiCompositorControllerChild::ShouldContinueFromReplyTimeout() {
  gfxCriticalNote << "Killing GPU process due to IPC reply timeout";
  gfx::GPUProcessManager::Get()->KillProcess(/* aGenerateMinidump */ true);
  return false;
}

}  // namespace layers
}  // namespace mozilla
