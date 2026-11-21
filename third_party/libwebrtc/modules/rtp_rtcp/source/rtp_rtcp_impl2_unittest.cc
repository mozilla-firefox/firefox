/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/rtp_rtcp_impl2.h"

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include "absl/types/optional.h"
#include "api/field_trials_registry.h"
#include "api/units/time_delta.h"
#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtcp_packet.h"
#include "modules/rtp_rtcp/source/rtcp_packet/nack.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_interface.h"
#include "modules/rtp_rtcp/source/rtp_sender_video.h"
#include "modules/rtp_rtcp/source/rtp_sequence_number_map.h"
#include "rtc_base/logging.h"
#include "rtc_base/rate_limiter.h"
#include "rtc_base/strings/string_builder.h"
#include "test/explicit_key_value_config.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/rtcp_packet_parser.h"
#include "test/run_loop.h"
#include "test/time_controller/simulated_time_controller.h"

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::Not;
using ::testing::Optional;
using ::testing::SizeIs;

using webrtc::test::ExplicitKeyValueConfig;

namespace webrtc {
namespace {
constexpr uint32_t kSenderSsrc = 0x12345;
constexpr uint32_t kReceiverSsrc = 0x23456;
constexpr uint32_t kRtxSenderSsrc = 0x12346;
constexpr TimeDelta kOneWayNetworkDelay = TimeDelta::Millis(100);
constexpr uint8_t kBaseLayerTid = 0;
constexpr uint8_t kHigherLayerTid = 1;
constexpr uint16_t kSequenceNumber = 100;
constexpr uint8_t kPayloadType = 100;
constexpr uint8_t kRtxPayloadType = 98;
constexpr int kWidth = 320;
constexpr int kHeight = 100;
constexpr int kCaptureTimeMsToRtpTimestamp = 90;  // 90 kHz clock.
constexpr TimeDelta kDefaultReportInterval = TimeDelta::Millis(1000);

// RTP header extension ids.
enum : int {
  kAbsoluteSendTimeExtensionId = 1,
  kTransportSequenceNumberExtensionId,
  kTransmissionOffsetExtensionId,
};

MATCHER_P2(Near, value, margin, "") {
  return value - margin <= arg && arg <= value + margin;
}

class RtcpRttStatsTestImpl : public RtcpRttStats {
 public:
  RtcpRttStatsTestImpl() : rtt_ms_(0) {}
  ~RtcpRttStatsTestImpl() override = default;

  void OnRttUpdate(int64_t rtt_ms) override { rtt_ms_ = rtt_ms; }
  int64_t LastProcessedRtt() const override { return rtt_ms_; }
  int64_t rtt_ms_;
};

// TODO(bugs.webrtc.org/11581): remove inheritance once the ModuleRtpRtcpImpl2
// Module/ProcessThread dependency is gone.
class SendTransport : public Transport,
                      public sim_time_impl::SimulatedSequenceRunner {
 public:
  SendTransport(TimeDelta delay, GlobalSimulatedTimeController* time_controller)
      : receiver_(nullptr),
        time_controller_(time_controller),
        delay_(delay),
        rtp_packets_sent_(0),
        rtcp_packets_sent_(0),
        last_packet_(&header_extensions_) {
    time_controller_->Register(this);
  }

  ~SendTransport() { time_controller_->Unregister(this); }

  void SetRtpRtcpModule(ModuleRtpRtcpImpl2* receiver) { receiver_ = receiver; }
  void SimulateNetworkDelay(TimeDelta delay) { delay_ = delay; }
  bool SendRtp(rtc::ArrayView<const uint8_t> data,
               const PacketOptions& options) override {
    EXPECT_TRUE(last_packet_.Parse(data));
    ++rtp_packets_sent_;
    return true;
  }
  bool SendRtcp(rtc::ArrayView<const uint8_t> data) override {
    test::RtcpPacketParser parser;
    parser.Parse(data);
    last_nack_list_ = parser.nack()->packet_ids();
    Timestamp current_time = time_controller_->GetClock()->CurrentTime();
    Timestamp delivery_time = current_time + delay_;
    rtcp_packets_.push_back(
        Packet{delivery_time, std::vector<uint8_t>(data.begin(), data.end())});
    ++rtcp_packets_sent_;
    RunReady(current_time);
    return true;
  }

  // sim_time_impl::SimulatedSequenceRunner
  Timestamp GetNextRunTime() const override {
    if (!rtcp_packets_.empty())
      return rtcp_packets_.front().send_time;
    return Timestamp::PlusInfinity();
  }
  void RunReady(Timestamp at_time) override {
    while (!rtcp_packets_.empty() &&
           rtcp_packets_.front().send_time <= at_time) {
      Packet packet = std::move(rtcp_packets_.front());
      rtcp_packets_.pop_front();
      EXPECT_TRUE(receiver_);
      receiver_->IncomingRtcpPacket(packet.data);
    }
  }
  TaskQueueBase* GetAsTaskQueue() override {
    return reinterpret_cast<TaskQueueBase*>(this);
  }

  size_t NumRtcpSent() { return rtcp_packets_sent_; }

  ModuleRtpRtcpImpl2* receiver_;
  GlobalSimulatedTimeController* const time_controller_;
  TimeDelta delay_;
  int rtp_packets_sent_;
  size_t rtcp_packets_sent_;
  std::vector<uint16_t> last_nack_list_;
  RtpHeaderExtensionMap header_extensions_;
  RtpPacketReceived last_packet_;
  struct Packet {
    Timestamp send_time;
    std::vector<uint8_t> data;
  };
  std::deque<Packet> rtcp_packets_;
};

class RtpRtcpModule : public RtcpPacketTypeCounterObserver,
                      public SendPacketObserver {
 public:
  struct SentPacket {
    SentPacket(uint16_t packet_id, Timestamp capture_time, uint32_t ssrc)
        : packet_id(packet_id), capture_time(capture_time), ssrc(ssrc) {}
    uint16_t packet_id;
    Timestamp capture_time;
    uint32_t ssrc;
  };

  RtpRtcpModule(GlobalSimulatedTimeController* time_controller,
                bool is_sender,
                const FieldTrialsRegistry& trials)
      : time_controller_(time_controller),
        is_sender_(is_sender),
        trials_(trials),
        receive_statistics_(
            ReceiveStatistics::Create(time_controller->GetClock())),
        transport_(kOneWayNetworkDelay, time_controller) {
    CreateModuleImpl();
  }

  TimeController* const time_controller_;
  const bool is_sender_;
  const FieldTrialsRegistry& trials_;
  RtcpPacketTypeCounter packets_sent_;
  RtcpPacketTypeCounter packets_received_;
  std::unique_ptr<ReceiveStatistics> receive_statistics_;
  SendTransport transport_;
  RtcpRttStatsTestImpl rtt_stats_;
  std::unique_ptr<ModuleRtpRtcpImpl2> impl_;

  void RtcpPacketTypesCounterUpdated(
      uint32_t ssrc,
      const RtcpPacketTypeCounter& packet_counter) override {
    counter_map_[ssrc] = packet_counter;
  }

  void OnSendPacket(absl::optional<uint16_t> packet_id,
                    Timestamp capture_time,
                    uint32_t ssrc) override {
    if (packet_id.has_value()) {
      last_sent_packet_.emplace(*packet_id, capture_time, ssrc);
    }
  }

  absl::optional<SentPacket> last_sent_packet() const {
    return last_sent_packet_;
  }

  RtcpPacketTypeCounter RtcpSent() {
    // RTCP counters for remote SSRC.
    return counter_map_[is_sender_ ? kReceiverSsrc : kSenderSsrc];
  }

  RtcpPacketTypeCounter RtcpReceived() {
    // Received RTCP stats for (own) local SSRC.
    return counter_map_[impl_->SSRC()];
  }
  int RtpSent() { return transport_.rtp_packets_sent_; }
  uint16_t LastRtpSequenceNumber() { return last_packet().SequenceNumber(); }
  std::vector<uint16_t> LastNackListSent() {
    return transport_.last_nack_list_;
  }
  void SetRtcpReportIntervalAndReset(TimeDelta rtcp_report_interval) {
    rtcp_report_interval_ = rtcp_report_interval;
    CreateModuleImpl();
  }
  const RtpPacketReceived& last_packet() { return transport_.last_packet_; }
  void RegisterHeaderExtension(absl::string_view uri, int id) {
    impl_->RegisterRtpHeaderExtension(uri, id);
    transport_.header_extensions_.RegisterByUri(id, uri);
    transport_.last_packet_.IdentifyExtensions(transport_.header_extensions_);
  }
  void ReinintWithFec(VideoFecGenerator* fec_generator) {
    fec_generator_ = fec_generator;
    CreateModuleImpl();
  }

  void CreateModuleImpl() {
    RtpRtcpInterface::Configuration config;
    config.audio = false;
    config.clock = time_controller_->GetClock();
    config.outgoing_transport = &transport_;
    config.receive_statistics = receive_statistics_.get();
    config.rtcp_packet_type_counter_observer = this;
    config.rtt_stats = &rtt_stats_;
    config.rtcp_report_interval_ms = rtcp_report_interval_.ms();
    config.local_media_ssrc = is_sender_ ? kSenderSsrc : kReceiverSsrc;
    config.rtx_send_ssrc =
        is_sender_ ? absl::make_optional(kRtxSenderSsrc) : absl::nullopt;
    config.need_rtp_packet_infos = true;
    config.non_sender_rtt_measurement = true;
    config.field_trials = &trials_;
    config.send_packet_observer = this;
    config.fec_generator = fec_generator_;
    impl_.reset(new ModuleRtpRtcpImpl2(config));
    impl_->SetRemoteSSRC(is_sender_ ? kReceiverSsrc : kSenderSsrc);
    impl_->SetRTCPStatus(RtcpMode::kCompound);
  }

 private:
  std::map<uint32_t, RtcpPacketTypeCounter> counter_map_;
  absl::optional<SentPacket> last_sent_packet_;
  VideoFecGenerator* fec_generator_ = nullptr;
  TimeDelta rtcp_report_interval_ = kDefaultReportInterval;
};
}  // namespace

class RtpRtcpImpl2Test : public ::testing::Test {
 protected:
  RtpRtcpImpl2Test()
      : time_controller_(Timestamp::Micros(133590000000000)),
        field_trials_(""),
        sender_(&time_controller_,
                /*is_sender=*/true,
                field_trials_),
        receiver_(&time_controller_,
                  /*is_sender=*/false,
                  field_trials_) {}

