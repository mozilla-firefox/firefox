/*
 *  Copyright (c) 2023 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/test/create_frame_generator.h"
#include "api/test/frame_generator_interface.h"
#include "api/video/builtin_video_bitrate_allocator_factory.h"
#include "api/video_codecs/builtin_video_encoder_factory.h"
#include "media/base/media_constants.h"
#include "modules/video_coding/codecs/av1/av1_svc_config.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/svc/scalability_mode_util.h"
#include "rtc_base/logging.h"
#include "rtc_tools/video_encoder/encoded_image_file_writer.h"
#include "test/testsupport/y4m_frame_generator.h"

ABSL_FLAG(std::string,
          video_codec,
          "",
          "Specify codec of video encoder: vp8, vp9, h264, av1");
ABSL_FLAG(std::string,
          scalability_mode,
          "L1T1",
          "Specify scalability mode of video encoder");

ABSL_FLAG(uint32_t,
          raw_frame_generator,
          0,
          "Specify SquareFrameGenerator or SlideGenerator.\n"
          "0: SquareFrameGenerator, 1: SlideGenerator");
ABSL_FLAG(uint32_t, width, 1280, "Specify width of video encoder");
ABSL_FLAG(uint32_t, height, 720, "Specify height of video encoder");

ABSL_FLAG(std::string,
          y4m_input_file,
          "",
          "Specify y4m input file of Y4mFrameGenerator");

ABSL_FLAG(std::string,
          ivf_input_file,
          "",
          "Specify ivf input file of IvfVideoFrameGenerator");

ABSL_FLAG(uint32_t, frame_rate_fps, 30, "Specify frame rate of video encoder");
ABSL_FLAG(uint32_t,
          bitrate_kbps,
          1500,
          "Specify bitrate_kbps of video encoder");
ABSL_FLAG(uint32_t,
          key_frame_interval,
          100,
          "Specify key frame interval of video encoder");

ABSL_FLAG(uint32_t, frames, 300, "Specify maximum encoded frames");

ABSL_FLAG(bool,
          list_formats,
          false,
          "List all supported formats of video encoder");

ABSL_FLAG(bool, verbose, false, "Verbose logs to stderr");

namespace webrtc {
namespace {

[[maybe_unused]] const char* InterLayerPredModeToString(
    const InterLayerPredMode& inter_layer_pred_mode) {
  switch (inter_layer_pred_mode) {
    case InterLayerPredMode::kOff:
      return "Off";
    case InterLayerPredMode::kOn:
      return "On";
    case InterLayerPredMode::kOnKeyPic:
      return "OnKeyPic";
  }
  RTC_CHECK_NOTREACHED();
  return "";
}

std::string ToString(const EncodedImage& encoded_image) {
  char buffer[1024];
  rtc::SimpleStringBuilder ss(buffer);

  ss << VideoFrameTypeToString(encoded_image._frameType)
     << ", size=" << encoded_image.size() << ", qp=" << encoded_image.qp_
     << ", timestamp=" << encoded_image.RtpTimestamp();

  if (encoded_image.SimulcastIndex()) {
    ss << ", SimulcastIndex=" << *encoded_image.SimulcastIndex();
  }

  if (encoded_image.SpatialIndex()) {
    ss << ", SpatialIndex=" << *encoded_image.SpatialIndex();
  }

  if (encoded_image.TemporalIndex()) {
    ss << ", TemporalIndex=" << *encoded_image.TemporalIndex();
  }

  return ss.str();
}

[[maybe_unused]] std::string ToString(
    const CodecSpecificInfo& codec_specific_info) {
  char buffer[1024];
  rtc::SimpleStringBuilder ss(buffer);

  ss << CodecTypeToPayloadString(codec_specific_info.codecType);

  if (codec_specific_info.scalability_mode) {
    ss << ", "
       << ScalabilityModeToString(*codec_specific_info.scalability_mode);
  }

  if (codec_specific_info.generic_frame_info) {
    auto& generic_frame_info = codec_specific_info.generic_frame_info;

    ss << ", decode_target_indications="
       << generic_frame_info->decode_target_indications.size();
  }

  if (codec_specific_info.template_structure) {
    auto& template_structure = codec_specific_info.template_structure;

    ss << ", structure_id=" << template_structure->structure_id
       << ", num_decode_targets=" << template_structure->num_decode_targets
       << ", num_chains=" << template_structure->num_chains;
  }

  return ss.str();
}

// Wrapper of `EncodedImageCallback` that writes all encoded images into ivf
// files through `test::EncodedImageFileWriter`.
class TestEncodedImageCallback final : public EncodedImageCallback {
 public:
  explicit TestEncodedImageCallback(const VideoCodec& video_codec_setting)
      : video_codec_setting_(video_codec_setting) {
    writer_ =
        std::make_unique<test::EncodedImageFileWriter>(video_codec_setting);
  }

  ~TestEncodedImageCallback() = default;

 private:
  Result OnEncodedImage(const EncodedImage& encoded_image,
                        const CodecSpecificInfo* codec_specific_info) override {
    RTC_LOG(LS_VERBOSE) << "frame " << frames_ << ": {"
                        << ToString(encoded_image)
                        << "}, codec_specific_info: {"
                        << ToString(*codec_specific_info) << "}";

    RTC_CHECK(writer_);
    writer_->Write(encoded_image);

    RTC_CHECK(codec_specific_info);
    // For SVC, every picture generates multiple encoded images of different
    // spatial layers.
    if (codec_specific_info->end_of_picture) {
      ++frames_;
    }

    return Result(Result::Error::OK);
  }

  VideoCodec video_codec_setting_;
  int32_t frames_ = 0;

  std::unique_ptr<test::EncodedImageFileWriter> writer_;
};

// Wrapper of `BuiltinVideoEncoderFactory`.
class TestVideoEncoderFactoryWrapper final {
 public:
  TestVideoEncoderFactoryWrapper() {
    builtin_video_encoder_factory_ = CreateBuiltinVideoEncoderFactory();
    RTC_CHECK(builtin_video_encoder_factory_);
  }

  ~TestVideoEncoderFactoryWrapper() = default;

  void ListSupportedFormats() const {
    // Log all supported formats.
    auto formats = builtin_video_encoder_factory_->GetSupportedFormats();
    for (auto& format : formats) {
      RTC_LOG(LS_INFO) << format.ToString();
    }
  }

  bool QueryCodecSupport(const std::string& video_codec_string,
                         const std::string& scalability_mode_string) const {
    RTC_CHECK(!video_codec_string.empty());
    RTC_CHECK(!scalability_mode_string.empty());

    // Simulcast is not implemented at this moment.
    if (scalability_mode_string[0] == 'S') {
      RTC_LOG(LS_ERROR) << "Not implemented format: "
                        << scalability_mode_string;
      return false;
    }

    // VP9 profile2 is not implemented at this moment.
    VideoEncoderFactory::CodecSupport support =
        builtin_video_encoder_factory_->QueryCodecSupport(
            SdpVideoFormat(video_codec_string), scalability_mode_string);
    return support.is_supported;
  }

  VideoCodec CreateVideoCodec(const std::string& video_codec_string,
                              const std::string& scalability_mode_string,
                              const uint32_t width,
                              const uint32_t height,
                              const uint32_t frame_rate_fps,
                              const uint32_t bitrate_kbps) {
    VideoCodec video_codec = {};

    VideoCodecType codec_type = PayloadStringToCodecType(video_codec_string);
    RTC_CHECK_NE(codec_type, kVideoCodecGeneric);

    // Retrieve scalability mode information.
    absl::optional<ScalabilityMode> scalability_mode =
        ScalabilityModeFromString(scalability_mode_string);
    RTC_CHECK(scalability_mode);

    uint32_t spatial_layers =
        ScalabilityModeToNumSpatialLayers(*scalability_mode);
    uint32_t temporal_layers =
        ScalabilityModeToNumTemporalLayers(*scalability_mode);
    InterLayerPredMode inter_layer_pred_mode =
        ScalabilityModeToInterLayerPredMode(*scalability_mode);

    // Codec settings.
    video_codec.SetScalabilityMode(*scalability_mode);
    video_codec.SetFrameDropEnabled(false);
    video_codec.SetVideoEncoderComplexity(
        VideoCodecComplexity::kComplexityNormal);

    video_codec.width = width;
    video_codec.height = height;
    video_codec.maxFramerate = frame_rate_fps;

    video_codec.startBitrate = bitrate_kbps;
    video_codec.maxBitrate = bitrate_kbps;
    video_codec.minBitrate = bitrate_kbps;

    video_codec.active = true;

    // Simulcast is not implemented at this moment.
    video_codec.numberOfSimulcastStreams = 0;

    video_codec.codecType = codec_type;
    // Codec specific settings.
    switch (video_codec.codecType) {
      case kVideoCodecVP8:
        RTC_CHECK_LE(spatial_layers, 1);

        *(video_codec.VP8()) = VideoEncoder::GetDefaultVp8Settings();
        video_codec.VP8()->numberOfTemporalLayers = temporal_layers;
        video_codec.qpMax = cricket::kDefaultVideoMaxQpVpx;
        break;

      case kVideoCodecVP9:
        *(video_codec.VP9()) = VideoEncoder::GetDefaultVp9Settings();
        video_codec.VP9()->numberOfSpatialLayers = spatial_layers;
        video_codec.VP9()->numberOfTemporalLayers = temporal_layers;
        video_codec.VP9()->interLayerPred = inter_layer_pred_mode;
        video_codec.qpMax = cricket::kDefaultVideoMaxQpVpx;
        break;

      case kVideoCodecH264:
        RTC_CHECK_LE(spatial_layers, 1);

        *(video_codec.H264()) = VideoEncoder::GetDefaultH264Settings();
        video_codec.H264()->numberOfTemporalLayers = temporal_layers;
        video_codec.qpMax = cricket::kDefaultVideoMaxQpH26x;
        break;

      case kVideoCodecAV1:
        if (SetAv1SvcConfig(video_codec, temporal_layers, spatial_layers)) {
          for (size_t i = 0; i < spatial_layers; ++i) {
            video_codec.spatialLayers[i].active = true;
          }
        } else {
          RTC_LOG(LS_WARNING) << "Failed to configure svc bitrates for av1.";
        }
        video_codec.qpMax = cricket::kDefaultVideoMaxQpVpx;
        break;
      case kVideoCodecH265:
        // TODO(bugs.webrtc.org/13485)
        video_codec.qpMax = cricket::kDefaultVideoMaxQpH26x;
        break;
      default:
        RTC_CHECK_NOTREACHED();
        break;
    }

    return video_codec;
  }

  std::unique_ptr<VideoEncoder> CreateAndInitializeVideoEncoder(
      const Environment& env,
      const VideoCodec& video_codec_setting) {
    const std::string video_codec_string =
        CodecTypeToPayloadString(video_codec_setting.codecType);
    const uint32_t bitrate_kbps = video_codec_setting.maxBitrate;
    const uint32_t frame_rate_fps = video_codec_setting.maxFramerate;

    // Create video encoder.
    std::unique_ptr<VideoEncoder> video_encoder =
        builtin_video_encoder_factory_->Create(
            env, SdpVideoFormat(video_codec_string));
    RTC_CHECK(video_encoder);

    // Initialize video encoder.
    const webrtc::VideoEncoder::Settings kSettings(
        webrtc::VideoEncoder::Capabilities(false),
        /*number_of_cores=*/1,
        /*max_payload_size=*/0);

    int ret = video_encoder->InitEncode(&video_codec_setting, kSettings);
    RTC_CHECK_EQ(ret, WEBRTC_VIDEO_CODEC_OK);

    // Set bitrates.
    std::unique_ptr<webrtc::VideoBitrateAllocator> bitrate_allocator_;
    bitrate_allocator_ = webrtc::CreateBuiltinVideoBitrateAllocatorFactory()
                             ->CreateVideoBitrateAllocator(video_codec_setting);
    RTC_CHECK(bitrate_allocator_);

    webrtc::VideoBitrateAllocation allocation =
        bitrate_allocator_->GetAllocation(bitrate_kbps * 1000, frame_rate_fps);
    RTC_LOG(LS_INFO) << allocation.ToString();

    video_encoder->SetRates(webrtc::VideoEncoder::RateControlParameters(
        allocation, frame_rate_fps));

    return video_encoder;
  }

 private:
  std::unique_ptr<VideoEncoderFactory> builtin_video_encoder_factory_;
};

}  // namespace
}  // namespace webrtc

