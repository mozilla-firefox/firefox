/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Http3Session.h"

#include "ASpdySession.h"  // because of SoftStreamError()
#include "Http3ConnectUDPStream.h"
#include "Http3Stream.h"
#include "Http3StreamBase.h"
#include "Http3StreamTunnel.h"
#include "Http3WebTransportSession.h"
#include "Http3WebTransportStream.h"
#include "HttpConnectionUDP.h"
#include "HttpLog.h"
#include "QuicSocketControl.h"
#include "SSLServerCertVerification.h"
#include "SSLTokensCache.h"
#include "ScopedNSSTypes.h"
#include "WebTransportCertificateVerifier.h"
#include "mozilla/RandomNum.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/glean/NetwerkDnsMetrics.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "mozilla/net/DNS.h"
#include "nsHttpConnectionMgr.h"
#include "nsHttpHandler.h"
#include "nsHttpTransaction.h"
#include "nsIHttpActivityObserver.h"
#include "nsIOService.h"
#include "nsITLSSocketControl.h"
#include "nsNetAddr.h"
#include "nsQueryObject.h"
#include "nsSocketTransportService2.h"
#include "nsThreadUtils.h"
#include "sslerr.h"

namespace mozilla::net {

extern const nsCString& TRRProviderKey();

const uint64_t HTTP3_APP_ERROR_NO_ERROR = 0x100;
// const uint64_t HTTP3_APP_ERROR_GENERAL_PROTOCOL_ERROR = 0x101;
// const uint64_t HTTP3_APP_ERROR_INTERNAL_ERROR = 0x102;
// const uint64_t HTTP3_APP_ERROR_STREAM_CREATION_ERROR = 0x103;
// const uint64_t HTTP3_APP_ERROR_CLOSED_CRITICAL_STREAM = 0x104;
// const uint64_t HTTP3_APP_ERROR_FRAME_UNEXPECTED = 0x105;
// const uint64_t HTTP3_APP_ERROR_FRAME_ERROR = 0x106;
// const uint64_t HTTP3_APP_ERROR_EXCESSIVE_LOAD = 0x107;
// const uint64_t HTTP3_APP_ERROR_ID_ERROR = 0x108;
// const uint64_t HTTP3_APP_ERROR_SETTINGS_ERROR = 0x109;
// const uint64_t HTTP3_APP_ERROR_MISSING_SETTINGS = 0x10a;
const uint64_t HTTP3_APP_ERROR_REQUEST_REJECTED = 0x10b;
const uint64_t HTTP3_APP_ERROR_REQUEST_CANCELLED = 0x10c;
// const uint64_t HTTP3_APP_ERROR_REQUEST_INCOMPLETE = 0x10d;
// const uint64_t HTTP3_APP_ERROR_EARLY_RESPONSE = 0x10e;
// const uint64_t HTTP3_APP_ERROR_CONNECT_ERROR = 0x10f;
const uint64_t HTTP3_APP_ERROR_VERSION_FALLBACK = 0x110;

// const uint32_t UDP_MAX_PACKET_SIZE = 4096;
const uint32_t MAX_PTO_COUNTS = 16;

const uint32_t TRANSPORT_ERROR_STATELESS_RESET = 20;

NS_IMPL_ADDREF_INHERITED(Http3Session, nsAHttpConnection)
NS_IMPL_RELEASE_INHERITED(Http3Session, nsAHttpConnection)
NS_INTERFACE_MAP_BEGIN(Http3Session)
  NS_INTERFACE_MAP_ENTRY(nsAHttpConnection)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(Http3Session)
NS_INTERFACE_MAP_END

Http3Session::Http3Session() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("Http3Session::Http3Session [this=%p]", this));

  mCurrentBrowserId = gHttpHandler->ConnMgr()->CurrentBrowserId();
}

static nsresult RawBytesToNetAddr(uint16_t aFamily, const uint8_t* aRemoteAddr,
                                  uint16_t remotePort, NetAddr* netAddr) {
  if (aFamily == AF_INET) {
    netAddr->inet.family = AF_INET;
    netAddr->inet.port = htons(remotePort);
    memcpy(&netAddr->inet.ip, aRemoteAddr, 4);
  } else if (aFamily == AF_INET6) {
    netAddr->inet6.family = AF_INET6;
    netAddr->inet6.port = htons(remotePort);
    memcpy(&netAddr->inet6.ip.u8, aRemoteAddr, 16);
  } else {
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

nsresult Http3Session::Init(nsHttpConnectionInfo* aConnInfo,
                            nsINetAddr* aSelfAddr, nsINetAddr* aPeerAddr,
                            HttpConnectionUDP* udpConn, uint32_t aProviderFlags,
                            nsIInterfaceRequestor* callbacks,
                            nsIUDPSocket* socket, bool aIsTunnel) {
  LOG3(("Http3Session::Init %p", this));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(udpConn);

  mConnInfo = aConnInfo;
  mIsTrrServiceChannel = aConnInfo->GetIsTrrServiceChannel();
  mNetAddr = aPeerAddr;

  // When `isOuterConnection` is true, this Http3Session represents the *outer*
  // connection between Firefox and the proxy (e.g., when using CONNECT-UDP).
  //
  // We track this flag for two main reasons:
  // 1. To select the correct hostname during TLS negotiation on the outer
  //    connection.
  // 2. To explicitly enable Path MTU Discovery (PMTUD) on the outer connection,
  //    since the outer path’s MTU must be at least as large as the inner one.
  bool isOuterConnection = false;
  if (!aIsTunnel) {
    if (auto* proxyInfo = aConnInfo->ProxyInfo()) {
      isOuterConnection = proxyInfo->IsHttp3Proxy();
    }
  }

  // Create security control and info object for quic.
  mSocketControl = new QuicSocketControl(
      isOuterConnection ? aConnInfo->ProxyInfo()->Host()
                        : aConnInfo->GetOrigin(),
      isOuterConnection ? aConnInfo->ProxyInfo()->Port()
                        : aConnInfo->OriginPort(),
      aProviderFlags, aConnInfo->GetOriginAttributes(), this);
  const nsCString& alpn = isOuterConnection ? aConnInfo->GetProxyNPNToken()
                                            : aConnInfo->GetNPNToken();

  NetAddr selfAddr;
  MOZ_ALWAYS_SUCCEEDS(aSelfAddr->GetNetAddr(&selfAddr));
  NetAddr peerAddr;
  MOZ_ALWAYS_SUCCEEDS(aPeerAddr->GetNetAddr(&peerAddr));

  LOG3(
      ("Http3Session::Init origin=%s, alpn=%s, selfAddr=%s, peerAddr=%s,"
       " qpack table size=%u, max blocked streams=%u webtransport=%d "
       "[this=%p]",
       PromiseFlatCString(mSocketControl->GetHostName()).get(),
       PromiseFlatCString(alpn).get(), selfAddr.ToString().get(),
       peerAddr.ToString().get(), gHttpHandler->DefaultQpackTableSize(),
       gHttpHandler->DefaultHttp3MaxBlockedStreams(),
       mConnInfo->GetWebTransport(), this));

  if (mConnInfo->GetWebTransport()) {
    ExtState(ExtendedConnectKind::WebTransport).mStatus = NEGOTIATING;
  }
  if (isOuterConnection) {
    ExtState(ExtendedConnectKind::ConnectUDP).mStatus = NEGOTIATING;
  }

  mUseNSPRForIO =
      StaticPrefs::network_http_http3_use_nspr_for_io() || aIsTunnel;

  uint32_t idleTimeout = StaticPrefs::network_http_http3_idle_timeout();

  // 0 means "use neqo's spec-compliant default PTO scaling".
  uint32_t fastPto = mConnInfo->GetIsTrrServiceChannel()
                         ? StaticPrefs::network_trr_fast_pto_for_http3_conn()
                         : 0;

  nsresult rv;
  if (mUseNSPRForIO) {
    rv = NeqoHttp3Conn::InitUseNSPRForIO(
        mSocketControl->GetHostName(), alpn, selfAddr, peerAddr,
        gHttpHandler->DefaultQpackTableSize(),
        gHttpHandler->DefaultHttp3MaxBlockedStreams(),
        StaticPrefs::network_http_http3_max_data(),
        StaticPrefs::network_http_http3_max_stream_data(),
        StaticPrefs::network_http_http3_version_negotiation_enabled(),
        mConnInfo->GetWebTransport(), gHttpHandler->Http3QlogDir(), idleTimeout,
        fastPto, getter_AddRefs(mHttp3Connection));
  } else {
    rv = NeqoHttp3Conn::Init(
        mSocketControl->GetHostName(), alpn, selfAddr, peerAddr,
        gHttpHandler->DefaultQpackTableSize(),
        gHttpHandler->DefaultHttp3MaxBlockedStreams(),
        StaticPrefs::network_http_http3_max_data(),
        StaticPrefs::network_http_http3_max_stream_data(),
        StaticPrefs::network_http_http3_version_negotiation_enabled(),
        mConnInfo->GetWebTransport(), gHttpHandler->Http3QlogDir(), idleTimeout,
        fastPto, socket->GetFileDescriptor(), isOuterConnection,
        getter_AddRefs(mHttp3Connection));
  }
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsAutoCString peerId;
  mSocketControl->GetPeerId(peerId);
  nsTArray<uint8_t> token;
  SessionCacheInfo info;
  udpConn->ChangeConnectionState(ConnectionState::TLS_HANDSHAKING);

  auto hasServCertHashes = [&]() -> bool {
    if (!mConnInfo->GetWebTransport()) {
      return false;
    }
    const nsTArray<RefPtr<nsIWebTransportHash>>* servCertHashes =
        gHttpHandler->ConnMgr()->GetServerCertHashes(mConnInfo);
    return servCertHashes && !servCertHashes->IsEmpty();
  };

  // See https://github.com/mozilla/neqo/issues/2442.
  // We need to set ECH first before set resumption token.
  auto config = mConnInfo->GetEchConfig();
  if (config.IsEmpty()) {
    if (StaticPrefs::security_tls_ech_grease_http3() && config.IsEmpty()) {
      if ((RandomUint64().valueOr(0) % 100) >=
          100 - StaticPrefs::security_tls_ech_grease_probability()) {
        // Setting an empty config enables GREASE mode.
        mSocketControl->SetEchConfig(config);
        mEchExtensionStatus = EchExtensionStatus::kGREASE;
      }
    }
  } else if (nsHttpHandler::EchConfigEnabled(true) && !config.IsEmpty()) {
    mSocketControl->SetEchConfig(config);
    mEchExtensionStatus = EchExtensionStatus::kReal;
    HttpConnectionActivity activity(
        mConnInfo->HashKey(), mConnInfo->GetOrigin(), mConnInfo->OriginPort(),
        mConnInfo->EndToEndSSL(), !mConnInfo->GetEchConfig().IsEmpty(),
        mConnInfo->IsHttp3());
    gHttpHandler->ObserveHttpActivityWithArgs(
        HttpActivityArgs(activity), NS_ACTIVITY_TYPE_HTTP_CONNECTION,
        NS_HTTP_ACTIVITY_SUBTYPE_ECH_SET, PR_Now(), 0, ""_ns);
  } else {
    mEchExtensionStatus = EchExtensionStatus::kNotPresent;
  }

  // In WebTransport, when servCertHashes is specified, it indicates that the
  // connection to the WebTransport server should authenticate using the
  // expected certificate hash. Therefore, 0RTT should be disabled in this
  // context to ensure the certificate hash is checked.
  if (StaticPrefs::network_http_http3_enable_0rtt() && !hasServCertHashes()) {
    uint32_t maxAttempts =
        StaticPrefs::network_ssl_tokens_cache_records_per_entry();
    bool tokenFound = false;
    bool tokenAccepted = false;
    for (uint32_t attempt = 0; attempt < maxAttempts; ++attempt) {
      if (NS_FAILED(SSLTokensCache::Get(peerId, token, info))) {
        break;
      }
      tokenFound = true;
      LOG(("Found a resumption token in the cache [attempt=%u].", attempt));
      nsresult rv = mHttp3Connection->SetResumptionToken(token);
      if (NS_FAILED(rv)) {
        LOG(("SetResumptionToken failed [attempt=%u], trying next token",
             attempt));
        continue;
      }
      tokenAccepted = true;
      mSocketControl->SetSessionCacheInfo(std::move(info));
      if (mHttp3Connection->IsZeroRtt()) {
        LOG(("Can send ZeroRtt data"));
        RefPtr<Http3Session> self(this);
        mState = ZERORTT;
        udpConn->ChangeConnectionState(ConnectionState::ZERORTT);
        mZeroRttStarted = TimeStamp::Now();
        // Let the nsHttpConnectionMgr know that the connection can accept
        // transactions.
        // We need to dispatch the following function to this thread so that
        // it is executed after the current function. At this point a
        // Http3Session is still being initialized and ReportHttp3Connection
        // will try to dispatch transaction on this session therefore it
        // needs to be executed after the initializationg is done.
        nsCOMPtr<nsIRunnable> event =
            NS_NewRunnableFunction("Http3Session::ReportHttp3Connection",
                                   [self]() { self->ReportHttp3Connection(); });
        if (StaticPrefs::network_trr_high_priority_events() &&
            mConnInfo->GetIsTrrServiceChannel()) {
          event = new PrioritizableRunnable(
              event.forget(), nsIRunnablePriority::PRIORITY_MEDIUMHIGH);
        }
        DebugOnly<nsresult> rv = DispatchToCurrent(event.forget());
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Dispatch failed");
      }
      break;
    }
    if (tokenFound && !tokenAccepted) {
      glean::network::ssl_token_resumption_outcome.Get("rejected"_ns).Add();
    }
  }

  if (mState != ZERORTT) {
    ZeroRttTelemetry(ZeroRttOutcome::NOT_USED);
  }

  // After this line, Http3Session and HttpConnectionUDP become a cycle. We put
  // this line in the end of Http3Session::Init to make sure Http3Session can be
  // released when Http3Session::Init early returned.
  mUdpConn = udpConn;
  return NS_OK;
}

void Http3Session::RekeyAfterHttp3OnlyHandOff(nsHttpConnectionInfo* aConnInfo) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(aConnInfo);
  MOZ_ASSERT(mConnInfo);
  MOZ_ASSERT(!aConnInfo->GetHttp3Only(),
             "hand-off must relax the policy to Allowed");

  LOG(("Http3Session::RekeyAfterHttp3OnlyHandOff [this=%p] %s -> %s", this,
       mConnInfo->HashKey().get(), aConnInfo->HashKey().get()));
  mConnInfo = aConnInfo;
  mIsTrrServiceChannel = aConnInfo->GetIsTrrServiceChannel();
}

void Http3Session::DoSetEchConfig(const nsACString& aEchConfig) {
  LOG(("Http3Session::DoSetEchConfig %p of length %zu", this,
       aEchConfig.Length()));
  nsTArray<uint8_t> config;
  config.AppendElements(
      reinterpret_cast<const uint8_t*>(aEchConfig.BeginReading()),
      aEchConfig.Length());
  mHttp3Connection->SetEchConfig(config);
}

nsresult Http3Session::SendPriorityUpdateFrame(uint64_t aStreamId,
                                               uint8_t aPriorityUrgency,
                                               bool aPriorityIncremental) {
  return mHttp3Connection->PriorityUpdate(aStreamId, aPriorityUrgency,
                                          aPriorityIncremental);
}

// Shutdown the http3session and close all transactions.
void Http3Session::Shutdown() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (mTimer) {
    mTimer->Cancel();
  }
  mTimer = nullptr;
  mTimerCallback = nullptr;

  bool isEchRetry = mError == mozilla::psm::GetXPCOMFromNSSError(
                                  SSL_ERROR_ECH_RETRY_WITH_ECH);
  bool isNSSError = psm::IsNSSErrorCode(-1 * NS_ERROR_GET_CODE(mError));
  bool allowToRetryWithDifferentIPFamily =
      mBeforeConnectedError &&
      gHttpHandler->ConnMgr()->AllowToRetryDifferentIPFamilyForHttp3(mConnInfo,
                                                                     mError);
  LOG(("Http3Session::Shutdown %p allowToRetryWithDifferentIPFamily=%d", this,
       allowToRetryWithDifferentIPFamily));
  // Don't exclude H3 if the failure was in the 0-RTT phase: the PSK ticket
  // is single-use, so the retry will do a full TLS handshake and the H3
  // server itself should still be reachable.
  if (mBeforeConnectedError && mHad0RttStream) {
    mDontExclude = true;
  }
  if ((mBeforeConnectedError ||
       (mError == NS_ERROR_NET_HTTP3_PROTOCOL_ERROR)) &&
      !isNSSError && !isEchRetry && !mConnInfo->GetWebTransport() &&
      !allowToRetryWithDifferentIPFamily && !mDontExclude) {
    gHttpHandler->ExcludeHttp3(mConnInfo);
    if (mFirstHttpTransaction) {
      mFirstHttpTransaction->DisableHttp3(false);
    }
  }

  nsTArray<RefPtr<Http3StreamBase>> streams;
  streams.SetCapacity(mStreamTransactionHash.Count());
  for (const auto& s : mStreamTransactionHash.Values()) {
    streams.AppendElement(s);
  }
  for (const auto& stream : streams) {
    if (mBeforeConnectedError) {
      // We have an error before we were connected, just restart transactions.
      // The transaction restart code path will remove AltSvc mapping and the
      // direct path will be used.
      MOZ_ASSERT(NS_FAILED(mError));
      if (isEchRetry) {
        // We have to propagate this error to nsHttpTransaction, so the
        // transaction will be restarted with a new echConfig.
        stream->Close(mError);
      } else if (isNSSError) {
        stream->Close(mError);
      } else {
        if (allowToRetryWithDifferentIPFamily && mNetAddr) {
          NetAddr addr;
          mNetAddr->GetNetAddr(&addr);
          gHttpHandler->ConnMgr()->SetRetryDifferentIPFamilyForHttp3(
              mConnInfo, addr.raw.family);
          // We want the transaction to be restarted with the same connection
          // info.
          stream->Transaction()->DoNotRemoveAltSvc();
          // We already set the preference in SetRetryDifferentIPFamilyForHttp3,
          // so we want to keep it for the next retry.
          stream->Transaction()->DoNotResetIPFamilyPreference();
          stream->Close(NS_ERROR_NET_RESET);
          // Since Http3Session::Shutdown can be called multiple times, we set
          // mDontExclude for not putting this domain into the excluded list.
          mDontExclude = true;
        } else {
          stream->Close(NS_ERROR_NET_RESET);
        }
      }
    } else if (!stream->HasStreamId()) {
      if (NS_SUCCEEDED(mError)) {
        // Connection has not been started yet. We can restart it.
        stream->Transaction()->DoNotRemoveAltSvc();
      }
      stream->Close(
          mError == NS_ERROR_NET_HTTP3_PROTOCOL_ERROR &&
                  !mConnInfo->IsHttp3ProxyConnection() &&
                  StaticPrefs::
                      network_http_http3_fallback_to_h2_on_protocol_error()
              ? NS_ERROR_NET_HTTP3_PROTOCOL_ERROR
              : NS_ERROR_NET_RESET);
    } else if (stream->GetHttp3Stream() &&
               stream->GetHttp3Stream()->RecvdData()) {
      stream->Close(NS_ERROR_NET_PARTIAL_TRANSFER);
    } else if (mError == NS_ERROR_NET_HTTP3_PROTOCOL_ERROR) {
      stream->Close(NS_ERROR_NET_HTTP3_PROTOCOL_ERROR);
    } else if (mError == NS_ERROR_NET_RESET) {
      stream->Close(NS_ERROR_NET_RESET);
    } else {
      // The session went away without a clean reset/protocol error, but this
      // stream never received any response data, so it is safe to retry it
      // on a new connection.
      stream->Close(NS_ERROR_NET_UNCLEAN_SHUTDOWN);
    }
    RemoveStreamFromQueues(stream);
    if (stream->HasStreamId()) {
      mStreamIdHash.Remove(stream->StreamId());
    }
  }
  mStreamTransactionHash.Clear();

  for (const auto& stream : mWebTransportSessions) {
    stream->Close(NS_ERROR_ABORT);
    RemoveStreamFromQueues(stream);
    mStreamIdHash.Remove(stream->StreamId());
  }
  mWebTransportSessions.Clear();

  for (const auto& stream : mTunnelStreams) {
    stream->Close(NS_ERROR_ABORT);
    RemoveStreamFromQueues(stream);
    mStreamIdHash.Remove(stream->StreamId());
  }
  mTunnelStreams.Clear();

  for (const auto& stream : mWebTransportStreams) {
    stream->Close(NS_ERROR_ABORT);
    RemoveStreamFromQueues(stream);
    mStreamIdHash.Remove(stream->StreamId());
  }

  RefPtr<Http3StreamBase> stream;
  while ((stream = mQueuedStreams.PopFront())) {
    LOG(("Close remaining stream in queue:%p", stream.get()));
    stream->SetQueued(false);
    stream->Close(NS_ERROR_ABORT);
  }
  mWebTransportStreams.Clear();
}

