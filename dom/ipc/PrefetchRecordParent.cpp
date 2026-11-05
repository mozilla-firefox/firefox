/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/PrefetchRecordParent.h"

#include "mozilla/DebugOnly.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/PrefetchLog.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/glean/DomMetrics.h"
#include "mozilla/net/NoVarySearchUtils.h"
#include "nsContentPolicyUtils.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsICachingChannel.h"
#include "nsIHttpChannel.h"
#include "nsILoadInfo.h"
#include "nsMixedContentBlocker.h"
#include "nsNetUtil.h"
#include "nsStreamUtils.h"

namespace mozilla::dom {

// Spec: https://wicg.github.io/nav-speculation/prefetch.html
LazyLogModule gSpeculationRulesLog("SpeculationRules");

NS_IMPL_ADDREF(PrefetchRecordParent)
NS_IMPL_RELEASE(PrefetchRecordParent)
NS_INTERFACE_MAP_BEGIN(PrefetchRecordParent)
  NS_INTERFACE_MAP_ENTRY(nsIStreamListener)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
  NS_INTERFACE_MAP_ENTRY(nsIChannelEventSink)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIStreamListener)
NS_INTERFACE_MAP_END

// Implements "start a referrer-initiated navigational prefetch".
// Spec:
// https://wicg.github.io/nav-speculation/prefetch.html#start-a-referrer-initiated-navigational-prefetch
void PrefetchRecordParent::Init(WindowGlobalParent* aWGP,
                                const SpeculativePrefetchArgs& aArgs) {
  // aWGP is passed explicitly by AllocPPrefetchRecordParent because Manager()
  // is not yet set when Init is called during actor allocation.
  if (!aWGP) {
    LOG_SPECRULES_WARN(
        ("PrefetchRecordParent::Init: this=%p wgp is null", this));
    mState = PrefetchState::Canceled;
    return;
  }
  nsCOMPtr<nsIPrincipal> docPrincipal = aWGP->DocumentPrincipal();
  if (!docPrincipal) {
    LOG_SPECRULES_WARN(
        ("PrefetchRecordParent::Init: this=%p docPrincipal is null", this));
    mState = PrefetchState::Canceled;
    return;
  }
  // Use the browsing context's origin attributes (not the document principal's)
  // so that the prefetch channel uses the same dFPI partition key as the
  // navigation channel will at activation time.
  OriginAttributes sourceAttrs;
  aWGP->BrowsingContext()->GetOriginAttributes(sourceAttrs);
  mSourcePartitionKey = sourceAttrs.mPartitionKey;

  mURL = aArgs.uri();
  if (!mURL) {
    LOG_SPECRULES_WARN(("PrefetchRecordParent::Init: this=%p null URL", this));
    mState = PrefetchState::Canceled;
    return;
  }
  mTags = aArgs.tags().Clone();
  mReferrerInfo = aArgs.referrerInfo();
  if (!mReferrerInfo) {
    // A null referrerInfo carries no referrer policy; use an empty
    // ReferrerInfo (ReferrerPolicy::_empty) so mReferrerInfo is never
    // null for the rest of the actor's lifetime.
    mReferrerInfo = new ReferrerInfo();
  }
  mNoVarySearchHint = aArgs.noVarySearchHint();
  mAnonPolicy =
      aArgs.anonymizeClientIP()
          ? PrefetchAnonymizationPolicy::AnonymousClientIPWhenCrossOrigin
          : PrefetchAnonymizationPolicy::None;
  mStartTime = TimeStamp::Now();

  // Compute cross-origin flag directly from docPrincipal since Manager() is
  // not yet set during AllocPPrefetchRecordParent (IsCrossOriginToDocument
  // uses Manager() internally and would return false incorrectly here).
  bool isCrossOrigin = false;
  docPrincipal->IsSameOrigin(mURL, &isCrossOrigin);
  isCrossOrigin = !isCrossOrigin;

  if (StaticPrefs::dom_speculation_rules_same_origin_only() && isCrossOrigin) {
    LOG_SPECRULES_WARN(
        ("PrefetchRecordParent::Init: this=%p cross-origin rejected "
         "(same_origin_only=true)",
         this));
    mState = PrefetchState::Canceled;
    return;
  }

  if (!nsMixedContentBlocker::IsPotentiallyTrustworthyOrigin(mURL)) {
    LOG_SPECRULES_WARN(
        ("PrefetchRecordParent::Init: this=%p URL not trustworthy; dropping",
         this));
    mState = PrefetchState::Canceled;
    return;
  }

  mIsEligibleForPrefetch = true;

  // Set up the prefetch record's "isolated partition key" for cross-origin
  // isolation (used when creating navigation params by fetching).
  OriginAttributes isolatedAttrs = sourceAttrs;
  if (isCrossOrigin) {
    PopulateIsolatedPartitionKey(isolatedAttrs);
  }
  mIsolatedPartitionKey = isolatedAttrs.mPartitionKey;

  if (StaticPrefs::dom_speculation_rules_prefetch_dry_run()) {
    LOG_SPECRULES(
        ("PrefetchRecordParent::Init: this=%p dry run, not fetching", this));
    mState = PrefetchState::DryRun;
    return;
  }

  // Use CreateForNonDocument (instead of passing a null aLoadingNode to
  // NS_NewChannelInternal) so the resulting LoadInfo carries aWGP's
  // BrowsingContext ID; otherwise DevTools can't attribute the prefetch
  // request to the tab that triggered it.
  RefPtr<net::LoadInfo> loadInfo = net::LoadInfo::CreateForNonDocument(
      aWGP, docPrincipal, nsIContentPolicy::TYPE_OTHER,
      nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT,
      /* aSandboxFlags */ 0);
  loadInfo->SetOriginAttributes(isolatedAttrs);

  nsresult rv = NS_NewChannelInternal(
      getter_AddRefs(mChannel), mURL, loadInfo,
      nullptr,  // aPerformanceStorage — Resource Timing entries are recorded at
                //   activation, not during the background prefetch fetch
      nullptr,  // aLoadGroup — intentionally no load group; prefetch is
                // fire-and-forget
      this,     // aCallbacks (provides nsIChannelEventSink)
      nsIRequest::LOAD_BACKGROUND);

  if (NS_FAILED(rv)) {
    LOG_SPECRULES_WARN(
        ("PrefetchRecordParent::Init: this=%p NS_NewChannelInternal failed "
         "(0x%" PRIx32 ")",
         this, static_cast<uint32_t>(rv)));
    mState = PrefetchState::Canceled;
    return;
  }

  ConfigureSecPurpose(mChannel);

  MOZ_ASSERT(mReferrerInfo);
  nsCOMPtr<nsIHttpChannel> http = do_QueryInterface(mChannel);
  if (http) {
    DebugOnly<nsresult> rv2 = http->SetReferrerInfo(mReferrerInfo);
    MOZ_ASSERT(NS_SUCCEEDED(rv2));
  }

  AppendRedirectChainEntry(mURL);

  rv = mChannel->AsyncOpen(this);
  if (NS_FAILED(rv)) {
    LOG_SPECRULES_WARN(
        ("PrefetchRecordParent::Init: this=%p AsyncOpen failed (0x%" PRIx32 ")",
         this, static_cast<uint32_t>(rv)));
    mState = PrefetchState::Canceled;
    mChannel = nullptr;
    return;
  }

  mOutcome = Outcome::Incomplete;
}

