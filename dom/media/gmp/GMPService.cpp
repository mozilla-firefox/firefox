/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPService.h"

#include "ChromiumCDMParent.h"
#include "GMPLog.h"
#include "GMPParent.h"
#include "GMPProcessParent.h"
#include "GMPServiceChild.h"
#include "GMPServiceParent.h"
#include "GMPVideoDecoderParent.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/PluginCrashedEvent.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"
#include "nsThreadUtils.h"
#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
#  include "mozilla/SandboxInfo.h"
#endif
#include "VideoUtils.h"
#include "mozilla/Services.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPtr.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsComponentManagerUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsGlobalWindowInner.h"
#include "nsHashKeys.h"
#include "nsIObserverService.h"
#include "nsIXULAppInfo.h"
#include "nsNativeCharsetUtils.h"
#include "nsXPCOMPrivate.h"
#include "prio.h"
#include "runnable_utils.h"

namespace mozilla {

LogModule* GetGMPLog() {
  static LazyLogModule sLog("GMP");
  return sLog;
}

LogModule* GetGMPLibraryLog() {
  static LazyLogModule sLog("GMPLibrary");
  return sLog;
}

GMPLogLevel GetGMPLibraryLogLevel() {
  switch (GetGMPLibraryLog()->Level()) {
    case LogLevel::Disabled:
      return kGMPLogQuiet;
    case LogLevel::Error:
      return kGMPLogError;
    case LogLevel::Warning:
      return kGMPLogWarning;
    case LogLevel::Info:
      return kGMPLogInfo;
    case LogLevel::Debug:
      return kGMPLogDebug;
    case LogLevel::Verbose:
      return kGMPLogDetail;
  }
  return kGMPLogInvalid;
}

#ifdef __CLASS__
#  undef __CLASS__
#endif
#define __CLASS__ "GMPService"

namespace gmp {

StaticMutex GeckoMediaPluginService::sSingletonMutex;
StaticRefPtr<GeckoMediaPluginService>
    GeckoMediaPluginService::sSingletonService;

already_AddRefed<GeckoMediaPluginService>
GeckoMediaPluginService::GetGeckoMediaPluginService() {
  RefPtr<GeckoMediaPluginService> service;
  bool created = false;
  {
    StaticMutexAutoLock lock(sSingletonMutex);
    service = sSingletonService.get();
    if (!service) {
      // Refuse to construct a new singleton once shutdown has been confirmed,
      // as the main thread setup could not add its observers and blockers.
      if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
        return nullptr;
      }
      // Nothing in here may wait for another thread, as other threads wait for
      // us in the component manager while we are in here.
      if (XRE_IsParentProcess()) {
        service = new GeckoMediaPluginServiceParent();
      } else {
        service = new GeckoMediaPluginServiceChild();
      }
      // The GMP thread has to exist once the service is registered.
      nsCOMPtr<nsIThread> gmpThread;
      if (NS_WARN_IF(
              NS_FAILED(service->GetThread(getter_AddRefs(gmpThread))))) {
        return nullptr;
      }
      sSingletonService = service;
      created = true;
    }
  }

  if (NS_IsMainThread()) {
    // Does nothing if the main thread setup has already run.
    service->RunMainThreadInit();
  } else if (created) {
    // Do not wait for this. A main thread caller of the component manager can
    // be waiting for us to return, and it runs the setup itself in that case.
    nsresult rv = NS_DispatchToMainThread(
        NS_NewRunnableFunction("GeckoMediaPluginService::RunMainThreadInit",
                               [service] { service->RunMainThreadInit(); }));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      service->Shutdown();
    }
  }
  return service.forget();
}

void GeckoMediaPluginService::RunMainThreadInit() {
  MOZ_ASSERT(NS_IsMainThread());

  constexpr MainInit expected = MainInit::Pending;
  if (!mMainInit.compareExchange(expected, MainInit::Running)) {
    return;
  }

  // Phases only advance on the main thread, so no phase can start between this
  // check and the registrations below, and a service that passes it sees every
  // shutdown notification from here on.
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    GMP_LOG_DEBUG("{}::{} shutdown already started", __CLASS__, __FUNCTION__);
    Shutdown();
    return;
  }

  if (NS_WARN_IF(NS_FAILED(InitMainThread()))) {
    // Last, as the GMP thread must be gone before the registrations are.
    Shutdown();
    UninitMainThread();
    return;
  }

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  if (XRE_IsParentProcess()) {
    // GMPProcessParent should only be instantiated in the parent
    // so initialization only needs to be done in the parent.
    GMPProcessParent::InitStaticMainThread();
  }
