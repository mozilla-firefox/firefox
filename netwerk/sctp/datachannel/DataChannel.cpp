/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>

#include <algorithm>

#ifdef XP_WIN
#  include <winsock.h>  // for htonl, htons, ntohl, ntohs
#endif

#include "mozilla/Components.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/dom/RTCDataChannel.h"
#include "mozilla/dom/RTCDataChannelBinding.h"
#include "nsIInputStream.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsNetUtil.h"
#include "nsProxyRelease.h"
#include "nsThread.h"
#include "nsThreadUtils.h"
#ifdef MOZ_PEERCONNECTION
#  include "jsapi/MediaTransportHandler.h"
#  include "mediapacket.h"
#  include "transport/runnable_utils.h"
#endif

#include "DataChannel.h"
#include "DataChannelDcSctp.h"
#include "DataChannelLog.h"
#include "DataChannelProtocol.h"
#include "DataChannelUsrsctp.h"

namespace mozilla {

LazyLogModule gDataChannelLog("DataChannel");

OutgoingMsg::OutgoingMsg(nsACString&& aData,
                         const DataChannelMessageMetadata& aMetadata)
    : mData(std::move(aData)), mMetadata(aMetadata) {}

void OutgoingMsg::Advance(size_t offset) {
  mPos += offset;
  if (mPos > mData.Length()) {
    mPos = mData.Length();
  }
}

DataChannelConnection::~DataChannelConnection() {
  DC_INFO(("%p: Deleting DataChannelConnection", this));
  // This may die on the MainThread, or on the STS thread, or on an
  // sctp thread if we were in a callback when the DOM side shut things down.
  MOZ_ASSERT(mState == DataChannelConnectionState::Closed);
  MOZ_ASSERT(mPending.empty());

  if (!mSTS->IsOnCurrentThread()) {
    // We may be on MainThread *or* on an sctp thread (being called from
    // receive_cb() or SendSctpPacket())
    if (mInternalIOThread) {
      // Avoid spinning the event thread from here (which if we're mainthread
      // is in the event loop already)
      nsCOMPtr<nsIRunnable> r = WrapRunnable(
          nsCOMPtr<nsIThread>(mInternalIOThread), &nsIThread::AsyncShutdown);
      mSTS->Dispatch(r.forget(), NS_DISPATCH_FALLIBLE);
    }
  } else {
    // on STS, safe to call shutdown
    if (mInternalIOThread) {
      mInternalIOThread->Shutdown();
    }
  }
}

void DataChannelConnection::Destroy() {
  MOZ_ASSERT(NS_IsMainThread());
  DC_INFO(("%p: Destroying DataChannelConnection", this));
  CloseAll();
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  MOZ_DIAGNOSTIC_ASSERT(mSTS);
#endif
  mListener = nullptr;
  mSTS->Dispatch(
      NS_NewCancelableRunnableFunction(
          __func__,
          [this, self = RefPtr<DataChannelConnection>(this)]() {
            mPacketReceivedListener.DisconnectIfExists();
            mStateChangeListener.DisconnectIfExists();
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
            mShutdown = true;
            DC_INFO(("Shutting down connection %p, id %p", this, (void*)mId));
#endif
          }),
      NS_DISPATCH_FALLIBLE);
}

Maybe<RefPtr<DataChannelConnection>> DataChannelConnection::Create(
    DataChannelConnection::DataConnectionListener* aListener,
    nsISerialEventTarget* aTarget, MediaTransportHandler* aHandler,
    const uint16_t aLocalPort, const uint16_t aNumStreams) {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<DataChannelConnection> connection;
  if (Preferences::GetBool("media.peerconnection.sctp.use_dcsctp", false)) {
    connection = new DataChannelConnectionDcSctp(aListener, aTarget,
                                                 aHandler);  // Walks into a bar
  } else {
    connection = new DataChannelConnectionUsrsctp(
        aListener, aTarget, aHandler);  // Walks into a bar
  }
  return connection->Init(aLocalPort, aNumStreams) ? Some(connection)
                                                   : Nothing();
}

DataChannelConnection::DataChannelConnection(
    DataChannelConnection::DataConnectionListener* aListener,
    nsISerialEventTarget* aTarget, MediaTransportHandler* aHandler)
    : NeckoTargetHolder(aTarget),
      mListener(aListener),
      mTransportHandler(aHandler) {
  MOZ_ASSERT(NS_IsMainThread());
  DC_VERBOSE(
      ("%p: DataChannelConnection c'tor, listener=%p", this, mListener.get()));

  // XXX FIX! make this a global we get once
  // Find the STS thread
  nsresult rv;
  mSTS = mozilla::components::SocketTransport::Service(&rv);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  mShutdown = false;
#endif
}

// Only called on MainThread, mMaxMessageSize is read on other threads
void DataChannelConnection::SetMaxMessageSize(uint64_t aMaxMessageSize) {
  MOZ_ASSERT(NS_IsMainThread());

  mMaxMessageSize = aMaxMessageSize;

  nsresult rv;
  nsCOMPtr<nsIPrefService> prefs;
  prefs = mozilla::components::Preferences::Service(&rv);
  if (!NS_WARN_IF(NS_FAILED(rv))) {
    nsCOMPtr<nsIPrefBranch> branch = do_QueryInterface(prefs);

    if (branch) {
      int32_t temp;
      if (!NS_FAILED(branch->GetIntPref(
              "media.peerconnection.sctp.force_maximum_message_size", &temp))) {
        if (temp > 0 && (uint64_t)temp < mMaxMessageSize) {
          mMaxMessageSize = (uint64_t)temp;
        }
      }
    }
  }

  // Fix remote MMS. This code exists, so future implementations of
  // RTCSctpTransport.maxMessageSize can simply provide that value from
  // GetMaxMessageSize.

  // TODO: Bug 1382779, once resolved, can be increased to
  // min(Uint8ArrayMaxSize, UINT32_MAX)
  // TODO: Bug 1381146, once resolved, can be increased to whatever we support
  // then (hopefully
  //       SIZE_MAX)
  if (mMaxMessageSize == 0 ||
      mMaxMessageSize > WEBRTC_DATACHANNEL_MAX_MESSAGE_SIZE_REMOTE) {
    mMaxMessageSize = WEBRTC_DATACHANNEL_MAX_MESSAGE_SIZE_REMOTE;
  }

  DC_DEBUG(("%p: Maximum message size (outgoing data): %" PRIu64
            " (enforced=%s)",
            this, mMaxMessageSize,
            aMaxMessageSize != mMaxMessageSize ? "yes" : "no"));

  for (auto& channel : mChannels.GetAll()) {
    channel->SetMaxMessageSize(GetMaxMessageSize());
  }
}

double DataChannelConnection::GetMaxMessageSize() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mMaxMessageSize) {
    return static_cast<double>(mMaxMessageSize);
  }

  return std::numeric_limits<double>::infinity();
}

RefPtr<DataChannelConnection::StatsPromise> DataChannelConnection::GetStats(
    const DOMHighResTimeStamp aTimestamp) const {
  MOZ_ASSERT(NS_IsMainThread());
  nsTArray<RefPtr<DataChannelStatsPromise>> statsPromises;
  for (const RefPtr<DataChannel>& chan : mChannels.GetAll()) {
    if (chan) {
      RefPtr<DataChannelStatsPromise> statsPromise(chan->GetStats(aTimestamp));
      if (statsPromise) {
        statsPromises.AppendElement(std::move(statsPromise));
      }
    }
  }

  return DataChannelStatsPromise::All(GetMainThreadSerialEventTarget(),
                                      statsPromises);
}

