/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "video/video_quality_test.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#ifdef WEBRTC_WIN
#include <conio.h>
#endif

#include "absl/flags/flag.h"
#include "api/audio/audio_device.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio/create_audio_device_module.h"
#include "api/call/transport.h"
#include "api/environment/environment.h"
#include "api/fec_controller_override.h"
#include "api/field_trials_view.h"
#include "api/make_ref_counted.h"
#include "api/rtc_event_log/rtc_event_log.h"
#include "api/rtc_event_log_output_file.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/task_queue_base.h"
#include "api/test/create_frame_generator.h"
#include "api/test/frame_generator_interface.h"
#include "api/test/simulated_network.h"
#include "api/units/time_delta.h"
#include "api/video/builtin_video_bitrate_allocator_factory.h"
#include "api/video/encoded_image.h"
#include "api/video/resolution.h"
#include "api/video/video_bitrate_allocation.h"
#include "api/video/video_codec_constants.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_source_interface.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/spatial_layer.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_encoder.h"
#include "call/audio_receive_stream.h"
#include "call/audio_send_stream.h"
#include "call/audio_state.h"
#include "call/call_config.h"
#include "call/fake_network_pipe.h"
#include "call/video_receive_stream.h"
#include "call/video_send_stream.h"
#include "media/base/media_constants.h"
#include "media/engine/adm_helpers.h"
#include "media/engine/fake_video_codec_factory.h"
#include "media/engine/internal_decoder_factory.h"
#include "media/engine/internal_encoder_factory.h"
#include "media/engine/simulcast_encoder_adapter.h"
#include "media/engine/webrtc_video_engine.h"
#include "modules/audio_device/include/test_audio_device.h"
#include "modules/audio_mixer/audio_mixer_impl.h"
#include "modules/video_coding/utility/ivf_file_writer.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/task_queue_for_test.h"
#include "test/call_test.h"
#include "test/create_test_field_trials.h"
#include "test/direct_transport.h"
#include "test/frame_generator_capturer.h"
#include "test/gtest.h"
#include "test/layer_filtering_transport.h"
#include "test/network/simulated_network.h"
#include "test/platform_video_capturer.h"
#include "test/test_flags.h"
#include "test/testsupport/file_utils.h"
#include "test/testsupport/pendulum_frame_generator.h"
#include "test/testsupport/switching_frame_reader.h"
#include "test/testsupport/y4m_frame_generator.h"
#include "test/video_renderer.h"
#include "test/video_test_constants.h"
#include "video/config/encoder_stream_factory.h"
#include "video/config/video_encoder_config.h"
#include "video/frame_dumping_decoder.h"
#include "video/video_analyzer.h"
#ifdef WEBRTC_WIN
#include "modules/audio_device/include/audio_device_factory.h"
#endif

