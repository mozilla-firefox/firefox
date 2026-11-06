/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/pc/e2e/analyzer/video/quality_analyzing_video_encoder.h"

#include <cmath>
#include <memory>
#include <utility>

#include "absl/strings/string_view.h"
#include "api/environment/environment.h"
#include "api/video/video_codec_type.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "modules/video_coding/svc/scalability_mode_util.h"
#include "rtc_base/logging.h"

namespace webrtc {
namespace webrtc_pc_e2e {
namespace {

using EmulatedSFUConfigMap =
    ::webrtc::webrtc_pc_e2e::QualityAnalyzingVideoEncoder::EmulatedSFUConfigMap;

constexpr size_t kMaxFrameInPipelineCount = 1000;
constexpr double kNoMultiplier = 1.0;
constexpr double kEps = 1e-6;

std::pair<uint32_t, uint32_t> GetMinMaxBitratesBps(const VideoCodec& codec,
                                                   size_t spatial_idx) {
  uint32_t min_bitrate = codec.minBitrate;
  uint32_t max_bitrate = codec.maxBitrate;
  if (spatial_idx < codec.numberOfSimulcastStreams &&
      codec.codecType != VideoCodecType::kVideoCodecVP9) {
    min_bitrate =
        std::max(min_bitrate, codec.simulcastStream[spatial_idx].minBitrate);
    max_bitrate =
        std::min(max_bitrate, codec.simulcastStream[spatial_idx].maxBitrate);
  }
  if (codec.codecType == VideoCodecType::kVideoCodecVP9 &&
      spatial_idx < codec.VP9().numberOfSpatialLayers) {
    min_bitrate =
        std::max(min_bitrate, codec.spatialLayers[spatial_idx].minBitrate);
    max_bitrate =
        std::min(max_bitrate, codec.spatialLayers[spatial_idx].maxBitrate);
  }
  RTC_DCHECK_GT(max_bitrate, min_bitrate);
  return {min_bitrate * 1000, max_bitrate * 1000};
}

}  // namespace

QualityAnalyzingVideoEncoder::QualityAnalyzingVideoEncoder(
    absl::string_view peer_name,
    std::unique_ptr<VideoEncoder> delegate,
    double bitrate_multiplier,
    EmulatedSFUConfigMap stream_to_sfu_config,
    EncodedImageDataInjector* injector,
    VideoQualityAnalyzerInterface* analyzer)
    : peer_name_(peer_name),
      delegate_(std::move(delegate)),
      bitrate_multiplier_(bitrate_multiplier),
      stream_to_sfu_config_(std::move(stream_to_sfu_config)),
      injector_(injector),
      analyzer_(analyzer),
      mode_(SimulcastMode::kNormal),
      delegate_callback_(nullptr) {}
QualityAnalyzingVideoEncoder::~QualityAnalyzingVideoEncoder() = default;

void QualityAnalyzingVideoEncoder::SetFecControllerOverride(
    FecControllerOverride* fec_controller_override) {
  // Ignored.
}

int32_t QualityAnalyzingVideoEncoder::InitEncode(
    const VideoCodec* codec_settings,
    const Settings& settings) {
  MutexLock lock(&mutex_);
  codec_settings_ = *codec_settings;
  mode_ = SimulcastMode::kNormal;
  absl::optional<InterLayerPredMode> inter_layer_pred_mode;
  if (codec_settings->GetScalabilityMode().has_value()) {
    inter_layer_pred_mode = ScalabilityModeToInterLayerPredMode(
        *codec_settings->GetScalabilityMode());
  } else if (codec_settings->codecType == kVideoCodecVP9) {
    if (codec_settings->VP9().numberOfSpatialLayers > 1) {
      inter_layer_pred_mode = codec_settings->VP9().interLayerPred;
    }
  }
  if (inter_layer_pred_mode.has_value()) {
    switch (*inter_layer_pred_mode) {
      case InterLayerPredMode::kOn:
        mode_ = SimulcastMode::kSVC;
        break;
      case InterLayerPredMode::kOnKeyPic:
        mode_ = SimulcastMode::kKSVC;
        break;
      case InterLayerPredMode::kOff:
        mode_ = SimulcastMode::kSimulcast;
        break;
      default:
        RTC_DCHECK_NOTREACHED()
            << "Unknown InterLayerPredMode value " << *inter_layer_pred_mode;
        break;
    }
  }
  if (codec_settings->numberOfSimulcastStreams > 1) {
    mode_ = SimulcastMode::kSimulcast;
  }
  return delegate_->InitEncode(codec_settings, settings);
}

int32_t QualityAnalyzingVideoEncoder::RegisterEncodeCompleteCallback(
    EncodedImageCallback* callback) {
  // We need to get a lock here because delegate_callback can be hypothetically
  // accessed from different thread (encoder one) concurrently.
  MutexLock lock(&mutex_);
  delegate_callback_ = callback;
  return delegate_->RegisterEncodeCompleteCallback(this);
}

int32_t QualityAnalyzingVideoEncoder::Release() {
  // Release encoder first. During release process it can still encode some
  // frames, so we don't take a lock to prevent deadlock.
  int32_t result = delegate_->Release();

  MutexLock lock(&mutex_);
  delegate_callback_ = nullptr;
  return result;
}

int32_t QualityAnalyzingVideoEncoder::Encode(
    const VideoFrame& frame,
    const std::vector<VideoFrameType>* frame_types) {
  {
    MutexLock lock(&mutex_);
    // Store id to be able to retrieve it in analyzing callback.
    timestamp_to_frame_id_list_.push_back({frame.rtp_timestamp(), frame.id()});
    // If this list is growing, it means that we are not receiving new encoded
    // images from encoder. So it should be a bug in setup on in the encoder.
    RTC_DCHECK_LT(timestamp_to_frame_id_list_.size(), kMaxFrameInPipelineCount);
  }
  analyzer_->OnFramePreEncode(peer_name_, frame);
  int32_t result = delegate_->Encode(frame, frame_types);
  if (result != WEBRTC_VIDEO_CODEC_OK) {
    // If origin encoder failed, then cleanup data for this frame.
    {
      MutexLock lock(&mutex_);
      // The timestamp-frame_id pair can be not the last one, so we need to
      // find it first and then remove. We will search from the end, because
      // usually it will be the last or close to the last one.
      auto it = timestamp_to_frame_id_list_.end();
      while (it != timestamp_to_frame_id_list_.begin()) {
        --it;
        if (it->first == frame.rtp_timestamp()) {
          timestamp_to_frame_id_list_.erase(it);
          break;
        }
      }
    }
    analyzer_->OnEncoderError(peer_name_, frame, result);
  }
  return result;
}

void QualityAnalyzingVideoEncoder::SetRates(
    const VideoEncoder::RateControlParameters& parameters) {
  RTC_DCHECK_GT(bitrate_multiplier_, 0.0);
  if (fabs(bitrate_multiplier_ - kNoMultiplier) < kEps) {
    {
      MutexLock lock(&mutex_);
      bitrate_allocation_ = parameters.bitrate;
    }
    return delegate_->SetRates(parameters);
  }

  RateControlParameters adjusted_params = parameters;
  {
    MutexLock lock(&mutex_);
    // Simulating encoder overshooting target bitrate, by configuring actual
    // encoder too high. Take care not to adjust past limits of config,
    // otherwise encoders may crash on DCHECK.
    VideoBitrateAllocation multiplied_allocation;
    for (size_t si = 0; si < kMaxSpatialLayers; ++si) {
      const uint32_t spatial_layer_bitrate_bps =
          parameters.bitrate.GetSpatialLayerSum(si);
      if (spatial_layer_bitrate_bps == 0) {
        continue;
      }

      uint32_t min_bitrate_bps;
      uint32_t max_bitrate_bps;
      std::tie(min_bitrate_bps, max_bitrate_bps) =
          GetMinMaxBitratesBps(codec_settings_, si);
      double bitrate_multiplier = bitrate_multiplier_;
      const uint32_t corrected_bitrate = rtc::checked_cast<uint32_t>(
          bitrate_multiplier * spatial_layer_bitrate_bps);
      if (corrected_bitrate < min_bitrate_bps) {
        bitrate_multiplier = min_bitrate_bps / spatial_layer_bitrate_bps;
      } else if (corrected_bitrate > max_bitrate_bps) {
        bitrate_multiplier = max_bitrate_bps / spatial_layer_bitrate_bps;
      }

      for (size_t ti = 0; ti < kMaxTemporalStreams; ++ti) {
        if (parameters.bitrate.HasBitrate(si, ti)) {
          multiplied_allocation.SetBitrate(
              si, ti,
              rtc::checked_cast<uint32_t>(
                  bitrate_multiplier * parameters.bitrate.GetBitrate(si, ti)));
        }
      }
    }

    adjusted_params.bitrate = multiplied_allocation;
    bitrate_allocation_ = adjusted_params.bitrate;
  }
  return delegate_->SetRates(adjusted_params);
}

VideoEncoder::EncoderInfo QualityAnalyzingVideoEncoder::GetEncoderInfo() const {
  return delegate_->GetEncoderInfo();
}

// It is assumed, that encoded callback will be always invoked with encoded
// images that correspond to the frames in the same sequence, that frames
// arrived. In other words, assume we have frames F1, F2 and F3 and they have
// corresponding encoded images I1, I2 and I3. In such case if we will call
// encode first with F1, then with F2 and then with F3, then encoder callback
// will be called first with all spatial layers for F1 (I1), then F2 (I2) and
// then F3 (I3).
//
// Basing on it we will use a list of timestamp-frame_id pairs like this:
//  1. If current encoded image timestamp is equals to timestamp in the front
//     pair - pick frame id from that pair
//  2. If current encoded image timestamp isn't equals to timestamp in the front
//     pair - remove the front pair and got to the step 1.
EncodedImageCallback::Result QualityAnalyzingVideoEncoder::OnEncodedImage(
    const EncodedImage& encoded_image,
    const CodecSpecificInfo* codec_specific_info) {
  uint16_t frame_id;
  bool discard = false;
  uint32_t target_encode_bitrate = 0;
  std::string codec_name;
  {
    MutexLock lock(&mutex_);
    std::pair<uint32_t, uint16_t> timestamp_frame_id;
    while (!timestamp_to_frame_id_list_.empty()) {
      timestamp_frame_id = timestamp_to_frame_id_list_.front();
      if (timestamp_frame_id.first == encoded_image.RtpTimestamp()) {
        break;
      }
      timestamp_to_frame_id_list_.pop_front();
    }

    // After the loop the first element should point to current `encoded_image`
    // frame id. We don't remove it from the list, because there may be
    // multiple spatial layers for this frame, so encoder can produce more
    // encoded images with this timestamp. The first element will be removed
    // when the next frame would be encoded and EncodedImageCallback would be
    // called with the next timestamp.

    if (timestamp_to_frame_id_list_.empty()) {
      // Ensure, that we have info about this frame. It can happen that for some
      // reasons encoder response, that he failed to decode, when we were
      // posting frame to it, but then call the callback for this frame.
      RTC_LOG(LS_ERROR) << "QualityAnalyzingVideoEncoder::OnEncodedImage: No "
                           "frame id for encoded_image.Timestamp()="
                        << encoded_image.RtpTimestamp();
      return EncodedImageCallback::Result(
          EncodedImageCallback::Result::Error::OK);
    }
    frame_id = timestamp_frame_id.second;

    discard = ShouldDiscard(frame_id, encoded_image);
    if (!discard) {
      // We could either have simulcast layers or spatial layers.
      // TODO(https://crbug.com/webrtc/14891): If we want to support a mix of
      // simulcast and SVC we'll also need to consider the case where we have
      // both simulcast and spatial indices.
      size_t stream_index = encoded_image.SpatialIndex().value_or(
          encoded_image.SimulcastIndex().value_or(0));
      target_encode_bitrate =
          bitrate_allocation_.GetSpatialLayerSum(stream_index);
    }
    codec_name =
        std::string(CodecTypeToPayloadString(codec_settings_.codecType)) + "_" +
        delegate_->GetEncoderInfo().implementation_name;
  }

  VideoQualityAnalyzerInterface::EncoderStats stats;
  stats.encoder_name = codec_name;
  stats.target_encode_bitrate = target_encode_bitrate;
  stats.qp = encoded_image.qp_;
  analyzer_->OnFrameEncoded(peer_name_, frame_id, encoded_image, stats,
                            discard);

  // Image data injector injects frame id and discard flag into provided
  // EncodedImage and returns the image with a) modified original buffer (in
  // such case the current owner of the buffer will be responsible for deleting
  // it) or b) a new buffer (in such case injector will be responsible for
  // deleting it).
  const EncodedImage& image =
      injector_->InjectData(frame_id, discard, encoded_image);
  {
    MutexLock lock(&mutex_);
    RTC_DCHECK(delegate_callback_);
    return delegate_callback_->OnEncodedImage(image, codec_specific_info);
  }
}

void QualityAnalyzingVideoEncoder::OnDroppedFrame(
    EncodedImageCallback::DropReason reason) {
  MutexLock lock(&mutex_);
  analyzer_->OnFrameDropped(peer_name_, reason);
  RTC_DCHECK(delegate_callback_);
  delegate_callback_->OnDroppedFrame(reason);
}

bool QualityAnalyzingVideoEncoder::ShouldDiscard(
    uint16_t frame_id,
    const EncodedImage& encoded_image) {
  std::string stream_label = analyzer_->GetStreamLabel(frame_id);
  EmulatedSFUConfigMap::mapped_type emulated_sfu_config =
      stream_to_sfu_config_[stream_label];

  if (!emulated_sfu_config)
    return false;

  // We could either have simulcast layers or spatial layers.
  // TODO(https://crbug.com/webrtc/14891): If we want to support a mix of
  // simulcast and SVC we'll also need to consider the case where we have both
  // simulcast and spatial indices.
  int cur_stream_index = encoded_image.SpatialIndex().value_or(
      encoded_image.SimulcastIndex().value_or(0));
  int cur_temporal_index = encoded_image.TemporalIndex().value_or(0);

  if (emulated_sfu_config->target_temporal_index &&
      cur_temporal_index > *emulated_sfu_config->target_temporal_index)
    return true;

  if (emulated_sfu_config->target_layer_index) {
    switch (mode_) {
      case SimulcastMode::kSimulcast:
        // In simulcast mode only encoded images with required spatial index are
        // interested, so all others have to be discarded.
        return cur_stream_index != *emulated_sfu_config->target_layer_index;
      case SimulcastMode::kSVC:
        // In SVC mode encoded images with spatial indexes that are equal or
        // less than required one are interesting, so all above have to be
        // discarded.
        return cur_stream_index > *emulated_sfu_config->target_layer_index;
      case SimulcastMode::kKSVC:
        // In KSVC mode for key frame encoded images with spatial indexes that
        // are equal or less than required one are interesting, so all above
        // have to be discarded. For other frames only required spatial index
        // is interesting, so all others except the ones depending on the
        // keyframes can be discarded. There's no good test for that, so we keep
        // all of temporal layer 0 for now.
        if (encoded_image._frameType == VideoFrameType::kVideoFrameKey ||
            cur_temporal_index == 0)
          return cur_stream_index > *emulated_sfu_config->target_layer_index;
        return cur_stream_index != *emulated_sfu_config->target_layer_index;
      case SimulcastMode::kNormal:
        RTC_DCHECK_NOTREACHED() << "Analyzing encoder is in kNormal mode, but "
                                   "target_layer_index is set";
    }
  }
  return false;
}

QualityAnalyzingVideoEncoderFactory::QualityAnalyzingVideoEncoderFactory(
    absl::string_view peer_name,
    std::unique_ptr<VideoEncoderFactory> delegate,
    double bitrate_multiplier,
    EmulatedSFUConfigMap stream_to_sfu_config,
    EncodedImageDataInjector* injector,
    VideoQualityAnalyzerInterface* analyzer)
    : peer_name_(peer_name),
      delegate_(std::move(delegate)),
      bitrate_multiplier_(bitrate_multiplier),
      stream_to_sfu_config_(std::move(stream_to_sfu_config)),
      injector_(injector),
      analyzer_(analyzer) {}
QualityAnalyzingVideoEncoderFactory::~QualityAnalyzingVideoEncoderFactory() =
    default;

std::vector<SdpVideoFormat>
QualityAnalyzingVideoEncoderFactory::GetSupportedFormats() const {
  return delegate_->GetSupportedFormats();
}

VideoEncoderFactory::CodecSupport
QualityAnalyzingVideoEncoderFactory::QueryCodecSupport(
    const SdpVideoFormat& format,
    absl::optional<std::string> scalability_mode) const {
  return delegate_->QueryCodecSupport(format, scalability_mode);
}

std::unique_ptr<VideoEncoder> QualityAnalyzingVideoEncoderFactory::Create(
    const Environment& env,
    const SdpVideoFormat& format) {
  return std::make_unique<QualityAnalyzingVideoEncoder>(
      peer_name_, delegate_->Create(env, format), bitrate_multiplier_,
      stream_to_sfu_config_, injector_, analyzer_);
}

}  // namespace webrtc_pc_e2e
}  // namespace webrtc
