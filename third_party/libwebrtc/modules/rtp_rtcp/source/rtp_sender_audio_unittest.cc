/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/rtp_sender_audio.h"

#include <memory>
#include <vector>

#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_impl2.h"
#include "rtc_base/thread.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

namespace {
enum : int {  // The first valid value is 1.
  kAudioLevelExtensionId = 1,
  kAbsoluteCaptureTimeExtensionId = 2,
};

const uint16_t kSeqNum = 33;
const uint32_t kSsrc = 725242;
const uint64_t kStartTime = 123456789;

using ::testing::ElementsAreArray;

class LoopbackTransportTest : public webrtc::Transport {
 public:
  LoopbackTransportTest() {
    receivers_extensions_.Register<AudioLevelExtension>(kAudioLevelExtensionId);
    receivers_extensions_.Register<AbsoluteCaptureTimeExtension>(
        kAbsoluteCaptureTimeExtensionId);
  }

  bool SendRtp(rtc::ArrayView<const uint8_t> data,
               const PacketOptions& /*options*/) override {
    sent_packets_.push_back(RtpPacketReceived(&receivers_extensions_));
    EXPECT_TRUE(sent_packets_.back().Parse(data));
    return true;
  }
  bool SendRtcp(rtc::ArrayView<const uint8_t> data) override { return false; }
  const RtpPacketReceived& last_sent_packet() { return sent_packets_.back(); }
  int packets_sent() { return sent_packets_.size(); }

 private:
  RtpHeaderExtensionMap receivers_extensions_;
  std::vector<RtpPacketReceived> sent_packets_;
};

}  // namespace

class RtpSenderAudioTest : public ::testing::Test {
 public:
  RtpSenderAudioTest()
      : fake_clock_(kStartTime),
        rtp_module_(ModuleRtpRtcpImpl2::Create([&] {
          RtpRtcpInterface::Configuration config;
          config.audio = true;
          config.clock = &fake_clock_;
          config.outgoing_transport = &transport_;
          config.local_media_ssrc = kSsrc;
          return config;
        }())),
        rtp_sender_audio_(
            std::make_unique<RTPSenderAudio>(&fake_clock_,
                                             rtp_module_->RtpSender())) {
    rtp_module_->SetSequenceNumber(kSeqNum);
  }

  rtc::AutoThread main_thread_;
  SimulatedClock fake_clock_;
  LoopbackTransportTest transport_;
  std::unique_ptr<ModuleRtpRtcpImpl2> rtp_module_;
  std::unique_ptr<RTPSenderAudio> rtp_sender_audio_;
};

TEST_F(RtpSenderAudioTest, SendAudio) {
  const char payload_name[] = "PAYLOAD_NAME";
  const uint8_t payload_type = 127;
  ASSERT_EQ(0, rtp_sender_audio_->RegisterAudioPayload(
                   payload_name, payload_type, 48000, 0, 1500));
  uint8_t payload[] = {47, 11, 32, 93, 89};

  ASSERT_TRUE(rtp_sender_audio_->SendAudio(
      {.payload = payload, .payload_id = payload_type}));

  auto sent_payload = transport_.last_sent_packet().payload();
  EXPECT_THAT(sent_payload, ElementsAreArray(payload));
}

TEST_F(RtpSenderAudioTest, SendAudioWithAudioLevelExtension) {
  const uint8_t kAudioLevel = 0x5a;
  rtp_module_->RegisterRtpHeaderExtension(AudioLevelExtension::Uri(),
                                          kAudioLevelExtensionId);

  const char payload_name[] = "PAYLOAD_NAME";
  const uint8_t payload_type = 127;
  ASSERT_EQ(0, rtp_sender_audio_->RegisterAudioPayload(
                   payload_name, payload_type, 48000, 0, 1500));

  uint8_t payload[] = {47, 11, 32, 93, 89};

  ASSERT_TRUE(
      rtp_sender_audio_->SendAudio({.type = AudioFrameType::kAudioFrameCN,
                                    .payload = payload,
                                    .payload_id = payload_type,
                                    .audio_level_dbov = kAudioLevel}));

  auto sent_payload = transport_.last_sent_packet().payload();
  EXPECT_THAT(sent_payload, ElementsAreArray(payload));
  // Verify AudioLevel extension.
  AudioLevel audio_level;
  EXPECT_TRUE(transport_.last_sent_packet().GetExtension<AudioLevelExtension>(
      &audio_level));
  EXPECT_EQ(kAudioLevel, audio_level.level());
  EXPECT_FALSE(audio_level.voice_activity());
}

TEST_F(RtpSenderAudioTest, SendAudioWithoutAbsoluteCaptureTime) {
  constexpr Timestamp kAbsoluteCaptureTimestamp = Timestamp::Millis(521);
  const char payload_name[] = "audio";
  const uint8_t payload_type = 127;
  ASSERT_EQ(0, rtp_sender_audio_->RegisterAudioPayload(
                   payload_name, payload_type, 48000, 0, 1500));
  uint8_t payload[] = {47, 11, 32, 93, 89};

  ASSERT_TRUE(rtp_sender_audio_->SendAudio(
      {.payload = payload,
       .payload_id = payload_type,
       .capture_time = kAbsoluteCaptureTimestamp}));

  // AbsoluteCaptureTimeExtension wasn't registered, thus can't be sent.
  EXPECT_FALSE(transport_.last_sent_packet()
                   .HasExtension<AbsoluteCaptureTimeExtension>());
}

