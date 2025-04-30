/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AppleVTEncoder.h"

#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFByteOrder.h>
#include <CoreFoundation/CFDictionary.h>
#include <MacTypes.h>

#include "AnnexB.h"
#include "H264.h"
#include "ImageContainer.h"

#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ImageUtils.h"

namespace mozilla {
extern LazyLogModule sPEMLog;
#define LOGE(fmt, ...)                       \
  MOZ_LOG(sPEMLog, mozilla::LogLevel::Error, \
          ("[AppleVTEncoder] %s: " fmt, __func__, ##__VA_ARGS__))
#define LOGW(fmt, ...)                         \
  MOZ_LOG(sPEMLog, mozilla::LogLevel::Warning, \
          ("[AppleVTEncoder] %s: " fmt, __func__, ##__VA_ARGS__))
#define LOGD(fmt, ...)                       \
  MOZ_LOG(sPEMLog, mozilla::LogLevel::Debug, \
          ("[AppleVTEncoder] %s: " fmt, __func__, ##__VA_ARGS__))
#define LOGV(fmt, ...)                         \
  MOZ_LOG(sPEMLog, mozilla::LogLevel::Verbose, \
          ("[AppleVTEncoder] %s: " fmt, __func__, ##__VA_ARGS__))

static CFDictionaryRef BuildEncoderSpec(const bool aHardwareNotAllowed,
                                        const bool aLowLatencyRateControl) {
  if (__builtin_available(macos 11.3, *)) {
    if (aLowLatencyRateControl) {
      // If doing low-latency rate control, the hardware encoder is required.
      const void* keys[] = {
          kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder,
          kVTVideoEncoderSpecification_EnableLowLatencyRateControl};
      const void* values[] = {kCFBooleanTrue, kCFBooleanTrue};

      static_assert(std::size(keys) == std::size(values),
                    "Non matching keys/values array size");
      return CFDictionaryCreate(kCFAllocatorDefault, keys, values,
                                std::size(keys), &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks);
    }
  }
  const void* keys[] = {
      kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder};
  const void* values[] = {aHardwareNotAllowed ? kCFBooleanFalse
                                              : kCFBooleanTrue};

  static_assert(std::size(keys) == std::size(values),
                "Non matching keys/values array size");
  return CFDictionaryCreate(kCFAllocatorDefault, keys, values, std::size(keys),
                            &kCFTypeDictionaryKeyCallBacks,
                            &kCFTypeDictionaryValueCallBacks);
}

static void FrameCallback(void* aEncoder, void* aFrameRefCon, OSStatus aStatus,
                          VTEncodeInfoFlags aInfoFlags,
                          CMSampleBufferRef aSampleBuffer) {
  (static_cast<AppleVTEncoder*>(aEncoder))
      ->OutputFrame(aStatus, aInfoFlags, aSampleBuffer);
}

bool AppleVTEncoder::SetAverageBitrate(uint32_t aBitsPerSec) {
  MOZ_ASSERT(mSession);

  SessionPropertyManager mgr(mSession);
  return mgr.Set(kVTCompressionPropertyKey_AverageBitRate,
                 int64_t(aBitsPerSec)) == noErr;
}

bool AppleVTEncoder::SetConstantBitrate(uint32_t aBitsPerSec) {
  MOZ_ASSERT(mSession);

  if (__builtin_available(macos 13.0, *)) {
    SessionPropertyManager mgr(mSession);
    OSStatus rv = mgr.Set(kVTCompressionPropertyKey_ConstantBitRate,
                          AssertedCast<int32_t>(aBitsPerSec));
    if (rv == kVTPropertyNotSupportedErr) {
      LOGE("Constant bitrate not supported.");
    }
    return rv == noErr;
  }
  return false;
}

bool AppleVTEncoder::SetBitrateAndMode(BitrateMode aBitrateMode,
                                       uint32_t aBitsPerSec) {
  if (aBitrateMode == BitrateMode::Variable) {
    return SetAverageBitrate(aBitsPerSec);
  }
  return SetConstantBitrate(aBitsPerSec);
}

bool AppleVTEncoder::SetFrameRate(int64_t aFPS) {
  MOZ_ASSERT(mSession);

  SessionPropertyManager mgr(mSession);
  return mgr.Set(kVTCompressionPropertyKey_ExpectedFrameRate, aFPS) == noErr;
}

bool AppleVTEncoder::SetRealtime(bool aEnabled) {
  MOZ_ASSERT(mSession);

  // B-frames has been disabled in Init(), so no need to set it here.

  SessionPropertyManager mgr(mSession);
  OSStatus status = mgr.Set(kVTCompressionPropertyKey_RealTime, aEnabled);
  LOGD("%s real time, status: %d", aEnabled ? "Enable" : "Disable", status);
  if (status != noErr) {
    return false;
  }

  if (__builtin_available(macos 11.0, *)) {
    status = mgr.Set(
        kVTCompressionPropertyKey_PrioritizeEncodingSpeedOverQuality, aEnabled);
    LOGD("%s PrioritizeEncodingSpeedOverQuality, status: %d",
         aEnabled ? "Enable" : "Disable", status);
    if (status != noErr && status != kVTPropertyNotSupportedErr) {
      return false;
    }
  }

  int32_t maxFrameDelayCount = aEnabled ? 0 : kVTUnlimitedFrameDelayCount;
  status =
      mgr.Set(kVTCompressionPropertyKey_MaxFrameDelayCount, maxFrameDelayCount);
  LOGD("Set max frame delay count to %d, status: %d", maxFrameDelayCount,
       status);
  if (status != noErr && status != kVTPropertyNotSupportedErr) {
    return false;
  }

  return true;
}

bool AppleVTEncoder::SetProfileLevel(H264_PROFILE aValue) {
  MOZ_ASSERT(mSession);

  CFStringRef profileLevel = nullptr;
  switch (aValue) {
    case H264_PROFILE::H264_PROFILE_BASE:
      profileLevel = kVTProfileLevel_H264_Baseline_AutoLevel;
      break;
    case H264_PROFILE::H264_PROFILE_MAIN:
      profileLevel = kVTProfileLevel_H264_Main_AutoLevel;
      break;
    case H264_PROFILE::H264_PROFILE_HIGH:
      profileLevel = kVTProfileLevel_H264_High_AutoLevel;
      break;
    default:
      LOGE("Profile %d not handled", static_cast<int>(aValue));
  }

  if (profileLevel == nullptr) {
    return false;
  }

  SessionPropertyManager mgr(mSession);
  return mgr.Set(kVTCompressionPropertyKey_ProfileLevel, profileLevel) == noErr;
}

static Maybe<CFStringRef> MapColorPrimaries(
    const gfx::ColorSpace2& aPrimaries) {
  switch (aPrimaries) {
    case gfx::ColorSpace2::Display:
      return Nothing();
    case gfx::ColorSpace2::SRGB:
      return Some(kCVImageBufferColorPrimaries_P22);
    case gfx::ColorSpace2::DISPLAY_P3:
      return Some(kCVImageBufferColorPrimaries_P3_D65);
    case gfx::ColorSpace2::BT601_525:
      return Some(kCVImageBufferColorPrimaries_SMPTE_C);
    case gfx::ColorSpace2::BT709:
      return Some(kCVImageBufferColorPrimaries_ITU_R_709_2);
    case gfx::ColorSpace2::BT2020:
      return Some(kCVImageBufferColorPrimaries_ITU_R_2020);
  }

  MOZ_ASSERT_UNREACHABLE("Unsupported color primaries");
  return Nothing();
}

static Maybe<CFStringRef> MapYCbCrMatrix(const gfx::YUVColorSpace& aMatrix) {
  switch (aMatrix) {
    case gfx::YUVColorSpace::BT601:
      return Some(kCVImageBufferYCbCrMatrix_ITU_R_601_4);
    case gfx::YUVColorSpace::BT709:
      return Some(kCVImageBufferYCbCrMatrix_ITU_R_709_2);
    case gfx::YUVColorSpace::BT2020:
      return Some(kCVImageBufferYCbCrMatrix_ITU_R_2020);
    case gfx::YUVColorSpace::Identity:
      return Nothing();
  }

  MOZ_ASSERT_UNREACHABLE("Unsupported YCbCr matrix");
  return Nothing();
}

static Maybe<CFStringRef> MapTransferFunction(
    const gfx::TransferFunction& aTransferFunction) {
  switch (aTransferFunction) {
    case gfx::TransferFunction::BT709:
      return Some(kCVImageBufferTransferFunction_ITU_R_709_2);
    case gfx::TransferFunction::SRGB:
      return Some(kCVImageBufferTransferFunction_sRGB);
    case gfx::TransferFunction::PQ:
      return Some(kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ);
    case gfx::TransferFunction::HLG:
      return Some(kCVImageBufferTransferFunction_ITU_R_2100_HLG);
  }

  MOZ_ASSERT_UNREACHABLE("Unsupported transfer function");
  return Nothing();
}

struct EncoderColorSpace {
  CFStringRef mColorPrimaries = nullptr;
  CFStringRef mYCbCrMatrix = nullptr;
  CFStringRef mTransferFunction = nullptr;
};

static Result<EncoderColorSpace, MediaResult> MapColorSpace(
    const EncoderConfig::VideoColorSpace& aColorSpace) {
  EncoderColorSpace colorSpace;
  if (aColorSpace.mPrimaries) {
    Maybe<CFStringRef> p = MapColorPrimaries(aColorSpace.mPrimaries.ref());
    if (p.isNothing()) {
      return Err(MediaResult(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR,
                             "Unsupported color primaries"));
    }
    colorSpace.mColorPrimaries = p.value();
  }
  if (aColorSpace.mMatrix) {
    Maybe<CFStringRef> m = MapYCbCrMatrix(aColorSpace.mMatrix.ref());
    if (m.isNothing()) {
      return Err(MediaResult(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR,
                             "Unsupported YCbCr matrix"));
    }
    colorSpace.mYCbCrMatrix = m.value();
  }
  if (aColorSpace.mTransferFunction) {
    Maybe<CFStringRef> f =
        MapTransferFunction(aColorSpace.mTransferFunction.ref());
    if (f.isNothing()) {
      return Err(MediaResult(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR,
                             "Unsupported transfer function"));
    }
    colorSpace.mTransferFunction = f.value();
  }
  return colorSpace;
}

bool AppleVTEncoder::IsSettingColorSpaceSupported() const {
  SessionPropertyManager mgr(mSession);
  return mgr.IsSupported(kVTCompressionPropertyKey_ColorPrimaries) &&
         mgr.IsSupported(kVTCompressionPropertyKey_YCbCrMatrix) &&
         mgr.IsSupported(kVTCompressionPropertyKey_TransferFunction);
}

MediaResult AppleVTEncoder::SetColorSpace(
    const EncoderConfig::SampleFormat& aFormat) {
  MOZ_ASSERT(mSession);

  if (!aFormat.IsYUV()) {
    return MediaResult(NS_OK, "Skip setting color space for non-YUV formats");
  }

  if (!IsSettingColorSpaceSupported()) {
    return MediaResult(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR,
                       "Setting color space not supported");
  }

  auto r = MapColorSpace(aFormat.mColorSpace);
  if (r.isErr()) {
    return r.unwrapErr();
  }

  EncoderColorSpace colorSpace = r.unwrap();

  SessionPropertyManager mgr(mSession);
  AutoTArray<const char*, 3> properties;

  if (colorSpace.mColorPrimaries) {
    OSStatus status = mgr.Set(kVTCompressionPropertyKey_ColorPrimaries,
                              colorSpace.mColorPrimaries);
    if (status != noErr) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         "Failed to set color primaries");
    }
    properties.AppendElement("ColorPrimaries");
  }
  if (colorSpace.mYCbCrMatrix) {
    OSStatus status =
        mgr.Set(kVTCompressionPropertyKey_YCbCrMatrix, colorSpace.mYCbCrMatrix);
    if (status != noErr) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         "Failed to set YCbCr matrix");
    }
    properties.AppendElement("YCbCrMatrix");
  }
  if (colorSpace.mTransferFunction) {
    OSStatus status = mgr.Set(kVTCompressionPropertyKey_TransferFunction,
                              colorSpace.mTransferFunction);
    if (status != noErr) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         "Failed to set transfer function");
    }
    properties.AppendElement("TransferFunction");
  }

  nsCString msg;
  if (properties.IsEmpty()) {
    msg = "No color space properties set"_ns;
  } else {
    msg = StringJoin(","_ns, properties);
    msg.Append(" set");
  }

  return MediaResult(NS_OK, msg);
}

static Result<OSType, MediaResult> MapPixelFormat(
    dom::ImageBitmapFormat aFormat, gfx::ColorRange aColorRange) {
  const bool isFullRange = aColorRange == gfx::ColorRange::FULL;

  Maybe<OSType> fmt;
  switch (aFormat) {
    case dom::ImageBitmapFormat::YUV444P:
      return kCVPixelFormatType_444YpCbCr8;
    case dom::ImageBitmapFormat::YUV420P:
      return isFullRange ? kCVPixelFormatType_420YpCbCr8PlanarFullRange
                         : kCVPixelFormatType_420YpCbCr8Planar;
    case dom::ImageBitmapFormat::YUV420SP_NV12:
      return isFullRange ? kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
                         : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    case dom::ImageBitmapFormat::RGBA32:
      fmt.emplace(kCVPixelFormatType_32RGBA);
      break;
    case dom::ImageBitmapFormat::BGRA32:
      fmt.emplace(kCVPixelFormatType_32BGRA);
      break;
    case dom::ImageBitmapFormat::RGB24:
      fmt.emplace(kCVPixelFormatType_24RGB);
      break;
    case dom::ImageBitmapFormat::BGR24:
      fmt.emplace(kCVPixelFormatType_24BGR);
      break;
    case dom::ImageBitmapFormat::GRAY8:
      fmt.emplace(kCVPixelFormatType_OneComponent8);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unsupported image format");
  }

  // Limited RGB formats are not supported on MacOS (Bug 1957758).
  if (fmt) {
    if (!isFullRange) {
      return Err(MediaResult(
          NS_ERROR_NOT_IMPLEMENTED,
          nsPrintfCString("format %s with limited colorspace is not supported",
                          dom::GetEnumString(aFormat).get())));
    }
    return fmt.value();
  }

  return Err(MediaResult(NS_ERROR_NOT_IMPLEMENTED,
                         nsPrintfCString("format %s is not supported",
                                         dom::GetEnumString(aFormat).get())));
}

RefPtr<MediaDataEncoder::InitPromise> AppleVTEncoder::Init() {
  MOZ_ASSERT(!mSession,
             "Cannot initialize encoder again without shutting down");

  MediaResult r = InitSession();
  if (NS_FAILED(r.Code())) {
    LOGE("%s", r.Description().get());
    return InitPromise::CreateAndReject(r, __func__);
  }

  mError = NS_OK;
  return InitPromise::CreateAndResolve(true, __func__);
}

MediaResult AppleVTEncoder::InitSession() {
  MOZ_ASSERT(!mSession);

  auto errorExit = MakeScopeExit([&] { InvalidateSessionIfNeeded(); });

  if (mConfig.mSize.width == 0 || mConfig.mSize.height == 0) {
    return MediaResult(NS_ERROR_ILLEGAL_VALUE,
                       "width or height 0 in encoder init");
  }

  if (mConfig.mScalabilityMode != ScalabilityMode::None && !OSSupportsSVC()) {
    return MediaResult(NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR,
                       "SVC only supported on macOS 11.3 and more recent");
  }

  bool lowLatencyRateControl =
      mConfig.mUsage == Usage::Realtime ||
      mConfig.mScalabilityMode != ScalabilityMode::None;
  LOGD("low latency rate control: %s, Hardware allowed: %s",
       lowLatencyRateControl ? "yes" : "no",
       mHardwareNotAllowed ? "no" : "yes");
  AutoCFTypeRef<CFDictionaryRef> spec(
      BuildEncoderSpec(mHardwareNotAllowed, lowLatencyRateControl));

  // Bug 1955153: Set sourceImageBufferAttributes using the pixel format derived
  // from mConfig.mFormat.
  OSStatus status = VTCompressionSessionCreate(
      kCFAllocatorDefault, mConfig.mSize.width, mConfig.mSize.height,
      kCMVideoCodecType_H264, spec, nullptr /* sourceImageBufferAttributes */,
      kCFAllocatorDefault, &FrameCallback, this /* outputCallbackRefCon */,
      mSession.Receive());
  if (status != noErr) {
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       "fail to create encoder session");
  }

  SessionPropertyManager mgr(mSession);

  if (mgr.Set(kVTCompressionPropertyKey_AllowFrameReordering, false) != noErr) {
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       "Couldn't disable bframes");
  }

  if (mConfig.mUsage == Usage::Realtime && !SetRealtime(true)) {
    return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                       "fail to configure real-time");
  }

