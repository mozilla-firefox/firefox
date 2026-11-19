/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "api/media_types.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_headers.h"
#include "api/rtp_parameters.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/test/create_frame_generator.h"
#include "api/test/simulated_network.h"
#include "api/test/video/function_video_decoder_factory.h"
#include "api/video/video_codec_type.h"
#include "api/video_codecs/sdp_video_format.h"
#include "call/call.h"
#include "call/video_receive_stream.h"
#include "call/video_send_stream.h"
#include "modules/remote_bitrate_estimator/include/bwe_defines.h"
#include "rtc_base/checks.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/thread_annotations.h"
#include "system_wrappers/include/metrics.h"
#include "test/call_test.h"
#include "test/encoder_settings.h"
#include "test/fake_decoder.h"
#include "test/frame_generator_capturer.h"
#include "test/gtest.h"
#include "test/rtcp_packet_parser.h"
#include "test/rtp_rtcp_observer.h"
#include "test/video_test_constants.h"
#include "video/config/video_encoder_config.h"

namespace webrtc {
namespace {

class RembObserver : public test::RtpRtcpObserver {
 public:
  explicit RembObserver(TaskQueueBase* task_queue)
      : test::RtpRtcpObserver(test::VideoTestConstants::kDefaultTimeout),
        task_queue_(task_queue) {
    RTC_DCHECK_RUN_ON(task_queue_);
  }

  ~RembObserver() override { RTC_DCHECK_RUN_ON(task_queue_); }

  void ExpectRembForSsrc(uint32_t ssrc) {
    RTC_DCHECK_RUN_ON(task_queue_);
    expected_ssrc_ = ssrc;
    observation_complete_.Reset();
  }

  Action OnReceiveRtcp(std::span<const uint8_t> packet) override {
    test::RtcpPacketParser parser;
    EXPECT_TRUE(parser.Parse(packet));
    if (parser.remb()->num_packets() > 0) {
      const uint64_t bitrate_bps = parser.remb()->bitrate_bps();
      std::vector<uint32_t> ssrcs = parser.remb()->ssrcs();
      task_queue_->PostTask(SafeTask(
          safety_.flag(), [this, bitrate_bps, ssrcs = std::move(ssrcs)]() {
            RTC_DCHECK_RUN_ON(task_queue_);
            EXPECT_GT(bitrate_bps, 0u);
            if (expected_ssrc_.has_value() &&
                absl::c_linear_search(ssrcs, *expected_ssrc_)) {
              expected_ssrc_.reset();
              observation_complete_.Set();
            }
          }));
    }
    return SEND_PACKET;
  }

 private:
  TaskQueueBase* const task_queue_;
  std::optional<uint32_t> expected_ssrc_ RTC_GUARDED_BY(task_queue_);
  ScopedTaskSafety safety_;
};

constexpr RtpHeaderExtensionId kTOFExtensionId(4);
constexpr RtpHeaderExtensionId kASTExtensionId(5);

class BitrateEstimatorTest : public test::CallTest {
 public:
  BitrateEstimatorTest() : receive_config_(nullptr) {}

  ~BitrateEstimatorTest() override { EXPECT_TRUE(streams_.empty()); }

  void SetUp() override {
    metrics::Reset();
    SendTask(task_queue(), [this]() {
      RegisterRtpExtension(
          RtpExtension(RtpExtension::kTimestampOffsetUri, kTOFExtensionId));
      RegisterRtpExtension(
          RtpExtension(RtpExtension::kAbsSendTimeUri, kASTExtensionId));

      CreateCalls();

      remb_observer_ = std::make_unique<RembObserver>(task_queue());
      CreateSendTransport(BuiltInNetworkBehaviorConfig(), /*observer=*/nullptr);
      CreateReceiveTransport(BuiltInNetworkBehaviorConfig(),
                             remb_observer_.get());

      VideoSendStream::Config video_send_config(send_transport_.get());
      video_send_config.rtp.ssrcs.push_back(
          test::VideoTestConstants::kVideoSendSsrcs[0]);
      video_send_config.encoder_settings.encoder_factory =
          &fake_encoder_factory_;
      video_send_config.encoder_settings.bitrate_allocator_factory =
          bitrate_allocator_factory_.get();
      video_send_config.rtp.payload_name = "FAKE";
      video_send_config.rtp.payload_type =
          test::VideoTestConstants::kFakeVideoSendPayloadType;
      SetVideoSendConfig(video_send_config);
      VideoEncoderConfig video_encoder_config;
      test::FillEncoderConfiguration(kVideoCodecVP8, 1, &video_encoder_config);
      SetVideoEncoderConfig(video_encoder_config);

      receive_config_ =
          VideoReceiveStreamInterface::Config(receive_transport_.get());
      // receive_config_.decoders will be set by every stream separately.
      receive_config_.rtp.remote_ssrc = GetVideoSendConfig()->rtp.ssrcs[0];
    });
    SendTask(network_thread(), [this]() {
      sender_call_->SignalChannelNetworkState(MediaType::VIDEO, kNetworkUp);
      receiver_call_->SignalChannelNetworkState(MediaType::VIDEO, kNetworkUp);
    });
  }