bool PrefetchRecordParent::IsCrossOriginToDocument(nsIURI* aURI) const {
  auto* wgp = static_cast<WindowGlobalParent*>(Manager());
  if (!wgp) {
    return false;
  }
  nsCOMPtr<nsIPrincipal> principal = wgp->DocumentPrincipal();
  if (!principal) {
    return false;
  }
  bool sameOrigin = false;
  principal->IsSameOrigin(aURI, &sameOrigin);
  return !sameOrigin;
}

void PrefetchRecordParent::PopulateIsolatedPartitionKey(
    mozilla::OriginAttributes& aAttrs) {
  // Synthesize an opaque-origin partition key for cross-origin isolation.
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#prefetch-record-isolated-partition-key
  // Primitive identification deferred (not yet implemented).
}

void PrefetchRecordParent::ConfigureSecPurpose(nsIChannel* aChannel) {
  nsAutoCString secPurpose("prefetch"_ns);
  if (PrefetchIPAnonymizationPolicyRequiresAnonymity(mURL)) {
    secPurpose.AppendLiteral(";anonymous-client-ip");
  }
  nsCOMPtr<nsIHttpChannel> http = do_QueryInterface(aChannel);
  if (http) {
    DebugOnly<nsresult> rv =
        http->SetRequestHeader("Sec-Purpose"_ns, secPurpose, false);
  }
  LOG_SPECRULES_V(
      ("PrefetchRecordParent::ConfigureSecPurpose: this=%p Sec-Purpose=%s",
       this, secPurpose.get()));
}