Http3Session::~Http3Session() {
  LOG3(("Http3Session::~Http3Session %p", this));
#ifndef ANDROID
  EchOutcomeTelemetry();
#endif
  glean::http3::request_per_conn.AccumulateSingleSample(mTransactionCount);
  glean::http3::blocked_by_stream_limit_per_conn.AccumulateSingleSample(
      mBlockedByStreamLimitCount);
  glean::http3::trans_blocked_by_stream_limit_per_conn.AccumulateSingleSample(
      mTransactionsBlockedByStreamLimitCount);

  glean::http3::trans_sending_blocked_by_flow_control_per_conn
      .AccumulateSingleSample(mTransactionsSenderBlockedByFlowControlCount);

  if (mTrrStreams) {
    mozilla::glean::networking::trr_request_count_per_conn
        .Get(nsPrintfCString("%s_h3", mConnInfo->Origin()))
        .Add(static_cast<int32_t>(mTrrStreams));
  }

  Shutdown();
}

// This function may return a socket error.
// It will not return an error if socket error is
// NS_BASE_STREAM_WOULD_BLOCK.
// A caller of this function will close the Http3 connection
// in case of an error.
// The only callers is Http3Session::RecvData.
nsresult Http3Session::ProcessInput(nsIUDPSocket* socket) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(mUdpConn);

  LOG(("Http3Session::ProcessInput writer=%p [this=%p state=%d]",
       mUdpConn.get(), this, mState));

  if (!socket || socket->IsSocketClosed()) {
    MOZ_DIAGNOSTIC_ASSERT(false, "UDP socket should still be open");
    return NS_ERROR_UNEXPECTED;
  }

  if (mUseNSPRForIO) {
    while (true) {
      nsTArray<uint8_t> data;
      NetAddr addr{};
      // RecvWithAddr actually does not return an error.
      nsresult rv = socket->RecvWithAddr(&addr, data);
      MOZ_ALWAYS_SUCCEEDS(rv);
      if (NS_FAILED(rv) || data.IsEmpty()) {
        break;
      }
      rv = mHttp3Connection->ProcessInputUseNSPRForIO(addr, data);
      MOZ_ALWAYS_SUCCEEDS(rv);
      if (NS_FAILED(rv)) {
        break;
      }

      LOG(("Http3Session::ProcessInput received=%zu", data.Length()));
      mTotalBytesRead += static_cast<int64_t>(data.Length());
    }

    return NS_OK;
  }

  // Not using NSPR.

  auto rv = mHttp3Connection->ProcessInput();
  // Note: WOULD_BLOCK is handled in neqo_glue.
  if (NS_FAILED(rv.result)) {
    mSocketError = rv.result;
    // If there was an error return from here. We do not need to set a timer,
    // because we will close the connection.
    return rv.result;
  }
  mTotalBytesRead += rv.bytes_read;
  socket->AddInputBytes(rv.bytes_read);

  return NS_OK;
}

nsresult Http3Session::ProcessTransactionRead(uint64_t stream_id) {
  RefPtr<Http3StreamBase> stream = mStreamIdHash.Get(stream_id);
  if (!stream) {
    LOG(
        ("Http3Session::ProcessTransactionRead - stream not found "
         "stream_id=0x%" PRIx64 " [this=%p].",
         stream_id, this));
    return NS_OK;
  }

  return ProcessTransactionRead(stream);
}

nsresult Http3Session::ProcessTransactionRead(Http3StreamBase* stream) {
  nsresult rv = stream->WriteSegments();

  if (ASpdySession::SoftStreamError(rv) || stream->Done()) {
    LOG3(
        ("Http3Session::ProcessSingleTransactionRead session=%p stream=%p "
         "0x%" PRIx64 " cleanup stream rv=0x%" PRIx32 " done=%d.\n",
         this, stream, stream->StreamId(), static_cast<uint32_t>(rv),
         stream->Done()));
    CloseStream(stream,
                (rv == NS_BINDING_RETARGETED) ? NS_BINDING_RETARGETED : NS_OK);
    return NS_OK;
  }

  if (NS_FAILED(rv) && rv != NS_BASE_STREAM_WOULD_BLOCK) {
    return rv;
  }
  return NS_OK;
}

