/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "video/video_send_stream_impl.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "api/adaptation/resource.h"
#include "api/call/bitrate_allocation.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/fec_controller.h"
#include "api/field_trials_view.h"
#include "api/metronome/metronome.h"
#include "api/rtp_parameters.h"
#include "api/rtp_sender_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "api/video/encoded_image.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_layers_allocation.h"
#include "api/video/video_source_interface.h"
#include "api/video/video_stream_encoder_settings.h"
#include "api/video_codecs/spatial_layer.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "call/bitrate_allocator.h"
#include "call/rtp_config.h"
#include "call/rtp_transport_controller_send_interface.h"
#include "call/video_send_stream.h"
#include "media/base/sdp_video_format_utils.h"
#include "modules/pacing/pacing_controller.h"
#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_header_extension_size.h"
#include "modules/rtp_rtcp/source/rtp_sender.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "rtc_base/checks.h"
#include "rtc_base/experiments/alr_experiment.h"
#include "rtc_base/experiments/field_trial_parser.h"
#include "rtc_base/experiments/min_video_bitrate_experiment.h"
#include "rtc_base/experiments/rate_control_settings.h"
#include "rtc_base/experiments/rfc8888_congestion_control_feedback_experiment.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "rtc_base/task_utils/repeating_task.h"
#include "rtc_base/trace_event.h"
#include "video/adaptation/overuse_frame_detector.h"
#include "video/config/video_encoder_config.h"
#include "video/encoder_rtcp_feedback.h"
#include "video/frame_cadence_adapter.h"
#include "video/send_delay_stats.h"
#include "video/send_statistics_proxy.h"
#include "video/video_stream_encoder.h"
#include "video/video_stream_encoder_interface.h"

