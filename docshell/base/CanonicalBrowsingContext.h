/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_CanonicalBrowsingContext_h
#define mozilla_dom_CanonicalBrowsingContext_h

#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/MediaControlKeySource.h"
#include "mozilla/dom/BrowsingContextWebProgress.h"
#include "mozilla/dom/PermissionsPolicy.h"
#include "mozilla/dom/ProcessIsolation.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/SessionHistoryEntry.h"
#include "mozilla/dom/SessionStoreRestoreData.h"
#include "mozilla/dom/SessionStoreUtils.h"
#include "mozilla/dom/UniqueContentParentKeepAlive.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/RefPtr.h"
#include "mozilla/MozPromise.h"
#include "nsCycleCollectionParticipant.h"
#include "nsWrapperCache.h"
#include "nsTArray.h"
#include "nsTHashtable.h"
#include "nsHashKeys.h"
#include "nsISecureBrowserUI.h"

class nsIBrowserDOMWindow;
class nsISHistory;
class nsIWidget;
class nsIPrintSettings;
class nsSHistory;
class nsBrowserStatusFilter;
class nsSecureBrowserUI;
class CallerWillNotifyHistoryIndexAndLengthChanges;
class nsITimer;
class nsIScopedPrefs;

namespace mozilla {
enum class CallState;
class BounceTrackingState;

namespace embedding {
class PrintData;
}

namespace net {
class DocumentLoadListener;
}

namespace dom {

class BrowserParent;
class BrowserBridgeParent;
class PermissionsPolicy;
struct LoadURIOptions;
class MediaController;
enum class AudioFocusInterruptAction : uint8_t;
struct LoadingSessionHistoryInfo;
class SessionHistoryInfo;
class SSCacheCopy;
class WindowGlobalParent;
class SessionStoreFormData;
class SessionStoreScrollData;

// CanonicalBrowsingContext is a BrowsingContext living in the parent
// process, with whatever extra data that a BrowsingContext in the
// parent needs.
class CanonicalBrowsingContext final : public BrowsingContext {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(
      CanonicalBrowsingContext, BrowsingContext)

  static already_AddRefed<CanonicalBrowsingContext> Get(uint64_t aId);
  static CanonicalBrowsingContext* Cast(BrowsingContext* aContext);
  static const CanonicalBrowsingContext* Cast(const BrowsingContext* aContext);
  static already_AddRefed<CanonicalBrowsingContext> Cast(
      already_AddRefed<BrowsingContext> aContext);

  bool IsOwnedByProcess(uint64_t aProcessId) const {
    return mProcessId == aProcessId;
  }
  bool IsEmbeddedInProcess(uint64_t aProcessId) const {
    return mEmbedderProcessId == aProcessId;
  }
  bool IsKnownInSubTree(uint64_t aProcessId);
  uint64_t OwnerProcessId() const { return mProcessId; }
  uint64_t EmbedderProcessId() const { return mEmbedderProcessId; }
  ContentParent* GetContentParent() const;

  void GetCurrentRemoteType(nsACString& aRemoteType, ErrorResult& aRv) const;

  void SetOwnerProcessId(uint64_t aProcessId);

  // The ID of the BrowsingContext which caused this BrowsingContext to be
  // opened, or `0` if this is unknown.
  // Only set for toplevel content BrowsingContexts, and may be from a different
  // BrowsingContextGroup.
  uint64_t GetCrossGroupOpenerId() const { return mCrossGroupOpenerId; }
  already_AddRefed<CanonicalBrowsingContext> GetCrossGroupOpener() const;
  void SetCrossGroupOpenerId(uint64_t aOpenerId);
  void SetCrossGroupOpener(CanonicalBrowsingContext* aCrossGroupOpener,
                           ErrorResult& aRv);

  void GetWindowGlobals(nsTArray<RefPtr<WindowGlobalParent>>& aWindows);

  // The current active WindowGlobal.
  WindowGlobalParent* GetCurrentWindowGlobal() const;

  // Same as the methods on `BrowsingContext`, but with the types already cast
  // to the parent process type.
  CanonicalBrowsingContext* GetParent() {
    return Cast(BrowsingContext::GetParent());
  }
  CanonicalBrowsingContext* Top() { return Cast(BrowsingContext::Top()); }
  WindowGlobalParent* GetParentWindowContext();
  WindowGlobalParent* GetTopWindowContext();

