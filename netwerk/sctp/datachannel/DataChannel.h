/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NETWERK_SCTP_DATACHANNEL_DATACHANNEL_H_
#define NETWERK_SCTP_DATACHANNEL_DATACHANNEL_H_

#include <errno.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "DataChannelProtocol.h"
#include "MediaEventSource.h"
#include "mozilla/MozPromise.h"
#include "mozilla/Mutex.h"
#include "mozilla/StopGapEventTarget.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/Blob.h"
#include "mozilla/dom/RTCStatsReportBinding.h"
#include "mozilla/net/NeckoTargetHolder.h"
#include "nsCOMPtr.h"
#include "nsDeque.h"
#include "nsISupports.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "transport/MediaTransportTypes.h"
#include "transport/transportlayer.h"  // For TransportLayer::State

namespace mozilla {

class DataChannelConnection;
class DataChannel;
class MediaPacket;
class MediaTransportHandler;
namespace dom {
class RTCDataChannel;
struct RTCStatsCollection;
};  // namespace dom

enum class DataChannelConnectionState { Connecting, Open, Closed };
enum class DataChannelReliabilityPolicy {
  Reliable,
  LimitedRetransmissions,
  LimitedLifetime
};

class DataChannelMessageMetadata {
 public:
  DataChannelMessageMetadata(uint16_t aStreamId, uint32_t aPpid,
                             bool aUnordered,
                             Maybe<uint16_t> aMaxRetransmissions = Nothing(),
                             Maybe<uint16_t> aMaxLifetimeMs = Nothing())
      : mStreamId(aStreamId),
        mPpid(aPpid),
        mUnordered(aUnordered),
        mMaxRetransmissions(aMaxRetransmissions),
        mMaxLifetimeMs(aMaxLifetimeMs) {}

  DataChannelMessageMetadata(const DataChannelMessageMetadata& aOrig) = default;
  DataChannelMessageMetadata(DataChannelMessageMetadata&& aOrig) = default;
  DataChannelMessageMetadata& operator=(
      const DataChannelMessageMetadata& aOrig) = default;
  DataChannelMessageMetadata& operator=(DataChannelMessageMetadata&& aOrig) =
      default;

  uint16_t mStreamId;
  uint32_t mPpid;
  bool mUnordered;
  Maybe<uint16_t> mMaxRetransmissions;
  Maybe<uint16_t> mMaxLifetimeMs;
};

class OutgoingMsg {
 public:
  OutgoingMsg(nsACString&& data, const DataChannelMessageMetadata& aMetadata);
  OutgoingMsg(OutgoingMsg&& aOrig) = default;
  OutgoingMsg& operator=(OutgoingMsg&& aOrig) = default;
  OutgoingMsg(const OutgoingMsg&) = delete;
  OutgoingMsg& operator=(const OutgoingMsg&) = delete;

  void Advance(size_t offset);
  const DataChannelMessageMetadata& GetMetadata() const { return mMetadata; };
  size_t GetLength() const { return mData.Length(); };
  Span<const uint8_t> GetRemainingData() const {
    auto span = Span<const uint8_t>(mData);
    return span.From(mPos);
  }

 protected:
  nsCString mData;
  DataChannelMessageMetadata mMetadata;
  size_t mPos = 0;
};

class IncomingMsg {
 public:
  explicit IncomingMsg(uint32_t aPpid, uint16_t aStreamId)
      : mPpid(aPpid), mStreamId(aStreamId) {}
  IncomingMsg(IncomingMsg&& aOrig) = default;
  IncomingMsg& operator=(IncomingMsg&& aOrig) = default;
  IncomingMsg(const IncomingMsg&) = delete;
  IncomingMsg& operator=(const IncomingMsg&) = delete;

  void Append(const uint8_t* aData, size_t aLen) {
    mData.Append((const char*)aData, aLen);
  }

  const nsCString& GetData() const { return mData; }
  nsCString& GetData() { return mData; }
  size_t GetLength() const { return mData.Length(); };
  uint16_t GetStreamId() const { return mStreamId; }
  uint32_t GetPpid() const { return mPpid; }