namespace webrtc {
namespace internal {
namespace {

constexpr TimeDelta kEncoderTimeOut = TimeDelta::Seconds(2);

constexpr double kVideoHysteresis = 1.2;
constexpr double kScreenshareHysteresis = 1.35;

constexpr int kMinDefaultAv1BitrateBps =
    15000;  // This value acts as an absolute minimum AV1 bitrate limit.

// When send-side BWE is used a stricter 1.1x pacing factor is used, rather than
// the 2.5x which is used with receive-side BWE. Provides a more careful
// bandwidth rampup with less risk of overshoots causing adverse effects like
// packet loss. Not used for receive side BWE, since there we lack the probing
// feature and so may result in too slow initial rampup.
constexpr double kStrictPacingMultiplier = 1.1;

bool TransportSeqNumExtensionConfigured(const VideoSendStream::Config& config) {
  const std::vector<RtpExtension>& extensions = config.rtp.extensions;
  return absl::c_any_of(extensions, [](const RtpExtension& ext) {
    return ext.uri == RtpExtension::kTransportSequenceNumberUri;
  });
}

// Calculate max padding bitrate for a multi layer codec.
int CalculateMaxPadBitrateBps(const std::vector<VideoStream>& streams,
                              bool is_svc,
                              VideoEncoderConfig::ContentType content_type,
                              int min_transmit_bitrate_bps,
                              bool pad_to_min_bitrate,
                              bool alr_probing) {
  int pad_up_to_bitrate_bps = 0;

  RTC_DCHECK(!is_svc || streams.size() <= 1) << "Only one stream is allowed in "
                                                "SVC mode.";

  // Filter out only the active streams;
  std::vector<VideoStream> active_streams;
  for (const VideoStream& stream : streams) {
    if (stream.active)
      active_streams.emplace_back(stream);
  }

  if (active_streams.size() > 1 || (!active_streams.empty() && is_svc)) {
    // Simulcast or SVC is used.
    // if SVC is used, stream bitrates should already encode svc bitrates:
    // min_bitrate = min bitrate of a lowest svc layer.
    // target_bitrate = sum of target bitrates of lower layers + min bitrate
    // of the last one (as used in the calculations below).
    // max_bitrate = sum of all active layers' max_bitrate.
    if (alr_probing) {
      // With alr probing, just pad to the min bitrate of the lowest stream,
      // probing will handle the rest of the rampup.
      pad_up_to_bitrate_bps = active_streams[0].min_bitrate_bps;
    } else {
      // Without alr probing, pad up to start bitrate of the
      // highest active stream.
      const double hysteresis_factor =
          content_type == VideoEncoderConfig::ContentType::kScreen
              ? kScreenshareHysteresis
              : kVideoHysteresis;
      if (is_svc) {
        // For SVC, since there is only one "stream", the padding bitrate
        // needed to enable the top spatial layer is stored in the
        // `target_bitrate_bps` field.
        // TODO(sprang): This behavior needs to die.
        pad_up_to_bitrate_bps = static_cast<int>(
            hysteresis_factor * active_streams[0].target_bitrate_bps + 0.5);
      } else {
        const size_t top_active_stream_idx = active_streams.size() - 1;
        pad_up_to_bitrate_bps = std::min(
            static_cast<int>(
                hysteresis_factor *
                    active_streams[top_active_stream_idx].min_bitrate_bps +
                0.5),
            active_streams[top_active_stream_idx].target_bitrate_bps);

        // Add target_bitrate_bps of the lower active streams.
        for (size_t i = 0; i < top_active_stream_idx; ++i) {
          pad_up_to_bitrate_bps += active_streams[i].target_bitrate_bps;
        }
      }
    }
  } else if (!active_streams.empty() && pad_to_min_bitrate) {
    pad_up_to_bitrate_bps = active_streams[0].min_bitrate_bps;
  }

  pad_up_to_bitrate_bps =
      std::max(pad_up_to_bitrate_bps, min_transmit_bitrate_bps);

  return pad_up_to_bitrate_bps;
}

std::optional<AlrExperimentSettings> GetAlrSettings(
    const FieldTrialsView& field_trials,
    VideoEncoderConfig::ContentType content_type) {
  if (content_type == VideoEncoderConfig::ContentType::kScreen) {
    return AlrExperimentSettings::CreateFromFieldTrial(
        field_trials,
        AlrExperimentSettings::kScreenshareProbingBweExperimentName);
  }
  return AlrExperimentSettings::CreateFromFieldTrial(
      field_trials,
      AlrExperimentSettings::kStrictPacingAndProbingExperimentName);
}

// Calculates the maximum bitrate across a set of layers (spatial or simulcast).
// If there are active layers but they lack individual bitrate caps, falls back
// to the provided global maximum bitrate. Returns std::nullopt if there are no
// active layers, indicating that no bitrate should be allocated.
template <typename T, typename ActiveGetter, typename MaxBitrateGetter>
std::optional<DataRate> CalculateMaxBitrate(const std::vector<T>& layers,
                                            ActiveGetter active_getter,
                                            MaxBitrateGetter max_bitrate_getter,
                                            DataRate global_max_bitrate) {
  bool has_active_layer = false;
  std::optional<DataRate> max_bitrate_sum;
  for (const auto& layer : layers) {
    if (active_getter(layer)) {
      has_active_layer = true;
      DataRate layer_max = max_bitrate_getter(layer);
      if (layer_max > DataRate::Zero()) {
        max_bitrate_sum =
            max_bitrate_sum.value_or(DataRate::Zero()) + layer_max;
      }
    }
  }
  if (has_active_layer) {
    if (!max_bitrate_sum.has_value() && !global_max_bitrate.IsZero()) {
      return global_max_bitrate;
    }
    return max_bitrate_sum;
  }
  return std::nullopt;
}

// Returns an optional that has value iff TransportSeqNumExtensionConfigured
// is `true` for the given video send stream config.
std::optional<float> GetConfiguredPacingFactor(
    const VideoSendStream::Config& config,
    VideoEncoderConfig::ContentType content_type,
    const PacingConfig& default_pacing_config,
    const FieldTrialsView& field_trials) {
  if (!TransportSeqNumExtensionConfigured(config))
    return std::nullopt;

  std::optional<AlrExperimentSettings> alr_settings =
      GetAlrSettings(field_trials, content_type);
  if (alr_settings)
    return alr_settings->pacing_factor;

  return RateControlSettings(field_trials)
      .GetPacingFactor()
      .value_or(default_pacing_config.pacing_factor);
}

DataRate GetDefaultMinVideoBitrate(VideoCodecType codec_type) {
  if (codec_type == VideoCodecType::kVideoCodecAV1) {
    return DataRate::BitsPerSec(kMinDefaultAv1BitrateBps);
  }
  return DataRate::BitsPerSec(kDefaultMinVideoBitrateBps);
}

std::optional<DataRate> GetConfiguredMaxVideoBitrate(
    const VideoEncoderConfig& config) {
  // If spatial layers are configured, the sum of their bitrate takes precedence
  // over both simulcast and the global max_bitrate_bps.
  if (!config.spatial_layers.empty()) {
    return CalculateMaxBitrate(
        config.spatial_layers, [](const SpatialLayer& l) { return l.active; },
        [](const SpatialLayer& l) {
          return l.maxBitrate > 0 ? DataRate::KilobitsPerSec(l.maxBitrate)
                                  : DataRate::Zero();
        },
        DataRate::BitsPerSec(std::max(config.max_bitrate_bps, 0)));
  }

  // If simulcast layers are configured, the sum of their bitrate takes
  // precedence over the global max_bitrate_bps.
  if (!config.simulcast_layers.empty()) {
    return CalculateMaxBitrate(
        config.simulcast_layers, [](const VideoStream& s) { return s.active; },
        [](const VideoStream& s) {
          return s.max_bitrate_bps > 0 ? DataRate::BitsPerSec(s.max_bitrate_bps)
                                       : DataRate::Zero();
        },
        DataRate::BitsPerSec(std::max(config.max_bitrate_bps, 0)));
  }

  // If single stream, use the global max_bitrate_bps.
  if (config.max_bitrate_bps > 0) {
    return DataRate::BitsPerSec(config.max_bitrate_bps);
  }

  return std::nullopt;
}

size_t CalculateMaxHeaderSize(const RtpConfig& config) {
  size_t header_size = kRtpHeaderSize;
  size_t extensions_size = 0;
  size_t fec_extensions_size = 0;
  if (!config.extensions.empty()) {
    RtpHeaderExtensionMap extensions_map(config.extensions);
    extensions_size = RtpHeaderExtensionSize(RTPSender::VideoExtensionSizes(),
                                             extensions_map);
    fec_extensions_size =
        RtpHeaderExtensionSize(RTPSender::FecExtensionSizes(), extensions_map);
  }
  header_size += extensions_size;
  if (config.flexfec.payload_type >= 0) {
    // All FEC extensions again plus maximum FlexFec overhead.
    header_size += fec_extensions_size + 32;
  } else {
    if (config.ulpfec.ulpfec_payload_type >= 0) {
      // Header with all the FEC extensions will be repeated plus maximum
      // UlpFec overhead.
      header_size += fec_extensions_size + 18;
    }
    if (config.ulpfec.red_payload_type >= 0) {
      header_size += 1;  // RED header.
    }
  }
  // Additional room for Rtx.
  if (config.rtx.payload_type >= 0)
    header_size += kRtxHeaderSize;
  return header_size;
}

VideoStreamEncoder::BitrateAllocationCallbackType
GetBitrateAllocationCallbackType(const VideoSendStream::Config& config,
                                 const FieldTrialsView& field_trials) {
  if (RtpExtension::FindHeaderExtensionByUri(
          config.rtp.extensions, RtpExtension::kVideoLayersAllocationUri,
          config.crypto_options.srtp.enable_encrypted_rtp_header_extensions
              ? RtpExtension::Filter::kPreferEncryptedExtension
              : RtpExtension::Filter::kDiscardEncryptedExtension)) {
    return VideoStreamEncoder::BitrateAllocationCallbackType::
        kVideoLayersAllocation;
  }
  return VideoStreamEncoder::BitrateAllocationCallbackType::kNone;
}

RtpSenderFrameEncryptionConfig CreateFrameEncryptionConfig(
    const VideoSendStream::Config* config) {
  RtpSenderFrameEncryptionConfig frame_encryption_config;
  frame_encryption_config.frame_encryptor = config->frame_encryptor.get();
  frame_encryption_config.crypto_options = config->crypto_options;
  return frame_encryption_config;
}

RtpSenderObservers CreateObservers(RtcpRttStats* call_stats,
                                   EncoderRtcpFeedback* encoder_feedback,
                                   SendStatisticsProxy* stats_proxy,
                                   SendPacketObserver* send_packet_observer) {
  RtpSenderObservers observers;
  observers.rtcp_rtt_stats = call_stats;
  observers.intra_frame_callback = encoder_feedback;
  observers.rtcp_loss_notification_observer = encoder_feedback;
  observers.report_block_data_observer = stats_proxy;
  observers.rtp_stats = stats_proxy;
  observers.bitrate_observer = stats_proxy;
  observers.frame_count_observer = stats_proxy;
  observers.rtcp_type_observer = stats_proxy;
  observers.send_packet_observer = send_packet_observer;
  return observers;
}

std::unique_ptr<VideoStreamEncoderInterface> CreateVideoStreamEncoder(
    const Environment& env,
    int num_cpu_cores,
    SendStatisticsProxy* stats_proxy,
    VideoStreamEncoderSettings encoder_settings,
    VideoStreamEncoder::BitrateAllocationCallbackType
        bitrate_allocation_callback_type,
    Metronome* metronome,
    scoped_refptr<VideoEncoderFactory::EncoderSelectorInterface>
        encoder_selector,
    EncoderSwitchRequestCallback encoder_switch_request_callback) {
  std::unique_ptr<TaskQueueBase, TaskQueueDeleter> encoder_queue =
      env.task_queue_factory().CreateTaskQueue(
          "VideoEncoderQueue",
          !env.field_trials().IsDisabled("WebRTC-MediaTaskQueuePriorities")
              ? TaskQueueFactory::Priority::kVideo
              : TaskQueueFactory::Priority::kNormal);
  TaskQueueBase* encoder_queue_ptr = encoder_queue.get();
  return std::make_unique<VideoStreamEncoder>(
      env, num_cpu_cores, stats_proxy, std::move(encoder_settings),
      std::make_unique<OveruseFrameDetector>(env, stats_proxy),
      FrameCadenceAdapterInterface::Create(
          &env.clock(), encoder_queue_ptr, metronome,
          /*worker_queue=*/TaskQueueBase::Current(), env.field_trials()),
      std::move(encoder_queue), bitrate_allocation_callback_type,
      std::move(encoder_selector), std::move(encoder_switch_request_callback));
}

bool HasActiveEncodings(const VideoEncoderConfig& config) {
  if (!config.spatial_layers.empty()) {
    for (const auto& layer : config.spatial_layers) {
      if (layer.active) {
        return true;
      }
    }
    return false;
  }
  for (const VideoStream& stream : config.simulcast_layers) {
    if (stream.active) {
      return true;
    }
  }
  return false;
}

}  // namespace

PacingConfig::PacingConfig(const FieldTrialsView& field_trials)
    : pacing_factor("factor", kStrictPacingMultiplier),
      max_pacing_delay("max_delay", PacingController::kMaxExpectedQueueLength) {
  ParseFieldTrial({&pacing_factor, &max_pacing_delay},
                  field_trials.Lookup("WebRTC-Video-Pacing"));
}
PacingConfig::PacingConfig(const PacingConfig&) = default;
PacingConfig::~PacingConfig() = default;

VideoSendStreamImpl::VideoSendStreamImpl(
    const Environment& env,
    int num_cpu_cores,
    RtcpRttStats* call_stats,
    RtpTransportControllerSendInterface* transport,
    Metronome* metronome,
    BitrateAllocatorInterface* bitrate_allocator,
    SendDelayStats* send_delay_stats,
    VideoSendStream::Config config,
    VideoEncoderConfig encoder_config,
    const std::map<uint32_t, RtpState>& suspended_ssrcs,
    const std::map<uint32_t, RtpPayloadState>& suspended_payload_states,
    std::unique_ptr<FecController> fec_controller,
    EncoderSwitchRequestCallback encoder_switch_request_callback,
    std::unique_ptr<VideoStreamEncoderInterface> video_stream_encoder_for_test)
    : env_(env),
      transport_(transport),
      stats_proxy_(env_, config, encoder_config.content_type),
      send_packet_observer_(&stats_proxy_, send_delay_stats),
      config_(std::move(config)),
      content_type_(encoder_config.content_type),
      video_stream_encoder_(
          video_stream_encoder_for_test
              ? std::move(video_stream_encoder_for_test)
              : CreateVideoStreamEncoder(
                    env_,
                    num_cpu_cores,
                    &stats_proxy_,
                    std::move(config_.encoder_settings),
                    GetBitrateAllocationCallbackType(config_,
                                                     env_.field_trials()),
                    metronome,
                    config_.encoder_selector,
                    std::move(encoder_switch_request_callback))),
      encoder_feedback_(
          env_,
          SupportsPerLayerPictureLossIndication(
              encoder_config.video_format.parameters) ||
              env_.field_trials().IsEnabled("WebRTC-Video-PerSsrcKeyframes"),
          config_.rtp.ssrcs,
          video_stream_encoder_.get(),
          [this](uint32_t ssrc, const std::vector<uint16_t>& seq_nums) {
            return rtp_video_sender_->GetSentRtpPacketInfos(ssrc, seq_nums);
          }),
      rtp_video_sender_(transport->CreateRtpVideoSender(
          suspended_ssrcs,
          suspended_payload_states,
          config_.rtp,
          config_.rtcp_report_interval_ms,
          config_.send_transport,
          CreateObservers(call_stats,
                          &encoder_feedback_,
                          &stats_proxy_,
                          &send_packet_observer_),
          std::move(fec_controller),
          CreateFrameEncryptionConfig(&config_),
          config_.frame_transformer)),
      has_alr_probing_(
          config_.periodic_alr_bandwidth_probing ||
          GetAlrSettings(env_.field_trials(), encoder_config.content_type)),
      pacing_config_(PacingConfig(env_.field_trials())),
      worker_queue_(TaskQueueBase::Current()),
      timed_out_(false),
      bitrate_allocator_(bitrate_allocator),
      has_active_encodings_(HasActiveEncodings(encoder_config)),
      disable_padding_(true),
      max_padding_bitrate_(0),
      encoder_min_bitrate_(DataRate::Zero()),
      encoder_max_bitrate_(GetConfiguredMaxVideoBitrate(encoder_config)),
      configured_max_bitrate_(
          DataRate::BitsPerSec(std::max(0, encoder_config.max_bitrate_bps))),
      encoder_target_rate_(DataRate::Zero()),
      encoder_bitrate_priority_(encoder_config.bitrate_priority) {
  RTC_DCHECK_GE(config_.rtp.payload_type, 0);
  RTC_DCHECK_LE(config_.rtp.payload_type, 127);
  RTC_DCHECK(!config_.rtp.ssrcs.empty());
  RTC_DCHECK(transport_);
  RTC_LOG(LS_INFO) << "VideoSendStreamImpl: " << config_.ToString();

  RTC_CHECK(
      AlrExperimentSettings::MaxOneFieldTrialEnabled(env_.field_trials()));

  std::optional<bool> enable_alr_bw_probing;

  bool rfc8888_experiment_enabled =
      IsRfc8888CongestionControlFeedbackEnabled(env_.field_trials());

  // If send-side BWE, or RFC8888 congestion control feedback experiment is
  // enabled, check if we should apply updated probing and pacing settings.
  std::optional<float> pacing_factor_override = GetConfiguredPacingFactor(
      config_, content_type_, pacing_config_, env_.field_trials());
  if (pacing_factor_override.has_value() || rfc8888_experiment_enabled) {
    std::optional<AlrExperimentSettings> alr_settings =
        GetAlrSettings(env_.field_trials(), content_type_);
    int queue_time_limit_ms;
    if (alr_settings) {
      enable_alr_bw_probing = true;
      queue_time_limit_ms = alr_settings->max_paced_queue_time;
    } else {
      enable_alr_bw_probing =
          RateControlSettings(env_.field_trials()).UseAlrProbing();
      queue_time_limit_ms = pacing_config_.max_pacing_delay.Get().ms();
    }

    transport_->SetQueueTimeLimit(queue_time_limit_ms);
    if (!rfc8888_experiment_enabled) {
      // In the RFC8888 experiment, the pacing factor is decided exclusively in
      // the congestion controller, and SetPacingFactor is not allowed.
      transport_->SetPacingFactor(*pacing_factor_override);
    }
  }

  if (config_.periodic_alr_bandwidth_probing) {
    enable_alr_bw_probing = config_.periodic_alr_bandwidth_probing;
  }

  if (enable_alr_bw_probing) {
    transport->EnablePeriodicAlrProbing(*enable_alr_bw_probing);
  }

  // Only request rotation at the source when we positively know that the
  // remote side doesn't support the rotation extension. This allows us to
  // prepare the encoder in the expectation that rotation is supported - which
  // is the common case.
  bool rotation_applied = absl::c_none_of(
      config_.rtp.extensions, [](const RtpExtension& extension) {
        return extension.uri == RtpExtension::kVideoRotationUri;
      });

  video_stream_encoder_->SetSink(this, rotation_applied);
  video_stream_encoder_->SetStartBitrate(
      bitrate_allocator_->GetStartBitrate(this));
  video_stream_encoder_->SetFecControllerOverride(rtp_video_sender_);
  ReconfigureVideoEncoder(std::move(encoder_config));
}

VideoSendStreamImpl::~VideoSendStreamImpl() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_LOG(LS_INFO) << "~VideoSendStreamImpl: " << config_.ToString();
  RTC_DCHECK(!started());
  RTC_DCHECK(!IsRunning());
  transport_->DestroyRtpVideoSender(rtp_video_sender_);
}

void VideoSendStreamImpl::AddAdaptationResource(
    scoped_refptr<Resource> resource) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  adaptation_resources_.push_back(resource);
  video_stream_encoder_->AddAdaptationResource(std::move(resource));
}