  already_AddRefed<nsIWidget> GetParentProcessWidgetContaining();
  already_AddRefed<nsIBrowserDOMWindow> GetBrowserDOMWindow();

  // Same as `GetParentWindowContext`, but will also cross <browser> and
  // content/chrome boundaries.
  already_AddRefed<WindowGlobalParent> GetEmbedderWindowGlobal();

  CanonicalBrowsingContext* GetParentCrossChromeBoundary();
  CanonicalBrowsingContext* TopCrossChromeBoundary();
  Nullable<WindowProxyHolder> GetTopChromeWindow();

  nsISHistory* GetSessionHistory();
  SessionHistoryEntry* GetActiveSessionHistoryEntry();
  void SetActiveSessionHistoryEntryFromBFCache(SessionHistoryEntry* aEntry);

  bool ManuallyManagesActiveness() const;

  UniquePtr<LoadingSessionHistoryInfo> CreateLoadingSessionHistoryEntryForLoad(
      nsDocShellLoadState* aLoadState, SessionHistoryEntry* aExistingEntry,
      nsIChannel* aChannel);

  UniquePtr<LoadingSessionHistoryInfo> ReplaceLoadingSessionHistoryEntryForLoad(
      LoadingSessionHistoryInfo* aInfo, nsIChannel* aNewChannel);

  using PrintPromise =
      MozPromise<MaybeDiscardedBrowsingContext, nsresult, false>;
  MOZ_CAN_RUN_SCRIPT RefPtr<PrintPromise> Print(nsIPrintSettings*);
  MOZ_CAN_RUN_SCRIPT already_AddRefed<Promise> PrintJS(nsIPrintSettings*,
                                                       ErrorResult&);
  MOZ_CAN_RUN_SCRIPT RefPtr<PrintPromise> PrintWithNoContentAnalysis(
      nsIPrintSettings* aPrintSettings, bool aForceStaticDocument,
      const MaybeDiscardedBrowsingContext& aClonedStaticBrowsingContext);
  MOZ_CAN_RUN_SCRIPT void ReleaseClonedPrint(
      const MaybeDiscardedBrowsingContext& aClonedStaticBrowsingContext);

  enum class TopDescendantKind {
    // All top descendants are included, even those inside a nested top
    // browser.
    All,
    // Top descendants that are either direct children or under a non-nested
    // descendant are included, but not those nested inside a separate top.
    NonNested,
    // Only our direct children are included. This is usually slightly less
    // efficient than the alternatives, but might be needed in some cases.
    ChildrenOnly,
  };
  // Call the given callback on top-level descendant BrowsingContexts.
  // Return Callstate::Stop from the callback to stop calling further children.
  void CallOnTopDescendants(
      const FunctionRef<CallState(CanonicalBrowsingContext*)>& aCallback,
      TopDescendantKind aKind);

  void SessionHistoryCommit(uint64_t aLoadId, const nsID& aChangeID,
                            uint32_t aLoadType, bool aCloneEntryChildren,
                            bool aChannelExpired, uint32_t aCacheKey);

  // Calls the session history listeners' OnHistoryReload, storing the result in
  // aCanReload. If aCanReload is set to true and we have an active or a loading
  // entry then aLoadState will be initialized from that entry. If aCanReload
  // is true and aLoadState is not set then we should attempt to reload based
  // on the current document in the docshell.
  void NotifyOnHistoryReload(
      uint32_t aReloadFlags, bool& aCanReload,
      Maybe<NotNull<RefPtr<nsDocShellLoadState>>>& aLoadState);

  // See BrowsingContext::SetActiveSessionHistoryEntry. If
  // aKeepSharedState is true, aInfo's shared state is used even if the
  // previous active entry belongs to the same document.
  void SetActiveSessionHistoryEntry(const Maybe<nsPoint>& aPreviousScrollPos,
                                    SessionHistoryInfo* aInfo,
                                    uint32_t aLoadType,
                                    uint32_t aUpdatedCacheKey,
                                    const nsID& aChangeID,
                                    bool aKeepSharedState = false);