RefPtr<DataChannelStatsPromise> DataChannel::GetStats(
    const DOMHighResTimeStamp aTimestamp) {
  MOZ_ASSERT(NS_IsMainThread());

  return InvokeAsync(mDomEventTarget, __func__,
                     [this, self = RefPtr<DataChannel>(this), aTimestamp] {
                       if (!GetDomDataChannel()) {
                         // Empty stats object, I guess... too late to
                         // return a nullptr and rejecting will trash stats
                         // promises for all other datachannels.
                         return DataChannelStatsPromise::CreateAndResolve(
                             dom::RTCDataChannelStats(), __func__);
                       }
                       return DataChannelStatsPromise::CreateAndResolve(
                           GetDomDataChannel()->GetStats(aTimestamp), __func__);
                     });
}

bool DataChannelConnection::ConnectToTransport(const std::string& aTransportId,
                                               const bool aClient,
                                               const uint16_t aLocalPort,
                                               const uint16_t aRemotePort) {
  MOZ_ASSERT(NS_IsMainThread());

  static const auto paramString =
      [](const std::string& tId, const Maybe<bool>& client,
         const uint16_t localPort, const uint16_t remotePort) -> std::string {
    std::ostringstream stream;
    stream << "Transport ID: '" << tId << "', Role: '"
           << (client ? (client.value() ? "client" : "server") : "")
           << "', Local Port: '" << localPort << "', Remote Port: '"
           << remotePort << "'";
    return stream.str();
  };

  const auto params =
      paramString(aTransportId, Some(aClient), aLocalPort, aRemotePort);
  DC_INFO(
      ("%p: ConnectToTransport connecting DTLS transport with parameters: %s",
       this, params.c_str()));

  DC_INFO(("%p: New transport parameters: %s", this, params.c_str()));
  if (NS_WARN_IF(aTransportId.empty())) {
    return false;
  }

  if (!mAllocateEven.isSome()) {
    // Do this stuff once.
    mLocalPort = aLocalPort;
    mRemotePort = aRemotePort;
    mAllocateEven = Some(aClient);
    nsTArray<RefPtr<DataChannel>> hasStreamId;
    // Could be faster. Probably doesn't matter.
    while (auto channel = mChannels.Get(INVALID_STREAM)) {
      mChannels.Remove(channel);
      auto id = FindFreeStream();
      if (id != INVALID_STREAM) {
        channel->SetStream(id);
        mChannels.Insert(channel);
        DC_DEBUG(("%p: Inserting auto-selected id %u for channel %p", this,
                  static_cast<unsigned>(id), channel.get()));
        mStreamIds.InsertElementSorted(id);
        hasStreamId.AppendElement(std::move(channel));
      } else {
        DC_WARN(("%p: Could not find id for channel %p, calling AnnounceClosed",
                 this, channel.get()));
        // Spec language is very similar to AnnounceClosed, the differences
        // being a lack of a closed check at the top, a different error event,
        // and no removal of the channel from the [[DataChannels]] slot.
        // We don't support firing errors right now, and we probabaly want the
        // closed check anyway, and we don't really have something equivalent
        // to the [[DataChannels]] slot, so just use AnnounceClosed for now.
        channel->AnnounceClosed(Nothing());
      }
    }

    mSTS->Dispatch(NS_NewCancelableRunnableFunction(
                       __func__,
                       [this, self = RefPtr<DataChannelConnection>(this),
                        hasStreamId = std::move(hasStreamId)]() {
                         SetState(DataChannelConnectionState::Connecting);
                         for (auto& channel : hasStreamId) {
                           OpenFinish(std::move(channel));
                         }
                       }),
                   NS_DISPATCH_FALLIBLE);
  }

  // We do not check whether this is a new transport id here, that happens on
  // STS.
  RUN_ON_THREAD(mSTS,
                WrapRunnable(RefPtr<DataChannelConnection>(this),
                             &DataChannelConnection::SetSignals, aTransportId),
                NS_DISPATCH_NORMAL);
  return true;
}

void DataChannelConnection::SetSignals(const std::string& aTransportId) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  if (mTransportId == aTransportId) {
    // Nothing to do!
    return;
  }

  mTransportId = aTransportId;

  if (!mConnectedToTransportHandler) {
    mPacketReceivedListener =
        mTransportHandler->GetSctpPacketReceived().Connect(
            mSTS, this, &DataChannelConnection::OnPacketReceived);
    mStateChangeListener = mTransportHandler->GetStateChange().Connect(
        mSTS, this, &DataChannelConnection::TransportStateChange);
    mConnectedToTransportHandler = true;
  }
  // SignalStateChange() doesn't call you with the initial state
  if (mTransportHandler->GetState(mTransportId, false) ==
      TransportLayer::TS_OPEN) {
    DC_DEBUG(("%p: Setting transport signals, dtls already open", this));
    OnTransportReady();
  } else {
    DC_DEBUG(("%p: Setting transport signals, dtls not open yet", this));
  }
}

void DataChannelConnection::TransportStateChange(
    const std::string& aTransportId, TransportLayer::State aState,
    const nsTArray<nsTArray<uint8_t>>&, Maybe<dom::RTCErrorParams>) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  if (aTransportId == mTransportId) {
    if (aState == TransportLayer::TS_OPEN) {
      DC_DEBUG(("%p: Transport is open!", this));
      OnTransportReady();
    } else if (aState == TransportLayer::TS_CLOSED ||
               aState == TransportLayer::TS_NONE ||
               aState == TransportLayer::TS_ERROR) {
      DC_DEBUG(("%p: Transport is closed!", this));
      CloseAll_s();
    }
  }
}

// Process any pending Opens
void DataChannelConnection::ProcessQueuedOpens() {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  std::set<RefPtr<DataChannel>> temp(std::move(mPending));
  // Technically in an unspecified state, although no reasonable impl will leave
  // anything in here.
  mPending.clear();
  for (const auto& channel : temp) {
    DC_DEBUG(("%p: Processing queued open for %p (%u)", this, channel.get(),
              channel->mStream));
    OpenFinish(channel);  // may end up back in mPending
  }
}

void DataChannelConnection::OnPacketReceived(const std::string& aTransportId,
                                             const MediaPacket& packet) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  if (packet.type() == MediaPacket::SCTP && mTransportId == aTransportId) {
    OnSctpPacketReceived(packet);
  }
}

void DataChannelConnection::SendPacket(std::unique_ptr<MediaPacket>&& packet) {
  mSTS->Dispatch(NS_NewCancelableRunnableFunction(
                     "DataChannelConnection::SendPacket",
                     [this, self = RefPtr<DataChannelConnection>(this),
                      packet = std::move(packet)]() mutable {
                       // DC_DEBUG(("%p: SCTP/DTLS sent %ld bytes",
                       // this, len));
                       if (!mTransportId.empty() && mTransportHandler) {
                         mTransportHandler->SendPacket(mTransportId,
                                                       std::move(*packet));
                       }
                     }),
                 NS_DISPATCH_FALLIBLE);
}

already_AddRefed<DataChannel> DataChannelConnection::FindChannelByStream(
    uint16_t stream) {
  return mChannels.Get(stream).forget();
}

uint16_t DataChannelConnection::FindFreeStream() const {
  MOZ_ASSERT(NS_IsMainThread());

  MOZ_ASSERT(mAllocateEven.isSome());
  if (!mAllocateEven.isSome()) {
    return INVALID_STREAM;
  }

  uint16_t i = (*mAllocateEven ? 0 : 1);

  // Find the lowest odd/even id that is not present in mStreamIds
  for (auto id : mStreamIds) {
    if (i >= GetStreamIdCeiling()) {
      return INVALID_STREAM;
    }

    if (id == i) {
      // i is in use, try the next one
      i += 2;
    } else if (id > i) {
      // i is definitely not in use
      break;
    }
  }

  return i;
}

// Returns a POSIX error code.
int DataChannelConnection::SendControlMessage(DataChannel& aChannel,
                                              const uint8_t* data,
                                              uint32_t len) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  // Create message instance and send
  // Note: Main-thread IO, but doesn't block
