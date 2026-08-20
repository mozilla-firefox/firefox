/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/UnderlyingSinkCallbackHelpers.h"

#include "StreamUtils.h"
#include "mozilla/dom/BufferSourceBinding.h"
#include "mozilla/dom/BufferSourceBindingFwd.h"
#include "mozilla/dom/UnionTypes.h"
#include "mozilla/dom/WebTransportError.h"
#include "nsHttp.h"

using namespace mozilla::dom;

NS_IMPL_CYCLE_COLLECTION(UnderlyingSinkAlgorithmsBase)
NS_IMPL_CYCLE_COLLECTING_ADDREF(UnderlyingSinkAlgorithmsBase)
NS_IMPL_CYCLE_COLLECTING_RELEASE(UnderlyingSinkAlgorithmsBase)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(UnderlyingSinkAlgorithmsBase)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_INHERITED_WITH_JS_MEMBERS(
    UnderlyingSinkAlgorithms, UnderlyingSinkAlgorithmsBase,
    (mGlobal, mStartCallback, mWriteCallback, mCloseCallback, mAbortCallback),
    (mUnderlyingSink))
NS_IMPL_ADDREF_INHERITED(UnderlyingSinkAlgorithms, UnderlyingSinkAlgorithmsBase)
NS_IMPL_RELEASE_INHERITED(UnderlyingSinkAlgorithms,
                          UnderlyingSinkAlgorithmsBase)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(UnderlyingSinkAlgorithms)
NS_INTERFACE_MAP_END_INHERITING(UnderlyingSinkAlgorithmsBase)

// https://streams.spec.whatwg.org/#set-up-writable-stream-default-controller-from-underlying-sink
void UnderlyingSinkAlgorithms::StartCallback(
    JSContext* aCx, WritableStreamDefaultController& aController,
    JS::MutableHandle<JS::Value> aRetVal, ErrorResult& aRv) {
  if (!mStartCallback) {
    // Step 2: Let startAlgorithm be an algorithm that returns undefined.
    aRetVal.setUndefined();
    return;
  }

  // Step 6: If underlyingSinkDict["start"] exists, then set startAlgorithm to
  // an algorithm which returns the result of invoking
  // underlyingSinkDict["start"] with argument list « controller » and callback
  // this value underlyingSink.
  JS::Rooted<JSObject*> thisObj(aCx, mUnderlyingSink);
  return mStartCallback->Call(thisObj, aController, aRetVal, aRv,
                              "UnderlyingSink.start",
                              CallbackFunction::eRethrowExceptions);
}

// https://streams.spec.whatwg.org/#set-up-writable-stream-default-controller-from-underlying-sink
already_AddRefed<Promise> UnderlyingSinkAlgorithms::WriteCallback(
    JSContext* aCx, JS::Handle<JS::Value> aChunk,
    WritableStreamDefaultController& aController, ErrorResult& aRv) {
  if (!mWriteCallback) {
    // Step 3: Let writeAlgorithm be an algorithm that returns a promise
    // resolved with undefined.
    return Promise::CreateResolvedWithUndefined(mGlobal, aRv);
  }

  // Step 7: If underlyingSinkDict["write"] exists, then set writeAlgorithm to
  // an algorithm which takes an argument chunk and returns the result of
  // invoking underlyingSinkDict["write"] with argument list « chunk, controller
  // » and callback this value underlyingSink.
  JS::Rooted<JSObject*> thisObj(aCx, mUnderlyingSink);
  RefPtr<Promise> promise = mWriteCallback->Call(
      thisObj, aChunk, aController, aRv, "UnderlyingSink.write",
      CallbackFunction::eRethrowExceptions);
  return promise.forget();
}

// https://streams.spec.whatwg.org/#set-up-writable-stream-default-controller-from-underlying-sink
already_AddRefed<Promise> UnderlyingSinkAlgorithms::CloseCallback(
    JSContext* aCx, ErrorResult& aRv) {
  if (!mCloseCallback) {
    // Step 4: Let closeAlgorithm be an algorithm that returns a promise
    // resolved with undefined.
    return Promise::CreateResolvedWithUndefined(mGlobal, aRv);
  }

  // Step 8: If underlyingSinkDict["close"] exists, then set closeAlgorithm to
  // an algorithm which returns the result of invoking
  // underlyingSinkDict["close"] with argument list «» and callback this value
  // underlyingSink.
  JS::Rooted<JSObject*> thisObj(aCx, mUnderlyingSink);
  RefPtr<Promise> promise =
      mCloseCallback->Call(thisObj, aRv, "UnderlyingSink.close",
                           CallbackFunction::eRethrowExceptions);
  return promise.forget();
}