  void SetUp() override {
    // Send module.
    EXPECT_EQ(0, sender_.impl_->SetSendingStatus(true));
    sender_.impl_->SetSendingMediaStatus(true);
    sender_.impl_->SetSequenceNumber(kSequenceNumber);
    sender_.impl_->SetStorePacketsStatus(true, 100);

    RTPSenderVideo::Config video_config;
    video_config.clock = time_controller_.GetClock();
    video_config.rtp_sender = sender_.impl_->RtpSender();
    video_config.field_trials = &field_trials_;
    sender_video_ = std::make_unique<RTPSenderVideo>(video_config);

    // Receive module.
    EXPECT_EQ(0, receiver_.impl_->SetSendingStatus(false));
    receiver_.impl_->SetSendingMediaStatus(false);
    // Transport settings.
    sender_.transport_.SetRtpRtcpModule(receiver_.impl_.get());
    receiver_.transport_.SetRtpRtcpModule(sender_.impl_.get());
  }

  void AdvanceTime(TimeDelta duration) {
    time_controller_.AdvanceTime(duration);
  }

  void ReinitWithFec(VideoFecGenerator* fec_generator,
                     absl::optional<int> red_payload_type) {
    sender_.ReinintWithFec(fec_generator);
    EXPECT_EQ(0, sender_.impl_->SetSendingStatus(true));
    sender_.impl_->SetSendingMediaStatus(true);
    sender_.impl_->SetSequenceNumber(kSequenceNumber);
    sender_.impl_->SetStorePacketsStatus(true, 100);
    receiver_.transport_.SetRtpRtcpModule(sender_.impl_.get());

    RTPSenderVideo::Config video_config;
    video_config.clock = time_controller_.GetClock();
    video_config.rtp_sender = sender_.impl_->RtpSender();
    video_config.field_trials = &field_trials_;
    video_config.fec_overhead_bytes = fec_generator->MaxPacketOverhead();
    video_config.fec_type = fec_generator->GetFecType();
    video_config.red_payload_type = red_payload_type;
    sender_video_ = std::make_unique<RTPSenderVideo>(video_config);
  }

  GlobalSimulatedTimeController time_controller_;
  test::ExplicitKeyValueConfig field_trials_;
  RtpRtcpModule sender_;
  std::unique_ptr<RTPSenderVideo> sender_video_;
  RtpRtcpModule receiver_;

  bool SendFrame(const RtpRtcpModule* module,
                 RTPSenderVideo* sender,
                 uint8_t tid) {
    int64_t now_ms = time_controller_.GetClock()->TimeInMilliseconds();
    return SendFrame(
        module, sender, tid,
        static_cast<uint32_t>(now_ms * kCaptureTimeMsToRtpTimestamp), now_ms);
  }

  bool SendFrame(const RtpRtcpModule* module,
                 RTPSenderVideo* sender,
                 uint8_t tid,
                 uint32_t rtp_timestamp,
                 int64_t capture_time_ms) {
    RTPVideoHeaderVP8 vp8_header = {};
    vp8_header.temporalIdx = tid;
    RTPVideoHeader rtp_video_header;
    rtp_video_header.frame_type = VideoFrameType::kVideoFrameKey;
    rtp_video_header.width = kWidth;
    rtp_video_header.height = kHeight;
    rtp_video_header.rotation = kVideoRotation_0;
    rtp_video_header.content_type = VideoContentType::UNSPECIFIED;
    rtp_video_header.is_first_packet_in_frame = true;
    rtp_video_header.simulcastIdx = 0;
    rtp_video_header.codec = kVideoCodecVP8;
    rtp_video_header.video_type_header = vp8_header;
    rtp_video_header.video_timing = {0u, 0u, 0u, 0u, 0u, 0u, false};

    const uint8_t payload[100] = {0};
    bool success = module->impl_->OnSendingRtpFrame(0, 0, kPayloadType, true);

    success &= sender->SendVideo(
        kPayloadType, VideoCodecType::kVideoCodecVP8, rtp_timestamp,
        Timestamp::Millis(capture_time_ms), payload, sizeof(payload),
        rtp_video_header, TimeDelta::Zero(), {});
    return success;
  }