 protected:
  // TODO(bug 1949918): We've historically passed this around as a c-string, but
  // that's not really appropriate for binary messages.
  nsCString mData;
  uint32_t mPpid;
  uint16_t mStreamId;
};

// Would be nice if this were DataChannel::StatsPromise, but no big deal.
typedef MozPromise<dom::RTCDataChannelStats, nsresult, true>
    DataChannelStatsPromise;

// One per PeerConnection
class DataChannelConnection : public net::NeckoTargetHolder {
  friend class DataChannel;
  friend class DataChannelConnectRunnable;
  friend class DataChannelConnectionUsrsctp;

 protected:
  virtual ~DataChannelConnection();

 public:
  enum class PendingType {
    None,  // No outgoing messages are pending.
    Dcep,  // Outgoing DCEP messages are pending.
    Data,  // Outgoing data channel messages are pending.
  };

  class DataConnectionListener : public SupportsWeakPtr {
   public:
    virtual ~DataConnectionListener() = default;

    // Called when a new DataChannel has been opened by the other side.
    virtual void NotifyDataChannel(
        already_AddRefed<DataChannel> aChannel, const nsACString& aLabel,
        bool aOrdered, mozilla::dom::Nullable<uint16_t> aMaxLifeTime,
        mozilla::dom::Nullable<uint16_t> aMaxRetransmits,
        const nsACString& aProtocol, bool aNegotiated) = 0;

    // Called when a DataChannel transitions to state open
    virtual void NotifyDataChannelOpen(DataChannel* aChannel) = 0;

    // Called when a DataChannel (that was open at some point in the past)
    // transitions to state closed
    virtual void NotifyDataChannelClosed(DataChannel* aChannel) = 0;

    // Called when SCTP connects
    virtual void NotifySctpConnected(Maybe<uint16_t> aMaxChannels) = 0;

    // Called when SCTP closes
    virtual void NotifySctpClosed() = 0;
  };

  // Create a new DataChannel Connection
  // Must be called on Main thread
  static Maybe<RefPtr<DataChannelConnection>> Create(
      DataConnectionListener* aListener, nsISerialEventTarget* aTarget,
      MediaTransportHandler* aHandler, const uint16_t aLocalPort,
      const uint16_t aNumStreams);

  DataChannelConnection(const DataChannelConnection&) = delete;
  DataChannelConnection(DataChannelConnection&&) = delete;
  DataChannelConnection& operator=(const DataChannelConnection&) = delete;
  DataChannelConnection& operator=(DataChannelConnection&&) = delete;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(DataChannelConnection)

  // Called immediately after construction
  virtual bool Init(const uint16_t aLocalPort, const uint16_t aNumStreams) = 0;
  // Called when our transport is ready to send and recv
  virtual void OnTransportReady() = 0;
  // This is called after an ACK comes in, to prompt subclasses to deliver
  // anything they've buffered while awaiting the ACK.
  virtual void OnStreamOpen(uint16_t stream) = 0;
  // Called when the base class wants to raise the stream limit
  virtual bool RaiseStreamLimitTo(uint16_t aNewLimit) = 0;
  // The exclusive upper bound on stream ids we will allocate. This is a
  // per-stack constant (usrsctp grows toward it on demand; dcsctp announces the
  // full range up front), and is safe to read from any thread.
  virtual uint16_t GetStreamIdCeiling() const = 0;
  // Called when the base class wants to send a message; it is expected that
  // this will eventually result in a call/s to SendSctpPacket once the SCTP
  // packet is ready to be sent to the transport.
  virtual int SendMessage(DataChannel& aChannel, OutgoingMsg&& aMsg) = 0;
  // Called when the base class receives a packet from the transport
  virtual void OnSctpPacketReceived(const MediaPacket& packet) = 0;
  // Called when the base class is closing streams
  virtual bool ResetStreams(nsTArray<uint16_t>& aStreams) = 0;
  // Called when the SCTP connection is being shut down
  virtual void Destroy();

  // Call only when the remote SDP has a=max-message-size
  void SetMaxMessageSize(uint64_t aMaxMessageSize);
  double GetMaxMessageSize();
  void HandleDataMessage(IncomingMsg&& aMsg);
  void HandleDCEPMessage(IncomingMsg&& aMsg);
  void ProcessQueuedOpens();
  void OnStreamsReset(std::vector<uint16_t>&& aStreams);
  void OnStreamsResetComplete(std::vector<uint16_t>&& aStreams);

