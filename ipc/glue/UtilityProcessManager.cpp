/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "UtilityProcessManager.h"

#include "JSOracleParent.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/ipc/UtilityProcessHost.h"
#include "mozilla/MemoryReportingProcess.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/SyncRunnable.h"  // for LaunchUtilityProcess
#ifndef ANDROID
#  include "mozilla/hwinference/HWInferenceParent.h"
#endif  // !ANDROID
#include "mozilla/ipc/UtilityProcessParent.h"
#include "mozilla/ipc/UtilityMediaServiceChild.h"
#include "mozilla/ipc/UtilityMediaServiceParent.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/UtilityProcessSandboxing.h"
#include "mozilla/ipc/ProcessChild.h"
#include "nsAppRunner.h"
#include "nsContentUtils.h"

#ifdef XP_WIN
#  include "mozilla/dom/WindowsUtilsParent.h"
#  include "mozilla/widget/filedialog/WinFileDialogParent.h"
#endif

#include "mozilla/GeckoArgs.h"

#if defined(NIGHTLY_BUILD) && !defined(MOZ_NO_SMART_CARDS)
#  include "mozilla/psm/PPKCS11ModuleChild.h"
#endif  // NIGHTLY_BUILD && !MOZ_NO_SMART_CARDS

namespace mozilla::ipc {

extern LazyLogModule gUtilityProcessLog;
#define LOGD(...) MOZ_LOG(gUtilityProcessLog, LogLevel::Debug, (__VA_ARGS__))

static StaticRefPtr<UtilityProcessManager> sSingleton;

// We tear the Utility processes down at the beginning of XPCOMWillShutdown, so
// from that point on there is no point in handing out or launching any.
static bool IsPastLaunchDeadline() {
  return AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMWillShutdown);
}

bool UtilityProcessManager::IsShutdown() const {
  MOZ_ASSERT(NS_IsMainThread());
  return IsPastLaunchDeadline() || !sSingleton;
}

RefPtr<UtilityProcessManager> UtilityProcessManager::GetSingleton() {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  if (!IsPastLaunchDeadline() && sSingleton == nullptr) {
    sSingleton = new UtilityProcessManager();
    sSingleton->Init();
  }
  return sSingleton;
}

RefPtr<UtilityProcessManager> UtilityProcessManager::GetIfExists() {
  MOZ_ASSERT(NS_IsMainThread());
  return sSingleton;
}

UtilityProcessManager::UtilityProcessManager() {
  LOGD("[%p] UtilityProcessManager::UtilityProcessManager", this);
}

void UtilityProcessManager::Init() {
  // Start listening for pref changes so we can
  // forward them to the process once it is running.
  mObserver = new Observer(this);
  nsContentUtils::RegisterShutdownObserver(mObserver);
  Preferences::AddStrongObserver(mObserver, "");

  RegisterShutdownBlocker();
}

void UtilityProcessManager::RegisterShutdownBlocker() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mShutdownBlocker);

  nsCOMPtr<nsIAsyncShutdownService> svc = components::AsyncShutdown::Service();
  if (!svc) {
    return;
  }

  nsCOMPtr<nsIAsyncShutdownClient> client;
  if (NS_FAILED(svc->GetXpcomWillShutdown(getter_AddRefs(client))) || !client) {
    return;
  }

  RefPtr<ShutdownBlocker> blocker = new ShutdownBlocker(this);
  if (NS_FAILED(client->AddBlocker(blocker,
                                   NS_LITERAL_STRING_FROM_CSTRING(__FILE__),
                                   __LINE__, u""_ns))) {
    // The phase is already closed for new blockers, so there is nothing left
    // for us to hold back. OnXPCOMShutdown will do the teardown instead.
    return;
  }

  mShutdownBlocker = std::move(blocker);
  mShutdownBlockerClient = std::move(client);
}

void UtilityProcessManager::RemoveShutdownBlocker() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mShutdownBlocker) {
    return;
  }

  LOGD("[%p] UtilityProcessManager::RemoveShutdownBlocker", this);

  mShutdownBlockerClient->RemoveBlocker(mShutdownBlocker);
  mShutdownBlockerClient = nullptr;
  mShutdownBlocker = nullptr;
}

NS_IMPL_ISUPPORTS(UtilityProcessManager::ShutdownBlocker,
                  nsIAsyncShutdownBlocker)

