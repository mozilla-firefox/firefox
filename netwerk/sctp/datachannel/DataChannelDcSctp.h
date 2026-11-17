/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NETWERK_SCTP_DATACHANNEL_DATACHANNELDCSCTP_H_
#define NETWERK_SCTP_DATACHANNEL_DATACHANNELDCSCTP_H_

#include "DataChannel.h"
#include "net/dcsctp/public/dcsctp_socket.h"
#include "net/dcsctp/public/dcsctp_socket_factory.h"

namespace mozilla {
using namespace dcsctp;

class DataChannelConnectionDcSctp : public DataChannelConnection,
                                    public DcSctpSocketCallbacks {
 public:
  DataChannelConnectionDcSctp(DataConnectionListener* aListener,
                              nsISerialEventTarget* aTarget,
                              MediaTransportHandler* aHandler);

  // DataChannelConnection API
  void Destroy() override;
  bool RaiseStreamLimitTo(uint16_t aNewLimit) override;
  uint16_t GetStreamIdCeiling() const override;
  void OnTransportReady() override;
  bool Init(const uint16_t aLocalPort, const uint16_t aNumStreams) override;
  int SendMessage(DataChannel& aChannel, OutgoingMsg&& aMsg) override;
  void OnSctpPacketReceived(const MediaPacket& aPacket) override;
  bool ResetStreams(nsTArray<uint16_t>& aStreams) override;
  // This is called after an ACK comes in, to prompt subclasses to deliver
  // anything they've buffered while awaiting the ACK.
  void OnStreamOpen(uint16_t aStream) override;

  // DcSctpSocketCallbacks API

  // Called when the library wants the packet serialized as `data` to be sent.
  //
  // Note that it's NOT ALLOWED to call into this library from within this
  // callback.
  SendPacketStatus SendPacketWithStatus(
      std::span<const uint8_t> aData) override;

  // Called when the library wants to create a Timeout. The callback must return
  // an object that implements that interface.
  //
  // Low precision tasks are scheduled more efficiently by using leeway to
  // reduce Idle Wake Ups and is the preferred precision whenever possible. High
  // precision timeouts do not have this leeway, but is still limited by OS
  // timer precision. At the time of writing, kLow's additional leeway may be up
  // to 17 ms, but please see webrtc::TaskQueueBase::DelayPrecision for
  // up-to-date information.
  //
  // Note that it's NOT ALLOWED to call into this library from within this
  // callback.
  std::unique_ptr<Timeout> CreateTimeout(
      webrtc::TaskQueueBase::DelayPrecision aPrecision) override;
  void HandleTimeout(TimeoutID aId);

  // Returns the current time (from any epoch).
  //
  // Note that it's NOT ALLOWED to call into this library from within this
  // callback.
  webrtc::Timestamp Now() override;

  // Called when the library needs a random number uniformly distributed between
  // `low` (inclusive) and `high` (exclusive). The random numbers used by the
  // library are not used for cryptographic purposes. There are no requirements
  // that the random number generator must be secure.
  //
  // Note that it's NOT ALLOWED to call into this library from within this
  // callback.
  uint32_t GetRandomInt(uint32_t aLow, uint32_t aHigh) override;

  // Called when the library has received an SCTP message in full and delivers
  // it to the upper layer.
  //
  // It is allowed to call into this library from within this callback.
  void OnMessageReceived(DcSctpMessage aMessage) override;

  // Triggered when an non-fatal error is reported by either this library or
  // from the other peer (by sending an ERROR command). These should be logged,
  // but no other action need to be taken as the association is still viable.
  //
  // It is allowed to call into this library from within this callback.
  void OnError(ErrorKind aError, absl::string_view aMessage) override;

  // Triggered when the socket has aborted - either as decided by this socket
  // due to e.g. too many retransmission attempts, or by the peer when
  // receiving an ABORT command. No other callbacks will be done after this
  // callback, unless reconnecting.
  //
  // It is allowed to call into this library from within this callback.
  void OnAborted(ErrorKind aError, absl::string_view aMessage) override;

  // Called when calling `Connect` succeeds, but also for incoming successful
  // connection attempts.
  //
  // It is allowed to call into this library from within this callback.
  void OnConnected() override;

  // Called when the socket is closed in a controlled way. No other
  // callbacks will be done after this callback, unless reconnecting.
  //
  // It is allowed to call into this library from within this callback.
  void OnClosed() override;

  // On connection restarted (by peer). This is just a notification, and the
  // association is expected to work fine after this call, but there could have
  // been packet loss as a result of restarting the association.
  //
  // It is allowed to call into this library from within this callback.
  void OnConnectionRestarted() override;

  // Indicates that a stream reset request has failed.
  //
  // It is allowed to call into this library from within this callback.
  void OnStreamsResetFailed(std::span<const StreamID> aOutgoingStreams,
                            absl::string_view aReason) override;

  // Indicates that a stream reset request has been performed.
  //
  // It is allowed to call into this library from within this callback.
  void OnStreamsResetPerformed(
      std::span<const StreamID> aOutgoingStreams) override;

  // When a peer has reset some of its outgoing streams, this will be called. An
  // empty list indicates that all streams have been reset.
  //
  // It is allowed to call into this library from within this callback.
  void OnIncomingStreamsReset(
      std::span<const StreamID> aIncomingStreams) override;

  // Will be called when the amount of data buffered to be sent falls to or
  // below the threshold set when calling `SetBufferedAmountLowThreshold`.
  //
  // It is allowed to call into this library from within this callback.
  void OnBufferedAmountLow(StreamID aStreamId) override;

  void OnLifecycleMessageFullySent(LifecycleId aLifecycleId) override;
  void OnLifecycleMessageExpired(LifecycleId aLifecycleId,
                                 bool aMaybeDelivered) override;

 private:
  void UpdateBufferedAmount(StreamID aStreamId);
  void OnDCEPMessageDone(LifecycleId aLifecycleId);

  bool HasPreChannelData(uint16_t aStream) const;

  std::unique_ptr<DcSctpSocketInterface> mDcSctp;
  std::set<uint16_t> mStreamsAwaitingAck;

  // dcsctp counts DCEP payloads as part of bufferedAmount and bufferedamountlow
  // This is wrong. dcsctp does not make it easy to tell whether any DCEP has
  // been sent when bufferedAmount decreases. We can set bufferedAmount
  // thresholds to detect when any data is sent, but those callbacks don't tell
  // us whether that data was DCEP or not. We can also monitor the lifecycle of
  // packets, but we will not be able to detect when a large packet is partially
  // sent. We need to combine these approaches to figure out how much actual
  // data is buffered. We take advantage of a couple of things:
  //
  // 1. DCEP messages are small enough that partial sends will not happen,
  // meaning that we can expect OnLifecycleMessageFullySent to accurately
  // reflect how much DCEP has just been sent.
  // 2. OnBufferedAmountLow and OnLifecycleMessageFullySent are called in the
  // same task when data is sent.
  //
  // The basic idea is to track the total of both DCEP and data bytes using the
  // OnBufferedAmountLow callback, and subtract the DCEP bytes if we see
  // OnLifecycleMessageFullySent callback/s for the DCEP messages. This
  // subtraction is done in a dispatched task; inside of that task we will not
  // have cases where OnBufferedAmountLow has fired, but the corresponding
  // OnLifecycleMessageFullySent (if any) have not.

  std::map<uint16_t, size_t> mBufferedAmounts;
  std::map<uint16_t, int> mDCEPBytesSent;
  uint64_t mNextLifecycleId = 1;
  // lifecycle-id -> (stream-id, amount)
  std::map<uint64_t, std::pair<uint16_t, size_t>> mBufferedDCEPBytes;
  // Holding tank for messages whose channel has not been created yet.
  std::vector<IncomingMsg> mPreChannelData;
};

}  // namespace mozilla

#endif  // NETWERK_SCTP_DATACHANNEL_DATACHANNELDCSCTP_H_
