/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_media_MediaIPCUtils_h
#define mozilla_dom_media_MediaIPCUtils_h

#include "DecoderDoctorDiagnostics.h"
#include "EncoderConfig.h"
#include "MediaTrackConstraints.h"
#include "PerformanceRecorder.h"
#include "PlatformDecoderModule.h"
#include "PlatformEncoderModule.h"
#include "ipc/EnumSerializer.h"
#include "mozilla/CDMProxy.h"
#include "mozilla/GfxMessageUtils.h"
#include "mozilla/Maybe.h"
#include "mozilla/dom/MFCDMSerializers.h"
#include "mozilla/dom/MediaKeysBinding.h"
#include "mozilla/gfx/Rect.h"

namespace IPC {
template <>
struct ParamTraits<mozilla::VideoInfo> {
  typedef mozilla::VideoInfo paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    // TrackInfo
    WriteParam(aWriter, aParam.mMimeType);

    // VideoInfo
    WriteParam(aWriter, aParam.mDisplay);
    WriteParam(aWriter, aParam.mStereoMode);
    WriteParam(aWriter, aParam.mImage);
    WriteParam(aWriter, aParam.mImageRect);
    WriteParam(aWriter, *aParam.mCodecSpecificConfig);
    WriteParam(aWriter, *aParam.mExtraData);
    WriteParam(aWriter, aParam.mRotation);
    WriteParam(aWriter, aParam.mColorDepth);
    WriteParam(aWriter, aParam.mColorSpace);
    WriteParam(aWriter, aParam.mColorPrimaries);
    WriteParam(aWriter, aParam.mTransferFunction);
    WriteParam(aWriter, aParam.mHDRMetadata);
    WriteParam(aWriter, aParam.mColorRange);
    WriteParam(aWriter, aParam.mAlphaPresent);
    WriteParam(aWriter, aParam.mCrypto);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mMimeType) &&
           ReadParam(aReader, &aResult->mDisplay) &&
           ReadParam(aReader, &aResult->mStereoMode) &&
           ReadParam(aReader, &aResult->mImage) &&
           ReadParam(aReader, &aResult->mImageRect) &&
           ReadParam(aReader, aResult->mCodecSpecificConfig.get()) &&
           ReadParam(aReader, aResult->mExtraData.get()) &&
           ReadParam(aReader, &aResult->mRotation) &&
           ReadParam(aReader, &aResult->mColorDepth) &&
           ReadParam(aReader, &aResult->mColorSpace) &&
           ReadParam(aReader, &aResult->mColorPrimaries) &&
           ReadParam(aReader, &aResult->mTransferFunction) &&
           ReadParam(aReader, &aResult->mHDRMetadata) &&
           ReadParam(aReader, &aResult->mColorRange) &&
           ReadParam(aReader, &aResult->mAlphaPresent) &&
           ReadParam(aReader, &aResult->mCrypto);
  }
};

template <>
struct ParamTraits<mozilla::TrackInfo::TrackType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::TrackInfo::TrackType,
          mozilla::TrackInfo::TrackType::kUndefinedTrack,
          mozilla::TrackInfo::TrackType::kTextTrack> {};

struct VideoRotationValidator {
  using IntegralType = std::underlying_type_t<mozilla::VideoRotation>;

  static bool IsLegalValue(const IntegralType e) {
    return e == IntegralType(mozilla::VideoRotation::kDegree_0) ||
           e == IntegralType(mozilla::VideoRotation::kDegree_90) ||
           e == IntegralType(mozilla::VideoRotation::kDegree_180) ||
           e == IntegralType(mozilla::VideoRotation::kDegree_270);
  }
};

template <>
struct ParamTraits<mozilla::VideoRotation>
    : EnumSerializer<mozilla::VideoRotation, VideoRotationValidator> {};

template <>
struct ParamTraits<mozilla::MediaByteBuffer>
    : public ParamTraits<nsTArray<uint8_t>> {
  typedef mozilla::MediaByteBuffer paramType;
};

// Traits for AudioCodecSpecificVariant types.

