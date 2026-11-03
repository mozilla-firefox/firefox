/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "VRService.h"

#include <cstring>  // for memcmp

#include "../VRShMem.h"
#include "../gfxVRMutex.h"
#include "PuppetSession.h"
#include "mozilla/BackgroundHangMonitor.h"
#include "mozilla/StaticPrefs_dom.h"
#include "nsThread.h"
#include "nsXULAppAPI.h"

#if defined(XP_WIN)
#  include "OculusSession.h"
#endif

#if defined(XP_WIN) || defined(XP_MACOSX) || \
    (defined(XP_LINUX) && !defined(MOZ_WIDGET_ANDROID))
#  include "OpenVRSession.h"
#endif
#if !defined(MOZ_WIDGET_ANDROID)
#  include "OSVRSession.h"
#endif

using namespace mozilla;
using namespace mozilla::gfx;

namespace {

int64_t FrameIDFromBrowserState(const mozilla::gfx::VRBrowserState& aState) {
  for (const auto& layer : aState.layerState) {
    if (layer.type == VRLayerType::LayerType_Stereo_Immersive) {
      return layer.layer_stereo_immersive.frameId;
    }
  }
  return 0;
}

bool IsImmersiveContentActive(const mozilla::gfx::VRBrowserState& aState) {
  for (const auto& layer : aState.layerState) {
    if (layer.type == VRLayerType::LayerType_Stereo_Immersive) {
      return true;
    }
  }
  return false;
}

}  // anonymous namespace

/*static*/
already_AddRefed<VRService> VRService::Create(
    volatile VRExternalShmem* aShmem) {
  RefPtr<VRService> service = new VRService(aShmem);
  return service.forget();
}

VRService::VRService(volatile VRExternalShmem* aShmem)
    : mSystemState{},
      mBrowserState{},
      mShutdownRequested(false),
      mLastHapticState{},
      mFrameStartTime{} {
  // When we have the VR process, we map the memory
  // of mAPIShmem from GPU process and pass it to the CTOR.
  // If we don't have the VR process, we will instantiate
  // mAPIShmem in VRService.
  mShmem = new VRShMem(aShmem, aShmem == nullptr /*aRequiresMutex*/);
}

VRService::~VRService() {
  // PSA: We must store the value of any staticPrefs preferences as this
  // destructor will be called after staticPrefs has been shut down.
  StopInternal(true /*aFromDtor*/);
}

void VRService::Refresh() {
  if (mShmem != nullptr && mShmem->IsDisplayStateShutdown()) {
    Stop();
  }
}

void VRService::Start() {
  if (!mServiceThread) {
    /**
     * We must ensure that any time the service is re-started, that
     * the VRSystemState is reset, including mSystemState.enumerationCompleted
     * This must happen before VRService::Start returns to the caller, in order
     * to prevent the WebVR/WebXR promises from being resolved before the
     * enumeration has been completed.
     */
    memset(&mSystemState, 0, sizeof(mSystemState));
    PushState(mSystemState);
    RefPtr<VRService> self = this;
    nsCOMPtr<nsIThread> thread;
    nsresult rv = NS_NewNamedThread(
        "VRService", getter_AddRefs(thread),
        NS_NewRunnableFunction("VRService::ServiceThreadStartup", [self]() {
          self->mBackgroundHangMonitor =
              std::make_unique<mozilla::BackgroundHangMonitor>(
                  "VRService",
                  /* Timeout values are powers-of-two to enable us get better
                     data. 128ms is chosen for transient hangs because 8Hz
                     should be the minimally acceptable goal for Compositor
                     responsiveness (normal goal is 60Hz). */
                  128,
                  /* 2048ms is chosen for permanent hangs because it's longer
                   * than most Compositor hangs seen in the wild, but is short
                   * enough to not miss getting native hang stacks. */
                  2048);
          static_cast<nsThread*>(NS_GetCurrentThread())
              ->SetUseHangMonitor(true);
        }));

    if (NS_FAILED(rv)) {
      return;
    }
    thread.swap(mServiceThread);
    // ServiceInitialize needs mServiceThread to be set in order to be able to
    // assert that it's running on the right thread as well as dispatching new
    // tasks. It can't be run within the NS_NewRunnableFunction initial event.
    MOZ_ALWAYS_SUCCEEDS(mServiceThread->Dispatch(
        NewRunnableMethod("gfx::VRService::ServiceInitialize", this,
                          &VRService::ServiceInitialize)));
  }
}

void VRService::Stop() { StopInternal(false /*aFromDtor*/); }

