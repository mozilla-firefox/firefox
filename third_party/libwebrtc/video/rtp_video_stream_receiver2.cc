/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/rtp_video_stream_receiver2.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "api/crypto/frame_decryptor_interface.h"
#include "api/environment/environment.h"
#include "api/field_trials_view.h"
#include "api/frame_transformer_interface.h"
#include "api/make_ref_counted.h"
#include "api/rtp_headers.h"
#include "api/rtp_packet_info.h"
#include "api/rtp_packet_infos.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/task_queue_base.h"
#include "api/transport/rtp/corruption_detection_message.h"
#include "api/transport/rtp/dependency_descriptor.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/encoded_image.h"
#include "api/video/video_codec_constants.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_content_type.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_rotation.h"
#include "api/video/video_timing.h"
#include "call/rtp_config.h"
#include "call/rtp_packet_sink_interface.h"
#include "call/syncable.h"
#include "call/video_receive_stream.h"
#include "logging/rtc_event_log/events/rtc_event_rtp_packet_incoming.h"
#include "media/base/media_constants.h"
#include "modules/include/module_common_types.h"
#include "modules/pacing/packet_router.h"
#include "modules/remote_bitrate_estimator/include/remote_bitrate_estimator.h"
#include "modules/rtp_rtcp/include/receive_statistics.h"
#include "modules/rtp_rtcp/include/recovered_packet_receiver.h"
#include "modules/rtp_rtcp/include/rtcp_statistics.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/absolute_capture_time_interpolator.h"
#include "modules/rtp_rtcp/source/capture_clock_offset_updater.h"
#include "modules/rtp_rtcp/source/corruption_detection_extension.h"
#include "modules/rtp_rtcp/source/create_video_rtp_depacketizer.h"
#include "modules/rtp_rtcp/source/frame_object.h"
#include "modules/rtp_rtcp/source/rtp_dependency_descriptor_extension.h"
#include "modules/rtp_rtcp/source/rtp_generic_frame_descriptor.h"
#include "modules/rtp_rtcp/source/rtp_generic_frame_descriptor_extension.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_config.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_impl2.h"
#include "modules/rtp_rtcp/source/rtp_video_header.h"
#include "modules/rtp_rtcp/source/rtp_video_stream_receiver_frame_transformer_delegate.h"
#include "modules/rtp_rtcp/source/ulpfec_receiver.h"
#include "modules/rtp_rtcp/source/video_rtp_depacketizer.h"
#include "modules/rtp_rtcp/source/video_rtp_depacketizer_raw.h"
#include "modules/video_coding/h264_sprop_parameter_sets.h"
#include "modules/video_coding/h264_sps_pps_tracker.h"
#include "modules/video_coding/h26x_packet_buffer.h"
#include "modules/video_coding/loss_notification_controller.h"
#include "modules/video_coding/nack_requester.h"
#include "modules/video_coding/packet_buffer.h"
#include "modules/video_coding/rtp_frame_reference_finder.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/experiments/field_trial_parser.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/sequence_number_util.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/trace_event.h"
#include "system_wrappers/include/ntp_time.h"
#include "video/buffered_frame_decryptor.h"

