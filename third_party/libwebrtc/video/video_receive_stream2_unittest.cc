/*
 *  Copyright 2017 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/video_receive_stream2.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <limits>
#include <memory>
#include <ostream>
#include <queue>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/types/optional.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/metronome/test/fake_metronome.h"
#include "api/test/mock_video_decoder.h"
#include "api/test/mock_video_decoder_factory.h"
#include "api/test/time_controller.h"
#include "api/units/frequency.h"
#include "api/units/time_delta.h"
#include "api/video/encoded_image.h"
#include "api/video/recordable_encoded_frame.h"
#include "api/video/test/video_frame_matchers.h"
#include "api/video/video_frame.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_decoder.h"
#include "call/rtp_stream_receiver_controller.h"
#include "call/video_receive_stream.h"
#include "common_video/test/utilities.h"
#include "media/engine/fake_webrtc_call.h"
#include "modules/pacing/packet_router.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "modules/video_coding/encoded_frame.h"
#include "rtc_base/logging.h"
#include "system_wrappers/include/clock.h"
#include "test/fake_decoder.h"
#include "test/fake_encoded_frame.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/mock_transport.h"
#include "test/rtcp_packet_parser.h"
#include "test/time_controller/simulated_time_controller.h"
#include "test/video_decoder_proxy_factory.h"
#include "video/call_stats2.h"

namespace webrtc {

// Printing SdpVideoFormat for gmock argument matchers.
void PrintTo(const SdpVideoFormat& value, std::ostream* os) {
  *os << value.ToString();
}

void PrintTo(const RecordableEncodedFrame::EncodedResolution& value,
             std::ostream* os) {
  *os << value.width << "x" << value.height;
}

void PrintTo(const RecordableEncodedFrame& value, std::ostream* os) {
  *os << "RecordableEncodedFrame(render_time=" << value.render_time()
      << " resolution=" << ::testing::PrintToString(value.resolution()) << ")";
}

}  // namespace webrtc

namespace webrtc {

namespace {

using test::video_frame_matchers::NtpTimestamp;
using test::video_frame_matchers::PacketInfos;
using test::video_frame_matchers::Rotation;
using ::testing::_;
using ::testing::AllOf;
using ::testing::AnyNumber;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Field;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::IsEmpty;
using ::testing::Optional;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::Return;
using ::testing::SizeIs;
using ::testing::WithoutArgs;

auto RenderedFrameWith(::testing::Matcher<VideoFrame> m) {
  return Optional(m);
}
auto RenderedFrame() {
  return RenderedFrameWith(_);
}
testing::Matcher<absl::optional<VideoFrame>> DidNotReceiveFrame() {
  return Eq(absl::nullopt);
}

constexpr TimeDelta kDefaultTimeOut = TimeDelta::Millis(50);
constexpr int kDefaultNumCpuCores = 2;

constexpr Timestamp kStartTime = Timestamp::Millis(1'337'000);
constexpr Frequency k30Fps = Frequency::Hertz(30);
constexpr TimeDelta k30FpsDelay = 1 / k30Fps;
constexpr Frequency kRtpTimestampHz = Frequency::KiloHertz(90);
constexpr uint32_t k30FpsRtpTimestampDelta = kRtpTimestampHz / k30Fps;
constexpr uint32_t kFirstRtpTimestamp = 90000;

class FakeVideoRenderer : public rtc::VideoSinkInterface<VideoFrame> {
 public:
  explicit FakeVideoRenderer(TimeController* time_controller)
      : time_controller_(time_controller) {}
  ~FakeVideoRenderer() override = default;

  void OnFrame(const VideoFrame& frame) override {
    RTC_LOG(LS_VERBOSE) << "Received frame with timestamp="
                        << frame.rtp_timestamp();
    if (!last_frame_.empty()) {
      RTC_LOG(LS_INFO) << "Already had frame queue with timestamp="
                       << last_frame_.back().rtp_timestamp();
    }
    last_frame_.push_back(frame);
  }

  // If `advance_time`, then the clock will always advance by `timeout`.
  absl::optional<VideoFrame> WaitForFrame(TimeDelta timeout,
                                          bool advance_time = false) {
    auto start = time_controller_->GetClock()->CurrentTime();
    if (last_frame_.empty()) {
      time_controller_->AdvanceTime(TimeDelta::Zero());
      time_controller_->Wait([this] { return !last_frame_.empty(); }, timeout);
    }
    absl::optional<VideoFrame> ret;
    if (!last_frame_.empty()) {
      ret = last_frame_.front();
      last_frame_.pop_front();
    }
    if (advance_time) {
      time_controller_->AdvanceTime(
          timeout - (time_controller_->GetClock()->CurrentTime() - start));
    }
    return ret;
  }

 private:
  std::deque<VideoFrame> last_frame_;
  TimeController* const time_controller_;
};

MATCHER_P2(MatchResolution, w, h, "") {
  return arg.resolution().width == w && arg.resolution().height == h;
}

MATCHER_P(RtpTimestamp, timestamp, "") {
  if (arg.rtp_timestamp() != timestamp) {
    *result_listener->stream()
        << "rtp timestamp was " << arg.rtp_timestamp() << " != " << timestamp;
    return false;
  }
  return true;
}

// Rtp timestamp for in order frame at 30fps.
uint32_t RtpTimestampForFrame(int id) {
  return kFirstRtpTimestamp + id * k30FpsRtpTimestampDelta;
}

// Receive time for in order frame at 30fps.
Timestamp ReceiveTimeForFrame(int id) {
  return kStartTime + id * k30FpsDelay;
}

}  // namespace

class VideoReceiveStream2Test : public ::testing::TestWithParam<bool> {
 public:
  auto DefaultDecodeAction() {
    return Invoke(&fake_decoder_, &test::FakeDecoder::Decode);
  }

  bool UseMetronome() const { return GetParam(); }

  VideoReceiveStream2Test()
      : time_controller_(kStartTime),
        env_(CreateEnvironment(time_controller_.CreateTaskQueueFactory(),
                               time_controller_.GetClock())),
        config_(&mock_transport_, &mock_h264_decoder_factory_),
        call_stats_(&env_.clock(), time_controller_.GetMainThread()),
        fake_renderer_(&time_controller_),
        fake_call_(env_),
        fake_metronome_(TimeDelta::Millis(16)),
        decode_sync_(&env_.clock(),
                     &fake_metronome_,
                     time_controller_.GetMainThread()),
        h264_decoder_factory_(&mock_decoder_) {
    // By default, mock decoder factory is backed by VideoDecoderProxyFactory.
    ON_CALL(mock_h264_decoder_factory_, Create)
        .WillByDefault(Invoke(&h264_decoder_factory_,
                              &test::VideoDecoderProxyFactory::Create));

    // By default, mock decode will wrap the fake decoder.
    ON_CALL(mock_decoder_, Configure)
        .WillByDefault(Invoke(&fake_decoder_, &test::FakeDecoder::Configure));
    ON_CALL(mock_decoder_, Decode(_, _)).WillByDefault(DefaultDecodeAction());
    ON_CALL(mock_decoder_, RegisterDecodeCompleteCallback)
        .WillByDefault(
            Invoke(&fake_decoder_,
                   &test::FakeDecoder::RegisterDecodeCompleteCallback));
    ON_CALL(mock_decoder_, Release)
        .WillByDefault(Invoke(&fake_decoder_, &test::FakeDecoder::Release));
    ON_CALL(mock_transport_, SendRtcp)
        .WillByDefault(
            Invoke(&rtcp_packet_parser_, &test::RtcpPacketParser::Parse));
  }

  ~VideoReceiveStream2Test() override {
    if (video_receive_stream_) {
      video_receive_stream_->Stop();
      video_receive_stream_->UnregisterFromTransport();
    }
    time_controller_.AdvanceTime(TimeDelta::Zero());
  }

  void SetUp() override {
    config_.rtp.remote_ssrc = 1111;
    config_.rtp.local_ssrc = 2222;
    config_.renderer = &fake_renderer_;
    VideoReceiveStreamInterface::Decoder h264_decoder;
    h264_decoder.payload_type = 99;
    h264_decoder.video_format = SdpVideoFormat::H264();
    h264_decoder.video_format.parameters.insert(
        {"sprop-parameter-sets", "Z0IACpZTBYmI,aMljiA=="});
    VideoReceiveStreamInterface::Decoder h265_decoder;
    h265_decoder.payload_type = 100;
    h265_decoder.video_format = SdpVideoFormat("H265");

    config_.decoders = {h265_decoder, h264_decoder};

    RecreateReceiveStream();
  }

  void RecreateReceiveStream(
      absl::optional<VideoReceiveStreamInterface::RecordingState> state =
          absl::nullopt) {
    if (video_receive_stream_) {
      video_receive_stream_->UnregisterFromTransport();
      video_receive_stream_ = nullptr;
    }
    timing_ = new VCMTiming(&env_.clock(), env_.field_trials());
    video_receive_stream_ =
        std::make_unique<webrtc::internal::VideoReceiveStream2>(
            env_, &fake_call_, kDefaultNumCpuCores, &packet_router_,
            config_.Copy(), &call_stats_, absl::WrapUnique(timing_),
            &nack_periodic_processor_,
            UseMetronome() ? &decode_sync_ : nullptr);
    video_receive_stream_->RegisterWithTransport(
        &rtp_stream_receiver_controller_);
    if (state)
      video_receive_stream_->SetAndGetRecordingState(std::move(*state), false);
  }

 protected:
  GlobalSimulatedTimeController time_controller_;
  Environment env_;
  NackPeriodicProcessor nack_periodic_processor_;
  testing::NiceMock<MockVideoDecoderFactory> mock_h264_decoder_factory_;
  VideoReceiveStreamInterface::Config config_;
  internal::CallStats call_stats_;
  testing::NiceMock<MockVideoDecoder> mock_decoder_;
  FakeVideoRenderer fake_renderer_;
  cricket::FakeCall fake_call_;
  MockTransport mock_transport_;
  test::RtcpPacketParser rtcp_packet_parser_;
  PacketRouter packet_router_;
  RtpStreamReceiverController rtp_stream_receiver_controller_;
  std::unique_ptr<webrtc::internal::VideoReceiveStream2> video_receive_stream_;
  VCMTiming* timing_;
  test::FakeMetronome fake_metronome_;
  DecodeSynchronizer decode_sync_;

 private:
  test::VideoDecoderProxyFactory h264_decoder_factory_;
  test::FakeDecoder fake_decoder_;
};

TEST_P(VideoReceiveStream2Test, CreateFrameFromH264FmtpSpropAndIdr) {
  constexpr uint8_t idr_nalu[] = {0x05, 0xFF, 0xFF, 0xFF};
  RtpPacketToSend rtppacket(nullptr);
  uint8_t* payload = rtppacket.AllocatePayload(sizeof(idr_nalu));
  memcpy(payload, idr_nalu, sizeof(idr_nalu));
  rtppacket.SetMarker(true);
  rtppacket.SetSsrc(1111);
  rtppacket.SetPayloadType(99);
  rtppacket.SetSequenceNumber(1);
  rtppacket.SetTimestamp(0);
  EXPECT_CALL(mock_decoder_, RegisterDecodeCompleteCallback(_));
  video_receive_stream_->Start();
  EXPECT_CALL(mock_decoder_, Decode(_, _));
  RtpPacketReceived parsed_packet;
  ASSERT_TRUE(parsed_packet.Parse(rtppacket.data(), rtppacket.size()));
  rtp_stream_receiver_controller_.OnRtpPacket(parsed_packet);
  EXPECT_CALL(mock_decoder_, Release());

  time_controller_.AdvanceTime(TimeDelta::Zero());
}

TEST_P(VideoReceiveStream2Test, PlayoutDelay) {
  const VideoPlayoutDelay kPlayoutDelay(TimeDelta::Millis(123),
                                        TimeDelta::Millis(321));
  std::unique_ptr<test::FakeEncodedFrame> test_frame =
      test::FakeFrameBuilder()
          .Id(0)
          .PlayoutDelay(kPlayoutDelay)
          .AsLast()
          .Build();

  video_receive_stream_->OnCompleteFrame(std::move(test_frame));
  auto timings = timing_->GetTimings();
  EXPECT_EQ(kPlayoutDelay.min(), timings.min_playout_delay);
  EXPECT_EQ(kPlayoutDelay.max(), timings.max_playout_delay);

  // Check that the biggest minimum delay is chosen.
  video_receive_stream_->SetMinimumPlayoutDelay(400);
  timings = timing_->GetTimings();
  EXPECT_EQ(400, timings.min_playout_delay.ms());

  // Check base minimum delay validation.
  EXPECT_FALSE(video_receive_stream_->SetBaseMinimumPlayoutDelayMs(12345));
  EXPECT_FALSE(video_receive_stream_->SetBaseMinimumPlayoutDelayMs(-1));
  EXPECT_TRUE(video_receive_stream_->SetBaseMinimumPlayoutDelayMs(500));
  timings = timing_->GetTimings();
  EXPECT_EQ(500, timings.min_playout_delay.ms());

  // Check that intermidiate values are remembered and the biggest remembered
  // is chosen.
  video_receive_stream_->SetBaseMinimumPlayoutDelayMs(0);
  timings = timing_->GetTimings();
  EXPECT_EQ(400, timings.min_playout_delay.ms());

  video_receive_stream_->SetMinimumPlayoutDelay(0);
  timings = timing_->GetTimings();
  EXPECT_EQ(123, timings.min_playout_delay.ms());
}

TEST_P(VideoReceiveStream2Test, RenderParametersSetToDefaultValues) {
  // Default render parameters.
  const VideoFrame::RenderParameters kDefaultRenderParameters;
  // Default with no playout delay set.
  std::unique_ptr<test::FakeEncodedFrame> test_frame0 =
      test::FakeFrameBuilder().Id(0).AsLast().Build();
  video_receive_stream_->OnCompleteFrame(std::move(test_frame0));
  EXPECT_EQ(timing_->RenderParameters(), kDefaultRenderParameters);
}

TEST_P(VideoReceiveStream2Test, UseLowLatencyRenderingSetFromPlayoutDelay) {
  std::unique_ptr<test::FakeEncodedFrame> test_frame0 =
      test::FakeFrameBuilder()
          .Id(0)
          .PlayoutDelay(VideoPlayoutDelay::Minimal())
          .AsLast()
          .Build();
  video_receive_stream_->OnCompleteFrame(std::move(test_frame0));
  EXPECT_TRUE(timing_->RenderParameters().use_low_latency_rendering);

  std::unique_ptr<test::FakeEncodedFrame> test_frame1 =
      test::FakeFrameBuilder()
          .Id(1)
          .PlayoutDelay({TimeDelta::Zero(), TimeDelta::Millis(500)})
          .AsLast()
          .Build();
  video_receive_stream_->OnCompleteFrame(std::move(test_frame1));
  EXPECT_TRUE(timing_->RenderParameters().use_low_latency_rendering);
}

TEST_P(VideoReceiveStream2Test, MaxCompositionDelaySetFromMaxPlayoutDelay) {
  // The max composition delay is dependent on the number of frames in the
  // pre-decode queue. It's therefore important to advance the time as the test
  // runs to get the correct expectations of max_composition_delay_in_frames.
  video_receive_stream_->Start();
  // Max composition delay not set if no playout delay is set.
  std::unique_ptr<test::FakeEncodedFrame> test_frame0 =
      test::FakeFrameBuilder()
          .Id(0)
          .Time(RtpTimestampForFrame(0))
          .ReceivedTime(ReceiveTimeForFrame(0))
          .AsLast()
          .Build();
  video_receive_stream_->OnCompleteFrame(std::move(test_frame0));
  EXPECT_THAT(timing_->RenderParameters().max_composition_delay_in_frames,
              Eq(absl::nullopt));
  time_controller_.AdvanceTime(k30FpsDelay);

  // Max composition delay not set for playout delay 0,0.
  std::unique_ptr<test::FakeEncodedFrame> test_frame1 =
      test::FakeFrameBuilder()
          .Id(1)
          .Time(RtpTimestampForFrame(1))
          .ReceivedTime(ReceiveTimeForFrame(1))
          .PlayoutDelay(VideoPlayoutDelay::Minimal())
          .AsLast()
          .Build();
  video_receive_stream_->OnCompleteFrame(std::move(test_frame1));
  EXPECT_THAT(timing_->RenderParameters().max_composition_delay_in_frames,
              Eq(absl::nullopt));
  time_controller_.AdvanceTime(k30FpsDelay);

  // Max composition delay not set for playout delay X,Y, where X,Y>0.
  std::unique_ptr<test::FakeEncodedFrame> test_frame2 =
      test::FakeFrameBuilder()
          .Id(2)
          .Time(RtpTimestampForFrame(2))
          .ReceivedTime(ReceiveTimeForFrame(2))
          .PlayoutDelay({TimeDelta::Millis(10), TimeDelta::Millis(30)})
          .AsLast()
          .Build();
  video_receive_stream_->OnCompleteFrame(std::move(test_frame2));
  EXPECT_THAT(timing_->RenderParameters().max_composition_delay_in_frames,
              Eq(absl::nullopt));

  time_controller_.AdvanceTime(k30FpsDelay);

  // Max composition delay set if playout delay X,Y, where X=0,Y>0.
  const int kExpectedMaxCompositionDelayInFrames = 3;  // ~50 ms at 60 fps.
  std::unique_ptr<test::FakeEncodedFrame> test_frame3 =
      test::FakeFrameBuilder()
          .Id(3)
          .Time(RtpTimestampForFrame(3))
          .ReceivedTime(ReceiveTimeForFrame(3))
          .PlayoutDelay({TimeDelta::Zero(), TimeDelta::Millis(50)})
          .AsLast()
          .Build();
  video_receive_stream_->OnCompleteFrame(std::move(test_frame3));
  EXPECT_THAT(timing_->RenderParameters().max_composition_delay_in_frames,
              Optional(kExpectedMaxCompositionDelayInFrames));
}

TEST_P(VideoReceiveStream2Test, LazyDecoderCreation) {
  constexpr uint8_t idr_nalu[] = {0x05, 0xFF, 0xFF, 0xFF};
  RtpPacketToSend rtppacket(nullptr);
  uint8_t* payload = rtppacket.AllocatePayload(sizeof(idr_nalu));
  memcpy(payload, idr_nalu, sizeof(idr_nalu));
  rtppacket.SetMarker(true);
  rtppacket.SetSsrc(1111);
  // H265 payload type.
  rtppacket.SetPayloadType(99);
  rtppacket.SetSequenceNumber(1);
  rtppacket.SetTimestamp(0);

  // No decoders are created by default.
  EXPECT_CALL(mock_h264_decoder_factory_, Create).Times(0);
  video_receive_stream_->Start();
  time_controller_.AdvanceTime(TimeDelta::Zero());

  EXPECT_TRUE(
      testing::Mock::VerifyAndClearExpectations(&mock_h264_decoder_factory_));
  // Verify that the decoder is created when we receive payload data and tries
  // to decode a frame.
  EXPECT_CALL(mock_h264_decoder_factory_,
              Create(_, Field(&SdpVideoFormat::name, Eq("H264"))));
  EXPECT_CALL(mock_decoder_, Configure);
  EXPECT_CALL(mock_decoder_, RegisterDecodeCompleteCallback);
  EXPECT_CALL(mock_decoder_, Decode(_, _));
  RtpPacketReceived parsed_packet;
  ASSERT_TRUE(parsed_packet.Parse(rtppacket.data(), rtppacket.size()));
  rtp_stream_receiver_controller_.OnRtpPacket(parsed_packet);
  EXPECT_CALL(mock_decoder_, Release);

  // Make sure the decoder thread had a chance to run.
  time_controller_.AdvanceTime(TimeDelta::Zero());
}

TEST_P(VideoReceiveStream2Test, PassesNtpTime) {
  const Timestamp kNtpTimestamp = Timestamp::Millis(12345);
  std::unique_ptr<test::FakeEncodedFrame> test_frame =
      test::FakeFrameBuilder()
          .Id(0)
          .PayloadType(99)
          .NtpTime(kNtpTimestamp)
          .AsLast()
          .Build();

  video_receive_stream_->Start();
  video_receive_stream_->OnCompleteFrame(std::move(test_frame));
  EXPECT_THAT(fake_renderer_.WaitForFrame(kDefaultTimeOut),
              RenderedFrameWith(NtpTimestamp(kNtpTimestamp)));
}

TEST_P(VideoReceiveStream2Test, PassesRotation) {
  const webrtc::VideoRotation kRotation = webrtc::kVideoRotation_180;
  std::unique_ptr<test::FakeEncodedFrame> test_frame = test::FakeFrameBuilder()
                                                           .Id(0)
                                                           .PayloadType(99)
                                                           .Rotation(kRotation)
                                                           .AsLast()
                                                           .Build();

  video_receive_stream_->Start();
  video_receive_stream_->OnCompleteFrame(std::move(test_frame));
  EXPECT_THAT(fake_renderer_.WaitForFrame(kDefaultTimeOut),
              RenderedFrameWith(Rotation(kRotation)));
}

TEST_P(VideoReceiveStream2Test, PassesPacketInfos) {
  RtpPacketInfos packet_infos = CreatePacketInfos(3);
  auto test_frame = test::FakeFrameBuilder()
                        .Id(0)
                        .PayloadType(99)
                        .PacketInfos(packet_infos)
                        .AsLast()
                        .Build();

  video_receive_stream_->Start();
  video_receive_stream_->OnCompleteFrame(std::move(test_frame));
  EXPECT_THAT(fake_renderer_.WaitForFrame(kDefaultTimeOut),
              RenderedFrameWith(PacketInfos(ElementsAreArray(packet_infos))));
}

TEST_P(VideoReceiveStream2Test, RenderedFrameUpdatesGetSources) {
  constexpr uint32_t kSsrc = 1111;
  constexpr uint32_t kCsrc = 9001;
  constexpr uint32_t kRtpTimestamp = 12345;

  // Prepare one video frame with per-packet information.
  auto test_frame =
      test::FakeFrameBuilder().Id(0).PayloadType(99).AsLast().Build();
  RtpPacketInfos packet_infos;
  {
    RtpPacketInfos::vector_type infos;

    RtpPacketInfo info;
    info.set_ssrc(kSsrc);
    info.set_csrcs({kCsrc});
    info.set_rtp_timestamp(kRtpTimestamp);

    info.set_receive_time(env_.clock().CurrentTime() - TimeDelta::Millis(5000));
    infos.push_back(info);

    info.set_receive_time(env_.clock().CurrentTime() - TimeDelta::Millis(3000));
    infos.push_back(info);

    info.set_receive_time(env_.clock().CurrentTime() - TimeDelta::Millis(2000));
    infos.push_back(info);

    info.set_receive_time(env_.clock().CurrentTime() - TimeDelta::Millis(1000));
    infos.push_back(info);

    packet_infos = RtpPacketInfos(std::move(infos));
  }
  test_frame->SetPacketInfos(packet_infos);

  // Start receive stream.
  video_receive_stream_->Start();
  EXPECT_THAT(video_receive_stream_->GetSources(), IsEmpty());

  // Render one video frame.
  Timestamp timestamp_min = env_.clock().CurrentTime();
  video_receive_stream_->OnCompleteFrame(std::move(test_frame));
  // Verify that the per-packet information is passed to the renderer.
  EXPECT_THAT(fake_renderer_.WaitForFrame(kDefaultTimeOut),
              RenderedFrameWith(PacketInfos(ElementsAreArray(packet_infos))));
  Timestamp timestamp_max = env_.clock().CurrentTime();

  // Verify that the per-packet information also updates `GetSources()`.
  std::vector<RtpSource> sources = video_receive_stream_->GetSources();
  ASSERT_THAT(sources, SizeIs(2));
  {
    auto it = std::find_if(sources.begin(), sources.end(),
                           [](const RtpSource& source) {
                             return source.source_type() == RtpSourceType::SSRC;
                           });
    ASSERT_NE(it, sources.end());

    EXPECT_EQ(it->source_id(), kSsrc);
    EXPECT_EQ(it->source_type(), RtpSourceType::SSRC);
    EXPECT_EQ(it->rtp_timestamp(), kRtpTimestamp);
    EXPECT_GE(it->timestamp(), timestamp_min);
    EXPECT_LE(it->timestamp(), timestamp_max);
  }
  {
    auto it = std::find_if(sources.begin(), sources.end(),
                           [](const RtpSource& source) {
                             return source.source_type() == RtpSourceType::CSRC;
                           });
    ASSERT_NE(it, sources.end());

    EXPECT_EQ(it->source_id(), kCsrc);
    EXPECT_EQ(it->source_type(), RtpSourceType::CSRC);
    EXPECT_EQ(it->rtp_timestamp(), kRtpTimestamp);
    EXPECT_GE(it->timestamp(), timestamp_min);
    EXPECT_LE(it->timestamp(), timestamp_max);
  }
}

std::unique_ptr<test::FakeEncodedFrame> MakeFrameWithResolution(
    VideoFrameType frame_type,
    int picture_id,
    int width,
    int height) {
  auto frame =
      test::FakeFrameBuilder().Id(picture_id).PayloadType(99).AsLast().Build();
  frame->SetFrameType(frame_type);
  frame->_encodedWidth = width;
  frame->_encodedHeight = height;
  return frame;
}

std::unique_ptr<test::FakeEncodedFrame> MakeFrame(VideoFrameType frame_type,
                                                  int picture_id) {
  return MakeFrameWithResolution(frame_type, picture_id, 320, 240);
}

TEST_P(VideoReceiveStream2Test, PassesFrameWhenEncodedFramesCallbackSet) {
  testing::MockFunction<void(const RecordableEncodedFrame&)> callback;
  video_receive_stream_->Start();
  EXPECT_CALL(callback, Call);
  video_receive_stream_->SetAndGetRecordingState(
      VideoReceiveStreamInterface::RecordingState(callback.AsStdFunction()),
      true);
  video_receive_stream_->OnCompleteFrame(
      MakeFrame(VideoFrameType::kVideoFrameKey, 0));
  EXPECT_TRUE(fake_renderer_.WaitForFrame(kDefaultTimeOut));

  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(1));

  video_receive_stream_->Stop();
}

TEST_P(VideoReceiveStream2Test, MovesEncodedFrameDispatchStateWhenReCreating) {
  testing::MockFunction<void(const RecordableEncodedFrame&)> callback;
  video_receive_stream_->Start();
  // Expect a key frame request over RTCP.
  video_receive_stream_->SetAndGetRecordingState(
      VideoReceiveStreamInterface::RecordingState(callback.AsStdFunction()),
      true);
  video_receive_stream_->Stop();
  VideoReceiveStreamInterface::RecordingState old_state =
      video_receive_stream_->SetAndGetRecordingState(
          VideoReceiveStreamInterface::RecordingState(), false);
  RecreateReceiveStream(std::move(old_state));

  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(1));

  video_receive_stream_->Stop();
}

TEST_P(VideoReceiveStream2Test, RequestsKeyFramesUntilKeyFrameReceived) {
  // Recreate receive stream with shorter delay to test rtx.
  TimeDelta rtx_delay = TimeDelta::Millis(50);
  config_.rtp.nack.rtp_history_ms = rtx_delay.ms();
  auto tick = rtx_delay / 2;
  RecreateReceiveStream();
  video_receive_stream_->Start();

  video_receive_stream_->GenerateKeyFrame();
  video_receive_stream_->OnCompleteFrame(
      MakeFrame(VideoFrameType::kVideoFrameDelta, 0));
  fake_renderer_.WaitForFrame(kDefaultTimeOut);
  time_controller_.AdvanceTime(tick);
  video_receive_stream_->OnCompleteFrame(
      MakeFrame(VideoFrameType::kVideoFrameDelta, 1));
  fake_renderer_.WaitForFrame(kDefaultTimeOut);
  time_controller_.AdvanceTime(TimeDelta::Zero());
  testing::Mock::VerifyAndClearExpectations(&mock_transport_);

  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(1));

  // T+keyframetimeout: still no key frame received, expect key frame request
  // sent again.
  time_controller_.AdvanceTime(tick);
  video_receive_stream_->OnCompleteFrame(
      MakeFrame(VideoFrameType::kVideoFrameDelta, 2));
  EXPECT_THAT(fake_renderer_.WaitForFrame(kDefaultTimeOut), RenderedFrame());
  testing::Mock::VerifyAndClearExpectations(&mock_transport_);

  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(2));

  // T+keyframetimeout: now send a key frame - we should not observe new key
  // frame requests after this.
  video_receive_stream_->OnCompleteFrame(
      MakeFrame(VideoFrameType::kVideoFrameKey, 3));
  EXPECT_THAT(fake_renderer_.WaitForFrame(kDefaultTimeOut), RenderedFrame());
  time_controller_.AdvanceTime(2 * tick);
  video_receive_stream_->OnCompleteFrame(
      MakeFrame(VideoFrameType::kVideoFrameDelta, 4));
  EXPECT_THAT(fake_renderer_.WaitForFrame(kDefaultTimeOut), RenderedFrame());

  EXPECT_THAT(rtcp_packet_parser_.pli()->num_packets(), Eq(2));
}

TEST_P(VideoReceiveStream2Test,
       DispatchesEncodedFrameSequenceStartingWithKeyframeWithoutResolution) {
  video_receive_stream_->Start();
  testing::MockFunction<void(const RecordableEncodedFrame&)> callback;
  video_receive_stream_->SetAndGetRecordingState(
      VideoReceiveStreamInterface::RecordingState(callback.AsStdFunction()),
      /*generate_key_frame=*/false);

  InSequence s;
  EXPECT_CALL(callback,
              Call(MatchResolution(test::FakeDecoder::kDefaultWidth,
                                   test::FakeDecoder::kDefaultHeight)));
  EXPECT_CALL(callback, Call);

  video_receive_stream_->OnCompleteFrame(
      MakeFrameWithResolution(VideoFrameType::kVideoFrameKey, 0, 0, 0));
  EXPECT_THAT(fake_renderer_.WaitForFrame(kDefaultTimeOut), RenderedFrame());
  video_receive_stream_->OnCompleteFrame(
      MakeFrameWithResolution(VideoFrameType::kVideoFrameDelta, 1, 0, 0));
  EXPECT_THAT(fake_renderer_.WaitForFrame(kDefaultTimeOut), RenderedFrame());

  video_receive_stream_->Stop();
}

