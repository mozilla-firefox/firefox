/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsJXLDecoder.h"

#include "AnimationParams.h"
#include "ImageLogging.h"  // Must appear first
#include "RasterImage.h"
#include "SurfacePipeFactory.h"
#include "gfxPlatform.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/glean/ImageDecodersMetrics.h"

using namespace mozilla::gfx;

namespace mozilla::image {

static LazyLogModule sJXLLog("JXLDecoder");

nsJXLDecoder::nsJXLDecoder(RasterImage* aImage) : Decoder(aImage) {
  MOZ_LOG(sJXLLog, LogLevel::Debug,
          ("[this=%p] nsJXLDecoder::nsJXLDecoder", this));
}

Maybe<glean::impl::MemoryDistributionMetric> nsJXLDecoder::SpeedMetric() const {
  return Some(glean::image_decode::speed_jxl);
}

nsresult nsJXLDecoder::InitInternal() {
  bool hasCMS = GetCMSOutputProfile() && mCMSMode != CMSMode::Off;
  mDecoder.reset(jxl_decoder_new(IsMetadataDecode(), hasCMS));
  if (WantsFrameCount()) {
    mScanner.reset(jxl_scanner_new());
  }
  return NS_OK;
}

nsJXLDecoder::~nsJXLDecoder() {
  MOZ_LOG(sJXLLog, LogLevel::Debug,
          ("[this=%p] nsJXLDecoder::~nsJXLDecoder", this));
}

LexerResult nsJXLDecoder::DoDecode(SourceBufferIterator& aIterator,
                                   IResumable* aOnResume) {
  LexerResult result = DoDecodeInternal(aIterator, aOnResume);
  if (result.is<TerminalState>()) {
    RecordDecodeTelemetry(result.as<TerminalState>());
  }
  return result;
}

void nsJXLDecoder::RecordDecodeTelemetry(TerminalState aState) {
  using Label = glean::jxl::DecodeResultLabel;

  // We want to report failures in metadata decodes because we won't get a
  // full decode in that case to report the failure.
  if (WantsFrameCount() ||
      (IsMetadataDecode() && aState == TerminalState::SUCCESS)) {
    return;
  }

  if (aState == TerminalState::FAILURE) {
    Label label = Label::eDecodeError;
    switch (mDecodeResult) {
      case DecodeResult::DecodeError:
        label = Label::eDecodeError;
        break;
      case DecodeResult::SizeOverflow:
        label = Label::eSizeOverflow;
        break;
      case DecodeResult::OutOfMemory:
        label = Label::eOutOfMemory;
        break;
      case DecodeResult::PipeInitError:
        label = Label::ePipeInitError;
        break;
      case DecodeResult::InvalidFrameDuration:
        label = Label::eInvalidFrameDuration;
        break;
      case DecodeResult::WriteError:
        label = Label::eWriteError;
        break;
      case DecodeResult::NoBasicInfo:
        label = Label::eNoBasicInfo;
        break;
    }
    glean::jxl::decode_result.EnumGet(label).Add();
    return;
  }

  Label successLabel = mFrameCompleted         ? Label::eSuccess
                       : mPartialFrameRendered ? Label::ePartialFrame
                                               : Label::eNoFrame;
  glean::jxl::decode_result.EnumGet(successLabel).Add();

  // hdr and animated come from the basic info, which is available on any
  // successful terminal state (including partial_frame / no_frame), so record
  // them regardless of how much of the frame was produced.
  glean::jxl::hdr
      .EnumGet(jxl_decoder_use_f16(mDecoder.get())
                   ? glean::jxl::HdrLabel::ePresent
                   : glean::jxl::HdrLabel::eAbsent)
      .Add();

  JxlBasicInfo basicInfo = jxl_decoder_get_basic_info(mDecoder.get());
  glean::jxl::animated
      .EnumGet(basicInfo.valid && basicInfo.is_animated
                   ? glean::jxl::AnimatedLabel::ePresent
                   : glean::jxl::AnimatedLabel::eAbsent)
      .Add();
}

LexerResult nsJXLDecoder::DoDecodeInternal(SourceBufferIterator& aIterator,
                                           IResumable* aOnResume) {
  MOZ_ASSERT(!HasError(), "Shouldn't call DoDecode after error!");

  if (WantsFrameCount()) {
    return ScanForFrameCount(aIterator, aOnResume);
  }

  while (true) {
    if (mIteratorComplete) {
      return DrainFrames();
    }

    if (!aIterator.IsReady() || aIterator.Length() == 0) {
      SourceBufferIterator::State state =
          aIterator.AdvanceOrScheduleResume(SIZE_MAX, aOnResume);

      if (state == SourceBufferIterator::WAITING ||
          state == SourceBufferIterator::COMPLETE) {
        // We are about to suspend until more bytes arrive, so this is the
        // point where flushing a partial frame is actually useful for the
        // user. NeedMoreData inside ProcessAvailableData does not imply this:
        // the iterator may still have buffered bytes that we will consume on
        // the next loop iteration in ProcessAvailableData. This is also
        // potentially the only and last chance to actually push the pixels
        // through the surface pipe so they get to the user.
        if (!HasAnimation() && !mPixelBuffer.empty()) {
          FlushPartialFrame();
        }
      }

      if (state == SourceBufferIterator::WAITING) {
        MOZ_LOG(
            sJXLLog, LogLevel::Verbose,
            ("[this=%p] nsJXLDecoder::DoDecode -- yield NEED_MORE_DATA", this));
        return LexerResult(Yield::NEED_MORE_DATA);
      }
      if (state == SourceBufferIterator::COMPLETE) {
        mIteratorComplete = true;
        continue;
      }
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(aIterator.Data());
    size_t length = aIterator.Length();
    MOZ_ASSERT(length > 0);
    const uint8_t* const chunkStart = data;

    // data and length are in/out: updated to reflect the unconsumed remainder.
    ProcessResult result = ProcessAvailableData(&data, &length);

    aIterator.MarkConsumed(static_cast<size_t>(data - chunkStart));

    switch (result) {
      case ProcessResult::Error:
        return LexerResult(TerminalState::FAILURE);
      case ProcessResult::Complete:
        return LexerResult(TerminalState::SUCCESS);
      case ProcessResult::NeedMoreData:
        break;
      case ProcessResult::YieldOutput:
        return LexerResult(Yield::OUTPUT_AVAILABLE);
    }
  }
}

LexerResult nsJXLDecoder::ScanForFrameCount(SourceBufferIterator& aIterator,
                                            IResumable* aOnResume) {
  MOZ_ASSERT(mScanner);

  const uint8_t* currentData = nullptr;
  size_t currentLength = 0;
  bool iteratorComplete = false;

  while (true) {
    // Only fetch a new chunk when all buffered bytes have been consumed.
    if (currentLength == 0 && !iteratorComplete) {
      SourceBufferIterator::State state =
          aIterator.AdvanceOrScheduleResume(SIZE_MAX, aOnResume);

      if (state == SourceBufferIterator::WAITING) {
        return LexerResult(Yield::NEED_MORE_DATA);
      }

      if (state == SourceBufferIterator::READY) {
        currentData = reinterpret_cast<const uint8_t*>(aIterator.Data());
        currentLength = aIterator.Length();
      }

      if (state == SourceBufferIterator::COMPLETE) {
        iteratorComplete = true;
      }
    }

    JxlDecoderStatus scanStatus = jxl_decoder_process_data(
        mScanner.get(), &currentData, &currentLength,
        /*pixel_buffer=*/nullptr, 0, /*k_buffer=*/nullptr, 0);
    if (scanStatus == JxlDecoderStatus::Error) {
      return LexerResult(TerminalState::FAILURE);
    }

    if (!HasSize()) {
      JxlBasicInfo info = jxl_decoder_get_basic_info(mScanner.get());
      if (info.valid) {
        if (info.width > INT32_MAX || info.height > INT32_MAX) {
          return LexerResult(TerminalState::FAILURE);
        }
        PostSize(info.width, info.height);
        if (info.has_alpha) {
          PostHasTransparency();
        }
        if (!info.is_animated) {
          PostFrameCount(1);
          return LexerResult(TerminalState::SUCCESS);
        }
      }
    }

    // Post animation info once we have at least one scanned frame.
    if (HasSize() && !HasAnimation()) {
      // If we get here we know that we are animated but we haven't called
      // PostIsAnimated yet because we return above if we are not animated.
      uint32_t scannedCount =
          jxl_decoder_get_scanned_frame_count(mScanner.get());
      if (scannedCount > 0) {
        JxlBasicInfo info = jxl_decoder_get_basic_info(mScanner.get());
        JxlFrameInfo frameInfo =
            jxl_decoder_get_scanned_frame_info(mScanner.get(), 0);
        PostIsAnimated(
            FrameTimeout::FromRawMilliseconds(frameInfo.duration_ms));
        // num_loops == 0 in jxl means infinite loop, whereas 1 means one total
        // play through.
        PostLoopCount((info.num_loops == 0 || info.num_loops > INT32_MAX)
                          ? -1
                          : static_cast<int32_t>(info.num_loops - 1));
      }
    }

    if (HasSize()) {
      uint32_t count = jxl_decoder_get_scanned_frame_count(mScanner.get());
      if (count >= 1) {
        PostFrameCount(count);
      }
    }

    if (HasSize() && !jxl_decoder_has_more_frames(mScanner.get())) {
      return LexerResult(TerminalState::SUCCESS);
    }

    // If the scanner needs more data and the iterator is exhausted with no
    // buffered bytes remaining, finalize with what we have.
    if (scanStatus == JxlDecoderStatus::NeedMoreData && currentLength == 0 &&
        iteratorComplete) {
      if (!HasSize()) {
        return LexerResult(TerminalState::FAILURE);
      }
      uint32_t count = jxl_decoder_get_scanned_frame_count(mScanner.get());
      PostFrameCount(count > 0 ? count : 1);
      return LexerResult(TerminalState::SUCCESS);
    }
  }
}

// aData and aLength are in/out: on return they point to and describe the
// unconsumed remainder of the input.
nsJXLDecoder::ProcessResult nsJXLDecoder::ProcessAvailableData(
    const uint8_t** aData, size_t* aLength) {
  while (true) {
    JxlDecoderStatus status = ProcessInput(aData, aLength);

    if (status == JxlDecoderStatus::Error) {
      MOZ_LOG(sJXLLog, LogLevel::Debug,
              ("[this=%p] nsJXLDecoder::ProcessAvailableData -- decode error",
               this));
      mDecodeResult = DecodeResult::DecodeError;
      return ProcessResult::Error;
    }

    // Basic info may have just become available, regardless of whether
    // ProcessInput returned Ok or NeedMoreData (jxl-rs sets it during its
    // internal loop and continues). Posting the size and pre-allocating
    // the pixel buffer and SurfacePipe before the visible frame header
    // arrives is what lets flush_pixels render an LF-frame preview into
    // the buffer while we are still waiting on bytes.
    if (mDecoderState == DecoderState::Initial) {
      JxlBasicInfo basicInfo = jxl_decoder_get_basic_info(mDecoder.get());
      if (basicInfo.valid) {
        if (basicInfo.width > INT32_MAX || basicInfo.height > INT32_MAX) {
          MOZ_LOG(sJXLLog, LogLevel::Debug,
                  ("[this=%p] nsJXLDecoder::ProcessAvailableData -- dimensions "
                   "%ux%u exceed INT32_MAX, failing",
                   this, basicInfo.width, basicInfo.height));
          mDecodeResult = DecodeResult::SizeOverflow;
          return ProcessResult::Error;
        }

        PostSize(basicInfo.width, basicInfo.height);
        if (basicInfo.has_alpha) {
          PostHasTransparency();
        }

        if (!basicInfo.is_animated) {
          PostFrameCount(1);
          if (IsMetadataDecode()) {
            return ProcessResult::Complete;
          }
        }

        mDecoderState = DecoderState::HaveBasicInfo;
        MOZ_LOG(sJXLLog, LogLevel::Debug,
                ("[this=%p] nsJXLDecoder::ProcessAvailableData -- have basic "
                 "info (animated=%d, hasAlpha=%d)",
                 this, basicInfo.is_animated, basicInfo.has_alpha));

        // Allocate the pixel buffer for non-animated images here so that
        // FlushPartialFrame can render an LF preview during the LF-frame
        // phase. The SurfacePipe (and the surface it exposes) is created
        // lazily in FlushPartialFrame / FinishFrame so that no opaque-black
        // surface is visible while we wait for the first rendered pixels.
        // Animated images don't take the FlushPartialFrame path, so eager
        // allocation buys them nothing; each frame's buffer is allocated
        // per-frame in HandleFrameOutput.
        if (!basicInfo.is_animated) {
          if (NS_FAILED(AllocateFrameBuffers())) {
            return ProcessResult::Error;
          }
        }
      } else if (status == JxlDecoderStatus::Ok) {
        // Ok without basic info shouldn't happen, but if it does, retry
        // with the remaining bytes.
        if (*aLength == 0) {
          return ProcessResult::NeedMoreData;
        }
        continue;
      }
    }

    if (status == JxlDecoderStatus::NeedMoreData) {
      return ProcessResult::NeedMoreData;
    }

    MOZ_ASSERT(status == JxlDecoderStatus::Ok);
    if (mDecoderState == DecoderState::HaveBasicInfo) {
      if (jxl_decoder_is_frame_ready(mDecoder.get()) && !HasAnimation()) {
        JxlBasicInfo basicInfo = jxl_decoder_get_basic_info(mDecoder.get());
        if (basicInfo.is_animated) {
          JxlFrameInfo frameInfo = jxl_decoder_get_frame_info(mDecoder.get());
          PostIsAnimated(
              FrameTimeout::FromRawMilliseconds(frameInfo.duration_ms));
          // num_loops == 0 in jxl means infinite loop, whereas 1 means one
          // total play through.
          PostLoopCount(
              (basicInfo.num_loops == 0 || basicInfo.num_loops > INT32_MAX)
                  ? -1
                  : static_cast<int32_t>(basicInfo.num_loops - 1));
          if (IsMetadataDecode()) {
            return ProcessResult::Complete;
          }
        }
      }
    }

    switch (HandleFrameOutput()) {
      case FrameOutputResult::BufferAllocated:
      case FrameOutputResult::NoOutput:
        continue;
      case FrameOutputResult::FrameAdvanced:
        MOZ_LOG(sJXLLog, LogLevel::Verbose,
                ("[this=%p] nsJXLDecoder::ProcessAvailableData -- frame "
                 "advanced, yielding output",
                 this));
        return ProcessResult::YieldOutput;
      case FrameOutputResult::DecodeComplete:
        MOZ_LOG(
            sJXLLog, LogLevel::Debug,
            ("[this=%p] nsJXLDecoder::ProcessAvailableData -- decode complete",
             this));
        return ProcessResult::Complete;
      case FrameOutputResult::Error:
        MOZ_LOG(sJXLLog, LogLevel::Debug,
                ("[this=%p] nsJXLDecoder::ProcessAvailableData -- frame output "
                 "error",
                 this));
        return ProcessResult::Error;
    }
    MOZ_CRASH("Unhandled FrameOutputResult");
  }
}

LexerResult nsJXLDecoder::DrainFrames() {
  MOZ_LOG(sJXLLog, LogLevel::Verbose,
          ("[this=%p] nsJXLDecoder::DrainFrames -- draining buffered frames",
           this));
  while (true) {
    const uint8_t* noData = nullptr;
    size_t noLength = 0;
    JxlDecoderStatus status = ProcessInput(&noData, &noLength);

    switch (status) {
      case JxlDecoderStatus::Ok: {
        if (!HasSize()) {
          mDecodeResult = DecodeResult::NoBasicInfo;
          return LexerResult(TerminalState::FAILURE);
        }

        switch (HandleFrameOutput()) {
          case FrameOutputResult::BufferAllocated:
            break;
          case FrameOutputResult::FrameAdvanced:
            MOZ_LOG(sJXLLog, LogLevel::Verbose,
                    ("[this=%p] nsJXLDecoder::DrainFrames -- yield "
                     "OUTPUT_AVAILABLE",
                     this));
            return LexerResult(Yield::OUTPUT_AVAILABLE);
          case FrameOutputResult::DecodeComplete:
          case FrameOutputResult::NoOutput:
            MOZ_LOG(sJXLLog, LogLevel::Debug,
                    ("[this=%p] nsJXLDecoder::DrainFrames -- SUCCESS", this));
            return LexerResult(TerminalState::SUCCESS);
          case FrameOutputResult::Error:
            return LexerResult(TerminalState::FAILURE);
        }
        break;
      }

      case JxlDecoderStatus::NeedMoreData:
        if (!HasSize()) {
          mDecodeResult = DecodeResult::NoBasicInfo;
          return LexerResult(TerminalState::FAILURE);
        }
        return LexerResult(TerminalState::SUCCESS);

      case JxlDecoderStatus::Error:
        mDecodeResult = DecodeResult::DecodeError;
        return LexerResult(TerminalState::FAILURE);
    }
  }
}

JxlDecoderStatus nsJXLDecoder::ProcessInput(const uint8_t** aData,
                                            size_t* aLength) {
  uint8_t* bufferPtr = mPixelBuffer.empty() ? nullptr : mPixelBuffer.begin();
  size_t bufferLen = mPixelBuffer.length();
  uint8_t* kPtr = mKBuffer.empty() ? nullptr : mKBuffer.begin();
  size_t kLen = mKBuffer.length();
  return jxl_decoder_process_data(mDecoder.get(), aData, aLength, bufferPtr,
                                  bufferLen, kPtr, kLen);
}

nsJXLDecoder::FrameOutputResult nsJXLDecoder::HandleFrameOutput() {
  bool frameNeedsBuffer = jxl_decoder_is_frame_ready(mDecoder.get());

  if (frameNeedsBuffer && mPixelBuffer.empty()) {
    if (NS_FAILED(AllocateFrameBuffers())) {
      return FrameOutputResult::Error;
    }
    return FrameOutputResult::BufferAllocated;
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
    mKBuffer.clear();
    return FrameOutputResult::FrameAdvanced;
  }

  return FrameOutputResult::NoOutput;
}

nsJXLDecoder::PixelFormat nsJXLDecoder::DetectPixelFormat(
    JxlApiDecoder* aDecoder, const JxlBasicInfo& aBasicInfo) {
  if (jxl_decoder_use_f16(aDecoder)) {
    return PixelFormat::Rgba16f;
  }
  if (jxl_decoder_is_gray(aDecoder)) {
    return aBasicInfo.has_alpha ? PixelFormat::GrayAlpha8 : PixelFormat::Gray8;
  }
  // Cmyk8 is set when a Black extra channel is present, regardless of CMS,
  // so the no-CMS fallback works too.
  return jxl_decoder_has_black_channel(aDecoder) ? PixelFormat::Cmyk8
                                                 : PixelFormat::Rgba8;
}

nsresult nsJXLDecoder::AllocateFrameBuffers() {
  MOZ_ASSERT(HasSize());
  OrientedIntSize size = Size();
  JxlBasicInfo basicInfo = jxl_decoder_get_basic_info(mDecoder.get());
  MOZ_ASSERT(basicInfo.valid);

  // Format is constant across all frames; detect once on the first frame.
  if (mFrameIndex == 0) {
    mPixelFormat.set(DetectPixelFormat(mDecoder.get(), basicInfo));
  }

  // These buffers are cleared in HandleFrameOutput after each frame is consumed
  // and resized here for the next frame. After the first frame the capacity is
  // already sufficient, so clear() + resize() is just two integer field updates
  // with no memory operations.
  CheckedInt<size_t> bufferSize =
      CheckedInt<size_t>(size.width) * size.height * BytesPerPixel();
  if (!bufferSize.isValid() || !mPixelBuffer.resize(bufferSize.value())) {
    MOZ_LOG(sJXLLog, LogLevel::Error,
            ("[this=%p] nsJXLDecoder::AllocateFrameBuffers -- "
             "failed to allocate pixel buffer\n",
             this));
    mDecodeResult = DecodeResult::OutOfMemory;
    return NS_ERROR_FAILURE;
  }

  if (mPixelFormat.value() == PixelFormat::Cmyk8 &&
      !mKBuffer.resize(size_t(size.width) * size.height)) {
    mDecodeResult = DecodeResult::OutOfMemory;
    return NS_ERROR_FAILURE;
  }

  // Per-row u8 scratch for all non-passthrough paths (gray, CMYK, HDR).
  // Rgba8 passes directly through the pipe; all other formats need conversion.
  // Cmyk8 qcms output is RGB8 (3 bytes/pixel) but we allocate 4 for uniformity.
  if (mPixelFormat.value() != PixelFormat::Rgba8) {
    CheckedInt<size_t> rowBufSize = CheckedInt<size_t>(size.width) * 4;
    if (!rowBufSize.isValid() || !mU8RowBuf.resize(rowBufSize.value())) {
      mDecodeResult = DecodeResult::OutOfMemory;
      return NS_ERROR_FAILURE;
    }
  }

  // Build the qcms transform once on the first frame. The ICC profile and
  // color space are constant across all frames of a JXL image.
  if (mFrameIndex == 0 && GetCMSOutputProfile() && mCMSMode != CMSMode::Off) {
    BuildCMSTransform();
  }

  return NS_OK;
}

nsresult nsJXLDecoder::EnsureSurfacePipe() {
  if (mCurrentPipe) {
    return NS_OK;
  }

  MOZ_ASSERT(HasSize());
  OrientedIntSize size = Size();
  JxlBasicInfo basicInfo = jxl_decoder_get_basic_info(mDecoder.get());
  MOZ_ASSERT(basicInfo.valid);

  Maybe<AnimationParams> animParams;
  if (HasAnimation()) {
    JxlFrameInfo frameInfo = jxl_decoder_get_frame_info(mDecoder.get());
    MOZ_ASSERT(frameInfo.frame_duration_valid);
    if (!frameInfo.frame_duration_valid) {
      mDecodeResult = DecodeResult::InvalidFrameDuration;
      return NS_ERROR_FAILURE;
    }
    animParams.emplace(FullFrame().ToUnknownRect(),
                       FrameTimeout::FromRawMilliseconds(frameInfo.duration_ms),
                       mFrameIndex, BlendMethod::SOURCE, DisposalMethod::KEEP);
  }

  // Cmyk8 with CMS: qcms outputs RGB8 (3 bytes/pixel), pipe handles R8G8B8.
  // All other cases: pipe input is R8G8B8A8.
  SurfaceFormat inFormat;
  SurfaceFormat outFormat;
  if (mPixelFormat.value() == PixelFormat::Cmyk8 && mTransform) {
    inFormat = SurfaceFormat::R8G8B8;
    outFormat = SurfaceFormat::OS_RGBX;
  } else {
    inFormat = SurfaceFormat::R8G8B8A8;
    outFormat =
        basicInfo.has_alpha && mPixelFormat.value() != PixelFormat::Cmyk8
            ? SurfaceFormat::OS_RGBA
            : SurfaceFormat::OS_RGBX;
  }

  // mTransform usage: for Rgba8 it is passed to SurfacePipe for inline CMS;
  // for all other formats it is applied per-row in WritePixelRowsToPipe
  // (Rgba16f via qcms_transform_data_rgba_f16_to_rgba_u8, gray/CMYK via
  // qcms_transform_data). Null when CMS is off, the ICC profile is
  // missing/invalid, or the profile color space doesn't match mPixelFormat.
  bool usePipeTransform = mPixelFormat.value() == PixelFormat::Rgba8;
  qcms_transform* pipeTransform = usePipeTransform ? mTransform : nullptr;

  // jxl-rs always outputs straight alpha; the pipe handles premultiplication
  // for all formats. CMYK is excluded as its pipe input has no alpha channel.
  const bool wantPremultiply =
      !(GetSurfaceFlags() & SurfaceFlags::NO_PREMULTIPLY_ALPHA);
  SurfacePipeFlags pipeFlags = SurfacePipeFlags();
  if (wantPremultiply && mPixelFormat.value() != PixelFormat::Cmyk8) {
    pipeFlags |= SurfacePipeFlags::PREMULTIPLY_ALPHA;
  }

  mCurrentPipe = SurfacePipeFactory::CreateSurfacePipe(
      this, size, OutputSize(), FullFrame(), inFormat, outFormat, animParams,
      pipeTransform, pipeFlags);
  if (!mCurrentPipe) {
    mDecodeResult = DecodeResult::PipeInitError;
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

qcms_profile* nsJXLDecoder::MaybeCreateInputProfileFromCICP() {
  // Only RGB output maps to CICP; grayscale and CMYK use the ICC path.
  if (mPixelFormat.value() != PixelFormat::Rgba8 &&
      mPixelFormat.value() != PixelFormat::Rgba16f) {
    return nullptr;
  }

  // The CICP enums have uint8_t as their underlying type, so their storage can
  // be written through the uint8_t* out-params of the FFI (which can't traffic
  // in the C++ enum types).
  CICP::ColourPrimaries cicpPrimaries = CICP::ColourPrimaries::CP_UNSPECIFIED;
  CICP::TransferCharacteristics cicpTransfer =
      CICP::TransferCharacteristics::TC_UNSPECIFIED;
  // qcms_intent is not a uint8_t-backed enum like the CICP enums above, so the
  // FFI writes the rendering intent as a raw ICC code that we cast below.
  uint8_t cicpIntent = 0;
  if (!jxl_decoder_get_cicp(
          mDecoder.get(), reinterpret_cast<uint8_t*>(&cicpPrimaries),
          reinterpret_cast<uint8_t*>(&cicpTransfer), &cicpIntent)) {
    return nullptr;
  }

  // We still want to use the ICC for the HDR transfer functions (PQ and HLG)
  // because qcms currently doesn't tone map them, but the icc profile that
  // jxl-rs synthesizes does a good job of tonemapping for us.
  if (cicpTransfer == CICP::TC_SMPTE2084 || cicpTransfer == CICP::TC_HLG) {
    return nullptr;
  }

  qcms_profile* profile = qcms_profile_create_cicp_with_intent(
      cicpPrimaries, cicpTransfer, static_cast<qcms_intent>(cicpIntent));
  MOZ_LOG(
      sJXLLog, LogLevel::Debug,
      ("[this=%p] nsJXLDecoder::MaybeCreateInputProfileFromCICP -- "
       "CICP profile %s (primaries %u, transfer %u, intent %u)",
       this, profile ? "created" : "creation FAILED",
       static_cast<unsigned>(cicpPrimaries),
       static_cast<unsigned>(cicpTransfer), static_cast<unsigned>(cicpIntent)));
  return profile;
}

qcms_profile* nsJXLDecoder::MaybeCreateInputProfileFromICC() {
  size_t iccLen = 0;
  const uint8_t* iccData = jxl_decoder_get_icc_profile(mDecoder.get(), &iccLen);
  if (!iccData || !iccLen) {
    return nullptr;
  }

  qcms_profile* profile =
      qcms_profile_from_memory(reinterpret_cast<const char*>(iccData), iccLen);
  if (!profile) {
    MOZ_LOG(sJXLLog, LogLevel::Debug,
            ("[this=%p] nsJXLDecoder::MaybeCreateInputProfileFromICC -- "
             "failed to parse %zu-byte ICC profile",
             this, iccLen));
  }
  return profile;
}

void nsJXLDecoder::BuildCMSTransform() {
  // Prefer building the input profile directly from the output color space's
  // CICP code points; only fall back to the (larger, jxl-rs-synthesized) ICC
  // when CICP can't represent it.
  mInProfile = MaybeCreateInputProfileFromCICP();
  if (!mInProfile) {
    mInProfile = MaybeCreateInputProfileFromICC();
  }

  if (!mInProfile) {
    MOZ_LOG(sJXLLog, LogLevel::Debug,
            ("[this=%p] nsJXLDecoder::BuildCMSTransform -- no color profile "
             "available, skipping CMS",
             this));
    return;
  }

  auto intent = static_cast<qcms_intent>(gfxPlatform::GetRenderingIntent());
  if (intent < QCMS_INTENT_MIN || intent > QCMS_INTENT_MAX) {
    intent = qcms_profile_get_rendering_intent(mInProfile);
  }

  uint32_t profileSpace = qcms_profile_get_color_space(mInProfile);
  qcms_data_type inType;
  qcms_data_type outType;
  bool compatible = true;

  if (profileSpace == icSigGrayData) {
    if (mPixelFormat.value() != PixelFormat::Gray8 &&
        mPixelFormat.value() != PixelFormat::GrayAlpha8) {
      compatible = false;
    }
    // jxl-rs outputs Gray8 or GrayAlpha8; qcms produces Rgba8 output.
    inType = mPixelFormat.value() == PixelFormat::GrayAlpha8 ? QCMS_DATA_GRAYA_8
                                                             : QCMS_DATA_GRAY_8;
    outType = QCMS_DATA_RGBA_8;
  } else if (profileSpace == icSigCmykData) {
    if (mPixelFormat.value() != PixelFormat::Cmyk8) {
      compatible = false;
    }
    // jxl-rs outputs C,M,Y,_ in Rgba8 positions; K in mKBuffer.
    // qcms expects CMYK (0=no ink) and produces RGB8 output.
    inType = QCMS_DATA_CMYK;
    outType = QCMS_DATA_RGB_8;
  } else {
    if (mPixelFormat.value() != PixelFormat::Rgba8 &&
        mPixelFormat.value() != PixelFormat::Rgba16f) {
      compatible = false;
    }
    inType = QCMS_DATA_RGBA_8;
    outType = QCMS_DATA_RGBA_8;
  }

  if (compatible) {
    mTransform = qcms_transform_create(mInProfile, inType,
                                       GetCMSOutputProfile(), outType, intent);
    MOZ_LOG(sJXLLog, LogLevel::Debug,
            ("[this=%p] nsJXLDecoder::BuildCMSTransform -- CMS transform %s "
             "(color space 0x%x)",
             this, mTransform ? "created" : "creation FAILED", profileSpace));
  } else {
    MOZ_LOG(sJXLLog, LogLevel::Debug,
            ("[this=%p] nsJXLDecoder::BuildCMSTransform -- color space 0x%x "
             "incompatible with pixel format, skipping CMS",
             this, profileSpace));
  }
}

// IEEE 754 half-float to float, used for HDR fallback when no CMS transform.
static float F16ToF32(uint16_t h) {
  uint32_t sign = (h >> 15) & 1u;
  uint32_t exp = (h >> 10) & 0x1fu;
  uint32_t mantissa = h & 0x3ffu;
  uint32_t f;
  if (exp == 0) {
    f = sign << 31;  // zero (subnormals treated as zero)
  } else if (exp == 31) {
    f = (sign << 31) | (0xffu << 23) | (mantissa << 13);  // inf / nan
  } else {
    f = (sign << 31) | ((exp + 127u - 15u) << 23) | (mantissa << 13);
  }
  float result;
  memcpy(&result, &f, sizeof(result));
  return result;
}

bool nsJXLDecoder::WritePixelRowsToPipe() {
  MOZ_ASSERT(mCurrentPipe);
#ifdef DEBUG
  ++mWritePixelRowsCount;
#endif
  OrientedIntSize size = Size();

  uint8_t* currentRow = mPixelBuffer.begin();
  for (size_t y = 0; y < size_t(size.height); ++y) {
    uint8_t* pipeInput;
    if (mPixelFormat.value() == PixelFormat::Rgba16f) {
      if (mTransform) {
        qcms_transform_data_rgba_f16_to_rgba_u8(
            mTransform, reinterpret_cast<const uint16_t*>(currentRow),
            mU8RowBuf.begin(), size.width);
      } else {
        // No CMS: clip f16 to [0,1].
        const uint16_t* src = reinterpret_cast<const uint16_t*>(currentRow);
        for (size_t i = 0; i < size_t(size.width) * 4; ++i) {
          float v = F16ToF32(src[i]);
          mU8RowBuf[i] =
              v <= 0.0f ? 0 : (v >= 1.0f ? 255 : uint8_t(v * 255.0f + 0.5f));
        }
      }
      pipeInput = mU8RowBuf.begin();
    } else if (mPixelFormat.value() == PixelFormat::Gray8 ||
               mPixelFormat.value() == PixelFormat::GrayAlpha8) {
      if (mTransform) {
        // qcms reads the packed Gray8/GrayAlpha8 and produces Rgba8 output.
        qcms_transform_data(mTransform, currentRow, mU8RowBuf.begin(),
                            size.width);
      } else {
        // No CMS: expand gray → Rgba8 without color management.
        uint8_t* out = mU8RowBuf.begin();
        for (size_t x = 0; x < size_t(size.width); ++x) {
          uint8_t g = currentRow[x * BytesPerPixel()];
          uint8_t a = mPixelFormat.value() == PixelFormat::GrayAlpha8
                          ? currentRow[x * BytesPerPixel() + 1]
                          : 255;
          out[x * 4] = g;
          out[x * 4 + 1] = g;
          out[x * 4 + 2] = g;
          out[x * 4 + 3] = a;
        }
      }
      pipeInput = mU8RowBuf.begin();
    } else if (mPixelFormat.value() == PixelFormat::Cmyk8) {
      uint8_t* out = mU8RowBuf.begin();
      const uint8_t* kRow =
          mKBuffer.empty() ? nullptr : mKBuffer.begin() + y * size.width;
      if (mTransform) {
        // JXL CMYK: all channels use 0=max-ink, 255=no-ink; qcms uses 0=no-ink,
        // so invert all. qcms produces RGB8 (3 bytes/pixel); pipe was
        // configured with R8G8B8 inFormat.
        for (size_t x = 0; x < size_t(size.width); ++x) {
          out[x * 4] = 255 - currentRow[x * 4];
          out[x * 4 + 1] = 255 - currentRow[x * 4 + 1];
          out[x * 4 + 2] = 255 - currentRow[x * 4 + 2];
          // Alpha from the pixel buffer is unused; K fills the 4th CMYK slot.
          out[x * 4 + 3] = kRow ? (255 - kRow[x]) : 0;
        }
        qcms_transform_data(mTransform, out, out, size.width);
      } else {
        // No CMS: naive CMY+K → RGB without color management.
        // JXL encodes 0=max-ink, 255=no-ink, so R = C*K/255 gives correct
        // luminance.
        for (size_t x = 0; x < size_t(size.width); ++x) {
          uint8_t k = kRow ? kRow[x] : 255;
          out[x * 4] = (uint16_t)currentRow[x * 4] * k / 255;
          out[x * 4 + 1] = (uint16_t)currentRow[x * 4 + 1] * k / 255;
          out[x * 4 + 2] = (uint16_t)currentRow[x * 4 + 2] * k / 255;
          out[x * 4 + 3] = 255;
        }
      }
      pipeInput = out;
    } else {
      pipeInput = currentRow;
    }
    if (mCurrentPipe->WriteBuffer(reinterpret_cast<uint32_t*>(pipeInput)) ==
        WriteState::FAILURE) {
      mDecodeResult = DecodeResult::WriteError;
      return false;
    }
    currentRow += size.width * BytesPerPixel();
  }
  return true;
}

nsresult nsJXLDecoder::FinishFrame() {
  MOZ_ASSERT(HasSize());
  MOZ_ASSERT(mDecoder);

  nsresult rv = EnsureSurfacePipe();
  if (NS_FAILED(rv)) {
    return rv;
  }

  JxlBasicInfo basicInfo = jxl_decoder_get_basic_info(mDecoder.get());

  mCurrentPipe->ResetToFirstRow();
  if (!WritePixelRowsToPipe()) {
    mCurrentPipe.reset();
    return NS_ERROR_FAILURE;
  }

  if (Maybe<SurfaceInvalidRect> invalidRect = mCurrentPipe->TakeInvalidRect()) {
    PostInvalidation(invalidRect->mInputSpaceRect,
                     Some(invalidRect->mOutputSpaceRect));
  }

  // Cmyk8 images are always opaque: alpha is not decoded (see
  // WritePixelRowsToPipe).
  bool hasTransparency =
      basicInfo.has_alpha && mPixelFormat.value() != PixelFormat::Cmyk8;
  MOZ_LOG(sJXLLog, LogLevel::Debug,
          ("[this=%p] nsJXLDecoder::FinishFrame -- frame %u committed, "
           "hasTransparency=%d",
           this, mFrameIndex, hasTransparency));
  PostFrameStop(hasTransparency ? Opacity::SOME_TRANSPARENCY
                                : Opacity::FULLY_OPAQUE);
  mCurrentPipe.reset();
  mFrameCompleted = true;
  return NS_OK;
}

void nsJXLDecoder::FlushPartialFrame() {
  MOZ_ASSERT(!mPixelBuffer.empty());

  JxlDecoderStatus status = jxl_decoder_flush_pixels(
      mDecoder.get(), mPixelBuffer.begin(), mPixelBuffer.length(),
      mKBuffer.empty() ? nullptr : mKBuffer.begin(), mKBuffer.length());
  if (status != JxlDecoderStatus::Ok) {
    // Nothing new was rendered.
    return;
  }
  mPartialFrameRendered = true;

  // Lazily create the SurfacePipe now that we have content for it. Doing
  // this before any pixels are ready would expose an opaque-black surface
  // for images without an alpha channel.
  if (NS_FAILED(EnsureSurfacePipe())) {
    MOZ_LOG(sJXLLog, LogLevel::Error,
            ("[this=%p] nsJXLDecoder::FlushPartialFrame -- "
             "EnsureSurfacePipe failed\n",
             this));
    return;
  }

  mCurrentPipe->ResetToFirstRow();
  WritePixelRowsToPipe();

  if (Maybe<SurfaceInvalidRect> invalidRect = mCurrentPipe->TakeInvalidRect()) {
    PostInvalidation(invalidRect->mInputSpaceRect,
                     Some(invalidRect->mOutputSpaceRect));
  }
}

}  // namespace mozilla::image
