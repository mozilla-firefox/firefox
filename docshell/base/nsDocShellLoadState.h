/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDocShellLoadState_h_
#define nsDocShellLoadState_h_

#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/NavigationBinding.h"
#include "mozilla/dom/SessionHistoryEntry.h"
#include "mozilla/dom/UserNavigationInvolvement.h"
#include "mozilla/dom/LoadURIOptionsBinding.h"

#include "nsIClassifiedChannel.h"
#include "nsILoadInfo.h"

// Helper Classes
#include "mozilla/Maybe.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsDocShellLoadTypes.h"
#include "nsTArrayForwardDeclare.h"

class nsIInputStream;
class nsIURI;
class nsIDocShell;
class nsIChannel;
class nsIReferrerInfo;
struct HTTPSFirstDowngradeData;
namespace mozilla {
class OriginAttributes;
namespace net {
class DocumentLoadListener;
}
namespace dom {
class FormData;
class DocShellLoadStateInit;
struct NavigationAPIMethodTracker;
class SessionHistoryEntry;
}  // namespace dom
}  // namespace mozilla

namespace mozilla::dom {

bool ContentTriggeredURILoadIsAllowed(nsIURI* aURI,
                                      const nsACString& aEffectiveRemoteType);

}

/**
 * nsDocShellLoadState contains setup information used in a nsIDocShell::loadURI
 * call.
 */
class nsDocShellLoadState final {
  using BrowsingContext = mozilla::dom::BrowsingContext;
  template <typename T>
  using MaybeDiscarded = mozilla::dom::MaybeDiscarded<T>;
  using SessionHistoryEntry = mozilla::dom::SessionHistoryEntry;

 public:
  NS_INLINE_DECL_REFCOUNTING(nsDocShellLoadState);

  explicit nsDocShellLoadState(nsIURI* aURI);
  explicit nsDocShellLoadState(
      const mozilla::dom::DocShellLoadStateInit& aLoadState,
      mozilla::ipc::IProtocol* aActor, bool* aReadSuccess);
  explicit nsDocShellLoadState(const nsDocShellLoadState& aOther);
  nsDocShellLoadState(nsIURI* aURI, uint64_t aLoadIdentifier);

  static nsresult CreateFromPendingChannel(nsIChannel* aPendingChannel,
                                           uint64_t aLoadIdentifier,
                                           uint64_t aRegistarId,
                                           nsDocShellLoadState** aResult);

  static nsresult CreateFromLoadURIOptions(
      BrowsingContext* aBrowsingContext, const nsAString& aURI,
      const mozilla::dom::LoadURIOptions& aLoadURIOptions,
      nsDocShellLoadState** aResult);
  static nsresult CreateFromLoadURIOptions(
      BrowsingContext* aBrowsingContext, nsIURI* aURI,
      const mozilla::dom::LoadURIOptions& aLoadURIOptions,
      nsDocShellLoadState** aResult);

  // Getters and Setters

  nsIReferrerInfo* GetReferrerInfo() const;

  void SetReferrerInfo(nsIReferrerInfo* aReferrerInfo);

  nsIURI* URI() const;

  void SetURI(nsIURI* aURI);

  nsIURI* OriginalURI() const;

  void SetOriginalURI(nsIURI* aOriginalURI);

  nsIURI* ResultPrincipalURI() const;

  void SetResultPrincipalURI(nsIURI* aResultPrincipalURI);

  bool ResultPrincipalURIIsSome() const;

  void SetResultPrincipalURIIsSome(bool aIsSome);

  bool KeepResultPrincipalURIIfSet() const;

  void SetKeepResultPrincipalURIIfSet(bool aKeep);

  nsIPrincipal* PrincipalToInherit() const;

  void SetPrincipalToInherit(nsIPrincipal* aPrincipalToInherit);

  nsIPrincipal* PartitionedPrincipalToInherit() const;

