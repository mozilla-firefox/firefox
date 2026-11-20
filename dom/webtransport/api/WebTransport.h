/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_WEBTRANSPORT_API_WEBTRANSPORT_H_
#define DOM_WEBTRANSPORT_API_WEBTRANSPORT_H_

#include "mozilla/dom/BufferSourceBindingFwd.h"
#include "mozilla/dom/PWebTransport.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/WebTransportBinding.h"
#include "mozilla/dom/WebTransportChild.h"
#include "mozilla/dom/WebTransportReceiveStream.h"
#include "mozilla/dom/WebTransportSendGroup.h"
#include "mozilla/dom/WebTransportSendStream.h"
#include "mozilla/dom/WebTransportStreams.h"
#include "mozilla/ipc/DataPipe.h"
#include "mozilla/net/WebTransportEventService.h"
#include "nsCOMPtr.h"
#include "nsISupports.h"
#include "nsPIDOMWindow.h"
#include "nsTArray.h"
#include "nsTHashMap.h"
#include "nsWrapperCache.h"

namespace mozilla::dom {

class Headers;
class WebTransportError;
class WebTransportDatagramDuplexStream;
class WebTransportIncomingStreamsAlgorithms;
class WebTransportSendGroup;
class ReadableStream;
class WritableStream;
using BidirectionalPair = std::pair<RefPtr<mozilla::ipc::DataPipeReceiver>,
                                    RefPtr<mozilla::ipc::DataPipeSender>>;

struct DatagramEntry {
  DatagramEntry(nsTArray<uint8_t>&& aData, const mozilla::TimeStamp& aTimeStamp)
      : mBuffer(std::move(aData)), mTimeStamp(aTimeStamp) {}
  DatagramEntry(Span<uint8_t>& aData, const mozilla::TimeStamp& aTimeStamp)
      : mBuffer(aData), mTimeStamp(aTimeStamp) {}

  nsTArray<uint8_t> mBuffer;
  mozilla::TimeStamp mTimeStamp;
};

class WebTransport final : public nsISupports, public nsWrapperCache {
  friend class WebTransportIncomingStreamsAlgorithms;
  // For mSendStreams/mReceiveStreams
  friend class WebTransportSendStream;
  friend class WebTransportReceiveStream;
  // For mState access in CreateWritable
  friend class WebTransportDatagramDuplexStream;
  // For mWebTransport access in CreateWritable
  friend class WebTransportSendGroup;

 public:
  explicit WebTransport(nsIGlobalObject* aGlobal);

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(WebTransport)

  enum class WebTransportState { CONNECTING, CONNECTED, CLOSED, FAILED };

  static void NotifyBFCacheOnMainThread(nsPIDOMWindowInner* aInner,
                                        bool aCreated);
  void NotifyToWindow(bool aCreated) const;

  void Init(const GlobalObject& aGlobal, const nsAString& aUrl,
            const WebTransportOptions& aOptions, ErrorResult& aError);
  void ResolveWaitingConnection(WebTransportReliabilityMode aReliability);
  void RejectWaitingConnection(nsresult aRv);
  void ResolveDraining();
  bool ParseURL(const nsAString& aURL) const;
  // this calls CloseNative(), which doesn't actually run script.   See bug
  // 1810942
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void Cleanup(
      WebTransportError* aError, const WebTransportCloseInfo* aCloseInfo,
      ErrorResult& aRv);

  // From Parent
  void NewBidirectionalStream(
      uint64_t aStreamId,
      const RefPtr<mozilla::ipc::DataPipeReceiver>& aIncoming,
      const RefPtr<mozilla::ipc::DataPipeSender>& aOutgoing);

  void NewUnidirectionalStream(
      uint64_t aStreamId,
      const RefPtr<mozilla::ipc::DataPipeReceiver>& aStream);

  void NewDatagramReceived(nsTArray<uint8_t>&& aData,
                           const mozilla::TimeStamp& aTimeStamp);

  void OnDatagramReady(uint64_t aDatagramId);

  void RemoteClosed(bool aCleanly, const uint32_t& aCode,
                    const nsACString& aReason,
                    const Maybe<WebTransportStatsData>& aStats);

  void SetNegotiatedProtocol(const nsACString& aProtocol);
  void SetResponseHeaders(
      nsTArray<mozilla::ipc::WebTransportHeader>&& aHeaders);

  // WebIDL
  Headers* GetResponseHeaders() { return mResponseHeaders; }

  void OnStreamResetOrStopSending(uint64_t aStreamId,
                                  const StreamResetOrStopSendingError& aError);
  // WebIDL Boilerplate
  nsIGlobalObject* GetParentObject() const;

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  // WebIDL Interface
  static already_AddRefed<WebTransport> Constructor(
      const GlobalObject& aGlobal, const nsAString& aUrl,
      const WebTransportOptions& aOptions, ErrorResult& aError);

  already_AddRefed<Promise> GetStats(ErrorResult& aError);
  already_AddRefed<Promise> ExportKeyingMaterial(const BufferSource& aLabel,
                                                 const BufferSource& aContext,
                                                 uint32_t aOutputLength,
                                                 ErrorResult& aError);