template <>
struct ParamTraits<mozilla::NoCodecSpecificData>
    : public EmptyStructSerializer<mozilla::NoCodecSpecificData> {};

template <>
struct ParamTraits<mozilla::AudioCodecSpecificBinaryBlob> {
  using paramType = mozilla::AudioCodecSpecificBinaryBlob;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, *aParam.mBinaryBlob);
  }
  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, aResult->mBinaryBlob.get());
  }
};

template <>
struct ParamTraits<mozilla::AacCodecSpecificData> {
  using paramType = mozilla::AacCodecSpecificData;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, *aParam.mEsDescriptorBinaryBlob);
    WriteParam(aWriter, *aParam.mDecoderConfigDescriptorBinaryBlob);
    WriteParam(aWriter, aParam.mEncoderDelayFrames);
    WriteParam(aWriter, aParam.mMediaFrameCount);
  }
  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, aResult->mEsDescriptorBinaryBlob.get()) &&
           ReadParam(aReader,
                     aResult->mDecoderConfigDescriptorBinaryBlob.get()) &&
           ReadParam(aReader, &aResult->mEncoderDelayFrames) &&
           ReadParam(aReader, &aResult->mMediaFrameCount);
  }
};

template <>
struct ParamTraits<mozilla::FlacCodecSpecificData> {
  using paramType = mozilla::FlacCodecSpecificData;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, *aParam.mStreamInfoBinaryBlob);
  }
  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, aResult->mStreamInfoBinaryBlob.get());
  }
};

template <>
struct ParamTraits<mozilla::Mp3CodecSpecificData>
    : public ParamTraits_TiedFields<mozilla::Mp3CodecSpecificData> {};

template <>
struct ParamTraits<mozilla::OpusCodecSpecificData> {
  using paramType = mozilla::OpusCodecSpecificData;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mContainerCodecDelayFrames);
    WriteParam(aWriter, aParam.mOutputChannels);
    WriteParam(aWriter, *aParam.mHeadersBinaryBlob);
  }
  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mContainerCodecDelayFrames) &&
           ReadParam(aReader, &aResult->mOutputChannels) &&
           ReadParam(aReader, aResult->mHeadersBinaryBlob.get());
  }
};

template <>
struct ParamTraits<mozilla::VorbisCodecSpecificData> {
  using paramType = mozilla::VorbisCodecSpecificData;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, *aParam.mHeadersBinaryBlob);
  }
  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, aResult->mHeadersBinaryBlob.get());
  }
};

template <>
struct ParamTraits<mozilla::WaveCodecSpecificData>
    : public EmptyStructSerializer<mozilla::WaveCodecSpecificData> {};

// End traits for AudioCodecSpecificVariant types.

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::AudioInfo, mMimeType, mRate,
                                  mChannels, mChannelMap, mBitDepth, mProfile,
                                  mExtendedProfile, mCodecSpecificConfig,
                                  mCrypto);

template <>
struct ParamTraits<mozilla::MediaDataDecoder::ConversionRequired>
    : public ContiguousEnumSerializerInclusive<
          mozilla::MediaDataDecoder::ConversionRequired,
          mozilla::MediaDataDecoder::ConversionRequired(0),
          mozilla::MediaDataDecoder::ConversionRequired(
              mozilla::MediaDataDecoder::ConversionRequired::kNeedHVCC)> {};

template <>
struct MOZ_ENUM_SERIALIZER_ALLOW_SENTINEL_UPPER_BOUND
    ParamTraits<mozilla::MediaDataDecoder::PropertyName>
    : public ContiguousEnumSerializerInclusive<
          mozilla::MediaDataDecoder::PropertyName,
          mozilla::MediaDataDecoder::PropertyName(0),
          mozilla::MediaDataDecoder::sHighestPropertyName> {};