namespace webrtc {

namespace {
// TODO(philipel): Change kPacketBufferStartSize back to 32 in M63 see:
//                 crbug.com/752886
constexpr int kPacketBufferStartSize = 512;
constexpr int kPacketBufferMaxSize = 2048;
constexpr int kMaxFrameHistorySize = 2048;

constexpr int kMaxPacketAgeToNack = 450;

int PacketBufferMaxSize(const FieldTrialsView& field_trials) {
  // The group here must be a positive power of 2, in which case that is used as
  // size. All other values shall result in the default value being used.
  const std::string group_name =
      field_trials.Lookup("WebRTC-PacketBufferMaxSize");
  int packet_buffer_max_size = kPacketBufferMaxSize;
  if (!group_name.empty() &&
      (sscanf(group_name.c_str(), "%d", &packet_buffer_max_size) != 1 ||
       packet_buffer_max_size <= 0 ||
       // Verify that the number is a positive power of 2.
       (packet_buffer_max_size & (packet_buffer_max_size - 1)) != 0)) {
    RTC_LOG(LS_WARNING) << "Invalid packet buffer max size: " << group_name;
    packet_buffer_max_size = kPacketBufferMaxSize;
  }
  return packet_buffer_max_size;
}

std::unique_ptr<ModuleRtpRtcpImpl2> CreateRtpRtcpModule(
    const Environment& env,
    ReceiveStatistics* receive_statistics,
    Transport* outgoing_transport,
    RtcpRttStats* rtt_stats,
    RtcpPacketTypeCounterObserver* rtcp_packet_type_counter_observer,
    RtcpCnameCallback* rtcp_cname_callback,
    PacketRouter* packet_router,
    bool non_sender_rtt_measurement,
    RtcpEventObserver* rtcp_event_observer,
    std::optional<uint32_t> remote_ssrc,
    uint32_t local_ssrc) {
  RtpRtcpInterface::Configuration configuration;
  configuration.audio = false;
  configuration.receiver_only = true;
  configuration.receive_statistics = receive_statistics;
  configuration.outgoing_transport = outgoing_transport;
  configuration.rtt_stats = rtt_stats;
  configuration.rtcp_packet_type_counter_observer =
      rtcp_packet_type_counter_observer;
  configuration.rtcp_cname_callback = rtcp_cname_callback;
  configuration.rtcp_event_observer = rtcp_event_observer;
  configuration.non_sender_rtt_measurement = non_sender_rtt_measurement;
  configuration.rtcp_mode = RtcpMode::kCompound;
  configuration.remote_ssrc = remote_ssrc;

  auto rtp_rtcp = ModuleRtpRtcpImpl2::CreateReceiveModule(
      env, configuration, [local_ssrc] { return local_ssrc; });

  return rtp_rtcp;
}

std::unique_ptr<NackRequester> MaybeConstructNackModule(
    const Environment& env,
    TaskQueueBase* current_queue,
    NackPeriodicProcessor* nack_periodic_processor,
    const NackConfig& nack,
    NackSender* nack_sender,
    KeyFrameRequestSender* keyframe_request_sender) {
  if (nack.rtp_history_ms == 0)
    return nullptr;

  // TODO(bugs.webrtc.org/12420): pass rtp_history_ms to the nack module.
  return std::make_unique<NackRequester>(
      current_queue, nack_periodic_processor, &env.clock(), nack_sender,
      keyframe_request_sender, env.field_trials());
}

std::unique_ptr<UlpfecReceiver> MaybeConstructUlpfecReceiver(
    uint32_t remote_ssrc,
    int red_payload_type,
    int ulpfec_payload_type,
    RecoveredPacketReceiver* callback,
    Clock* clock) {
  RTC_DCHECK_GE(red_payload_type, -1);
  RTC_DCHECK_GE(ulpfec_payload_type, -1);
  if (red_payload_type == -1)
    return nullptr;

  // TODO(tommi, brandtr): Consider including this check too once
  // `UlpfecReceiver` has been updated to not consider both red and ulpfec
  // payload ids.
  //  if (ulpfec_payload_type == -1)
  //    return nullptr;

  return std::make_unique<UlpfecReceiver>(remote_ssrc, ulpfec_payload_type,
                                          callback, clock);
}

const int kPacketLogIntervalMs = 10000;

}  // namespace

RtpVideoStreamReceiver2::RtcpFeedbackBuffer::RtcpFeedbackBuffer(
    KeyFrameRequestSender* key_frame_request_sender,
    NackSender* nack_sender,
    LossNotificationSender* loss_notification_sender,
    TaskQueueBase* worker_queue)
    : worker_task_checker_(worker_queue),
      key_frame_request_sender_(key_frame_request_sender),
      nack_sender_(nack_sender),
      loss_notification_sender_(loss_notification_sender),
      request_key_frame_(false) {
  RTC_DCHECK(key_frame_request_sender_);
  RTC_DCHECK(nack_sender_);
  RTC_DCHECK(loss_notification_sender_);
}

void RtpVideoStreamReceiver2::RtcpFeedbackBuffer::RequestKeyFrame() {
  RTC_DCHECK_RUN_ON(&worker_task_checker_);
  request_key_frame_ = true;
}

void RtpVideoStreamReceiver2::RtcpFeedbackBuffer::SendNack(
    const std::vector<uint16_t>& sequence_numbers,
    bool buffering_allowed) {
  RTC_DCHECK_RUN_ON(&worker_task_checker_);
  RTC_DCHECK(!sequence_numbers.empty());
  nack_sequence_numbers_.insert(nack_sequence_numbers_.end(),
                                sequence_numbers.cbegin(),
                                sequence_numbers.cend());
  if (!buffering_allowed) {
    // Note that while *buffering* is not allowed, *batching* is, meaning that
    // previously buffered messages may be sent along with the current message.
    SendBufferedRtcpFeedback();
  }
}

void RtpVideoStreamReceiver2::RtcpFeedbackBuffer::SendLossNotification(
    uint16_t last_decoded_seq_num,
    uint16_t last_received_seq_num,
    bool decodability_flag,
    bool buffering_allowed) {
  RTC_DCHECK_RUN_ON(&worker_task_checker_);
  RTC_DCHECK(buffering_allowed);
  RTC_DCHECK(!lntf_state_)
      << "SendLossNotification() called twice in a row with no call to "
         "SendBufferedRtcpFeedback() in between.";
  lntf_state_ = std::make_optional<LossNotificationState>(
      last_decoded_seq_num, last_received_seq_num, decodability_flag);
}

void RtpVideoStreamReceiver2::RtcpFeedbackBuffer::SendBufferedRtcpFeedback() {
  RTC_DCHECK_RUN_ON(&worker_task_checker_);

  bool request_key_frame = false;
  std::vector<uint16_t> nack_sequence_numbers;
  std::optional<LossNotificationState> lntf_state;

  std::swap(request_key_frame, request_key_frame_);
  std::swap(nack_sequence_numbers, nack_sequence_numbers_);
  std::swap(lntf_state, lntf_state_);

  if (lntf_state) {
    // If either a NACK or a key frame request is sent, we should buffer
    // the LNTF and wait for them (NACK or key frame request) to trigger
    // the compound feedback message.
    // Otherwise, the LNTF should be sent out immediately.
    const bool buffering_allowed =
        request_key_frame || !nack_sequence_numbers.empty();

    loss_notification_sender_->SendLossNotification(
        lntf_state->last_decoded_seq_num, lntf_state->last_received_seq_num,
        lntf_state->decodability_flag, buffering_allowed);
  }

  if (request_key_frame) {
    key_frame_request_sender_->RequestKeyFrame();
  } else if (!nack_sequence_numbers.empty()) {
    nack_sender_->SendNack(nack_sequence_numbers, true);
  }
}

void RtpVideoStreamReceiver2::RtcpFeedbackBuffer::ClearLossNotificationState() {
  RTC_DCHECK_RUN_ON(&worker_task_checker_);
  lntf_state_.reset();
}

RtpVideoStreamReceiver2::RtpVideoStreamReceiver2(
    const Environment& env,
    TaskQueueBase* worker_queue,
    Transport* transport,
    RtcpRttStats* rtt_stats,
    PacketRouter* packet_router,
    const VideoReceiveStreamInterface::Config* config,
    TimeDelta max_wait_for_keyframe,
    ReceiveStatistics* rtp_receive_statistics,
    RtcpPacketTypeCounterObserver* rtcp_packet_type_counter_observer,
    RtcpCnameCallback* rtcp_cname_callback,
    NackPeriodicProcessor* nack_periodic_processor,
    VideoStreamBufferControllerStatsObserver* vcm_receive_statistics,
    OnCompleteFrameCallback* complete_frame_callback,
    scoped_refptr<FrameDecryptorInterface> frame_decryptor,
    scoped_refptr<FrameTransformerInterface> frame_transformer,
    absl::AnyInvocable<void(uint32_t ssrc) &&> on_first_packet)
    : env_(env),
      worker_queue_(worker_queue),
      config_(*config),
      packet_router_(packet_router),
      ntp_estimator_(&env_.clock()),
      forced_playout_delay_max_ms_("max_ms", std::nullopt),
      forced_playout_delay_min_ms_("min_ms", std::nullopt),
      rtp_receive_statistics_(rtp_receive_statistics),
      ulpfec_receiver_(
          MaybeConstructUlpfecReceiver(config->rtp.remote_ssrc,
                                       config->rtp.red_payload_type,
                                       config->rtp.ulpfec_payload_type,
                                       this,
                                       &env_.clock())),
      red_payload_type_(config_.rtp.red_payload_type),
      packet_sink_(config->rtp.packet_sink_),
      receiving_(false),
      last_packet_log_ms_(-1),
      rtp_rtcp_(CreateRtpRtcpModule(
          env_,
          rtp_receive_statistics_,
          transport,
          rtt_stats,
          rtcp_packet_type_counter_observer,
          rtcp_cname_callback,
          packet_router,
          config_.rtp.rtcp_xr.receiver_reference_time_report,
          config_.rtp.rtcp_event_observer,
          config->rtp.remote_ssrc,
          config_.rtp.local_ssrc)),
      nack_periodic_processor_(nack_periodic_processor),
      complete_frame_callback_(complete_frame_callback),
      keyframe_request_method_(config_.rtp.keyframe_method),
      // TODO(bugs.webrtc.org/10336): Let `rtcp_feedback_buffer_` communicate
      // directly with `rtp_rtcp_`.
      rtcp_feedback_buffer_(this, this, this, worker_queue),
      nack_module_(MaybeConstructNackModule(env_,
                                            worker_queue,
                                            nack_periodic_processor,
                                            config_.rtp.nack,
                                            &rtcp_feedback_buffer_,
                                            &rtcp_feedback_buffer_)),
      vcm_receive_statistics_(vcm_receive_statistics),
      packet_buffer_(kPacketBufferStartSize,
                     PacketBufferMaxSize(env_.field_trials())),
      reference_finder_(
          std::make_unique<RtpFrameReferenceFinder>(&env_.field_trials())),
      has_received_frame_(false),
      frames_decryptable_(false),
      absolute_capture_time_interpolator_(&env_.clock()),
      max_wait_for_keyframe_(max_wait_for_keyframe),
      on_first_packet_(std::move(on_first_packet)) {
  RTC_DCHECK_GT(max_wait_for_keyframe_, TimeDelta::Zero());
  if (packet_router_) {
    // Do not register as REMB candidate, this is only done when starting to
    // receive.
    packet_router_->AddReceiveRtpModule(rtp_rtcp_.get(),
                                        /*remb_candidate=*/false);
  }

  RTC_DCHECK(config_.rtp.rtcp_mode != RtcpMode::kOff)
      << "A stream should not be configured with RTCP disabled. This value is "
         "reserved for internal usage.";

  rtp_rtcp_->SetRTCPStatus(config_.rtp.rtcp_mode);

  if (config_.rtp.nack.rtp_history_ms > 0) {
    rtp_receive_statistics_->SetMaxReorderingThreshold(config_.rtp.remote_ssrc,
                                                       kMaxPacketAgeToNack);
  }
  ParseFieldTrial(
      {&forced_playout_delay_max_ms_, &forced_playout_delay_min_ms_},
      env_.field_trials().Lookup("WebRTC-ForcePlayoutDelay"));

  if (config_.rtp.lntf.enabled) {
    loss_notification_controller_ =
        std::make_unique<LossNotificationController>(&rtcp_feedback_buffer_,
                                                     &rtcp_feedback_buffer_);
  }

  // Only construct the encrypted receiver if frame encryption is enabled.
  if (config_.crypto_options.sframe.require_frame_encryption) {
    buffered_frame_decryptor_ = std::make_unique<BufferedFrameDecryptor>(
        this, this, env_.field_trials());
    if (frame_decryptor != nullptr) {
      buffered_frame_decryptor_->SetFrameDecryptor(std::move(frame_decryptor));
    }
  }

  if (frame_transformer) {
    frame_transformer_delegate_ =
        make_ref_counted<RtpVideoStreamReceiverFrameTransformerDelegate>(
            this, &env_.clock(), std::move(frame_transformer),
            TaskQueueBase::Current(), config_.rtp.remote_ssrc);
    frame_transformer_delegate_->Init();
  }
}

RtpVideoStreamReceiver2::~RtpVideoStreamReceiver2() {
  if (packet_router_)
    packet_router_->RemoveReceiveRtpModule(rtp_rtcp_.get());
  ulpfec_receiver_.reset();
  if (frame_transformer_delegate_)
    frame_transformer_delegate_->Reset();
}

void RtpVideoStreamReceiver2::AddReceiveCodec(
    uint8_t payload_type,
    VideoCodecType video_codec,
    const CodecParameterMap& codec_params,
    bool raw_payload) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  if (codec_params.count(std::string(kH264FmtpSpsPpsIdrInKeyframe)) > 0 ||
      env_.field_trials().IsEnabled("WebRTC-SpsPpsIdrIsH264Keyframe")) {
    packet_buffer_.ForceSpsPpsIdrIsH264Keyframe();
    sps_pps_idr_is_h264_keyframe_ = true;
    if (h26x_packet_buffer_ != nullptr) {
      // Update existing H26xPacketBuffer setting in-place if codec parameters
      // change mid-stream to avoid recreating the buffer and dropping packets.
      h26x_packet_buffer_->SetH264IdrOnlyKeyframesAllowed(false);
    }
  }
  if (receiving_ && h26x_packet_buffer_ == nullptr &&
      UseH26xPacketBuffer(video_codec)) {
    h26x_packet_buffer_ =
        std::make_unique<H26xPacketBuffer>(!sps_pps_idr_is_h264_keyframe_);
  }
  const CodecTypeAndRaw new_state{.video_codec = video_codec,
                                  .raw_payload = raw_payload};
  const auto existing_codec_it = pt_codec_.find(payload_type);
  const auto existing_params_it = pt_codec_params_.find(payload_type);
  // Return early if the codec has already been added.
  if (existing_codec_it != pt_codec_.end() &&
      existing_codec_it->second == new_state &&
      existing_params_it != pt_codec_params_.end() &&
      existing_params_it->second == codec_params &&
      payload_type_map_.contains(payload_type)) {
    return;
  }
  payload_type_map_.insert_or_assign(
      payload_type, raw_payload ? std::make_unique<VideoRtpDepacketizerRaw>()
                                : CreateVideoRtpDepacketizer(video_codec));
  pt_codec_params_.insert_or_assign(existing_params_it, payload_type,
                                    codec_params);
  pt_codec_.insert_or_assign(existing_codec_it, payload_type, new_state);
}

