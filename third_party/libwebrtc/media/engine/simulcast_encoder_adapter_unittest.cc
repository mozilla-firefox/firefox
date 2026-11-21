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

#include <array>
#include <memory>
#include <vector>

#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/field_trials_view.h"
#include "api/test/create_simulcast_test_fixture.h"
#include "api/test/simulcast_test_fixture.h"
#include "api/test/video/function_video_decoder_factory.h"
#include "api/test/video/function_video_encoder_factory.h"
#include "api/video/video_codec_constants.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "common_video/include/video_frame_buffer.h"
#include "media/base/media_constants.h"
#include "media/engine/internal_encoder_factory.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/utility/simulcast_test_fixture_impl.h"
#include "rtc_base/checks.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/scoped_key_value_config.h"

using ::testing::_;
using ::testing::Return;
using EncoderInfo = webrtc::VideoEncoder::EncoderInfo;
using FramerateFractions =
    absl::InlinedVector<uint8_t, webrtc::kMaxTemporalStreams>;

namespace webrtc {
namespace test {

namespace {

constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 720;

const VideoEncoder::Capabilities kCapabilities(false);
const VideoEncoder::Settings kSettings(kCapabilities, 1, 1200);

std::unique_ptr<SimulcastTestFixture> CreateSpecificSimulcastTestFixture(
    VideoEncoderFactory* internal_encoder_factory) {
  std::unique_ptr<VideoEncoderFactory> encoder_factory =
      std::make_unique<FunctionVideoEncoderFactory>(
          [internal_encoder_factory](const Environment& env,
                                     const SdpVideoFormat& format) {
            return std::make_unique<SimulcastEncoderAdapter>(
                env, internal_encoder_factory, nullptr, SdpVideoFormat::VP8());
          });
  std::unique_ptr<VideoDecoderFactory> decoder_factory =
      std::make_unique<FunctionVideoDecoderFactory>(
          [](const Environment& env, const SdpVideoFormat& format) {
            return CreateVp8Decoder(env);
          });
  return CreateSimulcastTestFixture(std::move(encoder_factory),
                                    std::move(decoder_factory),
                                    SdpVideoFormat::VP8());
}
}  // namespace

TEST(SimulcastEncoderAdapterSimulcastTest, TestKeyFrameRequestsOnAllStreams) {
  InternalEncoderFactory internal_encoder_factory;
  auto fixture = CreateSpecificSimulcastTestFixture(&internal_encoder_factory);
  fixture->TestKeyFrameRequestsOnAllStreams();
}

TEST(SimulcastEncoderAdapterSimulcastTest, TestPaddingAllStreams) {
  InternalEncoderFactory internal_encoder_factory;
  auto fixture = CreateSpecificSimulcastTestFixture(&internal_encoder_factory);
  fixture->TestPaddingAllStreams();
}

TEST(SimulcastEncoderAdapterSimulcastTest, TestPaddingTwoStreams) {
  InternalEncoderFactory internal_encoder_factory;
  auto fixture = CreateSpecificSimulcastTestFixture(&internal_encoder_factory);
  fixture->TestPaddingTwoStreams();
}

TEST(SimulcastEncoderAdapterSimulcastTest, TestPaddingTwoStreamsOneMaxedOut) {
  InternalEncoderFactory internal_encoder_factory;
  auto fixture = CreateSpecificSimulcastTestFixture(&internal_encoder_factory);
  fixture->TestPaddingTwoStreamsOneMaxedOut();
}

TEST(SimulcastEncoderAdapterSimulcastTest, TestPaddingOneStream) {
  InternalEncoderFactory internal_encoder_factory;
  auto fixture = CreateSpecificSimulcastTestFixture(&internal_encoder_factory);
  fixture->TestPaddingOneStream();
}

TEST(SimulcastEncoderAdapterSimulcastTest, TestPaddingOneStreamTwoMaxedOut) {
  InternalEncoderFactory internal_encoder_factory;
  auto fixture = CreateSpecificSimulcastTestFixture(&internal_encoder_factory);
  fixture->TestPaddingOneStreamTwoMaxedOut();
}

TEST(SimulcastEncoderAdapterSimulcastTest, TestSendAllStreams) {
  InternalEncoderFactory internal_encoder_factory;
  auto fixture = CreateSpecificSimulcastTestFixture(&internal_encoder_factory);
  fixture->TestSendAllStreams();
}

TEST(SimulcastEncoderAdapterSimulcastTest, TestDisablingStreams) {
  InternalEncoderFactory internal_encoder_factory;
  auto fixture = CreateSpecificSimulcastTestFixture(&internal_encoder_factory);
  fixture->TestDisablingStreams();
}

TEST(SimulcastEncoderAdapterSimulcastTest, TestActiveStreams) {
  InternalEncoderFactory internal_encoder_factory;
  auto fixture = CreateSpecificSimulcastTestFixture(&internal_encoder_factory);
  fixture->TestActiveStreams();
}

TEST(SimulcastEncoderAdapterSimulcastTest, TestSwitchingToOneStream) {
  InternalEncoderFactory internal_encoder_factory;
  auto fixture = CreateSpecificSimulcastTestFixture(&internal_encoder_factory);
  fixture->TestSwitchingToOneStream();
}

TEST(SimulcastEncoderAdapterSimulcastTest, TestSwitchingToOneOddStream) {
  InternalEncoderFactory internal_encoder_factory;
  auto fixture = CreateSpecificSimulcastTestFixture(&internal_encoder_factory);
  fixture->TestSwitchingToOneOddStream();
}

TEST(SimulcastEncoderAdapterSimulcastTest, TestStrideEncodeDecode) {
  InternalEncoderFactory internal_encoder_factory;
  auto fixture = CreateSpecificSimulcastTestFixture(&internal_encoder_factory);
  fixture->TestStrideEncodeDecode();
}

TEST(SimulcastEncoderAdapterSimulcastTest,
     TestSpatioTemporalLayers333PatternEncoder) {
  InternalEncoderFactory internal_encoder_factory;
  auto fixture = CreateSpecificSimulcastTestFixture(&internal_encoder_factory);
  fixture->TestSpatioTemporalLayers333PatternEncoder();
}

TEST(SimulcastEncoderAdapterSimulcastTest,
     TestSpatioTemporalLayers321PatternEncoder) {
  InternalEncoderFactory internal_encoder_factory;
  auto fixture = CreateSpecificSimulcastTestFixture(&internal_encoder_factory);
  fixture->TestSpatioTemporalLayers321PatternEncoder();
}

TEST(SimulcastEncoderAdapterSimulcastTest, TestDecodeWidthHeightSet) {
  InternalEncoderFactory internal_encoder_factory;
  auto fixture = CreateSpecificSimulcastTestFixture(&internal_encoder_factory);
  fixture->TestDecodeWidthHeightSet();
}

class MockVideoEncoder;

class MockVideoEncoderFactory : public VideoEncoderFactory {
 public:
  std::vector<SdpVideoFormat> GetSupportedFormats() const override;

  std::unique_ptr<VideoEncoder> Create(const Environment& env,
                                       const SdpVideoFormat& format) override;

  const std::vector<MockVideoEncoder*>& encoders() const;
  void SetEncoderNames(const std::vector<const char*>& encoder_names);
  void set_create_video_encode_return_nullptr(bool return_nullptr) {
    create_video_encoder_return_nullptr_ = return_nullptr;
  }
  void set_init_encode_return_value(int32_t value);
  void set_requested_resolution_alignments(
      std::vector<uint32_t> requested_resolution_alignments) {
    requested_resolution_alignments_ = requested_resolution_alignments;
  }
  void set_supports_simulcast(bool supports_simulcast) {
    supports_simulcast_ = supports_simulcast;
  }
  void set_resolution_bitrate_limits(
      std::vector<VideoEncoder::ResolutionBitrateLimits> limits) {
    resolution_bitrate_limits_ = limits;
  }

  void DestroyVideoEncoder(VideoEncoder* encoder);

 private:
  bool create_video_encoder_return_nullptr_ = false;
  int32_t init_encode_return_value_ = 0;
  std::vector<MockVideoEncoder*> encoders_;
  std::vector<const char*> encoder_names_;
  // Keep number of entries in sync with `kMaxSimulcastStreams`.
  std::vector<uint32_t> requested_resolution_alignments_ = {1, 1, 1};
  bool supports_simulcast_ = false;
  std::vector<VideoEncoder::ResolutionBitrateLimits> resolution_bitrate_limits_;
};

class MockVideoEncoder : public VideoEncoder {
 public:
  explicit MockVideoEncoder(MockVideoEncoderFactory* factory)
      : factory_(factory),
        scaling_settings_(VideoEncoder::ScalingSettings::kOff),
        video_format_("unknown"),
        callback_(nullptr) {}

  MOCK_METHOD(void,
              SetFecControllerOverride,
              (FecControllerOverride * fec_controller_override),
              (override));

  int32_t InitEncode(const VideoCodec* codecSettings,
                     const VideoEncoder::Settings& settings) override {
    codec_ = *codecSettings;
    return init_encode_return_value_;
  }

  MOCK_METHOD(int32_t,
              Encode,
              (const VideoFrame& inputImage,
               const std::vector<VideoFrameType>* frame_types),
              (override));

  int32_t RegisterEncodeCompleteCallback(
      EncodedImageCallback* callback) override {
    callback_ = callback;
    return 0;
  }

  MOCK_METHOD(int32_t, Release, (), (override));

  void SetRates(const RateControlParameters& parameters) {
    last_set_rates_ = parameters;
  }

