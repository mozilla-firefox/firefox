/*
 *  Copyright 2016 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/rtc_stats_collector.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "api/audio/audio_device.h"
#include "api/audio/audio_processing_statistics.h"
#include "api/candidate.h"
#include "api/data_channel_interface.h"
#include "api/dtls_transport_interface.h"
#include "api/environment/environment.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_direction.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/stats/rtc_stats.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/transport/enums.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/video_content_type.h"
#include "api/video_codecs/scalability_mode.h"
#include "call/call.h"
#include "common_video/include/quality_limitation_reason.h"
#include "media/base/media_channel.h"
#include "media/base/stream_params.h"
#include "modules/rtp_rtcp/include/report_block_data.h"
#include "p2p/base/connection_info.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port.h"
#include "p2p/base/transport_description.h"
#include "pc/channel_interface.h"
#include "pc/data_channel_utils.h"
#include "pc/peer_connection_internal.h"
#include "pc/rtc_stats_traversal.h"
#include "pc/rtp_receiver_proxy.h"
#include "pc/rtp_sender_proxy.h"
#include "pc/rtp_transceiver.h"
#include "pc/scoped_operations_batcher.h"
#include "pc/track_media_info_map.h"
#include "pc/transport_stats.h"
#include "rtc_base/checks.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/network_constants.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/ssl_certificate.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/system/plan_b_only.h"
#include "rtc_base/thread.h"
#include "rtc_base/time_utils.h"
#include "rtc_base/trace_event.h"

namespace webrtc {

namespace {

constexpr char kDirectionInbound = 'I';
constexpr char kDirectionOutbound = 'O';

constexpr char kAudioPlayoutSingletonId[] = "AP";

// TODO(https://crbug.com/webrtc/10656): Consider making IDs less predictable.
std::string RTCCertificateIDFromFingerprint(const std::string& fingerprint) {
  return "CF" + fingerprint;
}

// `direction` is either kDirectionInbound or kDirectionOutbound.
std::string RTCCodecStatsIDFromTransportAndCodecParameters(
    const char direction,
    const std::string& transport_id,
    const RtpCodecParameters& codec_params) {
  StringBuilder sb;
  sb << 'C' << direction << transport_id << '_' << codec_params.payload_type;
  // TODO(https://crbug.com/webrtc/14420): If we stop supporting different FMTP
  // lines for the same PT and transport, which should be illegal SDP, then we
  // wouldn't need `fmtp` to be part of the ID here.
  StringBuilder fmtp;
  if (WriteFmtpParameters(codec_params.parameters, fmtp)) {
    sb << '_' << fmtp.Release();
  }
  return sb.Release();
}

std::string RTCIceCandidatePairStatsIDFromConnectionInfo(
    const ConnectionInfo& info) {
  StringBuilder sb;
  sb << "CP" << info.local_candidate.id() << "_" << info.remote_candidate.id();
  return sb.Release();
}

std::string RTCTransportStatsIDFromTransportChannel(
    const std::string& transport_name,
    int channel_component) {
  StringBuilder sb;
  sb << 'T' << transport_name << channel_component;
  return sb.Release();
}

std::string RTCInboundRtpStreamStatsIDFromSSRC(const std::string& transport_id,
                                               MediaType media_type,
                                               uint32_t ssrc) {
  StringBuilder sb;
  sb << 'I' << transport_id << (media_type == MediaType::AUDIO ? 'A' : 'V')
     << ssrc;
  return sb.Release();
}

std::string RTCOutboundRtpStreamStatsIDFromSSRC(const std::string& transport_id,
                                                MediaType media_type,
                                                uint32_t ssrc) {
  StringBuilder sb;
  sb << 'O' << transport_id << (media_type == MediaType::AUDIO ? 'A' : 'V')
     << ssrc;
  return sb.Release();
}

std::string RTCRemoteInboundRtpStreamStatsIdFromSourceSsrc(
    MediaType media_type,
    uint32_t source_ssrc) {
  StringBuilder sb;
  sb << "RI" << (media_type == MediaType::AUDIO ? 'A' : 'V') << source_ssrc;
  return sb.Release();
}

std::string RTCRemoteOutboundRTPStreamStatsIDFromSSRC(MediaType media_type,
                                                      uint32_t source_ssrc) {
  StringBuilder sb;
  sb << "RO" << (media_type == MediaType::AUDIO ? 'A' : 'V') << source_ssrc;
  return sb.Release();
}

std::string RTCMediaSourceStatsIDFromKindAndAttachment(MediaType media_type,
                                                       int attachment_id) {
  StringBuilder sb;
  sb << 'S' << (media_type == MediaType::AUDIO ? 'A' : 'V') << attachment_id;
  return sb.Release();
}

const char* DataStateToRTCDataChannelState(
    DataChannelInterface::DataState state) {
  switch (state) {
    case DataChannelInterface::kConnecting:
      return "connecting";
    case DataChannelInterface::kOpen:
      return "open";
    case DataChannelInterface::kClosing:
      return "closing";
    case DataChannelInterface::kClosed:
      return "closed";
    default:
      RTC_DCHECK_NOTREACHED();
      return nullptr;
  }
}

const char* IceCandidatePairStateToRTCStatsIceCandidatePairState(
    IceCandidatePairState state) {
  switch (state) {
    case IceCandidatePairState::WAITING:
      return "waiting";
    case IceCandidatePairState::IN_PROGRESS:
      return "in-progress";
    case IceCandidatePairState::SUCCEEDED:
      return "succeeded";
    case IceCandidatePairState::FAILED:
      return "failed";
    default:
      RTC_DCHECK_NOTREACHED();
      return nullptr;
  }
}

const char* IceRoleToRTCIceRole(IceRole role) {
  switch (role) {
    case IceRole::ICEROLE_UNKNOWN:
      return "unknown";
    case IceRole::ICEROLE_CONTROLLED:
      return "controlled";
    case IceRole::ICEROLE_CONTROLLING:
      return "controlling";
    default:
      RTC_DCHECK_NOTREACHED();
      return nullptr;
  }
}

const char* DtlsTransportStateToRTCDtlsTransportState(
    DtlsTransportState state) {
  switch (state) {
    case DtlsTransportState::kNew:
      return "new";
    case DtlsTransportState::kConnecting:
      return "connecting";
    case DtlsTransportState::kConnected:
      return "connected";
    case DtlsTransportState::kClosed:
      return "closed";
    case DtlsTransportState::kFailed:
      return "failed";
    default:
      RTC_CHECK_NOTREACHED();
      return nullptr;
  }
}

const char* IceTransportStateToRTCIceTransportState(IceTransportState state) {
  switch (state) {
    case IceTransportState::kNew:
      return "new";
    case IceTransportState::kChecking:
      return "checking";
    case IceTransportState::kConnected:
      return "connected";
    case IceTransportState::kCompleted:
      return "completed";
    case IceTransportState::kFailed:
      return "failed";
    case IceTransportState::kDisconnected:
      return "disconnected";
    case IceTransportState::kClosed:
      return "closed";
    default:
      RTC_CHECK_NOTREACHED();
      return nullptr;
  }
}

const char* NetworkTypeToStatsType(AdapterType type) {
  switch (type) {
    case ADAPTER_TYPE_CELLULAR:
    case ADAPTER_TYPE_CELLULAR_2G:
    case ADAPTER_TYPE_CELLULAR_3G:
    case ADAPTER_TYPE_CELLULAR_4G:
    case ADAPTER_TYPE_CELLULAR_5G:
      return "cellular";
    case ADAPTER_TYPE_ETHERNET:
      return "ethernet";
    case ADAPTER_TYPE_WIFI:
      return "wifi";
    case ADAPTER_TYPE_VPN:
      return "vpn";
    case ADAPTER_TYPE_UNKNOWN:
    case ADAPTER_TYPE_LOOPBACK:
    case ADAPTER_TYPE_ANY:
      return "unknown";
  }
  RTC_DCHECK_NOTREACHED();
  return nullptr;
}

absl::string_view NetworkTypeToStatsNetworkAdapterType(AdapterType type) {
  switch (type) {
    case ADAPTER_TYPE_CELLULAR:
      return "cellular";
    case ADAPTER_TYPE_CELLULAR_2G:
      return "cellular2g";
    case ADAPTER_TYPE_CELLULAR_3G:
      return "cellular3g";
    case ADAPTER_TYPE_CELLULAR_4G:
      return "cellular4g";
    case ADAPTER_TYPE_CELLULAR_5G:
      return "cellular5g";
    case ADAPTER_TYPE_ETHERNET:
      return "ethernet";
    case ADAPTER_TYPE_WIFI:
      return "wifi";
    case ADAPTER_TYPE_UNKNOWN:
      return "unknown";
    case ADAPTER_TYPE_LOOPBACK:
      return "loopback";
    case ADAPTER_TYPE_ANY:
      return "any";
    case ADAPTER_TYPE_VPN:
      /* should not be handled here. Vpn is modelled as a bool */
      break;
  }
  RTC_DCHECK_NOTREACHED();
  return {};
}

absl::string_view NetworkSliceToStatsNetworkSlice(NetworkSlice slice) {
  switch (slice) {
    case NetworkSlice::UNIFIED_COMMUNICATIONS:
      return "unified-communications";
    case NetworkSlice::NO_SLICE:
      /* Should not be handled here. Report empty slice in stats instead. */
      break;
  }
  RTC_DCHECK_NOTREACHED();
  return {};
}

const char* QualityLimitationReasonToRTCQualityLimitationReason(
    QualityLimitationReason reason) {
  switch (reason) {
    case QualityLimitationReason::kNone:
      return "none";
    case QualityLimitationReason::kCpu:
      return "cpu";
    case QualityLimitationReason::kBandwidth:
      return "bandwidth";
    case QualityLimitationReason::kOther:
      return "other";
  }
  RTC_CHECK_NOTREACHED();
}

std::map<std::string, double>
QualityLimitationDurationToRTCQualityLimitationDuration(
    std::map<QualityLimitationReason, int64_t> durations_ms) {
  std::map<std::string, double> result;
  // The internal duration is defined in milliseconds while the spec defines
  // the value in seconds:
  // https://w3c.github.io/webrtc-stats/#dom-rtcoutboundrtpstreamstats-qualitylimitationdurations
  for (const auto& elem : durations_ms) {
    result[QualityLimitationReasonToRTCQualityLimitationReason(elem.first)] =
        elem.second / static_cast<double>(kNumMillisecsPerSec);
  }
  return result;
}

double DoubleAudioLevelFromIntAudioLevel(int audio_level) {
  RTC_DCHECK_GE(audio_level, 0);
  RTC_DCHECK_LE(audio_level, 32767);
  return audio_level / 32767.0;
}

// Gets the `codecId` identified by `transport_id` and `codec_params`. If no
// such `RTCCodecStats` exist yet, create it and add it to `report`.
std::string GetCodecIdAndMaybeCreateCodecStats(
    Timestamp timestamp,
    const char direction,
    const std::string& transport_id,
    const RtpCodecParameters& codec_params,
    RTCStatsReport* report) {
  RTC_DCHECK_GE(codec_params.payload_type, 0);
  RTC_DCHECK_LE(codec_params.payload_type, 127);
  RTC_DCHECK(codec_params.clock_rate);
  uint32_t payload_type = static_cast<uint32_t>(codec_params.payload_type);
  std::string codec_id = RTCCodecStatsIDFromTransportAndCodecParameters(
      direction, transport_id, codec_params);
  if (report->Get(codec_id) != nullptr) {
    // The `RTCCodecStats` already exists.
    return codec_id;
  }
  // Create the `RTCCodecStats` that we want to reference.
  auto codec_stats = std::make_unique<RTCCodecStats>(codec_id, timestamp);
  codec_stats->payload_type = payload_type;
  codec_stats->mime_type = codec_params.mime_type();
  if (codec_params.clock_rate.has_value()) {
    codec_stats->clock_rate = static_cast<uint32_t>(*codec_params.clock_rate);
  }
  if (codec_params.num_channels) {
    codec_stats->channels = *codec_params.num_channels;
  }

  StringBuilder fmtp;
  if (WriteFmtpParameters(codec_params.parameters, fmtp)) {
    codec_stats->sdp_fmtp_line = fmtp.Release();
  }
  codec_stats->transport_id = transport_id;
  report->AddStats(std::move(codec_stats));
  return codec_id;
}

