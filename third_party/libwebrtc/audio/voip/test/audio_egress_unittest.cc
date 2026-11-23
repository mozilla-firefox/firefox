/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/voip/audio_egress.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include "api/audio/audio_frame.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/call/transport.h"
#include "api/environment/environment.h"
#include "api/rtp_headers.h"
#include "api/scoped_refptr.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/audio_mixer/sine_wave_generator.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_impl2.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_interface.h"
#include "rtc_base/event.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/mock_audio_encoder.h"
#include "test/mock_transport.h"
#include "test/time_controller/simulated_time_controller.h"

namespace webrtc {
namespace {

using ::testing::FloatEq;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Unused;

std::unique_ptr<ModuleRtpRtcpImpl2> CreateRtpStack(const Environment& env,
                                                   Transport* transport,
                                                   uint32_t remote_ssrc) {
  RtpRtcpInterface::Configuration rtp_config;
  rtp_config.audio = true;
  rtp_config.rtcp_report_interval_ms = 5000;
  rtp_config.outgoing_transport = transport;
  rtp_config.local_media_ssrc = remote_ssrc;
  rtp_config.rtcp_mode = RtcpMode::kCompound;
  auto rtp_rtcp = ModuleRtpRtcpImpl2::CreateSendModule(env, rtp_config);
  rtp_rtcp->SetSendingMediaStatus(false);
  return rtp_rtcp;
}

constexpr int16_t kAudioLevel = 3004;  // Used for sine wave level.

// AudioEgressTest configures audio egress by using Rtp Stack, fake clock,
// and task queue factory.  Encoder factory is needed to create codec and
// configure the RTP stack in audio egress.
class AudioEgressTest : public ::testing::Test {
 public:
  static constexpr uint16_t kSeqNum = 12345;
  static constexpr uint64_t kStartTime = 123456789;
  static constexpr uint32_t kRemoteSsrc = 0xDEADBEEF;
  const SdpAudioFormat kPcmuFormat = {"pcmu", 8000, 1};

  AudioEgressTest() : wave_generator_(1000.0, kAudioLevel) {
    encoder_factory_ = CreateBuiltinAudioEncoderFactory();
  }

  // Prepare test on audio egress by using PCMu codec with specific
  // sequence number and its status to be running.
  void SetUp() override {
    rtp_rtcp_ = CreateRtpStack(env_, &transport_, kRemoteSsrc);
    egress_ = std::make_unique<AudioEgress>(env_, rtp_rtcp_.get());
    constexpr int kPcmuPayload = 0;
    egress_->SetEncoder(kPcmuPayload, kPcmuFormat,
                        encoder_factory_->Create(
                            env_, kPcmuFormat, {.payload_type = kPcmuPayload}));
    egress_->StartSend();
    rtp_rtcp_->SetSequenceNumber(kSeqNum);
    rtp_rtcp_->SetSendingStatus(true);
  }

  // Make sure we have shut down rtp stack and reset egress for each test.
  void TearDown() override {
    egress_->StopSend();
    rtp_rtcp_->SetSendingStatus(false);
    egress_.reset();
    rtp_rtcp_.reset();
  }

  // Create an audio frame prepared for pcmu encoding. Timestamp is
  // increased per RTP specification which is the number of samples it contains.
  // Wave generator writes sine wave which has expected high level set
  // by kAudioLevel.
  std::unique_ptr<AudioFrame> GetAudioFrame(int order) {
    auto frame = std::make_unique<AudioFrame>();
    frame->sample_rate_hz_ = kPcmuFormat.clockrate_hz;
    frame->samples_per_channel_ = kPcmuFormat.clockrate_hz / 100;  // 10 ms.
    frame->num_channels_ = kPcmuFormat.num_channels;
    frame->timestamp_ = frame->samples_per_channel_ * order;
    wave_generator_.GenerateNextFrame(frame.get());
    return frame;
  }

