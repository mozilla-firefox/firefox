/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_ContentChild_h
#define mozilla_dom_ContentChild_h

#include "mozilla/Atomics.h"
#include "mozilla/dom/BlobImpl.h"
#include "mozilla/dom/GetFilesHelper.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/PContentChild.h"
#include "mozilla/dom/ProcessActor.h"
#include "mozilla/dom/RemoteType.h"
#include "mozilla/Hal.h"
#include "mozilla/ipc/InputStreamUtils.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/UniquePtr.h"
#include "nsClassHashtable.h"
#include "nscore.h"
#include "nsHashKeys.h"
#include "nsIDOMProcessChild.h"
#include "nsRefPtrHashtable.h"
#include "nsString.h"
#include "nsTArrayForwardDeclare.h"
#include "nsTHashSet.h"

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
#  include "nsIFile.h"
#endif

#if defined(MOZ_SANDBOX) && defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
#  include "mozilla/PSandboxTestingChild.h"
#endif

struct ChromePackage;
class nsIObserver;
struct SubstitutionMapping;
struct OverrideMapping;
class nsIDomainPolicy;
class nsIURIClassifierCallback;
class nsDocShellLoadState;
class nsFrameLoader;
class nsIOpenWindowInfo;

namespace mozilla {
class RemoteSpellcheckEngineChild;
class ChildProfilerController;

namespace ipc {
class UntypedEndpoint;
}

namespace loader {
class PScriptCacheChild;
}

namespace widget {
enum class ThemeChangeKind : uint8_t;
}
namespace dom {

namespace ipc {
class SharedMap;
}

class ConsoleListener;
class ClonedMessageData;
class BrowserChild;
class TabContext;
enum class CallerType : uint32_t;

class ContentChild final : public PContentChild,
                           public nsIDOMProcessChild,
                           public mozilla::ipc::IShmemAllocator,
                           public ProcessActor {
  using ClonedMessageData = mozilla::dom::ClonedMessageData;
  using FileDescriptor = mozilla::ipc::FileDescriptor;

  friend class PContentChild;

 public:
  NS_DECL_NSIDOMPROCESSCHILD

  ContentChild();
  virtual ~ContentChild();
  NS_IMETHOD QueryInterface(REFNSIID aIID, void** aInstancePtr) override;
  NS_IMETHOD_(MozExternalRefCountType) AddRef(void) override { return 1; }
  NS_IMETHOD_(MozExternalRefCountType) Release(void) override { return 1; }

  struct AppInfo {
    nsCString version;
    nsCString buildID;
    nsCString name;
    nsCString UAName;
    nsCString ID;
    nsCString vendor;
    nsCString sourceURL;
    nsCString updateURL;
  };

  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult ProvideWindowCommon(
      NotNull<BrowserChild*> aTabOpener, nsIOpenWindowInfo* aOpenWindowInfo,
      uint32_t aChromeFlags, bool aCalledFromJS, nsIURI* aURI,
      const nsAString& aName, const nsACString& aFeatures,
      const UserActivation::Modifiers& aModifiers, bool aForceNoOpener,
      bool aForceNoReferrer, bool aIsPopupRequested,
      nsDocShellLoadState* aLoadState, bool* aWindowIsNew,
      BrowsingContext** aReturn);

  void Init(mozilla::ipc::UntypedEndpoint&& aEndpoint,
            const char* aParentBuildID, bool aIsForBrowser);

  void InitXPCOM(XPCOMInitData&& aXPCOMInit,
                 const mozilla::dom::ipc::StructuredCloneData& aInitialData,
                 bool aIsReadyForBackgroundProcessing);

  void InitSharedUASheets(
      Maybe<mozilla::ipc::ReadOnlySharedMemoryHandle>&& aHandle,
      uintptr_t aAddress);

  void InitGraphicsDeviceData(const ContentDeviceData& aData);

  static ContentChild* GetSingleton() { return sSingleton; }

  const AppInfo& GetAppInfo() { return mAppInfo; }

  void SetProcessName(const nsACString& aName,
                      const nsACString* aETLDplus1 = nullptr,
                      const nsACString* aCurrentProfile = nullptr);

  void GetProcessName(nsACString& aName) const;

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  void GetProfileDir(nsIFile** aProfileDir) const {
    *aProfileDir = mProfileDir;
    NS_IF_ADDREF(*aProfileDir);
  }

  void SetProfileDir(nsIFile* aProfileDir) { mProfileDir = aProfileDir; }
#endif

  bool IsAlive() const;

  bool IsShuttingDown() const;

  ipc::SharedMap* SharedData() { return mSharedData; };

  static void AppendProcessId(nsACString& aName);

  static RefPtr<GenericPromise> UpdateCookieStatus(nsIChannel* aChannel);

  mozilla::ipc::IPCResult RecvInitGMPService(
      Endpoint<PGMPServiceChild>&& aGMPService);

  mozilla::ipc::IPCResult RecvInitProfiler(
      Endpoint<PProfilerChild>&& aEndpoint);

  mozilla::ipc::IPCResult RecvGMPsChanged(
      nsTArray<GMPCapabilityData>&& capabilities);

  mozilla::ipc::IPCResult RecvInitProcessHangMonitor(
      Endpoint<PProcessHangMonitorChild>&& aHangMonitor);

  mozilla::ipc::IPCResult RecvInitRendering(
      Endpoint<PCompositorManagerChild>&& aCompositor,
      Endpoint<PImageBridgeChild>&& aImageBridge,
      Endpoint<PVRManagerChild>&& aVRBridge,
      Endpoint<PRemoteDecoderManagerChild>&& aVideoManager,
      nsTArray<uint32_t>&& namespaces);

  mozilla::ipc::IPCResult RecvReinitRendering(
      Endpoint<PCompositorManagerChild>&& aCompositor,
      Endpoint<PImageBridgeChild>&& aImageBridge,
      Endpoint<PVRManagerChild>&& aVRBridge,
      Endpoint<PRemoteDecoderManagerChild>&& aVideoManager,
      nsTArray<uint32_t>&& namespaces);

  mozilla::ipc::IPCResult RecvReinitRenderingForDeviceReset();

  mozilla::ipc::IPCResult RecvSetProcessSandbox(
      const Maybe<FileDescriptor>& aBroker);

  PHalChild* AllocPHalChild();
  bool DeallocPHalChild(PHalChild*);

  PHeapSnapshotTempFileHelperChild* AllocPHeapSnapshotTempFileHelperChild();

  bool DeallocPHeapSnapshotTempFileHelperChild(
      PHeapSnapshotTempFileHelperChild*);

  PCycleCollectWithLogsChild* AllocPCycleCollectWithLogsChild(
      const bool& aDumpAllTraces, const FileDescriptor& aGCLog,
      const FileDescriptor& aCCLog);

  bool DeallocPCycleCollectWithLogsChild(PCycleCollectWithLogsChild* aActor);

  virtual mozilla::ipc::IPCResult RecvPCycleCollectWithLogsConstructor(
      PCycleCollectWithLogsChild* aChild, const bool& aDumpAllTraces,
      const FileDescriptor& aGCLog, const FileDescriptor& aCCLog) override;

  already_AddRefed<PWebBrowserPersistDocumentChild>
  AllocPWebBrowserPersistDocumentChild(
      PBrowserChild* aBrowser, const MaybeDiscarded<BrowsingContext>& aContext);

  virtual mozilla::ipc::IPCResult RecvPWebBrowserPersistDocumentConstructor(
      PWebBrowserPersistDocumentChild* aActor, PBrowserChild* aBrowser,
      const MaybeDiscarded<BrowsingContext>& aContext) override;

  already_AddRefed<PTestShellChild> AllocPTestShellChild();

  virtual mozilla::ipc::IPCResult RecvPTestShellConstructor(
      PTestShellChild*) override;

  PScriptCacheChild* AllocPScriptCacheChild(const FileDescOrError& cacheFile,
                                            const bool& wantCacheData);

  bool DeallocPScriptCacheChild(PScriptCacheChild*);

  virtual mozilla::ipc::IPCResult RecvPScriptCacheConstructor(
      PScriptCacheChild*, const FileDescOrError& cacheFile,
      const bool& wantCacheData) override;

  PRemotePrintJobChild* AllocPRemotePrintJobChild();

  PMediaChild* AllocPMediaChild();

  bool DeallocPMediaChild(PMediaChild* aActor);

  mozilla::ipc::IPCResult RecvNotifyEmptyHTTPCache();

  mozilla::ipc::IPCResult RecvRegisterChrome(
      nsTArray<ChromePackage>&& packages,
      nsTArray<SubstitutionMapping>&& resources,
      nsTArray<OverrideMapping>&& overrides, const nsCString& locale,
      const bool& reset);
  mozilla::ipc::IPCResult RecvRegisterChromeItem(
      const ChromeRegistryItem& item);

  mozilla::ipc::IPCResult RecvClearStyleSheetCache(
      const Maybe<bool>& aChrome, const Maybe<RefPtr<nsIPrincipal>>& aPrincipal,
      const Maybe<nsCString>& aSchemelessSite,
      const Maybe<OriginAttributesPattern>& aPattern,
      const Maybe<nsCString>& aURL);

  mozilla::ipc::IPCResult RecvClearScriptCache(
      const Maybe<bool>& aChrome, const Maybe<RefPtr<nsIPrincipal>>& aPrincipal,
      const Maybe<nsCString>& aSchemelessSite,
      const Maybe<OriginAttributesPattern>& aPattern,
      const Maybe<nsCString>& aURL);

  mozilla::ipc::IPCResult RecvClearImageCache(
      const Maybe<bool>& aPrivateLoader, const Maybe<bool>& aChrome,
      const Maybe<RefPtr<nsIPrincipal>>& aPrincipal,
      const Maybe<nsCString>& aSchemelessSite,
      const Maybe<OriginAttributesPattern>& aPattern,
      const Maybe<nsCString>& aURL);

  PRemoteSpellcheckEngineChild* AllocPRemoteSpellcheckEngineChild();

  bool DeallocPRemoteSpellcheckEngineChild(PRemoteSpellcheckEngineChild*);

  mozilla::ipc::IPCResult RecvSetOffline(const bool& offline);

  mozilla::ipc::IPCResult RecvSetConnectivity(const bool& connectivity);
  mozilla::ipc::IPCResult RecvSetCaptivePortalState(const int32_t& state);
  mozilla::ipc::IPCResult RecvSetTRRMode(
      const nsIDNSService::ResolverMode& mode,
      const nsIDNSService::ResolverMode& modeFromPref);

  mozilla::ipc::IPCResult RecvBidiKeyboardNotify(const bool& isLangRTL,
                                                 const bool& haveBidiKeyboards);

  mozilla::ipc::IPCResult RecvNotifyVisited(nsTArray<VisitedQueryResult>&&);

  mozilla::ipc::IPCResult RecvThemeChanged(FullLookAndFeel&&,
                                           widget::ThemeChangeKind);

  mozilla::ipc::IPCResult RecvPreferenceUpdate(const Pref& aPref);
  mozilla::ipc::IPCResult RecvVarUpdate(const GfxVarUpdate& pref);

  mozilla::ipc::IPCResult RecvUpdatePerfStatsCollectionMask(
      const uint64_t& aMask);

  mozilla::ipc::IPCResult RecvCollectPerfStatsJSON(
      CollectPerfStatsJSONResolver&& aResolver);

  mozilla::ipc::IPCResult RecvCollectScrollingMetrics(
      CollectScrollingMetricsResolver&& aResolver);

  mozilla::ipc::IPCResult RecvLoadProcessScript(const nsString& aURL);

  mozilla::ipc::IPCResult RecvAsyncMessage(const nsString& aMsg,
                                           const ClonedMessageData& aData);

  mozilla::ipc::IPCResult RecvRegisterStringBundles(
      nsTArray<StringBundleDescriptor>&& stringBundles);

  mozilla::ipc::IPCResult RecvSimpleURIUnknownRemoteSchemes(
      nsTArray<nsCString>&& aRemoteSchemes);

  mozilla::ipc::IPCResult RecvUpdateL10nFileSources(
      nsTArray<L10nFileSourceDescriptor>&& aDescriptors);

  mozilla::ipc::IPCResult RecvUpdateSharedData(
      mozilla::ipc::ReadOnlySharedMemoryHandle&& aMapHandle,
      nsTArray<IPCBlob>&& aBlobs, nsTArray<nsCString>&& aChangedKeys);

  mozilla::ipc::IPCResult RecvForceGlobalReflow(
      const gfxPlatform::GlobalReflowFlags& aFlags);

  mozilla::ipc::IPCResult RecvGeolocationUpdate(nsIDOMGeoPosition* aPosition);

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY because we don't have MOZ_CAN_RUN_SCRIPT bits
  // in IPC code yet.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvGeolocationError(const uint16_t& errorCode);

  mozilla::ipc::IPCResult RecvUpdateDictionaryList(
      nsTArray<nsCString>&& aDictionaries);

  mozilla::ipc::IPCResult RecvUpdateFontList(SystemFontList&&);
  mozilla::ipc::IPCResult RecvRebuildFontList(const bool& aFullRebuild);
  mozilla::ipc::IPCResult RecvFontListShmBlockAdded(
      const uint32_t& aGeneration, const uint32_t& aIndex,
      mozilla::ipc::ReadOnlySharedMemoryHandle&& aHandle);

  mozilla::ipc::IPCResult RecvUpdateAppLocales(
      nsTArray<nsCString>&& aAppLocales);
  mozilla::ipc::IPCResult RecvUpdateRequestedLocales(
      nsTArray<nsCString>&& aRequestedLocales);

  mozilla::ipc::IPCResult RecvSystemTimezoneChanged();

  mozilla::ipc::IPCResult RecvAddPermission(const IPC::Permission& permission);

  mozilla::ipc::IPCResult RecvRemoveAllPermissions();

  mozilla::ipc::IPCResult RecvFlushMemory(const nsString& reason);

  mozilla::ipc::IPCResult RecvActivateA11y(uint64_t aCacheDomains);
  mozilla::ipc::IPCResult RecvShutdownA11y();
  mozilla::ipc::IPCResult RecvSetCacheDomains(uint64_t aCacheDomains);

  mozilla::ipc::IPCResult RecvApplicationForeground();
  mozilla::ipc::IPCResult RecvApplicationBackground();
  mozilla::ipc::IPCResult RecvGarbageCollect();
  mozilla::ipc::IPCResult RecvCycleCollect();
  mozilla::ipc::IPCResult RecvUnlinkGhosts();

  mozilla::ipc::IPCResult RecvAppInfo(
      const nsCString& version, const nsCString& buildID, const nsCString& name,
      const nsCString& UAName, const nsCString& ID, const nsCString& vendor,
      const nsCString& sourceURL, const nsCString& updateURL);

  mozilla::ipc::IPCResult RecvRemoteType(const nsCString& aRemoteType,
                                         const nsCString& aProfile);

  void PreallocInit();

  // Call RemoteTypePrefix() on the result to remove URIs if you want to use
  // this for telemetry.
  const nsACString& GetRemoteType() const override;

  mozilla::ipc::IPCResult RecvInitRemoteWorkerService(
      Endpoint<PRemoteWorkerServiceChild>&& aEndpoint,
      Endpoint<PRemoteWorkerDebuggerManagerChild>&& aDebuggerChildEp);

  mozilla::ipc::IPCResult RecvInitBlobURLs(
      nsTArray<BlobURLRegistrationData>&& aRegistations);

  mozilla::ipc::IPCResult RecvInitJSActorInfos(
      nsTArray<JSProcessActorInfo>&& aContentInfos,
      nsTArray<JSWindowActorInfo>&& aWindowInfos);

  mozilla::ipc::IPCResult RecvUnregisterJSWindowActor(const nsCString& aName);

  mozilla::ipc::IPCResult RecvUnregisterJSProcessActor(const nsCString& aName);

  mozilla::ipc::IPCResult RecvLastPrivateDocShellDestroyed();

  mozilla::ipc::IPCResult RecvNotifyProcessPriorityChanged(
      const hal::ProcessPriority& aPriority);

  mozilla::ipc::IPCResult RecvMinimizeMemoryUsage();

  mozilla::ipc::IPCResult RecvLoadAndRegisterSheet(nsIURI* aURI,
                                                   const uint32_t& aType);

  mozilla::ipc::IPCResult RecvUnregisterSheet(nsIURI* aURI,
                                              const uint32_t& aType);

  void AddIdleObserver(nsIObserver* aObserver, uint32_t aIdleTimeInS);

  void RemoveIdleObserver(nsIObserver* aObserver, uint32_t aIdleTimeInS);

  mozilla::ipc::IPCResult RecvNotifyIdleObserver(const uint64_t& aObserver,
                                                 const nsCString& aTopic,
                                                 const nsString& aData);

  mozilla::ipc::IPCResult RecvUpdateWindow(const uintptr_t& aChildId);

  mozilla::ipc::IPCResult RecvDomainSetChanged(const uint32_t& aSetType,
                                               const uint32_t& aChangeType,
                                               nsIURI* aDomain);

  mozilla::ipc::IPCResult RecvShutdownConfirmedHP();

  mozilla::ipc::IPCResult RecvShutdown();

  mozilla::ipc::IPCResult RecvPush(const nsCString& aScope,
                                   nsIPrincipal* aPrincipal,
                                   const nsString& aMessageId);

  mozilla::ipc::IPCResult RecvPushWithData(const nsCString& aScope,
                                           nsIPrincipal* aPrincipal,
                                           const nsString& aMessageId,
                                           nsTArray<uint8_t>&& aData);

  mozilla::ipc::IPCResult RecvPushError(const nsCString& aScope,
                                        nsIPrincipal* aPrincipal,
                                        const nsString& aMessage,
                                        const uint32_t& aFlags);

  mozilla::ipc::IPCResult RecvNotifyPushSubscriptionModifiedObservers(
      const nsCString& aScope, nsIPrincipal* aPrincipal);

  mozilla::ipc::IPCResult RecvRefreshScreens(
      nsTArray<ScreenDetails>&& aScreens);

  mozilla::ipc::IPCResult RecvNetworkLinkTypeChange(const uint32_t& aType);
  uint32_t NetworkLinkType() const { return mNetworkLinkType; }

  mozilla::ipc::IPCResult RecvSocketProcessCrashed();

  // Get the directory for IndexedDB files. We query the parent for this and
  // cache the value
  nsString& GetIndexedDBPath();

  ContentParentId GetID() const {
    return ContentParentId{static_cast<uint64_t>(XRE_GetChildID())};
  }

  bool IsForBrowser() const { return mIsForBrowser; }

  MOZ_CAN_RUN_SCRIPT_BOUNDARY mozilla::ipc::IPCResult RecvConstructBrowser(
      ManagedEndpoint<PBrowserChild>&& aBrowserEp,
      ManagedEndpoint<PWindowGlobalChild>&& aWindowEp, const TabId& aTabId,
      const IPCTabContext& aContext, const WindowGlobalInit& aWindowInit,
      const uint32_t& aChromeFlags, const ContentParentId& aCpID,
      const bool& aIsForBrowser, const bool& aIsTopLevel);

  FORWARD_SHMEM_ALLOCATOR_TO(PContentChild)

  void GetAvailableDictionaries(nsTArray<nsCString>& aDictionaries);

#ifdef MOZ_WEBRTC
  PWebrtcGlobalChild* AllocPWebrtcGlobalChild();
  bool DeallocPWebrtcGlobalChild(PWebrtcGlobalChild* aActor);
#endif

  PContentPermissionRequestChild* AllocPContentPermissionRequestChild(
      Span<const PermissionRequest> aRequests, nsIPrincipal* aPrincipal,
      nsIPrincipal* aTopLevelPrincipal, const bool& aIsHandlingUserInput,
      const bool& aMaybeUnsafePermissionDelegate, const TabId& aTabId);
  bool DeallocPContentPermissionRequestChild(
      PContentPermissionRequestChild* actor);

  // GetFiles for WebKit/Blink FileSystem API and Directory API must run on the
  // parent process.
  void CreateGetFilesRequest(nsTArray<nsString>&& aDirectoryPath,
                             bool aRecursiveFlag, nsID& aUUID,
                             GetFilesHelperChild* aChild);

  void DeleteGetFilesRequest(nsID& aUUID, GetFilesHelperChild* aChild);

  mozilla::ipc::IPCResult RecvGetFilesResponse(
      const nsID& aUUID, const GetFilesResponseResult& aResult);

  mozilla::ipc::IPCResult RecvBlobURLRegistration(
      const nsCString& aURI, const IPCBlob& aBlob, nsIPrincipal* aPrincipal,
      const nsCString& aPartitionKey);

  mozilla::ipc::IPCResult RecvBlobURLUnregistration(
      const nsTArray<nsCString>& aURIs);

  mozilla::ipc::IPCResult RecvRequestMemoryReport(
      const uint32_t& generation, const bool& anonymize,
      const bool& minimizeMemoryUsage, const Maybe<FileDescriptor>& DMDFile,
      const RequestMemoryReportResolver& aResolver);

#if defined(XP_WIN)
  mozilla::ipc::IPCResult RecvGetUntrustedModulesData(
      GetUntrustedModulesDataResolver&& aResolver);
  mozilla::ipc::IPCResult RecvUnblockUntrustedModulesThread();
#endif  // defined(XP_WIN)

  mozilla::ipc::IPCResult RecvSetXPCOMProcessAttributes(
      XPCOMInitData&& aXPCOMInit, const StructuredCloneData& aInitialData,
      FullLookAndFeel&& aLookAndFeelData, SystemFontList&& aFontList,
      Maybe<mozilla::ipc::ReadOnlySharedMemoryHandle>&& aSharedUASheetHandle,
      const uintptr_t& aSharedUASheetAddress,
      nsTArray<mozilla::ipc::ReadOnlySharedMemoryHandle>&&
          aSharedFontListBlocks,
      const bool& aIsReadyForBackgroundProcessing);

  mozilla::ipc::IPCResult RecvProvideAnonymousTemporaryFile(
      const uint64_t& aID, const FileDescOrError& aFD);

  mozilla::ipc::IPCResult RecvSetPermissionsWithKey(
      const nsCString& aPermissionKey, nsTArray<IPC::Permission>&& aPerms);

  mozilla::ipc::IPCResult RecvShareCodeCoverageMutex(
      CrossProcessMutexHandle aHandle);

  mozilla::ipc::IPCResult RecvFlushCodeCoverageCounters(
      FlushCodeCoverageCountersResolver&& aResolver);

  mozilla::ipc::IPCResult RecvSetInputEventQueueEnabled();

  mozilla::ipc::IPCResult RecvFlushInputEventQueue();

  mozilla::ipc::IPCResult RecvSuspendInputEventQueue();

  mozilla::ipc::IPCResult RecvResumeInputEventQueue();

  mozilla::ipc::IPCResult RecvAddDynamicScalars(
      nsTArray<DynamicScalarDefinition>&& aDefs);

  // Get a reference to the font list passed from the chrome process,
  // for use during gfx initialization.
  SystemFontList& SystemFontList() { return mFontList; }

  nsTArray<mozilla::ipc::ReadOnlySharedMemoryHandle>& SharedFontListBlocks() {
    return mSharedFontListBlocks;
  }

  // PURLClassifierChild
  PURLClassifierChild* AllocPURLClassifierChild(nsIPrincipal* aPrincipal,
                                                bool* aSuccess);
  bool DeallocPURLClassifierChild(PURLClassifierChild* aActor);

  // PURLClassifierLocalChild
  PURLClassifierLocalChild* AllocPURLClassifierLocalChild(
      nsIURI* aUri, Span<const IPCURLClassifierFeature> aFeatures);
  bool DeallocPURLClassifierLocalChild(PURLClassifierLocalChild* aActor);

  // PURLClassifierLocalByNameByNameParentChild
  PURLClassifierLocalByNameChild* AllocPURLClassifierLocalByNameChild(
      nsIURI* aUri, Span<const nsCString> aFeatures,
      const nsIUrlClassifierFeature::listType& aListType);
  bool DeallocPURLClassifierLocalByNameChild(
      PURLClassifierLocalByNameChild* aActor);

  PSessionStorageObserverChild* AllocPSessionStorageObserverChild();

  bool DeallocPSessionStorageObserverChild(
      PSessionStorageObserverChild* aActor);

  FullLookAndFeel& BorrowLookAndFeelData() { return mLookAndFeelData; }

  /**
   * Helper function for protocols that use the GPU process when available.
   * Overrides FatalError to just be a warning when communicating with the
   * GPU process since we don't want to crash the content process when the
   * GPU process crashes.
   */
  static void FatalErrorIfNotUsingGPUProcess(const char* const aErrorMsg,
                                             GeckoChildID aOtherChildID);

  using AnonymousTemporaryFileCallback = std::function<void(PRFileDesc*)>;
  nsresult AsyncOpenAnonymousTemporaryFile(
      const AnonymousTemporaryFileCallback& aCallback);

  mozilla::ipc::IPCResult RecvSaveRecording(const FileDescriptor& aFile);

  mozilla::ipc::IPCResult RecvCrossProcessRedirect(
      RedirectToRealChannelArgs&& aArgs,
      nsTArray<Endpoint<extensions::PStreamFilterParent>>&& aEndpoints,
      CrossProcessRedirectResolver&& aResolve);

  mozilla::ipc::IPCResult RecvStartDelayedAutoplayMediaComponents(
      const MaybeDiscarded<BrowsingContext>& aContext);

  mozilla::ipc::IPCResult RecvUpdateMediaControlAction(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const MediaControlAction& aAction);

  // See `BrowsingContext::mEpochs` for an explanation of this field.
  uint64_t GetBrowsingContextFieldEpoch() const {
    return mBrowsingContextFieldEpoch;
  }
  uint64_t NextBrowsingContextFieldEpoch() {
    mBrowsingContextFieldEpoch++;
    return mBrowsingContextFieldEpoch;
  }

  mozilla::ipc::IPCResult RecvOnAllowAccessFor(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const nsCString& aTrackingOrigin, uint32_t aCookieBehavior,
      const ContentBlockingNotifier::StorageAccessPermissionGrantedReason&
          aReason);

  mozilla::ipc::IPCResult RecvOnContentBlockingDecision(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const ContentBlockingNotifier::BlockingDecision& aDecision,
      uint32_t aRejectedReason);

#ifdef NIGHTLY_BUILD
  // Fetch the current number of pending input events.
  //
  // NOTE: This method performs an atomic read, and is safe to call from all
  // threads.
  uint32_t GetPendingInputEvents() { return mPendingInputEvents; }
#endif

#if defined(MOZ_SANDBOX) && defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
  mozilla::ipc::IPCResult RecvInitSandboxTesting(
      Endpoint<PSandboxTestingChild>&& aEndpoint);
#endif

 private:
  static void ForceKillTimerCallback(nsITimer* aTimer, void* aClosure);
  void StartForceKillTimer();

  void ShutdownInternal();

  mozilla::ipc::IPCResult GetResultForRenderingInitFailure(
      GeckoChildID aOtherChildID);

  virtual void ActorDestroy(ActorDestroyReason why) override;

  virtual void ProcessingError(Result aCode, const char* aReason) override;

  mozilla::ipc::IPCResult RecvCreateBrowsingContext(
      uint64_t aGroupId, BrowsingContext::IPCInitializer&& aInit);

  mozilla::ipc::IPCResult RecvDiscardBrowsingContext(
      const MaybeDiscarded<BrowsingContext>& aContext, bool aDoDiscard,
      DiscardBrowsingContextResolver&& aResolve);

  mozilla::ipc::IPCResult RecvRegisterBrowsingContextGroup(
      uint64_t aGroupId, nsTArray<SyncedContextInitializer>&& aInits,
      nsTArray<OriginAgentClusterInitializer>&& aUseOriginAgentCluster);
  mozilla::ipc::IPCResult RecvDestroyBrowsingContextGroup(uint64_t aGroupId);

  mozilla::ipc::IPCResult RecvSetUseOriginAgentCluster(
      uint64_t aGroupId, nsIPrincipal* aPrincipal, bool aUseOriginAgentCluster);

  mozilla::ipc::IPCResult RecvWindowClose(
      const MaybeDiscarded<BrowsingContext>& aContext, bool aTrustedCaller);
  mozilla::ipc::IPCResult RecvWindowFocus(
      const MaybeDiscarded<BrowsingContext>& aContext, CallerType aCallerType,
      uint64_t aActionId);
  mozilla::ipc::IPCResult RecvWindowBlur(
      const MaybeDiscarded<BrowsingContext>& aContext, CallerType aCallerType);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY mozilla::ipc::IPCResult RecvRaiseWindow(
      const MaybeDiscarded<BrowsingContext>& aContext, CallerType aCallerType,
      uint64_t aActionId);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY mozilla::ipc::IPCResult RecvAdjustWindowFocus(
      const MaybeDiscarded<BrowsingContext>& aContext, bool aIsVisible,
      uint64_t aActionId, bool aShouldClearFocus,
      const MaybeDiscarded<BrowsingContext>& aAncestorBrowsingContextToFocus);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY mozilla::ipc::IPCResult RecvClearFocus(
      const MaybeDiscarded<BrowsingContext>& aContext);
  mozilla::ipc::IPCResult RecvSetFocusedBrowsingContext(
      const MaybeDiscarded<BrowsingContext>& aContext, uint64_t aActionId);
  mozilla::ipc::IPCResult RecvSetActiveBrowsingContext(
      const MaybeDiscarded<BrowsingContext>& aContext, uint64_t aActionId);
  mozilla::ipc::IPCResult RecvAbortOrientationPendingPromises(
      const MaybeDiscarded<BrowsingContext>& aContext);
  mozilla::ipc::IPCResult RecvUnsetActiveBrowsingContext(
      const MaybeDiscarded<BrowsingContext>& aContext, uint64_t aActionId);
  mozilla::ipc::IPCResult RecvSetFocusedElement(
      const MaybeDiscarded<BrowsingContext>& aContext, bool aNeedsFocus);
  mozilla::ipc::IPCResult RecvFinalizeFocusOuter(
      const MaybeDiscarded<BrowsingContext>& aContext, bool aCanFocus,
      CallerType aCallerType);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY mozilla::ipc::IPCResult RecvBlurToChild(
      const MaybeDiscarded<BrowsingContext>& aFocusedBrowsingContext,
      const MaybeDiscarded<BrowsingContext>& aBrowsingContextToClear,
      const MaybeDiscarded<BrowsingContext>& aAncestorBrowsingContextToFocus,
      bool aIsLeavingDocument, bool aAdjustWidget, uint64_t aActionId);
  mozilla::ipc::IPCResult RecvSetupFocusedAndActive(
      const MaybeDiscarded<BrowsingContext>& aFocusedBrowsingContext,
      uint64_t aActionIdForFocused,
      const MaybeDiscarded<BrowsingContext>& aActiveBrowsingContext,
      uint64_t aActionIdForActive);
  mozilla::ipc::IPCResult RecvReviseActiveBrowsingContext(
      uint64_t aOldActionId,
      const MaybeDiscarded<BrowsingContext>& aActiveBrowsingContext,
      uint64_t aNewActionId);
  mozilla::ipc::IPCResult RecvReviseFocusedBrowsingContext(
      uint64_t aOldActionId,
      const MaybeDiscarded<BrowsingContext>& aFocusedBrowsingContext,
      uint64_t aNewActionId);
  mozilla::ipc::IPCResult RecvMaybeExitFullscreen(
      const MaybeDiscarded<BrowsingContext>& aContext);

  mozilla::ipc::IPCResult RecvWindowPostMessage(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const ClonedOrErrorMessageData& aMessage, const PostMessageData& aData);

  mozilla::ipc::IPCResult RecvCommitBrowsingContextTransaction(
      const MaybeDiscarded<BrowsingContext>& aContext,
      BrowsingContext::BaseTransaction&& aTransaction, uint64_t aEpoch);

  mozilla::ipc::IPCResult RecvCommitWindowContextTransaction(
      const MaybeDiscarded<WindowContext>& aContext,
      WindowContext::BaseTransaction&& aTransaction, uint64_t aEpoch);

  mozilla::ipc::IPCResult RecvCreateWindowContext(
      WindowContext::IPCInitializer&& aInit);
  mozilla::ipc::IPCResult RecvDiscardWindowContext(
      uint64_t aContextId, DiscardWindowContextResolver&& aResolve);

  mozilla::ipc::IPCResult RecvScriptError(
      const nsString& aMessage, const nsCString& aSourceName,
      const uint32_t& aLineNumber, const uint32_t& aColNumber,
      const uint32_t& aFlags, const nsCString& aCategory,
      const bool& aFromPrivateWindow, const uint64_t& aInnerWindowId,
      const bool& aFromChromeContext);

  mozilla::ipc::IPCResult RecvReportFrameTimingData(
      const LoadInfoArgs& loadInfoArgs, const nsString& entryName,
      const nsString& initiatorType, UniquePtr<PerformanceTimingData>&& aData);

  mozilla::ipc::IPCResult RecvLoadURI(
      const MaybeDiscarded<BrowsingContext>& aContext,
      nsDocShellLoadState* aLoadState, bool aSetNavigating,
      LoadURIResolver&& aResolve);

  mozilla::ipc::IPCResult RecvInternalLoad(nsDocShellLoadState* aLoadState);

  mozilla::ipc::IPCResult RecvDisplayLoadError(
      const MaybeDiscarded<BrowsingContext>& aContext, const nsAString& aURI);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvGoBack(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const Maybe<int32_t>& aCancelContentJSEpoch, bool aRequireUserInteraction,
      bool aUserActivation);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvGoForward(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const Maybe<int32_t>& aCancelContentJSEpoch, bool aRequireUserInteraction,
      bool aUserActivation);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvGoToIndex(
      const MaybeDiscarded<BrowsingContext>& aContext, const int32_t& aIndex,
      const Maybe<int32_t>& aCancelContentJSEpoch, bool aUserActivation);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvReload(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const uint32_t aReloadFlags);
  mozilla::ipc::IPCResult RecvStopLoad(
      const MaybeDiscarded<BrowsingContext>& aContext,
      const uint32_t aStopFlags);

  mozilla::ipc::IPCResult RecvRawMessage(
      const JSActorMessageMeta& aMeta, const Maybe<ClonedMessageData>& aData,
      const Maybe<ClonedMessageData>& aStack);

  already_AddRefed<JSActor> InitJSActor(JS::Handle<JSObject*> aMaybeActor,
                                        const nsACString& aName,
                                        ErrorResult& aRv) override;
  mozilla::ipc::IProtocol* AsNativeActor() override { return this; }

  mozilla::ipc::IPCResult RecvHistoryCommitIndexAndLength(
      const MaybeDiscarded<BrowsingContext>& aContext, const uint32_t& aIndex,
      const uint32_t& aLength, const nsID& aChangeID);

  mozilla::ipc::IPCResult RecvGetLayoutHistoryState(
      const MaybeDiscarded<BrowsingContext>& aContext,
      GetLayoutHistoryStateResolver&& aResolver);

  mozilla::ipc::IPCResult RecvDispatchLocationChangeEvent(
      const MaybeDiscarded<BrowsingContext>& aContext);

  mozilla::ipc::IPCResult RecvDispatchBeforeUnloadToSubtree(
      const MaybeDiscarded<BrowsingContext>& aStartingAt,
      DispatchBeforeUnloadToSubtreeResolver&& aResolver);

  mozilla::ipc::IPCResult RecvInitNextGenLocalStorageEnabled(
      const bool& aEnabled);

 public:
  static void DispatchBeforeUnloadToSubtree(
      BrowsingContext* aStartingAt,
      const DispatchBeforeUnloadToSubtreeResolver& aResolver);

  hal::ProcessPriority GetProcessPriority() const { return mProcessPriority; }

  hal::PerformanceHintSession* PerformanceHintSession() const {
    return mPerformanceHintSession.get();
  }

  // Returns the target work duration for the PerformanceHintSession, based on
  // the refresh interval. Estimate that we want the tick to complete in at most
  // half of the refresh period. This is fairly arbitrary and can be tweaked
  // later.
  static TimeDuration GetPerformanceHintTarget(TimeDuration aRefreshInterval) {
    return aRefreshInterval / int64_t(2);
  }

 private:
  void AddProfileToProcessName(const nsACString& aProfile);
  mozilla::ipc::IPCResult RecvFlushFOGData(FlushFOGDataResolver&& aResolver);

  mozilla::ipc::IPCResult RecvUpdateMediaCodecsSupported(
      RemoteDecodeIn aLocation, const media::MediaCodecsSupported& aSupported);

#ifdef NIGHTLY_BUILD
  virtual void OnChannelReceivedMessage(const Message& aMsg) override;

  virtual PContentChild::Result OnMessageReceived(const Message& aMsg) override;

  virtual PContentChild::Result OnMessageReceived(
      const Message& aMsg, UniquePtr<Message>& aReply) override;
#endif

  void ConfigureThreadPerformanceHints(const hal::ProcessPriority& aPriority);

  RefPtr<ConsoleListener> mConsoleListener;

  nsTHashSet<nsIObserver*> mIdleObservers;

  nsTArray<nsCString> mAvailableDictionaries;

  // Temporary storage for a list of available fonts, passed from the
  // parent process and used to initialize gfx in the child. Currently used
  // only on MacOSX and Linux.
  dom::SystemFontList mFontList;
  // Temporary storage for look and feel data.
  FullLookAndFeel mLookAndFeelData;
  // Temporary storage for list of shared-fontlist memory blocks.
  nsTArray<mozilla::ipc::ReadOnlySharedMemoryHandle> mSharedFontListBlocks;

  AppInfo mAppInfo;

  bool mIsForBrowser;
  nsCString mRemoteType = NOT_REMOTE_TYPE;
  bool mIsAlive;
  nsCString mProcessName;

  static ContentChild* sSingleton;

  class ShutdownCanary;
  static StaticAutoPtr<ShutdownCanary> sShutdownCanary;

  nsCOMPtr<nsIDomainPolicy> mPolicy;
  nsCOMPtr<nsITimer> mForceKillTimer;

  RefPtr<ipc::SharedMap> mSharedData;

  RefPtr<ChildProfilerController> mProfilerController;

#if defined(XP_MACOSX) && defined(MOZ_SANDBOX)
  nsCOMPtr<nsIFile> mProfileDir;
#endif

  // Hashtable to keep track of the pending GetFilesHelper objects.
  // This GetFilesHelperChild objects are removed when RecvGetFilesResponse is
  // received.
  nsRefPtrHashtable<nsIDHashKey, GetFilesHelperChild> mGetFilesPendingRequests;

  nsClassHashtable<nsUint64HashKey, AnonymousTemporaryFileCallback>
      mPendingAnonymousTemporaryFiles;

  mozilla::Atomic<bool> mShuttingDown;

#ifdef NIGHTLY_BUILD
  // NOTE: This member is atomic because it can be accessed from
  // off-main-thread.
  mozilla::Atomic<uint32_t> mPendingInputEvents;
#endif

  uint32_t mNetworkLinkType = 0;

  // See `BrowsingContext::mEpochs` for an explanation of this field.
  uint64_t mBrowsingContextFieldEpoch = 0;

  hal::ProcessPriority mProcessPriority = hal::PROCESS_PRIORITY_UNKNOWN;

  // Session created when the process priority is FOREGROUND to ensure high
  // priority scheduling of important threads. (Currently main thread and style
  // threads.) The work duration is reported by the RefreshDriverTimer.
  UniquePtr<hal::PerformanceHintSession> mPerformanceHintSession;
};

inline nsISupports* ToSupports(mozilla::dom::ContentChild* aContentChild) {
  return static_cast<nsIDOMProcessChild*>(aContentChild);
}

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_ContentChild_h