  void IncomingRtcpNack(const RtpRtcpModule* module, uint16_t sequence_number) {
    bool sender = module->impl_->SSRC() == kSenderSsrc;
    rtcp::Nack nack;
    uint16_t list[1];
    list[0] = sequence_number;
    const uint16_t kListLength = sizeof(list) / sizeof(list[0]);
    nack.SetSenderSsrc(sender ? kReceiverSsrc : kSenderSsrc);
    nack.SetMediaSsrc(sender ? kSenderSsrc : kReceiverSsrc);
    nack.SetPacketIds(list, kListLength);
    rtc::Buffer packet = nack.Build();
    module->impl_->IncomingRtcpPacket(packet);
  }
};

TEST_F(RtpRtcpImpl2Test, RetransmitsAllLayers) {
  // Send frames.
  EXPECT_EQ(0, sender_.RtpSent());
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(),
                        kBaseLayerTid));  // kSequenceNumber
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(),
                        kHigherLayerTid));  // kSequenceNumber + 1
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(),
                        kNoTemporalIdx));  // kSequenceNumber + 2
  EXPECT_EQ(3, sender_.RtpSent());
  EXPECT_EQ(kSequenceNumber + 2, sender_.LastRtpSequenceNumber());

  // Min required delay until retransmit = 5 + RTT ms (RTT = 0).
  AdvanceTime(TimeDelta::Millis(5));

  // Frame with kBaseLayerTid re-sent.
  IncomingRtcpNack(&sender_, kSequenceNumber);
  EXPECT_EQ(4, sender_.RtpSent());
  EXPECT_EQ(kSequenceNumber, sender_.LastRtpSequenceNumber());
  // Frame with kHigherLayerTid re-sent.
  IncomingRtcpNack(&sender_, kSequenceNumber + 1);
  EXPECT_EQ(5, sender_.RtpSent());
  EXPECT_EQ(kSequenceNumber + 1, sender_.LastRtpSequenceNumber());
  // Frame with kNoTemporalIdx re-sent.
  IncomingRtcpNack(&sender_, kSequenceNumber + 2);
  EXPECT_EQ(6, sender_.RtpSent());
  EXPECT_EQ(kSequenceNumber + 2, sender_.LastRtpSequenceNumber());
}

TEST_F(RtpRtcpImpl2Test, Rtt) {
  RtpPacketReceived packet;
  packet.SetTimestamp(1);
  packet.SetSequenceNumber(123);
  packet.SetSsrc(kSenderSsrc);
  packet.AllocatePayload(100 - 12);
  receiver_.receive_statistics_->OnRtpPacket(packet);

  // Send Frame before sending an SR.
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  // Sender module should send an SR.
  EXPECT_EQ(0, sender_.impl_->SendRTCP(kRtcpReport));
  AdvanceTime(kOneWayNetworkDelay);

  // Receiver module should send a RR with a response to the last received SR.
  EXPECT_EQ(0, receiver_.impl_->SendRTCP(kRtcpReport));
  AdvanceTime(kOneWayNetworkDelay);

  // Verify RTT.
  EXPECT_THAT(sender_.impl_->LastRtt(),
              Near(2 * kOneWayNetworkDelay, TimeDelta::Millis(1)));

  // Verify RTT from rtt_stats config.
  EXPECT_EQ(0, sender_.rtt_stats_.LastProcessedRtt());
  EXPECT_EQ(0, sender_.impl_->rtt_ms());
  AdvanceTime(TimeDelta::Millis(1000));

  EXPECT_NEAR(2 * kOneWayNetworkDelay.ms(),
              sender_.rtt_stats_.LastProcessedRtt(), 1);
  EXPECT_NEAR(2 * kOneWayNetworkDelay.ms(), sender_.impl_->rtt_ms(), 1);
}

TEST_F(RtpRtcpImpl2Test, RttForReceiverOnly) {
  // Receiver module should send a Receiver reference time report block (RRTR).
  EXPECT_EQ(0, receiver_.impl_->SendRTCP(kRtcpReport));

  // Sender module should send a response to the last received RRTR (DLRR).
  AdvanceTime(TimeDelta::Millis(1000));
  // Send Frame before sending a SR.
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  EXPECT_EQ(0, sender_.impl_->SendRTCP(kRtcpReport));

  // Verify RTT.
  EXPECT_EQ(0, receiver_.rtt_stats_.LastProcessedRtt());
  EXPECT_EQ(0, receiver_.impl_->rtt_ms());
  AdvanceTime(TimeDelta::Millis(1000));
  EXPECT_NEAR(2 * kOneWayNetworkDelay.ms(),
              receiver_.rtt_stats_.LastProcessedRtt(), 1);
  EXPECT_NEAR(2 * kOneWayNetworkDelay.ms(), receiver_.impl_->rtt_ms(), 1);
}

TEST_F(RtpRtcpImpl2Test, NoSrBeforeMedia) {
  // Ignore fake transport delays in this test.
  sender_.transport_.SimulateNetworkDelay(TimeDelta::Zero());
  receiver_.transport_.SimulateNetworkDelay(TimeDelta::Zero());

  // Move ahead to the instant a rtcp is expected.
  // Verify no SR is sent before media has been sent, RR should still be sent
  // from the receiving module though.
  AdvanceTime(kDefaultReportInterval / 2);
  EXPECT_EQ(sender_.transport_.NumRtcpSent(), 0u);
  EXPECT_EQ(receiver_.transport_.NumRtcpSent(), 1u);

  // RTCP should be triggered by the RTP send.
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  EXPECT_EQ(sender_.transport_.NumRtcpSent(), 1u);
}

TEST_F(RtpRtcpImpl2Test, RtcpPacketTypeCounter_Nack) {
  EXPECT_EQ(0U, sender_.RtcpReceived().nack_packets);
  EXPECT_EQ(0U, receiver_.RtcpSent().nack_packets);

  // Receive module sends a NACK.
  const uint16_t kNackLength = 1;
  uint16_t nack_list[kNackLength] = {123};
  EXPECT_EQ(0, receiver_.impl_->SendNACK(nack_list, kNackLength));
  AdvanceTime(kOneWayNetworkDelay);
  EXPECT_EQ(1U, receiver_.RtcpSent().nack_packets);

  // Send module receives the NACK.
  EXPECT_EQ(1U, sender_.RtcpReceived().nack_packets);
}