namespace webrtc {

namespace {
constexpr RtpHeaderExtensionId kAbsSendTimeExtensionId(1);
constexpr RtpHeaderExtensionId kGenericFrameDescriptorExtensionId00(2);
constexpr RtpHeaderExtensionId kDependencyDescriptorExtensionId(3);
constexpr RtpHeaderExtensionId kTransportSequenceNumberExtensionId(4);
constexpr RtpHeaderExtensionId kVideoContentTypeExtensionId(5);
constexpr RtpHeaderExtensionId kVideoTimingExtensionId(6);

constexpr char kSyncGroup[] = "av_sync";
constexpr int kOpusMinBitrateBps = 6000;
constexpr int kOpusBitrateFbBps = 32000;
constexpr int kFramesSentInQuickTest = 1;
constexpr uint32_t kThumbnailSendSsrcStart = 0xE0000;
constexpr uint32_t kThumbnailRtxSsrcStart = 0xF0000;

std::pair<uint32_t, uint32_t> GetMinMaxBitratesBps(const VideoCodec& codec,
                                                   size_t spatial_idx) {
  uint32_t min_bitrate = codec.minBitrate;
  uint32_t max_bitrate = codec.maxBitrate;
  if (spatial_idx < codec.numberOfSimulcastStreams) {
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
  max_bitrate = std::max(max_bitrate, min_bitrate);
  return {min_bitrate * 1000, max_bitrate * 1000};
}

class VideoStreamFactory
    : public VideoEncoderConfig::VideoStreamFactoryInterface {
 public:
  explicit VideoStreamFactory(const std::vector<VideoStream>& streams)
      : streams_(streams) {}

 private:
  std::vector<VideoStream> CreateEncoderStreams(
      const FieldTrialsView& /*field_trials*/,
      int frame_width,
      int frame_height,
      const VideoEncoderConfig& encoder_config) override {
    // The highest layer must match the incoming resolution.
    std::vector<VideoStream> streams = streams_;
    streams[streams_.size() - 1].height = frame_height;
    streams[streams_.size() - 1].width = frame_width;

    streams[0].bitrate_priority = encoder_config.bitrate_priority;
    return streams;
  }

  std::vector<VideoStream> streams_;
};

// This wrapper provides two features needed by the video quality tests:
//  1. Invoke VideoAnalyzer callbacks before and after encoding each frame.
//  2. Write the encoded frames to file, one file per simulcast layer.
class QualityTestVideoEncoder : public VideoEncoder,
                                private EncodedImageCallback {
 public:
  QualityTestVideoEncoder(std::unique_ptr<VideoEncoder> encoder,
                          VideoAnalyzer* analyzer,
                          std::vector<std::string> file_names,
                          double overshoot_factor)
      : encoder_(std::move(encoder)),
        overshoot_factor_(overshoot_factor),
        analyzer_(analyzer) {
    for (const std::string& file_name : file_names) {
      writers_.push_back(
          IvfFileWriter::Wrap(file_name, /* byte_limit= */ 100000000));
    }
  }

  // Implement VideoEncoder
  void SetFecControllerOverride(
      FecControllerOverride* fec_controller_override) override {
    // Ignored.
  }

  int32_t InitEncode(const VideoCodec* codec_settings,
                     const Settings& settings) override {
    codec_settings_ = *codec_settings;
    return encoder_->InitEncode(codec_settings, settings);
  }

  int32_t RegisterEncodeCompleteCallback(
      EncodedImageCallback* callback) override {
    callback_ = callback;
    return encoder_->RegisterEncodeCompleteCallback(this);
  }

  int32_t Release() override { return encoder_->Release(); }

  int32_t Encode(const VideoFrame& frame,
                 const std::vector<VideoFrameType>* frame_types) override {
    if (analyzer_) {
      analyzer_->PreEncodeOnFrame(frame);
    }
    return encoder_->Encode(frame, frame_types);
  }

  void SetRates(const RateControlParameters& parameters) override {
    RTC_DCHECK_GT(overshoot_factor_, 0.0);
    if (overshoot_factor_ == 1.0) {
      encoder_->SetRates(parameters);
      return;
    }

    // Simulating encoder overshooting target bitrate, by configuring actual
    // encoder too high. Take care not to adjust past limits of config,
    // otherwise encoders may crash on DCHECK.
    VideoBitrateAllocation overshot_allocation;
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
      double overshoot_factor = overshoot_factor_;
      const uint32_t corrected_bitrate =
          checked_cast<uint32_t>(overshoot_factor * spatial_layer_bitrate_bps);
      if (corrected_bitrate < min_bitrate_bps) {
        overshoot_factor = min_bitrate_bps / spatial_layer_bitrate_bps;
      } else if (corrected_bitrate > max_bitrate_bps) {
        overshoot_factor = max_bitrate_bps / spatial_layer_bitrate_bps;
      }

      for (size_t ti = 0; ti < kMaxTemporalStreams; ++ti) {
        if (parameters.bitrate.HasBitrate(si, ti)) {
          overshot_allocation.SetBitrate(
              si, ti,
              checked_cast<uint32_t>(overshoot_factor *
                                     parameters.bitrate.GetBitrate(si, ti)));
        }
      }
    }

    return encoder_->SetRates(
        RateControlParameters(overshot_allocation, parameters.framerate_fps,
                              parameters.bandwidth_allocation));
  }

  void OnPacketLossRateUpdate(float packet_loss_rate) override {
    encoder_->OnPacketLossRateUpdate(packet_loss_rate);
  }

  void OnRttUpdate(int64_t rtt_ms) override { encoder_->OnRttUpdate(rtt_ms); }

  EncoderInfo GetEncoderInfo() const override {
    EncoderInfo info = encoder_->GetEncoderInfo();
    if (overshoot_factor_ != 1.0) {
      // We're simulating bad encoder, don't forward trusted setting
      // from eg libvpx.
      info.has_trusted_rate_controller = false;
    }
    return info;
  }

 private:
  // Implement EncodedImageCallback
  Result OnEncodedImage(const EncodedImage& encoded_image,
                        const CodecSpecificInfo* codec_specific_info) override {
    if (codec_specific_info) {
      int simulcast_index = encoded_image.SimulcastIndex().value_or(0);
      RTC_DCHECK_GE(simulcast_index, 0);
      if (analyzer_) {
        analyzer_->PostEncodeOnFrame(simulcast_index,
                                     encoded_image.RtpTimestamp());
      }
      if (static_cast<size_t>(simulcast_index) < writers_.size()) {
        writers_[simulcast_index]->WriteFrame(encoded_image,
                                              codec_specific_info->codecType);
      }
    }

    return callback_->OnEncodedImage(encoded_image, codec_specific_info);
  }

  void OnFrameDropped(uint32_t rtp_timestamp,
                      int spatial_id,
                      bool is_end_of_temporal_unit) override {
    callback_->OnFrameDropped(rtp_timestamp, spatial_id,
                              is_end_of_temporal_unit);
  }

  const std::unique_ptr<VideoEncoder> encoder_;
  const double overshoot_factor_;
  VideoAnalyzer* const analyzer_;
  std::vector<std::unique_ptr<IvfFileWriter>> writers_;
  EncodedImageCallback* callback_ = nullptr;
  VideoCodec codec_settings_;
};

#if defined(WEBRTC_WIN) && !defined(WINUWP)
void PressEnterToContinue(TaskQueueBase* task_queue) {
  puts(">> Press ENTER to continue...");

  while (!_kbhit() || _getch() != '\r') {
    // Drive the message loop for the thread running the task_queue
    SendTask(task_queue, [&]() {
      MSG msg;
      if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
    });
  }
}
#else
void PressEnterToContinue(TaskQueueBase* /*task_queue*/) {
  puts(">> Press ENTER to continue...");
  while (getc(stdin) != '\n' && !feof(stdin))
    ;  // NOLINT
}
#endif

}  // namespace

std::unique_ptr<VideoDecoder> VideoQualityTest::CreateVideoDecoder(
    const Environment& env,
    const SdpVideoFormat& format) {
  std::unique_ptr<VideoDecoder> decoder;
  if (format.name == "FakeCodec") {
    decoder = FakeVideoDecoderFactory::CreateVideoDecoder();
  } else {
    decoder = decoder_factory_->Create(env, format);
  }
  if (!params_.logging.encoded_frame_base_path.empty()) {
    StringBuilder str;
    str << receive_logs_++;
    std::string path =
        params_.logging.encoded_frame_base_path + "." + str.str() + ".recv.ivf";
    decoder = CreateFrameDumpingDecoderWrapper(std::move(decoder), path);
  }
  return decoder;
}

std::unique_ptr<VideoEncoder> VideoQualityTest::CreateVideoEncoder(
    const Environment& env,
    const SdpVideoFormat& format,
    VideoAnalyzer* analyzer) {
  std::unique_ptr<VideoEncoder> encoder;
  if (format.name == "FakeCodec") {
    encoder = FakeVideoEncoderFactory().Create(env, format);
  } else {
    encoder = std::make_unique<SimulcastEncoderAdapter>(
        env, encoder_factory_.get(), nullptr, format);
  }

  std::vector<std::string> encoded_frame_dump_files;
  if (!params_.logging.encoded_frame_base_path.empty()) {
    StringBuilder sb;
    sb << send_logs_++;
    std::string prefix =
        params_.logging.encoded_frame_base_path + "." + sb.str() + ".send.";
    encoded_frame_dump_files.push_back(prefix + "1.ivf");
    encoded_frame_dump_files.push_back(prefix + "2.ivf");
    encoded_frame_dump_files.push_back(prefix + "3.ivf");
  }

  double overshoot_factor = 1.0;
  // Match format to either of the streams in dual-stream mode in order to get
  // the overshoot factor. This is not very robust but we can't know for sure
  // which stream this encoder is meant for, from within the factory.
  if (format ==
      SdpVideoFormat(params_.video[0].codec, params_.video[0].sdp_params)) {
    overshoot_factor = params_.video[0].encoder_overshoot_factor;
  } else if (format == SdpVideoFormat(params_.video[1].codec,
                                      params_.video[1].sdp_params)) {
    overshoot_factor = params_.video[1].encoder_overshoot_factor;
  }
  if (overshoot_factor == 0.0) {
    // If params were zero-initialized, set to 1.0 instead.
    overshoot_factor = 1.0;
  }

  if (analyzer || !encoded_frame_dump_files.empty() || overshoot_factor > 1.0) {
    encoder = std::make_unique<QualityTestVideoEncoder>(
        std::move(encoder), analyzer, std::move(encoded_frame_dump_files),
        overshoot_factor);
  }

  return encoder;
}

VideoQualityTest::VideoQualityTest(InjectionComponents injection_components)
    : CallTest(injection_components.field_trials_ptr
                   ? *injection_components.field_trials_ptr
                   : CreateTestFieldTrials("")),
      video_decoder_factory_(
          [this](const Environment& env, const SdpVideoFormat& format) {
            return this->CreateVideoDecoder(env, format);
          }),
      video_encoder_factory_(
          [this](const Environment& env, const SdpVideoFormat& format) {
            return this->CreateVideoEncoder(env, format, nullptr);
          }),
      video_encoder_factory_with_analyzer_(
          [this](const Environment& env, const SdpVideoFormat& format) {
            return this->CreateVideoEncoder(env, format, analyzer_.get());
          }),
      video_bitrate_allocator_factory_(
          CreateBuiltinVideoBitrateAllocatorFactory()),
      receive_logs_(0),
      send_logs_(0),
      injection_components_(std::move(injection_components)),
      num_video_streams_(0) {
  if (injection_components_.video_decoder_factory != nullptr) {
    decoder_factory_ = std::move(injection_components_.video_decoder_factory);
  } else {
    decoder_factory_ = std::make_unique<InternalDecoderFactory>();
  }
  if (injection_components_.video_encoder_factory != nullptr) {
    encoder_factory_ = std::move(injection_components_.video_encoder_factory);
  } else {
    encoder_factory_ = std::make_unique<InternalEncoderFactory>();
  }

  fec_controller_factory_ =
      std::move(injection_components_.fec_controller_factory);
  network_state_predictor_factory_ =
      std::move(injection_components_.network_state_predictor_factory);
  network_controller_factory_ =
      std::move(injection_components_.network_controller_factory);

  // Register header extensions that are used by transport to identify
  // extensions when parsing incoming packets.
  RegisterRtpExtension(RtpExtension(RtpExtension::kTransportSequenceNumberUri,
                                    kTransportSequenceNumberExtensionId));
  RegisterRtpExtension(
      RtpExtension(RtpExtension::kAbsSendTimeUri, kAbsSendTimeExtensionId));
  RegisterRtpExtension(RtpExtension(RtpExtension::kGenericFrameDescriptorUri00,
                                    kGenericFrameDescriptorExtensionId00));
  RegisterRtpExtension(RtpExtension(RtpExtension::kDependencyDescriptorUri,
                                    kDependencyDescriptorExtensionId));
  RegisterRtpExtension(RtpExtension(RtpExtension::kVideoContentTypeUri,
                                    kVideoContentTypeExtensionId));
  RegisterRtpExtension(
      RtpExtension(RtpExtension::kVideoTimingUri, kVideoTimingExtensionId));
}

void VideoQualityTest::TestBody() {}

std::string VideoQualityTest::GenerateGraphTitle() const {
  StringBuilder ss;
  ss << params_.video[0].codec;
  ss << " (" << params_.video[0].target_bitrate_bps / 1000 << "kbps";
  ss << ", " << params_.video[0].fps << " FPS";
  if (params_.screenshare[0].scroll_duration)
    ss << ", " << params_.screenshare[0].scroll_duration << "s scroll";
  if (params_.ss[0].streams.size() > 1)
    ss << ", Stream #" << params_.ss[0].selected_stream;
  if (params_.ss[0].num_spatial_layers > 1)
    ss << ", Layer #" << params_.ss[0].selected_sl;
  ss << ")";
  return ss.Release();
}

void VideoQualityTest::CheckParamsAndInjectionComponents() {
  if (!params_.config && injection_components_.sender_network == nullptr &&
      injection_components_.receiver_network == nullptr) {
    params_.config = BuiltInNetworkBehaviorConfig();
  }
  RTC_CHECK(
      (params_.config && injection_components_.sender_network == nullptr &&
       injection_components_.receiver_network == nullptr) ||
      (!params_.config && injection_components_.sender_network != nullptr &&
       injection_components_.receiver_network != nullptr));
  for (size_t video_idx = 0; video_idx < num_video_streams_; ++video_idx) {
    // Iterate over primary and secondary video streams.
    if (!params_.video[video_idx].enabled)
      return;
    // Add a default stream in none specified.
    if (params_.ss[video_idx].streams.empty())
      params_.ss[video_idx].streams.push_back(
          VideoQualityTest::DefaultVideoStream(params_, video_idx));
    if (params_.ss[video_idx].num_spatial_layers == 0)
      params_.ss[video_idx].num_spatial_layers = 1;

    if (params_.config) {
      if (params_.config->loss_percent != 0 ||
          params_.config->queue_length_packets != 0) {
        // Since LayerFilteringTransport changes the sequence numbers, we can't
        // use that feature with pack loss, since the NACK request would end up
        // retransmitting the wrong packets.
        RTC_CHECK(params_.ss[video_idx].selected_sl == -1 ||
                  params_.ss[video_idx].selected_sl ==
                      params_.ss[video_idx].num_spatial_layers - 1);
        RTC_CHECK(params_.video[video_idx].selected_tl == -1 ||
                  params_.video[video_idx].selected_tl ==
                      params_.video[video_idx].num_temporal_layers - 1);
      }
    }

    // TODO(ivica): Should max_bitrate_bps == -1 represent inf max bitrate, as
    // it does in some parts of the code?
    RTC_CHECK_GE(params_.video[video_idx].max_bitrate_bps,
                 params_.video[video_idx].target_bitrate_bps);
    RTC_CHECK_GE(params_.video[video_idx].target_bitrate_bps,
                 params_.video[video_idx].min_bitrate_bps);
    int selected_stream = params_.ss[video_idx].selected_stream;
    if (params_.video[video_idx].selected_tl > -1) {
      RTC_CHECK_LT(selected_stream, params_.ss[video_idx].streams.size())
          << "Can not use --selected_tl when --selected_stream is all streams";
      int stream_tl = params_.ss[video_idx]
                          .streams[selected_stream]
                          .num_temporal_layers.value_or(1);
      RTC_CHECK_LT(params_.video[video_idx].selected_tl, stream_tl);
    }
    RTC_CHECK_LE(params_.ss[video_idx].selected_stream,
                 params_.ss[video_idx].streams.size());
    for (const VideoStream& stream : params_.ss[video_idx].streams) {
      RTC_CHECK_GE(stream.min_bitrate_bps, 0);
      RTC_CHECK_GE(stream.target_bitrate_bps, stream.min_bitrate_bps);
      RTC_CHECK_GE(stream.max_bitrate_bps, stream.target_bitrate_bps);
    }
    // TODO(ivica): Should we check if the sum of all streams/layers is equal to
    // the total bitrate? We anyway have to update them in the case bitrate
    // estimator changes the total bitrates.
    RTC_CHECK_GE(params_.ss[video_idx].num_spatial_layers, 1);
    RTC_CHECK_LE(params_.ss[video_idx].selected_sl,
                 params_.ss[video_idx].num_spatial_layers);
    RTC_CHECK(
        params_.ss[video_idx].spatial_layers.empty() ||
        params_.ss[video_idx].spatial_layers.size() ==
            static_cast<size_t>(params_.ss[video_idx].num_spatial_layers));
    if (params_.video[video_idx].codec == "VP8") {
      RTC_CHECK_EQ(params_.ss[video_idx].num_spatial_layers, 1);
    } else if (params_.video[video_idx].codec == "VP9") {
      RTC_CHECK_EQ(params_.ss[video_idx].streams.size(), 1);
    }
    RTC_CHECK_GE(params_.call.num_thumbnails, 0);
    if (params_.call.num_thumbnails > 0) {
      RTC_CHECK_EQ(params_.ss[video_idx].num_spatial_layers, 1);
      RTC_CHECK_EQ(params_.ss[video_idx].streams.size(), 3);
      RTC_CHECK_EQ(params_.video[video_idx].num_temporal_layers, 3);
      RTC_CHECK_EQ(params_.video[video_idx].codec, "VP8");
    }
    // Dual streams with FEC not supported in tests yet.
    RTC_CHECK(!params_.video[video_idx].flexfec || num_video_streams_ == 1);
    RTC_CHECK(!params_.video[video_idx].ulpfec || num_video_streams_ == 1);
  }
}

// Static.
std::vector<int> VideoQualityTest::ParseCSV(const std::string& str) {
  // Parse comma separated nonnegative integers, where some elements may be
  // empty. The empty values are replaced with -1.
  // E.g. "10,-20,,30,40" --> {10, 20, -1, 30,40}
  // E.g. ",,10,,20," --> {-1, -1, 10, -1, 20, -1}
  std::vector<int> result;
  if (str.empty())
    return result;

  const char* p = str.c_str();
  int value = -1;
  int pos;
  while (*p) {
    if (*p == ',') {
      result.push_back(value);
      value = -1;
      ++p;
      continue;
    }
    RTC_CHECK_EQ(sscanf(p, "%d%n", &value, &pos), 1)
        << "Unexpected non-number value.";
    p += pos;
  }
  result.push_back(value);
  return result;
}

// Static.
VideoStream VideoQualityTest::DefaultVideoStream(const Params& params,
                                                 size_t video_idx) {
  VideoStream stream;
  stream.width = params.video[video_idx].width;
  stream.height = params.video[video_idx].height;
  stream.max_framerate = params.video[video_idx].fps;
  stream.min_bitrate_bps = params.video[video_idx].min_bitrate_bps;
  stream.target_bitrate_bps = params.video[video_idx].target_bitrate_bps;
  stream.max_bitrate_bps = params.video[video_idx].max_bitrate_bps;
  stream.max_qp = kDefaultVideoMaxQpVpx;
  stream.num_temporal_layers = params.video[video_idx].num_temporal_layers;
  stream.active = true;
  stream.scalability_mode = params.ss[0].scalability_mode;
  return stream;
}

// Static.
VideoStream VideoQualityTest::DefaultThumbnailStream() {
  VideoStream stream;
  stream.width = 320;
  stream.height = 180;
  stream.max_framerate = 7;
  stream.min_bitrate_bps = 7500;
  stream.target_bitrate_bps = 37500;
  stream.max_bitrate_bps = 50000;
  stream.max_qp = kDefaultVideoMaxQpVpx;
  return stream;
}

void VideoQualityTest::FillScalabilitySettings(
    Params* params,
    size_t video_idx,
    const std::vector<std::string>& stream_descriptors,
    int num_streams,
    size_t selected_stream,
    int num_spatial_layers,
    int selected_sl,
    InterLayerPredMode inter_layer_pred,
    const std::vector<std::string>& sl_descriptors) {
  if (params->ss[video_idx].streams.empty() &&
      params->ss[video_idx].infer_streams) {
    VideoEncoder::EncoderInfo encoder_info;
    VideoEncoderConfig encoder_config;
    encoder_config.codec_type =
        PayloadStringToCodecType(params->video[video_idx].codec);
    encoder_config.content_type =
        params->screenshare[video_idx].enabled
            ? VideoEncoderConfig::ContentType::kScreen
            : VideoEncoderConfig::ContentType::kRealtimeVideo;
    encoder_config.max_bitrate_bps = params->video[video_idx].max_bitrate_bps;
    encoder_config.min_transmit_bitrate_bps =
        params->video[video_idx].min_transmit_bps;
    encoder_config.number_of_streams = num_streams;
    encoder_config.spatial_layers = params->ss[video_idx].spatial_layers;
    encoder_config.simulcast_layers = std::vector<VideoStream>(num_streams);
    encoder_config.video_stream_factory =
        make_ref_counted<EncoderStreamFactory>(encoder_info);
    params->ss[video_idx].streams =
        encoder_config.video_stream_factory->CreateEncoderStreams(
            env().field_trials(), params->video[video_idx].width,
            params->video[video_idx].height, encoder_config);
  } else {
    // Read VideoStream and SpatialLayer elements from a list of comma separated
    // lists. To use a default value for an element, use -1 or leave empty.
    // Validity checks performed in CheckParamsAndInjectionComponents.
    RTC_CHECK(params->ss[video_idx].streams.empty());
    for (const auto& descriptor : stream_descriptors) {
      if (descriptor.empty())
        continue;
      VideoStream stream =
          VideoQualityTest::DefaultVideoStream(*params, video_idx);
      std::vector<int> v = VideoQualityTest::ParseCSV(descriptor);
      if (v[0] != -1)
        stream.width = static_cast<size_t>(v[0]);
      if (v[1] != -1)
        stream.height = static_cast<size_t>(v[1]);
      if (v[2] != -1)
        stream.max_framerate = v[2];
      if (v[3] != -1)
        stream.min_bitrate_bps = v[3];
      if (v[4] != -1)
        stream.target_bitrate_bps = v[4];
      if (v[5] != -1)
        stream.max_bitrate_bps = v[5];
      if (v.size() > 6 && v[6] != -1)
        stream.max_qp = v[6];
      if (v.size() > 7 && v[7] != -1) {
        stream.num_temporal_layers = v[7];
      } else {
        // Automatic TL thresholds for more than two layers not supported.
        RTC_CHECK_LE(params->video[video_idx].num_temporal_layers, 2);
      }
      params->ss[video_idx].streams.push_back(stream);
    }
  }

  params->ss[video_idx].num_spatial_layers = std::max(1, num_spatial_layers);
  params->ss[video_idx].selected_stream = selected_stream;

  params->ss[video_idx].selected_sl = selected_sl;
  params->ss[video_idx].inter_layer_pred = inter_layer_pred;
  RTC_CHECK(params->ss[video_idx].spatial_layers.empty());
  for (const auto& descriptor : sl_descriptors) {
    if (descriptor.empty())
      continue;
    std::vector<int> v = VideoQualityTest::ParseCSV(descriptor);
    RTC_CHECK_EQ(v.size(), 8);

    SpatialLayer layer = {.width = 0};
    layer.width = v[0];
    layer.height = v[1];
    layer.maxFramerate = v[2];
    layer.numberOfTemporalLayers = v[3];
    layer.maxBitrate = v[4];
    layer.minBitrate = v[5];
    layer.targetBitrate = v[6];
    layer.qpMax = v[7];
    layer.active = true;

    params->ss[video_idx].spatial_layers.push_back(layer);
  }
}

void VideoQualityTest::SetupVideo(Transport* send_transport,
                                  Transport* recv_transport) {
  size_t total_streams_used = 0;
  video_receive_configs_.clear();
  video_send_configs_.clear();
  video_encoder_configs_.clear();
  bool decode_all_receive_streams = true;
  size_t num_video_substreams = params_.ss[0].streams.size();
  RTC_CHECK(num_video_streams_ > 0);
  video_encoder_configs_.resize(num_video_streams_);
  std::string generic_codec_name;
  VideoEncoder::EncoderInfo encoder_info;
  for (size_t video_idx = 0; video_idx < num_video_streams_; ++video_idx) {
    VideoSendStream::Config config(send_transport);
    config.rtp.extmap_allow_mixed = true;
    video_send_configs_.push_back(std::move(config));
    video_encoder_configs_.push_back(VideoEncoderConfig());
    num_video_substreams = params_.ss[video_idx].streams.size();
    RTC_CHECK_GT(num_video_substreams, 0);
    for (size_t i = 0; i < num_video_substreams; ++i)
      video_send_configs_[video_idx].rtp.ssrcs.push_back(
          test::VideoTestConstants::kVideoSendSsrcs[total_streams_used + i]);

    int payload_type;
    if (params_.video[video_idx].codec == "H264") {
      payload_type = test::VideoTestConstants::kPayloadTypeH264;
    } else if (params_.video[video_idx].codec == "VP8") {
      payload_type = test::VideoTestConstants::kPayloadTypeVP8;
    } else if (params_.video[video_idx].codec == "VP9") {
      payload_type = test::VideoTestConstants::kPayloadTypeVP9;
    } else if (params_.video[video_idx].codec == "FakeCodec") {
      payload_type = test::VideoTestConstants::kFakeVideoSendPayloadType;
    } else {
      RTC_CHECK(generic_codec_name.empty() ||
                generic_codec_name == params_.video[video_idx].codec)
          << "Supplying multiple generic codecs is unsupported.";
      RTC_LOG(LS_INFO) << "Treating codec " << params_.video[video_idx].codec
                       << " as generic.";
      payload_type = test::VideoTestConstants::kPayloadTypeGeneric;
      generic_codec_name = params_.video[video_idx].codec;
    }
    video_send_configs_[video_idx].encoder_settings.encoder_factory =
        (video_idx == 0) ? &video_encoder_factory_with_analyzer_
                         : &video_encoder_factory_;
    video_send_configs_[video_idx].encoder_settings.bitrate_allocator_factory =
        video_bitrate_allocator_factory_.get();

    video_send_configs_[video_idx].rtp.payload_name =
        params_.video[video_idx].codec;
    video_send_configs_[video_idx].rtp.payload_type = payload_type;
    video_send_configs_[video_idx].rtp.nack.rtp_history_ms =
        test::VideoTestConstants::kNackRtpHistoryMs;
    video_send_configs_[video_idx].rtp.rtx.payload_type =
        test::VideoTestConstants::kSendRtxPayloadType;
    for (size_t i = 0; i < num_video_substreams; ++i) {
      video_send_configs_[video_idx].rtp.rtx.ssrcs.push_back(
          test::VideoTestConstants::kSendRtxSsrcs[i + total_streams_used]);
    }
    video_send_configs_[video_idx].rtp.extensions.clear();
    if (params_.call.send_side_bwe) {
      video_send_configs_[video_idx].rtp.extensions.emplace_back(
          RtpExtension::kTransportSequenceNumberUri,
          kTransportSequenceNumberExtensionId);
    } else {
      video_send_configs_[video_idx].rtp.extensions.emplace_back(
          RtpExtension::kAbsSendTimeUri, kAbsSendTimeExtensionId);
    }

    if (params_.call.generic_descriptor) {
      video_send_configs_[video_idx].rtp.extensions.emplace_back(
          RtpExtension::kGenericFrameDescriptorUri00,
          kGenericFrameDescriptorExtensionId00);
    }

    if (params_.call.dependency_descriptor) {
      video_send_configs_[video_idx].rtp.extensions.emplace_back(
          RtpExtension::kDependencyDescriptorUri,
          kDependencyDescriptorExtensionId);
    }

    video_send_configs_[video_idx].rtp.extensions.emplace_back(
        RtpExtension::kVideoContentTypeUri, kVideoContentTypeExtensionId);
    video_send_configs_[video_idx].rtp.extensions.emplace_back(
        RtpExtension::kVideoTimingUri, kVideoTimingExtensionId);

    video_encoder_configs_[video_idx].video_format.name =
        params_.video[video_idx].codec;

    video_encoder_configs_[video_idx].video_format.parameters =
        params_.video[video_idx].sdp_params;

    video_encoder_configs_[video_idx].codec_type =
        PayloadStringToCodecType(params_.video[video_idx].codec);

    video_encoder_configs_[video_idx].min_transmit_bitrate_bps =
        params_.video[video_idx].min_transmit_bps;

    video_send_configs_[video_idx].suspend_below_min_bitrate =
        params_.video[video_idx].suspend_below_min_bitrate;

    video_encoder_configs_[video_idx].number_of_streams =
        params_.ss[video_idx].streams.size();
    video_encoder_configs_[video_idx].max_bitrate_bps = 0;
    for (size_t i = 0; i < params_.ss[video_idx].streams.size(); ++i) {
      video_encoder_configs_[video_idx].max_bitrate_bps +=
          params_.ss[video_idx].streams[i].max_bitrate_bps;
    }
    video_encoder_configs_[video_idx].simulcast_layers =
        std::vector<VideoStream>(params_.ss[video_idx].streams.size());
    if (!params_.ss[video_idx].infer_streams) {
      video_encoder_configs_[video_idx].simulcast_layers =
          params_.ss[video_idx].streams;
    }

    video_encoder_configs_[video_idx].spatial_layers =
        params_.ss[video_idx].spatial_layers;

    video_encoder_configs_[video_idx].frame_drop_enabled = true;

    decode_all_receive_streams = params_.ss[video_idx].selected_stream ==
                                 params_.ss[video_idx].streams.size();
    std::optional<int> decode_sub_stream;
    if (!decode_all_receive_streams)
      decode_sub_stream = params_.ss[video_idx].selected_stream;
    CreateMatchingVideoReceiveConfigs(
        video_send_configs_[video_idx], recv_transport, &video_decoder_factory_,
        decode_sub_stream, true, test::VideoTestConstants::kNackRtpHistoryMs);

    if (params_.screenshare[video_idx].enabled) {
      // Fill out codec settings.
      video_encoder_configs_[video_idx].content_type =
          VideoEncoderConfig::ContentType::kScreen;
      video_encoder_configs_[video_idx].legacy_conference_mode = true;
      degradation_preference_ = DegradationPreference::MAINTAIN_RESOLUTION;
      if (params_.video[video_idx].codec == "VP8") {
        VideoCodecVP8 vp8_settings = VideoEncoder::GetDefaultVp8Settings();
        vp8_settings.denoisingOn = false;
        vp8_settings.numberOfTemporalLayers = static_cast<unsigned char>(
            params_.video[video_idx].num_temporal_layers);
        video_encoder_configs_[video_idx].encoder_specific_settings =
            make_ref_counted<VideoEncoderConfig::Vp8EncoderSpecificSettings>(
                vp8_settings);
      } else if (params_.video[video_idx].codec == "VP9") {
        VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
        vp9_settings.denoisingOn = false;
        vp9_settings.automaticResizeOn = false;
        vp9_settings.numberOfTemporalLayers = static_cast<unsigned char>(
            params_.video[video_idx].num_temporal_layers);
        vp9_settings.numberOfSpatialLayers = static_cast<unsigned char>(
            params_.ss[video_idx].num_spatial_layers);
        vp9_settings.interLayerPred = params_.ss[video_idx].inter_layer_pred;
        // High FPS vp9 screenshare requires flexible mode.
        if (params_.ss[video_idx].num_spatial_layers > 1) {
          vp9_settings.flexibleMode = true;
        }
        video_encoder_configs_[video_idx].encoder_specific_settings =
            make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
                vp9_settings);
      }
    } else if (params_.ss[video_idx].num_spatial_layers > 1) {
      // If SVC mode without screenshare, still need to set codec specifics.
      RTC_CHECK(params_.video[video_idx].codec == "VP9");
      VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
      vp9_settings.numberOfTemporalLayers = static_cast<unsigned char>(
          params_.video[video_idx].num_temporal_layers);
      vp9_settings.numberOfSpatialLayers =
          static_cast<unsigned char>(params_.ss[video_idx].num_spatial_layers);
      vp9_settings.interLayerPred = params_.ss[video_idx].inter_layer_pred;
      vp9_settings.automaticResizeOn = false;
      video_encoder_configs_[video_idx].encoder_specific_settings =
          make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
              vp9_settings);
      RTC_DCHECK_EQ(video_encoder_configs_[video_idx].simulcast_layers.size(),
                    1);
      // Min bitrate will be enforced by spatial layer config instead.
      video_encoder_configs_[video_idx].simulcast_layers[0].min_bitrate_bps = 0;
    } else if (params_.video[video_idx].automatic_scaling) {
      if (params_.video[video_idx].codec == "VP8") {
        VideoCodecVP8 vp8_settings = VideoEncoder::GetDefaultVp8Settings();
        vp8_settings.automaticResizeOn = true;
        video_encoder_configs_[video_idx].encoder_specific_settings =
            make_ref_counted<VideoEncoderConfig::Vp8EncoderSpecificSettings>(
                vp8_settings);
      } else if (params_.video[video_idx].codec == "VP9") {
        VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
        // Only enable quality scaler for single spatial layer.
        vp9_settings.automaticResizeOn =
            params_.ss[video_idx].num_spatial_layers == 1;
        video_encoder_configs_[video_idx].encoder_specific_settings =
            make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
                vp9_settings);
      } else if (params_.video[video_idx].codec == "H264") {
        // Quality scaling is always on for H.264.
      } else if (params_.video[video_idx].codec == kAv1CodecName) {
        // TODO(bugs.webrtc.org/11404): Propagate the flag to
        // aom_codec_enc_cfg_t::rc_resize_mode in Av1 encoder wrapper.
        // Until then do nothing, specially do not crash.
      } else {
        RTC_DCHECK_NOTREACHED()
            << "Automatic scaling not supported for codec "
            << params_.video[video_idx].codec << ", stream " << video_idx;
      }
    } else {
      // Default mode. Single SL, no automatic_scaling,
      if (params_.video[video_idx].codec == "VP8") {
        VideoCodecVP8 vp8_settings = VideoEncoder::GetDefaultVp8Settings();
        vp8_settings.automaticResizeOn = false;
        video_encoder_configs_[video_idx].encoder_specific_settings =
            make_ref_counted<VideoEncoderConfig::Vp8EncoderSpecificSettings>(
                vp8_settings);
      } else if (params_.video[video_idx].codec == "VP9") {
        VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
        vp9_settings.automaticResizeOn = false;
        video_encoder_configs_[video_idx].encoder_specific_settings =
            make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
                vp9_settings);
      } else if (params_.video[video_idx].codec == "H264") {
        video_encoder_configs_[video_idx].encoder_specific_settings = nullptr;
      }
    }
    total_streams_used += num_video_substreams;
  }

  // FEC supported only for single video stream mode yet.
  if (params_.video[0].flexfec) {
    if (decode_all_receive_streams) {
      SetSendFecConfig(GetVideoSendConfig()->rtp.ssrcs);
    } else {
      SetSendFecConfig(
          {test::VideoTestConstants::kVideoSendSsrcs[params_.ss[0]
                                                         .selected_stream]});
    }

    CreateMatchingFecConfig(recv_transport, *GetVideoSendConfig());
  }

  if (params_.video[0].ulpfec) {
    SetSendUlpFecConfig(GetVideoSendConfig());
    if (decode_all_receive_streams) {
      for (auto& receive_config : video_receive_configs_) {
        SetReceiveUlpFecConfig(&receive_config);
      }
    } else {
      SetReceiveUlpFecConfig(
          &video_receive_configs_[params_.ss[0].selected_stream]);
    }
  }
}

