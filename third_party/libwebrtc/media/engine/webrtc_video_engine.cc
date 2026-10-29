/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/engine/webrtc_video_engine.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/bind_front.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "api/crypto/crypto_options.h"
#include "api/crypto/frame_decryptor_interface.h"
#include "api/environment/environment.h"
#include "api/field_trials_view.h"
#include "api/frame_transformer_interface.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/payload_type.h"
#include "api/priority.h"
#include "api/rtc_error.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_headers.h"
#include "api/rtp_packet_infos.h"
#include "api/rtp_parameters.h"
#include "api/rtp_sender_interface.h"
#include "api/rtp_transceiver_direction.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/recordable_encoded_frame.h"
#include "api/video/video_bitrate_allocator_factory.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_sink_interface.h"
#include "api/video/video_source_interface.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "call/call.h"
#include "call/flexfec_receive_stream.h"
#include "call/packet_receiver.h"
#include "call/payload_type.h"
#include "call/payload_type_picker.h"
#include "call/rtp_config.h"
#include "call/rtp_transport_controller_send_interface.h"
#include "call/sframe_options.h"
#include "call/video_receive_stream.h"
#include "call/video_send_stream.h"
#include "common_video/frame_counts.h"
#include "common_video/include/quality_limitation_reason.h"
#include "media/base/codec.h"
#include "media/base/codec_comparators.h"
#include "media/base/media_channel.h"
#include "media/base/media_channel_impl.h"
#include "media/base/media_config.h"
#include "media/base/media_constants.h"
#include "media/base/media_engine.h"
#include "media/base/rid_description.h"
#include "media/base/rtp_utils.h"
#include "media/base/stream_params.h"
#include "media/engine/webrtc_media_engine.h"
#include "modules/rtp_rtcp/include/receive_statistics.h"
#include "modules/rtp_rtcp/include/rtcp_statistics.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/sframe/sframe_encryption_config.h"
#include "modules/video_coding/svc/scalability_mode_util.h"
#include "rtc_base/checks.h"
#include "rtc_base/dscp.h"
#include "rtc_base/experiments/field_trial_parser.h"
#include "rtc_base/logging.h"
#include "rtc_base/network_route.h"
#include "rtc_base/socket.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/time_utils.h"
#include "rtc_base/trace_event.h"
#include "video/config/video_encoder_config.h"

namespace webrtc {

namespace {

constexpr int64_t kUnsignaledSsrcCooldownMs = kNumMillisecsPerSec / 2;

// This constant is really an on/off, lower-level configurable NACK history
// duration hasn't been implemented.
const int kNackHistoryMs = 1000;

// Minimum time interval for logging stats.
const int64_t kStatsLogIntervalMs = 10000;

const char* StreamTypeToString(VideoSendStream::StreamStats::StreamType type) {
  switch (type) {
    case VideoSendStream::StreamStats::StreamType::kMedia:
      return "kMedia";
    case VideoSendStream::StreamStats::StreamType::kRtx:
      return "kRtx";
    case VideoSendStream::StreamStats::StreamType::kFlexfec:
      return "kFlexfec";
  }
  return nullptr;
}

// Get the default set of supported codecs.
// is_decoder_factory is needed to keep track of the implict assumption that any
// H264 decoder also supports constrained base line profile.
// Also, is_decoder_factory is used to decide whether FlexFEC video format
// should be advertised as supported.
template <class T>
std::vector<SdpVideoFormat> GetDefaultSupportedFormats(
    const T* absl_nullable factory,
    bool is_decoder_factory,
    const FieldTrialsView& trials) {
  if (!factory) {
    return {};
  }

  std::vector<SdpVideoFormat> supported_formats =
      factory->GetSupportedFormats();
  if (is_decoder_factory) {
    AddH264ConstrainedBaselineProfileToSupportedFormats(&supported_formats);
  }

  if (supported_formats.empty())
    return supported_formats;

  supported_formats.push_back(SdpVideoFormat(kRedCodecName));
  supported_formats.push_back(SdpVideoFormat(kUlpfecCodecName));
  // flexfec-03 is always supported as receive codec and as send codec
  // only if WebRTC-FlexFEC-03-Advertised is enabled
  if (is_decoder_factory || trials.IsEnabled("WebRTC-FlexFEC-03-Advertised")) {
    SdpVideoFormat flexfec_format(kFlexfecCodecName);
    // This value is currently arbitrarily set to 10 seconds. (The unit
    // is microseconds.) This parameter MUST be present in the SDP, but
    // we never use the actual value anywhere in our code however.
    // TODO(brandtr): Consider honouring this value in the sender and receiver.
    flexfec_format.parameters = {
        {std::string(kFlexfecFmtpRepairWindow), "10000000"}};
    supported_formats.push_back(flexfec_format);
  }
  return supported_formats;
}

// This function will assign dynamic payload types (in the range [96, 127]
// and then [35, 63]) to the input codecs, and also add ULPFEC, RED, FlexFEC,
// It will also add default feedback params to the codecs.
RTCErrorOr<Codec> AssignPayloadType(const SdpVideoFormat& format,
                                    PayloadTypePicker& pt_mapper,
                                    const FieldTrialsView& trials) {
  Codec codec = CreateVideoCodec(format);
  RTCErrorOr<PayloadType> result = pt_mapper.SuggestMapping(codec);
  if (!result.ok()) {
    return result.MoveError();
  }
  codec.id = result.value();
  AddDefaultFeedbackParams(&codec, trials);
  return codec;
}

// This function will add a associated RTX codec for a recognized primary codecs
// (VP8, VP9, AV1, H264, and RED).
RTCErrorOr<Codec> AddRtx(const Codec& primary_codec,
                         PayloadTypePicker& pt_mapper) {
  Codec rtx_codec = CreateVideoRtxCodec(Codec::kIdNotSet, primary_codec.id);
  RTCErrorOr<PayloadType> result = pt_mapper.SuggestMapping(rtx_codec);
  if (!result.ok()) {
    return result.MoveError();
  }
  rtx_codec.id = result.value();
  return rtx_codec;
}

// TODO(kron): Perhaps it is better to move the implicit knowledge to the place
// where codecs are negotiated.
template <class T>
std::vector<Codec> GetPayloadTypesAndDefaultCodecs(
    const T* absl_nullable factory,
    bool is_decoder_factory,
    bool include_rtx,
    const FieldTrialsView& trials) {
  auto supported_formats =
      GetDefaultSupportedFormats(factory, is_decoder_factory, trials);

  PayloadTypePicker pt_mapper;
  std::unordered_set<int> used_payload_types;
  std::vector<Codec> output_codecs;
  for (const auto& supported_format : supported_formats) {
    RTCErrorOr<Codec> result =
        AssignPayloadType(supported_format, pt_mapper, trials);
    if (!result.ok()) {
      // TODO: https://issues.webrtc.org/360058654 - stop assigning PTs here.
      // TODO: https://issues.webrtc.org/360058654 - Handle running out of IDs.
      continue;
    }
    bool inserted = used_payload_types.insert(result.value().id).second;
    if (!inserted) {
      RTC_LOG(LS_WARNING) << "Factory produced duplicate codecs, ignoring "
                          << result.value() << " produced from "
                          << supported_format;
      continue;
    }
    output_codecs.push_back(result.value());
    if (include_rtx) {
      Codec::ResiliencyType resiliency_type =
          result.value().GetResiliencyType();
      // FEC codecs do not use retransmission.
      if (resiliency_type == Codec::ResiliencyType::kFlexfec ||
          resiliency_type == Codec::ResiliencyType::kUlpfec) {
        continue;
      }

      RTCErrorOr<Codec> rtx_result = AddRtx(result.value(), pt_mapper);
      if (!rtx_result.ok()) {
        // TODO: https://issues.webrtc.org/360058654 - stop assigning PTs here.
        // TODO: https://issues.webrtc.org/360058654 - Handle running out of
        // IDs.
        continue;
      }
      output_codecs.push_back(rtx_result.MoveValue());
    }
  }
  return output_codecs;
}

std::string CodecVectorToString(const std::vector<Codec>& codecs) {
  StringBuilder out;
  out << "{";
  for (size_t i = 0; i < codecs.size(); ++i) {
    out << codecs[i].ToString();
    if (i != codecs.size() - 1) {
      out << ", ";
    }
  }
  out << "}";
  return out.Release();
}

bool ValidateCodecFormats(const std::vector<Codec>& codecs) {
  bool has_video = false;
  for (size_t i = 0; i < codecs.size(); ++i) {
    if (!codecs[i].ValidateCodecFormat()) {
      return false;
    }
    if (codecs[i].IsMediaCodec()) {
      has_video = true;
    }
  }
  if (!has_video) {
    RTC_LOG(LS_ERROR) << "Setting codecs without a video codec is invalid: "
                      << CodecVectorToString(codecs);
    return false;
  }
  return true;
}

bool ValidateStreamParams(const StreamParams& sp) {
  if (sp.ssrcs.empty()) {
    RTC_LOG(LS_ERROR) << "No SSRCs in stream parameters: " << sp.ToString();
    return false;
  }

  // Validate that a primary SSRC can only have one ssrc-group per semantics.
  std::map<uint32_t, std::set<std::string>> primary_ssrc_to_semantics;
  for (const auto& group : sp.ssrc_groups) {
    auto result = primary_ssrc_to_semantics.try_emplace(
        group.ssrcs[0], std::set<std::string>({group.semantics}));
    if (!result.second) {
      // A duplicate SSRC was found, check for duplicate semantics.
      auto semantics_it = result.first->second.insert(group.semantics);
      if (!semantics_it.second) {
        RTC_LOG(LS_ERROR) << "Duplicate ssrc-group '" << group.semantics
                          << " for primary SSRC " << group.ssrcs[0] << " "
                          << sp.ToString();
        return false;
      }
    }
  }

  std::vector<uint32_t> primary_ssrcs;
  sp.GetPrimarySsrcs(&primary_ssrcs);
  for (const auto& semantic :
       {kFidSsrcGroupSemantics, kFecFrSsrcGroupSemantics}) {
    if (!sp.has_ssrc_group(semantic)) {
      continue;
    }
    std::vector<uint32_t> secondary_ssrcs;
    sp.GetSecondarySsrcs(semantic, primary_ssrcs, &secondary_ssrcs);
    for (uint32_t secondary_ssrc : secondary_ssrcs) {
      bool secondary_ssrc_present = false;
      for (uint32_t sp_ssrc : sp.ssrcs) {
        if (sp_ssrc == secondary_ssrc) {
          secondary_ssrc_present = true;
          break;
        }
      }
      if (!secondary_ssrc_present) {
        RTC_LOG(LS_ERROR) << "SSRC '" << secondary_ssrc
                          << "' missing from StreamParams ssrcs with semantics "
                          << semantic << ": " << sp.ToString();
        return false;
      }
    }
    if (!secondary_ssrcs.empty() &&
        primary_ssrcs.size() != secondary_ssrcs.size()) {
      RTC_LOG(LS_ERROR)
          << semantic
          << " secondary SSRCs exist, but don't cover all SSRCs (unsupported): "
          << sp.ToString();
      return false;
    }
  }
  for (const auto& group : sp.ssrc_groups) {
    if (!(group.semantics == kFidSsrcGroupSemantics ||
          group.semantics == kSimSsrcGroupSemantics ||
          group.semantics == kFecFrSsrcGroupSemantics)) {
      continue;
    }
    for (uint32_t group_ssrc : group.ssrcs) {
      auto it = absl::c_find_if(sp.ssrcs, [&group_ssrc](uint32_t ssrc) {
        return ssrc == group_ssrc;
      });
      if (it == sp.ssrcs.end()) {
        RTC_LOG(LS_ERROR) << "SSRC '" << group_ssrc
                          << "' missing from StreamParams ssrcs with semantics "
                          << group.semantics << ": " << sp.ToString();
        return false;
      }
    }
  }
  return true;
}

// Returns true if the given codec is disallowed from doing simulcast.
bool IsCodecDisabledForSimulcast(bool legacy_scalability_mode,
                                 VideoCodecType codec_type) {
  if (legacy_scalability_mode &&
      (codec_type == kVideoCodecVP9 || codec_type == kVideoCodecAV1)) {
    return true;
  }

  return false;
}

bool IsLayerActive(const RtpEncodingParameters& layer) {
  return layer.active &&
         (!layer.max_bitrate_bps || *layer.max_bitrate_bps > 0) &&
         (!layer.max_framerate || *layer.max_framerate > 0);
}

int NumActiveStreams(const RtpParameters& rtp_parameters) {
  int res = 0;
  for (size_t i = 0; i < rtp_parameters.encodings.size(); ++i) {
    if (rtp_parameters.encodings[i].active) {
      ++res;
    }
  }
  return res;
}

std::optional<int> NumSpatialLayersFromEncoding(
    const RtpParameters& rtp_parameters,
    size_t idx) {
  if (idx >= rtp_parameters.encodings.size())
    return std::nullopt;

  std::optional<ScalabilityMode> scalability_mode = ScalabilityModeFromString(
      rtp_parameters.encodings[idx].scalability_mode.value_or(""));
  return scalability_mode
             ? std::optional<int>(
                   ScalabilityModeToNumSpatialLayers(*scalability_mode))
             : std::nullopt;
}

std::map<uint32_t, VideoSendStream::StreamStats>
MergeInfoAboutOutboundRtpSubstreams(
    const std::map<uint32_t, VideoSendStream::StreamStats>& substreams) {
  std::map<uint32_t, VideoSendStream::StreamStats> rtp_substreams;
  // Add substreams for all RTP media streams.
  for (const auto& pair : substreams) {
    uint32_t ssrc = pair.first;
    const VideoSendStream::StreamStats& substream = pair.second;
    switch (substream.type) {
      case VideoSendStream::StreamStats::StreamType::kMedia:
        break;
      case VideoSendStream::StreamStats::StreamType::kRtx:
      case VideoSendStream::StreamStats::StreamType::kFlexfec:
        continue;
    }
    rtp_substreams.insert(std::make_pair(ssrc, substream));
  }
  // Complement the kMedia substream stats with the associated kRtx and kFlexfec
  // substream stats.
  for (const auto& pair : substreams) {
    switch (pair.second.type) {
      case VideoSendStream::StreamStats::StreamType::kMedia:
        continue;
      case VideoSendStream::StreamStats::StreamType::kRtx:
      case VideoSendStream::StreamStats::StreamType::kFlexfec:
        break;
    }
    // The associated substream is an RTX or FlexFEC substream that is
    // referencing an RTP media substream.
    const VideoSendStream::StreamStats& associated_substream = pair.second;
    RTC_DCHECK(associated_substream.referenced_media_ssrc.has_value());
    uint32_t media_ssrc = associated_substream.referenced_media_ssrc.value();
    if (substreams.find(media_ssrc) == substreams.end()) {
      RTC_LOG(LS_WARNING) << "Substream [ssrc: " << pair.first << ", type: "
                          << StreamTypeToString(associated_substream.type)
                          << "] is associated with a media ssrc (" << media_ssrc
                          << ") that does not have StreamStats. Ignoring its "
                          << "RTP stats.";
      continue;
    }
    VideoSendStream::StreamStats& rtp_substream = rtp_substreams[media_ssrc];

    // We only merge `rtp_stats`. All other metrics are not applicable for RTX
    // and FlexFEC.
    // TODO(hbos): kRtx and kFlexfec stats should use a separate struct to make
    // it clear what is or is not applicable.
    rtp_substream.rtp_stats.Add(associated_substream.rtp_stats);
  }
  return rtp_substreams;
}

bool IsActiveFromEncodings(
    std::optional<uint32_t> ssrc,
    const std::vector<RtpEncodingParameters>& encodings) {
  if (ssrc.has_value()) {
    // Report the `active` value of a specific ssrc, or false if an encoding
    // with this ssrc does not exist.
    auto encoding_it = std::find_if(
        encodings.begin(), encodings.end(),
        [ssrc = ssrc.value()](const RtpEncodingParameters& encoding) {
          return encoding.ssrc.has_value() && encoding.ssrc.value() == ssrc;
        });
    return encoding_it != encodings.end() ? encoding_it->active : false;
  }
  // If `ssrc` is not specified then any encoding being active counts as active.
  for (const auto& encoding : encodings) {
    if (encoding.active) {
      return true;
    }
  }
  return false;
}

bool IsScalabilityModeSupportedByCodec(const Codec& codec,
                                       const std::string& scalability_mode,
                                       const VideoSendStream::Config& config) {
  return config.encoder_settings.encoder_factory
      ->QueryCodecSupport(SdpVideoFormat(codec.name, codec.params),
                          scalability_mode, std::nullopt)
      .is_supported;
}

// Fallback to default value if the scalability mode is unset or unsupported by
// the codec.
void FallbackToDefaultScalabilityModeIfNotSupported(
    const Codec& codec,
    const VideoSendStream::Config& config,
    std::vector<RtpEncodingParameters>& encodings) {
  if (!absl::c_any_of(encodings, [](const RtpEncodingParameters& encoding) {
        return encoding.scalability_mode && !encoding.scalability_mode->empty();
      })) {
    // Fallback is only enabled if the scalability mode is configured for any of
    // the encodings for now.
    return;
  }
  if (config.encoder_settings.encoder_factory == nullptr) {
    return;
  }
  for (auto& encoding : encodings) {
    RTC_LOG(LS_INFO) << "Encoding scalability_mode: "
                     << encoding.scalability_mode.value_or("-");
    if (!encoding.active && !encoding.scalability_mode.has_value()) {
      // Inactive encodings should not fallback since apps may only specify the
      // scalability mode of the first encoding when the others are inactive.
      continue;
    }

    if (!encoding.scalability_mode.has_value() ||
        !IsScalabilityModeSupportedByCodec(codec, *encoding.scalability_mode,
                                           config)) {
      encoding.scalability_mode =
          (encoding.scalability_mode !=
               std::string(kDefaultScalabilityModeStr) &&
           IsScalabilityModeSupportedByCodec(codec, kDefaultScalabilityModeStr,
                                             config))
              ? kDefaultScalabilityModeStr
              : kNoLayeringScalabilityModeStr;
      RTC_LOG(LS_INFO) << " -> " << *encoding.scalability_mode;
    }
  }
}

// Generate the list of codec parameters to pass down based on the negotiated
// "codecs". Note that VideoCodecSettings correspond to concrete codecs like
// VP8, VP9, H264 while VideoCodecs correspond also to "virtual" codecs like
// RTX, ULPFEC, FLEXFEC.
RTCErrorOr<std::vector<VideoCodecSettings>> MapCodecs(
    const std::vector<Codec>& codecs) {
  std::vector<VideoCodecSettings> video_codecs;
  if (codecs.empty()) {
    return video_codecs;
  }

  std::map<int, Codec::ResiliencyType> payload_codec_type;
  // `rtx_mapping` maps video payload type to rtx payload type.
  std::map<int, int> rtx_mapping;
  std::map<int, int> rtx_time_mapping;
  std::map<int, Codec> defined_codecs;

  UlpfecConfig ulpfec_config;
  std::optional<int> flexfec_payload_type;

  for (const Codec& in_codec : codecs) {
    const int payload_type = in_codec.id;

    if (payload_codec_type.find(payload_type) != payload_codec_type.end()) {
      if (MatchesWithCodecRules(defined_codecs.at(in_codec.id), in_codec)) {
        // Ignore second occurence of the same codec.
        // This can happen with multiple H.264 profiles.
        continue;
      }
      RTC_LOG(LS_ERROR) << "Duplicate codec ID, rejecting " << in_codec
                        << " because " << defined_codecs.at(in_codec.id)
                        << " is earlier in the list.";
      return RTCError(RTCErrorType::INVALID_PARAMETER,
                      "Duplicate codec ID with non-matching codecs");
    }
    defined_codecs.insert({in_codec.id, in_codec});
    payload_codec_type[payload_type] = in_codec.GetResiliencyType();

    switch (in_codec.GetResiliencyType()) {
      case Codec::ResiliencyType::kRed: {
        if (ulpfec_config.red_payload_type != -1) {
          RTC_LOG(LS_ERROR)
              << "Duplicate RED codec: ignoring PT=" << payload_type
              << " in favor of PT=" << ulpfec_config.red_payload_type
              << " which was specified first.";
          break;
        }
        ulpfec_config.red_payload_type = payload_type;
        break;
      }

      case Codec::ResiliencyType::kUlpfec: {
        if (ulpfec_config.ulpfec_payload_type != -1) {
          RTC_LOG(LS_ERROR)
              << "Duplicate ULPFEC codec: ignoring PT=" << payload_type
              << " in favor of PT=" << ulpfec_config.ulpfec_payload_type
              << " which was specified first.";
          break;
        }
        ulpfec_config.ulpfec_payload_type = payload_type;
        break;
      }

      case Codec::ResiliencyType::kFlexfec: {
        if (flexfec_payload_type) {
          RTC_LOG(LS_ERROR)
              << "Duplicate FLEXFEC codec: ignoring PT=" << payload_type
              << " in favor of PT=" << *flexfec_payload_type
              << " which was specified first.";
          break;
        }
        flexfec_payload_type = payload_type;
        break;
      }

      case Codec::ResiliencyType::kRtx: {
        int associated_payload_type;
        if (!in_codec.GetParam(kCodecParamAssociatedPayloadType,
                               &associated_payload_type) ||
            !IsValidRtpPayloadType(associated_payload_type)) {
          RTC_LOG(LS_ERROR)
              << "RTX codec with invalid or no associated payload type: "
              << in_codec.ToString();
          return RTCError(RTCErrorType::INVALID_PARAMETER,
                          "RTX codec with invalid APT");
        }
        int rtx_time;
        if (in_codec.GetParam(kCodecParamRtxTime, &rtx_time) && rtx_time > 0) {
          rtx_time_mapping[associated_payload_type] = rtx_time;
        }
        rtx_mapping[associated_payload_type] = payload_type;
        break;
      }

      case Codec::ResiliencyType::kNone: {
        video_codecs.emplace_back(in_codec);
        break;
      }
    }
  }

  // One of these codecs should have been a video codec. Only having FEC
  // parameters into this code is a logic error.
  RTC_DCHECK(!video_codecs.empty());

  for (const auto& entry : rtx_mapping) {
    const int associated_payload_type = entry.first;
    const int rtx_payload_type = entry.second;
    auto it = payload_codec_type.find(associated_payload_type);
    if (it == payload_codec_type.end()) {
      RTC_LOG(LS_ERROR) << "RTX codec (PT=" << rtx_payload_type
                        << ") mapped to PT=" << associated_payload_type
                        << " which is not in the codec list.";
      return RTCError(RTCErrorType::INVALID_PARAMETER,
                      "RTX codec with unlisted APT");
    }
    const Codec::ResiliencyType associated_codec_type = it->second;
    if (associated_codec_type != Codec::ResiliencyType::kNone &&
        associated_codec_type != Codec::ResiliencyType::kRed) {
      RTC_LOG(LS_ERROR)
          << "RTX PT=" << rtx_payload_type
          << " not mapped to regular video codec or RED codec (PT="
          << associated_payload_type << ").";
      return RTCError(RTCErrorType::INVALID_PARAMETER,
                      "RTX codec with APT not video");
    }

    if (associated_payload_type == ulpfec_config.red_payload_type) {
      ulpfec_config.red_rtx_payload_type = rtx_payload_type;
    }
  }

  for (VideoCodecSettings& codec_settings : video_codecs) {
    const int payload_type = codec_settings.codec.id;
    codec_settings.ulpfec = ulpfec_config;
    codec_settings.flexfec_payload_type = flexfec_payload_type.value_or(-1);
    auto it = rtx_mapping.find(payload_type);
    if (it != rtx_mapping.end()) {
      const int rtx_payload_type = it->second;
      codec_settings.rtx_payload_type = rtx_payload_type;

      auto rtx_time_it = rtx_time_mapping.find(payload_type);
      if (rtx_time_it != rtx_time_mapping.end()) {
        const int rtx_time = rtx_time_it->second;
        if (rtx_time < kNackHistoryMs) {
          codec_settings.rtx_time = rtx_time;
        } else {
          codec_settings.rtx_time = kNackHistoryMs;
        }
      }
    }
  }

  return video_codecs;
}

bool NonFlexfecReceiveCodecsHaveChanged(std::vector<VideoCodecSettings> before,
                                        std::vector<VideoCodecSettings> after) {
  // The receive codec order doesn't matter, so we sort the codecs before
  // comparing. This is necessary because currently the
  // only way to change the send codec is to munge SDP, which causes
  // the receive codec list to change order, which causes the streams
  // to be recreates which causes a "blink" of black video.  In order
  // to support munging the SDP in this way without recreating receive
  // streams, we ignore the order of the received codecs so that
  // changing the order doesn't cause this "blink".
  auto comparison = [](const VideoCodecSettings& codec1,
                       const VideoCodecSettings& codec2) {
    return codec1.codec.id > codec2.codec.id;
  };
  absl::c_sort(before, comparison);
  absl::c_sort(after, comparison);

  // Changes in FlexFEC payload type are handled separately in
  // WebRtcVideoReceiveChannel::GetChangedReceiverParameters, so disregard
  // FlexFEC in the comparison here.
  return !absl::c_equal(before, after,
                        VideoCodecSettings::EqualsDisregardingFlexfec);
}

std::string CodecSettingsVectorToString(
    const std::vector<VideoCodecSettings>& codecs) {
  StringBuilder out;
  out << "{";
  for (size_t i = 0; i < codecs.size(); ++i) {
    out << codecs[i].codec.ToString();
    if (i != codecs.size() - 1) {
      out << ", ";
    }
  }
  out << "}";
  return out.Release();
}

void ExtractCodecInformation(
    std::span<const VideoCodecSettings> recv_codecs,
    std::map<int, int>& rtx_associated_payload_types,
    std::set<int>& raw_payload_types,
    std::vector<VideoReceiveStreamInterface::Decoder>& decoders) {
  RTC_DCHECK(!recv_codecs.empty());
  RTC_DCHECK(rtx_associated_payload_types.empty());
  RTC_DCHECK(raw_payload_types.empty());
  RTC_DCHECK(decoders.empty());

  for (const VideoCodecSettings& recv_codec : recv_codecs) {
    decoders.emplace_back(
        SdpVideoFormat(recv_codec.codec.name, recv_codec.codec.params),
        recv_codec.codec.id);
    rtx_associated_payload_types.emplace(recv_codec.rtx_payload_type,
                                         recv_codec.codec.id);
    if (recv_codec.codec.packetization == kPacketizationParamRaw) {
      raw_payload_types.insert(recv_codec.codec.id);
    }
  }
}

int ParseReceiveBufferSize(const FieldTrialsView& trials) {
  FieldTrialParameter<int> size_bytes("size_bytes", kVideoRtpRecvBufferSize);
  ParseFieldTrial({&size_bytes}, trials.Lookup("WebRTC-ReceiveBufferSize"));
  if (size_bytes.Get() < 10'000 || size_bytes.Get() > 10'000'000) {
    RTC_LOG(LS_WARNING) << "WebRTC-ReceiveBufferSize out of bounds: "
                        << size_bytes.Get();
    return kVideoRtpRecvBufferSize;
  }
  return size_bytes.Get();
}

RTCError ResolveSendCodecs(
    const VideoCodecSettings& current_codec,
    const std::vector<VideoCodecSettings>& current_codecs,
    const std::vector<RtpEncodingParameters>& encodings,
    const std::vector<VideoCodecSettings>& negotiated_codecs,
    std::vector<VideoCodecSettings>* resolved_codecs) {
  RTC_DCHECK(resolved_codecs);
  resolved_codecs->clear();
  for (size_t i = 0; i < encodings.size(); i++) {
    const std::optional<RtpCodec>& requested_codec = encodings[i].codec;
    std::optional<VideoCodecSettings> found_codec;
    if (!requested_codec) {
      found_codec = current_codec;
    } else if (i < current_codecs.size()) {
      const VideoCodecSettings& codec = current_codecs[i];
      if (IsSameRtpCodecIgnoringLevel(codec.codec, *requested_codec)) {
        found_codec = codec;
      }
    }
    if (!found_codec) {
      RTC_DCHECK(requested_codec);
      auto matched_codec =
          absl::c_find_if(negotiated_codecs, [&](const auto& negotiated_codec) {
            return IsSameRtpCodecIgnoringLevel(negotiated_codec.codec,
                                               *requested_codec);
          });
      if (matched_codec == negotiated_codecs.end()) {
        return RTC_LOG_ERROR(RTCError(RTCErrorType::INVALID_MODIFICATION)
                             << "Attempted to use an unsupported codec "
                             << requested_codec->name << " for encodings[" << i
                             << "].");
      }
      found_codec = *matched_codec;
    }
    RTC_DCHECK(found_codec);
    resolved_codecs->push_back(*found_codec);
  }
  return RTCError::OK();
}

}  // namespace
// --------------- WebRtcVideoEngine ---------------------------

WebRtcVideoEngine::WebRtcVideoEngine(
    std::unique_ptr<VideoEncoderFactory> video_encoder_factory,
    std::unique_ptr<VideoDecoderFactory> video_decoder_factory,
    const FieldTrialsView& trials)
    : decoder_factory_(std::move(video_decoder_factory)),
      encoder_factory_(std::move(video_encoder_factory)),
      trials_(trials) {
  RTC_DLOG(LS_INFO) << "WebRtcVideoEngine::WebRtcVideoEngine()";
}

WebRtcVideoEngine::~WebRtcVideoEngine() {
  RTC_DLOG(LS_INFO) << "WebRtcVideoEngine::~WebRtcVideoEngine";
}

std::unique_ptr<VideoMediaSendChannelInterface>
WebRtcVideoEngine::CreateSendChannel(
    const Environment& env,
    Call* call,
    const MediaConfig& config,
    const VideoOptions& options,
    const CryptoOptions& crypto_options,
    VideoBitrateAllocatorFactory* video_bitrate_allocator_factory,
    VideoMediaSendChannelInterface::EncoderSwitchRequestCallback
        video_encoder_switch_request_callback,
    absl::AnyInvocable<void()> parameters_changed_callback) {
  return std::make_unique<WebRtcVideoSendChannel>(
      env, call, config, options, crypto_options, encoder_factory_.get(),
      video_bitrate_allocator_factory,
      std::move(video_encoder_switch_request_callback),
      std::move(parameters_changed_callback));
}
std::unique_ptr<VideoMediaReceiveChannelInterface>
WebRtcVideoEngine::CreateReceiveChannel(
    const Environment& env,
    Call* call,
    const MediaConfig& config,
    const CryptoOptions& crypto_options,
    absl::AnyInvocable<void(uint32_t ssrc)> on_first_packet,
    absl::AnyInvocable<void(uint32_t ssrc, const RtpPacketInfos&, Timestamp)
                           const> on_frame_delivered_callback) {
  return std::make_unique<WebRtcVideoReceiveChannel>(
      env, call, config, crypto_options, decoder_factory_.get(),
      std::move(on_first_packet), std::move(on_frame_delivered_callback));
}

std::vector<Codec> WebRtcVideoEngine::LegacySendCodecs(bool include_rtx) const {
  return GetPayloadTypesAndDefaultCodecs(encoder_factory_.get(),
                                         /*is_decoder_factory=*/false,
                                         include_rtx, trials_);
}

std::vector<Codec> WebRtcVideoEngine::LegacyRecvCodecs(bool include_rtx) const {
  return GetPayloadTypesAndDefaultCodecs(decoder_factory_.get(),
                                         /*is_decoder_factory=*/true,
                                         include_rtx, trials_);
}

std::vector<SdpVideoFormat> WebRtcVideoEngine::GetSupportedFormats(
    bool is_decoder) const {
  std::vector<SdpVideoFormat> formats;
  if (is_decoder) {
    formats = GetDefaultSupportedFormats(decoder_factory_.get(),
                                         /*is_decoder_factory=*/true, trials_);
  } else {
    formats = GetDefaultSupportedFormats(encoder_factory_.get(),
                                         /*is_decoder_factory=*/false, trials_);
  }
  if (!formats.empty()) {
    formats.push_back(SdpVideoFormat(kRtxCodecName));
  }
  return formats;
}

std::vector<RtpHeaderExtensionCapability>
WebRtcVideoEngine::GetRtpHeaderExtensions(
    const FieldTrialsView* field_trials) const {
  // Use field trials from PeerConnection `field_trials` or from
  // PeerConnectionFactory `trials_`.
  const FieldTrialsView& trials =
      (field_trials != nullptr ? *field_trials : trials_);

  std::vector<RtpHeaderExtensionCapability> result;
  // id is *not* incremented for non-default extensions. Conflicting IDs
  // need to be resolved.
  RtpHeaderExtensionId id(1);
  for (const auto& uri :
       {RtpExtension::kTimestampOffsetUri, RtpExtension::kAbsSendTimeUri,
        RtpExtension::kVideoRotationUri,
        RtpExtension::kTransportSequenceNumberUri,
        RtpExtension::kPlayoutDelayUri, RtpExtension::kVideoContentTypeUri,
        RtpExtension::kVideoTimingUri, RtpExtension::kColorSpaceUri,
        RtpExtension::kMidUri, RtpExtension::kRidUri,
        RtpExtension::kRepairedRidUri}) {
    result.emplace_back(uri, id, RtpTransceiverDirection::kSendRecv);
    id = RtpHeaderExtensionId(id.value() + 1);
  }
  result.emplace_back(RtpExtension::kCorruptionDetectionUri, id,
                      /*preferred_encrypt=*/true,
                      RtpTransceiverDirection::kStopped);
  id = RtpHeaderExtensionId(id.value() + 1);
  for (const auto& uri : {RtpExtension::kAbsoluteCaptureTimeUri}) {
    result.emplace_back(uri, id, RtpTransceiverDirection::kStopped);
  }
  result.emplace_back(RtpExtension::kGenericFrameDescriptorUri00, id,
                      trials.IsEnabled("WebRTC-GenericDescriptorAdvertised")
                          ? RtpTransceiverDirection::kSendRecv
                          : RtpTransceiverDirection::kStopped);
  result.emplace_back(RtpExtension::kDependencyDescriptorUri, id,
                      trials.IsEnabled("WebRTC-DependencyDescriptorAdvertised")
                          ? RtpTransceiverDirection::kSendRecv
                          : RtpTransceiverDirection::kStopped);
  result.emplace_back(RtpExtension::kVideoLayersAllocationUri, id,
                      trials.IsEnabled("WebRTC-VideoLayersAllocationAdvertised")
                          ? RtpTransceiverDirection::kSendRecv
                          : RtpTransceiverDirection::kStopped);

  // VideoFrameTrackingId is a test-only extension.
  if (trials.IsEnabled("WebRTC-VideoFrameTrackingIdAdvertised")) {
    result.emplace_back(RtpExtension::kVideoFrameTrackingIdUri, id,
                        RtpTransceiverDirection::kSendRecv);
  }
  return result;
}

// Free function, exported for testing
std::map<uint32_t, VideoSendStream::StreamStats>
MergeInfoAboutOutboundRtpSubstreamsForTesting(
    const std::map<uint32_t, VideoSendStream::StreamStats>& substreams) {
  return MergeInfoAboutOutboundRtpSubstreams(substreams);
}

// --------------- WebRtcVideoSendChannel ----------------------
WebRtcVideoSendChannel::WebRtcVideoSendChannel(
    const Environment& env,
    Call* call,
    const MediaConfig& config,
    const VideoOptions& options,
    const CryptoOptions& crypto_options,
    VideoEncoderFactory* encoder_factory,
    VideoBitrateAllocatorFactory* bitrate_allocator_factory,
    VideoMediaSendChannelInterface::EncoderSwitchRequestCallback
        video_encoder_switch_request_callback,
    absl::AnyInvocable<void()> parameters_changed_callback)
    : MediaChannelUtil(call->network_thread(), config.enable_dscp),
      env_(env),
      worker_thread_(call->worker_thread()),
      sending_(false),
      call_(call),
      video_config_(config.video),
      encoder_factory_(encoder_factory),
      bitrate_allocator_factory_(bitrate_allocator_factory),
      default_send_options_(options),
      last_send_stats_log_ms_(-1),
      crypto_options_(crypto_options),
      parameters_changed_callback_(std::move(parameters_changed_callback)),
      encoder_switch_request_callback_(
          std::move(video_encoder_switch_request_callback)) {}

WebRtcVideoSendChannel::~WebRtcVideoSendChannel() {
  for (auto& kv : send_streams_)
    delete kv.second;
}

scoped_refptr<VideoEncoderConfig::EncoderSpecificSettings>
WebRtcVideoSendChannel::WebRtcVideoSendStream::ConfigureVideoEncoderSettings(
    const Codec& codec) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  bool is_screencast = parameters_.options.is_screencast.value_or(false);
  // No automatic resizing when using simulcast or screencast, or when
  // disabled by field trial flag.
  bool automatic_resize = !disable_automatic_resize_ && !is_screencast &&
                          (parameters_.config.rtp.ssrcs.size() == 1 ||
                           NumActiveStreams(rtp_parameters_) == 1);

