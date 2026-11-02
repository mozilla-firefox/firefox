/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HappyEyeballsConnectionAttempt.h"

#include <algorithm>

#include "ConnectionEntry.h"
#include "HttpConnectionUDP.h"
#include "HttpLog.h"
#include "NSSErrorsService.h"
#include "NetworkConnectivityService.h"
#include "PendingTransactionInfo.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/net/HttpActivityArgs.h"
#include "nsDNSService2.h"
#include "nsHttpConnectionMgr.h"
#include "nsHttpHandler.h"
#include "nsHttpTransaction.h"
#include "nsIDNSAdditionalInfo.h"
#include "nsIHttpActivityObserver.h"
#include "nsQueryObject.h"
#include "nsSocketTransport2.h"
#include "nsSocketTransportService2.h"
#include "sslerr.h"

// Log on level :5, instead of default :4.
#undef LOG
#define LOG(args) LOG5(args)
#undef LOG_ENABLED
#define LOG_ENABLED() LOG5_ENABLED()

namespace mozilla::net {

using happy_eyeballs::happy_eyeballs_process_connection_result;
using happy_eyeballs::happy_eyeballs_process_dns_response_a;
using happy_eyeballs::happy_eyeballs_process_dns_response_aaaa;
using happy_eyeballs::happy_eyeballs_process_dns_response_https;
using happy_eyeballs::happy_eyeballs_process_ech_retry;
using happy_eyeballs::happy_eyeballs_process_output;

static void NotifyConnectionActivity(nsHttpConnectionInfo* aConnInfo,
                                     uint32_t aSubtype) {
  HttpConnectionActivity activity(
      aConnInfo->HashKey(), aConnInfo->GetOrigin(), aConnInfo->OriginPort(),
      aConnInfo->EndToEndSSL(), !aConnInfo->GetEchConfig().IsEmpty(),
      aConnInfo->IsHttp3());
  gHttpHandler->ObserveHttpActivityWithArgs(HttpActivityArgs(activity),
                                            NS_ACTIVITY_TYPE_HTTP_CONNECTION,
                                            aSubtype, PR_Now(), 0, ""_ns);
}

NS_IMPL_ADDREF_INHERITED(HappyEyeballsConnectionAttempt, ConnectionAttempt)
NS_IMPL_RELEASE_INHERITED(HappyEyeballsConnectionAttempt, ConnectionAttempt)

NS_INTERFACE_MAP_BEGIN(HappyEyeballsConnectionAttempt)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsITimerCallback)
  NS_INTERFACE_MAP_ENTRY(nsINamed)
  NS_INTERFACE_MAP_ENTRY(nsIDNSListener)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(HappyEyeballsConnectionAttempt)
NS_INTERFACE_MAP_END

// Production delegate: forwards verbatim to the connection manager / entry.
// Friended by nsHttpConnectionMgr to reach its private dispatch helpers.
class DefaultHappyEyeballsConnMgrDelegate final
    : public HappyEyeballsConnMgrDelegate {
 public:
  already_AddRefed<PendingTransactionInfo> FindTransaction(
      bool aRemoveWhenFound, ConnectionEntry* aEntry,
      nsAHttpTransaction* aTrans) override {
    return gHttpHandler->ConnMgr()->FindTransactionHelper(aRemoveWhenFound,
                                                          aEntry, aTrans);
  }
  nsresult DispatchTransaction(ConnectionEntry* aEntry,
                               nsHttpTransaction* aTrans,
                               HttpConnectionBase* aConn) override {
    return gHttpHandler->ConnMgr()->DispatchTransaction(aEntry, aTrans, aConn);
  }
  void AddTransaction(nsHttpTransaction* aTrans, int32_t aPriority) override {
    gHttpHandler->ConnMgr()->AddTransaction(aTrans, aPriority);
  }
  void ReportSpdyConnection(HttpConnectionBase* aConn, bool aUsingSpdy,
                            bool aDisallowHttp3) override {
    if (RefPtr<nsHttpConnection> h1 = do_QueryObject(aConn)) {
      gHttpHandler->ConnMgr()->ReportSpdyConnection(h1, aUsingSpdy,
                                                    aDisallowHttp3);
    }
  }
  void ReportHttp3Connection(HttpConnectionBase* aConn,
                             ConnectionEntry* aEntry) override {
    gHttpHandler->ConnMgr()->ReportHttp3Connection(aConn, aEntry);
  }
  void ReclaimConnection(HttpConnectionBase* aConn) override {
    gHttpHandler->ConnMgr()->OnMsgReclaimConnection(aConn);
  }
  void ProcessSpdyPendingQ(ConnectionEntry* aEntry) override {
    gHttpHandler->ConnMgr()->ProcessSpdyPendingQ(aEntry);
  }
  already_AddRefed<ConnectionEntry> HandOffHttp3OnlyConnection(
      HttpConnectionBase* aConn, ConnectionEntry* aFromEnt) override {
    return gHttpHandler->ConnMgr()->HandOffHttp3OnlyConnection(aConn, aFromEnt);
  }
  void InsertIntoActiveConns(ConnectionEntry* aEntry,
                             HttpConnectionBase* aConn) override {
    aEntry->InsertIntoActiveConns(aConn);
  }
  void RemoveConnectionAttempt(ConnectionEntry* aEntry,
                               ConnectionAttempt* aAttempt,
                               bool aAbandon) override {
    aEntry->RemoveConnectionAttempt(aAttempt, aAbandon);
  }
  void RecordIPFamilyPreference(ConnectionEntry* aEntry,
                                uint16_t aFamily) override {
    aEntry->RecordIPFamilyPreference(aFamily);
  }
  void ResetIPFamilyPreference(ConnectionEntry* aEntry) override {
    aEntry->ResetIPFamilyPreference();
  }
  bool MaybeProcessCoalescingKeys(ConnectionEntry* aEntry,
                                  const nsTArray<NetAddr>& aAddresses,
                                  bool aIsHttp3) override {
    return aEntry->MaybeProcessCoalescingKeys(aAddresses, aIsHttp3);
  }
  bool RemoveTransFromPendingQ(ConnectionEntry* aEntry,
                               nsHttpTransaction* aTrans) override {
    return aEntry->RemoveTransFromPendingQ(aTrans);
  }
  nsresult StartRetry(ConnectionEntry* aEntry, nsHttpTransaction* aTrans,
                      uint32_t aCaps, bool aSpeculative, bool aUrgentStart,
                      bool aAllow1918, bool aRetryWithoutTRR) override {
    RefPtr<PendingTransactionInfo> pendingTransInfo =
        new PendingTransactionInfo(aTrans);
    return aEntry->CreateDnsAndConnectSocket(
        aTrans, aCaps, aSpeculative, aUrgentStart, aAllow1918, pendingTransInfo,
        aRetryWithoutTRR);
  }
};

HappyEyeballsConnectionAttempt::HappyEyeballsConnectionAttempt(
    nsHttpConnectionInfo* ci, nsAHttpTransaction* trans, uint32_t caps,
    bool speculative, bool urgentStart, bool retryWithoutTRR)
    : ConnectionAttempt(ci, trans, caps, speculative, urgentStart),
      mEstablisherFactory(new DefaultConnectionEstablisherFactory()),
      mConnMgrDelegate(new DefaultHappyEyeballsConnMgrDelegate()),
      mRetryWithoutTRR(retryWithoutTRR),
      mZeroRttHandle(new ZeroRttHandle(this)) {
  LOG(("HappyEyeballsConnectionAttempt ctor %p retryWithoutTRR=%d", this,
       retryWithoutTRR));
  // mHost is the origin host: the base Happy Eyeballs resolves and falls back
  // to. An alt-svc route's alternate host is passed to Happy Eyeballs as an
  // alt-svc entry (see CreateHappyEyeballs) so it is attempted ahead of, but
  // still falls back to, the origin.
  mHost = mConnInfo->GetOrigin();

  NotifyConnectionActivity(
      mConnInfo, mSpeculative
                     ? NS_HTTP_ACTIVITY_SUBTYPE_SPECULATIVE_DNSANDSOCKET_CREATED
                     : NS_HTTP_ACTIVITY_SUBTYPE_DNSANDSOCKET_CREATED);
}

HappyEyeballsConnectionAttempt::~HappyEyeballsConnectionAttempt() {
  LOG(("HappyEyeballsConnectionAttempt dtor %p", this));
}

// Returns true if the network connectivity service has positively determined
// that IPv6 is unreachable on the current network while IPv4 works. The UNKNOWN
// state (probes still in flight) is intentionally treated as "no signal" so we
// stay optimistic.
static bool ShouldPreferIPv4DueToNoIPv6Connectivity() {
  RefPtr<NetworkConnectivityService> ncs =
      NetworkConnectivityService::GetSingleton();
  if (!ncs) {
    return false;
  }
  return ncs->GetIPv6() == nsINetworkConnectivityService::NOT_AVAILABLE &&
         ncs->GetIPv4() == nsINetworkConnectivityService::OK;
}

nsresult HappyEyeballsConnectionAttempt::CreateHappyEyeballs(
    ConnectionEntry* ent) {
  happy_eyeballs::IpPreference ipPref =
      happy_eyeballs::IpPreference::DualStackPreferV6;
  if (mConnInfo->GetIPv6Disabled()) {
    ipPref = happy_eyeballs::IpPreference::Ipv4Only;
  } else if (ent->PreferenceKnown() && ent->mPreferIPv4) {
    ipPref = happy_eyeballs::IpPreference::DualStackPreferV4;
  } else if (!ent->PreferenceKnown() &&
             ShouldPreferIPv4DueToNoIPv6Connectivity()) {
    ipPref = happy_eyeballs::IpPreference::DualStackPreferV4;
  }

  // Restrict the protocols the Happy Eyeballs engine may attempt to those
  // enabled by prefs, so disabled protocols are never raced (from HTTPS
  // records, IP hints, or alt-svc). NS_HTTP_DISALLOW_HTTP3 is set for
  // transactions that can't use HTTP/3 (e.g. WebSocket upgrades), so honor it
  // here too. An http3Only conn info (eager Alt-Svc h3 validation) must not
  // race h1/h2: the point is to warm an h3 connection, and racing could settle
  // on h2 instead.
  const bool http3Only = mConnInfo->GetHttp3Only();
  happy_eyeballs::HttpVersions httpVersions{
      /* h1 */ !http3Only,
      /* h2 */ !http3Only && StaticPrefs::network_http_http2_enabled(),
      /* h3 */ nsHttpHandler::IsHttp3Enabled() &&
          !(mCaps & NS_HTTP_DISALLOW_HTTP3),
  };

  LOG(("CreateHappyEyeballs ipPref=%d", static_cast<uint32_t>(ipPref)));

  // Happy Eyeballs always resolves and races the origin (mHost / origin port)
  // as the baseline. An alt-svc route is expressed as an alt-svc entry that
  // names the alternate host, port and protocol, so the engine attempts the
  // alternate ahead of the origin but still falls back to the origin if the
  // alternate is unreachable (bug 2052534).
  const uint16_t originPort = static_cast<uint16_t>(mConnInfo->OriginPort());
  const nsCString& routedHost = mConnInfo->GetRoutedHost();

  // Add an alt-svc entry when the connection must race HTTP/3 (an explicit
  // HTTP/3 connection info: an alt-svc HTTP/3 route, or a direct HTTP/3
  // connection such as WebTransport) or when there is an HTTP/2 alt-svc route.
  // A routed host names the alternate; without one (a direct HTTP/3 connection)
  // the entry carries only the protocol and the state machine races the origin
  // over HTTP/3 at the origin port.
  nsTArray<happy_eyeballs::AltSvc> altSvcArray;
  const bool isHttp3 = mConnInfo->IsHttp3();
  if (isHttp3 || !routedHost.IsEmpty()) {
    happy_eyeballs::AltSvc altsvc{};
    altsvc.http_version = isHttp3 ? happy_eyeballs::HttpVersion::H3
                                  : happy_eyeballs::HttpVersion::H2;
    if (!routedHost.IsEmpty()) {
      altsvc.host = routedHost;
      altsvc.port = static_cast<uint16_t>(mConnInfo->RoutedPort());
    }
    altSvcArray.AppendElement(std::move(altsvc));
  }

  return HappyEyeballs::Init(getter_AddRefs(mHappyEyeballs), mHost, originPort,
                             &altSvcArray, ipPref, httpVersions);
}

