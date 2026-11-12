/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/video_stream_encoder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/cleanup/cleanup.h"
#include "api/adaptation/resource.h"
#include "api/environment/environment.h"
#include "api/fec_controller_override.h"
#include "api/field_trials_view.h"
#include "api/make_ref_counted.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/rtp_sender_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/corruption_detection/frame_instrumentation_data.h"
#include "api/video/corruption_detection/frame_instrumentation_generator.h"
#include "api/video/corruption_detection/frame_instrumentation_generator_factory.h"
#include "api/video/encoded_image.h"
#include "api/video/render_resolution.h"
#include "api/video/video_adaptation_counters.h"
#include "api/video/video_adaptation_reason.h"
#include "api/video/video_bitrate_allocation.h"
#include "api/video/video_bitrate_allocator.h"
#include "api/video/video_bitrate_allocator_factory.h"
#include "api/video/video_codec_constants.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_layers_allocation.h"
#include "api/video/video_source_interface.h"
#include "api/video/video_stream_encoder_settings.h"
#include "api/video/video_timing.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "call/adaptation/degradation_preference_provider.h"
#include "call/adaptation/encoder_settings.h"
#include "call/adaptation/resource_adaptation_processor.h"
#include "call/adaptation/video_source_restrictions.h"
#include "call/adaptation/video_stream_adapter.h"
#include "media/base/media_channel.h"
#include "modules/video_coding/codecs/interface/common_constants.h"
#include "modules/video_coding/include/video_codec_initializer.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/svc/scalability_mode_util.h"
#include "modules/video_coding/svc/svc_rate_allocator.h"
#include "rtc_base/checks.h"
#include "rtc_base/event.h"
#include "rtc_base/experiments/encoder_info_settings.h"
#include "rtc_base/experiments/field_trial_parser.h"
#include "rtc_base/experiments/rate_control_settings.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/system/no_unique_address.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/trace_event.h"
#include "video/adaptation/overuse_frame_detector.h"
#include "video/adaptation/video_stream_encoder_resource_manager.h"
#include "video/alignment_adjuster.h"
#include "video/config/encoder_stream_factory.h"
#include "video/config/video_encoder_config.h"
#include "video/encoder_bitrate_adjuster.h"
#include "video/frame_cadence_adapter.h"
#include "video/frame_dumping_encoder.h"
#include "video/video_stream_encoder_observer.h"