nsresult Http3Session::ProcessEvents() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  RefPtr<Http3Session> self(this);

  LOG(("Http3Session::ProcessEvents [this=%p]", this));

  // We need an array to pick up header data or a resumption token.
  nsTArray<uint8_t> data;
  Http3Event event{};
  event.tag = Http3Event::Tag::NoEvent;

  nsresult rv = mHttp3Connection->GetEvent(&event, data);
  if (NS_FAILED(rv)) {
    LOG(("Http3Session::ProcessEvents [this=%p] rv=%" PRIx32, this,
         static_cast<uint32_t>(rv)));
    return rv;
  }

  while (event.tag != Http3Event::Tag::NoEvent) {
    switch (event.tag) {
      case Http3Event::Tag::HeaderReady: {
        MOZ_ASSERT(mState == CONNECTED);
        LOG(("Http3Session::ProcessEvents - HeaderReady"));
        uint64_t id = event.header_ready.stream_id;

        RefPtr<Http3StreamBase> stream = mStreamIdHash.Get(id);
        if (!stream) {
          LOG(
              ("Http3Session::ProcessEvents - HeaderReady - stream not found "
               "stream_id=0x%" PRIx64 " [this=%p].",
               id, this));
          break;
        }

        MOZ_RELEASE_ASSERT(stream->GetHttp3Stream(),
                           "This must be a Http3Stream");

        stream->SetResponseHeaders(data, event.header_ready.fin,
                                   event.header_ready.interim);

        RefPtr<Http3Stream> http3Stream = stream->GetHttp3Stream();
        MOZ_RELEASE_ASSERT(http3Stream, "This must be a Http3Stream");
        RefPtr<nsAHttpTransaction> trans = http3Stream->Transaction();
        nsHttpTransaction* httpTrans =
            trans ? trans->QueryHttpTransaction() : nullptr;
        if (httpTrans) {
          if (event.header_ready.interim) {
            if (httpTrans->GetFirstInterimResponseStart().IsNull()) {
              auto now = TimeStamp::Now();
              httpTrans->SetFirstInterimResponseStart(now, true);
              httpTrans->SetResponseStart(now, false);
            }
          } else {
            auto now = TimeStamp::Now();
            httpTrans->SetFinalResponseHeadersStart(now, true);
            TimeStamp firstInterim = httpTrans->GetFirstInterimResponseStart();
            if (!firstInterim.IsNull()) {
              httpTrans->SetResponseStart(firstInterim, false);
            } else {
              httpTrans->SetResponseStart(now, false);
            }
          }
        }

        rv = ProcessTransactionRead(stream);

        if (NS_FAILED(rv)) {
          LOG(("Http3Session::ProcessEvents [this=%p] rv=%" PRIx32, this,
               static_cast<uint32_t>(rv)));
          return rv;
        }

        mUdpConn->NotifyDataRead();
        break;
      }
      case Http3Event::Tag::DataReadable: {
        MOZ_ASSERT(mState == CONNECTED);
        LOG(("Http3Session::ProcessEvents - DataReadable"));
        uint64_t id = event.data_readable.stream_id;

        nsresult rv = ProcessTransactionRead(id);

        if (NS_FAILED(rv)) {
          LOG(("Http3Session::ProcessEvents [this=%p] rv=%" PRIx32, this,
               static_cast<uint32_t>(rv)));
          return rv;
        }
        break;
      }
      case Http3Event::Tag::DataWritable: {
        MOZ_ASSERT(CanSendData());
        LOG(("Http3Session::ProcessEvents - DataWritable"));

        RefPtr<Http3StreamBase> stream =
            mStreamIdHash.Get(event.data_writable.stream_id);

        if (stream) {
          StreamReadyToWrite(stream);
          stream->SetBlockedByFlowControl(false);
        }
      } break;
      case Http3Event::Tag::Reset:
        LOG(("Http3Session::ProcessEvents %p - Reset", this));
        ResetOrStopSendingRecvd(event.reset.stream_id, event.reset.error,
                                RESET);
        break;
      case Http3Event::Tag::StopSending:
        LOG(
            ("Http3Session::ProcessEvents %p - StopSeniding with error "
             "0x%" PRIx64,
             this, event.stop_sending.error));
        if (event.stop_sending.error == HTTP3_APP_ERROR_NO_ERROR) {
          RefPtr<Http3StreamBase> stream =
              mStreamIdHash.Get(event.stop_sending.stream_id);
          if (stream) {
            if (RefPtr<Http3Stream> httpStream = stream->GetHttp3Stream()) {
              httpStream->StopSending();
            } else {
              ResetOrStopSendingRecvd(event.stop_sending.stream_id,
                                      event.stop_sending.error, STOP_SENDING);
            }
          }
        } else {
          ResetOrStopSendingRecvd(event.stop_sending.stream_id,
                                  event.stop_sending.error, STOP_SENDING);
        }
        break;
      case Http3Event::Tag::PushPromise:
        LOG(("Http3Session::ProcessEvents - PushPromise"));
        break;
      case Http3Event::Tag::PushHeaderReady:
        LOG(("Http3Session::ProcessEvents - PushHeaderReady"));
        break;
      case Http3Event::Tag::PushDataReadable:
        LOG(("Http3Session::ProcessEvents - PushDataReadable"));
        break;
      case Http3Event::Tag::PushCanceled:
        LOG(("Http3Session::ProcessEvents - PushCanceled"));
        break;
      case Http3Event::Tag::RequestsCreatable:
        LOG(("Http3Session::ProcessEvents - StreamCreatable"));
        ProcessPending();
        break;
      case Http3Event::Tag::AuthenticationNeeded:
        LOG(("Http3Session::ProcessEvents - AuthenticationNeeded %d",
             mAuthenticationStarted));
        if (!mAuthenticationStarted) {
          mAuthenticationStarted = true;
          LOG(("Http3Session::ProcessEvents - AuthenticationNeeded called"));
          CallCertVerification(Nothing());
        }
        break;
      case Http3Event::Tag::ZeroRttRejected:
        LOG(("Http3Session::ProcessEvents - ZeroRttRejected"));
        if (mState == ZERORTT) {
          mState = INITIALIZING;
          mTransactionCount = 0;
          Finish0Rtt(true);
          if (IsClosing()) {
            break;
          }
          ZeroRttTelemetry(ZeroRttOutcome::USED_REJECTED);
        }
        break;
      case Http3Event::Tag::ResumptionToken: {
        LOG(("Http3Session::ProcessEvents - ResumptionToken"));
        if (StaticPrefs::network_http_http3_enable_0rtt() && !data.IsEmpty()) {
          LOG(("Got a resumption token"));
          nsAutoCString peerId;
          mSocketControl->GetPeerId(peerId);
          if (NS_FAILED(SSLTokensCache::Put(
                  peerId, data.Elements(), data.Length(), mSocketControl,
                  PR_Now() + event.resumption_token.expire_in))) {
            LOG(("Adding resumption token failed"));
          }
        }
      } break;
      case Http3Event::Tag::ConnectionConnected: {
        LOG(("Http3Session::ProcessEvents - ConnectionConnected"));
        if (IsClosing()) {
          break;
        }
        bool was0RTT = mState == ZERORTT;
        mState = CONNECTED;
        SetSecInfo();
        mSocketControl->HandshakeCompleted();
        if (was0RTT) {
          Finish0Rtt(false);
          if (IsClosing()) {
            break;
          }
          ZeroRttTelemetry(ZeroRttOutcome::USED_SUCCEEDED);
        }

        OnTransportStatus(nullptr, NS_NET_STATUS_CONNECTED_TO, 0);
        mUdpConn->OnConnected();
        ReportHttp3Connection();
        // Maybe call ResumeSend:
        // In case ZeroRtt has been used and it has been rejected, 2 events will
        // be received: ZeroRttRejected and ConnectionConnected. ZeroRttRejected
        // that will put all transaction into mReadyForWrite queue and it will
        // call MaybeResumeSend, but that will not have an effect because the
        // connection is ont in CONNECTED state. When ConnectionConnected event
        // is received call MaybeResumeSend to trigger reads for the
        // zero-rtt-rejected transactions.
        MaybeResumeSend();
      } break;
      case Http3Event::Tag::GoawayReceived:
        LOG(("Http3Session::ProcessEvents - GoawayReceived"));
        mUdpConn->SetCloseReason(ConnectionCloseReason::GO_AWAY);
        mGoawayReceived = true;
        break;
      case Http3Event::Tag::ConnectionClosing:
        LOG(("Http3Session::ProcessEvents - ConnectionClosing"));
        if (NS_SUCCEEDED(mError) && !IsClosing()) {
          mError = NS_ERROR_NET_HTTP3_PROTOCOL_ERROR;
          CloseConnectionTelemetry(event.connection_closing.error, true);

          auto isStatelessResetOrNoError = [](CloseError& aError) -> bool {
            if (aError.tag == CloseError::Tag::TransportInternalErrorOther &&
                aError.transport_internal_error_other._0 ==
                    TRANSPORT_ERROR_STATELESS_RESET) {
              return true;
            }
            if (aError.tag == CloseError::Tag::TransportError &&
                aError.transport_error._0 == 0) {
              return true;
            }
            if (aError.tag == CloseError::Tag::PeerError &&
                aError.peer_error._0 == 0) {
              return true;
            }
            if (aError.tag == CloseError::Tag::AppError &&
                aError.app_error._0 == HTTP3_APP_ERROR_NO_ERROR) {
              return true;
            }
            if (aError.tag == CloseError::Tag::PeerAppError &&
                aError.peer_app_error._0 == HTTP3_APP_ERROR_NO_ERROR) {
              return true;
            }
            return false;
          };
          if (isStatelessResetOrNoError(event.connection_closing.error)) {
            mError = NS_ERROR_NET_RESET;
          }
          if (event.connection_closing.error.tag == CloseError::Tag::EchRetry) {
            mSocketControl->SetRetryEchConfig(Substring(
                reinterpret_cast<const char*>(data.Elements()), data.Length()));
            mError = psm::GetXPCOMFromNSSError(SSL_ERROR_ECH_RETRY_WITH_ECH);
          }
        }
        return mError;
        break;
      case Http3Event::Tag::ConnectionClosed:
        LOG(("Http3Session::ProcessEvents - ConnectionClosed"));
        if (NS_SUCCEEDED(mError)) {
          mError = NS_ERROR_NET_TIMEOUT;
          mUdpConn->SetCloseReason(ConnectionCloseReason::IDLE_TIMEOUT);
          CloseConnectionTelemetry(event.connection_closed.error, false);
        }
        mIsClosedByNeqo = true;
        if (event.connection_closed.error.tag == CloseError::Tag::EchRetry) {
          mSocketControl->SetRetryEchConfig(Substring(
              reinterpret_cast<const char*>(data.Elements()), data.Length()));
          mError = psm::GetXPCOMFromNSSError(SSL_ERROR_ECH_RETRY_WITH_ECH);
        }
        LOG(("Http3Session::ProcessEvents - ConnectionClosed error=%" PRIx32,
             static_cast<uint32_t>(mError)));
        // We need to return here and let HttpConnectionUDP close the session.
        return mError;
        break;
      case Http3Event::Tag::EchFallbackAuthenticationNeeded: {
        nsCString echPublicName(reinterpret_cast<const char*>(data.Elements()),
                                data.Length());
        LOG(
            ("Http3Session::ProcessEvents - EchFallbackAuthenticationNeeded "
             "echPublicName=%s",
             echPublicName.get()));
        if (!mAuthenticationStarted) {
          mAuthenticationStarted = true;
          CallCertVerification(Some(echPublicName));
        }
      } break;
      case Http3Event::Tag::WebTransport: {
        switch (event.web_transport._0.tag) {
          case WebTransportEventExternal::Tag::Negotiated:
            LOG(("Http3Session::ProcessEvents - WebTransport %d",
                 event.web_transport._0.negotiated._0));
            FinishNegotiation(ExtendedConnectKind::WebTransport,
                              event.web_transport._0.negotiated._0);
            break;
          case WebTransportEventExternal::Tag::Session: {
            MOZ_ASSERT(mState == CONNECTED);

            uint64_t id = event.web_transport._0.session._0;
            LOG(
                ("Http3Session::ProcessEvents - WebTransport Session "
                 " sessionId=0x%" PRIx64,
                 id));
            RefPtr<Http3StreamBase> stream = mStreamIdHash.Get(id);
            if (!stream) {
              LOG(
                  ("Http3Session::ProcessEvents - WebTransport Session - "
                   "stream not found "
                   "stream_id=0x%" PRIx64 " [this=%p].",
                   id, this));
              break;
            }

            MOZ_RELEASE_ASSERT(stream->GetHttp3WebTransportSession(),
                               "It must be a WebTransport session");
            stream->SetResponseHeaders(data, false, false);

            rv = stream->WriteSegments();

            if (ASpdySession::SoftStreamError(rv) || stream->Done()) {
              LOG3(
                  ("Http3Session::ProcessSingleTransactionRead session=%p "
                   "stream=%p "
                   "0x%" PRIx64 " cleanup stream rv=0x%" PRIx32 " done=%d.\n",
                   this, stream.get(), stream->StreamId(),
                   static_cast<uint32_t>(rv), stream->Done()));
              // We need to keep the transaction, so we can use it to remove the
              // stream from mStreamTransactionHash.
              nsAHttpTransaction* trans = stream->Transaction();
              if (mStreamTransactionHash.Contains(trans)) {
                CloseStream(stream, (rv == NS_BINDING_RETARGETED)
                                        ? NS_BINDING_RETARGETED
                                        : NS_OK);
                mStreamTransactionHash.Remove(trans);
              } else {
                stream->GetHttp3WebTransportSession()->TransactionIsDone(
                    (rv == NS_BINDING_RETARGETED) ? NS_BINDING_RETARGETED
                                                  : NS_OK);
              }
              break;
            }

            if (NS_FAILED(rv) && rv != NS_BASE_STREAM_WOULD_BLOCK) {
              LOG(("Http3Session::ProcessEvents [this=%p] rv=%" PRIx32, this,
                   static_cast<uint32_t>(rv)));
              return rv;
            }
          } break;
          case WebTransportEventExternal::Tag::SessionClosed: {
            uint64_t id = event.web_transport._0.session_closed.stream_id;
            LOG(
                ("Http3Session::ProcessEvents - WebTransport SessionClosed "
                 " sessionId=0x%" PRIx64,
                 id));
            RefPtr<Http3StreamBase> stream = mStreamIdHash.Get(id);
            if (!stream) {
              LOG(
                  ("Http3Session::ProcessEvents - WebTransport SessionClosed - "
                   "stream not found "
                   "stream_id=0x%" PRIx64 " [this=%p].",
                   id, this));
              break;
            }

            RefPtr<Http3WebTransportSession> wt =
                stream->GetHttp3WebTransportSession();
            MOZ_RELEASE_ASSERT(wt, "It must be a WebTransport session");

            bool cleanly = false;

            // TODO we do not handle the case when a WebTransport session stream
            // is closed before headers are sent.
            SessionCloseReasonExternal& reasonExternal =
                event.web_transport._0.session_closed.reason;
            uint32_t status = 0;
            nsCString reason = ""_ns;
            if (reasonExternal.tag == SessionCloseReasonExternal::Tag::Error) {
              status = reasonExternal.error._0;
            } else if (reasonExternal.tag ==
                       SessionCloseReasonExternal::Tag::Status) {
              status = reasonExternal.status._0;
              cleanly = true;
            } else {
              status = reasonExternal.clean._0;
              reason.Assign(reinterpret_cast<const char*>(data.Elements()),
                            data.Length());
              cleanly = true;
            }
            LOG(("reason.tag=%u err=%u data=%s\n",
                 static_cast<uint32_t>(reasonExternal.tag), status,
                 reason.get()));
            // Get stats before the session is closed (spec requirement).
            // neqo drops the session as it processes the close, before we get
            // here to drain the event, so for a server-initiated close only
            // the connection-level counters are still available -- they cover
            // everything we report except the session's datagram counters.
            mozilla::dom::WebTransportStatsData stats;
            if (!mHttp3Connection->GetWebTransportSessionStats(id, stats)) {
              mHttp3Connection->GetWebTransportTransportStats(stats);
            }
            wt->OnSessionClosedWithStats(cleanly, status, reason, stats);
          } break;
          case WebTransportEventExternal::Tag::NewStream: {
            LOG(
                ("Http3Session::ProcessEvents - WebTransport NewStream "
                 "streamId=0x%" PRIx64 " sessionId=0x%" PRIx64,
                 event.web_transport._0.new_stream.stream_id,
                 event.web_transport._0.new_stream.session_id));
            uint64_t sessionId = event.web_transport._0.new_stream.session_id;
            RefPtr<Http3StreamBase> stream = mStreamIdHash.Get(sessionId);
            if (!stream) {
              LOG(
                  ("Http3Session::ProcessEvents - WebTransport NewStream - "
                   "session not found "
                   "sessionId=0x%" PRIx64 " [this=%p].",
                   sessionId, this));
              break;
            }

            RefPtr<Http3WebTransportSession> wt =
                stream->GetHttp3WebTransportSession();
            if (!wt) {
              break;
            }

            RefPtr<Http3WebTransportStream> wtStream =
                wt->OnIncomingWebTransportStream(
                    event.web_transport._0.new_stream.stream_type,
                    event.web_transport._0.new_stream.stream_id);
            if (!wtStream) {
              break;
            }

            // WebTransportStream is managed by Http3Session now.
            mWebTransportStreams.AppendElement(wtStream);
            mWebTransportStreamToSessionMap.InsertOrUpdate(wtStream->StreamId(),
                                                           wt->StreamId());
            mStreamIdHash.InsertOrUpdate(wtStream->StreamId(),
                                         std::move(wtStream));
          } break;
          case WebTransportEventExternal::Tag::Datagram: {
            LOG(
                ("Http3Session::ProcessEvents - "
                 "WebTransportEventExternal::Tag::Datagram [this=%p]",
                 this));
            uint64_t sessionId = event.web_transport._0.datagram.session_id;
            RefPtr<Http3StreamBase> stream = mStreamIdHash.Get(sessionId);
            if (!stream) {
              LOG(
                  ("Http3Session::ProcessEvents - WebTransport Datagram - "
                   "session not found "
                   "sessionId=0x%" PRIx64 " [this=%p].",
                   sessionId, this));
              break;
            }

            RefPtr<Http3WebTransportSession> wt =
                stream->GetHttp3WebTransportSession();
            if (!wt) {
              break;
            }

            wt->OnDatagramReceived(std::move(data));
          } break;
          case WebTransportEventExternal::Tag::Draining: {
            uint64_t sessionId = event.web_transport._0.draining.session_id;
            LOG(
                ("Http3Session::ProcessEvents - WebTransport Draining "
                 "sessionId=0x%" PRIx64,
                 sessionId));
            RefPtr<Http3StreamBase> stream = mStreamIdHash.Get(sessionId);
            if (!stream) {
              LOG(
                  ("Http3Session::ProcessEvents - WebTransport Draining - "
                   "session not found "
                   "sessionId=0x%" PRIx64 " [this=%p].",
                   sessionId, this));
              break;
            }

            RefPtr<Http3WebTransportSession> wt =
                stream->GetHttp3WebTransportSession();
            if (wt) {
              wt->OnSessionDraining();
            }
          } break;
        }
      } break;
      case Http3Event::Tag::ConnectUdp: {
        switch (event.connect_udp._0.tag) {
          case ConnectUdpEventExternal::Tag::Negotiated:
            LOG(("Http3Session::ProcessEvents - ConnectUdp Negotiated %d",
                 event.connect_udp._0.negotiated._0));
            FinishNegotiation(ExtendedConnectKind::ConnectUDP,
                              event.connect_udp._0.negotiated._0);
            break;
          case ConnectUdpEventExternal::Tag::Session: {
            MOZ_ASSERT(mState == CONNECTED);

            uint64_t id = event.connect_udp._0.session._0;
            LOG(
                ("Http3Session::ProcessEvents - ConnectUdp "
                 " streamId=0x%" PRIx64,
                 id));
            RefPtr<Http3StreamBase> stream = mStreamIdHash.Get(id);
            if (!stream) {
              LOG(
                  ("Http3Session::ProcessEvents - ConnectUdp Session - "
                   "stream not found "
                   "stream_id=0x%" PRIx64 " [this=%p].",
                   id, this));
              break;
            }

            MOZ_RELEASE_ASSERT(stream->GetHttp3ConnectUDPStream(),
                               "It must be a ConnectUdp session");
            stream->SetResponseHeaders(data, false, false);

            rv = stream->WriteSegments();

            LOG(("rv=%x", static_cast<uint32_t>(rv)));

            if (ASpdySession::SoftStreamError(rv) || stream->Done()) {
              LOG3(
                  ("Http3Session::ProcessSingleTransactionRead session=%p "
                   "stream=%p "
                   "0x%" PRIx64 " cleanup stream rv=0x%" PRIx32 " done=%d.\n",
                   this, stream.get(), stream->StreamId(),
                   static_cast<uint32_t>(rv), stream->Done()));
              // We need to keep the transaction, so we can use it to remove the
              // stream from mStreamTransactionHash.
              nsAHttpTransaction* trans = stream->Transaction();
              if (mStreamTransactionHash.Contains(trans)) {
                CloseStream(stream, (rv == NS_BINDING_RETARGETED)
                                        ? NS_BINDING_RETARGETED
                                        : NS_OK);
                mStreamTransactionHash.Remove(trans);
              } else {
                stream->GetHttp3ConnectUDPStream()->TransactionIsDone(
                    (rv == NS_BINDING_RETARGETED) ? NS_BINDING_RETARGETED
                                                  : NS_OK);
              }
              break;
            }

            if (NS_FAILED(rv) && rv != NS_BASE_STREAM_WOULD_BLOCK) {
              LOG(("Http3Session::ProcessEvents [this=%p] rv=%" PRIx32, this,
                   static_cast<uint32_t>(rv)));
              return rv;
            }
          } break;
          case ConnectUdpEventExternal::Tag::SessionClosed: {
            uint64_t id = event.connect_udp._0.session_closed.stream_id;
            LOG(
                ("Http3Session::ProcessEvents - connect_udp SessionClosed "
                 " sessionId=0x%" PRIx64,
                 id));
            RefPtr<Http3StreamBase> stream = mStreamIdHash.Get(id);
            if (!stream) {
              LOG(
                  ("Http3Session::ProcessEvents - connect_udp SessionClosed - "
                   "stream not found "
                   "stream_id=0x%" PRIx64 " [this=%p].",
                   id, this));
              break;
            }

            RefPtr<Http3ConnectUDPStream> connectUDPStream =
                stream->GetHttp3ConnectUDPStream();
            MOZ_RELEASE_ASSERT(connectUDPStream,
                               "It must be a ConnectUDP stream");

            // TODO we do not handle the case when a ConnectUDP session stream
            // is closed before headers are sent.
            SessionCloseReasonExternal& reasonExternal =
                event.connect_udp._0.session_closed.reason;
            uint32_t status = 0;
            nsCString reason = ""_ns;
            if (reasonExternal.tag == SessionCloseReasonExternal::Tag::Error) {
              status = reasonExternal.error._0;
            } else if (reasonExternal.tag ==
                       SessionCloseReasonExternal::Tag::Status) {
              status = reasonExternal.status._0;
            } else {
              status = reasonExternal.clean._0;
              reason.Assign(reinterpret_cast<const char*>(data.Elements()),
                            data.Length());
            }
            LOG(("reason.tag=%u err=%u data=%s\n",
                 static_cast<uint32_t>(reasonExternal.tag), status,
                 reason.get()));
            CloseStream(connectUDPStream,
                        status == 0 ? NS_OK : NS_ERROR_FAILURE);
          } break;
          case ConnectUdpEventExternal::Tag::Datagram:
            LOG(("Http3Session::ProcessEvents - ConnectUdp Datagram"));
            uint64_t streamId = event.connect_udp._0.datagram.session_id;
            RefPtr<Http3StreamBase> stream = mStreamIdHash.Get(streamId);
            if (!stream) {
              LOG(
                  ("Http3Session::ProcessEvents - ConnectUdp Datagram - "
                   "stream not found "
                   "streamId=0x%" PRIx64 " [this=%p].",
                   streamId, this));
              break;
            }

            RefPtr<Http3ConnectUDPStream> tunnelStream =
                stream->GetHttp3ConnectUDPStream();
            if (!tunnelStream) {
              break;
            }
            tunnelStream->OnDatagramReceived(std::move(data));
            break;
        }
      } break;
      default:
        break;
    }
    // Delete previous content of data
    data.ClearAndRetainStorage();
    rv = mHttp3Connection->GetEvent(&event, data);
    if (NS_FAILED(rv)) {
      LOG(("Http3Session::ProcessEvents [this=%p] rv=%" PRIx32, this,
           static_cast<uint32_t>(rv)));
      return rv;
    }
  }

  return NS_OK;
}  // namespace net

