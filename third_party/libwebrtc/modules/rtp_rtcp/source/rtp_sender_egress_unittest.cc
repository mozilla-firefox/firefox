/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/rtp_sender_egress.h"

#include <cstdint>
#include <string>

#include "absl/types/optional.h"
#include "api/array_view.h"
#include "api/call/transport.h"
#include "api/field_trials_registry.h"
#include "api/units/data_size.h"
#include "api/units/timestamp.h"
#include "logging/rtc_event_log/mock/mock_rtc_event_log.h"
#include "modules/rtp_rtcp/include/flexfec_sender.h"
#include "modules/rtp_rtcp/include/rtp_rtcp.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_history.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "test/explicit_key_value_config.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/time_controller/simulated_time_controller.h"

namespace webrtc {
namespace {

using ::testing::_;
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Field;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::StrictMock;

constexpr Timestamp kStartTime = Timestamp::Millis(123456789);
constexpr int kDefaultPayloadType = 100;
constexpr int kFlexfectPayloadType = 110;
constexpr uint16_t kStartSequenceNumber = 33;
constexpr uint32_t kSsrc = 725242;
constexpr uint32_t kRtxSsrc = 12345;
constexpr uint32_t kFlexFecSsrc = 23456;
enum : int {
  kTransportSequenceNumberExtensionId = 1,
  kAbsoluteSendTimeExtensionId,
  kTransmissionOffsetExtensionId,
  kVideoTimingExtensionId,
};

class MockSendPacketObserver : public SendPacketObserver {
 public:
  MOCK_METHOD(void,
              OnSendPacket,
              (absl::optional<uint16_t>, Timestamp, uint32_t),
              (override));
};

class MockStreamDataCountersCallback : public StreamDataCountersCallback {
 public:
  MOCK_METHOD(void,
              DataCountersUpdated,
              (const StreamDataCounters& counters, uint32_t ssrc),
              (override));
};

struct TransmittedPacket {
  TransmittedPacket(rtc::ArrayView<const uint8_t> data,
                    const PacketOptions& packet_options,
                    RtpHeaderExtensionMap* extensions)
      : packet(extensions), options(packet_options) {
    EXPECT_TRUE(packet.Parse(data));
  }
  RtpPacketReceived packet;
  PacketOptions options;
};

class TestTransport : public Transport {
 public:
  explicit TestTransport(RtpHeaderExtensionMap* extensions)
      : total_data_sent_(DataSize::Zero()), extensions_(extensions) {}
  MOCK_METHOD(void, SentRtp, (const PacketOptions& options), ());
  bool SendRtp(rtc::ArrayView<const uint8_t> packet,
               const PacketOptions& options) override {
    total_data_sent_ += DataSize::Bytes(packet.size());
    last_packet_.emplace(packet, options, extensions_);
    SentRtp(options);
    return true;
  }

  bool SendRtcp(rtc::ArrayView<const uint8_t>) override {
    RTC_CHECK_NOTREACHED();
  }

  absl::optional<TransmittedPacket> last_packet() { return last_packet_; }

 private:
  DataSize total_data_sent_;
  absl::optional<TransmittedPacket> last_packet_;
  RtpHeaderExtensionMap* const extensions_;
};

}  // namespace

class RtpSenderEgressTest : public ::testing::Test {
 protected:
  RtpSenderEgressTest()
      : time_controller_(kStartTime),
        clock_(time_controller_.GetClock()),
        transport_(&header_extensions_),
        packet_history_(clock_, /*enable_rtx_padding_prioritization=*/true),
        trials_(""),
        sequence_number_(kStartSequenceNumber) {}

  std::unique_ptr<RtpSenderEgress> CreateRtpSenderEgress() {
    return std::make_unique<RtpSenderEgress>(DefaultConfig(), &packet_history_);
  }

  RtpRtcpInterface::Configuration DefaultConfig() {
    RtpRtcpInterface::Configuration config;
    config.audio = false;
    config.clock = clock_;
    config.outgoing_transport = &transport_;
    config.local_media_ssrc = kSsrc;
    config.rtx_send_ssrc = kRtxSsrc;
    config.fec_generator = nullptr;
    config.event_log = &mock_rtc_event_log_;
    config.send_packet_observer = &send_packet_observer_;
    config.rtp_stats_callback = &mock_rtp_stats_callback_;
    config.populate_network2_timestamp = false;
    config.field_trials = &trials_;
    return config;
  }

  std::unique_ptr<RtpPacketToSend> BuildRtpPacket(bool marker_bit,
                                                  int64_t capture_time_ms) {
    auto packet = std::make_unique<RtpPacketToSend>(&header_extensions_);
    packet->SetSsrc(kSsrc);
    packet->ReserveExtension<AbsoluteSendTime>();
    packet->ReserveExtension<TransmissionOffset>();
    packet->ReserveExtension<TransportSequenceNumber>();

    packet->SetPayloadType(kDefaultPayloadType);
    packet->set_packet_type(RtpPacketMediaType::kVideo);
    packet->SetMarker(marker_bit);
    packet->SetTimestamp(capture_time_ms * 90);
    packet->set_capture_time(Timestamp::Millis(capture_time_ms));
    packet->SetSequenceNumber(sequence_number_++);
    return packet;
  }

  std::unique_ptr<RtpPacketToSend> BuildRtpPacket() {
    return BuildRtpPacket(/*marker_bit=*/true, clock_->CurrentTime().ms());
  }