  if (mConfig.mBitrate) {
    if (mConfig.mCodec == CodecType::H264 &&
        mConfig.mBitrateMode == BitrateMode::Constant) {
      // Not supported, fall-back to VBR.
      LOGD("H264 CBR not supported in VideoToolbox, falling back to VBR");
      mConfig.mBitrateMode = BitrateMode::Variable;
    }
    bool rv = SetBitrateAndMode(mConfig.mBitrateMode, mConfig.mBitrate);
    if (!rv) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         "fail to configurate bitrate");
    }
  }

  if (mConfig.mScalabilityMode != ScalabilityMode::None) {
    if (__builtin_available(macos 11.3, *)) {
      float baseLayerFPSRatio = 1.0f;
      switch (mConfig.mScalabilityMode) {
        case ScalabilityMode::L1T2:
          baseLayerFPSRatio = 0.5;
          break;
        case ScalabilityMode::L1T3:
          // Not supported in hw on macOS, but is accepted and errors out when
          // encoding. Reject the configuration now.
          return MediaResult(
              NS_ERROR_DOM_MEDIA_FATAL_ERR,
              nsPrintfCString("macOS only support L1T2 h264 SVC"));
        default:
          MOZ_ASSERT_UNREACHABLE("Unhandled value");
      }

      if (mgr.Set(kVTCompressionPropertyKey_BaseLayerFrameRateFraction,
                  baseLayerFPSRatio) != noErr) {
        return MediaResult(
            NS_ERROR_DOM_MEDIA_FATAL_ERR,
            nsPrintfCString("fail to configure SVC (base ratio: %f",
                            baseLayerFPSRatio));
      }
    } else {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         "macOS version too old to enable SVC");
    }
  }

  int64_t interval =
      mConfig.mKeyframeInterval > std::numeric_limits<int64_t>::max()
          ? std::numeric_limits<int64_t>::max()
          : AssertedCast<int64_t>(mConfig.mKeyframeInterval);

  if (mgr.Set(kVTCompressionPropertyKey_MaxKeyFrameInterval, interval) !=
      noErr) {
    return MediaResult(
        NS_ERROR_DOM_MEDIA_FATAL_ERR,
        nsPrintfCString("fail to configurate keyframe interval:%" PRId64,
                        interval));
  }

  if (mConfig.mCodecSpecific) {
    const H264Specific& specific = mConfig.mCodecSpecific->as<H264Specific>();
    if (!SetProfileLevel(specific.mProfile)) {
      return MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         nsPrintfCString("fail to configurate profile level:%d",
                                         int(specific.mProfile)));
    }
  }

  MediaResult colorSpaceResult = SetColorSpace(mConfig.mFormat);
  if (NS_SUCCEEDED(colorSpaceResult.Code())) {
    LOGD("%s", colorSpaceResult.Description().get());
  } else if (colorSpaceResult.Code() == NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR) {
    // Color space not supported, ignore.
    LOGW("%s", colorSpaceResult.Description().get());
  } else {
    MOZ_ASSERT(NS_FAILED(colorSpaceResult.Code()));
    LOGE("%s", colorSpaceResult.Description().get());
    return colorSpaceResult;
  }

  bool isUsingHW = false;
  status =
      mgr.Copy(kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder,
               isUsingHW);
  mIsHardwareAccelerated = status == noErr && isUsingHW;
  LOGD("Using hw acceleration: %s", mIsHardwareAccelerated ? "yes" : "no");

  errorExit.release();
  return NS_OK;
}