namespace webrtc {

namespace {

// Time interval for logging frame counts.
const int64_t kFrameLogIntervalMs = 60000;

// Time to keep a single cached pending frame in paused state.
const int64_t kPendingFrameTimeoutMs = 1000;

constexpr char kFrameDropperFieldTrial[] = "WebRTC-FrameDropper";

// TODO(bugs.webrtc.org/13572): Remove this kill switch after deploying the
// feature.
constexpr char kSwitchEncoderOnInitializationFailuresFieldTrial[] =
    "WebRTC-SwitchEncoderOnInitializationFailures";

// TODO(crbugs.com/378566918): Remove this kill switch after rollout.
constexpr char kSwitchEncoderFollowCodecPreferenceOrderFieldTrial[] =
    "WebRTC-SwitchEncoderFollowCodecPreferenceOrder";

const size_t kDefaultPayloadSize = 1440;

const int64_t kParameterUpdateIntervalMs = 1000;

constexpr int kDefaultMinScreenSharebps = 1200000;

constexpr int kMaxFramesInPreparation = 10;

int GetNumSpatialLayers(const VideoCodec& codec) {
  if (codec.codecType == kVideoCodecVP9) {
    return codec.VP9().numberOfSpatialLayers;
  } else if (codec.codecType == kVideoCodecAV1 &&
             codec.GetScalabilityMode().has_value()) {
    return ScalabilityModeToNumSpatialLayers(*(codec.GetScalabilityMode()));
  } else if (codec.codecType == kVideoCodecH265) {
    // No spatial scalability support for H.265.
    return 1;
  } else {
    return 0;
  }
}

bool RequiresEncoderReset(const VideoCodec& prev_send_codec,
                          const VideoCodec& new_send_codec,
                          bool was_encode_called_since_last_initialization) {
  // Does not check max/minBitrate or maxFramerate.
  if (new_send_codec.codecType != prev_send_codec.codecType ||
      new_send_codec.width != prev_send_codec.width ||
      new_send_codec.height != prev_send_codec.height ||
      new_send_codec.qpMax != prev_send_codec.qpMax ||
      new_send_codec.numberOfSimulcastStreams !=
          prev_send_codec.numberOfSimulcastStreams ||
      new_send_codec.mode != prev_send_codec.mode ||
      new_send_codec.GetFrameDropEnabled() !=
          prev_send_codec.GetFrameDropEnabled()) {
    return true;
  }

  if (!was_encode_called_since_last_initialization &&
      (new_send_codec.startBitrate != prev_send_codec.startBitrate)) {
    // If start bitrate has changed reconfigure encoder only if encoding had not
    // yet started.
    return true;
  }

  switch (new_send_codec.codecType) {
    case kVideoCodecVP8:
      if (new_send_codec.VP8() != prev_send_codec.VP8()) {
        return true;
      }
      break;

    case kVideoCodecVP9:
      if (new_send_codec.VP9() != prev_send_codec.VP9()) {
        return true;
      }
      break;

    case kVideoCodecH264:
      if (new_send_codec.H264() != prev_send_codec.H264()) {
        return true;
      }
      break;
    case kVideoCodecH265:
      // No H.265 specific handling needed.
      [[fallthrough]];
    default:
      break;
  }

  for (unsigned char i = 0; i < new_send_codec.numberOfSimulcastStreams; ++i) {
    if (!new_send_codec.simulcastStream[i].active) {
      // No need to reset when stream is inactive.
      continue;
    }

    if (!prev_send_codec.simulcastStream[i].active ||
        new_send_codec.simulcastStream[i].width !=
            prev_send_codec.simulcastStream[i].width ||
        new_send_codec.simulcastStream[i].height !=
            prev_send_codec.simulcastStream[i].height ||
        new_send_codec.simulcastStream[i].numberOfTemporalLayers !=
            prev_send_codec.simulcastStream[i].numberOfTemporalLayers ||
        new_send_codec.simulcastStream[i].qpMax !=
            prev_send_codec.simulcastStream[i].qpMax) {
      return true;
    }

    if (new_send_codec.simulcastStream[i].maxFramerate !=
            prev_send_codec.simulcastStream[i].maxFramerate &&
        new_send_codec.simulcastStream[i].maxFramerate !=
            new_send_codec.maxFramerate) {
      // SetRates can only represent maxFramerate for one layer. Reset the
      // encoder if there are multiple layers that differ in maxFramerate.
      return true;
    }
  }

  if (new_send_codec.codecType == kVideoCodecVP9) {
    size_t num_spatial_layers = new_send_codec.VP9().numberOfSpatialLayers;
    for (unsigned char i = 0; i < num_spatial_layers; ++i) {
      if (!new_send_codec.spatialLayers[i].active) {
        // No need to reset when layer is inactive.
        continue;
      }
      if (new_send_codec.spatialLayers[i].width !=
              prev_send_codec.spatialLayers[i].width ||
          new_send_codec.spatialLayers[i].height !=
              prev_send_codec.spatialLayers[i].height ||
          new_send_codec.spatialLayers[i].numberOfTemporalLayers !=
              prev_send_codec.spatialLayers[i].numberOfTemporalLayers ||
          new_send_codec.spatialLayers[i].qpMax !=
              prev_send_codec.spatialLayers[i].qpMax ||
          !prev_send_codec.spatialLayers[i].active) {
        return true;
      }
    }
  }

  if (new_send_codec.GetScalabilityMode() !=
      prev_send_codec.GetScalabilityMode()) {
    return true;
  }

  return false;
}

// Converts a VideoBitrateAllocation that contains allocated bitrate per layer,
// and an EncoderInfo that contains information about the actual encoder
// structure used by a codec. Stream structures can be Ksvc, Full SVC, Simulcast
// etc.
VideoLayersAllocation CreateVideoLayersAllocation(
    const VideoCodec& encoder_config,
    const VideoEncoder::RateControlParameters& current_rate,
    const VideoEncoder::EncoderInfo& encoder_info) {
  const VideoBitrateAllocation& target_bitrate = current_rate.target_bitrate;
  VideoLayersAllocation layers_allocation;
  if (target_bitrate.get_sum_bps() == 0) {
    return layers_allocation;
  }

  if (encoder_config.numberOfSimulcastStreams > 1) {
    layers_allocation.resolution_and_frame_rate_is_valid = true;
    for (int si = 0; si < encoder_config.numberOfSimulcastStreams; ++si) {
      if (!target_bitrate.IsSpatialLayerUsed(si) ||
          target_bitrate.GetSpatialLayerSum(si) == 0) {
        continue;
      }
      layers_allocation.active_spatial_layers.emplace_back();
      VideoLayersAllocation::SpatialLayer& spatial_layer =
          layers_allocation.active_spatial_layers.back();
      spatial_layer.width = encoder_config.simulcastStream[si].width;
      spatial_layer.height = encoder_config.simulcastStream[si].height;
      spatial_layer.rtp_stream_index = si;
      spatial_layer.spatial_id = 0;
      auto frame_rate_fraction =
          VideoEncoder::EncoderInfo::kMaxFramerateFraction;
      if (encoder_info.fps_allocation[si].size() == 1) {
        // One TL is signalled to be used by the encoder. Do not distribute
        // bitrate allocation across TLs (use sum at tl:0).
        spatial_layer.target_bitrate_per_temporal_layer.push_back(
            DataRate::BitsPerSec(target_bitrate.GetSpatialLayerSum(si)));
        frame_rate_fraction = encoder_info.fps_allocation[si][0];
      } else {  // Temporal layers are supported.
        uint32_t temporal_layer_bitrate_bps = 0;
        for (size_t ti = 0;
             ti < encoder_config.simulcastStream[si].numberOfTemporalLayers;
             ++ti) {
          if (!target_bitrate.HasBitrate(si, ti)) {
            break;
          }
          if (ti < encoder_info.fps_allocation[si].size()) {
            // Use frame rate of the top used temporal layer.
            frame_rate_fraction = encoder_info.fps_allocation[si][ti];
          }
          temporal_layer_bitrate_bps += target_bitrate.GetBitrate(si, ti);
          spatial_layer.target_bitrate_per_temporal_layer.push_back(
              DataRate::BitsPerSec(temporal_layer_bitrate_bps));
        }
      }
      // Encoder may drop frames internally if `maxFramerate` is set.
      spatial_layer.frame_rate_fps = std::min<uint8_t>(
          encoder_config.simulcastStream[si].maxFramerate,
          saturated_cast<uint8_t>(
              (current_rate.framerate_fps * frame_rate_fraction) /
              VideoEncoder::EncoderInfo::kMaxFramerateFraction));
    }
  } else if (encoder_config.numberOfSimulcastStreams == 1) {
    // TODO(bugs.webrtc.org/12000): Implement support for AV1 with
    // scalability.
    const bool higher_spatial_depend_on_lower =
        encoder_config.codecType == kVideoCodecVP9 &&
        encoder_config.VP9().interLayerPred == InterLayerPredMode::kOn;
    layers_allocation.resolution_and_frame_rate_is_valid = true;

    std::vector<DataRate> aggregated_spatial_bitrate(kMaxTemporalStreams,
                                                     DataRate::Zero());
    for (size_t si = 0; si < kMaxSpatialLayers; ++si) {
      layers_allocation.resolution_and_frame_rate_is_valid = true;
      if (!target_bitrate.IsSpatialLayerUsed(si) ||
          target_bitrate.GetSpatialLayerSum(si) == 0) {
        break;
      }
      layers_allocation.active_spatial_layers.emplace_back();
      VideoLayersAllocation::SpatialLayer& spatial_layer =
          layers_allocation.active_spatial_layers.back();
      spatial_layer.width = encoder_config.spatialLayers[si].width;
      spatial_layer.height = encoder_config.spatialLayers[si].height;
      spatial_layer.rtp_stream_index = 0;
      spatial_layer.spatial_id = si;
      auto frame_rate_fraction =
          VideoEncoder::EncoderInfo::kMaxFramerateFraction;
      if (encoder_info.fps_allocation[si].size() == 1) {
        // One TL is signalled to be used by the encoder. Do not distribute
        // bitrate allocation across TLs (use sum at tl:0).
        DataRate aggregated_temporal_bitrate =
            DataRate::BitsPerSec(target_bitrate.GetSpatialLayerSum(si));
        aggregated_spatial_bitrate[0] += aggregated_temporal_bitrate;
        if (higher_spatial_depend_on_lower) {
          spatial_layer.target_bitrate_per_temporal_layer.push_back(
              aggregated_spatial_bitrate[0]);
        } else {
          spatial_layer.target_bitrate_per_temporal_layer.push_back(
              aggregated_temporal_bitrate);
        }
        frame_rate_fraction = encoder_info.fps_allocation[si][0];
      } else {  // Temporal layers are supported.
        DataRate aggregated_temporal_bitrate = DataRate::Zero();
        for (size_t ti = 0;
             ti < encoder_config.spatialLayers[si].numberOfTemporalLayers;
             ++ti) {
          if (!target_bitrate.HasBitrate(si, ti)) {
            break;
          }
          if (ti < encoder_info.fps_allocation[si].size()) {
            // Use frame rate of the top used temporal layer.
            frame_rate_fraction = encoder_info.fps_allocation[si][ti];
          }
          aggregated_temporal_bitrate +=
              DataRate::BitsPerSec(target_bitrate.GetBitrate(si, ti));
          if (higher_spatial_depend_on_lower) {
            spatial_layer.target_bitrate_per_temporal_layer.push_back(
                aggregated_temporal_bitrate + aggregated_spatial_bitrate[ti]);
            aggregated_spatial_bitrate[ti] += aggregated_temporal_bitrate;
          } else {
            spatial_layer.target_bitrate_per_temporal_layer.push_back(
                aggregated_temporal_bitrate);
          }
        }
      }
      // Encoder may drop frames internally if `maxFramerate` is set.
      spatial_layer.frame_rate_fps = std::min<uint8_t>(
          encoder_config.spatialLayers[si].maxFramerate,
          saturated_cast<uint8_t>(
              (current_rate.framerate_fps * frame_rate_fraction) /
              VideoEncoder::EncoderInfo::kMaxFramerateFraction));
    }
  }

  return layers_allocation;
}

VideoEncoder::EncoderInfo GetEncoderInfoWithBitrateLimitUpdate(
    VideoEncoder::EncoderInfo info,
    const VideoEncoderConfig& encoder_config) {
  bool are_all_bitrate_limits_zero = true;
  // Hardware encoders commonly only report resolution limits, while reporting
  // the bitrate limits as 0. In such case, we should not use them for setting
  // bitrate limits.
  if (!info.resolution_bitrate_limits.empty()) {
    are_all_bitrate_limits_zero = std::all_of(
        info.resolution_bitrate_limits.begin(),
        info.resolution_bitrate_limits.end(),
        [](const VideoEncoder::ResolutionBitrateLimits& limit) {
          return limit.max_bitrate_bps == 0 && limit.min_bitrate_bps == 0;
        });
  }

  if (!are_all_bitrate_limits_zero ||
      encoder_config.simulcast_layers.size() <= 1) {
    return info;
  }

  // Bitrate limits are not configured and more than one layer is used, use
  // the default limits (bitrate limits are not used for simulcast).
  info.resolution_bitrate_limits =
      EncoderInfoSettings::GetDefaultSinglecastBitrateLimits(
          encoder_config.codec_type);
  return info;
}

int NumActiveStreams(const std::vector<VideoStream>& streams) {
  int num_active = 0;
  for (const auto& stream : streams) {
    if (stream.active)
      ++num_active;
  }
  return num_active;
}

void ApplySpatialLayerBitrateLimits(
    const VideoEncoder::EncoderInfo& encoder_info,
    const VideoEncoderConfig& encoder_config,
    VideoCodec* codec) {
  if (!(GetNumSpatialLayers(*codec) > 0)) {
    // ApplySpatialLayerBitrateLimits() supports VP9 and AV1 (the latter with
    // scalability mode set) only.
    return;
  }
  if (VideoStreamEncoderResourceManager::IsSimulcastOrMultipleSpatialLayers(
          encoder_config, *codec) ||
      encoder_config.simulcast_layers.size() <= 1) {
    // Resolution bitrate limits usage is restricted to singlecast.
    return;
  }

  // Get bitrate limits for active stream.
  std::optional<uint32_t> pixels =
      VideoStreamAdapter::GetSingleActiveLayerPixels(*codec);
  if (!pixels.has_value()) {
    return;
  }
  std::optional<VideoEncoder::ResolutionBitrateLimits> bitrate_limits =
      encoder_info.GetEncoderBitrateLimitsForResolution(*pixels);
  if (!bitrate_limits.has_value()) {
    return;
  }
  // Index for the active stream.
  std::optional<size_t> index;
  for (size_t i = 0; i < encoder_config.simulcast_layers.size(); ++i) {
    if (encoder_config.simulcast_layers[i].active)
      index = i;
  }
  if (!index.has_value()) {
    return;
  }
  int min_bitrate_bps;
  if (encoder_config.simulcast_layers[*index].min_bitrate_bps <= 0) {
    min_bitrate_bps = bitrate_limits->min_bitrate_bps;
  } else {
    min_bitrate_bps = encoder_config.simulcast_layers[*index].min_bitrate_bps;
  }
  int max_bitrate_bps;
  if (encoder_config.simulcast_layers[*index].max_bitrate_bps <= 0) {
    max_bitrate_bps = bitrate_limits->max_bitrate_bps;
  } else {
    max_bitrate_bps = encoder_config.simulcast_layers[*index].max_bitrate_bps;
  }

  if (encoder_config.simulcast_layers[*index].min_bitrate_bps > 0) {
    // Ensure max is not below configured min.
    max_bitrate_bps = std::max(min_bitrate_bps, max_bitrate_bps);
  } else {
    // Ensure min is not above max.
    min_bitrate_bps = std::min(min_bitrate_bps, max_bitrate_bps);
  }

  for (int i = 0; i < GetNumSpatialLayers(*codec); ++i) {
    if (codec->spatialLayers[i].active) {
      codec->spatialLayers[i].minBitrate = min_bitrate_bps / 1000;
      codec->spatialLayers[i].maxBitrate = max_bitrate_bps / 1000;
      codec->spatialLayers[i].targetBitrate =
          std::clamp(codec->spatialLayers[i].targetBitrate,
                     codec->spatialLayers[i].minBitrate,
                     codec->spatialLayers[i].maxBitrate);
      break;
    }
  }
}

void ApplyEncoderBitrateLimitsIfSingleActiveStream(
    const VideoEncoder::EncoderInfo& encoder_info,
    const std::vector<VideoStream>& encoder_config_layers,
    std::vector<VideoStream>* streams) {
  // Apply limits if simulcast with one active stream (expect lowest).
  bool single_active_stream =
      streams->size() > 1 && NumActiveStreams(*streams) == 1 &&
      !streams->front().active && NumActiveStreams(encoder_config_layers) == 1;
  if (!single_active_stream) {
    return;
  }

  // Index for the active stream.
  size_t index = 0;
  for (size_t i = 0; i < encoder_config_layers.size(); ++i) {
    if (encoder_config_layers[i].active)
      index = i;
  }
  if (streams->size() < (index + 1) || !(*streams)[index].active) {
    return;
  }

  // Get bitrate limits for active stream.
  std::optional<VideoEncoder::ResolutionBitrateLimits> encoder_bitrate_limits =
      encoder_info.GetEncoderBitrateLimitsForResolution(
          (*streams)[index].width * (*streams)[index].height);
  if (!encoder_bitrate_limits) {
    return;
  }

  int min_bitrate_bps;
  if (encoder_config_layers[index].min_bitrate_bps <= 0) {
    min_bitrate_bps = encoder_bitrate_limits->min_bitrate_bps;
  } else {
    min_bitrate_bps = (*streams)[index].min_bitrate_bps;
  }
  int max_bitrate_bps;
  if (encoder_config_layers[index].max_bitrate_bps <= 0) {
    max_bitrate_bps = encoder_bitrate_limits->max_bitrate_bps;
  } else {
    max_bitrate_bps = (*streams)[index].max_bitrate_bps;
  }

  if (encoder_config_layers[index].min_bitrate_bps > 0) {
    // Ensure max is not below configured min.
    max_bitrate_bps = std::max(min_bitrate_bps, max_bitrate_bps);
  } else {
    // Ensure min is not above max.
    min_bitrate_bps = std::min(min_bitrate_bps, max_bitrate_bps);
  }

  (*streams)[index].min_bitrate_bps = min_bitrate_bps;
  (*streams)[index].max_bitrate_bps = max_bitrate_bps;
  (*streams)[index].target_bitrate_bps = std::clamp(
      (*streams)[index].target_bitrate_bps, min_bitrate_bps, max_bitrate_bps);
}

std::optional<int> ParseVp9LowTierCoreCountThreshold(
    const FieldTrialsView& trials) {
  FieldTrialFlag disable_low_tier("Disabled");
  FieldTrialParameter<int> max_core_count("max_core_count", 2);
  ParseFieldTrial({&disable_low_tier, &max_core_count},
                  trials.Lookup("WebRTC-VP9-LowTierOptimizations"));
  if (disable_low_tier.Get()) {
    return std::nullopt;
  }
  return max_core_count.Get();
}

std::optional<int> ParseEncoderThreadLimit(const FieldTrialsView& trials) {
  FieldTrialOptional<int> encoder_thread_limit("encoder_thread_limit");
  ParseFieldTrial({&encoder_thread_limit},
                  trials.Lookup("WebRTC-VideoEncoderSettings"));
  return encoder_thread_limit.GetOptional();
}

}  //  namespace

VideoStreamEncoder::PreparedFramesProcessor::PreparedFramesProcessor(
    VideoStreamEncoder* parent)
    : parent_(parent) {}

void VideoStreamEncoder::PreparedFramesProcessor::StopCallbacks() {
  MutexLock lock(&lock_);
  parent_ = nullptr;
}

void VideoStreamEncoder::PreparedFramesProcessor::OnFramePrepared(
    size_t frame_identifier) {
  MutexLock lock(&lock_);
  if (parent_) {
    parent_->OnFramePrepared(frame_identifier);
  }
}

VideoStreamEncoder::EncoderRateSettings::EncoderRateSettings()
    : rate_control(), encoder_target(DataRate::Zero()) {}

VideoStreamEncoder::EncoderRateSettings::EncoderRateSettings(
    const VideoBitrateAllocation& bitrate,
    double framerate_fps,
    DataRate bandwidth_allocation,
    DataRate encoder_target)
    : rate_control(bitrate, framerate_fps, bandwidth_allocation),
      encoder_target(encoder_target) {}

bool VideoStreamEncoder::EncoderRateSettings::operator==(
    const EncoderRateSettings& rhs) const {
  return rate_control == rhs.rate_control &&
         encoder_target == rhs.encoder_target;
}

bool VideoStreamEncoder::EncoderRateSettings::operator!=(
    const EncoderRateSettings& rhs) const {
  return !(*this == rhs);
}

class VideoStreamEncoder::DegradationPreferenceManager
    : public DegradationPreferenceProvider {
 public:
  explicit DegradationPreferenceManager(
      VideoStreamAdapter* video_stream_adapter)
      : degradation_preference_(
            DegradationPreference::MAINTAIN_FRAMERATE_AND_RESOLUTION),
        is_screenshare_(false),
        effective_degradation_preference_(
            DegradationPreference::MAINTAIN_FRAMERATE_AND_RESOLUTION),
        video_stream_adapter_(video_stream_adapter) {
    RTC_DCHECK(video_stream_adapter_);
    sequence_checker_.Detach();
  }

  ~DegradationPreferenceManager() override = default;

  DegradationPreference degradation_preference() const override {
    RTC_DCHECK_RUN_ON(&sequence_checker_);
    return effective_degradation_preference_;
  }

  void SetDegradationPreference(DegradationPreference degradation_preference) {
    RTC_DCHECK_RUN_ON(&sequence_checker_);
    degradation_preference_ = degradation_preference;
    MaybeUpdateEffectiveDegradationPreference();
  }

  void SetIsScreenshare(bool is_screenshare) {
    RTC_DCHECK_RUN_ON(&sequence_checker_);
    is_screenshare_ = is_screenshare;
    MaybeUpdateEffectiveDegradationPreference();
  }

 private:
  void MaybeUpdateEffectiveDegradationPreference()
      RTC_RUN_ON(&sequence_checker_) {
    DegradationPreference effective_degradation_preference =
        (is_screenshare_ &&
         degradation_preference_ == DegradationPreference::BALANCED)
            ? DegradationPreference::MAINTAIN_RESOLUTION
            : degradation_preference_;

    if (effective_degradation_preference != effective_degradation_preference_) {
      effective_degradation_preference_ = effective_degradation_preference;
      video_stream_adapter_->SetDegradationPreference(
          effective_degradation_preference);
    }
  }

  RTC_NO_UNIQUE_ADDRESS SequenceChecker sequence_checker_;
  DegradationPreference degradation_preference_
      RTC_GUARDED_BY(&sequence_checker_);
  bool is_screenshare_ RTC_GUARDED_BY(&sequence_checker_);
  DegradationPreference effective_degradation_preference_
      RTC_GUARDED_BY(&sequence_checker_);
  VideoStreamAdapter* video_stream_adapter_ RTC_GUARDED_BY(&sequence_checker_);
};

VideoStreamEncoder::VideoStreamEncoder(
    const Environment& env,
    uint32_t number_of_cores,
    VideoStreamEncoderObserver* encoder_stats_observer,
    VideoStreamEncoderSettings settings,
    std::unique_ptr<OveruseFrameDetector> overuse_detector,
    std::unique_ptr<FrameCadenceAdapterInterface> frame_cadence_adapter,
    std::unique_ptr<TaskQueueBase, TaskQueueDeleter> encoder_queue,
    BitrateAllocationCallbackType allocation_cb_type,
    scoped_refptr<VideoEncoderFactory::EncoderSelectorInterface>
        encoder_selector,
    EncoderSwitchRequestCallback encoder_switch_request_callback,
    AdaptationInjectionsForTest adaptation_injections_for_test)
    : env_(env),
      worker_queue_(TaskQueueBase::Current()),
      number_of_cores_(number_of_cores),
      settings_(std::move(settings)),
      encoder_switch_request_callback_(
          std::move(encoder_switch_request_callback)),
      allocation_cb_type_(allocation_cb_type),
      rate_control_settings_(env_.field_trials()),
      encoder_selector_(
          encoder_selector != nullptr ? std::move(encoder_selector)
          : settings_.encoder_factory != nullptr
              ? scoped_refptr<VideoEncoderFactory::EncoderSelectorInterface>(
                    settings_.encoder_factory->GetEncoderSelector().release())
              : nullptr),
      encoder_stats_observer_(encoder_stats_observer),
      frame_cadence_adapter_(std::move(frame_cadence_adapter)),
      delta_ntp_internal_ms_(env_.clock().CurrentNtpInMilliseconds() -
                             env_.clock().TimeInMilliseconds()),
      last_frame_log_ms_(env_.clock().TimeInMilliseconds()),
      next_frame_types_(1, VideoFrameType::kVideoFrameDelta),
      input_state_provider_(encoder_stats_observer),
      video_stream_adapter_(
          std::make_unique<VideoStreamAdapter>(&input_state_provider_,
                                               encoder_stats_observer,
                                               env_.field_trials())),
      degradation_preference_manager_(
          std::make_unique<DegradationPreferenceManager>(
              video_stream_adapter_.get())),
      stream_resource_manager_(&input_state_provider_,
                               encoder_stats_observer,
                               &env_.clock(),
                               settings_.experiment_cpu_load_estimator,
                               std::move(overuse_detector),
                               degradation_preference_manager_.get(),
                               env_.field_trials()),
      video_source_sink_controller_(/*sink=*/frame_cadence_adapter_.get(),
                                    /*source=*/nullptr),
      qp_parsing_allowed_(
          !env_.field_trials().IsEnabled("WebRTC-QpParsingKillSwitch")),
      qcm_ignore_enhancement_temporal_layers_(!env_.field_trials().IsDisabled(
          "WebRTC-QCM-IgnoreEnhancementTemporalLayers")),
      switch_encoder_on_init_failures_(!env_.field_trials().IsDisabled(
          kSwitchEncoderOnInitializationFailuresFieldTrial)),
      vp9_low_tier_core_threshold_(
          ParseVp9LowTierCoreCountThreshold(env_.field_trials())),
      experimental_encoder_thread_limit_(
          ParseEncoderThreadLimit(env_.field_trials())),
      speed_experiment_(env_.field_trials(),
                        /* use_low_complexity_for_vp9 = */
                        vp9_low_tier_core_threshold_.has_value()
                            ? number_of_cores_ <= *vp9_low_tier_core_threshold_
                            : false),
      encoder_queue_(std::move(encoder_queue)),
      prepared_frames_processor_(
          make_ref_counted<PreparedFramesProcessor>(this)) {
  TRACE_EVENT0("webrtc", "VideoStreamEncoder::VideoStreamEncoder");
  RTC_DCHECK_RUN_ON(worker_queue_);
  RTC_DCHECK(encoder_stats_observer);
  RTC_DCHECK_GE(number_of_cores, 1);

  frame_cadence_adapter_->Initialize(&cadence_callback_);
  stream_resource_manager_.Initialize(encoder_queue_.get());

  encoder_queue_->PostTask([this, injections = std::move(
                                      adaptation_injections_for_test)] {
    RTC_DCHECK_RUN_ON(encoder_queue_.get());

    resource_adaptation_processor_ =
        std::make_unique<ResourceAdaptationProcessor>(
            video_stream_adapter_.get());

    stream_resource_manager_.SetAdaptationProcessor(
        resource_adaptation_processor_.get(), video_stream_adapter_.get());
    resource_adaptation_processor_->AddResourceLimitationsListener(
        &stream_resource_manager_);
    video_stream_adapter_->AddRestrictionsListener(&stream_resource_manager_);
    video_stream_adapter_->AddRestrictionsListener(this);
    stream_resource_manager_.MaybeInitializePixelLimitResource();

    // Add the stream resource manager's resources to the processor.
    adaptation_constraints_ = stream_resource_manager_.AdaptationConstraints();
    for (auto* constraint : adaptation_constraints_) {
      video_stream_adapter_->AddAdaptationConstraint(constraint);
    }

    // Add any resources and constraints injected by tests.
    for (const auto& [resource, reason] : injections.resources) {
      additional_resources_.push_back(resource);
      stream_resource_manager_.AddResource(resource, reason);
    }
    for (auto* constraint : injections.constraints) {
      adaptation_constraints_.push_back(constraint);
      video_stream_adapter_->AddAdaptationConstraint(constraint);
    }
  });
}

VideoStreamEncoder::~VideoStreamEncoder() {
  RTC_DCHECK_RUN_ON(worker_queue_);
  RTC_DCHECK(is_stopped_) << "Must call ::Stop() before destruction.";

  // The queue must be destroyed before its pointer is invalidated to avoid race
  // between destructor and running task that check if function is called on the
  // encoder_queue_.
  // std::unique_ptr destructor does the same two operations in reverse order as
  // it doesn't expect member would be used after its destruction has started.
  encoder_queue_.get_deleter()(encoder_queue_.get());
  encoder_queue_.release();
}

void VideoStreamEncoder::Stop() {
  RTC_DCHECK_RUN_ON(worker_queue_);
  video_source_sink_controller_.SetSource(nullptr);
  is_stopped_ = true;

  // `StopCallbacks` must be called before the queue is destroyed, because
  // ongoing notifications of prepared frames may post tasks or run on
  // `encoder_queue_`.
  prepared_frames_processor_->StopCallbacks();

  Event shutdown_event;
  absl::Cleanup shutdown = [&shutdown_event] { shutdown_event.Set(); };
  encoder_queue_->PostTask([this, shutdown = std::move(shutdown)] {
    RTC_DCHECK_RUN_ON(encoder_queue_.get());
    if (resource_adaptation_processor_) {
      // We're no longer interested in restriction updates, which may get
      // triggered as part of removing resources.
      video_stream_adapter_->RemoveRestrictionsListener(this);
      video_stream_adapter_->RemoveRestrictionsListener(
          &stream_resource_manager_);
      resource_adaptation_processor_->RemoveResourceLimitationsListener(
          &stream_resource_manager_);
      // Stop and remove resources and delete adaptation processor.
      stream_resource_manager_.StopManagedResources();
      for (auto* constraint : adaptation_constraints_) {
        video_stream_adapter_->RemoveAdaptationConstraint(constraint);
      }
      for (auto& resource : additional_resources_) {
        stream_resource_manager_.RemoveResource(resource);
      }
      additional_resources_.clear();
      stream_resource_manager_.SetAdaptationProcessor(nullptr, nullptr);
      resource_adaptation_processor_.reset();
    }
    rate_allocator_ = nullptr;
    ReleaseEncoder();
    encoder_ = nullptr;
    frame_cadence_adapter_ = nullptr;
    frame_instrumentation_generator_ = nullptr;
  });
  shutdown_event.Wait(Event::kForever);
}

void VideoStreamEncoder::SetFecControllerOverride(
    FecControllerOverride* fec_controller_override) {
  encoder_queue_->PostTask([this, fec_controller_override] {
    RTC_DCHECK_RUN_ON(encoder_queue_.get());
    RTC_DCHECK(!fec_controller_override_);
    fec_controller_override_ = fec_controller_override;
    if (encoder_) {
      encoder_->SetFecControllerOverride(fec_controller_override_);
    }
  });
}

void VideoStreamEncoder::AddAdaptationResource(
    scoped_refptr<Resource> resource) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  TRACE_EVENT0("webrtc", "VideoStreamEncoder::AddAdaptationResource");
  // Map any externally added resources as kCpu for the sake of stats reporting.
  // TODO(hbos): Make the manager map any unknown resources to kCpu and get rid
  // of this MapResourceToReason() call.
  TRACE_EVENT_ASYNC_BEGIN0(
      "webrtc", "VideoStreamEncoder::AddAdaptationResource(latency)", this);
  encoder_queue_->PostTask([this, resource = std::move(resource)] {
    TRACE_EVENT_ASYNC_END0(
        "webrtc", "VideoStreamEncoder::AddAdaptationResource(latency)", this);
    RTC_DCHECK_RUN_ON(encoder_queue_.get());
    additional_resources_.push_back(resource);
    stream_resource_manager_.AddResource(resource, VideoAdaptationReason::kCpu);
  });
}