NS_IMETHODIMP UtilityProcessManager::ShutdownBlocker::GetName(
    nsAString& aName) {
  aName = u"UtilityProcessManager: shutting down the Utility processes"_ns;
  return NS_OK;
}

NS_IMETHODIMP UtilityProcessManager::ShutdownBlocker::BlockShutdown(
    nsIAsyncShutdownClient* /*aBarrierClient*/) {
  if (mManager) {
    mManager->OnXPCOMWillShutdown();
  }
  return NS_OK;
}

NS_IMETHODIMP UtilityProcessManager::ShutdownBlocker::GetState(
    nsIPropertyBag** aState) {
  *aState = nullptr;
  return NS_OK;
}

UtilityProcessManager::~UtilityProcessManager() {
  LOGD("[%p] UtilityProcessManager::~UtilityProcessManager", this);

  // The Utility process should ALL have already been shut down.
  MOZ_ASSERT(mProcesses.IsEmpty());
}

NS_IMPL_ISUPPORTS(UtilityProcessManager::Observer, nsIObserver);

UtilityProcessManager::Observer::Observer(UtilityProcessManager* aManager)
    : mManager(aManager) {}

NS_IMETHODIMP
UtilityProcessManager::Observer::Observe(nsISupports* /*aSubject*/,
                                         const char* aTopic,
                                         const char16_t* aData) {
  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    mManager->OnXPCOMShutdown();
  } else if (!strcmp(aTopic, "nsPref:changed")) {
    mManager->OnPreferenceChange(aData);
  }
  return NS_OK;
}

void UtilityProcessManager::OnXPCOMWillShutdown() {
  LOGD("[%p] UtilityProcessManager::OnXPCOMWillShutdown", this);

  MOZ_ASSERT(NS_IsMainThread());

  mBlockingShutdownPhase = true;

  CleanShutdownAllProcesses();

  // Nothing to wait for; otherwise OnProcessShutdownComplete removes the
  // blocker once every host is done.
  if (mPendingShutdowns == 0) {
    RemoveShutdownBlocker();
  }
}

void UtilityProcessManager::OnXPCOMShutdown() {
  LOGD("[%p] UtilityProcessManager::OnXPCOMShutdown", this);

  MOZ_ASSERT(NS_IsMainThread());
  if (mObserver) {
    nsContentUtils::UnregisterShutdownObserver(mObserver);
  }

  // Normally a no-op, since our blocker already did this during
  // xpcom-will-shutdown. If we failed to register it, this is the last chance.
  CleanShutdownAllProcesses();
}

void UtilityProcessManager::OnProcessShutdownComplete() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(mPendingShutdowns > 0);

  --mPendingShutdowns;
  if (mPendingShutdowns > 0) {
    return;
  }

  // If we're already within shutdown, ensure our async shutdown blocker is gone
  // now that all processes have shut down.
  if (mBlockingShutdownPhase) {
    MOZ_ASSERT(mProcesses.IsEmpty(),
               "How was a process launched during shutdown?");
    RemoveShutdownBlocker();
  }
}

void UtilityProcessManager::OnPreferenceChange(const char16_t* aData) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mProcesses.IsEmpty()) {
    // Process hasn't been launched yet
    return;
  }
  // We know prefs are ASCII here.
  NS_LossyConvertUTF16toASCII strData(aData);

  mozilla::dom::Pref pref(strData, /* isLocked */ false,
                          /* isSanitized */ false, Nothing(), Nothing());
  Preferences::GetPreference(&pref, GeckoProcessType_Utility,
                             /* remoteType */ {});

  for (auto& p : mProcesses) {
    if (p->mProcessParent) {
      (void)p->mProcessParent->SendPreferenceUpdate(pref);
    } else {
      p->mQueuedPrefs.AppendElement(pref);
    }
  }
}

void UtilityProcessManager::RegisterActor(
    const RefPtr<UtilityProcessParent>& aParent, UtilityActorName aActorName) {
  for (auto& p : mProcesses) {
    if (p->mProcessParent && p->mProcessParent == aParent) {
      MOZ_LOG(
          gChildProcessLifecycleLog, LogLevel::Info,
          ("UTILITYACTOR [childID = %" PRIi32 "] [actorName = %s]",
           p->mProcess->GetChildID(), dom::GetEnumString(aActorName).get()));
      p->mActors.AppendElement(aActorName);
      return;
    }
  }
}