void RtpVideoStreamReceiver2::SetReceiveCodecs(
    const std::vector<ReceiveCodec>& codecs) {
  RTC_DCHECK_RUN_ON(worker_queue_);

  // Reset H.264 keyframe mode to default so that it is re-evaluated across the
  // new codec set in AddReceiveCodec(). This only updates the keyframe criteria
  // and does not flush cached SPS/PPS or buffered packets.
  sps_pps_idr_is_h264_keyframe_ = false;
  packet_buffer_.ResetSpsPpsIdrIsH264Keyframe();
  if (h26x_packet_buffer_ != nullptr) {
    h26x_packet_buffer_->SetH264IdrOnlyKeyframesAllowed(true);
  }

  // Removes any already registered codecs which are not part of the new set of
  // codecs.
  for (auto it = pt_codec_.begin(); it != pt_codec_.end();) {
    bool found = absl::c_any_of(codecs, [&](const ReceiveCodec& codec) {
      return codec.payload_type == it->first;
    });
    if (!found) {
      payload_type_map_.erase(it->first);
      pt_codec_params_.erase(it->first);
      it = pt_codec_.erase(it);
    } else {
      ++it;
    }
  }

  for (const ReceiveCodec& codec : codecs) {
    AddReceiveCodec(codec.payload_type, codec.video_codec, codec.codec_params,
                    codec.raw_payload);
  }
}

void RtpVideoStreamReceiver2::RemoveReceiveCodecs() {
  RTC_DCHECK_RUN_ON(worker_queue_);

  pt_codec_params_.clear();
  payload_type_map_.clear();
  packet_buffer_.ResetSpsPpsIdrIsH264Keyframe();
  sps_pps_idr_is_h264_keyframe_ = false;
  h26x_packet_buffer_.reset();
  pt_codec_.clear();
}

void RtpVideoStreamReceiver2::SetRawPayloadTypes(
    const std::set<int>& raw_payload_types) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  for (auto& [pt, codec_state] : pt_codec_) {
    const bool raw_payload = raw_payload_types.contains(pt);
    codec_state.raw_payload = raw_payload;
    payload_type_map_[pt] =
        raw_payload ? std::make_unique<VideoRtpDepacketizerRaw>()
                    : CreateVideoRtpDepacketizer(codec_state.video_codec);
  }
}

std::optional<Syncable::Info> RtpVideoStreamReceiver2::GetSyncInfo() const {
  RTC_DCHECK_RUN_ON(worker_queue_);
  Syncable::Info info;
  std::optional<RtpRtcpInterface::SenderReportStats> last_sr =
      rtp_rtcp_->GetSenderReportStats();
  if (!last_sr.has_value()) {
    return std::nullopt;
  }
  info.capture_time_ntp = last_sr->last_remote_ntp_timestamp;
  info.capture_time_rtp = last_sr->last_remote_rtp_timestamp;

  if (!last_received_rtp_timestamp_ || !last_received_rtp_system_time_) {
    return std::nullopt;
  }
  info.latest_received_capture_rtp_timestamp = *last_received_rtp_timestamp_;
  info.latest_receive_time = *last_received_rtp_system_time_;

  // Leaves info.current_delay_ms uninitialized.
  return info;
}

RtpVideoStreamReceiver2::ParseGenericDependenciesResult
RtpVideoStreamReceiver2::ParseGenericDependenciesExtension(
    const RtpPacketReceived& rtp_packet,
    RTPVideoHeader* video_header) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  if (DependencyDescriptorMandatory dd_mandatory;
      rtp_packet.GetExtension<RtpDependencyDescriptorExtension>(
          &dd_mandatory)) {
    const int64_t frame_id =
        frame_id_unwrapper_.Unwrap(dd_mandatory.frame_number());
    DependencyDescriptor dependency_descriptor;
    if (!rtp_packet.GetExtension<RtpDependencyDescriptorExtension>(
            video_structure_.get(), &dependency_descriptor)) {
      if (!video_structure_frame_id_ || frame_id < video_structure_frame_id_) {
        return kDropPacket;
      } else {
        return kStashPacket;
      }
    }

    if (dependency_descriptor.attached_structure != nullptr &&
        !dependency_descriptor.first_packet_in_frame) {
      RTC_LOG(LS_WARNING) << "ssrc: " << rtp_packet.Ssrc()
                          << "Invalid dependency descriptor: structure "
                             "attached to non first packet of a frame.";
      return kDropPacket;
    }
    video_header->is_first_packet_in_frame =
        dependency_descriptor.first_packet_in_frame;
    video_header->is_last_packet_in_frame =
        dependency_descriptor.last_packet_in_frame;

    auto& generic_descriptor_info = video_header->generic.emplace();
    generic_descriptor_info.frame_id = frame_id;
    generic_descriptor_info.spatial_index =
        dependency_descriptor.frame_dependencies.spatial_id;
    generic_descriptor_info.temporal_index =
        dependency_descriptor.frame_dependencies.temporal_id;
    for (int fdiff : dependency_descriptor.frame_dependencies.frame_diffs) {
      generic_descriptor_info.dependencies.push_back(frame_id - fdiff);
    }
    generic_descriptor_info.decode_target_indications =
        dependency_descriptor.frame_dependencies.decode_target_indications;
    if (dependency_descriptor.resolution) {
      video_header->width = dependency_descriptor.resolution->Width();
      video_header->height = dependency_descriptor.resolution->Height();
    }

    // FrameDependencyStructure is sent in dependency descriptor of the first
    // packet of a key frame and required for parsed dependency descriptor in
    // all the following packets until next key frame.
    // Save it if there is a (potentially) new structure.
    if (dependency_descriptor.attached_structure) {
      RTC_DCHECK(dependency_descriptor.first_packet_in_frame);
      if (video_structure_frame_id_.has_value() &&
          video_structure_frame_id_ > frame_id) {
        RTC_LOG(LS_WARNING)
            << "Arrived key frame with id " << frame_id << " and structure id "
            << dependency_descriptor.attached_structure->structure_id
            << " is older than the latest received key frame with id "
            << *video_structure_frame_id_ << " and structure id "
            << video_structure_->structure_id;
        return kDropPacket;
      }
      video_structure_ = std::move(dependency_descriptor.attached_structure);
      video_structure_frame_id_ = frame_id;
      video_header->frame_type = VideoFrameType::kVideoFrameKey;
      return kNewVideoStructure;
    } else {
      video_header->frame_type = VideoFrameType::kVideoFrameDelta;
      return kHasGenericDescriptor;
    }
  }

  RtpGenericFrameDescriptor generic_frame_descriptor;
  if (!rtp_packet.GetExtension<RtpGenericFrameDescriptorExtension00>(
          &generic_frame_descriptor)) {
    return kNoGenericDescriptor;
  }

  video_header->is_first_packet_in_frame =
      generic_frame_descriptor.FirstPacketInSubFrame();
  video_header->is_last_packet_in_frame =
      generic_frame_descriptor.LastPacketInSubFrame();

  if (generic_frame_descriptor.FirstPacketInSubFrame()) {
    video_header->frame_type =
        generic_frame_descriptor.FrameDependenciesDiffs().empty()
            ? VideoFrameType::kVideoFrameKey
            : VideoFrameType::kVideoFrameDelta;

    auto& generic_descriptor_info = video_header->generic.emplace();
    int64_t frame_id =
        frame_id_unwrapper_.Unwrap(generic_frame_descriptor.FrameId());
    generic_descriptor_info.frame_id = frame_id;
    generic_descriptor_info.spatial_index =
        generic_frame_descriptor.SpatialLayer();
    generic_descriptor_info.temporal_index =
        generic_frame_descriptor.TemporalLayer();
    for (uint16_t fdiff : generic_frame_descriptor.FrameDependenciesDiffs()) {
      generic_descriptor_info.dependencies.push_back(frame_id - fdiff);
    }
  }
  video_header->width = generic_frame_descriptor.Width();
  video_header->height = generic_frame_descriptor.Height();
  return kHasGenericDescriptor;
}

void RtpVideoStreamReceiver2::OnReceivedPayloadDataForTesting(
    CopyOnWriteBuffer codec_payload,
    const RtpPacketReceived& rtp_packet,
    const RTPVideoHeader& video) {
  RTC_DCHECK_RUN_ON(worker_queue_);

  std::optional<int64_t> unwrapped_rtp_seq_num =
      UnwrapSequenceNumberOrRecover(rtp_packet, &video);
  if (!unwrapped_rtp_seq_num.has_value()) {
    return;
  }

  auto packet = std::make_unique<video_coding::PacketBuffer::Packet>(
      rtp_packet, *unwrapped_rtp_seq_num, video);
  packet->video_payload = std::move(codec_payload);

  OnReceivedPayloadData(rtp_packet, packet);
}