  void SetPartitionedPrincipalToInherit(
      nsIPrincipal* aPartitionedPrincipalToInherit);

  bool LoadReplace() const;

  void SetLoadReplace(bool aLoadReplace);

  nsIPrincipal* TriggeringPrincipal() const;

  void SetTriggeringPrincipal(nsIPrincipal* aTriggeringPrincipal);

  uint32_t TriggeringSandboxFlags() const;

  void SetTriggeringSandboxFlags(uint32_t aTriggeringSandboxFlags);

  uint64_t TriggeringWindowId() const;

  void SetTriggeringWindowId(uint64_t aTriggeringWindowId);

  bool TriggeringStorageAccess() const;

  void SetTriggeringStorageAccess(bool aTriggeringStorageAccess);

  mozilla::net::ClassificationFlags TriggeringClassificationFlags() const;
  void SetTriggeringClassificationFlags(
      mozilla::net::ClassificationFlags aFlags);

  nsIPolicyContainer* PolicyContainer() const;

  void SetPolicyContainer(nsIPolicyContainer* aPolicyContainer);

  bool InheritPrincipal() const;

  void SetInheritPrincipal(bool aInheritPrincipal);

  bool PrincipalIsExplicit() const;

  void SetPrincipalIsExplicit(bool aPrincipalIsExplicit);

  // If true, "beforeunload" event listeners were notified by the creater of the
  // LoadState and given the chance to abort the navigation, and should not be
  // notified again.
  bool NotifiedBeforeUnloadListeners() const;

  void SetNotifiedBeforeUnloadListeners(bool aNotifiedBeforeUnloadListeners);

  bool ForceAllowDataURI() const;

  void SetForceAllowDataURI(bool aForceAllowDataURI);

  bool IsExemptFromHTTPSFirstMode() const;

  void SetIsExemptFromHTTPSFirstMode(bool aIsExemptFromHTTPSFirstMode);

  RefPtr<HTTPSFirstDowngradeData> GetHttpsFirstDowngradeData() const;

  void SetHttpsFirstDowngradeData(
      RefPtr<HTTPSFirstDowngradeData> const& aHttpsFirstTelemetryData);

  bool OriginalFrameSrc() const;

  void SetOriginalFrameSrc(bool aOriginalFrameSrc);

  bool ShouldCheckForRecursion() const;

  void SetShouldCheckForRecursion(bool aShouldCheckForRecursion);

  bool IsFormSubmission() const;

  void SetIsFormSubmission(bool aIsFormSubmission);

  bool NeedsCompletelyLoadedDocument() const;

  void SetNeedsCompletelyLoadedDocument(bool aNeedsCompletelyLoadedDocument);

  mozilla::Maybe<mozilla::dom::NavigationHistoryBehavior> HistoryBehavior()
      const;

  void SetHistoryBehavior(
      mozilla::dom::NavigationHistoryBehavior aHistoryBehavior);

  void ResetHistoryBehavior();

  uint32_t LoadType() const;

  void SetLoadType(uint32_t aLoadType);

  mozilla::dom::UserNavigationInvolvement UserNavigationInvolvement() const;

  void SetUserNavigationInvolvement(
      mozilla::dom::UserNavigationInvolvement aUserNavigationInvolvement);

  SessionHistoryEntry* SHEntry() const;

  void SetSHEntry(SessionHistoryEntry* aSHEntry);

  void SetPreviousEntryForActivation(nsISHEntry* aSHEntry);
  void SetPreviousEntryForActivation(
      const mozilla::Maybe<mozilla::dom::PreviousSessionHistoryInfo>& aInfo);

  const mozilla::dom::LoadingSessionHistoryInfo* GetLoadingSessionHistoryInfo()
      const;

  // Copies aLoadingInfo and stores the copy in this nsDocShellLoadState.
  void SetLoadingSessionHistoryInfo(
      const mozilla::dom::LoadingSessionHistoryInfo& aLoadingInfo);

