/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/WebTransportStreams.h"

#include "mozilla/Result.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/WebTransport.h"
#include "mozilla/dom/WebTransportBidirectionalStream.h"
#include "mozilla/dom/WebTransportLog.h"
#include "mozilla/dom/WebTransportReceiveStream.h"
#include "mozilla/dom/WebTransportSendStream.h"

using namespace mozilla::ipc;

namespace mozilla::dom {
NS_IMPL_CYCLE_COLLECTION_INHERITED(WebTransportIncomingStreamsAlgorithms,
                                   UnderlyingSourceAlgorithmsWrapper,
                                   mTransport, mCallback)
NS_IMPL_ADDREF_INHERITED(WebTransportIncomingStreamsAlgorithms,
                         UnderlyingSourceAlgorithmsWrapper)
NS_IMPL_RELEASE_INHERITED(WebTransportIncomingStreamsAlgorithms,
                          UnderlyingSourceAlgorithmsWrapper)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WebTransportIncomingStreamsAlgorithms)
NS_INTERFACE_MAP_END_INHERITING(UnderlyingSourceAlgorithmsWrapper)

WebTransportIncomingStreamsAlgorithms::WebTransportIncomingStreamsAlgorithms(
    StreamType aUnidirectional, WebTransport* aTransport)
    : mUnidirectional(aUnidirectional), mTransport(aTransport) {}

WebTransportIncomingStreamsAlgorithms::
    ~WebTransportIncomingStreamsAlgorithms() = default;

already_AddRefed<Promise>
WebTransportIncomingStreamsAlgorithms::PullCallbackImpl(
    JSContext* aCx, ReadableStreamControllerBase& aController,
    ErrorResult& aRv) {
  // https://w3c.github.io/webtransport/#pullbidirectionalstream and
  // https://w3c.github.io/webtransport/#pullunidirectionalstream

  // Step 1: If transport.[[State]] is "connecting", then return the result
  // of performing the following steps upon fulfillment of
  // transport.[[Ready]]:
  // We don't explicitly check mState here, since we'll reject
  // mIncomingStreamPromise if we go to FAILED or CLOSED
  //
  // Step 2: Let session be transport.[[Session]].
  // Step 3: Let p be a new promise.
  RefPtr<Promise> promise =
      Promise::CreateInfallible(mTransport->GetParentObject());
  RefPtr<WebTransportIncomingStreamsAlgorithms> self(this);
  // The real work of PullCallback()
  // Step 5: Wait until there is an available incoming unidirectional stream.
  auto length = (mUnidirectional == StreamType::Unidirectional)
                    ? mTransport->mUnidirectionalStreams.Length()
                    : mTransport->mBidirectionalStreams.Length();
  if (length == 0) {
    // We need to wait.
    // Per
    // https://streams.spec.whatwg.org/#readablestreamdefaultcontroller-pulling
    // we can't be called again until the promise is resolved
    MOZ_ASSERT(!mCallback);
    mCallback = promise;

    LOG(("Incoming%sDirectionalStreams Pull waiting for a stream",
         mUnidirectional == StreamType::Unidirectional ? "Uni" : "Bi"));
    Result<RefPtr<Promise>, nsresult> returnResult =
        promise->ThenWithCycleCollectedArgs(
            [](JSContext* aCx, JS::Handle<JS::Value>, ErrorResult& aRv,
               RefPtr<WebTransportIncomingStreamsAlgorithms> self,
               RefPtr<Promise> aPromise) -> already_AddRefed<Promise> {
              self->BuildStream(aCx, aRv);
              return nullptr;
            },
            self, promise);
    if (returnResult.isErr()) {
      // XXX Reject?
      aRv.Throw(returnResult.unwrapErr());
      return nullptr;
    }
    // Step 4: Return p and run the remaining steps in parallel.
    return returnResult.unwrap().forget();
  }
  self->BuildStream(aCx, aRv);
  if (aRv.Failed()) {
    promise->MaybeReject(aRv.StealNSResult());
    return promise.forget();
  }
  // Step 7.3: Resolve p with undefined.
  promise->MaybeResolveWithUndefined();
  return promise.forget();
}