std::vector<scoped_refptr<Resource>>
VideoSendStreamImpl::GetAdaptationResources() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return adaptation_resources_;
}

void VideoSendStreamImpl::SetSource(
    VideoSourceInterface<VideoFrame>* source,
    const DegradationPreference& degradation_preference) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  video_stream_encoder_->SetSource(source, degradation_preference);
}

void VideoSendStreamImpl::ReconfigureVideoEncoder(VideoEncoderConfig config) {
  ReconfigureVideoEncoder(std::move(config), nullptr);
}

void VideoSendStreamImpl::ReconfigureVideoEncoder(
    VideoEncoderConfig config,
    SetParametersCallback callback) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK_EQ(content_type_, config.content_type);
  RTC_LOG(LS_INFO) << "Encoder config: " << config.ToString()
                   << " VideoSendStream config: " << config_.ToString();

  has_active_encodings_ = HasActiveEncodings(config);
  configured_max_bitrate_ =
      DataRate::BitsPerSec(std::max(0, config.max_bitrate_bps));
  if (has_active_encodings_ && rtp_video_sender_->IsActive() && !IsRunning()) {
    StartupVideoSendStream();
  } else if (!has_active_encodings_ && IsRunning()) {
    StopVideoSendStream();
  }
  video_stream_encoder_->ConfigureEncoder(
      std::move(config),
      config_.rtp.max_packet_size - CalculateMaxHeaderSize(config_.rtp),
      std::move(callback));
}

