/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "video/config/video_encoder_config.h"

#include <cstddef>
#include <optional>
#include <string>

#include "api/video/video_codec_type.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "rtc_base/checks.h"
#include "rtc_base/strings/string_builder.h"

namespace webrtc {
VideoStream::VideoStream()
    : width(0),
      height(0),
      max_framerate(-1),
      min_bitrate_bps(-1),
      target_bitrate_bps(-1),
      max_bitrate_bps(-1),
      scale_resolution_down_by(-1.),
      max_qp(-1),
      num_temporal_layers(std::nullopt),
      active(true) {}
VideoStream::VideoStream(const VideoStream& other) = default;

VideoStream::~VideoStream() = default;

std::string VideoStream::ToString() const {
  StringBuilder ss;
  ss << "{width: " << width;
  ss << ", height: " << height;
  ss << ", max_framerate: " << max_framerate;
  ss << ", min_bitrate_bps:" << min_bitrate_bps;
  ss << ", target_bitrate_bps:" << target_bitrate_bps;
  ss << ", max_bitrate_bps:" << max_bitrate_bps;
  ss << ", max_qp: " << max_qp;
  ss << ", num_temporal_layers: " << num_temporal_layers.value_or(1);
  ss << ", bitrate_priority: " << bitrate_priority.value_or(0);
  ss << ", active: " << active;
  ss << ", scale_down_by: " << scale_resolution_down_by;
  if (scale_resolution_down_to.has_value()) {
    ss << ", scale_down_to: " << scale_resolution_down_to->width << "x"
       << scale_resolution_down_to->height;
  }
  if (scalability_mode.has_value()) {
    ss << ", scalability_mode: " << ScalabilityModeToString(*scalability_mode);
  }
  if (encoding_complexity.has_value()) {
    ss << ", encoding_complexity: " << static_cast<int>(*encoding_complexity);
  }
  ss << '}';
  return ss.Release();
}

VideoEncoderConfig::VideoEncoderConfig()
    : codec_type(kVideoCodecGeneric),
      video_format("Unset"),
      content_type(ContentType::kRealtimeVideo),
      frame_drop_enabled(false),
      encoder_specific_settings(nullptr),
      min_transmit_bitrate_bps(0),
      max_bitrate_bps(0),
      bitrate_priority(1.0),
      number_of_streams(0),
      legacy_conference_mode(false),
      is_quality_scaling_allowed(false),
      allow_zero_hertz_video(false),
      max_qp(-1) {}

VideoEncoderConfig::VideoEncoderConfig(VideoEncoderConfig&&) = default;

VideoEncoderConfig::~VideoEncoderConfig() = default;

std::string VideoEncoderConfig::ToString() const {
  StringBuilder ss;
  ss << "{codec_type: " << CodecTypeToPayloadString(codec_type);
  ss << ", content_type: ";
  switch (content_type) {
    case ContentType::kRealtimeVideo:
      ss << "kRealtimeVideo";
      break;
    case ContentType::kScreen:
      ss << "kScreenshare";
      break;
  }
  ss << ", frame_drop_enabled: " << frame_drop_enabled;
  ss << ", encoder_specific_settings: ";
  ss << (encoder_specific_settings != nullptr ? "(ptr)" : "NULL");
  ss << ", min_transmit_bitrate_bps: " << min_transmit_bitrate_bps;
  ss << ", number_of_streams: " << number_of_streams;
  ss << ", legacy_conference_mode: " << legacy_conference_mode;
  ss << ", is_quality_scaling_allowed: " << is_quality_scaling_allowed;
  ss << ", allow_zero_hertz_video: " << allow_zero_hertz_video;
  ss << ", max_qp: " << max_qp;
  for (size_t n = 0; n < simulcast_layers.size(); ++n) {
    ss << ", simulcast_layers[" << n << "]: " << simulcast_layers[n].ToString();
  }
  ss << '}';
  return ss.Release();
}

bool VideoEncoderConfig::HasScaleResolutionDownTo() const {
  for (const VideoStream& simulcast_layer : simulcast_layers) {
    if (simulcast_layer.scale_resolution_down_to.has_value()) {
      return true;
    }
  }
  return false;
}

bool VideoEncoderConfig::HasScaleResolutionDownBy() const {
  for (const VideoStream& simulcast_layer : simulcast_layers) {
    if (simulcast_layer.scale_resolution_down_by >= 1.0) {
      return true;
    }
  }
  return false;
}

VideoEncoderConfig::VideoEncoderConfig(const VideoEncoderConfig&) = default;

SdpVideoFormat VideoEncoderConfig::GetSimulcastVideoFormat(
    size_t stream_index) const {
  if (stream_index >= simulcast_layers.size()) {
    return video_format;
  }
  return simulcast_layers[stream_index].video_format.value_or(video_format);
}

void VideoEncoderConfig::EncoderSpecificSettings::FillEncoderSpecificSettings(
    VideoCodec* codec) const {
  if (codec->codecType == kVideoCodecVP8) {
    FillVideoCodecVp8(codec->VP8());
  } else if (codec->codecType == kVideoCodecVP9) {
    FillVideoCodecVp9(codec->VP9());
  } else if (codec->codecType == kVideoCodecAV1) {
    FillVideoCodecAv1(codec->AV1());
  } else {
    RTC_DCHECK_NOTREACHED()
        << "Encoder specifics set/used for unknown codec type.";
  }
}

void VideoEncoderConfig::EncoderSpecificSettings::FillVideoCodecVp8(
    VideoCodecVP8* vp8_settings) const {
  RTC_DCHECK_NOTREACHED();
}

void VideoEncoderConfig::EncoderSpecificSettings::FillVideoCodecVp9(
    VideoCodecVP9* vp9_settings) const {
  RTC_DCHECK_NOTREACHED();
}

void VideoEncoderConfig::EncoderSpecificSettings::FillVideoCodecAv1(
    VideoCodecAV1* av1_settings) const {
  RTC_DCHECK_NOTREACHED();
}

VideoEncoderConfig::Vp8EncoderSpecificSettings::Vp8EncoderSpecificSettings(
    const VideoCodecVP8& specifics)
    : specifics_(specifics) {}

void VideoEncoderConfig::Vp8EncoderSpecificSettings::FillVideoCodecVp8(
    VideoCodecVP8* vp8_settings) const {
  *vp8_settings = specifics_;
}

VideoEncoderConfig::Vp9EncoderSpecificSettings::Vp9EncoderSpecificSettings(
    const VideoCodecVP9& specifics)
    : specifics_(specifics) {}

void VideoEncoderConfig::Vp9EncoderSpecificSettings::FillVideoCodecVp9(
    VideoCodecVP9* vp9_settings) const {
  *vp9_settings = specifics_;
}

VideoEncoderConfig::Av1EncoderSpecificSettings::Av1EncoderSpecificSettings(
    const VideoCodecAV1& specifics)
    : specifics_(specifics) {}

void VideoEncoderConfig::Av1EncoderSpecificSettings::FillVideoCodecAv1(
    VideoCodecAV1* av1_settings) const {
  *av1_settings = specifics_;
}

}  // namespace webrtc