// https://streams.spec.whatwg.org/#set-up-writable-stream-default-controller-from-underlying-sink
already_AddRefed<Promise> UnderlyingSinkAlgorithms::AbortCallback(
    JSContext* aCx, const Optional<JS::Handle<JS::Value>>& aReason,
    ErrorResult& aRv) {
  if (!mAbortCallback) {
    // Step 5: Let abortAlgorithm be an algorithm that returns a promise
    // resolved with undefined.
    return Promise::CreateResolvedWithUndefined(mGlobal, aRv);
  }

  // Step 9: Let abortAlgorithm be an algorithm that returns a promise resolved
  // with undefined.
  JS::Rooted<JSObject*> thisObj(aCx, mUnderlyingSink);
  RefPtr<Promise> promise =
      mAbortCallback->Call(thisObj, aReason, aRv, "UnderlyingSink.abort",
                           CallbackFunction::eRethrowExceptions);

  return promise.forget();
}

// https://streams.spec.whatwg.org/#writable-set-up
// This one is not covered by the above section as the spec expects any spec
// implementation to explicitly return a promise for this callback. It's still
// useful for Gecko as error handling is very frequently done with
// ErrorResult instead of a rejected promise. See also
// https://github.com/whatwg/streams/issues/1253.
already_AddRefed<Promise> UnderlyingSinkAlgorithmsWrapper::WriteCallback(
    JSContext* aCx, JS::Handle<JS::Value> aChunk,
    WritableStreamDefaultController& aController, ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = xpc::CurrentNativeGlobal(aCx);
  return PromisifyAlgorithm(
      global,
      [&](ErrorResult& aRv) {
        return WriteCallbackImpl(aCx, aChunk, aController, aRv);
      },
      aRv);
}

// https://streams.spec.whatwg.org/#writable-set-up
// Step 2.1: Let closeAlgorithmWrapper be an algorithm that runs these steps:
already_AddRefed<Promise> UnderlyingSinkAlgorithmsWrapper::CloseCallback(
    JSContext* aCx, ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = xpc::CurrentNativeGlobal(aCx);
  return PromisifyAlgorithm(
      global, [&](ErrorResult& aRv) { return CloseCallbackImpl(aCx, aRv); },
      aRv);
}

// https://streams.spec.whatwg.org/#writable-set-up
// Step 3.1: Let abortAlgorithmWrapper be an algorithm that runs these steps:
already_AddRefed<Promise> UnderlyingSinkAlgorithmsWrapper::AbortCallback(
    JSContext* aCx, const Optional<JS::Handle<JS::Value>>& aReason,
    ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = xpc::CurrentNativeGlobal(aCx);
  return PromisifyAlgorithm(
      global,
      [&](ErrorResult& aRv) { return AbortCallbackImpl(aCx, aReason, aRv); },
      aRv);
}

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED(WritableStreamToOutputAlgorithms,
                                             UnderlyingSinkAlgorithmsBase,
                                             nsIOutputStreamCallback)
NS_IMPL_CYCLE_COLLECTION_INHERITED(WritableStreamToOutputAlgorithms,
                                   UnderlyingSinkAlgorithmsBase, mParent,
                                   mOutput, mPromise)

NS_IMETHODIMP
WritableStreamToOutputAlgorithms::OnOutputStreamReady(
    nsIAsyncOutputStream* aStream) {
  if (!mData) {
    return NS_OK;
  }
  MOZ_ASSERT(mPromise);
  uint32_t written = 0;
  nsresult rv = mOutput->Write(
      reinterpret_cast<const char*>(mData->Elements() + mWritten),
      mData->Length() - mWritten, &written);
  if (NS_FAILED(rv) && rv != NS_BASE_STREAM_WOULD_BLOCK) {
    mPromise->MaybeRejectWithAbortError("Error writing to stream"_ns);
    ClearData();
    // XXX should we add mErrored and fail future calls immediately?
    // I presume new calls to Write() will fail though, too
    return rv;
  }
  if (NS_SUCCEEDED(rv)) {
    mWritten += written;
    MOZ_ASSERT(mWritten <= mData->Length());
    if (mWritten >= mData->Length()) {
      mPromise->MaybeResolveWithUndefined();
      ClearData();
      return NS_OK;
    }
    // more to write
  }
  // wrote partial or nothing
  // Wait for space
  nsCOMPtr<nsIEventTarget> target = mozilla::GetCurrentSerialEventTarget();
  rv = mOutput->AsyncWait(this, 0, 0, target);
  if (NS_FAILED(rv)) {
    mPromise->MaybeRejectWithUnknownError("error waiting to write data"_ns);
    ClearData();
    // XXX should we add mErrored and fail future calls immediately?
    // New calls to Write() will fail, note
    // See step 5.2 of
    // https://streams.spec.whatwg.org/#writable-stream-default-controller-process-write.
    return rv;
  }
  return NS_OK;
}

