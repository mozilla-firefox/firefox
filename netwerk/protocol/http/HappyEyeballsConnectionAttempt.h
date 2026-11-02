/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HappyEyeballsConnectionAttempt_h_
#define HappyEyeballsConnectionAttempt_h_

#include "ConnectionAttempt.h"
#include "ConnectionEstablisher.h"
#include "HappyEyeballsConnMgrDelegate.h"
#include "HappyEyeballsTransaction.h"
#include "happy_eyeballs_glue/HappyEyeballs.h"
#include "mozilla/Maybe.h"
#include "mozilla/Result.h"
#include "nsAHttpConnection.h"
#include "nsICancelable.h"
#include "nsIDNSListener.h"
#include "nsIDNSService.h"
#include "nsRefPtrHashtable.h"
#include "nsTHashSet.h"

namespace mozilla {
namespace net {

class HttpConnectionUDP;
class nsHttpConnection;
class PendingTransactionInfo;

class DnsRequestInfo final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(DnsRequestInfo)

  DnsRequestInfo(uint64_t aId, happy_eyeballs::DnsRecordType aType)
      : mId(aId), mType(aType) {}

  uint64_t Id() const { return mId; }
  happy_eyeballs::DnsRecordType Type() const { return mType; }
  void SetRequest(nsICancelable* aRequest) { mRequest = aRequest; }

  void Cancel() {
    if (mRequest) {
      mRequest->Cancel(NS_ERROR_ABORT);
      mRequest = nullptr;
    }
  }

 private:
  ~DnsRequestInfo() = default;