  EncoderInfo GetEncoderInfo() const override {
    EncoderInfo info;
    info.supports_native_handle = supports_native_handle_;
    info.implementation_name = implementation_name_;
    info.scaling_settings = scaling_settings_;
    info.requested_resolution_alignment = requested_resolution_alignment_;
    info.apply_alignment_to_all_simulcast_layers =
        apply_alignment_to_all_simulcast_layers_;
    info.has_trusted_rate_controller = has_trusted_rate_controller_;
    info.is_hardware_accelerated = is_hardware_accelerated_;
    info.fps_allocation[0] = fps_allocation_;
    info.supports_simulcast = supports_simulcast_;
    info.is_qp_trusted = is_qp_trusted_;
    info.resolution_bitrate_limits = resolution_bitrate_limits;
    return info;
  }

  virtual ~MockVideoEncoder() { factory_->DestroyVideoEncoder(this); }

  const VideoCodec& codec() const { return codec_; }

  void SendEncodedImage(int width, int height) {
    // Sends a fake image of the given width/height.
    EncodedImage image;
    image._encodedWidth = width;
    image._encodedHeight = height;
    CodecSpecificInfo codec_specific_info;
    codec_specific_info.codecType = webrtc::kVideoCodecVP8;
    callback_->OnEncodedImage(image, &codec_specific_info);
  }

  void set_supports_native_handle(bool enabled) {
    supports_native_handle_ = enabled;
  }

  void set_implementation_name(const std::string& name) {
    implementation_name_ = name;
  }

  void set_init_encode_return_value(int32_t value) {
    init_encode_return_value_ = value;
  }

  void set_scaling_settings(const VideoEncoder::ScalingSettings& settings) {
    scaling_settings_ = settings;
  }

  void set_requested_resolution_alignment(
      uint32_t requested_resolution_alignment) {
    requested_resolution_alignment_ = requested_resolution_alignment;
  }

  void set_apply_alignment_to_all_simulcast_layers(bool apply) {
    apply_alignment_to_all_simulcast_layers_ = apply;
  }

  void set_has_trusted_rate_controller(bool trusted) {
    has_trusted_rate_controller_ = trusted;
  }

  void set_is_hardware_accelerated(bool is_hardware_accelerated) {
    is_hardware_accelerated_ = is_hardware_accelerated;
  }

  void set_fps_allocation(const FramerateFractions& fps_allocation) {
    fps_allocation_ = fps_allocation;
  }

  RateControlParameters last_set_rates() const { return last_set_rates_; }

  void set_supports_simulcast(bool supports_simulcast) {
    supports_simulcast_ = supports_simulcast;
  }

  void set_video_format(const SdpVideoFormat& video_format) {
    video_format_ = video_format;
  }

  void set_is_qp_trusted(absl::optional<bool> is_qp_trusted) {
    is_qp_trusted_ = is_qp_trusted;
  }

  void set_resolution_bitrate_limits(
      std::vector<VideoEncoder::ResolutionBitrateLimits> limits) {
    resolution_bitrate_limits = limits;
  }

  bool supports_simulcast() const { return supports_simulcast_; }

  SdpVideoFormat video_format() const { return video_format_; }

 private:
  MockVideoEncoderFactory* const factory_;
  bool supports_native_handle_ = false;
  std::string implementation_name_ = "unknown";
  VideoEncoder::ScalingSettings scaling_settings_;
  uint32_t requested_resolution_alignment_ = 1;
  bool apply_alignment_to_all_simulcast_layers_ = false;
  bool has_trusted_rate_controller_ = false;
  bool is_hardware_accelerated_ = false;
  int32_t init_encode_return_value_ = 0;
  VideoEncoder::RateControlParameters last_set_rates_;
  FramerateFractions fps_allocation_;
  bool supports_simulcast_ = false;
  absl::optional<bool> is_qp_trusted_;
  SdpVideoFormat video_format_;
  std::vector<VideoEncoder::ResolutionBitrateLimits> resolution_bitrate_limits;

  VideoCodec codec_;
  EncodedImageCallback* callback_;
};

std::vector<SdpVideoFormat> MockVideoEncoderFactory::GetSupportedFormats()
    const {
  return {SdpVideoFormat::VP8()};
}

std::unique_ptr<VideoEncoder> MockVideoEncoderFactory::Create(
    const Environment& env,
    const SdpVideoFormat& format) {
  if (create_video_encoder_return_nullptr_) {
    return nullptr;
  }

  auto encoder = std::make_unique<::testing::NiceMock<MockVideoEncoder>>(this);
  encoder->set_init_encode_return_value(init_encode_return_value_);
  const char* encoder_name = encoder_names_.empty()
                                 ? "codec_implementation_name"
                                 : encoder_names_[encoders_.size()];
  encoder->set_implementation_name(encoder_name);
  RTC_CHECK_LT(encoders_.size(), requested_resolution_alignments_.size());
  encoder->set_requested_resolution_alignment(
      requested_resolution_alignments_[encoders_.size()]);
  encoder->set_supports_simulcast(supports_simulcast_);
  encoder->set_video_format(format);
  encoder->set_resolution_bitrate_limits(resolution_bitrate_limits_);
  encoders_.push_back(encoder.get());
  return encoder;
}

void MockVideoEncoderFactory::DestroyVideoEncoder(VideoEncoder* encoder) {
  for (size_t i = 0; i < encoders_.size(); ++i) {
    if (encoders_[i] == encoder) {
      encoders_.erase(encoders_.begin() + i);
      break;
    }
  }
}

const std::vector<MockVideoEncoder*>& MockVideoEncoderFactory::encoders()
    const {
  return encoders_;
}
void MockVideoEncoderFactory::SetEncoderNames(
    const std::vector<const char*>& encoder_names) {
  encoder_names_ = encoder_names;
}
void MockVideoEncoderFactory::set_init_encode_return_value(int32_t value) {
  init_encode_return_value_ = value;
}

class TestSimulcastEncoderAdapterFakeHelper {
 public:
  explicit TestSimulcastEncoderAdapterFakeHelper(
      const Environment& env,
      bool use_fallback_factory,
      const SdpVideoFormat& video_format)
      : env_(env),
        fallback_factory_(use_fallback_factory
                              ? std::make_unique<MockVideoEncoderFactory>()
                              : nullptr),
        video_format_(video_format) {}

  std::unique_ptr<VideoEncoder> CreateMockEncoderAdapter() {
    return std::make_unique<SimulcastEncoderAdapter>(
        env_, &primary_factory_, fallback_factory_.get(), video_format_);
  }

  MockVideoEncoderFactory* factory() { return &primary_factory_; }
  MockVideoEncoderFactory* fallback_factory() {
    return fallback_factory_.get();
  }

