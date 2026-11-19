/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/audio_receive_stream.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "api/audio_codecs/audio_format.h"
#include "api/crypto/frame_decryptor_interface.h"
#include "api/make_ref_counted.h"
#include "api/rtp_headers.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/task_queue_base.h"
#include "api/test/mock_audio_mixer.h"
#include "api/test/mock_frame_decryptor.h"
#include "audio/channel_receive.h"
#include "audio/conversion.h"
#include "audio/mock_voe_channel_proxy.h"
#include "call/audio_receive_stream.h"
#include "call/audio_state.h"
#include "call/rtp_packet_sink_interface.h"
#include "call/rtp_stream_receiver_controller.h"
#include "modules/audio_coding/include/audio_coding_module_typedefs.h"
#include "modules/audio_device/include/mock_audio_device.h"
#include "modules/audio_processing/include/mock_audio_processing.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "rtc_base/time_utils.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/mock_audio_decoder_factory.h"
#include "test/mock_transport.h"
#include "test/run_loop.h"

namespace webrtc {
namespace test {
namespace {

using ::testing::_;
using ::testing::FloatEq;
using ::testing::NiceMock;
using ::testing::Return;

AudioDecodingCallStats MakeAudioDecodeStatsForTest() {
  AudioDecodingCallStats audio_decode_stats;
  audio_decode_stats.calls_to_silence_generator = 234;
  audio_decode_stats.calls_to_neteq = 567;
  audio_decode_stats.decoded_normal = 890;
  audio_decode_stats.decoded_neteq_plc = 123;
  audio_decode_stats.decoded_codec_plc = 124;
  audio_decode_stats.decoded_cng = 456;
  audio_decode_stats.decoded_plc_cng = 789;
  audio_decode_stats.decoded_muted_output = 987;
  return audio_decode_stats;
}

constexpr uint32_t kRemoteSsrc = 1234;
constexpr uint32_t kLocalSsrc = 5678;
constexpr int kJitterBufferDelay = -7;
constexpr int kPlayoutBufferDelay = 302;
constexpr unsigned int kSpeechOutputLevel = 99;
constexpr double kTotalOutputEnergy = 0.25;
constexpr double kTotalOutputDuration = 0.5;
constexpr int64_t kPlayoutNtpTimestampMs = 5678;

const ChannelReceiveStatistics kChannelStats = {
    .packets_lost = 678,
    .jitter_ms = 234,
    .payload_bytes_received = -12,
    .header_and_padding_bytes_received = 567,
    .packets_received = 78,
    .packets_received_with_ect1 = 890,
    .packets_received_with_ce = 123};
const std::pair<int, SdpAudioFormat> kReceiveCodec = {
    123,
    {"codec_name_recv", 96000, 0}};
const NetworkStatistics kNetworkStats = {.currentBufferSize = 123,
                                         .preferredBufferSize = 456,
                                         .jitterPeaksFound = false,
                                         .totalSamplesReceived = 789012,
                                         .concealedSamples = 3456,
                                         .silentConcealedSamples = 123,
                                         .concealmentEvents = 456,
                                         .jitterBufferDelayMs = 789,
                                         .jitterBufferTargetDelayMs = 543,
                                         .jitterBufferMinimumDelayMs = 123,
                                         .jitterBufferEmittedCount = 222,
                                         .insertedSamplesForDeceleration = 432,
                                         .removedSamplesForAcceleration = 321,
                                         .fecPacketsReceived = 123,
                                         .fecPacketsDiscarded = 101,
                                         .totalProcessingDelayUs = 154,
                                         .packetsDiscarded = 989,
                                         .currentExpandRate = 789,
                                         .currentSpeechExpandRate = 12,
                                         .currentPreemptiveRate = 345,
                                         .currentAccelerateRate = 678,
                                         .currentSecondaryDecodedRate = 901,
                                         .currentSecondaryDiscardedRate = 0,
                                         .meanWaitingTimeMs = -1,
                                         .maxWaitingTimeMs = -1,
                                         .packetBufferFlushes = 0,
                                         .delayedPacketOutageSamples = 0,
                                         .relativePacketArrivalDelayMs = 135,
                                         .interruptionCount = -1,
                                         .totalInterruptionDurationMs = -1};
const AudioDecodingCallStats kAudioDecodeStats = MakeAudioDecodeStatsForTest();

class DummySinkValidator : public RtpSinkValidator {
 public:
  void OnSinkAdded(RtpPacketSinkInterface* sink) override {}
  void OnSinkRemoved(RtpPacketSinkInterface* sink) override {}
  bool IsValidSink(RtpPacketSinkInterface* sink) const override { return true; }
};

struct ConfigHelper {
  ConfigHelper(TaskQueueBase* network_thread, bool use_null_audio_processing)
      : ConfigHelper(network_thread,
                     make_ref_counted<MockAudioMixer>(),
                     use_null_audio_processing) {}

