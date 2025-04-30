/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageLogging.h"  // Must appear first
#include "nsPNGDecoder.h"

#include <algorithm>
#include <cstdint>

#include "gfxColor.h"
#include "gfxPlatform.h"
#include "imgFrame.h"
#include "nsColor.h"
#include "nsRect.h"
#include "nspr.h"
#include "png.h"

#include "RasterImage.h"
#include "SurfaceCache.h"
#include "SurfacePipeFactory.h"
#include "mozilla/DebugOnly.h"

using namespace mozilla::gfx;

using std::min;

namespace mozilla {
namespace image {

static LazyLogModule sPNGLog("PNGDecoder");
static LazyLogModule sPNGDecoderAccountingLog("PNGDecoderAccounting");

// limit image dimensions (bug #251381, #591822, #967656, and #1283961)
#ifndef MOZ_PNG_MAX_WIDTH
#  define MOZ_PNG_MAX_WIDTH 0x7fffffff  // Unlimited
#endif
#ifndef MOZ_PNG_MAX_HEIGHT
#  define MOZ_PNG_MAX_HEIGHT 0x7fffffff  // Unlimited
#endif

/* Controls the maximum chunk size configuration for libpng. We set this to a
 * very large number, 256MB specifically. */
static constexpr png_alloc_size_t kPngMaxChunkSize = 0x10000000;

nsPNGDecoder::AnimFrameInfo::AnimFrameInfo()
    : mDispose(DisposalMethod::KEEP), mBlend(BlendMethod::OVER), mTimeout(0) {}

#ifdef PNG_APNG_SUPPORTED

int32_t GetNextFrameDelay(png_structp aPNG, png_infop aInfo) {
  // Delay, in seconds, is delayNum / delayDen.
  png_uint_16 delayNum = png_get_next_frame_delay_num(aPNG, aInfo);
  png_uint_16 delayDen = png_get_next_frame_delay_den(aPNG, aInfo);

  if (delayNum == 0) {
    return 0;  // SetFrameTimeout() will set to a minimum.
  }

  if (delayDen == 0) {
    delayDen = 100;  // So says the APNG spec.
  }

  // Need to cast delay_num to float to have a proper division and
  // the result to int to avoid a compiler warning.
  return static_cast<int32_t>(static_cast<double>(delayNum) * 1000 / delayDen);
}

nsPNGDecoder::AnimFrameInfo::AnimFrameInfo(png_structp aPNG, png_infop aInfo)
    : mDispose(DisposalMethod::KEEP), mBlend(BlendMethod::OVER), mTimeout(0) {
  png_byte dispose_op = png_get_next_frame_dispose_op(aPNG, aInfo);
  png_byte blend_op = png_get_next_frame_blend_op(aPNG, aInfo);

  if (dispose_op == PNG_DISPOSE_OP_PREVIOUS) {
    mDispose = DisposalMethod::RESTORE_PREVIOUS;
  } else if (dispose_op == PNG_DISPOSE_OP_BACKGROUND) {
    mDispose = DisposalMethod::CLEAR;
  } else {
    mDispose = DisposalMethod::KEEP;
  }

  if (blend_op == PNG_BLEND_OP_SOURCE) {
    mBlend = BlendMethod::SOURCE;
  } else {
    mBlend = BlendMethod::OVER;
  }

  mTimeout = GetNextFrameDelay(aPNG, aInfo);
}
#endif

// First 8 bytes of a PNG file
const uint8_t nsPNGDecoder::pngSignatureBytes[] = {137, 80, 78, 71,
                                                   13,  10, 26, 10};

nsPNGDecoder::nsPNGDecoder(RasterImage* aImage)
    : Decoder(aImage),
      mLexer(Transition::ToUnbuffered(State::FINISHED_PNG_DATA, State::PNG_DATA,
                                      SIZE_MAX),
             Transition::TerminateSuccess()),
      mNextTransition(Transition::ContinueUnbuffered(State::PNG_DATA)),
      mLastChunkLength(0),
      mPNG(nullptr),
      mInfo(nullptr),
      mCMSLine(nullptr),
      interlacebuf(nullptr),
      mFormat(SurfaceFormat::UNKNOWN),
      mChannels(0),
      mPass(0),
      mFrameIsHidden(false),
      mDisablePremultipliedAlpha(false),
      mGotInfoCallback(false),
      mUsePipeTransform(false),
      mErrorIsRecoverable(false),
      mNumFrames(0) {}

nsPNGDecoder::~nsPNGDecoder() {
  if (mPNG) {
    png_destroy_read_struct(&mPNG, mInfo ? &mInfo : nullptr, nullptr);
  }
  if (mCMSLine) {
    free(mCMSLine);
  }
  if (interlacebuf) {
    free(interlacebuf);
  }
}

nsPNGDecoder::TransparencyType nsPNGDecoder::GetTransparencyType(
    const OrientedIntRect& aFrameRect) {
  // Check if the image has a transparent color in its palette.
  if (HasAlphaChannel()) {
    return TransparencyType::eAlpha;
  }
  if (!aFrameRect.IsEqualEdges(FullFrame())) {
    MOZ_ASSERT(HasAnimation());
    return TransparencyType::eFrameRect;
  }

  return TransparencyType::eNone;
}

void nsPNGDecoder::PostHasTransparencyIfNeeded(
    TransparencyType aTransparencyType) {
  switch (aTransparencyType) {
    case TransparencyType::eNone:
      return;

    case TransparencyType::eAlpha:
      PostHasTransparency();
      return;

    case TransparencyType::eFrameRect:
      // If the first frame of animated image doesn't draw into the whole image,
      // then record that it is transparent. For subsequent frames, this doesn't
      // affect transparency, because they're composited on top of all previous
      // frames.
      if (mNumFrames == 0) {
        PostHasTransparency();
      }
      return;
  }
}

// CreateFrame() is used for both simple and animated images.
nsresult nsPNGDecoder::CreateFrame(const FrameInfo& aFrameInfo) {
  MOZ_ASSERT(HasSize());
  MOZ_ASSERT(!IsMetadataDecode());

  // Check if we have transparency, and send notifications if needed.
  auto transparency = GetTransparencyType(aFrameInfo.mFrameRect);
  PostHasTransparencyIfNeeded(transparency);
  mFormat = transparency == TransparencyType::eNone ? SurfaceFormat::OS_RGBX
                                                    : SurfaceFormat::OS_RGBA;

  // Make sure there's no animation or padding if we're downscaling.
  MOZ_ASSERT_IF(Size() != OutputSize(), mNumFrames == 0);
  MOZ_ASSERT_IF(Size() != OutputSize(), !GetImageMetadata().HasAnimation());
  MOZ_ASSERT_IF(Size() != OutputSize(),
                transparency != TransparencyType::eFrameRect);

  Maybe<AnimationParams> animParams;
#ifdef PNG_APNG_SUPPORTED
  if (!IsFirstFrameDecode() && png_get_valid(mPNG, mInfo, PNG_INFO_acTL)) {
    mAnimInfo = AnimFrameInfo(mPNG, mInfo);

    if (mAnimInfo.mDispose == DisposalMethod::CLEAR) {
      // We may have to display the background under this image during
      // animation playback, so we regard it as transparent.
      PostHasTransparency();
    }

    animParams.emplace(
        AnimationParams{aFrameInfo.mFrameRect.ToUnknownRect(),
                        FrameTimeout::FromRawMilliseconds(mAnimInfo.mTimeout),
                        mNumFrames, mAnimInfo.mBlend, mAnimInfo.mDispose});
  }
#endif

  // If this image is interlaced, we can display better quality intermediate
  // results to the user by post processing them with ADAM7InterpolatingFilter.
  SurfacePipeFlags pipeFlags = aFrameInfo.mIsInterlaced
                                   ? SurfacePipeFlags::ADAM7_INTERPOLATE
                                   : SurfacePipeFlags();

  if (mNumFrames == 0) {
    // The first frame may be displayed progressively.
    pipeFlags |= SurfacePipeFlags::PROGRESSIVE_DISPLAY;
  }

  SurfaceFormat inFormat;
  if (mTransform && !mUsePipeTransform) {
    // QCMS will output in the correct format.
    inFormat = mFormat;
  } else if (transparency == TransparencyType::eAlpha) {
    // We are outputting directly as RGBA, so we need to swap at this step.
    inFormat = SurfaceFormat::R8G8B8A8;
  } else {
    // We have no alpha channel, so we need to unpack from RGB to BGRA.
    inFormat = SurfaceFormat::R8G8B8;
  }

  // Only apply premultiplication if the frame has true alpha. If we ever
  // support downscaling animated images, we will need to premultiply for frame
  // rect transparency when downscaling as well.
  if (transparency == TransparencyType::eAlpha && !mDisablePremultipliedAlpha) {
    pipeFlags |= SurfacePipeFlags::PREMULTIPLY_ALPHA;
  }

  qcms_transform* pipeTransform = mUsePipeTransform ? mTransform : nullptr;
  Maybe<SurfacePipe> pipe = SurfacePipeFactory::CreateSurfacePipe(
      this, Size(), OutputSize(), aFrameInfo.mFrameRect, inFormat, mFormat,
      animParams, pipeTransform, pipeFlags);

  if (!pipe) {
    mPipe = SurfacePipe();
    return NS_ERROR_FAILURE;
  }

  mPipe = std::move(*pipe);

  mFrameRect = aFrameInfo.mFrameRect;
  mPass = 0;

  MOZ_LOG(sPNGDecoderAccountingLog, LogLevel::Debug,
          ("PNGDecoderAccounting: nsPNGDecoder::CreateFrame -- created "
           "image frame with %dx%d pixels for decoder %p",
           mFrameRect.Width(), mFrameRect.Height(), this));

  return NS_OK;
}

// set timeout and frame disposal method for the current frame
void nsPNGDecoder::EndImageFrame() {
  if (mFrameIsHidden) {
    return;
  }

  mNumFrames++;

  Opacity opacity = mFormat == SurfaceFormat::OS_RGBX
                        ? Opacity::FULLY_OPAQUE
                        : Opacity::SOME_TRANSPARENCY;

  PostFrameStop(opacity);
}

nsresult nsPNGDecoder::InitInternal() {
  mDisablePremultipliedAlpha =
      bool(GetSurfaceFlags() & SurfaceFlags::NO_PREMULTIPLY_ALPHA);

#ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
  static png_byte color_chunks[] = {99,  72, 82, 77, '\0',     // cHRM
                                    105, 67, 67, 80, '\0'};    // iCCP
  static png_byte unused_chunks[] = {98,  75, 71, 68,  '\0',   // bKGD
                                     101, 88, 73, 102, '\0',   // eXIf
                                     104, 73, 83, 84,  '\0',   // hIST
                                     105, 84, 88, 116, '\0',   // iTXt
                                     111, 70, 70, 115, '\0',   // oFFs
                                     112, 67, 65, 76,  '\0',   // pCAL
                                     115, 67, 65, 76,  '\0',   // sCAL
                                     112, 72, 89, 115, '\0',   // pHYs
                                     115, 66, 73, 84,  '\0',   // sBIT
                                     115, 80, 76, 84,  '\0',   // sPLT
                                     116, 69, 88, 116, '\0',   // tEXt
                                     116, 73, 77, 69,  '\0',   // tIME
                                     122, 84, 88, 116, '\0'};  // zTXt
#endif

  // Initialize the container's source image header
  // Always decode to 24 bit pixdepth

  mPNG = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                nsPNGDecoder::error_callback,
                                nsPNGDecoder::warning_callback);
  if (!mPNG) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  mInfo = png_create_info_struct(mPNG);
  if (!mInfo) {
    png_destroy_read_struct(&mPNG, nullptr, nullptr);
    return NS_ERROR_OUT_OF_MEMORY;
  }

#ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
  // Ignore unused chunks
  if (mCMSMode == CMSMode::Off || IsMetadataDecode()) {
    png_set_keep_unknown_chunks(mPNG, 1, color_chunks, 2);
  }