// Provides the media independent counters (both audio and video).
void SetInboundRTPStreamStatsFromMediaReceiverInfo(
    const MediaReceiverInfo& media_receiver_info,
    RTCInboundRtpStreamStats* inbound_stats) {
  RTC_DCHECK(inbound_stats);
  inbound_stats->ssrc = media_receiver_info.ssrc();
  inbound_stats->packets_received =
      static_cast<uint32_t>(media_receiver_info.packets_received);
  inbound_stats->packets_received_with_ect1 =
      media_receiver_info.packets_received_with_ect1;
  inbound_stats->packets_received_with_ce =
      media_receiver_info.packets_received_with_ce;
  inbound_stats->bytes_received =
      static_cast<uint64_t>(media_receiver_info.payload_bytes_received);
  inbound_stats->header_bytes_received = static_cast<uint64_t>(
      media_receiver_info.header_and_padding_bytes_received);
  if (media_receiver_info.retransmitted_bytes_received.has_value()) {
    inbound_stats->retransmitted_bytes_received =
        *media_receiver_info.retransmitted_bytes_received;
  }
  if (media_receiver_info.retransmitted_packets_received.has_value()) {
    inbound_stats->retransmitted_packets_received =
        *media_receiver_info.retransmitted_packets_received;
  }
  inbound_stats->packets_lost =
      static_cast<int32_t>(media_receiver_info.packets_lost);
  inbound_stats->jitter_buffer_delay =
      media_receiver_info.jitter_buffer_delay_seconds;
  inbound_stats->jitter_buffer_target_delay =
      media_receiver_info.jitter_buffer_target_delay_seconds;
  inbound_stats->jitter_buffer_minimum_delay =
      media_receiver_info.jitter_buffer_minimum_delay_seconds;
  inbound_stats->jitter_buffer_emitted_count =
      media_receiver_info.jitter_buffer_emitted_count;
  if (media_receiver_info.nacks_sent.has_value()) {
    inbound_stats->nack_count = *media_receiver_info.nacks_sent;
  }
  if (media_receiver_info.fec_packets_received.has_value()) {
    inbound_stats->fec_packets_received =
        *media_receiver_info.fec_packets_received;
  }
  if (media_receiver_info.fec_packets_discarded.has_value()) {
    inbound_stats->fec_packets_discarded =
        *media_receiver_info.fec_packets_discarded;
  }
  if (media_receiver_info.fec_bytes_received.has_value()) {
    inbound_stats->fec_bytes_received = *media_receiver_info.fec_bytes_received;
  }
  inbound_stats->total_processing_delay =
      media_receiver_info.total_processing_delay_seconds;
}

void AppendCallStats(const Call::Stats& call_stats,
                     RTCInboundRtpStreamStats& rtp_stats) {
  if (!rtp_stats.ssrc.has_value()) {
    return;
  }
  auto it = call_stats.sent_ccfb_stats_per_ssrc.find(*rtp_stats.ssrc);
  if (it == call_stats.sent_ccfb_stats_per_ssrc.end()) {
    return;
  }
  rtp_stats.packets_reported_as_lost = it->second.num_packets_reported_lost;
  rtp_stats.packets_reported_as_lost_but_recovered =
      it->second.num_packets_reported_recovered;
}

std::unique_ptr<RTCInboundRtpStreamStats> CreateInboundAudioStreamStats(
    const VoiceMediaInfo& voice_media_info,
    const VoiceReceiverInfo& voice_receiver_info,
    const std::string& transport_id,
    const std::string& mid,
    Timestamp timestamp,
    RTCStatsReport* report) {
  auto inbound_audio = std::make_unique<RTCInboundRtpStreamStats>(
      /*id=*/RTCInboundRtpStreamStatsIDFromSSRC(transport_id, MediaType::AUDIO,
                                                voice_receiver_info.ssrc()),
      timestamp);
  SetInboundRTPStreamStatsFromMediaReceiverInfo(voice_receiver_info,
                                                inbound_audio.get());
  inbound_audio->transport_id = transport_id;
  inbound_audio->mid = mid;
  inbound_audio->kind = "audio";
  if (voice_receiver_info.codec_payload_type.has_value()) {
    auto codec_param_it = voice_media_info.receive_codecs.find(
        *voice_receiver_info.codec_payload_type);
    RTC_DCHECK(codec_param_it != voice_media_info.receive_codecs.end());
    if (codec_param_it != voice_media_info.receive_codecs.end()) {
      inbound_audio->codec_id = GetCodecIdAndMaybeCreateCodecStats(
          inbound_audio->timestamp(), kDirectionInbound, transport_id,
          codec_param_it->second, report);
    }
  }
  inbound_audio->jitter =
      static_cast<double>(voice_receiver_info.jitter_ms) / kNumMillisecsPerSec;
  inbound_audio->total_samples_received =
      voice_receiver_info.total_samples_received;
  inbound_audio->concealed_samples = voice_receiver_info.concealed_samples;
  inbound_audio->silent_concealed_samples =
      voice_receiver_info.silent_concealed_samples;
  inbound_audio->concealment_events = voice_receiver_info.concealment_events;
  inbound_audio->inserted_samples_for_deceleration =
      voice_receiver_info.inserted_samples_for_deceleration;
  inbound_audio->removed_samples_for_acceleration =
      voice_receiver_info.removed_samples_for_acceleration;
  if (voice_receiver_info.audio_level >= 0) {
    inbound_audio->audio_level =
        DoubleAudioLevelFromIntAudioLevel(voice_receiver_info.audio_level);
  }
  inbound_audio->total_audio_energy = voice_receiver_info.total_output_energy;
  inbound_audio->total_samples_duration =
      voice_receiver_info.total_output_duration;
  // `fir_count` and `pli_count` are only valid for video and are
  // purposefully left undefined for audio.
  if (voice_receiver_info.last_packet_received.has_value()) {
    inbound_audio->last_packet_received_timestamp =
        voice_receiver_info.last_packet_received->ms<double>();
  }
  if (voice_receiver_info.estimated_playout_ntp_timestamp_ms.has_value()) {
    // TODO(bugs.webrtc.org/10529): Fix time origin.
    inbound_audio->estimated_playout_timestamp = static_cast<double>(
        *voice_receiver_info.estimated_playout_ntp_timestamp_ms);
  }
  inbound_audio->packets_discarded = voice_receiver_info.packets_discarded;
  inbound_audio->jitter_buffer_flushes =
      voice_receiver_info.jitter_buffer_flushes;
  inbound_audio->delayed_packet_outage_samples =
      voice_receiver_info.delayed_packet_outage_samples;
  inbound_audio->relative_packet_arrival_delay =
      voice_receiver_info.relative_packet_arrival_delay_seconds;
  inbound_audio->interruption_count =
      voice_receiver_info.interruption_count >= 0
          ? voice_receiver_info.interruption_count
          : 0;
  inbound_audio->total_interruption_duration =
      static_cast<double>(voice_receiver_info.total_interruption_duration_ms) /
      kNumMillisecsPerSec;
  return inbound_audio;
}

std::unique_ptr<RTCAudioPlayoutStats> CreateAudioPlayoutStats(
    const AudioDeviceModule::Stats& audio_device_stats,
    Timestamp timestamp) {
  auto stats = std::make_unique<RTCAudioPlayoutStats>(
      /*id=*/kAudioPlayoutSingletonId, timestamp);
  stats->synthesized_samples_duration =
      audio_device_stats.synthesized_samples_duration_s;
  stats->synthesized_samples_events =
      audio_device_stats.synthesized_samples_events;
  stats->total_samples_count = audio_device_stats.total_samples_count;
  stats->total_samples_duration = audio_device_stats.total_samples_duration_s;
  stats->total_playout_delay = audio_device_stats.total_playout_delay_s;
  return stats;
}

std::unique_ptr<RTCRemoteOutboundRtpStreamStats>
CreateRemoteOutboundMediaStreamStats(
    const MediaReceiverInfo& media_receiver_info,
    const std::string& mid,
    MediaType media_type,
    const RTCInboundRtpStreamStats& inbound_audio_stats,
    const std::string& transport_id,
    const bool stats_timestamp_with_environment_clock) {
  std::optional<Timestamp> last_sender_report_timestamp =
      stats_timestamp_with_environment_clock
          ? media_receiver_info.last_sender_report_timestamp
          : media_receiver_info.last_sender_report_utc_timestamp;
  if (!last_sender_report_timestamp.has_value()) {
    // Cannot create `RTCRemoteOutboundRtpStreamStats` when the RTCP SR arrival
    // timestamp is not available - i.e., until the first sender report is
    // received.
    return nullptr;
  }
  RTC_DCHECK_GT(media_receiver_info.sender_reports_reports_count, 0);

  // Create.
  auto stats = std::make_unique<RTCRemoteOutboundRtpStreamStats>(
      /*id=*/RTCRemoteOutboundRTPStreamStatsIDFromSSRC(
          media_type, media_receiver_info.ssrc()),
      *last_sender_report_timestamp);

  // Populate.
  // - RTCRtpStreamStats.
  stats->ssrc = media_receiver_info.ssrc();
  stats->kind = MediaTypeToString(media_type);
  stats->transport_id = transport_id;
  if (inbound_audio_stats.codec_id.has_value()) {
    stats->codec_id = *inbound_audio_stats.codec_id;
  }
  // - RTCSentRtpStreamStats.
  stats->packets_sent = media_receiver_info.sender_reports_packets_sent;
  stats->bytes_sent = media_receiver_info.sender_reports_bytes_sent;
  // - RTCRemoteOutboundRtpStreamStats.
  stats->local_id = inbound_audio_stats.id();
  // last_sender_report_remote_utc_timestamp_ms is set together with
  // last_sender_report_utc_timestamp_ms.
  RTC_DCHECK(
      media_receiver_info.last_sender_report_remote_utc_timestamp.has_value());
  stats->remote_timestamp =
      media_receiver_info.last_sender_report_remote_utc_timestamp->ms<double>();
  stats->reports_sent = media_receiver_info.sender_reports_reports_count;
  if (media_receiver_info.round_trip_time.has_value()) {
    stats->round_trip_time =
        media_receiver_info.round_trip_time->seconds<double>();
  }
  stats->round_trip_time_measurements =
      media_receiver_info.round_trip_time_measurements;
  stats->total_round_trip_time =
      media_receiver_info.total_round_trip_time.seconds<double>();

  return stats;
}

std::unique_ptr<RTCInboundRtpStreamStats>
CreateInboundRTPStreamStatsFromVideoReceiverInfo(
    const std::string& transport_id,
    const std::string& mid,
    const VideoMediaInfo& video_media_info,
    const VideoReceiverInfo& video_receiver_info,
    Timestamp timestamp,
    RTCStatsReport* report) {
  auto inbound_video = std::make_unique<RTCInboundRtpStreamStats>(
      RTCInboundRtpStreamStatsIDFromSSRC(transport_id, MediaType::VIDEO,
                                         video_receiver_info.ssrc()),
      timestamp);
  SetInboundRTPStreamStatsFromMediaReceiverInfo(video_receiver_info,
                                                inbound_video.get());
  inbound_video->transport_id = transport_id;
  inbound_video->mid = mid;
  inbound_video->kind = "video";
  if (video_receiver_info.codec_payload_type.has_value()) {
    auto codec_param_it = video_media_info.receive_codecs.find(
        *video_receiver_info.codec_payload_type);
    RTC_DCHECK(codec_param_it != video_media_info.receive_codecs.end());
    if (codec_param_it != video_media_info.receive_codecs.end()) {
      inbound_video->codec_id = GetCodecIdAndMaybeCreateCodecStats(
          inbound_video->timestamp(), kDirectionInbound, transport_id,
          codec_param_it->second, report);
    }
  }
  inbound_video->jitter =
      static_cast<double>(video_receiver_info.jitter_ms) / kNumMillisecsPerSec;
  inbound_video->fir_count =
      static_cast<uint32_t>(video_receiver_info.firs_sent);
  inbound_video->pli_count =
      static_cast<uint32_t>(video_receiver_info.plis_sent);
  inbound_video->frames_received = video_receiver_info.frames_received;
  inbound_video->frames_decoded = video_receiver_info.frames_decoded;
  inbound_video->frames_dropped = video_receiver_info.frames_dropped;
  inbound_video->key_frames_decoded = video_receiver_info.key_frames_decoded;
  if (video_receiver_info.frame_width > 0) {
    inbound_video->frame_width =
        static_cast<uint32_t>(video_receiver_info.frame_width);
  }
  if (video_receiver_info.frame_height > 0) {
    inbound_video->frame_height =
        static_cast<uint32_t>(video_receiver_info.frame_height);
  }
  if (video_receiver_info.framerate_decoded > 0) {
    inbound_video->frames_per_second = video_receiver_info.framerate_decoded;
  }
  if (video_receiver_info.qp_sum.has_value()) {
    inbound_video->qp_sum = *video_receiver_info.qp_sum;
  }
  if (video_receiver_info.corruption_score_sum.has_value()) {
    RTC_CHECK(video_receiver_info.corruption_score_squared_sum.has_value());
    RTC_CHECK_GT(video_receiver_info.corruption_score_count, 0);
    inbound_video->total_corruption_probability =
        *video_receiver_info.corruption_score_sum;
    inbound_video->total_squared_corruption_probability =
        *video_receiver_info.corruption_score_squared_sum;
    inbound_video->corruption_measurements =
        video_receiver_info.corruption_score_count;
  }
  if (video_receiver_info.timing_frame_info.has_value()) {
    inbound_video->goog_timing_frame_info =
        video_receiver_info.timing_frame_info->ToString();
  }
  inbound_video->total_decode_time =
      video_receiver_info.total_decode_time.seconds<double>();
  inbound_video->total_processing_delay =
      video_receiver_info.total_processing_delay.seconds<double>();
  inbound_video->total_assembly_time =
      video_receiver_info.total_assembly_time.seconds<double>();
  inbound_video->frames_assembled_from_multiple_packets =
      video_receiver_info.frames_assembled_from_multiple_packets;
  inbound_video->total_inter_frame_delay =
      video_receiver_info.total_inter_frame_delay;
  inbound_video->total_squared_inter_frame_delay =
      video_receiver_info.total_squared_inter_frame_delay;
  inbound_video->pause_count = video_receiver_info.pause_count;
  inbound_video->total_pauses_duration =
      static_cast<double>(video_receiver_info.total_pauses_duration_ms) /
      kNumMillisecsPerSec;
  inbound_video->freeze_count = video_receiver_info.freeze_count;
  inbound_video->total_freezes_duration =
      static_cast<double>(video_receiver_info.total_freezes_duration_ms) /
      kNumMillisecsPerSec;
  inbound_video->min_playout_delay =
      static_cast<double>(video_receiver_info.min_playout_delay_ms) /
      kNumMillisecsPerSec;
  if (video_receiver_info.last_packet_received.has_value()) {
    inbound_video->last_packet_received_timestamp =
        video_receiver_info.last_packet_received->ms<double>();
  }
  if (video_receiver_info.estimated_playout_ntp_timestamp_ms.has_value()) {
    // TODO(bugs.webrtc.org/10529): Fix time origin if needed.
    inbound_video->estimated_playout_timestamp = static_cast<double>(
        *video_receiver_info.estimated_playout_ntp_timestamp_ms);
  }
  // TODO(bugs.webrtc.org/10529): When info's `content_info` is optional
  // support the "unspecified" value.
  if (videocontenttypehelpers::IsScreenshare(video_receiver_info.content_type))
    inbound_video->content_type = "screenshare";
  if (video_receiver_info.decoder_implementation_name.has_value()) {
    inbound_video->decoder_implementation =
        *video_receiver_info.decoder_implementation_name;
  }
  if (video_receiver_info.power_efficient_decoder.has_value()) {
    inbound_video->power_efficient_decoder =
        *video_receiver_info.power_efficient_decoder;
  }
  for (const auto& ssrc_group : video_receiver_info.ssrc_groups) {
    if (ssrc_group.semantics == kFidSsrcGroupSemantics &&
        ssrc_group.ssrcs.size() == 2) {
      inbound_video->rtx_ssrc = ssrc_group.ssrcs[1];
    } else if (ssrc_group.semantics == kFecFrSsrcGroupSemantics &&
               ssrc_group.ssrcs.size() == 2) {
      // TODO(bugs.webrtc.org/15002): the ssrc-group might be >= 2 with
      // multistream support.
      inbound_video->fec_ssrc = ssrc_group.ssrcs[1];
    }
  }

  return inbound_video;
}