// This function may return a socket error.
// It will not return an error if socket error is
// NS_BASE_STREAM_WOULD_BLOCK.
// A Caller of this function will close the Http3 connection
// if this function returns an error.
// Callers are:
//   1) HttpConnectionUDP::OnQuicTimeoutExpired
//   2) HttpConnectionUDP::SendData ->
//      Http3Session::SendData
nsresult Http3Session::ProcessOutput(nsIUDPSocket* socket) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(mUdpConn);

  LOG(("Http3Session::ProcessOutput reader=%p, [this=%p]", mUdpConn.get(),
       this));

  if (!socket || socket->IsSocketClosed()) {
    MOZ_DIAGNOSTIC_ASSERT(false, "UDP socket should still be open");
    return NS_ERROR_UNEXPECTED;
  }

  if (mUseNSPRForIO) {
    mSocket = socket;
    nsresult rv = mHttp3Connection->ProcessOutputAndSendUseNSPRForIO(
        this,
        [](void* aContext, uint16_t aFamily, const uint8_t* aAddr,
           uint16_t aPort, const uint8_t* aData, uint32_t aLength) {
          Http3Session* self = (Http3Session*)aContext;

          uint32_t written = 0;
          NetAddr addr;
          if (NS_FAILED(RawBytesToNetAddr(aFamily, aAddr, aPort, &addr))) {
            return NS_OK;
          }

          LOG3(
              ("Http3Session::ProcessOutput sending packet with %u bytes to %s "
               "port=%d [this=%p].",
               aLength, addr.ToString().get(), aPort, self));

          nsresult rv =
              self->mSocket->SendWithAddress(&addr, aData, aLength, &written);

          LOG(("Http3Session::ProcessOutput sending packet rv=%d osError=%d",
               static_cast<int32_t>(rv), NS_FAILED(rv) ? PR_GetOSError() : 0));
          if (NS_FAILED(rv) && (rv != NS_BASE_STREAM_WOULD_BLOCK)) {
            if (rv == NS_ERROR_OUT_OF_MEMORY) {
              // NSPR maps ENOBUFS to PR_INSUFFICIENT_RESOURCES_ERROR, which
              // becomes NS_ERROR_OUT_OF_MEMORY. On macOS/BSD, ENOBUFS means
              // the NIC transmit queue is momentarily full.
              LOG(
                  ("Http3Session::ProcessOutput ENOBUFS (transient), dropping "
                   "datagram [this=%p]",
                   self));
            } else {
              self->mSocketError = rv;
              // If there was another error, return from here. We do not need to
              // set a timer, because we will close the connection.
              return rv;
            }
          }
          self->mTotalBytesWritten += aLength;
          self->mLastWriteTime = PR_IntervalNow();
          return NS_OK;
        },
        [](void* aContext, uint64_t timeout) {
          Http3Session* self = (Http3Session*)aContext;
          self->SetupTimer(timeout);
        });
    mSocket = nullptr;
    return rv;
  }

  // Not using NSPR.

  auto rv = mHttp3Connection->ProcessOutputAndSend(
      this, [](void* aContext, uint64_t timeout) {
        Http3Session* self = (Http3Session*)aContext;
        self->SetupTimer(timeout);
      });
  if (rv.result == NS_BASE_STREAM_WOULD_BLOCK) {
    // The OS buffer was full. Tell the UDP socket to poll for
    // write-availability.
    socket->EnableWritePoll();
  } else if (NS_FAILED(rv.result)) {
    mSocketError = rv.result;
    // If there was an error return from here. We do not need to set a timer,
    // because we will close the connection.
    return rv.result;
  }
  if (rv.bytes_written != 0) {
    mTotalBytesWritten += rv.bytes_written;
    mLastWriteTime = PR_IntervalNow();
    socket->AddOutputBytes(rv.bytes_written);
  }

  return NS_OK;
}

// This is only called when timer expires.
// It is called by HttpConnectionUDP::OnQuicTimeout.
// If tihs function returns an error OnQuicTimeout will handle the error
// properly and close the connection.
nsresult Http3Session::ProcessOutputAndEvents(nsIUDPSocket* socket) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  MOZ_ASSERT(mTimerShouldTrigger);

  // Check if session has been stuck in ZERORTT state for too long
  if (mState == ZERORTT) {
    MOZ_ASSERT(mZeroRttStarted);
    uint32_t timeout = StaticPrefs::network_http_http3_0rtt_timeout();
    if (timeout > 0) {
      TimeDuration elapsed = TimeStamp::Now() - mZeroRttStarted;
      if (elapsed.ToMilliseconds() > timeout) {
        LOG(
            ("Http3Session %p stuck in ZERORTT for %.2fms (timeout=%ums), "
             "closing connection",
             this, elapsed.ToMilliseconds(), timeout));
        return NS_ERROR_NET_TIMEOUT;
      }
    }
  }

  if (Telemetry::CanRecordPrereleaseData()) {
    auto now = TimeStamp::Now();
    if (mTimerShouldTrigger > now) {
      // See bug 1935459
      glean::http3::timer_delayed.AccumulateRawDuration(nullptr);
    } else {
      glean::http3::timer_delayed.AccumulateRawDuration(now -
                                                        mTimerShouldTrigger);
    }
  }

  mTimerShouldTrigger = TimeStamp();

  nsresult rv = SendData(socket);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return NS_OK;
}

NS_IMPL_ISUPPORTS(Http3Session::OnQuicTimeout, nsITimerCallback, nsINamed)

Http3Session::OnQuicTimeout::OnQuicTimeout(HttpConnectionUDP* aConnection)
    : mConnection(aConnection) {
  MOZ_ASSERT(mConnection);
}

NS_IMETHODIMP
Http3Session::OnQuicTimeout::Notify(nsITimer* timer) {
  mConnection->OnQuicTimeoutExpired();
  return NS_OK;
}

NS_IMETHODIMP
Http3Session::OnQuicTimeout::GetName(nsACString& aName) {
  aName.AssignLiteral("net::HttpConnectionUDP::OnQuicTimeout");
  return NS_OK;
}

void Http3Session::SetupTimer(uint64_t aTimeout) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  // UINT64_MAX indicated a no-op from neqo, which only happens when a
  // connection is in or going to be Closed state.
  if (aTimeout == UINT64_MAX) {
    return;
  }

  // If we're in ZERORTT state, ensure the timeout doesn't exceed the
  // 0-RTT timeout to prevent the session from being closed later than expected.
  if (mState == ZERORTT) {
    MOZ_ASSERT(mZeroRttStarted);
    uint32_t zeroRttTimeout = StaticPrefs::network_http_http3_0rtt_timeout();
    if (zeroRttTimeout > 0) {
      TimeDuration elapsed = TimeStamp::Now() - mZeroRttStarted;
      uint64_t remainingMs =
          static_cast<uint64_t>(zeroRttTimeout - elapsed.ToMilliseconds());

      if (elapsed.ToMilliseconds() < zeroRttTimeout && aTimeout > remainingMs) {
        LOG3(("Http3Session::SetupTimer capping timeout from %" PRIu64
              "ms to %" PRIu64 "ms (0-RTT timeout remaining) [this=%p].",
              aTimeout, remainingMs, this));
        aTimeout = remainingMs;
      }
    }
  }

  LOG3(
      ("Http3Session::SetupTimer to %" PRIu64 "ms [this=%p].", aTimeout, this));

  // Remember the time when the timer should trigger.
  mTimerShouldTrigger =
      TimeStamp::Now() + TimeDuration::FromMilliseconds(aTimeout);

  if (!mTimerCallback) {
    // We can keep the same callback object for all our lifetime.
    mTimerCallback = MakeRefPtr<OnQuicTimeout>(mUdpConn);
  }

  if (!mTimer) {
    // This can only fail on OOM and we'd crash.
    mTimer = NS_NewTimer();
  }

  DebugOnly<nsresult> rv = mTimer->InitWithCallback(mTimerCallback, aTimeout,
                                                    nsITimer::TYPE_ONE_SHOT);
  // There is no meaningful error handling we can do here. But an error here
  // should only be possible if the timer thread did already shut down.
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

bool Http3Session::AddStream(nsAHttpTransaction* aHttpTransaction,
                             int32_t aPriority,
                             nsIInterfaceRequestor* aCallbacks) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  nsHttpTransaction* trans = aHttpTransaction->QueryHttpTransaction();

  bool firstStream = false;
  if (!mConnection) {
    // Get the connection from the first transaction.
    mConnection = aHttpTransaction->Connection();
    firstStream = true;
  }

  // Make sure we report the connectStart
  auto reportConnectStart = MakeScopeExit([&] {
    if (firstStream) {
      OnTransportStatus(nullptr, NS_NET_STATUS_CONNECTING_TO, 0);
    }
  });

  if (IsClosing()) {
    LOG3(
        ("Http3Session::AddStream %p atrans=%p trans=%p session unusable - "
         "resched.\n",
         this, aHttpTransaction, trans));
    aHttpTransaction->SetConnection(nullptr);
    nsresult rv = gHttpHandler->InitiateTransaction(trans, trans->Priority());
    if (NS_FAILED(rv)) {
      LOG3(
          ("Http3Session::AddStream %p atrans=%p trans=%p failed to initiate "
           "transaction (0x%" PRIx32 ").\n",
           this, aHttpTransaction, trans, static_cast<uint32_t>(rv)));
    }
    return true;
  }

  aHttpTransaction->SetConnection(this);
  aHttpTransaction->OnActivated();
  // reset the read timers to wash away any idle time
  mLastWriteTime = PR_IntervalNow();

  ClassOfService cos;
  if (trans) {
    cos = trans->GetClassOfService();
  }

  Http3StreamBase* stream = nullptr;

  if (trans && mConnInfo->IsHttp3ProxyConnection() && !mIsInTunnel) {
    LOG3(("Http3Session::AddStream new connect-udp stream %p atrans=%p.\n",
          this, aHttpTransaction));
    stream = new Http3ConnectUDPStream(aHttpTransaction, this,
                                       NS_GetCurrentThread());
    if (mConnInfo->GetIsTrrServiceChannel()) {
      stream->GetHttp3ConnectUDPStream()->MarkAsTRRServiceChannel();
    }
  } else if (trans && trans->IsForWebTransport()) {
    LOG3(("Http3Session::AddStream new  WeTransport session %p atrans=%p.\n",
          this, aHttpTransaction));
    stream = new Http3WebTransportSession(aHttpTransaction, this);
    mHasWebTransportSession = true;
  } else {
    LOG3(("Http3Session::AddStream %p atrans=%p.\n", this, aHttpTransaction));
    stream = new Http3Stream(aHttpTransaction, this, cos, mCurrentBrowserId);
  }

  mStreamTransactionHash.InsertOrUpdate(aHttpTransaction, RefPtr{stream});

  // A transaction added before the handshake completes didn't necessarily go
  // through the pending queue, where the backup timer is normally started.
  // Without it, the transaction would wait forever if the handshake never
  // completes.
  if (!IsConnected()) {
    if (nsHttpTransaction* trans = aHttpTransaction->QueryHttpTransaction()) {
      trans->MaybeStartHttp3BackupTimer();
    }
  }

  if (mState == ZERORTT) {
    if (!stream->Do0RTT()) {
      LOG(("Http3Session %p will not get early data from Http3Stream %p", this,
           stream));
      if (!mCannotDo0RTTStreams.Contains(stream)) {
        mCannotDo0RTTStreams.AppendElement(stream);
      }
      if (stream->GetHttp3WebTransportSession()) {
        DeferIfNegotiating(ExtendedConnectKind::WebTransport, stream);
      } else if (stream->GetHttp3ConnectUDPStream()) {
        DeferIfNegotiating(ExtendedConnectKind::ConnectUDP, stream);
      }
      return true;
    }
    m0RTTStreams.AppendElement(stream);
  }

  if (stream->GetHttp3WebTransportSession()) {
    if (DeferIfNegotiating(ExtendedConnectKind::WebTransport, stream)) {
      return true;
    }
  } else if (stream->GetHttp3ConnectUDPStream()) {
    if (DeferIfNegotiating(ExtendedConnectKind::ConnectUDP, stream)) {
      return true;
    }
  }

  if (!mFirstHttpTransaction && !IsConnected()) {
    mFirstHttpTransaction = aHttpTransaction->QueryHttpTransaction();
    LOG3(("Http3Session::AddStream first session=%p trans=%p ", this,
          mFirstHttpTransaction.get()));
  }
  StreamReadyToWrite(stream);

  return true;
}

void Http3Session::SwapTransaction(nsAHttpTransaction* aOld,
                                   nsAHttpTransaction* aNew) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(aOld && aNew);
  RefPtr<Http3StreamBase> stream = mStreamTransactionHash.Get(aOld);
  if (!stream) {
    LOG3(("Http3Session::SwapTransaction %p aOld=%p not in hash", this, aOld));
    return;
  }
  LOG3(("Http3Session::SwapTransaction %p aOld=%p -> aNew=%p stream=%p", this,
        aOld, aNew, stream.get()));
  stream->SetTransaction(aNew);
  mStreamTransactionHash.Remove(aOld);
  mStreamTransactionHash.InsertOrUpdate(aNew, std::move(stream));
  // mFirstHttpTransaction is tracked by nsHttpTransaction pointer; if it
  // was the shim, replace it with the real txn too.
  if (mFirstHttpTransaction &&
      mFirstHttpTransaction.get() == aOld->QueryHttpTransaction()) {
    mFirstHttpTransaction = aNew->QueryHttpTransaction();
  }
}

bool Http3Session::DeferIfNegotiating(ExtendedConnectKind aKind,
                                      Http3StreamBase* aStream) {
  auto& st = ExtState(aKind);
  if (st.mStatus == NEGOTIATING) {
    if (!st.mWaiters.Contains(aStream)) {
      LOG(("waiting for negotiation"));
      st.mWaiters.AppendElement(aStream);
    }
    return true;
  }
  return false;
}

void Http3Session::FinishNegotiation(ExtendedConnectKind aKind, bool aSuccess) {
  auto& st = ExtState(aKind);
  if (st.mWaiters.IsEmpty()) {
    st.mStatus = aSuccess ? SUCCEEDED : FAILED;
    return;
  }

  MOZ_ASSERT(st.mStatus == NEGOTIATING);
  st.mStatus = aSuccess ? SUCCEEDED : FAILED;

  for (size_t i = 0; i < st.mWaiters.Length(); ++i) {
    if (st.mWaiters[i]) {
      mReadyForWrite.Push(st.mWaiters[i]);
    }
  }
  st.mWaiters.Clear();
  MaybeResumeSend();
}

bool Http3Session::CanReuse() {
  // TODO: we assume "pooling" is disabled here, so we don't allow this session
  // to be reused. "pooling" will be implemented in bug 1815735.
  return CanSendData() && !(mGoawayReceived || mShouldClose) &&
         !mHasWebTransportSession;
}

void Http3Session::QueueStream(Http3StreamBase* stream) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!stream->Queued());

  LOG3(("Http3Session::QueueStream %p stream %p queued.", this, stream));

  stream->SetQueued(true);
  mQueuedStreams.Push(stream);
}

void Http3Session::ProcessPending() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  RefPtr<Http3StreamBase> stream;
  while ((stream = mQueuedStreams.PopFront())) {
    LOG3(("Http3Session::ProcessPending %p stream %p woken from queue.", this,
          stream.get()));
    MOZ_ASSERT(stream->Queued());
    stream->SetQueued(false);
    mReadyForWrite.Push(stream);
  }
  MaybeResumeSend();
}

static void RemoveStreamFromQueue(Http3StreamBase* aStream,
                                  nsRefPtrDeque<Http3StreamBase>& queue) {
  size_t size = queue.GetSize();
  for (size_t count = 0; count < size; ++count) {
    RefPtr<Http3StreamBase> stream = queue.PopFront();
    if (stream != aStream) {
      queue.Push(stream);
    }
  }
}

void Http3Session::RemoveStreamFromQueues(Http3StreamBase* aStream) {
  RemoveStreamFromQueue(aStream, mReadyForWrite);
  RemoveStreamFromQueue(aStream, mQueuedStreams);
  mSlowConsumersReadyForRead.RemoveElement(aStream);
}