  bool denoising;
  bool codec_default_denoising = false;
  if (is_screencast) {
    denoising = false;
  } else {
    // Use codec default if video_noise_reduction is unset.
    codec_default_denoising = !parameters_.options.video_noise_reduction;
    denoising = parameters_.options.video_noise_reduction.value_or(false);
  }

  if (absl::EqualsIgnoreCase(codec.name, kH264CodecName)) {
    return nullptr;
  }
  if (absl::EqualsIgnoreCase(codec.name, kVp8CodecName)) {
    VideoCodecVP8 vp8_settings = VideoEncoder::GetDefaultVp8Settings();
    vp8_settings.automaticResizeOn = automatic_resize;
    // VP8 denoising is enabled by default.
    vp8_settings.denoisingOn = codec_default_denoising ? true : denoising;
    return make_ref_counted<VideoEncoderConfig::Vp8EncoderSpecificSettings>(
        vp8_settings);
  }
  if (absl::EqualsIgnoreCase(codec.name, kVp9CodecName)) {
    VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();

    vp9_settings.numberOfSpatialLayers = std::min<unsigned char>(
        parameters_.config.rtp.ssrcs.size(), kConferenceMaxNumSpatialLayers);
    vp9_settings.numberOfTemporalLayers =
        std::min<unsigned char>(parameters_.config.rtp.ssrcs.size() > 1
                                    ? kConferenceDefaultNumTemporalLayers
                                    : 1,
                                kConferenceMaxNumTemporalLayers);

    // VP9 denoising is disabled by default.
    vp9_settings.denoisingOn = codec_default_denoising ? true : denoising;
    // Disable automatic resize if more than one spatial layer is requested.
    bool vp9_automatic_resize = automatic_resize;
    std::optional<int> num_spatial_layers =
        NumSpatialLayersFromEncoding(rtp_parameters_, /*idx=*/0);
    if (num_spatial_layers && *num_spatial_layers > 1) {
      vp9_automatic_resize = false;
    }
    vp9_settings.automaticResizeOn = vp9_automatic_resize;
    if (!is_screencast) {
      FieldTrialFlag interlayer_pred_experiment_enabled("Enabled");
      FieldTrialEnum<InterLayerPredMode> inter_layer_pred_mode(
          "inter_layer_pred_mode", InterLayerPredMode::kOnKeyPic,
          {{"off", InterLayerPredMode::kOff},
           {"on", InterLayerPredMode::kOn},
           {"onkeypic", InterLayerPredMode::kOnKeyPic}});
      ParseFieldTrial(
          {&interlayer_pred_experiment_enabled, &inter_layer_pred_mode},
          env_.field_trials().Lookup("WebRTC-Vp9InterLayerPred"));
      if (interlayer_pred_experiment_enabled) {
        vp9_settings.interLayerPred = inter_layer_pred_mode;
      } else {
        // Limit inter-layer prediction to key pictures by default.
        vp9_settings.interLayerPred = InterLayerPredMode::kOnKeyPic;
      }

      vp9_settings.flexibleMode = true;
    } else {
      // Multiple spatial layers vp9 screenshare needs flexible mode.
      vp9_settings.flexibleMode = vp9_settings.numberOfSpatialLayers > 1;
      vp9_settings.interLayerPred = InterLayerPredMode::kOn;
    }
    return make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
        vp9_settings);
  }
  if (absl::EqualsIgnoreCase(codec.name, kAv1CodecName)) {
    VideoCodecAV1 av1_settings = {.automatic_resize_on = automatic_resize};
    if (NumSpatialLayersFromEncoding(rtp_parameters_, /*idx=*/0) > 1) {
      av1_settings.automatic_resize_on = false;
    }
    return make_ref_counted<VideoEncoderConfig::Av1EncoderSpecificSettings>(
        av1_settings);
  }
  return nullptr;
}
std::vector<VideoCodecSettings> WebRtcVideoSendChannel::SelectSendVideoCodecs(
    const std::vector<VideoCodecSettings>& remote_mapped_codecs) const {
  std::vector<SdpVideoFormat> sdp_formats =
      encoder_factory_ ? encoder_factory_->GetImplementations()
                       : std::vector<SdpVideoFormat>();

  // The returned vector holds the VideoCodecSettings in term of preference.
  // They are orderd by receive codec preference first and local implementation
  // preference second.
  std::vector<VideoCodecSettings> encoders;
  for (const VideoCodecSettings& remote_codec : remote_mapped_codecs) {
    for (auto format_it = sdp_formats.begin();
         format_it != sdp_formats.end();) {
      // For H264, we will limit the encode level to the remote offered level
      // regardless if level asymmetry is allowed or not. This is strictly not
      // following the spec in https://tools.ietf.org/html/rfc6184#section-8.2.2
      // since we should limit the encode level to the lower of local and remote
      // level when level asymmetry is not allowed.
      // For H.265, the level asymmetry is implicitly allowed. We need to make
      // sure the encode level is set to the remote offered level.
      if (format_it->IsSameCodec(
              {remote_codec.codec.name, remote_codec.codec.params})) {
        encoders.push_back(remote_codec);

        // To allow the VideoEncoderFactory to keep information about which
        // implementation to instantitate when CreateEncoder is called the two
        // parmeter sets are merged.
        encoders.back().codec.params.insert(format_it->parameters.begin(),
                                            format_it->parameters.end());

        format_it = sdp_formats.erase(format_it);
      } else {
        ++format_it;
      }
    }
  }

  return encoders;
}

