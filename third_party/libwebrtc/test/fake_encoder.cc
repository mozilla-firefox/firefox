/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/fake_encoder.h"

#include <string.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

#include "api/environment/environment.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/video/video_content_type.h"
#include "modules/video_coding/codecs/h264/include/h264_globals.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/checks.h"
#include "system_wrappers/include/sleep.h"

namespace webrtc {
namespace test {
namespace {
const int kKeyframeSizeFactor = 5;

// Inverse of proportion of frames assigned to each temporal layer for all
// possible temporal layers numbers.
const int kTemporalLayerRateFactor[4][4] = {
    {1, 0, 0, 0},  // 1/1
    {2, 2, 0, 0},  // 1/2 + 1/2
    {4, 4, 2, 0},  // 1/4 + 1/4 + 1/2
    {8, 8, 4, 2},  // 1/8 + 1/8 + 1/4 + 1/2
};

void WriteCounter(unsigned char* payload, uint32_t counter) {
  payload[0] = (counter & 0x00FF);
  payload[1] = (counter & 0xFF00) >> 8;
  payload[2] = (counter & 0xFF0000) >> 16;
  payload[3] = (counter & 0xFF000000) >> 24;
}

}  // namespace

FakeEncoder::FakeEncoder(const Environment& env)
    : FakeEncoder(env, &env.clock()) {}

FakeEncoder::FakeEncoder(Clock* clock) : FakeEncoder(absl::nullopt, clock) {}

FakeEncoder::FakeEncoder(absl::optional<Environment> env, Clock* clock)
    : env_(env),
      clock_(clock),
      num_initializations_(0),
      callback_(nullptr),
      max_target_bitrate_kbps_(-1),
      pending_keyframe_(true),
      counter_(0),
      debt_bytes_(0) {
  for (bool& used : used_layers_) {
    used = false;
  }
}

void FakeEncoder::SetFecControllerOverride(
    FecControllerOverride* fec_controller_override) {
  // Ignored.
}

void FakeEncoder::SetMaxBitrate(int max_kbps) {
  RTC_DCHECK_GE(max_kbps, -1);  // max_kbps == -1 disables it.
  MutexLock lock(&mutex_);
  max_target_bitrate_kbps_ = max_kbps;
  SetRatesLocked(current_rate_settings_);
}

void FakeEncoder::SetQp(int qp) {
  MutexLock lock(&mutex_);
  qp_ = qp;
}

void FakeEncoder::SetImplementationName(absl::string_view implementation_name) {
  MutexLock lock(&mutex_);
  implementation_name_ = std::string(implementation_name);
}

int32_t FakeEncoder::InitEncode(const VideoCodec* config,
                                const Settings& settings) {
  MutexLock lock(&mutex_);
  config_ = *config;
  ++num_initializations_;
  current_rate_settings_.bitrate.SetBitrate(0, 0, config_.startBitrate * 1000);
  current_rate_settings_.framerate_fps = config_.maxFramerate;
  pending_keyframe_ = true;
  last_frame_info_ = FrameInfo();
  return 0;
}

int32_t FakeEncoder::Encode(const VideoFrame& input_image,
                            const std::vector<VideoFrameType>* frame_types) {
  unsigned char max_framerate;
  unsigned char num_simulcast_streams;
  SimulcastStream simulcast_streams[kMaxSimulcastStreams];
  EncodedImageCallback* callback;
  RateControlParameters rates;
  bool keyframe;
  uint32_t counter;
  absl::optional<int> qp;
  {
    MutexLock lock(&mutex_);
    max_framerate = config_.maxFramerate;
    num_simulcast_streams = config_.numberOfSimulcastStreams;
    for (int i = 0; i < num_simulcast_streams; ++i) {
      simulcast_streams[i] = config_.simulcastStream[i];
    }
    callback = callback_;
    rates = current_rate_settings_;
    if (rates.framerate_fps <= 0.0) {
      rates.framerate_fps = max_framerate;
    }
    keyframe = pending_keyframe_;
    pending_keyframe_ = false;
    counter = counter_++;
    qp = qp_;
  }

  FrameInfo frame_info =
      NextFrame(frame_types, keyframe, num_simulcast_streams, rates.bitrate,
                simulcast_streams, static_cast<int>(rates.framerate_fps + 0.5));
  for (uint8_t i = 0; i < frame_info.layers.size(); ++i) {
    constexpr int kMinPayLoadLength = 14;
    if (frame_info.layers[i].size < kMinPayLoadLength) {
      // Drop this temporal layer.
      continue;
    }

    auto buffer = EncodedImageBuffer::Create(frame_info.layers[i].size);
    // Fill the buffer with arbitrary data. Write someting to make Asan happy.
    memset(buffer->data(), 9, frame_info.layers[i].size);
    // Write a counter to the image to make each frame unique.
    WriteCounter(buffer->data() + frame_info.layers[i].size - 4, counter);

    EncodedImage encoded;
    encoded.SetEncodedData(buffer);

    encoded.SetRtpTimestamp(input_image.rtp_timestamp());
    encoded._frameType = frame_info.keyframe ? VideoFrameType::kVideoFrameKey
                                             : VideoFrameType::kVideoFrameDelta;
    encoded._encodedWidth = simulcast_streams[i].width;
    encoded._encodedHeight = simulcast_streams[i].height;
    if (qp)
      encoded.qp_ = *qp;
    encoded.SetSimulcastIndex(i);
    CodecSpecificInfo codec_specific = EncodeHook(encoded, buffer);

    if (callback->OnEncodedImage(encoded, &codec_specific).error !=
        EncodedImageCallback::Result::OK) {
      return -1;
    }
  }
  return 0;
}

CodecSpecificInfo FakeEncoder::EncodeHook(
    EncodedImage& encoded_image,
    rtc::scoped_refptr<EncodedImageBuffer> buffer) {
  CodecSpecificInfo codec_specific;
  codec_specific.codecType = kVideoCodecGeneric;
  return codec_specific;
}

FakeEncoder::FrameInfo FakeEncoder::NextFrame(
    const std::vector<VideoFrameType>* frame_types,
    bool keyframe,
    uint8_t num_simulcast_streams,
    const VideoBitrateAllocation& target_bitrate,
    SimulcastStream simulcast_streams[kMaxSimulcastStreams],
    int framerate) {
  FrameInfo frame_info;
  frame_info.keyframe = keyframe;

  if (frame_types) {
    for (VideoFrameType frame_type : *frame_types) {
      if (frame_type == VideoFrameType::kVideoFrameKey) {
        frame_info.keyframe = true;
        break;
      }
    }
  }

  MutexLock lock(&mutex_);
  for (uint8_t i = 0; i < num_simulcast_streams; ++i) {
    if (target_bitrate.GetBitrate(i, 0) > 0) {
      int temporal_id = last_frame_info_.layers.size() > i
                            ? ++last_frame_info_.layers[i].temporal_id %
                                  simulcast_streams[i].numberOfTemporalLayers
                            : 0;
      frame_info.layers.emplace_back(0, temporal_id);
    }
  }

  if (last_frame_info_.layers.size() < frame_info.layers.size()) {
    // A new keyframe is needed since a new layer will be added.
    frame_info.keyframe = true;
  }

  for (uint8_t i = 0; i < frame_info.layers.size(); ++i) {
    FrameInfo::SpatialLayer& layer_info = frame_info.layers[i];
    if (frame_info.keyframe) {
      layer_info.temporal_id = 0;
      size_t avg_frame_size =
          (target_bitrate.GetBitrate(i, 0) + 7) *
          kTemporalLayerRateFactor[frame_info.layers.size() - 1][i] /
          (8 * framerate);

      // The first frame is a key frame and should be larger.
      // Store the overshoot bytes and distribute them over the coming frames,
      // so that we on average meet the bitrate target.
      debt_bytes_ += (kKeyframeSizeFactor - 1) * avg_frame_size;
      layer_info.size = kKeyframeSizeFactor * avg_frame_size;
    } else {
      size_t avg_frame_size =
          (target_bitrate.GetBitrate(i, layer_info.temporal_id) + 7) *
          kTemporalLayerRateFactor[frame_info.layers.size() - 1][i] /
          (8 * framerate);
      layer_info.size = avg_frame_size;
      if (debt_bytes_ > 0) {
        // Pay at most half of the frame size for old debts.
        size_t payment_size = std::min(avg_frame_size / 2, debt_bytes_);
        debt_bytes_ -= payment_size;
        layer_info.size -= payment_size;
      }
    }
  }
  last_frame_info_ = frame_info;
  return frame_info;
}

int32_t FakeEncoder::RegisterEncodeCompleteCallback(
    EncodedImageCallback* callback) {
  MutexLock lock(&mutex_);
  callback_ = callback;
  return 0;
}

int32_t FakeEncoder::Release() {
  return 0;
}

void FakeEncoder::SetRates(const RateControlParameters& parameters) {
  MutexLock lock(&mutex_);
  SetRatesLocked(parameters);
}

void FakeEncoder::SetRatesLocked(const RateControlParameters& parameters) {
  current_rate_settings_ = parameters;
  int allocated_bitrate_kbps = parameters.bitrate.get_sum_kbps();

  // Scale bitrate allocation to not exceed the given max target bitrate.
  if (max_target_bitrate_kbps_ > 0 &&
      allocated_bitrate_kbps > max_target_bitrate_kbps_) {
    for (uint8_t spatial_idx = 0; spatial_idx < kMaxSpatialLayers;
         ++spatial_idx) {
      for (uint8_t temporal_idx = 0; temporal_idx < kMaxTemporalStreams;
           ++temporal_idx) {
        if (current_rate_settings_.bitrate.HasBitrate(spatial_idx,
                                                      temporal_idx)) {
          uint32_t bitrate = current_rate_settings_.bitrate.GetBitrate(
              spatial_idx, temporal_idx);
          bitrate = static_cast<uint32_t>(
              (bitrate* int64_t{max_target_bitrate_kbps_}) /
              allocated_bitrate_kbps);
          current_rate_settings_.bitrate.SetBitrate(spatial_idx, temporal_idx,
                                                    bitrate);
        }
      }
    }
  }
}

VideoEncoder::EncoderInfo FakeEncoder::GetEncoderInfo() const {
  EncoderInfo info;
  MutexLock lock(&mutex_);
  info.implementation_name = implementation_name_.value_or(kImplementationName);
  info.is_hardware_accelerated = true;
  for (int sid = 0; sid < config_.numberOfSimulcastStreams; ++sid) {
    int number_of_temporal_layers =
        config_.simulcastStream[sid].numberOfTemporalLayers;
    info.fps_allocation[sid].clear();
    for (int tid = 0; tid < number_of_temporal_layers; ++tid) {
      // {1/4, 1/2, 1} allocation for num layers = 3.
      info.fps_allocation[sid].push_back(255 /
                                         (number_of_temporal_layers - tid));
    }
  }
  return info;
}

int FakeEncoder::GetConfiguredInputFramerate() const {
  MutexLock lock(&mutex_);
  return static_cast<int>(current_rate_settings_.framerate_fps + 0.5);
}

int FakeEncoder::GetNumInitializations() const {
  MutexLock lock(&mutex_);
  return num_initializations_;
}

const VideoCodec& FakeEncoder::config() const {
  MutexLock lock(&mutex_);
  return config_;
}

FakeH264Encoder::FakeH264Encoder(const Environment& env)
    : FakeEncoder(env), idr_counter_(0) {}

FakeH264Encoder::FakeH264Encoder(Clock* clock)
    : FakeEncoder(clock), idr_counter_(0) {}

CodecSpecificInfo FakeH264Encoder::EncodeHook(
    EncodedImage& encoded_image,
    rtc::scoped_refptr<EncodedImageBuffer> buffer) {
  static constexpr std::array<uint8_t, 3> kStartCode = {0, 0, 1};
  const size_t kSpsSize = 8;
  const size_t kPpsSize = 11;
  const int kIdrFrequency = 10;
  int current_idr_counter;
  {
    MutexLock lock(&local_mutex_);
    current_idr_counter = idr_counter_;
    ++idr_counter_;
  }
  for (size_t i = 0; i < encoded_image.size(); ++i) {
    buffer->data()[i] = static_cast<uint8_t>(i);
  }

  if (current_idr_counter % kIdrFrequency == 0 &&
      encoded_image.size() > kSpsSize + kPpsSize + 1 + 3 * kStartCode.size()) {
    const size_t kSpsNalHeader = 0x67;
    const size_t kPpsNalHeader = 0x68;
    const size_t kIdrNalHeader = 0x65;
    uint8_t* data = buffer->data();
    memcpy(data, kStartCode.data(), kStartCode.size());
    data += kStartCode.size();
    data[0] = kSpsNalHeader;
    data += kSpsSize;

    memcpy(data, kStartCode.data(), kStartCode.size());
    data += kStartCode.size();
    data[0] = kPpsNalHeader;
    data += kPpsSize;

    memcpy(data, kStartCode.data(), kStartCode.size());
    data += kStartCode.size();
    data[0] = kIdrNalHeader;
  } else {
    memcpy(buffer->data(), kStartCode.data(), kStartCode.size());
    const size_t kNalHeader = 0x41;
    buffer->data()[kStartCode.size()] = kNalHeader;
  }

  CodecSpecificInfo codec_specific;
  codec_specific.codecType = kVideoCodecH264;
  codec_specific.codecSpecific.H264.packetization_mode =
      H264PacketizationMode::NonInterleaved;
  return codec_specific;
}

DelayedEncoder::DelayedEncoder(const Environment& env, int delay_ms)
    : test::FakeEncoder(env), delay_ms_(delay_ms) {
  // The encoder could be created on a different thread than
  // it is being used on.
  sequence_checker_.Detach();
}

void DelayedEncoder::SetDelay(int delay_ms) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  delay_ms_ = delay_ms;
}

int32_t DelayedEncoder::Encode(const VideoFrame& input_image,
                               const std::vector<VideoFrameType>* frame_types) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);

  SleepMs(delay_ms_);

  return FakeEncoder::Encode(input_image, frame_types);
}

