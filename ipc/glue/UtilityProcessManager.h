/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _include_ipc_glue_UtilityProcessManager_h_
#define _include_ipc_glue_UtilityProcessManager_h_
#include "mozilla/MozPromise.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/ipc/UtilityProcessHost.h"
#include "mozilla/EnumeratedArray.h"
#include "mozilla/ProcInfo.h"
#include "mozilla/WeakPtr.h"
#include "nsIAsyncShutdown.h"
#include "nsIObserver.h"
#include "nsTArray.h"

#include "mozilla/PRemoteMediaManagerChild.h"

#if defined(NIGHTLY_BUILD) && !defined(MOZ_NO_SMART_CARDS)
#  include "mozilla/psm/PKCS11ModuleParent.h"
#endif  // NIGHTLY_BUILD && !MOZ_NO_SMART_CARDS

namespace mozilla {

class MemoryReportingProcess;

#ifndef ANDROID
namespace hwinference {
class HWInferenceParent;
}  // namespace hwinference
#endif  // !ANDROID

namespace dom {
class JSOracleParent;
class WindowsUtilsParent;
}  // namespace dom

namespace widget::filedialog {
class ProcessProxy;
}  // namespace widget::filedialog

namespace ipc {

class UtilityProcessParent;
class UtilityProcessKeepAlive;

// The UtilityProcessManager is a singleton responsible for creating
// Utility-bound objects that may live in another process. Currently, it
// provides access to the Utility process via ContentParent.
class UtilityProcessManager final : public UtilityProcessHost::Listener {
  friend class UtilityProcessParent;
  friend class UtilityProcessKeepAlive;

 public:
  template <typename T>
  using LaunchPromise = MozPromise<T, LaunchError, true>;
  template <typename T>
  using SharedLaunchPromise = MozPromise<T, LaunchError, false>;

  using StartRemoteDecodingUtilityPromise =
      LaunchPromise<Endpoint<PRemoteMediaManagerChild>>;
  using JSOraclePromise = GenericNonExclusivePromise;

#ifdef XP_WIN
  using WindowsUtilsPromise = LaunchPromise<RefPtr<dom::WindowsUtilsParent>>;
  using WinFileDialogPromise = LaunchPromise<widget::filedialog::ProcessProxy>;
#endif

#if defined(NIGHTLY_BUILD) && !defined(MOZ_NO_SMART_CARDS)
  using PKCS11ModulePromise = LaunchPromise<RefPtr<psm::PKCS11ModuleParent>>;
#endif  // NIGHTLY_BUILD && !MOZ_NO_SMART_CARDS

  static RefPtr<UtilityProcessManager> GetSingleton();

  static RefPtr<UtilityProcessManager> GetIfExists();

  // Launch the shared Utility process for aSandbox asynchronously.
  RefPtr<SharedLaunchPromise<Ok>> LaunchProcess(SandboxingKind aSandbox);

  // Launch a new independent Utility process for aSandbox. This will always
  // launch a new process. The process will be shut down when the returned
  // keep-alive goes away. Returns nullptr if the launch failed outright. Main
  // thread only.
  already_AddRefed<UtilityProcessKeepAlive> LaunchIndependentProcess(
      SandboxingKind aSandbox);

  template <typename Actor>
  RefPtr<LaunchPromise<Ok>> StartUtility(RefPtr<Actor> aActor,
                                         SandboxingKind aSandbox);

  RefPtr<StartRemoteDecodingUtilityPromise> StartProcessForRemoteMediaDecoding(
      EndpointProcInfo aOtherProcess, dom::ContentParentId aChildId,
      SandboxingKind aSandbox);

  RefPtr<JSOraclePromise> StartJSOracle(mozilla::dom::JSOracleParent* aParent);

#ifdef XP_WIN
  // Get the (possibly already resolved) promise for the Windows utility
  // process actor.  Creates the process if it is not running.
  RefPtr<WindowsUtilsPromise> GetWindowsUtilsPromise();
  // Releases the WindowsUtils actor so that it can be destroyed.
  // Subsequent attempts to use WindowsUtils will create a new process.
  void ReleaseWindowsUtils();