 private:
  const Environment env_;
  MockVideoEncoderFactory primary_factory_;
  std::unique_ptr<MockVideoEncoderFactory> fallback_factory_;
  SdpVideoFormat video_format_;
};

static const int kTestTemporalLayerProfile[3] = {3, 2, 1};

class TestSimulcastEncoderAdapterFake : public ::testing::Test,
                                        public EncodedImageCallback {
 public:
  TestSimulcastEncoderAdapterFake() : use_fallback_factory_(false) {}

  virtual ~TestSimulcastEncoderAdapterFake() {
    if (adapter_) {
      adapter_->Release();
    }
  }

  void SetUp() override {
    helper_ = std::make_unique<TestSimulcastEncoderAdapterFakeHelper>(
        CreateEnvironment(&field_trials_), use_fallback_factory_,
        SdpVideoFormat("VP8", sdp_video_parameters_));
    adapter_ = helper_->CreateMockEncoderAdapter();
    last_encoded_image_width_ = absl::nullopt;
    last_encoded_image_height_ = absl::nullopt;
    last_encoded_image_simulcast_index_ = absl::nullopt;
  }

  void ReSetUp() {
    if (adapter_) {
      adapter_->Release();
      // `helper_` owns factories which `adapter_` needs to destroy encoders.
      // Release `adapter_` before `helper_` (released in SetUp()).
      adapter_.reset();
    }
    SetUp();
  }

  Result OnEncodedImage(const EncodedImage& encoded_image,
                        const CodecSpecificInfo* codec_specific_info) override {
    last_encoded_image_width_ = encoded_image._encodedWidth;
    last_encoded_image_height_ = encoded_image._encodedHeight;
    last_encoded_image_simulcast_index_ = encoded_image.SimulcastIndex();

    return Result(Result::OK, encoded_image.RtpTimestamp());
  }

  bool GetLastEncodedImageInfo(absl::optional<int>* out_width,
                               absl::optional<int>* out_height,
                               absl::optional<int>* out_simulcast_index) {
    if (!last_encoded_image_width_.has_value()) {
      return false;
    }
    *out_width = last_encoded_image_width_;
    *out_height = last_encoded_image_height_;
    *out_simulcast_index = last_encoded_image_simulcast_index_;
    return true;
  }

  void SetupCodec() { SetupCodec(/*active_streams=*/{true, true, true}); }

  void SetupCodec(std::vector<bool> active_streams) {
    SimulcastTestFixtureImpl::DefaultSettings(
        &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
        kVideoCodecVP8);
    ASSERT_LE(active_streams.size(), codec_.numberOfSimulcastStreams);
    codec_.numberOfSimulcastStreams = active_streams.size();
    for (size_t stream_idx = 0; stream_idx < kMaxSimulcastStreams;
         ++stream_idx) {
      if (stream_idx >= codec_.numberOfSimulcastStreams) {
        // Reset parameters of unspecified stream.
        codec_.simulcastStream[stream_idx] = {0};
      } else {
        codec_.simulcastStream[stream_idx].active = active_streams[stream_idx];
      }
    }
    rate_allocator_.reset(new SimulcastRateAllocator(codec_));
    EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
    adapter_->RegisterEncodeCompleteCallback(this);
  }

  void VerifyCodec(const VideoCodec& ref, int stream_index) {
    const VideoCodec& target =
        helper_->factory()->encoders()[stream_index]->codec();
    EXPECT_EQ(ref.codecType, target.codecType);
    EXPECT_EQ(ref.width, target.width);
    EXPECT_EQ(ref.height, target.height);
    EXPECT_EQ(ref.startBitrate, target.startBitrate);
    EXPECT_EQ(ref.maxBitrate, target.maxBitrate);
    EXPECT_EQ(ref.minBitrate, target.minBitrate);
    EXPECT_EQ(ref.maxFramerate, target.maxFramerate);
    EXPECT_EQ(ref.GetVideoEncoderComplexity(),
              target.GetVideoEncoderComplexity());
    EXPECT_EQ(ref.VP8().numberOfTemporalLayers,
              target.VP8().numberOfTemporalLayers);
    EXPECT_EQ(ref.VP8().denoisingOn, target.VP8().denoisingOn);
    EXPECT_EQ(ref.VP8().automaticResizeOn, target.VP8().automaticResizeOn);
    EXPECT_EQ(ref.GetFrameDropEnabled(), target.GetFrameDropEnabled());
    EXPECT_EQ(ref.VP8().keyFrameInterval, target.VP8().keyFrameInterval);
    EXPECT_EQ(ref.qpMax, target.qpMax);
    EXPECT_EQ(0, target.numberOfSimulcastStreams);
    EXPECT_EQ(ref.mode, target.mode);

    // No need to compare simulcastStream as numberOfSimulcastStreams should
    // always be 0.
  }

  void InitRefCodec(int stream_index,
                    VideoCodec* ref_codec,
                    bool reverse_layer_order = false) {
    *ref_codec = codec_;
    ref_codec->VP8()->numberOfTemporalLayers =
        kTestTemporalLayerProfile[reverse_layer_order ? 2 - stream_index
                                                      : stream_index];
    ref_codec->width = codec_.simulcastStream[stream_index].width;
    ref_codec->height = codec_.simulcastStream[stream_index].height;
    ref_codec->maxBitrate = codec_.simulcastStream[stream_index].maxBitrate;
    ref_codec->minBitrate = codec_.simulcastStream[stream_index].minBitrate;
    ref_codec->qpMax = codec_.simulcastStream[stream_index].qpMax;
  }

  void VerifyCodecSettings() {
    EXPECT_EQ(3u, helper_->factory()->encoders().size());
    VideoCodec ref_codec;

    // stream 0, the lowest resolution stream.
    InitRefCodec(0, &ref_codec);
    ref_codec.qpMax = 45;
    ref_codec.SetVideoEncoderComplexity(
        webrtc::VideoCodecComplexity::kComplexityHigher);
    ref_codec.VP8()->denoisingOn = false;
    ref_codec.startBitrate = 100;  // Should equal to the target bitrate.
    VerifyCodec(ref_codec, 0);

    // stream 1
    InitRefCodec(1, &ref_codec);
    ref_codec.VP8()->denoisingOn = false;
    // The start bitrate (300kbit) minus what we have for the lower layers
    // (100kbit).
    ref_codec.startBitrate = 200;
    VerifyCodec(ref_codec, 1);

    // stream 2, the biggest resolution stream.
    InitRefCodec(2, &ref_codec);
    // We don't have enough bits to send this, so the adapter should have
    // configured it to use the min bitrate for this layer (600kbit) but turn
    // off sending.
    ref_codec.startBitrate = 600;
    VerifyCodec(ref_codec, 2);
  }

 protected:
  std::unique_ptr<TestSimulcastEncoderAdapterFakeHelper> helper_;
  std::unique_ptr<VideoEncoder> adapter_;
  VideoCodec codec_;
  absl::optional<int> last_encoded_image_width_;
  absl::optional<int> last_encoded_image_height_;
  absl::optional<int> last_encoded_image_simulcast_index_;
  std::unique_ptr<SimulcastRateAllocator> rate_allocator_;
  bool use_fallback_factory_;
  CodecParameterMap sdp_video_parameters_;
  test::ScopedKeyValueConfig field_trials_;
};

TEST_F(TestSimulcastEncoderAdapterFake, InitEncode) {
  SetupCodec();
  VerifyCodecSettings();
}

TEST_F(TestSimulcastEncoderAdapterFake, ReleaseWithoutInitEncode) {
  EXPECT_EQ(0, adapter_->Release());
}

TEST_F(TestSimulcastEncoderAdapterFake, Reinit) {
  SetupCodec();
  EXPECT_EQ(0, adapter_->Release());

  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
}

TEST_F(TestSimulcastEncoderAdapterFake, EncodedCallbackForDifferentEncoders) {
  SetupCodec();

  // Set bitrates so that we send all layers.
  adapter_->SetRates(VideoEncoder::RateControlParameters(
      rate_allocator_->Allocate(VideoBitrateAllocationParameters(1200, 30)),
      30.0));

  // At this point, the simulcast encoder adapter should have 3 streams: HD,
  // quarter HD, and quarter quarter HD. We're going to mostly ignore the exact
  // resolutions, to test that the adapter forwards on the correct resolution
  // and simulcast index values, going only off the encoder that generates the
  // image.
  std::vector<MockVideoEncoder*> encoders = helper_->factory()->encoders();
  ASSERT_EQ(3u, encoders.size());
  encoders[0]->SendEncodedImage(1152, 704);
  absl::optional<int> width;
  absl::optional<int> height;
  absl::optional<int> simulcast_index;
  EXPECT_TRUE(GetLastEncodedImageInfo(&width, &height, &simulcast_index));
  ASSERT_TRUE(width.has_value());
  EXPECT_EQ(1152, width.value());
  ASSERT_TRUE(height.has_value());
  EXPECT_EQ(704, height.value());
  // SEA doesn't intercept frame encode complete callback for the lowest stream.
  EXPECT_FALSE(simulcast_index.has_value());

  encoders[1]->SendEncodedImage(300, 620);
  EXPECT_TRUE(GetLastEncodedImageInfo(&width, &height, &simulcast_index));
  ASSERT_TRUE(width.has_value());
  EXPECT_EQ(300, width.value());
  ASSERT_TRUE(height.has_value());
  EXPECT_EQ(620, height.value());
  ASSERT_TRUE(simulcast_index.has_value());
  EXPECT_EQ(1, simulcast_index.value());

  encoders[2]->SendEncodedImage(120, 240);
  EXPECT_TRUE(GetLastEncodedImageInfo(&width, &height, &simulcast_index));
  ASSERT_TRUE(width.has_value());
  EXPECT_EQ(120, width.value());
  ASSERT_TRUE(height.has_value());
  EXPECT_EQ(240, height.value());
  ASSERT_TRUE(simulcast_index.has_value());
  EXPECT_EQ(2, simulcast_index.value());
}

// This test verifies that the underlying encoders are reused, when the adapter
// is reinited with different number of simulcast streams. It further checks
// that the allocated encoders are reused in the same order as before, starting
// with the lowest stream.
TEST_F(TestSimulcastEncoderAdapterFake, ReusesEncodersInOrder) {
  // Set up common settings for three streams.
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  rate_allocator_.reset(new SimulcastRateAllocator(codec_));
  adapter_->RegisterEncodeCompleteCallback(this);
  const uint32_t target_bitrate =
      1000 * (codec_.simulcastStream[0].targetBitrate +
              codec_.simulcastStream[1].targetBitrate +
              codec_.simulcastStream[2].minBitrate);

  // Input data.
  rtc::scoped_refptr<VideoFrameBuffer> buffer(I420Buffer::Create(1280, 720));
  VideoFrame input_frame = VideoFrame::Builder()
                               .set_video_frame_buffer(buffer)
                               .set_rtp_timestamp(100)
                               .set_timestamp_ms(1000)
                               .set_rotation(kVideoRotation_180)
                               .build();
  std::vector<VideoFrameType> frame_types;

  // Encode with three streams.
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  VerifyCodecSettings();
  adapter_->SetRates(VideoEncoder::RateControlParameters(
      rate_allocator_->Allocate(
          VideoBitrateAllocationParameters(target_bitrate, 30)),
      30.0));

  std::vector<MockVideoEncoder*> original_encoders =
      helper_->factory()->encoders();
  ASSERT_EQ(3u, original_encoders.size());
  EXPECT_CALL(*original_encoders[0], Encode(_, _))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*original_encoders[1], Encode(_, _))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*original_encoders[2], Encode(_, _))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  frame_types.resize(3, VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(0, adapter_->Encode(input_frame, &frame_types));
  EXPECT_CALL(*original_encoders[0], Release())
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*original_encoders[1], Release())
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*original_encoders[2], Release())
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_EQ(0, adapter_->Release());

  // Encode with two streams.
  codec_.width /= 2;
  codec_.height /= 2;
  codec_.numberOfSimulcastStreams = 2;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  adapter_->SetRates(VideoEncoder::RateControlParameters(
      rate_allocator_->Allocate(
          VideoBitrateAllocationParameters(target_bitrate, 30)),
      30.0));
  std::vector<MockVideoEncoder*> new_encoders = helper_->factory()->encoders();
  ASSERT_EQ(2u, new_encoders.size());
  ASSERT_EQ(original_encoders[0], new_encoders[0]);
  EXPECT_CALL(*original_encoders[0], Encode(_, _))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  ASSERT_EQ(original_encoders[1], new_encoders[1]);
  EXPECT_CALL(*original_encoders[1], Encode(_, _))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  frame_types.resize(2, VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(0, adapter_->Encode(input_frame, &frame_types));
  EXPECT_CALL(*original_encoders[0], Release())
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*original_encoders[1], Release())
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_EQ(0, adapter_->Release());

  // Encode with single stream.
  codec_.width /= 2;
  codec_.height /= 2;
  codec_.numberOfSimulcastStreams = 1;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  adapter_->SetRates(VideoEncoder::RateControlParameters(
      rate_allocator_->Allocate(
          VideoBitrateAllocationParameters(target_bitrate, 30)),
      30.0));
  new_encoders = helper_->factory()->encoders();
  ASSERT_EQ(1u, new_encoders.size());
  ASSERT_EQ(original_encoders[0], new_encoders[0]);
  EXPECT_CALL(*original_encoders[0], Encode(_, _))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  frame_types.resize(1, VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(0, adapter_->Encode(input_frame, &frame_types));
  EXPECT_CALL(*original_encoders[0], Release())
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_EQ(0, adapter_->Release());

  // Encode with three streams, again.
  codec_.width *= 4;
  codec_.height *= 4;
  codec_.numberOfSimulcastStreams = 3;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  adapter_->SetRates(VideoEncoder::RateControlParameters(
      rate_allocator_->Allocate(
          VideoBitrateAllocationParameters(target_bitrate, 30)),
      30.0));
  new_encoders = helper_->factory()->encoders();
  ASSERT_EQ(3u, new_encoders.size());
  // The first encoder is reused.
  ASSERT_EQ(original_encoders[0], new_encoders[0]);
  EXPECT_CALL(*original_encoders[0], Encode(_, _))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  // The second and third encoders are new.
  EXPECT_CALL(*new_encoders[1], Encode(_, _))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*new_encoders[2], Encode(_, _))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  frame_types.resize(3, VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(0, adapter_->Encode(input_frame, &frame_types));
  EXPECT_CALL(*original_encoders[0], Release())
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*new_encoders[1], Release())
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*new_encoders[2], Release())
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_EQ(0, adapter_->Release());
}