  void ReplaceActiveSessionHistoryEntry(SessionHistoryInfo* aInfo);

  // Gives `aInfo`, sent over IPC by the current document's process, the new
  // shared state that process asked for in `aNewState`. The principals come
  // from the document, and the state is bound to it. Returns false if the
  // request is refused. `aWasCreated` gets set to true if not null and it was
  // created.
  bool CreateSharedState(SessionHistoryInfo* aInfo,
                         const NewSessionHistorySharedState& aNewState,
                         bool* aWasCreated = nullptr);

  // Validates `aInfo` that has been sent over IPC: its shared state must be
  // bound to the current document, and its URI one the current document's
  // process could itself have loaded.
  bool ValidateSessionHistoryInfo(SessionHistoryInfo* aInfo) const;

  // The active entry's shared state keeps the principals of the load that
  // created it, e.g. a system triggering principal for a URL typed into the URL
  // bar, or the linking document's principal to inherit for a link click. If
  // content changed the entry's URI to one that inherits its principal, such as
  // document.open() from an about:srcdoc frame, and the principal the entry
  // would inherit isn't the current document's, makes it inherit the current
  // document's, so that loading the entry again still produces that document's
  // origin. Call once content's update has been validated and applied, as
  // applying it can replace the validated shared state with the one the
  // previous active entry of the same document had.
  void InheritDocumentPrincipalIfNeeded();

  // Moves the active entry's shared state binding from aPrevious to aCurrent if
  // both documents were produced by the same load and have the same principal,
  // as with the parts of a multipart response, which don't commit to session
  // history.
  void MaybeRebindActiveEntry(WindowGlobalParent* aPrevious,
                              WindowGlobalParent* aCurrent);

  void RemoveDynEntriesFromActiveSessionHistoryEntry();

  void RemoveFromSessionHistory(const nsID& aChangeID);

  MOZ_CAN_RUN_SCRIPT Maybe<int32_t> HistoryGo(
      int32_t aOffset, uint64_t aHistoryEpoch, bool aRequireUserInteraction,
      bool aUserActivation, bool aCheckForCancelation,
      Maybe<ContentParentId> aContentId,
      std::function<void(nsresult)>&& aResolver = [](nsresult) {});

  MOZ_CAN_RUN_SCRIPT void NavigationTraverse(
      const nsID& aKey, uint64_t aHistoryEpoch, bool aUserActivation,
      bool aCheckForCancelation, Maybe<ContentParentId> aContentId,
      std::function<void(nsresult)>&& aResolver);

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  // Dispatches a wheel zoom change to the embedder element.
  void DispatchWheelZoomChange(bool aIncrease);

  // This function is used to start the autoplay media which are delayed to
  // start. If needed, it would also notify the content browsing context which
  // are related with the canonical browsing content tree to start delayed
  // autoplay media.
  void NotifyStartDelayedAutoplayMedia();

  // Return the number of unique site origins by iterating all given BCs,
  // including their subtrees.
  static uint32_t CountSiteOrigins(
      GlobalObject& aGlobal,
      const Sequence<mozilla::OwningNonNull<BrowsingContext>>& aRoots);

  // Return true if a private browsing session is active.
  static bool IsPrivateBrowsingActive();

  // This function would propogate the action to its all child browsing contexts
  // in content processes.
  void UpdateMediaControlAction(const MediaControlAction& aAction);

  // Propagate an audio-focus interrupt (suspend or resume) to this browsing
  // context tree across content processes.
  void UpdateMediaSessionInterrupt(AudioFocusInterruptAction aAction);

  // Triggers a load in the process
  using BrowsingContext::LoadURI;
  MOZ_CAN_RUN_SCRIPT
  void FixupAndLoadURIString(const nsAString& aURI,
                             const LoadURIOptions& aOptions,
                             ErrorResult& aError);
  MOZ_CAN_RUN_SCRIPT
  void LoadURI(nsIURI* aURI, const LoadURIOptions& aOptions,
               ErrorResult& aError);