  void TearDown() override {
    SendTask(task_queue(), [this]() {
      for (auto* stream : streams_) {
        stream->StopSending();
        delete stream;
      }
      streams_.clear();
      DestroyCalls();
      remb_observer_.reset();
    });
  }

 protected:
  class Stream {
   public:
    explicit Stream(BitrateEstimatorTest* test)
        : test_(test),
          is_sending_receiving_(false),
          send_stream_(nullptr),
          frame_generator_capturer_(),
          decoder_factory_(
              []() { return std::make_unique<test::FakeDecoder>(); }) {
      ssrc_ = ++test_->GetVideoSendConfig()->rtp.ssrcs[0];
      send_stream_ = test_->sender_call_->CreateVideoSendStream(
          test_->GetVideoSendConfig()->Copy(),
          test_->GetVideoEncoderConfig()->Copy());
      RTC_DCHECK_EQ(1, test_->GetVideoEncoderConfig()->number_of_streams);
      frame_generator_capturer_ =
          std::make_unique<test::FrameGeneratorCapturer>(
              &test->env().clock(),
              test::CreateSquareFrameGenerator(
                  test::VideoTestConstants::kDefaultWidth,
                  test::VideoTestConstants::kDefaultHeight, std::nullopt,
                  std::nullopt),
              test::VideoTestConstants::kDefaultFramerate,
              test->env().task_queue_factory());
      frame_generator_capturer_->Init();
      frame_generator_capturer_->Start();
      send_stream_->SetSource(frame_generator_capturer_.get(),
                              DegradationPreference::MAINTAIN_FRAMERATE);
      send_stream_->Start();

      VideoReceiveStreamInterface::Decoder decoder;
      test_->receive_config_.decoder_factory = &decoder_factory_;
      decoder.payload_type = test_->GetVideoSendConfig()->rtp.payload_type;
      decoder.video_format =
          SdpVideoFormat(test_->GetVideoSendConfig()->rtp.payload_name);
      test_->receive_config_.decoders.clear();
      test_->receive_config_.decoders.push_back(decoder);
      test_->receive_config_.rtp.remote_ssrc = ssrc_;
      test_->receive_config_.renderer = &test->fake_renderer_;
      video_receive_stream_ = test_->receiver_call_->CreateVideoReceiveStream(
          test_->receive_config_.Copy());
      video_receive_stream_->Start();
      is_sending_receiving_ = true;
    }

    ~Stream() {
      EXPECT_FALSE(is_sending_receiving_);
      test_->sender_call_->DestroyVideoSendStream(send_stream_);
      frame_generator_capturer_.reset(nullptr);
      send_stream_ = nullptr;
      if (video_receive_stream_) {
        test_->receiver_call_->DestroyVideoReceiveStream(video_receive_stream_);
        video_receive_stream_ = nullptr;
      }
    }

    uint32_t ssrc() const { return ssrc_; }

    void StopSending() {
      if (is_sending_receiving_) {
        send_stream_->Stop();
        if (video_receive_stream_) {
          video_receive_stream_->Stop();
        }
        is_sending_receiving_ = false;
      }
    }

   private:
    BitrateEstimatorTest* test_;
    uint32_t ssrc_ = 0;
    bool is_sending_receiving_;
    VideoSendStream* send_stream_;
    VideoReceiveStreamInterface* video_receive_stream_;
    std::unique_ptr<test::FrameGeneratorCapturer> frame_generator_capturer_;