  png_set_keep_unknown_chunks(mPNG, 1, unused_chunks,
                              (int)sizeof(unused_chunks) / 5);
#endif

#ifdef PNG_SET_USER_LIMITS_SUPPORTED
  png_set_user_limits(mPNG, MOZ_PNG_MAX_WIDTH, MOZ_PNG_MAX_HEIGHT);
  png_set_chunk_malloc_max(mPNG, kPngMaxChunkSize);
#endif

#ifdef PNG_READ_CHECK_FOR_INVALID_INDEX_SUPPORTED
  // Disallow palette-index checking, for speed; we would ignore the warning
  // anyhow.  This feature was added at libpng version 1.5.10 and is disabled
  // in the embedded libpng but enabled by default in the system libpng.  This
  // call also disables it in the system libpng, for decoding speed.
  // Bug #745202.
  png_set_check_for_invalid_index(mPNG, 0);
#endif

#ifdef PNG_SET_OPTION_SUPPORTED
#  if defined(PNG_sRGB_PROFILE_CHECKS) && PNG_sRGB_PROFILE_CHECKS >= 0
  // Skip checking of sRGB ICC profiles
  png_set_option(mPNG, PNG_SKIP_sRGB_CHECK_PROFILE, PNG_OPTION_ON);
#  endif

#  ifdef PNG_MAXIMUM_INFLATE_WINDOW
  // Force a larger zlib inflate window as some images in the wild have
  // incorrectly set metadata (specifically CMF bits) which prevent us from
  // decoding them otherwise.
  png_set_option(mPNG, PNG_MAXIMUM_INFLATE_WINDOW, PNG_OPTION_ON);
#  endif
#endif