// Provides the media independent counters and information (both audio and
// video).
void SetOutboundRTPStreamStatsFromMediaSenderInfo(
    const MediaSenderInfo& media_sender_info,
    RTCOutboundRtpStreamStats* outbound_stats) {
  RTC_DCHECK(outbound_stats);
  outbound_stats->ssrc = media_sender_info.ssrc();
  outbound_stats->packets_sent =
      static_cast<uint32_t>(media_sender_info.packets_sent);
  outbound_stats->total_packet_send_delay =
      media_sender_info.total_packet_send_delay.seconds<double>();
  outbound_stats->retransmitted_packets_sent =
      media_sender_info.retransmitted_packets_sent;
  outbound_stats->bytes_sent =
      static_cast<uint64_t>(media_sender_info.payload_bytes_sent);
  outbound_stats->header_bytes_sent =
      static_cast<uint64_t>(media_sender_info.header_and_padding_bytes_sent);
  outbound_stats->retransmitted_bytes_sent =
      media_sender_info.retransmitted_bytes_sent;
  outbound_stats->nack_count = media_sender_info.nacks_received;
  if (media_sender_info.active.has_value()) {
    outbound_stats->active = *media_sender_info.active;
  }
  outbound_stats->packets_sent_with_ect1 =
      media_sender_info.packets_sent_with_ect1;
}

std::unique_ptr<RTCOutboundRtpStreamStats>
CreateOutboundRTPStreamStatsFromVoiceSenderInfo(
    const std::string& transport_id,
    const std::string& mid,
    const VoiceMediaInfo& voice_media_info,
    const VoiceSenderInfo& voice_sender_info,
    Timestamp timestamp,
    RTCStatsReport* report) {
  auto outbound_audio = std::make_unique<RTCOutboundRtpStreamStats>(
      RTCOutboundRtpStreamStatsIDFromSSRC(transport_id, MediaType::AUDIO,
                                          voice_sender_info.ssrc()),
      timestamp);
  SetOutboundRTPStreamStatsFromMediaSenderInfo(voice_sender_info,
                                               outbound_audio.get());
  outbound_audio->transport_id = transport_id;
  outbound_audio->mid = mid;
  outbound_audio->kind = "audio";
  if (voice_sender_info.target_bitrate.has_value()) {
    outbound_audio->target_bitrate = voice_sender_info.target_bitrate->bps();
  }
  if (voice_sender_info.codec_payload_type.has_value()) {
    auto codec_param_it = voice_media_info.send_codecs.find(
        *voice_sender_info.codec_payload_type);
    RTC_DCHECK(codec_param_it != voice_media_info.send_codecs.end());
    if (codec_param_it != voice_media_info.send_codecs.end()) {
      outbound_audio->codec_id = GetCodecIdAndMaybeCreateCodecStats(
          outbound_audio->timestamp(), kDirectionOutbound, transport_id,
          codec_param_it->second, report);
    }
  }
  // `fir_count` and `pli_count` are only valid for video and are
  // purposefully left undefined for audio.
  return outbound_audio;
}

std::unique_ptr<RTCOutboundRtpStreamStats>
CreateOutboundRTPStreamStatsFromVideoSenderInfo(
    const std::string& transport_id,
    const std::string& mid,
    const VideoMediaInfo& video_media_info,
    const VideoSenderInfo& video_sender_info,
    Timestamp timestamp,
    RTCStatsReport* report) {
  auto outbound_video = std::make_unique<RTCOutboundRtpStreamStats>(
      RTCOutboundRtpStreamStatsIDFromSSRC(transport_id, MediaType::VIDEO,
                                          video_sender_info.ssrc()),
      timestamp);
  SetOutboundRTPStreamStatsFromMediaSenderInfo(video_sender_info,
                                               outbound_video.get());
  outbound_video->transport_id = transport_id;
  outbound_video->mid = mid;
  outbound_video->kind = "video";
  if (video_sender_info.codec_payload_type.has_value()) {
    auto codec_param_it = video_media_info.send_codecs.find(
        *video_sender_info.codec_payload_type);
    RTC_DCHECK(codec_param_it != video_media_info.send_codecs.end());
    if (codec_param_it != video_media_info.send_codecs.end()) {
      outbound_video->codec_id = GetCodecIdAndMaybeCreateCodecStats(
          outbound_video->timestamp(), kDirectionOutbound, transport_id,
          codec_param_it->second, report);
    }
  }
  outbound_video->fir_count =
      static_cast<uint32_t>(video_sender_info.firs_received);
  outbound_video->pli_count =
      static_cast<uint32_t>(video_sender_info.plis_received);
  if (video_sender_info.qp_sum.has_value()) {
    outbound_video->qp_sum = *video_sender_info.qp_sum;
  }
  // psnrSum is gated on psnrMeasurements > 0.
  if (video_sender_info.psnr_measurements > 0) {
    outbound_video->psnr_measurements = video_sender_info.psnr_measurements;
    outbound_video->psnr_sum = {
        {"y", video_sender_info.psnr_sum.y},
        {"u", video_sender_info.psnr_sum.u},
        {"v", video_sender_info.psnr_sum.v},
    };
  }
  if (video_sender_info.psnr_measurements > 0) {
    outbound_video->psnr_measurements = video_sender_info.psnr_measurements;
  }
  if (video_sender_info.target_bitrate.has_value()) {
    outbound_video->target_bitrate = video_sender_info.target_bitrate->bps();
  }
  outbound_video->frames_encoded = video_sender_info.frames_encoded;
  outbound_video->key_frames_encoded = video_sender_info.key_frames_encoded;
  outbound_video->total_encode_time =
      static_cast<double>(video_sender_info.total_encode_time_ms) /
      kNumMillisecsPerSec;
  if (video_sender_info.send_frame_width > 0) {
    outbound_video->frame_width =
        static_cast<uint32_t>(video_sender_info.send_frame_width);
  }
  if (video_sender_info.send_frame_height > 0) {
    outbound_video->frame_height =
        static_cast<uint32_t>(video_sender_info.send_frame_height);
  }
  if (video_sender_info.framerate_sent > 0) {
    outbound_video->frames_per_second = video_sender_info.framerate_sent;
  }
  outbound_video->frames_sent = video_sender_info.frames_sent;
  outbound_video->huge_frames_sent = video_sender_info.huge_frames_sent;
  outbound_video->quality_limitation_reason =
      QualityLimitationReasonToRTCQualityLimitationReason(
          video_sender_info.quality_limitation_reason);
  outbound_video->quality_limitation_durations =
      QualityLimitationDurationToRTCQualityLimitationDuration(
          video_sender_info.quality_limitation_durations_ms);
  outbound_video->quality_limitation_resolution_changes =
      video_sender_info.quality_limitation_resolution_changes;
  // TODO(https://crbug.com/webrtc/10529): When info's `content_info` is
  // optional, support the "unspecified" value.
  if (videocontenttypehelpers::IsScreenshare(video_sender_info.content_type))
    outbound_video->content_type = "screenshare";
  if (video_sender_info.encoder_implementation_name.has_value()) {
    outbound_video->encoder_implementation =
        *video_sender_info.encoder_implementation_name;
  }
  if (video_sender_info.rid.has_value()) {
    outbound_video->rid = *video_sender_info.rid;
  }
  if (video_sender_info.encoding_index.has_value()) {
    outbound_video->encoding_index = *video_sender_info.encoding_index;
  }
  if (video_sender_info.power_efficient_encoder.has_value()) {
    outbound_video->power_efficient_encoder =
        *video_sender_info.power_efficient_encoder;
  }
  if (video_sender_info.scalability_mode) {
    outbound_video->scalability_mode = std::string(
        ScalabilityModeToString(*video_sender_info.scalability_mode));
  }
  for (const auto& ssrc_group : video_sender_info.ssrc_groups) {
    if (ssrc_group.semantics == kFidSsrcGroupSemantics &&
        ssrc_group.ssrcs.size() == 2 &&
        video_sender_info.ssrc() == ssrc_group.ssrcs[0]) {
      outbound_video->rtx_ssrc = ssrc_group.ssrcs[1];
    }
  }
  return outbound_video;
}

