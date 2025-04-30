/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ContentParent_h
#define mozilla_dom_ContentParent_h

#include "mozilla/dom/PContentParent.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/dom/MessageManagerCallback.h"
#include "mozilla/dom/MediaSessionBinding.h"
#include "mozilla/dom/ProcessIsolation.h"
#include "mozilla/dom/RemoteBrowser.h"
#include "mozilla/dom/RemoteType.h"
#include "mozilla/dom/JSProcessActorParent.h"
#include "mozilla/dom/ProcessActor.h"
#include "mozilla/dom/UniqueContentParentKeepAlive.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/gfx/gfxVarReceiver.h"
#include "mozilla/gfx/GPUProcessListener.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"
#include "mozilla/ipc/InputStreamUtils.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "mozilla/Attributes.h"
#include "mozilla/DataMutex.h"
#include "mozilla/HalTypes.h"
#include "mozilla/IdleTaskRunner.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Maybe.h"
#include "mozilla/MemoryReportingProcess.h"
#include "mozilla/MozPromise.h"
#include "mozilla/RecursiveMutex.h"
#include "mozilla/Result.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"

#include "MainThreadUtils.h"
#include "nsClassHashtable.h"
#include "nsTHashMap.h"
#include "nsTHashSet.h"
#include "nsHashKeys.h"
#include "nsIAsyncShutdown.h"
#include "nsIDOMProcessParent.h"
#include "nsIInterfaceRequestor.h"
#include "nsIObserver.h"
#include "nsIRemoteTab.h"
#include "nsITransferable.h"
#include "nsIDOMGeoPositionCallback.h"
#include "nsIDOMGeoPositionErrorCallback.h"
#include "nsIUrlClassifierFeature.h"
#include "nsRefPtrHashtable.h"
#include "PermissionMessageUtils.h"
#include "DriverCrashGuard.h"
#include "nsIReferrerInfo.h"

class nsConsoleService;
class nsICycleCollectorLogSink;
class nsIDumpGCAndCCLogsCallback;
class nsIRemoteTab;
class nsITimer;
class ParentIdleListener;
class nsIWidget;
class nsIX509Cert;

namespace CrashReporter {
class CrashReporterInitArgs;
}