// This is called by Http3Stream::OnReadSegment.
// ProcessOutput will be called in Http3Session::ReadSegment that
// calls Http3Stream::OnReadSegment.
nsresult Http3Session::TryActivating(
    const nsACString& aMethod, const nsACString& aScheme,
    const nsACString& aAuthorityHeader, const nsACString& aPathQuery,
    const nsACString& aHeaders, uint64_t* aStreamId, Http3StreamBase* aStream) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(*aStreamId == UINT64_MAX);

  LOG(("Http3Session::TryActivating [stream=%p, this=%p state=%d]", aStream,
       this, mState));

  if (IsClosing()) {
    if (NS_FAILED(mError)) {
      return mError;
    }
    return NS_ERROR_FAILURE;
  }

  if (aStream->Queued()) {
    LOG3(("Http3Session::TryActivating %p stream=%p already queued.\n", this,
          aStream));
    return NS_BASE_STREAM_WOULD_BLOCK;
  }

  if (mState == ZERORTT) {
    if (!aStream->Do0RTT()) {
      // Stream can't do 0RTT - queue it for activation when the session
      // reaches CONNECTED state via Finish0Rtt.
      if (!mCannotDo0RTTStreams.Contains(aStream)) {
        LOG(("Http3Session %p queuing stream %p for post-0RTT activation", this,
             aStream));
        mCannotDo0RTTStreams.AppendElement(aStream);
      }
      return NS_BASE_STREAM_WOULD_BLOCK;
    }
    mHad0RttStream = true;
  }

  nsresult rv = NS_OK;
  // The order of these checks is important: Http3StreamTunnel inherits from
  // Http3Stream, so a tunnel will also match conditions for a regular stream.
  // Ensure we handle Http3StreamTunnel cases before generic Http3Stream logic.
  if (RefPtr<Http3StreamTunnel> streamTunnel =
          aStream->GetHttp3StreamTunnel()) {
    rv = mHttp3Connection->Connect(aAuthorityHeader, aHeaders, aStreamId, 3,
                                   false);
  } else if (RefPtr<Http3Stream> httpStream = aStream->GetHttp3Stream()) {
    rv = mHttp3Connection->Fetch(
        aMethod, aScheme, aAuthorityHeader, aPathQuery, aHeaders, aStreamId,
        httpStream->PriorityUrgency(), httpStream->PriorityIncremental());
  } else if (RefPtr<Http3ConnectUDPStream> udpStream =
                 aStream->GetHttp3ConnectUDPStream()) {
    if (DeferIfNegotiating(ExtendedConnectKind::ConnectUDP, aStream)) {
      return NS_BASE_STREAM_WOULD_BLOCK;
    }
    rv = mHttp3Connection->CreateConnectUdp(aAuthorityHeader, aPathQuery,
                                            aHeaders, aStreamId);
  } else {
    MOZ_RELEASE_ASSERT(aStream->GetHttp3WebTransportSession(),
                       "It must be a WebTransport session");
    // Don't call CreateWebTransport if we are still waiting for the negotiation
    // result.
    if (DeferIfNegotiating(ExtendedConnectKind::WebTransport, aStream)) {
      return NS_BASE_STREAM_WOULD_BLOCK;
    }
    rv = mHttp3Connection->CreateWebTransport(aAuthorityHeader, aPathQuery,
                                              aHeaders, aStreamId);
    if (NS_SUCCEEDED(rv)) {
      ApplyAnticipatedWebTransportStreams(aStream, *aStreamId);
    }
  }

  if (NS_FAILED(rv)) {
    LOG(("Http3Session::TryActivating returns error=0x%" PRIx32 "[stream=%p, "
         "this=%p]",
         static_cast<uint32_t>(rv), aStream, this));
    if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
      LOG3(
          ("Http3Session::TryActivating %p stream=%p no room for more "
           "concurrent streams\n",
           this, aStream));
      mTransactionsBlockedByStreamLimitCount++;
      if (mQueuedStreams.GetSize() == 0) {
        mBlockedByStreamLimitCount++;
      }
      QueueStream(aStream);
      return rv;
    }

    // Previously we always returned NS_OK here, which caused the
    // transaction to wait until the quic connection timed out
    // after which it was retried without quic.
    if (StaticPrefs::network_http_http3_fallback_to_h2_on_error()) {
      return NS_ERROR_HTTP2_FALLBACK_TO_HTTP1;
    }

    return rv;
  }

  LOG(("Http3Session::TryActivating streamId=0x%" PRIx64
       " for stream=%p [this=%p].",
       *aStreamId, aStream, this));

  MOZ_ASSERT(*aStreamId != UINT64_MAX);

  if (mTransactionCount > 0 && mStreamIdHash.IsEmpty()) {
    MOZ_ASSERT(mConnectionIdleStart);
    MOZ_ASSERT(mFirstStreamIdReuseIdleConnection.isNothing());

    mConnectionIdleEnd = TimeStamp::Now();
    mFirstStreamIdReuseIdleConnection = Some(*aStreamId);
  }
  mStreamIdHash.InsertOrUpdate(*aStreamId, RefPtr{aStream});
  mTransactionCount++;

  return NS_OK;
}

void Http3Session::ApplyAnticipatedWebTransportStreams(Http3StreamBase* aStream,
                                                       uint64_t aSessionId) {
  nsHttpTransaction* trans = aStream->Transaction()->QueryHttpTransaction();
  if (!trans) {
    return;
  }
  uint16_t uni = 0;
  uint16_t bidi = 0;
  trans->GetWebTransportAnticipatedStreams(&uni, &bidi);
  if (uni > 0) {
    SetWebTransportAnticipatedConcurrentIncomingUnidirectionalStreams(
        aSessionId, uni);
  }
  if (bidi > 0) {
    SetWebTransportAnticipatedConcurrentIncomingBidirectionalStreams(aSessionId,
                                                                     bidi);
  }
}

// This is called by Http3WebTransportStream::OnReadSegment.
// TODO: this function is almost the same as TryActivating().
// We should try to reduce the duplicate code.
nsresult Http3Session::TryActivatingWebTransportStream(
    uint64_t* aStreamId, Http3StreamBase* aStream) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(*aStreamId == UINT64_MAX);

  LOG(
      ("Http3Session::TryActivatingWebTransportStream [stream=%p, this=%p "
       "state=%d]",
       aStream, this, mState));

  if (IsClosing()) {
    if (NS_FAILED(mError)) {
      return mError;
    }
    return NS_ERROR_FAILURE;
  }

  if (aStream->Queued()) {
    LOG3(
        ("Http3Session::TryActivatingWebTransportStream %p stream=%p already "
         "queued.\n",
         this, aStream));
    return NS_BASE_STREAM_WOULD_BLOCK;
  }

  nsresult rv = NS_OK;
  RefPtr<Http3WebTransportStream> wtStream =
      aStream->GetHttp3WebTransportStream();
  MOZ_RELEASE_ASSERT(wtStream, "It must be a WebTransport stream");
  rv = mHttp3Connection->CreateWebTransportStream(
      wtStream->SessionId(), wtStream->StreamType(), aStreamId);

  if (NS_FAILED(rv)) {
    LOG((
        "Http3Session::TryActivatingWebTransportStream returns error=0x%" PRIx32
        "[stream=%p, "
        "this=%p]",
        static_cast<uint32_t>(rv), aStream, this));
    if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
      LOG3(
          ("Http3Session::TryActivatingWebTransportStream %p stream=%p no room "
           "for more "
           "concurrent streams\n",
           this, aStream));
      QueueStream(aStream);
      return rv;
    }

    return rv;
  }

  LOG(("Http3Session::TryActivatingWebTransportStream streamId=0x%" PRIx64
       " for stream=%p [this=%p].",
       *aStreamId, aStream, this));

  MOZ_ASSERT(*aStreamId != UINT64_MAX);

  RefPtr<Http3StreamBase> session = mStreamIdHash.Get(wtStream->SessionId());
  MOZ_ASSERT(session);
  Http3WebTransportSession* wtSession = session->GetHttp3WebTransportSession();
  MOZ_ASSERT(wtSession);

  wtSession->RemoveWebTransportStream(wtStream);

  // WebTransportStream is managed by Http3Session now.
  mWebTransportStreams.AppendElement(wtStream);
  mWebTransportStreamToSessionMap.InsertOrUpdate(*aStreamId,
                                                 session->StreamId());
  mStreamIdHash.InsertOrUpdate(*aStreamId, std::move(wtStream));
  return NS_OK;
}

// This is only called by Http3Stream::OnReadSegment.
// ProcessOutput will be called in Http3Session::ReadSegment that
// calls Http3Stream::OnReadSegment.
void Http3Session::CloseSendingSide(uint64_t aStreamId) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  mHttp3Connection->CloseStream(aStreamId);
}

// This is only called by Http3Stream::OnReadSegment.
// ProcessOutput will be called in Http3Session::ReadSegment that
// calls Http3Stream::OnReadSegment.
nsresult Http3Session::SendRequestBody(uint64_t aStreamId, const char* buf,
                                       uint32_t count, uint32_t* countRead) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  nsresult rv = mHttp3Connection->SendRequestBody(
      aStreamId, (const uint8_t*)buf, count, countRead);
  if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
    mTransactionsSenderBlockedByFlowControlCount++;
  } else if (NS_FAILED(rv)) {
    // Ignore this error. This may happen if some events are not handled yet.
    // TODO we may try to add an assertion here.
    // We will pretend that sender is blocked, that will cause the caller to
    // stop trying.
    *countRead = 0;
    rv = NS_BASE_STREAM_WOULD_BLOCK;
  }

  MOZ_ASSERT((*countRead != 0) || NS_FAILED(rv));
  return rv;
}

void Http3Session::ResetOrStopSendingRecvd(uint64_t aStreamId, uint64_t aError,
                                           ResetType aType) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  uint64_t sessionId = 0;
  if (mWebTransportStreamToSessionMap.Get(aStreamId, &sessionId)) {
    Maybe<uint32_t> appError = Http3ErrorToWebTransportError(aError);
    // The nsresult carrier only holds 8 bits.
    nsresult rv = GetNSResultFromWebTransportError(
        appError && *appError <= UINT8_MAX ? static_cast<uint8_t>(*appError)
                                           : 0);
    // A code that is no application error code is reported as one above 32
    // bits, which the listener turns into "no code".
    uint64_t wtError = appError ? *appError : UINT64_MAX;

    RefPtr<Http3StreamBase> stream = mStreamIdHash.Get(aStreamId);
    if (stream) {
      if (aType == RESET) {
        stream->SetRecvdReset();
      }
      RefPtr<Http3WebTransportStream> wtStream =
          stream->GetHttp3WebTransportStream();
      if (wtStream) {
        if (wtStream->StreamType() == WebTransportStreamType::BiDi) {
          if (aType == RESET) {
            wtStream->CloseReceiveSide(rv);
          } else {
            wtStream->CloseSendSide(rv);
          }
          // The stream goes away once both halves are done.
          if (wtStream->Done()) {
            CloseWebTransportStream(wtStream, rv);
          }
        } else {
          CloseWebTransportStream(wtStream, rv);
        }
      }
    }

    RefPtr<Http3StreamBase> session = mStreamIdHash.Get(sessionId);
    if (session) {
      Http3WebTransportSession* wtSession =
          session->GetHttp3WebTransportSession();
      MOZ_ASSERT(wtSession);
      if (wtSession) {
        if (aType == RESET) {
          wtSession->OnStreamReset(aStreamId, wtError);
        } else {
          wtSession->OnStreamStopSending(aStreamId, wtError);
        }
      }
    }
    return;
  }

  RefPtr<Http3StreamBase> stream = mStreamIdHash.Get(aStreamId);
  if (!stream) {
    return;
  }

  RefPtr<Http3Stream> httpStream = stream->GetHttp3Stream();
  if (!httpStream) {
    return;
  }

  // We only handle some of Http3 error as epecial, the rest are just equivalent
  // to cancel.
  if (aError == HTTP3_APP_ERROR_VERSION_FALLBACK) {
    // We will restart the request and the alt-svc will be removed
    // automatically.
    // Also disable http3 we want http1.1.
    httpStream->Transaction()->DisableHttp3(false);
    httpStream->Transaction()->DisableSpdy();
    CloseStream(stream, NS_ERROR_NET_RESET);
  } else if (aError == HTTP3_APP_ERROR_REQUEST_REJECTED) {
    // This request was rejected because server is probably busy or going away.
    // We can restart the request using alt-svc. Without calling
    // DoNotRemoveAltSvc the alt-svc route will be removed.
    httpStream->Transaction()->DoNotRemoveAltSvc();
    CloseStream(stream, NS_ERROR_NET_RESET);
  } else if (aError == HTTP3_APP_ERROR_REQUEST_CANCELLED) {
    // The server cancelled this request; it may have had side effects, so
    // do not retry.
    CloseStream(stream, httpStream->RecvdData() ? NS_ERROR_NET_PARTIAL_TRANSFER
                                                : NS_ERROR_NET_INTERRUPT);
  } else {
    // Unrecognized application error code. Use NS_ERROR_NET_RESET so the
    // transaction restart path retries via H2/H1.
    CloseStream(stream, httpStream->RecvdData() ? NS_ERROR_NET_PARTIAL_TRANSFER
                                                : NS_ERROR_NET_RESET);
  }
}

void Http3Session::SetConnection(nsAHttpConnection* aConn) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  mConnection = aConn;
}

void Http3Session::GetSecurityCallbacks(nsIInterfaceRequestor** aOut) {
  *aOut = nullptr;
}

// TODO
void Http3Session::OnTransportStatus(nsITransport* aTransport, nsresult aStatus,
                                     int64_t aProgress) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  switch (aStatus) {
      // These should appear only once, deliver to the first
      // transaction on the session.
    case NS_NET_STATUS_RESOLVING_HOST:
    case NS_NET_STATUS_RESOLVED_HOST:
    case NS_NET_STATUS_CONNECTING_TO:
    case NS_NET_STATUS_CONNECTED_TO: {
      if (!mFirstHttpTransaction) {
        // if we still do not have a HttpTransaction store timings info in
        // a HttpConnection.
        // If some error occur it can happen that we do not have a connection.
        if (mConnection) {
          RefPtr<HttpConnectionBase> conn = mConnection->HttpConnection();
          conn->SetEvent(aStatus);
        }
      } else {
        mFirstHttpTransaction->OnTransportStatus(aTransport, aStatus,
                                                 aProgress);
      }

      if (aStatus == NS_NET_STATUS_CONNECTED_TO) {
        mFirstHttpTransaction = nullptr;
      }
      break;
    }

    default:
      // The other transport events are ignored here because there is no good
      // way to map them to the right transaction in HTTP3. Instead, the events
      // are generated again from the HTTP3 code and passed directly to the
      // correct transaction.

      // NS_NET_STATUS_SENDING_TO:
      // This is generated by the socket transport when (part) of
      // a transaction is written out
      //
      // There is no good way to map it to the right transaction in HTTP3,
      // so it is ignored here and generated separately when the request
      // is sent from Http3Stream.

      // NS_NET_STATUS_WAITING_FOR:
      // Created by nsHttpConnection when the request has been totally sent.
      // There is no good way to map it to the right transaction in HTTP3,
      // so it is ignored here and generated separately when the same
      // condition is complete in Http3Stream when there is no more
      // request body left to be transmitted.

      // NS_NET_STATUS_RECEIVING_FROM
      // Generated in Http3Stream whenever the stream reads data.

      break;
  }
}

bool Http3Session::IsDone() { return mState == CLOSED; }

nsresult Http3Session::Status() {
  MOZ_ASSERT(false, "Http3Session::Status()");
  return NS_ERROR_UNEXPECTED;
}

uint32_t Http3Session::Caps() {
  MOZ_ASSERT(false, "Http3Session::Caps()");
  return 0;
}

nsresult Http3Session::ReadSegments(nsAHttpSegmentReader* reader,
                                    uint32_t count, uint32_t* countRead) {
  MOZ_ASSERT(false, "Http3Session::ReadSegments()");
  return NS_ERROR_UNEXPECTED;
}

nsresult Http3Session::SendData(nsIUDPSocket* socket) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  RefPtr<Http3Session> self(this);

  LOG(("Http3Session::SendData [this=%p]", this));

  //   1) go through all streams/transactions that are ready to write and
  //      write their data into quic streams (no network write yet).
  //   2) call ProcessOutput that will loop until all available packets are
  //      written to a socket or the socket returns an error code.
  //   3) if we still have streams ready to write call ResumeSend()(we may
  //      still have such streams because on an stream error we return earlier
  //      to let the error be handled).
  //   4)

  nsresult rv = NS_OK;
  RefPtr<Http3StreamBase> stream;

  nsTArray<RefPtr<Http3StreamBase>> blockedStreams;

  // Step 1)
  while (CanSendData() && (stream = mReadyForWrite.PopFront())) {
    LOG(("Http3Session::SendData call ReadSegments from stream=%p [this=%p]",
         stream.get(), this));
    stream->SetInTxQueue(false);
    if (stream->BlockedByFlowControl()) {
      LOG(("stream %p blocked by flow control", stream.get()));
      blockedStreams.AppendElement(stream);
      continue;
    }
    rv = stream->ReadSegments();

    // on stream error we return earlier to let the error be handled.
    if (NS_FAILED(rv)) {
      LOG3(("Http3Session::SendData %p returns error code 0x%" PRIx32, this,
            static_cast<uint32_t>(rv)));
      MOZ_ASSERT(rv != NS_BASE_STREAM_WOULD_BLOCK);
      if (rv == NS_BASE_STREAM_WOULD_BLOCK) {  // Just in case!
        rv = NS_OK;
      } else if (ASpdySession::SoftStreamError(rv)) {
        CloseStream(stream, rv);
        LOG3(("Http3Session::SendData %p soft error override\n", this));
        rv = NS_OK;
      } else {
        break;
      }
    }
  }

  if (NS_SUCCEEDED(rv)) {
    // Step 2:
    // Call actual network write.
    rv = ProcessOutput(socket);
  }

  // Step 3:
  MaybeResumeSend();

  if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
    rv = NS_OK;
  }

  if (NS_FAILED(rv)) {
    return rv;
  }

  // Put the blocked streams back to the queue, since they are ready to write.
  for (const auto& stream : blockedStreams) {
    mReadyForWrite.Push(stream);
    stream->SetInTxQueue(true);
  }

  rv = ProcessEvents();

  // Let the connection know we sent some app data successfully.
  if (stream && NS_SUCCEEDED(rv)) {
    mUdpConn->NotifyDataWrite();
  }

  return rv;
}

