/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/TextEncoderStream.h"

#include "js/ArrayBuffer.h"
#include "js/experimental/TypedData.h"
#include "nsIGlobalObject.h"
#include "mozilla/Encoding.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/TextEncoderStreamBinding.h"
#include "mozilla/dom/TransformerCallbackHelpers.h"
#include "mozilla/dom/TransformStream.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(TextEncoderStream, mGlobal, mStream)
NS_IMPL_CYCLE_COLLECTING_ADDREF(TextEncoderStream)
NS_IMPL_CYCLE_COLLECTING_RELEASE(TextEncoderStream)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TextEncoderStream)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

TextEncoderStream::TextEncoderStream(nsISupports* aGlobal,
                                     TransformStream& aStream)
    : mGlobal(aGlobal), mStream(&aStream) {
// See the comment in EncodeNative() about why this uses a decoder instead of
// `UTF_8_ENCODING->NewEncoder()`.
// XXX: We have to consciously choose 16LE/BE because we ultimately have to read
// char16_t* as uint8_t*. See the same comment.
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  mDecoder = UTF_16LE_ENCODING->NewDecoder();
#else
  mDecoder = UTF_16BE_ENCODING->NewDecoder();
#endif
}

TextEncoderStream::~TextEncoderStream() = default;

JSObject* TextEncoderStream::WrapObject(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return TextEncoderStream_Binding::Wrap(aCx, this, aGivenProto);
}

// Note that the most of the encoding algorithm is implemented in
// mozilla::Decoder (see the comment in EncodeNative()), and this is mainly
// about calling it properly.
static void EncodeNative(JSContext* aCx, mozilla::Decoder* aDecoder,
                         Span<const char16_t> aInput, const bool aFlush,
                         JS::MutableHandle<JSObject*> aOutputArrayBufferView,
                         ErrorResult& aRv) {
  // XXX: Adjust the length since Decoder always accepts uint8_t (whereas
  // Encoder also accepts char16_t, see below).
  if (aInput.Length() > SIZE_MAX / 2) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }
  size_t lengthU8 = aInput.Length() * 2;

  CheckedInt<nsAString::size_type> needed =
      aDecoder->MaxUTF8BufferLength(lengthU8);
  if (!needed.isValid()) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  UniquePtr<uint8_t> buffer(
      static_cast<uint8_t*>(JS_malloc(aCx, needed.value())));
  if (!buffer) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  mozilla::Span<uint8_t> input((uint8_t*)aInput.data(), lengthU8);
  mozilla::Span<uint8_t> output(buffer, needed.value());

  // This originally wanted to use mozilla::Encoder::Encode() that accepts
  // char16_t*, but it lacks the pending-high-surrogate feature to properly
  // implement
  // https://encoding.spec.whatwg.org/#convert-code-unit-to-scalar-value.
  // See also https://github.com/hsivonen/encoding_rs/issues/82 about the
  // reasoning.
  // XXX: The code is more verbose here since we need to convert to
  // uint8_t* which is the only type mozilla::Decoder accepts.
  uint32_t result;
  size_t read;
  size_t written;
  std::tie(result, read, written, std::ignore) =
      aDecoder->DecodeToUTF8(input, output, aFlush);
  MOZ_ASSERT(result == kInputEmpty);
  MOZ_ASSERT(read == lengthU8);
  MOZ_ASSERT(written <= needed.value());

  // https://encoding.spec.whatwg.org/#encode-and-enqueue-a-chunk
  // Step 4.2.2.1. Let chunk be a Uint8Array object wrapping an ArrayBuffer
  // containing output.
  JS::Rooted<JSObject*> arrayBuffer(
      aCx, JS::NewArrayBufferWithContents(aCx, written, buffer.release()));
  if (!arrayBuffer.get()) {
    JS_ClearPendingException(aCx);
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }
  aOutputArrayBufferView.set(JS_NewUint8ArrayWithBuffer(
      aCx, arrayBuffer, 0, static_cast<int64_t>(written)));
  if (!aOutputArrayBufferView.get()) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }
}

class TextEncoderStreamAlgorithms : public TransformerAlgorithmsWrapper {
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(TextEncoderStreamAlgorithms,
                                           TransformerAlgorithmsBase)

  void SetEncoderStream(TextEncoderStream& aStream) {
    mEncoderStream = &aStream;
  }