TEST_P(VideoReceiveStream2Test,
       DispatchesEncodedFrameSequenceStartingWithKeyframeWithResolution) {
  video_receive_stream_->Start();
  testing::MockFunction<void(const RecordableEncodedFrame&)> callback;
  video_receive_stream_->SetAndGetRecordingState(
      VideoReceiveStreamInterface::RecordingState(callback.AsStdFunction()),
      /*generate_key_frame=*/false);

  InSequence s;
  EXPECT_CALL(callback, Call(MatchResolution(1080u, 720u)));
  EXPECT_CALL(callback, Call);

  video_receive_stream_->OnCompleteFrame(
      MakeFrameWithResolution(VideoFrameType::kVideoFrameKey, 0, 1080, 720));
  EXPECT_THAT(fake_renderer_.WaitForFrame(kDefaultTimeOut), RenderedFrame());
  video_receive_stream_->OnCompleteFrame(
      MakeFrameWithResolution(VideoFrameType::kVideoFrameDelta, 1, 0, 0));
  EXPECT_THAT(fake_renderer_.WaitForFrame(kDefaultTimeOut), RenderedFrame());

  video_receive_stream_->Stop();
}

TEST_P(VideoReceiveStream2Test, DependantFramesAreScheduled) {
  video_receive_stream_->Start();

  auto key_frame = test::FakeFrameBuilder()
                       .Id(0)
                       .PayloadType(99)
                       .Time(kFirstRtpTimestamp)
                       .ReceivedTime(kStartTime)
                       .AsLast()
                       .Build();
  auto delta_frame = test::FakeFrameBuilder()
                         .Id(1)
                         .PayloadType(99)
                         .Time(RtpTimestampForFrame(1))
                         .ReceivedTime(ReceiveTimeForFrame(1))
                         .Refs({0})
                         .AsLast()
                         .Build();

  // Expect frames are decoded in order.
  InSequence seq;
  EXPECT_CALL(mock_decoder_, Decode(test::RtpTimestamp(kFirstRtpTimestamp), _));
  EXPECT_CALL(mock_decoder_, Decode(test::RtpTimestamp(kFirstRtpTimestamp +
                                                       k30FpsRtpTimestampDelta),
                                    _))
      .Times(1);
  video_receive_stream_->OnCompleteFrame(std::move(key_frame));
  EXPECT_THAT(fake_renderer_.WaitForFrame(TimeDelta::Zero()), RenderedFrame());

  time_controller_.AdvanceTime(k30FpsDelay);
  video_receive_stream_->OnCompleteFrame(std::move(delta_frame));
  EXPECT_THAT(fake_renderer_.WaitForFrame(k30FpsDelay), RenderedFrame());

  video_receive_stream_->Stop();
}