  GlobalSimulatedTimeController time_controller_;
  Clock* const clock_;
  NiceMock<MockRtcEventLog> mock_rtc_event_log_;
  NiceMock<MockStreamDataCountersCallback> mock_rtp_stats_callback_;
  NiceMock<MockSendPacketObserver> send_packet_observer_;
  RtpHeaderExtensionMap header_extensions_;
  NiceMock<TestTransport> transport_;
  RtpPacketHistory packet_history_;
  test::ExplicitKeyValueConfig trials_;
  uint16_t sequence_number_;
};

TEST_F(RtpSenderEgressTest, SendsPacketsOneByOneWhenNotBatching) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();
  EXPECT_CALL(transport_,
              SentRtp(AllOf(Field(&PacketOptions::last_packet_in_batch, false),
                            Field(&PacketOptions::batchable, false))));
  sender->SendPacket(BuildRtpPacket(), PacedPacketInfo());
}

TEST_F(RtpSenderEgressTest, SendsPacketsOneByOneWhenBatchingWithAudio) {
  auto config = DefaultConfig();
  config.enable_send_packet_batching = true;
  config.audio = true;
  auto sender = std::make_unique<RtpSenderEgress>(config, &packet_history_);
  EXPECT_CALL(transport_,
              SentRtp(AllOf(Field(&PacketOptions::last_packet_in_batch, false),
                            Field(&PacketOptions::batchable, false))))
      .Times(2);
  sender->SendPacket(BuildRtpPacket(), PacedPacketInfo());
  sender->SendPacket(BuildRtpPacket(), PacedPacketInfo());
}

TEST_F(RtpSenderEgressTest, CollectsPacketsWhenBatchingWithVideo) {
  auto config = DefaultConfig();
  config.enable_send_packet_batching = true;
  auto sender = std::make_unique<RtpSenderEgress>(config, &packet_history_);
  sender->SendPacket(BuildRtpPacket(), PacedPacketInfo());
  sender->SendPacket(BuildRtpPacket(), PacedPacketInfo());
  InSequence s;
  EXPECT_CALL(transport_,
              SentRtp(AllOf(Field(&PacketOptions::last_packet_in_batch, false),
                            Field(&PacketOptions::batchable, true))));
  EXPECT_CALL(transport_,
              SentRtp(AllOf(Field(&PacketOptions::last_packet_in_batch, true),
                            Field(&PacketOptions::batchable, true))));
  sender->OnBatchComplete();
}

TEST_F(RtpSenderEgressTest, PacketOptionsIsRetransmitSetByPacketType) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();

  std::unique_ptr<RtpPacketToSend> media_packet = BuildRtpPacket();
  auto sequence_number = media_packet->SequenceNumber();
  media_packet->set_packet_type(RtpPacketMediaType::kVideo);
  sender->SendPacket(std::move(media_packet), PacedPacketInfo());
  EXPECT_FALSE(transport_.last_packet()->options.is_retransmit);

  std::unique_ptr<RtpPacketToSend> retransmission = BuildRtpPacket();
  retransmission->set_packet_type(RtpPacketMediaType::kRetransmission);
  retransmission->set_retransmitted_sequence_number(sequence_number);
  sender->SendPacket(std::move(retransmission), PacedPacketInfo());
  EXPECT_TRUE(transport_.last_packet()->options.is_retransmit);
}

TEST_F(RtpSenderEgressTest, DoesnSetIncludedInAllocationByDefault) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();

  std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
  sender->SendPacket(std::move(packet), PacedPacketInfo());
  EXPECT_FALSE(transport_.last_packet()->options.included_in_feedback);
  EXPECT_FALSE(transport_.last_packet()->options.included_in_allocation);
}

TEST_F(RtpSenderEgressTest,
       SetsIncludedInFeedbackWhenTransportSequenceNumberExtensionIsRegistered) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();

  header_extensions_.RegisterByUri(kTransportSequenceNumberExtensionId,
                                   TransportSequenceNumber::Uri());
  std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
  packet->set_transport_sequence_number(1);
  sender->SendPacket(std::move(packet), PacedPacketInfo());
  EXPECT_TRUE(transport_.last_packet()->options.included_in_feedback);
}

TEST_F(
    RtpSenderEgressTest,
    SetsIncludedInAllocationWhenTransportSequenceNumberExtensionIsRegistered) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();

  header_extensions_.RegisterByUri(kTransportSequenceNumberExtensionId,
                                   TransportSequenceNumber::Uri());
  std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
  packet->set_transport_sequence_number(1);
  sender->SendPacket(std::move(packet), PacedPacketInfo());
  EXPECT_TRUE(transport_.last_packet()->options.included_in_allocation);
}

TEST_F(RtpSenderEgressTest,
       SetsIncludedInAllocationWhenForcedAsPartOfAllocation) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();
  sender->ForceIncludeSendPacketsInAllocation(true);

  std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
  sender->SendPacket(std::move(packet), PacedPacketInfo());
  EXPECT_FALSE(transport_.last_packet()->options.included_in_feedback);
  EXPECT_TRUE(transport_.last_packet()->options.included_in_allocation);
}