void VideoStreamEncoder::SetSource(
    VideoSourceInterface<VideoFrame>* source,
    const DegradationPreference& degradation_preference) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  video_source_sink_controller_.SetSource(source);
  input_state_provider_.OnHasInputChanged(source);

  // This may trigger reconfiguring the QualityScaler on the encoder queue.
  encoder_queue_->PostTask([this, degradation_preference] {
    RTC_DCHECK_RUN_ON(encoder_queue_.get());
    degradation_preference_manager_->SetDegradationPreference(
        degradation_preference);
    stream_resource_manager_.SetDegradationPreferences(degradation_preference);
    if (encoder_) {
      stream_resource_manager_.ConfigureQualityScaler(
          encoder_->GetEncoderInfo());
      stream_resource_manager_.ConfigureBandwidthQualityScaler(
          encoder_->GetEncoderInfo());
    }
  });
}

void VideoStreamEncoder::SetSink(EncoderSink* sink, bool rotation_applied) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  video_source_sink_controller_.SetRotationApplied(rotation_applied);
  video_source_sink_controller_.PushSourceSinkSettings();

  encoder_queue_->PostTask([this, sink] {
    RTC_DCHECK_RUN_ON(encoder_queue_.get());
    sink_ = sink;
  });
}

void VideoStreamEncoder::SetStartBitrate(int start_bitrate_bps) {
  encoder_queue_->PostTask([this, start_bitrate_bps] {
    RTC_DCHECK_RUN_ON(encoder_queue_.get());
    RTC_LOG(LS_INFO) << "SetStartBitrate " << start_bitrate_bps;
    encoder_target_bitrate_bps_ =
        start_bitrate_bps != 0 ? std::optional<uint32_t>(start_bitrate_bps)
                               : std::nullopt;
    stream_resource_manager_.SetStartBitrate(
        DataRate::BitsPerSec(start_bitrate_bps));
  });
}

void VideoStreamEncoder::ConfigureEncoder(VideoEncoderConfig config,
                                          size_t max_data_payload_length) {
  ConfigureEncoder(std::move(config), max_data_payload_length, nullptr);
}

void VideoStreamEncoder::ConfigureEncoder(VideoEncoderConfig config,
                                          size_t max_data_payload_length,
                                          SetParametersCallback callback) {
  RTC_DCHECK_RUN_ON(worker_queue_);

  // Inform source about max configured framerate,
  // scale_resolution_down_to and which layers are active.
  int max_framerate = -1;
  // Is any layer active.
  bool active = false;
  // The max scale_resolution_down_to.
  std::optional<VideoSinkWants::FrameSize> scale_resolution_down_to;
  for (const auto& stream : config.simulcast_layers) {
    active |= stream.active;
    if (stream.active) {
      max_framerate = std::max(stream.max_framerate, max_framerate);
    }
    // Note: we propagate the highest scale_resolution_down_to regardless
    // if layer is active or not.
    if (stream.scale_resolution_down_to) {
      if (!scale_resolution_down_to) {
        scale_resolution_down_to.emplace(
            stream.scale_resolution_down_to->width,
            stream.scale_resolution_down_to->height);
      } else {
        scale_resolution_down_to.emplace(
            std::max(stream.scale_resolution_down_to->width,
                     scale_resolution_down_to->width),
            std::max(stream.scale_resolution_down_to->height,
                     scale_resolution_down_to->height));
      }
    }
  }

  bool scale_down_to_changed =
      scale_resolution_down_to !=
      video_source_sink_controller_.scale_resolution_down_to();
  if (scale_down_to_changed ||
      active != video_source_sink_controller_.active() ||
      max_framerate !=
          video_source_sink_controller_.frame_rate_upper_limit().value_or(-1)) {
    video_source_sink_controller_.SetScaleResolutionDownTo(
        scale_resolution_down_to);
    if (max_framerate >= 0) {
      video_source_sink_controller_.SetFrameRateUpperLimit(max_framerate);
    } else {
      video_source_sink_controller_.SetFrameRateUpperLimit(std::nullopt);
    }
    video_source_sink_controller_.SetActive(active);
    video_source_sink_controller_.PushSourceSinkSettings();
    video_source_sink_controller_.RequestRefreshFrame();
  }

  encoder_queue_->PostTask([this, config = std::move(config),
                            max_data_payload_length,
                            callback = std::move(callback)]() mutable {
    RTC_DCHECK_RUN_ON(encoder_queue_.get());
    RTC_DCHECK(sink_);
    RTC_LOG(LS_INFO) << "ConfigureEncoder requested.";

    // Set up the frame cadence adapter according to if we're going to do
    // screencast. The final number of spatial layers is based on info
    // in `send_codec_`, which is computed based on incoming frame
    // dimensions which can only be determined later.
    //
    // Note: zero-hertz mode isn't enabled by this alone. Constraints also
    // have to be set up with min_fps = 0 and max_fps > 0.
    if (config.content_type == VideoEncoderConfig::ContentType::kScreen ||
        config.allow_zero_hertz_video) {
      frame_cadence_adapter_->SetZeroHertzModeEnabled(
          FrameCadenceAdapterInterface::ZeroHertzModeParams{});
    } else {
      frame_cadence_adapter_->SetZeroHertzModeEnabled(std::nullopt);
    }

    bool video_format_changed =
        encoder_config_.video_format != config.video_format;
    if (!video_format_changed && encoder_config_.simulcast_layers.size() !=
                                     config.simulcast_layers.size()) {
      video_format_changed = true;
    }
    if (!video_format_changed) {
      for (size_t i = 0; i < encoder_config_.simulcast_layers.size(); i++) {
        if (!encoder_config_.GetSimulcastVideoFormat(i).IsSameCodec(
                config.GetSimulcastVideoFormat(i))) {
          video_format_changed = true;
          break;
        }
      }
    }

    pending_encoder_creation_ =
        (!encoder_ || video_format_changed ||
         max_data_payload_length_ != max_data_payload_length);
    encoder_config_ = std::move(config);
    max_data_payload_length_ = max_data_payload_length;
    pending_encoder_reconfiguration_ = true;

    // Reconfigure the encoder now if the frame resolution is known.
    // Otherwise, the reconfiguration is deferred until the next frame to
    // minimize the number of reconfigurations. The codec configuration
    // depends on incoming video frame size.
    if (last_frame_info_) {
      if (callback) {
        encoder_configuration_callbacks_.push_back(std::move(callback));
      }

      ReconfigureEncoder();
    } else {
      InvokeSetParametersCallback(callback, RTCError::OK());
    }
  });
}