    test::FunctionVideoDecoderFactory decoder_factory_;
  };

  std::unique_ptr<RembObserver> remb_observer_;
  VideoReceiveStreamInterface::Config receive_config_;
  std::vector<Stream*> streams_;
};

TEST_F(BitrateEstimatorTest, InstantiatesTOFPerDefaultForVideo) {
  SendTask(task_queue(), [this]() {
    GetVideoSendConfig()->rtp.extensions.push_back(
        RtpExtension(RtpExtension::kTimestampOffsetUri, kTOFExtensionId));
    streams_.push_back(new Stream(this));
    remb_observer_->ExpectRembForSsrc(streams_.back()->ssrc());
  });
  EXPECT_TRUE(remb_observer_->Wait());
  EXPECT_EQ(metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverTOffset),
            1);
  EXPECT_EQ(
      metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverAbsSendTime), 0);
}

TEST_F(BitrateEstimatorTest, ImmediatelySwitchToASTForVideo) {
  SendTask(task_queue(), [this]() {
    GetVideoSendConfig()->rtp.extensions.push_back(
        RtpExtension(RtpExtension::kAbsSendTimeUri, kASTExtensionId));
    streams_.push_back(new Stream(this));
    remb_observer_->ExpectRembForSsrc(streams_.back()->ssrc());
  });
  EXPECT_TRUE(remb_observer_->Wait());
  EXPECT_EQ(metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverTOffset),
            0);
  EXPECT_EQ(
      metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverAbsSendTime), 1);
}

TEST_F(BitrateEstimatorTest, SwitchesToASTForVideo) {
  SendTask(task_queue(), [this]() {
    GetVideoSendConfig()->rtp.extensions.push_back(
        RtpExtension(RtpExtension::kTimestampOffsetUri, kTOFExtensionId));
    streams_.push_back(new Stream(this));
    remb_observer_->ExpectRembForSsrc(streams_.back()->ssrc());
  });
  EXPECT_TRUE(remb_observer_->Wait());
  EXPECT_EQ(metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverTOffset),
            1);
  EXPECT_EQ(
      metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverAbsSendTime), 0);

  SendTask(task_queue(), [this]() {
    GetVideoSendConfig()->rtp.extensions[0] =
        RtpExtension(RtpExtension::kAbsSendTimeUri, kASTExtensionId);
    streams_.push_back(new Stream(this));
    remb_observer_->ExpectRembForSsrc(streams_.back()->ssrc());
  });
  EXPECT_TRUE(remb_observer_->Wait());
  EXPECT_EQ(metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverTOffset),
            1);
  EXPECT_EQ(
      metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverAbsSendTime), 1);
}

// This test is flaky. See webrtc:5790.
TEST_F(BitrateEstimatorTest, DISABLED_SwitchesToASTThenBackToTOFForVideo) {
  SendTask(task_queue(), [this]() {
    GetVideoSendConfig()->rtp.extensions.push_back(
        RtpExtension(RtpExtension::kTimestampOffsetUri, kTOFExtensionId));
    streams_.push_back(new Stream(this));
    remb_observer_->ExpectRembForSsrc(streams_.back()->ssrc());
  });
  EXPECT_TRUE(remb_observer_->Wait());
  EXPECT_EQ(metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverTOffset),
            1);

  SendTask(task_queue(), [this]() {
    GetVideoSendConfig()->rtp.extensions[0] =
        RtpExtension(RtpExtension::kAbsSendTimeUri, kASTExtensionId);
    streams_.push_back(new Stream(this));
    remb_observer_->ExpectRembForSsrc(streams_.back()->ssrc());
  });
  EXPECT_TRUE(remb_observer_->Wait());
  EXPECT_EQ(
      metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverAbsSendTime), 1);

  SendTask(task_queue(), [this]() {
    streams_[0]->StopSending();
    streams_[1]->StopSending();
    GetVideoSendConfig()->rtp.extensions[0] =
        RtpExtension(RtpExtension::kTimestampOffsetUri, kTOFExtensionId);
    streams_.push_back(new Stream(this));
    remb_observer_->ExpectRembForSsrc(streams_.back()->ssrc());
  });
  EXPECT_TRUE(remb_observer_->Wait());
  EXPECT_EQ(metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverTOffset),
            2);
}
}  // namespace
}  // namespace webrtc
