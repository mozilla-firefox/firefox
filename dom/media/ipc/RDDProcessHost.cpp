/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "RDDProcessHost.h"

#include "RDDChild.h"
#include "chrome/common/process_watcher.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/ipc/ProcessUtils.h"

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
#  include "mozilla/Sandbox.h"
#endif

namespace mozilla {

using namespace ipc;

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
bool RDDProcessHost::sLaunchWithMacSandbox = false;
#endif

RDDProcessHost::RDDProcessHost(Listener* aListener)
    : GeckoChildProcessHost(GeckoProcessType_RDD), mListener(aListener) {
  MOZ_COUNT_CTOR(RDDProcessHost);

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  if (!sLaunchWithMacSandbox) {
    sLaunchWithMacSandbox = (PR_GetEnv("MOZ_DISABLE_RDD_SANDBOX") == nullptr);
  }
  mDisableOSActivityMode = sLaunchWithMacSandbox;
#endif
}

RDDProcessHost::~RDDProcessHost() { MOZ_COUNT_DTOR(RDDProcessHost); }

bool RDDProcessHost::Launch(geckoargs::ChildProcessArgs aExtraOpts) {
  MOZ_ASSERT(NS_IsMainThread());

  MOZ_ASSERT(mLaunchPhase == LaunchPhase::Unlaunched);
  MOZ_ASSERT(!mRDDChild);

  mPrefSerializer = std::make_unique<ipc::SharedPreferenceSerializer>();
  if (!mPrefSerializer->SerializeToSharedMemory(GeckoProcessType_RDD,
                                                /* remoteType */ {})) {
    return false;
  }
  mPrefSerializer->AddSharedPrefCmdLineArgs(*this, aExtraOpts);

  mLaunchPhase = LaunchPhase::Waiting;
  mLaunchTime = TimeStamp::Now();

  int32_t timeoutMs = StaticPrefs::media_rdd_process_startup_timeout_ms();

  // If one of the following environment variables are set we can
  // effectively ignore the timeout - as we can guarantee the RDD
  // process will be terminated
  if (PR_GetEnv("MOZ_DEBUG_CHILD_PROCESS") ||
      PR_GetEnv("MOZ_DEBUG_CHILD_PAUSE")) {
    timeoutMs = 0;
  }
  if (timeoutMs) {
    // We queue a delayed task. If that task runs before the
    // WhenProcessHandleReady promise gets resolved, we will abort the launch.
    GetMainThreadSerialEventTarget()->DelayedDispatch(
        NS_NewRunnableFunction(
            "RDDProcessHost::Launchtimeout",
            [weakSelf = ThreadSafeWeakPtr<RDDProcessHost>{this}]() {
              RefPtr<RDDProcessHost> self(weakSelf);
              if (!self || self->mShutdownRequested || self->mTimerChecked) {
                // We have been deleted or the runnable has already started, we
                // can abort.
                return;
              }
              self->InitAfterConnect(false);
              MOZ_ASSERT(self->mTimerChecked,
                         "InitAfterConnect must have acted on the promise");
            }),
        timeoutMs);
  }

  if (!GeckoChildProcessHost::AsyncLaunch(std::move(aExtraOpts))) {
    mLaunchPhase = LaunchPhase::Complete;
    mPrefSerializer = nullptr;
    return false;
  }
  return true;
}

RefPtr<GenericNonExclusivePromise> RDDProcessHost::LaunchPromise() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mLaunchPromise) {
    return mLaunchPromise;
  }
  mLaunchPromise = MakeRefPtr<GenericNonExclusivePromise::Private>(__func__);
  WhenProcessHandleReady()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [this, self = RefPtr{this}](
          const ipc::ProcessHandlePromise::ResolveOrRejectValue& aResult) {
        if (mShutdownRequested) {
          // The RDDProcessHost is shutting down. Abort. The promise would have
          // already been rejected.
          return;
        }
        if (mTimerChecked) {
          // We hit the timeout earlier, abort.
          return;
        }
        mTimerChecked = true;
        if (aResult.IsReject()) {
          RejectPromise();
        }
        // If aResult.IsResolve() then we have succeeded in launching the
        // RDD process. The promise will be resolved once the channel has
        // connected (or failed to) later.
      });
  return mLaunchPromise;
}