VideoSendStream::Stats VideoSendStreamImpl::GetStats() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return stats_proxy_.GetStats();
}

void VideoSendStreamImpl::SetStats(const Stats& stats) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  stats_proxy_.SetStats(stats);
}

void VideoSendStreamImpl::SetCsrcs(std::span<const uint32_t> csrcs) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  rtp_video_sender_->SetCsrcs(csrcs);
}
void VideoSendStreamImpl::StopPermanentlyAndGetRtpStates(
    VideoSendStreamImpl::RtpStateMap* rtp_state_map,
    VideoSendStreamImpl::RtpPayloadStateMap* payload_state_map) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  video_stream_encoder_->Stop();

  running_ = false;
  // Always run these cleanup steps regardless of whether running_ was set
  // or not. This will unregister callbacks before destruction.
  // See `VideoSendStreamImpl::StopVideoSendStream` for more.
  Stop();
  *rtp_state_map = GetRtpStates();
  *payload_state_map = GetRtpPayloadStates();
}

void VideoSendStreamImpl::GenerateKeyFrame(
    const std::vector<std::string>& rids) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (!video_stream_encoder_) {
    return;
  }

  // Map rids to layers. If rids is empty, generate a keyframe for all layers.
  std::vector<VideoFrameType> next_frames(config_.rtp.ssrcs.size(),
                                          VideoFrameType::kVideoFrameKey);
  if (!config_.rtp.rids.empty() && !rids.empty()) {
    std::fill(next_frames.begin(), next_frames.end(),
              VideoFrameType::kVideoFrameDelta);
    for (const auto& rid : rids) {
      for (size_t i = 0; i < config_.rtp.rids.size(); i++) {
        if (config_.rtp.rids[i] == rid) {
          if (i < next_frames.size()) {
            next_frames[i] = VideoFrameType::kVideoFrameKey;
          }
          break;
        }
      }
    }
  }

  video_stream_encoder_->SendKeyFrame(next_frames);
}