bool WebRtcVideoSendChannel::GetChangedSenderParameters(
    const VideoSenderParameters& params,
    ChangedSenderParameters* changed_params) const {
  if (!ValidateCodecFormats(params.codecs)) {
    return false;
  }

  auto result = MapCodecs(params.codecs);
  if (!result.ok()) {
    RTC_LOG(LS_ERROR) << "Failure in codec list, error = " << result.error();
    return false;
  }
  const std::vector<VideoCodecSettings>& mapped_codecs = result.value();

  std::vector<VideoCodecSettings> negotiated_codecs =
      SelectSendVideoCodecs(mapped_codecs);

  if (params.is_stream_active && negotiated_codecs.empty()) {
    // This is not a failure but will lead to the answer being rejected.
    RTC_LOG(LS_ERROR) << "No video codecs in common.";
    return true;
  }

  // Never enable sending FlexFEC, unless we are in the experiment.
  if (!env_.field_trials().IsEnabled("WebRTC-FlexFEC-03")) {
    for (VideoCodecSettings& codec : negotiated_codecs)
      codec.flexfec_payload_type = -1;
  }

  std::optional<VideoCodecSettings> force_codec;
  if (!send_streams_.empty()) {
    // Since we do not support mixed-codec simulcast yet,
    // all send streams must have the same codec value.
    auto [ssrc, send_stream] = *send_streams_.begin();
    bool needs_update = false;
    auto rtp_parameters = send_stream->GetRtpParameters();
    if (rtp_parameters.encodings[0].codec) {
      auto matched_codec =
          absl::c_find_if(negotiated_codecs, [&](const auto& negotiated_codec) {
            return IsSameRtpCodec(negotiated_codec.codec,
                                  *rtp_parameters.encodings[0].codec);
          });
      if (matched_codec != negotiated_codecs.end()) {
        force_codec = *matched_codec;
      } else {
        // The requested codec has been negotiated away, we clear it from the
        // parameters.
        for (auto& encoding : rtp_parameters.encodings) {
          if (encoding.codec.has_value()) {
            needs_update = true;
            encoding.codec.reset();
          }
        }
        if (needs_update) {
          send_stream->SetRtpParameters(rtp_parameters, nullptr);
        } else {
          RTC_DCHECK(rtp_parameters == send_stream->GetRtpParameters());
        }
      }
    }
  }

  if (negotiated_codecs_ != negotiated_codecs) {
    if (negotiated_codecs.empty()) {
      changed_params->send_codec = std::nullopt;
    } else if (force_codec) {
      changed_params->send_codec = force_codec;
    } else if (send_codec() != negotiated_codecs.front()) {
      changed_params->send_codec = negotiated_codecs.front();
    }
    changed_params->negotiated_codecs = negotiated_codecs;
  }

  // For mixed-codec simulcast
  std::vector<VideoCodecSettings> send_codecs;
  if (!send_streams_.empty() && !negotiated_codecs.empty()) {
    bool needs_update = false;
    auto [ssrc, send_stream] = *send_streams_.begin();
    auto rtp_parameters = send_stream->GetRtpParameters();
    for (auto& encoding : rtp_parameters.encodings) {
      if (encoding.codec) {
        auto matched_codec = absl::c_find_if(
            negotiated_codecs, [&](const auto& negotiated_codec) {
              return IsSameRtpCodec(negotiated_codec.codec, *encoding.codec);
            });
        if (matched_codec != negotiated_codecs.end()) {
          send_codecs.push_back(*matched_codec);
        } else {
          // The requested codec has been negotiated away, we clear it from the
          // parameters.
          encoding.codec.reset();
          needs_update = true;
          send_codecs.push_back(negotiated_codecs.front());
        }
      } else {
        send_codecs.push_back(negotiated_codecs.front());
      }
    }

    if (needs_update) {
      RTC_DCHECK(send_stream->GetRtpParameters() != rtp_parameters);
      send_stream->SetRtpParameters(rtp_parameters, nullptr);
    }
  }

  if (send_codecs_ != send_codecs) {
    changed_params->send_codecs = send_codecs;
  }

  // Handle RTP header extensions.
  if (params.extmap_allow_mixed != ExtmapAllowMixed()) {
    changed_params->extmap_allow_mixed = params.extmap_allow_mixed;
  }
  std::vector<RtpExtension> filtered_extensions =
      FilterRtpExtensions(params.extensions, RtpExtension::IsSupportedForVideo,
                          true, env_.field_trials());
  if (send_rtp_extensions_ != filtered_extensions) {
    changed_params->rtp_header_extensions =
        std::optional<std::vector<RtpExtension>>(filtered_extensions);
  }

  if (params.mid != send_params_.mid) {
    changed_params->mid = params.mid;
  }

  // Handle max bitrate.
  if (params.max_bandwidth_bps != send_params_.max_bandwidth_bps &&
      params.max_bandwidth_bps >= -1) {
    // 0 or -1 uncaps max bitrate.
    // TODO(pbos): Reconsider how 0 should be treated. It is not mentioned as a
    // special value and might very well be used for stopping sending.
    changed_params->max_bandwidth_bps =
        params.max_bandwidth_bps == 0 ? -1 : params.max_bandwidth_bps;
  }

  // Handle conference mode.
  if (params.conference_mode != send_params_.conference_mode) {
    changed_params->conference_mode = params.conference_mode;
  }

  // Handle RTCP mode.
  if (params.rtcp.reduced_size != send_params_.rtcp.reduced_size) {
    changed_params->rtcp_mode =
        params.rtcp.reduced_size ? RtcpMode::kReducedSize : RtcpMode::kCompound;
  }

  return true;
}

bool WebRtcVideoSendChannel::SetSenderParameters(
    const VideoSenderParameters& params) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  TRACE_EVENT0("webrtc", "WebRtcVideoSendChannel::SetSenderParameters");
  RTC_LOG(LS_INFO) << "SetSenderParameters: " << params.ToString();
  ChangedSenderParameters changed_params;
  if (!GetChangedSenderParameters(params, &changed_params)) {
    return false;
  }

  if (changed_params.negotiated_codecs) {
    for (const auto& send_codec : *changed_params.negotiated_codecs)
      RTC_LOG(LS_INFO) << "Negotiated codec: " << send_codec.codec.ToString();
  }

  send_params_ = params;
  return ApplyChangedParams(changed_params);
}

void WebRtcVideoSendChannel::RequestEncoderSwitch(
    std::optional<SdpVideoFormat> format,
    bool allow_default_fallback) {
  auto task = SafeTask(task_safety_.flag(), [this, format = std::move(format),
                                             allow_default_fallback]() mutable {
    RTC_DCHECK_RUN_ON(worker_thread_);
    ApplyEncoderSwitch(std::move(format), allow_default_fallback);
  });
  if (encoder_switch_request_callback_) {
    encoder_switch_request_callback_(std::move(task));
  } else {
    worker_thread_->PostTask(std::move(task));
  }
}

void WebRtcVideoSendChannel::ApplyEncoderSwitch(
    std::optional<SdpVideoFormat> format,
    bool allow_default_fallback) {
  RTC_DCHECK_RUN_ON(worker_thread_);

  ChangedSenderParameters params;
  if (!format) {
    if (available_negotiated_codecs_.size() <= 1) {
      RTC_LOG(LS_WARNING)
          << "Encoder failed but no fallback codec is available";
      return;
    }

    // Find and remove the current codec. It might not be the first on the list
    // because of SetParameters().
    if (send_codec().has_value()) {
      auto it = std::find(available_negotiated_codecs_.begin(),
                          available_negotiated_codecs_.end(), *send_codec());
      if (it != available_negotiated_codecs_.end()) {
        available_negotiated_codecs_.erase(it);
      }
    }
    params.send_codec = available_negotiated_codecs_.front();
  } else {
    auto it = absl::c_find_if(
        available_negotiated_codecs_,
        [&](const VideoCodecSettings& codec_setting) {
          return format->IsSameCodec(
              {codec_setting.codec.name, codec_setting.codec.params});
        });
    if (it == available_negotiated_codecs_.end()) {
      RTC_LOG(LS_WARNING) << "Failed to switch encoder to: "
                          << format->ToString()
                          << ". Is default fallback allowed: "
                          << allow_default_fallback;

      if (allow_default_fallback) {
        ApplyEncoderSwitch(std::nullopt, true);
      }
      return;
    }

    VideoCodecSettings new_codec_setting = *it;
    for (const auto& kv : format->parameters) {
      new_codec_setting.codec.params[kv.first] = kv.second;
    }

    if (send_codec() == new_codec_setting) {
      return;
    }

    params.send_codec = new_codec_setting;
  }

  if (ApplyChangedParams(params, /*applying_encoder_switch=*/true) &&
      parameters_changed_callback_) {
    parameters_changed_callback_();
  }
}

bool WebRtcVideoSendChannel::ApplyChangedParams(
    const ChangedSenderParameters& changed_params,
    bool applying_encoder_switch) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  if (changed_params.negotiated_codecs)
    negotiated_codecs_ = *changed_params.negotiated_codecs;
  if (!applying_encoder_switch)
    available_negotiated_codecs_ = negotiated_codecs_;

  if (changed_params.send_codec)
    send_codec() = changed_params.send_codec;

  if (changed_params.send_codecs) {
    send_codecs_ = *changed_params.send_codecs;
  } else {
    send_codecs_.clear();
  }

  if (changed_params.extmap_allow_mixed) {
    SetExtmapAllowMixed(*changed_params.extmap_allow_mixed);
  }
  if (changed_params.rtp_header_extensions) {
    send_rtp_extensions_ = *changed_params.rtp_header_extensions;
  }

  if (changed_params.send_codec || changed_params.max_bandwidth_bps) {
    if (send_params_.max_bandwidth_bps == -1) {
      // Unset the global max bitrate (max_bitrate_bps) if max_bandwidth_bps is
      // -1, which corresponds to no "b=AS" attribute in SDP. Note that the
      // global max bitrate may be set below in GetBitrateConfigForCodec, from
      // the codec max bitrate.
      // TODO(pbos): This should be reconsidered (codec max bitrate should
      // probably not affect global call max bitrate).
      bitrate_config_.max_bitrate_bps = -1;
    }

    if (send_codec()) {
      // TODO(holmer): Changing the codec parameters shouldn't necessarily mean
      // that we change the min/max of bandwidth estimation. Reevaluate this.
      bitrate_config_ = GetBitrateConfigForCodec(send_codec()->codec);
      if (!changed_params.send_codec) {
        // If the codec isn't changing, set the start bitrate to -1 which means
        // "unchanged" so that BWE isn't affected.
        bitrate_config_.start_bitrate_bps = -1;
      }
    }

    if (send_params_.max_bandwidth_bps >= 0) {
      // Note that max_bandwidth_bps intentionally takes priority over the
      // bitrate config for the codec. This allows FEC to be applied above the
      // codec target bitrate.
      // TODO(pbos): Figure out whether b=AS means max bitrate for this
      // WebRtcVideoSendChannel (in which case we're good), or per sender
      // (SSRC), in which case this should not set a BitrateConstraints but
      // rather reconfigure all senders.
      bitrate_config_.max_bitrate_bps = send_params_.max_bandwidth_bps == 0
                                            ? -1
                                            : send_params_.max_bandwidth_bps;
    }

    call_->GetTransportControllerSend()->SetSdpBitrateParameters(
        bitrate_config_);
  }

  for (auto& kv : send_streams_) {
    kv.second->SetSenderParameters(changed_params);
  }
  return true;
}

RtpParameters WebRtcVideoSendChannel::GetRtpSendParameters(
    uint32_t ssrc) const {
  RTC_DCHECK_RUN_ON(worker_thread_);
  auto it = send_streams_.find(ssrc);
  if (it == send_streams_.end()) {
    RTC_LOG(LS_WARNING) << "Attempting to get RTP send parameters for stream "
                           "with ssrc "
                        << ssrc << " which doesn't exist.";
    return RtpParameters();
  }

  RtpParameters rtp_params = it->second->GetRtpParameters();
  // Need to add the common list of codecs to the send stream-specific
  // RTP parameters.
  for (const Codec& codec : send_params_.codecs) {
    if (send_codec() && send_codec()->codec.id == codec.id) {
      // Put the current send codec to the front of the codecs list.
      RTC_DCHECK_EQ(codec.name, send_codec()->codec.name);
      rtp_params.codecs.insert(rtp_params.codecs.begin(),
                               codec.ToCodecParameters());
    } else {
      rtp_params.codecs.push_back(codec.ToCodecParameters());
    }
  }

  return rtp_params;
}

absl::AnyInvocable<RtpParameters(uint32_t)>
WebRtcVideoSendChannel::GetRtpSendParametersCallback() const {
  return [this, safety = task_safety_.flag()](
             uint32_t ssrc) mutable -> RtpParameters {
    if (!safety->alive())
      return RtpParameters();
    return GetRtpSendParameters(ssrc);
  };
}

RTCError WebRtcVideoSendChannel::SetRtpSendParameters(
    uint32_t ssrc,
    const RtpParameters& parameters,
    SetParametersCallback callback) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  TRACE_EVENT0("webrtc", "WebRtcVideoSendChannel::SetRtpSendParameters");
  auto it = send_streams_.find(ssrc);
  if (it == send_streams_.end()) {
    RTC_LOG(LS_ERROR) << "Attempting to set RTP send parameters for stream "
                         "with ssrc "
                      << ssrc << " which doesn't exist.";
    return InvokeSetParametersCallback(callback,
                                       RTCError(RTCErrorType::INTERNAL_ERROR));
  }

  // TODO(deadbeef): Handle setting parameters with a list of codecs in a
  // different order (which should change the send codec).
  const RtpParameters current_parameters = GetRtpSendParameters(ssrc);
  if (current_parameters.codecs != parameters.codecs) {
    RTC_DLOG(LS_ERROR) << "Using SetParameters to change the set of codecs "
                          "is not currently supported.";
    return InvokeSetParametersCallback(callback,
                                       RTCError(RTCErrorType::INTERNAL_ERROR));
  }

  if (!parameters.encodings.empty()) {
    // Note that these values come from:
    // https://tools.ietf.org/html/draft-ietf-tsvwg-rtcweb-qos-16#section-5
    // TODO(deadbeef): Change values depending on whether we are sending a
    // keyframe or non-keyframe.
    DiffServCodePoint new_dscp = DSCP_DEFAULT;
    switch (parameters.encodings[0].network_priority) {
      case Priority::kVeryLow:
        new_dscp = DSCP_CS1;
        break;
      case Priority::kLow:
        new_dscp = DSCP_DEFAULT;
        break;
      case Priority::kMedium:
        new_dscp = DSCP_AF42;
        break;
      case Priority::kHigh:
        new_dscp = DSCP_AF41;
        break;
    }

    if (send_codec_ &&
        std::any_of(parameters.encodings.begin(), parameters.encodings.end(),
                    [](const auto& e) { return e.codec; })) {
      std::vector<VideoCodecSettings> send_codecs;
      auto error =
          ResolveSendCodecs(*send_codec_, send_codecs_, parameters.encodings,
                            negotiated_codecs_, &send_codecs);
      if (!error.ok()) {
        return InvokeSetParametersCallback(callback, error);
      }

      if (send_codecs_ != send_codecs) {
        ChangedSenderParameters params;
        if (!send_codecs.empty()) {
          params.send_codec = send_codecs[0];
        }
        params.send_codecs = send_codecs;
        ApplyChangedParams(params);
      }
    }

    SetPreferredDscp(new_dscp);
  }

  return it->second->SetRtpParameters(parameters, std::move(callback));
}

std::optional<Codec> WebRtcVideoSendChannel::GetSendCodec() const {
  RTC_DCHECK_RUN_ON(worker_thread_);
  if (!send_codec()) {
    RTC_LOG(LS_VERBOSE) << "GetSendCodec: No send codec set.";
    return std::nullopt;
  }
  return send_codec()->codec;
}

bool WebRtcVideoSendChannel::SetSend(bool send) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  TRACE_EVENT0("webrtc", "WebRtcVideoSendChannel::SetSend");
  RTC_LOG(LS_VERBOSE) << "SetSend: " << (send ? "true" : "false");
  if (send && !send_codec()) {
    RTC_DLOG(LS_ERROR) << "SetSend(true) called before setting codec.";
    return false;
  }
  for (const auto& kv : send_streams_) {
    kv.second->SetSend(send);
  }
  sending_ = send;
  return true;
}

bool WebRtcVideoSendChannel::SetVideoSend(
    uint32_t ssrc,
    const VideoOptions* options,
    VideoSourceInterface<VideoFrame>* source) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  TRACE_EVENT0("webrtc", "SetVideoSend");
  RTC_DCHECK(ssrc != 0);
  RTC_LOG(LS_INFO) << "SetVideoSend (ssrc= " << ssrc << ", options: "
                   << (options ? options->ToString() : "nullptr")
                   << ", source = " << (source ? "(source)" : "nullptr") << ")";

  const auto& kv = send_streams_.find(ssrc);
  if (kv == send_streams_.end()) {
    // Allow unknown ssrc only if source is null.
    RTC_CHECK(source == nullptr);
    RTC_LOG(LS_ERROR) << "No sending stream on ssrc " << ssrc;
    return false;
  }

  return kv->second->SetVideoSend(options, source);
}

bool WebRtcVideoSendChannel::ValidateSendSsrcAvailability(
    const StreamParams& sp) const {
  for (uint32_t ssrc : sp.ssrcs) {
    if (send_ssrcs_.find(ssrc) != send_ssrcs_.end()) {
      RTC_LOG(LS_ERROR) << "Send stream with SSRC '" << ssrc
                        << "' already exists.";
      return false;
    }
  }
  return true;
}
bool WebRtcVideoSendChannel::AddSendStream(const StreamParams& sp) {
  RTC_DCHECK_RUN_ON(worker_thread_);

  RTC_LOG(LS_INFO) << "AddSendStream: " << sp.ToString();
  if (!ValidateStreamParams(sp))
    return false;

  if (!ValidateSendSsrcAvailability(sp))
    return false;

  for (uint32_t used_ssrc : sp.ssrcs)
    send_ssrcs_.insert(used_ssrc);

  VideoSendStream::Config config(transport());

  for (const RidDescription& rid : sp.rids()) {
    config.rtp.rids.push_back(rid.rid);
  }

  config.suspend_below_min_bitrate = video_config_.suspend_below_min_bitrate;
  config.periodic_alr_bandwidth_probing =
      video_config_.periodic_alr_bandwidth_probing;
  config.encoder_settings.experiment_cpu_load_estimator =
      video_config_.experiment_cpu_load_estimator;
  config.encoder_settings.encoder_factory = encoder_factory_;
  config.encoder_settings.bitrate_allocator_factory =
      bitrate_allocator_factory_;

  config.crypto_options = crypto_options_;
  config.rtp.sframe_options = sframe_options_;
  config.rtp.extmap_allow_mixed = ExtmapAllowMixed();
  config.rtcp_report_interval_ms = video_config_.rtcp_report_interval_ms;
  config.rtp.enable_send_packet_batching =
      video_config_.enable_send_packet_batching;

  WebRtcVideoSendStream* stream = new WebRtcVideoSendStream(
      this, env_, call_, sp, std::move(config), default_send_options_,
      video_config_.enable_cpu_adaptation, bitrate_config_.max_bitrate_bps,
      send_codec(), send_codecs_, send_rtp_extensions_, send_params_);

  uint32_t ssrc = sp.first_ssrc();
  RTC_DCHECK(ssrc != 0);
  send_streams_[ssrc] = stream;

  if (ssrc_list_changed_callback_) {
    ssrc_list_changed_callback_(send_ssrcs_);
  }

  if (sending_) {
    stream->SetSend(true);
  }

  return true;
}