bool PrefetchRecordParent::PrefetchIPAnonymizationPolicyRequiresAnonymity(
    nsIURI* aURI) const {
  // Implements "prefetch IP anonymization policy requires anonymity".
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#prefetch-ip-anonymization-policy-requires-anonymity
  if (mAnonPolicy !=
      PrefetchAnonymizationPolicy::AnonymousClientIPWhenCrossOrigin) {
    return false;
  }
  return IsCrossOriginToDocument(aURI);
}

void PrefetchRecordParent::AppendRedirectChainEntry(nsIURI* aURI) {
  ExchangeRecord rec;
  rec.mRequestURI = aURI;
  mRedirectChain.AppendElement(std::move(rec));
}

void PrefetchRecordParent::FillResponseOnLastEntry(nsIChannel* aChannel) {
  if (mRedirectChain.IsEmpty()) {
    return;
  }
  ExchangeRecord& last = mRedirectChain.LastElement();
  nsCOMPtr<nsIHttpChannel> http = do_QueryInterface(aChannel);
  if (!http) {
    return;
  }
  uint32_t status = 0;
  DebugOnly<nsresult> rvStatus = http->GetResponseStatus(&status);
  last.mResponseStatus = status;
  nsAutoCString nvsHeader;
  // GetResponseHeader may fail if header is absent; that is expected.
  if (NS_SUCCEEDED(http->GetResponseHeader("No-Vary-Search"_ns, nvsHeader))) {
    last.mNoVarySearchHeader = nvsHeader;
  }
}

bool PrefetchRecordParent::MatchesURL(nsIURI* aURL) const {
  // Implements "prefetch record matches a URL".
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#prefetch-record-matches-a-url
  // Step 1: "If prefetchRecord's URL is equal to url, return true."
  bool eq = false;
  if (mURL) {
    mURL->Equals(aURL, &eq);
  }
  if (eq) {
    return true;
  }

  // Step 2: "If prefetchRecord's response is not null" (approximated here by
  // a completed record with a non-empty redirect chain); step 3 "Otherwise,
  // return false" covers the early return below.
  if (mState != PrefetchState::Completed || mRedirectChain.IsEmpty()) {
    return false;
  }
  // Step 2a: "Let searchVariance be the result of obtaining a URL search
  // variance given prefetchRecord's redirect chain[0]'s response."
  const ExchangeRecord& first = mRedirectChain[0];
  net::NoVarySearchData data =
      net::ParseNoVarySearchHeader(first.mNoVarySearchHeader);
  // Step 2b: "If prefetchRecord's URL and url are equivalent modulo search
  // variance given searchVariance, return true."
  return net::URLsAreEquivalentModuloVariationConfig(mURL, aURL, data);
}

// Steps 2a-2b of "prefetch record is expected to match a URL": compare using
// the rule's No-Vary-Search hint, which is available before any response.
bool PrefetchRecordParent::MatchesNoVarySearchHint(nsIURI* aURL) const {
  if (!mURL || mNoVarySearchHint.IsEmpty()) {
    return false;
  }
  net::NoVarySearchData hint =
      net::ParseNoVarySearchHeader(NS_ConvertUTF16toUTF8(mNoVarySearchHint));
  return net::URLsAreEquivalentModuloVariationConfig(mURL, aURL, hint);
}