  MOZ_CAN_RUN_SCRIPT
  void GoBack(const Optional<int32_t>& aCancelContentJSEpoch,
              bool aRequireUserInteraction, bool aUserActivation);
  MOZ_CAN_RUN_SCRIPT
  void GoForward(const Optional<int32_t>& aCancelContentJSEpoch,
                 bool aRequireUserInteraction, bool aUserActivation);
  MOZ_CAN_RUN_SCRIPT
  void GoToIndex(int32_t aIndex, const Optional<int32_t>& aCancelContentJSEpoch,
                 bool aUserActivation);
  MOZ_CAN_RUN_SCRIPT
  void Reload(uint32_t aReloadFlags);
  void Stop(uint32_t aStopFlags);

  // Get the publicly exposed current URI loaded in this BrowsingContext.
  already_AddRefed<nsIURI> GetCurrentURI() const;
  void SetCurrentRemoteURI(nsIURI* aCurrentRemoteURI);

  BrowserParent* GetBrowserParent() const;
  void SetCurrentBrowserParent(BrowserParent* aBrowserParent);

  // Internal method to change which process a BrowsingContext is being loaded
  // in. The returned promise will resolve when the process switch is completed.
  // The returned CanonicalBrowsingContext may be different than |this| if a BCG
  // switch was performed.
  //
  // A RemoteType::NotRemote() aRemoteType argument will perform a process
  // switch into the parent process, and the method will resolve with a null
  // BrowserParent.
  using RemotenessPromise = MozPromise<
      std::pair<RefPtr<BrowserParent>, RefPtr<CanonicalBrowsingContext>>,
      nsresult, false>;
  MOZ_CAN_RUN_SCRIPT
  RefPtr<RemotenessPromise> ChangeRemoteness(
      const NavigationIsolationOptions& aOptions, uint64_t aPendingSwitchId);

  // Return a media controller from the top-level browsing context that can
  // control all media belonging to this browsing context tree. Return nullptr
  // if the top-level browsing context has been discarded.
  MediaController* GetMediaController();
  bool HasCreatedMediaController() const;

  // Attempts to start loading the given load state in this BrowsingContext,
  // in parallel with a DocumentChannelChild being created in the docshell.
  // Requires the DocumentChannel to connect with this load for it to
  // complete successfully.
  bool AttemptSpeculativeLoadInParent(nsDocShellLoadState* aLoadState);

  // Get or create a secure browser UI for this BrowsingContext
  nsISecureBrowserUI* GetSecureBrowserUI();

  BrowsingContextWebProgress* GetWebProgress() { return mWebProgress; }

  // Called when the current URI changes (from an
  // nsIWebProgressListener::OnLocationChange event, so that we
  // can update our security UI for the new location, or when the
  // mixed content/https-only state for our current window is changed.
  void UpdateSecurityState();

  // Called when a navigation forces us to recreate our browsing
  // context (for example, when switching in or out of the parent
  // process).
  // aNewContext is the newly created BrowsingContext that is replacing
  // us.
  void ReplacedBy(CanonicalBrowsingContext* aNewContext,
                  const NavigationIsolationOptions& aRemotenessOptions);

  bool HasHistoryEntry(SessionHistoryEntry* aEntry);
  bool HasLoadingHistoryEntry(SessionHistoryEntry* aEntry) {
    for (const LoadingSessionHistoryEntry& loading : mLoadingEntries) {
      if (loading.mEntry == aEntry) {
        return true;
      }
    }
    return false;
  }

  void SwapHistoryEntries(SessionHistoryEntry* aOldEntry,
                          SessionHistoryEntry* aNewEntry);

  void AddLoadingSessionHistoryEntry(uint64_t aLoadId,
                                     SessionHistoryEntry* aEntry);

  void AdoptChildSHEntry(Maybe<LoadingSessionHistoryInfo>& aLoadingInfo);

  MOZ_CAN_RUN_SCRIPT
  void HistoryCommitIndexAndLength();

  void DeactivateDocuments(
      const SessionHistoryInfo* aPageSwapTargetInfo = nullptr,
      NavigationType aNavigationType = NavigationType::Push,
      bool aForUnload = false);

  MOZ_CAN_RUN_SCRIPT
  void ReactivateDocuments(SessionHistoryEntry* aEntry,
                           SessionHistoryEntry* aPreviousEntryForActivation);