TEST_P(VideoReceiveStream2Test, FramesScheduledInOrder) {
  video_receive_stream_->Start();

  auto key_frame = test::FakeFrameBuilder()
                       .Id(0)
                       .PayloadType(99)
                       .Time(kFirstRtpTimestamp)
                       .AsLast()
                       .Build();
  auto delta_frame1 = test::FakeFrameBuilder()
                          .Id(1)
                          .PayloadType(99)
                          .Time(RtpTimestampForFrame(1))
                          .Refs({0})
                          .AsLast()
                          .Build();
  auto delta_frame2 = test::FakeFrameBuilder()
                          .Id(2)
                          .PayloadType(99)
                          .Time(RtpTimestampForFrame(2))
                          .Refs({1})
                          .AsLast()
                          .Build();

  // Expect frames are decoded in order despite delta_frame1 arriving first.
  InSequence seq;
  EXPECT_CALL(mock_decoder_, Decode(test::RtpTimestamp(kFirstRtpTimestamp), _))
      .Times(1);
  EXPECT_CALL(mock_decoder_,
              Decode(test::RtpTimestamp(RtpTimestampForFrame(1)), _))
      .Times(1);
  EXPECT_CALL(mock_decoder_,
              Decode(test::RtpTimestamp(RtpTimestampForFrame(2)), _))
      .Times(1);
  key_frame->SetReceivedTime(env_.clock().CurrentTime().ms());
  video_receive_stream_->OnCompleteFrame(std::move(key_frame));
  EXPECT_THAT(fake_renderer_.WaitForFrame(TimeDelta::Zero()), RenderedFrame());

  delta_frame2->SetReceivedTime(env_.clock().CurrentTime().ms());
  video_receive_stream_->OnCompleteFrame(std::move(delta_frame2));
  EXPECT_THAT(fake_renderer_.WaitForFrame(k30FpsDelay), DidNotReceiveFrame());
  // `delta_frame1` arrives late.
  delta_frame1->SetReceivedTime(env_.clock().CurrentTime().ms());
  video_receive_stream_->OnCompleteFrame(std::move(delta_frame1));
  EXPECT_THAT(fake_renderer_.WaitForFrame(k30FpsDelay), RenderedFrame());
  EXPECT_THAT(fake_renderer_.WaitForFrame(k30FpsDelay * 2), RenderedFrame());
  video_receive_stream_->Stop();
}