  typedef DataChannelStatsPromise::AllPromiseType StatsPromise;
  RefPtr<StatsPromise> GetStats(const DOMHighResTimeStamp aTimestamp) const;

  bool ConnectToTransport(const std::string& aTransportId, const bool aClient,
                          const uint16_t aLocalPort,
                          const uint16_t aRemotePort);
  void TransportStateChange(const std::string& aTransportId,
                            TransportLayer::State aState,
                            const nsTArray<nsTArray<uint8_t>>& aRemoteCerts,
                            Maybe<dom::RTCErrorParams>);
  void SetSignals(const std::string& aTransportId);

  [[nodiscard]] already_AddRefed<DataChannel> Open(
      const nsACString& label, const nsACString& protocol,
      DataChannelReliabilityPolicy prPolicy, bool inOrder, uint32_t prValue,
      bool aExternalNegotiated, uint16_t aStream);

  void EndOfStream(const RefPtr<DataChannel>& aChannel);
  void FinishClose_s(const RefPtr<DataChannel>& aChannel,
                     Maybe<dom::RTCErrorParams> aError = Nothing());
  void CloseAll();
  void CloseAll_s(Maybe<dom::RTCErrorParams> aError = Nothing());
  void MarkStreamAvailable(uint16_t aStream);

  nsISerialEventTarget* GetIOThread();

  bool InShutdown() const {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    return mShutdown;
#else
    return false;
#endif
  }

 protected:
  class Channels {
   public:
    using ChannelArray = AutoTArray<RefPtr<DataChannel>, 16>;

    Channels() = default;
    Channels(const Channels&) = delete;
    Channels(Channels&&) = delete;
    Channels& operator=(const Channels&) = delete;
    Channels& operator=(Channels&&) = delete;

    void Insert(const RefPtr<DataChannel>& aChannel);
    bool Remove(const RefPtr<DataChannel>& aChannel);
    RefPtr<DataChannel> Get(uint16_t aId) const;
    ChannelArray GetAll() const {
      MutexAutoLock lock(mMutex);
      return mChannels.Clone();
    }
    RefPtr<DataChannel> GetNextChannel(uint16_t aCurrentId) const;

   private:
    struct IdComparator {
      bool Equals(const RefPtr<DataChannel>& aChannel, uint16_t aId) const;
      bool LessThan(const RefPtr<DataChannel>& aChannel, uint16_t aId) const;
      bool Equals(const RefPtr<DataChannel>& a1,
                  const RefPtr<DataChannel>& a2) const;
      bool LessThan(const RefPtr<DataChannel>& a1,
                    const RefPtr<DataChannel>& a2) const;
    };
    mutable Mutex mMutex{"DataChannelConnection::Channels::mMutex"};
    ChannelArray mChannels MOZ_GUARDED_BY(mMutex);
  };

  DataChannelConnection(DataConnectionListener* aListener,
                        nsISerialEventTarget* aTarget,
                        MediaTransportHandler* aHandler);

  void SendDataMessage(DataChannel& aChannel, nsACString&& aMsg,
                       bool aIsBinary);

  DataChannelConnectionState GetState() const {
    MOZ_ASSERT(mSTS->IsOnCurrentThread());
    return mState;
  }

  void SetState(DataChannelConnectionState aState);

  static void DTLSConnectThread(void* data);
  void SendPacket(std::unique_ptr<MediaPacket>&& packet);
  void OnPacketReceived(const std::string& aTransportId,
                        const MediaPacket& packet);
  already_AddRefed<DataChannel> FindChannelByStream(uint16_t stream);
  uint16_t FindFreeStream() const;
  int SendControlMessage(DataChannel& aChannel, const uint8_t* data,
                         uint32_t len);
  int SendOpenAckMessage(DataChannel& aChannel);
  int SendOpenRequestMessage(DataChannel& aChannel);

  void OpenFinish(RefPtr<DataChannel> aChannel);

  void HandleUnknownMessage(uint32_t ppid, uint32_t length, uint16_t stream);
  void HandleOpenRequestMessage(
      const struct rtcweb_datachannel_open_request* req, uint32_t length,
      uint16_t stream);
  void HandleOpenAckMessage(const struct rtcweb_datachannel_ack* ack,
                            uint32_t length, uint16_t stream);
  bool ReassembleMessageChunk(IncomingMsg& aReassembled, const void* buffer,
                              size_t length, uint32_t ppid, uint16_t stream);