  void SynchronizeLayoutHistoryState();

  void SynchronizeNavigationAPIState(nsIStructuredCloneContainer* aState);

  void ResetScalingZoom();

  void SetContainerPermissionsPolicy(
      Maybe<PermissionsPolicyInfo>&& aContainerPermissionsPolicyInfo);
  const Maybe<PermissionsPolicyInfo>& GetContainerPermissionsPolicy() const {
    return mContainerPermissionsPolicyInfo;
  }

  void SetRestoreData(SessionStoreRestoreData* aData, ErrorResult& aError);
  void ClearRestoreState();
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void RequestRestoreTabContent(
      WindowGlobalParent* aWindow);
  already_AddRefed<Promise> GetRestorePromise();

  nsresult WriteSessionStorageToSessionStore(
      const nsTArray<SSCacheCopy>& aSesssionStorage, uint32_t aEpoch);

  void UpdateSessionStoreSessionStorage(const std::function<void()>& aDone);

  static void UpdateSessionStoreForStorage(uint64_t aBrowsingContextId);

  // Called when a BrowserParent for this BrowsingContext has been fully
  // destroyed (i.e. `ActorDestroy` was called).
  void BrowserParentDestroyed(BrowserParent* aBrowserParent,
                              bool aAbnormalShutdown);

  void StartUnloadingHost(uint64_t aChildID);
  void ClearUnloadingHost(uint64_t aChildID);

  bool AllowedInBFCache(const Maybe<uint64_t>& aChannelId, nsIURI* aNewURI);

 private:
  static nsresult ContainsSameOriginBfcacheEntry(
      SessionHistoryEntry* aEntry, mozilla::dom::BrowsingContext* aBC,
      int32_t aChildIndex, void* aData);

 public:
  // Removes all bfcache entries that match the origin + originAttributes of the
  // principal. Must be passed partitionedPrincipal
  static nsresult ClearBfcacheByPrincipal(nsIPrincipal* aPrincipal);

  // Methods for getting and setting the active state for top level
  // browsing contexts, for the process priority manager.
  bool IsPriorityActive() const {
    MOZ_RELEASE_ASSERT(IsTop());
    return mPriorityActive;
  }
  void SetPriorityActive(bool aIsActive) {
    MOZ_RELEASE_ASSERT(IsTop());
    mPriorityActive = aIsActive;
  }

  void GetDownloadFolderOverride(nsString& aOut) const {
    if (IsTop()) {
      aOut = mDownloadFolderOverride;
    }
  }
  void SetDownloadFolderOverride(const nsAString& aValue, ErrorResult& aRv) {
    if (!IsTop()) {
      aRv.ThrowInvalidStateError(
          "downloadFolderOverride can only be set on the top "
          "BrowsingContext");
      return;
    }
    mDownloadFolderOverride = aValue;
  }

  void SetIsActive(bool aIsActive, ErrorResult& aRv);

  void SetIsActiveInternal(bool aIsActive, ErrorResult& aRv) {
    ExplicitActiveStatus newValue = aIsActive ? ExplicitActiveStatus::Active
                                              : ExplicitActiveStatus::Inactive;
    SetExplicitActive(newValue, aRv);
  }

  void SetTouchEventsOverride(dom::TouchEventsOverride, ErrorResult& aRv);
  void SetTargetTopLevelLinkClicksToBlank(bool aTargetTopLevelLinkClicksToBlank,
                                          ErrorResult& aRv);

  bool IsReplaced() const { return mIsReplaced; }

#ifdef ANDROID
  uint32_t GetAndroidAppLinkLaunchType() const {
    return mAndroidAppLinkLaunchType;
  }
  void SetAndroidAppLinkLaunchType(uint32_t aType) {
    mAndroidAppLinkLaunchType = aType;
  }
#endif

  const JS::Heap<JS::Value>& PermanentKey() { return mPermanentKey; }
  void ClearPermanentKey() { mPermanentKey.setNull(); }

  void SetCrossGroupEmbedderElement(Element* aEmbedder);

