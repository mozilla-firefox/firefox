/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GPUProcessManager.h"

#include "GPUProcessHost.h"
#include "GPUProcessListener.h"
#include "gfxConfig.h"
#include "gfxPlatform.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/MemoryReportingProcess.h"
#include "mozilla/Preferences.h"
#include "mozilla/RDDChild.h"
#include "mozilla/RDDProcessManager.h"
#include "mozilla/RemoteMediaManagerChild.h"
#include "mozilla/RemoteMediaManagerParent.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/gfx/GPUChild.h"
#include "mozilla/gfx/GPUParent.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/glean/GfxMetrics.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/layers/APZBridgeChild.h"
#include "mozilla/layers/APZCTreeManagerChild.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CompositorManagerChild.h"
#include "mozilla/layers/CompositorManagerParent.h"
#include "mozilla/layers/CompositorOptions.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/ImageBridgeParent.h"
#include "mozilla/layers/InProcessCompositorSession.h"
#include "mozilla/layers/LayerTreeLog.h"
#include "mozilla/layers/LayerTreeOwnerTracker.h"
#include "mozilla/layers/RemoteCompositorSession.h"
#include "mozilla/layers/VideoBridgeParent.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/widget/CompositorWidget.h"
#include "mozilla/widget/PlatformWidgetTypes.h"
#include "nsAppRunner.h"
#ifdef MOZ_WIDGET_SUPPORTS_OOP_COMPOSITING
#  include "mozilla/widget/CompositorWidgetChild.h"
#endif
#include "GPUProcessLog.h"
#include "VRManagerChild.h"
#include "VRManagerParent.h"
#include "VsyncBridgeChild.h"
#include "VsyncIOThreadHolder.h"
#include "VsyncSource.h"
#include "nsContentUtils.h"
#include "nsExceptionHandler.h"
#include "nsIWidget.h"
#include "nsPrintfCString.h"

#ifdef MOZ_WMF_MEDIA_ENGINE
#  include "mozilla/ipc/UtilityMediaServiceChild.h"
#endif

#if defined(MOZ_WIDGET_ANDROID)
#  include "mozilla/java/SurfaceControlManagerWrappers.h"
#  include "mozilla/layers/UiCompositorControllerChild.h"
#  include "mozilla/widget/AndroidUiThread.h"
#endif  // defined(MOZ_WIDGET_ANDROID)

#if defined(XP_WIN)
#  include "gfxWindowsPlatform.h"
#  include "mozilla/gfx/DeviceManagerDx.h"
#endif

