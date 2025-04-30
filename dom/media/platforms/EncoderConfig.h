/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_EncoderConfig_h_
#define mozilla_EncoderConfig_h_

#include "H264.h"
#include "MediaResult.h"
#include "mozilla/Result.h"
#include "mozilla/Variant.h"
#include "mozilla/dom/ImageBitmapBinding.h"

namespace mozilla {

namespace layers {
class Image;
}  // namespace layers

enum class CodecType {
  _BeginVideo_,
  H264,
  H265,
  VP8,
  VP9,
  AV1,
  _EndVideo_,
  _BeginAudio_ = _EndVideo_,
  Opus,
  Vorbis,
  Flac,
  AAC,
  PCM,
  G722,
  _EndAudio_,
  Unknown,
};

enum class Usage {
  Realtime,  // Low latency prefered
  Record
};
enum class BitrateMode { Constant, Variable };
// Scalable Video Coding (SVC) settings for WebCodecs:
// https://www.w3.org/TR/webrtc-svc/
enum class ScalabilityMode { None, L1T2, L1T3 };

enum class HardwarePreference { RequireHardware, RequireSoftware, None };

// TODO: Automatically generate this (Bug 1865896)
const char* GetCodecTypeString(const CodecType& aCodecType);

enum class H264BitStreamFormat { AVC, ANNEXB };

struct H264Specific final {
  const H264_PROFILE mProfile;
  const H264_LEVEL mLevel;
  const H264BitStreamFormat mFormat;

  H264Specific(H264_PROFILE aProfile, H264_LEVEL aLevel,
               H264BitStreamFormat aFormat)
      : mProfile(aProfile), mLevel(aLevel), mFormat(aFormat) {}
};

enum class OpusBitstreamFormat { Opus, OGG };

// The default values come from the Web Codecs specification.
struct OpusSpecific final {
  enum class Application { Unspecified, Voip, Audio, RestricedLowDelay };
  Application mApplication = Application::Unspecified;
  uint64_t mFrameDuration = 20000;  // microseconds
  uint8_t mComplexity = 10;         // 0-10
  OpusBitstreamFormat mFormat = OpusBitstreamFormat::Opus;
  uint64_t mPacketLossPerc = 0;  // 0-100
  bool mUseInBandFEC = false;
  bool mUseDTX = false;
};

enum class VPXComplexity { Normal, High, Higher, Max };
struct VP8Specific {
  VP8Specific() = default;
  // Ignore webrtc::VideoCodecVP8::errorConcealmentOn,
  // for it's always false in the codebase (except libwebrtc test cases).
  VP8Specific(const VPXComplexity aComplexity, const bool aResilience,
              const uint8_t aNumTemporalLayers, const bool aDenoising,
              const bool aAutoResize, const bool aFrameDropping)
      : mComplexity(aComplexity),
        mResilience(aResilience),
        mNumTemporalLayers(aNumTemporalLayers),
        mDenoising(aDenoising),
        mAutoResize(aAutoResize),
        mFrameDropping(aFrameDropping) {}
  const VPXComplexity mComplexity{VPXComplexity::Normal};
  const bool mResilience{true};
  const uint8_t mNumTemporalLayers{1};
  const bool mDenoising{true};
  const bool mAutoResize{false};
  const bool mFrameDropping{false};
};

struct VP9Specific : public VP8Specific {
  VP9Specific() = default;
  VP9Specific(const VPXComplexity aComplexity, const bool aResilience,
              const uint8_t aNumTemporalLayers, const bool aDenoising,
              const bool aAutoResize, const bool aFrameDropping,
              const bool aAdaptiveQp, const uint8_t aNumSpatialLayers,
              const bool aFlexible)
      : VP8Specific(aComplexity, aResilience, aNumTemporalLayers, aDenoising,
                    aAutoResize, aFrameDropping),
        mAdaptiveQp(aAdaptiveQp),
        mNumSpatialLayers(aNumSpatialLayers),
        mFlexible(aFlexible) {}
  const bool mAdaptiveQp{true};
  const uint8_t mNumSpatialLayers{1};
  const bool mFlexible{false};
};

// A class that holds the intial configuration of an encoder. For simplicity,
// this is used for both audio and video encoding. Members irrelevant to the
// instance are to be ignored, and are set at their default value.
class EncoderConfig final {
 public:
  using CodecSpecific =
      Variant<H264Specific, OpusSpecific, VP8Specific, VP9Specific>;

  struct VideoColorSpace {
    Maybe<gfx::ColorRange> mRange;
    Maybe<gfx::YUVColorSpace> mMatrix;
    Maybe<gfx::ColorSpace2> mPrimaries;
    Maybe<gfx::TransferFunction> mTransferFunction;

    VideoColorSpace() = default;
    VideoColorSpace(const gfx::ColorRange& aColorRange,
                    const gfx::YUVColorSpace& aMatrix,
                    const gfx::ColorSpace2& aPrimaries,
                    const gfx::TransferFunction& aTransferFunction)
        : mRange(Some(aColorRange)),
          mMatrix(Some(aMatrix)),
          mPrimaries(Some(aPrimaries)),
          mTransferFunction(Some(aTransferFunction)) {}