void VideoSendStreamImpl::DeliverRtcp(std::span<const uint8_t> packet) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  rtp_video_sender_->DeliverRtcp(packet);
}

bool VideoSendStreamImpl::started() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return rtp_video_sender_->IsActive();
}

void VideoSendStreamImpl::Start() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  // This sender is allowed to send RTP packets. Start monitoring and allocating
  // a rate if there is also active encodings. (has_active_encodings_).
  rtp_video_sender_->SetSending(true);
  if (!IsRunning() && has_active_encodings_) {
    StartupVideoSendStream();
  }
}

bool VideoSendStreamImpl::IsRunning() const {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  return check_encoder_activity_task_.Running();
}

void VideoSendStreamImpl::StartupVideoSendStream() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(rtp_video_sender_->IsActive());
  RTC_DCHECK(has_active_encodings_);

  bitrate_allocator_->AddObserver(this, GetAllocationConfig());
  // Start monitoring encoder activity.
  {
    RTC_DCHECK(!check_encoder_activity_task_.Running());

    activity_ = false;
    timed_out_ = false;
    check_encoder_activity_task_ = RepeatingTaskHandle::DelayedStart(
        worker_queue_, kEncoderTimeOut, [this] {
          RTC_DCHECK_RUN_ON(&thread_checker_);
          if (!activity_) {
            if (!timed_out_) {
              SignalEncoderTimedOut();
            }
            timed_out_ = true;
            disable_padding_ = true;
          } else if (timed_out_) {
            SignalEncoderActive();
            timed_out_ = false;
          }
          activity_ = false;
          return kEncoderTimeOut;
        });
  }

  video_stream_encoder_->SendKeyFrame();
}