namespace mozilla {
namespace gfx {

using namespace mozilla::layers;

LazyLogModule gGPUProcessLog("GPUProcess");

static StaticAutoPtr<GPUProcessManager> sSingleton;

GPUProcessManager* GPUProcessManager::Get() { return sSingleton; }

void GPUProcessManager::Initialize() {
  MOZ_ASSERT(XRE_IsParentProcess());
  sSingleton = new GPUProcessManager();
}

void GPUProcessManager::Shutdown() {
  if (!sSingleton) {
    return;
  }
  sSingleton->ShutdownInternal();
  sSingleton = nullptr;
}

GPUProcessManager::GPUProcessManager()
    : mTaskFactory(this),
      mNextNamespace(0),
      mUnstableProcessAttempts(0),
      mTotalProcessAttempts(0),
      mDeviceResetCount(0),
      mAppInForeground(true),
      mProcessToken(0),
      mGPUChild(nullptr) {
  MOZ_COUNT_CTOR(GPUProcessManager);

  mDeviceResetLastTime = TimeStamp::Now();

  LayerTreeOwnerTracker::Initialize();
  CompositorBridgeParent::InitializeStatics();
}

GPUProcessManager::~GPUProcessManager() {
  MOZ_COUNT_DTOR(GPUProcessManager);

  LayerTreeOwnerTracker::Shutdown();

  // The GPU process should have already been shut down.
  MOZ_ASSERT(!mProcess && !mGPUChild);

  // We should have already removed observers.
  MOZ_DIAGNOSTIC_ASSERT(!mObserver);
  MOZ_DIAGNOSTIC_ASSERT(!mBatteryObserver);
}

NS_IMPL_ISUPPORTS(GPUProcessManager::Observer, nsIObserver);

GPUProcessManager::Observer::Observer() {
  nsContentUtils::RegisterShutdownObserver(this);
  Preferences::AddStrongObserver(this, "");
  if (nsCOMPtr<nsIObserverService> obsServ = services::GetObserverService()) {
    obsServ->AddObserver(this, "application-foreground", false);
    obsServ->AddObserver(this, "application-background", false);
    obsServ->AddObserver(this, "screen-information-changed", false);
    obsServ->AddObserver(this, "xpcom-will-shutdown", false);
  }
}

void GPUProcessManager::Observer::Shutdown() {
  nsContentUtils::UnregisterShutdownObserver(this);
  Preferences::RemoveObserver(this, "");
  if (nsCOMPtr<nsIObserverService> obsServ = services::GetObserverService()) {
    obsServ->RemoveObserver(this, "application-foreground");
    obsServ->RemoveObserver(this, "application-background");
    obsServ->RemoveObserver(this, "screen-information-changed");
    obsServ->RemoveObserver(this, "xpcom-will-shutdown");
  }
}

NS_IMETHODIMP
GPUProcessManager::Observer::Observe(nsISupports* aSubject, const char* aTopic,
                                     const char16_t* aData) {
  if (auto* gpm = GPUProcessManager::Get()) {
    gpm->NotifyObserve(aTopic, aData);
  }
  return NS_OK;
}

void GPUProcessManager::NotifyObserve(const char* aTopic,
                                      const char16_t* aData) {
  if (!strcmp(aTopic, NS_XPCOM_WILL_SHUTDOWN_OBSERVER_ID)) {
    StopBatteryObserving();
  } else if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    ShutdownInternal();
  } else if (!strcmp(aTopic, "nsPref:changed")) {
    OnPreferenceChange(aData);
  } else if (!strcmp(aTopic, "application-foreground")) {
    SetAppInForeground(true);
  } else if (!strcmp(aTopic, "application-background")) {
    SetAppInForeground(false);
  } else if (!strcmp(aTopic, "screen-information-changed")) {
    ScreenInformationChanged();
  }
}

GPUProcessManager::BatteryObserver::BatteryObserver() {
  hal::RegisterBatteryObserver(this);
}

void GPUProcessManager::BatteryObserver::Notify(
    const hal::BatteryInformation& aBatteryInfo) {
  if (auto* gpm = GPUProcessManager::Get()) {
    gpm->NotifyBatteryInfo(aBatteryInfo);
  }
}

void GPUProcessManager::BatteryObserver::Shutdown() {
  hal::UnregisterBatteryObserver(this);
}

void GPUProcessManager::OnPreferenceChange(const char16_t* aData) {
  if (!mGPUChild && !IsGPUProcessLaunching()) {
    return;
  }

  // We know prefs are ASCII here.
  NS_LossyConvertUTF16toASCII strData(aData);

  mozilla::dom::Pref pref(strData, /* isLocked */ false,
                          /* isSanitized */ false, Nothing(), Nothing());

  Preferences::GetPreference(&pref, GeckoProcessType_GPU,
                             /* remoteType */ {});
  if (mGPUChild) {
    MOZ_ASSERT(mQueuedPrefs.IsEmpty());
    mGPUChild->SendPreferenceUpdate(pref);
  } else {
    mQueuedPrefs.AppendElement(pref);
  }
}

void GPUProcessManager::ScreenInformationChanged() {
#if defined(XP_WIN)
  if (!!mGPUChild) {
    mGPUChild->SendScreenInformationChanged();
  }
#endif
}

void GPUProcessManager::NotifyBatteryInfo(
    const hal::BatteryInformation& aBatteryInfo) {
  if (mGPUChild) {
    mGPUChild->SendNotifyBatteryInfo(aBatteryInfo);
  }
}

void GPUProcessManager::MaybeCrashIfGpuProcessOnceStable() {
  if (StaticPrefs::layers_gpu_process_allow_fallback_to_parent_AtStartup()) {
    return;
  }
  MOZ_RELEASE_ASSERT(!gfxConfig::IsEnabled(Feature::GPU_PROCESS));
  if (!mProcessStableOnce) {
    return;
  }
  // If the last launch error was suspected to be an OOM failure, let's annotate
  // this as an OOM crash.
  if (mProcess && mProcess->IsLaunchOomError()) {
    CrashReporter::AnnotateOOMAllocationSize(1);
  }
  MOZ_CRASH("Fallback to parent process not allowed!");
}

void GPUProcessManager::ResetProcessStable() {
  mTotalProcessAttempts++;
  mProcessStable = false;
  mProcessAttemptLastTime = TimeStamp::Now();
}

bool GPUProcessManager::IsProcessStable(const TimeStamp& aNow) {
  if (mTotalProcessAttempts > 0) {
    auto delta = (int32_t)(aNow - mProcessAttemptLastTime).ToMilliseconds();
    if (delta < StaticPrefs::layers_gpu_process_stable_min_uptime_ms()) {
      return false;
    }
  }
  return mProcessStable;
}

nsresult GPUProcessManager::LaunchGPUProcess() {
  if (mProcess) {
    return NS_OK;
  }

  if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdown)) {
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  // Start listening for pref changes so we can
  // forward them to the process once it is running.
  if (!mObserver) {
    mObserver = new Observer();
  }

  // Start the Vsync I/O thread so can use it as soon as the process launches.
  EnsureVsyncIOThread();

  mTotalProcessAttempts++;
  mozilla::glean::gpu_process::total_launch_attempts.Set(mTotalProcessAttempts);
  mProcessAttemptLastTime = TimeStamp::Now();
  mProcessStable = false;

  geckoargs::ChildProcessArgs extraArgs;
  ipc::ProcessChild::AddPlatformBuildID(extraArgs);

  GPUPROCESS_LOG("Launching GPU process, attempt {}", mTotalProcessAttempts);

  // The subprocess is launched asynchronously, so we wait for a callback to
  // acquire the IPDL actor.
  mProcess = new GPUProcessHost(this);
  if (!mProcess->Launch(std::move(extraArgs))) {
    DisableGPUProcess("Failed to launch GPU process");
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

bool GPUProcessManager::IsGPUProcessLaunching() {
  MOZ_ASSERT(NS_IsMainThread());
  return !!mProcess && !mGPUChild;
}

void GPUProcessManager::DisableGPUProcess(const char* aMessage) {
  MaybeDisableGPUProcess(aMessage, /* aAllowRestart */ false);
}

bool GPUProcessManager::MaybeDisableGPUProcess(const char* aMessage,
                                               bool aAllowRestart) {
  if (!gfxConfig::IsEnabled(Feature::GPU_PROCESS)) {
    return true;
  }

  bool wantRestart;
  {
    // Collect the gfxVar updates into a single message.
    gfxVarsCollectUpdates collect;

    if (!aAllowRestart) {
      gfxConfig::SetFailed(Feature::GPU_PROCESS, FeatureStatus::Failed,
                           aMessage);
      gfxVars::SetGPUProcessEnabled(false);
    }

    if (mLastError) {
      wantRestart =
          FallbackFromAcceleration(mLastError.value(), mLastErrorMsg.ref());
      mLastError.reset();
      mLastErrorMsg.reset();
    } else {
      wantRestart = gfxPlatform::FallbackFromAcceleration(
          FeatureStatus::Unavailable, aMessage,
          "FEATURE_FAILURE_GPU_PROCESS_ERROR"_ns);
    }
    if (aAllowRestart && wantRestart) {
      // The fallback method can make use of the GPU process.
      return false;
    }

    if (aAllowRestart) {
      gfxConfig::SetFailed(Feature::GPU_PROCESS, FeatureStatus::Failed,
                           aMessage);
      gfxVars::SetGPUProcessEnabled(false);
    }

    MOZ_ASSERT(!gfxConfig::IsEnabled(Feature::GPU_PROCESS));

    gfxCriticalNote << aMessage;
    GPUPROCESS_LOG("Disabling GPU process: {}", aMessage);

    gfxPlatform::DisableGPUProcess();

    MaybeCrashIfGpuProcessOnceStable();
  }

  mozilla::glean::gpu_process::feature_status.Set(
      gfxConfig::GetFeature(Feature::GPU_PROCESS)
          .GetStatusAndFailureIdString());

  mozilla::glean::gpu_process::crash_fallbacks.Get("disabled"_ns).Add(1);

  DestroyProcess();
  ShutdownVsyncIOThread();

  // Now the stability state is based upon the in process compositor session.
  ResetProcessStable();

  // We may have been in the middle of guaranteeing our various services are
  // available when one failed. Some callers may fallback to using the same
  // process equivalent, and we need to make sure those services are setup
  // correctly. We cannot re-enter DisableGPUProcess from this call because we
  // know that it is disabled in the config above.
  if (NS_WARN_IF(NS_FAILED(EnsureGPUReady()))) {
    MOZ_ASSERT(AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdown));
  } else {
    DebugOnly<bool> ready = EnsureProtocolsReady();
    MOZ_ASSERT(ready);
  }

  // If we disable the GPU process during reinitialization after a previous
  // crash, then we need to tell the content processes again, because they
  // need to rebind to the UI process.
  HandleProcessLost();
  return true;
}

bool GPUProcessManager::IsGPUReady() const {
  // If we have disabled the GPU process, then we know we are always ready.
  if (!gfxConfig::IsEnabled(Feature::GPU_PROCESS)) {
    MOZ_ASSERT(!mProcess);
    MOZ_ASSERT(!mGPUChild);
    return true;
  }

  // If we have a GPUChild, then we know the process has finished launching.
  if (mGPUChild) {
    return mGPUChild->IsGPUReady();
  }

  return false;
}

nsresult GPUProcessManager::EnsureGPUReady() {
  MOZ_ASSERT(NS_IsMainThread());

  // Common case is we already have a GPU process.
  if (mProcess && mProcess->IsConnected() && mGPUChild) {
    MOZ_DIAGNOSTIC_ASSERT(mGPUChild->IsGPUReady());
    return NS_OK;
  }

  // Next most common case is that we are compositing in the parent process.
  if (!gfxConfig::IsEnabled(Feature::GPU_PROCESS)) {
    MOZ_DIAGNOSTIC_ASSERT(!mProcess);
    MOZ_DIAGNOSTIC_ASSERT(!mGPUChild);
    return NS_OK;
  }

  // We aren't ready and in shutdown, we should just abort.
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdown)) {
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  while (true) {
    // We allow the GPU process to launch if we are:
    // 1) in the foreground, as the application is being actively used.
    // 2) if we have no launch failures, because even if we are backgrounded, we
    //    can try once to secure it. This is useful for geckoview-junit tests.
    // 3) if our pref is set to allow background launches; this is false by
    //    default on Android due to its issues with keeping the GPU process
    //    alive in the background, and true on all other platforms.
    //
    // If we are not in a position to try launching and/or waiting for the GPU
    // process, then we should just abort for now. The higher levels will fail
    // to create the content process, but all of this should get recreated when
    // the app comes back into the foreground.
    if (!mAppInForeground && mLaunchProcessAttempts > 0 &&
        !StaticPrefs::layers_gpu_process_launch_in_background()) {
      return NS_ERROR_ABORT;
    }

    // Launch the GPU process if it is enabled but hasn't been (re-)launched
    // yet, and wait for it to complete the handshake. As part of WaitForLaunch,
    // we know that OnProcessLaunchComplete has been called. If it succeeds,
    // we know that mGPUChild has been set and we already waited for it to be
    // ready. If it fails, then we know that the GPU process must have been
    // destroyed and/or disabled.
    nsresult rv = LaunchGPUProcess();
    if (NS_SUCCEEDED(rv) && mProcess->WaitForLaunch() && mGPUChild) {
      MOZ_DIAGNOSTIC_ASSERT(mGPUChild->IsGPUReady());
      break;
    }

    MOZ_RELEASE_ASSERT(rv != NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
    MOZ_RELEASE_ASSERT(!mProcess);
    MOZ_RELEASE_ASSERT(!mGPUChild);
    MOZ_DIAGNOSTIC_ASSERT(mLaunchProcessAttempts > 0);

    if (!gfxConfig::IsEnabled(Feature::GPU_PROCESS)) {
      break;
    }
  }

  return NS_OK;
}

bool GPUProcessManager::EnsureProtocolsReady() {
  return EnsureCompositorManagerChild() && EnsureImageBridgeChild() &&
         EnsureVRManager();
}

bool GPUProcessManager::EnsureCompositorManagerChild() {
  MOZ_DIAGNOSTIC_ASSERT(IsGPUReady());

  if (CompositorManagerChild::IsInitialized(mProcessToken)) {
    return true;
  }

  if (!mGPUChild) {
    CompositorManagerChild::InitSameProcess(AllocateNamespace(), mProcessToken);
    return true;
  }

  ipc::Endpoint<PCompositorManagerParent> parentPipe;
  ipc::Endpoint<PCompositorManagerChild> childPipe;
  nsresult rv = PCompositorManager::CreateEndpoints(
      mGPUChild->OtherEndpointProcInfo(), ipc::EndpointProcInfo::Current(),
      &parentPipe, &childPipe);
  if (NS_FAILED(rv)) {
    DisableGPUProcess("Failed to create PCompositorManager endpoints");
    return true;
  }

  uint32_t cmNamespace = AllocateNamespace();
  mGPUChild->SendInitCompositorManager(std::move(parentPipe), cmNamespace);
  CompositorManagerChild::Init(std::move(childPipe), cmNamespace,
                               mProcessToken);
  return true;
}

bool GPUProcessManager::EnsureImageBridgeChild() {
  MOZ_DIAGNOSTIC_ASSERT(IsGPUReady());

  if (ImageBridgeChild::GetSingleton()) {
    return true;
  }

  if (!mGPUChild) {
    ImageBridgeChild::InitSameProcess(AllocateNamespace());
    return true;
  }

  ipc::Endpoint<PImageBridgeParent> parentPipe;
  ipc::Endpoint<PImageBridgeChild> childPipe;
  nsresult rv = PImageBridge::CreateEndpoints(
      mGPUChild->OtherEndpointProcInfo(), ipc::EndpointProcInfo::Current(),
      &parentPipe, &childPipe);
  if (NS_FAILED(rv)) {
    DisableGPUProcess("Failed to create PImageBridge endpoints");
    return true;
  }

  uint32_t ibNamespace = AllocateNamespace();
  mGPUChild->SendInitImageBridge(std::move(parentPipe), ibNamespace);
  ImageBridgeChild::InitWithGPUProcess(std::move(childPipe), ibNamespace);
  return true;
}

bool GPUProcessManager::EnsureVRManager() {
  MOZ_DIAGNOSTIC_ASSERT(IsGPUReady());

  if (VRManagerChild::IsCreated()) {
    return true;
  }

  if (!mGPUChild) {
    VRManagerChild::InitSameProcess(AllocateNamespace());
    return true;
  }

  ipc::Endpoint<PVRManagerParent> parentPipe;
  ipc::Endpoint<PVRManagerChild> childPipe;
  nsresult rv = PVRManager::CreateEndpoints(mGPUChild->OtherEndpointProcInfo(),
                                            ipc::EndpointProcInfo::Current(),
                                            &parentPipe, &childPipe);
  if (NS_FAILED(rv)) {
    DisableGPUProcess("Failed to create PVRManager endpoints");
    return true;
  }

  uint32_t vrNamespace = AllocateNamespace();
  mGPUChild->SendInitVRManager(std::move(parentPipe), vrNamespace);
  VRManagerChild::InitWithGPUProcess(std::move(childPipe), vrNamespace);
  return true;
}

#if defined(MOZ_WIDGET_ANDROID)
RefPtr<UiCompositorControllerChild>
GPUProcessManager::CreateUiCompositorController(nsIWidget* aWidget,
                                                const LayersId aId) {
  MOZ_DIAGNOSTIC_ASSERT(IsGPUReady());

  if (!mGPUChild) {
    return UiCompositorControllerChild::CreateForSameProcess(aId, aWidget);
  }

  ipc::Endpoint<PUiCompositorControllerParent> parentPipe;
  ipc::Endpoint<PUiCompositorControllerChild> childPipe;
  nsresult rv = PUiCompositorController::CreateEndpoints(
      mGPUChild->OtherEndpointProcInfo(), ipc::EndpointProcInfo::Current(),
      &parentPipe, &childPipe);
  if (NS_FAILED(rv)) {
    DisableGPUProcess("Failed to create PUiCompositorController endpoints");
    return nullptr;
  }

  mGPUChild->SendInitUiCompositorController(aId, std::move(parentPipe));
  RefPtr<UiCompositorControllerChild> result =
      UiCompositorControllerChild::CreateForGPUProcess(
          mProcessToken, std::move(childPipe), aWidget);

  if (result) {
    result->SetCompositorSurfaceManager(
        mProcess->GetCompositorSurfaceManager());
  }
  return result;
}
#endif  // defined(MOZ_WIDGET_ANDROID)

void GPUProcessManager::OnProcessLaunchComplete(GPUProcessHost* aHost) {
  MOZ_ASSERT(mProcess && mProcess == aHost);

  // By definition, the process failing to launch is an unstable attempt. While
  // we did not get to the point where we are using the features, we should just
  // follow the same fallback procedure.
  auto* gpuChild = mProcess->GetActor();
  if (NS_WARN_IF(!mProcess->IsConnected()) || NS_WARN_IF(!gpuChild) ||
      NS_WARN_IF(!gpuChild->EnsureGPUReady())) {
    ++mLaunchProcessAttempts;
    GPUPROCESS_LOG("GPU process failed to launch, attempt {}",
                   mLaunchProcessAttempts);
    if (mLaunchProcessAttempts >
        uint32_t(StaticPrefs::layers_gpu_process_max_launch_attempts())) {
      char disableMessage[64];
      SprintfLiteral(disableMessage,
                     "Failed to launch GPU process after %d attempts",
                     mLaunchProcessAttempts);
      DisableGPUProcess(disableMessage);
    } else {
      DestroyProcess(/* aUnexpectedShutdown */ true);
    }
    return;
  }

  mLaunchProcessAttempts = 0;
  mGPUChild = gpuChild;
  mProcessToken = mProcess->GetProcessToken();
  GPUPROCESS_LOG("GPU process {} launched, pid {}", mProcessToken,
                 mGPUChild->OtherPid());
#if defined(XP_WIN)
  if (mAppInForeground) {
    SetProcessIsForeground();
  }
#endif

  // Set a high priority for the newly-created gpu process.
  int pID = mProcess->GetChildProcessId();
  hal::SetProcessPriority(pID, hal::PROCESS_PRIORITY_FOREGROUND_HIGH);

  ipc::Endpoint<PVsyncBridgeParent> vsyncParent;
  ipc::Endpoint<PVsyncBridgeChild> vsyncChild;
  nsresult rv = PVsyncBridge::CreateEndpoints(
      mGPUChild->OtherEndpointProcInfo(), ipc::EndpointProcInfo::Current(),
      &vsyncParent, &vsyncChild);
  if (NS_FAILED(rv)) {
    DisableGPUProcess("Failed to create PVsyncBridge endpoints");
    return;
  }

  mVsyncBridge = VsyncBridgeChild::Create(mVsyncIOThread, mProcessToken,
                                          std::move(vsyncChild));
  mGPUChild->SendInitVsyncBridge(std::move(vsyncParent));

  MOZ_DIAGNOSTIC_ASSERT(!mBatteryObserver);
  if (!AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMWillShutdown)) {
    mBatteryObserver = new BatteryObserver();
  }

  // Flush any pref updates that happened during launch and weren't
  // included in the blobs set up in LaunchGPUProcess.
  for (const mozilla::dom::Pref& pref : mQueuedPrefs) {
    (void)NS_WARN_IF(!mGPUChild->SendPreferenceUpdate(pref));
  }
  mQueuedPrefs.Clear();

  CrashReporter::RecordAnnotationCString(
      CrashReporter::Annotation::GPUProcessStatus, "Running");

  CrashReporter::RecordAnnotationU32(
      CrashReporter::Annotation::GPUProcessLaunchCount, mTotalProcessAttempts);

  ReinitializeRendering();
}

