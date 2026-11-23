/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_RTP_RTCP_SOURCE_RTCP_SENDER_H_
#define MODULES_RTP_RTCP_SOURCE_RTCP_SENDER_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/call/transport.h"
#include "api/environment/environment.h"
#include "api/rtp_headers.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/rtp_rtcp/include/receive_statistics.h"
#include "modules/rtp_rtcp/include/rtcp_statistics.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtcp_nack_stats.h"
#include "modules/rtp_rtcp/source/rtcp_packet.h"
#include "modules/rtp_rtcp/source/rtcp_packet/dlrr.h"
#include "modules/rtp_rtcp/source/rtcp_packet/report_block.h"
#include "modules/rtp_rtcp/source/rtcp_packet/tmmb_item.h"
#include "modules/rtp_rtcp/source/rtcp_receiver.h"
#include "rtc_base/random.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/thread_annotations.h"
#include "system_wrappers/include/ntp_time.h"

namespace webrtc {

class RTCPSender final {
 public:
  struct Configuration {
    // True for a audio version of the RTP/RTCP module object false will create
    // a video version.
    bool audio = false;
    // SSRC for sending media.
    uint32_t local_media_ssrc = 0;
    // Function for fetching SSRC for sending RTCP reports when this object
    // belongs to a ModuleRtpRtcp2 that is not used for sending RTP.
    absl::AnyInvocable<uint32_t() const> recv_ssrc_callback;
    // FlexFec SSRC is fetched from `flexfec_sender`.

    // Transport object that will be called when packets are ready to be sent
    // out on the network.
    Transport* outgoing_transport = nullptr;
    // Estimate RTT as non-sender as described in
    // https://tools.ietf.org/html/rfc3611#section-4.4 and #section-4.5
    bool non_sender_rtt_measurement = false;

    // Mandatory callback which is used by RTCPSender to schedule the next time
    // to evaluate if RTCP should be sent by means of
    // TimeToSendRTCPReport/SendRTCP. The RTCPSender client still needs to call
    // TimeToSendRTCPReport/SendRTCP to actually get RTCP sent.
    absl::AnyInvocable<void(TimeDelta)> schedule_next_rtcp_send_evaluation;

    TimeDelta rtcp_report_interval;

    // Initial RTCP status. Defaults to kOff to preserve backward compatibility
    // with raw low-level RTCPSender tests and usages that assume the sender
    // starts with RTCP disabled until explicitly configured.
    RtcpMode rtcp_mode = RtcpMode::kOff;
    ReceiveStatisticsProvider* receive_statistics = nullptr;
    RtcpPacketTypeCounterObserver* rtcp_packet_type_counter_observer = nullptr;
  };
  struct FeedbackState {
    FeedbackState();
    FeedbackState(const FeedbackState&);
    FeedbackState(FeedbackState&&);

    ~FeedbackState();

    uint32_t packets_sent;
    size_t media_bytes_sent;
    DataRate send_bitrate;

    uint32_t remote_sr;
    NtpTime last_rr;

    std::vector<rtcp::ReceiveTimeInfo> last_xr_rtis;

    // Used when generating TMMBR.
    RTCPReceiver* receiver;
  };

  RTCPSender(const Environment& env, Configuration config);

  RTCPSender() = delete;
  RTCPSender(const RTCPSender&) = delete;
  RTCPSender& operator=(const RTCPSender&) = delete;

  ~RTCPSender();

  RtcpMode Status() const RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);
  void SetRTCPStatus(RtcpMode method) RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

