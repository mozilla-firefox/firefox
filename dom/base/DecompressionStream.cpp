/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/DecompressionStream.h"

#include "js/TypeDecls.h"
#include "mozilla/Assertions.h"
#include "mozilla/dom/DecompressionStreamBinding.h"
#include "mozilla/dom/ReadableStream.h"
#include "mozilla/dom/WritableStream.h"
#include "mozilla/dom/TextDecoderStream.h"
#include "mozilla/dom/TransformStream.h"
#include "mozilla/dom/TransformerCallbackHelpers.h"

#include "ZLibHelper.h"

// See the zlib manual in https://www.zlib.net/manual.html or in
// https://searchfox.org/mozilla-central/source/modules/zlib/src/zlib.h

namespace mozilla::dom {

class DecompressionStreamAlgorithms : public TransformerAlgorithmsWrapper {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(DecompressionStreamAlgorithms,
                                           TransformerAlgorithmsBase)

  explicit DecompressionStreamAlgorithms(CompressionFormat format) {
    int8_t err = inflateInit2(&mZStream, ZLibWindowBits(format));
    if (err == Z_MEM_ERROR) {
      MOZ_CRASH("Out of memory");
    }
    MOZ_ASSERT(err == Z_OK);
  }

  // Step 3 of
  // https://wicg.github.io/compression/#dom-compressionstream-compressionstream
  // Let transformAlgorithm be an algorithm which takes a chunk argument and
  // runs the compress and enqueue a chunk algorithm with this and chunk.
  MOZ_CAN_RUN_SCRIPT
  void TransformCallbackImpl(JSContext* aCx, JS::Handle<JS::Value> aChunk,
                             TransformStreamDefaultController& aController,
                             ErrorResult& aRv) override {
    // https://wicg.github.io/compression/#compress-and-enqueue-a-chunk

    // Step 1: If chunk is not a BufferSource type, then throw a TypeError.
    // (ExtractSpanFromBufferSource does it)
    Span<const uint8_t> input = ExtractSpanFromBufferSource(aCx, aChunk, aRv);
    if (aRv.Failed()) {
      return;
    }

    // Step 2: Let buffer be the result of decompressing chunk with ds's format
    // and context. If this results in an error, then throw a TypeError.
    // Step 3 - 5: (Done in CompressAndEnqueue)
    DecompressAndEnqueue(aCx, input, ZLibFlush::No, aController, aRv);
  }

  // Step 4 of
  // https://wicg.github.io/compression/#dom-compressionstream-compressionstream
  // Let flushAlgorithm be an algorithm which takes no argument and runs the
  // compress flush and enqueue algorithm with this.
  MOZ_CAN_RUN_SCRIPT void FlushCallbackImpl(
      JSContext* aCx, TransformStreamDefaultController& aController,
      ErrorResult& aRv) override {
    // https://wicg.github.io/compression/#decompress-flush-and-enqueue

    // Step 1: Let buffer be the result of decompressing an empty input with
    // ds's format and context, with the finish flag.
    // Step 2 - 4: (Done in CompressAndEnqueue)
    DecompressAndEnqueue(aCx, Span<const uint8_t>(), ZLibFlush::Yes,
                         aController, aRv);
  }

