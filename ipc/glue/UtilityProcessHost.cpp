/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "UtilityProcessHost.h"

#include "mozilla/dom/ContentParent.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/UtilityProcessManager.h"
#include "mozilla/ipc/UtilityProcessSandboxing.h"
#include "mozilla/Telemetry.h"

#include "chrome/common/process_watcher.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_general.h"

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
#  include "mozilla/Sandbox.h"
#endif

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
#  include "mozilla/SandboxBrokerPolicyFactory.h"
#endif

#if defined(XP_WIN)
#  include "mozilla/WinDllServices.h"
#endif  // defined(XP_WIN)

#if defined(MOZ_WMF_CDM) && defined(MOZ_SANDBOX) && !defined(MOZ_ASAN)
#  define MOZ_WMF_CDM_LPAC_SANDBOX true
#endif

#ifdef MOZ_WMF_CDM_LPAC_SANDBOX
#  include "GMPServiceParent.h"
#  include "mozilla/dom/KeySystemNames.h"
#  include "mozilla/GeckoArgs.h"
#  include "mozilla/MFMediaEngineUtils.h"
#  include "mozilla/StaticPrefs_media.h"
#  include "nsIFile.h"
#  include "sandboxBroker.h"
#endif

#include "ProfilerParent.h"
#include "mozilla/PProfilerChild.h"

namespace mozilla::ipc {

LazyLogModule gUtilityProcessLog("utilityproc");
#define LOGD(...) MOZ_LOG(gUtilityProcessLog, LogLevel::Debug, (__VA_ARGS__))

#ifdef MOZ_WMF_CDM_LPAC_SANDBOX
#  define WMF_LOG(msg, ...)                     \
    MOZ_LOG(gMFMediaEngineLog, LogLevel::Debug, \
            ("UtilityProcessHost=%p, " msg, this, ##__VA_ARGS__))
#endif

UtilityProcessHost::UtilityProcessHost(SandboxingKind aSandbox,
                                       RefPtr<Listener> aListener)
    : GeckoChildProcessHost(GeckoProcessType_Utility),
      mListener(std::move(aListener)),
      mLiveToken(new media::Refcountable<bool>(true)),
      mLaunchPromise(MakeRefPtr<LaunchPromiseType::Private>(__func__)) {
  MOZ_COUNT_CTOR(UtilityProcessHost);
  LOGD("[%p] UtilityProcessHost::UtilityProcessHost sandboxingKind=%" PRIu64,
       this, aSandbox);

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  mDisableOSActivityMode = IsUtilitySandboxEnabled(aSandbox);
#endif
#if defined(MOZ_SANDBOX)
  mUtilitySandbox = aSandbox;
#endif
}

UtilityProcessHost::~UtilityProcessHost() {
  MOZ_COUNT_DTOR(UtilityProcessHost);
#if defined(MOZ_SANDBOX)
  LOGD("[%p] UtilityProcessHost::~UtilityProcessHost sandboxingKind=%" PRIu64,
       this, mUtilitySandbox);
#else
  LOGD("[%p] UtilityProcessHost::~UtilityProcessHost", this);
#endif
}

bool UtilityProcessHost::Launch(geckoargs::ChildProcessArgs aExtraOpts) {
  MOZ_ASSERT(NS_IsMainThread());

  MOZ_ASSERT(mLaunchPhase == LaunchPhase::Unlaunched);
  MOZ_ASSERT(!mUtilityProcessParent);

  LOGD("[%p] UtilityProcessHost::Launch", this);

  mPrefSerializer = MakeUnique<ipc::SharedPreferenceSerializer>();
  if (!mPrefSerializer->SerializeToSharedMemory(GeckoProcessType_Utility,
                                                /* remoteType */ ""_ns)) {
    return false;
  }
  mPrefSerializer->AddSharedPrefCmdLineArgs(*this, aExtraOpts);

#ifdef MOZ_WMF_CDM_LPAC_SANDBOX
  EnsureWidevineL1PathForSandbox(aExtraOpts);
#endif

  mLaunchPhase = LaunchPhase::Waiting;

  if (!GeckoChildProcessHost::AsyncLaunch(std::move(aExtraOpts))) {
    NS_WARNING("UtilityProcess AsyncLaunch failed, aborting.");
    mLaunchPhase = LaunchPhase::Complete;
    mPrefSerializer = nullptr;
    return false;
  }
  LOGD("[%p] UtilityProcessHost::Launch launching async", this);
  return true;
}

RefPtr<UtilityProcessHost::LaunchPromiseType>
UtilityProcessHost::LaunchPromise() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mLaunchPromiseLaunched) {
    return mLaunchPromise;
  }

  WhenProcessHandleReady()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [this, liveToken = mLiveToken](
          const ipc::ProcessHandlePromise::ResolveOrRejectValue& aResult) {
        if (!*liveToken) {
          // The UtilityProcessHost got deleted. Abort. The promise would have
          // already been rejected.
          return;
        }
        if (mLaunchCompleted) {
          return;
        }
        mLaunchCompleted = true;
        if (aResult.IsReject()) {
          RejectPromise(aResult.RejectValue());
        }
        // If aResult.IsResolve() then we have succeeded in launching the
        // Utility process. The promise will be resolved once the channel has
        // connected (or failed to) later.
      });

