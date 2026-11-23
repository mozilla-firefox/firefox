/*
 *  Copyright 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/legacy_stats_collector.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "api/audio/audio_processing_statistics.h"
#include "api/audio_codecs/audio_encoder.h"
#include "api/candidate.h"
#include "api/data_channel_interface.h"
#include "api/field_trials_view.h"
#include "api/legacy_stats_types.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/video/video_content_type.h"
#include "call/call.h"
#include "media/base/media_channel.h"
#include "p2p/base/connection_info.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "pc/channel_interface.h"
#include "pc/data_channel_utils.h"
#include "pc/jsep_transport_controller.h"
#include "pc/peer_connection_internal.h"
#include "pc/rtp_receiver.h"
#include "pc/rtp_receiver_proxy.h"
#include "pc/rtp_sender_proxy.h"
#include "pc/rtp_transceiver.h"
#include "pc/transport_stats.h"
#include "rtc_base/checks.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/network_constants.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/ssl_certificate.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/system/plan_b_only.h"
#include "rtc_base/thread.h"
#include "rtc_base/trace_event.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {
namespace {

// Field trial which controls whether to report standard-compliant bytes
// sent/received per stream.  If enabled, padding and headers are not included
// in bytes sent or received.
constexpr char kUseStandardBytesStats[] = "WebRTC-UseStandardBytesStats";

// The following is the enum RTCStatsIceCandidateType from
// http://w3c.github.io/webrtc-stats/#rtcstatsicecandidatetype-enum such that
// our stats report for ice candidate type could conform to that.
const char STATSREPORT_LOCAL_PORT_TYPE[] = "host";
const char STATSREPORT_STUN_PORT_TYPE[] = "serverreflexive";
const char STATSREPORT_PRFLX_PORT_TYPE[] = "peerreflexive";
const char STATSREPORT_RELAY_PORT_TYPE[] = "relayed";

// Strings used by the stats collector to report adapter types. This fits the
// general stype of http://w3c.github.io/webrtc-stats than what
// AdapterTypeToString does.
const char* STATSREPORT_ADAPTER_TYPE_ETHERNET = "lan";
const char* STATSREPORT_ADAPTER_TYPE_WIFI = "wlan";
const char* STATSREPORT_ADAPTER_TYPE_WWAN = "wwan";
const char* STATSREPORT_ADAPTER_TYPE_VPN = "vpn";
const char* STATSREPORT_ADAPTER_TYPE_LOOPBACK = "loopback";
const char* STATSREPORT_ADAPTER_TYPE_WILDCARD = "wildcard";

template <typename ValueType>
struct TypeForAdd {
  const StatsReport::StatsValueName name;
  const ValueType& value;
};

using BoolForAdd = TypeForAdd<bool>;
using FloatForAdd = TypeForAdd<float>;
using Int64ForAdd = TypeForAdd<int64_t>;
using IntForAdd = TypeForAdd<int>;

StatsReport* AddTrackReport(StatsCollection* reports,
                            const std::string& track_id) {
  // Adds an empty track report.
  StatsReport::Id id(
      StatsReport::NewTypedId(StatsReport::kStatsReportTypeTrack, track_id));
  StatsReport* report = reports->ReplaceOrAddNew(id);
  report->AddString(StatsReport::kStatsValueNameTrackId, track_id);
  return report;
}

template <class Track>
void CreateTrackReport(const Track* track,
                       StatsCollection* reports,
                       TrackIdMap* track_ids) {
  const std::string& track_id = track->id();
  StatsReport* report = AddTrackReport(reports, track_id);
  RTC_DCHECK(report != nullptr);
  (*track_ids)[track_id] = report;
}

template <class TrackVector>
void CreateTrackReports(const TrackVector& tracks,
                        StatsCollection* reports,
                        TrackIdMap* track_ids) {
  for (const auto& track : tracks) {
    CreateTrackReport(track.get(), reports, track_ids);
  }
}

void ExtractCommonSendProperties(const MediaSenderInfo& info,
                                 StatsReport* report,
                                 bool use_standard_bytes_stats) {
  report->AddString(StatsReport::kStatsValueNameCodecName, info.codec_name);
  int64_t bytes_sent = info.payload_bytes_sent;
  if (!use_standard_bytes_stats) {
    bytes_sent += info.header_and_padding_bytes_sent;
  }
  report->AddInt64(StatsReport::kStatsValueNameBytesSent, bytes_sent);
  if (info.rtt_ms >= 0) {
    report->AddInt64(StatsReport::kStatsValueNameRtt, info.rtt_ms);
  }
}

void ExtractCommonReceiveProperties(const MediaReceiverInfo& info,
                                    StatsReport* report) {
  report->AddString(StatsReport::kStatsValueNameCodecName, info.codec_name);
}

void SetAudioProcessingStats(StatsReport* report,
                             const AudioProcessingStats& apm_stats) {
  if (apm_stats.delay_median_ms) {
    report->AddInt(StatsReport::kStatsValueNameEchoDelayMedian,
                   *apm_stats.delay_median_ms);
  }
  if (apm_stats.delay_standard_deviation_ms) {
    report->AddInt(StatsReport::kStatsValueNameEchoDelayStdDev,
                   *apm_stats.delay_standard_deviation_ms);
  }
  if (apm_stats.echo_return_loss) {
    report->AddInt(StatsReport::kStatsValueNameEchoReturnLoss,
                   *apm_stats.echo_return_loss);
  }
  if (apm_stats.echo_return_loss_enhancement) {
    report->AddInt(StatsReport::kStatsValueNameEchoReturnLossEnhancement,
                   *apm_stats.echo_return_loss_enhancement);
  }
  if (apm_stats.residual_echo_likelihood) {
    report->AddFloat(StatsReport::kStatsValueNameResidualEchoLikelihood,
                     static_cast<float>(*apm_stats.residual_echo_likelihood));
  }
  if (apm_stats.residual_echo_likelihood_recent_max) {
    report->AddFloat(
        StatsReport::kStatsValueNameResidualEchoLikelihoodRecentMax,
        static_cast<float>(*apm_stats.residual_echo_likelihood_recent_max));
  }
  if (apm_stats.divergent_filter_fraction) {
    report->AddFloat(StatsReport::kStatsValueNameAecDivergentFilterFraction,
                     static_cast<float>(*apm_stats.divergent_filter_fraction));
  }
}

void ExtractStats(const VoiceReceiverInfo& info,
                  StatsReport* report,
                  bool use_standard_bytes_stats) {
  ExtractCommonReceiveProperties(info, report);
  const FloatForAdd floats[] = {
      {.name = StatsReport::kStatsValueNameExpandRate,
       .value = info.expand_rate},
      {.name = StatsReport::kStatsValueNameSecondaryDecodedRate,
       .value = info.secondary_decoded_rate},
      {.name = StatsReport::kStatsValueNameSecondaryDiscardedRate,
       .value = info.secondary_discarded_rate},
      {.name = StatsReport::kStatsValueNameSpeechExpandRate,
       .value = info.speech_expand_rate},
      {.name = StatsReport::kStatsValueNameAccelerateRate,
       .value = info.accelerate_rate},
      {.name = StatsReport::kStatsValueNamePreemptiveExpandRate,
       .value = info.preemptive_expand_rate},
      {.name = StatsReport::kStatsValueNameTotalAudioEnergy,
       .value = static_cast<float>(info.total_output_energy)},
      {.name = StatsReport::kStatsValueNameTotalSamplesDuration,
       .value = static_cast<float>(info.total_output_duration)}};

  const IntForAdd ints[] = {
      {.name = StatsReport::kStatsValueNameCurrentDelayMs,
       .value = info.delay_estimate_ms},
      {.name = StatsReport::kStatsValueNameDecodingCNG,
       .value = info.decoding_cng},
      {.name = StatsReport::kStatsValueNameDecodingCTN,
       .value = info.decoding_calls_to_neteq},
      {.name = StatsReport::kStatsValueNameDecodingCTSG,
       .value = info.decoding_calls_to_silence_generator},
      {.name = StatsReport::kStatsValueNameDecodingMutedOutput,
       .value = info.decoding_muted_output},
      {.name = StatsReport::kStatsValueNameDecodingNormal,
       .value = info.decoding_normal},
      {.name = StatsReport::kStatsValueNameDecodingPLC,
       .value = info.decoding_plc},
      {.name = StatsReport::kStatsValueNameDecodingPLCCNG,
       .value = info.decoding_plc_cng},
      {.name = StatsReport::kStatsValueNameJitterBufferMs,
       .value = info.jitter_buffer_ms},
      {.name = StatsReport::kStatsValueNameJitterReceived,
       .value = info.jitter_ms},
      {.name = StatsReport::kStatsValueNamePacketsLost,
       .value = info.packets_lost},
      {.name = StatsReport::kStatsValueNamePacketsReceived,
       .value = info.packets_received},
      {.name = StatsReport::kStatsValueNamePreferredJitterBufferMs,
       .value = info.jitter_buffer_preferred_ms},
  };

  for (const auto& f : floats)
    report->AddFloat(f.name, f.value);

  for (const auto& i : ints)
    report->AddInt(i.name, i.value);
  if (info.audio_level >= 0) {
    report->AddInt(StatsReport::kStatsValueNameAudioOutputLevel,
                   info.audio_level);
  }
  if (info.decoding_codec_plc)
    report->AddInt(StatsReport::kStatsValueNameDecodingCodecPLC,
                   info.decoding_codec_plc);

  int64_t bytes_received = info.payload_bytes_received;
  if (!use_standard_bytes_stats) {
    bytes_received += info.header_and_padding_bytes_received;
  }
  report->AddInt64(StatsReport::kStatsValueNameBytesReceived, bytes_received);
  if (info.capture_start_ntp_time_ms >= 0) {
    report->AddInt64(StatsReport::kStatsValueNameCaptureStartNtpTimeMs,
                     info.capture_start_ntp_time_ms);
  }
  report->AddString(StatsReport::kStatsValueNameMediaType, "audio");
}

void ExtractStats(const VoiceSenderInfo& info,
                  StatsReport* report,
                  bool use_standard_bytes_stats) {
  ExtractCommonSendProperties(info, report, use_standard_bytes_stats);

  SetAudioProcessingStats(report, info.apm_statistics);

  const FloatForAdd floats[] = {
      {.name = StatsReport::kStatsValueNameTotalAudioEnergy,
       .value = static_cast<float>(info.total_input_energy)},
      {.name = StatsReport::kStatsValueNameTotalSamplesDuration,
       .value = static_cast<float>(info.total_input_duration)}};

  RTC_DCHECK_GE(info.audio_level, 0);
  const IntForAdd ints[] = {
      {.name = StatsReport::kStatsValueNameAudioInputLevel,
       .value = info.audio_level},
      {.name = StatsReport::kStatsValueNameJitterReceived,
       .value = info.jitter_ms},
      {.name = StatsReport::kStatsValueNamePacketsLost,
       .value = info.packets_lost},
      {.name = StatsReport::kStatsValueNamePacketsSent,
       .value = info.packets_sent},
  };

  for (const auto& f : floats) {
    report->AddFloat(f.name, f.value);
  }

  for (const auto& i : ints) {
    if (i.value >= 0) {
      report->AddInt(i.name, i.value);
    }
  }
  report->AddString(StatsReport::kStatsValueNameMediaType, "audio");
  if (info.ana_statistics.bitrate_action_counter) {
    report->AddInt(StatsReport::kStatsValueNameAnaBitrateActionCounter,
                   *info.ana_statistics.bitrate_action_counter);
  }
  if (info.ana_statistics.channel_action_counter) {
    report->AddInt(StatsReport::kStatsValueNameAnaChannelActionCounter,
                   *info.ana_statistics.channel_action_counter);
  }
  if (info.ana_statistics.dtx_action_counter) {
    report->AddInt(StatsReport::kStatsValueNameAnaDtxActionCounter,
                   *info.ana_statistics.dtx_action_counter);
  }
  if (info.ana_statistics.fec_action_counter) {
    report->AddInt(StatsReport::kStatsValueNameAnaFecActionCounter,
                   *info.ana_statistics.fec_action_counter);
  }
  if (info.ana_statistics.frame_length_increase_counter) {
    report->AddInt(StatsReport::kStatsValueNameAnaFrameLengthIncreaseCounter,
                   *info.ana_statistics.frame_length_increase_counter);
  }
  if (info.ana_statistics.frame_length_decrease_counter) {
    report->AddInt(StatsReport::kStatsValueNameAnaFrameLengthDecreaseCounter,
                   *info.ana_statistics.frame_length_decrease_counter);
  }
  if (info.ana_statistics.uplink_packet_loss_fraction) {
    report->AddFloat(StatsReport::kStatsValueNameAnaUplinkPacketLossFraction,
                     *info.ana_statistics.uplink_packet_loss_fraction);
  }
}

void ExtractStats(const VideoReceiverInfo& info,
                  StatsReport* report,
                  bool use_standard_bytes_stats) {
  ExtractCommonReceiveProperties(info, report);
  report->AddString(StatsReport::kStatsValueNameCodecImplementationName,
                    info.decoder_implementation_name.value_or("unknown"));
  int64_t bytes_received = info.payload_bytes_received;
  if (!use_standard_bytes_stats) {
    bytes_received += info.header_and_padding_bytes_received;
  }
  report->AddInt64(StatsReport::kStatsValueNameBytesReceived, bytes_received);
  if (info.capture_start_ntp_time_ms >= 0) {
    report->AddInt64(StatsReport::kStatsValueNameCaptureStartNtpTimeMs,
                     info.capture_start_ntp_time_ms);
  }
  if (info.first_frame_received_to_decoded_ms >= 0) {
    report->AddInt64(StatsReport::kStatsValueNameFirstFrameReceivedToDecodedMs,
                     info.first_frame_received_to_decoded_ms);
  }
  if (info.qp_sum)
    report->AddInt64(StatsReport::kStatsValueNameQpSum, *info.qp_sum);

  if (info.nacks_sent) {
    report->AddInt(StatsReport::kStatsValueNameNacksSent, *info.nacks_sent);
  }

  const IntForAdd ints[] = {
      {.name = StatsReport::kStatsValueNameCurrentDelayMs,
       .value = info.current_delay_ms},
      {.name = StatsReport::kStatsValueNameDecodeMs, .value = info.decode_ms},
      {.name = StatsReport::kStatsValueNameFirsSent, .value = info.firs_sent},
      {.name = StatsReport::kStatsValueNameFrameHeightReceived,
       .value = info.frame_height},
      {.name = StatsReport::kStatsValueNameFrameRateDecoded,
       .value = info.framerate_decoded},
      {.name = StatsReport::kStatsValueNameFrameRateOutput,
       .value = info.framerate_output},
      {.name = StatsReport::kStatsValueNameFrameRateReceived,
       .value = info.framerate_received},
      {.name = StatsReport::kStatsValueNameFrameWidthReceived,
       .value = info.frame_width},
      {.name = StatsReport::kStatsValueNameJitterBufferMs,
       .value = info.jitter_buffer_ms},
      {.name = StatsReport::kStatsValueNameMaxDecodeMs,
       .value = info.max_decode_ms},
      {.name = StatsReport::kStatsValueNameMinPlayoutDelayMs,
       .value = info.min_playout_delay_ms},
      {.name = StatsReport::kStatsValueNamePacketsLost,
       .value = info.packets_lost},
      {.name = StatsReport::kStatsValueNamePacketsReceived,
       .value = info.packets_received},
      {.name = StatsReport::kStatsValueNamePlisSent, .value = info.plis_sent},
      {.name = StatsReport::kStatsValueNameRenderDelayMs,
       .value = info.render_delay_ms},
      {.name = StatsReport::kStatsValueNameTargetDelayMs,
       .value = info.target_delay_ms},
      {.name = StatsReport::kStatsValueNameFramesDecoded,
       .value = static_cast<int>(info.frames_decoded)},
  };

  for (const auto& i : ints)
    report->AddInt(i.name, i.value);
  report->AddString(StatsReport::kStatsValueNameMediaType, "video");

  if (info.timing_frame_info) {
    report->AddString(StatsReport::kStatsValueNameTimingFrameInfo,
                      info.timing_frame_info->ToString());
  }

  report->AddInt64(StatsReport::kStatsValueNameInterframeDelayMaxMs,
                   info.interframe_delay_max_ms);

  report->AddString(StatsReport::kStatsValueNameContentType,
                    videocontenttypehelpers::ToString(info.content_type));
}

void ExtractStats(const VideoSenderInfo& info,
                  StatsReport* report,
                  bool use_standard_bytes_stats) {
  ExtractCommonSendProperties(info, report, use_standard_bytes_stats);

  report->AddString(StatsReport::kStatsValueNameCodecImplementationName,
                    info.encoder_implementation_name.value_or("unknown"));
  report->AddBoolean(StatsReport::kStatsValueNameBandwidthLimitedResolution,
                     (info.adapt_reason & 0x2) > 0);
  report->AddBoolean(StatsReport::kStatsValueNameCpuLimitedResolution,
                     (info.adapt_reason & 0x1) > 0);
  report->AddBoolean(StatsReport::kStatsValueNameHasEnteredLowResolution,
                     info.has_entered_low_resolution);

  if (info.qp_sum)
    report->AddInt(StatsReport::kStatsValueNameQpSum, *info.qp_sum);

  const IntForAdd ints[] = {
      {.name = StatsReport::kStatsValueNameAdaptationChanges,
       .value = info.adapt_changes},
      {.name = StatsReport::kStatsValueNameAvgEncodeMs,
       .value = info.avg_encode_ms},
      {.name = StatsReport::kStatsValueNameEncodeUsagePercent,
       .value = info.encode_usage_percent},
      {.name = StatsReport::kStatsValueNameFirsReceived,
       .value = info.firs_received},
      {.name = StatsReport::kStatsValueNameFrameHeightSent,
       .value = info.send_frame_height},
      {.name = StatsReport::kStatsValueNameFrameRateInput,
       .value = static_cast<int>(round(info.framerate_input))},
      {.name = StatsReport::kStatsValueNameFrameRateSent,
       .value = info.framerate_sent},
      {.name = StatsReport::kStatsValueNameFrameWidthSent,
       .value = info.send_frame_width},
      {.name = StatsReport::kStatsValueNameNacksReceived,
       .value = static_cast<int>(info.nacks_received)},
      {.name = StatsReport::kStatsValueNamePacketsLost,
       .value = info.packets_lost},
      {.name = StatsReport::kStatsValueNamePacketsSent,
       .value = info.packets_sent},
      {.name = StatsReport::kStatsValueNamePlisReceived,
       .value = info.plis_received},
      {.name = StatsReport::kStatsValueNameFramesEncoded,
       .value = static_cast<int>(info.frames_encoded)},
      {.name = StatsReport::kStatsValueNameHugeFramesSent,
       .value = static_cast<int>(info.huge_frames_sent)},
  };

  for (const auto& i : ints)
    report->AddInt(i.name, i.value);
  report->AddString(StatsReport::kStatsValueNameMediaType, "video");
  report->AddString(StatsReport::kStatsValueNameContentType,
                    videocontenttypehelpers::ToString(info.content_type));
}

void ExtractStats(const BandwidthEstimationInfo& info,
                  double stats_gathering_started,
                  StatsReport* report) {
  RTC_DCHECK(report->type() == StatsReport::kStatsReportTypeBwe);

  report->set_timestamp(stats_gathering_started);
  const IntForAdd ints[] = {
      {.name = StatsReport::kStatsValueNameAvailableSendBandwidth,
       .value = info.available_send_bandwidth},
      {.name = StatsReport::kStatsValueNameAvailableReceiveBandwidth,
       .value = info.available_recv_bandwidth},
      {.name = StatsReport::kStatsValueNameTargetEncBitrate,
       .value = info.target_enc_bitrate},
      {.name = StatsReport::kStatsValueNameActualEncBitrate,
       .value = info.actual_enc_bitrate},
      {.name = StatsReport::kStatsValueNameRetransmitBitrate,
       .value = info.retransmit_bitrate},
      {.name = StatsReport::kStatsValueNameTransmitBitrate,
       .value = info.transmit_bitrate},
  };
  for (const auto& i : ints)
    report->AddInt(i.name, i.value);
  report->AddInt64(StatsReport::kStatsValueNameBucketDelay, info.bucket_delay);
}

void ExtractRemoteStats(const MediaSenderInfo& info, StatsReport* report) {
  report->set_timestamp(info.remote_stats[0].timestamp);
  // TODO(hta): Extract some stats here.
}

void ExtractRemoteStats(const MediaReceiverInfo& info, StatsReport* report) {
  report->set_timestamp(info.remote_stats[0].timestamp);
  // TODO(hta): Extract some stats here.
}

std::string GetTrackIdBySsrc(
    uint32_t ssrc,
    StatsReport::Direction direction,
    const std::map<uint32_t, std::string>& track_id_by_ssrc) {
  auto it = track_id_by_ssrc.find(ssrc);
  if (it != track_id_by_ssrc.end()) {
    return it->second;
  }
  if (direction == StatsReport::kReceive) {
    // If the track ID was not found, this might be an unsignaled receive
    // SSRC, so try looking up by the special SSRC 0.
    it = track_id_by_ssrc.find(0);
    if (it != track_id_by_ssrc.end()) {
      RTC_LOG(LS_INFO) << "Assuming SSRC=" << ssrc
                       << " is an unsignalled receive stream corresponding "
                          "to the RtpReceiver with track ID \""
                       << it->second << "\".";
      return it->second;
    }
  }
  return "";
}

// Template to extract stats from a data vector.
// In order to use the template, the functions that are called from it,
// ExtractStats and ExtractRemoteStats, must be defined and overloaded
// for each type.
template <typename T>
void ExtractStatsFromList(
    const std::vector<T>& data,
    const StatsReport::Id& transport_id,
    LegacyStatsCollector* collector,
    StatsReport::Direction direction,
    const std::map<uint32_t, std::string>& track_id_by_ssrc) {
  for (const auto& d : data) {
    uint32_t ssrc = d.ssrc();
    std::string track_id = GetTrackIdBySsrc(ssrc, direction, track_id_by_ssrc);
    // Each track can have stats for both local and remote objects.
    // TODO(hta): Handle the case of multiple SSRCs per object.
    StatsReport* report =
        collector->PrepareReport(true, ssrc, track_id, transport_id, direction);
    if (report)
      ExtractStats(d, report, collector->UseStandardBytesStats());

    if (!d.remote_stats.empty()) {
      report = collector->PrepareReport(false, ssrc, track_id, transport_id,
                                        direction);
      if (report)
        ExtractRemoteStats(d, report);
    }
  }
}

}  // namespace

const char* IceCandidateTypeToStatsType(const Candidate& candidate) {
  if (candidate.is_local()) {
    return STATSREPORT_LOCAL_PORT_TYPE;
  }
  if (candidate.is_stun()) {
    return STATSREPORT_STUN_PORT_TYPE;
  }
  if (candidate.is_prflx()) {
    return STATSREPORT_PRFLX_PORT_TYPE;
  }
  if (candidate.is_relay()) {
    return STATSREPORT_RELAY_PORT_TYPE;
  }
  RTC_DCHECK_NOTREACHED();
  return "unknown";
}

// Return std::string to make sure that the type remains kString compatible.
std::string GetLegacyCandidateTypeName(const Candidate& c) {
  if (c.is_local())
    return "local";
  if (c.is_stun())
    return "stun";
  return std::string(c.type_name());
}

const char* AdapterTypeToStatsType(AdapterType type) {
  switch (type) {
    case ADAPTER_TYPE_UNKNOWN:
      return "unknown";
    case ADAPTER_TYPE_ETHERNET:
      return STATSREPORT_ADAPTER_TYPE_ETHERNET;
    case ADAPTER_TYPE_WIFI:
      return STATSREPORT_ADAPTER_TYPE_WIFI;
    case ADAPTER_TYPE_CELLULAR:
    case ADAPTER_TYPE_CELLULAR_2G:
    case ADAPTER_TYPE_CELLULAR_3G:
    case ADAPTER_TYPE_CELLULAR_4G:
    case ADAPTER_TYPE_CELLULAR_5G:
      return STATSREPORT_ADAPTER_TYPE_WWAN;
    case ADAPTER_TYPE_VPN:
      return STATSREPORT_ADAPTER_TYPE_VPN;
    case ADAPTER_TYPE_LOOPBACK:
      return STATSREPORT_ADAPTER_TYPE_LOOPBACK;
    case ADAPTER_TYPE_ANY:
      return STATSREPORT_ADAPTER_TYPE_WILDCARD;
    default:
      RTC_DCHECK_NOTREACHED();
      return "";
  }
}

LegacyStatsCollector::LegacyStatsCollector(
    PeerConnectionInternal* pc,
    Clock& clock,
    absl::AnyInvocable<int64_t()> utc_time_now)
    : pc_(pc),
      clock_(clock),
      stats_gathering_started_(0),
      use_standard_bytes_stats_(pc->trials().IsEnabled(kUseStandardBytesStats)),
      utc_time_now_(std::move(utc_time_now)) {
  RTC_DCHECK(pc_);
  RTC_DCHECK(utc_time_now_);
}

LegacyStatsCollector::~LegacyStatsCollector() {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());
}

// Adds a MediaStream with tracks that can be used as a `selector` in a call
// to GetStats.
void LegacyStatsCollector::AddStream(MediaStreamInterface* stream) {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());
  RTC_DCHECK(stream != nullptr);

  CreateTrackReports<AudioTrackVector>(stream->GetAudioTracks(), &reports_,
                                       &track_ids_);
  CreateTrackReports<VideoTrackVector>(stream->GetVideoTracks(), &reports_,
                                       &track_ids_);
}

void LegacyStatsCollector::AddTrack(MediaStreamTrackInterface* track) {
  if (track->kind() == MediaStreamTrackInterface::kAudioKind) {
    CreateTrackReport(static_cast<AudioTrackInterface*>(track), &reports_,
                      &track_ids_);
  } else if (track->kind() == MediaStreamTrackInterface::kVideoKind) {
    CreateTrackReport(static_cast<VideoTrackInterface*>(track), &reports_,
                      &track_ids_);
  } else {
    RTC_DCHECK_NOTREACHED() << "Illegal track kind";
  }
}

void LegacyStatsCollector::AddLocalAudioTrack(AudioTrackInterface* audio_track,
                                              uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());
  RTC_DCHECK(audio_track != nullptr);
#if RTC_DCHECK_IS_ON
  for (const auto& track : local_audio_tracks_)
    RTC_DCHECK(track.first != audio_track || track.second != ssrc);
#endif

  local_audio_tracks_.push_back(std::make_pair(audio_track, ssrc));

  // Create the kStatsReportTypeTrack report for the new track if there is no
  // report yet.
  StatsReport::Id id(StatsReport::NewTypedId(StatsReport::kStatsReportTypeTrack,
                                             audio_track->id()));
  StatsReport* report = reports_.Find(id);
  if (!report) {
    report = reports_.InsertNew(id);
    report->AddString(StatsReport::kStatsValueNameTrackId, audio_track->id());
  }
}

void LegacyStatsCollector::RemoveLocalAudioTrack(
    AudioTrackInterface* audio_track,
    uint32_t ssrc) {
  RTC_DCHECK(audio_track != nullptr);
  std::erase_if(
      local_audio_tracks_,
      [audio_track, ssrc](const LocalAudioTrackVector::value_type& track) {
        return track.first == audio_track && track.second == ssrc;
      });
}

void LegacyStatsCollector::GetStats(MediaStreamTrackInterface* track,
                                    StatsReports* reports) {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());
  RTC_DCHECK(reports != nullptr);
  RTC_DCHECK(reports->empty());

  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  if (!track) {
    reports->reserve(reports_.size());
    for (auto* r : reports_)
      reports->push_back(r);
    return;
  }

  StatsReport* report = reports_.Find(StatsReport::NewTypedId(
      StatsReport::kStatsReportTypeSession, pc_->session_id()));
  if (report)
    reports->push_back(report);

  report = reports_.Find(
      StatsReport::NewTypedId(StatsReport::kStatsReportTypeTrack, track->id()));

  if (!report)
    return;

  reports->push_back(report);

  for (const auto* r : reports_) {
    if (r->type() != StatsReport::kStatsReportTypeSsrc)
      continue;

    const StatsReport::Value* v =
        r->FindValue(StatsReport::kStatsValueNameTrackId);
    if (v && v->string_val() == track->id())
      reports->push_back(r);
  }
}

void LegacyStatsCollector::UpdateStats(
    PeerConnectionInterface::StatsOutputLevel level) {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());
  // Calls to UpdateStats() that occur less than kMinGatherStatsPeriodMs apart
  // will be ignored. Using a monotonic clock specifically for this, while using
  // a UTC clock for the reports themselves.
  const int64_t kMinGatherStatsPeriodMs = 50;
  int64_t cache_now_ms = clock_.TimeInMilliseconds();
  if (cache_timestamp_ms_ != 0 &&
      cache_timestamp_ms_ + kMinGatherStatsPeriodMs > cache_now_ms) {
    return;
  }
  cache_timestamp_ms_ = cache_now_ms;
  stats_gathering_started_ = static_cast<double>(utc_time_now_());

  // TODO(tommi): ExtractSessionInfo now has a single hop to the network thread
  // to fetch stats, then applies them on the signaling thread. See if we need
  // to do this synchronously or if updating the stats without blocking is safe.
  std::map<std::string, std::string> transport_names_by_mid =
      ExtractSessionAndDataInfo();

  // TODO(tommi): All of these hop over to the worker thread to fetch
  // information.  We could post a task to run all of these and post
  // the information back to the signaling thread where we can create and
  // update stats reports.  That would also clean up the threading story a bit
  // since we'd be creating/updating the stats report objects consistently on
  // the same thread (this class has no locks right now).
  ExtractBweInfo();
  ExtractMediaInfo(transport_names_by_mid);
  ExtractSenderInfo();
  UpdateTrackReports();
}

StatsReport* LegacyStatsCollector::PrepareReport(
    bool local,
    uint32_t ssrc,
    const std::string& track_id,
    const StatsReport::Id& transport_id,
    StatsReport::Direction direction) {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());
  StatsReport::Id id(StatsReport::NewIdWithDirection(
      local ? StatsReport::kStatsReportTypeSsrc
            : StatsReport::kStatsReportTypeRemoteSsrc,
      absl::StrCat(ssrc), direction));
  StatsReport* report = reports_.Find(id);
  if (!report) {
    report = reports_.InsertNew(id);
  }

  // FYI - for remote reports, the timestamp will be overwritten later.
  report->set_timestamp(stats_gathering_started_);

  report->AddInt64(StatsReport::kStatsValueNameSsrc, ssrc);
  if (!track_id.empty()) {
    report->AddString(StatsReport::kStatsValueNameTrackId, track_id);
  }
  // Add the mapping of SSRC to transport.
  report->AddId(StatsReport::kStatsValueNameTransportId, transport_id);
  return report;
}

StatsReport* LegacyStatsCollector::PrepareADMReport() {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());
  StatsReport::Id id(StatsReport::NewTypedId(
      StatsReport::kStatsReportTypeSession, pc_->session_id()));
  StatsReport* report = reports_.FindOrAddNew(id);
  return report;
}

bool LegacyStatsCollector::IsValidTrack(const std::string& track_id) {
  return reports_.Find(StatsReport::NewTypedId(
             StatsReport::kStatsReportTypeTrack, track_id)) != nullptr;
}

StatsReport* LegacyStatsCollector::AddCertificateReports(
    std::unique_ptr<SSLCertificateStats> cert_stats) {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());

  StatsReport* first_report = nullptr;
  StatsReport* prev_report = nullptr;
  absl::flat_hash_set<std::string> visited_fingerprints;
  for (SSLCertificateStats* stats = cert_stats.get(); stats;
       stats = stats->issuer.get()) {
    if (!visited_fingerprints.insert(stats->fingerprint).second) {
      break;
    }

    StatsReport::Id id(StatsReport::NewTypedId(
        StatsReport::kStatsReportTypeCertificate, stats->fingerprint));

    StatsReport* report = reports_.ReplaceOrAddNew(id);
    report->set_timestamp(stats_gathering_started_);
    report->AddString(StatsReport::kStatsValueNameFingerprint,
                      stats->fingerprint);
    report->AddString(StatsReport::kStatsValueNameFingerprintAlgorithm,
                      stats->fingerprint_algorithm);
    report->AddString(StatsReport::kStatsValueNameDer,
                      stats->base64_certificate);
    if (!first_report)
      first_report = report;
    else
      prev_report->AddId(StatsReport::kStatsValueNameIssuerId, id);
    prev_report = report;
  }
  return first_report;
}

StatsReport* LegacyStatsCollector::AddConnectionInfoReport(
    const std::string& content_name,
    int component,
    int connection_id,
    const StatsReport::Id& channel_report_id,
    const ConnectionInfo& info) {
  StatsReport::Id id(
      StatsReport::NewCandidatePairId(content_name, component, connection_id));
  StatsReport* report = reports_.ReplaceOrAddNew(id);
  report->set_timestamp(stats_gathering_started_);

  const BoolForAdd bools[] = {
      {.name = StatsReport::kStatsValueNameActiveConnection,
       .value = info.best_connection},
      {.name = StatsReport::kStatsValueNameReceiving, .value = info.receiving},
      {.name = StatsReport::kStatsValueNameWritable, .value = info.writable},
  };
  for (const auto& b : bools)
    report->AddBoolean(b.name, b.value);

  report->AddId(StatsReport::kStatsValueNameChannelId, channel_report_id);
  CandidateStats local_candidate_stats(info.local_candidate);
  CandidateStats remote_candidate_stats(info.remote_candidate);
  report->AddId(StatsReport::kStatsValueNameLocalCandidateId,
                AddCandidateReport(local_candidate_stats, true)->id());
  report->AddId(StatsReport::kStatsValueNameRemoteCandidateId,
                AddCandidateReport(remote_candidate_stats, false)->id());

  const Int64ForAdd int64s[] = {
      {.name = StatsReport::kStatsValueNameBytesReceived,
       .value = static_cast<int64_t>(info.recv_total_bytes)},
      {.name = StatsReport::kStatsValueNameBytesSent,
       .value = static_cast<int64_t>(info.sent_total_bytes)},
      {.name = StatsReport::kStatsValueNamePacketsSent,
       .value = static_cast<int64_t>(info.sent_total_packets)},
      {.name = StatsReport::kStatsValueNameRtt,
       .value = static_cast<int64_t>(info.rtt)},
      {.name = StatsReport::kStatsValueNameSendPacketsDiscarded,
       .value = static_cast<int64_t>(info.sent_discarded_packets)},
      {.name = StatsReport::kStatsValueNameSentPingRequestsTotal,
       .value = static_cast<int64_t>(info.sent_ping_requests_total)},
      {.name = StatsReport::kStatsValueNameSentPingRequestsBeforeFirstResponse,
       .value =
           static_cast<int64_t>(info.sent_ping_requests_before_first_response)},
      {.name = StatsReport::kStatsValueNameSentPingResponses,
       .value = static_cast<int64_t>(info.sent_ping_responses)},
      {.name = StatsReport::kStatsValueNameRecvPingRequests,
       .value = static_cast<int64_t>(info.recv_ping_requests)},
      {.name = StatsReport::kStatsValueNameRecvPingResponses,
       .value = static_cast<int64_t>(info.recv_ping_responses)},
  };
  for (const auto& i : int64s)
    report->AddInt64(i.name, i.value);

  report->AddString(StatsReport::kStatsValueNameLocalAddress,
                    info.local_candidate.address().ToString());
  report->AddString(StatsReport::kStatsValueNameLocalCandidateType,
                    GetLegacyCandidateTypeName(info.local_candidate));
  report->AddString(StatsReport::kStatsValueNameRemoteAddress,
                    info.remote_candidate.address().ToString());
  report->AddString(StatsReport::kStatsValueNameRemoteCandidateType,
                    GetLegacyCandidateTypeName(info.remote_candidate));
  report->AddString(StatsReport::kStatsValueNameTransportType,
                    info.local_candidate.protocol());
  report->AddString(StatsReport::kStatsValueNameLocalCandidateRelayProtocol,
                    info.local_candidate.relay_protocol());

  return report;
}

StatsReport* LegacyStatsCollector::AddCandidateReport(
    const CandidateStats& candidate_stats,
    bool local) {
  const auto& candidate = candidate_stats.candidate();
  StatsReport::Id id(StatsReport::NewCandidateId(local, candidate.id()));
  StatsReport* report = reports_.Find(id);
  if (!report) {
    report = reports_.InsertNew(id);
    report->set_timestamp(stats_gathering_started_);
    if (local) {
      report->AddString(StatsReport::kStatsValueNameCandidateNetworkType,
                        AdapterTypeToStatsType(candidate.network_type()));
    }
    report->AddString(StatsReport::kStatsValueNameCandidateIPAddress,
                      candidate.address().ipaddr().ToString());
    report->AddString(StatsReport::kStatsValueNameCandidatePortNumber,
                      candidate.address().PortAsString());
    report->AddInt(StatsReport::kStatsValueNameCandidatePriority,
                   candidate.priority());
    report->AddString(StatsReport::kStatsValueNameCandidateType,
                      IceCandidateTypeToStatsType(candidate));
    report->AddString(StatsReport::kStatsValueNameCandidateTransportType,
                      candidate.protocol());
  }
  report->set_timestamp(stats_gathering_started_);

  if (local && candidate_stats.stun_stats().has_value()) {
    const auto& stun_stats = candidate_stats.stun_stats().value();
    report->AddInt64(StatsReport::kStatsValueNameSentStunKeepaliveRequests,
                     stun_stats.stun_binding_requests_sent);
    report->AddInt64(StatsReport::kStatsValueNameRecvStunKeepaliveResponses,
                     stun_stats.stun_binding_responses_received);
    report->AddFloat(StatsReport::kStatsValueNameStunKeepaliveRttTotal,
                     stun_stats.stun_binding_rtt_ms_total);
    report->AddFloat(StatsReport::kStatsValueNameStunKeepaliveRttSquaredTotal,
                     stun_stats.stun_binding_rtt_ms_squared_total);
  }

  return report;
}

std::map<std::string, std::string>
LegacyStatsCollector::ExtractSessionAndDataInfo() {
  TRACE_EVENT0("webrtc", "LegacyStatsCollector::ExtractSessionAndDataInfo");
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());

  StatsCollection::Container data_report_collection;
  auto transceivers = pc_->GetTransceiversInternal();
  std::vector<std::string> mids;
  for (const auto& transceiver : transceivers) {
    if (transceiver->mid()) {
      mids.push_back(*transceiver->mid());
    }
  }

  SessionStats stats = pc_->network_thread()->BlockingCall(
      [&, sctp_transport_name = pc_->sctp_transport_name(),
       sctp_mid = pc_->sctp_mid()]() mutable {
        StatsCollection data_reports;
        ExtractDataInfo_n(&data_reports);
        data_report_collection = data_reports.DetachCollection();
        return ExtractSessionInfo_n(mids, std::move(sctp_transport_name),
                                    std::move(sctp_mid));
      });

  reports_.MergeCollection(std::move(data_report_collection));

  ExtractSessionInfo_s(stats);
  return std::move(stats.transport_names_by_mid);
}

std::optional<std::string> LegacyStatsCollector::GetTransportName(
    absl::string_view mid) {
  RTC_DCHECK_RUN_ON(pc_->network_thread());
  JsepTransportController* controller = pc_->transport_controller_n();
  if (!controller) {
    return std::nullopt;
  }
  DtlsTransportInternal* dtls_transport = controller->GetDtlsTransport(mid);
  if (!dtls_transport) {
    return std::nullopt;
  }
  IceTransportInternal* ice_transport = dtls_transport->ice_transport();
  if (!ice_transport) {
    return std::nullopt;
  }
  return ice_transport->transport_name();
}

LegacyStatsCollector::SessionStats LegacyStatsCollector::ExtractSessionInfo_n(
    const std::vector<std::string>& mids,
    std::optional<std::string> sctp_transport_name,
    std::optional<std::string> sctp_mid) {
  TRACE_EVENT0("webrtc", "LegacyStatsCollector::ExtractSessionInfo_n");
  RTC_DCHECK_RUN_ON(pc_->network_thread());
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;
  SessionStats stats;
  stats.candidate_stats = pc_->GetPooledCandidateStats();
  for (const std::string& mid : mids) {
    if (std::optional<std::string> transport_name = GetTransportName(mid)) {
      stats.transport_names_by_mid[mid] = *transport_name;
    }
  }

  if (sctp_transport_name) {
    RTC_DCHECK(sctp_mid);
    stats.transport_names_by_mid[*sctp_mid] = *sctp_transport_name;
  }

  std::set<std::string> transport_names;
  for (const auto& entry : stats.transport_names_by_mid) {
    transport_names.insert(entry.second);
  }

  std::map<std::string, ::webrtc::TransportStats> transport_stats_by_name =
      pc_->GetTransportStatsByNames(transport_names);

  for (auto& entry : transport_stats_by_name) {
    stats.transport_stats.emplace_back(entry.first, std::move(entry.second));
    TransportStats& transport = stats.transport_stats.back();

    // Attempt to get a copy of the certificates from the transport and
    // expose them in stats reports.  All channels in a transport share the
    // same local and remote certificates.
    //
    StatsReport::Id local_cert_report_id, remote_cert_report_id;
    scoped_refptr<RTCCertificate> certificate;
    if (pc_->GetLocalCertificate(transport.name, &certificate)) {
      transport.local_cert_stats =
          certificate->GetSSLCertificateChain().GetStats();
    }

    std::unique_ptr<SSLCertChain> remote_cert_chain =
        pc_->GetRemoteSSLCertChain(transport.name);
    if (remote_cert_chain) {
      transport.remote_cert_stats = remote_cert_chain->GetStats();
    }
  }

  return stats;
}

void LegacyStatsCollector::ExtractSessionInfo_s(SessionStats& session_stats) {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());
  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  StatsReport::Id id(StatsReport::NewTypedId(
      StatsReport::kStatsReportTypeSession, pc_->session_id()));
  StatsReport* report = reports_.ReplaceOrAddNew(id);
  report->set_timestamp(stats_gathering_started_);
  report->AddBoolean(StatsReport::kStatsValueNameInitiator,
                     pc_->initial_offerer());

  for (const CandidateStats& stats : session_stats.candidate_stats) {
    AddCandidateReport(stats, true);
  }

  for (auto& transport : session_stats.transport_stats) {
    // Attempt to get a copy of the certificates from the transport and
    // expose them in stats reports.  All channels in a transport share the
    // same local and remote certificates.
    //
    StatsReport::Id local_cert_report_id, remote_cert_report_id;
    if (transport.local_cert_stats) {
      StatsReport* r =
          AddCertificateReports(std::move(transport.local_cert_stats));
      if (r)
        local_cert_report_id = r->id();
    }

    if (transport.remote_cert_stats) {
      StatsReport* r =
          AddCertificateReports(std::move(transport.remote_cert_stats));
      if (r)
        remote_cert_report_id = r->id();
    }

    for (const auto& channel_iter : transport.stats.channel_stats) {
      StatsReport::Id channel_stats_id(
          StatsReport::NewComponentId(transport.name, channel_iter.component));
      StatsReport* channel_report = reports_.ReplaceOrAddNew(channel_stats_id);
      channel_report->set_timestamp(stats_gathering_started_);
      channel_report->AddInt(StatsReport::kStatsValueNameComponent,
                             channel_iter.component);
      if (local_cert_report_id.get()) {
        channel_report->AddId(StatsReport::kStatsValueNameLocalCertificateId,
                              local_cert_report_id);
      }
      if (remote_cert_report_id.get()) {
        channel_report->AddId(StatsReport::kStatsValueNameRemoteCertificateId,
                              remote_cert_report_id);
      }
      int srtp_crypto_suite = channel_iter.srtp_crypto_suite;
      if (srtp_crypto_suite != kSrtpInvalidCryptoSuite &&
          !SrtpCryptoSuiteToName(srtp_crypto_suite).empty()) {
        channel_report->AddString(StatsReport::kStatsValueNameSrtpCipher,
                                  SrtpCryptoSuiteToName(srtp_crypto_suite));
      }
      if (channel_iter.tls_cipher_suite_name) {
        channel_report->AddString(
            StatsReport::kStatsValueNameDtlsCipher,
            std::string(*channel_iter.tls_cipher_suite_name));
      }

      // Collect stats for non-pooled candidates. Note that the reports
      // generated here supersedes the candidate reports generated in
      // AddConnectionInfoReport below, and they may report candidates that are
      // not paired. Also, the candidate report generated in
      // AddConnectionInfoReport do not report port stats like StunStats.
      for (const CandidateStats& stats :
           channel_iter.ice_transport_stats.candidate_stats_list) {
        AddCandidateReport(stats, true);
      }

      int connection_id = 0;
      for (const ConnectionInfo& info :
           channel_iter.ice_transport_stats.connection_infos) {
        StatsReport* connection_report = AddConnectionInfoReport(
            transport.name, channel_iter.component, connection_id++,
            channel_report->id(), info);
        if (info.best_connection) {
          channel_report->AddId(
              StatsReport::kStatsValueNameSelectedCandidatePairId,
              connection_report->id());
        }
      }
    }
  }
}

void LegacyStatsCollector::ExtractBweInfo() {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());

  if (pc_->signaling_state() == PeerConnectionInterface::kClosed)
    return;

  Call::Stats call_stats = pc_->GetCallStats();
  BandwidthEstimationInfo bwe_info;
  bwe_info.available_send_bandwidth = call_stats.send_bandwidth_bps;
  bwe_info.available_recv_bandwidth = call_stats.recv_bandwidth_bps;
  bwe_info.bucket_delay = call_stats.pacer_delay_ms;

  // Fill in target encoder bitrate, actual encoder bitrate, rtx bitrate, etc.
  // TODO(holmer): Also fill this in for audio.
  auto transceivers = pc_->GetTransceiversInternal();
  std::vector<VideoMediaSendChannelInterface*> video_media_channels;
  for (const auto& transceiver : transceivers) {
    if (transceiver->media_type() != MediaType::VIDEO) {
      continue;
    }
    if (transceiver->internal()->HasChannel()) {
      video_media_channels.push_back(
          transceiver->internal()->video_media_send_channel());
    }
  }

  if (!video_media_channels.empty()) {
    pc_->worker_thread()->BlockingCall([&] {
      for (const auto& channel : video_media_channels) {
        channel->FillBitrateInfo(&bwe_info);
      }
    });
  }

  StatsReport::Id report_id(StatsReport::NewBandwidthEstimationId());
  StatsReport* report = reports_.FindOrAddNew(report_id);
  ExtractStats(bwe_info, stats_gathering_started_, report);
}

namespace {

class ChannelStatsGatherer {
 public:
  explicit ChannelStatsGatherer(RtpTransceiver* absl_nonnull transceiver)
      : mid(transceiver->mid().value_or("")),
        transport_name(transceiver->transport_name().value_or("")),
        transceiver_(transceiver) {
    RTC_DCHECK(transceiver_);
  }
  virtual ~ChannelStatsGatherer() = default;

  virtual bool GetStatsOnWorkerThread() = 0;

  virtual void ExtractStats(LegacyStatsCollector* collector) const = 0;

  virtual bool HasRemoteAudio() const = 0;

  const std::string mid;
  const std::string transport_name;
  std::map<uint32_t, std::string> sender_track_id_by_ssrc;
  std::map<uint32_t, std::string> receiver_track_id_by_ssrc;

 protected:
  template <typename ReceiverT, typename SenderT>
  void ExtractSenderReceiverStats(
      LegacyStatsCollector* collector,
      const std::vector<ReceiverT>& receiver_data,
      const std::vector<SenderT>& sender_data) const {
    RTC_DCHECK(collector);
    StatsReport::Id transport_id = StatsReport::NewComponentId(
        transport_name, ICE_CANDIDATE_COMPONENT_RTP);
    ExtractStatsFromList(receiver_data, transport_id, collector,
                         StatsReport::kReceive, receiver_track_id_by_ssrc);
    ExtractStatsFromList(sender_data, transport_id, collector,
                         StatsReport::kSend, sender_track_id_by_ssrc);
  }
  RtpTransceiver* transceiver() { return transceiver_; }

 private:
  RtpTransceiver* const transceiver_;
};

class VoiceChannelStatsGatherer final : public ChannelStatsGatherer {
 public:
  explicit VoiceChannelStatsGatherer(RtpTransceiver* transceiver)
      : ChannelStatsGatherer(transceiver) {
    RTC_DCHECK_EQ(transceiver->media_type(), MediaType::AUDIO);
  }

  bool GetStatsOnWorkerThread() override {
    VoiceMediaSendInfo send_info;
    VoiceMediaReceiveInfo receive_info;
    bool success =
        transceiver()->voice_media_send_channel()->GetStats(&send_info);
    success &= transceiver()->voice_media_receive_channel()->GetStats(
        &receive_info,
        /*get_and_clear_legacy_stats=*/true);
    if (success) {
      voice_media_info =
          VoiceMediaInfo(std::move(send_info), std::move(receive_info));
    }
    return success;
  }

  void ExtractStats(LegacyStatsCollector* collector) const override {
    ExtractSenderReceiverStats(collector, voice_media_info.receivers,
                               voice_media_info.senders);
    if (voice_media_info.device_underrun_count == -2 ||
        voice_media_info.device_underrun_count > 0) {
      StatsReport* report = collector->PrepareADMReport();
      report->AddInt(StatsReport::kStatsValueNameAudioDeviceUnderrunCounter,
                     voice_media_info.device_underrun_count);
    }
  }

  bool HasRemoteAudio() const override {
    return !voice_media_info.receivers.empty();
  }

 private:
  VoiceMediaInfo voice_media_info;
};