void GPUProcessManager::OnProcessDeclaredStable() { mProcessStable = true; }

static bool ShouldLimitDeviceResets(uint32_t count, int32_t deltaMilliseconds) {
  // We decide to limit by comparing the amount of resets that have happened
  // and time since the last reset to two prefs.
  int32_t timeLimit = StaticPrefs::gfx_device_reset_threshold_ms_AtStartup();
  int32_t countLimit = StaticPrefs::gfx_device_reset_limit_AtStartup();

  bool hasTimeLimit = timeLimit >= 0;
  bool hasCountLimit = countLimit >= 0;

  bool triggeredTime = deltaMilliseconds < timeLimit;
  bool triggeredCount = count > (uint32_t)countLimit;

  // If we have both prefs set then it needs to trigger both limits,
  // otherwise we only test the pref that is set or none
  if (hasTimeLimit && hasCountLimit) {
    return triggeredTime && triggeredCount;
  } else if (hasTimeLimit) {
    return triggeredTime;
  } else if (hasCountLimit) {
    return triggeredCount;
  }

  return false;
}

void GPUProcessManager::ResetCompositors() {
  // Note: this will recreate devices in addition to recreating compositors.
  // This isn't optimal, but this is only used on linux where acceleration
  // isn't enabled by default, and this way we don't need a new code path.
  SimulateDeviceReset();
}

void GPUProcessManager::SimulateDeviceReset() {
  // Make sure we rebuild environment and configuration for accelerated
  // features.
  gfxPlatform::GetPlatform()->CompositorUpdated();

  if (mProcess) {
    if (mGPUChild) {
      mGPUChild->SendSimulateDeviceReset();
    }
  } else {
    wr::RenderThread::Get()->SimulateDeviceReset();
  }
}

bool GPUProcessManager::FallbackFromAcceleration(wr::WebRenderError aError,
                                                 const nsCString& aMsg) {
  if (aError == wr::WebRenderError::INITIALIZE) {
    return gfxPlatform::FallbackFromAcceleration(
        gfx::FeatureStatus::Unavailable, "WebRender initialization failed",
        aMsg);
  } else if (aError == wr::WebRenderError::MAKE_CURRENT) {
    return gfxPlatform::FallbackFromAcceleration(
        gfx::FeatureStatus::Unavailable,
        "Failed to make render context current",
        "FEATURE_FAILURE_WEBRENDER_MAKE_CURRENT"_ns);
  } else if (aError == wr::WebRenderError::RENDER) {
    return gfxPlatform::FallbackFromAcceleration(
        gfx::FeatureStatus::Unavailable, "Failed to render WebRender",
        "FEATURE_FAILURE_WEBRENDER_RENDER"_ns);
  } else if (aError == wr::WebRenderError::NEW_SURFACE) {
    // If we cannot create a new Surface even in the final fallback
    // configuration then force a crash.
    return gfxPlatform::FallbackFromAcceleration(
        gfx::FeatureStatus::Unavailable, "Failed to create new surface",
        "FEATURE_FAILURE_WEBRENDER_NEW_SURFACE"_ns,
        /* aCrashAfterFinalFallback */ true);
  } else if (aError == wr::WebRenderError::BEGIN_DRAW) {
    return gfxPlatform::FallbackFromAcceleration(
        gfx::FeatureStatus::Unavailable, "BeginDraw() failed",
        "FEATURE_FAILURE_WEBRENDER_BEGIN_DRAW"_ns);
  } else if (aError == wr::WebRenderError::EXCESSIVE_RESETS) {
    return gfxPlatform::FallbackFromAcceleration(
        gfx::FeatureStatus::Unavailable, "Device resets exceeded threshold",
        "FEATURE_FAILURE_WEBRENDER_EXCESSIVE_RESETS"_ns);
  } else {
    MOZ_ASSERT_UNREACHABLE("Invalid value");
    return gfxPlatform::FallbackFromAcceleration(
        gfx::FeatureStatus::Unavailable, "Unhandled failure reason",
        "FEATURE_FAILURE_WEBRENDER_UNHANDLED"_ns);
  }
}