std::unique_ptr<RTCRemoteInboundRtpStreamStats>
ProduceRemoteInboundRtpStreamStats(
    const std::string& transport_id,
    const ReportBlockData& report_block,
    MediaType media_type,
    const std::map<std::string, RTCOutboundRtpStreamStats*>& outbound_rtps,
    const RTCStatsReport& report,
    const Call::Stats& call_stats,
    const bool stats_timestamp_with_environment_clock) {
  // RTCStats' timestamp generally refers to when the metric was sampled, but
  // for "remote-[outbound/inbound]-rtp" it refers to the local time when the
  // Report Block was received.
  Timestamp arrival_timestamp = stats_timestamp_with_environment_clock
                                    ? report_block.report_block_timestamp()
                                    : report_block.report_block_timestamp_utc();
  auto remote_inbound = std::make_unique<RTCRemoteInboundRtpStreamStats>(
      RTCRemoteInboundRtpStreamStatsIdFromSourceSsrc(
          media_type, report_block.source_ssrc()),
      arrival_timestamp);
  uint32_t ssrc = report_block.source_ssrc();
  remote_inbound->ssrc = ssrc;
  remote_inbound->kind = media_type == MediaType::AUDIO ? "audio" : "video";
  remote_inbound->packets_lost = report_block.cumulative_lost();
  remote_inbound->fraction_lost = report_block.fraction_lost();
  if (report_block.num_rtts() > 0) {
    remote_inbound->round_trip_time = report_block.last_rtt().seconds<double>();
  }
  remote_inbound->total_round_trip_time =
      report_block.sum_rtts().seconds<double>();
  remote_inbound->round_trip_time_measurements = report_block.num_rtts();

  if (auto it = call_stats.received_ccfb_stats_per_ssrc.find(ssrc);
      it != call_stats.received_ccfb_stats_per_ssrc.end()) {
    remote_inbound->packets_received_with_ect1 =
        it->second.num_packets_received_with_ect1;
    remote_inbound->packets_received_with_ce =
        it->second.num_packets_received_with_ce;
    remote_inbound->packets_reported_as_lost =
        it->second.num_packets_reported_as_lost;
    remote_inbound->packets_reported_as_lost_but_recovered =
        it->second.num_packets_reported_as_lost_but_recovered;
    remote_inbound->packets_with_bleached_ect1_marking =
        it->second.num_packets_with_bleached_ect1_marking;
  }

  std::string local_id = RTCOutboundRtpStreamStatsIDFromSSRC(
      transport_id, media_type, report_block.source_ssrc());
  // Look up local stat from `outbound_rtps` where the pointers are non-const.
  auto local_id_it = outbound_rtps.find(local_id);
  if (local_id_it != outbound_rtps.end()) {
    remote_inbound->local_id = local_id;
    auto& outbound_rtp = *local_id_it->second;
    outbound_rtp.remote_id = remote_inbound->id();
    // The RTP/RTCP transport is obtained from the
    // RTCOutboundRtpStreamStats's transport.
    const auto* transport_from_id = report.Get(transport_id);
    if (transport_from_id) {
      const auto& transport = transport_from_id->cast_to<RTCTransportStats>();
      // If RTP and RTCP are not multiplexed, there is a separate RTCP
      // transport paired with the RTP transport, otherwise the same
      // transport is used for RTCP and RTP.
      remote_inbound->transport_id =
          transport.rtcp_transport_stats_id.has_value()
              ? *transport.rtcp_transport_stats_id
              : *outbound_rtp.transport_id;
    }
    // We're assuming the same codec is used on both ends. However if the
    // codec is switched out on the fly we may have received a Report Block
    // based on the previous codec and there is no way to tell which point in
    // time the codec changed for the remote end.
    const auto* codec_from_id = outbound_rtp.codec_id.has_value()
                                    ? report.Get(*outbound_rtp.codec_id)
                                    : nullptr;
    if (codec_from_id) {
      remote_inbound->codec_id = *outbound_rtp.codec_id;
      const auto& codec = codec_from_id->cast_to<RTCCodecStats>();
      if (codec.clock_rate.has_value()) {
        remote_inbound->jitter =
            report_block.jitter(*codec.clock_rate).seconds<double>();
      }
    }
  }
  return remote_inbound;
}

void ProduceCertificateStatsFromSSLCertificateStats(
    Timestamp timestamp,
    const SSLCertificateStats& certificate_stats,
    RTCStatsReport* report) {
  RTCCertificateStats* prev_certificate_stats = nullptr;
  for (const SSLCertificateStats* s = &certificate_stats; s;
       s = s->issuer.get()) {
    std::string certificate_stats_id =
        RTCCertificateIDFromFingerprint(s->fingerprint);
    // It is possible for the same certificate to show up multiple times, e.g.
    // if local and remote side use the same certificate in a loopback call.
    // If the report already contains stats for this certificate, skip it.
    if (report->Get(certificate_stats_id)) {
      RTC_DCHECK_EQ(s, &certificate_stats);
      break;
    }
    RTCCertificateStats* current_certificate_stats =
        new RTCCertificateStats(certificate_stats_id, timestamp);
    current_certificate_stats->fingerprint = s->fingerprint;
    current_certificate_stats->fingerprint_algorithm = s->fingerprint_algorithm;
    current_certificate_stats->base64_certificate = s->base64_certificate;
    if (prev_certificate_stats)
      prev_certificate_stats->issuer_certificate_id =
          current_certificate_stats->id();
    report->AddStats(
        std::unique_ptr<RTCCertificateStats>(current_certificate_stats));
    prev_certificate_stats = current_certificate_stats;
  }
}

const std::string& ProduceIceCandidateStats(Timestamp timestamp,
                                            const Candidate& candidate,
                                            bool is_local,
                                            const std::string& transport_id,
                                            RTCStatsReport* report) {
  std::string id = "I" + candidate.id();
  const RTCStats* stats = report->Get(id);
  if (!stats) {
    std::unique_ptr<RTCIceCandidateStats> candidate_stats;
    if (is_local) {
      candidate_stats =
          std::make_unique<RTCLocalIceCandidateStats>(std::move(id), timestamp);
    } else {
      candidate_stats = std::make_unique<RTCRemoteIceCandidateStats>(
          std::move(id), timestamp);
    }
    candidate_stats->transport_id = transport_id;
    if (is_local) {
      candidate_stats->network_type =
          NetworkTypeToStatsType(candidate.network_type());
      const std::string& relay_protocol = candidate.relay_protocol();
      const std::string& url = candidate.url();
      if (candidate.is_relay() ||
          (candidate.is_prflx() && !relay_protocol.empty())) {
        RTC_DCHECK(relay_protocol.compare("udp") == 0 ||
                   relay_protocol.compare("tcp") == 0 ||
                   relay_protocol.compare("tls") == 0);
        candidate_stats->relay_protocol = relay_protocol;
        if (!url.empty()) {
          candidate_stats->url = url;
        }
      } else if (candidate.is_stun()) {
        if (!url.empty()) {
          candidate_stats->url = url;
        }
      }
      if (candidate.network_type() == ADAPTER_TYPE_VPN) {
        candidate_stats->vpn = true;
        candidate_stats->network_adapter_type =
            std::string(NetworkTypeToStatsNetworkAdapterType(
                candidate.underlying_type_for_vpn()));
      } else {
        candidate_stats->vpn = false;
        candidate_stats->network_adapter_type = std::string(
            NetworkTypeToStatsNetworkAdapterType(candidate.network_type()));
      }
    } else {
      // We don't expect to know the adapter type of remote candidates.
      RTC_DCHECK_EQ(ADAPTER_TYPE_UNKNOWN, candidate.network_type());
      RTC_DCHECK_EQ(0, candidate.relay_protocol().compare(""));
      RTC_DCHECK_EQ(ADAPTER_TYPE_UNKNOWN, candidate.underlying_type_for_vpn());
    }
    candidate_stats->ip = candidate.address().ipaddr().ToString();
    candidate_stats->address = candidate.address().ipaddr().ToString();
    candidate_stats->port = static_cast<int32_t>(candidate.address().port());
    candidate_stats->protocol = candidate.protocol();
    candidate_stats->candidate_type = candidate.type_name();
    candidate_stats->priority = static_cast<int32_t>(candidate.priority());
    candidate_stats->foundation = candidate.foundation();
    auto related_address = candidate.related_address();
    if (related_address.port() != 0) {
      candidate_stats->related_address = related_address.ipaddr().ToString();
      candidate_stats->related_port =
          static_cast<int32_t>(related_address.port());
    }
    const std::string& username = candidate.username();
    if (!username.empty()) {
      candidate_stats->username_fragment = username;
    }
    if (candidate.protocol() == "tcp") {
      candidate_stats->tcp_type = candidate.tcptype();
    }
    if (candidate.network_slice() != NetworkSlice::NO_SLICE) {
      candidate_stats->network_slice =
          NetworkSliceToStatsNetworkSlice(candidate.network_slice());
    }

    stats = candidate_stats.get();
    report->AddStats(std::move(candidate_stats));
  }
  RTC_DCHECK_EQ(stats->type(), is_local ? RTCLocalIceCandidateStats::kType
                                        : RTCRemoteIceCandidateStats::kType);
  return stats->id();
}

template <typename StatsType>
void SetAudioProcessingStats(StatsType* stats,
                             const AudioProcessingStats& apm_stats) {
  if (apm_stats.echo_return_loss.has_value()) {
    stats->echo_return_loss = *apm_stats.echo_return_loss;
  }
  if (apm_stats.echo_return_loss_enhancement.has_value()) {
    stats->echo_return_loss_enhancement =
        *apm_stats.echo_return_loss_enhancement;
  }
}

// Helper function for inserting into voice/video send/receive std::map types
// while also DCHECKing for uniqueness.
template <typename StatsMap>
void AddChannelStats(StatsMap& stats_map, typename StatsMap::key_type channel) {
  RTC_DCHECK(!stats_map.contains(channel));
  stats_map.insert(std::make_pair(channel, typename StatsMap::mapped_type()));
}

}  // namespace

scoped_refptr<RTCStatsReport> RTCStatsCollector::CreateReportFilteredBySelector(
    bool filter_by_sender_selector,
    scoped_refptr<const RTCStatsReport> report,
    scoped_refptr<RtpSenderInternal> sender_selector,
    scoped_refptr<RtpReceiverInternal> receiver_selector) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  std::vector<std::string> rtpstream_ids;
  if (filter_by_sender_selector) {
    // Filter mode: RTCStatsCollector::RequestInfo::kSenderSelector
    if (sender_selector) {
      // Find outbound-rtp(s) of the sender using ssrc lookup.
      RtpParameters parameters =
          sender_selector->GetParametersInternal(/*with_all_layers=*/false);
      std::vector<RtpEncodingParameters>& encodings = parameters.encodings;
      for (const auto* outbound_rtp :
           report->GetStatsOfType<RTCOutboundRtpStreamStats>()) {
        RTC_DCHECK(outbound_rtp->ssrc.has_value());
        auto it = std::find_if(encodings.begin(), encodings.end(),
                               [ssrc = *outbound_rtp->ssrc](
                                   const RtpEncodingParameters& encoding) {
                                 return encoding.ssrc == ssrc;
                               });
        if (it != encodings.end()) {
          rtpstream_ids.push_back(outbound_rtp->id());
        }
      }
    }
  } else {
    // Filter mode: RTCStatsCollector::RequestInfo::kReceiverSelector
    if (receiver_selector) {
      // Find the inbound-rtp of the receiver using ssrc lookup.
      std::optional<uint32_t> ssrc = receiver_selector->ssrc_s();
      if (ssrc.has_value()) {
        for (const auto* inbound_rtp :
             report->GetStatsOfType<RTCInboundRtpStreamStats>()) {
          RTC_DCHECK(inbound_rtp->ssrc.has_value());
          if (*inbound_rtp->ssrc == *ssrc) {
            rtpstream_ids.push_back(inbound_rtp->id());
          }
        }
      }
    }
  }
  if (rtpstream_ids.empty())
    return RTCStatsReport::Create(report->timestamp());
  return TakeReferencedStats(report->Copy(), rtpstream_ids);
}

RTCStatsCollector::CertificateStatsPair
RTCStatsCollector::CertificateStatsPair::Copy() const {
  CertificateStatsPair copy;
  copy.local = local ? local->Copy() : nullptr;
  copy.remote = remote ? remote->Copy() : nullptr;
  return copy;
}

RTCStatsCollector::RequestInfo::RequestInfo(
    scoped_refptr<RTCStatsCollectorCallback> callback)
    : RequestInfo(FilterMode::kAll, std::move(callback), nullptr, nullptr) {}

RTCStatsCollector::RequestInfo::RequestInfo(
    scoped_refptr<RtpSenderInternal> selector,
    scoped_refptr<RTCStatsCollectorCallback> callback)
    : RequestInfo(FilterMode::kSenderSelector,
                  std::move(callback),
                  std::move(selector),
                  nullptr) {}

RTCStatsCollector::RequestInfo::RequestInfo(
    scoped_refptr<RtpReceiverInternal> selector,
    scoped_refptr<RTCStatsCollectorCallback> callback)
    : RequestInfo(FilterMode::kReceiverSelector,
                  std::move(callback),
                  nullptr,
                  std::move(selector)) {}

RTCStatsCollector::RequestInfo::RequestInfo(
    RTCStatsCollector::RequestInfo::FilterMode filter_mode,
    scoped_refptr<RTCStatsCollectorCallback> callback,
    scoped_refptr<RtpSenderInternal> sender_selector,
    scoped_refptr<RtpReceiverInternal> receiver_selector)
    : filter_mode_(filter_mode),
      callback_(std::move(callback)),
      sender_selector_(std::move(sender_selector)),
      receiver_selector_(std::move(receiver_selector)) {
  RTC_DCHECK(callback_);
  RTC_DCHECK(!sender_selector_ || !receiver_selector_);
}

struct RTCStatsCollector::CollectionContext {
  CollectionContext(scoped_refptr<RTCStatsReport> partial_report,
                    int64_t partial_report_timestamp_us)
      : partial_report_timestamp_us(partial_report_timestamp_us),
        partial_report(std::move(partial_report)) {}

  int64_t partial_report_timestamp_us = 0;

  // Reports that are produced on the signaling thread or the network thread are
  // merged into this report. It is only touched on the signaling thread. Once
  // all partial reports are merged this is the result of a request.
  scoped_refptr<RTCStatsReport> partial_report;
};

