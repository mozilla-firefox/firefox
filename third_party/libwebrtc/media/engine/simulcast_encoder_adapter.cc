/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/engine/simulcast_encoder_adapter.h"

#include <algorithm>
#include <bitset>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "api/environment/environment.h"
#include "api/fec_controller_override.h"
#include "api/field_trials_view.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/units/data_rate.h"
#include "api/units/timestamp.h"
#include "api/video/encoded_image.h"
#include "api/video/video_bitrate_allocation.h"
#include "api/video/video_bitrate_allocator.h"
#include "api/video/video_codec_constants.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_rotation.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/simulcast_stream.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "api/video_codecs/video_encoder_software_fallback_wrapper.h"
#include "common_video/framerate_controller.h"
#include "media/base/sdp_video_format_utils.h"
#include "modules/include/module_common_types_public.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "modules/video_coding/include/video_error_codes_utils.h"
#include "modules/video_coding/utility/simulcast_rate_allocator.h"
#include "rtc_base/checks.h"
#include "rtc_base/experiments/rate_control_settings.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "rtc_base/strings/str_join.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/synchronization/mutex.h"

namespace webrtc {
namespace {

// Max qp for lowest spatial resolution when doing simulcast.
const unsigned int kLowestResMaxQp = 45;

uint32_t SumStreamMaxBitrate(int streams, const VideoCodec& codec) {
  uint32_t bitrate_sum = 0;
  for (int i = 0; i < streams; ++i) {
    bitrate_sum += codec.simulcastStream[i].maxBitrate;
  }
  return bitrate_sum;
}

int CountAllStreams(const VideoCodec& codec) {
  int total_streams_count =
      codec.numberOfSimulcastStreams < 1 ? 1 : codec.numberOfSimulcastStreams;
  uint32_t simulcast_max_bitrate =
      SumStreamMaxBitrate(total_streams_count, codec);
  if (simulcast_max_bitrate == 0) {
    total_streams_count = 1;
  }
  return total_streams_count;
}

int CountActiveStreams(const VideoCodec& codec) {
  if (codec.numberOfSimulcastStreams < 1) {
    return 1;
  }
  int total_streams_count = CountAllStreams(codec);
  int active_streams_count = 0;
  for (int i = 0; i < total_streams_count; ++i) {
    if (codec.simulcastStream[i].active) {
      ++active_streams_count;
    }
  }
  return active_streams_count;
}

int VerifyCodec(const VideoCodec* codec_settings) {
  if (codec_settings == nullptr) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  if (codec_settings->maxFramerate < 1) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  // allow zero to represent an unspecified maxBitRate
  if (codec_settings->maxBitrate > 0 &&
      codec_settings->startBitrate > codec_settings->maxBitrate) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  if (codec_settings->width <= 1 || codec_settings->height <= 1) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  if (codec_settings->codecType == kVideoCodecVP8 &&
      codec_settings->VP8().automaticResizeOn &&
      CountActiveStreams(*codec_settings) > 1) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

bool StreamQualityCompare(const SimulcastStream& a, const SimulcastStream& b) {
  return std::tie(a.height, a.width, a.maxBitrate, a.maxFramerate) <
         std::tie(b.height, b.width, b.maxBitrate, b.maxFramerate);
}

void GetLowestAndHighestQualityStreamIndixes(
    std::span<const SimulcastStream> streams,
    int* lowest_quality_stream_idx,
    int* highest_quality_stream_idx) {
  const auto lowest_highest_quality_streams =
      absl::c_minmax_element(streams, StreamQualityCompare);
  *lowest_quality_stream_idx =
      std::distance(streams.begin(), lowest_highest_quality_streams.first);
  *highest_quality_stream_idx =
      std::distance(streams.begin(), lowest_highest_quality_streams.second);
}

std::vector<uint32_t> GetStreamStartBitratesKbps(const Environment& env,
                                                 const VideoCodec& codec) {
  VideoBitrateAllocation allocation =
      SimulcastRateAllocator(env, codec)
          .Allocate(VideoBitrateAllocationParameters(codec.startBitrate * 1000,
                                                     codec.maxFramerate));

  int total_streams_count = CountAllStreams(codec);
  std::vector<uint32_t> start_bitrates;
  for (int i = 0; i < total_streams_count; ++i) {
    uint32_t stream_bitrate = allocation.GetSpatialLayerSum(i) / 1000;
    start_bitrates.push_back(stream_bitrate);
  }
  return start_bitrates;
}

}  // namespace

SimulcastEncoderAdapter::EncoderContext::EncoderContext(
    std::unique_ptr<VideoEncoder> encoder,
    bool prefer_temporal_support,
    VideoEncoder::EncoderInfo primary_info,
    VideoEncoder::EncoderInfo fallback_info,
    SdpVideoFormat video_format)
    : encoder_(std::move(encoder)),
      prefer_temporal_support_(prefer_temporal_support),
      primary_info_(std::move(primary_info)),
      fallback_info_(std::move(fallback_info)),
      video_format_(std::move(video_format)) {}

void SimulcastEncoderAdapter::EncoderContext::Release() {
  if (encoder_) {
    encoder_->Release();
    encoder_->RegisterEncodeCompleteCallback(nullptr);
  }
}

SimulcastEncoderAdapter::StreamContext::StreamContext(
    SimulcastEncoderAdapter* parent,
    std::unique_ptr<EncoderContext> encoder_context,
    std::unique_ptr<FramerateController> framerate_controller,
    int stream_idx,
    uint16_t width,
    uint16_t height,
    bool is_paused)
    : parent_(parent),
      encoder_context_(std::move(encoder_context)),
      framerate_controller_(std::move(framerate_controller)),
      stream_idx_(stream_idx),
      width_(width),
      height_(height),
      is_keyframe_needed_(false),
      is_paused_(is_paused) {
  if (parent_) {
    // If we have a parent, this encoder is not running in bypass mode and we
    // should funnnel the callbacks back via this stream context.
    encoder_context_->encoder().RegisterEncodeCompleteCallback(this);
  }
}

SimulcastEncoderAdapter::StreamContext::StreamContext(StreamContext&& rhs)
    : parent_(rhs.parent_),
      encoder_context_(std::move(rhs.encoder_context_)),
      framerate_controller_(std::move(rhs.framerate_controller_)),
      stream_idx_(rhs.stream_idx_),
      width_(rhs.width_),
      height_(rhs.height_),
      is_keyframe_needed_(rhs.is_keyframe_needed_),
      is_paused_(rhs.is_paused_) {
  if (parent_) {
    // If we have a parent, this encoder is not running in bypass mode and we
    // should funnel the callbacks back via this stream context.
    encoder_context_->encoder().RegisterEncodeCompleteCallback(this);
  }
}

SimulcastEncoderAdapter::StreamContext::~StreamContext() {
  if (encoder_context_) {
    encoder_context_->Release();
  }
}

std::unique_ptr<SimulcastEncoderAdapter::EncoderContext>
SimulcastEncoderAdapter::StreamContext::ReleaseEncoderContext() && {
  encoder_context_->Release();
  return std::move(encoder_context_);
}

void SimulcastEncoderAdapter::StreamContext::OnKeyframe(Timestamp timestamp) {
  is_keyframe_needed_ = false;
  if (framerate_controller_) {
    framerate_controller_->KeepFrame(timestamp.us() * 1000);
  }
}

bool SimulcastEncoderAdapter::StreamContext::ShouldDropFrame(
    Timestamp timestamp) {
  if (!framerate_controller_) {
    return false;
  }
  return framerate_controller_->ShouldDropFrame(timestamp.us() * 1000);
}

EncodedImageCallback::Result
SimulcastEncoderAdapter::StreamContext::OnEncodedImage(
    const EncodedImage& encoded_image,
    const CodecSpecificInfo* codec_specific_info) {
  RTC_CHECK(parent_);  // If null, this method should never be called.
  // If encoder is simulcast capable, it should be used in bypass mode and never
  // end up here - so we expect all frame to always be end of temporal unit in
  // this context.
  RTC_DCHECK(encoded_image.is_end_of_temporal_unit().value_or(true));
  MaybeCullOldTimestamps(encoded_image.RtpTimestamp());
  return parent_->OnEncodedImage(stream_idx_, encoded_image,
                                 codec_specific_info);
}

void SimulcastEncoderAdapter::StreamContext::OnFrameDropped(
    uint32_t rtp_timestamp,
    int spatial_id,
    bool is_end_of_temporal_unit) {
  RTC_CHECK(parent_);  // If null, this method should never be called.
  // If encoder is simulcast capable, it should be used in bypass mode and never
  // end up here - so we expect all frame to always be end of temporal unit in
  // this context.
  RTC_DCHECK(is_end_of_temporal_unit);
  MaybeCullOldTimestamps(rtp_timestamp);
  parent_->OnFrameDropped(rtp_timestamp, stream_idx_ + spatial_id);
}

int SimulcastEncoderAdapter::StreamContext::Encode(
    const VideoFrame& frame,
    const std::vector<VideoFrameType>* frame_types) {
  {
    MutexLock lock(&queue_mutex_);
    pending_rtp_timestamps_.push_back(frame.rtp_timestamp());
  }
  int ret = encoder().Encode(frame, frame_types);
  if (ret != WEBRTC_VIDEO_CODEC_OK) {
    MutexLock lock(&queue_mutex_);
    RTC_DCHECK_EQ(pending_rtp_timestamps_.back(), frame.rtp_timestamp());
    pending_rtp_timestamps_.pop_back();
  }
  return ret;
}

void SimulcastEncoderAdapter::StreamContext::MaybeCullOldTimestamps(
    uint32_t new_rtp_timestamp) {
  std::vector<uint32_t> dropped_timestamps;
  {
    MutexLock lock(&queue_mutex_);
    while (
        !pending_rtp_timestamps_.empty() &&
        pending_rtp_timestamps_.front() != new_rtp_timestamp &&
        IsNewerTimestamp(new_rtp_timestamp, pending_rtp_timestamps_.front())) {
      dropped_timestamps.push_back(pending_rtp_timestamps_.front());
      pending_rtp_timestamps_.pop_front();
    }
    if (!pending_rtp_timestamps_.empty() &&
        pending_rtp_timestamps_.front() == new_rtp_timestamp) {
      pending_rtp_timestamps_.pop_front();
    }
  }
  // Make callback outside lock to avoid potential deadlocks.
  for (uint32_t timestamp : dropped_timestamps) {
    parent_->OnFrameDropped(timestamp, stream_idx_);
  }
}

SimulcastEncoderAdapter::SimulcastEncoderAdapter(
    const Environment& env,
    VideoEncoderFactory* absl_nonnull primary_factory,
    VideoEncoderFactory* absl_nullable fallback_factory,
    const SdpVideoFormat& format)
    : env_(env),
      inited_(0),
      primary_encoder_factory_(primary_factory),
      fallback_encoder_factory_(fallback_factory),
      video_format_(format),
      total_streams_count_(0),
      bypass_mode_(false),
      encoded_complete_callback_(nullptr),
      boost_base_layer_quality_(
          RateControlSettings(env_.field_trials()).Vp8BoostBaseLayerQuality()),
      prefer_temporal_support_on_base_layer_(env_.field_trials().IsEnabled(
          "WebRTC-Video-PreferTemporalSupportOnBaseLayer")),
      per_layer_pli_(SupportsPerLayerPictureLossIndication(format.parameters)),
      drop_unaligned_resolution_(!env_.field_trials().IsDisabled(
          "WebRTC-SimulcastEncoderAdapter-DropUnalignedResolution")),
      encoder_info_override_(env.field_trials()) {
  RTC_DCHECK(primary_factory);

  // The adapter is typically created on the worker thread, but operated on
  // the encoder task queue.
  encoder_queue_checker_.Detach();
}

SimulcastEncoderAdapter::~SimulcastEncoderAdapter() {
  RTC_DCHECK_RUN_ON(&encoder_queue_checker_);
  if (Initialized()) {
    Release();
  }
  DestroyStoredEncoders();
}

void SimulcastEncoderAdapter::SetFecControllerOverride(
    FecControllerOverride* /*fec_controller_override*/) {
  // Ignored.
}

int SimulcastEncoderAdapter::Release() {
  RTC_DCHECK_RUN_ON(&encoder_queue_checker_);

  while (!stream_contexts_.empty()) {
    // Move the encoder instances and put it on the `cached_encoder_contexts_`
    // where it may possibly be reused from (ordering does not matter).
    cached_encoder_contexts_.push_front(
        std::move(stream_contexts_.back()).ReleaseEncoderContext());
    stream_contexts_.pop_back();
  }

  bypass_mode_ = false;

  {
    MutexLock lock(&pending_frames_mutex_);
    pending_frames_.clear();
  }

  // It's legal to move the encoder to another queue now.
  encoder_queue_checker_.Detach();

  inited_.store(0);

  return WEBRTC_VIDEO_CODEC_OK;
}

int SimulcastEncoderAdapter::InitEncode(
    const VideoCodec* codec_settings,
    const VideoEncoder::Settings& settings) {
  RTC_DCHECK_RUN_ON(&encoder_queue_checker_);

  if (settings.number_of_cores < 1) {
    return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
  }

  int ret = VerifyCodec(codec_settings);
  if (ret < 0) {
    return ret;
  }

  Release();

  codec_ = *codec_settings;
  total_streams_count_ = CountAllStreams(*codec_settings);

  bool is_legacy_singlecast = codec_.numberOfSimulcastStreams == 0;
  int lowest_quality_stream_idx = 0;
  int highest_quality_stream_idx = 0;
  if (!is_legacy_singlecast) {
    GetLowestAndHighestQualityStreamIndixes(
        std::span<SimulcastStream>(codec_.simulcastStream,
                                   total_streams_count_),
        &lowest_quality_stream_idx, &highest_quality_stream_idx);
  }

  std::unique_ptr<EncoderContext> encoder_context = FetchOrCreateEncoderContext(
      /*is_lowest_quality_stream=*/(
          is_legacy_singlecast ||
          codec_.simulcastStream[lowest_quality_stream_idx].active),
      /*stream_idx=*/is_legacy_singlecast
          ? std::nullopt
          : std::make_optional(lowest_quality_stream_idx));
  if (encoder_context == nullptr) {
    return WEBRTC_VIDEO_CODEC_MEMORY;
  }

  bool is_mixed_codec = codec_.IsMixedCodec();

  // Two distinct scenarios:
  // * Singlecast (total_streams_count == 1) or simulcast with simulcast-capable
  //   underlaying encoder implementation if active_streams_count > 1. SEA
  //   operates in bypass mode: original settings are passed to the underlaying
  //   encoder, frame encode complete callback is not intercepted.
  // * Multi-encoder simulcast or singlecast if layers are deactivated
  //   (active_streams_count >= 1). SEA creates N=active_streams_count encoders
  //   and configures each to produce a single stream.

  int active_streams_count = CountActiveStreams(*codec_settings);
  // If we only have a single active layer it is better to create an encoder
  // with only one configured layer than creating it with all-but-one disabled
  // layers because that way we control scaling.
  // The use of the nonstandard x-google-per-layer-pli fmtp parameter also
  // forces the use of SEA with separate encoders to support per-layer
  // handling of PLIs.
  // We do not force separate encoders when WebRTC-Video-PerSsrcKeyframes is
  // enabled, because using a single encoder that supports simulcast is more
  // efficient (less CPU overhead) than running multiple encoders, and the
  // single encoder might natively support per-layer keyframe generation.
  bool separate_encoders_needed =
      is_mixed_codec ||
      !encoder_context->encoder().GetEncoderInfo().supports_simulcast ||
      active_streams_count == 1 || per_layer_pli_;
  RTC_LOG(LS_INFO) << "[SEA] InitEncode: total_streams_count: "
                   << total_streams_count_
                   << ", active_streams_count: " << active_streams_count
                   << ", separate_encoders_needed: "
                   << (separate_encoders_needed ? "true" : "false");
  // Singlecast or simulcast with simulcast-capable underlaying encoder.
  if (total_streams_count_ == 1 || !separate_encoders_needed) {
    RTC_LOG(LS_INFO) << "[SEA] InitEncode: Single-encoder mode";
    int result = encoder_context->encoder().InitEncode(&codec_, settings);
    if (result >= 0) {
      stream_contexts_.emplace_back(
          /*parent=*/nullptr, std::move(encoder_context),
          /*framerate_controller=*/nullptr, /*stream_idx=*/0, codec_.width,
          codec_.height, /*is_paused=*/active_streams_count == 0);
      bypass_mode_ = true;

      DestroyStoredEncoders();
      inited_.store(1);
      return WEBRTC_VIDEO_CODEC_OK;
    }

    encoder_context->Release();
    encoder_context->encoder().RegisterEncodeCompleteCallback(
        encoded_complete_callback_);
    if (total_streams_count_ == 1) {
      RTC_LOG(LS_ERROR) << "[SEA] InitEncode: failed with error code: "
                        << WebRtcVideoCodecErrorToString(result);
      return result;
    }
    RTC_LOG(LS_WARNING) << "[SEA] InitEncode: failed with error code: "
                        << WebRtcVideoCodecErrorToString(result)
                        << ". Falling back to multi-encoder mode.";
  }

  // Multi-encoder simulcast or singlecast (deactivated layers).
  std::vector<uint32_t> stream_start_bitrate_kbps =
      GetStreamStartBitratesKbps(env_, codec_);

  for (int stream_idx = 0; stream_idx < total_streams_count_; ++stream_idx) {
    if (!is_legacy_singlecast && !codec_.simulcastStream[stream_idx].active) {
      continue;
    }

    if (encoder_context == nullptr || is_mixed_codec) {
      encoder_context = FetchOrCreateEncoderContext(
          /*is_lowest_quality_stream=*/stream_idx == lowest_quality_stream_idx,
          stream_idx);
    }
    if (encoder_context == nullptr) {
      Release();
      return WEBRTC_VIDEO_CODEC_MEMORY;
    }

    VideoCodec stream_codec = MakeStreamCodec(
        codec_, stream_idx, stream_start_bitrate_kbps[stream_idx],
        /*is_lowest_quality_stream=*/stream_idx == lowest_quality_stream_idx,
        /*is_highest_quality_stream=*/stream_idx == highest_quality_stream_idx);

    RTC_LOG(LS_INFO) << "[SEA] Multi-encoder mode: initializing stream: "
                     << stream_idx << ", active: "
                     << (codec_.simulcastStream[stream_idx].active ? "true"
                                                                   : "false");
    int result = encoder_context->encoder().InitEncode(&stream_codec, settings);
    if (result < 0) {
      encoder_context.reset();
      Release();
      RTC_LOG(LS_ERROR) << "[SEA] InitEncode: failed with error code: "
                        << WebRtcVideoCodecErrorToString(result);
      return result;
    }

    SimulcastEncoderAdapter* parent = this;
    bool is_paused = stream_start_bitrate_kbps[stream_idx] == 0;
    stream_contexts_.emplace_back(
        parent, std::move(encoder_context),
        std::make_unique<FramerateController>(stream_codec.maxFramerate),
        stream_idx, stream_codec.width, stream_codec.height, is_paused);
    encoder_context = nullptr;
  }
  // To save memory, don't store encoders that we don't use.
  DestroyStoredEncoders();

  inited_.store(1);
  return WEBRTC_VIDEO_CODEC_OK;
}

int SimulcastEncoderAdapter::Encode(
    const VideoFrame& input_image,
    const std::vector<VideoFrameType>* frame_types) {
  RTC_DCHECK_RUN_ON(&encoder_queue_checker_);

  if (!Initialized()) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }
  if (encoded_complete_callback_ == nullptr) {
    return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
  }