TEST_F(RtpRtcpImpl2Test, AddStreamDataCounters) {
  StreamDataCounters rtp;
  const Timestamp kStartTime = Timestamp::Seconds(1);
  rtp.first_packet_time = kStartTime;
  rtp.transmitted.packets = 1;
  rtp.transmitted.payload_bytes = 1;
  rtp.transmitted.header_bytes = 2;
  rtp.transmitted.padding_bytes = 3;
  EXPECT_EQ(rtp.transmitted.TotalBytes(), rtp.transmitted.payload_bytes +
                                              rtp.transmitted.header_bytes +
                                              rtp.transmitted.padding_bytes);

  StreamDataCounters rtp2;
  rtp2.transmitted.packets = 10;
  rtp2.transmitted.payload_bytes = 10;
  rtp2.retransmitted.header_bytes = 4;
  rtp2.retransmitted.payload_bytes = 5;
  rtp2.retransmitted.padding_bytes = 6;
  rtp2.retransmitted.packets = 7;
  rtp2.fec.packets = 8;

  StreamDataCounters sum = rtp;
  sum.Add(rtp2);
  EXPECT_EQ(sum.first_packet_time, kStartTime);
  EXPECT_EQ(11U, sum.transmitted.packets);
  EXPECT_EQ(11U, sum.transmitted.payload_bytes);
  EXPECT_EQ(2U, sum.transmitted.header_bytes);
  EXPECT_EQ(3U, sum.transmitted.padding_bytes);
  EXPECT_EQ(4U, sum.retransmitted.header_bytes);
  EXPECT_EQ(5U, sum.retransmitted.payload_bytes);
  EXPECT_EQ(6U, sum.retransmitted.padding_bytes);
  EXPECT_EQ(7U, sum.retransmitted.packets);
  EXPECT_EQ(8U, sum.fec.packets);
  EXPECT_EQ(sum.transmitted.TotalBytes(),
            rtp.transmitted.TotalBytes() + rtp2.transmitted.TotalBytes());

  StreamDataCounters rtp3;
  rtp3.first_packet_time = kStartTime + TimeDelta::Millis(10);
  sum.Add(rtp3);
  EXPECT_EQ(sum.first_packet_time, kStartTime);  // Holds oldest time.
}

TEST_F(RtpRtcpImpl2Test, SendsInitialNackList) {
  // Send module sends a NACK.
  const uint16_t kNackLength = 1;
  uint16_t nack_list[kNackLength] = {123};
  EXPECT_EQ(0U, sender_.RtcpSent().nack_packets);
  // Send Frame before sending a compound RTCP that starts with SR.
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  EXPECT_EQ(0, sender_.impl_->SendNACK(nack_list, kNackLength));
  EXPECT_EQ(1U, sender_.RtcpSent().nack_packets);
  EXPECT_THAT(sender_.LastNackListSent(), ElementsAre(123));
}

TEST_F(RtpRtcpImpl2Test, SendsExtendedNackList) {
  // Send module sends a NACK.
  const uint16_t kNackLength = 1;
  uint16_t nack_list[kNackLength] = {123};
  EXPECT_EQ(0U, sender_.RtcpSent().nack_packets);
  // Send Frame before sending a compound RTCP that starts with SR.
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  EXPECT_EQ(0, sender_.impl_->SendNACK(nack_list, kNackLength));
  EXPECT_EQ(1U, sender_.RtcpSent().nack_packets);
  EXPECT_THAT(sender_.LastNackListSent(), ElementsAre(123));

  // Same list not re-send.
  EXPECT_EQ(0, sender_.impl_->SendNACK(nack_list, kNackLength));
  EXPECT_EQ(1U, sender_.RtcpSent().nack_packets);
  EXPECT_THAT(sender_.LastNackListSent(), ElementsAre(123));

  // Only extended list sent.
  const uint16_t kNackExtLength = 2;
  uint16_t nack_list_ext[kNackExtLength] = {123, 124};
  EXPECT_EQ(0, sender_.impl_->SendNACK(nack_list_ext, kNackExtLength));
  EXPECT_EQ(2U, sender_.RtcpSent().nack_packets);
  EXPECT_THAT(sender_.LastNackListSent(), ElementsAre(124));
}

TEST_F(RtpRtcpImpl2Test, ReSendsNackListAfterRttMs) {
  sender_.transport_.SimulateNetworkDelay(TimeDelta::Zero());
  // Send module sends a NACK.
  const uint16_t kNackLength = 2;
  uint16_t nack_list[kNackLength] = {123, 125};
  EXPECT_EQ(0U, sender_.RtcpSent().nack_packets);
  // Send Frame before sending a compound RTCP that starts with SR.
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  EXPECT_EQ(0, sender_.impl_->SendNACK(nack_list, kNackLength));
  EXPECT_EQ(1U, sender_.RtcpSent().nack_packets);
  EXPECT_THAT(sender_.LastNackListSent(), ElementsAre(123, 125));

  // Same list not re-send, rtt interval has not passed.
  const TimeDelta kStartupRtt = TimeDelta::Millis(100);
  AdvanceTime(kStartupRtt);
  EXPECT_EQ(0, sender_.impl_->SendNACK(nack_list, kNackLength));
  EXPECT_EQ(1U, sender_.RtcpSent().nack_packets);

  // Rtt interval passed, full list sent.
  AdvanceTime(TimeDelta::Millis(1));
  EXPECT_EQ(0, sender_.impl_->SendNACK(nack_list, kNackLength));
  EXPECT_EQ(2U, sender_.RtcpSent().nack_packets);
  EXPECT_THAT(sender_.LastNackListSent(), ElementsAre(123, 125));
}

TEST_F(RtpRtcpImpl2Test, UniqueNackRequests) {
  receiver_.transport_.SimulateNetworkDelay(TimeDelta::Zero());
  EXPECT_EQ(0U, receiver_.RtcpSent().nack_packets);
  EXPECT_EQ(0U, receiver_.RtcpSent().nack_requests);
  EXPECT_EQ(0U, receiver_.RtcpSent().unique_nack_requests);
  EXPECT_EQ(0, receiver_.RtcpSent().UniqueNackRequestsInPercent());

  // Receive module sends NACK request.
  const uint16_t kNackLength = 4;
  uint16_t nack_list[kNackLength] = {10, 11, 13, 18};
  EXPECT_EQ(0, receiver_.impl_->SendNACK(nack_list, kNackLength));
  EXPECT_EQ(1U, receiver_.RtcpSent().nack_packets);
  EXPECT_EQ(4U, receiver_.RtcpSent().nack_requests);
  EXPECT_EQ(4U, receiver_.RtcpSent().unique_nack_requests);
  EXPECT_THAT(receiver_.LastNackListSent(), ElementsAre(10, 11, 13, 18));

  // Send module receives the request.
  EXPECT_EQ(1U, sender_.RtcpReceived().nack_packets);
  EXPECT_EQ(4U, sender_.RtcpReceived().nack_requests);
  EXPECT_EQ(4U, sender_.RtcpReceived().unique_nack_requests);
  EXPECT_EQ(100, sender_.RtcpReceived().UniqueNackRequestsInPercent());

  // Receive module sends new request with duplicated packets.
  const TimeDelta kStartupRtt = TimeDelta::Millis(100);
  AdvanceTime(kStartupRtt + TimeDelta::Millis(1));
  const uint16_t kNackLength2 = 4;
  uint16_t nack_list2[kNackLength2] = {11, 18, 20, 21};
  EXPECT_EQ(0, receiver_.impl_->SendNACK(nack_list2, kNackLength2));
  EXPECT_EQ(2U, receiver_.RtcpSent().nack_packets);
  EXPECT_EQ(8U, receiver_.RtcpSent().nack_requests);
  EXPECT_EQ(6U, receiver_.RtcpSent().unique_nack_requests);
  EXPECT_THAT(receiver_.LastNackListSent(), ElementsAre(11, 18, 20, 21));

  // Send module receives the request.
  EXPECT_EQ(2U, sender_.RtcpReceived().nack_packets);
  EXPECT_EQ(8U, sender_.RtcpReceived().nack_requests);
  EXPECT_EQ(6U, sender_.RtcpReceived().unique_nack_requests);
  EXPECT_EQ(75, sender_.RtcpReceived().UniqueNackRequestsInPercent());
}