class VideoChannelStatsGatherer final : public ChannelStatsGatherer {
 public:
  explicit VideoChannelStatsGatherer(RtpTransceiver* transceiver)
      : ChannelStatsGatherer(transceiver) {
    RTC_DCHECK_EQ(transceiver->media_type(), MediaType::VIDEO);
  }

  bool GetStatsOnWorkerThread() override {
    VideoMediaSendInfo send_info;
    VideoMediaReceiveInfo receive_info;
    bool success =
        transceiver()->video_media_send_channel()->GetStats(&send_info);
    success &=
        transceiver()->video_media_receive_channel()->GetStats(&receive_info);
    if (success) {
      video_media_info =
          VideoMediaInfo(std::move(send_info), std::move(receive_info));
    }
    return success;
  }

  void ExtractStats(LegacyStatsCollector* collector) const override {
    ExtractSenderReceiverStats(collector, video_media_info.receivers,
                               video_media_info.aggregated_senders);
  }

  bool HasRemoteAudio() const override { return false; }

 private:
  VideoMediaInfo video_media_info;
};

std::unique_ptr<ChannelStatsGatherer> CreateChannelStatsGatherer(
    RtpTransceiver* transceiver) {
  RTC_DCHECK(transceiver);
  RTC_DCHECK(transceiver->HasChannel());
  if (transceiver->media_type() == MediaType::AUDIO) {
    return std::make_unique<VoiceChannelStatsGatherer>(transceiver);
  } else {
    RTC_DCHECK_EQ(transceiver->media_type(), MediaType::VIDEO);
    return std::make_unique<VideoChannelStatsGatherer>(transceiver);
  }
}

}  // namespace

