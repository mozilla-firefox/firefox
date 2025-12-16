/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"
#include "nsCOMPtr.h"
#include "nsStringFwd.h"

// Log on level :5, instead of default :4.
#undef LOG
#define LOG(args) LOG5(args)
#undef LOG_ENABLED
#define LOG_ENABLED() LOG5_ENABLED()

#include <algorithm>

#include "AltServiceChild.h"
#include "CacheControlParser.h"
#include "Http2Session.h"
#include "Http2Stream.h"
#include "Http2StreamBase.h"
#include "Http2StreamTunnel.h"
#include "Http2WebTransportSession.h"
#include "LoadContextInfo.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/Preferences.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "nsHttp.h"
#include "nsHttpConnection.h"
#include "nsHttpHandler.h"
#include "nsIRequestContext.h"
#include "nsISupportsPriority.h"
#include "nsITLSSocketControl.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"
#include "nsSocketTransportService2.h"
#include "nsStandardURL.h"
#include "nsURLHelper.h"
#include "prnetdb.h"
#include "sslerr.h"
#include "sslt.h"

namespace mozilla {
namespace net {

extern const nsCString& TRRProviderKey();

Http2StreamQueueManager::StreamQueue& Http2StreamQueueManager::GetQueue(
    Http2StreamQueueType aType) {
  switch (aType) {
    case Http2StreamQueueType::ReadyForWrite:
      return mReadyForWrite;
    case Http2StreamQueueType::QueuedStreams:
      return mQueuedStreams;
    case Http2StreamQueueType::SlowConsumersReadyForRead:
      return mSlowConsumersReadyForRead;
    default:
      MOZ_CRASH("Invalid queue type");
      return mReadyForWrite;
  }
}

bool Http2StreamQueueManager::GetQueueFlag(Http2StreamQueueType aType,
                                           Http2StreamBase* aStream) {
  switch (aType) {
    case Http2StreamQueueType::ReadyForWrite:
      return aStream->InWriteQueue();
    case Http2StreamQueueType::QueuedStreams:
      return aStream->Queued();
    case Http2StreamQueueType::SlowConsumersReadyForRead:
      return aStream->InReadQueue();
    default:
      MOZ_CRASH("Invalid queue type");
      return false;
  }
}

void Http2StreamQueueManager::SetQueueFlag(Http2StreamQueueType aType,
                                           Http2StreamBase* aStream,
                                           bool value) {
  switch (aType) {
    case Http2StreamQueueType::ReadyForWrite:
      aStream->SetInWriteQueue(value);
      break;
    case Http2StreamQueueType::QueuedStreams:
      aStream->SetQueued(value);
      break;
    case Http2StreamQueueType::SlowConsumersReadyForRead:
      aStream->SetInReadQueue(value);
      break;
    default:
      MOZ_CRASH("Invalid queue type");
  }
}

void Http2StreamQueueManager::RemoveStreamFromAllQueue(
    Http2StreamBase* aStream) {
  // This does not immediately remove the stream from the underlying queues.
  // Instead, it clears the queue flags so that the stream will be skipped
  // the next time GetNextStreamFromQueue is called.
  aStream->SetInWriteQueue(false);
  aStream->SetQueued(false);
  aStream->SetInReadQueue(false);
}

void Http2StreamQueueManager::AddStreamToQueue(Http2StreamQueueType aType,
                                               Http2StreamBase* aStream) {
  if (GetQueueFlag(aType, aStream)) {
    return;
  }

  GetQueue(aType).Push(aStream);
  SetQueueFlag(aType, aStream, true);
}

already_AddRefed<Http2StreamBase>
Http2StreamQueueManager::GetNextStreamFromQueue(Http2StreamQueueType aType) {
  StreamQueue& queue = GetQueue(aType);

  while (!queue.IsEmpty()) {
    RefPtr<Http2StreamBase> stream = queue.Pop().get();
    if (stream && GetQueueFlag(aType, stream)) {
      SetQueueFlag(aType, stream, false);
      return stream.forget();
    }
  }

  return nullptr;
}

// Http2Session has multiple inheritance of things that implement nsISupports
NS_IMPL_ADDREF_INHERITED(Http2Session, nsAHttpConnection)
NS_IMPL_RELEASE_INHERITED(Http2Session, nsAHttpConnection)

NS_INTERFACE_MAP_BEGIN(Http2Session)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(Http2Session)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsAHttpConnection)
NS_INTERFACE_MAP_END

// "magic" refers to the string that preceeds HTTP/2 on the wire
// to help find any intermediaries speaking an older version of HTTP
const uint8_t Http2Session::kMagicHello[] = {
    0x50, 0x52, 0x49, 0x20, 0x2a, 0x20, 0x48, 0x54, 0x54, 0x50, 0x2f, 0x32,
    0x2e, 0x30, 0x0d, 0x0a, 0x0d, 0x0a, 0x53, 0x4d, 0x0d, 0x0a, 0x0d, 0x0a};

Http2Session* Http2Session::CreateSession(nsISocketTransport* aSocketTransport,
                                          enum SpdyVersion version,
                                          bool attemptingEarlyData) {
  if (!gHttpHandler) {
    RefPtr<nsHttpHandler> handler = nsHttpHandler::GetInstance();
    (void)handler.get();
  }

  Http2Session* session =
      new Http2Session(aSocketTransport, version, attemptingEarlyData);
  session->SendHello();
  return session;
}

Http2Session::Http2Session(nsISocketTransport* aSocketTransport,
                           enum SpdyVersion version, bool attemptingEarlyData)
    : mSocketTransport(aSocketTransport),
      mSegmentReader(nullptr),
      mSegmentWriter(nullptr),
      kMaxStreamID(StaticPrefs::network_http_http2_max_stream_id()),
      mNextStreamID(3)  // 1 is reserved for Updgrade handshakes
      ,
      mConcurrentHighWater(0),
      mDownstreamState(BUFFERING_OPENING_SETTINGS),
      mInputFrameBufferSize(kDefaultBufferSize),
      mInputFrameBufferUsed(0),
      mInputFrameDataSize(0),
      mInputFrameDataRead(0),
      mInputFrameFinal(false),
      mInputFrameType(0),
      mInputFrameFlags(0),
      mInputFrameID(0),
      mPaddingLength(0),
      mInputFrameDataStream(nullptr),
      mNeedsCleanup(nullptr),
      mDownstreamRstReason(NO_HTTP_ERROR),
      mExpectedHeaderID(0),
      mExpectedPushPromiseID(0),
      mFlatHTTPResponseHeadersOut(0),
      mShouldGoAway(false),
      mClosed(false),
      mCleanShutdown(false),
      mReceivedSettings(false),
      mTLSProfileConfirmed(false),
      mGoAwayReason(NO_HTTP_ERROR),
      mClientGoAwayReason(UNASSIGNED),
      mPeerGoAwayReason(UNASSIGNED),
      mGoAwayID(0),
      mOutgoingGoAwayID(0),
      mConcurrent(0),
      mServerPushedResources(0),
      mServerInitialStreamWindow(kDefaultRwin),
      mLocalSessionWindow(kDefaultRwin),
      mServerSessionWindow(kDefaultRwin),
      mInitialRwin(ASpdySession::kInitialRwin),
      mOutputQueueSize(kDefaultQueueSize),
      mOutputQueueUsed(0),
      mOutputQueueSent(0),
      mLastReadEpoch(PR_IntervalNow()),
      mPingSentEpoch(0),
      mPreviousUsed(false),
      mAggregatedHeaderSize(0),
      mWaitingForSettingsAck(false),
      mGoAwayOnPush(false),
      mUseH2Deps(false),
      mAttemptingEarlyData(attemptingEarlyData),
      mOriginFrameActivated(false),
      mCntActivated(0),
      mTlsHandshakeFinished(false),
      mPeerFailedHandshake(false),
      mTrrStreams(0) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  static uint64_t sSerial;
  mSerial = ++sSerial;

  LOG3(("Http2Session::Http2Session %p serial=0x%" PRIX64 "\n", this, mSerial));

  mInputFrameBuffer = MakeUnique<char[]>(mInputFrameBufferSize);
  mOutputQueueBuffer = MakeUnique<char[]>(mOutputQueueSize);
  mDecompressBuffer.SetCapacity(kDefaultBufferSize);

  mPushAllowance = gHttpHandler->SpdyPushAllowance();
  mInitialRwin = std::max(gHttpHandler->SpdyPullAllowance(), mPushAllowance);
  mMaxConcurrent = gHttpHandler->DefaultSpdyConcurrent();
  mSendingChunkSize = gHttpHandler->SpdySendingChunkSize();

  mLastDataReadEpoch = mLastReadEpoch;

  mPingThreshold = gHttpHandler->SpdyPingThreshold();
  mPreviousPingThreshold = mPingThreshold;
  mCurrentBrowserId = gHttpHandler->ConnMgr()->CurrentBrowserId();

  mEnableWebsockets = StaticPrefs::network_http_http2_websockets();

  bool dumpHpackTables = StaticPrefs::network_http_http2_enable_hpack_dump();
  mCompressor.SetDumpTables(dumpHpackTables);
  mDecompressor.SetDumpTables(dumpHpackTables);
}

void Http2Session::Shutdown(nsresult aReason) {
  for (const auto& stream : mStreamTransactionHash.Values()) {
    ShutdownStream(stream, aReason);
  }

  for (auto& stream : mTunnelStreams) {
    ShutdownStream(stream, aReason);
  }
}

void Http2Session::ShutdownStream(Http2StreamBase* aStream, nsresult aReason) {
  // On a clean server hangup the server sets the GoAwayID to be the ID of
  // the last transaction it processed. If the ID of stream in the
  // local stream is greater than that it can safely be restarted because the
  // server guarantees it was not partially processed. Streams that have not
  // registered an ID haven't actually been sent yet so they can always be
  // restarted.
  if (mCleanShutdown &&
      (aStream->StreamID() > mGoAwayID || !aStream->HasRegisteredID())) {
    CloseStream(aStream, NS_ERROR_NET_RESET);  // can be restarted
  } else if (aStream->RecvdData()) {
    CloseStream(aStream, NS_ERROR_NET_PARTIAL_TRANSFER);
  } else if (mGoAwayReason == INADEQUATE_SECURITY) {
    CloseStream(aStream, NS_ERROR_NET_INADEQUATE_SECURITY);
  } else if (!mCleanShutdown && (mGoAwayReason != NO_HTTP_ERROR)) {
    CloseStream(aStream, NS_ERROR_NET_HTTP2_SENT_GOAWAY);
  } else if (!mCleanShutdown && PossibleZeroRTTRetryError(aReason)) {
    CloseStream(aStream, aReason);
  } else {
    CloseStream(aStream, NS_ERROR_ABORT);
  }
}

Http2Session::~Http2Session() {
  MOZ_DIAGNOSTIC_ASSERT(OnSocketThread());
  LOG3(("Http2Session::~Http2Session %p mDownstreamState=%X", this,
        mDownstreamState));

  Shutdown(NS_OK);

  if (mTrrStreams) {
    mozilla::glean::networking::trr_request_count_per_conn
        .Get(nsPrintfCString("%s_h2", mTrrHost.get()))
        .Add(static_cast<int32_t>(mTrrStreams));
  }
  glean::spdy::parallel_streams.AccumulateSingleSample(mConcurrentHighWater);
  glean::spdy::request_per_conn.AccumulateSingleSample(mCntActivated);
  glean::spdy::server_initiated_streams.AccumulateSingleSample(
      mServerPushedResources);
  glean::spdy::goaway_local.AccumulateSingleSample(mClientGoAwayReason);
  glean::spdy::goaway_peer.AccumulateSingleSample(mPeerGoAwayReason);
  glean::http::http2_fail_before_settings
      .EnumGet(static_cast<glean::http::Http2FailBeforeSettingsLabel>(
          mPeerFailedHandshake))
      .Add();
}

inline nsresult Http2Session::SessionError(enum errorType reason) {
  LOG3(("Http2Session::SessionError %p reason=0x%x mPeerGoAwayReason=0x%x",
        this, reason, mPeerGoAwayReason));
  mGoAwayReason = reason;

  if (reason == INADEQUATE_SECURITY) {
    // This one is special, as we have an error page just for this
    return NS_ERROR_NET_INADEQUATE_SECURITY;
  }

  // We're the one sending a generic GOAWAY
  return NS_ERROR_NET_HTTP2_SENT_GOAWAY;
}

void Http2Session::LogIO(Http2Session* self, Http2StreamBase* stream,
                         const char* label, const char* data,
                         uint32_t datalen) {
  if (!MOZ_LOG_TEST(gHttpIOLog, LogLevel::Verbose)) {
    return;
  }

  MOZ_LOG(gHttpIOLog, LogLevel::Verbose,
          ("Http2Session::LogIO %p stream=%p id=0x%X [%s]", self, stream,
           stream ? stream->StreamID() : 0, label));

  // Max line is (16 * 3) + 10(prefix) + newline + null
  char linebuf[128];
  uint32_t index;
  char* line = linebuf;

  linebuf[127] = 0;

  for (index = 0; index < datalen; ++index) {
    if (!(index % 16)) {
      if (index) {
        *line = 0;
        MOZ_LOG(gHttpIOLog, LogLevel::Verbose, ("%s", linebuf));
      }
      line = linebuf;
      snprintf(line, 128, "%08X: ", index);
      line += 10;
    }
    snprintf(line, 128 - (line - linebuf), "%02X ",
             (reinterpret_cast<const uint8_t*>(data))[index]);
    line += 3;
  }
  if (index) {
    *line = 0;
    MOZ_LOG(gHttpIOLog, LogLevel::Verbose, ("%s", linebuf));
  }
}

using Http2ControlFx = nsresult (*)(Http2Session*);
static constexpr Http2ControlFx sControlFunctions[] = {
    nullptr,  // type 0 data is not a control function
    Http2Session::RecvHeaders,
    Http2Session::RecvPriority,
    Http2Session::RecvRstStream,
    Http2Session::RecvSettings,
    Http2Session::RecvPushPromise,
    Http2Session::RecvPing,
    Http2Session::RecvGoAway,
    Http2Session::RecvWindowUpdate,
    Http2Session::RecvContinuation,
    Http2Session::RecvAltSvc,          // extension for type 0x0A
    Http2Session::RecvUnused,          // 0x0B was BLOCKED still radioactive
    Http2Session::RecvOrigin,          // extension for type 0x0C
    Http2Session::RecvUnused,          // 0x0D
    Http2Session::RecvUnused,          // 0x0E
    Http2Session::RecvUnused,          // 0x0F
    Http2Session::RecvPriorityUpdate,  // 0x10
};

static_assert(sControlFunctions[Http2Session::FRAME_TYPE_DATA] == nullptr);
static_assert(sControlFunctions[Http2Session::FRAME_TYPE_HEADERS] ==
              Http2Session::RecvHeaders);
static_assert(sControlFunctions[Http2Session::FRAME_TYPE_PRIORITY] ==
              Http2Session::RecvPriority);
static_assert(sControlFunctions[Http2Session::FRAME_TYPE_RST_STREAM] ==
              Http2Session::RecvRstStream);
static_assert(sControlFunctions[Http2Session::FRAME_TYPE_SETTINGS] ==
              Http2Session::RecvSettings);
static_assert(sControlFunctions[Http2Session::FRAME_TYPE_PUSH_PROMISE] ==
              Http2Session::RecvPushPromise);
static_assert(sControlFunctions[Http2Session::FRAME_TYPE_PING] ==
              Http2Session::RecvPing);
static_assert(sControlFunctions[Http2Session::FRAME_TYPE_GOAWAY] ==
              Http2Session::RecvGoAway);
static_assert(sControlFunctions[Http2Session::FRAME_TYPE_WINDOW_UPDATE] ==
              Http2Session::RecvWindowUpdate);
static_assert(sControlFunctions[Http2Session::FRAME_TYPE_CONTINUATION] ==
              Http2Session::RecvContinuation);
static_assert(sControlFunctions[Http2Session::FRAME_TYPE_ALTSVC] ==
              Http2Session::RecvAltSvc);
static_assert(sControlFunctions[Http2Session::FRAME_TYPE_UNUSED] ==
              Http2Session::RecvUnused);
static_assert(sControlFunctions[Http2Session::FRAME_TYPE_ORIGIN] ==
              Http2Session::RecvOrigin);
static_assert(sControlFunctions[0x0D] == Http2Session::RecvUnused);
static_assert(sControlFunctions[0x0E] == Http2Session::RecvUnused);
static_assert(sControlFunctions[0x0F] == Http2Session::RecvUnused);
static_assert(sControlFunctions[Http2Session::FRAME_TYPE_PRIORITY_UPDATE] ==
              Http2Session::RecvPriorityUpdate);

uint32_t Http2Session::RoomForMoreConcurrent() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (mConcurrent > mMaxConcurrent) {
    return 0;
  }
  return mMaxConcurrent - mConcurrent;
}

bool Http2Session::RoomForMoreStreams() {
  if (mNextStreamID + mStreamTransactionHash.Count() * 2 +
          mTunnelStreams.Length() >
      kMaxStreamID) {
    mShouldGoAway = true;
    return false;
  }

  return !mShouldGoAway;
}

PRIntervalTime Http2Session::IdleTime() {
  return PR_IntervalNow() - mLastDataReadEpoch;
}

uint32_t Http2Session::ReadTimeoutTick(PRIntervalTime now) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  LOG3(("Http2Session::ReadTimeoutTick %p delta since last read %ds\n", this,
        PR_IntervalToSeconds(now - mLastReadEpoch)));

  if (!mPingThreshold) {
    return UINT32_MAX;
  }

  if ((now - mLastReadEpoch) < mPingThreshold) {
    // recent activity means ping is not an issue
    if (mPingSentEpoch) {
      mPingSentEpoch = 0;
      if (mPreviousUsed) {
        // restore the former value
        mPingThreshold = mPreviousPingThreshold;
        mPreviousUsed = false;
      }
    }

    return PR_IntervalToSeconds(mPingThreshold) -
           PR_IntervalToSeconds(now - mLastReadEpoch);
  }

  if (mPingSentEpoch) {
    bool isTrr = (mTrrStreams > 0);
    uint32_t pingTimeout = isTrr ? StaticPrefs::network_trr_ping_timeout()
                                 : gHttpHandler->SpdyPingTimeout();
    LOG3(
        ("Http2Session::ReadTimeoutTick %p handle outstanding ping, "
         "timeout=%d\n",
         this, pingTimeout));
    if ((now - mPingSentEpoch) >= pingTimeout) {
      LOG3(("Http2Session::ReadTimeoutTick %p Ping Timer Exhaustion\n", this));
      if (mConnection) {
        mConnection->SetCloseReason(ConnectionCloseReason::IDLE_TIMEOUT);
      }
      mPingSentEpoch = 0;
      if (isTrr) {
        // These must be set this way to ensure we gracefully restart all
        // streams
        mGoAwayID = 0;
        mCleanShutdown = true;
        // If TRR is mode 2, this Http2Session will be closed due to TRR request
        // timeout, so we won't reach this code. If we are in mode 3, the
        // request timeout is usually larger than the ping timeout. We close the
        // stream with NS_ERROR_NET_RESET, so the transactions can be restarted.
        Close(NS_ERROR_NET_RESET);
      } else {
        Close(NS_ERROR_NET_TIMEOUT);
      }
      return UINT32_MAX;
    }
    return 1;  // run the tick aggressively while ping is outstanding
  }

  LOG3(("Http2Session::ReadTimeoutTick %p generating ping\n", this));

  mPingSentEpoch = PR_IntervalNow();
  if (!mPingSentEpoch) {
    mPingSentEpoch = 1;  // avoid the 0 sentinel value
  }
  GeneratePing(false);
  (void)ResumeRecv();  // read the ping reply

  return 1;  // run the tick aggressively while ping is outstanding
}

uint32_t Http2Session::RegisterStreamID(Http2StreamBase* stream,
                                        uint32_t aNewID) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(mNextStreamID < 0xfffffff0,
             "should have stopped admitting streams");
  MOZ_ASSERT(!(aNewID & 1),
             "0 for autoassign pull, otherwise explicit even push assignment");

  if (!aNewID) {
    // auto generate a new pull stream ID
    aNewID = mNextStreamID;
    MOZ_ASSERT(aNewID & 1, "pull ID must be odd.");
    mNextStreamID += 2;
  }

  LOG1(
      ("Http2Session::RegisterStreamID session=%p stream=%p id=0x%X "
       "concurrent=%d",
       this, stream, aNewID, mConcurrent));

  // We've used up plenty of ID's on this session. Start
  // moving to a new one before there is a crunch involving
  // server push streams or concurrent non-registered submits
  if (aNewID >= kMaxStreamID) {
    mShouldGoAway = true;
  }

  // integrity check
  if (mStreamIDHash.Contains(aNewID)) {
    LOG3(("   New ID already present\n"));
    MOZ_ASSERT(false, "New ID already present in mStreamIDHash");
    mShouldGoAway = true;
    return kDeadStreamID;
  }

  mStreamIDHash.InsertOrUpdate(aNewID, stream);

  if (aNewID & 1) {
    // don't count push streams here
    RefPtr<nsHttpConnectionInfo> ci(stream->ConnectionInfo());
    if (ci && ci->GetIsTrrServiceChannel()) {
      if (mTrrHost.IsEmpty()) {
        mTrrHost = ci->GetOrigin();
      }
      IncrementTrrCounter();
    }
  }
  return aNewID;
}

