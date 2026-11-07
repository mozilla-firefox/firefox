/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_DocumentLoadListener_h
#define mozilla_net_DocumentLoadListener_h

#include "EarlyHintsService.h"
#include "mozilla/MozPromise.h"
#include "mozilla/Variant.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/PrefetchMatchWaiter.h"
#include "mozilla/dom/RemoteType.h"
#include "mozilla/dom/SessionHistoryEntry.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/net/NeckoCommon.h"
#include "mozilla/net/NeckoParent.h"
#include "mozilla/net/PDocumentChannelParent.h"
#include "mozilla/net/ParentChannelListener.h"
#include "nsDOMNavigationTiming.h"
#include "nsIBrowser.h"
#include "nsIChannelEventSink.h"
#include "nsIEarlyHintObserver.h"
#include "nsIInterfaceRequestor.h"
#include "nsIMultiPartChannel.h"
#include "nsIParentChannel.h"
#include "nsIParentRedirectingChannel.h"
#include "nsIProgressEventSink.h"
#include "nsIRedirectResultListener.h"

#define DOCUMENT_LOAD_LISTENER_IID \
  {0x3b393c56, 0x9e01, 0x11e9, {0xa2, 0xa3, 0x2a, 0x2a, 0xe2, 0xdb, 0xcc, 0xe4}}

namespace mozilla {
namespace dom {
class CanonicalBrowsingContext;
class ParentProcessChannelHandle;
struct NavigationIsolationOptions;
}  // namespace dom
namespace net {
using ChildEndpointPromise =
    MozPromise<mozilla::ipc::Endpoint<extensions::PStreamFilterChild>, bool,
               true>;

// If we've been asked to attach a stream filter to our channel,
// then we return this promise and defer until we know the final
// content process. At that point we setup Endpoints between
// mStramFilterProcessId and the new content process, and send
// the parent Endpoint to the new process.
// Once we have confirmation of that being bound in the content
// process, we resolve the promise the child Endpoint.
struct StreamFilterRequest {
  StreamFilterRequest() = default;
  StreamFilterRequest(StreamFilterRequest&&) = default;
  ~StreamFilterRequest() {
    if (mPromise) {
      mPromise->Reject(false, __func__);
    }
  }
  RefPtr<ChildEndpointPromise::Private> mPromise;
  mozilla::ipc::Endpoint<extensions::PStreamFilterChild> mChildEndpoint;
};
}  // namespace net
}  // namespace mozilla
MOZ_DECLARE_RELOCATE_USING_MOVE_CONSTRUCTOR(mozilla::net::StreamFilterRequest)

