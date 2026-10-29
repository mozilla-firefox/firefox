/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/rtp_sender_video.h"

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/inlined_vector.h"
#include "absl/memory/memory.h"
#include "api/crypto/frame_encryptor_interface.h"
#include "api/field_trials_view.h"
#include "api/frame_transformer_interface.h"
#include "api/make_ref_counted.h"
#include "api/media_types.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/transport/rtp/corruption_detection_message.h"
#include "api/transport/rtp/dependency_descriptor.h"
#include "api/units/data_rate.h"
#include "api/units/frequency.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/encoded_image.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_content_type.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_layers_allocation.h"
#include "api/video/video_rotation.h"
#include "api/video/video_timing.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/absolute_capture_time_sender.h"
#include "modules/rtp_rtcp/source/corruption_detection_extension.h"
#include "modules/rtp_rtcp/source/rtp_dependency_descriptor_extension.h"
#include "modules/rtp_rtcp/source/rtp_descriptor_authentication.h"
#include "modules/rtp_rtcp/source/rtp_format.h"
#include "modules/rtp_rtcp/source/rtp_generic_frame_descriptor.h"
#include "modules/rtp_rtcp/source/rtp_generic_frame_descriptor_extension.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "modules/rtp_rtcp/source/rtp_sender_video_frame_transformer_delegate.h"
#include "modules/rtp_rtcp/source/rtp_video_header.h"
#include "modules/rtp_rtcp/source/rtp_video_layers_allocation_extension.h"
#include "modules/rtp_rtcp/source/video_fec_generator.h"
#include "modules/video_coding/codecs/h264/include/h264_globals.h"
#include "modules/video_coding/codecs/interface/common_constants.h"
#include "modules/video_coding/codecs/vp8/include/vp8_globals.h"
#include "modules/video_coding/codecs/vp9/include/vp9_globals.h"
#include "rtc_base/buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/experiments/field_trial_parser.h"
#include "rtc_base/logging.h"
#include "rtc_base/race_checker.h"
#include "rtc_base/synchronization/mutex.h"
#include "system_wrappers/include/ntp_time.h"

