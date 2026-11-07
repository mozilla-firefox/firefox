/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_PrefetchRecordParent_h
#define mozilla_dom_PrefetchRecordParent_h

#include "mozilla/Maybe.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/PPrefetchRecordParent.h"
#include "mozilla/dom/PrefetchTypes.h"
#include "mozilla/dom/ReferrerPolicyBinding.h"
#include "nsCOMPtr.h"
#include "nsIChannel.h"
#include "nsIChannelEventSink.h"
#include "nsIInputStream.h"
#include "nsIInterfaceRequestor.h"
#include "nsIReferrerInfo.h"
#include "nsIRequestObserver.h"
#include "nsIStreamListener.h"
#include "nsITimer.h"
#include "nsIURI.h"
#include "nsString.h"
#include "nsTArray.h"

namespace mozilla::dom {

class WindowGlobalParent;

// Parent-side actor for a speculation rules prefetch record.
// Authoritative: holds the prefetch record fields, opens the HTTP channel,
// and serves cache-key lookup at navigation activation.
//
// Spec:
// https://wicg.github.io/nav-speculation/prefetch.html#prefetch-record
class PrefetchRecordParent final : public PPrefetchRecordParent,
                                   public nsIStreamListener,
                                   public nsIInterfaceRequestor,
                                   public nsIChannelEventSink {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSICHANNELEVENTSINK

  // Called from WindowGlobalParent::AllocPPrefetchRecordParent.
  // aWGP is passed explicitly because Manager() is not yet set at alloc time.
  // Implements "start a referrer-initiated navigational prefetch".
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#start-a-referrer-initiated-navigational-prefetch
  void Init(WindowGlobalParent* aWGP, const SpeculativePrefetchArgs& aArgs);

  // Accessors for prefetch record fields
  // (https://wicg.github.io/nav-speculation/prefetch.html#prefetch-record) used
  // by Find / Wait / Activation algorithms.
  nsIURI* URL() const { return mURL; }
  PrefetchAnonymizationPolicy AnonymizationPolicy() const {
    return mAnonPolicy;
  }
  PrefetchState State() const { return mState; }
  PrefetchSource Source() const { return mSource; }
  const nsTArray<ExchangeRecord>& RedirectChain() const {
    return mRedirectChain;
  }
  const TimeStamp& ExpiryTime() const { return mExpiryTime; }
  const nsString& SourcePartitionKey() const { return mSourcePartitionKey; }
  const nsString& IsolatedPartitionKey() const { return mIsolatedPartitionKey; }
  bool HadConflictingCredentials() const { return mHadConflictingCredentials; }
  // The URL the held HTTP cache entry is stored under, or null if the record
  // holds no entry or the response was reached through redirects (the entry
  // then belongs to the last hop, which the navigation has to replay). This
  // can differ from URL() when the prefetch was served from a
  // No-Vary-Search-equivalent entry.
  already_AddRefed<nsIURI> CacheEntryURI() const;
  const nsTArray<Maybe<nsString>>& Tags() const { return mTags; }
  const nsString& NoVarySearchHint() const { return mNoVarySearchHint; }
  bool IsEligibleForPrefetch() const { return mIsEligibleForPrefetch; }

  // (https://wicg.github.io/nav-speculation/prefetch.html#prefetch-record-cancel-and-discard).
  mozilla::ipc::IPCResult RecvCancel();

  // Implements "trigger a prefetch status updated event".
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#trigger-a-prefetch-status-updated-event
  void FirePrefetchStatusUpdated(bool aSuccess);

  // Cancels the fetch and marks state=Canceled. Idempotent.
  // Called by RecvCancel and WindowGlobalParent::DedupePrefetchRecords.
  void MarkCanceled();

  // Called when a navigation activates this record.
  void MarkActivated() { mOutcome = Outcome::Used; }

  // Implements "prefetch record matches a URL" using the completed response's
  // No-Vary-Search header.
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#prefetch-record-matches-a-url
  bool MatchesURL(nsIURI* aURL) const;

  // Implements "prefetch record is expected to match a URL": the pre-response
  // variant used by "wait for a matching prefetch record" to check ongoing
  // records. Uses the rule's noVarySearchHint instead of the response header.
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#prefetch-record-is-expected-to-match-a-url
  bool IsExpectedToMatch(nsIURI* aURL) const;

  // Matches using only candidate data available in both experiment branches.
  // Unlike IsExpectedToMatch, this never consults response data, which the
  // dry-run branch cannot observe.
  bool MatchesCandidateURL(nsIURI* aURL) const;

  void ActorDestroy(ActorDestroyReason aReason) override;

 private:
  bool IsCrossOriginToDocument(nsIURI* aURI) const;
  void PopulateIsolatedPartitionKey(mozilla::OriginAttributes& aAttrs);
  void ConfigureSecPurpose(nsIChannel* aChannel);
  bool PrefetchIPAnonymizationPolicyRequiresAnonymity(nsIURI* aURI) const;
  void AppendRedirectChainEntry(nsIURI* aURI);
  void FillResponseOnLastEntry(nsIChannel* aChannel);
  nsString ComputePartitionKeyForChannel(nsIChannel* aChannel) const;
  bool IsReferrerPolicySufficientlyStrict() const;
  void ReleaseCacheToken();
  bool MatchesNoVarySearchHint(nsIURI* aURL) const;
  void AccumulateTransferSize(nsIRequest* aRequest);
  void RecordDataUsage();

  // Prefetch record fields:
  // https://wicg.github.io/nav-speculation/prefetch.html#prefetch-record
  nsTArray<Maybe<nsString>> mTags;
  nsCOMPtr<nsIURI> mURL;
  PrefetchAnonymizationPolicy mAnonPolicy = PrefetchAnonymizationPolicy::None;
  nsCOMPtr<nsIReferrerInfo> mReferrerInfo;
  nsString mNoVarySearchHint;
  PrefetchSource mSource = PrefetchSource::SpeculationRules;
  PrefetchState mState = PrefetchState::Ongoing;
  nsCOMPtr<nsIChannel> mChannel;
  // Keeps the response's HTTP cache entry alive so activation can be served
  // from it; no-store responses would otherwise be purged as expired.
  nsCOMPtr<nsISupports> mCacheToken;
  // Releases mCacheToken once the record expires.
  nsCOMPtr<nsITimer> mExpiryTimer;
  nsTArray<ExchangeRecord> mRedirectChain;
  TimeStamp mStartTime;
  TimeStamp mExpiryTime;
  nsString mSourcePartitionKey;
  nsString mIsolatedPartitionKey;
  bool mHadConflictingCredentials = false;
  bool mIsEligibleForPrefetch = false;

  // Only moves forward, and unlike mState it survives a later MarkCanceled.
  enum class Outcome : uint8_t { NotStarted, Incomplete, Unused, Used };
  Outcome mOutcome = Outcome::NotStarted;

  // Tracks incoming body bytes for the body cap.
  uint64_t mBytesReceived = 0;

  // On-the-wire bytes, summed over the redirect chain.
  uint64_t mTransferSize = 0;

 protected:
  ~PrefetchRecordParent() = default;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_PrefetchRecordParent_h