bool Http2Session::AddStream(nsAHttpTransaction* aHttpTransaction,
                             int32_t aPriority,
                             nsIInterfaceRequestor* aCallbacks) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  // integrity check
  if (mStreamTransactionHash.Contains(aHttpTransaction)) {
    LOG3(("   New transaction already present\n"));
    MOZ_ASSERT(false, "AddStream duplicate transaction pointer");
    return false;
  }

  if (!mConnection) {
    mConnection = aHttpTransaction->Connection();
  }

  if (!mFirstHttpTransaction && !mTlsHandshakeFinished) {
    mFirstHttpTransaction = aHttpTransaction->QueryHttpTransaction();
    LOG3(("Http2Session::AddStream first session=%p trans=%p ", this,
          mFirstHttpTransaction.get()));
  }

  if (mClosed || mShouldGoAway) {
    nsHttpTransaction* trans = aHttpTransaction->QueryHttpTransaction();
    if (trans) {
      LOG3(
          ("Http2Session::AddStream %p atrans=%p trans=%p session unusable - "
           "resched.\n",
           this, aHttpTransaction, trans));
      aHttpTransaction->SetConnection(nullptr);
      nsresult rv = gHttpHandler->InitiateTransaction(trans, trans->Priority());
      if (NS_FAILED(rv)) {
        LOG3(
            ("Http2Session::AddStream %p atrans=%p trans=%p failed to "
             "initiate "
             "transaction (%08x).\n",
             this, aHttpTransaction, trans, static_cast<uint32_t>(rv)));
      }
      return true;
    }
  }

  aHttpTransaction->SetConnection(this);
  aHttpTransaction->OnActivated();

  CreateStream(aHttpTransaction, aPriority, Http2StreamBaseType::Normal);
  return true;
}

void Http2Session::CreateStream(nsAHttpTransaction* aHttpTransaction,
                                int32_t aPriority,
                                Http2StreamBaseType streamType) {
  RefPtr<Http2StreamBase> refStream;
  switch (streamType) {
    case Http2StreamBaseType::Normal:
      refStream =
          new Http2Stream(aHttpTransaction, this, aPriority, mCurrentBrowserId);
      break;
    case Http2StreamBaseType::WebSocket:
    case Http2StreamBaseType::Tunnel:
    case Http2StreamBaseType::ServerPush:
      MOZ_RELEASE_ASSERT(false);
      return;
  }

  LOG3(("Http2Session::AddStream session=%p stream=%p serial=%" PRIu64 " "
        "NextID=0x%X (tentative)",
        this, refStream.get(), mSerial, mNextStreamID));

  RefPtr<Http2StreamBase> stream = refStream;
  mStreamTransactionHash.InsertOrUpdate(aHttpTransaction, std::move(refStream));

  mQueueManager.AddStreamToQueue(Http2StreamQueueType::ReadyForWrite, stream);
  SetWriteCallbacks();

  // Kick off the SYN transmit without waiting for the poll loop
  // This won't work for the first stream because there is no segment reader
  // yet.
  if (mSegmentReader) {
    uint32_t countRead;
    (void)ReadSegments(nullptr, kDefaultBufferSize, &countRead);
  }

  if (!(aHttpTransaction->Caps() & NS_HTTP_ALLOW_KEEPALIVE) &&
      !aHttpTransaction->IsNullTransaction()) {
    LOG3(("Http2Session::AddStream %p transaction %p forces keep-alive off.\n",
          this, aHttpTransaction));
    DontReuse();
  }
}

Result<already_AddRefed<nsHttpConnection>, nsresult>
Http2Session::CreateTunnelStream(nsAHttpTransaction* aHttpTransaction,
                                 nsIInterfaceRequestor* aCallbacks,
                                 PRIntervalTime aRtt, bool aIsExtendedCONNECT) {
  bool isWebTransport =
      aIsExtendedCONNECT && aHttpTransaction->IsForWebTransport();

  // Check if the WebTransport session limit is exceeded
  if (isWebTransport &&
      mOngoingWebTransportSessions >= mWebTransportMaxSessions) {
    LOG(
        ("Http2Session::CreateTunnelStream WebTransport session limit "
         "exceeded: Ongoing: %u, Max: %u",
         mOngoingWebTransportSessions + 1, mWebTransportMaxSessions));
    aHttpTransaction->Close(NS_ERROR_WEBTRANSPORT_SESSION_LIMIT_EXCEEDED);
    return Err(NS_ERROR_WEBTRANSPORT_SESSION_LIMIT_EXCEEDED);
  }

  RefPtr<Http2StreamTunnel> refStream = CreateTunnelStreamFromConnInfo(
      this, mCurrentBrowserId, aHttpTransaction->ConnectionInfo(),
      aIsExtendedCONNECT ? aHttpTransaction->IsForWebTransport()
                               ? ExtendedCONNECTType::WebTransport
                               : ExtendedCONNECTType::WebSocket
                         : ExtendedCONNECTType::Proxy);

  if (isWebTransport) {
    ++mOngoingWebTransportSessions;
  }

  RefPtr<nsHttpConnection> newConn = refStream->CreateHttpConnection(
      aHttpTransaction, aCallbacks, aRtt, aIsExtendedCONNECT);

  refStream->SetTransactionId(reinterpret_cast<uintptr_t>(aHttpTransaction));
  mTunnelStreams.AppendElement(std::move(refStream));
  return newConn.forget();
}

void Http2Session::QueueStream(Http2StreamBase* stream) {
  // will be removed via processpending or a shutdown path
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!stream->CountAsActive());
  MOZ_ASSERT(!stream->Queued());

  LOG3(("Http2Session::QueueStream %p stream %p queued.", this, stream));

  mQueueManager.AddStreamToQueue(Http2StreamQueueType::QueuedStreams, stream);
}

void Http2Session::ProcessPending() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  uint32_t available = RoomForMoreConcurrent();
  RefPtr<Http2StreamBase> stream;
  while (available && (stream = mQueueManager.GetNextStreamFromQueue(
                           Http2StreamQueueType::QueuedStreams))) {
    LOG3(("Http2Session::ProcessPending %p stream %p woken from queue.", this,
          stream.get()));
    MOZ_ASSERT(!stream->CountAsActive());
    mQueueManager.AddStreamToQueue(Http2StreamQueueType::ReadyForWrite, stream);
    SetWriteCallbacks();
    available--;
  }
}

nsresult Http2Session::NetworkRead(nsAHttpSegmentWriter* writer, char* buf,
                                   uint32_t count, uint32_t* countWritten) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (!count) {
    *countWritten = 0;
    return NS_OK;
  }

  nsresult rv = writer->OnWriteSegment(buf, count, countWritten);
  if (NS_SUCCEEDED(rv) && *countWritten > 0) {
    mLastReadEpoch = PR_IntervalNow();
  }
  return rv;
}

void Http2Session::SetWriteCallbacks() {
  if (mConnection &&
      (GetWriteQueueSize() || (mOutputQueueUsed > mOutputQueueSent))) {
    (void)mConnection->ResumeSend();
  }
}

void Http2Session::RealignOutputQueue() {
  if (mAttemptingEarlyData) {
    // We can't realign right now, because we may need what's in there if early
    // data fails.
    return;
  }

  mOutputQueueUsed -= mOutputQueueSent;
  memmove(mOutputQueueBuffer.get(), mOutputQueueBuffer.get() + mOutputQueueSent,
          mOutputQueueUsed);
  mOutputQueueSent = 0;
}

void Http2Session::FlushOutputQueue() {
  if (!mSegmentReader || !mOutputQueueUsed) return;

  nsresult rv;
  uint32_t countRead;
  uint32_t avail = mOutputQueueUsed - mOutputQueueSent;

  if (!avail && mAttemptingEarlyData) {
    // This is kind of a hack, but there are cases where we'll have already
    // written the data we want whlie doing early data, but we get called again
    // with a reader, and we need to avoid calling the reader when there's
    // nothing for it to read.
    return;
  }

  rv = mSegmentReader->OnReadSegment(
      mOutputQueueBuffer.get() + mOutputQueueSent, avail, &countRead);
  LOG3(("Http2Session::FlushOutputQueue %p sz=%d rv=%" PRIx32 " actual=%d",
        this, avail, static_cast<uint32_t>(rv), countRead));

  // Dont worry about errors on write, we will pick this up as a read error too
  if (NS_FAILED(rv)) return;

  mOutputQueueSent += countRead;

  if (mAttemptingEarlyData) {
    return;
  }

  if (countRead == avail) {
    mOutputQueueUsed = 0;
    mOutputQueueSent = 0;
    return;
  }

  // If the output queue is close to filling up and we have sent out a good
  // chunk of data from the beginning then realign it.

  if ((mOutputQueueSent >= kQueueMinimumCleanup) &&
      ((mOutputQueueSize - mOutputQueueUsed) < kQueueTailRoom)) {
    RealignOutputQueue();
  }
}

void Http2Session::DontReuse() {
  LOG3(("Http2Session::DontReuse %p\n", this));
  if (!OnSocketThread()) {
    LOG3(("Http2Session %p not on socket thread\n", this));
    nsCOMPtr<nsIRunnable> event = NewRunnableMethod(
        "Http2Session::DontReuse", this, &Http2Session::DontReuse);
    gSocketTransportService->Dispatch(event, NS_DISPATCH_NORMAL);
    return;
  }

  mShouldGoAway = true;
  if (!mClosed && IsDone()) {
    Close(NS_OK);
  }
}

enum SpdyVersion Http2Session::SpdyVersion() { return SpdyVersion::HTTP_2; }

uint32_t Http2Session::GetWriteQueueSize() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  return mQueueManager.GetWriteQueueSize();
}

void Http2Session::ChangeDownstreamState(enum internalStateType newState) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  LOG3(("Http2Session::ChangeDownstreamState() %p from %X to %X", this,
        mDownstreamState, newState));
  mDownstreamState = newState;
}

void Http2Session::ResetDownstreamState() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  LOG3(("Http2Session::ResetDownstreamState() %p", this));
  ChangeDownstreamState(BUFFERING_FRAME_HEADER);

  if (mInputFrameFinal && mInputFrameDataStream) {
    mInputFrameFinal = false;
    LOG3(("  SetRecvdFin id=0x%x\n", mInputFrameDataStream->StreamID()));
    mInputFrameDataStream->SetRecvdFin(true);
    MaybeDecrementConcurrent(mInputFrameDataStream);
  }
  mInputFrameFinal = false;
  mInputFrameBufferUsed = 0;
  mInputFrameDataStream = nullptr;
}

// return true if activated (and counted against max)
// otherwise return false and queue
bool Http2Session::TryToActivate(Http2StreamBase* aStream) {
  if (aStream->Queued()) {
    LOG3(("Http2Session::TryToActivate %p stream=%p already queued.\n", this,
          aStream));
    return false;
  }

  if (!RoomForMoreConcurrent()) {
    LOG3(
        ("Http2Session::TryToActivate %p stream=%p no room for more concurrent "
         "streams\n",
         this, aStream));
    QueueStream(aStream);
    return false;
  }

  LOG3(("Http2Session::TryToActivate %p stream=%p\n", this, aStream));
  IncrementConcurrent(aStream);

  mCntActivated++;
  return true;
}

void Http2Session::IncrementConcurrent(Http2StreamBase* stream) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!stream->StreamID() || (stream->StreamID() & 1),
             "Do not activate pushed streams");

  nsAHttpTransaction* trans = stream->Transaction();
  if (!trans || !trans->IsNullTransaction()) {
    MOZ_ASSERT(!stream->CountAsActive());
    stream->SetCountAsActive(true);
    ++mConcurrent;

    if (mConcurrent > mConcurrentHighWater) {
      mConcurrentHighWater = mConcurrent;
    }
    LOG3(
        ("Http2Session::IncrementCounter %p counting stream %p Currently %d "
         "streams in session, high water mark is %d\n",
         this, stream, mConcurrent, mConcurrentHighWater));
  }
}

// call with data length (i.e. 0 for 0 data bytes - ignore 9 byte header)
// dest must have 9 bytes of allocated space
template <typename charType>
void Http2Session::CreateFrameHeader(charType dest, uint16_t frameLength,
                                     uint8_t frameType, uint8_t frameFlags,
                                     uint32_t streamID) {
  MOZ_ASSERT(frameLength <= kMaxFrameData, "framelength too large");
  MOZ_ASSERT(!(streamID & 0x80000000));
  MOZ_ASSERT(!frameFlags || (frameType != FRAME_TYPE_PRIORITY &&
                             frameType != FRAME_TYPE_RST_STREAM &&
                             frameType != FRAME_TYPE_GOAWAY &&
                             frameType != FRAME_TYPE_WINDOW_UPDATE));

  dest[0] = 0x00;
  NetworkEndian::writeUint16(dest + 1, frameLength);
  dest[3] = frameType;
  dest[4] = frameFlags;
  NetworkEndian::writeUint32(dest + 5, streamID);
}

char* Http2Session::EnsureOutputBuffer(uint32_t spaceNeeded) {
  // this is an infallible allocation (if an allocation is
  // needed, which is probably isn't)
  EnsureBuffer(mOutputQueueBuffer, mOutputQueueUsed + spaceNeeded,
               mOutputQueueUsed, mOutputQueueSize);
  return mOutputQueueBuffer.get() + mOutputQueueUsed;
}

template void Http2Session::CreateFrameHeader(char* dest, uint16_t frameLength,
                                              uint8_t frameType,
                                              uint8_t frameFlags,
                                              uint32_t streamID);

template void Http2Session::CreateFrameHeader(uint8_t* dest,
                                              uint16_t frameLength,
                                              uint8_t frameType,
                                              uint8_t frameFlags,
                                              uint32_t streamID);

void Http2Session::MaybeDecrementConcurrent(Http2StreamBase* aStream) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("MaybeDecrementConcurrent %p id=0x%X concurrent=%d active=%d\n", this,
        aStream->StreamID(), mConcurrent, aStream->CountAsActive()));

  if (!aStream->CountAsActive()) return;

  MOZ_ASSERT(mConcurrent);
  aStream->SetCountAsActive(false);
  --mConcurrent;
  ProcessPending();
}

// Need to decompress some data in order to keep the compression
// context correct, but we really don't care what the result is
nsresult Http2Session::UncompressAndDiscard(bool isPush) {
  nsresult rv;
  nsAutoCString trash;

  rv = mDecompressor.DecodeHeaderBlock(
      reinterpret_cast<const uint8_t*>(mDecompressBuffer.BeginReading()),
      mDecompressBuffer.Length(), trash, isPush);
  mDecompressBuffer.Truncate();
  if (NS_FAILED(rv)) {
    LOG3(("Http2Session::UncompressAndDiscard %p Compression Error\n", this));
    mGoAwayReason = COMPRESSION_ERROR;
    return rv;
  }
  return NS_OK;
}

void Http2Session::GeneratePing(bool isAck) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http2Session::GeneratePing %p isAck=%d\n", this, isAck));

  char* packet = EnsureOutputBuffer(kFrameHeaderBytes + 8);
  mOutputQueueUsed += kFrameHeaderBytes + 8;

  if (isAck) {
    CreateFrameHeader(packet, 8, FRAME_TYPE_PING, kFlag_ACK, 0);
    memcpy(packet + kFrameHeaderBytes,
           mInputFrameBuffer.get() + kFrameHeaderBytes, 8);
  } else {
    CreateFrameHeader(packet, 8, FRAME_TYPE_PING, 0, 0);
    memset(packet + kFrameHeaderBytes, 0, 8);
  }

  LogIO(this, nullptr, "Generate Ping", packet, kFrameHeaderBytes + 8);
  FlushOutputQueue();
}

void Http2Session::GenerateSettingsAck() {
  // need to generate ack of this settings frame
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http2Session::GenerateSettingsAck %p\n", this));

  char* packet = EnsureOutputBuffer(kFrameHeaderBytes);
  mOutputQueueUsed += kFrameHeaderBytes;
  CreateFrameHeader(packet, 0, FRAME_TYPE_SETTINGS, kFlag_ACK, 0);
  LogIO(this, nullptr, "Generate Settings ACK", packet, kFrameHeaderBytes);
  FlushOutputQueue();
}

void Http2Session::GenerateRstStream(uint32_t aStatusCode, uint32_t aID) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  // make sure we don't do this twice for the same stream (at least if we
  // have a stream entry for it)
  Http2StreamBase* stream = mStreamIDHash.Get(aID);
  if (stream) {
    if (stream->SentReset()) return;
    stream->SetSentReset(true);
  }

  LOG3(("Http2Session::GenerateRst %p 0x%X %d\n", this, aID, aStatusCode));

  uint32_t frameSize = kFrameHeaderBytes + 4;
  char* packet = EnsureOutputBuffer(frameSize);
  mOutputQueueUsed += frameSize;
  CreateFrameHeader(packet, 4, FRAME_TYPE_RST_STREAM, 0, aID);

  NetworkEndian::writeUint32(packet + kFrameHeaderBytes, aStatusCode);

  LogIO(this, nullptr, "Generate Reset", packet, frameSize);
  FlushOutputQueue();
}

void Http2Session::GenerateGoAway(uint32_t aStatusCode) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http2Session::GenerateGoAway %p code=%X\n", this, aStatusCode));

  mClientGoAwayReason = aStatusCode;
  uint32_t frameSize = kFrameHeaderBytes + 8;
  char* packet = EnsureOutputBuffer(frameSize);
  mOutputQueueUsed += frameSize;

  CreateFrameHeader(packet, 8, FRAME_TYPE_GOAWAY, 0, 0);

  // last-good-stream-id are bytes 9-12 reflecting pushes
  NetworkEndian::writeUint32(packet + kFrameHeaderBytes, mOutgoingGoAwayID);

  // bytes 13-16 are the status code.
  NetworkEndian::writeUint32(packet + frameSize - 4, aStatusCode);

  LogIO(this, nullptr, "Generate GoAway", packet, frameSize);
  FlushOutputQueue();
}