  ConfigHelper(TaskQueueBase* network_thread,
               scoped_refptr<MockAudioMixer> audio_mixer,
               bool use_null_audio_processing)
      : audio_mixer_(audio_mixer),
        rtp_stream_receiver_controller_(network_thread, &dummy_validator_) {
    AudioState::Config config;
    config.audio_mixer = audio_mixer_;
    config.audio_processing =
        use_null_audio_processing
            ? nullptr
            : make_ref_counted<NiceMock<MockAudioProcessing>>();
    config.audio_device_module =
        make_ref_counted<testing::NiceMock<MockAudioDeviceModule>>();
    audio_state_ = AudioState::Create(config);
    channel_receive_ = new ::testing::StrictMock<MockChannelReceive>();
    EXPECT_CALL(*channel_receive_, SetNACKStatus(true, 15)).Times(1);
    EXPECT_CALL(*channel_receive_, SetRtcpMode(_)).Times(1);
    EXPECT_CALL(*channel_receive_, SetReceiveCodecs(_))
        .WillRepeatedly([](const std::map<int, SdpAudioFormat>& codecs) {
          EXPECT_THAT(codecs, ::testing::IsEmpty());
        });
    EXPECT_CALL(*channel_receive_, remote_ssrc)
        .WillRepeatedly(Return(kRemoteSsrc));

    stream_config_.rtp.remote_ssrc = kRemoteSsrc;
    stream_config_.rtp.nack.rtp_history_ms = 300;
    stream_config_.rtcp_send_transport = &rtcp_send_transport_;
    stream_config_.decoder_factory =
        make_ref_counted<MockAudioDecoderFactory>();
  }

  std::unique_ptr<AudioReceiveStreamImpl> CreateAudioReceiveStream() {
    auto ret = std::make_unique<AudioReceiveStreamImpl>(
        CreateTestEnvironment(), std::move(stream_config_), audio_state_,
        std::unique_ptr<voe::ChannelReceiveInterface>(channel_receive_));
    ret->RegisterWithTransport(&rtp_stream_receiver_controller_);
    return ret;
  }

  AudioReceiveStreamInterface::Config& config() { return stream_config_; }
  scoped_refptr<MockAudioMixer> audio_mixer() { return audio_mixer_; }
  MockChannelReceive* channel_receive() { return channel_receive_; }

  void SetupMockForGetStats() {
    ASSERT_TRUE(channel_receive_);
    EXPECT_CALL(*channel_receive_, GetRTCPStatistics())
        .WillOnce(Return(kChannelStats));
    EXPECT_CALL(*channel_receive_, GetDelayEstimate())
        .WillOnce(Return(kJitterBufferDelay + kPlayoutBufferDelay));
    EXPECT_CALL(*channel_receive_, GetSpeechOutputLevelFullRange())
        .WillOnce(Return(kSpeechOutputLevel));
    EXPECT_CALL(*channel_receive_, GetTotalOutputEnergy())
        .WillOnce(Return(kTotalOutputEnergy));
    EXPECT_CALL(*channel_receive_, GetTotalOutputDuration())
        .WillOnce(Return(kTotalOutputDuration));
    EXPECT_CALL(*channel_receive_, GetNetworkStatistics(_))
        .WillOnce(Return(kNetworkStats));
    EXPECT_CALL(*channel_receive_, GetDecodingCallStatistics())
        .WillOnce(Return(kAudioDecodeStats));
    EXPECT_CALL(*channel_receive_, GetReceiveCodec())
        .WillOnce(Return(kReceiveCodec));
    EXPECT_CALL(*channel_receive_, GetCurrentEstimatedPlayoutNtpTimestampMs(_))
        .WillOnce(Return(kPlayoutNtpTimestampMs));
  }