void GPUProcessManager::DisableWebRenderConfig(wr::WebRenderError aError,
                                               const nsCString& aMsg) {
  // Clear out any cached errors from a remote device reset.
  mLastError.reset();
  mLastErrorMsg.reset();

  bool wantRestart;
  {
    // Collect the gfxVar updates into a single message.
    gfxVarsCollectUpdates collect;

    // Disable WebRender
    wantRestart = FallbackFromAcceleration(aError, aMsg);
    GPUPROCESS_LOG("Falling back from WebRender, error {}, want GPU process {}",
                   fmt::enums::format_as(aError), wantRestart);
    gfxVars::SetUseWebRenderDCompVideoHwOverlayWin(false);
    gfxVars::SetUseWebRenderDCompVideoSwOverlayWin(false);
  }

  // If we still have the GPU process, and we fallback to a new configuration
  // that prefers to have the GPU process, reset the counter. Because we
  // updated the gfxVars, we call GPUChild::EnsureGPUReady to force us to wait
  // for the update to be processed before creating new compositor sessions.
  // Otherwise we risk them being out of sync with the content/parent processes.
  if (wantRestart && mProcess && mGPUChild) {
    mUnstableProcessAttempts = 1;
    mGPUChild->EnsureGPUReady(/* aForceSync */ true);
  }
}

void GPUProcessManager::DisableWebRender(wr::WebRenderError aError,
                                         const nsCString& aMsg) {
  DisableWebRenderConfig(aError, aMsg);
  if (mProcess) {
    DestroyRemoteCompositorSessions();
  } else {
    DestroyInProcessCompositorSessions();
  }
  NotifyListenersOnCompositeDeviceReset();
}

void GPUProcessManager::NotifyWebRenderError(wr::WebRenderError aError) {
  gfxCriticalNote << "Handling webrender error " << (unsigned int)aError;
#ifdef XP_WIN
  if (aError == wr::WebRenderError::VIDEO_OVERLAY) {
    gfxVarsCollectUpdates collect;
    gfxVars::SetUseWebRenderDCompVideoHwOverlayWin(false);
    gfxVars::SetUseWebRenderDCompVideoSwOverlayWin(false);
    return;
  }
  if (aError == wr::WebRenderError::VIDEO_HW_OVERLAY) {
    gfxVars::SetUseWebRenderDCompVideoHwOverlayWin(false);
    return;
  }
  if (aError == wr::WebRenderError::VIDEO_SW_OVERLAY) {
    gfxVars::SetUseWebRenderDCompVideoSwOverlayWin(false);
    return;
  }
  if (aError == wr::WebRenderError::DCOMP_TEXTURE_OVERLAY) {
    gfxVars::SetUseWebRenderDCompositionTextureOverlayWin(false);
    return;
  }
  if (aError == wr::WebRenderError::SOFTWARE_D3D11) {
    if (!gfxVars::AllowSoftwareWebRenderD3D11()) {
      return;
    }
    gfxVars::SetAllowSoftwareWebRenderD3D11(false);
    // Recreate the compositors for all windows so that none remain using D3D11
    // compositing with SWGL, otherwise they would disagree on what textures
    // they can draw.
    if (mProcess) {
      if (mGPUChild) {
        mGPUChild->EnsureGPUReady(/* aForceSync */ true);
      }
      DestroyRemoteCompositorSessions();
    } else {
      DestroyInProcessCompositorSessions();
    }
    NotifyListenersOnCompositeDeviceReset();
    return;
  }
#else
  if (aError == wr::WebRenderError::VIDEO_OVERLAY ||
      aError == wr::WebRenderError::VIDEO_HW_OVERLAY ||
      aError == wr::WebRenderError::VIDEO_SW_OVERLAY ||
      aError == wr::WebRenderError::DCOMP_TEXTURE_OVERLAY ||
      aError == wr::WebRenderError::SOFTWARE_D3D11) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }
#endif

  // If we have a stable GPU process, this may just be due to an OOM or bad
  // driver state. In that case, we should consider restarting the GPU process
  // to hopefully alleviate the situation.
  if (mProcess && (IsProcessStable(TimeStamp::Now()) ||
                   (kIsAndroid && !mAppInForeground))) {
    mProcess->KillProcess(/* aGenerateMinidump */ false);
    mLastError = Some(aError);
    mLastErrorMsg = Some(""_ns);
    return;
  }

  DisableWebRender(aError, ""_ns);
}

/* static */
void GPUProcessManager::RecordDeviceReset(DeviceResetReason aReason) {
  if (aReason != DeviceResetReason::FORCED_RESET) {
    glean::gfx::device_reset_reason.AccumulateSingleSample(uint32_t(aReason));
  }

  CrashReporter::RecordAnnotationU32(
      CrashReporter::Annotation::DeviceResetReason,
      static_cast<uint32_t>(aReason));
}

/* static */
void GPUProcessManager::NotifyDeviceReset(DeviceResetReason aReason,
                                          DeviceResetDetectPlace aPlace) {
  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "gfx::GPUProcessManager::NotifyDeviceReset",
        [aReason, aPlace]() -> void {
          gfx::GPUProcessManager::NotifyDeviceReset(aReason, aPlace);
        }));
    return;
  }

#ifdef XP_WIN
  // Reset and reinitialize the compositor devices
  if (auto* deviceManager = DeviceManagerDx::Get()) {
    deviceManager->MaybeResetAndReacquireDevices();
  }
#else
  gfx::GPUProcessManager::RecordDeviceReset(aReason);
#endif

  if (XRE_IsGPUProcess()) {
    if (auto* gpuParent = GPUParent::GetSingleton()) {
      // End up to GPUProcessManager::OnRemoteProcessDeviceReset()
      gpuParent->NotifyDeviceReset(aReason, aPlace);
    } else {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    }
    return;
  }

  MOZ_ASSERT(XRE_IsParentProcess());
  if (auto* gpm = GPUProcessManager::Get()) {
    gpm->OnInProcessDeviceReset(aReason, aPlace);
  } else {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
  }
}

bool GPUProcessManager::OnDeviceReset(bool aTrackThreshold) {
  // Ignore resets for thresholding if requested.
  if (!aTrackThreshold) {
    return false;
  }

  // Detect whether the device is resetting too quickly or too much
  // indicating that we should give up and use software
  mDeviceResetCount++;

  auto newTime = TimeStamp::Now();
  auto delta = (int32_t)(newTime - mDeviceResetLastTime).ToMilliseconds();
  mDeviceResetLastTime = newTime;

  // Returns true if we should disable acceleration due to the reset.
  return ShouldLimitDeviceResets(mDeviceResetCount, delta);
}

void GPUProcessManager::OnInProcessDeviceReset(DeviceResetReason aReason,
                                               DeviceResetDetectPlace aPlace) {
  gfxCriticalNote << "Detect DeviceReset " << aReason << " " << aPlace
                  << " in Parent process";

  bool guilty;
  switch (aReason) {
    case DeviceResetReason::HUNG:
    case DeviceResetReason::RESET:
    case DeviceResetReason::INVALID_CALL:
      guilty = true;
      break;
    default:
      guilty = false;
      break;
  }

  GPUPROCESS_LOG("Device reset in parent process, reason {} place {}",
                 fmt::enums::format_as(aReason), fmt::enums::format_as(aPlace));

  if (OnDeviceReset(guilty)) {
    gfxCriticalNoteOnce << "In-process device reset threshold exceeded";
#ifdef MOZ_WIDGET_GTK
    // FIXME(aosmond): Should we disable WebRender on other platforms?
    DisableWebRenderConfig(wr::WebRenderError::EXCESSIVE_RESETS, nsCString());
#endif
  }
#ifdef XP_WIN
  // Ensure device reset handling before re-creating in process sessions.
  // Normally nsWindow::OnPaint() already handled it.
  gfxWindowsPlatform::GetPlatform()->HandleDeviceReset();
#endif
  DestroyInProcessCompositorSessions();
  NotifyListenersOnCompositeDeviceReset();
}

void GPUProcessManager::OnRemoteProcessDeviceReset(
    GPUProcessHost* aHost, const DeviceResetReason& aReason,
    const DeviceResetDetectPlace& aPlace) {
  gfxCriticalNote << "Detect DeviceReset " << aReason << " " << aPlace
                  << " in GPU process";
  GPUPROCESS_LOG("Device reset in GPU process {}, reason {} place {}",
                 mProcessToken, fmt::enums::format_as(aReason),
                 fmt::enums::format_as(aPlace));

  if (OnDeviceReset(/* aTrackThreshold */ true)) {
    // If we have a stable GPU process, this may just be due to an OOM or bad
    // driver state. In that case, we should consider restarting the GPU process
    // to hopefully alleviate the situation.
    if (mProcess && (IsProcessStable(TimeStamp::Now()) ||
                     (kIsAndroid && !mAppInForeground))) {
      GPUPROCESS_LOG("Too many device resets, restarting GPU process");
      mProcess->KillProcess(/* aGenerateMinidump */ false);
      mLastError = Some(wr::WebRenderError::EXCESSIVE_RESETS);
      mLastErrorMsg = Some(""_ns);
      return;
    }

    DisableWebRenderConfig(wr::WebRenderError::EXCESSIVE_RESETS, ""_ns);
  }

  DestroyRemoteCompositorSessions();
  NotifyListenersOnCompositeDeviceReset();
}

void GPUProcessManager::NotifyListenersOnCompositeDeviceReset() {
  nsTArray<RefPtr<GPUProcessListener>> listeners;
  listeners.AppendElements(mListeners);
  for (const auto& listener : listeners) {
    listener->OnCompositorDeviceReset();
  }
}

