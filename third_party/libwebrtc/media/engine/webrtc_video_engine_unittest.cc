/*
 *  Copyright (c) 2004 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/engine/webrtc_video_engine.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/inlined_vector.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "api/call/transport.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/field_trials.h"
#include "api/make_ref_counted.h"
#include "api/media_types.h"
#include "api/payload_type.h"
#include "api/priority.h"
#include "api/rtc_error.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_headers.h"
#include "api/rtp_packet_infos.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/sframe/sframe_types.h"
#include "api/test/mock_encoder_selector.h"
#include "api/test/mock_video_bitrate_allocator.h"
#include "api/test/mock_video_bitrate_allocator_factory.h"
#include "api/test/mock_video_decoder_factory.h"
#include "api/test/mock_video_encoder_factory.h"
#include "api/test/video/function_video_decoder_factory.h"
#include "api/transport/bitrate_settings.h"
#include "api/transport/rtp/rtp_source.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/builtin_video_bitrate_allocator_factory.h"
#include "api/video/i420_buffer.h"
#include "api/video/recordable_encoded_frame.h"
#include "api/video/resolution.h"
#include "api/video/video_bitrate_allocator_factory.h"
#include "api/video/video_codec_constants.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_common.h"
#include "api/video/video_content_type.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_rotation.h"
#include "api/video/video_sink_interface.h"
#include "api/video/video_source_interface.h"
#include "api/video_codecs/h264_profile_level_id.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_decoder_factory_template.h"
#include "api/video_codecs/video_decoder_factory_template_dav1d_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_decoder_factory_template_open_h264_adapter.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "api/video_codecs/video_encoder_factory_template.h"
#include "api/video_codecs/video_encoder_factory_template_libaom_av1_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp8_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_libvpx_vp9_adapter.h"
#include "api/video_codecs/video_encoder_factory_template_open_h264_adapter.h"
#include "call/call.h"
#include "call/call_config.h"
#include "call/flexfec_receive_stream.h"
#include "call/sframe_options.h"
#include "call/video_receive_stream.h"
#include "call/video_send_stream.h"
#include "common_video/include/quality_limitation_reason.h"
#include "media/base/codec.h"
#include "media/base/fake_frame_source.h"
#include "media/base/fake_network_interface.h"
#include "media/base/fake_video_renderer.h"
#include "media/base/media_channel.h"
#include "media/base/media_config.h"
#include "media/base/media_constants.h"
#include "media/base/media_engine.h"
#include "media/base/rid_description.h"
#include "media/base/stream_params.h"
#include "media/base/test_utils.h"
#include "media/engine/fake_webrtc_call.h"
#include "media/engine/fake_webrtc_video_engine.h"
#include "modules/rtp_rtcp/include/report_block_data.h"
#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtcp_packet/report_block.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/source_tracker.h"
#include "modules/sframe/sframe_encryption_config.h"
#include "modules/sframe/sframe_encryptor.h"
#include "modules/video_coding/svc/scalability_mode_util.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/dscp.h"
#include "rtc_base/experiments/min_video_bitrate_experiment.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "rtc_base/socket.h"
#include "rtc_base/thread.h"
#include "test/create_test_environment.h"
#include "test/create_test_field_trials.h"
#include "test/fake_decoder.h"
#include "test/frame_forwarder.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/rtcp_packet_parser.h"
#include "test/run_loop.h"
#include "test/time_controller/simulated_time_controller.h"
#include "video/config/encoder_stream_factory.h"
#include "video/config/simulcast.h"
#include "video/config/video_encoder_config.h"

namespace webrtc {
namespace {

using ::testing::_;
using ::testing::Combine;
using ::testing::Contains;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::Lt;
using ::testing::Pair;
using ::testing::Return;
using ::testing::SizeIs;
using ::testing::StrNe;
using ::testing::Values;
using ::testing::WithArg;
// Types already in webrtc:
using test::FrameForwarder;
using test::FunctionVideoDecoderFactory;
using test::RtcpPacketParser;

constexpr uint8_t kRedRtxPayloadType = 125;

constexpr uint32_t kSsrc = 1234u;
constexpr uint32_t kSsrcs4[] = {1, 2, 3, 4};
constexpr int kVideoWidth = 640;
constexpr int kVideoHeight = 360;
constexpr int kFramerate = 30;
constexpr TimeDelta kFrameDuration = TimeDelta::Millis(1000 / kFramerate);

constexpr uint32_t kSsrcs1[] = {1};
constexpr uint32_t kSsrcs3[] = {1, 2, 3};
constexpr uint32_t kRtxSsrcs1[] = {4};
constexpr uint32_t kFlexfecSsrc = 5;
constexpr uint32_t kIncomingUnsignalledSsrc = 0xC0FFEE;
constexpr int64_t kUnsignalledReceiveStreamCooldownMs = 500;

constexpr uint32_t kRtpHeaderSize = 12;
constexpr size_t kNumSimulcastStreams = 3;

constexpr char kUnsupportedExtensionName[] =
    "urn:ietf:params:rtp-hdrext:unsupported";

void VerifyCodecHasDefaultFeedbackParams(const Codec& codec,
                                         bool lntf_expected) {
  EXPECT_EQ(lntf_expected, codec.HasFeedbackParam(FeedbackParam(
                               kRtcpFbParamLntf, kParamValueEmpty)));
  EXPECT_TRUE(codec.HasFeedbackParam(
      FeedbackParam(kRtcpFbParamNack, kParamValueEmpty)));
  EXPECT_TRUE(codec.HasFeedbackParam(
      FeedbackParam(kRtcpFbParamNack, kRtcpFbNackParamPli)));
  EXPECT_TRUE(codec.HasFeedbackParam(
      FeedbackParam(kRtcpFbParamRemb, kParamValueEmpty)));
  EXPECT_TRUE(codec.HasFeedbackParam(
      FeedbackParam(kRtcpFbParamTransportCc, kParamValueEmpty)));
  EXPECT_TRUE(codec.HasFeedbackParam(
      FeedbackParam(kRtcpFbParamCcm, kRtcpFbCcmParamFir)));
}

// Return true if any codec in `codecs` is an RTX codec with associated
// payload type `payload_type`.
bool HasRtxCodec(const std::vector<Codec>& codecs, int payload_type) {
  for (const Codec& codec : codecs) {
    int associated_payload_type;
    if (absl::EqualsIgnoreCase(codec.name.c_str(), "rtx") &&
        codec.GetParam(kCodecParamAssociatedPayloadType,
                       &associated_payload_type) &&
        associated_payload_type == payload_type) {
      return true;
    }
  }
  return false;
}

// Return true if any codec in `codecs` is an RTX codec, independent of
// payload type.
bool HasAnyRtxCodec(const std::vector<Codec>& codecs) {
  for (const Codec& codec : codecs) {
    if (absl::EqualsIgnoreCase(codec.name.c_str(), "rtx")) {
      return true;
    }
  }
  return false;
}

const int* FindKeyByValue(const std::map<int, int>& m, int v) {
  for (const auto& kv : m) {
    if (kv.second == v)
      return &kv.first;
  }
  return nullptr;
}

bool HasRtxReceiveAssociation(const VideoReceiveStreamInterface::Config& config,
                              int payload_type) {
  return FindKeyByValue(config.rtp.rtx_associated_payload_types,
                        payload_type) != nullptr;
}

// Check that there's an Rtx payload type for each decoder.
bool VerifyRtxReceiveAssociations(
    const VideoReceiveStreamInterface::Config& config) {
  for (const auto& decoder : config.decoders) {
    if (!HasRtxReceiveAssociation(config, decoder.payload_type))
      return false;
  }
  return true;
}

scoped_refptr<VideoFrameBuffer> CreateBlackFrameBuffer(int width, int height) {
  scoped_refptr<I420Buffer> buffer = I420Buffer::Create(width, height);
  I420Buffer::SetBlack(buffer.get());
  return buffer;
}

void VerifySendStreamHasRtxTypes(const VideoSendStream::Config& config,
                                 const std::map<int, int>& rtx_types) {
  std::map<int, int>::const_iterator it;
  it = rtx_types.find(config.rtp.payload_type);
  EXPECT_TRUE(it != rtx_types.end() &&
              it->second == config.rtp.rtx.payload_type);

  if (config.rtp.ulpfec.red_rtx_payload_type != -1) {
    it = rtx_types.find(config.rtp.ulpfec.red_payload_type);
    EXPECT_TRUE(it != rtx_types.end() &&
                it->second == config.rtp.ulpfec.red_rtx_payload_type);
  }
}

MediaConfig GetMediaConfig() {
  MediaConfig media_config;
  media_config.video.enable_cpu_adaptation = false;
  return media_config;
}

// Values from GetMaxDefaultVideoBitrateKbps in webrtcvideoengine.cc.
int GetMaxDefaultBitrateBps(size_t width, size_t height) {
  if (width * height <= 320 * 240) {
    return 600000;
  } else if (width * height <= 640 * 480) {
    return 1700000;
  } else if (width * height <= 960 * 540) {
    return 2000000;
  } else {
    return 2500000;
  }
}

class MockVideoSource : public VideoSourceInterface<VideoFrame> {
 public:
  MOCK_METHOD(void,
              AddOrUpdateSink,
              (VideoSinkInterface<VideoFrame> * sink,
               const VideoSinkWants& wants),
              (override));
  MOCK_METHOD(void,
              RemoveSink,
              (VideoSinkInterface<VideoFrame> * sink),
              (override));
};

class MockNetworkInterface : public MediaChannelNetworkInterface {
 public:
  MOCK_METHOD(bool,
              SendPacket,
              (CopyOnWriteBuffer * packet,
               const AsyncSocketPacketOptions& options),
              (override));
  MOCK_METHOD(bool,
              SendRtcp,
              (CopyOnWriteBuffer * packet,
               const AsyncSocketPacketOptions& options),
              (override));
  MOCK_METHOD(int,
              SetOption,
              (SocketType type, Socket::Option opt, int option),
              (override));
};

std::vector<Resolution> GetStreamResolutions(
    const std::vector<VideoStream>& streams) {
  std::vector<Resolution> res;
  for (const auto& s : streams) {
    if (s.active) {
      res.push_back({.width = checked_cast<int>(s.width),
                     .height = checked_cast<int>(s.height)});
    }
  }
  return res;
}

RtpPacketReceived BuildVp8KeyFrame(uint32_t ssrc, uint8_t payload_type) {
  RtpPacketReceived packet;
  packet.SetMarker(true);
  packet.SetPayloadType(payload_type);
  packet.SetSsrc(ssrc);

  // VP8 Keyframe + 1 byte payload
  uint8_t* buf_ptr = packet.AllocatePayload(11);
  memset(buf_ptr, 0, 11);  // Pass MSAN (don't care about bytes 1-9)
  buf_ptr[0] = 0x10;       // Partition ID 0 + beginning of partition.
  constexpr unsigned width = 1080;
  constexpr unsigned height = 720;
  buf_ptr[6] = width & 255;
  buf_ptr[7] = width >> 8;
  buf_ptr[8] = height & 255;
  buf_ptr[9] = height >> 8;
  return packet;
}

RtpPacketReceived BuildRtxPacket(uint32_t rtx_ssrc,
                                 uint8_t rtx_payload_type,
                                 const RtpPacketReceived& original_packet) {
  constexpr size_t kRtxHeaderSize = 2;
  RtpPacketReceived packet(original_packet);
  packet.SetPayloadType(rtx_payload_type);
  packet.SetSsrc(rtx_ssrc);

  uint8_t* rtx_payload =
      packet.AllocatePayload(original_packet.payload_size() + kRtxHeaderSize);
  // Add OSN (original sequence number).
  rtx_payload[0] = packet.SequenceNumber() >> 8;
  rtx_payload[1] = packet.SequenceNumber();

  // Add original payload data.
  if (!original_packet.payload().empty()) {
    memcpy(rtx_payload + kRtxHeaderSize, original_packet.payload().data(),
           original_packet.payload().size());
  }
  return packet;
}

// TODO(tommi): Consider replacing these macros with custom matchers.
#define EXPECT_FRAME(c, w, h)                      \
  EXPECT_EQ((c), renderer_.num_rendered_frames()); \
  EXPECT_EQ((w), renderer_.width());               \
  EXPECT_EQ((h), renderer_.height());

#define EXPECT_FRAME_ON_RENDERER(r, c, w, h) \
  EXPECT_EQ((c), (r).num_rendered_frames()); \
  EXPECT_EQ((w), (r).width());               \
  EXPECT_EQ((h), (r).height());

class WebRtcVideoEngineTest : public ::testing::Test {
 public:
  WebRtcVideoEngineTest() : WebRtcVideoEngineTest("") {}
  explicit WebRtcVideoEngineTest(const std::string& field_trials)
      : field_trials_(CreateTestFieldTrials(field_trials)),
        time_controller_(Timestamp::Millis(4711)),
        env_(CreateTestEnvironment({.field_trials = field_trials_.CreateCopy(),
                                    .time = &time_controller_})),
        call_(Call::Create(CallConfig::CreateSingleThreaded(env_))),
        encoder_factory_(new FakeWebRtcVideoEncoderFactory),
        decoder_factory_(new FakeWebRtcVideoDecoderFactory),
        video_bitrate_allocator_factory_(
            CreateBuiltinVideoBitrateAllocatorFactory()),
        engine_(std::make_unique<WebRtcVideoEngine>(
            std::unique_ptr<FakeWebRtcVideoEncoderFactory>(encoder_factory_),
            std::unique_ptr<FakeWebRtcVideoDecoderFactory>(decoder_factory_),
            env_.field_trials())) {}

 protected:
  void ChangeFieldTrials(absl::string_view key, absl::string_view value) {
    if (!key.empty()) {
      field_trials_.Set(key, value);
    }
    env_ = CreateTestEnvironment({.field_trials = field_trials_.CreateCopy(),
                                  .time = &time_controller_});
    encoder_factory_ = new FakeWebRtcVideoEncoderFactory();
    decoder_factory_ = new FakeWebRtcVideoDecoderFactory();
    engine_ = std::make_unique<WebRtcVideoEngine>(
        std::unique_ptr<FakeWebRtcVideoEncoderFactory>(encoder_factory_),
        std::unique_ptr<FakeWebRtcVideoDecoderFactory>(decoder_factory_),
        env_.field_trials());
  }
  void AssignDefaultAptRtxTypes();
  void AssignDefaultCodec();

  // Find the index of the codec in the engine with the given name. The codec
  // must be present.
  size_t GetEngineCodecIndex(const std::string& name) const;

  // Find the codec in the engine with the given name. The codec must be
  // present.
  Codec GetEngineCodec(const std::string& name) const;
  void AddSupportedVideoCodecType(
      const std::string& name,
      const std::vector<ScalabilityMode>& scalability_modes = {});
  void AddSupportedVideoCodec(SdpVideoFormat format);

  std::unique_ptr<VideoMediaSendChannelInterface>
  SetSendParamsWithAllSupportedCodecs();

  std::unique_ptr<VideoMediaReceiveChannelInterface>
  SetRecvParamsWithAllSupportedCodecs();
  std::unique_ptr<VideoMediaReceiveChannelInterface>
  SetRecvParamsWithSupportedCodecs(const std::vector<Codec>& codecs);

  void ExpectRtpCapabilitySupport(const char* uri, bool supported) const;

  FieldTrials field_trials_;
  GlobalSimulatedTimeController time_controller_;
  Environment env_;
  // Used in WebRtcVideoEngineVoiceTest, but defined here so it's properly
  // initialized when the constructor is called.
  std::unique_ptr<Call> call_;
  FakeWebRtcVideoEncoderFactory* encoder_factory_;
  FakeWebRtcVideoDecoderFactory* decoder_factory_;
  std::unique_ptr<VideoBitrateAllocatorFactory>
      video_bitrate_allocator_factory_;
  std::unique_ptr<WebRtcVideoEngine> engine_;
  std::optional<Codec> default_codec_;
  std::map<int, int> default_apt_rtx_types_;
};

TEST_F(WebRtcVideoEngineTest, DefaultRtxCodecHasAssociatedPayloadTypeSet) {
  encoder_factory_->AddSupportedVideoCodecType("VP8");
  AssignDefaultCodec();

  std::vector<Codec> engine_codecs = engine_->LegacySendCodecs();
  for (size_t i = 0; i < engine_codecs.size(); ++i) {
    if (engine_codecs[i].name != kRtxCodecName)
      continue;
    int associated_payload_type;
    EXPECT_TRUE(engine_codecs[i].GetParam(kCodecParamAssociatedPayloadType,
                                          &associated_payload_type));
    EXPECT_EQ(default_codec_->id, associated_payload_type);
    return;
  }
  FAIL() << "No RTX codec found among default codecs.";
}

// Test that we prefer to assign RTX payload types as "primary codec PT + 1".
// This is purely for backwards compatibility (see https://crbug.com/391132280).
// The spec does NOT mandate we do this and note that this is best-effort, if
// "PT + 1" is already in-use the PT suggester would pick a different PT.
TEST_F(WebRtcVideoEngineTest,
       DefaultRtxCodecIsAssignedAssociatedPayloadTypePlusOne) {
  AddSupportedVideoCodecType("VP8");
  AddSupportedVideoCodecType("VP9");
  AddSupportedVideoCodecType("AV1");
  AddSupportedVideoCodecType("H264");
  for (const Codec& codec : engine_->LegacySendCodecs()) {
    if (codec.name != kRtxCodecName)
      continue;
    int associated_payload_type;
    ASSERT_TRUE(codec.GetParam(kCodecParamAssociatedPayloadType,
                               &associated_payload_type));
    EXPECT_EQ(codec.id, associated_payload_type + 1);
  }
  for (const Codec& codec : engine_->LegacyRecvCodecs()) {
    if (codec.name != kRtxCodecName)
      continue;
    int associated_payload_type;
    ASSERT_TRUE(codec.GetParam(kCodecParamAssociatedPayloadType,
                               &associated_payload_type));
    EXPECT_EQ(codec.id, associated_payload_type + 1);
  }
}

MATCHER(HasUniquePtValues, "") {
  std::unordered_set<int> seen_ids;
  for (const auto& codec : arg) {
    if (seen_ids.count(codec.id) > 0) {
      *result_listener << "Duplicate id for " << absl::StrCat(codec);
      return false;
    }
    seen_ids.insert(codec.id);
  }
  return true;
}

TEST_F(WebRtcVideoEngineTest, SupportingTwoKindsOfVp9IsOk) {
  AddSupportedVideoCodecType("VP8");
  AddSupportedVideoCodec(SdpVideoFormat("VP9", {{"profile-id", "0"}}));
  AddSupportedVideoCodec(SdpVideoFormat("VP9", {{"profile-id", "1"}}));
  AddSupportedVideoCodec(SdpVideoFormat("VP9", {{"profile-id", "3"}}));
  AddSupportedVideoCodec(SdpVideoFormat(
      "AV1", {{"level-idx", "5"}, {"profile", "1"}, {"tier", "0"}}));
  AddSupportedVideoCodec(SdpVideoFormat(
      "AV1", {{"level-idx", "5"}, {"profile", "0"}, {"tier", "0"}}));
  AddSupportedVideoCodec(SdpVideoFormat("VP9"));  // No parameters
  ASSERT_THAT(engine_->LegacySendCodecs(), HasUniquePtValues());
  ASSERT_THAT(engine_->LegacyRecvCodecs(), HasUniquePtValues());
}

TEST_F(WebRtcVideoEngineTest, SupportsTimestampOffsetHeaderExtension) {
  ExpectRtpCapabilitySupport(RtpExtension::kTimestampOffsetUri, true);
}

TEST_F(WebRtcVideoEngineTest, SupportsAbsoluteSenderTimeHeaderExtension) {
  ExpectRtpCapabilitySupport(RtpExtension::kAbsSendTimeUri, true);
}

TEST_F(WebRtcVideoEngineTest, SupportsTransportSequenceNumberHeaderExtension) {
  ExpectRtpCapabilitySupport(RtpExtension::kTransportSequenceNumberUri, true);
}

TEST_F(WebRtcVideoEngineTest, SupportsVideoRotationHeaderExtension) {
  ExpectRtpCapabilitySupport(RtpExtension::kVideoRotationUri, true);
}

TEST_F(WebRtcVideoEngineTest, SupportsPlayoutDelayHeaderExtension) {
  ExpectRtpCapabilitySupport(RtpExtension::kPlayoutDelayUri, true);
}

TEST_F(WebRtcVideoEngineTest, SupportsVideoContentTypeHeaderExtension) {
  ExpectRtpCapabilitySupport(RtpExtension::kVideoContentTypeUri, true);
}

TEST_F(WebRtcVideoEngineTest, SupportsVideoTimingHeaderExtension) {
  ExpectRtpCapabilitySupport(RtpExtension::kVideoTimingUri, true);
}

TEST_F(WebRtcVideoEngineTest, SupportsColorSpaceHeaderExtension) {
  ExpectRtpCapabilitySupport(RtpExtension::kColorSpaceUri, true);
}

TEST_F(WebRtcVideoEngineTest, AdvertiseGenericDescriptor00) {
  ExpectRtpCapabilitySupport(RtpExtension::kGenericFrameDescriptorUri00, false);
}

TEST_F(WebRtcVideoEngineTest, SupportCorruptionDetectionHeaderExtension) {
  ExpectRtpCapabilitySupport(RtpExtension::kCorruptionDetectionUri, false);
}

class WebRtcVideoEngineTestWithGenericDescriptor
    : public WebRtcVideoEngineTest {
 public:
  WebRtcVideoEngineTestWithGenericDescriptor()
      : WebRtcVideoEngineTest("WebRTC-GenericDescriptorAdvertised/Enabled/") {}
};

TEST_F(WebRtcVideoEngineTestWithGenericDescriptor,
       AdvertiseGenericDescriptor00) {
  ExpectRtpCapabilitySupport(RtpExtension::kGenericFrameDescriptorUri00, true);
}

class WebRtcVideoEngineTestWithDependencyDescriptor
    : public WebRtcVideoEngineTest {
 public:
  WebRtcVideoEngineTestWithDependencyDescriptor()
      : WebRtcVideoEngineTest(
            "WebRTC-DependencyDescriptorAdvertised/Enabled/") {}
};

TEST_F(WebRtcVideoEngineTestWithDependencyDescriptor,
       AdvertiseDependencyDescriptor) {
  ExpectRtpCapabilitySupport(RtpExtension::kDependencyDescriptorUri, true);
}

TEST_F(WebRtcVideoEngineTest, AdvertiseVideoLayersAllocation) {
  ExpectRtpCapabilitySupport(RtpExtension::kVideoLayersAllocationUri, false);
}

class WebRtcVideoEngineTestWithVideoLayersAllocation
    : public WebRtcVideoEngineTest {
 public:
  WebRtcVideoEngineTestWithVideoLayersAllocation()
      : WebRtcVideoEngineTest(
            "WebRTC-VideoLayersAllocationAdvertised/Enabled/") {}
};

TEST_F(WebRtcVideoEngineTestWithVideoLayersAllocation,
       AdvertiseVideoLayersAllocation) {
  ExpectRtpCapabilitySupport(RtpExtension::kVideoLayersAllocationUri, true);
}

class WebRtcVideoFrameTrackingId : public WebRtcVideoEngineTest {
 public:
  WebRtcVideoFrameTrackingId()
      : WebRtcVideoEngineTest(
            "WebRTC-VideoFrameTrackingIdAdvertised/Enabled/") {}
};

TEST_F(WebRtcVideoFrameTrackingId, AdvertiseVideoFrameTrackingId) {
  ExpectRtpCapabilitySupport(RtpExtension::kVideoFrameTrackingIdUri, true);
}

TEST_F(WebRtcVideoEngineTest, CVOSetHeaderExtensionBeforeCapturer) {
  // Allocate the source first to prevent early destruction before channel's
  // dtor is called.
  ::testing::NiceMock<MockVideoSource> video_source;

  AddSupportedVideoCodecType("VP8");

  auto send_channel = SetSendParamsWithAllSupportedCodecs();
  EXPECT_TRUE(send_channel->AddSendStream(StreamParams::CreateLegacy(kSsrc)));

  // Add CVO extension.
  const RtpHeaderExtensionId id(1);
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.extensions.push_back(
      RtpExtension(RtpExtension::kVideoRotationUri, id));
  EXPECT_TRUE(send_channel->SetSenderParameters(parameters));

  EXPECT_CALL(
      video_source,
      AddOrUpdateSink(_, Field(&VideoSinkWants::rotation_applied, false)));
  // Set capturer.
  EXPECT_TRUE(send_channel->SetVideoSend(kSsrc, nullptr, &video_source));

  // Verify capturer has turned off applying rotation.
  ::testing::Mock::VerifyAndClear(&video_source);

  // Verify removing header extension turns on applying rotation.
  parameters.extensions.clear();
  EXPECT_CALL(
      video_source,
      AddOrUpdateSink(_, Field(&VideoSinkWants::rotation_applied, true)));

  EXPECT_TRUE(send_channel->SetSenderParameters(parameters));
}

TEST_F(WebRtcVideoEngineTest, CVOSetHeaderExtensionBeforeAddSendStream) {
  // Allocate the source first to prevent early destruction before channel's
  // dtor is called.
  ::testing::NiceMock<MockVideoSource> video_source;

  AddSupportedVideoCodecType("VP8");

  auto send_channel = SetSendParamsWithAllSupportedCodecs();
  // Add CVO extension.
  const RtpHeaderExtensionId id(1);
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.extensions.push_back(
      RtpExtension(RtpExtension::kVideoRotationUri, id));
  EXPECT_TRUE(send_channel->SetSenderParameters(parameters));
  EXPECT_TRUE(send_channel->AddSendStream(StreamParams::CreateLegacy(kSsrc)));

  // Set source.
  EXPECT_CALL(
      video_source,
      AddOrUpdateSink(_, Field(&VideoSinkWants::rotation_applied, false)));
  EXPECT_TRUE(send_channel->SetVideoSend(kSsrc, nullptr, &video_source));
}

TEST_F(WebRtcVideoEngineTest, CVOSetHeaderExtensionAfterCapturer) {
  ::testing::NiceMock<MockVideoSource> video_source;

  AddSupportedVideoCodecType("VP8");
  AddSupportedVideoCodecType("VP9");

  auto send_channel = SetSendParamsWithAllSupportedCodecs();

  EXPECT_TRUE(send_channel->AddSendStream(StreamParams::CreateLegacy(kSsrc)));

  // Set capturer.
  EXPECT_CALL(
      video_source,
      AddOrUpdateSink(_, Field(&VideoSinkWants::rotation_applied, true)));
  EXPECT_TRUE(send_channel->SetVideoSend(kSsrc, nullptr, &video_source));

  // Verify capturer has turned on applying rotation.
  ::testing::Mock::VerifyAndClear(&video_source);

  // Add CVO extension.
  const RtpHeaderExtensionId id(1);
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  parameters.extensions.push_back(
      RtpExtension(RtpExtension::kVideoRotationUri, id));
  // Also remove the first codec to trigger a codec change as well.
  parameters.codecs.erase(parameters.codecs.begin());
  EXPECT_CALL(
      video_source,
      AddOrUpdateSink(_, Field(&VideoSinkWants::rotation_applied, false)));
  EXPECT_TRUE(send_channel->SetSenderParameters(parameters));

  // Verify capturer has turned off applying rotation.
  ::testing::Mock::VerifyAndClear(&video_source);

  // Verify removing header extension turns on applying rotation.
  parameters.extensions.clear();
  EXPECT_CALL(
      video_source,
      AddOrUpdateSink(_, Field(&VideoSinkWants::rotation_applied, true)));
  EXPECT_TRUE(send_channel->SetSenderParameters(parameters));
}

TEST_F(WebRtcVideoEngineTest, SetSendFailsBeforeSettingCodecs) {
  AddSupportedVideoCodecType("VP8");

  std::unique_ptr<VideoMediaSendChannelInterface> send_channel =
      engine_->CreateSendChannel(
          env_, call_.get(), GetMediaConfig(), VideoOptions(), CryptoOptions(),
          video_bitrate_allocator_factory_.get(), nullptr, nullptr);

  EXPECT_TRUE(send_channel->AddSendStream(StreamParams::CreateLegacy(123)));

  EXPECT_FALSE(send_channel->SetSend(true))
      << "Channel should not start without codecs.";
  EXPECT_TRUE(send_channel->SetSend(false))
      << "Channel should be stoppable even without set codecs.";
}

TEST_F(WebRtcVideoEngineTest, GetStatsWithoutCodecsSetDoesNotCrash) {
  AddSupportedVideoCodecType("VP8");

  std::unique_ptr<VideoMediaSendChannelInterface> send_channel =
      engine_->CreateSendChannel(
          env_, call_.get(), GetMediaConfig(), VideoOptions(), CryptoOptions(),
          video_bitrate_allocator_factory_.get(), nullptr, nullptr);
  EXPECT_TRUE(send_channel->AddSendStream(StreamParams::CreateLegacy(123)));
  VideoMediaSendInfo send_info;
  send_channel->GetStats(&send_info);

  std::unique_ptr<VideoMediaReceiveChannelInterface> receive_channel =
      engine_->CreateReceiveChannel(
          env_, call_.get(), GetMediaConfig(), CryptoOptions(), nullptr,
          [](uint32_t, const RtpPacketInfos&, Timestamp) {});
  EXPECT_TRUE(receive_channel->AddRecvStream(StreamParams::CreateLegacy(123)));
  VideoMediaReceiveInfo receive_info;
  receive_channel->GetStats(&receive_info);
}

TEST_F(WebRtcVideoEngineTest, UseFactoryForVp8WhenSupported) {
  AddSupportedVideoCodecType("VP8");

  auto send_channel = SetSendParamsWithAllSupportedCodecs();

  send_channel->OnReadyToSend(true);

  EXPECT_TRUE(send_channel->AddSendStream(StreamParams::CreateLegacy(kSsrc)));
  EXPECT_EQ(0, encoder_factory_->GetNumCreatedEncoders());
  EXPECT_TRUE(send_channel->SetSend(true));
  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 30,
                               env_.clock().CurrentTime());
  EXPECT_TRUE(send_channel->SetVideoSend(kSsrc, nullptr, &frame_forwarder));
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  // Sending one frame will have allocate the encoder.
  ASSERT_TRUE(time_controller_.Wait(
      [&] { return encoder_factory_->GetNumCreatedEncoders() >= 1; }));
  EXPECT_GT(encoder_factory_->encoders()[0]->GetNumEncodedFrames(), 0);

  int num_created_encoders = encoder_factory_->GetNumCreatedEncoders();
  EXPECT_EQ(num_created_encoders, 1);

  // Setting codecs of the same type should not reallocate any encoders
  // (expecting a no-op).
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(send_channel->SetSenderParameters(parameters));
  EXPECT_EQ(num_created_encoders, encoder_factory_->GetNumCreatedEncoders());

  // Remove stream previously added to free the external encoder instance.
  EXPECT_TRUE(send_channel->RemoveSendStream(kSsrc));
  EXPECT_EQ(0u, encoder_factory_->encoders().size());
}

// Test that when an encoder factory supports H264, we add an RTX
// codec for it.
// TODO(deadbeef): This test should be updated if/when we start
// adding RTX codecs for unrecognized codec names.
TEST_F(WebRtcVideoEngineTest, RtxCodecAddedForH264Codec) {
  SdpVideoFormat h264_constrained_baseline("H264");
  h264_constrained_baseline.parameters[kH264FmtpProfileLevelId] =
      *H264ProfileLevelIdToString(H264ProfileLevelId(
          H264Profile::kProfileConstrainedBaseline, H264Level::kLevel1));
  SdpVideoFormat h264_constrained_high("H264");
  h264_constrained_high.parameters[kH264FmtpProfileLevelId] =
      *H264ProfileLevelIdToString(H264ProfileLevelId(
          H264Profile::kProfileConstrainedHigh, H264Level::kLevel1));
  SdpVideoFormat h264_high("H264");
  h264_high.parameters[kH264FmtpProfileLevelId] = *H264ProfileLevelIdToString(
      H264ProfileLevelId(H264Profile::kProfileHigh, H264Level::kLevel1));

  encoder_factory_->AddSupportedVideoCodec(h264_constrained_baseline);
  encoder_factory_->AddSupportedVideoCodec(h264_constrained_high);
  encoder_factory_->AddSupportedVideoCodec(h264_high);

  // First figure out what payload types the test codecs got assigned.
  const std::vector<Codec> codecs = engine_->LegacySendCodecs();
  // Now search for RTX codecs for them. Expect that they all have associated
  // RTX codecs.
  EXPECT_TRUE(HasRtxCodec(
      codecs, FindMatchingVideoCodec(
                  codecs, CreateVideoCodec(h264_constrained_baseline))
                  ->id));
  EXPECT_TRUE(HasRtxCodec(
      codecs,
      FindMatchingVideoCodec(codecs, CreateVideoCodec(h264_constrained_high))
          ->id));
  EXPECT_TRUE(HasRtxCodec(
      codecs, FindMatchingVideoCodec(codecs, CreateVideoCodec(h264_high))->id));
}

#if defined(RTC_ENABLE_VP9)
TEST_F(WebRtcVideoEngineTest, CanConstructDecoderForVp9EncoderFactory) {
  AddSupportedVideoCodecType("VP9");

  auto receive_channel = SetRecvParamsWithAllSupportedCodecs();

  EXPECT_TRUE(
      receive_channel->AddRecvStream(StreamParams::CreateLegacy(kSsrc)));
}
#endif  // defined(RTC_ENABLE_VP9)

TEST_F(WebRtcVideoEngineTest, EncoderSwitchRequestCallback) {
  AddSupportedVideoCodecType("VP8");

  bool callback_called = false;
  auto callback =
      [&callback_called, call = call_.get(), main_thread = Thread::Current()](
          VideoMediaSendChannelInterface::EncoderSwitchRequestAction action) {
        call->worker_thread()->PostTask([action = std::move(action),
                                         main_thread,
                                         &callback_called]() mutable {
          std::move(action)();
          main_thread->PostTask([&callback_called] { callback_called = true; });
        });
      };

  std::unique_ptr<VideoMediaSendChannelInterface> send_channel =
      engine_->CreateSendChannel(
          env_, call_.get(), GetMediaConfig(), VideoOptions(), CryptoOptions(),
          video_bitrate_allocator_factory_.get(), std::move(callback), nullptr);

  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(send_channel->SetSenderParameters(parameters));

  send_channel->OnReadyToSend(true);
  EXPECT_TRUE(send_channel->AddSendStream(StreamParams::CreateLegacy(kSsrc)));
  EXPECT_TRUE(send_channel->SetSend(true));

  auto* web_rtc_send_channel =
      static_cast<WebRtcVideoSendChannel*>(send_channel.get());

  std::unique_ptr<Thread> bg_thread = Thread::Create();
  bg_thread->Start();
  bg_thread->BlockingCall(
      [&] { web_rtc_send_channel->RequestEncoderSwitch(std::nullopt, true); });

  EXPECT_TRUE(time_controller_.Wait([&] { return callback_called; }));

  EXPECT_TRUE(send_channel->RemoveSendStream(kSsrc));
}

TEST_F(WebRtcVideoEngineTest, PropagatesInputFrameTimestamp) {
  AddSupportedVideoCodecType("VP8");
  FakeCall* fake_call = new FakeCall(env_);
  call_.reset(fake_call);
  auto send_channel = SetSendParamsWithAllSupportedCodecs();

  EXPECT_TRUE(send_channel->AddSendStream(StreamParams::CreateLegacy(kSsrc)));

  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 60,
                               env_.clock().CurrentTime());
  EXPECT_TRUE(send_channel->SetVideoSend(kSsrc, nullptr, &frame_forwarder));
  send_channel->SetSend(true);

  FakeVideoSendStream* stream = fake_call->GetVideoSendStreams()[0];

  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  int64_t last_timestamp = stream->GetLastTimestamp();
  for (int i = 0; i < 10; i++) {
    frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
    int64_t timestamp = stream->GetLastTimestamp();
    int64_t interval = timestamp - last_timestamp;

    // Precision changes from nanosecond to millisecond.
    // Allow error to be no more than 1.
    EXPECT_NEAR(VideoFormat::FpsToInterval(60) / 1E6, interval, 1);

    last_timestamp = timestamp;
  }

  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame(
      1280, 720, VideoRotation::kVideoRotation_0, TimeDelta::Seconds(1) / 30));
  last_timestamp = stream->GetLastTimestamp();
  for (int i = 0; i < 10; i++) {
    frame_forwarder.IncomingCapturedFrame(
        frame_source.GetFrame(1280, 720, VideoRotation::kVideoRotation_0,
                              TimeDelta::Seconds(1) / 30));
    int64_t timestamp = stream->GetLastTimestamp();
    int64_t interval = timestamp - last_timestamp;

    // Precision changes from nanosecond to millisecond.
    // Allow error to be no more than 1.
    EXPECT_NEAR(VideoFormat::FpsToInterval(30) / 1E6, interval, 1);

    last_timestamp = timestamp;
  }

  // Remove stream previously added to free the external encoder instance.
  EXPECT_TRUE(send_channel->RemoveSendStream(kSsrc));
}

void WebRtcVideoEngineTest::AssignDefaultAptRtxTypes() {
  std::vector<Codec> engine_codecs = engine_->LegacySendCodecs();
  RTC_DCHECK(!engine_codecs.empty());
  for (const Codec& codec : engine_codecs) {
    if (codec.name == "rtx") {
      int associated_payload_type;
      if (codec.GetParam(kCodecParamAssociatedPayloadType,
                         &associated_payload_type)) {
        default_apt_rtx_types_[associated_payload_type] = codec.id;
      }
    }
  }
}

void WebRtcVideoEngineTest::AssignDefaultCodec() {
  std::vector<Codec> engine_codecs = engine_->LegacySendCodecs();
  RTC_DCHECK(!engine_codecs.empty());
  bool codec_set = false;
  for (const Codec& codec : engine_codecs) {
    if (!codec_set && codec.name != "rtx" && codec.name != "red" &&
        codec.name != "ulpfec" && codec.name != "flexfec-03") {
      default_codec_ = codec;
      codec_set = true;
    }
  }

  RTC_DCHECK(codec_set);
}

size_t WebRtcVideoEngineTest::GetEngineCodecIndex(
    const std::string& name) const {
  const std::vector<Codec> codecs = engine_->LegacySendCodecs();
  for (size_t i = 0; i < codecs.size(); ++i) {
    const Codec engine_codec = codecs[i];
    if (!absl::EqualsIgnoreCase(name, engine_codec.name))
      continue;
    // The tests only use H264 Constrained Baseline. Make sure we don't return
    // an internal H264 codec from the engine with a different H264 profile.
    if (absl::EqualsIgnoreCase(name.c_str(), kH264CodecName)) {
      const std::optional<H264ProfileLevelId> profile_level_id =
          ParseSdpForH264ProfileLevelId(engine_codec.params);
      if (profile_level_id->profile !=
          H264Profile::kProfileConstrainedBaseline) {
        continue;
      }
    }
    return i;
  }
  // This point should never be reached.
  ADD_FAILURE() << "Unrecognized codec name: " << name;
  return -1;
}

Codec WebRtcVideoEngineTest::GetEngineCodec(const std::string& name) const {
  return engine_->LegacySendCodecs()[GetEngineCodecIndex(name)];
}

void WebRtcVideoEngineTest::AddSupportedVideoCodecType(
    const std::string& name,
    const std::vector<ScalabilityMode>& scalability_modes) {
  encoder_factory_->AddSupportedVideoCodecType(name, scalability_modes);
  decoder_factory_->AddSupportedVideoCodecType(name);
}

void WebRtcVideoEngineTest::AddSupportedVideoCodec(SdpVideoFormat format) {
  encoder_factory_->AddSupportedVideoCodec(format);
  decoder_factory_->AddSupportedVideoCodec(format);
}

std::unique_ptr<VideoMediaSendChannelInterface>
WebRtcVideoEngineTest::SetSendParamsWithAllSupportedCodecs() {
  std::unique_ptr<VideoMediaSendChannelInterface> channel =
      engine_->CreateSendChannel(
          env_, call_.get(), GetMediaConfig(), VideoOptions(), CryptoOptions(),
          video_bitrate_allocator_factory_.get(), nullptr, nullptr);
  VideoSenderParameters parameters;
  // We need to look up the codec in the engine to get the correct payload type.
  for (const SdpVideoFormat& format : encoder_factory_->GetSupportedFormats()) {
    Codec engine_codec = GetEngineCodec(format.name);
    if (!absl::c_linear_search(parameters.codecs, engine_codec)) {
      parameters.codecs.push_back(engine_codec);
    }
  }

  EXPECT_TRUE(channel->SetSenderParameters(parameters));

  return channel;
}

std::unique_ptr<VideoMediaReceiveChannelInterface>
WebRtcVideoEngineTest::SetRecvParamsWithSupportedCodecs(
    const std::vector<Codec>& codecs) {
  std::unique_ptr<VideoMediaReceiveChannelInterface> channel =
      engine_->CreateReceiveChannel(
          env_, call_.get(), GetMediaConfig(), CryptoOptions(), nullptr,
          [](uint32_t, const RtpPacketInfos&, Timestamp) {});
  VideoReceiverParameters parameters;
  parameters.codecs = codecs;
  EXPECT_TRUE(channel->SetReceiverParameters(parameters));

  return channel;
}

std::unique_ptr<VideoMediaReceiveChannelInterface>
WebRtcVideoEngineTest::SetRecvParamsWithAllSupportedCodecs() {
  std::vector<Codec> codecs;
  for (const SdpVideoFormat& format : decoder_factory_->GetSupportedFormats()) {
    Codec engine_codec = GetEngineCodec(format.name);
    if (!absl::c_linear_search(codecs, engine_codec)) {
      codecs.push_back(engine_codec);
    }
  }

  return SetRecvParamsWithSupportedCodecs(codecs);
}

void WebRtcVideoEngineTest::ExpectRtpCapabilitySupport(const char* uri,
                                                       bool supported) const {
  const std::vector<RtpHeaderExtensionCapability> header_extensions =
      GetDefaultEnabledRtpHeaderCapabilities(*engine_,
                                             /* field_trials= */ nullptr);
  if (supported) {
    EXPECT_THAT(header_extensions,
                Contains(Field(&RtpHeaderExtensionCapability::uri, uri)));
  } else {
    EXPECT_THAT(header_extensions,
                Each(Field(&RtpHeaderExtensionCapability::uri, StrNe(uri))));
  }
}

TEST_F(WebRtcVideoEngineTest, ReceiveBufferSizeViaFieldTrial) {
  ChangeFieldTrials("WebRTC-ReceiveBufferSize", "size_bytes:10000");
  std::unique_ptr<VideoMediaReceiveChannelInterface> receive_channel =
      engine_->CreateReceiveChannel(
          env_, call_.get(), GetMediaConfig(), CryptoOptions(), nullptr,
          [](uint32_t, const RtpPacketInfos&, Timestamp) {});
  FakeNetworkInterface network(env_);
  receive_channel->SetInterface(&network);
  EXPECT_EQ(10000, network.recvbuf_size());
  receive_channel->SetInterface(nullptr);
}

TEST_F(WebRtcVideoEngineTest, TooHighReceiveBufferSizeViaFieldTrial) {
  // 10000001 is too high, it will revert to the default
  // kVideoRtpRecvBufferSize.
  ChangeFieldTrials("WebRTC-ReceiveBufferSize", "size_bytes:10000001");
  std::unique_ptr<VideoMediaReceiveChannelInterface> receive_channel =
      engine_->CreateReceiveChannel(
          env_, call_.get(), GetMediaConfig(), CryptoOptions(), nullptr,
          [](uint32_t, const RtpPacketInfos&, Timestamp) {});
  FakeNetworkInterface network(env_);
  receive_channel->SetInterface(&network);
  EXPECT_EQ(kVideoRtpRecvBufferSize, network.recvbuf_size());
  receive_channel->SetInterface(nullptr);
}

TEST_F(WebRtcVideoEngineTest, TooLowReceiveBufferSizeViaFieldTrial) {
  // 9999 is too low, it will revert to the default kVideoRtpRecvBufferSize.
  ChangeFieldTrials("WebRTC-ReceiveBufferSize", "size_bytes:9999");
  std::unique_ptr<VideoMediaReceiveChannelInterface> receive_channel =
      engine_->CreateReceiveChannel(
          env_, call_.get(), GetMediaConfig(), CryptoOptions(), nullptr,
          [](uint32_t, const RtpPacketInfos&, Timestamp) {});
  FakeNetworkInterface network(env_);
  receive_channel->SetInterface(&network);
  EXPECT_EQ(kVideoRtpRecvBufferSize, network.recvbuf_size());
  receive_channel->SetInterface(nullptr);
}

TEST_F(WebRtcVideoEngineTest, UpdatesUnsignaledRtxSsrcAndRecoversPayload) {
  // Setup a channel with VP8, RTX and transport sequence number header
  // extension. Receive stream is not explicitly configured.
  AddSupportedVideoCodecType("VP8");
  std::vector<Codec> supported_codecs =
      engine_->LegacyRecvCodecs(/*include_rtx=*/true);
  ASSERT_EQ(supported_codecs[1].name, "rtx");
  int rtx_payload_type = supported_codecs[1].id;

  std::unique_ptr<VideoMediaReceiveChannelInterface> receive_channel =
      engine_->CreateReceiveChannel(
          env_, call_.get(), GetMediaConfig(), CryptoOptions(), nullptr,
          [](uint32_t, const RtpPacketInfos&, Timestamp) {});
  VideoReceiverParameters parameters;
  parameters.codecs = supported_codecs;
  ASSERT_TRUE(receive_channel->SetReceiverParameters(parameters));
  receive_channel->SetReceive(true);

  // Receive a normal payload packet. It is not a complete frame since the
  // marker bit is not set.
  RtpPacketReceived packet_1 =
      BuildVp8KeyFrame(/*ssrc*/ 123, supported_codecs[0].id);
  packet_1.SetMarker(false);
  receive_channel->OnPacketReceived(packet_1);

  time_controller_.AdvanceTime(TimeDelta::Millis(100));
  // No complete frame received. No decoder created yet.
  EXPECT_THAT(decoder_factory_->decoders(), IsEmpty());

  RtpPacketReceived packet_2;
  packet_2.SetSsrc(123);
  packet_2.SetPayloadType(supported_codecs[0].id);
  packet_2.SetSequenceNumber(packet_1.SequenceNumber() + 1);
  memset(packet_2.AllocatePayload(500), 0, 1);
  packet_2.SetMarker(true);  //  Frame is complete.
  RtpPacketReceived rtx_packet =
      BuildRtxPacket(345, rtx_payload_type, packet_2);

  receive_channel->OnPacketReceived(rtx_packet);

  time_controller_.AdvanceTime(TimeDelta::Millis(0));
  ASSERT_THAT(decoder_factory_->decoders(), Not(IsEmpty()));
  EXPECT_EQ(decoder_factory_->decoders()[0]->GetNumFramesReceived(), 1);
}

TEST_F(WebRtcVideoEngineTest, UsesSimulcastAdapterForVp8Factories) {
  AddSupportedVideoCodecType("VP8");

  auto send_channel = SetSendParamsWithAllSupportedCodecs();

  std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs3);

  EXPECT_TRUE(
      send_channel->AddSendStream(CreateSimStreamParams("cname", ssrcs)));
  EXPECT_TRUE(send_channel->SetSend(true));

  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 60,
                               env_.clock().CurrentTime());
  EXPECT_TRUE(
      send_channel->SetVideoSend(ssrcs.front(), nullptr, &frame_forwarder));
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  ASSERT_TRUE(time_controller_.Wait(
      [&] { return encoder_factory_->GetNumCreatedEncoders() >= 2; }));

  // Verify that encoders are configured for simulcast through adapter
  // (increasing resolution and only configured to send one stream each).
  int prev_width = -1;
  for (size_t i = 0; i < encoder_factory_->encoders().size(); ++i) {
    ASSERT_TRUE(encoder_factory_->encoders()[i]->WaitForInitEncode());
    VideoCodec codec_settings =
        encoder_factory_->encoders()[i]->GetCodecSettings();
    EXPECT_EQ(0, codec_settings.numberOfSimulcastStreams);
    EXPECT_GT(codec_settings.width, prev_width);
    prev_width = codec_settings.width;
  }

  EXPECT_TRUE(send_channel->SetVideoSend(ssrcs.front(), nullptr, nullptr));

  send_channel.reset();
  ASSERT_EQ(0u, encoder_factory_->encoders().size());
}

TEST_F(WebRtcVideoEngineTest, ChannelWithH264CanChangeToVp8) {
  AddSupportedVideoCodecType("VP8");
  AddSupportedVideoCodecType("H264");

  // Frame source.
  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 30,
                               env_.clock().CurrentTime());

  std::unique_ptr<VideoMediaSendChannelInterface> send_channel =
      engine_->CreateSendChannel(
          env_, call_.get(), GetMediaConfig(), VideoOptions(), CryptoOptions(),
          video_bitrate_allocator_factory_.get(), nullptr, nullptr);
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("H264"));
  EXPECT_TRUE(send_channel->SetSenderParameters(parameters));

  EXPECT_TRUE(send_channel->AddSendStream(StreamParams::CreateLegacy(kSsrc)));
  EXPECT_TRUE(send_channel->SetVideoSend(kSsrc, nullptr, &frame_forwarder));
  // Sending one frame will have allocate the encoder.
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  time_controller_.AdvanceTime(TimeDelta::Zero());

  ASSERT_EQ(1u, encoder_factory_->encoders().size());

  VideoSenderParameters new_parameters;
  new_parameters.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(send_channel->SetSenderParameters(new_parameters));

  // Sending one frame will switch encoder.
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  time_controller_.AdvanceTime(TimeDelta::Zero());

  EXPECT_EQ(1u, encoder_factory_->encoders().size());
}

TEST_F(WebRtcVideoEngineTest,
       UsesSimulcastAdapterForVp8WithCombinedVP8AndH264Factory) {
  AddSupportedVideoCodecType("VP8");
  AddSupportedVideoCodecType("H264");

  std::unique_ptr<VideoMediaSendChannelInterface> send_channel =
      engine_->CreateSendChannel(
          env_, call_.get(), GetMediaConfig(), VideoOptions(), CryptoOptions(),
          video_bitrate_allocator_factory_.get(), nullptr, nullptr);
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(send_channel->SetSenderParameters(parameters));

  std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs3);

  EXPECT_TRUE(
      send_channel->AddSendStream(CreateSimStreamParams("cname", ssrcs)));
  EXPECT_TRUE(send_channel->SetSend(true));

  // Send a fake frame, or else the media engine will configure the simulcast
  // encoder adapter at a low-enough size that it'll only create a single
  // encoder layer.
  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 30,
                               env_.clock().CurrentTime());
  EXPECT_TRUE(
      send_channel->SetVideoSend(ssrcs.front(), nullptr, &frame_forwarder));
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  ASSERT_TRUE(time_controller_.Wait(
      [&] { return encoder_factory_->GetNumCreatedEncoders() >= 2; }));
  ASSERT_TRUE(encoder_factory_->encoders()[0]->WaitForInitEncode());
  EXPECT_EQ(kVideoCodecVP8,
            encoder_factory_->encoders()[0]->GetCodecSettings().codecType);

  send_channel.reset();
  // Make sure DestroyVideoEncoder was called on the factory.
  EXPECT_EQ(0u, encoder_factory_->encoders().size());
}

TEST_F(WebRtcVideoEngineTest,
       DestroysNonSimulcastEncoderFromCombinedVP8AndH264Factory) {
  AddSupportedVideoCodecType("VP8");
  AddSupportedVideoCodecType("H264");

  std::unique_ptr<VideoMediaSendChannelInterface> send_channel =
      engine_->CreateSendChannel(
          env_, call_.get(), GetMediaConfig(), VideoOptions(), CryptoOptions(),
          video_bitrate_allocator_factory_.get(), nullptr, nullptr);
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("H264"));
  EXPECT_TRUE(send_channel->SetSenderParameters(parameters));

  EXPECT_TRUE(send_channel->AddSendStream(StreamParams::CreateLegacy(kSsrc)));

  // Send a frame of 720p. This should trigger a "real" encoder initialization.
  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 30,
                               env_.clock().CurrentTime());
  EXPECT_TRUE(send_channel->SetVideoSend(kSsrc, nullptr, &frame_forwarder));
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  ASSERT_TRUE(time_controller_.Wait(
      [&] { return encoder_factory_->GetNumCreatedEncoders() >= 1; }));
  ASSERT_EQ(1u, encoder_factory_->encoders().size());
  ASSERT_TRUE(encoder_factory_->encoders()[0]->WaitForInitEncode());
  EXPECT_EQ(kVideoCodecH264,
            encoder_factory_->encoders()[0]->GetCodecSettings().codecType);

  send_channel.reset();
  // Make sure DestroyVideoEncoder was called on the factory.
  ASSERT_EQ(0u, encoder_factory_->encoders().size());
}

TEST_F(WebRtcVideoEngineTest, SimulcastEnabledForH264) {
  AddSupportedVideoCodecType("H264");

  std::unique_ptr<VideoMediaSendChannelInterface> send_channel =
      engine_->CreateSendChannel(
          env_, call_.get(), GetMediaConfig(), VideoOptions(), CryptoOptions(),
          video_bitrate_allocator_factory_.get(), nullptr, nullptr);

  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("H264"));
  EXPECT_TRUE(send_channel->SetSenderParameters(parameters));

  const std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs3);
  EXPECT_TRUE(
      send_channel->AddSendStream(CreateSimStreamParams("cname", ssrcs)));

  // Send a frame of 720p. This should trigger a "real" encoder initialization.
  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 30,
                               env_.clock().CurrentTime());
  EXPECT_TRUE(send_channel->SetVideoSend(ssrcs[0], nullptr, &frame_forwarder));
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());

  ASSERT_TRUE(time_controller_.Wait(
      [&] { return encoder_factory_->GetNumCreatedEncoders() >= 1; }));
  ASSERT_EQ(1u, encoder_factory_->encoders().size());
  FakeWebRtcVideoEncoder* encoder = encoder_factory_->encoders()[0];
  ASSERT_TRUE(encoder_factory_->encoders()[0]->WaitForInitEncode());
  EXPECT_EQ(kVideoCodecH264, encoder->GetCodecSettings().codecType);
  EXPECT_LT(1u, encoder->GetCodecSettings().numberOfSimulcastStreams);
  EXPECT_TRUE(send_channel->SetVideoSend(ssrcs[0], nullptr, nullptr));
}

// Test that FlexFEC is not supported as a send video codec by default.
// Only enabling field trial should allow advertising FlexFEC send codec.
class WebRtcVideoEngineTestWithFlexfec : public WebRtcVideoEngineTest {
 public:
  WebRtcVideoEngineTestWithFlexfec()
      : WebRtcVideoEngineTest("WebRTC-FlexFEC-03-Advertised/Enabled/") {}
};

TEST_F(WebRtcVideoEngineTest, Flexfec03SendCodecEnablesWithoutFieldTrial) {
  encoder_factory_->AddSupportedVideoCodecType("VP8");
  auto flexfec = Field("name", &Codec::name, "flexfec-03");
  EXPECT_THAT(engine_->LegacySendCodecs(), Not(Contains(flexfec)));
}

TEST_F(WebRtcVideoEngineTestWithFlexfec,
       Flexfec03SendCodecEnablesWithFieldTrial) {
  encoder_factory_->AddSupportedVideoCodecType("VP8");
  auto flexfec = Field("name", &Codec::name, "flexfec-03");
  EXPECT_THAT(engine_->LegacySendCodecs(), Contains(flexfec));
}

// Test that the FlexFEC "codec" gets assigned to the lower payload type range
TEST_F(WebRtcVideoEngineTestWithFlexfec, Flexfec03LowerPayloadTypeRange) {
  encoder_factory_->AddSupportedVideoCodecType("VP8");

  auto flexfec = Field("name", &Codec::name, "flexfec-03");

  auto send_codecs = engine_->LegacySendCodecs();
  auto it = std::find_if(
      send_codecs.begin(), send_codecs.end(),
      [](const Codec& codec) { return codec.name == "flexfec-03"; });
  ASSERT_NE(it, send_codecs.end());
  EXPECT_LE(35, it->id);
  EXPECT_GE(65, it->id);
}

// Test that codecs are added in the order they are reported from the factory.
TEST_F(WebRtcVideoEngineTest, ReportSupportedCodecs) {
  encoder_factory_->AddSupportedVideoCodecType("VP8");
  const char* kFakeCodecName = "FakeCodec";
  encoder_factory_->AddSupportedVideoCodecType(kFakeCodecName);

  // The last reported codec should appear after the first codec in the vector.
  const size_t vp8_index = GetEngineCodecIndex("VP8");
  const size_t fake_codec_index = GetEngineCodecIndex(kFakeCodecName);
  EXPECT_LT(vp8_index, fake_codec_index);
}

// Test that a codec that was added after the engine was initialized
// does show up in the codec list after it was added.
TEST_F(WebRtcVideoEngineTest, ReportSupportedAddedCodec) {
  const char* kFakeExternalCodecName1 = "FakeExternalCodec1";
  const char* kFakeExternalCodecName2 = "FakeExternalCodec2";

  // Set up external encoder factory with first codec, and initialize engine.
  encoder_factory_->AddSupportedVideoCodecType(kFakeExternalCodecName1);

  std::vector<Codec> codecs_before(engine_->LegacySendCodecs());

  // Add second codec.
  encoder_factory_->AddSupportedVideoCodecType(kFakeExternalCodecName2);
  std::vector<Codec> codecs_after(engine_->LegacySendCodecs());
  // The codec itself and RTX should have been added.
  EXPECT_EQ(codecs_before.size() + 2, codecs_after.size());

  // Check that both fake codecs are present and that the second fake codec
  // appears after the first fake codec.
  const size_t fake_codec_index1 = GetEngineCodecIndex(kFakeExternalCodecName1);
  const size_t fake_codec_index2 = GetEngineCodecIndex(kFakeExternalCodecName2);
  EXPECT_LT(fake_codec_index1, fake_codec_index2);
}

TEST_F(WebRtcVideoEngineTest, ReportRtxForExternalCodec) {
  const char* kFakeCodecName = "FakeCodec";
  encoder_factory_->AddSupportedVideoCodecType(kFakeCodecName);

  const size_t fake_codec_index = GetEngineCodecIndex(kFakeCodecName);
  EXPECT_EQ("rtx", engine_->LegacySendCodecs().at(fake_codec_index + 1).name);
}

TEST_F(WebRtcVideoEngineTest, RegisterDecodersIfSupported) {
  AddSupportedVideoCodecType("VP8");
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));

  auto receive_channel = SetRecvParamsWithSupportedCodecs(parameters.codecs);

  EXPECT_TRUE(
      receive_channel->AddRecvStream(StreamParams::CreateLegacy(kSsrc)));
  // Decoders are not created until they are used.
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(0u, decoder_factory_->decoders().size());

  // Setting codecs of the same type should not reallocate the decoder.
  EXPECT_TRUE(receive_channel->SetReceiverParameters(parameters));
  EXPECT_EQ(0, decoder_factory_->GetNumCreatedDecoders());

  // Remove stream previously added to free the external decoder instance.
  EXPECT_TRUE(receive_channel->RemoveRecvStream(kSsrc));
  EXPECT_EQ(0u, decoder_factory_->decoders().size());
}

// Verifies that we can set up decoders.
TEST_F(WebRtcVideoEngineTest, RegisterH264DecoderIfSupported) {
  // TODO(pbos): Do not assume that encoder/decoder support is symmetric. We
  // can't even query the WebRtcVideoDecoderFactory for supported codecs.
  // For now we add a FakeWebRtcVideoEncoderFactory to add H264 to supported
  // codecs.
  AddSupportedVideoCodecType("H264");
  std::vector<Codec> codecs;
  codecs.push_back(GetEngineCodec("H264"));

  auto receive_channel = SetRecvParamsWithSupportedCodecs(codecs);

  EXPECT_TRUE(
      receive_channel->AddRecvStream(StreamParams::CreateLegacy(kSsrc)));
  // Decoders are not created until they are used.
  time_controller_.AdvanceTime(TimeDelta::Zero());
  ASSERT_EQ(0u, decoder_factory_->decoders().size());
}

TEST(WebRtcVideoEngineNewVideoCodecFactoryTest, NullFactories) {
  std::unique_ptr<VideoEncoderFactory> encoder_factory;
  std::unique_ptr<VideoDecoderFactory> decoder_factory;
  FieldTrials trials = CreateTestFieldTrials();
  WebRtcVideoEngine engine(std::move(encoder_factory),
                           std::move(decoder_factory), trials);
  EXPECT_EQ(0u, engine.LegacySendCodecs().size());
  EXPECT_EQ(0u, engine.LegacyRecvCodecs().size());
}

TEST(WebRtcVideoEngineNewVideoCodecFactoryTest, EmptyFactories) {
  // `engine` take ownership of the factories.
  MockVideoEncoderFactory* encoder_factory = new MockVideoEncoderFactory();
  MockVideoDecoderFactory* decoder_factory = new MockVideoDecoderFactory();
  FieldTrials trials = CreateTestFieldTrials();
  WebRtcVideoEngine engine(
      (std::unique_ptr<VideoEncoderFactory>(encoder_factory)),
      (std::unique_ptr<VideoDecoderFactory>(decoder_factory)), trials);
  // TODO(kron): Change to Times(1) once send and receive codecs are changed
  // to be treated independently.
  EXPECT_CALL(*encoder_factory, GetSupportedFormats()).Times(1);
  EXPECT_EQ(0u, engine.LegacySendCodecs().size());
  EXPECT_EQ(0u, engine.LegacyRecvCodecs().size());
  EXPECT_CALL(*encoder_factory, Die());
  EXPECT_CALL(*decoder_factory, Die());
}

// Test full behavior in the video engine when video codec factories of the new
// type are injected supporting the single codec Vp8. Check the returned codecs
// from the engine and that we will create a Vp8 encoder and decoder using the
// new factories.
TEST(WebRtcVideoEngineNewVideoCodecFactoryTest, Vp8) {
  // `engine` take ownership of the factories.
  MockVideoEncoderFactory* encoder_factory = new MockVideoEncoderFactory();
  MockVideoDecoderFactory* decoder_factory = new MockVideoDecoderFactory();
  std::unique_ptr<MockVideoBitrateAllocatorFactory> rate_allocator_factory =
      std::make_unique<MockVideoBitrateAllocatorFactory>();
  EXPECT_CALL(*rate_allocator_factory,
              Create(_, Field(&VideoCodec::codecType, kVideoCodecVP8)))
      .WillOnce([] { return std::make_unique<MockVideoBitrateAllocator>(); });
  FieldTrials trials = CreateTestFieldTrials();
  WebRtcVideoEngine engine(
      (std::unique_ptr<VideoEncoderFactory>(encoder_factory)),
      (std::unique_ptr<VideoDecoderFactory>(decoder_factory)), trials);
  const SdpVideoFormat vp8_format("VP8");
  const std::vector<SdpVideoFormat> supported_formats = {vp8_format};
  EXPECT_CALL(*encoder_factory, GetSupportedFormats())
      .WillRepeatedly(Return(supported_formats));
  EXPECT_CALL(*decoder_factory, GetSupportedFormats())
      .WillRepeatedly(Return(supported_formats));

  // Verify the codecs from the engine.
  const std::vector<Codec> engine_codecs = engine.LegacySendCodecs();
  // Verify default codecs has been added correctly.
  EXPECT_EQ(5u, engine_codecs.size());
  EXPECT_EQ("VP8", engine_codecs.at(0).name);

  // RTX codec for VP8.
  EXPECT_EQ("rtx", engine_codecs.at(1).name);
  int vp8_associated_payload;
  EXPECT_TRUE(engine_codecs.at(1).GetParam(kCodecParamAssociatedPayloadType,
                                           &vp8_associated_payload));
  EXPECT_EQ(vp8_associated_payload, engine_codecs.at(0).id);

  EXPECT_EQ(kRedCodecName, engine_codecs.at(2).name);

  // RTX codec for RED.
  EXPECT_EQ("rtx", engine_codecs.at(3).name);
  int red_associated_payload;
  EXPECT_TRUE(engine_codecs.at(3).GetParam(kCodecParamAssociatedPayloadType,
                                           &red_associated_payload));
  EXPECT_EQ(red_associated_payload, engine_codecs.at(2).id);

  EXPECT_EQ(kUlpfecCodecName, engine_codecs.at(4).name);

  int associated_payload_type;
  EXPECT_TRUE(engine_codecs.at(1).GetParam(kCodecParamAssociatedPayloadType,
                                           &associated_payload_type));
  EXPECT_EQ(engine_codecs.at(0).id, associated_payload_type);
  // Verify default parameters has been added to the VP8 codec.
  VerifyCodecHasDefaultFeedbackParams(engine_codecs.at(0),
                                      /*lntf_expected=*/false);

  // Mock encoder creation. `engine` take ownership of the encoder.
  const SdpVideoFormat format("VP8");
  EXPECT_CALL(*encoder_factory, Create(_, format)).WillOnce([&] {
    return std::make_unique<FakeWebRtcVideoEncoder>(nullptr);
  });

  // Expect no decoder to be created at this point. The decoder will only be
  // created if we receive payload data.
  EXPECT_CALL(*decoder_factory, Create).Times(0);

  // Create a call.
  GlobalSimulatedTimeController time_controller(Timestamp::Millis(4711));
  const Environment env = CreateTestEnvironment({.time = &time_controller});
  const std::unique_ptr<Call> call =
      Call::Create(CallConfig::CreateSingleThreaded(env));

  // Create send channel.
  const int send_ssrc = 123;
  std::unique_ptr<VideoMediaSendChannelInterface> send_channel =
      engine.CreateSendChannel(env, call.get(), GetMediaConfig(),
                               VideoOptions(), CryptoOptions(),
                               rate_allocator_factory.get(), nullptr, nullptr);

  VideoSenderParameters send_parameters;
  send_parameters.codecs.push_back(engine_codecs.at(0));
  EXPECT_TRUE(send_channel->SetSenderParameters(send_parameters));
  send_channel->OnReadyToSend(true);
  EXPECT_TRUE(
      send_channel->AddSendStream(StreamParams::CreateLegacy(send_ssrc)));
  EXPECT_TRUE(send_channel->SetSend(true));

  // Set capturer.
  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 30,
                               env.clock().CurrentTime());
  EXPECT_TRUE(send_channel->SetVideoSend(send_ssrc, nullptr, &frame_forwarder));
  // Sending one frame will allocate the encoder.
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  time_controller.AdvanceTime(TimeDelta::Zero());

  // Create recv channel.
  const int recv_ssrc = 321;
  std::unique_ptr<VideoMediaReceiveChannelInterface> receive_channel =
      engine.CreateReceiveChannel(
          env, call.get(), GetMediaConfig(), CryptoOptions(), nullptr,
          [](uint32_t, const RtpPacketInfos&, Timestamp) {});

  VideoReceiverParameters recv_parameters;
  recv_parameters.codecs.push_back(engine_codecs.at(0));
  EXPECT_TRUE(receive_channel->SetReceiverParameters(recv_parameters));
  EXPECT_TRUE(
      receive_channel->AddRecvStream(StreamParams::CreateLegacy(recv_ssrc)));

  // Remove streams previously added to free the encoder and decoder instance.
  EXPECT_CALL(*encoder_factory, Die());
  EXPECT_CALL(*decoder_factory, Die());
  EXPECT_CALL(*rate_allocator_factory, Die());
  EXPECT_TRUE(send_channel->RemoveSendStream(send_ssrc));
  EXPECT_TRUE(receive_channel->RemoveRecvStream(recv_ssrc));
}

TEST_F(WebRtcVideoEngineTest, DISABLED_RecreatesEncoderOnContentTypeChange) {
  encoder_factory_->AddSupportedVideoCodecType("VP8");
  auto fake_call = std::make_unique<FakeCall>(env_);
  auto send_channel = SetSendParamsWithAllSupportedCodecs();

  ASSERT_TRUE(send_channel->AddSendStream(StreamParams::CreateLegacy(kSsrc)));
  Codec codec = GetEngineCodec("VP8");
  VideoSenderParameters parameters;
  parameters.codecs.push_back(codec);
  send_channel->OnReadyToSend(true);
  send_channel->SetSend(true);
  ASSERT_TRUE(send_channel->SetSenderParameters(parameters));

  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 30,
                               env_.clock().CurrentTime());
  VideoOptions options;
  EXPECT_TRUE(send_channel->SetVideoSend(kSsrc, &options, &frame_forwarder));

  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  ASSERT_TRUE(time_controller_.Wait(
      [&] { return encoder_factory_->GetNumCreatedEncoders() >= 1; }));
  EXPECT_EQ(VideoCodecMode::kRealtimeVideo,
            encoder_factory_->encoders().back()->GetCodecSettings().mode);

  EXPECT_TRUE(send_channel->SetVideoSend(kSsrc, &options, &frame_forwarder));
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  // No change in content type, keep current encoder.
  EXPECT_EQ(1, encoder_factory_->GetNumCreatedEncoders());

  options.is_screencast.emplace(true);
  EXPECT_TRUE(send_channel->SetVideoSend(kSsrc, &options, &frame_forwarder));
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  // Change to screen content, recreate encoder. For the simulcast encoder
  // adapter case, this will result in two calls since InitEncode triggers a
  // a new instance.
  ASSERT_TRUE(time_controller_.Wait(
      [&] { return encoder_factory_->GetNumCreatedEncoders() >= 2; }));
  EXPECT_EQ(VideoCodecMode::kScreensharing,
            encoder_factory_->encoders().back()->GetCodecSettings().mode);

  EXPECT_TRUE(send_channel->SetVideoSend(kSsrc, &options, &frame_forwarder));
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  // Still screen content, no need to update encoder.
  EXPECT_EQ(2, encoder_factory_->GetNumCreatedEncoders());

  options.is_screencast.emplace(false);
  options.video_noise_reduction.emplace(false);
  EXPECT_TRUE(send_channel->SetVideoSend(kSsrc, &options, &frame_forwarder));
  // Change back to regular video content, update encoder. Also change
  // a non `is_screencast` option just to verify it doesn't affect recreation.
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  ASSERT_TRUE(time_controller_.Wait(
      [&] { return encoder_factory_->GetNumCreatedEncoders() >= 3; }));
  EXPECT_EQ(VideoCodecMode::kRealtimeVideo,
            encoder_factory_->encoders().back()->GetCodecSettings().mode);

  // Remove stream previously added to free the external encoder instance.
  EXPECT_TRUE(send_channel->RemoveSendStream(kSsrc));
  EXPECT_EQ(0u, encoder_factory_->encoders().size());
}

TEST_F(WebRtcVideoEngineTest, SetVideoRtxEnabled) {
  AddSupportedVideoCodecType("VP8");
  std::vector<Codec> send_codecs;
  std::vector<Codec> recv_codecs;

  // Don't want RTX
  send_codecs = engine_->LegacySendCodecs(false);
  EXPECT_FALSE(HasAnyRtxCodec(send_codecs));
  recv_codecs = engine_->LegacyRecvCodecs(false);
  EXPECT_FALSE(HasAnyRtxCodec(recv_codecs));

  // Want RTX
  send_codecs = engine_->LegacySendCodecs(true);
  EXPECT_TRUE(HasAnyRtxCodec(send_codecs));
  recv_codecs = engine_->LegacyRecvCodecs(true);
  EXPECT_TRUE(HasAnyRtxCodec(recv_codecs));
}

class WebRtcVideoChannelEncodedFrameCallbackTest : public ::testing::Test {
 protected:
  WebRtcVideoChannelEncodedFrameCallbackTest()
      : field_trials_(CreateTestFieldTrials()),
        env_(CreateTestEnvironment(
            {.field_trials = &field_trials_, .time = &time_controller_})),
        call_(Call::Create(CallConfig::CreateSingleThreaded(env_))),
        video_bitrate_allocator_factory_(
            CreateBuiltinVideoBitrateAllocatorFactory()),
        engine_(
            std::make_unique<
                VideoEncoderFactoryTemplate<LibvpxVp8EncoderTemplateAdapter,
                                            LibvpxVp9EncoderTemplateAdapter,
                                            OpenH264EncoderTemplateAdapter,
                                            LibaomAv1EncoderTemplateAdapter>>(),
            std::make_unique<FunctionVideoDecoderFactory>(
                []() { return std::make_unique<test::FakeDecoder>(); },
                kSdpVideoFormats),
            field_trials_),
        network_interface_(env_) {
    send_channel_ = engine_.CreateSendChannel(
        env_, call_.get(), MediaConfig(), VideoOptions(), CryptoOptions(),
        video_bitrate_allocator_factory_.get(), nullptr, nullptr);
    receive_channel_ = engine_.CreateReceiveChannel(
        env_, call_.get(), MediaConfig(), CryptoOptions(), nullptr,
        [](uint32_t, const RtpPacketInfos&, Timestamp) {});

    network_interface_.SetDestination(receive_channel_.get());
    send_channel_->SetInterface(&network_interface_);
    receive_channel_->SetInterface(&network_interface_);
    VideoReceiverParameters parameters;
    parameters.codecs = engine_.LegacyRecvCodecs();
    receive_channel_->SetReceiverParameters(parameters);
    receive_channel_->SetReceive(true);
  }

  ~WebRtcVideoChannelEncodedFrameCallbackTest() override {
    send_channel_->SetInterface(nullptr);
    receive_channel_->SetInterface(nullptr);
    send_channel_.reset();
    receive_channel_.reset();
  }

  void DeliverKeyFrame(uint32_t ssrc) {
    receive_channel_->OnPacketReceived(BuildVp8KeyFrame(ssrc, 96));
  }

  void DeliverKeyFrameAndWait(uint32_t ssrc) {
    DeliverKeyFrame(ssrc);
    time_controller_.AdvanceTime(kFrameDuration);
    EXPECT_EQ(renderer_.num_rendered_frames(), 1);
  }

  static const std::vector<SdpVideoFormat> kSdpVideoFormats;
  GlobalSimulatedTimeController time_controller_{Timestamp::Seconds(1000)};
  FieldTrials field_trials_;
  Environment env_;
  std::unique_ptr<Call> call_;
  std::unique_ptr<VideoBitrateAllocatorFactory>
      video_bitrate_allocator_factory_;
  WebRtcVideoEngine engine_;
  std::unique_ptr<VideoMediaSendChannelInterface> send_channel_;
  std::unique_ptr<VideoMediaReceiveChannelInterface> receive_channel_;
  FakeNetworkInterface network_interface_;
  FakeVideoRenderer renderer_;
};

const std::vector<SdpVideoFormat>
    WebRtcVideoChannelEncodedFrameCallbackTest::kSdpVideoFormats = {
        SdpVideoFormat::VP8()};

TEST_F(WebRtcVideoChannelEncodedFrameCallbackTest,
       SetEncodedFrameBufferFunction_DefaultStream) {
  testing::MockFunction<void(const RecordableEncodedFrame&)> callback;
  EXPECT_CALL(callback, Call);
  EXPECT_TRUE(receive_channel_->AddDefaultRecvStreamForTesting(
      StreamParams::CreateLegacy(kSsrc)));
  receive_channel_->SetRecordableEncodedFrameCallback(/*ssrc=*/0,
                                                      callback.AsStdFunction());
  EXPECT_TRUE(receive_channel_->SetSink(kSsrc, &renderer_));
  DeliverKeyFrame(kSsrc);
  time_controller_.AdvanceTime(kFrameDuration);
  EXPECT_EQ(renderer_.num_rendered_frames(), 1);
  receive_channel_->RemoveRecvStream(kSsrc);
}

TEST_F(WebRtcVideoChannelEncodedFrameCallbackTest,
       SetEncodedFrameBufferFunction_MatchSsrcWithDefaultStream) {
  testing::MockFunction<void(const RecordableEncodedFrame&)> callback;
  EXPECT_CALL(callback, Call);
  EXPECT_TRUE(receive_channel_->AddDefaultRecvStreamForTesting(
      StreamParams::CreateLegacy(kSsrc)));
  EXPECT_TRUE(receive_channel_->SetSink(kSsrc, &renderer_));
  receive_channel_->SetRecordableEncodedFrameCallback(kSsrc,
                                                      callback.AsStdFunction());
  DeliverKeyFrame(kSsrc);
  time_controller_.AdvanceTime(kFrameDuration);
  EXPECT_EQ(renderer_.num_rendered_frames(), 1);
  receive_channel_->RemoveRecvStream(kSsrc);
}

TEST_F(WebRtcVideoChannelEncodedFrameCallbackTest,
       SetEncodedFrameBufferFunction_MatchSsrc) {
  testing::MockFunction<void(const RecordableEncodedFrame&)> callback;
  EXPECT_CALL(callback, Call);
  EXPECT_TRUE(
      receive_channel_->AddRecvStream(StreamParams::CreateLegacy(kSsrc)));
  EXPECT_TRUE(receive_channel_->SetSink(kSsrc, &renderer_));
  receive_channel_->SetRecordableEncodedFrameCallback(kSsrc,
                                                      callback.AsStdFunction());
  DeliverKeyFrame(kSsrc);
  time_controller_.AdvanceTime(kFrameDuration);
  EXPECT_EQ(renderer_.num_rendered_frames(), 1);
  receive_channel_->RemoveRecvStream(kSsrc);
}

TEST_F(WebRtcVideoChannelEncodedFrameCallbackTest,
       SetEncodedFrameBufferFunction_MismatchSsrc) {
  testing::StrictMock<
      testing::MockFunction<void(const RecordableEncodedFrame&)>>
      callback;
  EXPECT_TRUE(
      receive_channel_->AddRecvStream(StreamParams::CreateLegacy(kSsrc + 1)));
  EXPECT_TRUE(receive_channel_->SetSink(kSsrc + 1, &renderer_));
  receive_channel_->SetRecordableEncodedFrameCallback(kSsrc,
                                                      callback.AsStdFunction());
  DeliverKeyFrame(kSsrc);  // Expected to not cause function to fire.
  DeliverKeyFrameAndWait(kSsrc + 1);
  receive_channel_->RemoveRecvStream(kSsrc + 1);
}

TEST_F(WebRtcVideoChannelEncodedFrameCallbackTest,
       SetEncodedFrameBufferFunction_MismatchSsrcWithDefaultStream) {
  testing::StrictMock<
      testing::MockFunction<void(const RecordableEncodedFrame&)>>
      callback;
  EXPECT_TRUE(receive_channel_->AddDefaultRecvStreamForTesting(
      StreamParams::CreateLegacy(kSsrc + 1)));
  EXPECT_TRUE(receive_channel_->SetSink(kSsrc + 1, &renderer_));
  receive_channel_->SetRecordableEncodedFrameCallback(kSsrc,
                                                      callback.AsStdFunction());
  receive_channel_->SetDefaultSink(&renderer_);
  DeliverKeyFrame(kSsrc);  // Expected to not cause function to fire.
  DeliverKeyFrameAndWait(kSsrc + 1);
  receive_channel_->RemoveRecvStream(kSsrc + 1);
}

TEST_F(WebRtcVideoChannelEncodedFrameCallbackTest, DoesNotDecodeWhenDisabled) {
  testing::MockFunction<void(const RecordableEncodedFrame&)> callback;
  EXPECT_CALL(callback, Call);
  EXPECT_TRUE(receive_channel_->AddDefaultRecvStreamForTesting(
      StreamParams::CreateLegacy(kSsrc)));
  receive_channel_->SetRecordableEncodedFrameCallback(/*ssrc=*/0,
                                                      callback.AsStdFunction());
  EXPECT_TRUE(receive_channel_->SetSink(kSsrc, &renderer_));
  receive_channel_->SetReceive(false);
  DeliverKeyFrame(kSsrc);
  time_controller_.AdvanceTime(kFrameDuration);
  EXPECT_EQ(renderer_.num_rendered_frames(), 0);

  receive_channel_->SetReceive(true);
  DeliverKeyFrame(kSsrc);
  time_controller_.AdvanceTime(kFrameDuration);
  EXPECT_EQ(renderer_.num_rendered_frames(), 1);

  receive_channel_->SetReceive(false);
  DeliverKeyFrame(kSsrc);
  time_controller_.AdvanceTime(kFrameDuration);
  EXPECT_EQ(renderer_.num_rendered_frames(), 1);
  receive_channel_->RemoveRecvStream(kSsrc);
}

class WebRtcVideoChannelBaseTest : public ::testing::Test {
 protected:
  WebRtcVideoChannelBaseTest()
      : field_trials_(CreateTestFieldTrials()),
        env_(CreateTestEnvironment({.field_trials = field_trials_.CreateCopy(),
                                    .time = &time_controller_})),
        video_bitrate_allocator_factory_(
            CreateBuiltinVideoBitrateAllocatorFactory()),
        engine_(std::make_unique<WebRtcVideoEngine>(
            std::make_unique<
                VideoEncoderFactoryTemplate<LibvpxVp8EncoderTemplateAdapter,
                                            LibvpxVp9EncoderTemplateAdapter,
                                            OpenH264EncoderTemplateAdapter,
                                            LibaomAv1EncoderTemplateAdapter>>(),
            std::make_unique<
                VideoDecoderFactoryTemplate<LibvpxVp8DecoderTemplateAdapter,
                                            LibvpxVp9DecoderTemplateAdapter,
                                            OpenH264DecoderTemplateAdapter,
                                            Dav1dDecoderTemplateAdapter>>(),
            env_.field_trials())),
        network_interface_(env_) {}

  void SetUp() override {
    // One testcase calls SetUp in a loop, only create call_ once.
    if (!call_) {
      call_ = Call::Create(CallConfig::CreateSingleThreaded(env_));
    }

    MediaConfig media_config;
    // Disabling cpu overuse detection actually disables quality scaling too; it
    // implies DegradationPreference kMaintainResolution. Automatic scaling
    // needs to be disabled, otherwise, tests which check the size of received
    // frames become flaky.
    media_config.video.enable_cpu_adaptation = false;
    send_channel_ = engine_->CreateSendChannel(
        env_, call_.get(), media_config, VideoOptions(), CryptoOptions(),
        video_bitrate_allocator_factory_.get(), nullptr, nullptr);
    receive_channel_ = engine_->CreateReceiveChannel(
        env_, call_.get(), media_config, CryptoOptions(), nullptr,
        [](uint32_t, const RtpPacketInfos&, Timestamp) {});
    send_channel_->OnReadyToSend(true);
    receive_channel_->SetReceive(true);
    network_interface_.SetDestination(receive_channel_.get());
    send_channel_->SetInterface(&network_interface_);
    receive_channel_->SetInterface(&network_interface_);
    VideoReceiverParameters parameters;
    parameters.codecs = engine_->LegacySendCodecs();
    receive_channel_->SetReceiverParameters(parameters);
    EXPECT_TRUE(send_channel_->AddSendStream(DefaultSendStreamParams()));
    frame_forwarder_ = std::make_unique<FrameForwarder>();
    frame_source_ = std::make_unique<FakeFrameSource>(
        640, 480, TimeDelta::Seconds(1) / kFramerate,
        env_.clock().CurrentTime());
    EXPECT_TRUE(
        send_channel_->SetVideoSend(kSsrc, nullptr, frame_forwarder_.get()));
  }

  // Returns pointer to implementation of the send channel.
  WebRtcVideoSendChannel* SendImpl() {
    // Note that this function requires intimate knowledge of how the channel
    // was created.
    return static_cast<WebRtcVideoSendChannel*>(send_channel_.get());
  }

  // Utility method to setup an additional stream to send and receive video.
  // Used to test send and recv between two streams.
  void SetUpSecondStream() {
    SetUpSecondStreamWithNoRecv();
    // Setup recv for second stream.
    EXPECT_TRUE(
        receive_channel_->AddRecvStream(StreamParams::CreateLegacy(kSsrc + 2)));
    // Make the second renderer available for use by a new stream.
    EXPECT_TRUE(receive_channel_->SetSink(kSsrc + 2, &renderer2_));
  }

  // Setup an additional stream just to send video. Defer add recv stream.
  // This is required if you want to test unsignalled recv of video rtp packets.
  void SetUpSecondStreamWithNoRecv() {
    // SetUp() already added kSsrc make sure duplicate SSRCs cant be added.
    EXPECT_TRUE(
        receive_channel_->AddRecvStream(StreamParams::CreateLegacy(kSsrc)));
    EXPECT_TRUE(receive_channel_->SetSink(kSsrc, &renderer_));
    EXPECT_FALSE(
        send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrc)));
    EXPECT_TRUE(
        send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrc + 2)));
    // We dont add recv for the second stream.

    // Setup the receive and renderer for second stream after send.
    frame_forwarder_2_ = std::make_unique<FrameForwarder>();
    EXPECT_TRUE(send_channel_->SetVideoSend(kSsrc + 2, nullptr,
                                            frame_forwarder_2_.get()));
  }

  void TearDown() override {
    send_channel_->SetInterface(nullptr);
    receive_channel_->SetInterface(nullptr);
    send_channel_.reset();
    receive_channel_.reset();
  }

  void ResetTest() {
    env_ = CreateTestEnvironment({.field_trials = field_trials_.CreateCopy(),
                                  .time = &time_controller_});
    TearDown();
    SetUp();
  }

  bool SetDefaultCodec() { return SetOneCodec(DefaultCodec()); }

  bool SetOneCodec(const Codec& codec) {
    frame_source_ = std::make_unique<FakeFrameSource>(
        kVideoWidth, kVideoHeight, TimeDelta::Seconds(1) / kFramerate,
        env_.clock().CurrentTime());

    bool sending = SendImpl()->sending();
    bool success = SetSend(false);
    if (success) {
      VideoSenderParameters parameters;
      parameters.codecs.push_back(codec);
      success = send_channel_->SetSenderParameters(parameters);
    }
    if (success) {
      success = SetSend(sending);
    }
    return success;
  }
  bool SetSend(bool send) { return send_channel_->SetSend(send); }
  void SendFrame() {
    if (frame_forwarder_2_) {
      frame_forwarder_2_->IncomingCapturedFrame(frame_source_->GetFrame());
    }
    frame_forwarder_->IncomingCapturedFrame(frame_source_->GetFrame());
    time_controller_.AdvanceTime(kFrameDuration);
  }
  bool WaitAndSendFrame(int wait_ms) {
    time_controller_.AdvanceTime(TimeDelta::Millis(wait_ms));
    SendFrame();
    return true;
  }
  int NumRtpBytes() { return network_interface_.NumRtpBytes(); }
  int NumRtpBytes(uint32_t ssrc) {
    return network_interface_.NumRtpBytes(ssrc);
  }
  int NumRtpPackets() { return network_interface_.NumRtpPackets(); }
  int NumRtpPackets(uint32_t ssrc) {
    return network_interface_.NumRtpPackets(ssrc);
  }
  int NumSentSsrcs() { return network_interface_.NumSentSsrcs(); }
  CopyOnWriteBuffer GetRtpPacket(int index) {
    return network_interface_.GetRtpPacket(index);
  }
  static int GetPayloadType(CopyOnWriteBuffer p) {
    RtpPacket header;
    EXPECT_TRUE(header.Parse(std::move(p)));
    return header.PayloadType();
  }

  // Tests that we can send and receive frames.
  void SendAndReceive(const Codec& codec) {
    EXPECT_TRUE(SetOneCodec(codec));
    EXPECT_TRUE(SetSend(true));
    receive_channel_->SetDefaultSink(&renderer_);
    EXPECT_EQ(0, renderer_.num_rendered_frames());
    SendFrame();
    EXPECT_FRAME(1, kVideoWidth, kVideoHeight);
    EXPECT_EQ(codec.id, GetPayloadType(GetRtpPacket(0)));
  }

  void SendReceiveManyAndGetStats(const Codec& codec,
                                  int duration_sec,
                                  int fps) {
    EXPECT_TRUE(SetOneCodec(codec));
    EXPECT_TRUE(SetSend(true));
    receive_channel_->SetDefaultSink(&renderer_);
    EXPECT_EQ(0, renderer_.num_rendered_frames());
    for (int i = 0; i < duration_sec; ++i) {
      for (int frame = 1; frame <= fps; ++frame) {
        EXPECT_TRUE(WaitAndSendFrame(1000 / fps));
        EXPECT_FRAME(frame + i * fps, kVideoWidth, kVideoHeight);
      }
    }
    EXPECT_EQ(codec.id, GetPayloadType(GetRtpPacket(0)));
  }

  VideoSenderInfo GetSenderStats(size_t i) {
    VideoMediaSendInfo send_info;
    EXPECT_TRUE(send_channel_->GetStats(&send_info));
    return send_info.senders[i];
  }

  VideoReceiverInfo GetReceiverStats(size_t i) {
    VideoMediaReceiveInfo info;
    EXPECT_TRUE(receive_channel_->GetStats(&info));
    return info.receivers[i];
  }

  // Two streams one channel tests.

  // Tests that we can send and receive frames.
  void TwoStreamsSendAndReceive(const Codec& codec) {
    SetUpSecondStream();
    // Test sending and receiving on first stream.
    SendAndReceive(codec);
    // Test sending and receiving on second stream.
    EXPECT_EQ(renderer2_.num_rendered_frames(), 1);
    EXPECT_GT(NumRtpPackets(), 0);
  }

  Codec GetEngineCodec(const std::string& name) {
    for (const Codec& engine_codec : engine_->LegacySendCodecs()) {
      if (absl::EqualsIgnoreCase(name, engine_codec.name))
        return engine_codec;
    }
    // This point should never be reached.
    ADD_FAILURE() << "Unrecognized codec name: " << name;
    return CreateVideoCodec(0, "");
  }

  Codec DefaultCodec() { return GetEngineCodec("VP8"); }

  StreamParams DefaultSendStreamParams() {
    return StreamParams::CreateLegacy(kSsrc);
  }

  GlobalSimulatedTimeController time_controller_{Timestamp::Seconds(1000)};

  FieldTrials field_trials_;
  Environment env_;
  std::unique_ptr<Call> call_;
  std::unique_ptr<VideoBitrateAllocatorFactory>
      video_bitrate_allocator_factory_;
  std::unique_ptr<WebRtcVideoEngine> engine_;

  std::unique_ptr<FakeFrameSource> frame_source_;
  std::unique_ptr<FrameForwarder> frame_forwarder_;
  std::unique_ptr<FrameForwarder> frame_forwarder_2_;

  std::unique_ptr<VideoMediaSendChannelInterface> send_channel_;
  std::unique_ptr<VideoMediaReceiveChannelInterface> receive_channel_;
  FakeNetworkInterface network_interface_;
  FakeVideoRenderer renderer_;

  // Used by test cases where 2 streams are run on the same channel.
  FakeVideoRenderer renderer2_;
};

// Test that SetSend works.
TEST_F(WebRtcVideoChannelBaseTest, SetSend) {
  EXPECT_FALSE(SendImpl()->sending());
  EXPECT_TRUE(
      send_channel_->SetVideoSend(kSsrc, nullptr, frame_forwarder_.get()));
  EXPECT_TRUE(SetOneCodec(DefaultCodec()));
  EXPECT_FALSE(SendImpl()->sending());
  EXPECT_TRUE(SetSend(true));
  EXPECT_TRUE(SendImpl()->sending());
  SendFrame();
  EXPECT_GT(NumRtpPackets(), 0);
  EXPECT_TRUE(SetSend(false));
  EXPECT_FALSE(SendImpl()->sending());
}

// Test that SetSend fails without codecs being set.
TEST_F(WebRtcVideoChannelBaseTest, SetSendWithoutCodecs) {
  EXPECT_FALSE(SendImpl()->sending());
  EXPECT_FALSE(SetSend(true));
  EXPECT_FALSE(SendImpl()->sending());
}

// Test that we properly set the send and recv buffer sizes by the time
// SetSend is called.
TEST_F(WebRtcVideoChannelBaseTest, SetSendSetsTransportBufferSizes) {
  EXPECT_TRUE(SetOneCodec(DefaultCodec()));
  EXPECT_TRUE(SetSend(true));
  EXPECT_EQ(kVideoRtpSendBufferSize, network_interface_.sendbuf_size());
  EXPECT_EQ(kVideoRtpRecvBufferSize, network_interface_.recvbuf_size());
}

// Test that stats work properly for a 1-1 call.
TEST_F(WebRtcVideoChannelBaseTest, GetStats) {
  const int kDurationSec = 3;
  const int kFps = 10;
  SendReceiveManyAndGetStats(DefaultCodec(), kDurationSec, kFps);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  ASSERT_EQ(1U, send_info.senders.size());
  // TODO(whyuan): bytes_sent and bytes_received are different. Are both
  // payload? For webrtc, bytes_sent does not include the RTP header length.
  EXPECT_EQ(send_info.senders[0].payload_bytes_sent,
            NumRtpBytes() - kRtpHeaderSize * NumRtpPackets());
  EXPECT_EQ(NumRtpPackets(), send_info.senders[0].packets_sent);
  EXPECT_EQ(0.0, send_info.senders[0].fraction_lost);
  ASSERT_TRUE(send_info.senders[0].codec_payload_type);
  EXPECT_EQ(DefaultCodec().id, *send_info.senders[0].codec_payload_type);
  EXPECT_EQ(0, send_info.senders[0].firs_received);
  EXPECT_EQ(0, send_info.senders[0].plis_received);
  EXPECT_EQ(0u, send_info.senders[0].nacks_received);
  EXPECT_EQ(kVideoWidth, send_info.senders[0].send_frame_width);
  EXPECT_EQ(kVideoHeight, send_info.senders[0].send_frame_height);
  EXPECT_GT(send_info.senders[0].framerate_input, 0);
  EXPECT_GT(send_info.senders[0].framerate_sent, 0);

  EXPECT_EQ(1U, send_info.send_codecs.count(DefaultCodec().id));
  EXPECT_EQ(DefaultCodec().ToCodecParameters(),
            send_info.send_codecs[DefaultCodec().id]);

  ASSERT_EQ(1U, receive_info.receivers.size());
  EXPECT_EQ(1U, send_info.senders[0].ssrcs().size());
  EXPECT_EQ(1U, receive_info.receivers[0].ssrcs().size());
  EXPECT_EQ(send_info.senders[0].ssrcs()[0],
            receive_info.receivers[0].ssrcs()[0]);
  ASSERT_TRUE(receive_info.receivers[0].codec_payload_type);
  EXPECT_EQ(DefaultCodec().id, *receive_info.receivers[0].codec_payload_type);
  EXPECT_EQ(NumRtpBytes() - kRtpHeaderSize * NumRtpPackets(),
            receive_info.receivers[0].payload_bytes_received);
  EXPECT_EQ(NumRtpPackets(), receive_info.receivers[0].packets_received);
  EXPECT_EQ(0, receive_info.receivers[0].packets_lost);
  // TODO(asapersson): Not set for webrtc. Handle missing stats.
  // EXPECT_EQ(0, receive_info.receivers[0].packets_concealed);
  EXPECT_EQ(0, receive_info.receivers[0].firs_sent);
  EXPECT_EQ(0, receive_info.receivers[0].plis_sent);
  EXPECT_EQ(0U, receive_info.receivers[0].nacks_sent);
  EXPECT_EQ(kVideoWidth, receive_info.receivers[0].frame_width);
  EXPECT_EQ(kVideoHeight, receive_info.receivers[0].frame_height);
  EXPECT_GT(receive_info.receivers[0].framerate_received, 0);
  EXPECT_GT(receive_info.receivers[0].framerate_decoded, 0);
  EXPECT_GT(receive_info.receivers[0].framerate_output, 0);
  EXPECT_GT(receive_info.receivers[0].jitter_buffer_delay_seconds, 0.0);
  EXPECT_GT(receive_info.receivers[0].jitter_buffer_emitted_count, 0u);

  EXPECT_EQ(1U, receive_info.receive_codecs.count(DefaultCodec().id));
  EXPECT_EQ(DefaultCodec().ToCodecParameters(),
            receive_info.receive_codecs[DefaultCodec().id]);
}

// Test that stats work properly for a conf call with multiple recv streams.
TEST_F(WebRtcVideoChannelBaseTest, GetStatsMultipleRecvStreams) {
  FakeVideoRenderer renderer1, renderer2;
  EXPECT_TRUE(SetOneCodec(DefaultCodec()));
  VideoSenderParameters parameters;
  parameters.codecs.push_back(DefaultCodec());
  parameters.conference_mode = true;
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  EXPECT_TRUE(SetSend(true));
  EXPECT_TRUE(receive_channel_->AddRecvStream(StreamParams::CreateLegacy(1)));
  EXPECT_TRUE(receive_channel_->AddRecvStream(StreamParams::CreateLegacy(2)));
  EXPECT_TRUE(receive_channel_->SetSink(1, &renderer1));
  EXPECT_TRUE(receive_channel_->SetSink(2, &renderer2));
  EXPECT_EQ(0, renderer1.num_rendered_frames());
  EXPECT_EQ(0, renderer2.num_rendered_frames());
  std::vector<uint32_t> ssrcs;
  ssrcs.push_back(1);
  ssrcs.push_back(2);
  network_interface_.SetConferenceMode(true, ssrcs);
  SendFrame();
  EXPECT_FRAME_ON_RENDERER(renderer1, 1, kVideoWidth, kVideoHeight);
  EXPECT_FRAME_ON_RENDERER(renderer2, 1, kVideoWidth, kVideoHeight);

  EXPECT_TRUE(send_channel_->SetSend(false));

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  ASSERT_EQ(1U, send_info.senders.size());
  // TODO(whyuan): bytes_sent and bytes_received are different. Are both
  // payload? For webrtc, bytes_sent does not include the RTP header length.
  EXPECT_EQ(NumRtpBytes() - kRtpHeaderSize * NumRtpPackets(),
            GetSenderStats(0).payload_bytes_sent);
  EXPECT_EQ(NumRtpPackets(), GetSenderStats(0).packets_sent);
  EXPECT_EQ(kVideoWidth, GetSenderStats(0).send_frame_width);
  EXPECT_EQ(kVideoHeight, GetSenderStats(0).send_frame_height);

  ASSERT_EQ(2U, receive_info.receivers.size());
  for (size_t i = 0; i < receive_info.receivers.size(); ++i) {
    EXPECT_EQ(1U, GetReceiverStats(i).ssrcs().size());
    EXPECT_EQ(i + 1, GetReceiverStats(i).ssrcs()[0]);
    EXPECT_EQ(NumRtpBytes() - kRtpHeaderSize * NumRtpPackets(),
              GetReceiverStats(i).payload_bytes_received);
    EXPECT_EQ(NumRtpPackets(), GetReceiverStats(i).packets_received);
    EXPECT_EQ(kVideoWidth, GetReceiverStats(i).frame_width);
    EXPECT_EQ(kVideoHeight, GetReceiverStats(i).frame_height);
  }
}

// Test that stats work properly for a conf call with multiple send streams.
TEST_F(WebRtcVideoChannelBaseTest, GetStatsMultipleSendStreams) {
  // Normal setup; note that we set the SSRC explicitly to ensure that
  // it will come first in the senders map.
  EXPECT_TRUE(SetOneCodec(DefaultCodec()));
  VideoSenderParameters parameters;
  parameters.codecs.push_back(DefaultCodec());
  parameters.conference_mode = true;
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  EXPECT_TRUE(
      receive_channel_->AddRecvStream(StreamParams::CreateLegacy(kSsrc)));
  EXPECT_TRUE(receive_channel_->SetSink(kSsrc, &renderer_));
  EXPECT_TRUE(SetSend(true));
  SendFrame();
  EXPECT_GT(NumRtpPackets(), 0);
  EXPECT_FRAME(1, kVideoWidth, kVideoHeight);

  // Add an additional capturer, and hook up a renderer to receive it.
  FakeVideoRenderer renderer2;
  FrameForwarder frame_forwarder;
  const int kTestWidth = 160;
  const int kTestHeight = 120;
  FakeFrameSource frame_source(kTestWidth, kTestHeight,
                               TimeDelta::Seconds(1) / 5,
                               env_.clock().CurrentTime());
  EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(5678)));
  EXPECT_TRUE(send_channel_->SetVideoSend(5678, nullptr, &frame_forwarder));
  EXPECT_TRUE(
      receive_channel_->AddRecvStream(StreamParams::CreateLegacy(5678)));
  EXPECT_TRUE(receive_channel_->SetSink(5678, &renderer2));
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  time_controller_.AdvanceTime(kFrameDuration);
  EXPECT_FRAME_ON_RENDERER(renderer2, 1, kTestWidth, kTestHeight);

  // Get stats, and make sure they are correct for two senders
  VideoMediaSendInfo send_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));

  ASSERT_EQ(2U, send_info.senders.size());

  EXPECT_EQ(NumRtpPackets(), send_info.senders[0].packets_sent +
                                 send_info.senders[1].packets_sent);
  EXPECT_EQ(1U, send_info.senders[0].ssrcs().size());
  EXPECT_EQ(1234U, send_info.senders[0].ssrcs()[0]);
  EXPECT_EQ(kVideoWidth, send_info.senders[0].send_frame_width);
  EXPECT_EQ(kVideoHeight, send_info.senders[0].send_frame_height);
  EXPECT_EQ(1U, send_info.senders[1].ssrcs().size());
  EXPECT_EQ(5678U, send_info.senders[1].ssrcs()[0]);
  EXPECT_EQ(kTestWidth, send_info.senders[1].send_frame_width);
  EXPECT_EQ(kTestHeight, send_info.senders[1].send_frame_height);
  // The capturer must be unregistered here as it runs out of it's scope next.
  send_channel_->SetVideoSend(5678, nullptr, nullptr);
}

TEST_F(WebRtcVideoChannelBaseTest, GetStatsDoesNotResetAfterCodecChange) {
  const int kDurationSec = 3;
  const int kFps = 10;
  SendReceiveManyAndGetStats(GetEngineCodec("VP9"), kDurationSec, kFps);

  const Codec& new_codec = GetEngineCodec("VP8");
  EXPECT_TRUE(SetOneCodec(new_codec));
  EXPECT_TRUE(SetSend(true));
  VideoMediaSendInfo send_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  ASSERT_EQ(1U, send_info.senders.size());
  EXPECT_EQ(send_info.senders[0].payload_bytes_sent,
            NumRtpBytes() - kRtpHeaderSize * NumRtpPackets());
  EXPECT_EQ(NumRtpPackets(), send_info.senders[0].packets_sent);
  ASSERT_TRUE(send_info.senders[0].codec_payload_type);
  EXPECT_EQ(new_codec.id, *send_info.senders[0].codec_payload_type);
}

// Test that we can set the bandwidth.
TEST_F(WebRtcVideoChannelBaseTest, SetSendBandwidth) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(DefaultCodec());
  parameters.max_bandwidth_bps = -1;  // <= 0 means unlimited.
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  parameters.max_bandwidth_bps = 128 * 1024;
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
}

// Test that we can set the SSRC for the default send source.
TEST_F(WebRtcVideoChannelBaseTest, SetSendSsrc) {
  EXPECT_TRUE(SetDefaultCodec());
  EXPECT_TRUE(SetSend(true));
  SendFrame();
  EXPECT_GT(NumRtpPackets(), 0);
  RtpPacket header;
  EXPECT_TRUE(header.Parse(GetRtpPacket(0)));
  EXPECT_EQ(kSsrc, header.Ssrc());

  // Packets are being paced out, so these can mismatch between the first and
  // second call to NumRtpPackets until pending packets are paced out.
  EXPECT_EQ(NumRtpPackets(), NumRtpPackets(header.Ssrc()));
  EXPECT_EQ(NumRtpBytes(), NumRtpBytes(header.Ssrc()));
  EXPECT_EQ(1, NumSentSsrcs());
  EXPECT_EQ(0, NumRtpPackets(kSsrc - 1));
  EXPECT_EQ(0, NumRtpBytes(kSsrc - 1));
}

// Test that we can set the SSRC even after codecs are set.
TEST_F(WebRtcVideoChannelBaseTest, SetSendSsrcAfterSetCodecs) {
  // Remove stream added in Setup.
  EXPECT_TRUE(send_channel_->RemoveSendStream(kSsrc));
  EXPECT_TRUE(SetDefaultCodec());
  EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(999)));
  EXPECT_TRUE(
      send_channel_->SetVideoSend(999u, nullptr, frame_forwarder_.get()));
  EXPECT_TRUE(SetSend(true));
  EXPECT_TRUE(WaitAndSendFrame(0));
  EXPECT_GT(NumRtpPackets(), 0);
  RtpPacket header;
  EXPECT_TRUE(header.Parse(GetRtpPacket(0)));
  EXPECT_EQ(999u, header.Ssrc());
  // Packets are being paced out, so these can mismatch between the first and
  // second call to NumRtpPackets until pending packets are paced out.
  EXPECT_EQ(NumRtpPackets(), NumRtpPackets(header.Ssrc()));
  EXPECT_EQ(NumRtpBytes(), NumRtpBytes(header.Ssrc()));
  EXPECT_EQ(1, NumSentSsrcs());
  EXPECT_EQ(0, NumRtpPackets(kSsrc));
  EXPECT_EQ(0, NumRtpBytes(kSsrc));
}

// Test that we can set the default video renderer before and after
// media is received.
TEST_F(WebRtcVideoChannelBaseTest, SetSink) {
  RtpPacketReceived packet;
  packet.SetSsrc(kSsrc);
  receive_channel_->SetDefaultSink(nullptr);
  EXPECT_TRUE(SetDefaultCodec());
  EXPECT_TRUE(SetSend(true));
  EXPECT_EQ(0, renderer_.num_rendered_frames());
  receive_channel_->SetDefaultSink(&renderer_);
  receive_channel_->OnPacketReceived(packet);
  SendFrame();
  EXPECT_FRAME(1, kVideoWidth, kVideoHeight);
}

// Tests setting up and configuring a send stream.
TEST_F(WebRtcVideoChannelBaseTest, AddRemoveSendStreams) {
  EXPECT_TRUE(SetOneCodec(DefaultCodec()));
  EXPECT_TRUE(SetSend(true));
  receive_channel_->SetDefaultSink(&renderer_);
  SendFrame();
  EXPECT_FRAME(1, kVideoWidth, kVideoHeight);
  EXPECT_GT(NumRtpPackets(), 0);
  RtpPacket header;
  size_t last_packet = NumRtpPackets() - 1;
  EXPECT_TRUE(header.Parse(GetRtpPacket(static_cast<int>(last_packet))));
  EXPECT_EQ(kSsrc, header.Ssrc());

  // Remove the send stream that was added during Setup.
  EXPECT_TRUE(send_channel_->RemoveSendStream(kSsrc));
  int rtp_packets = NumRtpPackets();

  EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(789u)));
  EXPECT_TRUE(
      send_channel_->SetVideoSend(789u, nullptr, frame_forwarder_.get()));
  EXPECT_EQ(rtp_packets, NumRtpPackets());
  // Wait 30ms to guarantee the engine does not drop the frame.
  EXPECT_TRUE(WaitAndSendFrame(30));
  EXPECT_GT(NumRtpPackets(), rtp_packets);

  last_packet = NumRtpPackets() - 1;
  EXPECT_TRUE(header.Parse(GetRtpPacket(static_cast<int>(last_packet))));
  EXPECT_EQ(789u, header.Ssrc());
}

// Tests the behavior of incoming streams in a conference scenario.
TEST_F(WebRtcVideoChannelBaseTest, SimulateConference) {
  FakeVideoRenderer renderer1, renderer2;
  EXPECT_TRUE(SetDefaultCodec());
  VideoSenderParameters parameters;
  parameters.codecs.push_back(DefaultCodec());
  parameters.conference_mode = true;
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  EXPECT_TRUE(SetSend(true));
  EXPECT_TRUE(receive_channel_->AddRecvStream(StreamParams::CreateLegacy(1)));
  EXPECT_TRUE(receive_channel_->AddRecvStream(StreamParams::CreateLegacy(2)));
  EXPECT_TRUE(receive_channel_->SetSink(1, &renderer1));
  EXPECT_TRUE(receive_channel_->SetSink(2, &renderer2));
  EXPECT_EQ(0, renderer1.num_rendered_frames());
  EXPECT_EQ(0, renderer2.num_rendered_frames());
  std::vector<uint32_t> ssrcs;
  ssrcs.push_back(1);
  ssrcs.push_back(2);
  network_interface_.SetConferenceMode(true, ssrcs);
  SendFrame();
  EXPECT_FRAME_ON_RENDERER(renderer1, 1, kVideoWidth, kVideoHeight);
  EXPECT_FRAME_ON_RENDERER(renderer2, 1, kVideoWidth, kVideoHeight);

  EXPECT_EQ(DefaultCodec().id, GetPayloadType(GetRtpPacket(0)));
  EXPECT_EQ(kVideoWidth, renderer1.width());
  EXPECT_EQ(kVideoHeight, renderer1.height());
  EXPECT_EQ(kVideoWidth, renderer2.width());
  EXPECT_EQ(kVideoHeight, renderer2.height());
  EXPECT_TRUE(receive_channel_->RemoveRecvStream(2));
  EXPECT_TRUE(receive_channel_->RemoveRecvStream(1));
}

// Tests that we can add and remove capturers and frames are sent out properly
TEST_F(WebRtcVideoChannelBaseTest, DISABLED_AddRemoveCapturer) {
  Codec codec = DefaultCodec();
  const int time_between_send_ms = VideoFormat::FpsToInterval(kFramerate);
  EXPECT_TRUE(SetOneCodec(codec));
  EXPECT_TRUE(SetSend(true));
  receive_channel_->SetDefaultSink(&renderer_);
  EXPECT_EQ(0, renderer_.num_rendered_frames());
  SendFrame();
  EXPECT_FRAME(1, kVideoWidth, kVideoHeight);

  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(480, 360, TimeDelta::Seconds(1) / 30,
                               env_.clock().CurrentTime());

  // TODO(nisse): This testcase fails if we don't configure
  // screencast. It's unclear why, I see nothing obvious in this
  // test which is related to screencast logic.
  VideoOptions video_options;
  video_options.is_screencast = true;
  send_channel_->SetVideoSend(kSsrc, &video_options, nullptr);

  int captured_frames = 1;
  for (int iterations = 0; iterations < 2; ++iterations) {
    EXPECT_TRUE(send_channel_->SetVideoSend(kSsrc, nullptr, &frame_forwarder));
    time_controller_.AdvanceTime(TimeDelta::Millis(time_between_send_ms));
    frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());

    ++captured_frames;
    // Check if the right size was captured.
    EXPECT_TRUE(renderer_.num_rendered_frames() >= captured_frames &&
                480 == renderer_.width() && 360 == renderer_.height() &&
                !renderer_.black_frame());
    EXPECT_GE(renderer_.num_rendered_frames(), captured_frames);
    EXPECT_EQ(480, renderer_.width());
    EXPECT_EQ(360, renderer_.height());
    captured_frames = renderer_.num_rendered_frames() + 1;
    EXPECT_FALSE(renderer_.black_frame());
    EXPECT_TRUE(send_channel_->SetVideoSend(kSsrc, nullptr, nullptr));
    // Make sure a black frame was generated.
    // The black frame should have the resolution of the previous frame to
    // prevent expensive encoder reconfigurations.
    EXPECT_TRUE(renderer_.num_rendered_frames() >= captured_frames &&
                480 == renderer_.width() && 360 == renderer_.height() &&
                renderer_.black_frame());
    EXPECT_GE(renderer_.num_rendered_frames(), captured_frames);
    EXPECT_EQ(480, renderer_.width());
    EXPECT_EQ(360, renderer_.height());
    EXPECT_TRUE(renderer_.black_frame());

    // The black frame has the same timestamp as the next frame since it's
    // timestamp is set to the last frame's timestamp + interval. WebRTC will
    // not render a frame with the same timestamp so capture another frame
    // with the frame capturer to increment the next frame's timestamp.
    frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  }
}

// Tests that if SetVideoSend is called with a NULL capturer after the
// capturer was already removed, the application doesn't crash (and no black
// frame is sent).
TEST_F(WebRtcVideoChannelBaseTest, RemoveCapturerWithoutAdd) {
  EXPECT_TRUE(SetOneCodec(DefaultCodec()));
  EXPECT_TRUE(SetSend(true));
  receive_channel_->SetDefaultSink(&renderer_);
  EXPECT_EQ(0, renderer_.num_rendered_frames());
  SendFrame();
  EXPECT_FRAME(1, kVideoWidth, kVideoHeight);
  // Allow one frame so they don't get dropped because we send frames too
  // tightly.
  time_controller_.AdvanceTime(kFrameDuration);
  // Remove the capturer.
  EXPECT_TRUE(send_channel_->SetVideoSend(kSsrc, nullptr, nullptr));

  // No capturer was added, so this SetVideoSend shouldn't do anything.
  EXPECT_TRUE(send_channel_->SetVideoSend(kSsrc, nullptr, nullptr));
  time_controller_.AdvanceTime(TimeDelta::Millis(300));
  // Verify no more frames were sent.
  EXPECT_EQ(1, renderer_.num_rendered_frames());
}

// Tests that we can add and remove capturer as unique sources.
TEST_F(WebRtcVideoChannelBaseTest, AddRemoveCapturerMultipleSources) {
  // Set up the stream associated with the engine.
  EXPECT_TRUE(
      receive_channel_->AddRecvStream(StreamParams::CreateLegacy(kSsrc)));
  EXPECT_TRUE(receive_channel_->SetSink(kSsrc, &renderer_));
  VideoFormat capture_format(kVideoWidth, kVideoHeight,
                             VideoFormat::FpsToInterval(kFramerate),
                             FOURCC_I420);
  // Set up additional stream 1.
  FakeVideoRenderer renderer1;
  EXPECT_FALSE(receive_channel_->SetSink(1, &renderer1));
  EXPECT_TRUE(receive_channel_->AddRecvStream(StreamParams::CreateLegacy(1)));
  EXPECT_TRUE(receive_channel_->SetSink(1, &renderer1));
  EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(1)));

  FrameForwarder frame_forwarder1;
  FakeFrameSource frame_source(kVideoWidth, kVideoHeight,
                               TimeDelta::Seconds(1) / kFramerate,
                               env_.clock().CurrentTime());

  // Set up additional stream 2.
  FakeVideoRenderer renderer2;
  EXPECT_FALSE(receive_channel_->SetSink(2, &renderer2));
  EXPECT_TRUE(receive_channel_->AddRecvStream(StreamParams::CreateLegacy(2)));
  EXPECT_TRUE(receive_channel_->SetSink(2, &renderer2));
  EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(2)));
  FrameForwarder frame_forwarder2;

  // State for all the streams.
  EXPECT_TRUE(SetOneCodec(DefaultCodec()));
  // A limitation in the lmi implementation requires that SetVideoSend() is
  // called after SetOneCodec().
  // TODO(hellner): this seems like an unnecessary constraint, fix it.
  EXPECT_TRUE(send_channel_->SetVideoSend(1, nullptr, &frame_forwarder1));
  EXPECT_TRUE(send_channel_->SetVideoSend(2, nullptr, &frame_forwarder2));
  EXPECT_TRUE(SetSend(true));
  // Test capturer associated with engine.
  const int kTestWidth = 160;
  const int kTestHeight = 120;
  frame_forwarder1.IncomingCapturedFrame(frame_source.GetFrame(
      kTestWidth, kTestHeight, VideoRotation::kVideoRotation_0,
      TimeDelta::Seconds(1) / kFramerate));
  time_controller_.AdvanceTime(kFrameDuration);
  EXPECT_FRAME_ON_RENDERER(renderer1, 1, kTestWidth, kTestHeight);
  // Capture a frame with additional capturer2, frames should be received
  frame_forwarder2.IncomingCapturedFrame(frame_source.GetFrame(
      kTestWidth, kTestHeight, VideoRotation::kVideoRotation_0,
      TimeDelta::Seconds(1) / kFramerate));
  time_controller_.AdvanceTime(kFrameDuration);
  EXPECT_FRAME_ON_RENDERER(renderer2, 1, kTestWidth, kTestHeight);
  // Successfully remove the capturer.
  EXPECT_TRUE(send_channel_->SetVideoSend(kSsrc, nullptr, nullptr));
  // The capturers must be unregistered here as it runs out of it's scope
  // next.
  EXPECT_TRUE(send_channel_->SetVideoSend(1, nullptr, nullptr));
  EXPECT_TRUE(send_channel_->SetVideoSend(2, nullptr, nullptr));
}

// Tests empty StreamParams is rejected.
TEST_F(WebRtcVideoChannelBaseTest, RejectEmptyStreamParams) {
  // Remove the send stream that was added during Setup.
  EXPECT_TRUE(send_channel_->RemoveSendStream(kSsrc));

  StreamParams empty;
  EXPECT_FALSE(send_channel_->AddSendStream(empty));
  EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(789u)));
}

// Test that multiple send streams can be created and deleted properly.
TEST_F(WebRtcVideoChannelBaseTest, MultipleSendStreams) {
  // Remove stream added in Setup. I.e. remove stream corresponding to default
  // channel.
  EXPECT_TRUE(send_channel_->RemoveSendStream(kSsrc));
  const unsigned int kSsrcsSize = sizeof(kSsrcs4) / sizeof(kSsrcs4[0]);
  for (unsigned int i = 0; i < kSsrcsSize; ++i) {
    EXPECT_TRUE(
        send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrcs4[i])));
  }
  // Delete one of the non default channel streams, let the destructor delete
  // the remaining ones.
  EXPECT_TRUE(send_channel_->RemoveSendStream(kSsrcs4[kSsrcsSize - 1]));
  // Stream should already be deleted.
  EXPECT_FALSE(send_channel_->RemoveSendStream(kSsrcs4[kSsrcsSize - 1]));
}

TEST_F(WebRtcVideoChannelBaseTest, SendAndReceiveVp8Vga) {
  SendAndReceive(GetEngineCodec("VP8"));
}

TEST_F(WebRtcVideoChannelBaseTest, SendAndReceiveVp8Qvga) {
  SendAndReceive(GetEngineCodec("VP8"));
}

TEST_F(WebRtcVideoChannelBaseTest, SendAndReceiveVp8SvcQqvga) {
  SendAndReceive(GetEngineCodec("VP8"));
}

TEST_F(WebRtcVideoChannelBaseTest, TwoStreamsSendAndReceive) {
  // Set a high bitrate to not be downscaled by VP8 due to low initial start
  // bitrates. This currently happens at <250k, and two streams sharing 300k
  // initially will use QVGA instead of VGA.
  // TODO(pbos): Set up the quality scaler so that both senders reliably start
  // at QVGA, then verify that instead.
  Codec codec = GetEngineCodec("VP8");
  codec.SetParam(kCodecParamStartBitrate, "1000000");
  TwoStreamsSendAndReceive(codec);
}

TEST_F(WebRtcVideoChannelBaseTest,
       RequestEncoderFallbackNextCodecFollowNegotiatedOrder) {
  field_trials_.Set("WebRTC-SwitchEncoderFollowCodecPreferenceOrder",
                    "Enabled");
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  parameters.codecs.push_back(GetEngineCodec("AV1"));
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  std::optional<Codec> codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP9", codec->name);

  SendImpl()->RequestEncoderSwitch(std::nullopt, true);
  time_controller_.AdvanceTime(kFrameDuration);
  codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("AV1", codec->name);

  SendImpl()->RequestEncoderSwitch(std::nullopt, true);
  time_controller_.AdvanceTime(kFrameDuration);
  codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP8", codec->name);

  SendImpl()->RequestEncoderSwitch(std::nullopt, true);
  time_controller_.AdvanceTime(kFrameDuration);

  FrameForwarder frame_forwarder;
  EXPECT_TRUE(send_channel_->SetVideoSend(kSsrc, nullptr, &frame_forwarder));
  EXPECT_TRUE(send_channel_->RemoveSendStream(kSsrc));
}

TEST_F(WebRtcVideoChannelBaseTest,
       RequestEncoderFallbackDoesNotSkipFirstNegotiatedCodec) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("AV1"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  // Switch to a codec that is not first in the list, this emulates what happens
  // if SetParameters() is called.
  SendImpl()->RequestEncoderSwitch(SdpVideoFormat::VP8(), false);
  time_controller_.AdvanceTime(kFrameDuration);
  std::optional<Codec> codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP8", codec->name);

  // Fallback order: VP9, AV1 (because middle codec VP8 was already removed).
  SendImpl()->RequestEncoderSwitch(std::nullopt, true);
  time_controller_.AdvanceTime(kFrameDuration);
  codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP9", codec->name);
  SendImpl()->RequestEncoderSwitch(std::nullopt, true);
  time_controller_.AdvanceTime(kFrameDuration);
  codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("AV1", codec->name);
}

#if defined(RTC_ENABLE_VP9)

TEST_F(WebRtcVideoChannelBaseTest, RequestEncoderSwitchWithNullopt) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  std::optional<Codec> codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP9", codec->name);

  // RequestEncoderSwitch will post a task to the worker thread (which is also
  // the current thread), hence the ProcessMessages call.
  SendImpl()->RequestEncoderSwitch(std::nullopt, true);
  time_controller_.AdvanceTime(kFrameDuration);
  codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP8", codec->name);

  // No other codec to fall back to, keep using VP8.
  SendImpl()->RequestEncoderSwitch(std::nullopt, true);
  time_controller_.AdvanceTime(kFrameDuration);
  codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP8", codec->name);
}

TEST_F(WebRtcVideoChannelBaseTest, RequestEncoderSwitchWithNoCodecs) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  std::optional<Codec> codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP9", codec->name);

  // RequestEncoderSwitch with std::nullopt and allow_default_fallback=false.
  // Codec should still switch to the next available codec.
  // Since there's no other codec it should stay the same.
  SendImpl()->RequestEncoderSwitch(std::nullopt,
                                   /*allow_default_fallback=*/false);
  time_controller_.AdvanceTime(kFrameDuration);
  codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP9", codec->name);
}

TEST_F(WebRtcVideoChannelBaseTest, RequestEncoderSwitchDefaultFallback) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  std::optional<Codec> codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP9", codec->name);

  // RequestEncoderSwitch will post a task to the worker thread (which is also
  // the current thread), hence the ProcessMessages call.
  SendImpl()->RequestEncoderSwitch(SdpVideoFormat("UnavailableCodec"),
                                   /*allow_default_fallback=*/true);
  time_controller_.AdvanceTime(kFrameDuration);

  // Requested encoder is not available. Default fallback is allowed. Switch to
  // the next negotiated codec, VP8.
  codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP8", codec->name);
}

TEST_F(WebRtcVideoChannelBaseTest, RequestEncoderSwitchStrictPreference) {
  Codec vp9 = GetEngineCodec("VP9");
  vp9.params["profile-id"] = "0";

  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(vp9);
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  std::optional<Codec> codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP8", codec->name);

  SendImpl()->RequestEncoderSwitch(SdpVideoFormat::VP9Profile1(),
                                   /*allow_default_fallback=*/false);
  time_controller_.AdvanceTime(kFrameDuration);

  // VP9 profile_id=1 is not available. Default fallback is not allowed. Switch
  // is not performed.
  codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP8", codec->name);

  SendImpl()->RequestEncoderSwitch(SdpVideoFormat::VP9Profile0(),
                                   /*allow_default_fallback=*/false);
  time_controller_.AdvanceTime(kFrameDuration);

  // VP9 profile_id=0 is available. Switch encoder.
  codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP9", codec->name);
}

TEST_F(WebRtcVideoChannelBaseTest,
       SendCodecOrderInRtpParametersUnchangedAfterEncoderSwitch) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  EXPECT_THAT(send_channel_->GetRtpSendParameters(kSsrc).codecs,
              ElementsAre(Field(&RtpCodecParameters::name, "VP9"),
                          Field(&RtpCodecParameters::name, "VP8")));

  // RequestEncoderSwitch will post a task to the worker thread (which is also
  // the current thread).
  SendImpl()->RequestEncoderSwitch(std::nullopt, true);
  time_controller_.AdvanceTime(kFrameDuration);

  std::optional<Codec> send_codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(send_codec.has_value());
  EXPECT_EQ(send_codec->name, "VP8");
  EXPECT_THAT(send_channel_->GetRtpSendParameters(kSsrc).codecs,
              ElementsAre(Field(&RtpCodecParameters::name, "VP9"),
                          Field(&RtpCodecParameters::name, "VP8")));
}

TEST_F(WebRtcVideoChannelBaseTest,
       SetRtpSendParametersWithParametersFetchedBeforeEncoderSwitch) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  RtpParameters rtp_parameters = send_channel_->GetRtpSendParameters(kSsrc);
  ASSERT_EQ(rtp_parameters.encodings.size(), 1u);

  // Switch active send codec from VP9 to VP8 after fetching RtpParameters.
  SendImpl()->RequestEncoderSwitch(std::nullopt, true);
  time_controller_.AdvanceTime(kFrameDuration);
  std::optional<Codec> send_codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(send_codec.has_value());
  EXPECT_EQ(send_codec->name, "VP8");

  // Setting parameters fetched before the encoder switch must succeed because
  // the negotiated codecs order in RtpParameters did not change.
  rtp_parameters.encodings[0].max_bitrate_bps = 500000;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(kSsrc, rtp_parameters).ok());
  EXPECT_EQ(
      send_channel_->GetRtpSendParameters(kSsrc).encodings[0].max_bitrate_bps,
      500000);
}

TEST_F(WebRtcVideoChannelBaseTest,
       SetRtpSendParametersAfterRequestEncoderSwitch) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  std::optional<Codec> codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP9", codec->name);

  // Switch from VP9 to VP8.
  SendImpl()->RequestEncoderSwitch(std::nullopt, true);
  time_controller_.AdvanceTime(kFrameDuration);
  codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP8", codec->name);

  // SetRtpSendParameters() can still select VP9 because it was negotiated,
  // even though we previously switched from it.
  RtpParameters rtp_parameters = send_channel_->GetRtpSendParameters(kSsrc);
  ASSERT_EQ(rtp_parameters.encodings.size(), 1u);
  rtp_parameters.encodings[0].codec = GetEngineCodec("VP9").ToCodecParameters();
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(kSsrc, rtp_parameters).ok());
  codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP9", codec->name);

  // A subsequent encoder switch can fall back to VP8 again.
  SendImpl()->RequestEncoderSwitch(std::nullopt, true);
  time_controller_.AdvanceTime(kFrameDuration);
  codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP8", codec->name);
}

#endif  // defined(RTC_ENABLE_VP9)

class WebRtcVideoChannelTest : public WebRtcVideoEngineTest {
 public:
  WebRtcVideoChannelTest() : WebRtcVideoChannelTest("") {}
  explicit WebRtcVideoChannelTest(const char* field_trials)
      : WebRtcVideoEngineTest(field_trials),
        frame_source_(1280,
                      720,
                      TimeDelta::Seconds(1) / 30,
                      env_.clock().CurrentTime()),
        last_ssrc_(0) {}
  void SetUp() override {
    AddSupportedVideoCodecType("VP8");
    AddSupportedVideoCodecType("VP9");
    AddSupportedVideoCodecType(
        "AV1", {ScalabilityMode::kL1T3, ScalabilityMode::kL2T3});
#if defined(WEBRTC_USE_H264)
    AddSupportedVideoCodecType("H264");
#endif

    fake_call_ = std::make_unique<FakeCall>(env_);
    send_channel_ = engine_->CreateSendChannel(
        env_, fake_call_.get(), GetMediaConfig(), VideoOptions(),
        CryptoOptions(), video_bitrate_allocator_factory_.get(), nullptr,
        nullptr);
    receive_channel_ = engine_->CreateReceiveChannel(
        env_, fake_call_.get(), GetMediaConfig(), CryptoOptions(), nullptr,
        [](uint32_t, const RtpPacketInfos&, Timestamp) {});
    send_channel_->OnReadyToSend(true);
    receive_channel_->SetReceive(true);
    last_ssrc_ = 123;
    send_parameters_.codecs = engine_->LegacySendCodecs();
    recv_parameters_.codecs = engine_->LegacyRecvCodecs();
    ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
  }

  void TearDown() override {
    send_channel_->SetInterface(nullptr);
    receive_channel_->SetInterface(nullptr);
    send_channel_.reset();
    receive_channel_.reset();
    fake_call_ = nullptr;
  }

  void ResetTest() {
    TearDown();
    WebRtcVideoEngineTest::ChangeFieldTrials("", "");
    SetUp();
  }

  // Returns pointer to implementation of the send channel.
  WebRtcVideoSendChannel* SendImpl() {
    // Note that this function requires intimate knowledge of how the channel
    // was created.
    return static_cast<WebRtcVideoSendChannel*>(send_channel_.get());
  }

  // Casts a shim channel to a Transport. Used once.
  Transport* ChannelImplAsTransport(VideoMediaSendChannelInterface* channel) {
    return static_cast<WebRtcVideoSendChannel*>(channel)->transport();
  }

  Codec GetEngineCodec(const std::string& name) {
    for (const Codec& engine_codec : engine_->LegacySendCodecs()) {
      if (absl::EqualsIgnoreCase(name, engine_codec.name))
        return engine_codec;
    }
    // This point should never be reached.
    ADD_FAILURE() << "Unrecognized codec name: " << name;
    return CreateVideoCodec(0, "");
  }

  Codec DefaultCodec() { return GetEngineCodec("VP8"); }

  // After receciving and processing the packet, enough time is advanced that
  // the unsignalled receive stream cooldown is no longer in effect.
  void ReceivePacketAndAdvanceTime(const RtpPacketReceived& packet) {
    receive_channel_->OnPacketReceived(packet);
    time_controller_.AdvanceTime(
        TimeDelta::Millis(kUnsignalledReceiveStreamCooldownMs));
  }

 protected:
  FakeVideoSendStream* AddSendStream() {
    return AddSendStream(StreamParams::CreateLegacy(++last_ssrc_));
  }

  FakeVideoSendStream* AddSendStream(const StreamParams& sp) {
    size_t num_streams = fake_call_->GetVideoSendStreams().size();
    EXPECT_TRUE(send_channel_->AddSendStream(sp));
    std::vector<FakeVideoSendStream*> streams =
        fake_call_->GetVideoSendStreams();
    EXPECT_EQ(num_streams + 1, streams.size());
    return streams[streams.size() - 1];
  }

  std::vector<FakeVideoSendStream*> GetFakeSendStreams() {
    return fake_call_->GetVideoSendStreams();
  }

  FakeVideoReceiveStream* AddRecvStream() {
    return AddRecvStream(StreamParams::CreateLegacy(++last_ssrc_));
  }

  FakeVideoReceiveStream* AddRecvStream(const StreamParams& sp) {
    size_t num_streams = fake_call_->GetVideoReceiveStreams().size();
    EXPECT_TRUE(receive_channel_->AddRecvStream(sp));
    std::vector<FakeVideoReceiveStream*> streams =
        fake_call_->GetVideoReceiveStreams();
    EXPECT_EQ(num_streams + 1, streams.size());
    return streams[streams.size() - 1];
  }

  void SetSendCodecsShouldWorkForBitrates(const char* min_bitrate_kbps,
                                          int expected_min_bitrate_bps,
                                          const char* start_bitrate_kbps,
                                          int expected_start_bitrate_bps,
                                          const char* max_bitrate_kbps,
                                          int expected_max_bitrate_bps) {
    ExpectSetBitrateParameters(expected_min_bitrate_bps,
                               expected_start_bitrate_bps,
                               expected_max_bitrate_bps);
    auto& codecs = send_parameters_.codecs;
    codecs.clear();
    codecs.push_back(GetEngineCodec("VP8"));
    codecs[0].SetParam(kCodecParamMinBitrate, min_bitrate_kbps);
    codecs[0].SetParam(kCodecParamStartBitrate, start_bitrate_kbps);
    codecs[0].SetParam(kCodecParamMaxBitrate, max_bitrate_kbps);
    EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
  }

  void ExpectSetBitrateParameters(int min_bitrate_bps,
                                  int start_bitrate_bps,
                                  int max_bitrate_bps) {
    EXPECT_CALL(
        *fake_call_->GetMockTransportControllerSend(),
        SetSdpBitrateParameters(AllOf(
            Field(&BitrateConstraints::min_bitrate_bps, min_bitrate_bps),
            Field(&BitrateConstraints::start_bitrate_bps, start_bitrate_bps),
            Field(&BitrateConstraints::max_bitrate_bps, max_bitrate_bps))));
  }

  void ExpectSetMaxBitrate(int max_bitrate_bps) {
    EXPECT_CALL(*fake_call_->GetMockTransportControllerSend(),
                SetSdpBitrateParameters(Field(
                    &BitrateConstraints::max_bitrate_bps, max_bitrate_bps)));
  }

  void TestExtmapAllowMixedCaller(bool extmap_allow_mixed) {
    // For a caller, the answer will be applied in set remote description
    // where SetSenderParameters() is called.
    EXPECT_TRUE(
        send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrc)));
    send_parameters_.extmap_allow_mixed = extmap_allow_mixed;
    EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
    const VideoSendStream::Config& config =
        fake_call_->GetVideoSendStreams()[0]->GetConfig();
    EXPECT_EQ(extmap_allow_mixed, config.rtp.extmap_allow_mixed);
  }

  void TestExtmapAllowMixedCallee(bool extmap_allow_mixed) {
    // For a callee, the answer will be applied in set local description
    // where SetExtmapAllowMixed() and AddSendStream() are called.
    send_channel_->SetExtmapAllowMixed(extmap_allow_mixed);
    EXPECT_TRUE(
        send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrc)));
    const VideoSendStream::Config& config =
        fake_call_->GetVideoSendStreams()[0]->GetConfig();
    EXPECT_EQ(extmap_allow_mixed, config.rtp.extmap_allow_mixed);
  }

  void TestSetSendRtpHeaderExtensions(const std::string& ext_uri) {
    // Enable extension.
    const RtpHeaderExtensionId id(1);
    VideoSenderParameters parameters = send_parameters_;
    parameters.extensions.push_back(RtpExtension(ext_uri, id));
    EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
    FakeVideoSendStream* send_stream =
        AddSendStream(StreamParams::CreateLegacy(123));

    // Verify the send extension id.
    ASSERT_EQ(1u, send_stream->GetConfig().rtp.extensions.size());
    EXPECT_EQ(id, send_stream->GetConfig().rtp.extensions[0].id);
    EXPECT_EQ(ext_uri, send_stream->GetConfig().rtp.extensions[0].uri);
    // Verify call with same set of extensions returns true.
    EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

    // Verify that existing RTP header extensions can be removed.
    EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
    ASSERT_EQ(1u, fake_call_->GetVideoSendStreams().size());
    send_stream = fake_call_->GetVideoSendStreams()[0];
    EXPECT_TRUE(send_stream->GetConfig().rtp.extensions.empty());

    // Verify that adding receive RTP header extensions adds them for existing
    // streams.
    EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
    send_stream = fake_call_->GetVideoSendStreams()[0];
    ASSERT_EQ(1u, send_stream->GetConfig().rtp.extensions.size());
    EXPECT_EQ(id, send_stream->GetConfig().rtp.extensions[0].id);
    EXPECT_EQ(ext_uri, send_stream->GetConfig().rtp.extensions[0].uri);
  }

  void TestSetRecvRtpHeaderExtensions(const std::string& ext_uri) {
    // Enable extension.
    const RtpHeaderExtensionId id(1);
    VideoReceiverParameters parameters = recv_parameters_;
    parameters.extensions.push_back(RtpExtension(ext_uri, id));
    EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

    AddRecvStream(StreamParams::CreateLegacy(123));
    EXPECT_THAT(
        receive_channel_->GetRtpReceiverParameters(123).header_extensions,
        ElementsAre(RtpExtension(ext_uri, id)));

    // Verify call with same set of extensions returns true.
    EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

    // Verify that SetRecvRtpHeaderExtensions doesn't implicitly add them for
    // senders.
    EXPECT_TRUE(AddSendStream(StreamParams::CreateLegacy(123))
                    ->GetConfig()
                    .rtp.extensions.empty());

    // Verify that existing RTP header extensions can be removed.
    EXPECT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters_));
    EXPECT_THAT(
        receive_channel_->GetRtpReceiverParameters(123).header_extensions,
        IsEmpty());

    // Verify that adding receive RTP header extensions adds them for existing
    // streams.
    EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
    EXPECT_EQ(receive_channel_->GetRtpReceiverParameters(123).header_extensions,
              parameters.extensions);
  }

  void TestExtensionFilter(const std::vector<std::string>& extensions,
                           const std::string& expected_extension) {
    VideoSenderParameters parameters = send_parameters_;
    RtpHeaderExtensionId expected_id = RtpHeaderExtensionId::NotSet();
    RtpHeaderExtensionId id(1);
    for (const std::string& extension : extensions) {
      if (extension == expected_extension)
        expected_id = id;
      parameters.extensions.push_back(RtpExtension(extension, id));
      id = RtpHeaderExtensionId(id.value() + 1);
    }
    EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
    FakeVideoSendStream* send_stream =
        AddSendStream(StreamParams::CreateLegacy(123));

    // Verify that only one of them has been set, and that it is the one with
    // highest priority (transport sequence number).
    ASSERT_EQ(1u, send_stream->GetConfig().rtp.extensions.size());
    EXPECT_EQ(expected_id, send_stream->GetConfig().rtp.extensions[0].id);
    EXPECT_EQ(expected_extension,
              send_stream->GetConfig().rtp.extensions[0].uri);
  }

  void TestDegradationPreference(bool resolution_scaling_enabled,
                                 bool fps_scaling_enabled);

  void TestCpuAdaptation(bool enable_overuse, bool is_screenshare);
  void TestReceiverLocalSsrcConfiguration(bool receiver_first);
  void TestReceiveUnsignaledSsrcPacket(uint8_t payload_type,
                                       bool expect_created_receive_stream);

  FakeVideoSendStream* SetDenoisingOption(uint32_t ssrc,
                                          FrameForwarder* frame_forwarder,
                                          bool enabled) {
    VideoOptions options;
    options.video_noise_reduction = enabled;
    EXPECT_TRUE(send_channel_->SetVideoSend(ssrc, &options, frame_forwarder));
    // Options only take effect on the next frame.
    frame_forwarder->IncomingCapturedFrame(frame_source_.GetFrame());

    return fake_call_->GetVideoSendStreams().back();
  }

  FakeVideoSendStream* SetUpSimulcast(bool enabled, bool with_rtx) {
    const int kRtxSsrcOffset = 0xDEADBEEF;
    last_ssrc_ += 3;
    std::vector<uint32_t> ssrcs;
    std::vector<uint32_t> rtx_ssrcs;
    uint32_t num_streams = enabled ? kNumSimulcastStreams : 1;
    for (uint32_t i = 0; i < num_streams; ++i) {
      uint32_t ssrc = last_ssrc_ + i;
      ssrcs.push_back(ssrc);
      if (with_rtx) {
        rtx_ssrcs.push_back(ssrc + kRtxSsrcOffset);
      }
    }
    if (with_rtx) {
      return AddSendStream(
          CreateSimWithRtxStreamParams("cname", ssrcs, rtx_ssrcs));
    }
    return AddSendStream(CreateSimStreamParams("cname", ssrcs));
  }

  int GetMaxEncoderBitrate() {
    std::vector<FakeVideoSendStream*> streams =
        fake_call_->GetVideoSendStreams();
    EXPECT_EQ(1u, streams.size());
    FakeVideoSendStream* stream = streams[streams.size() - 1];
    EXPECT_EQ(1u, stream->GetEncoderConfig().number_of_streams);
    return stream->GetVideoStreams()[0].max_bitrate_bps;
  }

  void SetAndExpectMaxBitrate(int global_max,
                              std::optional<int> stream_max,
                              int expected_encoder_bitrate) {
    VideoSenderParameters limited_send_params = send_parameters_;
    limited_send_params.max_bandwidth_bps = global_max;
    EXPECT_TRUE(send_channel_->SetSenderParameters(limited_send_params));
    RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
    EXPECT_EQ(1UL, parameters.encodings.size());
    parameters.encodings[0].max_bitrate_bps = stream_max;
    EXPECT_TRUE(
        send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
    // Read back the parameteres and verify they have the correct value
    parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
    EXPECT_EQ(1UL, parameters.encodings.size());
    EXPECT_EQ(stream_max, parameters.encodings[0].max_bitrate_bps);
    // Verify that the new value propagated down to the encoder
    EXPECT_EQ(expected_encoder_bitrate, GetMaxEncoderBitrate());
  }

  // Values from kSimulcastConfigs in simulcast.cc.
  const std::vector<VideoStream> GetSimulcastBitrates720p() const {
    std::vector<VideoStream> layers(3);
    layers[0].min_bitrate_bps = 30000;
    layers[0].target_bitrate_bps = 150000;
    layers[0].max_bitrate_bps = 200000;
    layers[1].min_bitrate_bps = 150000;
    layers[1].target_bitrate_bps = 500000;
    layers[1].max_bitrate_bps = 700000;
    layers[2].min_bitrate_bps = 600000;
    layers[2].target_bitrate_bps = 2500000;
    layers[2].max_bitrate_bps = 2500000;
    return layers;
  }

  FakeFrameSource frame_source_;
  std::unique_ptr<FakeCall> fake_call_;
  std::unique_ptr<VideoMediaSendChannelInterface> send_channel_;
  std::unique_ptr<VideoMediaReceiveChannelInterface> receive_channel_;
  VideoSenderParameters send_parameters_;
  VideoReceiverParameters recv_parameters_;
  uint32_t last_ssrc_;
};

// SetReceiveNonSenderRttEnabled may toggle the value after a receive stream
// has started: false->true enables it, true->false disables it.
TEST_F(WebRtcVideoChannelTest, ToggleReceiveNonSenderRttAfterStreamStart) {
  FakeVideoReceiveStream* recv_stream =
      AddRecvStream(StreamParams::CreateLegacy(kSsrc));
  // Not negotiated yet -> disabled.
  EXPECT_FALSE(
      recv_stream->GetConfig().rtp.rtcp_xr.receiver_reference_time_report);

  // false -> true: enabled on the already-running stream.
  receive_channel_->SetReceiveNonSenderRttEnabled(true);
  EXPECT_TRUE(
      recv_stream->GetConfig().rtp.rtcp_xr.receiver_reference_time_report);

  // true -> false: disabled immediately.
  receive_channel_->SetReceiveNonSenderRttEnabled(false);
  EXPECT_FALSE(
      recv_stream->GetConfig().rtp.rtcp_xr.receiver_reference_time_report);
}

TEST_F(WebRtcVideoChannelTest, SetsSyncGroupFromSyncLabel) {
  const uint32_t kVideoSsrc = 123;
  const std::string kSyncLabel = "AvSyncLabel";

  StreamParams sp = StreamParams::CreateLegacy(kVideoSsrc);
  sp.set_stream_ids({kSyncLabel});
  EXPECT_TRUE(receive_channel_->AddRecvStream(sp));

  EXPECT_EQ(1u, fake_call_->GetVideoReceiveStreams().size());
  EXPECT_EQ(kSyncLabel,
            fake_call_->GetVideoReceiveStreams()[0]->GetConfig().sync_group)
      << "SyncGroup should be set based on sync_label";
}

// By default Sframe is not enabled on created streams.
TEST_F(WebRtcVideoChannelTest, SframeDisabledByDefault) {
  FakeVideoSendStream* send_stream = AddSendStream();
  EXPECT_FALSE(send_stream->GetConfig().rtp.sframe_options.required);
  EXPECT_FALSE(send_stream->GetConfig()
                   .rtp.sframe_options.encryption_config.has_value());
}

// Enabling Sframe before a send stream is added is propagated to the stream
// config at construction.
TEST_F(WebRtcVideoChannelTest, EnableSframePropagatesToSendStreamConfig) {
  send_channel_->EnableSframe();
  FakeVideoSendStream* stream = AddSendStream();
  EXPECT_TRUE(stream->GetConfig().rtp.sframe_options.required);
  EXPECT_FALSE(
      stream->GetConfig().rtp.sframe_options.encryption_config.has_value());
}

// Enabling Sframe on an existing send stream reconfigures it, but repeated
// calls (one per negotiation) must not recreate the stream again.
TEST_F(WebRtcVideoChannelTest, EnableSframeOnExistingSendStreamIsIdempotent) {
  AddSendStream();
  ASSERT_EQ(fake_call_->GetNumCreatedSendStreams(), 1);

  send_channel_->EnableSframe();
  ASSERT_EQ(fake_call_->GetNumCreatedSendStreams(), 2);
  EXPECT_TRUE(fake_call_->GetVideoSendStreams()[0]
                  ->GetConfig()
                  .rtp.sframe_options.required);
  EXPECT_FALSE(fake_call_->GetVideoSendStreams()[0]
                   ->GetConfig()
                   .rtp.sframe_options.encryption_config.has_value());

  send_channel_->EnableSframe();
  send_channel_->EnableSframe();
  EXPECT_EQ(fake_call_->GetNumCreatedSendStreams(), 2);
}

TEST_F(WebRtcVideoChannelTest, SframeEncryptorModePropagatesToNewSendStream) {
  scoped_refptr<SframeEncryptor> encryptor =
      SframeEncryptor::Create(SframeCipherSuite::kAes128GcmSha256_128);
  send_channel_->SetSframeEncryptionConfig(SframeEncryptionConfig{
      .mode = SframeMode::kPerPacket, .encryptor = encryptor});
  FakeVideoSendStream* stream = AddSendStream();
  ASSERT_TRUE(
      stream->GetConfig().rtp.sframe_options.encryption_config.has_value());
  EXPECT_EQ(stream->GetConfig().rtp.sframe_options.encryption_config->encryptor,
            encryptor);
  EXPECT_EQ(stream->GetConfig().rtp.sframe_options.encryption_config->mode,
            SframeMode::kPerPacket);
  EXPECT_FALSE(stream->GetConfig().rtp.sframe_options.required);

  send_channel_->EnableSframe();
  stream = fake_call_->GetVideoSendStreams()[0];
  ASSERT_TRUE(
      stream->GetConfig().rtp.sframe_options.encryption_config.has_value());
  EXPECT_EQ(stream->GetConfig().rtp.sframe_options.encryption_config->encryptor,
            encryptor);
  EXPECT_EQ(stream->GetConfig().rtp.sframe_options.encryption_config->mode,
            SframeMode::kPerPacket);
  EXPECT_TRUE(stream->GetConfig().rtp.sframe_options.required);
}

TEST_F(WebRtcVideoChannelTest, SframeEncryptorModeUpdatesExistingSendStream) {
  send_channel_->EnableSframe();
  AddSendStream();
  for (SframeMode mode : {SframeMode::kPerPacket, SframeMode::kPerFrame}) {
    scoped_refptr<SframeEncryptor> encryptor =
        SframeEncryptor::Create(SframeCipherSuite::kAes128GcmSha256_128);
    send_channel_->SetSframeEncryptionConfig(
        SframeEncryptionConfig{.mode = mode, .encryptor = encryptor});
    send_channel_->EnableSframe();
    const SframeSendOptions& options =
        fake_call_->GetVideoSendStreams()[0]->GetConfig().rtp.sframe_options;
    ASSERT_TRUE(options.encryption_config.has_value());
    EXPECT_EQ(options.encryption_config->encryptor, encryptor);
    EXPECT_EQ(options.encryption_config->mode, mode);
    EXPECT_TRUE(options.required);
  }
}

TEST_F(WebRtcVideoChannelTest, RecvStreamWithSimAndRtx) {
  VideoSenderParameters parameters;
  parameters.codecs = engine_->LegacySendCodecs();
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  EXPECT_TRUE(send_channel_->SetSend(true));
  parameters.conference_mode = true;
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  // Send side.
  const std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs1);
  const std::vector<uint32_t> rtx_ssrcs = MAKE_VECTOR(kRtxSsrcs1);
  FakeVideoSendStream* send_stream =
      AddSendStream(CreateSimWithRtxStreamParams("cname", ssrcs, rtx_ssrcs));

  ASSERT_EQ(rtx_ssrcs.size(), send_stream->GetConfig().rtp.rtx.ssrcs.size());
  for (size_t i = 0; i < rtx_ssrcs.size(); ++i)
    EXPECT_EQ(rtx_ssrcs[i], send_stream->GetConfig().rtp.rtx.ssrcs[i]);

  // Receiver side.
  FakeVideoReceiveStream* recv_stream =
      AddRecvStream(CreateSimWithRtxStreamParams("cname", ssrcs, rtx_ssrcs));
  EXPECT_FALSE(
      recv_stream->GetConfig().rtp.rtx_associated_payload_types.empty());
  EXPECT_TRUE(VerifyRtxReceiveAssociations(recv_stream->GetConfig()))
      << "RTX should be mapped for all decoders/payload types.";
  EXPECT_TRUE(HasRtxReceiveAssociation(recv_stream->GetConfig(),
                                       GetEngineCodec("red").id))
      << "RTX should be mapped for the RED payload type";

  EXPECT_EQ(rtx_ssrcs[0], recv_stream->GetConfig().rtp.rtx_ssrc);
}

TEST_F(WebRtcVideoChannelTest, RecvStreamWithRtx) {
  // Setup one channel with an associated RTX stream.
  StreamParams params = StreamParams::CreateLegacy(kSsrcs1[0]);
  params.AddFidSsrc(kSsrcs1[0], kRtxSsrcs1[0]);
  FakeVideoReceiveStream* recv_stream = AddRecvStream(params);
  EXPECT_EQ(kRtxSsrcs1[0], recv_stream->GetConfig().rtp.rtx_ssrc);

  EXPECT_TRUE(VerifyRtxReceiveAssociations(recv_stream->GetConfig()))
      << "RTX should be mapped for all decoders/payload types.";
  EXPECT_TRUE(HasRtxReceiveAssociation(recv_stream->GetConfig(),
                                       GetEngineCodec("red").id))
      << "RTX should be mapped for the RED payload type";
}

TEST_F(WebRtcVideoChannelTest, RecvStreamNoRtx) {
  // Setup one channel without an associated RTX stream.
  StreamParams params = StreamParams::CreateLegacy(kSsrcs1[0]);
  FakeVideoReceiveStream* recv_stream = AddRecvStream(params);
  ASSERT_EQ(0U, recv_stream->GetConfig().rtp.rtx_ssrc);
}

// Test propagation of extmap allow mixed setting.
TEST_F(WebRtcVideoChannelTest, SetExtmapAllowMixedAsCaller) {
  TestExtmapAllowMixedCaller(/*extmap_allow_mixed=*/true);
}
TEST_F(WebRtcVideoChannelTest, SetExtmapAllowMixedDisabledAsCaller) {
  TestExtmapAllowMixedCaller(/*extmap_allow_mixed=*/false);
}
TEST_F(WebRtcVideoChannelTest, SetExtmapAllowMixedAsCallee) {
  TestExtmapAllowMixedCallee(/*extmap_allow_mixed=*/true);
}
TEST_F(WebRtcVideoChannelTest, SetExtmapAllowMixedDisabledAsCallee) {
  TestExtmapAllowMixedCallee(/*extmap_allow_mixed=*/false);
}

TEST_F(WebRtcVideoChannelTest, NoHeaderExtesionsByDefault) {
  FakeVideoSendStream* send_stream =
      AddSendStream(StreamParams::CreateLegacy(kSsrcs1[0]));
  ASSERT_TRUE(send_stream->GetConfig().rtp.extensions.empty());

  AddRecvStream(StreamParams::CreateLegacy(kSsrcs1[0]));
  ASSERT_TRUE(receive_channel_->GetRtpReceiverParameters(kSsrcs1[0])
                  .header_extensions.empty());
}

// Test support for RTP timestamp offset header extension.
TEST_F(WebRtcVideoChannelTest, SendRtpTimestampOffsetHeaderExtensions) {
  TestSetSendRtpHeaderExtensions(RtpExtension::kTimestampOffsetUri);
}

TEST_F(WebRtcVideoChannelTest, RecvRtpTimestampOffsetHeaderExtensions) {
  TestSetRecvRtpHeaderExtensions(RtpExtension::kTimestampOffsetUri);
}

// Test support for absolute send time header extension.
TEST_F(WebRtcVideoChannelTest, SendAbsoluteSendTimeHeaderExtensions) {
  TestSetSendRtpHeaderExtensions(RtpExtension::kAbsSendTimeUri);
}

TEST_F(WebRtcVideoChannelTest, RecvAbsoluteSendTimeHeaderExtensions) {
  TestSetRecvRtpHeaderExtensions(RtpExtension::kAbsSendTimeUri);
}

class WebRtcVideoChannelWithFilterAbsSendTimeExtensionTest
    : public WebRtcVideoChannelTest {
 public:
  WebRtcVideoChannelWithFilterAbsSendTimeExtensionTest()
      : WebRtcVideoChannelTest("WebRTC-FilterAbsSendTimeExtension/Enabled/") {}
};

TEST_F(WebRtcVideoChannelWithFilterAbsSendTimeExtensionTest,
       FiltersExtensionsPicksTransportSeqNum) {
  // Enable three redundant extensions.
  std::vector<std::string> extensions;
  extensions.push_back(RtpExtension::kAbsSendTimeUri);
  extensions.push_back(RtpExtension::kTimestampOffsetUri);
  extensions.push_back(RtpExtension::kTransportSequenceNumberUri);
  TestExtensionFilter(extensions, RtpExtension::kTransportSequenceNumberUri);
}

TEST_F(WebRtcVideoChannelTest, FiltersExtensionsPicksAbsSendTime) {
  // Enable two redundant extensions.
  std::vector<std::string> extensions;
  extensions.push_back(RtpExtension::kAbsSendTimeUri);
  extensions.push_back(RtpExtension::kTimestampOffsetUri);
  TestExtensionFilter(extensions, RtpExtension::kAbsSendTimeUri);
}

// Test support for transport sequence number header extension.
TEST_F(WebRtcVideoChannelTest, SendTransportSequenceNumberHeaderExtensions) {
  TestSetSendRtpHeaderExtensions(RtpExtension::kTransportSequenceNumberUri);
}
TEST_F(WebRtcVideoChannelTest, RecvTransportSequenceNumberHeaderExtensions) {
  TestSetRecvRtpHeaderExtensions(RtpExtension::kTransportSequenceNumberUri);
}

// Test support for video rotation header extension.
TEST_F(WebRtcVideoChannelTest, SendVideoRotationHeaderExtensions) {
  TestSetSendRtpHeaderExtensions(RtpExtension::kVideoRotationUri);
}
TEST_F(WebRtcVideoChannelTest, RecvVideoRotationHeaderExtensions) {
  TestSetRecvRtpHeaderExtensions(RtpExtension::kVideoRotationUri);
}

TEST_F(WebRtcVideoChannelTest, SendCorruptionDetectionHeaderExtensions) {
  TestSetSendRtpHeaderExtensions(RtpExtension::kCorruptionDetectionUri);
}
TEST_F(WebRtcVideoChannelTest, RecvCorruptionDetectionHeaderExtensions) {
  TestSetRecvRtpHeaderExtensions(RtpExtension::kCorruptionDetectionUri);
}

TEST_F(WebRtcVideoChannelTest, DisableFrameInstrumentationByDefault) {
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
  FakeVideoSendStream* send_stream =
      AddSendStream(StreamParams::CreateLegacy(123));
  EXPECT_FALSE(send_stream->GetConfig()
                   .encoder_settings.enable_frame_instrumentation_generator);
}

TEST_F(WebRtcVideoChannelTest,
       EnableFrameInstrumentationWhenEncryptedExtensionIsPresent) {
  VideoSenderParameters parameters = send_parameters_;
  parameters.extensions.push_back(
      RtpExtension(RtpExtension::kCorruptionDetectionUri,
                   RtpHeaderExtensionId(1), /*encrypt=*/true));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  FakeVideoSendStream* send_stream =
      AddSendStream(StreamParams::CreateLegacy(123));
  EXPECT_TRUE(send_stream->GetConfig()
                  .encoder_settings.enable_frame_instrumentation_generator);
}

TEST_F(WebRtcVideoChannelTest,
       DisableFrameInstrumentationWhenNoEncryptedExtensionIsPresent) {
  VideoSenderParameters parameters = send_parameters_;
  parameters.extensions.push_back(
      RtpExtension(RtpExtension::kCorruptionDetectionUri,
                   RtpHeaderExtensionId(1), /*encrypt=*/false));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  FakeVideoSendStream* send_stream =
      AddSendStream(StreamParams::CreateLegacy(123));
  EXPECT_FALSE(send_stream->GetConfig()
                   .encoder_settings.enable_frame_instrumentation_generator);
}

TEST_F(WebRtcVideoChannelTest, IdenticalSendExtensionsDoesntRecreateStream) {
  constexpr RtpHeaderExtensionId kAbsSendTimeId(1);
  constexpr RtpHeaderExtensionId kVideoRotationId(2);
  send_parameters_.extensions.push_back(
      RtpExtension(RtpExtension::kAbsSendTimeUri, kAbsSendTimeId));
  send_parameters_.extensions.push_back(
      RtpExtension(RtpExtension::kVideoRotationUri, kVideoRotationId));

  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
  FakeVideoSendStream* send_stream =
      AddSendStream(StreamParams::CreateLegacy(123));

  EXPECT_EQ(1, fake_call_->GetNumCreatedSendStreams());
  ASSERT_EQ(2u, send_stream->GetConfig().rtp.extensions.size());

  // Setting the same extensions (even if in different order) shouldn't
  // reallocate the stream.
  absl::c_reverse(send_parameters_.extensions);
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  EXPECT_EQ(1, fake_call_->GetNumCreatedSendStreams());

  // Setting different extensions should recreate the stream.
  send_parameters_.extensions.resize(1);
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  EXPECT_EQ(2, fake_call_->GetNumCreatedSendStreams());
}

TEST_F(WebRtcVideoChannelTest,
       SetSendRtpHeaderExtensionsExcludeUnsupportedExtensions) {
  constexpr RtpHeaderExtensionId kUnsupportedId(1);
  constexpr RtpHeaderExtensionId kTOffsetId(2);

  send_parameters_.extensions.push_back(
      RtpExtension(kUnsupportedExtensionName, kUnsupportedId));
  send_parameters_.extensions.push_back(
      RtpExtension(RtpExtension::kTimestampOffsetUri, kTOffsetId));
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
  FakeVideoSendStream* send_stream =
      AddSendStream(StreamParams::CreateLegacy(123));

  // Only timestamp offset extension is set to send stream,
  // unsupported rtp extension is ignored.
  ASSERT_EQ(1u, send_stream->GetConfig().rtp.extensions.size());
  EXPECT_STREQ(RtpExtension::kTimestampOffsetUri,
               send_stream->GetConfig().rtp.extensions[0].uri.c_str());
}

TEST_F(WebRtcVideoChannelTest,
       SetRecvRtpHeaderExtensionsExcludeUnsupportedExtensions) {
  constexpr RtpHeaderExtensionId kUnsupportedId(1);
  constexpr RtpHeaderExtensionId kTOffsetId(2);

  recv_parameters_.extensions.push_back(
      RtpExtension(kUnsupportedExtensionName, kUnsupportedId));
  recv_parameters_.extensions.push_back(
      RtpExtension(RtpExtension::kTimestampOffsetUri, kTOffsetId));
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters_));
  AddRecvStream(StreamParams::CreateLegacy(123));

  // Only timestamp offset extension is set to receive stream,
  // unsupported rtp extension is ignored.
  ASSERT_THAT(receive_channel_->GetRtpReceiverParameters(123).header_extensions,
              SizeIs(1));
  EXPECT_STREQ(receive_channel_->GetRtpReceiverParameters(123)
                   .header_extensions[0]
                   .uri.c_str(),
               RtpExtension::kTimestampOffsetUri);
}



TEST_F(WebRtcVideoChannelTest, OnPacketReceivedIdentifiesExtensions) {
  VideoReceiverParameters parameters = recv_parameters_;
  parameters.extensions.push_back(
      RtpExtension(RtpExtension::kVideoRotationUri, RtpHeaderExtensionId(1)));
  ASSERT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  time_controller_.AdvanceTime(TimeDelta::Zero());
  RtpHeaderExtensionMap extension_map(parameters.extensions);
  RtpPacketReceived reference_packet(&extension_map);
  reference_packet.SetExtension<VideoOrientation>(
      VideoRotation::kVideoRotation_270);
  // Create a packet without the extension map but with the same content.
  // We need to parse it using the extension map to identify extensions properly
  // since RtpTransport is effectively skipped in this test.
  RtpPacketReceived received_packet(&extension_map);
  ASSERT_TRUE(received_packet.Parse(reference_packet.Buffer()));

  receive_channel_->OnPacketReceived(received_packet);
  time_controller_.AdvanceTime(TimeDelta::Zero());

  EXPECT_EQ(
      fake_call_->last_received_rtp_packet().GetExtension<VideoOrientation>(),
      VideoRotation::kVideoRotation_270);
}

TEST_F(WebRtcVideoChannelTest, AddRecvStreamOnlyUsesOneReceiveStream) {
  EXPECT_TRUE(receive_channel_->AddRecvStream(StreamParams::CreateLegacy(1)));
  EXPECT_EQ(1u, fake_call_->GetVideoReceiveStreams().size());
}

TEST_F(WebRtcVideoChannelTest, RtcpIsCompoundByDefault) {
  FakeVideoReceiveStream* stream = AddRecvStream();
  EXPECT_EQ(RtcpMode::kCompound, stream->GetConfig().rtp.rtcp_mode);
}

TEST_F(WebRtcVideoChannelTest, NackIsEnabledByDefault) {
  AssignDefaultCodec();
  VerifyCodecHasDefaultFeedbackParams(*default_codec_, false);

  VideoSenderParameters parameters;
  parameters.codecs = engine_->LegacySendCodecs();
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  EXPECT_TRUE(send_channel_->SetSend(true));

  // Send side.
  FakeVideoSendStream* send_stream =
      AddSendStream(StreamParams::CreateLegacy(1));
  EXPECT_GT(send_stream->GetConfig().rtp.nack.rtp_history_ms, 0);

  // Receiver side.
  FakeVideoReceiveStream* recv_stream =
      AddRecvStream(StreamParams::CreateLegacy(1));
  EXPECT_GT(recv_stream->GetConfig().rtp.nack.rtp_history_ms, 0);

  // Nack history size should match between sender and receiver.
  EXPECT_EQ(send_stream->GetConfig().rtp.nack.rtp_history_ms,
            recv_stream->GetConfig().rtp.nack.rtp_history_ms);
}

// This test verifies that new frame sizes reconfigures encoders even though not
// (yet) sending. The purpose of this is to permit encoding as quickly as
// possible once we start sending. Likely the frames being input are from the
// same source that will be sent later, which just means that we're ready
// earlier.
TEST_F(WebRtcVideoChannelTest, ReconfiguresEncodersWhenNotSending) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));
  send_channel_->SetSend(false);

  FakeVideoSendStream* stream = AddSendStream();

  // No frames entered.
  std::vector<VideoStream> streams = stream->GetVideoStreams();
  EXPECT_EQ(0u, streams[0].width);
  EXPECT_EQ(0u, streams[0].height);

  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 30,
                               env_.clock().CurrentTime());

  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, nullptr, &frame_forwarder));
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());

  // Frame entered, should be reconfigured to new dimensions.
  streams = stream->GetVideoStreams();
  EXPECT_EQ(checked_cast<size_t>(1280), streams[0].width);
  EXPECT_EQ(checked_cast<size_t>(720), streams[0].height);

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest, UsesCorrectSettingsForScreencast) {
  static const int kScreenshareMinBitrateKbps = 800;
  Codec codec = GetEngineCodec("VP8");
  VideoSenderParameters parameters;
  parameters.codecs.push_back(codec);
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  AddSendStream();

  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 30,
                               env_.clock().CurrentTime());
  VideoOptions min_bitrate_options;
  min_bitrate_options.screencast_min_bitrate_kbps = kScreenshareMinBitrateKbps;
  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, &min_bitrate_options,
                                          &frame_forwarder));

  EXPECT_TRUE(send_channel_->SetSend(true));

  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  ASSERT_EQ(1u, fake_call_->GetVideoSendStreams().size());
  FakeVideoSendStream* send_stream = fake_call_->GetVideoSendStreams().front();

  EXPECT_EQ(1, send_stream->GetNumberOfSwappedFrames());

  // Verify non-screencast settings.
  VideoEncoderConfig encoder_config = send_stream->GetEncoderConfig().Copy();
  EXPECT_EQ(VideoEncoderConfig::ContentType::kRealtimeVideo,
            encoder_config.content_type);
  std::vector<VideoStream> streams = send_stream->GetVideoStreams();
  EXPECT_EQ(checked_cast<size_t>(1280), streams.front().width);
  EXPECT_EQ(checked_cast<size_t>(720), streams.front().height);
  EXPECT_EQ(0, encoder_config.min_transmit_bitrate_bps)
      << "Non-screenshare shouldn't use min-transmit bitrate.";

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
  EXPECT_EQ(1, send_stream->GetNumberOfSwappedFrames());
  VideoOptions screencast_options;
  screencast_options.is_screencast = true;
  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, &screencast_options,
                                          &frame_forwarder));
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  // Send stream recreated after option change.
  ASSERT_EQ(2, fake_call_->GetNumCreatedSendStreams());
  send_stream = fake_call_->GetVideoSendStreams().front();
  EXPECT_EQ(1, send_stream->GetNumberOfSwappedFrames());

  // Verify screencast settings.
  encoder_config = send_stream->GetEncoderConfig().Copy();
  EXPECT_EQ(VideoEncoderConfig::ContentType::kScreen,
            encoder_config.content_type);
  EXPECT_EQ(kScreenshareMinBitrateKbps * 1000,
            encoder_config.min_transmit_bitrate_bps);

  streams = send_stream->GetVideoStreams();
  EXPECT_EQ(checked_cast<size_t>(1280), streams.front().width);
  EXPECT_EQ(checked_cast<size_t>(720), streams.front().height);
  EXPECT_FALSE(streams[0].num_temporal_layers.has_value());
  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest,
       ConferenceModeScreencastConfiguresTemporalLayer) {
  static const int kConferenceScreencastTemporalBitrateBps = 200 * 1000;
  send_parameters_.conference_mode = true;
  send_channel_->SetSenderParameters(send_parameters_);

  AddSendStream();
  VideoOptions options;
  options.is_screencast = true;
  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 30,
                               env_.clock().CurrentTime());
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  EXPECT_TRUE(send_channel_->SetSend(true));

  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
  ASSERT_EQ(1u, fake_call_->GetVideoSendStreams().size());
  FakeVideoSendStream* send_stream = fake_call_->GetVideoSendStreams().front();

  VideoEncoderConfig encoder_config = send_stream->GetEncoderConfig().Copy();

  // Verify screencast settings.
  encoder_config = send_stream->GetEncoderConfig().Copy();
  EXPECT_EQ(VideoEncoderConfig::ContentType::kScreen,
            encoder_config.content_type);

  std::vector<VideoStream> streams = send_stream->GetVideoStreams();
  ASSERT_EQ(1u, streams.size());
  ASSERT_EQ(2u, streams[0].num_temporal_layers);
  EXPECT_EQ(kConferenceScreencastTemporalBitrateBps,
            streams[0].target_bitrate_bps);

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest, SuspendBelowMinBitrateDisabledByDefault) {
  FakeVideoSendStream* stream = AddSendStream();
  EXPECT_FALSE(stream->GetConfig().suspend_below_min_bitrate);
}

TEST_F(WebRtcVideoChannelTest, SetMediaConfigSuspendBelowMinBitrate) {
  MediaConfig media_config = GetMediaConfig();
  media_config.video.suspend_below_min_bitrate = true;

  send_channel_ = engine_->CreateSendChannel(
      env_, fake_call_.get(), media_config, VideoOptions(), CryptoOptions(),
      video_bitrate_allocator_factory_.get(), nullptr, nullptr);
  receive_channel_ = engine_->CreateReceiveChannel(
      env_, fake_call_.get(), media_config, CryptoOptions(), nullptr,
      [](uint32_t, const RtpPacketInfos&, Timestamp) {});
  send_channel_->OnReadyToSend(true);

  send_channel_->SetSenderParameters(send_parameters_);

  FakeVideoSendStream* stream = AddSendStream();
  EXPECT_TRUE(stream->GetConfig().suspend_below_min_bitrate);

  media_config.video.suspend_below_min_bitrate = false;
  send_channel_ = engine_->CreateSendChannel(
      env_, fake_call_.get(), media_config, VideoOptions(), CryptoOptions(),
      video_bitrate_allocator_factory_.get(), nullptr, nullptr);
  receive_channel_ = engine_->CreateReceiveChannel(
      env_, fake_call_.get(), media_config, CryptoOptions(), nullptr,
      [](uint32_t, const RtpPacketInfos&, Timestamp) {});
  send_channel_->OnReadyToSend(true);

  send_channel_->SetSenderParameters(send_parameters_);

  stream = AddSendStream();
  EXPECT_FALSE(stream->GetConfig().suspend_below_min_bitrate);
}

TEST_F(WebRtcVideoChannelTest, Vp8DenoisingEnabledByDefault) {
  FakeVideoSendStream* stream = AddSendStream();
  VideoCodecVP8 vp8_settings;
  ASSERT_TRUE(stream->GetVp8Settings(&vp8_settings)) << "No VP8 config set.";
  EXPECT_TRUE(vp8_settings.denoisingOn);
}

TEST_F(WebRtcVideoChannelTest, VerifyVp8SpecificSettings) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  // Single-stream settings should apply with RTX as well (verifies that we
  // check number of regular SSRCs and not StreamParams::ssrcs which contains
  // both RTX and regular SSRCs).
  FakeVideoSendStream* stream = SetUpSimulcast(false, /*with_rtx=*/true);

  FrameForwarder frame_forwarder;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, nullptr, &frame_forwarder));
  send_channel_->SetSend(true);

  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  VideoCodecVP8 vp8_settings;
  ASSERT_TRUE(stream->GetVp8Settings(&vp8_settings)) << "No VP8 config set.";
  EXPECT_TRUE(vp8_settings.denoisingOn)
      << "VP8 denoising should be on by default.";

  stream = SetDenoisingOption(last_ssrc_, &frame_forwarder, false);

  ASSERT_TRUE(stream->GetVp8Settings(&vp8_settings)) << "No VP8 config set.";
  EXPECT_FALSE(vp8_settings.denoisingOn);
  EXPECT_TRUE(vp8_settings.automaticResizeOn);
  EXPECT_TRUE(stream->GetEncoderConfig().frame_drop_enabled);

  stream = SetDenoisingOption(last_ssrc_, &frame_forwarder, true);

  ASSERT_TRUE(stream->GetVp8Settings(&vp8_settings)) << "No VP8 config set.";
  EXPECT_TRUE(vp8_settings.denoisingOn);
  EXPECT_TRUE(vp8_settings.automaticResizeOn);
  EXPECT_TRUE(stream->GetEncoderConfig().frame_drop_enabled);

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
  stream = SetUpSimulcast(true, /*with_rtx=*/false);
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, nullptr, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  EXPECT_EQ(3u, stream->GetVideoStreams().size());
  ASSERT_TRUE(stream->GetVp8Settings(&vp8_settings)) << "No VP8 config set.";
  // Autmatic resize off when using simulcast.
  EXPECT_FALSE(vp8_settings.automaticResizeOn);
  EXPECT_TRUE(stream->GetEncoderConfig().frame_drop_enabled);

  // In screen-share mode, denoising is forced off.
  VideoOptions options;
  options.is_screencast = true;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));

  stream = SetDenoisingOption(last_ssrc_, &frame_forwarder, false);

  EXPECT_EQ(3u, stream->GetVideoStreams().size());
  ASSERT_TRUE(stream->GetVp8Settings(&vp8_settings)) << "No VP8 config set.";
  EXPECT_FALSE(vp8_settings.denoisingOn);
  // Resizing always off for screen sharing.
  EXPECT_FALSE(vp8_settings.automaticResizeOn);
  EXPECT_TRUE(stream->GetEncoderConfig().frame_drop_enabled);

  stream = SetDenoisingOption(last_ssrc_, &frame_forwarder, true);

  ASSERT_TRUE(stream->GetVp8Settings(&vp8_settings)) << "No VP8 config set.";
  EXPECT_FALSE(vp8_settings.denoisingOn);
  EXPECT_FALSE(vp8_settings.automaticResizeOn);
  EXPECT_TRUE(stream->GetEncoderConfig().frame_drop_enabled);

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest, VerifyAv1SpecificSettings) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("AV1"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));
  FrameForwarder frame_forwarder;
  VideoCodecAV1 settings;

  // Single-stream settings should apply with RTX as well (verifies that we
  // check number of regular SSRCs and not StreamParams::ssrcs which contains
  // both RTX and regular SSRCs).
  FakeVideoSendStream* stream = SetUpSimulcast(false, /*with_rtx=*/true);
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, nullptr, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  ASSERT_TRUE(stream->GetAv1Settings(&settings)) << "No AV1 config set.";
  EXPECT_TRUE(settings.automatic_resize_on);

  RtpParameters rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_THAT(rtp_parameters.encodings,
              ElementsAre(Field(&RtpEncodingParameters::scalability_mode,
                                std::nullopt)));
  rtp_parameters.encodings[0].scalability_mode = "L2T3";
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  ASSERT_TRUE(stream->GetAv1Settings(&settings)) << "No AV1 config set.";
  EXPECT_FALSE(settings.automatic_resize_on);

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

// Test that setting the same options doesn't result in the encoder being
// reconfigured.
TEST_F(WebRtcVideoChannelTest, SetIdenticalOptionsDoesntReconfigureEncoder) {
  VideoOptions options;
  FrameForwarder frame_forwarder;

  AddSendStream();
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));
  FakeVideoSendStream* send_stream = fake_call_->GetVideoSendStreams().front();

  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());
  // Expect 1 reconfigurations at this point from the initial configuration.
  EXPECT_EQ(1, send_stream->num_encoder_reconfigurations());

  FrameForwarder new_frame_forwarder;

  // Set the options one more time but with a new source instance, expect
  // one additional reconfiguration.
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &new_frame_forwarder));
  new_frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());
  EXPECT_EQ(2, send_stream->num_encoder_reconfigurations());

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

// Test that if a new source is set, we reconfigure the encoder even if the
// same options are used.
TEST_F(WebRtcVideoChannelTest,
       SetNewSourceWithIdenticalOptionsReconfiguresEncoder) {
  VideoOptions options;
  FrameForwarder frame_forwarder;

  AddSendStream();
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));
  FakeVideoSendStream* send_stream = fake_call_->GetVideoSendStreams().front();

  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());
  // Expect 1 reconfigurations at this point from the initial configuration.
  EXPECT_EQ(1, send_stream->num_encoder_reconfigurations());

  // Set the options one more time and expect no additional reconfigurations.
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  EXPECT_EQ(1, send_stream->num_encoder_reconfigurations());

  // Change `options` and expect 2 reconfigurations.
  options.video_noise_reduction = true;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  EXPECT_EQ(2, send_stream->num_encoder_reconfigurations());

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

class Vp9SettingsTest : public WebRtcVideoChannelTest {
 public:
  Vp9SettingsTest() : Vp9SettingsTest("") {}
  explicit Vp9SettingsTest(const char* field_trials)
      : WebRtcVideoChannelTest(field_trials) {
    encoder_factory_->AddSupportedVideoCodecType("VP9");
  }
  ~Vp9SettingsTest() override {}

 protected:
  void TearDown() override {
    // Remove references to encoder_factory_ since this will be destroyed
    // before send_channel_ and engine_->
    ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
  }
};

TEST_F(Vp9SettingsTest, VerifyVp9SpecificSettings) {
  encoder_factory_->AddSupportedVideoCodec(
      SdpVideoFormat("VP9", CodecParameterMap(),
                     {ScalabilityMode::kL1T1, ScalabilityMode::kL2T1}));

  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  FakeVideoSendStream* stream = SetUpSimulcast(false, /*with_rtx=*/false);

  FrameForwarder frame_forwarder;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, nullptr, &frame_forwarder));
  send_channel_->SetSend(true);

  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  VideoCodecVP9 vp9_settings;
  ASSERT_TRUE(stream->GetVp9Settings(&vp9_settings)) << "No VP9 config set.";
  EXPECT_TRUE(vp9_settings.denoisingOn)
      << "VP9 denoising should be on by default.";
  EXPECT_TRUE(vp9_settings.automaticResizeOn)
      << "Automatic resize on for one active stream.";

  stream = SetDenoisingOption(last_ssrc_, &frame_forwarder, false);

  ASSERT_TRUE(stream->GetVp9Settings(&vp9_settings)) << "No VP9 config set.";
  EXPECT_FALSE(vp9_settings.denoisingOn);
  EXPECT_TRUE(stream->GetEncoderConfig().frame_drop_enabled)
      << "Frame dropping always on for real time video.";
  EXPECT_TRUE(vp9_settings.automaticResizeOn);

  stream = SetDenoisingOption(last_ssrc_, &frame_forwarder, true);

  ASSERT_TRUE(stream->GetVp9Settings(&vp9_settings)) << "No VP9 config set.";
  EXPECT_TRUE(vp9_settings.denoisingOn);
  EXPECT_TRUE(stream->GetEncoderConfig().frame_drop_enabled);
  EXPECT_TRUE(vp9_settings.automaticResizeOn);

  RtpParameters rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_THAT(rtp_parameters.encodings,
              ElementsAre(Field(&RtpEncodingParameters::scalability_mode,
                                std::nullopt)));
  rtp_parameters.encodings[0].scalability_mode = "L2T1";
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());

  ASSERT_TRUE(stream->GetVp9Settings(&vp9_settings)) << "No VP9 config set.";
  EXPECT_TRUE(vp9_settings.denoisingOn);
  EXPECT_TRUE(stream->GetEncoderConfig().frame_drop_enabled);
  EXPECT_FALSE(vp9_settings.automaticResizeOn)
      << "Automatic resize off for multiple spatial layers.";

  rtp_parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_THAT(
      rtp_parameters.encodings,
      ElementsAre(Field(&RtpEncodingParameters::scalability_mode, "L2T1")));
  rtp_parameters.encodings[0].scalability_mode = "L1T1";
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());
  rtp_parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_THAT(
      rtp_parameters.encodings,
      ElementsAre(Field(&RtpEncodingParameters::scalability_mode, "L1T1")));

  ASSERT_TRUE(stream->GetVp9Settings(&vp9_settings)) << "No VP9 config set.";
  EXPECT_TRUE(vp9_settings.denoisingOn);
  EXPECT_TRUE(stream->GetEncoderConfig().frame_drop_enabled);
  EXPECT_TRUE(vp9_settings.automaticResizeOn)
      << "Automatic resize on for one spatial layer.";

  // In screen-share mode, denoising is forced off.
  VideoOptions options;
  options.is_screencast = true;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));

  stream = SetDenoisingOption(last_ssrc_, &frame_forwarder, false);

  ASSERT_TRUE(stream->GetVp9Settings(&vp9_settings)) << "No VP9 config set.";
  EXPECT_FALSE(vp9_settings.denoisingOn);
  EXPECT_TRUE(stream->GetEncoderConfig().frame_drop_enabled)
      << "Frame dropping always on for screen sharing.";
  EXPECT_FALSE(vp9_settings.automaticResizeOn)
      << "Automatic resize off for screencast.";

  stream = SetDenoisingOption(last_ssrc_, &frame_forwarder, false);

  ASSERT_TRUE(stream->GetVp9Settings(&vp9_settings)) << "No VP9 config set.";
  EXPECT_FALSE(vp9_settings.denoisingOn);
  EXPECT_TRUE(stream->GetEncoderConfig().frame_drop_enabled);
  EXPECT_FALSE(vp9_settings.automaticResizeOn);

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(Vp9SettingsTest, MultipleSsrcsEnablesSvc) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs3);

  FakeVideoSendStream* stream =
      AddSendStream(CreateSimStreamParams("cname", ssrcs));

  FrameForwarder frame_forwarder;
  EXPECT_TRUE(send_channel_->SetVideoSend(ssrcs[0], nullptr, &frame_forwarder));
  send_channel_->SetSend(true);

  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  VideoCodecVP9 vp9_settings;
  ASSERT_TRUE(stream->GetVp9Settings(&vp9_settings)) << "No VP9 config set.";

  const size_t kNumSpatialLayers = ssrcs.size();
  const size_t kNumTemporalLayers = 3;
  EXPECT_EQ(vp9_settings.numberOfSpatialLayers, kNumSpatialLayers);
  EXPECT_EQ(vp9_settings.numberOfTemporalLayers, kNumTemporalLayers);

  EXPECT_TRUE(send_channel_->SetVideoSend(ssrcs[0], nullptr, nullptr));
}

TEST_F(Vp9SettingsTest, SvcModeCreatesSingleRtpStream) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs3);

  FakeVideoSendStream* stream =
      AddSendStream(CreateSimStreamParams("cname", ssrcs));

  const VideoSendStream::Config& config = stream->GetConfig();

  // Despite 3 ssrcs provided, single layer is used.
  EXPECT_EQ(1u, config.rtp.ssrcs.size());

  FrameForwarder frame_forwarder;
  EXPECT_TRUE(send_channel_->SetVideoSend(ssrcs[0], nullptr, &frame_forwarder));
  send_channel_->SetSend(true);

  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  VideoCodecVP9 vp9_settings;
  ASSERT_TRUE(stream->GetVp9Settings(&vp9_settings)) << "No VP9 config set.";

  const size_t kNumSpatialLayers = ssrcs.size();
  EXPECT_EQ(vp9_settings.numberOfSpatialLayers, kNumSpatialLayers);

  EXPECT_TRUE(send_channel_->SetVideoSend(ssrcs[0], nullptr, nullptr));
}

TEST_F(Vp9SettingsTest, AllEncodingParametersCopied) {
  VideoSenderParameters send_parameters;
  send_parameters.codecs.push_back(GetEngineCodec("VP9"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters));

  const size_t kNumSpatialLayers = 3;
  std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs3);

  FakeVideoSendStream* stream =
      AddSendStream(CreateSimStreamParams("cname", ssrcs));

  RtpParameters parameters = send_channel_->GetRtpSendParameters(ssrcs[0]);
  ASSERT_EQ(kNumSpatialLayers, parameters.encodings.size());
  ASSERT_TRUE(parameters.encodings[0].active);
  ASSERT_TRUE(parameters.encodings[1].active);
  ASSERT_TRUE(parameters.encodings[2].active);
  // Invert value to verify copying.
  parameters.encodings[1].active = false;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(ssrcs[0], parameters).ok());

  VideoEncoderConfig encoder_config = stream->GetEncoderConfig().Copy();

  // number_of_streams should be 1 since all spatial layers are sent on the
  // same SSRC. But encoding parameters of all layers is supposed to be copied
  // and stored in simulcast_layers[].
  EXPECT_EQ(1u, encoder_config.number_of_streams);
  EXPECT_EQ(encoder_config.simulcast_layers.size(), kNumSpatialLayers);
  EXPECT_TRUE(encoder_config.simulcast_layers[0].active);
  EXPECT_FALSE(encoder_config.simulcast_layers[1].active);
  EXPECT_TRUE(encoder_config.simulcast_layers[2].active);
}

TEST_F(Vp9SettingsTest, MaxBitrateDeterminedBySvcResolutions) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs3);

  FakeVideoSendStream* stream =
      AddSendStream(CreateSimStreamParams("cname", ssrcs));

  FrameForwarder frame_forwarder;
  EXPECT_TRUE(send_channel_->SetVideoSend(ssrcs[0], nullptr, &frame_forwarder));
  send_channel_->SetSend(true);

  // Send frame at 1080p@30fps.
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame(
      1920, 1080, VideoRotation::kVideoRotation_0, TimeDelta::Millis(33)));

  VideoCodecVP9 vp9_settings;
  ASSERT_TRUE(stream->GetVp9Settings(&vp9_settings)) << "No VP9 config set.";

  const size_t kNumSpatialLayers = ssrcs.size();
  const size_t kNumTemporalLayers = 3;
  EXPECT_EQ(vp9_settings.numberOfSpatialLayers, kNumSpatialLayers);
  EXPECT_EQ(vp9_settings.numberOfTemporalLayers, kNumTemporalLayers);

  EXPECT_TRUE(send_channel_->SetVideoSend(ssrcs[0], nullptr, nullptr));

  // VideoStream max bitrate should be more than legacy 2.5Mbps default stream
  // cap.
  EXPECT_THAT(stream->GetVideoStreams(),
              ElementsAre(Field(&VideoStream::max_bitrate_bps, Gt(2500000))));

  // Update send parameters to 2Mbps, this should cap the max bitrate of the
  // stream.
  parameters.max_bandwidth_bps = 2000000;
  send_channel_->SetSenderParameters(parameters);
  EXPECT_THAT(stream->GetVideoStreams(),
              ElementsAre(Field(&VideoStream::max_bitrate_bps, Eq(2000000))));
}

TEST_F(Vp9SettingsTest, Vp9SvcTargetBitrateCappedByMax) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs3);

  FakeVideoSendStream* stream =
      AddSendStream(CreateSimStreamParams("cname", ssrcs));

  FrameForwarder frame_forwarder;
  EXPECT_TRUE(send_channel_->SetVideoSend(ssrcs[0], nullptr, &frame_forwarder));
  send_channel_->SetSend(true);

  // Set up 3 spatial layers with 720p, which should result in a max bitrate of
  // 2084 kbps.
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame(
      1280, 720, VideoRotation::kVideoRotation_0, TimeDelta::Millis(33)));

  VideoCodecVP9 vp9_settings;
  ASSERT_TRUE(stream->GetVp9Settings(&vp9_settings)) << "No VP9 config set.";

  const size_t kNumSpatialLayers = ssrcs.size();
  const size_t kNumTemporalLayers = 3;
  EXPECT_EQ(vp9_settings.numberOfSpatialLayers, kNumSpatialLayers);
  EXPECT_EQ(vp9_settings.numberOfTemporalLayers, kNumTemporalLayers);

  EXPECT_TRUE(send_channel_->SetVideoSend(ssrcs[0], nullptr, nullptr));

  // VideoStream both min and max bitrate should be lower than legacy 2.5Mbps
  // default stream cap.
  EXPECT_THAT(stream->GetVideoStreams()[0],
              AllOf(Field(&VideoStream::max_bitrate_bps, Lt(2500000)),
                    Field(&VideoStream::target_bitrate_bps, Lt(2500000))));
}

class Vp9SettingsTestWithFieldTrial
    : public Vp9SettingsTest,
      public ::testing::WithParamInterface<
          ::testing::tuple<const char*, int, int, InterLayerPredMode>> {
 protected:
  Vp9SettingsTestWithFieldTrial()
      : Vp9SettingsTest(::testing::get<0>(GetParam())),
        num_spatial_layers_(::testing::get<1>(GetParam())),
        num_temporal_layers_(::testing::get<2>(GetParam())),
        inter_layer_pred_mode_(::testing::get<3>(GetParam())) {}

  void VerifySettings(int num_spatial_layers,
                      int num_temporal_layers,
                      InterLayerPredMode /* interLayerPred */) {
    VideoSenderParameters parameters;
    parameters.codecs.push_back(GetEngineCodec("VP9"));
    ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

    FakeVideoSendStream* stream = SetUpSimulcast(false, /*with_rtx=*/false);

    FrameForwarder frame_forwarder;
    EXPECT_TRUE(
        send_channel_->SetVideoSend(last_ssrc_, nullptr, &frame_forwarder));
    send_channel_->SetSend(true);

    frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

    VideoCodecVP9 vp9_settings;
    ASSERT_TRUE(stream->GetVp9Settings(&vp9_settings)) << "No VP9 config set.";
    EXPECT_EQ(num_spatial_layers, vp9_settings.numberOfSpatialLayers);
    EXPECT_EQ(num_temporal_layers, vp9_settings.numberOfTemporalLayers);
    EXPECT_EQ(inter_layer_pred_mode_, vp9_settings.interLayerPred);

    EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
  }

  const uint8_t num_spatial_layers_;
  const uint8_t num_temporal_layers_;
  const InterLayerPredMode inter_layer_pred_mode_;
};

TEST_P(Vp9SettingsTestWithFieldTrial, VerifyCodecSettings) {
  VerifySettings(num_spatial_layers_, num_temporal_layers_,
                 inter_layer_pred_mode_);
}

INSTANTIATE_TEST_SUITE_P(
    All,
    Vp9SettingsTestWithFieldTrial,
    Values(
        std::make_tuple("", 1, 1, InterLayerPredMode::kOnKeyPic),
        std::make_tuple("WebRTC-Vp9InterLayerPred/Default/",
                        1,
                        1,
                        InterLayerPredMode::kOnKeyPic),
        std::make_tuple("WebRTC-Vp9InterLayerPred/Disabled/",
                        1,
                        1,
                        InterLayerPredMode::kOnKeyPic),
        std::make_tuple(
            "WebRTC-Vp9InterLayerPred/Enabled,inter_layer_pred_mode:off/",
            1,
            1,
            InterLayerPredMode::kOff),
        std::make_tuple(
            "WebRTC-Vp9InterLayerPred/Enabled,inter_layer_pred_mode:on/",
            1,
            1,
            InterLayerPredMode::kOn),
        std::make_tuple(
            "WebRTC-Vp9InterLayerPred/Enabled,inter_layer_pred_mode:onkeypic/",
            1,
            1,
            InterLayerPredMode::kOnKeyPic)));

TEST_F(WebRtcVideoChannelTest, VerifyMinBitrate) {
  std::vector<VideoStream> streams = AddSendStream()->GetVideoStreams();
  ASSERT_EQ(1u, streams.size());
  EXPECT_EQ(kDefaultMinVideoBitrateBps, streams[0].min_bitrate_bps);
}

class WebRtcVideoChanneWithForcedFallbackTest : public WebRtcVideoChannelTest {
 public:
  WebRtcVideoChanneWithForcedFallbackTest()
      : WebRtcVideoChannelTest(
            "WebRTC-VP8-Forced-Fallback-Encoder-v2/Enabled-1,2,34567/") {}
};

TEST_F(WebRtcVideoChanneWithForcedFallbackTest,
       VerifyMinBitrateWithForcedFallbackFieldTrial) {
  std::vector<VideoStream> streams = AddSendStream()->GetVideoStreams();
  ASSERT_EQ(1u, streams.size());
  EXPECT_EQ(34567, streams[0].min_bitrate_bps);
}

TEST_F(WebRtcVideoChannelTest,
       BalancedDegradationPreferenceNotSupportedWithoutFieldtrial) {
  const bool kResolutionScalingEnabled = true;
  const bool kFpsScalingEnabled = false;
  TestDegradationPreference(kResolutionScalingEnabled, kFpsScalingEnabled);
}

class WebRtcVideoChannelWithBalancedDegradationTest
    : public WebRtcVideoChannelTest {
 public:
  WebRtcVideoChannelWithBalancedDegradationTest()
      : WebRtcVideoChannelTest("WebRTC-Video-BalancedDegradation/Enabled/") {}
};

TEST_F(WebRtcVideoChannelWithBalancedDegradationTest,
       BalancedDegradationPreferenceSupportedBehindFieldtrial) {
  const bool kResolutionScalingEnabled = true;
  const bool kFpsScalingEnabled = true;
  TestDegradationPreference(kResolutionScalingEnabled, kFpsScalingEnabled);
}

TEST_F(WebRtcVideoChannelTest, AdaptsOnOveruse) {
  TestCpuAdaptation(true, false);
}

TEST_F(WebRtcVideoChannelTest, DoesNotAdaptOnOveruseWhenDisabled) {
  TestCpuAdaptation(false, false);
}

TEST_F(WebRtcVideoChannelTest, DoesNotAdaptWhenScreeensharing) {
  TestCpuAdaptation(false, true);
}

TEST_F(WebRtcVideoChannelTest, DoesNotAdaptOnOveruseWhenScreensharing) {
  TestCpuAdaptation(true, true);
}

TEST_F(WebRtcVideoChannelTest, PreviousAdaptationDoesNotApplyToScreenshare) {
  Codec codec = GetEngineCodec("VP8");
  VideoSenderParameters parameters;
  parameters.codecs.push_back(codec);

  MediaConfig media_config = GetMediaConfig();
  media_config.video.enable_cpu_adaptation = true;
  send_channel_ = engine_->CreateSendChannel(
      env_, fake_call_.get(), media_config, VideoOptions(), CryptoOptions(),
      video_bitrate_allocator_factory_.get(), nullptr, nullptr);
  receive_channel_ = engine_->CreateReceiveChannel(
      env_, fake_call_.get(), media_config, CryptoOptions(), nullptr,
      [](uint32_t, const RtpPacketInfos&, Timestamp) {});

  send_channel_->OnReadyToSend(true);
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  AddSendStream();
  FrameForwarder frame_forwarder;

  ASSERT_TRUE(send_channel_->SetSend(true));
  VideoOptions camera_options;
  camera_options.is_screencast = false;
  send_channel_->SetVideoSend(last_ssrc_, &camera_options, &frame_forwarder);

  ASSERT_EQ(1u, fake_call_->GetVideoSendStreams().size());
  FakeVideoSendStream* send_stream = fake_call_->GetVideoSendStreams().front();

  EXPECT_TRUE(send_stream->resolution_scaling_enabled());
  // Dont' expect anything on framerate_scaling_enabled, since the default is
  // transitioning from MAINTAIN_FRAMERATE to BALANCED.

  // Switch to screen share. Expect no resolution scaling.
  VideoOptions screenshare_options;
  screenshare_options.is_screencast = true;
  send_channel_->SetVideoSend(last_ssrc_, &screenshare_options,
                              &frame_forwarder);
  ASSERT_EQ(2, fake_call_->GetNumCreatedSendStreams());
  send_stream = fake_call_->GetVideoSendStreams().front();
  EXPECT_FALSE(send_stream->resolution_scaling_enabled());

  // Switch back to the normal capturer. Expect resolution scaling to be
  // reenabled.
  send_channel_->SetVideoSend(last_ssrc_, &camera_options, &frame_forwarder);
  send_stream = fake_call_->GetVideoSendStreams().front();
  ASSERT_EQ(3, fake_call_->GetNumCreatedSendStreams());
  send_stream = fake_call_->GetVideoSendStreams().front();
  EXPECT_TRUE(send_stream->resolution_scaling_enabled());

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

// TODO(asapersson): Remove this test when the balanced field trial is removed.
void WebRtcVideoChannelTest::TestDegradationPreference(
    bool resolution_scaling_enabled,
    bool fps_scaling_enabled) {
  Codec codec = GetEngineCodec("VP8");
  VideoSenderParameters parameters;
  parameters.codecs.push_back(codec);

  MediaConfig media_config = GetMediaConfig();
  media_config.video.enable_cpu_adaptation = true;
  send_channel_ = engine_->CreateSendChannel(
      env_, fake_call_.get(), media_config, VideoOptions(), CryptoOptions(),
      video_bitrate_allocator_factory_.get(), nullptr, nullptr);
  receive_channel_ = engine_->CreateReceiveChannel(
      env_, fake_call_.get(), media_config, CryptoOptions(), nullptr,
      [](uint32_t, const RtpPacketInfos&, Timestamp) {});
  send_channel_->OnReadyToSend(true);

  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  AddSendStream();

  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));

  EXPECT_TRUE(send_channel_->SetSend(true));

  FakeVideoSendStream* send_stream = fake_call_->GetVideoSendStreams().front();
  EXPECT_EQ(resolution_scaling_enabled,
            send_stream->resolution_scaling_enabled());
  EXPECT_EQ(fps_scaling_enabled, send_stream->framerate_scaling_enabled());

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

void WebRtcVideoChannelTest::TestCpuAdaptation(bool enable_overuse,
                                               bool is_screenshare) {
  Codec codec = GetEngineCodec("VP8");
  VideoSenderParameters parameters;
  parameters.codecs.push_back(codec);

  MediaConfig media_config = GetMediaConfig();
  if (enable_overuse) {
    media_config.video.enable_cpu_adaptation = true;
  }
  send_channel_ = engine_->CreateSendChannel(
      env_, fake_call_.get(), media_config, VideoOptions(), CryptoOptions(),
      video_bitrate_allocator_factory_.get(), nullptr, nullptr);
  receive_channel_ = engine_->CreateReceiveChannel(
      env_, fake_call_.get(), media_config, CryptoOptions(), nullptr,
      [](uint32_t, const RtpPacketInfos&, Timestamp) {});
  send_channel_->OnReadyToSend(true);

  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  AddSendStream();

  FrameForwarder frame_forwarder;
  VideoOptions options;
  options.is_screencast = is_screenshare;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));

  EXPECT_TRUE(send_channel_->SetSend(true));

  FakeVideoSendStream* send_stream = fake_call_->GetVideoSendStreams().front();

  if (!enable_overuse) {
    EXPECT_FALSE(send_stream->resolution_scaling_enabled());
    EXPECT_FALSE(send_stream->framerate_scaling_enabled());
  } else if (is_screenshare) {
    EXPECT_FALSE(send_stream->resolution_scaling_enabled());
    EXPECT_TRUE(send_stream->framerate_scaling_enabled());
  } else {
    EXPECT_TRUE(send_stream->resolution_scaling_enabled());
    EXPECT_FALSE(send_stream->framerate_scaling_enabled());
  }
  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest, EstimatesNtpStartTimeCorrectly) {
  // Start at last timestamp to verify that wraparounds are estimated correctly.
  static const uint32_t kInitialTimestamp = 0xFFFFFFFFu;
  static const int64_t kInitialNtpTimeMs = 1247891230;
  static const int kFrameOffsetMs = 20;
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters_));

  FakeVideoReceiveStream* stream = AddRecvStream();
  FakeVideoRenderer renderer;
  EXPECT_TRUE(receive_channel_->SetSink(last_ssrc_, &renderer));

  VideoFrame video_frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(CreateBlackFrameBuffer(4, 4))
          .set_rtp_timestamp(kInitialTimestamp)
          .set_timestamp_us(0)
          .set_rotation(kVideoRotation_0)
          .build();
  // Initial NTP time is not available on the first frame, but should still be
  // able to be estimated.
  stream->InjectFrame(video_frame);

  EXPECT_EQ(1, renderer.num_rendered_frames());

  // This timestamp is kInitialTimestamp (-1) + kFrameOffsetMs * 90, which
  // triggers a constant-overflow warning, hence we're calculating it explicitly
  // here.
  time_controller_.AdvanceTime(TimeDelta::Millis(kFrameOffsetMs));
  video_frame.set_rtp_timestamp(kFrameOffsetMs * 90 - 1);
  video_frame.set_ntp_time_ms(kInitialNtpTimeMs + kFrameOffsetMs);
  stream->InjectFrame(video_frame);

  EXPECT_EQ(2, renderer.num_rendered_frames());

  // Verify that NTP time has been correctly deduced.
  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  ASSERT_EQ(1u, receive_info.receivers.size());
  EXPECT_EQ(kInitialNtpTimeMs,
            receive_info.receivers[0].capture_start_ntp_time_ms);
}

TEST_F(WebRtcVideoChannelTest, SetDefaultSendCodecs) {
  AssignDefaultAptRtxTypes();
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  std::optional<Codec> codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_TRUE(codec->Matches(engine_->LegacySendCodecs()[0]));

  // Using a RTX setup to verify that the default RTX payload type is good.
  const std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs1);
  const std::vector<uint32_t> rtx_ssrcs = MAKE_VECTOR(kRtxSsrcs1);
  FakeVideoSendStream* stream =
      AddSendStream(CreateSimWithRtxStreamParams("cname", ssrcs, rtx_ssrcs));
  const VideoSendStream::Config& config = stream->GetConfig();

  // Make sure NACK and FEC are enabled on the correct payload types.
  EXPECT_EQ(1000, config.rtp.nack.rtp_history_ms);
  EXPECT_EQ(GetEngineCodec("ulpfec").id, config.rtp.ulpfec.ulpfec_payload_type);
  EXPECT_EQ(GetEngineCodec("red").id, config.rtp.ulpfec.red_payload_type);

  EXPECT_EQ(1u, config.rtp.rtx.ssrcs.size());
  EXPECT_EQ(kRtxSsrcs1[0], config.rtp.rtx.ssrcs[0]);
  VerifySendStreamHasRtxTypes(config, default_apt_rtx_types_);
  // TODO(juberti): Check RTCP, PLI, TMMBR.
}

TEST_F(WebRtcVideoChannelTest, SetSendCodecsWithoutPacketization) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  FakeVideoSendStream* stream = AddSendStream();
  const VideoSendStream::Config& config = stream->GetConfig();
  EXPECT_FALSE(config.rtp.raw_payload);
}

TEST_F(WebRtcVideoChannelTest, SetSendCodecsWithPacketization) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.back().packetization = kPacketizationParamRaw;
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  FakeVideoSendStream* stream = AddSendStream();
  const VideoSendStream::Config& config = stream->GetConfig();
  EXPECT_TRUE(config.rtp.raw_payload);
}

// The following four tests ensures that FlexFEC is not activated by default
// when the field trials are not enabled.
// TODO(brandtr): Remove or update these tests when FlexFEC _is_ enabled by
// default.
TEST_F(WebRtcVideoChannelTest, FlexfecSendCodecWithoutSsrcNotExposedByDefault) {
  FakeVideoSendStream* stream = AddSendStream();
  const VideoSendStream::Config& config = stream->GetConfig();

  EXPECT_EQ(-1, config.rtp.flexfec.payload_type);
  EXPECT_EQ(0U, config.rtp.flexfec.ssrc);
  EXPECT_TRUE(config.rtp.flexfec.protected_media_ssrcs.empty());
}

TEST_F(WebRtcVideoChannelTest, FlexfecSendCodecWithSsrcNotExposedByDefault) {
  FakeVideoSendStream* stream = AddSendStream(
      CreatePrimaryWithFecFrStreamParams("cname", kSsrcs1[0], kFlexfecSsrc));
  const VideoSendStream::Config& config = stream->GetConfig();

  EXPECT_EQ(-1, config.rtp.flexfec.payload_type);
  EXPECT_EQ(0U, config.rtp.flexfec.ssrc);
  EXPECT_TRUE(config.rtp.flexfec.protected_media_ssrcs.empty());
}

TEST_F(WebRtcVideoChannelTest, FlexfecRecvCodecWithoutSsrcNotExposedByDefault) {
  AddRecvStream();

  const std::vector<FakeFlexfecReceiveStream*>& streams =
      fake_call_->GetFlexfecReceiveStreams();
  EXPECT_TRUE(streams.empty());
}

TEST_F(WebRtcVideoChannelTest, FlexfecRecvCodecWithSsrcExposedByDefault) {
  AddRecvStream(
      CreatePrimaryWithFecFrStreamParams("cname", kSsrcs1[0], kFlexfecSsrc));

  const std::vector<FakeFlexfecReceiveStream*>& streams =
      fake_call_->GetFlexfecReceiveStreams();
  EXPECT_EQ(1U, streams.size());
}

// TODO(brandtr): When FlexFEC is no longer behind a field trial, merge all
// tests that use this test fixture into the corresponding "non-field trial"
// tests.
class WebRtcVideoChannelFlexfecRecvTest : public WebRtcVideoChannelTest {
 public:
  WebRtcVideoChannelFlexfecRecvTest()
      : WebRtcVideoChannelTest("WebRTC-FlexFEC-03-Advertised/Enabled/") {}
};

TEST_F(WebRtcVideoChannelFlexfecRecvTest,
       DefaultFlexfecCodecHasRembFeedbackParam) {
  EXPECT_TRUE(HasRemb(GetEngineCodec("flexfec-03")));
}

TEST_F(WebRtcVideoChannelFlexfecRecvTest, SetDefaultRecvCodecsWithoutSsrc) {
  AddRecvStream();

  const std::vector<FakeFlexfecReceiveStream*>& streams =
      fake_call_->GetFlexfecReceiveStreams();
  EXPECT_TRUE(streams.empty());

  const std::vector<FakeVideoReceiveStream*>& video_streams =
      fake_call_->GetVideoReceiveStreams();
  ASSERT_EQ(1U, video_streams.size());
  const FakeVideoReceiveStream& video_stream = *video_streams.front();
  const VideoReceiveStreamInterface::Config& video_config =
      video_stream.GetConfig();
  EXPECT_FALSE(video_config.rtp.protected_by_flexfec);
  EXPECT_EQ(video_config.rtp.packet_sink_, nullptr);
}

TEST_F(WebRtcVideoChannelFlexfecRecvTest, SetDefaultRecvCodecsWithSsrc) {
  AddRecvStream(
      CreatePrimaryWithFecFrStreamParams("cname", kSsrcs1[0], kFlexfecSsrc));

  const std::vector<FakeFlexfecReceiveStream*>& streams =
      fake_call_->GetFlexfecReceiveStreams();
  ASSERT_EQ(1U, streams.size());
  const auto* stream = streams.front();
  const FlexfecReceiveStream::Config& config = stream->GetConfig();
  EXPECT_EQ(GetEngineCodec("flexfec-03").id, config.payload_type);
  EXPECT_EQ(kFlexfecSsrc, config.remote_ssrc);
  ASSERT_EQ(1U, config.protected_media_ssrcs.size());
  EXPECT_EQ(kSsrcs1[0], config.protected_media_ssrcs[0]);

  const std::vector<FakeVideoReceiveStream*>& video_streams =
      fake_call_->GetVideoReceiveStreams();
  ASSERT_EQ(1U, video_streams.size());
  const FakeVideoReceiveStream& video_stream = *video_streams.front();
  const VideoReceiveStreamInterface::Config& video_config =
      video_stream.GetConfig();
  EXPECT_TRUE(video_config.rtp.protected_by_flexfec);
  EXPECT_NE(video_config.rtp.packet_sink_, nullptr);
}

// Test changing the configuration after a video stream has been created and
// turn on flexfec. This will result in video stream being reconfigured but not
// recreated because the flexfec stream pointer will be given to the already
// existing video stream instance.
TEST_F(WebRtcVideoChannelFlexfecRecvTest,
       EnablingFlexfecDoesNotRecreateVideoReceiveStream) {
  VideoReceiverParameters recv_parameters;
  recv_parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters));

  AddRecvStream(
      CreatePrimaryWithFecFrStreamParams("cname", kSsrcs1[0], kFlexfecSsrc));
  EXPECT_EQ(1, fake_call_->GetNumCreatedReceiveStreams());
  const std::vector<FakeVideoReceiveStream*>& video_streams =
      fake_call_->GetVideoReceiveStreams();
  ASSERT_EQ(1U, video_streams.size());
  const FakeVideoReceiveStream* video_stream = video_streams.front();
  const VideoReceiveStreamInterface::Config* video_config =
      &video_stream->GetConfig();
  EXPECT_FALSE(video_config->rtp.protected_by_flexfec);
  EXPECT_EQ(video_config->rtp.packet_sink_, nullptr);

  // Enable FlexFEC.
  recv_parameters.codecs.push_back(GetEngineCodec("flexfec-03"));
  ASSERT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters));

  // The count of created streams will remain 2 despite the creation of a new
  // flexfec stream. The existing receive stream will have been reconfigured
  // to use the new flexfec instance.
  EXPECT_EQ(2, fake_call_->GetNumCreatedReceiveStreams())
      << "Enabling FlexFEC should not create VideoReceiveStreamInterface (1).";
  EXPECT_EQ(1U, fake_call_->GetVideoReceiveStreams().size())
      << "Enabling FlexFEC should not create VideoReceiveStreamInterface (2).";
  EXPECT_EQ(1U, fake_call_->GetFlexfecReceiveStreams().size())
      << "Enabling FlexFEC should create a single FlexfecReceiveStream.";
  video_stream = video_streams.front();
  video_config = &video_stream->GetConfig();
  EXPECT_TRUE(video_config->rtp.protected_by_flexfec);
  EXPECT_NE(video_config->rtp.packet_sink_, nullptr);
}

// Test changing the configuration after a video stream has been created with
// flexfec enabled and then turn off flexfec. This will not result in the video
// stream being recreated. The flexfec stream pointer that's held by the video
// stream will be set/cleared as dictated by the configuration change.
TEST_F(WebRtcVideoChannelFlexfecRecvTest,
       DisablingFlexfecDoesNotRecreateVideoReceiveStream) {
  VideoReceiverParameters recv_parameters;
  recv_parameters.codecs.push_back(GetEngineCodec("VP8"));
  recv_parameters.codecs.push_back(GetEngineCodec("flexfec-03"));
  ASSERT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters));

  AddRecvStream(
      CreatePrimaryWithFecFrStreamParams("cname", kSsrcs1[0], kFlexfecSsrc));
  EXPECT_EQ(2, fake_call_->GetNumCreatedReceiveStreams());
  EXPECT_EQ(1U, fake_call_->GetFlexfecReceiveStreams().size());
  const std::vector<FakeVideoReceiveStream*>& video_streams =
      fake_call_->GetVideoReceiveStreams();
  ASSERT_EQ(1U, video_streams.size());
  const FakeVideoReceiveStream* video_stream = video_streams.front();
  const VideoReceiveStreamInterface::Config* video_config =
      &video_stream->GetConfig();
  EXPECT_TRUE(video_config->rtp.protected_by_flexfec);
  EXPECT_NE(video_config->rtp.packet_sink_, nullptr);

  // Disable FlexFEC.
  recv_parameters.codecs.clear();
  recv_parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters));
  // The count of created streams should remain 2 since the video stream will
  // have been reconfigured to not reference flexfec and not recreated on
  // account of the flexfec stream being deleted.
  EXPECT_EQ(2, fake_call_->GetNumCreatedReceiveStreams())
      << "Disabling FlexFEC should not recreate VideoReceiveStreamInterface.";
  EXPECT_EQ(1U, fake_call_->GetVideoReceiveStreams().size())
      << "Disabling FlexFEC should not destroy VideoReceiveStreamInterface.";
  EXPECT_TRUE(fake_call_->GetFlexfecReceiveStreams().empty())
      << "Disabling FlexFEC should destroy FlexfecReceiveStream.";
  video_stream = video_streams.front();
  video_config = &video_stream->GetConfig();
  EXPECT_FALSE(video_config->rtp.protected_by_flexfec);
  EXPECT_EQ(video_config->rtp.packet_sink_, nullptr);
}

TEST_F(WebRtcVideoChannelFlexfecRecvTest, DuplicateFlexfecCodecIsDropped) {
  constexpr int kUnusedPayloadType1 = 127;

  VideoReceiverParameters recv_parameters;
  recv_parameters.codecs.push_back(GetEngineCodec("VP8"));
  recv_parameters.codecs.push_back(GetEngineCodec("flexfec-03"));
  Codec duplicate = GetEngineCodec("flexfec-03");
  duplicate.id = kUnusedPayloadType1;
  recv_parameters.codecs.push_back(duplicate);
  ASSERT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters));

  AddRecvStream(
      CreatePrimaryWithFecFrStreamParams("cname", kSsrcs1[0], kFlexfecSsrc));

  const std::vector<FakeFlexfecReceiveStream*>& streams =
      fake_call_->GetFlexfecReceiveStreams();
  ASSERT_EQ(1U, streams.size());
  const auto* stream = streams.front();
  const FlexfecReceiveStream::Config& config = stream->GetConfig();
  EXPECT_EQ(GetEngineCodec("flexfec-03").id, config.payload_type);
}

// TODO(brandtr): When FlexFEC is no longer behind a field trial, merge all
// tests that use this test fixture into the corresponding "non-field trial"
// tests.
class WebRtcVideoChannelFlexfecSendRecvTest : public WebRtcVideoChannelTest {
 public:
  WebRtcVideoChannelFlexfecSendRecvTest()
      : WebRtcVideoChannelTest(
            "WebRTC-FlexFEC-03-Advertised/Enabled/WebRTC-FlexFEC-03/Enabled/") {
  }
};

TEST_F(WebRtcVideoChannelFlexfecSendRecvTest, SetDefaultSendCodecsWithoutSsrc) {
  FakeVideoSendStream* stream = AddSendStream();
  const VideoSendStream::Config& config = stream->GetConfig();

  EXPECT_EQ(GetEngineCodec("flexfec-03").id, config.rtp.flexfec.payload_type);
  EXPECT_EQ(0U, config.rtp.flexfec.ssrc);
  EXPECT_TRUE(config.rtp.flexfec.protected_media_ssrcs.empty());
}

TEST_F(WebRtcVideoChannelFlexfecSendRecvTest, SetDefaultSendCodecsWithSsrc) {
  FakeVideoSendStream* stream = AddSendStream(
      CreatePrimaryWithFecFrStreamParams("cname", kSsrcs1[0], kFlexfecSsrc));
  const VideoSendStream::Config& config = stream->GetConfig();

  EXPECT_EQ(GetEngineCodec("flexfec-03").id, config.rtp.flexfec.payload_type);
  EXPECT_EQ(kFlexfecSsrc, config.rtp.flexfec.ssrc);
  ASSERT_EQ(1U, config.rtp.flexfec.protected_media_ssrcs.size());
  EXPECT_EQ(kSsrcs1[0], config.rtp.flexfec.protected_media_ssrcs[0]);
}

TEST_F(WebRtcVideoChannelTest, SetSendCodecsWithoutFec) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  FakeVideoSendStream* stream = AddSendStream();
  const VideoSendStream::Config& config = stream->GetConfig();

  EXPECT_EQ(-1, config.rtp.ulpfec.ulpfec_payload_type);
  EXPECT_EQ(-1, config.rtp.ulpfec.red_payload_type);
}

TEST_F(WebRtcVideoChannelFlexfecSendRecvTest, SetSendCodecsWithoutFec) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  FakeVideoSendStream* stream = AddSendStream();
  const VideoSendStream::Config& config = stream->GetConfig();

  EXPECT_EQ(-1, config.rtp.flexfec.payload_type);
}

TEST_F(WebRtcVideoChannelFlexfecRecvTest, SetRecvCodecsWithFec) {
  AddRecvStream(
      CreatePrimaryWithFecFrStreamParams("cname", kSsrcs1[0], kFlexfecSsrc));

  VideoReceiverParameters recv_parameters;
  recv_parameters.codecs.push_back(GetEngineCodec("VP8"));
  recv_parameters.codecs.push_back(GetEngineCodec("flexfec-03"));
  ASSERT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters));

  const std::vector<FakeFlexfecReceiveStream*>& flexfec_streams =
      fake_call_->GetFlexfecReceiveStreams();
  ASSERT_EQ(1U, flexfec_streams.size());
  const FakeFlexfecReceiveStream* flexfec_stream = flexfec_streams.front();
  const FlexfecReceiveStream::Config& flexfec_stream_config =
      flexfec_stream->GetConfig();
  EXPECT_EQ(GetEngineCodec("flexfec-03").id,
            flexfec_stream_config.payload_type);
  EXPECT_EQ(kFlexfecSsrc, flexfec_stream_config.remote_ssrc);
  ASSERT_EQ(1U, flexfec_stream_config.protected_media_ssrcs.size());
  EXPECT_EQ(kSsrcs1[0], flexfec_stream_config.protected_media_ssrcs[0]);
  const std::vector<FakeVideoReceiveStream*>& video_streams =
      fake_call_->GetVideoReceiveStreams();
  const FakeVideoReceiveStream* video_stream = video_streams.front();
  const VideoReceiveStreamInterface::Config& video_stream_config =
      video_stream->GetConfig();
  EXPECT_EQ(video_stream_config.rtp.rtcp_mode, flexfec_stream_config.rtcp_mode);
  EXPECT_EQ(video_stream_config.rtcp_send_transport,
            flexfec_stream_config.rtcp_send_transport);
  EXPECT_EQ(video_stream_config.rtp.rtcp_mode, flexfec_stream_config.rtcp_mode);
}

// We should not send FlexFEC, even if we advertise it, unless the right
// field trial is set.
// TODO(brandtr): Remove when FlexFEC is enabled by default.
TEST_F(WebRtcVideoChannelFlexfecRecvTest,
       SetSendCodecsWithoutSsrcWithFecDoesNotEnableFec) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("flexfec-03"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  FakeVideoSendStream* stream = AddSendStream();
  const VideoSendStream::Config& config = stream->GetConfig();

  EXPECT_EQ(-1, config.rtp.flexfec.payload_type);
  EXPECT_EQ(0u, config.rtp.flexfec.ssrc);
  EXPECT_TRUE(config.rtp.flexfec.protected_media_ssrcs.empty());
}

TEST_F(WebRtcVideoChannelFlexfecRecvTest,
       SetSendCodecsWithSsrcWithFecDoesNotEnableFec) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("flexfec-03"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  FakeVideoSendStream* stream = AddSendStream(
      CreatePrimaryWithFecFrStreamParams("cname", kSsrcs1[0], kFlexfecSsrc));
  const VideoSendStream::Config& config = stream->GetConfig();

  EXPECT_EQ(-1, config.rtp.flexfec.payload_type);
  EXPECT_EQ(0u, config.rtp.flexfec.ssrc);
  EXPECT_TRUE(config.rtp.flexfec.protected_media_ssrcs.empty());
}

TEST_F(WebRtcVideoChannelTest,
       SetSendCodecRejectsRtxWithoutAssociatedPayloadType) {
  const int kUnusedPayloadType = 127;
  EXPECT_FALSE(FindCodecById(engine_->LegacySendCodecs(), kUnusedPayloadType));

  VideoSenderParameters parameters;
  Codec rtx_codec = CreateVideoCodec(kUnusedPayloadType, "rtx");
  parameters.codecs.push_back(rtx_codec);
  EXPECT_FALSE(send_channel_->SetSenderParameters(parameters))
      << "RTX codec without associated payload type should be rejected.";
}

TEST_F(WebRtcVideoChannelTest,
       SetSendCodecRejectsRtxWithoutMatchingVideoCodec) {
  const int kUnusedPayloadType1 = 126;
  const int kUnusedPayloadType2 = 127;
  EXPECT_FALSE(FindCodecById(engine_->LegacySendCodecs(), kUnusedPayloadType1));
  EXPECT_FALSE(FindCodecById(engine_->LegacySendCodecs(), kUnusedPayloadType2));
  {
    Codec rtx_codec =
        CreateVideoRtxCodec(kUnusedPayloadType1, GetEngineCodec("VP8").id);
    VideoSenderParameters parameters;
    parameters.codecs.push_back(GetEngineCodec("VP8"));
    parameters.codecs.push_back(rtx_codec);
    ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));
  }
  {
    Codec rtx_codec =
        CreateVideoRtxCodec(kUnusedPayloadType1, kUnusedPayloadType2);
    VideoSenderParameters parameters;
    parameters.codecs.push_back(GetEngineCodec("VP8"));
    parameters.codecs.push_back(rtx_codec);
    EXPECT_FALSE(send_channel_->SetSenderParameters(parameters))
        << "RTX without matching video codec should be rejected.";
  }
}

TEST_F(WebRtcVideoChannelTest, SetSendCodecsWithChangedRtxPayloadType) {
  const int kUnusedPayloadType1 = 126;
  const int kUnusedPayloadType2 = 127;
  EXPECT_FALSE(FindCodecById(engine_->LegacySendCodecs(), kUnusedPayloadType1));
  EXPECT_FALSE(FindCodecById(engine_->LegacySendCodecs(), kUnusedPayloadType2));

  // SSRCs for RTX.
  StreamParams params = StreamParams::CreateLegacy(kSsrcs1[0]);
  params.AddFidSsrc(kSsrcs1[0], kRtxSsrcs1[0]);
  AddSendStream(params);

  // Original payload type for RTX.
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  Codec rtx_codec = CreateVideoCodec(kUnusedPayloadType1, "rtx");
  rtx_codec.SetParam("apt", GetEngineCodec("VP8").id);
  parameters.codecs.push_back(rtx_codec);
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  ASSERT_EQ(1U, fake_call_->GetVideoSendStreams().size());
  const VideoSendStream::Config& config_before =
      fake_call_->GetVideoSendStreams()[0]->GetConfig();
  EXPECT_EQ(kUnusedPayloadType1, config_before.rtp.rtx.payload_type);
  ASSERT_EQ(1U, config_before.rtp.rtx.ssrcs.size());
  EXPECT_EQ(kRtxSsrcs1[0], config_before.rtp.rtx.ssrcs[0]);

  // Change payload type for RTX.
  parameters.codecs[1].id = kUnusedPayloadType2;
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  ASSERT_EQ(1U, fake_call_->GetVideoSendStreams().size());
  const VideoSendStream::Config& config_after =
      fake_call_->GetVideoSendStreams()[0]->GetConfig();
  EXPECT_EQ(kUnusedPayloadType2, config_after.rtp.rtx.payload_type);
  ASSERT_EQ(1U, config_after.rtp.rtx.ssrcs.size());
  EXPECT_EQ(kRtxSsrcs1[0], config_after.rtp.rtx.ssrcs[0]);
}

TEST_F(WebRtcVideoChannelTest, SetSendCodecsWithoutFecDisablesFec) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("ulpfec"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  FakeVideoSendStream* stream = AddSendStream();
  const VideoSendStream::Config& config = stream->GetConfig();

  EXPECT_EQ(GetEngineCodec("ulpfec").id, config.rtp.ulpfec.ulpfec_payload_type);

  parameters.codecs.pop_back();
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));
  stream = fake_call_->GetVideoSendStreams()[0];
  ASSERT_TRUE(stream != nullptr);
  const VideoSendStream::Config& config2 = stream->GetConfig();
  EXPECT_EQ(-1, config2.rtp.ulpfec.ulpfec_payload_type)
      << "SetSendCodec without ULPFEC should disable current ULPFEC.";
}

TEST_F(WebRtcVideoChannelFlexfecSendRecvTest,
       SetSendCodecsWithoutFecDisablesFec) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("flexfec-03"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  FakeVideoSendStream* stream = AddSendStream(
      CreatePrimaryWithFecFrStreamParams("cname", kSsrcs1[0], kFlexfecSsrc));
  const VideoSendStream::Config& config = stream->GetConfig();

  EXPECT_EQ(GetEngineCodec("flexfec-03").id, config.rtp.flexfec.payload_type);
  EXPECT_EQ(kFlexfecSsrc, config.rtp.flexfec.ssrc);
  ASSERT_EQ(1U, config.rtp.flexfec.protected_media_ssrcs.size());
  EXPECT_EQ(kSsrcs1[0], config.rtp.flexfec.protected_media_ssrcs[0]);

  parameters.codecs.pop_back();
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));
  stream = fake_call_->GetVideoSendStreams()[0];
  ASSERT_TRUE(stream != nullptr);
  const VideoSendStream::Config& config2 = stream->GetConfig();
  EXPECT_EQ(-1, config2.rtp.flexfec.payload_type)
      << "SetSendCodec without FlexFEC should disable current FlexFEC.";
}

TEST_F(WebRtcVideoChannelTest, SetSendCodecsChangesExistingStreams) {
  VideoSenderParameters parameters;
  Codec codec = CreateVideoCodec(100, "VP8");
  codec.SetParam(kCodecParamMaxQuantization, kDefaultVideoMaxQpVpx);
  parameters.codecs.push_back(codec);

  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));
  send_channel_->SetSend(true);

  FakeVideoSendStream* stream = AddSendStream();
  FrameForwarder frame_forwarder;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, nullptr, &frame_forwarder));

  std::vector<VideoStream> streams = stream->GetVideoStreams();
  EXPECT_EQ(kDefaultVideoMaxQpVpx, streams[0].max_qp);

  parameters.codecs.clear();
  codec.SetParam(kCodecParamMaxQuantization, kDefaultVideoMaxQpVpx + 1);
  parameters.codecs.push_back(codec);
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));
  streams = fake_call_->GetVideoSendStreams()[0]->GetVideoStreams();
  EXPECT_EQ(kDefaultVideoMaxQpVpx + 1, streams[0].max_qp);
  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest, SetSendCodecsWithBitrates) {
  SetSendCodecsShouldWorkForBitrates("100", 100000, "150", 150000, "200",
                                     200000);
}

TEST_F(WebRtcVideoChannelTest, SetSendCodecsWithHighMaxBitrate) {
  SetSendCodecsShouldWorkForBitrates("", 0, "", -1, "10000", 10000000);
  std::vector<VideoStream> streams = AddSendStream()->GetVideoStreams();
  ASSERT_EQ(1u, streams.size());
  EXPECT_EQ(10000000, streams[0].max_bitrate_bps);
}

TEST_F(WebRtcVideoChannelTest,
       SetSendCodecsWithoutBitratesUsesCorrectDefaults) {
  SetSendCodecsShouldWorkForBitrates("", 0, "", -1, "", -1);
}

TEST_F(WebRtcVideoChannelTest, SetSendCodecsCapsMinAndStartBitrate) {
  SetSendCodecsShouldWorkForBitrates("-1", 0, "-100", -1, "", -1);
}

TEST_F(WebRtcVideoChannelTest, SetSendCodecsRejectsMaxLessThanMinBitrate) {
  send_parameters_.codecs[0].SetParam(kCodecParamMinBitrate, "300");
  send_parameters_.codecs[0].SetParam(kCodecParamMaxBitrate, "200");
  EXPECT_FALSE(send_channel_->SetSenderParameters(send_parameters_));
}

TEST_F(WebRtcVideoChannelTest,
       SetSenderParametersRemovesSelectedCodecFromRtpParameters) {
  EXPECT_TRUE(AddSendStream());
  VideoSenderParameters parameters;
  parameters.codecs.push_back(CreateVideoCodec(100, "VP8"));
  parameters.codecs.push_back(CreateVideoCodec(100, "VP9"));
  send_channel_->SetSenderParameters(parameters);

  RtpParameters initial_params =
      send_channel_->GetRtpSendParameters(last_ssrc_);

  RtpCodec vp9_rtp_codec;
  vp9_rtp_codec.name = "VP9";
  vp9_rtp_codec.kind = MediaType::VIDEO;
  vp9_rtp_codec.clock_rate = 90000;
  initial_params.encodings[0].codec = vp9_rtp_codec;

  // We should be able to set the params with the VP9 codec that has been
  // negotiated.
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, initial_params).ok());

  parameters.codecs.clear();
  parameters.codecs.push_back(CreateVideoCodec(100, "VP8"));
  send_channel_->SetSenderParameters(parameters);

  // Since VP9 is no longer negotiated, the RTP parameters should not have a
  // forced codec anymore.
  RtpParameters new_params = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(new_params.encodings[0].codec, std::nullopt);
}

// Test that when both the codec-specific bitrate params and max_bandwidth_bps
// are present in the same send parameters, the settings are combined correctly.
TEST_F(WebRtcVideoChannelTest, SetSendCodecsWithBitratesAndMaxSendBandwidth) {
  send_parameters_.codecs[0].SetParam(kCodecParamMinBitrate, "100");
  send_parameters_.codecs[0].SetParam(kCodecParamStartBitrate, "200");
  send_parameters_.codecs[0].SetParam(kCodecParamMaxBitrate, "300");
  send_parameters_.max_bandwidth_bps = 400000;
  // We expect max_bandwidth_bps to take priority, if set.
  ExpectSetBitrateParameters(100000, 200000, 400000);
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
  // Since the codec isn't changing, start_bitrate_bps should be -1.
  ExpectSetBitrateParameters(100000, -1, 350000);

  // Decrease max_bandwidth_bps.
  send_parameters_.max_bandwidth_bps = 350000;
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  // Now try again with the values flipped around.
  send_parameters_.codecs[0].SetParam(kCodecParamMaxBitrate, "400");
  send_parameters_.max_bandwidth_bps = 300000;
  ExpectSetBitrateParameters(100000, 200000, 300000);
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  // If we change the codec max, max_bandwidth_bps should still apply.
  send_parameters_.codecs[0].SetParam(kCodecParamMaxBitrate, "350");
  ExpectSetBitrateParameters(100000, 200000, 300000);
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
}

TEST_F(WebRtcVideoChannelTest, SetMaxSendBandwidthShouldPreserveOtherBitrates) {
  SetSendCodecsShouldWorkForBitrates("100", 100000, "150", 150000, "200",
                                     200000);
  send_parameters_.max_bandwidth_bps = 300000;
  // Setting max bitrate should keep previous min bitrate.
  // Setting max bitrate should not reset start bitrate.
  ExpectSetBitrateParameters(100000, -1, 300000);
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
}

TEST_F(WebRtcVideoChannelTest, SetMaxSendBandwidthShouldBeRemovable) {
  send_parameters_.max_bandwidth_bps = 300000;
  ExpectSetMaxBitrate(300000);
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
  // -1 means to disable max bitrate (set infinite).
  send_parameters_.max_bandwidth_bps = -1;
  ExpectSetMaxBitrate(-1);
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
}

TEST_F(WebRtcVideoChannelTest, SetMaxSendBandwidthAndAddSendStream) {
  send_parameters_.max_bandwidth_bps = 99999;
  FakeVideoSendStream* stream = AddSendStream();
  ExpectSetMaxBitrate(send_parameters_.max_bandwidth_bps);
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
  ASSERT_EQ(1u, stream->GetVideoStreams().size());
  EXPECT_EQ(send_parameters_.max_bandwidth_bps,
            stream->GetVideoStreams()[0].max_bitrate_bps);

  send_parameters_.max_bandwidth_bps = 77777;
  ExpectSetMaxBitrate(send_parameters_.max_bandwidth_bps);
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
  EXPECT_EQ(send_parameters_.max_bandwidth_bps,
            stream->GetVideoStreams()[0].max_bitrate_bps);
}

TEST_F(WebRtcVideoChannelTest, MaxBitrateZeroDisablesCodecMaxBitrateFallback) {
  // "x-google-max-bitrate" only applies if no encoding configured a maximum.
  send_parameters_.codecs[0].SetParam(kCodecParamMaxBitrate, "300");
  send_parameters_.max_bandwidth_bps = -1;
  AddSendStream();
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  // SetSenderParameters() recreates the send stream.
  std::vector<FakeVideoSendStream*> send_streams = GetFakeSendStreams();
  ASSERT_EQ(1u, send_streams.size());
  FakeVideoSendStream* stream = send_streams[0];
  ASSERT_EQ(1u, stream->GetVideoStreams().size());
  EXPECT_EQ(300000, stream->GetVideoStreams()[0].max_bitrate_bps);

  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  ASSERT_EQ(1u, parameters.encodings.size());
  parameters.encodings[0].max_bitrate_bps = 0;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
  ASSERT_EQ(1u, stream->GetVideoStreams().size());
  EXPECT_EQ(0, stream->GetVideoStreams()[0].max_bitrate_bps);
}

// Tests that when the codec specific max bitrate and VideoSenderParameters
// max_bandwidth_bps are used, that it sets the VideoStream's max bitrate
// appropriately.
TEST_F(WebRtcVideoChannelTest,
       MaxBitratePrioritizesVideoSendParametersOverCodecMaxBitrate) {
  send_parameters_.codecs[0].SetParam(kCodecParamMinBitrate, "100");
  send_parameters_.codecs[0].SetParam(kCodecParamStartBitrate, "200");
  send_parameters_.codecs[0].SetParam(kCodecParamMaxBitrate, "300");
  send_parameters_.max_bandwidth_bps = -1;
  AddSendStream();
  ExpectSetMaxBitrate(300000);
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  std::vector<FakeVideoSendStream*> video_send_streams = GetFakeSendStreams();
  ASSERT_EQ(1u, video_send_streams.size());
  FakeVideoSendStream* video_send_stream = video_send_streams[0];
  ASSERT_EQ(1u, video_send_streams[0]->GetVideoStreams().size());
  // First the max bitrate is set based upon the codec param.
  EXPECT_EQ(300000,
            video_send_streams[0]->GetVideoStreams()[0].max_bitrate_bps);

  // The VideoSenderParameters max bitrate overrides the codec's.
  send_parameters_.max_bandwidth_bps = 500000;
  ExpectSetMaxBitrate(send_parameters_.max_bandwidth_bps);
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
  ASSERT_EQ(1u, video_send_stream->GetVideoStreams().size());
  EXPECT_EQ(500000, video_send_stream->GetVideoStreams()[0].max_bitrate_bps);
}

// Tests that when the codec specific max bitrate and RtpParameters
// max_bitrate_bps are used, that it sets the VideoStream's max bitrate
// appropriately.
TEST_F(WebRtcVideoChannelTest,
       MaxBitratePrioritizesRtpParametersOverCodecMaxBitrate) {
  send_parameters_.codecs[0].SetParam(kCodecParamMinBitrate, "100");
  send_parameters_.codecs[0].SetParam(kCodecParamStartBitrate, "200");
  send_parameters_.codecs[0].SetParam(kCodecParamMaxBitrate, "300");
  send_parameters_.max_bandwidth_bps = -1;
  AddSendStream();
  ExpectSetMaxBitrate(300000);
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  std::vector<FakeVideoSendStream*> video_send_streams = GetFakeSendStreams();
  ASSERT_EQ(1u, video_send_streams.size());
  FakeVideoSendStream* video_send_stream = video_send_streams[0];
  ASSERT_EQ(1u, video_send_stream->GetVideoStreams().size());
  // First the max bitrate is set based upon the codec param.
  EXPECT_EQ(300000, video_send_stream->GetVideoStreams()[0].max_bitrate_bps);

  // The RtpParameter max bitrate overrides the codec's.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  ASSERT_EQ(1u, parameters.encodings.size());
  parameters.encodings[0].max_bitrate_bps = 500000;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
  ASSERT_EQ(1u, video_send_stream->GetVideoStreams().size());
  EXPECT_EQ(parameters.encodings[0].max_bitrate_bps,
            video_send_stream->GetVideoStreams()[0].max_bitrate_bps);
}

TEST_F(WebRtcVideoChannelTest,
       MaxBitrateIsMinimumOfMaxSendBandwidthAndMaxEncodingBitrate) {
  send_parameters_.max_bandwidth_bps = 99999;
  FakeVideoSendStream* stream = AddSendStream();
  ExpectSetMaxBitrate(send_parameters_.max_bandwidth_bps);
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
  ASSERT_EQ(1u, stream->GetVideoStreams().size());
  EXPECT_EQ(send_parameters_.max_bandwidth_bps,
            stream->GetVideoStreams()[0].max_bitrate_bps);

  // Get and set the rtp encoding parameters.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(1u, parameters.encodings.size());

  parameters.encodings[0].max_bitrate_bps = 99999 - 1;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
  EXPECT_EQ(parameters.encodings[0].max_bitrate_bps,
            stream->GetVideoStreams()[0].max_bitrate_bps);

  parameters.encodings[0].max_bitrate_bps = 99999 + 1;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
  EXPECT_EQ(send_parameters_.max_bandwidth_bps,
            stream->GetVideoStreams()[0].max_bitrate_bps);
}

TEST_F(WebRtcVideoChannelTest, SetMaxSendBitrateCanIncreaseSenderBitrate) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));
  send_channel_->SetSend(true);

  FakeVideoSendStream* stream = AddSendStream();

  FrameForwarder frame_forwarder;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, nullptr, &frame_forwarder));

  std::vector<VideoStream> streams = stream->GetVideoStreams();
  int initial_max_bitrate_bps = streams[0].max_bitrate_bps;
  EXPECT_GT(initial_max_bitrate_bps, 0);

  parameters.max_bandwidth_bps = initial_max_bitrate_bps * 2;
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  // Insert a frame to update the encoder config.
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());
  streams = stream->GetVideoStreams();
  EXPECT_EQ(initial_max_bitrate_bps * 2, streams[0].max_bitrate_bps);
  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest,
       SetMaxSendBitrateCanIncreaseSimulcastSenderBitrate) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));
  send_channel_->SetSend(true);

  FakeVideoSendStream* stream =
      AddSendStream(CreateSimStreamParams("cname", MAKE_VECTOR(kSsrcs3)));

  // Send a frame to make sure this scales up to >1 stream (simulcast).
  FrameForwarder frame_forwarder;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(kSsrcs3[0], nullptr, &frame_forwarder));
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  std::vector<VideoStream> streams = stream->GetVideoStreams();
  ASSERT_GT(streams.size(), 1u)
      << "Without simulcast this test doesn't make sense.";
  int initial_max_bitrate_bps = GetTotalMaxBitrate(streams).bps();
  EXPECT_GT(initial_max_bitrate_bps, 0);

  parameters.max_bandwidth_bps = initial_max_bitrate_bps * 2;
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  // Insert a frame to update the encoder config.
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());
  streams = stream->GetVideoStreams();
  int increased_max_bitrate_bps = GetTotalMaxBitrate(streams).bps();
  EXPECT_EQ(initial_max_bitrate_bps * 2, increased_max_bitrate_bps);

  EXPECT_TRUE(send_channel_->SetVideoSend(kSsrcs3[0], nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest, SetSendCodecsWithMaxQuantization) {
  static const char* kMaxQuantization = "21";
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs[0].SetParam(kCodecParamMaxQuantization, kMaxQuantization);
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  EXPECT_EQ(atoi(kMaxQuantization),
            AddSendStream()->GetVideoStreams().back().max_qp);

  std::optional<Codec> codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  std::string max_quantization;
  EXPECT_TRUE(codec->GetParam(kCodecParamMaxQuantization, &max_quantization));
  EXPECT_EQ(kMaxQuantization, max_quantization);
}

TEST_F(WebRtcVideoChannelTest, SetSendCodecsRejectBadPayloadTypes) {
  // TODO(pbos): Should we only allow the dynamic range?
  static const int kIncorrectPayloads[] = {-2, -1, 128, 129};
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  for (int incorrect_id : kIncorrectPayloads) {
    parameters.codecs[0].id = incorrect_id;
    EXPECT_FALSE(send_channel_->SetSenderParameters(parameters))
        << "Bad payload type '" << incorrect_id << "' accepted.";
  }
}

TEST_F(WebRtcVideoChannelTest, SetSendCodecsAcceptAllValidPayloadTypes) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  for (int payload_type = 96; payload_type <= 127; ++payload_type) {
    parameters.codecs[0].id = payload_type;
    EXPECT_TRUE(send_channel_->SetSenderParameters(parameters))
        << "Payload type '" << payload_type << "' rejected.";
  }
}

// Test that setting the a different set of codecs but with an identical front
// codec doesn't result in the stream being recreated.
// This may happen when a subsequent negotiation includes fewer codecs, as a
// result of one of the codecs being rejected.
TEST_F(WebRtcVideoChannelTest,
       SetSendCodecsIdenticalFirstCodecDoesntRecreateStream) {
  VideoSenderParameters parameters1;
  parameters1.codecs.push_back(GetEngineCodec("VP8"));
  parameters1.codecs.push_back(GetEngineCodec("VP9"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters1));

  AddSendStream();
  EXPECT_EQ(1, fake_call_->GetNumCreatedSendStreams());

  VideoSenderParameters parameters2;
  parameters2.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters2));
  EXPECT_EQ(1, fake_call_->GetNumCreatedSendStreams());
}

TEST_F(WebRtcVideoChannelTest, SetRecvCodecsWithOnlyVp8) {
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
}

// Test that we set our inbound RTX codecs properly.
TEST_F(WebRtcVideoChannelTest, SetRecvCodecsWithRtx) {
  const int kUnusedPayloadType1 = 126;
  const int kUnusedPayloadType2 = 127;
  EXPECT_FALSE(FindCodecById(engine_->LegacyRecvCodecs(), kUnusedPayloadType1));
  EXPECT_FALSE(FindCodecById(engine_->LegacyRecvCodecs(), kUnusedPayloadType2));

  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  Codec rtx_codec = CreateVideoCodec(kUnusedPayloadType1, "rtx");
  parameters.codecs.push_back(rtx_codec);
  EXPECT_FALSE(receive_channel_->SetReceiverParameters(parameters))
      << "RTX codec without associated payload should be rejected.";

  parameters.codecs[1].SetParam("apt", kUnusedPayloadType2);
  EXPECT_FALSE(receive_channel_->SetReceiverParameters(parameters))
      << "RTX codec with invalid associated payload type should be rejected.";

  parameters.codecs[1].SetParam("apt", GetEngineCodec("VP8").id);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

  Codec rtx_codec2 = CreateVideoCodec(kUnusedPayloadType2, "rtx");
  rtx_codec2.SetParam("apt", rtx_codec.id);
  parameters.codecs.push_back(rtx_codec2);

  EXPECT_FALSE(receive_channel_->SetReceiverParameters(parameters))
      << "RTX codec with another RTX as associated payload type should be "
         "rejected.";
}

TEST_F(WebRtcVideoChannelTest, SetRecvCodecsWithPacketization) {
  Codec vp8_codec = GetEngineCodec("VP8");
  vp8_codec.packetization = kPacketizationParamRaw;

  VideoReceiverParameters parameters;
  parameters.codecs = {vp8_codec, GetEngineCodec("VP9")};
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

  const StreamParams params = StreamParams::CreateLegacy(kSsrcs1[0]);
  AddRecvStream(params);
  ASSERT_THAT(fake_call_->GetVideoReceiveStreams(), SizeIs(1));

  const VideoReceiveStreamInterface::Config& config =
      fake_call_->GetVideoReceiveStreams()[0]->GetConfig();
  ASSERT_THAT(config.rtp.raw_payload_types, SizeIs(1));
  EXPECT_EQ(config.rtp.raw_payload_types.count(vp8_codec.id), 1U);
}

TEST_F(WebRtcVideoChannelTest,
       SetRecvCodecsWithPacketizationDoesNotRecreateStream) {
  VideoReceiverParameters parameters;
  parameters.codecs = {GetEngineCodec("VP8"), GetEngineCodec("VP9")};
  parameters.codecs.back().packetization = kPacketizationParamRaw;
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

  const StreamParams params = StreamParams::CreateLegacy(kSsrcs1[0]);
  AddRecvStream(params);
  ASSERT_THAT(fake_call_->GetVideoReceiveStreams(), SizeIs(1));
  EXPECT_EQ(fake_call_->GetNumCreatedReceiveStreams(), 1);
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams()[0]
                ->GetConfig()
                .rtp.raw_payload_types.count(parameters.codecs.back().id),
            1U);

  parameters.codecs.back().packetization.reset();
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  EXPECT_EQ(fake_call_->GetNumCreatedReceiveStreams(), 1);
  EXPECT_TRUE(fake_call_->GetVideoReceiveStreams()[0]
                  ->GetConfig()
                  .rtp.raw_payload_types.empty());
}

TEST_F(WebRtcVideoChannelTest, SetReceiveStopAndStartDoesNotRecreateStream) {
  VideoReceiverParameters parameters;
  parameters.codecs = {GetEngineCodec("VP8")};
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

  const StreamParams params = StreamParams::CreateLegacy(kSsrcs1[0]);
  AddRecvStream(params);
  ASSERT_THAT(fake_call_->GetVideoReceiveStreams(), SizeIs(1));
  EXPECT_EQ(fake_call_->GetNumCreatedReceiveStreams(), 1);

  receive_channel_->SetReceive(false);
  EXPECT_EQ(fake_call_->GetNumCreatedReceiveStreams(), 1);

  receive_channel_->SetReceive(true);
  EXPECT_EQ(fake_call_->GetNumCreatedReceiveStreams(), 1);
}

TEST_F(WebRtcVideoChannelTest, DuplicateUlpfecCodecIsDropped) {
  constexpr int kFirstUlpfecPayloadType = 126;
  constexpr int kSecondUlpfecPayloadType = 127;

  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(
      CreateVideoCodec(kFirstUlpfecPayloadType, kUlpfecCodecName));
  parameters.codecs.push_back(
      CreateVideoCodec(kSecondUlpfecPayloadType, kUlpfecCodecName));
  ASSERT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  time_controller_.AdvanceTime(TimeDelta::Zero());

  FakeVideoReceiveStream* recv_stream = AddRecvStream();
  EXPECT_EQ(kFirstUlpfecPayloadType,
            recv_stream->GetConfig().rtp.ulpfec_payload_type);
}

TEST_F(WebRtcVideoChannelTest, DuplicateRedCodecIsDropped) {
  constexpr int kFirstRedPayloadType = 126;
  constexpr int kSecondRedPayloadType = 127;

  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(
      CreateVideoCodec(kFirstRedPayloadType, kRedCodecName));
  parameters.codecs.push_back(
      CreateVideoCodec(kSecondRedPayloadType, kRedCodecName));
  ASSERT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  time_controller_.AdvanceTime(TimeDelta::Zero());

  FakeVideoReceiveStream* recv_stream = AddRecvStream();
  EXPECT_EQ(kFirstRedPayloadType,
            recv_stream->GetConfig().rtp.red_payload_type);
}

TEST_F(WebRtcVideoChannelTest, SetRecvCodecsWithChangedRtxPayloadType) {
  const int kUnusedPayloadType1 = 126;
  const int kUnusedPayloadType2 = 127;
  EXPECT_FALSE(FindCodecById(engine_->LegacyRecvCodecs(), kUnusedPayloadType1));
  EXPECT_FALSE(FindCodecById(engine_->LegacyRecvCodecs(), kUnusedPayloadType2));

  // SSRCs for RTX.
  StreamParams params = StreamParams::CreateLegacy(kSsrcs1[0]);
  params.AddFidSsrc(kSsrcs1[0], kRtxSsrcs1[0]);
  AddRecvStream(params);

  // Original payload type for RTX.
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  Codec rtx_codec = CreateVideoCodec(kUnusedPayloadType1, "rtx");
  rtx_codec.SetParam("apt", GetEngineCodec("VP8").id);
  parameters.codecs.push_back(rtx_codec);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  ASSERT_EQ(1U, fake_call_->GetVideoReceiveStreams().size());
  const VideoReceiveStreamInterface::Config& config_before =
      fake_call_->GetVideoReceiveStreams()[0]->GetConfig();
  EXPECT_EQ(1U, config_before.rtp.rtx_associated_payload_types.size());
  const int* payload_type_before = FindKeyByValue(
      config_before.rtp.rtx_associated_payload_types, GetEngineCodec("VP8").id);
  ASSERT_NE(payload_type_before, nullptr);
  EXPECT_EQ(kUnusedPayloadType1, *payload_type_before);
  EXPECT_EQ(kRtxSsrcs1[0], config_before.rtp.rtx_ssrc);

  // Change payload type for RTX.
  parameters.codecs[1].id = kUnusedPayloadType2;
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  ASSERT_EQ(1U, fake_call_->GetVideoReceiveStreams().size());
  const VideoReceiveStreamInterface::Config& config_after =
      fake_call_->GetVideoReceiveStreams()[0]->GetConfig();
  EXPECT_EQ(1U, config_after.rtp.rtx_associated_payload_types.size());
  const int* payload_type_after = FindKeyByValue(
      config_after.rtp.rtx_associated_payload_types, GetEngineCodec("VP8").id);
  ASSERT_NE(payload_type_after, nullptr);
  EXPECT_EQ(kUnusedPayloadType2, *payload_type_after);
  EXPECT_EQ(kRtxSsrcs1[0], config_after.rtp.rtx_ssrc);
}

TEST_F(WebRtcVideoChannelTest, SetRecvCodecsRtxWithRtxTime) {
  const int kUnusedPayloadType1 = 126;
  const int kUnusedPayloadType2 = 127;
  EXPECT_FALSE(FindCodecById(engine_->LegacyRecvCodecs(), kUnusedPayloadType1));
  EXPECT_FALSE(FindCodecById(engine_->LegacyRecvCodecs(), kUnusedPayloadType2));

  // SSRCs for RTX.
  StreamParams params = StreamParams::CreateLegacy(kSsrcs1[0]);
  params.AddFidSsrc(kSsrcs1[0], kRtxSsrcs1[0]);
  AddRecvStream(params);

  // Payload type for RTX.
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  Codec rtx_codec = CreateVideoCodec(kUnusedPayloadType1, "rtx");
  rtx_codec.SetParam("apt", GetEngineCodec("VP8").id);
  parameters.codecs.push_back(rtx_codec);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  ASSERT_EQ(1U, fake_call_->GetVideoReceiveStreams().size());
  const VideoReceiveStreamInterface::Config& config =
      fake_call_->GetVideoReceiveStreams()[0]->GetConfig();

  const int kRtxTime = 343;
  // Assert that the default value is different from the ones we test
  // and store the default value.
  EXPECT_NE(config.rtp.nack.rtp_history_ms, kRtxTime);
  int default_history_ms = config.rtp.nack.rtp_history_ms;

  // Set rtx-time.
  parameters.codecs[1].SetParam(kCodecParamRtxTime, kRtxTime);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams()[0]
                ->GetConfig()
                .rtp.nack.rtp_history_ms,
            kRtxTime);

  // Negative values are ignored so the default value applies.
  parameters.codecs[1].SetParam(kCodecParamRtxTime, -1);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  EXPECT_NE(fake_call_->GetVideoReceiveStreams()[0]
                ->GetConfig()
                .rtp.nack.rtp_history_ms,
            -1);
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams()[0]
                ->GetConfig()
                .rtp.nack.rtp_history_ms,
            default_history_ms);

  // 0 is ignored so the default applies.
  parameters.codecs[1].SetParam(kCodecParamRtxTime, 0);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  EXPECT_NE(fake_call_->GetVideoReceiveStreams()[0]
                ->GetConfig()
                .rtp.nack.rtp_history_ms,
            0);
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams()[0]
                ->GetConfig()
                .rtp.nack.rtp_history_ms,
            default_history_ms);

  // Values larger than the default are clamped to the default.
  parameters.codecs[1].SetParam(kCodecParamRtxTime, default_history_ms + 100);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams()[0]
                ->GetConfig()
                .rtp.nack.rtp_history_ms,
            default_history_ms);
}

TEST_F(WebRtcVideoChannelTest, SetRecvCodecsDifferentPayloadType) {
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs[0].id = 99;
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
}

TEST_F(WebRtcVideoChannelTest, SetRecvCodecsAcceptDefaultCodecs) {
  VideoReceiverParameters parameters;
  parameters.codecs = engine_->LegacyRecvCodecs();
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

  FakeVideoReceiveStream* stream = AddRecvStream();
  const VideoReceiveStreamInterface::Config& config = stream->GetConfig();
  EXPECT_EQ(engine_->LegacyRecvCodecs()[0].name,
            config.decoders[0].video_format.name);
  EXPECT_EQ(engine_->LegacyRecvCodecs()[0].id, config.decoders[0].payload_type);
}

TEST_F(WebRtcVideoChannelTest, SetRecvCodecsRejectUnsupportedCodec) {
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(CreateVideoCodec(101, "WTF3"));
  EXPECT_FALSE(receive_channel_->SetReceiverParameters(parameters));
}

TEST_F(WebRtcVideoChannelTest, SetRecvCodecsRejectUnsupportedCodecParameters) {
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  // The decoder factory only supports VP9 profile 0.
  Codec vp9 = GetEngineCodec("VP9");
  vp9.params["profile-id"] = "2";
  parameters.codecs.push_back(vp9);
  EXPECT_FALSE(receive_channel_->SetReceiverParameters(parameters));
}

TEST_F(WebRtcVideoChannelTest, SetRecvCodecsAcceptsMultipleVideoCodecs) {
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
}

TEST_F(WebRtcVideoChannelTest, SetRecvCodecsWithoutFecDisablesFec) {
  VideoSenderParameters send_parameters;
  send_parameters.codecs.push_back(GetEngineCodec("VP8"));
  send_parameters.codecs.push_back(GetEngineCodec("red"));
  send_parameters.codecs.push_back(GetEngineCodec("ulpfec"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters));

  FakeVideoReceiveStream* stream = AddRecvStream();

  EXPECT_EQ(GetEngineCodec("ulpfec").id,
            stream->GetConfig().rtp.ulpfec_payload_type);

  VideoReceiverParameters recv_parameters;
  recv_parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters));
  stream = fake_call_->GetVideoReceiveStreams()[0];
  ASSERT_TRUE(stream != nullptr);
  EXPECT_EQ(-1, stream->GetConfig().rtp.ulpfec_payload_type)
      << "SetSendCodec without ULPFEC should disable current ULPFEC.";
}

TEST_F(WebRtcVideoChannelFlexfecRecvTest, SetRecvParamsWithoutFecDisablesFec) {
  AddRecvStream(
      CreatePrimaryWithFecFrStreamParams("cname", kSsrcs1[0], kFlexfecSsrc));
  const std::vector<FakeFlexfecReceiveStream*>& streams =
      fake_call_->GetFlexfecReceiveStreams();

  ASSERT_EQ(1U, streams.size());
  const FakeFlexfecReceiveStream* stream = streams.front();
  EXPECT_EQ(GetEngineCodec("flexfec-03").id, stream->GetConfig().payload_type);
  EXPECT_EQ(kFlexfecSsrc, stream->remote_ssrc());
  ASSERT_EQ(1U, stream->GetConfig().protected_media_ssrcs.size());
  EXPECT_EQ(kSsrcs1[0], stream->GetConfig().protected_media_ssrcs[0]);

  VideoReceiverParameters recv_parameters;
  recv_parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters));
  EXPECT_TRUE(streams.empty())
      << "SetSendCodec without FlexFEC should disable current FlexFEC.";
}

TEST_F(WebRtcVideoChannelTest, SetSendParamsWithFecEnablesFec) {
  FakeVideoReceiveStream* stream = AddRecvStream();
  EXPECT_EQ(GetEngineCodec("ulpfec").id,
            stream->GetConfig().rtp.ulpfec_payload_type);

  VideoReceiverParameters recv_parameters;
  recv_parameters.codecs.push_back(GetEngineCodec("VP8"));
  recv_parameters.codecs.push_back(GetEngineCodec("red"));
  recv_parameters.codecs.push_back(GetEngineCodec("ulpfec"));
  ASSERT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters));
  stream = fake_call_->GetVideoReceiveStreams()[0];
  ASSERT_TRUE(stream != nullptr);
  EXPECT_EQ(GetEngineCodec("ulpfec").id,
            stream->GetConfig().rtp.ulpfec_payload_type)
      << "ULPFEC should be enabled on the receive stream.";

  VideoSenderParameters send_parameters;
  send_parameters.codecs.push_back(GetEngineCodec("VP8"));
  send_parameters.codecs.push_back(GetEngineCodec("red"));
  send_parameters.codecs.push_back(GetEngineCodec("ulpfec"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters));
  stream = fake_call_->GetVideoReceiveStreams()[0];
  EXPECT_EQ(GetEngineCodec("ulpfec").id,
            stream->GetConfig().rtp.ulpfec_payload_type)
      << "ULPFEC should be enabled on the receive stream.";
}

TEST_F(WebRtcVideoChannelFlexfecSendRecvTest,
       SetSendRecvParamsWithFecEnablesFec) {
  AddRecvStream(
      CreatePrimaryWithFecFrStreamParams("cname", kSsrcs1[0], kFlexfecSsrc));
  const std::vector<FakeFlexfecReceiveStream*>& streams =
      fake_call_->GetFlexfecReceiveStreams();

  VideoReceiverParameters recv_parameters;
  recv_parameters.codecs.push_back(GetEngineCodec("VP8"));
  recv_parameters.codecs.push_back(GetEngineCodec("flexfec-03"));
  ASSERT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters));
  ASSERT_EQ(1U, streams.size());
  const FakeFlexfecReceiveStream* stream_with_recv_params = streams.front();
  EXPECT_EQ(GetEngineCodec("flexfec-03").id,
            stream_with_recv_params->GetConfig().payload_type);
  EXPECT_EQ(kFlexfecSsrc, stream_with_recv_params->GetConfig().remote_ssrc);
  EXPECT_EQ(1U,
            stream_with_recv_params->GetConfig().protected_media_ssrcs.size());
  EXPECT_EQ(kSsrcs1[0],
            stream_with_recv_params->GetConfig().protected_media_ssrcs[0]);

  VideoSenderParameters send_parameters;
  send_parameters.codecs.push_back(GetEngineCodec("VP8"));
  send_parameters.codecs.push_back(GetEngineCodec("flexfec-03"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters));
  ASSERT_EQ(1U, streams.size());
  const FakeFlexfecReceiveStream* stream_with_send_params = streams.front();
  EXPECT_EQ(GetEngineCodec("flexfec-03").id,
            stream_with_send_params->GetConfig().payload_type);
  EXPECT_EQ(kFlexfecSsrc, stream_with_send_params->GetConfig().remote_ssrc);
  EXPECT_EQ(1U,
            stream_with_send_params->GetConfig().protected_media_ssrcs.size());
  EXPECT_EQ(kSsrcs1[0],
            stream_with_send_params->GetConfig().protected_media_ssrcs[0]);
}

TEST_F(WebRtcVideoChannelTest, SetRecvCodecsRejectDuplicateFecPayloads) {
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("red"));
  parameters.codecs[1].id = parameters.codecs[0].id;
  EXPECT_FALSE(receive_channel_->SetReceiverParameters(parameters));
}

TEST_F(WebRtcVideoChannelFlexfecRecvTest,
       SetRecvCodecsRejectDuplicateFecPayloads) {
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("flexfec-03"));
  parameters.codecs[1].id = parameters.codecs[0].id;
  EXPECT_FALSE(receive_channel_->SetReceiverParameters(parameters));
}

TEST_F(WebRtcVideoChannelTest, SetRecvCodecsRejectDuplicateCodecPayloads) {
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  parameters.codecs[1].id = parameters.codecs[0].id;
  EXPECT_FALSE(receive_channel_->SetReceiverParameters(parameters));
}

TEST_F(WebRtcVideoChannelTest,
       SetRecvCodecsAcceptSameCodecOnMultiplePayloadTypes) {
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs[1].id = PayloadType(parameters.codecs[1].id + 1);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
}

// Test that setting the same codecs but with a different order
// doesn't result in the stream being recreated.
TEST_F(WebRtcVideoChannelTest,
       SetRecvCodecsDifferentOrderDoesntRecreateStream) {
  VideoReceiverParameters parameters1;
  parameters1.codecs.push_back(GetEngineCodec("VP8"));
  parameters1.codecs.push_back(GetEngineCodec("red"));
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters1));

  AddRecvStream(StreamParams::CreateLegacy(123));
  EXPECT_EQ(1, fake_call_->GetNumCreatedReceiveStreams());

  VideoReceiverParameters parameters2;
  parameters2.codecs.push_back(GetEngineCodec("red"));
  parameters2.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters2));
  EXPECT_EQ(1, fake_call_->GetNumCreatedReceiveStreams());
}

TEST_F(WebRtcVideoChannelTest, SendStreamNotSendingByDefault) {
  EXPECT_FALSE(AddSendStream()->IsSending());
}

TEST_F(WebRtcVideoChannelTest, ReceiveStreamReceivingByDefault) {
  EXPECT_TRUE(AddRecvStream()->IsReceiving());
}

TEST_F(WebRtcVideoChannelTest,
       SetSendParametersRecreatesStreamAndClearsAdaptationStats) {
  FakeVideoSendStream* stream = AddSendStream();
  webrtc::VideoSendStream::Stats stats;
  // Set cumulative stats that should be retained.
  stats.number_of_cpu_adapt_changes = 2;
  stats.number_of_quality_adapt_changes = 3;
  stats.quality_limitation_durations_ms[webrtc::QualityLimitationReason::kCpu] =
      100;

  // Set ephemeral flags that should be cleared by RecreateWebRtcStream.
  stats.bw_limited_resolution = true;
  stats.bw_limited_framerate = true;
  stats.cpu_limited_resolution = true;
  stats.cpu_limited_framerate = true;
  stats.quality_limitation_reason = webrtc::QualityLimitationReason::kBandwidth;

  stream->SetStats(stats);

  // Trigger stream recreation by changing construction-time parameters.
  EXPECT_EQ(1, fake_call_->GetNumCreatedSendStreams());
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.extmap_allow_mixed = true;  // This forces recreation.
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  EXPECT_EQ(2, fake_call_->GetNumCreatedSendStreams());

  ASSERT_EQ(1U, fake_call_->GetVideoSendStreams().size());
  FakeVideoSendStream* new_stream = fake_call_->GetVideoSendStreams()[0];

  webrtc::VideoSendStream::Stats new_stats =
      static_cast<webrtc::VideoSendStream*>(new_stream)->GetStats();

  // Check that ephemeral flags were cleared.
  EXPECT_FALSE(new_stats.bw_limited_resolution);
  EXPECT_FALSE(new_stats.bw_limited_framerate);
  EXPECT_FALSE(new_stats.cpu_limited_resolution);
  EXPECT_FALSE(new_stats.cpu_limited_framerate);
  EXPECT_EQ(webrtc::QualityLimitationReason::kNone,
            new_stats.quality_limitation_reason);

  // Check that cumulative stats were retained.
  EXPECT_EQ(2, new_stats.number_of_cpu_adapt_changes);
  EXPECT_EQ(3, new_stats.number_of_quality_adapt_changes);
  EXPECT_EQ(100, new_stats.quality_limitation_durations_ms
                     [webrtc::QualityLimitationReason::kCpu]);
}

TEST_F(WebRtcVideoChannelTest, RecreatedSendStreamIsCreatedWithCsrcs) {
  AddSendStream();
  RtpParameters rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  ASSERT_EQ(1u, rtp_parameters.encodings.size());
  rtp_parameters.encodings[0].csrcs = std::vector<uint32_t>{0x1111, 0x2222};
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());

  // Trigger stream recreation by changing construction-time parameters.
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.extmap_allow_mixed = true;  // This forces recreation.
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  EXPECT_EQ(2, fake_call_->GetNumCreatedSendStreams());

  ASSERT_EQ(1U, fake_call_->GetVideoSendStreams().size());
  EXPECT_THAT(fake_call_->GetVideoSendStreams()[0]->GetConfig().rtp.csrcs,
              ElementsAre(0x1111, 0x2222));
}

TEST_F(WebRtcVideoChannelTest, SetSend) {
  FakeVideoSendStream* stream = AddSendStream();
  EXPECT_FALSE(stream->IsSending());

  // false->true
  EXPECT_TRUE(send_channel_->SetSend(true));
  EXPECT_TRUE(stream->IsSending());
  // true->true
  EXPECT_TRUE(send_channel_->SetSend(true));
  EXPECT_TRUE(stream->IsSending());
  // true->false
  EXPECT_TRUE(send_channel_->SetSend(false));
  EXPECT_FALSE(stream->IsSending());
  // false->false
  EXPECT_TRUE(send_channel_->SetSend(false));
  EXPECT_FALSE(stream->IsSending());

  EXPECT_TRUE(send_channel_->SetSend(true));
  FakeVideoSendStream* new_stream = AddSendStream();
  EXPECT_TRUE(new_stream->IsSending())
      << "Send stream created after SetSend(true) not sending initially.";
}

// This test verifies DSCP settings are properly applied on video media channel.
TEST_F(WebRtcVideoChannelTest, TestSetDscpOptions) {
  auto network_interface = std::make_unique<FakeNetworkInterface>(env_);
  MediaConfig config;
  std::unique_ptr<VideoMediaSendChannelInterface> send_channel;
  RtpParameters parameters;

  send_channel = engine_->CreateSendChannel(
      env_, call_.get(), config, VideoOptions(), CryptoOptions(),
      video_bitrate_allocator_factory_.get(), nullptr, nullptr);

  send_channel->SetInterface(network_interface.get());
  // Default value when DSCP is disabled should be DSCP_DEFAULT.
  EXPECT_EQ(DSCP_DEFAULT, network_interface->dscp());
  send_channel->SetInterface(nullptr);

  // Default value when DSCP is enabled is also DSCP_DEFAULT, until it is set
  // through rtp parameters.
  config.enable_dscp = true;
  send_channel = engine_->CreateSendChannel(
      env_, call_.get(), config, VideoOptions(), CryptoOptions(),
      video_bitrate_allocator_factory_.get(), nullptr, nullptr);
  send_channel->SetInterface(network_interface.get());
  EXPECT_EQ(DSCP_DEFAULT, network_interface->dscp());

  // Create a send stream to configure
  EXPECT_TRUE(send_channel->AddSendStream(StreamParams::CreateLegacy(kSsrc)));
  parameters = send_channel->GetRtpSendParameters(kSsrc);
  ASSERT_FALSE(parameters.encodings.empty());

  // Various priorities map to various dscp values.
  parameters.encodings[0].network_priority = Priority::kHigh;
  ASSERT_TRUE(
      send_channel->SetRtpSendParameters(kSsrc, parameters, nullptr).ok());
  EXPECT_EQ(DSCP_AF41, network_interface->dscp());
  parameters.encodings[0].network_priority = Priority::kVeryLow;
  ASSERT_TRUE(
      send_channel->SetRtpSendParameters(kSsrc, parameters, nullptr).ok());
  EXPECT_EQ(DSCP_CS1, network_interface->dscp());

  // Packets should also self-identify their dscp in PacketOptions.
  const uint8_t kData[10] = {0};
  EXPECT_TRUE(ChannelImplAsTransport(send_channel.get())
                  ->SendRtcp(kData, /*packet_options=*/{}));
  EXPECT_EQ(DSCP_CS1, network_interface->options().dscp);
  send_channel->SetInterface(nullptr);

  // Verify that setting the option to false resets the
  // DiffServCodePoint.
  config.enable_dscp = false;
  send_channel = engine_->CreateSendChannel(
      env_, call_.get(), config, VideoOptions(), CryptoOptions(),
      video_bitrate_allocator_factory_.get(), nullptr, nullptr);
  send_channel->SetInterface(network_interface.get());
  EXPECT_EQ(DSCP_DEFAULT, network_interface->dscp());
  send_channel->SetInterface(nullptr);
}

// This test verifies that the RTCP reduced size mode is properly applied to
// send video streams.
TEST_F(WebRtcVideoChannelTest, TestSetSendRtcpReducedSize) {
  // Create stream, expecting that default mode is "compound".
  FakeVideoSendStream* stream1 = AddSendStream();
  EXPECT_EQ(RtcpMode::kCompound, stream1->GetConfig().rtp.rtcp_mode);
  RtpParameters rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_FALSE(rtp_parameters.rtcp.reduced_size);

  // Now enable reduced size mode.
  send_parameters_.rtcp.reduced_size = true;
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
  stream1 = fake_call_->GetVideoSendStreams()[0];
  EXPECT_EQ(RtcpMode::kReducedSize, stream1->GetConfig().rtp.rtcp_mode);
  rtp_parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_TRUE(rtp_parameters.rtcp.reduced_size);

  // Create a new stream and ensure it picks up the reduced size mode.
  FakeVideoSendStream* stream2 = AddSendStream();
  EXPECT_EQ(RtcpMode::kReducedSize, stream2->GetConfig().rtp.rtcp_mode);
}

TEST_F(WebRtcVideoChannelTest, OnReadyToSendSignalsNetworkState) {
  EXPECT_EQ(kNetworkUp, fake_call_->GetNetworkState(MediaType::VIDEO));
  EXPECT_EQ(kNetworkUp, fake_call_->GetNetworkState(MediaType::AUDIO));

  send_channel_->OnReadyToSend(false);
  EXPECT_EQ(kNetworkDown, fake_call_->GetNetworkState(MediaType::VIDEO));
  EXPECT_EQ(kNetworkUp, fake_call_->GetNetworkState(MediaType::AUDIO));

  send_channel_->OnReadyToSend(true);
  EXPECT_EQ(kNetworkUp, fake_call_->GetNetworkState(MediaType::VIDEO));
  EXPECT_EQ(kNetworkUp, fake_call_->GetNetworkState(MediaType::AUDIO));
}

TEST_F(WebRtcVideoChannelTest, GetStatsReportsSentCodecName) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  AddSendStream();

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_EQ("VP8", send_info.senders[0].codec_name);
}

TEST_F(WebRtcVideoChannelTest, GetStatsReportsEncoderImplementationName) {
  FakeVideoSendStream* stream = AddSendStream();
  VideoSendStream::Stats stats;
  stats.encoder_implementation_name = "encoder_implementation_name";
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_EQ(stats.encoder_implementation_name,
            send_info.senders[0].encoder_implementation_name);
}

TEST_F(WebRtcVideoChannelTest, GetStatsReportsPowerEfficientEncoder) {
  FakeVideoSendStream* stream = AddSendStream();
  VideoSendStream::Stats stats;
  stats.power_efficient_encoder = true;
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_TRUE(send_info.senders[0].power_efficient_encoder);
}

TEST_F(WebRtcVideoChannelTest, GetStatsReportsCpuOveruseMetrics) {
  FakeVideoSendStream* stream = AddSendStream();
  VideoSendStream::Stats stats;
  stats.avg_encode_time_ms = 13;
  stats.encode_usage_percent = 42;
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_EQ(stats.avg_encode_time_ms, send_info.senders[0].avg_encode_ms);
  EXPECT_EQ(stats.encode_usage_percent,
            send_info.senders[0].encode_usage_percent);
}

TEST_F(WebRtcVideoChannelTest, GetStatsReportsFramesEncoded) {
  FakeVideoSendStream* stream = AddSendStream();
  VideoSendStream::Stats stats;
  stats.substreams[123].frames_encoded = 13;
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_EQ(13u, send_info.senders[0].frames_encoded);
}

TEST_F(WebRtcVideoChannelTest, GetStatsReportsKeyFramesEncoded) {
  FakeVideoSendStream* stream = AddSendStream();
  VideoSendStream::Stats stats;
  stats.substreams[123].frame_counts.key_frames = 10;
  stats.substreams[456].frame_counts.key_frames = 87;
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_EQ(send_info.senders.size(), 2u);
  EXPECT_EQ(10u, send_info.senders[0].key_frames_encoded);
  EXPECT_EQ(87u, send_info.senders[1].key_frames_encoded);
  EXPECT_EQ(97u, send_info.aggregated_senders[0].key_frames_encoded);
}

TEST_F(WebRtcVideoChannelTest, GetStatsReportsPerLayerQpSum) {
  FakeVideoSendStream* stream = AddSendStream();
  VideoSendStream::Stats stats;
  stats.substreams[123].qp_sum = 15;
  stats.substreams[456].qp_sum = 11;
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_EQ(send_info.senders.size(), 2u);
  EXPECT_EQ(stats.substreams[123].qp_sum, send_info.senders[0].qp_sum);
  EXPECT_EQ(stats.substreams[456].qp_sum, send_info.senders[1].qp_sum);
  EXPECT_EQ(*send_info.aggregated_senders[0].qp_sum, 26u);
}

VideoSendStream::Stats GetInitialisedStats() {
  VideoSendStream::Stats stats;
  stats.encoder_implementation_name = "vp";
  stats.input_frame_rate = 1.0;
  stats.encode_frame_rate = 2;
  stats.avg_encode_time_ms = 3;
  stats.encode_usage_percent = 4;
  stats.frames_encoded = 5;
  stats.total_encode_time_ms = 6;
  stats.frames_dropped_by_capturer = 7;
  stats.frames_dropped_by_encoder_queue = 8;
  stats.frames_dropped_by_rate_limiter = 9;
  stats.frames_dropped_by_congestion_window = 10;
  stats.frames_dropped_by_encoder = 11;
  stats.target_media_bitrate_bps = 13;
  stats.media_bitrate_bps = 14;
  stats.suspended = true;
  stats.bw_limited_resolution = true;
  stats.cpu_limited_resolution = true;
  // Not wired.
  stats.bw_limited_framerate = true;
  // Not wired.
  stats.cpu_limited_framerate = true;
  stats.quality_limitation_reason = QualityLimitationReason::kCpu;
  stats.quality_limitation_durations_ms[QualityLimitationReason::kCpu] = 15;
  stats.quality_limitation_resolution_changes = 16;
  stats.number_of_cpu_adapt_changes = 17;
  stats.number_of_quality_adapt_changes = 18;
  stats.has_entered_low_resolution = true;
  stats.content_type = VideoContentType::SCREENSHARE;
  stats.frames_sent = 19;
  stats.huge_frames_sent = 20;

  return stats;
}

TEST_F(WebRtcVideoChannelTest, GetAggregatedStatsReportWithoutSubStreams) {
  FakeVideoSendStream* stream = AddSendStream();
  auto stats = GetInitialisedStats();
  stream->SetStats(stats);
  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_EQ(send_info.aggregated_senders.size(), 1u);
  auto& sender = send_info.aggregated_senders[0];

  // MediaSenderInfo

  EXPECT_EQ(sender.payload_bytes_sent, 0);
  EXPECT_EQ(sender.header_and_padding_bytes_sent, 0);
  EXPECT_EQ(sender.retransmitted_bytes_sent, 0u);
  EXPECT_EQ(sender.packets_sent, 0);
  EXPECT_EQ(sender.retransmitted_packets_sent, 0u);
  EXPECT_EQ(sender.packets_lost, 0);
  EXPECT_EQ(sender.fraction_lost, 0.0f);
  EXPECT_EQ(sender.rtt_ms, 0);
  EXPECT_EQ(sender.codec_name, DefaultCodec().name);
  EXPECT_EQ(sender.codec_payload_type, DefaultCodec().id);
  EXPECT_EQ(sender.local_stats.size(), 1u);
  EXPECT_EQ(sender.local_stats[0].ssrc, last_ssrc_);
  EXPECT_EQ(sender.local_stats[0].timestamp, 0.0f);
  EXPECT_EQ(sender.remote_stats.size(), 0u);
  EXPECT_EQ(sender.report_block_datas.size(), 0u);

  // VideoSenderInfo

  EXPECT_EQ(sender.ssrc_groups.size(), 0u);
  EXPECT_EQ(sender.encoder_implementation_name,
            stats.encoder_implementation_name);
  // Comes from substream only.
  EXPECT_EQ(sender.firs_received, 0);
  EXPECT_EQ(sender.plis_received, 0);
  EXPECT_EQ(sender.nacks_received, 0u);
  EXPECT_EQ(sender.send_frame_width, 0);
  EXPECT_EQ(sender.send_frame_height, 0);
  EXPECT_EQ(sender.framerate_sent, 0);
  EXPECT_EQ(sender.frames_encoded, 0u);

  EXPECT_EQ(sender.framerate_input, stats.input_frame_rate);
  EXPECT_EQ(sender.nominal_bitrate, stats.media_bitrate_bps);
  EXPECT_NE(sender.adapt_reason & WebRtcVideoChannel::ADAPTREASON_CPU, 0);
  EXPECT_NE(sender.adapt_reason & WebRtcVideoChannel::ADAPTREASON_BANDWIDTH, 0);
  EXPECT_EQ(sender.adapt_changes, stats.number_of_cpu_adapt_changes);
  EXPECT_EQ(sender.quality_limitation_reason, stats.quality_limitation_reason);
  EXPECT_EQ(sender.quality_limitation_durations_ms,
            stats.quality_limitation_durations_ms);
  EXPECT_EQ(sender.quality_limitation_resolution_changes,
            stats.quality_limitation_resolution_changes);
  EXPECT_EQ(sender.avg_encode_ms, stats.avg_encode_time_ms);
  EXPECT_EQ(sender.encode_usage_percent, stats.encode_usage_percent);
  // Comes from substream only.
  EXPECT_EQ(sender.key_frames_encoded, 0u);
  EXPECT_EQ(sender.total_encode_time_ms, 0u);
  EXPECT_EQ(sender.total_packet_send_delay, TimeDelta::Zero());
  EXPECT_EQ(sender.qp_sum, std::nullopt);
  EXPECT_EQ(sender.frames_sent, 0u);
  EXPECT_EQ(sender.huge_frames_sent, 0u);

  EXPECT_EQ(sender.has_entered_low_resolution,
            stats.has_entered_low_resolution);
  EXPECT_EQ(sender.content_type, VideoContentType::SCREENSHARE);
  EXPECT_EQ(sender.rid, std::nullopt);
}

TEST_F(WebRtcVideoChannelTest, GetAggregatedStatsReportForSubStreams) {
  FakeVideoSendStream* stream = AddSendStream();
  auto stats = GetInitialisedStats();

  const uint32_t ssrc_1 = 123u;
  const uint32_t ssrc_2 = 456u;

  auto& substream = stats.substreams[ssrc_1];
  substream.frame_counts.key_frames = 1;
  substream.frame_counts.delta_frames = 2;
  substream.width = 3;
  substream.height = 4;
  substream.total_bitrate_bps = 5;
  substream.retransmit_bitrate_bps = 6;
  substream.avg_delay_ms = 7;
  substream.max_delay_ms = 8;
  substream.rtp_stats.transmitted.total_packet_delay = TimeDelta::Millis(9);
  substream.rtp_stats.transmitted.header_bytes = 10;
  substream.rtp_stats.transmitted.padding_bytes = 11;
  substream.rtp_stats.retransmitted.payload_bytes = 12;
  substream.rtp_stats.retransmitted.packets = 13;
  substream.rtcp_packet_type_counts.fir_packets = 14;
  substream.rtcp_packet_type_counts.nack_packets = 15;
  substream.rtcp_packet_type_counts.pli_packets = 16;
  rtcp::ReportBlock report_block;
  report_block.SetCumulativeLost(17);
  report_block.SetFractionLost(18);
  ReportBlockData report_block_data;
  report_block_data.SetReportBlock(0, report_block, Timestamp::Zero(),
                                   Timestamp::Zero());
  report_block_data.AddRoundTripTimeSample(TimeDelta::Millis(19));
  substream.report_block_data = report_block_data;
  substream.encode_frame_rate = 20.0;
  substream.frames_encoded = 21;
  substream.qp_sum = 22;
  substream.total_encode_time_ms = 23;
  substream.huge_frames_sent = 25;

  stats.substreams[ssrc_2] = substream;

  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_EQ(send_info.aggregated_senders.size(), 1u);
  auto& sender = send_info.aggregated_senders[0];

  // MediaSenderInfo

  EXPECT_EQ(
      sender.payload_bytes_sent,
      static_cast<int64_t>(2u * substream.rtp_stats.transmitted.payload_bytes));
  EXPECT_EQ(sender.header_and_padding_bytes_sent,
            static_cast<int64_t>(
                2u * (substream.rtp_stats.transmitted.header_bytes +
                      substream.rtp_stats.transmitted.padding_bytes)));
  EXPECT_EQ(sender.retransmitted_bytes_sent,
            2u * substream.rtp_stats.retransmitted.payload_bytes);
  EXPECT_EQ(sender.packets_sent,
            static_cast<int>(2 * substream.rtp_stats.transmitted.packets));
  EXPECT_EQ(sender.retransmitted_packets_sent,
            2u * substream.rtp_stats.retransmitted.packets);
  EXPECT_EQ(sender.total_packet_send_delay,
            2 * substream.rtp_stats.transmitted.total_packet_delay);
  EXPECT_EQ(sender.packets_lost,
            2 * substream.report_block_data->cumulative_lost());
  EXPECT_FLOAT_EQ(sender.fraction_lost,
                  substream.report_block_data->fraction_lost());
  EXPECT_EQ(sender.rtt_ms, 0);
  EXPECT_EQ(sender.codec_name, DefaultCodec().name);
  EXPECT_EQ(sender.codec_payload_type, DefaultCodec().id);
  EXPECT_EQ(sender.local_stats.size(), 1u);
  EXPECT_EQ(sender.local_stats[0].ssrc, last_ssrc_);
  EXPECT_EQ(sender.local_stats[0].timestamp, 0.0f);
  EXPECT_EQ(sender.remote_stats.size(), 0u);
  EXPECT_EQ(sender.report_block_datas.size(), 2u * 1);

  // VideoSenderInfo

  EXPECT_EQ(sender.ssrc_groups.size(), 0u);
  EXPECT_EQ(sender.encoder_implementation_name,
            stats.encoder_implementation_name);
  EXPECT_EQ(
      sender.firs_received,
      static_cast<int>(2 * substream.rtcp_packet_type_counts.fir_packets));
  EXPECT_EQ(
      sender.plis_received,
      static_cast<int>(2 * substream.rtcp_packet_type_counts.pli_packets));
  EXPECT_EQ(sender.nacks_received,
            2 * substream.rtcp_packet_type_counts.nack_packets);
  EXPECT_EQ(sender.send_frame_width, substream.width);
  EXPECT_EQ(sender.send_frame_height, substream.height);

  EXPECT_EQ(sender.framerate_input, stats.input_frame_rate);
  EXPECT_EQ(sender.framerate_sent, stats.encode_frame_rate);
  EXPECT_EQ(sender.nominal_bitrate, stats.media_bitrate_bps);
  EXPECT_NE(sender.adapt_reason & WebRtcVideoChannel::ADAPTREASON_CPU, 0);
  EXPECT_NE(sender.adapt_reason & WebRtcVideoChannel::ADAPTREASON_BANDWIDTH, 0);
  EXPECT_EQ(sender.adapt_changes, stats.number_of_cpu_adapt_changes);
  EXPECT_EQ(sender.quality_limitation_reason, stats.quality_limitation_reason);
  EXPECT_EQ(sender.quality_limitation_durations_ms,
            stats.quality_limitation_durations_ms);
  EXPECT_EQ(sender.quality_limitation_resolution_changes,
            stats.quality_limitation_resolution_changes);
  EXPECT_EQ(sender.avg_encode_ms, stats.avg_encode_time_ms);
  EXPECT_EQ(sender.encode_usage_percent, stats.encode_usage_percent);
  EXPECT_EQ(sender.frames_encoded, 2u * substream.frames_encoded);
  EXPECT_EQ(sender.key_frames_encoded, 2u * substream.frame_counts.key_frames);
  EXPECT_EQ(sender.total_encode_time_ms, 2u * substream.total_encode_time_ms);
  EXPECT_EQ(sender.has_entered_low_resolution,
            stats.has_entered_low_resolution);
  EXPECT_EQ(sender.qp_sum, 2u * *substream.qp_sum);
  EXPECT_EQ(sender.content_type, VideoContentType::SCREENSHARE);
  EXPECT_EQ(sender.frames_sent, 2u * substream.frames_encoded);
  EXPECT_EQ(sender.huge_frames_sent, stats.huge_frames_sent);
  EXPECT_EQ(sender.rid, std::nullopt);
}

TEST_F(WebRtcVideoChannelTest, GetPerLayerStatsReportForSubStreams) {
  FakeVideoSendStream* stream = AddSendStream();
  auto stats = GetInitialisedStats();

  const uint32_t ssrc_1 = 123u;
  const uint32_t ssrc_2 = 456u;

  auto& substream = stats.substreams[ssrc_1];
  substream.frame_counts.key_frames = 1;
  substream.frame_counts.delta_frames = 2;
  substream.width = 3;
  substream.height = 4;
  substream.total_bitrate_bps = 5;
  substream.retransmit_bitrate_bps = 6;
  substream.avg_delay_ms = 7;
  substream.max_delay_ms = 8;
  substream.rtp_stats.transmitted.total_packet_delay = TimeDelta::Millis(9);
  substream.rtp_stats.transmitted.header_bytes = 10;
  substream.rtp_stats.transmitted.padding_bytes = 11;
  substream.rtp_stats.retransmitted.payload_bytes = 12;
  substream.rtp_stats.retransmitted.packets = 13;
  substream.rtcp_packet_type_counts.fir_packets = 14;
  substream.rtcp_packet_type_counts.nack_packets = 15;
  substream.rtcp_packet_type_counts.pli_packets = 16;
  rtcp::ReportBlock report_block;
  report_block.SetCumulativeLost(17);
  report_block.SetFractionLost(18);
  ReportBlockData report_block_data;
  report_block_data.SetReportBlock(0, report_block, Timestamp::Zero(),
                                   Timestamp::Zero());
  report_block_data.AddRoundTripTimeSample(TimeDelta::Millis(19));
  substream.report_block_data = report_block_data;
  substream.encode_frame_rate = 20.0;
  substream.frames_encoded = 21;
  substream.qp_sum = 22;
  substream.total_encode_time_ms = 23;
  substream.huge_frames_sent = 25;

  stats.substreams[ssrc_2] = substream;

  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_EQ(send_info.senders.size(), 2u);
  auto& sender = send_info.senders[0];

  // MediaSenderInfo

  EXPECT_EQ(
      sender.payload_bytes_sent,
      static_cast<int64_t>(substream.rtp_stats.transmitted.payload_bytes));
  EXPECT_EQ(
      sender.header_and_padding_bytes_sent,
      static_cast<int64_t>(substream.rtp_stats.transmitted.header_bytes +
                           substream.rtp_stats.transmitted.padding_bytes));
  EXPECT_EQ(sender.retransmitted_bytes_sent,
            substream.rtp_stats.retransmitted.payload_bytes);
  EXPECT_EQ(sender.packets_sent,
            static_cast<int>(substream.rtp_stats.transmitted.packets));
  EXPECT_EQ(sender.total_packet_send_delay,
            substream.rtp_stats.transmitted.total_packet_delay);
  EXPECT_EQ(sender.retransmitted_packets_sent,
            substream.rtp_stats.retransmitted.packets);
  EXPECT_EQ(sender.packets_lost,
            substream.report_block_data->cumulative_lost());
  EXPECT_FLOAT_EQ(sender.fraction_lost,
                  substream.report_block_data->fraction_lost());
  EXPECT_EQ(sender.rtt_ms, 0);
  EXPECT_EQ(sender.codec_name, DefaultCodec().name);
  EXPECT_EQ(sender.codec_payload_type, DefaultCodec().id);
  EXPECT_EQ(sender.local_stats.size(), 1u);
  EXPECT_EQ(sender.local_stats[0].ssrc, ssrc_1);
  EXPECT_EQ(sender.local_stats[0].timestamp, 0.0f);
  EXPECT_EQ(sender.remote_stats.size(), 0u);
  EXPECT_EQ(sender.report_block_datas.size(), 1u);

  // VideoSenderInfo

  EXPECT_EQ(sender.ssrc_groups.size(), 0u);
  EXPECT_EQ(sender.encoder_implementation_name,
            stats.encoder_implementation_name);
  EXPECT_EQ(sender.firs_received,
            static_cast<int>(substream.rtcp_packet_type_counts.fir_packets));
  EXPECT_EQ(sender.plis_received,
            static_cast<int>(substream.rtcp_packet_type_counts.pli_packets));
  EXPECT_EQ(sender.nacks_received,
            substream.rtcp_packet_type_counts.nack_packets);
  EXPECT_EQ(sender.send_frame_width, substream.width);
  EXPECT_EQ(sender.send_frame_height, substream.height);

  EXPECT_EQ(sender.framerate_input, stats.input_frame_rate);
  EXPECT_EQ(sender.framerate_sent, substream.encode_frame_rate);
  EXPECT_EQ(sender.nominal_bitrate, stats.media_bitrate_bps);
  EXPECT_NE(sender.adapt_reason & WebRtcVideoChannel::ADAPTREASON_CPU, 0);
  EXPECT_NE(sender.adapt_reason & WebRtcVideoChannel::ADAPTREASON_BANDWIDTH, 0);
  EXPECT_EQ(sender.adapt_changes, stats.number_of_cpu_adapt_changes);
  EXPECT_EQ(sender.quality_limitation_reason, stats.quality_limitation_reason);
  EXPECT_EQ(sender.quality_limitation_durations_ms,
            stats.quality_limitation_durations_ms);
  EXPECT_EQ(sender.quality_limitation_resolution_changes,
            stats.quality_limitation_resolution_changes);
  EXPECT_EQ(sender.avg_encode_ms, stats.avg_encode_time_ms);
  EXPECT_EQ(sender.encode_usage_percent, stats.encode_usage_percent);
  EXPECT_EQ(sender.frames_encoded,
            static_cast<uint32_t>(substream.frames_encoded));
  EXPECT_EQ(sender.key_frames_encoded,
            static_cast<uint32_t>(substream.frame_counts.key_frames));
  EXPECT_EQ(sender.total_encode_time_ms, substream.total_encode_time_ms);
  EXPECT_EQ(sender.has_entered_low_resolution,
            stats.has_entered_low_resolution);
  EXPECT_EQ(sender.qp_sum, *substream.qp_sum);
  EXPECT_EQ(sender.content_type, VideoContentType::SCREENSHARE);
  EXPECT_EQ(sender.frames_sent,
            static_cast<uint32_t>(substream.frames_encoded));
  EXPECT_EQ(sender.huge_frames_sent, substream.huge_frames_sent);
  EXPECT_EQ(sender.rid, std::nullopt);
}

TEST_F(WebRtcVideoChannelTest,
       OutboundRtpIsActiveComesFromMatchingEncodingInSimulcast) {
  constexpr uint32_t kSsrc1 = 123u;
  constexpr uint32_t kSsrc2 = 456u;

  // Create simulcast stream from both SSRCs.
  // `kSsrc1` is the "main" ssrc used for getting parameters.
  FakeVideoSendStream* stream =
      AddSendStream(CreateSimStreamParams("cname", {kSsrc1, kSsrc2}));

  RtpParameters parameters = send_channel_->GetRtpSendParameters(kSsrc1);
  ASSERT_EQ(2u, parameters.encodings.size());
  parameters.encodings[0].active = false;
  parameters.encodings[1].active = true;
  send_channel_->SetRtpSendParameters(kSsrc1, parameters);

  // Fill in dummy stats.
  auto stats = GetInitialisedStats();
  stats.substreams[kSsrc1];
  stats.substreams[kSsrc2];
  stream->SetStats(stats);

  // GetStats() and ensure `active` matches `encodings` for each SSRC.
  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  ASSERT_EQ(send_info.senders.size(), 2u);
  ASSERT_TRUE(send_info.senders[0].active.has_value());
  EXPECT_FALSE(send_info.senders[0].active.value());
  ASSERT_TRUE(send_info.senders[1].active.has_value());
  EXPECT_TRUE(send_info.senders[1].active.value());
}

TEST_F(WebRtcVideoChannelTest, OutboundRtpIsActiveComesFromAnyEncodingInSvc) {
  VideoSenderParameters send_parameters;
  send_parameters.codecs.push_back(GetEngineCodec("VP9"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters));

  constexpr uint32_t kSsrc1 = 123u;
  constexpr uint32_t kSsrc2 = 456u;
  constexpr uint32_t kSsrc3 = 789u;

  // Configuring SVC is done the same way that simulcast is configured, the only
  // difference is that the VP9 codec is used. This triggers special hacks that
  // we depend on because we don't have a proper SVC API yet.
  FakeVideoSendStream* stream =
      AddSendStream(CreateSimStreamParams("cname", {kSsrc1, kSsrc2, kSsrc3}));
  // Expect that we got SVC.
  EXPECT_EQ(stream->GetEncoderConfig().number_of_streams, 1u);
  VideoCodecVP9 vp9_settings;
  ASSERT_TRUE(stream->GetVp9Settings(&vp9_settings));
  EXPECT_EQ(vp9_settings.numberOfSpatialLayers, 3u);

  RtpParameters parameters = send_channel_->GetRtpSendParameters(kSsrc1);
  ASSERT_EQ(3u, parameters.encodings.size());
  parameters.encodings[0].active = false;
  parameters.encodings[1].active = true;
  parameters.encodings[2].active = false;
  send_channel_->SetRtpSendParameters(kSsrc1, parameters);

  // Fill in dummy stats.
  auto stats = GetInitialisedStats();
  stats.substreams[kSsrc1];
  stream->SetStats(stats);

  // GetStats() and ensure `active` is true if ANY encoding is active.
  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  ASSERT_EQ(send_info.senders.size(), 1u);
  // Middle layer is active.
  ASSERT_TRUE(send_info.senders[0].active.has_value());
  EXPECT_TRUE(send_info.senders[0].active.value());

  parameters = send_channel_->GetRtpSendParameters(kSsrc1);
  ASSERT_EQ(3u, parameters.encodings.size());
  parameters.encodings[0].active = false;
  parameters.encodings[1].active = false;
  parameters.encodings[2].active = false;
  send_channel_->SetRtpSendParameters(kSsrc1, parameters);
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  ASSERT_EQ(send_info.senders.size(), 1u);
  // No layer is active.
  ASSERT_TRUE(send_info.senders[0].active.has_value());
  EXPECT_FALSE(send_info.senders[0].active.value());
}

TEST_F(WebRtcVideoChannelTest, MediaSubstreamMissingProducesEmpyStats) {
  FakeVideoSendStream* stream = AddSendStream();

  const uint32_t kRtxSsrc = 123u;
  const uint32_t kMissingMediaSsrc = 124u;

  // Set up a scenarios where we have a substream that is not kMedia (in this
  // case: kRtx) but its associated kMedia stream does not exist yet. This
  // results in zero GetPerLayerVideoSenderInfos despite non-empty substreams.
  // Covers https://crbug.com/1090712.
  auto stats = GetInitialisedStats();
  auto& substream = stats.substreams[kRtxSsrc];
  substream.type = VideoSendStream::StreamStats::StreamType::kRtx;
  substream.referenced_media_ssrc = kMissingMediaSsrc;
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_TRUE(send_info.senders.empty());
}

TEST_F(WebRtcVideoChannelTest, GetStatsReportsUpperResolution) {
  FakeVideoSendStream* stream = AddSendStream();
  VideoSendStream::Stats stats;
  stats.substreams[17].width = 123;
  stats.substreams[17].height = 40;
  stats.substreams[42].width = 80;
  stats.substreams[42].height = 31;
  stats.substreams[11].width = 20;
  stats.substreams[11].height = 90;
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  ASSERT_EQ(1u, send_info.aggregated_senders.size());
  ASSERT_EQ(3u, send_info.senders.size());
  EXPECT_EQ(123, send_info.senders[1].send_frame_width);
  EXPECT_EQ(40, send_info.senders[1].send_frame_height);
  EXPECT_EQ(80, send_info.senders[2].send_frame_width);
  EXPECT_EQ(31, send_info.senders[2].send_frame_height);
  EXPECT_EQ(20, send_info.senders[0].send_frame_width);
  EXPECT_EQ(90, send_info.senders[0].send_frame_height);
  EXPECT_EQ(123, send_info.aggregated_senders[0].send_frame_width);
  EXPECT_EQ(90, send_info.aggregated_senders[0].send_frame_height);
}

TEST_F(WebRtcVideoChannelTest, GetStatsReportsCpuAdaptationStats) {
  FakeVideoSendStream* stream = AddSendStream();
  VideoSendStream::Stats stats;
  stats.number_of_cpu_adapt_changes = 2;
  stats.cpu_limited_resolution = true;
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  ASSERT_EQ(1U, send_info.senders.size());
  EXPECT_EQ(WebRtcVideoChannel::ADAPTREASON_CPU,
            send_info.senders[0].adapt_reason);
  EXPECT_EQ(stats.number_of_cpu_adapt_changes,
            send_info.senders[0].adapt_changes);
}

TEST_F(WebRtcVideoChannelTest, GetStatsReportsAdaptationAndBandwidthStats) {
  FakeVideoSendStream* stream = AddSendStream();
  VideoSendStream::Stats stats;
  stats.number_of_cpu_adapt_changes = 2;
  stats.cpu_limited_resolution = true;
  stats.bw_limited_resolution = true;
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  ASSERT_EQ(1U, send_info.senders.size());
  EXPECT_EQ(WebRtcVideoChannel::ADAPTREASON_CPU |
                WebRtcVideoChannel::ADAPTREASON_BANDWIDTH,
            send_info.senders[0].adapt_reason);
  EXPECT_EQ(stats.number_of_cpu_adapt_changes,
            send_info.senders[0].adapt_changes);
}

TEST(WebRtcVideoChannelHelperTest, MergeInfoAboutOutboundRtpSubstreams) {
  const uint32_t kFirstMediaStreamSsrc = 10;
  const uint32_t kSecondMediaStreamSsrc = 20;
  const uint32_t kRtxSsrc = 30;
  std::map<uint32_t, VideoSendStream::StreamStats> substreams;
  // First kMedia stream.
  substreams[kFirstMediaStreamSsrc].type =
      VideoSendStream::StreamStats::StreamType::kMedia;
  substreams[kFirstMediaStreamSsrc].rtp_stats.transmitted.header_bytes = 1;
  substreams[kFirstMediaStreamSsrc].rtp_stats.transmitted.padding_bytes = 2;
  substreams[kFirstMediaStreamSsrc].rtp_stats.transmitted.payload_bytes = 3;
  substreams[kFirstMediaStreamSsrc].rtp_stats.transmitted.packets = 4;
  substreams[kFirstMediaStreamSsrc].rtp_stats.retransmitted.header_bytes = 5;
  substreams[kFirstMediaStreamSsrc].rtp_stats.retransmitted.padding_bytes = 6;
  substreams[kFirstMediaStreamSsrc].rtp_stats.retransmitted.payload_bytes = 7;
  substreams[kFirstMediaStreamSsrc].rtp_stats.retransmitted.packets = 8;
  substreams[kFirstMediaStreamSsrc].referenced_media_ssrc = std::nullopt;
  substreams[kFirstMediaStreamSsrc].width = 1280;
  substreams[kFirstMediaStreamSsrc].height = 720;
  // Second kMedia stream.
  substreams[kSecondMediaStreamSsrc].type =
      VideoSendStream::StreamStats::StreamType::kMedia;
  substreams[kSecondMediaStreamSsrc].rtp_stats.transmitted.header_bytes = 10;
  substreams[kSecondMediaStreamSsrc].rtp_stats.transmitted.padding_bytes = 11;
  substreams[kSecondMediaStreamSsrc].rtp_stats.transmitted.payload_bytes = 12;
  substreams[kSecondMediaStreamSsrc].rtp_stats.transmitted.packets = 13;
  substreams[kSecondMediaStreamSsrc].rtp_stats.retransmitted.header_bytes = 14;
  substreams[kSecondMediaStreamSsrc].rtp_stats.retransmitted.padding_bytes = 15;
  substreams[kSecondMediaStreamSsrc].rtp_stats.retransmitted.payload_bytes = 16;
  substreams[kSecondMediaStreamSsrc].rtp_stats.retransmitted.packets = 17;
  substreams[kSecondMediaStreamSsrc].referenced_media_ssrc = std::nullopt;
  substreams[kSecondMediaStreamSsrc].width = 640;
  substreams[kSecondMediaStreamSsrc].height = 480;
  // kRtx stream referencing the first kMedia stream.
  substreams[kRtxSsrc].type = VideoSendStream::StreamStats::StreamType::kRtx;
  substreams[kRtxSsrc].rtp_stats.transmitted.header_bytes = 19;
  substreams[kRtxSsrc].rtp_stats.transmitted.padding_bytes = 20;
  substreams[kRtxSsrc].rtp_stats.transmitted.payload_bytes = 21;
  substreams[kRtxSsrc].rtp_stats.transmitted.packets = 22;
  substreams[kRtxSsrc].rtp_stats.retransmitted.header_bytes = 23;
  substreams[kRtxSsrc].rtp_stats.retransmitted.padding_bytes = 24;
  substreams[kRtxSsrc].rtp_stats.retransmitted.payload_bytes = 25;
  substreams[kRtxSsrc].rtp_stats.retransmitted.packets = 26;
  substreams[kRtxSsrc].referenced_media_ssrc = kFirstMediaStreamSsrc;
  // kFlexfec stream referencing the second kMedia stream.
  substreams[kFlexfecSsrc].type =
      VideoSendStream::StreamStats::StreamType::kFlexfec;
  substreams[kFlexfecSsrc].rtp_stats.transmitted.header_bytes = 19;
  substreams[kFlexfecSsrc].rtp_stats.transmitted.padding_bytes = 20;
  substreams[kFlexfecSsrc].rtp_stats.transmitted.payload_bytes = 21;
  substreams[kFlexfecSsrc].rtp_stats.transmitted.packets = 22;
  substreams[kFlexfecSsrc].rtp_stats.retransmitted.header_bytes = 23;
  substreams[kFlexfecSsrc].rtp_stats.retransmitted.padding_bytes = 24;
  substreams[kFlexfecSsrc].rtp_stats.retransmitted.payload_bytes = 25;
  substreams[kFlexfecSsrc].rtp_stats.retransmitted.packets = 26;
  substreams[kFlexfecSsrc].referenced_media_ssrc = kSecondMediaStreamSsrc;

  auto merged_substreams =
      MergeInfoAboutOutboundRtpSubstreamsForTesting(substreams);
  // Only kMedia substreams remain.
  EXPECT_TRUE(merged_substreams.find(kFirstMediaStreamSsrc) !=
              merged_substreams.end());
  EXPECT_EQ(merged_substreams[kFirstMediaStreamSsrc].type,
            VideoSendStream::StreamStats::StreamType::kMedia);
  EXPECT_TRUE(merged_substreams.find(kSecondMediaStreamSsrc) !=
              merged_substreams.end());
  EXPECT_EQ(merged_substreams[kSecondMediaStreamSsrc].type,
            VideoSendStream::StreamStats::StreamType::kMedia);
  EXPECT_FALSE(merged_substreams.find(kRtxSsrc) != merged_substreams.end());
  EXPECT_FALSE(merged_substreams.find(kFlexfecSsrc) != merged_substreams.end());
  // Expect kFirstMediaStreamSsrc's rtp_stats to be merged with kRtxSsrc.
  StreamDataCounters first_media_expected_rtp_stats =
      substreams[kFirstMediaStreamSsrc].rtp_stats;
  first_media_expected_rtp_stats.Add(substreams[kRtxSsrc].rtp_stats);
  EXPECT_EQ(merged_substreams[kFirstMediaStreamSsrc].rtp_stats.transmitted,
            first_media_expected_rtp_stats.transmitted);
  EXPECT_EQ(merged_substreams[kFirstMediaStreamSsrc].rtp_stats.retransmitted,
            first_media_expected_rtp_stats.retransmitted);
  // Expect kSecondMediaStreamSsrc' rtp_stats to be merged with kFlexfecSsrc.
  StreamDataCounters second_media_expected_rtp_stats =
      substreams[kSecondMediaStreamSsrc].rtp_stats;
  second_media_expected_rtp_stats.Add(substreams[kFlexfecSsrc].rtp_stats);
  EXPECT_EQ(merged_substreams[kSecondMediaStreamSsrc].rtp_stats.transmitted,
            second_media_expected_rtp_stats.transmitted);
  EXPECT_EQ(merged_substreams[kSecondMediaStreamSsrc].rtp_stats.retransmitted,
            second_media_expected_rtp_stats.retransmitted);
  // Expect other metrics to come from the original kMedia stats.
  EXPECT_EQ(merged_substreams[kFirstMediaStreamSsrc].width,
            substreams[kFirstMediaStreamSsrc].width);
  EXPECT_EQ(merged_substreams[kFirstMediaStreamSsrc].height,
            substreams[kFirstMediaStreamSsrc].height);
  EXPECT_EQ(merged_substreams[kSecondMediaStreamSsrc].width,
            substreams[kSecondMediaStreamSsrc].width);
  EXPECT_EQ(merged_substreams[kSecondMediaStreamSsrc].height,
            substreams[kSecondMediaStreamSsrc].height);
}

TEST_F(WebRtcVideoChannelTest,
       GetStatsReportsTransmittedAndRetransmittedBytesAndPacketsCorrectly) {
  FakeVideoSendStream* stream = AddSendStream();
  VideoSendStream::Stats stats;
  // Simulcast layer 1, RTP stream. header+padding=10, payload=20, packets=3.
  stats.substreams[101].type = VideoSendStream::StreamStats::StreamType::kMedia;
  stats.substreams[101].rtp_stats.transmitted.header_bytes = 5;
  stats.substreams[101].rtp_stats.transmitted.padding_bytes = 5;
  stats.substreams[101].rtp_stats.transmitted.payload_bytes = 20;
  stats.substreams[101].rtp_stats.transmitted.packets = 3;
  stats.substreams[101].rtp_stats.retransmitted.header_bytes = 0;
  stats.substreams[101].rtp_stats.retransmitted.padding_bytes = 0;
  stats.substreams[101].rtp_stats.retransmitted.payload_bytes = 0;
  stats.substreams[101].rtp_stats.retransmitted.packets = 0;
  stats.substreams[101].referenced_media_ssrc = std::nullopt;
  // Simulcast layer 1, RTX stream. header+padding=5, payload=10, packets=1.
  stats.substreams[102].type = VideoSendStream::StreamStats::StreamType::kRtx;
  stats.substreams[102].rtp_stats.retransmitted.header_bytes = 3;
  stats.substreams[102].rtp_stats.retransmitted.padding_bytes = 2;
  stats.substreams[102].rtp_stats.retransmitted.payload_bytes = 10;
  stats.substreams[102].rtp_stats.retransmitted.packets = 1;
  stats.substreams[102].rtp_stats.transmitted =
      stats.substreams[102].rtp_stats.retransmitted;
  stats.substreams[102].referenced_media_ssrc = 101;
  // Simulcast layer 2, RTP stream. header+padding=20, payload=40, packets=7.
  stats.substreams[201].type = VideoSendStream::StreamStats::StreamType::kMedia;
  stats.substreams[201].rtp_stats.transmitted.header_bytes = 10;
  stats.substreams[201].rtp_stats.transmitted.padding_bytes = 10;
  stats.substreams[201].rtp_stats.transmitted.payload_bytes = 40;
  stats.substreams[201].rtp_stats.transmitted.packets = 7;
  stats.substreams[201].rtp_stats.retransmitted.header_bytes = 0;
  stats.substreams[201].rtp_stats.retransmitted.padding_bytes = 0;
  stats.substreams[201].rtp_stats.retransmitted.payload_bytes = 0;
  stats.substreams[201].rtp_stats.retransmitted.packets = 0;
  stats.substreams[201].referenced_media_ssrc = std::nullopt;
  // Simulcast layer 2, RTX stream. header+padding=10, payload=20, packets=4.
  stats.substreams[202].type = VideoSendStream::StreamStats::StreamType::kRtx;
  stats.substreams[202].rtp_stats.retransmitted.header_bytes = 6;
  stats.substreams[202].rtp_stats.retransmitted.padding_bytes = 4;
  stats.substreams[202].rtp_stats.retransmitted.payload_bytes = 20;
  stats.substreams[202].rtp_stats.retransmitted.packets = 4;
  stats.substreams[202].rtp_stats.transmitted =
      stats.substreams[202].rtp_stats.retransmitted;
  stats.substreams[202].referenced_media_ssrc = 201;
  // FlexFEC stream associated with the Simulcast layer 2.
  // header+padding=15, payload=17, packets=5.
  stats.substreams[301].type =
      VideoSendStream::StreamStats::StreamType::kFlexfec;
  stats.substreams[301].rtp_stats.transmitted.header_bytes = 13;
  stats.substreams[301].rtp_stats.transmitted.padding_bytes = 2;
  stats.substreams[301].rtp_stats.transmitted.payload_bytes = 17;
  stats.substreams[301].rtp_stats.transmitted.packets = 5;
  stats.substreams[301].rtp_stats.retransmitted.header_bytes = 0;
  stats.substreams[301].rtp_stats.retransmitted.padding_bytes = 0;
  stats.substreams[301].rtp_stats.retransmitted.payload_bytes = 0;
  stats.substreams[301].rtp_stats.retransmitted.packets = 0;
  stats.substreams[301].referenced_media_ssrc = 201;
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_EQ(send_info.senders.size(), 2u);
  EXPECT_EQ(15u, send_info.senders[0].header_and_padding_bytes_sent);
  EXPECT_EQ(30u, send_info.senders[0].payload_bytes_sent);
  EXPECT_EQ(4, send_info.senders[0].packets_sent);
  EXPECT_EQ(10u, send_info.senders[0].retransmitted_bytes_sent);
  EXPECT_EQ(1u, send_info.senders[0].retransmitted_packets_sent);

  EXPECT_EQ(45u, send_info.senders[1].header_and_padding_bytes_sent);
  EXPECT_EQ(77u, send_info.senders[1].payload_bytes_sent);
  EXPECT_EQ(16, send_info.senders[1].packets_sent);
  EXPECT_EQ(20u, send_info.senders[1].retransmitted_bytes_sent);
  EXPECT_EQ(4u, send_info.senders[1].retransmitted_packets_sent);
}

TEST_F(WebRtcVideoChannelTest,
       GetStatsTranslatesBandwidthLimitedResolutionCorrectly) {
  FakeVideoSendStream* stream = AddSendStream();
  VideoSendStream::Stats stats;
  stats.bw_limited_resolution = true;
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  ASSERT_EQ(1U, send_info.senders.size());
  EXPECT_EQ(WebRtcVideoChannel::ADAPTREASON_BANDWIDTH,
            send_info.senders[0].adapt_reason);
}

TEST_F(WebRtcVideoChannelTest, GetStatsTranslatesSendRtcpPacketTypesCorrectly) {
  FakeVideoSendStream* stream = AddSendStream();
  VideoSendStream::Stats stats;
  stats.substreams[17].rtcp_packet_type_counts.fir_packets = 2;
  stats.substreams[17].rtcp_packet_type_counts.nack_packets = 3;
  stats.substreams[17].rtcp_packet_type_counts.pli_packets = 4;

  stats.substreams[42].rtcp_packet_type_counts.fir_packets = 5;
  stats.substreams[42].rtcp_packet_type_counts.nack_packets = 7;
  stats.substreams[42].rtcp_packet_type_counts.pli_packets = 9;

  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_EQ(2, send_info.senders[0].firs_received);
  EXPECT_EQ(3u, send_info.senders[0].nacks_received);
  EXPECT_EQ(4, send_info.senders[0].plis_received);

  EXPECT_EQ(5, send_info.senders[1].firs_received);
  EXPECT_EQ(7u, send_info.senders[1].nacks_received);
  EXPECT_EQ(9, send_info.senders[1].plis_received);

  EXPECT_EQ(7, send_info.aggregated_senders[0].firs_received);
  EXPECT_EQ(10u, send_info.aggregated_senders[0].nacks_received);
  EXPECT_EQ(13, send_info.aggregated_senders[0].plis_received);
}

TEST_F(WebRtcVideoChannelTest,
       GetStatsTranslatesReceiveRtcpPacketTypesCorrectly) {
  FakeVideoReceiveStream* stream = AddRecvStream();
  VideoReceiveStreamInterface::Stats stats;
  stats.rtcp_packet_type_counts.fir_packets = 2;
  stats.rtcp_packet_type_counts.nack_packets = 3;
  stats.rtcp_packet_type_counts.pli_packets = 4;
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_EQ(stats.rtcp_packet_type_counts.fir_packets,
            checked_cast<unsigned int>(receive_info.receivers[0].firs_sent));
  EXPECT_EQ(stats.rtcp_packet_type_counts.nack_packets,
            receive_info.receivers[0].nacks_sent);
  EXPECT_EQ(stats.rtcp_packet_type_counts.pli_packets,
            checked_cast<unsigned int>(receive_info.receivers[0].plis_sent));
}

TEST_F(WebRtcVideoChannelTest, GetStatsTranslatesDecodeStatsCorrectly) {
  FakeVideoReceiveStream* stream = AddRecvStream();
  VideoReceiveStreamInterface::Stats stats;
  stats.decoder_implementation_name = "decoder_implementation_name";
  stats.decode_ms = 2;
  stats.max_decode_ms = 3;
  stats.current_delay_ms = 4;
  stats.target_delay_ms = 5;
  stats.jitter_buffer_ms = 6;
  stats.jitter_buffer_delay = TimeDelta::Seconds(60);
  stats.jitter_buffer_target_delay = TimeDelta::Seconds(55);
  stats.jitter_buffer_emitted_count = 6;
  stats.jitter_buffer_minimum_delay = TimeDelta::Seconds(50);
  stats.min_playout_delay_ms = 7;
  stats.render_delay_ms = 8;
  stats.width = 9;
  stats.height = 10;
  stats.received_frame_counts.key_frames = 11;
  stats.received_frame_counts.delta_frames = 12;
  stats.frames_rendered = 13;
  stats.frames_decoded = 14;
  stats.key_frames_decoded = 4;
  stats.qp_sum = 15;
  stats.corruption_score_sum = 0.3;
  stats.corruption_score_squared_sum = 0.05;
  stats.corruption_score_count = 2;
  stats.total_decode_time = TimeDelta::Millis(16);
  stats.total_assembly_time = TimeDelta::Millis(4);
  stats.frames_assembled_from_multiple_packets = 2;
  stats.power_efficient_decoder = true;
  RtpReceiveStats rtx_stats;
  rtx_stats.packet_counter.packets = 5;
  rtx_stats.packet_counter.payload_bytes = 23;
  stats.rtx_rtp_stats = rtx_stats;
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_EQ(stats.decoder_implementation_name,
            receive_info.receivers[0].decoder_implementation_name);
  EXPECT_EQ(stats.decode_ms, receive_info.receivers[0].decode_ms);
  EXPECT_EQ(stats.max_decode_ms, receive_info.receivers[0].max_decode_ms);
  EXPECT_EQ(stats.current_delay_ms, receive_info.receivers[0].current_delay_ms);
  EXPECT_EQ(stats.target_delay_ms, receive_info.receivers[0].target_delay_ms);
  EXPECT_EQ(stats.jitter_buffer_ms, receive_info.receivers[0].jitter_buffer_ms);
  EXPECT_EQ(stats.jitter_buffer_delay.seconds<double>(),
            receive_info.receivers[0].jitter_buffer_delay_seconds);
  EXPECT_EQ(stats.jitter_buffer_target_delay.seconds<double>(),
            receive_info.receivers[0].jitter_buffer_target_delay_seconds);
  EXPECT_EQ(stats.jitter_buffer_emitted_count,
            receive_info.receivers[0].jitter_buffer_emitted_count);
  EXPECT_EQ(stats.jitter_buffer_minimum_delay.seconds<double>(),
            receive_info.receivers[0].jitter_buffer_minimum_delay_seconds);
  EXPECT_EQ(stats.min_playout_delay_ms,
            receive_info.receivers[0].min_playout_delay_ms);
  EXPECT_EQ(stats.render_delay_ms, receive_info.receivers[0].render_delay_ms);
  EXPECT_EQ(stats.width, receive_info.receivers[0].frame_width);
  EXPECT_EQ(stats.height, receive_info.receivers[0].frame_height);
  EXPECT_EQ(
      checked_cast<unsigned int>(stats.received_frame_counts.key_frames +
                                 stats.received_frame_counts.delta_frames),
      receive_info.receivers[0].frames_received);
  EXPECT_EQ(stats.frames_rendered, receive_info.receivers[0].frames_rendered);
  EXPECT_EQ(stats.frames_decoded, receive_info.receivers[0].frames_decoded);
  EXPECT_EQ(stats.key_frames_decoded,
            receive_info.receivers[0].key_frames_decoded);
  EXPECT_EQ(stats.qp_sum, receive_info.receivers[0].qp_sum);
  EXPECT_EQ(stats.corruption_score_sum,
            receive_info.receivers[0].corruption_score_sum);
  EXPECT_EQ(stats.corruption_score_squared_sum,
            receive_info.receivers[0].corruption_score_squared_sum);
  EXPECT_EQ(stats.corruption_score_count,
            receive_info.receivers[0].corruption_score_count);
  EXPECT_EQ(stats.total_decode_time,
            receive_info.receivers[0].total_decode_time);
  EXPECT_EQ(stats.total_assembly_time,
            receive_info.receivers[0].total_assembly_time);
  EXPECT_EQ(stats.frames_assembled_from_multiple_packets,
            receive_info.receivers[0].frames_assembled_from_multiple_packets);
  EXPECT_TRUE(receive_info.receivers[0].power_efficient_decoder);
  EXPECT_EQ(stats.rtx_rtp_stats->packet_counter.packets,
            receive_info.receivers[0].retransmitted_packets_received);
  EXPECT_EQ(stats.rtx_rtp_stats->packet_counter.payload_bytes,
            receive_info.receivers[0].retransmitted_bytes_received);
}

TEST_F(WebRtcVideoChannelTest,
       GetStatsTranslatesInterFrameDelayStatsCorrectly) {
  FakeVideoReceiveStream* stream = AddRecvStream();
  VideoReceiveStreamInterface::Stats stats;
  stats.total_inter_frame_delay = 0.123;
  stats.total_squared_inter_frame_delay = 0.00456;
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_EQ(stats.total_inter_frame_delay,
            receive_info.receivers[0].total_inter_frame_delay);
  EXPECT_EQ(stats.total_squared_inter_frame_delay,
            receive_info.receivers[0].total_squared_inter_frame_delay);
}

TEST_F(WebRtcVideoChannelTest, GetStatsTranslatesReceivePacketStatsCorrectly) {
  FakeVideoReceiveStream* stream = AddRecvStream();
  VideoReceiveStreamInterface::Stats stats;
  stats.rtp_stats.packet_counter.payload_bytes = 2;
  stats.rtp_stats.packet_counter.header_bytes = 3;
  stats.rtp_stats.packet_counter.padding_bytes = 4;
  stats.rtp_stats.packet_counter.packets = 5;
  stats.rtp_stats.packets_lost = 6;
  stream->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_EQ(
      stats.rtp_stats.packet_counter.payload_bytes,
      checked_cast<size_t>(receive_info.receivers[0].payload_bytes_received));
  EXPECT_EQ(
      stats.rtp_stats.packet_counter.packets,
      checked_cast<unsigned int>(receive_info.receivers[0].packets_received));
  EXPECT_EQ(stats.rtp_stats.packets_lost,
            receive_info.receivers[0].packets_lost);
}

TEST_F(WebRtcVideoChannelTest, TranslatesCallStatsCorrectly) {
  AddSendStream();
  AddSendStream();
  Call::Stats stats;
  stats.rtt_ms = 123;
  fake_call_->SetStats(stats);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  ASSERT_EQ(2u, send_info.senders.size());
  EXPECT_EQ(stats.rtt_ms, send_info.senders[0].rtt_ms);
  EXPECT_EQ(stats.rtt_ms, send_info.senders[1].rtt_ms);
}

TEST_F(WebRtcVideoChannelTest, TranslatesSenderBitrateStatsCorrectly) {
  FakeVideoSendStream* stream = AddSendStream();
  VideoSendStream::Stats stats;
  stats.target_media_bitrate_bps = 156;
  stats.media_bitrate_bps = 123;
  stats.substreams[17].total_bitrate_bps = 1;
  stats.substreams[17].retransmit_bitrate_bps = 2;
  stats.substreams[42].total_bitrate_bps = 3;
  stats.substreams[42].retransmit_bitrate_bps = 4;
  stream->SetStats(stats);

  FakeVideoSendStream* stream2 = AddSendStream();
  VideoSendStream::Stats stats2;
  stats2.target_media_bitrate_bps = 200;
  stats2.media_bitrate_bps = 321;
  stats2.substreams[13].total_bitrate_bps = 5;
  stats2.substreams[13].retransmit_bitrate_bps = 6;
  stats2.substreams[21].total_bitrate_bps = 7;
  stats2.substreams[21].retransmit_bitrate_bps = 8;
  stream2->SetStats(stats2);

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  ASSERT_EQ(2u, send_info.aggregated_senders.size());
  ASSERT_EQ(4u, send_info.senders.size());
  BandwidthEstimationInfo bwe_info;
  send_channel_->FillBitrateInfo(&bwe_info);
  // Assuming stream and stream2 corresponds to senders[0] and [1] respectively
  // is OK as std::maps are sorted and AddSendStream() gives increasing SSRCs.
  EXPECT_EQ(stats.media_bitrate_bps,
            send_info.aggregated_senders[0].nominal_bitrate);
  EXPECT_EQ(stats2.media_bitrate_bps,
            send_info.aggregated_senders[1].nominal_bitrate);
  EXPECT_EQ(stats.target_media_bitrate_bps + stats2.target_media_bitrate_bps,
            bwe_info.target_enc_bitrate);
  EXPECT_EQ(stats.media_bitrate_bps + stats2.media_bitrate_bps,
            bwe_info.actual_enc_bitrate);
  EXPECT_EQ(1 + 3 + 5 + 7, bwe_info.transmit_bitrate)
      << "Bandwidth stats should take all streams into account.";
  EXPECT_EQ(2 + 4 + 6 + 8, bwe_info.retransmit_bitrate)
      << "Bandwidth stats should take all streams into account.";
}

TEST_F(WebRtcVideoChannelTest, DefaultReceiveStreamReconfiguresToUseRtx) {
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  const std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs1);
  const std::vector<uint32_t> rtx_ssrcs = MAKE_VECTOR(kRtxSsrcs1);

  ASSERT_EQ(0u, fake_call_->GetVideoReceiveStreams().size());
  RtpPacketReceived packet;
  packet.SetSsrc(ssrcs[0]);
  ReceivePacketAndAdvanceTime(packet);

  ASSERT_EQ(1u, fake_call_->GetVideoReceiveStreams().size())
      << "No default receive stream created.";
  FakeVideoReceiveStream* recv_stream = fake_call_->GetVideoReceiveStreams()[0];
  EXPECT_EQ(0u, recv_stream->GetConfig().rtp.rtx_ssrc)
      << "Default receive stream should not have configured RTX";

  EXPECT_TRUE(receive_channel_->AddRecvStream(
      CreateSimWithRtxStreamParams("cname", ssrcs, rtx_ssrcs)));
  ASSERT_EQ(1u, fake_call_->GetVideoReceiveStreams().size())
      << "AddRecvStream should have reconfigured, not added a new receiver.";
  recv_stream = fake_call_->GetVideoReceiveStreams()[0];
  EXPECT_FALSE(
      recv_stream->GetConfig().rtp.rtx_associated_payload_types.empty());
  EXPECT_TRUE(VerifyRtxReceiveAssociations(recv_stream->GetConfig()))
      << "RTX should be mapped for all decoders/payload types.";
  EXPECT_TRUE(HasRtxReceiveAssociation(recv_stream->GetConfig(),
                                       GetEngineCodec("red").id))
      << "RTX should be mapped also for the RED payload type";
  EXPECT_EQ(rtx_ssrcs[0], recv_stream->GetConfig().rtp.rtx_ssrc);
}

TEST_F(WebRtcVideoChannelTest, RejectsAddingStreamsWithMissingSsrcsForRtx) {
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  const std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs1);
  const std::vector<uint32_t> rtx_ssrcs = MAKE_VECTOR(kRtxSsrcs1);

  StreamParams sp = CreateSimWithRtxStreamParams("cname", ssrcs, rtx_ssrcs);
  sp.ssrcs = ssrcs;  // Without RTXs, this is the important part.

  EXPECT_FALSE(send_channel_->AddSendStream(sp));
  EXPECT_FALSE(receive_channel_->AddRecvStream(sp));
}

TEST_F(WebRtcVideoChannelTest, RejectsAddingStreamsWithOverlappingRtxSsrcs) {
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  const std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs1);
  const std::vector<uint32_t> rtx_ssrcs = MAKE_VECTOR(kRtxSsrcs1);

  StreamParams sp = CreateSimWithRtxStreamParams("cname", ssrcs, rtx_ssrcs);

  EXPECT_TRUE(send_channel_->AddSendStream(sp));
  EXPECT_TRUE(receive_channel_->AddRecvStream(sp));

  // The RTX SSRC is already used in previous streams, using it should fail.
  sp = StreamParams::CreateLegacy(rtx_ssrcs[0]);
  EXPECT_FALSE(send_channel_->AddSendStream(sp));
  EXPECT_FALSE(receive_channel_->AddRecvStream(sp));

  // After removing the original stream this should be fine to add (makes sure
  // that RTX ssrcs are not forever taken).
  EXPECT_TRUE(send_channel_->RemoveSendStream(ssrcs[0]));
  EXPECT_TRUE(receive_channel_->RemoveRecvStream(ssrcs[0]));
  EXPECT_TRUE(send_channel_->AddSendStream(sp));
  EXPECT_TRUE(receive_channel_->AddRecvStream(sp));
}

TEST_F(WebRtcVideoChannelTest,
       RejectsAddingStreamsWithOverlappingSimulcastSsrcs) {
  static const uint32_t kFirstStreamSsrcs[] = {1, 2, 3};
  static const uint32_t kOverlappingStreamSsrcs[] = {4, 3, 5};
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  StreamParams sp =
      CreateSimStreamParams("cname", MAKE_VECTOR(kFirstStreamSsrcs));

  EXPECT_TRUE(send_channel_->AddSendStream(sp));
  EXPECT_TRUE(receive_channel_->AddRecvStream(sp));

  // One of the SSRCs is already used in previous streams, using it should fail.
  sp = CreateSimStreamParams("cname", MAKE_VECTOR(kOverlappingStreamSsrcs));
  EXPECT_FALSE(send_channel_->AddSendStream(sp));
  EXPECT_FALSE(receive_channel_->AddRecvStream(sp));

  // After removing the original stream this should be fine to add (makes sure
  // that RTX ssrcs are not forever taken).
  EXPECT_TRUE(send_channel_->RemoveSendStream(kFirstStreamSsrcs[0]));
  EXPECT_TRUE(receive_channel_->RemoveRecvStream(kFirstStreamSsrcs[0]));
  EXPECT_TRUE(send_channel_->AddSendStream(sp));
  EXPECT_TRUE(receive_channel_->AddRecvStream(sp));
}

TEST_F(WebRtcVideoChannelTest, ReportsSsrcGroupsInStats) {
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  static const uint32_t kSenderSsrcs[] = {4, 7, 10};
  static const uint32_t kSenderRtxSsrcs[] = {5, 8, 11};

  StreamParams sender_sp = CreateSimWithRtxStreamParams(
      "cname", MAKE_VECTOR(kSenderSsrcs), MAKE_VECTOR(kSenderRtxSsrcs));

  EXPECT_TRUE(send_channel_->AddSendStream(sender_sp));

  static const uint32_t kReceiverSsrcs[] = {3};
  static const uint32_t kReceiverRtxSsrcs[] = {2};

  StreamParams receiver_sp = CreateSimWithRtxStreamParams(
      "cname", MAKE_VECTOR(kReceiverSsrcs), MAKE_VECTOR(kReceiverRtxSsrcs));
  EXPECT_TRUE(receive_channel_->AddRecvStream(receiver_sp));

  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  ASSERT_EQ(3u, send_info.senders.size());
  ASSERT_EQ(1u, receive_info.receivers.size());

  EXPECT_NE(sender_sp.ssrc_groups, receiver_sp.ssrc_groups);
  EXPECT_EQ(sender_sp.ssrc_groups, send_info.senders[0].ssrc_groups);
  EXPECT_EQ(receiver_sp.ssrc_groups, receive_info.receivers[0].ssrc_groups);
}

TEST_F(WebRtcVideoChannelTest, MapsReceivedPayloadTypeToCodecName) {
  FakeVideoReceiveStream* stream = AddRecvStream();
  VideoReceiveStreamInterface::Stats stats;

  // Report no codec name before receiving.
  stream->SetStats(stats);
  VideoMediaSendInfo send_info;
  VideoMediaReceiveInfo receive_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_STREQ("", receive_info.receivers[0].codec_name.c_str());

  // Report VP8 if we're receiving it.
  stats.current_payload_type = GetEngineCodec("VP8").id;
  stream->SetStats(stats);
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_STREQ(kVp8CodecName, receive_info.receivers[0].codec_name.c_str());

  // Report no codec name for unknown playload types.
  stats.current_payload_type = 3;
  stream->SetStats(stats);
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_TRUE(receive_channel_->GetStats(&receive_info));

  EXPECT_STREQ("", receive_info.receivers[0].codec_name.c_str());
}

// Tests that when we add a stream without SSRCs, but contains a stream_id
// that it is stored and its stream id is later used when the first packet
// arrives to properly create a receive stream with a sync label.
TEST_F(WebRtcVideoChannelTest, RecvUnsignaledSsrcWithSignaledStreamId) {
  const char kSyncLabel[] = "sync_label";
  StreamParams unsignaled_stream;
  unsignaled_stream.set_stream_ids({kSyncLabel});
  ASSERT_TRUE(receive_channel_->AddRecvStream(unsignaled_stream));
  receive_channel_->OnDemuxerCriteriaUpdatePending();
  receive_channel_->OnDemuxerCriteriaUpdateComplete();
  time_controller_.AdvanceTime(TimeDelta::Zero());
  // The stream shouldn't have been created at this point because it doesn't
  // have any SSRCs.
  EXPECT_EQ(0u, fake_call_->GetVideoReceiveStreams().size());

  // Create and deliver packet.
  RtpPacketReceived packet;
  packet.SetSsrc(kIncomingUnsignalledSsrc);
  ReceivePacketAndAdvanceTime(packet);

  // The stream should now be created with the appropriate sync label.
  EXPECT_EQ(1u, fake_call_->GetVideoReceiveStreams().size());
  EXPECT_EQ(kSyncLabel,
            fake_call_->GetVideoReceiveStreams()[0]->GetConfig().sync_group);

  // Reset the unsignaled stream to clear the cache. This deletes the receive
  // stream.
  receive_channel_->ResetUnsignaledRecvStream();
  receive_channel_->OnDemuxerCriteriaUpdatePending();
  EXPECT_EQ(0u, fake_call_->GetVideoReceiveStreams().size());

  // Until the demuxer criteria has been updated, we ignore in-flight ssrcs of
  // the recently removed unsignaled receive stream.
  ReceivePacketAndAdvanceTime(packet);
  EXPECT_EQ(0u, fake_call_->GetVideoReceiveStreams().size());

  // After the demuxer criteria has been updated, we should proceed to create
  // unsignalled receive streams. This time when a default video receive stream
  // is created it won't have a sync_group.
  receive_channel_->OnDemuxerCriteriaUpdateComplete();
  ReceivePacketAndAdvanceTime(packet);
  EXPECT_EQ(1u, fake_call_->GetVideoReceiveStreams().size());
  EXPECT_TRUE(
      fake_call_->GetVideoReceiveStreams()[0]->GetConfig().sync_group.empty());
}

TEST_F(WebRtcVideoChannelTest,
       ResetUnsignaledRecvStreamDeletesAllDefaultStreams) {
  // No receive streams to start with.
  EXPECT_TRUE(fake_call_->GetVideoReceiveStreams().empty());

  // Packet with unsignaled SSRC is received.
  RtpPacketReceived packet;
  packet.SetSsrc(kIncomingUnsignalledSsrc);
  ReceivePacketAndAdvanceTime(packet);

  // Default receive stream created.
  const auto& receivers1 = fake_call_->GetVideoReceiveStreams();
  ASSERT_EQ(receivers1.size(), 1u);
  EXPECT_EQ(receivers1[0]->GetConfig().rtp.remote_ssrc,
            kIncomingUnsignalledSsrc);

  // Stream with another SSRC gets signaled.
  receive_channel_->ResetUnsignaledRecvStream();
  constexpr uint32_t kIncomingSignalledSsrc = kIncomingUnsignalledSsrc + 1;
  ASSERT_TRUE(receive_channel_->AddRecvStream(
      StreamParams::CreateLegacy(kIncomingSignalledSsrc)));

  // New receiver is for the signaled stream.
  const auto& receivers2 = fake_call_->GetVideoReceiveStreams();
  ASSERT_EQ(receivers2.size(), 1u);
  EXPECT_EQ(receivers2[0]->GetConfig().rtp.remote_ssrc, kIncomingSignalledSsrc);
}

TEST_F(WebRtcVideoChannelTest, GetUnsignaledSsrcs) {
  EXPECT_TRUE(receive_channel_->GetUnsignaledSsrcs().empty());

  // Receive a packet with an unsignaled SSRC.
  RtpPacketReceived packet;
  packet.SetSsrc(kIncomingUnsignalledSsrc);
  ReceivePacketAndAdvanceTime(packet);

  EXPECT_THAT(receive_channel_->GetUnsignaledSsrcs(),
              testing::ElementsAre(kIncomingUnsignalledSsrc));

  // Resetting the unsignaled stream should clear the SSRCs.
  receive_channel_->ResetUnsignaledRecvStream();
  EXPECT_TRUE(receive_channel_->GetUnsignaledSsrcs().empty());
}

// If the first RTP packet for an unsignaled SSRC arrives before the default
// sink is attached, a later SetDefaultSink must still bind to the existing
// unsignaled receive stream.
TEST_F(WebRtcVideoChannelTest,
       SetDefaultSinkAfterUnsignaledPacketBindsExistingStream) {
  // Unsignaled packet arrives with no default sink set.
  RtpPacketReceived packet;
  packet.SetSsrc(kIncomingUnsignalledSsrc);
  ReceivePacketAndAdvanceTime(packet);

  ASSERT_EQ(1u, fake_call_->GetVideoReceiveStreams().size());
  FakeVideoReceiveStream* recv_stream = fake_call_->GetVideoReceiveStreams()[0];
  EXPECT_EQ(kIncomingUnsignalledSsrc, recv_stream->GetConfig().rtp.remote_ssrc);

  // Install default sink after the fact; must be bound to the existing stream.
  FakeVideoRenderer renderer;
  receive_channel_->SetDefaultSink(&renderer);

  VideoFrame video_frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(CreateBlackFrameBuffer(4, 4))
          .set_rtp_timestamp(100)
          .set_timestamp_us(0)
          .set_rotation(kVideoRotation_0)
          .build();
  recv_stream->InjectFrame(video_frame);
  EXPECT_EQ(1, renderer.num_rendered_frames());
}

TEST_F(WebRtcVideoChannelTest,
       RecentlyAddedSsrcsDoNotCreateUnsignalledRecvStreams) {
  const uint32_t kSsrc1 = 1;
  const uint32_t kSsrc2 = 2;

  // Starting point: receiving kSsrc1.
  EXPECT_TRUE(
      receive_channel_->AddRecvStream(StreamParams::CreateLegacy(kSsrc1)));
  receive_channel_->OnDemuxerCriteriaUpdatePending();
  receive_channel_->OnDemuxerCriteriaUpdateComplete();
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 1u);

  // If this is the only m= section the demuxer might be configure to forward
  // all packets, regardless of ssrc, to this channel. When we go to multiple m=
  // sections, there can thus be a window of time where packets that should
  // never have belonged to this channel arrive anyway.

  // Emulate a second m= section being created by updating the demuxer criteria
  // without adding any streams.
  receive_channel_->OnDemuxerCriteriaUpdatePending();

  // Emulate there being in-flight packets for kSsrc1 and kSsrc2 arriving before
  // the demuxer is updated.
  {
    // Receive a packet for kSsrc1.
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc1);
    ReceivePacketAndAdvanceTime(packet);
  }
  {
    // Receive a packet for kSsrc2.
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc2);
    ReceivePacketAndAdvanceTime(packet);
  }

  // No unsignaled ssrc for kSsrc2 should have been created, but kSsrc1 should
  // arrive since it already has a stream.
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 1u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc1), 1u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc2), 0u);

  // Signal that the demuxer update is complete. Because there are no more
  // pending demuxer updates, receiving unknown ssrcs (kSsrc2) should again
  // result in unsignalled receive streams being created.
  receive_channel_->OnDemuxerCriteriaUpdateComplete();
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // Receive packets for kSsrc1 and kSsrc2 again.
  {
    // Receive a packet for kSsrc1.
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc1);
    ReceivePacketAndAdvanceTime(packet);
  }
  {
    // Receive a packet for kSsrc2.
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc2);
    ReceivePacketAndAdvanceTime(packet);
  }

  // An unsignalled ssrc for kSsrc2 should be created and the packet counter
  // should increase for both ssrcs.
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 2u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc1), 2u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc2), 1u);
}

TEST_F(WebRtcVideoChannelTest,
       RecentlyRemovedSsrcsDoNotCreateUnsignalledRecvStreams) {
  const uint32_t kSsrc1 = 1;
  const uint32_t kSsrc2 = 2;

  // Starting point: receiving kSsrc1 and kSsrc2.
  EXPECT_TRUE(
      receive_channel_->AddRecvStream(StreamParams::CreateLegacy(kSsrc1)));
  EXPECT_TRUE(
      receive_channel_->AddRecvStream(StreamParams::CreateLegacy(kSsrc2)));
  receive_channel_->OnDemuxerCriteriaUpdatePending();
  receive_channel_->OnDemuxerCriteriaUpdateComplete();
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 2u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc1), 0u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc2), 0u);

  // Remove kSsrc1, signal that a demuxer criteria update is pending, but not
  // completed yet.
  EXPECT_TRUE(receive_channel_->RemoveRecvStream(kSsrc1));
  receive_channel_->OnDemuxerCriteriaUpdatePending();

  // We only have a receiver for kSsrc2 now.
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 1u);

  // Emulate there being in-flight packets for kSsrc1 and kSsrc2 arriving before
  // the demuxer is updated.
  {
    // Receive a packet for kSsrc1.
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc1);
    ReceivePacketAndAdvanceTime(packet);
  }
  {
    // Receive a packet for kSsrc2.
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc2);
    ReceivePacketAndAdvanceTime(packet);
  }

  // No unsignaled ssrc for kSsrc1 should have been created, but the packet
  // count for kSsrc2 should increase.
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 1u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc1), 0u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc2), 1u);

  // Signal that the demuxer update is complete. This means we should stop
  // ignorning kSsrc1.
  receive_channel_->OnDemuxerCriteriaUpdateComplete();
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // Receive packets for kSsrc1 and kSsrc2 again.
  {
    // Receive a packet for kSsrc1.
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc1);
    ReceivePacketAndAdvanceTime(packet);
  }
  {
    // Receive a packet for kSsrc2.
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc2);
    ReceivePacketAndAdvanceTime(packet);
  }

  // An unsignalled ssrc for kSsrc1 should be created and the packet counter
  // should increase for both ssrcs.
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 2u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc1), 1u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc2), 2u);
}

TEST_F(WebRtcVideoChannelTest, MultiplePendingDemuxerCriteriaUpdates) {
  // Starting point: receiving kSsrc.
  EXPECT_TRUE(
      receive_channel_->AddRecvStream(StreamParams::CreateLegacy(kSsrc)));
  receive_channel_->OnDemuxerCriteriaUpdatePending();
  receive_channel_->OnDemuxerCriteriaUpdateComplete();
  time_controller_.AdvanceTime(TimeDelta::Zero());
  ASSERT_EQ(fake_call_->GetVideoReceiveStreams().size(), 1u);

  // Remove kSsrc...
  EXPECT_TRUE(receive_channel_->RemoveRecvStream(kSsrc));
  receive_channel_->OnDemuxerCriteriaUpdatePending();
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 0u);
  // And then add it back again, before the demuxer knows about the new
  // criteria!
  EXPECT_TRUE(
      receive_channel_->AddRecvStream(StreamParams::CreateLegacy(kSsrc)));
  receive_channel_->OnDemuxerCriteriaUpdatePending();
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 1u);

  // In-flight packets should arrive because the stream was recreated, even
  // though demuxer criteria updates are pending...
  {
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc);
    ReceivePacketAndAdvanceTime(packet);
  }
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc), 1u);

  // Signal that the demuxer knows about the first update: the removal.
  receive_channel_->OnDemuxerCriteriaUpdateComplete();
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // This still should not prevent in-flight packets from arriving because we
  // have a receive stream for it.
  {
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc);
    ReceivePacketAndAdvanceTime(packet);
  }
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc), 2u);

  // Remove the kSsrc again while previous demuxer updates are still pending.
  EXPECT_TRUE(receive_channel_->RemoveRecvStream(kSsrc));
  receive_channel_->OnDemuxerCriteriaUpdatePending();
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 0u);

  // Now the packet should be dropped and not create an unsignalled receive
  // stream.
  {
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc);
    ReceivePacketAndAdvanceTime(packet);
  }
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 0u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc), 2u);

  // Signal that the demuxer knows about the second update: adding it back.
  receive_channel_->OnDemuxerCriteriaUpdateComplete();
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // The packets should continue to be dropped because removal happened after
  // the most recently completed demuxer update.
  {
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc);
    ReceivePacketAndAdvanceTime(packet);
  }
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 0u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc), 2u);

  // Signal that the demuxer knows about the last update: the second removal.
  receive_channel_->OnDemuxerCriteriaUpdateComplete();
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // If packets still arrive after the demuxer knows about the latest removal we
  // should finally create an unsignalled receive stream.
  {
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc);
    ReceivePacketAndAdvanceTime(packet);
  }
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 1u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc), 3u);
}

TEST_F(WebRtcVideoChannelTest, UnsignalledSsrcHasACooldown) {
  const uint32_t kSsrc1 = 1;
  const uint32_t kSsrc2 = 2;

  // Send packets for kSsrc1, creating an unsignalled receive stream.
  {
    // Receive a packet for kSsrc1.
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc1);
    receive_channel_->OnPacketReceived(packet);
  }

  time_controller_.AdvanceTime(
      TimeDelta::Millis(kUnsignalledReceiveStreamCooldownMs - 1));

  // We now have an unsignalled receive stream for kSsrc1.
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 1u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc1), 1u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc2), 0u);

  {
    // Receive a packet for kSsrc2.
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc2);
    receive_channel_->OnPacketReceived(packet);
  }
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // Not enough time has passed to replace the unsignalled receive stream, so
  // the kSsrc2 should be ignored.
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 1u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc1), 1u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc2), 0u);

  // After 500 ms, kSsrc2 should trigger a new unsignalled receive stream that
  // replaces the old one.
  time_controller_.AdvanceTime(TimeDelta::Millis(1));
  {
    // Receive a packet for kSsrc2.
    RtpPacketReceived packet;
    packet.SetSsrc(kSsrc2);
    receive_channel_->OnPacketReceived(packet);
  }
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // The old unsignalled receive stream was destroyed and replaced, so we still
  // only have one unsignalled receive stream. But tha packet counter for kSsrc2
  // has now increased.
  EXPECT_EQ(fake_call_->GetVideoReceiveStreams().size(), 1u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc1), 1u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(kSsrc2), 1u);
}

// Test BaseMinimumPlayoutDelayMs on receive streams.
TEST_F(WebRtcVideoChannelTest, BaseMinimumPlayoutDelayMs) {
  // Test that set won't work for non-existing receive streams.
  EXPECT_FALSE(receive_channel_->SetBaseMinimumPlayoutDelayMs(kSsrc + 2, 200));
  // Test that get won't work for non-existing receive streams.
  EXPECT_FALSE(receive_channel_->GetBaseMinimumPlayoutDelayMs(kSsrc + 2));

  EXPECT_TRUE(AddRecvStream());
  // Test that set works for the existing receive stream.
  EXPECT_TRUE(receive_channel_->SetBaseMinimumPlayoutDelayMs(last_ssrc_, 200));
  auto* recv_stream = fake_call_->GetVideoReceiveStream(last_ssrc_);
  EXPECT_TRUE(recv_stream);
  EXPECT_EQ(recv_stream->base_mininum_playout_delay_ms(), 200);
  EXPECT_EQ(
      receive_channel_->GetBaseMinimumPlayoutDelayMs(last_ssrc_).value_or(0),
      200);
}

// Test BaseMinimumPlayoutDelayMs on unsignaled receive streams.
TEST_F(WebRtcVideoChannelTest, BaseMinimumPlayoutDelayMsUnsignaledRecvStream) {
  std::optional<int> delay_ms;
  const FakeVideoReceiveStream* recv_stream;

  // Set default stream with SSRC 0
  EXPECT_TRUE(receive_channel_->SetBaseMinimumPlayoutDelayMs(0, 200));
  EXPECT_EQ(200, receive_channel_->GetBaseMinimumPlayoutDelayMs(0).value_or(0));

  // Spawn an unsignaled stream by sending a packet, it should inherit
  // default delay 200.
  RtpPacketReceived packet;
  packet.SetSsrc(kIncomingUnsignalledSsrc);
  ReceivePacketAndAdvanceTime(packet);

  recv_stream = fake_call_->GetVideoReceiveStream(kIncomingUnsignalledSsrc);
  EXPECT_EQ(recv_stream->base_mininum_playout_delay_ms(), 200);
  delay_ms =
      receive_channel_->GetBaseMinimumPlayoutDelayMs(kIncomingUnsignalledSsrc);
  EXPECT_EQ(200, delay_ms.value_or(0));

  // Check that now if we change delay for SSRC 0 it will change delay for the
  // default receiving stream as well.
  EXPECT_TRUE(receive_channel_->SetBaseMinimumPlayoutDelayMs(0, 300));
  EXPECT_EQ(300, receive_channel_->GetBaseMinimumPlayoutDelayMs(0).value_or(0));
  delay_ms =
      receive_channel_->GetBaseMinimumPlayoutDelayMs(kIncomingUnsignalledSsrc);
  EXPECT_EQ(300, delay_ms.value_or(0));
  recv_stream = fake_call_->GetVideoReceiveStream(kIncomingUnsignalledSsrc);
  EXPECT_EQ(recv_stream->base_mininum_playout_delay_ms(), 300);
}

void WebRtcVideoChannelTest::TestReceiveUnsignaledSsrcPacket(
    uint8_t payload_type,
    bool expect_created_receive_stream) {
  // kRedRtxPayloadType must currently be unused.
  EXPECT_FALSE(FindCodecById(engine_->LegacyRecvCodecs(), kRedRtxPayloadType));

  // Add a RED RTX codec.
  Codec red_rtx_codec =
      CreateVideoRtxCodec(kRedRtxPayloadType, GetEngineCodec("red").id);
  recv_parameters_.codecs.push_back(red_rtx_codec);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters_));

  ASSERT_EQ(0u, fake_call_->GetVideoReceiveStreams().size());
  RtpPacketReceived packet;
  packet.SetPayloadType(payload_type);
  packet.SetSsrc(kIncomingUnsignalledSsrc);
  ReceivePacketAndAdvanceTime(packet);

  if (expect_created_receive_stream) {
    EXPECT_EQ(1u, fake_call_->GetVideoReceiveStreams().size())
        << "Should have created a receive stream for payload type: "
        << payload_type;
  } else {
    EXPECT_EQ(0u, fake_call_->GetVideoReceiveStreams().size())
        << "Shouldn't have created a receive stream for payload type: "
        << payload_type;
  }
}

class WebRtcVideoChannelDiscardUnknownSsrcTest : public WebRtcVideoChannelTest {
 public:
  WebRtcVideoChannelDiscardUnknownSsrcTest()
      : WebRtcVideoChannelTest(
            "WebRTC-Video-DiscardPacketsWithUnknownSsrc/Enabled/") {}
};

TEST_F(WebRtcVideoChannelDiscardUnknownSsrcTest, NoUnsignalledStreamCreated) {
  TestReceiveUnsignaledSsrcPacket(GetEngineCodec("VP8").id,
                                  false /* expect_created_receive_stream */);
}

TEST_F(WebRtcVideoChannelTest, Vp8PacketCreatesUnsignalledStream) {
  TestReceiveUnsignaledSsrcPacket(GetEngineCodec("VP8").id,
                                  true /* expect_created_receive_stream */);
}

TEST_F(WebRtcVideoChannelTest, Vp9PacketCreatesUnsignalledStream) {
  TestReceiveUnsignaledSsrcPacket(GetEngineCodec("VP9").id,
                                  true /* expect_created_receive_stream */);
}

TEST_F(WebRtcVideoChannelTest, RtxPacketDoesntCreateUnsignalledStream) {
  AssignDefaultAptRtxTypes();
  const Codec vp8 = GetEngineCodec("VP8");
  const int rtx_vp8_payload_type = default_apt_rtx_types_[vp8.id];
  TestReceiveUnsignaledSsrcPacket(rtx_vp8_payload_type,
                                  false /* expect_created_receive_stream */);
}

TEST_F(WebRtcVideoChannelTest, UlpfecPacketDoesntCreateUnsignalledStream) {
  TestReceiveUnsignaledSsrcPacket(GetEngineCodec("ulpfec").id,
                                  false /* expect_created_receive_stream */);
}

TEST_F(WebRtcVideoChannelFlexfecRecvTest,
       FlexfecPacketDoesntCreateUnsignalledStream) {
  TestReceiveUnsignaledSsrcPacket(GetEngineCodec("flexfec-03").id,
                                  false /* expect_created_receive_stream */);
}

TEST_F(WebRtcVideoChannelTest, RedRtxPacketDoesntCreateUnsignalledStream) {
  TestReceiveUnsignaledSsrcPacket(kRedRtxPayloadType,
                                  false /* expect_created_receive_stream */);
}

TEST_F(WebRtcVideoChannelTest, RtxAfterMediaPacketUpdatesUnsignalledRtxSsrc) {
  AssignDefaultAptRtxTypes();
  const Codec vp8 = GetEngineCodec("VP8");
  const int payload_type = vp8.id;
  const int rtx_vp8_payload_type = default_apt_rtx_types_[vp8.id];
  const uint32_t ssrc = kIncomingUnsignalledSsrc;
  const uint32_t rtx_ssrc = ssrc + 1;

  // Send media packet.
  RtpPacketReceived packet;
  packet.SetPayloadType(payload_type);
  packet.SetSsrc(ssrc);
  ReceivePacketAndAdvanceTime(packet);
  EXPECT_EQ(1u, fake_call_->GetVideoReceiveStreams().size())
      << "Should have created a receive stream for payload type: "
      << payload_type;

  // Send rtx packet.
  RtpPacketReceived rtx_packet;
  rtx_packet.SetPayloadType(rtx_vp8_payload_type);
  rtx_packet.SetSsrc(rtx_ssrc);
  ReceivePacketAndAdvanceTime(rtx_packet);
  EXPECT_EQ(1u, fake_call_->GetVideoReceiveStreams().size())
      << "RTX packet should not have added or removed a receive stream";

  auto recv_stream = fake_call_->GetVideoReceiveStreams().front();
  auto& config = recv_stream->GetConfig();
  EXPECT_EQ(config.rtp.remote_ssrc, ssrc)
      << "Receive stream should have correct media ssrc";
  EXPECT_EQ(config.rtp.rtx_ssrc, rtx_ssrc)
      << "Receive stream should have correct rtx ssrc";
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(ssrc), 1u);
  EXPECT_EQ(fake_call_->GetDeliveredPacketsForSsrc(rtx_ssrc), 1u);
}

TEST_F(WebRtcVideoChannelTest, UnsignaledStreamCreatedAfterMediaPacket) {
  AssignDefaultAptRtxTypes();
  const Codec vp8 = GetEngineCodec("VP8");
  const int payload_type = vp8.id;
  const int rtx_vp8_payload_type = default_apt_rtx_types_[vp8.id];
  const uint32_t ssrc = kIncomingUnsignalledSsrc;
  const uint32_t rtx_ssrc = ssrc + 1;

  // Receive rtx packet.
  RtpPacketReceived rtx_packet;
  rtx_packet.SetPayloadType(rtx_vp8_payload_type);
  rtx_packet.SetSsrc(rtx_ssrc);
  receive_channel_->OnPacketReceived(rtx_packet);
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_EQ(0u, fake_call_->GetVideoReceiveStreams().size());

  // Receive media packet.
  RtpPacketReceived packet;
  packet.SetPayloadType(payload_type);
  packet.SetSsrc(ssrc);
  ReceivePacketAndAdvanceTime(packet);
  EXPECT_EQ(1u, fake_call_->GetVideoReceiveStreams().size());

  // Check receive stream has been recreated with correct ssrcs.
  auto recv_stream = fake_call_->GetVideoReceiveStreams().front();
  auto& config = recv_stream->GetConfig();
  EXPECT_EQ(config.rtp.remote_ssrc, ssrc)
      << "Receive stream should have correct media ssrc";
}

// Test that receiving any unsignalled SSRC works even if it changes.
// The first unsignalled SSRC received will create a default receive stream.
// Any different unsignalled SSRC received will replace the default.
TEST_F(WebRtcVideoChannelTest, ReceiveDifferentUnsignaledSsrc) {
  // Allow receiving VP8, VP9, H264 (if enabled).
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("VP9"));

#if defined(WEBRTC_USE_H264)
  Codec H264codec = CreateVideoCodec(126, "H264");
  parameters.codecs.push_back(H264codec);
#endif

  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  // No receive streams yet.
  ASSERT_EQ(0u, fake_call_->GetVideoReceiveStreams().size());
  FakeVideoRenderer renderer;
  receive_channel_->SetDefaultSink(&renderer);

  // Receive VP8 packet on first SSRC.
  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(GetEngineCodec("VP8").id);
  rtp_packet.SetSsrc(kIncomingUnsignalledSsrc + 1);
  ReceivePacketAndAdvanceTime(rtp_packet);
  // VP8 packet should create default receive stream.
  ASSERT_EQ(1u, fake_call_->GetVideoReceiveStreams().size());
  FakeVideoReceiveStream* recv_stream = fake_call_->GetVideoReceiveStreams()[0];
  EXPECT_EQ(rtp_packet.Ssrc(), recv_stream->GetConfig().rtp.remote_ssrc);
  // Verify that the receive stream sinks to a renderer.
  VideoFrame video_frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(CreateBlackFrameBuffer(4, 4))
          .set_rtp_timestamp(100)
          .set_timestamp_us(0)
          .set_rotation(kVideoRotation_0)
          .build();
  recv_stream->InjectFrame(video_frame);
  EXPECT_EQ(1, renderer.num_rendered_frames());

  // Receive VP9 packet on second SSRC.
  rtp_packet.SetPayloadType(GetEngineCodec("VP9").id);
  rtp_packet.SetSsrc(kIncomingUnsignalledSsrc + 2);
  ReceivePacketAndAdvanceTime(rtp_packet);
  // VP9 packet should replace the default receive SSRC.
  ASSERT_EQ(1u, fake_call_->GetVideoReceiveStreams().size());
  recv_stream = fake_call_->GetVideoReceiveStreams()[0];
  EXPECT_EQ(rtp_packet.Ssrc(), recv_stream->GetConfig().rtp.remote_ssrc);
  // Verify that the receive stream sinks to a renderer.
  VideoFrame video_frame2 =
      VideoFrame::Builder()
          .set_video_frame_buffer(CreateBlackFrameBuffer(4, 4))
          .set_rtp_timestamp(200)
          .set_timestamp_us(0)
          .set_rotation(kVideoRotation_0)
          .build();
  recv_stream->InjectFrame(video_frame2);
  EXPECT_EQ(2, renderer.num_rendered_frames());

#if defined(WEBRTC_USE_H264)
  // Receive H264 packet on third SSRC.
  rtp_packet.SetPayloadType(126);
  rtp_packet.SetSsrc(kIncomingUnsignalledSsrc + 3);
  ReceivePacketAndAdvanceTime(rtp_packet);
  // H264 packet should replace the default receive SSRC.
  ASSERT_EQ(1u, fake_call_->GetVideoReceiveStreams().size());
  recv_stream = fake_call_->GetVideoReceiveStreams()[0];
  EXPECT_EQ(rtp_packet.Ssrc(), recv_stream->GetConfig().rtp.remote_ssrc);
  // Verify that the receive stream sinks to a renderer.
  VideoFrame video_frame3 =
      VideoFrame::Builder()
          .set_video_frame_buffer(CreateBlackFrameBuffer(4, 4))
          .set_rtp_timestamp(300)
          .set_timestamp_us(0)
          .set_rotation(kVideoRotation_0)
          .build();
  recv_stream->InjectFrame(video_frame3);
  EXPECT_EQ(3, renderer.num_rendered_frames());
#endif
}

// This test verifies that when a new default stream is created for a new
// unsignaled SSRC, the new stream does not overwrite any old stream that had
// been the default receive stream before being properly signaled.
TEST_F(WebRtcVideoChannelTest,
       NewUnsignaledStreamDoesNotDestroyPreviouslyUnsignaledStream) {
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // No streams signaled and no packets received, so we should not have any
  // stream objects created yet.
  EXPECT_EQ(0u, fake_call_->GetVideoReceiveStreams().size());

  // Receive packet on an unsignaled SSRC.
  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(GetEngineCodec("VP8").id);
  rtp_packet.SetSsrc(kSsrcs3[0]);
  ReceivePacketAndAdvanceTime(rtp_packet);
  // Default receive stream should be created.
  ASSERT_EQ(1u, fake_call_->GetVideoReceiveStreams().size());
  FakeVideoReceiveStream* recv_stream0 =
      fake_call_->GetVideoReceiveStreams()[0];
  EXPECT_EQ(kSsrcs3[0], recv_stream0->GetConfig().rtp.remote_ssrc);

  // Signal the SSRC.
  EXPECT_TRUE(
      receive_channel_->AddRecvStream(StreamParams::CreateLegacy(kSsrcs3[0])));
  ASSERT_EQ(1u, fake_call_->GetVideoReceiveStreams().size());
  recv_stream0 = fake_call_->GetVideoReceiveStreams()[0];
  EXPECT_EQ(kSsrcs3[0], recv_stream0->GetConfig().rtp.remote_ssrc);

  // Receive packet on a different unsignaled SSRC.
  rtp_packet.SetSsrc(kSsrcs3[1]);
  ReceivePacketAndAdvanceTime(rtp_packet);
  // New default receive stream should be created, but old stream should remain.
  ASSERT_EQ(2u, fake_call_->GetVideoReceiveStreams().size());
  EXPECT_EQ(recv_stream0, fake_call_->GetVideoReceiveStreams()[0]);
  FakeVideoReceiveStream* recv_stream1 =
      fake_call_->GetVideoReceiveStreams()[1];
  EXPECT_EQ(kSsrcs3[1], recv_stream1->GetConfig().rtp.remote_ssrc);
}

TEST_F(WebRtcVideoChannelTest, CanSetMaxBitrateForExistingStream) {
  AddSendStream();

  FrameForwarder frame_forwarder;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, nullptr, &frame_forwarder));
  EXPECT_TRUE(send_channel_->SetSend(true));
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  int default_encoder_bitrate = GetMaxEncoderBitrate();
  EXPECT_GT(default_encoder_bitrate, 1000);

  // TODO(skvlad): Resolve the inconsistency between the interpretation
  // of the global bitrate limit for audio and video:
  // - Audio: max_bandwidth_bps = 0 - fail the operation,
  //          max_bandwidth_bps = -1 - remove the bandwidth limit
  // - Video: max_bandwidth_bps = 0 - remove the bandwidth limit,
  //          max_bandwidth_bps = -1 - remove the bandwidth limit

  SetAndExpectMaxBitrate(1000, std::nullopt, 1000);
  SetAndExpectMaxBitrate(1000, 800, 800);
  SetAndExpectMaxBitrate(600, 800, 600);
  SetAndExpectMaxBitrate(0, 800, 800);
  SetAndExpectMaxBitrate(0, std::nullopt, default_encoder_bitrate);
  // An encoding max bitrate of zero is a configured value, not an unset one,
  // so neither the global limit nor the default may raise it, see
  // https://w3c.github.io/webrtc-pc/#dom-rtcrtpencodingparameters-maxbitrate
  SetAndExpectMaxBitrate(1000, 0, 0);
  SetAndExpectMaxBitrate(0, 0, 0);

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest, CannotSetMaxBitrateForNonexistentStream) {
  RtpParameters nonexistent_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(0u, nonexistent_parameters.encodings.size());

  nonexistent_parameters.encodings.push_back(RtpEncodingParameters());
  EXPECT_FALSE(
      send_channel_->SetRtpSendParameters(last_ssrc_, nonexistent_parameters)
          .ok());
}

TEST_F(WebRtcVideoChannelTest,
       SetLowMaxBitrateOverwritesVideoStreamMinBitrate) {
  FakeVideoSendStream* stream = AddSendStream();

  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(1UL, parameters.encodings.size());
  EXPECT_FALSE(parameters.encodings[0].max_bitrate_bps.has_value());
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Note that this is testing the behavior of the FakeVideoSendStream, which
  // also calls to CreateEncoderStreams to get the VideoStreams, so essentially
  // we are just testing the behavior of
  // EncoderStreamFactory::CreateEncoderStreams.
  ASSERT_EQ(1UL, stream->GetVideoStreams().size());
  EXPECT_EQ(kDefaultMinVideoBitrateBps,
            stream->GetVideoStreams()[0].min_bitrate_bps);

  // Set a low max bitrate & check that VideoStream.min_bitrate_bps is limited
  // by this amount.
  parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  int low_max_bitrate_bps = kDefaultMinVideoBitrateBps - 1000;
  parameters.encodings[0].max_bitrate_bps = low_max_bitrate_bps;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  ASSERT_EQ(1UL, stream->GetVideoStreams().size());
  EXPECT_EQ(low_max_bitrate_bps, stream->GetVideoStreams()[0].min_bitrate_bps);
  EXPECT_EQ(low_max_bitrate_bps, stream->GetVideoStreams()[0].max_bitrate_bps);
}

TEST_F(WebRtcVideoChannelTest,
       SetHighMinBitrateOverwritesVideoStreamMaxBitrate) {
  FakeVideoSendStream* stream = AddSendStream();

  // Note that this is testing the behavior of the FakeVideoSendStream, which
  // also calls to CreateEncoderStreams to get the VideoStreams, so essentially
  // we are just testing the behavior of
  // EncoderStreamFactory::CreateEncoderStreams.
  ASSERT_EQ(1UL, stream->GetVideoStreams().size());
  int high_min_bitrate_bps = stream->GetVideoStreams()[0].max_bitrate_bps + 1;

  // Set a high min bitrate and check that max_bitrate_bps is adjusted up.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(1UL, parameters.encodings.size());
  parameters.encodings[0].min_bitrate_bps = high_min_bitrate_bps;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  ASSERT_EQ(1UL, stream->GetVideoStreams().size());
  EXPECT_EQ(high_min_bitrate_bps, stream->GetVideoStreams()[0].min_bitrate_bps);
  EXPECT_EQ(high_min_bitrate_bps, stream->GetVideoStreams()[0].max_bitrate_bps);
}

TEST_F(WebRtcVideoChannelTest,
       SetMinBitrateAboveMaxBitrateLimitAdjustsMinBitrateDown) {
  send_parameters_.max_bandwidth_bps = 99999;
  FakeVideoSendStream* stream = AddSendStream();
  ExpectSetMaxBitrate(send_parameters_.max_bandwidth_bps);
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters_));
  ASSERT_EQ(1UL, stream->GetVideoStreams().size());
  EXPECT_EQ(kDefaultMinVideoBitrateBps,
            stream->GetVideoStreams()[0].min_bitrate_bps);
  EXPECT_EQ(send_parameters_.max_bandwidth_bps,
            stream->GetVideoStreams()[0].max_bitrate_bps);

  // Set min bitrate above global max bitrate and check that min_bitrate_bps is
  // adjusted down.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(1UL, parameters.encodings.size());
  parameters.encodings[0].min_bitrate_bps = 99999 + 1;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
  ASSERT_EQ(1UL, stream->GetVideoStreams().size());
  EXPECT_EQ(send_parameters_.max_bandwidth_bps,
            stream->GetVideoStreams()[0].min_bitrate_bps);
  EXPECT_EQ(send_parameters_.max_bandwidth_bps,
            stream->GetVideoStreams()[0].max_bitrate_bps);
}

TEST_F(WebRtcVideoChannelTest, SetMaxFramerateOneStream) {
  FakeVideoSendStream* stream = AddSendStream();

  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(1UL, parameters.encodings.size());
  EXPECT_FALSE(parameters.encodings[0].max_framerate.has_value());
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Note that this is testing the behavior of the FakeVideoSendStream, which
  // also calls to CreateEncoderStreams to get the VideoStreams, so essentially
  // we are just testing the behavior of
  // EncoderStreamFactory::CreateEncoderStreams.
  ASSERT_EQ(1UL, stream->GetVideoStreams().size());
  EXPECT_EQ(kDefaultVideoMaxFramerate,
            stream->GetVideoStreams()[0].max_framerate);

  // Set max framerate and check that VideoStream.max_framerate is set.
  const int kNewMaxFramerate = kDefaultVideoMaxFramerate - 1;
  parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  parameters.encodings[0].max_framerate = kNewMaxFramerate;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  ASSERT_EQ(1UL, stream->GetVideoStreams().size());
  EXPECT_EQ(kNewMaxFramerate, stream->GetVideoStreams()[0].max_framerate);
}

TEST_F(WebRtcVideoChannelTest, SetNumTemporalLayersForSingleStream) {
  FakeVideoSendStream* stream = AddSendStream();

  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(1UL, parameters.encodings.size());
  EXPECT_FALSE(parameters.encodings[0].num_temporal_layers.has_value());
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Note that this is testing the behavior of the FakeVideoSendStream, which
  // also calls to CreateEncoderStreams to get the VideoStreams, so essentially
  // we are just testing the behavior of
  // EncoderStreamFactory::CreateEncoderStreams.
  ASSERT_EQ(1UL, stream->GetVideoStreams().size());
  EXPECT_FALSE(stream->GetVideoStreams()[0].num_temporal_layers.has_value());

  // Set temporal layers and check that VideoStream.num_temporal_layers is set.
  parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  parameters.encodings[0].num_temporal_layers = 2;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  ASSERT_EQ(1UL, stream->GetVideoStreams().size());
  EXPECT_EQ(2UL, stream->GetVideoStreams()[0].num_temporal_layers);
}

TEST_F(WebRtcVideoChannelTest,
       CannotSetRtpSendParametersWithIncorrectNumberOfEncodings) {
  AddSendStream();
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  // Two or more encodings should result in failure.
  parameters.encodings.push_back(RtpEncodingParameters());
  EXPECT_FALSE(
      send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
  // Zero encodings should also fail.
  parameters.encodings.clear();
  EXPECT_FALSE(
      send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
}

TEST_F(WebRtcVideoChannelTest,
       CannotSetSimulcastRtpSendParametersWithIncorrectNumberOfEncodings) {
  std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs3);
  StreamParams sp = CreateSimStreamParams("cname", ssrcs);
  AddSendStream(sp);

  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);

  // Additional encodings should result in failure.
  parameters.encodings.push_back(RtpEncodingParameters());
  EXPECT_FALSE(
      send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
  // Zero encodings should also fail.
  parameters.encodings.clear();
  EXPECT_FALSE(
      send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
}

// Changing the SSRC through RtpParameters is not allowed.
TEST_F(WebRtcVideoChannelTest, CannotSetSsrcInRtpSendParameters) {
  AddSendStream();
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  parameters.encodings[0].ssrc = 0xdeadbeef;
  EXPECT_FALSE(
      send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
}

// Tests that when RTCRtpEncodingParameters.bitrate_priority gets set to
// a value <= 0, setting the parameters returns false.
TEST_F(WebRtcVideoChannelTest, SetRtpSendParametersInvalidBitratePriority) {
  AddSendStream();
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(1UL, parameters.encodings.size());
  EXPECT_EQ(kDefaultBitratePriority, parameters.encodings[0].bitrate_priority);

  parameters.encodings[0].bitrate_priority = 0;
  EXPECT_FALSE(
      send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
  parameters.encodings[0].bitrate_priority = -2;
  EXPECT_FALSE(
      send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
}

// Tests when the the RTCRtpEncodingParameters.bitrate_priority gets set
// properly on the VideoChannel and propogates down to the video encoder.
TEST_F(WebRtcVideoChannelTest, SetRtpSendParametersPriorityOneStream) {
  AddSendStream();
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(1UL, parameters.encodings.size());
  EXPECT_EQ(kDefaultBitratePriority, parameters.encodings[0].bitrate_priority);

  // Change the value and set it on the VideoChannel.
  double new_bitrate_priority = 2.0;
  parameters.encodings[0].bitrate_priority = new_bitrate_priority;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Verify that the encoding parameters bitrate_priority is set for the
  // VideoChannel.
  parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(1UL, parameters.encodings.size());
  EXPECT_EQ(new_bitrate_priority, parameters.encodings[0].bitrate_priority);

  // Verify that the new value propagated down to the encoder.
  std::vector<FakeVideoSendStream*> video_send_streams =
      fake_call_->GetVideoSendStreams();
  EXPECT_EQ(1UL, video_send_streams.size());
  FakeVideoSendStream* video_send_stream = video_send_streams.front();
  // Check that the WebRtcVideoSendStream updated the VideoEncoderConfig
  // appropriately.
  EXPECT_EQ(new_bitrate_priority,
            video_send_stream->GetEncoderConfig().bitrate_priority);
  // Check that the vector of VideoStreams also was propagated correctly. Note
  // that this is testing the behavior of the FakeVideoSendStream, which mimics
  // the calls to CreateEncoderStreams to get the VideoStreams.
  EXPECT_EQ(std::optional<double>(new_bitrate_priority),
            video_send_stream->GetVideoStreams()[0].bitrate_priority);
}

// Tests that the RTCRtpEncodingParameters.bitrate_priority is set for the
// VideoChannel and the value propogates to the video encoder with all simulcast
// streams.
TEST_F(WebRtcVideoChannelTest, SetRtpSendParametersPrioritySimulcastStreams) {
  // Create the stream params with multiple ssrcs for simulcast.
  std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs3);
  StreamParams stream_params = CreateSimStreamParams("cname", ssrcs);
  AddSendStream(stream_params);
  uint32_t primary_ssrc = stream_params.first_ssrc();

  // Using the FrameForwarder, we manually send a full size
  // frame. This creates multiple VideoStreams for all simulcast layers when
  // reconfiguring, and allows us to test this behavior.
  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(primary_ssrc, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame(
      1920, 1080, VideoRotation::kVideoRotation_0, TimeDelta::Seconds(1) / 30));

  // Get and set the rtp encoding parameters.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(primary_ssrc);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  EXPECT_EQ(kDefaultBitratePriority, parameters.encodings[0].bitrate_priority);
  // Change the value and set it on the VideoChannel.
  double new_bitrate_priority = 2.0;
  parameters.encodings[0].bitrate_priority = new_bitrate_priority;
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(primary_ssrc, parameters).ok());

  // Verify that the encoding parameters priority is set on the VideoChannel.
  parameters = send_channel_->GetRtpSendParameters(primary_ssrc);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  EXPECT_EQ(new_bitrate_priority, parameters.encodings[0].bitrate_priority);

  // Verify that the new value propagated down to the encoder.
  std::vector<FakeVideoSendStream*> video_send_streams =
      fake_call_->GetVideoSendStreams();
  EXPECT_EQ(1UL, video_send_streams.size());
  FakeVideoSendStream* video_send_stream = video_send_streams.front();
  // Check that the WebRtcVideoSendStream updated the VideoEncoderConfig
  // appropriately.
  EXPECT_EQ(kNumSimulcastStreams,
            video_send_stream->GetEncoderConfig().number_of_streams);
  EXPECT_EQ(new_bitrate_priority,
            video_send_stream->GetEncoderConfig().bitrate_priority);
  // Check that the vector of VideoStreams also propagated correctly. The
  // FakeVideoSendStream calls CreateEncoderStreams, and we are testing that
  // these are created appropriately for the simulcast case.
  EXPECT_EQ(kNumSimulcastStreams, video_send_stream->GetVideoStreams().size());
  EXPECT_EQ(std::optional<double>(new_bitrate_priority),
            video_send_stream->GetVideoStreams()[0].bitrate_priority);
  // Since we are only setting bitrate priority per-sender, the other
  // VideoStreams should have a bitrate priority of 0.
  EXPECT_EQ(std::nullopt,
            video_send_stream->GetVideoStreams()[1].bitrate_priority);
  EXPECT_EQ(std::nullopt,
            video_send_stream->GetVideoStreams()[2].bitrate_priority);
  EXPECT_TRUE(send_channel_->SetVideoSend(primary_ssrc, nullptr, nullptr));
}

struct ScaleResolutionDownByTestParameters {
  std::string field_trials;
  Resolution resolution;
  std::vector<std::optional<double>> scale_resolution_down_by;
  std::vector<Resolution> expected_resolutions;
};

class WebRtcVideoChannelScaleResolutionDownByTest
    : public WebRtcVideoChannelTest,
      public ::testing::WithParamInterface<
          std::tuple<ScaleResolutionDownByTestParameters,
                     /*codec_name=*/std::string>> {};

TEST_P(WebRtcVideoChannelScaleResolutionDownByTest, ScaleResolutionDownBy) {
  ScaleResolutionDownByTestParameters test_params = std::get<0>(GetParam());
  std::string codec_name = std::get<1>(GetParam());
  field_trials_.Merge(CreateTestFieldTrials(test_params.field_trials));
  ResetTest();
  // Set up WebRtcVideoChannel for 3-layer simulcast.
  encoder_factory_->AddSupportedVideoCodecType(codec_name);
  VideoSenderParameters parameters;
  Codec codec = CreateVideoCodec(codec_name);
  // Codec ID does not matter, but must be valid.
  codec.id = 123;
  parameters.codecs.push_back(codec);
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));
  FakeVideoSendStream* stream = SetUpSimulcast(true, /*with_rtx=*/false);
  FrameForwarder frame_forwarder;
  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, /*options=*/nullptr,
                                          &frame_forwarder));
  send_channel_->SetSend(true);

  // Set `scale_resolution_down_by`'s.
  auto rtp_parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  ASSERT_EQ(rtp_parameters.encodings.size(), 3u);
  rtp_parameters.encodings[0].scale_resolution_down_by =
      test_params.scale_resolution_down_by[0];
  rtp_parameters.encodings[1].scale_resolution_down_by =
      test_params.scale_resolution_down_by[1];
  rtp_parameters.encodings[2].scale_resolution_down_by =
      test_params.scale_resolution_down_by[2];
  const auto result =
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters);
  ASSERT_TRUE(result.ok());

  // Use a capture resolution whose width and height are not divisible by 2^3.
  // (See field trial set at the top of the test.)
  FakeFrameSource frame_source(
      test_params.resolution.width, test_params.resolution.height,
      TimeDelta::Seconds(1) / 30, env_.clock().CurrentTime());
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());

  // Ensure the scaling is correct.
  const auto streams = stream->GetVideoStreams();
  ASSERT_EQ(streams.size(), 3u);
  EXPECT_EQ(static_cast<int>(streams[0].width),
            test_params.expected_resolutions[0].width);
  EXPECT_EQ(static_cast<int>(streams[0].height),
            test_params.expected_resolutions[0].height);
  EXPECT_EQ(static_cast<int>(streams[1].width),
            test_params.expected_resolutions[1].width);
  EXPECT_EQ(static_cast<int>(streams[1].height),
            test_params.expected_resolutions[1].height);
  EXPECT_EQ(static_cast<int>(streams[2].width),
            test_params.expected_resolutions[2].width);
  EXPECT_EQ(static_cast<int>(streams[2].height),
            test_params.expected_resolutions[2].height);

  // Tear down.
  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

INSTANTIATE_TEST_SUITE_P(
    All,
    WebRtcVideoChannelScaleResolutionDownByTest,
    Combine(Values(
                // Try layers in natural order (smallest to largest).
                ScaleResolutionDownByTestParameters{
                    .resolution = {.width = 1280, .height = 720},
                    .scale_resolution_down_by = {4, 2, 1},
                    .expected_resolutions = {{.width = 320, .height = 180},
                                             {.width = 640, .height = 360},
                                             {.width = 1280, .height = 720}}},
                // Try layers in reverse natural order (largest to smallest).
                ScaleResolutionDownByTestParameters{
                    .resolution = {.width = 1280, .height = 720},
                    .scale_resolution_down_by = {1, 2, 4},
                    .expected_resolutions = {{.width = 1280, .height = 720},
                                             {.width = 640, .height = 360},
                                             {.width = 320, .height = 180}}},
                // Try layers in mixed order.
                ScaleResolutionDownByTestParameters{
                    .resolution = {.width = 1280, .height = 720},
                    .scale_resolution_down_by = {10, 2, 4},
                    .expected_resolutions = {{.width = 128, .height = 72},
                                             {.width = 640, .height = 360},
                                             {.width = 320, .height = 180}}},
                // Try with a missing scale setting, defaults to 1.0 if any
                // other is set.
                ScaleResolutionDownByTestParameters{
                    .resolution = {.width = 1280, .height = 720},
                    .scale_resolution_down_by = {1, std::nullopt, 4},
                    .expected_resolutions = {{.width = 1280, .height = 720},
                                             {.width = 1280, .height = 720},
                                             {.width = 320, .height = 180}}},
                // Odd resolution. Request alignment by 8 to get the resolution
                // of the smallest layer multiple by 2.
                ScaleResolutionDownByTestParameters{
                    .field_trials =
                        "WebRTC-NormalizeSimulcastResolution/Enabled-3/",
                    .resolution = {.width = 2007, .height = 1207},
                    .scale_resolution_down_by = {1, 2, 4},
                    .expected_resolutions = {{.width = 2000, .height = 1200},
                                             {.width = 1000, .height = 600},
                                             {.width = 500, .height = 300}}}),
            Values(kVp8CodecName, kH264CodecName)));

TEST_F(WebRtcVideoChannelTest, GetAndSetRtpSendParametersMaxFramerate) {
  SetUpSimulcast(true, /*with_rtx=*/false);

  // Get and set the rtp encoding parameters.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  for (const auto& encoding : parameters.encodings) {
    EXPECT_FALSE(encoding.max_framerate);
  }

  // Change the value and set it on the VideoChannel.
  parameters.encodings[0].max_framerate = 10;
  parameters.encodings[1].max_framerate = 20;
  parameters.encodings[2].max_framerate = 25;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Verify that the bitrates are set on the VideoChannel.
  parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  EXPECT_EQ(10, parameters.encodings[0].max_framerate);
  EXPECT_EQ(20, parameters.encodings[1].max_framerate);
  EXPECT_EQ(25, parameters.encodings[2].max_framerate);
}

TEST_F(WebRtcVideoChannelTest,
       SetRtpSendParametersNumTemporalLayersFailsForInvalidRange) {
  SetUpSimulcast(true, /*with_rtx=*/false);

  // Get and set the rtp encoding parameters.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());

  // Num temporal layers should be in the range [1, kMaxTemporalStreams].
  parameters.encodings[0].num_temporal_layers = 0;
  EXPECT_EQ(RTCErrorType::INVALID_RANGE,
            send_channel_->SetRtpSendParameters(last_ssrc_, parameters).type());
  parameters.encodings[0].num_temporal_layers = kMaxTemporalStreams + 1;
  EXPECT_EQ(RTCErrorType::INVALID_RANGE,
            send_channel_->SetRtpSendParameters(last_ssrc_, parameters).type());
}

TEST_F(WebRtcVideoChannelTest, GetAndSetRtpSendParametersNumTemporalLayers) {
  SetUpSimulcast(true, /*with_rtx=*/false);

  // Get and set the rtp encoding parameters.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  for (const auto& encoding : parameters.encodings)
    EXPECT_FALSE(encoding.num_temporal_layers);

  // Change the value and set it on the VideoChannel.
  parameters.encodings[0].num_temporal_layers = 3;
  parameters.encodings[1].num_temporal_layers = 3;
  parameters.encodings[2].num_temporal_layers = 3;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Verify that the number of temporal layers are set on the VideoChannel.
  parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  EXPECT_EQ(3, parameters.encodings[0].num_temporal_layers);
  EXPECT_EQ(3, parameters.encodings[1].num_temporal_layers);
  EXPECT_EQ(3, parameters.encodings[2].num_temporal_layers);
}

TEST_F(WebRtcVideoChannelTest, NumTemporalLayersPropagatedToEncoder) {
  FakeVideoSendStream* stream = SetUpSimulcast(true, /*with_rtx=*/false);

  // Send a full size frame so all simulcast layers are used when reconfiguring.
  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  // Get and set the rtp encoding parameters.
  // Change the value and set it on the VideoChannel.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  parameters.encodings[0].num_temporal_layers = 3;
  parameters.encodings[1].num_temporal_layers = 2;
  parameters.encodings[2].num_temporal_layers = 1;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Verify that the new value is propagated down to the encoder.
  // Check that WebRtcVideoSendStream updates VideoEncoderConfig correctly.
  EXPECT_EQ(2, stream->num_encoder_reconfigurations());
  VideoEncoderConfig encoder_config = stream->GetEncoderConfig().Copy();
  EXPECT_EQ(kNumSimulcastStreams, encoder_config.number_of_streams);
  EXPECT_EQ(kNumSimulcastStreams, encoder_config.simulcast_layers.size());
  EXPECT_EQ(3UL, encoder_config.simulcast_layers[0].num_temporal_layers);
  EXPECT_EQ(2UL, encoder_config.simulcast_layers[1].num_temporal_layers);
  EXPECT_EQ(1UL, encoder_config.simulcast_layers[2].num_temporal_layers);

  // FakeVideoSendStream calls CreateEncoderStreams, test that the vector of
  // VideoStreams are created appropriately for the simulcast case.
  EXPECT_EQ(kNumSimulcastStreams, stream->GetVideoStreams().size());
  EXPECT_EQ(3UL, stream->GetVideoStreams()[0].num_temporal_layers);
  EXPECT_EQ(2UL, stream->GetVideoStreams()[1].num_temporal_layers);
  EXPECT_EQ(1UL, stream->GetVideoStreams()[2].num_temporal_layers);

  // No parameter changed, encoder should not be reconfigured.
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
  EXPECT_EQ(2, stream->num_encoder_reconfigurations());

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest,
       DefaultValuePropagatedToEncoderForUnsetNumTemporalLayers) {
  const size_t kDefaultNumTemporalLayers = 3;
  FakeVideoSendStream* stream = SetUpSimulcast(true, /*with_rtx=*/false);

  // Send a full size frame so all simulcast layers are used when reconfiguring.
  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  // Change rtp encoding parameters.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  parameters.encodings[0].num_temporal_layers = 2;
  parameters.encodings[2].num_temporal_layers = 1;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Verify that no value is propagated down to the encoder.
  VideoEncoderConfig encoder_config = stream->GetEncoderConfig().Copy();
  EXPECT_EQ(kNumSimulcastStreams, encoder_config.number_of_streams);
  EXPECT_EQ(kNumSimulcastStreams, encoder_config.simulcast_layers.size());
  EXPECT_EQ(2UL, encoder_config.simulcast_layers[0].num_temporal_layers);
  EXPECT_FALSE(encoder_config.simulcast_layers[1].num_temporal_layers);
  EXPECT_EQ(1UL, encoder_config.simulcast_layers[2].num_temporal_layers);

  // FakeVideoSendStream calls CreateEncoderStreams, test that the vector of
  // VideoStreams are created appropriately for the simulcast case.
  EXPECT_EQ(kNumSimulcastStreams, stream->GetVideoStreams().size());
  EXPECT_EQ(2UL, stream->GetVideoStreams()[0].num_temporal_layers);
  EXPECT_EQ(kDefaultNumTemporalLayers,
            stream->GetVideoStreams()[1].num_temporal_layers);
  EXPECT_EQ(1UL, stream->GetVideoStreams()[2].num_temporal_layers);

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest,
       DefaultValuePropagatedToEncoderForUnsetFramerate) {
  const std::vector<VideoStream> kDefault = GetSimulcastBitrates720p();
  FakeVideoSendStream* stream = SetUpSimulcast(true, /*with_rtx=*/false);

  // Send a full size frame so all simulcast layers are used when reconfiguring.
  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  // Get and set the rtp encoding parameters.
  // Change the value and set it on the VideoChannel.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  parameters.encodings[0].max_framerate = 15;
  parameters.encodings[2].max_framerate = 20;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Verify that the new value propagated down to the encoder.
  // Check that WebRtcVideoSendStream updates VideoEncoderConfig correctly.
  VideoEncoderConfig encoder_config = stream->GetEncoderConfig().Copy();
  EXPECT_EQ(kNumSimulcastStreams, encoder_config.number_of_streams);
  EXPECT_EQ(kNumSimulcastStreams, encoder_config.simulcast_layers.size());
  EXPECT_EQ(15, encoder_config.simulcast_layers[0].max_framerate);
  EXPECT_EQ(-1, encoder_config.simulcast_layers[1].max_framerate);
  EXPECT_EQ(20, encoder_config.simulcast_layers[2].max_framerate);

  // FakeVideoSendStream calls CreateEncoderStreams, test that the vector of
  // VideoStreams are created appropriately for the simulcast case.
  // The maximum `max_framerate` is used, kDefaultVideoMaxFramerate: 60.
  EXPECT_EQ(kNumSimulcastStreams, stream->GetVideoStreams().size());
  EXPECT_EQ(15, stream->GetVideoStreams()[0].max_framerate);
  EXPECT_EQ(kDefaultVideoMaxFramerate,
            stream->GetVideoStreams()[1].max_framerate);
  EXPECT_EQ(20, stream->GetVideoStreams()[2].max_framerate);

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest, FallbackForUnsetOrUnsupportedScalabilityMode) {
  const absl::InlinedVector<ScalabilityMode, kScalabilityModeCount>
      kSupportedModes = {ScalabilityMode::kL1T1, ScalabilityMode::kL1T2,
                         ScalabilityMode::kL1T3};

  encoder_factory_->AddSupportedVideoCodec(
      SdpVideoFormat("VP8", CodecParameterMap(), kSupportedModes));

  FakeVideoSendStream* stream = SetUpSimulcast(true, /*with_rtx=*/false);

  // Send a full size frame so all simulcast layers are used when reconfiguring.
  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  // Set scalability mode.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  parameters.encodings[0].scalability_mode = std::nullopt;
  parameters.encodings[1].scalability_mode = "L1T3";  // Supported.
  parameters.encodings[2].scalability_mode = "L3T3";  // Unsupported.
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Verify that the new value is propagated down to the encoder.
  // Check that WebRtcVideoSendStream updates VideoEncoderConfig correctly.
  const std::optional<ScalabilityMode> kDefaultScalabilityMode =
      ScalabilityModeFromString(kDefaultScalabilityModeStr);
  EXPECT_EQ(2, stream->num_encoder_reconfigurations());
  VideoEncoderConfig encoder_config = stream->GetEncoderConfig().Copy();
  EXPECT_EQ(kNumSimulcastStreams, encoder_config.number_of_streams);
  EXPECT_THAT(
      encoder_config.simulcast_layers,
      ElementsAre(
          Field(&VideoStream::scalability_mode, kDefaultScalabilityMode),
          Field(&VideoStream::scalability_mode, ScalabilityMode::kL1T3),
          Field(&VideoStream::scalability_mode, kDefaultScalabilityMode)));

  // FakeVideoSendStream calls CreateEncoderStreams, test that the vector of
  // VideoStreams are created appropriately for the simulcast case.
  EXPECT_THAT(
      stream->GetVideoStreams(),
      ElementsAre(
          Field(&VideoStream::scalability_mode, kDefaultScalabilityMode),
          Field(&VideoStream::scalability_mode, ScalabilityMode::kL1T3),
          Field(&VideoStream::scalability_mode, kDefaultScalabilityMode)));

  // GetParameters.
  parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_THAT(
      parameters.encodings,
      ElementsAre(Field(&RtpEncodingParameters::scalability_mode,
                        kDefaultScalabilityModeStr),
                  Field(&RtpEncodingParameters::scalability_mode, "L1T3"),
                  Field(&RtpEncodingParameters::scalability_mode,
                        kDefaultScalabilityModeStr)));

  // No parameters changed, encoder should not be reconfigured.
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
  EXPECT_EQ(2, stream->num_encoder_reconfigurations());

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

#ifdef RTC_ENABLE_H265
TEST_F(
    WebRtcVideoChannelTest,
    NoLayeringValueUsedIfModeIsUnsetOrUnsupportedByH265AndDefaultUnsupported) {
  const absl::InlinedVector<ScalabilityMode, kScalabilityModeCount>
      kSupportedModes = {ScalabilityMode::kL1T1, ScalabilityMode::kL1T3};

  encoder_factory_->AddSupportedVideoCodec(
      SdpVideoFormat("H265", CodecParameterMap(), kSupportedModes));
  VideoSenderParameters send_parameters;
  send_parameters.codecs.push_back(GetEngineCodec("H265"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters));

  FakeVideoSendStream* stream = SetUpSimulcast(true, /*with_rtx=*/false);

  // Send a full size frame so all simulcast layers are used when reconfiguring.
  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  // Set scalability mode.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  parameters.encodings[0].scalability_mode = std::nullopt;
  parameters.encodings[1].scalability_mode = "L1T3";  // Supported.
  parameters.encodings[2].scalability_mode = "L3T3";  // Unsupported.
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Verify that the new value is propagated down to the encoder.
  // Check that WebRtcVideoSendStream updates VideoEncoderConfig correctly.
  const std::optional<ScalabilityMode> kDefaultScalabilityMode =
      ScalabilityModeFromString(kNoLayeringScalabilityModeStr);
  EXPECT_EQ(2, stream->num_encoder_reconfigurations());
  VideoEncoderConfig encoder_config = stream->GetEncoderConfig().Copy();
  EXPECT_EQ(kNumSimulcastStreams, encoder_config.number_of_streams);
  EXPECT_THAT(
      encoder_config.simulcast_layers,
      ElementsAre(
          Field(&VideoStream::scalability_mode, kDefaultScalabilityMode),
          Field(&VideoStream::scalability_mode, ScalabilityMode::kL1T3),
          Field(&VideoStream::scalability_mode, kDefaultScalabilityMode)));

  // FakeVideoSendStream calls CreateEncoderStreams, test that the vector of
  // VideoStreams are created appropriately for the simulcast case.
  EXPECT_THAT(
      stream->GetVideoStreams(),
      ElementsAre(
          Field(&VideoStream::scalability_mode, kDefaultScalabilityMode),
          Field(&VideoStream::scalability_mode, ScalabilityMode::kL1T3),
          Field(&VideoStream::scalability_mode, kDefaultScalabilityMode)));

  // GetParameters.
  parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_THAT(
      parameters.encodings,
      ElementsAre(Field(&RtpEncodingParameters::scalability_mode,
                        kNoLayeringScalabilityModeStr),
                  Field(&RtpEncodingParameters::scalability_mode, "L1T3"),
                  Field(&RtpEncodingParameters::scalability_mode,
                        kNoLayeringScalabilityModeStr)));

  // No parameters changed, encoder should not be reconfigured.
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
  EXPECT_EQ(2, stream->num_encoder_reconfigurations());

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest,
       SetRtpParametersForH265ShouldSucceedIgnoreLowerLevelId) {
  encoder_factory_->AddSupportedVideoCodec(
      SdpVideoFormat("H265",
                     {{"profile-id", "1"},
                      {"tier-flag", "0"},
                      {"level-id", "156"},
                      {"tx-mode", "SRST"}},
                     {ScalabilityMode::kL1T1}));
  VideoSenderParameters send_parameters;
  send_parameters.codecs.push_back(GetEngineCodec("H265"));
  for (auto& codec : send_parameters.codecs) {
    if (absl::EqualsIgnoreCase(codec.name, "H265")) {
      codec.params["level-id"] = "156";
    }
  }

  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters));
  FakeVideoSendStream* stream = AddSendStream();
  ASSERT_TRUE(stream);

  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);

  RtpCodecParameters matched_codec;
  for (const auto& codec : parameters.codecs) {
    if (absl::EqualsIgnoreCase(codec.name, "H265")) {
      EXPECT_EQ(codec.parameters.at("level-id"), "156");
      matched_codec = codec;
    }
  }

  FakeVideoSendStream* send_stream = fake_call_->GetVideoSendStreams().front();
  ASSERT_TRUE(send_stream);
  VideoEncoderConfig encoder_config = send_stream->GetEncoderConfig().Copy();
  EXPECT_EQ(encoder_config.video_format.parameters.at("level-id"), "156");

  // Set the level-id parameter to lower than the negotiated codec level-id.
  EXPECT_EQ(1u, parameters.encodings.size());
  matched_codec.parameters["level-id"] = "120";
  parameters.encodings[0].codec = matched_codec;

  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
  RtpParameters parameters2 = send_channel_->GetRtpSendParameters(last_ssrc_);

  for (const auto& codec : parameters2.codecs) {
    if (absl::EqualsIgnoreCase(codec.name, "H265")) {
      EXPECT_EQ(codec.parameters.at("level-id"), "156");
    }
  }

  FakeVideoSendStream* send_stream2 = fake_call_->GetVideoSendStreams().front();
  ASSERT_TRUE(send_stream2);
  VideoEncoderConfig encoder_config2 = send_stream2->GetEncoderConfig().Copy();
  EXPECT_EQ(encoder_config2.video_format.parameters.at("level-id"), "156");

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest,
       SetRtpParametersForH265WithSameLevelIdShouldSucceed) {
  encoder_factory_->AddSupportedVideoCodec(
      SdpVideoFormat("H265",
                     {{"profile-id", "1"},
                      {"tier-flag", "0"},
                      {"level-id", "156"},
                      {"tx-mode", "SRST"}},
                     {ScalabilityMode::kL1T1}));
  VideoSenderParameters send_parameters;
  send_parameters.codecs.push_back(GetEngineCodec("H265"));
  for (auto& codec : send_parameters.codecs) {
    if (absl::EqualsIgnoreCase(codec.name, "H265")) {
      codec.params["level-id"] = "156";
    }
  }

  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters));
  FakeVideoSendStream* stream = AddSendStream();
  ASSERT_TRUE(stream);

  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);

  RtpCodecParameters matched_codec;
  for (const auto& codec : parameters.codecs) {
    if (absl::EqualsIgnoreCase(codec.name, "H265")) {
      EXPECT_EQ(codec.parameters.at("level-id"), "156");
      matched_codec = codec;
    }
  }

  FakeVideoSendStream* send_stream = fake_call_->GetVideoSendStreams().front();
  ASSERT_TRUE(send_stream);
  VideoEncoderConfig encoder_config = send_stream->GetEncoderConfig().Copy();
  EXPECT_EQ(encoder_config.video_format.parameters.at("level-id"), "156");

  // Set the level-id parameter to the same as the negotiated codec level-id.
  EXPECT_EQ(1u, parameters.encodings.size());
  matched_codec.parameters["level-id"] = "156";
  parameters.encodings[0].codec = matched_codec;

  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  RtpParameters parameters2 = send_channel_->GetRtpSendParameters(last_ssrc_);

  for (const auto& codec : parameters2.codecs) {
    if (absl::EqualsIgnoreCase(codec.name, "H265")) {
      EXPECT_EQ(codec.parameters.at("level-id"), "156");
      matched_codec = codec;
    }
  }

  FakeVideoSendStream* send_stream2 = fake_call_->GetVideoSendStreams().front();
  ASSERT_TRUE(send_stream2);
  VideoEncoderConfig encoder_config2 = send_stream2->GetEncoderConfig().Copy();
  EXPECT_EQ(encoder_config2.video_format.parameters.at("level-id"), "156");

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest,
       SetRtpParametersForH265ShouldSucceedIgnoreHigherLevelId) {
  encoder_factory_->AddSupportedVideoCodec(
      SdpVideoFormat("H265",
                     {{"profile-id", "1"},
                      {"tier-flag", "0"},
                      {"level-id", "156"},
                      {"tx-mode", "SRST"}},
                     {ScalabilityMode::kL1T1}));
  VideoSenderParameters send_parameters;
  send_parameters.codecs.push_back(GetEngineCodec("H265"));
  for (auto& codec : send_parameters.codecs) {
    if (absl::EqualsIgnoreCase(codec.name, "H265")) {
      codec.params["level-id"] = "156";
    }
  }

  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters));
  FakeVideoSendStream* stream = AddSendStream();
  ASSERT_TRUE(stream);

  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);

  RtpCodecParameters matched_codec;
  for (const auto& codec : parameters.codecs) {
    if (absl::EqualsIgnoreCase(codec.name, "H265")) {
      EXPECT_EQ(codec.parameters.at("level-id"), "156");
      matched_codec = codec;
    }
  }

  FakeVideoSendStream* send_stream = fake_call_->GetVideoSendStreams().front();
  ASSERT_TRUE(send_stream);
  VideoEncoderConfig encoder_config = send_stream->GetEncoderConfig().Copy();
  EXPECT_EQ(encoder_config.video_format.parameters.at("level-id"), "156");

  // Set the level-id parameter to higher than the negotiated codec level-id.
  EXPECT_EQ(1u, parameters.encodings.size());
  matched_codec.parameters["level-id"] = "180";
  parameters.encodings[0].codec = matched_codec;

  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  RtpParameters parameters2 = send_channel_->GetRtpSendParameters(last_ssrc_);

  for (const auto& codec : parameters2.codecs) {
    if (absl::EqualsIgnoreCase(codec.name, "H265")) {
      EXPECT_EQ(codec.parameters.at("level-id"), "156");
    }
  }
  FakeVideoSendStream* send_stream2 = fake_call_->GetVideoSendStreams().front();
  ASSERT_TRUE(send_stream2);
  VideoEncoderConfig encoder_config2 = send_stream2->GetEncoderConfig().Copy();
  EXPECT_EQ(encoder_config2.video_format.parameters.at("level-id"), "156");

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}
#endif

TEST_F(WebRtcVideoChannelTest,
       DefaultValueUsedIfScalabilityModeIsUnsupportedByCodec) {
  encoder_factory_->AddSupportedVideoCodec(
      SdpVideoFormat("VP8", CodecParameterMap(),
                     {ScalabilityMode::kL1T1, ScalabilityMode::kL1T2}));
  encoder_factory_->AddSupportedVideoCodec(
      SdpVideoFormat("VP9", CodecParameterMap(),
                     {ScalabilityMode::kL1T2, ScalabilityMode::kL3T3}));

  VideoSenderParameters send_parameters;
  send_parameters.codecs.push_back(GetEngineCodec("VP9"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(send_parameters));

  FakeVideoSendStream* stream = SetUpSimulcast(true, /*with_rtx=*/false);

  // Send a full size frame so all simulcast layers are used when reconfiguring.
  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  // Set scalability mode.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  parameters.encodings[0].scalability_mode = "L3T3";
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Verify that the new value is propagated down to the encoder.
  // Check that WebRtcVideoSendStream updates VideoEncoderConfig correctly.
  const std::optional<ScalabilityMode> kDefaultScalabilityMode =
      ScalabilityModeFromString(kDefaultScalabilityModeStr);
  EXPECT_EQ(2, stream->num_encoder_reconfigurations());
  VideoEncoderConfig encoder_config = stream->GetEncoderConfig().Copy();
  EXPECT_EQ(1u, encoder_config.number_of_streams);
  EXPECT_THAT(
      encoder_config.simulcast_layers,
      ElementsAre(
          Field(&VideoStream::scalability_mode, ScalabilityMode::kL3T3),
          Field(&VideoStream::scalability_mode, kDefaultScalabilityMode),
          Field(&VideoStream::scalability_mode, kDefaultScalabilityMode)));

  // FakeVideoSendStream calls CreateEncoderStreams, test that the vector of
  // VideoStreams are created appropriately for the simulcast case.
  EXPECT_THAT(stream->GetVideoStreams(),
              ElementsAre(Field(&VideoStream::scalability_mode,
                                ScalabilityMode::kL3T3)));

  // GetParameters.
  parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_THAT(
      parameters.encodings,
      ElementsAre(Field(&RtpEncodingParameters::scalability_mode, "L3T3"),
                  Field(&RtpEncodingParameters::scalability_mode,
                        kDefaultScalabilityModeStr),
                  Field(&RtpEncodingParameters::scalability_mode,
                        kDefaultScalabilityModeStr)));

  // Change codec to VP8.
  VideoSenderParameters vp8_parameters;
  vp8_parameters.codecs.push_back(GetEngineCodec("VP8"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(vp8_parameters));
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  // The stream should be recreated due to codec change.
  std::vector<FakeVideoSendStream*> new_streams = GetFakeSendStreams();
  EXPECT_EQ(1u, new_streams.size());
  EXPECT_EQ(2, fake_call_->GetNumCreatedSendStreams());

  // Verify fallback to default value triggered (L3T3 is not supported).
  EXPECT_THAT(
      new_streams[0]->GetVideoStreams(),
      ElementsAre(
          Field(&VideoStream::scalability_mode, kDefaultScalabilityMode),
          Field(&VideoStream::scalability_mode, kDefaultScalabilityMode),
          Field(&VideoStream::scalability_mode, kDefaultScalabilityMode)));

  parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_THAT(parameters.encodings,
              ElementsAre(Field(&RtpEncodingParameters::scalability_mode,
                                kDefaultScalabilityModeStr),
                          Field(&RtpEncodingParameters::scalability_mode,
                                kDefaultScalabilityModeStr),
                          Field(&RtpEncodingParameters::scalability_mode,
                                kDefaultScalabilityModeStr)));

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest, GetAndSetRtpSendParametersMinAndMaxBitrate) {
  SetUpSimulcast(true, /*with_rtx=*/false);

  // Get and set the rtp encoding parameters.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  for (const auto& encoding : parameters.encodings) {
    EXPECT_FALSE(encoding.min_bitrate_bps);
    EXPECT_FALSE(encoding.max_bitrate_bps);
  }

  // Change the value and set it on the VideoChannel.
  parameters.encodings[0].min_bitrate_bps = 100000;
  parameters.encodings[0].max_bitrate_bps = 200000;
  parameters.encodings[1].min_bitrate_bps = 300000;
  parameters.encodings[1].max_bitrate_bps = 400000;
  parameters.encodings[2].min_bitrate_bps = 500000;
  parameters.encodings[2].max_bitrate_bps = 600000;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Verify that the bitrates are set on the VideoChannel.
  parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  EXPECT_EQ(100000, parameters.encodings[0].min_bitrate_bps);
  EXPECT_EQ(200000, parameters.encodings[0].max_bitrate_bps);
  EXPECT_EQ(300000, parameters.encodings[1].min_bitrate_bps);
  EXPECT_EQ(400000, parameters.encodings[1].max_bitrate_bps);
  EXPECT_EQ(500000, parameters.encodings[2].min_bitrate_bps);
  EXPECT_EQ(600000, parameters.encodings[2].max_bitrate_bps);
}

TEST_F(WebRtcVideoChannelTest, SetRtpSendParametersFailsWithIncorrectBitrate) {
  SetUpSimulcast(true, /*with_rtx=*/false);

  // Get and set the rtp encoding parameters.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());

  // Max bitrate lower than min bitrate should fail.
  parameters.encodings[2].min_bitrate_bps = 100000;
  parameters.encodings[2].max_bitrate_bps = 100000 - 1;
  EXPECT_EQ(RTCErrorType::INVALID_RANGE,
            send_channel_->SetRtpSendParameters(last_ssrc_, parameters).type());
}

// Test that min and max bitrate values set via RtpParameters are correctly
// propagated to the underlying encoder, and that the target is set to 3/4 of
// the maximum (3/4 was chosen because it's similar to the simulcast defaults
// that are used if no min/max are specified).
TEST_F(WebRtcVideoChannelTest, MinAndMaxSimulcastBitratePropagatedToEncoder) {
  FakeVideoSendStream* stream = SetUpSimulcast(true, /*with_rtx=*/false);

  // Send a full size frame so all simulcast layers are used when reconfiguring.
  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  // Get and set the rtp encoding parameters.
  // Change the value and set it on the VideoChannel.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  parameters.encodings[0].min_bitrate_bps = 100000;
  parameters.encodings[0].max_bitrate_bps = 200000;
  parameters.encodings[1].min_bitrate_bps = 300000;
  parameters.encodings[1].max_bitrate_bps = 400000;
  parameters.encodings[2].min_bitrate_bps = 500000;
  parameters.encodings[2].max_bitrate_bps = 600000;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Verify that the new value propagated down to the encoder.
  // Check that WebRtcVideoSendStream updates VideoEncoderConfig correctly.
  EXPECT_EQ(2, stream->num_encoder_reconfigurations());
  VideoEncoderConfig encoder_config = stream->GetEncoderConfig().Copy();
  EXPECT_EQ(kNumSimulcastStreams, encoder_config.number_of_streams);
  EXPECT_EQ(kNumSimulcastStreams, encoder_config.simulcast_layers.size());
  EXPECT_EQ(100000, encoder_config.simulcast_layers[0].min_bitrate_bps);
  EXPECT_EQ(200000, encoder_config.simulcast_layers[0].max_bitrate_bps);
  EXPECT_EQ(300000, encoder_config.simulcast_layers[1].min_bitrate_bps);
  EXPECT_EQ(400000, encoder_config.simulcast_layers[1].max_bitrate_bps);
  EXPECT_EQ(500000, encoder_config.simulcast_layers[2].min_bitrate_bps);
  EXPECT_EQ(600000, encoder_config.simulcast_layers[2].max_bitrate_bps);

  // FakeVideoSendStream calls CreateEncoderStreams, test that the vector of
  // VideoStreams are created appropriately for the simulcast case.
  EXPECT_EQ(kNumSimulcastStreams, stream->GetVideoStreams().size());
  // Target bitrate: 200000 * 3 / 4 = 150000.
  EXPECT_EQ(100000, stream->GetVideoStreams()[0].min_bitrate_bps);
  EXPECT_EQ(150000, stream->GetVideoStreams()[0].target_bitrate_bps);
  EXPECT_EQ(200000, stream->GetVideoStreams()[0].max_bitrate_bps);
  // Target bitrate: 400000 * 3 / 4 = 300000.
  EXPECT_EQ(300000, stream->GetVideoStreams()[1].min_bitrate_bps);
  EXPECT_EQ(300000, stream->GetVideoStreams()[1].target_bitrate_bps);
  EXPECT_EQ(400000, stream->GetVideoStreams()[1].max_bitrate_bps);
  // Target bitrate: 600000 * 3 / 4 = 450000, less than min -> max.
  EXPECT_EQ(500000, stream->GetVideoStreams()[2].min_bitrate_bps);
  EXPECT_EQ(600000, stream->GetVideoStreams()[2].target_bitrate_bps);
  EXPECT_EQ(600000, stream->GetVideoStreams()[2].max_bitrate_bps);

  // No parameter changed, encoder should not be reconfigured.
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());
  EXPECT_EQ(2, stream->num_encoder_reconfigurations());

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

// Test to only specify the min or max bitrate value for a layer via
// RtpParameters. The unspecified min/max and target value should be set to the
// simulcast default that is used if no min/max are specified.
TEST_F(WebRtcVideoChannelTest, MinOrMaxSimulcastBitratePropagatedToEncoder) {
  const std::vector<VideoStream> kDefault = GetSimulcastBitrates720p();
  FakeVideoSendStream* stream = SetUpSimulcast(true, /*with_rtx=*/false);

  // Send a full size frame so all simulcast layers are used when reconfiguring.
  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  // Get and set the rtp encoding parameters.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());

  // Change the value and set it on the VideoChannel.
  // Layer 0: only configure min bitrate.
  const int kMinBpsLayer0 = kDefault[0].min_bitrate_bps + 1;
  parameters.encodings[0].min_bitrate_bps = kMinBpsLayer0;
  // Layer 1: only configure max bitrate.
  const int kMaxBpsLayer1 = kDefault[1].max_bitrate_bps - 1;
  parameters.encodings[1].max_bitrate_bps = kMaxBpsLayer1;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Verify that the new value propagated down to the encoder.
  // Check that WebRtcVideoSendStream updates VideoEncoderConfig correctly.
  VideoEncoderConfig encoder_config = stream->GetEncoderConfig().Copy();
  EXPECT_EQ(kNumSimulcastStreams, encoder_config.number_of_streams);
  EXPECT_EQ(kNumSimulcastStreams, encoder_config.simulcast_layers.size());
  EXPECT_EQ(kMinBpsLayer0, encoder_config.simulcast_layers[0].min_bitrate_bps);
  EXPECT_EQ(-1, encoder_config.simulcast_layers[0].max_bitrate_bps);
  EXPECT_EQ(-1, encoder_config.simulcast_layers[1].min_bitrate_bps);
  EXPECT_EQ(kMaxBpsLayer1, encoder_config.simulcast_layers[1].max_bitrate_bps);
  EXPECT_EQ(-1, encoder_config.simulcast_layers[2].min_bitrate_bps);
  EXPECT_EQ(-1, encoder_config.simulcast_layers[2].max_bitrate_bps);

  // FakeVideoSendStream calls CreateEncoderStreams, test that the vector of
  // VideoStreams are created appropriately for the simulcast case.
  EXPECT_EQ(kNumSimulcastStreams, stream->GetVideoStreams().size());
  // Layer 0: min configured bitrate should overwrite min default.
  EXPECT_EQ(kMinBpsLayer0, stream->GetVideoStreams()[0].min_bitrate_bps);
  EXPECT_EQ(kDefault[0].target_bitrate_bps,
            stream->GetVideoStreams()[0].target_bitrate_bps);
  EXPECT_EQ(kDefault[0].max_bitrate_bps,
            stream->GetVideoStreams()[0].max_bitrate_bps);
  // Layer 1: max configured bitrate should overwrite max default.
  // And target bitrate should be 3/4 * max bitrate or default target
  // which is larger.
  EXPECT_EQ(kDefault[1].min_bitrate_bps,
            stream->GetVideoStreams()[1].min_bitrate_bps);
  const int kTargetBpsLayer1 =
      std::max(kDefault[1].target_bitrate_bps, kMaxBpsLayer1 * 3 / 4);
  EXPECT_EQ(kTargetBpsLayer1, stream->GetVideoStreams()[1].target_bitrate_bps);
  EXPECT_EQ(kMaxBpsLayer1, stream->GetVideoStreams()[1].max_bitrate_bps);
  // Layer 2: min and max bitrate not configured, default expected.
  EXPECT_EQ(kDefault[2].min_bitrate_bps,
            stream->GetVideoStreams()[2].min_bitrate_bps);
  EXPECT_EQ(kDefault[2].target_bitrate_bps,
            stream->GetVideoStreams()[2].target_bitrate_bps);
  EXPECT_EQ(kDefault[2].max_bitrate_bps,
            stream->GetVideoStreams()[2].max_bitrate_bps);

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

// Test that specifying the min (or max) bitrate value for a layer via
// RtpParameters above (or below) the simulcast default max (or min) adjusts the
// unspecified values accordingly.
TEST_F(WebRtcVideoChannelTest, SetMinAndMaxSimulcastBitrateAboveBelowDefault) {
  const std::vector<VideoStream> kDefault = GetSimulcastBitrates720p();
  FakeVideoSendStream* stream = SetUpSimulcast(true, /*with_rtx=*/false);

  // Send a full size frame so all simulcast layers are used when reconfiguring.
  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  // Get and set the rtp encoding parameters.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());

  // Change the value and set it on the VideoChannel.
  // For layer 0, set the min bitrate above the default max.
  const int kMinBpsLayer0 = kDefault[0].max_bitrate_bps + 1;
  parameters.encodings[0].min_bitrate_bps = kMinBpsLayer0;
  // For layer 1, set the max bitrate below the default min.
  const int kMaxBpsLayer1 = kDefault[1].min_bitrate_bps - 1;
  parameters.encodings[1].max_bitrate_bps = kMaxBpsLayer1;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Verify that the new value propagated down to the encoder.
  // FakeVideoSendStream calls CreateEncoderStreams, test that the vector of
  // VideoStreams are created appropriately for the simulcast case.
  EXPECT_EQ(kNumSimulcastStreams, stream->GetVideoStreams().size());
  // Layer 0: Min bitrate above default max (target/max should be adjusted).
  EXPECT_EQ(kMinBpsLayer0, stream->GetVideoStreams()[0].min_bitrate_bps);
  EXPECT_EQ(kMinBpsLayer0, stream->GetVideoStreams()[0].target_bitrate_bps);
  EXPECT_EQ(kMinBpsLayer0, stream->GetVideoStreams()[0].max_bitrate_bps);
  // Layer 1: Max bitrate below default min (min/target should be adjusted).
  EXPECT_EQ(kMaxBpsLayer1, stream->GetVideoStreams()[1].min_bitrate_bps);
  EXPECT_EQ(kMaxBpsLayer1, stream->GetVideoStreams()[1].target_bitrate_bps);
  EXPECT_EQ(kMaxBpsLayer1, stream->GetVideoStreams()[1].max_bitrate_bps);
  // Layer 2: min and max bitrate not configured, default expected.
  EXPECT_EQ(kDefault[2].min_bitrate_bps,
            stream->GetVideoStreams()[2].min_bitrate_bps);
  EXPECT_EQ(kDefault[2].target_bitrate_bps,
            stream->GetVideoStreams()[2].target_bitrate_bps);
  EXPECT_EQ(kDefault[2].max_bitrate_bps,
            stream->GetVideoStreams()[2].max_bitrate_bps);

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest, BandwidthAboveTotalMaxBitrateGivenToMaxLayer) {
  const std::vector<VideoStream> kDefault = GetSimulcastBitrates720p();
  FakeVideoSendStream* stream = SetUpSimulcast(true, /*with_rtx=*/false);

  // Send a full size frame so all simulcast layers are used when reconfiguring.
  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  // Set max bitrate for all but the highest layer.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  parameters.encodings[0].max_bitrate_bps = kDefault[0].max_bitrate_bps;
  parameters.encodings[1].max_bitrate_bps = kDefault[1].max_bitrate_bps;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Set max bandwidth equal to total max bitrate.
  send_parameters_.max_bandwidth_bps =
      GetTotalMaxBitrate(stream->GetVideoStreams()).bps();
  ExpectSetMaxBitrate(send_parameters_.max_bandwidth_bps);
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  // No bitrate above the total max to give to the highest layer.
  EXPECT_EQ(kNumSimulcastStreams, stream->GetVideoStreams().size());
  EXPECT_EQ(kDefault[2].max_bitrate_bps,
            stream->GetVideoStreams()[2].max_bitrate_bps);

  // Set max bandwidth above the total max bitrate.
  send_parameters_.max_bandwidth_bps =
      GetTotalMaxBitrate(stream->GetVideoStreams()).bps() + 1;
  ExpectSetMaxBitrate(send_parameters_.max_bandwidth_bps);
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  // The highest layer has no max bitrate set -> the bitrate above the total
  // max should be given to the highest layer.
  EXPECT_EQ(kNumSimulcastStreams, stream->GetVideoStreams().size());
  EXPECT_EQ(send_parameters_.max_bandwidth_bps,
            GetTotalMaxBitrate(stream->GetVideoStreams()).bps());
  EXPECT_EQ(kDefault[2].max_bitrate_bps + 1,
            stream->GetVideoStreams()[2].max_bitrate_bps);

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest,
       BandwidthAboveTotalMaxBitrateNotGivenToMaxLayerIfMaxBitrateSet) {
  const std::vector<VideoStream> kDefault = GetSimulcastBitrates720p();
  EXPECT_EQ(kNumSimulcastStreams, kDefault.size());
  FakeVideoSendStream* stream = SetUpSimulcast(true, /*with_rtx=*/false);

  // Send a full size frame so all simulcast layers are used when reconfiguring.
  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame());

  // Set max bitrate for the highest layer.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  parameters.encodings[2].max_bitrate_bps = kDefault[2].max_bitrate_bps;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Set max bandwidth above the total max bitrate.
  send_parameters_.max_bandwidth_bps =
      GetTotalMaxBitrate(stream->GetVideoStreams()).bps() + 1;
  ExpectSetMaxBitrate(send_parameters_.max_bandwidth_bps);
  ASSERT_TRUE(send_channel_->SetSenderParameters(send_parameters_));

  // The highest layer has the max bitrate set -> the bitrate above the total
  // max should not be given to the highest layer.
  EXPECT_EQ(kNumSimulcastStreams, stream->GetVideoStreams().size());
  EXPECT_EQ(*parameters.encodings[2].max_bitrate_bps,
            stream->GetVideoStreams()[2].max_bitrate_bps);

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

class WebRtcVideoChannelWithMixedCodecSimulcastTest
    : public WebRtcVideoChannelTest {
 public:
  WebRtcVideoChannelWithMixedCodecSimulcastTest()
      : WebRtcVideoChannelTest("WebRTC-MixedCodecSimulcast/Enabled/") {}
};

TEST_F(WebRtcVideoChannelWithMixedCodecSimulcastTest,
       SetMixedCodecSimulcastStreamConfig) {
  StreamParams sp = CreateSimStreamParams("cname", {123, 456, 789});

  std::vector<RidDescription> rid_descriptions;
  rid_descriptions.emplace_back("f", RidDirection::kSend);
  rid_descriptions.emplace_back("h", RidDirection::kSend);
  rid_descriptions.emplace_back("q", RidDirection::kSend);
  sp.set_rids(rid_descriptions);

  ASSERT_TRUE(send_channel_->AddSendStream(sp));

  RtpParameters rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(3UL, rtp_parameters.encodings.size());
  Codec vp8 = GetEngineCodec("VP8");
  Codec vp9 = GetEngineCodec("VP9");
  rtp_parameters.encodings[0].codec = vp8.ToCodecParameters();
  rtp_parameters.encodings[1].codec = vp8.ToCodecParameters();
  rtp_parameters.encodings[2].codec = vp9.ToCodecParameters();
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());

  VideoSenderParameters parameters;
  parameters.codecs.push_back(vp8);
  parameters.codecs.push_back(vp9);
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  const auto& streams = fake_call_->GetVideoSendStreams();
  ASSERT_EQ(1u, streams.size());
  auto stream = streams[0];
  ASSERT_NE(stream, nullptr);
  const auto& config = stream->GetConfig();
  // RtpStreamConfig should have the correct codec name and payload type.
  ASSERT_THAT(config.rtp.stream_configs, SizeIs(3));
  EXPECT_EQ(config.rtp.stream_configs[0].rid, "f");
  EXPECT_EQ(config.rtp.stream_configs[1].rid, "h");
  EXPECT_EQ(config.rtp.stream_configs[2].rid, "q");
  EXPECT_EQ(config.rtp.stream_configs[0].payload_name, vp8.name);
  EXPECT_EQ(config.rtp.stream_configs[1].payload_name, vp8.name);
  EXPECT_EQ(config.rtp.stream_configs[2].payload_name, vp9.name);
  EXPECT_EQ(config.rtp.stream_configs[0].payload_type, vp8.id);
  EXPECT_EQ(config.rtp.stream_configs[1].payload_type, vp8.id);
  EXPECT_EQ(config.rtp.stream_configs[2].payload_type, vp9.id);
}

#if RTC_DCHECK_IS_ON && GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)
TEST_F(WebRtcVideoChannelWithMixedCodecSimulcastTest,
       SetMixedCodecSimulcastWithDifferentConfigSettingsSizes) {
  AddSendStream();

  VideoSenderParameters parameters;
  Codec vp8 = GetEngineCodec("VP8");
  parameters.codecs.push_back(vp8);

  // `codec_settings_list.size()` is 1 after this in the
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  // It sets 2 sizes of config ssrc.
  StreamParams sp = CreateSimStreamParams("cname", {123, 456});
  std::vector<RidDescription> rid_descriptions2;
  rid_descriptions2.emplace_back("f", RidDirection::kSend);
  rid_descriptions2.emplace_back("h", RidDirection::kSend);
  sp.set_rids(rid_descriptions2);

  // `WebRtcVideoSendStream::SetCodec` test for different sizes
  // between parameters_.config.rtp.ssrcs.size() and codec_settings_list.size().
  EXPECT_DEATH(send_channel_->AddSendStream(sp), "");
}
#endif

TEST_F(WebRtcVideoChannelWithMixedCodecSimulcastTest,
       SetRtpSendParametersFailsForMixedCodecSimulcastWithScalabilityMode) {
  StreamParams sp = CreateSimStreamParams("cname", {123, 456, 789});

  std::vector<RidDescription> rid_descriptions;
  rid_descriptions.emplace_back("f", RidDirection::kSend);
  rid_descriptions.emplace_back("h", RidDirection::kSend);
  rid_descriptions.emplace_back("q", RidDirection::kSend);
  sp.set_rids(rid_descriptions);

  ASSERT_TRUE(send_channel_->AddSendStream(sp));

  Codec vp8 = GetEngineCodec("VP8");
  Codec vp9 = GetEngineCodec("VP9");
  RtpParameters rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  ASSERT_EQ(3UL, rtp_parameters.encodings.size());

  // In single-codec simulcast, any scalability_mode is allowed.
  rtp_parameters.encodings[0].codec = vp9.ToCodecParameters();
  rtp_parameters.encodings[1].codec = vp9.ToCodecParameters();
  rtp_parameters.encodings[2].codec = vp9.ToCodecParameters();
  rtp_parameters.encodings[2].scalability_mode = "L1T1";
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());
  rtp_parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  rtp_parameters.encodings[2].scalability_mode = "L2T1";
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());

  // In mixed-codec simulcast, only L1 scalability modes are allowed; higher
  // spatial-layer modes (e.g., L2) must be rejected.
  rtp_parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  rtp_parameters.encodings[0].codec = vp8.ToCodecParameters();
  rtp_parameters.encodings[1].codec = vp8.ToCodecParameters();
  rtp_parameters.encodings[2].codec = vp9.ToCodecParameters();
  rtp_parameters.encodings[2].scalability_mode = "L1T1";
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());
  rtp_parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  rtp_parameters.encodings[2].scalability_mode = "L2T1";
  RTCError error =
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters);
  EXPECT_FALSE(error.ok());
  EXPECT_EQ(RTCErrorType::UNSUPPORTED_OPERATION, error.type());
  // UNSUPPORTED_OPERATION could also indicate mixed-codec rejection; verify the
  // message to ensure it specifically rejects spatial layers in
  // scalabilityMode.
  EXPECT_THAT(error.message(),
              testing::HasSubstr(
                  "Attempted to use a scalabilityMode with spatial layers"));
}

// When stats are retrieved in mixed-codec simulcast, verify that information
// about multiple codecs and the streams referencing them is obtained.
TEST_F(WebRtcVideoChannelWithMixedCodecSimulcastTest, GetStatsMixedCodec) {
  StreamParams sp = CreateSimStreamParams("cname", {123, 456, 789});

  std::vector<RidDescription> rid_descriptions;
  rid_descriptions.emplace_back("f", RidDirection::kSend);
  rid_descriptions.emplace_back("h", RidDirection::kSend);
  rid_descriptions.emplace_back("q", RidDirection::kSend);
  sp.set_rids(rid_descriptions);

  FakeVideoSendStream* stream = AddSendStream(sp);

  VideoSendStream::Stats stats;
  stats.substreams[123];
  stats.substreams[456];
  stats.substreams[789];
  stream->SetStats(stats);

  RtpParameters rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(3UL, rtp_parameters.encodings.size());
  Codec vp8 = GetEngineCodec("VP8");
  Codec vp9 = GetEngineCodec("VP9");
  rtp_parameters.encodings[0].codec = vp8.ToCodecParameters();
  rtp_parameters.encodings[1].codec = vp8.ToCodecParameters();
  rtp_parameters.encodings[2].codec = vp9.ToCodecParameters();
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());

  VideoMediaSendInfo send_info;
  EXPECT_TRUE(send_channel_->GetStats(&send_info));
  EXPECT_EQ(2U, send_info.send_codecs.size());
  EXPECT_EQ(vp8.id, send_info.send_codecs[vp8.id].payload_type);
  EXPECT_EQ(vp9.id, send_info.send_codecs[vp9.id].payload_type);
  EXPECT_EQ(vp8.name, send_info.send_codecs[vp8.id].name);
  EXPECT_EQ(vp9.name, send_info.send_codecs[vp9.id].name);
  EXPECT_EQ(3U, send_info.senders.size());
  EXPECT_EQ(vp8.id, send_info.senders[0].codec_payload_type);
  EXPECT_EQ(vp8.id, send_info.senders[1].codec_payload_type);
  EXPECT_EQ(vp9.id, send_info.senders[2].codec_payload_type);
  EXPECT_EQ(vp8.name, send_info.senders[0].codec_name);
  EXPECT_EQ(vp8.name, send_info.senders[1].codec_name);
  EXPECT_EQ(vp9.name, send_info.senders[2].codec_name);
}

// Test that min and max bitrate values set via RtpParameters are correctly
// propagated to the underlying encoder for a single stream.
TEST_F(WebRtcVideoChannelTest, MinAndMaxBitratePropagatedToEncoder) {
  FakeVideoSendStream* stream = AddSendStream();
  EXPECT_TRUE(send_channel_->SetSend(true));
  EXPECT_TRUE(stream->IsSending());

  // Set min and max bitrate.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(1u, parameters.encodings.size());
  parameters.encodings[0].min_bitrate_bps = 80000;
  parameters.encodings[0].max_bitrate_bps = 150000;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(last_ssrc_, parameters).ok());

  // Check that WebRtcVideoSendStream updates VideoEncoderConfig correctly.
  VideoEncoderConfig encoder_config = stream->GetEncoderConfig().Copy();
  EXPECT_EQ(1u, encoder_config.number_of_streams);
  EXPECT_EQ(1u, encoder_config.simulcast_layers.size());
  EXPECT_EQ(80000, encoder_config.simulcast_layers[0].min_bitrate_bps);
  EXPECT_EQ(150000, encoder_config.simulcast_layers[0].max_bitrate_bps);

  // FakeVideoSendStream calls CreateEncoderStreams, test that the vector of
  // VideoStreams are created appropriately.
  EXPECT_EQ(1u, stream->GetVideoStreams().size());
  EXPECT_EQ(80000, stream->GetVideoStreams()[0].min_bitrate_bps);
  EXPECT_EQ(150000, stream->GetVideoStreams()[0].target_bitrate_bps);
  EXPECT_EQ(150000, stream->GetVideoStreams()[0].max_bitrate_bps);
}

// Test the default min and max bitrate value are correctly propagated to the
// underlying encoder for a single stream (when the values are not set via
// RtpParameters).
TEST_F(WebRtcVideoChannelTest, DefaultMinAndMaxBitratePropagatedToEncoder) {
  FakeVideoSendStream* stream = AddSendStream();
  EXPECT_TRUE(send_channel_->SetSend(true));
  EXPECT_TRUE(stream->IsSending());

  // Check that WebRtcVideoSendStream updates VideoEncoderConfig correctly.
  VideoEncoderConfig encoder_config = stream->GetEncoderConfig().Copy();
  EXPECT_EQ(1u, encoder_config.number_of_streams);
  EXPECT_EQ(1u, encoder_config.simulcast_layers.size());
  EXPECT_EQ(-1, encoder_config.simulcast_layers[0].min_bitrate_bps);
  EXPECT_EQ(-1, encoder_config.simulcast_layers[0].max_bitrate_bps);

  // FakeVideoSendStream calls CreateEncoderStreams, test that the vector of
  // VideoStreams are created appropriately.
  EXPECT_EQ(1u, stream->GetVideoStreams().size());
  EXPECT_EQ(kDefaultMinVideoBitrateBps,
            stream->GetVideoStreams()[0].min_bitrate_bps);
  EXPECT_GT(stream->GetVideoStreams()[0].max_bitrate_bps,
            stream->GetVideoStreams()[0].min_bitrate_bps);
  EXPECT_EQ(stream->GetVideoStreams()[0].max_bitrate_bps,
            stream->GetVideoStreams()[0].target_bitrate_bps);
}

// Tests that when some streams are disactivated then the lowest
// stream min_bitrate would be reused for the first active stream.
TEST_F(WebRtcVideoChannelTest,
       SetRtpSendParametersSetsMinBitrateForFirstActiveStream) {
  // Create the stream params with multiple ssrcs for simulcast.
  std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs3);
  StreamParams stream_params = CreateSimStreamParams("cname", ssrcs);
  FakeVideoSendStream* fake_video_send_stream = AddSendStream(stream_params);
  uint32_t primary_ssrc = stream_params.first_ssrc();

  // Using the FrameForwarder, we manually send a full size
  // frame. This allows us to test that ReconfigureEncoder is called
  // appropriately.
  FrameForwarder frame_forwarder;
  VideoOptions options;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(primary_ssrc, &options, &frame_forwarder));
  send_channel_->SetSend(true);
  frame_forwarder.IncomingCapturedFrame(frame_source_.GetFrame(
      1920, 1080, VideoRotation::kVideoRotation_0, TimeDelta::Seconds(1) / 30));

  // Check that all encodings are initially active.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(primary_ssrc);
  EXPECT_EQ(kNumSimulcastStreams, parameters.encodings.size());
  EXPECT_TRUE(parameters.encodings[0].active);
  EXPECT_TRUE(parameters.encodings[1].active);
  EXPECT_TRUE(parameters.encodings[2].active);
  EXPECT_TRUE(fake_video_send_stream->IsSending());

  // Only turn on the highest stream.
  parameters.encodings[0].active = false;
  parameters.encodings[1].active = false;
  parameters.encodings[2].active = true;
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(primary_ssrc, parameters).ok());

  // Check that the VideoSendStream is updated appropriately. This means its
  // send state was updated and it was reconfigured.
  EXPECT_TRUE(fake_video_send_stream->IsSending());
  std::vector<VideoStream> simulcast_streams =
      fake_video_send_stream->GetVideoStreams();
  EXPECT_EQ(kNumSimulcastStreams, simulcast_streams.size());
  EXPECT_FALSE(simulcast_streams[0].active);
  EXPECT_FALSE(simulcast_streams[1].active);
  EXPECT_TRUE(simulcast_streams[2].active);

  EXPECT_EQ(simulcast_streams[2].min_bitrate_bps,
            simulcast_streams[0].min_bitrate_bps);

  EXPECT_TRUE(send_channel_->SetVideoSend(primary_ssrc, nullptr, nullptr));
}

// Test that GetRtpSendParameters returns the currently configured codecs.
TEST_F(WebRtcVideoChannelTest, GetRtpSendParametersCodecs) {
  AddSendStream();
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  RtpParameters rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  ASSERT_EQ(2u, rtp_parameters.codecs.size());
  EXPECT_EQ(GetEngineCodec("VP8").ToCodecParameters(),
            rtp_parameters.codecs[0]);
  EXPECT_EQ(GetEngineCodec("VP9").ToCodecParameters(),
            rtp_parameters.codecs[1]);
}

// Test that GetRtpSendParameters returns the currently configured RTCP CNAME.
TEST_F(WebRtcVideoChannelTest, GetRtpSendParametersRtcpCname) {
  StreamParams params = StreamParams::CreateLegacy(kSsrc);
  params.cname = "rtcpcname";
  AddSendStream(params);

  RtpParameters rtp_parameters = send_channel_->GetRtpSendParameters(kSsrc);
  EXPECT_STREQ("rtcpcname", rtp_parameters.rtcp.cname.c_str());
}

// Test that RtpParameters for send stream has one encoding and it has
// the correct SSRC.
TEST_F(WebRtcVideoChannelTest, GetRtpSendParametersSsrc) {
  AddSendStream();

  RtpParameters rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  ASSERT_EQ(1u, rtp_parameters.encodings.size());
  EXPECT_EQ(last_ssrc_, rtp_parameters.encodings[0].ssrc);
}

TEST_F(WebRtcVideoChannelTest, DetectRtpSendParameterHeaderExtensionsChange) {
  AddSendStream();

  RtpParameters rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  rtp_parameters.header_extensions.emplace_back();

  EXPECT_NE(0u, rtp_parameters.header_extensions.size());

  RTCError result =
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters);
  EXPECT_EQ(RTCErrorType::INVALID_MODIFICATION, result.type());
}

TEST_F(WebRtcVideoChannelTest, GetRtpSendParametersDegradationPreference) {
  AddSendStream();

  FrameForwarder frame_forwarder;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, nullptr, &frame_forwarder));

  RtpParameters rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_FALSE(rtp_parameters.degradation_preference.has_value());
  rtp_parameters.degradation_preference =
      DegradationPreference::MAINTAIN_FRAMERATE;

  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());

  RtpParameters updated_rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  EXPECT_EQ(updated_rtp_parameters.degradation_preference,
            DegradationPreference::MAINTAIN_FRAMERATE);

  // Remove the source since it will be destroyed before the channel
  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

// Test that if we set/get parameters multiple times, we get the same results.
TEST_F(WebRtcVideoChannelTest, SetAndGetRtpSendParameters) {
  AddSendStream();
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));

  RtpParameters initial_params =
      send_channel_->GetRtpSendParameters(last_ssrc_);

  // We should be able to set the params we just got.
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, initial_params).ok());

  // ... And this shouldn't change the params returned by GetRtpSendParameters.
  EXPECT_EQ(initial_params, send_channel_->GetRtpSendParameters(last_ssrc_));
}

TEST_F(WebRtcVideoChannelTest, SetRtpSendParametersRejectsUnsupportedCodec) {
  AddSendStream();
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  RtpParameters rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  ASSERT_EQ(1u, rtp_parameters.encodings.size());
  RtpCodec vp9_rtp_codec;
  vp9_rtp_codec.name = "VP9";
  vp9_rtp_codec.kind = MediaType::VIDEO;
  vp9_rtp_codec.clock_rate = 90000;
  rtp_parameters.encodings[0].codec = vp9_rtp_codec;

  RTCError error =
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters);
  EXPECT_EQ(RTCErrorType::INVALID_MODIFICATION, error.type());
  EXPECT_STREQ("Attempted to use an unsupported codec VP9 for encodings[0].",
               error.message());
}

// Test that GetRtpReceiverParameters returns the currently configured codecs.
TEST_F(WebRtcVideoChannelTest, GetRtpReceiveParametersCodecs) {
  AddRecvStream();
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

  RtpParameters rtp_parameters =
      receive_channel_->GetRtpReceiverParameters(last_ssrc_);
  ASSERT_EQ(2u, rtp_parameters.codecs.size());
  EXPECT_EQ(GetEngineCodec("VP8").ToCodecParameters(),
            rtp_parameters.codecs[0]);
  EXPECT_EQ(GetEngineCodec("VP9").ToCodecParameters(),
            rtp_parameters.codecs[1]);
}

#if defined(WEBRTC_USE_H264)
TEST_F(WebRtcVideoChannelTest, GetRtpReceiveFmtpSprop) {
#else
TEST_F(WebRtcVideoChannelTest, DISABLED_GetRtpReceiveFmtpSprop) {
#endif
  VideoReceiverParameters parameters;
  Codec kH264sprop1 = CreateVideoCodec(101, "H264");
  kH264sprop1.SetParam(kH264FmtpSpropParameterSets, "uvw");
  parameters.codecs.push_back(kH264sprop1);
  Codec kH264sprop2 = CreateVideoCodec(102, "H264");
  kH264sprop2.SetParam(kH264FmtpSpropParameterSets, "xyz");
  parameters.codecs.push_back(kH264sprop2);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

  FakeVideoReceiveStream* recv_stream = AddRecvStream();
  const VideoReceiveStreamInterface::Config& cfg = recv_stream->GetConfig();
  RtpParameters rtp_parameters =
      receive_channel_->GetRtpReceiverParameters(last_ssrc_);
  ASSERT_EQ(2u, rtp_parameters.codecs.size());
  EXPECT_EQ(kH264sprop1.ToCodecParameters(), rtp_parameters.codecs[0]);
  ASSERT_EQ(2u, cfg.decoders.size());
  EXPECT_EQ(101, cfg.decoders[0].payload_type);
  EXPECT_EQ("H264", cfg.decoders[0].video_format.name);
  const auto it0 = cfg.decoders[0].video_format.parameters.find(
      std::string(kH264FmtpSpropParameterSets));
  ASSERT_TRUE(it0 != cfg.decoders[0].video_format.parameters.end());
  EXPECT_EQ("uvw", it0->second);

  EXPECT_EQ(102, cfg.decoders[1].payload_type);
  EXPECT_EQ("H264", cfg.decoders[1].video_format.name);
  const auto it1 = cfg.decoders[1].video_format.parameters.find(
      std::string(kH264FmtpSpropParameterSets));
  ASSERT_TRUE(it1 != cfg.decoders[1].video_format.parameters.end());
  EXPECT_EQ("xyz", it1->second);
}

// Test that RtpParameters for receive stream has one encoding and it has
// the correct SSRC.
TEST_F(WebRtcVideoChannelTest, GetRtpReceiveParametersSsrc) {
  AddRecvStream();

  RtpParameters rtp_parameters =
      receive_channel_->GetRtpReceiverParameters(last_ssrc_);
  ASSERT_EQ(1u, rtp_parameters.encodings.size());
  EXPECT_EQ(last_ssrc_, rtp_parameters.encodings[0].ssrc);
}

// Test that if we set/get parameters multiple times, we get the same results.
TEST_F(WebRtcVideoChannelTest, SetAndGetRtpReceiveParameters) {
  AddRecvStream();
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

  RtpParameters initial_params =
      receive_channel_->GetRtpReceiverParameters(last_ssrc_);

  // ... And this shouldn't change the params returned by
  // GetRtpReceiverParameters.
  EXPECT_EQ(initial_params,
            receive_channel_->GetRtpReceiverParameters(last_ssrc_));
}

// Test that GetDefaultRtpReceiveParameters returns parameters correctly when
// SSRCs aren't signaled. It should always return an empty
// "RtpEncodingParameters", even after a packet is received and the unsignaled
// SSRC is known.
TEST_F(WebRtcVideoChannelTest,
       GetDefaultRtpReceiveParametersWithUnsignaledSsrc) {
  // Call necessary methods to configure receiving a default stream as
  // soon as it arrives.
  VideoReceiverParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(GetEngineCodec("VP9"));
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

  // Call GetRtpReceiverParameters before configured to receive an unsignaled
  // stream. Should return nothing.
  EXPECT_EQ(RtpParameters(),
            receive_channel_->GetDefaultRtpReceiveParameters());

  // Set a sink for an unsignaled stream.
  FakeVideoRenderer renderer;
  receive_channel_->SetDefaultSink(&renderer);

  // Call GetDefaultRtpReceiveParameters before the SSRC is known.
  RtpParameters rtp_parameters =
      receive_channel_->GetDefaultRtpReceiveParameters();
  ASSERT_EQ(1u, rtp_parameters.encodings.size());
  EXPECT_FALSE(rtp_parameters.encodings[0].ssrc);

  // Receive VP8 packet.
  RtpPacketReceived rtp_packet;
  rtp_packet.SetPayloadType(GetEngineCodec("VP8").id);
  rtp_packet.SetSsrc(kIncomingUnsignalledSsrc);
  ReceivePacketAndAdvanceTime(rtp_packet);

  // The `ssrc` member should still be unset.
  rtp_parameters = receive_channel_->GetDefaultRtpReceiveParameters();
  ASSERT_EQ(1u, rtp_parameters.encodings.size());
  EXPECT_FALSE(rtp_parameters.encodings[0].ssrc);
}

// Test that if a default stream is created for a non-primary stream (for
// example, RTX before we know it's RTX), we are still able to explicitly add
// the stream later.
TEST_F(WebRtcVideoChannelTest,
       AddReceiveStreamAfterReceivingNonPrimaryUnsignaledSsrc) {
  // Receive VP8 RTX packet.
  RtpPacketReceived rtp_packet;
  const Codec vp8 = GetEngineCodec("VP8");
  rtp_packet.SetPayloadType(default_apt_rtx_types_[vp8.id]);
  rtp_packet.SetSsrc(2);
  ReceivePacketAndAdvanceTime(rtp_packet);
  EXPECT_EQ(1u, fake_call_->GetVideoReceiveStreams().size());

  StreamParams params = StreamParams::CreateLegacy(1);
  params.AddFidSsrc(1, 2);
  EXPECT_TRUE(receive_channel_->AddRecvStream(params));
}

TEST_F(WebRtcVideoChannelTest, Simulcast_QualityScalingNotAllowed) {
  FakeVideoSendStream* stream = SetUpSimulcast(true, /*with_rtx=*/true);
  EXPECT_FALSE(stream->GetEncoderConfig().is_quality_scaling_allowed);
}

TEST_F(WebRtcVideoChannelTest, Singlecast_QualityScalingAllowed) {
  FakeVideoSendStream* stream = SetUpSimulcast(false, /*with_rtx=*/true);
  EXPECT_TRUE(stream->GetEncoderConfig().is_quality_scaling_allowed);
}

TEST_F(WebRtcVideoChannelTest,
       SinglecastScreenSharing_QualityScalingNotAllowed) {
  SetUpSimulcast(false, /*with_rtx=*/true);

  FrameForwarder frame_forwarder;
  VideoOptions options;
  options.is_screencast = true;
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, &options, &frame_forwarder));
  // Fetch the latest stream since SetVideoSend() may recreate it if the
  // screen content setting is changed.
  FakeVideoSendStream* stream = fake_call_->GetVideoSendStreams().front();

  EXPECT_FALSE(stream->GetEncoderConfig().is_quality_scaling_allowed);
  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest,
       SimulcastSingleActiveStream_QualityScalingAllowed) {
  FakeVideoSendStream* stream = SetUpSimulcast(true, /*with_rtx=*/false);

  RtpParameters rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  ASSERT_EQ(3u, rtp_parameters.encodings.size());
  ASSERT_TRUE(rtp_parameters.encodings[0].active);
  ASSERT_TRUE(rtp_parameters.encodings[1].active);
  ASSERT_TRUE(rtp_parameters.encodings[2].active);
  rtp_parameters.encodings[0].active = false;
  rtp_parameters.encodings[1].active = false;
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());
  EXPECT_TRUE(stream->GetEncoderConfig().is_quality_scaling_allowed);
}

TEST_F(WebRtcVideoChannelTest, GenerateKeyFrameSinglecast) {
  FakeVideoSendStream* stream = AddSendStream();

  RtpParameters rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  ASSERT_EQ(1u, rtp_parameters.encodings.size());
  EXPECT_EQ(rtp_parameters.encodings[0].rid, "");
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());
  EXPECT_THAT(stream->GetKeyFramesRequested(), std::vector<std::string>({}));

  // Manually set the key frames requested to check they are cleared by the next
  // call.
  stream->GenerateKeyFrame({"bogus"});
  rtp_parameters.encodings[0].request_key_frame = true;
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());
  EXPECT_THAT(stream->GetKeyFramesRequested(),
              ElementsAreArray(std::vector<std::string>({})));
}

TEST_F(WebRtcVideoChannelTest, GenerateKeyFrameSimulcast) {
  StreamParams stream_params = CreateSimStreamParams("cname", {123, 456, 789});

  std::vector<std::string> rids = {"f", "h", "q"};
  std::vector<RidDescription> rid_descriptions;
  for (const auto& rid : rids) {
    rid_descriptions.emplace_back(rid, RidDirection::kSend);
  }
  stream_params.set_rids(rid_descriptions);
  FakeVideoSendStream* stream = AddSendStream(stream_params);

  RtpParameters rtp_parameters =
      send_channel_->GetRtpSendParameters(last_ssrc_);
  ASSERT_EQ(3u, rtp_parameters.encodings.size());
  EXPECT_EQ(rtp_parameters.encodings[0].rid, "f");
  EXPECT_EQ(rtp_parameters.encodings[1].rid, "h");
  EXPECT_EQ(rtp_parameters.encodings[2].rid, "q");

  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());
  EXPECT_THAT(stream->GetKeyFramesRequested(),
              ElementsAreArray(std::vector<std::string>({})));

  rtp_parameters.encodings[0].request_key_frame = true;
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());
  EXPECT_THAT(stream->GetKeyFramesRequested(), ElementsAreArray({"f"}));

  rtp_parameters.encodings[0].request_key_frame = true;
  rtp_parameters.encodings[1].request_key_frame = true;
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());
  EXPECT_THAT(stream->GetKeyFramesRequested(), ElementsAreArray({"f", "h"}));

  rtp_parameters.encodings[0].request_key_frame = true;
  rtp_parameters.encodings[1].request_key_frame = true;
  rtp_parameters.encodings[2].request_key_frame = true;
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());
  EXPECT_THAT(stream->GetKeyFramesRequested(),
              ElementsAreArray({"f", "h", "q"}));

  rtp_parameters.encodings[0].request_key_frame = true;
  rtp_parameters.encodings[1].request_key_frame = false;
  rtp_parameters.encodings[2].request_key_frame = true;
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());
  EXPECT_THAT(stream->GetKeyFramesRequested(), ElementsAreArray({"f", "q"}));

  rtp_parameters.encodings[0].request_key_frame = false;
  rtp_parameters.encodings[1].request_key_frame = false;
  rtp_parameters.encodings[2].request_key_frame = true;
  EXPECT_TRUE(
      send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters).ok());
  EXPECT_THAT(stream->GetKeyFramesRequested(), ElementsAreArray({"q"}));
}

class WebRtcVideoChannelSimulcastTest : public ::testing::Test {
 public:
  WebRtcVideoChannelSimulcastTest()
      : run_loop_(),
        env_(CreateTestEnvironment()),
        fake_call_(env_),
        encoder_factory_(new FakeWebRtcVideoEncoderFactory),
        decoder_factory_(new FakeWebRtcVideoDecoderFactory),
        mock_rate_allocator_factory_(
            std::make_unique<MockVideoBitrateAllocatorFactory>()),
        engine_(
            std::unique_ptr<FakeWebRtcVideoEncoderFactory>(encoder_factory_),
            std::unique_ptr<FakeWebRtcVideoDecoderFactory>(decoder_factory_),
            env_.field_trials()),
        last_ssrc_(0) {}

  void SetUp() override {
    encoder_factory_->AddSupportedVideoCodecType("VP8");
    decoder_factory_->AddSupportedVideoCodecType("VP8");
    send_channel_ = engine_.CreateSendChannel(
        env_, &fake_call_, GetMediaConfig(), VideoOptions(), CryptoOptions(),
        mock_rate_allocator_factory_.get(), nullptr, nullptr);
    receive_channel_ = engine_.CreateReceiveChannel(
        env_, &fake_call_, GetMediaConfig(), CryptoOptions(), nullptr,
        [](uint32_t, const RtpPacketInfos&, Timestamp) {});
    send_channel_->OnReadyToSend(true);
    receive_channel_->SetReceive(true);
    last_ssrc_ = 123;
  }

 protected:
  void VerifySimulcastSettings(const Codec& codec_in,
                               int capture_width,
                               int capture_height,
                               size_t num_configured_streams,
                               size_t expected_num_streams,
                               bool screenshare,
                               bool conference_mode) {
    VideoSenderParameters parameters;
    // The codec ID does not matter, but must be valid.
    Codec codec = codec_in;
    codec.id = 123;
    parameters.codecs.push_back(codec);
    parameters.conference_mode = conference_mode;
    ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

    std::vector<uint32_t> ssrcs = MAKE_VECTOR(kSsrcs3);
    RTC_DCHECK(num_configured_streams <= ssrcs.size());
    ssrcs.resize(num_configured_streams);

    AddSendStream(CreateSimStreamParams("cname", ssrcs));
    // Send a full-size frame to trigger a stream reconfiguration to use all
    // expected simulcast layers.
    FrameForwarder frame_forwarder;
    FakeFrameSource frame_source(capture_width, capture_height,
                                 TimeDelta::Seconds(1) / 30,
                                 env_.clock().CurrentTime());

    VideoOptions options;
    if (screenshare)
      options.is_screencast = screenshare;
    EXPECT_TRUE(
        send_channel_->SetVideoSend(ssrcs.front(), &options, &frame_forwarder));
    // Fetch the latest stream since SetVideoSend() may recreate it if the
    // screen content setting is changed.
    FakeVideoSendStream* stream = fake_call_.GetVideoSendStreams().front();
    send_channel_->SetSend(true);
    frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());

    auto rtp_parameters = send_channel_->GetRtpSendParameters(kSsrcs3[0]);
    EXPECT_EQ(num_configured_streams, rtp_parameters.encodings.size());

    std::vector<VideoStream> video_streams = stream->GetVideoStreams();
    ASSERT_EQ(expected_num_streams, video_streams.size());
    EXPECT_LE(expected_num_streams, stream->GetConfig().rtp.ssrcs.size());

    std::vector<VideoStream> expected_streams;
    if (num_configured_streams > 1 || conference_mode) {
      const VideoEncoderConfig& encoder_config = stream->GetEncoderConfig();
      VideoEncoder::EncoderInfo encoder_info;
      auto factory = make_ref_counted<EncoderStreamFactory>(encoder_info);
      expected_streams = factory->CreateEncoderStreams(
          env_.field_trials(), capture_width, capture_height, encoder_config);
      if (screenshare && conference_mode) {
        for (const VideoStream& expected_stream : expected_streams) {
          // Never scale screen content.
          EXPECT_EQ(expected_stream.width, checked_cast<size_t>(capture_width));
          EXPECT_EQ(expected_stream.height,
                    checked_cast<size_t>(capture_height));
        }
      }
    } else {
      VideoStream expected_stream;
      expected_stream.width = capture_width;
      expected_stream.height = capture_height;
      expected_stream.max_framerate = kDefaultVideoMaxFramerate;
      expected_stream.min_bitrate_bps = kDefaultMinVideoBitrateBps;
      expected_stream.target_bitrate_bps = expected_stream.max_bitrate_bps =
          GetMaxDefaultBitrateBps(capture_width, capture_height);
      expected_stream.max_qp = kDefaultVideoMaxQpVpx;
      expected_streams.push_back(expected_stream);
    }

    ASSERT_EQ(expected_streams.size(), video_streams.size());

    size_t num_streams = video_streams.size();
    for (size_t i = 0; i < num_streams; ++i) {
      EXPECT_EQ(expected_streams[i].width, video_streams[i].width);
      EXPECT_EQ(expected_streams[i].height, video_streams[i].height);

      EXPECT_GT(video_streams[i].max_framerate, 0);
      EXPECT_EQ(expected_streams[i].max_framerate,
                video_streams[i].max_framerate);

      EXPECT_GT(video_streams[i].min_bitrate_bps, 0);
      EXPECT_EQ(expected_streams[i].min_bitrate_bps,
                video_streams[i].min_bitrate_bps);

      EXPECT_GT(video_streams[i].target_bitrate_bps, 0);
      EXPECT_EQ(expected_streams[i].target_bitrate_bps,
                video_streams[i].target_bitrate_bps);

      EXPECT_GT(video_streams[i].max_bitrate_bps, 0);
      EXPECT_EQ(expected_streams[i].max_bitrate_bps,
                video_streams[i].max_bitrate_bps);

      EXPECT_GT(video_streams[i].max_qp, 0);
      EXPECT_EQ(video_streams[i].max_qp, kDefaultVideoMaxQpVpx);

      EXPECT_EQ(num_configured_streams > 1 || conference_mode,
                expected_streams[i].num_temporal_layers.has_value());

      if (conference_mode) {
        EXPECT_EQ(expected_streams[i].num_temporal_layers,
                  video_streams[i].num_temporal_layers);
      }
    }

    EXPECT_TRUE(send_channel_->SetVideoSend(ssrcs.front(), nullptr, nullptr));
  }

  FakeVideoSendStream* AddSendStream() {
    return AddSendStream(StreamParams::CreateLegacy(last_ssrc_++));
  }

  FakeVideoSendStream* AddSendStream(const StreamParams& sp) {
    size_t num_streams = fake_call_.GetVideoSendStreams().size();
    EXPECT_TRUE(send_channel_->AddSendStream(sp));
    std::vector<FakeVideoSendStream*> streams =
        fake_call_.GetVideoSendStreams();
    EXPECT_EQ(num_streams + 1, streams.size());
    return streams[streams.size() - 1];
  }

  std::vector<FakeVideoSendStream*> GetFakeSendStreams() {
    return fake_call_.GetVideoSendStreams();
  }

  FakeVideoReceiveStream* AddRecvStream() {
    return AddRecvStream(StreamParams::CreateLegacy(last_ssrc_++));
  }

  FakeVideoReceiveStream* AddRecvStream(const StreamParams& sp) {
    size_t num_streams = fake_call_.GetVideoReceiveStreams().size();
    EXPECT_TRUE(receive_channel_->AddRecvStream(sp));
    std::vector<FakeVideoReceiveStream*> streams =
        fake_call_.GetVideoReceiveStreams();
    EXPECT_EQ(num_streams + 1, streams.size());
    return streams[streams.size() - 1];
  }

  test::RunLoop run_loop_;
  Environment env_;
  FakeCall fake_call_;
  FakeWebRtcVideoEncoderFactory* encoder_factory_;
  FakeWebRtcVideoDecoderFactory* decoder_factory_;
  std::unique_ptr<MockVideoBitrateAllocatorFactory>
      mock_rate_allocator_factory_;
  WebRtcVideoEngine engine_;
  std::unique_ptr<VideoMediaSendChannelInterface> send_channel_;
  std::unique_ptr<VideoMediaReceiveChannelInterface> receive_channel_;
  uint32_t last_ssrc_;
};

TEST_F(WebRtcVideoChannelSimulcastTest, SetSendCodecsWith2SimulcastStreams) {
  VerifySimulcastSettings(CreateVideoCodec("VP8"), 640, 360, 2, 2, false, true);
}

TEST_F(WebRtcVideoChannelSimulcastTest, SetSendCodecsWith3SimulcastStreams) {
  VerifySimulcastSettings(CreateVideoCodec("VP8"), 1280, 720, 3, 3, false,
                          true);
}

// Test that we normalize send codec format size in simulcast.
TEST_F(WebRtcVideoChannelSimulcastTest, SetSendCodecsWithOddSizeInSimulcast) {
  VerifySimulcastSettings(CreateVideoCodec("VP8"), 541, 271, 2, 2, false, true);
}

TEST_F(WebRtcVideoChannelSimulcastTest, SetSendCodecsForScreenshare) {
  VerifySimulcastSettings(CreateVideoCodec("VP8"), 1280, 720, 3, 3, true,
                          false);
}

TEST_F(WebRtcVideoChannelSimulcastTest, SetSendCodecsForSimulcastScreenshare) {
  VerifySimulcastSettings(CreateVideoCodec("VP8"), 1280, 720, 3, 2, true, true);
}

TEST_F(WebRtcVideoChannelSimulcastTest, SimulcastScreenshareWithoutConference) {
  VerifySimulcastSettings(CreateVideoCodec("VP8"), 1280, 720, 3, 3, true,
                          false);
}

TEST_F(WebRtcVideoChannelBaseTest, OnFrameDeliveredCallback) {
  receive_channel_->SetInterface(nullptr);
  SourceTracker tracker(&env_.clock());
  receive_channel_ = engine_->CreateReceiveChannel(
      env_, call_.get(), MediaConfig(), CryptoOptions(),
      /*on_first_packet=*/nullptr,
      [&tracker](uint32_t ssrc, const RtpPacketInfos& infos,
                 Timestamp delivery_time) {
        tracker.OnFrameDelivered(infos, delivery_time);
      });
  network_interface_.SetDestination(receive_channel_.get());
  receive_channel_->SetInterface(&network_interface_);
  VideoReceiverParameters parameters;
  parameters.codecs = engine_->LegacySendCodecs();
  receive_channel_->SetReceiverParameters(parameters);
  receive_channel_->SetReceive(true);
  EXPECT_THAT(tracker.GetSources(), IsEmpty());

  receive_channel_->SetDefaultSink(&renderer_);
  EXPECT_TRUE(SetDefaultCodec());
  EXPECT_TRUE(SetSend(true));
  EXPECT_EQ(renderer_.num_rendered_frames(), 0);

  // Send and receive one frame.
  SendFrame();
  EXPECT_FRAME(1, kVideoWidth, kVideoHeight);

  EXPECT_THAT(tracker.GetSources(), SizeIs(1));

  RtpSource source = tracker.GetSources()[0];
  EXPECT_EQ(source.source_id(), kSsrc);
  EXPECT_EQ(source.source_type(), RtpSourceType::SSRC);
  int64_t rtp_timestamp_1 = source.rtp_timestamp();
  Timestamp timestamp_1 = source.timestamp();

  // Send and receive another frame.
  SendFrame();
  EXPECT_FRAME(2, kVideoWidth, kVideoHeight);

  EXPECT_THAT(tracker.GetSources(), SizeIs(1));

  source = tracker.GetSources()[0];
  EXPECT_EQ(source.source_id(), kSsrc);
  EXPECT_EQ(source.source_type(), RtpSourceType::SSRC);
  int64_t rtp_timestamp_2 = source.rtp_timestamp();
  Timestamp timestamp_2 = source.timestamp();

  EXPECT_GT(rtp_timestamp_2, rtp_timestamp_1);
  EXPECT_GE(timestamp_2, timestamp_1);
}

TEST_F(WebRtcVideoChannelTest, SetsRidsOnSendStream) {
  StreamParams sp = CreateSimStreamParams("cname", {123, 456, 789});

  std::vector<std::string> rids = {"f", "h", "q"};
  std::vector<RidDescription> rid_descriptions;
  for (const auto& rid : rids) {
    rid_descriptions.emplace_back(rid, RidDirection::kSend);
  }
  sp.set_rids(rid_descriptions);

  ASSERT_TRUE(send_channel_->AddSendStream(sp));
  const auto& streams = fake_call_->GetVideoSendStreams();
  ASSERT_EQ(1u, streams.size());
  auto stream = streams[0];
  ASSERT_NE(stream, nullptr);
  const auto& config = stream->GetConfig();
  EXPECT_THAT(config.rtp.rids, ElementsAreArray(rids));
}

TEST_F(WebRtcVideoChannelBaseTest, EncoderSelectorSwitchCodec) {
  Codec vp9 = GetEngineCodec("VP9");

  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  parameters.codecs.push_back(vp9);
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  send_channel_->SetSend(true);

  RtpParameters rtp_parameters = send_channel_->GetRtpSendParameters(kSsrc);
  ASSERT_EQ(1u, rtp_parameters.encodings.size());
  rtp_parameters.encodings[0].max_bitrate_bps = 1000000;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(kSsrc, rtp_parameters).ok());

  std::optional<Codec> codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP8", codec->name);

  scoped_refptr<MockEncoderSelector> encoder_selector =
      make_ref_counted<MockEncoderSelector>();
  EXPECT_CALL(*encoder_selector, OnAvailableBitrate)
      .WillRepeatedly(Return(SdpVideoFormat::VP9Profile0()));

  send_channel_->SetEncoderSelector(kSsrc, encoder_selector);
  time_controller_.AdvanceTime(kFrameDuration);

  codec = send_channel_->GetSendCodec();
  ASSERT_TRUE(codec);
  EXPECT_EQ("VP9", codec->name);

  // Deregister the encoder selector in case it's called during test tear-down.
  send_channel_->SetEncoderSelector(kSsrc, nullptr);
}

TEST_F(WebRtcVideoChannelTest, ScaleResolutionDownToSinglecast) {
  VideoSenderParameters parameters;
  FakeVideoSendStream* stream = AddSendStream();
  const uint32_t ssrc = stream->GetConfig().rtp.ssrcs[0];
  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 30,
                               env_.clock().CurrentTime());
  EXPECT_TRUE(send_channel_->SetVideoSend(ssrc, nullptr, &frame_forwarder));
  frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());

  {  // TEST scale_resolution_down_to < frame size
    RtpParameters rtp_parameters = send_channel_->GetRtpSendParameters(ssrc);
    EXPECT_EQ(1UL, rtp_parameters.encodings.size());
    rtp_parameters.encodings[0].scale_resolution_down_to = {.width = 640,
                                                            .height = 360};
    send_channel_->SetRtpSendParameters(ssrc, rtp_parameters);

    frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());

    auto streams = stream->GetVideoStreams();
    ASSERT_EQ(streams.size(), 1u);
    EXPECT_EQ(checked_cast<size_t>(640), streams[0].width);
    EXPECT_EQ(checked_cast<size_t>(360), streams[0].height);
  }

  {  // TEST scale_resolution_down_to == frame size
    auto rtp_parameters = send_channel_->GetRtpSendParameters(ssrc);
    EXPECT_EQ(1UL, rtp_parameters.encodings.size());
    rtp_parameters.encodings[0].scale_resolution_down_to = {.width = 1280,
                                                            .height = 720};
    send_channel_->SetRtpSendParameters(ssrc, rtp_parameters);

    frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
    auto streams = stream->GetVideoStreams();
    ASSERT_EQ(streams.size(), 1u);
    EXPECT_EQ(checked_cast<size_t>(1280), streams[0].width);
    EXPECT_EQ(checked_cast<size_t>(720), streams[0].height);
  }

  {  // TEST scale_resolution_down_to > frame size
    auto rtp_parameters = send_channel_->GetRtpSendParameters(ssrc);
    EXPECT_EQ(1UL, rtp_parameters.encodings.size());
    rtp_parameters.encodings[0].scale_resolution_down_to = {.width = 2 * 1280,
                                                            .height = 2 * 720};
    send_channel_->SetRtpSendParameters(ssrc, rtp_parameters);

    frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());
    auto streams = stream->GetVideoStreams();
    ASSERT_EQ(streams.size(), 1u);
    EXPECT_EQ(checked_cast<size_t>(1280), streams[0].width);
    EXPECT_EQ(checked_cast<size_t>(720), streams[0].height);
  }

  EXPECT_TRUE(send_channel_->SetVideoSend(ssrc, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest, ScaleResolutionDownToSinglecastScaling) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  FakeVideoSendStream* stream = AddSendStream();
  const uint32_t ssrc = stream->GetConfig().rtp.ssrcs[0];
  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 30,
                               env_.clock().CurrentTime());
  EXPECT_TRUE(send_channel_->SetVideoSend(ssrc, nullptr, &frame_forwarder));

  {
    auto rtp_parameters = send_channel_->GetRtpSendParameters(ssrc);
    EXPECT_EQ(1UL, rtp_parameters.encodings.size());
    rtp_parameters.encodings[0].scale_resolution_down_to = {.width = 720,
                                                            .height = 720};
    send_channel_->SetRtpSendParameters(ssrc, rtp_parameters);

    frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());

    auto streams = stream->GetVideoStreams();
    ASSERT_EQ(streams.size(), 1u);
    // The scaling factor is 720/1280 because of orientation,
    // scaling the height (720) by this value gets you 405p.
    EXPECT_EQ(checked_cast<size_t>(720), streams[0].width);
    EXPECT_EQ(checked_cast<size_t>(405), streams[0].height);
  }

  {
    auto rtp_parameters = send_channel_->GetRtpSendParameters(ssrc);
    EXPECT_EQ(1UL, rtp_parameters.encodings.size());
    rtp_parameters.encodings[0].scale_resolution_down_to = {.width = 1280,
                                                            .height = 1280};
    send_channel_->SetRtpSendParameters(ssrc, rtp_parameters);

    frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());

    auto streams = stream->GetVideoStreams();
    ASSERT_EQ(streams.size(), 1u);
    // No downscale needed to fit 1280x1280.
    EXPECT_EQ(checked_cast<size_t>(1280), streams[0].width);
    EXPECT_EQ(checked_cast<size_t>(720), streams[0].height);
  }

  {
    auto rtp_parameters = send_channel_->GetRtpSendParameters(ssrc);
    EXPECT_EQ(1UL, rtp_parameters.encodings.size());
    rtp_parameters.encodings[0].scale_resolution_down_to = {.width = 650,
                                                            .height = 650};
    send_channel_->SetRtpSendParameters(ssrc, rtp_parameters);

    auto streams = stream->GetVideoStreams();
    ASSERT_EQ(streams.size(), 1u);
    // The scaling factor is 650/1280 because of orientation,
    // scaling the height (720) by this value gets you 365.625 which is rounded.
    EXPECT_EQ(checked_cast<size_t>(650), streams[0].width);
    EXPECT_EQ(checked_cast<size_t>(366), streams[0].height);
  }

  {
    auto rtp_parameters = send_channel_->GetRtpSendParameters(ssrc);
    EXPECT_EQ(1UL, rtp_parameters.encodings.size());
    rtp_parameters.encodings[0].scale_resolution_down_to = {.width = 2560,
                                                            .height = 1440};
    send_channel_->SetRtpSendParameters(ssrc, rtp_parameters);

    auto streams = stream->GetVideoStreams();
    ASSERT_EQ(streams.size(), 1u);
    // We don't upscale.
    EXPECT_EQ(checked_cast<size_t>(1280), streams[0].width);
    EXPECT_EQ(checked_cast<size_t>(720), streams[0].height);
  }

  EXPECT_TRUE(send_channel_->SetVideoSend(ssrc, nullptr, nullptr));
}

TEST_F(WebRtcVideoChannelTest, ScaleResolutionDownToSimulcast) {
  VideoSenderParameters parameters;
  parameters.codecs.push_back(GetEngineCodec("VP8"));
  ASSERT_TRUE(send_channel_->SetSenderParameters(parameters));

  FakeVideoSendStream* stream = SetUpSimulcast(true, /*with_rtx=*/false);
  FrameForwarder frame_forwarder;
  FakeFrameSource frame_source(1280, 720, TimeDelta::Seconds(1) / 30,
                               env_.clock().CurrentTime());
  EXPECT_TRUE(
      send_channel_->SetVideoSend(last_ssrc_, nullptr, &frame_forwarder));

  {
    RtpParameters rtp_parameters =
        send_channel_->GetRtpSendParameters(last_ssrc_);
    EXPECT_EQ(3UL, rtp_parameters.encodings.size());
    rtp_parameters.encodings[0].scale_resolution_down_to = {.width = 320,
                                                            .height = 180};
    rtp_parameters.encodings[1].scale_resolution_down_to = {.width = 640,
                                                            .height = 360};
    rtp_parameters.encodings[2].scale_resolution_down_to = {.width = 1280,
                                                            .height = 720};
    send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters);

    frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());

    EXPECT_EQ(GetStreamResolutions(stream->GetVideoStreams()),
              (std::vector<Resolution>{
                  {.width = 320, .height = 180},
                  {.width = 640, .height = 360},
                  {.width = 1280, .height = 720},
              }));
  }

  {
    RtpParameters rtp_parameters =
        send_channel_->GetRtpSendParameters(last_ssrc_);
    EXPECT_EQ(3UL, rtp_parameters.encodings.size());
    rtp_parameters.encodings[0].scale_resolution_down_to = {.width = 320,
                                                            .height = 180};
    rtp_parameters.encodings[1].active = false;

    rtp_parameters.encodings[2].scale_resolution_down_to = {.width = 1280,
                                                            .height = 720};
    send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters);

    frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());

    EXPECT_EQ(GetStreamResolutions(stream->GetVideoStreams()),
              (std::vector<Resolution>{
                  {.width = 320, .height = 180},
                  {.width = 1280, .height = 720},
              }));
  }

  {
    RtpParameters rtp_parameters =
        send_channel_->GetRtpSendParameters(last_ssrc_);
    EXPECT_EQ(3UL, rtp_parameters.encodings.size());
    rtp_parameters.encodings[0].scale_resolution_down_to = {.width = 320,
                                                            .height = 180};
    rtp_parameters.encodings[1].active = true;
    rtp_parameters.encodings[1].scale_resolution_down_to = {.width = 640,
                                                            .height = 360};
    rtp_parameters.encodings[2].scale_resolution_down_to = {.width = 960,
                                                            .height = 540};
    send_channel_->SetRtpSendParameters(last_ssrc_, rtp_parameters);

    frame_forwarder.IncomingCapturedFrame(frame_source.GetFrame());

    EXPECT_EQ(GetStreamResolutions(stream->GetVideoStreams()),
              (std::vector<Resolution>{
                  {.width = 320, .height = 180},
                  {.width = 640, .height = 360},
                  {.width = 960, .height = 540},
              }));
  }

  EXPECT_TRUE(send_channel_->SetVideoSend(last_ssrc_, nullptr, nullptr));
}

}  // namespace
}  // namespace webrtc
