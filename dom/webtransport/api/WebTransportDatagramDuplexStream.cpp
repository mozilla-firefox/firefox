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
                                      mCreatedWritables, mAllWritables,
                                      mWritableDatagramMap)
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
  mWritable = WritableStream::CreateNative(cx, *global, *outgoingAlgorithms,
                                           Nothing(), nullptr, aError);
  if (aError.Failed()) {
    return;
  }
  LOG(("Created datagram streams"));
}

void WebTransportDatagramDuplexStream::SetChild(WebTransportChild* aChild) {
  mOutgoingAlgorithms->SetChild(aChild);
  for (auto& writable : mCreatedWritables) {
    writable->SetChild(aChild);
  }
  mCreatedWritables.Clear();
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

  // Send updated parameters to parent/neqo; null means no expiration (INFINITY)
  mOutgoingAlgorithms->UpdateQueueParams(
      mOutgoingMaxBufferedDatagrams,
      mOutgoingMaxAge.IsNull() ? INFINITY : mOutgoingMaxAge.Value());
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

  // Send updated parameters to parent/neqo
  mOutgoingAlgorithms->UpdateQueueParams(
      mOutgoingMaxBufferedDatagrams,
      mOutgoingMaxAge.IsNull() ? INFINITY : mOutgoingMaxAge.Value());
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

// nsTArray<QueuedDatagram>'s generic cycle-collection support (nsTArray.h)
// needs these to reach mPromise; declared friend in QueuedDatagram's
// enclosing class since the type itself is private.
void ImplCycleCollectionTraverse(
    nsCycleCollectionTraversalCallback& aCallback,
    OutgoingDatagramStreamAlgorithms::QueuedDatagram& aField, const char* aName,
    uint32_t aFlags) {
  ImplCycleCollectionTraverse(aCallback, aField.mPromise, aName, aFlags);
}

void ImplCycleCollectionUnlink(
    OutgoingDatagramStreamAlgorithms::QueuedDatagram& aField) {
  ImplCycleCollectionUnlink(aField.mPromise);
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(OutgoingDatagramStreamAlgorithms,
                                   UnderlyingSinkAlgorithmsWrapper, mDatagrams,
                                   mSendGroup, mOwnerWritable,
                                   mWaitConnectQueue, mPendingDatagramPromises)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(OutgoingDatagramStreamAlgorithms)
NS_INTERFACE_MAP_END_INHERITING(UnderlyingSinkAlgorithmsWrapper)
NS_IMPL_ADDREF_INHERITED(OutgoingDatagramStreamAlgorithms,
                         UnderlyingSinkAlgorithmsWrapper)
NS_IMPL_RELEASE_INHERITED(OutgoingDatagramStreamAlgorithms,
                          UnderlyingSinkAlgorithmsWrapper)

already_AddRefed<Promise> OutgoingDatagramStreamAlgorithms::WriteCallbackImpl(
    JSContext* aCx, JS::Handle<JS::Value> aChunk,
    WritableStreamDefaultController& aController, ErrorResult& aError) {
  // https://w3c.github.io/webtransport/#writedatagrams
  // Step 1. Let timestamp be a timestamp representing now.
  TimeStamp now = TimeStamp::Now();

  // Step 2: If data is not a BufferSource object, then return a promise
  // rejected with a TypeError. { BufferSource == ArrayBuffer/ArrayBufferView }
  ArrayBufferViewOrArrayBuffer arrayBuffer;
  if (!arrayBuffer.Init(aCx, aChunk)) {
    JS_ClearPendingException(aCx);
    aError.ThrowTypeError("Wrong type for Datagram stream write"_ns);
    return nullptr;
  }

  // Step 3: Let datagrams be transport.[[Datagrams]].
  // (mDatagrams is transport.[[Datagrams]])

  nsTArray<uint8_t> data;
  (void)AppendTypedArrayDataTo(arrayBuffer, data);

  // Step 4: If datagrams.[[OutgoingMaxDatagramSize]] is less than data’s
  // [[ByteLength]], return a promise resolved with undefined.
  if (mDatagrams->mOutgoingMaxDataSize < static_cast<uint64_t>(data.Length())) {
    return Promise::CreateResolvedWithUndefined(mDatagrams->GetParentObject(),
                                                aError);
  }

  // Step 5: Let promise be a new Promise
  RefPtr<Promise> promise =
      Promise::CreateInfallible(mDatagrams->GetParentObject());

  // Assign unique datagram ID from the shared counter on the duplex stream
  uint64_t datagramId = mDatagrams->mNextDatagramId++;

  // mChild is set when we move to Connected
  if (mChild) {
    // Step 7: Let chunk be (bytes, timestamp, promise).
    // Step 8: Enqueue chunk to writable.[[OutgoingDatagramsQueue]].
    //
    // Not in spec: the promise is always tracked in mPendingDatagramPromises
    // so OnDatagramReady()/Cleanup() can find and settle it later.
    mPendingDatagramPromises.InsertOrUpdate(datagramId, promise);
    // Step 9: If the length of writable.[[OutgoingDatagramsQueue]] is less
    // than datagrams.[[OutgoingMaxBufferedDatagrams]], then resolve promise
    // with undefined.
    //
    // Not in spec: [[OutgoingDatagramsQueue]] lives in the parent, so the
    // count of tracked promises stands in for its length.
    if (mPendingDatagramPromises.Count() <
        mDatagrams->mOutgoingMaxBufferedDatagrams) {
      // Below outgoingMaxBufferedDatagrams: resolve immediately.
      promise->MaybeResolveWithUndefined();
    } else {
      // At or above outgoingMaxBufferedDatagrams: withhold the promise so the
      // WritableStream applies backpressure. Resolved later by
      // OnDatagramReady() once the datagram is sent or dropped.
    }

    // Register for DatagramReady routing. mOwnerWritable is null for the
    // default writable exposed via `.writable` (it isn't wrapped in its own
    // WebTransportDatagramsWritable), so this registers a null entry for it.
    // Every outstanding datagram id is always present in the map, so
    // OnDatagramReady can treat a missing entry as a bug rather than
    // silently guessing it belongs to the default writable.
    mDatagrams->mWritableDatagramMap.InsertOrUpdate(datagramId, mOwnerWritable);

    // We pass along the datagram to the parent with its ID.
    // The OutgoingDatagramsQueue lives in parent/neqo, and steps 6-9 are
    // implemented there. Parent will send DatagramReady when appropriate.
    LOG(("Sending Datagram id=%" PRIu64 ", size = %zu", datagramId,
         data.Length()));
    uint64_t sendGroupId = mSendGroup ? mSendGroup->GetGroupId() : 0;
    LOG(("Sending Datagram id=%" PRIu64 ", size = %zu, sendGroup = %" PRIu64
         ", sendOrder = %" PRId64,
         datagramId, data.Length(), sendGroupId, mSendOrder));
    mChild->SendOutgoingDatagram(
        datagramId, std::move(data), now, sendGroupId, mSendOrder,
        [](nsresult&&) {
          // IPC succeeded - actual promise resolution happens via DatagramReady
          LOG(("Datagram IPC succeeded"));
        },
        [self = RefPtr{this},
         datagramId](mozilla::ipc::ResponseRejectReason&&) {
          LOG(("Datagram IPC failed for id=%" PRIu64, datagramId));
          // IPC failed - remove from pending maps and resolve promise
          // immediately
          self->mDatagrams->mWritableDatagramMap.Remove(datagramId);
          if (Maybe<RefPtr<Promise>> promise =
                  self->mPendingDatagramPromises.Extract(datagramId)) {
            (*promise)->MaybeResolveWithUndefined();
          }
        });
  } else {
    LOG(("Queuing datagram id=%" PRIu64 " for connect", datagramId));
    // Queue locally until we can send it.
    // Multiple datagrams can be queued if they arrive before connection.
    mWaitConnectQueue.AppendElement(QueuedDatagram(
        datagramId, std::make_unique<DatagramEntry>(std::move(data), now),
        promise));
  }

  // Step 10: return promise
  return promise.forget();
}

// XXX should we allow datagrams to be sent before connect?  Check IETF spec
void OutgoingDatagramStreamAlgorithms::SetChild(WebTransportChild* aChild) {
  LOG(("Setting child in datagrams, %zu queued datagrams",
       mWaitConnectQueue.Length()));
  mChild = aChild;
  // Send all queued datagrams
  for (auto& queued : mWaitConnectQueue) {
    // Add promise to pending map so OnDatagramReady can resolve it
    mPendingDatagramPromises.InsertOrUpdate(queued.mDatagramId,
                                            queued.mPromise);

    // See the identical registration in WriteCallbackImpl.
    mDatagrams->mWritableDatagramMap.InsertOrUpdate(queued.mDatagramId,
                                                    mOwnerWritable);

    uint64_t sendGroupId = mSendGroup ? mSendGroup->GetGroupId() : 0;
    uint64_t datagramId = queued.mDatagramId;
    mChild->SendOutgoingDatagram(
        datagramId, queued.mEntry->mBuffer, queued.mEntry->mTimeStamp,
        sendGroupId, mSendOrder,
        [](nsresult&&) { LOG_VERBOSE(("Early Datagram IPC succeeded")); },
        [self = RefPtr{this},
         datagramId](mozilla::ipc::ResponseRejectReason&&) {
          LOG(("Early Datagram IPC failed for id=%" PRIu64, datagramId));
          // Remove from pending maps and resolve on failure
          self->mDatagrams->mWritableDatagramMap.Remove(datagramId);
          if (Maybe<RefPtr<Promise>> promise =
                  self->mPendingDatagramPromises.Extract(datagramId)) {
            (*promise)->MaybeResolveWithUndefined();
          }
        });
  }
  mWaitConnectQueue.Clear();
}

void WebTransportDatagramDuplexStream::OnDatagramReady(uint64_t aDatagramId) {
  Maybe<RefPtr<WebTransportDatagramsWritable>> entry =
      mWritableDatagramMap.Extract(aDatagramId);
  if (!entry) {
    // A missing entry means DatagramReady arrived for an id that was never
    // registered or already completed. Expected right after Cleanup()
    // (mChild can outlive it, e.g. after a remote close), a bug otherwise.
    MOZ_ASSERT(
        mCleanedUp,
        "OnDatagramReady for unregistered/already-completed datagram id");
    return;
  }
  RefPtr<WebTransportDatagramsWritable> writable = entry.extract();
  if (writable) {
    writable->OnDatagramReady(aDatagramId);
  } else {
    // Not in spec: the default writable exposed via our legacy `.writable`
    // attribute, which the current spec has removed from
    // WebTransportDatagramDuplexStream entirely (datagrams are only sent
    // via createWritable()-created writables now). It isn't wrapped in a
    // WebTransportDatagramsWritable object, so it registers a null entry
    // above instead of an owner.
    mOutgoingAlgorithms->OnDatagramReady(aDatagramId);
  }
}

void OutgoingDatagramStreamAlgorithms::OnDatagramReady(uint64_t aDatagramId) {
  LOG(("OnDatagramReady for id=%" PRIu64, aDatagramId));

  if (Maybe<RefPtr<Promise>> promise =
          mPendingDatagramPromises.Extract(aDatagramId)) {
    LOG(("Resolving promise for datagram id=%" PRIu64, aDatagramId));
    (*promise)->MaybeResolveWithUndefined();
  } else {
    LOG(("Warning: No pending promise found for datagram id=%" PRIu64,
         aDatagramId));
  }
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
  for (const auto& promise : mPendingDatagramPromises.Values()) {
    promise->MaybeResolveWithUndefined();
  }
  mPendingDatagramPromises.Clear();
  for (auto& queued : mWaitConnectQueue) {
    queued.mPromise->MaybeResolveWithUndefined();
  }
  mWaitConnectQueue.Clear();
}

void WebTransportDatagramDuplexStream::Cleanup() {
  mOutgoingAlgorithms->Cleanup();
  for (const auto& writable : mAllWritables) {
    writable->Cleanup();
  }
  mWritableDatagramMap.Clear();
  mCleanedUp = true;
}

// static
void WebTransportDatagramDuplexStream::CloseOrErrorWritable(
    JSContext* aCx, WritableStream* aWritable, bool aClean,
    JS::Handle<JS::Value> aErrorValue) {
  if (!aWritable) {
    return;
  }
  // Best-effort: one writable already closing/errored (e.g. a duplicate
  // call) must not stop the rest from being settled.
  if (aClean) {
    // https://w3c.github.io/webtransport/#webtransport-cleanup step 16.5
    // calls "close" on every writable unconditionally, but
    // WritableStreamClose() asserts the stream has no close already
    // queued/in-flight -- a race with the page's own writer.close() would
    // trip it. WritableStreamCloseQueuedOrInFlight isn't observable from
    // the public Streams API, so a fix belongs in WebTransport's
    // cleanup() algorithm, not here; skipping is safe meanwhile, since the
    // page's own close() call already settles the writable once the
    // underlying sink finishes closing.
    // See https://github.com/w3c/webtransport/issues/797
    if (aWritable->CloseQueuedOrInFlight()) {
      return;
    }
    RefPtr<Promise> ignored = aWritable->CloseNative(aCx, IgnoreErrors());
  } else {
    aWritable->ErrorNative(aCx, aErrorValue, IgnoreErrors());
  }
}

void WebTransportDatagramDuplexStream::CloseOrErrorWritables(
    JSContext* aCx, bool aClean, JS::Handle<JS::Value> aErrorValue) {
  CloseOrErrorWritable(aCx, MOZ_KnownLive(mWritable), aClean, aErrorValue);
  // Iterate a copy, since settling a writable must not invalidate the loop.
  nsTArray<RefPtr<WebTransportDatagramsWritable>> writables =
      mAllWritables.Clone();
  for (const auto& writable : writables) {
    CloseOrErrorWritable(aCx, MOZ_KnownLive(writable), aClean, aErrorValue);
  }
}

}  // namespace mozilla::dom