// The Hello is comprised of
// 1] 24 octets of magic, which are designed to
// flush out silent but broken intermediaries
// 2] a settings frame which sets a small flow control window for pushes
// 3] a window update frame which creates a large session flow control window
// 4] 6 priority frames for streams which will never be opened with headers
//    these streams (3, 5, 7, 9, b, d) build a dependency tree that all other
//    streams will be direct leaves of.
void Http2Session::SendHello() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http2Session::SendHello %p\n", this));

  // sized for magic + 6 settings and a session window update and 6 priority
  // frames 24 magic, 33 for settings (9 header + 4 settings @6), 13 for window
  // update, 6 priority frames at 14 (9 + 5) each
  static const uint32_t maxSettings = 6;
  static const uint32_t prioritySize =
      kPriorityGroupCount * (kFrameHeaderBytes + 5);
  static const uint32_t maxDataLen =
      24 + kFrameHeaderBytes + maxSettings * 6 + 13 + prioritySize;
  char* packet = EnsureOutputBuffer(maxDataLen);
  memcpy(packet, kMagicHello, 24);
  mOutputQueueUsed += 24;
  LogIO(this, nullptr, "Magic Connection Header", packet, 24);

  packet = mOutputQueueBuffer.get() + mOutputQueueUsed;
  memset(packet, 0, maxDataLen - 24);

  // frame header will be filled in after we know how long the frame is
  uint8_t numberOfEntries = 0;

  // entries need to be listed in order by ID
  // 1st entry is bytes 9 to 14
  // 2nd entry is bytes 15 to 20
  // 3rd entry is bytes 21 to 26
  // 4th entry is bytes 27 to 32
  // 5th entry is bytes 33 to 38

  // Let the other endpoint know about our default HPACK decompress table size
  uint32_t maxHpackBufferSize = gHttpHandler->DefaultHpackBuffer();
  mDecompressor.SetInitialMaxBufferSize(maxHpackBufferSize);
  NetworkEndian::writeUint16(packet + kFrameHeaderBytes + (6 * numberOfEntries),
                             SETTINGS_TYPE_HEADER_TABLE_SIZE);
  NetworkEndian::writeUint32(
      packet + kFrameHeaderBytes + (6 * numberOfEntries) + 2,
      maxHpackBufferSize);
  numberOfEntries++;

  // We don't support HTTP/2 Push. Set SETTINGS_TYPE_ENABLE_PUSH to 0
  NetworkEndian::writeUint16(packet + kFrameHeaderBytes + (6 * numberOfEntries),
                             SETTINGS_TYPE_ENABLE_PUSH);
  // The value portion of the setting pair is already initialized to 0
  numberOfEntries++;

  // We might also want to set the SETTINGS_TYPE_MAX_CONCURRENT to 0
  // to indicate that we don't support any incoming push streams,
  // but some websites panic when we do that, so we don't by default.
  if (StaticPrefs::network_http_http2_send_push_max_concurrent_frame()) {
    NetworkEndian::writeUint16(
        packet + kFrameHeaderBytes + (6 * numberOfEntries),
        SETTINGS_TYPE_MAX_CONCURRENT);
    // The value portion of the setting pair is already initialized to 0
    numberOfEntries++;
  }
  mWaitingForSettingsAck = true;

  // Advertise the Push RWIN for the session, and on each new pull stream
  // send a window update
  NetworkEndian::writeUint16(packet + kFrameHeaderBytes + (6 * numberOfEntries),
                             SETTINGS_TYPE_INITIAL_WINDOW);
  NetworkEndian::writeUint32(
      packet + kFrameHeaderBytes + (6 * numberOfEntries) + 2, mPushAllowance);
  numberOfEntries++;

  // Make sure the other endpoint knows that we're sticking to the default max
  // frame size
  NetworkEndian::writeUint16(packet + kFrameHeaderBytes + (6 * numberOfEntries),
                             SETTINGS_TYPE_MAX_FRAME_SIZE);
  NetworkEndian::writeUint32(
      packet + kFrameHeaderBytes + (6 * numberOfEntries) + 2, kMaxFrameData);
  numberOfEntries++;

  bool disableRFC7540Priorities =
      !StaticPrefs::network_http_http2_enabled_deps() ||
      !gHttpHandler->CriticalRequestPrioritization();

  // See bug 1909666. Sending this new setting could break some websites.
  if (disableRFC7540Priorities &&
      StaticPrefs::network_http_http2_send_NO_RFC7540_PRI()) {
    NetworkEndian::writeUint16(
        packet + kFrameHeaderBytes + (6 * numberOfEntries),
        SETTINGS_NO_RFC7540_PRIORITIES);
    NetworkEndian::writeUint32(
        packet + kFrameHeaderBytes + (6 * numberOfEntries) + 2,
        disableRFC7540Priorities ? 1 : 0);
    numberOfEntries++;
  }

  MOZ_ASSERT(numberOfEntries <= maxSettings);
  uint32_t dataLen = 6 * numberOfEntries;
  CreateFrameHeader(packet, dataLen, FRAME_TYPE_SETTINGS, 0, 0);
  mOutputQueueUsed += kFrameHeaderBytes + dataLen;

  LogIO(this, nullptr, "Generate Settings", packet,
        kFrameHeaderBytes + dataLen);

  // now bump the local session window from 64KB
  uint32_t sessionWindowBump = mInitialRwin - kDefaultRwin;
  if (kDefaultRwin < mInitialRwin) {
    // send a window update for the session (Stream 0) for something large
    mLocalSessionWindow = mInitialRwin;

    packet = mOutputQueueBuffer.get() + mOutputQueueUsed;
    CreateFrameHeader(packet, 4, FRAME_TYPE_WINDOW_UPDATE, 0, 0);
    mOutputQueueUsed += kFrameHeaderBytes + 4;
    NetworkEndian::writeUint32(packet + kFrameHeaderBytes, sessionWindowBump);

    LOG3(("Session Window increase at start of session %p %u\n", this,
          sessionWindowBump));
    LogIO(this, nullptr, "Session Window Bump ", packet, kFrameHeaderBytes + 4);
  }

  if (!disableRFC7540Priorities) {
    mUseH2Deps = true;
    MOZ_ASSERT(mNextStreamID == kLeaderGroupID);
    CreatePriorityNode(kLeaderGroupID, 0, 200, "leader");
    mNextStreamID += 2;
    MOZ_ASSERT(mNextStreamID == kOtherGroupID);
    CreatePriorityNode(kOtherGroupID, 0, 100, "other");
    mNextStreamID += 2;
    MOZ_ASSERT(mNextStreamID == kBackgroundGroupID);
    CreatePriorityNode(kBackgroundGroupID, 0, 0, "background");
    mNextStreamID += 2;
    MOZ_ASSERT(mNextStreamID == kSpeculativeGroupID);
    CreatePriorityNode(kSpeculativeGroupID, kBackgroundGroupID, 0,
                       "speculative");
    mNextStreamID += 2;
    MOZ_ASSERT(mNextStreamID == kFollowerGroupID);
    CreatePriorityNode(kFollowerGroupID, kLeaderGroupID, 0, "follower");
    mNextStreamID += 2;
    MOZ_ASSERT(mNextStreamID == kUrgentStartGroupID);
    CreatePriorityNode(kUrgentStartGroupID, 0, 240, "urgentStart");
    mNextStreamID += 2;
    // Hey, you! YES YOU! If you add/remove any groups here, you almost
    // certainly need to change the lookup of the stream/ID hash in
    // Http2Session::OnTransportStatus. Yeah, that's right. YOU!
  }

  FlushOutputQueue();
}

void Http2Session::SendPriorityFrame(uint32_t streamID, uint32_t dependsOn,
                                     uint8_t weight) {
  // If mUseH2Deps is false, that means that we've sent
  // SETTINGS_NO_RFC7540_PRIORITIES = 1. Since the server must
  // ignore priority frames anyway, we can skip sending it.
  if (!UseH2Deps()) {
    return;
  }
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(
      ("Http2Session::SendPriorityFrame %p Frame 0x%X depends on 0x%X "
       "weight %d\n",
       this, streamID, dependsOn, weight));

  char* packet = CreatePriorityFrame(streamID, dependsOn, weight);

  LogIO(this, nullptr, "SendPriorityFrame", packet, kFrameHeaderBytes + 5);
  FlushOutputQueue();
}

void Http2Session::SendPriorityUpdateFrame(uint32_t streamID, uint8_t urgency,
                                           bool incremental) {
  CreatePriorityUpdateFrame(streamID, urgency, incremental);
  FlushOutputQueue();
}

char* Http2Session::CreatePriorityUpdateFrame(uint32_t streamID,
                                              uint8_t urgency,
                                              bool incremental) {
  // https://www.rfc-editor.org/rfc/rfc9218.html#section-7.1
  nsPrintfCString priorityFieldValue(
      "%s", urgency != 3 ? nsPrintfCString("u=%d", urgency).get() : "");
  size_t payloadSize = 4 + priorityFieldValue.Length();
  char* packet = EnsureOutputBuffer(kFrameHeaderBytes + payloadSize);
  // The Stream Identifier field (see Section 5.1.1 of [HTTP/2]) in the
  // PRIORITY_UPDATE frame header MUST be zero
  CreateFrameHeader(packet, payloadSize,
                    Http2Session::FRAME_TYPE_PRIORITY_UPDATE,
                    0,   // unused flags
                    0);  // streamID

  // Reserved (1),
  // Prioritized Stream ID (31),
  MOZ_ASSERT(!(streamID & 0x80000000));
  NetworkEndian::writeUint32(packet + kFrameHeaderBytes, streamID & 0x7FFFFFFF);
  // Priority Field Value (..),
  for (size_t i = 0; i < priorityFieldValue.Length(); ++i) {
    packet[kFrameHeaderBytes + 4 + i] = priorityFieldValue[i];
  }
  mOutputQueueUsed += kFrameHeaderBytes + payloadSize;

  LogIO(this, nullptr, "SendPriorityUpdateFrame", packet,
        kFrameHeaderBytes + payloadSize);
  return packet;
}

char* Http2Session::CreatePriorityFrame(uint32_t streamID, uint32_t dependsOn,
                                        uint8_t weight) {
  MOZ_ASSERT(streamID, "Priority on stream 0");
  char* packet = EnsureOutputBuffer(kFrameHeaderBytes + 5);
  CreateFrameHeader(packet, 5, FRAME_TYPE_PRIORITY, 0, streamID);
  mOutputQueueUsed += kFrameHeaderBytes + 5;
  NetworkEndian::writeUint32(packet + kFrameHeaderBytes,
                             dependsOn);   // depends on
  packet[kFrameHeaderBytes + 4] = weight;  // weight
  return packet;
}

void Http2Session::CreatePriorityNode(uint32_t streamID, uint32_t dependsOn,
                                      uint8_t weight, const char* label) {
  char* packet = CreatePriorityFrame(streamID, dependsOn, weight);

  LOG3(
      ("Http2Session %p generate Priority Frame 0x%X depends on 0x%X "
       "weight %d for %s class\n",
       this, streamID, dependsOn, weight, label));
  LogIO(this, nullptr, "Priority dep node", packet, kFrameHeaderBytes + 5);
}

// perform a bunch of integrity checks on the stream.
// returns true if passed, false (plus LOG and ABORT) if failed.
bool Http2Session::VerifyStream(Http2StreamBase* aStream,
                                uint32_t aOptionalID = 0) {
  // This is annoying, but at least it is O(1)
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

#ifndef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  // Only do the real verification in early beta builds
  return true;
#else   // MOZ_DIAGNOSTIC_ASSERT_ENABLED

  if (!aStream) return true;

  uint32_t test = 0;

  do {
    if (aStream->StreamID() == kDeadStreamID) break;

    test++;
    if (aStream->StreamID()) {
      Http2StreamBase* idStream = mStreamIDHash.Get(aStream->StreamID());

      test++;
      if (idStream != aStream) break;

      if (aOptionalID) {
        test++;
        if (idStream->StreamID() != aOptionalID) break;
      }
    }

    if (aStream->IsTunnel()) {
      return true;
    }

    nsAHttpTransaction* trans = aStream->Transaction();

    test++;
    if (!trans) break;

    test++;
    if (mStreamTransactionHash.GetWeak(trans) != aStream) break;

    // tests passed
    return true;
  } while (false);

  LOG3(
      ("Http2Session %p VerifyStream Failure %p stream->id=0x%X "
       "optionalID=0x%X trans=%p test=%d\n",
       this, aStream, aStream->StreamID(), aOptionalID, aStream->Transaction(),
       test));

  MOZ_DIAGNOSTIC_ASSERT(false, "VerifyStream");
  return false;
#endif  // DEBUG
}

// static
Http2StreamTunnel* Http2Session::CreateTunnelStreamFromConnInfo(
    Http2Session* session, uint64_t bcId, nsHttpConnectionInfo* info,
    ExtendedCONNECTType aType) {
  MOZ_ASSERT(info);
  MOZ_ASSERT(session);

  if (aType == ExtendedCONNECTType::WebTransport) {
    LOG(("Http2Session creating Http2WebTransportSession"));
    MOZ_ASSERT(session->GetExtendedCONNECTSupport() ==
               ExtendedCONNECTSupport::SUPPORTED);
    Http2WebTransportInitialSettings settings;
    settings.mInitialMaxStreamsUni =
        session->mInitialWebTransportMaxStreamsUnidi;
    settings.mInitialMaxStreamsBidi =
        session->mInitialWebTransportMaxStreamsBidi;
    settings.mInitialMaxStreamDataUni =
        session->mInitialWebTransportMaxStreamDataUnidi;
    settings.mInitialMaxStreamDataBidi =
        session->mInitialWebTransportMaxStreamDataBidi;
    settings.mInitialMaxData = session->mInitialWebTransportMaxData;
    return new Http2WebTransportSession(
        session, nsISupportsPriority::PRIORITY_NORMAL, bcId, info, settings);
  }

  if (aType == ExtendedCONNECTType::WebSocket) {
    LOG(("Http2Session creating Http2StreamWebSocket"));
    MOZ_ASSERT(session->GetExtendedCONNECTSupport() ==
               ExtendedCONNECTSupport::SUPPORTED);
    return new Http2StreamWebSocket(
        session, nsISupportsPriority::PRIORITY_NORMAL, bcId, info);
  }

  MOZ_ASSERT(info->UsingHttpProxy() && info->UsingConnect());
  MOZ_ASSERT(aType == ExtendedCONNECTType::Proxy);
  LOG(("Http2Session creating Http2StreamTunnel"));
  return new Http2StreamTunnel(session, nsISupportsPriority::PRIORITY_NORMAL,
                               bcId, info);
}

void Http2Session::CleanupStream(Http2StreamBase* aStream, nsresult aResult,
                                 errorType aResetCode) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http2Session::CleanupStream %p %p 0x%X %" PRIX32 "\n", this, aStream,
        aStream ? aStream->StreamID() : 0, static_cast<uint32_t>(aResult)));
  if (!aStream) {
    return;
  }

  if (aStream->DeferCleanup(aResult)) {
    LOG3(("Http2Session::CleanupStream 0x%X deferred\n", aStream->StreamID()));
    return;
  }

  if (!VerifyStream(aStream)) {
    LOG3(("Http2Session::CleanupStream failed to verify stream\n"));
    return;
  }

  // don't reset a stream that has recevied a fin or rst
  if (!aStream->RecvdFin() && !aStream->RecvdReset() && aStream->StreamID() &&
      !(mInputFrameFinal &&
        (aStream == mInputFrameDataStream))) {  // !(recvdfin with mark pending)
    LOG3(("Stream 0x%X had not processed recv FIN, sending RST code %X\n",
          aStream->StreamID(), aResetCode));
    GenerateRstStream(aResetCode, aStream->StreamID());
  }

  CloseStream(aStream, aResult);

  RemoveStreamFromQueues(aStream);
  RemoveStreamFromTables(aStream);

  mTunnelStreams.RemoveElement(aStream);

  if (mNeedsCleanup == aStream) {
    mNeedsCleanup = nullptr;
  }

  if (mShouldGoAway && IsDone()) {
    Close(NS_OK);
  }
}

void Http2Session::CleanupStream(uint32_t aID, nsresult aResult,
                                 errorType aResetCode) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  Http2StreamBase* stream = mStreamIDHash.Get(aID);
  LOG3(("Http2Session::CleanupStream %p by ID 0x%X to stream %p\n", this, aID,
        stream));
  if (!stream) {
    return;
  }
  CleanupStream(stream, aResult, aResetCode);
}

void Http2Session::RemoveStreamFromQueues(Http2StreamBase* aStream) {
  mQueueManager.RemoveStreamFromAllQueue(aStream);
}

void Http2Session::RemoveStreamFromTables(Http2StreamBase* aStream) {
  // Remove the stream from the ID hash table
  if (aStream->HasRegisteredID()) {
    mStreamIDHash.Remove(aStream->StreamID());
  }
  // removing from the stream transaction hash will
  // delete the Http2StreamBase and drop the reference to
  // its transaction
  mStreamTransactionHash.Remove(aStream->Transaction());
}

void Http2Session::CloseStream(Http2StreamBase* aStream, nsresult aResult,
                               bool aRemoveFromQueue) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http2Session::CloseStream %p %p 0x%x %" PRIX32 "\n", this, aStream,
        aStream->StreamID(), static_cast<uint32_t>(aResult)));

  MaybeDecrementConcurrent(aStream);

  // Check if partial frame reader
  if (aStream == mInputFrameDataStream) {
    LOG3(("Stream had active partial read frame on close"));
    ChangeDownstreamState(DISCARDING_DATA_FRAME);
    mInputFrameDataStream = nullptr;
  }

  if (aRemoveFromQueue) {
    RemoveStreamFromQueues(aStream);
  }

  RefPtr<nsHttpConnectionInfo> ci(aStream->ConnectionInfo());
  if ((NS_SUCCEEDED(aResult) || NS_BASE_STREAM_CLOSED == aResult) && ci &&
      ci->GetIsTrrServiceChannel()) {
    // save time of last successful response
    mLastTRRResponseTime = TimeStamp::Now();
  }

  // Send the stream the close() indication
  aStream->CloseStream(aResult);
}

nsresult Http2Session::SetInputFrameDataStream(uint32_t streamID) {
  mInputFrameDataStream = mStreamIDHash.Get(streamID);
  if (VerifyStream(mInputFrameDataStream, streamID)) return NS_OK;

  LOG3(("Http2Session::SetInputFrameDataStream failed to verify 0x%X\n",
        streamID));
  mInputFrameDataStream = nullptr;
  return NS_ERROR_UNEXPECTED;
}

nsresult Http2Session::ParsePadding(uint8_t& paddingControlBytes,
                                    uint16_t& paddingLength) {
  if (mInputFrameFlags & kFlag_PADDED) {
    paddingLength =
        *reinterpret_cast<uint8_t*>(&mInputFrameBuffer[kFrameHeaderBytes]);
    paddingControlBytes = 1;
  } else {
    paddingLength = 0;
    paddingControlBytes = 0;
  }

  if (static_cast<uint32_t>(paddingLength + paddingControlBytes) >
      mInputFrameDataSize) {
    // This is fatal to the session
    LOG3(
        ("Http2Session::ParsePadding %p stream 0x%x PROTOCOL_ERROR "
         "paddingLength %d > frame size %d\n",
         this, mInputFrameID, paddingLength, mInputFrameDataSize));
    return SessionError(PROTOCOL_ERROR);
  }

  return NS_OK;
}