  // Stores aLoadingInfo in this nsDocShellLoadState.
  void SetLoadingSessionHistoryInfo(
      mozilla::UniquePtr<mozilla::dom::LoadingSessionHistoryInfo> aLoadingInfo);

  bool LoadIsFromSessionHistory() const;

  const nsString& Target() const;

  void SetTarget(const nsAString& aTarget);

  nsIInputStream* PostDataStream() const;

  void SetPostDataStream(nsIInputStream* aStream);

  nsIInputStream* HeadersStream() const;

  void SetHeadersStream(nsIInputStream* aHeadersStream);

  bool IsSrcdocLoad() const;

  const nsString& SrcdocData() const;

  void SetSrcdocData(const nsAString& aSrcdocData);

  const MaybeDiscarded<BrowsingContext>& SourceBrowsingContext() const {
    return mSourceBrowsingContext;
  }

  void SetSourceBrowsingContext(BrowsingContext*);

  void SetAllowFocusMove(bool aAllow) { mAllowFocusMove = aAllow; }

  bool AllowFocusMove() const { return mAllowFocusMove; }

  const MaybeDiscarded<BrowsingContext>& TargetBrowsingContext() const {
    return mTargetBrowsingContext;
  }

  void SetTargetBrowsingContext(BrowsingContext* aTargetBrowsingContext);

  nsIURI* BaseURI() const;

  void SetBaseURI(nsIURI* aBaseURI);

  // Helper function allowing convenient work with mozilla::Maybe in C++, hiding
  // resultPrincipalURI and resultPrincipalURIIsSome attributes from the
  // consumer.
  void GetMaybeResultPrincipalURI(
      mozilla::Maybe<nsCOMPtr<nsIURI>>& aRPURI) const;

  void SetMaybeResultPrincipalURI(
      mozilla::Maybe<nsCOMPtr<nsIURI>> const& aRPURI);

  uint32_t LoadFlags() const;

  void SetLoadFlags(uint32_t aFlags);

  void SetLoadFlag(uint32_t aFlag);

  void UnsetLoadFlag(uint32_t aFlag);

  bool HasLoadFlags(uint32_t aFlag);

  uint32_t InternalLoadFlags() const;

  void SetInternalLoadFlags(uint32_t aFlags);

  void SetInternalLoadFlag(uint32_t aFlag);

  void UnsetInternalLoadFlag(uint32_t aFlag);

  bool HasInternalLoadFlags(uint32_t aFlag);

  bool FirstParty() const;

  void SetFirstParty(bool aFirstParty);

  bool HasValidUserGestureActivation() const;

  void SetHasValidUserGestureActivation(bool HasValidUserGestureActivation);

  void SetTextDirectiveUserActivation(bool aTextDirectiveUserActivation);

  bool GetTextDirectiveUserActivation();

  const nsCString& TypeHint() const;

  void SetTypeHint(const nsCString& aTypeHint);

  const nsString& FileName() const;

  void SetFileName(const nsAString& aFileName);

  nsIURI* GetUnstrippedURI() const;

  void SetUnstrippedURI(nsIURI* aUnstrippedURI);

  // Give the type of DocShell we're loading into (chrome/content/etc) and
  // origin attributes for the URI we're loading, figure out if we should
  // inherit our principal from the document the load was requested from, or
  // else if the principal should be set up later in the process (after loads).
  // See comments in function for more info on principal selection algorithm
  nsresult SetupInheritingPrincipal(
      mozilla::dom::BrowsingContext::Type aType,
      const mozilla::OriginAttributes& aOriginAttributes);

  // If no triggering principal exists at the moment, create one using referrer
  // information and origin attributes.
  nsresult SetupTriggeringPrincipal(
      const mozilla::OriginAttributes& aOriginAttributes);

  void SetIsFromProcessingFrameAttributes() {
    mIsFromProcessingFrameAttributes = true;
  }
  bool GetIsFromProcessingFrameAttributes() const {
    return mIsFromProcessingFrameAttributes;
  }