#endif

  RunOnShutdown([] {
    RefPtr<GeckoMediaPluginService> service;
    StaticMutexAutoLock lock(sSingletonMutex);
    service = sSingletonService.forget();
  });
}

void GeckoMediaPluginService::Shutdown() {
  GMP_LOG_DEBUG("{}::{}", __CLASS__, __FUNCTION__);
  mMainInit = MainInit::Shutdown;
  mXPCOMWillShutdown = true;
  OnShutdown();
  // Also refuses to create the GMP thread from now on.
  ShutdownGMPThread();

  // Release outside of the lock, as the last reference may run the destructor.
  RefPtr<GeckoMediaPluginService> service;
  StaticMutexAutoLock lock(sSingletonMutex);
  if (sSingletonService.get() == this) {
    service = sSingletonService.forget();
  }
}

NS_IMPL_ISUPPORTS(GeckoMediaPluginService, mozIGeckoMediaPluginService,
                  nsIObserver)

GeckoMediaPluginService::GeckoMediaPluginService()
    : mMainInit(MainInit::Pending),
      mMutex("GeckoMediaPluginService::mMutex"),
      mMainThread(GetMainThreadSerialEventTarget()),
      mGMPThreadShutdown(false),
      mShuttingDownOnGMPThread(false),
      mXPCOMWillShutdown(false) {}

GeckoMediaPluginService::~GeckoMediaPluginService() = default;

NS_IMETHODIMP
GeckoMediaPluginService::RunPluginCrashCallbacks(
    uint32_t aPluginId, const nsACString& aPluginName) {
  MOZ_ASSERT(NS_IsMainThread());
  GMP_LOG_DEBUG("{}::{}({})", __CLASS__, __FUNCTION__, aPluginId);

  mozilla::UniquePtr<nsTArray<RefPtr<GMPCrashHelper>>> helpers;
  {
    MutexAutoLock lock(mMutex);
    mPluginCrashHelpers.Remove(aPluginId, &helpers);
  }
  if (!helpers) {
    GMP_LOG_DEBUG("{}::{}({}) No crash helpers, not handling crash.", __CLASS__,
                  __FUNCTION__, aPluginId);
    return NS_OK;
  }

  for (const auto& helper : *helpers) {
    nsCOMPtr<nsPIDOMWindowInner> window = helper->GetPluginCrashedEventTarget();
    if (NS_WARN_IF(!window)) {
      continue;
    }
    RefPtr<dom::Document> document(window->GetExtantDoc());
    if (NS_WARN_IF(!document)) {
      continue;
    }

    dom::PluginCrashedEventInit init;
    init.mPluginID = aPluginId;
    init.mBubbles = true;
    init.mCancelable = true;
    init.mGmpPlugin = true;
    CopyUTF8toUTF16(aPluginName, init.mPluginName);
    init.mSubmittedCrashReport = false;
    RefPtr<dom::PluginCrashedEvent> event =
        dom::PluginCrashedEvent::Constructor(document, u"PluginCrashed"_ns,
                                             init);
    event->SetTrusted(true);
    event->WidgetEventPtr()->mFlags.mOnlyChromeDispatch = true;

    // MOZ_KnownLive due to bug 1506441
    EventDispatcher::DispatchDOMEvent(
        MOZ_KnownLive(nsGlobalWindowInner::Cast(window)), nullptr, event,
        nullptr, nullptr);
  }

  return NS_OK;
}

nsresult GeckoMediaPluginService::InitMainThread() {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIXULAppInfo> appInfo =
      do_GetService("@mozilla.org/xre/app-info;1");
  if (appInfo) {
    nsAutoCString version;
    nsAutoCString buildID;
    if (NS_SUCCEEDED(appInfo->GetVersion(version)) &&
        NS_SUCCEEDED(appInfo->GetAppBuildID(buildID))) {
      GMP_LOG_DEBUG(
          "GeckoMediaPluginService created; Gecko version={} buildID={}",
          version.get(), buildID.get());
    }
  }

  nsCOMPtr<nsIObserverService> obsService =
      mozilla::services::GetObserverService();
  MOZ_ASSERT(obsService);
  MOZ_ALWAYS_SUCCEEDS(obsService->AddObserver(
      this, NS_XPCOM_SHUTDOWN_THREADS_OBSERVER_ID, false));

  mMainInit = MainInit::Done;

  // The GMP thread exists already, this kicks off scanning for plugins now that
  // the state is Done.
  nsCOMPtr<nsIThread> thread;
  return GetThread(getter_AddRefs(thread));
}

