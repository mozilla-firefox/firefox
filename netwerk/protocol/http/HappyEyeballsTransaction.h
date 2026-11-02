/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HappyEyeballsTransaction_h_
#define HappyEyeballsTransaction_h_

#include <functional>

#include "SpeculativeTransaction.h"
#include "ZeroRttHandle.h"
#include "mozilla/Maybe.h"

namespace mozilla {
namespace net {

class HappyEyeballsConnectionAttempt;
class nsHttpTransaction;

// One HappyEyeballsTransaction (HT) exists per ConnectionEstablisher
// during a Happy Eyeballs race.
//
// Lifecycle — two phases:
//
//   1. RACING (pre-adopt): HT behaves like SpeculativeTransaction —
//      drives the TLS handshake on its connection without consuming
//      real request data. The only interaction with the real txn is
//      via the shared ZeroRttHandle, which reads the real txn's
//      request stream for 0-RTT early data. HT does NOT hold a
//      reference to the real nsHttpTransaction and MUST NOT mutate
//      real txn state in this phase.
//
//   2. ADOPTED (post-winner): after HE picks a winner, the owning
//      HappyEyeballsConnectionAttempt hands the real nsHttpTransaction
//      to the winning HT via Adopt(). Adopt points the real txn at the
//      same live connection HT was racing on AND calls the carrier's
//      SwapTransaction to re-point its transaction slot from HT to
//      real_txn — Http3Session / Http2Session swap the stream-hash key
//      and Http{2,3}Stream::mTransaction, nsHttpConnection swaps its
//      own mTransaction. After that swap the connection drives the
//      real txn directly; HT becomes dormant and is dropped when
//      HappyEyeballsConnectionAttempt releases.
//
// Responsibilities retained in both phases:
//   * Collects TCP/TLS timings locally (inherited from NullHttpTransaction
//     via OnTransportStatus). HET grabs the winner's Timings() and
//     bootstraps the real transaction before adoption.
//   * Forwards OnTransportStatus to HET via the caller-supplied status
//     forwarder so HET can dedup and propagate a single copy to the real
//     transaction.
//   * Shares a single ZeroRttHandle (owned by HET) with its racers.
class HappyEyeballsTransaction final : public SpeculativeTransaction {
 public:
  using StatusForwarder = std::function<void(nsITransport*, nsresult, int64_t)>;
  using ClientAuthForwarder = std::function<void()>;

  HappyEyeballsTransaction(nsHttpConnectionInfo* aConnInfo,
                           nsIInterfaceRequestor* aCallbacks, uint32_t aCaps,
                           uint64_t aBrowserId,
                           StatusForwarder&& aStatusForwarder,
                           ClientAuthForwarder&& aClientAuthRequestedForwarder,
                           ClientAuthForwarder&& aClientAuthSelectedForwarder,
                           ZeroRttHandle* aZeroRttHandle);

  // Forward the real transaction's BrowserId: PSM's client-cert dialog looks
  // up a BrowsingContext.
  uint64_t BrowserId() override { return mBrowserId; }

  void SetConnectedCallback(std::function<void(nsresult)>&& aCallback) {
    mCloseCallback = std::move(aCallback);
  }

  // Transition this HT from RACING to ADOPTED. Called from
  // ZeroRttHandle::Finish0RTT on the winning HT. Hands the real txn to
  // the live connection and calls the carrier's SwapTransaction
  // (Http3Session / Http2Session / nsHttpConnection) to re-key the
  // carrier from HT to real_txn so the connection bypasses HT
  // entirely post-adopt.
  void Adopt(nsHttpTransaction* aRealTxn);

  // Lifecycle state.
  //   Racing  : default after construction. HT is racing in the HE
  //             pool; can transition to Adopted (winner) or Closed
  //             (loser / failure / abandoned).
  //   Adopted : Adopt() ran; mRealTxn is set; SwapTransaction has
  //             re-keyed the carrier from HT to the real txn.
  //   Closed  : Close() ran. Terminal.
  //
  // Legal transitions:
  //   Racing  -> Adopted | Closed
  //   Adopted -> Closed
  //   Closed  -> Closed (idempotent re-Close is a no-op)
  enum class State : uint8_t {
    Racing,
    Adopted,
    Closed,
  };
  State GetState() const { return mState; }

  // mRealTxn is sticky: set in Adopt(), never cleared. So IsAdopted()
  // is true after Adopt regardless of subsequent Close transitions.
  bool IsAdopted() const { return !!mRealTxn; }

