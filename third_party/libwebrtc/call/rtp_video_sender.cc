/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "call/rtp_video_sender.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"
#include "api/call/bitrate_allocation.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/fec_controller.h"
#include "api/field_trials_view.h"
#include "api/frame_transformer_interface.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_headers.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/transport/rtp/dependency_descriptor.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/frequency.h"
#include "api/units/time_delta.h"
#include "api/video/encoded_image.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_layers_allocation.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "call/rtp_config.h"
#include "call/rtp_payload_params.h"
#include "call/rtp_transport_controller_send_interface.h"
#include "common_video/frame_counts.h"
#include "common_video/generic_frame_descriptor/generic_frame_info.h"
#include "modules/include/module_fec_types.h"
#include "modules/pacing/packet_router.h"
#include "modules/rtp_rtcp/include/flexfec_sender.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_impl2.h"
#include "modules/rtp_rtcp/source/rtp_sender.h"
#include "modules/rtp_rtcp/source/rtp_sender_video.h"
#include "modules/rtp_rtcp/source/rtp_sequence_number_map.h"
#include "modules/rtp_rtcp/source/rtp_video_header.h"
#include "modules/rtp_rtcp/source/ulpfec_generator.h"
#include "modules/rtp_rtcp/source/video_fec_generator.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "rtc_base/checks.h"
#include "rtc_base/containers/flat_map.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/trace_event.h"

