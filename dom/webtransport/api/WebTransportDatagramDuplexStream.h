/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_WEBTRANSPORT_API_WEBTRANSPORTDATAGRAMDUPLEXSTREAM_H_
#define DOM_WEBTRANSPORT_API_WEBTRANSPORTDATAGRAMDUPLEXSTREAM_H_

#include "mozilla/dom/DatagramQueueCharge.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ReadableStream.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/dom/WebTransport.h"
#include "mozilla/dom/WebTransportDatagramDuplexStreamBinding.h"
#include "mozilla/dom/WebTransportDatagramsWritable.h"
#include "mozilla/dom/WritableStream.h"
#include "nsCOMPtr.h"
#include "nsISupports.h"
#include "nsWrapperCache.h"

namespace mozilla::dom {

struct WebTransportSendOptions;

class IncomingDatagramStreamAlgorithms
    : public UnderlyingSourceAlgorithmsWrapper {
 public:
  explicit IncomingDatagramStreamAlgorithms(
      WebTransportDatagramDuplexStream* aDatagrams);

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(IncomingDatagramStreamAlgorithms,
                                           UnderlyingSourceAlgorithmsWrapper)

  MOZ_CAN_RUN_SCRIPT already_AddRefed<Promise> PullCallbackImpl(
      JSContext* aCx, ReadableStreamControllerBase& aController,
      ErrorResult& aRv) override;

  MOZ_CAN_RUN_SCRIPT void ReturnDatagram(JSContext* aCx, ErrorResult& aRv);

  void NotifyDatagramAvailable();

 protected:
  ~IncomingDatagramStreamAlgorithms() override;

 private:
  RefPtr<WebTransportDatagramDuplexStream> mDatagrams;
  RefPtr<Promise> mIncomingDatagramsPullPromise;
};

class EarlyDatagram final {
 public:
  EarlyDatagram(DatagramEntry* aDatagram, Promise* aPromise)
      : mDatagram(aDatagram), mWaitConnectPromise(aPromise) {}

  UniquePtr<DatagramEntry> mDatagram;
  RefPtr<Promise> mWaitConnectPromise;