namespace mozilla {
class PClipboardWriteRequestParent;
class PRemoteSpellcheckEngineParent;

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
class SandboxBroker;
class SandboxBrokerPolicyFactory;
#endif

class PreallocatedProcessManagerImpl;

using mozilla::loader::PScriptCacheParent;

namespace ipc {
class CrashReporterHost;
class TestShellParent;
class SharedPreferenceSerializer;
}  // namespace ipc

namespace layers {
struct TextureFactoryIdentifier;
}  // namespace layers

namespace dom {

class BrowsingContextGroup;
class Element;
class BrowserParent;
class ClonedMessageData;
class MemoryReport;
class TabContext;
class GetFilesHelper;
class MemoryReportRequestHost;
class RemoteWorkerDebuggerManagerParent;
class RemoteWorkerManager;
class RemoteWorkerServiceParent;
class ThreadsafeContentParentHandle;
struct CancelContentJSOptions;

#define NS_CONTENTPARENT_IID \
  {0xeeec9ebf, 0x8ecf, 0x4e38, {0x81, 0xda, 0xb7, 0x34, 0x13, 0x7e, 0xac, 0xf3}}

class ContentParent final : public PContentParent,
                            public nsIDOMProcessParent,
                            public nsIObserver,
                            public nsIDOMGeoPositionCallback,
                            public nsIDOMGeoPositionErrorCallback,
                            public nsIAsyncShutdownBlocker,
                            public nsIInterfaceRequestor,
                            public gfx::gfxVarReceiver,
                            public mozilla::LinkedListElement<ContentParent>,
                            public gfx::GPUProcessListener,
                            public mozilla::MemoryReportingProcess,
                            public mozilla::dom::ipc::MessageManagerCallback,
                            public mozilla::ipc::IShmemAllocator,
                            public ProcessActor {
  typedef mozilla::ipc::GeckoChildProcessHost GeckoChildProcessHost;
  typedef mozilla::ipc::TestShellParent TestShellParent;
  typedef mozilla::ipc::PrincipalInfo PrincipalInfo;
  typedef mozilla::dom::ClonedMessageData ClonedMessageData;
  typedef mozilla::dom::BrowsingContextGroup BrowsingContextGroup;

  friend class mozilla::PreallocatedProcessManagerImpl;
  friend class PContentParent;
  friend class mozilla::dom::RemoteWorkerManager;
  friend struct mozilla::dom::ContentParentKeepAliveDeleter;

 public:
  using LaunchPromise =
      mozilla::MozPromise<UniqueContentParentKeepAlive, nsresult, true>;

  NS_DECLARE_STATIC_IID_ACCESSOR(NS_CONTENTPARENT_IID)

  static LogModule* GetLog();

  static ContentParent* Cast(PContentParent* aActor) {
    return static_cast<ContentParent*>(aActor);
  }

  /**
   * Create a ContentParent suitable for use later as a content process.
   */
  static UniqueContentParentKeepAlive MakePreallocProcess();

  /**
   * Start up the content-process machinery.  This might include
   * scheduling pre-launch tasks.
   */
  static void StartUp();

  /** Shut down the content-process machinery. */
  static void ShutDown();

  static uint32_t GetPoolSize(const nsACString& aContentProcessType);

  static uint32_t GetMaxProcessCount(const nsACString& aContentProcessType);

  static bool IsMaxProcessCountReached(const nsACString& aContentProcessType);

  static void ReleaseCachedProcesses();

  static void LogAndAssertFailedPrincipalValidationInfo(
      nsIPrincipal* aPrincipal, const char* aMethod);

  /**
   * Picks a random content parent from |aContentParents| respecting the index
   * limit set by |aMaxContentParents|. If |aBrowserId| is non-zero, that tab
   * will be ignored when counting tabs in this process.
   * Returns null if non available.
   */
  static already_AddRefed<ContentParent> MinTabSelect(
      const nsTArray<ContentParent*>& aContentParents,
      int32_t maxContentParents, uint64_t aBrowserId);

  /**
   * Get or create a content process which can be used for hosting web content
   * or workers.
   *
   * This method returns a |UniqueContentParentKeepAlive|, which manages the
   * lifecycle of the process. See the documentation on |AddKeepAlive| for more
   * information about managing content process lifecycles.
   *
   * The returned ContentParent which may still be in the process of launching.
   * Use the |WaitForLaunchAsync| or |WaitForLaunchSync| methods to wait for
   * this process to finish launching.
   *
   * @param aRemoteType Required remote type for new & used processes.
   * @param aGroup If specified, the |BrowsingContextGroup| requesting process
   *               selection. Used to ensure that only a single process per
   *               remoteType is used for each |BrowsingContextGroup|.
   * @param aPriority Initial process priority for a new content process.
   * @param aPreferUsed If true, process selection will prefer re-using an
   *                    existing ContentProcess over launching a new one.
   * @param aBrowserId The |BrowserId| requesting process selection. This
   *                   information is used to reduce unnecessary process churn
   *                   when navigating (see |MinTabSelect|).
   *                   The returned KeepAlive will be for this BrowserId.
   */
  static UniqueContentParentKeepAlive GetNewOrUsedLaunchingBrowserProcess(
      const nsACString& aRemoteType, BrowsingContextGroup* aGroup = nullptr,
      hal::ProcessPriority aPriority =
          hal::ProcessPriority::PROCESS_PRIORITY_FOREGROUND,
      bool aPreferUsed = false, uint64_t aBrowserId = 0);

  /**
   * Like |GetNewOrUsedLaunchingBrowserProcess|, but returns a promise which
   * resolves when the process is finished launching.
   */
  static RefPtr<ContentParent::LaunchPromise> GetNewOrUsedBrowserProcessAsync(
      const nsACString& aRemoteType, BrowsingContextGroup* aGroup = nullptr,
      hal::ProcessPriority aPriority =
          hal::ProcessPriority::PROCESS_PRIORITY_FOREGROUND,
      bool aPreferUsed = false, uint64_t aBrowserId = 0);

  /**
   * Like |GetNewOrUsedLaunchingBrowserProcess|, but blocks the main thread
   * until the process process is finished launching before returning.
   */
  static UniqueContentParentKeepAlive GetNewOrUsedBrowserProcess(
      const nsACString& aRemoteType, BrowsingContextGroup* aGroup = nullptr,
      hal::ProcessPriority aPriority =
          hal::ProcessPriority::PROCESS_PRIORITY_FOREGROUND,
      bool aPreferUsed = false, uint64_t aBrowserId = 0);

  /**
   * Create an nsITransferable with the specified data flavor types.
   */
  static mozilla::Result<nsCOMPtr<nsITransferable>, nsresult>
  CreateClipboardTransferable(const nsTArray<nsCString>& aTypes);

  /**
   * Asynchronously wait for this content process to finish launching, such that
   * the ContentParent actor is ready for IPC.
   *
   * @param aPriority The initial priority for the process after launching.
   * @param aBrowserId The BrowserId to hold a KeepAlive for during the async
   *                   launch which will be used to resolve the LaunchPromise.
   */
  RefPtr<ContentParent::LaunchPromise> WaitForLaunchAsync(
      hal::ProcessPriority aPriority =
          hal::ProcessPriority::PROCESS_PRIORITY_FOREGROUND,
      uint64_t aBrowserId = 0);

  /**
   * Like `WaitForLaunchAsync`, but synchronously blocks the main thread until
   * the content process has finished launching.
   */
  bool WaitForLaunchSync(hal::ProcessPriority aPriority =
                             hal::ProcessPriority::PROCESS_PRIORITY_FOREGROUND);

  /**
   * Get or create a content process for the given TabContext.  aFrameElement
   * should be the frame/iframe element with which this process will
   * associated.
   */
  static already_AddRefed<RemoteBrowser> CreateBrowser(
      const TabContext& aContext, Element* aFrameElement,
      const nsACString& aRemoteType, BrowsingContext* aBrowsingContext,
      ContentParent* aOpenerContentParent);

  /**
   * Get all content parents.
   *
   * # Lifetime
   *
   * These pointers are ONLY valid for synchronous use from the main thread.
   *
   * Do NOT attempt to use them after the main thread has had a chance to handle
   * messages or you could end up with dangling pointers.
   */
  static void GetAll(nsTArray<ContentParent*>& aArray);

  static void GetAllEvenIfDead(nsTArray<ContentParent*>& aArray);

  static void BroadcastStringBundle(const StringBundleDescriptor&);

  static void BroadcastShmBlockAdded(uint32_t aGeneration, uint32_t aIndex);

  static void BroadcastThemeUpdate(widget::ThemeChangeKind);

  static void BroadcastMediaCodecsSupportedUpdate(
      RemoteDecodeIn aLocation, const media::MediaCodecsSupported& aSupported);

  const nsACString& GetRemoteType() const override;

  virtual void DoGetRemoteType(nsACString& aRemoteType,
                               ErrorResult& aError) const override {
    aRemoteType = GetRemoteType();
  }

  enum CPIteratorPolicy { eLive, eAll };

  class ContentParentIterator {
   private:
    ContentParent* mCurrent;
    CPIteratorPolicy mPolicy;

   public:
    ContentParentIterator(CPIteratorPolicy aPolicy, ContentParent* aCurrent)
        : mCurrent(aCurrent), mPolicy(aPolicy) {}

    ContentParentIterator begin() {
      // Move the cursor to the first element that matches the policy.
      while (mPolicy != eAll && mCurrent && !mCurrent->IsAlive()) {
        mCurrent = mCurrent->LinkedListElement<ContentParent>::getNext();
      }

      return *this;
    }
    ContentParentIterator end() {
      return ContentParentIterator(mPolicy, nullptr);
    }

    const ContentParentIterator& operator++() {
      MOZ_ASSERT(mCurrent);
      do {
        mCurrent = mCurrent->LinkedListElement<ContentParent>::getNext();
      } while (mPolicy != eAll && mCurrent && !mCurrent->IsAlive());

      return *this;
    }

    bool operator!=(const ContentParentIterator& aOther) const {
      MOZ_ASSERT(mPolicy == aOther.mPolicy);
      return mCurrent != aOther.mCurrent;
    }

    ContentParent* operator*() { return mCurrent; }
  };

  static ContentParentIterator AllProcesses(CPIteratorPolicy aPolicy) {
    ContentParent* first =
        sContentParents ? sContentParents->getFirst() : nullptr;
    return ContentParentIterator(aPolicy, first);
  }

  static void NotifyUpdatedDictionaries();

  // Tell content processes the font list has changed. If aFullRebuild is true,
  // the shared list has been rebuilt and must be freshly mapped by child
  // processes; if false, existing mappings are still valid but the data has
  // been updated and so full reflows are in order.
  static void NotifyUpdatedFonts(bool aFullRebuild);

  mozilla::ipc::IPCResult RecvCreateClipboardContentAnalysis(
      Endpoint<PClipboardContentAnalysisParent>&& aParentEndpoint);
  mozilla::ipc::IPCResult RecvCreateGMPService();

  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(ContentParent, nsIObserver)

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSIDOMPROCESSPARENT
  NS_DECL_NSIOBSERVER
  NS_DECL_NSIDOMGEOPOSITIONCALLBACK
  NS_DECL_NSIDOMGEOPOSITIONERRORCALLBACK
  NS_DECL_NSIASYNCSHUTDOWNBLOCKER
  NS_DECL_NSIINTERFACEREQUESTOR

  /**
   * MessageManagerCallback methods that we override.
   */
  virtual bool DoLoadMessageManagerScript(const nsAString& aURL,
                                          bool aRunInGlobalScope) override;

  virtual nsresult DoSendAsyncMessage(const nsAString& aMessage,
                                      StructuredCloneData& aData) override;

  /*
   * Attempt to add a KeepAlive for the given BrowserId. A KeepAlive will try to
   * keep the process alive, though it may still die (e.g. due to a crash,
   * explicit shutdown request, or similar).
   *
   * The returned `UniqueContentParentKeepAlive` will clear this KeepAlive when
   * destroyed, and acts as a strong pointer to this `ContentParent`.
   *
   * Returns nullptr if the process is already being shut down.
   */
  [[nodiscard]] UniqueContentParentKeepAlive TryAddKeepAlive(
      uint64_t aBrowserId);

  /*
   * Like `TryAddKeepAlive`, but never returns `nullptr`, instead asserting if
   * the ContentParent is already shutting down.
   */
  [[nodiscard]] UniqueContentParentKeepAlive AddKeepAlive(uint64_t aBrowserId);

  /**
   * Check if this process is ready to be shut down, and if it is, begin the
   * shutdown process.  Automatically called whenever a KeepAlive is removed, or
   * a BrowserParent is removed.
   *
   * By default when a process becomes unused, it will be kept alive for a short
   * time, potentially allowing the process to be re-used.
   *
   * @param aImmediate If true, immediately begins shutdown if the process is
   *                   eligible, without any grace period for process re-use.
   * @param aIgnoreKeepAlivePref If true, the dom.ipc.keepProcessesAlive.*
   *                             preferences will be ignored, for clean-up of
   *                             cached processes. Requires aImmediate.
   */
  void MaybeBeginShutDown(bool aImmediate = false,
                          bool aIgnoreKeepAlivePref = false);

  TestShellParent* CreateTestShell();

  bool DestroyTestShell(TestShellParent* aTestShell);

  TestShellParent* GetTestShellSingleton();

  void ReportChildAlreadyBlocked();

  bool RequestRunToCompletion();

  void UpdateCookieStatus(nsIChannel* aChannel);

  bool IsLaunching() const {
    return mLifecycleState == LifecycleState::LAUNCHING;
  }
  bool IsAlive() const override;
  bool IsInitialized() const;
  bool IsSignaledImpendingShutdown() const {
    return mIsSignaledImpendingShutdown;
  }
  bool IsShuttingDown() const {
    return IsDead() || IsSignaledImpendingShutdown();
  }
  bool IsDead() const { return mLifecycleState == LifecycleState::DEAD; }

  bool IsForBrowser() const { return mIsForBrowser; }

  GeckoChildProcessHost* Process() const { return mSubprocess; }

  mozilla::dom::ProcessMessageManager* GetMessageManager() const {
    return mMessageManager;
  }

  bool NeedsPermissionsUpdate(const nsACString& aPermissionKey) const;

  // Getter for which permission keys should signal that a content
  // process needs to know about the change of a permission with this as the
  // secondary key, like for 3rdPartyFrameStorage^https://secondary.com
  bool NeedsSecondaryKeyPermissionsUpdate(
      const nsACString& aPermissionKey) const;

  // Manage pending load states which have been sent to this process, and are
  // expected to be used to start a load imminently.
  already_AddRefed<nsDocShellLoadState> TakePendingLoadStateForId(
      uint64_t aLoadIdentifier);
  void StorePendingLoadState(nsDocShellLoadState* aLoadState);

  /**
   * Kill our subprocess and make sure it dies.  Should only be used
   * in emergency situations since it bypasses the normal shutdown
   * process.
   *
   * WARNING: aReason appears in telemetry, so any new value passed in requires
   * data review.
   */
  void KillHard(const char* aReason);

  /**
   * Get a unique identifier for this child process. This ID is unique across
   * all types of child processes.
   *
   * NOTE: The `ContentParentId` wrapper type is a historical artifact from when
   * the ID was only specific to content processes.
   */
  ContentParentId ChildID() const { return mChildID; }

  /**
   * Get a user-friendly name for this ContentParent.  We make no guarantees
   * about this name: It might not be unique, apps can spoof special names,
   * etc.  So please don't use this name to make any decisions about the
   * ContentParent based on the value returned here.
   */
  void FriendlyName(nsAString& aName, bool aAnonymize = false);

  mozilla::ipc::IPCResult RecvInitCrashReporter(
      const CrashReporter::CrashReporterInitArgs& aInitArgs);

  already_AddRefed<PNeckoParent> AllocPNeckoParent();

  virtual mozilla::ipc::IPCResult RecvPNeckoConstructor(
      PNeckoParent* aActor) override {
    return PContentParent::RecvPNeckoConstructor(aActor);
  }

  mozilla::ipc::IPCResult RecvInitStreamFilter(
      const uint64_t& aChannelId, const nsAString& aAddonId,
      InitStreamFilterResolver&& aResolver);

  PHalParent* AllocPHalParent();

  virtual mozilla::ipc::IPCResult RecvPHalConstructor(
      PHalParent* aActor) override {
    return PContentParent::RecvPHalConstructor(aActor);
  }

  mozilla::ipc::IPCResult RecvAttributionEvent(
      const nsACString& aHost, PrivateAttributionImpressionType aType,
      uint32_t aIndex, const nsAString& aAd, const nsACString& aTargetHost);
  mozilla::ipc::IPCResult RecvAttributionConversion(
      const nsACString& aHost, const nsAString& aTask, uint32_t aHistogramSize,
      const Maybe<uint32_t>& aLookbackDays,
      const Maybe<PrivateAttributionImpressionType>& aImpressionType,
      const nsTArray<nsString>& aAds, const nsTArray<nsCString>& aSourceHosts);

  PHeapSnapshotTempFileHelperParent* AllocPHeapSnapshotTempFileHelperParent();

  PRemoteSpellcheckEngineParent* AllocPRemoteSpellcheckEngineParent();

  bool CycleCollectWithLogs(bool aDumpAllTraces,
                            nsICycleCollectorLogSink* aSink,
                            nsIDumpGCAndCCLogsCallback* aCallback);

  mozilla::ipc::IPCResult RecvNotifyTabDestroying(const TabId& aTabId,
                                                  const ContentParentId& aCpId);

  mozilla::ipc::IPCResult RecvFinishShutdown();

  mozilla::ipc::IPCResult RecvNotifyShutdownSuccess();

  PContentPermissionRequestParent* AllocPContentPermissionRequestParent(
      const nsTArray<PermissionRequest>& aRequests, nsIPrincipal* aPrincipal,
      nsIPrincipal* aTopLevelPrincipal, const bool& aIsHandlingUserInput,
      const bool& aMaybeUnsafePermissionDelegate, const TabId& aTabId);

  bool DeallocPContentPermissionRequestParent(
      PContentPermissionRequestParent* actor);

  void ForkNewProcess(bool aBlocking);

  mozilla::ipc::IPCResult RecvCreateWindow(
      PBrowserParent* aThisTab, const MaybeDiscarded<BrowsingContext>& aParent,
      PBrowserParent* aNewTab, const uint32_t& aChromeFlags,
      const bool& aCalledFromJS, const bool& aForPrinting,
      const bool& aForWindowDotPrint, const bool& aTopLevelCreatedByWebContent,
      nsIURI* aURIToLoad, const nsACString& aFeatures,
      const UserActivation::Modifiers& aModifiers,
      nsIPrincipal* aTriggeringPrincipal, nsIContentSecurityPolicy* aCsp,
      nsIReferrerInfo* aReferrerInfo, const OriginAttributes& aOriginAttributes,
      bool aUserActivation, bool aTextDirectiveUserActivation,
      CreateWindowResolver&& aResolve);

  mozilla::ipc::IPCResult RecvCreateWindowInDifferentProcess(
      PBrowserParent* aThisTab, const MaybeDiscarded<BrowsingContext>& aParent,
      const uint32_t& aChromeFlags, const bool& aCalledFromJS,
      const bool& aTopLevelCreatedByWebContent, nsIURI* aURIToLoad,
      const nsACString& aFeatures, const UserActivation::Modifiers& aModifiers,
      const nsAString& aName, nsIPrincipal* aTriggeringPrincipal,
      nsIContentSecurityPolicy* aCsp, nsIReferrerInfo* aReferrerInfo,
      const OriginAttributes& aOriginAttributes, bool aUserActivation,
      bool aTextDirectiveUserActivation);

  static void BroadcastBlobURLRegistration(
      const nsACString& aURI, BlobImpl* aBlobImpl, nsIPrincipal* aPrincipal,
      const nsCString& aPartitionKey, ContentParent* aIgnoreThisCP = nullptr);

  static void BroadcastBlobURLUnregistration(
      const nsTArray<BroadcastBlobURLUnregistrationRequest>& aRequests,
      ContentParent* aIgnoreThisCP = nullptr);

  mozilla::ipc::IPCResult RecvStoreAndBroadcastBlobURLRegistration(
      const nsACString& aURI, const IPCBlob& aBlob, nsIPrincipal* aPrincipal,
      const nsCString& aPartitionKey);

  mozilla::ipc::IPCResult RecvUnstoreAndBroadcastBlobURLUnregistration(
      const nsTArray<BroadcastBlobURLUnregistrationRequest>& aRequests);

  virtual int32_t Pid() const override;

  // PURLClassifierParent.
  PURLClassifierParent* AllocPURLClassifierParent(nsIPrincipal* aPrincipal,
                                                  bool* aSuccess);
  virtual mozilla::ipc::IPCResult RecvPURLClassifierConstructor(
      PURLClassifierParent* aActor, nsIPrincipal* aPrincipal,
      bool* aSuccess) override;

  // PURLClassifierLocalParent.
  PURLClassifierLocalParent* AllocPURLClassifierLocalParent(
      nsIURI* aURI, const nsTArray<IPCURLClassifierFeature>& aFeatures);

  virtual mozilla::ipc::IPCResult RecvPURLClassifierLocalConstructor(
      PURLClassifierLocalParent* aActor, nsIURI* aURI,
      nsTArray<IPCURLClassifierFeature>&& aFeatures) override;

  // PURLClassifierLocalByNameParent.
  PURLClassifierLocalByNameParent* AllocPURLClassifierLocalByNameParent(
      nsIURI* aURI, const nsTArray<nsCString>& aFeatures,
      const nsIUrlClassifierFeature::listType& aListType);

  virtual mozilla::ipc::IPCResult RecvPURLClassifierLocalByNameConstructor(
      PURLClassifierLocalByNameParent* aActor, nsIURI* aURI,
      nsTArray<nsCString>&& aFeatureNames,
      const nsIUrlClassifierFeature::listType& aListType) override;

  PSessionStorageObserverParent* AllocPSessionStorageObserverParent();

  virtual mozilla::ipc::IPCResult RecvPSessionStorageObserverConstructor(
      PSessionStorageObserverParent* aActor) override;

  bool DeallocPSessionStorageObserverParent(
      PSessionStorageObserverParent* aActor);

  bool DeallocPURLClassifierLocalByNameParent(
      PURLClassifierLocalByNameParent* aActor);

  bool DeallocPURLClassifierLocalParent(PURLClassifierLocalParent* aActor);

  bool DeallocPURLClassifierParent(PURLClassifierParent* aActor);

  // Use the PHangMonitor channel to ask the child to repaint a tab.
  void PaintTabWhileInterruptingJS(BrowserParent*);

  void UnloadLayersWhileInterruptingJS(BrowserParent*);

  void CancelContentJSExecutionIfRunning(
      BrowserParent* aBrowserParent,
      nsIRemoteTab::NavigationType aNavigationType,
      const CancelContentJSOptions& aCancelContentJSOptions);

  void SetMainThreadQoSPriority(nsIThread::QoSPriority aQoSPriority);

  // This function is called when we are about to load a document from an
  // HTTP(S) channel for a content process.  It is a useful place
  // to start to kick off work as early as possible in response to such
  // document loads.
  // aShouldWaitForPermissionCookieUpdate is set to true if main thread IPCs for
  // updating permissions/cookies are sent.
  nsresult AboutToLoadHttpDocumentForChild(
      nsIChannel* aChannel,
      bool* aShouldWaitForPermissionCookieUpdate = nullptr);

  // Send Blob URLs for this aPrincipal if they are not already known to this
  // content process and mark the process to receive any new/revoked Blob URLs
  // to this content process forever.
  void TransmitBlobURLsForPrincipal(nsIPrincipal* aPrincipal);

  // Update a cache list of allowed domains to store cookies for the current
  // process. This method is called when PCookieServiceParent actor is not
  // available yet.
  void AddPrincipalToCookieInProcessCache(nsIPrincipal* aPrincipal);
  void TakeCookieInProcessCache(nsTArray<nsCOMPtr<nsIPrincipal>>& aList);

  nsresult TransmitPermissionsForPrincipal(nsIPrincipal* aPrincipal);

  // Whenever receiving a Principal we need to validate that Principal case
  // by case, where we grant individual callsites to customize the checks!
  bool ValidatePrincipal(
      nsIPrincipal* aPrincipal,
      const EnumSet<ValidatePrincipalOptions>& aOptions = {});

  // This function is called in BrowsingContext immediately before IPC call to
  // load a URI. If aURI is a BlobURL, this method transmits all BlobURLs for
  // aURI's principal that were previously not transmitted. This allows for
  // opening a locally created BlobURL in a new tab.
  //
  // The reason all previously untransmitted Blobs are transmitted is that the
  // current BlobURL could contain html code, referring to another untransmitted
  // BlobURL.
  //
  // Should eventually be made obsolete by broader design changes that only
  // store BlobURLs in the parent process.
  void TransmitBlobDataIfBlobURL(nsIURI* aURI);

  void OnCompositorDeviceReset() override;

  // Control the priority of the IPC messages for input events.
  void SetInputPriorityEventEnabled(bool aEnabled);
  bool IsInputPriorityEventEnabled() { return mIsInputPriorityEventEnabled; }

  mozilla::ipc::IPCResult RecvCreateBrowsingContext(
      uint64_t aGroupId, BrowsingContext::IPCInitializer&& aInit);

  mozilla::ipc::IPCResult RecvDiscardBrowsingContext(
      const MaybeDiscarded<BrowsingContext>& aContext, bool aDoDiscard,
      DiscardBrowsingContextResolver&& aResolve);

  mozilla::ipc::IPCResult RecvWindowClose(
      const MaybeDiscarded<BrowsingContext>& aContext, bool aTrustedCaller);
  mozilla::ipc::IPCResult RecvWindowFocus(
      const MaybeDiscarded<BrowsingContext>& aContext, CallerType aCallerType,
      uint64_t aActionId);
  mozilla::ipc::IPCResult RecvWindowBlur(
      const MaybeDiscarded<BrowsingContext>& aContext, CallerType aCallerType);
  mozilla::ipc::IPCResult RecvRaiseWindow(
      const MaybeDiscarded<BrowsingContext>& aContext, CallerType aCallerType,
      uint64_t aActionId);
  mozilla::ipc::IPCResult RecvAdjustWindowFocus(
      const MaybeDiscarded<BrowsingContext>& aContext, bool aIsVisible,
      uint64_t aActionId, bool aShouldClearFocus,
      const MaybeDiscarded<BrowsingContext>& aAncestorBrowsingContextToFocus);
  mozilla::ipc::IPCResult RecvClearFocus(
      const MaybeDiscarded<BrowsingContext>& aContext);
  mozilla::ipc::IPCResult RecvSetFocusedBrowsingContext(
      const MaybeDiscarded<BrowsingContext>& aContext, uint64_t aActionId);
  mozilla::ipc::IPCResult RecvSetActiveBrowsingContext(
      const MaybeDiscarded<BrowsingContext>& aContext, uint64_t aActionId);
  mozilla::ipc::IPCResult RecvUnsetActiveBrowsingContext(
      const MaybeDiscarded<BrowsingContext>& aContext, uint64_t aActionId);
  mozilla::ipc::IPCResult RecvSetFocusedElement(
      const MaybeDiscarded<BrowsingContext>& aContext, bool aNeedsFocus);
  mozilla::ipc::IPCResult RecvFinalizeFocusOuter(
      const MaybeDiscarded<BrowsingContext>& aContext, bool aCanFocus,
      CallerType aCallerType);
  mozilla::ipc::IPCResult RecvInsertNewFocusActionId(uint64_t aActionId);
  mozilla::ipc::IPCResult RecvBlurToParent(
      const MaybeDiscarded<BrowsingContext>& aFocusedBrowsingContext,
      const MaybeDiscarded<BrowsingContext>& aBrowsingContextToClear,
      const MaybeDiscarded<BrowsingContext>& aAncestorBrowsingContextToFocus,
      bool aIsLeavingDocument, bool aAdjustWidget,
      bool aBrowsingContextToClearHandled,
      bool aAncestorBrowsingContextToFocusHandled, uint64_t aActionId);
  mozilla::ipc::IPCResult RecvMaybeExitFullscreen(
      const MaybeDiscarded<BrowsingContext>& aContext);

  mozilla::ipc::IPCResult RecvWindowPostMessage(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const ClonedOrErrorMessageData& aMessage, const PostMessageData& aData);

  FORWARD_SHMEM_ALLOCATOR_TO(PContentParent)

  mozilla::ipc::IPCResult RecvBlobURLDataRequest(
      const nsACString& aBlobURL, nsIPrincipal* pTriggeringPrincipal,
      nsIPrincipal* pLoadingPrincipal,
      const OriginAttributes& aOriginAttributes, uint64_t aInnerWindowId,
      const nsCString& aPartitionKey, BlobURLDataRequestResolver&& aResolver);

 protected:
  bool CheckBrowsingContextEmbedder(CanonicalBrowsingContext* aBC,
                                    const char* aOperation) const;

  void ActorDestroy(ActorDestroyReason why) override;

  bool ShouldContinueFromReplyTimeout() override;

  void OnVarChanged(const GfxVarUpdate& aVar) override;
  void OnCompositorUnexpectedShutdown() override;

 private:
  /**
   * A map of the remote content process type to a list of content parents
   * currently available to host *new* tabs/frames of that type.
   *
   * If a content process is identified as troubled or dead, it will be
   * removed from this list, but will still be in the sContentParents list for
   * the GetAll/GetAllEvenIfDead APIs.
   */
  static nsClassHashtable<nsCStringHashKey, nsTArray<ContentParent*>>*
      sBrowserContentParents;
  static mozilla::StaticAutoPtr<LinkedList<ContentParent>> sContentParents;

  void AddShutdownBlockers();
  void RemoveShutdownBlockers();

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  // Cached Mac sandbox params used when launching content processes.
  static mozilla::StaticAutoPtr<std::vector<std::string>> sMacSandboxParams;
#endif

  // Set aLoadUri to true to load aURIToLoad and to false to only create the
  // window. aURIToLoad should always be provided, if available, to ensure
  // compatibility with GeckoView.
  mozilla::ipc::IPCResult CommonCreateWindow(
      PBrowserParent* aThisTab, BrowsingContext& aParent, bool aSetOpener,
      const uint32_t& aChromeFlags, const bool& aCalledFromJS,
      const bool& aForPrinting, const bool& aForWindowDotPrint,
      const bool& aIsTopLevelCreatedByWebContent, nsIURI* aURIToLoad,
      const nsACString& aFeatures, const UserActivation::Modifiers& aModifiers,
      BrowserParent* aNextRemoteBrowser, const nsAString& aName,
      nsresult& aResult, nsCOMPtr<nsIRemoteTab>& aNewRemoteTab,
      bool* aWindowIsNew, int32_t& aOpenLocation,
      nsIPrincipal* aTriggeringPrincipal, nsIReferrerInfo* aReferrerInfo,
      bool aLoadUri, nsIContentSecurityPolicy* aCsp,
      const OriginAttributes& aOriginAttributes, bool aUserActivation,
      bool aTextDirectiveUserActivation);

  explicit ContentParent(const nsACString& aRemoteType);

  // Common implementation of LaunchSubprocess{Sync,Async}.
  // Return `true` in case of success, `false` if launch was
  // aborted because of shutdown.
  bool BeginSubprocessLaunch(ProcessPriority aPriority);
  void LaunchSubprocessReject();
  bool LaunchSubprocessResolve(bool aIsSync, ProcessPriority aPriority);

  // Common initialization after sub process launch.
  bool InitInternal(ProcessPriority aPriority);

  // Generate a minidump for the child process and one for the main process
  void GeneratePairedMinidump(const char* aReason);
  void HandleOrphanedMinidump(nsString* aDumpId);

  virtual ~ContentParent();

  void Init();

  // Some information could be sent to content very early, it
  // should be send from this function. This function should only be
  // called after the process has been transformed to browser.
  void ForwardKnownInfo();

  /**
   * Removing it from the static array so it won't be returned for new tabs in
   * GetNewOrUsedBrowserProcess.
   */
  void RemoveFromList();

  /**
   * Mark this ContentParent as dead for the purposes of Get*().
   * This method is idempotent.
   */
  void MarkAsDead();

  /**
   * Let the process know we are about to send a shutdown through a
   * non-mainthread side channel in order to bypass mainthread congestion.
   * This potentially cancels mainthread content JS execution.
   */
  void SignalImpendingShutdownToContentJS();

  /**
   * How we will shut down this ContentParent and its subprocess.
   */
  enum ShutDownMethod {
    // Send a shutdown message and wait for FinishShutdown call back.
    SEND_SHUTDOWN_MESSAGE,
    // Close the channel ourselves and let the subprocess clean up itself.
    CLOSE_CHANNEL,
  };

  void AsyncSendShutDownMessage();

  /**
   * Exit the subprocess and vamoose.  After this call IsAlive()
   * will return false and this ContentParent will not be returned
   * by the Get*() funtions.  However, the shutdown sequence itself
   * may be asynchronous.
   */
  bool ShutDownProcess(ShutDownMethod aMethod);

  // Perform any steps necesssary to gracefully shtudown the message
  // manager and null out mMessageManager.
  void ShutDownMessageManager();

  // Start the send shutdown timer on shutdown.
  void StartSendShutdownTimer();

  // Start the force-kill timer on shutdown.
  void StartForceKillTimer();

  // Ensure that the permissions for the giben Permission key are set in the
  // content process.
  //
  // See nsIPermissionManager::GetPermissionsForKey for more information on
  // these keys.
  void EnsurePermissionsByKey(const nsACString& aKey,
                              const nsACString& aOrigin);

  static void SendShutdownTimerCallback(nsITimer* aTimer, void* aClosure);
  static void ForceKillTimerCallback(nsITimer* aTimer, void* aClosure);

  bool CanOpenBrowser(const IPCTabContext& aContext);

  /**
   * Get or create the corresponding content parent array to
   * |aContentProcessType|.
   */
  static nsTArray<ContentParent*>& GetOrCreatePool(
      const nsACString& aContentProcessType);

  mozilla::ipc::IPCResult RecvInitBackground(
      Endpoint<mozilla::ipc::PBackgroundStarterParent>&& aEndpoint);

  mozilla::ipc::IPCResult RecvAddMemoryReport(const MemoryReport& aReport);

  bool DeallocPRemoteSpellcheckEngineParent(PRemoteSpellcheckEngineParent*);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvCloneDocumentTreeInto(
      const MaybeDiscarded<BrowsingContext>& aSource,
      const MaybeDiscarded<BrowsingContext>& aTarget, PrintData&& aPrintData);

  mozilla::ipc::IPCResult RecvUpdateRemotePrintSettings(
      const MaybeDiscarded<BrowsingContext>& aTarget, PrintData&& aPrintData);

  mozilla::ipc::IPCResult RecvConstructPopupBrowser(
      ManagedEndpoint<PBrowserParent>&& aBrowserEp,
      ManagedEndpoint<PWindowGlobalParent>&& windowEp, const TabId& tabId,
      const IPCTabContext& context, const WindowGlobalInit& initialWindowInit,
      const uint32_t& chromeFlags);

  mozilla::ipc::IPCResult RecvIsSecureURI(
      nsIURI* aURI, const OriginAttributes& aOriginAttributes,
      bool* aIsSecureURI);

  mozilla::ipc::IPCResult RecvAccumulateMixedContentHSTS(
      nsIURI* aURI, const bool& aActive,
      const OriginAttributes& aOriginAttributes);

  bool DeallocPHalParent(PHalParent*);

  bool DeallocPHeapSnapshotTempFileHelperParent(
      PHeapSnapshotTempFileHelperParent*);

  PCycleCollectWithLogsParent* AllocPCycleCollectWithLogsParent(
      const bool& aDumpAllTraces, const FileDescriptor& aGCLog,
      const FileDescriptor& aCCLog);

  bool DeallocPCycleCollectWithLogsParent(PCycleCollectWithLogsParent* aActor);

  PScriptCacheParent* AllocPScriptCacheParent(const FileDescOrError& cacheFile,
                                              const bool& wantCacheData);

  bool DeallocPScriptCacheParent(PScriptCacheParent* shell);

  already_AddRefed<PExternalHelperAppParent> AllocPExternalHelperAppParent(
      nsIURI* aUri, const mozilla::net::LoadInfoArgs& aLoadInfoArgs,
      const nsACString& aMimeContentType, const nsACString& aContentDisposition,
      const uint32_t& aContentDispositionHint,
      const nsAString& aContentDispositionFilename, const bool& aForceSave,
      const int64_t& aContentLength, const bool& aWasFileChannel,
      nsIURI* aReferrer, const MaybeDiscarded<BrowsingContext>& aContext);

  mozilla::ipc::IPCResult RecvPExternalHelperAppConstructor(
      PExternalHelperAppParent* actor, nsIURI* uri,
      const LoadInfoArgs& loadInfoArgs, const nsACString& aMimeContentType,
      const nsACString& aContentDisposition,
      const uint32_t& aContentDispositionHint,
      const nsAString& aContentDispositionFilename, const bool& aForceSave,
      const int64_t& aContentLength, const bool& aWasFileChannel,
      nsIURI* aReferrer,
      const MaybeDiscarded<BrowsingContext>& aContext) override;

  already_AddRefed<PHandlerServiceParent> AllocPHandlerServiceParent();

  PMediaParent* AllocPMediaParent();

  bool DeallocPMediaParent(PMediaParent* aActor);

#ifdef MOZ_WEBSPEECH
  already_AddRefed<PSpeechSynthesisParent> AllocPSpeechSynthesisParent();

  virtual mozilla::ipc::IPCResult RecvPSpeechSynthesisConstructor(
      PSpeechSynthesisParent* aActor) override;
#endif

  already_AddRefed<PWebBrowserPersistDocumentParent>
  AllocPWebBrowserPersistDocumentParent(
      PBrowserParent* aBrowser,
      const MaybeDiscarded<BrowsingContext>& aContext);

  mozilla::ipc::IPCResult RecvSetClipboard(
      const IPCTransferable& aTransferable,
      const nsIClipboard::ClipboardType& aWhichClipboard,
      const MaybeDiscarded<WindowContext>& aRequestingWindowContext);

  mozilla::ipc::IPCResult RecvGetClipboard(
      nsTArray<nsCString>&& aTypes,
      const nsIClipboard::ClipboardType& aWhichClipboard,
      const MaybeDiscarded<WindowContext>& aRequestingWindowContext,
      IPCTransferableDataOrError* aTransferableDataOrError);

  mozilla::ipc::IPCResult RecvEmptyClipboard(
      const nsIClipboard::ClipboardType& aWhichClipboard);

  mozilla::ipc::IPCResult RecvClipboardHasType(
      nsTArray<nsCString>&& aTypes,
      const nsIClipboard::ClipboardType& aWhichClipboard, bool* aHasType);

  mozilla::ipc::IPCResult RecvGetClipboardDataSnapshot(
      nsTArray<nsCString>&& aTypes,
      const nsIClipboard::ClipboardType& aWhichClipboard,
      const MaybeDiscarded<WindowContext>& aRequestingWindowContext,
      mozilla::NotNull<nsIPrincipal*> aRequestingPrincipal,
      GetClipboardDataSnapshotResolver&& aResolver);

  mozilla::ipc::IPCResult RecvGetClipboardDataSnapshotSync(
      nsTArray<nsCString>&& aTypes,
      const nsIClipboard::ClipboardType& aWhichClipboard,
      const MaybeDiscarded<WindowContext>& aRequestingWindowContext,
      ClipboardReadRequestOrError* aRequestOrError);

  already_AddRefed<PClipboardWriteRequestParent>
  AllocPClipboardWriteRequestParent(
      const nsIClipboard::ClipboardType& aClipboardType,
      const MaybeDiscarded<WindowContext>& aSettingWindowContext);

  mozilla::ipc::IPCResult RecvGetIconForExtension(const nsACString& aFileExt,
                                                  const uint32_t& aIconSize,
                                                  nsTArray<uint8_t>* bits);

  mozilla::ipc::IPCResult RecvStartVisitedQueries(
      const nsTArray<RefPtr<nsIURI>>&);

  mozilla::ipc::IPCResult RecvSetURITitle(nsIURI* uri, const nsAString& title);

  mozilla::ipc::IPCResult RecvLoadURIExternal(
      nsIURI* uri, nsIPrincipal* triggeringPrincipal,
      nsIPrincipal* redirectPrincipal,
      const MaybeDiscarded<BrowsingContext>& aContext,
      bool aWasExternallyTriggered, bool aHasValidUserGestureActivation,
      bool aNewWindowTarget);
  mozilla::ipc::IPCResult RecvExtProtocolChannelConnectParent(
      const uint64_t& registrarId);

  mozilla::ipc::IPCResult RecvSyncMessage(
      const nsAString& aMsg, const ClonedMessageData& aData,
      nsTArray<StructuredCloneData>* aRetvals);

  mozilla::ipc::IPCResult RecvAsyncMessage(const nsAString& aMsg,
                                           const ClonedMessageData& aData);

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY because we don't have MOZ_CAN_RUN_SCRIPT bits
  // in IPC code yet.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvAddGeolocationListener(const bool& aHighAccuracy);
  mozilla::ipc::IPCResult RecvRemoveGeolocationListener();

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY because we don't have MOZ_CAN_RUN_SCRIPT bits
  // in IPC code yet.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvSetGeolocationHigherAccuracy(const bool& aEnable);

  mozilla::ipc::IPCResult RecvConsoleMessage(const nsAString& aMessage);

  mozilla::ipc::IPCResult RecvScriptError(
      const nsAString& aMessage, const nsACString& aSourceName,
      const uint32_t& aLineNumber, const uint32_t& aColNumber,
      const uint32_t& aFlags, const nsACString& aCategory,
      const bool& aIsFromPrivateWindow, const uint64_t& aInnerWindowId,
      const bool& aIsFromChromeContext);

  mozilla::ipc::IPCResult RecvReportFrameTimingData(
      const LoadInfoArgs& loadInfoArgs, const nsAString& entryName,
      const nsAString& initiatorType, UniquePtr<PerformanceTimingData>&& aData);

  mozilla::ipc::IPCResult RecvScriptErrorWithStack(
      const nsAString& aMessage, const nsACString& aSourceName,
      const uint32_t& aLineNumber, const uint32_t& aColNumber,
      const uint32_t& aFlags, const nsACString& aCategory,
      const bool& aIsFromPrivateWindow, const bool& aIsFromChromeContext,
      const ClonedMessageData& aStack);

 private:
  mozilla::ipc::IPCResult RecvScriptErrorInternal(
      const nsAString& aMessage, const nsACString& aSourceName,
      const uint32_t& aLineNumber, const uint32_t& aColNumber,
      const uint32_t& aFlags, const nsACString& aCategory,
      const bool& aIsFromPrivateWindow, const bool& aIsFromChromeContext,
      const ClonedMessageData* aStack = nullptr);

 public:
  mozilla::ipc::IPCResult RecvCommitBrowsingContextTransaction(
      const MaybeDiscarded<BrowsingContext>& aContext,
      BrowsingContext::BaseTransaction&& aTransaction, uint64_t aEpoch);

  mozilla::ipc::IPCResult RecvCommitWindowContextTransaction(
      const MaybeDiscarded<WindowContext>& aContext,
      WindowContext::BaseTransaction&& aTransaction, uint64_t aEpoch);

  mozilla::ipc::IPCResult RecvAddSecurityState(
      const MaybeDiscarded<WindowContext>& aContext, uint32_t aStateFlags);

  mozilla::ipc::IPCResult RecvFirstIdle();

  mozilla::ipc::IPCResult RecvDeviceReset();

  mozilla::ipc::IPCResult RecvCopyFavicon(nsIURI* aOldURI, nsIURI* aNewURI,
                                          const bool& aInPrivateBrowsing);

  mozilla::ipc::IPCResult RecvFindImageText(IPCImage&&, nsTArray<nsCString>&&,
                                            FindImageTextResolver&&);

  virtual void ProcessingError(Result aCode, const char* aMsgName) override;

  mozilla::ipc::IPCResult RecvGraphicsError(const nsACString& aError);

  mozilla::ipc::IPCResult RecvBeginDriverCrashGuard(const uint32_t& aGuardType,
                                                    bool* aOutCrashed);

  mozilla::ipc::IPCResult RecvEndDriverCrashGuard(const uint32_t& aGuardType);

  mozilla::ipc::IPCResult RecvAddIdleObserver(const uint64_t& aObserverId,
                                              const uint32_t& aIdleTimeInS);

  mozilla::ipc::IPCResult RecvRemoveIdleObserver(const uint64_t& aObserverId,
                                                 const uint32_t& aIdleTimeInS);

  mozilla::ipc::IPCResult RecvBackUpXResources(
      const FileDescriptor& aXSocketFd);

  mozilla::ipc::IPCResult RecvRequestAnonymousTemporaryFile(
      const uint64_t& aID);

  mozilla::ipc::IPCResult RecvCreateAudioIPCConnection(
      CreateAudioIPCConnectionResolver&& aResolver);

  already_AddRefed<extensions::PExtensionsParent> AllocPExtensionsParent();

#ifdef MOZ_WEBRTC
  PWebrtcGlobalParent* AllocPWebrtcGlobalParent();
  bool DeallocPWebrtcGlobalParent(PWebrtcGlobalParent* aActor);
#endif

  mozilla::ipc::IPCResult RecvShutdownProfile(const nsACString& aProfile);

  mozilla::ipc::IPCResult RecvShutdownPerfStats(const nsACString& aPerfStats);

  mozilla::ipc::IPCResult RecvGetFontListShmBlock(
      const uint32_t& aGeneration, const uint32_t& aIndex,
      mozilla::ipc::ReadOnlySharedMemoryHandle* aOut);

  mozilla::ipc::IPCResult RecvInitializeFamily(const uint32_t& aGeneration,
                                               const uint32_t& aFamilyIndex,
                                               const bool& aLoadCmaps);

  mozilla::ipc::IPCResult RecvSetCharacterMap(const uint32_t& aGeneration,
                                              const uint32_t& aFamilyIndex,
                                              const bool& aAlias,
                                              const uint32_t& aFaceIndex,
                                              const gfxSparseBitSet& aMap);

  mozilla::ipc::IPCResult RecvInitOtherFamilyNames(const uint32_t& aGeneration,
                                                   const bool& aDefer,
                                                   bool* aLoaded);

  mozilla::ipc::IPCResult RecvSetupFamilyCharMap(const uint32_t& aGeneration,
                                                 const uint32_t& aIndex,
                                                 const bool& aAlias);

  mozilla::ipc::IPCResult RecvStartCmapLoading(const uint32_t& aGeneration,
                                               const uint32_t& aStartIndex);

  mozilla::ipc::IPCResult RecvGetHyphDict(
      nsIURI* aURIParams, mozilla::ipc::ReadOnlySharedMemoryHandle* aOutHandle);

  mozilla::ipc::IPCResult RecvNotifyPushObservers(const nsACString& aScope,
                                                  nsIPrincipal* aPrincipal,
                                                  const nsAString& aMessageId);

  mozilla::ipc::IPCResult RecvNotifyPushObserversWithData(
      const nsACString& aScope, nsIPrincipal* aPrincipal,
      const nsAString& aMessageId, nsTArray<uint8_t>&& aData);

  mozilla::ipc::IPCResult RecvPushError(const nsACString& aScope,
                                        nsIPrincipal* aPrincipal,
                                        const nsAString& aMessage,
                                        const uint32_t& aFlags);

  mozilla::ipc::IPCResult RecvNotifyPushSubscriptionModifiedObservers(
      const nsACString& aScope, nsIPrincipal* aPrincipal);

  mozilla::ipc::IPCResult RecvGetFilesRequest(
      const nsID& aID, nsTArray<nsString>&& aDirectoryPaths,
      const bool& aRecursiveFlag);

  mozilla::ipc::IPCResult RecvDeleteGetFilesRequest(const nsID& aID);

  mozilla::ipc::IPCResult RecvAccumulateChildHistograms(
      nsTArray<HistogramAccumulation>&& aAccumulations);
  mozilla::ipc::IPCResult RecvAccumulateChildKeyedHistograms(
      nsTArray<KeyedHistogramAccumulation>&& aAccumulations);
  mozilla::ipc::IPCResult RecvUpdateChildScalars(
      nsTArray<ScalarAction>&& aScalarActions);
  mozilla::ipc::IPCResult RecvUpdateChildKeyedScalars(
      nsTArray<KeyedScalarAction>&& aScalarActions);
  mozilla::ipc::IPCResult RecvRecordChildEvents(
      nsTArray<ChildEventData>&& events);
  mozilla::ipc::IPCResult RecvRecordDiscardedData(
      const DiscardedData& aDiscardedData);
  mozilla::ipc::IPCResult RecvRecordPageLoadEvent(
      mozilla::glean::perf::PageLoadExtra&& aPageLoadEventExtra);
  mozilla::ipc::IPCResult RecvRecordOrigin(const uint32_t& aMetricId,
                                           const nsACString& aOrigin);
  mozilla::ipc::IPCResult RecvReportContentBlockingLog(
      const IPCStream& aIPCStream);

  mozilla::ipc::IPCResult RecvBHRThreadHang(const HangDetails& aHangDetails);

  mozilla::ipc::IPCResult RecvAddCertException(
      nsIX509Cert* aCert, const nsACString& aHostName, int32_t aPort,
      const OriginAttributes& aOriginAttributes, bool aIsTemporary,
      AddCertExceptionResolver&& aResolver);

  mozilla::ipc::IPCResult RecvAutomaticStorageAccessPermissionCanBeGranted(
      nsIPrincipal* aPrincipal,
      AutomaticStorageAccessPermissionCanBeGrantedResolver&& aResolver);

  mozilla::ipc::IPCResult RecvStorageAccessPermissionGrantedForOrigin(
      uint64_t aTopLevelWindowId,
      const MaybeDiscarded<BrowsingContext>& aParentContext,
      nsIPrincipal* aTrackingPrincipal, const nsACString& aTrackingOrigin,
      const int& aAllowMode,
      const Maybe<
          ContentBlockingNotifier::StorageAccessPermissionGrantedReason>&
          aReason,
      const bool& aFrameOnly,
      StorageAccessPermissionGrantedForOriginResolver&& aResolver);

  mozilla::ipc::IPCResult RecvCompleteAllowAccessFor(
      const MaybeDiscarded<BrowsingContext>& aParentContext,
      uint64_t aTopLevelWindowId, nsIPrincipal* aTrackingPrincipal,
      const nsACString& aTrackingOrigin, uint32_t aCookieBehavior,
      const ContentBlockingNotifier::StorageAccessPermissionGrantedReason&
          aReason,
      CompleteAllowAccessForResolver&& aResolver);

  mozilla::ipc::IPCResult RecvSetAllowStorageAccessRequestFlag(
      nsIPrincipal* aEmbeddedPrincipal, nsIURI* aEmbeddingOrigin,
      SetAllowStorageAccessRequestFlagResolver&& aResolver);

  mozilla::ipc::IPCResult RecvTestAllowStorageAccessRequestFlag(
      nsIPrincipal* aEmbeddingPrincipal, nsIURI* aEmbeddedOrigin,
      TestAllowStorageAccessRequestFlagResolver&& aResolver);

  mozilla::ipc::IPCResult RecvStoreUserInteractionAsPermission(
      nsIPrincipal* aPrincipal);

  mozilla::ipc::IPCResult RecvTestCookiePermissionDecided(
      const MaybeDiscarded<BrowsingContext>& aContext, nsIPrincipal* aPrincipal,
      const TestCookiePermissionDecidedResolver&& aResolver);

  mozilla::ipc::IPCResult RecvTestStorageAccessPermission(
      nsIPrincipal* aEmbeddingPrincipal, const nsCString& aEmbeddedOrigin,
      const TestStorageAccessPermissionResolver&& aResolver);

  mozilla::ipc::IPCResult RecvNotifyMediaPlaybackChanged(
      const MaybeDiscarded<BrowsingContext>& aContext,
      MediaPlaybackState aState);

  mozilla::ipc::IPCResult RecvNotifyMediaAudibleChanged(
      const MaybeDiscarded<BrowsingContext>& aContext,
      MediaAudibleState aState);

  mozilla::ipc::IPCResult RecvNotifyPictureInPictureModeChanged(
      const MaybeDiscarded<BrowsingContext>& aContext, bool aEnabled);

  mozilla::ipc::IPCResult RecvNotifyMediaSessionUpdated(
      const MaybeDiscarded<BrowsingContext>& aContext, bool aIsCreated);

  mozilla::ipc::IPCResult RecvNotifyUpdateMediaMetadata(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const Maybe<MediaMetadataBase>& aMetadata);

  mozilla::ipc::IPCResult RecvNotifyMediaSessionPlaybackStateChanged(
      const MaybeDiscarded<BrowsingContext>& aContext,
      MediaSessionPlaybackState aPlaybackState);

  mozilla::ipc::IPCResult RecvNotifyMediaSessionSupportedActionChanged(
      const MaybeDiscarded<BrowsingContext>& aContext,
      MediaSessionAction aAction, bool aEnabled);

  mozilla::ipc::IPCResult RecvNotifyMediaFullScreenState(
      const MaybeDiscarded<BrowsingContext>& aContext, bool aIsInFullScreen);

  mozilla::ipc::IPCResult RecvNotifyPositionStateChanged(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const Maybe<PositionState>& aState);

  mozilla::ipc::IPCResult RecvNotifyGuessedPositionStateChanged(
      const MaybeDiscarded<BrowsingContext>& aContext, const nsID& aMediaId,
      const Maybe<PositionState>& aState);

  mozilla::ipc::IPCResult RecvAddOrRemovePageAwakeRequest(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const bool& aShouldAddCount);

#if defined(XP_WIN)
  mozilla::ipc::IPCResult RecvGetModulesTrust(
      ModulePaths&& aModPaths, bool aRunAtNormalPriority,
      GetModulesTrustResolver&& aResolver);
#endif  // defined(XP_WIN)

  mozilla::ipc::IPCResult RecvReportServiceWorkerShutdownProgress(
      uint32_t aShutdownStateId,
      ServiceWorkerShutdownState::Progress aProgress);

  mozilla::ipc::IPCResult RecvRawMessage(
      const JSActorMessageMeta& aMeta, const Maybe<ClonedMessageData>& aData,
      const Maybe<ClonedMessageData>& aStack);

  mozilla::ipc::IPCResult RecvAbortOtherOrientationPendingPromises(
      const MaybeDiscarded<BrowsingContext>& aContext);

  mozilla::ipc::IPCResult RecvNotifyOnHistoryReload(
      const MaybeDiscarded<BrowsingContext>& aContext, const bool& aForceReload,
      NotifyOnHistoryReloadResolver&& aResolver);

  mozilla::ipc::IPCResult RecvHistoryCommit(
      const MaybeDiscarded<BrowsingContext>& aContext, const uint64_t& aLoadID,
      const nsID& aChangeID, const uint32_t& aLoadType, const bool& aPersist,
      const bool& aCloneEntryChildren, const bool& aChannelExpired,
      const uint32_t& aCacheKey);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvHistoryGo(
      const MaybeDiscarded<BrowsingContext>& aContext, int32_t aOffset,
      uint64_t aHistoryEpoch, bool aRequireUserInteraction,
      bool aUserActivation, HistoryGoResolver&& aResolveRequestedIndex);

  mozilla::ipc::IPCResult RecvSynchronizeLayoutHistoryState(
      const MaybeDiscarded<BrowsingContext>& aContext,
      nsILayoutHistoryState* aState);

  mozilla::ipc::IPCResult RecvSessionHistoryEntryTitle(
      const MaybeDiscarded<BrowsingContext>& aContext, const nsAString& aTitle);

  mozilla::ipc::IPCResult RecvSessionHistoryEntryScrollRestorationIsManual(
      const MaybeDiscarded<BrowsingContext>& aContext, const bool& aIsManual);

  mozilla::ipc::IPCResult RecvSessionHistoryEntryScrollPosition(
      const MaybeDiscarded<BrowsingContext>& aContext, const int32_t& aX,
      const int32_t& aY);

  mozilla::ipc::IPCResult RecvSessionHistoryEntryCacheKey(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const uint32_t& aCacheKey);

  mozilla::ipc::IPCResult RecvSessionHistoryEntryWireframe(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const Wireframe& aWireframe);

  mozilla::ipc::IPCResult
  RecvSessionHistoryEntryStoreWindowNameInContiguousEntries(
      const MaybeDiscarded<BrowsingContext>& aContext, const nsAString& aName);

  mozilla::ipc::IPCResult RecvGetLoadingSessionHistoryInfoFromParent(
      const MaybeDiscarded<BrowsingContext>& aContext,
      GetLoadingSessionHistoryInfoFromParentResolver&& aResolver);

  mozilla::ipc::IPCResult RecvGetContiguousSessionHistoryInfos(
      const MaybeDiscarded<BrowsingContext>& aContext,
      SessionHistoryInfo&& aInfo,
      GetContiguousSessionHistoryInfosResolver&& aResolver);

  mozilla::ipc::IPCResult RecvRemoveFromBFCache(
      const MaybeDiscarded<BrowsingContext>& aContext);

  mozilla::ipc::IPCResult RecvSetActiveSessionHistoryEntry(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const Maybe<nsPoint>& aPreviousScrollPos, SessionHistoryInfo&& aInfo,
      uint32_t aLoadType, uint32_t aUpdatedCacheKey, const nsID& aChangeID);

  mozilla::ipc::IPCResult RecvReplaceActiveSessionHistoryEntry(
      const MaybeDiscarded<BrowsingContext>& aContext,
      SessionHistoryInfo&& aInfo);

  mozilla::ipc::IPCResult RecvRemoveDynEntriesFromActiveSessionHistoryEntry(
      const MaybeDiscarded<BrowsingContext>& aContext);

  mozilla::ipc::IPCResult RecvRemoveFromSessionHistory(
      const MaybeDiscarded<BrowsingContext>& aContext, const nsID& aChangeID);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvHistoryReload(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const uint32_t aReloadFlags);

  mozilla::ipc::IPCResult RecvCleanupPendingLoadState(uint64_t aLoadIdentifier);

  // Notify the ContentChild to enable the input event prioritization when
  // initializing.
  void MaybeEnableRemoteInputEventQueue();

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  void AppendSandboxParams(std::vector<std::string>& aArgs);
  void AppendDynamicSandboxParams(std::vector<std::string>& aArgs);
#endif

  mozilla::ipc::IPCResult RecvFOGData(ByteBuf&& buf);

  mozilla::ipc::IPCResult RecvSetContainerFeaturePolicy(
      const MaybeDiscardedBrowsingContext& aContainerContext,
      MaybeFeaturePolicyInfo&& aContainerFeaturePolicyInfo);

  mozilla::ipc::IPCResult RecvGetSystemIcon(nsIURI* aURI,
                                            GetSystemIconResolver&& aResolver);

  mozilla::ipc::IPCResult RecvGetSystemGeolocationPermissionBehavior(
      GetSystemGeolocationPermissionBehaviorResolver&& aResolver);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY mozilla::ipc::IPCResult
  RecvRequestGeolocationPermissionFromUser(
      const MaybeDiscardedBrowsingContext& aBrowsingContext,
      RequestGeolocationPermissionFromUserResolver&& aResolver);

#ifdef FUZZING_SNAPSHOT
  mozilla::ipc::IPCResult RecvSignalFuzzingReady();
#endif

 public:
  void SendGetFilesResponseAndForget(const nsID& aID,
                                     const GetFilesResponseResult& aResult);

  bool SendRequestMemoryReport(const uint32_t& aGeneration,
                               const bool& aAnonymize,
                               const bool& aMinimizeMemoryUsage,
                               const Maybe<FileDescriptor>& aDMDFile) override;

  void AddBrowsingContextGroup(BrowsingContextGroup* aGroup);
  void RemoveBrowsingContextGroup(BrowsingContextGroup* aGroup);

  // See `BrowsingContext::mEpochs` for an explanation of this field.
  uint64_t GetBrowsingContextFieldEpoch() const {
    return mBrowsingContextFieldEpoch;
  }

  void UpdateNetworkLinkType();

  already_AddRefed<JSActor> InitJSActor(JS::Handle<JSObject*> aMaybeActor,
                                        const nsACString& aName,
                                        ErrorResult& aRv) override;
  mozilla::ipc::IProtocol* AsNativeActor() override { return this; }

  static already_AddRefed<nsIPrincipal> CreateRemoteTypeIsolationPrincipal(
      const nsACString& aRemoteType);

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  bool IsBlockingShutdown() { return mBlockShutdownCalled; }
#endif

  ThreadsafeContentParentHandle* ThreadsafeHandle() const {
    return mThreadsafeHandle;
  }

  RemoteWorkerServiceParent* GetRemoteWorkerServiceParent() const {
    return mRemoteWorkerServiceActor;
  }

 private:
  // Return an existing ContentParent if possible. Otherwise, `nullptr`.
  static UniqueContentParentKeepAlive GetUsedBrowserProcess(
      const nsACString& aRemoteType, nsTArray<ContentParent*>& aContentParents,
      uint32_t aMaxContentParents, bool aPreferUsed, ProcessPriority aPriority,
      uint64_t aBrowserId);

  void AddToPool(nsTArray<ContentParent*>&);
  void RemoveFromPool(nsTArray<ContentParent*>&);
  void AssertNotInPool();

  void RemoveKeepAlive(uint64_t aBrowserId);

  void AssertAlive();

  void StartRemoteWorkerService();

 private:
  // If you add strong pointers to cycle collected objects here, be sure to
  // release these objects in ShutDownProcess.  See the comment there for more
  // details.

  GeckoChildProcessHost* mSubprocess;
  const TimeStamp mLaunchTS;  // used to calculate time to start content process
  TimeStamp mLaunchYieldTS;   // used to calculate async launch main thread time
  TimeStamp mActivateTS;

  bool mIsAPreallocBlocker;  // We called AddBlocker for this ContentParent

  nsCString mRemoteType;
  nsCString mProfile;
  nsCOMPtr<nsIPrincipal> mRemoteTypeIsolationPrincipal;

  ContentParentId mChildID;
  int32_t mGeolocationWatchID;

  // After we destroy the last Browser, we also start a timer to ensure
  // that even content processes that are not responding will get a
  // second chance and a shutdown message.
  nsCOMPtr<nsITimer> mSendShutdownTimer;
  bool mSentShutdownMessage = false;

  // After we initiate shutdown, we also start a timer to ensure
  // that even content processes that are 100% blocked (say from
  // SIGSTOP), are still killed eventually.  This task enforces that
  // timer.
  nsCOMPtr<nsITimer> mForceKillTimer;

  // Threadsafe handle object which can be used by actors like PBackground to
  // track the identity and other relevant information about the content process
  // they're attached to.
  const RefPtr<ThreadsafeContentParentHandle> mThreadsafeHandle;

  // The process starts in the LAUNCHING state, and transitions to
  // ALIVE once it can accept IPC messages.  It remains ALIVE only
  // while remote content is being actively used from this process.
  // After the state becaomes DEAD, some previously scheduled IPC
  // traffic may still pass through.
  enum class LifecycleState : uint8_t {
    LAUNCHING,
    ALIVE,
    INITIALIZED,
    DEAD,
  };

  LifecycleState mLifecycleState;

  uint8_t mIsForBrowser : 1;

  // These variables track whether we've called Close() and KillHard() on our
  // channel.
  uint8_t mCalledClose : 1;
  uint8_t mCalledKillHard : 1;
  uint8_t mCreatedPairedMinidumps : 1;
  uint8_t mShutdownPending : 1;

  // Whether or not `LaunchSubprocessResolve` has been called, and whether or
  // not it returned `true` when called.
  uint8_t mLaunchResolved : 1;
  uint8_t mLaunchResolvedOk : 1;

  // True if the input event queue on the main thread of the content process is
  // enabled.
  uint8_t mIsRemoteInputEventQueueEnabled : 1;

  // True if we send input events with input priority. Otherwise, we send input
  // events with normal priority.
  uint8_t mIsInputPriorityEventEnabled : 1;

  uint8_t mIsInPool : 1;

  // True if we already created a GMP service.
  uint8_t mGMPCreated : 1;
  // True if we already created the
  // ClipboardContentAnalysis actor
  uint8_t mClipboardContentAnalysisCreated : 1;

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  bool mBlockShutdownCalled;
#endif

  nsTArray<nsCOMPtr<nsIObserver>> mIdleListeners;

#ifdef MOZ_X11
  // Dup of child's X socket, used to scope its resources to this
  // object instead of the child process's lifetime.
  UniqueFileHandle mChildXSocketFdDup;
#endif

  RefPtr<PProcessHangMonitorParent> mHangMonitorActor;

  RefPtr<RemoteWorkerServiceParent> mRemoteWorkerServiceActor;

  RefPtr<RemoteWorkerDebuggerManagerParent> mRemoteWorkerDebuggerManagerActor;

  UniquePtr<gfx::DriverCrashGuard> mDriverCrashGuard;
  UniquePtr<MemoryReportRequestHost> mMemoryReportRequest;

#if defined(XP_LINUX) && defined(MOZ_SANDBOX)
  RefPtr<SandboxBroker> mSandboxBroker;
  static mozilla::StaticAutoPtr<SandboxBrokerPolicyFactory>
      sSandboxBrokerPolicyFactory;
#endif

  // This hashtable is used to run GetFilesHelper objects in the parent process.
  // GetFilesHelper can be aborted by receiving RecvDeleteGetFilesRequest.
  nsRefPtrHashtable<nsIDHashKey, GetFilesHelper> mGetFilesPendingRequests;

  nsTHashSet<nsCString> mActivePermissionKeys;
  nsTHashSet<nsCString> mActiveSecondaryPermissionKeys;

  nsTArray<nsCOMPtr<nsIPrincipal>> mCookieInContentListCache;

  // This is intended to be a memory and time efficient means of determining
  // whether an origin has ever existed in a process so that Blob URL broadcast
  // doesn't need to transmit every Blob URL to every content process. False
  // positives are acceptable because receiving a Blob URL does not grant access
  // to its contents, and the act of creating/revoking a Blob is currently
  // viewed as an acceptable side-channel leak. In the future bug 1491018 will
  // moot the need for this structure.
  nsTArray<uint64_t> mLoadedOriginHashes;

  UniquePtr<mozilla::ipc::CrashReporterHost> mCrashReporter;

  // Collects any pref changes that occur during process launch (after
  // the initial map is passed in command-line arguments) to be sent
  // when the process can receive IPC messages.
  nsTArray<Pref> mQueuedPrefs;

  RefPtr<mozilla::dom::ProcessMessageManager> mMessageManager;

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  // When set to true, indicates that content processes should
  // initialize their sandbox during startup instead of waiting
  // for the SetProcessSandbox IPDL message.
  static bool sEarlySandboxInit;
#endif

  nsTHashSet<RefPtr<BrowsingContextGroup>> mGroups;

  // When we request a content process to load a document on our behalf, we'll
  // record the nsDocShellLoadState we sent to the content process mapped by the
  // load ID. If the load is then requested from the content process, we can
  // compare the load state and ensure it matches.
  nsTHashMap<uint64_t, RefPtr<nsDocShellLoadState>> mPendingLoadStates;

  // See `BrowsingContext::mEpochs` for an explanation of this field.
  uint64_t mBrowsingContextFieldEpoch = 0;

  // A preference serializer used to share preferences with the process.
  // Cleared once startup is complete.
  UniquePtr<mozilla::ipc::SharedPreferenceSerializer> mPrefSerializer;

  RefPtr<IdleTaskRunner> mMaybeBeginShutdownRunner;

  static uint32_t sMaxContentProcesses;
  static uint32_t sPageLoadEventCounter;

  bool mIsSignaledImpendingShutdown = false;
  bool mIsNotifiedShutdownSuccess = false;

  nsCOMPtr<nsIThread> mClipboardContentAnalysisThread;
};

NS_DEFINE_STATIC_IID_ACCESSOR(ContentParent, NS_CONTENTPARENT_IID)

// Threadsafe handle object allowing off-main-thread code to get some
// information and maintain a weak reference to a ContentParent.
class ThreadsafeContentParentHandle final {
  friend class ContentParent;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ThreadsafeContentParentHandle);

  // Get the ChildID of this process. Safe to call from any thread.
  ContentParentId ChildID() const { return mChildID; }

  // Get the current RemoteType of this ContentParent. Safe to call from any
  // thread. If the returned RemoteType is PREALLOC_REMOTE_TYPE, it may change
  // again in the future.
  nsCString GetRemoteType() MOZ_EXCLUDES(mMutex);

  // Try to get a reference to the real `ContentParent` object from this weak
  // reference. This may only be called on the main thread.
  already_AddRefed<ContentParent> GetContentParent()
      MOZ_REQUIRES(sMainThreadCapability) {
    return do_AddRef(mWeakActor);
  }

  // Attempt to add a KeepAlive for the given BrowserId. A KeepAlive will try to
  // keep the process alive, though it may still die (e.g. due to a crash,
  // explicit shutdown request, or similar).
  //
  // Returns nullptr if the process is already being shut down.
  //
  // May be called from any thread.
  [[nodiscard]] UniqueThreadsafeContentParentKeepAlive TryAddKeepAlive(
      uint64_t aBrowserId = 0) MOZ_EXCLUDES(mMutex);

 private:
  ThreadsafeContentParentHandle(ContentParent* aActor, ContentParentId aChildID,
                                const nsACString& aRemoteType)
      : mChildID(aChildID), mRemoteType(aRemoteType), mWeakActor(aActor) {}
  ~ThreadsafeContentParentHandle() { MOZ_ASSERT(!mWeakActor); }

