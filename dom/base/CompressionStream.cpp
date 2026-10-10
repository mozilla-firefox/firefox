/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/CompressionStream.h"

#include "js/TypeDecls.h"
#include "mozilla/Assertions.h"
#include "mozilla/dom/CompressionStreamBinding.h"
#include "mozilla/dom/ReadableStream.h"
#include "mozilla/dom/WritableStream.h"
#include "mozilla/dom/TransformStream.h"
#include "mozilla/dom/TextDecoderStream.h"
#include "mozilla/dom/TransformerCallbackHelpers.h"
#include "mozilla/dom/UnionTypes.h"

#include "ZLibHelper.h"

// See the zlib manual in https://www.zlib.net/manual.html or in
// https://searchfox.org/mozilla-central/source/modules/zlib/src/zlib.h

namespace mozilla::dom {

class CompressionStreamAlgorithms : public TransformerAlgorithmsWrapper {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(CompressionStreamAlgorithms,
                                           TransformerAlgorithmsBase)

  explicit CompressionStreamAlgorithms(CompressionFormat format) {
    int8_t err = deflateInit2(&mZStream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                              ZLibWindowBits(format), 8 /* default memLevel */,
                              Z_DEFAULT_STRATEGY);
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

    // Step 2: Let buffer be the result of compressing chunk with cs's format
    // and context.
    // Step 3 - 5: (Done in CompressAndEnqueue)
    CompressAndEnqueue(aCx, input, ZLibFlush::No, aController, aRv);
  }

  // Step 4 of
  // https://wicg.github.io/compression/#dom-compressionstream-compressionstream
  // Let flushAlgorithm be an algorithm which takes no argument and runs the
  // compress flush and enqueue algorithm with this.
  MOZ_CAN_RUN_SCRIPT void FlushCallbackImpl(
      JSContext* aCx, TransformStreamDefaultController& aController,
      ErrorResult& aRv) override {
    // https://wicg.github.io/compression/#compress-flush-and-enqueue

    // Step 1: Let buffer be the result of compressing an empty input with cs's
    // format and context, with the finish flag.
    // Step 2 - 4: (Done in CompressAndEnqueue)
    CompressAndEnqueue(aCx, Span<const uint8_t>(), ZLibFlush::Yes, aController,
                       aRv);
  }

 private:
  // Shared by:
  // https://wicg.github.io/compression/#compress-and-enqueue-a-chunk
  // https://wicg.github.io/compression/#compress-flush-and-enqueue
  MOZ_CAN_RUN_SCRIPT void CompressAndEnqueue(
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

      int8_t err = deflate(&mZStream, aFlush);

      // From the manual: deflate() returns ...
      switch (err) {
        case Z_OK:
        case Z_STREAM_END:
        case Z_BUF_ERROR:
          // * Z_OK if some progress has been made
          // * Z_STREAM_END if all input has been consumed and all output has
          // been produced (only when flush is set to Z_FINISH)
          // * Z_BUF_ERROR if no progress is possible (for example avail_in or
          // avail_out was zero). Note that Z_BUF_ERROR is not fatal, and
          // deflate() can be called again with more input and more output space
          // to continue compressing.
          //
          // (But of course no input should be given after Z_FINISH)
          break;
        case Z_STREAM_ERROR:
        default:
          // * Z_STREAM_ERROR if the stream state was inconsistent
          // (which is fatal)
          MOZ_ASSERT_UNREACHABLE("Unexpected compression error code");
          aRv.ThrowTypeError("Unexpected compression error");
          return;
      }

      // Stream should end only when flushed and vise versa, see above
      MOZ_ASSERT_IF(err == Z_STREAM_END, aFlush == ZLibFlush::Yes);
      MOZ_ASSERT_IF(aFlush == ZLibFlush::Yes, err == Z_STREAM_END);

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
    } while (mZStream.avail_out == 0);
    // From the manual:
    // If deflate returns with avail_out == 0, this function must be called
    // again with the same value of the flush parameter and more output space
    // (updated avail_out)

    // Step 5: For each Uint8Array array, enqueue array in cs's transform.
    for (const auto& view : array) {
      JS::Rooted<JS::Value> value(aCx, JS::ObjectValue(*view));
      aController.Enqueue(aCx, value, aRv);
      if (aRv.Failed()) {
        return;
      }
    }
  }

  ~CompressionStreamAlgorithms() override { deflateEnd(&mZStream); };

  z_stream mZStream = {};
};

NS_IMPL_CYCLE_COLLECTION_INHERITED(CompressionStreamAlgorithms,
                                   TransformerAlgorithmsBase)
NS_IMPL_ADDREF_INHERITED(CompressionStreamAlgorithms, TransformerAlgorithmsBase)
NS_IMPL_RELEASE_INHERITED(CompressionStreamAlgorithms,
                          TransformerAlgorithmsBase)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(CompressionStreamAlgorithms)
NS_INTERFACE_MAP_END_INHERITING(TransformerAlgorithmsBase)

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(CompressionStream, mGlobal, mStream)
NS_IMPL_CYCLE_COLLECTING_ADDREF(CompressionStream)
NS_IMPL_CYCLE_COLLECTING_RELEASE(CompressionStream)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(CompressionStream)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

CompressionStream::CompressionStream(nsISupports* aGlobal,
                                     TransformStream& aStream)
    : mGlobal(aGlobal), mStream(&aStream) {}

CompressionStream::~CompressionStream() = default;

JSObject* CompressionStream::WrapObject(JSContext* aCx,
                                        JS::Handle<JSObject*> aGivenProto) {
  return CompressionStream_Binding::Wrap(aCx, this, aGivenProto);
}

// https://wicg.github.io/compression/#dom-compressionstream-compressionstream
already_AddRefed<CompressionStream> CompressionStream::Constructor(
    const GlobalObject& aGlobal, CompressionFormat aFormat, ErrorResult& aRv) {
  // Step 1: If format is unsupported in CompressionStream, then throw a
  // TypeError.
  // XXX: Skipped as we are using enum for this

  // Step 2 - 4: (Done in CompressionStreamAlgorithms)

  // Step 5: Set this's transform to a new TransformStream.

  // Step 6: Set up this's transform with transformAlgorithm set to
  // transformAlgorithm and flushAlgorithm set to flushAlgorithm.
  auto algorithms = MakeRefPtr<CompressionStreamAlgorithms>(aFormat);

  RefPtr<TransformStream> stream =
      TransformStream::CreateGeneric(aGlobal, *algorithms, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  return do_AddRef(new CompressionStream(aGlobal.GetAsSupports(), *stream));
}

already_AddRefed<ReadableStream> CompressionStream::Readable() const {
  return do_AddRef(mStream->Readable());
}

already_AddRefed<WritableStream> CompressionStream::Writable() const {
  return do_AddRef(mStream->Writable());
}

}  // namespace mozilla::dom