bool WebRtcVideoSendChannel::RemoveSendStream(uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_LOG(LS_INFO) << "RemoveSendStream: " << ssrc;

  WebRtcVideoSendStream* removed_stream;
  auto it = send_streams_.find(ssrc);
  if (it == send_streams_.end()) {
    return false;
  }

  for (uint32_t old_ssrc : it->second->GetSsrcs())
    send_ssrcs_.erase(old_ssrc);

  removed_stream = it->second;
  send_streams_.erase(it);

  // Switch receiver report SSRCs, in case the one in use is no longer valid.
  if (ssrc_list_changed_callback_) {
    ssrc_list_changed_callback_(send_ssrcs_);
  }

  delete removed_stream;

  return true;
}

bool WebRtcVideoSendChannel::GetStats(VideoMediaSendInfo* info) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  TRACE_EVENT0("webrtc", "WebRtcVideoSendChannel::GetSendStats");

  info->Clear();
  if (send_streams_.empty()) {
    return true;
  }

  // Log stats periodically.
  bool log_stats = false;
  int64_t now_ms = env_.clock().TimeInMilliseconds();
  if (last_send_stats_log_ms_ == -1 ||
      now_ms - last_send_stats_log_ms_ > kStatsLogIntervalMs) {
    last_send_stats_log_ms_ = now_ms;
    log_stats = true;
  }

  info->Clear();
  FillSenderStats(info, log_stats);
  FillSendCodecStats(info);
  // TODO(holmer): We should either have rtt available as a metric on
  // VideoSend/ReceiveStreams, or we should remove rtt from VideoSenderInfo.
  Call::Stats stats = call_->GetStats();
  if (stats.rtt_ms != -1) {
    for (size_t i = 0; i < info->senders.size(); ++i) {
      info->senders[i].rtt_ms = stats.rtt_ms;
    }
    for (size_t i = 0; i < info->aggregated_senders.size(); ++i) {
      info->aggregated_senders[i].rtt_ms = stats.rtt_ms;
    }
  }

  if (log_stats)
    RTC_LOG(LS_INFO) << stats.ToString(now_ms);

  return true;
}
void WebRtcVideoSendChannel::FillSenderStats(
    VideoMediaSendInfo* video_media_info,
    bool log_stats) {
  for (const auto& it : send_streams_) {
    auto infos = it.second->GetPerLayerVideoSenderInfos(log_stats);
    if (infos.empty())
      continue;
    video_media_info->aggregated_senders.push_back(
        it.second->GetAggregatedVideoSenderInfo(infos));
    for (auto&& info : infos) {
      video_media_info->senders.push_back(info);
    }
  }
}

void WebRtcVideoSendChannel::FillBitrateInfo(
    BandwidthEstimationInfo* bwe_info) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  for (const auto& it : send_streams_) {
    it.second->FillBitrateInfo(bwe_info);
  }
}

void WebRtcVideoSendChannel::FillSendCodecStats(
    VideoMediaSendInfo* video_media_info) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  if (!send_codec()) {
    return;
  }
  // Note: since RTP stats don't account for RTX and FEC separately (see
  // https://w3c.github.io/webrtc-stats/#dom-rtcstatstype-outbound-rtp)
  // we can omit the codec information for those here and only insert the
  // primary codec that is being used to send here.
  video_media_info->send_codecs.insert(std::make_pair(
      send_codec()->codec.id.value(), send_codec()->codec.ToCodecParameters()));

  for (const auto& it : send_codecs_) {
    auto codec_param_it = video_media_info->send_codecs.find(it.codec.id);
    if (codec_param_it == video_media_info->send_codecs.end()) {
      video_media_info->send_codecs.insert(
          std::make_pair(it.codec.id.value(), it.codec.ToCodecParameters()));
    }
  }
}

absl::AnyInvocable<std::optional<VideoMediaSendInfo>()>
WebRtcVideoSendChannel::GetStatsTask() {
  return [this, safety = task_safety_.flag()]() mutable
             -> std::optional<VideoMediaSendInfo> {
    if (!safety->alive()) {
      return std::nullopt;
    }
    VideoMediaSendInfo info;
    if (GetStats(&info)) {
      return info;
    }
    return std::nullopt;
  };
}

void WebRtcVideoSendChannel::OnPacketSent(const SentPacketInfo& sent_packet) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  // TODO(tommi): We shouldn't need to go through call_ to deliver this
  // notification. We should already have direct access to
  // video_send_delay_stats_ and transport_send_ptr_ via `stream_`.
  // So we should be able to remove OnSentPacket from Call and handle this per
  // channel instead. At the moment Call::OnSentPacket calls OnSentPacket for
  // the video stats, for all sent packets, including audio, which causes
  // unnecessary lookups.
  call_->OnSentPacket(sent_packet);
}

void WebRtcVideoSendChannel::OnReadyToSend(bool ready) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  RTC_LOG(LS_VERBOSE) << "OnReadyToSend: " << (ready ? "Ready." : "Not ready.");
  call_->SignalChannelNetworkState(MediaType::VIDEO,
                                   ready ? kNetworkUp : kNetworkDown);
}

void WebRtcVideoSendChannel::OnNetworkRouteChanged(
    absl::string_view transport_name,
    const NetworkRoute& network_route) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  worker_thread_->PostTask(SafeTask(
      task_safety_.flag(),
      [this, name = std::string(transport_name), route = network_route] {
        RTC_DCHECK_RUN_ON(worker_thread_);
        RtpTransportControllerSendInterface* transport =
            call_->GetTransportControllerSend();
        transport->OnNetworkRouteChanged(name, route);
      }));
}

void WebRtcVideoSendChannel::SetInterface(MediaChannelNetworkInterface* iface) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  MediaChannelUtil::SetInterface(iface);

  // Speculative change to increase the outbound socket buffer size.
  // In b/15152257, we are seeing a significant number of packets discarded
  // due to lack of socket buffer space, although it's not yet clear what the
  // ideal value should be.
  const std::string group_name_send_buf_size =
      env_.field_trials().Lookup("WebRTC-SendBufferSizeBytes");
  int send_buffer_size = kVideoRtpSendBufferSize;
  if (!group_name_send_buf_size.empty() &&
      (sscanf(group_name_send_buf_size.c_str(), "%d", &send_buffer_size) != 1 ||
       send_buffer_size <= 0)) {
    RTC_LOG(LS_WARNING) << "Invalid send buffer size: "
                        << group_name_send_buf_size;
    send_buffer_size = kVideoRtpSendBufferSize;
  }

  MediaChannelUtil::SetOption(MediaChannelNetworkInterface::ST_RTP,
                              Socket::OPT_SNDBUF, send_buffer_size);
}

void WebRtcVideoSendChannel::SetFrameEncryptor(
    uint32_t ssrc,
    scoped_refptr<FrameEncryptorInterface> frame_encryptor) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  auto matching_stream = send_streams_.find(ssrc);
  if (matching_stream != send_streams_.end()) {
    matching_stream->second->SetFrameEncryptor(frame_encryptor);
  } else {
    RTC_LOG(LS_ERROR) << "No stream found to attach frame encryptor";
  }
}

void WebRtcVideoSendChannel::EnableSframe() {
  RTC_DCHECK_RUN_ON(worker_thread_);

  if (sframe_options_.required) {
    return;
  }

  sframe_options_.required = true;

  for (auto& [ssrc, stream] : send_streams_) {
    stream->UpdateSframeOptions(sframe_options_);
  }
}

void WebRtcVideoSendChannel::SetSframeEncryptionConfig(
    SframeEncryptionConfig sframe_encryption_config) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  sframe_options_.encryption_config = std::move(sframe_encryption_config);

  for (auto& [ssrc, stream] : send_streams_) {
    stream->UpdateSframeOptions(sframe_options_);
  }
}

void WebRtcVideoSendChannel::SetEncoderSelector(
    uint32_t ssrc,
    scoped_refptr<VideoEncoderFactory::EncoderSelectorInterface>
        encoder_selector) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  auto matching_stream = send_streams_.find(ssrc);
  if (matching_stream != send_streams_.end()) {
    matching_stream->second->SetEncoderSelector(std::move(encoder_selector));
  } else {
    RTC_LOG(LS_ERROR) << "No stream found to attach encoder selector";
  }
}

bool WebRtcVideoSendChannel::SetEncoderFactoryOverride(
    uint32_t ssrc,
    absl_nonnull std::unique_ptr<VideoEncoderFactory> encoder_factory) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  auto matching_stream = send_streams_.find(ssrc);
  if (matching_stream == send_streams_.end()) {
    RTC_LOG(LS_ERROR)
        << "No stream found to set video encoder factory override";
    return false;
  }
  matching_stream->second->SetEncoderFactoryOverride(
      std::move(encoder_factory));
  return true;
}

void WebRtcVideoSendChannel::ResetEncoderFactoryOverride(uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  auto matching_stream = send_streams_.find(ssrc);
  if (matching_stream != send_streams_.end()) {
    matching_stream->second->ResetEncoderFactoryOverride();
  } else {
    RTC_LOG(LS_ERROR)
        << "No stream found to reset video encoder factory override";
  }
}

WebRtcVideoSendChannel::WebRtcVideoSendStream::VideoSendStreamParameters::
    VideoSendStreamParameters(
        VideoSendStream::Config config,
        const VideoOptions& options,
        int max_bitrate_bps,
        const std::optional<VideoCodecSettings>& codec_settings,
        const std::vector<VideoCodecSettings>& codec_settings_list)
    : config(std::move(config)),
      options(options),
      max_bitrate_bps(max_bitrate_bps),
      conference_mode(false),
      codec_settings(codec_settings),
      codec_settings_list(codec_settings_list) {}

WebRtcVideoSendChannel::WebRtcVideoSendStream::WebRtcVideoSendStream(
    WebRtcVideoSendChannel* send_channel,
    const Environment& env,
    Call* call,
    const StreamParams& sp,
    VideoSendStream::Config config,
    const VideoOptions& options,
    bool enable_cpu_overuse_detection,
    int max_bitrate_bps,
    const std::optional<VideoCodecSettings>& codec_settings,
    const std::vector<VideoCodecSettings>& codec_settings_list,
    const std::optional<std::vector<RtpExtension>>& rtp_extensions,
    // TODO(deadbeef): Don't duplicate information between send_params,
    // rtp_extensions, options, etc.
    const VideoSenderParameters& send_params)
    : send_channel_(send_channel),
      env_(env),
      thread_checker_(call->worker_thread()),
      ssrcs_(sp.ssrcs),
      ssrc_groups_(sp.ssrc_groups),
      call_(call),
      enable_cpu_overuse_detection_(enable_cpu_overuse_detection),
      source_(nullptr),
      stream_(nullptr),
      parameters_(std::move(config),
                  options,
                  max_bitrate_bps,
                  codec_settings,
                  codec_settings_list),
      rtp_parameters_(CreateRtpParametersWithEncodings(sp)),
      sending_(false),
      disable_automatic_resize_(env_.field_trials().IsEnabled(
          "WebRTC-Video-DisableAutomaticResize")) {
  // Maximum packet size may come in RtpConfig from external transport, for
  // example from QuicTransportInterface implementation, so do not exceed
  // given max_packet_size.
  parameters_.config.rtp.max_packet_size =
      std::min<size_t>(parameters_.config.rtp.max_packet_size, kVideoMtu);
  parameters_.conference_mode = send_params.conference_mode;

  sp.GetPrimarySsrcs(&parameters_.config.rtp.ssrcs);

  // ValidateStreamParams should prevent this from happening.
  RTC_CHECK(!parameters_.config.rtp.ssrcs.empty());
  rtp_parameters_.encodings[0].ssrc = parameters_.config.rtp.ssrcs[0];

  // RTX.
  sp.GetFidSsrcs(parameters_.config.rtp.ssrcs,
                 &parameters_.config.rtp.rtx.ssrcs);

  // FlexFEC SSRCs.
  // TODO(brandtr): This code needs to be generalized when we add support for
  // multistream protection.
  if (env_.field_trials().IsEnabled("WebRTC-FlexFEC-03")) {
    uint32_t flexfec_ssrc;
    bool flexfec_enabled = false;
    for (uint32_t primary_ssrc : parameters_.config.rtp.ssrcs) {
      if (sp.GetFecFrSsrc(primary_ssrc, &flexfec_ssrc)) {
        if (flexfec_enabled) {
          RTC_LOG(LS_INFO)
              << "Multiple FlexFEC streams in local SDP, but "
                 "our implementation only supports a single FlexFEC "
                 "stream. Will not enable FlexFEC for proposed "
                 "stream with SSRC: "
              << flexfec_ssrc << ".";
          continue;
        }

        flexfec_enabled = true;
        parameters_.config.rtp.flexfec.ssrc = flexfec_ssrc;
        parameters_.config.rtp.flexfec.protected_media_ssrcs = {primary_ssrc};
      }
    }
  }

  parameters_.config.rtp.c_name = sp.cname;
  if (rtp_extensions) {
    parameters_.config.rtp.extensions = *rtp_extensions;
    rtp_parameters_.header_extensions = *rtp_extensions;
  }
  parameters_.config.rtp.rtcp_mode = send_params.rtcp.reduced_size
                                         ? RtcpMode::kReducedSize
                                         : RtcpMode::kCompound;
  parameters_.config.rtp.mid = send_params.mid;
  rtp_parameters_.rtcp.reduced_size = send_params.rtcp.reduced_size;

  if (codec_settings) {
    SetCodec(*codec_settings, codec_settings_list);
  }
}

WebRtcVideoSendChannel::WebRtcVideoSendStream::~WebRtcVideoSendStream() {
  if (stream_ != nullptr) {
    call_->DestroyVideoSendStream(stream_);
  }
}

bool WebRtcVideoSendChannel::WebRtcVideoSendStream::SetVideoSend(
    const VideoOptions* options,
    VideoSourceInterface<VideoFrame>* source) {
  TRACE_EVENT0("webrtc", "WebRtcVideoSendStream::SetVideoSend");
  RTC_DCHECK_RUN_ON(&thread_checker_);

  bool reconfiguration_needed = false;
  if (options) {
    VideoOptions old_options = parameters_.options;
    parameters_.options.SetAll(*options);
    if (parameters_.options.is_screencast.value_or(false) !=
            old_options.is_screencast.value_or(false) &&
        parameters_.codec_settings) {
      // If screen content settings change, we may need to recreate the codec
      // instance so that the correct type is used.

      SetCodec(*parameters_.codec_settings, parameters_.codec_settings_list);
      // Mark screenshare parameter as being updated, then test for any other
      // changes that may require codec reconfiguration.
      old_options.is_screencast = options->is_screencast;
    }
    if (parameters_.options != old_options) {
      reconfiguration_needed = true;
    }
  }

  if (source_ && stream_) {
    stream_->SetSource(
        nullptr, DegradationPreference::MAINTAIN_FRAMERATE_AND_RESOLUTION);
    if (source && source != source_) {
      reconfiguration_needed = true;
    }
  }

  if (reconfiguration_needed) {
    ReconfigureEncoder(nullptr);
  }

  // Switch to the new source.
  source_ = source;
  if (source && stream_) {
    stream_->SetSource(source_, GetDegradationPreference());
  }
  return true;
}

DegradationPreference
WebRtcVideoSendChannel::WebRtcVideoSendStream::GetDegradationPreference()
    const {
  // Do not adapt resolution for screen content as this will likely
  // result in blurry and unreadable text.
  // `this` acts like a VideoSource to make sure SinkWants are handled on the
  // correct thread.
  if (!enable_cpu_overuse_detection_) {
    return DegradationPreference::MAINTAIN_FRAMERATE_AND_RESOLUTION;
  }

  DegradationPreference degradation_preference;
  if (rtp_parameters_.degradation_preference.has_value()) {
    degradation_preference = *rtp_parameters_.degradation_preference;
  } else {
    if (parameters_.options.content_hint ==
        VideoTrackInterface::ContentHint::kFluid) {
      degradation_preference = DegradationPreference::MAINTAIN_FRAMERATE;
    } else if (parameters_.options.is_screencast.value_or(false) ||
               parameters_.options.content_hint ==
                   VideoTrackInterface::ContentHint::kDetailed ||
               parameters_.options.content_hint ==
                   VideoTrackInterface::ContentHint::kText) {
      degradation_preference = DegradationPreference::MAINTAIN_RESOLUTION;
    } else if (env_.field_trials().IsEnabled(
                   "WebRTC-Video-BalancedDegradation")) {
      // Standard wants balanced by default, but it needs to be tuned first.
      degradation_preference = DegradationPreference::BALANCED;
    } else {
      // Keep MAINTAIN_FRAMERATE by default until BALANCED has been tuned for
      // all codecs and launched.
      degradation_preference = DegradationPreference::MAINTAIN_FRAMERATE;
    }
  }

  return degradation_preference;
}

const std::vector<uint32_t>&
WebRtcVideoSendChannel::WebRtcVideoSendStream::GetSsrcs() const {
  return ssrcs_;
}

void WebRtcVideoSendChannel::WebRtcVideoSendStream::SetCodec(
    const VideoCodecSettings& codec_settings,
    const std::vector<VideoCodecSettings>& codec_settings_list) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  FallbackToDefaultScalabilityModeIfNotSupported(
      codec_settings.codec, parameters_.config, rtp_parameters_.encodings);

  parameters_.encoder_config = CreateVideoEncoderConfig(codec_settings.codec);
  RTC_DCHECK_GT(parameters_.encoder_config.number_of_streams, 0);

  parameters_.config.rtp.payload_name = codec_settings.codec.name;
  parameters_.config.rtp.payload_type = codec_settings.codec.id;
  parameters_.config.rtp.raw_payload =
      codec_settings.codec.packetization == kPacketizationParamRaw;
  parameters_.config.rtp.ulpfec = codec_settings.ulpfec;
  parameters_.config.rtp.flexfec.payload_type =
      codec_settings.flexfec_payload_type;

  // Set RTX payload type if RTX is enabled.
  if (!parameters_.config.rtp.rtx.ssrcs.empty()) {
    if (codec_settings.rtx_payload_type == -1) {
      RTC_LOG(LS_WARNING)
          << "RTX SSRCs configured but there's no configured RTX "
             "payload type. Ignoring.";
      parameters_.config.rtp.rtx.ssrcs.clear();
    } else {
      parameters_.config.rtp.rtx.payload_type = codec_settings.rtx_payload_type;
    }
  }

  const bool has_lntf = HasLntf(codec_settings.codec);
  parameters_.config.rtp.lntf.enabled = has_lntf;
  parameters_.config.encoder_settings.capabilities.loss_notification = has_lntf;

  parameters_.config.rtp.nack.rtp_history_ms =
      HasNack(codec_settings.codec) ? kNackHistoryMs : 0;

  parameters_.codec_settings = codec_settings;

  // Settings for mixed-codec simulcast.
  if (codec_settings_list.empty()) {
    parameters_.config.rtp.stream_configs.clear();
  } else {
    if (parameters_.config.rtp.ssrcs.size() == codec_settings_list.size()) {
      parameters_.config.rtp.stream_configs.resize(
          parameters_.config.rtp.ssrcs.size());
      for (size_t i = 0; i < codec_settings_list.size(); i++) {
        auto& stream_config = parameters_.config.rtp.stream_configs[i];
        const auto& cs = codec_settings_list[i];
        stream_config.ssrc = parameters_.config.rtp.ssrcs[i];
        if (i < parameters_.config.rtp.rids.size()) {
          stream_config.rid = parameters_.config.rtp.rids[i];
        }
        stream_config.payload_name = cs.codec.name;
        stream_config.payload_type = cs.codec.id;
        stream_config.raw_payload =
            cs.codec.packetization == kPacketizationParamRaw;
        if (i < parameters_.config.rtp.rtx.ssrcs.size()) {
          auto& rtx = stream_config.rtx.emplace(
              decltype(stream_config.rtx)::value_type());
          rtx.ssrc = parameters_.config.rtp.rtx.ssrcs[i];
          rtx.payload_type = cs.rtx_payload_type;
        }
      }
    } else {
      // TODO(crbug.com/378724147): We need to investigate when it
      // has mismatched sizes.
      RTC_DCHECK_EQ(parameters_.config.rtp.ssrcs.size(),
                    codec_settings_list.size());

      RTC_LOG(LS_ERROR) << "Mismatched sizes between codec_settings_list:"
                        << codec_settings_list.size()
                        << ", parameters_.config.rtp.ssrcs:"
                        << parameters_.config.rtp.ssrcs.size();
    }
  }

  parameters_.codec_settings_list = codec_settings_list;

  // TODO(bugs.webrtc.org/8830): Avoid recreation, it should be enough to call
  // ReconfigureEncoder.
  RTC_LOG(LS_INFO) << "RecreateWebRtcStream (send) because of SetCodec.";
  RecreateWebRtcStream();
}