namespace mozilla {
namespace net {

class LoadInfo;

/**
 * DocumentLoadListener represents a connecting document load for a
 * CanonicalBrowsingContext (in the parent process).
 *
 * It creates a network channel for the document load and then waits for it to
 * receive a response (after all redirects are resolved). It then decides where
 * to handle that load (could be in a different process from the initiator),
 * and then sets up a real network nsIChannel to deliver the data to the final
 * destination docshell, maybe through an nsIParentChannel/nsIChildChannel IPDL
 * layer.
 *
 * In the case where this was initiated from an nsDocShell, we also create an
 * nsIChannel to act as a placeholder within the docshell while this process
 * completes, and then notify the docshell of a 'redirect' when we replace this
 * channel with the real one.
 */

// TODO: We currently don't implement nsIProgressEventSink and forward those
// to the child. Should we? We get the interface requested.
class DocumentLoadListener : public nsIInterfaceRequestor,
                             public nsIAsyncVerifyRedirectReadyCallback,
                             public nsIParentChannel,
                             public nsIChannelEventSink,
                             public HttpChannelSecurityWarningReporter,
                             public nsIMultiPartChannelListener,
                             public nsIProgressEventSink,
                             public nsIEarlyHintObserver {
 public:
  // See the comment on GetLoadingBrowsingContext for explanation of
  // aLoadingBrowsingContext.
  DocumentLoadListener(dom::CanonicalBrowsingContext* aLoadingBrowsingContext,
                       bool aIsDocumentLoad);

  struct OpenPromiseSucceededType {
    nsTArray<mozilla::ipc::Endpoint<extensions::PStreamFilterParent>>
        mStreamFilterEndpoints;
    uint32_t mRedirectFlags;
    uint32_t mLoadFlags;
    uint32_t mEarlyHintLinkType;
    RefPtr<PDocumentChannelParent::RedirectToRealChannelPromise::Private>
        mPromise;
  };
  struct OpenPromiseFailedType {
    nsresult mStatus;
    nsresult mLoadGroupStatus;
    // This is set to true if the navigation in the content process should not
    // be cancelled, as the load is logically continuing within the current
    // browsing session, just within a different process or browsing context.
    bool mContinueNavigating = false;
  };

  using OpenPromise =
      MozPromise<OpenPromiseSucceededType, OpenPromiseFailedType, true>;

  // Interface which may be provided when performing an <object> or <embed> load
  // with `DocumentLoadListener`, to allow upgrading the Object load to a proper
  // Document load.
  struct ObjectUpgradeHandler : public SupportsWeakPtr {
    using ObjectUpgradePromise =
        MozPromise<RefPtr<dom::CanonicalBrowsingContext>, nsresult,
                   true /* isExclusive */>;

    // Upgrade an object load to be a potentially remote document.
    //
    // The returned promise will resolve with the BrowsingContext which has been
    // created in the <object> or <embed> element to finish the load with.
    virtual RefPtr<ObjectUpgradePromise> UpgradeObjectLoad() = 0;
  };

 private:
  // Creates the channel, and then calls AsyncOpen on it.
  // The DocumentLoadListener will require additional process from the consumer
  // in order to complete the redirect to the end channel. This is done by
  // returning a RedirectToRealChannelPromise and then waiting for it to be
  // resolved or rejected accordingly.
  // Once that promise is resolved; the consumer no longer needs to hold a
  // reference to the DocumentLoadListener nor will the consumer required to be
  // used again.
  RefPtr<OpenPromise> Open(nsDocShellLoadState* aLoadState, LoadInfo* aLoadInfo,
                           nsLoadFlags aLoadFlags, uint32_t aCacheKey,
                           const Maybe<uint64_t>& aChannelId,
                           const TimeStamp& aAsyncOpenTime,
                           nsDOMNavigationTiming* aTiming,
                           Maybe<dom::ClientInfo>&& aInfo, bool aUrgentStart,
                           dom::ContentParent* aContentParent, nsresult* aRv);

 public:
  RefPtr<OpenPromise> OpenDocument(
      nsDocShellLoadState* aLoadState, nsLoadFlags aLoadFlags,
      uint32_t aCacheKey, const Maybe<uint64_t>& aChannelId,
      const TimeStamp& aAsyncOpenTime, nsDOMNavigationTiming* aTiming,
      Maybe<dom::ClientInfo>&& aInfo, bool aUriModified,
      Maybe<bool> aIsEmbeddingBlockedError, dom::ContentParent* aContentParent,
      nsresult* aRv);

  RefPtr<OpenPromise> OpenObject(
      nsDocShellLoadState* aLoadState, uint32_t aCacheKey,
      const Maybe<uint64_t>& aChannelId, const TimeStamp& aAsyncOpenTime,
      nsDOMNavigationTiming* aTiming, Maybe<dom::ClientInfo>&& aInfo,
      uint64_t aInnerWindowId, nsLoadFlags aLoadFlags,
      nsContentPolicyType aContentPolicyType, bool aUrgentStart,
      dom::ContentParent* aContentParent,
      ObjectUpgradeHandler* aObjectUpgradeHandler, nsresult* aRv);

  // Creates a DocumentLoadListener entirely in the parent process and opens it,
  // and never needs a DocumentChannel to connect to an existing docshell.
  // Once we get a response it takes the 'process switch' path to find the right
  // process and docshell, and delivers the response there directly.
  static bool LoadInParent(dom::CanonicalBrowsingContext* aBrowsingContext,
                           nsDocShellLoadState* aLoadState,
                           bool aSetNavigating);

  static uint32_t SecurityFlagsForLoadInfo(nsDocShellLoadState*);
  static uint32_t EffectiveSandboxFlags(uint32_t aBaseSandboxFlags,
                                        nsSecurityFlags);

  // Creates a DocumentLoadListener directly in the parent process and opens it,
  // without needing an existing DocumentChannel.
  // If successful, aLoadState will be given a SpeculativeListener, which will
  // be used for the load when the DocumentChannel is connected from the content
  // process.
  static bool SpeculativeLoadInParent(
      dom::CanonicalBrowsingContext* aBrowsingContext,
      nsDocShellLoadState* aLoadState);

  // Called when we were created without a document channel, and the
  // nsDocShellLoadState was destroyed without the load being claimed.
  void CleanupParentLoadAttempt();

  // Called when the content process's DocumentChannel has connected to this
  // speculative load. Returns the promise for the load's result.
  RefPtr<OpenPromise> ClaimParentLoad();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSIPARENTCHANNEL
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSIASYNCVERIFYREDIRECTREADYCALLBACK
  NS_DECL_NSICHANNELEVENTSINK
  NS_DECL_NSIMULTIPARTCHANNELLISTENER
  NS_DECL_NSIPROGRESSEVENTSINK
  NS_DECL_NSIEARLYHINTOBSERVER

  // We suspend the underlying channel when replacing ourselves with
  // the real listener channel.
  // This helper resumes the underlying channel again, and manually
  // forwards any nsIStreamListener messages that arrived while we
  // were suspended (which might have failed).
  // Returns true if the channel was finished before we could resume it.
  bool ResumeSuspendedChannel(nsIStreamListener* aListener);

  NS_INLINE_DECL_STATIC_IID(DOCUMENT_LOAD_LISTENER_IID)

  // Called by the DocumentChannel if cancelled.
  void Cancel(const nsresult& aStatusCode, const nsACString& aReason);

  nsIChannel* GetChannel() const { return mChannel; }

  uint32_t GetRedirectChannelId() const { return mRedirectChannelId; }

  const Maybe<uint64_t>& GetDocumentChannelId() const {
    return mDocumentChannelId;
  }

  nsresult ReportSecurityMessage(const nsAString& aMessageTag,
                                 const nsAString& aMessageCategory) override {
    ReportSecurityMessageParams params;
    params.mMessageTag = aMessageTag;
    params.mMessageCategory = aMessageCategory;
    mSecurityWarningFunctions.AppendElement(
        SecurityWarningFunction{VariantIndex<0>{}, std::move(params)});
    return NS_OK;
  }

  nsresult LogBlockedCORSRequest(const nsAString& aMessage,
                                 const nsACString& aCategory,
                                 bool aIsWarning) override {
    LogBlockedCORSRequestParams params;
    params.mMessage = aMessage;
    params.mCategory = aCategory;
    params.mIsWarning = aIsWarning;
    mSecurityWarningFunctions.AppendElement(
        SecurityWarningFunction{VariantIndex<1>{}, std::move(params)});
    return NS_OK;
  }

  nsresult LogMimeTypeMismatch(const nsACString& aMessageName, bool aWarning,
                               const nsAString& aURL,
                               const nsAString& aContentType) override {
    LogMimeTypeMismatchParams params;
    params.mMessageName = aMessageName;
    params.mWarning = aWarning;
    params.mURL = aURL;
    params.mContentType = aContentType;
    mSecurityWarningFunctions.AppendElement(
        SecurityWarningFunction{VariantIndex<2>{}, std::move(params)});
    return NS_OK;
  }

  // The content process corresponding to this DocumentLoadListener, or nullptr
  // if connected to the parent process.
  dom::ContentParent* GetContentParent() const { return mContentParent; }

  // The process id of the content process that we are being called from
  // or 0 initiated from a parent process load.
  base::ProcessId OtherPid() const;

  [[nodiscard]] RefPtr<ChildEndpointPromise> AttachStreamFilter();

  // EarlyHints aren't supported on ParentProcessDocumentChannels yet, allow
  // EarlyHints to be cancelled from there (Bug 1819886)
  void CancelEarlyHintPreloads();

  // Gets the EarlyHint preloads for this document to pass them to the
  // ContentProcess. Registers them in the EarlyHintRegister
  void RegisterEarlyHintLinksAndGetConnectArgs(
      dom::ContentParentId aCpId, nsTArray<EarlyHintConnectArgs>& aOutLinks);

  // Serializes all data needed to setup the new replacement channel
  // in the content process into the RedirectToRealChannelArgs struct.
  void SerializeRedirectData(RedirectToRealChannelArgs& aArgs,
                             bool aIsCrossProcess, uint32_t aRedirectFlags,
                             uint32_t aLoadFlags,
                             nsTArray<EarlyHintConnectArgs>&& aEarlyHints,
                             uint32_t aEarlyHintLinkType) const;

  uint64_t GetLoadIdentifier() const { return mLoadIdentifier; }
  uint32_t GetLoadType() const { return mLoadStateLoadType; }
  bool IsDownload() const { return mIsDownload; }
  bool IsLoadingJSURI() const { return mIsLoadingJSURI; }
  nsDOMNavigationTiming* GetTiming() { return mTiming; }

  mozilla::dom::LoadingSessionHistoryInfo* GetLoadingSessionHistoryInfo() {
    return mLoadingSessionHistoryInfo.get();
  }

  bool IsDocumentLoad() const { return mIsDocumentLoad; }

  // Determine what process switching behavior a browser element should have.
  enum ProcessBehavior : uint8_t {
    // Gecko won't automatically change which process this frame, or it's
    // subframes, are loaded in.
    PROCESS_BEHAVIOR_DISABLED,

    // If `useRemoteTabs` is enabled, Gecko will change which process this frame
    // is loaded in automatically, without calling `performProcessSwitch`.
    // When `useRemoteSubframes` is enabled, subframes will change processes.
    PROCESS_BEHAVIOR_STANDARD,

    // Gecko won't automatically change which process this frame is loaded, but
    // when `useRemoteSubframes` is enabled, subframes will change processes.
    //
    // NOTE: This configuration is included only for backwards compatibility,
    // and will be removed, as it can easily lead to invalid behavior.
    PROCESS_BEHAVIOR_SUBFRAME_ONLY,
  };

 protected:
  virtual ~DocumentLoadListener();

 private:
  RefPtr<OpenPromise> OpenInParent(nsDocShellLoadState* aLoadState,
                                   bool aSupportsRedirectToRealChannel);

  friend class ParentProcessDocumentOpenInfo;

  // Will reject the promise to notify the DLL consumer that we are done.
  //
  // If `aContinueNavigating` is true, the navigation in the content process
  // will not be aborted, as navigation is logically continuing in the existing
  // browsing session (e.g. due to a process switch or entering the bfcache).
  void DisconnectListeners(nsresult aStatus, nsresult aLoadGroupStatus,
                           bool aContinueNavigating = false);

  // Initiates the switch from DocumentChannel to the real protocol-specific
  // channel, and ensures that RedirectToRealChannelFinished is called when
  // this is complete.
  void TriggerRedirectToRealChannel(
      dom::CanonicalBrowsingContext* aDestinationBrowsingContext,
      const Maybe<dom::ContentParent*>& aDestinationProcess,
      nsTArray<StreamFilterRequest> aStreamFilterRequests);

  // Called once the content-process side on setting up a replacement
  // channel is complete. May wait for the new parent channel to
  // finish, and then calls into FinishReplacementChannelSetup.
  void RedirectToRealChannelFinished(nsresult aRv);

  // Completes the replacement of the new channel.
  // This redirects the ParentChannelListener to forward any future
  // messages to the new channel, manually forwards any being held
  // by us, and resumes the underlying source channel.
  void FinishReplacementChannelSetup(nsresult aResult);

  // TODO: Make nsIRequestObserver MOZ_CAN_RUN_SCRIPT, then remove this. It's a
  // scriptable interface so it should be the right thing to do.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  nsresult DoOnStartRequest(nsIRequest*);

  // Called from `OnStartRequest` to make the decision about whether or not to
  // change process. This method will return `nullptr` if the current target
  // process is appropriate.
  // aWillSwitchToRemote is set to true if we initiate a process switch,
  // and that the new remote type will be something other than NOT_REMOTE
  MOZ_CAN_RUN_SCRIPT
  bool MaybeTriggerProcessSwitch(bool* aWillSwitchToRemote);

  // The commit part of DoOnStartRequest. MaybeBeginOutboundViewTransition can
  // delay it.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  nsresult ContinueDoOnStartRequestCommit(bool aSilentErrorLoad);

  // Lets the old document fire pageswap before the commit. If the navigation
  // can have a view transition, the old document also captures its state. This
  // then returns true and commits after the capture. Otherwise it returns
  // false, and the caller commits right away.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  bool MaybeBeginOutboundViewTransition();

  // Called when the process switch is going to happen, potentially
  // asynchronously, from `MaybeTriggerProcessSwitch`.
  //
  // aContext should be the target context for the navigation. This will either
  // be the loading BrowsingContext, the newly created BrowsingContext for an
  // object or embed element load, or a newly created tab for new tab load.
  //
  // If `aIsNewTab` is specified, the navigation in the original process will be
  // aborted immediately, rather than waiting for a process switch to happen and
  // the previous page to be unloaded or hidden.
  MOZ_CAN_RUN_SCRIPT
  void TriggerProcessSwitch(dom::CanonicalBrowsingContext* aContext,
                            const dom::NavigationIsolationOptions& aOptions,
                            bool aIsNewTab = false);

  // A helper for TriggerRedirectToRealChannel that abstracts over
  // the same-process and cross-process switch cases and returns
  // a single promise to wait on.
  using ParentEndpoint =
      mozilla::ipc::Endpoint<extensions::PStreamFilterParent>;
  RefPtr<PDocumentChannelParent::RedirectToRealChannelPromise>
  RedirectToRealChannel(uint32_t aRedirectFlags, uint32_t aLoadFlags,
                        const Maybe<dom::ContentParent*>& aDestinationProcess,
                        nsTArray<ParentEndpoint>&& aStreamFilterEndpoints);

  // A helper for RedirectToRealChannel that handles the case where we started
  // from a content process and are process switching into the parent process.
  RefPtr<PDocumentChannelParent::RedirectToRealChannelPromise>
  RedirectToParentProcess(uint32_t aRedirectFlags, uint32_t aLoadFlags);

  // Return the Browsing Context that is performing the load.
  // For document loads, the BC is the one that the (sub)doc
  // will load into. For <object>/<embed>, it's the embedder document's BC.
  dom::CanonicalBrowsingContext* GetLoadingBrowsingContext() const;

  // Return the Browsing Context that document is being loaded into. For
  // non-document loads, this will return nullptr.
  dom::CanonicalBrowsingContext* GetDocumentBrowsingContext() const;
  dom::CanonicalBrowsingContext* GetTopBrowsingContext() const;

  // Return the Window Context which which contains the element which the load
  // is being performed in. For toplevel loads, this will return `nullptr`.
  dom::WindowGlobalParent* GetParentWindowContext() const;

  // Looks for a speculation rules prefetch of aURI issued by the document
  // currently in the browsing context. Activates synchronously if a
  // completed prefetch matches. If one is still in flight, returns a promise
  // for AsyncOpenAfterPrefetchWait; otherwise null.
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#create-navigation-params-from-a-prefetch-record
  RefPtr<dom::PrefetchMatchPromise> TryActivateFromPrefetch(nsIURI* aURI);
  // Copies the prefetch's cookies into the destination partition and marks
  // the navigation timing as activated from a prefetch.
  void ActivateFromPrefetch(dom::PrefetchRecordParent* aRec);
  // Waits on aPromise (from TryActivateFromPrefetch), activates from the
  // resolved record if any, then calls AsyncOpen on mChannel. Does nothing if
  // the load was cancelled while waiting.
  void AsyncOpenAfterPrefetchWait(dom::PrefetchMatchPromise* aPromise,
                                  nsIStreamListener* aListener);

  void AddURIVisit(nsIChannel* aChannel, uint32_t aLoadFlags);
  bool HasCrossOriginOpenerPolicyMismatch() const;
  void ApplyPendingFunctions(nsIParentChannel* aChannel) const;

  void Disconnect(bool aContinueNavigating);

  void MaybeReportBlockedByURLClassifier(nsresult aStatus);

  // Returns true if a channel with aStatus will display
  // some sort of content (could be the actual channel data,
  // attempt a uri fixup and new load, or an error page).
  // Returns false if the docshell will ignore the load entirely.
  bool DocShellWillDisplayContent(nsresult aStatus);

  void FireStateChange(uint32_t aStateFlags, nsresult aStatus);

  // Returns true if this is a failed load, where we have successfully
  // created a fixed URI to attempt loading instead.
  // If successful, this calls DisconnectListeners to completely finish
  // the current load, and calls BrowsingContext::LoadURI to start the new one.
  MOZ_CAN_RUN_SCRIPT bool MaybeHandleLoadErrorWithURIFixup(nsresult aStatus);

  // This defines a variant that describes all the attribute setters (and their
  // parameters) from nsIParentChannel
  //
  // SetClassifierMatchedInfo(const nsACString& aList, const nsACString&
  // aProvider, const nsACString& aFullHash) = 0;
  // SetClassifierMatchedTrackingInfo(const nsACString& aLists, const
  // nsACString& aFullHashes) = 0; NotifyClassificationFlags(uint32_t
  // aClassificationFlags, bool aIsThirdParty) = 0;
  struct ClassifierMatchedInfoParams {
    nsCString mList;
    nsCString mProvider;
    nsCString mFullHash;
  };

  struct ClassifierMatchedTrackingInfoParams {
    nsCString mLists;
    nsCString mFullHashes;
  };

  struct ClassificationFlagsParams {
    uint32_t mClassificationFlags;
    bool mIsThirdParty;
  };

  using IParentChannelFunction =
      mozilla::Variant<ClassifierMatchedInfoParams,
                       ClassifierMatchedTrackingInfoParams,
                       ClassificationFlagsParams>;

  // Store a list of all the attribute setters that have been called on this
  // channel, so that we can repeat them on the real channel that we redirect
  // to.
  nsTArray<IParentChannelFunction> mIParentChannelFunctions;

  // This defines a variant this describes all the functions
  // from HttpChannelSecurityWarningReporter so that we can forward
  // them on to the real channel.

  struct ReportSecurityMessageParams {
    nsString mMessageTag;
    nsString mMessageCategory;
  };

  struct LogBlockedCORSRequestParams {
    nsString mMessage;
    nsCString mCategory;
    bool mIsWarning;
  };

  struct LogMimeTypeMismatchParams {
    nsCString mMessageName;
    bool mWarning = false;
    nsString mURL;
    nsString mContentType;
  };

  using SecurityWarningFunction =
      mozilla::Variant<ReportSecurityMessageParams, LogBlockedCORSRequestParams,
                       LogMimeTypeMismatchParams>;
  nsTArray<SecurityWarningFunction> mSecurityWarningFunctions;

  // TODO Backtrack this.
  // The set of nsIStreamListener functions that got called on this
  // listener, so that we can replay them onto the replacement channel's
  // listener. This should generally only be OnStartRequest, since we
  // Suspend() the channel at that point, but it can fail sometimes
  // so we have to support holding a list.
  nsTArray<StreamListenerFunction> mStreamListenerFunctions;

  nsCOMPtr<nsIChannel> mChannel;

  Maybe<uint64_t> mDocumentChannelId;

  // An instance of ParentChannelListener that we use as a listener
  // between mChannel (and any future redirected mChannels) and us.
  // This handles service worker interception, and retargetting
  // OnDataAvailable/OnStopRequest messages onto the listener that
  // replaces us.
  RefPtr<ParentChannelListener> mParentChannelListener;

  // Get the channel creation URI for constructing the channel in the content
  // process. See the function for more details.
  nsIURI* GetChannelCreationURI() const;

  // The original navigation timing information containing various timestamps
  // such as when the original load started.
  // This will be passed back to the new content process should a process
  // switch occurs.
  RefPtr<nsDOMNavigationTiming> mTiming;

  net::EarlyHintsService mEarlyHintsService;

  // An optional ObjectUpgradeHandler which can be used to upgrade an <object>
  // or <embed> element to contain a nsFrameLoader, allowing us to switch them
  // into a different process.
  //
  // A weak pointer is held in order to avoid reference cycles.
  WeakPtr<ObjectUpgradeHandler> mObjectUpgradeHandler;

  // Used to identify an internal redirect in redirect chain.
  // True when we have seen at least one non-interal redirect.
  bool mHaveVisibleRedirect = false;

  // Pending stream filter requests which should be attached when redirecting to
  // the real channel. Moved into `TriggerRedirectToRealChannel` when the
  // connection is ready.
  nsTArray<StreamFilterRequest> mStreamFilterRequests;

  nsString mSrcdocData;
  nsCOMPtr<nsIURI> mBaseURI;

  mozilla::UniquePtr<mozilla::dom::LoadingSessionHistoryInfo>
      mLoadingSessionHistoryInfo;

  RefPtr<dom::WindowGlobalParent> mParentWindowContext;

  // Flags from nsDocShellLoadState::LoadFlags/Type that we want to make
  // available to the new docshell if we switch processes.
  uint32_t mLoadStateExternalLoadFlags = 0;
  uint32_t mLoadStateInternalLoadFlags = 0;
  uint32_t mLoadStateLoadType = 0;

  // See nsDocShellLoadState::PageSwapFired.
  bool mLoadStatePageSwapFired = false;

  // Indicates if this load is a download.
  bool mIsDownload = false;

  // Indicates if we are loading a javascript URI.
  bool mIsLoadingJSURI = false;

  // Set to true if this load was moved into a container other than the one of
  // the browsing context it started in, because its URI is bound to that
  // container. Such a load must be retargeted into a new tab.
  bool mSwitchedContainer = false;

  // Corresponding redirect channel registrar Id for the final channel that
  // we want to use when redirecting the child, or doing a process switch.
  // 0 means redirection is not started.
  uint64_t mRedirectChannelId = 0;
  // Set to true once we initiate the redirect to a real channel (either
  // via a process switch or a same-process redirect, and Suspend the
  // underlying channel.
  bool mInitiatedRedirectToRealChannel = false;
  // The value of GetApplyConversion on mChannel when OnStartRequest
  // was called. We override it to false to prevent a conversion
  // helper from being installed, but we need to restore the value
  // later.
  bool mOldApplyConversion = false;
  // Set to true if any previous channel that we redirected away
  // from had a COOP mismatch.
  bool mHasCrossOriginOpenerPolicyMismatch = false;
  // Set to true if we've received OnStopRequest, and shouldn't
  // setup a reference from the ParentChannelListener to the replacement
  // channel.
  bool mIsFinished = false;

  // The id of the currently pending load which is
  // passed to the childChannel in order to identify it in the new process.
  uint64_t mLoadIdentifier = 0;

  Maybe<nsCString> mOriginalUriString;

  // Parent-initiated loads do not support redirects to real channels.
  bool mSupportsRedirectToRealChannel = true;

  // Handle which will be passed to the content process. This will be passed to
  // the WindowGlobalParent via. the content process to allow correlating the
  // channel to the final document.
  RefPtr<dom::ParentProcessChannelHandle> mParentProcessChannelHandle;

  Maybe<dom::RemoteType> mRemoteTypeOverride;

  // The ContentParent which this channel is currently connected to, or nullptr
  // if connected to the parent process.
  RefPtr<dom::ContentParent> mContentParent;

  void RejectOpenPromise(nsresult aStatus, nsresult aLoadGroupStatus,
                         bool aContinueNavigating, StaticString aLocation) {
    // It is possible for mOpenPromise to not be set if AsyncOpen failed and
    // the DocumentChannel got canceled.
    if (!mOpenPromiseResolved && mOpenPromise) {
      mOpenPromise->Reject(OpenPromiseFailedType({aStatus, aLoadGroupStatus,
                                                  aContinueNavigating}),
                           aLocation);
      mOpenPromiseResolved = true;
    }
  }
  RefPtr<OpenPromise::Private> mOpenPromise;
  bool mOpenPromiseResolved = false;

  const bool mIsDocumentLoad;

  RefPtr<HTTPSFirstDowngradeData> mHTTPSFirstDowngradeData;
};

inline nsISupports* ToSupports(DocumentLoadListener* aObj) {
  return static_cast<nsIInterfaceRequestor*>(aObj);
}

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_DocumentChannelParent_h