TEST_F(RtpSenderEgressTest,
       DoesntWriteTransmissionOffsetOnRtxPaddingBeforeMedia) {
  header_extensions_.RegisterByUri(kTransmissionOffsetExtensionId,
                                   TransmissionOffset::Uri());

  // Prior to sending media, timestamps are 0.
  std::unique_ptr<RtpPacketToSend> padding =
      BuildRtpPacket(/*marker_bit=*/true, /*capture_time_ms=*/0);
  padding->set_packet_type(RtpPacketMediaType::kPadding);
  padding->SetSsrc(kRtxSsrc);
  EXPECT_TRUE(padding->HasExtension<TransmissionOffset>());

  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();
  sender->SendPacket(std::move(padding), PacedPacketInfo());

  absl::optional<int32_t> offset =
      transport_.last_packet()->packet.GetExtension<TransmissionOffset>();
  EXPECT_EQ(offset, 0);
}

TEST_F(RtpSenderEgressTest, WritesPacerExitToTimingExtension) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();
  header_extensions_.RegisterByUri(kVideoTimingExtensionId,
                                   VideoTimingExtension::Uri());

  std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
  packet->SetExtension<VideoTimingExtension>(VideoSendTiming{});

  const int kStoredTimeInMs = 100;
  time_controller_.AdvanceTime(TimeDelta::Millis(kStoredTimeInMs));
  sender->SendPacket(std::move(packet), PacedPacketInfo());
  ASSERT_TRUE(transport_.last_packet().has_value());

  VideoSendTiming video_timing;
  EXPECT_TRUE(
      transport_.last_packet()->packet.GetExtension<VideoTimingExtension>(
          &video_timing));
  EXPECT_EQ(video_timing.pacer_exit_delta_ms, kStoredTimeInMs);
}

TEST_F(RtpSenderEgressTest, WritesNetwork2ToTimingExtension) {
  RtpRtcpInterface::Configuration rtp_config = DefaultConfig();
  rtp_config.populate_network2_timestamp = true;
  auto sender = std::make_unique<RtpSenderEgress>(rtp_config, &packet_history_);
  header_extensions_.RegisterByUri(kVideoTimingExtensionId,
                                   VideoTimingExtension::Uri());

  const uint16_t kPacerExitMs = 1234u;
  std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
  VideoSendTiming send_timing = {};
  send_timing.pacer_exit_delta_ms = kPacerExitMs;
  packet->SetExtension<VideoTimingExtension>(send_timing);

  const int kStoredTimeInMs = 100;
  time_controller_.AdvanceTime(TimeDelta::Millis(kStoredTimeInMs));
  sender->SendPacket(std::move(packet), PacedPacketInfo());
  ASSERT_TRUE(transport_.last_packet().has_value());

  VideoSendTiming video_timing;
  EXPECT_TRUE(
      transport_.last_packet()->packet.GetExtension<VideoTimingExtension>(
          &video_timing));
  EXPECT_EQ(video_timing.network2_timestamp_delta_ms, kStoredTimeInMs);
  EXPECT_EQ(video_timing.pacer_exit_delta_ms, kPacerExitMs);
}

TEST_F(RtpSenderEgressTest, WritesTransportSequenceNumberExtensionIfAllocated) {
  RtpSenderEgress sender(DefaultConfig(), &packet_history_);
  header_extensions_.RegisterByUri(kTransportSequenceNumberExtensionId,
                                   TransportSequenceNumber::Uri());
  std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
  ASSERT_TRUE(packet->HasExtension<TransportSequenceNumber>());
  const int64_t kTransportSequenceNumber = 0xFFFF000F;
  packet->set_transport_sequence_number(kTransportSequenceNumber);

  sender.SendPacket(std::move(packet), PacedPacketInfo());

  ASSERT_TRUE(transport_.last_packet().has_value());
  EXPECT_EQ(
      transport_.last_packet()->packet.GetExtension<TransportSequenceNumber>(),
      kTransportSequenceNumber & 0xFFFF);
}

TEST_F(RtpSenderEgressTest, OnSendPacketUpdated) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();
  header_extensions_.RegisterByUri(kTransportSequenceNumberExtensionId,
                                   TransportSequenceNumber::Uri());

  const uint16_t kTransportSequenceNumber = 1;
  EXPECT_CALL(
      send_packet_observer_,
      OnSendPacket(Eq(kTransportSequenceNumber), clock_->CurrentTime(), kSsrc));
  std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
  packet->set_transport_sequence_number(kTransportSequenceNumber);
  sender->SendPacket(std::move(packet), PacedPacketInfo());
}

TEST_F(RtpSenderEgressTest, OnSendPacketUpdatedWithoutTransportSequenceNumber) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();

  EXPECT_CALL(send_packet_observer_,
              OnSendPacket(Eq(absl::nullopt), clock_->CurrentTime(), kSsrc));
  sender->SendPacket(BuildRtpPacket(), PacedPacketInfo());
}

TEST_F(RtpSenderEgressTest, OnSendPacketNotUpdatedForRetransmits) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();
  header_extensions_.RegisterByUri(kTransportSequenceNumberExtensionId,
                                   TransportSequenceNumber::Uri());

  const uint16_t kTransportSequenceNumber = 1;
  EXPECT_CALL(send_packet_observer_, OnSendPacket).Times(0);
  std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
  packet->set_transport_sequence_number(kTransportSequenceNumber);
  packet->set_packet_type(RtpPacketMediaType::kRetransmission);
  packet->set_retransmitted_sequence_number(packet->SequenceNumber());
  packet->set_original_ssrc(packet->Ssrc());
  sender->SendPacket(std::move(packet), PacedPacketInfo());
}