 private:
  scoped_refptr<AudioState> audio_state_;
  scoped_refptr<MockAudioMixer> audio_mixer_;
  AudioReceiveStreamInterface::Config stream_config_;
  ::testing::StrictMock<MockChannelReceive>* channel_receive_ = nullptr;
  DummySinkValidator dummy_validator_;
  RtpStreamReceiverController rtp_stream_receiver_controller_;
  MockTransport rtcp_send_transport_;
};

std::vector<uint8_t> CreateRtcpSenderReport() {
  std::vector<uint8_t> packet;
  const size_t kRtcpSrLength = 28;  // In bytes.
  packet.resize(kRtcpSrLength);
  packet[0] = 0x80;  // Version 2.
  packet[1] = 0xc8;  // PT = 200, SR.
  // Length in number of 32-bit words - 1.
  ByteWriter<uint16_t>::WriteBigEndian(&packet[2], 6);
  ByteWriter<uint32_t>::WriteBigEndian(&packet[4], kLocalSsrc);
  return packet;
}
}  // namespace

TEST(AudioReceiveStreamTest, ConfigToString) {
  AudioReceiveStreamInterface::Config config;
  config.rtp.remote_ssrc = kRemoteSsrc;
  config.rtp.rtcp_mode = RtcpMode::kOff;
  EXPECT_EQ(
      "{rtp: {remote_ssrc: 1234, nack: "
      "{rtp_history_ms: 0}, rtcp: off}, "
      "rtcp_send_transport: null}",
      config.ToString());
}

TEST(AudioReceiveStreamTest, ConstructDestruct) {
  test::RunLoop loop;
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(loop.task_queue(), use_null_audio_processing);
    auto recv_stream = helper.CreateAudioReceiveStream();
    recv_stream->UnregisterFromTransport();
  }
}

TEST(AudioReceiveStreamTest, ReceiveRtcpPacket) {
  test::RunLoop loop;
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(loop.task_queue(), use_null_audio_processing);
    auto recv_stream = helper.CreateAudioReceiveStream();
    std::vector<uint8_t> rtcp_packet = CreateRtcpSenderReport();
    EXPECT_CALL(*helper.channel_receive(),
                ReceivedRTCPPacket(&rtcp_packet[0], rtcp_packet.size()))
        .WillOnce(Return());
    recv_stream->DeliverRtcp(rtcp_packet);
    recv_stream->UnregisterFromTransport();
  }
}