void AppleVTEncoder::InvalidateSessionIfNeeded() {
  if (mSession) {
    VTCompressionSessionInvalidate(mSession);
    mSession.Reset();
  }
}

CFDictionaryRef AppleVTEncoder::BuildSourceImageBufferAttributes(
    OSType aPixelFormat) {
  // Source image buffer attributes
  const void* keys[] = {kCVPixelBufferOpenGLCompatibilityKey,  // TODO
                        kCVPixelBufferIOSurfacePropertiesKey,  // TODO
                        kCVPixelBufferPixelFormatTypeKey};

  AutoCFTypeRef<CFDictionaryRef> ioSurfaceProps(CFDictionaryCreate(
      kCFAllocatorDefault, nullptr, nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks));
  AutoCFTypeRef<CFNumberRef> pixelFormat(
      CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &aPixelFormat));
  const void* values[] = {kCFBooleanTrue, ioSurfaceProps, pixelFormat};

  MOZ_ASSERT(std::size(keys) == std::size(values),
             "Non matching keys/values array size");

  return CFDictionaryCreate(kCFAllocatorDefault, keys, values, std::size(keys),
                            &kCFTypeDictionaryKeyCallBacks,
                            &kCFTypeDictionaryValueCallBacks);
}

static bool IsKeyframe(CMSampleBufferRef aSample) {
  CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(aSample, 0);
  if (attachments == nullptr || CFArrayGetCount(attachments) == 0) {
    return false;
  }

  return !CFDictionaryContainsKey(
      static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0)),
      kCMSampleAttachmentKey_NotSync);
}