  // The common part of encode-and-enqueue and encode-and-flush.
  // https://encoding.spec.whatwg.org/#decode-and-enqueue-a-chunk
  MOZ_CAN_RUN_SCRIPT void EncodeAndEnqueue(
      JSContext* aCx, const nsAString& aInput,
      TransformStreamDefaultController& aController, bool aFlush,
      ErrorResult& aRv) {
    JS::Rooted<JSObject*> outView(aCx);
    // Passing a Decoder for a reason, see the comments in the method.
    EncodeNative(aCx, mEncoderStream->Decoder(), aInput, aFlush, &outView, aRv);

    if (JS_GetTypedArrayLength(outView) > 0) {
      // Step 4.2.2.2. Enqueue chunk into encoder’s transform.
      JS::Rooted<JS::Value> value(aCx, JS::ObjectValue(*outView));
      aController.Enqueue(aCx, value, aRv);
    }
  }

  // https://encoding.spec.whatwg.org/#dom-textencoderstream
  MOZ_CAN_RUN_SCRIPT void TransformCallbackImpl(
      JSContext* aCx, JS::Handle<JS::Value> aChunk,
      TransformStreamDefaultController& aController,
      ErrorResult& aRv) override {
    // Step 2. Let transformAlgorithm be an algorithm which takes a chunk
    // argument and runs the encode and enqueue a chunk algorithm with this and
    // chunk.

    // https://encoding.spec.whatwg.org/#encode-and-enqueue-a-chunk

    // Step 1. Let input be the result of converting chunk to a DOMString.
    // Step 2. Convert input to an I/O queue of code units.
    nsString str;
    if (!ConvertJSValueToString(aCx, aChunk, eStringify, eStringify, str)) {
      aRv.MightThrowJSException();
      aRv.StealExceptionFromJSContext(aCx);
      return;
    }

    EncodeAndEnqueue(aCx, str, aController, false, aRv);
  }

  // https://encoding.spec.whatwg.org/#dom-textencoderstream
  MOZ_CAN_RUN_SCRIPT void FlushCallbackImpl(
      JSContext* aCx, TransformStreamDefaultController& aController,
      ErrorResult& aRv) override {
    // Step 3. Let flushAlgorithm be an algorithm which runs the encode and
    // flush algorithm with this.

    // The spec manually manages pending high surrogate here, but let's call the
    // encoder as it's managed there.
    EncodeAndEnqueue(aCx, u""_ns, aController, true, aRv);
  }

 private:
  ~TextEncoderStreamAlgorithms() override = default;

  RefPtr<TextEncoderStream> mEncoderStream;
};

NS_IMPL_CYCLE_COLLECTION_INHERITED(TextEncoderStreamAlgorithms,
                                   TransformerAlgorithmsBase, mEncoderStream)
NS_IMPL_ADDREF_INHERITED(TextEncoderStreamAlgorithms, TransformerAlgorithmsBase)
NS_IMPL_RELEASE_INHERITED(TextEncoderStreamAlgorithms,
                          TransformerAlgorithmsBase)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TextEncoderStreamAlgorithms)
NS_INTERFACE_MAP_END_INHERITING(TransformerAlgorithmsBase)

// https://encoding.spec.whatwg.org/#dom-textencoderstream
already_AddRefed<TextEncoderStream> TextEncoderStream::Constructor(
    const GlobalObject& aGlobal, ErrorResult& aRv) {
  // Step 1. Set this’s encoder to an instance of the UTF-8 encoder.

  // Step 2-3
  auto algorithms = MakeRefPtr<TextEncoderStreamAlgorithms>();

  // Step 4-5
  RefPtr<TransformStream> transformStream =
      TransformStream::CreateGeneric(aGlobal, *algorithms, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  // Step 6. Set this’s transform to transformStream.
  // (Done in the constructor)
  auto encoderStream =
      MakeRefPtr<TextEncoderStream>(aGlobal.GetAsSupports(), *transformStream);
  algorithms->SetEncoderStream(*encoderStream);
  return encoderStream.forget();
}

ReadableStream* TextEncoderStream::Readable() const {
  return mStream->Readable();
}

WritableStream* TextEncoderStream::Writable() const {
  return mStream->Writable();
}

void TextEncoderStream::GetEncoding(nsCString& aRetVal) const {
  aRetVal.AssignLiteral("utf-8");
}

}  // namespace mozilla::dom