void Http3Session::StreamReadyToWrite(Http3StreamBase* aStream) {
  MOZ_ASSERT(aStream);
  // Http3Session::StreamReadyToWrite can be called multiple times when we get
  // duplicate DataWrite events from neqo at the same time. In this case, we
  // only want to insert the stream in `mReadyForWrite` once.
  if (aStream->IsInTxQueue()) {
    return;
  }

  mReadyForWrite.Push(aStream);
  aStream->SetInTxQueue(true);
  if (CanSendData() && mConnection) {
    (void)mConnection->ResumeSend();
  }
}

void Http3Session::MaybeResumeSend() {
  if ((mReadyForWrite.GetSize() > 0) && CanSendData() && mConnection) {
    (void)mConnection->ResumeSend();
  }
}

nsresult Http3Session::ProcessSlowConsumers() {
  if (mSlowConsumersReadyForRead.IsEmpty()) {
    return NS_OK;
  }

  RefPtr<Http3StreamBase> slowConsumer =
      mSlowConsumersReadyForRead.ElementAt(0);
  mSlowConsumersReadyForRead.RemoveElementAt(0);

  nsresult rv = ProcessTransactionRead(slowConsumer);

  return rv;
}

nsresult Http3Session::WriteSegments(nsAHttpSegmentWriter* writer,
                                     uint32_t count, uint32_t* countWritten) {
  MOZ_ASSERT(false, "Http3Session::WriteSegments()");
  return NS_ERROR_UNEXPECTED;
}

nsresult Http3Session::RecvData(nsIUDPSocket* socket) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  RefPtr<Http3Session> self(this);

  // Process slow consumers.
  nsresult rv = ProcessSlowConsumers();
  if (NS_FAILED(rv)) {
    LOG3(("Http3Session %p ProcessSlowConsumers returns 0x%" PRIx32 "\n", this,
          static_cast<uint32_t>(rv)));
    return rv;
  }

  rv = ProcessInput(socket);
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = ProcessEvents();
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = SendData(socket);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_OK;
}

const uint32_t HTTP3_TELEMETRY_APP_NECKO = 42;

void Http3Session::Close(nsresult aReason) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  LOG(("Http3Session::Close [this=%p]", this));

  if (NS_FAILED(mError)) {
    CloseInternal(false);
  } else {
    mError = aReason;
    // If necko closes connection, this will map to the "closing" key and the
    // value HTTP3_TELEMETRY_APP_NECKO.
    glean::http3::connection_close_code.Get("app_closing"_ns)
        .AccumulateSingleSample(HTTP3_TELEMETRY_APP_NECKO);
    CloseInternal(true);
  }

  if (mCleanShutdown || mIsClosedByNeqo || NS_FAILED(mSocketError)) {
    // It is network-tear-down, a socker error or neqo is state CLOSED
    // (it does not need to send any more packets or wait for new packets).
    // We need to remove all references, so that
    // Http3Session will be destroyed.
    if (mTimer) {
      mTimer->Cancel();
    }
    mTimer = nullptr;
    mTimerCallback = nullptr;
    mConnection = nullptr;
    mUdpConn = nullptr;
    mState = CLOSED;
  }
  if (mConnection) {
    // resume sending to send CLOSE_CONNECTION frame.
    (void)mConnection->ResumeSend();
  }
}

void Http3Session::CloseInternal(bool aCallNeqoClose) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (IsClosing()) {
    return;
  }

  LOG(("Http3Session::Closing [this=%p]", this));

  // A clean pre-CONNECTED shutdown closes with a success code; only flag a
  // before-connected error when mError actually failed.
  if (mState != CONNECTED && NS_FAILED(mError)) {
    mBeforeConnectedError = true;
  }

  if (mState == ZERORTT) {
    ZeroRttTelemetry(aCallNeqoClose ? ZeroRttOutcome::USED_CONN_CLOSED_BY_NECKO
                                    : ZeroRttOutcome::USED_CONN_ERROR);
  }

  mState = CLOSING;
  Shutdown();

  if (aCallNeqoClose) {
    mHttp3Connection->Close(HTTP3_APP_ERROR_NO_ERROR);
  }

  mStreamIdHash.Clear();
  mStreamTransactionHash.Clear();
}

nsHttpConnectionInfo* Http3Session::ConnectionInfo() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  RefPtr<nsHttpConnectionInfo> ci;
  GetConnectionInfo(getter_AddRefs(ci));
  return ci.get();
}

void Http3Session::SetProxyConnectFailed() {
  MOZ_ASSERT(false, "Http3Session::SetProxyConnectFailed()");
}

const nsHttpRequestHead* Http3Session::RequestHead() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(false,
             "Http3Session::RequestHead() "
             "should not be called after http/3 is setup");
  return nullptr;
}

uint32_t Http3Session::Http1xTransactionCount() { return 0; }

nsresult Http3Session::TakeSubTransactions(
    nsTArray<RefPtr<nsAHttpTransaction>>& outTransactions) {
  return NS_OK;
}

//-----------------------------------------------------------------------------
// Pass through methods of nsAHttpConnection
//-----------------------------------------------------------------------------

nsAHttpConnection* Http3Session::Connection() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  return mConnection;
}

nsresult Http3Session::OnHeadersAvailable(nsAHttpTransaction* transaction,
                                          nsHttpRequestHead* requestHead,
                                          nsHttpResponseHead* responseHead,
                                          bool* reset) {
  MOZ_ASSERT(mConnection);
  if (mConnection) {
    return mConnection->OnHeadersAvailable(transaction, requestHead,
                                           responseHead, reset);
  }
  return NS_OK;
}

bool Http3Session::IsReused() {
  if (mConnection) {
    return mConnection->IsReused();
  }
  return true;
}

nsresult Http3Session::PushBack(const char* buf, uint32_t len) {
  return NS_ERROR_UNEXPECTED;
}

already_AddRefed<HttpConnectionBase> Http3Session::TakeHttpConnection() {
  LOG(("Http3Session::TakeHttpConnection %p", this));
  return nullptr;
}

already_AddRefed<HttpConnectionBase> Http3Session::HttpConnection() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (mConnection) {
    return mConnection->HttpConnection();
  }
  return nullptr;
}

void Http3Session::CloseTransaction(nsAHttpTransaction* aTransaction,
                                    nsresult aResult) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http3Session::CloseTransaction %p %p 0x%" PRIx32, this, aTransaction,
        static_cast<uint32_t>(aResult)));

  // Generally this arrives as a cancel event from the connection manager.

  // need to find the stream and call CloseStream() on it.
  RefPtr<Http3StreamBase> stream = mStreamTransactionHash.Get(aTransaction);
  if (!stream) {
    LOG3(("Http3Session::CloseTransaction %p %p 0x%" PRIx32 " - not found.",
          this, aTransaction, static_cast<uint32_t>(aResult)));
    return;
  }
  LOG3(
      ("Http3Session::CloseTransaction probably a cancel. this=%p, "
       "trans=%p, result=0x%" PRIx32 ", streamId=0x%" PRIx64 " stream=%p",
       this, aTransaction, static_cast<uint32_t>(aResult), stream->StreamId(),
       stream.get()));
  CloseStream(stream, aResult);
  if (mConnection) {
    (void)mConnection->ResumeSend();
  }
}

void Http3Session::CloseStream(Http3StreamBase* aStream, nsresult aResult) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (aStream->Closed()) {
    LOG(("Http3Session::CloseStream aStream %p already closed", aStream));
    return;
  }

  RefPtr<Http3WebTransportStream> wtStream =
      aStream->GetHttp3WebTransportStream();
  if (wtStream) {
    CloseWebTransportStream(wtStream, aResult);
    return;
  }

  RefPtr<Http3Stream> httpStream = aStream->GetHttp3Stream();
  if (httpStream && !httpStream->RecvdFin() && !httpStream->RecvdReset() &&
      httpStream->HasStreamId()) {
    mHttp3Connection->CancelFetch(httpStream->StreamId(),
                                  HTTP3_APP_ERROR_REQUEST_CANCELLED);
  }

  if ((NS_SUCCEEDED(aResult) || NS_BASE_STREAM_CLOSED == aResult) &&
      mConnInfo->GetIsTrrServiceChannel()) {
    // save time of last successful response
    mLastTRRResponseTime = TimeStamp::Now();
    mTrrStreams++;
  }

  aStream->Close(aResult);
  CloseStreamInternal(aStream, aResult);
}

void Http3Session::CloseStreamInternal(Http3StreamBase* aStream,
                                       nsresult aResult) {
  LOG3(("Http3Session::CloseStreamInternal %p %p 0x%" PRIx32, this, aStream,
        static_cast<uint32_t>(aResult)));
  if (aStream->HasStreamId()) {
    // We know the transaction reusing an idle connection has succeeded or
    // failed.
    if (mFirstStreamIdReuseIdleConnection.isSome() &&
        aStream->StreamId() == *mFirstStreamIdReuseIdleConnection) {
      MOZ_ASSERT(mConnectionIdleStart);
      MOZ_ASSERT(mConnectionIdleEnd);

#ifndef ANDROID
      if (mConnectionIdleStart) {
        mozilla::glean::netwerk::http3_time_to_reuse_idle_connection
            .Get(NS_SUCCEEDED(aResult) ? "succeeded"_ns : "failed"_ns)
            .AccumulateRawDuration(mConnectionIdleEnd - mConnectionIdleStart);
      }
#endif

      mConnectionIdleStart = TimeStamp();
      mConnectionIdleEnd = TimeStamp();
      mFirstStreamIdReuseIdleConnection.reset();
    }

    mStreamIdHash.Remove(aStream->StreamId());

    // Start to idle when we remove the last stream.
    if (mStreamIdHash.IsEmpty()) {
      mConnectionIdleStart = TimeStamp::Now();
    }
  }
  RemoveStreamFromQueues(aStream);
  if (nsAHttpTransaction* transaction = aStream->Transaction()) {
    mStreamTransactionHash.Remove(transaction);
  }
  mWebTransportSessions.RemoveElement(aStream);
  mWebTransportStreams.RemoveElement(aStream);
  mTunnelStreams.RemoveElement(aStream);
  // Close(NS_OK) implies that the NeqoHttp3Conn will be closed, so we can only
  // do this when there is no Http3Steeam, WebTransportSession and
  // WebTransportStream.
  if ((mShouldClose || mGoawayReceived) && HasNoActiveStreams()) {
    MOZ_ASSERT(!IsClosing());
    Close(NS_OK);
  }
}

void Http3Session::CloseWebTransportStream(Http3WebTransportStream* aStream,
                                           nsresult aResult) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http3Session::CloseWebTransportStream %p %p 0x%" PRIx32, this, aStream,
        static_cast<uint32_t>(aResult)));
  if (aStream && !aStream->RecvdFin() && !aStream->RecvdReset() &&
      !aStream->SentReset() && (aStream->HasStreamId())) {
    mHttp3Connection->ResetStream(aStream->StreamId(),
                                  HTTP3_APP_ERROR_REQUEST_CANCELLED);
  }

  aStream->Close(aResult);
  CloseStreamInternal(aStream, aResult);
}

void Http3Session::ResetWebTransportStream(Http3WebTransportStream* aStream,
                                           uint64_t aErrorCode) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http3Session::ResetWebTransportStream %p %p 0x%" PRIx64, this, aStream,
        aErrorCode));
  mHttp3Connection->ResetStream(aStream->StreamId(), aErrorCode);
}

void Http3Session::StreamStopSending(Http3WebTransportStream* aStream,
                                     uint64_t aErrorCode) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("Http3Session::StreamStopSending %p %p 0x%" PRIx64, this, aStream,
       aErrorCode));
  mHttp3Connection->StreamStopSending(aStream->StreamId(), aErrorCode);
}

nsresult Http3Session::TakeTransport(nsISocketTransport**,
                                     nsIAsyncInputStream**,
                                     nsIAsyncOutputStream**) {
  MOZ_ASSERT(false, "TakeTransport of Http3Session");
  return NS_ERROR_UNEXPECTED;
}

WebTransportSessionBase* Http3Session::GetWebTransportSession(
    nsAHttpTransaction* aTransaction) {
  RefPtr<Http3StreamBase> stream = mStreamTransactionHash.Get(aTransaction);

  if (!stream || !stream->GetHttp3WebTransportSession()) {
    MOZ_ASSERT(false, "There must be a stream");
    return nullptr;
  }
  RemoveStreamFromQueues(stream);
  mStreamTransactionHash.Remove(aTransaction);
  mWebTransportSessions.AppendElement(stream);
  return stream->GetHttp3WebTransportSession();
}

bool Http3Session::IsPersistent() { return true; }

void Http3Session::DontReuse() {
  LOG3(("Http3Session::DontReuse %p\n", this));
  if (!OnSocketThread()) {
    LOG3(("Http3Session %p not on socket thread\n", this));
    nsCOMPtr<nsIRunnable> event = NewRunnableMethod(
        "Http3Session::DontReuse", this, &Http3Session::DontReuse);
    if (StaticPrefs::network_trr_high_priority_events() &&
        mIsTrrServiceChannel) {
      event = new PrioritizableRunnable(
          event.forget(), nsIRunnablePriority::PRIORITY_MEDIUMHIGH);
    }
    gSocketTransportService->Dispatch(event, NS_DISPATCH_NORMAL);
    return;
  }

  if (mGoawayReceived || IsClosing()) {
    return;
  }

  mShouldClose = true;
  if (HasNoActiveStreams()) {
    // This is a temporary workaround and should be fixed properly in Happy
    // Eyeballs project. We should not exclude this domain if
    // Http3Session::DontReuse is called from
    // ConnectionEntry::MakeAllDontReuseExcept.
    if (mUdpConn &&
        mUdpConn->CloseReason() ==
            ConnectionCloseReason::CLOSE_EXISTING_CONN_FOR_COALESCING) {
      mDontExclude = true;
    }
    Close(NS_OK);
  }
}

void Http3Session::CloseWebTransportConn() {
  LOG3(("Http3Session::CloseWebTransportConn %p\n", this));
  // We need to dispatch, since Http3Session could be released in
  // HttpConnectionUDP::CloseTransaction.
  nsCOMPtr<nsIRunnable> event = NS_NewRunnableFunction(
      "Http3Session::CloseWebTransportConn", [self = RefPtr{this}]() {
        if (self->mUdpConn) {
          self->mUdpConn->CloseTransaction(self, NS_ERROR_ABORT);
        }
      });
  if (StaticPrefs::network_trr_high_priority_events() &&
      mConnInfo->GetIsTrrServiceChannel()) {
    event = new PrioritizableRunnable(event.forget(),
                                      nsIRunnablePriority::PRIORITY_MEDIUMHIGH);
  }
  gSocketTransportService->Dispatch(event.forget(), NS_DISPATCH_NORMAL);
}

void Http3Session::CurrentBrowserIdChanged(uint64_t id) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  mCurrentBrowserId = id;

  for (const auto& stream : mStreamTransactionHash.Values()) {
    RefPtr<Http3Stream> httpStream = stream->GetHttp3Stream();
    if (httpStream) {
      httpStream->CurrentBrowserIdChanged(id);
    }
  }
}

// This is called by Http3Stream::OnWriteSegment.
nsresult Http3Session::ReadResponseData(uint64_t aStreamId, char* aBuf,
                                        uint32_t aCount,
                                        uint32_t* aCountWritten, bool* aFin) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  nsresult rv = mHttp3Connection->ReadResponseData(aStreamId, (uint8_t*)aBuf,
                                                   aCount, aCountWritten, aFin);

  // A stream must be present in neqo and in necko at the same time, except
  // that a WebTransport stream is removed from neqo when it is reset (via
  // RESET_STREAM or STOP_SENDING) while the necko stream may still read.
  MOZ_ASSERT_IF(!mWebTransportStreamToSessionMap.Contains(aStreamId),
                rv != NS_ERROR_INVALID_ARG);
  if (NS_FAILED(rv)) {
    LOG3(("Http3Session::ReadResponseData return an error %" PRIx32
          " [this=%p]",
          static_cast<uint32_t>(rv), this));
    // For NS_ERROR_INVALID_ARG (stream not found in neqo), treat it as
    // no data available. For other errors, neqo will handle the error
    // and potentially close the connection.
    *aCountWritten = 0;
    *aFin = false;
    rv = NS_BASE_STREAM_WOULD_BLOCK;
  }

  MOZ_ASSERT((*aCountWritten != 0) || aFin || NS_FAILED(rv));
  return rv;
}

void Http3Session::TransactionHasDataToWrite(nsAHttpTransaction* caller) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http3Session::TransactionHasDataToWrite %p trans=%p", this, caller));

  // a trapped signal from the http transaction to the connection that
  // it is no longer blocked on read.

  RefPtr<Http3StreamBase> stream = mStreamTransactionHash.Get(caller);
  if (!stream) {
    LOG3(("Http3Session::TransactionHasDataToWrite %p caller %p not found",
          this, caller));
    return;
  }

  LOG3(("Http3Session::TransactionHasDataToWrite %p ID is 0x%" PRIx64, this,
        stream->StreamId()));

  StreamHasDataToWrite(stream);
}