// We should reduce the number of 'full' ReconfigureEncoder(). If only need
// subset of it at runtime, consider handle it in
// VideoStreamEncoder::EncodeVideoFrame() when encoder_info_ != info.
void VideoStreamEncoder::ReconfigureEncoder() {
  // Running on the encoder queue.
  RTC_DCHECK(pending_encoder_reconfiguration_);
  RTC_LOG(LS_INFO) << "[VSE] " << __func__
                   << " [encoder_config=" << encoder_config_.ToString() << "]";

  bool encoder_reset_required = false;
  if (pending_encoder_creation_) {
    // Destroy existing encoder instance before creating a new one. Otherwise
    // attempt to create another instance will fail if encoder factory
    // supports only single instance of encoder of given type.
    ReleaseEncoder();
    encoder_.reset();

    encoder_ = MaybeCreateFrameDumpingEncoderWrapper(
        env_,
        settings_.encoder_factory->Create(env_, encoder_config_.video_format));
    if (!encoder_) {
      RTC_LOG(LS_ERROR) << "CreateVideoEncoder failed, failing encoder format: "
                        << encoder_config_.video_format.ToString();
      RequestEncoderSwitch();
      return;
    }

    if (encoder_selector_) {
      encoder_selector_->OnCurrentEncoder(encoder_config_.video_format);
    }

    encoder_->SetFecControllerOverride(fec_controller_override_);

    encoder_reset_required = true;
  }

  // TODO(webrtc:14451) : Move AlignmentAdjuster into EncoderStreamFactory
  // Possibly adjusts scale_resolution_down_by in `encoder_config_` to limit the
  // alignment value.
  AlignmentAdjuster::GetAlignmentAndMaybeAdjustScaleFactors(
      encoder_->GetEncoderInfo(), &encoder_config_, std::nullopt);

  std::vector<VideoStream> streams;
  if (encoder_config_.video_stream_factory) {
    // Note: only tests set their own EncoderStreamFactory...
    encoder_config_.video_stream_factory->SetEncoderInfo(encoder_->GetEncoderInfo());
    streams = encoder_config_.video_stream_factory->CreateEncoderStreams(
        env_.field_trials(), last_frame_info_->width, last_frame_info_->height,
        encoder_config_);
  } else {
    auto factory = make_ref_counted<EncoderStreamFactory>(
        encoder_->GetEncoderInfo(), latest_restrictions_);

    streams = factory->CreateEncoderStreams(
        env_.field_trials(), last_frame_info_->width, last_frame_info_->height,
        encoder_config_);
  }

  // TODO(webrtc:14451) : Move AlignmentAdjuster into EncoderStreamFactory
  // Get alignment when actual number of layers are known.
  int alignment = AlignmentAdjuster::GetAlignmentAndMaybeAdjustScaleFactors(
      encoder_->GetEncoderInfo(), &encoder_config_, streams.size());

  // Check that the higher layers do not try to set number of temporal layers
  // to less than 1.
  // TODO(brandtr): Get rid of the wrapping optional as it serves no purpose
  // at this layer.
#if RTC_DCHECK_IS_ON
  for (const auto& stream : streams) {
    RTC_DCHECK_GE(stream.num_temporal_layers.value_or(1), 1);
  }
#endif

  // TODO(ilnik): If configured resolution is significantly less than provided,
  // e.g. because there are not enough SSRCs for all simulcast streams,
  // signal new resolutions via SinkWants to video source.

  // Stream dimensions may be not equal to given because of a simulcast
  // restrictions.
  auto highest_stream = absl::c_max_element(
      streams, [](const VideoStream& a, const VideoStream& b) {
        return std::tie(a.width, a.height) < std::tie(b.width, b.height);
      });
  int highest_stream_width = static_cast<int>(highest_stream->width);
  int highest_stream_height = static_cast<int>(highest_stream->height);
  // Dimension may be reduced to be, e.g. divisible by 4.
  RTC_CHECK_GE(last_frame_info_->width, highest_stream_width);
  RTC_CHECK_GE(last_frame_info_->height, highest_stream_height);
  crop_width_ = last_frame_info_->width - highest_stream_width;
  crop_height_ = last_frame_info_->height - highest_stream_height;

  if (!encoder_->GetEncoderInfo().is_qp_trusted.value_or(true)) {
    // when qp is not trusted, we priorities to using the
    // |resolution_bitrate_limits| provided by the decoder.
    const std::vector<VideoEncoder::ResolutionBitrateLimits>& bitrate_limits =
        encoder_->GetEncoderInfo().resolution_bitrate_limits.empty()
            ? EncoderInfoSettings::
                  GetDefaultSinglecastBitrateLimitsWhenQpIsUntrusted(
                      encoder_config_.codec_type)
            : encoder_->GetEncoderInfo().resolution_bitrate_limits;

    // For BandwidthQualityScaler, its implement based on a certain pixel_count
    // correspond a certain bps interval. In fact, WebRTC default max_bps is
    // 2500Kbps when width * height > 960 * 540. For example, we assume:
    // 1.the camera support 1080p.
    // 2.ResolutionBitrateLimits set 720p bps interval is [1500Kbps,2000Kbps].
    // 3.ResolutionBitrateLimits set 1080p bps interval is [2000Kbps,2500Kbps].
    // We will never be stable at 720p due to actual encoding bps of 720p and
    // 1080p are both 2500Kbps. So it is necessary to do a linear interpolation
    // to get a certain bitrate for certain pixel_count. It also doesn't work
    // for 960*540 and 640*520, we will nerver be stable at 640*520 due to their
    // |target_bitrate_bps| are both 2000Kbps.
    std::optional<VideoEncoder::ResolutionBitrateLimits>
        qp_untrusted_bitrate_limit = EncoderInfoSettings::
            GetSinglecastBitrateLimitForResolutionWhenQpIsUntrusted(
                last_frame_info_->width * last_frame_info_->height,
                bitrate_limits);

    if (qp_untrusted_bitrate_limit) {
      // bandwidth_quality_scaler is only used for singlecast.
      if (streams.size() == 1 && encoder_config_.simulcast_layers.size() == 1) {
        VideoStream& stream = streams.back();
        stream.max_bitrate_bps =
            std::min(stream.max_bitrate_bps,
                     qp_untrusted_bitrate_limit->max_bitrate_bps);
        stream.min_bitrate_bps =
            std::min(stream.max_bitrate_bps,
                     qp_untrusted_bitrate_limit->min_bitrate_bps);
        // If it is screen share mode, the minimum value of max_bitrate should
        // be greater than/equal to 1200kbps.
        if (encoder_config_.content_type ==
            VideoEncoderConfig::ContentType::kScreen) {
          stream.max_bitrate_bps =
              std::max(stream.max_bitrate_bps, kDefaultMinScreenSharebps);
        }
        stream.target_bitrate_bps = stream.max_bitrate_bps;
      }
    }
  } else {
    std::optional<VideoEncoder::ResolutionBitrateLimits>
        encoder_bitrate_limits =
            encoder_->GetEncoderInfo().GetEncoderBitrateLimitsForResolution(
                last_frame_info_->width * last_frame_info_->height);

    if (encoder_bitrate_limits) {
      if (streams.size() == 1 && encoder_config_.simulcast_layers.size() == 1) {
        // Bitrate limits can be set by app (in SDP or RtpEncodingParameters)
        // or/and can be provided by encoder. In presence of both set of
        // limits, the final set is derived as their intersection.
        int min_bitrate_bps;
        if (encoder_config_.simulcast_layers[0].min_bitrate_bps <= 0) {
          min_bitrate_bps = encoder_bitrate_limits->min_bitrate_bps;
        } else {
          min_bitrate_bps = std::max(encoder_bitrate_limits->min_bitrate_bps,
                                     streams.back().min_bitrate_bps);
        }

        int max_bitrate_bps;
        // The API max bitrate comes from both `encoder_config_.max_bitrate_bps`
        // and `encoder_config_.simulcast_layers[0].max_bitrate_bps`.
        std::optional<int> api_max_bitrate_bps;
        if (encoder_config_.simulcast_layers[0].max_bitrate_bps > 0) {
          api_max_bitrate_bps =
              encoder_config_.simulcast_layers[0].max_bitrate_bps;
        }
        if (encoder_config_.max_bitrate_bps > 0) {
          api_max_bitrate_bps = api_max_bitrate_bps.has_value()
                                    ? std::min(encoder_config_.max_bitrate_bps,
                                               *api_max_bitrate_bps)
                                    : encoder_config_.max_bitrate_bps;
        }
        if (!api_max_bitrate_bps.has_value()) {
          max_bitrate_bps = encoder_bitrate_limits->max_bitrate_bps;
        } else {
          max_bitrate_bps = std::min(encoder_bitrate_limits->max_bitrate_bps,
                                     streams.back().max_bitrate_bps);
        }

        if (min_bitrate_bps < max_bitrate_bps) {
          streams.back().min_bitrate_bps = min_bitrate_bps;
          streams.back().max_bitrate_bps = max_bitrate_bps;
          streams.back().target_bitrate_bps =
              std::min(streams.back().target_bitrate_bps,
                       encoder_bitrate_limits->max_bitrate_bps);
        } else {
          RTC_LOG(LS_WARNING)
              << "Bitrate limits provided by encoder"
              << " (min=" << encoder_bitrate_limits->min_bitrate_bps
              << ", max=" << encoder_bitrate_limits->max_bitrate_bps
              << ") do not intersect with limits set by app"
              << " (min=" << streams.back().min_bitrate_bps
              << ", max=" << api_max_bitrate_bps.value_or(-1)
              << "). The app bitrate limits will be used.";
        }
      }
    }
  }

  ApplyEncoderBitrateLimitsIfSingleActiveStream(
      GetEncoderInfoWithBitrateLimitUpdate(encoder_->GetEncoderInfo(),
                                           encoder_config_),
      encoder_config_.simulcast_layers, &streams);

  VideoCodec codec = VideoCodecInitializer::SetupCodec(
      env_.field_trials(), encoder_config_, streams);

  if (encoder_config_.codec_type == kVideoCodecVP9 ||
      encoder_config_.codec_type == kVideoCodecAV1
#ifdef RTC_ENABLE_H265
      || encoder_config_.codec_type == kVideoCodecH265
#endif
  ) {
    // Spatial layers configuration might impose some parity restrictions,
    // thus some cropping might be needed.
    RTC_CHECK_GE(last_frame_info_->width, codec.width);
    RTC_CHECK_GE(last_frame_info_->height, codec.height);
    crop_width_ = last_frame_info_->width - codec.width;
    crop_height_ = last_frame_info_->height - codec.height;
    ApplySpatialLayerBitrateLimits(
        GetEncoderInfoWithBitrateLimitUpdate(encoder_->GetEncoderInfo(),
                                             encoder_config_),
        encoder_config_, &codec);
  }

  StringBuilder log_stream;
  log_stream << "ReconfigureEncoder: simulcast streams: ";
  for (size_t i = 0; i < codec.numberOfSimulcastStreams; ++i) {
    log_stream << "{" << i << ": " << codec.simulcastStream[i].width << "x"
               << codec.simulcastStream[i].height << " "
               << ScalabilityModeToString(
                      codec.simulcastStream[i].GetScalabilityMode())
               << ", min_kbps: " << codec.simulcastStream[i].minBitrate
               << ", target_kbps: " << codec.simulcastStream[i].targetBitrate
               << ", max_kbps: " << codec.simulcastStream[i].maxBitrate
               << ", max_fps: " << codec.simulcastStream[i].maxFramerate
               << ", max_qp: " << codec.simulcastStream[i].qpMax << ", num_tl: "
               << codec.simulcastStream[i].numberOfTemporalLayers
               << ", active: "
               << (codec.simulcastStream[i].active ? "true" : "false") << "}";
  }
  if (encoder_config_.codec_type == kVideoCodecVP9 ||
      encoder_config_.codec_type == kVideoCodecAV1
#ifdef RTC_ENABLE_H265
      || encoder_config_.codec_type == kVideoCodecH265
#endif
  ) {
    log_stream << ", spatial layers: ";
    for (int i = 0; i < GetNumSpatialLayers(codec); ++i) {
      log_stream << "{" << i << ": " << codec.spatialLayers[i].width << "x"
                 << codec.spatialLayers[i].height
                 << ", min_kbps: " << codec.spatialLayers[i].minBitrate
                 << ", target_kbps: " << codec.spatialLayers[i].targetBitrate
                 << ", max_kbps: " << codec.spatialLayers[i].maxBitrate
                 << ", max_fps: " << codec.spatialLayers[i].maxFramerate
                 << ", max_qp: " << codec.spatialLayers[i].qpMax << ", num_tl: "
                 << codec.spatialLayers[i].numberOfTemporalLayers
                 << ", active: "
                 << (codec.spatialLayers[i].active ? "true" : "false") << "}";
    }
  }
  RTC_LOG(LS_INFO) << "[VSE] " << log_stream.str();

  codec.startBitrate = std::max(encoder_target_bitrate_bps_.value_or(0) / 1000,
                                codec.minBitrate);
  codec.startBitrate = std::min(codec.startBitrate, codec.maxBitrate);
  codec.expect_encode_from_texture = last_frame_info_->is_texture;
  // Make sure the start bit rate is sane...
  RTC_DCHECK_LE(codec.startBitrate, 1000000);
  max_framerate_ = codec.maxFramerate;

  // The resolutions that we're actually encoding with.
  std::vector<VideoSinkWants::FrameSize> encoder_resolutions;
  // TODO(hbos): For the case of SVC, also make use of `codec.spatialLayers`.
  // For now, SVC layers are handled by the VP9 encoder.
  for (const auto& simulcastStream : codec.simulcastStream) {
    if (!simulcastStream.active)
      continue;
    encoder_resolutions.emplace_back(simulcastStream.width,
                                     simulcastStream.height);
  }

  // Never ask the source for more pixels than the encoder can encode.
  std::optional<size_t> max_pixels_per_frame =
      encoder_->GetEncoderInfo().max_pixels_per_frame;

  worker_queue_->PostTask(SafeTask(
      task_safety_.flag(),
      [this, alignment, max_pixels_per_frame,
       encoder_resolutions = std::move(encoder_resolutions)]() mutable {
        RTC_DCHECK_RUN_ON(worker_queue_);
        if (alignment != video_source_sink_controller_.resolution_alignment() ||
            encoder_resolutions !=
                video_source_sink_controller_.resolutions() ||
            max_pixels_per_frame !=
                video_source_sink_controller_.pixels_per_frame_upper_limit()) {
          video_source_sink_controller_.SetResolutionAlignment(alignment);
          video_source_sink_controller_.SetResolutions(
              std::move(encoder_resolutions));
          video_source_sink_controller_.SetPixelsPerFrameUpperLimit(
              max_pixels_per_frame);
          video_source_sink_controller_.PushSourceSinkSettings();
        }
      }));

  rate_allocator_ = settings_.bitrate_allocator_factory->Create(env_, codec);
  rate_allocator_->SetLegacyConferenceMode(
      encoder_config_.legacy_conference_mode);

  // Reset (release existing encoder) if one exists and anything except
  // start bitrate or max framerate has changed.
  if (!encoder_reset_required) {
    encoder_reset_required = RequiresEncoderReset(
        send_codec_, codec, was_encode_called_since_last_initialization_);
  }

  // GetComplexity() will return kComplexityNormal if nothing configured via
  // field trials.
  codec.SetVideoEncoderComplexity(speed_experiment_.GetComplexity(
      codec.codecType, codec.mode == VideoCodecMode::kScreensharing));

  send_codec_ = codec;

  // Keep the same encoder, as long as the video_format is unchanged.
  // Encoder creation block is split in two since EncoderInfo needed to start
  // CPU adaptation with the correct settings should be polled after
  // encoder_->InitEncode().
  if (encoder_reset_required) {
    ReleaseEncoder();
    const size_t max_data_payload_length = max_data_payload_length_ > 0
                                               ? max_data_payload_length_
                                               : kDefaultPayloadSize;
    VideoEncoder::Settings settings = VideoEncoder::Settings(
        settings_.capabilities, number_of_cores_, max_data_payload_length);
    settings.encoder_thread_limit = experimental_encoder_thread_limit_;
    int error = encoder_->InitEncode(&send_codec_, settings);
    if (error != 0) {
      RTC_LOG(LS_ERROR) << "Failed to initialize the encoder associated with "
                           "codec type: "
                        << CodecTypeToPayloadString(send_codec_.codecType)
                        << " (" << send_codec_.codecType
                        << "). Error: " << error;
      ReleaseEncoder();
    } else {
      encoder_initialized_ = true;
      encoder_->RegisterEncodeCompleteCallback(this);
      frame_encode_metadata_writer_.OnEncoderInit(send_codec_);
      next_frame_types_.clear();
      next_frame_types_.resize(
          std::max(static_cast<int>(codec.numberOfSimulcastStreams), 1),
          VideoFrameType::kVideoFrameKey);
      if (settings_.enable_frame_instrumentation_generator) {
        frame_instrumentation_generator_ =
            FrameInstrumentationGeneratorFactory::Create(
                env_, encoder_config_.codec_type,
                GetScalabilityModeFromVideoCodec(send_codec_));
      }
    }

    frame_encode_metadata_writer_.Reset();
    last_encode_info_ms_ = std::nullopt;
    was_encode_called_since_last_initialization_ = false;
    encoder_fallback_requested_ = false;
  }

  // Inform dependents of updated encoder settings.
  OnEncoderSettingsChanged();

  if (encoder_initialized_) {
    RTC_LOG(LS_VERBOSE) << " max bitrate " << codec.maxBitrate
                        << " start bitrate " << codec.startBitrate
                        << " max frame rate " << codec.maxFramerate
                        << " max payload size " << max_data_payload_length_;
  } else {
    RTC_LOG(LS_ERROR) << "[VSE] Failed to configure encoder.";
    rate_allocator_ = nullptr;
  }

  if (pending_encoder_creation_) {
    stream_resource_manager_.ConfigureEncodeUsageResource();
    pending_encoder_creation_ = false;
  }

  int num_layers;
  if (codec.codecType == kVideoCodecVP8) {
    num_layers = codec.VP8()->numberOfTemporalLayers;
  } else if (codec.codecType == kVideoCodecVP9) {
    num_layers = codec.VP9()->numberOfTemporalLayers;
  } else if ((codec.codecType == kVideoCodecAV1 ||
              codec.codecType == kVideoCodecH265) &&
             codec.GetScalabilityMode().has_value()) {
    num_layers =
        ScalabilityModeToNumTemporalLayers(*(codec.GetScalabilityMode()));
  } else if (codec.codecType == kVideoCodecH264) {
    num_layers = codec.H264()->numberOfTemporalLayers;
  } else if (codec.codecType == kVideoCodecGeneric &&
             codec.numberOfSimulcastStreams > 0) {
    // This is mainly for unit testing, disabling frame dropping.
    // TODO(sprang): Add a better way to disable frame dropping.
    num_layers = codec.simulcastStream[0].numberOfTemporalLayers;
  } else {
    num_layers = 1;
  }

  frame_dropper_.Reset();
  frame_dropper_.SetRates(codec.startBitrate, max_framerate_);
  // Force-disable frame dropper if either:
  //  * We have screensharing with layers.
  //  * "WebRTC-FrameDropper" field trial is "Disabled".
  force_disable_frame_dropper_ =
      env_.field_trials().IsDisabled(kFrameDropperFieldTrial) ||
      (num_layers > 1 && codec.mode == VideoCodecMode::kScreensharing);

  const VideoEncoder::EncoderInfo info = encoder_->GetEncoderInfo();
  if (rate_control_settings_.UseEncoderBitrateAdjuster()) {
    bitrate_adjuster_ = std::make_unique<EncoderBitrateAdjuster>(
        codec, env_.field_trials(), env_.clock());
    bitrate_adjuster_->OnEncoderInfo(info);
  }

  quality_convergence_controller_.Initialize(codec.numberOfSimulcastStreams,
                                             info.min_qp, codec.codecType,
                                             env_.field_trials());

  if (rate_allocator_ && last_encoder_rate_settings_) {
    // We have a new rate allocator instance and already configured target
    // bitrate. Update the rate allocation and notify observers.
    // We must invalidate the last_encoder_rate_settings_ to ensure
    // the changes get propagated to all listeners.
    EncoderRateSettings rate_settings = *last_encoder_rate_settings_;
    last_encoder_rate_settings_.reset();
    rate_settings.rate_control.framerate_fps = GetInputFramerateFps();

    SetEncoderRates(UpdateBitrateAllocation(rate_settings));
  }

  encoder_stats_observer_->OnEncoderReconfigured(encoder_config_, streams);

  pending_encoder_reconfiguration_ = false;

  bool is_svc = false;
  bool single_stream_or_non_first_inactive = true;
  for (size_t i = 1; i < encoder_config_.simulcast_layers.size(); ++i) {
    if (encoder_config_.simulcast_layers[i].active) {
      single_stream_or_non_first_inactive = false;
      break;
    }
  }
  // Set min_bitrate_bps, max_bitrate_bps, and max padding bit rate for VP9,
  // AV1 and H.265, and leave only one stream containing all necessary
  // information.
  if ((
#ifdef RTC_ENABLE_H265
          encoder_config_.codec_type == kVideoCodecH265 ||
#endif
          encoder_config_.codec_type == kVideoCodecVP9 ||
          encoder_config_.codec_type == kVideoCodecAV1) &&
      single_stream_or_non_first_inactive) {
    // Lower max bitrate to the level codec actually can produce.
    streams[0].max_bitrate_bps =
        std::min(streams[0].max_bitrate_bps,
                 SvcRateAllocator::GetMaxBitrate(codec).bps<int>());
    streams[0].min_bitrate_bps = codec.spatialLayers[0].minBitrate * 1000;
    // target_bitrate_bps specifies the maximum padding bitrate.
    streams[0].target_bitrate_bps =
        SvcRateAllocator::GetPaddingBitrate(codec).bps<int>();
    streams[0].width = streams.back().width;
    streams[0].height = streams.back().height;
    is_svc = GetNumSpatialLayers(codec) > 1;
    streams.resize(1);
  }

  sink_->OnEncoderConfigurationChanged(
      std::move(streams), is_svc, encoder_config_.content_type,
      encoder_config_.min_transmit_bitrate_bps);

  stream_resource_manager_.ConfigureQualityScaler(info);
  stream_resource_manager_.ConfigureBandwidthQualityScaler(info);

  RTCError encoder_configuration_result = RTCError::OK();

  if (!encoder_initialized_) {
    RTC_LOG(LS_WARNING) << "Failed to initialize "
                        << CodecTypeToPayloadString(codec.codecType)
                        << " encoder." << "switch_encoder_on_init_failures: "
                        << switch_encoder_on_init_failures_;

    if (switch_encoder_on_init_failures_) {
      RequestEncoderSwitch();
    } else {
      encoder_configuration_result =
          RTCError(RTCErrorType::UNSUPPORTED_OPERATION);
    }
  }

  if (!encoder_configuration_callbacks_.empty()) {
    for (auto& callback : encoder_configuration_callbacks_) {
      InvokeSetParametersCallback(callback, encoder_configuration_result);
    }
    encoder_configuration_callbacks_.clear();
  }
}