static size_t GetNumParamSets(CMFormatDescriptionRef aDescription) {
  size_t numParamSets = 0;
  OSStatus status = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
      aDescription, 0, nullptr, nullptr, &numParamSets, nullptr);
  if (status != noErr) {
    LOGE("Cannot get number of parameter sets from format description");
  }

  return numParamSets;
}

static const uint8_t kNALUStart[4] = {0, 0, 0, 1};

static size_t GetParamSet(CMFormatDescriptionRef aDescription, size_t aIndex,
                          const uint8_t** aDataPtr) {
  size_t length = 0;
  int headerSize = 0;
  if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
          aDescription, aIndex, aDataPtr, &length, nullptr, &headerSize) !=
      noErr) {
    LOGE("failed to get parameter set from format description");
    return 0;
  }
  MOZ_ASSERT(headerSize == sizeof(kNALUStart), "Only support 4 byte header");

  return length;
}

static bool WriteSPSPPS(MediaRawData* aDst,
                        CMFormatDescriptionRef aDescription) {
  // Get SPS/PPS
  const size_t numParamSets = GetNumParamSets(aDescription);
  UniquePtr<MediaRawDataWriter> writer(aDst->CreateWriter());
  for (size_t i = 0; i < numParamSets; i++) {
    const uint8_t* data = nullptr;
    size_t length = GetParamSet(aDescription, i, &data);
    if (length == 0) {
      return false;
    }
    if (!writer->Append(kNALUStart, sizeof(kNALUStart))) {
      LOGE("Cannot write NAL unit start code");
      return false;
    }
    if (!writer->Append(data, length)) {
      LOGE("Cannot write parameter set");
      return false;
    }
  }
  return true;
}

static RefPtr<MediaByteBuffer> extractAvcc(
    CMFormatDescriptionRef aDescription) {
  CFPropertyListRef list = CMFormatDescriptionGetExtension(
      aDescription,
      kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms);
  if (!list) {
    LOGE("fail to get atoms");
    return nullptr;
  }
  CFDataRef avcC = static_cast<CFDataRef>(
      CFDictionaryGetValue(static_cast<CFDictionaryRef>(list), CFSTR("avcC")));
  if (!avcC) {
    LOGE("fail to extract avcC");
    return nullptr;
  }
  CFIndex length = CFDataGetLength(avcC);
  const UInt8* bytes = CFDataGetBytePtr(avcC);
  if (length <= 0 || !bytes) {
    LOGE("empty avcC");
    return nullptr;
  }

  RefPtr<MediaByteBuffer> config = new MediaByteBuffer(length);
  config->AppendElements(bytes, length);
  return config;
}