void GeckoMediaPluginService::UninitMainThread() {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> obsService =
      mozilla::services::GetObserverService();
  if (obsService) {
    (void)obsService->RemoveObserver(this,
                                     NS_XPCOM_SHUTDOWN_THREADS_OBSERVER_ID);
  }
}

RefPtr<GetCDMParentPromise> GeckoMediaPluginService::GetCDM(
    const NodeIdParts& aNodeIdParts, const nsACString& aKeySystem,
    GMPCrashHelper* aHelper) {
  AssertOnGMPThread();

  if (mShuttingDownOnGMPThread || aKeySystem.IsEmpty()) {
    nsPrintfCString reason(
        "%s::%s failed, aKeySystem.IsEmpty() = %d, mShuttingDownOnGMPThread = "
        "%d.",
        __CLASS__, __FUNCTION__, aKeySystem.IsEmpty(),
        mShuttingDownOnGMPThread);
    return GetCDMParentPromise::CreateAndReject(
        MediaResult(NS_ERROR_FAILURE, reason.get()), __func__);
  }

  typedef MozPromiseHolder<GetCDMParentPromise> PromiseHolder;
  PromiseHolder* rawHolder(new PromiseHolder());
  RefPtr<GetCDMParentPromise> promise = rawHolder->Ensure(__func__);
  nsCOMPtr<nsISerialEventTarget> thread(GetGMPThread());
  RefPtr<GMPCrashHelper> helper(aHelper);
  nsTArray<nsCString> tags{nsCString{aKeySystem}};
  GetContentParent(aHelper, NodeIdVariant{aNodeIdParts},
                   nsLiteralCString(CHROMIUM_CDM_API), tags)
      ->Then(
          thread, __func__,
          [rawHolder, helper, keySystem = nsCString{aKeySystem}](
              const RefPtr<GMPContentParentCloseBlocker>& wrapper) {
            RefPtr<GMPContentParent> parent = wrapper->mParent;
            MOZ_ASSERT(
                parent,
                "Wrapper should wrap a valid parent if we're in this path.");
            UniquePtr<PromiseHolder> holder(rawHolder);
            RefPtr<ChromiumCDMParent> cdm = parent->GetChromiumCDM(keySystem);
            if (!cdm) {
              nsPrintfCString reason(
                  "%s::%s failed since GetChromiumCDM returns nullptr.",
                  __CLASS__, __FUNCTION__);
              holder->Reject(MediaResult(NS_ERROR_FAILURE, reason.get()),
                             __func__);
              return;
            }
            if (helper) {
              cdm->SetCrashHelper(helper);
            }
            holder->Resolve(cdm, __func__);
          },
          [rawHolder](MediaResult result) {
            nsPrintfCString reason(
                "%s::%s failed since GetContentParent rejects the promise with "
                "reason %s.",
                __CLASS__, __FUNCTION__, result.Description().get());
            UniquePtr<PromiseHolder> holder(rawHolder);
            holder->Reject(MediaResult(NS_ERROR_FAILURE, reason.get()),
                           __func__);
          });

  return promise;
}

#if defined(MOZ_SANDBOX) && defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
RefPtr<GetGMPContentParentPromise>
GeckoMediaPluginService::GetContentParentForTest() {
  AssertOnGMPThread();

  nsTArray<nsCString> tags;
  tags.AppendElement("fake"_ns);

  const nsString origin1 = u"http://example1.com"_ns;
  const nsString origin2 = u"http://example2.org"_ns;
  const nsString gmpName = u"gmp-fake"_ns;

  NodeIdParts nodeIdParts = NodeIdParts{origin1, origin2, gmpName};

  if (mShuttingDownOnGMPThread) {
    nsPrintfCString reason("%s::%s failed, mShuttingDownOnGMPThread = %d.",
                           __CLASS__, __FUNCTION__, mShuttingDownOnGMPThread);
    return GetGMPContentParentPromise::CreateAndReject(
        MediaResult(NS_ERROR_FAILURE, reason.get()), __func__);
  }

  using PromiseHolder = MozPromiseHolder<GetGMPContentParentPromise>;
  PromiseHolder* rawHolder(new PromiseHolder());
  RefPtr<GetGMPContentParentPromise> promise = rawHolder->Ensure(__func__);
  nsCOMPtr<nsISerialEventTarget> thread(GetGMPThread());
  GetContentParent(nullptr, NodeIdVariant{nodeIdParts},
                   nsLiteralCString(CHROMIUM_CDM_API), tags)
      ->Then(
          thread, __func__,
          [rawHolder](const RefPtr<GMPContentParentCloseBlocker>& wrapper) {
            RefPtr<GMPContentParent> parent = wrapper->mParent;
            MOZ_ASSERT(
                parent,
                "Wrapper should wrap a valid parent if we're in this path.");
            UniquePtr<PromiseHolder> holder(rawHolder);
            if (!parent) {
              nsPrintfCString reason("%s::%s failed since no GMPContentParent.",
                                     __CLASS__, __FUNCTION__);
              holder->Reject(MediaResult(NS_ERROR_FAILURE, reason.get()),
                             __func__);
              return;
            }
            holder->Resolve(wrapper, __func__);
          },
          [rawHolder](const MediaResult& result) {
            nsPrintfCString reason(
                "%s::%s failed since GetContentParent rejects the promise with "
                "reason %s.",
                __CLASS__, __FUNCTION__, result.Description().get());
            UniquePtr<PromiseHolder> holder(rawHolder);
            holder->Reject(MediaResult(NS_ERROR_FAILURE, reason.get()),
                           __func__);
          });

  return promise;
}
#endif