void Http3Session::StreamHasDataToWrite(Http3StreamBase* aStream) {
  if (!IsClosing()) {
    StreamReadyToWrite(aStream);
  } else {
    LOG3(
        ("Http3Session::TransactionHasDataToWrite %p closed so not setting "
         "Ready4Write\n",
         this));
  }

  // NSPR poll will not poll the network if there are non system PR_FileDesc's
  // that are ready - so we can get into a deadlock waiting for the system IO
  // to come back here if we don't force the send loop manually.
  (void)ForceSend();
}

void Http3Session::TransactionHasDataToRecv(nsAHttpTransaction* caller) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http3Session::TransactionHasDataToRecv %p trans=%p", this, caller));

  // a signal from the http transaction to the connection that it will consume
  // more
  RefPtr<Http3StreamBase> stream = mStreamTransactionHash.Get(caller);
  if (!stream) {
    LOG3(("Http3Session::TransactionHasDataToRecv %p caller %p not found", this,
          caller));
    return;
  }

  LOG3(("Http3Session::TransactionHasDataToRecv %p ID is 0x%" PRIx64 "\n", this,
        stream->StreamId()));
  ConnectSlowConsumer(stream);
}

void Http3Session::ConnectSlowConsumer(Http3StreamBase* stream) {
  LOG3(("Http3Session::ConnectSlowConsumer %p 0x%" PRIx64 "\n", this,
        stream->StreamId()));
  mSlowConsumersReadyForRead.AppendElement(stream);
  (void)ForceRecv();
}

bool Http3Session::TestJoinConnection(const nsACString& hostname,
                                      int32_t port) {
  return RealJoinConnection(hostname, port, true);
}

bool Http3Session::JoinConnection(const nsACString& hostname, int32_t port) {
  return RealJoinConnection(hostname, port, false);
}

// TODO test
bool Http3Session::RealJoinConnection(const nsACString& hostname, int32_t port,
                                      bool justKidding) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (!mConnection || !CanSendData() || mShouldClose || mGoawayReceived) {
    return false;
  }

  nsHttpConnectionInfo* ci = ConnectionInfo();
  if (ci->UsingProxy()) {
    MOZ_ASSERT(false,
               "RealJoinConnection should not be called when using proxy");
    return false;
  }

  if (nsCString(hostname).EqualsIgnoreCase(ci->Origin()) &&
      (port == ci->OriginPort())) {
    return true;
  }

  nsAutoCString key(hostname);
  key.Append(':');
  key.Append(justKidding ? 'k' : '.');
  key.AppendInt(port);
  bool cachedResult;
  if (mJoinConnectionCache.Get(key, &cachedResult)) {
    LOG(("joinconnection [%p %s] %s result=%d cache\n", this,
         ConnectionInfo()->HashKey().get(), key.get(), cachedResult));
    return cachedResult;
  }

  nsresult rv;
  bool isJoined = false;

  nsCOMPtr<nsITLSSocketControl> sslSocketControl;
  mConnection->GetTLSSocketControl(getter_AddRefs(sslSocketControl));
  if (!sslSocketControl) {
    return false;
  }

  bool joinedReturn = false;
  if (justKidding) {
    rv = sslSocketControl->TestJoinConnection(mConnInfo->GetNPNToken(),
                                              hostname, port, &isJoined);
  } else {
    rv = sslSocketControl->JoinConnection(mConnInfo->GetNPNToken(), hostname,
                                          port, &isJoined);
  }
  if (NS_SUCCEEDED(rv) && isJoined) {
    joinedReturn = true;
  }

  LOG(("joinconnection [%p %s] %s result=%d lookup\n", this,
       ConnectionInfo()->HashKey().get(), key.get(), joinedReturn));
  mJoinConnectionCache.InsertOrUpdate(key, joinedReturn);
  if (!justKidding) {
    // cache a kidding entry too as this one is good for both
    nsAutoCString key2(hostname);
    key2.Append(':');
    key2.Append('k');
    key2.AppendInt(port);
    if (!mJoinConnectionCache.Get(key2)) {
      mJoinConnectionCache.InsertOrUpdate(key2, joinedReturn);
    }
  }
  return joinedReturn;
}

void Http3Session::CallCertVerification(Maybe<nsCString> aEchPublicName) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("Http3Session::CallCertVerification [this=%p]", this));

  NeqoCertificateInfo certInfo;
  if (NS_FAILED(mHttp3Connection->PeerCertificateInfo(&certInfo))) {
    LOG(("Http3Session::CallCertVerification [this=%p] - no cert", this));
    mHttp3Connection->PeerAuthenticated(SSL_ERROR_BAD_CERTIFICATE);
    mError = psm::GetXPCOMFromNSSError(SSL_ERROR_BAD_CERTIFICATE);
    return;
  }

  if (mConnInfo->GetWebTransport()) {
    // if our connection is webtransport, we might do a verification
    // based on serverCertificatedHashes
    const nsTArray<RefPtr<nsIWebTransportHash>>* servCertHashes =
        gHttpHandler->ConnMgr()->GetServerCertHashes(mConnInfo);
    if (servCertHashes && !servCertHashes->IsEmpty() &&
        certInfo.certs.Length() >= 1) {
      // ok, we verify based on serverCertificateHashes
      mozilla::pkix::Result rv = AuthCertificateWithServerCertificateHashes(
          certInfo.certs[0], *servCertHashes);
      if (rv != mozilla::pkix::Result::Success) {
        // ok we failed, report it back
        LOG(
            ("Http3Session::CallCertVerification [this=%p] "
             "AuthCertificateWithServerCertificateHashes failed",
             this));
        mHttp3Connection->PeerAuthenticated(SSL_ERROR_BAD_CERTIFICATE);
        mError = psm::GetXPCOMFromNSSError(SSL_ERROR_BAD_CERTIFICATE);
        return;
      }
      // ok, we succeded
      Authenticated(0, true);
      return;
    }
  }

  Maybe<nsTArray<nsTArray<uint8_t>>> stapledOCSPResponse;
  if (certInfo.stapled_ocsp_responses_present) {
    stapledOCSPResponse.emplace(std::move(certInfo.stapled_ocsp_responses));
  }

  Maybe<nsTArray<uint8_t>> sctsFromTLSExtension;
  if (certInfo.signed_cert_timestamp_present) {
    sctsFromTLSExtension.emplace(std::move(certInfo.signed_cert_timestamp));
  }

  uint32_t providerFlags;
  // the return value is always NS_OK, just ignore it.
  (void)mSocketControl->GetProviderFlags(&providerFlags);

  nsCString echConfig;
  nsresult nsrv = mSocketControl->GetEchConfig(echConfig);
  bool verifyToEchPublicName = NS_SUCCEEDED(nsrv) && !echConfig.IsEmpty() &&
                               aEchPublicName && !aEchPublicName->IsEmpty();
  const nsACString& hostname =
      verifyToEchPublicName ? *aEchPublicName : mSocketControl->GetHostName();

  SECStatus rv = psm::AuthCertificateHookWithInfo(
      mSocketControl, hostname, static_cast<const void*>(this),
      std::move(certInfo.certs), stapledOCSPResponse, sctsFromTLSExtension,
      providerFlags);
  if ((rv != SECSuccess) && (rv != SECWouldBlock)) {
    LOG(("Http3Session::CallCertVerification [this=%p] AuthCertificate failed",
         this));
    mHttp3Connection->PeerAuthenticated(SSL_ERROR_BAD_CERTIFICATE);
    mError = psm::GetXPCOMFromNSSError(SSL_ERROR_BAD_CERTIFICATE);
  }
}

void Http3Session::Authenticated(int32_t aError,
                                 bool aServCertHashesSucceeded) {
  LOG(("Http3Session::Authenticated error=0x%" PRIx32 " [this=%p].", aError,
       this));
  if ((mState == INITIALIZING) || (mState == ZERORTT)) {
    if (psm::IsNSSErrorCode(aError)) {
      mError = psm::GetXPCOMFromNSSError(aError);
      LOG(("Http3Session::Authenticated psm-error=0x%" PRIx32 " [this=%p].",
           static_cast<uint32_t>(mError), this));
    } else if (StaticPrefs::
                   network_http_http3_disable_when_third_party_roots_found()) {
      // In test, we use another perf value to override the value of
      // hasThirdPartyRoots.
      bool hasThirdPartyRoots =
          (xpc::IsInAutomation() || PR_GetEnv("XPCSHELL_TEST_PROFILE_DIR"))
              ? StaticPrefs::
                    network_http_http3_has_third_party_roots_found_in_automation()
              : !mSocketControl->IsBuiltCertChainRootBuiltInRoot();
      LOG(
          ("Http3Session::Authenticated [this=%p, hasThirdPartyRoots=%d, "
           "servCertHashesSucceeded=%d]",
           this, hasThirdPartyRoots, aServCertHashesSucceeded));
      // If serverCertificateHashes is used a thirdPartyRoot is legal
      if (hasThirdPartyRoots && !aServCertHashesSucceeded) {
        if (mFirstHttpTransaction) {
          mFirstHttpTransaction->DisableHttp3(false);
        }
        mUdpConn->CloseTransaction(this, NS_ERROR_NET_RESET);
        return;
      }
    }
    mHttp3Connection->PeerAuthenticated(aError);

    // Call OnQuicTimeoutExpired to properly process neqo events and outputs.
    // We call OnQuicTimeoutExpired instead of ProcessOutputAndEvents, because
    // HttpConnectionUDP must close this session in case of an error.
    nsCOMPtr<nsIRunnable> event =
        NewRunnableMethod("net::HttpConnectionUDP::OnQuicTimeoutExpired",
                          mUdpConn, &HttpConnectionUDP::OnQuicTimeoutExpired);
    if (StaticPrefs::network_trr_high_priority_events() &&
        mConnInfo->GetIsTrrServiceChannel()) {
      event = new PrioritizableRunnable(
          event.forget(), nsIRunnablePriority::PRIORITY_MEDIUMHIGH);
    }
    DispatchToCurrent(event.forget());
    mUdpConn->ChangeConnectionState(ConnectionState::TRANSFERING);
  }
}

void Http3Session::SetSecInfo() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  NeqoSecretInfo secInfo;
  if (NS_SUCCEEDED(mHttp3Connection->GetSecInfo(&secInfo))) {
    mSocketControl->SetSSLVersionUsed(secInfo.version);
    mSocketControl->SetResumed(secInfo.resumed);
    mSocketControl->SetNegotiatedNPN(secInfo.alpn);

    mSocketControl->SetInfo(secInfo.cipher, secInfo.version, secInfo.group,
                            secInfo.signature_scheme, secInfo.ech_accepted);
    mHandshakeSucceeded = true;

    bool tokenPresent = false;
    if (NS_SUCCEEDED(
            mSocketControl->GetResumptionTokenPresent(&tokenPresent)) &&
        tokenPresent) {
      glean::network::ssl_token_resumption_outcome
          .Get(secInfo.resumed ? "resumed"_ns : "not_resumed"_ns)
          .Add();
    }
  }

  if (!mSocketControl->HasServerCert()) {
    mSocketControl->RebuildCertificateInfoFromSSLTokenCache();
  }
}

// Transport error have values from 0x0 to 0x11.
// (https://tools.ietf.org/html/draft-ietf-quic-transport-34#section-20.1)
// We will map this error to 0-16.
// 17 will capture error codes between and including 0x12 and 0x0ff. This
// error codes are not define by the spec but who know peer may sent them.
// CryptoAlerts have value 0x100 + alert code. The range of alert code is
// 0x00-0xff. (https://tools.ietf.org/html/draft-ietf-quic-tls_34#section-4.8)
// Since telemetry does not allow more than 100 bucket, we use three diffrent
// keys to map all alert codes.
const uint32_t HTTP3_TELEMETRY_TRANSPORT_INTERNAL_ERROR = 15;
const uint32_t HTTP3_TELEMETRY_TRANSPORT_END = 16;
const uint32_t HTTP3_TELEMETRY_TRANSPORT_UNKNOWN = 17;
const uint32_t HTTP3_TELEMETRY_TRANSPORT_CRYPTO_UNKNOWN = 18;
// All errors from CloseError::Tag::CryptoError will be map to 19
const uint32_t HTTP3_TELEMETRY_CRYPTO_ERROR = 19;

uint64_t GetCryptoAlertCode(nsCString& key, uint64_t error) {
  if (error < 100) {
    key.Append("_a"_ns);
    return error;
  }
  if (error < 200) {
    error -= 100;
    key.Append("_b"_ns);
    return error;
  }
  if (error < 256) {
    error -= 200;
    key.Append("_c"_ns);
    return error;
  }
  return HTTP3_TELEMETRY_TRANSPORT_CRYPTO_UNKNOWN;
}

uint64_t GetTransportErrorCodeForTelemetry(nsCString& key, uint64_t error) {
  if (error <= HTTP3_TELEMETRY_TRANSPORT_END) {
    return error;
  }
  if (error < 0x100) {
    return HTTP3_TELEMETRY_TRANSPORT_UNKNOWN;
  }

  return GetCryptoAlertCode(key, error - 0x100);
}

// Http3 error codes are 0x100-0x110.
// (https://tools.ietf.org/html/draft-ietf-quic-http-33#section-8.1)
// The mapping is described below.
// 0x00-0x10 mapped to 0-16
// 0x11-0xff mapped to 17
// 0x100-0x110 mapped to 18-36
// 0x111-0x1ff mapped to 37
// 0x200-0x202 mapped to 38-40
// Others mapped to 41
const uint32_t HTTP3_TELEMETRY_APP_UNKNOWN_1 = 17;
const uint32_t HTTP3_TELEMETRY_APP_START = 18;
// Values between 0x111 and 0x1ff are no definded and will be map to 18.
const uint32_t HTTP3_TELEMETRY_APP_UNKNOWN_2 = 37;
// Error codes between 0x200 and 0x202 are related to qpack.
// (https://tools.ietf.org/html/draft-ietf-quic-qpack-20#section-6)
// They will be mapped to 19-21
const uint32_t HTTP3_TELEMETRY_APP_QPACK_START = 38;
// Values greater or equal to 0x203 are no definded and will be map to 41.
const uint32_t HTTP3_TELEMETRY_APP_UNKNOWN_3 = 41;

uint64_t GetAppErrorCodeForTelemetry(uint64_t error) {
  if (error <= 0x10) {
    return error;
  }
  if (error <= 0xff) {
    return HTTP3_TELEMETRY_APP_UNKNOWN_1;
  }
  if (error <= 0x110) {
    return error - 0x100 + HTTP3_TELEMETRY_APP_START;
  }
  if (error < 0x200) {
    return HTTP3_TELEMETRY_APP_UNKNOWN_2;
  }
  if (error <= 0x202) {
    return error - 0x200 + HTTP3_TELEMETRY_APP_QPACK_START;
  }
  return HTTP3_TELEMETRY_APP_UNKNOWN_3;
}