void VideoStreamEncoder::RequestEncoderSwitch() {
  bool is_encoder_switching_supported =
      encoder_switch_request_callback_ != nullptr;
  bool is_encoder_selector_available = encoder_selector_ != nullptr;

  RTC_LOG(LS_INFO) << "RequestEncoderSwitch."
                   << " is_encoder_selector_available: "
                   << is_encoder_selector_available
                   << " is_encoder_switching_supported: "
                   << is_encoder_switching_supported;

  if (!is_encoder_switching_supported) {
    return;
  }

  // If encoder selector is available, switch to the encoder it prefers.
  std::optional<SdpVideoFormat> preferred_fallback_encoder;
  if (is_encoder_selector_available) {
    preferred_fallback_encoder = encoder_selector_->OnEncoderBroken();
  }

  if (!preferred_fallback_encoder) {
    if (!env_.field_trials().IsDisabled(
            kSwitchEncoderFollowCodecPreferenceOrderFieldTrial)) {
      encoder_fallback_requested_ = true;
      encoder_switch_request_callback_(std::nullopt,
                                       /*allow_default_fallback=*/false);
      return;
    } else {
      preferred_fallback_encoder =
          SdpVideoFormat(CodecTypeToPayloadString(kVideoCodecVP8));
    }
  }

  encoder_switch_request_callback_(*preferred_fallback_encoder,
                                   /*allow_default_fallback=*/true);
}

void VideoStreamEncoder::OnEncoderSettingsChanged() {
  EncoderSettings encoder_settings(
      GetEncoderInfoWithBitrateLimitUpdate(encoder_->GetEncoderInfo(),
                                           encoder_config_),
      encoder_config_.Copy(), send_codec_);
  input_state_provider_.OnEncoderSettingsChanged(encoder_settings);
  stream_resource_manager_.SetEncoderSettings(std::move(encoder_settings));
  bool is_screenshare =
      encoder_config_.content_type == VideoEncoderConfig::ContentType::kScreen;
  degradation_preference_manager_->SetIsScreenshare(is_screenshare);
  if (is_screenshare || encoder_config_.allow_zero_hertz_video) {
    frame_cadence_adapter_->SetZeroHertzModeEnabled(
        FrameCadenceAdapterInterface::ZeroHertzModeParams{
            send_codec_.numberOfSimulcastStreams});
  }
}

void VideoStreamEncoder::OnFrame(Timestamp post_time,
                                 bool queue_overload,
                                 const VideoFrame& video_frame) {
  RTC_DCHECK_RUN_ON(encoder_queue_.get());

  VideoFrame incoming_frame = video_frame;

  // In some cases, e.g., when the frame from decoder is fed to encoder,
  // the timestamp may be set to the future. As the encoding pipeline assumes
  // capture time to be less than present time, we should reset the capture
  // timestamps here. Otherwise there may be issues with RTP send stream.
  if (incoming_frame.timestamp_us() > post_time.us())
    incoming_frame.set_timestamp_us(post_time.us());

  // Capture time may come from clock with an offset and drift from clock_.
  int64_t capture_ntp_time_ms;
  if (video_frame.ntp_time_ms() > 0) {
    capture_ntp_time_ms = video_frame.ntp_time_ms();
  } else if (video_frame.render_time_ms() != 0) {
    capture_ntp_time_ms = video_frame.render_time_ms() + delta_ntp_internal_ms_;
  } else {
    capture_ntp_time_ms = post_time.ms() + delta_ntp_internal_ms_;
  }
  incoming_frame.set_ntp_time_ms(capture_ntp_time_ms);

  // Convert NTP time, in ms, to RTP timestamp.
  const int kMsToRtpTimestamp = 90;
  incoming_frame.set_rtp_timestamp(
      kMsToRtpTimestamp * static_cast<uint32_t>(incoming_frame.ntp_time_ms()));

  // Identifier should remain the same for newly produced incoming frame and the
  // received |video_frame|.
  incoming_frame.set_presentation_timestamp(
      video_frame.presentation_timestamp());

  if (incoming_frame.ntp_time_ms() <= last_captured_timestamp_) {
    // We don't allow the same capture time for two frames, drop this one.
    RTC_LOG(LS_WARNING) << "Same/old NTP timestamp ("
                        << incoming_frame.ntp_time_ms()
                        << " <= " << last_captured_timestamp_
                        << ") for incoming frame. Dropping.";
    ProcessDroppedFrame(incoming_frame,
                        VideoStreamEncoderObserver::DropReason::kBadTimestamp);
    return;
  }

  bool log_stats = false;
  if (post_time.ms() - last_frame_log_ms_ > kFrameLogIntervalMs) {
    last_frame_log_ms_ = post_time.ms();
    log_stats = true;
  }

  encoder_stats_observer_->OnIncomingFrame(incoming_frame.width(),
                                           incoming_frame.height());
  ++captured_frame_count_;
  bool cwnd_frame_drop =
      cwnd_frame_drop_interval_ &&
      (cwnd_frame_counter_++ % cwnd_frame_drop_interval_.value() == 0);
  if (!queue_overload && !cwnd_frame_drop) {
    // Only update `last_captured_timestamp_` when the frame is actually passed
    // on for encoding; otherwise a newer frame queued in the same millisecond
    // that triggered `queue_overload` would be dropped as a duplicate.
    last_captured_timestamp_ = incoming_frame.ntp_time_ms();
    MaybePrepareVideoFrame(incoming_frame, post_time.us());
  } else {
    if (cwnd_frame_drop) {
      // Frame drop by congestion window pushback. Do not encode this
      // frame.
      ++dropped_frame_cwnd_pushback_count_;
    } else {
      // There is a newer frame in flight. Do not encode this frame.
      RTC_LOG(LS_VERBOSE)
          << "Incoming frame dropped due to that the encoder is blocked.";
      ++dropped_frame_encoder_block_count_;
    }
    ProcessDroppedFrame(
        incoming_frame,
        cwnd_frame_drop
            ? VideoStreamEncoderObserver::DropReason::kCongestionWindow
            : VideoStreamEncoderObserver::DropReason::kEncoderQueue);
  }
  if (log_stats) {
    RTC_LOG(LS_INFO) << "Number of frames: captured " << captured_frame_count_
                     << ", dropped (due to congestion window pushback) "
                     << dropped_frame_cwnd_pushback_count_
                     << ", dropped (due to encoder blocked) "
                     << dropped_frame_encoder_block_count_ << ", interval_ms "
                     << kFrameLogIntervalMs;
    captured_frame_count_ = 0;
    dropped_frame_cwnd_pushback_count_ = 0;
    dropped_frame_encoder_block_count_ = 0;
  }
}

void VideoStreamEncoder::OnDiscardedFrame() {
  encoder_stats_observer_->OnFrameDropped(
      VideoStreamEncoderObserver::DropReason::kSource);
}

bool VideoStreamEncoder::EncoderPaused() const {
  RTC_DCHECK_RUN_ON(encoder_queue_.get());
  // Pause video if paused by caller or as long as the network is down or the
  // pacer queue has grown too large in buffered mode.
  // If the pacer queue has grown too large or the network is down,
  // `last_encoder_rate_settings_->encoder_target` will be 0.
  return !last_encoder_rate_settings_ ||
         last_encoder_rate_settings_->encoder_target == DataRate::Zero();
}