void GPUProcessManager::OnProcessUnexpectedShutdown(GPUProcessHost* aHost) {
  MOZ_ASSERT(mProcess && mProcess == aHost);

  if (aHost->WasKilledForTesting()) {
    GPUPROCESS_LOG("GPU process {} killed for testing",
                   aHost->GetProcessToken());
    CompositorManagerChild::OnGPUProcessLost(aHost->GetProcessToken());
    DestroyProcess(/* aUnexpectedShutdown */ true);
    HandleProcessLost();
    return;
  }

  if (StaticPrefs::layers_gpu_process_crash_also_crashes_browser()) {
    MOZ_CRASH("GPU process crashed and pref is set to crash the browser.");
  }

  GPUPROCESS_LOG("GPU process {} shut down unexpectedly",
                 aHost->GetProcessToken());
  CompositorManagerChild::OnGPUProcessLost(aHost->GetProcessToken());
  DestroyProcess(/* aUnexpectedShutdown */ true);

  // If the process didn't live long enough, increment our unstable attempts
  // counter so that we don't end up in a restart loop. If the process did live
  // long enough, reset the counter so that we don't disable the process too
  // eagerly.
  if (IsProcessStable(TimeStamp::Now())) {
    mProcessStableOnce = true;
    mUnstableProcessAttempts = 0;
  } else if (kIsAndroid && !mAppInForeground) {
    // On Android if the process is lost whilst in the background it was
    // probably killed by the OS, and it may never have had a chance to have
    // been declared stable prior to being killed. We don't want this happening
    // repeatedly to result in the GPU process being disabled, so treat any
    // process lost whilst in the background as stable.
    mUnstableProcessAttempts = 0;
  } else {
    mUnstableProcessAttempts++;
    mozilla::glean::gpu_process::unstable_launch_attempts.Set(
        mUnstableProcessAttempts);
  }

  if (mUnstableProcessAttempts >
      uint32_t(StaticPrefs::layers_gpu_process_max_restarts())) {
    char disableMessage[64];
    SprintfLiteral(disableMessage, "GPU process disabled after %d attempts",
                   mTotalProcessAttempts);
    if (!MaybeDisableGPUProcess(disableMessage, /* aAllowRestart */ true)) {
      // Fallback wants the GPU process. Reset our counter.
      MOZ_DIAGNOSTIC_ASSERT(gfxConfig::IsEnabled(Feature::GPU_PROCESS));
      mUnstableProcessAttempts = 0;
      HandleProcessLost();
    } else {
      MOZ_DIAGNOSTIC_ASSERT(!gfxConfig::IsEnabled(Feature::GPU_PROCESS));
    }
  } else if (
      // On Android, we only stop decoding in the GPU process if it is disabled,
      // since we only fallback to platform decoding in the RDD process then.
      !kIsAndroid &&
      mUnstableProcessAttempts >
          uint32_t(
              StaticPrefs::layers_gpu_process_max_restarts_with_decoder()) &&
      mDecodeVideoOnGpuProcess) {
    mDecodeVideoOnGpuProcess = false;
    dom::ContentParent::BroadcastMediaCodecsSupportedUpdate(
        RemoteMediaIn::GpuProcess, media::MediaCodecsSupported{});
    mozilla::glean::gpu_process::crash_fallbacks.Get("decoding_disabled"_ns)
        .Add(1);
    HandleProcessLost();
  } else {
    mozilla::glean::gpu_process::crash_fallbacks.Get("none"_ns).Add(1);
    HandleProcessLost();
  }
}

void GPUProcessManager::HandleProcessLost() {
  MOZ_ASSERT(NS_IsMainThread());

  // The shutdown and restart sequence for the GPU process is as follows:
  //
  //  (1) The GPU process dies. IPDL will enqueue an ActorDestroy message on
  //      each channel owning a bridge to the GPU process, on the thread owning
  //      that channel.
  //
  //  (2) The first channel to process its ActorDestroy message will post a
  //      message to the main thread to call NotifyRemoteActorDestroyed on the
  //      GPUProcessManager, which calls OnProcessUnexpectedShutdown if it has
  //      not handled shutdown for this process yet. OnProcessUnexpectedShutdown
  //      is responsible for tearing down the old process and deciding whether
  //      or not to disable the GPU process. It then calls this function,
  //      HandleProcessLost.
  //
  //  (3) We then notify each widget that its session with the compositor is now
  //      invalid. The widget is responsible for destroying its layer manager
  //      and CompositorBridgeChild. Note that at this stage, not all actors may
  //      have received ActorDestroy yet. CompositorBridgeChild may attempt to
  //      send messages, and if this happens, it will probably report a
  //      MsgDropped error. This is okay.
  //
  //  (4) At this point, the UI process has a clean slate: no layers should
  //      exist for the old compositor. We may make a decision on whether or not
  //      to re-launch the GPU process. Or, on Android if the app is in the
  //      background we may decide to wait until it comes to the foreground
  //      before re-launching.
  //
  //  (5) When we do decide to re-launch, or continue without a GPU process, we
  //      notify each ContentParent of the lost connection. It will request new
  //      endpoints from the GPUProcessManager and forward them to its
  //      ContentChild. The parent-side of these endpoints may come from the
  //      compositor thread of the UI process, or the compositor thread of the
  //      GPU process. However, no actual compositors should exist yet.
  //
  //  (6) Each ContentChild will receive new endpoints. It will destroy its
  //      Compositor/ImageBridgeChild singletons and recreate them, as well
  //      as invalidate all retained layers.
  //
  //  (7) In addition, each ContentChild will ask each of its BrowserChildren
  //      to re-request association with the compositor for the window
  //      owning the tab. The sequence of calls looks like:
  //        (a) [CONTENT] ContentChild::RecvReinitRendering
  //        (b) [CONTENT] BrowserChild::ReinitRendering
  //        (c) [CONTENT] BrowserChild::SendEnsureLayersConnected
  //        (d)      [UI] BrowserParent::RecvEnsureLayersConnected
  //        (e)      [UI] RemoteLayerTreeOwner::EnsureLayersConnected
  //        (f)      [UI] CompositorBridgeChild::SendNotifyChildRecreated
  //
  //      Note that at step (e), RemoteLayerTreeOwner will call
  //      GetWindowRenderer on the nsIWidget owning the tab. This step ensures
  //      that a compositor exists for the window. If we decided to launch a new
  //      GPU Process, at this point we block until the process has launched and
  //      we're able to create a new window compositor. Otherwise, if
  //      compositing is now in-process, this will simply create a new
  //      CompositorBridgeParent in the UI process. If there are multiple tabs
  //      in the same window, additional tabs will simply return the already-
  //      established compositor.
  //
  //      Finally, this step serves one other crucial function: tabs must be
  //      associated with a window compositor or else they can't forward
  //      layer transactions. So this step both ensures that a compositor
  //      exists, and that the tab can forward layers.
  //
  //  (8) Last, if the window had no remote tabs, step (7) will not have
  //      applied, and the window will not have a new compositor just yet. The
  //      next refresh tick and paint will ensure that one exists, again via
  //      nsIWidget::GetWindowRenderer. On Android, we called
  //      nsIWidgetListener::RequestRepaint back in step (3) to ensure this
  //      tick occurs, but on other platforms this is not necessary.

  DestroyRemoteCompositorSessions();

#ifdef MOZ_WIDGET_ANDROID
  java::SurfaceControlManager::GetInstance()->OnGpuProcessLoss();
#endif

  GPUPROCESS_LOG(
      "Handling GPU process loss, GPU process {}, app in "
      "foreground {}",
      gfxConfig::IsEnabled(Feature::GPU_PROCESS) ? "enabled" : "disabled",
      mAppInForeground);

  // Re-launch the process if immediately if the GPU process is still enabled.
  // Except on Android if the app is in the background, where we want to wait
  // until the app is in the foreground again.
  if (gfxConfig::IsEnabled(Feature::GPU_PROCESS)) {
#ifdef MOZ_WIDGET_ANDROID
    if (mAppInForeground) {
#else
    {
#endif
      (void)LaunchGPUProcess();
    }
  } else {
    // If the GPU process is disabled we can reinitialize rendering immediately.
    // This will be handled in OnProcessLaunchComplete() if the GPU process is
    // enabled.
    ReinitializeRendering();
  }
}

void GPUProcessManager::ReinitializeRendering() {
  // Notify content. This will ensure that each content process re-establishes
  // a connection to the compositor thread (whether it's in-process or in a
  // newly launched GPU process).
  GPUPROCESS_LOG("Reinitializing rendering, GPU process {}", mProcessToken);
  nsTArray<RefPtr<GPUProcessListener>> listeners;
  listeners.AppendElements(mListeners);
  // Make sure any fallback renderers get destroyed first.
  for (const auto& listener : listeners) {
    listener->OnCompositorDestroyBackgrounded();
  }
  // Then do the recreations.
  for (const auto& listener : listeners) {
    listener->OnCompositorUnexpectedShutdown();
  }

  // Notify any observers that the compositor has been reinitialized,
  // eg the ZoomConstraintsClients for parent process documents.
  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (observerService) {
    observerService->NotifyObservers(nullptr, "compositor-reinitialized",
                                     nullptr);
  }
}

void GPUProcessManager::DestroyRemoteCompositorSessions() {
  // Build a list of sessions to notify, since notification might delete
  // entries from the list.
  nsTArray<RefPtr<RemoteCompositorSession>> sessions;
  for (auto& session : mRemoteSessions) {
    sessions.AppendElement(session);
  }

  // Notify each widget that we have lost the GPU process. This will ensure
  // that each widget destroys its layer manager and CompositorBridgeChild.
  for (const auto& session : sessions) {
    session->NotifySessionLost();
  }
}

void GPUProcessManager::DestroyInProcessCompositorSessions() {
  // Build a list of sessions to notify, since notification might delete
  // entries from the list.
  nsTArray<RefPtr<InProcessCompositorSession>> sessions;
  for (auto& session : mInProcessSessions) {
    sessions.AppendElement(session);
  }

  // Notify each widget that we have lost the GPU process. This will ensure
  // that each widget destroys its layer manager and CompositorBridgeChild.
  for (const auto& session : sessions) {
    session->NotifySessionLost();
  }

  // Ensure our stablility state is reset so that we don't necessarily crash
  // right away on some WebRender errors.
  CompositorBridgeParent::ResetStable();
  ResetProcessStable();
}

void GPUProcessManager::NotifyRemoteActorDestroyed(
    const uint64_t& aProcessToken) {
  if (!NS_IsMainThread()) {
    RefPtr<Runnable> task = mTaskFactory.NewRunnableMethod(
        &GPUProcessManager::NotifyRemoteActorDestroyed, aProcessToken);
    NS_DispatchToMainThread(task.forget());
    return;
  }

  if (mProcessToken != aProcessToken) {
    // This token is for an older process; we can safely ignore it.
    return;
  }

  GPUPROCESS_LOG("Actor for GPU process {} destroyed before PGPU",
                 aProcessToken);

  // One of the bridged top-level actors for the GPU process has been
  // prematurely terminated, and we're receiving a notification. This
  // can happen if the ActorDestroy for a bridged protocol fires
  // before the ActorDestroy for PGPUChild.
  OnProcessUnexpectedShutdown(mProcess);
}

void GPUProcessManager::ShutdownInternal() {
  if (mObserver) {
    mObserver->Shutdown();
    mObserver = nullptr;
  }

  DestroyProcess();
  mVsyncIOThread = nullptr;
}

void GPUProcessManager::KillProcess(bool aGenerateMinidump) {
  if (!NS_IsMainThread()) {
    RefPtr<Runnable> task = mTaskFactory.NewRunnableMethod(
        &GPUProcessManager::KillProcess, aGenerateMinidump);
    NS_DispatchToMainThread(task.forget());
    return;
  }

  if (!mProcess) {
    return;
  }

  mProcess->KillProcess(aGenerateMinidump);
}

void GPUProcessManager::KillProcessForTesting() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mProcess) {
    return;
  }

  mProcess->KillProcessForTesting();
}