already_AddRefed<Promise> WritableStreamToOutputAlgorithms::WriteCallbackImpl(
    JSContext* aCx, JS::Handle<JS::Value> aChunk,
    WritableStreamDefaultController& aController, ErrorResult& aRv) {
  BufferSource data;
  if (!data.Init(aCx, aChunk)) {
    aRv.MightThrowJSException();
    aRv.StealExceptionFromJSContext(aCx);
    return nullptr;
  }
  // buffer/bufferView
  MOZ_ASSERT(data.IsArrayBuffer() || data.IsArrayBufferView());

  RefPtr<Promise> promise = Promise::Create(mParent, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  // Try to write first, and only enqueue data if we were already blocked
  // or the write didn't write it all.  This avoids allocations and copies
  // in common cases.
  MOZ_ASSERT(!mPromise);
  MOZ_ASSERT(mWritten == 0);
  uint32_t written = 0;
  ProcessTypedArraysFixed(data, [&](const Span<uint8_t>& aData) {
    Span<uint8_t> dataSpan = aData;
    nsresult rv = mOutput->Write(mozilla::AsChars(dataSpan).Elements(),
                                 dataSpan.Length(), &written);
    if (NS_FAILED(rv) && rv != NS_BASE_STREAM_WOULD_BLOCK) {
      promise->MaybeRejectWithAbortError("error writing data"_ns);
      return;
    }
    if (NS_SUCCEEDED(rv)) {
      if (written == dataSpan.Length()) {
        promise->MaybeResolveWithUndefined();
        return;
      }
      dataSpan = dataSpan.From(written);
    }

    auto buffer = Buffer<uint8_t>::CopyFrom(dataSpan);
    if (buffer.isNothing()) {
      promise->MaybeReject(NS_ERROR_OUT_OF_MEMORY);
      return;
    }
    mData = std::move(buffer);
  });

  if (promise->State() != Promise::PromiseState::Pending) {
    return promise.forget();
  }

  mPromise = promise;

  nsCOMPtr<nsIEventTarget> target = mozilla::GetCurrentSerialEventTarget();
  nsresult rv = mOutput->AsyncWait(this, 0, 0, target);
  if (NS_FAILED(rv)) {
    ClearData();
    promise->MaybeRejectWithUnknownError("error waiting to write data"_ns);
  }
  return promise.forget();
}

already_AddRefed<Promise> WritableStreamToOutputAlgorithms::AbortCallbackImpl(
    JSContext* aCx, const Optional<JS::Handle<JS::Value>>& aReason,
    ErrorResult& aRv) {
  // https://streams.spec.whatwg.org/#writablestream-set-up
  // Step 3. Let abortAlgorithmWrapper be an algorithm that runs these steps:

  // XXX The close or rather a dedicated abort should be async. For now we have
  // to always fall back to the Step 3.3 below.
  mOutput->CloseWithStatus(BuildErrorStatus(aCx, aReason));

  // Step 3.3. Return a promise resolved with undefined.
  // Wrapper handles this
  return nullptr;
}

nsresult WritableStreamToOutputAlgorithms::BuildErrorStatus(
    JSContext* aCx, const Optional<JS::Handle<JS::Value>>& aReason) {
  if (aReason.WasPassed() && aReason.Value().isObject()) {
    JS::Rooted<JSObject*> obj(aCx, &aReason.Value().toObject());
    RefPtr<WebTransportError> error;
    UnwrapObject<prototypes::id::WebTransportError, WebTransportError>(
        obj, error, nullptr);
    if (error && !error->GetStreamErrorCode().IsNull()) {
      return net::GetNSResultFromWebTransportError(
          error->GetStreamErrorCode().Value());
    }
  }

  // XXX how do we know this stream is used by webtransport?
  return NS_ERROR_WEBTRANSPORT_CODE_BASE;
}

void WritableStreamToOutputAlgorithms::ReleaseObjects() { mOutput->Close(); }