nsresult HappyEyeballsConnectionAttempt::Init(ConnectionEntry* ent) {
  mEntry = ent;
  nsresult rv = CreateHappyEyeballs(ent);
  if (NS_FAILED(rv)) {
    return rv;
  }
  Transition(State::Connecting);
  return ProcessHappyEyeballsOutput();
}

static Result<NetAddr, nsresult> ToNetAddr(
    const happy_eyeballs::IpAddr& aIpAddr, uint16_t aPort) {
  NetAddr addr;
  memset(&addr, 0, sizeof(NetAddr));

  uint16_t port = htons(aPort);

  switch (aIpAddr.tag) {
    case happy_eyeballs::IpAddr::Tag::V4:
      addr.inet.family = AF_INET;
      addr.inet.port = port;
      memcpy(&addr.inet.ip, aIpAddr.v4._0, 4);
      break;
    case happy_eyeballs::IpAddr::Tag::V6:
      addr.inet6.family = AF_INET6;
      addr.inet6.port = port;
      memcpy(&addr.inet6.ip, aIpAddr.v6._0, 16);
      break;
    default:
      return Err(NS_ERROR_UNEXPECTED);
  }

  return addr;
}

// Inverse of ToNetAddr. The engine takes addresses as happy_eyeballs::IpAddr
// rather than NetAddr, so cbindgen lays the element out for both sides. Every
// caller already knows the family, so there is no error case here.
static happy_eyeballs::IpAddr ToIpAddrV4(const NetAddr& aAddr) {
  MOZ_ASSERT(aAddr.raw.family == AF_INET);
  happy_eyeballs::IpAddr ip{};
  ip.tag = happy_eyeballs::IpAddr::Tag::V4;
  memcpy(ip.v4._0, &aAddr.inet.ip, 4);
  return ip;
}

static happy_eyeballs::IpAddr ToIpAddrV6(const NetAddr& aAddr) {
  MOZ_ASSERT(aAddr.raw.family == AF_INET6);
  happy_eyeballs::IpAddr ip{};
  ip.tag = happy_eyeballs::IpAddr::Tag::V6;
  memcpy(ip.v6._0, &aAddr.inet6.ip, 16);
  return ip;
}

HappyEyeballsConnectionAttempt::ConnResultOutcome
HappyEyeballsConnectionAttempt::ClassifyConnectionResult(
    nsresult aStatus) const {
  if (PossibleZeroRTTRetryError(aStatus)) {
    return ConnResultOutcome::RestartTransaction;
  }
  // NET_RESET after 0-RTT was sent: the server rejected the early data
  // (stale PSK ticket, mismatched QUIC transport params, H3 protocol
  // error, TCP RST on TLS 1.3 early data, …). Restart on the same
  // endpoint without 0-RTT rather than letting Restart()'s default
  // CloneAsDirectRoute() strip the alt-svc / H3 route.
  if (aStatus == NS_ERROR_NET_RESET && mZeroRttHandle->AnyStarted()) {
    return ConnResultOutcome::RestartTransaction;
  }
  // Local Network Access denial: the target IP space is not permitted.
  // No address for this server will succeed; stop all attempts.
  if (aStatus == NS_ERROR_LOCAL_NETWORK_ACCESS_DENIED) {
    return ConnResultOutcome::AbortTransaction;
  }
  // NSS / TLS errors are server-state-specific (cert verification, PSK
  // resumption alert, transport-level alert during handshake, ...).
  // Trying another resolved address won't help — they'll all fail the same way.
  if (NS_ERROR_GET_MODULE(aStatus) == NS_ERROR_MODULE_SECURITY) {
    return ConnResultOutcome::AbortTransaction;
  }
  return ConnResultOutcome::ForwardAndContinue;
}

void HappyEyeballsConnectionAttempt::ReleaseRealTransaction(
    nsresult aCloseReason, ConnectionEntry* aEntry) {
  if (!mTransaction) {
    return;
  }
  // Adopted: the carrier's stream still references the transaction;
  // closing it here would null mConnection mid-flight (bug 2040246).
  // Just drop our ref and let the carrier drive it.
  if (mTransactionAdopted) {
    LOG(
        ("HappyEyeballsConnectionAttempt::ReleaseRealTransaction %p skipping "
         "Close — real transaction already adopted",
         this));
    mTransaction = nullptr;
    return;
  }
  if (nsHttpTransaction* trans = mTransaction->QueryHttpTransaction()) {
    // Another attempt may have won and dispatched this still-pending
    // transaction onto its connection (e.g. via DispatchSpdyPendingQ). Once it
    // has a connection it's no longer ours to close.
    if (trans->Connection()) {
      LOG(
          ("HappyEyeballsConnectionAttempt::ReleaseRealTransaction %p skipping "
           "Close — real transaction already dispatched to a connection",
           this));
      mTransaction = nullptr;
      return;
    }
    if (aEntry) {
      mConnMgrDelegate->RemoveTransFromPendingQ(aEntry, trans);
    }
  }
  mTransaction->Close(aCloseReason);
  // Null out so later paths can't race with the Restart() / AddTransaction
  // that Close just triggered.
  mTransaction = nullptr;
}

nsresult HappyEyeballsConnectionAttempt::ProcessConnectionResult(
    const NetAddr& aAddr, nsresult aStatus, uint64_t aId) {
  LOG(
      ("HappyEyeballsConnectionAttempt::ProcessConnectionResult %p addr=[%s] "
       "id=%" PRIu64 " aStatus=%x",
       this, aAddr.ToString().get(), aId, static_cast<uint32_t>(aStatus)));

  RefPtr<HappyEyeballsConnectionAttempt> self(this);

  // Late establisher results can arrive after we've wound down. No-op.
  if (IsTerminal()) {
    return NS_OK;
  }

  if (mPausedForClientAuth && aId == mClientAuthHolderId) {
    mPausedForClientAuth = false;
    mClientAuthHolderId = 0;
  }

  if (mState != State::ProcessingConnectionResult) {
    Transition(State::ProcessingConnectionResult);
  }

  ConnResultOutcome outcome = ClassifyConnectionResult(aStatus);
  switch (outcome) {
    case ConnResultOutcome::RestartTransaction: {
      TransitionPayload payload;
      payload.mCloseReason = aStatus;
      Transition(State::RestartTransaction, std::move(payload));
      return NS_OK;
    }
    case ConnResultOutcome::AbortTransaction: {
      nsresult closeReason = aStatus;
      if (NS_ERROR_GET_MODULE(aStatus) == NS_ERROR_MODULE_SECURITY) {
        PRErrorCode prCode =
            -static_cast<PRErrorCode>(NS_ERROR_GET_CODE(aStatus));
        if (!mozilla::psm::IsNSSErrorCode(prCode)) {
          // NSPR-base error (e.g. PR_END_OF_FILE_ERROR): translate to the
          // network-module nsresult that nsSocketTransport would have
          // produced.
          closeReason = ErrorAccordingToNSPR(prCode);
        }
      }
      TransitionPayload payload;
      payload.mCloseReason = closeReason;
      Transition(State::AbortTransaction, std::move(payload));
      return NS_OK;
    }
    case ConnResultOutcome::ForwardAndContinue:
      break;
  }

  if (NS_FAILED(aStatus)) {
    mLastConnectionError = aStatus;
  }

  nsresult rv =
      happy_eyeballs_process_connection_result(mHappyEyeballs, aId, aStatus);
  if (NS_FAILED(rv)) {
    LOG(("process_connection_result failed rv=%x", static_cast<uint32_t>(rv)));
  }
  rv = ProcessHappyEyeballsOutput();

  // If drain didn't reach a terminal state, recompute activity state.
  if (mState == State::ProcessingConnectionResult) {
    if (mZeroRttHandle->AnyStarted() && !mZeroRttHandle->HadWinner()) {
      Transition(State::ZeroRttRacing);
    } else {
      Transition(State::Connecting);
    }
  }
  return rv;
}

Maybe<nsCString> HappyEyeballsConnectionAttempt::MaybeExtractRetryEchConfig(
    ConnectionEstablisher* aEstablisher, nsresult aStatus) {
  if (aStatus == psm::GetXPCOMFromNSSError(SSL_ERROR_ECH_RETRY_WITHOUT_ECH)) {
    LOG(
        ("HappyEyeballsConnectionAttempt::MaybeExtractRetryEchConfig %p "
         "SSL_ERROR_ECH_RETRY_WITHOUT_ECH",
         this));
    return Some(nsCString());
  }

  if (aStatus != psm::GetXPCOMFromNSSError(SSL_ERROR_ECH_RETRY_WITH_ECH)) {
    return Nothing();
  }

  RefPtr<HttpConnectionBase> conn =
      aEstablisher ? aEstablisher->ResultConn() : nullptr;
  if (!conn) {
    return Nothing();
  }

  // H1/H2: NSS retry_configs aren't queryable from this callback; use the
  // bytes cached by nsHttpConnection::PostProcessNPNSetup. H3: read from
  // QuicSocketControl, populated by Http3Session::ProcessEvents.
  nsAutoCString retryEchConfig;
  if (RefPtr<nsHttpConnection> httpConn = do_QueryObject(conn)) {
    retryEchConfig = httpConn->CachedRetryEchConfig();
  } else {
    nsCOMPtr<nsITLSSocketControl> tlsCtrl;
    conn->GetTLSSocketControl(getter_AddRefs(tlsCtrl));
    if (tlsCtrl && NS_FAILED(tlsCtrl->GetRetryEchConfig(retryEchConfig))) {
      return Nothing();
    }
  }
  if (retryEchConfig.IsEmpty()) {
    return Nothing();
  }
  LOG(
      ("HappyEyeballsConnectionAttempt::MaybeExtractRetryEchConfig %p "
       "SSL_ERROR_ECH_RETRY_WITH_ECH retryEchConfig.len=%zu",
       this, retryEchConfig.Length()));
  return Some(nsCString(retryEchConfig));
}

nsresult HappyEyeballsConnectionAttempt::ProcessEchRetryConnectionResult(
    const NetAddr& aAddr, uint64_t aId, const nsACString& aEchBytes) {
  LOG(
      ("HappyEyeballsConnectionAttempt::ProcessEchRetryConnectionResult %p "
       "addr=[%s] id=%" PRIu64 " ech.len=%zu",
       this, aAddr.ToString().get(), aId, aEchBytes.Length()));

  RefPtr<HappyEyeballsConnectionAttempt> self(this);

  if (IsTerminal()) {
    return NS_OK;
  }

  if (mState != State::ProcessingConnectionResult) {
    Transition(State::ProcessingConnectionResult);
  }

  nsTArray<uint8_t> echBytes;
  echBytes.AppendElements(
      reinterpret_cast<const uint8_t*>(aEchBytes.BeginReading()),
      aEchBytes.Length());

  nsresult rv =
      happy_eyeballs_process_ech_retry(mHappyEyeballs, aId, &echBytes);
  if (NS_FAILED(rv)) {
    LOG(("process_ech_retry failed rv=%x", static_cast<uint32_t>(rv)));
  }
  rv = ProcessHappyEyeballsOutput();

  if (mState == State::ProcessingConnectionResult) {
    if (mZeroRttHandle->AnyStarted() && !mZeroRttHandle->HadWinner()) {
      Transition(State::ZeroRttRacing);
    } else {
      Transition(State::Connecting);
    }
  }
  return rv;
}

void HappyEyeballsConnectionAttempt::DnsLookupTimings(TimeStamp& aStart,
                                                      TimeStamp& aEnd) const {
  aStart = mFirstDnsLookupStart;
  aEnd = mDnsResolutionEnd.IsNull() ? mFirstConnectionStart : mDnsResolutionEnd;
}

void HappyEyeballsConnectionAttempt::FillConnectTimings(
    bool aIsQuic, TimingStruct& aTimings) const {
  aTimings.connectStart = mFirstConnectionStart;
  aTimings.connectEnd = mFirstConnectEnd;
  if (aIsQuic) {
    // QUIC has no separate TCP handshake; secureConnectionStart coincides
    // with the connection start.
    aTimings.secureConnectionStart = mFirstConnectionStart;
  } else {
    aTimings.tcpConnectEnd = mFirstTcpConnectEnd;
    aTimings.secureConnectionStart = mFirstSecureConnectionStart;
  }
}