#if (UINT32_MAX > SIZE_MAX)
  if (len > SIZE_MAX) {
    return EMSGSIZE;
  }
#endif

  DataChannelMessageMetadata metadata(aChannel.mStream,
                                      DATA_CHANNEL_PPID_CONTROL, false);
  nsCString buffer(reinterpret_cast<const char*>(data), len);
  OutgoingMsg msg(std::move(buffer), metadata);

  return SendMessage(aChannel, std::move(msg));
}

// Returns a POSIX error code.
int DataChannelConnection::SendOpenAckMessage(DataChannel& aChannel) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_INFO(("%p: Sending DataChannel open ack, channel %p", this, &aChannel));
  struct rtcweb_datachannel_ack ack = {};
  ack.msg_type = DATA_CHANNEL_ACK;

  return SendControlMessage(aChannel, (const uint8_t*)&ack, sizeof(ack));
}

// Returns a POSIX error code.
int DataChannelConnection::SendOpenRequestMessage(DataChannel& aChannel) {
  DC_INFO(
      ("%p: Sending DataChannel open request, channel %p", this, &aChannel));
  const nsACString& label = aChannel.mLabel;
  const nsACString& protocol = aChannel.mProtocol;
  const bool unordered = !aChannel.mOrdered;
  const DataChannelReliabilityPolicy prPolicy = aChannel.mPrPolicy;
  const uint32_t prValue = aChannel.mPrValue;

  const size_t label_len = label.Length();     // not including nul
  const size_t proto_len = protocol.Length();  // not including nul
  // careful - request struct include one char for the label
  const size_t req_size = sizeof(struct rtcweb_datachannel_open_request) - 1 +
                          label_len + proto_len;
  UniqueFreePtr<struct rtcweb_datachannel_open_request> req(
      (struct rtcweb_datachannel_open_request*)moz_xmalloc(req_size));

  memset(req.get(), 0, req_size);
  req->msg_type = DATA_CHANNEL_OPEN_REQUEST;
  switch (prPolicy) {
    case DataChannelReliabilityPolicy::Reliable:
      req->channel_type = DATA_CHANNEL_RELIABLE;
      break;
    case DataChannelReliabilityPolicy::LimitedLifetime:
      req->channel_type = DATA_CHANNEL_PARTIAL_RELIABLE_TIMED;
      break;
    case DataChannelReliabilityPolicy::LimitedRetransmissions:
      req->channel_type = DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT;
      break;
    default:
      return EINVAL;
  }
  if (unordered) {
    // Per the current types, all differ by 0x80 between ordered and unordered
    req->channel_type |=
        0x80;  // NOTE: be careful if new types are added in the future
  }

  req->reliability_param = htonl(prValue);
  req->priority = htons(0); /* XXX: add support */
  req->label_length = htons(label_len);
  req->protocol_length = htons(proto_len);
  memcpy(&req->label[0], PromiseFlatCString(label).get(), label_len);
  memcpy(&req->label[label_len], PromiseFlatCString(protocol).get(), proto_len);

  // TODO: req_size is an int... that looks hairy
  return SendControlMessage(aChannel, (const uint8_t*)req.get(), req_size);
}

// Caller must ensure that length <= SIZE_MAX
void DataChannelConnection::HandleOpenRequestMessage(
    const struct rtcweb_datachannel_open_request* req, uint32_t length,
    uint16_t stream) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DataChannelReliabilityPolicy prPolicy;

  const size_t requiredLength = (sizeof(*req) - 1) + ntohs(req->label_length) +
                                ntohs(req->protocol_length);
  if (((size_t)length) != requiredLength) {
    if (((size_t)length) < requiredLength) {
      DC_ERROR(
          ("%p: insufficient length: %u, should be %zu. Unable to continue.",
           this, length, requiredLength));
      return;
    }
    DC_WARN(("%p: Inconsistent length: %u, should be %zu", this, length,
             requiredLength));
  }

  DC_DEBUG(("%p: length %u, sizeof(*req) = %zu", this, length, sizeof(*req)));

  switch (req->channel_type) {
    case DATA_CHANNEL_RELIABLE:
    case DATA_CHANNEL_RELIABLE_UNORDERED:
      prPolicy = DataChannelReliabilityPolicy::Reliable;
      break;
    case DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT:
    case DATA_CHANNEL_PARTIAL_RELIABLE_REXMIT_UNORDERED:
      prPolicy = DataChannelReliabilityPolicy::LimitedRetransmissions;
      break;
    case DATA_CHANNEL_PARTIAL_RELIABLE_TIMED:
    case DATA_CHANNEL_PARTIAL_RELIABLE_TIMED_UNORDERED:
      prPolicy = DataChannelReliabilityPolicy::LimitedLifetime;
      break;
    default:
      DC_ERROR(("%p: Unknown channel type %d", this, req->channel_type));
      /* XXX error handling */
      return;
  }

  if (stream >= mNegotiatedIdLimit) {
    DC_ERROR(
        ("%p: stream %u out of bounds (%u)", this, stream, mNegotiatedIdLimit));
    return;
  }

  const uint32_t prValue = ntohl(req->reliability_param);
  const bool ordered = !(req->channel_type & 0x80);
  const nsCString label(&req->label[0], ntohs(req->label_length));
  const nsCString protocol(&req->label[ntohs(req->label_length)],
                           ntohs(req->protocol_length));

  // Always dispatch this to mainthread; this is a brand new datachannel, which
  // has not had any opportunity to be transferred to a worker.
  Dispatch(
      NS_NewCancelableRunnableFunction(
          "DataChannelConnection::HandleOpenRequestMessage",
          [this, self = RefPtr<DataChannelConnection>(this), stream, prPolicy,
           prValue, ordered, label, protocol]() {
            if (!mListener) {
              DC_INFO(
                  ("%p: Ignoring open request for stream %u after "
                   "Destroy",
                   this, stream));
              return;
            }
            RefPtr<DataChannel> channel = FindChannelByStream(stream);
            if (channel) {
              if (!channel->mNegotiated) {
                DC_ERROR((
                    "HandleOpenRequestMessage: channel for pre-existing stream "
                    "%u that was not externally negotiated. JS is lying to us, "
                    "or there's an id collision.",
                    stream));
                /* XXX: some error handling */
              } else {
                DC_DEBUG(("Open for externally negotiated channel %u", stream));
                // XXX should also check protocol, maybe label
                if (prPolicy != channel->mPrPolicy ||
                    prValue != channel->mPrValue ||
                    ordered != channel->mOrdered) {
                  DC_WARN(
                      ("external negotiation mismatch with OpenRequest:"
                       "channel %u, policy %s/%s, value %u/%u, ordered %d/%d",
                       stream, ToString(prPolicy), ToString(channel->mPrPolicy),
                       prValue, channel->mPrValue, static_cast<int>(ordered),
                       static_cast<int>(channel->mOrdered)));
                }
              }
              return;
            }
            channel = new DataChannel(this, stream, label, protocol, prPolicy,
                                      prValue, ordered, false);
            mChannels.Insert(channel);
            mStreamIds.InsertElementSorted(stream);

            DC_INFO(("%p: sending ON_CHANNEL_CREATED for %p/%s/%s: %u", this,
                     channel.get(), channel->mLabel.get(),
                     channel->mProtocol.get(), stream));

            // Awkward. If we convert over to using Maybe for this in
            // DataChannel, we won't need to have this extra conversion, since
            // Nullable converts easily to Maybe.
            dom::Nullable<uint16_t> maxLifeTime;
            dom::Nullable<uint16_t> maxRetransmits;
            if (prPolicy == DataChannelReliabilityPolicy::LimitedLifetime) {
              maxLifeTime.SetValue(std::min(
                  std::numeric_limits<uint16_t>::max(), (uint16_t)prValue));
            } else if (prPolicy ==
                       DataChannelReliabilityPolicy::LimitedRetransmissions) {
              maxRetransmits.SetValue(std::min(
                  std::numeric_limits<uint16_t>::max(), (uint16_t)prValue));
            }

            if (mListener) {
              // important to give it an already_AddRefed pointer!
              // TODO(bug 1974443): Have nsDOMDataChannel create the DataChannel
              // object, or have DataChannel take an nsDOMDataChannel, to avoid
              // passing this param list more than once?
              mListener->NotifyDataChannel(do_AddRef(channel), label, ordered,
                                           maxLifeTime, maxRetransmits,
                                           protocol, false);
              // Spec says to queue this in the queued task for ondatachannel
              channel->AnnounceOpen();
            }

            mSTS->Dispatch(
                NS_NewCancelableRunnableFunction(
                    "DataChannelConnection::HandleOpenRequestMessage",
                    [this, self = RefPtr<DataChannelConnection>(this),
                     channel = std::move(channel)]() {
                      // Note that any message can be buffered;
                      // SendOpenAckMessage may error later than this check.
                      const auto error = SendOpenAckMessage(*channel);
                      if (error) {
                        DC_ERROR(
                            ("%p: SendOpenAckMessage failed, channel %p, error "
                             "= %d",
                             this, channel.get(), error));
                        FinishClose_s(channel);
                        return;
                      }
                      channel->mWaitingForAck = false;
                      channel->mSendStreamNeedsReset = true;
                      channel->mRecvStreamNeedsReset = true;
                      OnStreamOpen(channel->mStream);
                    }),
                NS_DISPATCH_FALLIBLE);
          }),
      NS_DISPATCH_FALLIBLE);
}