void VRService::StopInternal(bool aFromDtor) {
  if (mServiceThread) {
    // We must disable the background hang monitor before we can shutdown this
    // thread. Dispatched a last task to do so. No task will be allowed to run
    // on the service thread after this one.
    mServiceThread->Dispatch(NS_NewRunnableFunction(
        "VRService::StopInternal", [self = RefPtr<VRService>(this), this] {
          static_cast<nsThread*>(NS_GetCurrentThread())
              ->SetUseHangMonitor(false);
          mBackgroundHangMonitor = nullptr;
        }));
    mShutdownRequested = true;
    mServiceThread->Shutdown();
    mServiceThread = nullptr;
  }

  if (mShmem != nullptr && (aFromDtor || !mShmem->IsSharedExternalShmem())) {
    // Only leave the VRShMem and clean up the pointer when the struct
    // was not passed in. Otherwise, VRService will no longer have a
    // way to access that struct if VRService starts again.
    mShmem->LeaveShMem();
    delete mShmem;
    mShmem = nullptr;
  }

  mSession = nullptr;
}

bool VRService::InitShmem() { return mShmem->JoinShMem(); }

bool VRService::IsInServiceThread() {
  return mServiceThread && mServiceThread->IsOnCurrentThread();
}

void VRService::ServiceInitialize() {
  MOZ_RELEASE_ASSERT(IsInServiceThread());

  if (!InitShmem()) {
    return;
  }

  mShutdownRequested = false;
  // Get initial state from the browser
  PullState(mBrowserState);

  // Try to start a VRSession
  UniquePtr<VRSession> session;

  if (StaticPrefs::dom_vr_puppet_enabled()) {
    // When the VR Puppet is enabled, we don't want
    // to enumerate any real devices
    session = std::make_unique<PuppetSession>();
    if (!session->Initialize(mSystemState, mBrowserState.detectRuntimesOnly)) {
      session = nullptr;
    }
  } else {
    // We try Oculus first to ensure we use Oculus
    // devices trough the most native interface
    // when possible.
#if defined(XP_WIN)
    // Try Oculus
    if (!session) {
      session = std::make_unique<OculusSession>();
      if (!session->Initialize(mSystemState,
                               mBrowserState.detectRuntimesOnly)) {
        session = nullptr;
      }
    }
#endif

#if defined(XP_WIN) || defined(XP_MACOSX) || \
    (defined(XP_LINUX) && !defined(MOZ_WIDGET_ANDROID))
    // Try OpenVR
    if (!session) {
      session = std::make_unique<OpenVRSession>();
      if (!session->Initialize(mSystemState,
                               mBrowserState.detectRuntimesOnly)) {
        session = nullptr;
      }
    }
#endif
#if !defined(MOZ_WIDGET_ANDROID)
    // Try OSVR
    if (!session) {
      session = std::make_unique<OSVRSession>();
      if (!session->Initialize(mSystemState,
                               mBrowserState.detectRuntimesOnly)) {
        session = nullptr;
      }
    }
#endif

  }  // if (staticPrefs:VRPuppetEnabled())

  if (session) {
    mSession = std::move(session);
    // Setting enumerationCompleted to true indicates to the browser
    // that it should resolve any promises in the WebVR/WebXR API
    // waiting for hardware detection.
    mSystemState.enumerationCompleted = true;
    PushState(mSystemState);

    mServiceThread->Dispatch(
        NewRunnableMethod("gfx::VRService::ServiceWaitForImmersive", this,
                          &VRService::ServiceWaitForImmersive));
  } else {
    // VR hardware was not detected.
    // We must inform the browser of the failure so it may try again
    // later and resolve WebVR promises.  A failure or shutdown is
    // indicated by enumerationCompleted being set to true, with all
    // other fields remaining zeroed out.
    VRDisplayCapabilityFlags capFlags =
        mSystemState.displayState.capabilityFlags;
    memset(&mSystemState, 0, sizeof(mSystemState));
    mSystemState.enumerationCompleted = true;

    if (mBrowserState.detectRuntimesOnly) {
      mSystemState.displayState.capabilityFlags = capFlags;
    } else {
      mSystemState.displayState.minRestartInterval =
          StaticPrefs::dom_vr_external_notdetected_timeout();
    }
    mSystemState.displayState.shutdown = true;
    PushState(mSystemState);
  }
}

void VRService::ServiceShutdown() {
  MOZ_RELEASE_ASSERT(IsInServiceThread());

  // Notify the browser that we have shut down.
  // This is indicated by enumerationCompleted being set
  // to true, with all other fields remaining zeroed out.
  memset(&mSystemState, 0, sizeof(mSystemState));
  mSystemState.enumerationCompleted = true;
  mSystemState.displayState.shutdown = true;
  if (mSession && mSession->ShouldQuit()) {
    mSystemState.displayState.minRestartInterval =
        StaticPrefs::dom_vr_external_quit_timeout();
  }
  PushState(mSystemState);
  mSession = nullptr;
}