bool AppleVTEncoder::WriteExtraData(MediaRawData* aDst, CMSampleBufferRef aSrc,
                                    const bool aAsAnnexB) {
  if (!IsKeyframe(aSrc)) {
    return true;
  }

  LOGV("Writing extra data (%s) for keyframe", aAsAnnexB ? "AnnexB" : "AVCC");

  aDst->mKeyframe = true;
  CMFormatDescriptionRef desc = CMSampleBufferGetFormatDescription(aSrc);
  if (!desc) {
    LOGE("fail to get format description from sample");
    return false;
  }

  if (aAsAnnexB) {
    return WriteSPSPPS(aDst, desc);
  }

  RefPtr<MediaByteBuffer> avcc = extractAvcc(desc);
  if (!avcc) {
    LOGE("failed to extract avcc");
    return false;
  }

  if (!mAvcc || !H264::CompareExtraData(avcc, mAvcc)) {
    LOGV("avcC changed, updating");
    mAvcc = avcc;
    aDst->mExtraData = mAvcc;
  }

  return true;
}

static bool WriteNALUs(MediaRawData* aDst, CMSampleBufferRef aSrc,
                       bool aAsAnnexB = false) {
  size_t srcRemaining = CMSampleBufferGetTotalSampleSize(aSrc);
  CMBlockBufferRef block = CMSampleBufferGetDataBuffer(aSrc);
  if (!block) {
    LOGE("Cannot get block buffer frome sample");
    return false;
  }
  UniquePtr<MediaRawDataWriter> writer(aDst->CreateWriter());
  size_t writtenLength = aDst->Size();
  // Ensure capacity.
  if (!writer->SetSize(writtenLength + srcRemaining)) {
    LOGE("Cannot allocate buffer");
    return false;
  }
  size_t readLength = 0;
  while (srcRemaining > 0) {
    // Extract the size of next NAL unit
    uint8_t unitSizeBytes[4];
    MOZ_ASSERT(srcRemaining > sizeof(unitSizeBytes));
    if (CMBlockBufferCopyDataBytes(block, readLength, sizeof(unitSizeBytes),
                                   reinterpret_cast<uint32_t*>(
                                       unitSizeBytes)) != kCMBlockBufferNoErr) {
      LOGE("Cannot copy unit size bytes");
      return false;
    }
    size_t unitSize =
        CFSwapInt32BigToHost(*reinterpret_cast<uint32_t*>(unitSizeBytes));

    if (aAsAnnexB) {
      // Replace unit size bytes with NALU start code.
      PodCopy(writer->Data() + writtenLength, kNALUStart, sizeof(kNALUStart));
      readLength += sizeof(unitSizeBytes);
      srcRemaining -= sizeof(unitSizeBytes);
      writtenLength += sizeof(kNALUStart);
    } else {
      // Copy unit size bytes + data.
      unitSize += sizeof(unitSizeBytes);
    }
    MOZ_ASSERT(writtenLength + unitSize <= aDst->Size());
    // Copy NAL unit data
    if (CMBlockBufferCopyDataBytes(block, readLength, unitSize,
                                   writer->Data() + writtenLength) !=
        kCMBlockBufferNoErr) {
      LOGE("Cannot copy unit data");
      return false;
    }
    readLength += unitSize;
    srcRemaining -= unitSize;
    writtenLength += unitSize;
  }
  MOZ_ASSERT(writtenLength == aDst->Size());
  return true;
}

void AppleVTEncoder::OutputFrame(OSStatus aStatus, VTEncodeInfoFlags aFlags,
                                 CMSampleBufferRef aBuffer) {
  LOGV("status: %d, flags: %d, buffer %p", aStatus, aFlags, aBuffer);

  if (aStatus != noErr) {
    ProcessOutput(nullptr, EncodeResult::EncodeError);
    return;
  }

  if (aFlags & kVTEncodeInfo_FrameDropped) {
    ProcessOutput(nullptr, EncodeResult::FrameDropped);
    return;
  }

  if (!aBuffer) {
    ProcessOutput(nullptr, EncodeResult::EmptyBuffer);
    return;
  }

  RefPtr<MediaRawData> output(new MediaRawData());

  if (__builtin_available(macos 11.3, *)) {
    if (mConfig.mScalabilityMode != ScalabilityMode::None) {
      CFDictionaryRef dict = (CFDictionaryRef)(CFArrayGetValueAtIndex(
          CMSampleBufferGetSampleAttachmentsArray(aBuffer, true), 0));
      CFBooleanRef isBaseLayerRef = (CFBooleanRef)CFDictionaryGetValue(
          dict, (const void*)kCMSampleAttachmentKey_IsDependedOnByOthers);
      Boolean isBaseLayer = CFBooleanGetValue(isBaseLayerRef);
      output->mTemporalLayerId.emplace(isBaseLayer ? 0 : 1);
    }
  }

  bool forceAvcc = false;
  if (mConfig.mCodecSpecific->is<H264Specific>()) {
    forceAvcc = mConfig.mCodecSpecific->as<H264Specific>().mFormat ==
                H264BitStreamFormat::AVC;
  }
  bool asAnnexB = !forceAvcc;
  bool succeeded = WriteExtraData(output, aBuffer, asAnnexB) &&
                   WriteNALUs(output, aBuffer, asAnnexB);

  output->mTime = media::TimeUnit::FromSeconds(
      CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(aBuffer)));
  output->mDuration = media::TimeUnit::FromSeconds(
      CMTimeGetSeconds(CMSampleBufferGetOutputDuration(aBuffer)));
  LOGV("Make a %s output[time: %s, duration: %s]: %s",
       asAnnexB ? "AnnexB" : "AVCC", output->mTime.ToString().get(),
       output->mDuration.ToString().get(), succeeded ? "succeed" : "failed");
  ProcessOutput(succeeded ? std::move(output) : nullptr, EncodeResult::Success);
}