 private:
  // Shared by:
  // https://wicg.github.io/compression/#decompress-and-enqueue-a-chunk
  // https://wicg.github.io/compression/#decompress-flush-and-enqueue
  // All data errors throw TypeError by step 2: If this results in an error,
  // then throw a TypeError.
  MOZ_CAN_RUN_SCRIPT void DecompressAndEnqueue(
      JSContext* aCx, Span<const uint8_t> aInput, ZLibFlush aFlush,
      TransformStreamDefaultController& aController, ErrorResult& aRv) {
    MOZ_ASSERT_IF(aFlush == ZLibFlush::Yes, !aInput.Length());

    mZStream.avail_in = aInput.Length();
    mZStream.next_in = const_cast<uint8_t*>(aInput.Elements());

    JS::RootedVector<JSObject*> array(aCx);

    do {
      static uint16_t kBufferSize = 16384;
      UniquePtr<uint8_t> buffer(
          static_cast<uint8_t*>(JS_malloc(aCx, kBufferSize)));
      if (!buffer) {
        aRv.ThrowTypeError("Out of memory");
        return;
      }

      mZStream.avail_out = kBufferSize;
      mZStream.next_out = buffer.get();

      int8_t err = inflate(&mZStream, aFlush);

      // From the manual: inflate() returns ...
      switch (err) {
        case Z_DATA_ERROR:
          // Z_DATA_ERROR if the input data was corrupted (input stream not
          // conforming to the zlib format or incorrect check value, in which
          // case strm->msg points to a string with a more specific error)
          aRv.ThrowTypeError("The input data is corrupted: "_ns +
                             nsDependentCString(mZStream.msg));
          return;
        case Z_MEM_ERROR:
          // Z_MEM_ERROR if there was not enough memory
          aRv.ThrowTypeError("Out of memory");
          return;
        case Z_NEED_DICT:
          // Z_NEED_DICT if a preset dictionary is needed at this point
          //
          // From the `deflate` section of
          // https://wicg.github.io/compression/#supported-formats:
          // * The FDICT flag is not supported by these APIs, and will error the
          // stream if set.
          // And FDICT means preset dictionary per
          // https://datatracker.ietf.org/doc/html/rfc1950#page-5.
          aRv.ThrowTypeError(
              "The stream needs a preset dictionary but such setup is "
              "unsupported");
          return;
        case Z_STREAM_END:
          // Z_STREAM_END if the end of the compressed data has been reached and
          // all uncompressed output has been produced
          //
          // https://wicg.github.io/compression/#supported-formats has error
          // conditions for each compression format when additional input comes
          // after stream end.
          // Note that additional calls for inflate() immediately emits
          // Z_STREAM_END after this point.
          if (mZStream.avail_in > 0) {
            aRv.ThrowTypeError("Unexpected input after the end of stream");
            return;
          }
          mObservedStreamEnd = true;
          break;
        case Z_OK:
        case Z_BUF_ERROR:
          // * Z_OK if some progress has been made
          // * Z_BUF_ERROR if no progress was possible or if there was not
          // enough room in the output buffer when Z_FINISH is used. Note that
          // Z_BUF_ERROR is not fatal, and inflate() can be called again with
          // more input and more output space to continue decompressing.
          //
          // (But of course no input should be given after Z_FINISH)
          break;
        case Z_STREAM_ERROR:
        default:
          // * Z_STREAM_ERROR if the stream state was inconsistent
          // (which is fatal)
          MOZ_ASSERT_UNREACHABLE("Unexpected decompression error code");
          aRv.ThrowTypeError("Unexpected decompression error");
          return;
      }

      // At this point we either exhausted the input or the output buffer
      MOZ_ASSERT(!mZStream.avail_in || !mZStream.avail_out);

      size_t written = kBufferSize - mZStream.avail_out;
      if (!written) {
        break;
      }

      // Step 3: If buffer is empty, return.
      // (We'll implicitly return when the array is empty.)

      // Step 4: Split buffer into one or more non-empty pieces and convert them
      // into Uint8Arrays.
      // (The buffer is 'split' by having a fixed sized buffer above.)

      JS::Rooted<JSObject*> view(
          aCx, nsJSUtils::MoveBufferAsUint8Array(aCx, written, buffer));
      if (!view || !array.append(view)) {
        JS_ClearPendingException(aCx);
        aRv.ThrowTypeError("Out of memory");
        return;
      }
    } while (mZStream.avail_out == 0 && !mObservedStreamEnd);
    // From the manual:
    // * It must update next_out and avail_out when avail_out has dropped to
    // zero.
    // * inflate() should normally be called until it returns Z_STREAM_END or an
    // error.

    if (aFlush == ZLibFlush::Yes && !mObservedStreamEnd) {
      // Step 2 of
      // https://wicg.github.io/compression/#decompress-flush-and-enqueue
      // If the end of the compressed input has not been reached, then throw a
      // TypeError.
      aRv.ThrowTypeError("The input is ended without reaching the stream end");
      return;
    }

    // Step 5: For each Uint8Array array, enqueue array in cs's transform.
    for (const auto& view : array) {
      JS::Rooted<JS::Value> value(aCx, JS::ObjectValue(*view));
      aController.Enqueue(aCx, value, aRv);
      if (aRv.Failed()) {
        return;
      }
    }
  }

  ~DecompressionStreamAlgorithms() override { inflateEnd(&mZStream); };

  z_stream mZStream = {};
  bool mObservedStreamEnd = false;
};

NS_IMPL_CYCLE_COLLECTION_INHERITED(DecompressionStreamAlgorithms,
                                   TransformerAlgorithmsBase)
NS_IMPL_ADDREF_INHERITED(DecompressionStreamAlgorithms,
                         TransformerAlgorithmsBase)
NS_IMPL_RELEASE_INHERITED(DecompressionStreamAlgorithms,
                          TransformerAlgorithmsBase)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DecompressionStreamAlgorithms)
NS_INTERFACE_MAP_END_INHERITING(TransformerAlgorithmsBase)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(DecompressionStream, mGlobal, mStream)
NS_IMPL_CYCLE_COLLECTING_ADDREF(DecompressionStream)
NS_IMPL_CYCLE_COLLECTING_RELEASE(DecompressionStream)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(DecompressionStream)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

DecompressionStream::DecompressionStream(nsISupports* aGlobal,
                                         TransformStream& aStream)
    : mGlobal(aGlobal), mStream(&aStream) {}

DecompressionStream::~DecompressionStream() = default;

JSObject* DecompressionStream::WrapObject(JSContext* aCx,
                                          JS::Handle<JSObject*> aGivenProto) {
  return DecompressionStream_Binding::Wrap(aCx, this, aGivenProto);
}

// https://wicg.github.io/compression/#dom-decompressionstream-decompressionstream
already_AddRefed<DecompressionStream> DecompressionStream::Constructor(
    const GlobalObject& aGlobal, CompressionFormat aFormat, ErrorResult& aRv) {
  // Step 1: If format is unsupported in DecompressionStream, then throw a
  // TypeError.
  // XXX: Skipped as we are using enum for this

  // Step 2 - 4: (Done in DecompressionStreamAlgorithms)

  // Step 5: Set this's transform to a new TransformStream.

  // Step 6: Set up this's transform with transformAlgorithm set to
  // transformAlgorithm and flushAlgorithm set to flushAlgorithm.
  auto algorithms = MakeRefPtr<DecompressionStreamAlgorithms>(aFormat);

  RefPtr<TransformStream> stream =
      TransformStream::CreateGeneric(aGlobal, *algorithms, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  return do_AddRef(new DecompressionStream(aGlobal.GetAsSupports(), *stream));
}

already_AddRefed<ReadableStream> DecompressionStream::Readable() const {
  return do_AddRef(mStream->Readable());
};

already_AddRefed<WritableStream> DecompressionStream::Writable() const {
  return do_AddRef(mStream->Writable());
};

}  // namespace mozilla::dom