bool PrefetchRecordParent::IsExpectedToMatch(nsIURI* aURL) const {
  // Implements "prefetch record is expected to match a URL".
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#prefetch-record-is-expected-to-match-a-url
  // Step 1: "If prefetchRecord matches a URL given url, return true."
  if (MatchesURL(aURL)) {
    return true;
  }

  // Steps 2-3: with no response yet, fall back to the hint.
  return MatchesNoVarySearchHint(aURL);
}

bool PrefetchRecordParent::MatchesCandidateURL(nsIURI* aURL) const {
  if (!mURL) {
    return false;
  }
  bool eq = false;
  mURL->Equals(aURL, &eq);
  if (eq) {
    return true;
  }
  return MatchesNoVarySearchHint(aURL);
}

nsString PrefetchRecordParent::ComputePartitionKeyForChannel(
    nsIChannel* aChannel) const {
  nsCOMPtr<nsIURI> uri;
  aChannel->GetURI(getter_AddRefs(uri));
  if (!uri) {
    return nsString{};
  }
  OriginAttributes attrs;
  attrs.SetPartitionKey(uri, false);
  return attrs.mPartitionKey;
}

bool PrefetchRecordParent::IsReferrerPolicySufficientlyStrict() const {
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#list-of-sufficiently-strict-speculative-navigation-referrer-policies
  using dom::ReferrerPolicy;

  MOZ_ASSERT(mReferrerInfo);
  switch (mReferrerInfo->ReferrerPolicy()) {
    case ReferrerPolicy::_empty:
    case ReferrerPolicy::Strict_origin_when_cross_origin:
    case ReferrerPolicy::Strict_origin:
    case ReferrerPolicy::Same_origin:
    case ReferrerPolicy::No_referrer:
      return true;
    default:
      return false;
  }
}

// nsIStreamListener

NS_IMETHODIMP
PrefetchRecordParent::OnStartRequest(nsIRequest* aRequest) {
  nsCOMPtr<nsIChannel> ch = do_QueryInterface(aRequest);
  FillResponseOnLastEntry(ch);
  if (nsCOMPtr<nsICachingChannel> caching = do_QueryInterface(aRequest)) {
    (void)caching->GetCacheToken(getter_AddRefs(mCacheToken));
  }

  if (LOG_SPECRULES_ENABLED()) {
    nsCOMPtr<nsIHttpChannel> http = do_QueryInterface(aRequest);
    uint32_t status = 0;
    if (http) {
      (void)http->GetResponseStatus(&status);
    }
    LOG_SPECRULES(("PrefetchRecordParent::OnStartRequest: this=%p status=%u",
                   this, status));
  }
  return NS_OK;
}

NS_IMETHODIMP
PrefetchRecordParent::OnDataAvailable(nsIRequest* aRequest,
                                      nsIInputStream* aStream, uint64_t aOffset,
                                      uint32_t aCount) {
  mBytesReceived += aCount;
  uint32_t maxBytes = StaticPrefs::dom_speculation_rules_max_body_bytes();
  if (mBytesReceived > maxBytes) {
    LOG_SPECRULES_WARN(
        ("PrefetchRecordParent::OnDataAvailable: this=%p body cap exceeded "
         "(%" PRIu64 " > %u); canceling",
         this, mBytesReceived, maxBytes));
    if (mChannel) {
      mChannel->Cancel(NS_ERROR_FILE_TOO_BIG);
    } else {
      // TODO: add telemetry for this case.
      LOG_SPECRULES_WARN(
          ("PrefetchRecordParent::OnDataAvailable: this=%p mChannel is null "
           "at body cap",
           this));
    }
    return NS_ERROR_FILE_TOO_BIG;
  }
  uint32_t consumed = 0;
  return aStream->ReadSegments(NS_DiscardSegment, nullptr, aCount, &consumed);
}

