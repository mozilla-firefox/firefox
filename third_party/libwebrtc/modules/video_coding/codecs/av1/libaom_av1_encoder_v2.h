/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_VIDEO_CODING_CODECS_AV1_LIBAOM_AV1_ENCODER_V2_H_
#define MODULES_VIDEO_CODING_CODECS_AV1_LIBAOM_AV1_ENCODER_V2_H_

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "api/scoped_refptr.h"
#include "api/video/resolution.h"
#include "api/video/video_codec_constants.h"
#include "api/video/video_frame_buffer.h"
#include "api/video_codecs/video_encoder_factory_interface.h"
#include "api/video_codecs/video_encoder_interface.h"
#include "modules/video_coding/utility/cbr_layer_rate_tracker.h"
#include "rtc_base/numerics/rational.h"
#include "third_party/libaom/source/libaom/aom/aom_codec.h"
#include "third_party/libaom/source/libaom/aom/aom_encoder.h"
#include "third_party/libaom/source/libaom/aom/aom_image.h"
#include "third_party/libaom/source/libaom/aom/aomcx.h"

namespace webrtc {

class LibaomAv1EncoderV2 : public VideoEncoderInterface {
 public:
  static VideoEncoderFactoryInterface::Capabilities GetCapabilities();

  LibaomAv1EncoderV2() = default;
  ~LibaomAv1EncoderV2() override;

  bool InitEncode(
      const VideoEncoderFactoryInterface::StaticEncoderSettings& settings,
      const std::map<std::string, std::string>& encoder_specific_settings);

  void Encode(scoped_refptr<VideoFrameBuffer> frame_buffer,
              const TemporalUnitSettings& tu_settings,
              std::vector<FrameEncodeSettings> frame_settings) override;

 private:
  static constexpr int kNumBuffers = 8;
  using aom_img_ptr = std::unique_ptr<aom_image_t, decltype(&aom_img_free)>;

  aom_svc_params_t GetSvcParams(
      const VideoFrameBuffer& frame_buffer,
      const std::vector<FrameEncodeSettings>& frame_settings) const;

  aom_img_ptr image_to_encode_ = aom_img_ptr(nullptr, aom_img_free);
  aom_codec_ctx_t ctx_{};
  aom_codec_enc_cfg_t cfg_{};
  // The configuration and SVC parameters last handed to libaom. Reapplying
  // them has side effects even when nothing changed, e.g. the rate control of
  // layers running at a lower frame rate than the last encoded one is reset,
  // so they are only applied when they differ from these.
  std::optional<aom_codec_enc_cfg_t> applied_cfg_;
  std::optional<aom_svc_params_t> applied_svc_params_;

  std::optional<ContentHint> content_type_;
  std::optional<int> effort_level_;
  std::optional<int> cq_level_;
  // Spatial layers declared to libaom: the most seen in a temporal unit since
  // `InitEncode`. Changing `number_spatial_layers` updates the sequence header
  // operating points and makes libaom force a keyframe. We keep the state for
  // layers we have seen so far in order to minimize resets.
  // TODO(bugs.webrtc.org/496266459): The RTP profile for AV1 specified that an
  // implementation may specify a single operating point with a value of 0xFFF,
  // indicating there are no operating points signalled in the bitstream.
  // Consider implementing that behavior in libaom to avoid this workaround.
  int num_spatial_layers_ = 0;
  // Last scaling factor per spatial layer, kept for the temporal units without
  // a frame in the layer.
  std::array<Rational, kMaxSpatialLayers> scaling_factor_by_spatial_id_;
  int max_number_of_threads_ = 0;
  std::array<std::optional<Resolution>, kNumBuffers> last_resolution_in_buffer_;
  // Recreated on every `InitEncode`, since what it has learned only describes
  // the configuration it was fed.
  std::unique_ptr<CbrLayerRateTracker> rate_tracker_;
};

}  // namespace webrtc

#endif  // MODULES_VIDEO_CODING_CODECS_AV1_LIBAOM_AV1_ENCODER_V2_H_