RTCStatsCollector::RTCStatsCollector(PeerConnectionInternal* pc,
                                     const Environment& env,
                                     int64_t cache_lifetime_us)
    : pc_(pc),
      is_unified_plan_(pc->IsUnifiedPlan()),
      env_(env),
      stats_timestamp_with_environment_clock_(
          pc->GetConfiguration().stats_timestamp_with_environment_clock()),
      signaling_thread_(pc->signaling_thread()),
      worker_thread_(pc->worker_thread()),
      network_thread_(pc->network_thread()),
      cache_timestamp_us_(0),
      cache_lifetime_us_(cache_lifetime_us),
      signaling_safety_(
          PendingTaskSafetyFlag::CreateAttachedToTaskQueue(/*alive=*/true,
                                                           signaling_thread_)),
      worker_safety_(
          PendingTaskSafetyFlag::CreateAttachedToTaskQueue(/*alive=*/true,
                                                           worker_thread_)),
      network_safety_(
          PendingTaskSafetyFlag::CreateAttachedToTaskQueue(/*alive=*/true,
                                                           network_thread_)) {
  RTC_DCHECK(pc_);
  RTC_DCHECK(signaling_thread_);
  RTC_DCHECK(worker_thread_);
  RTC_DCHECK(network_thread_);
  RTC_DCHECK_GE(cache_lifetime_us_, 0);
}

RTCStatsCollector::~RTCStatsCollector() = default;

void RTCStatsCollector::GetStatsReport(
    scoped_refptr<RTCStatsCollectorCallback> callback) {
  GetStatsReportInternal(RequestInfo(std::move(callback)));
}

void RTCStatsCollector::GetStatsReport(
    scoped_refptr<RtpSenderInternal> selector,
    scoped_refptr<RTCStatsCollectorCallback> callback) {
  GetStatsReportInternal(RequestInfo(std::move(selector), std::move(callback)));
}

void RTCStatsCollector::GetStatsReport(
    scoped_refptr<RtpReceiverInternal> selector,
    scoped_refptr<RTCStatsCollectorCallback> callback) {
  GetStatsReportInternal(RequestInfo(std::move(selector), std::move(callback)));
}

/*
[Signaling Thread]        [Worker Thread]          [Network Thread]
       |                         |                        |
GetStatsReport                   |                        |
       |                         |                        |
GetStatsReportInternal           |                        |
       |                         |                        |
PrepareTransceiverStatsInfos-    |                        |
  AndCallStats_s_w               |                        |
       |                         |                        |
       |----- (Post Task) ------>|                        |
       |                         |                        |
       |                  Call::GetStats                  |
       |              MediaChannel::GetStats              |
       |                         |                        |
       |<---- (Post Task) -------|                        |
       |                         |                        |
ProducePartialResultsOnSig       |                        |
       |                         |                        |
       |----------------------------- (Post Task) ------->|
       |                         |                        |
       |                         |       ProducePartialResultsOnNetworkThread
       |                         |           GetTransportStatsByNames
       |                         |           GetDataChannelStats
       |                         |           PrepareTransportCertStats
       |                         |                        |
       |<---------------------------- (Post Task) --------|
       |                         |                        |
ProcessResultsFromNetworkThread  |                        |
 (ProduceCertificateStats_s)     |                        |
 (ProduceTransportStats_s)       |                        |
 (ProduceRTPStreamStats_s)       |                        |
       |                         |                        |
OnNetworkReportReady             |                        |
 (Deliver Callback)              |                        |
       v                         v                        v
*/
void RTCStatsCollector::GetStatsReportInternal(
    RTCStatsCollector::RequestInfo request) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  if (!signaling_safety_->alive()) {
    RTC_LOG(LS_WARNING) << "GetStats called after shutdown.";
    return;
  }

  // "Now" using a monotonically increasing timer.
  int64_t cache_now_us = env_.clock().TimeInMicroseconds();
  if (cached_report_ &&
      cache_now_us - cache_timestamp_us_ <= cache_lifetime_us_) {
    // We have a fresh cached report to deliver. Deliver asynchronously, since
    // the caller may not be expecting a synchronous callback, and it avoids
    // reentrancy problems.
    signaling_thread_->PostTask(
        SafeTask(signaling_safety_, [this, request = std::move(request),
                                     report = cached_report_]() mutable {
          DeliverReport(request, report);
        }));
    return;
  }

  requests_.push_back(std::move(request));
  if (collection_context_) {
    // A stats gathering operation is already in progress.
    return;
  }

  // Only start gathering stats if we're not already gathering stats. In the
  // case of already gathering stats, `callback_` will be invoked when there
  // are no more pending partial reports.

  // Initialize common variables for the stats gather operation.
  Timestamp timestamp =
      stats_timestamp_with_environment_clock_
          ?
          // "Now" using a monotonically increasing timer.
          env_.clock().CurrentTime()
          :
          // "Now" using a system clock, relative to the UNIX epoch (Jan 1,
          // 1970, UTC), in microseconds. The system clock could be modified
          // and is not necessarily monotonically increasing.
          Timestamp::Micros(TimeUTCMicros());

  collection_context_ = std::make_unique<CollectionContext>(
      RTCStatsReport::Create(timestamp), cache_now_us);

  // Prepare `transceiver_stats_infos` and `call_stats` for use in
  // `ProducePartialResultsOnNetworkThread` and
  // `ProducePartialResultsOnSignalingThread`.
  auto worker_task = PrepareTransceiverStatsInfosAndCallStats_s_w();

  auto signaling_task = [this](WorkerThreadResult worker_result) mutable {
    RTC_DCHECK_RUN_ON(signaling_thread_);
    // Create the initial `partial_report_` for the gathering operation.
    ProducePartialResultsOnSignalingThread(
        worker_result.results.transceiver_stats_infos,
        worker_result.transceiver_references,
        worker_result.results.audio_device_stats);

    Timestamp timestamp = collection_context_->partial_report->timestamp();
    std::set<std::string> transport_names;
    auto sctp_transport_name = pc_->sctp_transport_name();
    if (sctp_transport_name) {
      transport_names.emplace(std::move(*sctp_transport_name));
    }

    for (const auto& info : worker_result.results.transceiver_stats_infos) {
      if (info.transport_name)
        transport_names.insert(*info.transport_name);
    }

    network_thread_->PostTask(SafeTask(
        network_safety_,
        [this, transport_names = std::move(transport_names), timestamp,
         signaling_flag = signaling_safety_,
         results = std::move(worker_result.results)]() mutable {
          ProducePartialResultsOnNetworkThread(
              std::move(signaling_flag), timestamp, std::move(transport_names),
              std::move(results));
        }));
  };

  worker_thread_->PostTask(SafeTask(
      worker_safety_, [this, worker_task = std::move(worker_task),
                       signaling_task = std::move(signaling_task)]() mutable {
        auto worker_result = std::move(worker_task)();
        signaling_thread_->PostTask(
            SafeTask(signaling_safety_,
                     [signaling_task = std::move(signaling_task),
                      worker_result = std::move(worker_result)]() mutable {
                       signaling_task(std::move(worker_result));
                     }));
      }));
}

void RTCStatsCollector::ClearCachedStatsReport() {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  cached_report_ = nullptr;
  // If we're not shutting down, clear the cache on the network thread.
  if (signaling_safety_->alive()) {
    network_thread_->PostTask(SafeTask(network_safety_, [this]() {
      RTC_DCHECK_RUN_ON(network_thread_);
      cached_certificates_by_transport_.clear();
    }));
  }
}

void RTCStatsCollector::CancelPendingRequestAndGetShutdownTasks(
    ScopedOperationsBatcher& network_tasks,
    ScopedOperationsBatcher& worker_tasks) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  signaling_safety_->SetNotAlive();
  worker_tasks.Add([flag = worker_safety_]() { flag->SetNotAlive(); });
  network_tasks.Add([flag = network_safety_]() { flag->SetNotAlive(); });
}

void RTCStatsCollector::ProducePartialResultsOnSignalingThread(
    const std::vector<RtpTransceiverStatsInfo>& transceiver_stats_infos,
    const std::vector<TransceiverReferences>& transceiver_references,
    const std::optional<AudioDeviceModule::Stats>& audio_device_stats) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  RTC_DCHECK(collection_context_);
  RTC_DCHECK(collection_context_->partial_report.get());
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  ProducePartialResultsOnSignalingThreadImpl(
      collection_context_->partial_report->timestamp(), transceiver_stats_infos,
      transceiver_references, audio_device_stats,
      collection_context_->partial_report.get());
}

void RTCStatsCollector::ProducePartialResultsOnSignalingThreadImpl(
    Timestamp timestamp,
    const std::vector<RtpTransceiverStatsInfo>& transceiver_stats_infos,
    const std::vector<TransceiverReferences>& transceiver_references,
    const std::optional<AudioDeviceModule::Stats>& audio_device_stats,
    RTCStatsReport* partial_report) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  ProduceMediaSourceStats_s(timestamp, transceiver_stats_infos,
                            transceiver_references, partial_report);
  ProducePeerConnectionStats_s(timestamp, partial_report);
  ProduceAudioPlayoutStats_s(timestamp, audio_device_stats, partial_report);
}

void RTCStatsCollector::ProducePartialResultsOnNetworkThread(
    scoped_refptr<PendingTaskSafetyFlag> signaling_safety,
    Timestamp timestamp,
    std::set<std::string> transport_names,
    StatsGatheringResults results) {
  RTC_DCHECK_RUN_ON(network_thread_);
  TRACE_EVENT0("webrtc",
               "RTCStatsCollector::ProducePartialResultsOnNetworkThread");
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  std::vector<DataChannelStats> data_channel_stats = pc_->GetDataChannelStats();

  std::map<std::string, TransportStats> transport_stats_by_name =
      pc_->GetTransportStatsByNames(transport_names);
  std::map<std::string, CertificateStatsPair> transport_cert_stats =
      PrepareTransportCertificateStats_n(transport_stats_by_name);

  signaling_thread_->PostTask(SafeTask(
      std::move(signaling_safety),
      [this, timestamp,
       transport_stats_by_name = std::move(transport_stats_by_name),
       transport_cert_stats = std::move(transport_cert_stats),
       transceiver_stats_infos = std::move(results.transceiver_stats_infos),
       call_stats = results.call_stats,
       audio_device_stats = results.audio_device_stats,
       data_channel_stats = std::move(data_channel_stats)]() mutable {
        auto network_report = RTCStatsReport::Create(timestamp);
        ProcessResultsFromNetworkThread(
            timestamp, std::move(transport_stats_by_name),
            std::move(transport_cert_stats), std::move(transceiver_stats_infos),
            call_stats, audio_device_stats, network_report.get());
        OnNetworkReportReady(std::move(network_report),
                             std::move(data_channel_stats));
      }));
}

void RTCStatsCollector::ProcessResultsFromNetworkThread(
    Timestamp timestamp,
    std::map<std::string, TransportStats> transport_stats_by_name,
    std::map<std::string, CertificateStatsPair> transport_cert_stats,
    std::vector<RtpTransceiverStatsInfo> transceiver_stats_infos,
    Call::Stats call_stats,
    std::optional<AudioDeviceModule::Stats> audio_device_stats,
    RTCStatsReport* partial_report) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  ProduceCertificateStats_s(timestamp, transport_cert_stats, partial_report);
  ProduceIceCandidateAndPairStats_s(timestamp, transport_stats_by_name,
                                    call_stats, partial_report);
  ProduceTransportStats_s(timestamp, transport_stats_by_name,
                          transport_cert_stats, call_stats, partial_report);
  ProduceRTPStreamStats_s(timestamp, transceiver_stats_infos, call_stats,
                          audio_device_stats, partial_report);
}

void RTCStatsCollector::OnNetworkReportReady(
    scoped_refptr<RTCStatsReport> network_report,
    std::vector<DataChannelStats> data_channel_stats) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  RTC_DCHECK(collection_context_);
  RTC_DCHECK(collection_context_->partial_report);
  RTC_DCHECK(network_report);

  collection_context_->partial_report->TakeMembersFrom(network_report);

  ProduceDataChannelStats_s(collection_context_->partial_report->timestamp(),
                            data_channel_stats,
                            collection_context_->partial_report.get());

  cache_timestamp_us_ = collection_context_->partial_report_timestamp_us;
  cached_report_ = std::move(collection_context_->partial_report);
  collection_context_ = nullptr;

  // Trace WebRTC Stats when getStats is called on Javascript.
  // This allows access to WebRTC stats from trace logs. To enable them,
  // select the "webrtc_stats" category when recording traces.
  TRACE_EVENT_INSTANT1("webrtc_stats", "webrtc_stats", TRACE_EVENT_SCOPE_GLOBAL,
                       "report", cached_report_->ToJson());

  // Clear `requests_` before the loop in case a callback adds a request.
  auto requests = std::move(requests_);
  for (const RequestInfo& request : requests) {
    DeliverReport(request, cached_report_);
  }
}

void RTCStatsCollector::DeliverReport(
    const RequestInfo& request,
    const scoped_refptr<const RTCStatsReport>& report) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  if (request.filter_mode() == RequestInfo::FilterMode::kAll) {
    request.callback()->OnStatsDelivered(report);
  } else {
    bool filter_by_sender_selector;
    scoped_refptr<RtpSenderInternal> sender_selector;
    scoped_refptr<RtpReceiverInternal> receiver_selector;
    if (request.filter_mode() == RequestInfo::FilterMode::kSenderSelector) {
      filter_by_sender_selector = true;
      sender_selector = request.sender_selector();
    } else {
      RTC_DCHECK(request.filter_mode() ==
                 RequestInfo::FilterMode::kReceiverSelector);
      filter_by_sender_selector = false;
      receiver_selector = request.receiver_selector();
    }
    request.callback()->OnStatsDelivered(CreateReportFilteredBySelector(
        filter_by_sender_selector, report, sender_selector, receiver_selector));
  }
}

