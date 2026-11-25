/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidEncoderModule.h"

#include "AndroidDataEncoder.h"
#include "mozilla/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/java/HardwareCodecCapabilityUtilsWrappers.h"

using mozilla::media::EncodeSupport;
using mozilla::media::EncodeSupportSet;

namespace mozilla {
extern LazyLogModule sPEMLog;
#define AND_PEM_LOG(arg, ...)                                                 \
  MOZ_LOG_FMT(sPEMLog, mozilla::LogLevel::Debug,                              \
              "AndroidEncoderModule({})::{}: " arg, fmt::ptr(this), __func__, \
              ##__VA_ARGS__)

EncodeSupportSet AndroidEncoderModule::SupportsCodec(CodecType aCodec) const {
  EncodeSupportSet supports{};
  switch (aCodec) {
    case CodecType::H264:
      supports += EncodeSupport::SoftwareEncode;
      if (gfx::gfxVars::UseH264HwEncode()) {
        supports += EncodeSupport::HardwareEncode;
      }
      break;
    case CodecType::VP8:
      if (gfx::gfxVars::UseVP8HwEncode()) {
        supports += EncodeSupport::HardwareEncode;
      }
      break;
    case CodecType::VP9:
      if (gfx::gfxVars::UseVP9HwEncode()) {
        supports += EncodeSupport::HardwareEncode;
      }
      break;
    default:
      break;
  }
  return supports;
}

Maybe<uint32_t> AndroidEncoderModule::MaxPixelsPerFrame(
    CodecType aCodec) const {
  int32_t pixels = java::HardwareCodecCapabilityUtils::GetMaxEncoderPixels(
      nsDependentCString(AndroidEncoderMimeType(aCodec)));
  if (pixels <= 0) {
    return Nothing();
  }
  AND_PEM_LOG("Codec {} max pixels per frame: {}", static_cast<int>(aCodec),
              pixels);
  return Some(static_cast<uint32_t>(pixels));
}

EncodeSupportSet AndroidEncoderModule::Supports(
    const EncoderConfig& aConfig) const {
  if (!CanLikelyEncode(aConfig)) {
    return EncodeSupportSet{};
  }
  if (aConfig.mScalabilityMode != ScalabilityMode::None) {
    return EncodeSupportSet{};
  }
  // Only hardware encoder are supported for now.
  return SupportsCodec(aConfig.mCodec);
}

already_AddRefed<MediaDataEncoder> AndroidEncoderModule::CreateVideoEncoder(
    const EncoderConfig& aConfig, const RefPtr<TaskQueue>& aTaskQueue) const {
  if (Supports(aConfig).isEmpty()) {
    AND_PEM_LOG("Unsupported codec type: {}",
                EnumValueToString(aConfig.mCodec));
    return nullptr;
  }
  return MakeRefPtr<AndroidDataEncoder>(aConfig, aTaskQueue).forget();
}

}  // namespace mozilla

#undef AND_PEM_LOG