  already_AddRefed<Promise> Ready() { return do_AddRef(mReady); }
  WebTransportReliabilityMode Reliability();
  WebTransportCongestionControl CongestionControl();
  static bool SupportsReliableOnly(const GlobalObject& aGlobal);
  void GetProtocol(nsAString& aProtocol);
  Nullable<uint16_t> GetAnticipatedConcurrentIncomingUnidirectionalStreams();
  void SetAnticipatedConcurrentIncomingUnidirectionalStreams(
      const Nullable<uint16_t>& aValue);
  Nullable<uint16_t> GetAnticipatedConcurrentIncomingBidirectionalStreams();
  void SetAnticipatedConcurrentIncomingBidirectionalStreams(
      const Nullable<uint16_t>& aValue);
  already_AddRefed<Promise> Draining() { return do_AddRef(mDraining); }
  already_AddRefed<Promise> Closed() { return do_AddRef(mClosed); }
  MOZ_CAN_RUN_SCRIPT void Close(const WebTransportCloseInfo& aOptions,
                                ErrorResult& aRv);
  already_AddRefed<WebTransportDatagramDuplexStream> GetDatagrams(
      ErrorResult& aRv);
  already_AddRefed<Promise> CreateBidirectionalStream(
      const WebTransportSendStreamOptions& aOptions, ErrorResult& aRv);
  already_AddRefed<Promise> CreateUnidirectionalStream(
      const WebTransportSendStreamOptions& aOptions, ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY already_AddRefed<ReadableStream>
  IncomingBidirectionalStreams();
  MOZ_CAN_RUN_SCRIPT_BOUNDARY already_AddRefed<ReadableStream>
  IncomingUnidirectionalStreams();

  already_AddRefed<WebTransportSendGroup> CreateSendGroup(ErrorResult& aRv);

  void SendSetSendOrder(uint64_t aStreamId, int64_t aSendOrder);
  void SendSetSendGroup(uint64_t aStreamId, uint64_t aGroupId);

  void Shutdown() {}

 private:
  ~WebTransport();

  template <typename Stream>
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void PropagateError(Stream* aStream,
                                                  WebTransportError* aError);

  // Internal helpers for creating streams with sendGroup and sendOrder
  already_AddRefed<Promise> CreateBidirectionalStreamInternal(
      const WebTransportSendStreamOptions& aOptions,
      WebTransportSendGroup* aSendGroup, int64_t aSendOrder, ErrorResult& aRv);
  already_AddRefed<Promise> CreateUnidirectionalStreamInternal(
      const WebTransportSendStreamOptions& aOptions,
      WebTransportSendGroup* aSendGroup, int64_t aSendOrder, ErrorResult& aRv);

  // Issues the IPC GetStats request and wires the response to aPromise. Used
  // both immediately (when connected) and deferred until [[Ready]] settles
  // (when called during "connecting").
  void SendGetStatsRequest(Promise* aPromise);

  nsCOMPtr<nsIGlobalObject> mGlobal;
  // We are the owner of WebTransportChild.  We must call Shutdown() on it
  // before we're destroyed.
  RefPtr<WebTransportChild> mChild;

  // Spec in 5.8 says it can't be GC'd while CONNECTING or CONNECTED.  We won't
  // hold ref which we drop on CLOSED or FAILED because a reference is also held
  // by IPC.  We drop the IPC connection and by proxy the reference when it goes
  // to FAILED or CLOSED.

  // Spec-defined slots:
  // ordered sets, but we can't have duplicates, and this spec only appends.
  // Order is visible due to
  // https://w3c.github.io/webtransport/#webtransport-procedures step 10: "For
  // each sendStream in sendStreams, error sendStream with error."
  nsTHashMap<uint64_t, RefPtr<WebTransportSendStream>> mSendStreams;
  nsTHashMap<uint64_t, RefPtr<WebTransportReceiveStream>> mReceiveStreams;

  WebTransportState mState;
  RefPtr<Promise> mReady;
  RefPtr<Promise> mDraining;
  uint64_t mInnerWindowID = 0;
  uint64_t mHttpChannelID = 0;
  uint64_t mBrowsingContextID = 0;
  uint64_t mNextSendGroupId = 1;
  RefPtr<mozilla::net::WebTransportEventService> mService;
  // XXX may not need to be a RefPtr, since we own it through the Streams
  RefPtr<WebTransportIncomingStreamsAlgorithms> mIncomingBidirectionalAlgorithm;
  RefPtr<WebTransportIncomingStreamsAlgorithms>
      mIncomingUnidirectionalAlgorithm;
  WebTransportReliabilityMode mReliability;
  nsString mProtocol;
  RefPtr<Headers> mResponseHeaders;
  Nullable<uint16_t> mAnticipatedConcurrentIncomingUnidirectionalStreams;
  Nullable<uint16_t> mAnticipatedConcurrentIncomingBidirectionalStreams;
  // Incoming streams get queued here.  Use a TArray though it's working as
  // a FIFO - rarely will there be more than one entry in these arrays, so
  // the overhead of mozilla::Queue is unneeded
  nsTArray<std::tuple<uint64_t, RefPtr<mozilla::ipc::DataPipeReceiver>>>
      mUnidirectionalStreams;
  nsTArray<std::tuple<uint64_t, UniquePtr<BidirectionalPair>>>
      mBidirectionalStreams;

  // These are created in the constructor
  RefPtr<ReadableStream> mIncomingUnidirectionalStreams;
  RefPtr<ReadableStream> mIncomingBidirectionalStreams;
  RefPtr<WebTransportDatagramDuplexStream> mDatagrams;
  RefPtr<Promise> mClosed;

  Maybe<WebTransportStatsData> mCachedStats;
  uint64_t mCachedDroppedIncoming = 0;
  uint64_t mCachedExpiredIncoming = 0;

  // Close pending state and queued GetStats requests
  bool mClosePending = false;
  nsTArray<RefPtr<Promise>> mPendingGetStatsPromises;
};

}  // namespace mozilla::dom

#endif  // DOM_WEBTRANSPORT_API_WEBTRANSPORT_H_