TEST_P(VideoReceiveStream2Test, WaitsforAllSpatialLayers) {
  video_receive_stream_->Start();
  auto sl0 = test::FakeFrameBuilder()
                 .Id(0)
                 .PayloadType(99)
                 .Time(kFirstRtpTimestamp)
                 .ReceivedTime(kStartTime)
                 .Build();
  auto sl1 = test::FakeFrameBuilder()
                 .Id(1)
                 .PayloadType(99)
                 .ReceivedTime(kStartTime)
                 .Time(kFirstRtpTimestamp)
                 .Refs({0})
                 .Build();
  auto sl2 = test::FakeFrameBuilder()
                 .Id(2)
                 .PayloadType(99)
                 .ReceivedTime(kStartTime)
                 .Time(kFirstRtpTimestamp)
                 .Refs({0, 1})
                 .AsLast()
                 .Build();

  // No decodes should be called until `sl2` is received.
  EXPECT_CALL(mock_decoder_, Decode(_, _)).Times(0);
  sl0->SetReceivedTime(env_.clock().CurrentTime().ms());
  video_receive_stream_->OnCompleteFrame(std::move(sl0));
  EXPECT_THAT(fake_renderer_.WaitForFrame(TimeDelta::Zero()),
              DidNotReceiveFrame());
  video_receive_stream_->OnCompleteFrame(std::move(sl1));
  EXPECT_THAT(fake_renderer_.WaitForFrame(TimeDelta::Zero()),
              DidNotReceiveFrame());
  // When `sl2` arrives decode should happen.
  EXPECT_CALL(mock_decoder_, Decode(test::RtpTimestamp(kFirstRtpTimestamp), _))
      .Times(1);
  video_receive_stream_->OnCompleteFrame(std::move(sl2));
  EXPECT_THAT(fake_renderer_.WaitForFrame(TimeDelta::Zero()), RenderedFrame());
  video_receive_stream_->Stop();
}