RefPtr<UtilityProcessKeepAlive> UtilityProcessManager::GetSharedKeepAlive(
    SandboxingKind aSandbox) {
  MOZ_ASSERT(NS_IsMainThread());
  return mSharedKeepAlives[aSandbox];
}

RefPtr<UtilityProcessManager::SharedLaunchPromise<Ok>>
UtilityProcessManager::LaunchProcess(SandboxingKind aSandbox) {
  LOGD("[%p] UtilityProcessManager::LaunchProcess SandboxingKind=%" PRIu64,
       this, aSandbox);
  using RetPromise = SharedLaunchPromise<Ok>;

  MOZ_ASSERT(NS_IsMainThread());

  if (IsShutdown()) {
    NS_WARNING("Reject early LaunchProcess() for Shutdown");
    return RetPromise::CreateAndReject(
        LaunchError("UPM::LaunchProcess(): IsShutdown()"), __func__);
  }

  RefPtr<UtilityProcessKeepAlive>& keepAlive = mSharedKeepAlives[aSandbox];
  if (!keepAlive) {
    keepAlive = LaunchIndependentProcess(aSandbox);
    if (!keepAlive) {
      NS_WARNING("Reject LaunchProcess() for launch failure");
      return RetPromise::CreateAndReject(
          LaunchError("UPM::LaunchProcess(): launch failure"), __func__);
    }
  }

  // DestroyProcess drops the shared keep-alive along with its process.
  MOZ_ASSERT(keepAlive->IsAlive());
  return keepAlive->GetLaunchPromise();
}

already_AddRefed<UtilityProcessKeepAlive>
UtilityProcessManager::LaunchIndependentProcess(SandboxingKind aSandbox) {
  LOGD(
      "[%p] UtilityProcessManager::LaunchIndependentProcess "
      "SandboxingKind=%" PRIu64,
      this, aSandbox);

  MOZ_ASSERT(NS_IsMainThread());

  if (IsShutdown()) {
    NS_WARNING("Reject early LaunchIndependentProcess() for Shutdown");
    return nullptr;
  }

  using RetPromise = SharedLaunchPromise<Ok>;

  RefPtr<ProcessFields> p = new ProcessFields(aSandbox);
  RefPtr<UtilityProcessKeepAlive> keepAlive = new UtilityProcessKeepAlive(p);

  geckoargs::ChildProcessArgs extraArgs;
  ProcessChild::AddPlatformBuildID(extraArgs);
  geckoargs::sSandboxingKind.Put(aSandbox, extraArgs);

  // The subprocess is launched asynchronously, so we
  // wait for the promise to be resolved to acquire the IPDL actor.
  p->mProcess = new UtilityProcessHost(aSandbox, this);
  mProcesses.AppendElement(p);
  if (!p->mProcess->Launch(std::move(extraArgs))) {
    NS_WARNING("Utility process failed to launch");
    DestroyProcess(p);
    return nullptr;
  }

  RefPtr<UtilityProcessManager> self = this;
  p->mLaunchPromise = p->mProcess->LaunchPromise()->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [self, p](Ok) -> RefPtr<RetPromise> {
        if (self->IsShutdown()) {
          NS_WARNING("Reject launch after LaunchPromise() for Shutdown");
          return RetPromise::CreateAndReject(
              LaunchError(
                  "UPM::LaunchIndependentProcess(): post-await IsShutdown()"),
              __func__);
        }

        if (!p->mProcess) {
          NS_WARNING(
              "Reject launch after LaunchPromise() for destroyed process");
          return RetPromise::CreateAndReject(
              LaunchError("UPM::LaunchIndependentProcess(): post-await "
                          "destroyed process"),
              __func__);
        }

        p->mProcessParent = p->mProcess->GetActor();

        // Flush any pref updates that happened during
        // launch and weren't included in the blobs set
        // up in LaunchUtilityProcess.
        for (const mozilla::dom::Pref& pref : p->mQueuedPrefs) {
          (void)NS_WARN_IF(!p->mProcessParent->SendPreferenceUpdate(pref));
        }
        p->mQueuedPrefs.Clear();

        CrashReporter::RecordAnnotationCString(
            CrashReporter::Annotation::UtilityProcessStatus, "Running");

        return RetPromise::CreateAndResolve(Ok{}, __func__);
      },
      [self, p](LaunchError error) {
        self->DestroyProcess(p);
        NS_WARNING("Reject launch for LaunchPromise() rejection");
        return RetPromise::CreateAndReject(std::move(error), __func__);
      });

  return keepAlive.forget();
}