  // use this as libpng "progressive pointer" (retrieve in callbacks)
  png_set_progressive_read_fn(
      mPNG, static_cast<png_voidp>(this), nsPNGDecoder::info_callback,
      nsPNGDecoder::row_callback, nsPNGDecoder::end_callback);

  return NS_OK;
}

LexerResult nsPNGDecoder::DoDecode(SourceBufferIterator& aIterator,
                                   IResumable* aOnResume) {
  MOZ_ASSERT(!HasError(), "Shouldn't call DoDecode after error!");

  return mLexer.Lex(aIterator, aOnResume,
                    [=](State aState, const char* aData, size_t aLength) {
                      switch (aState) {
                        case State::PNG_DATA:
                          return ReadPNGData(aData, aLength);
                        case State::FINISHED_PNG_DATA:
                          return FinishedPNGData();
                      }
                      MOZ_CRASH("Unknown State");
                    });
}

LexerTransition<nsPNGDecoder::State> nsPNGDecoder::ReadPNGData(
    const char* aData, size_t aLength) {
  // If we were waiting until after returning from a yield to call
  // CreateFrame(), call it now.
  if (mNextFrameInfo) {
    if (NS_FAILED(CreateFrame(*mNextFrameInfo))) {
      return Transition::TerminateFailure();
    }

    MOZ_ASSERT(mImageData, "Should have a buffer now");
    mNextFrameInfo = Nothing();
  }

  // libpng uses setjmp/longjmp for error handling.
  if (setjmp(png_jmpbuf(mPNG))) {
    return (GetFrameCount() > 0 && mErrorIsRecoverable)
               ? Transition::TerminateSuccess()
               : Transition::TerminateFailure();
  }

  // Pass the data off to libpng.
  mLastChunkLength = aLength;
  mNextTransition = Transition::ContinueUnbuffered(State::PNG_DATA);
  png_process_data(mPNG, mInfo,
                   reinterpret_cast<unsigned char*>(const_cast<char*>((aData))),
                   aLength);

  // Make sure that we've reached a terminal state if decoding is done.
  MOZ_ASSERT_IF(GetDecodeDone(), mNextTransition.NextStateIsTerminal());
  MOZ_ASSERT_IF(HasError(), mNextTransition.NextStateIsTerminal());

  // Continue with whatever transition the callback code requested. We
  // initialized this to Transition::ContinueUnbuffered(State::PNG_DATA) above,
  // so by default we just continue the unbuffered read.
  return mNextTransition;
}

LexerTransition<nsPNGDecoder::State> nsPNGDecoder::FinishedPNGData() {
  // Since we set up an unbuffered read for SIZE_MAX bytes, if we actually read
  // all that data something is really wrong.
  MOZ_ASSERT_UNREACHABLE("Read the entire address space?");
  return Transition::TerminateFailure();
}

// Sets up gamma pre-correction in libpng before our callback gets called.
// We need to do this if we don't end up with a CMS profile.
static void PNGDoGammaCorrection(png_structp png_ptr, png_infop info_ptr) {
  double aGamma;

  if (png_get_gAMA(png_ptr, info_ptr, &aGamma)) {
    if ((aGamma <= 0.0) || (aGamma > 21474.83)) {
      aGamma = 0.45455;
      png_set_gAMA(png_ptr, info_ptr, aGamma);
    }
    png_set_gamma(png_ptr, 2.2, aGamma);
  } else {
    png_set_gamma(png_ptr, 2.2, 0.45455);
  }
}

// Adapted from http://www.littlecms.com/pngchrm.c example code
uint32_t nsPNGDecoder::ReadColorProfile(png_structp png_ptr, png_infop info_ptr,
                                        int color_type, bool* sRGBTag) {
  // Check if cICP chunk is present
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_cICP)) {
    png_byte primaries;
    png_byte tc;
    png_byte matrix_coefficients;
    png_byte range;
    if (png_get_cICP(png_ptr, info_ptr, &primaries, &tc, &matrix_coefficients,
                     &range)) {
      if (matrix_coefficients == 0 && range <= 1) {
        if (range == 0) {
          MOZ_LOG(sPNGLog, LogLevel::Warning,
                  ("limited range specified in cicp chunk not properly "
                   "supported\n"));
        }

        mInProfile = qcms_profile_create_cicp(
            primaries, ChooseTransferCharacteristics(tc));
        if (mInProfile) {
          if (!(color_type & PNG_COLOR_MASK_COLOR)) {
            png_set_gray_to_rgb(png_ptr);
          }
          return qcms_profile_get_rendering_intent(mInProfile);
        }
      }
    }
  }

  // Check if iCCP chunk is present
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_iCCP)) {
    png_uint_32 profileLen;
    png_bytep profileData;
    png_charp profileName;
    int compression;

    png_get_iCCP(png_ptr, info_ptr, &profileName, &compression, &profileData,
                 &profileLen);

    mInProfile = qcms_profile_from_memory((char*)profileData, profileLen);
    if (mInProfile) {
      uint32_t profileSpace = qcms_profile_get_color_space(mInProfile);

      bool mismatch = false;
      if (color_type & PNG_COLOR_MASK_COLOR) {
        if (profileSpace != icSigRgbData) {
          mismatch = true;
        }
      } else {
        if (profileSpace == icSigRgbData) {
          png_set_gray_to_rgb(png_ptr);
        } else if (profileSpace != icSigGrayData) {
          mismatch = true;
        }
      }

      if (mismatch) {
        qcms_profile_release(mInProfile);
        mInProfile = nullptr;
      } else {
        return qcms_profile_get_rendering_intent(mInProfile);
      }
    }
  }

  // Check sRGB chunk
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_sRGB)) {
    *sRGBTag = true;

    int fileIntent;
    png_set_gray_to_rgb(png_ptr);
    png_get_sRGB(png_ptr, info_ptr, &fileIntent);
    uint32_t map[] = {QCMS_INTENT_PERCEPTUAL, QCMS_INTENT_RELATIVE_COLORIMETRIC,
                      QCMS_INTENT_SATURATION,
                      QCMS_INTENT_ABSOLUTE_COLORIMETRIC};
    return map[fileIntent];
  }

  // Check gAMA/cHRM chunks
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_gAMA) &&
      png_get_valid(png_ptr, info_ptr, PNG_INFO_cHRM)) {
    qcms_CIE_xyYTRIPLE primaries;
    qcms_CIE_xyY whitePoint;

    png_get_cHRM(png_ptr, info_ptr, &whitePoint.x, &whitePoint.y,
                 &primaries.red.x, &primaries.red.y, &primaries.green.x,
                 &primaries.green.y, &primaries.blue.x, &primaries.blue.y);
    whitePoint.Y = primaries.red.Y = primaries.green.Y = primaries.blue.Y = 1.0;

    double gammaOfFile;

    png_get_gAMA(png_ptr, info_ptr, &gammaOfFile);

    mInProfile = qcms_profile_create_rgb_with_gamma(whitePoint, primaries,
                                                    1.0 / gammaOfFile);

    if (mInProfile) {
      png_set_gray_to_rgb(png_ptr);
    }
  }

  return QCMS_INTENT_PERCEPTUAL;  // Our default
}