void VideoQualityTest::SetupThumbnails(Transport* send_transport,
                                       Transport* recv_transport) {
  for (int i = 0; i < params_.call.num_thumbnails; ++i) {
    // Thumbnails will be send in the other way: from receiver_call to
    // sender_call.
    VideoSendStream::Config thumbnail_send_config(recv_transport);
    thumbnail_send_config.rtp.ssrcs.push_back(kThumbnailSendSsrcStart + i);
    thumbnail_send_config.encoder_settings.encoder_factory =
        &video_encoder_factory_;
    thumbnail_send_config.encoder_settings.bitrate_allocator_factory =
        video_bitrate_allocator_factory_.get();
    thumbnail_send_config.rtp.payload_name = params_.video[0].codec;
    thumbnail_send_config.rtp.payload_type =
        test::VideoTestConstants::kPayloadTypeVP8;
    thumbnail_send_config.rtp.nack.rtp_history_ms =
        test::VideoTestConstants::kNackRtpHistoryMs;
    thumbnail_send_config.rtp.rtx.payload_type =
        test::VideoTestConstants::kSendRtxPayloadType;
    thumbnail_send_config.rtp.rtx.ssrcs.push_back(kThumbnailRtxSsrcStart + i);
    thumbnail_send_config.rtp.extensions.clear();
    if (params_.call.send_side_bwe) {
      thumbnail_send_config.rtp.extensions.push_back(
          RtpExtension(RtpExtension::kTransportSequenceNumberUri,
                       kTransportSequenceNumberExtensionId));
    } else {
      thumbnail_send_config.rtp.extensions.push_back(
          RtpExtension(RtpExtension::kAbsSendTimeUri, kAbsSendTimeExtensionId));
    }

    VideoEncoderConfig thumbnail_encoder_config;
    thumbnail_encoder_config.codec_type = kVideoCodecVP8;
    thumbnail_encoder_config.video_format.name = "VP8";
    thumbnail_encoder_config.min_transmit_bitrate_bps = 7500;
    thumbnail_send_config.suspend_below_min_bitrate =
        params_.video[0].suspend_below_min_bitrate;
    thumbnail_encoder_config.number_of_streams = 1;
    thumbnail_encoder_config.max_bitrate_bps = 50000;
    std::vector<VideoStream> streams{params_.ss[0].streams[0]};
    thumbnail_encoder_config.video_stream_factory =
        make_ref_counted<VideoStreamFactory>(streams);
    thumbnail_encoder_config.spatial_layers = params_.ss[0].spatial_layers;

    thumbnail_encoder_configs_.push_back(thumbnail_encoder_config.Copy());
    thumbnail_send_configs_.push_back(thumbnail_send_config.Copy());

    AddMatchingVideoReceiveConfigs(&thumbnail_receive_configs_,
                                   thumbnail_send_config, send_transport,
                                   &video_decoder_factory_, std::nullopt, false,
                                   test::VideoTestConstants::kNackRtpHistoryMs);
  }
  for (size_t i = 0; i < thumbnail_send_configs_.size(); ++i) {
    thumbnail_send_streams_.push_back(receiver_call_->CreateVideoSendStream(
        thumbnail_send_configs_[i].Copy(),
        thumbnail_encoder_configs_[i].Copy()));
  }
  for (size_t i = 0; i < thumbnail_receive_configs_.size(); ++i) {
    thumbnail_receive_streams_.push_back(sender_call_->CreateVideoReceiveStream(
        std::move(thumbnail_receive_configs_[i])));
  }
  thumbnail_receive_configs_.clear();
}