  ~EarlyDatagram() = default;
};

class OutgoingDatagramStreamAlgorithms final
    : public UnderlyingSinkAlgorithmsWrapper {
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(OutgoingDatagramStreamAlgorithms,
                                           UnderlyingSinkAlgorithmsWrapper)

  explicit OutgoingDatagramStreamAlgorithms(
      WebTransportDatagramDuplexStream* aDatagrams,
      WebTransportSendGroup* aSendGroup = nullptr, int64_t aSendOrder = 0)
      : mDatagrams(aDatagrams),
        mSendGroup(aSendGroup),
        mSendOrder(aSendOrder) {}

  void SetChild(WebTransportChild* aChild);
  WebTransportChild* GetChild() const { return mChild; }

  void SetSendGroup(WebTransportSendGroup* aSendGroup) {
    mSendGroup = aSendGroup;
  }
  void SetSendOrder(int64_t aSendOrder) { mSendOrder = aSendOrder; }

  void SetOwnerWritable(WebTransportDatagramsWritable* aWritable) {
    mOwnerWritable = aWritable;
  }

  // Streams algorithms

  already_AddRefed<Promise> WriteCallbackImpl(
      JSContext* aCx, JS::Handle<JS::Value> aChunk,
      WritableStreamDefaultController& aController,
      ErrorResult& aError) override;

  // Resolves the oldest deferred promise if aAvailableBytes covers its
  // DatagramQueueCharge(), standing in for sendDatagrams steps 5.1.2.2/5.3.4.
  // On success, *aResolvedBytes is that datagram's size.
  bool TryResolveOldestFromCredit(uint64_t aAvailableBytes,
                                  size_t* aResolvedBytes);

  // Lowers the in-flight count by aCount (never below 0) as datagrams leave
  // neqo's queue, freeing writeDatagrams step 9 slots for new writes.  It
  // resolves nothing already deferred.
  void RetireInFlight(uint64_t aCount);

  // Send queue parameter updates to parent
  void UpdateQueueParams(uint32_t aMaxBufferedDatagrams, double aMaxAge);

  // Resolves this writable's withheld write promise, if any, so the
  // WritableStream is not left waiting on it once the session is torn down.
  // See WebTransportDatagramDuplexStream::Cleanup().
  void Cleanup();

 private:
  ~OutgoingDatagramStreamAlgorithms() override = default;

  struct QueuedDatagram {
    UniquePtr<DatagramEntry> mEntry;
    RefPtr<Promise> mPromise;

    QueuedDatagram(UniquePtr<DatagramEntry>&& aEntry, RefPtr<Promise> aPromise)
        : mEntry(std::move(aEntry)), mPromise(std::move(aPromise)) {}
  };

  struct PendingDatagram {
    RefPtr<Promise> mPromise;
    size_t mBytes;
  };

  // So nsTArray<QueuedDatagram>/nsTArray<PendingDatagram>'s generic
  // cycle-collection support (see nsTArray.h) can reach mPromise via the
  // overloads below, which need to name these otherwise-private types.
  friend void ImplCycleCollectionTraverse(nsCycleCollectionTraversalCallback&,
                                          QueuedDatagram&, const char*,
                                          uint32_t);
  friend void ImplCycleCollectionUnlink(QueuedDatagram&);
  friend void ImplCycleCollectionTraverse(nsCycleCollectionTraversalCallback&,
                                          PendingDatagram&, const char*,
                                          uint32_t);
  friend void ImplCycleCollectionUnlink(PendingDatagram&);

  RefPtr<WebTransportDatagramDuplexStream> mDatagrams;
  RefPtr<WebTransportChild> mChild;
  RefPtr<WebTransportDatagramsWritable> mOwnerWritable;
  // Queue for datagrams sent before connection is ready
  nsTArray<QueuedDatagram> mWaitConnectQueue;

  // Send group and order for this writable stream (null/0 for default)
  RefPtr<WebTransportSendGroup> mSendGroup;
  int64_t mSendOrder;

  // Oldest first, the write() promises step 9 left unresolved; settled only
  // by TryResolveOldestFromCredit() or Cleanup().
  nsTArray<PendingDatagram> mPendingDatagramPromises;

  // Stands in for the length of this writable's [[OutgoingDatagramsQueue]],
  // which lives in the parent: datagrams handed over, resolved or not, that
  // RetireInFlight() hasn't retired yet.
  size_t mInFlightCount = 0;
};

class WebTransportDatagramDuplexStream final : public nsISupports,
                                               public nsWrapperCache {
  friend class IncomingDatagramStreamAlgorithms;
  friend class OutgoingDatagramStreamAlgorithms;
  friend class WebTransportDatagramsWritable;

 public:
  WebTransportDatagramDuplexStream(nsIGlobalObject* aGlobal,
                                   WebTransport* aWebTransport);

  void Init(bool aUseByteStream, ErrorResult& aError);

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(WebTransportDatagramDuplexStream)

  void SetChild(WebTransportChild* aChild);

  // https://w3c.github.io/webtransport/#webtransport-cleanup step 11: empty
  // every outgoing datagram writable's [[OutgoingDatagramsQueue]].
  //
  // The queued chunk's promise is also our WritableStream sink's write
  // promise, which the spec leaves unsettled here; dropping it would wedge
  // WritableStreamStartErroring(), which only finishes erroring -- and so
  // only reaches CloseOrErrorWritables() below -- once no write is marked in
  // flight. Resolving with undefined matches how sendDatagrams settles a
  // datagram dropped without being sent (step 5.1.2.2).
  void Cleanup();

  // https://w3c.github.io/webtransport/#webtransport-cleanup steps 16.5/17.7:
  // close (aClean) or error (!aClean, with aErrorValue) every outgoing
  // datagram writable -- the default one plus every one from
  // createWritable(). Must run after Cleanup().
  MOZ_CAN_RUN_SCRIPT void CloseOrErrorWritables(
      JSContext* aCx, bool aClean, JS::Handle<JS::Value> aErrorValue);

  void NewDatagramReceived(nsTArray<uint8_t>&& aData,
                           const mozilla::TimeStamp& aTimeStamp);

  // Sends a datagram to the parent in an OutgoingDatagrams IPC message.
  // Called by every OutgoingDatagramStreamAlgorithms instance (the default
  // writable plus any from createWritable()) once mChild is set.
  void QueueOutgoingDatagram(OutgoingDatagram&& aDatagram);

  // Sends whatever has accumulated in mPendingOutgoing, split into
  // OutgoingDatagrams messages of at most about kMaxOutgoingMessageBytes or
  // kMaxOutgoingMessageDatagrams; a no-op if nothing has.
  void FlushOutgoingDatagrams();

  // Handles a DatagramCapacity message, the parent's aggregate stand-in for
  // sendDatagrams.  The credit limits only rise, as with MAX_DATA.
  void OnDatagramCapacity(uint64_t aCumulativeDatagrams,
                          uint64_t aCumulativeBytes, uint64_t aSentDelta,
                          uint64_t aExpiredDelta, uint64_t aDroppedDelta,
                          uint64_t aDatagramPrefixBytes);

  // WebIDL Boilerplate
  nsIGlobalObject* GetParentObject() const;

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  // WebIDL Interface
  already_AddRefed<ReadableStream> Readable() const {
    RefPtr<ReadableStream> result(mReadable);
    return result.forget();
  }
  already_AddRefed<WritableStream> Writable() {
    RefPtr<WritableStream> result(mWritable);
    return result.forget();
  }

  uint64_t MaxDatagramSize() const { return mOutgoingMaxDataSize; }
  void SetMaxDatagramSize(const uint64_t& aMaxDatagramSize) {
    mOutgoingMaxDataSize = aMaxDatagramSize;
  }
  Nullable<double> GetIncomingMaxAge(ErrorResult& aRv) const {
    return mIncomingMaxAge;
  }
  void SetIncomingMaxAge(const Nullable<double>& aMaxAge, ErrorResult& aRv);
  Nullable<double> GetOutgoingMaxAge(ErrorResult& aRv) const {
    return mOutgoingMaxAge;
  }
  void SetOutgoingMaxAge(const Nullable<double>& aMaxAge, ErrorResult& aRv);
  uint32_t IncomingMaxBufferedDatagrams() const {
    return mIncomingMaxBufferedDatagrams;
  }
  void SetIncomingMaxBufferedDatagrams(uint32_t aValue);
  uint32_t OutgoingMaxBufferedDatagrams() const {
    return mOutgoingMaxBufferedDatagrams;
  }
  void SetOutgoingMaxBufferedDatagrams(uint32_t aValue);

  already_AddRefed<WebTransportDatagramsWritable> CreateWritable(
      const WebTransportSendOptions& aOptions, ErrorResult& aRv);

 private:
  ~WebTransportDatagramDuplexStream() = default;

  MOZ_CAN_RUN_SCRIPT static void CloseOrErrorWritable(
      JSContext* aCx, WritableStream* aWritable, bool aClean,
      JS::Handle<JS::Value> aErrorValue);

  nsCOMPtr<nsIGlobalObject> mGlobal;
  RefPtr<WebTransport> mWebTransport;
  RefPtr<ReadableStream> mReadable;
  RefPtr<WritableStream> mWritable;
  RefPtr<IncomingDatagramStreamAlgorithms> mIncomingAlgorithms;
  RefPtr<OutgoingDatagramStreamAlgorithms> mOutgoingAlgorithms;
  // WebTransportDatagramsWritable objects from CreateWritable() calls made
  // before the session connected, waiting for SetChild() to flush their
  // queued datagrams.
  nsTArray<RefPtr<WebTransportDatagramsWritable>> mCreatedWritables;
  // The spec's [[Writables]]: every writable CreateWritable() made, never
  // cleared so Cleanup() reaches them all.  The deprecated `.writable`, which
  // the spec has dropped, isn't one; it is mOutgoingAlgorithms.
  nsTArray<RefPtr<WebTransportDatagramsWritable>> mAllWritables;

  // https://w3c.github.io/webtransport/#webtransportdatagramduplexstream-create
  // null means no maximum age (infinite)
  Nullable<double> mIncomingMaxAge;
  Nullable<double> mOutgoingMaxAge;
  // Both are implementation-defined; for the outgoing one the spec asks only
  // that it be "tuned to ensure decent throughput, without jeopardizing the
  // timeliness of transmitted data".
  //
  // outgoingMaxBufferedDatagrams is the length of the sink's send queue.
  // writeDatagrams withholds write()'s promise once the queue reaches it, so
  // the WritableStream holds back further writes, awaited or not, until
  // datagrams leave the queue. (The incoming side is deliberately different:
  // receiveDatagrams drops whatever exceeds incomingMaxBufferedDatagrams.)
  //
  // 5 rather than Chromium's and WebKit's 1 is deliberate: our outgoing queue
  // lives in the parent process, so a slot frees up only once a
  // DatagramCapacity grant comes back over IPC. The count is therefore
  // pipeline depth measured in IPC round trips, not in local queue slots, and
  // 1 would serialize a sender on them.
  uint32_t mIncomingMaxBufferedDatagrams = 5;
  uint32_t mOutgoingMaxBufferedDatagrams = 5;
  uint64_t mOutgoingMaxDataSize = 1024;  // implementation-defined integer

  mozilla::Queue<UniquePtr<DatagramEntry>, 32> mIncomingDatagramsQueue;
  uint64_t mDroppedIncoming = 0;
  uint64_t mExpiredIncoming = 0;

  // https://w3c.github.io/webtransport/#dom-webtransportdatagramstats-expiredoutgoing
  // Counts neqo's expiries and the parent's drops of datagrams already too
  // old on arrival, which neqo can't see.
  uint64_t mExpiredOutgoing = 0;
  // Not in getStats(): WebTransportDatagramStats has no field for either
  // (lostOutgoing is for datagrams lost in the network).  Kept for telemetry
  // or an adaptive sender.
  uint64_t mSentOutgoing = 0;
  uint64_t mDroppedOutgoing = 0;

  // Outgoing datagrams accumulated by QueueOutgoingDatagram() or SetChild()
  // for the next OutgoingDatagrams message.
  nsTArray<OutgoingDatagram> mPendingOutgoing;

  // Mirrors neqo-transport's SenderFlowControl (limit/used/available),
  // separately for each credit dimension. mCredit*Limit is the latest
  // DatagramCapacity grant; mCredit*Used is how much of it has been spent
  // resolving write() promises, immediately or from every
  // OutgoingDatagramStreamAlgorithms instance's own mPendingDatagramPromises.
  uint64_t mCreditDatagramsLimit = 0;
  uint64_t mCreditDatagramsUsed = 0;
  uint64_t mCreditBytesLimit = 0;
  uint64_t mCreditBytesUsed = 0;
  // From DatagramCapacity; 0 until the first grant.
  uint64_t mDatagramPrefixBytes = 0;

  // Charged when a write() promise resolves, immediately or not, which can be
  // before the parent has been told about the datagram.  Bytes are charged the
  // way neqo's queue charges them, payload plus a fixed overhead.
  void ChargeCredit(uint64_t aPayloadBytes) {
    mCreditDatagramsUsed++;
    mCreditBytesUsed +=
        DatagramQueueCharge(aPayloadBytes, mDatagramPrefixBytes);
  }
  uint64_t CreditBytesAvailable() const {
    return mCreditBytesLimit > mCreditBytesUsed
               ? mCreditBytesLimit - mCreditBytesUsed
               : 0;
  }
  bool HasCreditBytesFor(uint64_t aPayloadBytes) const {
    return DatagramQueueCharge(aPayloadBytes, mDatagramPrefixBytes) <=
           CreditBytesAvailable();
  }

 public:
  uint64_t GetDroppedIncoming() const { return mDroppedIncoming; }
  uint64_t GetExpiredIncoming() const { return mExpiredIncoming; }
  uint64_t GetExpiredOutgoing() const { return mExpiredOutgoing; }
  uint64_t GetSentOutgoing() const { return mSentOutgoing; }
  uint64_t GetDroppedOutgoing() const { return mDroppedOutgoing; }
};

}  // namespace mozilla::dom

#endif