// Caller must ensure that length <= SIZE_MAX
void DataChannelConnection::HandleOpenAckMessage(
    const struct rtcweb_datachannel_ack* ack, uint32_t length,
    uint16_t stream) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());

  RefPtr<DataChannel> channel = FindChannelByStream(stream);
  if (NS_WARN_IF(!channel)) {
    return;
  }

  DC_INFO(("%p: OpenAck received for channel %p, stream %u, waiting=%d", this,
           channel.get(), stream, channel->mWaitingForAck ? 1 : 0));

  channel->mWaitingForAck = false;

  // Either externally negotiated or we sent Open
  channel->AnnounceOpen();
  OnStreamOpen(stream);
}

// Caller must ensure that length <= SIZE_MAX
void DataChannelConnection::HandleUnknownMessage(uint32_t ppid, uint32_t length,
                                                 uint16_t stream) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  /* XXX: Send an error message? */
  DC_ERROR(("%p: unknown DataChannel message received: %u, len %u on stream %d",
            this, ppid, length, stream));
  // XXX Log to JS error console if possible
}

void DataChannelConnection::HandleDataMessage(IncomingMsg&& aMsg) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());

  RefPtr<DataChannel> channel = FindChannelByStream(aMsg.GetStreamId());
  if (!channel) {
    MOZ_ASSERT(false,
               "Wait until OnStreamOpen is called before calling "
               "HandleDataMessage!");
    return;
  }

  const size_t data_length = aMsg.GetData().Length();
  bool isBinary = false;

  switch (aMsg.GetPpid()) {
    case DATA_CHANNEL_PPID_DOMSTRING:
    case DATA_CHANNEL_PPID_DOMSTRING_PARTIAL:
      DC_DEBUG(
          ("%p: DataChannel: Received string message of length %zu on "
           "channel %u",
           this, data_length, channel->mStream));
      // WebSockets checks IsUTF8() here; we can try to deliver it
      break;

    case DATA_CHANNEL_PPID_DOMSTRING_EMPTY:
      DC_DEBUG(
          ("%p: DataChannel: Received empty string message of length %zu on "
           "channel %u",
           this, data_length, channel->mStream));
      // Just in case.
      aMsg.GetData().Truncate(0);
      break;

    case DATA_CHANNEL_PPID_BINARY:
    case DATA_CHANNEL_PPID_BINARY_PARTIAL:
      DC_DEBUG(
          ("%p: DataChannel: Received binary message of length %zu on "
           "channel id %u",
           this, data_length, channel->mStream));
      isBinary = true;
      break;

    case DATA_CHANNEL_PPID_BINARY_EMPTY:
      DC_DEBUG(
          ("%p: DataChannel: Received empty binary message of length %zu on "
           "channel id %u",
           this, data_length, channel->mStream));
      // Just in case.
      aMsg.GetData().Truncate(0);
      isBinary = true;
      break;

    default:
      NS_ERROR("Unknown data PPID");
      DC_ERROR(("%p: Unknown data PPID %" PRIu32, this, aMsg.GetPpid()));
      return;
  }

  channel->OnMessageReceived(std::move(aMsg.GetData()), isBinary);
}

void DataChannelConnection::HandleDCEPMessage(IncomingMsg&& aMsg) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  const struct rtcweb_datachannel_open_request* req;
  const struct rtcweb_datachannel_ack* ack;

  req = reinterpret_cast<const struct rtcweb_datachannel_open_request*>(
      aMsg.GetData().BeginReading());

  size_t data_length = aMsg.GetLength();

  DC_INFO(("%p: Handling DCEP message of length %zu", this, data_length));

  // Ensure minimum message size (ack is the smallest DCEP message)
  if (data_length < sizeof(*ack)) {
    DC_WARN(("%p: Ignored invalid DCEP message (too short)", this));
    return;
  }

  switch (req->msg_type) {
    case DATA_CHANNEL_OPEN_REQUEST:
      // structure includes a possibly-unused char label[1] (in a packed
      // structure)
      if (NS_WARN_IF(data_length < sizeof(*req) - 1)) {
        return;
      }

      HandleOpenRequestMessage(req, data_length, aMsg.GetStreamId());
      break;
    case DATA_CHANNEL_ACK:
      // >= sizeof(*ack) checked above

      ack = reinterpret_cast<const struct rtcweb_datachannel_ack*>(
          aMsg.GetData().BeginReading());
      HandleOpenAckMessage(ack, data_length, aMsg.GetStreamId());
      break;
    default:
      HandleUnknownMessage(aMsg.GetPpid(), data_length, aMsg.GetStreamId());
      break;
  }
}

bool DataChannelConnection::ReassembleMessageChunk(IncomingMsg& aReassembled,
                                                   const void* buffer,
                                                   size_t length, uint32_t ppid,
                                                   uint16_t stream) {
  // Note: Until we support SIZE_MAX sized messages, we need this check
#if (SIZE_MAX > UINT32_MAX)
  if (length > UINT32_MAX) {
    DC_ERROR(("%p: Cannot handle message of size %zu (max=%u)", this, length,
              UINT32_MAX));
    return false;
  }
#endif

  // Ensure it doesn't blow up our buffer
  // TODO: Change 'WEBRTC_DATACHANNEL_MAX_MESSAGE_SIZE_LOCAL' to whatever the
  //       new buffer is capable of holding.
  if (length + aReassembled.GetLength() >
      WEBRTC_DATACHANNEL_MAX_MESSAGE_SIZE_LOCAL) {
    DC_ERROR(
        ("%p: Buffered message would become too large to handle, closing "
         "connection",
         this));
    return false;
  }

  if (aReassembled.GetPpid() != ppid) {
    NS_WARNING("DataChannel message aborted by fragment type change!");
    return false;
  }

  aReassembled.Append((uint8_t*)buffer, length);

  return true;
}