TEST_P(VideoReceiveStream2Test, FramesFastForwardOnSystemHalt) {
  video_receive_stream_->Start();

  // The frame structure looks like this,
  //   F1
  //   /
  // F0 --> F2
  //
  // In this case we will have a system halt simulated. By the time the system
  // resumes, F1 will be old and so F2 should be decoded.
  auto key_frame = test::FakeFrameBuilder()
                       .Id(0)
                       .PayloadType(99)
                       .Time(kFirstRtpTimestamp)
                       .AsLast()
                       .Build();
  auto ffwd_frame = test::FakeFrameBuilder()
                        .Id(1)
                        .PayloadType(99)
                        .Time(RtpTimestampForFrame(1))
                        .Refs({0})
                        .AsLast()
                        .Build();
  auto rendered_frame = test::FakeFrameBuilder()
                            .Id(2)
                            .PayloadType(99)
                            .Time(RtpTimestampForFrame(2))
                            .Refs({0})
                            .AsLast()
                            .Build();
  InSequence seq;
  EXPECT_CALL(mock_decoder_, Decode(test::RtpTimestamp(kFirstRtpTimestamp), _))
      .WillOnce(testing::DoAll(Invoke([&] {
                                 // System halt will be simulated in the decode.
                                 time_controller_.AdvanceTime(k30FpsDelay * 2);
                               }),
                               DefaultDecodeAction()));
  EXPECT_CALL(mock_decoder_,
              Decode(test::RtpTimestamp(RtpTimestampForFrame(2)), _));
  video_receive_stream_->OnCompleteFrame(std::move(key_frame));
  video_receive_stream_->OnCompleteFrame(std::move(ffwd_frame));
  video_receive_stream_->OnCompleteFrame(std::move(rendered_frame));
  EXPECT_THAT(fake_renderer_.WaitForFrame(TimeDelta::Zero()),
              RenderedFrameWith(RtpTimestamp(RtpTimestampForFrame(0))));
  EXPECT_THAT(fake_renderer_.WaitForFrame(TimeDelta::Zero()),
              RenderedFrameWith(RtpTimestamp(RtpTimestampForFrame(2))));

  // Check stats show correct dropped frames.
  auto stats = video_receive_stream_->GetStats();
  EXPECT_EQ(stats.frames_dropped, 1u);

  video_receive_stream_->Stop();
}