TEST_F(TestSimulcastEncoderAdapterFake, DoesNotLeakEncoders) {
  SetupCodec();
  VerifyCodecSettings();

  EXPECT_EQ(3u, helper_->factory()->encoders().size());

  // The adapter should destroy all encoders it has allocated. Since
  // `helper_->factory()` is owned by `adapter_`, however, we need to rely on
  // lsan to find leaks here.
  EXPECT_EQ(0, adapter_->Release());
  adapter_.reset();
}

// This test verifies that an adapter reinit with the same codec settings as
// before does not change the underlying encoder codec settings.
TEST_F(TestSimulcastEncoderAdapterFake, ReinitDoesNotReorderEncoderSettings) {
  SetupCodec();
  VerifyCodecSettings();

  // Capture current codec settings.
  std::vector<MockVideoEncoder*> encoders = helper_->factory()->encoders();
  ASSERT_EQ(3u, encoders.size());
  std::array<VideoCodec, 3> codecs_before;
  for (int i = 0; i < 3; ++i) {
    codecs_before[i] = encoders[i]->codec();
  }

  // Reinitialize and verify that the new codec settings are the same.
  EXPECT_EQ(0, adapter_->Release());
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  for (int i = 0; i < 3; ++i) {
    const VideoCodec& codec_before = codecs_before[i];
    const VideoCodec& codec_after = encoders[i]->codec();

    // webrtc::VideoCodec does not implement operator==.
    EXPECT_EQ(codec_before.codecType, codec_after.codecType);
    EXPECT_EQ(codec_before.width, codec_after.width);
    EXPECT_EQ(codec_before.height, codec_after.height);
    EXPECT_EQ(codec_before.startBitrate, codec_after.startBitrate);
    EXPECT_EQ(codec_before.maxBitrate, codec_after.maxBitrate);
    EXPECT_EQ(codec_before.minBitrate, codec_after.minBitrate);
    EXPECT_EQ(codec_before.maxFramerate, codec_after.maxFramerate);
    EXPECT_EQ(codec_before.qpMax, codec_after.qpMax);
    EXPECT_EQ(codec_before.numberOfSimulcastStreams,
              codec_after.numberOfSimulcastStreams);
    EXPECT_EQ(codec_before.mode, codec_after.mode);
    EXPECT_EQ(codec_before.expect_encode_from_texture,
              codec_after.expect_encode_from_texture);
  }
}

// This test is similar to the one above, except that it tests the simulcastIdx
// from the CodecSpecificInfo that is connected to an encoded frame. The
// PayloadRouter demuxes the incoming encoded frames on different RTP modules
// using the simulcastIdx, so it's important that there is no corresponding
// encoder reordering in between adapter reinits as this would lead to PictureID
// discontinuities.
TEST_F(TestSimulcastEncoderAdapterFake, ReinitDoesNotReorderFrameSimulcastIdx) {
  SetupCodec();
  adapter_->SetRates(VideoEncoder::RateControlParameters(
      rate_allocator_->Allocate(VideoBitrateAllocationParameters(1200, 30)),
      30.0));
  VerifyCodecSettings();

  // Send frames on all streams.
  std::vector<MockVideoEncoder*> encoders = helper_->factory()->encoders();
  ASSERT_EQ(3u, encoders.size());
  encoders[0]->SendEncodedImage(1152, 704);
  absl::optional<int> width;
  absl::optional<int> height;
  absl::optional<int> simulcast_index;
  EXPECT_TRUE(GetLastEncodedImageInfo(&width, &height, &simulcast_index));
  // SEA doesn't intercept frame encode complete callback for the lowest stream.
  EXPECT_FALSE(simulcast_index.has_value());

  encoders[1]->SendEncodedImage(300, 620);
  EXPECT_TRUE(GetLastEncodedImageInfo(&width, &height, &simulcast_index));
  ASSERT_TRUE(simulcast_index.has_value());
  EXPECT_EQ(1, simulcast_index.value());

  encoders[2]->SendEncodedImage(120, 240);
  EXPECT_TRUE(GetLastEncodedImageInfo(&width, &height, &simulcast_index));
  ASSERT_TRUE(simulcast_index.has_value());
  EXPECT_EQ(2, simulcast_index.value());

  // Reinitialize.
  EXPECT_EQ(0, adapter_->Release());
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  adapter_->SetRates(VideoEncoder::RateControlParameters(
      rate_allocator_->Allocate(VideoBitrateAllocationParameters(1200, 30)),
      30.0));

  // Verify that the same encoder sends out frames on the same simulcast index.
  encoders[0]->SendEncodedImage(1152, 704);
  EXPECT_TRUE(GetLastEncodedImageInfo(&width, &height, &simulcast_index));
  EXPECT_FALSE(simulcast_index.has_value());

  encoders[1]->SendEncodedImage(300, 620);
  EXPECT_TRUE(GetLastEncodedImageInfo(&width, &height, &simulcast_index));
  ASSERT_TRUE(simulcast_index.has_value());
  EXPECT_EQ(1, simulcast_index.value());

  encoders[2]->SendEncodedImage(120, 240);
  EXPECT_TRUE(GetLastEncodedImageInfo(&width, &height, &simulcast_index));
  ASSERT_TRUE(simulcast_index.has_value());
  EXPECT_EQ(2, simulcast_index.value());
}

TEST_F(TestSimulcastEncoderAdapterFake, SupportsNativeHandleForSingleStreams) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 1;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  adapter_->RegisterEncodeCompleteCallback(this);
  ASSERT_EQ(1u, helper_->factory()->encoders().size());
  helper_->factory()->encoders()[0]->set_supports_native_handle(true);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_TRUE(adapter_->GetEncoderInfo().supports_native_handle);
  helper_->factory()->encoders()[0]->set_supports_native_handle(false);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_FALSE(adapter_->GetEncoderInfo().supports_native_handle);
}

TEST_F(TestSimulcastEncoderAdapterFake, SetRatesUnderMinBitrate) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.minBitrate = 50;
  codec_.numberOfSimulcastStreams = 1;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  rate_allocator_.reset(new SimulcastRateAllocator(codec_));

  // Above min should be respected.
  VideoBitrateAllocation target_bitrate = rate_allocator_->Allocate(
      VideoBitrateAllocationParameters(codec_.minBitrate * 1000, 30));
  adapter_->SetRates(VideoEncoder::RateControlParameters(target_bitrate, 30.0));
  EXPECT_EQ(target_bitrate,
            helper_->factory()->encoders()[0]->last_set_rates().bitrate);

  // Below min but non-zero should be replaced with the min bitrate.
  VideoBitrateAllocation too_low_bitrate = rate_allocator_->Allocate(
      VideoBitrateAllocationParameters((codec_.minBitrate - 1) * 1000, 30));
  adapter_->SetRates(
      VideoEncoder::RateControlParameters(too_low_bitrate, 30.0));
  EXPECT_EQ(target_bitrate,
            helper_->factory()->encoders()[0]->last_set_rates().bitrate);

  // Zero should be passed on as is, since it means "pause".
  adapter_->SetRates(
      VideoEncoder::RateControlParameters(VideoBitrateAllocation(), 30.0));
  EXPECT_EQ(VideoBitrateAllocation(),
            helper_->factory()->encoders()[0]->last_set_rates().bitrate);
}