std::optional<int64_t> RtpVideoStreamReceiver2::UnwrapSequenceNumberOrRecover(
    const RtpPacketReceived& rtp_packet,
    const RTPVideoHeader* absl_nullable video_header) {
  RTC_DCHECK_RUN_ON(worker_queue_);

  // H26x needs codec-specific recovery: H26xPacketBuffer has no reset API, and
  // H264 keyframe classification may change in H264SpsPpsTracker.
  if (video_header != nullptr && (video_header->codec == kVideoCodecH264 ||
                                  video_header->codec == kVideoCodecH265)) {
    const int64_t unwrapped_seq_num =
        rtp_seq_num_unwrapper_.Unwrap(rtp_packet.SequenceNumber());
    UpdateNewestMediaSeqNumAndTimestamp(unwrapped_seq_num,
                                        rtp_packet.Timestamp());
    return unwrapped_seq_num;
  }

  int64_t unwrapped_seq_num =
      rtp_seq_num_unwrapper_.PeekUnwrap(rtp_packet.SequenceNumber());
  const bool is_media = video_header != nullptr;
  const bool starts_frame = is_media && video_header->is_first_packet_in_frame;
  const bool is_keyframe =
      is_media && video_header->frame_type == VideoFrameType::kVideoFrameKey;

  // PacketBuffer cannot recover dependencies across more packets than it can
  // retain. An RTP timestamp newer than any received media corroborates that
  // this is resumed media, rather than a severely reordered packet from an old
  // frame.
  const bool large_backward_jump =
      newest_media_seq_num_.has_value() &&
      *newest_media_seq_num_ - unwrapped_seq_num > kPacketBufferMaxSize;
  const bool timestamp_is_newer =
      newest_media_rtp_timestamp_.has_value() &&
      AheadOf(rtp_packet.Timestamp(), *newest_media_rtp_timestamp_);
  const bool unrecoverable_backward_jump =
      is_media && large_backward_jump && timestamp_is_newer;

  if (unrecoverable_backward_jump) {
    waiting_for_keyframe_after_seq_num_discontinuity_ = true;
  }

  if (waiting_for_keyframe_after_seq_num_discontinuity_) {
    // Re-anchor only at a frame boundary. Earlier reordered packets can be
    // recovered by NACK. A non-boundary packet that starts recovery also
    // requests a keyframe.
    const bool is_newer_keyframe =
        starts_frame && is_keyframe && timestamp_is_newer;
    if (!is_newer_keyframe) {
      Timestamp now = env_.clock().CurrentTime();
      if (next_keyframe_request_for_seq_num_discontinuity_ < now) {
        RTC_LOG(LS_WARNING)
            << "Large RTP sequence number discontinuity; requesting a "
               "keyframe";
        RequestKeyFrame();
        next_keyframe_request_for_seq_num_discontinuity_ =
            now + max_wait_for_keyframe_;
      }
      return std::nullopt;
    }

    // Drop state from the previous epoch and anchor at the recovery keyframe.
    packet_buffer_.Clear();
    reference_finder_ = std::make_unique<RtpFrameReferenceFinder>(
        &env_.field_trials(),
        last_completed_picture_id_ + std::numeric_limits<uint16_t>::max());
    nack_module_ = MaybeConstructNackModule(
        env_, worker_queue_, nack_periodic_processor_, config_.rtp.nack,
        &rtcp_feedback_buffer_, &rtcp_feedback_buffer_);
    if (loss_notification_controller_) {
      loss_notification_controller_ =
          std::make_unique<LossNotificationController>(&rtcp_feedback_buffer_,
                                                       &rtcp_feedback_buffer_);
    }
    rtp_seq_num_unwrapper_.Reset();
    frame_id_unwrapper_.Reset();
    video_structure_.reset();
    video_structure_frame_id_.reset();
    stashed_packets_.clear();
    last_seq_num_for_pic_id_.clear();
    last_timestamp_for_pic_id_.clear();
    last_received_rtp_system_time_.reset();
    last_received_keyframe_rtp_system_time_.reset();
    last_received_keyframe_rtp_timestamp_.reset();
    waiting_for_keyframe_after_seq_num_discontinuity_ = false;
    next_keyframe_request_for_seq_num_discontinuity_ =
        Timestamp::MinusInfinity();
    unwrapped_seq_num =
        rtp_seq_num_unwrapper_.Unwrap(rtp_packet.SequenceNumber());
    newest_media_seq_num_ = unwrapped_seq_num;
    newest_media_rtp_timestamp_ = rtp_packet.Timestamp();
    return unwrapped_seq_num;
  }

  // Do not let padding, FEC, or old media move the unwrapper to an ambiguous
  // epoch. A later media packet can corroborate the discontinuity.
  if (large_backward_jump) {
    return std::nullopt;
  }

  unwrapped_seq_num =
      rtp_seq_num_unwrapper_.Unwrap(rtp_packet.SequenceNumber());
  if (is_media) {
    UpdateNewestMediaSeqNumAndTimestamp(unwrapped_seq_num,
                                        rtp_packet.Timestamp());
  }
  return unwrapped_seq_num;
}

void RtpVideoStreamReceiver2::UpdateNewestMediaSeqNumAndTimestamp(
    int64_t unwrapped_seq_num,
    uint32_t rtp_timestamp) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  if (!newest_media_seq_num_.has_value() ||
      unwrapped_seq_num > *newest_media_seq_num_) {
    newest_media_seq_num_ = unwrapped_seq_num;
  }
  if (!newest_media_rtp_timestamp_.has_value() ||
      AheadOf(rtp_timestamp, *newest_media_rtp_timestamp_)) {
    newest_media_rtp_timestamp_ = rtp_timestamp;
  }
}