  nsIChannel* GetPendingRedirectedChannel() {
    return mPendingRedirectedChannel;
  }

  uint64_t GetPendingRedirectChannelRegistrarId() const {
    return mChannelRegistrarId;
  }

  void SetOriginalURIString(const nsCString& aOriginalURI) {
    mOriginalURIString.emplace(aOriginalURI);
  }
  const mozilla::Maybe<nsCString>& GetOriginalURIString() const {
    return mOriginalURIString;
  }

  void SetCancelContentJSEpoch(int32_t aCancelEpoch) {
    mCancelContentJSEpoch.emplace(aCancelEpoch);
  }
  const mozilla::Maybe<int32_t>& GetCancelContentJSEpoch() const {
    return mCancelContentJSEpoch;
  }

  uint64_t GetLoadIdentifier() const { return mLoadIdentifier; }

  void SetSpeculativeListener(mozilla::net::DocumentLoadListener* aListener);
  already_AddRefed<mozilla::net::DocumentLoadListener>
  TakeSpeculativeListener();

  void SetIsMetaRefresh(bool aMetaRefresh) { mIsMetaRefresh = aMetaRefresh; }

  bool IsMetaRefresh() const { return mIsMetaRefresh; }

  const mozilla::Maybe<nsCString>& GetRemoteTypeOverride() const {
    return mRemoteTypeOverride;
  }

  void SetRemoteTypeOverride(const nsCString& aRemoteTypeOverride);

  void SetSchemelessInput(nsILoadInfo::SchemelessInputType aSchemelessInput) {
    mSchemelessInput = aSchemelessInput;
  }

  nsILoadInfo::SchemelessInputType GetSchemelessInput() {
    return mSchemelessInput;
  }

  void SetForceMediaDocument(
      mozilla::dom::ForceMediaDocument aForceMediaDocument) {
    mForceMediaDocument = aForceMediaDocument;
  }

  mozilla::dom::ForceMediaDocument GetForceMediaDocument() const {
    return mForceMediaDocument;
  }

  void SetHttpsUpgradeTelemetry(
      nsILoadInfo::HTTPSUpgradeTelemetryType aHttpsUpgradeTelemetry) {
    mHttpsUpgradeTelemetry = aHttpsUpgradeTelemetry;
  }

  nsILoadInfo::HTTPSUpgradeTelemetryType GetHttpsUpgradeTelemetry() {
    return mHttpsUpgradeTelemetry;
  }

  // Determine the remote type of the process which should be considered
  // responsible for this load for the purposes of security checks.
  //
  // This will generally be the process which created the nsDocShellLoadState
  // originally, however non-errorpage history loads are always considered to be
  // triggered by the parent process, as we can validate them against the
  // history entry.
  const nsCString& GetEffectiveTriggeringRemoteType() const;

  void SetTriggeringRemoteType(const nsACString& aTriggeringRemoteType);

  // Diagnostic assert if this is a system-principal triggered load, and it is
  // trivial to determine that the effective triggering remote type would not be
  // allowed to perform this load.
  //
  // This is called early during the load to crash as close to the cause as
  // possible. See bug 1838686 for details.
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  void AssertProcessCouldTriggerLoadIfSystem();
#else
  void AssertProcessCouldTriggerLoadIfSystem() {}
#endif

  // When loading a document through nsDocShell::LoadURI(), a special set of
  // flags needs to be set based on other values in nsDocShellLoadState. This
  // function calculates those flags, before the LoadState is passed to
  // nsDocShell::InternalLoad.
  void CalculateLoadURIFlags();

  // Compute the load flags to be used by creating channel.  aUriModified and
  // aIsEmbeddingBlockedError are expected to be Nothing when called from parent
  // process.
  nsLoadFlags CalculateChannelLoadFlags(
      mozilla::dom::BrowsingContext* aBrowsingContext, bool aUriModified,
      mozilla::Maybe<bool> aIsEmbeddingBlockedError);

