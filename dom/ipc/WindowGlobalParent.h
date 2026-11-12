/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_WindowGlobalParent_h
#define mozilla_dom_WindowGlobalParent_h

#include "mozilla/ContentBlockingLog.h"
#include "mozilla/ContentBlockingNotifier.h"
#include "mozilla/Maybe.h"
#include "mozilla/PrincipalHashKey.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ClientIPCTypes.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/DOMRect.h"
#include "mozilla/dom/PWindowGlobalParent.h"
#include "mozilla/dom/PrefetchMatchWaiter.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/dom/WindowGlobalActor.h"
#include "mozilla/dom/WindowGlobalActorsBinding.h"
#include "mozilla/net/CookieJarSettings.h"
#include "nsIDOMProcessParent.h"
#include "nsISupports.h"
#include "nsRefPtrHashtable.h"
#include "nsTHashMap.h"
#include "nsTHashtable.h"
#include "nsURIHashKey.h"
#include "nsWrapperCache.h"

class nsIPrincipal;
class nsIURI;
class nsFrameLoader;

namespace mozilla {

namespace a11y {
class DocAccessibleParent;
class PDocAccessibleParent;
}  // namespace a11y

namespace gfx {
class CrossProcessPaint;
}  // namespace gfx

namespace dom {

class BrowserParent;
class PrefetchRecordParent;
class WindowGlobalChild;
class JSWindowActorParent;
class JSActorMessageMeta;
struct PageUseCounters;
class WindowSessionStoreState;
struct WindowSessionStoreUpdate;
class SSCacheQueryResult;
enum class FullscreenKeyboardLock : uint8_t;

// No-cors media request state used by Opaque Response Blocking, derived in the
// parent from state recorded on the owning window so a content process cannot
// spoof a subsequent request.
// See: https://whatpr.org/fetch/1442.html#request-no-cors-media-request-state
enum class NoCorsMediaRequestState : uint8_t {
  NotAvailable,
  Initial,
  Subsequent,
};

/**
 * A handle in the parent process to a specific nsGlobalWindowInner object.
 */
class WindowGlobalParent final : public WindowContext,
                                 public WindowGlobalActor,
                                 public PWindowGlobalParent {
  friend class gfx::CrossProcessPaint;
  friend class PWindowGlobalParent;

 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(WindowGlobalParent,
                                                         WindowContext)

  static already_AddRefed<WindowGlobalParent> GetByInnerWindowId(
      uint64_t aInnerWindowId);

  static already_AddRefed<WindowGlobalParent> GetByInnerWindowId(
      const GlobalObject& aGlobal, uint64_t aInnerWindowId) {
    return GetByInnerWindowId(aInnerWindowId);
  }

  static WindowGlobalParent* Cast(WindowContext* aContext);

  // The same as the corresponding methods on `WindowContext`, except that the
  // return types are already cast to their parent-process type variants, such
  // as `WindowGlobalParent` or `CanonicalBrowsingContext`.
  WindowGlobalParent* GetParentWindowContext() {
    return Cast(WindowContext::GetParentWindowContext());
  }
  WindowGlobalParent* TopWindowContext() {
    return Cast(WindowContext::TopWindowContext());
  }
  CanonicalBrowsingContext* GetBrowsingContext() const {
    return CanonicalBrowsingContext::Cast(WindowContext::GetBrowsingContext());
  }

  Element* GetRootOwnerElement();

  // Has this actor been shut down
  bool IsClosed() { return !CanSend(); }

  // Get the other side of this actor if it is an in-process actor. Returns
  // |nullptr| if the actor has been torn down, or is not in-process.
  already_AddRefed<WindowGlobalChild> GetChildActor();

  // Get a JS actor object by name.
  already_AddRefed<JSWindowActorParent> GetActor(JSContext* aCx,
                                                 const nsACString& aName,
                                                 ErrorResult& aRv);
  already_AddRefed<JSWindowActorParent> GetExistingActor(
      const nsACString& aName);

  // Get this actor's manager if it is not an in-process actor.
  // Returns |nullptr| if the actor is in-process.
  BrowserParent* GetBrowserParent() const;

  ContentParent* GetContentParent() const;

  // The principal of this WindowGlobal. This value will not change over the
  // lifetime of the WindowGlobal object, even to reflect changes in
  // |document.domain|.
  nsIPrincipal* DocumentPrincipal() { return mDocumentPrincipal; }

  nsIPrincipal* DocumentPartitionedPrincipal() {
    return mDocumentPartitionedPrincipal;
  }

  nsIPrincipal* DocumentStoragePrincipal() {
    return mPartitionStoragePrincipal ? DocumentPartitionedPrincipal()
                                      : DocumentPrincipal();
  }

  // The BrowsingContext which this WindowGlobal has been loaded into.
  // FIXME: It's quite awkward that this method has a slightly different name
  // than the one on WindowContext.
  CanonicalBrowsingContext* BrowsingContext() override {
    return GetBrowsingContext();
  }

  // Get the root nsFrameLoader object for the tree of BrowsingContext nodes
  // which this WindowGlobal is a part of. This will be the nsFrameLoader
  // holding the BrowserParent for remote tabs, and the root content frameloader
  // for non-remote tabs.
  already_AddRefed<nsFrameLoader> GetRootFrameLoader();

  // The current URI which loaded in the document.
  nsIURI* GetDocumentURI() override { return mDocumentURI; }

  void GetDocumentTitle(nsAString& aTitle) const {
    aTitle = mDocumentTitle.valueOr(nsString());
  }

  nsIPrincipal* GetContentBlockingAllowListPrincipal() const {
    return mDocContentBlockingAllowListPrincipal;
  }

  Maybe<ClientInfo> GetClientInfo() { return mClientInfo; }

  uint64_t ContentParentId();

  int32_t OsPid();

  bool IsCurrentGlobal();

  bool IsActiveInTab();

  bool IsProcessRoot();

  uint32_t ContentBlockingEvents();

  void GetContentBlockingLog(nsAString& aLog);

  bool IsInitialDocument() {
    return mIsInitialDocument.isSome() && mIsInitialDocument.value();
  }

  bool IsUncommittedInitialDocument() { return mIsUncommittedInitialDocument; }

  already_AddRefed<mozilla::dom::Promise> PermitUnload(
      PermitUnloadAction aAction, uint32_t aTimeout, mozilla::ErrorResult& aRv);

  void PermitUnload(
      std::function<void(nsIDocumentViewer::PermitUnloadResult)>&& aResolver);

  void CheckIfUnloadingIsCanceledForTraversable(
      nsDocShellLoadState* aDocShellLoadState,
      nsIDocumentViewer::PermitUnloadAction aAction,
      std::function<void(nsIDocumentViewer::PermitUnloadResult)>&& aResolver);

  void PermitUnloadChildNavigables(
      nsIDocumentViewer::PermitUnloadAction aAction,
      std::function<void(nsIDocumentViewer::PermitUnloadResult)>&& aResolver);

  already_AddRefed<mozilla::dom::Promise> DrawSnapshot(
      const DOMRect* aRect, double aScale, const nsACString& aBackgroundColor,
      const DrawSnapshotOptions& aOptions, mozilla::ErrorResult& aRv);

  already_AddRefed<mozilla::dom::Promise> RequestDocumentLanguageMetadata(
      const DocumentLanguageMetadataRequestOptions& aOptions,
      mozilla::ErrorResult& aRv);

  static already_AddRefed<WindowGlobalParent> CreateDisconnected(
      const WindowGlobalInit& aInit, ContentParent* aForProcess);

  // Initialize the mFrameLoader fields for a created WindowGlobalParent. Must
  // be called after setting the Manager actor.
  void Init() final;

  // `Init` for a WindowGlobalParent whose initializer `aSource` authored. The
  // object must have been constructed with the fields this process derived
  // (see `ComputeInitialFields`); `aRequested` are the fields `aSource` sent.
  // Reconciles the two and corrects `aSource` where its values were refused.
  // Must be called after binding to `aSource`.
  void InitFromContentProcess(const FieldValues& aRequested,
                              ContentParent* aSource);

  nsIGlobalObject* GetParentObject();
  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  void NotifyContentBlockingEvent(
      uint32_t aEvent, nsIRequest* aRequest, bool aBlocked,
      const nsACString& aTrackingOrigin,
      const nsTArray<nsCString>& aTrackingFullHashes,
      const Maybe<
          ContentBlockingNotifier::StorageAccessPermissionGrantedReason>&
          aReason,
      const Maybe<CanvasFingerprintingEvent>& aCanvasFingerprintingEvent);

  ContentBlockingLog* GetContentBlockingLog() { return &mContentBlockingLog; }

  // Apply the existing content-blocking gates (out-of-process, non-private,
  // top-level content) and, if they pass, flush the in-memory
  // ContentBlockingLog to the tracking database. Safe to call repeatedly;
  // ContentBlockingLog::ReportLog() emits only the delta since the last
  // flush, so no double-counting occurs.
  void MaybeReportContentBlockingLog();

  // Flush every live top-level WindowGlobalParent's content-blocking log to
  // the tracking database. Exposed to chrome JS via WebIDL so
  // TrackingDBService can force ingestion before a read query.
  static void FlushAllContentBlockingLogs(const GlobalObject& aGlobal) {
    FlushAllContentBlockingLogs();
  }
  static void FlushAllContentBlockingLogs();

  nsIDOMProcessParent* GetDomProcess();

  nsICookieJarSettings* CookieJarSettings() { return mCookieJarSettings; }

  nsICookieJarSettings* GetCookieJarSettings() const {
    return mCookieJarSettings;
  }

  bool DocumentHasLoaded() { return mDocumentHasLoaded; }

  bool MayHaveViewTransitionOptIn() const {
    return mMayHaveViewTransitionOptIn;
  }

  bool DocumentHasUserInteracted() { return mDocumentHasUserInteracted; }

  uint32_t SandboxFlags() { return mSandboxFlags; }

  bool GetDocumentBlockAllMixedContent() { return mBlockAllMixedContent; }

  bool GetDocumentUpgradeInsecureRequests() { return mUpgradeInsecureRequests; }

  void DidBecomeCurrentWindowGlobal(bool aCurrent);

  uint32_t HttpsOnlyStatus() { return mHttpsOnlyStatus; }

  void ApplySecurityStateToTop(uint32_t aStateFlags);
  uint32_t GetSecurityFlags() { return mSecurityState; }

  const RemoteType& GetRemoteType() const override;
  void GetRemoteType(nsACString& aRemoteType) const;

  void NotifySessionStoreUpdatesComplete(Element* aEmbedder);

  Maybe<uint64_t> GetSingleChannelId() { return mSingleChannelId; }

  uint32_t GetBFCacheStatus() { return mBFCacheStatus; }

  bool HasActivePeerConnections();

  bool Fullscreen() { return mFullscreen; }
  void SetFullscreen(bool aFullscreen) { mFullscreen = aFullscreen; }

  void ExitTopChromeDocumentFullscreen();

  void SetShouldReportHasBlockedOpaqueResponse(
      nsContentPolicyType aContentPolicy);

  // Get the nsIChannel which led to this document being loaded, if known.
  already_AddRefed<nsIChannel> GetDocumentChannel();

  // Get the nsIChannel which failed, leading to this error document being
  // loaded, if known.
  already_AddRefed<nsIChannel> GetFailedChannel();

  // The LoadingSessionHistoryInfo::mLoadId of the parent process load which
  // produced this document, or 0 if there was none.
  uint64_t DocumentHistoryLoadId() const { return mDocumentHistoryLoadId; }

  dom::NoCorsMediaRequestState NoCorsMediaRequestState(nsIURI* aURI);

  void RecordSubsequentNoCorsRequestState(nsIURI* aURI);

 protected:
  already_AddRefed<JSActor> InitJSActor(JS::Handle<JSObject*> aMaybeActor,
                                        const nsACString& aName,
                                        ErrorResult& aRv) override;
  mozilla::ipc::IProtocol* AsNativeActor() override { return this; }

  // IPC messages
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvLoadURI(
      const MaybeDiscarded<dom::BrowsingContext>& aTargetBC,
      nsDocShellLoadState* aLoadState, bool aSetNavigating);
  mozilla::ipc::IPCResult RecvInternalLoad(nsDocShellLoadState* aLoadState);
  mozilla::ipc::IPCResult RecvAddSecurityState(uint32_t aStateFlags);
  mozilla::ipc::IPCResult RecvUpdateDocumentURI(NotNull<nsIURI*> aURI);
  mozilla::ipc::IPCResult RecvUpdateDocumentPrincipal(
      Tainted<NotNull<RefPtr<nsIPrincipal>>>&& aNewDocumentPrincipal,
      Tainted<NotNull<RefPtr<nsIPrincipal>>>&&
          aNewDocumentPartitionedPrincipal);
  mozilla::ipc::IPCResult RecvUpdatePrincipalPartitioning(
      bool aPartitionStoragePrincipal);
  mozilla::ipc::IPCResult RecvUpdateDocumentHasLoaded(bool aDocumentHasLoaded);
  mozilla::ipc::IPCResult RecvSetMayHaveViewTransitionOptIn(bool aMayHave);
  mozilla::ipc::IPCResult RecvUpdateDocumentHasUserInteracted(
      bool aDocumentHasUserInteracted);
  mozilla::ipc::IPCResult RecvUpdateSandboxFlags(uint32_t aSandboxFlags);
  mozilla::ipc::IPCResult RecvUpdateDocumentCspSettings(
      bool aBlockAllMixedContent, bool aUpgradeInsecureRequests);
  mozilla::ipc::IPCResult RecvUpdateDocumentTitle(const nsString& aTitle);
  mozilla::ipc::IPCResult RecvUpdateHttpsOnlyStatus(uint32_t aHttpsOnlyStatus);
  mozilla::ipc::IPCResult RecvSetIsInitialDocument(bool aIsInitialDocument) {
    if (aIsInitialDocument && mIsInitialDocument.isSome() &&
        (mIsInitialDocument.value() != aIsInitialDocument)) {
      return IPC_FAIL_NO_REASON(this);
    }

    mIsInitialDocument = Some(aIsInitialDocument);
    mIsUncommittedInitialDocument = aIsInitialDocument;
    return IPC_OK();
  }
  mozilla::ipc::IPCResult RecvCommitToInitialDocument() {
    MOZ_ASSERT(mIsInitialDocument.isSome() && mIsInitialDocument.value());
    mIsUncommittedInitialDocument = false;
    return IPC_OK();
  }
  mozilla::ipc::IPCResult RecvUpdateChannels(
      ParentProcessChannelHandle* aDocumentHandle,
      ParentProcessChannelHandle* aFailedHandle);
  mozilla::ipc::IPCResult RecvSetClientInfo(
      Tainted<IPCClientInfo>&& aIPCClientInfo);
  mozilla::ipc::IPCResult RecvDestroy();
  mozilla::ipc::IPCResult RecvRawMessage(const JSActorMessageMeta& aMeta,
                                         JSIPCValue&& aData,
                                         StructuredCloneData* aStack);

  mozilla::ipc::IPCResult RecvGetContentBlockingEvents(
      GetContentBlockingEventsResolver&& aResolver);
  mozilla::ipc::IPCResult RecvUpdateCookieJarSettings(
      const CookieJarSettingsArgs& aCookieJarSettingsArgs);

  void ActorDestroy(ActorDestroyReason aWhy) override;

  void DrawSnapshotInternal(gfx::CrossProcessPaint* aPaint,
                            const Maybe<IntRect>& aRect, float aScale,
                            nscolor aBackgroundColor,
                            gfx::CrossProcessPaintFlags aFlags);

  // WebShare API - try to share
  mozilla::ipc::IPCResult RecvShare(IPCWebShareData&& aData,
                                    ShareResolver&& aResolver);

  mozilla::ipc::IPCResult RecvCheckPermitUnload(
      bool aHasInProcessBlocker, XPCOMPermitUnloadAction aAction,
      CheckPermitUnloadResolver&& aResolver);

  mozilla::ipc::IPCResult RecvExpectPageUseCounters(
      const MaybeDiscarded<dom::WindowContext>& aTop);
  mozilla::ipc::IPCResult RecvAccumulatePageUseCounters(
      const UseCounters& aUseCounters);

  mozilla::ipc::IPCResult RecvRequestRestoreTabContent();

  mozilla::ipc::IPCResult RecvUpdateBFCacheStatus(const uint32_t& aOnFlags,
                                                  const uint32_t& aOffFlags);

  // This IPC method is to notify the parent process that the caller process
  // creates the first active peer connection (aIsAdded = true) or closes the
  // last active peer connection (aIsAdded = false).
  mozilla::ipc::IPCResult RecvUpdateActivePeerConnectionStatus(bool aIsAdded);

 public:
  mozilla::ipc::IPCResult RecvSetSingleChannelId(
      const Maybe<uint64_t>& aSingleChannelId);

  mozilla::ipc::IPCResult RecvSetDocumentDomain(NotNull<nsIURI*> aDomain);

  mozilla::ipc::IPCResult RecvSetSiteIntegrityProtected(
      NotNull<nsIURI*> aSourceURI, uint64_t aMaxAge);

  nsresult DoAddCertException(bool aTemporary);
  mozilla::ipc::IPCResult RecvAddCertException(
      bool aTemporary, AddCertExceptionResolver&& aResolver);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvReloadWithHttpsOnlyException();

  mozilla::ipc::IPCResult RecvGetStorageAccessPermission(
      bool aIncludeIdentityCredential,
      GetStorageAccessPermissionResolver&& aResolve);

  mozilla::ipc::IPCResult RecvSetCookies(
      const OriginAttributes& aOriginAttributes, nsIURI* aHost,
      bool aIsThirdParty, const nsTArray<CookieStruct>& aCookies);

  mozilla::ipc::IPCResult RecvRecordUserActivationForBTP();

  mozilla::ipc::IPCResult RecvRecordUserInteractionForPermissions();

  mozilla::ipc::IPCResult RecvNotifyAudioSessionTypeOverride(
      const dom::AudioSessionType& aType);

  already_AddRefed<dom::PSerialManagerParent> AllocPSerialManagerParent();

  mozilla::ipc::IPCResult RecvPSerialManagerConstructor(
      PSerialManagerParent* aActor) override;

  already_AddRefed<dom::PWebAuthnTransactionParent>
  AllocPWebAuthnTransactionParent();

  already_AddRefed<dom::PWebIdentityParent> AllocPWebIdentityParent();

  already_AddRefed<dom::PDigitalCredentialParent>
  AllocPDigitalCredentialParent();

#ifdef ACCESSIBILITY
  already_AddRefed<a11y::PDocAccessibleParent> AllocPDocAccessibleParent(
      const uint64_t&, const bool&);
  mozilla::ipc::IPCResult RecvPDocAccessibleConstructor(
      a11y::PDocAccessibleParent* aDoc, const uint64_t& aParentID,
      const bool& aIsPrintDoc) override;
#endif

  // Spec: https://wicg.github.io/nav-speculation/prefetch.html#prefetch-record
  already_AddRefed<dom::PPrefetchRecordParent> AllocPPrefetchRecordParent(
      const dom::SpeculativePrefetchArgs& aArgs);

  // Called when a prefetch record's state changes (complete or canceled).
  // Wakes PrefetchMatchWaiters registered via WaitForMatchingPrefetchRecord.
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#wait-for-a-matching-prefetch-record
  void NotifyPrefetchStateChanged(dom::PrefetchRecordParent* aRec);
  void DedupePrefetchRecords(dom::PrefetchRecordParent* aJustCompleted);

  static void ClearPrefetchCache(nsIPrincipal* aPartitionedPrincipal);

  // "Find a matching complete prefetch record": synchronous lookup of a
  // completed prefetch record for navigation.
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#find-a-matching-complete-prefetch-record
  dom::PrefetchRecordParent* FindMatchingPrefetchRecord(nsIURI* aURI);

  // "Wait for a matching prefetch record": returns null if no ongoing
  // prefetch could match aURI, otherwise a promise that resolves when a match
  // completes (or with nullptr when none can, or when
  // dom.speculation_rules.wait_timeout_ms expires).
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#wait-for-a-matching-prefetch-record
  RefPtr<PrefetchMatchPromise> WaitForMatchingPrefetchRecord(nsIURI* aURI);

  // Whether some ongoing prefetch record could still become a matching
  // prefetch record for aURI once it completes. Used by
  // WaitForMatchingPrefetchRecord and by PrefetchMatchWaiter to decide
  // whether to keep waiting or give up early.
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#wait-for-a-matching-prefetch-record
  bool HasPotentialPrefetchMatch(nsIURI* aURI);

  // Whether a prefetch candidate covered aURI, whether or not it was fetched.
  // Branch-symmetric, so the experiment can compare prefetched navigations
  // against ones that would have been prefetched.
  bool HasMatchingPrefetchCandidate(nsIURI* aURI);

  void RemoveWaiter(PrefetchMatchWaiter* aWaiter);

  void UpdateFullscreenKeyboardLockStatus(FullscreenKeyboardLock aStatus);

 private:
  WindowGlobalParent(CanonicalBrowsingContext* aBrowsingContext,
                     uint64_t aInnerWindowId, uint64_t aOuterWindowId,
                     FieldValues&& aInit);

  ~WindowGlobalParent();

  bool ShouldTrackSiteOriginTelemetry();
  enum class PageUseCounterResultBits : uint8_t {
    WAITING,
    DATA_RECEIVED,
    EMPTY_DATA,
  };
  using PageUseCounterResult = EnumSet<PageUseCounterResultBits>;
  PageUseCounterResult FinishAccumulatingPageUseCounters();

  struct KnownAllowedSubsequentRequests : public SupportsWeakPtr {
    NS_INLINE_DECL_REFCOUNTING(KnownAllowedSubsequentRequests);
    nsTHashtable<nsCStringHashKey> mNoCorsMediaRequestURIs;
    nsCOMPtr<nsIPrincipal> mPrincipal;

   private:
    ~KnownAllowedSubsequentRequests();
  };

  using AllKnownAllowedSubsequentRequests =
      nsTHashMap<PrincipalHashKey, WeakPtr<KnownAllowedSubsequentRequests>>;

  static AllKnownAllowedSubsequentRequests&
  GetAllKnownAllowedSubsequentRequests();

  KnownAllowedSubsequentRequests* EnsureKnownAllowedSubsequentRequests();

  // NOTE: Neither this document principal nor the partitioned principal reflect
  // possible |document.domain| mutations which may have been made in the actual
  // document.
  nsCOMPtr<nsIPrincipal> mDocumentPrincipal;
  nsCOMPtr<nsIPrincipal> mDocumentPartitionedPrincipal;

  // The principal to use for the content blocking allow list.
  nsCOMPtr<nsIPrincipal> mDocContentBlockingAllowListPrincipal;

  nsCOMPtr<nsIURI> mDocumentURI;
  Maybe<nsString> mDocumentTitle;

  RefPtr<WindowGlobalParent> mStaticCloneOf;

  Maybe<bool> mIsInitialDocument;

  bool mIsUncommittedInitialDocument;

  // True if this window has a "beforeunload" event listener.
  bool mHasBeforeUnload;

  // The log of all content blocking actions taken on the document related to
  // this WindowGlobalParent. This is only stored on top-level documents and
  // includes the activity log for all of the nested subdocuments as well.
  ContentBlockingLog mContentBlockingLog;

  uint32_t mSecurityState = 0;

  Maybe<ClientInfo> mClientInfo;
  // Fields being mirrored from the corresponding document
  nsCOMPtr<nsICookieJarSettings> mCookieJarSettings;

  // The parent process channel which was used to create the current document.
  // May not match the channel in the content process in some cases.
  nsCOMPtr<nsIChannel> mDocumentChannel;

  // The failed parent process channel which led to an error page load.
  // May not match the channel in the content process in some cases.
  nsCOMPtr<nsIChannel> mFailedChannel;

  uint64_t mDocumentHistoryLoadId = 0;

  uint32_t mSandboxFlags;

  struct OriginCounter {
    void UpdateSiteOriginsFrom(WindowGlobalParent* aParent, bool aIncrease);
    void Accumulate();

    nsTHashMap<nsCStringHashKey, int32_t> mOriginMap;
    uint32_t mMaxOrigins = 0;
  };

  // Used to collect unique site origin telemetry.
  //
  // Is only Some() on top-level content windows.
  Maybe<OriginCounter> mOriginCounter;

  bool mDocumentHasLoaded;
  bool mDocumentHasUserInteracted;
  bool mMayHaveViewTransitionOptIn = false;
  bool mDocumentTreeWouldPreloadResources = false;
  bool mBlockAllMixedContent;
  bool mUpgradeInsecureRequests;
  bool mPartitionStoragePrincipal;

  // HTTPS-Only Mode flags
  uint32_t mHttpsOnlyStatus;

  // The window of the document whose page use counters our document's use
  // counters will contribute to.  (If we are a top-level document, this
  // will point to ourselves.)
  RefPtr<WindowGlobalParent> mPageUseCountersWindow;
  nsTArray<RefPtr<PrefetchMatchWaiter>> mPrefetchWaiters;

  // Our page use counters, if we are a top-level document.
  UniquePtr<PageUseCounters> mPageUseCounters;

  // Whether we have sent our page use counters, and so should ignore any
  // subsequent ExpectPageUseCounters calls.
  bool mSentPageUseCounters = false;

  uint32_t mBFCacheStatus = 0;

  // If this WindowGlobalParent is a top window, this field indicates how many
  // child processes have active peer connections for this window and its
  // descendants.
  uint32_t mNumOfProcessesWithActivePeerConnections = 0;

  // mSingleChannelId records whether the loadgroup contains a single request
  // with an id. If there is one channel in the loadgroup and it has an id then
  // mSingleChannelId is set to Some(id) (ids are non-zero). If there is one
  // request in the loadgroup and it's not a channel or it doesn't have an id,
  // or there are multiple requests in the loadgroup, then mSingleChannelId is
  // set to Some(0). If there are no requests in the loadgroup then
  // mSingleChannelId is set to Nothing().
  // Note: We ignore favicon loads when considering the requests in the
  // loadgroup.
  Maybe<uint64_t> mSingleChannelId;

  // True if the current loaded document is in fullscreen.
  bool mFullscreen = false;

  bool mShouldReportHasBlockedOpaqueResponse = false;

  // The per-principal set of media resources for which an initial no-cors media
  // request has passed the Opaque Response Blocking media checks, shared
  // between all windows with this window's principal. Used to recognise
  // subsequent (range) requests for the same resource.
  RefPtr<KnownAllowedSubsequentRequests> mKnownAllowedSubsequentRequests;

  static StaticAutoPtr<AllKnownAllowedSubsequentRequests>
      sAllKnownSubsequentRequests;
};

nsCString BFCacheStatusToString(uint32_t aFlags);

}  // namespace dom
}  // namespace mozilla

inline nsISupports* ToSupports(
    mozilla::dom::WindowGlobalParent* aWindowGlobal) {
  return static_cast<mozilla::dom::WindowContext*>(aWindowGlobal);
}

#endif  // !defined(mozilla_dom_WindowGlobalParent_h)
