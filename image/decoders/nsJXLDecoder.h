/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_decoders_nsJXLDecoder_h
#define mozilla_image_decoders_nsJXLDecoder_h

#include "Decoder.h"
#include "SourceBuffer.h"
#include "SurfacePipe.h"
#include "mozilla/Maybe.h"
#include "mozilla/Vector.h"
#include "mozilla/image/jxl_decoder_ffi.h"

namespace mozilla::image {

struct JxlDecoderDeleter {
  void operator()(JxlApiDecoder* ptr) { jxl_decoder_destroy(ptr); }
};

class nsJXLDecoder final : public Decoder {
 public:
  ~nsJXLDecoder() override;

  DecoderType GetType() const override { return DecoderType::JXL; }

  void TransferScannedFrames(Vector<JxlFrameInfo>&& aFrames);
  void SetSeekTargetFrame(uint32_t aFrameIndex);
  const Vector<JxlFrameInfo>& ScannedFrames() const { return mScannedFrames; }

 protected:
  nsresult InitInternal() override;
  LexerResult DoDecode(SourceBufferIterator& aIterator,
                       IResumable* aOnResume) override;

 private:
  friend class DecoderFactory;

  explicit nsJXLDecoder(RasterImage* aImage);

  enum class DecoderState { Initial, HaveBasicInfo };

  enum class FrameOutputResult {
    BufferAllocated,
    FrameAdvanced,
    DecodeComplete,
    NoOutput,
    Error
  };

  JxlDecoderStatus ProcessInput(const uint8_t** aData, size_t* aLength);
  FrameOutputResult HandleFrameOutput();

  FrameOutputResult BeginFrame();
  nsresult FinishFrame();
  void FlushPartialFrame();

  LexerResult DrainFrames();
  void FeedScanner(const uint8_t* aData, size_t aLength);

  std::unique_ptr<JxlApiDecoder, JxlDecoderDeleter> mDecoder;
  std::unique_ptr<JxlApiDecoder, JxlDecoderDeleter> mScanner;

  DecoderState mDecoderState = DecoderState::Initial;
  SourceBuffer* mSourceBuffer = nullptr;  // Non-owning; outlives decoder.
  Maybe<SourceBufferIterator> mOwnIterator;
  size_t mTotalBytesReceived = 0;
  size_t mSkipToOffset = 0;
  Maybe<uint32_t> mSeekTargetFrame;

  uint32_t mFrameIndex = 0;

  Vector<uint8_t> mPixelBuffer;
  Maybe<SurfacePipe> mCurrentPipe;
  uint32_t mLastFlushedPasses = 0;

  bool mScannerDone = false;
  size_t mScannerBytesConsumed = 0;
  Vector<JxlFrameInfo> mScannedFrames;

  bool mIteratorComplete = false;
};

}  // namespace mozilla::image

#endif  // mozilla_image_decoders_nsJXLDecoder_h