#ifndef ANDROID
already_AddRefed<UtilityProcessKeepAlive>
UtilityProcessManager::LaunchIndependentHWInferenceProcess(
    RefPtr<hwinference::HWInferenceParent> aActor) {
  RefPtr<UtilityProcessKeepAlive> keepAlive =
      LaunchIndependentProcess(SandboxingKind::HW_INFERENCE);
  if (keepAlive) {
    StartUtilityOnProcess(std::move(aActor), keepAlive->mProcess,
                          keepAlive->mProcess->mLaunchPromise);
  }
  return keepAlive.forget();
}
#endif  // !ANDROID

// Lets an actor that never got bound respond to the launch failing.
template <typename Actor>
static void NotifyLaunchFailed(Actor* aActor) {
  if (!aActor->CanSend()) {
    (void)aActor->BindToUtilityProcess(nullptr);
  }
}

template <typename Actor>
RefPtr<UtilityProcessManager::LaunchPromise<Ok>>
UtilityProcessManager::StartUtility(RefPtr<Actor> aActor,
                                    SandboxingKind aSandbox) {
  using RetPromise = LaunchPromise<Ok>;

  LOGD(
      "[%p] UtilityProcessManager::StartUtility actor=%p "
      "SandboxingKind=%" PRIu64,
      this, aActor.get(), aSandbox);

  if (!aActor) {
    MOZ_ASSERT(false, "Actor singleton failure");
    return RetPromise::CreateAndReject(
        LaunchError("UPM::StartUtility: aActor is null"), __func__);
  }

  if (aActor->CanSend()) {
    // Actor has already been setup, so we:
    //   - know the process has been launched
    //   - the ipc actors are ready
    PROFILER_MARKER_TEXT(
        "UtilityProcessManager::StartUtility", IPC,
        MarkerOptions(MarkerTiming::InstantNow()),
        nsPrintfCString("SandboxingKind=%" PRIu64 " aActor->CanSend()",
                        aSandbox));
    return RetPromise::CreateAndResolve(Ok{}, __func__);
  }

  RefPtr<SharedLaunchPromise<Ok>> launchPromise = LaunchProcess(aSandbox);
  RefPtr<UtilityProcessKeepAlive> keepAlive = GetSharedKeepAlive(aSandbox);
  if (!keepAlive) {
    // LaunchProcess() failed outright and dropped the process.
    NS_WARNING("Reject StartUtility() for LaunchProcess() failure");
    NotifyLaunchFailed(aActor.get());
    return RetPromise::CreateAndReject(
        LaunchError("UPM::StartUtility: LaunchProcess()"), __func__);
  }

  return StartUtilityOnProcess(std::move(aActor), keepAlive->mProcess,
                               launchPromise);
}