  // When request for page awake, it would increase a count that is used to
  // prevent whole browsing context tree from being suspended. The request can
  // be called multiple times. When calling the revoke, it would decrease the
  // count and once the count reaches to zero, the browsing context tree could
  // be suspended when the tree is inactive.
  void AddPageAwakeRequest();
  void RemovePageAwakeRequest();

  MOZ_CAN_RUN_SCRIPT
  void CloneDocumentTreeInto(CanonicalBrowsingContext* aSource,
                             const RemoteType& aRemoteType,
                             embedding::PrintData&& aPrintData);

  // Returns a Promise which resolves when cloning documents for printing
  // finished if this browsing context is cloning document tree.
  RefPtr<GenericNonExclusivePromise> GetClonePromise() const {
    return mClonePromise;
  }

  bool StartApzAutoscroll(float aAnchorX, float aAnchorY, nsViewID aScrollId,
                          uint32_t aPresShellId);
  void StopApzAutoscroll(nsViewID aScrollId, uint32_t aPresShellId);

  void AddFinalDiscardListener(std::function<void(uint64_t)>&& aListener);

  bool ForceAppWindowActive() const { return mForceAppWindowActive; }
  void SetForceAppWindowActive(bool, ErrorResult&);
  void RecomputeAppWindowVisibility();

  void IncrementDocumentPiPWindowCount();
  void DecrementDocumentPiPWindowCount();

  already_AddRefed<nsISHEntry> GetMostRecentLoadingSessionHistoryEntry();

  already_AddRefed<BounceTrackingState> GetBounceTrackingState();
  already_AddRefed<nsIScopedPrefs> GetScopedPrefs();

  bool CanOpenModalPicker();

  static bool ShouldEnforceParentalControls();

  // Get the load listener for the current load in this browsing context.
  already_AddRefed<net::DocumentLoadListener> GetCurrentLoad();

  // https://html.spec.whatwg.org/#concept-internal-location-ancestor-origin-objects-list
  void CreateRedactedAncestorOriginsList(
      nsIPrincipal* aThisDocumentPrincipal,
      ReferrerPolicy aFrameReferrerPolicyAttribute);

  Span<const nsCOMPtr<nsIPrincipal>> GetPossiblyRedactedAncestorOriginsList()
      const;
  void SetPossiblyRedactedAncestorOriginsList(
      nsTArray<nsCOMPtr<nsIPrincipal>> aAncestorOriginsList);

  void SetEmbedderFrameReferrerPolicy(ReferrerPolicy aPolicy);

  // Called when we need to snap shot referrer policy for ancestorOrigins
  // and also when building the internal ancestor origins list for about:blank
  // because it needs special handling.
  ReferrerPolicy GetEmbedderFrameReferrerPolicy() const {
    return mEmbedderFrameReferrerPolicy;
  }

 protected:
  // Called when the browsing context is being discarded.
  void CanonicalDiscard();

  // Called when the browsing context is being attached.
  void CanonicalAttach();

  // Called when the browsing context private mode is changed after
  // being attached, but before being discarded.
  void AdjustPrivateBrowsingCount(bool aPrivateBrowsing);

  using Type = BrowsingContext::Type;
  CanonicalBrowsingContext(WindowContext* aParentWindow,
                           BrowsingContextGroup* aGroup,
                           uint64_t aBrowsingContextId,
                           uint64_t aOwnerProcessId,
                           uint64_t aEmbedderProcessId, Type aType,
                           FieldValues&& aInit);

 private:
  friend class BrowsingContext;

  virtual ~CanonicalBrowsingContext();

  class PendingRemotenessChange {
   public:
    NS_INLINE_DECL_REFCOUNTING(PendingRemotenessChange)

    PendingRemotenessChange(CanonicalBrowsingContext* aTarget,
                            RemotenessPromise::Private* aPromise,
                            uint64_t aPendingSwitchId,
                            const NavigationIsolationOptions& aOptions);

    void Cancel(nsresult aRv);

   private:
    friend class CanonicalBrowsingContext;

    ~PendingRemotenessChange();
    MOZ_CAN_RUN_SCRIPT
    void ProcessLaunched();
    MOZ_CAN_RUN_SCRIPT
    void ProcessReady();
    MOZ_CAN_RUN_SCRIPT
    void MaybeFinish();
    void Clear();