RtpVideoStreamReceiver2::StashResult
RtpVideoStreamReceiver2::OnReceivedPayloadData(
    const RtpPacketReceived& rtp_packet,
    absl_nonnull std::unique_ptr<video_coding::PacketBuffer::Packet>& packet) {
  RTC_DCHECK_RUN_ON(worker_queue_);

  RtpPacketInfo& packet_info = packet->rtp_packet_info;
  packet_info.set_receive_time(env_.clock().CurrentTime());

  // Try to extrapolate absolute capture time if it is missing.
  packet_info.set_absolute_capture_time(
      absolute_capture_time_interpolator_.OnReceivePacket(
          AbsoluteCaptureTimeInterpolator::GetSource(packet_info.ssrc(),
                                                     packet_info.csrcs()),
          packet_info.rtp_timestamp(),
          // Assume frequency is the same one for all video frames.
          kVideoPayloadTypeFrequency,
          rtp_packet.GetExtension<AbsoluteCaptureTimeExtension>()));
  if (packet_info.absolute_capture_time().has_value()) {
    packet_info.set_local_capture_clock_offset(
        CaptureClockOffsetUpdater::ConvertToTimeDelta(
            capture_clock_offset_updater_.AdjustEstimatedCaptureClockOffset(
                packet_info.absolute_capture_time()
                    ->estimated_capture_clock_offset)));
  }
  RTPVideoHeader& video_header = packet->video_header;
  video_header.rotation = kVideoRotation_0;
  video_header.content_type = VideoContentType::UNSPECIFIED;
  video_header.video_timing.flags = VideoSendTiming::kInvalid;
  video_header.is_last_packet_in_frame |= rtp_packet.Marker();

  rtp_packet.GetExtension<VideoOrientation>(&video_header.rotation);
  rtp_packet.GetExtension<VideoContentTypeExtension>(
      &video_header.content_type);
  rtp_packet.GetExtension<VideoTimingExtension>(&video_header.video_timing);
  if (forced_playout_delay_max_ms_ && forced_playout_delay_min_ms_) {
    if (!video_header.playout_delay.emplace().Set(
            TimeDelta::Millis(*forced_playout_delay_min_ms_),
            TimeDelta::Millis(*forced_playout_delay_max_ms_))) {
      video_header.playout_delay = std::nullopt;
    }
  } else {
    video_header.playout_delay = rtp_packet.GetExtension<PlayoutDelayLimits>();
  }

  if (!rtp_packet.recovered()) {
    UpdatePacketReceiveTimestamps(
        rtp_packet, video_header.frame_type == VideoFrameType::kVideoFrameKey);
  }

  ParseGenericDependenciesResult generic_descriptor_state =
      ParseGenericDependenciesExtension(rtp_packet, &video_header);

  if (generic_descriptor_state == kStashPacket) {
    return StashResult::kStash;
  } else if (generic_descriptor_state == kDropPacket) {
    Timestamp now = env_.clock().CurrentTime();
    if (now - last_logged_failed_to_parse_dd_ > TimeDelta::Seconds(1)) {
      last_logged_failed_to_parse_dd_ = now;
      RTC_LOG(LS_WARNING) << "Failed to parse dependency descriptor for "
                          << "ssrc: " << rtp_packet.Ssrc()
                          << ", timestamp: " << rtp_packet.Timestamp();
    }
    if (video_structure_ == nullptr &&
        next_keyframe_request_for_missing_video_structure_ < now) {
      // No video structure received yet, most likely part of the initial
      // keyframe was lost.
      RequestKeyFrame();
      next_keyframe_request_for_missing_video_structure_ =
          now + TimeDelta::Seconds(1);
    }
    return StashResult::kIgnore;
  }

  // Extensions that should only be transmitted in the last packet of a frame.
  if (video_header.is_last_packet_in_frame) {
    video_header.color_space = rtp_packet.GetExtension<ColorSpaceExtension>();
    if (video_header.color_space ||
        video_header.frame_type == VideoFrameType::kVideoFrameKey) {
      // Store color space since it's only transmitted when changed or for key
      // frames. Color space will be cleared if a key frame is transmitted
      // without color space information.
      last_color_space_ = video_header.color_space;
    } else if (last_color_space_) {
      video_header.color_space = last_color_space_;
    }

    std::optional<int> spatial_id;
    if (video_header.generic.has_value()) {
      spatial_id = video_header.generic->spatial_index;
      if (spatial_id >= kMaxSpatialLayers) {
        RTC_LOG(LS_WARNING) << "Invalid spatial id: " << *spatial_id
                            << " in generic descriptor.";
        spatial_id.reset();
      }
    } else {
      spatial_id = 0;
    }

    std::optional<CorruptionDetectionMessage> message =
        rtp_packet.GetExtension<CorruptionDetectionExtension>();
    if (message.has_value() && spatial_id.has_value()) {
      RTC_CHECK_GE(*spatial_id, 0);
      video_header.frame_instrumentation_data =
          last_corruption_detection_state_by_layer_[*spatial_id].ParseMessage(
              *message);
    }
  }
  video_header.video_frame_tracking_id =
      rtp_packet.GetExtension<VideoFrameTrackingIdExtension>();

  if (loss_notification_controller_) {
    if (rtp_packet.recovered()) {
      // TODO(bugs.webrtc.org/10336): Implement support for reordering.
      RTC_LOG(LS_INFO)
          << "LossNotificationController does not support reordering.";
    } else if (generic_descriptor_state == kNoGenericDescriptor) {
      RTC_LOG(LS_WARNING) << "LossNotificationController requires generic "
                             "frame descriptor, but it is missing.";
    } else {
      if (video_header.is_first_packet_in_frame) {
        RTC_DCHECK(video_header.generic);
        LossNotificationController::FrameDetails frame;
        frame.is_keyframe =
            video_header.frame_type == VideoFrameType::kVideoFrameKey;
        frame.frame_id = video_header.generic->frame_id;
        frame.frame_dependencies = video_header.generic->dependencies;
        loss_notification_controller_->OnReceivedPacket(
            rtp_packet.SequenceNumber(), &frame);
      } else {
        loss_notification_controller_->OnReceivedPacket(
            rtp_packet.SequenceNumber(), nullptr);
      }
    }
  }

  if (packet->video_payload.empty()) {
    NotifyReceiverOfEmptyPacket(packet->seq_num(),
                                GetCodecFromPayloadType(packet->payload_type));
    rtcp_feedback_buffer_.SendBufferedRtcpFeedback();
    return StashResult::kIgnore;
  }

  if (packet->codec() == kVideoCodecH264) {
    // Only when we start to receive packets will we know what payload type
    // that will be used. When we know the payload type insert the correct
    // sps/pps into the tracker.
    if (packet->payload_type != last_payload_type_) {
      last_payload_type_ = packet->payload_type;
      InsertSpsPpsIntoTracker(packet->payload_type);
    }
  }

  if (packet->codec() == kVideoCodecH264 &&
      !UseH26xPacketBuffer(packet->codec())) {
    video_coding::H264SpsPpsTracker::FixedBitstream fixed =
        tracker_.CopyAndFixBitstream(packet->video_payload,
                                     &packet->video_header);

    switch (fixed.action) {
      case video_coding::H264SpsPpsTracker::kRequestKeyframe:
        rtcp_feedback_buffer_.RequestKeyFrame();
        rtcp_feedback_buffer_.SendBufferedRtcpFeedback();
        [[fallthrough]];
      case video_coding::H264SpsPpsTracker::kDrop:
        return StashResult::kIgnore;
      case video_coding::H264SpsPpsTracker::kInsert:
        packet->video_payload = std::move(fixed.bitstream);
        break;
    }
  }

  rtcp_feedback_buffer_.SendBufferedRtcpFeedback();
  frame_counter_.Add(packet->timestamp);

  if (h26x_packet_buffer_ && UseH26xPacketBuffer(packet->codec())) {
    OnInsertedPacket(h26x_packet_buffer_->InsertPacket(std::move(packet)));
  } else {
    OnInsertedPacket(packet_buffer_.InsertPacket(std::move(packet)));
  }
  return generic_descriptor_state == kNewVideoStructure ? StashResult::kUnstash
                                                        : StashResult::kIgnore;
}

void RtpVideoStreamReceiver2::OnRecoveredPacket(
    const RtpPacketReceived& packet) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  if (packet.PayloadType() == red_payload_type_) {
    RTC_LOG(LS_WARNING) << "Discarding recovered packet with RED encapsulation";
    return;
  }
  ReceivePacket(packet);
}

// This method handles both regular RTP packets and packets recovered
// via RTX or FlexFEC.
void RtpVideoStreamReceiver2::OnRtpPacket(const RtpPacketReceived& packet) {
  RTC_DCHECK_RUN_ON(worker_queue_);

  if (!packet.recovered()) {
    // Recovery packets (RTX or FlexFEC) are logged in their respective streams.
    env_.event_log().Log(std::make_unique<RtcEventRtpPacketIncoming>(packet));
  }

  if (!receiving_)
    return;

  if (on_first_packet_) {
    auto cb = std::move(on_first_packet_);
    std::move(cb)(config_.rtp.remote_ssrc);
  }

  ReceivePacket(packet);

  // Update receive statistics after ReceivePacket.
  // Receive statistics will be reset if the payload type changes (make sure
  // that the first packet is included in the stats).
  if (!packet.recovered()) {
    rtp_receive_statistics_->OnRtpPacket(packet);
  }

  if (packet_sink_) {
    packet_sink_->OnRtpPacket(packet);
  }
}

void RtpVideoStreamReceiver2::RequestKeyFrame() {
  RTC_DCHECK_RUN_ON(worker_queue_);
  TRACE_EVENT2("webrtc", "RtpVideoStreamReceiver2::RequestKeyFrame",
               "remote_ssrc", config_.rtp.remote_ssrc, "method",
               keyframe_request_method_ == KeyFrameReqMethod::kPliRtcp   ? "PLI"
               : keyframe_request_method_ == KeyFrameReqMethod::kFirRtcp ? "FIR"
               : keyframe_request_method_ == KeyFrameReqMethod::kNone ? "None"
                                                                      : "Other");
  // TODO(bugs.webrtc.org/10336): Allow the sender to ignore key frame requests
  // issued by anything other than the LossNotificationController if it (the
  // sender) is relying on LNTF alone.
  if (keyframe_request_method_ == KeyFrameReqMethod::kPliRtcp) {
    rtp_rtcp_->SendPictureLossIndication();
  } else if (keyframe_request_method_ == KeyFrameReqMethod::kFirRtcp) {
    rtp_rtcp_->SendFullIntraRequest();
  }
}

void RtpVideoStreamReceiver2::SendNack(
    const std::vector<uint16_t>& sequence_numbers,
    bool /*buffering_allowed*/) {
  rtp_rtcp_->SendNack(sequence_numbers);
}

void RtpVideoStreamReceiver2::SendLossNotification(
    uint16_t last_decoded_seq_num,
    uint16_t last_received_seq_num,
    bool decodability_flag,
    bool buffering_allowed) {
  RTC_DCHECK(config_.rtp.lntf.enabled);
  rtp_rtcp_->SendLossNotification(last_decoded_seq_num, last_received_seq_num,
                                  decodability_flag, buffering_allowed);
}

bool RtpVideoStreamReceiver2::IsDecryptable() const {
  RTC_DCHECK_RUN_ON(worker_queue_);
  return frames_decryptable_;
}

