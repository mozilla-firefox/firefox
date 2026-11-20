/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_WEBTRANSPORT_API_WEBTRANSPORTDATAGRAMDUPLEXSTREAM_H_
#define DOM_WEBTRANSPORT_API_WEBTRANSPORTDATAGRAMDUPLEXSTREAM_H_

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

  // Called when parent notifies that a datagram is ready (sent or dropped)
  void OnDatagramReady(uint64_t aDatagramId);

  // Send queue parameter updates to parent
  void UpdateQueueParams(uint32_t aMaxBufferedDatagrams, double aMaxAge);

  // Resolves this writable's withheld write promise, if any, so the
  // WritableStream is not left waiting on it once the session is torn down.
  // See WebTransportDatagramDuplexStream::Cleanup().
  void Cleanup();

 private:
  ~OutgoingDatagramStreamAlgorithms() override = default;

  struct QueuedDatagram {
    uint64_t mDatagramId;
    UniquePtr<DatagramEntry> mEntry;
    RefPtr<Promise> mPromise;

    QueuedDatagram(uint64_t aId, UniquePtr<DatagramEntry>&& aEntry,
                   RefPtr<Promise> aPromise)
        : mDatagramId(aId),
          mEntry(std::move(aEntry)),
          mPromise(std::move(aPromise)) {}
  };

  // So nsTArray<QueuedDatagram>'s generic cycle-collection support (see
  // nsTArray.h) can reach mPromise via the overloads below, which need to
  // name the otherwise-private QueuedDatagram type.
  friend void ImplCycleCollectionTraverse(nsCycleCollectionTraversalCallback&,
                                          QueuedDatagram&, const char*,
                                          uint32_t);
  friend void ImplCycleCollectionUnlink(QueuedDatagram&);

  RefPtr<WebTransportDatagramDuplexStream> mDatagrams;
  RefPtr<WebTransportChild> mChild;
  RefPtr<WebTransportDatagramsWritable> mOwnerWritable;
  // Queue for datagrams sent before connection is ready
  nsTArray<QueuedDatagram> mWaitConnectQueue;

  // Send group and order for this writable stream (null/0 for default)
  RefPtr<WebTransportSendGroup> mSendGroup;
  int64_t mSendOrder;

  // Pending promises tracked by datagram ID
  nsTHashMap<nsUint64HashKey, RefPtr<Promise>> mPendingDatagramPromises;
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

  // Called when parent notifies that a datagram is ready
  void OnDatagramReady(uint64_t aDatagramId);

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
  // Every WebTransportDatagramsWritable this duplex stream has ever created
  // via CreateWritable(), for the lifetime of the stream. The default
  // writable exposed via `.writable` isn't wrapped in a
  // WebTransportDatagramsWritable object, so it's tracked separately via
  // mOutgoingAlgorithms instead. Unlike mCreatedWritables, never cleared, so
  // Cleanup() can always reach all of them.
  nsTArray<RefPtr<WebTransportDatagramsWritable>> mAllWritables;

  // https://w3c.github.io/webtransport/#webtransportdatagramduplexstream-create
  // null means no maximum age (infinite)
  Nullable<double> mIncomingMaxAge;
  Nullable<double> mOutgoingMaxAge;
  // These are implementation-defined
  uint32_t mIncomingMaxBufferedDatagrams = 5;
  uint32_t mOutgoingMaxBufferedDatagrams = 5;
  uint64_t mOutgoingMaxDataSize = 1024;  // implementation-defined integer

  mozilla::Queue<UniquePtr<DatagramEntry>, 32> mIncomingDatagramsQueue;
  uint64_t mDroppedIncoming = 0;

  // Datagram ID counter shared across all OutgoingDatagramStreamAlgorithms
  // instances created by this duplex stream (scoped to this duplex stream,
  // not global across duplex streams).
  uint64_t mNextDatagramId = 1;
  // Maps datagram IDs from created writables to the owning writable for routing
  nsTHashMap<nsUint64HashKey, RefPtr<WebTransportDatagramsWritable>>
      mWritableDatagramMap;
  // Set by Cleanup(); lets OnDatagramReady() distinguish a late,
  // already-expected notification (mChild can stay alive past Cleanup(),
  // e.g. after a remote close, so a DatagramReady already in flight can
  // still arrive) from a genuine unregistered/duplicate-id bug.
  bool mCleanedUp = false;

 public:
  uint64_t GetDroppedIncoming() const { return mDroppedIncoming; }
};

}  // namespace mozilla::dom

#endif