void LegacyStatsCollector::ExtractMediaInfo(
    const std::map<std::string, std::string>& transport_names_by_mid) {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());

  std::vector<std::unique_ptr<ChannelStatsGatherer>> gatherers;

  auto transceivers = pc_->GetTransceiversInternal();
  {
    Thread::ScopedDisallowBlockingCalls no_blocking_calls;
    for (const auto& transceiver : transceivers) {
      if (!transceiver->internal()->HasChannel()) {
        continue;
      }
      std::unique_ptr<ChannelStatsGatherer> gatherer =
          CreateChannelStatsGatherer(transceiver->internal());
      RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN()
      for (const auto& sender : transceiver->internal()->senders()) {
        auto track = sender->track();
        std::string track_id = (track ? track->id() : "");
        gatherer->sender_track_id_by_ssrc.insert(
            std::make_pair(sender->ssrc(), track_id));
      }
      for (const auto& receiver : transceiver->internal()->receivers()) {
        gatherer->receiver_track_id_by_ssrc.insert(
            std::make_pair(receiver->internal()->ssrc_s().value_or(0),
                           receiver->track()->id()));
      }
      RTC_ALLOW_PLAN_B_DEPRECATION_END()

      gatherers.push_back(std::move(gatherer));
    }
  }

  pc_->worker_thread()->BlockingCall([&] {
    Thread::ScopedDisallowBlockingCalls no_blocking_calls;
    for (auto it = gatherers.begin(); it != gatherers.end();
         /* incremented manually */) {
      ChannelStatsGatherer* gatherer = it->get();
      if (!gatherer->GetStatsOnWorkerThread()) {
        RTC_LOG(LS_ERROR) << "Failed to get media channel stats for mid="
                          << gatherer->mid;
        it = gatherers.erase(it);
        continue;
      }
      ++it;
    }
  });

  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  bool has_remote_audio = false;
  for (const auto& gatherer : gatherers) {
    gatherer->ExtractStats(this);
    has_remote_audio |= gatherer->HasRemoteAudio();
  }

  UpdateStatsFromExistingLocalAudioTracks(has_remote_audio);
}