void nsPNGDecoder::info_callback(png_structp png_ptr, png_infop info_ptr) {
  png_uint_32 width, height;
  int bit_depth, color_type, interlace_type, compression_type, filter_type;
  unsigned int channels;

  png_bytep trans = nullptr;
  int num_trans = 0;

  nsPNGDecoder* decoder =
      static_cast<nsPNGDecoder*>(png_get_progressive_ptr(png_ptr));

  if (decoder->mGotInfoCallback) {
    MOZ_LOG(sPNGLog, LogLevel::Warning,
            ("libpng called info_callback more than once\n"));
    return;
  }

  decoder->mGotInfoCallback = true;

  // Always decode to 24-bit RGB or 32-bit RGBA
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
               &interlace_type, &compression_type, &filter_type);

  const OrientedIntRect frameRect(0, 0, width, height);

  // Post our size to the superclass
  decoder->PostSize(frameRect.Width(), frameRect.Height());

  if (width > SurfaceCache::MaximumCapacity() / (bit_depth > 8 ? 16 : 8)) {
    // libpng needs space to allocate two row buffers
    png_error(decoder->mPNG, "Image is too wide");
  }

  if (decoder->HasError()) {
    // Setting the size led to an error.
    png_error(decoder->mPNG, "Sizing error");
  }

  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    png_set_expand(png_ptr);
  }

  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
    png_set_expand(png_ptr);
  }

  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
    png_color_16p trans_values;
    png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, &trans_values);
    if (num_trans != 0) {
      png_set_expand(png_ptr);
    }
  }

  if (bit_depth == 16) {
    png_set_scale_16(png_ptr);
  }

  // We only need to extract the color profile for non-metadata decodes. It is
  // fairly expensive to read the profile and create the transform so we should
  // avoid it if not necessary.
  uint32_t intent = -1;
  bool sRGBTag = false;
  if (!decoder->IsMetadataDecode()) {
    if (decoder->mCMSMode != CMSMode::Off) {
      intent = gfxPlatform::GetRenderingIntent();
      uint32_t pIntent =
          decoder->ReadColorProfile(png_ptr, info_ptr, color_type, &sRGBTag);
      // If we're not mandating an intent, use the one from the image.
      if (intent == uint32_t(-1)) {
        intent = pIntent;
      }
    }
    const bool hasColorInfo = decoder->mInProfile || sRGBTag;
    if (!hasColorInfo || !decoder->GetCMSOutputProfile()) {
      png_set_gray_to_rgb(png_ptr);

      // only do gamma correction if CMS isn't entirely disabled
      if (decoder->mCMSMode != CMSMode::Off) {
        PNGDoGammaCorrection(png_ptr, info_ptr);
      }
    }
  }

  // Let libpng expand interlaced images.
  const bool isInterlaced = interlace_type == PNG_INTERLACE_ADAM7;
  if (isInterlaced) {
    png_set_interlace_handling(png_ptr);
  }

  // now all of those things we set above are used to update various struct
  // members and whatnot, after which we can get channels, rowbytes, etc.
  png_read_update_info(png_ptr, info_ptr);
  decoder->mChannels = channels = png_get_channels(png_ptr, info_ptr);

  //---------------------------------------------------------------//
  // copy PNG info into imagelib structs (formerly png_set_dims()) //
  //---------------------------------------------------------------//

  if (channels < 1 || channels > 4) {
    png_error(decoder->mPNG, "Invalid number of channels");
  }