TEST_F(TestSimulcastEncoderAdapterFake, SupportsImplementationName) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  std::vector<const char*> encoder_names;
  encoder_names.push_back("codec1");
  encoder_names.push_back("codec2");
  encoder_names.push_back("codec3");
  helper_->factory()->SetEncoderNames(encoder_names);
  EXPECT_EQ("SimulcastEncoderAdapter",
            adapter_->GetEncoderInfo().implementation_name);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_EQ("SimulcastEncoderAdapter (codec1, codec2, codec3)",
            adapter_->GetEncoderInfo().implementation_name);

  // Single streams should not expose "SimulcastEncoderAdapter" in name.
  EXPECT_EQ(0, adapter_->Release());
  codec_.numberOfSimulcastStreams = 1;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  adapter_->RegisterEncodeCompleteCallback(this);
  ASSERT_EQ(1u, helper_->factory()->encoders().size());
  EXPECT_EQ("codec1", adapter_->GetEncoderInfo().implementation_name);
}

TEST_F(TestSimulcastEncoderAdapterFake, RuntimeEncoderInfoUpdate) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  std::vector<const char*> encoder_names;
  encoder_names.push_back("codec1");
  encoder_names.push_back("codec2");
  encoder_names.push_back("codec3");
  helper_->factory()->SetEncoderNames(encoder_names);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_EQ("SimulcastEncoderAdapter (codec1, codec2, codec3)",
            adapter_->GetEncoderInfo().implementation_name);

  // Change name of first encoder to indicate it has done a fallback to another
  // implementation.
  helper_->factory()->encoders().front()->set_implementation_name("fallback1");
  EXPECT_EQ("SimulcastEncoderAdapter (fallback1, codec2, codec3)",
            adapter_->GetEncoderInfo().implementation_name);
}

TEST_F(TestSimulcastEncoderAdapterFake,
       SupportsNativeHandleForMultipleStreams) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  adapter_->RegisterEncodeCompleteCallback(this);
  ASSERT_EQ(3u, helper_->factory()->encoders().size());
  for (MockVideoEncoder* encoder : helper_->factory()->encoders())
    encoder->set_supports_native_handle(true);
  // As long as one encoder supports native handle, it's enabled.
  helper_->factory()->encoders()[0]->set_supports_native_handle(false);
  EXPECT_TRUE(adapter_->GetEncoderInfo().supports_native_handle);
  // Once none do, then the adapter claims no support.
  helper_->factory()->encoders()[1]->set_supports_native_handle(false);
  helper_->factory()->encoders()[2]->set_supports_native_handle(false);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_FALSE(adapter_->GetEncoderInfo().supports_native_handle);
}

class FakeNativeBufferI420 : public VideoFrameBuffer {
 public:
  FakeNativeBufferI420(int width, int height, bool allow_to_i420)
      : width_(width), height_(height), allow_to_i420_(allow_to_i420) {}

  Type type() const override { return Type::kNative; }
  int width() const override { return width_; }
  int height() const override { return height_; }

  rtc::scoped_refptr<I420BufferInterface> ToI420() override {
    if (allow_to_i420_) {
      return I420Buffer::Create(width_, height_);
    } else {
      RTC_DCHECK_NOTREACHED();
    }
    return nullptr;
  }

 private:
  const int width_;
  const int height_;
  const bool allow_to_i420_;
};

TEST_F(TestSimulcastEncoderAdapterFake,
       NativeHandleForwardingForMultipleStreams) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;
  // High start bitrate, so all streams are enabled.
  codec_.startBitrate = 3000;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  adapter_->RegisterEncodeCompleteCallback(this);
  ASSERT_EQ(3u, helper_->factory()->encoders().size());
  for (MockVideoEncoder* encoder : helper_->factory()->encoders())
    encoder->set_supports_native_handle(true);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_TRUE(adapter_->GetEncoderInfo().supports_native_handle);

  rtc::scoped_refptr<VideoFrameBuffer> buffer(
      rtc::make_ref_counted<FakeNativeBufferI420>(1280, 720,
                                                  /*allow_to_i420=*/false));
  VideoFrame input_frame = VideoFrame::Builder()
                               .set_video_frame_buffer(buffer)
                               .set_rtp_timestamp(100)
                               .set_timestamp_ms(1000)
                               .set_rotation(kVideoRotation_180)
                               .build();
  // Expect calls with the given video frame verbatim, since it's a texture
  // frame and can't otherwise be modified/resized.
  for (MockVideoEncoder* encoder : helper_->factory()->encoders())
    EXPECT_CALL(*encoder, Encode(::testing::Ref(input_frame), _)).Times(1);
  std::vector<VideoFrameType> frame_types(3, VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(0, adapter_->Encode(input_frame, &frame_types));
}

TEST_F(TestSimulcastEncoderAdapterFake, NativeHandleForwardingOnlyIfSupported) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;
  // High start bitrate, so all streams are enabled.
  codec_.startBitrate = 3000;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  adapter_->RegisterEncodeCompleteCallback(this);
  ASSERT_EQ(3u, helper_->factory()->encoders().size());

  // QVGA encoders has fallen back to software.
  auto& encoders = helper_->factory()->encoders();
  encoders[0]->set_supports_native_handle(false);
  encoders[1]->set_supports_native_handle(true);
  encoders[2]->set_supports_native_handle(true);

  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_TRUE(adapter_->GetEncoderInfo().supports_native_handle);

  rtc::scoped_refptr<VideoFrameBuffer> buffer(
      rtc::make_ref_counted<FakeNativeBufferI420>(1280, 720,
                                                  /*allow_to_i420=*/true));
  VideoFrame input_frame = VideoFrame::Builder()
                               .set_video_frame_buffer(buffer)
                               .set_rtp_timestamp(100)
                               .set_timestamp_ms(1000)
                               .set_rotation(kVideoRotation_180)
                               .build();
  // Expect calls with the given video frame verbatim, since it's a texture
  // frame and can't otherwise be modified/resized, but only on the two
  // streams supporting it...
  EXPECT_CALL(*encoders[1], Encode(::testing::Ref(input_frame), _)).Times(1);
  EXPECT_CALL(*encoders[2], Encode(::testing::Ref(input_frame), _)).Times(1);
  // ...the lowest one gets a software buffer.
  EXPECT_CALL(*encoders[0], Encode)
      .WillOnce([&](const VideoFrame& frame,
                    const std::vector<VideoFrameType>* frame_types) {
        EXPECT_EQ(frame.video_frame_buffer()->type(),
                  VideoFrameBuffer::Type::kI420);
        return 0;
      });
  std::vector<VideoFrameType> frame_types(3, VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(0, adapter_->Encode(input_frame, &frame_types));
}

TEST_F(TestSimulcastEncoderAdapterFake, GeneratesKeyFramesOnRequestedLayers) {
  // Set up common settings for three streams.
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  rate_allocator_.reset(new SimulcastRateAllocator(codec_));
  adapter_->RegisterEncodeCompleteCallback(this);

  // Input data.
  rtc::scoped_refptr<VideoFrameBuffer> buffer(I420Buffer::Create(1280, 720));

  // Encode with three streams.
  codec_.startBitrate = 3000;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));

  std::vector<VideoFrameType> frame_types;
  frame_types.resize(3, VideoFrameType::kVideoFrameKey);

  std::vector<VideoFrameType> expected_keyframe(1,
                                                VideoFrameType::kVideoFrameKey);
  std::vector<VideoFrameType> expected_deltaframe(
      1, VideoFrameType::kVideoFrameDelta);

  std::vector<MockVideoEncoder*> original_encoders =
      helper_->factory()->encoders();
  ASSERT_EQ(3u, original_encoders.size());
  EXPECT_CALL(*original_encoders[0],
              Encode(_, ::testing::Pointee(::testing::Eq(expected_keyframe))))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*original_encoders[1],
              Encode(_, ::testing::Pointee(::testing::Eq(expected_keyframe))))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*original_encoders[2],
              Encode(_, ::testing::Pointee(::testing::Eq(expected_keyframe))))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  VideoFrame first_frame = VideoFrame::Builder()
                               .set_video_frame_buffer(buffer)
                               .set_rtp_timestamp(0)
                               .set_timestamp_ms(0)
                               .build();
  EXPECT_EQ(0, adapter_->Encode(first_frame, &frame_types));

  // Request [key, delta, delta].
  EXPECT_CALL(*original_encoders[0],
              Encode(_, ::testing::Pointee(::testing::Eq(expected_keyframe))))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*original_encoders[1],
              Encode(_, ::testing::Pointee(::testing::Eq(expected_deltaframe))))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*original_encoders[2],
              Encode(_, ::testing::Pointee(::testing::Eq(expected_deltaframe))))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  frame_types[1] = VideoFrameType::kVideoFrameKey;
  frame_types[1] = VideoFrameType::kVideoFrameDelta;
  frame_types[2] = VideoFrameType::kVideoFrameDelta;
  VideoFrame second_frame = VideoFrame::Builder()
                                .set_video_frame_buffer(buffer)
                                .set_rtp_timestamp(10000)
                                .set_timestamp_ms(100000)
                                .build();
  EXPECT_EQ(0, adapter_->Encode(second_frame, &frame_types));

  // Request [delta, key, delta].
  EXPECT_CALL(*original_encoders[0],
              Encode(_, ::testing::Pointee(::testing::Eq(expected_deltaframe))))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*original_encoders[1],
              Encode(_, ::testing::Pointee(::testing::Eq(expected_keyframe))))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*original_encoders[2],
              Encode(_, ::testing::Pointee(::testing::Eq(expected_deltaframe))))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  frame_types[0] = VideoFrameType::kVideoFrameDelta;
  frame_types[1] = VideoFrameType::kVideoFrameKey;
  frame_types[2] = VideoFrameType::kVideoFrameDelta;
  VideoFrame third_frame = VideoFrame::Builder()
                               .set_video_frame_buffer(buffer)
                               .set_rtp_timestamp(20000)
                               .set_timestamp_ms(200000)
                               .build();
  EXPECT_EQ(0, adapter_->Encode(third_frame, &frame_types));
}