TEST_P(VideoReceiveStream2Test, BetterFrameInsertedWhileWaitingToDecodeFrame) {
  video_receive_stream_->Start();

  auto key_frame = test::FakeFrameBuilder()
                       .Id(0)
                       .PayloadType(99)
                       .Time(kFirstRtpTimestamp)
                       .ReceivedTime(ReceiveTimeForFrame(0))
                       .AsLast()
                       .Build();
  auto f1 = test::FakeFrameBuilder()
                .Id(1)
                .PayloadType(99)
                .Time(RtpTimestampForFrame(1))
                .ReceivedTime(ReceiveTimeForFrame(1))
                .Refs({0})
                .AsLast()
                .Build();
  auto f2 = test::FakeFrameBuilder()
                .Id(2)
                .PayloadType(99)
                .Time(RtpTimestampForFrame(2))
                .ReceivedTime(ReceiveTimeForFrame(2))
                .Refs({0})
                .AsLast()
                .Build();

  video_receive_stream_->OnCompleteFrame(std::move(key_frame));
  EXPECT_THAT(fake_renderer_.WaitForFrame(TimeDelta::Zero()), RenderedFrame());

  InSequence seq;
  EXPECT_CALL(mock_decoder_,
              Decode(test::RtpTimestamp(RtpTimestampForFrame(1)), _))
      .Times(1);
  EXPECT_CALL(mock_decoder_,
              Decode(test::RtpTimestamp(RtpTimestampForFrame(2)), _))
      .Times(1);
  // Simulate f1 arriving after f2 but before f2 is decoded.
  video_receive_stream_->OnCompleteFrame(std::move(f2));
  EXPECT_THAT(fake_renderer_.WaitForFrame(k30FpsDelay), DidNotReceiveFrame());
  video_receive_stream_->OnCompleteFrame(std::move(f1));
  EXPECT_THAT(fake_renderer_.WaitForFrame(k30FpsDelay), RenderedFrame());
  EXPECT_THAT(fake_renderer_.WaitForFrame(k30FpsDelay), RenderedFrame());

  video_receive_stream_->Stop();
}