void RTCStatsCollector::ProduceCertificateStats_s(
    Timestamp timestamp,
    const std::map<std::string, CertificateStatsPair>& transport_cert_stats,
    RTCStatsReport* report) const {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  for (const auto& transport_cert_stats_pair : transport_cert_stats) {
    if (transport_cert_stats_pair.second.local) {
      ProduceCertificateStatsFromSSLCertificateStats(
          timestamp, *transport_cert_stats_pair.second.local, report);
    }
    if (transport_cert_stats_pair.second.remote) {
      ProduceCertificateStatsFromSSLCertificateStats(
          timestamp, *transport_cert_stats_pair.second.remote, report);
    }
  }
}

void RTCStatsCollector::ProduceDataChannelStats_s(
    Timestamp timestamp,
    const std::vector<DataChannelStats>& data_stats,
    RTCStatsReport* report) const {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  for (const auto& stats : data_stats) {
    auto data_channel_stats = std::make_unique<RTCDataChannelStats>(
        "D" + absl::StrCat(stats.internal_id), timestamp);
    data_channel_stats->label = std::move(stats.label);
    data_channel_stats->protocol = std::move(stats.protocol);
    if (stats.id >= 0) {
      // Do not set this value before the DTLS handshake is finished
      // and filter out the magic value -1.
      data_channel_stats->data_channel_identifier = stats.id;
    }
    data_channel_stats->state = DataStateToRTCDataChannelState(stats.state);
    data_channel_stats->messages_sent = stats.messages_sent;
    data_channel_stats->bytes_sent = stats.bytes_sent;
    data_channel_stats->messages_received = stats.messages_received;
    data_channel_stats->bytes_received = stats.bytes_received;
    report->AddStats(std::move(data_channel_stats));
  }
}

void RTCStatsCollector::ProduceIceCandidateAndPairStats_s(
    Timestamp timestamp,
    const std::map<std::string, TransportStats>& transport_stats_by_name,
    const Call::Stats& call_stats,
    RTCStatsReport* report) const {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  for (const auto& entry : transport_stats_by_name) {
    const std::string& transport_name = entry.first;
    const TransportStats& transport_stats = entry.second;
    for (const auto& channel_stats : transport_stats.channel_stats) {
      std::string transport_id = RTCTransportStatsIDFromTransportChannel(
          transport_name, channel_stats.component);
      for (const auto& info :
           channel_stats.ice_transport_stats.connection_infos) {
        auto candidate_pair_stats = std::make_unique<RTCIceCandidatePairStats>(
            RTCIceCandidatePairStatsIDFromConnectionInfo(info), timestamp);

        candidate_pair_stats->transport_id = transport_id;
        candidate_pair_stats->local_candidate_id = ProduceIceCandidateStats(
            timestamp, info.local_candidate, true, transport_id, report);
        candidate_pair_stats->remote_candidate_id = ProduceIceCandidateStats(
            timestamp, info.remote_candidate, false, transport_id, report);
        candidate_pair_stats->state =
            IceCandidatePairStateToRTCStatsIceCandidatePairState(info.state);
        candidate_pair_stats->priority = info.priority;
        candidate_pair_stats->nominated = info.nominated;
        // TODO(hbos): This writable is different than the spec. It goes to
        // false after a certain amount of time without a response passes.
        // https://crbug.com/633550
        candidate_pair_stats->writable = info.writable;
        // Note that sent_total_packets includes discarded packets but
        // sent_total_bytes does not.
        candidate_pair_stats->packets_sent = static_cast<uint64_t>(
            info.sent_total_packets - info.sent_discarded_packets);
        candidate_pair_stats->packets_discarded_on_send =
            static_cast<uint64_t>(info.sent_discarded_packets);
        candidate_pair_stats->packets_received =
            static_cast<uint64_t>(info.packets_received);
        candidate_pair_stats->bytes_sent =
            static_cast<uint64_t>(info.sent_total_bytes);
        candidate_pair_stats->bytes_discarded_on_send =
            static_cast<uint64_t>(info.sent_discarded_bytes);
        candidate_pair_stats->bytes_received =
            static_cast<uint64_t>(info.recv_total_bytes);
        candidate_pair_stats->total_round_trip_time =
            static_cast<double>(info.total_round_trip_time_ms) /
            kNumMillisecsPerSec;
        if (info.current_round_trip_time_ms.has_value()) {
          candidate_pair_stats->current_round_trip_time =
              static_cast<double>(*info.current_round_trip_time_ms) /
              kNumMillisecsPerSec;
        }
        if (info.best_connection) {
          // The bandwidth estimations we have are for the selected candidate
          // pair ("info.best_connection").
          RTC_DCHECK_GE(call_stats.send_bandwidth_bps, 0);
          RTC_DCHECK_GE(call_stats.recv_bandwidth_bps, 0);
          if (call_stats.send_bandwidth_bps > 0) {
            candidate_pair_stats->available_outgoing_bitrate =
                static_cast<double>(call_stats.send_bandwidth_bps);
          }
          if (call_stats.recv_bandwidth_bps > 0) {
            candidate_pair_stats->available_incoming_bitrate =
                static_cast<double>(call_stats.recv_bandwidth_bps);
          }
        }
        candidate_pair_stats->requests_received =
            static_cast<uint64_t>(info.recv_ping_requests);
        candidate_pair_stats->requests_sent =
            static_cast<uint64_t>(info.sent_ping_requests_total);
        candidate_pair_stats->responses_received =
            static_cast<uint64_t>(info.recv_ping_responses);
        candidate_pair_stats->responses_sent =
            static_cast<uint64_t>(info.sent_ping_responses);
        RTC_DCHECK_GE(info.sent_ping_requests_total,
                      info.sent_ping_requests_before_first_response);
        candidate_pair_stats->consent_requests_sent = static_cast<uint64_t>(
            info.sent_ping_requests_total -
            info.sent_ping_requests_before_first_response);

        if (info.last_data_received.has_value()) {
          candidate_pair_stats->last_packet_received_timestamp =
              static_cast<double>(info.last_data_received->ms());
        }
        if (info.last_data_sent) {
          candidate_pair_stats->last_packet_sent_timestamp =
              static_cast<double>(info.last_data_sent->ms());
        }

        report->AddStats(std::move(candidate_pair_stats));
      }

      // Produce local candidate stats. If a transport exists these will already
      // have been produced.
      for (const auto& candidate_stats :
           channel_stats.ice_transport_stats.candidate_stats_list) {
        const auto& candidate = candidate_stats.candidate();
        ProduceIceCandidateStats(timestamp, candidate, true, transport_id,
                                 report);
      }
    }
  }
}

void RTCStatsCollector::ProduceMediaSourceStats_s(
    Timestamp timestamp,
    const std::vector<RtpTransceiverStatsInfo>& transceiver_stats_infos,
    const std::vector<TransceiverReferences>& transceiver_references,
    RTCStatsReport* report) const {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  RTC_DCHECK_EQ(transceiver_stats_infos.size(), transceiver_references.size());
  for (size_t i = 0; i < transceiver_stats_infos.size(); ++i) {
    const RtpTransceiverStatsInfo& transceiver_stats_info =
        transceiver_stats_infos[i];
    const TransceiverReferences& refs = transceiver_references[i];

    // The transceiver will still exist but in a stopped state after pc.close().
    if (transceiver_stats_info.current_direction ==
        RtpTransceiverDirection::kStopped) {
      continue;
    }
    RTC_DCHECK(transceiver_stats_info.track_media_info_map);
    const TrackMediaInfoMap& track_media_info_map =
        *transceiver_stats_info.track_media_info_map;

    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN()
    for (const auto& sender : refs.transceiver->senders()) {
      RTC_ALLOW_PLAN_B_DEPRECATION_END()
      const auto& sender_internal = sender->internal();
      const auto& track = sender_internal->track();
      if (!track)
        continue;
      // TODO(https://crbug.com/webrtc/10771): The same track could be attached
      // to multiple senders which should result in multiple senders referencing
      // the same media-source stats. When all media source related metrics are
      // moved to the track's source (e.g. input frame rate is moved from
      // webrtc::VideoSenderInfo to VideoTrackSourceInterface::Stats and audio
      // levels are moved to the corresponding audio track/source object), don't
      // create separate media source stats objects on a per-attachment basis.
      std::unique_ptr<RTCMediaSourceStats> media_source_stats;
      if (sender_internal->media_type() == MediaType::AUDIO) {
        AudioTrackInterface* audio_track =
            static_cast<AudioTrackInterface*>(track.get());
        auto audio_source_stats = std::make_unique<RTCAudioSourceStats>(
            RTCMediaSourceStatsIDFromKindAndAttachment(
                MediaType::AUDIO, sender_internal->AttachmentId()),
            timestamp);
        // TODO(https://crbug.com/webrtc/10771): We shouldn't need to have an
        // SSRC assigned (there shouldn't need to exist a send-stream, created
        // by an O/A exchange) in order to read audio media-source stats.
        // TODO(https://crbug.com/webrtc/8694): SSRC 0 shouldn't be a magic
        // value indicating no SSRC.
        if (sender_internal->ssrc() != 0) {
          auto* voice_sender_info =
              track_media_info_map.GetVoiceSenderInfoBySsrc(
                  sender_internal->ssrc());
          if (voice_sender_info) {
            audio_source_stats->audio_level = DoubleAudioLevelFromIntAudioLevel(
                voice_sender_info->audio_level);
            audio_source_stats->total_audio_energy =
                voice_sender_info->total_input_energy;
            audio_source_stats->total_samples_duration =
                voice_sender_info->total_input_duration;
            SetAudioProcessingStats(audio_source_stats.get(),
                                    voice_sender_info->apm_statistics);
          }
        }
        // Audio processor may be attached to either the track or the send
        // stream, so look in both places.
        auto audio_processor(audio_track->GetAudioProcessor());
        if (audio_processor) {
          // The `has_remote_tracks` argument is obsolete; makes no difference
          // if it's set to true or false.
          AudioProcessorInterface::AudioProcessorStatistics ap_stats =
              audio_processor->GetStats(/*has_remote_tracks=*/false);
          SetAudioProcessingStats(audio_source_stats.get(),
                                  ap_stats.apm_statistics);
        }
        media_source_stats = std::move(audio_source_stats);
      } else {
        RTC_DCHECK_EQ(MediaStreamTrackInterface::kVideoKind, track->kind());
        auto video_source_stats = std::make_unique<RTCVideoSourceStats>(
            RTCMediaSourceStatsIDFromKindAndAttachment(
                MediaType::VIDEO, sender_internal->AttachmentId()),
            timestamp);
        auto* video_track = static_cast<VideoTrackInterface*>(track.get());
        auto* video_source = video_track->GetSource();
        VideoTrackSourceInterface::Stats source_stats;
        if (video_source && video_source->GetStats(&source_stats)) {
          video_source_stats->width = source_stats.input_width;
          video_source_stats->height = source_stats.input_height;
        }
        // TODO(https://crbug.com/webrtc/10771): We shouldn't need to have an
        // SSRC assigned (there shouldn't need to exist a send-stream, created
        // by an O/A exchange) in order to get framesPerSecond.
        // TODO(https://crbug.com/webrtc/8694): SSRC 0 shouldn't be a magic
        // value indicating no SSRC.
        if (sender_internal->ssrc() != 0) {
          auto* video_sender_info =
              track_media_info_map.GetVideoSenderInfoBySsrc(
                  sender_internal->ssrc());
          if (video_sender_info) {
            video_source_stats->frames_per_second =
                video_sender_info->framerate_input;
            video_source_stats->frames = video_sender_info->frames;
          }
        }
        media_source_stats = std::move(video_source_stats);
      }
      media_source_stats->track_identifier = track->id();
      media_source_stats->kind = track->kind();
      report->AddStats(std::move(media_source_stats));
    }
  }
}

void RTCStatsCollector::ProducePeerConnectionStats_s(
    Timestamp timestamp,
    RTCStatsReport* report) const {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  auto stats(std::make_unique<RTCPeerConnectionStats>("P", timestamp));
  stats->data_channels_opened = internal_record_.data_channels_opened;
  stats->data_channels_closed = internal_record_.data_channels_closed;
  report->AddStats(std::move(stats));
}

void RTCStatsCollector::ProduceAudioPlayoutStats_s(
    Timestamp timestamp,
    const std::optional<AudioDeviceModule::Stats>& audio_device_stats,
    RTCStatsReport* report) const {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  if (audio_device_stats) {
    report->AddStats(CreateAudioPlayoutStats(*audio_device_stats, timestamp));
  }
}