TEST_F(RtpSenderAudioTest,
       SendAudioWithAbsoluteCaptureTimeWithCaptureClockOffset) {
  rtp_module_->RegisterRtpHeaderExtension(AbsoluteCaptureTimeExtension::Uri(),
                                          kAbsoluteCaptureTimeExtensionId);
  constexpr Timestamp kAbsoluteCaptureTimestamp = Timestamp::Millis(521);
  const char payload_name[] = "audio";
  const uint8_t payload_type = 127;
  ASSERT_EQ(0, rtp_sender_audio_->RegisterAudioPayload(
                   payload_name, payload_type, 48000, 0, 1500));
  uint8_t payload[] = {47, 11, 32, 93, 89};

  ASSERT_TRUE(rtp_sender_audio_->SendAudio(
      {.payload = payload,
       .payload_id = payload_type,
       .capture_time = kAbsoluteCaptureTimestamp}));

  auto absolute_capture_time =
      transport_.last_sent_packet()
          .GetExtension<AbsoluteCaptureTimeExtension>();
  ASSERT_TRUE(absolute_capture_time);
  EXPECT_EQ(NtpTime(absolute_capture_time->absolute_capture_timestamp),
            fake_clock_.ConvertTimestampToNtpTime(kAbsoluteCaptureTimestamp));
  EXPECT_EQ(absolute_capture_time->estimated_capture_clock_offset, 0);
}

// As RFC4733, named telephone events are carried as part of the audio stream
// and must use the same sequence number and timestamp base as the regular
// audio channel.
// This test checks the marker bit for the first packet and the consequent
// packets of the same telephone event. Since it is specifically for DTMF
// events, ignoring audio packets and sending kEmptyFrame instead of those.
TEST_F(RtpSenderAudioTest, CheckMarkerBitForTelephoneEvents) {
  const char* kDtmfPayloadName = "telephone-event";
  const uint32_t kPayloadFrequency = 8000;
  const uint8_t kPayloadType = 126;
  ASSERT_EQ(0, rtp_sender_audio_->RegisterAudioPayload(
                   kDtmfPayloadName, kPayloadType, kPayloadFrequency, 0, 0));
  // For Telephone events, payload is not added to the registered payload list,
  // it will register only the payload used for audio stream.
  // Registering the payload again for audio stream with different payload name.
  const char* kPayloadName = "payload_name";
  ASSERT_EQ(0, rtp_sender_audio_->RegisterAudioPayload(
                   kPayloadName, kPayloadType, kPayloadFrequency, 1, 0));
  // Start time is arbitrary.
  uint32_t capture_timestamp = 12345;
  // DTMF event key=9, duration=500 and attenuationdB=10
  rtp_sender_audio_->SendTelephoneEvent(9, 500, 10);
  // During start, it takes the starting timestamp as last sent timestamp.
  // The duration is calculated as the difference of current and last sent
  // timestamp. So for first call it will skip since the duration is zero.
  ASSERT_TRUE(
      rtp_sender_audio_->SendAudio({.type = AudioFrameType::kEmptyFrame,
                                    .payload_id = kPayloadType,
                                    .rtp_timestamp = capture_timestamp}));

  // DTMF Sample Length is (Frequency/1000) * Duration.
  // So in this case, it is (8000/1000) * 500 = 4000.
  // Sending it as two packets.
  ASSERT_TRUE(rtp_sender_audio_->SendAudio(
      {.type = AudioFrameType::kEmptyFrame,
       .payload_id = kPayloadType,
       .rtp_timestamp = capture_timestamp + 2000}));

  // Marker Bit should be set to 1 for first packet.
  EXPECT_TRUE(transport_.last_sent_packet().Marker());

  ASSERT_TRUE(rtp_sender_audio_->SendAudio(
      {.type = AudioFrameType::kEmptyFrame,
       .payload_id = kPayloadType,
       .rtp_timestamp = capture_timestamp + 4000}));

  // Marker Bit should be set to 0 for rest of the packets.
  EXPECT_FALSE(transport_.last_sent_packet().Marker());
}

TEST_F(RtpSenderAudioTest, SendsCsrcs) {
  const char payload_name[] = "audio";
  const uint8_t payload_type = 127;
  ASSERT_EQ(0, rtp_sender_audio_->RegisterAudioPayload(
                   payload_name, payload_type, 48000, 0, 1500));
  uint8_t payload[] = {47, 11, 32, 93, 89};

  std::vector<uint32_t> csrcs({123, 456, 789});

  ASSERT_TRUE(rtp_sender_audio_->SendAudio(
      {.payload = payload, .payload_id = payload_type, .csrcs = csrcs}));

  EXPECT_EQ(transport_.last_sent_packet().Csrcs(), csrcs);
}

}  // namespace webrtc