TEST_F(RtpRtcpImpl2Test, ConfigurableRtcpReportInterval) {
  const TimeDelta kVideoReportInterval = TimeDelta::Millis(3000);

  // Recreate sender impl with new configuration, and redo setup.
  sender_.SetRtcpReportIntervalAndReset(kVideoReportInterval);
  SetUp();

  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));

  // Initial state
  EXPECT_EQ(0u, sender_.transport_.NumRtcpSent());

  // Move ahead to the last ms before a rtcp is expected, no action.
  AdvanceTime(kVideoReportInterval / 2 - TimeDelta::Millis(1));
  EXPECT_EQ(sender_.transport_.NumRtcpSent(), 0u);

  // Move ahead to the first rtcp. Send RTCP.
  AdvanceTime(TimeDelta::Millis(1));
  EXPECT_EQ(sender_.transport_.NumRtcpSent(), 1u);

  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));

  // Move ahead to the last possible second before second rtcp is expected.
  AdvanceTime(kVideoReportInterval * 1 / 2 - TimeDelta::Millis(1));
  EXPECT_EQ(sender_.transport_.NumRtcpSent(), 1u);

  // Move ahead into the range of second rtcp, the second rtcp may be sent.
  AdvanceTime(TimeDelta::Millis(1));
  EXPECT_GE(sender_.transport_.NumRtcpSent(), 1u);

  AdvanceTime(kVideoReportInterval / 2);
  EXPECT_GE(sender_.transport_.NumRtcpSent(), 1u);

  // Move out the range of second rtcp, the second rtcp must have been sent.
  AdvanceTime(kVideoReportInterval / 2);
  EXPECT_EQ(sender_.transport_.NumRtcpSent(), 2u);
}

TEST_F(RtpRtcpImpl2Test, RtpSenderEgressTimestampOffset) {
  // RTP timestamp offset not explicitly set, default to random value.
  uint16_t seqno = sender_.impl_->GetRtpState().sequence_number;
  uint32_t media_rtp_ts = 1001;
  uint32_t rtp_ts = media_rtp_ts + sender_.impl_->StartTimestamp();
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid, rtp_ts,
                        /*capture_time_ms=*/0));
  AdvanceTime(kOneWayNetworkDelay);
  EXPECT_THAT(
      sender_.impl_->GetSentRtpPacketInfos(std::vector<uint16_t>{seqno}),
      ElementsAre(Field(&RtpSequenceNumberMap::Info::timestamp, media_rtp_ts)));

  RtpState saved_rtp_state = sender_.impl_->GetRtpState();

  // Change RTP timestamp offset.
  sender_.impl_->SetStartTimestamp(2000);

  // Restores RtpState and make sure the old timestamp offset is in place.
  sender_.impl_->SetRtpState(saved_rtp_state);
  seqno = sender_.impl_->GetRtpState().sequence_number;
  media_rtp_ts = 1031;
  rtp_ts = media_rtp_ts + sender_.impl_->StartTimestamp();
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid, rtp_ts,
                        /*capture_time_ms=*/0));
  AdvanceTime(kOneWayNetworkDelay);
  EXPECT_THAT(
      sender_.impl_->GetSentRtpPacketInfos(std::vector<uint16_t>{seqno}),
      ElementsAre(Field(&RtpSequenceNumberMap::Info::timestamp, media_rtp_ts)));
}

TEST_F(RtpRtcpImpl2Test, StoresPacketInfoForSentPackets) {
  const uint32_t kStartTimestamp = 1u;
  SetUp();
  sender_.impl_->SetStartTimestamp(kStartTimestamp);

  sender_.impl_->SetSequenceNumber(1);

  PacedPacketInfo pacing_info;
  RtpPacketToSend packet(nullptr);
  packet.set_packet_type(RtpPacketToSend::Type::kVideo);
  packet.SetSsrc(kSenderSsrc);

  // Single-packet frame.
  packet.SetTimestamp(1);
  packet.set_first_packet_of_frame(true);
  packet.SetMarker(true);
  sender_.impl_->TrySendPacket(std::make_unique<RtpPacketToSend>(packet),
                               pacing_info);
  AdvanceTime(TimeDelta::Millis(1));

  std::vector<RtpSequenceNumberMap::Info> seqno_info =
      sender_.impl_->GetSentRtpPacketInfos(std::vector<uint16_t>{1});

  EXPECT_THAT(seqno_info, ElementsAre(RtpSequenceNumberMap::Info(
                              /*timestamp=*/1 - kStartTimestamp,
                              /*is_first=*/1,
                              /*is_last=*/1)));

  // Three-packet frame.
  packet.SetTimestamp(2);
  packet.set_first_packet_of_frame(true);
  packet.SetMarker(false);
  sender_.impl_->TrySendPacket(std::make_unique<RtpPacketToSend>(packet),
                               pacing_info);

  packet.set_first_packet_of_frame(false);
  sender_.impl_->TrySendPacket(std::make_unique<RtpPacketToSend>(packet),
                               pacing_info);

  packet.SetMarker(true);
  sender_.impl_->TrySendPacket(std::make_unique<RtpPacketToSend>(packet),
                               pacing_info);

  AdvanceTime(TimeDelta::Millis(1));

  seqno_info =
      sender_.impl_->GetSentRtpPacketInfos(std::vector<uint16_t>{2, 3, 4});

  EXPECT_THAT(seqno_info, ElementsAre(RtpSequenceNumberMap::Info(
                                          /*timestamp=*/2 - kStartTimestamp,
                                          /*is_first=*/1,
                                          /*is_last=*/0),
                                      RtpSequenceNumberMap::Info(
                                          /*timestamp=*/2 - kStartTimestamp,
                                          /*is_first=*/0,
                                          /*is_last=*/0),
                                      RtpSequenceNumberMap::Info(
                                          /*timestamp=*/2 - kStartTimestamp,
                                          /*is_first=*/0,
                                          /*is_last=*/1)));
}

// Checks that the sender report stats are not available if no RTCP SR was sent.
TEST_F(RtpRtcpImpl2Test, SenderReportStatsNotAvailable) {
  EXPECT_THAT(receiver_.impl_->GetSenderReportStats(), Eq(absl::nullopt));
}

// Checks that the sender report stats are available if an RTCP SR was sent.
TEST_F(RtpRtcpImpl2Test, SenderReportStatsAvailable) {
  // Send a frame in order to send an SR.
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  // Send an SR.
  ASSERT_THAT(sender_.impl_->SendRTCP(kRtcpReport), Eq(0));
  AdvanceTime(kOneWayNetworkDelay);
  EXPECT_THAT(receiver_.impl_->GetSenderReportStats(), Not(Eq(absl::nullopt)));
}