// Note: fallible
void WebTransportIncomingStreamsAlgorithms::BuildStream(JSContext* aCx,
                                                        ErrorResult& aRv) {
  // https://w3c.github.io/webtransport/#pullbidirectionalstream and
  // https://w3c.github.io/webtransport/#pullunidirectionalstream
  LOG(("Incoming%sDirectionalStreams Pull building a stream",
       mUnidirectional == StreamType::Unidirectional ? "Uni" : "Bi"));
  if (mUnidirectional == StreamType::Unidirectional) {
    // Step 6: Let internalStream be the result of receiving an incoming
    // unidirectional stream.
    MOZ_ASSERT(mTransport->mUnidirectionalStreams.Length() > 0);
    std::tuple<uint64_t, RefPtr<mozilla::ipc::DataPipeReceiver>> tuple =
        mTransport->mUnidirectionalStreams[0];
    mTransport->mUnidirectionalStreams.RemoveElementAt(0);

    // Step 7.1: Let stream be the result of creating a
    // WebTransportReceiveStream with internalStream and transport
    RefPtr<WebTransportReceiveStream> readableStream =
        WebTransportReceiveStream::Create(mTransport, mTransport->mGlobal,
                                          std::get<0>(tuple),
                                          std::get<1>(tuple), aRv);
    if (MOZ_UNLIKELY(!readableStream)) {
      aRv.ThrowUnknownError("Internal error");
      return;
    }
    // Step 7.2 Enqueue stream to transport.[[IncomingUnidirectionalStreams]].
    JS::Rooted<JS::Value> jsStream(aCx);
    if (MOZ_UNLIKELY(!ToJSValue(aCx, readableStream, &jsStream))) {
      aRv.ThrowUnknownError("Internal error");
      return;
    }
    // EnqueueNative is CAN_RUN_SCRIPT
    RefPtr<ReadableStream> incomingStream =
        mTransport->mIncomingUnidirectionalStreams;
    incomingStream->EnqueueNative(aCx, jsStream, aRv);
    if (MOZ_UNLIKELY(aRv.Failed())) {
      aRv.ThrowUnknownError("Internal error");
      return;
    }
  } else {
    // Step 6: Let internalStream be the result of receiving a bidirectional
    // stream
    MOZ_ASSERT(mTransport->mBidirectionalStreams.Length() > 0);
    std::tuple<uint64_t, UniquePtr<BidirectionalPair>> tuple =
        std::move(mTransport->mBidirectionalStreams.ElementAt(0));
    mTransport->mBidirectionalStreams.RemoveElementAt(0);
    RefPtr<DataPipeReceiver> input = std::get<1>(tuple)->first.forget();
    RefPtr<DataPipeSender> output = std::get<1>(tuple)->second.forget();

    RefPtr<WebTransportBidirectionalStream> stream =
        WebTransportBidirectionalStream::Create(mTransport, mTransport->mGlobal,
                                                std::get<0>(tuple), input,
                                                output, 0, nullptr, aRv);

    // Step 7.2 Enqueue stream to transport.[[IncomingBidirectionalStreams]].
    JS::Rooted<JS::Value> jsStream(aCx);
    if (MOZ_UNLIKELY(!ToJSValue(aCx, stream, &jsStream))) {
      return;
    }
    LOG(("Enqueuing bidirectional stream\n"));
    // EnqueueNative is CAN_RUN_SCRIPT
    RefPtr<ReadableStream> incomingStream =
        mTransport->mIncomingBidirectionalStreams;
    incomingStream->EnqueueNative(aCx, jsStream, aRv);
    if (MOZ_UNLIKELY(aRv.Failed())) {
      return;
    }
  }
  // Step 7.3: Resolve p with undefined.
}

void WebTransportIncomingStreamsAlgorithms::NotifyIncomingStream() {
  if (mUnidirectional == StreamType::Unidirectional) {
    LOG(("NotifyIncomingStream: %zu Unidirectional ",
         mTransport->mUnidirectionalStreams.Length()));
#ifdef DEBUG
    auto number = mTransport->mUnidirectionalStreams.Length();
    MOZ_ASSERT(number > 0);
#endif
    RefPtr<Promise> promise = mCallback.forget();
    if (promise) {
      promise->MaybeResolveWithUndefined();
    }
  } else {
    LOG(("NotifyIncomingStream: %zu Bidirectional ",
         mTransport->mBidirectionalStreams.Length()));
#ifdef DEBUG
    auto number = mTransport->mBidirectionalStreams.Length();
    MOZ_ASSERT(number > 0);
#endif
    RefPtr<Promise> promise = mCallback.forget();
    if (promise) {
      promise->MaybeResolveWithUndefined();
    }
  }
}

void WebTransportIncomingStreamsAlgorithms::NotifyRejectAll() {
  // cancel all pulls
  LOG(("Cancel all WebTransport Pulls"));
  // Ensure we clear the callback before resolving/rejecting it
  if (RefPtr<Promise> promise = mCallback.forget()) {
    promise->MaybeReject(NS_ERROR_FAILURE);
  }
}

}  // namespace mozilla::dom