  mLaunchPromiseLaunched = true;
  return mLaunchPromise;
}

void UtilityProcessHost::OnChannelConnected(base::ProcessId peer_pid) {
  MOZ_ASSERT(!NS_IsMainThread());
  LOGD("[%p] UtilityProcessHost::OnChannelConnected", this);

  GeckoChildProcessHost::OnChannelConnected(peer_pid);

  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "UtilityProcessHost::OnChannelConnected",
      [this, liveToken = mLiveToken]() {
        if (*liveToken && mLaunchPhase == LaunchPhase::Waiting) {
          InitAfterConnect(true);
        }
      }));
}

void UtilityProcessHost::InitAfterConnect(bool aSucceeded) {
  MOZ_ASSERT(NS_IsMainThread());

  MOZ_ASSERT(mLaunchPhase == LaunchPhase::Waiting);
  MOZ_ASSERT(!mUtilityProcessParent);

  // This function is patterned after other `FooProcessHost` functions, but we
  // never actually call it with `false`.
  MOZ_ASSERT(aSucceeded);

  mLaunchPhase = LaunchPhase::Complete;

  mUtilityProcessParent = MakeRefPtr<UtilityProcessParent>(this);
  DebugOnly<bool> rv = TakeInitialEndpoint().Bind(mUtilityProcessParent.get());
  MOZ_ASSERT(rv);

  // Only clear mPrefSerializer in the success case to avoid a
  // possible race in the case case of a timeout on Windows launch.
  // See Bug 1555076 comment 7:
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1555076#c7
  mPrefSerializer = nullptr;

  Maybe<FileDescriptor> brokerFd;

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
  UniquePtr<SandboxBroker::Policy> policy;
  if (IsUtilitySandboxEnabled(mUtilitySandbox)) {
    switch (mUtilitySandbox) {
      case SandboxingKind::GENERIC_UTILITY:
        policy = SandboxBrokerPolicyFactory::GetUtilityProcessPolicy(
            GetActor()->OtherPid());
        break;

#  ifndef ANDROID
      case SandboxingKind::HW_INFERENCE:
        policy = SandboxBrokerPolicyFactory::GetHWInferencePolicy(
            GetActor()->OtherPid());
        break;
#  endif  // !ANDROID

      default:
        MOZ_ASSERT(false, "Invalid SandboxingKind");
        break;
    }
  }
  if (policy != nullptr) {
    brokerFd = Some(FileDescriptor());
    mSandboxBroker = SandboxBroker::Create(
        std::move(policy), GetActor()->OtherPid(), brokerFd.ref());
    // This is unlikely to fail and probably indicates OS resource
    // exhaustion, but we can at least try to recover.
    (void)NS_WARN_IF(mSandboxBroker == nullptr);
    MOZ_ASSERT(brokerFd.ref().IsValid());
  }
#endif  // XP_LINUX && MOZ_SANDBOX

  bool isReadyForBackgroundProcessing = false;
#if defined(XP_WIN)
  RefPtr<DllServices> dllSvc(DllServices::Get());
  isReadyForBackgroundProcessing = dllSvc->IsReadyForBackgroundProcessing();
#endif

  (void)GetActor()->SendInit(brokerFd, Telemetry::CanRecordReleaseData(),
                             isReadyForBackgroundProcessing);

  (void)GetActor()->SendInitProfiler(
      ProfilerParent::CreateForProcess(GetActor()->OtherPid()));

  LOGD("[%p] UtilityProcessHost::InitAfterConnect succeeded", this);

  // Promise will be resolved later, from UtilityProcessParent when the child
  // will send the InitCompleted message.
}

void UtilityProcessHost::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mShutdownRequested);
  LOGD("[%p] UtilityProcessHost::Shutdown", this);

  RejectPromise(LaunchError("aborted by UtilityProcessHost::Shutdown"));

  if (mUtilityProcessParent) {
    LOGD("[%p] UtilityProcessHost::Shutdown not destroying utility process.",
         this);

    // OnChannelClosed uses this to check if the shutdown was expected or
    // unexpected.
    mShutdownRequested = true;

    // The channel might already be closed if we got here unexpectedly.
    if (mUtilityProcessParent->CanSend()) {
      mUtilityProcessParent->Close();
    }

#ifndef NS_FREE_PERMANENT_DATA
    // No need to communicate shutdown, the Utility process doesn't need to
    // communicate anything back.
    KillHard("NormalShutdown");
#endif

    // If we're shutting down unexpectedly, we're in the middle of handling an
    // ActorDestroy for PUtilityProcessParent, which is still on the stack.
    // We'll return back to OnChannelClosed.
    //
    // Otherwise, we'll wait for OnChannelClose to be called whenever
    // PUtilityProcessParent acknowledges shutdown.
    return;
  }

  DestroyProcess();
}