void GeckoMediaPluginService::ShutdownGMPThread() {
  GMP_LOG_DEBUG("{}::{}", __CLASS__, __FUNCTION__);
  nsCOMPtr<nsIThread> gmpThread;
  {
    MutexAutoLock lock(mMutex);
    mGMPThreadShutdown = true;
    mGMPThread.swap(gmpThread);
  }

  if (gmpThread) {
    gmpThread->Shutdown();
  }
}

nsresult GeckoMediaPluginService::GMPDispatch(
    nsIRunnable* event, nsIEventTarget::DispatchFlags flags) {
  return GMPDispatch(do_AddRef(event), flags);
}

nsresult GeckoMediaPluginService::GMPDispatch(
    already_AddRefed<nsIRunnable> event, nsIEventTarget::DispatchFlags flags) {
  // NOTE: This method always releases `event` on failure, rather than leaking
  // it, even if `NS_DISPATCH_FALLIBLE` is not specified.
  nsCOMPtr<nsIRunnable> r(event);
  nsCOMPtr<nsIThread> thread;
  nsresult rv = GetThread(getter_AddRefs(thread));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return thread->Dispatch(r.forget(), flags | NS_DISPATCH_FALLIBLE);
}

// always call with getter_AddRefs, because it does
NS_IMETHODIMP
GeckoMediaPluginService::GetThread(nsIThread** aThread) {
  MOZ_ASSERT(aThread);

  // This can be called from any thread.
  MutexAutoLock lock(mMutex);

  return GetThreadLocked(aThread);
}

void GeckoMediaPluginService::MaybeInitializePluginsLocked() {
  mMutex.AssertCurrentThreadOwns();
  MOZ_ASSERT(mGMPThread);
  // The GMP thread is created together with the service, so for a service
  // first used off the main thread it exists before RunMainThreadInit() has
  // run. Scanning for plugins waits for that: InitMainThread() calls
  // GetThread() once the state is Done, which gets here again. It never does
  // after Shutdown().
  if (mPluginsInitialized || mMainInit != MainInit::Done) {
    return;
  }
  mPluginsInitialized = true;
  // Tell the thread to initialize plugins
  InitializePlugins(mGMPThread);
}