  mozilla::dom::DocShellLoadStateInit Serialize(
      mozilla::ipc::IProtocol* aActor);

  void SetLoadIsFromSessionHistory(int32_t aOffset, bool aLoadingCurrentEntry);
  void ClearLoadIsFromSessionHistory();

  void MaybeStripTrackerQueryStrings(mozilla::dom::BrowsingContext* aContext);

  // This is used as the parameter for https://html.spec.whatwg.org/#navigate
  void SetSourceElement(mozilla::dom::Element* aElement);
  already_AddRefed<mozilla::dom::Element> GetSourceElement() const;
  bool HasSourceElement() const {
    return mSourceElement && mSourceElement->IsAlive();
  }

  // This is used as the parameter for https://html.spec.whatwg.org/#navigate
  nsIStructuredCloneContainer* GetNavigationAPIState() const;
  void SetNavigationAPIState(nsIStructuredCloneContainer* aNavigationAPIState);

  // This is used to pass the navigation API method tracker through the
  // navigation pipeline for navigate().
  // See https://html.spec.whatwg.org/#navigation-api-method-tracker
  mozilla::dom::NavigationAPIMethodTracker* GetNavigationAPIMethodTracker()
      const;
  void SetNavigationAPIMethodTracker(
      mozilla::dom::NavigationAPIMethodTracker* aTracker);

  // This is used as the parameter for https://html.spec.whatwg.org/#navigate
  mozilla::dom::NavigationType GetNavigationType() const;

  // This is used as the parameter for https://html.spec.whatwg.org/#navigate
  // It should only ever be set if the method is POST.
  mozilla::dom::FormData* GetFormDataEntryList();
  void SetFormDataEntryList(mozilla::dom::FormData* aFormDataEntryList);

  // This is used as the getter/setter for the app link intent launch type
  // for the load.
  uint32_t GetAppLinkLaunchType() const;
  void SetAppLinkLaunchType(uint32_t aAppLinkLaunchType);

  // This is used as the getter/setter for the captive portal tab flag.
  bool GetIsCaptivePortalTab() const;
  void SetIsCaptivePortalTab(bool aIsCaptivePortalTab);

  void ProhibitInitialAboutBlankHandling() {
    mIsInitialAboutBlankHandlingProhibited = true;
  }
  bool IsInitialAboutBlankHandlingProhibited() {
    return mIsInitialAboutBlankHandlingProhibited;
  }

  void SetIsResumingInterceptedNavigation(
      bool aIsResumingInterceptedNavigation) {
    mIsResumingInterceptedNavigation = aIsResumingInterceptedNavigation;
  }

  bool IsResumingInterceptedNavigation() const {
    return mIsResumingInterceptedNavigation;
  }

  bool HasComputedNamedTargetBrowsingContext() const {
    return mHasComputedNamedTargetBrowsingContext;
  }
  void SetHasComputedNamedTargetBrowsingContext(bool aValue) {
    mHasComputedNamedTargetBrowsingContext = aValue;
  }

 protected:
  // Destructor can't be defaulted or inlined, as header doesn't have all type
  // includes it needs to do so.
  ~nsDocShellLoadState();

  // Given the original `nsDocShellLoadState` which was sent to a content
  // process, validate that they corespond to the same load.
  // Returns a static (telemetry-safe) string naming what did not match, or
  // nullptr if it succeeds.
  const char* ValidateWithOriginalState(nsDocShellLoadState* aOriginalState);

  static nsresult CreateFromLoadURIOptions(
      BrowsingContext* aBrowsingContext, nsIURI* aURI,
      const mozilla::dom::LoadURIOptions& aLoadURIOptions,
      uint32_t aLoadFlagsOverride, nsIInputStream* aPostDataOverride,
      nsDocShellLoadState** aResult);

  // This is the referrer for the load.
  nsCOMPtr<nsIReferrerInfo> mReferrerInfo;

