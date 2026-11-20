/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_RTP_RTCP_SOURCE_RTCP_RECEIVER_H_
#define MODULES_RTP_RTCP_SOURCE_RTCP_RECEIVER_H_

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <span>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "api/environment/environment.h"
#include "api/sequence_checker.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/video_codec_constants.h"
#include "modules/rtp_rtcp/include/report_block_data.h"
#include "modules/rtp_rtcp/include/rtcp_statistics.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtcp_nack_stats.h"
#include "modules/rtp_rtcp/source/rtcp_packet/dlrr.h"
#include "modules/rtp_rtcp/source/rtcp_packet/tmmb_item.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_interface.h"
#include "rtc_base/containers/flat_map.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/system/no_unique_address.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {
namespace rtcp {
class CommonHeader;
class ReportBlock;
class Rrtr;
class TmmbItem;
}  // namespace rtcp

class RTCPReceiver final {
 public:
  class ModuleRtpRtcp {
   public:
    virtual void SetTmmbn(std::vector<rtcp::TmmbItem> bounding_set) = 0;
    virtual void OnRequestSendReport() = 0;
    virtual void OnReceivedNack(
        const std::vector<uint16_t>& nack_sequence_numbers) = 0;
    virtual void OnReceivedRtcpReportBlocks(
        std::span<const ReportBlockData> report_blocks) = 0;

   protected:
    virtual ~ModuleRtpRtcp() = default;
  };
  // Standardized stats derived from the non-sender RTT.
  class NonSenderRttStats {
   public:
    NonSenderRttStats() = default;
    NonSenderRttStats(const NonSenderRttStats&) = default;
    NonSenderRttStats& operator=(const NonSenderRttStats&) = default;
    ~NonSenderRttStats() = default;
    void Update(TimeDelta non_sender_rtt_seconds) {
      round_trip_time_ = non_sender_rtt_seconds;
      total_round_trip_time_ += non_sender_rtt_seconds;
      round_trip_time_measurements_++;
    }
    void Invalidate() { round_trip_time_.reset(); }
    // https://www.w3.org/TR/webrtc-stats/#dom-rtcremoteoutboundrtpstreamstats-roundtriptime
    std::optional<TimeDelta> round_trip_time() const {
      return round_trip_time_;
    }
    // https://www.w3.org/TR/webrtc-stats/#dom-rtcremoteoutboundrtpstreamstats-totalroundtriptime
    TimeDelta total_round_trip_time() const { return total_round_trip_time_; }
    // https://www.w3.org/TR/webrtc-stats/#dom-rtcremoteoutboundrtpstreamstats-roundtriptimemeasurements
    int round_trip_time_measurements() const {
      return round_trip_time_measurements_;
    }

   private:
    std::optional<TimeDelta> round_trip_time_;
    TimeDelta total_round_trip_time_ = TimeDelta::Zero();
    int round_trip_time_measurements_ = 0;
  };

  RTCPReceiver(const Environment& env,
               const RtpRtcpInterface::Configuration& config,
               ModuleRtpRtcp* owner);

  ~RTCPReceiver();

  void IncomingPacket(std::span<const uint8_t> packet);

  void set_local_media_ssrc(uint32_t ssrc);
  uint32_t local_media_ssrc() const;

  void SetRemoteSSRC(uint32_t ssrc);

  // Returns stats based on the received RTCP Sender Reports.
  std::optional<RtpRtcpInterface::SenderReportStats> GetSenderReportStats()
      const;

  std::vector<rtcp::ReceiveTimeInfo> ConsumeReceivedXrReferenceTimeInfo();

  // Get received sender packet and octet counts
  void RemoteRTCPSenderInfo(uint32_t* packet_count,
                            uint32_t* octet_count,
                            int64_t* ntp_timestamp_ms,
                            int64_t* remote_ntp_timestamp_ms) const;

  std::optional<TimeDelta> AverageRtt() const;
  std::optional<TimeDelta> LastRtt() const;

  // Returns non-sender RTT metrics for the remote SSRC.
  NonSenderRttStats GetNonSenderRTT() const;

  void SetNonSenderRttMeasurement(bool enabled);
  std::optional<TimeDelta> GetAndResetXrRrRtt();

  // Called once per second on the worker thread to do rtt calculations.
  // Returns an optional rtt value if one is available.
  std::optional<TimeDelta> OnPeriodicRttUpdate(Timestamp newer_than,
                                               bool sending);