TEST(AudioReceiveStreamTest, GetStats) {
  test::RunLoop loop;
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(loop.task_queue(), use_null_audio_processing);
    auto recv_stream = helper.CreateAudioReceiveStream();
    helper.SetupMockForGetStats();
    AudioReceiveStreamInterface::Stats stats =
        recv_stream->GetStats(/*get_and_clear_legacy_stats=*/true);
    EXPECT_EQ(kRemoteSsrc, stats.remote_ssrc);
    EXPECT_EQ(kChannelStats.payload_bytes_received,
              stats.payload_bytes_received);
    EXPECT_EQ(kChannelStats.header_and_padding_bytes_received,
              stats.header_and_padding_bytes_received);
    EXPECT_EQ(static_cast<uint32_t>(kChannelStats.packets_received),
              stats.packets_received);
    EXPECT_EQ(kChannelStats.packets_lost, stats.packets_lost);
    EXPECT_EQ(kReceiveCodec.second.name, stats.codec_name);
    EXPECT_EQ(kChannelStats.jitter_ms, stats.jitter_ms);
    EXPECT_EQ(kNetworkStats.currentBufferSize, stats.jitter_buffer_ms);
    EXPECT_EQ(kNetworkStats.preferredBufferSize,
              stats.jitter_buffer_preferred_ms);
    EXPECT_EQ(static_cast<uint32_t>(kJitterBufferDelay + kPlayoutBufferDelay),
              stats.delay_estimate_ms);
    EXPECT_EQ(static_cast<int32_t>(kSpeechOutputLevel), stats.audio_level);
    EXPECT_EQ(kTotalOutputEnergy, stats.total_output_energy);
    EXPECT_EQ(kNetworkStats.totalSamplesReceived, stats.total_samples_received);
    EXPECT_EQ(kTotalOutputDuration, stats.total_output_duration);
    EXPECT_EQ(kNetworkStats.concealedSamples, stats.concealed_samples);
    EXPECT_EQ(kNetworkStats.concealmentEvents, stats.concealment_events);
    EXPECT_EQ(static_cast<double>(kNetworkStats.jitterBufferDelayMs) /
                  static_cast<double>(kNumMillisecsPerSec),
              stats.jitter_buffer_delay_seconds);
    EXPECT_EQ(kNetworkStats.jitterBufferEmittedCount,
              stats.jitter_buffer_emitted_count);
    EXPECT_EQ(static_cast<double>(kNetworkStats.jitterBufferTargetDelayMs) /
                  static_cast<double>(kNumMillisecsPerSec),
              stats.jitter_buffer_target_delay_seconds);
    EXPECT_EQ(static_cast<double>(kNetworkStats.jitterBufferMinimumDelayMs) /
                  static_cast<double>(kNumMillisecsPerSec),
              stats.jitter_buffer_minimum_delay_seconds);
    EXPECT_EQ(kNetworkStats.insertedSamplesForDeceleration,
              stats.inserted_samples_for_deceleration);
    EXPECT_EQ(kNetworkStats.removedSamplesForAcceleration,
              stats.removed_samples_for_acceleration);
    EXPECT_EQ(kNetworkStats.fecPacketsReceived, stats.fec_packets_received);
    EXPECT_EQ(kNetworkStats.fecPacketsDiscarded, stats.fec_packets_discarded);
    EXPECT_EQ(static_cast<double>(kNetworkStats.totalProcessingDelayUs) /
                  static_cast<double>(kNumMicrosecsPerSec),
              stats.total_processing_delay_seconds);
    EXPECT_EQ(kNetworkStats.packetsDiscarded, stats.packets_discarded);
    EXPECT_EQ(Q14ToFloat(kNetworkStats.currentExpandRate), stats.expand_rate);
    EXPECT_EQ(Q14ToFloat(kNetworkStats.currentSpeechExpandRate),
              stats.speech_expand_rate);
    EXPECT_EQ(Q14ToFloat(kNetworkStats.currentSecondaryDecodedRate),
              stats.secondary_decoded_rate);
    EXPECT_EQ(Q14ToFloat(kNetworkStats.currentSecondaryDiscardedRate),
              stats.secondary_discarded_rate);
    EXPECT_EQ(Q14ToFloat(kNetworkStats.currentAccelerateRate),
              stats.accelerate_rate);
    EXPECT_EQ(Q14ToFloat(kNetworkStats.currentPreemptiveRate),
              stats.preemptive_expand_rate);
    EXPECT_EQ(kNetworkStats.packetBufferFlushes, stats.jitter_buffer_flushes);
    EXPECT_EQ(kNetworkStats.delayedPacketOutageSamples,
              stats.delayed_packet_outage_samples);
    EXPECT_EQ(static_cast<double>(kNetworkStats.relativePacketArrivalDelayMs) /
                  static_cast<double>(kNumMillisecsPerSec),
              stats.relative_packet_arrival_delay_seconds);
    EXPECT_EQ(kNetworkStats.interruptionCount, stats.interruption_count);
    EXPECT_EQ(kNetworkStats.totalInterruptionDurationMs,
              stats.total_interruption_duration_ms);

    EXPECT_EQ(kAudioDecodeStats.calls_to_silence_generator,
              stats.decoding_calls_to_silence_generator);
    EXPECT_EQ(kAudioDecodeStats.calls_to_neteq, stats.decoding_calls_to_neteq);
    EXPECT_EQ(kAudioDecodeStats.decoded_normal, stats.decoding_normal);
    EXPECT_EQ(kAudioDecodeStats.decoded_neteq_plc, stats.decoding_plc);
    EXPECT_EQ(kAudioDecodeStats.decoded_codec_plc, stats.decoding_codec_plc);
    EXPECT_EQ(kAudioDecodeStats.decoded_cng, stats.decoding_cng);
    EXPECT_EQ(kAudioDecodeStats.decoded_plc_cng, stats.decoding_plc_cng);
    EXPECT_EQ(kAudioDecodeStats.decoded_muted_output,
              stats.decoding_muted_output);
    EXPECT_EQ(kChannelStats.capture_start_ntp_time_ms,
              stats.capture_start_ntp_time_ms);
    EXPECT_EQ(kPlayoutNtpTimestampMs, stats.estimated_playout_ntp_timestamp_ms);
    recv_stream->UnregisterFromTransport();
  }
}