  // The URI we are navigating to. Will not be null once set.
  nsCOMPtr<nsIURI> mURI;

  // The URI to set as the originalURI on the channel that does the load. If
  // null, aURI will be set as the originalURI.
  nsCOMPtr<nsIURI> mOriginalURI;

  // The URI to be set to loadInfo.resultPrincipalURI
  // - When Nothing, there will be no change
  // - When Some, the principal URI will overwrite even
  //   with a null value.
  //
  // Valid only if mResultPrincipalURIIsSome is true (has the same meaning as
  // isSome() on mozilla::Maybe.)
  nsCOMPtr<nsIURI> mResultPrincipalURI;
  bool mResultPrincipalURIIsSome;

  // The principal of the load, that is, the entity responsible for causing the
  // load to occur. In most cases the referrer and the triggeringPrincipal's URI
  // will be identical.
  //
  // Please note that this is the principal that is used for security checks. If
  // the argument aURI is provided by the web, then please do not pass a
  // SystemPrincipal as the triggeringPrincipal.
  nsCOMPtr<nsIPrincipal> mTriggeringPrincipal;

  // The SandboxFlags of the load, that are, the SandboxFlags of the entity
  // responsible for causing the load to occur. Most likely this are the
  // SandboxFlags of the document that started the load.
  uint32_t mTriggeringSandboxFlags;

  // The window ID and current "has storage access" value of the entity
  // triggering the load. This allows the identification of self-initiated
  // same-origin navigations that should propogate unpartitioned storage access.
  uint64_t mTriggeringWindowId;
  bool mTriggeringStorageAccess;

  // The classification flags of the context responsible for causing
  // the load to start.
  mozilla::net::ClassificationFlags mTriggeringClassificationFlags;

  // The policyContainer of the load, that is, the policyContainer of the entity
  // responsible for causing the load to occur. Most likely this is the
  // policyContainer of the document that started the load. In case the entity
  // starting the load did not use a policyContainer, then mpPolicyContainer can
  // be null. Please note that this is also the policyContainer that will be
  // applied to the load in case the load encounters a server side redirect.
  nsCOMPtr<nsIPolicyContainer> mPolicyContainer;

  // If a refresh is caused by http-equiv="refresh" we want to set
  // aResultPrincipalURI, but we do not want to overwrite the channel's
  // ResultPrincipalURI, if it has already been set on the channel by a protocol
  // handler.
  bool mKeepResultPrincipalURIIfSet;

  // If set LOAD_REPLACE flag will be set on the channel. If aOriginalURI is
  // null, this argument is ignored.
  bool mLoadReplace;

  // If this attribute is true and no triggeringPrincipal is specified,
  // copy the principal from the referring document.
  bool mInheritPrincipal;

  // If this attribute is true only ever use the principal specified
  // by the triggeringPrincipal and inheritPrincipal attributes.
  // If there are security reasons for why this is unsafe, such
  // as trying to use a systemprincipal as the triggeringPrincipal
  // for a content docshell the load fails.
  bool mPrincipalIsExplicit;

  // If this attribute is true, any potential unload listeners have been
  // notified if applicable.
  bool mNotifiedBeforeUnloadListeners;

  // Principal we're inheriting. If null, this means the principal should be
  // inherited from the current document. If set to NullPrincipal, the channel
  // will fill in principal information later in the load. See internal comments
  // of SetupInheritingPrincipal for more info.
  //
  // When passed to InternalLoad, If this argument is null then
  // principalToInherit is computed differently. See nsDocShell::InternalLoad
  // for more comments.

  nsCOMPtr<nsIPrincipal> mPrincipalToInherit;

  nsCOMPtr<nsIPrincipal> mPartitionedPrincipalToInherit;

  // If this attribute is true, then a top-level navigation
  // to a data URI will be allowed.
  bool mForceAllowDataURI;