nsresult HappyEyeballsConnectionAttempt::ProcessHappyEyeballsOutput() {
  LOG(("HappyEyeballsConnectionAttempt::ProcessHappyEyeballsOutput %p", this));

  if (IsTerminal()) {
    return NS_OK;
  }

  // Paused on a client-cert prompt: stop polling so no new attempts start
  // (running ones continue). Resumed by OnClientAuthCertificateSelected or
  // the holder's result.
  if (mPausedForClientAuth) {
    LOG(("  paused for client-auth (holder id=%" PRIu64 "); not polling",
         mClientAuthHolderId));
    return NS_OK;
  }

  nsresult rv = NS_OK;

  while (!IsTerminal()) {
    happy_eyeballs::Output event{};
    nsTArray<uint8_t> echConfig;
    nsCString dnsHostname;
    rv = happy_eyeballs_process_output(mHappyEyeballs, &event, &echConfig,
                                       &dnsHostname);
    if (NS_FAILED(rv)) {
      LOG(("process_output failed rv=%x", static_cast<uint32_t>(rv)));
      return rv;
    }

    switch (event.tag) {
      case happy_eyeballs::Output::Tag::SendDnsQuery: {
        LOG(("HappyEyeballsEvent::Tag::SendDnsQuery id=%" PRIu64 " hostname=%s",
             event.send_dns_query.id, dnsHostname.get()));
        DNSLookup(event.send_dns_query.record_type,
                  SetupDnsFlags(event.send_dns_query.record_type,
                                event.send_dns_query.allow_stale),
                  event.send_dns_query.id, dnsHostname);
        break;
      }

      case happy_eyeballs::Output::Tag::Timer: {
        SetupTimer(event.timer.duration_ms);
        return NS_OK;
      }

      case happy_eyeballs::Output::Tag::AttemptConnection: {
        LOG(("HappyEyeballsEvent::Tag::AttemptConnection id=%" PRIu64
             " protocol=%d port=%d ",
             event.attempt_connection.id,
             static_cast<uint32_t>(event.attempt_connection.http_version),
             event.attempt_connection.port));

        if (mFirstConnectionStart.IsNull()) {
          mFirstConnectionStart = TimeStamp::Now();
          // If no DNS response arrived before the first connection attempt,
          // report domainLookupEnd as connectStart (matches Chrome's
          // null-dns_resolution_end fallback).
          if (mDnsResolutionEnd.IsNull()) {
            mDnsResolutionEnd = mFirstConnectionStart;
          }
        }

        auto res = ToNetAddr(event.attempt_connection.addr,
                             event.attempt_connection.port);
        if (res.isErr()) {
          LOG(("Failed to convert to NetAddr"));
          // TODO: how to handle this error?
          MOZ_ASSERT(false, "Failed to convert to NetAddr");
          return res.unwrapErr();
        }

        LOG(("connect to:[%s] ech_config_len=%zu",
             res.unwrap().ToString().get(), echConfig.Length()));
        bool isEchRetry = event.attempt_connection.is_ech_retry;

        if (event.attempt_connection.http_version ==
            happy_eyeballs::ConnectionAttemptHttpVersions::H3) {
          EstablishUDPConnection(res.unwrap(), event.attempt_connection.port,
                                 std::move(echConfig),
                                 event.attempt_connection.id, isEchRetry);
        } else {
          EstablishTCPConnection(res.unwrap(), event.attempt_connection.port,
                                 std::move(echConfig),
                                 event.attempt_connection.id, isEchRetry);
        }
        break;
      }

      case happy_eyeballs::Output::Tag::CancelConnection: {
        LOG(("CancelConnection id=%" PRIu64, event.cancel_connection.id));
        CancelConnection(event.cancel_connection.id);
        break;
      }

      case happy_eyeballs::Output::Tag::Succeeded:
        LOG(("happy_eyeballs::Output::Tag::Succeeded"));
        Transition(State::Succeeded);
        return NS_OK;

      case happy_eyeballs::Output::Tag::Failed: {
        LOG(("happy_eyeballs::Output::Tag::Failed reason=%d",
             static_cast<uint32_t>(event.failed.reason)));
        if (ShouldRetryWithoutTRR(event.failed.reason)) {
          RetryWithoutTRR();
          return NS_OK;
        }
        if (ShouldRetryForLocalAddress(event.failed.reason)) {
          RetryForLocalAddress();
          return NS_OK;
        }
        TransitionPayload payload;
        payload.mFailureReason = Some(event.failed.reason);
        Transition(State::Failed, std::move(payload));
        return NS_OK;
      }

      case happy_eyeballs::Output::Tag::None:
        LOG(("happy_eyeballs::Output::Tag::None"));
        // No more events to process
        return NS_OK;
    }
  }

  return NS_OK;
}

Result<nsIDNSService::DNSFlags, nsresult>
HappyEyeballsConnectionAttempt::SetupDnsFlags(
    happy_eyeballs::DnsRecordType aType, bool aAllowStale) {
  LOG(("HappyEyeballsConnectionAttempt::SetupDnsFlags [this=%p aType=%d] ",
       this, static_cast<uint32_t>(aType)));

  nsIDNSService::DNSFlags dnsFlags = nsIDNSService::RESOLVE_DEFAULT_FLAGS;

  if (mCaps & NS_HTTP_REFRESH_DNS) {
    dnsFlags = nsIDNSService::RESOLVE_BYPASS_CACHE;
  }

  // Optimistic DNS: happy-eyeballs sends this query to revalidate an answer it
  // received from a stale (expired) cache entry, so it must not be served from
  // that same stale entry. Bypassing the cache forces a fresh lookup; the stale
  // entry is left in place so concurrent consumers can still use it while the
  // revalidation is in flight.
  if (!aAllowStale) {
    dnsFlags |= nsIDNSService::RESOLVE_BYPASS_CACHE;
  }

  // Fallback attempt after TRR-resolved addresses failed to connect: bypass TRR
  // and the (TRR-populated) cache to re-resolve natively.
  if (mRetryWithoutTRR) {
    dnsFlags |= nsIDNSService::RESOLVE_DISABLE_TRR |
                nsIDNSService::RESOLVE_BYPASS_CACHE |
                nsIDNSService::RESOLVE_REFRESH_CACHE;
  }

  switch (aType) {
    case happy_eyeballs::DnsRecordType::Https:
      if (!mRetryWithoutTRR) {
        dnsFlags |= nsIDNSService::GetFlagsFromTRRMode(mConnInfo->GetTRRMode());
      }
      return dnsFlags;
    case happy_eyeballs::DnsRecordType::Aaaa:
      if (mCaps & NS_HTTP_DISABLE_IPV6) {
        return Err(NS_ERROR_NOT_AVAILABLE);
      }
      dnsFlags |= nsIDNSService::RESOLVE_DISABLE_IPV4;
      break;
    case happy_eyeballs::DnsRecordType::A:
      if (mCaps & NS_HTTP_DISABLE_IPV4) {
        return Err(NS_ERROR_NOT_AVAILABLE);
      }
      dnsFlags |= nsIDNSService::RESOLVE_DISABLE_IPV6;
      break;
  }

  if (!mRetryWithoutTRR) {
    dnsFlags |=
        nsIDNSService::GetFlagsFromTRRMode(NS_HTTP_TRR_MODE_FROM_FLAGS(mCaps));
  }

  // When we get here, we are not resolving using any configured proxy likely
  // because of individual proxy setting on the request or because the host is
  // excluded from proxying.  Hence, force resolution despite global proxy-DNS
  // configuration.
  dnsFlags |= nsIDNSService::RESOLVE_IGNORE_SOCKS_DNS;

  NS_ASSERTION(!(dnsFlags & nsIDNSService::RESOLVE_DISABLE_IPV6) ||
                   !(dnsFlags & nsIDNSService::RESOLVE_DISABLE_IPV4),
               "Setting both RESOLVE_DISABLE_IPV6 and RESOLVE_DISABLE_IPV4");

  LOG(("dnsFlags=%u", dnsFlags));
  return dnsFlags;
}

void HappyEyeballsConnectionAttempt::MaybeSendTransportStatus(
    nsresult aStatus, nsITransport* aTransport, int64_t aProgress) {
  // Capture the first racer to reach each connect milestone across all racers.
  // connectStart (mFirstConnectionStart) and connectEnd (mFirstConnectEnd) are
  // set elsewhere.
  if (aStatus == NS_NET_STATUS_CONNECTED_TO && mFirstTcpConnectEnd.IsNull()) {
    mFirstTcpConnectEnd = TimeStamp::Now();
  } else if (aStatus == NS_NET_STATUS_TLS_HANDSHAKE_STARTING &&
             mFirstSecureConnectionStart.IsNull()) {
    mFirstSecureConnectionStart = TimeStamp::Now();
  }

  if (!mSentTransportStatuses.EnsureInserted(static_cast<uint32_t>(aStatus)) ||
      !mTransaction) {
    return;
  }
  // Skip forwarding to NullTransaction/SpeculativeTransaction. They fire the
  // activity distributor themselves, causing duplicate events. The statuses
  // will be replayed to the real transaction when Claim() replaces it.
  if (mTransaction->IsNullTransaction()) {
    return;
  }
  mTransaction->OnTransportStatus(aTransport, aStatus, aProgress);
}

nsresult HappyEyeballsConnectionAttempt::CheckLNA(
    nsISocketTransport* aTransport) {
  if (!aTransport) {
    return NS_OK;
  }

  NetAddr peerAddr;
  if (NS_FAILED(aTransport->GetPeerAddr(&peerAddr))) {
    return NS_OK;
  }

  return CheckLNAForAddr(peerAddr);
}

nsresult HappyEyeballsConnectionAttempt::CheckLNAForAddr(const NetAddr& aAddr) {
  if (!mConnInfo->FirstHopSSL() || mConnInfo->UsingProxy()) {
    return NS_OK;
  }

  auto addrSpace = aAddr.GetIpAddressSpace();
  // Local targets are always checked pre-TLS. Private targets on HTTPS can
  // be deferred until after the TLS handshake succeeds (see
  // nsHttpConnection::HandshakeDoneInternal) when
  // network.lna.defer_https_check is set.
  // This is in order to prevent unactionable LNA prompts when captive portals
  // temporarily intercept DNS - See bug 2017712.
  bool deferPrivate = addrSpace == nsILoadInfo::IPAddressSpace::Private &&
                      StaticPrefs::network_lna_defer_https_check();
  if (addrSpace != nsILoadInfo::IPAddressSpace::Local &&
      (addrSpace != nsILoadInfo::IPAddressSpace::Private || deferPrivate)) {
    return NS_OK;
  }

  if (mTransaction &&
      !mTransaction->AllowedToConnectToIpAddressSpace(addrSpace)) {
    LOG((
        "HappyEyeballsConnectionAttempt::CheckLNAForAddr %p "
        "blocking connection to %s address space",
        this,
        addrSpace == nsILoadInfo::IPAddressSpace::Local ? "local" : "private"));
    return NS_ERROR_LOCAL_NETWORK_ACCESS_DENIED;
  }

  return NS_OK;
}

