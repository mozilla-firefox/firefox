/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebTransportDatagramDuplexStream.h"

#include <algorithm>
#include <memory>

#include "js/experimental/TypedData.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/dom/WebTransport.h"
#include "mozilla/dom/WebTransportDatagramsWritable.h"
#include "mozilla/dom/WebTransportLog.h"
#include "mozilla/dom/WebTransportSendGroup.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(WebTransportDatagramDuplexStream, mGlobal,
                                      mReadable, mWritable, mWebTransport,
                                      mIncomingAlgorithms, mOutgoingAlgorithms,
                                      mCreatedWritables, mAllWritables)
// mIncomingDatagramsQueue can't participate in a cycle
NS_IMPL_CYCLE_COLLECTING_ADDREF(WebTransportDatagramDuplexStream)
NS_IMPL_CYCLE_COLLECTING_RELEASE(WebTransportDatagramDuplexStream)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WebTransportDatagramDuplexStream)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

WebTransportDatagramDuplexStream::WebTransportDatagramDuplexStream(
    nsIGlobalObject* aGlobal, WebTransport* aWebTransport)
    : mGlobal(aGlobal), mWebTransport(aWebTransport) {}

OutgoingDatagramStreamAlgorithms::OutgoingDatagramStreamAlgorithms(
    WebTransportDatagramDuplexStream* aDatagrams,
    WebTransportSendGroup* aSendGroup, int64_t aSendOrder)
    : mDatagrams(aDatagrams),
      mSendGroup(aSendGroup),
      mSendOrder(aSendOrder),
      mBackpressurePromise(
          Promise::CreateInfallible(aDatagrams->GetParentObject())) {
  mBackpressurePromise->MaybeResolveWithUndefined();
}

void WebTransportDatagramDuplexStream::Init(bool aUseByteStream,
                                            ErrorResult& aError) {
  // https://w3c.github.io/webtransport/#webtransport-constructor
  // We are only called synchronously from JS creating a WebTransport object
  AutoEntryScript aes(mGlobal, "WebTransportDatagrams");
  JSContext* cx = aes.cx();

  mIncomingAlgorithms = new IncomingDatagramStreamAlgorithms(this);
  nsCOMPtr<nsIGlobalObject> global(mGlobal);
  RefPtr<IncomingDatagramStreamAlgorithms> incomingAlgorithms =
      mIncomingAlgorithms;
  // Step 20-21: If datagramsReadableType is "bytes", set up with byte reading
  // support; otherwise set up normally.
  if (aUseByteStream) {
    mReadable = ReadableStream::CreateByteNative(
        cx, global, *incomingAlgorithms, Some(0.0), aError);
  } else {
    mReadable = ReadableStream::CreateNative(cx, global, *incomingAlgorithms,
                                             Some(0.0), nullptr, aError);
  }
  if (aError.Failed()) {
    return;
  }

  mOutgoingAlgorithms = new OutgoingDatagramStreamAlgorithms(this);
  RefPtr<OutgoingDatagramStreamAlgorithms> outgoingAlgorithms =
      mOutgoingAlgorithms;
  // Step 19: Set up outgoingDatagrams with writeAlgorithm set to
  // writeDatagramsAlgorithm.
  //
  // Backpressure at the outgoingMaxBufferedDatagrams limit is exerted by
  // writeDatagrams returning a pending [[BackpressurePromise]] (see
  // WriteCallbackImpl), not by the queuing strategy, so highWaterMark is left
  // at the Streams default (Nothing() -> 1).
  mWritable = WritableStream::CreateNative(cx, *global, *outgoingAlgorithms,
                                           Nothing(), nullptr, aError);
  if (aError.Failed()) {
    return;
  }
  LOG(("Created datagram streams"));
}

void WebTransportDatagramDuplexStream::SetChild(WebTransportChild* aChild) {
  // The parent must know outgoingMaxAge before it sees any pre-connect
  // datagram: queuing them below can flush a batch straight away.
  aChild->SendUpdateDatagramQueueParams(
      mOutgoingMaxBufferedDatagrams,
      mOutgoingMaxAge.IsNull() ? NAN : mOutgoingMaxAge.Value());
  mOutgoingAlgorithms->SetChild(aChild);
  for (auto& writable : mCreatedWritables) {
    writable->SetChild(aChild);
  }
  mCreatedWritables.Clear();

  // Every algorithms instance above just queued its mWaitConnectQueue via
  // QueueOutgoingDatagram(); flush that initial batch now rather than
  // waiting for a posted runnable, since this is already a known batch
  // boundary.
  FlushOutgoingDatagrams();
}