MultithreadedFakeH264Encoder::MultithreadedFakeH264Encoder(
    const Environment& env)
    : test::FakeH264Encoder(env),
      current_queue_(0),
      queue1_(nullptr),
      queue2_(nullptr) {
  // The encoder could be created on a different thread than
  // it is being used on.
  sequence_checker_.Detach();
}

int32_t MultithreadedFakeH264Encoder::InitEncode(const VideoCodec* config,
                                                 const Settings& settings) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);

  queue1_ = env_->task_queue_factory().CreateTaskQueue(
      "Queue 1", TaskQueueFactory::Priority::NORMAL);
  queue2_ = env_->task_queue_factory().CreateTaskQueue(
      "Queue 2", TaskQueueFactory::Priority::NORMAL);

  return FakeH264Encoder::InitEncode(config, settings);
}

int32_t MultithreadedFakeH264Encoder::Encode(
    const VideoFrame& input_image,
    const std::vector<VideoFrameType>* frame_types) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);

  TaskQueueBase* queue =
      (current_queue_++ % 2 == 0) ? queue1_.get() : queue2_.get();

  if (!queue) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }

  queue->PostTask([this, input_image, frame_types = *frame_types] {
    EncodeCallback(input_image, &frame_types);
  });

  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t MultithreadedFakeH264Encoder::EncodeCallback(
    const VideoFrame& input_image,
    const std::vector<VideoFrameType>* frame_types) {
  return FakeH264Encoder::Encode(input_image, frame_types);
}

int32_t MultithreadedFakeH264Encoder::Release() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);

  queue1_.reset();
  queue2_.reset();

  return FakeH264Encoder::Release();
}

}  // namespace test
}  // namespace webrtc