namespace webrtc {

namespace {
using PacketizationFormat = RtpPacketizer::PacketizationFormat;

constexpr size_t kRedForFecHeaderLength = 1;
constexpr TimeDelta kMaxUnretransmittableFrameInterval =
    TimeDelta::Millis(33 * 4);
// Minimum interval between repeated warnings.
constexpr TimeDelta kWarningLogInterval = TimeDelta::Seconds(10);
// Largest chain diff that the dependency descriptor can represent, see
// `frame_chain_fdiff` in the dependency descriptor specification.
constexpr int kMaxChainDiff = (1 << 8) - 1;

void BuildRedPayload(const RtpPacketToSend& media_packet,
                     RtpPacketToSend* red_packet) {
  uint8_t* red_payload = red_packet->AllocatePayload(
      kRedForFecHeaderLength + media_packet.payload_size());
  RTC_DCHECK(red_payload);
  red_payload[0] = media_packet.PayloadType();

  auto media_payload = media_packet.payload();
  memcpy(&red_payload[kRedForFecHeaderLength], media_payload.data(),
         media_payload.size());
}

bool MinimizeDescriptor(RTPVideoHeader* video_header) {
  if (auto* vp8 =
          std::get_if<RTPVideoHeaderVP8>(&video_header->video_type_header)) {
    // Set minimum fields the RtpPacketizer is using to create vp8 packets.
    // nonReference is the only field that doesn't require extra space.
    bool non_reference = vp8->nonReference;
    vp8->InitRTPVideoHeaderVP8();
    vp8->nonReference = non_reference;
    return true;
  }
  return false;
}

bool IsBaseLayer(const RTPVideoHeader& video_header) {
  // For AV1 & H.265 we fetch temporal index from the generic descriptor.
  if (video_header.generic) {
    const auto& generic = video_header.generic.value();
    return (generic.temporal_index == 0 ||
            generic.temporal_index == kNoTemporalIdx);
  }
  switch (video_header.codec) {
    case kVideoCodecVP8: {
      const auto& vp8 =
          std::get<RTPVideoHeaderVP8>(video_header.video_type_header);
      return (vp8.temporalIdx == 0 || vp8.temporalIdx == kNoTemporalIdx);
    }
    case kVideoCodecVP9: {
      const auto& vp9 =
          std::get<RTPVideoHeaderVP9>(video_header.video_type_header);
      return (vp9.temporal_idx == 0 || vp9.temporal_idx == kNoTemporalIdx);
    }
    case kVideoCodecH264:
      // TODO(kron): Implement logic for H264 once WebRTC supports temporal
      // layers for H264.
      break;
    // These codecs do not have codec-specifics, from which we can fetch
    // temporal index.
    case kVideoCodecH265:
    case kVideoCodecAV1:
    case kVideoCodecGeneric:
      break;
  }
  return true;
}

std::optional<VideoPlayoutDelay> LoadVideoPlayoutDelayOverride(
    const FieldTrialsView* key_value_config) {
  RTC_DCHECK(key_value_config);
  FieldTrialOptional<int> playout_delay_min_ms("min_ms", std::nullopt);
  FieldTrialOptional<int> playout_delay_max_ms("max_ms", std::nullopt);
  ParseFieldTrial({&playout_delay_max_ms, &playout_delay_min_ms},
                  key_value_config->Lookup("WebRTC-ForceSendPlayoutDelay"));
  return playout_delay_max_ms && playout_delay_min_ms
             ? std::make_optional<VideoPlayoutDelay>(
                   TimeDelta::Millis(*playout_delay_min_ms),
                   TimeDelta::Millis(*playout_delay_max_ms))
             : std::nullopt;
}

// Some packets can be skipped and the stream can still be decoded. Those
// packets are less likely to be retransmitted if they are lost.
bool PacketWillLikelyBeRequestedForRestransmissionIfLost(
    const RTPVideoHeader& video_header) {
  return IsBaseLayer(video_header) &&
         !(video_header.generic.has_value()
               ? absl::c_linear_search(
                     video_header.generic->decode_target_indications,
                     DecodeTargetIndication::kDiscardable)
               : false);
}

}  // namespace

RTPSenderVideo::RTPSenderVideo(const Config& config)
    : rtp_sender_(config.rtp_sender),
      clock_(config.clock),
      retransmission_settings_(
          config.enable_retransmit_all_layers
              ? kRetransmitAllLayers
              : (kRetransmitBaseLayer | kConditionallyRetransmitHigherLayers)),
      last_rotation_(kVideoRotation_0),
      transmit_color_space_next_frame_(false),
      send_allocation_(SendVideoLayersAllocation::kDontSend),
      playout_delay_pending_(false),
      forced_playout_delay_(LoadVideoPlayoutDelayOverride(config.field_trials)),
      red_payload_type_(config.red_payload_type),
      fec_type_(config.fec_type),
      fec_overhead_bytes_(config.fec_overhead_bytes),
      post_encode_overhead_bitrate_(/*max_window_size=*/TimeDelta::Seconds(1)),
      frame_encryptor_(config.frame_encryptor),
      require_frame_encryption_(config.require_frame_encryption),
      sframe_required_by_negotiation_(config.sframe_required_by_negotiation),
      sframe_encryption_config_(config.sframe_encryption_config),
      generic_descriptor_auth_experiment_(
          !config.field_trials->IsDisabled("WebRTC-GenericDescriptorAuth")),
      raw_packetization_(config.raw_packetization),
      absolute_capture_time_sender_(config.clock),
      frame_transformer_delegate_(
          config.frame_transformer
              ? make_ref_counted<RTPSenderVideoFrameTransformerDelegate>(
                    this,
                    config.frame_transformer,
                    rtp_sender_->SSRC(),
                    rtp_sender_->Rid(),
                    config.task_queue_factory,
                    !config.field_trials->IsDisabled(
                        "WebRTC-MediaTaskQueuePriorities")
                        ? TaskQueueFactory::Priority::kVideo
                        : TaskQueueFactory::Priority::kNormal)
              : nullptr) {
  if (frame_transformer_delegate_)
    frame_transformer_delegate_->Init();
}

RTPSenderVideo::~RTPSenderVideo() {
  if (frame_transformer_delegate_)
    frame_transformer_delegate_->Reset();
}

void RTPSenderVideo::LogAndSendToNetwork(
    std::vector<std::unique_ptr<RtpPacketToSend>> packets,
    size_t encoder_output_size) {
  {
    MutexLock lock(&stats_mutex_);
    size_t packetized_payload_size = 0;
    for (const auto& packet : packets) {
      if (*packet->packet_type() == RtpPacketMediaType::kVideo) {
        packetized_payload_size += packet->payload_size();
      }
    }
    // AV1 and H264 packetizers may produce less packetized bytes than
    // unpacketized.
    if (packetized_payload_size >= encoder_output_size) {
      post_encode_overhead_bitrate_.Update(
          packetized_payload_size - encoder_output_size, clock_->CurrentTime());
    }
  }

  rtp_sender_->EnqueuePackets(std::move(packets));
}

size_t RTPSenderVideo::FecPacketOverhead() const {
  size_t overhead = fec_overhead_bytes_;
  if (red_enabled()) {
    // The RED overhead is due to a small header.
    overhead += kRedForFecHeaderLength;

    if (fec_type_ == VideoFecGenerator::FecType::kUlpFec) {
      // For ULPFEC, the overhead is the FEC headers plus RED for FEC header
      // (see above) plus anything in RTP header beyond the 12 bytes base header
      // (CSRC list, extensions...)
      // This reason for the header extensions to be included here is that
      // from an FEC viewpoint, they are part of the payload to be protected.
      // (The base RTP header is already protected by the FEC header.)
      overhead +=
          rtp_sender_->FecOrPaddingPacketMaxRtpHeaderLength() - kRtpHeaderSize;
    }
  }
  return overhead;
}

void RTPSenderVideo::SetRetransmissionSetting(int32_t retransmission_settings) {
  RTC_DCHECK_RUNS_SERIALIZED(&send_checker_);
  retransmission_settings_ = retransmission_settings;
}

void RTPSenderVideo::SetVideoStructure(
    const FrameDependencyStructure* video_structure) {
  if (frame_transformer_delegate_) {
    frame_transformer_delegate_->SetVideoStructureUnderLock(video_structure);
    return;
  }
  SetVideoStructureInternal(video_structure);
}

void RTPSenderVideo::SetVideoStructureAfterTransformation(
    const FrameDependencyStructure* video_structure) {
  SetVideoStructureInternal(video_structure);
}

void RTPSenderVideo::SetVideoStructureInternal(
    const FrameDependencyStructure* video_structure) {
  RTC_DCHECK_RUNS_SERIALIZED(&send_checker_);
  if (video_structure == nullptr) {
    video_structure_ = nullptr;
    return;
  }
  // Simple sanity checks video structure is set up.
  RTC_DCHECK_GT(video_structure->num_decode_targets, 0);
  RTC_DCHECK_GT(video_structure->templates.size(), 0);

  int structure_id = 0;
  if (video_structure_) {
    if (*video_structure_ == *video_structure) {
      // Same structure (just a new key frame), no update required.
      return;
    }
    // When setting different video structure make sure structure_id is updated
    // so that templates from different structures do not collide.
    static constexpr int kMaxTemplates = 64;
    structure_id =
        (video_structure_->structure_id + video_structure_->templates.size()) %
        kMaxTemplates;
  }

  video_structure_ =
      std::make_unique<FrameDependencyStructure>(*video_structure);
  video_structure_->structure_id = structure_id;
}

void RTPSenderVideo::SetVideoLayersAllocation(
    VideoLayersAllocation allocation) {
  if (frame_transformer_delegate_) {
    frame_transformer_delegate_->SetVideoLayersAllocationUnderLock(
        std::move(allocation));
    return;
  }
  SetVideoLayersAllocationInternal(std::move(allocation));
}

void RTPSenderVideo::SetVideoLayersAllocationAfterTransformation(
    VideoLayersAllocation allocation) {
  SetVideoLayersAllocationInternal(std::move(allocation));
}

void RTPSenderVideo::SetVideoLayersAllocationInternal(
    VideoLayersAllocation allocation) {
  RTC_DCHECK_RUNS_SERIALIZED(&send_checker_);
  if (!allocation_ || allocation.active_spatial_layers.size() !=
                          allocation_->active_spatial_layers.size()) {
    send_allocation_ = SendVideoLayersAllocation::kSendWithResolution;
  } else if (send_allocation_ == SendVideoLayersAllocation::kDontSend) {
    send_allocation_ = SendVideoLayersAllocation::kSendWithoutResolution;
  }
  if (send_allocation_ == SendVideoLayersAllocation::kSendWithoutResolution) {
    // Check if frame rate changed more than 5fps since the last time the
    // extension was sent with frame rate and resolution.
    for (size_t i = 0; i < allocation.active_spatial_layers.size(); ++i) {
      if (abs(static_cast<int>(
                  allocation.active_spatial_layers[i].frame_rate_fps) -
              static_cast<int>(
                  last_full_sent_allocation_->active_spatial_layers[i]
                      .frame_rate_fps)) > 5) {
        send_allocation_ = SendVideoLayersAllocation::kSendWithResolution;
        break;
      }
    }
  }
  allocation_ = std::move(allocation);
}

void RTPSenderVideo::AddRtpHeaderExtensions(const RTPVideoHeader& video_header,
                                            bool first_packet,
                                            bool last_packet,
                                            RtpPacketToSend* packet) const {
  // Send color space when changed or if the frame is a key frame. Keep
  // sending color space information until the first base layer frame to
  // guarantee that the information is retrieved by the receiver.
  bool set_color_space =
      video_header.color_space != last_color_space_ ||
      video_header.frame_type == VideoFrameType::kVideoFrameKey ||
      transmit_color_space_next_frame_;
  // Color space requires two-byte header extensions if HDR metadata is
  // included. Therefore, it's best to add this extension first so that the
  // other extensions in the same packet are written as two-byte headers at
  // once.
  if (last_packet && set_color_space && video_header.color_space)
    packet->SetExtension<ColorSpaceExtension>(video_header.color_space.value());

  // According to
  // http://www.etsi.org/deliver/etsi_ts/126100_126199/126114/12.07.00_60/
  // ts_126114v120700p.pdf Section 7.4.5:
  // The MTSI client shall add the payload bytes as defined in this clause
  // onto the last RTP packet in each group of packets which make up a key
  // frame (I-frame or IDR frame in H.264 (AVC), or an IRAP picture in H.265
  // (HEVC)). The MTSI client may also add the payload bytes onto the last RTP
  // packet in each group of packets which make up another type of frame
  // (e.g. a P-Frame) only if the current value is different from the previous
  // value sent.
  // Set rotation when key frame or when changed (to follow standard).
  // Or when different from 0 (to follow current receiver implementation).
  bool set_video_rotation =
      video_header.frame_type == VideoFrameType::kVideoFrameKey ||
      video_header.rotation != last_rotation_ ||
      video_header.rotation != kVideoRotation_0;
  if (last_packet && set_video_rotation)
    packet->SetExtension<VideoOrientation>(video_header.rotation);

  // Report content type only for key frames.
  if (last_packet &&
      video_header.frame_type == VideoFrameType::kVideoFrameKey &&
      video_header.content_type != VideoContentType::UNSPECIFIED)
    packet->SetExtension<VideoContentTypeExtension>(video_header.content_type);

  if (last_packet &&
      video_header.video_timing.flags != VideoSendTiming::kInvalid)
    packet->SetExtension<VideoTimingExtension>(video_header.video_timing);

  // If transmitted, add to all packets; ack logic depends on this.
  if (playout_delay_pending_ && current_playout_delay_.has_value()) {
    packet->SetExtension<PlayoutDelayLimits>(*current_playout_delay_);
  }

  if (first_packet && video_header.absolute_capture_time.has_value()) {
    packet->SetExtension<AbsoluteCaptureTimeExtension>(
        *video_header.absolute_capture_time);
  }

  if (video_header.generic) {
    bool extension_is_set = false;
    if (packet->IsRegistered<RtpDependencyDescriptorExtension>() &&
        video_structure_ != nullptr) {
      DependencyDescriptor descriptor;
      descriptor.first_packet_in_frame = first_packet;
      descriptor.last_packet_in_frame = last_packet;
      descriptor.frame_number = video_header.generic->frame_id & 0xFFFF;
      descriptor.frame_dependencies.spatial_id =
          video_header.generic->spatial_index;
      descriptor.frame_dependencies.temporal_id =
          video_header.generic->temporal_index;
      for (int64_t dep : video_header.generic->dependencies) {
        descriptor.frame_dependencies.frame_diffs.push_back(
            video_header.generic->frame_id - dep);
      }
      descriptor.frame_dependencies.chain_diffs =
          video_header.generic->chain_diffs;
      descriptor.frame_dependencies.decode_target_indications =
          video_header.generic->decode_target_indications;
      RTC_DCHECK_EQ(
          descriptor.frame_dependencies.decode_target_indications.size(),
          video_structure_->num_decode_targets);

      if (first_packet) {
        descriptor.active_decode_targets_bitmask =
            active_decode_targets_tracker_.ActiveDecodeTargetsBitmask();
      }
      // VP9 mark all layer frames of the first picture as kVideoFrameKey,
      // Structure should be attached to the descriptor to lowest spatial layer
      // when inter layer dependency is used, i.e. L structures; or to all
      // layers when inter layer dependency is not used, i.e. S structures.
      // Distinguish these two cases by checking if there are any dependencies.
      if (video_header.frame_type == VideoFrameType::kVideoFrameKey &&
          video_header.generic->dependencies.empty() && first_packet) {
        // To avoid extra structure copy, temporary share ownership of the
        // video_structure with the dependency descriptor.
        descriptor.attached_structure =
            absl::WrapUnique(video_structure_.get());
      }
      extension_is_set = packet->SetExtension<RtpDependencyDescriptorExtension>(
          *video_structure_,
          active_decode_targets_tracker_.ActiveChainsBitmask(), descriptor);

      // Remove the temporary shared ownership.
      descriptor.attached_structure.release();
    }

    // Do not use generic frame descriptor when dependency descriptor is stored.
    if (packet->IsRegistered<RtpGenericFrameDescriptorExtension00>() &&
        !extension_is_set) {
      RtpGenericFrameDescriptor generic_descriptor;
      generic_descriptor.SetFirstPacketInSubFrame(first_packet);
      generic_descriptor.SetLastPacketInSubFrame(last_packet);

      if (first_packet) {
        generic_descriptor.SetFrameId(
            static_cast<uint16_t>(video_header.generic->frame_id));
        for (int64_t dep : video_header.generic->dependencies) {
          // A dependency that the descriptor can't represent is left out. That
          // only matters to receivers that lost the referenced frame, whereas
          // leaving out the descriptor would make the stream inconsistent.
          generic_descriptor.AddFrameDependencyDiff(
              video_header.generic->frame_id - dep);
        }

        uint8_t spatial_bitmask = 1 << video_header.generic->spatial_index;
        generic_descriptor.SetSpatialLayersBitmask(spatial_bitmask);

        generic_descriptor.SetTemporalLayer(
            video_header.generic->temporal_index);

        if (video_header.frame_type == VideoFrameType::kVideoFrameKey) {
          generic_descriptor.SetResolution(video_header.width,
                                           video_header.height);
        }
      }

      packet->SetExtension<RtpGenericFrameDescriptorExtension00>(
          generic_descriptor);
    }
  }

  if (packet->IsRegistered<RtpVideoLayersAllocationExtension>() &&
      first_packet &&
      send_allocation_ != SendVideoLayersAllocation::kDontSend &&
      (video_header.frame_type == VideoFrameType::kVideoFrameKey ||
       PacketWillLikelyBeRequestedForRestransmissionIfLost(video_header))) {
    VideoLayersAllocation allocation = allocation_.value();
    allocation.resolution_and_frame_rate_is_valid =
        send_allocation_ == SendVideoLayersAllocation::kSendWithResolution;
    packet->SetExtension<RtpVideoLayersAllocationExtension>(allocation);
  }

  if (first_packet && video_header.video_frame_tracking_id) {
    packet->SetExtension<VideoFrameTrackingIdExtension>(
        *video_header.video_frame_tracking_id);
  }

  if (last_packet && video_header.frame_instrumentation_data) {
    packet->SetExtension<CorruptionDetectionExtension>(
        CorruptionDetectionMessage::FromFrameInstrumentationData(
            *video_header.frame_instrumentation_data));
  }
}

bool RTPSenderVideo::HandleMissingDependencyDescriptor(
    const RTPVideoHeader& video_header,
    const RtpPacketToSend& packet) {
  if (video_structure_ == nullptr ||
      !packet.IsRegistered<RtpDependencyDescriptorExtension>()) {
    // The dependency descriptor isn't used.
    return true;
  }
  if (video_header.frame_type == VideoFrameType::kVideoFrameKey) {
    // Disable attaching dependency descriptor to delta packets (including
    // non-first packet of a key frame) when it wasn't attached to a key frame,
    // as dependency descriptor can't be usable in such case.
    // This can also happen when the descriptor is larger than 15 bytes and
    // two-byte header extensions are not negotiated using extmap-allow-mixed.
    RTC_LOG(LS_WARNING) << "Disable dependency descriptor because failed to "
                           "attach it to a key frame.";
    video_structure_ = nullptr;
    return true;
  }
  // The dependency descriptor can't describe this delta frame, e.g. because it
  // references a frame too far back. Drop the frame rather than send it
  // without a description of its dependencies, which receivers and middleboxes
  // rely on. Unlike for key frames, keep the dependency descriptor enabled for
  // the frames that follow.
  Timestamp now = clock_->CurrentTime();
  if (now >= last_undescribable_frame_log_ + kWarningLogInterval) {
    RTC_LOG(LS_WARNING) << "Failed to describe frame dependencies. Frame is "
                           "dropped.";
    last_undescribable_frame_log_ = now;
  }
  return false;
}

bool RTPSenderVideo::SendVideo(int payload_type,
                               VideoCodecType codec_type,
                               uint32_t rtp_timestamp,
                               Timestamp capture_time,
                               std::span<const uint8_t> payload,
                               size_t encoder_output_size,
                               RTPVideoHeader video_header,
                               TimeDelta expected_retransmission_time,
                               std::vector<uint32_t> csrcs) {
  return SendVideoFrame(payload_type, codec_type,
                        RtpTimestampWithOffset{rtp_timestamp}, capture_time,
                        payload, encoder_output_size, video_header,
                        expected_retransmission_time, std::move(csrcs));
}

bool RTPSenderVideo::SendVideoFrame(int payload_type,
                                    VideoCodecType codec_type,
                                    RtpTimestampInfo rtp_timestamp_info,
                                    Timestamp capture_time,
                                    std::span<const uint8_t> payload,
                                    size_t encoder_output_size,
                                    RTPVideoHeader video_header,
                                    TimeDelta expected_retransmission_time,
                                    std::vector<uint32_t> csrcs) {
  RTC_CHECK_RUNS_SERIALIZED(&send_checker_);

  uint32_t rtp_timestamp =
      std::holds_alternative<RtpTimestampWithoutOffset>(rtp_timestamp_info)
          ? std::get<RtpTimestampWithoutOffset>(rtp_timestamp_info) +
                rtp_sender_->TimestampOffset()
          : std::get<RtpTimestampWithOffset>(rtp_timestamp_info);

  if (video_header.frame_type == VideoFrameType::kEmptyFrame)
    return true;

  if (payload.empty())
    return false;

  if (!rtp_sender_->SendingMedia()) {
    return false;
  }

  int32_t retransmission_settings = retransmission_settings_;
  if (codec_type == VideoCodecType::kVideoCodecH264) {
    // Backward compatibility for older receivers without temporal layer logic.
    retransmission_settings = kRetransmitBaseLayer | kRetransmitHigherLayers;
  }
  const uint8_t temporal_id = GetTemporalId(video_header);
  // TODO(bugs.webrtc.org/10714): retransmission_settings_ should generally be
  // replaced by expected_retransmission_time.IsFinite().
  const bool allow_retransmission =
      expected_retransmission_time.IsFinite() &&
      AllowRetransmission(temporal_id, retransmission_settings,
                          expected_retransmission_time);

  MaybeUpdateCurrentPlayoutDelay(video_header);
  if (video_header.frame_type == VideoFrameType::kVideoFrameKey) {
    if (current_playout_delay_.has_value()) {
      // Force playout delay on key-frames, if set.
      playout_delay_pending_ = true;
    }
    if (allocation_) {
      // Send the bitrate allocation on every key frame.
      send_allocation_ = SendVideoLayersAllocation::kSendWithResolution;
    }
  }

  if (video_structure_ != nullptr && video_header.generic) {
    active_decode_targets_tracker_.OnFrame(
        video_structure_->decode_target_protected_by_chain,
        video_header.generic->active_decode_targets,
        video_header.frame_type == VideoFrameType::kVideoFrameKey,
        video_header.generic->frame_id, video_header.generic->chain_diffs);
    // Chain diffs of inactive chains aren't serialized, so they may be
    // arbitrarily large, e.g. when a spatial layer isn't being produced.
    const std::bitset<32> active_chains =
        active_decode_targets_tracker_.ActiveChainsBitmask();
    absl::InlinedVector<int, 4>& chain_diffs =
        video_header.generic->chain_diffs;
    for (size_t i = 0; i < chain_diffs.size() && i < active_chains.size();
         ++i) {
      int& chain_diff = chain_diffs[i];
      if (!active_chains[i] || chain_diff <= kMaxChainDiff) {
        continue;
      }
      // A chain diff that the dependency descriptor can't represent would
      // make the frame undescribable. Instead, describe the chain as having
      // no previous frame, like RtpPayloadParams does for VP9. That can lead
      // to video corruption if a previous frame of the chain was lost.
      Timestamp now = clock_->CurrentTime();
      if (now >= last_chain_diff_log_ + kWarningLogInterval) {
        RTC_LOG(LS_WARNING) << "Chain diff " << chain_diff
                            << " is too large for the dependency descriptor. "
                               "Sending 0 instead.";
        last_chain_diff_log_ = now;
      }
      chain_diff = 0;
    }
  }

  // No FEC protection for upper temporal layers, if used.
  const bool use_fec = fec_type_.has_value() &&
                       (temporal_id == 0 || temporal_id == kNoTemporalIdx);

  std::unique_ptr<RtpPacketToSend> single_packet =
      rtp_sender_->AllocatePacket(csrcs);

  // Maximum size of packet including rtp headers. Capped by the capacity of the
  // allocated packet so that the packetizer never exceeds the buffer, even if
  // the max packet size is changed concurrently.
  // Extra space left in case packet will be resent using fec or rtx.
  int packet_capacity =
      std::min(single_packet->capacity(), rtp_sender_->MaxRtpPacketSize());
  if (use_fec) {
    packet_capacity -= FecPacketOverhead();
  }
  if (allow_retransmission) {
    packet_capacity -= rtp_sender_->RtxPacketOverhead();
  }

  single_packet->SetPayloadType(payload_type);
  single_packet->SetTimestamp(rtp_timestamp);
  if (capture_time.IsFinite())
    single_packet->set_capture_time(capture_time);

  // Construct the absolute capture time extension if not provided.
  if (!video_header.absolute_capture_time.has_value() &&
      capture_time.IsFinite()) {
    video_header.absolute_capture_time.emplace();
    video_header.absolute_capture_time->absolute_capture_timestamp =
        Int64MsToUQ32x32(
            clock_->ConvertTimestampToNtpTime(capture_time).ToMs());
    video_header.absolute_capture_time->estimated_capture_clock_offset = 0;
  }

  // Let `absolute_capture_time_sender_` decide if the extension should be sent.
  if (video_header.absolute_capture_time.has_value()) {
    video_header.absolute_capture_time =
        absolute_capture_time_sender_.OnSendPacket(
            AbsoluteCaptureTimeSender::GetSource(single_packet->Ssrc(), csrcs),
            single_packet->Timestamp(), kVideoPayloadTypeFrequency,
            NtpTime(
                video_header.absolute_capture_time->absolute_capture_timestamp),
            video_header.absolute_capture_time->estimated_capture_clock_offset);
  }

  auto first_packet = std::make_unique<RtpPacketToSend>(*single_packet);
  auto middle_packet = std::make_unique<RtpPacketToSend>(*single_packet);
  auto last_packet = std::make_unique<RtpPacketToSend>(*single_packet);
  // Simplest way to estimate how much extensions would occupy is to set them.
  AddRtpHeaderExtensions(video_header,
                         /*first_packet=*/true, /*last_packet=*/true,
                         single_packet.get());
  if (!single_packet->HasExtension<RtpDependencyDescriptorExtension>() &&
      !HandleMissingDependencyDescriptor(video_header, *single_packet)) {
    return false;
  }

  AddRtpHeaderExtensions(video_header,
                         /*first_packet=*/true, /*last_packet=*/false,
                         first_packet.get());
  AddRtpHeaderExtensions(video_header,
                         /*first_packet=*/false, /*last_packet=*/false,
                         middle_packet.get());
  AddRtpHeaderExtensions(video_header,
                         /*first_packet=*/false, /*last_packet=*/true,
                         last_packet.get());

  RTC_DCHECK_GT(packet_capacity, single_packet->headers_size());
  RTC_DCHECK_GT(packet_capacity, first_packet->headers_size());
  RTC_DCHECK_GT(packet_capacity, middle_packet->headers_size());
  RTC_DCHECK_GT(packet_capacity, last_packet->headers_size());
  RtpPacketizer::PayloadSizeLimits limits;
  limits.max_payload_len = packet_capacity - middle_packet->headers_size();

  RTC_DCHECK_GE(single_packet->headers_size(), middle_packet->headers_size());
  limits.single_packet_reduction_len =
      single_packet->headers_size() > middle_packet->headers_size()
          ? single_packet->headers_size() - middle_packet->headers_size()
          : 0;

  RTC_DCHECK_GE(first_packet->headers_size(), middle_packet->headers_size());
  limits.first_packet_reduction_len =
      first_packet->headers_size() > middle_packet->headers_size()
          ? first_packet->headers_size() - middle_packet->headers_size()
          : 0;

  RTC_DCHECK_GE(last_packet->headers_size(), middle_packet->headers_size());
  limits.last_packet_reduction_len =
      last_packet->headers_size() > middle_packet->headers_size()
          ? last_packet->headers_size() - middle_packet->headers_size()
          : 0;

  bool has_generic_descriptor =
      first_packet->HasExtension<RtpGenericFrameDescriptorExtension00>() ||
      first_packet->HasExtension<RtpDependencyDescriptorExtension>();

  // Minimization of the vp8 descriptor may erase temporal_id, so use
  // `temporal_id` rather than reference `video_header` beyond this point.
  if (has_generic_descriptor) {
    MinimizeDescriptor(&video_header);
  }

  Buffer encrypted_video_payload;
  if (frame_encryptor_ != nullptr) {
    const size_t max_ciphertext_size =
        frame_encryptor_->GetMaxCiphertextByteSize(MediaType::VIDEO,
                                                   payload.size());
    encrypted_video_payload.SetSize(max_ciphertext_size);

    size_t bytes_written = 0;

    // Enable header authentication if the field trial isn't disabled.
    std::vector<uint8_t> additional_data;
    if (generic_descriptor_auth_experiment_) {
      additional_data = RtpDescriptorAuthentication(video_header);
    }

    if (frame_encryptor_->Encrypt(
            MediaType::VIDEO, first_packet->Ssrc(), additional_data, payload,
            encrypted_video_payload, &bytes_written) != 0) {
      return false;
    }

    encrypted_video_payload.SetSize(bytes_written);
    payload = encrypted_video_payload;
  } else if (require_frame_encryption_) {
    RTC_LOG(LS_WARNING)
        << "No FrameEncryptor is attached to this video sending stream but "
           "one is required since require_frame_encryptor is set";
  }

  std::unique_ptr<RtpPacketizer> packetizer = RtpPacketizer::Create(
      GetPacketizationFormat(codec_type), payload, limits, video_header);

  const size_t num_packets = packetizer->NumPackets();

  if (num_packets == 0) {
    Timestamp now = clock_->CurrentTime();
    if (now >= last_fail_packetize_log_ + kWarningLogInterval) {
      RTC_LOG(LS_WARNING) << "Failed to packetize " << codec_type
                          << " video frame of size " << payload.size()
                          << ". Frame is dropped.";
      last_fail_packetize_log_ = now;
    }
    return false;
  }

  bool first_frame = first_frame_sent_();
  std::vector<std::unique_ptr<RtpPacketToSend>> rtp_packets;
  for (size_t i = 0; i < num_packets; ++i) {
    std::unique_ptr<RtpPacketToSend> packet;
    int expected_payload_capacity;
    // Choose right packet template:
    if (num_packets == 1) {
      packet = std::move(single_packet);
      expected_payload_capacity =
          limits.max_payload_len - limits.single_packet_reduction_len;
    } else if (i == 0) {
      packet = std::move(first_packet);
      expected_payload_capacity =
          limits.max_payload_len - limits.first_packet_reduction_len;
    } else if (i == num_packets - 1) {
      packet = std::move(last_packet);
      expected_payload_capacity =
          limits.max_payload_len - limits.last_packet_reduction_len;
    } else {
      packet = std::make_unique<RtpPacketToSend>(*middle_packet);
      expected_payload_capacity = limits.max_payload_len;
    }

    packet->set_first_packet_of_frame(i == 0);

    if (!packetizer->NextPacket(packet.get()))
      return false;
    RTC_DCHECK_LE(packet->payload_size(), expected_payload_capacity);

    packet->set_allow_retransmission(allow_retransmission);
    packet->set_is_key_frame(video_header.frame_type ==
                             VideoFrameType::kVideoFrameKey);

    // Put packetization finish timestamp into extension.
    if (packet->HasExtension<VideoTimingExtension>()) {
      packet->set_packetization_finish_time(clock_->CurrentTime());
    }

    packet->set_fec_protect_packet(use_fec);

    if (red_enabled()) {
      // TODO(sprang): Consider packetizing directly into packets with the RED
      // header already in place, to avoid this copy.
      std::unique_ptr<RtpPacketToSend> red_packet(new RtpPacketToSend(*packet));
      BuildRedPayload(*packet, red_packet.get());
      red_packet->SetPayloadType(*red_payload_type_);
      red_packet->set_is_red(true);

      // Append `red_packet` instead of `packet` to output.
      red_packet->set_packet_type(RtpPacketMediaType::kVideo);
      red_packet->set_allow_retransmission(packet->allow_retransmission());
      rtp_packets.emplace_back(std::move(red_packet));
    } else {
      packet->set_packet_type(RtpPacketMediaType::kVideo);
      rtp_packets.emplace_back(std::move(packet));
    }

    if (first_frame) {
      if (i == 0) {
        RTC_LOG(LS_INFO)
            << "Sent first RTP packet of the first video frame (pre-pacer)";
      }
      if (i == num_packets - 1) {
        RTC_LOG(LS_INFO)
            << "Sent last RTP packet of the first video frame (pre-pacer)";
      }
    }
  }

  LogAndSendToNetwork(std::move(rtp_packets), encoder_output_size);

  // Update details about the last sent frame.
  last_rotation_ = video_header.rotation;

  if (video_header.color_space != last_color_space_) {
    last_color_space_ = video_header.color_space;
    transmit_color_space_next_frame_ = !IsBaseLayer(video_header);
  } else {
    transmit_color_space_next_frame_ =
        transmit_color_space_next_frame_ ? !IsBaseLayer(video_header) : false;
  }

  if (video_header.frame_type == VideoFrameType::kVideoFrameKey ||
      PacketWillLikelyBeRequestedForRestransmissionIfLost(video_header)) {
    // This frame will likely be delivered, no need to populate playout
    // delay extensions until it changes again.
    playout_delay_pending_ = false;
    if (send_allocation_ == SendVideoLayersAllocation::kSendWithResolution) {
      last_full_sent_allocation_ = allocation_;
    }
    send_allocation_ = SendVideoLayersAllocation::kDontSend;
  }

  return true;
}

bool RTPSenderVideo::SendEncodedImage(int payload_type,
                                      VideoCodecType codec_type,
                                      uint32_t rtp_timestamp,
                                      const EncodedImage& encoded_image,
                                      RTPVideoHeader video_header,
                                      TimeDelta expected_retransmission_time,
                                      const std::vector<uint32_t>& csrcs) {
  if (sframe_required_by_negotiation_) {
    // TODO(bugs.webrtc.org/479862368): Enable the encryptor check once frames
    // are encrypted; until then dropping unconditionally keeps media off the
    // wire.
    RTC_LOG(LS_WARNING) << "Dropping video frame: Sframe is required but no "
                           "Sframe encryptor is set up for this stream.";
    return false;
  }

  if (frame_transformer_delegate_) {
    // The frame will be sent async once transformed.
    return frame_transformer_delegate_->TransformFrame(
        payload_type, codec_type, rtp_timestamp, encoded_image, video_header,
        expected_retransmission_time, csrcs);
  }
  return SendVideoFrame(payload_type, codec_type,
                        RtpTimestampInfo(RtpTimestampWithOffset{rtp_timestamp}),
                        encoded_image.CaptureTime(), encoded_image,
                        encoded_image.size(), video_header,
                        expected_retransmission_time, csrcs);
}

DataRate RTPSenderVideo::PostEncodeOverhead() const {
  MutexLock lock(&stats_mutex_);
  return post_encode_overhead_bitrate_.Rate(clock_->CurrentTime())
      .value_or(DataRate::Zero());
}

PacketizationFormat RTPSenderVideo::GetPacketizationFormat(
    VideoCodecType codec_type) const {
  if (raw_packetization_) {
    return PacketizationFormat::kRaw;
  }

  switch (codec_type) {
    case kVideoCodecH264:
      return PacketizationFormat::kH264;
    case kVideoCodecVP8:
      return PacketizationFormat::kVP8;
    case kVideoCodecVP9:
      return PacketizationFormat::kVP9;
    case kVideoCodecAV1:
      return PacketizationFormat::kAV1;
    case kVideoCodecH265:
      return PacketizationFormat::kH265;
    case kVideoCodecGeneric:
      return PacketizationFormat::kGeneric;
  }
}

bool RTPSenderVideo::AllowRetransmission(
    uint8_t temporal_id,
    int32_t retransmission_settings,
    TimeDelta expected_retransmission_time) {
  if (retransmission_settings == kRetransmitOff)
    return false;

  MutexLock lock(&stats_mutex_);
  // Media packet storage.
  if ((retransmission_settings & kConditionallyRetransmitHigherLayers) &&
      UpdateConditionalRetransmit(temporal_id, expected_retransmission_time)) {
    retransmission_settings |= kRetransmitHigherLayers;
  }

  if (temporal_id == kNoTemporalIdx)
    return true;

  if ((retransmission_settings & kRetransmitBaseLayer) && temporal_id == 0)
    return true;

  if ((retransmission_settings & kRetransmitHigherLayers) && temporal_id > 0)
    return true;

  return false;
}

uint8_t RTPSenderVideo::GetTemporalId(const RTPVideoHeader& header) {
  struct TemporalIdGetter {
    uint8_t operator()(const RTPVideoHeaderVP8& vp8) { return vp8.temporalIdx; }
    uint8_t operator()(const RTPVideoHeaderVP9& vp9) {
      return vp9.temporal_idx;
    }
    uint8_t operator()(const RTPVideoHeaderH264&) { return kNoTemporalIdx; }
    uint8_t operator()(const RTPVideoHeaderLegacyGeneric&) {
      return kNoTemporalIdx;
    }
    uint8_t operator()(const std::monostate&) { return kNoTemporalIdx; }
  };
  return std::visit(TemporalIdGetter(), header.video_type_header);
}

bool RTPSenderVideo::UpdateConditionalRetransmit(
    uint8_t temporal_id,
    TimeDelta expected_retransmission_time) {
  Timestamp now = clock_->CurrentTime();
  // Update stats for any temporal layer.
  TemporalLayerStats* current_layer_stats =
      &frame_stats_by_temporal_layer_[temporal_id];
  current_layer_stats->frame_rate.Update(now);
  TimeDelta tl_frame_interval = now - current_layer_stats->last_frame_time;
  current_layer_stats->last_frame_time = now;

  // Conditional retransmit only applies to upper layers.
  if (temporal_id != kNoTemporalIdx && temporal_id > 0) {
    if (tl_frame_interval >= kMaxUnretransmittableFrameInterval) {
      // Too long since a retransmittable frame in this layer, enable NACK
      // protection.
      return true;
    } else {
      // Estimate when the next frame of any lower layer will be sent.
      Timestamp expected_next_frame_time = Timestamp::PlusInfinity();
      for (int i = temporal_id - 1; i >= 0; --i) {
        TemporalLayerStats* stats = &frame_stats_by_temporal_layer_[i];
        std::optional<Frequency> rate = stats->frame_rate.Rate(now);
        if (rate > Frequency::Zero()) {
          Timestamp tl_next = stats->last_frame_time + 1 / *rate;
          if (tl_next - now > -expected_retransmission_time &&
              tl_next < expected_next_frame_time) {
            expected_next_frame_time = tl_next;
          }
        }
      }

      if (expected_next_frame_time - now > expected_retransmission_time) {
        // The next frame in a lower layer is expected at a later time (or
        // unable to tell due to lack of data) than a retransmission is
        // estimated to be able to arrive, so allow this packet to be nacked.
        return true;
      }
    }
  }

  return false;
}

void RTPSenderVideo::MaybeUpdateCurrentPlayoutDelay(
    const RTPVideoHeader& header) {
  std::optional<VideoPlayoutDelay> requested_delay =
      forced_playout_delay_.has_value() ? forced_playout_delay_
                                        : header.playout_delay;

  if (!requested_delay.has_value()) {
    return;
  }

  current_playout_delay_ = requested_delay;
  playout_delay_pending_ = true;
}

}  // namespace webrtc