nsresult Http2Session::RecvHeaders(Http2Session* self) {
  MOZ_ASSERT(self->mInputFrameType == FRAME_TYPE_HEADERS ||
             self->mInputFrameType == FRAME_TYPE_CONTINUATION);

  bool isContinuation = self->mExpectedHeaderID != 0;

  // If this doesn't have END_HEADERS set on it then require the next
  // frame to be HEADERS of the same ID
  bool endHeadersFlag = self->mInputFrameFlags & kFlag_END_HEADERS;

  if (endHeadersFlag) {
    self->mExpectedHeaderID = 0;
  } else {
    self->mExpectedHeaderID = self->mInputFrameID;
  }

  uint32_t priorityLen = 0;
  if (self->mInputFrameFlags & kFlag_PRIORITY) {
    priorityLen = 5;
  }
  nsresult rv = self->SetInputFrameDataStream(self->mInputFrameID);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  // Find out how much padding this frame has, so we can only extract the real
  // header data from the frame.
  uint16_t paddingLength = 0;
  uint8_t paddingControlBytes = 0;

  if (!isContinuation) {
    self->mDecompressBuffer.Truncate();
    rv = self->ParsePadding(paddingControlBytes, paddingLength);
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  LOG3((
      "Http2Session::RecvHeaders %p stream 0x%X priorityLen=%d stream=%p "
      "end_stream=%d end_headers=%d priority_group=%d "
      "paddingLength=%d padded=%d\n",
      self, self->mInputFrameID, priorityLen, self->mInputFrameDataStream.get(),
      self->mInputFrameFlags & kFlag_END_STREAM,
      self->mInputFrameFlags & kFlag_END_HEADERS,
      self->mInputFrameFlags & kFlag_PRIORITY, paddingLength,
      self->mInputFrameFlags & kFlag_PADDED));

  if ((paddingControlBytes + priorityLen + paddingLength) >
      self->mInputFrameDataSize) {
    // This is fatal to the session
    return self->SessionError(PROTOCOL_ERROR);
  }

  uint32_t frameSize = self->mInputFrameDataSize - paddingControlBytes -
                       priorityLen - paddingLength;
  if (self->mAggregatedHeaderSize + frameSize >
      StaticPrefs::network_http_max_response_header_size()) {
    LOG(("Http2Session %p header exceeds the limit\n", self));
    return self->SessionError(PROTOCOL_ERROR);
  }
  if (!self->mInputFrameDataStream) {
    // Cannot find stream. We can continue the session, but we need to
    // uncompress the header block to maintain the correct compression context

    LOG3(
        ("Http2Session::RecvHeaders %p lookup mInputFrameID stream "
         "0x%X failed. NextStreamID = 0x%X\n",
         self, self->mInputFrameID, self->mNextStreamID));

    if (self->mInputFrameID >= self->mNextStreamID) {
      self->GenerateRstStream(PROTOCOL_ERROR, self->mInputFrameID);
    }

    self->mDecompressBuffer.Append(
        &self->mInputFrameBuffer[kFrameHeaderBytes + paddingControlBytes +
                                 priorityLen],
        frameSize);

    if (self->mInputFrameFlags & kFlag_END_HEADERS) {
      rv = self->UncompressAndDiscard(false);
      if (NS_FAILED(rv)) {
        LOG3(("Http2Session::RecvHeaders uncompress failed\n"));
        // this is fatal to the session
        self->mGoAwayReason = COMPRESSION_ERROR;
        return rv;
      }
    }

    self->ResetDownstreamState();
    return NS_OK;
  }

  // make sure this is either the first headers or a trailer
  if (self->mInputFrameDataStream->AllHeadersReceived() &&
      !(self->mInputFrameFlags & kFlag_END_STREAM)) {
    // Any header block after the first that does *not* end the stream is
    // illegal.
    LOG3(("Http2Session::Illegal Extra HeaderBlock %p 0x%X\n", self,
          self->mInputFrameID));
    return self->SessionError(PROTOCOL_ERROR);
  }

  // queue up any compression bytes
  self->mDecompressBuffer.Append(
      &self->mInputFrameBuffer[kFrameHeaderBytes + paddingControlBytes +
                               priorityLen],
      frameSize);

  self->mInputFrameDataStream->UpdateTransportReadEvents(
      self->mInputFrameDataSize);
  self->mLastDataReadEpoch = self->mLastReadEpoch;

  if (!isContinuation) {
    self->mAggregatedHeaderSize = frameSize;
  } else {
    self->mAggregatedHeaderSize += frameSize;
  }

  if (!endHeadersFlag) {  // more are coming - don't process yet
    self->ResetDownstreamState();
    return NS_OK;
  }

  if (isContinuation) {
    glean::spdy::continued_headers.Accumulate(self->mAggregatedHeaderSize);
  }

  rv = self->ResponseHeadersComplete();
  if (rv == NS_ERROR_ILLEGAL_VALUE) {
    LOG3(("Http2Session::RecvHeaders %p PROTOCOL_ERROR detected stream 0x%X\n",
          self, self->mInputFrameID));
    self->CleanupStream(self->mInputFrameDataStream, rv, PROTOCOL_ERROR);
    self->ResetDownstreamState();
    rv = NS_OK;
  } else if (NS_FAILED(rv)) {
    // This is fatal to the session.
    self->mGoAwayReason = COMPRESSION_ERROR;
  }
  return rv;
}

// ResponseHeadersComplete() returns NS_ERROR_ILLEGAL_VALUE when the stream
// should be reset with a PROTOCOL_ERROR, NS_OK when the response headers were
// fine, and any other error is fatal to the session.
nsresult Http2Session::ResponseHeadersComplete() {
  if (!mInputFrameDataStream) {
    return NS_ERROR_UNEXPECTED;
  }

  LOG3(("Http2Session::ResponseHeadersComplete %p for 0x%X fin=%d", this,
        mInputFrameDataStream->StreamID(), mInputFrameFinal));

  // Anything prior to AllHeadersReceived() => true is actual headers. After
  // that, we need to handle them as trailers instead (which are special-cased
  // so we don't have to use the nasty chunked parser for all h2, just in case).
  if (mInputFrameDataStream->AllHeadersReceived()) {
    LOG3(("Http2Session::ResponseHeadersComplete processing trailers"));
    MOZ_ASSERT(mInputFrameFlags & kFlag_END_STREAM);
    nsresult rv = mInputFrameDataStream->ConvertResponseTrailers(
        &mDecompressor, mDecompressBuffer);
    if (NS_FAILED(rv)) {
      LOG3((
          "Http2Session::ResponseHeadersComplete trailer conversion failed\n"));
      return rv;
    }
    mFlatHTTPResponseHeadersOut = 0;
    mFlatHTTPResponseHeaders.Truncate();
    if (mInputFrameFinal) {
      // need to process the fin
      ChangeDownstreamState(PROCESSING_COMPLETE_HEADERS);
    } else {
      ResetDownstreamState();
    }

    return NS_OK;
  }

  // if this turns out to be a 1xx response code we have to
  // undo the headers received bit that we are setting here.
  bool didFirstSetAllRecvd = !mInputFrameDataStream->AllHeadersReceived();
  mInputFrameDataStream->SetAllHeadersReceived();

  // The stream needs to see flattened http headers
  // Uncompressed http/2 format headers currently live in
  // Http2StreamBase::mDecompressBuffer - convert that to HTTP format in
  // mFlatHTTPResponseHeaders via ConvertHeaders()

  nsresult rv;
  int32_t httpResponseCode;  // out param to ConvertResponseHeaders
  mFlatHTTPResponseHeadersOut = 0;
  rv = mInputFrameDataStream->ConvertResponseHeaders(
      &mDecompressor, mDecompressBuffer, mFlatHTTPResponseHeaders,
      httpResponseCode);
  if (rv == NS_ERROR_NET_RESET) {
    LOG(
        ("Http2Session::ResponseHeadersComplete %p ConvertResponseHeaders "
         "reset\n",
         this));
    // This means the stream found connection-oriented auth. Treat this like we
    // got a reset with HTTP_1_1_REQUIRED.
    mInputFrameDataStream->DisableSpdy();
    CleanupStream(mInputFrameDataStream, NS_ERROR_NET_RESET, CANCEL_ERROR);
    ResetDownstreamState();
    return NS_OK;
  }
  if (NS_FAILED(rv)) {
    return rv;
  }

  // allow more headers in the case of 1xx
  if (((httpResponseCode / 100) == 1) && didFirstSetAllRecvd) {
    mInputFrameDataStream->UnsetAllHeadersReceived();

    // Capture interim response timing
    RefPtr<nsAHttpTransaction> trans = mInputFrameDataStream->Transaction();
    if (trans) {
      nsHttpTransaction* httpTrans = trans->QueryHttpTransaction();
      if (httpTrans) {
        httpTrans->SetFirstInterimResponseStart(TimeStamp::Now(), true);
      }
    }
  } else if (didFirstSetAllRecvd) {
    // This is a final response (2xx/3xx/4xx/5xx) - capture final timing
    RefPtr<nsAHttpTransaction> trans = mInputFrameDataStream->Transaction();
    if (trans) {
      nsHttpTransaction* httpTrans = trans->QueryHttpTransaction();
      if (httpTrans) {
        httpTrans->SetFinalResponseHeadersStart(TimeStamp::Now(), true);

        // Update responseStart for backward compatibility
        TimeStamp firstInterim = httpTrans->GetFirstInterimResponseStart();
        if (!firstInterim.IsNull()) {
          httpTrans->SetResponseStart(firstInterim, true);
        } else {
          httpTrans->SetResponseStart(TimeStamp::Now(), true);
        }
      }
    }
  }

  ChangeDownstreamState(PROCESSING_COMPLETE_HEADERS);
  return NS_OK;
}

nsresult Http2Session::RecvPriority(Http2Session* self) {
  MOZ_ASSERT(self->mInputFrameType == FRAME_TYPE_PRIORITY);

  if (self->mInputFrameDataSize != 5) {
    LOG3(("Http2Session::RecvPriority %p wrong length data=%d\n", self,
          self->mInputFrameDataSize));
    return self->SessionError(PROTOCOL_ERROR);
  }

  if (!self->mInputFrameID) {
    LOG3(("Http2Session::RecvPriority %p stream ID of 0.\n", self));
    return self->SessionError(PROTOCOL_ERROR);
  }

  nsresult rv = self->SetInputFrameDataStream(self->mInputFrameID);
  if (NS_FAILED(rv)) return rv;

  uint32_t newPriorityDependency = NetworkEndian::readUint32(
      self->mInputFrameBuffer.get() + kFrameHeaderBytes);
  bool exclusive = !!(newPriorityDependency & 0x80000000);
  newPriorityDependency &= 0x7fffffff;
  uint8_t newPriorityWeight =
      *(self->mInputFrameBuffer.get() + kFrameHeaderBytes + 4);

  // undefined what it means when the server sends a priority frame. ignore it.
  LOG3(
      ("Http2Session::RecvPriority %p 0x%X received dependency=0x%X "
       "weight=%u exclusive=%d",
       self->mInputFrameDataStream.get(), self->mInputFrameID,
       newPriorityDependency, newPriorityWeight, exclusive));

  self->ResetDownstreamState();
  return NS_OK;
}

nsresult Http2Session::RecvRstStream(Http2Session* self) {
  MOZ_ASSERT(self->mInputFrameType == FRAME_TYPE_RST_STREAM);

  if (self->mInputFrameDataSize != 4) {
    LOG3(("Http2Session::RecvRstStream %p RST_STREAM wrong length data=%d",
          self, self->mInputFrameDataSize));
    return self->SessionError(PROTOCOL_ERROR);
  }

  if (!self->mInputFrameID) {
    LOG3(("Http2Session::RecvRstStream %p stream ID of 0.\n", self));
    return self->SessionError(PROTOCOL_ERROR);
  }

  self->mDownstreamRstReason = NetworkEndian::readUint32(
      self->mInputFrameBuffer.get() + kFrameHeaderBytes);

  LOG3(("Http2Session::RecvRstStream %p RST_STREAM Reason Code %u ID %x\n",
        self, self->mDownstreamRstReason, self->mInputFrameID));

  DebugOnly<nsresult> rv = self->SetInputFrameDataStream(self->mInputFrameID);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  if (!self->mInputFrameDataStream) {
    // if we can't find the stream just ignore it (4.2 closed)
    self->ResetDownstreamState();
    return NS_OK;
  }

  self->mInputFrameDataStream->SetRecvdReset(true);
  self->MaybeDecrementConcurrent(self->mInputFrameDataStream);
  self->ChangeDownstreamState(PROCESSING_CONTROL_RST_STREAM);
  return NS_OK;
}

nsresult Http2Session::RecvSettings(Http2Session* self) {
  MOZ_ASSERT(self->mInputFrameType == FRAME_TYPE_SETTINGS);

  if (self->mInputFrameID) {
    LOG3(("Http2Session::RecvSettings %p needs stream ID of 0. 0x%X\n", self,
          self->mInputFrameID));
    return self->SessionError(PROTOCOL_ERROR);
  }

  if (self->mInputFrameDataSize % 6) {
    // Number of Settings is determined by dividing by each 6 byte setting
    // entry. So the payload must be a multiple of 6.
    LOG3(("Http2Session::RecvSettings %p SETTINGS wrong length data=%d", self,
          self->mInputFrameDataSize));
    return self->SessionError(PROTOCOL_ERROR);
  }

  self->mReceivedSettings = true;

  uint32_t numEntries = self->mInputFrameDataSize / 6;
  LOG3(
      ("Http2Session::RecvSettings %p SETTINGS Control Frame "
       "with %d entries ack=%X",
       self, numEntries, self->mInputFrameFlags & kFlag_ACK));

  if ((self->mInputFrameFlags & kFlag_ACK) && self->mInputFrameDataSize) {
    LOG3(("Http2Session::RecvSettings %p ACK with non zero payload is err\n",
          self));
    return self->SessionError(PROTOCOL_ERROR);
  }

  for (uint32_t index = 0; index < numEntries; ++index) {
    uint8_t* setting =
        reinterpret_cast<uint8_t*>(self->mInputFrameBuffer.get()) +
        kFrameHeaderBytes + index * 6;

    uint16_t id = NetworkEndian::readUint16(setting);
    uint32_t value = NetworkEndian::readUint32(setting + 2);
    LOG3(("Settings ID %u, Value %u", id, value));

    switch (id) {
      case SETTINGS_TYPE_HEADER_TABLE_SIZE:
        LOG3(("Compression header table setting received: %d\n", value));
        self->mCompressor.SetMaxBufferSize(value);
        break;

      case SETTINGS_TYPE_ENABLE_PUSH:
        LOG3(("Client received an ENABLE Push SETTING. Odd.\n"));
        // nop
        break;

      case SETTINGS_TYPE_MAX_CONCURRENT:
        self->mMaxConcurrent = value;
        glean::spdy::settings_max_streams.AccumulateSingleSample(value);
        self->ProcessPending();
        break;

      case SETTINGS_TYPE_INITIAL_WINDOW: {
        int32_t delta = value - self->mServerInitialStreamWindow;
        self->mServerInitialStreamWindow = value;

        // SETTINGS only adjusts stream windows. Leave the session window alone.
        // We need to add the delta to all open streams (delta can be negative)
        for (const auto& stream : self->mStreamTransactionHash.Values()) {
          stream->UpdateServerReceiveWindow(delta);
        }
      } break;

      case SETTINGS_TYPE_MAX_FRAME_SIZE: {
        if ((value < kMaxFrameData) || (value >= 0x01000000)) {
          LOG3(("Received invalid max frame size 0x%X", value));
          return self->SessionError(PROTOCOL_ERROR);
        }
        // We stick to the default for simplicity's sake, so nothing to change
      } break;

      case SETTINGS_TYPE_ENABLE_CONNECT_PROTOCOL: {
        if (value == 1) {
          LOG3(("Enabling extended CONNECT"));
          self->mPeerAllowsExtendedCONNECT = true;
        } else if (value > 1) {
          LOG3(("Peer sent invalid value for ENABLE_CONNECT_PROTOCOL %d",
                value));
          return self->SessionError(PROTOCOL_ERROR);
        } else if (self->mPeerAllowsExtendedCONNECT) {
          LOG3(("Peer tried to re-disable extended CONNECT"));
          return self->SessionError(PROTOCOL_ERROR);
        }
        self->mHasTransactionWaitingForExtendedCONNECT = true;
      } break;

      case SETTINGS_WEBTRANSPORT_MAX_SESSIONS: {
        // If the value is 0, the server doesn't want to accept webtransport
        // session. An error will ultimately be returned when the transaction
        // attempts to create a webtransport session.
        LOG3(("SETTINGS_WEBTRANSPORT_MAX_SESSIONS set to %u", value));
        self->mWebTransportMaxSessions = value;
      } break;

      case SETTINGS_WEBTRANSPORT_INITIAL_MAX_DATA: {
        if (!self->mPeerAllowsExtendedCONNECT) {
          return self->SessionError(PROTOCOL_ERROR);
        }
        self->mInitialWebTransportMaxData = value;
      } break;

      case SETTINGS_WEBTRANSPORT_INITIAL_MAX_STREAM_DATA_UNI: {
        if (!self->mPeerAllowsExtendedCONNECT) {
          return self->SessionError(PROTOCOL_ERROR);
        }
        self->mInitialWebTransportMaxStreamDataUnidi = value;
      } break;

      case SETTINGS_WEBTRANSPORT_INITIAL_MAX_STREAM_DATA_BIDI: {
        if (!self->mPeerAllowsExtendedCONNECT) {
          return self->SessionError(PROTOCOL_ERROR);
        }
        self->mInitialWebTransportMaxStreamDataBidi = value;
      } break;

      case SETTINGS_WEBTRANSPORT_INITIAL_MAX_STREAMS_UNI: {
        if (!self->mPeerAllowsExtendedCONNECT) {
          return self->SessionError(PROTOCOL_ERROR);
        }
        self->mInitialWebTransportMaxStreamsUnidi = value;
      } break;

      case SETTINGS_WEBTRANSPORT_INITIAL_MAX_STREAMS_BIDI: {
        if (!self->mPeerAllowsExtendedCONNECT) {
          return self->SessionError(PROTOCOL_ERROR);
        }
        self->mInitialWebTransportMaxStreamsBidi = value;
      } break;

      default:
        LOG3(("Received an unknown SETTING id %d. Ignoring.", id));
        break;
    }
  }

  self->ResetDownstreamState();

  if (!(self->mInputFrameFlags & kFlag_ACK)) {
    self->GenerateSettingsAck();
  } else if (self->mWaitingForSettingsAck) {
    self->mGoAwayOnPush = true;
  }

  if (self->mHasTransactionWaitingForExtendedCONNECT) {
    // trigger a queued websockets transaction -- enabled or not
    LOG3(("Http2Sesssion::RecvSettings triggering queued transactions"));
    RefPtr<nsHttpConnectionInfo> ci;
    self->GetConnectionInfo(getter_AddRefs(ci));
    gHttpHandler->ConnMgr()->ProcessPendingQ(ci);
    self->mHasTransactionWaitingForExtendedCONNECT = false;
  }

  return NS_OK;
}

nsresult Http2Session::RecvPushPromise(Http2Session* self) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult Http2Session::RecvPing(Http2Session* self) {
  MOZ_ASSERT(self->mInputFrameType == FRAME_TYPE_PING);

  LOG3(("Http2Session::RecvPing %p PING Flags 0x%X.", self,
        self->mInputFrameFlags));

  if (self->mInputFrameDataSize != 8) {
    LOG3(("Http2Session::RecvPing %p PING had wrong amount of data %d", self,
          self->mInputFrameDataSize));
    return self->SessionError(FRAME_SIZE_ERROR);
  }

  if (self->mInputFrameID) {
    LOG3(("Http2Session::RecvPing %p PING needs stream ID of 0. 0x%X\n", self,
          self->mInputFrameID));
    return self->SessionError(PROTOCOL_ERROR);
  }

  if (self->mInputFrameFlags & kFlag_ACK) {
    // presumably a reply to our timeout ping.. don't reply to it
    self->mPingSentEpoch = 0;
    // We need to reset mPreviousUsed. If we don't, the next time
    // Http2Session::SendPing is called, it will have no effect.
    self->mPreviousUsed = false;
  } else {
    // reply with a ack'd ping
    self->GeneratePing(true);
  }

  self->ResetDownstreamState();
  return NS_OK;
}

nsresult Http2Session::RecvGoAway(Http2Session* self) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(self->mInputFrameType == FRAME_TYPE_GOAWAY);

  if (self->mInputFrameDataSize < 8) {
    // data > 8 is an opaque token that we can't interpret. NSPR Logs will
    // have the hex of all packets so there is no point in separately logging.
    LOG3(("Http2Session::RecvGoAway %p GOAWAY had wrong amount of data %d",
          self, self->mInputFrameDataSize));
    return self->SessionError(PROTOCOL_ERROR);
  }

  if (self->mInputFrameID) {
    LOG3(("Http2Session::RecvGoAway %p GOAWAY had non zero stream ID 0x%X\n",
          self, self->mInputFrameID));
    return self->SessionError(PROTOCOL_ERROR);
  }

  self->mConnection->SetCloseReason(ConnectionCloseReason::GO_AWAY);
  self->mShouldGoAway = true;
  self->mGoAwayID = NetworkEndian::readUint32(self->mInputFrameBuffer.get() +
                                              kFrameHeaderBytes);
  self->mGoAwayID &= 0x7fffffff;
  self->mCleanShutdown = true;
  self->mPeerGoAwayReason = NetworkEndian::readUint32(
      self->mInputFrameBuffer.get() + kFrameHeaderBytes + 4);

  // Find streams greater than the last-good ID and mark them for deletion
  // in the mGoAwayStreamsToRestart queue. The underlying transaction can be
  // restarted.
  for (const auto& stream : self->mStreamTransactionHash.Values()) {
    // these streams were not processed by the server and can be restarted.
    // Do that after the enumerator completes to avoid the risk of
    // a restart event re-entrantly modifying this hash. Be sure not to restart
    // a pushed (even numbered) stream
    if ((stream->StreamID() > self->mGoAwayID && (stream->StreamID() & 1)) ||
        !stream->HasRegisteredID()) {
      self->mGoAwayStreamsToRestart.Push(stream);
    }
  }

  // Process the streams marked for deletion and restart.
  size_t size = self->mGoAwayStreamsToRestart.GetSize();
  for (size_t count = 0; count < size; ++count) {
    Http2StreamBase* stream =
        static_cast<Http2StreamBase*>(self->mGoAwayStreamsToRestart.PopFront());

    if (self->mPeerGoAwayReason == HTTP_1_1_REQUIRED) {
      stream->DisableSpdy();
    }
    self->CloseStream(stream, NS_ERROR_NET_RESET);
    self->RemoveStreamFromTables(stream);
  }

  // Queued streams can also be deleted from this session and restarted
  // in another one. (they were never sent on the network so they implicitly
  // are not covered by the last-good id.
  RefPtr<Http2StreamBase> queuedStream;
  while ((queuedStream = self->mQueueManager.GetNextStreamFromQueue(
              Http2StreamQueueType::QueuedStreams))) {
    if (self->mPeerGoAwayReason == HTTP_1_1_REQUIRED) {
      queuedStream->DisableSpdy();
    }
    self->CloseStream(queuedStream, NS_ERROR_NET_RESET, false);
    self->RemoveStreamFromTables(queuedStream);
  }

  LOG3(
      ("Http2Session::RecvGoAway %p GOAWAY Last-Good-ID 0x%X status 0x%X "
       "live streams=%d\n",
       self, self->mGoAwayID, self->mPeerGoAwayReason,
       self->mStreamTransactionHash.Count()));

  self->ResetDownstreamState();
  return NS_OK;
}