  uint64_t mId = 0;
  happy_eyeballs::DnsRecordType mType = happy_eyeballs::DnsRecordType::A;
  nsCOMPtr<nsICancelable> mRequest;
};

#define NS_HAPPYEYEBALLSCONNECTIONATTEMPT_IID \
  {0x3d2e8a41, 0x9c5b, 0x4f6e, {0xa1, 0x02, 0x2b, 0x7c, 0x8e, 0x4d, 0x6f, 0x90}}

class HappyEyeballsConnectionAttempt final : public ConnectionAttempt,
                                             public nsIDNSListener,
                                             public nsITimerCallback,
                                             public nsINamed {
 public:
  NS_INLINE_DECL_STATIC_IID(NS_HAPPYEYEBALLSCONNECTIONATTEMPT_IID)

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDNSLISTENER
  NS_DECL_NSITIMERCALLBACK
  NS_DECL_NSINAMED

  // aRetryWithoutTRR is set for the fallback attempt started after a first
  // attempt's TRR-resolved addresses all failed to connect; it forces DNS
  // resolution to bypass TRR (and the DNS cache).
  HappyEyeballsConnectionAttempt(nsHttpConnectionInfo* ci,
                                 nsAHttpTransaction* trans, uint32_t caps,
                                 bool speculative, bool urgentStart,
                                 bool retryWithoutTRR = false);

  nsresult Init(ConnectionEntry* ent) override;
  void Abandon() override;
  double Duration(TimeStamp epoch) override;
  void OnTimeout() override;
  void PrintDiagnostics(nsCString& log) override;
  bool Claim(nsHttpTransaction* newTransaction = nullptr) override;
  // No-op: HE attempts are 1:1 owned by their creator transaction. See
  // ConnectionAttempt::Unclaim's comment for the failure mode this
  // override prevents.
  void Unclaim() override {}
  uint32_t UnconnectedUDPConnsLength() const override;

  // Test-only seams; call before Init().
  void SetConnectionEstablisherFactoryForTesting(
      ConnectionEstablisherFactory* aFactory) {
    mEstablisherFactory = aFactory;
  }
  void SetConnMgrDelegateForTesting(HappyEyeballsConnMgrDelegate* aDelegate) {
    mConnMgrDelegate = aDelegate;
  }

  // Test-only accessors.
  bool WasTransactionAdoptedForTesting() const { return mTransactionAdopted; }
  ZeroRttHandle* ZeroRttHandleForTesting() const { return mZeroRttHandle; }

  // Real transaction accessor, used by the shared ZeroRttHandle.
  nsHttpTransaction* RealHttpTransaction() const {
    return mTransaction ? mTransaction->QueryHttpTransaction() : nullptr;
  }

  // Called by ZeroRttHandle::Finish0RTT on the winning HT. Pulls the
  // real nsHttpTransaction out of the pending queue (so a reject-path
  // realTransaction->Close → Restart doesn't trip the pending-queue assertion,
  // and so EnterSucceeded won't re-dispatch it) and calls aWinner->Adopt().
  // No-op if the HE race was started without a real transaction yet
  // (speculative entry) or the transaction can't be queried.
  void AdoptWinner(HappyEyeballsTransaction* aWinner);

  // Remove the real nsHttpTransaction from this entry's pending queue
  // if it's still there. Returns true if the trans was found and
  // removed, false if it was already gone (dispatched elsewhere).
  // Called by ZeroRttHandle at the first Do0RTT acceptance: if the
  // trans is already gone (returns false), Do0RTT must decline 0-RTT
  // so we don't put early-data bytes on the wire for a trans that is
  // already being served by a different connection.
  bool LockInRealTransactionFromPendingQueue();

  // Lifecycle state machine. IsTerminal() returns true from Succeeded
  // onward.
  //   Init                       : initial state; Init() not yet called.
  //   Connecting                 : Rust state machine driving DNS and
  //                                connection attempts.
  //   ZeroRttRacing              : a racer has entered 0-RTT, no winner yet.
  //   ProcessingConnectionResult : inside a TCP/UDP connect callback.
  //   Succeeded                  : Rust state machine emitted Succeeded.
  //   Failed                     : Rust state machine emitted Failed.
  //   RestartTransaction         : restart real transaction.
  //   AbortTransaction           : close real transaction.
  //   TimedOut                   : OnTimeout fired.
  //   Done                       : terminal; Abandon() released all refs.
  enum class State : uint8_t {
    Init,
    Connecting,
    ZeroRttRacing,
    ProcessingConnectionResult,
    Succeeded,
    Failed,
    RestartTransaction,
    AbortTransaction,
    TimedOut,
    Done,
  };

  // Outcome of classifying a connection result.
  enum class ConnResultOutcome : uint8_t {
    ForwardAndContinue,  // forward to Rust state machine and continue
    RestartTransaction,  // abort HE; restart real transaction
    AbortTransaction,    // abort HE; close real transaction with status
  };

  // Payload for Transition; entry actions read only the fields they need.
  struct TransitionPayload {
    nsresult mCloseReason = NS_OK;
    Maybe<happy_eyeballs::FailureReason> mFailureReason;
  };

  bool IsTerminal() const { return mState >= State::Succeeded; }

 private:
  ~HappyEyeballsConnectionAttempt();

  // Single dispatcher for lifecycle transitions. Outcome entry actions
  // do their teardown and end by calling Abandon() -> Done.
  void Transition(State aNext);
  void Transition(State aNext, TransitionPayload aPayload);

  // Pure classifier over status + current 0-RTT flags. No side effects.
  ConnResultOutcome ClassifyConnectionResult(nsresult aStatus) const;

  // Release the real transaction: remove it from |aEntry|'s pending
  // queue (if not null), Close() it with the given reason, and drop
  // our reference. No-op if the transaction has already been adopted
  // onto a winning carrier (mTransactionAdopted), see bug 2040246.
  //
  // Caller contract: capture mEntry into a local RefPtr<ConnectionEntry>
  // at the top of the Enter* action and pass it here. The explicit
  // parameter is what keeps this safe regardless of when the helper is
  // called relative to RemoveConnectionAttempt(true) / Abandon() /
  // EnterDone(), which nulls mEntry.
  void ReleaseRealTransaction(nsresult aCloseReason, ConnectionEntry* aEntry);

  nsresult CreateHappyEyeballs(ConnectionEntry* ent);

  nsresult ProcessConnectionResult(const NetAddr& aAddr, nsresult aStatus,
                                   uint64_t aId);

  nsresult ProcessEchRetryConnectionResult(const NetAddr& aAddr, uint64_t aId,
                                           const nsACString& aEchBytes);
  Maybe<nsCString> MaybeExtractRetryEchConfig(
      ConnectionEstablisher* aEstablisher, nsresult aStatus);

  nsresult ProcessHappyEyeballsOutput();

  // Report the domain lookup span: domainLookupStart is the first DNS query
  // (any type), domainLookupEnd is the start of the first connection attempt.
  void DnsLookupTimings(TimeStamp& aStart, TimeStamp& aEnd) const;

  // Fill the four connect-phase timings from the first-racer timestamps.
  // For QUIC, tcpConnectEnd stays null and secureConnectionStart equals
  // connectStart.
  void FillConnectTimings(bool aIsQuic, TimingStruct& aTimings) const;

  void MaybeSendTransportStatus(nsresult aStatus,
                                nsITransport* aTransport = nullptr,
                                int64_t aProgress = 0);

  // DNS lookups
  Result<nsIDNSService::DNSFlags, nsresult> SetupDnsFlags(
      happy_eyeballs::DnsRecordType aType, bool aAllowStale);
  void DNSLookup(happy_eyeballs::DnsRecordType aType,
                 Result<nsIDNSService::DNSFlags, nsresult> aFlags, uint64_t aId,
                 const nsACString& aHostname);

  // DNS answers
  nsresult OnARecord(nsIDNSRecord* aRecord, nsresult status, uint64_t aId);
  nsresult OnAAAARecord(nsIDNSRecord* aRecord, nsresult status, uint64_t aId);
  nsresult OnHTTPSRecord(nsIDNSRecord* aRecord, nsresult status, uint64_t aId);

  // True when this attempt's (TRR-resolved) addresses all failed to connect and
  // we should hand the transaction to a fresh attempt that re-resolves with TRR
  // disabled (the connection-time equivalent of nsSocketTransport's legacy
  // native-DNS fallback). Only applies to a real transaction, outside TRR_ONLY
  // mode, and not to an attempt that is already a TRR-disabled retry.
  bool ShouldRetryWithoutTRR(happy_eyeballs::FailureReason aReason) const;
  // Hand the transaction to a new HappyEyeballsConnectionAttempt that resolves
  // with TRR disabled, and abandon this one without closing the transaction.
  void RetryWithoutTRR();
  // Shared body of RetryWithoutTRR/RetryForLocalAddress: start a fresh attempt
  // for the real transaction via the delegate (aRetryWithoutTRR selects the TRR
  // fallback vs. a plain retry), then detach and abandon this one. aLogTag
  // names the caller in log messages.
  void DoRetry(bool aRetryWithoutTRR, const char* aLogTag);
  // True when every failure was a local-peer refusal while speculative and the
  // attempt has since been claimed (mAllow1918 became true).
  bool ShouldRetryForLocalAddress(happy_eyeballs::FailureReason aReason) const;
  // Hand the transaction to a new HappyEyeballsConnectionAttempt that allows
  // local addresses, and abandon this one without closing the transaction.
  void RetryForLocalAddress();
  // Once the origin host's (mHost) A and AAAA lookups have both completed,
  // build the connection entry's coalescing keys from their combined addresses
  // and reprocess the pending queue. Addresses resolved for an HTTPS RR target
  // name are intentionally excluded.
  void MaybeBuildOriginCoalescingKeys();

  // Connection Attempt
  // Build a per-establisher HappyEyeballsTransaction wired up to forward
  // its OnTransportStatus events back through MaybeSendTransportStatus
  // for dedup + propagation to the real transaction. aEstablisherId lets the
  // client-auth forwarders identify which racer.
  already_AddRefed<HappyEyeballsTransaction> CreateAttemptTransaction(
      nsHttpConnectionInfo* aInfo, uint64_t aEstablisherId);

  // TLS handshake saw a CertificateRequest: pause polling (no new attempts)
  // until the prompt resolves. Selected clears the pause.
  void OnClientAuthCertificateRequested(uint64_t aEstablisherId);
  void OnClientAuthCertificateSelected(uint64_t aEstablisherId);

  nsresult EstablishTCPConnection(NetAddr aAddr, uint16_t aPort,
                                  nsTArray<uint8_t>&& aEchConfig, uint64_t aId,
                                  bool aIsEchRetry);
  // Handles both TCP and UDP results; IsUDP() distinguishes where it matters.
  void HandleConnectionResult(
      Result<RefPtr<HttpConnectionBase>, nsresult> aResult,
      ConnectionEstablisher* aEstablisher, uint64_t aId);
  // If 0-RTT was active, forward the connection's security info to the real
  // transaction so MaybeRemoveSSLToken() in nsHttpTransaction::Restart() can
  // clear the SSL token cache for the retry.
  void MaybeForward0RTTSecurityInfo(ConnectionEstablisher* aEstablisher);
  void CancelConnection(uint64_t aId);
  nsresult EstablishUDPConnection(NetAddr aAddr, uint16_t aPort,
                                  nsTArray<uint8_t>&& aEchConfig, uint64_t aId,
                                  bool aIsEchRetry);

  nsresult CheckLNA(nsISocketTransport* aTransport);
  nsresult CheckLNAForAddr(const NetAddr& aAddr);

  // Timer
  void SetupTimer(uint64_t aTimeout);

  // Entry actions for the terminal states; each ends by Transitioning to Done.
  void EnterSucceeded();
  void EnterFailed(happy_eyeballs::FailureReason aReason);
  void EnterRestartTransaction(nsresult aCloseReason);
  void EnterAbortTransaction(nsresult aCloseReason);
  void EnterTimedOut();
  void EnterDone();

  void ProcessTCPConn(HttpConnectionBase* aConn, ConnectionEntry* aEntry,
                      bool aTransactionAlreadyOnConn);
  void ProcessUDPConn(HttpConnectionBase* aConn, ConnectionEntry* aEntry,
                      bool aTransactionAlreadyOnConn);
  void CloseHttpTransaction(happy_eyeballs::FailureReason aReason,
                            ConnectionEntry* aEntry);

  RefPtr<HappyEyeballs> mHappyEyeballs;

  nsCString mHost;

  nsRefPtrHashtable<nsPtrHashKey<nsICancelable>, DnsRequestInfo>
      mDnsRequestTable;

  nsRefPtrHashtable<nsUint64HashKey, ConnectionEstablisher>
      mConnectionEstablisherTable;
  // Creates per-attempt establishers; default uses real sockets.
  RefPtr<ConnectionEstablisherFactory> mEstablisherFactory;
  // Wraps the connection manager + entry; default forwards to ConnMgr().
  RefPtr<HappyEyeballsConnMgrDelegate> mConnMgrDelegate;
  RefPtr<HttpConnectionBase> mOutputConn;
  // Winning establisher's per-attempt transaction; used to read its
  // collected handshake timings before we dispatch the real transaction.
  RefPtr<HappyEyeballsTransaction> mOutputTrans;
  uint64_t mOutputConnId{0};
  uint16_t mAddrFamily{0};

  // IDs of the still-outstanding A/AAAA lookups issued for mHost (the origin /
  // routed host). HTTPS RR target-name lookups are not tracked here, so their
  // addresses never enter the coalescing keys.
  nsTHashSet<uint64_t> mOriginDnsLookupIds;
  // Combined A+AAAA addresses resolved for mHost, used to build the entry's
  // coalescing keys once both lookups complete.
  nsTArray<NetAddr> mOriginAddresses;

  // When set, DNS resolution bypasses TRR and the cache. Set on the fallback
  // attempt started after TRR-resolved addresses failed to connect; also
  // prevents that fallback attempt from triggering a second retry.
  bool mRetryWithoutTRR = false;

  nsCOMPtr<nsITimer> mTimer;
  WeakPtr<ConnectionEntry> mEntry;
  State mState{State::Init};
  nsresult mLastConnectionError = NS_OK;
  nsresult mLastDnsError = NS_OK;
  nsTHashSet<uint32_t> mSentTransportStatuses;

  // Shared 0-RTT coordinator. Created lazily (first time we hand out a
  // per-attempt HappyEyeballsTransaction) and passed to every racer.
  RefPtr<ZeroRttHandle> mZeroRttHandle;

  // True once the real transaction has been handed to a winning carrier. After
  // this, ReleaseRealTransaction must NOT Close()/Restart() the real
  // transaction — doing so nulls mConnection while the carrier's stream still
  // feeds bytes to it (bug 2040246).
  bool mTransactionAdopted = false;

  DnsMetadata mDnsMetadata;
  bool mTRRInfoForwarded = false;

  // domainLookupStart: when the first DNS query (A/AAAA/HTTPS) was issued.
  TimeStamp mFirstDnsLookupStart;
  TimeStamp mFirstConnectionStart;
  // domainLookupEnd: the latest DNS response (OnLookupComplete) that arrives
  // before the first connection attempt starts; if none has by then, it is set
  // to mFirstConnectionStart. domainLookupEnd is reported as
  // min(mFirstConnectionStart, mDnsResolutionEnd) (see DnsLookupTimings).
  TimeStamp mDnsResolutionEnd;

  // First-racer connect timings, mirroring the domainLookup span: the first
  // racer to reach each milestone across all racers (captured in
  // MaybeSendTransportStatus). connectStart is mFirstConnectionStart;
  // mFirstConnectEnd is the winning racer's completion (the first connection
  // to fully succeed). For QUIC, tcpConnectEnd stays null and
  // secureConnectionStart equals connectStart.
  TimeStamp mFirstTcpConnectEnd;
  TimeStamp mFirstSecureConnectionStart;
  TimeStamp mFirstConnectEnd;

  // While set, attempt mClientAuthHolderId is on the cert prompt and
  // ProcessHappyEyeballsOutput stops polling so no new attempts start.
  bool mPausedForClientAuth = false;
  uint64_t mClientAuthHolderId = 0;

  // Set when a speculative attempt refused a local (RFC1918) peer. If it is
  // later claimed and every attempt was such a refusal, we retry.
  bool mLocalAddrRefused = false;
  // Cleared once any establisher does something other than refuse a local peer
  // (i.e. actually attempts a connection). With mLocalAddrRefused, means "every
  // attempt refused a local peer".
  bool mAllAttemptsRefusedLocal = true;
};

}  // namespace net
}  // namespace mozilla

#endif