TEST_F(RtpSenderEgressTest, ReportsFecRate) {
  constexpr int kNumPackets = 10;
  constexpr TimeDelta kTimeBetweenPackets = TimeDelta::Millis(33);

  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();
  DataSize total_fec_data_sent = DataSize::Zero();
  // Send some packets, alternating between media and FEC.
  for (size_t i = 0; i < kNumPackets; ++i) {
    std::unique_ptr<RtpPacketToSend> media_packet = BuildRtpPacket();
    media_packet->set_packet_type(RtpPacketMediaType::kVideo);
    media_packet->SetPayloadSize(500);
    sender->SendPacket(std::move(media_packet), PacedPacketInfo());

    std::unique_ptr<RtpPacketToSend> fec_packet = BuildRtpPacket();
    fec_packet->set_packet_type(RtpPacketMediaType::kForwardErrorCorrection);
    fec_packet->SetPayloadSize(123);
    auto fec_packet_size = fec_packet->size();
    sender->SendPacket(std::move(fec_packet), PacedPacketInfo());
    total_fec_data_sent += DataSize::Bytes(fec_packet_size);

    time_controller_.AdvanceTime(kTimeBetweenPackets);
  }

  EXPECT_NEAR(
      (sender->GetSendRates(
           time_controller_.GetClock()
               ->CurrentTime())[RtpPacketMediaType::kForwardErrorCorrection])
          .bps(),
      (total_fec_data_sent / (kTimeBetweenPackets * kNumPackets)).bps(), 500);
}

TEST_F(RtpSenderEgressTest, BitrateCallbacks) {
  class MockBitrateStaticsObserver : public BitrateStatisticsObserver {
   public:
    MOCK_METHOD(void, Notify, (uint32_t, uint32_t, uint32_t), (override));
  } observer;

  RtpRtcpInterface::Configuration config = DefaultConfig();
  config.send_bitrate_observer = &observer;
  auto sender = std::make_unique<RtpSenderEgress>(config, &packet_history_);

  // Simulate kNumPackets sent with kPacketInterval intervals, with the
  // number of packets selected so that we fill (but don't overflow) the one
  // second averaging window.
  const TimeDelta kWindowSize = TimeDelta::Seconds(1);
  const TimeDelta kPacketInterval = TimeDelta::Millis(20);
  const int kNumPackets = (kWindowSize - kPacketInterval) / kPacketInterval;

  DataSize total_data_sent = DataSize::Zero();

  // Send all but on of the packets, expect a call for each packet but don't
  // verify bitrate yet (noisy measurements in the beginning).
  for (int i = 0; i < kNumPackets; ++i) {
    std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
    packet->SetPayloadSize(500);
    // Mark all packets as retransmissions - will cause total and retransmission
    // rates to be equal.
    packet->set_packet_type(RtpPacketMediaType::kRetransmission);
    packet->set_retransmitted_sequence_number(packet->SequenceNumber());
    total_data_sent += DataSize::Bytes(packet->size());

    EXPECT_CALL(observer, Notify(_, _, kSsrc))
        .WillOnce([&](uint32_t total_bitrate_bps,
                      uint32_t retransmission_bitrate_bps, uint32_t /*ssrc*/) {
          TimeDelta window_size = i * kPacketInterval + TimeDelta::Millis(1);
          // If there is just a single data point, there is no well defined
          // averaging window so a bitrate of zero will be reported.
          const double expected_bitrate_bps =
              i == 0 ? 0.0 : (total_data_sent / window_size).bps();
          EXPECT_NEAR(total_bitrate_bps, expected_bitrate_bps, 500);
          EXPECT_NEAR(retransmission_bitrate_bps, expected_bitrate_bps, 500);
        });

    sender->SendPacket(std::move(packet), PacedPacketInfo());
    time_controller_.AdvanceTime(kPacketInterval);
  }
}

TEST_F(RtpSenderEgressTest, DoesNotPutNotRetransmittablePacketsInHistory) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();
  packet_history_.SetStorePacketsStatus(
      RtpPacketHistory::StorageMode::kStoreAndCull, 10);

  std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
  packet->set_allow_retransmission(false);
  auto packet_sequence_number = packet->SequenceNumber();
  sender->SendPacket(std::move(packet), PacedPacketInfo());
  EXPECT_FALSE(packet_history_.GetPacketState(packet_sequence_number));
}

TEST_F(RtpSenderEgressTest, PutsRetransmittablePacketsInHistory) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();
  packet_history_.SetStorePacketsStatus(
      RtpPacketHistory::StorageMode::kStoreAndCull, 10);

  std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
  packet->set_allow_retransmission(true);
  auto packet_sequence_number = packet->SequenceNumber();
  sender->SendPacket(std::move(packet), PacedPacketInfo());
  EXPECT_TRUE(packet_history_.GetPacketState(packet_sequence_number));
}