  mozilla::RecursiveMutex mMutex{"ContentParentIdentity"};

  const ContentParentId mChildID;

  nsCString mRemoteType MOZ_GUARDED_BY(mMutex);

  // Keepalives for this browser, keyed by BrowserId. A BrowserId of `0` is used
  // for non-tab code keeping the process alive (such as for workers).
  // Each KeepAlive increments the corresponding BrowserId's counter, and the
  // process will begin shutdown when the last KeepAlive is removed.
  // FIXME: These sets are probably quite small, so it might make sense to avoid
  // hashtable storage.
  nsTHashMap<uint64_t, uint32_t> mKeepAlivesPerBrowserId MOZ_GUARDED_BY(mMutex);

  // If set, the browser is shutting down, and new workers or tabs should not be
  // created in this process.
  bool mShutdownStarted MOZ_GUARDED_BY(mMutex) = false;

  // Weak reference to the actual ContentParent actor. Only touched on the main
  // thread to read or clear.
  ContentParent* mWeakActor MOZ_GUARDED_BY(sMainThreadCapability);
};

// This is the C++ version of remoteTypePrefix in E10SUtils.sys.mjs.
nsDependentCSubstring RemoteTypePrefix(const nsACString& aContentProcessType);

// This is based on isWebRemoteType in E10SUtils.sys.mjs.
bool IsWebRemoteType(const nsACString& aContentProcessType);

bool IsWebCoopCoepRemoteType(const nsACString& aContentProcessType);

bool IsExtensionRemoteType(const nsACString& aContentProcessType);

inline nsISupports* ToSupports(mozilla::dom::ContentParent* aContentParent) {
  return static_cast<nsIDOMProcessParent*>(aContentParent);
}

}  // namespace dom
}  // namespace mozilla

class ParentIdleListener : public nsIObserver {
  friend class mozilla::dom::ContentParent;

 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  ParentIdleListener(mozilla::dom::ContentParent* aParent, uint64_t aObserver,
                     uint32_t aTime)
      : mParent(aParent), mObserver(aObserver), mTime(aTime) {}

 private:
  virtual ~ParentIdleListener() = default;

  RefPtr<mozilla::dom::ContentParent> mParent;
  uint64_t mObserver;
  uint32_t mTime;
};

#endif  // mozilla_dom_ContentParent_h