void VideoQualityTest::DestroyThumbnailStreams() {
  for (VideoSendStream* thumbnail_send_stream : thumbnail_send_streams_) {
    receiver_call_->DestroyVideoSendStream(thumbnail_send_stream);
  }
  thumbnail_send_streams_.clear();
  for (VideoReceiveStreamInterface* thumbnail_receive_stream :
       thumbnail_receive_streams_) {
    sender_call_->DestroyVideoReceiveStream(thumbnail_receive_stream);
  }
  thumbnail_send_streams_.clear();
  thumbnail_receive_streams_.clear();
  for (std::unique_ptr<VideoSourceInterface<VideoFrame>>& video_capturer :
       thumbnail_capturers_) {
    video_capturer.reset();
  }
}

void VideoQualityTest::SetupThumbnailCapturers(size_t num_thumbnail_streams) {
  VideoStream thumbnail = DefaultThumbnailStream();
  for (size_t i = 0; i < num_thumbnail_streams; ++i) {
    auto frame_generator_capturer =
        std::make_unique<test::FrameGeneratorCapturer>(
            &env_.clock(),
            test::CreateSquareFrameGenerator(static_cast<int>(thumbnail.width),
                                             static_cast<int>(thumbnail.height),
                                             std::nullopt, std::nullopt),
            thumbnail.max_framerate, env_.task_queue_factory());
    EXPECT_TRUE(frame_generator_capturer->Init());
    thumbnail_capturers_.push_back(std::move(frame_generator_capturer));
  }
}