  // If this attribute is true, then the top-level navigaion
  // will be exempt from HTTPS-Only-Mode upgrades.
  bool mIsExemptFromHTTPSFirstMode;

  // If set, this load is a HTTPS-First downgrade, and the downgrade data will
  // be submitted to telemetry later if the load succeeds.
  RefPtr<HTTPSFirstDowngradeData> mHttpsFirstDowngradeData;

  // If this attribute is true, this load corresponds to a frame
  // element loading its original src (or srcdoc) attribute.
  bool mOriginalFrameSrc;

  // If this attribute is true, this load corresponds to a frame, object, or
  // embed element that needs a recursion check when loading it's src (or data).
  // Unlike mOriginalFrameSrc, this attribute will always be set regardless
  // whether we've loaded the src already.
  bool mShouldCheckForRecursion;

  // If this attribute is true, then the load was initiated by a
  // form submission.
  bool mIsFormSubmission;

  // If this attribute is true, we need to check if the current document is
  // completely loaded to determine if we should perform a push or replace load.
  bool mNeedsCompletelyLoadedDocument;

  // If this attribute is `Auto`, we should determine if this should be a push
  // or replace load when actually loading.
  mozilla::Maybe<mozilla::dom::NavigationHistoryBehavior> mHistoryBehavior;

  // Contains a load type as specified by the nsDocShellLoadTypes::load*
  // constants
  uint32_t mLoadType;

  // https://html.spec.whatwg.org/#user-navigation-involvement
  mozilla::dom::UserNavigationInvolvement mUserNavigationInvolvement =
      mozilla::dom::UserNavigationInvolvement::None;

  // Active Session History entry (if loading from SH)
  RefPtr<SessionHistoryEntry> mSHEntry;

  // Loading session history info for the load
  mozilla::UniquePtr<mozilla::dom::LoadingSessionHistoryInfo>
      mLoadingSessionHistoryInfo;

  // Target for load, like _content, _blank etc.
  nsString mTarget;

  // When set, this is the Target Browsing Context for the navigation
  // after retargeting.
  MaybeDiscarded<BrowsingContext> mTargetBrowsingContext;

  // Post data stream (if POSTing)
  nsCOMPtr<nsIInputStream> mPostDataStream;

  // Additional Headers
  nsCOMPtr<nsIInputStream> mHeadersStream;

  // When set, the load will be interpreted as a srcdoc load, where contents of
  // this string will be loaded instead of the URI. Setting srcdocData sets
  // isSrcdocLoad to true
  nsString mSrcdocData;

  // When set, this is the Source Browsing Context for the navigation.
  MaybeDiscarded<BrowsingContext> mSourceBrowsingContext;

  // Used for srcdoc loads to give view-source knowledge of the load's base URI
  // as this information isn't embedded in the load's URI.
  nsCOMPtr<nsIURI> mBaseURI;

  // Set of Load Flags, taken from nsDocShellLoadTypes.h and nsIWebNavigation
  uint32_t mLoadFlags;

  // Set of internal load flags
  uint32_t mInternalLoadFlags;

  // Is this a First Party Load?
  bool mFirstParty;

  // Is this load triggered by a user gesture?
  bool mHasValidUserGestureActivation;

  // True if a text directive can be scrolled to. This is true either if the
  // load is triggered by a user, or the document has an unconsumed activation
  // (eg. client redirect).
  bool mTextDirectiveUserActivation = false;

  // Whether this load can steal the focus from the source browsing context.
  bool mAllowFocusMove;

  // A hint as to the content-type of the resulting data. If no hint, IsVoid()
  // should return true.
  nsCString mTypeHint;

  // Non-void when the link should be downloaded as the given filename.
  // mFileName being non-void but empty means that no filename hint was
  // specified, but link should still trigger a download. If not a download,
  // mFileName.IsVoid() should return true.
  nsString mFileName;

  // This will be true if this load is triggered by attribute changes.
  // See nsILoadInfo.isFromProcessingFrameAttributes
  bool mIsFromProcessingFrameAttributes;