void UtilityProcessHost::OnChannelClosed(
    IProtocol::ActorDestroyReason aReason) {
  MOZ_ASSERT(NS_IsMainThread());
  LOGD("[%p] UtilityProcessHost::OnChannelClosed", this);

  // `aReason` was not originally passed into this function; a value of 0 for
  // the `why` in telemetry means it's from an older build with no info
  RejectPromise(
      LaunchError("UtilityProcessHost::OnChannelClosed", 1 + (long)aReason));

  if (!mShutdownRequested && mListener) {
    // This is an unclean shutdown. Notify our listener that we're going away.
    mListener->OnProcessUnexpectedShutdown(this);
  }

  DestroyProcess();

  // Release the actor.
  UtilityProcessParent::Destroy(std::move(mUtilityProcessParent));
}

void UtilityProcessHost::KillHard(const char* aReason) {
  MOZ_ASSERT(NS_IsMainThread());
  LOGD("[%p] UtilityProcessHost::KillHard", this);

  ProcessHandle handle = GetChildProcessHandle();
  if (!base::KillProcess(handle, base::PROCESS_END_KILLED_BY_USER)) {
    NS_WARNING("failed to kill subprocess!");
  }

  SetAlreadyDead();
}

void UtilityProcessHost::DestroyProcess() {
  MOZ_ASSERT(NS_IsMainThread());
  LOGD("[%p] UtilityProcessHost::DestroyProcess", this);

  RejectPromise(LaunchError("UtilityProcessHost::DestroyProcess"));

  // Any pending tasks will be cancelled from now on.
  *mLiveToken = false;

  NS_DispatchToMainThread(
      NS_NewRunnableFunction("DestroyProcessRunnable", [this] { Destroy(); }));
}

void UtilityProcessHost::ResolvePromise() {
  MOZ_ASSERT(NS_IsMainThread());
  LOGD("[%p] UtilityProcessHost connected - resolving launch promise", this);

  if (!mLaunchPromiseSettled) {
    mLaunchPromise->Resolve(Ok{}, __func__);
    mLaunchPromiseSettled = true;
  }

  mLaunchCompleted = true;
}

void UtilityProcessHost::RejectPromise(LaunchError err) {
  MOZ_ASSERT(NS_IsMainThread());
  LOGD("[%p] UtilityProcessHost connection failed - rejecting launch promise",
       this);

  if (!mLaunchPromiseSettled) {
    mLaunchPromise->Reject(std::move(err), __func__);
    mLaunchPromiseSettled = true;
  }

  mLaunchCompleted = true;
}

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
bool UtilityProcessHost::FillMacSandboxInfo(MacSandboxInfo& aInfo) {
  GeckoChildProcessHost::FillMacSandboxInfo(aInfo);
  if (!aInfo.shouldLog && PR_GetEnv("MOZ_SANDBOX_UTILITY_LOGGING")) {
    aInfo.shouldLog = true;
  }
  return true;
}

/* static */
MacSandboxType UtilityProcessHost::GetMacSandboxType() {
  return MacSandboxType_Utility;
}
#endif

#ifdef MOZ_WMF_CDM_LPAC_SANDBOX
void UtilityProcessHost::EnsureWidevineL1PathForSandbox(
    geckoargs::ChildProcessArgs& aExtraOpts) {
  if (mUtilitySandbox != SandboxingKind::MF_MEDIA_ENGINE_CDM) {
    return;
  }

  RefPtr<mozilla::gmp::GeckoMediaPluginServiceParent> gmps =
      mozilla::gmp::GeckoMediaPluginServiceParent::GetSingleton();
  if (NS_WARN_IF(!gmps)) {
    WMF_LOG("Failed to get GeckoMediaPluginServiceParent!");
    return;
  }

  if (!StaticPrefs::media_eme_widevine_experiment_enabled()) {
    return;
  }

  // If Widevine L1 is installed after the MFCDM process starts, we will set it
  // path later via MFCDMService::UpdateWideivineL1Path().
  nsString widevineL1Path;
  nsCOMPtr<nsIFile> pluginFile;
  if (NS_WARN_IF(NS_FAILED(gmps->FindPluginDirectoryForAPI(
          nsCString(kWidevineExperimentAPIName),
          {nsCString(kWidevineExperimentKeySystemName)},
          getter_AddRefs(pluginFile))))) {
    WMF_LOG("Widevine L1 is not installed yet");
    return;
  }

  if (!pluginFile) {
    WMF_LOG("No plugin file found!");
    return;
  }

  if (NS_WARN_IF(NS_FAILED(pluginFile->GetTarget(widevineL1Path)))) {
    WMF_LOG("Failed to get L1 path!");
    return;
  }

  WMF_LOG("Set Widevine L1 path=%s",
          NS_ConvertUTF16toUTF8(widevineL1Path).get());
  geckoargs::sPluginPath.Put(NS_ConvertUTF16toUTF8(widevineL1Path).get(),
                             aExtraOpts);
  SandboxBroker::EnsureLpacPermsissionsOnDir(widevineL1Path);
}

#  undef WMF_LOG

#endif

}  // namespace mozilla::ipc