  // Get a new Windows file-dialog utility-process actor. These are never
  // reused; this will always return a fresh actor.
  RefPtr<WinFileDialogPromise> CreateWinFileDialogActor();
#endif

#if defined(NIGHTLY_BUILD) && !defined(MOZ_NO_SMART_CARDS)
  RefPtr<PKCS11ModulePromise> StartPKCS11Module();
#endif  // NIGHTLY_BUILD && !MOZ_NO_SMART_CARDS

#ifndef ANDROID
  // Launches an independent HWInference process and binds aActor to it.
  already_AddRefed<UtilityProcessKeepAlive> LaunchIndependentHWInferenceProcess(
      RefPtr<hwinference::HWInferenceParent> aActor);
#endif  // !ANDROID

  void OnProcessUnexpectedShutdown(UtilityProcessHost* aHost);

  // Returns the platform pid for this utility sandbox process.
  Maybe<base::ProcessId> ProcessPid(SandboxingKind aSandbox);

  RefPtr<UtilityProcessKeepAlive> GetSharedKeepAlive(SandboxingKind aSandbox);

  // Create a MemoryReportingProcess object for this utility process
  RefPtr<MemoryReportingProcess> GetProcessMemoryReporter(
      UtilityProcessParent* parent);

  // Get a list of all valid utility process parent references
  nsTArray<RefPtr<UtilityProcessParent>> GetAllProcessesProcessParent() {
    nsTArray<RefPtr<UtilityProcessParent>> rv;
    for (auto& p : mProcesses) {
      if (p->mProcessParent) {
        rv.AppendElement(p->mProcessParent);
      }
    }
    return rv;
  }

  void RegisterActor(const RefPtr<UtilityProcessParent>& aParent,
                     UtilityActorName aActorName);

  Span<const UtilityActorName> GetActors(
      const RefPtr<UtilityProcessParent>& aParent) {
    for (auto& p : mProcesses) {
      if (p->mProcessParent && p->mProcessParent == aParent) {
        return p->mActors;
      }
    }
    return {};
  }

  Span<const UtilityActorName> GetActors(GeckoChildProcessHost* aHost) {
    for (auto& p : mProcesses) {
      if (p->mProcess == aHost) {
        return p->mActors;
      }
    }
    return {};
  }

  // Shutdown the shared Utility process for that sandbox.
  void CleanShutdown(SandboxingKind aSandbox);

  // Shutdown all utility processes
  void CleanShutdownAllProcesses();

  size_t AliveProcesses();

 private:
  ~UtilityProcessManager();

  // Called from our async shutdown blocker. Tears the Utility processes down,
  // holding the xpcom-will-shutdown phase open until they are all gone.
  void OnXPCOMWillShutdown();

  // Called from our xpcom-shutdown observer. Only does anything if we failed to
  // register the async shutdown blocker.
  void OnXPCOMShutdown();
  void OnPreferenceChange(const char16_t* aData);

  // Called once a UtilityProcessHost has completed its shutdown sequence.
  void OnProcessShutdownComplete();

  void RegisterShutdownBlocker();
  void RemoveShutdownBlocker();

  UtilityProcessManager();

  void Init();

  bool IsShutdown() const;

  class Observer final : public nsIObserver {
   public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOBSERVER
    explicit Observer(UtilityProcessManager* aManager);

   protected:
    ~Observer() = default;

    RefPtr<UtilityProcessManager> mManager;
  };
  friend class Observer;

  RefPtr<Observer> mObserver;

  // Holds the xpcom-will-shutdown phase open while the Utility processes go
  // through their graceful shutdown sequence, so that the main thread's IPC
  // channels are still around when they send us their last messages.
  class ShutdownBlocker final : public nsIAsyncShutdownBlocker {
   public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIASYNCSHUTDOWNBLOCKER

    explicit ShutdownBlocker(UtilityProcessManager* aManager)
        : mManager(aManager) {}

   protected:
    ~ShutdownBlocker() = default;

    RefPtr<UtilityProcessManager> mManager;
  };

  RefPtr<ShutdownBlocker> mShutdownBlocker;
  nsCOMPtr<nsIAsyncShutdownClient> mShutdownBlockerClient;

  // Number of UtilityProcessHosts whose shutdown sequence is still in flight.
  uint32_t mPendingShutdowns = 0;

  // Whether the xpcom-will-shutdown phase is waiting on our blocker. From then
  // on it must be removed as soon as mPendingShutdowns drains, or we hang.
  bool mBlockingShutdownPhase = false;

  class ProcessFields final {
   public:
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ProcessFields);

    explicit ProcessFields(SandboxingKind aSandbox) : mSandbox(aSandbox) {};

    // Promise will be resolved when this Utility process has been fully started
    // and configured. Only accessed on the main thread.
    RefPtr<SharedLaunchPromise<Ok>> mLaunchPromise;

    // Fields that are associated with the current Utility process.
    RefPtr<UtilityProcessHost> mProcess = nullptr;
    RefPtr<UtilityProcessParent> mProcessParent = nullptr;

    // Collects any pref changes that occur during process launch (after
    // the initial map is passed in command-line arguments) to be sent
    // when the process can receive IPC messages.
    nsTArray<dom::Pref> mQueuedPrefs;

    nsTArray<UtilityActorName> mActors;

    SandboxingKind mSandbox = SandboxingKind::COUNT;

   protected:
    ~ProcessFields() = default;
  };

  void DestroyProcess(ProcessFields* aProcess);

  nsTArray<RefPtr<ProcessFields>> mProcesses;

  EnumeratedArray<SandboxingKind, RefPtr<UtilityProcessKeepAlive>,
                  size_t(SandboxingKind::COUNT)>
      mSharedKeepAlives;

  // Core of both StartUtility() entry points: binds aActor to aProcess once
  // aLaunchPromise resolves. The launch promise is passed in rather than read
  // off aProcess because LaunchProcess() can reject without storing one there.
  // If the launch fails while aActor is still unbound, this calls
  // aActor->BindToUtilityProcess(nullptr) to let it know.
  template <typename Actor>
  RefPtr<LaunchPromise<Ok>> StartUtilityOnProcess(
      RefPtr<Actor> aActor, ProcessFields* aProcess,
      SharedLaunchPromise<Ok>* aLaunchPromise);

#ifdef XP_WIN
  RefPtr<dom::WindowsUtilsParent> mWindowsUtils;
#endif  // XP_WIN
};

// Holds the utility process it was acquired on, and shuts that process down
// once the last reference on it goes away. Handed out by
// UtilityProcessManager::LaunchIndependentProcess(). Main thread only.
class UtilityProcessKeepAlive final : public SupportsWeakPtr {
 public:
  NS_INLINE_DECL_REFCOUNTING(UtilityProcessKeepAlive);

  // Resolves once the process this was acquired on has finished launching.
  RefPtr<UtilityProcessManager::SharedLaunchPromise<Ok>> GetLaunchPromise()
      const;

  // The process' PUtilityProcess actor: null until it has launched, and again
  // once it is gone.
  RefPtr<UtilityProcessParent> GetProcessParent() const;

  // False once the process has been torn down, whether or not that was
  // expected. This keep-alive is then inert.
  bool IsAlive() const { return !!mProcess->mProcess; }

 private:
  friend class UtilityProcessManager;

  explicit UtilityProcessKeepAlive(
      UtilityProcessManager::ProcessFields* aProcess);
  ~UtilityProcessKeepAlive();

  const RefPtr<UtilityProcessManager::ProcessFields> mProcess;
};

}  // namespace ipc

}  // namespace mozilla

#endif  // _include_ipc_glue_UtilityProcessManager_h_