void HappyEyeballsConnectionAttempt::DNSLookup(
    happy_eyeballs::DnsRecordType aType,
    Result<nsIDNSService::DNSFlags, nsresult> aFlags, uint64_t aId,
    const nsACString& aHostname) {
  // Track A/AAAA lookups for the origin host (mHost) so the coalescing keys are
  // built from their combined addresses once both finish. Lookups for an HTTPS
  // RR target name could use a different hostname and are intentionally
  // excluded. The id is removed in OnARecord/OnAAAARecord, which run on both
  // the success and failure paths.
  if ((aType == happy_eyeballs::DnsRecordType::A ||
       aType == happy_eyeballs::DnsRecordType::Aaaa) &&
      aHostname.Equals(mHost)) {
    mOriginDnsLookupIds.Insert(aId);
  }

  nsCOMPtr<nsIDNSService> dns = aFlags.isOk() ? GetOrInitDNSService() : nullptr;

  if (dns) {
    if (mFirstDnsLookupStart.IsNull()) {
      mFirstDnsLookupStart = TimeStamp::Now();
    }
    MaybeSendTransportStatus(NS_NET_STATUS_RESOLVING_HOST);
  }

  RefPtr<DnsRequestInfo> requestInfo = new DnsRequestInfo(aId, aType);
  nsCOMPtr<nsICancelable> request;
  nsresult rv = NS_ERROR_UNEXPECTED;
  if (dns) {
    nsIDNSService::DNSFlags flags = aFlags.unwrap();
    switch (aType) {
      case happy_eyeballs::DnsRecordType::Https: {
        // Skip the HTTPS RR lookup in two cases:
        //   1. Plain-HTTP origin (!FirstHopSSL). HTTPS RR is an HTTPS-upgrade
        //      mechanism; nsHttpChannel normally does the upgrade before
        //      creating connection, but some edge cases slip through with an
        //      http:// conn info.
        //   2. NS_HTTP_DISALLOW_HTTPS_RR is set.
        // Setting rv = NS_ERROR_NOT_AVAILABLE feeds an empty HTTPS RR to the
        // state machine instead of issuing a query.
        if (!mConnInfo->FirstHopSSL() ||
            (!StaticPrefs::network_dns_force_use_https_rr() &&
             (mCaps & NS_HTTP_DISALLOW_HTTPS_RR))) {
          rv = NS_ERROR_NOT_AVAILABLE;
        } else {
          nsCOMPtr<nsIDNSAdditionalInfo> info;
          if (mConnInfo->OriginPort() != NS_HTTPS_DEFAULT_PORT) {
            dns->NewAdditionalInfo(""_ns, mConnInfo->OriginPort(),
                                   getter_AddRefs(info));
          }
          rv = dns->AsyncResolveNative(
              aHostname, nsIDNSService::RESOLVE_TYPE_HTTPSSVC,
              flags | nsIDNSService::RESOLVE_WANT_RECORD_ON_ERROR, info, this,
              gSocketTransportService, mConnInfo->GetOriginAttributes(),
              getter_AddRefs(request));
        }
        break;
      }
      case happy_eyeballs::DnsRecordType::Aaaa:
        rv = dns->AsyncResolveNative(
            aHostname, nsIDNSService::RESOLVE_TYPE_DEFAULT,
            flags | nsIDNSService::RESOLVE_WANT_RECORD_ON_ERROR, nullptr, this,
            gSocketTransportService, mConnInfo->GetOriginAttributes(),
            getter_AddRefs(request));
        break;
      case happy_eyeballs::DnsRecordType::A:
        rv = dns->AsyncResolveNative(
            aHostname, nsIDNSService::RESOLVE_TYPE_DEFAULT,
            flags | nsIDNSService::RESOLVE_WANT_RECORD_ON_ERROR, nullptr, this,
            gSocketTransportService, mConnInfo->GetOriginAttributes(),
            getter_AddRefs(request));
        break;
    }
  }

  if (NS_SUCCEEDED(rv) && request) {
    requestInfo->SetRequest(request);
    mDnsRequestTable.InsertOrUpdate(request, requestInfo);
    return;
  }

  // Notify the state machine about DNS failure asynchronously.
  DispatchToCurrent(
      NS_NewRunnableFunction("HappyEyeballsConnectionAttempt::DNSLookup",
                             [self = RefPtr{this}, rv, aType, aId]() {
                               switch (aType) {
                                 case happy_eyeballs::DnsRecordType::Https:
                                   (void)self->OnHTTPSRecord(nullptr, rv, aId);
                                   break;
                                 case happy_eyeballs::DnsRecordType::Aaaa:
                                   (void)self->OnAAAARecord(nullptr, rv, aId);
                                   break;
                                 case happy_eyeballs::DnsRecordType::A:
                                   (void)self->OnARecord(nullptr, rv, aId);
                                   break;
                               }
                             }));
}

void HappyEyeballsConnectionAttempt::MaybeForward0RTTSecurityInfo(
    ConnectionEstablisher* aEstablisher) {
  if (!mZeroRttHandle->AnyStarted()) {
    return;
  }
  RefPtr<HttpConnectionBase> conn = aEstablisher->ResultConn();
  if (!conn) {
    return;
  }
  nsCOMPtr<nsITLSSocketControl> tlsCtrl;
  conn->GetTLSSocketControl(getter_AddRefs(tlsCtrl));
  nsCOMPtr<nsITransportSecurityInfo> secInfo;
  if (tlsCtrl) {
    tlsCtrl->GetSecurityInfo(getter_AddRefs(secInfo));
  }
  if (secInfo && mTransaction) {
    if (nsHttpTransaction* trans = mTransaction->QueryHttpTransaction()) {
      trans->SetSecurityInfo(secInfo);
    }
  }
}

void HappyEyeballsConnectionAttempt::HandleConnectionResult(
    Result<RefPtr<HttpConnectionBase>, nsresult> aResult,
    ConnectionEstablisher* aEstablisher, uint64_t aId) {
  RefPtr<ConnectionEstablisher> establisher = aEstablisher;
  mConnectionEstablisherTable.Remove(aId);
  NetAddr addr = establisher->Addr();

  // A local establisher refuses synchronously in Start() and never reaches
  // here, so this means an establisher actually attempted a connection.
  mAllAttemptsRefusedLocal = false;

  LOG((
      "HappyEyeballsConnectionAttempt::HandleConnectionResult %p addr=[%s] "
      "family=[%d] id=%" PRIu64 " isUDP=%d",
      this, addr.ToString().get(), addr.raw.family, aId, establisher->IsUDP()));

  if (aResult.isErr()) {
    nsresult status = aResult.unwrapErr();
    MaybeForward0RTTSecurityInfo(establisher);
    Maybe<nsCString> retryEch = MaybeExtractRetryEchConfig(establisher, status);
    establisher->Close(status);
    if (retryEch) {
      ProcessEchRetryConnectionResult(addr, aId, *retryEch);
    } else {
      ProcessConnectionResult(addr, status, aId);
    }
    return;
  }

  if (IsTerminal()) {
    establisher->Close(NS_BASE_STREAM_CLOSED);
    ProcessConnectionResult(addr, NS_BASE_STREAM_CLOSED, aId);
    return;
  }

  mOutputConn = aResult.unwrap();
  mOutputTrans = establisher->Transaction();
  mOutputConnId = aId;
  mAddrFamily = addr.raw.family;
  // The winner is the first connection to fully succeed.
  mFirstConnectEnd = TimeStamp::Now();
  // The ownership of connection is moved to HappyEyeballsConnectionAttempt now.
  establisher->ClearResultConnection();

  ProcessConnectionResult(addr, NS_OK, aId);
}

void HappyEyeballsConnectionAttempt::AdoptWinner(
    HappyEyeballsTransaction* aWinner) {
  MOZ_ASSERT(OnSocketThread());
  if (!aWinner || aWinner->IsAdopted()) {
    return;
  }

  nsHttpTransaction* realTransaction = RealHttpTransaction();
  if (!realTransaction) {
    LOG(
        ("HappyEyeballsConnectionAttempt::AdoptWinner %p no real transaction; "
         "closing winner=%p",
         this, aWinner));
    aWinner->Close(NS_ERROR_ABORT);
    return;
  }

  // The trans must have been removed from the pending queue by
  // LockInRealTransactionFromPendingQueue at Do0RTT time.
#ifdef DEBUG
  {
    RefPtr<ConnectionEntry> entry(mEntry);
    if (entry) {
      RefPtr<PendingTransactionInfo> pendingInfo =
          mConnMgrDelegate->FindTransaction(
              /*removeWhenFound*/ false, entry, realTransaction);
      MOZ_ASSERT(
          !pendingInfo,
          "real transaction must have been removed from the pending queue "
          "by LockInRealTransactionFromPendingQueue");
    }
  }
#endif
  aWinner->Adopt(realTransaction);
  // Real transaction now lives on the carrier's stream — disarm
  // ReleaseRealTransaction for the rest of this HCA's lifetime (bug 2040246).
  mTransactionAdopted = true;
}

bool HappyEyeballsConnectionAttempt::LockInRealTransactionFromPendingQueue() {
  nsHttpTransaction* realTransaction = RealHttpTransaction();
  if (!realTransaction) {
    return false;
  }
  RefPtr<ConnectionEntry> entry(mEntry);
  if (!entry) {
    return false;
  }
  RefPtr<PendingTransactionInfo> pendingInfo =
      mConnMgrDelegate->FindTransaction(
          /*removeWhenFound*/ true, entry, realTransaction);
  LOG(
      ("HappyEyeballsConnectionAttempt::LockInRealTransactionFromPendingQueue "
       "%p realTransaction=%p removed=%d",
       this, realTransaction, !!pendingInfo));
  return !!pendingInfo;
}

already_AddRefed<HappyEyeballsTransaction>
HappyEyeballsConnectionAttempt::CreateAttemptTransaction(
    nsHttpConnectionInfo* aInfo, uint64_t aEstablisherId) {
  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  uint64_t browserId = 0;
  if (mTransaction) {
    mTransaction->GetSecurityCallbacks(getter_AddRefs(callbacks));
    browserId = mTransaction->BrowserId();
  }
  RefPtr<HappyEyeballsTransaction> trans = new HappyEyeballsTransaction(
      aInfo, callbacks, mCaps, browserId,
      [self = RefPtr{this}](nsITransport* t, nsresult s, int64_t p) {
        self->MaybeSendTransportStatus(s, t, p);
      },
      [self = RefPtr{this}, id = aEstablisherId]() {
        self->OnClientAuthCertificateRequested(id);
      },
      [self = RefPtr{this}, id = aEstablisherId]() {
        self->OnClientAuthCertificateSelected(id);
      },
      mZeroRttHandle);
  return trans.forget();
}

nsresult HappyEyeballsConnectionAttempt::EstablishTCPConnection(
    NetAddr aAddr, uint16_t aPort, nsTArray<uint8_t>&& aEchConfig, uint64_t aId,
    bool aIsEchRetry) {
  // TODO: we always use happy_eyeballs::ConnectionAttemptHttpVersions::H2OrH1
  // for now. Do we really want to race H2 and H1?
  RefPtr<nsHttpConnectionInfo> info = mConnInfo->CloneAndAdoptPortAndAlpn(
      aPort, happy_eyeballs::ConnectionAttemptHttpVersions::H2OrH1);
  if (!aEchConfig.IsEmpty()) {
    info = info->Mutate()
               .SetEchConfig(nsCString((const char*)aEchConfig.Elements(),
                                       aEchConfig.Length()))
               .Finalize();
    NotifyConnectionActivity(info, NS_HTTP_ACTIVITY_SUBTYPE_ECH_SET);
  }
  NotifyConnectionActivity(info, NS_HTTP_ACTIVITY_SUBTYPE_CONNECTION_CREATED);
  uint32_t caps = mCaps | (aIsEchRetry ? NS_HTTP_IS_RETRY : 0);
  RefPtr<ConnectionEstablisher> establisher =
      mEstablisherFactory->Create(ConnectionEstablisherType::TCP, info, aAddr,
                                  caps, mSpeculative, mAllow1918);
  establisher->SetDnsMetadata(mDnsMetadata);
  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  mTransaction->GetSecurityCallbacks(getter_AddRefs(callbacks));
  establisher->SetSecurityCallbacks(callbacks);
  establisher->SetTransportStatusCallback(
      [self = RefPtr{this}](nsITransport* trans, nsresult status,
                            int64_t progress) {
        self->MaybeSendTransportStatus(status, trans, progress);
      });
  establisher->SetLnaCheckCallback(
      [self = RefPtr{this}](nsISocketTransport* aTransport) -> nsresult {
        return self->CheckLNA(aTransport);
      });

  RefPtr<HappyEyeballsTransaction> attempt =
      CreateAttemptTransaction(info, aId);
  establisher->SetTransaction(attempt);

  auto callback = [self = RefPtr{this}, establisher,
                   aId](Result<RefPtr<HttpConnectionBase>, nsresult> aResult) {
    self->HandleConnectionResult(std::move(aResult), establisher, aId);
  };

  if (establisher->Start(std::move(callback))) {
    mConnectionEstablisherTable.InsertOrUpdate(aId, std::move(establisher));
  } else {
    // Start() refused synchronously. A speculative attempt refuses local
    // (RFC1918) peers; surface it as a refusal and remember it so a later Claim
    // can retry with local addresses allowed.
    nsresult reason = NS_ERROR_FAILURE;
    if (establisher->RefusedForLocalAddress()) {
      mLocalAddrRefused = true;
      reason = NS_ERROR_CONNECTION_REFUSED;
    } else {
      mAllAttemptsRefusedLocal = false;
    }
    ProcessConnectionResult(aAddr, reason, aId);
  }

  return NS_OK;
}