void GPUProcessManager::CrashProcess() {
  if (!mProcess) {
    return;
  }

  mProcess->CrashProcess();
}

void GPUProcessManager::DestroyProcess(bool aUnexpectedShutdown) {
  if (!mProcess) {
    return;
  }

  GPUPROCESS_LOG("Destroying GPU process {}", mProcessToken);
  mProcess->Shutdown(aUnexpectedShutdown);
  mProcessToken = 0;
  mProcess = nullptr;
  mGPUChild = nullptr;
  mQueuedPrefs.Clear();
  if (mVsyncBridge) {
    mVsyncBridge->Close();
    mVsyncBridge = nullptr;
  }
  StopBatteryObserving();

  CrashReporter::RecordAnnotationCString(
      CrashReporter::Annotation::GPUProcessStatus, "Destroyed");
}

void GPUProcessManager::StopBatteryObserving() {
  if (mBatteryObserver) {
    mBatteryObserver->Shutdown();
    mBatteryObserver = nullptr;
  }
}

already_AddRefed<CompositorSession> GPUProcessManager::CreateTopLevelCompositor(
    nsIWidget* aWidget, CSSToLayoutDeviceScale aScale,
    const CompositorOptions& aOptions, bool aUseExternalSurfaceSize,
    const gfx::IntSize& aSurfaceSize, uint64_t aInnerWindowId,
    bool* aRetryOut) {
  MOZ_DIAGNOSTIC_ASSERT(IsGPUReady());
  MOZ_ASSERT(aRetryOut);

  if (!EnsureProtocolsReady()) {
    *aRetryOut = false;
    return nullptr;
  }

  LayersId layerTreeId = AllocateLayerTreeId();
  RefPtr<CompositorSession> session;
  if (mGPUChild) {
    session = CreateRemoteSession(aWidget, layerTreeId, aScale, aOptions,
                                  aUseExternalSurfaceSize, aSurfaceSize,
                                  aInnerWindowId);
    if (NS_WARN_IF(!session)) {
      // This may have failed for intermittent reasons, or perhaps indicates we
      // are fundamentally unable to use acceleration.
      // OnProcessUnexpectedShutdown will first attempt to relaunch the GPU
      // process in the same configuration a number of times, then fallback from
      // acceleration, then finally disable the GPU process if it continues to
      // fail.
      OnProcessUnexpectedShutdown(mProcess);
      *aRetryOut = true;
      return nullptr;
    }
  } else {
    session = InProcessCompositorSession::Create(
        aWidget, layerTreeId, aScale, aOptions, aUseExternalSurfaceSize,
        aSurfaceSize, AllocateNamespace(), aInnerWindowId);
  }

  LAYERTREE_LOG("Created {} compositor for root layer tree {:x}",
                mGPUChild ? "remote" : "in-process", layerTreeId);

#if defined(MOZ_WIDGET_ANDROID)
  if (session) {
    // Nothing to do if controller gets a nullptr
    auto controller =
        CreateUiCompositorController(aWidget, session->RootLayerTreeId());
    MOZ_ASSERT(controller);
    session->SetUiCompositorControllerChild(std::move(controller));
  }
#endif  // defined(MOZ_WIDGET_ANDROID)

  *aRetryOut = false;
  return session.forget();
}

RefPtr<CompositorSession> GPUProcessManager::CreateRemoteSession(
    nsIWidget* aWidget, const LayersId& aRootLayerTreeId,
    CSSToLayoutDeviceScale aScale, const CompositorOptions& aOptions,
    bool aUseExternalSurfaceSize, const gfx::IntSize& aSurfaceSize,
    uint64_t aInnerWindowId) {
#ifdef MOZ_WIDGET_SUPPORTS_OOP_COMPOSITING
  widget::CompositorWidgetInitData initData;
  aWidget->GetCompositorWidgetInitData(&initData);

  RefPtr<CompositorBridgeChild> child =
      CompositorManagerChild::CreateWidgetCompositorBridge(
          mProcessToken, AllocateNamespace(), aScale, aOptions,
          aUseExternalSurfaceSize, aSurfaceSize, aInnerWindowId);
  if (!child) {
    gfxCriticalNote << "Failed to create CompositorBridgeChild";
    return nullptr;
  }

  RefPtr<CompositorVsyncDispatcher> dispatcher =
      aWidget->GetCompositorVsyncDispatcher();
  RefPtr<widget::CompositorWidgetVsyncObserver> observer =
      new widget::CompositorWidgetVsyncObserver(mVsyncBridge, aRootLayerTreeId);

  widget::CompositorWidgetChild* widget =
      new widget::CompositorWidgetChild(dispatcher, observer, initData);
  if (!child->SendPCompositorWidgetConstructor(widget, std::move(initData))) {
    return nullptr;
  }
  if (!widget->Initialize(aOptions)) {
    return nullptr;
  }
  if (!child->SendInitialize(aRootLayerTreeId)) {
    return nullptr;
  }

  RefPtr<APZCTreeManagerChild> apz = nullptr;
  if (aOptions.UseAPZ()) {
    apz = MakeRefPtr<APZCTreeManagerChild>();
    if (!child->SendPAPZCTreeManagerConstructor(apz, LayersId{0})) {
      return nullptr;
    }

    ipc::Endpoint<PAPZBridgeParent> parentPipe;
    ipc::Endpoint<PAPZBridgeChild> childPipe;
    nsresult rv = PAPZBridge::CreateEndpoints(
        mGPUChild->OtherEndpointProcInfo(), ipc::EndpointProcInfo::Current(),
        &parentPipe, &childPipe);
    if (NS_FAILED(rv)) {
      return nullptr;
    }
    mGPUChild->SendInitAPZBridge(aRootLayerTreeId, std::move(parentPipe));

    RefPtr<APZBridgeChild> apzBridge =
        APZBridgeChild::Create(mProcessToken, std::move(childPipe));
    if (!apzBridge) {
      return nullptr;
    }

    apz->SetBridge(std::move(apzBridge));
  }

  return MakeRefPtr<RemoteCompositorSession>(aWidget, child, widget,
                                             std::move(apz), aRootLayerTreeId);
#else
  gfxCriticalNote << "Platform does not support out-of-process compositing";
  return nullptr;
#endif
}

bool GPUProcessManager::CreateContentBridges(
    ipc::EndpointProcInfo aOtherProcess,
    ipc::Endpoint<PCompositorManagerChild>* aOutCompositor,
    ipc::Endpoint<PImageBridgeChild>* aOutImageBridge,
    ipc::Endpoint<PVRManagerChild>* aOutVRBridge,
    ipc::Endpoint<PRemoteMediaManagerChild>* aOutVideoManager,
    dom::ContentParentId aChildId, nsTArray<uint32_t>* aNamespaces) {
  const uint32_t compositorManagerNamespace = AllocateNamespace();
  const uint32_t compositorBridgeNamespace = AllocateNamespace();
  const uint32_t imageBridgeNamespace = AllocateNamespace();
  const uint32_t vrManagerNamespace = AllocateNamespace();
  if (!CreateContentCompositorManager(
          aOtherProcess, aChildId, compositorManagerNamespace,
          compositorBridgeNamespace, aOutCompositor) ||
      !CreateContentImageBridge(aOtherProcess, aChildId, imageBridgeNamespace,
                                aOutImageBridge) ||
      !CreateContentVRManager(aOtherProcess, aChildId, vrManagerNamespace,
                              aOutVRBridge)) {
    LAYERTREE_LOG("Failed to create compositor bridges for content {}",
                  aChildId);
    return false;
  }
  // RemoteMediaManager is only supported in the GPU process, so we allow this
  // to be fallible.
  CreateContentRemoteMediaManager(aOtherProcess, aChildId, aOutVideoManager);

  LAYERTREE_LOG(
      "Created {} compositor bridges for content {}, compositor manager "
      "namespace {}, compositor bridge namespace {}",
      mGPUChild ? "remote" : "in-process", aChildId, compositorManagerNamespace,
      compositorBridgeNamespace);

  // The order of the outparam namespaces must be kept in sync with
  // ContentChild::RecvInitRendering and ContentChild::RecvReinitRendering.
  aNamespaces->AppendElement(compositorManagerNamespace);
  aNamespaces->AppendElement(compositorBridgeNamespace);
  aNamespaces->AppendElement(imageBridgeNamespace);
  aNamespaces->AppendElement(vrManagerNamespace);
  return true;
}