void LegacyStatsCollector::ExtractSenderInfo() {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());

  for (const auto& sender : pc_->GetSenders()) {
    // TODO(bugs.webrtc.org/8694): SSRC == 0 currently means none. Delete check
    // when that is fixed.
    if (!sender->ssrc()) {
      continue;
    }
    const scoped_refptr<MediaStreamTrackInterface> track(sender->track());
    if (!track || track->kind() != MediaStreamTrackInterface::kVideoKind) {
      continue;
    }
    // Safe, because kind() == kVideoKind implies a subclass of
    // VideoTrackInterface; see mediastreaminterface.h.
    VideoTrackSourceInterface* source =
        static_cast<VideoTrackInterface*>(track.get())->GetSource();

    VideoTrackSourceInterface::Stats stats;
    if (!source->GetStats(&stats)) {
      continue;
    }
    const StatsReport::Id stats_id = StatsReport::NewIdWithDirection(
        StatsReport::kStatsReportTypeSsrc, absl::StrCat(sender->ssrc()),
        StatsReport::kSend);
    StatsReport* report = reports_.FindOrAddNew(stats_id);
    report->AddInt(StatsReport::kStatsValueNameFrameWidthInput,
                   stats.input_width);
    report->AddInt(StatsReport::kStatsValueNameFrameHeightInput,
                   stats.input_height);
  }
}

