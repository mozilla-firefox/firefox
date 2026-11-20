/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_WEBTRANSPORT_API_WEBTRANSPORTDATAGRAMDUPLEXSTREAM_H_
#define DOM_WEBTRANSPORT_API_WEBTRANSPORTDATAGRAMDUPLEXSTREAM_H_

#include "mozilla/dom/DatagramCredit.h"
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
      WebTransportSendGroup* aSendGroup = nullptr, int64_t aSendOrder = 0);

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

  // https://w3c.github.io/webtransport/#dequeue-outgoing-datagrams step 3:
  // "If writable's send queue is not full, then resolve
  // writable.[[BackpressurePromise]] with undefined."
  //
  // Resolves [[BackpressurePromise]] if it is pending and aAvailableBytes
  // covers the DatagramQueueCharge() of the datagram that left it pending.
  // Not in spec: that queue lives in the parent and neqo, so room is measured
  // by the parent's DatagramCapacity credit instead.  Called by
  // WebTransportDatagramDuplexStream::OnDatagramCapacity()'s round-robin
  // across every algorithms instance.  On success, *aResolvedBytes is that
  // datagram's size, for the caller to charge against its byte credit.
  bool TryResolveBackpressureFromCredit(uint64_t aAvailableBytes,
                                        size_t* aResolvedBytes);

  // https://w3c.github.io/webtransport/#dequeue-outgoing-datagrams steps 1-2:
  // "Let n be the minimum of count and the length of
  // writable.[[OutgoingDatagramsQueue]]." and "Repeat dequeuing
  // writable.[[OutgoingDatagramsQueue]] n times."
  //
  // Lowers the in-flight count by aCount (never below 0), called from
  // WebTransportDatagramDuplexStream::OnDatagramCapacity() with the
  // sent+expired+dropped delta -- the aggregate signal that this many
  // datagrams have left neqo's queue one way or another.  This only lets
  // future writes leave [[BackpressurePromise]] resolved;
  // TryResolveBackpressureFromCredit() is what resolves a pending one.
  void RetireInFlight(uint64_t aCount);

  // Send queue parameter updates to parent
  void UpdateQueueParams(uint32_t aMaxBufferedDatagrams, double aMaxAge);

  // https://w3c.github.io/webtransport/#webtransport-cleanup step 12 for this
  // writable; see WebTransportDatagramDuplexStream::Cleanup().
  void Cleanup();

 private:
  ~OutgoingDatagramStreamAlgorithms() override = default;

  RefPtr<WebTransportDatagramDuplexStream> mDatagrams;
  RefPtr<WebTransportChild> mChild;
  RefPtr<WebTransportDatagramsWritable> mOwnerWritable;
  // Datagrams written before the session connected, waiting for SetChild().
  nsTArray<UniquePtr<DatagramEntry>> mWaitConnectQueue;

  // Send group and order for this writable stream (null/0 for default)
  RefPtr<WebTransportSendGroup> mSendGroup;
  int64_t mSendOrder;

  // [[BackpressurePromise]]: "A promise which is resolved once
  // [[OutgoingDatagramsQueue]] has room for another datagram."  Initially "a
  // promise resolved with undefined".  WritableStream only calls the sink's
  // write once the previous write's promise has settled, so at most one write
  // is ever waiting on it.
  RefPtr<Promise> mBackpressurePromise;
  // Size of the datagram whose write left mBackpressurePromise pending, to
  // charge against the byte credit when it resolves.
  size_t mBackpressureBytes = 0;

  // Stands in for the length of this writable's [[OutgoingDatagramsQueue]],
  // which really lives in the parent: datagrams handed to the parent that
  // RetireInFlight() hasn't yet been told have left neqo's queue.
  // writeDatagrams step 8 measures against this.
  OutgoingDatagramInFlight mInFlight;

  // Set by Cleanup().  WritableStream dequeues one write() at a time, only
  // advancing once the previous one's promise settles, so a burst queued
  // synchronously is still draining across many later microtask turns
  // after Cleanup() itself already ran and returned.  Once set,
  // WriteCallbackImpl returns the [[BackpressurePromise]] Cleanup() resolved
  // without queueing or forwarding the write, since nothing will ever grant
  // more credit to resolve a new one.
  bool mCleanedUp = false;
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

  // https://w3c.github.io/webtransport/#webtransport-cleanup steps 11-13.
  // Returns what [[Writables]] held, for ErrorWritables().
  //
  // Resolving [[BackpressurePromise]] (step 12.2) also lets
  // WritableStreamStartErroring() finish, since it only does once no write
  // is marked in flight.
  [[nodiscard]] nsTArray<RefPtr<WebTransportDatagramsWritable>> Cleanup();

  // https://w3c.github.io/webtransport/#webtransport-cleanup step 17, for
  // aWritables as returned by Cleanup() plus the default writable.
  MOZ_CAN_RUN_SCRIPT void ErrorWritables(
      JSContext* aCx,
      const nsTArray<RefPtr<WebTransportDatagramsWritable>>& aWritables,
      JS::Handle<JS::Value> aErrorValue);

  // https://w3c.github.io/webtransport/#webtransport-cleanup steps 18.5
  // (aClean) and 19.7 (!aClean, with aErrorValue as error).
  MOZ_CAN_RUN_SCRIPT void CloseOrErrorReadable(
      JSContext* aCx, bool aClean, JS::Handle<JS::Value> aErrorValue);

  void NewDatagramReceived(nsTArray<uint8_t>&& aData,
                           const mozilla::TimeStamp& aTimeStamp);

  // Queues a datagram for the next batched OutgoingDatagrams IPC message.
  // Called by every OutgoingDatagramStreamAlgorithms instance (the default
  // writable plus any from createWritable()) instead of sending immediately,
  // so a burst spread across writables still coalesces into one message.
  // Flushes immediately if the pending batch has reached
  // kMaxOutgoingMessageBytes; otherwise posts a flush runnable to the current
  // thread on the first datagram queued, which runs as a later task.  Since
  // this can send right away, an UpdateDatagramQueueParams meant to precede
  // these datagrams must be sent before the first is queued (SetChild()), and
  // one meant to follow datagrams already queued must flush first (the
  // setters).
  void QueueOutgoingDatagram(OutgoingDatagram&& aDatagram);

  // Sends whatever QueueOutgoingDatagram() has accumulated, split into
  // OutgoingDatagrams messages of at most about kMaxOutgoingMessageBytes or
  // kMaxOutgoingMessageDatagrams. Safe to call when there is nothing pending
  // (no-op) or when mChild is not yet set (also a no-op; SetChild() flushes
  // explicitly once it is).
  void FlushOutgoingDatagrams();

  // Handles a DatagramCapacity message, the parent's aggregate stand-in for
  // sendDatagrams (https://w3c.github.io/webtransport/#senddatagrams) calling
  // dequeue outgoing datagrams: raises the credit limits (a decrease is
  // ignored, matching MAX_DATA), adds sentDelta/droppedDelta to their own
  // running totals and expiredDelta to mExpiredOutgoing, dequeues that many
  // datagrams from every writable (RetireInFlight()), then resolves pending
  // [[BackpressurePromise]]s out of the credit -- round-robin across every
  // OutgoingDatagramStreamAlgorithms instance while both credit dimensions
  // allow it, so no single writable can starve another.
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
  // The spec's [[Writables]]: every writable CreateWritable() made, emptied
  // only by Cleanup().  The deprecated `.writable`, which the spec has
  // dropped, isn't one; it is mOutgoingAlgorithms.
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
  // writeDatagrams returns a pending [[BackpressurePromise]] once the queue
  // reaches it, so the WritableStream holds back further writes, awaited or
  // not, until datagrams leave the queue. (The incoming side is deliberately
  // different: receiveDatagrams drops whatever exceeds
  // incomingMaxBufferedDatagrams.)
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

  // Outgoing datagrams accumulated by QueueOutgoingDatagram() for the next
  // OutgoingDatagrams message.
  nsTArray<OutgoingDatagram> mPendingOutgoing;
  size_t mPendingOutgoingBytes = 0;
  // Set while a FlushOutgoingDatagrams() runnable is posted but hasn't run
  // yet, so a burst within one task posts only one.
  bool mOutgoingFlushPending = false;

  // The latest DatagramCapacity grant and how much of it write() promises
  // have used, across every OutgoingDatagramStreamAlgorithms instance.
  OutgoingDatagramCredit mCredit;

 public:
  uint64_t GetDroppedIncoming() const { return mDroppedIncoming; }
  uint64_t GetExpiredIncoming() const { return mExpiredIncoming; }
  uint64_t GetExpiredOutgoing() const { return mExpiredOutgoing; }
  uint64_t GetSentOutgoing() const { return mSentOutgoing; }
  uint64_t GetDroppedOutgoing() const { return mDroppedOutgoing; }
};

}  // namespace mozilla::dom

#endif
