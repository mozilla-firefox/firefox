/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "test/encoder_settings.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "api/field_trials_view.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/video/video_codec_type.h"
#include "api/video_codecs/sdp_video_format.h"
#include "call/rtp_config.h"
#include "call/video_receive_stream.h"
#include "call/video_send_stream.h"
#include "rtc_base/checks.h"
#include "video/config/video_encoder_config.h"

namespace webrtc {
namespace test {

const size_t DefaultVideoStreamFactory::kMaxNumberOfStreams;
const int DefaultVideoStreamFactory::kMaxBitratePerStream[] = {150000, 450000,
                                                               1500000};
const int DefaultVideoStreamFactory::kDefaultMinBitratePerStream[] = {
    30000, 200000, 700000};

// static
std::vector<VideoStream> CreateVideoStreams(
    int width,
    int height,
    const VideoEncoderConfig& encoder_config) {
  RTC_DCHECK(encoder_config.number_of_streams <=
             DefaultVideoStreamFactory::kMaxNumberOfStreams);

  std::vector<VideoStream> stream_settings(encoder_config.number_of_streams);

  int bitrate_left_bps = 0;
  if (encoder_config.max_bitrate_bps > 0) {
    bitrate_left_bps = encoder_config.max_bitrate_bps;
  } else {
    for (size_t stream_num = 0; stream_num < encoder_config.number_of_streams;
         ++stream_num) {
      bitrate_left_bps +=
          DefaultVideoStreamFactory::kMaxBitratePerStream[stream_num];
    }
  }

  for (size_t i = 0; i < encoder_config.number_of_streams; ++i) {
    stream_settings[i].width =
        (i + 1) * width / encoder_config.number_of_streams;
    stream_settings[i].height =
        (i + 1) * height / encoder_config.number_of_streams;
    stream_settings[i].max_framerate = 30;
    stream_settings[i].max_qp = 56;
    stream_settings[i].min_bitrate_bps =
        DefaultVideoStreamFactory::kDefaultMinBitratePerStream[i];

    // Use configured values instead of default values if set.
    const VideoStream stream = (i < encoder_config.simulcast_layers.size())
                                   ? encoder_config.simulcast_layers[i]
                                   : VideoStream();

    int max_bitrate_bps =
        stream.max_bitrate_bps > 0
            ? stream.max_bitrate_bps
            : DefaultVideoStreamFactory::kMaxBitratePerStream[i];
    max_bitrate_bps = std::min(bitrate_left_bps, max_bitrate_bps);

    int target_bitrate_bps = stream.target_bitrate_bps > 0
                                 ? stream.target_bitrate_bps
                                 : max_bitrate_bps;
    target_bitrate_bps = std::min(max_bitrate_bps, target_bitrate_bps);

    if (stream.min_bitrate_bps > 0) {
      RTC_DCHECK_LE(stream.min_bitrate_bps, target_bitrate_bps);
      stream_settings[i].min_bitrate_bps = stream.min_bitrate_bps;
    }
    if (stream.max_framerate > 0) {
      stream_settings[i].max_framerate = stream.max_framerate;
    }
    if (stream.num_temporal_layers) {
      RTC_DCHECK_GE(*stream.num_temporal_layers, 1);
      stream_settings[i].num_temporal_layers = stream.num_temporal_layers;
    }
    if (stream.scale_resolution_down_by >= 1.0) {
      stream_settings[i].width = width / stream.scale_resolution_down_by;
      stream_settings[i].height = height / stream.scale_resolution_down_by;
    }
    stream_settings[i].scalability_mode = stream.scalability_mode;
    stream_settings[i].encoding_complexity = stream.encoding_complexity;
    stream_settings[i].target_bitrate_bps = target_bitrate_bps;
    stream_settings[i].max_bitrate_bps = max_bitrate_bps;
    stream_settings[i].active =
        encoder_config.number_of_streams == 1 || stream.active;

    bitrate_left_bps -= stream_settings[i].target_bitrate_bps;
  }

  stream_settings[encoder_config.number_of_streams - 1].max_bitrate_bps +=
      bitrate_left_bps;
  stream_settings[0].bitrate_priority = encoder_config.bitrate_priority;

  return stream_settings;
}

DefaultVideoStreamFactory::DefaultVideoStreamFactory() {}

std::vector<VideoStream> DefaultVideoStreamFactory::CreateEncoderStreams(
    const FieldTrialsView& /*field_trials*/,
    int frame_width,
    int frame_height,
    const VideoEncoderConfig& encoder_config) {
  return CreateVideoStreams(frame_width, frame_height, encoder_config);
}

void FillEncoderConfiguration(VideoCodecType codec_type,
                              size_t num_streams,
                              VideoEncoderConfig* configuration) {
  RTC_DCHECK_LE(num_streams, DefaultVideoStreamFactory::kMaxNumberOfStreams);

  configuration->codec_type = codec_type;
  configuration->number_of_streams = num_streams;
  configuration->video_stream_factory =
      make_ref_counted<DefaultVideoStreamFactory>();
  configuration->max_bitrate_bps = 0;
  configuration->frame_drop_enabled = true;
  configuration->simulcast_layers = std::vector<VideoStream>(num_streams);
  for (size_t i = 0; i < num_streams; ++i) {
    configuration->max_bitrate_bps +=
        DefaultVideoStreamFactory::kMaxBitratePerStream[i];
  }
}

VideoReceiveStreamInterface::Decoder CreateMatchingDecoder(
    int payload_type,
    const std::string& payload_name) {
  VideoReceiveStreamInterface::Decoder decoder;
  decoder.payload_type = payload_type;
  decoder.video_format = SdpVideoFormat(payload_name);
  return decoder;
}

VideoReceiveStreamInterface::Decoder CreateMatchingDecoder(
    const VideoSendStream::Config& config) {
  return CreateMatchingDecoder(config.rtp.payload_type,
                               config.rtp.payload_name);
}

}  // namespace test
}  // namespace webrtc
