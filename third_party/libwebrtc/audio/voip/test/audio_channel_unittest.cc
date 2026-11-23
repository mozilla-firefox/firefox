/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/voip/audio_channel.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "api/audio/audio_frame.h"
#include "api/audio/audio_mixer.h"
#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/voip/voip_statistics.h"
#include "audio/voip/test/mock_task_queue.h"
#include "call/audio_sender.h"
#include "modules/audio_mixer/audio_mixer_impl.h"
#include "modules/audio_mixer/sine_wave_generator.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtcp_packet/receiver_report.h"
#include "modules/rtp_rtcp/source/rtcp_packet/report_block.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "system_wrappers/include/clock.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/mock_audio_encoder.h"
#include "test/mock_transport.h"

namespace webrtc {
namespace {

using ::testing::FloatEq;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Unused;
using ::testing::WithArg;

constexpr uint64_t kStartTime = 123456789;
constexpr uint32_t kLocalSsrc = 0xdeadc0de;
constexpr int16_t kAudioLevel = 3004;  // used for sine wave level
constexpr int kPcmuPayload = 0;

class AudioChannelTest : public ::testing::Test {
 public:
  const SdpAudioFormat kPcmuFormat = {"pcmu", 8000, 1};

  AudioChannelTest()
      : fake_clock_(kStartTime),
        wave_generator_(1000.0, kAudioLevel),
        env_(CreateEnvironment(
            CreateTestFieldTrialsPtr(),
            &fake_clock_,
            std::make_unique<MockTaskQueueFactory>(&task_queue_))) {
    audio_mixer_ = AudioMixerImpl::Create();
    encoder_factory_ = CreateBuiltinAudioEncoderFactory();
    decoder_factory_ = CreateBuiltinAudioDecoderFactory();

    // By default, run the queued task immediately.
    ON_CALL(task_queue_, PostTaskImpl)
        .WillByDefault(WithArg<0>(
            [](absl::AnyInvocable<void()&&> task) { std::move(task)(); }));
  }

  void SetUp() override { audio_channel_ = CreateAudioChannel(kLocalSsrc); }

  void TearDown() override { audio_channel_ = nullptr; }

  scoped_refptr<AudioChannel> CreateAudioChannel(uint32_t ssrc) {
    // Use same audio mixer here for simplicity sake as we are not checking
    // audio activity of RTP in our testcases. If we need to do test on audio
    // signal activity then we need to assign audio mixer for each channel.
    // Also this uses the same transport object for different audio channel to
    // simplify network routing logic.
    scoped_refptr<AudioChannel> audio_channel = make_ref_counted<AudioChannel>(
        env_, &transport_, ssrc, audio_mixer_.get(), decoder_factory_);
    audio_channel->SetEncoder(
        kPcmuPayload, kPcmuFormat,
        encoder_factory_->Create(env_, kPcmuFormat,
                                 {.payload_type = kPcmuPayload}));
    audio_channel->SetReceiveCodecs({{kPcmuPayload, kPcmuFormat}});
    audio_channel->StartSend();
    audio_channel->StartPlay();
    return audio_channel;
  }

  std::unique_ptr<AudioFrame> GetAudioFrame(int order) {
    auto frame = std::make_unique<AudioFrame>();
    frame->sample_rate_hz_ = kPcmuFormat.clockrate_hz;
    frame->samples_per_channel_ = kPcmuFormat.clockrate_hz / 100;  // 10 ms.
    frame->num_channels_ = kPcmuFormat.num_channels;
    frame->timestamp_ = frame->samples_per_channel_ * order;
    wave_generator_.GenerateNextFrame(frame.get());
    return frame;
  }