void VRService::ServiceWaitForImmersive() {
  MOZ_RELEASE_ASSERT(IsInServiceThread());
  MOZ_RELEASE_ASSERT(mSession);

  mSession->ProcessEvents(mSystemState);
  PushState(mSystemState);
  PullState(mBrowserState);

  if (mSession->ShouldQuit() || mShutdownRequested) {
    // Shut down
    mServiceThread->Dispatch(NewRunnableMethod(
        "gfx::VRService::ServiceShutdown", this, &VRService::ServiceShutdown));
  } else if (IsImmersiveContentActive(mBrowserState)) {
    // Enter Immersive Mode
    mSession->StartPresentation();
    mSession->StartFrame(mSystemState);
    PushState(mSystemState);

    mServiceThread->Dispatch(
        NewRunnableMethod("gfx::VRService::ServiceImmersiveMode", this,
                          &VRService::ServiceImmersiveMode));
  } else {
    // Continue waiting for immersive mode
    mServiceThread->Dispatch(
        NewRunnableMethod("gfx::VRService::ServiceWaitForImmersive", this,
                          &VRService::ServiceWaitForImmersive));
  }
}

void VRService::ServiceImmersiveMode() {
  MOZ_RELEASE_ASSERT(IsInServiceThread());
  MOZ_RELEASE_ASSERT(mSession);

  mSession->ProcessEvents(mSystemState);
  UpdateHaptics();
  PushState(mSystemState);
  PullState(mBrowserState);

  if (mSession->ShouldQuit() || mShutdownRequested) {
    // Shut down
    mServiceThread->Dispatch(NewRunnableMethod(
        "gfx::VRService::ServiceShutdown", this, &VRService::ServiceShutdown));
    return;
  }

  if (!IsImmersiveContentActive(mBrowserState)) {
    // Exit immersive mode
    mSession->StopAllHaptics();
    mSession->StopPresentation();
    mServiceThread->Dispatch(
        NewRunnableMethod("gfx::VRService::ServiceWaitForImmersive", this,
                          &VRService::ServiceWaitForImmersive));
    return;
  }

  uint64_t newFrameId = FrameIDFromBrowserState(mBrowserState);
  if (newFrameId != mSystemState.displayState.lastSubmittedFrameId) {
    // A new immersive frame has been received.
    // Submit the textures to the VR system compositor.
    bool success = false;
    for (const auto& layer : mBrowserState.layerState) {
      if (layer.type == VRLayerType::LayerType_Stereo_Immersive) {
        // SubmitFrame may block in order to control the timing for
        // the next frame start
        success = mSession->SubmitFrame(layer.layer_stereo_immersive);
        break;
      }
    }

    // Changing mLastSubmittedFrameId triggers a new frame to start
    // rendering.  Changes to mLastSubmittedFrameId and the values
    // used for rendering, such as headset pose, must be pushed
    // atomically to the browser.
    mSystemState.displayState.lastSubmittedFrameId = newFrameId;
    mSystemState.displayState.lastSubmittedFrameSuccessful = success;

    // StartFrame may block to control the timing for the next frame start
    mSession->StartFrame(mSystemState);
    mSystemState.sensorState.inputFrameID++;
    size_t historyIndex =
        mSystemState.sensorState.inputFrameID % std::size(mFrameStartTime);
    mFrameStartTime[historyIndex] = TimeStamp::Now();
    PushState(mSystemState);
  }

  // Continue immersive mode
  mServiceThread->Dispatch(
      NewRunnableMethod("gfx::VRService::ServiceImmersiveMode", this,
                        &VRService::ServiceImmersiveMode));
}

void VRService::UpdateHaptics() {
  MOZ_RELEASE_ASSERT(IsInServiceThread());
  MOZ_RELEASE_ASSERT(mSession);

  for (size_t i = 0; i < std::size(mBrowserState.hapticState); i++) {
    VRHapticState& state = mBrowserState.hapticState[i];
    VRHapticState& lastState = mLastHapticState[i];
    // Note that VRHapticState is asserted to be a POD type, thus memcmp is safe
    if (memcmp(&state, &lastState, sizeof(VRHapticState)) == 0) {
      // No change since the last update
      continue;
    }
    if (state.inputFrameID == 0) {
      // The haptic feedback was stopped
      mSession->StopVibrateHaptic(state.controllerIndex);
    } else {
      TimeStamp now;
      if (now.IsNull()) {
        // TimeStamp::Now() is expensive, so we
        // must call it only when needed and save the
        // output for further loop iterations.
        now = TimeStamp::Now();
      }
      // This is a new haptic pulse, or we are overriding a prior one
      size_t historyIndex = state.inputFrameID % std::size(mFrameStartTime);
      float startOffset =
          (float)(now - mFrameStartTime[historyIndex]).ToSeconds();

      // state.pulseStart is guaranteed never to be in the future
      mSession->VibrateHaptic(
          state.controllerIndex, state.hapticIndex, state.pulseIntensity,
          state.pulseDuration + state.pulseStart - startOffset);
    }
    // Record the state for comparison in the next run
    memcpy(&lastState, &state, sizeof(VRHapticState));
  }
}

void VRService::PushState(const mozilla::gfx::VRSystemState& aState) {
  if (mShmem != nullptr) {
    mShmem->PushSystemState(aState);
  }
}

void VRService::PullState(mozilla::gfx::VRBrowserState& aState) {
  if (mShmem != nullptr) {
    mShmem->PullBrowserState(aState);
  }
}