NS_IMETHODIMP
PrefetchRecordParent::OnStopRequest(nsIRequest* aRequest, nsresult aStatus) {
  // aRequest, not mChannel: a redirected prefetch ends on the last hop.
  AccumulateTransferSize(aRequest);
  mChannel = nullptr;

  if (NS_FAILED(aStatus)) {
    LOG_SPECRULES_WARN(
        ("PrefetchRecordParent::OnStopRequest: this=%p failed 0x%" PRIx32, this,
         static_cast<uint32_t>(aStatus)));
    MarkCanceled();
    return NS_OK;
  }

  // dont use prefetch responses that does not have OK status
  // Spec Ref:
  // https://wicg.github.io/nav-speculation/prefetch.html#supports-prefetch
  if (!mRedirectChain.IsEmpty()) {
    uint32_t status = mRedirectChain.LastElement().mResponseStatus;
    if (status < 200 || status > 299) {
      LOG_SPECRULES_WARN(
          ("PrefetchRecordParent::OnStopRequest: this=%p final status %u does "
           "not support prefetch; discarding",
           this, status));
      MarkCanceled();
      return NS_OK;
    }
  }

  auto* wgp = static_cast<WindowGlobalParent*>(Manager());

  // Implements "complete a prefetch record".
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#prefetch-record-complete
  // Step 1: assert document is fully active.
  if (!wgp || wgp->IsClosed()) {
    return NS_OK;
  }

  // Steps 2-3: compute expiry time (currentTime + 5 min per spec; pref allows
  // shorter values in tests).
  mExpiryTime = TimeStamp::Now() +
                TimeDuration::FromMilliseconds(
                    StaticPrefs::dom_speculation_rules_record_expiry_ms());
  NS_NewTimerWithCallback(
      getter_AddRefs(mExpiryTimer),
      [self = RefPtr{this}](nsITimer*) { self->ReleaseCacheToken(); },
      StaticPrefs::dom_speculation_rules_record_expiry_ms(),
      nsITimer::TYPE_ONE_SHOT, "PrefetchRecordParent::Expiry"_ns);

  // Step 4: remove prior completed records with the same URL.
  wgp->DedupePrefetchRecords(this);
  // Step 5: set state to "completed".
  mState = PrefetchState::Completed;
  mOutcome = Outcome::Unused;
  // Step 6: trigger a prefetch status updated event ("ready").
  FirePrefetchStatusUpdated(true);
  wgp->NotifyPrefetchStateChanged(this);

  LOG_SPECRULES(
      ("PrefetchRecordParent::OnStopRequest: this=%p completed", this));
  return NS_OK;
}

// nsIInterfaceRequestor

NS_IMETHODIMP
PrefetchRecordParent::GetInterface(const nsIID& aIID, void** aResult) {
  if (aIID.Equals(NS_GET_IID(nsIChannelEventSink))) {
    NS_ADDREF_THIS();
    *aResult = static_cast<nsIChannelEventSink*>(this);
    return NS_OK;
  }
  return NS_ERROR_NO_INTERFACE;
}

// nsIChannelEventSink

NS_IMETHODIMP
PrefetchRecordParent::AsyncOnChannelRedirect(
    nsIChannel* aOldChannel, nsIChannel* aNewChannel, uint32_t aFlags,
    nsIAsyncVerifyRedirectCallback* aCb) {
  LOG_SPECRULES(
      ("PrefetchRecordParent::AsyncOnChannelRedirect: this=%p flags=%u", this,
       aFlags));
  FillResponseOnLastEntry(aOldChannel);
  AccumulateTransferSize(aOldChannel);

  // Validate the partition key on a cross-site redirect. Per "create
  // navigation params by fetching"
  // https://wicg.github.io/nav-speculation/prefetch.html#create-navigation-params-by-fetching,
  // a redirect whose partition key differs from the prefetch record's source
  // partition key is only allowed when the referrer policy is sufficiently
  // strict.
  nsString proposedKey = ComputePartitionKeyForChannel(aNewChannel);
  if (proposedKey != mSourcePartitionKey &&
      !IsReferrerPolicySufficientlyStrict()) {
    LOG_SPECRULES_WARN(
        ("PrefetchRecordParent::AsyncOnChannelRedirect: this=%p cross-site "
         "redirect rejected (referrer policy not strict enough)",
         this));
    aCb->OnRedirectVerifyCallback(NS_BINDING_ABORTED);
    return NS_OK;
  }

  nsCOMPtr<nsIURI> newURI;
  aNewChannel->GetURI(getter_AddRefs(newURI));
  if (!newURI) {
    LOG_SPECRULES_WARN(
        ("PrefetchRecordParent::AsyncOnChannelRedirect: this=%p newURI is null",
         this));
    aCb->OnRedirectVerifyCallback(NS_BINDING_ABORTED);
    return NS_OK;
  }
  AppendRedirectChainEntry(newURI);
  ConfigureSecPurpose(aNewChannel);
  aCb->OnRedirectVerifyCallback(NS_OK);
  return NS_OK;
}

