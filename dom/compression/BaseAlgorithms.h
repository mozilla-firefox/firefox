/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_COMPRESSION_BASEALGORITHMS_H_
#define DOM_COMPRESSION_BASEALGORITHMS_H_

#include "js/TypeDecls.h"
#include "mozilla/dom/TransformerCallbackHelpers.h"

namespace mozilla::dom::compression {

// A top-level, library-agnostic flush enum that should be converted
// into the native flush values for a given (de)compression library
// with a function defined below.
enum class Flush : bool { No, Yes };

class CompressionStreamAlgorithms : public TransformerAlgorithmsWrapper {
 public:
  // Step 3 of
  // https://compression.spec.whatwg.org/#dom-compressionstream-compressionstream
  // Let transformAlgorithm be an algorithm which takes a chunk argument and
  // runs the compress and enqueue a chunk algorithm with this and chunk.
  MOZ_CAN_RUN_SCRIPT
  void TransformCallbackImpl(JS::Handle<JS::Value> aChunk,
                             TransformStreamDefaultController& aController,
                             ErrorResult& aRv) override;

  // Step 4 of
  // https://compression.spec.whatwg.org/#dom-compressionstream-compressionstream
  // Let flushAlgorithm be an algorithm which takes no argument and runs the
  // compress flush and enqueue algorithm with this.
  MOZ_CAN_RUN_SCRIPT void FlushCallbackImpl(
      TransformStreamDefaultController& aController, ErrorResult& aRv) override;

 protected:
  static const uint16_t kBufferSize = 16384;

  ~CompressionStreamAlgorithms() = default;

  // Steps 2 - 4 of
  // https://compression.spec.whatwg.org/#compress-and-enqueue-a-chunk
  // and steps 1 - 3 of
  // https://compression.spec.whatwg.org/#compress-flush-and-enqueue
  virtual void Compress(JSContext* aCx, Span<const uint8_t> aInput,
                        JS::MutableHandleVector<JSObject*> aOutput,
                        Flush aFlush, ErrorResult& aRv) = 0;

 private:
  MOZ_CAN_RUN_SCRIPT void Enqueue(JSContext* aCx,
                                  JS::HandleVector<JSObject*> aArray,
                                  TransformStreamDefaultController& aController,
                                  ErrorResult& aRv);
};

class DecompressionStreamAlgorithms : public TransformerAlgorithmsWrapper {
 public:
  // Step 3 of
  // https://compression.spec.whatwg.org/#dom-decompressionstream-decompressionstream
  // Let transformAlgorithm be an algorithm which takes a chunk argument and
  // runs the compress and enqueue a chunk algorithm with this and chunk.
  MOZ_CAN_RUN_SCRIPT
  void TransformCallbackImpl(JS::Handle<JS::Value> aChunk,
                             TransformStreamDefaultController& aController,
                             ErrorResult& aRv) override;

  // Step 4 of
  // https://compression.spec.whatwg.org/#dom-decompressionstream-decompressionstream
  // Let flushAlgorithm be an algorithm which takes no argument and runs the
  // compress flush and enqueue algorithm with this.
  MOZ_CAN_RUN_SCRIPT void FlushCallbackImpl(
      TransformStreamDefaultController& aController, ErrorResult& aRv) override;

 protected:
  static const uint16_t kBufferSize = 16384;

  ~DecompressionStreamAlgorithms() = default;

  /**
   * Steps 2 and 4 of
   * https://compression.spec.whatwg.org/#decompress-and-enqueue-a-chunk
   * (step 3, returning early if buffer is empty, is intentionally not
   * implemented; see https://github.com/whatwg/compression/issues/78)
   * and steps 1 and 2.1 of
   * https://compression.spec.whatwg.org/#decompress-flush-and-enqueue
   *
   * @return true if the input is fully consumed, else false
   */
  virtual bool Decompress(JSContext* aCx, Span<const uint8_t> aInput,
                          JS::MutableHandleVector<JSObject*> aOutput,
                          Flush aFlush, ErrorResult& aRv) = 0;

  bool mObservedStreamEnd = false;

 private:
  MOZ_CAN_RUN_SCRIPT void Enqueue(JSContext* aCx,
                                  JS::HandleVector<JSObject*> aArray,
                                  TransformStreamDefaultController& aController,
                                  ErrorResult& aRv);
};

}  // namespace mozilla::dom::compression

#endif  // DOM_COMPRESSION_BASEALGORITHMS_H_