  if (encoder_info_override_.requested_resolution_alignment()) {
    const int alignment =
        *encoder_info_override_.requested_resolution_alignment();
    if (input_image.width() % alignment != 0 ||
        input_image.height() % alignment != 0) {
      RTC_LOG(LS_WARNING) << "Frame " << input_image.width() << "x"
                          << input_image.height() << " not divisible by "
                          << alignment;
      return drop_unaligned_resolution_ ? WEBRTC_VIDEO_CODEC_NO_OUTPUT
                                        : WEBRTC_VIDEO_CODEC_ERROR;
    }
    if (encoder_info_override_.apply_alignment_to_all_simulcast_layers()) {
      for (const auto& layer : stream_contexts_) {
        if (layer.width() % alignment != 0 || layer.height() % alignment != 0) {
          RTC_LOG(LS_WARNING)
              << "Codec " << layer.width() << "x" << layer.height()
              << " not divisible by " << alignment;
          return drop_unaligned_resolution_ ? WEBRTC_VIDEO_CODEC_NO_OUTPUT
                                            : WEBRTC_VIDEO_CODEC_ERROR;
        }
      }
    }
  }

  bool is_keyframe_needed = false;
  for (const auto& layer : stream_contexts_) {
    if (layer.is_keyframe_needed()) {
      // This is legacy behavior, generating a keyframe on all layers
      // when generating one for a layer that became active for the first time
      // or after being disabled.
      is_keyframe_needed = true;
      break;
    }
  }