template <>
struct ParamTraits<mozilla::media::TimeUnit> {
  using paramType = mozilla::media::TimeUnit;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.IsValid());
    WriteParam(aWriter, aParam.IsValid() ? aParam.mTicks.value() : 0);
    WriteParam(aWriter,
               aParam.IsValid() ? aParam.mBase : 1);  // base can't be 0
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    bool valid;
    int64_t ticks;
    int64_t base;
    if (ReadParam(aReader, &valid) && ReadParam(aReader, &ticks) &&
        ReadParam(aReader, &base)) {
      if (valid) {
        if (base <= 0 || base > int64_t(UINT32_MAX)) {
          return false;
        }

        *aResult = mozilla::media::TimeUnit(ticks, base);
      } else {
        *aResult = mozilla::media::TimeUnit::Invalid();
      }
      return true;
    }
    return false;
  };
};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::media::TimeInterval, mStart, mEnd,
                                  mFuzz);

template <>
struct ParamTraits<mozilla::MediaResult> {
  typedef mozilla::MediaResult paramType;

  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.Code());
    WriteParam(aWriter, aParam.Message());
    WriteParam(aWriter, aParam.GetPlatformErrorCode());
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    nsresult result;
    nsCString message;
    mozilla::Maybe<int32_t> platformErrorCode;
    if (ReadParam(aReader, &result) && ReadParam(aReader, &message) &&
        ReadParam(aReader, &platformErrorCode)) {
      *aResult = paramType(result, std::move(message), platformErrorCode);
      return true;
    }
    return false;
  };
};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::DecoderDoctorDiagnostics,
                                  mDiagnosticsType, mFormat, mFlags, mEvent);

template <>
struct ParamTraits<mozilla::DecoderDoctorDiagnostics::DiagnosticsType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::DecoderDoctorDiagnostics::DiagnosticsType,
          mozilla::DecoderDoctorDiagnostics::DiagnosticsType::eUnsaved,
          mozilla::DecoderDoctorDiagnostics::DiagnosticsType::eDecodeWarning> {
};

template <>
struct ParamTraits<mozilla::DecoderDoctorEvent::Domain>
    : public ContiguousEnumSerializerInclusive<
          mozilla::DecoderDoctorEvent::Domain,
          mozilla::DecoderDoctorEvent::Domain::eAudioSinkStartup,
          mozilla::DecoderDoctorEvent::Domain::eAudioSinkStartup> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::DecoderDoctorEvent, mDomain,
                                  mResult);

template <>
struct ParamTraits<mozilla::TrackingId::Source>
    : public ContiguousEnumSerializer<
          mozilla::TrackingId::Source,
          mozilla::TrackingId::Source::Unimplemented,
          mozilla::TrackingId::Source::LAST> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::TrackingId, mSource, mProcId,
                                  mUniqueInProcId);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::CryptoTrack, mCryptoScheme);

template <>
struct ParamTraits<mozilla::dom::ImageBitmapFormat>
    : public ContiguousEnumSerializerInclusive<
          mozilla::dom::ImageBitmapFormat,
          mozilla::dom::ImageBitmapFormat::RGBA32,
          mozilla::dom::ImageBitmapFormat::DEPTH> {};

template <>
struct ParamTraits<mozilla::CodecType>
    : public ContiguousEnumSerializerInclusive<mozilla::CodecType,
                                               mozilla::CodecType::_BeginVideo_,
                                               mozilla::CodecType::Unknown> {};

template <>
struct ParamTraits<mozilla::BitrateMode>
    : public ContiguousEnumSerializerInclusive<mozilla::BitrateMode,
                                               mozilla::BitrateMode::Constant,
                                               mozilla::BitrateMode::Variable> {
};

template <>
struct ParamTraits<mozilla::ScalabilityMode>
    : public ContiguousEnumSerializerInclusive<mozilla::ScalabilityMode,
                                               mozilla::ScalabilityMode::None,
                                               mozilla::ScalabilityMode::L1T3> {
};

template <>
struct ParamTraits<mozilla::H264BitStreamFormat>
    : public ContiguousEnumSerializerInclusive<
          mozilla::H264BitStreamFormat, mozilla::H264BitStreamFormat::AVC,
          mozilla::H264BitStreamFormat::ANNEXB> {};