template <typename Actor>
RefPtr<UtilityProcessManager::LaunchPromise<Ok>>
UtilityProcessManager::StartUtilityOnProcess(
    RefPtr<Actor> aActor, ProcessFields* aProcess,
    SharedLaunchPromise<Ok>* aLaunchPromise) {
  using RetPromise = LaunchPromise<Ok>;

  MOZ_ASSERT(NS_IsMainThread());

  TimeStamp utilityStart = TimeStamp::Now();
  SandboxingKind sandbox = aProcess->mSandbox;

  RefPtr<UtilityProcessManager> self = this;
  RefPtr<ProcessFields> process = aProcess;
  return aLaunchPromise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [self, aActor, sandbox, process, utilityStart]() -> RefPtr<RetPromise> {
        RefPtr<UtilityProcessParent> utilityParent = process->mProcessParent;
        if (!utilityParent) {
          NS_WARNING("Missing parent in StartUtility");
          NotifyLaunchFailed(aActor.get());
          return RetPromise::CreateAndReject(
              LaunchError("UPM::GetProcessParent"), __func__);
        }

        // It is possible if multiple processes concurrently request a utility
        // actor that the previous CanSend() check returned false for both but
        // that by the time we have started our process for real, one of them
        // has already been able to establish the IPC connection and thus we
        // would perform more than one Open() call.
        //
        // The tests within browser_utility_multipleAudio.js should be able to
        // catch that behavior.
        if (!aActor->CanSend()) {
          if (!utilityParent->CanSend()) {
            NS_WARNING("Utility process died before IPC could be established");
            NotifyLaunchFailed(aActor.get());
            return RetPromise::CreateAndReject(
                LaunchError("UPM::UtilityParent died"), __func__);
          }

          nsresult rv = aActor->BindToUtilityProcess(utilityParent);
          if (NS_FAILED(rv)) {
            LOGD("BindToUtilityProcess failed with rv=%x",
                 static_cast<uint32_t>(rv));
            MOZ_ASSERT(false, "Protocol endpoints failure");
            return RetPromise::CreateAndReject(
                LaunchError("BindToUtilityProcess", rv), __func__);
          }

          MOZ_DIAGNOSTIC_ASSERT(aActor->CanSend(), "IPC established for actor");
          self->RegisterActor(utilityParent, aActor->GetActorName());
        }

        PROFILER_MARKER_TEXT(
            "UtilityProcessManager::StartUtility", IPC,
            MarkerOptions(MarkerTiming::IntervalUntilNowFrom(utilityStart)),
            nsPrintfCString("SandboxingKind=%" PRIu64 " Resolve", sandbox));
        return RetPromise::CreateAndResolve(Ok{}, __func__);
      },
      [self, aActor, sandbox, utilityStart](LaunchError const& error) {
        NS_WARNING("Reject StartUtility() for LaunchProcess() rejection");
        NotifyLaunchFailed(aActor.get());
        if (!self->IsShutdown()) {
          NS_WARNING("Reject StartUtility() when !IsShutdown()");
        }
        PROFILER_MARKER_TEXT(
            "UtilityProcessManager::StartUtility", IPC,
            MarkerOptions(MarkerTiming::IntervalUntilNowFrom(utilityStart)),
            nsPrintfCString("SandboxingKind=%" PRIu64 " Reject", sandbox));
        return RetPromise::CreateAndReject(error, __func__);
      });
}

RefPtr<UtilityProcessManager::StartRemoteDecodingUtilityPromise>
UtilityProcessManager::StartProcessForRemoteMediaDecoding(
    EndpointProcInfo aOtherProcess, dom::ContentParentId aChildId,
    SandboxingKind aSandbox) {
  using RetPromise = StartRemoteDecodingUtilityPromise;

  // Not supported kinds.
  if (aSandbox != SandboxingKind::GENERIC_UTILITY
#ifdef MOZ_APPLEMEDIA
      && aSandbox != SandboxingKind::UTILITY_AUDIO_DECODING_APPLE_MEDIA
#endif
#ifdef XP_WIN
      && aSandbox != SandboxingKind::UTILITY_AUDIO_DECODING_WMF
#endif
#ifdef MOZ_WMF_MEDIA_ENGINE
      && aSandbox != SandboxingKind::MF_MEDIA_ENGINE_CDM
#endif
  ) {
    return RetPromise::CreateAndReject(
        LaunchError("Start...MediaDecoding: bad sandbox type"), __func__);
  }
  TimeStamp remoteDecodingStart = TimeStamp::Now();

  RefPtr<UtilityProcessManager> self = this;
  RefPtr<UtilityMediaServiceChild> umsc =
      UtilityMediaServiceChild::GetSingleton(aSandbox);
  MOZ_ASSERT(umsc, "Unable to get a singleton for UtilityMediaServiceChild");
  return StartUtility(umsc, aSandbox)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self, umsc, aOtherProcess, aChildId, aSandbox,
           remoteDecodingStart]() {
            RefPtr<UtilityProcessKeepAlive> keepAlive =
                self->GetSharedKeepAlive(aSandbox);
            RefPtr<UtilityProcessParent> parent =
                keepAlive ? keepAlive->GetProcessParent() : nullptr;
            if (!parent) {
              NS_WARNING("UtilityMediaServiceParent lost in the middle");
              return RetPromise::CreateAndReject(
                  LaunchError("Start...MediaDecoding: parent lost"), __func__);
            }

            if (!umsc->CanSend()) {
              NS_WARNING("UtilityMediaServiceChild lost in the middle");
              return RetPromise::CreateAndReject(
                  LaunchError("Start...MediaDecoding: child lost"), __func__);
            }

            EndpointProcInfo process = parent->OtherEndpointProcInfo();

            Endpoint<PRemoteMediaManagerChild> childPipe;
            Endpoint<PRemoteMediaManagerParent> parentPipe;
            if (nsresult const rv = PRemoteMediaManager::CreateEndpoints(
                    process, aOtherProcess, &parentPipe, &childPipe);
                NS_FAILED(rv)) {
              MOZ_ASSERT(false, "Could not create content remote decoder");
              return RetPromise::CreateAndReject(
                  LaunchError("PRemoteMediaManager::CreateEndpoints", rv),
                  __func__);
            }

            if (!umsc->SendNewContentRemoteMediaManager(std::move(parentPipe),
                                                        aChildId)) {
              MOZ_ASSERT(false, "SendNewContentRemoteMediaManager failure");
              return RetPromise::CreateAndReject(
                  LaunchError("UMSC::SendNewCRDM"), __func__);
            }

#ifdef MOZ_WMF_MEDIA_ENGINE
            if (aSandbox == SandboxingKind::MF_MEDIA_ENGINE_CDM &&
                !umsc->CreateVideoBridge(process)) {
              MOZ_ASSERT(false, "Failed to create video bridge");
              return RetPromise::CreateAndReject(
                  LaunchError("UMSC::CreateVideoBridge"), __func__);
            }
#endif
            PROFILER_MARKER_TEXT(
                "UtilityProcessManager::StartProcessForRemoteMediaDecoding",
                MEDIA,
                MarkerOptions(
                    MarkerTiming::IntervalUntilNowFrom(remoteDecodingStart)),
                "Resolve"_ns);
            return RetPromise::CreateAndResolve(std::move(childPipe), __func__);
          },
          [self, remoteDecodingStart](LaunchError&& error) {
            NS_WARNING(
                "Reject StartProcessForRemoteMediaDecoding() for "
                "StartUtility() rejection");
            PROFILER_MARKER_TEXT(
                "UtilityProcessManager::StartProcessForRemoteMediaDecoding",
                MEDIA,
                MarkerOptions(
                    MarkerTiming::IntervalUntilNowFrom(remoteDecodingStart)),
                "Reject"_ns);
            return RetPromise::CreateAndReject(std::move(error), __func__);
          });
}