void PrefetchRecordParent::FirePrefetchStatusUpdated(bool aSuccess) {
  LOG_SPECRULES(
      ("PrefetchRecordParent::FirePrefetchStatusUpdated: this=%p success=%d",
       this, static_cast<int>(aSuccess)));
  // "Trigger a prefetch status updated event" is not yet wired to the WebDriver
  // BiDi speculation module.
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#trigger-a-prefetch-status-updated-event
}

void PrefetchRecordParent::ReleaseCacheToken() {
  mCacheToken = nullptr;
  if (mExpiryTimer) {
    mExpiryTimer->Cancel();
    mExpiryTimer = nullptr;
  }
}

void PrefetchRecordParent::MarkCanceled() {
  // Implements "cancel and discard a prefetch record".
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#prefetch-record-cancel-and-discard
  // Step 2: assert state is not "canceled" (enforced as an idempotent guard).
  if (mState == PrefetchState::Canceled) {
    return;
  }

  LOG_SPECRULES(("PrefetchRecordParent::MarkCanceled: this=%p", this));

  mState = PrefetchState::Canceled;  // Step 3: set state to "canceled"
  ReleaseCacheToken();

  if (mChannel) {  // Step 4: abort the fetch controller
    mChannel->Cancel(NS_BINDING_ABORTED);
    mChannel = nullptr;
  }

  // Step 5: destroy prerendering traversable — deferred (no prerender in M1).
  FirePrefetchStatusUpdated(false);  // Step 7: trigger "failure" status event

  if (auto* wgp = static_cast<WindowGlobalParent*>(Manager())) {
    wgp->NotifyPrefetchStateChanged(this);
  }
}

// IPC

mozilla::ipc::IPCResult PrefetchRecordParent::RecvCancel() {
  LOG_SPECRULES(("PrefetchRecordParent::RecvCancel: this=%p", this));
  MarkCanceled();
  // https://wicg.github.io/nav-speculation/prefetch.html#prefetch-record-cancel-and-discard
  // Step 6: remove from document's prefetch records — handled by DOM's
  // subsequent Send__delete__.
  return IPC_OK();
}

void PrefetchRecordParent::ActorDestroy(ActorDestroyReason aReason) {
  LOG_SPECRULES(("PrefetchRecordParent::ActorDestroy: this=%p, reason=%d", this,
                 static_cast<int>(aReason)));
  // Safety net: ensure the channel is canceled if we die mid-flight
  // (covers PWindowGlobal teardown, tab close, content process crash).
  if (mChannel && mState == PrefetchState::Ongoing) {
    mChannel->Cancel(NS_BINDING_ABORTED);
    mChannel = nullptr;
  }
  ReleaseCacheToken();

  RecordDataUsage();
}

void PrefetchRecordParent::AccumulateTransferSize(nsIRequest* aRequest) {
  nsCOMPtr<nsIHttpChannel> http = do_QueryInterface(aRequest);
  if (!http) {
    return;
  }
  uint64_t size = 0;
  if (NS_SUCCEEDED(http->GetTransferSize(&size))) {
    mTransferSize += size;
  }
}

void PrefetchRecordParent::RecordDataUsage() {
  nsAutoCString label;
  switch (mOutcome) {
    // A candidate rejected before AsyncOpen cost no data.
    case Outcome::NotStarted:
      return;
    case Outcome::Incomplete:
      label.AssignLiteral("incomplete");
      break;
    case Outcome::Unused:
      label.AssignLiteral("unused");
      break;
    case Outcome::Used:
      label.AssignLiteral("used");
      break;
  }

  glean::speculation_rules::prefetch_transfer_size.Get(label).Accumulate(
      mTransferSize);
}

}  // namespace mozilla::dom