std::unique_ptr<test::FrameGeneratorInterface>
VideoQualityTest::CreateFrameGenerator(size_t video_idx) {
  // Setup frame generator.
  const size_t kWidth = 1850;
  const size_t kHeight = 1110;
  std::unique_ptr<test::FrameGeneratorInterface> frame_generator;
  if (params_.video[video_idx].pendulum.has_value()) {
    const auto& pendulum = *params_.video[video_idx].pendulum;
    test::PendulumFrameGenerator::Config config;
    config.target_resolution = {.width = params_.video[video_idx].width,
                                .height = params_.video[video_idx].height};
    config.fps = params_.video[video_idx].fps;
    config.source_image_path = pendulum.image_path;
    config.source_resolution = {
        .width = static_cast<size_t>(pendulum.image_width),
        .height = static_cast<size_t>(pendulum.image_height)};
    config.min_zoom = pendulum.min_zoom;
    config.max_zoom = pendulum.max_zoom;
    config.zoom_speed = pendulum.zoom_speed;
    config.noise_level = pendulum.noise_level;
    return test::CreatePendulumFrameGenerator(config);
  } else if (params_.screenshare[video_idx].generate_slides) {
    frame_generator = test::CreateSlideFrameGenerator(
        kWidth, kHeight,
        params_.screenshare[video_idx].slide_change_interval *
            params_.video[video_idx].fps);
  } else if (!params_.video[video_idx].clip_paths.empty() ||
             params_.video[video_idx].camera_switching_interval >
                 TimeDelta::Zero() ||
             (!params_.video[video_idx].clip_path.empty() &&
              params_.video[video_idx].clip_path.find(',') !=
                  std::string::npos)) {
    std::vector<std::string> paths = params_.video[video_idx].clip_paths;
    if (paths.empty() && !params_.video[video_idx].clip_path.empty()) {
      tokenize(params_.video[video_idx].clip_path, ',', &paths);
    }
    if (!paths.empty()) {
      TimeDelta interval =
          params_.video[video_idx].camera_switching_interval > TimeDelta::Zero()
              ? params_.video[video_idx].camera_switching_interval
              : TimeDelta::Seconds(2);
      Resolution res = {
          .width = static_cast<int>(params_.video[video_idx].width),
          .height = static_cast<int>(params_.video[video_idx].height)};
      frame_generator = test::CreateSwitchingFrameGenerator(
          paths, res, params_.video[video_idx].fps, interval);
    }
  } else if (!params_.video[video_idx].clip_path.empty()) {
    RTC_CHECK_GE(params_.video[video_idx].clip_path.size(), 4)
        << "Clip path must have a three letter file ending.";
    std::string file_ending = params_.video[video_idx].clip_path.substr(
        params_.video[video_idx].clip_path.size() - 4);
    if (file_ending == ".yuv") {
      frame_generator = test::CreateFromYuvFileFrameGenerator(
          {params_.video[video_idx].clip_path}, params_.video[video_idx].width,
          params_.video[video_idx].height, 1);
    } else if (file_ending == ".y4m") {
      frame_generator = std::make_unique<test::Y4mFrameGenerator>(
          params_.video[video_idx].clip_path,
          test::Y4mFrameGenerator::RepeatMode::kPingPong);
    } else {
      RTC_DCHECK_NOTREACHED() << "Unsupported file format: " << file_ending;
    }
  } else {
    std::vector<std::string> slides = params_.screenshare[video_idx].slides;
    if (slides.empty()) {
      slides.push_back(test::ResourcePath("web_screenshot_1850_1110", "yuv"));
      slides.push_back(test::ResourcePath("presentation_1850_1110", "yuv"));
      slides.push_back(test::ResourcePath("photo_1850_1110", "yuv"));
      slides.push_back(test::ResourcePath("difficult_photo_1850_1110", "yuv"));
    }
    if (params_.screenshare[video_idx].scroll_duration == 0) {
      // Cycle image every slide_change_interval seconds.
      frame_generator = test::CreateFromYuvFileFrameGenerator(
          slides, kWidth, kHeight,
          params_.screenshare[video_idx].slide_change_interval *
              params_.video[video_idx].fps);
    } else {
      RTC_CHECK_LE(params_.video[video_idx].width, kWidth);
      RTC_CHECK_LE(params_.video[video_idx].height, kHeight);
      RTC_CHECK_GT(params_.screenshare[video_idx].slide_change_interval, 0);
      const int kPauseDurationMs =
          (params_.screenshare[video_idx].slide_change_interval -
           params_.screenshare[video_idx].scroll_duration) *
          1000;
      RTC_CHECK_LE(params_.screenshare[video_idx].scroll_duration,
                   params_.screenshare[video_idx].slide_change_interval);

      frame_generator = test::CreateScrollingInputFromYuvFilesFrameGenerator(
          &env_.clock(), slides, kWidth, kHeight,
          params_.video[video_idx].width, params_.video[video_idx].height,
          params_.screenshare[video_idx].scroll_duration * 1000,
          kPauseDurationMs);
    }
  }
  return frame_generator;
}

