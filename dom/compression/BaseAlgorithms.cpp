/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BaseAlgorithms.h"

#include "mozilla/dom/BufferSourceBinding.h"
#include "mozilla/dom/BufferSourceBindingFwd.h"
#include "mozilla/dom/TransformStreamDefaultController.h"
#include "mozilla/dom/UnionTypes.h"

namespace mozilla::dom::compression {

// Step 3 of
// https://compression.spec.whatwg.org/#dom-compressionstream-compressionstream
// Let transformAlgorithm be an algorithm which takes a chunk argument and
// runs the compress and enqueue a chunk algorithm with this and chunk.
MOZ_CAN_RUN_SCRIPT
void CompressionStreamAlgorithms::TransformCallbackImpl(
    JS::Handle<JS::Value> aChunk, TransformStreamDefaultController& aController,
    ErrorResult& aRv) {
  AutoJSAPI jsapi;
  if (!jsapi.Init(aController.GetParentObject())) {
    aRv.ThrowUnknownError("Internal error");
    return;
  }
  JSContext* cx = jsapi.cx();

  // https://compression.spec.whatwg.org/#compress-and-enqueue-a-chunk

  // Step 1: If chunk is not a BufferSource type, then throw a TypeError.
  RootedUnion<OwningBufferSource> bufferSource(cx);
  if (!bufferSource.Init(cx, aChunk)) {
    aRv.MightThrowJSException();
    aRv.StealExceptionFromJSContext(cx);
    return;
  }

  // Steps 2 - 4:
  JS::RootedVector<JSObject*> array(cx);
  ProcessTypedArraysFixed(bufferSource, [&](const Span<uint8_t>& aData) {
    Compress(cx, aData, &array, Flush::No, aRv);
  });
  if (aRv.Failed()) {
    return;
  }

  // Step 5: For each Uint8Array array, enqueue array in cs's transform.
  Enqueue(cx, array, aController, aRv);
}

// Step 4 of
// https://compression.spec.whatwg.org/#dom-compressionstream-compressionstream
// Let flushAlgorithm be an algorithm which takes no argument and runs the
// compress flush and enqueue algorithm with this.
MOZ_CAN_RUN_SCRIPT void CompressionStreamAlgorithms::FlushCallbackImpl(
    TransformStreamDefaultController& aController, ErrorResult& aRv) {
  AutoJSAPI jsapi;
  if (!jsapi.Init(aController.GetParentObject())) {
    aRv.ThrowUnknownError("Internal error");
    return;
  }
  JSContext* cx = jsapi.cx();

  // https://compression.spec.whatwg.org/#compress-flush-and-enqueue
  // Step 1-3:
  JS::RootedVector<JSObject*> arrays(cx);
  Compress(cx, Span<const uint8_t>(), &arrays, Flush::Yes, aRv);
  if (aRv.Failed()) {
    return;
  }

  // Step 4: For each Uint8Array array, enqueue array in cs's transform.
  Enqueue(cx, arrays, aController, aRv);
}

MOZ_CAN_RUN_SCRIPT void CompressionStreamAlgorithms::Enqueue(
    JSContext* aCx, JS::HandleVector<JSObject*> aArray,
    TransformStreamDefaultController& aController, ErrorResult& aRv) {
  // For each Uint8Array array, enqueue array in cs's transform.
  for (const auto& view : aArray) {
    JS::Rooted<JS::Value> value(aCx, JS::ObjectValue(*view));
    aController.Enqueue(aCx, value, aRv);
    if (aRv.Failed()) {
      return;
    }
  }
}

// Step 3 of
// https://compression.spec.whatwg.org/#dom-decompressionstream-decompressionstream
// Let transformAlgorithm be an algorithm which takes a chunk argument and
// runs the compress and enqueue a chunk algorithm with this and chunk.
MOZ_CAN_RUN_SCRIPT
void DecompressionStreamAlgorithms::TransformCallbackImpl(
    JS::Handle<JS::Value> aChunk, TransformStreamDefaultController& aController,
    ErrorResult& aRv) {
  AutoJSAPI jsapi;
  if (!jsapi.Init(aController.GetParentObject())) {
    aRv.ThrowUnknownError("Internal error");
    return;
  }
  JSContext* cx = jsapi.cx();

  // https://compression.spec.whatwg.org/#decompress-and-enqueue-a-chunk

  // Step 1: If chunk is not a BufferSource type, then throw a TypeError.
  RootedUnion<OwningBufferSource> bufferSource(cx);
  if (!bufferSource.Init(cx, aChunk)) {
    aRv.MightThrowJSException();
    aRv.StealExceptionFromJSContext(cx);
    return;
  }

  // Step 2: Let buffer be the result of decompressing chunk with ds's
  // format and context. If this results in an error, then throw a
  // TypeError.
  // Step 4: Let arrays be the result of splitting buffer into one or more
  // non-empty pieces and converting them into Uint8Arrays.
  JS::RootedVector<JSObject*> array(cx);
  bool fullyConsumed = false;
  ProcessTypedArraysFixed(bufferSource, [&](const Span<uint8_t>& aData) {
    fullyConsumed = Decompress(cx, aData, &array, Flush::No, aRv);
  });
  if (aRv.Failed()) {
    return;
  }

  // Step 5: For each Uint8Array array, enqueue array in ds's transform.
  Enqueue(cx, array, aController, aRv);
  if (aRv.Failed()) {
    return;
  }

  // Step 6: If the end of the compressed input has been reached, and ds's
  // context has not fully consumed chunk, then throw a TypeError.
  if (mObservedStreamEnd && !fullyConsumed) {
    aRv.ThrowTypeError("Unexpected input after the end of stream");
  }
}

// Step 4 of
// https://compression.spec.whatwg.org/#dom-decompressionstream-decompressionstream
// Let flushAlgorithm be an algorithm which takes no argument and runs the
// compress flush and enqueue algorithm with this.
MOZ_CAN_RUN_SCRIPT void DecompressionStreamAlgorithms::FlushCallbackImpl(
    TransformStreamDefaultController& aController, ErrorResult& aRv) {
  AutoJSAPI jsapi;
  if (!jsapi.Init(aController.GetParentObject())) {
    aRv.ThrowUnknownError("Internal error");
    return;
  }
  JSContext* cx = jsapi.cx();

  JS::RootedVector<JSObject*> array(cx);

  // https://compression.spec.whatwg.org/#decompress-flush-and-enqueue
  // Step 1 and 2.1:
  (void)Decompress(cx, Span<const uint8_t>(), &array, Flush::Yes, aRv);
  if (aRv.Failed()) {
    return;
  }

  // Step 2.2: For each Uint8Array array, enqueue array in ds's transform.
  Enqueue(cx, array, aController, aRv);
  if (aRv.Failed()) {
    return;
  }

  // Step 3: If the end of the compressed input has not been reached, then
  // throw a TypeError.
  if (!mObservedStreamEnd) {
    aRv.ThrowTypeError("The input is ended without reaching the stream end");
    return;
  }
}

MOZ_CAN_RUN_SCRIPT void DecompressionStreamAlgorithms::Enqueue(
    JSContext* aCx, JS::HandleVector<JSObject*> aArray,
    TransformStreamDefaultController& aController, ErrorResult& aRv) {
  // For each Uint8Array array, enqueue array in ds's transform.
  for (const auto& view : aArray) {
    JS::Rooted<JS::Value> value(aCx, JS::ObjectValue(*view));
    aController.Enqueue(aCx, value, aRv);
    if (aRv.Failed()) {
      return;
    }
  }
}

}  // namespace mozilla::dom::compression