void RTCStatsCollector::ProduceRTPStreamStats_s(
    Timestamp timestamp,
    const std::vector<RtpTransceiverStatsInfo>& transceiver_stats_infos,
    const Call::Stats& call_stats,
    const std::optional<AudioDeviceModule::Stats>& audio_device_stats,
    RTCStatsReport* report) const {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  for (const RtpTransceiverStatsInfo& stats : transceiver_stats_infos) {
    if (!stats.mid || !stats.transport_name) {
      continue;
    }

    if (stats.current_direction == RtpTransceiverDirection::kStopped) {
      continue;
    }

    if (stats.media_type == MediaType::AUDIO) {
      ProduceAudioRTPStreamStats_s(timestamp, stats, call_stats,
                                   audio_device_stats, report);
    } else {
      RTC_DCHECK_EQ(stats.media_type, MediaType::VIDEO);
      ProduceVideoRTPStreamStats_s(timestamp, stats, call_stats, report);
    }
  }
}

void RTCStatsCollector::ProduceAudioRTPStreamStats_s(
    Timestamp timestamp,
    const RtpTransceiverStatsInfo& stats,
    const Call::Stats& call_stats,
    const std::optional<AudioDeviceModule::Stats>& audio_device_stats,
    RTCStatsReport* report) const {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  RTC_DCHECK(stats.mid);
  RTC_DCHECK(stats.transport_name);
  RTC_DCHECK(stats.track_media_info_map);
  RTC_DCHECK(stats.track_media_info_map->voice_media_info().has_value());

  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  std::string mid = *stats.mid;
  std::string transport_id = RTCTransportStatsIDFromTransportChannel(
      *stats.transport_name, ICE_CANDIDATE_COMPONENT_RTP);
  // Inbound and remote-outbound.
  // The remote-outbound stats are based on RTCP sender reports sent from the
  // remote endpoint providing metrics about the remote outbound streams.
  for (const VoiceReceiverInfo& voice_receiver_info :
       stats.track_media_info_map->voice_media_info()->receivers) {
    if (!voice_receiver_info.connected()) {
      continue;  // The SSRC is not known yet.
    }
    // Check both packets received and samples received to handle the Insertable
    // Streams use case of receiving media without receiving packets.
    if (is_unified_plan_ && voice_receiver_info.packets_received == 0 &&
        voice_receiver_info.total_samples_received == 0) {
      // The SSRC is known despite not receiving any packets. This happens if
      // SSRC is signalled in the SDP which we should not rely on for getStats.
      continue;
    }
    // Inbound.
    std::unique_ptr<RTCInboundRtpStreamStats> inbound_audio =
        CreateInboundAudioStreamStats(
            *stats.track_media_info_map->voice_media_info(),
            voice_receiver_info, transport_id, mid, timestamp, report);
    AppendCallStats(call_stats, *inbound_audio);
    // TODO(hta): This lookup should look for the sender, not the track.
    auto track_id = stats.track_media_info_map->GetReceiverTrackIdBySsrc(
        voice_receiver_info.ssrc(), MediaType::AUDIO);
    if (track_id.has_value()) {
      inbound_audio->track_identifier = *track_id;
    }
    if (audio_device_stats && stats.media_type == MediaType::AUDIO &&
        stats.current_direction &&
        (*stats.current_direction == RtpTransceiverDirection::kSendRecv ||
         *stats.current_direction == RtpTransceiverDirection::kRecvOnly)) {
      inbound_audio->playout_id = kAudioPlayoutSingletonId;
    }
    auto* inbound_audio_ptr = report->TryAddStats(std::move(inbound_audio));
    if (!inbound_audio_ptr) {
      RTC_LOG(LS_ERROR)
          << "Unable to add audio 'inbound-rtp' to report, ID is not unique.";
      continue;
    }
    // Remote-outbound.
    auto remote_outbound_audio = CreateRemoteOutboundMediaStreamStats(
        voice_receiver_info, mid, MediaType::AUDIO, *inbound_audio_ptr,
        transport_id, stats_timestamp_with_environment_clock_);
    // Add stats.
    if (remote_outbound_audio) {
      // When the remote outbound stats are available, the remote ID for the
      // local inbound stats is set.
      auto* remote_outbound_audio_ptr =
          report->TryAddStats(std::move(remote_outbound_audio));
      if (remote_outbound_audio_ptr) {
        inbound_audio_ptr->remote_id = remote_outbound_audio_ptr->id();
      } else {
        RTC_LOG(LS_ERROR) << "Unable to add audio 'remote-outbound-rtp' to "
                          << "report, ID is not unique.";
      }
    }
  }
  // Outbound.
  std::map<std::string, RTCOutboundRtpStreamStats*> audio_outbound_rtps;
  for (const VoiceSenderInfo& voice_sender_info :
       stats.track_media_info_map->voice_media_info()->senders) {
    if (!voice_sender_info.connected()) {
      continue;  // The SSRC is not known yet.
    }
    if (is_unified_plan_ && !stats.current_direction.has_value()) {
      continue;  // The SSRC is known but the O/A has not completed.
    }
    auto outbound_audio = CreateOutboundRTPStreamStatsFromVoiceSenderInfo(
        transport_id, mid, *stats.track_media_info_map->voice_media_info(),
        voice_sender_info, timestamp, report);
    if (!voice_sender_info.local_stats.empty()) {
      auto attachment_id = stats.track_media_info_map->GetAttachmentIdBySsrc(
          voice_sender_info.local_stats[0].ssrc, MediaType::AUDIO,
          /*is_sender=*/true);
      if (attachment_id.has_value()) {
        outbound_audio->media_source_id =
            RTCMediaSourceStatsIDFromKindAndAttachment(MediaType::AUDIO,
                                                       attachment_id.value());
      }
    }
    auto audio_outbound_pair =
        std::make_pair(outbound_audio->id(), outbound_audio.get());
    if (report->TryAddStats(std::move(outbound_audio))) {
      audio_outbound_rtps.insert(std::move(audio_outbound_pair));
    } else {
      RTC_LOG(LS_ERROR)
          << "Unable to add audio 'outbound-rtp' to report, ID is not unique.";
    }
  }
  // Remote-inbound.
  // These are Report Block-based, information sent from the remote endpoint,
  // providing metrics about our Outbound streams. We take advantage of the fact
  // that RTCOutboundRtpStreamStats, RTCCodecStats and RTCTransport have already
  // been added to the report.
  for (const VoiceSenderInfo& voice_sender_info :
       stats.track_media_info_map->voice_media_info()->senders) {
    for (const auto& report_block_data : voice_sender_info.report_block_datas) {
      report->AddStats(ProduceRemoteInboundRtpStreamStats(
          transport_id, report_block_data, MediaType::AUDIO,
          audio_outbound_rtps, *report, call_stats,
          stats_timestamp_with_environment_clock_));
    }
  }
}

void RTCStatsCollector::ProduceVideoRTPStreamStats_s(
    Timestamp timestamp,
    const RtpTransceiverStatsInfo& stats,
    const Call::Stats& call_stats,
    RTCStatsReport* report) const {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  RTC_DCHECK(stats.mid);
  RTC_DCHECK(stats.transport_name);
  RTC_DCHECK(stats.track_media_info_map);
  RTC_DCHECK(stats.track_media_info_map->video_media_info().has_value());

  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  std::string mid = *stats.mid;
  std::string transport_id = RTCTransportStatsIDFromTransportChannel(
      *stats.transport_name, ICE_CANDIDATE_COMPONENT_RTP);
  // Inbound and remote-outbound.
  for (const VideoReceiverInfo& video_receiver_info :
       stats.track_media_info_map->video_media_info()->receivers) {
    if (!video_receiver_info.connected()) {
      continue;  // The SSRC is not known yet.
    }
    // Check both packets received and frames received to handle the Insertable
    // Streams use case of receiving media without receiving packets.
    if (is_unified_plan_ && video_receiver_info.packets_received == 0 &&
        video_receiver_info.frames_received == 0) {
      // The SSRC is known despite not receiving any packets. This happens if
      // SSRC is signalled in the SDP which we should not rely on for getStats.
      continue;
    }
    std::unique_ptr<RTCInboundRtpStreamStats> inbound_video =
        CreateInboundRTPStreamStatsFromVideoReceiverInfo(
            transport_id, mid, *stats.track_media_info_map->video_media_info(),
            video_receiver_info, timestamp, report);
    AppendCallStats(call_stats, *inbound_video);
    auto track_id = stats.track_media_info_map->GetReceiverTrackIdBySsrc(
        video_receiver_info.ssrc(), MediaType::VIDEO);
    if (track_id.has_value()) {
      inbound_video->track_identifier = *track_id;
    }
    auto* inbound_video_ptr = report->TryAddStats(std::move(inbound_video));
    if (!inbound_video_ptr) {
      RTC_LOG(LS_ERROR)
          << "Unable to add video 'inbound-rtp' to report, ID is not unique.";
      continue;
    }
    // Remote-outbound.
    auto remote_outbound_video = CreateRemoteOutboundMediaStreamStats(
        video_receiver_info, mid, MediaType::VIDEO, *inbound_video_ptr,
        transport_id, stats_timestamp_with_environment_clock_);
    // Add stats.
    if (remote_outbound_video) {
      // When the remote outbound stats are available, the remote ID for the
      // local inbound stats is set.
      auto* remote_outbound_video_ptr =
          report->TryAddStats(std::move(remote_outbound_video));
      if (remote_outbound_video_ptr) {
        inbound_video_ptr->remote_id = remote_outbound_video_ptr->id();
      } else {
        RTC_LOG(LS_ERROR) << "Unable to add video 'remote-outbound-rtp' to "
                          << "report, ID is not unique.";
      }
    }
  }
  // Outbound
  std::map<std::string, RTCOutboundRtpStreamStats*> video_outbound_rtps;
  for (const VideoSenderInfo& video_sender_info :
       stats.track_media_info_map->video_media_info()->senders) {
    if (!video_sender_info.connected()) {
      continue;  // The SSRC is not known yet.
    }
    if (is_unified_plan_ && !stats.current_direction.has_value()) {
      continue;  // The SSRC is known but the O/A has not completed.
    }
    auto outbound_video = CreateOutboundRTPStreamStatsFromVideoSenderInfo(
        transport_id, mid, *stats.track_media_info_map->video_media_info(),
        video_sender_info, timestamp, report);
    if (!video_sender_info.local_stats.empty()) {
      auto attachment_id = stats.track_media_info_map->GetAttachmentIdBySsrc(
          video_sender_info.local_stats[0].ssrc, MediaType::VIDEO,
          /*is_sender=*/true);
      if (attachment_id.has_value()) {
        outbound_video->media_source_id =
            RTCMediaSourceStatsIDFromKindAndAttachment(MediaType::VIDEO,
                                                       attachment_id.value());
      }
    }
    auto video_outbound_pair =
        std::make_pair(outbound_video->id(), outbound_video.get());
    if (report->TryAddStats(std::move(outbound_video))) {
      video_outbound_rtps.insert(std::move(video_outbound_pair));
    } else {
      RTC_LOG(LS_ERROR)
          << "Unable to add video 'outbound-rtp' to report, ID is not unique.";
    }
  }
  // Remote-inbound
  // These are Report Block-based, information sent from the remote endpoint,
  // providing metrics about our Outbound streams. We take advantage of the fact
  // that RTCOutboundRtpStreamStats, RTCCodecStats and RTCTransport have already
  // been added to the report.
  for (const VideoSenderInfo& video_sender_info :
       stats.track_media_info_map->video_media_info()->senders) {
    for (const auto& report_block_data : video_sender_info.report_block_datas) {
      report->AddStats(ProduceRemoteInboundRtpStreamStats(
          transport_id, report_block_data, MediaType::VIDEO,
          video_outbound_rtps, *report, call_stats,
          stats_timestamp_with_environment_clock_));
    }
  }
}