void VideoStreamEncoder::TraceFrameDropStart() {
  RTC_DCHECK_RUN_ON(encoder_queue_.get());
  // Start trace event only on the first frame after encoder is paused.
  if (!encoder_paused_and_dropped_frame_) {
    TRACE_EVENT_ASYNC_BEGIN0("webrtc", "EncoderPaused", this);
  }
  encoder_paused_and_dropped_frame_ = true;
}

void VideoStreamEncoder::TraceFrameDropEnd() {
  RTC_DCHECK_RUN_ON(encoder_queue_.get());
  // End trace event on first frame after encoder resumes, if frame was dropped.
  if (encoder_paused_and_dropped_frame_) {
    TRACE_EVENT_ASYNC_END0("webrtc", "EncoderPaused", this);
  }
  encoder_paused_and_dropped_frame_ = false;
}

VideoStreamEncoder::EncoderRateSettings
VideoStreamEncoder::UpdateBitrateAllocation(
    const EncoderRateSettings& rate_settings) {
  VideoBitrateAllocation new_allocation;
  // Only call allocators if bitrate > 0 (ie, not suspended), otherwise they
  // might cap the bitrate to the min bitrate configured.
  if (rate_allocator_ && rate_settings.encoder_target > DataRate::Zero()) {
    new_allocation = rate_allocator_->Allocate(VideoBitrateAllocationParameters(
        rate_settings.encoder_target,
        rate_settings.rate_control.framerate_fps));
  }

  EncoderRateSettings new_rate_settings = rate_settings;
  new_rate_settings.rate_control.target_bitrate = new_allocation;
  new_rate_settings.rate_control.bitrate = new_allocation;
  // VideoBitrateAllocator subclasses may allocate a bitrate higher than the
  // target in order to sustain the min bitrate of the video codec. In this
  // case, make sure the bandwidth allocation is at least equal the allocation
  // as that is part of the document contract for that field.
  new_rate_settings.rate_control.bandwidth_allocation =
      std::max(new_rate_settings.rate_control.bandwidth_allocation,
               DataRate::BitsPerSec(
                   new_rate_settings.rate_control.bitrate.get_sum_bps()));

  if (bitrate_adjuster_) {
    VideoBitrateAllocation adjusted_allocation =
        bitrate_adjuster_->AdjustRateAllocation(new_rate_settings.rate_control);
    RTC_LOG(LS_VERBOSE) << "Adjusting allocation, fps = "
                        << rate_settings.rate_control.framerate_fps << ", from "
                        << new_allocation.ToString() << ", to "
                        << adjusted_allocation.ToString();
    new_rate_settings.rate_control.bitrate = adjusted_allocation;
  }

  return new_rate_settings;
}

uint32_t VideoStreamEncoder::GetInputFramerateFps() {
  const uint32_t default_fps = max_framerate_ != -1 ? max_framerate_ : 30;

  // This method may be called after we cleared out the frame_cadence_adapter_
  // reference in Stop(). In such a situation it's probably not important with a
  // decent estimate.
  std::optional<uint32_t> input_fps =
      frame_cadence_adapter_ ? frame_cadence_adapter_->GetInputFrameRateFps()
                             : std::nullopt;
  if (!input_fps || *input_fps == 0) {
    return default_fps;
  }
  return *input_fps;
}

void VideoStreamEncoder::SetEncoderRates(
    const EncoderRateSettings& rate_settings) {
  RTC_DCHECK_GT(rate_settings.rate_control.framerate_fps, 0.0);
  bool rate_control_changed =
      (!last_encoder_rate_settings_.has_value() ||
       last_encoder_rate_settings_->rate_control != rate_settings.rate_control);
  // For layer allocation signal we care only about the target bitrate (not the
  // adjusted one) and the target fps.
  bool layer_allocation_changed =
      !last_encoder_rate_settings_.has_value() ||
      last_encoder_rate_settings_->rate_control.target_bitrate !=
          rate_settings.rate_control.target_bitrate ||
      last_encoder_rate_settings_->rate_control.framerate_fps !=
          rate_settings.rate_control.framerate_fps;

  if (last_encoder_rate_settings_ != rate_settings) {
    last_encoder_rate_settings_ = rate_settings;
  }

  if (!encoder_)
    return;

  // Make the cadence adapter know if streams were disabled.
  for (int spatial_index = 0;
       spatial_index != send_codec_.numberOfSimulcastStreams; ++spatial_index) {
    frame_cadence_adapter_->UpdateLayerStatus(
        spatial_index,
        /*enabled=*/rate_settings.rate_control.target_bitrate
                .GetSpatialLayerSum(spatial_index) > 0);
  }

  // `bitrate_allocation` is 0 it means that the network is down or the send
  // pacer is full. We currently don't pass this on to the encoder since it is
  // unclear how current encoder implementations behave when given a zero target
  // bitrate.
  // TODO(perkj): Make sure all known encoder implementations handle zero
  // target bitrate and remove this check.
  if (rate_settings.rate_control.bitrate.get_sum_bps() == 0)
    return;

  if (rate_control_changed) {
    encoder_->SetRates(rate_settings.rate_control);

    encoder_stats_observer_->OnBitrateAllocationUpdated(
        send_codec_, rate_settings.rate_control.bitrate);
    frame_encode_metadata_writer_.OnSetRates(
        rate_settings.rate_control.bitrate,
        static_cast<uint32_t>(rate_settings.rate_control.framerate_fps + 0.5));
    stream_resource_manager_.SetEncoderRates(rate_settings.rate_control);
    if (layer_allocation_changed &&
        allocation_cb_type_ ==
            BitrateAllocationCallbackType::kVideoLayersAllocation) {
      sink_->OnVideoLayersAllocationUpdated(CreateVideoLayersAllocation(
          send_codec_, rate_settings.rate_control, encoder_->GetEncoderInfo()));
    }
  }
}

void VideoStreamEncoder::MaybePrepareVideoFrame(const VideoFrame& video_frame,
                                                int64_t time_when_posted_us) {
  RTC_DCHECK_RUN_ON(encoder_queue_.get());
  if (pending_mapped_frames_.size() > kMaxFramesInPreparation) {
    RTC_LOG(LS_ERROR) << "To many frames are being prepared.";
    ProcessDroppedFrame(video_frame,
                        VideoStreamEncoderObserver::DropReason::kEncoderQueue);
    return;
  }

  std::optional<VideoEncoder::Resolution> mapped_resolution;
  if (encoder_) {
    mapped_resolution = encoder_->GetEncoderInfo().mapped_resolution;
  }

  pending_mapped_frames_.push_back(
      PreparingFrame{.frame = std::move(video_frame),
                     .can_send = false,
                     .frame_id = frame_counter_++,
                     .time_when_posted_us = time_when_posted_us});

  const PreparingFrame& last_frame = pending_mapped_frames_.back();
  if (mapped_resolution.has_value()) {
    if (mapped_resolution->width > video_frame.width() ||
        mapped_resolution->height > video_frame.height()) {
      mapped_resolution->width = video_frame.width();
      mapped_resolution->height = video_frame.height();
    }
    last_frame.frame.video_frame_buffer()->PrepareMappedBufferAsync(
        mapped_resolution->width, mapped_resolution->height,
        prepared_frames_processor_, last_frame.frame_id);
  } else {
    OnFramePrepared(last_frame.frame_id);
  }
}

void VideoStreamEncoder::OnFramePrepared(size_t frame_id) {
  if (!encoder_queue_->IsCurrent()) {
    encoder_queue_->PostTask([this, frame_id]() { OnFramePrepared(frame_id); });
    return;
  }

  RTC_DCHECK_RUN_ON(encoder_queue_.get());

  // Encoder may already be stopped by the time this task starts executing.
  if (is_stopped_)
    return;

  for (auto& frame : pending_mapped_frames_) {
    if (frame.frame_id == frame_id) {
      frame.can_send = true;
      break;
    }
  }

  while (!pending_mapped_frames_.empty() &&
         pending_mapped_frames_.front().can_send) {
    auto& front = pending_mapped_frames_.front();
    MaybeEncodeVideoFrame(front.frame, front.time_when_posted_us);
    pending_mapped_frames_.pop_front();
  }
}

void VideoStreamEncoder::MaybeEncodeVideoFrame(const VideoFrame& video_frame,
                                               int64_t time_when_posted_us) {
  RTC_DCHECK_RUN_ON(encoder_queue_.get());

  input_state_provider_.OnFrameSizeObserved(video_frame.size());

  if (!last_frame_info_ || video_frame.width() != last_frame_info_->width ||
      video_frame.height() != last_frame_info_->height ||
      video_frame.is_texture() != last_frame_info_->is_texture) {
    if ((!last_frame_info_ || video_frame.width() != last_frame_info_->width ||
         video_frame.height() != last_frame_info_->height) &&
        encoder_switch_request_callback_ && encoder_selector_) {
      if (auto encoder = encoder_selector_->OnResolutionChange(
              {video_frame.width(), video_frame.height()})) {
        encoder_switch_request_callback_(*encoder,
                                         /*allow_default_fallback=*/false);
      }
    }

    pending_encoder_reconfiguration_ = true;
    last_frame_info_ = VideoFrameInfo(video_frame.width(), video_frame.height(),
                                      video_frame.is_texture());
    RTC_LOG(LS_INFO) << "Video frame parameters changed: dimensions="
                     << last_frame_info_->width << "x"
                     << last_frame_info_->height
                     << ", texture=" << last_frame_info_->is_texture << ".";
    // Force full frame update, since resolution has changed.
    accumulated_update_rect_ =
        VideoFrame::UpdateRect{.offset_x = 0,
                               .offset_y = 0,
                               .width = video_frame.width(),
                               .height = video_frame.height()};
  }

  // We have to create the encoder before the frame drop logic,
  // because the latter depends on encoder_->GetScalingSettings.
  // According to the testcase
  // InitialFrameDropOffWhenEncoderDisabledScaling, the return value
  // from GetScalingSettings should enable or disable the frame drop.
  uint32_t framerate_fps = GetInputFramerateFps();

  int64_t now_ms = env_.clock().TimeInMilliseconds();
  if (pending_encoder_reconfiguration_) {
    ReconfigureEncoder();
    last_parameters_update_ms_.emplace(now_ms);
  } else if (!last_parameters_update_ms_ ||
             now_ms - *last_parameters_update_ms_ >=
                 kParameterUpdateIntervalMs) {
    if (last_encoder_rate_settings_) {
      // Clone rate settings before update, so that SetEncoderRates() will
      // actually detect the change between the input and
      // `last_encoder_rate_setings_`, triggering the call to SetRate() on the
      // encoder.
      EncoderRateSettings new_rate_settings = *last_encoder_rate_settings_;
      new_rate_settings.rate_control.framerate_fps =
          static_cast<double>(framerate_fps);
      SetEncoderRates(UpdateBitrateAllocation(new_rate_settings));
    }
    last_parameters_update_ms_.emplace(now_ms);
  }

  // Because pending frame will be dropped in any case, we need to
  // remember its updated region.
  if (pending_frame_) {
    ProcessDroppedFrame(*pending_frame_,
                        VideoStreamEncoderObserver::DropReason::kEncoderQueue);
  }

  if (DropDueToSize(video_frame.size())) {
    RTC_LOG(LS_INFO) << "Dropping frame. Too large for target bitrate.";
    stream_resource_manager_.OnFrameDroppedDueToSize();
    // Storing references to a native buffer risks blocking frame capture.
    if (video_frame.video_frame_buffer()->type() !=
        VideoFrameBuffer::Type::kNative) {
      pending_frame_ = video_frame;
      pending_frame_post_time_us_ = time_when_posted_us;
    } else {
      // Ensure that any previously stored frame is dropped.
      pending_frame_.reset();
      ProcessDroppedFrame(
          video_frame, VideoStreamEncoderObserver::DropReason::kEncoderQueue);
    }
    return;
  }
  stream_resource_manager_.OnMaybeEncodeFrame();

  if (EncoderPaused()) {
    // Storing references to a native buffer risks blocking frame capture.
    if (video_frame.video_frame_buffer()->type() !=
        VideoFrameBuffer::Type::kNative) {
      if (pending_frame_)
        TraceFrameDropStart();
      pending_frame_ = video_frame;
      pending_frame_post_time_us_ = time_when_posted_us;
    } else {
      // Ensure that any previously stored frame is dropped.
      pending_frame_.reset();
      TraceFrameDropStart();
      ProcessDroppedFrame(
          video_frame, VideoStreamEncoderObserver::DropReason::kEncoderQueue);
    }
    return;
  }

  pending_frame_.reset();

  frame_dropper_.Leak(framerate_fps);
  // Frame dropping is enabled iff frame dropping is not force-disabled, and
  // rate controller is not trusted.
  const bool frame_dropping_enabled =
      !force_disable_frame_dropper_ &&
      !encoder_info_.has_trusted_rate_controller;
  frame_dropper_.Enable(frame_dropping_enabled);
  if (frame_dropping_enabled && frame_dropper_.DropFrame()) {
    RTC_LOG(LS_VERBOSE)
        << "Drop Frame: "
           "target bitrate "
        << (last_encoder_rate_settings_
                ? last_encoder_rate_settings_->encoder_target.bps()
                : 0)
        << ", input frame rate " << framerate_fps;
    ProcessDroppedFrame(
        video_frame,
        VideoStreamEncoderObserver::DropReason::kMediaOptimization);
    return;
  }

  EncodeVideoFrame(video_frame, time_when_posted_us);
}