  // Temporary thay may hold the result of texture to i420 buffer conversion.
  scoped_refptr<VideoFrameBuffer> src_buffer;
  int src_width = input_image.width();
  int src_height = input_image.height();

  if (!bypass_mode_) {
    // Limit to prevent this queue from growing if the underlying encoder is
    // not reporting the completion of frames via either OnEncodedFrame or
    // OnFrameDropped.
    constexpr size_t kMaxPendingFrames = 15;
    std::vector<std::tuple<uint32_t, int, bool>> frame_drops;
    {
      MutexLock lock(&pending_frames_mutex_);
      if (pending_frames_.size() >= kMaxPendingFrames) {
        // Drop the oldest frame.
        PendingFrame& dropped_frame = pending_frames_.front();
        RTC_LOG(LS_WARNING) << "Pending frames queue full. Dropping frame with "
                               "rtp_timestamp="
                            << dropped_frame.rtp_timestamp;
        for (size_t stream_idx = 0; stream_idx < kMaxSimulcastStreams;
             ++stream_idx) {
          if (dropped_frame.expected_layer_index.test(stream_idx)) {
            dropped_frame.expected_layer_index.reset(stream_idx);
            bool is_last = dropped_frame.expected_layer_index.none();
            frame_drops.emplace_back(dropped_frame.rtp_timestamp, stream_idx,
                                     is_last);
          }
        }
        pending_frames_.pop_front();
      }

      std::bitset<kMaxSimulcastStreams> expected_layer_index;
      for (const auto& layer : stream_contexts_) {
        if (!layer.is_paused()) {
          expected_layer_index.set(layer.stream_idx());
        }
      }
      pending_frames_.push_back({
          .rtp_timestamp = input_image.rtp_timestamp(),
          .expected_layer_index = expected_layer_index,
      });
    }

    // Make frame drop callbacks outside of the lock.
    for (const auto& [rtp_timestamp, stream_idx, is_last] : frame_drops) {
      encoded_complete_callback_->OnFrameDropped(rtp_timestamp, stream_idx,
                                                 is_last);
    }
  }