nsresult HappyEyeballsConnectionAttempt::EstablishUDPConnection(
    NetAddr aAddr, uint16_t aPort, nsTArray<uint8_t>&& aEchConfig, uint64_t aId,
    bool aIsEchRetry) {
  RefPtr<nsHttpConnectionInfo> info = mConnInfo->CloneAndAdoptPortAndAlpn(
      aPort, happy_eyeballs::ConnectionAttemptHttpVersions::H3);
  if (!aEchConfig.IsEmpty()) {
    info = info->Mutate()
               .SetEchConfig(nsCString((const char*)aEchConfig.Elements(),
                                       aEchConfig.Length()))
               .Finalize();
    NotifyConnectionActivity(info, NS_HTTP_ACTIVITY_SUBTYPE_ECH_SET);
  }
  NotifyConnectionActivity(info, NS_HTTP_ACTIVITY_SUBTYPE_CONNECTION_CREATED);
  uint32_t caps = mCaps | (aIsEchRetry ? NS_HTTP_IS_RETRY : 0);
  RefPtr<ConnectionEstablisher> establisher =
      mEstablisherFactory->Create(ConnectionEstablisherType::UDP, info, aAddr,
                                  caps, mSpeculative, mAllow1918);
  establisher->SetDnsMetadata(mDnsMetadata);
  establisher->SetTransportStatusCallback(
      [self = RefPtr{this}](nsITransport* trans, nsresult status,
                            int64_t progress) {
        self->MaybeSendTransportStatus(status, trans, progress);
      });

  RefPtr<HappyEyeballsTransaction> attempt =
      CreateAttemptTransaction(info, aId);
  establisher->SetTransaction(attempt);

  auto callback = [self = RefPtr{this}, establisher,
                   aId](Result<RefPtr<HttpConnectionBase>, nsresult> aResult) {
    self->HandleConnectionResult(std::move(aResult), establisher, aId);
  };

  if (establisher->Start(std::move(callback))) {
    mConnectionEstablisherTable.InsertOrUpdate(aId, std::move(establisher));
  } else {
    // Start() refused synchronously. A speculative attempt refuses local
    // (RFC1918) peers; surface it as a refusal and remember it so a later Claim
    // can retry with local addresses allowed.
    nsresult reason = NS_ERROR_FAILURE;
    if (establisher->RefusedForLocalAddress()) {
      mLocalAddrRefused = true;
      reason = NS_ERROR_CONNECTION_REFUSED;
    } else {
      mAllAttemptsRefusedLocal = false;
    }
    ProcessConnectionResult(aAddr, reason, aId);
  }

  return NS_OK;
}

void HappyEyeballsConnectionAttempt::OnClientAuthCertificateRequested(
    uint64_t aEstablisherId) {
  LOG(
      ("HappyEyeballsConnectionAttempt::OnClientAuthCertificateRequested %p "
       "id=%" PRIu64,
       this, aEstablisherId));

  if (IsTerminal()) {
    return;
  }

  if (mPausedForClientAuth) {
    return;
  }

  mPausedForClientAuth = true;
  mClientAuthHolderId = aEstablisherId;
}

void HappyEyeballsConnectionAttempt::OnClientAuthCertificateSelected(
    uint64_t aEstablisherId) {
  LOG(
      ("HappyEyeballsConnectionAttempt::OnClientAuthCertificateSelected %p "
       "id=%" PRIu64,
       this, aEstablisherId));
  if (!mPausedForClientAuth || aEstablisherId != mClientAuthHolderId) {
    return;
  }

  mPausedForClientAuth = false;
  mClientAuthHolderId = 0;
}

void HappyEyeballsConnectionAttempt::CancelConnection(uint64_t aId) {
  LOG(("HappyEyeballsConnectionAttempt::CancelConnection id=%" PRIu64, aId));

  RefPtr<ConnectionEstablisher> conn = mConnectionEstablisherTable.Get(aId);
  if (conn) {
    conn->Close(NS_ERROR_ABORT);
    mConnectionEstablisherTable.Remove(aId);
  } else {
    LOG(("No matching connection found for id=%" PRIu64, aId));
  }
}

void HappyEyeballsConnectionAttempt::CloseHttpTransaction(
    happy_eyeballs::FailureReason aReason, ConnectionEntry* aEntry) {
  LOG(("HappyEyeballsConnectionAttempt::CloseHttpTransaction %p reason=%d",
       this, static_cast<uint32_t>(aReason)));

  nsresult reason = NS_ERROR_ABORT;
  switch (aReason) {
    case happy_eyeballs::FailureReason::DnsResolution:
      reason = NS_FAILED(mLastDnsError) ? mLastDnsError : NS_ERROR_UNKNOWN_HOST;
      break;
    case happy_eyeballs::FailureReason::Connection:
      reason = (NS_FAILED(mLastConnectionError) &&
                mLastConnectionError != NS_ERROR_NET_RESET)
                   ? mLastConnectionError
                   : NS_ERROR_CONNECTION_REFUSED;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown FailureReason");
      break;
  }
  // Defensive: Failed implies no winner, so should never be adopted.
  // Route through ReleaseRealTransaction to keep the invariant uniform.
  ReleaseRealTransaction(reason, aEntry);
}

bool HappyEyeballsConnectionAttempt::ShouldRetryWithoutTRR(
    happy_eyeballs::FailureReason aReason) const {
  if (!StaticPrefs::network_http_happy_eyeballs_retry_without_trr()) {
    return false;
  }

  // Only when TRR-resolved addresses all failed to *connect* (not a pure DNS
  // failure), for a real transaction, outside TRR_ONLY mode, and only once (a
  // TRR-disabled retry attempt must not retry again).
  if (aReason != happy_eyeballs::FailureReason::Connection ||
      mRetryWithoutTRR || !mDnsMetadata.mIsTRR ||
      mDnsMetadata.mEffectiveTRRMode == nsIRequest::TRR_ONLY_MODE ||
      !RealHttpTransaction()) {
    return false;
  }

  // Don't fall back when TRR intentionally returned 0.0.0.0 / :: (a blocked
  // answer), unless the pref opts in. Matches
  // nsSocketTransport::RecoverFromError.
  if (!StaticPrefs::network_trr_fallback_on_zero_response() &&
      !mOriginAddresses.IsEmpty()) {
    bool allZero = true;
    for (const auto& addr : mOriginAddresses) {
      bool isZero = (addr.raw.family == AF_INET && addr.inet.ip == 0) ||
                    (addr.raw.family == AF_INET6 && addr.inet6.ip.u64[0] == 0 &&
                     addr.inet6.ip.u64[1] == 0);
      if (!isZero) {
        allZero = false;
        break;
      }
    }
    if (allZero) {
      return false;
    }
  }

  return true;
}

void HappyEyeballsConnectionAttempt::DoRetry(bool aRetryWithoutTRR,
                                             const char* aLogTag) {
  RefPtr<HappyEyeballsConnectionAttempt> self(this);
  RefPtr<ConnectionEntry> entry(mEntry);
  RefPtr<nsHttpTransaction> realTrans = RealHttpTransaction();
  LOG(("HappyEyeballsConnectionAttempt::%s %p trans=%p", aLogTag, this,
       realTrans.get()));
  MOZ_ASSERT(entry && realTrans);

  nsresult rv =
      mConnMgrDelegate->StartRetry(entry, realTrans, mCaps, mSpeculative,
                                   mUrgentStart, mAllow1918, aRetryWithoutTRR);
  if (NS_FAILED(rv)) {
    LOG(("  %s: StartRetry failed rv=%" PRIx32 "; failing normally", aLogTag,
         static_cast<uint32_t>(rv)));
    TransitionPayload payload;
    payload.mFailureReason = Some(happy_eyeballs::FailureReason::Connection);
    Transition(State::Failed, std::move(payload));
    return;
  }

  // The new attempt owns the transaction now. Detach so this attempt's teardown
  // can't close or re-queue it, then remove ourselves and abandon.
  ForgetRealTransaction();
  mConnMgrDelegate->RemoveConnectionAttempt(entry, this, /* abandon = */ true);
}

void HappyEyeballsConnectionAttempt::RetryWithoutTRR() {
  // Start a fresh attempt that re-resolves this transaction with TRR disabled.
  DoRetry(/* aRetryWithoutTRR = */ true, "RetryWithoutTRR");
}

bool HappyEyeballsConnectionAttempt::ShouldRetryForLocalAddress(
    happy_eyeballs::FailureReason aReason) const {
  // A speculative attempt must not open a connection to a local (RFC1918) peer:
  // that would let a remote origin use a preconnect/speculative connection to
  // probe the user's local network before any real navigation (Local Network
  // Access). So while speculative the establisher refuses local peers outright
  // rather than "just connecting" -- there is no safe way to speculatively
  // reach a local address. Claim() lifts that restriction (mAllow1918 becomes
  // true) once a real transaction takes over, but an establisher may already
  // have refused the peer before the claim raced in, leaving the attempt
  // terminally failed. This retry recovers that case.
  //
  // Retry only when every attempt was a local-peer refusal and the attempt has
  // since been claimed (mAllow1918 true, so a non-speculative attempt never
  // qualifies) with a real transaction to reconnect. The fresh retry allows
  // local addresses, so it can't refuse again or loop.
  return aReason == happy_eyeballs::FailureReason::Connection &&
         mLocalAddrRefused && mAllAttemptsRefusedLocal && mAllow1918 &&
         RealHttpTransaction();
}

void HappyEyeballsConnectionAttempt::RetryForLocalAddress() {
  // Start a fresh attempt for this transaction; mAllow1918 is now true, so the
  // establisher no longer refuses the local peer.
  DoRetry(/* aRetryWithoutTRR = */ false, "RetryForLocalAddress");
}

void HappyEyeballsConnectionAttempt::Abandon() {
  LOG(("HappyEyeballsConnectionAttempt::Abandon %p", this));
  // Route every path (external + outcome entry actions) through Done.
  // Idempotent — repeat calls are no-ops.
  if (mState == State::Done) {
    return;
  }
  Transition(State::Done);
}

void HappyEyeballsConnectionAttempt::ProcessTCPConn(
    HttpConnectionBase* aConn, ConnectionEntry* aEntry,
    bool aTransactionAlreadyOnConn) {
  RefPtr<ConnectionEntry> entry(mEntry);
  if (!entry) {
    return;
  }

  RefPtr<HttpConnectionBase> connTCP = aConn;
  LOG(("Got connTCP:%p transactionAlreadyOnConn=%d", connTCP.get(),
       aTransactionAlreadyOnConn));

  mConnMgrDelegate->InsertIntoActiveConns(entry, connTCP);

  bool isHttp2 = connTCP->UsingSpdy();

  nsHttpTransaction* realTrans =
      mTransaction ? mTransaction->QueryHttpTransaction() : nullptr;
  // WebSocket / WebTransport upgrades on an HTTP/2 connection must be
  // dispatched through the extended CONNECT tunnel (Http2StreamTunnel)
  // instead of being activated directly on the Http2Session.
  bool deferExtendedConnect =
      isHttp2 && realTrans &&
      (realTrans->IsWebsocketUpgrade() || realTrans->IsForWebTransport());

  if (!aTransactionAlreadyOnConn && deferExtendedConnect) {
    LOG(
        ("ProcessTCPConn deferring extended CONNECT upgrade trans=%p to "
         "ProcessPendingQ\n",
         realTrans));

    // Another racing attempt may have already dispatched this transaction onto
    // a connection (bug 2048392). It's no longer ours to re-queue; doing so
    // would re-dispatch onto its now transport-less connection.
    if (realTrans->Connection()) {
      LOG(
          ("ProcessTCPConn trans=%p already dispatched to a connection; not "
           "re-queuing\n",
           realTrans));
    } else {
      RefPtr<PendingTransactionInfo> existing =
          mConnMgrDelegate->FindTransaction(
              /* removeWhenFound = */ false, entry, realTrans);
      if (!existing) {
        mConnMgrDelegate->AddTransaction(realTrans, realTrans->Priority());
      }
    }
    mTransaction = nullptr;
  } else if (!aTransactionAlreadyOnConn) {
    RefPtr<PendingTransactionInfo> pendingTransInfo =
        mConnMgrDelegate->FindTransaction(true, entry, mTransaction);
    if (pendingTransInfo) {
      MOZ_ASSERT(!mSpeculative, "Speculative HE attempt found mTransaction");
      nsresult rv = mConnMgrDelegate->DispatchTransaction(
          entry, pendingTransInfo->Transaction(), connTCP);
      if (NS_FAILED(rv)) {
        mTransaction->Close(rv);
      } else {
        // Real transaction now on connTCP — disarm ReleaseRealTransaction.
        mTransactionAdopted = true;
      }
    } else if (!isHttp2) {
      // After about 1 second allow for the possibility of restarting a
      // transaction due to server close. Keep at sub 1 second as that is the
      // minimum granularity we can expect a server to be timing out with.
      if (RefPtr<nsHttpConnection> h1 = do_QueryObject(connTCP)) {
        h1->SetIsReusedAfter(950);
      }

      LOG(
          ("ProcessTCPConn no transaction match "
           "returning conn %p to pool\n",
           connTCP.get()));
      mConnMgrDelegate->ReclaimConnection(connTCP);
    }
  }

  connTCP->SetIsRacing(false);
  mConnMgrDelegate->ReportSpdyConnection(
      connTCP, isHttp2, isHttp2 && (mCaps & NS_HTTP_DISALLOW_HTTP3));
}