#ifdef PNG_APNG_SUPPORTED
  bool isAnimated = png_get_valid(png_ptr, info_ptr, PNG_INFO_acTL);
  if (isAnimated) {
    int32_t rawTimeout = GetNextFrameDelay(png_ptr, info_ptr);
    decoder->PostIsAnimated(FrameTimeout::FromRawMilliseconds(rawTimeout));

    if (decoder->Size() != decoder->OutputSize() &&
        !decoder->IsFirstFrameDecode()) {
      MOZ_ASSERT_UNREACHABLE(
          "Doing downscale-during-decode "
          "for an animated image?");
      png_error(decoder->mPNG, "Invalid downscale attempt");  // Abort decode.
    }
  }
#endif

  auto transparency = decoder->GetTransparencyType(frameRect);
  if (decoder->IsMetadataDecode()) {
    // If we are animated then the first frame rect is either:
    // 1) the whole image if the IDAT chunk is part of the animation
    // 2) the frame rect of the first fDAT chunk otherwise.
    // If we are not animated then we want to make sure to call
    // PostHasTransparency in the metadata decode if we need to. So it's
    // okay to pass IntRect(0, 0, width, height) here for animated images;
    // they will call with the proper first frame rect in the full decode.
    decoder->PostHasTransparencyIfNeeded(transparency);

    // We have the metadata we're looking for, so stop here, before we allocate
    // buffers below.
    return decoder->DoTerminate(png_ptr, TerminalState::SUCCESS);
  }

  if (decoder->mInProfile && decoder->GetCMSOutputProfile()) {
    qcms_data_type inType;
    qcms_data_type outType;

    uint32_t profileSpace = qcms_profile_get_color_space(decoder->mInProfile);
    decoder->mUsePipeTransform = profileSpace != icSigGrayData;
    if (decoder->mUsePipeTransform) {
      // libpng outputs data in RGBA order and we want our final output to be
      // BGRA order. SurfacePipe takes care of this for us but unfortunately the
      // swizzle to change the order can happen before or after color management
      // depending on if we have alpha. If we have alpha then the order will be
      // color management then swizzle. If we do not have alpha then the order
      // will be swizzle then color management. See CreateSurfacePipe
      // https://searchfox.org/mozilla-central/rev/7d6651d29c5c1620bc059f879a3e9bbfb53f271f/image/SurfacePipeFactory.h#133-145
      if (transparency == TransparencyType::eAlpha) {
        inType = QCMS_DATA_RGBA_8;
        outType = QCMS_DATA_RGBA_8;
      } else {
        inType = gfxPlatform::GetCMSOSRGBAType();
        outType = inType;
      }
    } else {
      // qcms operates on the data before we hand it to SurfacePipe.
      if (color_type & PNG_COLOR_MASK_ALPHA) {
        inType = QCMS_DATA_GRAYA_8;
        outType = gfxPlatform::GetCMSOSRGBAType();
      } else {
        inType = QCMS_DATA_GRAY_8;
        outType = gfxPlatform::GetCMSOSRGBAType();
      }
    }

    decoder->mTransform = qcms_transform_create(decoder->mInProfile, inType,
                                                decoder->GetCMSOutputProfile(),
                                                outType, (qcms_intent)intent);
  } else if ((sRGBTag && decoder->mCMSMode == CMSMode::TaggedOnly) ||
             decoder->mCMSMode == CMSMode::All) {
    // See comment above about SurfacePipe, color management and ordering.
    decoder->mUsePipeTransform = true;
    if (transparency == TransparencyType::eAlpha) {
      decoder->mTransform =
          decoder->GetCMSsRGBTransform(SurfaceFormat::R8G8B8A8);
    } else {
      decoder->mTransform =
          decoder->GetCMSsRGBTransform(SurfaceFormat::OS_RGBA);
    }
  }