void DataChannelConnection::OnStreamsReset(std::vector<uint16_t>&& aStreams) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  for (auto stream : aStreams) {
    DC_INFO(("%p: Received reset request for stream %u", this, stream));
    RefPtr<DataChannel> channel = FindChannelByStream(stream);
    if (channel) {
      channel->mRecvStreamNeedsReset = false;
      if (channel->mSendStreamNeedsReset) {
        // We do not send our own reset yet, we give the RTCDataChannel a chance
        // to finish sending messages first.
        DC_INFO(("%p: Need to send a reset, closing gracefully", this));
        channel->GracefulClose();
      } else {
        DC_INFO(
            ("%p: We've already reset our stream, closing immediately", this));
        FinishClose_s(channel);
      }
    }
  }
}

void DataChannelConnection::OnStreamsResetComplete(
    std::vector<uint16_t>&& aStreams) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  for (auto stream : aStreams) {
    DC_INFO(("%p: Received reset response for stream %u", this, stream));
    RefPtr<DataChannel> channel = FindChannelByStream(stream);
    if (channel) {
      channel->mSendStreamNeedsReset = false;
      if (!channel->mRecvStreamNeedsReset) {
        // The other end has already performed its reset
        DC_INFO(
            ("%p: Remote stream has already been reset, closing immediately",
             this));
        FinishClose_s(channel);
      }
    }
  }
}

already_AddRefed<DataChannel> DataChannelConnection::Open(
    const nsACString& label, const nsACString& protocol,
    DataChannelReliabilityPolicy prPolicy, bool inOrder, uint32_t prValue,
    bool aExternalNegotiated, uint16_t aStream) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!aExternalNegotiated) {
    if (mAllocateEven.isSome()) {
      aStream = FindFreeStream();
      if (aStream == INVALID_STREAM) {
        return nullptr;
      }
    } else {
      // We do not yet know whether we are client or server, and an id has not
      // been chosen for us. We will need to choose later.
      aStream = INVALID_STREAM;
    }
  }

  DC_INFO(
      ("%p: DC Open: label %s/%s, type %s, inorder %d, prValue %u, "
       "external: %s, stream %u",
       this, PromiseFlatCString(label).get(),
       PromiseFlatCString(protocol).get(), ToString(prPolicy), inOrder, prValue,
       aExternalNegotiated ? "true" : "false", aStream));

  if ((prPolicy == DataChannelReliabilityPolicy::Reliable) && (prValue != 0)) {
    return nullptr;
  }

  if (aStream != INVALID_STREAM) {
    if (mStreamIds.ContainsSorted(aStream)) {
      DC_ERROR(("%p: external negotiation of already-open channel %u", this,
                aStream));
      // This is the only place where duplicate id checking is performed. The
      // JSImpl code assumes that any error is due to id-related problems. This
      // probably needs some cleanup.
      return nullptr;
    }

    DC_DEBUG(("%p: Inserting externally-negotiated id %u", this,
              static_cast<unsigned>(aStream)));
    mStreamIds.InsertElementSorted(aStream);
  }

  RefPtr<DataChannel> channel(new DataChannel(this, aStream, label, protocol,
                                              prPolicy, prValue, inOrder,
                                              aExternalNegotiated));
  mChannels.Insert(channel);

  if (aStream != INVALID_STREAM) {
    mSTS->Dispatch(NS_NewCancelableRunnableFunction(
                       "DataChannel::OpenFinish",
                       [this, self = RefPtr<DataChannelConnection>(this),
                        channel]() mutable { OpenFinish(channel); }),
                   NS_DISPATCH_FALLIBLE);
  }

  return channel.forget();
}

// Separate routine so we can also call it to finish up from pending opens
void DataChannelConnection::OpenFinish(RefPtr<DataChannel> aChannel) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  const uint16_t stream = aChannel->mStream;

  // Cases we care about:
  // Pre-negotiated:
  //    Not Open:
  //      Doesn't fit:
  //         -> change initial ask or renegotiate after open
  //      -> queue open
  //    Open:
  //      Doesn't fit:
  //         -> RaiseStreamLimitTo && queue
  //      Does fit:
  //         -> open
  // Not negotiated:
  //    Not Open:
  //      -> queue open
  //    Open:
  //      -> Try to get a stream
  //      Doesn't fit:
  //         -> RaiseStreamLimitTo && queue
  //      Does fit:
  //         -> open
  // So the Open cases are basically the same
  // Not Open cases are simply queue for non-negotiated, and
  // either change the initial ask or possibly renegotiate after open.
  DataChannelConnectionState state = GetState();
  if (state != DataChannelConnectionState::Open ||
      stream >= mNegotiatedIdLimit) {
    if (state == DataChannelConnectionState::Open) {
      MOZ_ASSERT(stream != INVALID_STREAM);
      // RaiseStreamLimitTo() limits to MAX_NUM_STREAMS -- allocate extra
      // streams to avoid asking for more every time we want a higher limit.
      uint16_t num_desired = std::min(16 * (stream / 16 + 1), MAX_NUM_STREAMS);
      DC_DEBUG(("%p: Attempting to raise stream limit %u -> %u", this,
                mNegotiatedIdLimit, num_desired));
      if (!RaiseStreamLimitTo(num_desired)) {
        NS_ERROR("Failed to request more streams");
        FinishClose_s(aChannel);
        return;
      }
    }
    DC_INFO(("%p: Queuing channel %p (%u) to finish open", this, aChannel.get(),
             stream));
    mPending.insert(std::move(aChannel));
    return;
  }

  MOZ_ASSERT(state == DataChannelConnectionState::Open);
  MOZ_ASSERT(stream != INVALID_STREAM);
  MOZ_ASSERT(stream < mNegotiatedIdLimit);

  if (!aChannel->mNegotiated) {
    if (!aChannel->mOrdered) {
      // Don't send unordered until this gets cleared.
      aChannel->mWaitingForAck = true;
    }

    const int error = SendOpenRequestMessage(*aChannel);
    if (error) {
      DC_ERROR(("%p: SendOpenRequest failed, error = %d", this, error));
      FinishClose_s(aChannel);
      return;
    }
  }

  // Even if we're in the negotiated case, and will never send an open request,
  // we're supposed to send a stream reset when we tear down.
  aChannel->mSendStreamNeedsReset = true;
  aChannel->mRecvStreamNeedsReset = true;

  if (aChannel->mNegotiated) {
    // Either externally negotiated or we sent Open
    aChannel->AnnounceOpen();
    OnStreamOpen(stream);
  }
}

nsISerialEventTarget* DataChannelConnection::GetIOThread() {
  // Spawn a thread to send the data
  if (!mInternalIOThread) {
    // TODO(bug 1998966): Lazy shutdown once done? Maybe have this live in
    // DataChannel (so we have an IO thread for each channel that sends blobs)?
    NS_NewNamedThread("DataChannel IO", getter_AddRefs(mInternalIOThread));
  }

  return mInternalIOThread.get();
}

void DataChannelConnection::SetState(DataChannelConnectionState aState) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());

  DC_DEBUG(
      ("%p: DataChannelConnection labeled %s switching connection state %s -> "
       "%s",
       this, mTransportId.c_str(), ToString(mState), ToString(aState)));

  if (mState == aState) {
    return;
  }

  mState = aState;

  if (mState == DataChannelConnectionState::Open) {
    Dispatch(NS_NewCancelableRunnableFunction(
                 __func__,
                 [this, self = RefPtr<DataChannelConnection>(this),
                  maxChannels = Some(mNegotiatedIdLimit)]() {
                   if (mListener) {
                     mListener->NotifySctpConnected(maxChannels);
                   }
                 }),
             NS_DISPATCH_FALLIBLE);
  } else if (mState == DataChannelConnectionState::Closed) {
    Dispatch(NS_NewCancelableRunnableFunction(
                 __func__,
                 [this, self = RefPtr<DataChannelConnection>(this)]() {
                   if (mListener) {
                     mListener->NotifySctpClosed();
                   }
                 }),
             NS_DISPATCH_FALLIBLE);
  }
}