// Checks that the sender report stats are not available if an RTCP SR with an
// unexpected SSRC is received.
TEST_F(RtpRtcpImpl2Test, SenderReportStatsNotUpdatedWithUnexpectedSsrc) {
  constexpr uint32_t kUnexpectedSenderSsrc = 0x87654321;
  static_assert(kUnexpectedSenderSsrc != kSenderSsrc, "");
  // Forge a sender report and pass it to the receiver as if an RTCP SR were
  // sent by an unexpected sender.
  rtcp::SenderReport sr;
  sr.SetSenderSsrc(kUnexpectedSenderSsrc);
  sr.SetNtp({/*seconds=*/1u, /*fractions=*/1u << 31});
  sr.SetPacketCount(123u);
  sr.SetOctetCount(456u);
  auto raw_packet = sr.Build();
  receiver_.impl_->IncomingRtcpPacket(raw_packet);
  EXPECT_THAT(receiver_.impl_->GetSenderReportStats(), Eq(absl::nullopt));
}

// Checks the stats derived from the last received RTCP SR are set correctly.
TEST_F(RtpRtcpImpl2Test, SenderReportStatsCheckStatsFromLastReport) {
  using SenderReportStats = RtpRtcpInterface::SenderReportStats;
  const NtpTime ntp(/*seconds=*/1u, /*fractions=*/1u << 31);
  constexpr uint32_t kPacketCount = 123u;
  constexpr uint32_t kOctetCount = 456u;
  // Forge a sender report and pass it to the receiver as if an RTCP SR were
  // sent by the sender.
  rtcp::SenderReport sr;
  sr.SetSenderSsrc(kSenderSsrc);
  sr.SetNtp(ntp);
  sr.SetPacketCount(kPacketCount);
  sr.SetOctetCount(kOctetCount);
  auto raw_packet = sr.Build();
  receiver_.impl_->IncomingRtcpPacket(raw_packet);

  EXPECT_THAT(
      receiver_.impl_->GetSenderReportStats(),
      Optional(AllOf(Field(&SenderReportStats::last_remote_timestamp, Eq(ntp)),
                     Field(&SenderReportStats::packets_sent, Eq(kPacketCount)),
                     Field(&SenderReportStats::bytes_sent, Eq(kOctetCount)))));
}

// Checks that the sender report stats count equals the number of sent RTCP SRs.
TEST_F(RtpRtcpImpl2Test, SenderReportStatsCount) {
  using SenderReportStats = RtpRtcpInterface::SenderReportStats;
  // Send a frame in order to send an SR.
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  // Send the first SR.
  ASSERT_THAT(sender_.impl_->SendRTCP(kRtcpReport), Eq(0));
  AdvanceTime(kOneWayNetworkDelay);
  EXPECT_THAT(receiver_.impl_->GetSenderReportStats(),
              Optional(Field(&SenderReportStats::reports_count, Eq(1u))));
  // Send the second SR.
  ASSERT_THAT(sender_.impl_->SendRTCP(kRtcpReport), Eq(0));
  AdvanceTime(kOneWayNetworkDelay);
  EXPECT_THAT(receiver_.impl_->GetSenderReportStats(),
              Optional(Field(&SenderReportStats::reports_count, Eq(2u))));
}

// Checks that the sender report stats include a valid arrival time if an RTCP
// SR was sent.
TEST_F(RtpRtcpImpl2Test, SenderReportStatsArrivalTimestampSet) {
  // Send a frame in order to send an SR.
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  // Send an SR.
  ASSERT_THAT(sender_.impl_->SendRTCP(kRtcpReport), Eq(0));
  AdvanceTime(kOneWayNetworkDelay);
  auto stats = receiver_.impl_->GetSenderReportStats();
  ASSERT_THAT(stats, Not(Eq(absl::nullopt)));
  EXPECT_TRUE(stats->last_arrival_timestamp.Valid());
}

// Checks that the packet and byte counters from an RTCP SR are not zero once
// a frame is sent.
TEST_F(RtpRtcpImpl2Test, SenderReportStatsPacketByteCounters) {
  using SenderReportStats = RtpRtcpInterface::SenderReportStats;
  // Send a frame in order to send an SR.
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  ASSERT_THAT(sender_.transport_.rtp_packets_sent_, Gt(0));
  // Advance time otherwise the RTCP SR report will not include any packets
  // generated by `SendFrame()`.
  AdvanceTime(TimeDelta::Millis(1));
  // Send an SR.
  ASSERT_THAT(sender_.impl_->SendRTCP(kRtcpReport), Eq(0));
  AdvanceTime(kOneWayNetworkDelay);
  EXPECT_THAT(receiver_.impl_->GetSenderReportStats(),
              Optional(AllOf(Field(&SenderReportStats::packets_sent, Gt(0u)),
                             Field(&SenderReportStats::bytes_sent, Gt(0u)))));
}

TEST_F(RtpRtcpImpl2Test, SendingVideoAdvancesSequenceNumber) {
  const uint16_t sequence_number = sender_.impl_->SequenceNumber();
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  ASSERT_THAT(sender_.transport_.rtp_packets_sent_, Gt(0));
  EXPECT_EQ(sequence_number + 1, sender_.impl_->SequenceNumber());
}

TEST_F(RtpRtcpImpl2Test, SequenceNumberNotAdvancedWhenNotSending) {
  const uint16_t sequence_number = sender_.impl_->SequenceNumber();
  sender_.impl_->SetSendingMediaStatus(false);
  EXPECT_FALSE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  ASSERT_THAT(sender_.transport_.rtp_packets_sent_, Eq(0));
  EXPECT_EQ(sequence_number, sender_.impl_->SequenceNumber());
}

TEST_F(RtpRtcpImpl2Test, PaddingNotAllowedInMiddleOfFrame) {
  constexpr size_t kPaddingSize = 100;

  // Can't send padding before media.
  EXPECT_THAT(sender_.impl_->GeneratePadding(kPaddingSize), SizeIs(0u));

  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));

  // Padding is now ok.
  EXPECT_THAT(sender_.impl_->GeneratePadding(kPaddingSize), SizeIs(Gt(0u)));

  // Send half a video frame.
  PacedPacketInfo pacing_info;
  std::unique_ptr<RtpPacketToSend> packet =
      sender_.impl_->RtpSender()->AllocatePacket();
  packet->set_packet_type(RtpPacketToSend::Type::kVideo);
  packet->set_first_packet_of_frame(true);
  packet->SetMarker(false);  // Marker false - not last packet of frame.

  EXPECT_TRUE(sender_.impl_->TrySendPacket(std::move(packet), pacing_info));

  // Padding not allowed in middle of frame.
  EXPECT_THAT(sender_.impl_->GeneratePadding(kPaddingSize), SizeIs(0u));

  packet = sender_.impl_->RtpSender()->AllocatePacket();
  packet->set_packet_type(RtpPacketToSend::Type::kVideo);
  packet->set_first_packet_of_frame(true);
  packet->SetMarker(true);

  EXPECT_TRUE(sender_.impl_->TrySendPacket(std::move(packet), pacing_info));

  // Padding is OK again.
  EXPECT_THAT(sender_.impl_->GeneratePadding(kPaddingSize), SizeIs(Gt(0u)));
}