void WebRtcVideoSendChannel::WebRtcVideoSendStream::SetSenderParameters(
    const ChangedSenderParameters& params) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  // `recreate_stream` means construction-time parameters have changed and the
  // sending stream needs to be reset with the new config.
  bool recreate_stream = false;
  if (params.rtcp_mode) {
    parameters_.config.rtp.rtcp_mode = *params.rtcp_mode;
    rtp_parameters_.rtcp.reduced_size =
        parameters_.config.rtp.rtcp_mode == RtcpMode::kReducedSize;
    recreate_stream = true;
  }
  if (params.extmap_allow_mixed) {
    parameters_.config.rtp.extmap_allow_mixed = *params.extmap_allow_mixed;
    recreate_stream = true;
  }
  if (params.rtp_header_extensions) {
    parameters_.config.rtp.extensions = *params.rtp_header_extensions;
    rtp_parameters_.header_extensions = *params.rtp_header_extensions;
    recreate_stream = true;
  }
  if (params.mid) {
    parameters_.config.rtp.mid = *params.mid;
    recreate_stream = true;
  }
  if (params.max_bandwidth_bps) {
    parameters_.max_bitrate_bps = *params.max_bandwidth_bps;
    ReconfigureEncoder(nullptr);
  }
  if (params.conference_mode) {
    parameters_.conference_mode = *params.conference_mode;
  }

  // Set codecs and options.
  if (params.send_codec) {
    SetCodec(*params.send_codec,
             params.send_codecs.value_or(std::vector<VideoCodecSettings>()));
    recreate_stream = false;  // SetCodec has already recreated the stream.
  } else if (params.conference_mode && parameters_.codec_settings) {
    SetCodec(*parameters_.codec_settings, parameters_.codec_settings_list);
    recreate_stream = false;  // SetCodec has already recreated the stream.
  }
  if (recreate_stream) {
    RTC_LOG(LS_INFO)
        << "RecreateWebRtcStream (send) because of SetSenderParameters";
    RecreateWebRtcStream();
  }
}

RTCError WebRtcVideoSendChannel::WebRtcVideoSendStream::SetRtpParameters(
    const RtpParameters& new_parameters,
    SetParametersCallback callback) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  // This is checked higher in the stack (RtpSender), so this is only checking
  // for users accessing the private APIs or tests, not specification
  // conformance.
  // TODO(orphis): Migrate tests to later make this a DCHECK only
  RTCError error = CheckRtpParametersInvalidModificationAndValues(
      rtp_parameters_, new_parameters, env_.field_trials());
  if (!error.ok()) {
    return InvokeSetParametersCallback(callback, error);
  }

  bool new_param = false;
  for (size_t i = 0; i < rtp_parameters_.encodings.size(); ++i) {
    if ((new_parameters.encodings[i].min_bitrate_bps !=
         rtp_parameters_.encodings[i].min_bitrate_bps) ||
        (new_parameters.encodings[i].max_bitrate_bps !=
         rtp_parameters_.encodings[i].max_bitrate_bps) ||
        (new_parameters.encodings[i].max_framerate !=
         rtp_parameters_.encodings[i].max_framerate) ||
        (new_parameters.encodings[i].scale_resolution_down_by !=
         rtp_parameters_.encodings[i].scale_resolution_down_by) ||
        (new_parameters.encodings[i].num_temporal_layers !=
         rtp_parameters_.encodings[i].num_temporal_layers) ||
        (new_parameters.encodings[i].scale_resolution_down_to !=
         rtp_parameters_.encodings[i].scale_resolution_down_to) ||
        (new_parameters.encodings[i].scalability_mode !=
         rtp_parameters_.encodings[i].scalability_mode) ||
        (new_parameters.encodings[i].codec !=
         rtp_parameters_.encodings[i].codec)) {
      new_param = true;
      break;
    }
  }

  bool new_degradation_preference = false;
  if (new_parameters.degradation_preference !=
      rtp_parameters_.degradation_preference) {
    new_degradation_preference = true;
  }

  // Some fields (e.g. bitrate priority) only need to update the bitrate
  // allocator which is updated via ReconfigureEncoder (however, note that the
  // actual encoder should only be reconfigured if needed).
  bool reconfigure_encoder =
      new_param || (new_parameters.encodings[0].bitrate_priority !=
                    rtp_parameters_.encodings[0].bitrate_priority);

  // Note that the simulcast encoder adapter relies on the fact that layers
  // de/activation triggers encoder reinitialization.
  bool new_send_state = false;
  for (size_t i = 0; i < rtp_parameters_.encodings.size(); ++i) {
    bool new_active = IsLayerActive(new_parameters.encodings[i]);
    bool old_active = IsLayerActive(rtp_parameters_.encodings[i]);
    if (new_active != old_active) {
      new_send_state = true;
    }
  }
  bool new_csrcs =
      (new_parameters.encodings[0].csrcs.has_value() &&
       new_parameters.encodings[0].csrcs != rtp_parameters_.encodings[0].csrcs);

  rtp_parameters_ = new_parameters;
  // Codecs are currently handled at the WebRtcVideoSendChannel level.
  rtp_parameters_.codecs.clear();
  if (reconfigure_encoder || new_send_state) {
    // Callback responsibility is delegated to ReconfigureEncoder()
    ReconfigureEncoder(std::move(callback));
    callback = nullptr;
  }
  if (new_degradation_preference) {
    if (source_ && stream_) {
      stream_->SetSource(source_, GetDegradationPreference());
    }
  }
  if (stream_ && new_csrcs) {
    stream_->SetCsrcs(rtp_parameters_.encodings[0].csrcs.value());
  }
  // Check if a key frame was requested via setParameters.
  std::vector<std::string> key_frames_requested_by_rid;
  for (const auto& encoding : rtp_parameters_.encodings) {
    if (encoding.request_key_frame) {
      key_frames_requested_by_rid.push_back(encoding.rid);
    }
  }
  if (!key_frames_requested_by_rid.empty()) {
    if (key_frames_requested_by_rid.size() == 1 &&
        key_frames_requested_by_rid[0].empty()) {
      // For non-simulcast cases there is no rid,
      // request a keyframe on all layers.
      key_frames_requested_by_rid.clear();
    }
    GenerateKeyFrame(key_frames_requested_by_rid);
  }
  return InvokeSetParametersCallback(callback, RTCError::OK());
}

RtpParameters WebRtcVideoSendChannel::WebRtcVideoSendStream::GetRtpParameters()
    const {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rtp_parameters_;
}

void WebRtcVideoSendChannel::WebRtcVideoSendStream::SetFrameEncryptor(
    scoped_refptr<FrameEncryptorInterface> frame_encryptor) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  parameters_.config.frame_encryptor = std::move(frame_encryptor);
  if (stream_) {
    RTC_LOG(LS_INFO)
        << "RecreateWebRtcStream (send) because of SetFrameEncryptor, ssrc="
        << parameters_.config.rtp.ssrcs[0];
    RecreateWebRtcStream();
  }
}

void WebRtcVideoSendChannel::WebRtcVideoSendStream::UpdateSframeOptions(
    const SframeSendOptions& options) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  parameters_.config.rtp.sframe_options = options;
  if (stream_) {
    RTC_LOG(LS_INFO) << "RecreateWebRtcStream (send) because of "
                        "UpdateSframeOptions, ssrc="
                     << parameters_.config.rtp.ssrcs[0];
    RecreateWebRtcStream();
  }
}

void WebRtcVideoSendChannel::WebRtcVideoSendStream::SetEncoderSelector(
    scoped_refptr<VideoEncoderFactory::EncoderSelectorInterface>
        encoder_selector) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  parameters_.config.encoder_selector = std::move(encoder_selector);
  if (stream_) {
    RTC_LOG(LS_INFO)
        << "RecreateWebRtcStream (send) because of SetEncoderSelector, ssrc="
        << parameters_.config.rtp.ssrcs[0];
    RecreateWebRtcStream();
  }
}

void WebRtcVideoSendChannel::WebRtcVideoSendStream::SetEncoderFactoryOverride(
    absl_nonnull std::unique_ptr<VideoEncoderFactory> encoder_factory) {
  RTC_DCHECK_RUN_ON(&thread_checker_);

  encoder_factory_override_ = std::move(encoder_factory);

  if (stream_) {
    RTC_LOG(LS_INFO)
        << "RecreateWebRtcStream (send) because of SetEncoderFactory, ssrc="
        << parameters_.config.rtp.ssrcs[0];
    RecreateWebRtcStream();
  }
}

void WebRtcVideoSendChannel::WebRtcVideoSendStream::
    ResetEncoderFactoryOverride() {
  RTC_DCHECK_RUN_ON(&thread_checker_);

  if (!encoder_factory_override_) {
    return;
  }
  encoder_factory_override_ = nullptr;

  if (stream_) {
    RTC_LOG(LS_INFO)
        << "RecreateWebRtcStream (send) because of ResetEncoderFactory, ssrc="
        << parameters_.config.rtp.ssrcs[0];
    RecreateWebRtcStream();
  }
}

void WebRtcVideoSendChannel::WebRtcVideoSendStream::UpdateSendState() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (sending_) {
    RTC_DCHECK(stream_ != nullptr);
    // This allows the the Stream to be used. Ie, DTLS is connected and the
    // RtpTransceiver direction allows sending.
    stream_->Start();
  } else {
    if (stream_ != nullptr) {
      stream_->Stop();
    }
  }
}

VideoEncoderConfig
WebRtcVideoSendChannel::WebRtcVideoSendStream::CreateVideoEncoderConfig(
    const Codec& codec) const {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  VideoEncoderConfig encoder_config;
  encoder_config.codec_type = PayloadStringToCodecType(codec.name);
  encoder_config.video_format = SdpVideoFormat(codec.name, codec.params);
  encoder_config.allow_zero_hertz_video =
      parameters_.options.allow_zero_hertz_video.value_or(false);

  bool is_screencast = parameters_.options.is_screencast.value_or(false);
  if (is_screencast) {
    encoder_config.min_transmit_bitrate_bps =
        1000 * parameters_.options.screencast_min_bitrate_kbps.value_or(0);
    encoder_config.content_type = VideoEncoderConfig::ContentType::kScreen;
  } else {
    encoder_config.min_transmit_bitrate_bps = 0;
    encoder_config.content_type =
        VideoEncoderConfig::ContentType::kRealtimeVideo;
  }

  // By default, the stream count for the codec configuration should match the
  // number of negotiated ssrcs but this may be capped below depending on the
  // `legacy_scalability_mode` and codec used.
  encoder_config.number_of_streams = parameters_.config.rtp.ssrcs.size();
  bool legacy_scalability_mode = true;
  for (const RtpEncodingParameters& encoding : rtp_parameters_.encodings) {
    if (encoding.scalability_mode.has_value() &&
        (encoding.scale_resolution_down_by.has_value() ||
         encoding.scale_resolution_down_to.has_value())) {
      legacy_scalability_mode = false;
      break;
    }
  }
  // Maybe limit the number of simulcast layers depending on
  // `legacy_scalability_mode`, codec types (VP9/AV1). This path only exists
  // for backwards compatibility and will one day be deleted. If you want SVC,
  // please specify with the `scalability_mode` API instead amd disabling all
  // but one encoding.
  if (IsCodecDisabledForSimulcast(legacy_scalability_mode,
                                  encoder_config.codec_type)) {
    encoder_config.number_of_streams = 1;
  }

  // parameters_.max_bitrate comes from the max bitrate set at the SDP
  // (m-section) level with the attribute "b=AS." Note that stream max bitrate
  // is the RtpSender's max bitrate, but each individual encoding may also have
  // its own max bitrate specified by SetParameters.
  int stream_max_bitrate = parameters_.max_bitrate_bps;
  // The codec max bitrate comes from the "x-google-max-bitrate" parameter
  // attribute set in the SDP for a specific codec. It only has an effect if
  // max bitrate is not specified through other means.
  bool encodings_has_max_bitrate = false;
  for (const auto& encoding : rtp_parameters_.encodings) {
    if (encoding.active && encoding.max_bitrate_bps >= 0) {
      encodings_has_max_bitrate = true;
      break;
    }
  }
  int codec_max_bitrate_kbps;
  if (codec.GetParam(kCodecParamMaxBitrate, &codec_max_bitrate_kbps) &&
      stream_max_bitrate == -1 && !encodings_has_max_bitrate) {
    stream_max_bitrate = codec_max_bitrate_kbps * 1000;
  }
  encoder_config.max_bitrate_bps = stream_max_bitrate;

  // The encoder config's default bitrate priority is set to 1.0,
  // unless it is set through the sender's encoding parameters.
  // The bitrate priority, which is used in the bitrate allocation, is done
  // on a per sender basis, so we use the first encoding's value.
  encoder_config.bitrate_priority =
      rtp_parameters_.encodings[0].bitrate_priority;

  // Application-controlled state is held in the encoder_config's
  // simulcast_layers. Currently this is used to control which simulcast layers
  // are active and for configuring the min/max bitrate and max framerate.
  // The encoder_config's simulcast_layers is also used for non-simulcast (when
  // there is a single layer).
  RTC_DCHECK_GE(rtp_parameters_.encodings.size(),
                encoder_config.number_of_streams);
  RTC_DCHECK_GT(encoder_config.number_of_streams, 0);

  // Copy all provided constraints.
  encoder_config.simulcast_layers.resize(rtp_parameters_.encodings.size());
  for (size_t i = 0; i < encoder_config.simulcast_layers.size(); ++i) {
    encoder_config.simulcast_layers[i].active =
        rtp_parameters_.encodings[i].active;
    encoder_config.simulcast_layers[i].scalability_mode =
        ScalabilityModeFromString(
            rtp_parameters_.encodings[i].scalability_mode.value_or(""));
    if (rtp_parameters_.encodings[i].min_bitrate_bps) {
      encoder_config.simulcast_layers[i].min_bitrate_bps =
          *rtp_parameters_.encodings[i].min_bitrate_bps;
    }
    if (rtp_parameters_.encodings[i].max_bitrate_bps) {
      encoder_config.simulcast_layers[i].max_bitrate_bps =
          *rtp_parameters_.encodings[i].max_bitrate_bps;
    }
    if (rtp_parameters_.encodings[i].max_framerate) {
      encoder_config.simulcast_layers[i].max_framerate =
          *rtp_parameters_.encodings[i].max_framerate;
    }
    if (rtp_parameters_.encodings[i].scale_resolution_down_by) {
      encoder_config.simulcast_layers[i].scale_resolution_down_by =
          *rtp_parameters_.encodings[i].scale_resolution_down_by;
    }
    if (rtp_parameters_.encodings[i].num_temporal_layers) {
      encoder_config.simulcast_layers[i].num_temporal_layers =
          *rtp_parameters_.encodings[i].num_temporal_layers;
    }
    encoder_config.simulcast_layers[i].scale_resolution_down_to =
        rtp_parameters_.encodings[i].scale_resolution_down_to;
  }

  encoder_config.legacy_conference_mode = parameters_.conference_mode;

  encoder_config.is_quality_scaling_allowed =
      !disable_automatic_resize_ && !is_screencast &&
      (parameters_.config.rtp.ssrcs.size() == 1 ||
       NumActiveStreams(rtp_parameters_) == 1);

  // Ensure frame dropping is always enabled.
  encoder_config.frame_drop_enabled = true;

  int max_qp = -1;
  if (codec.GetParam(kCodecParamMaxQuantization, &max_qp) && max_qp > 0) {
    encoder_config.max_qp = max_qp;
  }

  return encoder_config;
}

void WebRtcVideoSendChannel::WebRtcVideoSendStream::ReconfigureEncoder(
    SetParametersCallback callback) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (!stream_) {
    // The VideoSendStream `stream_` has not yet been created but other
    // parameters has changed.
    InvokeSetParametersCallback(callback, RTCError::OK());
    return;
  }

  RTC_DCHECK_GT(parameters_.encoder_config.number_of_streams, 0);

  RTC_CHECK(parameters_.codec_settings);
  VideoCodecSettings codec_settings = *parameters_.codec_settings;

  FallbackToDefaultScalabilityModeIfNotSupported(
      codec_settings.codec, parameters_.config, rtp_parameters_.encodings);

  // Latest config, with and without encoder specfic settings.
  VideoEncoderConfig encoder_config =
      CreateVideoEncoderConfig(codec_settings.codec);
  encoder_config.encoder_specific_settings =
      ConfigureVideoEncoderSettings(codec_settings.codec);
  VideoEncoderConfig encoder_config_with_specifics = encoder_config.Copy();
  encoder_config.encoder_specific_settings = nullptr;

  // When switching between legacy SVC (3 encodings interpreted as 1 stream with
  // 3 spatial layers) and the standard API (3 encodings = 3 streams and spatial
  // layers specified by `scalability_mode`), the number of streams can change.
  bool num_streams_changed = parameters_.encoder_config.number_of_streams !=
                             encoder_config.number_of_streams;
  parameters_.encoder_config = std::move(encoder_config);

  if (num_streams_changed) {
    // The app is switching between legacy and standard modes, recreate instead
    // of reconfiguring to avoid number of streams not matching in lower layers.
    RecreateWebRtcStream();
    InvokeSetParametersCallback(callback, RTCError::OK());
    return;
  }

  stream_->ReconfigureVideoEncoder(std::move(encoder_config_with_specifics),
                                   std::move(callback));
}

void WebRtcVideoSendChannel::WebRtcVideoSendStream::SetSend(bool send) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  sending_ = send;
  UpdateSendState();
}