void RDDProcessHost::OnChannelConnected(base::ProcessId peer_pid) {
  MOZ_ASSERT(!NS_IsMainThread());

  GeckoChildProcessHost::OnChannelConnected(peer_pid);

  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "RDDProcessHost::OnChannelConnected", [this, self = RefPtr{this}]() {
        if (!mShutdownRequested && mLaunchPhase == LaunchPhase::Waiting) {
          InitAfterConnect(true);
        }
      }));
}

static uint64_t sRDDProcessTokenCounter = 0;

void RDDProcessHost::InitAfterConnect(bool aSucceeded) {
  MOZ_ASSERT(NS_IsMainThread());

  MOZ_ASSERT(mLaunchPhase == LaunchPhase::Waiting);
  MOZ_ASSERT(!mRDDChild);

  mLaunchPhase = LaunchPhase::Complete;

  if (!aSucceeded) {
    RejectPromise();
    return;
  }
  mProcessToken = ++sRDDProcessTokenCounter;
  mRDDChild = MakeRefPtr<RDDChild>(this);
  DebugOnly<bool> rv = TakeInitialEndpoint().Bind(mRDDChild.get());
  MOZ_ASSERT(rv);

  // Only clear mPrefSerializer in the success case to avoid a
  // possible race in the case case of a timeout on Windows launch.
  // See Bug 1555076 comment 7:
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1555076#c7
  mPrefSerializer = nullptr;

  if (!mRDDChild->Init()) {
    // Can't just kill here because it will create a timing race that
    // will crash the tab. We don't really want to crash the tab just
    // because RDD linux sandbox failed to initialize.  In this case,
    // we'll close the child channel which will cause the RDD process
    // to shutdown nicely avoiding the tab crash (which manifests as
    // Bug 1535335).
    mRDDChild->Close();
    RejectPromise();
  } else {
    ResolvePromise();
  }
}

void RDDProcessHost::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mShutdownRequested);

  RejectPromise();
  mShutdownRequested = true;

  if (mRDDChild) {
    // The channel might already be closed if we got here unexpectedly.
    if (!mChannelClosed) {
      mRDDChild->Close();
      MOZ_ASSERT(!mRDDChild);
    }

#ifndef NS_FREE_PERMANENT_DATA
    // No need to communicate shutdown, the RDD process doesn't need to
    // communicate anything back.
    KillHard("NormalShutdown");
#endif
  }
}

void RDDProcessHost::OnChannelClosed() {
  MOZ_ASSERT(NS_IsMainThread());

  mChannelClosed = true;
  RejectPromise();

  if (!mShutdownRequested && mListener) {
    // This is an unclean shutdown. Notify our listener that we're going away.
    mListener->OnProcessUnexpectedShutdown(this);
  }

  // Release the actor.
  RDDChild::Destroy(std::move(mRDDChild));
}

void RDDProcessHost::KillHard(const char* aReason) {
  MOZ_ASSERT(NS_IsMainThread());

  ProcessHandle handle = GetChildProcessHandle();
  if (!base::KillProcess(handle, base::PROCESS_END_KILLED_BY_USER)) {
    NS_WARNING("failed to kill subprocess!");
  }

  SetAlreadyDead();
}

uint64_t RDDProcessHost::GetProcessToken() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mProcessToken;
}

void RDDProcessHost::ResolvePromise() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mLaunchPromise && !mLaunchPromiseSettled) {
    mLaunchPromise->Resolve(true, __func__);
    mLaunchPromiseSettled = true;
  }
  // We have already acted on the promise; the timeout runnable no longer needs
  // to interrupt anything.
  mTimerChecked = true;
}

void RDDProcessHost::RejectPromise() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mLaunchPromise && !mLaunchPromiseSettled) {
    mLaunchPromise->Reject(NS_ERROR_FAILURE, __func__);
    mLaunchPromiseSettled = true;
  }
  // We have already acted on the promise; the timeout runnable no longer needs
  // to interrupt anything.
  mTimerChecked = true;
}

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
bool RDDProcessHost::FillMacSandboxInfo(MacSandboxInfo& aInfo) {
  GeckoChildProcessHost::FillMacSandboxInfo(aInfo);
  if (!aInfo.shouldLog && PR_GetEnv("MOZ_SANDBOX_RDD_LOGGING")) {
    aInfo.shouldLog = true;
  }
  return true;
}

/* static */
MacSandboxType RDDProcessHost::GetMacSandboxType() {
  return MacSandboxType_RDD;
}
#endif

}  // namespace mozilla