nsresult Http2Session::RecvWindowUpdate(Http2Session* self) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(self->mInputFrameType == FRAME_TYPE_WINDOW_UPDATE);

  if (self->mInputFrameDataSize != 4) {
    LOG3(("Http2Session::RecvWindowUpdate %p Window Update wrong length %d\n",
          self, self->mInputFrameDataSize));
    return self->SessionError(PROTOCOL_ERROR);
  }

  uint32_t delta = NetworkEndian::readUint32(self->mInputFrameBuffer.get() +
                                             kFrameHeaderBytes);
  delta &= 0x7fffffff;

  LOG3(("Http2Session::RecvWindowUpdate %p len=%d Stream 0x%X.\n", self, delta,
        self->mInputFrameID));

  if (self->mInputFrameID) {  // stream window
    nsresult rv = self->SetInputFrameDataStream(self->mInputFrameID);
    if (NS_FAILED(rv)) return rv;

    RefPtr<Http2StreamBase> stream = self->mInputFrameDataStream.get();
    if (!stream) {
      LOG3(("Http2Session::RecvWindowUpdate %p lookup streamID 0x%X failed.\n",
            self, self->mInputFrameID));
      // only reset the session if the ID is one we haven't ever opened
      if (self->mInputFrameID >= self->mNextStreamID) {
        self->GenerateRstStream(PROTOCOL_ERROR, self->mInputFrameID);
      }
      self->ResetDownstreamState();
      return NS_OK;
    }

    if (delta == 0) {
      LOG3(("Http2Session::RecvWindowUpdate %p received 0 stream window update",
            self));
      self->CleanupStream(stream, NS_ERROR_ILLEGAL_VALUE, PROTOCOL_ERROR);
      self->ResetDownstreamState();
      return NS_OK;
    }

    int64_t oldRemoteWindow = stream->ServerReceiveWindow();
    stream->UpdateServerReceiveWindow(delta);
    if (stream->ServerReceiveWindow() >= 0x80000000) {
      // a window cannot reach 2^31 and be in compliance. Our calculations
      // are 64 bit safe though.
      LOG3(
          ("Http2Session::RecvWindowUpdate %p stream window "
           "exceeds 2^31 - 1\n",
           self));
      self->CleanupStream(stream, NS_ERROR_ILLEGAL_VALUE, FLOW_CONTROL_ERROR);
      self->ResetDownstreamState();
      return NS_OK;
    }

    LOG3(
        ("Http2Session::RecvWindowUpdate %p stream 0x%X window "
         "%" PRId64 " increased by %" PRIu32 " now %" PRId64 ".\n",
         self, self->mInputFrameID, oldRemoteWindow, delta,
         oldRemoteWindow + delta));

  } else {  // session window update
    if (delta == 0) {
      LOG3(
          ("Http2Session::RecvWindowUpdate %p received 0 session window update",
           self));
      return self->SessionError(PROTOCOL_ERROR);
    }

    int64_t oldRemoteWindow = self->mServerSessionWindow;
    self->mServerSessionWindow += delta;

    if (self->mServerSessionWindow >= 0x80000000) {
      // a window cannot reach 2^31 and be in compliance. Our calculations
      // are 64 bit safe though.
      LOG3(
          ("Http2Session::RecvWindowUpdate %p session window "
           "exceeds 2^31 - 1\n",
           self));
      return self->SessionError(FLOW_CONTROL_ERROR);
    }

    if ((oldRemoteWindow <= 0) && (self->mServerSessionWindow > 0)) {
      LOG3(
          ("Http2Session::RecvWindowUpdate %p restart session window\n", self));
      for (const auto& stream : self->mStreamTransactionHash.Values()) {
        MOZ_ASSERT(self->mServerSessionWindow > 0);

        if (!stream->BlockedOnRwin() || stream->ServerReceiveWindow() <= 0) {
          continue;
        }

        self->mQueueManager.AddStreamToQueue(
            Http2StreamQueueType::ReadyForWrite, stream);
        self->SetWriteCallbacks();
      }
    }
    LOG3(
        ("Http2Session::RecvWindowUpdate %p session window "
         "%" PRId64 " increased by %d now %" PRId64 ".\n",
         self, oldRemoteWindow, delta, oldRemoteWindow + delta));
  }

  self->ResetDownstreamState();
  return NS_OK;
}

nsresult Http2Session::RecvContinuation(Http2Session* self) {
  MOZ_ASSERT(self->mInputFrameType == FRAME_TYPE_CONTINUATION);
  MOZ_ASSERT(self->mInputFrameID);
  MOZ_ASSERT(self->mExpectedPushPromiseID || self->mExpectedHeaderID);
  MOZ_ASSERT(!(self->mExpectedPushPromiseID && self->mExpectedHeaderID));

  LOG3(
      ("Http2Session::RecvContinuation %p Flags 0x%X id 0x%X "
       "promise id 0x%X header id 0x%X\n",
       self, self->mInputFrameFlags, self->mInputFrameID,
       self->mExpectedPushPromiseID, self->mExpectedHeaderID));

  DebugOnly<nsresult> rv = self->SetInputFrameDataStream(self->mInputFrameID);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  if (!self->mInputFrameDataStream) {
    LOG3(("Http2Session::RecvContination stream ID 0x%X not found.",
          self->mInputFrameID));
    return self->SessionError(PROTOCOL_ERROR);
  }

  // continued headers
  if (self->mExpectedHeaderID) {
    self->mInputFrameFlags &= ~kFlag_PRIORITY;
    return RecvHeaders(self);
  }

  // continued push promise
  if (self->mInputFrameFlags & kFlag_END_HEADERS) {
    self->mInputFrameFlags &= ~kFlag_END_HEADERS;
    self->mInputFrameFlags |= kFlag_END_PUSH_PROMISE;
  }
  return RecvPushPromise(self);
}

class UpdateAltSvcEvent : public Runnable {
 public:
  UpdateAltSvcEvent(const nsCString& header, const nsCString& aOrigin,
                    nsHttpConnectionInfo* aCI)
      : Runnable("net::UpdateAltSvcEvent"),
        mHeader(header),
        mOrigin(aOrigin),
        mCI(aCI) {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(NS_IsMainThread());

    nsCString originScheme;
    nsCString originHost;
    int32_t originPort = -1;

    nsCOMPtr<nsIURI> uri;
    if (NS_FAILED(NS_NewURI(getter_AddRefs(uri), mOrigin))) {
      LOG(("UpdateAltSvcEvent origin does not parse %s\n", mOrigin.get()));
      return NS_OK;
    }
    uri->GetScheme(originScheme);
    uri->GetHost(originHost);
    uri->GetPort(&originPort);

    if (XRE_IsSocketProcess()) {
      AltServiceChild::ProcessHeader(
          mHeader, originScheme, originHost, originPort, mCI->GetUsername(),
          mCI->GetPrivate(), nullptr, mCI->ProxyInfo(), 0,
          mCI->GetOriginAttributes(), mCI);
      return NS_OK;
    }

    AltSvcMapping::ProcessHeader(mHeader, originScheme, originHost, originPort,
                                 mCI->GetUsername(), mCI->GetPrivate(), nullptr,
                                 mCI->ProxyInfo(), 0,
                                 mCI->GetOriginAttributes(), mCI);
    return NS_OK;
  }

 private:
  nsCString mHeader;
  nsCString mOrigin;
  RefPtr<nsHttpConnectionInfo> mCI;
  nsCOMPtr<nsIInterfaceRequestor> mCallbacks;
};

// defined as an http2 extension - alt-svc
// defines receipt of frame type 0x0A.. See AlternateSevices.h at least draft
// -06 sec 4 as this is an extension, never generate protocol error - just
// ignore problems
nsresult Http2Session::RecvAltSvc(Http2Session* self) {
  MOZ_ASSERT(self->mInputFrameType == FRAME_TYPE_ALTSVC);
  LOG3(("Http2Session::RecvAltSvc %p Flags 0x%X id 0x%X\n", self,
        self->mInputFrameFlags, self->mInputFrameID));

  if (self->mInputFrameDataSize < 2) {
    LOG3(("Http2Session::RecvAltSvc %p frame too small", self));
    self->ResetDownstreamState();
    return NS_OK;
  }

  uint16_t originLen = NetworkEndian::readUint16(self->mInputFrameBuffer.get() +
                                                 kFrameHeaderBytes);
  if (originLen + 2U > self->mInputFrameDataSize) {
    LOG3(("Http2Session::RecvAltSvc %p origin len too big for frame", self));
    self->ResetDownstreamState();
    return NS_OK;
  }

  if (!gHttpHandler->AllowAltSvc()) {
    LOG3(("Http2Session::RecvAltSvc %p frame alt service pref'd off", self));
    self->ResetDownstreamState();
    return NS_OK;
  }

  uint16_t altSvcFieldValueLen =
      static_cast<uint16_t>(self->mInputFrameDataSize) - 2U - originLen;
  LOG3((
      "Http2Session::RecvAltSvc %p frame originLen=%u altSvcFieldValueLen=%u\n",
      self, originLen, altSvcFieldValueLen));

  if (self->mInputFrameDataSize > 2000) {
    LOG3(("Http2Session::RecvAltSvc %p frame too large to parse sensibly",
          self));
    self->ResetDownstreamState();
    return NS_OK;
  }

  nsAutoCString origin;
  bool impliedOrigin = true;
  if (originLen) {
    origin.Assign(self->mInputFrameBuffer.get() + kFrameHeaderBytes + 2,
                  originLen);
    impliedOrigin = false;
  }

  nsAutoCString altSvcFieldValue;
  if (altSvcFieldValueLen) {
    altSvcFieldValue.Assign(
        self->mInputFrameBuffer.get() + kFrameHeaderBytes + 2 + originLen,
        altSvcFieldValueLen);
  }

  if (altSvcFieldValue.IsEmpty() ||
      !nsHttp::IsReasonableHeaderValue(altSvcFieldValue)) {
    LOG(
        ("Http2Session %p Alt-Svc Response Header seems unreasonable - "
         "skipping\n",
         self));
    self->ResetDownstreamState();
    return NS_OK;
  }

  if (self->mInputFrameID & 1) {
    // pulled streams apply to the origin of the pulled stream.
    // If the origin field is filled in the frame, the frame should be ignored
    if (!origin.IsEmpty()) {
      LOG(("Http2Session %p Alt-Svc pulled stream has non empty origin\n",
           self));
      self->ResetDownstreamState();
      return NS_OK;
    }

    if (NS_FAILED(self->SetInputFrameDataStream(self->mInputFrameID)) ||
        !self->mInputFrameDataStream ||
        !self->mInputFrameDataStream->Transaction() ||
        !self->mInputFrameDataStream->Transaction()->RequestHead()) {
      LOG3(
          ("Http2Session::RecvAltSvc %p got frame w/o origin on invalid stream",
           self));
      self->ResetDownstreamState();
      return NS_OK;
    }

    self->mInputFrameDataStream->Transaction()->RequestHead()->Origin(origin);
  } else if (!self->mInputFrameID) {
    // ID 0 streams must supply their own origin
    if (origin.IsEmpty()) {
      LOG(("Http2Session %p Alt-Svc Stream 0 has empty origin\n", self));
      self->ResetDownstreamState();
      return NS_OK;
    }
  } else {
    // handling of push streams is not defined. Let's ignore it
    LOG(("Http2Session %p Alt-Svc received on pushed stream - ignoring\n",
         self));
    self->ResetDownstreamState();
    return NS_OK;
  }

  RefPtr<nsHttpConnectionInfo> ci(self->ConnectionInfo());
  if (!self->mConnection || !ci) {
    LOG3(("Http2Session::RecvAltSvc %p no connection or conninfo for %d", self,
          self->mInputFrameID));
    self->ResetDownstreamState();
    return NS_OK;
  }

  if (!impliedOrigin) {
    bool okToReroute = true;
    nsCOMPtr<nsITLSSocketControl> ssl;
    self->mConnection->GetTLSSocketControl(getter_AddRefs(ssl));
    if (!ssl) {
      okToReroute = false;
    }

    // a little off main thread origin parser. This is a non critical function
    // because any alternate route created has to be verified anyhow
    nsAutoCString specifiedOriginHost;
    if (StringBeginsWith(origin, "https://"_ns,
                         nsCaseInsensitiveCStringComparator)) {
      specifiedOriginHost.Assign(origin.get() + 8, origin.Length() - 8);
    } else if (StringBeginsWith(origin, "http://"_ns,
                                nsCaseInsensitiveCStringComparator)) {
      specifiedOriginHost.Assign(origin.get() + 7, origin.Length() - 7);
    }

    int32_t colonOffset = specifiedOriginHost.FindCharInSet(":", 0);
    if (colonOffset != kNotFound) {
      specifiedOriginHost.Truncate(colonOffset);
    }

    if (okToReroute) {
      ssl->IsAcceptableForHost(specifiedOriginHost, &okToReroute);
    }

    if (!okToReroute) {
      LOG3(
          ("Http2Session::RecvAltSvc %p can't reroute non-authoritative origin "
           "%s",
           self, origin.BeginReading()));
      self->ResetDownstreamState();
      return NS_OK;
    }
  }

  RefPtr<UpdateAltSvcEvent> event =
      new UpdateAltSvcEvent(altSvcFieldValue, origin, ci);
  NS_DispatchToMainThread(event);
  self->ResetDownstreamState();
  return NS_OK;
}

void Http2Session::Received421(nsHttpConnectionInfo* ci) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http2Session::Recevied421 %p %d\n", this, mOriginFrameActivated));
  if (!mOriginFrameActivated || !ci) {
    return;
  }

  nsAutoCString key(ci->GetOrigin());
  key.Append(':');
  key.AppendInt(ci->OriginPort());
  mOriginFrame.Remove(key);
  LOG3(("Http2Session::Received421 %p key %s removed\n", this, key.get()));
}

nsresult Http2Session::RecvUnused(Http2Session* self) {
  LOG3(("Http2Session %p unknown frame type %x ignored\n", self,
        self->mInputFrameType));
  self->ResetDownstreamState();
  return NS_OK;
}

// defined as an http2 extension - origin
// defines receipt of frame type 0x0b..
// http://httpwg.org/http-extensions/origin-frame.html as this is an extension,
// never generate protocol error - just ignore problems
nsresult Http2Session::RecvOrigin(Http2Session* self) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(self->mInputFrameType == FRAME_TYPE_ORIGIN);
  LOG3(("Http2Session::RecvOrigin %p Flags 0x%X id 0x%X\n", self,
        self->mInputFrameFlags, self->mInputFrameID));

  if (self->mInputFrameFlags & 0x0F) {
    LOG3(("Http2Session::RecvOrigin %p leading flags must be 0", self));
    self->ResetDownstreamState();
    return NS_OK;
  }

  if (self->mInputFrameID) {
    LOG3(("Http2Session::RecvOrigin %p not stream 0", self));
    self->ResetDownstreamState();
    return NS_OK;
  }

  if (self->ConnectionInfo()->UsingProxy()) {
    LOG3(("Http2Session::RecvOrigin %p must not use proxy", self));
    self->ResetDownstreamState();
    return NS_OK;
  }

  uint32_t offset = 0;
  self->mOriginFrameActivated = true;

  while (self->mInputFrameDataSize >= (offset + 2U)) {
    uint16_t originLen = NetworkEndian::readUint16(
        self->mInputFrameBuffer.get() + kFrameHeaderBytes + offset);
    LOG3(("Http2Session::RecvOrigin %p origin extension defined as %d bytes\n",
          self, originLen));
    if (originLen + 2U + offset > self->mInputFrameDataSize) {
      LOG3(("Http2Session::RecvOrigin %p origin len too big for frame", self));
      break;
    }

    nsAutoCString originString;
    nsCOMPtr<nsIURI> originURL;
    originString.Assign(
        self->mInputFrameBuffer.get() + kFrameHeaderBytes + offset + 2,
        originLen);
    offset += originLen + 2;
    if (NS_FAILED(MakeOriginURL(originString, originURL))) {
      LOG3(
          ("Http2Session::RecvOrigin %p origin frame string %s failed to "
           "parse\n",
           self, originString.get()));
      continue;
    }

    LOG3(("Http2Session::RecvOrigin %p origin frame string %s parsed OK\n",
          self, originString.get()));
    if (!originURL->SchemeIs("https")) {
      LOG3(("Http2Session::RecvOrigin %p origin frame not https\n", self));
      continue;
    }

    int32_t port = -1;
    originURL->GetPort(&port);
    if (port == -1) {
      port = 443;
    }
    // dont use ->GetHostPort because we want explicit 443
    nsAutoCString host;
    originURL->GetHost(host);
    nsAutoCString key(host);
    key.Append(':');
    key.AppendInt(port);
    self->mOriginFrame.WithEntryHandle(key, [&](auto&& entry) {
      if (!entry) {
        entry.Insert(true);
        RefPtr<HttpConnectionBase> conn(self->HttpConnection());
        MOZ_ASSERT(conn.get());
        gHttpHandler->ConnMgr()->RegisterOriginCoalescingKey(conn, host, port);
      } else {
        LOG3(("Http2Session::RecvOrigin %p origin frame already in set\n",
              self));
      }
    });
  }

  self->ResetDownstreamState();
  return NS_OK;
}

nsresult Http2Session::RecvPriorityUpdate(Http2Session* self) {
  // https://www.rfc-editor.org/rfc/rfc9218.html#section-7.1-9
  // Servers MUST NOT send PRIORITY_UPDATE frames. If a client receives a
  //   PRIORITY_UPDATE frame, it MUST respond with a connection error of
  //   type PROTOCOL_ERROR.
  return self->SessionError(PROTOCOL_ERROR);
}

//-----------------------------------------------------------------------------
// nsAHttpTransaction. It is expected that nsHttpConnection is the caller
// of these methods
//-----------------------------------------------------------------------------

void Http2Session::OnTransportStatus(nsITransport* aTransport, nsresult aStatus,
                                     int64_t aProgress) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  switch (aStatus) {
      // These should appear only once, deliver to the first
      // transaction on the session.
    case NS_NET_STATUS_RESOLVING_HOST:
    case NS_NET_STATUS_RESOLVED_HOST:
    case NS_NET_STATUS_CONNECTING_TO:
    case NS_NET_STATUS_CONNECTED_TO:
    case NS_NET_STATUS_TLS_HANDSHAKE_STARTING:
    case NS_NET_STATUS_TLS_HANDSHAKE_ENDED: {
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

      if (aStatus == NS_NET_STATUS_TLS_HANDSHAKE_ENDED) {
        mFirstHttpTransaction = nullptr;
        mTlsHandshakeFinished = true;
      }
      break;
    }

    default:
      // The other transport events are ignored here because there is no good
      // way to map them to the right transaction in http/2. Instead, the events
      // are generated again from the http/2 code and passed directly to the
      // correct transaction.

      // NS_NET_STATUS_SENDING_TO:
      // This is generated by the socket transport when (part) of
      // a transaction is written out
      //
      // There is no good way to map it to the right transaction in http/2,
      // so it is ignored here and generated separately when the request
      // is sent from Http2StreamBase::TransmitFrame

      // NS_NET_STATUS_WAITING_FOR:
      // Created by nsHttpConnection when the request has been totally sent.
      // There is no good way to map it to the right transaction in http/2,
      // so it is ignored here and generated separately when the same
      // condition is complete in Http2StreamBase when there is no more
      // request body left to be transmitted.

      // NS_NET_STATUS_RECEIVING_FROM
      // Generated in session whenever we read a data frame or a HEADERS
      // that can be attributed to a particular stream/transaction

      break;
  }
}

// ReadSegments() is used to write data to the network. Generally, HTTP
// request data is pulled from the approriate transaction and
// converted to http/2 data. Sometimes control data like window-update are
// generated instead.

