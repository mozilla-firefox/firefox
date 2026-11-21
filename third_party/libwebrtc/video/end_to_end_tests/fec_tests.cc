/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>

#include "api/task_queue/task_queue_base.h"
#include "api/test/simulated_network.h"
#include "api/test/video/function_video_encoder_factory.h"
#include "call/fake_network_pipe.h"
#include "call/simulated_network.h"
#include "media/engine/internal_decoder_factory.h"
#include "modules/include/module_common_types_public.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "modules/rtp_rtcp/source/rtp_packet.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "rtc_base/synchronization/mutex.h"
#include "test/call_test.h"
#include "test/field_trial.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/rtcp_packet_parser.h"
#include "test/video_test_constants.h"

using ::testing::Contains;
using ::testing::Not;

namespace webrtc {
namespace {
enum : int {  // The first valid value is 1.
  kTransportSequenceNumberExtensionId = 1,
  kVideoRotationExtensionId,
};
}  // namespace

class FecEndToEndTest : public test::CallTest {
 public:
  FecEndToEndTest() {
    RegisterRtpExtension(RtpExtension(RtpExtension::kTransportSequenceNumberUri,
                                      kTransportSequenceNumberExtensionId));
    RegisterRtpExtension(RtpExtension(RtpExtension::kVideoRotationUri,
                                      kVideoRotationExtensionId));
  }
};

TEST_F(FecEndToEndTest, ReceivesUlpfec) {
  class UlpfecRenderObserver : public test::EndToEndTest,
                               public rtc::VideoSinkInterface<VideoFrame> {
   public:
    UlpfecRenderObserver()
        : EndToEndTest(test::VideoTestConstants::kDefaultTimeout),
          encoder_factory_(
              [](const Environment& env, const SdpVideoFormat& format) {
                return CreateVp8Encoder(env);
              }),
          random_(0xcafef00d1),
          num_packets_sent_(0) {}

   private:
    Action OnSendRtp(rtc::ArrayView<const uint8_t> packet) override {
      MutexLock lock(&mutex_);
      RtpPacket rtp_packet;
      EXPECT_TRUE(rtp_packet.Parse(packet));

      EXPECT_TRUE(rtp_packet.PayloadType() ==
                      test::VideoTestConstants::kVideoSendPayloadType ||
                  rtp_packet.PayloadType() ==
                      test::VideoTestConstants::kRedPayloadType)
          << "Unknown payload type received.";
      EXPECT_EQ(test::VideoTestConstants::kVideoSendSsrcs[0], rtp_packet.Ssrc())
          << "Unknown SSRC received.";

      // Parse RED header.
      int encapsulated_payload_type = -1;
      if (rtp_packet.PayloadType() ==
          test::VideoTestConstants::kRedPayloadType) {
        encapsulated_payload_type = rtp_packet.payload()[0];

        EXPECT_TRUE(encapsulated_payload_type ==
                        test::VideoTestConstants::kVideoSendPayloadType ||
                    encapsulated_payload_type ==
                        test::VideoTestConstants::kUlpfecPayloadType)
            << "Unknown encapsulated payload type received.";
      }

      // To minimize test flakiness, always let ULPFEC packets through.
      if (encapsulated_payload_type ==
          test::VideoTestConstants::kUlpfecPayloadType) {
        return SEND_PACKET;
      }

      // Simulate 5% video packet loss after rampup period. Record the
      // corresponding timestamps that were dropped.
      if (num_packets_sent_++ > 100 && random_.Rand(1, 100) <= 5) {
        if (encapsulated_payload_type ==
            test::VideoTestConstants::kVideoSendPayloadType) {
          dropped_sequence_numbers_.insert(rtp_packet.SequenceNumber());
          dropped_timestamps_.insert(rtp_packet.Timestamp());
        }
        return DROP_PACKET;
      }

      return SEND_PACKET;
    }

    void OnFrame(const VideoFrame& video_frame) override {
      MutexLock lock(&mutex_);
      // Rendering frame with timestamp of packet that was dropped -> FEC
      // protection worked.
      auto it = dropped_timestamps_.find(video_frame.rtp_timestamp());
      if (it != dropped_timestamps_.end()) {
        observation_complete_.Set();
      }
    }

    void ModifyVideoConfigs(
        VideoSendStream::Config* send_config,
        std::vector<VideoReceiveStreamInterface::Config>* receive_configs,
        VideoEncoderConfig* encoder_config) override {
      // Use VP8 instead of FAKE, since the latter does not have PictureID
      // in the packetization headers.
      send_config->encoder_settings.encoder_factory = &encoder_factory_;
      send_config->rtp.payload_name = "VP8";
      send_config->rtp.payload_type =
          test::VideoTestConstants::kVideoSendPayloadType;
      encoder_config->codec_type = kVideoCodecVP8;
      VideoReceiveStreamInterface::Decoder decoder =
          test::CreateMatchingDecoder(*send_config);
      (*receive_configs)[0].decoder_factory = &decoder_factory_;
      (*receive_configs)[0].decoders.clear();
      (*receive_configs)[0].decoders.push_back(decoder);

      // Enable ULPFEC over RED.
      send_config->rtp.ulpfec.red_payload_type =
          test::VideoTestConstants::kRedPayloadType;
      send_config->rtp.ulpfec.ulpfec_payload_type =
          test::VideoTestConstants::kUlpfecPayloadType;
      (*receive_configs)[0].rtp.red_payload_type =
          test::VideoTestConstants::kRedPayloadType;
      (*receive_configs)[0].rtp.ulpfec_payload_type =
          test::VideoTestConstants::kUlpfecPayloadType;

      (*receive_configs)[0].renderer = this;
    }

    void PerformTest() override {
      EXPECT_TRUE(Wait())
          << "Timed out waiting for dropped frames to be rendered.";
    }

    Mutex mutex_;
    std::unique_ptr<VideoEncoder> encoder_;
    test::FunctionVideoEncoderFactory encoder_factory_;
    InternalDecoderFactory decoder_factory_;
    std::set<uint32_t> dropped_sequence_numbers_ RTC_GUARDED_BY(mutex_);
    // Several packets can have the same timestamp.
    std::multiset<uint32_t> dropped_timestamps_ RTC_GUARDED_BY(mutex_);
    Random random_;
    int num_packets_sent_ RTC_GUARDED_BY(mutex_);
  } test;

  RunBaseTest(&test);
}

class FlexfecRenderObserver : public test::EndToEndTest,
                              public rtc::VideoSinkInterface<VideoFrame> {
 public:
  static constexpr uint32_t kVideoLocalSsrc = 123;
  static constexpr uint32_t kFlexfecLocalSsrc = 456;

  explicit FlexfecRenderObserver(bool enable_nack, bool expect_flexfec_rtcp)
      : test::EndToEndTest(test::VideoTestConstants::kLongTimeout),
        enable_nack_(enable_nack),
        expect_flexfec_rtcp_(expect_flexfec_rtcp),
        received_flexfec_rtcp_(false),
        random_(0xcafef00d1),
        num_packets_sent_(0) {}

  size_t GetNumFlexfecStreams() const override { return 1; }

 private:
  Action OnSendRtp(rtc::ArrayView<const uint8_t> packet) override {
    MutexLock lock(&mutex_);
    RtpPacket rtp_packet;
    EXPECT_TRUE(rtp_packet.Parse(packet));

    EXPECT_TRUE(
        rtp_packet.PayloadType() ==
            test::VideoTestConstants::kFakeVideoSendPayloadType ||
        rtp_packet.PayloadType() ==
            test::VideoTestConstants::kFlexfecPayloadType ||
        (enable_nack_ && rtp_packet.PayloadType() ==
                             test::VideoTestConstants::kSendRtxPayloadType))
        << "Unknown payload type received.";
    EXPECT_TRUE(
        rtp_packet.Ssrc() == test::VideoTestConstants::kVideoSendSsrcs[0] ||
        rtp_packet.Ssrc() == test::VideoTestConstants::kFlexfecSendSsrc ||
        (enable_nack_ &&
         rtp_packet.Ssrc() == test::VideoTestConstants::kSendRtxSsrcs[0]))
        << "Unknown SSRC received.";

    // To reduce test flakiness, always let FlexFEC packets through.
    if (rtp_packet.PayloadType() ==
        test::VideoTestConstants::kFlexfecPayloadType) {
      EXPECT_EQ(test::VideoTestConstants::kFlexfecSendSsrc, rtp_packet.Ssrc());

      return SEND_PACKET;
    }

    // To reduce test flakiness, always let RTX packets through.
    if (rtp_packet.PayloadType() ==
        test::VideoTestConstants::kSendRtxPayloadType) {
      EXPECT_EQ(test::VideoTestConstants::kSendRtxSsrcs[0], rtp_packet.Ssrc());

      if (rtp_packet.payload_size() == 0) {
        // Pure padding packet.
        return SEND_PACKET;
      }

      // Parse RTX header.
      uint16_t original_sequence_number =
          ByteReader<uint16_t>::ReadBigEndian(rtp_packet.payload().data());

      // From the perspective of FEC, a retransmitted packet is no longer
      // dropped, so remove it from list of dropped packets.
      auto seq_num_it =
          dropped_sequence_numbers_.find(original_sequence_number);
      if (seq_num_it != dropped_sequence_numbers_.end()) {
        dropped_sequence_numbers_.erase(seq_num_it);
        auto ts_it = dropped_timestamps_.find(rtp_packet.Timestamp());
        EXPECT_NE(ts_it, dropped_timestamps_.end());
        dropped_timestamps_.erase(ts_it);
      }

      return SEND_PACKET;
    }

    // Simulate 5% video packet loss after rampup period. Record the
    // corresponding timestamps that were dropped.
    if (num_packets_sent_++ > 100 && random_.Rand(1, 100) <= 5) {
      EXPECT_EQ(test::VideoTestConstants::kFakeVideoSendPayloadType,
                rtp_packet.PayloadType());
      EXPECT_EQ(test::VideoTestConstants::kVideoSendSsrcs[0],
                rtp_packet.Ssrc());

      dropped_sequence_numbers_.insert(rtp_packet.SequenceNumber());
      dropped_timestamps_.insert(rtp_packet.Timestamp());

      return DROP_PACKET;
    }

    return SEND_PACKET;
  }

  Action OnReceiveRtcp(rtc::ArrayView<const uint8_t> data) override {
    test::RtcpPacketParser parser;

    parser.Parse(data);
    if (parser.sender_ssrc() == kFlexfecLocalSsrc) {
      EXPECT_EQ(1, parser.receiver_report()->num_packets());
      const std::vector<rtcp::ReportBlock>& report_blocks =
          parser.receiver_report()->report_blocks();
      if (!report_blocks.empty()) {
        EXPECT_EQ(1U, report_blocks.size());
        EXPECT_EQ(test::VideoTestConstants::kFlexfecSendSsrc,
                  report_blocks[0].source_ssrc());
        MutexLock lock(&mutex_);
        received_flexfec_rtcp_ = true;
      }
    }

    return SEND_PACKET;
  }

  BuiltInNetworkBehaviorConfig GetSendTransportConfig() const override {
    // At low RTT (< kLowRttNackMs) -> NACK only, no FEC.
    const int kNetworkDelayMs = 100;
    BuiltInNetworkBehaviorConfig config;
    config.queue_delay_ms = kNetworkDelayMs;
    return config;
  }

  void OnFrame(const VideoFrame& video_frame) override {
    EXPECT_EQ(kVideoRotation_90, video_frame.rotation());

    MutexLock lock(&mutex_);
    // Rendering frame with timestamp of packet that was dropped -> FEC
    // protection worked.
    auto it = dropped_timestamps_.find(video_frame.rtp_timestamp());
    if (it != dropped_timestamps_.end()) {
      if (!expect_flexfec_rtcp_ || received_flexfec_rtcp_) {
        observation_complete_.Set();
      }
    }
  }

  void ModifyVideoConfigs(
      VideoSendStream::Config* send_config,
      std::vector<VideoReceiveStreamInterface::Config>* receive_configs,
      VideoEncoderConfig* encoder_config) override {
    (*receive_configs)[0].rtp.local_ssrc = kVideoLocalSsrc;
    (*receive_configs)[0].renderer = this;

    if (enable_nack_) {
      send_config->rtp.nack.rtp_history_ms =
          test::VideoTestConstants::kNackRtpHistoryMs;
      send_config->rtp.rtx.ssrcs.push_back(
          test::VideoTestConstants::kSendRtxSsrcs[0]);
      send_config->rtp.rtx.payload_type =
          test::VideoTestConstants::kSendRtxPayloadType;

      (*receive_configs)[0].rtp.nack.rtp_history_ms =
          test::VideoTestConstants::kNackRtpHistoryMs;
      (*receive_configs)[0].rtp.rtx_ssrc =
          test::VideoTestConstants::kSendRtxSsrcs[0];
      (*receive_configs)[0].rtp.rtx_associated_payload_types
          [test::VideoTestConstants::kSendRtxPayloadType] =
          test::VideoTestConstants::kVideoSendPayloadType;
    }
  }

  void OnFrameGeneratorCapturerCreated(
      test::FrameGeneratorCapturer* frame_generator_capturer) override {
    frame_generator_capturer->SetFakeRotation(kVideoRotation_90);
  }

  void ModifyFlexfecConfigs(
      std::vector<FlexfecReceiveStream::Config>* receive_configs) override {
    (*receive_configs)[0].rtp.local_ssrc = kFlexfecLocalSsrc;
  }

  void PerformTest() override {
    EXPECT_TRUE(Wait())
        << "Timed out waiting for dropped frames to be rendered.";
  }

  Mutex mutex_;
  std::set<uint32_t> dropped_sequence_numbers_ RTC_GUARDED_BY(mutex_);
  // Several packets can have the same timestamp.
  std::multiset<uint32_t> dropped_timestamps_ RTC_GUARDED_BY(mutex_);
  const bool enable_nack_;
  const bool expect_flexfec_rtcp_;
  bool received_flexfec_rtcp_ RTC_GUARDED_BY(mutex_);
  Random random_;
  int num_packets_sent_;
};

TEST_F(FecEndToEndTest, RecoversWithFlexfec) {
  FlexfecRenderObserver test(false, false);
  RunBaseTest(&test);
}

TEST_F(FecEndToEndTest, RecoversWithFlexfecAndNack) {
  FlexfecRenderObserver test(true, false);
  RunBaseTest(&test);
}

TEST_F(FecEndToEndTest, RecoversWithFlexfecAndSendsCorrespondingRtcp) {
  FlexfecRenderObserver test(false, true);
  RunBaseTest(&test);
}

TEST_F(FecEndToEndTest, ReceivedUlpfecPacketsNotNacked) {
  class UlpfecNackObserver : public test::EndToEndTest {
   public:
    UlpfecNackObserver()
        : EndToEndTest(test::VideoTestConstants::kDefaultTimeout),
          state_(kFirstPacket),
          ulpfec_sequence_number_(0),
          has_last_sequence_number_(false),
          last_sequence_number_(0),
          encoder_factory_(
              [](const Environment& env, const SdpVideoFormat& format) {
                return CreateVp8Encoder(env);
              }) {}

   private:
    Action OnSendRtp(rtc::ArrayView<const uint8_t> packet) override {
      MutexLock lock_(&mutex_);
      RtpPacket rtp_packet;
      EXPECT_TRUE(rtp_packet.Parse(packet));

      int encapsulated_payload_type = -1;
      if (rtp_packet.PayloadType() ==
          test::VideoTestConstants::kRedPayloadType) {
        encapsulated_payload_type = rtp_packet.payload()[0];
        if (encapsulated_payload_type !=
            test::VideoTestConstants::kFakeVideoSendPayloadType)
          EXPECT_EQ(test::VideoTestConstants::kUlpfecPayloadType,
                    encapsulated_payload_type);
      } else {
        EXPECT_EQ(test::VideoTestConstants::kFakeVideoSendPayloadType,
                  rtp_packet.PayloadType());
      }

      if (has_last_sequence_number_ &&
          !IsNewerSequenceNumber(rtp_packet.SequenceNumber(),
                                 last_sequence_number_)) {
        // Drop retransmitted packets.
        return DROP_PACKET;
      }
      last_sequence_number_ = rtp_packet.SequenceNumber();
      has_last_sequence_number_ = true;

      bool ulpfec_packet = encapsulated_payload_type ==
                           test::VideoTestConstants::kUlpfecPayloadType;
      switch (state_) {
        case kFirstPacket:
          state_ = kDropEveryOtherPacketUntilUlpfec;
          break;
        case kDropEveryOtherPacketUntilUlpfec:
          if (ulpfec_packet) {
            state_ = kDropAllMediaPacketsUntilUlpfec;
          } else if (rtp_packet.SequenceNumber() % 2 == 0) {
            return DROP_PACKET;
          }
          break;
        case kDropAllMediaPacketsUntilUlpfec:
          if (!ulpfec_packet)
            return DROP_PACKET;
          ulpfec_sequence_number_ = rtp_packet.SequenceNumber();
          state_ = kDropOneMediaPacket;
          break;
        case kDropOneMediaPacket:
          if (ulpfec_packet)
            return DROP_PACKET;
          state_ = kPassOneMediaPacket;
          return DROP_PACKET;
        case kPassOneMediaPacket:
          if (ulpfec_packet)
            return DROP_PACKET;
          // Pass one media packet after dropped packet after last FEC,
          // otherwise receiver might never see a seq_no after
          // `ulpfec_sequence_number_`
          state_ = kVerifyUlpfecPacketNotInNackList;
          break;
        case kVerifyUlpfecPacketNotInNackList:
          // Continue to drop packets. Make sure no frame can be decoded.
          if (ulpfec_packet || rtp_packet.SequenceNumber() % 2 == 0)
            return DROP_PACKET;
          break;
      }
      return SEND_PACKET;
    }

    Action OnReceiveRtcp(rtc::ArrayView<const uint8_t> packet) override {
      MutexLock lock_(&mutex_);
      if (state_ == kVerifyUlpfecPacketNotInNackList) {
        test::RtcpPacketParser rtcp_parser;
        rtcp_parser.Parse(packet);
        const std::vector<uint16_t>& nacks = rtcp_parser.nack()->packet_ids();
        EXPECT_THAT(nacks, Not(Contains(ulpfec_sequence_number_)))
            << "Got nack for ULPFEC packet";
        if (!nacks.empty() &&
            IsNewerSequenceNumber(nacks.back(), ulpfec_sequence_number_)) {
          observation_complete_.Set();
        }
      }
      return SEND_PACKET;
    }

    BuiltInNetworkBehaviorConfig GetSendTransportConfig() const override {
      // At low RTT (< kLowRttNackMs) -> NACK only, no FEC.
      // Configure some network delay.
      const int kNetworkDelayMs = 50;
      BuiltInNetworkBehaviorConfig config;
      config.queue_delay_ms = kNetworkDelayMs;
      return config;
    }

    // TODO(holmer): Investigate why we don't send FEC packets when the bitrate
    // is 10 kbps.
    void ModifySenderBitrateConfig(
        BitrateConstraints* bitrate_config) override {
      const int kMinBitrateBps = 30000;
      bitrate_config->min_bitrate_bps = kMinBitrateBps;
    }

    void ModifyVideoConfigs(
        VideoSendStream::Config* send_config,
        std::vector<VideoReceiveStreamInterface::Config>* receive_configs,
        VideoEncoderConfig* encoder_config) override {
      // Configure hybrid NACK/FEC.
      send_config->rtp.nack.rtp_history_ms =
          test::VideoTestConstants::kNackRtpHistoryMs;
      send_config->rtp.ulpfec.red_payload_type =
          test::VideoTestConstants::kRedPayloadType;
      send_config->rtp.ulpfec.ulpfec_payload_type =
          test::VideoTestConstants::kUlpfecPayloadType;
      // Set codec to VP8, otherwise NACK/FEC hybrid will be disabled.
      send_config->encoder_settings.encoder_factory = &encoder_factory_;
      send_config->rtp.payload_name = "VP8";
      send_config->rtp.payload_type =
          test::VideoTestConstants::kFakeVideoSendPayloadType;
      encoder_config->codec_type = kVideoCodecVP8;

      (*receive_configs)[0].rtp.nack.rtp_history_ms =
          test::VideoTestConstants::kNackRtpHistoryMs;
      (*receive_configs)[0].rtp.red_payload_type =
          test::VideoTestConstants::kRedPayloadType;
      (*receive_configs)[0].rtp.ulpfec_payload_type =
          test::VideoTestConstants::kUlpfecPayloadType;

      (*receive_configs)[0].decoders.resize(1);
      (*receive_configs)[0].decoders[0].payload_type =
          send_config->rtp.payload_type;
      (*receive_configs)[0].decoders[0].video_format =
          SdpVideoFormat(send_config->rtp.payload_name);
      (*receive_configs)[0].decoder_factory = &decoder_factory_;
    }

    void PerformTest() override {
      EXPECT_TRUE(Wait())
          << "Timed out while waiting for FEC packets to be received.";
    }

    enum {
      kFirstPacket,
      kDropEveryOtherPacketUntilUlpfec,
      kDropAllMediaPacketsUntilUlpfec,
      kDropOneMediaPacket,
      kPassOneMediaPacket,
      kVerifyUlpfecPacketNotInNackList,
    } state_;

    Mutex mutex_;
    uint16_t ulpfec_sequence_number_ RTC_GUARDED_BY(&mutex_);
    bool has_last_sequence_number_;
    uint16_t last_sequence_number_;
    test::FunctionVideoEncoderFactory encoder_factory_;
    InternalDecoderFactory decoder_factory_;
  } test;

  RunBaseTest(&test);
}
}  // namespace webrtc