// Note: This test takes a long time (~10s) to run if the fake metronome is
// active. Since the test needs to wait for the timestamp to rollover, it has a
// fake delay of around 6.5 hours. Even though time is simulated, this will be
// around 1,500,000 metronome tick invocations.
TEST_P(VideoReceiveStream2Test, RtpTimestampWrapAround) {
  EXPECT_CALL(mock_transport_, SendRtcp).Times(AnyNumber());
  video_receive_stream_->Start();

  constexpr uint32_t kBaseRtp = std::numeric_limits<uint32_t>::max() / 2;
  video_receive_stream_->OnCompleteFrame(
      test::FakeFrameBuilder()
          .Id(0)
          .PayloadType(99)
          .Time(kBaseRtp)
          .ReceivedTime(env_.clock().CurrentTime())
          .AsLast()
          .Build());
  EXPECT_THAT(fake_renderer_.WaitForFrame(TimeDelta::Zero()), RenderedFrame());
  time_controller_.AdvanceTime(k30FpsDelay);
  video_receive_stream_->OnCompleteFrame(
      test::FakeFrameBuilder()
          .Id(1)
          .PayloadType(99)
          .Time(kBaseRtp + k30FpsRtpTimestampDelta)
          .ReceivedTime(env_.clock().CurrentTime())
          .AsLast()
          .Build());
  EXPECT_THAT(fake_renderer_.WaitForFrame(k30FpsDelay), RenderedFrame());

  // Pause stream so that RTP timestamp wraps around.
  constexpr uint32_t kLastRtp = kBaseRtp + k30FpsRtpTimestampDelta;
  constexpr uint32_t kWrapAroundRtp =
      kLastRtp + std::numeric_limits<uint32_t>::max() / 2 + 1;
  // Pause for corresponding delay such that RTP timestamp would increase this
  // much at 30fps.
  constexpr TimeDelta kWrapAroundDelay =
      (std::numeric_limits<uint32_t>::max() / 2 + 1) / kRtpTimestampHz;

  time_controller_.AdvanceTime(kWrapAroundDelay);
  video_receive_stream_->OnCompleteFrame(
      test::FakeFrameBuilder()
          .Id(2)
          .PayloadType(99)
          .Time(kWrapAroundRtp)
          .ReceivedTime(env_.clock().CurrentTime())
          .AsLast()
          .Build());
  EXPECT_CALL(mock_decoder_, Decode(test::RtpTimestamp(kWrapAroundRtp), _))
      .Times(1);
  EXPECT_THAT(fake_renderer_.WaitForFrame(TimeDelta::Seconds(1)),
              RenderedFrame());

  video_receive_stream_->Stop();
}