#ifdef PNG_APNG_SUPPORTED
  if (isAnimated) {
    png_set_progressive_frame_fn(png_ptr, nsPNGDecoder::frame_info_callback,
                                 nullptr);
  }

  if (png_get_first_frame_is_hidden(png_ptr, info_ptr)) {
    decoder->mFrameIsHidden = true;
  } else {
#endif
    nsresult rv = decoder->CreateFrame(FrameInfo{frameRect, isInterlaced});
    if (NS_FAILED(rv)) {
      png_error(decoder->mPNG, "CreateFrame failed");
    }
    MOZ_ASSERT(decoder->mImageData, "Should have a buffer now");
#ifdef PNG_APNG_SUPPORTED
  }
#endif

  if (decoder->mTransform && !decoder->mUsePipeTransform) {
    decoder->mCMSLine =
        static_cast<uint8_t*>(malloc(sizeof(uint32_t) * frameRect.Width()));
    if (!decoder->mCMSLine) {
      png_error(decoder->mPNG, "malloc of mCMSLine failed");
    }
  }

  if (interlace_type == PNG_INTERLACE_ADAM7) {
    if (frameRect.Height() <
        INT32_MAX / (frameRect.Width() * int32_t(channels))) {
      const size_t bufferSize =
          channels * frameRect.Width() * frameRect.Height();

      if (bufferSize > SurfaceCache::MaximumCapacity()) {
        png_error(decoder->mPNG, "Insufficient memory to deinterlace image");
      }

      decoder->interlacebuf = static_cast<uint8_t*>(malloc(bufferSize));
    }
    if (!decoder->interlacebuf) {
      png_error(decoder->mPNG, "malloc of interlacebuf failed");
    }
  }
}

void nsPNGDecoder::PostInvalidationIfNeeded() {
  Maybe<SurfaceInvalidRect> invalidRect = mPipe.TakeInvalidRect();
  if (!invalidRect) {
    return;
  }

  PostInvalidation(invalidRect->mInputSpaceRect,
                   Some(invalidRect->mOutputSpaceRect));
}