  GlobalSimulatedTimeController time_controller_{Timestamp::Micros(kStartTime)};
  const Environment env_ = CreateTestEnvironment({.time = &time_controller_});
  NiceMock<MockTransport> transport_;
  SineWaveGenerator wave_generator_;
  std::unique_ptr<ModuleRtpRtcpImpl2> rtp_rtcp_;
  scoped_refptr<AudioEncoderFactory> encoder_factory_;
  std::unique_ptr<AudioEgress> egress_;
};

TEST_F(AudioEgressTest, SendingStatusAfterStartAndStop) {
  EXPECT_TRUE(egress_->IsSending());
  egress_->StopSend();
  EXPECT_FALSE(egress_->IsSending());
}

TEST_F(AudioEgressTest, ProcessAudioWithMute) {
  constexpr int kExpected = 10;
  Event event;
  int rtp_count = 0;
  RtpPacketReceived rtp;
  auto rtp_sent = [&](std::span<const uint8_t> packet, Unused) {
    rtp.Parse(packet);
    if (++rtp_count == kExpected) {
      event.Set();
    }
    return true;
  };

  EXPECT_CALL(transport_, SendRtp).WillRepeatedly(rtp_sent);

  egress_->SetMute(true);

  // Two 10 ms audio frames will result in rtp packet with ptime 20.
  for (size_t i = 0; i < kExpected * 2; i++) {
    egress_->SendAudioData(GetAudioFrame(i));
    time_controller_.AdvanceTime(TimeDelta::Millis(10));
  }

  event.Wait(TimeDelta::Seconds(1));
  EXPECT_EQ(rtp_count, kExpected);

  // we expect on pcmu payload to result in 255 for silenced payload
  RTPHeader header;
  rtp.GetHeader(&header);
  size_t packet_length = rtp.size();
  size_t payload_length = packet_length - header.headerLength;
  size_t payload_data_length = payload_length - header.paddingLength;
  const uint8_t* payload = rtp.data() + header.headerLength;
  for (size_t i = 0; i < payload_data_length; ++i) {
    EXPECT_EQ(*payload++, 255);
  }
}

TEST_F(AudioEgressTest, ProcessAudioWithSineWave) {
  constexpr int kExpected = 10;
  Event event;
  int rtp_count = 0;
  RtpPacketReceived rtp;
  auto rtp_sent = [&](std::span<const uint8_t> packet, Unused) {
    rtp.Parse(packet);
    if (++rtp_count == kExpected) {
      event.Set();
    }
    return true;
  };

  EXPECT_CALL(transport_, SendRtp).WillRepeatedly(rtp_sent);

  // Two 10 ms audio frames will result in rtp packet with ptime 20.
  for (size_t i = 0; i < kExpected * 2; i++) {
    egress_->SendAudioData(GetAudioFrame(i));
    time_controller_.AdvanceTime(TimeDelta::Millis(10));
  }

  event.Wait(TimeDelta::Seconds(1));
  EXPECT_EQ(rtp_count, kExpected);

  // we expect on pcmu to result in < 255 for payload with sine wave
  RTPHeader header;
  rtp.GetHeader(&header);
  size_t packet_length = rtp.size();
  size_t payload_length = packet_length - header.headerLength;
  size_t payload_data_length = payload_length - header.paddingLength;
  const uint8_t* payload = rtp.data() + header.headerLength;
  for (size_t i = 0; i < payload_data_length; ++i) {
    EXPECT_NE(*payload++, 255);
  }
}

TEST_F(AudioEgressTest, SkipAudioEncodingAfterStopSend) {
  constexpr int kExpected = 10;
  Event event;
  int rtp_count = 0;
  auto rtp_sent = [&](std::span<const uint8_t> /* packet */, Unused) {
    if (++rtp_count == kExpected) {
      event.Set();
    }
    return true;
  };

  EXPECT_CALL(transport_, SendRtp).WillRepeatedly(rtp_sent);

  // Two 10 ms audio frames will result in rtp packet with ptime 20.
  for (size_t i = 0; i < kExpected * 2; i++) {
    egress_->SendAudioData(GetAudioFrame(i));
    time_controller_.AdvanceTime(TimeDelta::Millis(10));
  }

  event.Wait(TimeDelta::Seconds(1));
  EXPECT_EQ(rtp_count, kExpected);

  // Now stop send and yet feed more data.
  egress_->StopSend();

  // It should be safe to exit the test case while encoder_queue_ has
  // outstanding data to process. We are making sure that this doesn't
  // result in crashes or sanitizer errors due to remaining data.
  for (size_t i = 0; i < kExpected * 2; i++) {
    egress_->SendAudioData(GetAudioFrame(i));
    time_controller_.AdvanceTime(TimeDelta::Millis(10));
  }
}

TEST_F(AudioEgressTest, ChangeEncoderFromPcmuToOpus) {
  std::optional<SdpAudioFormat> pcmu = egress_->GetEncoderFormat();
  EXPECT_TRUE(pcmu);
  EXPECT_EQ(pcmu->clockrate_hz, kPcmuFormat.clockrate_hz);
  EXPECT_EQ(pcmu->num_channels, kPcmuFormat.num_channels);

  constexpr int kOpusPayload = 120;
  const SdpAudioFormat kOpusFormat = {"opus", 48000, 2};

  egress_->SetEncoder(kOpusPayload, kOpusFormat,
                      encoder_factory_->Create(env_, kOpusFormat,
                                               {.payload_type = kOpusPayload}));

  std::optional<SdpAudioFormat> opus = egress_->GetEncoderFormat();
  EXPECT_TRUE(opus);
  EXPECT_EQ(opus->clockrate_hz, kOpusFormat.clockrate_hz);
  EXPECT_EQ(opus->num_channels, kOpusFormat.num_channels);
}

TEST_F(AudioEgressTest, SendDTMF) {
  constexpr int kExpected = 7;
  constexpr int kPayloadType = 100;
  constexpr int kDurationMs = 100;
  constexpr int kSampleRate = 8000;
  constexpr int kEvent = 3;

  egress_->RegisterTelephoneEventType(kPayloadType, kSampleRate);
  // 100 ms duration will produce total 7 DTMF
  // 1 @ 20 ms, 2 @ 40 ms, 3 @ 60 ms, 4 @ 80 ms
  // 5, 6, 7 @ 100 ms (last one sends 3 dtmf)
  egress_->SendTelephoneEvent(kEvent, kDurationMs);

  Event event;
  int dtmf_count = 0;
  auto is_dtmf = [&](RtpPacketReceived& rtp) {
    return (rtp.PayloadType() == kPayloadType &&
            rtp.SequenceNumber() == kSeqNum + dtmf_count &&
            rtp.padding_size() == 0 && rtp.Marker() == (dtmf_count == 0) &&
            rtp.Ssrc() == kRemoteSsrc);
  };

  // It's possible that we may have actual audio RTP packets along with
  // DTMF packtets.  We are only interested in the exact number of DTMF
  // packets rtp stack is emitting.
  auto rtp_sent = [&](std::span<const uint8_t> packet, Unused) {
    RtpPacketReceived rtp;
    rtp.Parse(packet);
    if (is_dtmf(rtp) && ++dtmf_count == kExpected) {
      event.Set();
    }
    return true;
  };

  EXPECT_CALL(transport_, SendRtp).WillRepeatedly(rtp_sent);

  // Two 10 ms audio frames will result in rtp packet with ptime 20.
  for (size_t i = 0; i < kExpected * 2; i++) {
    egress_->SendAudioData(GetAudioFrame(i));
    time_controller_.AdvanceTime(TimeDelta::Millis(10));
  }

  event.Wait(TimeDelta::Seconds(1));
  EXPECT_EQ(dtmf_count, kExpected);
}

TEST_F(AudioEgressTest, TestAudioInputLevelAndEnergyDuration) {
  // Per audio_level's kUpdateFrequency, we need more than 10 audio samples to
  // get audio level from input source.
  constexpr int kExpected = 6;
  Event event;
  int rtp_count = 0;
  auto rtp_sent = [&](std::span<const uint8_t> /* packet */, Unused) {
    if (++rtp_count == kExpected) {
      event.Set();
    }
    return true;
  };

  EXPECT_CALL(transport_, SendRtp).WillRepeatedly(rtp_sent);

  // Two 10 ms audio frames will result in rtp packet with ptime 20.
  for (size_t i = 0; i < kExpected * 2; i++) {
    egress_->SendAudioData(GetAudioFrame(i));
    time_controller_.AdvanceTime(TimeDelta::Millis(10));
  }

  event.Wait(/*give_up_after=*/TimeDelta::Seconds(1));
  EXPECT_EQ(rtp_count, kExpected);

  constexpr double kExpectedEnergy = 0.00016809565587789564;
  constexpr double kExpectedDuration = 0.11999999999999998;

  EXPECT_EQ(egress_->GetInputAudioLevel(), kAudioLevel);
  EXPECT_DOUBLE_EQ(egress_->GetInputTotalEnergy(), kExpectedEnergy);
  EXPECT_DOUBLE_EQ(egress_->GetInputTotalDuration(), kExpectedDuration);
}

TEST_F(AudioEgressTest, ForwardsUplinkPacketLossFractionToEncoder) {
  auto mock_encoder = std::make_unique<NiceMock<MockAudioEncoder>>();
  ON_CALL(*mock_encoder, SampleRateHz()).WillByDefault(Return(8000));
  ON_CALL(*mock_encoder, NumChannels()).WillByDefault(Return(1));
  ON_CALL(*mock_encoder, RtpTimestampRateHz()).WillByDefault(Return(8000));
  EXPECT_CALL(*mock_encoder, OnReceivedUplinkPacketLossFraction(FloatEq(0.25f)))
      .Times(1);

  constexpr int kPcmuPayload = 0;
  egress_->SetEncoder(kPcmuPayload, kPcmuFormat, std::move(mock_encoder));
  egress_->OnReceivedUplinkPacketLossFraction(0.25f);
  time_controller_.AdvanceTime(TimeDelta::Zero());
}

TEST_F(AudioEgressTest, UplinkPacketLossFractionWithoutEncoderIsNoOp) {
  AudioEgress unconfigured_egress(env_, rtp_rtcp_.get());
  unconfigured_egress.OnReceivedUplinkPacketLossFraction(0.25f);
  time_controller_.AdvanceTime(TimeDelta::Zero());
}

}  // namespace
}  // namespace webrtc