    MOZ_CAN_RUN_SCRIPT
    nsresult FinishTopContent();
    nsresult FinishSubframe();

    RefPtr<CanonicalBrowsingContext> mTarget;
    RefPtr<RemotenessPromise::Private> mPromise;
    UniqueContentParentKeepAlive mContentParentKeepAlive;
    RefPtr<BrowsingContextGroup> mSpecificGroup;

    bool mProcessReady = false;
    bool mWaitingForPrepareToChange = false;

    uint64_t mPendingSwitchId;
    NavigationIsolationOptions mOptions;
  };

  struct RestoreState {
    NS_INLINE_DECL_REFCOUNTING(RestoreState)

    void ClearData() { mData = nullptr; }
    void Resolve();

    RefPtr<SessionStoreRestoreData> mData;
    RefPtr<Promise> mPromise;
    uint32_t mRequests = 0;
    uint32_t mResolves = 0;

   private:
    ~RestoreState() = default;
  };

  friend class net::DocumentLoadListener;
  // Called when a DocumentLoadListener is created to start a load for
  // this browsing context. Returns false if a higher priority load is
  // already in-progress and the new one has been rejected.
  bool StartDocumentLoad(net::DocumentLoadListener* aLoad);
  // Called once DocumentLoadListener completes handling a load, and it
  // is either complete, or handed off to the final channel to deliver
  // data to the destination docshell.
  // If aContinueNavigating it set, the reference to the DocumentLoadListener
  // will be cleared to prevent it being cancelled, however the current load ID
  // will be preserved until `EndDocumentLoad` is called again.
  void EndDocumentLoad(bool aContinueNavigating);

  bool SupportsLoadingInParent(nsDocShellLoadState* aLoadState,
                               uint64_t* aOuterWindowId);

  void HistoryCommitIndexAndLength(
      const nsID& aChangeID,
      const CallerWillNotifyHistoryIndexAndLengthChanges& aProofOfCaller);

  struct UnloadingHost {
    uint64_t mChildID;
    nsTArray<std::function<void()>> mCallbacks;
  };
  nsTArray<UnloadingHost>::iterator FindUnloadingHost(uint64_t aChildID);

  // Called when we want to show the subframe crashed UI as our previous browser
  // has become unloaded for one reason or another.
  void ShowSubframeCrashedUI(BrowserBridgeParent* aBridge);

  void MaybeScheduleSessionStoreUpdate();

  void CancelSessionStoreUpdate();

  void AddPendingDiscard();

  void RemovePendingDiscard();

  bool ShouldAddEntryForRefresh(const SessionHistoryEntry* aEntry) {
    return ShouldAddEntryForRefresh(aEntry->Info().GetURI(),
                                    aEntry->Info().HasPostData());
  }
  bool ShouldAddEntryForRefresh(nsIURI* aNewURI, bool aHasPostData) {
    nsCOMPtr<nsIURI> currentURI = GetCurrentURI();
    return BrowsingContext::ShouldAddEntryForRefresh(currentURI, aNewURI,
                                                     aHasPostData);
  }

  already_AddRefed<nsDocShellLoadState> CreateLoadInfo(
      SessionHistoryEntry* aEntry, NavigationType aNavigationType);

  void GetContiguousEntriesForLoad(LoadingSessionHistoryInfo& aLoadingInfo,
                                   const RefPtr<SessionHistoryEntry>& aEntry);

  void MaybeReuseNavigationKeyFromActiveEntry(SessionHistoryEntry* aEntry);

  // XXX(farre): Store a ContentParent pointer here rather than mProcessId?
  // Indicates which process owns the docshell.
  uint64_t mProcessId;

  // Indicates which process owns the embedder element.
  uint64_t mEmbedderProcessId;

  uint64_t mCrossGroupOpenerId = 0;

  // Window ID of the cross-group WindowGlobalParent which most recently
  // embedded this toplevel content BrowsingContext.
  //
  // This member is 0 for subframes, toplevel chrome documents, and windows
  // which have not yet been embedded within a cross-group context.
  //
  // Unlike the embedder element, this will not be cleared when the document is
  // removed from the DOM, and can be used on a CanonicalBrowsingContext which
  // is actively being torn down.
  uint64_t mCrossGroupEmbedderWindowId = 0;