  // A snapshot of Report Blocks with additional data of interest to statistics.
  // Within this list, the source SSRC is unique and ReportBlockData represents
  // the latest Report Block that was received for that SSRC.
  std::vector<ReportBlockData> GetLatestReportBlockData() const;

  std::vector<rtcp::TmmbItem> TmmbrReceived();
  std::vector<rtcp::TmmbItem> BoundingSet(bool* tmmbr_owner);
  // Set new bandwidth and notify remote clients about it.
  void NotifyTmmbrUpdated();

 private:
  // A lightweight inlined set of local SSRCs.
  class RegisteredSsrcs {
   public:
    static constexpr size_t kMediaSsrcIndex = 0;
    // Initializes the set of registered local SSRCS by extracting them from the
    // provided `config`.
    explicit RegisteredSsrcs(const RtpRtcpInterface::Configuration& config);

    // Indicates if `ssrc` is in the set of registered local SSRCs.
    bool contains(uint32_t ssrc) const;
    uint32_t media_ssrc() const;
    void set_media_ssrc(uint32_t ssrc);

   private:
    RTC_NO_UNIQUE_ADDRESS SequenceChecker packet_sequence_checker_;
    absl::InlinedVector<uint32_t, kMaxSimulcastStreams> ssrcs_
        RTC_GUARDED_BY(packet_sequence_checker_);
  };

  struct PacketInformation;

  // Structure for handling TMMBR rtcp messages (RFC5104, section 3.5.4).
  struct TimedTmmbrItem {
    rtcp::TmmbItem tmmbr_item;
    Timestamp last_updated;
  };

  // Structure for storing received RRTR RTCP messages (RFC3611, section 4.4).
  struct RrtrInformation {
    RrtrInformation(uint32_t ssrc,
                    uint32_t received_remote_mid_ntp_time,
                    uint32_t local_receive_mid_ntp_time)
        : ssrc(ssrc),
          received_remote_mid_ntp_time(received_remote_mid_ntp_time),
          local_receive_mid_ntp_time(local_receive_mid_ntp_time) {}

    uint32_t ssrc;
    // Received NTP timestamp in compact representation.
    uint32_t received_remote_mid_ntp_time;
    // NTP time when the report was received in compact representation.
    uint32_t local_receive_mid_ntp_time;
  };

  struct LastFir {
    friend bool operator==(const LastFir&, const LastFir&) = default;

    uint32_t ssrc;
    uint8_t sequence_number;
  };

  class RttStats {
   public:
    RttStats() = default;
    RttStats(const RttStats&) = default;
    RttStats& operator=(const RttStats&) = default;

    void AddRtt(TimeDelta rtt);

    std::optional<TimeDelta> last_rtt() const;
    std::optional<TimeDelta> average_rtt() const;

   private:
    TimeDelta last_rtt_ = TimeDelta::Zero();
    TimeDelta sum_rtt_ = TimeDelta::Zero();
    size_t num_rtts_ = 0;
  };

  bool ParseCompoundPacket(std::span<const uint8_t> packet,
                           PacketInformation* packet_information);

  void TriggerCallbacksFromRtcpPacket(
      const PacketInformation& packet_information);