namespace webrtc {

namespace webrtc_internal_rtp_video_sender {

RtpStreamSender::RtpStreamSender(
    std::unique_ptr<ModuleRtpRtcpImpl2> rtp_rtcp,
    std::unique_ptr<RTPSenderVideo> sender_video,
    std::unique_ptr<VideoFecGenerator> fec_generator)
    : rtp_rtcp(std::move(rtp_rtcp)),
      sender_video(std::move(sender_video)),
      fec_generator(std::move(fec_generator)) {}

RtpStreamSender::~RtpStreamSender() = default;

}  // namespace webrtc_internal_rtp_video_sender

namespace {
const int kMinSendSidePacketHistorySize = 600;
// We don't do MTU discovery, so assume that we have the standard ethernet MTU.
const size_t kPathMTU = 1500;

using webrtc_internal_rtp_video_sender::RtpStreamSender;

bool PayloadTypeSupportsSkippingFecPackets(absl::string_view payload_name,
                                           const FieldTrialsView& trials) {
  const VideoCodecType codecType = PayloadStringToCodecType(payload_name);
  if (codecType == kVideoCodecVP8 || codecType == kVideoCodecVP9) {
    return true;
  }
  if (codecType == kVideoCodecGeneric &&
      trials.IsEnabled("WebRTC-GenericPictureId")) {
    return true;
  }
  return false;
}

bool ShouldDisableRedAndUlpfec(bool flexfec_enabled,
                               const RtpConfig& rtp_config,
                               const FieldTrialsView& trials) {
  // Consistency of NACK and RED+ULPFEC parameters is checked in this function.
  const bool nack_enabled = rtp_config.nack.rtp_history_ms > 0;

  // Shorthands.
  auto IsRedEnabled = [&]() { return rtp_config.ulpfec.red_payload_type >= 0; };
  auto IsUlpfecEnabled = [&]() {
    return rtp_config.ulpfec.ulpfec_payload_type >= 0;
  };

  bool should_disable_red_and_ulpfec = false;

  if (trials.IsEnabled("WebRTC-DisableUlpFecExperiment")) {
    RTC_LOG(LS_INFO) << "Experiment to disable sending ULPFEC is enabled.";
    should_disable_red_and_ulpfec = true;
  }

  // If enabled, FlexFEC takes priority over RED+ULPFEC.
  if (flexfec_enabled) {
    if (IsUlpfecEnabled()) {
      RTC_LOG(LS_INFO)
          << "Both FlexFEC and ULPFEC are configured. Disabling ULPFEC.";
    }
    should_disable_red_and_ulpfec = true;
  }

  // Payload types without picture ID cannot determine that a stream is complete
  // without retransmitting FEC, so using ULPFEC + NACK for H.264 (for instance)
  // is a waste of bandwidth since FEC packets still have to be transmitted.
  // Note that this is not the case with FlexFEC.
  if (nack_enabled && IsUlpfecEnabled() &&
      !PayloadTypeSupportsSkippingFecPackets(rtp_config.payload_name, trials)) {
    RTC_LOG(LS_WARNING)
        << "Transmitting payload type without picture ID using "
           "NACK+ULPFEC is a waste of bandwidth since ULPFEC packets "
           "also have to be retransmitted. Disabling ULPFEC.";
    should_disable_red_and_ulpfec = true;
  }

  // Verify payload types.
  if (IsUlpfecEnabled() ^ IsRedEnabled()) {
    RTC_LOG(LS_WARNING)
        << "Only RED or only ULPFEC enabled, but not both. Disabling both.";
    should_disable_red_and_ulpfec = true;
  }

  return should_disable_red_and_ulpfec;
}

// TODO(brandtr): Update this function when we support multistream protection.
std::unique_ptr<VideoFecGenerator> MaybeCreateFecGenerator(
    const Environment& env,
    const RtpConfig& rtp,
    const std::map<uint32_t, RtpState>& suspended_ssrcs,
    int simulcast_index) {
  // If flexfec is configured that takes priority.
  if (rtp.flexfec.payload_type >= 0) {
    RTC_DCHECK_GE(rtp.flexfec.payload_type, 0);
    RTC_DCHECK_LE(rtp.flexfec.payload_type, 127);
    if (rtp.flexfec.ssrc == 0) {
      RTC_LOG(LS_WARNING) << "FlexFEC is enabled, but no FlexFEC SSRC given. "
                             "Therefore disabling FlexFEC.";
      return nullptr;
    }
    if (rtp.flexfec.protected_media_ssrcs.empty()) {
      RTC_LOG(LS_WARNING)
          << "FlexFEC is enabled, but no protected media SSRC given. "
             "Therefore disabling FlexFEC.";
      return nullptr;
    }

    if (rtp.flexfec.protected_media_ssrcs.size() > 1) {
      RTC_LOG(LS_WARNING)
          << "The supplied FlexfecConfig contained multiple protected "
             "media streams, but our implementation currently only "
             "supports protecting a single media stream. "
             "To avoid confusion, disabling FlexFEC completely.";
      return nullptr;
    }

    if (absl::c_find(rtp.flexfec.protected_media_ssrcs,
                     rtp.ssrcs[simulcast_index]) ==
        rtp.flexfec.protected_media_ssrcs.end()) {
      // Media SSRC not among flexfec protected SSRCs.
      return nullptr;
    }

    const RtpState* rtp_state = nullptr;
    auto it = suspended_ssrcs.find(rtp.flexfec.ssrc);
    if (it != suspended_ssrcs.end()) {
      rtp_state = &it->second;
    }

    RTC_DCHECK_EQ(1U, rtp.flexfec.protected_media_ssrcs.size());
    return std::make_unique<FlexfecSender>(
        env, rtp.flexfec.payload_type, rtp.flexfec.ssrc,
        rtp.flexfec.protected_media_ssrcs[0], rtp.mid, rtp.extensions,
        RTPSender::FecExtensionSizes(), rtp_state);
  } else if (rtp.ulpfec.red_payload_type >= 0 &&
             rtp.ulpfec.ulpfec_payload_type >= 0 &&
             !ShouldDisableRedAndUlpfec(/*flexfec_enabled=*/false, rtp,
                                        env.field_trials())) {
    // Flexfec not configured, but ulpfec is and is not disabled.
    return std::make_unique<UlpfecGenerator>(env, rtp.ulpfec.red_payload_type,
                                             rtp.ulpfec.ulpfec_payload_type);
  }

  // Not a single FEC is given.
  return nullptr;
}

// Returns the state that the stream with `ssrc` had when it was suspended, if
// any.
std::optional<RtpState> FindSuspendedRtpState(
    const std::map<uint32_t, RtpState>& suspended_ssrcs,
    uint32_t ssrc) {
  auto it = suspended_ssrcs.find(ssrc);
  if (it == suspended_ssrcs.end()) {
    return std::nullopt;
  }
  return it->second;
}

// Configures RTX for the media stream at `simulcast_index`.
void ConfigureRtx(const RtpConfig& rtp_config,
                  size_t simulcast_index,
                  RtpRtcpInterface::Configuration& configuration) {
  // Configure RTX payload types.
  RTC_DCHECK_GE(rtp_config.rtx.payload_type, 0);
  RtpStreamConfig stream_config = rtp_config.GetStreamConfig(simulcast_index);
  RTC_DCHECK(stream_config.rtx);
  configuration.rtx_payload_types = {
      {stream_config.payload_type, stream_config.rtx->payload_type}};
  configuration.rtx_mode = kRtxRetransmitted | kRtxRedundantPayloads;
  if (rtp_config.ulpfec.red_payload_type != -1 &&
      rtp_config.ulpfec.red_rtx_payload_type != -1) {
    configuration.rtx_payload_types[rtp_config.ulpfec.red_payload_type] =
        rtp_config.ulpfec.red_rtx_payload_type;
  }
}

// Configures the RTP module that sends the media stream at `simulcast_index`.
void ConfigureRtpModule(const RtpConfig& rtp_config,
                        size_t simulcast_index,
                        RtpRtcpInterface& rtp_rtcp) {
  rtp_rtcp.SetSendingStatus(false);
  rtp_rtcp.SetSendingMediaStatus(false);
  rtp_rtcp.SetRTCPStatus(RtcpMode::kCompound);
  // Set NACK.
  rtp_rtcp.SetStorePacketsStatus(true, kMinSendSidePacketHistorySize);

  for (const RtpExtension& extension : rtp_config.extensions) {
    RTC_DCHECK(RtpExtension::IsSupportedForVideo(extension.uri));
    rtp_rtcp.RegisterRtpHeaderExtension(extension.uri, extension.id);
  }

  // Simulcast has one module for each layer. Set the CNAME on all modules.
  rtp_rtcp.SetCNAME(rtp_config.c_name.c_str());
  rtp_rtcp.SetMaxRtpPacketSize(rtp_config.max_packet_size);
  rtp_rtcp.RegisterSendPayloadFrequency(
      rtp_config.GetStreamConfig(simulcast_index).payload_type,
      kVideoPayloadTypeFrequency);
}

std::vector<RtpStreamSender> CreateRtpStreamSenders(
    const Environment& env,
    const RtpConfig& rtp_config,
    const RtpSenderObservers& observers,
    int rtcp_report_interval_ms,
    Transport* send_transport,
    RtpTransportControllerSendInterface* transport,
    const std::map<uint32_t, RtpState>& suspended_ssrcs,
    FrameEncryptorInterface* frame_encryptor,
    const CryptoOptions& crypto_options,
    scoped_refptr<FrameTransformerInterface> frame_transformer) {
  RTC_DCHECK_GT(rtp_config.ssrcs.size(), 0);

  RtpRtcpInterface::Configuration configuration;
  configuration.audio = false;
  configuration.receiver_only = false;
  configuration.outgoing_transport = send_transport;
  configuration.intra_frame_callback = observers.intra_frame_callback;
  configuration.network_link_rtcp_observer = transport->GetRtcpObserver();
  configuration.network_state_estimate_observer =
      transport->network_state_estimate_observer();
  configuration.rtt_stats = observers.rtcp_rtt_stats;
  configuration.rtcp_packet_type_counter_observer =
      observers.rtcp_type_observer;
  configuration.report_block_data_observer =
      observers.report_block_data_observer;
  configuration.paced_sender = transport->packet_sender();
  configuration.send_bitrate_observer = observers.bitrate_observer;
  configuration.send_packet_observer = observers.send_packet_observer;
  configuration.rtp_stats_callback = observers.rtp_stats;
  configuration.frame_encryptor = frame_encryptor;
  configuration.require_frame_encryption =
      crypto_options.sframe.require_frame_encryption;
  configuration.extmap_allow_mixed = rtp_config.extmap_allow_mixed;
  configuration.mid = rtp_config.mid;
  configuration.rtcp_report_interval_ms = rtcp_report_interval_ms;
  configuration.enable_send_packet_batching =
      rtp_config.enable_send_packet_batching;

  std::vector<RtpStreamSender> rtp_streams;

  RTC_DCHECK(rtp_config.rtx.ssrcs.empty() ||
             rtp_config.rtx.ssrcs.size() == rtp_config.ssrcs.size());

  // Some streams could have been disabled, but the rids are still there.
  // This will occur when simulcast has been disabled for a codec (e.g. VP9)
  RTC_DCHECK(rtp_config.rids.empty() ||
             rtp_config.rids.size() >= rtp_config.ssrcs.size());

  for (size_t i = 0; i < rtp_config.ssrcs.size(); ++i) {
    RTPSenderVideo::Config video_config;
    configuration.local_media_ssrc = rtp_config.ssrcs[i];

    std::unique_ptr<VideoFecGenerator> fec_generator =
        MaybeCreateFecGenerator(env, rtp_config, suspended_ssrcs, i);
    configuration.fec_generator = fec_generator.get();

    configuration.rtx_send_ssrc =
        rtp_config.GetRtxSsrcAssociatedWithMediaSsrc(rtp_config.ssrcs[i]);
    RTC_DCHECK_EQ(configuration.rtx_send_ssrc.has_value(),
                  !rtp_config.rtx.ssrcs.empty());

    // Set up RTX if available.
    if (!rtp_config.rtx.ssrcs.empty()) {
      ConfigureRtx(rtp_config, i, configuration);
    }

    // Restore RTP state if previous existed.
    configuration.rtp_state =
        FindSuspendedRtpState(suspended_ssrcs, rtp_config.ssrcs[i]);
    configuration.rtx_rtp_state =
        configuration.rtx_send_ssrc.has_value()
            ? FindSuspendedRtpState(suspended_ssrcs,
                                    *configuration.rtx_send_ssrc)
            : std::nullopt;

    configuration.rid = (i < rtp_config.rids.size()) ? rtp_config.rids[i] : "";

    auto rtp_rtcp = ModuleRtpRtcpImpl2::CreateSendModule(env, configuration);
    ConfigureRtpModule(rtp_config, i, *rtp_rtcp);

    video_config.clock = &env.clock();
    video_config.rtp_sender = rtp_rtcp->RtpSender();
    video_config.frame_encryptor = frame_encryptor;
    video_config.require_frame_encryption =
        crypto_options.sframe.require_frame_encryption;
    video_config.sframe_required_by_negotiation =
        rtp_config.sframe_options.required;
    video_config.sframe_encryption_config =
        rtp_config.sframe_options.encryption_config;
    video_config.field_trials = &env.field_trials();
    video_config.enable_retransmit_all_layers =
        !video_config.field_trials->IsDisabled(
            "WebRTC-Video-EnableRetransmitAllLayers");

    const bool using_flexfec =
        fec_generator &&
        fec_generator->GetFecType() == VideoFecGenerator::FecType::kFlexFec;
    const bool should_disable_red_and_ulpfec = ShouldDisableRedAndUlpfec(
        using_flexfec, rtp_config, env.field_trials());
    if (!should_disable_red_and_ulpfec &&
        rtp_config.ulpfec.red_payload_type != -1) {
      video_config.red_payload_type = rtp_config.ulpfec.red_payload_type;
    }
    if (fec_generator) {
      video_config.fec_type = fec_generator->GetFecType();
      video_config.fec_overhead_bytes = fec_generator->MaxPacketOverhead();
    }
    video_config.frame_transformer = frame_transformer;
    video_config.task_queue_factory = &env.task_queue_factory();
    video_config.raw_packetization = rtp_config.raw_payload;

    auto sender_video = std::make_unique<RTPSenderVideo>(video_config);
    rtp_streams.emplace_back(std::move(rtp_rtcp), std::move(sender_video),
                             std::move(fec_generator));
  }
  return rtp_streams;
}

// Maps each media SSRC to the RTP module that sends it. `rtp_streams` are
// sorted in the same order as `ssrcs`.
flat_map<uint32_t, RtpRtcpInterface*> CreateSsrcToRtpModuleMap(
    const std::vector<uint32_t>& ssrcs,
    const std::vector<RtpStreamSender>& rtp_streams) {
  std::vector<std::pair<uint32_t, RtpRtcpInterface*>> ssrc_to_rtp_module;
  ssrc_to_rtp_module.reserve(ssrcs.size());
  for (size_t i = 0; i < ssrcs.size(); ++i) {
    ssrc_to_rtp_module.emplace_back(ssrcs[i], rtp_streams[i].rtp_rtcp.get());
  }
  return flat_map<uint32_t, RtpRtcpInterface*>(std::move(ssrc_to_rtp_module));
}

// Creates the payload params for each media SSRC, in the same order as
// `ssrcs`, i.e. the same order as the RTP modules.
std::vector<RtpPayloadParams> CreatePayloadParams(
    const Environment& env,
    const std::vector<uint32_t>& ssrcs,
    const std::map<uint32_t, RtpPayloadState>& states) {
  std::vector<RtpPayloadParams> params;
  params.reserve(ssrcs.size());
  for (uint32_t ssrc : ssrcs) {
    // Restore state if it previously existed.
    auto it = states.find(ssrc);
    params.emplace_back(env, ssrc, it != states.end() ? &it->second : nullptr);
  }
  return params;
}

// Returns the shared frame id to continue from, i.e. the largest one among the
// previous states of the media SSRCs.
int64_t MaxSharedFrameId(const std::vector<uint32_t>& ssrcs,
                         const std::map<uint32_t, RtpPayloadState>& states) {
  int64_t shared_frame_id = 0;
  for (uint32_t ssrc : ssrcs) {
    auto it = states.find(ssrc);
    if (it != states.end()) {
      shared_frame_id = std::max(shared_frame_id, it->second.shared_frame_id);
    }
  }
  return shared_frame_id;
}

// Returns the CSRCs that fit in an RTP header, i.e. the first kRtpCsrcSize.
std::vector<uint32_t> LimitCsrcs(std::span<const uint32_t> csrcs) {
  std::span<const uint32_t> limited =
      csrcs.first(std::min<size_t>(csrcs.size(), kRtpCsrcSize));
  return std::vector<uint32_t>(limited.begin(), limited.end());
}

bool TransportSeqNumExtensionConfigured(const RtpConfig& config) {
  return absl::c_any_of(config.extensions, [](const RtpExtension& ext) {
    return ext.uri == RtpExtension::kTransportSequenceNumberUri;
  });
}

// Returns true when some coded video sequence can be decoded starting with
// this frame without requiring any previous frames.
// e.g. it is the same as a key frame when spatial scalability is not used.
// When spatial scalability is used, then it is true for layer frames of
// a key frame without inter-layer dependencies.
bool IsFirstFrameOfACodedVideoSequence(
    const EncodedImage& encoded_image,
    const CodecSpecificInfo* codec_specific_info) {
  if (!encoded_image.IsKey()) {
    return false;
  }

  if (codec_specific_info != nullptr) {
    if (codec_specific_info->generic_frame_info.has_value()) {
      // This function is used before
      // `codec_specific_info->generic_frame_info->frame_diffs` are calculated,
      // so need to use a more complicated way to check for presence of the
      // dependencies.
      return absl::c_none_of(
          codec_specific_info->generic_frame_info->encoder_buffers,
          [](const CodecBufferUsage& buffer) { return buffer.referenced; });
    }

    if (codec_specific_info->codecType == VideoCodecType::kVideoCodecVP8 ||
        codec_specific_info->codecType == VideoCodecType::kVideoCodecH264 ||
        codec_specific_info->codecType == VideoCodecType::kVideoCodecGeneric) {
      // These codecs do not support intra picture dependencies, so a frame
      // marked as a key frame should be a key frame.
      return true;
    }
  }

  // Without depenedencies described in generic format do an educated guess.
  // It might be wrong for VP9 with spatial layer 0 skipped or higher spatial
  // layer not depending on the spatial layer 0. This corner case is unimportant
  // for current usage of this helper function.

  // Use <= to accept both 0 (i.e. the first) and nullopt (i.e. the only).
  return encoded_image.SpatialIndex() <= 0;
}

}  // namespace

RtpVideoSender::RtpVideoSender(
    const Environment& env,
    TaskQueueBase* absl_nonnull transport_queue,
    const std::map<uint32_t, RtpState>& suspended_ssrcs,
    const std::map<uint32_t, RtpPayloadState>& states,
    const RtpConfig& rtp_config,
    int rtcp_report_interval_ms,
    Transport* send_transport,
    const RtpSenderObservers& observers,
    RtpTransportControllerSendInterface* transport,
    std::unique_ptr<FecController> fec_controller,
    FrameEncryptorInterface* frame_encryptor,
    const CryptoOptions& crypto_options,
    scoped_refptr<FrameTransformerInterface> frame_transformer)
    : env_(env),
      use_frame_rate_for_overhead_(
          env.field_trials().IsEnabled("WebRTC-Video-UseFrameRateForOverhead")),
      has_packet_feedback_(TransportSeqNumExtensionConfigured(rtp_config)),
      transport_queue_(*transport_queue),
      fec_controller_(std::move(fec_controller)),
      rtp_streams_(CreateRtpStreamSenders(env,
                                          rtp_config,
                                          observers,
                                          rtcp_report_interval_ms,
                                          send_transport,
                                          transport,
                                          suspended_ssrcs,
                                          frame_encryptor,
                                          crypto_options,
                                          std::move(frame_transformer))),
      rtp_config_(rtp_config),
      transport_(transport),
      csrcs_(LimitCsrcs(rtp_config.csrcs)),
      shared_frame_id_(MaxSharedFrameId(rtp_config.ssrcs, states)),
      independent_frame_ids_(
          env.field_trials().IsDisabled("WebRTC-GenericDescriptorAuth")),
      params_(CreatePayloadParams(env, rtp_config.ssrcs, states)),
      frame_counts_(rtp_config.ssrcs.size()),
      frame_count_observer_(observers.frame_count_observer),
      ssrc_to_rtp_module_(
          CreateSsrcToRtpModuleMap(rtp_config.ssrcs, rtp_streams_)),
      safety_(PendingTaskSafetyFlag::CreateAttachedToTaskQueue(
          /*alive=*/true,
          transport_queue)) {
  transport_checker_.Detach();
  RTC_DCHECK_EQ(rtp_config_.ssrcs.size(), rtp_streams_.size());
  if (has_packet_feedback_)
    transport_->IncludeOverheadInPacedSender();

  // Currently, both ULPFEC and FlexFEC use the same FEC rate calculation logic,
  // so enable that logic if either of those FEC schemes are enabled.
  const bool fec_enabled =
      absl::c_any_of(rtp_streams_, [](const RtpStreamSender& stream) {
        return stream.fec_generator != nullptr;
      });
  fec_controller_->SetProtectionMethod(fec_enabled, NackEnabled());

  fec_controller_->SetProtectionCallback(this);

  // Construction happens on the worker thread (see Call::CreateVideoSendStream)
  // but subseqeuent calls to the RTP state will happen on one of two threads:
  // * The pacer thread for actually sending packets.
  // * The transport thread when tearing down and quering GetRtpState().
  // Detach thread checkers.
  for (const RtpStreamSender& stream : rtp_streams_) {
    stream.rtp_rtcp->OnPacketSendingThreadSwitched();
  }
}

RtpVideoSender::~RtpVideoSender() {
  RTC_DCHECK_RUN_ON(&transport_checker_);
  SetActiveModulesLocked(/*sending=*/false);
}

void RtpVideoSender::SetSending(bool enabled) {
  RTC_DCHECK_RUN_ON(&transport_checker_);
  MutexLock lock(&mutex_);
  if (enabled == active_) {
    return;
  }
  SetActiveModulesLocked(/*sending=*/enabled);
}

void RtpVideoSender::SetActiveModulesLocked(bool sending) {
  RTC_DCHECK_RUN_ON(&transport_checker_);
  if (active_ == sending) {
    return;
  }
  active_ = sending;
  for (const RtpStreamSender& stream : rtp_streams_) {
    SetModuleIsActive(sending, *stream.rtp_rtcp);
  }
  auto* feedback_provider = transport_->GetStreamFeedbackProvider();
  if (!sending) {
    feedback_provider->DeRegisterStreamFeedbackObserver(this);
  } else {
    feedback_provider->RegisterStreamFeedbackObserver(rtp_config_.ssrcs, this);
  }
}

void RtpVideoSender::SetModuleIsActive(bool sending,
                                       RtpRtcpInterface& rtp_module) {
  rtp_module.SetSendingMediaStatus(sending);
  // OnVideoLayersAllocationUpdated() enables media before the module is
  // activated here, so the RTCP sending status tells whether the module is
  // active.
  if (rtp_module.Sending() == sending) {
    return;
  }

  // Sends a kRtcpByeCode when going from true to false.
  rtp_module.SetSendingStatus(sending);
  if (sending) {
    transport_->RegisterSendingRtpStream(rtp_module);
  } else {
    transport_->DeRegisterSendingRtpStream(rtp_module);
  }
}

bool RtpVideoSender::IsActive() {
  RTC_DCHECK_RUN_ON(&transport_checker_);
  MutexLock lock(&mutex_);
  return IsActiveLocked();
}

bool RtpVideoSender::IsActiveLocked() {
  return active_ && !rtp_streams_.empty();
}

EncodedImageCallback::Result RtpVideoSender::OnEncodedImage(
    const EncodedImage& encoded_image,
    const CodecSpecificInfo* codec_specific_info) {
  fec_controller_->UpdateWithEncodedData(encoded_image.size(),
                                         encoded_image.frame_type());
  MutexLock lock(&mutex_);
  RTC_DCHECK(!rtp_streams_.empty());
  if (!active_)
    return Result(Result::ERROR_SEND_FAILED);

  shared_frame_id_++;
  size_t simulcast_index = encoded_image.SimulcastIndex().value_or(0);
  if (simulcast_index >= rtp_streams_.size()) {
    RTC_DCHECK_NOTREACHED();
    return Result(Result::ERROR_SEND_FAILED);
  }

  // Create the RTP video header before checking if the RTP module is sending,
  // so that `params_` also observe frames that are dropped below. `params_`
  // track the encoder's reference buffers to describe frame dependencies.
  // Skipping a frame that the encoder later references would instead describe
  // a dependency on an older frame, which can be arbitrarily far back when
  // simulcast streams share frame ids.
  std::optional<int64_t> frame_id;
  if (!independent_frame_ids_) {
    frame_id = shared_frame_id_;
  }
  RTPVideoHeader video_header = params_[simulcast_index].GetRtpVideoHeader(
      encoded_image, codec_specific_info, frame_id);

  uint32_t rtp_timestamp =
      encoded_image.RtpTimestamp() +
      rtp_streams_[simulcast_index].rtp_rtcp->StartTimestamp();

  // RTCPSender has it's own copy of the timestamp offset, added in
  // RTCPSender::BuildSR, hence we must not add the in the offset for this call.
  // TODO(nisse): Delete RTCPSender:timestamp_offset_, and see if we can confine
  // knowledge of the offset to a single place.
  if (!rtp_streams_[simulcast_index].rtp_rtcp->OnSendingRtpFrame(
          encoded_image.RtpTimestamp(), encoded_image.capture_time_ms_,
          rtp_config_.GetStreamConfig(simulcast_index).payload_type,
          encoded_image.IsKey()) &&
      // OnSendingRtpFrame() returns false until the module has been activated
      // on the transport queue, but OnVideoLayersAllocationUpdated() enables
      // media right away. Send such frames rather than drop them.
      !rtp_streams_[simulcast_index].rtp_rtcp->SendingMedia()) {
    // The payload router could be active but this module isn't sending.
    return Result(Result::ERROR_SEND_FAILED);
  }

  TimeDelta expected_retransmission_time = TimeDelta::PlusInfinity();
  if (encoded_image.RetransmissionAllowed()) {
    expected_retransmission_time =
        rtp_streams_[simulcast_index].rtp_rtcp->ExpectedRetransmissionTime();
  }

  if (IsFirstFrameOfACodedVideoSequence(encoded_image, codec_specific_info)) {
    // In order to use the dependency descriptor RTP header extension:
    //  - Pass along any `FrameDependencyStructure` templates produced by the
    //    encoder adapter.
    //  - If none were produced the `RtpPayloadParams::*ToGeneric` for the
    //    particular codec have simulated a dependency structure, so provide a
    //    minimal set of templates.
    //  - Otherwise, don't pass along any templates at all which will disable
    //    the generation of a dependency descriptor.
    RTPSenderVideo& sender_video = *rtp_streams_[simulcast_index].sender_video;
    if (codec_specific_info && codec_specific_info->template_structure) {
      sender_video.SetVideoStructure(&*codec_specific_info->template_structure);
    } else if (std::optional<FrameDependencyStructure> structure =
                   params_[simulcast_index].GenericStructure(
                       codec_specific_info)) {
      sender_video.SetVideoStructure(&*structure);
    } else {
      sender_video.SetVideoStructure(nullptr);
    }
  }

  bool send_result =
      rtp_streams_[simulcast_index].sender_video->SendEncodedImage(
          rtp_config_.GetStreamConfig(simulcast_index).payload_type,
          PayloadStringToCodecType(
              rtp_config_.GetStreamConfig(simulcast_index).payload_name),
          rtp_timestamp, encoded_image, std::move(video_header),
          expected_retransmission_time, csrcs_);
  if (frame_count_observer_) {
    FrameCounts& counts = frame_counts_[simulcast_index];
    if (encoded_image.IsKey()) {
      ++counts.key_frames;
    } else if (encoded_image.IsDelta()) {
      ++counts.delta_frames;
    } else {
      RTC_DCHECK(encoded_image.frame_type() == VideoFrameType::kEmptyFrame);
    }
    frame_count_observer_->FrameCountUpdated(
        counts, rtp_config_.ssrcs[simulcast_index]);
  }
  if (!send_result)
    return Result(Result::ERROR_SEND_FAILED);

  return Result(Result::OK, rtp_timestamp);
}

void RtpVideoSender::OnFrameDropped(uint32_t /*rtp_timestamp*/,
                                    int /*spatial_id*/,
                                    bool /*is_end_of_temporal_unit*/) {}

void RtpVideoSender::OnVideoLayersAllocationUpdated(
    const VideoLayersAllocation& allocation) {
  MutexLock lock(&mutex_);
  if (IsActiveLocked()) {
    for (size_t i = 0; i < rtp_streams_.size(); ++i) {
      VideoLayersAllocation stream_allocation = allocation;
      stream_allocation.rtp_stream_index = i;
      rtp_streams_[i].sender_video->SetVideoLayersAllocation(
          std::move(stream_allocation));
    }

    // Only send video frames on the rtp module if the encoder is configured
    // to send. This is to prevent stray frames to be sent after an encoder
    // has been reconfigured.
    // Reconfiguration of the RtpRtcp modules must happen on the transport queue
    // to avoid races with batch sending of packets.
    std::vector<bool> sending(rtp_streams_.size(), false);
    for (const VideoLayersAllocation::SpatialLayer& layer :
         allocation.active_spatial_layers) {
      if (layer.rtp_stream_index < static_cast<int>(sending.size())) {
        sending[layer.rtp_stream_index] = true;
        // Enable media right away, so that the key frame the encoder produces
        // for a re-enabled layer isn't dropped if it's encoded before the task
        // below has run. This doesn't race with batch sending since the pacer
        // has no packets for an inactive module (DeRegisterSendingRtpStream()
        // prunes them, and RTPSender builds no RTX or padding packets while
        // media is off). Packets of new frames are enqueued in the pacer on
        // the transport queue, i.e. after the task has activated the module.
        rtp_streams_[layer.rtp_stream_index].rtp_rtcp->SetSendingMediaStatus(
            true);
      }
    }
    transport_queue_.PostTask(
        SafeTask(safety_.flag(), [this, sending = std::move(sending)] {
          RTC_DCHECK_RUN_ON(&transport_checker_);
          // It's possible for another task to be scheduled on the transport
          // checker ahead of this call that makes the sender not active.
          if (!IsActive()) {
            return;
          }
          RTC_CHECK_EQ(sending.size(), rtp_streams_.size());
          for (size_t i = 0; i < sending.size(); ++i) {
            SetModuleIsActive(sending[i], *rtp_streams_[i].rtp_rtcp);
          }
        }));
  }
}

bool RtpVideoSender::NackEnabled() const {
  const bool nack_enabled = rtp_config_.nack.rtp_history_ms > 0;
  return nack_enabled;
}

DataRate RtpVideoSender::GetPostEncodeOverhead() const {
  DataRate post_encode_overhead = DataRate::Zero();
  for (size_t i = 0; i < rtp_streams_.size(); ++i) {
    if (rtp_streams_[i].rtp_rtcp->SendingMedia()) {
      post_encode_overhead +=
          rtp_streams_[i].sender_video->PostEncodeOverhead();
    }
  }
  return post_encode_overhead;
}

void RtpVideoSender::DeliverRtcp(std::span<const uint8_t> packet) {
  // Runs on a network thread.
  for (const RtpStreamSender& stream : rtp_streams_)
    stream.rtp_rtcp->IncomingRtcpPacket(packet);
}

void RtpVideoSender::OnNetworkAvailability(bool network_available) {
  RTC_DCHECK_RUN_ON(&transport_checker_);
  for (const RtpStreamSender& stream : rtp_streams_) {
    stream.rtp_rtcp->SetRTCPStatus(network_available ? rtp_config_.rtcp_mode
                                                     : RtcpMode::kOff);
  }
}

std::map<uint32_t, RtpState> RtpVideoSender::GetRtpStates() const {
  RTC_DCHECK_RUN_ON(&transport_checker_);
  std::map<uint32_t, RtpState> rtp_states;

  for (size_t i = 0; i < rtp_config_.ssrcs.size(); ++i) {
    uint32_t ssrc = rtp_config_.ssrcs[i];
    RTC_DCHECK_EQ(ssrc, rtp_streams_[i].rtp_rtcp->SSRC());
    rtp_states[ssrc] = rtp_streams_[i].rtp_rtcp->GetRtpState();

    // Only happens during shutdown, when RTP module is already inactive,
    // so OK to call fec generator here.
    if (rtp_streams_[i].fec_generator) {
      std::optional<RtpState> fec_state =
          rtp_streams_[i].fec_generator->GetRtpState();
      if (fec_state) {
        uint32_t fec_ssrc = rtp_config_.flexfec.ssrc;
        rtp_states[fec_ssrc] = *fec_state;
      }
    }
  }

  for (size_t i = 0; i < rtp_config_.rtx.ssrcs.size(); ++i) {
    uint32_t ssrc = rtp_config_.rtx.ssrcs[i];
    rtp_states[ssrc] = rtp_streams_[i].rtp_rtcp->GetRtxState();
  }

  return rtp_states;
}

std::map<uint32_t, RtpPayloadState> RtpVideoSender::GetRtpPayloadStates()
    const {
  RTC_DCHECK_RUN_ON(&transport_checker_);
  MutexLock lock(&mutex_);
  std::map<uint32_t, RtpPayloadState> payload_states;
  for (const auto& param : params_) {
    payload_states[param.ssrc()] = param.state();
    payload_states[param.ssrc()].shared_frame_id = shared_frame_id_;
  }
  return payload_states;
}

void RtpVideoSender::OnBitrateUpdated(BitrateAllocationUpdate update,
                                      int framerate) {
  RTC_DCHECK_RUN_ON(&transport_checker_);
  // Substract overhead from bitrate.
  if (transport_overhead_bytes_per_packet_ !=
      update.packet_overhead.bytes<size_t>()) {
    transport_overhead_bytes_per_packet_ =
        update.packet_overhead.bytes<size_t>();
    size_t max_rtp_packet_size =
        std::min(rtp_config_.max_packet_size,
                 kPathMTU - transport_overhead_bytes_per_packet_);
    for (const RtpStreamSender& stream : rtp_streams_) {
      stream.rtp_rtcp->SetMaxRtpPacketSize(max_rtp_packet_size);
    }
  }

  size_t num_active_streams = 0;
  size_t overhead_bytes_per_packet = 0;
  for (const auto& stream : rtp_streams_) {
    if (stream.rtp_rtcp->SendingMedia()) {
      overhead_bytes_per_packet += stream.rtp_rtcp->ExpectedPerPacketOverhead();
      ++num_active_streams;
    }
  }
  if (num_active_streams > 1) {
    overhead_bytes_per_packet /= num_active_streams;
  }

  DataSize packet_overhead = DataSize::Bytes(
      overhead_bytes_per_packet + transport_overhead_bytes_per_packet_);
  DataSize max_total_packet_size = DataSize::Bytes(
      rtp_config_.max_packet_size + transport_overhead_bytes_per_packet_);
  uint32_t payload_bitrate_bps = update.target_bitrate.bps();
  if (has_packet_feedback_) {
    DataRate overhead_rate =
        CalculateOverheadRate(update.target_bitrate, max_total_packet_size,
                              packet_overhead, Frequency::Hertz(framerate));
    // TODO(srte): We probably should not accept 0 payload bitrate here.
    payload_bitrate_bps =
        saturated_cast<uint32_t>(payload_bitrate_bps - overhead_rate.bps());
  }

  // Get the encoder target rate. It is the estimated network rate -
  // protection overhead.
  // TODO(srte): We should multiply with 255 here.
  encoder_target_rate_bps_ = fec_controller_->UpdateFecRates(
      payload_bitrate_bps, framerate,
      saturated_cast<uint8_t>(update.packet_loss_ratio * 256),
      loss_mask_vector_, update.round_trip_time.ms());
  if (!fec_allowed_) {
    encoder_target_rate_bps_ = payload_bitrate_bps;
    // fec_controller_->UpdateFecRates() was still called so as to allow
    // `fec_controller_` to update whatever internal state it might have,
    // since `fec_allowed_` may be toggled back on at any moment.
  }

  // Subtract post encode overhead from the encoder target. If target rate
  // is really low, cap the overhead at 50%. This also avoids the case where
  // `encoder_target_rate_bps_` is 0 due to encoder pause event while the
  // packetization rate is positive since packets are still flowing.
  uint32_t post_encode_overhead_bps = std::min(
      GetPostEncodeOverhead().bps<uint32_t>(), encoder_target_rate_bps_ / 2);
  encoder_target_rate_bps_ -= post_encode_overhead_bps;

  loss_mask_vector_.clear();

  uint32_t encoder_overhead_rate_bps = 0;
  if (has_packet_feedback_) {
    // TODO(srte): The packet size should probably be the same as in the
    // CalculateOverheadRate call above (just max_total_packet_size), it doesn't
    // make sense to use different packet rates for different overhead
    // calculations.
    DataRate encoder_overhead_rate = CalculateOverheadRate(
        DataRate::BitsPerSec(encoder_target_rate_bps_),
        max_total_packet_size - DataSize::Bytes(overhead_bytes_per_packet),
        packet_overhead, Frequency::Hertz(framerate));
    encoder_overhead_rate_bps = std::min(
        encoder_overhead_rate.bps<uint32_t>(),
        update.target_bitrate.bps<uint32_t>() - encoder_target_rate_bps_);
  }
  const uint32_t media_rate = encoder_target_rate_bps_ +
                              encoder_overhead_rate_bps +
                              post_encode_overhead_bps;
  RTC_DCHECK_GE(update.target_bitrate, DataRate::BitsPerSec(media_rate));
  // `protection_bitrate_bps_` includes overhead.
  protection_bitrate_bps_ = update.target_bitrate.bps() - media_rate;
}

uint32_t RtpVideoSender::GetPayloadBitrateBps() const {
  RTC_DCHECK_RUN_ON(&transport_checker_);
  return encoder_target_rate_bps_;
}

uint32_t RtpVideoSender::GetProtectionBitrateBps() const {
  RTC_DCHECK_RUN_ON(&transport_checker_);
  return protection_bitrate_bps_;
}

std::vector<RtpSequenceNumberMap::Info> RtpVideoSender::GetSentRtpPacketInfos(
    uint32_t ssrc,
    std::span<const uint16_t> sequence_numbers) const {
  for (const auto& rtp_stream : rtp_streams_) {
    if (ssrc == rtp_stream.rtp_rtcp->SSRC()) {
      return rtp_stream.rtp_rtcp->GetSentRtpPacketInfos(sequence_numbers);
    }
  }
  return std::vector<RtpSequenceNumberMap::Info>();
}

int RtpVideoSender::ProtectionRequest(const FecProtectionParams* delta_params,
                                      const FecProtectionParams* key_params,
                                      uint32_t* sent_video_rate_bps,
                                      uint32_t* sent_nack_rate_bps,
                                      uint32_t* sent_fec_rate_bps) {
  *sent_video_rate_bps = 0;
  *sent_nack_rate_bps = 0;
  *sent_fec_rate_bps = 0;
  for (const RtpStreamSender& stream : rtp_streams_) {
    stream.rtp_rtcp->SetFecProtectionParams(*delta_params, *key_params);

    auto send_bitrate = stream.rtp_rtcp->GetSendRates();
    *sent_video_rate_bps += send_bitrate[RtpPacketMediaType::kVideo].bps();
    *sent_fec_rate_bps +=
        send_bitrate[RtpPacketMediaType::kForwardErrorCorrection].bps();
    *sent_nack_rate_bps +=
        send_bitrate[RtpPacketMediaType::kRetransmission].bps();
  }
  return 0;
}

void RtpVideoSender::SetFecAllowed(bool fec_allowed) {
  // Called by the encoder, which may run on any thread. `fec_allowed_` is only
  // used on the transport queue, so apply the value there.
  transport_queue_.PostTask(SafeTask(safety_.flag(), [this, fec_allowed] {
    RTC_DCHECK_RUN_ON(&transport_checker_);
    fec_allowed_ = fec_allowed;
  }));
}

void RtpVideoSender::OnPacketFeedbackVector(
    std::vector<StreamPacketInfo> packet_feedback_vector) {
  RTC_DCHECK_RUN_ON(&transport_checker_);
  if (fec_controller_->UseLossVectorMask()) {
    for (const StreamPacketInfo& packet : packet_feedback_vector) {
      loss_mask_vector_.push_back(!packet.received);
    }
  }

  // Map from SSRC to all acked packets for that RTP module.
  std::map<uint32_t, std::vector<uint16_t>> acked_packets_per_ssrc;
  for (const StreamPacketInfo& packet : packet_feedback_vector) {
    if (packet.received && packet.ssrc) {
      acked_packets_per_ssrc[*packet.ssrc].push_back(
          packet.rtp_sequence_number);
    }
  }

  // Map from SSRC to vector of RTP sequence numbers that are indicated as
  // lost by feedback, without being trailed by any received packets.
  std::map<uint32_t, std::vector<uint16_t>> early_loss_detected_per_ssrc;

  for (const StreamPacketInfo& packet : packet_feedback_vector) {
    // Only include new media packets, not retransmissions/padding/fec.
    if (!packet.received && packet.ssrc && !packet.is_retransmission) {
      // Last known lost packet, might not be detectable as lost by remote
      // jitter buffer.
      early_loss_detected_per_ssrc[*packet.ssrc].push_back(
          packet.rtp_sequence_number);
    } else {
      // Packet received, so any loss prior to this is already detectable.
      early_loss_detected_per_ssrc.erase(*packet.ssrc);
    }
  }

  for (const auto& kv : early_loss_detected_per_ssrc) {
    const uint32_t ssrc = kv.first;
    auto it = ssrc_to_rtp_module_.find(ssrc);
    RTC_CHECK(it != ssrc_to_rtp_module_.end());
    RTPSender* rtp_sender = it->second->RtpSender();
    for (uint16_t sequence_number : kv.second) {
      rtp_sender->ReSendPacket(sequence_number);
    }
  }

  for (const auto& kv : acked_packets_per_ssrc) {
    const uint32_t ssrc = kv.first;
    auto it = ssrc_to_rtp_module_.find(ssrc);
    if (it == ssrc_to_rtp_module_.end()) {
      // No media, likely FEC or padding. Ignore since there's no RTP history to
      // clean up anyway.
      continue;
    }
    std::span<const uint16_t> rtp_sequence_numbers(kv.second);
    it->second->OnPacketsAcknowledged(rtp_sequence_numbers);
  }
}

void RtpVideoSender::SetEncodingData(size_t width,
                                     size_t height,
                                     size_t num_temporal_layers) {
  RTC_DCHECK_RUN_ON(&transport_checker_);
  fec_controller_->SetEncodingData(width, height, num_temporal_layers,
                                   rtp_config_.max_packet_size);
}

void RtpVideoSender::SetCsrcs(std::span<const uint32_t> csrcs) {
  RTC_DCHECK_RUN_ON(&transport_checker_);
  MutexLock lock(&mutex_);
  csrcs_ = LimitCsrcs(csrcs);
}

DataRate RtpVideoSender::CalculateOverheadRate(DataRate data_rate,
                                               DataSize packet_size,
                                               DataSize overhead_per_packet,
                                               Frequency framerate) const {
  Frequency packet_rate = data_rate / packet_size;
  if (use_frame_rate_for_overhead_) {
    framerate = std::max(framerate, Frequency::Hertz(1));
    DataSize frame_size = data_rate / framerate;
    int packets_per_frame = ceil(frame_size / packet_size);
    packet_rate = packets_per_frame * framerate;
  }
  return packet_rate.RoundUpTo(Frequency::Hertz(1)) * overhead_per_packet;
}

}  // namespace webrtc