    bool operator==(const VideoColorSpace& aOther) const {
      return mRange == aOther.mRange && mMatrix == aOther.mMatrix &&
             mPrimaries == aOther.mPrimaries &&
             mTransferFunction == aOther.mTransferFunction;
    }
    bool operator!=(const VideoColorSpace& aOther) const {
      return !(*this == aOther);
    }
    nsCString ToString() const;
  };

  struct SampleFormat {
    dom::ImageBitmapFormat mPixelFormat;
    VideoColorSpace mColorSpace;

    SampleFormat(const dom::ImageBitmapFormat& aPixelFormat,
                 const VideoColorSpace& aColorSpace)
        : mPixelFormat(aPixelFormat), mColorSpace(aColorSpace) {}
    explicit SampleFormat(const dom::ImageBitmapFormat& aPixelFormat)
        : mPixelFormat(aPixelFormat) {}
    SampleFormat() = default;

    bool operator==(const SampleFormat& aOther) const {
      return mPixelFormat == aOther.mPixelFormat &&
             mColorSpace == aOther.mColorSpace;
    }
    bool operator!=(const SampleFormat& aOther) const {
      return !(*this == aOther);
    }

    nsCString ToString() const;

    bool IsRGB32() const {
      return mPixelFormat == dom::ImageBitmapFormat::BGRA32 ||
             mPixelFormat == dom::ImageBitmapFormat::RGBA32;
    }
    bool IsYUV() const {
      return mPixelFormat == dom::ImageBitmapFormat::YUV444P ||
             mPixelFormat == dom::ImageBitmapFormat::YUV422P ||
             mPixelFormat == dom::ImageBitmapFormat::YUV420P ||
             mPixelFormat == dom::ImageBitmapFormat::YUV420SP_NV12 ||
             mPixelFormat == dom::ImageBitmapFormat::YUV420SP_NV21;
    }

    static Result<SampleFormat, MediaResult> FromImage(layers::Image* aImage);
  };

  EncoderConfig(const EncoderConfig& aConfig) = default;

  // This constructor is used for video encoders
  EncoderConfig(const CodecType aCodecType, gfx::IntSize aSize,
                const Usage aUsage, const SampleFormat& aFormat,
                const uint32_t aFramerate, const size_t aKeyframeInterval,
                const uint32_t aBitrate, const uint32_t aMinBitrate,
                const uint32_t aMaxBitrate, const BitrateMode aBitrateMode,
                const HardwarePreference aHardwarePreference,
                const ScalabilityMode aScalabilityMode,
                const Maybe<CodecSpecific>& aCodecSpecific)
      : mCodec(aCodecType),
        mSize(aSize),
        mBitrateMode(aBitrateMode),
        mBitrate(aBitrate),
        mMinBitrate(aMinBitrate),
        mMaxBitrate(aMaxBitrate),
        mUsage(aUsage),
        mHardwarePreference(aHardwarePreference),
        mFormat(aFormat),
        mScalabilityMode(aScalabilityMode),
        mFramerate(aFramerate),
        mKeyframeInterval(aKeyframeInterval),
        mCodecSpecific(aCodecSpecific) {
    MOZ_ASSERT(IsVideo());
  }

  // This constructor is used for audio encoders
  EncoderConfig(const CodecType aCodecType, uint32_t aNumberOfChannels,
                const BitrateMode aBitrateMode, uint32_t aSampleRate,
                uint32_t aBitrate, const Maybe<CodecSpecific>& aCodecSpecific)
      : mCodec(aCodecType),
        mBitrateMode(aBitrateMode),
        mBitrate(aBitrate),
        mNumberOfChannels(aNumberOfChannels),
        mSampleRate(aSampleRate),
        mCodecSpecific(aCodecSpecific) {
    MOZ_ASSERT(IsAudio());
  }

  static CodecType CodecTypeForMime(const nsACString& aMimeType);

  nsCString ToString() const;

  bool IsVideo() const {
    return mCodec > CodecType::_BeginVideo_ && mCodec < CodecType::_EndVideo_;
  }

  bool IsAudio() const {
    return mCodec > CodecType::_BeginAudio_ && mCodec < CodecType::_EndAudio_;
  }

  CodecType mCodec{};
  gfx::IntSize mSize{};
  BitrateMode mBitrateMode{};
  uint32_t mBitrate{};
  uint32_t mMinBitrate{};
  uint32_t mMaxBitrate{};
  Usage mUsage{};
  // Video-only
  HardwarePreference mHardwarePreference{};
  SampleFormat mFormat{};
  ScalabilityMode mScalabilityMode{};
  uint32_t mFramerate{};
  size_t mKeyframeInterval{};
  // Audio-only
  uint32_t mNumberOfChannels{};
  uint32_t mSampleRate{};
  Maybe<CodecSpecific> mCodecSpecific{};
};

}  // namespace mozilla

#endif  // mozilla_EncoderConfig_h_