TEST_F(RtpSenderEgressTest, DoesNotPutNonMediaInHistory) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();
  packet_history_.SetStorePacketsStatus(
      RtpPacketHistory::StorageMode::kStoreAndCull, 10);

  // Non-media packets, even when marked as retransmittable, are not put into
  // the packet history.
  std::unique_ptr<RtpPacketToSend> retransmission = BuildRtpPacket();
  retransmission->set_allow_retransmission(true);
  retransmission->set_packet_type(RtpPacketMediaType::kRetransmission);
  retransmission->set_retransmitted_sequence_number(
      retransmission->SequenceNumber());
  auto retransmission_sequence_number = retransmission->SequenceNumber();
  sender->SendPacket(std::move(retransmission), PacedPacketInfo());
  EXPECT_FALSE(packet_history_.GetPacketState(retransmission_sequence_number));

  std::unique_ptr<RtpPacketToSend> fec = BuildRtpPacket();
  fec->set_allow_retransmission(true);
  fec->set_packet_type(RtpPacketMediaType::kForwardErrorCorrection);
  auto fec_sequence_number = fec->SequenceNumber();
  sender->SendPacket(std::move(fec), PacedPacketInfo());
  EXPECT_FALSE(packet_history_.GetPacketState(fec_sequence_number));

  std::unique_ptr<RtpPacketToSend> padding = BuildRtpPacket();
  padding->set_allow_retransmission(true);
  padding->set_packet_type(RtpPacketMediaType::kPadding);
  auto padding_sequence_number = padding->SequenceNumber();
  sender->SendPacket(std::move(padding), PacedPacketInfo());
  EXPECT_FALSE(packet_history_.GetPacketState(padding_sequence_number));
}

TEST_F(RtpSenderEgressTest, UpdatesSendStatusOfRetransmittedPackets) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();
  packet_history_.SetStorePacketsStatus(
      RtpPacketHistory::StorageMode::kStoreAndCull, 10);

  // Send a packet, putting it in the history.
  std::unique_ptr<RtpPacketToSend> media_packet = BuildRtpPacket();
  media_packet->set_allow_retransmission(true);
  auto media_packet_sequence_number = media_packet->SequenceNumber();
  sender->SendPacket(std::move(media_packet), PacedPacketInfo());
  EXPECT_TRUE(packet_history_.GetPacketState(media_packet_sequence_number));

  // Simulate a retransmission, marking the packet as pending.
  std::unique_ptr<RtpPacketToSend> retransmission =
      packet_history_.GetPacketAndMarkAsPending(media_packet_sequence_number);
  retransmission->set_retransmitted_sequence_number(
      media_packet_sequence_number);
  retransmission->set_packet_type(RtpPacketMediaType::kRetransmission);
  EXPECT_TRUE(packet_history_.GetPacketState(media_packet_sequence_number));

  // Simulate packet leaving pacer, the packet should be marked as non-pending.
  sender->SendPacket(std::move(retransmission), PacedPacketInfo());
  EXPECT_TRUE(packet_history_.GetPacketState(media_packet_sequence_number));
}

TEST_F(RtpSenderEgressTest, StreamDataCountersCallbacks) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();

  const RtpPacketCounter kEmptyCounter;
  RtpPacketCounter expected_transmitted_counter;
  RtpPacketCounter expected_retransmission_counter;

  // Send a media packet.
  std::unique_ptr<RtpPacketToSend> media_packet = BuildRtpPacket();
  media_packet->SetPayloadSize(6);
  media_packet->SetSequenceNumber(kStartSequenceNumber);
  media_packet->set_time_in_send_queue(TimeDelta::Millis(10));
  expected_transmitted_counter.packets += 1;
  expected_transmitted_counter.payload_bytes += media_packet->payload_size();
  expected_transmitted_counter.header_bytes += media_packet->headers_size();
  expected_transmitted_counter.total_packet_delay += TimeDelta::Millis(10);

  EXPECT_CALL(
      mock_rtp_stats_callback_,
      DataCountersUpdated(AllOf(Field(&StreamDataCounters::transmitted,
                                      expected_transmitted_counter),
                                Field(&StreamDataCounters::retransmitted,
                                      expected_retransmission_counter),
                                Field(&StreamDataCounters::fec, kEmptyCounter)),
                          kSsrc));
  sender->SendPacket(std::move(media_packet), PacedPacketInfo());
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // Send a retransmission. Retransmissions are counted into both transmitted
  // and retransmitted packet statistics.
  std::unique_ptr<RtpPacketToSend> retransmission_packet = BuildRtpPacket();
  retransmission_packet->set_packet_type(RtpPacketMediaType::kRetransmission);
  retransmission_packet->SetSequenceNumber(kStartSequenceNumber);
  retransmission_packet->set_retransmitted_sequence_number(
      kStartSequenceNumber);
  retransmission_packet->set_time_in_send_queue(TimeDelta::Millis(20));
  expected_transmitted_counter.packets += 1;
  expected_transmitted_counter.payload_bytes +=
      retransmission_packet->payload_size();
  expected_transmitted_counter.header_bytes +=
      retransmission_packet->headers_size();
  expected_transmitted_counter.total_packet_delay += TimeDelta::Millis(20);

  expected_retransmission_counter.packets += 1;
  expected_retransmission_counter.payload_bytes +=
      retransmission_packet->payload_size();
  expected_retransmission_counter.header_bytes +=
      retransmission_packet->headers_size();
  expected_retransmission_counter.total_packet_delay += TimeDelta::Millis(20);

  EXPECT_CALL(
      mock_rtp_stats_callback_,
      DataCountersUpdated(AllOf(Field(&StreamDataCounters::transmitted,
                                      expected_transmitted_counter),
                                Field(&StreamDataCounters::retransmitted,
                                      expected_retransmission_counter),
                                Field(&StreamDataCounters::fec, kEmptyCounter)),
                          kSsrc));
  sender->SendPacket(std::move(retransmission_packet), PacedPacketInfo());
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // Send a padding packet.
  std::unique_ptr<RtpPacketToSend> padding_packet = BuildRtpPacket();
  padding_packet->set_packet_type(RtpPacketMediaType::kPadding);
  padding_packet->SetPadding(224);
  padding_packet->SetSequenceNumber(kStartSequenceNumber + 1);
  padding_packet->set_time_in_send_queue(TimeDelta::Millis(30));
  expected_transmitted_counter.packets += 1;
  expected_transmitted_counter.padding_bytes += padding_packet->padding_size();
  expected_transmitted_counter.header_bytes += padding_packet->headers_size();
  expected_transmitted_counter.total_packet_delay += TimeDelta::Millis(30);

  EXPECT_CALL(
      mock_rtp_stats_callback_,
      DataCountersUpdated(AllOf(Field(&StreamDataCounters::transmitted,
                                      expected_transmitted_counter),
                                Field(&StreamDataCounters::retransmitted,
                                      expected_retransmission_counter),
                                Field(&StreamDataCounters::fec, kEmptyCounter)),
                          kSsrc));
  sender->SendPacket(std::move(padding_packet), PacedPacketInfo());
  time_controller_.AdvanceTime(TimeDelta::Zero());
}