void DataChannelConnection::SendDataMessage(DataChannel& aChannel,
                                            nsACString&& aMsg, bool aIsBinary) {
  // Could be main, could be a worker

  nsCString temp(std::move(aMsg));

  mSTS->Dispatch(
      NS_NewCancelableRunnableFunction(
          __func__,
          [this, self = RefPtr<DataChannelConnection>(this),
           channel = RefPtr(&aChannel), msg = std::move(temp),
           aIsBinary]() mutable {
            Maybe<uint16_t> maxRetransmissions;
            Maybe<uint16_t> maxLifetimeMs;

            switch (channel->mPrPolicy) {
              case DataChannelReliabilityPolicy::Reliable:
                break;
              case DataChannelReliabilityPolicy::LimitedRetransmissions:
                maxRetransmissions = Some(channel->mPrValue);
                break;
              case DataChannelReliabilityPolicy::LimitedLifetime:
                maxLifetimeMs = Some(channel->mPrValue);
                break;
            }

            uint32_t ppid;
            if (aIsBinary) {
              if (msg.Length()) {
                ppid = DATA_CHANNEL_PPID_BINARY;
              } else {
                ppid = DATA_CHANNEL_PPID_BINARY_EMPTY;
                msg.Append('\0');
              }
            } else {
              if (msg.Length()) {
                ppid = DATA_CHANNEL_PPID_DOMSTRING;
              } else {
                ppid = DATA_CHANNEL_PPID_DOMSTRING_EMPTY;
                msg.Append('\0');
              }
            }

            DataChannelMessageMetadata metadata(
                channel->mStream, ppid,
                !channel->mOrdered && !channel->mWaitingForAck,
                maxRetransmissions, maxLifetimeMs);
            // Create message instance and send
            OutgoingMsg outgoing(std::move(msg), metadata);

            SendMessage(*channel, std::move(outgoing));
          }),
      NS_DISPATCH_FALLIBLE);
}

void DataChannelConnection::EndOfStream(const RefPtr<DataChannel>& aChannel) {
  mSTS->Dispatch(
      NS_NewCancelableRunnableFunction(
          __func__,
          [this, self = RefPtr<DataChannelConnection>(this), channel = aChannel,
           stream = aChannel->mStream]() {
            if (channel->mSendStreamNeedsReset) {
              if (channel->mEndOfStreamCalled) {
                return;
              }
              channel->mEndOfStreamCalled = true;
              DC_INFO((
                  "%p: Need to send a reset for channel %p, closing gracefully",
                  this, channel.get()));
              nsTArray<uint16_t> temp({stream});
              bool success = ResetStreams(temp);
              if (success) {
                return;
              }
              // We presume that OnStreamResetComplete will not be called in
              // this case, nor will we receive a stream reset from the other
              // end.
              DC_INFO(
                  ("%p: Failed to send a reset for channel %p, closing "
                   "immediately",
                   this, channel.get()));
              channel->mRecvStreamNeedsReset = false;
            }

            if (!channel->mRecvStreamNeedsReset) {
              // Stream is reset in both directions (or never existed in the
              // first place), we're ready to finish tearing down.
              DC_INFO(
                  ("%p: Stream does not need reset in either direction for "
                   "channel %p",
                   this, channel.get()));
              FinishClose_s(channel);
            }
          }),
      NS_DISPATCH_FALLIBLE);
}

void DataChannel::EndOfStream() {
  // This can happen before mDomEventTarget is actually ready.
  if (mConnection) {
    mConnection->EndOfStream(this);
  }
}

RefPtr<dom::RTCDataChannel> DataChannel::GetDomDataChannel() const {
  MOZ_ASSERT(mDomEventTarget->IsOnCurrentThread());
  if (NS_IsMainThread()) {
    return mMainthreadDomDataChannel;
  }
  return mWorkerDomDataChannel;
}

void DataChannelConnection::FinishClose_s(const RefPtr<DataChannel>& aChannel,
                                          Maybe<dom::RTCErrorParams> aError) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());

  // We're removing this from all containers, make sure the passed pointer
  // stays valid.
  // It is possible for this to be called twice if both JS and the transport
  // side cause closure at the same time, but this is idempotent so no big deal
  aChannel->mBufferedData.Clear();
  mChannels.Remove(aChannel);
  mPending.erase(aChannel);

  // Close the channel's data transport by following the associated
  // procedure.
  aChannel->AnnounceClosed(std::move(aError));
}

void DataChannelConnection::CloseAll_s(Maybe<dom::RTCErrorParams> aError) {
  // Make sure no more channels will be opened
  SetState(DataChannelConnectionState::Closed);

  nsTArray<uint16_t> streamsToReset;
  // Close current channels
  // If there are runnables, they hold a strong ref and keep the channel
  // and/or connection alive (even if in a CLOSED state)
  for (auto& channel : mChannels.GetAll()) {
    DC_INFO(("%p: closing channel %p, stream %u", this, channel.get(),
             channel->mStream));
    if (channel->mSendStreamNeedsReset) {
      DC_INFO(("%p: channel %p needs to send reset", this, channel.get()));
      channel->mSendStreamNeedsReset = false;
      streamsToReset.AppendElement(channel->mStream);
    }
    // We do not wait for the reset to finish in this case; we won't be around
    // to see the response.
    FinishClose_s(channel, aError);
  }

  // Clean up any pending opens for channels
  std::set<RefPtr<DataChannel>> temp(std::move(mPending));
  // Technically in an unspecified state, although no reasonable impl will leave
  // anything in here.
  mPending.clear();
  for (const auto& channel : temp) {
    DC_INFO(("%p: closing pending channel %p, stream %u", this, channel.get(),
             channel->mStream));
    FinishClose_s(channel);  // also releases the ref on each iteration
  }

  // It's more efficient to let the Resets queue in shutdown and then
  // ResetStreams() here.
  if (!streamsToReset.IsEmpty()) {
    ResetStreams(streamsToReset);
  }
}

void DataChannelConnection::CloseAll() {
  MOZ_ASSERT(NS_IsMainThread());
  DC_INFO(("%p: Closing all channels", this));

  mSTS->Dispatch(NS_NewCancelableRunnableFunction(
                     "DataChannelConnection::CloseAll",
                     [this, self = RefPtr<DataChannelConnection>(this)]() {
                       CloseAll_s();
                     }),
                 NS_DISPATCH_FALLIBLE);
}

void DataChannelConnection::MarkStreamAvailable(uint16_t aStream) {
  MOZ_ASSERT(NS_IsMainThread());
  mStreamIds.RemoveElementSorted(aStream);
}

bool DataChannelConnection::Channels::IdComparator::Equals(
    const RefPtr<DataChannel>& aChannel, uint16_t aId) const {
  return aChannel->mStream == aId;
}

bool DataChannelConnection::Channels::IdComparator::LessThan(
    const RefPtr<DataChannel>& aChannel, uint16_t aId) const {
  return aChannel->mStream < aId;
}

bool DataChannelConnection::Channels::IdComparator::Equals(
    const RefPtr<DataChannel>& a1, const RefPtr<DataChannel>& a2) const {
  return Equals(a1, a2->mStream);
}

bool DataChannelConnection::Channels::IdComparator::LessThan(
    const RefPtr<DataChannel>& a1, const RefPtr<DataChannel>& a2) const {
  return LessThan(a1, a2->mStream);
}

void DataChannelConnection::Channels::Insert(
    const RefPtr<DataChannel>& aChannel) {
  DC_DEBUG(("%p: Inserting channel %u : %p", this, aChannel->mStream,
            aChannel.get()));
  MutexAutoLock lock(mMutex);
  if (aChannel->mStream != INVALID_STREAM) {
    MOZ_ASSERT(!mChannels.ContainsSorted(aChannel, IdComparator()));
  }

  MOZ_ASSERT(!mChannels.Contains(aChannel));

  mChannels.InsertElementSorted(aChannel, IdComparator());
}