void RtpVideoStreamReceiver2::OnInsertedPacket(
    video_coding::PacketBuffer::InsertResult result) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  video_coding::PacketBuffer::Packet* first_packet = nullptr;
  int max_nack_count;
  Timestamp min_recv_time = Timestamp::PlusInfinity();
  Timestamp max_recv_time = Timestamp::MinusInfinity();
  std::optional<int64_t> absolute_capture_time_ms;
  std::vector<std::span<const uint8_t>> payloads;
  RtpPacketInfos::vector_type packet_infos;

  bool skip_frame = false;
  for (auto& packet : result.packets) {
    if (skip_frame && !packet->is_first_packet_in_frame()) {
      continue;
    }
    skip_frame = false;

    const RtpPacketInfo& packet_info = packet->rtp_packet_info;
    if (packet->is_first_packet_in_frame()) {
      payloads.clear();
      packet_infos.clear();
      first_packet = packet.get();
      max_nack_count = packet->times_nacked;
      min_recv_time = packet_info.receive_time();
      max_recv_time = packet_info.receive_time();
      if (env_.field_trials().IsEnabled("WebRTC-UseAbsCapTimeForG2gMetric") &&
          packet_info.absolute_capture_time().has_value() &&
          packet_info.local_capture_clock_offset().has_value()) {
        absolute_capture_time_ms =
            NtpTime(
                packet_info.absolute_capture_time()->absolute_capture_timestamp)
                .ToMs() +
            packet_info.local_capture_clock_offset()->ms();
      }
    } else {
      max_nack_count = std::max(max_nack_count, packet->times_nacked);
      min_recv_time = std::min(min_recv_time, packet_info.receive_time());
      max_recv_time = std::max(max_recv_time, packet_info.receive_time());
    }
    payloads.emplace_back(packet->video_payload);
    packet_infos.push_back(packet_info);

    packet->video_header.absolute_capture_time =
        packet_info.absolute_capture_time();
    if (packet->is_last_packet_in_frame()) {
      auto depacketizer_it = payload_type_map_.find(first_packet->payload_type);
      // May happen if buffered packets are being drained for a payload type
      // that was removed during codec reconfiguration.
      if (depacketizer_it == payload_type_map_.end() ||
          !depacketizer_it->second) {
        RTC_LOG(LS_WARNING) << "Missing depacketizer for payload type "
                            << static_cast<int>(first_packet->payload_type);
        continue;
      }

      scoped_refptr<EncodedImageBuffer> bitstream =
          depacketizer_it->second->AssembleFrame(payloads);
      if (!bitstream) {
        // Failed to assemble a frame. Discard and continue.
        continue;
      }

      const video_coding::PacketBuffer::Packet& last_packet = *packet;
      OnAssembledFrame(std::make_unique<RtpFrameObject>(
          static_cast<uint16_t>(first_packet->seq_num()),  //
          static_cast<uint16_t>(last_packet.seq_num()),    //
          last_packet.marker_bit,                          //
          max_nack_count,                                  //
          min_recv_time,                                   //
          max_recv_time,                                   //
          first_packet->timestamp,                         //
          absolute_capture_time_ms.has_value()
              ? *absolute_capture_time_ms
              : ntp_estimator_.Estimate(first_packet->timestamp),  //
          last_packet.video_header.video_timing,                   //
          first_packet->payload_type,                              //
          first_packet->codec(),                                   //
          last_packet.video_header.rotation,                       //
          last_packet.video_header.content_type,                   //
          first_packet->video_header,                              //
          last_packet.video_header.color_space,                    //
          last_packet.video_header.frame_instrumentation_data,     //
          RtpPacketInfos(std::move(packet_infos)),                 //
          std::move(bitstream)));
      packet_infos.clear();
    }
  }
  if (result.buffer_cleared) {
    last_received_rtp_system_time_.reset();
    last_received_keyframe_rtp_system_time_.reset();
    last_received_keyframe_rtp_timestamp_.reset();
    RequestKeyFrame();
  }
}

void RtpVideoStreamReceiver2::OnAssembledFrame(
    std::unique_ptr<RtpFrameObject> frame) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  RTC_DCHECK(frame);

  const std::optional<RTPVideoHeader::GenericDescriptorInfo>& descriptor =
      frame->GetRtpVideoHeader().generic;

  if (loss_notification_controller_ && descriptor) {
    loss_notification_controller_->OnAssembledFrame(
        frame->first_seq_num(), descriptor->frame_id,
        absl::c_linear_search(descriptor->decode_target_indications,
                              DecodeTargetIndication::kDiscardable),
        descriptor->dependencies);
  }

  // If frames arrive before a key frame, they would not be decodable.
  // In that case, request a key frame ASAP.
  if (!has_received_frame_) {
    if (!frame->IsKey()) {
      // `loss_notification_controller_`, if present, would have already
      // requested a key frame when the first packet for the non-key frame
      // had arrived, so no need to replicate the request.
      if (!loss_notification_controller_) {
        RequestKeyFrame();
      }
    }
    has_received_frame_ = true;
  }

  // Reset `reference_finder_` if `frame` is new and the codec have changed.
  if (current_codec_) {
    bool frame_is_newer =
        AheadOf(frame->RtpTimestamp(), last_assembled_frame_rtp_timestamp_);

    if (frame->codec_type() != current_codec_) {
      if (frame_is_newer) {
        // When we reset the `reference_finder_` we don't want new picture ids
        // to overlap with old picture ids. To ensure that doesn't happen we
        // start from the `last_completed_picture_id_` and add an offset in case
        // of reordering.
        reference_finder_ = std::make_unique<RtpFrameReferenceFinder>(
            &env_.field_trials(),
            last_completed_picture_id_ + std::numeric_limits<uint16_t>::max());
        current_codec_ = frame->codec_type();
      } else {
        // Old frame from before the codec switch, discard it.
        return;
      }
    }

    if (frame_is_newer) {
      last_assembled_frame_rtp_timestamp_ = frame->RtpTimestamp();
    }
  } else {
    current_codec_ = frame->codec_type();
    last_assembled_frame_rtp_timestamp_ = frame->RtpTimestamp();
  }

  if (buffered_frame_decryptor_ != nullptr) {
    buffered_frame_decryptor_->ManageEncryptedFrame(std::move(frame));
  } else if (frame_transformer_delegate_) {
    frame_transformer_delegate_->TransformFrame(std::move(frame));
  } else {
    OnCompleteFrames(reference_finder_->ManageFrame(std::move(frame)));
  }
}

void RtpVideoStreamReceiver2::OnCompleteFrames(
    RtpFrameReferenceFinder::ReturnVector frames) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  for (auto& frame : frames) {
    last_seq_num_for_pic_id_[frame->Id()] = frame->last_seq_num();
    last_timestamp_for_pic_id_[frame->Id()] = frame->RtpTimestamp();

    last_completed_picture_id_ =
        std::max(last_completed_picture_id_, frame->Id());
    complete_frame_callback_->OnCompleteFrame(std::move(frame));
  }
  while (last_seq_num_for_pic_id_.size() > kMaxFrameHistorySize) {
    last_seq_num_for_pic_id_.erase(last_seq_num_for_pic_id_.begin());
  }
  while (last_timestamp_for_pic_id_.size() > kMaxFrameHistorySize) {
    last_timestamp_for_pic_id_.erase(last_timestamp_for_pic_id_.begin());
  }
}

void RtpVideoStreamReceiver2::OnDecryptedFrame(
    std::unique_ptr<RtpFrameObject> frame) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  OnCompleteFrames(reference_finder_->ManageFrame(std::move(frame)));
}

void RtpVideoStreamReceiver2::OnDecryptionStatusChange(
    FrameDecryptorInterface::Status status) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  // Called from BufferedFrameDecryptor::DecryptFrame.
  frames_decryptable_ =
      (status == FrameDecryptorInterface::Status::kOk) ||
      (status == FrameDecryptorInterface::Status::kRecoverable);
}

void RtpVideoStreamReceiver2::SetFrameDecryptor(
    scoped_refptr<FrameDecryptorInterface> frame_decryptor) {
  // TODO(bugs.webrtc.org/11993): Update callers or post the operation over to
  // the network thread.
  RTC_DCHECK_RUN_ON(worker_queue_);
  if (buffered_frame_decryptor_ == nullptr) {
    buffered_frame_decryptor_ = std::make_unique<BufferedFrameDecryptor>(
        this, this, env_.field_trials());
  }
  buffered_frame_decryptor_->SetFrameDecryptor(std::move(frame_decryptor));
}

void RtpVideoStreamReceiver2::SetDepacketizerToDecoderFrameTransformer(
    scoped_refptr<FrameTransformerInterface> frame_transformer) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  frame_transformer_delegate_ =
      make_ref_counted<RtpVideoStreamReceiverFrameTransformerDelegate>(
          this, &env_.clock(), std::move(frame_transformer),
          TaskQueueBase::Current(), config_.rtp.remote_ssrc);
  frame_transformer_delegate_->Init();
}

void RtpVideoStreamReceiver2::UpdateRtt(int64_t max_rtt_ms) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  if (nack_module_)
    nack_module_->UpdateRtt(max_rtt_ms);
}

void RtpVideoStreamReceiver2::SetRtcpMode(RtcpMode mode) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  rtp_rtcp_->SetRTCPStatus(mode);
}

void RtpVideoStreamReceiver2::SetReferenceTimeReport(bool enabled) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  rtp_rtcp_->SetNonSenderRttMeasurement(enabled);
}

void RtpVideoStreamReceiver2::SetPacketSink(
    RtpPacketSinkInterface* packet_sink) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  packet_sink_ = packet_sink;
}

void RtpVideoStreamReceiver2::SetLossNotificationEnabled(bool enabled) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  if (enabled && !loss_notification_controller_) {
    loss_notification_controller_ =
        std::make_unique<LossNotificationController>(&rtcp_feedback_buffer_,
                                                     &rtcp_feedback_buffer_);
  } else if (!enabled && loss_notification_controller_) {
    loss_notification_controller_.reset();
    rtcp_feedback_buffer_.ClearLossNotificationState();
  }
}

void RtpVideoStreamReceiver2::SetNackHistory(TimeDelta history) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  if (history.ms() == 0) {
    nack_module_.reset();
  } else if (!nack_module_) {
    nack_module_ = std::make_unique<NackRequester>(
        worker_queue_, nack_periodic_processor_, &env_.clock(),
        &rtcp_feedback_buffer_, &rtcp_feedback_buffer_, env_.field_trials());
  }

  rtp_receive_statistics_->SetMaxReorderingThreshold(
      config_.rtp.remote_ssrc,
      history.ms() > 0 ? kMaxPacketAgeToNack : kDefaultMaxReorderingThreshold);
}

void RtpVideoStreamReceiver2::SetMaxWaitForKeyframe(
    TimeDelta max_wait_for_keyframe) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  RTC_DCHECK_GT(max_wait_for_keyframe, TimeDelta::Zero());
  max_wait_for_keyframe_ = max_wait_for_keyframe;
}

int RtpVideoStreamReceiver2::ulpfec_payload_type() const {
  RTC_DCHECK_RUN_ON(worker_queue_);
  return ulpfec_receiver_ ? ulpfec_receiver_->ulpfec_payload_type() : -1;
}