TEST_F(RtpSenderEgressTest, StreamDataCountersCallbacksFec) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();

  const RtpPacketCounter kEmptyCounter;
  RtpPacketCounter expected_transmitted_counter;
  RtpPacketCounter expected_fec_counter;

  // Send a media packet.
  std::unique_ptr<RtpPacketToSend> media_packet = BuildRtpPacket();
  media_packet->SetPayloadSize(6);
  expected_transmitted_counter.packets += 1;
  expected_transmitted_counter.payload_bytes += media_packet->payload_size();
  expected_transmitted_counter.header_bytes += media_packet->headers_size();

  EXPECT_CALL(
      mock_rtp_stats_callback_,
      DataCountersUpdated(
          AllOf(Field(&StreamDataCounters::transmitted,
                      expected_transmitted_counter),
                Field(&StreamDataCounters::retransmitted, kEmptyCounter),
                Field(&StreamDataCounters::fec, expected_fec_counter)),
          kSsrc));
  sender->SendPacket(std::move(media_packet), PacedPacketInfo());
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // Send and FEC packet. FEC is counted into both transmitted and FEC packet
  // statistics.
  std::unique_ptr<RtpPacketToSend> fec_packet = BuildRtpPacket();
  fec_packet->set_packet_type(RtpPacketMediaType::kForwardErrorCorrection);
  fec_packet->SetPayloadSize(6);
  expected_transmitted_counter.packets += 1;
  expected_transmitted_counter.payload_bytes += fec_packet->payload_size();
  expected_transmitted_counter.header_bytes += fec_packet->headers_size();

  expected_fec_counter.packets += 1;
  expected_fec_counter.payload_bytes += fec_packet->payload_size();
  expected_fec_counter.header_bytes += fec_packet->headers_size();

  EXPECT_CALL(
      mock_rtp_stats_callback_,
      DataCountersUpdated(
          AllOf(Field(&StreamDataCounters::transmitted,
                      expected_transmitted_counter),
                Field(&StreamDataCounters::retransmitted, kEmptyCounter),
                Field(&StreamDataCounters::fec, expected_fec_counter)),
          kSsrc));
  sender->SendPacket(std::move(fec_packet), PacedPacketInfo());
  time_controller_.AdvanceTime(TimeDelta::Zero());
}

TEST_F(RtpSenderEgressTest, UpdatesDataCounters) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();

  const RtpPacketCounter kEmptyCounter;

  // Send a media packet.
  std::unique_ptr<RtpPacketToSend> media_packet = BuildRtpPacket();
  media_packet->SetPayloadSize(6);
  auto media_packet_sequence_number = media_packet->SequenceNumber();
  auto media_packet_payload_size = media_packet->payload_size();
  auto media_packet_padding_size = media_packet->padding_size();
  auto media_packet_headers_size = media_packet->headers_size();
  sender->SendPacket(std::move(media_packet), PacedPacketInfo());
  time_controller_.AdvanceTime(TimeDelta::Zero());

  // Send an RTX retransmission packet.
  std::unique_ptr<RtpPacketToSend> rtx_packet = BuildRtpPacket();
  rtx_packet->set_packet_type(RtpPacketMediaType::kRetransmission);
  rtx_packet->SetSsrc(kRtxSsrc);
  rtx_packet->SetPayloadSize(7);
  rtx_packet->set_retransmitted_sequence_number(media_packet_sequence_number);
  auto rtx_packet_payload_size = rtx_packet->payload_size();
  auto rtx_packet_padding_size = rtx_packet->padding_size();
  auto rtx_packet_headers_size = rtx_packet->headers_size();
  sender->SendPacket(std::move(rtx_packet), PacedPacketInfo());
  time_controller_.AdvanceTime(TimeDelta::Zero());

  StreamDataCounters rtp_stats;
  StreamDataCounters rtx_stats;
  sender->GetDataCounters(&rtp_stats, &rtx_stats);

  EXPECT_EQ(rtp_stats.transmitted.packets, 1u);
  EXPECT_EQ(rtp_stats.transmitted.payload_bytes, media_packet_payload_size);
  EXPECT_EQ(rtp_stats.transmitted.padding_bytes, media_packet_padding_size);
  EXPECT_EQ(rtp_stats.transmitted.header_bytes, media_packet_headers_size);
  EXPECT_EQ(rtp_stats.retransmitted, kEmptyCounter);
  EXPECT_EQ(rtp_stats.fec, kEmptyCounter);

  // Retransmissions are counted both into transmitted and retransmitted
  // packet counts.
  EXPECT_EQ(rtx_stats.transmitted.packets, 1u);
  EXPECT_EQ(rtx_stats.transmitted.payload_bytes, rtx_packet_payload_size);
  EXPECT_EQ(rtx_stats.transmitted.padding_bytes, rtx_packet_padding_size);
  EXPECT_EQ(rtx_stats.transmitted.header_bytes, rtx_packet_headers_size);
  EXPECT_EQ(rtx_stats.retransmitted, rtx_stats.transmitted);
  EXPECT_EQ(rtx_stats.fec, kEmptyCounter);
}

