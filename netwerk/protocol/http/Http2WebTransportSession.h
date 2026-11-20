/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_Http2WebTransportSession_h
#define mozilla_net_Http2WebTransportSession_h

#include "CapsuleParser.h"
#include "Http2StreamTunnel.h"
#include "WebTransportFlowControl.h"
#include "WebTransportSessionBase.h"
#include "WebTransportStreamBase.h"
#include "mozilla/Queue.h"
#include "mozilla/UniquePtr.h"
#include "nsHashKeys.h"
#include "nsRefPtrHashtable.h"
#include "nsTHashMap.h"

namespace mozilla::net {

class CapsuleEncoder;
class Http2WebTransportStream;

// A handler used exclusively by Http2WebTransportSessionImpl for capsule I/O,
// primarily responsible for sending capsules.
class CapsuleIOHandler {
 public:
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  virtual void HasCapsuleToSend() = 0;
  virtual void SetSentFin() = 0;
  virtual void StartReading() = 0;
  virtual void OnCapsuleParseFailure(nsresult aError) = 0;

 protected:
  virtual ~CapsuleIOHandler() = default;
};

struct Http2WebTransportInitialSettings {
  // Initial session-level data limit.
  uint32_t mInitialMaxData = 0;
  // Initial stream-level data limit for outgoing unidirectional streams.
  uint32_t mInitialMaxStreamDataUni = 0;
  // Initial stream-level data limit for outgoing bidirectional streams.
  uint32_t mInitialMaxStreamDataBidi = 0;
  // Initial max unidirectional streams per session.
  uint32_t mInitialMaxStreamsUni = 0;
  // Initial max bidirectional streams per session.
  uint32_t mInitialMaxStreamsBidi = 0;
  // Initial limit on unidirectional streams that the peer creates.
  uint32_t mInitialLocalMaxStreamsUnidi = 16;
  // Initial limit on bidirectional streams that the peer creates.
  uint32_t mInitialLocalMaxStreamsBidi = 16;
  // Initial flow control limit for receiving data on unidirectional streams
  // that the peer creates.
  uint32_t mInitialLocalMaxStreamDataUnidi = 0x100000;
  // Initial flow control limit for receiving data on bidirectional streams
  // that the peer creates.
  uint32_t mInitialLocalMaxStreamDataBidi = 0x100000;
  // Initial session-level flow control limit.
  uint64_t mInitialLocalMaxData = 0x3FFFFFFFFFFFFFFF;
};

enum class CapsuleTransmissionPriority : uint8_t {
  Critical = 0,
  Important = 1,
  High = 2,
  Normal = 3,
  Low = 4,
};

// Core implementation of the logic behind Http2WebTransportSession.
// It's designed to be independently instantiated, which makes it easier to
// test.
class Http2WebTransportSessionImpl final : public WebTransportSessionBase,
                                           public CapsuleParser::Listener {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(Http2WebTransportSessionImpl, override)

  explicit Http2WebTransportSessionImpl(
      CapsuleIOHandler* aHandler, Http2WebTransportInitialSettings aSettings);

  void CloseSession(uint32_t aStatus, const nsACString& aReason) override;
  uint64_t GetStreamId() const override;
  void GetMaxDatagramSize() override;
  void GetStats() override;
  void SendDatagram(nsTArray<uint8_t>&& aData, uint64_t aTrackingId,
                    uint64_t aSendGroupId, int64_t aSendOrder,
                    TimeStamp aTimestamp) override;
  nsresult ExportKeyingMaterial(const nsTArray<uint8_t>& aLabel,
                                const nsTArray<uint8_t>& aContext,
                                uint32_t aOutputLength,
                                nsTArray<uint8_t>& aKeyingMaterial) override;
  void GetNegotiatedProtocol(nsACString& aProtocol) override;
  void SetAnticipatedConcurrentIncomingUnidirectionalStreams(
      uint16_t aValue) override;
  void SetAnticipatedConcurrentIncomingBidirectionalStreams(
      uint16_t aValue) override;
  void SetDatagramMaxBufferedDatagrams(uint32_t aMaxBufferedDatagrams) override;
  void SetDatagramMaxAge(double aMaxAge) override;
  nsresult RegisterSendGroup(uint64_t aGroupId) override;
  void CreateOutgoingBidirectionalStream(
      std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>&&
          aCallback) override;
  void CreateOutgoingUnidirectionalStream(
      std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>&&
          aCallback) override;
  bool OnCapsule(Capsule&& aCapsule) override;
  void OnCapsuleParseFailure(nsresult aError) override;
  void StartReading() override;
  void Close(nsresult aReason);

  void OnStreamClosed(Http2WebTransportStream* aStream);
  void PrepareCapsulesToSend(
      mozilla::Queue<UniquePtr<CapsuleEncoder>>& aOutput);
  SenderFlowControlSession& SessionDataFc() { return mSessionDataFc; }
  ReceiverFlowControlSession& ReceiverFc() { return mReceiverFc; }

  void StreamHasCapsuleToSend();
  void OnStreamDataSent(StreamId aId, size_t aCount);
  void OnError(uint64_t aError);

 private:
  virtual ~Http2WebTransportSessionImpl();

  void CreateOutgoingStreamInternal(
      StreamId aStreamId,
      std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>&&
          aCallback);

  class PendingStreamCallback {
   public:
    explicit PendingStreamCallback(
        std::function<void(
            Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>&& aCallback)
        : mCallback(std::move(aCallback)) {}
    std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>
    TakeCallback() {
      return std::move(mCallback);
    }

   private:
    std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>
        mCallback;
  };
  void ProcessPendingStreamCallbacks(
      mozilla::Queue<UniquePtr<PendingStreamCallback>>& aCallbacks,
      WebTransportStreamType aStreamType);
  bool ProcessIncomingStreamCapsule(Capsule&& aCapsule, StreamId aID,
                                    WebTransportStreamType aStreamType);
  void SendMaintenanceCapsules(CapsuleTransmissionPriority aPriority);
  already_AddRefed<Http2WebTransportStream> GetStream(StreamId aId);
  bool HandleMaxStreamDataCapsule(StreamId aId, Capsule&& aCapsule);
  bool HandleStreamStopSendingCapsule(StreamId aId, Capsule&& aCapsule);
  bool HandleStreamResetCapsule(StreamId aId, Capsule&& aCapsule);

  class CapsuleQueue final {
   public:
    CapsuleQueue();
    mozilla::Queue<UniquePtr<CapsuleEncoder>>& operator[](
        CapsuleTransmissionPriority aPriority);

   private:
    mozilla::Queue<UniquePtr<CapsuleEncoder>> mCritical;
    mozilla::Queue<UniquePtr<CapsuleEncoder>> mImportant;
    mozilla::Queue<UniquePtr<CapsuleEncoder>> mHigh;
    mozilla::Queue<UniquePtr<CapsuleEncoder>> mNormal;
    mozilla::Queue<UniquePtr<CapsuleEncoder>> mLow;
  };
  void EnqueueOutCapsule(CapsuleTransmissionPriority aPriority,
                         UniquePtr<CapsuleEncoder>&& aData);

  uint64_t mStreamId = 0;
  nsRefPtrHashtable<nsUint64HashKey, Http2WebTransportStream> mOutgoingStreams;
  nsRefPtrHashtable<nsUint64HashKey, Http2WebTransportStream> mIncomingStreams;

  mozilla::Queue<UniquePtr<PendingStreamCallback>> mBidiPendingStreamCallbacks;
  mozilla::Queue<UniquePtr<PendingStreamCallback>> mUnidiPendingStreamCallbacks;
  Http2WebTransportInitialSettings mSettings;
  LocalStreamLimits mLocalStreamsFlowControl;
  RemoteStreamLimits mRemoteStreamsFlowControl;

  RefPtr<CapsuleIOHandler> mHandler;
  CapsuleQueue mCapsuleQueue;
  SenderFlowControlSession mSessionDataFc;
  ReceiverFlowControlSession mReceiverFc;
};

class Http2WebTransportSession final : public Http2StreamTunnel,
                                       public nsIOutputStreamCallback,
                                       public nsIInputStreamCallback,
                                       public CapsuleIOHandler {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIOUTPUTSTREAMCALLBACK
  NS_DECL_NSIINPUTSTREAMCALLBACK

  Http2WebTransportSession(Http2Session* aSession, int32_t aPriority,
                           uint64_t aBcId,
                           nsHttpConnectionInfo* aConnectionInfo,
                           Http2WebTransportInitialSettings aSettings);
  Http2WebTransportSession* GetHttp2WebTransportSession() override {
    return this;
  }
  Http2WebTransportSessionImpl* GetHttp2WebTransportSessionImpl() {
    return mImpl;
  }

  void CloseStream(nsresult aReason) override;
  void HasCapsuleToSend() override;
  void SetSentFin() override;
  void StartReading() override;
  void OnCapsuleParseFailure(nsresult aError) override;

 private:
  virtual ~Http2WebTransportSession();
  nsresult GenerateHeaders(nsCString& aCompressedData,
                           uint8_t& aFirstFrameFlags) override;

  size_t mWriteOffset{0};
  mozilla::Queue<UniquePtr<CapsuleEncoder>> mOutgoingQueue;
  RefPtr<Http2WebTransportSessionImpl> mImpl;
  UniquePtr<CapsuleParser> mCapsuleParser;
  UniquePtr<CapsuleEncoder> mCurrentOutCapsule;
};

}  // namespace mozilla::net

#endif  // mozilla_net_Http2WebTransportSession_h