RefPtr<UtilityProcessManager::JSOraclePromise>
UtilityProcessManager::StartJSOracle(dom::JSOracleParent* aParent) {
  using RetPromise = JSOraclePromise;
  return StartUtility(RefPtr{aParent}, SandboxingKind::GENERIC_UTILITY)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          []() { return RetPromise::CreateAndResolve(true, __func__); },
          [](LaunchError const&) {
            return RetPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE,
                                               __func__);
          });
}

#ifdef XP_WIN

// Windows Utils

RefPtr<UtilityProcessManager::WindowsUtilsPromise>
UtilityProcessManager::GetWindowsUtilsPromise() {
  TimeStamp windowsUtilsStart = TimeStamp::Now();
  RefPtr<UtilityProcessManager> self = this;
  if (!mWindowsUtils) {
    mWindowsUtils = new dom::WindowsUtilsParent();
  }

  RefPtr<dom::WindowsUtilsParent> wup = mWindowsUtils;
  MOZ_ASSERT(wup, "Unable to get a singleton for WindowsUtils");
  return StartUtility(wup, SandboxingKind::WINDOWS_UTILS)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self, wup, windowsUtilsStart]() {
            if (!wup->CanSend()) {
              MOZ_ASSERT(false, "WindowsUtilsParent can't send");
              return WindowsUtilsPromise::CreateAndReject(
                  LaunchError("GetWindowsUtilsPromise: !wup->CanSend()"),
                  __func__);
            }
            PROFILER_MARKER_TEXT(
                "UtilityProcessManager::GetWindowsUtilsPromise", OTHER,
                MarkerOptions(
                    MarkerTiming::IntervalUntilNowFrom(windowsUtilsStart)),
                "Resolve"_ns);
            return WindowsUtilsPromise::CreateAndResolve(wup, __func__);
          },
          [self, windowsUtilsStart](LaunchError&& error) {
            NS_WARNING("StartUtility rejected promise for PWindowsUtils");
            PROFILER_MARKER_TEXT(
                "UtilityProcessManager::GetWindowsUtilsPromise", OTHER,
                MarkerOptions(
                    MarkerTiming::IntervalUntilNowFrom(windowsUtilsStart)),
                "Reject"_ns);
            return WindowsUtilsPromise::CreateAndReject(std::move(error),
                                                        __func__);
          });
}