  bool Sending() const RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);
  void SetSendingStatus(const FeedbackState& feedback_state,
                        bool enabled)
      RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);  // combine the functions

  void SetNonSenderRttMeasurement(bool enabled)
      RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

  void SetTimestampOffset(uint32_t timestamp_offset)
      RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

  void SetLastRtpTime(uint32_t rtp_timestamp,
                      std::optional<Timestamp> capture_time,
                      std::optional<int8_t> payload_type)
      RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

  void SetRtpClockRate(int8_t payload_type, int rtp_clock_rate_hz)
      RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

  uint32_t SSRC() const;

  void SetRemoteSSRC(uint32_t ssrc) RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

  int32_t SetCNAME(absl::string_view cName)
      RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

  bool TimeToSendRTCPReport(bool send_keyframe_before_rtp = false) const
      RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

  int32_t SendRTCP(const FeedbackState& feedback_state,
                   RTCPPacketType packetType,
                   std::span<const uint16_t> nacks = {})
      RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

  void SetRemb(int64_t bitrate_bps, std::vector<uint32_t> ssrcs)
      RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

  void UnsetRemb() RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

  bool TMMBR() const RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

  void SetMaxRtpPacketSize(size_t max_packet_size)
      RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

  void SetTmmbn(std::vector<rtcp::TmmbItem> bounding_set)
      RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

  void SetCsrcs(const std::vector<uint32_t>& csrcs)
      RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

  void SetTargetBitrate(unsigned int target_bitrate)
      RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);
  void SendCombinedRtcpPacket(
      std::vector<std::unique_ptr<rtcp::RtcpPacket>> rtcp_packets)
      RTC_LOCKS_EXCLUDED(mutex_rtcp_sender_);

 private:
  class RtcpContext;
  class PacketSender;

  uint32_t ComputeSsrc() const RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);

  std::optional<int32_t> ComputeCompoundRTCPPacket(
      const FeedbackState& feedback_state,
      RTCPPacketType packet_type,
      std::span<const uint16_t> nacks,
      PacketSender& sender) RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);

  TimeDelta ComputeTimeUntilNextReport(DataRate send_bitrate)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);

  // Determine which RTCP messages should be sent and setup flags.
  void PrepareReport(const FeedbackState& feedback_state)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);

  std::vector<rtcp::ReportBlock> CreateReportBlocks(
      const FeedbackState& feedback_state)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);

  void BuildSR(const RtcpContext& context, PacketSender& sender)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);
  void BuildRR(const RtcpContext& context, PacketSender& sender)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);
  void BuildSDES(const RtcpContext& context, PacketSender& sender)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);
  void BuildPLI(const RtcpContext& context, PacketSender& sender)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);
  void BuildREMB(const RtcpContext& context, PacketSender& sender)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);
  void BuildTMMBR(const RtcpContext& context, PacketSender& sender)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);
  void BuildTMMBN(const RtcpContext& context, PacketSender& sender)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);
  void BuildAPP(const RtcpContext& context, PacketSender& sender)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);
  void BuildExtendedReports(const RtcpContext& context, PacketSender& sender)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);
  void BuildBYE(const RtcpContext& context, PacketSender& sender)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);
  void BuildFIR(const RtcpContext& context, PacketSender& sender)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);
  void BuildNACK(const RtcpContext& context, PacketSender& sender)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);

  // `duration` being TimeDelta::Zero() means schedule immediately.
  void SetNextRtcpSendEvaluationDuration(TimeDelta duration)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);

  const Environment env_;
  const bool is_rtp_sender_;
  const bool audio_;
  // The SSRC used for sending when is_rtp_sender_ is true
  // and sending_ is true.
  const uint32_t send_ssrc_;
  // The function used for getting the right SSRC to send from
  // when the RTCPSender is used with a ModuleRtpRtcp that is not
  // configured for sending RTP (is_rtp_sender_ is false).
  absl::AnyInvocable<uint32_t() const> recv_ssrc_callback_;
  Random random_ RTC_GUARDED_BY(mutex_rtcp_sender_);
  RtcpMode method_ RTC_GUARDED_BY(mutex_rtcp_sender_);

  Transport* const transport_;

  const TimeDelta report_interval_;
  absl::AnyInvocable<void(TimeDelta)> schedule_next_rtcp_send_evaluation_;

  mutable Mutex mutex_rtcp_sender_;
  bool sending_ RTC_GUARDED_BY(mutex_rtcp_sender_);

  std::optional<Timestamp> next_time_to_send_rtcp_
      RTC_GUARDED_BY(mutex_rtcp_sender_);

  uint32_t timestamp_offset_ RTC_GUARDED_BY(mutex_rtcp_sender_);
  uint32_t last_rtp_timestamp_ RTC_GUARDED_BY(mutex_rtcp_sender_);
  std::optional<Timestamp> last_frame_capture_time_
      RTC_GUARDED_BY(mutex_rtcp_sender_);
  // SSRC that we receive on our RTP channel
  uint32_t remote_ssrc_ RTC_GUARDED_BY(mutex_rtcp_sender_);
  std::string cname_ RTC_GUARDED_BY(mutex_rtcp_sender_);

  ReceiveStatisticsProvider* receive_statistics_
      RTC_GUARDED_BY(mutex_rtcp_sender_);

  // send CSRCs
  std::vector<uint32_t> csrcs_ RTC_GUARDED_BY(mutex_rtcp_sender_);

  // Full intra request
  uint8_t sequence_number_fir_ RTC_GUARDED_BY(mutex_rtcp_sender_);

  // REMB
  int64_t remb_bitrate_ RTC_GUARDED_BY(mutex_rtcp_sender_);
  std::vector<uint32_t> remb_ssrcs_ RTC_GUARDED_BY(mutex_rtcp_sender_);

  std::vector<rtcp::TmmbItem> tmmbn_to_send_ RTC_GUARDED_BY(mutex_rtcp_sender_);
  uint32_t tmmbr_send_bps_ RTC_GUARDED_BY(mutex_rtcp_sender_);
  uint32_t packet_oh_send_ RTC_GUARDED_BY(mutex_rtcp_sender_);
  size_t max_packet_size_ RTC_GUARDED_BY(mutex_rtcp_sender_);

  // True if sending of XR Receiver reference time report is enabled.
  bool xr_send_receiver_reference_time_enabled_
      RTC_GUARDED_BY(mutex_rtcp_sender_);

  RtcpPacketTypeCounterObserver* const packet_type_counter_observer_;
  RtcpPacketTypeCounter packet_type_counter_ RTC_GUARDED_BY(mutex_rtcp_sender_);

  RtcpNackStats nack_stats_ RTC_GUARDED_BY(mutex_rtcp_sender_);

  std::map<int8_t, int> rtp_clock_rates_khz_ RTC_GUARDED_BY(mutex_rtcp_sender_);
  int8_t last_payload_type_ RTC_GUARDED_BY(mutex_rtcp_sender_);

  void SetFlag(uint32_t type, bool is_volatile)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);
  bool IsFlagPresent(uint32_t type) const
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);
  bool ConsumeFlag(uint32_t type, bool forced = false)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);
  bool AllVolatileFlagsConsumed() const
      RTC_EXCLUSIVE_LOCKS_REQUIRED(mutex_rtcp_sender_);
  struct ReportFlag {
    ReportFlag(uint32_t type, bool is_volatile)
        : type(type), is_volatile(is_volatile) {}
    bool operator<(const ReportFlag& flag) const { return type < flag.type; }
    bool operator==(const ReportFlag& flag) const { return type == flag.type; }
    const uint32_t type;
    const bool is_volatile;
  };

  std::set<ReportFlag> report_flags_ RTC_GUARDED_BY(mutex_rtcp_sender_);

  typedef void (RTCPSender::*BuilderFunc)(const RtcpContext&, PacketSender&);
  // Map from RTCPPacketType to builder.
  std::map<uint32_t, BuilderFunc> builders_;
};
}  // namespace webrtc

#endif  // MODULES_RTP_RTCP_SOURCE_RTCP_SENDER_H_