TEST_F(RtpRtcpImpl2Test, PaddingTimestampMatchesMedia) {
  constexpr size_t kPaddingSize = 100;
  const uint32_t kTimestamp = 123;

  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid,
                        kTimestamp, /*capture_time_ms=*/0));
  EXPECT_EQ(sender_.last_packet().Timestamp(), kTimestamp);
  uint16_t media_seq = sender_.last_packet().SequenceNumber();

  // Generate and send padding.
  auto padding = sender_.impl_->GeneratePadding(kPaddingSize);
  ASSERT_FALSE(padding.empty());
  for (auto& packet : padding) {
    sender_.impl_->TrySendPacket(std::move(packet), PacedPacketInfo());
  }

  // Verify we sent a new packet, but with the same timestamp.
  EXPECT_NE(sender_.last_packet().SequenceNumber(), media_seq);
  EXPECT_EQ(sender_.last_packet().Timestamp(), kTimestamp);
}

TEST_F(RtpRtcpImpl2Test, AssignsTransportSequenceNumber) {
  sender_.RegisterHeaderExtension(TransportSequenceNumber::Uri(),
                                  kTransportSequenceNumberExtensionId);

  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  uint16_t first_transport_seq = 0;
  EXPECT_TRUE(sender_.last_packet().GetExtension<TransportSequenceNumber>(
      &first_transport_seq));

  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  uint16_t second_transport_seq = 0;
  EXPECT_TRUE(sender_.last_packet().GetExtension<TransportSequenceNumber>(
      &second_transport_seq));

  EXPECT_EQ(first_transport_seq + 1, second_transport_seq);
}

TEST_F(RtpRtcpImpl2Test, AssignsAbsoluteSendTime) {
  sender_.RegisterHeaderExtension(AbsoluteSendTime::Uri(),
                                  kAbsoluteSendTimeExtensionId);

  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  EXPECT_NE(sender_.last_packet().GetExtension<AbsoluteSendTime>(), 0u);
}

TEST_F(RtpRtcpImpl2Test, AssignsTransmissionTimeOffset) {
  sender_.RegisterHeaderExtension(TransmissionOffset::Uri(),
                                  kTransmissionOffsetExtensionId);

  constexpr TimeDelta kOffset = TimeDelta::Millis(100);
  // Transmission offset is calculated from difference between capture time
  // and send time.
  int64_t capture_time_ms = time_controller_.GetClock()->TimeInMilliseconds();
  time_controller_.AdvanceTime(kOffset);

  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid,
                        /*timestamp=*/0, capture_time_ms));
  EXPECT_EQ(sender_.last_packet().GetExtension<TransmissionOffset>(),
            kOffset.ms() * kCaptureTimeMsToRtpTimestamp);
}

TEST_F(RtpRtcpImpl2Test, PropagatesSentPacketInfo) {
  sender_.RegisterHeaderExtension(TransportSequenceNumber::Uri(),
                                  kTransportSequenceNumberExtensionId);
  Timestamp now = time_controller_.GetClock()->CurrentTime();
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  EXPECT_THAT(sender_.last_sent_packet(),
              Optional(AllOf(
                  Field(&RtpRtcpModule::SentPacket::packet_id,
                        Eq(sender_.last_packet()
                               .GetExtension<TransportSequenceNumber>())),
                  Field(&RtpRtcpModule::SentPacket::capture_time, Eq(now)),
                  Field(&RtpRtcpModule::SentPacket::ssrc, Eq(kSenderSsrc)))));
}

TEST_F(RtpRtcpImpl2Test, GeneratesFlexfec) {
  constexpr int kFlexfecPayloadType = 118;
  constexpr uint32_t kFlexfecSsrc = 17;
  const char kNoMid[] = "";
  const std::vector<RtpExtension> kNoRtpExtensions;
  const std::vector<RtpExtensionSize> kNoRtpExtensionSizes;

  // Make sure FlexFec sequence numbers start at a different point than media.
  const uint16_t fec_start_seq = sender_.impl_->SequenceNumber() + 100;
  RtpState start_state;
  start_state.sequence_number = fec_start_seq;
  FlexfecSender flexfec_sender(kFlexfecPayloadType, kFlexfecSsrc, kSenderSsrc,
                               kNoMid, kNoRtpExtensions, kNoRtpExtensionSizes,
                               &start_state, time_controller_.GetClock());
  ReinitWithFec(&flexfec_sender, /*red_payload_type=*/absl::nullopt);

  // Parameters selected to generate a single FEC packet per media packet.
  FecProtectionParams params;
  params.fec_rate = 15;
  params.max_fec_frames = 1;
  params.fec_mask_type = kFecMaskRandom;
  sender_.impl_->SetFecProtectionParams(params, params);

  // Send a one packet frame, expect one media packet and one FEC packet.
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  ASSERT_THAT(sender_.transport_.rtp_packets_sent_, Eq(2));

  const RtpPacketReceived& fec_packet = sender_.last_packet();
  EXPECT_EQ(fec_packet.SequenceNumber(), fec_start_seq);
  EXPECT_EQ(fec_packet.Ssrc(), kFlexfecSsrc);
  EXPECT_EQ(fec_packet.PayloadType(), kFlexfecPayloadType);
}

TEST_F(RtpRtcpImpl2Test, GeneratesUlpfec) {
  constexpr int kUlpfecPayloadType = 118;
  constexpr int kRedPayloadType = 119;
  UlpfecGenerator ulpfec_sender(kRedPayloadType, kUlpfecPayloadType,
                                time_controller_.GetClock());
  ReinitWithFec(&ulpfec_sender, kRedPayloadType);

  // Parameters selected to generate a single FEC packet per media packet.
  FecProtectionParams params;
  params.fec_rate = 15;
  params.max_fec_frames = 1;
  params.fec_mask_type = kFecMaskRandom;
  sender_.impl_->SetFecProtectionParams(params, params);

  // Send a one packet frame, expect one media packet and one FEC packet.
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  ASSERT_THAT(sender_.transport_.rtp_packets_sent_, Eq(2));

  // Ulpfec is sent on the media ssrc, sharing the sequene number series.
  const RtpPacketReceived& fec_packet = sender_.last_packet();
  EXPECT_EQ(fec_packet.SequenceNumber(), kSequenceNumber + 1);
  EXPECT_EQ(fec_packet.Ssrc(), kSenderSsrc);
  // The packets are encapsulated in RED packets, check that and that the RED
  // header (first byte of payload) indicates the desired FEC payload type.
  EXPECT_EQ(fec_packet.PayloadType(), kRedPayloadType);
  EXPECT_EQ(fec_packet.payload()[0], kUlpfecPayloadType);
}