void HappyEyeballsConnectionAttempt::ProcessUDPConn(
    HttpConnectionBase* aConn, ConnectionEntry* aEntry,
    bool aTransactionAlreadyOnConn) {
  RefPtr<ConnectionEntry> entry(mEntry);
  if (!entry) {
    return;
  }

  LOG(("Got connUDP:%p transactionAlreadyOnConn=%d", aConn,
       aTransactionAlreadyOnConn));

  mConnMgrDelegate->InsertIntoActiveConns(entry, aConn);

  if (!aTransactionAlreadyOnConn) {
    RefPtr<PendingTransactionInfo> pendingTransInfo =
        mConnMgrDelegate->FindTransaction(true, entry, mTransaction);
    nsresult rv = NS_OK;
    if (pendingTransInfo) {
      MOZ_ASSERT(!mSpeculative, "Speculative HE attempt found mTransaction");
      rv = mConnMgrDelegate->DispatchTransaction(
          entry, pendingTransInfo->Transaction(), aConn);
      if (NS_FAILED(rv)) {
        mTransaction->Close(rv);
      } else {
        // Real transaction now on aConn — see ProcessTCPConn.
        mTransactionAdopted = true;
      }
    } else {
      nsHttpTransaction* trans = mTransaction->QueryHttpTransaction();
      if (trans && trans->IsDone()) {
        LOG(("ProcessUDPConn transaction already done, not activating"));
      } else if (trans && trans->Connection()) {
        // Another racing attempt already dispatched this transaction onto a
        // connection (cf. bug 2048392 / bug 2048294); don't re-activate it.
        LOG(
            ("ProcessUDPConn transaction already dispatched to a connection, "
             "not activating"));
      } else {
        rv = aConn->Activate(mTransaction, mCaps, 0);
        if (NS_SUCCEEDED(rv)) {
          mTransactionAdopted = true;
        }
      }
    }
  }

  // An h3-only attempt (eager Alt-Svc h3 validation) deliberately runs in its
  // own connection entry so the speculative, TCP-less race cannot be claimed
  // by normal transactions to the origin. What it produces is an ordinary h3
  // connection to the origin though, so once it is established hand it over to
  // the origin's entry.
  RefPtr<ConnectionEntry> reportEntry = entry;
  if (entry->mConnInfo->GetHttp3Only()) {
    if (RefPtr<ConnectionEntry> originEntry =
            mConnMgrDelegate->HandOffHttp3OnlyConnection(aConn, entry)) {
      reportEntry = originEntry;
    }
  }

  aConn->SetIsRacing(false);
  mConnMgrDelegate->ReportHttp3Connection(aConn, reportEntry);
}

void HappyEyeballsConnectionAttempt::EnterSucceeded() {
  LOG(("HappyEyeballsConnectionAttempt::EnterSucceeded %p", this));
  MOZ_ASSERT(mState == State::Succeeded);

  RefPtr<HappyEyeballsConnectionAttempt> self(this);
  RefPtr<ConnectionEntry> entry(mEntry);
  MOZ_ASSERT(entry);

  // RecordIPFamilyPreference is sticky (set-once) and can't flip an existing
  // preference. If the winning family contradicts the entry's learned
  // preference (e.g. the preferred family has since gone dead and the other
  // family won), reset first so the winner becomes the new preference instead
  // of being ignored -- otherwise the stale preference persists and every
  // subsequent connection keeps giving the dead family a head start.
  if ((mAddrFamily == AF_INET && entry->mPreferIPv6) ||
      (mAddrFamily == AF_INET6 && entry->mPreferIPv4)) {
    mConnMgrDelegate->ResetIPFamilyPreference(entry);
  }
  mConnMgrDelegate->RecordIPFamilyPreference(entry, mAddrFamily);

  TimeStamp dnsLookupStart, dnsLookupEnd;
  DnsLookupTimings(dnsLookupStart, dnsLookupEnd);
  if (!dnsLookupStart.IsNull()) {
    mOutputConn->SetDnsBootstrapTimings(dnsLookupStart, dnsLookupEnd);
  }
  // Record the first-racer connect spans (rather than the winning attempt's own
  // collected timings) on the connection: it outlives this attempt and hands
  // them to the transaction it gets activated with.
  if (!mFirstConnectionStart.IsNull()) {
    TimingStruct connectTimings;
    FillConnectTimings(/* aIsQuic = */ mOutputConn->UsingHttp3(),
                       connectTimings);
    mOutputConn->SetConnectBootstrapTimings(
        connectTimings.connectStart, connectTimings.tcpConnectEnd,
        connectTimings.secureConnectionStart, connectTimings.connectEnd);
  }
  mOutputTrans = nullptr;

  // Fallback for the case where ShouldDisqualify didn't fire. A racer that did
  // 0-RTT advanced the real transaction's request stream; its flags are
  // half-set and the winner isn't a 0-RTT racer. Tell the real transaction the
  // 0-RTT attempt was effectively rejected — FinishAdopted0RTT(restart=true)
  // rewinds the stream to 0 and marks mDoNotTryEarlyData /
  // mEarlyDataWasAvailable so the real transaction re-sends a fresh request on
  // the winning conn.
  bool restartedFallback0Rtt = false;
  nsHttpTransaction* trans =
      mTransaction ? mTransaction->QueryHttpTransaction() : nullptr;
  if (mZeroRttHandle->AnyStarted() && !mZeroRttHandle->HadWinner()) {
    if (!mTransaction) {
      // ReleaseRealTransaction already restarted the real transaction via
      // Close()/Restart().  The winning connection goes into the pool below
      // and the CM will dispatch the restarted transaction on it.
    } else {
      // AnyStarted() is set only after LockInRealTransactionFromPendingQueue()
      // succeeds, which requires QueryHttpTransaction() to return non-null.
      // So trans is always non-null here when mTransaction is non-null.
      MOZ_ASSERT(trans,
                 "AnyStarted implies a live real transaction; "
                 "QueryHttpTransaction() should not be null");
      if (trans) {
        trans->FinishAdopted0RTT(/*aRestart=*/true);
        // LockInRealTransactionFromPendingQueue removed the real transaction
        // from the pending queue when 0-RTT was entered. Re-queue it so the
        // conn manager can dispatch it on the winning conn or open a new
        // connection. Guard against double-queuing (which would trip
        // CheckTransInPendingQueue's assertion in AddTransaction) by checking
        // first.
        // If another racing attempt already dispatched this transaction onto a
        // connection it's no longer ours to re-queue (cf. bug 2048392 /
        // bug 2048294).
        if (!trans->Connection()) {
          RefPtr<PendingTransactionInfo> existing;
          if (entry) {
            existing = mConnMgrDelegate->FindTransaction(
                /*removeWhenFound=*/false, entry, trans);
          }
          if (!existing) {
            mConnMgrDelegate->AddTransaction(trans, trans->Priority());
          }
        }
        restartedFallback0Rtt = true;
        mTransaction = nullptr;
      }
    }
  }

  MOZ_DIAGNOSTIC_ASSERT(
      !mZeroRttHandle->AnyStarted() || mZeroRttHandle->HadWinner() ||
          !mTransaction,
      "EnterSucceeded: 0-RTT transaction not re-queued and not adopted");

  // Adopted: real transaction is on the conn and already out of the pending
  // queue. Skip ProcessTCPConn's pending-queue branch — on H1 it
  // would otherwise reclaim the live conn to the idle pool; on H2/H3
  // it's a no-op.
  // Also skip FindTransactionHelper for the fallback restart case: the
  // re-inserted trans will be dispatched by ReportSpdyConnection →
  // ProcessPendingQ once the conn is in the active pool.
  bool alreadyOnConn = mZeroRttHandle->HadWinner() || restartedFallback0Rtt;

  // A transaction that adopted 0-RTT on mOutputConn is already running there,
  // so Activate has been and gone: hand it the connect phase now. Any other
  // transaction is either about to be activated on mOutputConn, which hands it
  // over, or was dispatched onto a different connection while we were
  // connecting -- this attempt's spans would then describe a connect phase that
  // never happened for it (bug 2046698).
  if (alreadyOnConn && mTransaction) {
    mOutputConn->HandOffConnectPhase(mTransaction);
  }

  if (!mOutputConn->UsingHttp3()) {
    // If the original request had an alt-svc route but a direct TCP
    // connection won, remove the Alt-Used header since we're not using
    // the alt-svc route.
    if (!mConnInfo->GetRoutedHost().IsEmpty()) {
      if (trans) {
        trans->RemoveAltSvcUsedHeader();
      }
    }

    ProcessTCPConn(mOutputConn, entry, alreadyOnConn);
  } else {
    ProcessUDPConn(mOutputConn, entry, alreadyOnConn);
  }

  mOutputConn = nullptr;

  // Make sure everything is released.
  Abandon();

  mConnMgrDelegate->RemoveConnectionAttempt(entry, this, false);
}

double HappyEyeballsConnectionAttempt::Duration(TimeStamp epoch) {
  if (mFirstConnectionStart.IsNull()) {
    return 0;
  }
  return (epoch - mFirstConnectionStart).ToMilliseconds();
}

void HappyEyeballsConnectionAttempt::OnTimeout() {
  LOG(("HappyEyeballsConnectionAttempt::OnTimeout %p" PRIx32, this));
  if (IsTerminal()) {
    return;
  }
  Transition(State::TimedOut);
}

void HappyEyeballsConnectionAttempt::EnterTimedOut() {
  LOG(("HappyEyeballsConnectionAttempt::EnterTimedOut %p", this));
  MOZ_ASSERT(mState == State::TimedOut);
  RefPtr<ConnectionEntry> entry(mEntry);
  ReleaseRealTransaction(NS_ERROR_NET_TIMEOUT, entry);
  Abandon();
}

void HappyEyeballsConnectionAttempt::EnterFailed(
    happy_eyeballs::FailureReason aReason) {
  LOG(("HappyEyeballsConnectionAttempt::EnterFailed %p reason=%d", this,
       static_cast<uint32_t>(aReason)));
  MOZ_ASSERT(mState == State::Failed);

  RefPtr<HappyEyeballsConnectionAttempt> self(this);
  RefPtr<ConnectionEntry> entry(mEntry);

  if (entry) {
    mConnMgrDelegate->RemoveConnectionAttempt(entry, this, false);
  }

  CloseHttpTransaction(aReason, entry);
  Abandon();
}

void HappyEyeballsConnectionAttempt::EnterRestartTransaction(
    nsresult aCloseReason) {
  LOG(("HappyEyeballsConnectionAttempt::EnterRestartTransaction %p reason=%x",
       this, static_cast<uint32_t>(aCloseReason)));
  MOZ_ASSERT(mState == State::RestartTransaction);

  RefPtr<HappyEyeballsConnectionAttempt> self(this);
  RefPtr<ConnectionEntry> entry(mEntry);

  if (entry) {
    // abandon=true routes through Abandon() -> Done; cleanup happens there.
    // mEntry will be nulled by EnterDone, but |entry| is a local RefPtr
    // and stays valid for the ReleaseRealTransaction call below.
    mConnMgrDelegate->RemoveConnectionAttempt(entry, this, true);
  }

  // Both classifier inputs that land here (PossibleZeroRTTRetryError TLS
  // alerts and NET_RESET while a racer had entered 0-RTT) mean "the
  // alt-svc endpoint is fine, retry on the same endpoint without 0-RTT".
  // Keep the alt-svc route so Restart()'s CloneAsDirectRoute() doesn't
  // strip it and force a TCP/H2 downgrade.
  if (mTransaction) {
    if (nsHttpTransaction* trans = mTransaction->QueryHttpTransaction()) {
      trans->DoNotRemoveAltSvc();
      // FinishAdopted0RTT mutates state that only makes sense if a HET
      // actually adopted 0-RTT (mResumptionAttempted, mDoNotTryEarlyData,
      // rewind request stream); otherwise leave the txn alone and let
      // Restart() itself disable 0-RTT and evict the stale token.
      if (aCloseReason == NS_ERROR_NET_RESET && mZeroRttHandle->AnyStarted()) {
        trans->FinishAdopted0RTT(/* aRestart = */ true);
      }
    }
  }

  ReleaseRealTransaction(aCloseReason, entry);

  // Fallback if the entry==nullptr path skipped the RemoveConnectionAttempt
  // -> Abandon route above.
  if (mState != State::Done) {
    Abandon();
  }
}