void UtilityProcessManager::ReleaseWindowsUtils() { mWindowsUtils = nullptr; }

RefPtr<UtilityProcessManager::WinFileDialogPromise>
UtilityProcessManager::CreateWinFileDialogActor() {
  using RetPromise = WinFileDialogPromise;
  TimeStamp startTime = TimeStamp::Now();
  auto wfdp = MakeRefPtr<widget::filedialog::WinFileDialogParent>();

  return StartUtility(wfdp, SandboxingKind::WINDOWS_FILE_DIALOG)
      ->Then(
          GetMainThreadSerialEventTarget(), __PRETTY_FUNCTION__,
          [wfdp, startTime]() mutable {
            LOGD("CreateWinFileDialogAsync() resolve: wfdp = [%p]", wfdp.get());
            if (!wfdp->CanSend()) {
              MOZ_ASSERT(false, "WinFileDialogParent can't send");
              return RetPromise::CreateAndReject(
                  LaunchError("CreateWinFileDialogActor: !wfdp->CanSend()"),
                  __PRETTY_FUNCTION__);
            }
            PROFILER_MARKER_TEXT(
                "UtilityProcessManager::CreateWinFileDialogAsync", OTHER,
                MarkerOptions(MarkerTiming::IntervalUntilNowFrom(startTime)),
                "Resolve"_ns);

            return RetPromise::CreateAndResolve(
                widget::filedialog::ProcessProxy(std::move(wfdp)),
                __PRETTY_FUNCTION__);
          },
          [startTime](LaunchError&& error) {
            LOGD("CreateWinFileDialogAsync() reject");
            PROFILER_MARKER_TEXT(
                "UtilityProcessManager::CreateWinFileDialogAsync", OTHER,
                MarkerOptions(MarkerTiming::IntervalUntilNowFrom(startTime)),
                "Reject"_ns);

            return RetPromise::CreateAndReject(std::move(error),
                                               __PRETTY_FUNCTION__);
          });
}

#endif  // XP_WIN

#if defined(NIGHTLY_BUILD) && !defined(MOZ_NO_SMART_CARDS)
RefPtr<UtilityProcessManager::PKCS11ModulePromise>
UtilityProcessManager::StartPKCS11Module() {
  using RetPromise = PKCS11ModulePromise;
  auto parent = MakeRefPtr<psm::PKCS11ModuleParent>();
  auto startPromise = StartUtility(parent, SandboxingKind::PKCS11_MODULE);
  return startPromise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [parent = std::move(parent)]() mutable {
        if (!parent->CanSend()) {
          MOZ_ASSERT(false, "PKCS11ModuleParent lost in the middle");
          return RetPromise::CreateAndReject(
              LaunchError("StartPKCS11Module: !parent->CanSend()"),
              __PRETTY_FUNCTION__);
        }
        return RetPromise::CreateAndResolve(std::move(parent), __func__);
      },
      [](LaunchError&& aError) {
        return RetPromise::CreateAndReject(std::move(aError), __func__);
      });
}
#endif  // NIGHTLY_BUILD && !MOZ_NO_SMART_CARDS

void UtilityProcessManager::OnProcessUnexpectedShutdown(
    UtilityProcessHost* aHost) {
  MOZ_ASSERT(NS_IsMainThread());

  for (auto& it : mProcesses) {
    if (it->mProcess == aHost) {
      DestroyProcess(it);
      return;
    }
  }

  MOZ_CRASH(
      "Called UtilityProcessManager::OnProcessUnexpectedShutdown with invalid "
      "aHost");
}

void UtilityProcessManager::CleanShutdownAllProcesses() {
  LOGD("[%p] UtilityProcessManager::CleanShutdownAllProcesses", this);

  while (!mProcesses.IsEmpty()) {
    DestroyProcess(mProcesses.LastElement());
  }
}

void UtilityProcessManager::CleanShutdown(SandboxingKind aSandbox) {
  LOGD("[%p] UtilityProcessManager::CleanShutdown SandboxingKind=%" PRIu64,
       this, aSandbox);

  MOZ_ASSERT(NS_IsMainThread());

  // Dropping our keep-alive is what shuts the process down, unless someone
  // else promoted a WeakPtr to it, in which case it lives on until they let go
  // too.
  mSharedKeepAlives[aSandbox] = nullptr;
}