void LegacyStatsCollector::ExtractDataInfo_n(StatsCollection* reports) {
  RTC_DCHECK_RUN_ON(pc_->network_thread());

  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  std::vector<DataChannelStats> data_stats = pc_->GetDataChannelStats();
  for (const auto& stats : data_stats) {
    StatsReport::Id id(StatsReport::NewTypedIntId(
        StatsReport::kStatsReportTypeDataChannel, stats.id));
    StatsReport* report = reports->ReplaceOrAddNew(id);
    report->set_timestamp(stats_gathering_started_);
    report->AddString(StatsReport::kStatsValueNameLabel, stats.label);
    // Filter out the initial id (-1).
    if (stats.id >= 0) {
      report->AddInt(StatsReport::kStatsValueNameDataChannelId, stats.id);
    }
    report->AddString(StatsReport::kStatsValueNameProtocol, stats.protocol);
    report->AddString(StatsReport::kStatsValueNameState,
                      DataChannelInterface::DataStateString(stats.state));
  }
}

StatsReport* LegacyStatsCollector::GetReport(const StatsReport::StatsType& type,
                                             const std::string& id,
                                             StatsReport::Direction direction) {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());
  RTC_DCHECK(type == StatsReport::kStatsReportTypeSsrc ||
             type == StatsReport::kStatsReportTypeRemoteSsrc);
  return reports_.Find(StatsReport::NewIdWithDirection(type, id, direction));
}