void HappyEyeballsConnectionAttempt::EnterAbortTransaction(
    nsresult aCloseReason) {
  LOG(
      ("HappyEyeballsConnectionAttempt::EnterAbortTransaction %p "
       "reason=%x",
       this, static_cast<uint32_t>(aCloseReason)));
  MOZ_ASSERT(mState == State::AbortTransaction);

  RefPtr<HappyEyeballsConnectionAttempt> self(this);
  RefPtr<ConnectionEntry> entry(mEntry);

  ReleaseRealTransaction(aCloseReason, entry);
  Abandon();
  if (entry) {
    mConnMgrDelegate->RemoveConnectionAttempt(entry, this, false);
  }
}

void HappyEyeballsConnectionAttempt::EnterDone() {
  LOG(("HappyEyeballsConnectionAttempt::EnterDone %p", this));
  MOZ_ASSERT(mState == State::Done);

  for (auto iter = mDnsRequestTable.Iter(); !iter.Done(); iter.Next()) {
    iter.Data()->Cancel();
  }
  mDnsRequestTable.Clear();

  // Snapshot first — Close() callbacks may re-enter and mutate the table.
  nsTArray<RefPtr<ConnectionEstablisher>> establishers;
  for (auto iter = mConnectionEstablisherTable.Iter(); !iter.Done();
       iter.Next()) {
    establishers.AppendElement(iter.Data());
  }
  mConnectionEstablisherTable.Clear();

  for (auto& conn : establishers) {
    conn->Close(NS_ERROR_ABORT);
  }

  if (mTimer) {
    mTimer->Cancel();
  }
  mTimer = nullptr;

  // 0-RTT started but no winner: the real transaction was pulled from the
  // pending queue by LockInRealTransactionFromPendingQueue. Re-queue it
  // (guarded against double-queue via FindTransactionHelper).
  if (mTransaction && mZeroRttHandle->AnyStarted() &&
      !mZeroRttHandle->HadWinner()) {
    if (nsHttpTransaction* realTransaction =
            mTransaction->QueryHttpTransaction()) {
      if (!realTransaction->Closed()) {
        realTransaction->FinishAdopted0RTT(/*aRestart=*/true);
        // If another racing attempt already dispatched this transaction onto a
        // connection it's no longer ours to re-queue (cf. bug 2048392 /
        // bug 2048294).
        if (!realTransaction->Connection()) {
          RefPtr<ConnectionEntry> entry(mEntry);
          RefPtr<PendingTransactionInfo> existing;
          if (entry) {
            existing = mConnMgrDelegate->FindTransaction(
                /*removeWhenFound=*/false, entry, realTransaction);
          }
          if (!existing) {
            mConnMgrDelegate->AddTransaction(realTransaction,
                                             realTransaction->Priority());
          }
        }
      }
    }
    mTransaction = nullptr;
  }

  MOZ_DIAGNOSTIC_ASSERT(!mZeroRttHandle->AnyStarted() ||
                            mZeroRttHandle->HadWinner() || !mTransaction,
                        "transaction not re-queued and not adopted");

  mZeroRttHandle->Cleanup();
  mEntry = nullptr;
}

void HappyEyeballsConnectionAttempt::Transition(State aNext) {
  Transition(aNext, TransitionPayload{});
}

void HappyEyeballsConnectionAttempt::Transition(State aNext,
                                                TransitionPayload aPayload) {
  LOG(("HappyEyeballsConnectionAttempt::Transition %p mState=%d aNext=%d", this,
       static_cast<int>(mState), static_cast<int>(aNext)));

  // Activity-state self-loops are no-ops.
  if (mState == aNext &&
      (aNext == State::Connecting || aNext == State::ZeroRttRacing ||
       aNext == State::ProcessingConnectionResult)) {
    return;
  }

  switch (aNext) {
    case State::Init:
      MOZ_ASSERT_UNREACHABLE("Init is the initial state");
      break;

    case State::Connecting:
      MOZ_ASSERT(
          mState == State::Init || mState == State::ProcessingConnectionResult,
          "Connecting entered from Init or "
          "ProcessingConnectionResult only");
      mState = State::Connecting;
      break;

    case State::ZeroRttRacing:
      MOZ_ASSERT(mState == State::Connecting ||
                     mState == State::ProcessingConnectionResult,
                 "ZeroRttRacing entered from Connecting or "
                 "ProcessingConnectionResult only");
      mState = State::ZeroRttRacing;
      break;

    case State::ProcessingConnectionResult:
      MOZ_ASSERT(mState == State::Connecting || mState == State::ZeroRttRacing,
                 "ProcessingConnectionResult entered from Connecting or "
                 "ZeroRttRacing only");
      mState = State::ProcessingConnectionResult;
      break;

    case State::Succeeded:
      MOZ_ASSERT(!IsTerminal(), "Succeeded from a non-terminal state only");
      mState = State::Succeeded;
      EnterSucceeded();
      break;

    case State::Failed:
      MOZ_ASSERT(!IsTerminal(), "Failed from a non-terminal state only");
      MOZ_ASSERT(aPayload.mFailureReason.isSome(),
                 "Failed requires a FailureReason payload");
      mState = State::Failed;
      EnterFailed(aPayload.mFailureReason.ref());
      break;

    case State::RestartTransaction:
      MOZ_ASSERT(!IsTerminal(),
                 "RestartTransaction from a non-terminal state only");
      mState = State::RestartTransaction;
      EnterRestartTransaction(aPayload.mCloseReason);
      break;

    case State::AbortTransaction:
      MOZ_ASSERT(!IsTerminal(),
                 "AbortTransaction from a non-terminal state only");
      mState = State::AbortTransaction;
      EnterAbortTransaction(aPayload.mCloseReason);
      break;

    case State::TimedOut:
      MOZ_ASSERT(!IsTerminal(), "TimedOut from a non-terminal state only");
      mState = State::TimedOut;
      EnterTimedOut();
      break;

    case State::Done:
      // Idempotent — external Abandon() and every outcome entry action
      // both route here.
      if (mState == State::Done) {
        return;
      }
      mState = State::Done;
      EnterDone();
      break;
  }
}

void HappyEyeballsConnectionAttempt::PrintDiagnostics(nsCString& log) {}

uint32_t HappyEyeballsConnectionAttempt::UnconnectedUDPConnsLength() const {
  uint32_t len = 0;
  for (auto iter = mConnectionEstablisherTable.ConstIter(); !iter.Done();
       iter.Next()) {
    if (iter.Data()->IsUDP()) {
      len++;
    }
  }

  if (len == 0) {
    if (mConnInfo->IsHttp3()) {
      return 1;
    }
  }
  return len;
}

bool HappyEyeballsConnectionAttempt::Claim(nsHttpTransaction* newTransaction) {
  if (mSpeculative) {
    mSpeculative = false;
    mAllow1918 = true;
    for (auto iter = mConnectionEstablisherTable.Iter(); !iter.Done();
         iter.Next()) {
      RefPtr<ConnectionEstablisher> conn = iter.Data();
      conn->ResetSpeculativeFlags();
    }
  }

  if (mFreeToUse) {
    mFreeToUse = false;
    if (newTransaction && mTransaction &&
        mTransaction->QueryNullTransaction()) {
      LOG(
          ("HappyEyeballsConnectionAttempt::Claim %p replacing null "
           "transaction %p with %p",
           this, mTransaction.get(), newTransaction));
      mTransaction->Close(NS_ERROR_ABORT);
      mTransaction = newTransaction;
      // Replay transport statuses that were sent while the null transaction
      // was in place, in the correct order.
      static const nsresult kStatusOrder[] = {
          NS_NET_STATUS_RESOLVING_HOST, NS_NET_STATUS_RESOLVED_HOST,
          NS_NET_STATUS_CONNECTING_TO, NS_NET_STATUS_CONNECTED_TO};
      for (nsresult status : kStatusOrder) {
        if (mSentTransportStatuses.Contains(static_cast<uint32_t>(status))) {
          mTransaction->OnTransportStatus(nullptr, status, 0);
        }
      }
    }
    return true;
  }

  return false;
}

NS_IMETHODIMP
HappyEyeballsConnectionAttempt::OnLookupComplete(nsICancelable* request,
                                                 nsIDNSRecord* rec,
                                                 nsresult status) {
  LOG(("HappyEyeballsConnectionAttempt::OnLookupComplete"));

  // domainLookupEnd tracks the latest DNS response received before the first
  // connection attempt starts. Later responses are ignored; if no lookup has
  // completed by the time the first connection starts, it is set to
  // mFirstConnectionStart there instead.
  if (mFirstConnectionStart.IsNull()) {
    mDnsResolutionEnd = TimeStamp::Now();
  }

  if (!request) {
    return NS_OK;
  }

  RefPtr<DnsRequestInfo> info = mDnsRequestTable.Get(request);
  if (!info) {
    LOG(("OnLookupComplete: Unknown DNS request"));
    return NS_OK;
  }

  uint64_t id = info->Id();
  happy_eyeballs::DnsRecordType type = info->Type();
  mDnsRequestTable.Remove(request);

  switch (type) {
    case happy_eyeballs::DnsRecordType::A:
      return OnARecord(rec, status, id);
    case happy_eyeballs::DnsRecordType::Aaaa:
      return OnAAAARecord(rec, status, id);
    case happy_eyeballs::DnsRecordType::Https:
      return OnHTTPSRecord(rec, status, id);
  }

  return NS_OK;
}

nsresult HappyEyeballsConnectionAttempt::OnARecord(nsIDNSRecord* aRecord,
                                                   nsresult status,
                                                   uint64_t aId) {
  LOG(("HappyEyeballsConnectionAttempt::OnARecord: this=%p status %" PRIx32
       " id=%" PRIu64,
       this, static_cast<uint32_t>(status), aId));
  if (NS_SUCCEEDED(status)) {
    MaybeSendTransportStatus(NS_NET_STATUS_RESOLVED_HOST);
  } else if (NS_FAILED(status)) {
    mLastDnsError = status;
  }

  // TODO: use NS_ERROR_UNKNOWN_PROXY_HOST if stasus is failed and proxy is used

  nsCOMPtr<nsIDNSAddrRecord> addrRecord = do_QueryInterface(aRecord);
  if (addrRecord) {
    mDnsMetadata.Fill(addrRecord);
    if (mTransaction && !mTRRInfoForwarded) {
      mTransaction->SetTRRInfo(mDnsMetadata.mEffectiveTRRMode,
                               mDnsMetadata.mTrrSkipReason);
      mTRRInfoForwarded = true;
    }
  }

  nsresult rv;
  if (NS_FAILED(status) || !addrRecord) {
    if (mOriginDnsLookupIds.Contains(aId)) {
      mOriginDnsLookupIds.Remove(aId);
      MaybeBuildOriginCoalescingKeys();
    }
    // A negative answer can be served from a stale (grace-period) cache entry;
    // report that so the state machine revalidates it (optimistic DNS). The
    // record is available even on failure because the lookup used
    // RESOLVE_WANT_RECORD_ON_ERROR.
    bool fromStaleCache = false;
    if (addrRecord) {
      (void)addrRecord->GetFromStaleCache(&fromStaleCache);
    }
    nsTArray<happy_eyeballs::IpAddr> emptyArray;
    rv = happy_eyeballs_process_dns_response_a(
        mHappyEyeballs, aId, &emptyArray, mDnsMetadata.mIsTRR, fromStaleCache);
    if (NS_FAILED(rv)) {
      return rv;
    }
    return ProcessHappyEyeballsOutput();
  }

  nsTArray<NetAddr> addresses;
  addrRecord->GetAddresses(addresses);

  // Filter to only IPv4 addresses
  nsTArray<NetAddr> ipv4Addresses;
  nsTArray<happy_eyeballs::IpAddr> ipv4IpAddrs;
  for (const auto& addr : addresses) {
    if (addr.raw.family == AF_INET) {
      LOG(("Addr=[%s]", addr.ToString().get()));
      ipv4Addresses.AppendElement(addr);
      ipv4IpAddrs.AppendElement(ToIpAddrV4(addr));
    }
  }

  if (mOriginDnsLookupIds.Contains(aId)) {
    mOriginDnsLookupIds.Remove(aId);
    mOriginAddresses.AppendElements(ipv4Addresses);
    MaybeBuildOriginCoalescingKeys();
  }

  bool aFromStaleCache = false;
  (void)addrRecord->GetFromStaleCache(&aFromStaleCache);

  rv = happy_eyeballs_process_dns_response_a(
      mHappyEyeballs, aId, &ipv4IpAddrs, mDnsMetadata.mIsTRR, aFromStaleCache);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return ProcessHappyEyeballsOutput();
}