nsresult Http2Session::ReadSegmentsAgain(nsAHttpSegmentReader* reader,
                                         uint32_t count, uint32_t* countRead,
                                         bool* again) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  MOZ_DIAGNOSTIC_ASSERT(
      !mSegmentReader || !reader || (mSegmentReader == reader),
      "Inconsistent Write Function Callback");

  nsresult rv = ConfirmTLSProfile();
  if (NS_FAILED(rv)) {
    if (mGoAwayReason == INADEQUATE_SECURITY) {
      LOG3(
          ("Http2Session::ReadSegments %p returning INADEQUATE_SECURITY "
           "%" PRIx32,
           this, static_cast<uint32_t>(NS_ERROR_NET_INADEQUATE_SECURITY)));
      rv = NS_ERROR_NET_INADEQUATE_SECURITY;
    }
    return rv;
  }

  if (reader) mSegmentReader = reader;

  *countRead = 0;

  LOG3(("Http2Session::ReadSegments %p", this));

  RefPtr<Http2StreamBase> stream =
      mQueueManager.GetNextStreamFromQueue(Http2StreamQueueType::ReadyForWrite);

  if (!stream) {
    LOG3(("Http2Session %p could not identify a stream to write; suspending.",
          this));
    uint32_t availBeforeFlush = mOutputQueueUsed - mOutputQueueSent;
    FlushOutputQueue();
    uint32_t availAfterFlush = mOutputQueueUsed - mOutputQueueSent;
    if (availBeforeFlush != availAfterFlush) {
      LOG3(("Http2Session %p ResumeRecv After early flush in ReadSegments",
            this));
      (void)ResumeRecv();
    }
    SetWriteCallbacks();
    if (mAttemptingEarlyData) {
      // We can still try to send our preamble as early-data
      *countRead = mOutputQueueUsed - mOutputQueueSent;
      LOG(("Http2Session %p nothing to send because of 0RTT failed", this));
      (void)ResumeRecv();
    }
    return *countRead ? NS_OK : NS_BASE_STREAM_WOULD_BLOCK;
  }

  uint32_t earlyDataUsed = 0;
  if (mAttemptingEarlyData) {
    if (!stream->Do0RTT()) {
      LOG3(
          ("Http2Session %p will not get early data from Http2StreamBase %p "
           "0x%X",
           this, stream.get(), stream->StreamID()));
      FlushOutputQueue();
      SetWriteCallbacks();
      if (!mCannotDo0RTTStreams.Contains(stream)) {
        mCannotDo0RTTStreams.AppendElement(stream);
      }
      // We can still send our preamble
      *countRead = mOutputQueueUsed - mOutputQueueSent;
      return *countRead ? NS_OK : NS_BASE_STREAM_WOULD_BLOCK;
    }

    // Need to adjust this to only take as much as we can fit in with the
    // preamble/settings/priority stuff
    count -= (mOutputQueueUsed - mOutputQueueSent);

    // Keep track of this to add it into countRead later, as
    // stream->ReadSegments will likely change the value of mOutputQueueUsed.
    earlyDataUsed = mOutputQueueUsed - mOutputQueueSent;
  }

  LOG3(
      ("Http2Session %p will write from Http2StreamBase %p 0x%X "
       "block-input=%d block-output=%d\n",
       this, stream.get(), stream->StreamID(), stream->RequestBlockedOnRead(),
       stream->BlockedOnRwin()));

  rv = stream->ReadSegments(this, count, countRead);

  if (earlyDataUsed) {
    // Do this here because countRead could get reset somewhere down the rabbit
    // hole of stream->ReadSegments, and we want to make sure we return the
    // proper value to our caller.
    *countRead += earlyDataUsed;
  }

  if (mAttemptingEarlyData && !m0RTTStreams.Contains(stream)) {
    LOG3(("Http2Session::ReadSegmentsAgain adding stream %d to m0RTTStreams\n",
          stream->StreamID()));
    m0RTTStreams.AppendElement(stream);
  }

  // Not every permutation of stream->ReadSegents produces data (and therefore
  // tries to flush the output queue) - SENDING_FIN_STREAM can be an example
  // of that. But we might still have old data buffered that would be good
  // to flush.
  FlushOutputQueue();

  // Allow new server reads - that might be data or control information
  // (e.g. window updates or http replies) that are responses to these writes
  (void)ResumeRecv();

  if (stream->RequestBlockedOnRead()) {
    // We are blocked waiting for input - either more http headers or
    // any request body data. When more data from the request stream
    // becomes available the httptransaction will call conn->ResumeSend().

    LOG3(("Http2Session::ReadSegments %p dealing with block on read", this));

    // call readsegments again if there are other streams ready
    // to run in this session
    if (GetWriteQueueSize()) {
      rv = NS_OK;
    } else {
      rv = NS_BASE_STREAM_WOULD_BLOCK;
    }
    SetWriteCallbacks();
    return rv;
  }

  if (NS_FAILED(rv)) {
    LOG3(("Http2Session::ReadSegments %p may return FAIL code %" PRIX32, this,
          static_cast<uint32_t>(rv)));
    if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
      return rv;
    }

    CleanupStream(stream, rv, CANCEL_ERROR);
    if (SoftStreamError(rv)) {
      LOG3(("Http2Session::ReadSegments %p soft error override\n", this));
      *again = false;
      SetWriteCallbacks();
      rv = NS_OK;
    }
    return rv;
  }

  if (*countRead > 0) {
    LOG3(("Http2Session::ReadSegments %p stream=%p countread=%d", this,
          stream.get(), *countRead));
    mQueueManager.AddStreamToQueue(Http2StreamQueueType::ReadyForWrite, stream);
    SetWriteCallbacks();
    return rv;
  }

  if (stream->BlockedOnRwin()) {
    LOG3(("Http2Session %p will stream %p 0x%X suspended for flow control\n",
          this, stream.get(), stream->StreamID()));
    return NS_BASE_STREAM_WOULD_BLOCK;
  }

  LOG3(("Http2Session::ReadSegments %p stream=%p stream send complete", this,
        stream.get()));

  // call readsegments again if there are other streams ready
  // to go in this session
  SetWriteCallbacks();

  return rv;
}

nsresult Http2Session::ReadSegments(nsAHttpSegmentReader* reader,
                                    uint32_t count, uint32_t* countRead) {
  bool again = false;
  return ReadSegmentsAgain(reader, count, countRead, &again);
}

nsresult Http2Session::ReadyToProcessDataFrame(
    enum internalStateType newState) {
  MOZ_ASSERT(newState == PROCESSING_DATA_FRAME ||
             newState == DISCARDING_DATA_FRAME_PADDING);
  ChangeDownstreamState(newState);

  mLastDataReadEpoch = mLastReadEpoch;

  if (!mInputFrameID) {
    LOG3(("Http2Session::ReadyToProcessDataFrame %p data frame stream 0\n",
          this));
    return SessionError(PROTOCOL_ERROR);
  }

  nsresult rv = SetInputFrameDataStream(mInputFrameID);
  if (NS_FAILED(rv)) {
    LOG3(
        ("Http2Session::ReadyToProcessDataFrame %p lookup streamID 0x%X "
         "failed. probably due to verification.\n",
         this, mInputFrameID));
    return rv;
  }
  if (!mInputFrameDataStream) {
    LOG3(
        ("Http2Session::ReadyToProcessDataFrame %p lookup streamID 0x%X "
         "failed. Next = 0x%X",
         this, mInputFrameID, mNextStreamID));
    if (mInputFrameID >= mNextStreamID) {
      GenerateRstStream(PROTOCOL_ERROR, mInputFrameID);
    }
    ChangeDownstreamState(DISCARDING_DATA_FRAME);
  } else if (mInputFrameDataStream->RecvdFin() ||
             mInputFrameDataStream->RecvdReset() ||
             mInputFrameDataStream->SentReset()) {
    LOG3(
        ("Http2Session::ReadyToProcessDataFrame %p streamID 0x%X "
         "Data arrived for already server closed stream.\n",
         this, mInputFrameID));
    if (mInputFrameDataStream->RecvdFin() ||
        mInputFrameDataStream->RecvdReset()) {
      GenerateRstStream(STREAM_CLOSED_ERROR, mInputFrameID);
    }
    ChangeDownstreamState(DISCARDING_DATA_FRAME);
  } else if (mInputFrameDataSize == 0 && !mInputFrameFinal) {
    // Only if non-final because the stream properly handles final frames of any
    // size, and we want the stream to be able to notice its own end flag.
    LOG3(
        ("Http2Session::ReadyToProcessDataFrame %p streamID 0x%X "
         "Ignoring 0-length non-terminal data frame.",
         this, mInputFrameID));
    ChangeDownstreamState(DISCARDING_DATA_FRAME);
  } else if (newState == PROCESSING_DATA_FRAME &&
             !mInputFrameDataStream->AllHeadersReceived()) {
    LOG3(
        ("Http2Session::ReadyToProcessDataFrame %p streamID 0x%X "
         "Receiving data frame without having headers.",
         this, mInputFrameID));
    CleanupStream(mInputFrameDataStream, NS_ERROR_NET_HTTP2_SENT_GOAWAY,
                  PROTOCOL_ERROR);
    return NS_OK;
  }

  LOG3(
      ("Start Processing Data Frame. "
       "Session=%p Stream ID 0x%X Stream Ptr %p Fin=%d Len=%d",
       this, mInputFrameID, mInputFrameDataStream.get(), mInputFrameFinal,
       mInputFrameDataSize));
  UpdateLocalRwin(mInputFrameDataStream, mInputFrameDataSize);

  if (mInputFrameDataStream) {
    mInputFrameDataStream->SetRecvdData(true);
  }

  return NS_OK;
}

// WriteSegments() is used to read data off the socket. Generally this is
// just the http2 frame header and from there the appropriate *Stream
// is identified from the Stream-ID. The http transaction associated with
// that read then pulls in the data directly, which it will feed to
// OnWriteSegment(). That function will gateway it into http and feed
// it to the appropriate transaction.

// we call writer->OnWriteSegment via NetworkRead() to get a http2 header..
// and decide if it is data or control.. if it is control, just deal with it.
// if it is data, identify the stream
// call stream->WriteSegments which can call this::OnWriteSegment to get the
// data. It always gets full frames if they are part of the stream

nsresult Http2Session::WriteSegmentsAgain(nsAHttpSegmentWriter* writer,
                                          uint32_t count,
                                          uint32_t* countWritten, bool* again) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  LOG3(("Http2Session::WriteSegments %p InternalState %X\n", this,
        mDownstreamState));

  *countWritten = 0;

  if (mClosed) {
    LOG(("Http2Session::WriteSegments %p already closed", this));
    // We return NS_ERROR_ABORT (a "soft" error) here, so when this error is
    // propagated to another Http2Session, the Http2Session will not be closed
    // due to this error code.
    return NS_ERROR_ABORT;
  }

  nsresult rv = ConfirmTLSProfile();
  if (NS_FAILED(rv)) return rv;

  SetWriteCallbacks();

  // feed gecko channels that previously stopped consuming data
  // only take data from stored buffers
  RefPtr<Http2StreamBase> slowConsumer = mQueueManager.GetNextStreamFromQueue(
      Http2StreamQueueType::SlowConsumersReadyForRead);
  if (slowConsumer) {
    internalStateType savedState = mDownstreamState;
    mDownstreamState = NOT_USING_NETWORK;
    rv = ProcessSlowConsumer(slowConsumer, writer, count, countWritten);
    mDownstreamState = savedState;
    return rv;
  }

  // The BUFFERING_OPENING_SETTINGS state is just like any
  // BUFFERING_FRAME_HEADER except the only frame type it will allow is SETTINGS

  // The session layer buffers the leading 8 byte header of every frame.
  // Non-Data frames are then buffered for their full length, but data
  // frames (type 0) are passed through to the http stack unprocessed

  if (mDownstreamState == BUFFERING_OPENING_SETTINGS ||
      mDownstreamState == BUFFERING_FRAME_HEADER) {
    // The first 9 bytes of every frame is header information that
    // we are going to want to strip before passing to http. That is
    // true of both control and data packets.

    MOZ_ASSERT(mInputFrameBufferUsed < kFrameHeaderBytes,
               "Frame Buffer Used Too Large for State");

    rv = NetworkRead(writer, &mInputFrameBuffer[mInputFrameBufferUsed],
                     kFrameHeaderBytes - mInputFrameBufferUsed, countWritten);

    if (NS_FAILED(rv)) {
      LOG3(("Http2Session %p buffering frame header read failure %" PRIx32 "\n",
            this, static_cast<uint32_t>(rv)));
      // maybe just blocked reading from network
      if (rv == NS_BASE_STREAM_WOULD_BLOCK) rv = NS_OK;
      return rv;
    }

    LogIO(this, nullptr, "Reading Frame Header",
          &mInputFrameBuffer[mInputFrameBufferUsed], *countWritten);

    mInputFrameBufferUsed += *countWritten;

    if (mInputFrameBufferUsed < kFrameHeaderBytes) {
      LOG3(
          ("Http2Session::WriteSegments %p "
           "BUFFERING FRAME HEADER incomplete size=%d",
           this, mInputFrameBufferUsed));
      return rv;
    }

    // 3 bytes of length, 1 type byte, 1 flag byte, 1 unused bit, 31 bits of ID
    uint8_t totallyWastedByte = mInputFrameBuffer.get()[0];
    mInputFrameDataSize =
        NetworkEndian::readUint16(mInputFrameBuffer.get() + 1);
    if (totallyWastedByte || (mInputFrameDataSize > kMaxFrameData)) {
      LOG3(("Got frame too large 0x%02X%04X", totallyWastedByte,
            mInputFrameDataSize));
      return SessionError(PROTOCOL_ERROR);
    }
    mInputFrameType = *reinterpret_cast<uint8_t*>(mInputFrameBuffer.get() +
                                                  kFrameLengthBytes);
    mInputFrameFlags = *reinterpret_cast<uint8_t*>(
        mInputFrameBuffer.get() + kFrameLengthBytes + kFrameTypeBytes);
    mInputFrameID =
        NetworkEndian::readUint32(mInputFrameBuffer.get() + kFrameLengthBytes +
                                  kFrameTypeBytes + kFrameFlagBytes);
    mInputFrameID &= 0x7fffffff;
    mInputFrameDataRead = 0;

    if (mInputFrameType == FRAME_TYPE_DATA ||
        mInputFrameType == FRAME_TYPE_HEADERS) {
      mInputFrameFinal = mInputFrameFlags & kFlag_END_STREAM;
    } else {
      mInputFrameFinal = false;
    }

    mPaddingLength = 0;

    LOG3(("Http2Session::WriteSegments[%p::%" PRIu64 "] Frame Header Read "
          "type %X data len %u flags %x id 0x%X",
          this, mSerial, mInputFrameType, mInputFrameDataSize, mInputFrameFlags,
          mInputFrameID));

    // if mExpectedHeaderID is non 0, it means this frame must be a CONTINUATION
    // of a HEADERS frame with a matching ID (section 6.2)
    if (mExpectedHeaderID && ((mInputFrameType != FRAME_TYPE_CONTINUATION) ||
                              (mExpectedHeaderID != mInputFrameID))) {
      LOG3(
          ("Expected CONINUATION OF HEADERS for ID 0x%X\n", mExpectedHeaderID));
      return SessionError(PROTOCOL_ERROR);
    }

    // if mExpectedPushPromiseID is non 0, it means this frame must be a
    // CONTINUATION of a PUSH_PROMISE with a matching ID (section 6.2)
    if (mExpectedPushPromiseID &&
        ((mInputFrameType != FRAME_TYPE_CONTINUATION) ||
         (mExpectedPushPromiseID != mInputFrameID))) {
      LOG3(("Expected CONTINUATION of PUSH PROMISE for ID 0x%X\n",
            mExpectedPushPromiseID));
      return SessionError(PROTOCOL_ERROR);
    }

    if (mDownstreamState == BUFFERING_OPENING_SETTINGS &&
        mInputFrameType != FRAME_TYPE_SETTINGS) {
      LOG3(("First Frame Type Must Be Settings\n"));
      mPeerFailedHandshake = true;

      // Don't allow any more h2 connections to this host
      RefPtr<nsHttpConnectionInfo> ci = ConnectionInfo();
      if (ci) {
        gHttpHandler->ExcludeHttp2(ci);
      }

      // Go through and re-start all of our transactions with h2 disabled.
      for (const auto& stream : mStreamTransactionHash.Values()) {
        stream->DisableSpdy();
        CloseStream(stream, NS_ERROR_NET_RESET);
      }
      mStreamTransactionHash.Clear();
      return SessionError(PROTOCOL_ERROR);
    }

    if (mInputFrameType != FRAME_TYPE_DATA) {  // control frame
      EnsureBuffer(mInputFrameBuffer, mInputFrameDataSize + kFrameHeaderBytes,
                   kFrameHeaderBytes, mInputFrameBufferSize);
      ChangeDownstreamState(BUFFERING_CONTROL_FRAME);
    } else if (mInputFrameFlags & kFlag_PADDED) {
      ChangeDownstreamState(PROCESSING_DATA_FRAME_PADDING_CONTROL);
    } else {
      rv = ReadyToProcessDataFrame(PROCESSING_DATA_FRAME);
      if (NS_FAILED(rv)) {
        return rv;
      }
    }
  }

  if (mDownstreamState == PROCESSING_DATA_FRAME_PADDING_CONTROL) {
    MOZ_ASSERT(mInputFrameFlags & kFlag_PADDED,
               "Processing padding control on unpadded frame");

    MOZ_ASSERT(mInputFrameBufferUsed < (kFrameHeaderBytes + 1),
               "Frame buffer used too large for state");

    rv = NetworkRead(writer, &mInputFrameBuffer[mInputFrameBufferUsed],
                     (kFrameHeaderBytes + 1) - mInputFrameBufferUsed,
                     countWritten);

    if (NS_FAILED(rv)) {
      LOG3(
          ("Http2Session %p buffering data frame padding control read failure "
           "%" PRIx32 "\n",
           this, static_cast<uint32_t>(rv)));
      // maybe just blocked reading from network
      if (rv == NS_BASE_STREAM_WOULD_BLOCK) rv = NS_OK;
      return rv;
    }

    LogIO(this, nullptr, "Reading Data Frame Padding Control",
          &mInputFrameBuffer[mInputFrameBufferUsed], *countWritten);

    mInputFrameBufferUsed += *countWritten;

    if (mInputFrameBufferUsed - kFrameHeaderBytes < 1) {
      LOG3(
          ("Http2Session::WriteSegments %p "
           "BUFFERING DATA FRAME CONTROL PADDING incomplete size=%d",
           this, mInputFrameBufferUsed - 8));
      return rv;
    }

    ++mInputFrameDataRead;

    char* control = &mInputFrameBuffer[kFrameHeaderBytes];
    mPaddingLength = static_cast<uint8_t>(*control);

    LOG3(("Http2Session::WriteSegments %p stream 0x%X mPaddingLength=%d", this,
          mInputFrameID, mPaddingLength));

    if (1U + mPaddingLength > mInputFrameDataSize) {
      LOG3(
          ("Http2Session::WriteSegments %p stream 0x%X padding too large for "
           "frame",
           this, mInputFrameID));
      return SessionError(PROTOCOL_ERROR);
    }
    if (1U + mPaddingLength == mInputFrameDataSize) {
      // This frame consists entirely of padding, we can just discard it
      LOG3(
          ("Http2Session::WriteSegments %p stream 0x%X frame with only padding",
           this, mInputFrameID));
      rv = ReadyToProcessDataFrame(DISCARDING_DATA_FRAME_PADDING);
      if (NS_FAILED(rv)) {
        return rv;
      }
    } else {
      LOG3(
          ("Http2Session::WriteSegments %p stream 0x%X ready to read HTTP data",
           this, mInputFrameID));
      rv = ReadyToProcessDataFrame(PROCESSING_DATA_FRAME);
      if (NS_FAILED(rv)) {
        return rv;
      }
    }
  }

  if (mDownstreamState == PROCESSING_CONTROL_RST_STREAM) {
    nsresult streamCleanupCode;

    if (!mInputFrameDataStream) {
      return NS_ERROR_UNEXPECTED;
    }

    // There is no bounds checking on the error code.. we provide special
    // handling for a couple of cases and all others (including unknown) are
    // equivalent to cancel.
    if (mDownstreamRstReason == REFUSED_STREAM_ERROR) {
      streamCleanupCode = NS_ERROR_NET_RESET;  // can retry this 100% safely
      mInputFrameDataStream->ReuseConnectionOnRestartOK(true);
    } else if (mDownstreamRstReason == HTTP_1_1_REQUIRED) {
      streamCleanupCode = NS_ERROR_NET_RESET;
      mInputFrameDataStream->ReuseConnectionOnRestartOK(true);
      mInputFrameDataStream->DisableSpdy();
      // actually allow restart by unsticking
      mInputFrameDataStream->MakeNonSticky();
    } else {
      streamCleanupCode = mInputFrameDataStream->RecvdData()
                              ? NS_ERROR_NET_PARTIAL_TRANSFER
                              : NS_ERROR_NET_INTERRUPT;
    }

    if (mDownstreamRstReason == COMPRESSION_ERROR) {
      mShouldGoAway = true;
    }

    // mInputFrameDataStream is reset by ChangeDownstreamState
    Http2StreamBase* stream = mInputFrameDataStream;
    ResetDownstreamState();
    LOG3(
        ("Http2Session::WriteSegments cleanup stream on recv of rst "
         "session=%p stream=%p 0x%X\n",
         this, stream, stream ? stream->StreamID() : 0));
    CleanupStream(stream, streamCleanupCode, CANCEL_ERROR);
    return NS_OK;
  }

  if (mDownstreamState == PROCESSING_DATA_FRAME ||
      mDownstreamState == PROCESSING_COMPLETE_HEADERS) {
    // The cleanup stream should only be set while stream->WriteSegments is
    // on the stack and then cleaned up in this code block afterwards.
    MOZ_ASSERT(!mNeedsCleanup, "cleanup stream set unexpectedly");
    mNeedsCleanup = nullptr; /* just in case */

    if (!mInputFrameDataStream) {
      return NS_ERROR_UNEXPECTED;
    }

    RefPtr<Http2StreamBase> refStream = mInputFrameDataStream.get();
    uint32_t streamID = refStream->StreamID();
    mSegmentWriter = writer;
    rv = refStream->WriteSegments(this, count, countWritten);
    if (refStream->Closed() && NS_SUCCEEDED(rv)) {
      rv = NS_BASE_STREAM_CLOSED;
    }
    mSegmentWriter = nullptr;

    mLastDataReadEpoch = mLastReadEpoch;

    if (SoftStreamError(rv)) {
      // This will happen when the transaction figures out it is EOF, generally
      // due to a content-length match being made. Return OK from this function
      // otherwise the whole session would be torn down.

      // if we were doing PROCESSING_COMPLETE_HEADERS need to pop the state
      // back to PROCESSING_DATA_FRAME where we came from
      mDownstreamState = PROCESSING_DATA_FRAME;

      if (mInputFrameDataRead == mInputFrameDataSize) ResetDownstreamState();
      LOG3(
          ("Http2Session::WriteSegments session=%p id 0x%X "
           "needscleanup=%p. cleanup stream based on "
           "stream->writeSegments returning code %" PRIx32 "\n",
           this, streamID, mNeedsCleanup.get(), static_cast<uint32_t>(rv)));
      MOZ_ASSERT(!mNeedsCleanup || mNeedsCleanup->StreamID() == streamID);
      CleanupStream(
          streamID,
          (rv == NS_BINDING_RETARGETED) ? NS_BINDING_RETARGETED : NS_OK,
          CANCEL_ERROR);
      mNeedsCleanup = nullptr;
      *again = false;
      rv = ResumeRecv();
      if (NS_FAILED(rv)) {
        LOG3(("ResumeRecv returned code %x", static_cast<uint32_t>(rv)));
      }
      return NS_OK;
    }

    if (mNeedsCleanup) {
      LOG3(
          ("Http2Session::WriteSegments session=%p stream=%p 0x%X "
           "cleanup stream based on mNeedsCleanup.\n",
           this, mNeedsCleanup.get(),
           mNeedsCleanup ? mNeedsCleanup->StreamID() : 0));
      CleanupStream(mNeedsCleanup, NS_OK, CANCEL_ERROR);
      mNeedsCleanup = nullptr;
    }

    if (NS_FAILED(rv)) {
      LOG3(("Http2Session %p data frame read failure %" PRIx32 "\n", this,
            static_cast<uint32_t>(rv)));
      // maybe just blocked reading from network
      if (rv == NS_BASE_STREAM_WOULD_BLOCK) rv = NS_OK;
    }

    return rv;
  }

  if (mDownstreamState == DISCARDING_DATA_FRAME ||
      mDownstreamState == DISCARDING_DATA_FRAME_PADDING) {
    char trash[4096];
    uint32_t discardCount =
        std::min(mInputFrameDataSize - mInputFrameDataRead, 4096U);
    LOG3(("Http2Session::WriteSegments %p trying to discard %d bytes of %s",
          this, discardCount,
          mDownstreamState == DISCARDING_DATA_FRAME ? "data" : "padding"));

    if (!discardCount && mDownstreamState == DISCARDING_DATA_FRAME) {
      // Only do this short-cirtuit if we're not discarding a pure padding
      // frame, as we need to potentially handle the stream FIN in those cases.
      // See bug 1381016 comment 36 for more details.
      ResetDownstreamState();
      (void)ResumeRecv();
      return NS_BASE_STREAM_WOULD_BLOCK;
    }

    rv = NetworkRead(writer, trash, discardCount, countWritten);

    if (NS_FAILED(rv)) {
      LOG3(("Http2Session %p discard frame read failure %" PRIx32 "\n", this,
            static_cast<uint32_t>(rv)));
      // maybe just blocked reading from network
      if (rv == NS_BASE_STREAM_WOULD_BLOCK) rv = NS_OK;
      return rv;
    }

    LogIO(this, nullptr, "Discarding Frame", trash, *countWritten);

    mInputFrameDataRead += *countWritten;

    if (mInputFrameDataRead == mInputFrameDataSize) {
      Http2StreamBase* streamToCleanup = nullptr;
      if (mInputFrameFinal) {
        streamToCleanup = mInputFrameDataStream;
      }

      ResetDownstreamState();

      if (streamToCleanup) {
        CleanupStream(streamToCleanup, NS_OK, CANCEL_ERROR);
      }
    }
    return rv;
  }

  if (mDownstreamState != BUFFERING_CONTROL_FRAME) {
    MOZ_ASSERT(false);  // this cannot happen
    return NS_ERROR_UNEXPECTED;
  }

  MOZ_ASSERT(mInputFrameBufferUsed == kFrameHeaderBytes,
             "Frame Buffer Header Not Present");
  MOZ_ASSERT(mInputFrameDataSize + kFrameHeaderBytes <= mInputFrameBufferSize,
             "allocation for control frame insufficient");

  rv = NetworkRead(writer,
                   &mInputFrameBuffer[kFrameHeaderBytes + mInputFrameDataRead],
                   mInputFrameDataSize - mInputFrameDataRead, countWritten);

  if (NS_FAILED(rv)) {
    LOG3(("Http2Session %p buffering control frame read failure %" PRIx32 "\n",
          this, static_cast<uint32_t>(rv)));
    // maybe just blocked reading from network
    if (rv == NS_BASE_STREAM_WOULD_BLOCK) rv = NS_OK;
    return rv;
  }

  LogIO(this, nullptr, "Reading Control Frame",
        &mInputFrameBuffer[kFrameHeaderBytes + mInputFrameDataRead],
        *countWritten);

  mInputFrameDataRead += *countWritten;

  if (mInputFrameDataRead != mInputFrameDataSize) return NS_OK;

  MOZ_ASSERT(mInputFrameType != FRAME_TYPE_DATA);
  if (mInputFrameType < std::size(sControlFunctions)) {
    rv = sControlFunctions[mInputFrameType](this);
  } else {
    // Section 4.1 requires this to be ignored; though protocol_error would
    // be better
    LOG3(("Http2Session %p unknown frame type %x ignored\n", this,
          mInputFrameType));
    ResetDownstreamState();
    rv = NS_OK;
  }

  MOZ_ASSERT(NS_FAILED(rv) || mDownstreamState != BUFFERING_CONTROL_FRAME,
             "Control Handler returned OK but did not change state");

  if (mShouldGoAway && IsDone()) {
    Close(NS_OK);
  }
  return rv;
}