template <>
struct ParamTraits<mozilla::HardwarePreference>
    : public ContiguousEnumSerializerInclusive<
          mozilla::HardwarePreference, mozilla::HardwarePreference::None,
          mozilla::HardwarePreference::RequireSoftware> {};

template <>
struct ParamTraits<mozilla::Usage>
    : public ContiguousEnumSerializerInclusive<
          mozilla::Usage, mozilla::Usage::Realtime, mozilla::Usage::Record> {};

struct H264ProfileValidator {
  using IntegralType = std::underlying_type_t<mozilla::H264_PROFILE>;

  static bool IsLegalValue(const IntegralType e) {
    return e == IntegralType(mozilla::H264_PROFILE::H264_PROFILE_UNKNOWN) ||
           e == IntegralType(mozilla::H264_PROFILE::H264_PROFILE_BASE) ||
           e == IntegralType(mozilla::H264_PROFILE::H264_PROFILE_MAIN) ||
           e == IntegralType(mozilla::H264_PROFILE::H264_PROFILE_EXTENDED) ||
           e == IntegralType(mozilla::H264_PROFILE::H264_PROFILE_HIGH);
  }
};

template <>
struct ParamTraits<mozilla::H264_PROFILE>
    : EnumSerializer<mozilla::H264_PROFILE, H264ProfileValidator> {};

struct H264LevelValidator {
  using IntegralType = std::underlying_type_t<mozilla::H264_LEVEL>;

  static bool IsLegalValue(const IntegralType e) {
    static_assert(mozilla::H264_LEVEL::H264_LEVEL_1_b ==
                  mozilla::H264_LEVEL::H264_LEVEL_1_1);
    return e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_1) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_1_1) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_1_2) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_1_3) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_2) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_2_1) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_2_2) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_3) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_3_1) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_3_2) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_4) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_4_1) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_4_2) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_5) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_5_1) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_5_2) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_6) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_6_1) ||
           e == IntegralType(mozilla::H264_LEVEL::H264_LEVEL_6_2);
  }
};

template <>
struct ParamTraits<mozilla::H264_LEVEL>
    : EnumSerializer<mozilla::H264_LEVEL, H264LevelValidator> {};

template <>
struct ParamTraits<mozilla::OpusBitstreamFormat>
    : public ContiguousEnumSerializerInclusive<
          mozilla::OpusBitstreamFormat, mozilla::OpusBitstreamFormat::Opus,
          mozilla::OpusBitstreamFormat::OGG> {};

template <>
struct ParamTraits<mozilla::OpusSpecific::Application>
    : public ContiguousEnumSerializerInclusive<
          mozilla::OpusSpecific::Application,
          mozilla::OpusSpecific::Application::Unspecified,
          mozilla::OpusSpecific::Application::RestricedLowDelay> {};

template <>
struct MOZ_ENUM_SERIALIZER_ALLOW_SENTINEL_UPPER_BOUND
    ParamTraits<mozilla::VPXComplexity>
    : public ContiguousEnumSerializerInclusive<mozilla::VPXComplexity,
                                               mozilla::VPXComplexity::Normal,
                                               mozilla::VPXComplexity::Max> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(struct mozilla::H264Specific, mProfile,
                                  mLevel, mFormat);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(struct mozilla::OpusSpecific, mApplication,
                                  mFrameDuration, mComplexity, mFormat,
                                  mPacketLossPerc, mUseInBandFEC, mUseDTX);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(struct mozilla::VP8Specific, mComplexity,
                                  mResilience, mNumTemporalLayers, mDenoising,
                                  mAutoResize, mFrameDropping);

DEFINE_IPC_SERIALIZER_WITH_SUPER_CLASS_AND_FIELDS(struct mozilla::VP9Specific,
                                                  mozilla::VP8Specific,
                                                  mAdaptiveQp,
                                                  mNumSpatialLayers, mFlexible);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(
    struct mozilla::EncoderConfig::VideoColorSpace, mRange, mMatrix, mPrimaries,
    mTransferFunction);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(struct mozilla::EncoderConfig::SampleFormat,
                                  mPixelFormat, mColorSpace);

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::EncoderConfig, mCodec, mSize,
                                  mBitrateMode, mBitrate, mMinBitrate,
                                  mMaxBitrate, mUsage, mHardwarePreference,
                                  mFormat, mScalabilityMode, mFramerate,
                                  mKeyframeInterval, mNumberOfChannels,
                                  mSampleRate, mCodecSpecific);