void AppleVTEncoder::ProcessOutput(RefPtr<MediaRawData>&& aOutput,
                                   EncodeResult aResult) {
  if (!mTaskQueue->IsCurrentThreadIn()) {
    LOGV("Dispatch ProcessOutput to task queue");
    nsresult rv = mTaskQueue->Dispatch(
        NewRunnableMethod<RefPtr<MediaRawData>, EncodeResult>(
            "AppleVTEncoder::ProcessOutput", this,
            &AppleVTEncoder::ProcessOutput, std::move(aOutput), aResult));
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    Unused << rv;
    return;
  }

  if (aResult != EncodeResult::Success) {
    switch (aResult) {
      case EncodeResult::EncodeError:
        mError = MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, "Failed to encode");
        break;
      case EncodeResult::EmptyBuffer:
        mError = MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, "Buffer is empty");
        break;
      case EncodeResult::FrameDropped:
        if (mConfig.mUsage == Usage::Realtime) {
          // Dropping a frame in real-time usage is okay.
          LOGW("Frame is dropped");
        } else {
          // Some usages like transcoding should not drop a frame.
          LOGE("Frame is dropped");
          mError =
              MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, "Frame is dropped");
        }
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("Unknown EncodeResult");
        break;
    }
    MaybeResolveOrRejectEncodePromise();
    return;
  }

  LOGV("Got %zu bytes of output", !aOutput.get() ? 0 : aOutput->Size());

  if (!aOutput) {
    mError = MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR, "No converted output");
    MaybeResolveOrRejectEncodePromise();
    return;
  }

  mEncodedData.AppendElement(std::move(aOutput));
  MaybeResolveOrRejectEncodePromise();
}

RefPtr<MediaDataEncoder::EncodePromise> AppleVTEncoder::Encode(
    const MediaData* aSample) {
  MOZ_ASSERT(aSample != nullptr);

  RefPtr<const VideoData> sample(aSample->As<const VideoData>());

  RefPtr<AppleVTEncoder> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self, this, sample] {
    MOZ_ASSERT(mEncodePromise.IsEmpty(),
               "Encode should not be called again before getting results");
    RefPtr<EncodePromise> p = mEncodePromise.Ensure(__func__);
    ProcessEncode(sample);
    return p;
  });
}

RefPtr<MediaDataEncoder::ReconfigurationPromise> AppleVTEncoder::Reconfigure(
    const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges) {
  return InvokeAsync(mTaskQueue, this, __func__,
                     &AppleVTEncoder::ProcessReconfigure,
                     aConfigurationChanges);
}

void AppleVTEncoder::ProcessEncode(const RefPtr<const VideoData>& aSample) {
  LOGV("::ProcessEncode");
  AssertOnTaskQueue();
  MOZ_ASSERT(mSession);

  if (NS_FAILED(mError)) {
    LOGE("Pending error: %s", mError.Description().get());
    MaybeResolveOrRejectEncodePromise();
  }

  AutoCVBufferRef<CVImageBufferRef> buffer(
      CreateCVPixelBuffer(aSample->mImage));
  if (!buffer) {
    LOGE("Failed to allocate buffer");
    mError = MediaResult(NS_ERROR_OUT_OF_MEMORY, "failed to allocate buffer");
    MaybeResolveOrRejectEncodePromise();
    return;
  }

  CFDictionaryRef frameProps = nullptr;
  if (aSample->mKeyframe) {
    CFTypeRef keys[] = {kVTEncodeFrameOptionKey_ForceKeyFrame};
    CFTypeRef values[] = {kCFBooleanTrue};
    MOZ_ASSERT(std::size(keys) == std::size(values));
    frameProps = CFDictionaryCreate(
        kCFAllocatorDefault, keys, values, std::size(keys),
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  };

  VTEncodeInfoFlags info;
  OSStatus status = VTCompressionSessionEncodeFrame(
      mSession, buffer,
      CMTimeMake(aSample->mTime.ToMicroseconds(), USECS_PER_S),
      CMTimeMake(aSample->mDuration.ToMicroseconds(), USECS_PER_S), frameProps,
      nullptr /* sourceFrameRefcon */, &info);
  if (status != noErr) {
    LOGE("VTCompressionSessionEncodeFrame error: %d", status);
    mError = MediaResult(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                         "VTCompressionSessionEncodeFrame error");
    MaybeResolveOrRejectEncodePromise();
    return;
  }

  if (mConfig.mUsage != Usage::Realtime) {
    MaybeResolveOrRejectEncodePromise();
    return;
  }

  // The latency between encoding a sample and receiving the encoded output is
  // critical in real-time usage. To minimize the latency, the output result
  // should be returned immediately once they are ready, instead of being
  // returned in the next or later Encode() iterations.
  LOGV("Encoding in progress");

  // Workaround for real-time encoding in OS versions < 11.
  ForceOutputIfNeeded();
}

RefPtr<MediaDataEncoder::ReconfigurationPromise>
AppleVTEncoder::ProcessReconfigure(
    const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges) {
  AssertOnTaskQueue();
  MOZ_ASSERT(mSession);

  bool ok = false;
  for (const auto& confChange : aConfigurationChanges->mChanges) {
    // A reconfiguration on the fly succeeds if all changes can be applied
    // successfuly. In case of failure, the encoder will be drained and
    // recreated.
    ok &= confChange.match(
        // Not supported yet
        [&](const DimensionsChange& aChange) -> bool { return false; },
        [&](const DisplayDimensionsChange& aChange) -> bool { return false; },
        [&](const BitrateModeChange& aChange) -> bool {
          mConfig.mBitrateMode = aChange.get();
          return SetBitrateAndMode(mConfig.mBitrateMode, mConfig.mBitrate);
        },
        [&](const BitrateChange& aChange) -> bool {
          mConfig.mBitrate = aChange.get().refOr(0);
          // 0 is the default in AppleVTEncoder: the encoder chooses the bitrate
          // based on the content.
          return SetBitrateAndMode(mConfig.mBitrateMode, mConfig.mBitrate);
        },
        [&](const FramerateChange& aChange) -> bool {
          // 0 means default, in VideoToolbox, and is valid, perform some light
          // sanitation on other values.
          double fps = aChange.get().refOr(0);
          if (std::isnan(fps) || fps < 0 ||
              int64_t(fps) > std::numeric_limits<int32_t>::max()) {
            LOGE("Invalid fps of %lf", fps);
            return false;
          }
          return SetFrameRate(AssertedCast<int64_t>(fps));
        },
        [&](const UsageChange& aChange) -> bool {
          mConfig.mUsage = aChange.get();
          return SetRealtime(aChange.get() == Usage::Realtime);
        },
        [&](const ContentHintChange& aChange) -> bool { return false; },
        [&](const SampleRateChange& aChange) -> bool { return false; },
        [&](const NumberOfChannelsChange& aChange) -> bool { return false; });
  };
  using P = MediaDataEncoder::ReconfigurationPromise;
  if (ok) {
    return P::CreateAndResolve(true, __func__);
  }
  return P::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR, __func__);
}