void VideoSendStreamImpl::Stop() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_LOG(LS_INFO) << "VideoSendStreamImpl::Stop";
  if (!rtp_video_sender_->IsActive())
    return;

  TRACE_EVENT_INSTANT0("webrtc", "VideoSendStream::Stop",
                       TRACE_EVENT_SCOPE_GLOBAL);
  rtp_video_sender_->SetSending(false);
  if (IsRunning()) {
    StopVideoSendStream();
  }
}

void VideoSendStreamImpl::StopVideoSendStream() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  bitrate_allocator_->RemoveObserver(this);
  check_encoder_activity_task_.Stop();
  video_stream_encoder_->OnBitrateUpdated(DataRate::Zero(), DataRate::Zero(), 0,
                                          0, 0);
  stats_proxy_.OnSetEncoderTargetRate(0);
}

void VideoSendStreamImpl::SignalEncoderTimedOut() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  // If the encoder has not produced anything the last kEncoderTimeOut and it
  // is supposed to, deregister as BitrateAllocatorObserver. This can happen
  // if a camera stops producing frames.
  if (!encoder_target_rate_.IsZero()) {
    RTC_LOG(LS_INFO) << "SignalEncoderTimedOut, Encoder timed out.";
    bitrate_allocator_->RemoveObserver(this);
  }
}