// A video encode tool supports to specify video codec, scalability mode,
// resolution, frame rate, bitrate, key frame interval and maximum number of
// frames. The video encoder supports multiple `FrameGeneratorInterface`
// implementations: `SquareFrameGenerator`, `SlideFrameGenerator`,
// `Y4mFrameGenerator` and `IvfFileFrameGenerator`. All the encoded bitstreams
// are wrote into ivf output files.
int main(int argc, char* argv[]) {
  absl::SetProgramUsageMessage(
      "A video encode tool.\n"
      "\n"
      "Example usage:\n"
      "./video_encoder --list_formats\n"
      "\n"
      "./video_encoder --video_codec=vp8 --width=1280 "
      "--height=720 --bitrate_kbps=1500\n"
      "\n"
      "./video_encoder --raw_frame_generator=1 --video_codec=vp9 "
      "--scalability_mode=L3T3_KEY --width=640 --height=360 "
      "--frame_rate_fps=30 "
      "--bitrate_kbps=500\n"
      "\n"
      "./video_encoder --y4m_input_file=input.y4m --video_codec=av1 "
      "--scalability_mode=L1T3\n"
      "\n"
      "./video_encoder --ivf_input_file=input.ivf --video_codec=av1 "
      "--scalability_mode=L1T3\n");
  absl::ParseCommandLine(argc, argv);

  if (absl::GetFlag(FLAGS_verbose)) {
    rtc::LogMessage::LogToDebug(rtc::LS_VERBOSE);
  } else {
    rtc::LogMessage::LogToDebug(rtc::LS_INFO);
  }

  rtc::LogMessage::SetLogToStderr(true);

  const bool list_formats = absl::GetFlag(FLAGS_list_formats);

  // Video encoder configurations.
  const std::string video_codec_string = absl::GetFlag(FLAGS_video_codec);
  const std::string scalability_mode_string =
      absl::GetFlag(FLAGS_scalability_mode);

  const uint32_t width = absl::GetFlag(FLAGS_width);
  const uint32_t height = absl::GetFlag(FLAGS_height);

  uint32_t raw_frame_generator = absl::GetFlag(FLAGS_raw_frame_generator);

  const std::string y4m_input_file = absl::GetFlag(FLAGS_y4m_input_file);
  const std::string ivf_input_file = absl::GetFlag(FLAGS_ivf_input_file);

  const uint32_t frame_rate_fps = absl::GetFlag(FLAGS_frame_rate_fps);
  const uint32_t bitrate_kbps = absl::GetFlag(FLAGS_bitrate_kbps);
  const uint32_t key_frame_interval = absl::GetFlag(FLAGS_key_frame_interval);
  const uint32_t maximum_number_of_frames = absl::GetFlag(FLAGS_frames);

  const webrtc::Environment env = webrtc::CreateEnvironment();

  std::unique_ptr<webrtc::TestVideoEncoderFactoryWrapper>
      test_video_encoder_factory_wrapper =
          std::make_unique<webrtc::TestVideoEncoderFactoryWrapper>();

  // List all supported formats.
  if (list_formats) {
    test_video_encoder_factory_wrapper->ListSupportedFormats();
    return EXIT_SUCCESS;
  }

  if (video_codec_string.empty()) {
    RTC_LOG(LS_ERROR) << "Video codec is empty";
    return EXIT_FAILURE;
  }

  if (scalability_mode_string.empty()) {
    RTC_LOG(LS_ERROR) << "Scalability mode is empty";
    return EXIT_FAILURE;
  }

  // Check if the format is supported.
  bool is_supported = test_video_encoder_factory_wrapper->QueryCodecSupport(
      video_codec_string, scalability_mode_string);
  if (!is_supported) {
    RTC_LOG(LS_ERROR) << "Not supported format: video codec "
                      << video_codec_string << ", scalability_mode "
                      << scalability_mode_string;
    return EXIT_FAILURE;
  }

  // Create `FrameGeneratorInterface`.
  if (!y4m_input_file.empty() && !ivf_input_file.empty()) {
    RTC_LOG(LS_ERROR)
        << "Can not specify both '--y4m_input_file' and '--ivf_input_file'";
    return EXIT_FAILURE;
  }

  std::unique_ptr<webrtc::test::FrameGeneratorInterface> frame_buffer_generator;
  if (!y4m_input_file.empty()) {
    // Use `Y4mFrameGenerator` if specify `--y4m_input_file`.
    frame_buffer_generator = std::make_unique<webrtc::test::Y4mFrameGenerator>(
        y4m_input_file, webrtc::test::Y4mFrameGenerator::RepeatMode::kLoop);

    webrtc::test::FrameGeneratorInterface::Resolution resolution =
        frame_buffer_generator->GetResolution();
    if (resolution.width != width || resolution.height != height) {
      frame_buffer_generator->ChangeResolution(width, height);
    }

    RTC_LOG(LS_INFO) << "Create Y4mFrameGenerator: " << width << "x" << height;
  } else if (!ivf_input_file.empty()) {
    // Use `IvfFileFrameGenerator` if specify `--ivf_input_file`.
    frame_buffer_generator =
        webrtc::test::CreateFromIvfFileFrameGenerator(env, ivf_input_file);

    // Set width and height.
    webrtc::test::FrameGeneratorInterface::Resolution resolution =
        frame_buffer_generator->GetResolution();
    if (resolution.width != width || resolution.height != height) {
      frame_buffer_generator->ChangeResolution(width, height);
    }

    RTC_LOG(LS_INFO) << "CreateFromIvfFileFrameGenerator: " << ivf_input_file
                     << ", " << width << "x" << height;
  } else {
    RTC_CHECK_LE(raw_frame_generator, 1);

    if (raw_frame_generator == 0) {
      // Use `SquareFrameGenerator`.
      frame_buffer_generator = webrtc::test::CreateSquareFrameGenerator(
          width, height,
          webrtc::test::FrameGeneratorInterface::OutputType::kI420,
          absl::nullopt);
      RTC_CHECK(frame_buffer_generator);

      RTC_LOG(LS_INFO) << "CreateSquareFrameGenerator: " << width << "x"
                       << height;
    } else {
      // Use `SlideFrameGenerator`.
      const int kFrameRepeatCount = frame_rate_fps;
      frame_buffer_generator = webrtc::test::CreateSlideFrameGenerator(
          width, height, kFrameRepeatCount);
      RTC_CHECK(frame_buffer_generator);

      RTC_LOG(LS_INFO) << "CreateSlideFrameGenerator: " << width << "x"
                       << height << ", frame_repeat_count "
                       << kFrameRepeatCount;
    }
  }

  RTC_LOG(LS_INFO) << "Create video encoder, video codec " << video_codec_string
                   << ", scalability mode " << scalability_mode_string << ", "
                   << width << "x" << height << ", frame rate "
                   << frame_rate_fps << ", bitrate_kbps " << bitrate_kbps
                   << ", key frame interval " << key_frame_interval
                   << ", frames " << maximum_number_of_frames;

  // Create and initialize video encoder.
  webrtc::VideoCodec video_codec_setting =
      test_video_encoder_factory_wrapper->CreateVideoCodec(
          video_codec_string, scalability_mode_string, width, height,
          frame_rate_fps, bitrate_kbps);

  std::unique_ptr<webrtc::VideoEncoder> video_encoder =
      test_video_encoder_factory_wrapper->CreateAndInitializeVideoEncoder(
          env, video_codec_setting);
  RTC_CHECK(video_encoder);

  // Create `TestEncodedImageCallback`.
  std::unique_ptr<webrtc::TestEncodedImageCallback>
      test_encoded_image_callback =
          std::make_unique<webrtc::TestEncodedImageCallback>(
              video_codec_setting);
  RTC_CHECK(test_encoded_image_callback);
  int ret = video_encoder->RegisterEncodeCompleteCallback(
      test_encoded_image_callback.get());
  RTC_CHECK_EQ(ret, WEBRTC_VIDEO_CODEC_OK);

  // Start to encode frames.
  const uint32_t kRtpTick = 90000 / frame_rate_fps;
  // `IvfFileWriter` expects non-zero timestamp for the first frame.
  uint32_t rtp_timestamp = 1;
  for (uint32_t i = 0; i < maximum_number_of_frames; ++i) {
    // Generate key frame for every `key_frame_interval`.
    std::vector<webrtc::VideoFrameType> frame_types = {
        (i % key_frame_interval) ? webrtc::VideoFrameType::kVideoFrameDelta
                                 : webrtc::VideoFrameType::kVideoFrameKey};
    webrtc::VideoFrame frame =
        webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(frame_buffer_generator->NextFrame().buffer)
            .set_rtp_timestamp(rtp_timestamp)
            .build();
    ret = video_encoder->Encode(frame, &frame_types);
    RTC_CHECK_EQ(ret, WEBRTC_VIDEO_CODEC_OK);

    rtp_timestamp += kRtpTick;
  }

  // Cleanup.
  video_encoder->Release();

  return EXIT_SUCCESS;
}
