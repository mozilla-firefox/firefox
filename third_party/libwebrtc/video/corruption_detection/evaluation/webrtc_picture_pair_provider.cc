/*
 * Copyright 2025 The WebRTC project authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file in the root of the source
 * tree. An additional intellectual property rights grant can be found
 * in the file PATENTS.  All contributing project authors may
 * be found in the AUTHORS file in the root of the source tree.
 */

#include "video/corruption_detection/evaluation/webrtc_picture_pair_provider.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "api/environment/environment.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/units/data_rate.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/resolution.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_type.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "modules/video_coding/svc/svc_rate_allocator.h"
#include "modules/video_coding/utility/simulcast_rate_allocator.h"
#include "modules/video_coding/utility/simulcast_utility.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"
#include "test/testsupport/frame_reader.h"
#include "video/corruption_detection/evaluation/picture_pair_provider.h"
#include "video/corruption_detection/evaluation/test_clip.h"
#include "video/corruption_detection/evaluation/utils.h"

namespace webrtc {

namespace {

// Chosen inspired from third_party/webrtc/video/video_stream_encoder.cc
constexpr int32_t kMaxPayloadSize = 1500;

// 90kHz clock for video.
constexpr int kRtpTimestampFreq = 90'000;

}  // namespace

WebRtcEncoderDecoderPicturePairProvider::
    WebRtcEncoderDecoderPicturePairProvider(
        VideoCodecType type,
        std::unique_ptr<VideoEncoderFactory> encoder_factory,
        std::unique_ptr<VideoDecoderFactory> decoder_factory)
    : WebRtcEncoderDecoderPicturePairProvider(CreateTestEnvironment(),
                                              type,
                                              std::move(encoder_factory),
                                              std::move(decoder_factory)) {}

WebRtcEncoderDecoderPicturePairProvider::
    WebRtcEncoderDecoderPicturePairProvider(
        const Environment& env,
        VideoCodecType type,
        std::unique_ptr<VideoEncoderFactory> encoder_factory,
        std::unique_ptr<VideoDecoderFactory> decoder_factory)
    : env_(env),
      type_(type),
      encoder_factory_(std::move(encoder_factory)),
      encoder_(encoder_factory_->Create(env_, GetSdpVideoFormat(type))),
      decoder_(decoder_factory->Create(env_, GetSdpVideoFormat(type))),
      is_initialized_(false) {
  RTC_CHECK(encoder_) << "encoder_ could not be initialized correctly.";
  RTC_CHECK(decoder_) << "decoder_ could not be initialized correctly.";

  int32_t status = encoder_->RegisterEncodeCompleteCallback(this);
  RTC_CHECK_EQ(status, WEBRTC_VIDEO_CODEC_OK)
      << "RegisterEncodeCompleteCallback could not succesfully be called on "
         "encoder_.";

  status = decoder_->RegisterDecodeCompleteCallback(this);
  RTC_CHECK_EQ(status, WEBRTC_VIDEO_CODEC_OK)
      << "RegisterDecodeCompleteCallback could not succesfully be called on "
         "decoder_.";
}

WebRtcEncoderDecoderPicturePairProvider::
    ~WebRtcEncoderDecoderPicturePairProvider() {
  encoder_->Release();
  decoder_->Release();
  encoder_->RegisterEncodeCompleteCallback(nullptr);
  decoder_->RegisterDecodeCompleteCallback(nullptr);
}

bool WebRtcEncoderDecoderPicturePairProvider::InitializeFrameGenerator(
    const TestClip& clip) {
  // Check if the file exists
  std::string clip_path = std::string(clip.clip_path());
  if (!test::FileExists(clip_path)) {
    RTC_LOG(LS_ERROR) << "Could not find clip " << clip_path;
    return false;
  }

  if (clip.is_yuv()) {
    frame_generator_ = CreateYuvFrameReader(
        clip_path, Resolution{.width = clip.width(), .height = clip.height()},
        test::YuvFrameReaderImpl::RepeatMode::kPingPong);
  } else {
    frame_generator_ = CreateY4mFrameReader(
        clip_path, test::YuvFrameReaderImpl::RepeatMode::kPingPong);
  }

  return true;
}

bool WebRtcEncoderDecoderPicturePairProvider::ConfigureEncoderSettings(
    const TestClip& clip,
    const DataRate bitrate) {
  codec_config_.width = clip.width();
  codec_config_.height = clip.height();
  codec_config_.maxFramerate = clip.framerate();
  codec_config_.codecType = type_;
  codec_config_.minBitrate = bitrate.kbps();
  codec_config_.startBitrate = bitrate.kbps();
  codec_config_.maxBitrate = bitrate.kbps();
  codec_config_.mode = clip.codec_mode();

  // `qpMax` values are chosen based on the limits in libaom, libvpx and
  // openh264 libraries' API limits respectively.
  switch (type_) {
    case VideoCodecType::kVideoCodecAV1:
      [[fallthrough]];
    case VideoCodecType::kVideoCodecVP9:
      codec_config_.SetScalabilityMode(ScalabilityMode::kL1T3);
      codec_config_.qpMax = 63;

      // Need to set the bitrates for each spatial layer manually. In the case
      // of L1T3 we only have 1 spatial layer, hence only setting index `0`.
      // This must be done such that the rate allocator can allocate correct
      // bitrates to each spatial and temporal layer.
      codec_config_.spatialLayers[0].targetBitrate =
          static_cast<unsigned int>(bitrate.kbps());
      codec_config_.spatialLayers[0].maxBitrate =
          static_cast<unsigned int>(bitrate.kbps());
      codec_config_.spatialLayers[0].active = true;
      break;
    case VideoCodecType::kVideoCodecVP8:
      codec_config_.VP8()->numberOfTemporalLayers = 3;
      codec_config_.qpMax = 63;
      break;
    case VideoCodecType::kVideoCodecH264:
      codec_config_.H264()->numberOfTemporalLayers = 3;
      codec_config_.qpMax = 51;

      // A simple hack because of how
      // `SimulcastRateAllocator::NumTemporalStreams` looks in
      // modules/video_coding/utility/simulcast_rate_allocator.cc.
      codec_config_.simulcastStream[0].numberOfTemporalLayers = 3;
      break;
    default:
      RTC_LOG(LS_ERROR) << "Codec type " << CodecTypeToPayloadString(type_)
                        << " is not supported.";
      RTC_DCHECK_NOTREACHED();
      return false;
  }
  return true;
}

bool WebRtcEncoderDecoderPicturePairProvider::InitializeEncoder() {
  constexpr VideoEncoder::Settings encoder_settings(
      /*number_of_cores = */ 1, /*max_payload_size = */ kMaxPayloadSize);

  if (encoder_->InitEncode(&codec_config_, encoder_settings) ==
      WEBRTC_VIDEO_CODEC_OK) {
    return true;
  } else {
    RTC_LOG(LS_ERROR) << "Failed to initialize encoder.";
    return false;
  }
}

void WebRtcEncoderDecoderPicturePairProvider::SetEncoderRate(
    const TestClip& clip,
    const DataRate bitrate) {
  VideoEncoder::RateControlParameters rate_params;

  switch (type_) {
    case VideoCodecType::kVideoCodecAV1:
      [[fallthrough]];
    case VideoCodecType::kVideoCodecVP9: {
      SvcRateAllocator svc_rate_allocator(codec_config_, env_.field_trials());
      rate_params = VideoEncoder::RateControlParameters(
          svc_rate_allocator.GetAllocation(bitrate.bps(), clip.framerate()),
          clip.framerate(), bitrate);
      break;
    }
    case VideoCodecType::kVideoCodecVP8:
      [[fallthrough]];
    case VideoCodecType::kVideoCodecH264: {
      SimulcastRateAllocator simulcast_rate_allocator(env_, codec_config_);
      rate_params = VideoEncoder::RateControlParameters(
          simulcast_rate_allocator.GetAllocation(bitrate.bps(),
                                                 clip.framerate()),
          clip.framerate(), bitrate);
      break;
    }
    default:
      RTC_LOG(LS_ERROR) << "Given codec type is not supported.";
      break;
  }

  // TODO: b/337750641 - For AV1 and VP9 screensharing, the bitrate is not set
  // for each temporal layer, hence the following check is needed to prevent
  // this from failing. Remove the following check once the bitrate is set
  // properly for each temporal layer in screensharing.
  if (!(clip.codec_mode() == VideoCodecMode::kScreensharing &&
        (type_ == VideoCodecType::kVideoCodecVP9 ||
         type_ == VideoCodecType::kVideoCodecAV1))) {
    // A simple check that the bitrate has been set for each temporal layer.
    const int num_temporal_layers =
        SimulcastUtility::NumberOfTemporalLayers(codec_config_,
                                                 /*spatial_id=*/0);
    for (int ti = 0; ti < num_temporal_layers; ++ti) {
      EXPECT_GT(rate_params.bitrate.GetBitrate(/*si=*/0, ti), 0u);
    }
  }

  encoder_->SetRates(rate_params);
}

bool WebRtcEncoderDecoderPicturePairProvider::InitializeDecoder() {
  VideoDecoder::Settings decoder_settings;
  decoder_settings.set_codec_type(type_);

  if (decoder_->Configure(decoder_settings)) {
    return true;
  } else {
    RTC_LOG(LS_ERROR) << "Failed to configure decoder.";
    return false;
  }
}

bool WebRtcEncoderDecoderPicturePairProvider::Configure(
    const TestClip& clip,
    const DataRate bitrate) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);

  if (is_initialized_) {
    encoder_->Release();
    decoder_->Release();
    is_initialized_ = false;
  }

  if (!InitializeFrameGenerator(clip)) {
    return false;
  }
  if (!ConfigureEncoderSettings(clip, bitrate)) {
    return false;
  }
  if (!InitializeEncoder()) {
    return false;
  }
  SetEncoderRate(clip, bitrate);
  if (!InitializeDecoder()) {
    return false;
  }

  // Operates at a 90kHz clock.
  rtp_timestamp_interval_ = kRtpTimestampFreq / clip.framerate();

  // Encoder & decoder have been initialized.
  is_initialized_ = true;

  return true;
}