TEST_F(RtpSenderEgressTest, SendPacketUpdatesExtensions) {
  header_extensions_.RegisterByUri(kVideoTimingExtensionId,
                                   VideoTimingExtension::Uri());
  header_extensions_.RegisterByUri(kAbsoluteSendTimeExtensionId,
                                   AbsoluteSendTime::Uri());
  header_extensions_.RegisterByUri(kTransmissionOffsetExtensionId,
                                   TransmissionOffset::Uri());
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();

  std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
  packet->set_packetization_finish_time(clock_->CurrentTime());

  const int32_t kDiffMs = 10;
  time_controller_.AdvanceTime(TimeDelta::Millis(kDiffMs));

  sender->SendPacket(std::move(packet), PacedPacketInfo());

  RtpPacketReceived received_packet = transport_.last_packet()->packet;

  EXPECT_EQ(received_packet.GetExtension<TransmissionOffset>(), kDiffMs * 90);

  EXPECT_EQ(received_packet.GetExtension<AbsoluteSendTime>(),
            AbsoluteSendTime::To24Bits(clock_->CurrentTime()));

  VideoSendTiming timing;
  EXPECT_TRUE(received_packet.GetExtension<VideoTimingExtension>(&timing));
  EXPECT_EQ(timing.pacer_exit_delta_ms, kDiffMs);
}

TEST_F(RtpSenderEgressTest, SendPacketSetsPacketOptions) {
  const uint16_t kPacketId = 42;
  const uint16_t kSequenceNumber = 456;
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();
  header_extensions_.RegisterByUri(kTransportSequenceNumberExtensionId,
                                   TransportSequenceNumber::Uri());

  std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
  uint32_t ssrc = packet->Ssrc();
  packet->SetSequenceNumber(kSequenceNumber);
  packet->set_transport_sequence_number(kPacketId);
  EXPECT_CALL(send_packet_observer_, OnSendPacket);
  sender->SendPacket(std::move(packet), PacedPacketInfo());

  PacketOptions packet_options = transport_.last_packet()->options;

  EXPECT_EQ(packet_options.packet_id, kPacketId);
  EXPECT_TRUE(packet_options.included_in_allocation);
  EXPECT_TRUE(packet_options.included_in_feedback);
  EXPECT_FALSE(packet_options.is_retransmit);

  // Send another packet as retransmission, verify options are populated.
  std::unique_ptr<RtpPacketToSend> retransmission = BuildRtpPacket();
  retransmission->set_transport_sequence_number(kPacketId + 1);
  retransmission->set_packet_type(RtpPacketMediaType::kRetransmission);
  retransmission->set_retransmitted_sequence_number(kSequenceNumber);
  retransmission->set_original_ssrc(ssrc);
  sender->SendPacket(std::move(retransmission), PacedPacketInfo());
  EXPECT_TRUE(transport_.last_packet()->options.is_retransmit);
}

TEST_F(RtpSenderEgressTest, SendPacketSetsPacketOptionsIdFromExtension) {
  header_extensions_.RegisterByUri(kTransportSequenceNumberExtensionId,
                                   TransportSequenceNumber::Uri());
  RtpSenderEgress sender(DefaultConfig(), &packet_history_);

  // 64-bit transport sequence number.
  const int64_t kTransportSequenceNumber = 0xFFFF000F;
  std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
  packet->set_transport_sequence_number(kTransportSequenceNumber);

  EXPECT_CALL(send_packet_observer_, OnSendPacket);
  sender.SendPacket(std::move(packet), PacedPacketInfo());

  ASSERT_TRUE(transport_.last_packet().has_value());
  EXPECT_EQ(
      transport_.last_packet()->packet.GetExtension<TransportSequenceNumber>(),
      kTransportSequenceNumber & 0xFFFF);
  PacketOptions packet_options = transport_.last_packet()->options;
  // 16 bit packet id.
  EXPECT_EQ(packet_options.packet_id, kTransportSequenceNumber & 0xFFFF);
}