bool GPUProcessManager::CreateContentCompositorManager(
    ipc::EndpointProcInfo aOtherProcess, dom::ContentParentId aChildId,
    uint32_t aNamespace, uint32_t aContentBridgeNamespace,
    ipc::Endpoint<PCompositorManagerChild>* aOutEndpoint) {
  MOZ_DIAGNOSTIC_ASSERT(IsGPUReady());

  ipc::Endpoint<PCompositorManagerParent> parentPipe;
  ipc::Endpoint<PCompositorManagerChild> childPipe;

  ipc::EndpointProcInfo parentInfo = mGPUChild
                                         ? mGPUChild->OtherEndpointProcInfo()
                                         : ipc::EndpointProcInfo::Current();

  nsresult rv = PCompositorManager::CreateEndpoints(parentInfo, aOtherProcess,
                                                    &parentPipe, &childPipe);
  if (NS_FAILED(rv)) {
    gfxCriticalNote << "Could not create content compositor manager: "
                    << hexa(int(rv));
    return false;
  }

  if (mGPUChild) {
    mGPUChild->SendNewContentCompositorManager(
        std::move(parentPipe), aChildId, aNamespace, aContentBridgeNamespace);
  } else if (!CompositorManagerParent::Create(std::move(parentPipe), aChildId,
                                              aNamespace,
                                              aContentBridgeNamespace,
                                              /* aIsRoot */ false)) {
    return false;
  }

  *aOutEndpoint = std::move(childPipe);
  return true;
}

bool GPUProcessManager::CreateContentImageBridge(
    ipc::EndpointProcInfo aOtherProcess, dom::ContentParentId aChildId,
    uint32_t aNamespace, ipc::Endpoint<PImageBridgeChild>* aOutEndpoint) {
  MOZ_DIAGNOSTIC_ASSERT(IsGPUReady());

  if (!EnsureImageBridgeChild()) {
    return false;
  }

  ipc::EndpointProcInfo parentInfo = mGPUChild
                                         ? mGPUChild->OtherEndpointProcInfo()
                                         : ipc::EndpointProcInfo::Current();

  ipc::Endpoint<PImageBridgeParent> parentPipe;
  ipc::Endpoint<PImageBridgeChild> childPipe;
  nsresult rv = PImageBridge::CreateEndpoints(parentInfo, aOtherProcess,
                                              &parentPipe, &childPipe);
  if (NS_FAILED(rv)) {
    gfxCriticalNote << "Could not create content compositor bridge: "
                    << hexa(int(rv));
    return false;
  }

  if (mGPUChild) {
    mGPUChild->SendNewContentImageBridge(std::move(parentPipe), aChildId,
                                         aNamespace);
  } else {
    if (!ImageBridgeParent::CreateForContent(std::move(parentPipe), aChildId,
                                             aNamespace)) {
      return false;
    }
  }

  *aOutEndpoint = std::move(childPipe);
  return true;
}

base::ProcessId GPUProcessManager::GPUProcessPid() {
  base::ProcessId gpuPid =
      mGPUChild ? mGPUChild->OtherPid() : base::kInvalidProcessId;
  return gpuPid;
}

ipc::EndpointProcInfo GPUProcessManager::GPUEndpointProcInfo() {
  return mGPUChild ? mGPUChild->OtherEndpointProcInfo()
                   : ipc::EndpointProcInfo::Invalid();
}

bool GPUProcessManager::CreateContentVRManager(
    ipc::EndpointProcInfo aOtherProcess, dom::ContentParentId aChildId,
    uint32_t aNamespace, ipc::Endpoint<PVRManagerChild>* aOutEndpoint) {
  MOZ_DIAGNOSTIC_ASSERT(IsGPUReady());

  if (NS_WARN_IF(!EnsureVRManager())) {
    return false;
  }

  ipc::EndpointProcInfo parentInfo = mGPUChild
                                         ? mGPUChild->OtherEndpointProcInfo()
                                         : ipc::EndpointProcInfo::Current();

  ipc::Endpoint<PVRManagerParent> parentPipe;
  ipc::Endpoint<PVRManagerChild> childPipe;
  nsresult rv = PVRManager::CreateEndpoints(parentInfo, aOtherProcess,
                                            &parentPipe, &childPipe);
  if (NS_FAILED(rv)) {
    gfxCriticalNote << "Could not create content compositor bridge: "
                    << hexa(int(rv));
    return false;
  }

  if (mGPUChild) {
    mGPUChild->SendNewContentVRManager(std::move(parentPipe), aChildId,
                                       aNamespace);
  } else {
    if (!VRManagerParent::CreateForContent(std::move(parentPipe), aChildId,
                                           aNamespace)) {
      return false;
    }
  }

  *aOutEndpoint = std::move(childPipe);
  return true;
}

void GPUProcessManager::CreateContentRemoteMediaManager(
    ipc::EndpointProcInfo aOtherProcess, dom::ContentParentId aChildId,
    ipc::Endpoint<PRemoteMediaManagerChild>* aOutEndpoint) {
  MOZ_DIAGNOSTIC_ASSERT(IsGPUReady());

  // The same endpoint is used for both decoding and encoding.
  if (!mGPUChild ||
      !(StaticPrefs::media_gpu_process_decoder() ||
        StaticPrefs::media_gpu_process_encoder()) ||
      !mDecodeVideoOnGpuProcess) {
    return;
  }

  ipc::Endpoint<PRemoteMediaManagerParent> parentPipe;
  ipc::Endpoint<PRemoteMediaManagerChild> childPipe;

  nsresult rv = PRemoteMediaManager::CreateEndpoints(
      mGPUChild->OtherEndpointProcInfo(), aOtherProcess, &parentPipe,
      &childPipe);
  if (NS_FAILED(rv)) {
    gfxCriticalNote << "Could not create content video decoder: "
                    << hexa(int(rv));
    return;
  }

  mGPUChild->SendNewContentRemoteMediaManager(std::move(parentPipe), aChildId);

  *aOutEndpoint = std::move(childPipe);
}

#ifdef MOZ_WMF_MEDIA_ENGINE
nsresult GPUProcessManager::CreateUtilityMFCDMVideoBridge(
    mozilla::ipc::UtilityMediaServiceChild* aChild,
    mozilla::ipc::EndpointProcInfo aOtherProcess) {
  MOZ_ASSERT(aChild);
  MOZ_ASSERT(aChild->CanSend());

  ipc::Endpoint<PVideoBridgeChild> childPipe;
  nsresult rv = EnsureVideoBridge(VideoBridgeSource::MFMediaEngineCDMProcess,
                                  aOtherProcess, &childPipe);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  gfx::ContentDeviceData contentDeviceData;
  gfxPlatform::GetPlatform()->BuildContentDeviceData(&contentDeviceData);
  aChild->SendInitVideoBridge(std::move(childPipe), contentDeviceData);
  return NS_OK;
}
#endif

nsresult GPUProcessManager::CreateRddVideoBridge(RDDProcessManager* aRDD,
                                                 RDDChild* aChild) {
  MOZ_ASSERT(aRDD);
  MOZ_ASSERT(aChild);
  MOZ_ASSERT(aChild->CanSend());

  ipc::Endpoint<PVideoBridgeChild> childPipe;
  nsresult rv = EnsureVideoBridge(VideoBridgeSource::RddProcess,
                                  aChild->OtherEndpointProcInfo(), &childPipe);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  gfx::ContentDeviceData contentDeviceData;
  gfxPlatform::GetPlatform()->BuildContentDeviceData(&contentDeviceData);
  aChild->SendInitVideoBridge(std::move(childPipe),
                              !aRDD->AttemptedRDDProcess(), contentDeviceData);
  return NS_OK;
}

nsresult GPUProcessManager::EnsureVideoBridge(
    layers::VideoBridgeSource aSource,
    mozilla::ipc::EndpointProcInfo aOtherProcess,
    mozilla::ipc::Endpoint<layers::PVideoBridgeChild>* aOutChildPipe) {
  MOZ_ASSERT(aOutChildPipe);
  MOZ_DIAGNOSTIC_ASSERT(IsGPUReady());

  ipc::EndpointProcInfo gpuInfo = mGPUChild ? mGPUChild->OtherEndpointProcInfo()
                                            : ipc::EndpointProcInfo::Current();

  // The child end is the producer of video frames; the parent end is the
  // consumer.
  ipc::Endpoint<PVideoBridgeParent> parentPipe;
  nsresult rv = PVideoBridge::CreateEndpoints(gpuInfo, aOtherProcess,
                                              &parentPipe, aOutChildPipe);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (mGPUChild) {
    mGPUChild->SendInitVideoBridge(std::move(parentPipe), aSource);
  } else {
    VideoBridgeParent::Open(std::move(parentPipe), aSource);
  }
  return NS_OK;
}