  // This function will make the top window context reset its
  // "SHEntryHasUserInteraction" cache that prevents documents from repeatedly
  // setting user interaction on SH entries. Should be called anytime SH
  // entries are added or replaced.
  void ResetSHEntryHasUserInteractionCache();

  RefPtr<BrowserParent> mCurrentBrowserParent;

  nsTArray<UnloadingHost> mUnloadingHosts;

  // The current URI loaded in this BrowsingContext. This value is only set for
  // BrowsingContexts loaded in content processes.
  nsCOMPtr<nsIURI> mCurrentRemoteURI;

  // The current remoteness change which is in a pending state.
  RefPtr<PendingRemotenessChange> mPendingRemotenessChange;

  RefPtr<nsSHistory> mSessionHistory;

  // Tab media controller is used to control all media existing in the same
  // browsing context tree, so it would only exist in the top level browsing
  // context.
  RefPtr<MediaController> mTabMediaController;

  RefPtr<net::DocumentLoadListener> mCurrentLoad;

  struct LoadingSessionHistoryEntry {
    uint64_t mLoadId = 0;
    RefPtr<SessionHistoryEntry> mEntry;
  };
  nsTArray<LoadingSessionHistoryEntry> mLoadingEntries;
  RefPtr<SessionHistoryEntry> mActiveEntry;

  RefPtr<nsSecureBrowserUI> mSecureBrowserUI;
  RefPtr<BrowsingContextWebProgress> mWebProgress;

  // ScopedPrefs is set on all top-level browsing contexts and is shared
  // across navigation, therefore lifetime of tab.
  nsCOMPtr<nsIScopedPrefs> mScopedPrefs;

  nsCOMPtr<nsIWebProgressListener> mDocShellProgressBridge;
  RefPtr<nsBrowserStatusFilter> mStatusFilter;

  Maybe<PermissionsPolicyInfo> mContainerPermissionsPolicyInfo;

  friend class BrowserSessionStore;
  WeakPtr<SessionStoreFormData>& GetSessionStoreFormDataRef() {
    return mFormdata;
  }
  WeakPtr<SessionStoreScrollData>& GetSessionStoreScrollDataRef() {
    return mScroll;
  }

  WeakPtr<SessionStoreFormData> mFormdata;
  WeakPtr<SessionStoreScrollData> mScroll;

  RefPtr<RestoreState> mRestoreState;

  nsCOMPtr<nsITimer> mSessionStoreSessionStorageUpdateTimer;

  // If this is a top level context, this is true if our browser ID is marked as
  // active in the process priority manager.
  bool mPriorityActive = false;

  // If this is a top level context, an override for the default downloads
  // directory, set via WebDriver BiDi's.
  nsString mDownloadFolderOverride;

  // See CanonicalBrowsingContext.forceAppWindowActive.
  bool mForceAppWindowActive = false;

  uint32_t mDocumentPiPWindowCount = 0;

  bool mIsReplaced = false;

#ifdef ANDROID
  // App link launch type for the current load; 0 means not an app link.
  // Stored here so it survives process switches and COOP-triggered BC swaps.
  uint32_t mAndroidAppLinkLaunchType = 0;
#endif

  // A Promise created when cloning documents for printing.
  RefPtr<GenericNonExclusivePromise> mClonePromise;

  JS::Heap<JS::Value> mPermanentKey;

  uint32_t mPendingDiscards = 0;

  bool mFullyDiscarded = false;
  // the referrerPolicy attribute of the iframe hosting this browsing context
  // defaults to the empty string
  ReferrerPolicy mEmbedderFrameReferrerPolicy = ReferrerPolicy::_empty;

  nsTArray<std::function<void(uint64_t)>> mFullyDiscardedListeners;

  // https://html.spec.whatwg.org/#concept-internal-location-ancestor-origin-objects-list
  nsTArray<nsCOMPtr<nsIPrincipal>> mPossiblyRedactedAncestorOriginsList;
};

}  // namespace dom
}  // namespace mozilla

#endif  // !defined(mozilla_dom_CanonicalBrowsingContext_h)