std::vector<VideoSenderInfo>
WebRtcVideoSendChannel::WebRtcVideoSendStream::GetPerLayerVideoSenderInfos(
    bool log_stats) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  VideoSenderInfo common_info;
  if (parameters_.codec_settings) {
    common_info.codec_name = parameters_.codec_settings->codec.name;
    common_info.codec_payload_type =
        parameters_.codec_settings->codec.id.value();
  }
  // If SVC is used, one stream is configured but multiple encodings exist. This
  // is not spec-compliant, but it is how we've implemented SVC so this affects
  // how the RTP stream's "active" value is determined.
  bool is_svc = (parameters_.encoder_config.number_of_streams == 1 &&
                 rtp_parameters_.encodings.size() > 1);
  std::vector<VideoSenderInfo> infos;
  VideoSendStream::Stats stats;
  if (stream_ == nullptr) {
    for (uint32_t ssrc : parameters_.config.rtp.ssrcs) {
      common_info.add_ssrc(ssrc);
    }
    infos.push_back(common_info);
    return infos;
  } else {
    stats = stream_->GetStats();
    if (log_stats)
      RTC_LOG(LS_INFO) << stats.ToString(env_.clock().TimeInMilliseconds());

    // Metrics that are in common for all substreams.
    common_info.adapt_changes = stats.number_of_cpu_adapt_changes;
    common_info.adapt_reason =
        stats.cpu_limited_resolution ? ADAPTREASON_CPU : ADAPTREASON_NONE;
    common_info.has_entered_low_resolution = stats.has_entered_low_resolution;

    // Get bandwidth limitation info from stream_->GetStats().
    // Input resolution (output from video_adapter) can be further scaled down
    // or higher video layer(s) can be dropped due to bitrate constraints.
    // Note, adapt_changes only include changes from the video_adapter.
    if (stats.bw_limited_resolution)
      common_info.adapt_reason |= ADAPTREASON_BANDWIDTH;

    common_info.quality_limitation_reason = stats.quality_limitation_reason;
    common_info.quality_limitation_durations_ms =
        stats.quality_limitation_durations_ms;
    common_info.quality_limitation_resolution_changes =
        stats.quality_limitation_resolution_changes;
    common_info.encoder_implementation_name = stats.encoder_implementation_name;
    common_info.ssrc_groups = ssrc_groups_;
    common_info.frames = stats.frames;
    common_info.framerate_input = stats.input_frame_rate;
    common_info.avg_encode_ms = stats.avg_encode_time_ms;
    common_info.encode_usage_percent = stats.encode_usage_percent;
    common_info.nominal_bitrate = stats.media_bitrate_bps;
    common_info.content_type = stats.content_type;
    common_info.aggregated_framerate_sent = stats.encode_frame_rate;
    common_info.aggregated_huge_frames_sent = stats.huge_frames_sent;
    common_info.power_efficient_encoder = stats.power_efficient_encoder;

    // The "typical case" where `substreams` exist because we have negotiated
    // and connected is handled below, but prior to that `substreams` is empty.
    // In this case we still need to return one "info" per SSRC and set a few
    // stats that should never be missing.
    if (stats.substreams.empty()) {
      size_t encoding_index = 0;
      for (uint32_t ssrc : parameters_.config.rtp.ssrcs) {
        auto info = common_info;
        info.add_ssrc(ssrc);
        info.rid = parameters_.config.rtp.GetRidForSsrc(ssrc);
        info.encoding_index = encoding_index;
        info.active = IsActiveFromEncodings(
            !is_svc ? std::optional<uint32_t>(ssrc) : std::nullopt,
            rtp_parameters_.encodings);
        ++encoding_index;
        infos.push_back(info);
      }
      return infos;
    }
  }
  // Merge `stats.substreams`, which may contain additional SSRCs for RTX or
  // Flexfec, with media SSRCs. This results in a set of substreams that match
  // with the outbound-rtp stats objects.
  auto outbound_rtp_substreams =
      MergeInfoAboutOutboundRtpSubstreams(stats.substreams);
  // The streams are ordered by SSRC, but the SSRCs are randomly assigned so we
  // need map for index lookup by SSRC.
  std::map<uint32_t, size_t> encoding_index_by_ssrc;
  for (size_t i = 0; i < parameters_.config.rtp.ssrcs.size(); ++i) {
    encoding_index_by_ssrc[parameters_.config.rtp.ssrcs[i]] = i;
  }
  for (const auto& pair : outbound_rtp_substreams) {
    auto info = common_info;
    uint32_t ssrc = pair.first;
    info.add_ssrc(ssrc);
    info.rid = parameters_.config.rtp.GetRidForSsrc(ssrc);
    if (encoding_index_by_ssrc.find(ssrc) != encoding_index_by_ssrc.end()) {
      info.encoding_index = encoding_index_by_ssrc[ssrc];
    }
    auto stream_config =
        std::find_if(parameters_.config.rtp.stream_configs.begin(),
                     parameters_.config.rtp.stream_configs.end(),
                     [ssrc](const auto& stream_config) {
                       return stream_config.ssrc == ssrc;
                     });
    if (stream_config != parameters_.config.rtp.stream_configs.end()) {
      if (stream_config->payload_type != -1) {
        info.codec_name = stream_config->payload_name;
        info.codec_payload_type = stream_config->payload_type;
      } else {
        info.codec_name = "";
        info.codec_payload_type = std::nullopt;
      }
    }
    info.active = IsActiveFromEncodings(
        !is_svc ? std::optional<uint32_t>(ssrc) : std::nullopt,
        rtp_parameters_.encodings);
    auto stream_stats = pair.second;
    RTC_DCHECK_EQ(stream_stats.type,
                  VideoSendStream::StreamStats::StreamType::kMedia);
    info.payload_bytes_sent = stream_stats.rtp_stats.transmitted.payload_bytes;
    info.header_and_padding_bytes_sent =
        stream_stats.rtp_stats.transmitted.header_bytes +
        stream_stats.rtp_stats.transmitted.padding_bytes;
    info.packets_sent = stream_stats.rtp_stats.transmitted.packets;
    info.packets_sent_with_ect1 =
        stream_stats.rtp_stats.transmitted.packets_with_ect1;
    info.total_packet_send_delay +=
        stream_stats.rtp_stats.transmitted.total_packet_delay;
    info.send_frame_width = stream_stats.width;
    info.send_frame_height = stream_stats.height;
    info.key_frames_encoded = stream_stats.frame_counts.key_frames;
    info.framerate_sent = stream_stats.encode_frame_rate;
    info.frames_encoded = stream_stats.frames_encoded;
    info.frames_sent = stream_stats.frames_encoded;
    info.retransmitted_bytes_sent =
        stream_stats.rtp_stats.retransmitted.payload_bytes;
    info.retransmitted_packets_sent =
        stream_stats.rtp_stats.retransmitted.packets;
    info.firs_received = stream_stats.rtcp_packet_type_counts.fir_packets;
    info.nacks_received = stream_stats.rtcp_packet_type_counts.nack_packets;
    info.plis_received = stream_stats.rtcp_packet_type_counts.pli_packets;
    if (stream_stats.report_block_data.has_value()) {
      info.packets_lost = stream_stats.report_block_data->cumulative_lost();
      info.fraction_lost = stream_stats.report_block_data->fraction_lost();
      info.report_block_datas.push_back(*stream_stats.report_block_data);
    }
    info.qp_sum = stream_stats.qp_sum;
    info.psnr_sum = stream_stats.psnr_sum;
    info.psnr_measurements = stream_stats.psnr_measurements;
    info.total_encode_time_ms = stream_stats.total_encode_time_ms;
    info.huge_frames_sent = stream_stats.huge_frames_sent;
    info.scalability_mode = stream_stats.scalability_mode;
    info.target_bitrate = stream_stats.target_bitrate;
    infos.push_back(info);
  }
  return infos;
}

VideoSenderInfo
WebRtcVideoSendChannel::WebRtcVideoSendStream::GetAggregatedVideoSenderInfo(
    const std::vector<VideoSenderInfo>& infos) const {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_CHECK(!infos.empty());
  if (infos.size() == 1) {
    return infos[0];
  }
  VideoSenderInfo info = infos[0];
  info.encoding_index = std::nullopt;  // An aggregated info has no index.
  info.local_stats.clear();
  for (uint32_t ssrc : parameters_.config.rtp.ssrcs) {
    info.add_ssrc(ssrc);
  }
  info.framerate_sent = info.aggregated_framerate_sent;
  info.huge_frames_sent = info.aggregated_huge_frames_sent;

  for (size_t i = 1; i < infos.size(); i++) {
    info.key_frames_encoded += infos[i].key_frames_encoded;
    info.payload_bytes_sent += infos[i].payload_bytes_sent;
    info.header_and_padding_bytes_sent +=
        infos[i].header_and_padding_bytes_sent;
    info.packets_sent += infos[i].packets_sent;
    info.total_packet_send_delay += infos[i].total_packet_send_delay;
    info.retransmitted_bytes_sent += infos[i].retransmitted_bytes_sent;
    info.retransmitted_packets_sent += infos[i].retransmitted_packets_sent;
    info.packets_lost += infos[i].packets_lost;
    if (infos[i].send_frame_width > info.send_frame_width)
      info.send_frame_width = infos[i].send_frame_width;
    if (infos[i].send_frame_height > info.send_frame_height)
      info.send_frame_height = infos[i].send_frame_height;
    info.firs_received += infos[i].firs_received;
    info.nacks_received += infos[i].nacks_received;
    info.plis_received += infos[i].plis_received;
    if (!infos[i].report_block_datas.empty())
      info.report_block_datas.push_back(infos[i].report_block_datas[0]);
    if (infos[i].qp_sum) {
      if (!info.qp_sum) {
        info.qp_sum = 0;
      }
      info.qp_sum = *info.qp_sum + *infos[i].qp_sum;
    }
    if (infos[i].psnr_measurements > 0) {
      info.psnr_measurements += infos[i].psnr_measurements;
      info.psnr_sum.y += infos[i].psnr_sum.y;
      info.psnr_sum.u += infos[i].psnr_sum.u;
      info.psnr_sum.v += infos[i].psnr_sum.v;
    }
    info.frames_encoded += infos[i].frames_encoded;
    info.frames_sent += infos[i].frames_sent;
    info.total_encode_time_ms += infos[i].total_encode_time_ms;
  }
  return info;
}

void WebRtcVideoSendChannel::WebRtcVideoSendStream::FillBitrateInfo(
    BandwidthEstimationInfo* bwe_info) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (stream_ == nullptr) {
    return;
  }
  VideoSendStream::Stats stats = stream_->GetStats();
  for (const auto& it : stats.substreams) {
    bwe_info->transmit_bitrate += it.second.total_bitrate_bps;
    bwe_info->retransmit_bitrate += it.second.retransmit_bitrate_bps;
  }
  bwe_info->target_enc_bitrate += stats.target_media_bitrate_bps;
  bwe_info->actual_enc_bitrate += stats.media_bitrate_bps;
}

void WebRtcVideoSendChannel::WebRtcVideoSendStream::
    SetEncoderToPacketizerFrameTransformer(
        scoped_refptr<FrameTransformerInterface> frame_transformer) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  parameters_.config.frame_transformer = std::move(frame_transformer);
  if (stream_)
    RecreateWebRtcStream();
}

void WebRtcVideoSendChannel::WebRtcVideoSendStream::RecreateWebRtcStream() {
  RTC_DCHECK_RUN_ON(&thread_checker_);

  RTC_CHECK(parameters_.codec_settings);
  RTC_DCHECK_EQ((parameters_.encoder_config.content_type ==
                 VideoEncoderConfig::ContentType::kScreen),
                parameters_.options.is_screencast.value_or(false))
      << "encoder content type inconsistent with screencast option";
  parameters_.encoder_config.encoder_specific_settings =
      ConfigureVideoEncoderSettings(parameters_.codec_settings->codec);

  VideoSendStream::Config config = parameters_.config.Copy();

  if (encoder_factory_override_) {
    // Passing the raw ptr is safe since it has the same lifetime as this
    // object. The pointer will be stored in VideoSendStreamImpl and
    // VideoStreamEncoder.
    config.encoder_settings.encoder_factory = encoder_factory_override_.get();
  }  // default factory is used if no override is set.

  auto encoder_switch_request_callback =
      [send_channel = send_channel_](std::optional<SdpVideoFormat> format,
                                     bool allow_default_fallback) {
        send_channel->RequestEncoderSwitch(std::move(format),
                                           allow_default_fallback);
      };
  if (!config.rtp.rtx.ssrcs.empty() && config.rtp.rtx.payload_type == -1) {
    RTC_LOG(LS_WARNING) << "RTX SSRCs configured but there's no configured RTX "
                           "payload type the set codec. Ignoring RTX.";
    config.rtp.rtx.ssrcs.clear();
  }
  if (parameters_.encoder_config.number_of_streams == 1) {
    // SVC is used instead of simulcast. Remove unnecessary SSRCs.
    if (config.rtp.ssrcs.size() > 1) {
      config.rtp.ssrcs.resize(1);
      if (config.rtp.rids.size() > 1) {
        config.rtp.rids.resize(1);
      }
      if (config.rtp.rtx.ssrcs.size() > 1) {
        config.rtp.rtx.ssrcs.resize(1);
      }
    }
  }

  if (RtpExtension::FindHeaderExtensionByUri(
          config.rtp.extensions, RtpExtension::kCorruptionDetectionUri,
          RtpExtension::kRequireEncryptedExtension)) {
    config.encoder_settings.enable_frame_instrumentation_generator = true;
  }

  if (!rtp_parameters_.encodings.empty() &&
      rtp_parameters_.encodings[0].csrcs.has_value()) {
    config.rtp.csrcs = rtp_parameters_.encodings[0].csrcs.value();
  }

  if (stream_ != nullptr) {
    // TODO: webrtc:40644448 - Make sure the stats are not updated between
    // GetStats and DestroyVideoSendStream.
    VideoSendStream::Stats stats = stream_->GetStats();
    call_->DestroyVideoSendStream(stream_);
    stream_ = call_->CreateVideoSendStream(
        std::move(config), parameters_.encoder_config.Copy(),
        std::move(encoder_switch_request_callback));

    // A new stream is created without any scaling or limitations, so these
    // flags don't apply until the new stream experiences an adaptation event.
    stats.bw_limited_resolution = false;
    stats.bw_limited_framerate = false;
    stats.cpu_limited_resolution = false;
    stats.cpu_limited_framerate = false;
    stats.quality_limitation_reason = QualityLimitationReason::kNone;

    stream_->SetStats(stats);
  } else {
    stream_ = call_->CreateVideoSendStream(
        std::move(config), parameters_.encoder_config.Copy(),
        std::move(encoder_switch_request_callback));
  }

  parameters_.encoder_config.encoder_specific_settings = nullptr;

  // Calls stream_->StartPerRtpStream() to start the VideoSendStream
  // if necessary conditions are met.
  UpdateSendState();

  // Attach the source after starting the send stream to prevent frames from
  // being injected into a not-yet initializated video stream encoder.
  if (source_) {
    stream_->SetSource(source_, GetDegradationPreference());
  }
}

void WebRtcVideoSendChannel::WebRtcVideoSendStream::GenerateKeyFrame(
    const std::vector<std::string>& rids) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (stream_ != nullptr) {
    stream_->GenerateKeyFrame(rids);
  } else {
    RTC_LOG(LS_WARNING)
        << "Absent send stream; ignoring request to generate keyframe.";
  }
}

void WebRtcVideoSendChannel::GenerateSendKeyFrame(
    uint32_t ssrc,
    const std::vector<std::string>& rids) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  auto it = send_streams_.find(ssrc);
  if (it != send_streams_.end()) {
    it->second->GenerateKeyFrame(rids);
  } else {
    RTC_LOG(LS_ERROR)
        << "Absent send stream; ignoring key frame generation for ssrc "
        << ssrc;
  }
}

void WebRtcVideoSendChannel::SetEncoderToPacketizerFrameTransformer(
    uint32_t ssrc,
    scoped_refptr<FrameTransformerInterface> frame_transformer) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  auto matching_stream = send_streams_.find(ssrc);
  if (matching_stream != send_streams_.end()) {
    matching_stream->second->SetEncoderToPacketizerFrameTransformer(
        std::move(frame_transformer));
  }
}

// ------------------------ WebRtcVideoReceiveChannel ---------------------
WebRtcVideoReceiveChannel::WebRtcVideoReceiveChannel(
    const Environment& env,
    Call* absl_nonnull call,
    const MediaConfig& config,
    const CryptoOptions& crypto_options,
    VideoDecoderFactory* absl_nullable decoder_factory,
    absl::AnyInvocable<void(uint32_t ssrc)> on_first_packet,
    absl::AnyInvocable<void(uint32_t ssrc, const RtpPacketInfos&, Timestamp)
                           const> on_frame_delivered_callback)
    : MediaChannelUtil(call->network_thread(), config.enable_dscp),
      env_(env),
      network_thread_safety_(PendingTaskSafetyFlag::CreateAttachedToTaskQueue(
          /*alive=*/true,
          call->network_thread())),
      thread_checker_(call->worker_thread()),
      receiving_(false),
      call_(call),
      default_sink_(nullptr),
      video_config_(config.video),
      decoder_factory_(decoder_factory),
      last_receive_stats_log_ms_(-1),
      discard_unknown_ssrc_packets_(env_.field_trials().IsEnabled(
          "WebRTC-Video-DiscardPacketsWithUnknownSsrc")),
      crypto_options_(crypto_options),
      on_frame_delivered_callback_(std::move(on_frame_delivered_callback)),
      receive_buffer_size_(ParseReceiveBufferSize(env_.field_trials())),
      on_first_packet_(std::move(on_first_packet)) {
  RTC_DCHECK(on_frame_delivered_callback_ != nullptr);
  // Crash if MapCodecs fails.
  recv_codecs_ = MapCodecs(GetPayloadTypesAndDefaultCodecs(
                               decoder_factory_, /*is_decoder_factory=*/true,
                               /*include_rtx=*/true, env_.field_trials()))
                     .value();
  recv_flexfec_payload_type_ =
      recv_codecs_.empty() ? 0 : recv_codecs_.front().flexfec_payload_type;
}

WebRtcVideoReceiveChannel::~WebRtcVideoReceiveChannel() {
  for (auto& kv : receive_streams_)
    delete kv.second;
}

RtpParameters WebRtcVideoReceiveChannel::GetRtpReceiverParameters(
    uint32_t ssrc) const {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RtpParameters rtp_params;
  auto it = receive_streams_.find(ssrc);
  if (it == receive_streams_.end()) {
    RTC_LOG(LS_WARNING)
        << "Attempting to get RTP receive parameters for stream "
           "with SSRC "
        << ssrc << " which doesn't exist.";
    return RtpParameters();
  }
  rtp_params = it->second->GetRtpParameters();

  // Add codecs, which any stream is prepared to receive.
  for (const Codec& codec : recv_params_.codecs) {
    rtp_params.codecs.push_back(codec.ToCodecParameters());
  }

  rtp_params.header_extensions = FilterRtpExtensions(
      recv_params_.extensions, RtpExtension::IsSupportedForVideo, false,
      env_.field_trials());

  return rtp_params;
}

RtpParameters WebRtcVideoReceiveChannel::GetDefaultRtpReceiveParameters()
    const {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RtpParameters rtp_params;
  if (!default_sink_) {
    // Getting parameters on a default, unsignaled video receive stream but
    // because we've not configured to receive such a stream, `encodings` is
    // empty.
    return rtp_params;
  }
  rtp_params.encodings.emplace_back();

  // Add codecs, which any stream is prepared to receive.
  for (const Codec& codec : recv_params_.codecs) {
    rtp_params.codecs.push_back(codec.ToCodecParameters());
  }

  return rtp_params;
}

bool WebRtcVideoReceiveChannel::GetChangedReceiverParameters(
    const VideoReceiverParameters& params,
    ChangedReceiverParameters* changed_params) const {
  if (!ValidateCodecFormats(params.codecs)) {
    return false;
  }

  // Handle receive codecs.
  auto result = MapCodecs(params.codecs);
  if (!result.ok()) {
    RTC_LOG(LS_ERROR) << "GetChangedReceiverParameters called without valid "
                         "video codecs, error ="
                      << result.error();
    return false;
  }
  const std::vector<VideoCodecSettings>& mapped_codecs = result.value();

  // Verify that every mapped codec is supported locally.
  if (params.is_stream_active) {
    // Only the codec name and codec specific parameters are compared, so
    // there is no need to assign payload types to the local codecs.
    const std::vector<SdpVideoFormat> local_supported_formats =
        GetDefaultSupportedFormats(decoder_factory_,
                                   /*is_decoder_factory=*/true,
                                   env_.field_trials());
    for (const VideoCodecSettings& mapped_codec : mapped_codecs) {
      if (!SdpVideoFormat(mapped_codec.codec.name, mapped_codec.codec.params)
               .IsCodecInList(local_supported_formats)) {
        RTC_LOG(LS_ERROR) << "GetChangedReceiverParameters called with "
                             "unsupported video codec: "
                          << mapped_codec.codec.ToString();
        return false;
      }
    }
  }

  if (NonFlexfecReceiveCodecsHaveChanged(recv_codecs_, mapped_codecs)) {
    changed_params->codec_settings =
        std::optional<std::vector<VideoCodecSettings>>(mapped_codecs);
  }

  int flexfec_payload_type = mapped_codecs.front().flexfec_payload_type;
  if (flexfec_payload_type != recv_flexfec_payload_type_) {
    changed_params->flexfec_payload_type = flexfec_payload_type;
  }

  if (params.rtcp.reduced_size != recv_params_.rtcp.reduced_size) {
    changed_params->rtcp_mode =
        params.rtcp.reduced_size ? RtcpMode::kReducedSize : RtcpMode::kCompound;
  }

  return true;
}

bool WebRtcVideoReceiveChannel::SetReceiverParameters(
    const VideoReceiverParameters& params) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  TRACE_EVENT0("webrtc", "WebRtcVideoReceiveChannel::SetReceiverParameters");
  RTC_LOG(LS_INFO) << "SetReceiverParameters: " << params.ToString();
  ChangedReceiverParameters changed_params;
  if (!GetChangedReceiverParameters(params, &changed_params)) {
    return false;
  }
  if (changed_params.flexfec_payload_type) {
    RTC_DLOG(LS_INFO) << "Changing FlexFEC payload type (recv) from "
                      << recv_flexfec_payload_type_ << " to "
                      << *changed_params.flexfec_payload_type;
    recv_flexfec_payload_type_ = *changed_params.flexfec_payload_type;
  }
  if (changed_params.codec_settings) {
    RTC_DLOG(LS_INFO) << "Changing recv codecs from "
                      << CodecSettingsVectorToString(recv_codecs_) << " to "
                      << CodecSettingsVectorToString(
                             *changed_params.codec_settings);
    recv_codecs_ = *changed_params.codec_settings;
  }

  for (auto& kv : receive_streams_) {
    kv.second->SetReceiverParameters(changed_params);
  }
  recv_params_ = params;
  return true;
}

void WebRtcVideoReceiveChannel::SetReceive(bool receive) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  TRACE_EVENT0("webrtc", "WebRtcVideoReceiveChannel::SetReceive");
  RTC_LOG(LS_VERBOSE) << "SetReceive: " << (receive ? "true" : "false");
  for (const auto& kv : receive_streams_) {
    if (receive) {
      kv.second->StartReceiveStream();
    } else {
      kv.second->StopReceiveStream();
    }
  }
  receiving_ = receive;
}

void WebRtcVideoReceiveChannel::SetReceiveNonSenderRttEnabled(bool enabled) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (enable_non_sender_rtt_ == enabled) {
    return;
  }
  enable_non_sender_rtt_ = enabled;
  for (auto& kv : receive_streams_) {
    kv.second->SetNonSenderRttMeasurement(enabled);
  }
}

bool WebRtcVideoReceiveChannel::ValidateReceiveSsrcAvailability(
    const StreamParams& sp) const {
  for (uint32_t ssrc : sp.ssrcs) {
    if (receive_ssrcs_.find(ssrc) != receive_ssrcs_.end()) {
      RTC_LOG(LS_ERROR) << "Receive stream with SSRC '" << ssrc
                        << "' already exists.";
      return false;
    }
  }
  return true;
}