  /******************** Mainthread only **********************/
  // Avoid cycles with PeerConnectionImpl
  // Use from main thread only as WeakPtr is not threadsafe
  WeakPtr<DataConnectionListener> mListener;
  uint64_t mMaxMessageSize = WEBRTC_DATACHANNEL_MAX_MESSAGE_SIZE_REMOTE_DEFAULT;
  nsTArray<uint16_t> mStreamIds;
  Maybe<bool> mAllocateEven;
  nsCOMPtr<nsIThread> mInternalIOThread = nullptr;
  /***********************************************************/

  /*********************** STS only **************************/
  std::set<RefPtr<DataChannel>> mPending;
  uint16_t mNegotiatedIdLimit = 0;
  PendingType mPendingType = PendingType::None;
  std::string mTransportId;
  bool mConnectedToTransportHandler = false;
  RefPtr<MediaTransportHandler> mTransportHandler;
  MediaEventListener mPacketReceivedListener;
  MediaEventListener mStateChangeListener;
  DataChannelConnectionState mState = DataChannelConnectionState::Closed;
  /***********************************************************/

  // NOTE: while this container will auto-expand, increases in the number of
  // channels available from the stack must be negotiated!
  // Accessed from both main and sts, API is threadsafe
  Channels mChannels;

  // Set once on main in Init, invariant thereafter
  uintptr_t mId = 0;

  // Set once on main in ConnectToTransport, and read only (STS) thereafter.
  // Nothing should be using these before that first ConnectToTransport call.
  uint16_t mLocalPort = 0;
  uint16_t mRemotePort = 0;

  nsCOMPtr<nsISerialEventTarget> mSTS;

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  bool mShutdown = false;
#endif
};

class DataChannel {
  friend class DataChannelConnection;
  friend class DataChannelConnectionUsrsctp;

 public:
  DataChannel(DataChannelConnection* connection, uint16_t stream,
              const nsACString& label, const nsACString& protocol,
              DataChannelReliabilityPolicy policy, uint32_t value, bool ordered,
              bool negotiated);
  DataChannel(const DataChannel&) = delete;
  DataChannel(DataChannel&&) = delete;
  DataChannel& operator=(const DataChannel&) = delete;
  DataChannel& operator=(DataChannel&&) = delete;

 private:
  ~DataChannel();

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(DataChannel)

  // The transfer dance here is somewhat complex because we can be dispatching
  // events while the transfer is in progress, and even before we know whether a
  // transfer might occur.
  //
  // Called when the mainthread RTCDataChannel is created
  void SetMainthreadDomDataChannel(dom::RTCDataChannel* aChannel);

  // Called when the mainthread RTCDataChannel has started the transfer steps.
  // This is important, because it means that certain updates (particularly
  // things like stream id and max message size) that are ordinarily set
  // synchronously should now be dispatched.
  void OnWorkerTransferStarted();

  // Called when the worker thread RTCDataChannel is created. This is where we
  // learn about the worker thread, and we can start dispatching events to it.
  void OnWorkerTransferComplete(dom::RTCDataChannel* aChannel);

  // Called when the window of opportunity to start a transfer has closed. This
  // is where we know that main is our event target, and we can start
  // dispatching events to it.
  void OnWorkerTransferDisabled();

  // Unsets our (weak) ref to the mainthread RTCDataChannel. If we *also* have a
  // worker-thread RTCDataChannel, we must notify it about this, because it
  // means that there's one less reason for it to keep a self-ref.
  void UnsetMainthreadDomDataChannel();

  // Unsets our (weak) ref to a worker thread RTCDataChannel, if one exists.
  void UnsetWorkerDomDataChannel();

  // Helper for send methods that converts POSIX error codes to an ErrorResult.
  static void SendErrnoToErrorResult(int error, size_t aMessageSize,
                                     ErrorResult& aRv);

  // Send a string
  void SendMsg(nsCString&& aMsg);

  // Send a binary message (TypedArray)
  void SendBinaryMsg(nsCString&& aMsg);

