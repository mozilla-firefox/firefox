/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/codecs/vp9/include/vp9.h"

#include <memory>

#include "absl/container/inlined_vector.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/vp9_profile.h"
#include "modules/video_coding/codecs/vp9/libvpx_vp9_decoder.h"
#include "modules/video_coding/codecs/vp9/libvpx_vp9_encoder.h"
#include "modules/video_coding/svc/create_scalability_structure.h"
#include "rtc_base/checks.h"
#include "vpx/vp8cx.h"
#include "vpx/vp8dx.h"
#include "vpx/vpx_codec.h"

namespace webrtc {

std::vector<SdpVideoFormat> SupportedVP9Codecs(bool add_scalability_modes) {
#ifdef RTC_ENABLE_VP9
  // Profile 2 might not be available on some platforms until
  // https://bugs.chromium.org/p/webm/issues/detail?id=1544 is solved.
  static bool vpx_supports_high_bit_depth =
      (vpx_codec_get_caps(vpx_codec_vp9_cx()) & VPX_CODEC_CAP_HIGHBITDEPTH) !=
          0 &&
      (vpx_codec_get_caps(vpx_codec_vp9_dx()) & VPX_CODEC_CAP_HIGHBITDEPTH) !=
          0;

  absl::InlinedVector<ScalabilityMode, kScalabilityModeCount> scalability_modes;
  if (add_scalability_modes) {
    for (const auto scalability_mode : kAllScalabilityModes) {
      if (ScalabilityStructureConfig(scalability_mode).has_value()) {
        scalability_modes.push_back(scalability_mode);
      }
    }
  }
  std::vector<SdpVideoFormat> supported_formats{
      SdpVideoFormat(SdpVideoFormat::VP9Profile0(), scalability_modes)};
  if (vpx_supports_high_bit_depth) {
    supported_formats.push_back(
        SdpVideoFormat(SdpVideoFormat::VP9Profile2(), scalability_modes));
  }

  return supported_formats;
#else
  return std::vector<SdpVideoFormat>();
#endif
}

std::vector<SdpVideoFormat> SupportedVP9DecoderCodecs() {
#ifdef RTC_ENABLE_VP9
  std::vector<SdpVideoFormat> supported_formats = SupportedVP9Codecs();
  // The WebRTC internal decoder supports VP9 profile 1 and 3. However, there's
  // currently no way of sending VP9 profile 1 or 3 using the internal encoder.
  // It would require extended support for I444, I422, and I440 buffers.
  supported_formats.push_back(SdpVideoFormat::VP9Profile1());
  supported_formats.push_back(SdpVideoFormat::VP9Profile3());
  return supported_formats;
#else
  return std::vector<SdpVideoFormat>();
#endif
}

absl::Nonnull<std::unique_ptr<VideoEncoder>> CreateVp9Encoder(
    const Environment& env,
    Vp9EncoderSettings settings) {
#ifdef RTC_ENABLE_VP9
  return std::make_unique<LibvpxVp9Encoder>(env, settings,
                                            LibvpxInterface::Create());
#else
  RTC_CHECK_NOTREACHED();
#endif
}

bool VP9Encoder::SupportsScalabilityMode(ScalabilityMode scalability_mode) {
  return ScalabilityStructureConfig(scalability_mode).has_value();
}

std::unique_ptr<VP9Decoder> VP9Decoder::Create() {
#ifdef RTC_ENABLE_VP9
  return std::make_unique<LibvpxVp9Decoder>();
#else
  RTC_DCHECK_NOTREACHED();
  return nullptr;
#endif
}

}  // namespace webrtc
