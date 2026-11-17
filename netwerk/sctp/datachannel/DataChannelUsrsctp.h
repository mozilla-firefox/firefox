/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NETWERK_SCTP_DATACHANNEL_DATACHANNELUSRSCTP_H_
#define NETWERK_SCTP_DATACHANNEL_DATACHANNELUSRSCTP_H_

#include "DataChannel.h"

extern "C" {
struct socket;
struct sctp_rcvinfo;
}

#ifndef EALREADY
#  define EALREADY WSAEALREADY
#endif

namespace mozilla {

// for queuing incoming data messages before the Open or
// external negotiation is indicated to us
class QueuedDataMessage {
 public:
  QueuedDataMessage(uint16_t stream, uint32_t ppid, uint16_t messageId,
                    int flags, const uint8_t* data, uint32_t length)
      : mStream(stream),
        mPpid(ppid),
        mMessageId(messageId),
        mFlags(flags),
        mData(data, length) {}

  const uint16_t mStream;
  const uint32_t mPpid;
  const uint16_t mMessageId;
  const int mFlags;
  const nsTArray<uint8_t> mData;
};

class DataChannelConnectionUsrsctp : public DataChannelConnection {
  virtual ~DataChannelConnectionUsrsctp();

 public:
  DataChannelConnectionUsrsctp(DataConnectionListener* aListener,
                               nsISerialEventTarget* aTarget,
                               MediaTransportHandler* aHandler);
  void Destroy() override;
  bool RaiseStreamLimitTo(uint16_t aNewLimit) override;
  // usrsctp grows the stream count on demand toward this fixed ceiling.
  uint16_t GetStreamIdCeiling() const override { return MAX_NUM_STREAMS; }
  void OnTransportReady() override;
  bool Init(const uint16_t aLocalPort, const uint16_t aNumStreams) override;
  int SendMessage(DataChannel& aChannel, OutgoingMsg&& aMsg) override;
  void OnSctpPacketReceived(const MediaPacket& packet) override;
  bool ResetStreams(nsTArray<uint16_t>& aStreams) override;
  void OnStreamOpen(uint16_t stream) override;

  // Called on data reception from the SCTP library
  // must(?) be public so my c->c++ trampoline can call it
  // May be called with (STS thread) or without the lock
  int ReceiveCallback(struct socket* sock, void* data, size_t datalen,
                      struct sctp_rcvinfo rcv, int flags);
  int SendSctpPacket(const uint8_t* buffer, size_t length);

 private:
  void HandleAssociationChangeEvent(const struct sctp_assoc_change* sac);
  void HandlePeerAddressChangeEvent(const struct sctp_paddr_change* spc);
  void HandleRemoteErrorEvent(const struct sctp_remote_error* sre);
  void HandleShutdownEvent(const struct sctp_shutdown_event* sse);
  void HandleAdaptationIndication(const struct sctp_adaptation_event* sai);
  void HandlePartialDeliveryEvent(const struct sctp_pdapi_event* spde);
  void HandleSendFailedEvent(const struct sctp_send_failed_event* ssfe);
  void HandleStreamResetEvent(const struct sctp_stream_reset_event* strrst);
  void HandleStreamChangeEvent(const struct sctp_stream_change_event* strchg);
  void HandleNotification(const union sctp_notification* notif, size_t n);
  int SendMsgInternal(OutgoingMsg& msg, size_t* aWritten);
  bool SendBufferedMessages(nsTArray<OutgoingMsg>& buffer, size_t* aWritten);
  void SendDeferredMessages();
  static int OnThresholdEvent(struct socket* sock, uint32_t sb_free,
                              void* ulp_info);
  int SendMsgInternalOrBuffer(nsTArray<OutgoingMsg>& buffer, OutgoingMsg&& msg,
                              bool* aBuffered, size_t* aWritten);
  uint32_t UpdateCurrentStreamIndex();
  uint32_t GetCurrentStreamIndex();
  // Finish Destroy on STS to avoid SCTP race condition with ABORT from far end
  void DestroyOnSTS();
  void HandleMessageChunk(const void* buffer, size_t length, uint32_t ppid,
                          uint16_t messageId, uint16_t stream, int flags);
  void HandleDataMessageChunk(const void* data, size_t length, uint32_t ppid,
                              uint16_t stream, uint16_t messageId, int flags);
  void HandleDCEPMessageChunk(const void* buffer, size_t length, uint32_t ppid,
                              uint16_t stream, int flags);
  bool HasQueuedData(uint16_t aStream) const;

  // All STS only
  bool mSendInterleaved = false;
  // Keeps track of whose turn it is in the round robin
  uint32_t mCurrentStream = 0;
  PendingType mPendingType = PendingType::None;
  // holds outgoing control messages if usrsctp is not ready to send them
  nsTArray<OutgoingMsg> mBufferedControl;
  // For partial DCEP messages (should be _really_ rare, since they're small)
  Maybe<IncomingMsg> mRecvBuffer;
  // holds data that's come in before a channel is open
  nsTArray<UniquePtr<QueuedDataMessage>> mQueuedData;
  // accessed from STS thread
  // Set once on main in Init, STS-only thereafter
  struct socket* mSocket = nullptr;
  bool mSctpConfigured = false;
};

}  // namespace mozilla

#endif  // NETWERK_SCTP_DATACHANNEL_DATACHANNELUSRSCTP_H_