// always call with getter_AddRefs, because it does
nsresult GeckoMediaPluginService::GetThreadLocked(nsIThread** aThread) {
  MOZ_ASSERT(aThread);

  mMutex.AssertCurrentThreadOwns();

  if (!mGMPThread) {
    // Don't allow the thread to be created after shutdown has started.
    if (mGMPThreadShutdown) {
      return NS_ERROR_FAILURE;
    }

    nsresult rv = NS_NewNamedThread("GMPThread", getter_AddRefs(mGMPThread));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  MaybeInitializePluginsLocked();

  nsCOMPtr<nsIThread> copy = mGMPThread;
  copy.forget(aThread);

  return NS_OK;
}

already_AddRefed<nsISerialEventTarget> GeckoMediaPluginService::GetGMPThread() {
  nsCOMPtr<nsISerialEventTarget> thread;
  {
    MutexAutoLock lock(mMutex);
    thread = mGMPThread;
  }
  return thread.forget();
}

NS_IMETHODIMP
GeckoMediaPluginService::GetGMPVideoDecoder(
    GMPCrashHelper* aHelper, nsTArray<nsCString>* aTags,
    const nsACString& aNodeId,
    UniquePtr<GetGMPVideoDecoderCallback>&& aCallback) {
  AssertOnGMPThread();
  NS_ENSURE_ARG(aTags && aTags->Length() > 0);
  NS_ENSURE_ARG(aCallback);

  if (mShuttingDownOnGMPThread) {
    return NS_ERROR_FAILURE;
  }

  GetGMPVideoDecoderCallback* rawCallback = aCallback.release();
  nsCOMPtr<nsISerialEventTarget> thread(GetGMPThread());
  RefPtr<GMPCrashHelper> helper(aHelper);
  GetContentParent(aHelper, NodeIdVariant{nsCString(aNodeId)},
                   nsLiteralCString(GMP_API_VIDEO_DECODER), *aTags)
      ->Then(
          thread, __func__,
          [rawCallback,
           helper](const RefPtr<GMPContentParentCloseBlocker>& wrapper) {
            RefPtr<GMPContentParent> parent = wrapper->mParent;
            UniquePtr<GetGMPVideoDecoderCallback> callback(rawCallback);
            GMPVideoDecoderParent* actor = nullptr;
            if (parent && NS_SUCCEEDED(parent->GetGMPVideoDecoder(&actor))) {
              actor->SetCrashHelper(helper);
            }
            callback->Done(actor, actor);
          },
          [rawCallback] {
            UniquePtr<GetGMPVideoDecoderCallback> callback(rawCallback);
            callback->Done(nullptr, nullptr);
          });

  return NS_OK;
}

NS_IMETHODIMP
GeckoMediaPluginService::GetGMPVideoEncoder(
    GMPCrashHelper* aHelper, nsTArray<nsCString>* aTags,
    const nsACString& aNodeId,
    UniquePtr<GetGMPVideoEncoderCallback>&& aCallback) {
  AssertOnGMPThread();
  NS_ENSURE_ARG(aTags && aTags->Length() > 0);
  NS_ENSURE_ARG(aCallback);

  if (mShuttingDownOnGMPThread) {
    return NS_ERROR_FAILURE;
  }

  GetGMPVideoEncoderCallback* rawCallback = aCallback.release();
  nsCOMPtr<nsISerialEventTarget> thread(GetGMPThread());
  RefPtr<GMPCrashHelper> helper(aHelper);
  GetContentParent(aHelper, NodeIdVariant{nsCString(aNodeId)},
                   nsLiteralCString(GMP_API_VIDEO_ENCODER), *aTags)
      ->Then(
          thread, __func__,
          [rawCallback,
           helper](const RefPtr<GMPContentParentCloseBlocker>& wrapper) {
            RefPtr<GMPContentParent> parent = wrapper->mParent;
            UniquePtr<GetGMPVideoEncoderCallback> callback(rawCallback);
            GMPVideoEncoderParent* actor = nullptr;
            if (parent && NS_SUCCEEDED(parent->GetGMPVideoEncoder(&actor))) {
              actor->SetCrashHelper(helper);
            }
            callback->Done(actor, actor);
          },
          [rawCallback] {
            UniquePtr<GetGMPVideoEncoderCallback> callback(rawCallback);
            callback->Done(nullptr, nullptr);
          });

  return NS_OK;
}

void GeckoMediaPluginService::ConnectCrashHelper(uint32_t aPluginId,
                                                 GMPCrashHelper* aHelper) {
  if (!aHelper) {
    return;
  }

  MutexAutoLock lock(mMutex);
  mPluginCrashHelpers.WithEntryHandle(aPluginId, [&](auto&& entry) {
    if (!entry) {
      entry.Insert(std::make_unique<nsTArray<RefPtr<GMPCrashHelper>>>());
    } else if (entry.Data()->Contains(aHelper)) {
      return;
    }
    entry.Data()->AppendElement(aHelper);
  });
}

void GeckoMediaPluginService::DisconnectCrashHelper(GMPCrashHelper* aHelper) {
  if (!aHelper) {
    return;
  }
  MutexAutoLock lock(mMutex);
  for (auto iter = mPluginCrashHelpers.Iter(); !iter.Done(); iter.Next()) {
    nsTArray<RefPtr<GMPCrashHelper>>* helpers = iter.UserData();
    if (!helpers->Contains(aHelper)) {
      continue;
    }
    helpers->RemoveElement(aHelper);
    MOZ_ASSERT(!helpers->Contains(aHelper));  // Ensure there aren't duplicates.
    if (helpers->IsEmpty()) {
      iter.Remove();
    }
  }
}

}  // namespace gmp
}  // namespace mozilla

#undef __CLASS__