bool DataChannelConnection::Channels::Remove(
    const RefPtr<DataChannel>& aChannel) {
  DC_DEBUG(("%p: Removing channel %u : %p", this, aChannel->mStream,
            aChannel.get()));
  MutexAutoLock lock(mMutex);
  if (aChannel->mStream == INVALID_STREAM) {
    return mChannels.RemoveElement(aChannel);
  }

  auto index = mChannels.BinaryIndexOf(aChannel->mStream, IdComparator());
  if (index != ChannelArray::NoIndex) {
    if (mChannels[index].get() == aChannel.get()) {
      mChannels.RemoveElementAt(index);
      return true;
    }
  }
  return false;
}

RefPtr<DataChannel> DataChannelConnection::Channels::Get(uint16_t aId) const {
  MutexAutoLock lock(mMutex);
  auto index = mChannels.BinaryIndexOf(aId, IdComparator());
  if (index == ChannelArray::NoIndex) {
    return nullptr;
  }
  return mChannels[index];
}

RefPtr<DataChannel> DataChannelConnection::Channels::GetNextChannel(
    uint16_t aCurrentId) const {
  MutexAutoLock lock(mMutex);
  if (mChannels.IsEmpty()) {
    return nullptr;
  }

  auto index = mChannels.IndexOfFirstElementGt(aCurrentId, IdComparator());
  if (index == mChannels.Length()) {
    index = 0;
  }
  return mChannels[index];
}

DataChannel::DataChannel(DataChannelConnection* connection, uint16_t stream,
                         const nsACString& label, const nsACString& protocol,
                         DataChannelReliabilityPolicy policy, uint32_t value,
                         bool ordered, bool negotiated)
    : mLabel(label),
      mProtocol(protocol),
      mPrPolicy(policy),
      mPrValue(value),
      mNegotiated(negotiated),
      mOrdered(ordered),
      mStream(stream),
      mConnection(connection),
      mDomEventTarget(new StopGapEventTarget) {
  DC_INFO(
      ("%p: Necko DataChannel created, label '%s'. Waiting for RTCDataChannel "
       "to be created.",
       this, mLabel.get()));
  NS_ASSERTION(mConnection, "NULL connection");
}

DataChannel::~DataChannel() {
  DC_INFO(("%p: DataChannel is being destroyed.", this));
}

void DataChannel::SetMainthreadDomDataChannel(dom::RTCDataChannel* aChannel) {
  MOZ_ASSERT(NS_IsMainThread());
  DC_INFO(
      ("%p: Mainthread RTCDataChannel created(%p). Waiting for confirmation of "
       "event target.",
       this, aChannel));
  mMainthreadDomDataChannel = aChannel;
  SetMaxMessageSize(mConnection->GetMaxMessageSize());
  if (GetStream()) {
    mMainthreadDomDataChannel->SetId(*GetStream());
  }
}

void DataChannel::OnWorkerTransferStarted() {
  MOZ_ASSERT(NS_IsMainThread());
  DC_INFO((
      "%p: RTCDataChannel is being transferred. Disabling synchronous updates. "
      "Mainthread will not be our event target, waiting to learn worker "
      "thread.",
      this));
  mHasWorkerDomDataChannel = true;
}

void DataChannel::OnWorkerTransferComplete(dom::RTCDataChannel* aChannel) {
  MOZ_ASSERT(!NS_IsMainThread());
  DC_INFO(
      ("%p: Worker RTCDataChannel created(%p). Worker thread is our event "
       "target.",
       this, aChannel));
  mWorkerDomDataChannel = aChannel;
  mDomEventTarget->SetRealEventTarget(GetCurrentSerialEventTarget());
}

void DataChannel::OnWorkerTransferDisabled() {
  MOZ_ASSERT(NS_IsMainThread());
  DC_INFO(
      ("%p: Mainthread RTCDataChannel is no longer eligible for transfer. "
       "Mainthread is our event target.",
       this));
  mDomEventTarget->SetRealEventTarget(GetCurrentSerialEventTarget());
}

void DataChannel::UnsetMainthreadDomDataChannel() {
  MOZ_ASSERT(NS_IsMainThread());
  DC_INFO(("%p: Mainthread RTCDataChannel is being destroyed(%p).", this,
           mMainthreadDomDataChannel));
  mMainthreadDomDataChannel = nullptr;
  if (mHasWorkerDomDataChannel) {
    DC_INFO(
        ("Mainthread RTCDataChannel is being destroyed. Dispatching task to "
         "inform corresponding worker RTCDataChannel."));
    mDomEventTarget->Dispatch(
        NS_NewCancelableRunnableFunction(
            "DataChannel::UnsetMainthreadDomDataChannel",
            [this, self = RefPtr<DataChannel>(this)] {
              if (mWorkerDomDataChannel) {
                mWorkerDomDataChannel->UnsetWorkerNeedsUs();
              }
            }),
        NS_DISPATCH_FALLIBLE);
  } else {
    DC_INFO(("%p: No worker RTCDataChannel. Closing.", this));
    EndOfStream();
  }
}

void DataChannel::UnsetWorkerDomDataChannel() {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(mDomEventTarget->IsOnCurrentThread());
  DC_INFO(("%p: Worker RTCDataChannel is being destroyed(%p). Closing.", this,
           mWorkerDomDataChannel));
  mWorkerDomDataChannel = nullptr;
  EndOfStream();
}

void DataChannel::DecrementBufferedAmount(size_t aSize) {
  mDomEventTarget->Dispatch(
      NS_NewCancelableRunnableFunction(
          "DataChannel::DecrementBufferedAmount",
          [this, self = RefPtr<DataChannel>(this), aSize] {
            if (GetDomDataChannel()) {
              GetDomDataChannel()->DecrementBufferedAmount(aSize);
            }
          }),
      NS_DISPATCH_FALLIBLE);
}

void DataChannel::AnnounceOpen() {
  // When an underlying data transport is to be announced (the other peer
  // created a channel with negotiated unset or set to false), the user agent of
  // the peer that did not initiate the creation process MUST queue a task to
  // run the following steps:
  DC_INFO(
      ("%p: DataChannel is open. Queueing AnnounceOpen call to RTCDataChannel.",
       this));

  mDomEventTarget->Dispatch(
      NS_NewCancelableRunnableFunction(
          "DataChannel::AnnounceOpen",
          [this, self = RefPtr<DataChannel>(this)] {
            if (GetDomDataChannel() && !mAnnouncedOpen) {
              mAnnouncedOpen = true;
              DC_INFO(("Calling AnnounceOpen on RTCDataChannel."));
              GetDomDataChannel()->AnnounceOpen();
            }

            // Right now, we're already on mainthread, but this might be a
            // worker someday.
            if (mConnection) {
              GetMainThreadSerialEventTarget()->Dispatch(
                  NS_NewCancelableRunnableFunction(
                      "DataChannel::AnnounceOpen",
                      [this, self = RefPtr<DataChannel>(this),
                       connection = mConnection]() {
                        // Stats stuff
                        // TODO: Can we simplify this?
                        if (!mEverOpened && connection->mListener) {
                          mEverOpened = true;
                          connection->mListener->NotifyDataChannelOpen(this);
                        }
                      }),
                  NS_DISPATCH_FALLIBLE);
            }
          }),
      NS_DISPATCH_FALLIBLE);
}