void VideoStreamEncoder::EncodeVideoFrame(const VideoFrame& video_frame,
                                          int64_t time_when_posted_us) {
  RTC_DCHECK_RUN_ON(encoder_queue_.get());
  RTC_LOG(LS_VERBOSE) << __func__ << " posted " << time_when_posted_us
                      << " ntp time " << video_frame.ntp_time_ms();

  // If encoder fallback is requested, but we run out of codecs to be
  // negotiated, we don't continue to encode frames. The send streams will still
  // be kept. Otherwise if WebRtcVideoEngine responds to the fallback request,
  // the send streams will be recreated and current VideoStreamEncoder will no
  // longer be used.
  if (encoder_fallback_requested_ || !encoder_initialized_) {
    return;
  }

  // It's possible that EncodeVideoFrame can be called after we've completed
  // a Stop() operation. Check if the encoder_ is set before continuing.
  // See: bugs.webrtc.org/12857
  if (!encoder_)
    return;

  TraceFrameDropEnd();

  // Encoder metadata needs to be updated before encode complete callback.
  {
    VideoEncoder::EncoderInfo info = encoder_->GetEncoderInfo();
    if (info.implementation_name != encoder_info_.implementation_name ||
        info.is_hardware_accelerated != encoder_info_.is_hardware_accelerated) {
      encoder_stats_observer_->OnEncoderImplementationChanged({
          .name = info.implementation_name,
          .is_hardware_accelerated = info.is_hardware_accelerated,
      });
      if (bitrate_adjuster_) {
        // Encoder implementation changed, reset overshoot detector states.
        bitrate_adjuster_->Reset();
      }
    }

    if (encoder_info_ != info) {
      OnEncoderSettingsChanged();
      stream_resource_manager_.ConfigureEncodeUsageResource();
      // Re-configure scalers when encoder info changed. Consider two cases:
      // 1. When the status of the scaler changes from enabled to disabled, if
      // we don't do this CL, scaler will adapt up/down to trigger an
      // unnecessary full ReconfigureEncoder() when the scaler should be banned.
      // 2. When the status of the scaler changes from disabled to enabled, if
      // we don't do this CL, scaler will not work until some code trigger
      // ReconfigureEncoder(). In extreme cases, the scaler doesn't even work
      // for a long time when we expect that the scaler should work.
      stream_resource_manager_.ConfigureQualityScaler(info);
      stream_resource_manager_.ConfigureBandwidthQualityScaler(info);

      RTC_LOG(LS_INFO) << "[VSE] Encoder info changed to " << info.ToString();
    }

    if (bitrate_adjuster_) {
      for (size_t si = 0; si < kMaxSpatialLayers; ++si) {
        if (info.fps_allocation[si] != encoder_info_.fps_allocation[si]) {
          bitrate_adjuster_->OnEncoderInfo(info);
          break;
        }
      }
    }
    encoder_info_ = std::move(info);
    last_encode_info_ms_ = env_.clock().TimeInMilliseconds();
  }

  VideoFrame out_frame(video_frame);
  // Crop or scale the frame if needed. Dimension may be reduced to fit encoder
  // requirements, e.g. some encoders may require them to be divisible by 4.
  if ((crop_width_ > 0 || crop_height_ > 0) &&
      (out_frame.video_frame_buffer()->type() !=
           VideoFrameBuffer::Type::kNative ||
       !encoder_info_.supports_native_handle)) {
    int cropped_width = video_frame.width() - crop_width_;
    int cropped_height = video_frame.height() - crop_height_;
    scoped_refptr<VideoFrameBuffer> cropped_buffer;
    // TODO(ilnik): Remove scaling if cropping is too big, as it should never
    // happen after SinkWants signaled correctly from ReconfigureEncoder.
    VideoFrame::UpdateRect update_rect = video_frame.update_rect();
    if (crop_width_ < 4 && crop_height_ < 4) {
      // The difference is small, crop without scaling.
      int offset_x = (crop_width_ + 1) / 2;
      int offset_y = (crop_height_ + 1) / 2;
      // Make sure offset is even so that u/v plane becomes aligned if u/v plane
      // is subsampled.
      offset_x -= offset_x % 2;
      offset_y -= offset_y % 2;
      cropped_buffer = video_frame.video_frame_buffer()->CropAndScale(
          offset_x, offset_y, cropped_width, cropped_height, cropped_width,
          cropped_height);
      update_rect.offset_x -= offset_x;
      update_rect.offset_y -= offset_y;
      update_rect.Intersect(VideoFrame::UpdateRect{.offset_x = 0,
                                                   .offset_y = 0,
                                                   .width = cropped_width,
                                                   .height = cropped_height});

    } else {
      // The difference is large, scale it.
      cropped_buffer = video_frame.video_frame_buffer()->Scale(cropped_width,
                                                               cropped_height);
      if (!update_rect.IsEmpty()) {
        // Since we can't reason about pixels after scaling, we invalidate whole
        // picture, if anything changed.
        update_rect = VideoFrame::UpdateRect{.offset_x = 0,
                                             .offset_y = 0,
                                             .width = cropped_width,
                                             .height = cropped_height};
      }
    }
    if (!cropped_buffer) {
      RTC_LOG(LS_ERROR) << "Cropping and scaling frame failed, dropping frame.";
      return;
    }

    out_frame.set_video_frame_buffer(cropped_buffer);
    out_frame.set_update_rect(update_rect);
    out_frame.set_ntp_time_ms(video_frame.ntp_time_ms());
    out_frame.set_presentation_timestamp(video_frame.presentation_timestamp());
    // Since accumulated_update_rect_ is constructed before cropping,
    // we can't trust it. If any changes were pending, we invalidate whole
    // frame here.
    if (!accumulated_update_rect_.IsEmpty()) {
      accumulated_update_rect_ =
          VideoFrame::UpdateRect{.offset_x = 0,
                                 .offset_y = 0,
                                 .width = out_frame.width(),
                                 .height = out_frame.height()};
      accumulated_update_rect_is_valid_ = false;
    }
  }

  if (!accumulated_update_rect_is_valid_) {
    out_frame.clear_update_rect();
  } else if (!accumulated_update_rect_.IsEmpty() &&
             out_frame.has_update_rect()) {
    accumulated_update_rect_.Union(out_frame.update_rect());
    accumulated_update_rect_.Intersect(
        VideoFrame::UpdateRect{.offset_x = 0,
                               .offset_y = 0,
                               .width = out_frame.width(),
                               .height = out_frame.height()});
    out_frame.set_update_rect(accumulated_update_rect_);
    accumulated_update_rect_.MakeEmptyUpdate();
  }
  accumulated_update_rect_is_valid_ = true;

  TRACE_EVENT_ASYNC_STEP_INTO0("webrtc", "Video", video_frame.render_time_ms(),
                               "Encode");

  stream_resource_manager_.OnEncodeStarted(out_frame, time_when_posted_us);

  // The encoder should get the size that it expects.
  RTC_DCHECK(send_codec_.width <= out_frame.width() &&
             send_codec_.height <= out_frame.height())
      << "Encoder configured to " << send_codec_.width << "x"
      << send_codec_.height << " received a too small frame "
      << out_frame.width() << "x" << out_frame.height();

  TRACE_EVENT2("webrtc", "VideoEncoder::Encode", "rtp_timestamp",
               out_frame.rtp_timestamp(), "storage_representation",
               out_frame.video_frame_buffer()->storage_representation());

  frame_encode_metadata_writer_.OnEncodeStarted(out_frame);

  if (frame_instrumentation_generator_) {
    frame_instrumentation_generator_->OnCapturedFrame(out_frame);
  }

  const int32_t encode_status = encoder_->Encode(out_frame, &next_frame_types_);
  was_encode_called_since_last_initialization_ = true;

  if (encode_status < 0) {
    RTC_LOG(LS_ERROR) << "Encoder failed, failing encoder format: "
                      << encoder_config_.video_format.ToString();
    RequestEncoderSwitch();
    return;
  }

  for (auto& it : next_frame_types_) {
    it = VideoFrameType::kVideoFrameDelta;
  }
}

void VideoStreamEncoder::RequestRefreshFrame() {
  worker_queue_->PostTask(SafeTask(task_safety_.flag(), [this] {
    RTC_DCHECK_RUN_ON(worker_queue_);
    video_source_sink_controller_.RequestRefreshFrame();
  }));
}

void VideoStreamEncoder::SendKeyFrame(
    const std::vector<VideoFrameType>& layers) {
  if (!encoder_queue_->IsCurrent()) {
    encoder_queue_->PostTask([this, layers] { SendKeyFrame(layers); });
    return;
  }
  RTC_DCHECK_RUN_ON(encoder_queue_.get());
  TRACE_EVENT0("webrtc", "OnKeyFrameRequest");
  RTC_DCHECK(!next_frame_types_.empty());

  if (frame_cadence_adapter_)
    frame_cadence_adapter_->ProcessKeyFrameRequest();

  if (!encoder_) {
    RTC_DLOG(LS_INFO) << __func__ << " no encoder.";
    return;  // Shutting down, or not configured yet.
  }

  if (!layers.empty()) {
    // In single-stream or SVC configurations (`next_frame_types_.size() == 1`),
    // the number of RTP SSRCs in `layers` can exceed the number of encoded
    // streams. Any keyframe requested on any SSRC layer must trigger a keyframe
    // on that single encoded stream.
    if (next_frame_types_.size() == 1) {
      if (absl::c_linear_search(layers, VideoFrameType::kVideoFrameKey)) {
        next_frame_types_[0] = VideoFrameType::kVideoFrameKey;
      }
    } else {
      for (size_t i = 0;
           i < layers.size() && i < next_frame_types_.size(); i++) {
        if (layers[i] == VideoFrameType::kVideoFrameKey) {
          next_frame_types_[i] = VideoFrameType::kVideoFrameKey;
        }
      }
    }
  } else {
    std::fill(next_frame_types_.begin(), next_frame_types_.end(),
              VideoFrameType::kVideoFrameKey);
  }
}

void VideoStreamEncoder::OnLossNotification(
    const VideoEncoder::LossNotification& loss_notification) {
  if (!encoder_queue_->IsCurrent()) {
    encoder_queue_->PostTask(
        [this, loss_notification] { OnLossNotification(loss_notification); });
    return;
  }

  RTC_DCHECK_RUN_ON(encoder_queue_.get());
  if (encoder_) {
    encoder_->OnLossNotification(loss_notification);
  }
}

EncodedImage VideoStreamEncoder::AugmentEncodedImage(
    const EncodedImage& encoded_image,
    const CodecSpecificInfo* codec_specific_info) {
  EncodedImage image_copy(encoded_image);
  // We could either have simulcast layers or spatial layers.
  // TODO(https://crbug.com/webrtc/14891): If we want to support a mix of
  // simulcast and SVC we'll also need to consider the case where we have both
  // simulcast and spatial indices.
  int stream_idx = std::max(encoded_image.SpatialIndex().value_or(0),
                            encoded_image.SimulcastIndex().value_or(0));

  frame_encode_metadata_writer_.FillMetadataAndTimingInfo(stream_idx,
                                                          &image_copy);
  frame_encode_metadata_writer_.UpdateBitstream(codec_specific_info,
                                                &image_copy);
  VideoCodecType codec_type = codec_specific_info
                                  ? codec_specific_info->codecType
                                  : VideoCodecType::kVideoCodecGeneric;
  if (image_copy.qp_ < 0 && qp_parsing_allowed_) {
    // Parse encoded frame QP if that was not provided by encoder.
    image_copy.qp_ =
        qp_parser_
            .Parse(codec_type, stream_idx, image_copy.data(), image_copy.size())
            .value_or(-1);
  }

  TRACE_EVENT2("webrtc", "VideoStreamEncoder::AugmentEncodedImage",
               "stream_idx", stream_idx, "qp", image_copy.qp_);
  RTC_LOG(LS_VERBOSE) << __func__ << " ntp time " << encoded_image.NtpTimeMs()
                      << " stream_idx " << stream_idx << " qp "
                      << image_copy.qp_;
  return image_copy;
}

EncodedImageCallback::Result VideoStreamEncoder::OnEncodedImage(
    const EncodedImage& encoded_image,
    const CodecSpecificInfo* codec_specific_info) {
  TRACE_EVENT_INSTANT1("webrtc", "VCMEncodedFrameCallback::Encoded",
                       TRACE_EVENT_SCOPE_GLOBAL, "timestamp",
                       encoded_image.RtpTimestamp());

  const size_t simulcast_index = encoded_image.SimulcastIndex().value_or(0);
  const VideoCodecType codec_type = codec_specific_info
                                        ? codec_specific_info->codecType
                                        : VideoCodecType::kVideoCodecGeneric;
  EncodedImage image_copy =
      AugmentEncodedImage(encoded_image, codec_specific_info);

  // Post a task because `send_codec_` requires `encoder_queue_` lock and we
  // need to update on quality convergence.
  unsigned int image_width = image_copy._encodedWidth;
  unsigned int image_height = image_copy._encodedHeight;
  encoder_queue_->PostTask(
      [this, codec_type, image_width, image_height, simulcast_index,
       temporal_index = image_copy.TemporalIndex().value_or(0),
       qp = image_copy.qp_,
       is_steady_state_refresh_frame = image_copy.IsSteadyStateRefreshFrame()] {
        RTC_DCHECK_RUN_ON(encoder_queue_.get());

        if (temporal_index == 0 || !qcm_ignore_enhancement_temporal_layers_) {
          // Check if the encoded image has reached target quality.
          bool at_target_quality =
              quality_convergence_controller_.AddSampleAndCheckTargetQuality(
                  simulcast_index, qp, is_steady_state_refresh_frame);

          // Let the frame cadence adapter know about quality convergence.
          if (frame_cadence_adapter_) {
            frame_cadence_adapter_->UpdateLayerQualityConvergence(
                simulcast_index, at_target_quality);
          }
        }

        // Currently, the internal quality scaler is used for VP9 instead of the
        // webrtc qp scaler (in the no-svc case or if only a single spatial
        // layer is encoded). It has to be explicitly detected and reported to
        // adaptation metrics.
        if (!send_codec_.IsMixedCodec() &&
            codec_type == VideoCodecType::kVideoCodecVP9 &&
            send_codec_.VP9()->automaticResizeOn) {
          unsigned int expected_width = send_codec_.width;
          unsigned int expected_height = send_codec_.height;
          int num_active_layers = 0;
          for (int i = 0; i < send_codec_.VP9()->numberOfSpatialLayers; ++i) {
            if (send_codec_.spatialLayers[i].active) {
              ++num_active_layers;
              expected_width = send_codec_.spatialLayers[i].width;
              expected_height = send_codec_.spatialLayers[i].height;
            }
          }
          RTC_DCHECK_LE(num_active_layers, 1)
              << "VP9 quality scaling is enabled for "
                 "SVC with several active layers.";
          encoder_stats_observer_->OnEncoderInternalScalerUpdate(
              image_width < expected_width || image_height < expected_height);
        }
      });

  // Encoded is called on whatever thread the real encoder implementation run
  // on. In the case of hardware encoders, there might be several encoders
  // running in parallel on different threads.
  encoder_stats_observer_->OnSendEncodedImage(image_copy, codec_specific_info);

  std::unique_ptr<CodecSpecificInfo> codec_specific_info_copy;
  if (codec_specific_info && frame_instrumentation_generator_) {
    std::optional<FrameInstrumentationData> frame_instrumentation_data =
        frame_instrumentation_generator_->OnEncodedImage(image_copy);
    RTC_CHECK(!codec_specific_info->frame_instrumentation_data.has_value())
        << "CodecSpecificInfo must not have frame_instrumentation_data set.";
    if (frame_instrumentation_data.has_value()) {
      codec_specific_info_copy =
          std::make_unique<CodecSpecificInfo>(*codec_specific_info);
      codec_specific_info_copy->frame_instrumentation_data =
          std::move(frame_instrumentation_data);
      codec_specific_info = codec_specific_info_copy.get();
    }
  }
  EncodedImageCallback::Result result =
      sink_->OnEncodedImage(image_copy, codec_specific_info);

  // We are only interested in propagating the meta-data about the image, not
  // encoded data itself, to the post encode function. Since we cannot be sure
  // the pointer will still be valid when run on the task queue, set it to null.
  DataSize frame_size = DataSize::Bytes(image_copy.size());
  image_copy.ClearEncodedData();

  int temporal_index = 0;
  if (encoded_image.TemporalIndex()) {
    // Give precedence to the metadata on EncodedImage, if available.
    temporal_index = *encoded_image.TemporalIndex();
  } else if (codec_specific_info) {
    if (codec_specific_info->codecType == kVideoCodecVP9) {
      temporal_index = codec_specific_info->codecSpecific.VP9.temporal_idx;
    } else if (codec_specific_info->codecType == kVideoCodecVP8) {
      temporal_index = codec_specific_info->codecSpecific.VP8.temporalIdx;
    }
  }
  if (temporal_index == kNoTemporalIdx) {
    temporal_index = 0;
  }

  RunPostEncode(image_copy, env_.clock().CurrentTime().us(), temporal_index,
                frame_size);

  if (result.error == Result::OK) {
    // In case of an internal encoder running on a separate thread, the
    // decision to drop a frame might be a frame late and signaled via
    // atomic flag. This is because we can't easily wait for the worker thread
    // without risking deadlocks, eg during shutdown when the worker thread
    // might be waiting for the internal encoder threads to stop.
    if (pending_frame_drops_.load() > 0) {
      int pending_drops = pending_frame_drops_.fetch_sub(1);
      RTC_DCHECK_GT(pending_drops, 0);
      result.drop_next_frame = true;
    }
  }

  return result;
}