  // Send a binary blob
  void SendBinaryBlob(nsIInputStream* aBlob);

  void DecrementBufferedAmount(size_t aSize);
  void AnnounceOpen();
  void AnnounceClosed(Maybe<dom::RTCErrorParams> aError);
  void GracefulClose();

  Maybe<uint16_t> GetStream() const {
    MOZ_ASSERT(NS_IsMainThread());
    if (mStream == INVALID_STREAM) {
      return Nothing();
    }
    return Some(mStream);
  }

  void SetStream(uint16_t aId);
  void SetMaxMessageSize(double aMaxMessageSize);
  double GetMaxMessageSize() const { return mConnection->GetMaxMessageSize(); }

  void OnMessageReceived(nsCString&& aMsg, bool aIsBinary);

  RefPtr<DataChannelStatsPromise> GetStats(
      const DOMHighResTimeStamp aTimestamp);

  // Called when there will be no more data sent
  void EndOfStream();

  RefPtr<dom::RTCDataChannel> GetDomDataChannel() const;

 private:
  nsresult AddDataToBinaryMsg(const char* data, uint32_t size);
  void SendBuffer(nsCString&& aMsg, bool aBinary);
  void UnsetMessagesSentPromiseWhenSettled();

  const nsCString mLabel;
  const nsCString mProtocol;
  const DataChannelReliabilityPolicy mPrPolicy;
  const uint32_t mPrValue;
  const bool mNegotiated;
  const bool mOrdered;

  // DOM Thread only; wherever the RTCDataChannel lives.
  dom::RTCDataChannel* mMainthreadDomDataChannel = nullptr;
  bool mHasWorkerDomDataChannel = false;
  bool mEverOpened = false;
  bool mAnnouncedOpen = false;
  bool mAnnouncedClosed = false;
  uint16_t mStream;
  RefPtr<GenericNonExclusivePromise> mMessagesSentPromise;
  RefPtr<DataChannelConnection> mConnection;

  // STS only
  // The channel has been opened, but the peer has not yet acked - ensures that
  // the messages are sent ordered until this is cleared.
  bool mWaitingForAck = false;
  bool mSendStreamNeedsReset = false;
  bool mRecvStreamNeedsReset = false;
  bool mEndOfStreamCalled = false;
  nsTArray<OutgoingMsg> mBufferedData;
  std::map<uint16_t, IncomingMsg> mRecvBuffers;

  // At first, this is not hooked to any real event target, and just buffers
  // events. Later on, when we know what event target we should use, we hook it
  // in here, this dispatches the buffered events, and then acts as a
  // passthrough.
  const RefPtr<StopGapEventTarget> mDomEventTarget;

  // Worker thread only. We keep this separately because the spec requires it to
  // have a strong ref (from the worker global scope) as long as the *original*
  // mainthread RTCDataChannel is still alive. When the mainthread
  // RTCDataChannel goes away, we will notice, and then let the worker
  // RTCDataChannel know about it.
  dom::RTCDataChannel* mWorkerDomDataChannel = nullptr;
};

static constexpr const char* ToString(DataChannelConnectionState state) {
  switch (state) {
    case DataChannelConnectionState::Connecting:
      return "CONNECTING";
    case DataChannelConnectionState::Open:
      return "OPEN";
    case DataChannelConnectionState::Closed:
      return "CLOSED";
  }
  return "";
};

static constexpr const char* ToString(DataChannelConnection::PendingType type) {
  switch (type) {
    case DataChannelConnection::PendingType::None:
      return "NONE";
    case DataChannelConnection::PendingType::Dcep:
      return "DCEP";
    case DataChannelConnection::PendingType::Data:
      return "DATA";
  }
  return "";
};

static constexpr const char* ToString(DataChannelReliabilityPolicy type) {
  switch (type) {
    case DataChannelReliabilityPolicy::Reliable:
      return "RELIABLE";
    case DataChannelReliabilityPolicy::LimitedRetransmissions:
      return "LIMITED_RETRANSMISSIONS";
    case DataChannelReliabilityPolicy::LimitedLifetime:
      return "LIMITED_LIFETIME";
  }
  return "";
};

}  // namespace mozilla

#endif  // NETWERK_SCTP_DATACHANNEL_DATACHANNEL_H_