TEST_F(TestSimulcastEncoderAdapterFake, TestFailureReturnCodesFromEncodeCalls) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  adapter_->RegisterEncodeCompleteCallback(this);
  ASSERT_EQ(3u, helper_->factory()->encoders().size());
  // Tell the 2nd encoder to request software fallback.
  EXPECT_CALL(*helper_->factory()->encoders()[1], Encode(_, _))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE));

  // Send a fake frame and assert the return is software fallback.
  rtc::scoped_refptr<I420Buffer> input_buffer =
      I420Buffer::Create(kDefaultWidth, kDefaultHeight);
  input_buffer->InitializeData();
  VideoFrame input_frame = VideoFrame::Builder()
                               .set_video_frame_buffer(input_buffer)
                               .set_rtp_timestamp(0)
                               .set_timestamp_us(0)
                               .set_rotation(kVideoRotation_0)
                               .build();
  std::vector<VideoFrameType> frame_types(3, VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE,
            adapter_->Encode(input_frame, &frame_types));
}

TEST_F(TestSimulcastEncoderAdapterFake, TestInitFailureCleansUpEncoders) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;
  helper_->factory()->set_init_encode_return_value(
      WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE);
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE,
            adapter_->InitEncode(&codec_, kSettings));
  EXPECT_TRUE(helper_->factory()->encoders().empty());
}

TEST_F(TestSimulcastEncoderAdapterFake, DoesNotAlterMaxQpForScreenshare) {
  const int kHighMaxQp = 56;
  const int kLowMaxQp = 46;

  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;
  codec_.simulcastStream[0].qpMax = kHighMaxQp;
  codec_.mode = VideoCodecMode::kScreensharing;

  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_EQ(3u, helper_->factory()->encoders().size());

  // Just check the lowest stream, which is the one that where the adapter
  // might alter the max qp setting.
  VideoCodec ref_codec;
  InitRefCodec(0, &ref_codec);
  ref_codec.qpMax = kHighMaxQp;
  ref_codec.SetVideoEncoderComplexity(
      webrtc::VideoCodecComplexity::kComplexityHigher);
  ref_codec.VP8()->denoisingOn = false;
  ref_codec.startBitrate = 100;  // Should equal to the target bitrate.
  VerifyCodec(ref_codec, 0);

  // Change the max qp and try again.
  codec_.simulcastStream[0].qpMax = kLowMaxQp;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_EQ(3u, helper_->factory()->encoders().size());
  ref_codec.qpMax = kLowMaxQp;
  VerifyCodec(ref_codec, 0);
}

TEST_F(TestSimulcastEncoderAdapterFake,
       DoesNotAlterMaxQpForScreenshareReversedLayer) {
  const int kHighMaxQp = 56;
  const int kLowMaxQp = 46;

  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8, true /* reverse_layer_order */);
  codec_.numberOfSimulcastStreams = 3;
  codec_.simulcastStream[2].qpMax = kHighMaxQp;
  codec_.mode = VideoCodecMode::kScreensharing;

  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_EQ(3u, helper_->factory()->encoders().size());

  // Just check the lowest stream, which is the one that where the adapter
  // might alter the max qp setting.
  VideoCodec ref_codec;
  InitRefCodec(2, &ref_codec, true /* reverse_layer_order */);
  ref_codec.qpMax = kHighMaxQp;
  ref_codec.SetVideoEncoderComplexity(
      webrtc::VideoCodecComplexity::kComplexityHigher);
  ref_codec.VP8()->denoisingOn = false;
  ref_codec.startBitrate = 100;  // Should equal to the target bitrate.
  VerifyCodec(ref_codec, 2);

  // Change the max qp and try again.
  codec_.simulcastStream[2].qpMax = kLowMaxQp;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_EQ(3u, helper_->factory()->encoders().size());
  ref_codec.qpMax = kLowMaxQp;
  VerifyCodec(ref_codec, 2);
}

TEST_F(TestSimulcastEncoderAdapterFake, ActivatesCorrectStreamsInInitEncode) {
  // Set up common settings for three streams.
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  rate_allocator_.reset(new SimulcastRateAllocator(codec_));
  adapter_->RegisterEncodeCompleteCallback(this);

  // Only enough start bitrate for the lowest stream.
  ASSERT_EQ(3u, codec_.numberOfSimulcastStreams);
  codec_.startBitrate = codec_.simulcastStream[0].targetBitrate +
                        codec_.simulcastStream[1].minBitrate - 1;

  // Input data.
  rtc::scoped_refptr<VideoFrameBuffer> buffer(I420Buffer::Create(1280, 720));
  VideoFrame input_frame = VideoFrame::Builder()
                               .set_video_frame_buffer(buffer)
                               .set_rtp_timestamp(100)
                               .set_timestamp_ms(1000)
                               .set_rotation(kVideoRotation_180)
                               .build();

  // Encode with three streams.
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  std::vector<MockVideoEncoder*> original_encoders =
      helper_->factory()->encoders();
  ASSERT_EQ(3u, original_encoders.size());
  // Only first encoder will be active and called.
  EXPECT_CALL(*original_encoders[0], Encode(_, _))
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*original_encoders[1], Encode(_, _)).Times(0);
  EXPECT_CALL(*original_encoders[2], Encode(_, _)).Times(0);

  std::vector<VideoFrameType> frame_types;
  frame_types.resize(3, VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(0, adapter_->Encode(input_frame, &frame_types));
}

TEST_F(TestSimulcastEncoderAdapterFake, TrustedRateControl) {
  // Set up common settings for three streams.
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  rate_allocator_.reset(new SimulcastRateAllocator(codec_));
  adapter_->RegisterEncodeCompleteCallback(this);

  // Only enough start bitrate for the lowest stream.
  ASSERT_EQ(3u, codec_.numberOfSimulcastStreams);
  codec_.startBitrate = codec_.simulcastStream[0].targetBitrate +
                        codec_.simulcastStream[1].minBitrate - 1;

  // Input data.
  rtc::scoped_refptr<VideoFrameBuffer> buffer(I420Buffer::Create(1280, 720));
  VideoFrame input_frame = VideoFrame::Builder()
                               .set_video_frame_buffer(buffer)
                               .set_rtp_timestamp(100)
                               .set_timestamp_ms(1000)
                               .set_rotation(kVideoRotation_180)
                               .build();

  // No encoder trusted, so simulcast adapter should not be either.
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_FALSE(adapter_->GetEncoderInfo().has_trusted_rate_controller);

  // Encode with three streams.
  std::vector<MockVideoEncoder*> original_encoders =
      helper_->factory()->encoders();

  // All encoders are trusted, so simulcast adapter should be too.
  original_encoders[0]->set_has_trusted_rate_controller(true);
  original_encoders[1]->set_has_trusted_rate_controller(true);
  original_encoders[2]->set_has_trusted_rate_controller(true);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_TRUE(adapter_->GetEncoderInfo().has_trusted_rate_controller);

  // One encoder not trusted, so simulcast adapter should not be either.
  original_encoders[2]->set_has_trusted_rate_controller(false);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_FALSE(adapter_->GetEncoderInfo().has_trusted_rate_controller);

  // No encoder trusted, so simulcast adapter should not be either.
  original_encoders[0]->set_has_trusted_rate_controller(false);
  original_encoders[1]->set_has_trusted_rate_controller(false);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_FALSE(adapter_->GetEncoderInfo().has_trusted_rate_controller);
}

TEST_F(TestSimulcastEncoderAdapterFake, ReportsHardwareAccelerated) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;
  adapter_->RegisterEncodeCompleteCallback(this);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  ASSERT_EQ(3u, helper_->factory()->encoders().size());

  // None of the encoders uses HW support, so simulcast adapter reports false.
  for (MockVideoEncoder* encoder : helper_->factory()->encoders()) {
    encoder->set_is_hardware_accelerated(false);
  }
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_FALSE(adapter_->GetEncoderInfo().is_hardware_accelerated);

  // One encoder uses HW support, so simulcast adapter reports true.
  helper_->factory()->encoders()[2]->set_is_hardware_accelerated(true);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_TRUE(adapter_->GetEncoderInfo().is_hardware_accelerated);
}

TEST_F(TestSimulcastEncoderAdapterFake,
       ReportsLeastCommonMultipleOfRequestedResolutionAlignments) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;
  helper_->factory()->set_requested_resolution_alignments({2, 4, 7});
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));

  EXPECT_EQ(adapter_->GetEncoderInfo().requested_resolution_alignment, 28u);
}

TEST_F(TestSimulcastEncoderAdapterFake,
       ReportsApplyAlignmentToSimulcastLayers) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;

  // No encoder has apply_alignment_to_all_simulcast_layers, report false.
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  ASSERT_EQ(3u, helper_->factory()->encoders().size());
  for (MockVideoEncoder* encoder : helper_->factory()->encoders()) {
    encoder->set_apply_alignment_to_all_simulcast_layers(false);
  }
  EXPECT_FALSE(
      adapter_->GetEncoderInfo().apply_alignment_to_all_simulcast_layers);

  // One encoder has apply_alignment_to_all_simulcast_layers, report true.
  helper_->factory()
      ->encoders()[1]
      ->set_apply_alignment_to_all_simulcast_layers(true);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_TRUE(
      adapter_->GetEncoderInfo().apply_alignment_to_all_simulcast_layers);
}