void WebRtcVideoReceiveChannel::DeleteReceiveStream(
    WebRtcVideoReceiveStream* stream) {
  for (uint32_t old_ssrc : stream->GetSsrcs())
    receive_ssrcs_.erase(old_ssrc);
  delete stream;
}

bool WebRtcVideoReceiveChannel::AddRecvStream(const StreamParams& sp) {
  return AddRecvStream(sp, false);
}

bool WebRtcVideoReceiveChannel::AddRecvStream(const StreamParams& sp,
                                              bool default_stream) {
  RTC_DCHECK_RUN_ON(&thread_checker_);

  RTC_LOG(LS_INFO) << "AddRecvStream"
                   << (default_stream ? " (default stream)" : "") << ": "
                   << sp.ToString();
  if (!sp.has_ssrcs()) {
    // This is a StreamParam with unsignaled SSRCs. Store it, so it can be used
    // later when we know the SSRC on the first packet arrival.
    unsignaled_stream_params_ = sp;
    return true;
  }

  if (!ValidateStreamParams(sp))
    return false;

  for (uint32_t ssrc : sp.ssrcs) {
    // Remove running stream if this was a default stream.
    const auto& prev_stream = receive_streams_.find(ssrc);
    if (prev_stream != receive_streams_.end()) {
      if (default_stream || !prev_stream->second->IsDefaultStream()) {
        RTC_LOG(LS_ERROR) << "Receive stream for SSRC '" << ssrc
                          << "' already exists.";
        return false;
      }
      DeleteReceiveStream(prev_stream->second);
      receive_streams_.erase(prev_stream);
    }
  }

  if (!ValidateReceiveSsrcAvailability(sp))
    return false;

  for (uint32_t used_ssrc : sp.ssrcs)
    receive_ssrcs_.insert(used_ssrc);

  VideoReceiveStreamInterface::Config config(transport(), decoder_factory_);
  FlexfecReceiveStream::Config flexfec_config(transport());
  ConfigureReceiverRtp(&config, &flexfec_config, sp);

  config.crypto_options = crypto_options_;
  config.enable_prerenderer_smoothing =
      video_config_.enable_prerenderer_smoothing;
  if (!sp.stream_ids().empty()) {
    config.sync_group = sp.stream_ids()[0];
  }

  if (unsignaled_frame_transformer_ && !config.frame_transformer)
    config.frame_transformer = unsignaled_frame_transformer_;

  config.on_first_packet = [this](uint32_t ssrc) {
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (on_first_packet_) {
      on_first_packet_(ssrc);
    }
  };

  RTC_DCHECK(on_frame_delivered_callback_ != nullptr);
  config.on_frame_delivered_callback = [this, ssrc = sp.first_ssrc()](
                                           const RtpPacketInfos& infos,
                                           Timestamp timestamp) {
    on_frame_delivered_callback_(ssrc, infos, timestamp);
  };

  config.rtp.rtcp_xr.receiver_reference_time_report = enable_non_sender_rtt_;
  auto receive_stream = new WebRtcVideoReceiveStream(
      env_, call_, sp, std::move(config), default_stream, recv_codecs_,
      flexfec_config);
  if (receiving_) {
    receive_stream->StartReceiveStream();
  }
  receive_streams_[sp.first_ssrc()] = receive_stream;
  return true;
}

void WebRtcVideoReceiveChannel::ConfigureReceiverRtp(
    VideoReceiveStreamInterface::Config* config,
    FlexfecReceiveStream::Config* flexfec_config,
    const StreamParams& sp) const {
  uint32_t ssrc = sp.first_ssrc();

  config->rtp.remote_ssrc = ssrc;

  sp.GetFidSsrc(ssrc, &config->rtp.rtx_ssrc);

  // TODO(brandtr): Generalize when we add support for multistream protection.
  flexfec_config->payload_type = recv_flexfec_payload_type_;
  if (!env_.field_trials().IsDisabled("WebRTC-FlexFEC-03-Advertised") &&
      sp.GetFecFrSsrc(ssrc, &flexfec_config->remote_ssrc)) {
    flexfec_config->protected_media_ssrcs = {ssrc};
    flexfec_config->rtcp_mode = config->rtp.rtcp_mode;
  }
}

bool WebRtcVideoReceiveChannel::RemoveRecvStream(uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_LOG(LS_INFO) << "RemoveRecvStream: " << ssrc;

  auto stream = receive_streams_.find(ssrc);
  if (stream == receive_streams_.end()) {
    RTC_LOG(LS_ERROR) << "Stream not found for ssrc: " << ssrc;
    return false;
  }
  DeleteReceiveStream(stream->second);
  receive_streams_.erase(stream);

  return true;
}

void WebRtcVideoReceiveChannel::ResetUnsignaledRecvStream() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_LOG(LS_INFO) << "ResetUnsignaledRecvStream.";
  unsignaled_stream_params_ = StreamParams();
  last_unsignalled_ssrc_creation_time_ms_ = std::nullopt;

  // Delete any created default streams. This is needed to avoid SSRC collisions
  // in Call's RtpDemuxer, in the case that `this` has created a default video
  // receiver, and then some other WebRtcVideoReceiveChannel gets the SSRC
  // signaled in the corresponding Unified Plan "m=" section.
  auto it = receive_streams_.begin();
  while (it != receive_streams_.end()) {
    if (it->second->IsDefaultStream()) {
      DeleteReceiveStream(it->second);
      receive_streams_.erase(it++);
    } else {
      ++it;
    }
  }
}

absl::AnyInvocable<void() &&>
WebRtcVideoReceiveChannel::GetResetUnsignaledRecvStreamTask() {
  return SafeTask(task_safety_.flag(),
                  [this]() { ResetUnsignaledRecvStream(); });
}

std::optional<uint32_t> WebRtcVideoReceiveChannel::GetUnsignaledSsrc() const {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  std::optional<uint32_t> ssrc;
  for (auto it = receive_streams_.begin(); it != receive_streams_.end(); ++it) {
    if (it->second->IsDefaultStream()) {
      ssrc.emplace(it->first);
      break;
    }
  }
  return ssrc;
}

std::vector<uint32_t> WebRtcVideoReceiveChannel::GetUnsignaledSsrcs() const {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  std::vector<uint32_t> ssrcs;
  for (const auto& [ssrc, stream] : receive_streams_) {
    if (stream->IsDefaultStream()) {
      ssrcs.push_back(ssrc);
    }
  }
  return ssrcs;
}

void WebRtcVideoReceiveChannel::OnDemuxerCriteriaUpdatePending() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  ++demuxer_criteria_id_;
}

void WebRtcVideoReceiveChannel::OnDemuxerCriteriaUpdateComplete() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  ++demuxer_criteria_completed_id_;
}

bool WebRtcVideoReceiveChannel::SetSink(uint32_t ssrc,
                                        VideoSinkInterface<VideoFrame>* sink) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_LOG(LS_INFO) << "SetSink: ssrc:" << ssrc << " "
                   << (sink ? "(ptr)" : "nullptr");

  auto it = receive_streams_.find(ssrc);
  if (it == receive_streams_.end()) {
    return false;
  }

  it->second->SetSink(sink);
  return true;
}

void WebRtcVideoReceiveChannel::SetDefaultSink(
    VideoSinkInterface<VideoFrame>* sink) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_LOG(LS_INFO) << "SetDefaultSink: " << (sink ? "(ptr)" : "nullptr");
  default_sink_ = sink;
  if (std::optional<uint32_t> ssrc = GetUnsignaledSsrc()) {
    SetSink(*ssrc, default_sink_);
  }
}

bool WebRtcVideoReceiveChannel::GetStats(VideoMediaReceiveInfo* info) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  TRACE_EVENT0("webrtc", "WebRtcVideoReceiveChannel::GetStats");

  info->Clear();
  if (receive_streams_.empty()) {
    return true;
  }

  // Log stats periodically.
  bool log_stats = false;
  int64_t now_ms = env_.clock().TimeInMilliseconds();
  if (last_receive_stats_log_ms_ == -1 ||
      now_ms - last_receive_stats_log_ms_ > kStatsLogIntervalMs) {
    last_receive_stats_log_ms_ = now_ms;
    log_stats = true;
  }

  FillReceiverStats(info, log_stats);
  FillReceiveCodecStats(info);

  return true;
}

void WebRtcVideoReceiveChannel::FillReceiverStats(
    VideoMediaReceiveInfo* video_media_info,
    bool log_stats) {
  for (const auto& it : receive_streams_) {
    video_media_info->receivers.push_back(
        it.second->GetVideoReceiverInfo(log_stats));
  }
}

void WebRtcVideoReceiveChannel::FillReceiveCodecStats(
    VideoMediaReceiveInfo* video_media_info) {
  for (const auto& receiver : video_media_info->receivers) {
    auto codec =
        absl::c_find_if(recv_params_.codecs, [&receiver](const Codec& c) {
          return receiver.codec_payload_type &&
                 *receiver.codec_payload_type == c.id;
        });
    if (codec != recv_params_.codecs.end()) {
      video_media_info->receive_codecs.emplace(
          std::make_pair(codec->id, codec->ToCodecParameters()));
    }
  }
}

absl::AnyInvocable<std::optional<VideoMediaReceiveInfo>()>
WebRtcVideoReceiveChannel::GetStatsTask() {
  return [this, safety = task_safety_.flag()]() mutable
             -> std::optional<VideoMediaReceiveInfo> {
    if (!safety->alive()) {
      return std::nullopt;
    }
    VideoMediaReceiveInfo info;
    if (GetStats(&info)) {
      return info;
    }
    return std::nullopt;
  };
}

void WebRtcVideoReceiveChannel::OnPacketReceived(RtpPacketReceived packet) {
  // Note: the network_thread_checker may refer to the worker thread if the two
  // threads are combined, but this is either always true or always false
  // depending on configuration set at object initialization.
  RTC_DCHECK_RUN_ON(&network_thread_checker_);

  packet.set_payload_type_frequency(kVideoPayloadTypeFrequency);
  if (!packet.arrival_time().IsFinite()) {
    packet.set_arrival_time(env_.clock().CurrentTime());
  }

  call_->Receiver()->DeliverRtpPacket(
      MediaType::VIDEO, std::move(packet),
      absl::bind_front(
          &WebRtcVideoReceiveChannel::MaybeCreateDefaultReceiveStream, this));
}

bool WebRtcVideoReceiveChannel::MaybeCreateDefaultReceiveStream(
    const RtpPacketReceived& packet) {
  if (discard_unknown_ssrc_packets_) {
    return false;
  }

  if (packet.PayloadType() == recv_flexfec_payload_type_) {
    return false;
  }

  // Ignore unknown ssrcs if there is a demuxer criteria update pending.
  // During a demuxer update we may receive ssrcs that were recently
  // removed or we may receve ssrcs that were recently configured for a
  // different video channel.
  if (demuxer_criteria_id_ != demuxer_criteria_completed_id_) {
    return false;
  }

  // See if this payload_type is registered as one that usually gets its
  // own SSRC (RTX) or at least is safe to drop either way (FEC). If it
  // is, and it wasn't handled above by DeliverPacket, that means we don't
  // know what stream it associates with, and we shouldn't ever create an
  // implicit channel for these.
  bool is_rtx_payload = false;
  for (auto& codec : recv_codecs_) {
    if (packet.PayloadType() == codec.ulpfec.red_rtx_payload_type ||
        packet.PayloadType() == codec.ulpfec.ulpfec_payload_type) {
      return false;
    }

    if (packet.PayloadType() == codec.rtx_payload_type) {
      is_rtx_payload = true;
      break;
    }
  }

  if (is_rtx_payload) {
    // As we don't support receiving simulcast there can only be one RTX
    // stream, which will be associated with unsignaled media stream.
    std::optional<uint32_t> current_default_ssrc = GetUnsignaledSsrc();
    if (current_default_ssrc) {
      FindReceiveStream(*current_default_ssrc)->UpdateRtxSsrc(packet.Ssrc());
      return true;
    }
    // Default media SSRC not known yet. Drop the packet.
    // BWE has already been notified of this received packet.
    return false;
  }
  // Ignore unknown ssrcs if we recently created an unsignalled receive
  // stream since this shouldn't happen frequently. Getting into a state
  // of creating decoders on every packet eats up processing time (e.g.
  // https://crbug.com/1069603) and this cooldown prevents that.
  if (last_unsignalled_ssrc_creation_time_ms_.has_value()) {
    int64_t now_ms = env_.clock().TimeInMilliseconds();
    if (now_ms - last_unsignalled_ssrc_creation_time_ms_.value() <
        kUnsignaledSsrcCooldownMs) {
      // We've already created an unsignalled ssrc stream within the last
      // 0.5 s, ignore with a warning.
      RTC_LOG(LS_WARNING)
          << "Another unsignalled ssrc packet arrived shortly after the "
          << "creation of an unsignalled ssrc stream. Dropping packet.";
      return false;
    }
  }

  // RTX SSRC not yet known.
  ReCreateDefaultReceiveStream(packet.Ssrc(), std::nullopt);
  last_unsignalled_ssrc_creation_time_ms_ = env_.clock().TimeInMilliseconds();
  return true;
}

void WebRtcVideoReceiveChannel::ReCreateDefaultReceiveStream(
    uint32_t ssrc,
    std::optional<uint32_t> rtx_ssrc) {
  RTC_DCHECK_RUN_ON(&thread_checker_);

  std::optional<uint32_t> default_recv_ssrc = GetUnsignaledSsrc();
  if (default_recv_ssrc) {
    RTC_LOG(LS_INFO) << "Destroying old default receive stream for SSRC="
                     << ssrc << ".";
    RemoveRecvStream(*default_recv_ssrc);
  }

  StreamParams sp = unsignaled_stream_params();
  sp.ssrcs.push_back(ssrc);
  if (rtx_ssrc) {
    sp.AddFidSsrc(ssrc, *rtx_ssrc);
  }
  RTC_LOG(LS_INFO) << "Creating default receive stream for SSRC=" << ssrc
                   << ".";
  if (!AddRecvStream(sp, /*default_stream=*/true)) {
    RTC_LOG(LS_WARNING) << "Could not create default receive stream.";
  }

  // SSRC 0 returns default_recv_base_minimum_delay_ms.
  const int unsignaled_ssrc = 0;
  int default_recv_base_minimum_delay_ms =
      GetBaseMinimumPlayoutDelayMs(unsignaled_ssrc).value_or(0);
  // Set base minimum delay if it was set before for the default receive
  // stream.
  SetBaseMinimumPlayoutDelayMs(ssrc, default_recv_base_minimum_delay_ms);
  SetSink(ssrc, default_sink_);
}

void WebRtcVideoReceiveChannel::SetInterface(
    MediaChannelNetworkInterface* iface) {
  RTC_DCHECK_RUN_ON(&network_thread_checker_);
  iface ? network_thread_safety_->SetAlive()
        : network_thread_safety_->SetNotAlive();
  MediaChannelUtil::SetInterface(iface);
  // Set the RTP recv/send buffer to a bigger size.
  MediaChannelUtil::SetOption(MediaChannelNetworkInterface::ST_RTP,
                              Socket::OPT_RCVBUF, receive_buffer_size_);
}

void WebRtcVideoReceiveChannel::SetFrameDecryptor(
    uint32_t ssrc,
    scoped_refptr<FrameDecryptorInterface> frame_decryptor) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  auto matching_stream = receive_streams_.find(ssrc);
  if (matching_stream != receive_streams_.end()) {
    matching_stream->second->SetFrameDecryptor(frame_decryptor);
  }
}

void WebRtcVideoReceiveChannel::EnableSframe() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  sframe_options_.required = true;
  // TODO(bugs.webrtc.org/479862368): Propagate Sframe options to the streams.
}

bool WebRtcVideoReceiveChannel::SetBaseMinimumPlayoutDelayMs(uint32_t ssrc,
                                                             int delay_ms) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  std::optional<uint32_t> default_ssrc = GetUnsignaledSsrc();

  // SSRC of 0 represents the default receive stream.
  if (ssrc == 0) {
    default_recv_base_minimum_delay_ms_ = delay_ms;
  }

  if (ssrc == 0 && !default_ssrc) {
    return true;
  }

  if (ssrc == 0 && default_ssrc) {
    ssrc = default_ssrc.value();
  }

  auto stream = receive_streams_.find(ssrc);
  if (stream != receive_streams_.end()) {
    stream->second->SetBaseMinimumPlayoutDelayMs(delay_ms);
    return true;
  } else {
    RTC_LOG(LS_ERROR) << "No stream found to set base minimum playout delay";
    return false;
  }
}

std::optional<int> WebRtcVideoReceiveChannel::GetBaseMinimumPlayoutDelayMs(
    uint32_t ssrc) const {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  // SSRC of 0 represents the default receive stream.
  if (ssrc == 0) {
    return default_recv_base_minimum_delay_ms_;
  }

  auto stream = receive_streams_.find(ssrc);
  if (stream != receive_streams_.end()) {
    return stream->second->GetBaseMinimumPlayoutDelayMs();
  } else {
    RTC_LOG(LS_ERROR) << "No stream found to get base minimum playout delay";
    return std::nullopt;
  }
}

WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::WebRtcVideoReceiveStream(
    const Environment& env,
    Call* call,
    const StreamParams& sp,
    VideoReceiveStreamInterface::Config config,
    bool default_stream,
    const std::vector<VideoCodecSettings>& recv_codecs,
    const FlexfecReceiveStream::Config& flexfec_config)
    : env_(env),
      call_(call),
      stream_params_(sp),
      stream_(nullptr),
      default_stream_(default_stream),
      config_(std::move(config)),
      on_frame_delivered_callback_(
          std::move(config_.on_frame_delivered_callback)),
      flexfec_config_(flexfec_config),
      flexfec_stream_(nullptr),
      sink_(nullptr),
      first_frame_timestamp_(-1),
      estimated_remote_start_ntp_time_ms_(0),
      thread_checker_(call_->worker_thread()),
      receiving_(false),
      on_first_packet_(std::move(config_.on_first_packet)) {
  RTC_DCHECK(on_frame_delivered_callback_ != nullptr);
  RTC_DCHECK(config_.decoder_factory);
  RTC_DCHECK(config_.decoders.empty())
      << "Decoder info is supplied via `recv_codecs`";

  ExtractCodecInformation(recv_codecs, config_.rtp.rtx_associated_payload_types,
                          config_.rtp.raw_payload_types, config_.decoders);
  const VideoCodecSettings& codec = recv_codecs.front();
  config_.rtp.ulpfec_payload_type = codec.ulpfec.ulpfec_payload_type;
  config_.rtp.red_payload_type = codec.ulpfec.red_payload_type;
  config_.rtp.lntf.enabled = HasLntf(codec.codec);
  config_.rtp.nack.rtp_history_ms = HasNack(codec.codec) ? kNackHistoryMs : 0;
  if (codec.rtx_time && config_.rtp.nack.rtp_history_ms != 0) {
    config_.rtp.nack.rtp_history_ms = *codec.rtx_time;
  }

  if (codec.ulpfec.red_rtx_payload_type != -1) {
    config_.rtp
        .rtx_associated_payload_types[codec.ulpfec.red_rtx_payload_type] =
        codec.ulpfec.red_payload_type;
  }

  config_.renderer = this;
  flexfec_config_.payload_type = flexfec_config.payload_type;

  CreateReceiveStream();
}

WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::
    ~WebRtcVideoReceiveStream() {
  call_->DestroyVideoReceiveStream(stream_);
  previous_stats_ = std::nullopt;
  if (flexfec_stream_)
    call_->DestroyFlexfecReceiveStream(flexfec_stream_);
}



const std::vector<uint32_t>&
WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::GetSsrcs() const {
  return stream_params_.ssrcs;
}

RtpParameters
WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::GetRtpParameters() const {
  RtpParameters rtp_parameters;

  std::vector<uint32_t> primary_ssrcs;
  stream_params_.GetPrimarySsrcs(&primary_ssrcs);
  for (uint32_t ssrc : primary_ssrcs) {
    rtp_parameters.encodings.emplace_back();
    rtp_parameters.encodings.back().ssrc = ssrc;
  }

  rtp_parameters.rtcp.reduced_size =
      config_.rtp.rtcp_mode == RtcpMode::kReducedSize;

  return rtp_parameters;
}

void WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::ReconfigureCodecs(
    const std::vector<VideoCodecSettings>& recv_codecs) {
  RTC_DCHECK(stream_);
  RTC_DCHECK(!recv_codecs.empty());

  std::map<int, int> rtx_associated_payload_types;
  std::set<int> raw_payload_types;
  std::vector<VideoReceiveStreamInterface::Decoder> decoders;
  ExtractCodecInformation(recv_codecs, rtx_associated_payload_types,
                          raw_payload_types, decoders);

  const auto& codec = recv_codecs.front();

  if (config_.rtp.red_payload_type != codec.ulpfec.red_payload_type ||
      config_.rtp.ulpfec_payload_type != codec.ulpfec.ulpfec_payload_type) {
    config_.rtp.ulpfec_payload_type = codec.ulpfec.ulpfec_payload_type;
    config_.rtp.red_payload_type = codec.ulpfec.red_payload_type;
    stream_->SetProtectionPayloadTypes(config_.rtp.red_payload_type,
                                       config_.rtp.ulpfec_payload_type);
  }

  const bool has_lntf = HasLntf(codec.codec);
  if (config_.rtp.lntf.enabled != has_lntf) {
    config_.rtp.lntf.enabled = has_lntf;
    stream_->SetLossNotificationEnabled(has_lntf);
  }

  int new_history_ms = config_.rtp.nack.rtp_history_ms;
  const int rtp_history_ms = HasNack(codec.codec) ? kNackHistoryMs : 0;
  if (rtp_history_ms != config_.rtp.nack.rtp_history_ms) {
    new_history_ms = rtp_history_ms;
  }

  // The rtx-time parameter can be used to override the hardcoded default for
  // the NACK buffer length.
  if (codec.rtx_time && new_history_ms != 0) {
    new_history_ms = *codec.rtx_time;
  }

  if (config_.rtp.nack.rtp_history_ms != new_history_ms) {
    config_.rtp.nack.rtp_history_ms = new_history_ms;
    stream_->SetNackHistory(TimeDelta::Millis(new_history_ms));
  }

  if (codec.ulpfec.red_rtx_payload_type != -1) {
    rtx_associated_payload_types[codec.ulpfec.red_rtx_payload_type] =
        codec.ulpfec.red_payload_type;
  }

  if (config_.rtp.rtx_associated_payload_types !=
      rtx_associated_payload_types) {
    stream_->SetAssociatedPayloadTypes(rtx_associated_payload_types);
    rtx_associated_payload_types.swap(config_.rtp.rtx_associated_payload_types);
  }

  if (raw_payload_types != config_.rtp.raw_payload_types) {
    stream_->SetRawPayloadTypes(raw_payload_types);
    raw_payload_types.swap(config_.rtp.raw_payload_types);
  }

  if (decoders != config_.decoders) {
    stream_->SetDecoders(decoders);
    decoders.swap(config_.decoders);
  }
}

void WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::SetFlexFecPayload(
    int payload_type) {
  // TODO(bugs.webrtc.org/11993, tommi): See if it is better to always have a
  // flexfec stream object around and instead of recreating the video stream,
  // reconfigure the flexfec object from within the rtp callback (soon to be on
  // the network thread).
  if (flexfec_stream_) {
    if (flexfec_stream_->payload_type() == payload_type) {
      RTC_DCHECK_EQ(flexfec_config_.payload_type, payload_type);
      return;
    }

    flexfec_config_.payload_type = payload_type;
    flexfec_stream_->SetPayloadType(payload_type);

    if (payload_type == -1) {
      stream_->SetFlexFecProtection(nullptr);
      call_->DestroyFlexfecReceiveStream(flexfec_stream_);
      flexfec_stream_ = nullptr;
    }
  } else if (payload_type != -1) {
    flexfec_config_.payload_type = payload_type;
    if (flexfec_config_.IsCompleteAndEnabled()) {
      flexfec_stream_ = call_->CreateFlexfecReceiveStream(flexfec_config_);
      stream_->SetFlexFecProtection(flexfec_stream_);
    }
  } else {
    // Noop. No flexfec stream exists and "new" payload_type == -1.
    RTC_DCHECK(!flexfec_config_.IsCompleteAndEnabled());
    flexfec_config_.payload_type = payload_type;
  }
}

void WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::SetReceiverParameters(
    const ChangedReceiverParameters& changed_params) {
  RTC_DCHECK(stream_);
  if (changed_params.codec_settings) {
    ReconfigureCodecs(*changed_params.codec_settings);
  }

  if (changed_params.flexfec_payload_type) {
    SetFlexFecPayload(*changed_params.flexfec_payload_type);
  }
  if (changed_params.rtcp_mode) {
    RtcpMode rtcp_mode = *changed_params.rtcp_mode;
    config_.rtp.rtcp_mode = rtcp_mode;
    stream_->SetRtcpMode(rtcp_mode);
    flexfec_config_.rtcp_mode = rtcp_mode;
    if (flexfec_stream_) {
      flexfec_stream_->SetRtcpMode(rtcp_mode);
    }
  }
}

void WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::
    CreateReceiveStream() {
  RTC_DCHECK(!stream_);
  RTC_DCHECK(!flexfec_stream_);
  if (flexfec_config_.IsCompleteAndEnabled()) {
    flexfec_stream_ = call_->CreateFlexfecReceiveStream(flexfec_config_);
  }

  VideoReceiveStreamInterface::Config config = config_.Copy();
  config.rtp.protected_by_flexfec = (flexfec_stream_ != nullptr);
  config.rtp.packet_sink_ = flexfec_stream_;
  config.on_first_packet = [this](uint32_t ssrc) {
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (on_first_packet_) {
      auto cb = std::move(on_first_packet_);
      std::move(cb)(ssrc);
    }
  };
  RTC_DCHECK(on_frame_delivered_callback_ != nullptr);
  config.on_frame_delivered_callback = [this](const RtpPacketInfos& infos,
                                              Timestamp timestamp) {
    on_frame_delivered_callback_(infos, timestamp);
  };
  stream_ = call_->CreateVideoReceiveStream(std::move(config));
}

void WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::StartReceiveStream() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  receiving_ = true;
  stream_->Start();
}

void WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::StopReceiveStream() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  receiving_ = false;
  stream_->Stop();
}

void WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::OnFrame(
    const VideoFrame& frame) {
  MutexLock lock(&sink_lock_);

  int64_t time_now_ms = env_.clock().TimeInMilliseconds();
  if (first_frame_timestamp_ < 0)
    first_frame_timestamp_ = time_now_ms;
  int64_t elapsed_time_ms = time_now_ms - first_frame_timestamp_;
  if (frame.ntp_time_ms() > 0)
    estimated_remote_start_ntp_time_ms_ = frame.ntp_time_ms() - elapsed_time_ms;

  if (sink_ == nullptr) {
    RTC_LOG(LS_WARNING)
        << "VideoReceiveStreamInterface not connected to a VideoSink.";
    return;
  }

  sink_->OnFrame(frame);
}

bool WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::IsDefaultStream()
    const {
  return default_stream_;
}

void WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::SetFrameDecryptor(
    scoped_refptr<FrameDecryptorInterface> frame_decryptor) {
  config_.frame_decryptor = frame_decryptor;
  if (stream_) {
    RTC_LOG(LS_INFO)
        << "Setting FrameDecryptor (recv) because of SetFrameDecryptor, "
           "remote_ssrc="
        << config_.rtp.remote_ssrc;
    stream_->SetFrameDecryptor(std::move(frame_decryptor));
  }
}

void WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::
    SetNonSenderRttMeasurement(bool enabled) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (config_.rtp.rtcp_xr.receiver_reference_time_report != enabled) {
    config_.rtp.rtcp_xr.receiver_reference_time_report = enabled;
    if (stream_) {
      stream_->SetRtcpXr(config_.rtp.rtcp_xr);
    }
  }
}

bool WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::
    SetBaseMinimumPlayoutDelayMs(int delay_ms) {
  return stream_ ? stream_->SetBaseMinimumPlayoutDelayMs(delay_ms) : false;
}

int WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::
    GetBaseMinimumPlayoutDelayMs() const {
  return stream_ ? stream_->GetBaseMinimumPlayoutDelayMs() : 0;
}

void WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::SetSink(
    VideoSinkInterface<VideoFrame>* sink) {
  MutexLock lock(&sink_lock_);
  sink_ = sink;
}

VideoReceiverInfo
WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::GetVideoReceiverInfo(
    bool log_stats) {
  VideoReceiverInfo info;
  info.ssrc_groups = stream_params_.ssrc_groups;
  info.add_ssrc(config_.rtp.remote_ssrc);
  VideoReceiveStreamInterface::Stats stats = stream_->GetStats();
  info.decoder_implementation_name = stats.decoder_implementation_name;
  info.power_efficient_decoder = stats.power_efficient_decoder;
  if (stats.current_payload_type != -1) {
    info.codec_payload_type = stats.current_payload_type;
    auto decoder_it = absl::c_find_if(config_.decoders, [&](const auto& d) {
      return d.payload_type == stats.current_payload_type;
    });
    if (decoder_it != config_.decoders.end())
      info.codec_name = decoder_it->video_format.name;
  }
  info.payload_bytes_received = stats.rtp_stats.packet_counter.payload_bytes;
  info.header_and_padding_bytes_received =
      stats.rtp_stats.packet_counter.header_bytes +
      stats.rtp_stats.packet_counter.padding_bytes;
  info.packets_received = stats.rtp_stats.packet_counter.packets;
  info.packets_received_with_ect1 =
      stats.rtp_stats.packet_counter.packets_with_ect1;
  info.packets_received_with_ce =
      stats.rtp_stats.packet_counter.packets_with_ce;
  info.packets_lost = stats.rtp_stats.packets_lost;
  info.jitter_ms = stats.rtp_stats.jitter / (kVideoCodecClockrate / 1000);

  info.framerate_received = stats.network_frame_rate;
  info.framerate_decoded = stats.decode_frame_rate;
  info.framerate_output = stats.render_frame_rate;
  info.frame_width = stats.width;
  info.frame_height = stats.height;

  {
    MutexLock frame_cs(&sink_lock_);
    info.capture_start_ntp_time_ms = estimated_remote_start_ntp_time_ms_;
  }

  info.decode_ms = stats.decode_ms;
  info.max_decode_ms = stats.max_decode_ms;
  info.current_delay_ms = stats.current_delay_ms;
  info.target_delay_ms = stats.target_delay_ms;
  info.jitter_buffer_ms = stats.jitter_buffer_ms;
  info.jitter_buffer_delay_seconds =
      stats.jitter_buffer_delay.seconds<double>();
  info.jitter_buffer_target_delay_seconds =
      stats.jitter_buffer_target_delay.seconds<double>();
  info.jitter_buffer_emitted_count = stats.jitter_buffer_emitted_count;
  info.jitter_buffer_minimum_delay_seconds =
      stats.jitter_buffer_minimum_delay.seconds<double>();
  info.min_playout_delay_ms = stats.min_playout_delay_ms;
  info.render_delay_ms = stats.render_delay_ms;
  info.frames_received = stats.received_frame_counts.key_frames +
                         stats.received_frame_counts.delta_frames;
  info.frames_dropped = stats.frames_dropped;
  info.frames_decoded = stats.frames_decoded;
  info.key_frames_decoded = stats.key_frames_decoded;
  info.frames_rendered = stats.frames_rendered;
  info.qp_sum = stats.qp_sum;
  info.corruption_score_sum = stats.corruption_score_sum;
  info.corruption_score_squared_sum = stats.corruption_score_squared_sum;
  info.corruption_score_count = stats.corruption_score_count;
  info.total_decode_time = stats.total_decode_time;
  info.total_processing_delay = stats.total_processing_delay;
  info.total_assembly_time = stats.total_assembly_time;
  info.frames_assembled_from_multiple_packets =
      stats.frames_assembled_from_multiple_packets;
  info.last_packet_received = stats.rtp_stats.last_packet_received;
  info.estimated_playout_ntp_timestamp_ms =
      stats.estimated_playout_ntp_timestamp_ms;
  info.first_frame_received_to_decoded_ms =
      stats.first_frame_received_to_decoded_ms;
  info.total_inter_frame_delay = stats.total_inter_frame_delay;
  info.total_squared_inter_frame_delay = stats.total_squared_inter_frame_delay;
  info.interframe_delay_max_ms = stats.interframe_delay_max_ms;
  info.freeze_count = stats.freeze_count;
  info.pause_count = stats.pause_count;
  info.total_freezes_duration_ms = stats.total_freezes_duration_ms;
  info.total_pauses_duration_ms = stats.total_pauses_duration_ms;

  info.content_type = stats.content_type;

  info.firs_sent = stats.rtcp_packet_type_counts.fir_packets;
  info.plis_sent = stats.rtcp_packet_type_counts.pli_packets;
  info.nacks_sent = stats.rtcp_packet_type_counts.nack_packets;
  // TODO(bugs.webrtc.org/10662): Add stats for LNTF.

  info.timing_frame_info = stats.timing_frame_info;

  if (stats.rtx_rtp_stats.has_value()) {
    info.retransmitted_packets_received =
        stats.rtx_rtp_stats->packet_counter.packets;
    info.retransmitted_bytes_received =
        stats.rtx_rtp_stats->packet_counter.payload_bytes;
    // RTX information gets added to primary counters.
    info.payload_bytes_received +=
        stats.rtx_rtp_stats->packet_counter.payload_bytes;
    info.header_and_padding_bytes_received +=
        stats.rtx_rtp_stats->packet_counter.header_bytes +
        stats.rtx_rtp_stats->packet_counter.padding_bytes;
    info.packets_received += stats.rtx_rtp_stats->packet_counter.packets;
    info.packets_received_with_ect1 +=
        stats.rtx_rtp_stats->packet_counter.packets_with_ect1;
    info.packets_received_with_ce +=
        stats.rtx_rtp_stats->packet_counter.packets_with_ce;
  }

  if (flexfec_stream_) {
    const ReceiveStatistics* fec_stats = flexfec_stream_->GetStats();
    if (fec_stats) {
      const StreamStatistician* statistican =
          fec_stats->GetStatistician(flexfec_config_.remote_ssrc);
      if (statistican) {
        const RtpReceiveStats fec_rtp_stats = statistican->GetStats();
        info.fec_packets_received = fec_rtp_stats.packet_counter.packets;
        // TODO(bugs.webrtc.org/15250): implement fecPacketsDiscarded.
        info.fec_bytes_received = fec_rtp_stats.packet_counter.payload_bytes;
        // FEC information gets added to primary counters.
        info.payload_bytes_received +=
            fec_rtp_stats.packet_counter.payload_bytes;
        info.header_and_padding_bytes_received +=
            fec_rtp_stats.packet_counter.header_bytes +
            fec_rtp_stats.packet_counter.padding_bytes;
        info.packets_received += fec_rtp_stats.packet_counter.packets;
        info.packets_received_with_ect1 +=
            fec_rtp_stats.packet_counter.packets_with_ect1;
        info.packets_received_with_ce +=
            fec_rtp_stats.packet_counter.packets_with_ce;
      } else {
        info.fec_packets_received = 0;
      }
    }
  }

  // remote-outbound-rtp stats.
  info.last_sender_report_timestamp = stats.last_sender_report_timestamp;
  info.last_sender_report_utc_timestamp =
      stats.last_sender_report_utc_timestamp;
  info.last_sender_report_remote_utc_timestamp =
      stats.last_sender_report_remote_utc_timestamp;
  info.sender_reports_packets_sent = stats.sender_reports_packets_sent;
  info.sender_reports_bytes_sent = stats.sender_reports_bytes_sent;
  info.sender_reports_reports_count = stats.sender_reports_reports_count;
  // Non-sender RTT (RRTR/DLRR), surfaced for recvonly endpoints; mirrors the
  // audio path.
  info.round_trip_time = stats.round_trip_time;
  info.round_trip_time_measurements = stats.round_trip_time_measurements;
  info.total_round_trip_time = stats.total_round_trip_time;

  if (log_stats)
    RTC_LOG(LS_INFO) << stats.ToString(env_.clock().TimeInMilliseconds(),
                                       previous_stats_);

  previous_stats_ = stats;

  return info;
}

void WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::
    SetRecordableEncodedFrameCallback(
        std::function<void(const RecordableEncodedFrame&)> callback) {
  if (stream_) {
    stream_->SetAndGetRecordingState(
        VideoReceiveStreamInterface::RecordingState(std::move(callback)),
        /*generate_key_frame=*/true);
  } else {
    RTC_LOG(LS_ERROR) << "Absent receive stream; ignoring setting encoded "
                         "frame sink";
  }
}

void WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::
    ClearRecordableEncodedFrameCallback() {
  if (stream_) {
    stream_->SetAndGetRecordingState(
        VideoReceiveStreamInterface::RecordingState(),
        /*generate_key_frame=*/false);
  } else {
    RTC_LOG(LS_ERROR) << "Absent receive stream; ignoring clearing encoded "
                         "frame sink";
  }
}

void WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::GenerateKeyFrame() {
  if (stream_) {
    stream_->GenerateKeyFrame();
  } else {
    RTC_LOG(LS_ERROR)
        << "Absent receive stream; ignoring key frame generation request.";
  }
}

void WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::
    SetDepacketizerToDecoderFrameTransformer(
        scoped_refptr<FrameTransformerInterface> frame_transformer) {
  config_.frame_transformer = frame_transformer;
  if (stream_)
    stream_->SetDepacketizerToDecoderFrameTransformer(
        std::move(frame_transformer));
}

void WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream::UpdateRtxSsrc(
    uint32_t ssrc) {
  stream_->UpdateRtxSsrc(ssrc);
}
WebRtcVideoReceiveChannel::WebRtcVideoReceiveStream*
WebRtcVideoReceiveChannel::FindReceiveStream(uint32_t ssrc) {
  if (ssrc == 0) {
    std::optional<uint32_t> default_ssrc = GetUnsignaledSsrc();
    if (!default_ssrc) {
      return nullptr;
    }
    ssrc = *default_ssrc;
  }
  auto it = receive_streams_.find(ssrc);
  if (it != receive_streams_.end()) {
    return it->second;
  }
  return nullptr;
}

void WebRtcVideoReceiveChannel::SetRecordableEncodedFrameCallback(
    uint32_t ssrc,
    std::function<void(const RecordableEncodedFrame&)> callback) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  WebRtcVideoReceiveStream* stream = FindReceiveStream(ssrc);
  if (stream) {
    stream->SetRecordableEncodedFrameCallback(std::move(callback));
  } else {
    RTC_LOG(LS_ERROR) << "Absent receive stream; ignoring setting encoded "
                         "frame sink for ssrc "
                      << ssrc;
  }
}

void WebRtcVideoReceiveChannel::ClearRecordableEncodedFrameCallback(
    uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  WebRtcVideoReceiveStream* stream = FindReceiveStream(ssrc);
  if (stream) {
    stream->ClearRecordableEncodedFrameCallback();
  } else {
    RTC_LOG(LS_ERROR) << "Absent receive stream; ignoring clearing encoded "
                         "frame sink for ssrc "
                      << ssrc;
  }
}

void WebRtcVideoReceiveChannel::RequestRecvKeyFrame(uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  WebRtcVideoReceiveStream* stream = FindReceiveStream(ssrc);
  if (stream) {
    return stream->GenerateKeyFrame();
  } else {
    RTC_LOG(LS_ERROR)
        << "Absent receive stream; ignoring key frame generation for ssrc "
        << ssrc;
  }
}

void WebRtcVideoReceiveChannel::SetDepacketizerToDecoderFrameTransformer(
    uint32_t ssrc,
    scoped_refptr<FrameTransformerInterface> frame_transformer) {
  RTC_DCHECK(frame_transformer);
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (ssrc == 0) {
    // If the receiver is unsignaled, save the frame transformer and set it
    // when the stream is associated with an ssrc.
    unsignaled_frame_transformer_ = std::move(frame_transformer);
    return;
  }

  auto matching_stream = receive_streams_.find(ssrc);
  if (matching_stream != receive_streams_.end()) {
    matching_stream->second->SetDepacketizerToDecoderFrameTransformer(
        std::move(frame_transformer));
  }
}

// ------------------------- VideoCodecSettings --------------------

VideoCodecSettings::VideoCodecSettings(const Codec& codec)
    : codec(codec), flexfec_payload_type(-1), rtx_payload_type(-1) {}

bool VideoCodecSettings::operator==(const VideoCodecSettings& other) const {
  return codec == other.codec && ulpfec == other.ulpfec &&
         flexfec_payload_type == other.flexfec_payload_type &&
         rtx_payload_type == other.rtx_payload_type &&
         rtx_time == other.rtx_time;
}

bool VideoCodecSettings::EqualsDisregardingFlexfec(
    const VideoCodecSettings& a,
    const VideoCodecSettings& b) {
  return a.codec == b.codec && a.ulpfec == b.ulpfec &&
         a.rtx_payload_type == b.rtx_payload_type && a.rtx_time == b.rtx_time;
}

bool VideoCodecSettings::operator!=(const VideoCodecSettings& other) const {
  return !(*this == other);
}

}  // namespace webrtc