void VideoQualityTest::CreateCapturers() {
  RTC_DCHECK(video_sources_.empty());
  video_sources_.resize(num_video_streams_);
  for (size_t video_idx = 0; video_idx < num_video_streams_; ++video_idx) {
    std::unique_ptr<test::FrameGeneratorInterface> frame_generator;
    if (params_.screenshare[video_idx].enabled) {
      frame_generator = CreateFrameGenerator(video_idx);
    } else if (params_.video[video_idx].clip_path == "Generator") {
      frame_generator = test::CreateSquareFrameGenerator(
          static_cast<int>(params_.video[video_idx].width),
          static_cast<int>(params_.video[video_idx].height), std::nullopt,
          std::nullopt);
    } else if (params_.video[video_idx].clip_path == "GeneratorI420A") {
      frame_generator = test::CreateSquareFrameGenerator(
          static_cast<int>(params_.video[video_idx].width),
          static_cast<int>(params_.video[video_idx].height),
          test::FrameGeneratorInterface::OutputType::kI420A, std::nullopt);
    } else if (params_.video[video_idx].clip_path == "GeneratorI010") {
      frame_generator = test::CreateSquareFrameGenerator(
          static_cast<int>(params_.video[video_idx].width),
          static_cast<int>(params_.video[video_idx].height),
          test::FrameGeneratorInterface::OutputType::kI010, std::nullopt);
    } else if (params_.video[video_idx].clip_path == "GeneratorNV12") {
      frame_generator = test::CreateSquareFrameGenerator(
          static_cast<int>(params_.video[video_idx].width),
          static_cast<int>(params_.video[video_idx].height),
          test::FrameGeneratorInterface::OutputType::kNV12, std::nullopt);
    } else if (params_.video[video_idx].pendulum.has_value()) {
      const auto& pendulum = *params_.video[video_idx].pendulum;
      test::PendulumFrameGenerator::Config config;
      config.target_resolution = {.width = params_.video[video_idx].width,
                                  .height = params_.video[video_idx].height};
      config.fps = params_.video[video_idx].fps;
      config.source_image_path = pendulum.image_path;
      config.source_resolution = {
          .width = static_cast<size_t>(pendulum.image_width),
          .height = static_cast<size_t>(pendulum.image_height)};
      config.min_zoom = pendulum.min_zoom;
      config.max_zoom = pendulum.max_zoom;
      config.zoom_speed = pendulum.zoom_speed;
      config.noise_level = pendulum.noise_level;
      frame_generator = test::CreatePendulumFrameGenerator(config);
    } else if (!params_.video[video_idx].clip_paths.empty() ||
               params_.video[video_idx].camera_switching_interval >
                   TimeDelta::Zero() ||
               (!params_.video[video_idx].clip_path.empty() &&
                params_.video[video_idx].clip_path.find(',') !=
                    std::string::npos)) {
      std::vector<std::string> paths = params_.video[video_idx].clip_paths;
      if (paths.empty() && !params_.video[video_idx].clip_path.empty()) {
        tokenize(params_.video[video_idx].clip_path, ',', &paths);
      }
      if (!paths.empty()) {
        TimeDelta interval =
            params_.video[video_idx].camera_switching_interval >
                    TimeDelta::Zero()
                ? params_.video[video_idx].camera_switching_interval
                : TimeDelta::Seconds(2);
        Resolution res = {
            .width = static_cast<int>(params_.video[video_idx].width),
            .height = static_cast<int>(params_.video[video_idx].height)};
        frame_generator = test::CreateSwitchingFrameGenerator(
            paths, res, params_.video[video_idx].fps, interval);
      }
    } else if (params_.video[video_idx].clip_path.empty()) {
      video_sources_[video_idx] = test::CreateVideoCapturer(
          params_.video[video_idx].width, params_.video[video_idx].height,
          params_.video[video_idx].fps,
          params_.video[video_idx].capture_device_index);
      if (video_sources_[video_idx]) {
        continue;
      } else {
        // Failed to get actual camera, use chroma generator as backup.
        frame_generator = test::CreateSquareFrameGenerator(
            static_cast<int>(params_.video[video_idx].width),
            static_cast<int>(params_.video[video_idx].height), std::nullopt,
            std::nullopt);
      }
    } else {
      RTC_CHECK_GE(params_.video[video_idx].clip_path.size(), 4)
          << "Clip path must have a three letter file ending.";
      std::string file_ending = params_.video[video_idx].clip_path.substr(
          params_.video[video_idx].clip_path.size() - 4);
      if (file_ending == ".yuv") {
        frame_generator = test::CreateFromYuvFileFrameGenerator(
            {params_.video[video_idx].clip_path},
            params_.video[video_idx].width, params_.video[video_idx].height, 1);
      } else if (file_ending == ".y4m") {
        frame_generator = std::make_unique<test::Y4mFrameGenerator>(
            params_.video[video_idx].clip_path,
            test::Y4mFrameGenerator::RepeatMode::kPingPong);
      } else {
        RTC_DCHECK_NOTREACHED() << "Unsupported file format: " << file_ending;
      }
    }
    ASSERT_TRUE(frame_generator);
    auto frame_generator_capturer =
        std::make_unique<test::FrameGeneratorCapturer>(
            &env_.clock(), std::move(frame_generator),
            params_.video[video_idx].fps, env_.task_queue_factory());
    EXPECT_TRUE(frame_generator_capturer->Init());
    video_sources_[video_idx] = std::move(frame_generator_capturer);
  }
}