  // Whether the target name has been looked up yet
  bool mHasComputedNamedTargetBrowsingContext = false;

  // If set, a pending cross-process redirected channel should be used to
  // perform the load. The channel will be stored in this value.
  nsCOMPtr<nsIChannel> mPendingRedirectedChannel;

  // An optional string representation of mURI, before any
  // fixups were applied, so that we can send it to a search
  // engine service if needed.
  mozilla::Maybe<nsCString> mOriginalURIString;

  // An optional value to pass to nsIDocShell::setCancelJSEpoch
  // when initiating the load.
  mozilla::Maybe<int32_t> mCancelContentJSEpoch;

  // If mPendingRedirectChannel is set, then this is the identifier
  // that the parent-process equivalent channel has been registered
  // with using RedirectChannelRegistrar.
  uint64_t mChannelRegistrarId;

  // An identifier to make it possible to examine if two loads are
  // equal, and which browsing context they belong to (see
  // BrowsingContext::{Get, Set}CurrentLoadIdentifier)
  const uint64_t mLoadIdentifier;

  // Optional DocumentLoadListener reference. This is only set in the parent
  // process, and indicates that the channel has been pre-initialized in the
  // parent process.
  RefPtr<mozilla::net::DocumentLoadListener> mSpeculativeListener;

  // Optional value available in content to indicate the channel has been
  // pre-initialized in the parent process.
  bool mHasSpeculativeListener = false;

  // True if the load was triggered by a meta refresh.
  bool mIsMetaRefresh;

  // True if the nsDocShellLoadState was received over IPC.
  bool mWasCreatedRemotely = false;

  // The original URI before query stripping happened. If it's present, it shows
  // the query stripping happened. Otherwise, it will be a nullptr.
  nsCOMPtr<nsIURI> mUnstrippedURI;

  // If set, the remote type which the load should be completed within.
  mozilla::Maybe<nsCString> mRemoteTypeOverride;

  // Remote type of the process which originally requested the load.
  nsCString mTriggeringRemoteType;

  // if the address had an intentional protocol
  nsILoadInfo::SchemelessInputType mSchemelessInput =
      nsILoadInfo::SchemelessInputTypeUnset;

  // If not None, force the load to result in a specific media document kind.
  mozilla::dom::ForceMediaDocument mForceMediaDocument =
      mozilla::dom::ForceMediaDocument::None;

  // Solely for the use of collecting Telemetry for HTTPS upgrades.
  nsILoadInfo::HTTPSUpgradeTelemetryType mHttpsUpgradeTelemetry =
      nsILoadInfo::NOT_INITIALIZED;

  nsWeakPtr mSourceElement;

  RefPtr<nsStructuredCloneContainer> mNavigationAPIState;

  RefPtr<mozilla::dom::NavigationAPIMethodTracker> mNavigationAPIMethodTracker;

  RefPtr<mozilla::dom::FormData> mFormDataEntryList;

  // App link intent launch type: 0 = unknown, 1 = cold, 2 = warm, 3 = hot.
  uint32_t mAppLinkLaunchType = 0;

  // Whether this is a captive portal tab.
  bool mIsCaptivePortalTab = false;

  // When this is the initial load and it is loading about:blank, force it
  // to take the regular load path. It will replace the previous document
  // and not load synchronous.
  bool mIsInitialAboutBlankHandlingProhibited;

  // True when this LoadURI call is synchronously resuming a traversal
  // navigation that was paused while the Navigation API's NavigateEvent was
  // dispatched and intercepted. Set by Navigation::CommitNavigateEvent after
  // the event commits, and consumed on the docshell side to keep the existing
  // ongoing navigation in place (rather than resetting it) and to forward the
  // flag through LoadHistoryEntry.
  bool mIsResumingInterceptedNavigation = false;
};

#endif /* nsDocShellLoadState_h_ */
