/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EncoderConfig.h"

#include "ImageContainer.h"
#include "MP4Decoder.h"
#include "VPXDecoder.h"
#include "mozilla/ToString.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ImageUtils.h"

namespace mozilla {

nsCString EncoderConfig::ToString() const {
  nsCString rv(EnumValueToString(mCodec));
  rv.AppendLiteral(mBitrateMode == BitrateMode::Constant ? " (CBR)" : " (VBR)");
  rv.AppendPrintf("%" PRIu32 "bps", mBitrate);
  if (mUsage == Usage::Realtime) {
    rv.AppendLiteral(", realtime");
  } else {
    rv.AppendLiteral(", record");
  }
  if (IsVideo()) {
    rv.AppendPrintf(" [%dx%d]", mSize.Width(), mSize.Height());
    if (mHardwarePreference == HardwarePreference::RequireHardware) {
      rv.AppendLiteral(", hw required");
    } else if (mHardwarePreference == HardwarePreference::RequireSoftware) {
      rv.AppendLiteral(", sw required");
    } else {
      rv.AppendLiteral(", hw: no preference");
    }
    rv.AppendPrintf(", %s", mFormat.ToString().get());
    if (mScalabilityMode == ScalabilityMode::L1T2) {
      rv.AppendLiteral(", L1T2");
    } else if (mScalabilityMode == ScalabilityMode::L1T3) {
      rv.AppendLiteral(", L1T3");
    }
    rv.AppendPrintf(", %" PRIu8 " fps", mFramerate);
    rv.AppendPrintf(", kf interval: %zu", mKeyframeInterval);
  } else {
    MOZ_ASSERT(IsAudio());
    rv.AppendPrintf(", ch: %" PRIu32 ", %" PRIu32 "Hz", mNumberOfChannels,
                    mSampleRate);
  }
  const char* specificStr = "";
  if (mCodecSpecific.is<void_t>()) {
    specificStr = "o";
  } else if (mCodecSpecific.is<H264Specific>()) {
    specificStr = " H264";
  } else if (mCodecSpecific.is<OpusSpecific>()) {
    specificStr = " Opus";
  } else if (mCodecSpecific.is<VP8Specific>()) {
    specificStr = " VP8";
  } else if (mCodecSpecific.is<VP9Specific>()) {
    specificStr = " VP9";
  } else {
    MOZ_ASSERT_UNREACHABLE("Unexpected codec specific type");
    specificStr = " unknown";
  }
  rv.AppendPrintf(" (w/%s codec specific)", specificStr);
  return rv;
};

nsCString EncoderConfig::VideoColorSpace::ToString() const {
  nsCString ret;
  ret.AppendFmt(
      "VideoColorSpace {{ range={}, matrix={}, primaries={}, transfer={} }}",
      mRange ? mozilla::ToString(mRange.value()) : "none",
      mMatrix ? mozilla::ToString(mMatrix.value()) : "none",
      mPrimaries ? mozilla::ToString(mPrimaries.value()) : "none",
      mTransferFunction ? mozilla::ToString(mTransferFunction.value())
                        : "none");
  return ret;
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