TEST(AudioReceiveStreamTest, SetGain) {
  test::RunLoop loop;
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(loop.task_queue(), use_null_audio_processing);
    auto recv_stream = helper.CreateAudioReceiveStream();
    EXPECT_CALL(*helper.channel_receive(),
                SetChannelOutputVolumeScaling(FloatEq(0.765f)));
    recv_stream->SetGain(0.765f);
    recv_stream->UnregisterFromTransport();
  }
}

TEST(AudioReceiveStreamTest, StreamsShouldBeAddedToMixerOnceOnStart) {
  test::RunLoop loop;
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper1(loop.task_queue(), use_null_audio_processing);
    ConfigHelper helper2(loop.task_queue(), helper1.audio_mixer(),
                         use_null_audio_processing);
    auto recv_stream1 = helper1.CreateAudioReceiveStream();
    auto recv_stream2 = helper2.CreateAudioReceiveStream();

    EXPECT_CALL(*helper1.channel_receive(), StartPlayout()).Times(1);
    EXPECT_CALL(*helper2.channel_receive(), StartPlayout()).Times(1);
    EXPECT_CALL(*helper1.channel_receive(), StopPlayout()).Times(1);
    EXPECT_CALL(*helper2.channel_receive(), StopPlayout()).Times(1);
    EXPECT_CALL(*helper1.audio_mixer(), AddSource(recv_stream1.get()))
        .WillOnce(Return(true));
    EXPECT_CALL(*helper1.audio_mixer(), AddSource(recv_stream2.get()))
        .WillOnce(Return(true));
    EXPECT_CALL(*helper1.audio_mixer(), RemoveSource(recv_stream1.get()))
        .Times(1);
    EXPECT_CALL(*helper1.audio_mixer(), RemoveSource(recv_stream2.get()))
        .Times(1);

    recv_stream1->Start();
    recv_stream2->Start();

    // One more should not result in any more mixer sources added.
    recv_stream1->Start();

    // Stop stream before it is being destructed.
    recv_stream2->Stop();

    recv_stream1->UnregisterFromTransport();
    recv_stream2->UnregisterFromTransport();
  }
}

TEST(AudioReceiveStreamTest, ReconfigureWithUpdatedConfig) {
  test::RunLoop loop;
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(loop.task_queue(), use_null_audio_processing);
    auto recv_stream = helper.CreateAudioReceiveStream();

    MockChannelReceive& channel_receive = *helper.channel_receive();

    std::map<int, SdpAudioFormat> new_decoder_map;
    new_decoder_map.emplace(1, SdpAudioFormat("foo", 8000, 1));
    EXPECT_CALL(channel_receive, SetReceiveCodecs(new_decoder_map));
    recv_stream->SetDecoderMap(new_decoder_map);

    EXPECT_CALL(channel_receive, SetNACKStatus(true, 15 + 1)).Times(1);
    recv_stream->SetNackHistory(300 + 20);

    recv_stream->UnregisterFromTransport();
  }
}

TEST(AudioReceiveStreamTest, SetFrameDecryptorForwardsToChannelReceive) {
  test::RunLoop loop;
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(loop.task_queue(), use_null_audio_processing);
    auto recv_stream = helper.CreateAudioReceiveStream();

    scoped_refptr<FrameDecryptorInterface> mock_frame_decryptor =
        make_ref_counted<MockFrameDecryptor>();
    EXPECT_CALL(*helper.channel_receive(),
                SetFrameDecryptor(mock_frame_decryptor));

    recv_stream->SetFrameDecryptor(mock_frame_decryptor);
    recv_stream->UnregisterFromTransport();
  }
}

}  // namespace test
}  // namespace webrtc