nsresult Http2Session::WriteSegments(nsAHttpSegmentWriter* writer,
                                     uint32_t count, uint32_t* countWritten) {
  bool again = false;
  return WriteSegmentsAgain(writer, count, countWritten, &again);
}

nsresult Http2Session::Finish0RTT(bool aRestart, bool aAlpnChanged) {
  MOZ_ASSERT(mAttemptingEarlyData);
  LOG3(("Http2Session::Finish0RTT %p aRestart=%d aAlpnChanged=%d", this,
        aRestart, aAlpnChanged));

  for (size_t i = 0; i < m0RTTStreams.Length(); ++i) {
    if (m0RTTStreams[i]) {
      m0RTTStreams[i]->Finish0RTT(aRestart, aAlpnChanged);
    }
  }

  if (aRestart) {
    // 0RTT failed
    if (aAlpnChanged) {
      // This is a slightly more involved case - we need to get all our streams/
      // transactions back in the queue so they can restart as http/1

      // These must be set this way to ensure we gracefully restart all streams
      mGoAwayID = 0;
      mCleanShutdown = true;

      // Close takes care of the rest of our work for us. The reason code here
      // doesn't matter, as we aren't actually going to send a GOAWAY frame, but
      // we use NS_ERROR_NET_RESET as it's closest to the truth.
      Close(NS_ERROR_NET_RESET);
    } else {
      // This is the easy case - early data failed, but we're speaking h2, so
      // we just need to rewind to the beginning of the preamble and try again.
      mOutputQueueSent = 0;

      for (size_t i = 0; i < mCannotDo0RTTStreams.Length(); ++i) {
        if (mCannotDo0RTTStreams[i] && VerifyStream(mCannotDo0RTTStreams[i])) {
          TransactionHasDataToWrite(mCannotDo0RTTStreams[i]);
        }
      }
    }
  } else {
    // 0RTT succeeded
    for (size_t i = 0; i < mCannotDo0RTTStreams.Length(); ++i) {
      if (mCannotDo0RTTStreams[i] && VerifyStream(mCannotDo0RTTStreams[i])) {
        TransactionHasDataToWrite(mCannotDo0RTTStreams[i]);
      }
    }
    // Make sure we look for any incoming data in repsonse to our early data.
    (void)ResumeRecv();
  }

  mAttemptingEarlyData = false;
  m0RTTStreams.Clear();
  mCannotDo0RTTStreams.Clear();
  RealignOutputQueue();

  return NS_OK;
}

nsresult Http2Session::ProcessConnectedPush(
    Http2StreamBase* pushConnectedStream, nsAHttpSegmentWriter* writer,
    uint32_t count, uint32_t* countWritten) {
  return nsresult::NS_ERROR_NOT_IMPLEMENTED;
}

nsresult Http2Session::ProcessSlowConsumer(Http2StreamBase* slowConsumer,
                                           nsAHttpSegmentWriter* writer,
                                           uint32_t count,
                                           uint32_t* countWritten) {
  LOG3(("Http2Session::ProcessSlowConsumer %p 0x%X\n", this,
        slowConsumer->StreamID()));
  mSegmentWriter = writer;
  nsresult rv = slowConsumer->WriteSegments(this, count, countWritten);
  mSegmentWriter = nullptr;
  LOG3(("Http2Session::ProcessSlowConsumer Writesegments %p 0x%X rv %" PRIX32
        " %d\n",
        this, slowConsumer->StreamID(), static_cast<uint32_t>(rv),
        *countWritten));
  if (NS_SUCCEEDED(rv) && !*countWritten && slowConsumer->RecvdFin()) {
    rv = NS_BASE_STREAM_CLOSED;
  }

  if (NS_SUCCEEDED(rv) && (*countWritten > 0)) {
    // There have been buffered bytes successfully fed into the
    // formerly blocked consumer. Repeat until buffer empty or
    // consumer is blocked again.
    UpdateLocalRwin(slowConsumer, 0);
    ConnectSlowConsumer(slowConsumer);
  }

  if (rv == NS_BASE_STREAM_CLOSED) {
    CleanupStream(slowConsumer, NS_OK, CANCEL_ERROR);
    rv = NS_OK;
  }

  return rv;
}

void Http2Session::UpdateLocalStreamWindow(Http2StreamBase* stream,
                                           uint32_t bytes) {
  if (!stream) {  // this is ok - it means there was a data frame for a rst
                  // stream
    return;
  }

  // If this data packet was not for a valid or live stream then there
  // is no reason to mess with the flow control
  if (!stream || stream->RecvdFin() || stream->RecvdReset() ||
      mInputFrameFinal) {
    return;
  }

  stream->DecrementClientReceiveWindow(bytes);

  // Don't necessarily ack every data packet. Only do it
  // after a significant amount of data.
  uint64_t unacked = stream->LocalUnAcked();
  int64_t localWindow = stream->ClientReceiveWindow();

  LOG3(
      ("Http2Session::UpdateLocalStreamWindow this=%p id=0x%X newbytes=%u "
       "unacked=%" PRIu64 " localWindow=%" PRId64 "\n",
       this, stream->StreamID(), bytes, unacked, localWindow));

  if (!unacked) return;

  if ((unacked < kMinimumToAck) && (localWindow > kEmergencyWindowThreshold)) {
    return;
  }

  if (!stream->HasSink()) {
    LOG3(
        ("Http2Session::UpdateLocalStreamWindow %p 0x%X Pushed Stream Has No "
         "Sink\n",
         this, stream->StreamID()));
    return;
  }

  // Generate window updates directly out of session instead of the stream
  // in order to avoid queue delays in getting the 'ACK' out.
  uint32_t toack = (unacked <= 0x7fffffffU) ? unacked : 0x7fffffffU;

  LOG3(
      ("Http2Session::UpdateLocalStreamWindow Ack this=%p id=0x%X acksize=%d\n",
       this, stream->StreamID(), toack));
  stream->IncrementClientReceiveWindow(toack);
  if (toack == 0) {
    // Ensure we never send an illegal 0 window update
    return;
  }

  // room for this packet needs to be ensured before calling this function
  char* packet = mOutputQueueBuffer.get() + mOutputQueueUsed;
  mOutputQueueUsed += kFrameHeaderBytes + 4;
  MOZ_ASSERT(mOutputQueueUsed <= mOutputQueueSize);

  CreateFrameHeader(packet, 4, FRAME_TYPE_WINDOW_UPDATE, 0, stream->StreamID());
  NetworkEndian::writeUint32(packet + kFrameHeaderBytes, toack);

  LogIO(this, stream, "Stream Window Update", packet, kFrameHeaderBytes + 4);
  // dont flush here, this write can commonly be coalesced with a
  // session window update to immediately follow.
}

void Http2Session::UpdateLocalSessionWindow(uint32_t bytes) {
  if (!bytes) return;

  mLocalSessionWindow -= bytes;

  LOG3(
      ("Http2Session::UpdateLocalSessionWindow this=%p newbytes=%u "
       "localWindow=%" PRId64 "\n",
       this, bytes, mLocalSessionWindow));

  // Don't necessarily ack every data packet. Only do it
  // after a significant amount of data.
  if ((mLocalSessionWindow > (mInitialRwin - kMinimumToAck)) &&
      (mLocalSessionWindow > kEmergencyWindowThreshold)) {
    return;
  }

  // Only send max  bits of window updates at a time.
  uint64_t toack64 = mInitialRwin - mLocalSessionWindow;
  uint32_t toack = (toack64 <= 0x7fffffffU) ? toack64 : 0x7fffffffU;

  LOG3(("Http2Session::UpdateLocalSessionWindow Ack this=%p acksize=%u\n", this,
        toack));
  mLocalSessionWindow += toack;

  if (toack == 0) {
    // Ensure we never send an illegal 0 window update
    return;
  }

  // room for this packet needs to be ensured before calling this function
  char* packet = mOutputQueueBuffer.get() + mOutputQueueUsed;
  mOutputQueueUsed += kFrameHeaderBytes + 4;
  MOZ_ASSERT(mOutputQueueUsed <= mOutputQueueSize);

  CreateFrameHeader(packet, 4, FRAME_TYPE_WINDOW_UPDATE, 0, 0);
  NetworkEndian::writeUint32(packet + kFrameHeaderBytes, toack);

  LogIO(this, nullptr, "Session Window Update", packet, kFrameHeaderBytes + 4);
  // dont flush here, this write can commonly be coalesced with others
}

void Http2Session::UpdateLocalRwin(Http2StreamBase* stream, uint32_t bytes) {
  // make sure there is room for 2 window updates even though
  // we may not generate any.
  EnsureOutputBuffer(2 * (kFrameHeaderBytes + 4));

  UpdateLocalStreamWindow(stream, bytes);
  UpdateLocalSessionWindow(bytes);
  FlushOutputQueue();
}

void Http2Session::Close(nsresult aReason) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (mClosed) return;

  LOG3(("Http2Session::Close %p %" PRIX32, this,
        static_cast<uint32_t>(aReason)));

  mClosed = true;

  if (!mLastTRRResponseTime.IsNull()) {
    RefPtr<nsHttpConnectionInfo> ci;
    GetConnectionInfo(getter_AddRefs(ci));
    if (ci && ci->GetIsTrrServiceChannel() && mCleanShutdown) {
      // Record telemetry keyed by TRR provider.
      glean::network::trr_idle_close_time_h2.Get(TRRProviderKey())
          .AccumulateRawDuration(TimeStamp::Now() - mLastTRRResponseTime);
    }
    mLastTRRResponseTime = TimeStamp();
  }

  Shutdown(aReason);

  mStreamIDHash.Clear();
  mStreamTransactionHash.Clear();
  mTunnelStreams.Clear();

  uint32_t goAwayReason;
  if (mGoAwayReason != NO_HTTP_ERROR) {
    goAwayReason = mGoAwayReason;
  } else if (NS_SUCCEEDED(aReason)) {
    goAwayReason = NO_HTTP_ERROR;
  } else if (aReason == NS_ERROR_NET_HTTP2_SENT_GOAWAY) {
    goAwayReason = PROTOCOL_ERROR;
  } else if (mCleanShutdown) {
    goAwayReason = NO_HTTP_ERROR;
  } else {
    goAwayReason = INTERNAL_ERROR;
  }
  if (!mAttemptingEarlyData) {
    GenerateGoAway(goAwayReason);
  }
  mConnection = nullptr;
  mSegmentReader = nullptr;
  mSegmentWriter = nullptr;
}

nsHttpConnectionInfo* Http2Session::ConnectionInfo() {
  RefPtr<nsHttpConnectionInfo> ci;
  GetConnectionInfo(getter_AddRefs(ci));
  return ci.get();
}

void Http2Session::CloseTransaction(nsAHttpTransaction* aTransaction,
                                    nsresult aResult) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http2Session::CloseTransaction %p %p %" PRIx32, this, aTransaction,
        static_cast<uint32_t>(aResult)));

  // Generally this arrives as a cancel event from the connection manager.

  // need to find the stream and call CleanupStream() on it.
  RefPtr<Http2StreamBase> stream = mStreamTransactionHash.Get(aTransaction);
  if (!stream) {
    LOG3(("Http2Session::CloseTransaction %p %p %" PRIx32 " - not found.", this,
          aTransaction, static_cast<uint32_t>(aResult)));
    return;
  }
  LOG3(
      ("Http2Session::CloseTransaction probably a cancel. "
       "this=%p, trans=%p, result=%" PRIx32 ", streamID=0x%X stream=%p",
       this, aTransaction, static_cast<uint32_t>(aResult), stream->StreamID(),
       stream.get()));
  CleanupStream(stream, aResult, CANCEL_ERROR);
  nsresult rv = ResumeRecv();
  if (NS_FAILED(rv)) {
    LOG3(("Http2Session::CloseTransaction %p %p %x ResumeRecv returned %x",
          this, aTransaction, static_cast<uint32_t>(aResult),
          static_cast<uint32_t>(rv)));
  }
}

//-----------------------------------------------------------------------------
// nsAHttpSegmentReader
//-----------------------------------------------------------------------------

nsresult Http2Session::OnReadSegment(const char* buf, uint32_t count,
                                     uint32_t* countRead) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  nsresult rv;

  // If we can release old queued data then we can try and write the new
  // data directly to the network without using the output queue at all
  if (mOutputQueueUsed) FlushOutputQueue();

  if (!mOutputQueueUsed && mSegmentReader) {
    // try and write directly without output queue
    rv = mSegmentReader->OnReadSegment(buf, count, countRead);

    if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
      *countRead = 0;
    } else if (NS_FAILED(rv)) {
      return rv;
    }

    if (*countRead < count) {
      uint32_t required = count - *countRead;
      // assuming a commitment() happened, this ensurebuffer is a nop
      // but just in case the queuesize is too small for the required data
      // call ensurebuffer().
      EnsureBuffer(mOutputQueueBuffer, required, 0, mOutputQueueSize);
      memcpy(mOutputQueueBuffer.get(), buf + *countRead, required);
      mOutputQueueUsed = required;
    }

    *countRead = count;
    return NS_OK;
  }

  // At this point we are going to buffer the new data in the output
  // queue if it fits. By coalescing multiple small submissions into one larger
  // buffer we can get larger writes out to the network later on.

  // This routine should not be allowed to fill up the output queue
  // all on its own - at least kQueueReserved bytes are always left
  // for other routines to use - but this is an all-or-nothing function,
  // so if it will not all fit just return WOULD_BLOCK

  if ((mOutputQueueUsed + count) > (mOutputQueueSize - kQueueReserved)) {
    return NS_BASE_STREAM_WOULD_BLOCK;
  }

  memcpy(mOutputQueueBuffer.get() + mOutputQueueUsed, buf, count);
  mOutputQueueUsed += count;
  *countRead = count;

  FlushOutputQueue();

  return NS_OK;
}