  for (auto& layer : stream_contexts_) {
    // Don't encode frames in resolutions that we don't intend to send.
    if (layer.is_paused()) {
      continue;
    }

    // Convert timestamp from RTP 90kHz clock.
    const Timestamp frame_timestamp =
        Timestamp::Micros((1000 * input_image.rtp_timestamp()) / 90);

    // If adapter is passed through and only one sw encoder does simulcast,
    // frame types for all streams should be passed to the encoder unchanged.
    // Otherwise a single per-encoder frame type is passed.
    std::vector<VideoFrameType> stream_frame_types(
        bypass_mode_
            ? std::max<unsigned char>(codec_.numberOfSimulcastStreams, 1)
            : 1,
        VideoFrameType::kVideoFrameDelta);

    bool keyframe_requested = false;
    if (is_keyframe_needed) {
      std::fill(stream_frame_types.begin(), stream_frame_types.end(),
                VideoFrameType::kVideoFrameKey);
      keyframe_requested = true;
    } else if (frame_types) {
      if (bypass_mode_) {
        // In bypass mode, we effectively pass on frame_types.
        RTC_DCHECK_EQ(frame_types->size(), stream_frame_types.size());
        stream_frame_types = *frame_types;
        keyframe_requested =
            absl::c_any_of(*frame_types, [](const VideoFrameType frame_type) {
              return frame_type == VideoFrameType::kVideoFrameKey;
            });
      } else {
        size_t stream_idx = static_cast<size_t>(layer.stream_idx());
        if (frame_types->size() > stream_idx &&
            (*frame_types)[stream_idx] == VideoFrameType::kVideoFrameKey) {
          stream_frame_types[0] = VideoFrameType::kVideoFrameKey;
          keyframe_requested = true;
        }
      }
    }
    if (keyframe_requested) {
      layer.OnKeyframe(frame_timestamp);
    } else if (layer.ShouldDropFrame(frame_timestamp)) {
      OnFrameDropped(input_image.rtp_timestamp(), layer.stream_idx());
      continue;
    }

    // If scaling isn't required, because the input resolution
    // matches the destination or the input image is empty (e.g.
    // a keyframe request for encoders with internal camera
    // sources) or the source image has a native handle, pass the image on
    // directly. Otherwise, we'll scale it to match what the encoder expects
    // (below).
    // For texture frames, the underlying encoder is expected to be able to
    // correctly sample/scale the source texture.
    // TODO(perkj): ensure that works going forward, and figure out how this
    // affects webrtc:5683.
    if ((layer.width() == src_width && layer.height() == src_height) ||
        (input_image.video_frame_buffer()->type() ==
             VideoFrameBuffer::Type::kNative &&
         layer.encoder().GetEncoderInfo().supports_native_handle)) {
      int ret = layer.Encode(input_image, &stream_frame_types);
      if (ret != WEBRTC_VIDEO_CODEC_OK) {
        if (!bypass_mode_) {
          MutexLock lock(&pending_frames_mutex_);
          pending_frames_.pop_back();
        }
        return ret;
      }
    } else {
      if (src_buffer == nullptr) {
        src_buffer = input_image.video_frame_buffer();
      }
      scoped_refptr<VideoFrameBuffer> dst_buffer =
          src_buffer->Scale(layer.width(), layer.height());
      if (!dst_buffer) {
        RTC_LOG(LS_ERROR) << "Failed to scale video frame";
        return WEBRTC_VIDEO_CODEC_ENCODER_FAILURE;
      }

      // UpdateRect is not propagated to lower simulcast layers currently.
      // TODO(ilnik): Consider scaling UpdateRect together with the buffer.
      VideoFrame frame(input_image);
      frame.set_video_frame_buffer(dst_buffer);
      frame.set_rotation(kVideoRotation_0);
      frame.set_update_rect(VideoFrame::UpdateRect{.offset_x = 0,
                                                   .offset_y = 0,
                                                   .width = frame.width(),
                                                   .height = frame.height()});

      int ret = layer.Encode(frame, &stream_frame_types);
      if (ret != WEBRTC_VIDEO_CODEC_OK) {
        return ret;
      }
    }
  }