void LegacyStatsCollector::UpdateStatsFromExistingLocalAudioTracks(
    bool has_remote_tracks) {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());
  // Loop through the existing local audio tracks.
  for (const auto& it : local_audio_tracks_) {
    AudioTrackInterface* track = it.first;
    uint32_t ssrc = it.second;
    StatsReport* report = GetReport(StatsReport::kStatsReportTypeSsrc,
                                    absl::StrCat(ssrc), StatsReport::kSend);
    if (report == nullptr) {
      // This can happen if a local audio track is added to a stream on the
      // fly and the report has not been set up yet. Do nothing in this case.
      RTC_LOG(LS_ERROR) << "Stats report does not exist for ssrc " << ssrc;
      continue;
    }

    // The same ssrc can be used by both local and remote audio tracks.
    const StatsReport::Value* v =
        report->FindValue(StatsReport::kStatsValueNameTrackId);
    if (!v || v->string_val() != track->id())
      continue;

    report->set_timestamp(stats_gathering_started_);
    UpdateReportFromAudioTrack(track, report, has_remote_tracks);
  }
}

void LegacyStatsCollector::UpdateReportFromAudioTrack(
    AudioTrackInterface* track,
    StatsReport* report,
    bool has_remote_tracks) {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());
  RTC_DCHECK(track != nullptr);

  // Don't overwrite report values if they're not available.
  int signal_level;
  if (track->GetSignalLevel(&signal_level)) {
    RTC_DCHECK_GE(signal_level, 0);
    report->AddInt(StatsReport::kStatsValueNameAudioInputLevel, signal_level);
  }

  auto audio_processor(track->GetAudioProcessor());

  if (audio_processor) {
    AudioProcessorInterface::AudioProcessorStatistics stats =
        audio_processor->GetStats(has_remote_tracks);

    SetAudioProcessingStats(report, stats.apm_statistics);
  }
}

void LegacyStatsCollector::UpdateTrackReports() {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());

  Thread::ScopedDisallowBlockingCalls no_blocking_calls;

  for (const auto& entry : track_ids_) {
    StatsReport* report = entry.second;
    report->set_timestamp(stats_gathering_started_);
  }
}

void LegacyStatsCollector::InvalidateCache() {
  RTC_DCHECK_RUN_ON(pc_->signaling_thread());
  cache_timestamp_ms_ = 0;
}

}  // namespace webrtc