TEST_F(RtpSenderEgressTest,
       SendPacketSetsPacketOptionsIdFromRtpSendPacketIfNotUsingExtension) {
  RtpSenderEgress sender(DefaultConfig(), &packet_history_);
  // 64-bit transport sequence number.
  const int64_t kTransportSequenceNumber = 0xFFFF000F;
  std::unique_ptr<RtpPacketToSend> packet = BuildRtpPacket();
  packet->set_transport_sequence_number(kTransportSequenceNumber);

  EXPECT_CALL(send_packet_observer_, OnSendPacket);
  sender.SendPacket(std::move(packet), PacedPacketInfo());

  ASSERT_TRUE(transport_.last_packet().has_value());
  ASSERT_FALSE(
      transport_.last_packet()->packet.HasExtension<TransportSequenceNumber>());
  PacketOptions packet_options = transport_.last_packet()->options;
  EXPECT_EQ(packet_options.packet_id, kTransportSequenceNumber);
}

TEST_F(RtpSenderEgressTest, SendPacketUpdatesStats) {
  const size_t kPayloadSize = 1000;

  const rtc::ArrayView<const RtpExtensionSize> kNoRtpHeaderExtensionSizes;
  FlexfecSender flexfec(kFlexfectPayloadType, kFlexFecSsrc, kSsrc, /*mid=*/"",
                        /*header_extensions=*/{}, kNoRtpHeaderExtensionSizes,
                        /*rtp_state=*/nullptr, time_controller_.GetClock());
  RtpRtcpInterface::Configuration config = DefaultConfig();
  config.fec_generator = &flexfec;
  auto sender = std::make_unique<RtpSenderEgress>(config, &packet_history_);

  header_extensions_.RegisterByUri(kTransportSequenceNumberExtensionId,
                                   TransportSequenceNumber::Uri());

  const Timestamp capture_time = clock_->CurrentTime();

  std::unique_ptr<RtpPacketToSend> video_packet = BuildRtpPacket();
  video_packet->set_packet_type(RtpPacketMediaType::kVideo);
  video_packet->SetPayloadSize(kPayloadSize);
  video_packet->set_transport_sequence_number(1);

  std::unique_ptr<RtpPacketToSend> rtx_packet = BuildRtpPacket();
  rtx_packet->SetSsrc(kRtxSsrc);
  rtx_packet->set_packet_type(RtpPacketMediaType::kRetransmission);
  rtx_packet->set_original_ssrc(video_packet->Ssrc());
  rtx_packet->set_retransmitted_sequence_number(video_packet->SequenceNumber());
  rtx_packet->SetPayloadSize(kPayloadSize);
  rtx_packet->set_transport_sequence_number(2);

  std::unique_ptr<RtpPacketToSend> fec_packet = BuildRtpPacket();
  fec_packet->SetSsrc(kFlexFecSsrc);
  fec_packet->set_packet_type(RtpPacketMediaType::kForwardErrorCorrection);
  fec_packet->SetPayloadSize(kPayloadSize);
  fec_packet->set_transport_sequence_number(3);

  const int64_t kDiffMs = 25;
  time_controller_.AdvanceTime(TimeDelta::Millis(kDiffMs));

  EXPECT_CALL(send_packet_observer_, OnSendPacket(Eq(1), capture_time, kSsrc));

  sender->SendPacket(std::move(video_packet), PacedPacketInfo());

  // Send packet observer not called for padding/retransmissions.
  EXPECT_CALL(send_packet_observer_, OnSendPacket(Eq(2), _, _)).Times(0);
  sender->SendPacket(std::move(rtx_packet), PacedPacketInfo());

  EXPECT_CALL(send_packet_observer_,
              OnSendPacket(Eq(3), capture_time, kFlexFecSsrc));
  sender->SendPacket(std::move(fec_packet), PacedPacketInfo());

  time_controller_.AdvanceTime(TimeDelta::Zero());
  StreamDataCounters rtp_stats;
  StreamDataCounters rtx_stats;
  sender->GetDataCounters(&rtp_stats, &rtx_stats);
  EXPECT_EQ(rtp_stats.transmitted.packets, 2u);
  EXPECT_EQ(rtp_stats.fec.packets, 1u);
  EXPECT_EQ(rtx_stats.retransmitted.packets, 1u);
}

TEST_F(RtpSenderEgressTest, SupportsAbortingRetransmissions) {
  std::unique_ptr<RtpSenderEgress> sender = CreateRtpSenderEgress();
  packet_history_.SetStorePacketsStatus(
      RtpPacketHistory::StorageMode::kStoreAndCull, 10);

  // Create a packet and send it so it is put in the history.
  std::unique_ptr<RtpPacketToSend> media_packet = BuildRtpPacket();
  media_packet->set_packet_type(RtpPacketMediaType::kVideo);
  media_packet->set_allow_retransmission(true);
  const uint16_t media_sequence_number = media_packet->SequenceNumber();
  sender->SendPacket(std::move(media_packet), PacedPacketInfo());

  // Fetch a retranmission packet from the history, this should mark the
  // media packets as pending so it is not available to grab again.
  std::unique_ptr<RtpPacketToSend> retransmission_packet =
      packet_history_.GetPacketAndMarkAsPending(media_sequence_number);
  ASSERT_TRUE(retransmission_packet);
  EXPECT_FALSE(
      packet_history_.GetPacketAndMarkAsPending(media_sequence_number));

  // Mark retransmission as aborted, fetching packet is possible again.
  retransmission_packet.reset();
  uint16_t kAbortedSequenceNumbers[] = {media_sequence_number};
  sender->OnAbortedRetransmissions(kAbortedSequenceNumbers);
  EXPECT_TRUE(packet_history_.GetPacketAndMarkAsPending(media_sequence_number));
}

}  // namespace webrtc