void RTCStatsCollector::ProduceTransportStats_s(
    Timestamp timestamp,
    const std::map<std::string, TransportStats>& transport_stats_by_name,
    const std::map<std::string, CertificateStatsPair>& transport_cert_stats,
    const Call::Stats& call_stats,
    RTCStatsReport* report) const {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  for (const auto& entry : transport_stats_by_name) {
    const std::string& transport_name = entry.first;
    const TransportStats& transport_stats = entry.second;

    // Get reference to RTCP channel, if it exists.
    std::string rtcp_transport_stats_id;
    for (const TransportChannelStats& channel_stats :
         transport_stats.channel_stats) {
      if (channel_stats.component == ICE_CANDIDATE_COMPONENT_RTCP) {
        rtcp_transport_stats_id = RTCTransportStatsIDFromTransportChannel(
            transport_name, channel_stats.component);
        break;
      }
    }

    // Get reference to local and remote certificates of this transport, if they
    // exist.
    const auto& certificate_stats_it =
        transport_cert_stats.find(transport_name);
    std::string local_certificate_id, remote_certificate_id;
    RTC_DCHECK(certificate_stats_it != transport_cert_stats.cend());
    if (certificate_stats_it != transport_cert_stats.cend()) {
      if (certificate_stats_it->second.local) {
        local_certificate_id = RTCCertificateIDFromFingerprint(
            certificate_stats_it->second.local->fingerprint);
      }
      if (certificate_stats_it->second.remote) {
        remote_certificate_id = RTCCertificateIDFromFingerprint(
            certificate_stats_it->second.remote->fingerprint);
      }
    }

    // There is one transport stats for each channel.
    for (const TransportChannelStats& channel_stats :
         transport_stats.channel_stats) {
      auto channel_transport_stats = std::make_unique<RTCTransportStats>(
          RTCTransportStatsIDFromTransportChannel(transport_name,
                                                  channel_stats.component),
          timestamp);
      channel_transport_stats->packets_sent =
          channel_stats.ice_transport_stats.packets_sent;
      channel_transport_stats->packets_received =
          channel_stats.ice_transport_stats.packets_received;
      channel_transport_stats->bytes_sent =
          channel_stats.ice_transport_stats.bytes_sent;
      channel_transport_stats->bytes_received =
          channel_stats.ice_transport_stats.bytes_received;
      channel_transport_stats->dtls_state =
          DtlsTransportStateToRTCDtlsTransportState(channel_stats.dtls_state);
      channel_transport_stats->selected_candidate_pair_changes =
          channel_stats.ice_transport_stats.selected_candidate_pair_changes;
      channel_transport_stats->ice_role =
          IceRoleToRTCIceRole(channel_stats.ice_transport_stats.ice_role);
      if (!channel_stats.ice_transport_stats.ice_local_username_fragment
               .empty()) {
        channel_transport_stats->ice_local_username_fragment =
            channel_stats.ice_transport_stats.ice_local_username_fragment;
      }
      channel_transport_stats->ice_state =
          IceTransportStateToRTCIceTransportState(
              channel_stats.ice_transport_stats.ice_state);
      for (const ConnectionInfo& info :
           channel_stats.ice_transport_stats.connection_infos) {
        if (info.best_connection) {
          channel_transport_stats->selected_candidate_pair_id =
              RTCIceCandidatePairStatsIDFromConnectionInfo(info);
        }
      }
      if (channel_stats.component != ICE_CANDIDATE_COMPONENT_RTCP &&
          !rtcp_transport_stats_id.empty()) {
        channel_transport_stats->rtcp_transport_stats_id =
            rtcp_transport_stats_id;
      }
      if (!local_certificate_id.empty())
        channel_transport_stats->local_certificate_id = local_certificate_id;
      if (!remote_certificate_id.empty())
        channel_transport_stats->remote_certificate_id = remote_certificate_id;
      // Crypto information
      if (channel_stats.ssl_version_bytes) {
        char bytes[5];
        snprintf(bytes, sizeof(bytes), "%04X", channel_stats.ssl_version_bytes);
        channel_transport_stats->tls_version = bytes;
      }

      if (channel_stats.dtls_role) {
        channel_transport_stats->dtls_role =
            *channel_stats.dtls_role == SSL_CLIENT ? "client" : "server";
      } else {
        channel_transport_stats->dtls_role = "unknown";
      }

      channel_transport_stats->dtls_cipher =
          channel_stats.tls_cipher_suite_name;
      if (channel_stats.srtp_crypto_suite != kSrtpInvalidCryptoSuite &&
          !SrtpCryptoSuiteToName(channel_stats.srtp_crypto_suite).empty()) {
        channel_transport_stats->srtp_cipher =
            SrtpCryptoSuiteToName(channel_stats.srtp_crypto_suite);
      }
      channel_transport_stats->ccfb_messages_received =
          call_stats.ccfb_messages_received;
      report->AddStats(std::move(channel_transport_stats));
    }
  }
}

std::map<std::string, RTCStatsCollector::CertificateStatsPair>
RTCStatsCollector::PrepareTransportCertificateStats_n(
    const std::map<std::string, TransportStats>& transport_stats_by_name) {
  RTC_DCHECK_RUN_ON(network_thread_);
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  std::map<std::string, CertificateStatsPair> transport_cert_stats;
  // Copy the certificate info from the cache, avoiding expensive
  // webrtc::SSLCertChain::GetStats() calls.
  for (const auto& pair : cached_certificates_by_transport_) {
    transport_cert_stats.insert(std::make_pair(pair.first, pair.second.Copy()));
  }

  if (transport_cert_stats.empty()) {
    // Collect certificate info.
    for (const auto& entry : transport_stats_by_name) {
      const std::string& transport_name = entry.first;

      CertificateStatsPair certificate_stats_pair;
      scoped_refptr<RTCCertificate> local_certificate;
      if (pc_->GetLocalCertificate(transport_name, &local_certificate)) {
        certificate_stats_pair.local =
            local_certificate->GetSSLCertificateChain().GetStats();
      }

      auto remote_cert_chain = pc_->GetRemoteSSLCertChain(transport_name);
      if (remote_cert_chain) {
        certificate_stats_pair.remote = remote_cert_chain->GetStats();
      }

      transport_cert_stats.insert(
          std::make_pair(transport_name, std::move(certificate_stats_pair)));
    }
    // Copy the result into the certificate cache for future reference.
    for (const auto& pair : transport_cert_stats) {
      cached_certificates_by_transport_.insert(
          std::make_pair(pair.first, pair.second.Copy()));
    }
  }
  return transport_cert_stats;
}

absl::AnyInvocable<RTCStatsCollector::WorkerThreadResult()>
RTCStatsCollector::PrepareTransceiverStatsInfosAndCallStats_s_w() {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();

  std::vector<RtpTransceiverStatsInfo> transceiver_stats_infos;
  std::vector<TransceiverReferences> transceiver_references;

  auto transceivers = pc_->GetTransceiversInternal();

  for (const auto& transceiver_proxy : transceivers) {
    RtpTransceiver* transceiver = transceiver_proxy->internal();

    RtpTransceiverStatsInfo stats{
        .media_type = transceiver->media_type(),
        .mid = transceiver->mid(),
        .transport_name = transceiver->transport_name(),
        .current_direction = transceiver->current_direction(),
        .has_channel = transceiver->HasChannel()};

    TransceiverReferences refs{.transceiver =
                                   scoped_refptr<RtpTransceiver>(transceiver)};

    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN()
    for (const auto& sender : transceiver->senders()) {
      stats.sender_infos.push_back(
          {.ssrc = sender->ssrc(),
           .attachment_id = sender->internal()->AttachmentId(),
           .media_type = sender->media_type()});
    }
    for (const auto& receiver : transceiver->receivers()) {
      stats.receiver_infos.push_back(
          {.track_id = receiver->track() ? receiver->track()->id() : "",
           .attachment_id = receiver->internal()->AttachmentId(),
           .media_type = receiver->media_type()});
      refs.receivers.push_back(
          scoped_refptr<RtpReceiverInternal>(receiver->internal()));
    }
    RTC_ALLOW_PLAN_B_DEPRECATION_END()
    stats.has_receivers = !refs.receivers.empty();

    if (stats.has_channel) {
      auto* send_channel = transceiver->media_send_channel();
      auto* receive_channel = transceiver->media_receive_channel();
      if (stats.media_type == MediaType::AUDIO) {
        VoiceMediaSendChannelInterface* voice_send =
            send_channel->AsVoiceSendChannel();
        RTC_CHECK(voice_send);
        VoiceMediaReceiveChannelInterface* voice_receive =
            receive_channel->AsVoiceReceiveChannel();
        RTC_CHECK(voice_receive);
        refs.get_send_stats_voice = voice_send->GetStatsTask();
        refs.get_receive_stats_voice = voice_receive->GetStatsTask(false);
        refs.get_send_parameters = voice_send->GetRtpSendParametersCallback();
      } else if (stats.media_type == MediaType::VIDEO) {
        VideoMediaSendChannelInterface* video_send =
            send_channel->AsVideoSendChannel();
        RTC_CHECK(video_send);
        VideoMediaReceiveChannelInterface* video_receive =
            receive_channel->AsVideoReceiveChannel();
        RTC_CHECK(video_receive);
        refs.get_send_stats_video = video_send->GetStatsTask();
        refs.get_receive_stats_video = video_receive->GetStatsTask();
        refs.get_send_parameters = video_send->GetRtpSendParametersCallback();
      }
    }

    transceiver_stats_infos.push_back(std::move(stats));
    transceiver_references.push_back(std::move(refs));
  }

  // Embed the collected information into this lambda which will run on the
  // worker thread. There, we'll call GetStats() on each media channel as well
  // as GetCallStats(). At the same time we construct the TrackMediaInfoMaps,
  // which also needs info from the worker thread.
  return [this, transceiver_stats_infos = std::move(transceiver_stats_infos),
          transceiver_references =
              std::move(transceiver_references)]() mutable {
    Thread::ScopedDisallowBlockingCalls no_blocking_calls;

    WorkerThreadResult worker_result;
    worker_result.results.transceiver_stats_infos =
        std::move(transceiver_stats_infos);
    worker_result.transceiver_references = std::move(transceiver_references);

    // Create the TrackMediaInfoMap for each transceiver stats object and keep
    // track of whether audio is played out.
    bool has_audio_receiver = false;
    RTC_DCHECK_EQ(worker_result.results.transceiver_stats_infos.size(),
                  worker_result.transceiver_references.size());
    for (size_t i = 0; i < worker_result.results.transceiver_stats_infos.size();
         ++i) {
      auto& stats = worker_result.results.transceiver_stats_infos[i];
      auto& refs = worker_result.transceiver_references[i];
      // The transceiver will still exist but in a stopped state after
      // pc.close().
      if (stats.current_direction == RtpTransceiverDirection::kStopped) {
        continue;
      }

      std::vector<RtpParameters> sender_parameters;
      for (const auto& sender : stats.sender_infos) {
        RtpParameters params;
        if (sender.ssrc != 0 && stats.has_channel) {
          params = refs.get_send_parameters(sender.ssrc);
        }
        // `sender_parameters` must be the same size as `stats.sender_infos` to
        // allow 1:1 mapping when TrackMediaInfoMap is constructed. Empty params
        // are safely ignored since they have no encodings/SSRCs.
        sender_parameters.push_back(std::move(params));
      }

      std::vector<RtpParameters> receiver_parameters;
      for (const auto& receiver : refs.receivers) {
        receiver_parameters.push_back(receiver->GetParameters());
      }

      std::optional<VoiceMediaInfo> voice_media_info;
      std::optional<VideoMediaInfo> video_media_info;
      if (stats.has_channel) {
        if (stats.media_type == MediaType::AUDIO) {
          std::optional<VoiceMediaSendInfo> voice_send =
              refs.get_send_stats_voice();
          std::optional<VoiceMediaReceiveInfo> voice_receive =
              refs.get_receive_stats_voice();
          voice_media_info = VoiceMediaInfo(
              std::move(voice_send).value_or(VoiceMediaSendInfo{}),
              std::move(voice_receive).value_or(VoiceMediaReceiveInfo{}));
        } else if (stats.media_type == MediaType::VIDEO) {
          std::optional<VideoMediaSendInfo> video_send =
              refs.get_send_stats_video();
          std::optional<VideoMediaReceiveInfo> video_receive =
              refs.get_receive_stats_video();
          video_media_info = VideoMediaInfo(
              std::move(video_send).value_or(VideoMediaSendInfo{}),
              std::move(video_receive).value_or(VideoMediaReceiveInfo{}));
        }
      }

      stats.track_media_info_map = std::make_unique<TrackMediaInfoMap>(
          std::move(voice_media_info), std::move(video_media_info),
          std::move(stats.sender_infos), std::move(sender_parameters),
          std::move(stats.receiver_infos), std::move(receiver_parameters));
      // There is no playout path until negotiated to receive audio.
      if (stats.media_type == MediaType::AUDIO && stats.has_receivers &&
          (stats.current_direction == RtpTransceiverDirection::kSendRecv ||
           stats.current_direction == RtpTransceiverDirection::kRecvOnly)) {
        has_audio_receiver = true;
      }
    }

    worker_result.results.call_stats = pc_->GetCallStats();
    worker_result.results.audio_device_stats =
        has_audio_receiver ? pc_->GetAudioDeviceStats() : std::nullopt;
    return worker_result;
  };
}

void RTCStatsCollector::OnSctpDataChannelStateChanged(
    int channel_id,
    DataChannelInterface::DataState state) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  if (state == DataChannelInterface::DataState::kOpen) {
    bool result =
        internal_record_.opened_data_channels.insert(channel_id).second;
    RTC_DCHECK(result);
    ++internal_record_.data_channels_opened;
  } else if (state == DataChannelInterface::DataState::kClosed) {
    // Only channels that have been fully opened (and have increased the
    // `data_channels_opened_` counter) increase the closed counter.
    if (internal_record_.opened_data_channels.erase(channel_id)) {
      ++internal_record_.data_channels_closed;
    }
  }
}

}  // namespace webrtc