void WebTransportDatagramDuplexStream::SetIncomingMaxAge(
    const Nullable<double>& aMaxAge, ErrorResult& aRv) {
  // https://w3c.github.io/webtransport/#dom-webtransportdatagramduplexstream-incomingmaxage
  Nullable<double> value = aMaxAge;
  if (!value.IsNull()) {
    // Step 1
    if (isnan(value.Value()) || value.Value() < 0.) {
      aRv.ThrowRangeError("Invalid IncomingMaxAge");
      return;
    }
    // Step 2
    if (value.Value() == 0.) {
      value.SetNull();
    }
  }
  // Step 3
  mIncomingMaxAge = value;
}

void WebTransportDatagramDuplexStream::SetOutgoingMaxAge(
    const Nullable<double>& aMaxAge, ErrorResult& aRv) {
  // https://w3c.github.io/webtransport/#dom-webtransportdatagramduplexstream-outgoingmaxage
  Nullable<double> value = aMaxAge;
  if (!value.IsNull()) {
    // Step 1
    if (isnan(value.Value()) || value.Value() < 0.) {
      aRv.ThrowRangeError("Invalid OutgoingMaxAge");
      return;
    }
    // Step 2
    if (value.Value() == 0.) {
      value.SetNull();
    }
  }
  // Step 3
  mOutgoingMaxAge = value;

  // Keep the new params ordered after datagrams already written.
  FlushOutgoingDatagrams();
  // NaN tells the parent outgoingMaxAge is null; the setter rejects a real NaN.
  mOutgoingAlgorithms->UpdateQueueParams(
      mOutgoingMaxBufferedDatagrams,
      mOutgoingMaxAge.IsNull() ? NAN : mOutgoingMaxAge.Value());
}

void WebTransportDatagramDuplexStream::SetIncomingMaxBufferedDatagrams(
    uint32_t aValue) {
  // https://w3c.github.io/webtransport/#dom-webtransportdatagramduplexstream-incomingmaxbuffereddatagrams
  mIncomingMaxBufferedDatagrams = std::max(aValue, 1u);
}

void WebTransportDatagramDuplexStream::SetOutgoingMaxBufferedDatagrams(
    uint32_t aValue) {
  // https://w3c.github.io/webtransport/#dom-webtransportdatagramduplexstream-outgoingmaxbuffereddatagrams
  mOutgoingMaxBufferedDatagrams = std::max(aValue, 1u);

  // outgoingMaxBufferedDatagrams sizes the outgoing datagram queue, not the
  // Streams queuing-strategy highWaterMark (which stays at the spec default of
  // 1). Backpressure at this limit is exerted by writeDatagrams returning a
  // pending [[BackpressurePromise]] (see WriteCallbackImpl), so the controller
  // HWM is unchanged.

  // Send updated parameters to parent/neqo
  FlushOutgoingDatagrams();
  mOutgoingAlgorithms->UpdateQueueParams(
      mOutgoingMaxBufferedDatagrams,
      mOutgoingMaxAge.IsNull() ? NAN : mOutgoingMaxAge.Value());
}