TEST_F(RtpRtcpImpl2Test, RtpStateReflectsCurrentState) {
  // Verify that that each of the field of GetRtpState actually reflects
  // the current state.

  // Current time will be used for `timestamp`, `capture_time` and
  // `last_timestamp_time`.
  const Timestamp time = time_controller_.GetClock()->CurrentTime();

  // Use different than default sequence number to test `sequence_number`.
  const uint16_t kSeq = kSequenceNumber + 123;
  // Hard-coded value for `start_timestamp`.
  const uint32_t kStartTimestamp = 3456;
  const Timestamp capture_time = time;
  const uint32_t timestamp = capture_time.ms() * kCaptureTimeMsToRtpTimestamp;

  sender_.impl_->SetSequenceNumber(kSeq - 1);
  sender_.impl_->SetStartTimestamp(kStartTimestamp);
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));

  // Simulate an RTCP receiver report in order to populate `ssrc_has_acked`.
  ReportBlockData ack[1];
  ack[0].set_source_ssrc(kSenderSsrc);
  ack[0].set_extended_highest_sequence_number(kSeq);
  sender_.impl_->OnReceivedRtcpReportBlocks(ack);

  RtpState state = sender_.impl_->GetRtpState();
  EXPECT_EQ(state.sequence_number, kSeq);
  EXPECT_EQ(state.start_timestamp, kStartTimestamp);
  EXPECT_EQ(state.timestamp, timestamp);
  EXPECT_EQ(state.capture_time, capture_time);
  EXPECT_EQ(state.last_timestamp_time, time);
  EXPECT_EQ(state.ssrc_has_acked, true);

  // Reset sender, advance time, restore state. Directly observing state
  // is not feasible, so just verify returned state matches what we set.
  sender_.CreateModuleImpl();
  time_controller_.AdvanceTime(TimeDelta::Millis(10));
  sender_.impl_->SetRtpState(state);

  state = sender_.impl_->GetRtpState();
  EXPECT_EQ(state.sequence_number, kSeq);
  EXPECT_EQ(state.start_timestamp, kStartTimestamp);
  EXPECT_EQ(state.timestamp, timestamp);
  EXPECT_EQ(state.capture_time, capture_time);
  EXPECT_EQ(state.last_timestamp_time, time);
  EXPECT_EQ(state.ssrc_has_acked, true);
}

TEST_F(RtpRtcpImpl2Test, RtxRtpStateReflectsCurrentState) {
  // Enable RTX.
  sender_.impl_->SetStorePacketsStatus(/*enable=*/true, /*number_to_store=*/10);
  sender_.impl_->SetRtxSendPayloadType(kRtxPayloadType, kPayloadType);
  sender_.impl_->SetRtxSendStatus(kRtxRetransmitted | kRtxRedundantPayloads);

  // `start_timestamp` is the only timestamp populate in the RTX state.
  const uint32_t kStartTimestamp = 3456;
  sender_.impl_->SetStartTimestamp(kStartTimestamp);

  // Send a frame and ask for a retransmit of the last packet. Capture the RTX
  // packet in order to verify RTX sequence number.
  EXPECT_TRUE(SendFrame(&sender_, sender_video_.get(), kBaseLayerTid));
  time_controller_.AdvanceTime(TimeDelta::Millis(5));
  sender_.impl_->OnReceivedNack(
      std::vector<uint16_t>{sender_.transport_.last_packet_.SequenceNumber()});
  RtpPacketReceived& rtx_packet = sender_.transport_.last_packet_;
  EXPECT_EQ(rtx_packet.Ssrc(), kRtxSenderSsrc);

  // Simulate an RTCP receiver report in order to populate `ssrc_has_acked`.
  ReportBlockData ack[1];
  ack[0].set_source_ssrc(kRtxSenderSsrc);
  ack[0].set_extended_highest_sequence_number(rtx_packet.SequenceNumber());
  sender_.impl_->OnReceivedRtcpReportBlocks(ack);

  RtpState rtp_state = sender_.impl_->GetRtpState();
  RtpState rtx_state = sender_.impl_->GetRtxState();
  EXPECT_EQ(rtx_state.start_timestamp, kStartTimestamp);
  EXPECT_EQ(rtx_state.ssrc_has_acked, true);
  EXPECT_EQ(rtx_state.sequence_number, rtx_packet.SequenceNumber() + 1);

  // Reset sender, advance time, restore state. Directly observing state
  // is not feasible, so just verify returned state matches what we set.
  // Needs SetRtpState() too in order to propagate start timestamp.
  sender_.CreateModuleImpl();
  time_controller_.AdvanceTime(TimeDelta::Millis(10));
  sender_.impl_->SetRtpState(rtp_state);
  sender_.impl_->SetRtxState(rtx_state);

  rtx_state = sender_.impl_->GetRtxState();
  EXPECT_EQ(rtx_state.start_timestamp, kStartTimestamp);
  EXPECT_EQ(rtx_state.ssrc_has_acked, true);
  EXPECT_EQ(rtx_state.sequence_number, rtx_packet.SequenceNumber() + 1);
}

TEST_F(RtpRtcpImpl2Test, CanSendPacketReturnTrueForMediaPacketIfSendingMedia) {
  RtpHeaderExtensionMap extensions;
  RtpPacketToSend packet(&extensions);
  packet.SetSsrc(sender_.impl_->SSRC());
  packet.set_packet_type(RtpPacketMediaType::kAudio);
  sender_.impl_->SetSendingMediaStatus(true);

  EXPECT_TRUE(sender_.impl_->CanSendPacket(packet));
}

TEST_F(RtpRtcpImpl2Test,
       CanSendPacketReturnFalseForMediaPacketIfNotSendingMedia) {
  RtpHeaderExtensionMap extensions;
  RtpPacketToSend packet(&extensions);
  packet.SetSsrc(sender_.impl_->SSRC());
  packet.set_packet_type(RtpPacketMediaType::kAudio);
  sender_.impl_->SetSendingMediaStatus(false);

  EXPECT_FALSE(sender_.impl_->CanSendPacket(packet));
}

TEST_F(RtpRtcpImpl2Test,
       CanSendPacketReturnFalseForPaddingPacketOnMediaSsrcBeforeMediaPacket) {
  RtpHeaderExtensionMap extensions;
  RtpPacketToSend packet(&extensions);
  packet.SetSsrc(sender_.impl_->SSRC());
  packet.set_packet_type(RtpPacketMediaType::kPadding);
  sender_.impl_->SetSendingMediaStatus(true);

  EXPECT_FALSE(sender_.impl_->CanSendPacket(packet));
}

TEST_F(RtpRtcpImpl2Test, RtpSequenceNumberSetByAssignSequenceNumber) {
  RtpHeaderExtensionMap extensions;
  RtpPacketToSend packet(&extensions);
  packet.SetSsrc(sender_.impl_->SSRC());

  sender_.impl_->SetSequenceNumber(1);
  sender_.impl_->AssignSequenceNumber(packet);
  EXPECT_EQ(packet.SequenceNumber(), 1);
  sender_.impl_->AssignSequenceNumber(packet);
  EXPECT_EQ(packet.SequenceNumber(), 2);
}

TEST_F(RtpRtcpImpl2Test, SendPacketSendsPacketOnTransport) {
  RtpHeaderExtensionMap extensions;
  auto packet = std::make_unique<RtpPacketToSend>(&extensions);
  packet->SetSsrc(sender_.impl_->SSRC());
  packet->set_packet_type(RtpPacketMediaType::kAudio);

  sender_.impl_->SendPacket(std::move(packet), PacedPacketInfo());
  EXPECT_EQ(sender_.RtpSent(), 1);
}

}  // namespace webrtc
