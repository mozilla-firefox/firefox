/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EncoderConfig.h"
#include "ImageContainer.h"
#include "MP4Decoder.h"
#include "VPXDecoder.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ImageUtils.h"

namespace mozilla {

CodecType EncoderConfig::CodecTypeForMime(const nsACString& aMimeType) {
  if (MP4Decoder::IsH264(aMimeType)) {
    return CodecType::H264;
  }
  if (VPXDecoder::IsVPX(aMimeType, VPXDecoder::VP8)) {
    return CodecType::VP8;
  }
  if (VPXDecoder::IsVPX(aMimeType, VPXDecoder::VP9)) {
    return CodecType::VP9;
  }
  MOZ_ASSERT_UNREACHABLE("Unsupported Mimetype");
  return CodecType::Unknown;
}

const char* CodecTypeStrings[] = {
    "BeginVideo", "H264", "VP8", "VP9",  "EndVideo", "Opus",   "Vorbis",
    "Flac",       "AAC",  "PCM", "G722", "EndAudio", "Unknown"};

nsCString EncoderConfig::ToString() const {
  nsCString rv;
  rv.Append(CodecTypeStrings[UnderlyingValue(mCodec)]);
  rv.AppendLiteral(mBitrateMode == BitrateMode::Constant ? " (CBR)" : " (VBR)");
  rv.AppendPrintf("%" PRIu32 "bps", mBitrate);
  if (mUsage == Usage::Realtime) {
    rv.AppendLiteral(", realtime");
  } else {
    rv.AppendLiteral(", record");
  }
  if (mCodec > CodecType::_BeginVideo_ && mCodec < CodecType::_EndVideo_) {
    rv.AppendPrintf(" [%dx%d]", mSize.Width(), mSize.Height());
    if (mHardwarePreference == HardwarePreference::RequireHardware) {
      rv.AppendLiteral(", hw required");
    } else if (mHardwarePreference == HardwarePreference::RequireSoftware) {
      rv.AppendLiteral(", sw required");
    } else {
      rv.AppendLiteral(", hw: no preference");
    }
    rv.AppendPrintf(" format: %s ", mFormat.ToString().get());
    if (mScalabilityMode == ScalabilityMode::L1T2) {
      rv.AppendLiteral(" (L1T2)");
    } else if (mScalabilityMode == ScalabilityMode::L1T3) {
      rv.AppendLiteral(" (L1T3)");
    }
    rv.AppendPrintf(", fps: %" PRIu8, mFramerate);
    rv.AppendPrintf(", kf interval: %zu", mKeyframeInterval);
  } else {
    rv.AppendPrintf(", ch: %" PRIu32 ", %" PRIu32 "Hz", mNumberOfChannels,
                    mSampleRate);
  }
  rv.AppendPrintf("(w/%s codec specific)", mCodecSpecific ? "" : "o");
  return rv;
};

static nsCString ColorRangeToString(const gfx::ColorRange& aColorRange) {
  switch (aColorRange) {
    case gfx::ColorRange::FULL:
      return "FULL"_ns;
    case gfx::ColorRange::LIMITED:
      return "LIMITED"_ns;
  }
  MOZ_ASSERT_UNREACHABLE("unknown ColorRange");
  return "unknown"_ns;
}

static nsCString YUVColorSpaceToString(
    const gfx::YUVColorSpace& aYUVColorSpace) {
  switch (aYUVColorSpace) {
    case gfx::YUVColorSpace::BT601:
      return "BT601"_ns;
    case gfx::YUVColorSpace::BT709:
      return "BT709"_ns;
    case gfx::YUVColorSpace::BT2020:
      return "BT2020"_ns;
    case gfx::YUVColorSpace::Identity:
      return "Identity"_ns;
  }
  MOZ_ASSERT_UNREACHABLE("unknown YUVColorSpace");
  return "unknown"_ns;
}

static nsCString ColorSpace2ToString(const gfx::ColorSpace2& aColorSpace2) {
  switch (aColorSpace2) {
    case gfx::ColorSpace2::Display:
      return "Display"_ns;
    case gfx::ColorSpace2::SRGB:
      return "SRGB"_ns;
    case gfx::ColorSpace2::DISPLAY_P3:
      return "DISPLAY_P3"_ns;
    case gfx::ColorSpace2::BT601_525:
      return "BT601_525"_ns;
    case gfx::ColorSpace2::BT709:
      return "BT709"_ns;
    case gfx::ColorSpace2::BT2020:
      return "BT2020"_ns;
  }
  MOZ_ASSERT_UNREACHABLE("unknown ColorSpace2");
  return "unknown"_ns;
}

static nsCString TransferFunctionToString(
    const gfx::TransferFunction& aTransferFunction) {
  switch (aTransferFunction) {
    case gfx::TransferFunction::BT709:
      return "BT709"_ns;
    case gfx::TransferFunction::SRGB:
      return "SRGB"_ns;
    case gfx::TransferFunction::PQ:
      return "PQ"_ns;
    case gfx::TransferFunction::HLG:
      return "HLG"_ns;
  }
  MOZ_ASSERT_UNREACHABLE("unknown TransferFunction");
  return "unknown"_ns;
}

nsCString EncoderConfig::VideoColorSpace::ToString() const {
  return nsPrintfCString(
      "VideoColorSpace: [range: %s, matrix: %s, primaries: %s, transfer: %s]",
      mRange ? ColorRangeToString(mRange.value()).get() : "none",
      mMatrix ? YUVColorSpaceToString(mMatrix.value()).get() : "none",
      mPrimaries ? ColorSpace2ToString(mPrimaries.value()).get() : "none",
      mTransferFunction
          ? TransferFunctionToString(mTransferFunction.value()).get()
          : "none");
}

nsCString EncoderConfig::SampleFormat::ToString() const {
  return nsPrintfCString("SampleFormat - [PixelFormat: %s, %s]",
                         dom::GetEnumString(mPixelFormat).get(),
                         mColorSpace.ToString().get());
}

Result<EncoderConfig::SampleFormat, MediaResult>
EncoderConfig::SampleFormat::FromImage(layers::Image* aImage) {
  if (!aImage) {
    return Err(MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, "No image"));
  }

  const dom::ImageUtils imageUtils(aImage);
  Maybe<dom::ImageBitmapFormat> format = imageUtils.GetFormat();
  if (format.isNothing()) {
    return Err(
        MediaResult(NS_ERROR_NOT_IMPLEMENTED,
                    nsPrintfCString("unsupported image format: %d",
                                    static_cast<int>(aImage->GetFormat()))));
  }

  if (layers::PlanarYCbCrImage* image = aImage->AsPlanarYCbCrImage()) {
    if (const layers::PlanarYCbCrImage::Data* yuv = image->GetData()) {
      return EncoderConfig::SampleFormat(
          format.ref(), EncoderConfig::VideoColorSpace(
                            yuv->mColorRange, yuv->mYUVColorSpace,
                            yuv->mColorPrimaries, yuv->mTransferFunction));
    }
    return Err(MediaResult(NS_ERROR_UNEXPECTED,
                           "failed to get YUV data from a YUV image"));
  }

  return EncoderConfig::SampleFormat(format.ref());
}

}  // namespace mozilla