  // nsAHttpTransaction virtuals we override. Only the methods that
  // have HT-specific behavior (RACING-phase 0-RTT driving, disqualify
  // logic on Close, status dedupe, RequestHead-through-handle) are
  // overridden; everything else inherits NullHttpTransaction defaults.
  // Post-Adopt the carrier (session or nsHttpConnection) no longer
  // calls these on HT — its SwapTransaction re-pointed it at the real
  // txn.
  void OnTransportStatus(nsITransport* aTransport, nsresult aStatus,
                         int64_t aProgress) override;
  void OnClientAuthCertificateRequested() override;
  void OnClientAuthCertificateSelected() override;
  nsresult ReadSegments(nsAHttpSegmentReader* aReader, uint32_t aCount,
                        uint32_t* aCountRead) override;
  // Asserts unreachable in debug. By design HET is never the transaction
  // the connection routes response bytes to.
  nsresult WriteSegments(nsAHttpSegmentWriter* aWriter, uint32_t aCount,
                         uint32_t* aCountWritten) override;
  void Close(nsresult aReason) override;
  nsHttpTransaction* QueryHttpTransaction() override;

  // Forward to the real nsHttpTransaction this race is being run for so
  // the deferred LNA check in nsHttpConnection::HandshakeDoneInternal
  // sees the same answer it would on the non-HE path. Pre-Adopt the
  // real txn is reachable through the shared ZeroRttHandle; post-Adopt
  // mRealTxn is set and used directly.
  bool AllowedToConnectToIpAddressSpace(
      nsILoadInfo::IPAddressSpace aTargetIpAddressSpace) override;

  // Pre-adopt, Http3Stream / Http2Stream reads pseudo-header values
  // off RequestHead() when encoding the 0-RTT HEADERS frame; we have
  // to return the real txn's head so :method/:authority/:path/:scheme
  // are correct. Post-swap the session queries the real txn directly.
  const nsHttpRequestHead* RequestHead() override;

  // Not implementable from this context — callers must reach the real
  // txn through nsHttpChannel's own HTTPS-RR path, not via the shim.
  nsresult FetchHTTPSRR() override;
  nsresult OnHTTPSRRAvailable(nsIDNSHTTPSSVCRecord* aHTTPSSVCRecord,
                              nsISVCBRecord* aHighestPriorityRecord,
                              const nsACString& aCname) override;

  // 0-RTT interface — delegates to the shared ZeroRttHandle while it is
  // non-null (i.e. before adoption; the Adopted transition clears it).
  bool Do0RTT(bool aCanSendEarlyData) override;
  nsresult Finish0RTT(bool aRestart, bool aAlpnChanged) override {
    return mZeroRttHandle
               ? mZeroRttHandle->Finish0RTT(this, aRestart, aAlpnChanged)
               : NS_OK;
  }

  // Position in the real transaction's request stream this attempt
  // has sent as 0-RTT early data. Nothing() means this attempt is not
  // in the 0-RTT flow. Read and written by the shared ZeroRttHandle.
  Maybe<uint64_t>& Request0RttStreamOffset() {
    return m0RttRequestStreamOffset;
  }
  const Maybe<uint64_t>& Request0RttStreamOffset() const {
    return m0RttRequestStreamOffset;
  }
  bool Entered0RTT() const { return m0RttRequestStreamOffset.isSome(); }

  // Remove SSL session tokens for this 0-RTT attempt via the live connection
  // (uses GetPeerId() as the SSLTokensCache key, not mConnInfo->HashKey()).
  void MaybeRemoveSSLTokens();

 private:
  ~HappyEyeballsTransaction() override;

  // Single dispatcher for state transitions. Switches on the target
  // state, asserts the move is legal from the current state, commits
  // mState, and runs the actions tied to entering that state. Public
  // Adopt() / Close() pre-validate their inputs and then call here;
  // every state mutation goes through this function.
  //
  // aRealTxn is consumed by the Adopted entry; aReason by Closed.
  // Other parameters are ignored per-case.
  void Transition(State aNext, nsHttpTransaction* aRealTxn = nullptr,
                  nsresult aReason = NS_OK);

  StatusForwarder mStatusForwarder;
  ClientAuthForwarder mClientAuthRequestedForwarder;
  ClientAuthForwarder mClientAuthSelectedForwarder;
  RefPtr<ZeroRttHandle> mZeroRttHandle;
  uint64_t mBrowserId = 0;

  // Non-null only after Adopt(). Backs QueryHttpTransaction() so
  // callers that still hold an HT pointer can reach the real txn.
  // Sticky: never cleared once set, so IsAdopted() is correct
  // post-Close too.
  RefPtr<nsHttpTransaction> mRealTxn;

  // Set by ZeroRttHandle when this attempt joins the 0-RTT flow and
  // advances as bytes are sent.
  Maybe<uint64_t> m0RttRequestStreamOffset;

  State mState = State::Racing;
};

}  // namespace net
}  // namespace mozilla

#endif