nsresult HappyEyeballsConnectionAttempt::OnAAAARecord(nsIDNSRecord* aRecord,
                                                      nsresult status,
                                                      uint64_t aId) {
  LOG(("HappyEyeballsConnectionAttempt::OnAAAARecord: this=%p status %" PRIx32
       " id=%" PRIu64,
       this, static_cast<uint32_t>(status), aId));
  if (NS_SUCCEEDED(status)) {
    MaybeSendTransportStatus(NS_NET_STATUS_RESOLVED_HOST);
  } else if (NS_FAILED(status)) {
    mLastDnsError = status;
  }

  // TODO: use NS_ERROR_UNKNOWN_PROXY_HOST if stasus is failed and proxy is used

  nsCOMPtr<nsIDNSAddrRecord> addrRecord = do_QueryInterface(aRecord);

  nsresult rv;
  if (NS_FAILED(status) || !addrRecord) {
    if (mOriginDnsLookupIds.Contains(aId)) {
      mOriginDnsLookupIds.Remove(aId);
      MaybeBuildOriginCoalescingKeys();
    }
    // See OnARecord: a stale (grace-period) negative must be reported as such
    // so the state machine revalidates it.
    bool fromStaleCache = false;
    if (addrRecord) {
      (void)addrRecord->GetFromStaleCache(&fromStaleCache);
    }
    nsTArray<happy_eyeballs::IpAddr> emptyArray;
    rv = happy_eyeballs_process_dns_response_aaaa(
        mHappyEyeballs, aId, &emptyArray, mDnsMetadata.mIsTRR, fromStaleCache);
    if (NS_FAILED(rv)) {
      return rv;
    }
    return ProcessHappyEyeballsOutput();
  }

  nsTArray<NetAddr> addresses;
  addrRecord->GetAddresses(addresses);

  // Filter to only IPv6 addresses
  nsTArray<NetAddr> ipv6Addresses;
  nsTArray<happy_eyeballs::IpAddr> ipv6IpAddrs;
  for (const auto& addr : addresses) {
    if (addr.raw.family == AF_INET6) {
      LOG(("Addr=[%s]", addr.ToString().get()));
      ipv6Addresses.AppendElement(addr);
      ipv6IpAddrs.AppendElement(ToIpAddrV6(addr));
    }
  }

  if (mOriginDnsLookupIds.Contains(aId)) {
    mOriginDnsLookupIds.Remove(aId);
    mOriginAddresses.AppendElements(ipv6Addresses);
    MaybeBuildOriginCoalescingKeys();
  }

  bool aaaaFromStaleCache = false;
  (void)addrRecord->GetFromStaleCache(&aaaaFromStaleCache);

  rv = happy_eyeballs_process_dns_response_aaaa(
      mHappyEyeballs, aId, &ipv6IpAddrs, mDnsMetadata.mIsTRR,
      aaaaFromStaleCache);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return ProcessHappyEyeballsOutput();
}

void HappyEyeballsConnectionAttempt::MaybeBuildOriginCoalescingKeys() {
  // Called on each origin A/AAAA completion; wait until both have finished.
  // Both lookups are issued up front, so this proceeds exactly once (when the
  // last one completes).
  if (!mOriginDnsLookupIds.IsEmpty()) {
    return;
  }

  if (mOriginAddresses.IsEmpty() ||
      !StaticPrefs::network_http_http2_coalesce_hostnames() ||
      (!StaticPrefs::network_http_http2_enabled() &&
       !nsHttpHandler::IsHttp3Enabled())) {
    return;
  }

  RefPtr<ConnectionEntry> entry(mEntry);
  if (!entry) {
    return;
  }

  // The H2/H3 outcome isn't known yet; the coalescing keys are IP-based and
  // identical either way. MaybeProcessCoalescingKeys still applies the
  // EndToEndSSL / AllowHttp2 / proxy gating, and the connection is only
  // registered in the coalescing hash at success (UpdateCoalescingForNewConn).
  if (mConnMgrDelegate->MaybeProcessCoalescingKeys(entry, mOriginAddresses,
                                                   mConnInfo->IsHttp3())) {
    mConnMgrDelegate->ProcessSpdyPendingQ(entry);
  }
}

// Helper function to convert ALPN string to HttpVersion enum
static Maybe<happy_eyeballs::HttpVersion> AlpnStringToProtocol(
    const nsACString& aAlpn) {
  if (aAlpn.EqualsLiteral("h3")) {
    return Some(happy_eyeballs::HttpVersion::H3);
  }
  if (aAlpn.EqualsLiteral("h2")) {
    return Some(happy_eyeballs::HttpVersion::H2);
  }
  if (aAlpn.EqualsLiteral("http/1.1")) {
    return Some(happy_eyeballs::HttpVersion::H1);
  }
  // Unknown ALPN protocol
  return Nothing();
}

nsresult HappyEyeballsConnectionAttempt::OnHTTPSRecord(nsIDNSRecord* aRecord,
                                                       nsresult status,
                                                       uint64_t aId) {
  LOG(("HappyEyeballsConnectionAttempt::OnHTTPSRecord %p status=%x id=%" PRIu64,
       this, static_cast<uint32_t>(status), aId));
  nsCOMPtr<nsIDNSHTTPSSVCRecord> httpsRecord = do_QueryInterface(aRecord);
  if (!httpsRecord || NS_FAILED(status)) {
    // A stale (grace-period) negative must be reported as such so the state
    // machine revalidates it (optimistic DNS).
    bool fromStaleCache = false;
    if (nsCOMPtr<nsIDNSByTypeRecord> byTypeRec = do_QueryInterface(aRecord)) {
      (void)byTypeRec->GetFromStaleCache(&fromStaleCache);
    }
    nsTArray<happy_eyeballs::ServiceInfo> emptyArray;
    (void)happy_eyeballs_process_dns_response_https(
        mHappyEyeballs, aId, &emptyArray, mDnsMetadata.mIsTRR, fromStaleCache);
    return ProcessHappyEyeballsOutput();
  }

  bool httpsIsTRR = false;
  (void)httpsRecord->IsTRR(&httpsIsTRR);

  bool httpsFromStaleCache = false;
  if (nsCOMPtr<nsIDNSByTypeRecord> byTypeRec = do_QueryInterface(aRecord)) {
    (void)byTypeRec->GetFromStaleCache(&httpsFromStaleCache);
  }

  if (httpsIsTRR) {
    mDnsMetadata.mIsTRR = true;
    mDnsMetadata.mEffectiveTRRMode =
        static_cast<nsIRequest::TRRMode>(StaticPrefs::network_trr_mode());
    mDnsMetadata.mTrrSkipReason = nsITRRSkipReason::TRR_OK;
    if (mTransaction && !mTRRInfoForwarded) {
      mTransaction->SetTRRInfo(mDnsMetadata.mEffectiveTRRMode,
                               mDnsMetadata.mTrrSkipReason);
      mTRRInfoForwarded = true;
    }
  }

  nsTArray<RefPtr<nsISVCBRecord>> svcbRecords;
  // TODO: Handle aNoHttp2, aNoHttp3, and aCname.
  (void)httpsRecord->GetRecords(svcbRecords);
  if (svcbRecords.IsEmpty()) {
    nsTArray<happy_eyeballs::ServiceInfo> emptyArray;
    (void)happy_eyeballs_process_dns_response_https(
        mHappyEyeballs, aId, &emptyArray, httpsIsTRR, httpsFromStaleCache);
    return ProcessHappyEyeballsOutput();
  }

  nsTArray<happy_eyeballs::ServiceInfo> serviceInfos;

  for (const auto& svcbRecord : svcbRecords) {
    happy_eyeballs::ServiceInfo svcInfo;
    (void)svcbRecord->GetPriority(&svcInfo.priority);
    (void)svcbRecord->GetName(svcInfo.target_name);
    svcInfo.port = svcbRecord->GetPort().valueOr(0);

    nsTArray<RefPtr<nsISVCParam>> values;
    (void)svcbRecord->GetValues(values);

    nsTArray<nsCString> alpn;
    bool noDefaultAlpn = false;
    nsTArray<RefPtr<nsINetAddr>> ipv4Hint;
    nsTArray<RefPtr<nsINetAddr>> ipv6Hint;

    for (const auto& value : values) {
      uint16_t type;
      (void)value->GetType(&type);
      switch (type) {
        case SvcParamKeyAlpn: {
          nsCOMPtr<nsISVCParamAlpn> alpnParam = do_QueryInterface(value);
          (void)alpnParam->GetAlpn(alpn);
          break;
        }
        case SvcParamKeyNoDefaultAlpn:
          noDefaultAlpn = true;
          break;
        case SvcParamKeyIpv4Hint: {
          nsCOMPtr<nsISVCParamIPv4Hint> ipv4Param = do_QueryInterface(value);
          (void)ipv4Param->GetIpv4Hint(ipv4Hint);
          break;
        }
        case SvcParamKeyIpv6Hint: {
          nsCOMPtr<nsISVCParamIPv6Hint> ipv6Param = do_QueryInterface(value);
          (void)ipv6Param->GetIpv6Hint(ipv6Hint);
          break;
        }
        case SvcParamKeyEchConfig: {
          nsCOMPtr<nsISVCParamEchConfig> echConfigParam =
              do_QueryInterface(value);
          nsCString echConfig;
          (void)echConfigParam->GetEchconfig(echConfig);
          svcInfo.ech_config.AppendElements(
              reinterpret_cast<const uint8_t*>(echConfig.BeginReading()),
              echConfig.Length());
          break;
        }
        default:
          break;
      }
    }

    for (const auto& alpnStr : alpn) {
      auto protocol = AlpnStringToProtocol(alpnStr);
      if (protocol) {
        svcInfo.alpn_http_versions.AppendElement(protocol.ref());
      }
    }

    // The SVCB ALPN set includes the scheme default ("http/1.1") unless
    // "no-default-alpn" is present (RFC 9460 Section 7.1.2).
    if (!noDefaultAlpn &&
        !svcInfo.alpn_http_versions.Contains(happy_eyeballs::HttpVersion::H1)) {
      svcInfo.alpn_http_versions.AppendElement(happy_eyeballs::HttpVersion::H1);
    }

    for (const auto& addr : ipv4Hint) {
      NetAddr netAddr;
      addr->GetNetAddr(&netAddr);
      svcInfo.ipv4_hints.AppendElement(ToIpAddrV4(netAddr));
    }

    for (const auto& addr : ipv6Hint) {
      NetAddr netAddr;
      addr->GetNetAddr(&netAddr);
      svcInfo.ipv6_hints.AppendElement(ToIpAddrV6(netAddr));
    }

    serviceInfos.AppendElement(std::move(svcInfo));
  }

  (void)happy_eyeballs_process_dns_response_https(
      mHappyEyeballs, aId, &serviceInfos, httpsIsTRR, httpsFromStaleCache);
  return ProcessHappyEyeballsOutput();
}

NS_IMETHODIMP  // method for nsITimerCallback
HappyEyeballsConnectionAttempt::Notify(nsITimer* timer) {
  return ProcessHappyEyeballsOutput();
}

NS_IMETHODIMP  // method for nsINamed
HappyEyeballsConnectionAttempt::GetName(nsACString& aName) {
  aName.AssignLiteral("HappyEyeballsConnectionAttempt");
  return NS_OK;
}

void HappyEyeballsConnectionAttempt::SetupTimer(uint64_t aTimeout) {
  if (!aTimeout) {
    MOZ_ASSERT(false, "aTimeout should not be 0");
    return;
  }

  LOG(("HappyEyeballsConnectionAttempt::SetupTimer to %" PRIu64 "ms [this=%p].",
       aTimeout, this));

  if (!mTimer) {
    // This can only fail on OOM and we'd crash.
    mTimer = NS_NewTimer();
  }

  DebugOnly<nsresult> rv =
      mTimer->InitWithCallback(this, aTimeout, nsITimer::TYPE_ONE_SHOT);
  // There is no meaningful error handling we can do here. But an error here
  // should only be possible if the timer thread did already shut down.
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

}  // namespace mozilla::net
