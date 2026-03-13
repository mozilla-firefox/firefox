/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageLogging.h"  // Must appear first

#include "nsJXLDecoder.h"

#include "AnimationParams.h"
#include "mozilla/CheckedInt.h"
#include "RasterImage.h"
#include "SurfacePipeFactory.h"

using namespace mozilla::gfx;

namespace mozilla::image {

static LazyLogModule sJXLLog("JXLDecoder");

nsJXLDecoder::nsJXLDecoder(RasterImage* aImage) : Decoder(aImage) {
  MOZ_LOG(sJXLLog, LogLevel::Debug,
          ("[this=%p] nsJXLDecoder::nsJXLDecoder", this));
}

nsresult nsJXLDecoder::InitInternal() {
  bool premultiply = !(GetSurfaceFlags() & SurfaceFlags::NO_PREMULTIPLY_ALPHA);
  mDecoder.reset(jxl_decoder_new(IsMetadataDecode(), premultiply));
  mScanner.reset(jxl_scanner_new());
  return NS_OK;
}

nsJXLDecoder::~nsJXLDecoder() {
  MOZ_LOG(sJXLLog, LogLevel::Debug,
          ("[this=%p] nsJXLDecoder::~nsJXLDecoder", this));
}

void nsJXLDecoder::TransferScannedFrames(Vector<JxlFrameInfo>&& aFrames) {
  mScannedFrames = std::move(aFrames);
  mScannerDone = true;
  mScanner.reset();
}

void nsJXLDecoder::SetSeekTargetFrame(uint32_t aFrameIndex) {
  mSeekTargetFrame = Some(aFrameIndex);
}

LexerResult nsJXLDecoder::DoDecode(SourceBufferIterator& aIterator,
                                   IResumable* aOnResume) {
  MOZ_ASSERT(!HasError(), "Shouldn't call DoDecode after error!");

  // Save source buffer reference on first call.
  if (!mSourceBuffer) {
    mSourceBuffer = aIterator.Owner();
  }

  while (true) {
    // Choose which iterator to use: our own (seeking) or the provided one.
    SourceBufferIterator& iter = mOwnIterator ? *mOwnIterator : aIterator;

    // Advance the iterator if we haven't reached the end.
    SourceBufferIterator::State state = SourceBufferIterator::COMPLETE;
    if (!mIteratorComplete) {
      state = iter.AdvanceOrScheduleResume(SIZE_MAX, aOnResume);
      mIteratorComplete = (state == SourceBufferIterator::COMPLETE);
    }

    if (state == SourceBufferIterator::WAITING) {
      return LexerResult(Yield::NEED_MORE_DATA);
    }

    const uint8_t* chunkData = nullptr;
    size_t chunkLength = 0;

    if (state == SourceBufferIterator::READY) {
      chunkData = reinterpret_cast<const uint8_t*>(iter.Data());
      chunkLength = iter.Length();
      mTotalBytesReceived += chunkLength;
    }

    // Skip chunks when seeking to a specific offset.
    if (mSkipToOffset > 0) {
      if (mTotalBytesReceived <= mSkipToOffset) {
        if (mIteratorComplete) {
          return LexerResult(TerminalState::FAILURE);
        }
        continue;
      }
      MOZ_ASSERT(chunkData);
      size_t overshoot = mTotalBytesReceived - mSkipToOffset;
      chunkData = chunkData + chunkLength - overshoot;
      chunkLength = overshoot;
      mSkipToOffset = 0;

      // Done seeking - release own iterator, switch back to framework
      // iterator.
      mOwnIterator.reset();
    }

    // Feed scanner with new data (only bytes it hasn't seen).
    if (chunkData && chunkLength > 0) {
      size_t scannerStart = mTotalBytesReceived - chunkLength;
      if (scannerStart < mScannerBytesConsumed) {
        size_t skip = mScannerBytesConsumed - scannerStart;
        if (skip < chunkLength) {
          FeedScanner(chunkData + skip, chunkLength - skip);
          mScannerBytesConsumed = mTotalBytesReceived;
        }
      } else {
        FeedScanner(chunkData, chunkLength);
        mScannerBytesConsumed = mTotalBytesReceived;
      }
    }

    // Feed data to the decoder and process state machine.
    const uint8_t* currentData = chunkData;
    size_t currentLength = chunkLength;
    bool seekStarted = false;

    while (true) {
      JxlDecoderStatus status = ProcessInput(&currentData, &currentLength);

      switch (status) {
        case JxlDecoderStatus::Ok: {
          if (mDecoderState == DecoderState::Initial) {
            JxlBasicInfo basicInfo = jxl_decoder_get_basic_info(mDecoder.get());
            if (!basicInfo.valid) {
              if (currentLength == 0) {
                break;  // Need more data.
              }
              continue;  // More data in buffer, keep processing.
            }

            if (basicInfo.width > INT32_MAX || basicInfo.height > INT32_MAX) {
              return LexerResult(TerminalState::FAILURE);
            }

            PostSize(basicInfo.width, basicInfo.height);
            if (basicInfo.has_alpha) {
              PostHasTransparency();
            }

            if (!basicInfo.is_animated) {
              PostFrameCount(1);
              mScannerDone = true;
              mScanner.reset();
              if (IsMetadataDecode()) {
                return LexerResult(TerminalState::SUCCESS);
              }
            }

            mDecoderState = DecoderState::HaveBasicInfo;

            // If we have a seek target, set up seeking now.
            if (mSeekTargetFrame && HasAnimation()) {
              if (!mScannerDone || mScannedFrames.empty()) {
                return LexerResult(TerminalState::FAILURE);
              }
              uint32_t target = *mSeekTargetFrame;
              if (target >= mScannedFrames.length()) {
                target = 0;
              }
              JxlFrameInfo& info = mScannedFrames[target];
              jxl_decoder_start_new_frame(
                  mDecoder.get(), info.decode_start_file_offset,
                  info.remaining_in_box, info.visible_frames_to_skip);
              mFrameIndex = target;
              mSkipToOffset = info.decode_start_file_offset;
              mSeekTargetFrame.reset();

              // Create our own iterator to replay data from the beginning.
              mOwnIterator.emplace(mSourceBuffer->Iterator());
              mIteratorComplete = false;
              mTotalBytesReceived = 0;
              seekStarted = true;
              break;
            }
          }

          if (!seekStarted) {
            // Handle animation metadata.
            if (mDecoderState == DecoderState::HaveBasicInfo) {
              if (jxl_decoder_is_frame_ready(mDecoder.get()) &&
                  !HasAnimation()) {
                JxlBasicInfo basicInfo =
                    jxl_decoder_get_basic_info(mDecoder.get());
                if (basicInfo.is_animated) {
                  JxlFrameInfo frameInfo =
                      jxl_decoder_get_frame_info(mDecoder.get());
                  PostIsAnimated(
                      FrameTimeout::FromRawMilliseconds(frameInfo.duration_ms));
                  PostLoopCount(
                      (basicInfo.num_loops == 0 ||
                       basicInfo.num_loops > INT32_MAX)
                          ? -1
                          : static_cast<int32_t>(basicInfo.num_loops));
                  if (IsMetadataDecode()) {
                    return LexerResult(TerminalState::SUCCESS);
                  }
                }
              }
            }

            switch (HandleFrameOutput()) {
              case FrameOutputResult::BufferAllocated:
                break;
              case FrameOutputResult::FrameAdvanced:
                return LexerResult(Yield::OUTPUT_AVAILABLE);
              case FrameOutputResult::DecodeComplete:
                return LexerResult(TerminalState::SUCCESS);
              case FrameOutputResult::NoOutput:
                if (currentLength == 0) {
                  break;
                }
                continue;
              case FrameOutputResult::Error:
                return LexerResult(TerminalState::FAILURE);
            }
          }
          break;
        }

        case JxlDecoderStatus::NeedMoreData: {
          // Progressive flush for still images only.
          if (!HasAnimation() && !mPixelBuffer.empty() && mCurrentPipe) {
            uint32_t passes = jxl_decoder_num_completed_passes(mDecoder.get());
            if (passes > mLastFlushedPasses) {
              FlushPartialFrame();
              mLastFlushedPasses = passes;
            }
          }
          break;
        }

        case JxlDecoderStatus::Error:
          return LexerResult(TerminalState::FAILURE);
      }

      // Inner loop exhausted current chunk data; break to outer loop.
      break;
    }

    // If a seek was started, re-enter outer loop with the own iterator.
    if (seekStarted) {
      continue;
    }

    // If iterator is complete, try draining remaining frames.
    if (mIteratorComplete) {
      return DrainFrames();
    }
  }
}

LexerResult nsJXLDecoder::DrainFrames() {
  while (true) {
    const uint8_t* noData = nullptr;
    size_t noLength = 0;
    JxlDecoderStatus status = ProcessInput(&noData, &noLength);

    switch (status) {
      case JxlDecoderStatus::Ok: {
        if (!HasSize()) {
          return LexerResult(TerminalState::FAILURE);
        }

        switch (HandleFrameOutput()) {
          case FrameOutputResult::BufferAllocated:
            break;
          case FrameOutputResult::FrameAdvanced:
            return LexerResult(Yield::OUTPUT_AVAILABLE);
          case FrameOutputResult::DecodeComplete:
          case FrameOutputResult::NoOutput:
            return LexerResult(TerminalState::SUCCESS);
          case FrameOutputResult::Error:
            return LexerResult(TerminalState::FAILURE);
        }
        break;
      }

      case JxlDecoderStatus::NeedMoreData:
        if (!HasSize()) {
          return LexerResult(TerminalState::FAILURE);
        }
        return LexerResult(TerminalState::SUCCESS);

      case JxlDecoderStatus::Error:
        return LexerResult(TerminalState::FAILURE);
    }
  }
}

JxlDecoderStatus nsJXLDecoder::ProcessInput(const uint8_t** aData,
                                            size_t* aLength) {
  uint8_t* bufferPtr = mPixelBuffer.empty() ? nullptr : mPixelBuffer.begin();
  size_t bufferLen = mPixelBuffer.length();
  return jxl_decoder_process_data(mDecoder.get(), aData, aLength, bufferPtr,
                                  bufferLen);
}

nsJXLDecoder::FrameOutputResult nsJXLDecoder::HandleFrameOutput() {
  bool frameNeedsBuffer = jxl_decoder_is_frame_ready(mDecoder.get());

  if (frameNeedsBuffer && mPixelBuffer.empty()) {
    return BeginFrame();
  }

  if (!frameNeedsBuffer && !mPixelBuffer.empty()) {
    nsresult rv = FinishFrame();
    if (NS_FAILED(rv)) {
      return FrameOutputResult::Error;
    }

    bool hasMoreFrames = jxl_decoder_has_more_frames(mDecoder.get());
    if (IsFirstFrameDecode() || !HasAnimation() || !hasMoreFrames) {
      PostFrameCount(mFrameIndex + 1);
      PostDecodeDone();
      return FrameOutputResult::DecodeComplete;
    }
    mFrameIndex++;
    mPixelBuffer.clear();
    mLastFlushedPasses = 0;
    return FrameOutputResult::FrameAdvanced;
  }

  return FrameOutputResult::NoOutput;
}

nsJXLDecoder::FrameOutputResult nsJXLDecoder::BeginFrame() {
  MOZ_ASSERT(HasSize());

  OrientedIntSize size = Size();
  CheckedInt<size_t> bufferSize =
      CheckedInt<size_t>(size.width) * size.height * 4;
  if (!bufferSize.isValid() || !mPixelBuffer.resize(bufferSize.value())) {
    MOZ_LOG(sJXLLog, LogLevel::Error,
            ("[this=%p] nsJXLDecoder::BeginFrame -- "
             "failed to allocate pixel buffer\n",
             this));
    return FrameOutputResult::Error;
  }

  JxlBasicInfo basicInfo = jxl_decoder_get_basic_info(mDecoder.get());

  Maybe<AnimationParams> animParams;
  if (HasAnimation()) {
    JxlFrameInfo frameInfo = jxl_decoder_get_frame_info(mDecoder.get());
    if (!frameInfo.frame_duration_valid) {
      return FrameOutputResult::Error;
    }
    animParams.emplace(FullFrame().ToUnknownRect(),
                       FrameTimeout::FromRawMilliseconds(frameInfo.duration_ms),
                       mFrameIndex, BlendMethod::SOURCE, DisposalMethod::KEEP);
  }

  SurfaceFormat inFormat = SurfaceFormat::R8G8B8A8;
  SurfaceFormat outFormat =
      basicInfo.has_alpha ? SurfaceFormat::OS_RGBA : SurfaceFormat::OS_RGBX;
  SurfacePipeFlags pipeFlags = SurfacePipeFlags();

  mCurrentPipe = SurfacePipeFactory::CreateSurfacePipe(
      this, size, OutputSize(), FullFrame(), inFormat, outFormat, animParams,
      nullptr, pipeFlags);
  if (!mCurrentPipe) {
    return FrameOutputResult::Error;
  }

  mLastFlushedPasses = 0;
  return FrameOutputResult::BufferAllocated;
}

nsresult nsJXLDecoder::FinishFrame() {
  MOZ_ASSERT(HasSize());
  MOZ_ASSERT(mDecoder);
  MOZ_ASSERT(mCurrentPipe);

  JxlBasicInfo basicInfo = jxl_decoder_get_basic_info(mDecoder.get());
  OrientedIntSize size = Size();

  if (mLastFlushedPasses > 0) {
    mCurrentPipe->ResetToFirstRow();
  }

  uint8_t* currentRow = mPixelBuffer.begin();
  for (int32_t y = 0; y < size.height; ++y) {
    WriteState result =
        mCurrentPipe->WriteBuffer(reinterpret_cast<uint32_t*>(currentRow));
    if (result == WriteState::FAILURE) {
      mCurrentPipe.reset();
      return NS_ERROR_FAILURE;
    }
    currentRow += size.width * 4;
  }

  if (Maybe<SurfaceInvalidRect> invalidRect = mCurrentPipe->TakeInvalidRect()) {
    PostInvalidation(invalidRect->mInputSpaceRect,
                     Some(invalidRect->mOutputSpaceRect));
  }

  PostFrameStop(basicInfo.has_alpha ? Opacity::SOME_TRANSPARENCY
                                    : Opacity::FULLY_OPAQUE);
  mCurrentPipe.reset();
  return NS_OK;
}

void nsJXLDecoder::FlushPartialFrame() {
  MOZ_ASSERT(!mPixelBuffer.empty());
  MOZ_ASSERT(mCurrentPipe);

  JxlDecoderStatus status = jxl_decoder_flush_pixels(
      mDecoder.get(), mPixelBuffer.begin(), mPixelBuffer.length());
  if (status != JxlDecoderStatus::Ok) {
    return;
  }

  OrientedIntSize size = Size();
  mCurrentPipe->ResetToFirstRow();
  uint8_t* currentRow = mPixelBuffer.begin();
  for (int32_t y = 0; y < size.height; ++y) {
    if (mCurrentPipe->WriteBuffer(reinterpret_cast<uint32_t*>(currentRow)) ==
        WriteState::FAILURE) {
      return;
    }
    currentRow += size.width * 4;
  }

  if (Maybe<SurfaceInvalidRect> invalidRect = mCurrentPipe->TakeInvalidRect()) {
    PostInvalidation(invalidRect->mInputSpaceRect,
                     Some(invalidRect->mOutputSpaceRect));
  }
}

void nsJXLDecoder::FeedScanner(const uint8_t* aData, size_t aLength) {
  if (mScannerDone || !mScanner) {
    return;
  }

  const uint8_t* data = aData;
  size_t len = aLength;
  jxl_decoder_process_data(mScanner.get(), &data, &len, nullptr, 0);

  if (!jxl_decoder_has_more_frames(mScanner.get())) {
    uint32_t count = jxl_decoder_get_scanned_frame_count(mScanner.get());
    mScannedFrames.clear();
    if (mScannedFrames.reserve(count)) {
      for (uint32_t i = 0; i < count; ++i) {
        JxlFrameInfo info =
            jxl_decoder_get_scanned_frame_info(mScanner.get(), i);
        mScannedFrames.infallibleAppend(info);
      }
    }
    PostFrameCount(count);
    mScannerDone = true;
    mScanner.reset();
  }
}

}  // namespace mozilla::image