TEST_F(
    TestSimulcastEncoderAdapterFake,
    EncoderInfoFromFieldTrialDoesNotOverrideExistingBitrateLimitsInSinglecast) {
  test::ScopedKeyValueConfig field_trials(
      field_trials_,
      "WebRTC-SimulcastEncoderAdapter-GetEncoderInfoOverride/"
      "frame_size_pixels:123|456|789,"
      "min_start_bitrate_bps:11000|22000|33000,"
      "min_bitrate_bps:44000|55000|66000,"
      "max_bitrate_bps:77000|88000|99000/");
  SetUp();

  std::vector<VideoEncoder::ResolutionBitrateLimits> bitrate_limits;
  bitrate_limits.push_back(
      VideoEncoder::ResolutionBitrateLimits(111, 11100, 44400, 77700));
  bitrate_limits.push_back(
      VideoEncoder::ResolutionBitrateLimits(444, 22200, 55500, 88700));
  bitrate_limits.push_back(
      VideoEncoder::ResolutionBitrateLimits(777, 33300, 66600, 99900));
  SetUp();
  helper_->factory()->set_resolution_bitrate_limits(bitrate_limits);

  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 1;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  ASSERT_EQ(1u, helper_->factory()->encoders().size());
  EXPECT_EQ(adapter_->GetEncoderInfo().resolution_bitrate_limits,
            bitrate_limits);
}

TEST_F(TestSimulcastEncoderAdapterFake, EncoderInfoFromFieldTrial) {
  test::ScopedKeyValueConfig field_trials(
      field_trials_,
      "WebRTC-SimulcastEncoderAdapter-GetEncoderInfoOverride/"
      "requested_resolution_alignment:8,"
      "apply_alignment_to_all_simulcast_layers/");
  SetUp();
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  ASSERT_EQ(3u, helper_->factory()->encoders().size());

  EXPECT_EQ(8u, adapter_->GetEncoderInfo().requested_resolution_alignment);
  EXPECT_TRUE(
      adapter_->GetEncoderInfo().apply_alignment_to_all_simulcast_layers);
  EXPECT_TRUE(adapter_->GetEncoderInfo().resolution_bitrate_limits.empty());
}

TEST_F(TestSimulcastEncoderAdapterFake,
       EncoderInfoFromFieldTrialForSingleStream) {
  test::ScopedKeyValueConfig field_trials(
      field_trials_,
      "WebRTC-SimulcastEncoderAdapter-GetEncoderInfoOverride/"
      "requested_resolution_alignment:9,"
      "frame_size_pixels:123|456|789,"
      "min_start_bitrate_bps:11000|22000|33000,"
      "min_bitrate_bps:44000|55000|66000,"
      "max_bitrate_bps:77000|88000|99000/");
  SetUp();
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 1;
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  ASSERT_EQ(1u, helper_->factory()->encoders().size());

  EXPECT_EQ(9u, adapter_->GetEncoderInfo().requested_resolution_alignment);
  EXPECT_FALSE(
      adapter_->GetEncoderInfo().apply_alignment_to_all_simulcast_layers);
  EXPECT_THAT(
      adapter_->GetEncoderInfo().resolution_bitrate_limits,
      ::testing::ElementsAre(
          VideoEncoder::ResolutionBitrateLimits{123, 11000, 44000, 77000},
          VideoEncoder::ResolutionBitrateLimits{456, 22000, 55000, 88000},
          VideoEncoder::ResolutionBitrateLimits{789, 33000, 66000, 99000}));
}

TEST_F(TestSimulcastEncoderAdapterFake, ReportsIsQpTrusted) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;
  adapter_->RegisterEncodeCompleteCallback(this);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  ASSERT_EQ(3u, helper_->factory()->encoders().size());

  // All encoders have internal source, simulcast adapter reports true.
  for (MockVideoEncoder* encoder : helper_->factory()->encoders()) {
    encoder->set_is_qp_trusted(true);
  }
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_TRUE(adapter_->GetEncoderInfo().is_qp_trusted.value_or(false));

  // One encoder reports QP not trusted, simulcast adapter reports false.
  helper_->factory()->encoders()[2]->set_is_qp_trusted(false);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_FALSE(adapter_->GetEncoderInfo().is_qp_trusted.value_or(true));
}

TEST_F(TestSimulcastEncoderAdapterFake, ReportsFpsAllocation) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;
  adapter_->RegisterEncodeCompleteCallback(this);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  ASSERT_EQ(3u, helper_->factory()->encoders().size());

  // Combination of three different supported mode:
  // Simulcast stream 0 has undefined fps behavior.
  // Simulcast stream 1 has three temporal layers.
  // Simulcast stream 2 has 1 temporal layer.
  FramerateFractions expected_fps_allocation[kMaxSpatialLayers];
  expected_fps_allocation[1].push_back(EncoderInfo::kMaxFramerateFraction / 4);
  expected_fps_allocation[1].push_back(EncoderInfo::kMaxFramerateFraction / 2);
  expected_fps_allocation[1].push_back(EncoderInfo::kMaxFramerateFraction);
  expected_fps_allocation[2].push_back(EncoderInfo::kMaxFramerateFraction);

  // All encoders have internal source, simulcast adapter reports true.
  for (size_t i = 0; i < codec_.numberOfSimulcastStreams; ++i) {
    MockVideoEncoder* encoder = helper_->factory()->encoders()[i];
    encoder->set_fps_allocation(expected_fps_allocation[i]);
  }
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  EXPECT_THAT(adapter_->GetEncoderInfo().fps_allocation,
              ::testing::ElementsAreArray(expected_fps_allocation));
}

TEST_F(TestSimulcastEncoderAdapterFake, SetRateDistributesBandwithAllocation) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;
  const DataRate target_bitrate =
      DataRate::KilobitsPerSec(codec_.simulcastStream[0].targetBitrate +
                               codec_.simulcastStream[1].targetBitrate +
                               codec_.simulcastStream[2].minBitrate);
  const DataRate bandwidth_allocation =
      target_bitrate + DataRate::KilobitsPerSec(600);

  rate_allocator_.reset(new SimulcastRateAllocator(codec_));
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  adapter_->RegisterEncodeCompleteCallback(this);

  // Set bitrates so that we send all layers.
  adapter_->SetRates(VideoEncoder::RateControlParameters(
      rate_allocator_->Allocate(
          VideoBitrateAllocationParameters(target_bitrate.bps(), 30)),
      30.0, bandwidth_allocation));

  std::vector<MockVideoEncoder*> encoders = helper_->factory()->encoders();

  ASSERT_EQ(3u, encoders.size());

  for (size_t i = 0; i < 3; ++i) {
    const uint32_t layer_bitrate_bps =
        (i < static_cast<size_t>(codec_.numberOfSimulcastStreams) - 1
             ? codec_.simulcastStream[i].targetBitrate
             : codec_.simulcastStream[i].minBitrate) *
        1000;
    EXPECT_EQ(layer_bitrate_bps,
              encoders[i]->last_set_rates().bitrate.get_sum_bps())
        << i;
    EXPECT_EQ(
        (layer_bitrate_bps * bandwidth_allocation.bps()) / target_bitrate.bps(),
        encoders[i]->last_set_rates().bandwidth_allocation.bps())
        << i;
  }
}

TEST_F(TestSimulcastEncoderAdapterFake, CanSetZeroBitrateWithHeadroom) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;

  rate_allocator_.reset(new SimulcastRateAllocator(codec_));
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  adapter_->RegisterEncodeCompleteCallback(this);

  // Set allocated bitrate to 0, but keep (network) bandwidth allocation.
  VideoEncoder::RateControlParameters rate_params;
  rate_params.framerate_fps = 30;
  rate_params.bandwidth_allocation = DataRate::KilobitsPerSec(600);

  adapter_->SetRates(rate_params);

  std::vector<MockVideoEncoder*> encoders = helper_->factory()->encoders();

  ASSERT_EQ(3u, encoders.size());
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_EQ(0u, encoders[i]->last_set_rates().bitrate.get_sum_bps());
  }
}

TEST_F(TestSimulcastEncoderAdapterFake, SupportsSimulcast) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;

  // Indicate that mock encoders internally support simulcast.
  helper_->factory()->set_supports_simulcast(true);
  adapter_->RegisterEncodeCompleteCallback(this);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));

  // Only one encoder should have been produced.
  ASSERT_EQ(1u, helper_->factory()->encoders().size());

  rtc::scoped_refptr<VideoFrameBuffer> buffer(I420Buffer::Create(1280, 720));
  VideoFrame input_frame = VideoFrame::Builder()
                               .set_video_frame_buffer(buffer)
                               .set_rtp_timestamp(100)
                               .set_timestamp_ms(1000)
                               .set_rotation(kVideoRotation_180)
                               .build();
  EXPECT_CALL(*helper_->factory()->encoders()[0], Encode)
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  std::vector<VideoFrameType> frame_types(3, VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(0, adapter_->Encode(input_frame, &frame_types));
}

TEST_F(TestSimulcastEncoderAdapterFake, PassesSdpVideoFormatToEncoder) {
  sdp_video_parameters_ = {{"test_param", "test_value"}};
  SetUp();
  SetupCodec();
  std::vector<MockVideoEncoder*> encoders = helper_->factory()->encoders();
  ASSERT_GT(encoders.size(), 0u);
  EXPECT_EQ(encoders[0]->video_format(),
            SdpVideoFormat("VP8", sdp_video_parameters_));
}