void VideoStreamEncoder::OnFrameDropped(uint32_t rtp_timestamp,
                                        int spatial_id,
                                        bool is_end_of_temporal_unit) {
  sink_->OnFrameDropped(rtp_timestamp, spatial_id, is_end_of_temporal_unit);
  encoder_queue_->PostTask([this, rtp_timestamp, is_end_of_temporal_unit] {
    RTC_DCHECK_RUN_ON(encoder_queue_.get());
    stream_resource_manager_.OnFrameDropped(
        VideoStreamEncoderObserver::DropReason::kEncoder);
    // If this is the end of the temporal unit, signal frame instrumentation
    // that any reference to this frame can be released.
    if (frame_instrumentation_generator_ && is_end_of_temporal_unit) {
      frame_instrumentation_generator_->OnFrameReleased(rtp_timestamp);
    }
    encoder_stats_observer_->OnFrameDropped(
        VideoStreamEncoderObserver::DropReason::kEncoder);
  });
}

DataRate VideoStreamEncoder::UpdateTargetBitrate(DataRate target_bitrate,
                                                 double cwnd_reduce_ratio) {
  RTC_DCHECK_RUN_ON(encoder_queue_.get());
  DataRate updated_target_bitrate = target_bitrate;

  // Drop frames when congestion window pushback ratio is larger than 1
  // percent and target bitrate is larger than codec min bitrate.
  // When target_bitrate is 0 means codec is paused, skip frame dropping.
  if (cwnd_reduce_ratio > 0.01 && target_bitrate.bps() > 0 &&
      target_bitrate.bps() > send_codec_.minBitrate * 1000) {
    int reduce_bitrate_bps = std::min(
        static_cast<int>(target_bitrate.bps() * cwnd_reduce_ratio),
        static_cast<int>(target_bitrate.bps() - send_codec_.minBitrate * 1000));
    if (reduce_bitrate_bps > 0) {
      // At maximum the congestion window can drop 1/2 frames.
      cwnd_frame_drop_interval_ = std::max(
          2, static_cast<int>(target_bitrate.bps() / reduce_bitrate_bps));
      // Reduce target bitrate accordingly.
      updated_target_bitrate =
          target_bitrate - (target_bitrate / cwnd_frame_drop_interval_.value());
      return updated_target_bitrate;
    }
  }
  cwnd_frame_drop_interval_.reset();
  return updated_target_bitrate;
}

void VideoStreamEncoder::OnBitrateUpdated(DataRate target_bitrate,
                                          DataRate link_allocation,
                                          uint8_t fraction_lost,
                                          int64_t round_trip_time_ms,
                                          double cwnd_reduce_ratio) {
  RTC_DCHECK_GE(link_allocation, target_bitrate);
  if (!encoder_queue_->IsCurrent()) {
    encoder_queue_->PostTask([this, target_bitrate, link_allocation,
                              fraction_lost, round_trip_time_ms,
                              cwnd_reduce_ratio] {
      DataRate updated_target_bitrate =
          UpdateTargetBitrate(target_bitrate, cwnd_reduce_ratio);
      OnBitrateUpdated(updated_target_bitrate, link_allocation, fraction_lost,
                       round_trip_time_ms, cwnd_reduce_ratio);
    });
    return;
  }
  RTC_DCHECK_RUN_ON(encoder_queue_.get());

  const bool video_is_suspended = target_bitrate == DataRate::Zero();
  const bool video_suspension_changed = video_is_suspended != EncoderPaused();

  if (!video_is_suspended && encoder_switch_request_callback_ &&
      encoder_selector_) {
    if (auto encoder = encoder_selector_->OnAvailableBitrate(link_allocation)) {
      encoder_switch_request_callback_(*encoder,
                                       /*allow_default_fallback=*/false);
    }
  }

  RTC_DCHECK(sink_) << "sink_ must be set before the encoder is active.";

  RTC_LOG(LS_VERBOSE) << "OnBitrateUpdated, bitrate " << target_bitrate.bps()
                      << " link allocation bitrate = " << link_allocation.bps()
                      << " packet loss " << static_cast<int>(fraction_lost)
                      << " rtt " << round_trip_time_ms;

  if (encoder_) {
    encoder_->OnPacketLossRateUpdate(static_cast<float>(fraction_lost) / 256.f);
    encoder_->OnRttUpdate(round_trip_time_ms);
  }

  uint32_t framerate_fps = GetInputFramerateFps();
  frame_dropper_.SetRates((target_bitrate.bps() + 500) / 1000, framerate_fps);

  EncoderRateSettings new_rate_settings{VideoBitrateAllocation(),
                                        static_cast<double>(framerate_fps),
                                        link_allocation, target_bitrate};
  SetEncoderRates(UpdateBitrateAllocation(new_rate_settings));

  if (target_bitrate.bps() != 0)
    encoder_target_bitrate_bps_ = target_bitrate.bps();

  stream_resource_manager_.SetTargetBitrate(target_bitrate);

  if (video_suspension_changed) {
    RTC_LOG(LS_INFO) << "Video suspend state changed to: "
                     << (video_is_suspended ? "suspended" : "not suspended");
    encoder_stats_observer_->OnSuspendChange(video_is_suspended);

    if (!video_is_suspended && pending_frame_ &&
        !DropDueToSize(pending_frame_->size())) {
      // A pending stored frame can be processed.
      int64_t pending_time_us =
          env_.clock().CurrentTime().us() - pending_frame_post_time_us_;
      if (pending_time_us < kPendingFrameTimeoutMs * 1000)
        EncodeVideoFrame(*pending_frame_, pending_frame_post_time_us_);
      pending_frame_.reset();
    } else if (!video_is_suspended && !pending_frame_ &&
               encoder_paused_and_dropped_frame_) {
      // A frame was enqueued during pause-state, but since it was a native
      // frame we could not store it in `pending_frame_` so request a
      // refresh-frame instead.
      RequestRefreshFrame();
    }
  }
}

bool VideoStreamEncoder::DropDueToSize(uint32_t source_pixel_count) const {
  if (!encoder_ || !stream_resource_manager_.DropInitialFrames() ||
      !encoder_target_bitrate_bps_ ||
      !stream_resource_manager_.SingleActiveStreamPixels()) {
    return false;
  }

  int pixel_count = std::min(
      source_pixel_count, *stream_resource_manager_.SingleActiveStreamPixels());

  uint32_t bitrate_bps =
      stream_resource_manager_.UseBandwidthAllocationBps().value_or(
          encoder_target_bitrate_bps_.value());

  std::optional<VideoEncoder::ResolutionBitrateLimits> encoder_bitrate_limits =
      GetEncoderInfoWithBitrateLimitUpdate(encoder_->GetEncoderInfo(),
                                           encoder_config_)
          .GetEncoderBitrateLimitsForResolution(pixel_count);

  if (encoder_bitrate_limits.has_value()) {
    // Use bitrate limits provided by encoder.
    return bitrate_bps <
           static_cast<uint32_t>(encoder_bitrate_limits->min_start_bitrate_bps);
  }

  if (bitrate_bps < 300000 /* qvga */) {
    return pixel_count > 320 * 240;
  } else if (bitrate_bps < 500000 /* vga */) {
    return pixel_count > 640 * 480;
  }
  return false;
}

void VideoStreamEncoder::OnVideoSourceRestrictionsUpdated(
    VideoSourceRestrictions restrictions,
    const VideoAdaptationCounters& adaptation_counters,
    scoped_refptr<Resource> reason,
    const VideoSourceRestrictions& unfiltered_restrictions) {
  RTC_DCHECK_RUN_ON(encoder_queue_.get());
  RTC_LOG(LS_INFO) << "Updating sink restrictions from "
                   << (reason ? reason->Name() : std::string("<null>"))
                   << " to " << restrictions.ToString();

  if (frame_cadence_adapter_) {
    frame_cadence_adapter_->UpdateVideoSourceRestrictions(
        restrictions.max_frame_rate());
  }

  bool max_pixels_updated =
      (latest_restrictions_.has_value()
           ? latest_restrictions_->max_pixels_per_frame()
           : std::nullopt) != restrictions.max_pixels_per_frame();

  // TODO(webrtc:14451) Split video_source_sink_controller_
  // so that ownership on restrictions/wants is kept on &encoder_queue_
  latest_restrictions_ = restrictions;

  // When the `scale_resolution_down_to` API is used, we need to reconfigure any
  // time the restricted resolution is updated. When that API isn't used, the
  // encoder settings are relative to the frame size and reconfiguration happens
  // automatically on new frame size and we don't need to reconfigure here.
  if (encoder_ && max_pixels_updated &&
      encoder_config_.HasScaleResolutionDownTo()) {
    // The encoder will be reconfigured on the next frame.
    pending_encoder_reconfiguration_ = true;
  }

  worker_queue_->PostTask(SafeTask(
      task_safety_.flag(),
      [this, restrictions = std::move(restrictions)]() mutable {
        RTC_DCHECK_RUN_ON(worker_queue_);
        video_source_sink_controller_.SetRestrictions(std::move(restrictions));
        video_source_sink_controller_.PushSourceSinkSettings();
      }));
}

void VideoStreamEncoder::RunPostEncode(const EncodedImage& encoded_image,
                                       int64_t time_sent_us,
                                       int temporal_index,
                                       DataSize frame_size) {
  if (!encoder_queue_->IsCurrent()) {
    encoder_queue_->PostTask([this, encoded_image, time_sent_us, temporal_index,
                              frame_size] {
      RunPostEncode(encoded_image, time_sent_us, temporal_index, frame_size);
    });
    return;
  }

  RTC_DCHECK_RUN_ON(encoder_queue_.get());

  std::optional<int> encode_duration_us;
  if (encoded_image.timing_.flags != VideoSendTiming::kInvalid) {
    encode_duration_us =
        TimeDelta::Millis(encoded_image.timing_.encode_finish_ms -
                          encoded_image.timing_.encode_start_ms)
            .us();
  }

  // Run post encode tasks, such as overuse detection and frame rate/drop
  // stats for internal encoders.
  const bool keyframe = encoded_image.IsKey();

  if (!frame_size.IsZero()) {
    frame_dropper_.Fill(frame_size.bytes(), !keyframe);
  }

  stream_resource_manager_.OnEncodeCompleted(encoded_image, time_sent_us,
                                             encode_duration_us, frame_size);
  if (bitrate_adjuster_) {
    // We could either have simulcast layers or spatial layers.
    // TODO(https://crbug.com/webrtc/14891): If we want to support a mix of
    // simulcast and SVC we'll also need to consider the case where we have both
    // simulcast and spatial indices.
    int stream_index = std::max(encoded_image.SimulcastIndex().value_or(0),
                                encoded_image.SpatialIndex().value_or(0));
    bitrate_adjuster_->OnEncodedFrame(frame_size, stream_index, temporal_index);
  }
}

void VideoStreamEncoder::ReleaseEncoder() {
  if (!encoder_ || !encoder_initialized_) {
    return;
  }
  encoder_->Release();
  encoder_initialized_ = false;
  frame_instrumentation_generator_.reset();
  TRACE_EVENT0("webrtc", "VCMGenericEncoder::Release");
}

// RTC_RUN_ON(&encoder_queue_)
void VideoStreamEncoder::ProcessDroppedFrame(
    const VideoFrame& frame,
    VideoStreamEncoderObserver::DropReason reason) {
  accumulated_update_rect_.Union(frame.update_rect());
  accumulated_update_rect_is_valid_ &= frame.has_update_rect();
  stream_resource_manager_.OnFrameDropped(reason);
  encoder_stats_observer_->OnFrameDropped(reason);
  if (reason == VideoStreamEncoderObserver::DropReason::kMediaOptimization &&
      bitrate_adjuster_) {
    bitrate_adjuster_->OnFrameDropped();
  }
}

}  // namespace webrtc