static size_t NumberOfPlanes(OSType aPixelFormat) {
  switch (aPixelFormat) {
    case kCVPixelFormatType_32RGBA:
    case kCVPixelFormatType_32BGRA:
    case kCVPixelFormatType_24RGB:
    case kCVPixelFormatType_24BGR:
    case kCVPixelFormatType_OneComponent8:
      return 1;
    case kCVPixelFormatType_444YpCbCr8:
    case kCVPixelFormatType_420YpCbCr8PlanarFullRange:
    case kCVPixelFormatType_420YpCbCr8Planar:
      return 3;
    case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
      return 2;
    default:
      LOGE("Unsupported input pixel format");
      return 0;
  }
}

using namespace layers;

static void ReleaseSurface(void* aReleaseRef, const void* aBaseAddress) {
  RefPtr<gfx::DataSourceSurface> released =
      dont_AddRef(static_cast<gfx::DataSourceSurface*>(aReleaseRef));
}

static void ReleaseImage(void* aImageGrip, const void* aDataPtr,
                         size_t aDataSize, size_t aNumOfPlanes,
                         const void** aPlanes) {
  (static_cast<PlanarYCbCrImage*>(aImageGrip))->Release();
}

CVPixelBufferRef AppleVTEncoder::CreateCVPixelBuffer(Image* aSource) {
  AssertOnTaskQueue();

  auto sfr = EncoderConfig::SampleFormat::FromImage(aSource);
  if (sfr.isErr()) {
    MediaResult err = sfr.unwrapErr();
    LOGE("%s", err.Description().get());
    return nullptr;
  }
  const EncoderConfig::SampleFormat sf = sfr.unwrap();

  gfx::ColorRange defaultColorRange =
      sf.IsYUV() ? gfx::ColorRange::LIMITED : gfx::ColorRange::FULL;
  auto pfr = MapPixelFormat(sf.mPixelFormat, sf.mColorSpace.mRange
                                                 ? sf.mColorSpace.mRange.value()
                                                 : defaultColorRange);
  if (pfr.isErr()) {
    MediaResult err = pfr.unwrapErr();
    LOGE("%s", err.Description().get());
    return nullptr;
  }

  OSType pixelFormat = pfr.unwrap();

  if (sf != mConfig.mFormat) {
    LOGV(
        "Input image in format %s but encoder configured with format %s. "
        "Fingers crossed",
        sf.ToString().get(), mConfig.mFormat.ToString().get());
    // Bug 1955153: If the encoder encounters a kVTPixelTransferNotSupportedErr
    // error due to an unsupported image format, it must be re-initialized.
    // Additionally, any changes to the color space also require re-initializing
    // the encoder.
  }

  if (aSource->GetFormat() == ImageFormat::PLANAR_YCBCR) {
    PlanarYCbCrImage* image = aSource->AsPlanarYCbCrImage();
    if (!image || !image->GetData()) {
      LOGE("Failed to get PlanarYCbCrImage or its data");
      return nullptr;
    }

    size_t numPlanes = NumberOfPlanes(pixelFormat);
    const PlanarYCbCrImage::Data* yuv = image->GetData();

    auto ySize = yuv->YDataSize();
    auto cbcrSize = yuv->CbCrDataSize();
    void* addresses[3] = {};
    size_t widths[3] = {};
    size_t heights[3] = {};
    size_t strides[3] = {};
    switch (numPlanes) {
      case 3:
        addresses[2] = yuv->mCrChannel;
        widths[2] = cbcrSize.width;
        heights[2] = cbcrSize.height;
        strides[2] = yuv->mCbCrStride;
        [[fallthrough]];
      case 2:
        addresses[1] = yuv->mCbChannel;
        widths[1] = cbcrSize.width;
        heights[1] = cbcrSize.height;
        strides[1] = yuv->mCbCrStride;
        [[fallthrough]];
      case 1:
        addresses[0] = yuv->mYChannel;
        widths[0] = ySize.width;
        heights[0] = ySize.height;
        strides[0] = yuv->mYStride;
        break;
      default:
        LOGE("Unexpected number of planes: %zu", numPlanes);
        MOZ_ASSERT_UNREACHABLE("Unexpected number of planes");
        return nullptr;
    }

    CVPixelBufferRef buffer = nullptr;
    image->AddRef();  // Grip input buffers.
    CVReturn rv = CVPixelBufferCreateWithPlanarBytes(
        kCFAllocatorDefault, yuv->mPictureRect.width, yuv->mPictureRect.height,
        pixelFormat, nullptr /* dataPtr */, 0 /* dataSize */, numPlanes,
        addresses, widths, heights, strides, ReleaseImage /* releaseCallback */,
        image /* releaseRefCon */, nullptr /* pixelBufferAttributes */,
        &buffer);
    if (rv == kCVReturnSuccess) {
      return buffer;
      // |image| will be released in |ReleaseImage()|.
    }
    LOGE("CVPIxelBufferCreateWithPlanarBytes error");
    image->Release();
    return nullptr;
  }

  RefPtr<gfx::SourceSurface> surface = aSource->GetAsSourceSurface();
  if (!surface) {
    LOGE("Failed to get SourceSurface");
    return nullptr;
  }

  RefPtr<gfx::DataSourceSurface> dataSurface = surface->GetDataSurface();
  if (!dataSurface) {
    LOGE("Failed to get DataSurface");
    return nullptr;
  }

  gfx::DataSourceSurface::ScopedMap map(dataSurface,
                                        gfx::DataSourceSurface::READ);
  if (NS_WARN_IF(!map.IsMapped())) {
    LOGE("Failed to map DataSurface");
    return nullptr;
  }

  CVPixelBufferRef buffer = nullptr;
  gfx::DataSourceSurface* dss = dataSurface.forget().take();
  CVReturn rv = CVPixelBufferCreateWithBytes(
      kCFAllocatorDefault, dss->GetSize().Width(), dss->GetSize().Height(),
      pixelFormat, map.GetData(), map.GetStride(), ReleaseSurface, dss, nullptr,
      &buffer);
  if (rv == kCVReturnSuccess) {
    return buffer;
    // |dss| will be released in |ReleaseSurface()|.
  }
  LOGE("CVPIxelBufferCreateWithBytes error: %d", rv);
  RefPtr<gfx::DataSourceSurface> released = dont_AddRef(dss);
  return nullptr;
}