  return WEBRTC_VIDEO_CODEC_OK;
}

int SimulcastEncoderAdapter::RegisterEncodeCompleteCallback(
    EncodedImageCallback* callback) {
  RTC_DCHECK_RUN_ON(&encoder_queue_checker_);
  encoded_complete_callback_ = callback;
  if (bypass_mode_ && !stream_contexts_.empty()) {
    stream_contexts_.front().encoder().RegisterEncodeCompleteCallback(callback);
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

void SimulcastEncoderAdapter::SetRates(
    const RateControlParameters& parameters) {
  RTC_DCHECK_RUN_ON(&encoder_queue_checker_);

  if (!Initialized()) {
    RTC_LOG(LS_WARNING) << "SetRates while not initialized";
    return;
  }

  if (parameters.framerate_fps < 1.0) {
    RTC_LOG(LS_WARNING) << "Invalid framerate: " << parameters.framerate_fps;
    return;
  }

  codec_.maxFramerate = static_cast<uint32_t>(parameters.framerate_fps + 0.5);

  if (bypass_mode_) {
    stream_contexts_.front().encoder().SetRates(parameters);
    return;
  }

  for (StreamContext& layer_context : stream_contexts_) {
    int stream_idx = layer_context.stream_idx();
    uint32_t stream_bitrate_kbps =
        parameters.bitrate.GetSpatialLayerSum(stream_idx) / 1000;

    // Need a key frame if we have not sent this stream before.
    if (stream_bitrate_kbps > 0 && layer_context.is_paused()) {
      layer_context.set_is_keyframe_needed();
    }
    layer_context.set_is_paused(stream_bitrate_kbps == 0);

    // Slice the temporal layers out of the full allocation and pass it on to
    // the encoder handling the current simulcast stream.
    RateControlParameters stream_parameters = parameters;
    stream_parameters.bitrate = VideoBitrateAllocation();
    for (int i = 0; checked_cast<size_t>(i) < kMaxTemporalStreams; ++i) {
      if (parameters.bitrate.HasBitrate(stream_idx, i)) {
        stream_parameters.bitrate.SetBitrate(
            0, i, parameters.bitrate.GetBitrate(stream_idx, i));
      }
    }

    // Assign link allocation proportionally to spatial layer allocation.
    if (!parameters.bandwidth_allocation.IsZero() &&
        parameters.bitrate.get_sum_bps() > 0) {
      stream_parameters.bandwidth_allocation =
          DataRate::BitsPerSec((parameters.bandwidth_allocation.bps() *
                                stream_parameters.bitrate.get_sum_bps()) /
                               parameters.bitrate.get_sum_bps());
      // Make sure we don't allocate bandwidth lower than target bitrate.
      if (stream_parameters.bandwidth_allocation.bps() <
          stream_parameters.bitrate.get_sum_bps()) {
        stream_parameters.bandwidth_allocation =
            DataRate::BitsPerSec(stream_parameters.bitrate.get_sum_bps());
      }
    }

    stream_parameters.framerate_fps = std::min<double>(
        parameters.framerate_fps,
        layer_context.target_fps().value_or(parameters.framerate_fps));

    layer_context.encoder().SetRates(stream_parameters);
  }
}

void SimulcastEncoderAdapter::OnPacketLossRateUpdate(float packet_loss_rate) {
  for (auto& c : stream_contexts_) {
    c.encoder().OnPacketLossRateUpdate(packet_loss_rate);
  }
}

void SimulcastEncoderAdapter::OnRttUpdate(int64_t rtt_ms) {
  for (auto& c : stream_contexts_) {
    c.encoder().OnRttUpdate(rtt_ms);
  }
}

void SimulcastEncoderAdapter::OnLossNotification(
    const LossNotification& loss_notification) {
  for (auto& c : stream_contexts_) {
    c.encoder().OnLossNotification(loss_notification);
  }
}

EncodedImageCallback::Result SimulcastEncoderAdapter::OnEncodedImage(
    size_t stream_idx,
    const EncodedImage& encodedImage,
    const CodecSpecificInfo* codecSpecificInfo) {
  EncodedImage stream_image(encodedImage);

  if (!bypass_mode_) {
    // In non-bypass mode, the adapter manages stream indices.
    // Overwrite any index set by the underlying encoder.
    stream_image.SetSimulcastIndex(stream_idx);
  }

  if (codec_.IsMixedCodec()) {
    stream_image.SetSpatialIndex(std::nullopt);
  }

  bool is_last_in_temporal_unit = false;
  if (!bypass_mode_) {
    MutexLock lock(&pending_frames_mutex_);
    auto it = std::find_if(pending_frames_.begin(), pending_frames_.end(),
                           [&](const PendingFrame& frame) {
                             return frame.rtp_timestamp ==
                                    encodedImage.RtpTimestamp();
                           });
    if (it != pending_frames_.end()) {
      it->expected_layer_index.reset(stream_idx);
      if (it->expected_layer_index.none()) {
        pending_frames_.erase(it);
        is_last_in_temporal_unit = true;
      }
    } else {
      // The frame was removed from the queue (e.g. due to overflow).
      // We should drop this encoded image and not propagate it.
      return EncodedImageCallback::Result(EncodedImageCallback::Result::OK,
                                          encodedImage.RtpTimestamp());
    }
    stream_image.set_end_of_temporal_unit(is_last_in_temporal_unit);
  }

  return encoded_complete_callback_->OnEncodedImage(stream_image,
                                                    codecSpecificInfo);
}

void SimulcastEncoderAdapter::OnFrameDropped(uint32_t rtp_timestamp,
                                             int spatial_id) {
  bool is_last_in_temporal_unit = false;
  {
    MutexLock lock(&pending_frames_mutex_);
    auto it = std::find_if(pending_frames_.begin(), pending_frames_.end(),
                           [rtp_timestamp](const PendingFrame& frame) {
                             return frame.rtp_timestamp == rtp_timestamp;
                           });
    if (it != pending_frames_.end()) {
      it->expected_layer_index.reset(spatial_id);
      if (it->expected_layer_index.none()) {
        pending_frames_.erase(it);
        is_last_in_temporal_unit = true;
      }
    } else {
      // Already removed, ignore.
      return;
    }
  }

  encoded_complete_callback_->OnFrameDropped(rtp_timestamp, spatial_id,
                                             is_last_in_temporal_unit);
}

bool SimulcastEncoderAdapter::Initialized() const {
  return inited_.load() == 1;
}

void SimulcastEncoderAdapter::DestroyStoredEncoders() {
  RTC_DCHECK_RUN_ON(&encoder_queue_checker_);
  while (!cached_encoder_contexts_.empty()) {
    cached_encoder_contexts_.pop_back();
  }
}

std::unique_ptr<SimulcastEncoderAdapter::EncoderContext>
SimulcastEncoderAdapter::FetchOrCreateEncoderContext(
    bool is_lowest_quality_stream,
    std::optional<int> stream_idx) const {
  RTC_DCHECK_RUN_ON(&encoder_queue_checker_);
  if (stream_idx) {
    RTC_CHECK_LT(*stream_idx, codec_.numberOfSimulcastStreams);
  }
  SdpVideoFormat video_format =
      stream_idx
          ? codec_.simulcastStream[*stream_idx].format.value_or(video_format_)
          : video_format_;
  bool prefer_temporal_support = fallback_encoder_factory_ != nullptr &&
                                 is_lowest_quality_stream &&
                                 prefer_temporal_support_on_base_layer_;

  // Toggling of `prefer_temporal_support` requires encoder recreation. Find
  // and reuse encoder with desired `prefer_temporal_support` and
  // `video_format`. Otherwise, if there is no such encoder in the cache, create
  // a new instance.
  auto encoder_context_iter =
      std::find_if(cached_encoder_contexts_.begin(),
                   cached_encoder_contexts_.end(), [&](auto& encoder_context) {
                     return encoder_context->prefer_temporal_support() ==
                                prefer_temporal_support &&
                            encoder_context->video_format() == video_format;
                   });

  std::unique_ptr<SimulcastEncoderAdapter::EncoderContext> encoder_context;
  if (encoder_context_iter != cached_encoder_contexts_.end()) {
    encoder_context = std::move(*encoder_context_iter);
    cached_encoder_contexts_.erase(encoder_context_iter);
  } else {
    std::unique_ptr<VideoEncoder> primary_encoder =
        primary_encoder_factory_->Create(env_, video_format);

    std::unique_ptr<VideoEncoder> fallback_encoder;
    if (fallback_encoder_factory_ != nullptr) {
      fallback_encoder = fallback_encoder_factory_->Create(env_, video_format);
    }

    std::unique_ptr<VideoEncoder> encoder;
    VideoEncoder::EncoderInfo primary_info;
    VideoEncoder::EncoderInfo fallback_info;

    if (primary_encoder != nullptr) {
      primary_info = primary_encoder->GetEncoderInfo();
      fallback_info = primary_info;

      if (fallback_encoder == nullptr) {
        encoder = std::move(primary_encoder);
      } else {
        encoder = CreateVideoEncoderSoftwareFallbackWrapper(
            env_, std::move(fallback_encoder), std::move(primary_encoder),
            prefer_temporal_support);
      }
    } else if (fallback_encoder != nullptr) {
      RTC_LOG(LS_WARNING) << "Failed to create primary " << video_format.name
                          << " encoder. Use fallback encoder.";
      fallback_info = fallback_encoder->GetEncoderInfo();
      primary_info = fallback_info;
      encoder = std::move(fallback_encoder);
    } else {
      RTC_LOG(LS_ERROR) << "Failed to create primary and fallback "
                        << video_format.name << " encoders.";
      return nullptr;
    }

    encoder_context = std::make_unique<SimulcastEncoderAdapter::EncoderContext>(
        std::move(encoder), prefer_temporal_support, std::move(primary_info),
        std::move(fallback_info), std::move(video_format));
  }

  encoder_context->encoder().RegisterEncodeCompleteCallback(
      encoded_complete_callback_);
  return encoder_context;
}

VideoCodec SimulcastEncoderAdapter::MakeStreamCodec(
    const VideoCodec& codec,
    int stream_idx,
    uint32_t start_bitrate_kbps,
    bool is_lowest_quality_stream,
    bool is_highest_quality_stream) {
  VideoCodec codec_params = codec;
  const SimulcastStream& stream_params = codec.simulcastStream[stream_idx];
  webrtc::VideoCodecType codec_type =
      stream_params.format
          ? PayloadStringToCodecType(stream_params.format->name)
          : codec.codecType;

  codec_params.codecType = codec_type;
  codec_params.numberOfSimulcastStreams = 0;
  codec_params.width = stream_params.width;
  codec_params.height = stream_params.height;
  codec_params.maxBitrate = stream_params.maxBitrate;
  codec_params.minBitrate = stream_params.minBitrate;
  codec_params.maxFramerate = stream_params.maxFramerate;
  codec_params.qpMax = stream_params.qpMax;
  codec_params.active = stream_params.active;
  // By default, `scalability_mode` comes from SimulcastStream when
  // SimulcastEncoderAdapter is used. This allows multiple encodings of L1Tx,
  // but SimulcastStream currently does not support multiple spatial layers.
  std::optional<ScalabilityMode> scalability_mode =
      stream_params.GetScalabilityMode();
  // To support the full set of scalability modes in the event that this is the
  // only active encoding, prefer VideoCodec::GetScalabilityMode() - but only if
  // it's the first simulcast layer in the list that is the only active one.
  bool only_active_stream = false;
  if (stream_idx == 0 && codec.simulcastStream[0].active) {
    only_active_stream = true;
    for (int i = 1; i < codec.numberOfSimulcastStreams; ++i) {
      if (codec.simulcastStream[i].active) {
        only_active_stream = false;
        break;
      }
    }
  }

  if (codec.GetScalabilityMode().has_value() && only_active_stream) {
    scalability_mode = codec.GetScalabilityMode();
  }
  if (scalability_mode.has_value()) {
    codec_params.SetScalabilityMode(*scalability_mode);
  }
  // Settings that are based on stream/resolution.
  if (is_lowest_quality_stream) {
    // Settings for lowest spatial resolutions.
    if (codec.mode == VideoCodecMode::kRealtimeVideo &&
        boost_base_layer_quality_) {
      codec_params.qpMax = kLowestResMaxQp;
    }
  }

  // Ensure default codec specifics matches the correct codec type for this
  // stream. This can only differ in mixed-codec simulcast.
  if (codec_type != codec.codecType) {
    switch (codec_type) {
      case kVideoCodecVP8:
        *codec_params.VP8() = VideoEncoder::GetDefaultVp8Settings();
        break;
      case kVideoCodecVP9:
        *codec_params.VP9() = VideoEncoder::GetDefaultVp9Settings();
        break;
      case kVideoCodecH264:
        *codec_params.H264() = VideoEncoder::GetDefaultH264Settings();
        break;
      case kVideoCodecAV1:
        memset(codec_params.AV1(), 0, sizeof(VideoCodecAV1));
        break;
      default:
        break;
    }
  }

  if (codec_type == kVideoCodecVP8) {
    codec_params.VP8()->numberOfTemporalLayers =
        stream_params.numberOfTemporalLayers;
    if (!is_highest_quality_stream) {
      // For resolutions below CIF, set the codec `complexity` parameter to
      // kComplexityHigher, which maps to cpu_used = -4.
      int pixels_per_frame = codec_params.width * codec_params.height;
      if (pixels_per_frame < 352 * 288) {
        codec_params.SetVideoEncoderComplexity(
            VideoCodecComplexity::kComplexityHigher);
      }
      // Turn off denoising for all streams but the highest resolution.
      codec_params.VP8()->denoisingOn = false;
    }
  } else if (codec_type == kVideoCodecH264) {
    codec_params.H264()->numberOfTemporalLayers =
        stream_params.numberOfTemporalLayers;
  } else if (codec_type == kVideoCodecVP9 && scalability_mode.has_value() &&
             !only_active_stream) {
    // If VP9 simulcast then explicitly set a single spatial layer for each
    // simulcast stream.
    codec_params.VP9()->numberOfSpatialLayers = 1;
    codec_params.VP9()->numberOfTemporalLayers =
        stream_params.GetNumberOfTemporalLayers();
    codec_params.VP9()->interLayerPred = InterLayerPredMode::kOff;
    codec_params.spatialLayers[0] = stream_params;
  }

  // Cap start bitrate to the min bitrate in order to avoid strange codec
  // behavior.
  codec_params.startBitrate =
      std::max(stream_params.minBitrate, start_bitrate_kbps);

  // Legacy screenshare mode is only enabled for the first simulcast layer
  codec_params.legacy_conference_mode =
      codec.legacy_conference_mode && stream_idx == 0;

  return codec_params;
}

void SimulcastEncoderAdapter::OverrideFromFieldTrial(
    VideoEncoder::EncoderInfo* info) const {
  if (encoder_info_override_.requested_resolution_alignment()) {
    info->requested_resolution_alignment =
        std::lcm(info->requested_resolution_alignment,
                 *encoder_info_override_.requested_resolution_alignment());
    info->apply_alignment_to_all_simulcast_layers =
        info->apply_alignment_to_all_simulcast_layers ||
        encoder_info_override_.apply_alignment_to_all_simulcast_layers();
  }
  // Override resolution bitrate limits unless they're set already.
  if (info->resolution_bitrate_limits.empty() &&
      !encoder_info_override_.resolution_bitrate_limits().empty()) {
    info->resolution_bitrate_limits =
        encoder_info_override_.resolution_bitrate_limits();
  }
}

VideoEncoder::EncoderInfo SimulcastEncoderAdapter::GetEncoderInfo() const {
  if (stream_contexts_.size() == 1) {
    // Not using simulcast adapting functionality, just pass through.
    VideoEncoder::EncoderInfo info =
        stream_contexts_.front().encoder().GetEncoderInfo();
    OverrideFromFieldTrial(&info);
    return info;
  }

  VideoEncoder::EncoderInfo encoder_info;
  encoder_info.implementation_name = "SimulcastEncoderAdapter";
  encoder_info.requested_resolution_alignment = 1;
  encoder_info.apply_alignment_to_all_simulcast_layers = false;
  encoder_info.supports_native_handle = true;
  encoder_info.scaling_settings.thresholds = std::nullopt;

  if (stream_contexts_.empty()) {
    // GetEncoderInfo queried before InitEncode. Only alignment info is needed
    // to be filled.
    // Create one encoder and query it.

    std::unique_ptr<SimulcastEncoderAdapter::EncoderContext> encoder_context =
        FetchOrCreateEncoderContext(/*is_lowest_quality_stream=*/true,
                                    std::nullopt);
    if (encoder_context == nullptr) {
      return encoder_info;
    }

    const VideoEncoder::EncoderInfo& primary_info =
        encoder_context->PrimaryInfo();
    const VideoEncoder::EncoderInfo& fallback_info =
        encoder_context->FallbackInfo();

    encoder_info.requested_resolution_alignment =
        std::lcm(primary_info.requested_resolution_alignment,
                 fallback_info.requested_resolution_alignment);
    // Query the encoder object itself, not `primary_info`: with a fallback
    // factory it is a software fallback wrapper, which reports its fallback
    // encoder's limit. This keeps the value identical to what the per-layer
    // encoders report after InitEncode.
    encoder_info.max_pixels_per_frame =
        encoder_context->encoder().GetEncoderInfo().max_pixels_per_frame;

    encoder_info.apply_alignment_to_all_simulcast_layers =
        primary_info.apply_alignment_to_all_simulcast_layers ||
        fallback_info.apply_alignment_to_all_simulcast_layers;

    if (!primary_info.supports_simulcast || !fallback_info.supports_simulcast) {
      encoder_info.apply_alignment_to_all_simulcast_layers = true;
    }

    cached_encoder_contexts_.emplace_back(std::move(encoder_context));

    OverrideFromFieldTrial(&encoder_info);
    return encoder_info;
  }

  // Default is to not enable CPU overuse detection. Aggregate across
  // sub-encoders so we enable it if any encoder opts in.
  encoder_info.enable_cpu_overuse_detection = false;

  encoder_info.scaling_settings = VideoEncoder::ScalingSettings::kOff;
  std::vector<std::string> encoder_names;

  for (size_t i = 0; i < stream_contexts_.size(); ++i) {
    VideoEncoder::EncoderInfo encoder_impl_info =
        stream_contexts_[i].encoder().GetEncoderInfo();

    // Encoder name indicates names of all active sub-encoders.
    if (!stream_contexts_[i].is_paused()) {
      encoder_names.push_back(encoder_impl_info.implementation_name);
    }

    if (encoder_impl_info.mapped_resolution.has_value() &&
        (!encoder_info.mapped_resolution.has_value() ||
         encoder_info.mapped_resolution->width <
             encoder_impl_info.mapped_resolution->width)) {
      encoder_info.mapped_resolution = encoder_impl_info.mapped_resolution;
    }

    if (i == 0) {
      encoder_info.supports_native_handle =
          encoder_impl_info.supports_native_handle;
      encoder_info.has_trusted_rate_controller =
          encoder_impl_info.has_trusted_rate_controller;
      encoder_info.is_hardware_accelerated =
          encoder_impl_info.is_hardware_accelerated;
      encoder_info.is_qp_trusted = encoder_impl_info.is_qp_trusted;
      encoder_info.min_qp = encoder_impl_info.min_qp;
    } else {
      // Native handle supported if any encoder supports it.
      encoder_info.supports_native_handle |=
          encoder_impl_info.supports_native_handle;

      // Trusted rate controller only if all encoders have it.
      encoder_info.has_trusted_rate_controller &=
          encoder_impl_info.has_trusted_rate_controller;

      // Uses hardware support if any of the encoders uses it.
      // For example, if we are having issues with down-scaling due to
      // pipelining delay in HW encoders we need higher encoder usage
      // thresholds in CPU adaptation.
      encoder_info.is_hardware_accelerated |=
          encoder_impl_info.is_hardware_accelerated;

      // Treat QP from frame/slice/tile header as average QP only if all
      // encoders report it as average QP.
      encoder_info.is_qp_trusted =
          encoder_info.is_qp_trusted.value_or(true) &&
          encoder_impl_info.is_qp_trusted.value_or(true);

      if (encoder_info.min_qp != encoder_impl_info.min_qp) {
        encoder_info.min_qp = std::nullopt;
      }
    }
    // If any encoder wants CPU overuse detection, enable it for all of them
    encoder_info.enable_cpu_overuse_detection |=
        encoder_impl_info.enable_cpu_overuse_detection;
    encoder_info.fps_allocation[i] = encoder_impl_info.fps_allocation[0];
    encoder_info.requested_resolution_alignment =
        std::lcm(encoder_info.requested_resolution_alignment,
                 encoder_impl_info.requested_resolution_alignment);
    // Smallest limit across sub-encoders. Conservative: a limit reported by
    // a low-resolution layer's encoder is applied to the full-size source.
    if (encoder_impl_info.max_pixels_per_frame.has_value() &&
        (!encoder_info.max_pixels_per_frame.has_value() ||
         *encoder_impl_info.max_pixels_per_frame <
             *encoder_info.max_pixels_per_frame)) {
      encoder_info.max_pixels_per_frame =
          encoder_impl_info.max_pixels_per_frame;
    }
    // request alignment on all layers if any of the encoders may need it, or
    // if any non-top layer encoder requests a non-trivial alignment.
    if (encoder_impl_info.apply_alignment_to_all_simulcast_layers ||
        (encoder_impl_info.requested_resolution_alignment > 1 &&
         (codec_.simulcastStream[i].height < codec_.height ||
          codec_.simulcastStream[i].width < codec_.width))) {
      encoder_info.apply_alignment_to_all_simulcast_layers = true;
    }
  }

  if (!encoder_names.empty()) {
    StringBuilder implementation_name_builder(" (");
    implementation_name_builder << StrJoin(encoder_names, ", ");
    implementation_name_builder << ")";
    encoder_info.implementation_name += implementation_name_builder.Release();
  }

  OverrideFromFieldTrial(&encoder_info);

  return encoder_info;
}

}  // namespace webrtc