void nsPNGDecoder::row_callback(png_structp png_ptr, png_bytep new_row,
                                png_uint_32 row_num, int pass) {
  /* libpng comments:
   *
   * This function is called for every row in the image.  If the
   * image is interlacing, and you turned on the interlace handler,
   * this function will be called for every row in every pass.
   * Some of these rows will not be changed from the previous pass.
   * When the row is not changed, the new_row variable will be
   * nullptr. The rows and passes are called in order, so you don't
   * really need the row_num and pass, but I'm supplying them
   * because it may make your life easier.
   *
   * For the non-nullptr rows of interlaced images, you must call
   * png_progressive_combine_row() passing in the row and the
   * old row.  You can call this function for nullptr rows (it will
   * just return) and for non-interlaced images (it just does the
   * memcpy for you) if it will make the code easier.  Thus, you
   * can just do this for all cases:
   *
   *    png_progressive_combine_row(png_ptr, old_row, new_row);
   *
   * where old_row is what was displayed for previous rows.  Note
   * that the first pass (pass == 0 really) will completely cover
   * the old row, so the rows do not have to be initialized.  After
   * the first pass (and only for interlaced images), you will have
   * to pass the current row, and the function will combine the
   * old row and the new row.
   */
  nsPNGDecoder* decoder =
      static_cast<nsPNGDecoder*>(png_get_progressive_ptr(png_ptr));

  if (decoder->mFrameIsHidden) {
    return;  // Skip this frame.
  }

  MOZ_ASSERT_IF(decoder->IsFirstFrameDecode(), decoder->mNumFrames == 0);

  while (pass > decoder->mPass) {
    // Advance to the next pass. We may have to do this multiple times because
    // libpng will skip passes if the image is so small that no pixels have
    // changed on a given pass, but ADAM7InterpolatingFilter needs to be reset
    // once for every pass to perform interpolation properly.
    decoder->mPipe.ResetToFirstRow();
    decoder->mPass++;
  }

  const png_uint_32 height =
      static_cast<png_uint_32>(decoder->mFrameRect.Height());

  if (row_num >= height) {
    // Bail if we receive extra rows. This is especially important because if we
    // didn't, we might overflow the deinterlacing buffer.
    MOZ_ASSERT_UNREACHABLE("libpng producing extra rows?");
    return;
  }

  // Note that |new_row| may be null here, indicating that this is an interlaced
  // image and |row_callback| is being called for a row that hasn't changed.
  MOZ_ASSERT_IF(!new_row, decoder->interlacebuf);

  if (decoder->interlacebuf) {
    uint32_t width = uint32_t(decoder->mFrameRect.Width());

    // We'll output the deinterlaced version of the row.
    uint8_t* rowToWrite =
        decoder->interlacebuf + (row_num * decoder->mChannels * width);

    // Update the deinterlaced version of this row with the new data.
    png_progressive_combine_row(png_ptr, rowToWrite, new_row);

    decoder->WriteRow(rowToWrite);
  } else {
    decoder->WriteRow(new_row);
  }
}

void nsPNGDecoder::WriteRow(uint8_t* aRow) {
  MOZ_ASSERT(aRow);

  uint8_t* rowToWrite = aRow;
  uint32_t width = uint32_t(mFrameRect.Width());

  // Apply color management to the row, if necessary, before writing it out.
  // This is only needed for grayscale images.
  if (mTransform && !mUsePipeTransform) {
    MOZ_ASSERT(mCMSLine);
    qcms_transform_data(mTransform, rowToWrite, mCMSLine, width);
    rowToWrite = mCMSLine;
  }

  // Write this row to the SurfacePipe.
  DebugOnly<WriteState> result =
      mPipe.WriteBuffer(reinterpret_cast<uint32_t*>(rowToWrite));
  MOZ_ASSERT(WriteState(result) != WriteState::FAILURE);

  PostInvalidationIfNeeded();
}

void nsPNGDecoder::DoTerminate(png_structp aPNGStruct, TerminalState aState) {
  // Stop processing data. Note that we intentionally ignore the return value of
  // png_process_data_pause(), which tells us how many bytes of the data that
  // was passed to png_process_data() have not been consumed yet, because now
  // that we've reached a terminal state, we won't do any more decoding or call
  // back into libpng anymore.
  png_process_data_pause(aPNGStruct, /* save = */ false);

  mNextTransition = aState == TerminalState::SUCCESS
                        ? Transition::TerminateSuccess()
                        : Transition::TerminateFailure();
}

void nsPNGDecoder::DoYield(png_structp aPNGStruct) {
  // Pause data processing. png_process_data_pause() returns how many bytes of
  // the data that was passed to png_process_data() have not been consumed yet.
  // We use this information to tell StreamingLexer where to place us in the
  // input stream when we come back from the yield.
  png_size_t pendingBytes = png_process_data_pause(aPNGStruct,
                                                   /* save = */ false);

  MOZ_ASSERT(pendingBytes < mLastChunkLength);
  size_t consumedBytes = mLastChunkLength - min(pendingBytes, mLastChunkLength);

  mNextTransition =
      Transition::ContinueUnbufferedAfterYield(State::PNG_DATA, consumedBytes);
}

nsresult nsPNGDecoder::FinishInternal() {
  // We shouldn't be called in error cases.
  MOZ_ASSERT(!HasError(), "Can't call FinishInternal on error!");

  int32_t loop_count = 0;
  uint32_t frame_count = 1;
#ifdef PNG_APNG_SUPPORTED
  uint32_t num_plays = 0;
  if (png_get_acTL(mPNG, mInfo, &frame_count, &num_plays)) {
    loop_count = int32_t(num_plays) - 1;
  } else {
    frame_count = 1;
  }
#endif

  PostLoopCount(loop_count);

  if (WantsFrameCount()) {
    PostFrameCount(frame_count);
  }

  if (IsMetadataDecode()) {
    return NS_OK;
  }

  if (InFrame()) {
    EndImageFrame();
  }
  PostDecodeDone();

  return NS_OK;
}