void Http3Session::CloseConnectionTelemetry(CloseError& aError, bool aClosing) {
  uint64_t value = 0;
  nsCString key = EmptyCString();

  switch (aError.tag) {
    case CloseError::Tag::TransportInternalError:
      key = "transport_internal"_ns;
      value = HTTP3_TELEMETRY_TRANSPORT_INTERNAL_ERROR;
      break;
    case CloseError::Tag::TransportInternalErrorOther:
      key = "transport_other"_ns;
      value = aError.transport_internal_error_other._0;
      break;
    case CloseError::Tag::TransportError:
      key = "transport"_ns;
      value = GetTransportErrorCodeForTelemetry(key, aError.transport_error._0);
      break;
    case CloseError::Tag::CryptoError:
      key = "transport"_ns;
      value = HTTP3_TELEMETRY_CRYPTO_ERROR;
      break;
    case CloseError::Tag::CryptoAlert:
      key = "transport_crypto_alert"_ns;
      value = GetCryptoAlertCode(key, aError.crypto_alert._0);
      break;
    case CloseError::Tag::PeerAppError:
      key = "peer_app"_ns;
      value = GetAppErrorCodeForTelemetry(aError.peer_app_error._0);
      break;
    case CloseError::Tag::PeerError:
      key = "peer_transport"_ns;
      value = GetTransportErrorCodeForTelemetry(key, aError.peer_error._0);
      break;
    case CloseError::Tag::AppError:
      key = "app"_ns;
      value = GetAppErrorCodeForTelemetry(aError.app_error._0);
      break;
    case CloseError::Tag::EchRetry:
      key = "transport_crypto_alert"_ns;
      value = 100;
  }

  MOZ_DIAGNOSTIC_ASSERT(value <= 100);

  key.Append(aClosing ? "_closing"_ns : "_closed"_ns);

  glean::http3::connection_close_code.Get(key).AccumulateSingleSample(value);

  Http3Stats stats{};
  mHttp3Connection->GetStats(&stats);

  if (stats.packets_tx > 0) {
    unsigned long loss = (stats.lost * 10000) / stats.packets_tx;
    glean::http3::loss_ratio.AccumulateSingleSample(loss);

    glean::http3::late_ack.EnumGet(glean::http3::LateAckLabel::eAck)
        .AccumulateSingleSample(stats.late_ack);
    glean::http3::late_ack.EnumGet(glean::http3::LateAckLabel::ePto)
        .AccumulateSingleSample(stats.pto_ack);

    unsigned long late_ack_ratio = (stats.late_ack * 10000) / stats.packets_tx;
    unsigned long pto_ack_ratio = (stats.pto_ack * 10000) / stats.packets_tx;
    glean::http3::late_ack_ratio.EnumGet(glean::http3::LateAckRatioLabel::eAck)
        .AccumulateSingleSample(late_ack_ratio);
    glean::http3::late_ack_ratio.EnumGet(glean::http3::LateAckRatioLabel::ePto)
        .AccumulateSingleSample(pto_ack_ratio);

    for (uint32_t i = 0; i < MAX_PTO_COUNTS; i++) {
      nsAutoCString key;
      key.AppendInt(i);
      glean::http3::counts_pto.Get(key).AccumulateSingleSample(
          stats.pto_counts[i]);
    }

    glean::http3::drop_dgrams.AccumulateSingleSample(stats.dropped_rx);
    glean::http3::saved_dgrams.AccumulateSingleSample(stats.saved_datagrams);
  }

  glean::http3::received_sent_dgrams
      .EnumGet(glean::http3::ReceivedSentDgramsLabel::eReceived)
      .AccumulateSingleSample(stats.packets_rx);
  glean::http3::received_sent_dgrams
      .EnumGet(glean::http3::ReceivedSentDgramsLabel::eSent)
      .AccumulateSingleSample(stats.packets_tx);

  if (aClosing) {
    RefPtr<nsHttpConnectionInfo> ci;
    GetConnectionInfo(getter_AddRefs(ci));
    if (ci && ci->GetIsTrrServiceChannel() && !mLastTRRResponseTime.IsNull() &&
        (mGoawayReceived ||
         (aError.tag == CloseError::Tag::PeerAppError &&
          aError.peer_app_error._0 == HTTP3_APP_ERROR_NO_ERROR))) {
      // Record telemetry keyed by TRR provider.
      glean::network::trr_idle_close_time_h3.Get(TRRProviderKey())
          .AccumulateRawDuration(TimeStamp::Now() - mLastTRRResponseTime);
      mLastTRRResponseTime = TimeStamp();
    }
  }
}

void Http3Session::Finish0Rtt(bool aRestart) {
  RefPtr<Http3Session> self(this);

  nsTArray<RefPtr<Http3StreamBase>> streams;
  for (const auto& weak : m0RTTStreams) {
    if (RefPtr<Http3StreamBase> s = weak.get()) {
      streams.AppendElement(std::move(s));
    }
  }
  m0RTTStreams.Clear();

  for (const auto& stream : streams) {
    if (aRestart) {
      // When we need to restart transactions remove them from all lists.
      if (stream->HasStreamId()) {
        mStreamIdHash.Remove(stream->StreamId());
      }
      RemoveStreamFromQueues(stream);
      // The stream is ready to write again.
      mReadyForWrite.Push(stream);
    }
    stream->Finish0RTT(aRestart);
  }

  for (size_t i = 0; i < mCannotDo0RTTStreams.Length(); ++i) {
    if (mCannotDo0RTTStreams[i]) {
      mReadyForWrite.Push(mCannotDo0RTTStreams[i]);
    }
  }
  mCannotDo0RTTStreams.Clear();
  MaybeResumeSend();
}

void Http3Session::ReportHttp3Connection() {
  if (CanSendData() && !mHttp3ConnectionReported) {
    mHttp3ConnectionReported = true;
    gHttpHandler->ConnMgr()->ReportHttp3Connection(mUdpConn);
    MaybeResumeSend();
  }
}

#ifndef ANDROID
void Http3Session::EchOutcomeTelemetry() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  glean::http3::EchOutcomeLabel label;
  switch (mEchExtensionStatus) {
    case EchExtensionStatus::kNotPresent:
      label = glean::http3::EchOutcomeLabel::eNone;
      break;
    case EchExtensionStatus::kGREASE:
      label = glean::http3::EchOutcomeLabel::eGrease;
      break;
    case EchExtensionStatus::kReal:
      label = glean::http3::EchOutcomeLabel::eReal;
      break;
  }

  glean::http3::ech_outcome.EnumGet(label).AccumulateSingleSample(
      mHandshakeSucceeded ? 0 : 1);
}
#endif

void Http3Session::ZeroRttTelemetry(ZeroRttOutcome aOutcome) {
  nsAutoCString key;

  switch (aOutcome) {
    case USED_SUCCEEDED:
      key = "succeeded"_ns;
      break;
    case USED_REJECTED:
      key = "rejected"_ns;
      break;
    case USED_CONN_ERROR:
      key = "conn_error"_ns;
      break;
    case USED_CONN_CLOSED_BY_NECKO:
      key = "conn_closed_by_necko"_ns;
      break;
    default:
      break;
  }

  bool isTrr = mConnInfo && mConnInfo->GetIsTrrServiceChannel();

  if (key.IsEmpty()) {
    mozilla::glean::netwerk::http3_0rtt_state.Get("not_used"_ns).Add(1);
    if (isTrr) {
      mozilla::glean::dns::trr_http3_0rtt_state.Get("not_used"_ns).Add(1);
    }
  } else {
    MOZ_ASSERT(mZeroRttStarted);
    mozilla::TimeStamp zeroRttEnded = mozilla::TimeStamp::Now();
    mozilla::glean::netwerk::http3_0rtt_state_duration.Get(key)
        .AccumulateRawDuration(zeroRttEnded - mZeroRttStarted);
    mozilla::glean::netwerk::http3_0rtt_state.Get(key).Add(1);

    if (isTrr) {
      mozilla::glean::dns::trr_http3_0rtt_state_duration.Get(key)
          .AccumulateRawDuration(zeroRttEnded - mZeroRttStarted);
      mozilla::glean::dns::trr_http3_0rtt_state.Get(key).Add(1);
    }
  }
}

nsresult Http3Session::GetTransactionTLSSocketControl(
    nsITLSSocketControl** tlsSocketControl) {
  NS_IF_ADDREF(*tlsSocketControl = mSocketControl);
  return NS_OK;
}

PRIntervalTime Http3Session::LastWriteTime() { return mLastWriteTime; }

//=========================================================================
// WebTransport
//=========================================================================

bool Http3Session::CloseWebTransport(
    uint64_t aSessionId, uint32_t aError, const nsACString& aMessage,
    mozilla::dom::WebTransportStatsData& aStats) {
  return mHttp3Connection->CloseWebTransport(aSessionId, aError, aMessage,
                                             aStats);
}

nsresult Http3Session::CreateWebTransportStream(
    uint64_t aSessionId, WebTransportStreamType aStreamType,
    uint64_t* aStreamId) {
  return mHttp3Connection->CreateWebTransportStream(aSessionId, aStreamType,
                                                    aStreamId);
}

void Http3Session::SendDatagram(Http3WebTransportSession* aSession,
                                nsTArray<uint8_t>& aData, uint64_t aTrackingId,
                                uint64_t aSendGroupId, int64_t aSendOrder,
                                TimeStamp aTimestamp) {
  // Taken as late as possible, so time spent getting here counts too.
  uint64_t ageUs = static_cast<uint64_t>(
      std::max(0.0, (TimeStamp::Now() - aTimestamp).ToMicroseconds()));
  bool belowHighWaterMark = false;
  nsresult rv = mHttp3Connection->WebTransportSendDatagram(
      aSession->StreamId(), aData, aTrackingId, &belowHighWaterMark,
      aSendGroupId, aSendOrder, ageUs);
  LOG(("Http3Session::SendDatagram %p res=%" PRIx32
       " belowHWM=%d, sendGroup=%" PRIu64 ", sendOrder=%" PRId64,
       this, static_cast<uint32_t>(rv), belowHighWaterMark, aSendGroupId,
       aSendOrder));

  // Per spec section 4.3 step 9: if queue is below high water mark,
  // resolve promise immediately (send OnDatagramReady now).
  // neqo will also emit a DatagramOutcome event for this datagram once it is
  // actually sent/dropped, which results in a second, redundant
  // OnDatagramReady/DatagramReady for the same tracking ID. This is
  // intentional: the content-process side treats an unknown tracking ID as a
  // harmless no-op (the promise was already resolved by the first
  // notification), so we accept the extra IPC round-trip rather than track
  // per-ID dedup state here.
  if (aTrackingId && NS_SUCCEEDED(rv) && belowHighWaterMark) {
    aSession->OnOutgoingDatagramOutCome(
        aTrackingId, WebTransportSessionEventListener::DatagramOutcome::SENT);
  }

  // Only report immediate failures if queuing itself failed.
  if (aTrackingId && NS_FAILED(rv)) {
    WebTransportSessionEventListener::DatagramOutcome outcome;
    if (rv == NS_ERROR_NOT_AVAILABLE) {
      outcome = WebTransportSessionEventListener::DatagramOutcome::
          DROPPED_TOO_MUCH_DATA;
    } else {
      outcome = WebTransportSessionEventListener::DatagramOutcome::UNKNOWN;
    }
    aSession->OnOutgoingDatagramOutCome(aTrackingId, outcome);
  }
}

uint64_t Http3Session::MaxDatagramSize(uint64_t aSessionId) {
  uint64_t size = 0;
  (void)mHttp3Connection->WebTransportMaxDatagramSize(aSessionId, &size);
  return size;
}

nsresult Http3Session::ExportWebTransportKeyingMaterial(
    uint64_t aSessionId, const nsTArray<uint8_t>& aLabel,
    const nsTArray<uint8_t>& aContext, uint32_t aOutputLength,
    nsTArray<uint8_t>& aKeyingMaterial) {
  return mHttp3Connection->ExportWebTransportKeyingMaterial(
      aSessionId, aLabel, aContext, aOutputLength, aKeyingMaterial);
}

void Http3Session::SetDatagramMaxBufferedDatagrams(
    uint64_t aSessionId, uint32_t aMaxBufferedDatagrams) {
  LOG(("Http3Session::SetDatagramMaxBufferedDatagrams sessionId=%" PRIu64
       " max=%" PRIu32,
       aSessionId, aMaxBufferedDatagrams));
  (void)mHttp3Connection->WebTransportSetDatagramMaxBufferedDatagrams(
      aSessionId, aMaxBufferedDatagrams);
}

void Http3Session::SetDatagramMaxAge(uint64_t aSessionId, double aMaxAge) {
  LOG(("Http3Session::SetDatagramMaxAge sessionId=%" PRIu64 " age=%f",
       aSessionId, aMaxAge));
  (void)mHttp3Connection->WebTransportSetDatagramMaxAge(aSessionId, aMaxAge);
}

bool Http3Session::GetWebTransportSessionStats(
    uint64_t aSessionId, mozilla::dom::WebTransportStatsData& aStats) {
  return mHttp3Connection->GetWebTransportSessionStats(aSessionId, aStats);
}

nsresult Http3Session::RegisterWebTransportSendGroup(uint64_t aSessionId,
                                                     uint64_t aGroupId) {
  return mHttp3Connection->RegisterWebTransportSendGroup(aSessionId, aGroupId);
}

nsresult Http3Session::GetWebTransportSessionProtocol(uint64_t aSessionId,
                                                      nsACString& aProtocol) {
  return mHttp3Connection->GetWebTransportSessionProtocol(aSessionId,
                                                          aProtocol);
}

void Http3Session::
    SetWebTransportAnticipatedConcurrentIncomingUnidirectionalStreams(
        uint64_t aSessionId, uint16_t aValue) {
  LOG(
      ("Http3Session::SetWebTransportAnticipatedConcurrentIncoming"
       "UnidirectionalStreams %p sessionId=0x%" PRIx64 " value=%u",
       this, aSessionId, aValue));
  nsresult rv =
      mHttp3Connection
          ->SetWebTransportAnticipatedConcurrentIncomingUnidirectionalStreams(
              aSessionId, aValue);
  if (NS_FAILED(rv)) {
    LOG(("  failed, no live WebTransport session for that id"));
  }
}

void Http3Session::
    SetWebTransportAnticipatedConcurrentIncomingBidirectionalStreams(
        uint64_t aSessionId, uint16_t aValue) {
  LOG(
      ("Http3Session::SetWebTransportAnticipatedConcurrentIncoming"
       "BidirectionalStreams %p sessionId=0x%" PRIx64 " value=%u",
       this, aSessionId, aValue));
  nsresult rv =
      mHttp3Connection
          ->SetWebTransportAnticipatedConcurrentIncomingBidirectionalStreams(
              aSessionId, aValue);
  if (NS_FAILED(rv)) {
    LOG(("  failed, no live WebTransport session for that id"));
  }
}

void Http3Session::SendHTTPDatagram(uint64_t aStreamId,
                                    nsTArray<uint8_t>& aData,
                                    uint64_t aTrackingId) {
  LOG(("Http3Session::SendHTTPDatagram %p length=%zu aTrackingId=%" PRIx64,
       this, aData.Length(), aTrackingId));
  // Connect-UDP (MASQUE) doesn't use WebTransport send groups or send order,
  // so pass 0 for both (0 = null sendGroup, 0 = default sendOrder).
  (void)mHttp3Connection->ConnectUdpSendDatagram(aStreamId, aData, aTrackingId,
                                                 0, 0);
}

void Http3Session::SetSendOrder(Http3StreamBase* aStream, int64_t aSendOrder) {
  if (!IsClosing()) {
    nsresult rv = mHttp3Connection->WebTransportSetSendOrder(
        aStream->StreamId(), aSendOrder);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    (void)rv;
  }
}

void Http3Session::SetSendGroup(Http3StreamBase* aStream,
                                uint64_t aSendGroupId) {
  if (!IsClosing()) {
    nsresult rv = mHttp3Connection->WebTransportSetSendGroup(
        aStream->StreamId(), aSendGroupId);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "WebTransportSetSendGroup failed");
    (void)rv;
  }
}

Http3Stats Http3Session::GetStats() {
  if (!mHttp3Connection) {
    return Http3Stats();
  }

  Http3Stats stats{};
  mHttp3Connection->GetStats(&stats);
  return stats;
}

already_AddRefed<HttpConnectionUDP> Http3Session::CreateTunnelStream(
    nsAHttpTransaction* aHttpTransaction, nsIInterfaceRequestor* aCallbacks) {
  LOG(("Http3Session::CreateTunnelStream %p aHttpTransaction=%p", this,
       aHttpTransaction));
  RefPtr<Http3StreamBase> stream =
      new Http3ConnectUDPStream(aHttpTransaction, this, NS_GetCurrentThread());
  if (mConnInfo->GetIsTrrServiceChannel()) {
    stream->GetHttp3ConnectUDPStream()->MarkAsTRRServiceChannel();
  }
  mStreamTransactionHash.InsertOrUpdate(aHttpTransaction, RefPtr{stream});
  StreamHasDataToWrite(stream);

  RefPtr<HttpConnectionUDP> conn =
      stream->GetHttp3ConnectUDPStream()->CreateUDPConnection(aCallbacks);
  return conn.forget();
}

void Http3Session::FinishTunnelSetup(nsAHttpTransaction* aTransaction) {
  LOG(("Http3Session::FinishTunnelSetup %p aHttpTransaction=%p", this,
       aTransaction));
  RefPtr<Http3StreamBase> stream = mStreamTransactionHash.Get(aTransaction);
  if (!stream || !stream->GetHttp3ConnectUDPStream()) {
    MOZ_ASSERT(false, "There must be a stream");
    return;
  }

  RemoveStreamFromQueues(stream);
  mStreamTransactionHash.Remove(aTransaction);
  mTunnelStreams.AppendElement(stream);
}

already_AddRefed<nsHttpConnection> Http3Session::CreateTunnelStream(
    nsAHttpTransaction* aHttpTransaction, nsIInterfaceRequestor* aCallbacks,
    PRIntervalTime aRtt, bool aIsExtendedCONNECT) {
  LOG(("Http3Session::CreateTunnelStream %p aHttpTransaction=%p", this,
       aHttpTransaction));
  RefPtr<Http3StreamBase> stream =
      new Http3StreamTunnel(aHttpTransaction, this, mCurrentBrowserId);
  mStreamTransactionHash.InsertOrUpdate(aHttpTransaction, RefPtr{stream});
  StreamHasDataToWrite(stream);

  RefPtr<nsHttpConnection> conn =
      stream->GetHttp3StreamTunnel()->CreateHttpConnection(aCallbacks, aRtt,
                                                           aIsExtendedCONNECT);
  return conn.forget();
}

}  // namespace mozilla::net