int RtpVideoStreamReceiver2::red_payload_type() const {
  RTC_DCHECK_RUN_ON(worker_queue_);
  return red_payload_type_;
}

void RtpVideoStreamReceiver2::SetProtectionPayloadTypes(
    int red_payload_type,
    int ulpfec_payload_type) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  RTC_DCHECK(red_payload_type >= -1 && red_payload_type < 0x80);
  RTC_DCHECK(ulpfec_payload_type >= -1 && ulpfec_payload_type < 0x80);
  red_payload_type_ = red_payload_type;
  ulpfec_receiver_ =
      MaybeConstructUlpfecReceiver(config_.rtp.remote_ssrc, red_payload_type,
                                   ulpfec_payload_type, this, &env_.clock());
}

std::optional<int64_t> RtpVideoStreamReceiver2::LastReceivedPacketMs() const {
  RTC_DCHECK_RUN_ON(worker_queue_);
  if (last_received_rtp_system_time_) {
    return std::optional<int64_t>(last_received_rtp_system_time_->ms());
  }
  return std::nullopt;
}

std::optional<uint32_t> RtpVideoStreamReceiver2::LastReceivedFrameRtpTimestamp()
    const {
  RTC_DCHECK_RUN_ON(worker_queue_);
  return last_received_rtp_timestamp_;
}

std::optional<int64_t> RtpVideoStreamReceiver2::LastReceivedKeyframePacketMs()
    const {
  RTC_DCHECK_RUN_ON(worker_queue_);
  if (last_received_keyframe_rtp_system_time_) {
    return std::optional<int64_t>(
        last_received_keyframe_rtp_system_time_->ms());
  }
  return std::nullopt;
}

// Mozilla modification: VideoReceiveStream2 and friends do not surface RTCP
// stats at all, and even on the most recent libwebrtc code there does not
// seem to be any support for these stats right now. So, we hack this in.
void RtpVideoStreamReceiver2::RemoteRTCPSenderInfo(
    uint32_t* packet_count, uint32_t* octet_count,
    int64_t* ntp_timestamp_ms, int64_t* remote_ntp_timestamp_ms) const {
  RTC_DCHECK_RUN_ON(worker_queue_);
  rtp_rtcp_->RemoteRTCPSenderInfo(packet_count, octet_count, ntp_timestamp_ms,
                                  remote_ntp_timestamp_ms);
}

std::optional<RtpRtcpInterface::SenderReportStats>
RtpVideoStreamReceiver2::GetSenderReportStats() const {
  RTC_DCHECK_RUN_ON(worker_queue_);
  return rtp_rtcp_->GetSenderReportStats();
}

std::optional<RtpRtcpInterface::NonSenderRttStats>
RtpVideoStreamReceiver2::GetNonSenderRttStats() const {
  RTC_DCHECK_RUN_ON(worker_queue_);
  return rtp_rtcp_->GetNonSenderRttStats();
}

std::optional<VideoCodecType> RtpVideoStreamReceiver2::GetCodecFromPayloadType(
    uint8_t payload_type) const {
  RTC_DCHECK_RUN_ON(worker_queue_);
  auto it = pt_codec_.find(payload_type);
  if (it == pt_codec_.end()) {
    return std::nullopt;
  }
  return it->second.video_codec;
}

bool RtpVideoStreamReceiver2::UseH26xPacketBuffer(
    std::optional<VideoCodecType> codec) const {
  RTC_DCHECK_RUN_ON(worker_queue_);
  if (codec == kVideoCodecH265) {
    return true;
  }
  if (codec == kVideoCodecH264) {
    return env_.field_trials().IsEnabled("WebRTC-Video-H26xPacketBuffer");
  }
  return false;
}

void RtpVideoStreamReceiver2::ManageFrame(
    std::unique_ptr<RtpFrameObject> frame) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  OnCompleteFrames(reference_finder_->ManageFrame(std::move(frame)));
}

void RtpVideoStreamReceiver2::ReceivePacket(
    const RtpPacketReceived& rtp_packet) {
  RTC_DCHECK_RUN_ON(worker_queue_);

  if (rtp_packet.payload_size() == 0) {
    // Padding or keep-alive packet.
    // TODO(nisse): Could drop empty packets earlier, but need to figure out how
    // they should be counted in stats.
    std::optional<int64_t> unwrapped_rtp_seq_num =
        UnwrapSequenceNumberOrRecover(rtp_packet, /*video_header=*/nullptr);
    if (unwrapped_rtp_seq_num.has_value()) {
      NotifyReceiverOfEmptyPacket(
          *unwrapped_rtp_seq_num,
          GetCodecFromPayloadType(rtp_packet.PayloadType()));
    }
    return;
  }
  if (rtp_packet.PayloadType() == red_payload_type_) {
    ParseAndHandleEncapsulatingHeader(rtp_packet);
    return;
  }

  const auto type_it = payload_type_map_.find(rtp_packet.PayloadType());
  if (type_it == payload_type_map_.end()) {
    return;
  }

  std::optional<VideoRtpDepacketizer::ParsedRtpPayload> parsed_payload =
      type_it->second->Parse(rtp_packet.PayloadBuffer());
  if (parsed_payload == std::nullopt) {
    RTC_LOG(LS_WARNING) << " Failed to parse payload for "
                        << "ssrc: " << rtp_packet.Ssrc()
                        << ", timestamp: " << rtp_packet.Timestamp();
    return;
  }

  std::optional<int64_t> unwrapped_rtp_seq_num =
      UnwrapSequenceNumberOrRecover(rtp_packet, &parsed_payload->video_header);
  if (!unwrapped_rtp_seq_num.has_value()) {
    return;
  }

  auto packet = std::make_unique<video_coding::PacketBuffer::Packet>(
      rtp_packet, *unwrapped_rtp_seq_num, parsed_payload->video_header);
  packet->video_payload = std::move(parsed_payload->video_payload);
  packet->times_nacked =
      nack_module_ != nullptr
          ? nack_module_->OnReceivedPacket(rtp_packet.SequenceNumber(),
                                           rtp_packet.recovered())
          : -1;

  // When the dependency descriptor is used and the descriptor fail to parse
  // then `OnReceivedPayloadData` may return true to signal the the packet
  // should be retried at a later stage, which is why they are stashed here.
  //
  // TODO(bugs.webrtc.org/15782):
  // This is an ugly solution. The way things should work is for the
  // `RtpFrameReferenceFinder` to stash assembled frames until the keyframe with
  // the relevant template structure has been received, but unfortunately the
  // `frame_transformer_delegate_` is called before the frames are inserted into
  // the `RtpFrameReferenceFinder`, and it expects the dependency descriptor to
  // be parsed at that stage.
  switch (OnReceivedPayloadData(rtp_packet, packet)) {
    case StashResult::kStash:
      // When `OnReceivedPayloadData` tells to stash `packet`, it should stay
      // nonnull.
      RTC_DCHECK(packet != nullptr);
      if (stashed_packets_.size() == 100) {
        stashed_packets_.clear();
      }
      stashed_packets_.push_back(
          {.rtp_packet = rtp_packet, .packet = std::move(packet)});
      break;
    case StashResult::kUnstash:
      for (auto it = stashed_packets_.begin(); it != stashed_packets_.end();) {
        if (OnReceivedPayloadData(it->rtp_packet, it->packet) ==
            StashResult::kStash) {
          // When `OnReceivedPayloadData` tells to keep packet in stash, it
          // should stay nonnull.
          RTC_DCHECK(it->packet != nullptr);
          ++it;
        } else {
          it = stashed_packets_.erase(it);
        }
      }
      break;
    case StashResult::kIgnore:
      break;
  }
}

void RtpVideoStreamReceiver2::ParseAndHandleEncapsulatingHeader(
    const RtpPacketReceived& packet) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  RTC_DCHECK_EQ(packet.PayloadType(), red_payload_type_);

  if (!ulpfec_receiver_ || packet.payload_size() == 0U)
    return;

  if (packet.payload()[0] == ulpfec_receiver_->ulpfec_payload_type()) {
    // Notify video_receiver about received FEC packets to avoid NACKing these
    // packets.
    std::optional<int64_t> unwrapped_rtp_seq_num =
        UnwrapSequenceNumberOrRecover(packet, /*video_header=*/nullptr);
    if (unwrapped_rtp_seq_num.has_value()) {
      NotifyReceiverOfEmptyPacket(
          *unwrapped_rtp_seq_num,
          GetCodecFromPayloadType(packet.PayloadType()));
    }
  }
  if (ulpfec_receiver_->AddReceivedRedPacket(packet)) {
    ulpfec_receiver_->ProcessReceivedFec();
  }
}

