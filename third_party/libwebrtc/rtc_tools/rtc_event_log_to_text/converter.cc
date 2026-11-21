/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_tools/rtc_event_log_to_text/converter.h"

#include <inttypes.h>

#include <map>
#include <vector>

#include "logging/rtc_event_log/events/logged_rtp_rtcp.h"
#include "logging/rtc_event_log/events/rtc_event_alr_state.h"
#include "logging/rtc_event_log/events/rtc_event_audio_network_adaptation.h"
#include "logging/rtc_event_log/events/rtc_event_audio_playout.h"
#include "logging/rtc_event_log/events/rtc_event_audio_receive_stream_config.h"
#include "logging/rtc_event_log/events/rtc_event_audio_send_stream_config.h"
#include "logging/rtc_event_log/events/rtc_event_begin_log.h"
#include "logging/rtc_event_log/events/rtc_event_bwe_update_delay_based.h"
#include "logging/rtc_event_log/events/rtc_event_bwe_update_loss_based.h"
#include "logging/rtc_event_log/events/rtc_event_dtls_transport_state.h"
#include "logging/rtc_event_log/events/rtc_event_dtls_writable_state.h"
#include "logging/rtc_event_log/events/rtc_event_end_log.h"
#include "logging/rtc_event_log/events/rtc_event_frame_decoded.h"
#include "logging/rtc_event_log/events/rtc_event_generic_ack_received.h"
#include "logging/rtc_event_log/events/rtc_event_generic_packet_received.h"
#include "logging/rtc_event_log/events/rtc_event_generic_packet_sent.h"
#include "logging/rtc_event_log/events/rtc_event_ice_candidate_pair.h"
#include "logging/rtc_event_log/events/rtc_event_ice_candidate_pair_config.h"
#include "logging/rtc_event_log/events/rtc_event_neteq_set_minimum_delay.h"
#include "logging/rtc_event_log/events/rtc_event_probe_cluster_created.h"
#include "logging/rtc_event_log/events/rtc_event_probe_result_failure.h"
#include "logging/rtc_event_log/events/rtc_event_probe_result_success.h"
#include "logging/rtc_event_log/events/rtc_event_remote_estimate.h"
#include "logging/rtc_event_log/events/rtc_event_route_change.h"
#include "logging/rtc_event_log/events/rtc_event_video_receive_stream_config.h"
#include "logging/rtc_event_log/events/rtc_event_video_send_stream_config.h"
#include "logging/rtc_event_log/rtc_event_log_parser.h"
#include "logging/rtc_event_log/rtc_event_processor.h"
#include "logging/rtc_event_log/rtc_stream_config.h"
#include "rtc_base/logging.h"

