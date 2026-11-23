/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WMFDataEncoderUtils.h"

#include "EncoderConfig.h"
#include "MFTEncoder.h"
#include "MediaData.h"
#include "mozilla/Logging.h"
#include "mozilla/gfx/gfxVars.h"

using mozilla::media::EncodeSupport;
using mozilla::media::EncodeSupportSet;

namespace mozilla {

#define WMF_ENC_LOG(arg, ...)                             \
  MOZ_LOG_FMT(mozilla::sPEMLog, mozilla::LogLevel::Error, \
              "WMFDataEncoderUtils::{}: " arg, __func__, ##__VA_ARGS__)

GUID CodecToSubtype(CodecType aCodec) {
  switch (aCodec) {
    case CodecType::H264:
      return MFVideoFormat_H264;
    case CodecType::VP8:
      return MFVideoFormat_VP80;
    case CodecType::VP9:
      return MFVideoFormat_VP90;
    default:
      return GUID_NULL;
  }
}

bool IsFrameSizeSupportedForNV12Input(const gfx::IntSize& aSize) {
  return aSize.width % 2 == 0;
}

static bool IsNotRealtimeOrHwRequired(const EncoderConfig& aConfig) {
  // Some hardware encoders have poor realtime performance, as measured by the
  // latency between frames. If the config is not realtime, or hardware is
  // required, hardware is allowed. See bug 2049606.
  return aConfig.mUsage != Usage::Realtime ||
         aConfig.mHardwarePreference == HardwarePreference::RequireHardware;
}

bool CanUseWMFHwEncoder(const GUID& aSubtype) {
  if (!gfx::gfxVars::IsInitialized() || !XRE_IsGPUProcess()) {
    return false;
  }

  if (IsEqualGUID(aSubtype, MFVideoFormat_H264)) {
    return gfx::gfxVars::UseH264HwEncode();
  }

  if (IsEqualGUID(aSubtype, MFVideoFormat_VP90)) {
    return gfx::gfxVars::UseVP9HwEncode();
  }

  if (IsEqualGUID(aSubtype, MFVideoFormat_VP80)) {
    return gfx::gfxVars::UseVP8HwEncode();
  }

  return false;
}

bool CanUseWMFHwEncoder(const EncoderConfig& aConfig) {
  if (!gfx::gfxVars::IsInitialized() || !XRE_IsGPUProcess()) {
    return false;
  }

  switch (aConfig.mCodec) {
    case CodecType::H264:
      return gfx::gfxVars::UseH264HwEncode();
    case CodecType::VP8:
      return IsNotRealtimeOrHwRequired(aConfig) &&
             gfx::gfxVars::UseVP8HwEncode();
    case CodecType::VP9:
      return IsNotRealtimeOrHwRequired(aConfig) &&
             gfx::gfxVars::UseVP9HwEncode();
    default:
      return false;
  }
}

EncodeSupportSet CanCreateWMFEncoder(const EncoderConfig& aConfig) {
  EncodeSupportSet supports;
  mscom::EnsureMTA([&]() {
    if (!wmf::MediaFoundationInitializer::HasInitialized()) {
      return;
    }
    // Try HW encoder if allowed by graphics and not disallowed by
    // the caller.
    if (aConfig.mHardwarePreference != HardwarePreference::RequireSoftware) {
      if (CanUseWMFHwEncoder(aConfig)) {
        auto hwEnc =
            MakeRefPtr<MFTEncoder>(MFTEncoder::HWPreference::HardwareOnly);
        if (SUCCEEDED(hwEnc->Create(CodecToSubtype(aConfig.mCodec),
                                    aConfig.mSize, aConfig.mCodecSpecific))) {
          supports += EncodeSupport::HardwareEncode;
        }
      } else {
        WMF_ENC_LOG("HW encoder is disabled for {}",
                    EnumValueToString(aConfig.mCodec));
      }
    }
    // Try SW encoder if not disallowed by the caller, only for H264. We
    // otherwise prefer to use SW encoders from ffvpx. We also want to avoid SW
    // encoding in the GPU process.
    if (aConfig.mHardwarePreference != HardwarePreference::RequireHardware &&
        aConfig.mCodec == CodecType::H264 && !XRE_IsGPUProcess()) {
      auto swEnc =
          MakeRefPtr<MFTEncoder>(MFTEncoder::HWPreference::SoftwareOnly);
      if (SUCCEEDED(swEnc->Create(CodecToSubtype(aConfig.mCodec), aConfig.mSize,
                                  aConfig.mCodecSpecific))) {
        supports += EncodeSupport::SoftwareEncode;
      }
    }

    WMF_ENC_LOG(
        "{} encoder support for {}",
        supports.contains(EncodeSupportSet(EncodeSupport::HardwareEncode,
                                           EncodeSupport::SoftwareEncode))
            ? "HW | SW"
        : supports.contains(EncodeSupport::HardwareEncode) ? "HW"
        : supports.contains(EncodeSupport::SoftwareEncode) ? "SW"
                                                           : "No",
        aConfig.ToString().get());
  });
  return supports;
}

static already_AddRefed<MediaByteBuffer> ParseH264Parameters(
    const nsTArray<uint8_t>& aHeader, const bool aAsAnnexB) {
  if (!aAsAnnexB) {
    return AnnexB::ExtractExtraDataForAVCC(aHeader).forget();
  }
  size_t length = aHeader.Length();
  auto annexB = MakeRefPtr<MediaByteBuffer>(length);
  PodCopy(annexB->Elements(), aHeader.Elements(), length);
  annexB->SetLength(length);
  return annexB.forget();
}

static uint32_t GetProfile(H264_PROFILE aProfileLevel) {
  switch (aProfileLevel) {
    case H264_PROFILE_BASE:
      return eAVEncH264VProfile_Base;
    case H264_PROFILE_MAIN:
      return eAVEncH264VProfile_Main;
    case H264_PROFILE_HIGH:
      return eAVEncH264VProfile_High;
    default:
      return eAVEncH264VProfile_unknown;
  }
}

already_AddRefed<IMFMediaType> CreateInputType(EncoderConfig& aConfig) {
  RefPtr<IMFMediaType> type;
  HRESULT hr = wmf::MFCreateMediaType(getter_AddRefs(type));
  if (FAILED(hr)) {
    WMF_ENC_LOG("MFCreateMediaType (input) error: {:x}", hr);
    return nullptr;
  }
  hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (FAILED(hr)) {
    WMF_ENC_LOG("Create input type: SetGUID (major type) error: {:x}", hr);
    return nullptr;
  }
  // Always NV12 input
  hr = type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
  if (FAILED(hr)) {
    WMF_ENC_LOG("Create input type: SetGUID (subtype) error: {:x}", hr);
    return nullptr;
  }
  hr = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  if (FAILED(hr)) {
    WMF_ENC_LOG("Create input type: interlace mode (input) error: {:x}", hr);
    return nullptr;
  }
  // WMF requires a framerate to intialize properly. Provide
  // something reasonnable if not provided.
  if (!aConfig.mFramerate) {
    aConfig.mFramerate = 30;
  }
  if (aConfig.mFramerate) {
    hr = MFSetAttributeRatio(type, MF_MT_FRAME_RATE, aConfig.mFramerate, 1);
    if (FAILED(hr)) {
      WMF_ENC_LOG("Create input type: frame rate (input) error: {:x}", hr);
      return nullptr;
    }
  }
  hr = MFSetAttributeSize(type, MF_MT_FRAME_SIZE, aConfig.mSize.width,
                          aConfig.mSize.height);
  if (FAILED(hr)) {
    WMF_ENC_LOG("Create input type: frame size (input) error: {:x}", hr);
    return nullptr;
  }
  return type.forget();
}

already_AddRefed<IMFMediaType> CreateOutputType(EncoderConfig& aConfig) {
  RefPtr<IMFMediaType> type;
  HRESULT hr = wmf::MFCreateMediaType(getter_AddRefs(type));
  if (FAILED(hr)) {
    WMF_ENC_LOG("MFCreateMediaType (output) error: {:x}", hr);
    return nullptr;
  }
  hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (FAILED(hr)) {
    WMF_ENC_LOG("Create output type: set major type error: {:x}", hr);
    return nullptr;
  }
  hr = type->SetGUID(MF_MT_SUBTYPE, CodecToSubtype(aConfig.mCodec));
  if (FAILED(hr)) {
    WMF_ENC_LOG("Create output type: set subtype error: {:x}", hr);
    return nullptr;
  }
  // A bitrate need to be set here, attempt to make an educated
  // guess if none is provided. This could be per codec to have
  // nicer defaults.
  size_t longDimension = std::max(aConfig.mSize.width, aConfig.mSize.height);
  if (!aConfig.mBitrate) {
    if (longDimension < 720) {
      aConfig.mBitrate = 2000000;
    } else if (longDimension < 1080) {
      aConfig.mBitrate = 4000000;
    } else {
      aConfig.mBitrate = 8000000;
    }
  }
  // No way to set variable / constant here.
  hr = type->SetUINT32(MF_MT_AVG_BITRATE, aConfig.mBitrate);
  if (FAILED(hr)) {
    WMF_ENC_LOG("Create output type: set bitrate error: {:x}", hr);
    return nullptr;
  }
  hr = type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  if (FAILED(hr)) {
    WMF_ENC_LOG("Create output type set interlave mode error: {:x}", hr);
    return nullptr;
  }
  // A positive rate must always be preset here, see the Input
  // config part.
  MOZ_ASSERT(aConfig.mFramerate);
  if (aConfig.mFramerate) {
    hr = MFSetAttributeRatio(type, MF_MT_FRAME_RATE, aConfig.mFramerate, 1);
    if (FAILED(hr)) {
      WMF_ENC_LOG("Create output type set frame rate error: {:x}", hr);
      return nullptr;
    }
  }
  // Required
  hr = MFSetAttributeSize(type, MF_MT_FRAME_SIZE, aConfig.mSize.width,
                          aConfig.mSize.height);
  if (FAILED(hr)) {
    WMF_ENC_LOG("Create output type set frame size error: {:x}", hr);
    return nullptr;
  }

  if (aConfig.mCodecSpecific.is<H264Specific>()) {
    MOZ_ASSERT(aConfig.mCodec == CodecType::H264);
    hr = type->SetUINT32(
        MF_MT_MPEG2_PROFILE,
        GetProfile(aConfig.mCodecSpecific.as<H264Specific>().mProfile));
    if (FAILED(hr)) {
      WMF_ENC_LOG("Create output type set profile error: {:x}", hr);
      return nullptr;
    }
  }

  // Set keyframe distance through both media type and codec API
  // for better compatibility. Some encoders may only support one
  // of these methods. `AVEncVideoMaxKeyframeDistance` is set in
  // `MFTEncoder::SetModes`.
  uint32_t interval = SaturatingCast<uint32_t>(aConfig.mKeyframeInterval);
  if (interval > 0) {
    hr = type->SetUINT32(MF_MT_MAX_KEYFRAME_SPACING, interval);
    if (FAILED(hr)) {
      WMF_ENC_LOG("Create output type set keyframe interval error: {:x}", hr);
      return nullptr;
    }
    WMF_ENC_LOG("Set MAX_KEYFRAME_SPACING to {}", interval);
  }

  return type.forget();
}

HRESULT SetMediaTypes(RefPtr<MFTEncoder>& aEncoder, EncoderConfig& aConfig) {
  RefPtr<IMFMediaType> inputType = CreateInputType(aConfig);
  if (!inputType) {
    return E_FAIL;
  }

  RefPtr<IMFMediaType> outputType = CreateOutputType(aConfig);
  if (!outputType) {
    return E_FAIL;
  }

  return aEncoder->SetMediaTypes(inputType, outputType);
}

}  // namespace mozilla