already_AddRefed<WebTransportDatagramsWritable>
WebTransportDatagramDuplexStream::CreateWritable(
    const WebTransportSendOptions& aOptions, ErrorResult& aRv) {
  LOG(("WebTransportDatagramDuplexStream::CreateWritable() called"));
  // https://w3c.github.io/webtransport/#dom-webtransportdatagramduplexstream-createwritable

  // Step 1: Let transport be WebTransport object associated with this.
  if (!mWebTransport) {
    aRv.ThrowInvalidStateError("WebTransport is not available");
    return nullptr;
  }

  // Step 2: Let sendGroup be options["sendGroup"].
  // Step 3: If sendGroup is not null, and sendGroup.[[Transport]] is not
  // this.[[Transport]], throw a TypeError.
  if (aOptions.mSendGroup &&
      aOptions.mSendGroup->GetTransport() != mWebTransport) {
    aRv.ThrowTypeError("sendGroup does not belong to the same WebTransport");
    return nullptr;
  }

  // Step 4: If transport.[[State]] is "closed" or "failed", throw an
  // InvalidStateError.
  auto state = mWebTransport->mState;
  if (state == WebTransport::WebTransportState::CLOSED ||
      state == WebTransport::WebTransportState::FAILED) {
    aRv.ThrowInvalidStateError("WebTransport closed or failed");
    return nullptr;
  }

  // Step 5: Let sendOrder be options["sendOrder"].
  int64_t sendOrder = aOptions.mSendOrder;

  // Step 6: Let writableStream be the result of creating a
  // WebTransportDatagramsWritable with transport, sendGroup, and sendOrder.
  AutoEntryScript aes(mGlobal, "WebTransportCreateWritable");
  JSContext* cx = aes.cx();
  RefPtr<WebTransportDatagramsWritable> writableStream =
      WebTransportDatagramsWritable::Create(cx, mGlobal, mWebTransport, this,
                                            aOptions.mSendGroup, sendOrder,
                                            aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  mAllWritables.AppendElement(writableStream);
  // If the session is not yet connected, register for SetChild() to flush
  // this writable's queued datagrams once it connects.
  if (!writableStream->IsConnected()) {
    mCreatedWritables.AppendElement(writableStream);
  }

  return writableStream.forget();
}

void WebTransportDatagramDuplexStream::NewDatagramReceived(
    nsTArray<uint8_t>&& aData, const mozilla::TimeStamp& aTimeStamp) {
  LOG(("received Datagram, size = %zu", aData.Length()));
  if (mWebTransport->mState == WebTransport::WebTransportState::CLOSED ||
      mWebTransport->mState == WebTransport::WebTransportState::FAILED) {
    return;
  }

  // Drop oldest datagrams if the queue has reached the buffer limit. We still
  // drop if equal since we're about to add one.
  // TODO(Bug 2083166): apply backpressure instead of dropping.
  while (mIncomingDatagramsQueue.Count() >= mIncomingMaxBufferedDatagrams) {
    LOG(
        ("Dropping oldest incoming datagram: queue size (%zu) >= "
         "incomingMaxBufferedDatagrams (%u)",
         mIncomingDatagramsQueue.Count(), mIncomingMaxBufferedDatagrams));
    UniquePtr<DatagramEntry> entry = mIncomingDatagramsQueue.Pop();
    mDroppedIncoming++;
  }
  // Filter out any datagrams that have a duration since reception longer than
  // the effective max age. Entries are FIFO by arrival time, so once the oldest
  // (front) entry isn't expired, none behind it can be either.
  //
  // A null incomingMaxAge does not mean "keep forever": receiveDatagrams step 4
  // requires us to substitute an implementation-defined duration, and the
  // attribute keeps reporting null either way since the substitution is local
  // to the algorithm. Unlike the outgoing default this is a fixed value rather
  // than a multiple of the RTT, because it bounds how long a datagram waits for
  // the application to read it, which has nothing to do with the path. It is
  // also only a staleness bound, not a memory bound -
  // incomingMaxBufferedDatagrams already caps the queue - so it can afford to
  // be generous. Same value Chromium uses (kDefaultIncomingMaxAge).
  static constexpr double kDefaultIncomingMaxAgeMs = 60000.0;
  const double maxAgeMs = mIncomingMaxAge.IsNull() ? kDefaultIncomingMaxAgeMs
                                                   : mIncomingMaxAge.Value();
  TimeStamp now = TimeStamp::Now();
  while (!mIncomingDatagramsQueue.IsEmpty() &&
         (now - mIncomingDatagramsQueue.FirstElement()->mTimeStamp)
                 .ToMilliseconds() > maxAgeMs) {
    LOG(("Dropping expired incoming datagram: age exceeds max age (%f ms)",
         maxAgeMs));
    UniquePtr<DatagramEntry> entry = mIncomingDatagramsQueue.Pop();
    mExpiredIncoming++;
  }

  mIncomingDatagramsQueue.Push(
      std::make_unique<DatagramEntry>(std::move(aData), aTimeStamp));
  mIncomingAlgorithms->NotifyDatagramAvailable();
}

// WebIDL Boilerplate

nsIGlobalObject* WebTransportDatagramDuplexStream::GetParentObject() const {
  return mGlobal;
}

JSObject* WebTransportDatagramDuplexStream::WrapObject(
    JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return WebTransportDatagramDuplexStream_Binding::Wrap(aCx, this, aGivenProto);
}

// WebIDL Interface

using namespace mozilla::ipc;

NS_IMPL_CYCLE_COLLECTION_INHERITED(IncomingDatagramStreamAlgorithms,
                                   UnderlyingSourceAlgorithmsWrapper,
                                   mDatagrams, mIncomingDatagramsPullPromise)
NS_IMPL_ADDREF_INHERITED(IncomingDatagramStreamAlgorithms,
                         UnderlyingSourceAlgorithmsWrapper)
NS_IMPL_RELEASE_INHERITED(IncomingDatagramStreamAlgorithms,
                          UnderlyingSourceAlgorithmsWrapper)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(IncomingDatagramStreamAlgorithms)
NS_INTERFACE_MAP_END_INHERITING(UnderlyingSourceAlgorithmsWrapper)

IncomingDatagramStreamAlgorithms::IncomingDatagramStreamAlgorithms(
    WebTransportDatagramDuplexStream* aDatagrams)
    : mDatagrams(aDatagrams) {}

IncomingDatagramStreamAlgorithms::~IncomingDatagramStreamAlgorithms() = default;

already_AddRefed<Promise> IncomingDatagramStreamAlgorithms::PullCallbackImpl(
    JSContext* aCx, ReadableStreamControllerBase& aController,
    ErrorResult& aRv) {
  // https://w3c.github.io/webtransport/#datagram-duplex-stream-procedures

  RefPtr<Promise> promise =
      Promise::CreateInfallible(mDatagrams->GetParentObject());
  // Step 1: Let datagrams be transport.[[Datagrams]].
  // Step 2: Assert: datagrams.[[IncomingDatagramsPullPromise]] is null.
  MOZ_ASSERT(!mIncomingDatagramsPullPromise);

  RefPtr<IncomingDatagramStreamAlgorithms> self(this);
  // The real work of PullCallback()
  // Step 3: Let queue be datagrams.[[IncomingDatagramsQueue]].
  // Step 4: If queue is empty, then:
  if (mDatagrams->mIncomingDatagramsQueue.IsEmpty()) {
    // We need to wait.
    // Per
    // https://streams.spec.whatwg.org/#readablestreamdefaultcontroller-pulling
    // we can't be called again until the promise is resolved
    // Step 4.1:  Set datagrams.[[IncomingDatagramsPullPromise]] to a new
    // promise.
    // Step 4.2: Return datagrams.[[IncomingDatagramsPullPromise]].
    mIncomingDatagramsPullPromise = promise;

    LOG(("Datagrams Pull waiting for a datagram"));
    Result<RefPtr<Promise>, nsresult> returnResult =
        promise->ThenWithCycleCollectedArgs(
            [](JSContext* aCx, JS::Handle<JS::Value>, ErrorResult& aRv,
               RefPtr<IncomingDatagramStreamAlgorithms> self,
               RefPtr<Promise> aPromise)
                MOZ_CAN_RUN_SCRIPT_FOR_DEFINITION -> already_AddRefed<Promise> {
                  // https://w3c.github.io/webtransport/#receivedatagrams
                  // step 10
                  self->ReturnDatagram(aCx, aRv);
                  return nullptr;
                },
            self, promise);
    if (returnResult.isErr()) {
      aRv.Throw(returnResult.unwrapErr());
      return nullptr;
    }
    // Step 4: Return p and run the remaining steps in parallel.
    return returnResult.unwrap().forget();
  }
  // Steps 5-7 are covered here:
  self->ReturnDatagram(aCx, aRv);
  // Step 8: Return a promise resolved with undefined.
  promise->MaybeResolveWithUndefined();
  return promise.forget();
}

// Note: fallible
void IncomingDatagramStreamAlgorithms::ReturnDatagram(JSContext* aCx,
                                                      ErrorResult& aRv) {
  // https://w3c.github.io/webtransport/#datagram-duplex-stream-procedures
  // Pull and Receive
  LOG(("Returning a Datagram"));

  MOZ_ASSERT(!mDatagrams->mIncomingDatagramsQueue.IsEmpty());

  // Step 5: Let incoming be the result of dequeuing queue.
  UniquePtr<DatagramEntry> entry = mDatagrams->mIncomingDatagramsQueue.Pop();
  // Step 6: Let datagram be incoming[0].
  size_t datagramSize = entry->mBuffer.Length();

  RefPtr<ReadableStream> stream = mDatagrams->mReadable;
  // Step 7: If datagrams.[[ReadableType]] is "bytes", then:
  if (stream->Controller()->IsByte()) {
    // Step 7.1: If datagrams.[[Readable]]'s current BYOB request view is not
    // null, then:
    JS::Rooted<JSObject*> byobView(aCx);
    stream->GetCurrentBYOBRequestView(aCx, &byobView, aRv);
    if (aRv.Failed()) {
      return;
    }

    if (byobView) {
      // Step 7.1.1: Let view be datagrams.[[Readable]]'s current BYOB request
      // view.
      // Step 7.1.2: If view's byte length is less than the size of datagram,
      // return a promise rejected with a RangeError.
      if (datagramSize > JS_GetArrayBufferViewByteLength(byobView)) {
        LOG(("BYOB buffer too small: datagram=%zu", datagramSize));
        aRv.ThrowRangeError("Buffer too small for datagram");
        return;
      }

      // Step 7.1.3: Let elementSize be the element size specified in the typed
      // array constructors table for view.[[TypedArrayName]]. If view does not
      // have a [[TypedArrayName]] internal slot (i.e. it is a DataView), let
      // elementSize be 0.
      // Step 7.1.4: If elementSize is not 1, return a promise rejected with a
      // TypeError.
      //
      // The current BYOB request view is always a Uint8Array, so elementSize
      // is always 1.
      MOZ_ASSERT(JS_IsTypedArrayObject(byobView) &&
                 JS_GetArrayBufferViewType(byobView) == JS::Scalar::Uint8);
    }

    // Step 7.2: Pull from bytes datagram into datagrams.[[Readable]].
    //
    // With a BYOB view we already rejected above if it is smaller than the
    // datagram; without one the whole datagram is pulled by construction.
    // Either way this consumes all of it.
    DebugOnly<uint64_t> pulled =
        stream->PullFromBytes(aCx, entry->mBuffer, aRv);
    MOZ_ASSERT(aRv.Failed() || pulled == datagramSize);
  } else {
    // Step 8: Otherwise:
    // Step 8.1: Let chunk be a new Uint8Array object representing datagram.
    JSObject* outView = Uint8Array::Create(aCx, entry->mBuffer, aRv);
    if (aRv.Failed()) {
      return;
    }
    JS::Rooted<JSObject*> chunk(aCx, outView);

    // Step 8.2: Enqueue chunk to transport.[[Datagrams]].[[Readable]].
    JS::Rooted<JS::Value> jsDatagram(aCx, JS::ObjectValue(*chunk));
    stream->EnqueueNative(aCx, jsDatagram, aRv);
  }
  // (caller) Step 9: Return a promise resolved with undefined.
}

void IncomingDatagramStreamAlgorithms::NotifyDatagramAvailable() {
  if (RefPtr<Promise> promise = mIncomingDatagramsPullPromise.forget()) {
    promise->MaybeResolveWithUndefined();
  }
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(OutgoingDatagramStreamAlgorithms,
                                   UnderlyingSinkAlgorithmsWrapper, mDatagrams,
                                   mSendGroup, mOwnerWritable,
                                   mBackpressurePromise)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(OutgoingDatagramStreamAlgorithms)
NS_INTERFACE_MAP_END_INHERITING(UnderlyingSinkAlgorithmsWrapper)
NS_IMPL_ADDREF_INHERITED(OutgoingDatagramStreamAlgorithms,
                         UnderlyingSinkAlgorithmsWrapper)
NS_IMPL_RELEASE_INHERITED(OutgoingDatagramStreamAlgorithms,
                          UnderlyingSinkAlgorithmsWrapper)

// Bound one OutgoingDatagrams message well under the parent's
// kMaxBatchBytes/kMaxBatchDatagrams, which it treats as a protocol error.  The
// count cap matters only for tiny datagrams.
static constexpr size_t kMaxOutgoingMessageBytes = 64 * 1024;
static constexpr size_t kMaxOutgoingMessageDatagrams = 1024;

void WebTransportDatagramDuplexStream::QueueOutgoingDatagram(
    OutgoingDatagram&& aDatagram) {
  mPendingOutgoingBytes += aDatagram.data().Length();
  mPendingOutgoing.AppendElement(std::move(aDatagram));

  if (mPendingOutgoingBytes >= kMaxOutgoingMessageBytes) {
    FlushOutgoingDatagrams();
    return;
  }
  if (mOutgoingFlushPending) {
    return;
  }
  mOutgoingFlushPending = true;
  nsresult rv = NS_DispatchToCurrentThread(NS_NewRunnableFunction(
      "WebTransportDatagramDuplexStream::QueueOutgoingDatagram",
      [self = RefPtr{this}]() {
        self->mOutgoingFlushPending = false;
        self->FlushOutgoingDatagrams();
      }));
  if (NS_FAILED(rv)) {
    mOutgoingFlushPending = false;
  }
}

void WebTransportDatagramDuplexStream::FlushOutgoingDatagrams() {
  if (mPendingOutgoing.IsEmpty()) {
    return;
  }

  WebTransportChild* child = mOutgoingAlgorithms->GetChild();
  MOZ_ASSERT(child);

  LOG(("Flushing %zu batched outgoing datagrams", mPendingOutgoing.Length()));
  // Fire-and-forget: datagrams are sent untracked, so there is no reply to
  // wait for. If the actor is already dead, Send* below just fails
  // (returns false) having done nothing -- WebTransportChild::ActorDestroy()
  // already resolved every [[BackpressurePromise]] by the time that can
  // happen, via
  // CloseAll(), so there's nothing left here to settle.
  Span<const OutgoingDatagram> pending(mPendingOutgoing);
  size_t start = 0;
  size_t bytes = 0;
  for (size_t i = 0; i < pending.Length(); ++i) {
    bytes += pending[i].data().Length();
    if (bytes >= kMaxOutgoingMessageBytes ||
        i + 1 - start == kMaxOutgoingMessageDatagrams ||
        i + 1 == pending.Length()) {
      (void)child->SendOutgoingDatagrams(pending.FromTo(start, i + 1));
      start = i + 1;
      bytes = 0;
    }
  }
  mPendingOutgoing.Clear();
  mPendingOutgoingBytes = 0;
}

already_AddRefed<Promise> OutgoingDatagramStreamAlgorithms::WriteCallbackImpl(
    JSContext* aCx, JS::Handle<JS::Value> aChunk,
    WritableStreamDefaultController& aController, ErrorResult& aError) {
  // https://w3c.github.io/webtransport/#writedatagrams
  // Step 1: Let timestamp be a timestamp representing now.
  TimeStamp now = TimeStamp::Now();

  // Step 2: If data is not a BufferSource object, then return a promise
  // rejected with a TypeError. { BufferSource == ArrayBuffer/ArrayBufferView }
  ArrayBufferViewOrArrayBuffer arrayBuffer;
  if (!arrayBuffer.Init(aCx, aChunk)) {
    JS_ClearPendingException(aCx);
    aError.ThrowTypeError("Wrong type for Datagram stream write"_ns);
    return nullptr;
  }

  // Step 3: Let duplex be transport.[[Datagrams]].
  // (mDatagrams is transport.[[Datagrams]])

  // Step 5 (early): Let bytes be a copy of bytes which data represents.
  // Copied here rather than in step order since step 4 needs its length and
  // the steps below hand the buffer straight off.
  nsTArray<uint8_t> data;
  (void)AppendTypedArrayDataTo(arrayBuffer, data);

  // Step 4: If duplex.[[OutgoingMaxDatagramSize]] is less than data’s
  // [[ByteLength]], return a promise resolved with undefined.
  if (mDatagrams->mOutgoingMaxDataSize < static_cast<uint64_t>(data.Length())) {
    return Promise::CreateResolvedWithUndefined(mDatagrams->GetParentObject(),
                                                aError);
  }

  // WritableStream doesn't call us again until the promise returned last
  // time settles, so a pending one is never replaced below.
  MOZ_ASSERT(mBackpressurePromise->State() != Promise::PromiseState::Pending);

  // Step 6: Let chunk be (bytes, timestamp).
  size_t dataLength = data.Length();

  // See mCleanedUp: the write is still consumed, but nothing is forwarded.
  //
  // Not in spec: writeDatagrams has no closed/failed check, so the spec
  // enqueues this write and, past outgoingMaxBufferedDatagrams, sets a new
  // [[BackpressurePromise]] that nothing resolves once cleanup has run,
  // since sendDatagrams no longer runs then.  Returning the one cleanup
  // resolved instead lets the WritableStream finish.
  if (mCleanedUp) {
    return do_AddRef(mBackpressurePromise);
  }

  // mChild is set when we move to Connected
  if (mChild) {
    // Step 7: Enqueue chunk to writable.[[OutgoingDatagramsQueue]].
    //
    // Not in spec: that queue lives in the parent process (and below it in
    // neqo), so the chunk is handed to the parent below, and mInFlight
    // stands in for the queue's length -- it counts every datagram handed to
    // the parent until RetireInFlight() is told (in aggregate) that it has
    // left neqo's queue.  Per-instance, like the spec's slot, which is
    // per-writable.
    //
    // Step 8: If writable's send queue is full, then set
    // writable.[[BackpressurePromise]] to a new promise.
    //
    // The send queue is full when its length is not less than
    // duplex.[[OutgoingMaxBufferedDatagrams]], or when its byte length is
    // greater than an implementation-defined limit.  Ours is neqo's queue byte
    // budget, which evicts once full, so the datagram must also fit in the
    // parent's byte credit.
    bool underMaxBuffered =
        mInFlight.Add(mDatagrams->mOutgoingMaxBufferedDatagrams);
    if (underMaxBuffered && mDatagrams->mCredit.HasBytesFor(dataLength)) {
      mDatagrams->mCredit.Charge(dataLength);
    } else {
      // Resolved by TryResolveBackpressureFromCredit() as the parent's
      // DatagramCapacity grants allow, or by Cleanup() at teardown.
      mBackpressurePromise =
          Promise::CreateInfallible(mDatagrams->GetParentObject());
      mBackpressureBytes = dataLength;
    }

    // The rest of step 7: queue the datagram for the next batched
    // OutgoingDatagrams message.  The real [[OutgoingDatagramsQueue]] and
    // all of sendDatagrams live in parent/neqo.
    uint64_t sendGroupId = mSendGroup ? mSendGroup->GetGroupId() : 0;
    LOG(("Queuing datagram size=%zu, sendGroup=%" PRIu64 ", sendOrder=%" PRId64,
         dataLength, sendGroupId, mSendOrder));
    mDatagrams->QueueOutgoingDatagram(
        OutgoingDatagram(std::move(data), now, sendGroupId, mSendOrder));
  } else {
    LOG(("Queuing datagram for connect, size=%zu", dataLength));
    // Step 7 again, with nowhere to hand the chunk off to yet: until
    // SetChild() connects us, mWaitConnectQueue is this writable's
    // [[OutgoingDatagramsQueue]].
    mWaitConnectQueue.AppendElement(
        std::make_unique<DatagramEntry>(std::move(data), now));

    // Step 8 measured against that queue instead.
    if (mWaitConnectQueue.Length() <
        mDatagrams->mOutgoingMaxBufferedDatagrams) {
      mDatagrams->mCredit.Charge(dataLength);
    } else {
      mBackpressurePromise =
          Promise::CreateInfallible(mDatagrams->GetParentObject());
      mBackpressureBytes = dataLength;
    }
  }

  // Step 9: Return writable.[[BackpressurePromise]].
  return do_AddRef(mBackpressurePromise);
}

// XXX should we allow datagrams to be sent before connect?  Check IETF spec
void OutgoingDatagramStreamAlgorithms::SetChild(WebTransportChild* aChild) {
  LOG(("Setting child in datagrams, %zu queued datagrams",
       mWaitConnectQueue.Length()));
  mChild = aChild;
  // The pre-connect stand-in queue drains into the real
  // [[OutgoingDatagramsQueue]] in the parent, so every datagram in it becomes
  // in flight now -- mirrors the mChild-set branch of WriteCallbackImpl,
  // which counts a datagram as in flight the moment it is handed to the
  // parent.
  mInFlight.AddQueued(mWaitConnectQueue.Length());
  // Send all queued datagrams
  for (auto& queued : mWaitConnectQueue) {
    uint64_t sendGroupId = mSendGroup ? mSendGroup->GetGroupId() : 0;
    mDatagrams->QueueOutgoingDatagram(
        OutgoingDatagram(std::move(queued->mBuffer), queued->mTimeStamp,
                         sendGroupId, mSendOrder));
  }
  mWaitConnectQueue.Clear();
}

bool OutgoingDatagramStreamAlgorithms::TryResolveBackpressureFromCredit(
    uint64_t aAvailableBytes, size_t* aResolvedBytes) {
  if (mBackpressurePromise->State() != Promise::PromiseState::Pending ||
      DatagramQueueCharge(mBackpressureBytes,
                          mDatagrams->mCredit.DatagramPrefixBytes()) >
          aAvailableBytes) {
    return false;
  }
  *aResolvedBytes = mBackpressureBytes;
  mBackpressurePromise->MaybeResolveWithUndefined();
  return true;
}

void OutgoingDatagramStreamAlgorithms::RetireInFlight(uint64_t aCount) {
  mInFlight.Retire(aCount);
}

void WebTransportDatagramDuplexStream::OnDatagramCapacity(
    uint64_t aCumulativeDatagrams, uint64_t aCumulativeBytes,
    uint64_t aSentDelta, uint64_t aExpiredDelta, uint64_t aDroppedDelta,
    uint64_t aDatagramPrefixBytes) {
  // The parent's aggregate stand-in for sendDatagrams:
  // https://w3c.github.io/webtransport/#senddatagrams
  // Every datagram counted here has left [[OutgoingDatagramsQueue]] -- sent
  // (step 5.4.6), expired (step 5.2.3) or dropped -- and sendDatagrams
  // dequeues outgoing datagrams for it (steps 5.3, 5.4.5 and 5.5).
  // https://w3c.github.io/webtransport/#dequeue-outgoing-datagrams

  mCredit.SetDatagramPrefixBytes(aDatagramPrefixBytes);

  mSentOutgoing += aSentDelta;
  mExpiredOutgoing += aExpiredDelta;
  mDroppedOutgoing += aDroppedDelta;

  // The aggregate can't say which writable a datagram came from, so free the
  // writeDatagrams step 8 slots on every one.  That is only ever too generous,
  // and exact for the usual single writable.
  uint64_t retiredCount = aSentDelta + aExpiredDelta + aDroppedDelta;
  mOutgoingAlgorithms->RetireInFlight(retiredCount);
  for (auto& writable : mAllWritables) {
    writable->RetireInFlight(retiredCount);
  }

  // Matches SenderFlowControl::update: only an increase moves the limit, so
  // a grant that arrives late relative to a newer one is a harmless no-op.
  mCredit.Raise(aCumulativeDatagrams, aCumulativeBytes);

  // The resolutions dequeue outgoing datagrams step 3 owes, round-robin across
  // writables so one long backlog can't starve the others while credit lasts.
  size_t resolvedCount = 0;
  bool resolvedAny = true;
  while (resolvedAny && mCredit.HasDatagramRoom()) {
    resolvedAny = false;
    size_t bytes = 0;
    if (mOutgoingAlgorithms->TryResolveBackpressureFromCredit(
            mCredit.BytesAvailable(), &bytes)) {
      mCredit.Charge(bytes);
      resolvedCount++;
      resolvedAny = true;
    }
    for (auto& writable : mAllWritables) {
      if (!mCredit.HasDatagramRoom()) {
        break;
      }
      size_t writableBytes = 0;
      if (writable->TryResolveBackpressureFromCredit(mCredit.BytesAvailable(),
                                                     &writableBytes)) {
        mCredit.Charge(writableBytes);
        resolvedCount++;
        resolvedAny = true;
      }
    }
  }
  LOG(("OnDatagramCapacity resolved %zu backpressure promises from credit",
       resolvedCount));
}

void OutgoingDatagramStreamAlgorithms::UpdateQueueParams(
    uint32_t aMaxBufferedDatagrams, double aMaxAge) {
  if (mChild) {
    LOG(("Sending UpdateDatagramQueueParams: maxBufferedDatagrams=%" PRIu32
         ", maxAge=%f",
         aMaxBufferedDatagrams, aMaxAge));
    mChild->SendUpdateDatagramQueueParams(aMaxBufferedDatagrams, aMaxAge);
  } else {
    LOG(("UpdateQueueParams called but child not set yet"));
  }
}

void OutgoingDatagramStreamAlgorithms::Cleanup() {
  mCleanedUp = true;
  // https://w3c.github.io/webtransport/#webtransport-cleanup
  // Step 12.1: Set writable.[[OutgoingDatagramsQueue]] to an empty queue.
  mWaitConnectQueue.Clear();
  // Step 12.2: Resolve writable.[[BackpressurePromise]] with undefined.
  mBackpressurePromise->MaybeResolveWithUndefined();
}

nsTArray<RefPtr<WebTransportDatagramsWritable>>
WebTransportDatagramDuplexStream::Cleanup() {
  // Step 11: Set transport.[[Datagrams]].[[Writables]] to an empty set.
  nsTArray<RefPtr<WebTransportDatagramsWritable>> writables =
      std::move(mAllWritables);
  mCreatedWritables.Clear();
  // Step 12: For each writable in outgoingDatagramWritables:
  // The default writable isn't in [[Writables]], so it is handled here too.
  mOutgoingAlgorithms->Cleanup();
  for (const auto& writable : writables) {
    writable->Cleanup();
  }
  // The rest of each writable's [[OutgoingDatagramsQueue]] is this batch, and
  // what the parent and neqo hold, which go away with the session.  Drop the
  // batch so a flush already posted for later becomes a no-op instead of
  // sending stale data for a torn-down session.
  mPendingOutgoing.Clear();
  mPendingOutgoingBytes = 0;
  // Step 13: Set transport.[[Datagrams]].[[IncomingDatagramsQueue]] to an
  // empty queue.
  mIncomingDatagramsQueue.Clear();
  return writables;
}

void WebTransportDatagramDuplexStream::ErrorWritables(
    JSContext* aCx,
    const nsTArray<RefPtr<WebTransportDatagramsWritable>>& aWritables,
    JS::Handle<JS::Value> aErrorValue) {
  if (RefPtr<WritableStream> writable = mWritable) {
    writable->ErrorNative(aCx, aErrorValue, IgnoreErrors());
  }
  for (const auto& writable : aWritables) {
    MOZ_KnownLive(writable)->ErrorNative(aCx, aErrorValue, IgnoreErrors());
  }
}

void WebTransportDatagramDuplexStream::CloseOrErrorReadable(
    JSContext* aCx, bool aClean, JS::Handle<JS::Value> aErrorValue) {
  RefPtr<ReadableStream> readable = mReadable;
  if (aClean) {
    readable->CloseNative(aCx, IgnoreErrors());
  } else {
    readable->ErrorNative(aCx, aErrorValue, IgnoreErrors());
  }
}

}  // namespace mozilla::dom
