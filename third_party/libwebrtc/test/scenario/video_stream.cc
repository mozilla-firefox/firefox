/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "test/scenario/video_stream.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "absl/strings/match.h"
#include "api/test/create_frame_generator.h"
#include "api/test/frame_generator_interface.h"
#include "api/test/video/function_video_encoder_factory.h"
#include "api/video/builtin_video_bitrate_allocator_factory.h"
#include "media/base/media_constants.h"
#include "media/engine/internal_decoder_factory.h"
#include "media/engine/internal_encoder_factory.h"
#include "media/engine/webrtc_video_engine.h"
#include "modules/video_coding/svc/scalability_mode_util.h"
#include "test/call_test.h"
#include "test/fake_encoder.h"
#include "test/scenario/hardware_codecs.h"
#include "test/testsupport/file_utils.h"
#include "test/video_test_constants.h"
#include "video/config/encoder_stream_factory.h"

namespace webrtc {
namespace test {
namespace {
enum : int {  // The first valid value is 1.
  kTransportSequenceNumberExtensionId = 1,
  kAbsSendTimeExtensionId,
  kVideoContentTypeExtensionId,
  kVideoRotationRtpExtensionId,
};

uint8_t CodecTypeToPayloadType(VideoCodecType codec_type) {
  switch (codec_type) {
    case VideoCodecType::kVideoCodecGeneric:
      return VideoTestConstants::kFakeVideoSendPayloadType;
    case VideoCodecType::kVideoCodecVP8:
      return VideoTestConstants::kPayloadTypeVP8;
    case VideoCodecType::kVideoCodecVP9:
      return VideoTestConstants::kPayloadTypeVP9;
    case VideoCodecType::kVideoCodecH264:
      return VideoTestConstants::kPayloadTypeH264;
    case VideoCodecType::kVideoCodecH265:
      return VideoTestConstants::kPayloadTypeH265;
    default:
      RTC_DCHECK_NOTREACHED();
  }
  return {};
}
std::string CodecTypeToCodecName(VideoCodecType codec_type) {
  switch (codec_type) {
    case VideoCodecType::kVideoCodecGeneric:
      return "";
    case VideoCodecType::kVideoCodecVP8:
      return cricket::kVp8CodecName;
    case VideoCodecType::kVideoCodecVP9:
      return cricket::kVp9CodecName;
    case VideoCodecType::kVideoCodecH264:
      return cricket::kH264CodecName;
    case VideoCodecType::kVideoCodecH265:
      return cricket::kH265CodecName;
    default:
      RTC_DCHECK_NOTREACHED();
  }
  return {};
}
VideoEncoderConfig::ContentType ConvertContentType(
    VideoStreamConfig::Encoder::ContentType content_type) {
  switch (content_type) {
    case VideoStreamConfig::Encoder::ContentType::kVideo:
      return VideoEncoderConfig::ContentType::kRealtimeVideo;
    case VideoStreamConfig::Encoder::ContentType::kScreen:
      return VideoEncoderConfig::ContentType::kScreen;
  }
}

std::string TransformFilePath(std::string path) {
  static const std::string resource_prefix = "res://";
  int ext_pos = path.rfind('.');
  if (ext_pos < 0) {
    return test::ResourcePath(path, "yuv");
  } else if (absl::StartsWith(path, resource_prefix)) {
    std::string name = path.substr(resource_prefix.length(), ext_pos);
    std::string ext = path.substr(ext_pos, path.size());
    return test::ResourcePath(name, ext);
  }
  return path;
}

VideoSendStream::Config CreateVideoSendStreamConfig(
    VideoStreamConfig config,
    std::vector<uint32_t> ssrcs,
    std::vector<uint32_t> rtx_ssrcs,
    Transport* send_transport) {
  VideoSendStream::Config send_config(send_transport);
  send_config.rtp.payload_name = CodecTypeToPayloadString(config.encoder.codec);
  send_config.rtp.payload_type = CodecTypeToPayloadType(config.encoder.codec);
  send_config.rtp.nack.rtp_history_ms =
      config.stream.nack_history_time.ms<int>();

  send_config.rtp.ssrcs = ssrcs;
  send_config.rtp.extensions = GetVideoRtpExtensions(config);

  if (config.stream.use_rtx) {
    send_config.rtp.rtx.payload_type = VideoTestConstants::kSendRtxPayloadType;
    send_config.rtp.rtx.ssrcs = rtx_ssrcs;
  }
  if (config.stream.use_flexfec) {
    send_config.rtp.flexfec.payload_type =
        VideoTestConstants::kFlexfecPayloadType;
    send_config.rtp.flexfec.ssrc = VideoTestConstants::kFlexfecSendSsrc;
    send_config.rtp.flexfec.protected_media_ssrcs = ssrcs;
  }
  if (config.stream.use_ulpfec) {
    send_config.rtp.ulpfec.red_payload_type =
        VideoTestConstants::kRedPayloadType;
    send_config.rtp.ulpfec.ulpfec_payload_type =
        VideoTestConstants::kUlpfecPayloadType;
    send_config.rtp.ulpfec.red_rtx_payload_type =
        VideoTestConstants::kRtxRedPayloadType;
  }
  return send_config;
}
rtc::scoped_refptr<VideoEncoderConfig::EncoderSpecificSettings>
CreateVp9SpecificSettings(VideoStreamConfig video_config) {
  constexpr auto kScreen = VideoStreamConfig::Encoder::ContentType::kScreen;
  VideoStreamConfig::Encoder conf = video_config.encoder;
  VideoCodecVP9 vp9 = VideoEncoder::GetDefaultVp9Settings();
  // TODO(bugs.webrtc.org/11607): Support separate scalability mode per
  // simulcast stream.
  ScalabilityMode scalability_mode = conf.simulcast_streams[0];
  vp9.keyFrameInterval = conf.key_frame_interval.value_or(0);
  vp9.numberOfTemporalLayers =
      ScalabilityModeToNumTemporalLayers(scalability_mode);
  vp9.numberOfSpatialLayers =
      ScalabilityModeToNumSpatialLayers(scalability_mode);
  vp9.interLayerPred = ScalabilityModeToInterLayerPredMode(scalability_mode);

  if (conf.content_type == kScreen &&
      (video_config.source.framerate > 5 || vp9.numberOfSpatialLayers >= 3)) {
    vp9.flexibleMode = true;
  }

  if (conf.content_type == kScreen || vp9.numberOfTemporalLayers > 1 ||
      vp9.numberOfSpatialLayers > 1) {
    vp9.automaticResizeOn = false;
    vp9.denoisingOn = false;
  } else {
    vp9.automaticResizeOn = conf.single.automatic_scaling;
    vp9.denoisingOn = conf.single.denoising;
  }
  return rtc::make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
      vp9);
}

rtc::scoped_refptr<VideoEncoderConfig::EncoderSpecificSettings>
CreateVp8SpecificSettings(VideoStreamConfig config) {
  VideoCodecVP8 vp8_settings = VideoEncoder::GetDefaultVp8Settings();
  vp8_settings.keyFrameInterval = config.encoder.key_frame_interval.value_or(0);
  // TODO(bugs.webrtc.org/11607): Support separate scalability mode per
  // simulcast stream.
  ScalabilityMode scalability_mode = config.encoder.simulcast_streams[0];
  vp8_settings.numberOfTemporalLayers =
      ScalabilityModeToNumTemporalLayers(scalability_mode);
  if (vp8_settings.numberOfTemporalLayers > 1 ||
      config.encoder.simulcast_streams.size() > 1) {
    vp8_settings.automaticResizeOn = false;
    vp8_settings.denoisingOn = false;
  } else {
    vp8_settings.automaticResizeOn = config.encoder.single.automatic_scaling;
    vp8_settings.denoisingOn = config.encoder.single.denoising;
  }
  return rtc::make_ref_counted<VideoEncoderConfig::Vp8EncoderSpecificSettings>(
      vp8_settings);
}

rtc::scoped_refptr<VideoEncoderConfig::EncoderSpecificSettings>
CreateH264SpecificSettings(VideoStreamConfig config) {
  RTC_DCHECK_EQ(config.encoder.simulcast_streams.size(), 1);
  RTC_DCHECK(config.encoder.simulcast_streams[0] == ScalabilityMode::kL1T1);
  // TODO(bugs.webrtc.org/6883): Set a key frame interval as a setting that
  // isn't codec specific.
  RTC_CHECK_EQ(0, config.encoder.key_frame_interval.value_or(0));
  return nullptr;
}

rtc::scoped_refptr<VideoEncoderConfig::EncoderSpecificSettings>
CreateEncoderSpecificSettings(VideoStreamConfig config) {
  using Codec = VideoStreamConfig::Encoder::Codec;
  switch (config.encoder.codec) {
    case Codec::kVideoCodecH264:
      return CreateH264SpecificSettings(config);
    case Codec::kVideoCodecVP8:
      return CreateVp8SpecificSettings(config);
    case Codec::kVideoCodecVP9:
      return CreateVp9SpecificSettings(config);
    case Codec::kVideoCodecGeneric:
    case Codec::kVideoCodecAV1:
    case Codec::kVideoCodecH265:
      return nullptr;
  }
}

VideoEncoderConfig CreateVideoEncoderConfig(VideoStreamConfig config) {
  webrtc::VideoEncoder::EncoderInfo encoder_info;
  VideoEncoderConfig encoder_config;
  encoder_config.codec_type = config.encoder.codec;
  encoder_config.content_type = ConvertContentType(config.encoder.content_type);
  encoder_config.video_format =
      SdpVideoFormat(CodecTypeToPayloadString(config.encoder.codec), {});

  encoder_config.number_of_streams = config.encoder.simulcast_streams.size();
  encoder_config.simulcast_layers =
      std::vector<VideoStream>(encoder_config.number_of_streams);
  encoder_config.min_transmit_bitrate_bps = config.stream.pad_to_rate.bps();

  std::string cricket_codec = CodecTypeToCodecName(config.encoder.codec);
  if (!cricket_codec.empty()) {
    bool screenshare = config.encoder.content_type ==
                       VideoStreamConfig::Encoder::ContentType::kScreen;
    encoder_config.video_stream_factory =
        rtc::make_ref_counted<cricket::EncoderStreamFactory>(
            cricket_codec, cricket::kDefaultVideoMaxQpVpx, screenshare,
            screenshare, encoder_info);
  } else {
    encoder_config.video_stream_factory =
        rtc::make_ref_counted<DefaultVideoStreamFactory>();
  }

  // TODO(srte): Base this on encoder capabilities.
  encoder_config.max_bitrate_bps =
      config.encoder.max_data_rate.value_or(DataRate::KilobitsPerSec(10000))
          .bps();

  encoder_config.frame_drop_enabled = config.encoder.frame_dropping;
  encoder_config.encoder_specific_settings =
      CreateEncoderSpecificSettings(config);

  for (size_t i = 0; i < encoder_config.number_of_streams; ++i) {
    auto& layer = encoder_config.simulcast_layers[i];
    if (config.encoder.max_framerate) {
      layer.max_framerate = *config.encoder.max_framerate;
      layer.min_bitrate_bps = config.encoder.min_data_rate->bps_or(-1);
    }
    layer.scalability_mode = config.encoder.simulcast_streams[i];
  }

  return encoder_config;
}

std::unique_ptr<FrameGeneratorInterface> CreateImageSlideGenerator(
    Clock* clock,
    VideoStreamConfig::Source::Slides slides,
    int framerate) {
  std::vector<std::string> paths = slides.images.paths;
  for (std::string& path : paths)
    path = TransformFilePath(path);
  if (slides.images.crop.width || slides.images.crop.height) {
    TimeDelta pause_duration =
        slides.change_interval - slides.images.crop.scroll_duration;
    RTC_CHECK_GE(pause_duration, TimeDelta::Zero());
    int crop_width = slides.images.crop.width.value_or(slides.images.width);
    int crop_height = slides.images.crop.height.value_or(slides.images.height);
    RTC_CHECK_LE(crop_width, slides.images.width);
    RTC_CHECK_LE(crop_height, slides.images.height);
    return CreateScrollingInputFromYuvFilesFrameGenerator(
        clock, paths, slides.images.width, slides.images.height, crop_width,
        crop_height, slides.images.crop.scroll_duration.ms(),
        pause_duration.ms());
  } else {
    return CreateFromYuvFileFrameGenerator(
        paths, slides.images.width, slides.images.height,
        slides.change_interval.seconds<double>() * framerate);
  }
}

std::unique_ptr<FrameGeneratorInterface> CreateFrameGenerator(
    Clock* clock,
    VideoStreamConfig::Source source) {
  using Capture = VideoStreamConfig::Source::Capture;
  switch (source.capture) {
    case Capture::kGenerator:
      return CreateSquareFrameGenerator(
          source.generator.width, source.generator.height,
          source.generator.pixel_format, /*num_squares*/ absl::nullopt);
    case Capture::kVideoFile:
      RTC_CHECK(source.video_file.width && source.video_file.height);
      return CreateFromYuvFileFrameGenerator(
          {TransformFilePath(source.video_file.name)}, source.video_file.width,
          source.video_file.height, /*frame_repeat_count*/ 1);
    case Capture::kGenerateSlides:
      return CreateSlideFrameGenerator(
          source.slides.generator.width, source.slides.generator.height,
          source.slides.change_interval.seconds<double>() * source.framerate);
    case Capture::kImageSlides:
      return CreateImageSlideGenerator(clock, source.slides, source.framerate);
  }
}

VideoReceiveStreamInterface::Config CreateVideoReceiveStreamConfig(
    VideoStreamConfig config,
    Transport* feedback_transport,
    VideoDecoderFactory* decoder_factory,
    VideoReceiveStreamInterface::Decoder decoder,
    rtc::VideoSinkInterface<VideoFrame>* renderer,
    uint32_t local_ssrc,
    uint32_t ssrc,
    uint32_t rtx_ssrc) {
  VideoReceiveStreamInterface::Config recv(feedback_transport);
  recv.rtp.local_ssrc = local_ssrc;

  RTC_DCHECK(!config.stream.use_rtx ||
             config.stream.nack_history_time > TimeDelta::Zero());
  recv.rtp.nack.rtp_history_ms = config.stream.nack_history_time.ms();
  recv.rtp.protected_by_flexfec = config.stream.use_flexfec;
  recv.rtp.remote_ssrc = ssrc;
  recv.decoder_factory = decoder_factory;
  recv.decoders.push_back(decoder);
  recv.renderer = renderer;
  if (config.stream.use_rtx) {
    recv.rtp.rtx_ssrc = rtx_ssrc;
    recv.rtp
        .rtx_associated_payload_types[VideoTestConstants::kSendRtxPayloadType] =
        CodecTypeToPayloadType(config.encoder.codec);
  }
  if (config.stream.use_ulpfec) {
    recv.rtp.red_payload_type = VideoTestConstants::kRedPayloadType;
    recv.rtp.ulpfec_payload_type = VideoTestConstants::kUlpfecPayloadType;
    recv.rtp
        .rtx_associated_payload_types[VideoTestConstants::kRtxRedPayloadType] =
        VideoTestConstants::kRedPayloadType;
  }
  recv.sync_group = config.render.sync_group;
  return recv;
}
}  // namespace

std::vector<RtpExtension> GetVideoRtpExtensions(
    const VideoStreamConfig config) {
  std::vector<RtpExtension> res = {
      RtpExtension(RtpExtension::kVideoContentTypeUri,
                   kVideoContentTypeExtensionId),
      RtpExtension(RtpExtension::kVideoRotationUri,
                   kVideoRotationRtpExtensionId)};
  if (config.stream.packet_feedback) {
    res.push_back(RtpExtension(RtpExtension::kTransportSequenceNumberUri,
                               kTransportSequenceNumberExtensionId));
  }
  if (config.stream.abs_send_time) {
    res.push_back(
        RtpExtension(RtpExtension::kAbsSendTimeUri, kAbsSendTimeExtensionId));
  }
  return res;
}

SendVideoStream::SendVideoStream(CallClient* sender,
                                 VideoStreamConfig config,
                                 Transport* send_transport,
                                 VideoFrameMatcher* matcher)
    : sender_(sender), config_(config) {
  video_capturer_ = std::make_unique<FrameGeneratorCapturer>(
      &sender_->env_.clock(),
      CreateFrameGenerator(&sender_->env_.clock(), config.source),
      config.source.framerate, sender_->env_.task_queue_factory());
  video_capturer_->Init();

  using Encoder = VideoStreamConfig::Encoder;
  using Codec = VideoStreamConfig::Encoder::Codec;
  switch (config.encoder.implementation) {
    case Encoder::Implementation::kFake:
      encoder_factory_ = std::make_unique<FunctionVideoEncoderFactory>(
          [this](const Environment& env, const SdpVideoFormat& format) {
            MutexLock lock(&mutex_);
            std::unique_ptr<FakeEncoder> encoder;
            if (config_.encoder.codec == Codec::kVideoCodecVP8) {
              encoder = std::make_unique<test::FakeVp8Encoder>(env);
            } else if (config_.encoder.codec == Codec::kVideoCodecGeneric) {
              encoder = std::make_unique<test::FakeEncoder>(env);
            } else {
              RTC_DCHECK_NOTREACHED();
            }
            fake_encoders_.push_back(encoder.get());
            if (config_.encoder.fake.max_rate.IsFinite())
              encoder->SetMaxBitrate(config_.encoder.fake.max_rate.kbps());
            return encoder;
          });
      break;
    case VideoStreamConfig::Encoder::Implementation::kSoftware:
      encoder_factory_.reset(new InternalEncoderFactory());
      break;
    case VideoStreamConfig::Encoder::Implementation::kHardware:
      encoder_factory_ = CreateHardwareEncoderFactory();
      break;
  }
  RTC_CHECK(encoder_factory_);

  bitrate_allocator_factory_ = CreateBuiltinVideoBitrateAllocatorFactory();
  RTC_CHECK(bitrate_allocator_factory_);

  VideoEncoderConfig encoder_config = CreateVideoEncoderConfig(config);
  for (size_t i = 0; i < encoder_config.number_of_streams; ++i) {
    ssrcs_.push_back(sender->GetNextVideoSsrc());
    rtx_ssrcs_.push_back(sender->GetNextRtxSsrc());
  }
  VideoSendStream::Config send_config =
      CreateVideoSendStreamConfig(config, ssrcs_, rtx_ssrcs_, send_transport);
  send_config.encoder_settings.encoder_factory = encoder_factory_.get();
  send_config.encoder_settings.bitrate_allocator_factory =
      bitrate_allocator_factory_.get();
  send_config.suspend_below_min_bitrate =
      config.encoder.suspend_below_min_bitrate;

  video_capturer_->Start();
  sender_->SendTask([&] {
    if (config.stream.fec_controller_factory) {
      send_stream_ = sender_->call_->CreateVideoSendStream(
          std::move(send_config), std::move(encoder_config),
          config.stream.fec_controller_factory->CreateFecController(
              sender_->env_));
    } else {
      send_stream_ = sender_->call_->CreateVideoSendStream(
          std::move(send_config), std::move(encoder_config));
    }

    if (matcher->Active()) {
      frame_tap_ = std::make_unique<ForwardingCapturedFrameTap>(
          &sender_->env_.clock(), matcher, video_capturer_.get());
      send_stream_->SetSource(frame_tap_.get(),
                              config.encoder.degradation_preference);
    } else {
      send_stream_->SetSource(video_capturer_.get(),
                              config.encoder.degradation_preference);
    }
  });
}

SendVideoStream::~SendVideoStream() {
  sender_->SendTask(
      [this] { sender_->call_->DestroyVideoSendStream(send_stream_); });
}

void SendVideoStream::Start() {
  sender_->SendTask([this] {
    send_stream_->Start();
    sender_->call_->SignalChannelNetworkState(MediaType::VIDEO, kNetworkUp);
  });
}

void SendVideoStream::Stop() {
  sender_->SendTask([this] { send_stream_->Stop(); });
}

void SendVideoStream::UpdateConfig(
    std::function<void(VideoStreamConfig*)> modifier) {
  sender_->SendTask([&] {
    MutexLock lock(&mutex_);
    VideoStreamConfig prior_config = config_;
    modifier(&config_);
    if (prior_config.encoder.fake.max_rate != config_.encoder.fake.max_rate) {
      for (auto* encoder : fake_encoders_) {
        encoder->SetMaxBitrate(config_.encoder.fake.max_rate.kbps());
      }
    }
    // TODO(srte): Add more conditions that should cause reconfiguration.
    if (prior_config.encoder.max_framerate != config_.encoder.max_framerate ||
        prior_config.encoder.max_data_rate != config_.encoder.max_data_rate) {
      VideoEncoderConfig encoder_config = CreateVideoEncoderConfig(config_);
      send_stream_->ReconfigureVideoEncoder(std::move(encoder_config));
    }
    if (prior_config.source.framerate != config_.source.framerate) {
      SetCaptureFramerate(config_.source.framerate);
    }
  });
}

void SendVideoStream::UpdateActiveLayers(std::vector<bool> active_layers) {
  sender_->task_queue_.PostTask([=] {
    MutexLock lock(&mutex_);
    VideoEncoderConfig encoder_config = CreateVideoEncoderConfig(config_);
    RTC_CHECK_EQ(encoder_config.simulcast_layers.size(), active_layers.size());
    for (size_t i = 0; i < encoder_config.simulcast_layers.size(); ++i)
      encoder_config.simulcast_layers[i].active = active_layers[i];
    send_stream_->ReconfigureVideoEncoder(std::move(encoder_config));
  });
}

bool SendVideoStream::UsingSsrc(uint32_t ssrc) const {
  for (uint32_t owned : ssrcs_) {
    if (owned == ssrc)
      return true;
  }
  return false;
}

bool SendVideoStream::UsingRtxSsrc(uint32_t ssrc) const {
  for (uint32_t owned : rtx_ssrcs_) {
    if (owned == ssrc)
      return true;
  }
  return false;
}

void SendVideoStream::SetCaptureFramerate(int framerate) {
  sender_->SendTask([&] { video_capturer_->ChangeFramerate(framerate); });
}

VideoSendStream::Stats SendVideoStream::GetStats() const {
  return send_stream_->GetStats();
}

ColumnPrinter SendVideoStream::StatsPrinter() {
  return ColumnPrinter::Lambda(
      "video_target_rate video_sent_rate width height",
      [this](rtc::SimpleStringBuilder& sb) {
        VideoSendStream::Stats video_stats = send_stream_->GetStats();
        int width = 0;
        int height = 0;
        for (const auto& stream_stat : video_stats.substreams) {
          width = std::max(width, stream_stat.second.width);
          height = std::max(height, stream_stat.second.height);
        }
        sb.AppendFormat("%.0lf %.0lf %i %i",
                        video_stats.target_media_bitrate_bps / 8.0,
                        video_stats.media_bitrate_bps / 8.0, width, height);
      },
      64);
}

ReceiveVideoStream::ReceiveVideoStream(CallClient* receiver,
                                       VideoStreamConfig config,
                                       SendVideoStream* send_stream,
                                       size_t chosen_stream,
                                       Transport* feedback_transport,
                                       VideoFrameMatcher* matcher)
    : receiver_(receiver), config_(config) {
  if (config.encoder.codec ==
          VideoStreamConfig::Encoder::Codec::kVideoCodecGeneric ||
      config.encoder.implementation == VideoStreamConfig::Encoder::kFake) {
    decoder_factory_ = std::make_unique<FunctionVideoDecoderFactory>(
        []() { return std::make_unique<FakeDecoder>(); });
  } else {
    decoder_factory_ = std::make_unique<InternalDecoderFactory>();
  }

  VideoReceiveStreamInterface::Decoder decoder =
      CreateMatchingDecoder(CodecTypeToPayloadType(config.encoder.codec),
                            CodecTypeToPayloadString(config.encoder.codec));
  size_t num_streams = config.encoder.simulcast_streams.size();
  for (size_t i = 0; i < num_streams; ++i) {
    rtc::VideoSinkInterface<VideoFrame>* renderer = &fake_renderer_;
    if (matcher->Active()) {
      render_taps_.emplace_back(std::make_unique<DecodedFrameTap>(
          &receiver_->env_.clock(), matcher, i));
      renderer = render_taps_.back().get();
    }
    auto recv_config = CreateVideoReceiveStreamConfig(
        config, feedback_transport, decoder_factory_.get(), decoder, renderer,
        receiver_->GetNextVideoLocalSsrc(), send_stream->ssrcs_[i],
        send_stream->rtx_ssrcs_[i]);
    if (config.stream.use_flexfec) {
      RTC_DCHECK(num_streams == 1);
      FlexfecReceiveStream::Config flexfec(feedback_transport);
      flexfec.payload_type = VideoTestConstants::kFlexfecPayloadType;
      flexfec.rtp.remote_ssrc = VideoTestConstants::kFlexfecSendSsrc;
      flexfec.protected_media_ssrcs = send_stream->rtx_ssrcs_;
      flexfec.rtp.local_ssrc = recv_config.rtp.local_ssrc;
      receiver_->ssrc_media_types_[flexfec.rtp.remote_ssrc] = MediaType::VIDEO;

      receiver_->SendTask([this, &flexfec] {
        flecfec_stream_ = receiver_->call_->CreateFlexfecReceiveStream(flexfec);
      });
    }
    receiver_->ssrc_media_types_[recv_config.rtp.remote_ssrc] =
        MediaType::VIDEO;
    if (config.stream.use_rtx)
      receiver_->ssrc_media_types_[recv_config.rtp.rtx_ssrc] = MediaType::VIDEO;
    receiver_->SendTask([this, &recv_config] {
      receive_streams_.push_back(
          receiver_->call_->CreateVideoReceiveStream(std::move(recv_config)));
    });
  }
}

ReceiveVideoStream::~ReceiveVideoStream() {
  receiver_->SendTask([this] {
    for (auto* recv_stream : receive_streams_)
      receiver_->call_->DestroyVideoReceiveStream(recv_stream);
    if (flecfec_stream_)
      receiver_->call_->DestroyFlexfecReceiveStream(flecfec_stream_);
  });
}

void ReceiveVideoStream::Start() {
  receiver_->SendTask([this] {
    for (auto* recv_stream : receive_streams_)
      recv_stream->Start();
    receiver_->call_->SignalChannelNetworkState(MediaType::VIDEO, kNetworkUp);
  });
}

void ReceiveVideoStream::Stop() {
  receiver_->SendTask([this] {
    for (auto* recv_stream : receive_streams_)
      recv_stream->Stop();
  });
}

VideoReceiveStreamInterface::Stats ReceiveVideoStream::GetStats() const {
  if (receive_streams_.empty())
    return VideoReceiveStreamInterface::Stats();
  // TODO(srte): Handle multiple receive streams.
  return receive_streams_.back()->GetStats();
}

VideoStreamPair::~VideoStreamPair() = default;

VideoStreamPair::VideoStreamPair(CallClient* sender,
                                 CallClient* receiver,
                                 VideoStreamConfig config)
    : config_(config),
      matcher_(config.hooks.frame_pair_handlers),
      send_stream_(sender, config, sender->transport_.get(), &matcher_),
      receive_stream_(receiver,
                      config,
                      &send_stream_,
                      /*chosen_stream=*/0,
                      receiver->transport_.get(),
                      &matcher_) {}

}  // namespace test
}  // namespace webrtc