// In the case of a video stream without picture ids and no rtx the
// RtpFrameReferenceFinder will need to know about padding to
// correctly calculate frame references.
void RtpVideoStreamReceiver2::NotifyReceiverOfEmptyPacket(
    int64_t seq_number,
    std::optional<VideoCodecType> codec) {
  uint16_t seq_num = static_cast<uint16_t>(seq_number);
  RTC_DCHECK_RUN_ON(worker_queue_);

  OnCompleteFrames(reference_finder_->PaddingReceived(seq_num));

  if (h26x_packet_buffer_ && UseH26xPacketBuffer(codec)) {
    OnInsertedPacket(h26x_packet_buffer_->InsertPadding(seq_num));
  } else {
    OnInsertedPacket(packet_buffer_.InsertPadding(seq_number));
  }
  if (nack_module_) {
    nack_module_->OnReceivedPacket(seq_num, /*is_recovered=*/false);
  }
  if (loss_notification_controller_) {
    // TODO(bugs.webrtc.org/10336): Handle empty packets.
    RTC_LOG(LS_WARNING)
        << "LossNotificationController does not expect empty packets.";
  }
}

bool RtpVideoStreamReceiver2::DeliverRtcp(
    std::span<const uint8_t> rtcp_packet) {
  RTC_DCHECK_RUN_ON(worker_queue_);

  if (!receiving_) {
    return false;
  }

  rtp_rtcp_->IncomingRtcpPacket((rtcp_packet));

  std::optional<TimeDelta> rtt = rtp_rtcp_->LastRtt();
  if (!rtt.has_value()) {
    // Waiting for valid rtt.
    return true;
  }

  std::optional<RtpRtcpInterface::SenderReportStats> last_sr =
      rtp_rtcp_->GetSenderReportStats();
  if (!last_sr.has_value()) {
    // Waiting for RTCP.
    return true;
  }
  int64_t time_since_received = env_.clock().CurrentNtpInMilliseconds() -
                                last_sr->last_arrival_ntp_timestamp.ToMs();
  // Don't use old SRs to estimate time.
  if (time_since_received <= 1) {
    ntp_estimator_.UpdateRtcpTimestamp(*rtt, last_sr->last_remote_ntp_timestamp,
                                       last_sr->last_remote_rtp_timestamp);
    std::optional<int64_t> remote_to_local_clock_offset =
        ntp_estimator_.EstimateRemoteToLocalClockOffset();
    if (remote_to_local_clock_offset.has_value()) {
      capture_clock_offset_updater_.SetRemoteToLocalClockOffset(
          *remote_to_local_clock_offset);
    }
  }

  return true;
}

void RtpVideoStreamReceiver2::FrameContinuous(int64_t picture_id) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  if (!nack_module_)
    return;

  int seq_num = -1;
  auto seq_num_it = last_seq_num_for_pic_id_.find(picture_id);
  if (seq_num_it != last_seq_num_for_pic_id_.end())
    seq_num = seq_num_it->second;
  if (seq_num != -1)
    nack_module_->ClearUpTo(seq_num);
}

void RtpVideoStreamReceiver2::FrameDecoded(int64_t picture_id) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  auto seq_num_it = last_seq_num_for_pic_id_.find(picture_id);
  if (seq_num_it == last_seq_num_for_pic_id_.end()) {
    RTC_LOG(LS_WARNING) << "Frame with id " << picture_id
                        << " not found in frame history maps.";
    return;
  }

  const uint16_t seq_num = seq_num_it->second;
  std::optional<uint32_t> rtp_timestamp;
  auto ts_it = last_timestamp_for_pic_id_.find(picture_id);
  if (ts_it != last_timestamp_for_pic_id_.end()) {
    rtp_timestamp = ts_it->second;
  }

  const int64_t unwrapped_seq_num = rtp_seq_num_unwrapper_.PeekUnwrap(seq_num);
  if (newest_media_seq_num_.has_value() &&
      unwrapped_seq_num > *newest_media_seq_num_) {
    // Delayed feedback from an older sequence-number epoch may unwrap ahead
    // of every received media packet. It must not advance cleanup state.
    RTC_LOG(LS_WARNING) << "Ignoring stale FrameDecoded cleanup: picture_id="
                        << picture_id << " seq=" << seq_num
                        << " unwrapped=" << unwrapped_seq_num
                        << " newest=" << *newest_media_seq_num_;
    last_seq_num_for_pic_id_.erase(seq_num_it);
    if (ts_it != last_timestamp_for_pic_id_.end()) {
      last_timestamp_for_pic_id_.erase(ts_it);
    }
    return;
  }

  last_seq_num_for_pic_id_.erase(last_seq_num_for_pic_id_.begin(),
                                 ++seq_num_it);
  if (ts_it != last_timestamp_for_pic_id_.end()) {
    last_timestamp_for_pic_id_.erase(last_timestamp_for_pic_id_.begin(),
                                     ++ts_it);
  }
  uint32_t num_packets_cleared = packet_buffer_.ClearTo(unwrapped_seq_num);
  if (num_packets_cleared > 0) {
    TRACE_EVENT2("webrtc",
                 "RtpVideoStreamReceiver2::FrameDecoded Cleared Old Packets",
                 "remote_ssrc", config_.rtp.remote_ssrc, "seq_num", seq_num);
    vcm_receive_statistics_->OnDiscardedPackets(num_packets_cleared);
  }
  reference_finder_->ClearTo(seq_num, rtp_timestamp);
}

void RtpVideoStreamReceiver2::SignalNetworkState(NetworkState state) {
  RTC_DCHECK_RUN_ON(worker_queue_);
  rtp_rtcp_->SetRTCPStatus(state == kNetworkUp ? config_.rtp.rtcp_mode
                                               : RtcpMode::kOff);
}

void RtpVideoStreamReceiver2::StartReceive() {
  RTC_DCHECK_RUN_ON(worker_queue_);
  // |h26x_packet_buffer_| is created here instead of in the ctor because we
  // need to know the value of |sps_pps_id_is_h264_keyframe_|.
  bool has_h26x_codec = false;
  for (const auto& [payload_type, codec] : pt_codec_) {
    if (UseH26xPacketBuffer(codec.video_codec)) {
      has_h26x_codec = true;
      break;
    }
  }
  if (!h26x_packet_buffer_ && has_h26x_codec) {
    h26x_packet_buffer_ =
        std::make_unique<H26xPacketBuffer>(!sps_pps_idr_is_h264_keyframe_);
  }
  if (!receiving_ && packet_router_) {
    // Change REMB candidate egibility.
    packet_router_->RemoveReceiveRtpModule(rtp_rtcp_.get());
    packet_router_->AddReceiveRtpModule(rtp_rtcp_.get(),
                                        /*remb_candidate=*/config_.rtp.remb);
  }
  receiving_ = true;
}

void RtpVideoStreamReceiver2::StopReceive() {
  RTC_DCHECK_RUN_ON(worker_queue_);
  if (receiving_ && packet_router_) {
    // Change REMB candidate egibility.
    packet_router_->RemoveReceiveRtpModule(rtp_rtcp_.get());
    packet_router_->AddReceiveRtpModule(rtp_rtcp_.get(),
                                        /*remb_candidate=*/false);
  }
  receiving_ = false;
}

void RtpVideoStreamReceiver2::InsertSpsPpsIntoTracker(uint8_t payload_type) {
  RTC_DCHECK_RUN_ON(worker_queue_);

  auto codec_params_it = pt_codec_params_.find(payload_type);
  if (codec_params_it == pt_codec_params_.end())
    return;

  RTC_LOG(LS_INFO) << "Found out of band supplied codec parameters for"
                      " payload type: "
                   << static_cast<int>(payload_type);

  H264SpropParameterSets sprop_decoder;
  auto sprop_base64_it =
      codec_params_it->second.find(std::string(kH264FmtpSpropParameterSets));

  if (sprop_base64_it == codec_params_it->second.end())
    return;

  if (!sprop_decoder.DecodeSprop(sprop_base64_it->second.c_str()))
    return;

  tracker_.InsertSpsPpsNalus(sprop_decoder.sps_nalu(),
                             sprop_decoder.pps_nalu());

  if (h26x_packet_buffer_ &&
      UseH26xPacketBuffer(GetCodecFromPayloadType(payload_type))) {
    h26x_packet_buffer_->SetSpropParameterSets(sprop_base64_it->second);
  }
}

void RtpVideoStreamReceiver2::UpdatePacketReceiveTimestamps(
    const RtpPacketReceived& packet,
    bool is_keyframe) {
  Timestamp now = env_.clock().CurrentTime();
  if (is_keyframe ||
      last_received_keyframe_rtp_timestamp_ == packet.Timestamp()) {
    last_received_keyframe_rtp_timestamp_ = packet.Timestamp();
    last_received_keyframe_rtp_system_time_ = now;
  }
  last_received_rtp_system_time_ = now;
  last_received_rtp_timestamp_ = packet.Timestamp();

  // Periodically log the RTP header of incoming packets.
  if (now.ms() - last_packet_log_ms_ > kPacketLogIntervalMs) {
    StringBuilder ss;
    ss << "Packet received on SSRC: " << packet.Ssrc()
       << " with payload type: " << static_cast<int>(packet.PayloadType())
       << ", timestamp: " << packet.Timestamp()
       << ", sequence number: " << packet.SequenceNumber()
       << ", arrival time: " << ToString(packet.arrival_time());
    int32_t time_offset;
    if (packet.GetExtension<TransmissionOffset>(&time_offset)) {
      ss << ", toffset: " << time_offset;
    }
    uint32_t send_time;
    if (packet.GetExtension<AbsoluteSendTime>(&send_time)) {
      ss << ", abs send time: " << send_time;
    }
    RTC_LOG(LS_INFO) << ss.str();
    last_packet_log_ms_ = now.ms();
  }
}

}  // namespace webrtc