std::optional<OriginalCompressedPicturePair>
WebRtcEncoderDecoderPicturePairProvider::GetNextPicturePair() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);

  RTC_CHECK(is_initialized_) << "Encoder and decoder have not been initialized."
                                " Try calling Configure first.";
  encoded_image_.reset();
  decoded_image_.reset();
  qp_.reset();

  // Read next frame.
  scoped_refptr<I420Buffer> buffer = frame_generator_->PullFrame();
  if (buffer == nullptr) {
    return std::nullopt;
  }
  VideoFrame input = VideoFrame::Builder()
                         .set_video_frame_buffer(buffer)
                         .set_rtp_timestamp(rtp_timestamp_)
                         .build();

  const std::vector<VideoFrameType> frame_type{
      is_keyframe_ ? VideoFrameType::kVideoFrameKey
                   : VideoFrameType::kVideoFrameDelta};
  // Set only the first frame to be a keyframe. If more keyframes are needed,
  // update this part.
  if (is_keyframe_) {
    is_keyframe_ = false;
  }

  // Update the RTP timestamp for the next frame. Needs to be done before the
  // `Encode` call. Because if the `Encode` call arises an error the
  // `rtp_timestamp_` should be updated to the next one.
  rtp_timestamp_ += rtp_timestamp_interval_;

  // Note: Only works for synchronous encoders/decoders.
  // This particular class only uses the built-in software encoders/decoders
  // which are all synchronous implementations.
  // If this class will be used for asynchronous encoder/decoders a wait logic
  // needs to be implemented after the `Encode` call so that we can be sure that
  // `encoded_image_` is set.
  if (encoder_->Encode(input, &frame_type) != WEBRTC_VIDEO_CODEC_OK) {
    RTC_LOG(LS_WARNING) << "Failed to encode input.";
    return std::nullopt;
  }

  if (!encoded_image_.has_value()) {
    // Encoder dropped the frame.
    return std::nullopt;
  }

  if (decoder_->Decode(*encoded_image_, /*render_time_ms=*/0) !=
      WEBRTC_VIDEO_CODEC_OK) {
    RTC_LOG(LS_WARNING) << "Failed to decode input.";
    return std::nullopt;
  }
  RTC_CHECK(decoded_image_.has_value()) << "Decoded image is not set.";
  decoded_image_->set_rtp_timestamp(input.rtp_timestamp());

  if (!qp_.has_value() && encoded_image_->qp_ > 0) {
    qp_ = encoded_image_->qp_;
  }
  RTC_CHECK(qp_.has_value()) << "QP is not set.";

  return OriginalCompressedPicturePair{.original_image = input,
                                       .compressed_image = *decoded_image_,
                                       .frame_average_qp = *qp_};
}

EncodedImageCallback::Result
WebRtcEncoderDecoderPicturePairProvider::OnEncodedImage(
    const EncodedImage& encoded_image,
    const CodecSpecificInfo*) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);

  encoded_image_.emplace(encoded_image);
  return EncodedImageCallback::Result(EncodedImageCallback::Result::Error::OK);
}

void WebRtcEncoderDecoderPicturePairProvider::OnFrameDropped(
    uint32_t rtp_timestamp,
    int spatial_id,
    bool is_end_of_temporal_unit) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);

  encoded_image_.reset();
}

int32_t WebRtcEncoderDecoderPicturePairProvider::Decoded(
    VideoFrame& decoded_image) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);

  decoded_image_.emplace(decoded_image);
  return 0;
}

}  // namespace webrtc