void VideoQualityTest::StartAudioStreams() {
  audio_send_stream_->Start();
  for (AudioReceiveStreamInterface* audio_recv_stream : audio_receive_streams_)
    audio_recv_stream->Start();
}

void VideoQualityTest::StartThumbnails() {
  for (VideoSendStream* send_stream : thumbnail_send_streams_)
    send_stream->Start();
  for (VideoReceiveStreamInterface* receive_stream : thumbnail_receive_streams_)
    receive_stream->Start();
}

void VideoQualityTest::StopThumbnails() {
  for (VideoReceiveStreamInterface* receive_stream : thumbnail_receive_streams_)
    receive_stream->Stop();
  for (VideoSendStream* send_stream : thumbnail_send_streams_)
    send_stream->Stop();
}

std::unique_ptr<test::LayerFilteringTransport>
VideoQualityTest::CreateSendTransport() {
  std::unique_ptr<NetworkBehaviorInterface> network_behavior = nullptr;
  if (injection_components_.sender_network == nullptr) {
    network_behavior = std::make_unique<SimulatedNetwork>(*params_.config);
  } else {
    network_behavior = std::move(injection_components_.sender_network);
  }
  return std::make_unique<test::LayerFilteringTransport>(
      env_, network_thread(),
      std::make_unique<FakeNetworkPipe>(&env_.clock(),
                                        std::move(network_behavior)),
      sender_call_.get(), test::VideoTestConstants::kPayloadTypeVP8,
      test::VideoTestConstants::kPayloadTypeVP9, params_.video[0].selected_tl,
      params_.ss[0].selected_sl, payload_type_map_,
      test::VideoTestConstants::kVideoSendSsrcs[0],
      static_cast<uint32_t>(test::VideoTestConstants::kVideoSendSsrcs[0] +
                            params_.ss[0].streams.size() - 1),
      GetRegisteredExtensions(), GetRegisteredExtensions());
}

std::unique_ptr<test::DirectTransport>
VideoQualityTest::CreateReceiveTransport() {
  std::unique_ptr<NetworkBehaviorInterface> network_behavior = nullptr;
  if (injection_components_.receiver_network == nullptr) {
    network_behavior = std::make_unique<SimulatedNetwork>(*params_.config);
  } else {
    network_behavior = std::move(injection_components_.receiver_network);
  }
  return std::make_unique<test::DirectTransport>(
      env_, network_thread(),
      std::make_unique<FakeNetworkPipe>(&env_.clock(),
                                        std::move(network_behavior)),
      receiver_call_.get(), payload_type_map_, GetRegisteredExtensions(),
      GetRegisteredExtensions());
}

void VideoQualityTest::RunWithAnalyzer(const Params& params) {
  num_video_streams_ = params.call.dual_video ? 2 : 1;
  std::unique_ptr<test::LayerFilteringTransport> send_transport;
  std::unique_ptr<test::DirectTransport> recv_transport;
  FILE* graph_data_output_file = nullptr;

  params_ = params;
  // TODO(ivica): Merge with RunWithRenderer and use a flag / argument to
  // differentiate between the analyzer and the renderer case.
  CheckParamsAndInjectionComponents();

  if (!params_.analyzer.graph_data_output_filename.empty()) {
    graph_data_output_file =
        fopen(params_.analyzer.graph_data_output_filename.c_str(), "w");
    RTC_CHECK(graph_data_output_file)
        << "Can't open the file " << params_.analyzer.graph_data_output_filename
        << "!";
  }

  if (!params.logging.rtc_event_log_name.empty()) {
    std::unique_ptr<RtcEventLog> send_event_log =
        rtc_event_log_factory_.Create(env());
    std::unique_ptr<RtcEventLog> recv_event_log =
        rtc_event_log_factory_.Create(env());
    std::unique_ptr<RtcEventLogOutputFile> send_output(
        std::make_unique<RtcEventLogOutputFile>(
            params.logging.rtc_event_log_name + "_send",
            RtcEventLog::kUnlimitedOutput));
    std::unique_ptr<RtcEventLogOutputFile> recv_output(
        std::make_unique<RtcEventLogOutputFile>(
            params.logging.rtc_event_log_name + "_recv",
            RtcEventLog::kUnlimitedOutput));
    bool event_log_started =
        send_event_log->StartLogging(std::move(send_output),
                                     RtcEventLog::kImmediateOutput) &&
        recv_event_log->StartLogging(std::move(recv_output),
                                     RtcEventLog::kImmediateOutput);
    RTC_DCHECK(event_log_started);
    SetSendEventLog(std::move(send_event_log));
    SetRecvEventLog(std::move(recv_event_log));
  }

  SendTask(task_queue(), [this, &params, &send_transport, &recv_transport]() {
    CallConfig send_call_config = SendCallConfig();
    CallConfig recv_call_config = RecvCallConfig();
    send_call_config.bitrate_config = params.call.call_bitrate_config;
    recv_call_config.bitrate_config = params.call.call_bitrate_config;
    if (params_.audio.enabled)
      InitializeAudioDevice(&send_call_config, &recv_call_config,
                            params_.audio.use_real_adm);

    CreateCalls(std::move(send_call_config), std::move(recv_call_config));
    send_transport = CreateSendTransport();
    recv_transport = CreateReceiveTransport();
  });

  std::string graph_title = params_.analyzer.graph_title;
  if (graph_title.empty())
    graph_title = VideoQualityTest::GenerateGraphTitle();
  bool is_quick_test_enabled = absl::GetFlag(FLAGS_webrtc_quick_perf_test);
  analyzer_ = std::make_unique<VideoAnalyzer>(
      send_transport.get(), params_.analyzer.test_label,
      params_.analyzer.avg_psnr_threshold, params_.analyzer.avg_ssim_threshold,
      is_quick_test_enabled
          ? kFramesSentInQuickTest
          : params_.analyzer.test_durations_secs * params_.video[0].fps,
      is_quick_test_enabled
          ? TimeDelta::Millis(1)
          : TimeDelta::Seconds(params_.analyzer.test_durations_secs),
      graph_data_output_file, graph_title,
      test::VideoTestConstants::kVideoSendSsrcs[params_.ss[0].selected_stream],
      test::VideoTestConstants::kSendRtxSsrcs[params_.ss[0].selected_stream],
      static_cast<size_t>(params_.ss[0].selected_stream),
      params.ss[0].selected_sl, params_.video[0].selected_tl,
      is_quick_test_enabled, &env_.clock(), params_.logging.rtp_dump_name,
      task_queue());

  SendTask(task_queue(), [&]() {
    analyzer_->SetCall(sender_call_.get());
    analyzer_->SetReceiver(receiver_call_->Receiver());
    send_transport->SetReceiver(analyzer_.get());
    recv_transport->SetReceiver(sender_call_->Receiver());

    SetupVideo(analyzer_.get(), recv_transport.get());
    SetupThumbnails(analyzer_.get(), recv_transport.get());
    video_receive_configs_[params_.ss[0].selected_stream].renderer =
        analyzer_.get();

    CreateFlexfecStreams();
    CreateVideoStreams();
    analyzer_->SetSendStream(video_send_streams_[0]);
    analyzer_->SetReceiveStream(
        video_receive_streams_[params_.ss[0].selected_stream]);

    GetVideoSendStream()->SetSource(analyzer_->OutputInterface(),
                                    degradation_preference_);
    SetupThumbnailCapturers(params_.call.num_thumbnails);
    for (size_t i = 0; i < thumbnail_send_streams_.size(); ++i) {
      thumbnail_send_streams_[i]->SetSource(thumbnail_capturers_[i].get(),
                                            degradation_preference_);
    }

    CreateCapturers();

    analyzer_->SetSource(video_sources_[0].get(), true);

    for (size_t video_idx = 1; video_idx < num_video_streams_; ++video_idx) {
      video_send_streams_[video_idx]->SetSource(video_sources_[video_idx].get(),
                                                degradation_preference_);
    }

    if (params_.audio.enabled) {
      SetupAudio(send_transport.get());
      StartAudioStreams();
      analyzer_->SetAudioReceiveStream(audio_receive_streams_[0]);
    }
    StartVideoStreams();
    StartThumbnails();
    analyzer_->StartMeasuringCpuProcessTime();
  });

  analyzer_->Wait();

  SendTask(task_queue(), [&]() {
    StopThumbnails();
    Stop();

    DestroyStreams();
    DestroyThumbnailStreams();

    if (graph_data_output_file)
      fclose(graph_data_output_file);

    send_transport.reset();
    recv_transport.reset();

    DestroyCalls();
  });
  analyzer_ = nullptr;
}