#ifdef PNG_APNG_SUPPORTED
// got the header of a new frame that's coming
void nsPNGDecoder::frame_info_callback(png_structp png_ptr,
                                       png_uint_32 frame_num) {
  nsPNGDecoder* decoder =
      static_cast<nsPNGDecoder*>(png_get_progressive_ptr(png_ptr));

  // old frame is done
  decoder->EndImageFrame();

  const bool previousFrameWasHidden = decoder->mFrameIsHidden;

  if (!previousFrameWasHidden && decoder->IsFirstFrameDecode()) {
    // We're about to get a second non-hidden frame, but we only want the first.
    // Stop decoding now. (And avoid allocating the unnecessary buffers below.)
    return decoder->DoTerminate(png_ptr, TerminalState::SUCCESS);
  }

  // Only the first frame can be hidden, so unhide unconditionally here.
  decoder->mFrameIsHidden = false;

  // Save the information necessary to create the frame; we'll actually create
  // it when we return from the yield.
  const OrientedIntRect frameRect(
      png_get_next_frame_x_offset(png_ptr, decoder->mInfo),
      png_get_next_frame_y_offset(png_ptr, decoder->mInfo),
      png_get_next_frame_width(png_ptr, decoder->mInfo),
      png_get_next_frame_height(png_ptr, decoder->mInfo));
  const bool isInterlaced = bool(decoder->interlacebuf);

#  ifndef MOZ_EMBEDDED_LIBPNG
  // if using system library, check frame_width and height against 0
  if (frameRect.width == 0) {
    png_error(png_ptr, "Frame width must not be 0");
  }
  if (frameRect.height == 0) {
    png_error(png_ptr, "Frame height must not be 0");
  }
#  endif

  const FrameInfo info{frameRect, isInterlaced};

  // If the previous frame was hidden, skip the yield (which will mislead the
  // caller, who will think the previous frame was real) and just allocate the
  // new frame here.
  if (previousFrameWasHidden) {
    if (NS_FAILED(decoder->CreateFrame(info))) {
      return decoder->DoTerminate(png_ptr, TerminalState::FAILURE);
    }

    MOZ_ASSERT(decoder->mImageData, "Should have a buffer now");
    return;  // No yield, so we'll just keep decoding.
  }

  // Yield to the caller to notify them that the previous frame is now complete.
  decoder->mNextFrameInfo = Some(info);
  return decoder->DoYield(png_ptr);
}
#endif

void nsPNGDecoder::end_callback(png_structp png_ptr, png_infop info_ptr) {
  /* libpng comments:
   *
   * this function is called when the whole image has been read,
   * including any chunks after the image (up to and including
   * the IEND).  You will usually have the same info chunk as you
   * had in the header, although some data may have been added
   * to the comments and time fields.
   *
   * Most people won't do much here, perhaps setting a flag that
   * marks the image as finished.
   */

  nsPNGDecoder* decoder =
      static_cast<nsPNGDecoder*>(png_get_progressive_ptr(png_ptr));

  // We shouldn't get here if we've hit an error
  MOZ_ASSERT(!decoder->HasError(), "Finishing up PNG but hit error!");

  return decoder->DoTerminate(png_ptr, TerminalState::SUCCESS);
}

void nsPNGDecoder::error_callback(png_structp png_ptr,
                                  png_const_charp error_msg) {
  MOZ_LOG(sPNGLog, LogLevel::Error, ("libpng error: %s\n", error_msg));

  nsPNGDecoder* decoder =
      static_cast<nsPNGDecoder*>(png_get_progressive_ptr(png_ptr));

  if (strstr(error_msg, "invalid chunk type") ||
      strstr(error_msg, "bad header (invalid type)")) {
    decoder->mErrorIsRecoverable = true;
  } else {
    decoder->mErrorIsRecoverable = false;
  }

  png_longjmp(png_ptr, 1);
}

void nsPNGDecoder::warning_callback(png_structp png_ptr,
                                    png_const_charp warning_msg) {
  MOZ_LOG(sPNGLog, LogLevel::Warning, ("libpng warning: %s\n", warning_msg));
}

Maybe<glean::impl::MemoryDistributionMetric> nsPNGDecoder::SpeedMetric() const {
  return Some(glean::image_decode::speed_png);
}

bool nsPNGDecoder::IsValidICOResource() const {
  // Only 32-bit RGBA PNGs are valid ICO resources; see here:
  //   http://blogs.msdn.com/b/oldnewthing/archive/2010/10/22/10079192.aspx

  // If there are errors in the call to png_get_IHDR, the error_callback in
  // nsPNGDecoder.cpp is called.  In this error callback we do a longjmp, so
  // we need to save the jump buffer here. Otherwise we'll end up without a
  // proper callstack.
  if (setjmp(png_jmpbuf(mPNG))) {
    // We got here from a longjmp call indirectly from png_get_IHDR via
    // error_callback. Ignore mErrorIsRecoverable: if we got an invalid chunk
    // error before even reading the IHDR we can't recover from that.
    return false;
  }

  png_uint_32 png_width,  // Unused
      png_height;         // Unused

  int png_bit_depth, png_color_type;

  if (png_get_IHDR(mPNG, mInfo, &png_width, &png_height, &png_bit_depth,
                   &png_color_type, nullptr, nullptr, nullptr)) {
    return ((png_color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
             png_color_type == PNG_COLOR_TYPE_RGB) &&
            png_bit_depth == 8);
  } else {
    return false;
  }
}

}  // namespace image
}  // namespace mozilla