void GPUProcessManager::UnmapLayerTreeId(LayersId aLayersId,
                                         base::ProcessId aOwningId) {
  // If the GPU process is down, but not disabled, there is no need to relaunch
  // here because the layers ID is already invalid.
  if (mGPUChild) {
    mGPUChild->SendRemoveLayerTreeIdMapping(
        LayerTreeIdMapping(aLayersId, aOwningId));
  } else if (!gfxConfig::IsEnabled(Feature::GPU_PROCESS)) {
    CompositorBridgeParent::DeallocateLayerTreeId(aLayersId);
  }

  LayerTreeOwnerTracker::Get()->Unmap(aLayersId, aOwningId);
}

bool GPUProcessManager::IsLayerTreeIdMapped(LayersId aLayersId,
                                            base::ProcessId aRequestingId) {
  return LayerTreeOwnerTracker::Get()->IsMapped(aLayersId, aRequestingId);
}

LayersId GPUProcessManager::AllocateLayerTreeId() {
  // Each layer tree gets its own id namespace, which keeps its pipelines apart
  // from those of other trees, external image ids and async image pipeline
  // ids. Its pipeline uses handle 0 and GetTemporaryWebRenderPipelineId uses
  // handle 1.
  MOZ_ASSERT(NS_IsMainThread());
  uint64_t layerTreeId = AllocateNamespace();
  layerTreeId <<= 32;
  return LayersId{layerTreeId};
}

// See the comment in AllocateLayerTreeId above.
// For now this is only used for view-transition snapshots of the old state,
// it's probably best to avoid using this for anything else.
wr::PipelineId GetTemporaryWebRenderPipelineId(wr::PipelineId aMainPipeline) {
  MOZ_ASSERT(aMainPipeline.mHandle == 0);
  auto id = aMainPipeline;
  id.mHandle += 1;
  return id;
}

uint32_t GPUProcessManager::AllocateNamespace() {
  MOZ_ASSERT(NS_IsMainThread());
  return ++mNextNamespace;
}

bool GPUProcessManager::AllocateAndConnectLayerTreeId(
    PCompositorBridgeChild* aCompositorBridge, base::ProcessId aOtherPid,
    LayersId aEmbedderLayersId, LayersId* aOutLayersId,
    CompositorOptions* aOutCompositorOptions) {
  MOZ_ASSERT(aOutLayersId);

  LayersId layersId = AllocateLayerTreeId();
  *aOutLayersId = layersId;
  LAYERTREE_LOG("Allocated layer tree {:x} for pid {}, embedder {:x}, {}",
                layersId, aOtherPid, aEmbedderLayersId,
                aCompositorBridge ? "with compositor" : "without compositor");

  // We always map the layer ID in the parent process so that we can recover
  // from GPU process crashes. In that case, the tree will be shared with the
  // new GPU process at initialization.
  LayerTreeOwnerTracker::Get()->Map(layersId, aOtherPid);

  if (NS_WARN_IF(NS_FAILED(EnsureGPUReady()))) {
    return false;
  }

  // If we have a CompositorBridgeChild, then we need to call
  // CompositorBridgeParent::NotifyChildCreated. If this is in the GPU process,
  // we can combine it with LayerTreeOwnerTracker::Map to minimize IPC.
  // messages.
  if (aCompositorBridge) {
    if (mGPUChild) {
      return aCompositorBridge->SendMapAndNotifyChildCreated(
          layersId, aEmbedderLayersId, aOtherPid, aOutCompositorOptions);
    }
    return aCompositorBridge->SendNotifyChildCreated(
        layersId, aEmbedderLayersId, aOutCompositorOptions);
  }

  // If we don't have a CompositorBridgeChild, we just need to call
  // LayerTreeOwnerTracker::Map in the compositing process.
  if (mGPUChild) {
    mGPUChild->SendAddLayerTreeIdMapping(
        LayerTreeIdMapping(layersId, aOtherPid));
  }
  return false;
}

void GPUProcessManager::EnsureVsyncIOThread() {
  if (mVsyncIOThread) {
    return;
  }

  mVsyncIOThread = new VsyncIOThreadHolder();
  MOZ_RELEASE_ASSERT(mVsyncIOThread->Start());
}

void GPUProcessManager::ShutdownVsyncIOThread() { mVsyncIOThread = nullptr; }

void GPUProcessManager::RegisterRemoteProcessSession(
    RemoteCompositorSession* aSession) {
  mRemoteSessions.AppendElement(aSession);
}

void GPUProcessManager::UnregisterRemoteProcessSession(
    RemoteCompositorSession* aSession) {
  mRemoteSessions.RemoveElement(aSession);
}

void GPUProcessManager::RegisterInProcessSession(
    InProcessCompositorSession* aSession) {
  mInProcessSessions.AppendElement(aSession);
}

void GPUProcessManager::UnregisterInProcessSession(
    InProcessCompositorSession* aSession) {
  mInProcessSessions.RemoveElement(aSession);
}

void GPUProcessManager::AddListener(GPUProcessListener* aListener) {
  if (!mListeners.Contains(aListener)) {
    mListeners.AppendElement(aListener);
  }
}

void GPUProcessManager::RemoveListener(GPUProcessListener* aListener) {
  mListeners.RemoveElement(aListener);
}

bool GPUProcessManager::FlushActiveCheckerboardReports() {
  if (mGPUChild) {
    mGPUChild->SendFlushActiveCheckerboardReports();
    return true;
  }

  if (!gfxConfig::IsEnabled(Feature::GPU_PROCESS)) {
    nsCOMPtr<nsIObserverService> obsSvc =
        mozilla::services::GetObserverService();
    MOZ_ASSERT(obsSvc);
    if (obsSvc) {
      obsSvc->NotifyObservers(nullptr, "APZ:FlushActiveCheckerboard", nullptr);
    }
    return true;
  }

  // If we still are using a GPU process, but do not have one ready at the
  // moment, we can drop these notifications since there is nothing to do.
  return false;
}

class GPUMemoryReporter : public MemoryReportingProcess {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(GPUMemoryReporter, override)

  bool IsAlive() const override {
    if (GPUProcessManager* gpm = GPUProcessManager::Get()) {
      return !!gpm->GetGPUChild();
    }
    return false;
  }

  bool SendRequestMemoryReport(
      const uint32_t& aGeneration, const bool& aAnonymize,
      const bool& aMinimizeMemoryUsage,
      const Maybe<ipc::FileDescriptor>& aDMDFile) override {
    GPUChild* child = GetChild();
    if (!child) {
      return false;
    }

    return child->SendRequestMemoryReport(aGeneration, aAnonymize,
                                          aMinimizeMemoryUsage, aDMDFile);
  }

  int32_t Pid() const override {
    if (GPUChild* child = GetChild()) {
      return (int32_t)child->OtherPid();
    }
    return 0;
  }

 private:
  GPUChild* GetChild() const {
    if (GPUProcessManager* gpm = GPUProcessManager::Get()) {
      return gpm->GetGPUChild();
    }
    return nullptr;
  }

 protected:
  ~GPUMemoryReporter() = default;
};

RefPtr<MemoryReportingProcess> GPUProcessManager::GetProcessMemoryReporter() {
  // If we are in the middle of launching a GPU process, we can wait for it to
  // finish, otherwise if there is no GPU process, we should just return now to
  // avoid launching it again.
  if (!mProcess || AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMShutdown) ||
      !mProcess->WaitForLaunch()) {
    return nullptr;
  }
  return MakeRefPtr<GPUMemoryReporter>();
}

void GPUProcessManager::SetAppInForeground(bool aInForeground) {
  if (mAppInForeground == aInForeground) {
    return;
  }

  mAppInForeground = aInForeground;
#if defined(XP_WIN)
  SetProcessIsForeground();
#endif

  // If we moved into the foreground, then we need to make sure the GPU process
  // completes its launch. Otherwise listeners may be left dangling from
  // previous calls that returned NS_ERROR_ABORT due to being in the background.
  if (aInForeground && gfxConfig::IsEnabled(Feature::GPU_PROCESS)) {
    (void)LaunchGPUProcess();
  }
}

#if defined(XP_WIN)
void GPUProcessManager::SetProcessIsForeground() {
  NTSTATUS WINAPI NtSetInformationProcess(
      IN HANDLE process_handle, IN ULONG info_class,
      IN PVOID process_information, IN ULONG information_length);
  constexpr unsigned int NtProcessInformationForeground = 25;

  static bool alreadyInitialized = false;
  static decltype(NtSetInformationProcess)* setInformationProcess = nullptr;
  if (!alreadyInitialized) {
    alreadyInitialized = true;
    nsModuleHandle module(LoadLibrary(L"ntdll.dll"));
    if (module) {
      setInformationProcess =
          (decltype(NtSetInformationProcess)*)GetProcAddress(
              module, "NtSetInformationProcess");
    }
  }
  if (MOZ_UNLIKELY(!setInformationProcess)) {
    return;
  }

  unsigned pid = GPUProcessPid();
  if (pid <= 0) {
    return;
  }
  // Using the handle from mProcess->GetChildProcessHandle() fails;
  // the PROCESS_SET_INFORMATION permission is probably missing.
  nsAutoHandle processHandle(
      ::OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid));
  if (!processHandle) {
    return;
  }

  BOOLEAN foreground = mAppInForeground;
  setInformationProcess(processHandle, NtProcessInformationForeground,
                        (PVOID)&foreground, sizeof(foreground));
}
#endif

RefPtr<PGPUChild::TestTriggerMetricsPromise>
GPUProcessManager::TestTriggerMetrics() {
  if (!NS_WARN_IF(!mGPUChild)) {
    return mGPUChild->SendTestTriggerMetrics();
  }

  return PGPUChild::TestTriggerMetricsPromise::CreateAndReject(
      ipc::ResponseRejectReason::SendError, __func__);
}

}  // namespace gfx
}  // namespace mozilla