void VideoSendStreamImpl::OnVideoLayersAllocationUpdated(
    VideoLayersAllocation allocation) {
  // OnVideoLayersAllocationUpdated is handled on the encoder task queue in
  // order to not race with OnEncodedImage callbacks.
  rtp_video_sender_->OnVideoLayersAllocationUpdated(allocation);
}

void VideoSendStreamImpl::SignalEncoderActive() {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  if (IsRunning()) {
    RTC_LOG(LS_INFO) << "SignalEncoderActive, Encoder is active.";
    bitrate_allocator_->AddObserver(this, GetAllocationConfig());
  }
}

MediaStreamAllocationConfig VideoSendStreamImpl::GetAllocationConfig() const {
  return MediaStreamAllocationConfig{
      .min_bitrate_bps = encoder_min_bitrate_.bps<uint32_t>(),
      .max_bitrate_bps =
          encoder_max_bitrate_.value_or(DataRate::Zero()).bps<uint32_t>(),
      .pad_up_bitrate_bps =
          static_cast<uint32_t>(disable_padding_ ? 0 : max_padding_bitrate_),
      .priority_bitrate_bps = 0,
      .enforce_min_bitrate = !config_.suspend_below_min_bitrate,
      .bitrate_priority = encoder_bitrate_priority_,
      .rate_elasticity =
          (content_type_ == VideoEncoderConfig::ContentType::kRealtimeVideo)
              ? std::optional(TrackRateElasticity::kCanConsumeExtraRate)
              : std::nullopt};
}

void VideoSendStreamImpl::OnEncoderConfigurationChanged(
    std::vector<VideoStream> streams,
    bool is_svc,
    VideoEncoderConfig::ContentType content_type,
    int min_transmit_bitrate_bps) {
  // Currently called on the encoder TQ
  RTC_DCHECK(!worker_queue_->IsCurrent());
  auto closure = [this, streams = std::move(streams), is_svc, content_type,
                  min_transmit_bitrate_bps]() mutable {
    RTC_DCHECK_GE(config_.rtp.ssrcs.size(), streams.size());
    TRACE_EVENT0("webrtc", "VideoSendStream::OnEncoderConfigurationChanged");
    RTC_DCHECK_RUN_ON(&thread_checker_);

    const VideoCodecType codec_type =
        PayloadStringToCodecType(config_.rtp.payload_name);

    const std::optional<DataRate> experimental_min_bitrate =
        GetExperimentalMinVideoBitrate(env_.field_trials(), codec_type);
    if (experimental_min_bitrate.has_value()) {
      encoder_min_bitrate_ = *experimental_min_bitrate;
    } else {
      DataRate min_bitrate =
          streams[0].min_bitrate_bps > 0
              ? DataRate::BitsPerSec(streams[0].min_bitrate_bps)
              : DataRate::Zero();
      encoder_min_bitrate_ =
          std::max(min_bitrate, GetDefaultMinVideoBitrate(codec_type));
    }
    double stream_bitrate_priority_sum = 0;
    for (const auto& stream : streams) {
      if (stream.bitrate_priority) {
        RTC_DCHECK_GT(*stream.bitrate_priority, 0);
        stream_bitrate_priority_sum += *stream.bitrate_priority;
      }
    }
    RTC_DCHECK_GT(stream_bitrate_priority_sum, 0);
    encoder_bitrate_priority_ = stream_bitrate_priority_sum;

    // Aggregate maximum bitrate from all active streams. Active streams missing
    // individual caps will fall back to the global maximum configured bitrate
    // cap.
    encoder_max_bitrate_ = CalculateMaxBitrate(
        streams, [](const VideoStream& s) { return s.active; },
        [](const VideoStream& s) {
          return s.max_bitrate_bps > 0 ? DataRate::BitsPerSec(s.max_bitrate_bps)
                                       : DataRate::Zero();
        },
        configured_max_bitrate_);

    if (encoder_max_bitrate_.has_value()) {
      encoder_max_bitrate_ =
          std::max(encoder_min_bitrate_, *encoder_max_bitrate_);
    }

    // TODO(bugs.webrtc.org/10266): Query the VideoBitrateAllocator instead.
    max_padding_bitrate_ = CalculateMaxPadBitrateBps(
        streams, is_svc, content_type, min_transmit_bitrate_bps,
        config_.suspend_below_min_bitrate, has_alr_probing_);

    // Clear stats for disabled layers.
    for (size_t i = streams.size(); i < config_.rtp.ssrcs.size(); ++i) {
      stats_proxy_.OnInactiveSsrc(config_.rtp.ssrcs[i]);
    }

    const size_t num_temporal_layers =
        streams.back().num_temporal_layers.value_or(1);

    rtp_video_sender_->SetEncodingData(streams[0].width, streams[0].height,
                                       num_temporal_layers);

    if (IsRunning()) {
      // The send stream is started already. Update the allocator with new
      // bitrate limits.
      bitrate_allocator_->AddObserver(this, GetAllocationConfig());
    }
  };

  worker_queue_->PostTask(
      SafeTask(worker_queue_safety_.flag(), std::move(closure)));
}