TEST_F(TestSimulcastEncoderAdapterFake, SupportsFallback) {
  // Enable support for fallback encoder factory and re-setup.
  use_fallback_factory_ = true;
  SetUp();

  SetupCodec();

  // Make sure we have bitrate for all layers.
  DataRate max_bitrate = DataRate::Zero();
  for (int i = 0; i < 3; ++i) {
    max_bitrate +=
        DataRate::KilobitsPerSec(codec_.simulcastStream[i].maxBitrate);
  }
  const auto rate_settings = VideoEncoder::RateControlParameters(
      rate_allocator_->Allocate(
          VideoBitrateAllocationParameters(max_bitrate.bps(), 30)),
      30.0, max_bitrate);
  adapter_->SetRates(rate_settings);

  std::vector<MockVideoEncoder*> primary_encoders =
      helper_->factory()->encoders();
  std::vector<MockVideoEncoder*> fallback_encoders =
      helper_->fallback_factory()->encoders();

  ASSERT_EQ(3u, primary_encoders.size());
  ASSERT_EQ(3u, fallback_encoders.size());

  // Create frame to test with.
  rtc::scoped_refptr<VideoFrameBuffer> buffer(I420Buffer::Create(1280, 720));
  VideoFrame input_frame = VideoFrame::Builder()
                               .set_video_frame_buffer(buffer)
                               .set_rtp_timestamp(100)
                               .set_timestamp_ms(1000)
                               .set_rotation(kVideoRotation_180)
                               .build();
  std::vector<VideoFrameType> frame_types(3, VideoFrameType::kVideoFrameKey);

  // All primary encoders used.
  for (auto codec : primary_encoders) {
    EXPECT_CALL(*codec, Encode).WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  }
  EXPECT_EQ(0, adapter_->Encode(input_frame, &frame_types));

  // Trigger fallback on first encoder.
  primary_encoders[0]->set_init_encode_return_value(
      WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  adapter_->SetRates(rate_settings);
  EXPECT_CALL(*fallback_encoders[0], Encode)
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*primary_encoders[1], Encode)
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*primary_encoders[2], Encode)
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_EQ(0, adapter_->Encode(input_frame, &frame_types));

  // Trigger fallback on all encoder.
  primary_encoders[1]->set_init_encode_return_value(
      WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE);
  primary_encoders[2]->set_init_encode_return_value(
      WEBRTC_VIDEO_CODEC_FALLBACK_SOFTWARE);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  adapter_->SetRates(rate_settings);
  EXPECT_CALL(*fallback_encoders[0], Encode)
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*fallback_encoders[1], Encode)
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_CALL(*fallback_encoders[2], Encode)
      .WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  EXPECT_EQ(0, adapter_->Encode(input_frame, &frame_types));

  // Return to primary encoders on all streams.
  for (int i = 0; i < 3; ++i) {
    primary_encoders[i]->set_init_encode_return_value(WEBRTC_VIDEO_CODEC_OK);
  }
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  adapter_->SetRates(rate_settings);
  for (auto codec : primary_encoders) {
    EXPECT_CALL(*codec, Encode).WillOnce(Return(WEBRTC_VIDEO_CODEC_OK));
  }
  EXPECT_EQ(0, adapter_->Encode(input_frame, &frame_types));
}

TEST_F(TestSimulcastEncoderAdapterFake, SupportsPerSimulcastLayerMaxFramerate) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;
  codec_.simulcastStream[0].maxFramerate = 60;
  codec_.simulcastStream[1].maxFramerate = 30;
  codec_.simulcastStream[2].maxFramerate = 10;

  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  ASSERT_EQ(3u, helper_->factory()->encoders().size());
  EXPECT_EQ(60u, helper_->factory()->encoders()[0]->codec().maxFramerate);
  EXPECT_EQ(30u, helper_->factory()->encoders()[1]->codec().maxFramerate);
  EXPECT_EQ(10u, helper_->factory()->encoders()[2]->codec().maxFramerate);
}

TEST_F(TestSimulcastEncoderAdapterFake, CreatesEncoderOnlyIfStreamIsActive) {
  // Legacy singlecast
  SetupCodec(/*active_streams=*/{});
  EXPECT_EQ(1u, helper_->factory()->encoders().size());

  // Simulcast-capable underlaying encoder
  ReSetUp();
  helper_->factory()->set_supports_simulcast(true);
  SetupCodec(/*active_streams=*/{true, true});
  EXPECT_EQ(1u, helper_->factory()->encoders().size());

  // Muti-encoder simulcast
  ReSetUp();
  helper_->factory()->set_supports_simulcast(false);
  SetupCodec(/*active_streams=*/{true, true});
  EXPECT_EQ(2u, helper_->factory()->encoders().size());

  // Singlecast via layers deactivation. Lowest layer is active.
  ReSetUp();
  helper_->factory()->set_supports_simulcast(false);
  SetupCodec(/*active_streams=*/{true, false});
  EXPECT_EQ(1u, helper_->factory()->encoders().size());

  // Singlecast via layers deactivation. Highest layer is active.
  ReSetUp();
  helper_->factory()->set_supports_simulcast(false);
  SetupCodec(/*active_streams=*/{false, true});
  EXPECT_EQ(1u, helper_->factory()->encoders().size());
}

TEST_F(TestSimulcastEncoderAdapterFake,
       RecreateEncoderIfPreferTemporalSupportIsEnabled) {
  // Normally SEA reuses encoders. But, when TL-based SW fallback is enabled,
  // the encoder which served the lowest stream should be recreated before it
  // can be used to process an upper layer and vice-versa.
  test::ScopedKeyValueConfig field_trials(
      field_trials_, "WebRTC-Video-PreferTemporalSupportOnBaseLayer/Enabled/");
  use_fallback_factory_ = true;
  ReSetUp();

  // Legacy singlecast
  SetupCodec(/*active_streams=*/{});
  ASSERT_EQ(1u, helper_->factory()->encoders().size());

  // Singlecast, the lowest stream is active. Encoder should be reused.
  MockVideoEncoder* prev_encoder = helper_->factory()->encoders()[0];
  SetupCodec(/*active_streams=*/{true, false});
  ASSERT_EQ(1u, helper_->factory()->encoders().size());
  EXPECT_EQ(helper_->factory()->encoders()[0], prev_encoder);

  // Singlecast, an upper stream is active. Encoder should be recreated.
  EXPECT_CALL(*prev_encoder, Release()).Times(1);
  SetupCodec(/*active_streams=*/{false, true});
  ASSERT_EQ(1u, helper_->factory()->encoders().size());
  EXPECT_NE(helper_->factory()->encoders()[0], prev_encoder);

  // Singlecast, the lowest stream is active. Encoder should be recreated.
  prev_encoder = helper_->factory()->encoders()[0];
  EXPECT_CALL(*prev_encoder, Release()).Times(1);
  SetupCodec(/*active_streams=*/{true, false});
  ASSERT_EQ(1u, helper_->factory()->encoders().size());
  EXPECT_NE(helper_->factory()->encoders()[0], prev_encoder);
}

TEST_F(TestSimulcastEncoderAdapterFake,
       UseFallbackEncoderIfCreatePrimaryEncoderFailed) {
  // Enable support for fallback encoder factory and re-setup.
  use_fallback_factory_ = true;
  SetUp();
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 1;
  helper_->factory()->SetEncoderNames({"primary"});
  helper_->fallback_factory()->SetEncoderNames({"fallback"});

  // Emulate failure at creating of primary encoder and verify that SEA switches
  // to fallback encoder.
  helper_->factory()->set_create_video_encode_return_nullptr(true);
  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  ASSERT_EQ(0u, helper_->factory()->encoders().size());
  ASSERT_EQ(1u, helper_->fallback_factory()->encoders().size());
  EXPECT_EQ("fallback", adapter_->GetEncoderInfo().implementation_name);
}

TEST_F(TestSimulcastEncoderAdapterFake,
       InitEncodeReturnsErrorIfEncoderCannotBeCreated) {
  // Enable support for fallback encoder factory and re-setup.
  use_fallback_factory_ = true;
  SetUp();
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 1;
  helper_->factory()->SetEncoderNames({"primary"});
  helper_->fallback_factory()->SetEncoderNames({"fallback"});

  // Emulate failure at creating of primary and fallback encoders and verify
  // that `InitEncode` returns an error.
  helper_->factory()->set_create_video_encode_return_nullptr(true);
  helper_->fallback_factory()->set_create_video_encode_return_nullptr(true);
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_MEMORY,
            adapter_->InitEncode(&codec_, kSettings));
}

TEST_F(TestSimulcastEncoderAdapterFake, PopulatesScalabilityModeOfSubcodecs) {
  SimulcastTestFixtureImpl::DefaultSettings(
      &codec_, static_cast<const int*>(kTestTemporalLayerProfile),
      kVideoCodecVP8);
  codec_.numberOfSimulcastStreams = 3;
  codec_.simulcastStream[0].numberOfTemporalLayers = 1;
  codec_.simulcastStream[1].numberOfTemporalLayers = 2;
  codec_.simulcastStream[2].numberOfTemporalLayers = 3;

  EXPECT_EQ(0, adapter_->InitEncode(&codec_, kSettings));
  ASSERT_EQ(3u, helper_->factory()->encoders().size());
  EXPECT_EQ(helper_->factory()->encoders()[0]->codec().GetScalabilityMode(),
            ScalabilityMode::kL1T1);
  EXPECT_EQ(helper_->factory()->encoders()[1]->codec().GetScalabilityMode(),
            ScalabilityMode::kL1T2);
  EXPECT_EQ(helper_->factory()->encoders()[2]->codec().GetScalabilityMode(),
            ScalabilityMode::kL1T3);
}

}  // namespace test
}  // namespace webrtc