size_t UtilityProcessManager::AliveProcesses() { return mProcesses.Length(); }

void UtilityProcessManager::DestroyProcess(ProcessFields* aProcess) {
  LOGD("[%p] UtilityProcessManager::DestroyProcess SandboxingKind=%" PRIu64,
       this, aProcess->mSandbox);

  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  RefPtr<ProcessFields> p = aProcess;
  if (!mProcesses.RemoveElement(p)) {
    return;
  }

  RefPtr<UtilityProcessHost> process = p->mProcess.forget();

  if (mSharedKeepAlives[p->mSandbox] &&
      mSharedKeepAlives[p->mSandbox]->mProcess == p) {
    mSharedKeepAlives[p->mSandbox] = nullptr;
  }

  if (mProcesses.IsEmpty() && mObserver) {
    Preferences::RemoveObserver(mObserver, "");
    mObserver = nullptr;
  }

  p->mQueuedPrefs.Clear();
  p->mProcessParent = nullptr;

  ++mPendingShutdowns;
  process->Shutdown()->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [self = RefPtr{this}](const UtilityProcessHost::ShutdownPromiseType::
                                ResolveOrRejectValue&) {
        self->OnProcessShutdownComplete();
      });
  CrashReporter::RecordAnnotationCString(
      CrashReporter::Annotation::UtilityProcessStatus, "Destroyed");

  if (mProcesses.IsEmpty()) {
    sSingleton = nullptr;
  }
}

Maybe<base::ProcessId> UtilityProcessManager::ProcessPid(
    SandboxingKind aSandbox) {
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<UtilityProcessKeepAlive> keepAlive = GetSharedKeepAlive(aSandbox);
  if (keepAlive) {
    if (RefPtr actor = keepAlive->GetProcessParent()) {
      return Some(actor->OtherPid());
    }
  }
  return Nothing();
}

class UtilityMemoryReporter : public MemoryReportingProcess {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(UtilityMemoryReporter, override)

  explicit UtilityMemoryReporter(UtilityProcessParent* aParent) {
    mParent = aParent;
  }

  bool IsAlive() const override { return bool(GetParent()); }

  bool SendRequestMemoryReport(
      const uint32_t& aGeneration, const bool& aAnonymize,
      const bool& aMinimizeMemoryUsage,
      const Maybe<ipc::FileDescriptor>& aDMDFile) override {
    RefPtr<UtilityProcessParent> parent = GetParent();
    if (!parent) {
      return false;
    }

    return parent->SendRequestMemoryReport(aGeneration, aAnonymize,
                                           aMinimizeMemoryUsage, aDMDFile);
  }

  int32_t Pid() const override {
    if (RefPtr<UtilityProcessParent> parent = GetParent()) {
      return (int32_t)parent->OtherPid();
    }
    return 0;
  }

 private:
  RefPtr<UtilityProcessParent> GetParent() const { return mParent; }

  RefPtr<UtilityProcessParent> mParent = nullptr;

 protected:
  ~UtilityMemoryReporter() = default;
};

RefPtr<MemoryReportingProcess> UtilityProcessManager::GetProcessMemoryReporter(
    UtilityProcessParent* parent) {
  return new UtilityMemoryReporter(parent);
}

UtilityProcessKeepAlive::UtilityProcessKeepAlive(
    UtilityProcessManager::ProcessFields* aProcess)
    : mProcess(aProcess) {
  MOZ_ASSERT(NS_IsMainThread());
}

UtilityProcessKeepAlive::~UtilityProcessKeepAlive() {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<UtilityProcessManager> upm = UtilityProcessManager::GetIfExists();
  if (!upm || !mProcess->mProcess) {
    LOGD("[%p] ~UtilityProcessKeepAlive - process already gone", this);
    return;
  }

  LOGD("[%p] ~UtilityProcessKeepAlive - last consumer gone, shutting down",
       this);
  upm->DestroyProcess(mProcess);
}

RefPtr<UtilityProcessManager::SharedLaunchPromise<Ok>>
UtilityProcessKeepAlive::GetLaunchPromise() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mProcess->mLaunchPromise;
}

RefPtr<UtilityProcessParent> UtilityProcessKeepAlive::GetProcessParent() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mProcess->mProcessParent;
}

}  // namespace mozilla::ipc