  SimulatedClock fake_clock_;
  SineWaveGenerator wave_generator_;
  NiceMock<MockTransport> transport_;
  NiceMock<MockTaskQueue> task_queue_;
  const Environment env_;
  scoped_refptr<AudioMixer> audio_mixer_;
  scoped_refptr<AudioDecoderFactory> decoder_factory_;
  scoped_refptr<AudioEncoderFactory> encoder_factory_;
  scoped_refptr<AudioChannel> audio_channel_;
};

// Validate RTP packet generation by feeding audio frames with sine wave.
// Resulted RTP packet is looped back into AudioChannel and gets decoded into
// audio frame to see if it has some signal to indicate its validity.
TEST_F(AudioChannelTest, PlayRtpByLocalLoop) {
  auto loop_rtp = [&](std::span<const uint8_t> packet, Unused) {
    audio_channel_->ReceivedRTPPacket(packet);
    return true;
  };
  EXPECT_CALL(transport_, SendRtp).WillOnce(loop_rtp);

  auto audio_sender = audio_channel_->GetAudioSender();
  audio_sender->SendAudioData(GetAudioFrame(0));
  audio_sender->SendAudioData(GetAudioFrame(1));

  AudioFrame empty_frame, audio_frame;
  empty_frame.Mute();
  empty_frame.mutable_data();  // This will zero out the data.
  audio_frame.CopyFrom(empty_frame);
  audio_mixer_->Mix(/*number_of_channels*/ 1, &audio_frame);

  // We expect now audio frame to pick up something.
  EXPECT_NE(memcmp(empty_frame.data(), audio_frame.data(),
                   AudioFrame::kMaxDataSizeBytes),
            0);
}

// Validate assigned local SSRC is resulted in RTP packet.
TEST_F(AudioChannelTest, VerifyLocalSsrcAsAssigned) {
  RtpPacketReceived rtp;
  auto loop_rtp = [&](std::span<const uint8_t> packet, Unused) {
    rtp.Parse(packet);
    return true;
  };
  EXPECT_CALL(transport_, SendRtp).WillOnce(loop_rtp);

  auto audio_sender = audio_channel_->GetAudioSender();
  audio_sender->SendAudioData(GetAudioFrame(0));
  audio_sender->SendAudioData(GetAudioFrame(1));

  EXPECT_EQ(rtp.Ssrc(), kLocalSsrc);
}

// Check metrics after processing an RTP packet.
TEST_F(AudioChannelTest, TestIngressStatistics) {
  auto loop_rtp = [&](std::span<const uint8_t> packet, Unused) {
    audio_channel_->ReceivedRTPPacket(packet);
    return true;
  };
  EXPECT_CALL(transport_, SendRtp).WillRepeatedly(loop_rtp);

  auto audio_sender = audio_channel_->GetAudioSender();
  audio_sender->SendAudioData(GetAudioFrame(0));
  audio_sender->SendAudioData(GetAudioFrame(1));

  AudioFrame audio_frame;
  audio_mixer_->Mix(/*number_of_channels=*/1, &audio_frame);
  audio_mixer_->Mix(/*number_of_channels=*/1, &audio_frame);

  std::optional<IngressStatistics> ingress_stats =
      audio_channel_->GetIngressStatistics();
  EXPECT_TRUE(ingress_stats);
  EXPECT_EQ(ingress_stats->neteq_stats.total_samples_received, 160ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.concealed_samples, 0ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.concealment_events, 0ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.inserted_samples_for_deceleration, 0ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.removed_samples_for_acceleration, 0ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.silent_concealed_samples, 0ULL);
  // To extract the jitter buffer length in millisecond, jitter_buffer_delay_ms
  // needs to be divided by jitter_buffer_emitted_count (number of samples).
  EXPECT_EQ(ingress_stats->neteq_stats.jitter_buffer_delay_ms, 1600ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.jitter_buffer_emitted_count, 160ULL);
  EXPECT_GT(ingress_stats->neteq_stats.jitter_buffer_target_delay_ms, 0ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.interruption_count, 0);
  EXPECT_EQ(ingress_stats->neteq_stats.total_interruption_duration_ms, 0);
  EXPECT_DOUBLE_EQ(ingress_stats->total_duration, 0.02);

  // Now without any RTP pending in jitter buffer pull more.
  audio_mixer_->Mix(/*number_of_channels=*/1, &audio_frame);
  audio_mixer_->Mix(/*number_of_channels=*/1, &audio_frame);

  // Send another RTP packet to intentionally break PLC.
  audio_sender->SendAudioData(GetAudioFrame(2));
  audio_sender->SendAudioData(GetAudioFrame(3));

  ingress_stats = audio_channel_->GetIngressStatistics();
  EXPECT_TRUE(ingress_stats);
  EXPECT_EQ(ingress_stats->neteq_stats.total_samples_received, 320ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.concealed_samples, 168ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.concealment_events, 1ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.inserted_samples_for_deceleration, 0ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.removed_samples_for_acceleration, 0ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.silent_concealed_samples, 0ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.jitter_buffer_delay_ms, 1600ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.jitter_buffer_emitted_count, 160ULL);
  EXPECT_GT(ingress_stats->neteq_stats.jitter_buffer_target_delay_ms, 0ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.interruption_count, 0);
  EXPECT_EQ(ingress_stats->neteq_stats.total_interruption_duration_ms, 0);
  EXPECT_DOUBLE_EQ(ingress_stats->total_duration, 0.04);

  // Pull the last RTP packet.
  audio_mixer_->Mix(/*number_of_channels=*/1, &audio_frame);
  audio_mixer_->Mix(/*number_of_channels=*/1, &audio_frame);

  ingress_stats = audio_channel_->GetIngressStatistics();
  EXPECT_TRUE(ingress_stats);
  EXPECT_EQ(ingress_stats->neteq_stats.total_samples_received, 480ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.concealed_samples, 168ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.concealment_events, 1ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.inserted_samples_for_deceleration, 0ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.removed_samples_for_acceleration, 0ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.silent_concealed_samples, 0ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.jitter_buffer_delay_ms, 3200ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.jitter_buffer_emitted_count, 320ULL);
  EXPECT_GT(ingress_stats->neteq_stats.jitter_buffer_target_delay_ms, 0ULL);
  EXPECT_EQ(ingress_stats->neteq_stats.interruption_count, 0);
  EXPECT_EQ(ingress_stats->neteq_stats.total_interruption_duration_ms, 0);
  EXPECT_DOUBLE_EQ(ingress_stats->total_duration, 0.06);
}

// Check ChannelStatistics metric after processing RTP and RTCP packets.
TEST_F(AudioChannelTest, TestChannelStatistics) {
  auto loop_rtp = [&](std::span<const uint8_t> packet, Unused) {
    audio_channel_->ReceivedRTPPacket(packet);
    return true;
  };
  auto loop_rtcp = [&](std::span<const uint8_t> packet, Unused) {
    audio_channel_->ReceivedRTCPPacket(packet);
    return true;
  };
  EXPECT_CALL(transport_, SendRtp).WillRepeatedly(loop_rtp);
  EXPECT_CALL(transport_, SendRtcp).WillRepeatedly(loop_rtcp);

  // Simulate microphone giving audio frame (10 ms). This will trigger transport
  // to send RTP as handled in loop_rtp above.
  auto audio_sender = audio_channel_->GetAudioSender();
  audio_sender->SendAudioData(GetAudioFrame(0));
  audio_sender->SendAudioData(GetAudioFrame(1));

  // Simulate speaker requesting audio frame (10 ms). This will trigger VoIP
  // engine to fetch audio samples from RTP packets stored in jitter buffer.
  AudioFrame audio_frame;
  audio_mixer_->Mix(/*number_of_channels=*/1, &audio_frame);
  audio_mixer_->Mix(/*number_of_channels=*/1, &audio_frame);

  // Force sending RTCP SR report in order to have remote_rtcp field available
  // in channel statistics. This will trigger transport to send RTCP as handled
  // in loop_rtcp above.
  audio_channel_->SendRTCPReportForTesting(kRtcpSr);

  std::optional<ChannelStatistics> channel_stats =
      audio_channel_->GetChannelStatistics();
  EXPECT_TRUE(channel_stats);

  EXPECT_EQ(channel_stats->packets_sent, 1ULL);
  EXPECT_EQ(channel_stats->bytes_sent, 160ULL);

  EXPECT_EQ(channel_stats->packets_received, 1ULL);
  EXPECT_EQ(channel_stats->bytes_received, 160ULL);
  EXPECT_EQ(channel_stats->jitter, 0);
  EXPECT_EQ(channel_stats->packets_lost, 0);
  EXPECT_EQ(channel_stats->remote_ssrc.value(), kLocalSsrc);

  EXPECT_TRUE(channel_stats->remote_rtcp.has_value());

  EXPECT_EQ(channel_stats->remote_rtcp->jitter, 0);
  EXPECT_EQ(channel_stats->remote_rtcp->packets_lost, 0);
  EXPECT_EQ(channel_stats->remote_rtcp->fraction_lost, 0);
  EXPECT_GT(channel_stats->remote_rtcp->last_report_received_timestamp_ms, 0);
  EXPECT_FALSE(channel_stats->remote_rtcp->round_trip_time.has_value());
}

// Check ChannelStatistics RTT metric after processing RTP and RTCP packets
// using three audio channels where each represents media endpoint.
//
//  1) AC1 <- RTP/RTCP -> AC2
//  2) AC1 <- RTP/RTCP -> AC3
//
// During step 1), AC1 should be able to check RTT from AC2's SSRC.
// During step 2), AC1 should be able to check RTT from AC3's SSRC.
TEST_F(AudioChannelTest, RttIsAvailableAfterChangeOfRemoteSsrc) {
  // Create AC2 and AC3.
  constexpr uint32_t kAc2Ssrc = 0xdeadbeef;
  constexpr uint32_t kAc3Ssrc = 0xdeafbeef;

  auto ac_2 = CreateAudioChannel(kAc2Ssrc);
  auto ac_3 = CreateAudioChannel(kAc3Ssrc);

  auto send_recv_rtp = [&](scoped_refptr<AudioChannel> rtp_sender,
                           scoped_refptr<AudioChannel> rtp_receiver) {
    // Setup routing logic via transport_.
    auto route_rtp = [&](std::span<const uint8_t> packet, Unused) {
      rtp_receiver->ReceivedRTPPacket(packet);
      return true;
    };
    ON_CALL(transport_, SendRtp).WillByDefault(route_rtp);

    // This will trigger route_rtp callback via transport_.
    rtp_sender->GetAudioSender()->SendAudioData(GetAudioFrame(0));
    rtp_sender->GetAudioSender()->SendAudioData(GetAudioFrame(1));

    // Process received RTP in receiver.
    AudioFrame audio_frame;
    audio_mixer_->Mix(/*number_of_channels=*/1, &audio_frame);
    audio_mixer_->Mix(/*number_of_channels=*/1, &audio_frame);

    // Revert to default to avoid using reference in route_rtp lambda.
    ON_CALL(transport_, SendRtp).WillByDefault(Return(true));
  };

  auto send_recv_rtcp = [&](scoped_refptr<AudioChannel> rtcp_sender,
                            scoped_refptr<AudioChannel> rtcp_receiver) {
    // Setup routing logic via transport_.
    auto route_rtcp = [&](std::span<const uint8_t> packet, Unused) {
      rtcp_receiver->ReceivedRTCPPacket(packet);
      return true;
    };
    ON_CALL(transport_, SendRtcp).WillByDefault(route_rtcp);

    // This will trigger route_rtcp callback via transport_.
    rtcp_sender->SendRTCPReportForTesting(kRtcpSr);

    // Revert to default to avoid using reference in route_rtcp lambda.
    ON_CALL(transport_, SendRtcp).WillByDefault(Return(true));
  };

  // AC1 <-- RTP/RTCP --> AC2
  send_recv_rtp(audio_channel_, ac_2);
  send_recv_rtp(ac_2, audio_channel_);
  send_recv_rtcp(audio_channel_, ac_2);
  send_recv_rtcp(ac_2, audio_channel_);

  std::optional<ChannelStatistics> channel_stats =
      audio_channel_->GetChannelStatistics();
  ASSERT_TRUE(channel_stats);
  EXPECT_EQ(channel_stats->remote_ssrc, kAc2Ssrc);
  ASSERT_TRUE(channel_stats->remote_rtcp);
  EXPECT_GT(channel_stats->remote_rtcp->round_trip_time, 0.0);

  // AC1 <-- RTP/RTCP --> AC3
  send_recv_rtp(audio_channel_, ac_3);
  send_recv_rtp(ac_3, audio_channel_);
  send_recv_rtcp(audio_channel_, ac_3);
  send_recv_rtcp(ac_3, audio_channel_);

  channel_stats = audio_channel_->GetChannelStatistics();
  ASSERT_TRUE(channel_stats);
  EXPECT_EQ(channel_stats->remote_ssrc, kAc3Ssrc);
  ASSERT_TRUE(channel_stats->remote_rtcp);
  EXPECT_GT(channel_stats->remote_rtcp->round_trip_time, 0.0);
}

TEST_F(AudioChannelTest, RtcpReportBlockUpdatesEncoderPacketLossFraction) {
  auto mock_encoder = std::make_unique<NiceMock<MockAudioEncoder>>();
  ON_CALL(*mock_encoder, SampleRateHz()).WillByDefault(Return(8000));
  ON_CALL(*mock_encoder, NumChannels()).WillByDefault(Return(1));
  ON_CALL(*mock_encoder, RtpTimestampRateHz()).WillByDefault(Return(8000));

  constexpr uint8_t kFractionLostRaw = 51;
  EXPECT_CALL(*mock_encoder,
              OnReceivedUplinkPacketLossFraction(
                  FloatEq(static_cast<float>(kFractionLostRaw) / 256.0f)))
      .Times(1);

  audio_channel_->SetEncoder(kPcmuPayload, kPcmuFormat,
                             std::move(mock_encoder));

  // Report block for an unrelated SSRC should be ignored.
  rtcp::ReportBlock unrelated_block;
  unrelated_block.SetMediaSsrc(kLocalSsrc + 1);
  unrelated_block.SetFractionLost(128);
  rtcp::ReceiverReport unrelated_rr;
  unrelated_rr.SetSenderSsrc(0xdeadbeef);
  unrelated_rr.AddReportBlock(unrelated_block);
  audio_channel_->ReceivedRTCPPacket(unrelated_rr.Build());

  // Report block matching local SSRC should update encoder packet loss.
  rtcp::ReportBlock local_block;
  local_block.SetMediaSsrc(kLocalSsrc);
  local_block.SetFractionLost(kFractionLostRaw);
  rtcp::ReceiverReport local_rr;
  local_rr.SetSenderSsrc(0xdeadbeef);
  local_rr.AddReportBlock(local_block);
  audio_channel_->ReceivedRTCPPacket(local_rr.Build());
}

TEST_F(AudioChannelTest, OpusInbandFecEnabledUponRtcpPacketLossReport) {
  constexpr int kOpusPayload = 111;
  const SdpAudioFormat kOpusFormat = {
      "opus", 48000, 2, {{"minptime", "10"}, {"useinbandfec", "1"}}};

  audio_channel_->SetEncoder(
      kOpusPayload, kOpusFormat,
      encoder_factory_->Create(env_, kOpusFormat,
                               {.payload_type = kOpusPayload}));
  audio_channel_->SetReceiveCodecs({{kOpusPayload, kOpusFormat}});

  EXPECT_CALL(transport_, SendRtp)
      .WillRepeatedly([&](std::span<const uint8_t> packet, Unused) {
        audio_channel_->ReceivedRTPPacket(packet);
        return true;
      });

  int frame_order = 0;
  auto make_48khz_frame = [&]() {
    auto frame = std::make_unique<AudioFrame>();
    frame->sample_rate_hz_ = 48000;
    frame->samples_per_channel_ = 480;
    frame->num_channels_ = 1;
    frame->timestamp_ = frame->samples_per_channel_ * frame_order++;
    wave_generator_.GenerateNextFrame(frame.get());
    return frame;
  };

  AudioSender* audio_sender = audio_channel_->GetAudioSender();
  for (int i = 0; i < 10; ++i) {
    audio_sender->SendAudioData(make_48khz_frame());
    fake_clock_.AdvanceTimeMilliseconds(10);
  }
  EXPECT_EQ(
      audio_channel_->GetIngressStatistics().neteq_stats.fec_packets_received,
      0ULL);

  // Report ~20% packet loss (51 / 256) via RTCP Receiver Report.
  rtcp::ReportBlock report_block;
  report_block.SetMediaSsrc(kLocalSsrc);
  report_block.SetFractionLost(51);
  rtcp::ReceiverReport rr;
  rr.SetSenderSsrc(0xdeadbeef);
  rr.AddReportBlock(report_block);
  audio_channel_->ReceivedRTCPPacket(rr.Build());

  for (int i = 0; i < 10; ++i) {
    audio_sender->SendAudioData(make_48khz_frame());
    fake_clock_.AdvanceTimeMilliseconds(10);
  }
  EXPECT_GT(
      audio_channel_->GetIngressStatistics().neteq_stats.fec_packets_received,
      0ULL);
}

}  // namespace
}  // namespace webrtc