  bool HandleSenderReport(const rtcp::CommonHeader& rtcp_block,
                          PacketInformation* packet_information)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  bool HandleReceiverReport(const rtcp::CommonHeader& rtcp_block,
                            PacketInformation* packet_information)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  void HandleReportBlock(const rtcp::ReportBlock& report_block,
                         PacketInformation* packet_information,
                         uint32_t remote_ssrc)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  bool HandleSdes(const rtcp::CommonHeader& rtcp_block,
                  PacketInformation* packet_information)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  bool HandleXr(const rtcp::CommonHeader& rtcp_block,
                PacketInformation* packet_information,
                bool& contains_dlrr,
                uint32_t& ssrc)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  void HandleXrReceiveReferenceTime(uint32_t sender_ssrc,
                                    const rtcp::Rrtr& rrtr)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  void HandleXrDlrrReportBlock(uint32_t ssrc, const rtcp::ReceiveTimeInfo& rti)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  bool HandleNack(const rtcp::CommonHeader& rtcp_block,
                  PacketInformation* packet_information)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  bool HandleApp(const rtcp::CommonHeader& rtcp_block,
                 PacketInformation* packet_information)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  bool HandleBye(const rtcp::CommonHeader& rtcp_block)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  bool HandlePli(const rtcp::CommonHeader& rtcp_block,
                 PacketInformation* packet_information)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  void HandlePsfbApp(const rtcp::CommonHeader& rtcp_block,
                     PacketInformation* packet_information)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  bool HandleTmmbr(const rtcp::CommonHeader& rtcp_block,
                   PacketInformation* packet_information)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  bool HandleTmmbn(const rtcp::CommonHeader& rtcp_block,
                   PacketInformation* packet_information)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  bool HandleSrReq(const rtcp::CommonHeader& rtcp_block,
                   PacketInformation* packet_information)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  bool HandleFir(const rtcp::CommonHeader& rtcp_block,
                 PacketInformation* packet_information)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  void HandleTransportFeedback(const rtcp::CommonHeader& rtcp_block,
                               PacketInformation* packet_information)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);
  bool HandleCongestionControlFeedback(const rtcp::CommonHeader& rtcp_block,
                                       PacketInformation* packet_information)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  bool RtcpRrTimeoutLocked(Timestamp now)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  bool RtcpRrSequenceNumberTimeoutLocked(Timestamp now)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(rtcp_receiver_lock_);

  const Environment env_;
  const bool receiver_only_;
  const bool enable_congestion_controller_feedback_;
  ModuleRtpRtcp* const rtp_rtcp_;
  // The set of registered local SSRCs.
  RegisteredSsrcs registered_ssrcs_;

  NetworkLinkRtcpObserver* const network_link_rtcp_observer_;
  RtcpEventObserver* const rtcp_event_observer_;
  RtcpIntraFrameObserver* const rtcp_intra_frame_observer_;
  NetworkStateEstimateObserver* const network_state_estimate_observer_;
  const TimeDelta report_interval_;

  mutable Mutex rtcp_receiver_lock_;
  uint32_t remote_ssrc_ RTC_GUARDED_BY(rtcp_receiver_lock_);

  // Received sender report.
  RtpRtcpInterface::SenderReportStats remote_sender_
      RTC_GUARDED_BY(rtcp_receiver_lock_);

  // Received RRTR information in ascending receive time order.
  std::list<RrtrInformation> received_rrtrs_
      RTC_GUARDED_BY(rtcp_receiver_lock_);
  // Received RRTR information mapped by remote ssrc.
  flat_map<uint32_t, std::list<RrtrInformation>::iterator>
      received_rrtrs_ssrc_it_ RTC_GUARDED_BY(rtcp_receiver_lock_);

  // Estimated rtt, nullopt when there is no valid estimate.
  bool xr_rrtr_status_ RTC_GUARDED_BY(rtcp_receiver_lock_);
  std::optional<TimeDelta> xr_rr_rtt_;

  std::list<TimedTmmbrItem> tmmbr_ RTC_GUARDED_BY(rtcp_receiver_lock_);

  // Last received timber notification rtcp messages (RFC5104, section 3.5.4).
  std::vector<rtcp::TmmbItem> tmmbn_ RTC_GUARDED_BY(rtcp_receiver_lock_);

  // Round-Trip Time calculated from received report blocks (for RTP sender)
  RttStats rtts_ RTC_GUARDED_BY(rtcp_receiver_lock_);
  // Round-Trip Time calculated from received DLRR blocks (for RTP receiver)
  NonSenderRttStats non_sender_rtts_ RTC_GUARDED_BY(rtcp_receiver_lock_);

  // Report blocks per local source ssrc.
  flat_map<uint32_t, ReportBlockData> received_report_blocks_
      RTC_GUARDED_BY(rtcp_receiver_lock_);

  Timestamp last_key_frame_request_ RTC_GUARDED_BY(rtcp_receiver_lock_) =
      Timestamp::MinusInfinity();
  std::optional<LastFir> last_fir_ RTC_GUARDED_BY(rtcp_receiver_lock_);

  // The last time we received an RTCP Report block for this module.
  Timestamp last_received_rb_ RTC_GUARDED_BY(rtcp_receiver_lock_) =
      Timestamp::PlusInfinity();

  // The time we last received an RTCP RR telling we have successfully
  // delivered RTP packet to the remote side.
  Timestamp last_increased_sequence_number_ = Timestamp::PlusInfinity();

  RtcpCnameCallback* const cname_callback_;
  ReportBlockDataObserver* const report_block_data_observer_;

  RtcpPacketTypeCounterObserver* const packet_type_counter_observer_;
  RtcpPacketTypeCounter packet_type_counter_;

  RtcpNackStats nack_stats_;

  size_t num_skipped_packets_;
  Timestamp last_skipped_packets_warning_;
};
}  // namespace webrtc
#endif  // MODULES_RTP_RTCP_SOURCE_RTCP_RECEIVER_H_