scoped_refptr<AudioDeviceModule> VideoQualityTest::CreateAudioDevice() {
#ifdef WEBRTC_WIN
  RTC_LOG(LS_INFO) << "Using latest version of ADM on Windows";
  // We must initialize the COM library on a thread before we calling any of
  // the library functions. All COM functions in the ADM will return
  // CO_E_NOTINITIALIZED otherwise. The legacy ADM for Windows used internal
  // COM initialization but the new ADM requires COM to be initialized
  // externally.
  com_initializer_ =
      std::make_unique<ScopedCOMInitializer>(ScopedCOMInitializer::kMTA);
  RTC_CHECK(com_initializer_->Succeeded());
  RTC_CHECK(webrtc_win::core_audio_utility::IsSupported());
  RTC_CHECK(webrtc_win::core_audio_utility::IsMMCSSSupported());
  return CreateWindowsCoreAudioAudioDeviceModule(env_);
#else
  // Use legacy factory method on all platforms except Windows.
  return CreateAudioDeviceModule(env_,
                                 AudioDeviceModule::kPlatformDefaultAudio);
#endif
}

void VideoQualityTest::InitializeAudioDevice(CallConfig* send_call_config,
                                             CallConfig* recv_call_config,
                                             bool use_real_adm) {
  scoped_refptr<AudioDeviceModule> audio_device;
  if (use_real_adm) {
    // Run test with real ADM (using default audio devices) if user has
    // explicitly set the --audio and --use_real_adm command-line flags.
    audio_device = CreateAudioDevice();
  } else {
    // By default, create a test ADM which fakes audio.
    audio_device = TestAudioDeviceModule::Create(
        env_, TestAudioDeviceModule::CreatePulsedNoiseCapturer(32000, 48000),
        TestAudioDeviceModule::CreateDiscardRenderer(48000), 1.f);
  }
  RTC_CHECK(audio_device);

  AudioState::Config audio_state_config;
  audio_state_config.audio_mixer = AudioMixerImpl::Create();
  audio_state_config.audio_processing =
      BuiltinAudioProcessingBuilder().Build(send_call_config->env);
  audio_state_config.audio_device_module = audio_device;
  send_call_config->audio_state = AudioState::Create(audio_state_config);
  recv_call_config->audio_state = AudioState::Create(audio_state_config);
  if (use_real_adm) {
    // The real ADM requires extra initialization: setting default devices,
    // setting up number of channels etc. Helper class also calls
    // AudioDeviceModule::Init().
    adm_helpers::Init(audio_device.get());
  } else {
    audio_device->Init();
  }
  // Always initialize the ADM before injecting a valid audio transport.
  RTC_CHECK(audio_device->RegisterAudioCallback(
                send_call_config->audio_state->audio_transport()) == 0);
}

void VideoQualityTest::SetupAudio(Transport* transport) {
  AudioSendStream::Config audio_send_config(transport);
  audio_send_config.rtp.ssrc = test::VideoTestConstants::kAudioSendSsrc;

  // Add extension to enable audio send side BWE, and allow audio bit rate
  // adaptation.
  audio_send_config.rtp.extensions.clear();
  audio_send_config.send_codec_spec = AudioSendStream::Config::SendCodecSpec(
      test::VideoTestConstants::kAudioSendPayloadType,
      {"OPUS",
       48000,
       2,
       {{"usedtx", (params_.audio.dtx ? "1" : "0")}, {"stereo", "1"}}});

  if (params_.call.send_side_bwe) {
    audio_send_config.rtp.extensions.push_back(
        RtpExtension(RtpExtension::kTransportSequenceNumberUri,
                     kTransportSequenceNumberExtensionId));
    audio_send_config.min_bitrate_bps = kOpusMinBitrateBps;
    audio_send_config.max_bitrate_bps = kOpusBitrateFbBps;
    // Only allow ANA when send-side BWE is enabled.
    audio_send_config.audio_network_adaptor_config = params_.audio.ana_config;
  }
  audio_send_config.encoder_factory = audio_encoder_factory_;
  SetAudioConfig(audio_send_config);

  std::string sync_group;
  if (params_.video[0].enabled && params_.audio.sync_video)
    sync_group = kSyncGroup;

  CreateMatchingAudioConfigs(transport, sync_group);
  CreateAudioStreams();
}

void VideoQualityTest::RunWithRenderers(const Params& params) {
  RTC_LOG(LS_INFO) << __FUNCTION__;
  num_video_streams_ = params.call.dual_video ? 2 : 1;
  std::unique_ptr<test::LayerFilteringTransport> send_transport;
  std::unique_ptr<test::DirectTransport> recv_transport;
  std::unique_ptr<test::VideoRenderer> local_preview;
  std::vector<std::unique_ptr<test::VideoRenderer>> loopback_renderers;

  if (!params.logging.rtc_event_log_name.empty()) {
    std::unique_ptr<RtcEventLog> send_event_log =
        rtc_event_log_factory_.Create(env());
    std::unique_ptr<RtcEventLog> recv_event_log =
        rtc_event_log_factory_.Create(env());
    std::unique_ptr<RtcEventLogOutputFile> send_output(
        std::make_unique<RtcEventLogOutputFile>(
            params.logging.rtc_event_log_name + "_send",
            RtcEventLog::kUnlimitedOutput));
    std::unique_ptr<RtcEventLogOutputFile> recv_output(
        std::make_unique<RtcEventLogOutputFile>(
            params.logging.rtc_event_log_name + "_recv",
            RtcEventLog::kUnlimitedOutput));
    bool event_log_started =
        send_event_log->StartLogging(std::move(send_output),
                                     /*output_period_ms=*/5000) &&
        recv_event_log->StartLogging(std::move(recv_output),
                                     /*output_period_ms=*/5000);
    RTC_DCHECK(event_log_started);
    SetSendEventLog(std::move(send_event_log));
    SetRecvEventLog(std::move(recv_event_log));
  }

  SendTask(task_queue(), [&]() {
    params_ = params;
    CheckParamsAndInjectionComponents();

    // TODO(ivica): Remove bitrate_config and use the default CallConfig(), to
    // match the full stack tests.
    CallConfig send_call_config = SendCallConfig();
    send_call_config.bitrate_config = params_.call.call_bitrate_config;
    CallConfig recv_call_config = RecvCallConfig();

    if (params_.audio.enabled)
      InitializeAudioDevice(&send_call_config, &recv_call_config,
                            params_.audio.use_real_adm);

    CreateCalls(std::move(send_call_config), std::move(recv_call_config));

    // TODO(minyue): consider if this is a good transport even for audio only
    // calls.
    send_transport = CreateSendTransport();

    recv_transport = CreateReceiveTransport();

    // TODO(ivica): Use two calls to be able to merge with RunWithAnalyzer or at
    // least share as much code as possible. That way this test would also match
    // the full stack tests better.
    send_transport->SetReceiver(receiver_call_->Receiver());
    recv_transport->SetReceiver(sender_call_->Receiver());

    if (params_.video[0].enabled) {
      // Create video renderers.
      SetupVideo(send_transport.get(), recv_transport.get());
      size_t num_streams_processed = 0;
      for (size_t video_idx = 0; video_idx < num_video_streams_; ++video_idx) {
        const size_t selected_stream_id = params_.ss[video_idx].selected_stream;
        const size_t num_streams = params_.ss[video_idx].streams.size();
        if (selected_stream_id == num_streams) {
          for (size_t stream_id = 0; stream_id < num_streams; ++stream_id) {
            StringBuilder oss;
            oss << "Loopback Video #" << video_idx << " - Stream #"
                << static_cast<int>(stream_id);
            loopback_renderers.emplace_back(test::VideoRenderer::Create(
                oss.str().c_str(),
                params_.ss[video_idx].streams[stream_id].width,
                params_.ss[video_idx].streams[stream_id].height));
            video_receive_configs_[stream_id + num_streams_processed].renderer =
                loopback_renderers.back().get();
            if (params_.audio.enabled && params_.audio.sync_video)
              video_receive_configs_[stream_id + num_streams_processed]
                  .sync_group = kSyncGroup;
          }
        } else {
          StringBuilder oss;
          oss << "Loopback Video #" << video_idx;
          loopback_renderers.emplace_back(test::VideoRenderer::Create(
              oss.str().c_str(),
              params_.ss[video_idx].streams[selected_stream_id].width,
              params_.ss[video_idx].streams[selected_stream_id].height));
          video_receive_configs_[selected_stream_id + num_streams_processed]
              .renderer = loopback_renderers.back().get();
          if (params_.audio.enabled && params_.audio.sync_video)
            video_receive_configs_[num_streams_processed + selected_stream_id]
                .sync_group = kSyncGroup;
        }
        num_streams_processed += num_streams;
      }
      CreateFlexfecStreams();
      CreateVideoStreams();

      CreateCapturers();
      if (params_.video[0].enabled) {
        // Create local preview
        local_preview.reset(test::VideoRenderer::Create(
            "Local Preview", params_.video[0].width, params_.video[0].height));

        video_sources_[0]->AddOrUpdateSink(local_preview.get(),
                                           VideoSinkWants());
      }
      ConnectVideoSourcesToStreams();
    }

    if (params_.audio.enabled) {
      SetupAudio(send_transport.get());
    }

    Start();
  });

  PressEnterToContinue(task_queue());

  SendTask(task_queue(), [&]() {
    Stop();
    DestroyStreams();

    send_transport.reset();
    recv_transport.reset();

    local_preview.reset();
    loopback_renderers.clear();

    DestroyCalls();
  });
}

}  // namespace webrtc