EncodedImageCallback::Result VideoSendStreamImpl::OnEncodedImage(
    const EncodedImage& encoded_image,
    const CodecSpecificInfo* codec_specific_info) {
  // Encoded is called on whatever thread the real encoder implementation run
  // on. In the case of hardware encoders, there might be several encoders
  // running in parallel on different threads.

  // Indicate that there still is activity going on.
  activity_ = true;
  RTC_DCHECK(!worker_queue_->IsCurrent());

  auto task_to_run_on_worker = [this]() {
    RTC_DCHECK_RUN_ON(&thread_checker_);
    if (disable_padding_) {
      disable_padding_ = false;
      // To ensure that padding bitrate is propagated to the bitrate allocator.
      SignalEncoderActive();
    }
  };
  worker_queue_->PostTask(
      SafeTask(worker_queue_safety_.flag(), std::move(task_to_run_on_worker)));

  return rtp_video_sender_->OnEncodedImage(encoded_image, codec_specific_info);
}

void VideoSendStreamImpl::OnFrameDropped(uint32_t rtp_timestamp,
                                         int spatial_id,
                                         bool is_end_of_temporal_unit) {
  activity_ = true;
}

std::map<uint32_t, RtpState> VideoSendStreamImpl::GetRtpStates() const {
  return rtp_video_sender_->GetRtpStates();
}

std::map<uint32_t, RtpPayloadState> VideoSendStreamImpl::GetRtpPayloadStates()
    const {
  return rtp_video_sender_->GetRtpPayloadStates();
}

uint32_t VideoSendStreamImpl::OnBitrateUpdated(BitrateAllocationUpdate update) {
  RTC_DCHECK_RUN_ON(&thread_checker_);
  RTC_DCHECK(rtp_video_sender_->IsActive())
      << "VideoSendStream::Start has not been called.";

  rtp_video_sender_->OnBitrateUpdated(update, stats_proxy_.GetSendFrameRate());
  encoder_target_rate_ =
      DataRate::BitsPerSec(rtp_video_sender_->GetPayloadBitrateBps());
  const DataRate protection_bitrate =
      DataRate::BitsPerSec(rtp_video_sender_->GetProtectionBitrateBps());
  DataRate link_allocation = DataRate::Zero();
  if (encoder_target_rate_ > protection_bitrate) {
    link_allocation = encoder_target_rate_ - protection_bitrate;
  }
  if (encoder_max_bitrate_.has_value()) {
    encoder_target_rate_ =
        std::min(*encoder_max_bitrate_, encoder_target_rate_);
  }
  link_allocation = std::max(encoder_target_rate_, link_allocation);
  video_stream_encoder_->OnBitrateUpdated(
      encoder_target_rate_, link_allocation,
      dchecked_cast<uint8_t>(update.packet_loss_ratio * 256),
      update.round_trip_time.ms(), update.cwnd_reduce_ratio);
  stats_proxy_.OnSetEncoderTargetRate(encoder_target_rate_.bps());
  return protection_bitrate.bps();
}

std::optional<DataRate> VideoSendStreamImpl::GetUsedRate() const {
  // This value is for real-time video. Screenshare may have unused bandwidth
  // that can be shared, and this needs to be changed to support that.
  return std::nullopt;
}

}  // namespace internal
}  // namespace webrtc