nsresult Http2Session::CommitToSegmentSize(uint32_t count,
                                           bool forceCommitment) {
  if (mOutputQueueUsed && !mAttemptingEarlyData) FlushOutputQueue();

  // would there be enough room to buffer this if needed?
  if ((mOutputQueueUsed + count) <= (mOutputQueueSize - kQueueReserved)) {
    return NS_OK;
  }

  // if we are using part of our buffers already, try again later unless
  // forceCommitment is set.
  if (mOutputQueueUsed && !forceCommitment) return NS_BASE_STREAM_WOULD_BLOCK;

  if (mOutputQueueUsed) {
    // normally we avoid the memmove of RealignOutputQueue, but we'll try
    // it if forceCommitment is set before growing the buffer.
    RealignOutputQueue();

    // is there enough room now?
    if ((mOutputQueueUsed + count) <= (mOutputQueueSize - kQueueReserved)) {
      return NS_OK;
    }
  }

  // resize the buffers as needed
  EnsureOutputBuffer(count + kQueueReserved);

  MOZ_ASSERT((mOutputQueueUsed + count) <= (mOutputQueueSize - kQueueReserved),
             "buffer not as large as expected");

  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsAHttpSegmentWriter
//-----------------------------------------------------------------------------

nsresult Http2Session::OnWriteSegment(char* buf, uint32_t count,
                                      uint32_t* countWritten) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  nsresult rv;

  if (!mSegmentWriter) {
    // the only way this could happen would be if Close() were called on the
    // stack with WriteSegments()
    return NS_ERROR_FAILURE;
  }

  if (mDownstreamState == NOT_USING_NETWORK ||
      mDownstreamState == BUFFERING_FRAME_HEADER ||
      mDownstreamState == DISCARDING_DATA_FRAME_PADDING) {
    return NS_BASE_STREAM_WOULD_BLOCK;
  }

  if (mDownstreamState == PROCESSING_DATA_FRAME) {
    if (mInputFrameFinal && mInputFrameDataRead == mInputFrameDataSize) {
      *countWritten = 0;
      SetNeedsCleanup();
      return NS_BASE_STREAM_CLOSED;
    }

    count = std::min(count, mInputFrameDataSize - mInputFrameDataRead);
    rv = NetworkRead(mSegmentWriter, buf, count, countWritten);
    if (NS_FAILED(rv)) return rv;

    LogIO(this, mInputFrameDataStream, "Reading Data Frame", buf,
          *countWritten);

    mInputFrameDataRead += *countWritten;
    if (mPaddingLength &&
        (mInputFrameDataSize - mInputFrameDataRead <= mPaddingLength)) {
      // We are crossing from real HTTP data into the realm of padding. If
      // we've actually crossed the line, we need to munge countWritten for the
      // sake of goodness and sanity. No matter what, any future calls to
      // WriteSegments need to just discard data until we reach the end of this
      // frame.
      if (mInputFrameDataSize != mInputFrameDataRead) {
        // Only change state if we still have padding to read. If we don't do
        // this, we can end up hanging on frames that combine real data,
        // padding, and END_STREAM (see bug 1019921)
        ChangeDownstreamState(DISCARDING_DATA_FRAME_PADDING);
      }
      uint32_t paddingRead =
          mPaddingLength - (mInputFrameDataSize - mInputFrameDataRead);
      LOG3(
          ("Http2Session::OnWriteSegment %p stream 0x%X len=%d read=%d "
           "crossed from HTTP data into padding (%d of %d) countWritten=%d",
           this, mInputFrameID, mInputFrameDataSize, mInputFrameDataRead,
           paddingRead, mPaddingLength, *countWritten));
      *countWritten -= paddingRead;
      LOG3(("Http2Session::OnWriteSegment %p stream 0x%X new countWritten=%d",
            this, mInputFrameID, *countWritten));
    }

    mInputFrameDataStream->UpdateTransportReadEvents(*countWritten);
    if ((mInputFrameDataRead == mInputFrameDataSize) && !mInputFrameFinal) {
      ResetDownstreamState();
    }

    return rv;
  }

  if (mDownstreamState == PROCESSING_COMPLETE_HEADERS) {
    if (mFlatHTTPResponseHeaders.Length() == mFlatHTTPResponseHeadersOut &&
        mInputFrameFinal) {
      *countWritten = 0;
      SetNeedsCleanup();
      return NS_BASE_STREAM_CLOSED;
    }

    count = std::min<uint32_t>(
        count, mFlatHTTPResponseHeaders.Length() - mFlatHTTPResponseHeadersOut);
    memcpy(buf, mFlatHTTPResponseHeaders.get() + mFlatHTTPResponseHeadersOut,
           count);
    mFlatHTTPResponseHeadersOut += count;
    *countWritten = count;

    if (mFlatHTTPResponseHeaders.Length() == mFlatHTTPResponseHeadersOut) {
      // Since mInputFrameFinal can be reset, we need to also check RecvdFin to
      // see if a stream doesn’t expect more frames.
      if (!mInputFrameFinal && !mInputFrameDataStream->RecvdFin()) {
        // If more frames are expected in this stream, then reset the state so
        // they can be handled. Otherwise (e.g. a 0 length response with the fin
        // on the incoming headers) stay in PROCESSING_COMPLETE_HEADERS state so
        // the SetNeedsCleanup() code above can cleanup the stream.
        ResetDownstreamState();
      }
    }

    return NS_OK;
  }

  MOZ_ASSERT(false);
  return NS_ERROR_UNEXPECTED;
}

void Http2Session::SetNeedsCleanup() {
  LOG3(
      ("Http2Session::SetNeedsCleanup %p - recorded downstream fin of "
       "stream %p 0x%X",
       this, mInputFrameDataStream.get(), mInputFrameDataStream->StreamID()));

  // This will result in Close() being called
  MOZ_ASSERT(!mNeedsCleanup, "mNeedsCleanup unexpectedly set");
  mInputFrameDataStream->SetResponseIsComplete();
  mNeedsCleanup = mInputFrameDataStream;
  ResetDownstreamState();
}

void Http2Session::ConnectSlowConsumer(Http2StreamBase* stream) {
  LOG3(("Http2Session::ConnectSlowConsumer %p 0x%X\n", this,
        stream->StreamID()));
  mQueueManager.AddStreamToQueue(
      Http2StreamQueueType::SlowConsumersReadyForRead, stream);
  (void)ForceRecv();
}

nsresult Http2Session::BufferOutput(const char* buf, uint32_t count,
                                    uint32_t* countRead) {
  RefPtr<nsAHttpSegmentReader> old;
  mSegmentReader.swap(old);  // Make mSegmentReader null
  nsresult rv = OnReadSegment(buf, count, countRead);
  mSegmentReader.swap(old);  // Restore the old mSegmentReader
  return rv;
}

bool  // static
Http2Session::ALPNCallback(nsITLSSocketControl* tlsSocketControl) {
  LOG3(("Http2Session::ALPNCallback sslsocketcontrol=%p\n", tlsSocketControl));
  if (tlsSocketControl) {
    int16_t version = tlsSocketControl->GetSSLVersionOffered();
    LOG3(("Http2Session::ALPNCallback version=%x\n", version));

    if (version == nsITLSSocketControl::TLS_VERSION_1_2 &&
        !gHttpHandler->IsH2MandatorySuiteEnabled()) {
      LOG3(("Http2Session::ALPNCallback Mandatory Cipher Suite Unavailable\n"));
      return false;
    }

    if (version >= nsITLSSocketControl::TLS_VERSION_1_2) {
      return true;
    }
  }
  return false;
}

nsresult Http2Session::ConfirmTLSProfile() {
  if (mTLSProfileConfirmed) {
    return NS_OK;
  }

  LOG3(("Http2Session::ConfirmTLSProfile %p mConnection=%p\n", this,
        mConnection.get()));

  if (mAttemptingEarlyData) {
    LOG3(
        ("Http2Session::ConfirmTLSProfile %p temporarily passing due to early "
         "data\n",
         this));
    return NS_OK;
  }

  if (!StaticPrefs::network_http_http2_enforce_tls_profile()) {
    LOG3(
        ("Http2Session::ConfirmTLSProfile %p passed due to configuration "
         "bypass\n",
         this));
    mTLSProfileConfirmed = true;
    return NS_OK;
  }

  if (!mConnection) return NS_ERROR_FAILURE;

  nsCOMPtr<nsITLSSocketControl> ssl;
  mConnection->GetTLSSocketControl(getter_AddRefs(ssl));
  LOG3(("Http2Session::ConfirmTLSProfile %p sslsocketcontrol=%p\n", this,
        ssl.get()));
  if (!ssl) return NS_ERROR_FAILURE;

  int16_t version = ssl->GetSSLVersionUsed();
  LOG3(("Http2Session::ConfirmTLSProfile %p version=%x\n", this, version));
  if (version < nsITLSSocketControl::TLS_VERSION_1_2) {
    LOG3(("Http2Session::ConfirmTLSProfile %p FAILED due to lack of TLS1.2\n",
          this));
    return SessionError(INADEQUATE_SECURITY);
  }

  uint16_t kea = ssl->GetKEAUsed();
  if (kea == ssl_kea_ecdh_hybrid && !StaticPrefs::security_tls_enable_kyber()) {
    LOG3(("Http2Session::ConfirmTLSProfile %p FAILED due to disabled KEA %d\n",
          this, kea));
    return SessionError(INADEQUATE_SECURITY);
  }

  if (kea != ssl_kea_dh && kea != ssl_kea_ecdh && kea != ssl_kea_ecdh_hybrid) {
    LOG3(("Http2Session::ConfirmTLSProfile %p FAILED due to invalid KEA %d\n",
          this, kea));
    return SessionError(INADEQUATE_SECURITY);
  }

  uint32_t keybits = ssl->GetKEAKeyBits();
  if (kea == ssl_kea_dh && keybits < 2048) {
    LOG3(("Http2Session::ConfirmTLSProfile %p FAILED due to DH %d < 2048\n",
          this, keybits));
    return SessionError(INADEQUATE_SECURITY);
  }
  if (kea == ssl_kea_ecdh && keybits < 224) {  // see rfc7540 9.2.1.
    LOG3(("Http2Session::ConfirmTLSProfile %p FAILED due to ECDH %d < 224\n",
          this, keybits));
    return SessionError(INADEQUATE_SECURITY);
  }

  int16_t macAlgorithm = ssl->GetMACAlgorithmUsed();
  LOG3(("Http2Session::ConfirmTLSProfile %p MAC Algortihm (aead==6) %d\n", this,
        macAlgorithm));
  if (macAlgorithm != nsITLSSocketControl::SSL_MAC_AEAD) {
    LOG3(("Http2Session::ConfirmTLSProfile %p FAILED due to lack of AEAD\n",
          this));
    return SessionError(INADEQUATE_SECURITY);
  }

  /* We are required to send SNI. We do that already, so no check is done
   * here to make sure we did. */

  /* We really should check to ensure TLS compression isn't enabled on
   * this connection. However, we never enable TLS compression on our end,
   * anyway, so it'll never be on. All the same, see https://bugzil.la/965881
   * for the possibility for an interface to ensure it never gets turned on. */

  mTLSProfileConfirmed = true;
  return NS_OK;
}

//-----------------------------------------------------------------------------
// Modified methods of nsAHttpConnection
//-----------------------------------------------------------------------------

void Http2Session::TransactionHasDataToWrite(nsAHttpTransaction* caller) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http2Session::TransactionHasDataToWrite %p trans=%p", this, caller));

  // a trapped signal from the http transaction to the connection that
  // it is no longer blocked on read.

  RefPtr<Http2StreamBase> stream = mStreamTransactionHash.Get(caller);
  if (!stream || !VerifyStream(stream)) {
    LOG3(("Http2Session::TransactionHasDataToWrite %p caller %p not found",
          this, caller));
    return;
  }

  LOG3(("Http2Session::TransactionHasDataToWrite %p ID is 0x%X\n", this,
        stream->StreamID()));

  if (!mClosed) {
    mQueueManager.AddStreamToQueue(Http2StreamQueueType::ReadyForWrite, stream);
    SetWriteCallbacks();
  } else {
    LOG3(
        ("Http2Session::TransactionHasDataToWrite %p closed so not setting "
         "Ready4Write\n",
         this));
  }

  // NSPR poll will not poll the network if there are non system PR_FileDesc's
  // that are ready - so we can get into a deadlock waiting for the system IO
  // to come back here if we don't force the send loop manually.
  (void)ForceSend();
}

void Http2Session::TransactionHasDataToRecv(nsAHttpTransaction* caller) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http2Session::TransactionHasDataToRecv %p trans=%p", this, caller));

  // a signal from the http transaction to the connection that it will consume
  // more
  RefPtr<Http2StreamBase> stream = mStreamTransactionHash.Get(caller);
  if (!stream || !VerifyStream(stream)) {
    LOG3(("Http2Session::TransactionHasDataToRecv %p caller %p not found", this,
          caller));
    return;
  }

  LOG3(("Http2Session::TransactionHasDataToRecv %p ID is 0x%X\n", this,
        stream->StreamID()));
  TransactionHasDataToRecv(stream);
}

void Http2Session::TransactionHasDataToWrite(Http2StreamBase* stream) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG3(("Http2Session::TransactionHasDataToWrite %p stream=%p ID=0x%x", this,
        stream, stream->StreamID()));

  mQueueManager.AddStreamToQueue(Http2StreamQueueType::ReadyForWrite, stream);
  SetWriteCallbacks();
  (void)ForceSend();
}

void Http2Session::TransactionHasDataToRecv(Http2StreamBase* caller) {
  ConnectSlowConsumer(caller);
}

bool Http2Session::IsPersistent() { return true; }

nsresult Http2Session::TakeTransport(nsISocketTransport**,
                                     nsIAsyncInputStream**,
                                     nsIAsyncOutputStream**) {
  MOZ_ASSERT(false, "TakeTransport of Http2Session");
  return NS_ERROR_UNEXPECTED;
}

WebTransportSessionBase* Http2Session::GetWebTransportSession(
    nsAHttpTransaction* aTransaction) {
  uintptr_t id = reinterpret_cast<uintptr_t>(aTransaction);
  RefPtr<Http2StreamBase> stream;
  for (auto& entry : mTunnelStreams) {
    if (entry->GetTransactionId() == id) {
      entry->SetTransactionId(0);
      stream = entry;
      break;
    }
  }

  if (!stream || !stream->GetHttp2WebTransportSession()) {
    MOZ_ASSERT(false, "There must be a stream");
    return nullptr;
  }
  RemoveStreamFromQueues(stream);

  return static_cast<Http2WebTransportSession*>(
             stream->GetHttp2WebTransportSession())
      ->GetHttp2WebTransportSessionImpl();
}

already_AddRefed<HttpConnectionBase> Http2Session::TakeHttpConnection() {
  LOG(("Http2Session::TakeHttpConnection %p", this));
  return nullptr;
}

already_AddRefed<HttpConnectionBase> Http2Session::HttpConnection() {
  if (mConnection) {
    return mConnection->HttpConnection();
  }
  return nullptr;
}

void Http2Session::GetSecurityCallbacks(nsIInterfaceRequestor** aOut) {
  *aOut = nullptr;
}

void Http2Session::SetConnection(nsAHttpConnection* aConn) {
  mConnection = aConn;
}

//-----------------------------------------------------------------------------
// unused methods of nsAHttpTransaction
// We can be sure of this because Http2Session is only constructed in
// nsHttpConnection and is never passed out of that object or a
// TLSFilterTransaction TLS tunnel
//-----------------------------------------------------------------------------

void Http2Session::SetProxyConnectFailed() {
  MOZ_ASSERT(false, "Http2Session::SetProxyConnectFailed()");
}

bool Http2Session::IsDone() {
  return !mStreamTransactionHash.Count() && mTunnelStreams.IsEmpty();
}

nsresult Http2Session::Status() {
  MOZ_ASSERT(false, "Http2Session::Status()");
  return NS_ERROR_UNEXPECTED;
}

uint32_t Http2Session::Caps() {
  MOZ_ASSERT(false, "Http2Session::Caps()");
  return 0;
}

nsHttpRequestHead* Http2Session::RequestHead() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(false,
             "Http2Session::RequestHead() "
             "should not be called after http/2 is setup");
  return nullptr;
}

uint32_t Http2Session::Http1xTransactionCount() { return 0; }

nsresult Http2Session::TakeSubTransactions(
    nsTArray<RefPtr<nsAHttpTransaction>>& outTransactions) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  // Generally this cannot be done with http/2 as transactions are
  // started right away.

  LOG3(("Http2Session::TakeSubTransactions %p\n", this));

  if (mConcurrentHighWater > 0) return NS_ERROR_ALREADY_OPENED;

  LOG3(("   taking %d\n", mStreamTransactionHash.Count()));

  for (auto iter = mStreamTransactionHash.Iter(); !iter.Done(); iter.Next()) {
    outTransactions.AppendElement(iter.Key());

    // Removing the stream from the hash will delete the stream and drop the
    // transaction reference the hash held.
    iter.Remove();
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------
// Pass through methods of nsAHttpConnection
//-----------------------------------------------------------------------------

nsAHttpConnection* Http2Session::Connection() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  return mConnection;
}

nsresult Http2Session::OnHeadersAvailable(nsAHttpTransaction* transaction,
                                          nsHttpRequestHead* requestHead,
                                          nsHttpResponseHead* responseHead,
                                          bool* reset) {
  return NS_OK;
}

bool Http2Session::IsReused() {
  if (!mConnection) {
    return false;
  }

  return mConnection->IsReused();
}

nsresult Http2Session::PushBack(const char* buf, uint32_t len) {
  return mConnection->PushBack(buf, len);
}

void Http2Session::SendPing() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("Http2Session::SendPing %p mPreviousUsed=%d", this, mPreviousUsed));

  if (mPreviousUsed) {
    // alredy in progress, get out
    return;
  }

  mPingSentEpoch = PR_IntervalNow();
  if (!mPingSentEpoch) {
    mPingSentEpoch = 1;  // avoid the 0 sentinel value
  }
  if (!mPingThreshold ||
      (mPingThreshold > gHttpHandler->NetworkChangedTimeout())) {
    mPreviousPingThreshold = mPingThreshold;
    mPreviousUsed = true;
    mPingThreshold = gHttpHandler->NetworkChangedTimeout();
    // Reset mLastReadEpoch, so we can really check when do we got pong from the
    // server.
    mLastReadEpoch = 0;
  }
  GeneratePing(false);
  (void)ResumeRecv();
}

bool Http2Session::TestOriginFrame(const nsACString& hostname, int32_t port) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(mOriginFrameActivated);

  nsAutoCString key(hostname);
  key.Append(':');
  key.AppendInt(port);
  bool rv = mOriginFrame.Get(key);
  LOG3(("TestOriginFrame() hash.get %p %s %d\n", this, key.get(), rv));
  if (!rv && ConnectionInfo()) {
    // the SNI is also implicitly in this list, so consult that too
    nsHttpConnectionInfo* ci = ConnectionInfo();
    rv = nsCString(hostname).EqualsIgnoreCase(ci->Origin()) &&
         (port == ci->OriginPort());
    LOG3(("TestOriginFrame() %p sni test %d\n", this, rv));
  }
  return rv;
}

bool Http2Session::TestJoinConnection(const nsACString& hostname,
                                      int32_t port) {
  return RealJoinConnection(hostname, port, true);
}

bool Http2Session::JoinConnection(const nsACString& hostname, int32_t port) {
  return RealJoinConnection(hostname, port, false);
}

bool Http2Session::RealJoinConnection(const nsACString& hostname, int32_t port,
                                      bool justKidding) {
  if (!mConnection || mClosed || mShouldGoAway) {
    return false;
  }

  nsHttpConnectionInfo* ci = ConnectionInfo();
  if (nsCString(hostname).EqualsIgnoreCase(ci->Origin()) &&
      (port == ci->OriginPort())) {
    return true;
  }

  if (!mReceivedSettings) {
    return false;
  }

  if (mOriginFrameActivated) {
    bool originFrameResult = TestOriginFrame(hostname, port);
    if (!originFrameResult) {
      return false;
    }
  } else {
    LOG3(("JoinConnection %p no origin frame check used.\n", this));
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

  // try all the coalescable versions we support.
  const SpdyInformation* info = gHttpHandler->SpdyInfo();
  bool joinedReturn = false;
  if (StaticPrefs::network_http_http2_enabled()) {
    if (justKidding) {
      rv = sslSocketControl->TestJoinConnection(info->VersionString, hostname,
                                                port, &isJoined);
    } else {
      rv = sslSocketControl->JoinConnection(info->VersionString, hostname, port,
                                            &isJoined);
    }
    if (NS_SUCCEEDED(rv) && isJoined) {
      joinedReturn = true;
    }
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

void Http2Session::CurrentBrowserIdChanged(uint64_t id) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  mCurrentBrowserId = id;

  for (const auto& stream : mStreamTransactionHash.Values()) {
    stream->CurrentBrowserIdChanged(id);
  }
}

void Http2Session::SetCleanShutdown(bool aCleanShutdown) {
  mCleanShutdown = aCleanShutdown;
}

ExtendedCONNECTSupport Http2Session::GetExtendedCONNECTSupport() {
  LOG3(
      ("Http2Session::GetExtendedCONNECTSupport %p enable=%d peer allow=%d "
       "receved setting=%d",
       this, mEnableWebsockets, mPeerAllowsExtendedCONNECT, mReceivedSettings));

  if (!mEnableWebsockets || mClosed) {
    return ExtendedCONNECTSupport::NO_SUPPORT;
  }

  if (mPeerAllowsExtendedCONNECT) {
    return ExtendedCONNECTSupport::SUPPORTED;
  }

  if (!mReceivedSettings) {
    mHasTransactionWaitingForExtendedCONNECT = true;
    return ExtendedCONNECTSupport::UNSURE;
  }

  return ExtendedCONNECTSupport::NO_SUPPORT;
}

PRIntervalTime Http2Session::LastWriteTime() {
  return mConnection->LastWriteTime();
}

}  // namespace net
}  // namespace mozilla