template <typename T, typename Phantom>
struct ParamTraits<mozilla::StrongTypedef<T, Phantom>>
    : ParamTraits_TiedFields<mozilla::StrongTypedef<T, Phantom>> {};

// [RefCounted] typed
template <>
struct ParamTraits<mozilla::EncoderConfigurationChangeList*> {
  typedef mozilla::EncoderConfigurationChangeList paramType;

  static void Write(MessageWriter* aWriter, const paramType* aParam) {
    WriteParam(aWriter, aParam->mChanges);
  }

  static bool Read(MessageReader* aReader, RefPtr<paramType>* aResult) {
    auto result = mozilla::MakeRefPtr<paramType>();
    if (!ReadParam(aReader, &result->mChanges)) {
      return false;
    }
    *aResult = std::move(result);
    return true;
  }
};

template <>
struct ParamTraits<mozilla::dom::MediaKeySessionType>
    : public ContiguousEnumSerializerInclusive<
          mozilla::dom::MediaKeySessionType,
          mozilla::dom::MediaKeySessionType::Temporary,
          mozilla::dom::MediaKeySessionType::Persistent_license> {};

DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::CDMKeyInfo, mKeyId, mStatus);

template <typename T>
struct ParamTraits<mozilla::NormalizedConstraintSet::ConstraintRange<T>> {
  using paramType = mozilla::NormalizedConstraintSet::ConstraintRange<T>;
  static void Write(MessageWriter* aWriter, const paramType& aParam) {
    WriteParams(aWriter, aParam.mMin, aParam.mMax, aParam.mIdeal);
  }

  static bool Read(MessageReader* aReader, paramType* aResult) {
    return ReadParams(aReader, aResult->mMin, aResult->mMax, aResult->mIdeal);
  }
};

DEFINE_IPC_SERIALIZER_WITH_SUPER_CLASS(
    mozilla::NormalizedConstraintSet::LongRange,
    mozilla::NormalizedConstraintSet::ConstraintRange<int32_t>);
DEFINE_IPC_SERIALIZER_WITH_SUPER_CLASS(
    mozilla::NormalizedConstraintSet::LongLongRange,
    mozilla::NormalizedConstraintSet::ConstraintRange<int64_t>);
DEFINE_IPC_SERIALIZER_WITH_SUPER_CLASS(
    mozilla::NormalizedConstraintSet::DoubleRange,
    mozilla::NormalizedConstraintSet::ConstraintRange<double>);
DEFINE_IPC_SERIALIZER_WITH_SUPER_CLASS(
    mozilla::NormalizedConstraintSet::BooleanRange,
    mozilla::NormalizedConstraintSet::ConstraintRange<bool>);
DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::NormalizedConstraintSet::StringRange,
                                  mExact, mIdeal);
DEFINE_IPC_SERIALIZER_WITH_FIELDS(mozilla::NormalizedConstraintSet, mWidth,
                                  mHeight, mFrameRate, mFacingMode, mResizeMode,
                                  mMediaSource, mBrowserWindow, mDeviceId,
                                  mGroupId, mViewportOffsetX, mViewportOffsetY,
                                  mViewportWidth, mViewportHeight,
                                  mEchoCancellation, mNoiseSuppression,
                                  mAutoGainControl, mChannelCount);
DEFINE_IPC_SERIALIZER_WITH_SUPER_CLASS_AND_FIELDS(
    mozilla::NormalizedConstraints, mozilla::NormalizedConstraintSet,
    mAdvanced);

template <>
struct ParamTraits<mozilla::dom::VideoResizeModeEnum>
    : public mozilla::dom::WebIDLEnumSerializer<
          mozilla::dom::VideoResizeModeEnum> {};

}  // namespace IPC

#endif  // mozilla_dom_media_MediaIPCUtils_h