namespace webrtc {
namespace {

void PrintHeaderExtensionConfig(
    FILE* output,
    const std::vector<RtpExtension>& rtp_extensions) {
  if (rtp_extensions.empty())
    return;
  fprintf(output, " extension_map=");
  for (const RtpExtension& extension : rtp_extensions) {
    fprintf(output, "{uri:%s,id:%d}", extension.uri.c_str(), extension.id);
  }
}

}  // namespace

bool Convert(std::string inputfile,
             FILE* output,
             ParsedRtcEventLog::UnconfiguredHeaderExtensions
                 handle_unconfigured_extensions) {
  ParsedRtcEventLog parsed_log(handle_unconfigured_extensions,
                               /*allow_incomplete_logs=*/true);

  auto status = parsed_log.ParseFile(inputfile);
  if (!status.ok()) {
    RTC_LOG(LS_ERROR) << "Failed to parse " << inputfile << ": "
                      << status.message();
    return false;
  }

  auto audio_recv_stream_handler = [&](const LoggedAudioRecvConfig& event) {
    fprintf(output, "AUDIO_RECV_STREAM_CONFIG %" PRId64, event.log_time_ms());
    fprintf(output, " remote_ssrc=%u", event.config.remote_ssrc);
    fprintf(output, " local_ssrc=%u", event.config.local_ssrc);
    PrintHeaderExtensionConfig(output, event.config.rtp_extensions);
    fprintf(output, "\n");
  };

  auto audio_send_stream_handler = [&](const LoggedAudioSendConfig& event) {
    fprintf(output, "AUDIO_SEND_STREAM_CONFIG %" PRId64, event.log_time_ms());
    fprintf(output, " ssrc=%u", event.config.local_ssrc);
    PrintHeaderExtensionConfig(output, event.config.rtp_extensions);
    fprintf(output, "\n");
  };

  auto video_recv_stream_handler = [&](const LoggedVideoRecvConfig& event) {
    fprintf(output, "VIDEO_RECV_STREAM_CONFIG %" PRId64, event.log_time_ms());
    fprintf(output, " remote_ssrc=%u", event.config.remote_ssrc);
    fprintf(output, " local_ssrc=%u", event.config.local_ssrc);
    fprintf(output, " rtx_ssrc=%u", event.config.rtx_ssrc);
    PrintHeaderExtensionConfig(output, event.config.rtp_extensions);
    fprintf(output, "\n");
  };

  auto video_send_stream_handler = [&](const LoggedVideoSendConfig& event) {
    fprintf(output, "VIDEO_SEND_STREAM_CONFIG %" PRId64, event.log_time_ms());
    fprintf(output, " ssrc=%u", event.config.local_ssrc);
    fprintf(output, " rtx_ssrc=%u", event.config.rtx_ssrc);
    PrintHeaderExtensionConfig(output, event.config.rtp_extensions);
    fprintf(output, "\n");
  };

  auto start_logging_handler = [&](const LoggedStartEvent& event) {
    fprintf(output, "START_LOG %" PRId64 "\n", event.log_time_ms());
  };

  auto stop_logging_handler = [&](const LoggedStopEvent& event) {
    fprintf(output, "STOP_LOG %" PRId64 "\n", event.log_time_ms());
  };

  auto alr_state_handler = [&](const LoggedAlrStateEvent& event) {
    fprintf(output, "ALR_STATE %" PRId64 " in_alr=%u\n", event.log_time_ms(),
            event.in_alr);
  };

  auto audio_playout_handler = [&](const LoggedAudioPlayoutEvent& event) {
    fprintf(output, "AUDIO_PLAYOUT %" PRId64 " ssrc=%u\n", event.log_time_ms(),
            event.ssrc);
  };

  auto neteq_set_minimum_delay_handler =
      [&](const LoggedNetEqSetMinimumDelayEvent& event) {
        fprintf(output,
                "NETEQ_SET_MINIMUM_DELAY %" PRId64
                " remote_ssrc=%u minimum_delay=%d\n",
                event.log_time_ms(), event.remote_ssrc, event.minimum_delay_ms);
      };

  auto audio_network_adaptation_handler =
      [&](const LoggedAudioNetworkAdaptationEvent& event) {
        fprintf(output, "AUDIO_NETWORK_ADAPTATION %" PRId64,
                event.log_time_ms());

        if (event.config.enable_dtx.has_value())
          fprintf(output, " enable_dtx=%u", event.config.enable_dtx.value());
        if (event.config.enable_fec.has_value())
          fprintf(output, " enable_fec=%u", event.config.enable_fec.value());
        if (event.config.bitrate_bps.has_value())
          fprintf(output, " bitrate_bps=%d", event.config.bitrate_bps.value());
        if (event.config.num_channels.has_value()) {
          fprintf(output, " num_channels=%zu",
                  event.config.num_channels.value());
        }
        if (event.config.frame_length_ms.has_value()) {
          fprintf(output, " frame_length_ms=%d",
                  event.config.frame_length_ms.value());
        }
        fprintf(output, " last_fl_change_increase=%u",
                event.config.last_fl_change_increase);
        if (event.config.uplink_packet_loss_fraction.has_value()) {
          fprintf(output, " uplink_packet_loss_fraction=%f",
                  event.config.uplink_packet_loss_fraction.value());
        }
        fprintf(output, "\n");
      };

  auto bwe_probe_cluster_created_handler =
      [&](const LoggedBweProbeClusterCreatedEvent& event) {
        fprintf(output,
                "BWE_PROBE_CREATED %" PRId64
                " id=%d bitrate_bps=%d min_packets=%u "
                "min_bytes=%u\n",
                event.log_time_ms(), event.id, event.bitrate_bps,
                event.min_packets, event.min_bytes);
      };

  auto bwe_probe_failure_handler =
      [&](const LoggedBweProbeFailureEvent& event) {
        fprintf(output, "BWE_PROBE_FAILURE %" PRId64 " id=%d reason=%d\n",
                event.log_time_ms(), event.id,
                static_cast<int>(event.failure_reason));
      };

  auto bwe_probe_success_handler =
      [&](const LoggedBweProbeSuccessEvent& event) {
        fprintf(output, "BWE_PROBE_SUCCESS %" PRId64 " id=%d bitrate_bps=%d\n",
                event.log_time_ms(), event.id, event.bitrate_bps);
      };

  auto bwe_delay_update_handler = [&](const LoggedBweDelayBasedUpdate& event) {
    static const std::map<BandwidthUsage, std::string> text{
        {BandwidthUsage::kBwNormal, "NORMAL"},
        {BandwidthUsage::kBwUnderusing, "UNDERUSING"},
        {BandwidthUsage::kBwOverusing, "OVERUSING"},
        {BandwidthUsage::kLast, "LAST"}};

    fprintf(output,
            "BWE_DELAY_BASED %" PRId64 " bitrate_bps=%d detector_state=%s\n",
            event.log_time_ms(), event.bitrate_bps,
            text.at(event.detector_state).c_str());
  };

  auto bwe_loss_update_handler = [&](const LoggedBweLossBasedUpdate& event) {
    fprintf(output,
            "BWE_LOSS_BASED %" PRId64
            " bitrate_bps=%d fraction_lost=%d "
            "expected_packets=%d\n",
            event.log_time_ms(), event.bitrate_bps, event.fraction_lost,
            event.expected_packets);
  };

  auto dtls_transport_state_handler =
      [&](const LoggedDtlsTransportState& event) {
        fprintf(output, "DTLS_TRANSPORT_STATE %" PRId64 " state=%d\n",
                event.log_time_ms(),
                static_cast<int>(event.dtls_transport_state));
      };

  auto dtls_transport_writable_handler =
      [&](const LoggedDtlsWritableState& event) {
        fprintf(output, "DTLS_WRITABLE %" PRId64 " writable=%u\n",
                event.log_time_ms(), event.writable);
      };

  auto ice_candidate_pair_config_handler =
      [&](const LoggedIceCandidatePairConfig& event) {
        static const std::map<IceCandidatePairConfigType, std::string>
            update_type_name{
                {IceCandidatePairConfigType::kAdded, "ADDED"},
                {IceCandidatePairConfigType::kUpdated, "UPDATED"},
                {IceCandidatePairConfigType::kDestroyed, "DESTROYED"},
                {IceCandidatePairConfigType::kSelected, "SELECTED"},
                {IceCandidatePairConfigType::kNumValues, "NUM_VALUES"}};

        static const std::map<IceCandidateType, std::string>
            candidate_type_name{{IceCandidateType::kHost, "LOCAL"},
                                {IceCandidateType::kSrflx, "STUN"},
                                {IceCandidateType::kPrflx, "PRFLX"},
                                {IceCandidateType::kRelay, "RELAY"}};

        static const std::map<IceCandidatePairProtocol, std::string>
            protocol_name{{IceCandidatePairProtocol::kUnknown, "UNKNOWN"},
                          {IceCandidatePairProtocol::kUdp, "UDP"},
                          {IceCandidatePairProtocol::kTcp, "TCP"},
                          {IceCandidatePairProtocol::kSsltcp, "SSLTCP"},
                          {IceCandidatePairProtocol::kTls, "TLS"},
                          {IceCandidatePairProtocol::kNumValues, "NUM_VALUES"}};

        static const std::map<IceCandidatePairAddressFamily, std::string>
            address_family_name{
                {IceCandidatePairAddressFamily::kUnknown, "UNKNOWN"},
                {IceCandidatePairAddressFamily::kIpv4, "IPv4"},
                {IceCandidatePairAddressFamily::kIpv6, "IPv6"},
                {IceCandidatePairAddressFamily::kNumValues, "NUM_VALUES"}};

        static const std::map<IceCandidateNetworkType, std::string>
            network_type_name{
                {IceCandidateNetworkType::kUnknown, "UNKNOWN"},
                {IceCandidateNetworkType::kEthernet, "ETHERNET"},
                {IceCandidateNetworkType::kLoopback, "LOOPBACK"},
                {IceCandidateNetworkType::kWifi, "WIFI"},
                {IceCandidateNetworkType::kVpn, "VPN"},
                {IceCandidateNetworkType::kCellular, "CELLULAR"},
                {IceCandidateNetworkType::kNumValues, "NUM_VALUES"}};

        fprintf(output, "ICE_CANDIDATE_CONFIG %" PRId64, event.log_time_ms());
        fprintf(output, " id=%u", event.candidate_pair_id);
        fprintf(output, " type=%s", update_type_name.at(event.type).c_str());
        fprintf(output, " local_network=%s",
                network_type_name.at(event.local_network_type).c_str());
        fprintf(output, " local_address_family=%s",
                address_family_name.at(event.local_address_family).c_str());
        fprintf(output, " local_candidate_type=%s",
                candidate_type_name.at(event.local_candidate_type).c_str());
        fprintf(output, " local_relay_protocol=%s",
                protocol_name.at(event.local_relay_protocol).c_str());
        fprintf(output, " remote_address=%s",
                address_family_name.at(event.remote_address_family).c_str());
        fprintf(output, " remote_candidate_type=%s",
                candidate_type_name.at(event.remote_candidate_type).c_str());
        fprintf(output, " candidate_pair_protocol=%s",
                protocol_name.at(event.candidate_pair_protocol).c_str());
        fprintf(output, "\n");
      };

  auto ice_candidate_pair_event_handler =
      [&](const LoggedIceCandidatePairEvent& event) {
        static const std::map<IceCandidatePairEventType, std::string>
            check_type_name{
                {IceCandidatePairEventType::kCheckSent, "CHECK_SENT"},
                {IceCandidatePairEventType::kCheckReceived, "CHECK_RECEIVED"},
                {IceCandidatePairEventType::kCheckResponseSent,
                 "CHECK_RESPONSE_SENT"},
                {IceCandidatePairEventType::kCheckResponseReceived,
                 "CHECK_RESPONSE_RECEIVED"},
                {IceCandidatePairEventType::kNumValues, "NUM_VALUES"}};

        fprintf(output, "ICE_CANDIDATE_UPDATE %" PRId64, event.log_time_ms());
        fprintf(output, " id=%u", event.candidate_pair_id);
        fprintf(output, " type=%s", check_type_name.at(event.type).c_str());
        fprintf(output, " transaction_id=%u", event.transaction_id);
        fprintf(output, "\n");
      };

  auto route_change_handler = [&](const LoggedRouteChangeEvent& event) {
    fprintf(output, "ROUTE_CHANGE %" PRId64 " connected=%u overhead=%u\n",
            event.log_time_ms(), event.connected, event.overhead);
  };

  auto remote_estimate_handler = [&](const LoggedRemoteEstimateEvent& event) {
    fprintf(output, "REMOTE_ESTIMATE %" PRId64, event.log_time_ms());
    if (event.link_capacity_lower.has_value()) {
      fprintf(output, " link_capacity_lower_kbps=%" PRId64,
              event.link_capacity_lower.value().kbps());
    }
    if (event.link_capacity_upper.has_value()) {
      fprintf(output, " link_capacity_upper_kbps=%" PRId64,
              event.link_capacity_upper.value().kbps());
    }
    fprintf(output, "\n");
  };

  auto incoming_rtp_packet_handler = [&](const LoggedRtpPacketIncoming& event) {
    fprintf(output, "RTP_IN %" PRId64, event.log_time_ms());
    fprintf(output, " ssrc=%u", event.rtp.header.ssrc);
    fprintf(output, " seq_no=%u", event.rtp.header.sequenceNumber);
    fprintf(output, " marker=%u", event.rtp.header.markerBit);
    fprintf(output, " pt=%u", event.rtp.header.payloadType);
    fprintf(output, " timestamp=%u", event.rtp.header.timestamp);
    if (event.rtp.header.extension.hasAbsoluteSendTime) {
      fprintf(output, " abs_send_time=%u",
              event.rtp.header.extension.absoluteSendTime);
    }
    if (event.rtp.header.extension.hasTransmissionTimeOffset) {
      fprintf(output, " transmission_offset=%d",
              event.rtp.header.extension.transmissionTimeOffset);
    }
    if (event.rtp.header.extension.audio_level()) {
      fprintf(output, " voice_activity=%d",
              event.rtp.header.extension.audio_level()->voice_activity());
      fprintf(output, " audio_level=%u",
              event.rtp.header.extension.audio_level()->level());
    }
    if (event.rtp.header.extension.hasVideoRotation) {
      fprintf(output, " video_rotation=%d",
              event.rtp.header.extension.videoRotation);
    }
    if (event.rtp.header.extension.hasTransportSequenceNumber) {
      fprintf(output, " transport_seq_no=%u",
              event.rtp.header.extension.transportSequenceNumber);
    }
    fprintf(output, " header_length=%zu", event.rtp.header_length);
    fprintf(output, " padding_length=%zu", event.rtp.header.paddingLength);
    fprintf(output, " total_length=%zu", event.rtp.total_length);
    fprintf(output, "\n");
  };

  auto outgoing_rtp_packet_handler = [&](const LoggedRtpPacketOutgoing& event) {
    fprintf(output, "RTP_OUT %" PRId64, event.log_time_ms());
    fprintf(output, " ssrc=%u", event.rtp.header.ssrc);
    fprintf(output, " seq_no=%u", event.rtp.header.sequenceNumber);
    fprintf(output, " marker=%u", event.rtp.header.markerBit);
    fprintf(output, " pt=%u", event.rtp.header.payloadType);
    fprintf(output, " timestamp=%u", event.rtp.header.timestamp);
    if (event.rtp.header.extension.hasAbsoluteSendTime) {
      fprintf(output, " abs_send_time=%u",
              event.rtp.header.extension.absoluteSendTime);
    }
    if (event.rtp.header.extension.hasTransmissionTimeOffset) {
      fprintf(output, " transmission_offset=%d",
              event.rtp.header.extension.transmissionTimeOffset);
    }
    if (event.rtp.header.extension.audio_level()) {
      fprintf(output, " voice_activity=%d",
              event.rtp.header.extension.audio_level()->voice_activity());
      fprintf(output, " audio_level=%u",
              event.rtp.header.extension.audio_level()->level());
    }
    if (event.rtp.header.extension.hasVideoRotation) {
      fprintf(output, " video_rotation=%d",
              event.rtp.header.extension.videoRotation);
    }
    if (event.rtp.header.extension.hasTransportSequenceNumber) {
      fprintf(output, " transport_seq_no=%u",
              event.rtp.header.extension.transportSequenceNumber);
    }
    fprintf(output, " header_length=%zu", event.rtp.header_length);
    fprintf(output, " padding_length=%zu", event.rtp.header.paddingLength);
    fprintf(output, " total_length=%zu", event.rtp.total_length);
    fprintf(output, "\n");
  };

  auto incoming_rtcp_packet_handler =
      [&](const LoggedRtcpPacketIncoming& event) {
        fprintf(output, "RTCP_IN %" PRId64 " <contents omitted>\n",
                event.log_time_ms());
      };

  auto outgoing_rtcp_packet_handler =
      [&](const LoggedRtcpPacketOutgoing& event) {
        fprintf(output, "RTCP_OUT %" PRId64 " <contents omitted>\n",
                event.log_time_ms());
      };

  auto generic_packet_received_handler =
      [&](const LoggedGenericPacketReceived& event) {
        fprintf(output,
                "GENERIC_PACKET_RECV %" PRId64 " packet_no=%" PRId64
                " length=%d\n",
                event.log_time_ms(), event.packet_number, event.packet_length);
      };

  auto generic_packet_sent_handler = [&](const LoggedGenericPacketSent& event) {
    fprintf(output,
            "GENERIC_PACKET_SENT %" PRId64 " packet_no=%" PRId64
            " overhead_length=%zu "
            "payload_length=%zu padding_length=%zu\n",
            event.log_time_ms(), event.packet_number, event.overhead_length,
            event.payload_length, event.padding_length);
  };

  auto generic_ack_received_handler =
      [&](const LoggedGenericAckReceived& event) {
        fprintf(output, "GENERIC_ACK_RECV %" PRId64 " <contents omitted>\n",
                event.log_time_ms());
      };

  auto decoded_frame_handler = [&](const LoggedFrameDecoded& event) {
    static const std::map<VideoCodecType, std::string> codec_name{
        {VideoCodecType::kVideoCodecGeneric, "GENERIC"},
        {VideoCodecType::kVideoCodecVP8, "VP8"},
        {VideoCodecType::kVideoCodecVP9, "VP9"},
        {VideoCodecType::kVideoCodecAV1, "AV1"},
        {VideoCodecType::kVideoCodecH264, "H264"},
        {VideoCodecType::kVideoCodecH265, "H265"}};

    fprintf(output,
            "FRAME_DECODED %" PRId64 " render_time=%" PRId64
            " ssrc=%u width=%d height=%d "
            "codec=%s qp=%u\n",
            event.log_time_ms(), event.render_time_ms, event.ssrc, event.width,
            event.height, codec_name.at(event.codec).c_str(), event.qp);
  };

  RtcEventProcessor processor;

  // Stream configs
  processor.AddEvents(parsed_log.audio_recv_configs(),
                      audio_recv_stream_handler);
  processor.AddEvents(parsed_log.audio_send_configs(),
                      audio_send_stream_handler);
  processor.AddEvents(parsed_log.video_recv_configs(),
                      video_recv_stream_handler);
  processor.AddEvents(parsed_log.video_send_configs(),
                      video_send_stream_handler);

  // Start and stop
  processor.AddEvents(parsed_log.start_log_events(), start_logging_handler);
  processor.AddEvents(parsed_log.stop_log_events(), stop_logging_handler);

  // Audio
  for (const auto& kv : parsed_log.audio_playout_events()) {
    processor.AddEvents(kv.second, audio_playout_handler);
  }

  for (const auto& kv : parsed_log.neteq_set_minimum_delay_events()) {
    processor.AddEvents(kv.second, neteq_set_minimum_delay_handler);
  }

  processor.AddEvents(parsed_log.audio_network_adaptation_events(),
                      audio_network_adaptation_handler);

  // Bandwidth estimation and pacing
  processor.AddEvents(parsed_log.alr_state_events(), alr_state_handler);
  processor.AddEvents(parsed_log.bwe_probe_cluster_created_events(),
                      bwe_probe_cluster_created_handler);
  processor.AddEvents(parsed_log.bwe_probe_failure_events(),
                      bwe_probe_failure_handler);
  processor.AddEvents(parsed_log.bwe_probe_success_events(),
                      bwe_probe_success_handler);
  processor.AddEvents(parsed_log.bwe_delay_updates(), bwe_delay_update_handler);
  processor.AddEvents(parsed_log.bwe_loss_updates(), bwe_loss_update_handler);
  processor.AddEvents(parsed_log.remote_estimate_events(),
                      remote_estimate_handler);

  // Connectivity
  processor.AddEvents(parsed_log.dtls_transport_states(),
                      dtls_transport_state_handler);
  processor.AddEvents(parsed_log.dtls_writable_states(),
                      dtls_transport_writable_handler);
  processor.AddEvents(parsed_log.ice_candidate_pair_configs(),
                      ice_candidate_pair_config_handler);
  processor.AddEvents(parsed_log.ice_candidate_pair_events(),
                      ice_candidate_pair_event_handler);
  processor.AddEvents(parsed_log.route_change_events(), route_change_handler);

  // RTP
  for (const auto& stream : parsed_log.incoming_rtp_packets_by_ssrc()) {
    processor.AddEvents(stream.incoming_packets, incoming_rtp_packet_handler);
  }
  for (const auto& stream : parsed_log.outgoing_rtp_packets_by_ssrc()) {
    processor.AddEvents(stream.outgoing_packets, outgoing_rtp_packet_handler);
  }

  // RTCP
  processor.AddEvents(parsed_log.incoming_rtcp_packets(),
                      incoming_rtcp_packet_handler);
  processor.AddEvents(parsed_log.outgoing_rtcp_packets(),
                      outgoing_rtcp_packet_handler);

  // Generic packets
  processor.AddEvents(parsed_log.generic_packets_received(),
                      generic_packet_received_handler);
  processor.AddEvents(parsed_log.generic_packets_sent(),
                      generic_packet_sent_handler);
  processor.AddEvents(parsed_log.generic_acks_received(),
                      generic_ack_received_handler);

  // Video frames
  for (const auto& kv : parsed_log.decoded_frames()) {
    processor.AddEvents(kv.second, decoded_frame_handler);
  }

  processor.ProcessEventsInOrder();

  return true;
}

}  // namespace webrtc
