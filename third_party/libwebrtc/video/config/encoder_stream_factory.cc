/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "video/config/encoder_stream_factory.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <optional>
#include <vector>

#include "absl/algorithm/container.h"
#include "api/field_trials_view.h"
#include "api/units/data_rate.h"
#include "api/video/resolution.h"
#include "api/video/video_adapter.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_source_interface.h"
#include "api/video_codecs/spatial_layer.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "call/adaptation/video_source_restrictions.h"
#include "media/base/media_constants.h"
#include "modules/video_coding/codecs/vp9/svc_config.h"
#include "modules/video_coding/utility/simulcast_utility.h"
#include "rtc_base/checks.h"
#include "rtc_base/experiments/min_video_bitrate_experiment.h"
#include "rtc_base/experiments/normalize_simulcast_size_experiment.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "video/config/simulcast.h"
#include "video/config/video_encoder_config.h"

namespace webrtc {
namespace {
constexpr int kMinLayerSize = 16;

Resolution ScaleResolutionDownBy(const Resolution& resolution,
                                 double scale_down_by) {
  if (resolution.width <= kMinLayerSize || resolution.height <= kMinLayerSize) {
    return resolution;
  }
  int short_side = std::min(resolution.width / scale_down_by,
                            resolution.height / scale_down_by);
  if (short_side < kMinLayerSize) {
    // Adjust scale factor to make the short side equal to kMinLayerSize.
    scale_down_by =
        std::min(resolution.width / static_cast<double>(kMinLayerSize),
                 resolution.height / static_cast<double>(kMinLayerSize));
  }

  return {
      .width = static_cast<int>(std::round(resolution.width / scale_down_by)),
      .height =
          static_cast<int>(std::round(resolution.height / scale_down_by))};
}

bool PowerOfTwo(int value) {
  return (value > 0) && ((value & (value - 1)) == 0);
}

bool IsScaleFactorsPowerOfTwo(const VideoEncoderConfig& config) {
  for (const auto& layer : config.simulcast_layers) {
    double scale = std::max(layer.scale_resolution_down_by, 1.0);
    if (std::round(scale) != scale || !PowerOfTwo(scale)) {
      return false;
    }
  }
  return true;
}

bool IsTemporalLayersSupported(VideoCodecType codec_type) {
  return codec_type == VideoCodecType::kVideoCodecVP8 ||
         codec_type == VideoCodecType::kVideoCodecVP9 ||
         codec_type == VideoCodecType::kVideoCodecAV1 ||
         codec_type == VideoCodecType::kVideoCodecH265;
}

size_t FindRequiredActiveLayers(const VideoEncoderConfig& encoder_config) {
  // Need enough layers so that at least the first active one is present.
  for (size_t i = 0; i < encoder_config.number_of_streams; ++i) {
    if (encoder_config.simulcast_layers[i].active) {
      return i + 1;
    }
  }
  return 0;
}

// The selected thresholds for QVGA and VGA corresponded to a QP around 10.
// The change in QP declined above the selected bitrates.
int GetMaxDefaultVideoBitrateKbps(int width, int height, bool is_screenshare) {
  int max_bitrate;
  if (width * height <= 320 * 240) {
    max_bitrate = 600;
  } else if (width * height <= 640 * 480) {
    max_bitrate = 1700;
  } else if (width * height <= 960 * 540) {
    max_bitrate = 2000;
  } else {
    max_bitrate = 2500;
  }
  if (is_screenshare)
    max_bitrate = std::max(max_bitrate, 1200);
  return max_bitrate;
}

int GetDefaultMaxQp(VideoCodecType codec_type) {
  switch (codec_type) {
    case kVideoCodecH264:
    case kVideoCodecH265:
      return kDefaultVideoMaxQpH26x;
    case kVideoCodecVP8:
    case kVideoCodecVP9:
    case kVideoCodecGeneric:
      return kDefaultVideoMaxQpVpx;
    case kVideoCodecAV1:
      return kDefaultVideoMaxQpAv1;
  }
}

Resolution NormalizeResolution(const Resolution& resolution,
                               const VideoEncoderConfig& encoder_config,
                               int max_num_layers,
                               const FieldTrialsView& trials) {
  if (resolution.width < kMinLayerSize || resolution.height < kMinLayerSize) {
    // Resolution is already too low. Avoid further adjustments.
    return resolution;
  }

  std::optional<int> log2_scale_factor =
      NormalizeSimulcastSizeExperiment::GetBase2Exponent(trials);
  if (log2_scale_factor && (resolution.width < (1 << *log2_scale_factor) ||
                            resolution.height < (1 << *log2_scale_factor))) {
    // The experimental scale factor is too large for the given input
    // resolution. Fall back to the default logic.
    log2_scale_factor = std::nullopt;
  }

  if (!log2_scale_factor) {
    // If `scale_resolution_down_by` is not set, default to 1/2 scaling. If
    // `scale_resolution_down_by` is set for one or more simulcast layers,
    // normalize the resolution to be a multiple of the maximum scale factor if
    // both of the following conditions are true:
    // 1. All configured scale factors are power of two.
    // 2. The maximum scale factor is greater than one.
    // Otherwise, keep the resolution unchanged.
    if (!encoder_config.HasScaleResolutionDownBy()) {
      log2_scale_factor = max_num_layers - 1;
    } else if (IsScaleFactorsPowerOfTwo(encoder_config)) {
      auto smallest_layer = absl::c_max_element(
          encoder_config.simulcast_layers, [](const auto& a, const auto& b) {
            return a.scale_resolution_down_by < b.scale_resolution_down_by;
          });

      double max_scale_factor = smallest_layer->scale_resolution_down_by;
      if (max_scale_factor > 1.0) {
        log2_scale_factor = static_cast<int>(std::log2(max_scale_factor));
      }
    }
  }

  if (!log2_scale_factor || (resolution.width < (1 << *log2_scale_factor) ||
                             resolution.height < (1 << *log2_scale_factor))) {
    // Too large scale factor.
    return resolution;
  }

  return {.width = (resolution.width >> *log2_scale_factor)
                   << *log2_scale_factor,
          .height = (resolution.height >> *log2_scale_factor)
                    << *log2_scale_factor};
}

// Override bitrate limits and other stream settings with values from
// `encoder_config.simulcast_layers` which come from `RtpEncodingParameters`.
void OverrideStreamSettings(
    const VideoEncoderConfig& encoder_config,
    const std::optional<DataRate>& experimental_min_bitrate,
    std::vector<VideoStream>& layers) {
  RTC_DCHECK_LE(layers.size(), encoder_config.simulcast_layers.size());

  // Allow an experiment to override the minimum bitrate for the lowest
  // spatial layer. The experiment's configuration has the lowest priority.
  layers[0].min_bitrate_bps =
      experimental_min_bitrate
          .value_or(DataRate::BitsPerSec(kDefaultMinVideoBitrateBps))
          .bps<int>();

  const bool temporal_layers_supported =
      IsTemporalLayersSupported(encoder_config.codec_type);

  for (size_t i = 0; i < layers.size(); ++i) {
    const VideoStream& overrides = encoder_config.simulcast_layers[i];
    VideoStream& layer = layers[i];
    // Zero is a configured value meaning "do not send this layer"; only a
    // negative value means unset.
    const bool max_bitrate_configured = overrides.max_bitrate_bps >= 0;
    layer.active = overrides.active;
    layer.scalability_mode = overrides.scalability_mode;
    layer.scale_resolution_down_to = overrides.scale_resolution_down_to;
    layer.scale_resolution_down_by = overrides.scale_resolution_down_by;
    layer.encoding_complexity = overrides.encoding_complexity;
    // Update with configured num temporal layers if supported by codec.
    if (overrides.num_temporal_layers > 0 && temporal_layers_supported) {
      layer.num_temporal_layers = *overrides.num_temporal_layers;
    }
    if (overrides.max_framerate > 0) {
      layer.max_framerate = overrides.max_framerate;
    }
    // Update simulcast bitrates with configured min and max bitrate.
    if (overrides.min_bitrate_bps > 0) {
      layer.min_bitrate_bps = overrides.min_bitrate_bps;
    }
    if (max_bitrate_configured) {
      layer.max_bitrate_bps = overrides.max_bitrate_bps;
    }
    if (overrides.target_bitrate_bps > 0) {
      layer.target_bitrate_bps = overrides.target_bitrate_bps;
    }
    if (overrides.min_bitrate_bps > 0 && max_bitrate_configured) {
      // Min and max bitrate are configured.
      // Set target to 3/4 of the max bitrate (or to max if below min).
      if (overrides.target_bitrate_bps <= 0)
        layer.target_bitrate_bps = layer.max_bitrate_bps * 3 / 4;
      if (layer.target_bitrate_bps < layer.min_bitrate_bps)
        layer.target_bitrate_bps = layer.max_bitrate_bps;
    } else if (overrides.min_bitrate_bps > 0) {
      // Only min bitrate is configured, make sure target/max are above min.
      layer.target_bitrate_bps =
          std::max(layer.target_bitrate_bps, layer.min_bitrate_bps);
      layer.max_bitrate_bps =
          std::max(layer.max_bitrate_bps, layer.min_bitrate_bps);
    } else if (max_bitrate_configured) {
      // Only max bitrate is configured, make sure min/target are below max.
      // Keep target bitrate if it is set explicitly in encoding config.
      // Otherwise set target bitrate to 3/4 of the max bitrate
      // or the one calculated from GetSimulcastConfig() which is larger.
      layer.min_bitrate_bps =
          std::min(layer.min_bitrate_bps, layer.max_bitrate_bps);
      if (overrides.target_bitrate_bps <= 0) {
        layer.target_bitrate_bps =
            std::max(layer.target_bitrate_bps, layer.max_bitrate_bps * 3 / 4);
      }
      layer.target_bitrate_bps =
          std::clamp(layer.target_bitrate_bps, layer.min_bitrate_bps,
                     layer.max_bitrate_bps);
    }

    if (overrides.max_qp > 0) {
      layer.max_qp = overrides.max_qp;
    } else if (encoder_config.max_qp > 0) {
      layer.max_qp = encoder_config.max_qp;
    } else {
      layer.max_qp = GetDefaultMaxQp(encoder_config.codec_type);
    }
  }

  bool is_highest_layer_max_bitrate_configured =
      encoder_config.simulcast_layers[layers.size() - 1].max_bitrate_bps >= 0;
  bool is_screencast =
      encoder_config.content_type == VideoEncoderConfig::ContentType::kScreen;
  if (!is_screencast && !is_highest_layer_max_bitrate_configured &&
      encoder_config.max_bitrate_bps > 0) {
    // No application-configured maximum for the largest layer.
    // If there is bitrate leftover, give it to the largest layer.
    BoostMaxSimulcastLayer(DataRate::BitsPerSec(encoder_config.max_bitrate_bps),
                           &layers);
  }

  // Sort the layers by max_bitrate_bps, they might not always be from
  // smallest to biggest
  std::vector<size_t> index(layers.size());
  std::iota(index.begin(), index.end(), 0);
  absl::c_stable_sort(index, [&layers](size_t a, size_t b) {
    return layers[a].max_bitrate_bps < layers[b].max_bitrate_bps;
  });

  if (!layers[index[0]].active) {
    // Adjust min bitrate of the first active layer to allow it to go as low as
    // the lowest (now inactive) layer could.
    // Otherwise, if e.g. a single HD stream is active, it would have 600kbps
    // min bitrate, which would always be allocated to the stream.
    // This would lead to congested network, dropped frames and overall bad
    // experience.

    const int min_configured_bitrate = layers[index[0]].min_bitrate_bps;
    for (size_t i = 0; i < layers.size(); ++i) {
      if (layers[index[i]].active) {
        layers[index[i]].min_bitrate_bps = min_configured_bitrate;
        break;
      }
    }
  }
}

}  // namespace

EncoderStreamFactory::EncoderStreamFactory(
    const VideoEncoder::EncoderInfo& encoder_info,
    std::optional<VideoSourceRestrictions> restrictions)
    : encoder_info_requested_resolution_alignment_(
          encoder_info.requested_resolution_alignment),
      restrictions_(restrictions) {}

std::vector<VideoStream> EncoderStreamFactory::CreateEncoderStreams(
    const FieldTrialsView& trials,
    int frame_width,
    int frame_height,
    const VideoEncoderConfig& encoder_config) {
  RTC_DCHECK_GT(encoder_config.number_of_streams, 0);
  RTC_DCHECK_GE(encoder_config.simulcast_layers.size(),
                encoder_config.number_of_streams);

  const std::optional<DataRate> experimental_min_bitrate =
      GetExperimentalMinVideoBitrate(trials, encoder_config.codec_type);

  bool is_simulcast = (encoder_config.number_of_streams > 1);
  // If scalability mode was specified, don't treat {active,inactive,inactive}
  // as simulcast since the simulcast configuration assumes very low bitrates
  // on the first layer. This would prevent rampup of multiple spatial layers.
  // See https://crbug.com/webrtc/15041.
  if (is_simulcast &&
      encoder_config.simulcast_layers[0].scalability_mode.has_value()) {
    // Require at least one non-first layer to be active for is_simulcast=true.
    is_simulcast = false;
    for (size_t i = 1; i < encoder_config.simulcast_layers.size(); ++i) {
      if (encoder_config.simulcast_layers[i].active) {
        is_simulcast = true;
        break;
      }
    }
  }

  std::vector<VideoStream> streams;
  if (is_simulcast ||
      SimulcastUtility::IsConferenceModeScreenshare(encoder_config)) {
    streams = CreateSimulcastOrConferenceModeScreenshareStreams(
        trials, frame_width, frame_height, encoder_config,
        experimental_min_bitrate);
  } else {
    streams = CreateDefaultVideoStreams(
        frame_width, frame_height, encoder_config, experimental_min_bitrate);
  }

  // The bitrate priority currently implemented on a per-sender level, so we
  // just set it for the first simulcast layer.
  RTC_DCHECK_GT(streams.size(), 0);
  streams[0].bitrate_priority = encoder_config.bitrate_priority;

  return streams;
}

std::vector<VideoStream> EncoderStreamFactory::CreateDefaultVideoStreams(
    int width,
    int height,
    const VideoEncoderConfig& encoder_config,
    const std::optional<DataRate>& experimental_min_bitrate) const {
  bool is_screencast =
      encoder_config.content_type == VideoEncoderConfig::ContentType::kScreen;

  // The max bitrate specified by the API.
  // - `encoder_config.simulcast_layers[0].max_bitrate_bps` comes from the first
  //   RtpEncodingParamters, which is the encoding of this stream.
  // - `encoder_config.max_bitrate_bps` comes from SDP; "b=AS" or conditionally
  //   "x-google-max-bitrate".
  // If `api_max_bitrate_bps` has a value then it is non-negative.
  //
  // A configured max bitrate of zero is a valid value that means "do not
  // allocate bitrate to this encoding"; only a negative value means unset.
  // Treating zero as unset would silently uncap the encoding instead, see
  // https://w3c.github.io/webrtc-pc/#dom-rtcrtpencodingparameters-maxbitrate
  const bool encoding_max_bitrate_configured =
      encoder_config.simulcast_layers[0].max_bitrate_bps >= 0;
  std::optional<int> api_max_bitrate_bps;
  if (encoding_max_bitrate_configured) {
    api_max_bitrate_bps = encoder_config.simulcast_layers[0].max_bitrate_bps;
  }
  if (encoder_config.max_bitrate_bps > 0) {
    api_max_bitrate_bps =
        api_max_bitrate_bps.has_value()
            ? std::min(encoder_config.max_bitrate_bps, *api_max_bitrate_bps)
            : encoder_config.max_bitrate_bps;
  }

  // For unset max bitrates set default bitrate for non-simulcast.
  int max_bitrate_bps =
      api_max_bitrate_bps.has_value()
          ? *api_max_bitrate_bps
          : GetMaxDefaultVideoBitrateKbps(width, height, is_screencast) * 1000;

  int min_bitrate_bps =
      experimental_min_bitrate
          ? saturated_cast<int>(experimental_min_bitrate->bps())
          : kDefaultMinVideoBitrateBps;
  if (encoder_config.simulcast_layers[0].min_bitrate_bps > 0) {
    // Use set min bitrate.
    min_bitrate_bps = encoder_config.simulcast_layers[0].min_bitrate_bps;
    // If only min bitrate is configured, make sure max is above min.
    if (!api_max_bitrate_bps.has_value())
      max_bitrate_bps = std::max(min_bitrate_bps, max_bitrate_bps);
  }
  int max_framerate = (encoder_config.simulcast_layers[0].max_framerate > 0)
                          ? encoder_config.simulcast_layers[0].max_framerate
                          : kDefaultVideoMaxFramerate;

  VideoStream layer;
  layer.max_framerate = max_framerate;
  layer.scale_resolution_down_by =
      encoder_config.simulcast_layers[0].scale_resolution_down_by;
  layer.scale_resolution_down_to =
      encoder_config.simulcast_layers[0].scale_resolution_down_to;
  // Note: VP9 seems to have be sending if any layer is active,
  // (see `UpdateSendState`) and still use parameters only from
  // encoder_config.simulcast_layers[0].
  layer.active = absl::c_any_of(encoder_config.simulcast_layers,
                                [](const auto& layer) { return layer.active; });

  if (encoder_config.simulcast_layers[0].scale_resolution_down_to) {
    auto res = GetLayerResolutionFromScaleResolutionDownTo(
        width, height,
        *encoder_config.simulcast_layers[0].scale_resolution_down_to);
    layer.width = res.width;
    layer.height = res.height;
  } else if (encoder_config.simulcast_layers[0].scale_resolution_down_by > 1.) {
    Resolution res = ScaleResolutionDownBy(
        {.width = width, .height = height},
        encoder_config.simulcast_layers[0].scale_resolution_down_by);
    layer.width = res.width;
    layer.height = res.height;
  } else {
    layer.width = width;
    layer.height = height;
  }

  if (encoder_config.codec_type == VideoCodecType::kVideoCodecVP9) {
    RTC_DCHECK(encoder_config.encoder_specific_settings);
    // Use VP9 SVC layering from codec settings which might be initialized
    // though field trial in ConfigureVideoEncoderSettings.
    VideoCodecVP9 vp9_settings;
    encoder_config.encoder_specific_settings->FillVideoCodecVp9(&vp9_settings);
    layer.num_temporal_layers = vp9_settings.numberOfTemporalLayers;

    // Number of spatial layers is signalled differently from different call
    // sites (sigh), pick the max as we are interested in the upper bound.
    int num_spatial_layers =
        std::max({encoder_config.simulcast_layers.size(),
                  encoder_config.spatial_layers.size(),
                  size_t{vp9_settings.numberOfSpatialLayers}});

    if (width * height > 0 &&
        (layer.num_temporal_layers > 1u || num_spatial_layers > 1)) {
      // In SVC mode, the VP9 max bitrate is determined by SvcConfig, instead of
      // GetMaxDefaultVideoBitrateKbps().
      std::vector<SpatialLayer> svc_layers =
          GetSvcConfig(width, height, max_framerate,
                       /*first_active_layer=*/0, num_spatial_layers,
                       *layer.num_temporal_layers, is_screencast);
      int sum_max_bitrates_kbps = 0;
      for (const SpatialLayer& spatial_layer : svc_layers) {
        sum_max_bitrates_kbps += spatial_layer.maxBitrate;
      }
      RTC_DCHECK_GE(sum_max_bitrates_kbps, 0);
      if (!api_max_bitrate_bps.has_value()) {
        max_bitrate_bps = sum_max_bitrates_kbps * 1000;
      } else if (!encoding_max_bitrate_configured) {
        // Encoding max bitrate is kept if configured.
        max_bitrate_bps =
            std::min(max_bitrate_bps, sum_max_bitrates_kbps * 1000);
      }
      if (max_bitrate_bps > 0) {
        max_bitrate_bps = std::max(min_bitrate_bps, max_bitrate_bps);
      }
    }
  }

  // In the case that the application sets a max bitrate that's lower than the
  // min bitrate, we adjust it down (see bugs.webrtc.org/9141).
  layer.min_bitrate_bps = std::min(min_bitrate_bps, max_bitrate_bps);
  if (encoder_config.simulcast_layers[0].target_bitrate_bps <= 0) {
    layer.target_bitrate_bps = max_bitrate_bps;
  } else {
    layer.target_bitrate_bps = std::min(
        encoder_config.simulcast_layers[0].target_bitrate_bps, max_bitrate_bps);
  }
  layer.max_bitrate_bps = max_bitrate_bps;
  layer.bitrate_priority = encoder_config.bitrate_priority;

  if (encoder_config.max_qp > 0) {
    layer.max_qp = encoder_config.max_qp;
  } else {
    layer.max_qp = GetDefaultMaxQp(encoder_config.codec_type);
  }

  if (IsTemporalLayersSupported(encoder_config.codec_type)) {
    // Use configured number of temporal layers if set.
    if (encoder_config.simulcast_layers[0].num_temporal_layers) {
      layer.num_temporal_layers =
          *encoder_config.simulcast_layers[0].num_temporal_layers;
    }
  }
  layer.scalability_mode = encoder_config.simulcast_layers[0].scalability_mode;
  layer.encoding_complexity =
      encoder_config.simulcast_layers[0].encoding_complexity;
  return {layer};
}

std::vector<VideoStream>
EncoderStreamFactory::CreateSimulcastOrConferenceModeScreenshareStreams(
    const FieldTrialsView& trials,
    int width,
    int height,
    const VideoEncoderConfig& encoder_config,
    const std::optional<DataRate>& experimental_min_bitrate) const {
  std::vector<Resolution> resolutions =
      GetStreamResolutions(trials, width, height, encoder_config);

  // Use legacy simulcast screenshare if conference mode is explicitly enabled
  // or use the regular simulcast configuration path which is generic.
  std::vector<VideoStream> layers = GetSimulcastConfig(
      resolutions,
      SimulcastUtility::IsConferenceModeScreenshare(encoder_config),
      IsTemporalLayersSupported(encoder_config.codec_type), trials,
      encoder_config.codec_type);

  OverrideStreamSettings(encoder_config, experimental_min_bitrate, layers);

  return layers;
}

Resolution EncoderStreamFactory::GetLayerResolutionFromScaleResolutionDownTo(
    int frame_width,
    int frame_height,
    Resolution scale_resolution_down_to) const {
  // Make frame and `scale_resolution_down_to` have matching orientation.
  if ((frame_width < frame_height) !=
      (scale_resolution_down_to.width < scale_resolution_down_to.height)) {
    scale_resolution_down_to = {.width = scale_resolution_down_to.height,
                                .height = scale_resolution_down_to.width};
  }
  // Downscale by smallest scaling factor, if necessary.
  if (frame_width > 0 && frame_height > 0 &&
      (scale_resolution_down_to.width < frame_width ||
       scale_resolution_down_to.height < frame_height)) {
    double scale_factor = std::min(
        scale_resolution_down_to.width / static_cast<double>(frame_width),
        scale_resolution_down_to.height / static_cast<double>(frame_height));
    frame_width = std::round(frame_width * scale_factor);
    frame_height = std::round(frame_height * scale_factor);
  }
  Resolution frame = {.width = frame_width, .height = frame_height};

  // Maybe adapt further based on restrictions and encoder alignment.
  VideoAdapter adapter(encoder_info_requested_resolution_alignment_);
  adapter.OnOutputFormatRequest(frame.ToPair(), frame.PixelCount(),
                                std::nullopt);
  if (restrictions_) {
    VideoSinkWants wants;
    wants.is_active = true;
    wants.target_pixel_count = restrictions_->target_pixels_per_frame();
    wants.max_pixel_count =
        dchecked_cast<int>(restrictions_->max_pixels_per_frame().value_or(
            std::numeric_limits<int>::max()));
    wants.aggregates.emplace(VideoSinkWants::Aggregates());
    wants.resolution_alignment = encoder_info_requested_resolution_alignment_;
    adapter.OnSinkWants(wants);
  }
  int cropped_width, cropped_height;
  int out_width = 0, out_height = 0;
  if (!adapter.AdaptFrameResolution(frame_width, frame_height, 0,
                                    &cropped_width, &cropped_height, &out_width,
                                    &out_height)) {
    RTC_LOG(LS_ERROR) << "AdaptFrameResolution returned false!";
  }
  return {.width = out_width, .height = out_height};
}

std::vector<Resolution> EncoderStreamFactory::GetStreamResolutions(
    const FieldTrialsView& trials,
    int width,
    int height,
    const VideoEncoderConfig& encoder_config) const {
  std::vector<Resolution> resolutions;
  if (SimulcastUtility::IsConferenceModeScreenshare(encoder_config)) {
    for (size_t i = 0; i < encoder_config.number_of_streams; ++i) {
      resolutions.push_back({.width = width, .height = height});
    }
  } else {
    const bool has_scale_resolution_down_to =
        encoder_config.HasScaleResolutionDownTo();
    size_t min_num_layers = FindRequiredActiveLayers(encoder_config);
    size_t max_num_layers =
        !has_scale_resolution_down_to
            ? LimitSimulcastLayerCount(
                  min_num_layers, encoder_config.number_of_streams, width,
                  height, trials, encoder_config.codec_type)
            : encoder_config.number_of_streams;
    RTC_DCHECK_LE(max_num_layers, encoder_config.number_of_streams);

    // When the `scale_resolution_down_to` API is used, disable upper layers
    // that are bigger than what adaptation restrictions allow. For example if
    // restrictions are 540p, simulcast 180p:360p:720p becomes 180p:360p:- as
    // opposed to 180p:360p:540p. This makes CPU adaptation consistent with BW
    // adaptation (bitrate allocator disabling layers rather than downscaling)
    // and means we don't have to break power of two optimization paths (i.e.
    // S-modes based simulcast). Note that the lowest active layer is never
    // disabled.
    if (has_scale_resolution_down_to && restrictions_.has_value() &&
        restrictions_->max_pixels_per_frame().has_value()) {
      int max_pixels =
          dchecked_cast<int>(restrictions_->max_pixels_per_frame().value());
      int prev_pixel_count =
          encoder_config.simulcast_layers[0]
              .scale_resolution_down_to.value_or(Resolution())
              .PixelCount();
      std::optional<size_t> restricted_num_layers;
      for (size_t i = 1; i < max_num_layers; ++i) {
        int pixel_count = encoder_config.simulcast_layers[i]
                              .scale_resolution_down_to.value_or(Resolution())
                              .PixelCount();
        if (!restricted_num_layers.has_value() && max_pixels < pixel_count) {
          // Current layer is the highest layer allowed by restrictions.
          restricted_num_layers = i;
        }
        if (pixel_count < prev_pixel_count) {
          // Cannot limit layers because config is not lower-to-higher.
          restricted_num_layers = std::nullopt;
          break;
        }
        prev_pixel_count = pixel_count;
      }
      max_num_layers = std::max(min_num_layers,
                                restricted_num_layers.value_or(max_num_layers));
    }

    Resolution norm_resolution =
        NormalizeResolution({.width = width, .height = height}, encoder_config,
                            max_num_layers, trials);

    const bool has_scale_resolution_down_by =
        encoder_config.HasScaleResolutionDownBy();

    resolutions.resize(max_num_layers);
    for (size_t i = 0; i < max_num_layers; i++) {
      if (encoder_config.simulcast_layers[i]
              .scale_resolution_down_to.has_value()) {
        resolutions[i] = GetLayerResolutionFromScaleResolutionDownTo(
            width, height,
            *encoder_config.simulcast_layers[i].scale_resolution_down_to);
      } else if (has_scale_resolution_down_by) {
        const double scale_resolution_down_by = std::max(
            encoder_config.simulcast_layers[i].scale_resolution_down_by, 1.0);
        resolutions[i] =
            ScaleResolutionDownBy(norm_resolution, scale_resolution_down_by);
      } else {
        // Resolutions with default 1/2 scale factor, from low to high.
        resolutions[i].width =
            norm_resolution.width >> (max_num_layers - i - 1);
        resolutions[i].height =
            norm_resolution.height >> (max_num_layers - i - 1);
      }
    }
  }

  return resolutions;
}

}  // namespace webrtc