void DataChannel::AnnounceClosed(Maybe<dom::RTCErrorParams> aError) {
  // When an RTCDataChannel object's underlying data transport has been closed,
  // the user agent MUST queue a task to run the following steps:
  DC_INFO(
      ("%p: DataChannel is closed. Queueing AnnounceClosed call to "
       "RTCDataChannel.",
       this));

  GetMainThreadSerialEventTarget()->Dispatch(
      NS_NewCancelableRunnableFunction(
          "DataChannel::AnnounceClosed",
          [this, self = RefPtr<DataChannel>(this), connection = mConnection,
           aError = std::move(aError)]() mutable {
            if (mAnnouncedClosed) {
              return;
            }
            mAnnouncedClosed = true;
            // We have to unset this first, and then fire DOM events, so the
            // event handler won't hit an error if it tries to reuse this id.
            if (mStream != INVALID_STREAM) {
              DC_INFO(("%p: Marking stream id %u available", this, mStream));
              connection->MarkStreamAvailable(mStream);
            }

            // Stats stuff
            if (mEverOpened && connection->mListener) {
              connection->mListener->NotifyDataChannelClosed(this);
            }

            DC_INFO(("%p: Dispatching AnnounceClosed to DOM thread", this));
            mDomEventTarget->Dispatch(
                NS_NewCancelableRunnableFunction(
                    "DataChannel::AnnounceClosed",
                    [this, self = RefPtr<DataChannel>(this),
                     aError = std::move(aError)] {
                      DC_INFO(("%p: Attempting to call AnnounceClosed.", this));
                      if (GetDomDataChannel()) {
                        DC_INFO(
                            ("%p: Calling AnnounceClosed on RTCDataChannel.",
                             this));
                        GetDomDataChannel()->AnnounceClosed(std::move(aError));
                      }
                    }),
                NS_DISPATCH_FALLIBLE);
          }),
      NS_DISPATCH_FALLIBLE);
}

void DataChannel::GracefulClose() {
  DC_INFO(
      ("%p: DataChannel transport is closing. Queueing GracefulClose call to "
       "RTCDataChannel.",
       this));

  mDomEventTarget->Dispatch(
      NS_NewCancelableRunnableFunction(
          "DataChannel::GracefulClose",
          [this, self = RefPtr<DataChannel>(this)] {
            if (GetDomDataChannel()) {
              DC_INFO(("Calling GracefulClose on RTCDataChannel."));
              GetDomDataChannel()->GracefulClose();
            }
          }),
      NS_DISPATCH_FALLIBLE);
}

void DataChannel::SendMsg(nsCString&& aMsg) {
  SendBuffer(std::move(aMsg), false);
}

void DataChannel::SendBinaryMsg(nsCString&& aMsg) {
  SendBuffer(std::move(aMsg), true);
}

void DataChannel::SendBuffer(nsCString&& aMsg, bool aBinary) {
  MOZ_ASSERT(mDomEventTarget->IsOnCurrentThread());
  if (mMessagesSentPromise) {
    mMessagesSentPromise = mMessagesSentPromise->Then(
        mDomEventTarget, __func__,
        [this, self = RefPtr<DataChannel>(this), msg = std::move(aMsg),
         aBinary](
            const GenericNonExclusivePromise::ResolveOrRejectValue&) mutable {
          if (mConnection) {
            mConnection->SendDataMessage(*this, std::move(msg), aBinary);
            return GenericNonExclusivePromise::CreateAndResolve(true, __func__);
          }
          return GenericNonExclusivePromise::CreateAndResolve(false, __func__);
        });

    UnsetMessagesSentPromiseWhenSettled();
    return;
  }
  mConnection->SendDataMessage(*this, std::move(aMsg), aBinary);
}

void DataChannel::SendBinaryBlob(nsIInputStream* aBlob) {
  MOZ_ASSERT(mDomEventTarget->IsOnCurrentThread());
  if (!mMessagesSentPromise) {
    mMessagesSentPromise =
        GenericNonExclusivePromise::CreateAndResolve(true, __func__);
  }

  mMessagesSentPromise = mMessagesSentPromise->Then(
      mConnection->GetIOThread(), __func__,
      [this, self = RefPtr<DataChannel>(this), blob = RefPtr(aBlob)](
          const GenericNonExclusivePromise::ResolveOrRejectValue&) {
        nsCString data;
        if (NS_SUCCEEDED(NS_ReadInputStreamToString(blob, data, -1))) {
          if (mConnection) {
            // This dispatches to STS, which is when we're supposed to resolve
            mConnection->SendDataMessage(*this, std::move(data), true);
          }
          blob->Close();
          return GenericNonExclusivePromise::CreateAndResolve(true, __func__);
        }
        return GenericNonExclusivePromise::CreateAndResolve(false, __func__);
      });

  UnsetMessagesSentPromiseWhenSettled();
}

void DataChannel::UnsetMessagesSentPromiseWhenSettled() {
  MOZ_ASSERT(mDomEventTarget->IsOnCurrentThread());
  // This is why we are using a non-exclusive promise; we want to null this out
  // when we're done, but only if nothing else has chained off of it.
  mMessagesSentPromise->Then(
      mDomEventTarget, __func__,
      [this, self = RefPtr(this), promise = mMessagesSentPromise]() {
        if (promise == mMessagesSentPromise) {
          mMessagesSentPromise = nullptr;
        }
      });
}

void DataChannel::SetStream(uint16_t aId) {
  MOZ_ASSERT(NS_IsMainThread());
  mStream = aId;

  // This is an inconvenient wrinkle in the spec; if the stream id is discovered
  // on main (for any reason), we update mainthread-homed RTCDataChannel
  // synchronously, but must dispatch for workers. It is possible this will
  // change, but probably not.
  if (mHasWorkerDomDataChannel) {
    DC_INFO(
        ("DataChannel has been allocated a stream ID. Queueing task to inform "
         "worker RTCDataChannel."));
    mDomEventTarget->Dispatch(
        NS_NewCancelableRunnableFunction(
            __func__,
            [this, self = RefPtr<DataChannel>(this), aId] {
              if (mWorkerDomDataChannel) {
                mWorkerDomDataChannel->SetId(aId);
              }
            }),
        NS_DISPATCH_FALLIBLE);
  } else {
    DC_INFO(
        ("%p: DataChannel has been allocated a stream ID. Synchronously "
         "informing mainthread RTCDataChannel.",
         this));
    mMainthreadDomDataChannel->SetId(aId);
  }
}

void DataChannel::SetMaxMessageSize(double aMaxMessageSize) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mHasWorkerDomDataChannel) {
    DC_INFO(
        ("DataChannel has updated its maximum message size. Queueing task to "
         "inform worker RTCDataChannel."));
    mDomEventTarget->Dispatch(
        NS_NewCancelableRunnableFunction(
            __func__,
            [this, self = RefPtr<DataChannel>(this), aMaxMessageSize] {
              if (mWorkerDomDataChannel) {
                mWorkerDomDataChannel->SetMaxMessageSize(aMaxMessageSize);
              }
            }),
        NS_DISPATCH_FALLIBLE);
  } else {
    DC_INFO(
        ("%p: DataChannel has updated its maximum message size. Synchronously "
         "informing mainthread RTCDataChannel.",
         this));
    if (mMainthreadDomDataChannel) {
      mMainthreadDomDataChannel->SetMaxMessageSize(aMaxMessageSize);
    }
  }
}

void DataChannel::OnMessageReceived(nsCString&& aMsg, bool aIsBinary) {
  // Receiving any data implies that the other end has received an OPEN
  // request from us.
  mWaitingForAck = false;

  DC_DEBUG(
      ("%p: received message (%s)", this, aIsBinary ? "binary" : "string"));

  mDomEventTarget->Dispatch(NS_NewCancelableRunnableFunction(
                                "DataChannel::OnMessageReceived",
                                [this, self = RefPtr<DataChannel>(this),
                                 msg = std::move(aMsg), aIsBinary]() {
                                  if (GetDomDataChannel()) {
                                    if (!mAnnouncedOpen) {
                                      mAnnouncedOpen = true;
                                      GetDomDataChannel()->AnnounceOpen();
                                    }
                                    GetDomDataChannel()->DoOnMessageAvailable(
                                        msg, aIsBinary);
                                  }
                                }),
                            NS_DISPATCH_FALLIBLE);
}

}  // namespace mozilla
