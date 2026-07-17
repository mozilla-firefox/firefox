/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebTransportDatagramDuplexStream.h"

#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/WebTransportLog.h"
#include <cmath>

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(WebTransportDatagramDuplexStream, mGlobal,
                                      mReadable, mWritable, mWebTransport,
                                      mIncomingAlgorithms, mOutgoingAlgorithms)
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

void WebTransportDatagramDuplexStream::Init(ErrorResult& aError) {
  // https://w3c.github.io/webtransport/#webtransport-constructor
  // We are only called synchronously from JS creating a WebTransport object
  AutoEntryScript aes(mGlobal, "WebTransportDatagrams");
  JSContext* cx = aes.cx();

  mIncomingAlgorithms = new IncomingDatagramStreamAlgorithms(this);
  nsCOMPtr<nsIGlobalObject> global(mGlobal);
  RefPtr<IncomingDatagramStreamAlgorithms> incomingAlgorithms =
      mIncomingAlgorithms;
  // Step 18: Set up incomingDatagrams with pullAlgorithm set to
  // pullDatagramsAlgorithm, and highWaterMark set to 0.
  mReadable = ReadableStream::CreateNative(cx, global, *incomingAlgorithms,
                                           Some(0.0), nullptr, aError);
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

void WebTransportDatagramDuplexStream::SetIncomingMaxAge(double aMaxAge,
                                                         ErrorResult& aRv) {
  // https://w3c.github.io/webtransport/#dom-webtransportdatagramduplexstream-incomingmaxage
  // Step 1
  if (std::isnan(aMaxAge) || aMaxAge < 0.) {
    aRv.ThrowRangeError("Invalid IncomingMaxAge");
    return;
  }
  // Step 2
  if (aMaxAge == 0) {
    aMaxAge = INFINITY;
  }
  // Step 3
  mIncomingMaxAge = aMaxAge;
}

void WebTransportDatagramDuplexStream::SetOutgoingMaxAge(double aMaxAge,
                                                         ErrorResult& aRv) {
  // https://w3c.github.io/webtransport/#dom-webtransportdatagramduplexstream-outgoingmaxage
  // Step 1
  if (std::isnan(aMaxAge) || aMaxAge < 0.) {
    aRv.ThrowRangeError("Invalid OutgoingMaxAge");
    return;
  }
  // Step 2
  if (aMaxAge == 0.) {
    aMaxAge = INFINITY;
  }
  // Step 3
  mOutgoingMaxAge = aMaxAge;
}

void WebTransportDatagramDuplexStream::SetIncomingHighWaterMark(
    double aWaterMark, ErrorResult& aRv) {
  // https://w3c.github.io/webtransport/#dom-webtransportdatagramduplexstream-incominghighwatermark
  // Step 1
  if (std::isnan(aWaterMark) || aWaterMark < 0.) {
    aRv.ThrowRangeError("Invalid OutgoingMaxAge");
    return;
  }
  // Step 2
  if (aWaterMark < 1.0) {
    aWaterMark = 1.0;
  }
  // Step 3
  mIncomingHighWaterMark = aWaterMark;
}

void WebTransportDatagramDuplexStream::SetOutgoingHighWaterMark(
    double aWaterMark, ErrorResult& aRv) {
  // https://w3c.github.io/webtransport/#dom-webtransportdatagramduplexstream-outgoinghighwatermark
  // Step 1
  if (std::isnan(aWaterMark) || aWaterMark < 0.) {
    aRv.ThrowRangeError("Invalid OutgoingHighWaterMark");
    return;
  }
  // Step 2 of setter: If value is < 1, set value to 1.
  if (aWaterMark < 1.0) {
    aWaterMark = 1.0;
  }
  // Step 3
  mOutgoingHighWaterMark = aWaterMark;
}

void WebTransportDatagramDuplexStream::NewDatagramReceived(
    nsTArray<uint8_t>&& aData, const mozilla::TimeStamp& aTimeStamp) {
  LOG(("received Datagram, size = %zu", aData.Length()));
  mIncomingDatagramsQueue.Push(UniquePtr<DatagramEntry>(
      new DatagramEntry(std::move(aData), aTimeStamp)));
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
  // Pull Step 5: Let bytes and timestamp be the result of dequeuing queue.
  UniquePtr<DatagramEntry> entry = mDatagrams->mIncomingDatagramsQueue.Pop();

  // Pull Step 6: Let chunk be a new Uint8Array object representing bytes.
  JSObject* outView = Uint8Array::Create(aCx, entry->mBuffer, aRv);
  if (aRv.Failed()) {
    return;
  }
  JS::Rooted<JSObject*> chunk(aCx, outView);

  // Pull Step 7: Enqueue chunk to transport.[[Datagrams]].[[Readable]]
  JS::Rooted<JS::Value> jsDatagram(aCx, JS::ObjectValue(*chunk));
  // EnqueueNative is CAN_RUN_SCRIPT
  RefPtr<ReadableStream> stream = mDatagrams->mReadable;
  stream->EnqueueNative(aCx, jsDatagram, aRv);
  if (MOZ_UNLIKELY(aRv.Failed())) {
    return;
  }
  // (caller) Step 8: return a promise resolved with Undefined
}

void IncomingDatagramStreamAlgorithms::NotifyDatagramAvailable() {
  if (RefPtr<Promise> promise = mIncomingDatagramsPullPromise.forget()) {
    promise->MaybeResolveWithUndefined();
  }
}

NS_IMPL_CYCLE_COLLECTION_INHERITED(OutgoingDatagramStreamAlgorithms,
                                   UnderlyingSinkAlgorithmsWrapper, mDatagrams,
                                   mWaitConnectPromise)

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

  // mChild is set when we move to Connected
  if (mChild) {
    // We pass along the datagram to the parent immediately.
    // The OutgoingDatagramsQueue lives there, and steps 6-9 generally are
    // implemented there
    LOG(("Sending Datagram, size = %zu", data.Length()));
    mChild->SendOutgoingDatagram(
        std::move(data), now,
        [promise](nsresult&&) {
          // XXX result
          LOG(("Datagram was sent"));
          promise->MaybeResolveWithUndefined();
        },
        [promise](mozilla::ipc::ResponseRejectReason&&) {
          LOG(("Datagram failed"));
          // there's no description in the spec of rejecting if a datagram
          // can't be sent; to the contrary, it says we should resolve with
          // undefined if we throw the datagram away
          promise->MaybeResolveWithUndefined();
        });
  } else {
    LOG(("Queuing datagram for connect"));
    // Queue locally until we can send it.
    // We should be guaranteed that we don't get called again until the
    // promise is resolved.
    MOZ_ASSERT(mWaitConnect == nullptr);
    mWaitConnect.reset(new DatagramEntry(std::move(data), now));
    mWaitConnectPromise = promise;
  }

  // Step 10: return promise
  return promise.forget();
}

// XXX should we allow datagrams to be sent before connect?  Check IETF spec
void OutgoingDatagramStreamAlgorithms::SetChild(WebTransportChild* aChild) {
  LOG(("Setting child in datagrams"));
  mChild = aChild;
  if (mWaitConnect) {
    LOG(("Sending queued datagram"));
    mChild->SendOutgoingDatagram(
        mWaitConnect->mBuffer, mWaitConnect->mTimeStamp,
        [promise = mWaitConnectPromise](nsresult&&) {
          LOG_VERBOSE(("Early Datagram was sent"));
          promise->MaybeResolveWithUndefined();
        },
        [promise = mWaitConnectPromise](mozilla::ipc::ResponseRejectReason&&) {
          LOG(("Early Datagram failed"));
          promise->MaybeResolveWithUndefined();
        });
    mWaitConnectPromise = nullptr;
    mWaitConnect.reset(nullptr);
  }
}

}  // namespace mozilla::dom
