/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/VideoEncoder.h"
#include "mozilla/dom/VideoEncoderBinding.h"
#include "mozilla/dom/VideoColorSpaceBinding.h"
#include "mozilla/dom/VideoColorSpace.h"
#include "mozilla/dom/VideoFrame.h"

#include "EncoderTraits.h"
#include "ImageContainer.h"
#include "VideoUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/Unused.h"
#include "mozilla/dom/EncodedVideoChunk.h"
#include "mozilla/dom/EncodedVideoChunkBinding.h"
#include "mozilla/dom/ImageUtils.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/VideoColorSpaceBinding.h"
#include "mozilla/dom/VideoFrameBinding.h"
#include "mozilla/dom/WebCodecsUtils.h"
#include "nsPrintfCString.h"
#include "nsReadableUtils.h"

extern mozilla::LazyLogModule gWebCodecsLog;

namespace mozilla::dom {

#ifdef LOG_INTERNAL
#  undef LOG_INTERNAL
#endif  // LOG_INTERNAL
#define LOG_INTERNAL(level, msg, ...) \
  MOZ_LOG(gWebCodecsLog, LogLevel::level, (msg, ##__VA_ARGS__))

#ifdef LOG
#  undef LOG
#endif  // LOG
#define LOG(msg, ...) LOG_INTERNAL(Debug, msg, ##__VA_ARGS__)

#ifdef LOGW
#  undef LOGW
#endif  // LOGW
#define LOGW(msg, ...) LOG_INTERNAL(Warning, msg, ##__VA_ARGS__)

#ifdef LOGE
#  undef LOGE
#endif  // LOGE
#define LOGE(msg, ...) LOG_INTERNAL(Error, msg, ##__VA_ARGS__)

#ifdef LOGV
#  undef LOGV
#endif  // LOGV
#define LOGV(msg, ...) LOG_INTERNAL(Verbose, msg, ##__VA_ARGS__)

NS_IMPL_CYCLE_COLLECTION_INHERITED(VideoEncoder, DOMEventTargetHelper,
                                   mErrorCallback, mOutputCallback)
NS_IMPL_ADDREF_INHERITED(VideoEncoder, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(VideoEncoder, DOMEventTargetHelper)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(VideoEncoder)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

VideoEncoderConfigInternal::VideoEncoderConfigInternal(
    const nsAString& aCodec, uint32_t aWidth, uint32_t aHeight,
    Maybe<uint32_t>&& aDisplayWidth, Maybe<uint32_t>&& aDisplayHeight,
    Maybe<uint32_t>&& aBitrate, Maybe<double>&& aFramerate,
    const HardwareAcceleration& aHardwareAcceleration,
    const AlphaOption& aAlpha, Maybe<nsString>&& aScalabilityMode,
    const VideoEncoderBitrateMode& aBitrateMode,
    const LatencyMode& aLatencyMode, Maybe<nsString>&& aContentHint)
    : mCodec(aCodec),
      mWidth(aWidth),
      mHeight(aHeight),
      mDisplayWidth(std::move(aDisplayWidth)),
      mDisplayHeight(std::move(aDisplayHeight)),
      mBitrate(std::move(aBitrate)),
      mFramerate(std::move(aFramerate)),
      mHardwareAcceleration(aHardwareAcceleration),
      mAlpha(aAlpha),
      mScalabilityMode(std::move(aScalabilityMode)),
      mBitrateMode(aBitrateMode),
      mLatencyMode(aLatencyMode),
      mContentHint(std::move(aContentHint)) {}

VideoEncoderConfigInternal::VideoEncoderConfigInternal(
    const VideoEncoderConfigInternal& aConfig)
    : mCodec(aConfig.mCodec),
      mWidth(aConfig.mWidth),
      mHeight(aConfig.mHeight),
      mDisplayWidth(aConfig.mDisplayWidth),
      mDisplayHeight(aConfig.mDisplayHeight),
      mBitrate(aConfig.mBitrate),
      mFramerate(aConfig.mFramerate),
      mHardwareAcceleration(aConfig.mHardwareAcceleration),
      mAlpha(aConfig.mAlpha),
      mScalabilityMode(aConfig.mScalabilityMode),
      mBitrateMode(aConfig.mBitrateMode),
      mLatencyMode(aConfig.mLatencyMode),
      mContentHint(aConfig.mContentHint),
      mAvc(aConfig.mAvc) {}

VideoEncoderConfigInternal::VideoEncoderConfigInternal(
    const VideoEncoderConfig& aConfig)
    : mCodec(aConfig.mCodec),
      mWidth(aConfig.mWidth),
      mHeight(aConfig.mHeight),
      mDisplayWidth(OptionalToMaybe(aConfig.mDisplayWidth)),
      mDisplayHeight(OptionalToMaybe(aConfig.mDisplayHeight)),
      mBitrate(OptionalToMaybe(aConfig.mBitrate)),
      mFramerate(OptionalToMaybe(aConfig.mFramerate)),
      mHardwareAcceleration(aConfig.mHardwareAcceleration),
      mAlpha(aConfig.mAlpha),
      mScalabilityMode(OptionalToMaybe(aConfig.mScalabilityMode)),
      mBitrateMode(aConfig.mBitrateMode),
      mLatencyMode(aConfig.mLatencyMode),
      mContentHint(OptionalToMaybe(aConfig.mContentHint)),
      mAvc(OptionalToMaybe(aConfig.mAvc)) {}

nsCString VideoEncoderConfigInternal::ToString() const {
  nsCString rv;

  rv.AppendLiteral("Codec: ");
  rv.Append(NS_ConvertUTF16toUTF8(mCodec));
  rv.AppendPrintf(" [%" PRIu32 "x%" PRIu32 "],", mWidth, mHeight);
  if (mDisplayWidth.isSome()) {
    rv.AppendPrintf(", display[%" PRIu32 "x%" PRIu32 "]", mDisplayWidth.value(),
                    mDisplayHeight.value());
  }
  if (mBitrate.isSome()) {
    rv.AppendPrintf(", %" PRIu32 "kbps", mBitrate.value());
  }
  if (mFramerate.isSome()) {
    rv.AppendPrintf(", %lfHz", mFramerate.value());
  }
  rv.AppendPrintf(" hw: %s", GetEnumString(mHardwareAcceleration).get());
  rv.AppendPrintf(", alpha: %s", GetEnumString(mAlpha).get());
  if (mScalabilityMode.isSome()) {
    rv.AppendPrintf(", scalability mode: %s",
                    NS_ConvertUTF16toUTF8(mScalabilityMode.value()).get());
  }
  rv.AppendPrintf(", bitrate mode: %s", GetEnumString(mBitrateMode).get());
  rv.AppendPrintf(", latency mode: %s", GetEnumString(mLatencyMode).get());
  if (mContentHint.isSome()) {
    rv.AppendPrintf(", content hint: %s",
                    NS_ConvertUTF16toUTF8(mContentHint.value()).get());
  }
  if (mAvc.isSome()) {
    rv.AppendPrintf(", avc-specific: %s", GetEnumString(mAvc->mFormat).get());
  }

  return rv;
}

template <typename T>
bool MaybeAreEqual(const Maybe<T>& aLHS, const Maybe<T> aRHS) {
  if (aLHS.isSome() && aRHS.isSome()) {
    return aLHS.value() == aRHS.value();
  }
  if (aLHS.isNothing() && aRHS.isNothing()) {
    return true;
  }
  return false;
}

bool VideoEncoderConfigInternal::Equals(
    const VideoEncoderConfigInternal& aOther) const {
  bool sameCodecSpecific = true;
  if ((mAvc.isSome() && aOther.mAvc.isSome() &&
       mAvc->mFormat != aOther.mAvc->mFormat) ||
      mAvc.isSome() != aOther.mAvc.isSome()) {
    sameCodecSpecific = false;
  }
  return mCodec.Equals(aOther.mCodec) && mWidth == aOther.mWidth &&
         mHeight == aOther.mHeight &&
         MaybeAreEqual(mDisplayWidth, aOther.mDisplayWidth) &&
         MaybeAreEqual(mDisplayHeight, aOther.mDisplayHeight) &&
         MaybeAreEqual(mBitrate, aOther.mBitrate) &&
         MaybeAreEqual(mFramerate, aOther.mFramerate) &&
         mHardwareAcceleration == aOther.mHardwareAcceleration &&
         mAlpha == aOther.mAlpha &&
         MaybeAreEqual(mScalabilityMode, aOther.mScalabilityMode) &&
         mBitrateMode == aOther.mBitrateMode &&
         mLatencyMode == aOther.mLatencyMode &&
         MaybeAreEqual(mContentHint, aOther.mContentHint) && sameCodecSpecific;
}

bool VideoEncoderConfigInternal::CanReconfigure(
    const VideoEncoderConfigInternal& aOther) const {
  return mCodec.Equals(aOther.mCodec) &&
         mHardwareAcceleration == aOther.mHardwareAcceleration;
}

EncoderConfig VideoEncoderConfigInternal::ToEncoderConfig() const {
  Usage usage;
  if (mLatencyMode == LatencyMode::Quality) {
    usage = Usage::Record;
  } else {
    usage = Usage::Realtime;
  }
  HardwarePreference hwPref = HardwarePreference::None;
  if (mHardwareAcceleration ==
      mozilla::dom::HardwareAcceleration::Prefer_hardware) {
    hwPref = HardwarePreference::RequireHardware;
  } else if (mHardwareAcceleration ==
             mozilla::dom::HardwareAcceleration::Prefer_software) {
    hwPref = HardwarePreference::RequireSoftware;
  }
  CodecType codecType;
  auto maybeCodecType = CodecStringToCodecType(mCodec);
  if (maybeCodecType.isSome()) {
    codecType = maybeCodecType.value();
  } else {
    MOZ_CRASH("The string should always contain a valid codec at this point.");
  }
  Maybe<EncoderConfig::CodecSpecific> specific;
  if (codecType == CodecType::H264) {
    uint8_t profile, constraints;
    H264_LEVEL level;
    H264BitStreamFormat format;
    if (mAvc) {
      format = mAvc->mFormat == AvcBitstreamFormat::Annexb
                   ? H264BitStreamFormat::ANNEXB
                   : H264BitStreamFormat::AVC;
    } else {
      format = H264BitStreamFormat::AVC;
    }
    if (ExtractH264CodecDetails(mCodec, profile, constraints, level,
                                H264CodecStringStrictness::Strict)) {
      if (profile == H264_PROFILE_BASE || profile == H264_PROFILE_MAIN ||
          profile == H264_PROFILE_EXTENDED || profile == H264_PROFILE_HIGH) {
        specific.emplace(
            H264Specific(static_cast<H264_PROFILE>(profile), level, format));
      }
    }
  }
  uint8_t numTemporalLayers = 1;
  ScalabilityMode scalabilityMode;
  if (mScalabilityMode) {
    if (mScalabilityMode->EqualsLiteral("L1T2")) {
      scalabilityMode = ScalabilityMode::L1T2;
      numTemporalLayers = 2;
    } else if (mScalabilityMode->EqualsLiteral("L1T3")) {
      scalabilityMode = ScalabilityMode::L1T3;
      numTemporalLayers = 3;
    } else {
      scalabilityMode = ScalabilityMode::None;
    }
  } else {
    scalabilityMode = ScalabilityMode::None;
  }
  // Only for vp9, not vp8
  if (codecType == CodecType::VP9) {
    uint8_t profile, level, bitdepth, chromasubsampling;
    mozilla::VideoColorSpace colorspace;
    DebugOnly<bool> rv = ExtractVPXCodecDetails(
        mCodec, profile, level, bitdepth, chromasubsampling, colorspace);
#ifdef DEBUG
    if (!rv) {
      LOGE("Error extracting VPX codec details, non fatal");
    }
#endif
    specific.emplace(VP9Specific(
        VPXComplexity::Normal, /* Complexity */
        true,                  /* Resilience */
        numTemporalLayers,     /* Number of temporal layers */
        true,                  /* Denoising */
        false,                 /* Auto resize */
        false,                 /* Frame dropping */
        true,                  /* Adaptive Qp */
        1,                     /* Number of spatial layers */
        false                  /* Flexible */
        ));
  }
  // For real-time usage, typically used in web conferencing, YUV420 is the most
  // common format and is set as the default. Otherwise, Gecko's preferred
  // format, BGRA, is assumed.
  EncoderConfig::SampleFormat format;
  if (usage == Usage::Realtime) {
    format.mPixelFormat = ImageBitmapFormat::YUV420P;
    format.mColorSpace.mRange.emplace(gfx::ColorRange::LIMITED);
  } else {
    format.mPixelFormat = ImageBitmapFormat::BGRA32;
  }
  return EncoderConfig(codecType, {mWidth, mHeight}, usage, format,
                       SaturatingCast<uint32_t>(mFramerate.refOr(0.f)), 0,
                       mBitrate.refOr(0), 0, 0,
                       (mBitrateMode == VideoEncoderBitrateMode::Constant)
                           ? mozilla::BitrateMode::Constant
                           : mozilla::BitrateMode::Variable,
                       hwPref, scalabilityMode, specific);
}
already_AddRefed<WebCodecsConfigurationChangeList>
VideoEncoderConfigInternal::Diff(
    const VideoEncoderConfigInternal& aOther) const {
  auto list = MakeRefPtr<WebCodecsConfigurationChangeList>();
  if (!mCodec.Equals(aOther.mCodec)) {
    list->Push(CodecChange{aOther.mCodec});
  }
  // Both must always be present, when a `VideoEncoderConfig` is passed to
  // `configure`.
  if (mWidth != aOther.mWidth || mHeight != aOther.mHeight) {
    list->Push(DimensionsChange{gfx::IntSize{aOther.mWidth, aOther.mHeight}});
  }
  // Similarly, both must always be present, when a `VideoEncoderConfig` is
  // passed to `configure`.
  if (!MaybeAreEqual(mDisplayWidth, aOther.mDisplayWidth) ||
      !MaybeAreEqual(mDisplayHeight, aOther.mDisplayHeight)) {
    Maybe<gfx::IntSize> displaySize =
        aOther.mDisplayWidth.isSome()
            ? Some(gfx::IntSize{aOther.mDisplayWidth.value(),
                                aOther.mDisplayHeight.value()})
            : Nothing();
    list->Push(DisplayDimensionsChange{displaySize});
  }
  if (!MaybeAreEqual(mBitrate, aOther.mBitrate)) {
    list->Push(BitrateChange{aOther.mBitrate});
  }
  if (!MaybeAreEqual(mFramerate, aOther.mFramerate)) {
    list->Push(FramerateChange{aOther.mFramerate});
  }
  if (mHardwareAcceleration != aOther.mHardwareAcceleration) {
    list->Push(HardwareAccelerationChange{aOther.mHardwareAcceleration});
  }
  if (mAlpha != aOther.mAlpha) {
    list->Push(AlphaChange{aOther.mAlpha});
  }
  if (!MaybeAreEqual(mScalabilityMode, aOther.mScalabilityMode)) {
    list->Push(ScalabilityModeChange{aOther.mScalabilityMode});
  }
  if (mBitrateMode != aOther.mBitrateMode) {
    list->Push(BitrateModeChange{aOther.mBitrateMode});
  }
  if (mLatencyMode != aOther.mLatencyMode) {
    list->Push(LatencyModeChange{aOther.mLatencyMode});
  }
  if (!MaybeAreEqual(mContentHint, aOther.mContentHint)) {
    list->Push(ContentHintChange{aOther.mContentHint});
  }
  return list.forget();
}

// https://w3c.github.io/webcodecs/#check-configuration-support
static bool CanEncode(const RefPtr<VideoEncoderConfigInternal>& aConfig) {
  // TODO: Enable WebCodecs on Android (Bug 1840508)
  if (IsOnAndroid()) {
    return false;
  }
  if (!IsSupportedVideoCodec(aConfig->mCodec)) {
    return false;
  }
  if (aConfig->mScalabilityMode.isSome()) {
    // Check if ScalabilityMode string is valid.
    if (!aConfig->mScalabilityMode->EqualsLiteral("L1T2") &&
        !aConfig->mScalabilityMode->EqualsLiteral("L1T3")) {
      LOGE("Scalability mode %s not supported for codec: %s",
           NS_ConvertUTF16toUTF8(aConfig->mScalabilityMode.value()).get(),
           NS_ConvertUTF16toUTF8(aConfig->mCodec).get());
      return false;
    }
  }
  return EncoderSupport::Supports(aConfig);
}

static Result<Ok, nsresult> CloneConfiguration(
    VideoEncoderConfig& aDest, JSContext* aCx,
    const VideoEncoderConfig& aConfig) {
  nsCString errorMessage;
  MOZ_ASSERT(VideoEncoderTraits::Validate(aConfig, errorMessage));

  aDest.mCodec = aConfig.mCodec;
  aDest.mWidth = aConfig.mWidth;
  aDest.mHeight = aConfig.mHeight;
  aDest.mAlpha = aConfig.mAlpha;
  if (aConfig.mBitrate.WasPassed()) {
    aDest.mBitrate.Construct(aConfig.mBitrate.Value());
  }
  aDest.mBitrateMode = aConfig.mBitrateMode;
  if (aConfig.mContentHint.WasPassed()) {
    aDest.mContentHint.Construct(aConfig.mContentHint.Value());
  }
  if (aConfig.mDisplayWidth.WasPassed()) {
    aDest.mDisplayWidth.Construct(aConfig.mDisplayWidth.Value());
  }
  if (aConfig.mDisplayHeight.WasPassed()) {
    aDest.mDisplayHeight.Construct(aConfig.mDisplayHeight.Value());
  }
  if (aConfig.mFramerate.WasPassed()) {
    aDest.mFramerate.Construct(aConfig.mFramerate.Value());
  }
  aDest.mHardwareAcceleration = aConfig.mHardwareAcceleration;
  aDest.mLatencyMode = aConfig.mLatencyMode;
  if (aConfig.mScalabilityMode.WasPassed()) {
    aDest.mScalabilityMode.Construct(aConfig.mScalabilityMode.Value());
  }

  // AVC specific
  if (aConfig.mAvc.WasPassed()) {
    aDest.mAvc.Construct(aConfig.mAvc.Value());
  }

  return Ok();
}

/* static */
bool VideoEncoderTraits::IsSupported(
    const VideoEncoderConfigInternal& aConfig) {
  return CanEncode(MakeRefPtr<VideoEncoderConfigInternal>(aConfig));
}

// https://w3c.github.io/webcodecs/#valid-videoencoderconfig
/* static */
bool VideoEncoderTraits::Validate(const VideoEncoderConfig& aConfig,
                                  nsCString& aErrorMessage) {
  Maybe<nsString> codec = ParseCodecString(aConfig.mCodec);
  // 1.
  if (!codec || codec->IsEmpty()) {
    aErrorMessage.AssignLiteral(
        "Invalid VideoEncoderConfig: invalid codec string");
    LOGE("%s", aErrorMessage.get());
    return false;
  }

  // 2.
  if (aConfig.mWidth == 0 || aConfig.mHeight == 0) {
    aErrorMessage.AppendPrintf("Invalid VideoEncoderConfig: %s equal to 0",
                               aConfig.mWidth == 0 ? "width" : "height");
    LOGE("%s", aErrorMessage.get());
    return false;
  }

  // 3.
  if (aConfig.mDisplayWidth.WasPassed() && aConfig.mDisplayWidth.Value() == 0) {
    aErrorMessage.AssignLiteral(
        "Invalid VideoEncoderConfig: displayWidth equal to 0");
    LOGE("%s", aErrorMessage.get());
    return false;
  }
  if (aConfig.mDisplayHeight.WasPassed() &&
      aConfig.mDisplayHeight.Value() == 0) {
    aErrorMessage.AssignLiteral(
        "Invalid VideoEncoderConfig: displayHeight equal to 0");
    LOGE("%s", aErrorMessage.get());
    return false;
  }

  // https://github.com/w3c/webcodecs/issues/816
  if ((aConfig.mBitrate.WasPassed() && aConfig.mBitrate.Value() == 0)) {
    aErrorMessage.AssignLiteral(
        "Invalid VideoEncoderConfig: bitrate equal to 0");
    LOGE("%s", aErrorMessage.get());
    return false;
  }

  return true;
}

/* static */
RefPtr<VideoEncoderConfigInternal> VideoEncoderTraits::CreateConfigInternal(
    const VideoEncoderConfig& aConfig) {
  return MakeRefPtr<VideoEncoderConfigInternal>(aConfig);
}

/* static */
RefPtr<mozilla::VideoData> VideoEncoderTraits::CreateInputInternal(
    const dom::VideoFrame& aInput,
    const dom::VideoEncoderEncodeOptions& aOptions) {
  media::TimeUnit duration =
      aInput.GetDuration().IsNull()
          ? media::TimeUnit::Zero()
          : media::TimeUnit::FromMicroseconds(
                AssertedCast<int64_t>(aInput.GetDuration().Value()));
  media::TimeUnit pts = media::TimeUnit::FromMicroseconds(aInput.Timestamp());
  return VideoData::CreateFromImage(
      gfx::IntSize{aInput.DisplayWidth(), aInput.DisplayHeight()},
      0 /* bytestream offset */, pts, duration, aInput.GetImage(),
      aOptions.mKeyFrame, pts);
}

/*
 * Below are VideoEncoder implementation
 */

VideoEncoder::VideoEncoder(
    nsIGlobalObject* aParent, RefPtr<WebCodecsErrorCallback>&& aErrorCallback,
    RefPtr<EncodedVideoChunkOutputCallback>&& aOutputCallback)
    : EncoderTemplate(aParent, std::move(aErrorCallback),
                      std::move(aOutputCallback)) {
  MOZ_ASSERT(mErrorCallback);
  MOZ_ASSERT(mOutputCallback);
  LOG("VideoEncoder %p ctor", this);
}

VideoEncoder::~VideoEncoder() {
  LOG("VideoEncoder %p dtor", this);
  Unused << ResetInternal(NS_ERROR_DOM_ABORT_ERR);
}

JSObject* VideoEncoder::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  AssertIsOnOwningThread();

  return VideoEncoder_Binding::Wrap(aCx, this, aGivenProto);
}

// https://w3c.github.io/webcodecs/#dom-videoencoder-videoencoder
/* static */
already_AddRefed<VideoEncoder> VideoEncoder::Constructor(
    const GlobalObject& aGlobal, const VideoEncoderInit& aInit,
    ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  return MakeAndAddRef<VideoEncoder>(
      global.get(), RefPtr<WebCodecsErrorCallback>(aInit.mError),
      RefPtr<EncodedVideoChunkOutputCallback>(aInit.mOutput));
}

// https://w3c.github.io/webcodecs/#dom-videoencoder-isconfigsupported
/* static */
already_AddRefed<Promise> VideoEncoder::IsConfigSupported(
    const GlobalObject& aGlobal, const VideoEncoderConfig& aConfig,
    ErrorResult& aRv) {
  LOG("VideoEncoder::IsConfigSupported, config: %s",
      NS_ConvertUTF16toUTF8(aConfig.mCodec).get());

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (!global) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<Promise> p = Promise::Create(global.get(), aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return p.forget();
  }

  nsCString errorMessage;
  if (!VideoEncoderTraits::Validate(aConfig, errorMessage)) {
    p->MaybeRejectWithTypeError(nsPrintfCString(
        "IsConfigSupported: config is invalid: %s", errorMessage.get()));
    return p.forget();
  }

  // TODO: Move the following works to another thread to unblock the current
  // thread, as what spec suggests.

  VideoEncoderConfig config;
  auto r = CloneConfiguration(config, aGlobal.Context(), aConfig);
  if (r.isErr()) {
    nsresult e = r.unwrapErr();
    LOGE("Failed to clone VideoEncoderConfig. Error: 0x%08" PRIx32,
         static_cast<uint32_t>(e));
    p->MaybeRejectWithTypeError("Failed to clone VideoEncoderConfig");
    aRv.Throw(e);
    return p.forget();
  }

  bool canEncode = CanEncode(MakeRefPtr<VideoEncoderConfigInternal>(config));
  VideoEncoderSupport s;
  s.mConfig.Construct(std::move(config));
  s.mSupported.Construct(canEncode);

  p->MaybeResolve(s);
  return p.forget();
}

RefPtr<EncodedVideoChunk> VideoEncoder::EncodedDataToOutputType(
    nsIGlobalObject* aGlobalObject, const RefPtr<MediaRawData>& aData) {
  AssertIsOnOwningThread();

  MOZ_RELEASE_ASSERT(aData->mType == MediaData::Type::RAW_DATA);
  // Package into an EncodedVideoChunk
  auto buffer =
      MakeRefPtr<MediaAlignedByteBuffer>(aData->Data(), aData->Size());
  auto encodedVideoChunk = MakeRefPtr<EncodedVideoChunk>(
      aGlobalObject, buffer.forget(),
      aData->mKeyframe ? EncodedVideoChunkType::Key
                       : EncodedVideoChunkType::Delta,
      aData->mTime.ToMicroseconds(),
      aData->mDuration.IsZero() ? Nothing()
                                : Some(aData->mDuration.ToMicroseconds()));
  return encodedVideoChunk;
}

void VideoEncoder::EncoderConfigToDecoderConfig(
    JSContext* aCx, const RefPtr<MediaRawData>& aRawData,
    const VideoEncoderConfigInternal& aSrcConfig,
    VideoDecoderConfig& aDestConfig) const {
  MOZ_ASSERT(aCx);

  aDestConfig.mCodec = aSrcConfig.mCodec;
  aDestConfig.mCodedHeight.Construct(aSrcConfig.mHeight);
  aDestConfig.mCodedWidth.Construct(aSrcConfig.mWidth);

  // Colorspace is mandatory when outputing a decoder config after encode
  RootedDictionary<VideoColorSpaceInit> colorSpace(aCx);
  colorSpace.mFullRange.SetValue(false);
  colorSpace.mMatrix.SetValue(VideoMatrixCoefficients::Bt709);
  colorSpace.mPrimaries.SetValue(VideoColorPrimaries::Bt709);
  colorSpace.mTransfer.SetValue(VideoTransferCharacteristics::Bt709);
  aDestConfig.mColorSpace.Construct(std::move(colorSpace));

  if (aRawData->mExtraData && !aRawData->mExtraData->IsEmpty()) {
    Span<const uint8_t> description(aRawData->mExtraData->Elements(),
                                    aRawData->mExtraData->Length());
    if (!CopyExtradataToDescription(aCx, description,
                                    aDestConfig.mDescription.Construct())) {
      LOGE("Failed to copy extra data");
    }
  }

  if (aSrcConfig.mDisplayHeight) {
    aDestConfig.mDisplayAspectHeight.Construct(
        aSrcConfig.mDisplayHeight.value());
  }
  if (aSrcConfig.mDisplayWidth) {
    aDestConfig.mDisplayAspectWidth.Construct(aSrcConfig.mDisplayWidth.value());
  }
  aDestConfig.mHardwareAcceleration = aSrcConfig.mHardwareAcceleration;
}

#undef LOG
#undef LOGW
#undef LOGE
#undef LOGV
#undef LOG_INTERNAL

}  // namespace mozilla::dom