RefPtr<MediaDataEncoder::EncodePromise> AppleVTEncoder::Drain() {
  return InvokeAsync(mTaskQueue, this, __func__, &AppleVTEncoder::ProcessDrain);
}

RefPtr<MediaDataEncoder::EncodePromise> AppleVTEncoder::ProcessDrain() {
  LOGV("::ProcessDrain");
  AssertOnTaskQueue();
  MOZ_ASSERT(mSession);

  OSStatus status =
      VTCompressionSessionCompleteFrames(mSession, kCMTimeIndefinite);
  if (status != noErr) {
    LOGE("VTCompressionSessionCompleteFrames error");
    return EncodePromise::CreateAndReject(NS_ERROR_DOM_MEDIA_FATAL_ERR,
                                          __func__);
  }

  // Resolve the pending encode promise if any.
  MaybeResolveOrRejectEncodePromise();

  // VTCompressionSessionCompleteFrames() could have queued multiple tasks with
  // the new drained frames. Dispatch a task after them to resolve the promise
  // with those frames.
  RefPtr<AppleVTEncoder> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self]() {
    EncodedData pendingFrames(std::move(self->mEncodedData));
    LOGV("Resolve drain promise with %zu encoded outputs",
         pendingFrames.Length());
    self->mEncodedData = EncodedData();
    return EncodePromise::CreateAndResolve(std::move(pendingFrames), __func__);
  });
}

RefPtr<ShutdownPromise> AppleVTEncoder::Shutdown() {
  return InvokeAsync(mTaskQueue, this, __func__,
                     &AppleVTEncoder::ProcessShutdown);
}

RefPtr<ShutdownPromise> AppleVTEncoder::ProcessShutdown() {
  LOGD("::ProcessShutdown");
  AssertOnTaskQueue();
  InvalidateSessionIfNeeded();

  mError = MediaResult(NS_ERROR_DOM_MEDIA_CANCELED, "Canceled in shutdown");
  MaybeResolveOrRejectEncodePromise();
  mError = NS_OK;

  return ShutdownPromise::CreateAndResolve(true, __func__);
}

RefPtr<GenericPromise> AppleVTEncoder::SetBitrate(uint32_t aBitsPerSec) {
  RefPtr<AppleVTEncoder> self = this;
  return InvokeAsync(mTaskQueue, __func__, [self, aBitsPerSec]() {
    MOZ_ASSERT(self->mSession);
    bool rv = self->SetBitrateAndMode(self->mConfig.mBitrateMode, aBitsPerSec);
    return rv ? GenericPromise::CreateAndResolve(true, __func__)
              : GenericPromise::CreateAndReject(
                    NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR, __func__);
  });
}

void AppleVTEncoder::MaybeResolveOrRejectEncodePromise() {
  AssertOnTaskQueue();

  if (mEncodePromise.IsEmpty()) {
    LOGV(
        "No pending promise to resolve(pending outputs: %zu) or reject(err: "
        "%s)",
        mEncodedData.Length(), mError.Description().get());
    return;
  }

  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }

  if (NS_FAILED(mError.Code())) {
    LOGE("Rejecting encode promise with error: %s", mError.Description().get());
    mEncodePromise.Reject(mError, __func__);
    return;
  }

  LOGV("Resolving with %zu encoded outputs", mEncodedData.Length());
  mEncodePromise.Resolve(std::move(mEncodedData), __func__);
}

void AppleVTEncoder::ForceOutputIfNeeded() {
  if (__builtin_available(macos 11.0, *)) {
    return;
  }
  // Ideally, OutputFrame (called via FrameCallback) should resolve the encode
  // promise. However, sometimes output is produced only after multiple
  // inputs. To ensure continuous encoding, we force the encoder to produce a
  // potentially empty output if no result is received in 50 ms.
  RefPtr<AppleVTEncoder> self = this;
  auto r = NS_NewTimerWithCallback(
      [self](nsITimer* aTimer) {
        if (!self->mSession) {
          LOGV("Do nothing since the encoder has been shut down");
          return;
        }

        LOGV("Resolving the pending promise");
        self->MaybeResolveOrRejectEncodePromise();
      },
      TimeDuration::FromMilliseconds(50), nsITimer::TYPE_ONE_SHOT,
      "EncodingProgressChecker", mTaskQueue);
  if (r.isErr()) {
    LOGE(
        "Failed to set an encoding progress checker. Resolve the pending "
        "promise now");
    MaybeResolveOrRejectEncodePromise();
    return;
  }
  mTimer = r.unwrap();
}

#undef LOGE
#undef LOGW
#undef LOGD
#undef LOGV

}  // namespace mozilla