// If a frame was lost causing the stream to become temporarily non-decodable
// and the sender reduces their framerate during this time, the video stream
// should start decoding at the new framerate. However, if the connection is
// poor, a keyframe will take a long time to send. If the timing of the incoming
// frames was not kept up to date with the new framerate while the stream was
// decodable, this late frame will have a large delay as the rtp timestamp of
// this keyframe will look like the frame arrived early if the frame-rate was
// not updated.
TEST_P(VideoReceiveStream2Test, PoorConnectionWithFpsChangeDuringLostFrame) {
  video_receive_stream_->Start();

  constexpr Frequency k15Fps = Frequency::Hertz(15);
  constexpr TimeDelta k15FpsDelay = 1 / k15Fps;
  constexpr uint32_t k15FpsRtpTimestampDelta = kRtpTimestampHz / k15Fps;

  // Initial keyframe and frames at 30fps.
  video_receive_stream_->OnCompleteFrame(
      test::FakeFrameBuilder()
          .Id(0)
          .PayloadType(99)
          .Time(RtpTimestampForFrame(0))
          .ReceivedTime(ReceiveTimeForFrame(0))
          .AsLast()
          .Build());
  EXPECT_THAT(fake_renderer_.WaitForFrame(k30FpsDelay, /*advance_time=*/true),
              RenderedFrameWith(RtpTimestamp(RtpTimestampForFrame(0))));

  video_receive_stream_->OnCompleteFrame(
      test::FakeFrameBuilder()
          .Id(1)
          .PayloadType(99)
          .Time(RtpTimestampForFrame(1))
          .ReceivedTime(ReceiveTimeForFrame(1))
          .Refs({0})
          .AsLast()
          .Build());
  EXPECT_THAT(fake_renderer_.WaitForFrame(k30FpsDelay, /*advance_time=*/true),
              RenderedFrameWith(RtpTimestamp(RtpTimestampForFrame(1))));

  // Simulate lost frame 2, followed by 2 second of frames at 30fps, followed by
  // 2 second of frames at 15 fps, and then a keyframe.
  time_controller_.AdvanceTime(k30FpsDelay);

  Timestamp send_30fps_end_time =
      env_.clock().CurrentTime() + TimeDelta::Seconds(2);
  int id = 3;
  EXPECT_CALL(mock_transport_, SendRtcp).Times(AnyNumber());
  while (env_.clock().CurrentTime() < send_30fps_end_time) {
    ++id;
    video_receive_stream_->OnCompleteFrame(
        test::FakeFrameBuilder()
            .Id(id)
            .PayloadType(99)
            .Time(RtpTimestampForFrame(id))
            .ReceivedTime(ReceiveTimeForFrame(id))
            .Refs({id - 1})
            .AsLast()
            .Build());
    EXPECT_THAT(fake_renderer_.WaitForFrame(k30FpsDelay, /*advance_time=*/true),
                Eq(absl::nullopt));
  }
  uint32_t current_rtp = RtpTimestampForFrame(id);
  Timestamp send_15fps_end_time =
      env_.clock().CurrentTime() + TimeDelta::Seconds(2);
  while (env_.clock().CurrentTime() < send_15fps_end_time) {
    ++id;
    current_rtp += k15FpsRtpTimestampDelta;
    video_receive_stream_->OnCompleteFrame(
        test::FakeFrameBuilder()
            .Id(id)
            .PayloadType(99)
            .Time(current_rtp)
            .ReceivedTime(env_.clock().CurrentTime())
            .Refs({id - 1})
            .AsLast()
            .Build());
    EXPECT_THAT(fake_renderer_.WaitForFrame(k15FpsDelay, /*advance_time=*/true),
                Eq(absl::nullopt));
  }

  ++id;
  current_rtp += k15FpsRtpTimestampDelta;
  // Insert keyframe which will recover the stream. However, on a poor
  // connection the keyframe will take significant time to send.
  constexpr TimeDelta kKeyframeDelay = TimeDelta::Millis(200);
  video_receive_stream_->OnCompleteFrame(
      test::FakeFrameBuilder()
          .Id(id)
          .PayloadType(99)
          .Time(current_rtp)
          .ReceivedTime(env_.clock().CurrentTime() + kKeyframeDelay)
          .AsLast()
          .Build());
  // If the framerate was not updated to be 15fps from the frames that arrived
  // previously, this will fail, as the delay will be longer.
  EXPECT_THAT(fake_renderer_.WaitForFrame(k15FpsDelay, /*advance_time=*/true),
              RenderedFrameWith(RtpTimestamp(current_rtp)));

  video_receive_stream_->Stop();
}

TEST_P(VideoReceiveStream2Test, StreamShouldNotTimeoutWhileWaitingForFrame) {
  // Disable smoothing since this makes it hard to test frame timing.
  config_.enable_prerenderer_smoothing = false;
  RecreateReceiveStream();

  video_receive_stream_->Start();
  EXPECT_CALL(mock_transport_, SendRtcp).Times(AnyNumber());

  video_receive_stream_->OnCompleteFrame(
      test::FakeFrameBuilder()
          .Id(0)
          .PayloadType(99)
          .Time(RtpTimestampForFrame(0))
          .ReceivedTime(ReceiveTimeForFrame(0))
          .AsLast()
          .Build());
  EXPECT_THAT(fake_renderer_.WaitForFrame(k30FpsDelay, /*advance_time=*/true),
              RenderedFrameWith(RtpTimestamp(RtpTimestampForFrame(0))));

  for (int id = 1; id < 30; ++id) {
    video_receive_stream_->OnCompleteFrame(
        test::FakeFrameBuilder()
            .Id(id)
            .PayloadType(99)
            .Time(RtpTimestampForFrame(id))
            .ReceivedTime(ReceiveTimeForFrame(id))
            .Refs({0})
            .AsLast()
            .Build());
    EXPECT_THAT(fake_renderer_.WaitForFrame(k30FpsDelay, /*advance_time=*/true),
                RenderedFrameWith(RtpTimestamp(RtpTimestampForFrame(id))));
  }

  // Simulate a pause in the stream, followed by a decodable frame that is ready
  // long in the future. The stream should not timeout in this case, but rather
  // decode the frame just before the timeout.
  time_controller_.AdvanceTime(TimeDelta::Millis(2900));
  uint32_t late_decode_rtp = kFirstRtpTimestamp + 200 * k30FpsRtpTimestampDelta;
  video_receive_stream_->OnCompleteFrame(
      test::FakeFrameBuilder()
          .Id(121)
          .PayloadType(99)
          .Time(late_decode_rtp)
          .ReceivedTime(env_.clock().CurrentTime())
          .AsLast()
          .Build());
  EXPECT_THAT(fake_renderer_.WaitForFrame(TimeDelta::Millis(100),
                                          /*advance_time=*/true),
              RenderedFrameWith(RtpTimestamp(late_decode_rtp)));

  video_receive_stream_->Stop();
}

INSTANTIATE_TEST_SUITE_P(VideoReceiveStream2Test,
                         VideoReceiveStream2Test,
                         testing::Bool(),
                         [](const auto& test_param_info) {
                           return (test_param_info.param
                                       ? "ScheduleDecodesWithMetronome"
                                       : "ScheduleDecodesWithPostTask");
                         });

}  // namespace webrtc
