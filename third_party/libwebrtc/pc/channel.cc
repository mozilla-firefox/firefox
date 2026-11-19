/*
 *  Copyright 2004 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/channel.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/cleanup/cleanup.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/crypto/crypto_options.h"
#include "api/jsep.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "api/rtp_headers.h"
#include "api/rtp_parameters.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "call/rtp_demuxer.h"
#include "media/base/codec.h"
#include "media/base/media_channel.h"
#include "media/base/rid_description.h"
#include "media/base/rtp_utils.h"
#include "media/base/stream_params.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "pc/channel_interface.h"
#include "pc/rtp_media_utils.h"
#include "pc/rtp_transport_internal.h"
#include "pc/session_description.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/checks.h"
#include "rtc_base/containers/flat_set.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/logging.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/socket.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/thread.h"
#include "rtc_base/trace_event.h"
#include "rtc_base/unique_id_generator.h"

namespace webrtc {
namespace {

// Finds a stream based on target's Primary SSRC or RIDs.
// This struct is used in BaseChannel::UpdateLocalStreams_n.
struct StreamFinder {
  explicit StreamFinder(const StreamParams* target) : target_(target) {
    RTC_DCHECK(target);
  }

  bool operator()(const StreamParams& sp) const {
    if (target_->has_ssrcs() && sp.has_ssrcs()) {
      return sp.has_ssrc(target_->first_ssrc());
    }

    if (!target_->has_rids() && !sp.has_rids()) {
      return false;
    }

    const std::vector<RidDescription>& target_rids = target_->rids();
    const std::vector<RidDescription>& source_rids = sp.rids();
    if (source_rids.size() != target_rids.size()) {
      return false;
    }

    // Check that all RIDs match.
    return std::equal(source_rids.begin(), source_rids.end(),
                      target_rids.begin(),
                      [](const RidDescription& lhs, const RidDescription& rhs) {
                        return lhs.rid == rhs.rid;
                      });
  }

  const StreamParams* target_;
};

void MediaChannelParametersFromMediaDescription(
    const MediaContentDescription* desc,
    const RtpHeaderExtensions& extensions,
    bool is_stream_active,
    MediaChannelParameters* params) {
  RTC_DCHECK(desc->type() == MediaType::AUDIO ||
             desc->type() == MediaType::VIDEO);
  params->is_stream_active = is_stream_active;
  params->codecs = desc->codecs();
  params->extensions = extensions;
  params->rtcp.reduced_size = desc->rtcp_reduced_size();
  params->rtcp.remote_estimate = desc->remote_estimate();
  params->rtcp_cc_ack_type = desc->preferred_rtcp_cc_ack_type();
}

void RtpSendParametersFromMediaDescription(
    const MediaContentDescription* desc,
    RtpExtension::Filter extensions_filter,
    SenderParameters* send_params) {
  RtpHeaderExtensions extensions = RtpExtension::DeduplicateHeaderExtensions(
      desc->rtp_header_extensions(), extensions_filter);
  const bool is_stream_active =
      RtpTransceiverDirectionHasRecv(desc->direction());
  MediaChannelParametersFromMediaDescription(desc, extensions, is_stream_active,
                                             send_params);
  send_params->max_bandwidth_bps = desc->bandwidth();
  send_params->extmap_allow_mixed = desc->extmap_allow_mixed();
}

// Ensure that there is a matching packetization for each codec in
// new_params. If old_params had a special packetization but the
// response in new_params has no special packetization we amend
// old_params by ignoring the packetization and fall back to standard
// packetization instead.
RTCError MaybeIgnorePacketization(const MediaChannelParameters& new_params,
                                  MediaChannelParameters& old_params) {
  flat_set<const Codec*> matched_codecs;
  for (Codec& codec : old_params.codecs) {
    if (absl::c_any_of(matched_codecs,
                       [&](const Codec* c) { return codec.Matches(*c); })) {
      continue;
    }

    std::vector<const Codec*> new_codecs =
        FindAllMatchingCodecs(new_params.codecs, codec);
    if (new_codecs.empty()) {
      continue;
    }

    bool may_ignore_packetization = false;
    bool has_matching_packetization = false;
    for (const Codec* new_codec : new_codecs) {
      if (!new_codec->packetization.has_value() &&
          codec.packetization.has_value()) {
        may_ignore_packetization = true;
      } else if (new_codec->packetization == codec.packetization) {
        has_matching_packetization = true;
        break;
      }
    }

    if (may_ignore_packetization) {
      // Note: this writes into old_params
      codec.packetization = std::nullopt;
    } else if (!has_matching_packetization) {
      return RTCError(RTCErrorType::INTERNAL_ERROR)
             << "Failed to set local answer due to incompatible codec "
             << "packetization for pt='" << codec.id << "' specified.";
    }

    if (has_matching_packetization) {
      matched_codecs.insert(&codec);
    }
  }
  return RTCError::OK();
}

}  // namespace

BaseChannel::BaseChannel(
    Thread* absl_nonnull network_thread,
    TaskQueueBase* absl_nonnull signaling_thread,
    std::unique_ptr<MediaSendChannelInterface> send_media_channel_impl,
    std::unique_ptr<MediaReceiveChannelInterface> receive_media_channel_impl,
    absl::string_view mid,
    MediaType media_type,
    bool srtp_required,
    CryptoOptions crypto_options,
    UniqueRandomIdGenerator* absl_nonnull ssrc_generator,
    ChannelCallbacks callbacks)
    : media_send_channel_(std::move(send_media_channel_impl)),
      media_receive_channel_(std::move(receive_media_channel_impl)),
      network_thread_(network_thread),
      signaling_thread_(signaling_thread),
      alive_(PendingTaskSafetyFlag::CreateAttachedToTaskQueue(true,
                                                              network_thread)),
      on_first_packet_sent_(std::move(callbacks.on_first_packet_sent)),
      on_packet_received_n_(std::move(callbacks.on_packet_received)),
      srtp_required_(srtp_required),
      extensions_filter_(
          crypto_options.srtp.enable_encrypted_rtp_header_extensions
              ? RtpExtension::kPreferEncryptedExtension
              : RtpExtension::kDiscardEncryptedExtension),
      mid_(std::string(mid)),
      last_recv_params_(media_type == MediaType::VIDEO
                            ? ReceiverParamsVariant(VideoReceiverParameters())
                            : ReceiverParamsVariant(AudioReceiverParameters())),
      last_send_params_(media_type == MediaType::VIDEO
                            ? SenderParamsVariant(VideoSenderParameters())
                            : SenderParamsVariant(AudioSenderParameter())),
      media_type_(media_type),
      ssrc_generator_(ssrc_generator) {
  RTC_DCHECK(media_send_channel_);
  RTC_DCHECK(media_receive_channel_);
  RTC_DCHECK(ssrc_generator_);
  RTC_DLOG(LS_INFO) << "Created channel: " << ToString();
}

BaseChannel::~BaseChannel() {
  TRACE_EVENT0("webrtc", "BaseChannel::~BaseChannel");
  RTC_DCHECK_RUN_ON(network_thread_);

  DisableMedia_n();

  // Eats any outstanding messages or packets.
  alive_->SetNotAlive();
  // The channel must be disconnected from the transport before destruction.
  RTC_DCHECK(rtp_transport_ == nullptr);
}

std::string BaseChannel::ToString() const {
  return (StringBuilder() << "{mid: " << mid() << ", media_type: "
                          << media_send_channel_->media_type() << "}")
      .Release();
}

bool BaseChannel::ConnectToRtpTransport_n(RtpTransportInternal* rtp_transport) {
  RTC_DCHECK(!rtp_transport_);
  RTC_DCHECK(rtp_transport);
  RTC_DCHECK(media_send_channel());

  // We don't need to call OnDemuxerCriteriaUpdatePending/Complete because
  // there's no previous criteria to worry about.
  RtpDemuxerCriteria criteria = demuxer_criteria();
  if (!rtp_transport->RegisterRtpDemuxerSink(criteria, this)) {
    return false;
  }
  rtp_transport_ = rtp_transport;
  rtp_transport_->SubscribeReadyToSend(
      this, [this](bool ready) { OnTransportReadyToSend(ready); });
  rtp_transport_->SubscribeWritableState(
      this, [this](bool state) { OnWritableState(state); });
  rtp_transport_->SubscribeSentPacket(
      this,
      [this](const SentPacketInfo& packet) { SignalSentPacket_n(packet); });
  return true;
}

void BaseChannel::DisconnectFromRtpTransport_n(bool permanent_teardown) {
  RTC_DCHECK(rtp_transport_);
  RTC_DCHECK(media_send_channel());
  rtp_transport_->UnregisterRtpDemuxerSink(this);
  rtp_transport_->UnsubscribeReadyToSend(this);
  rtp_transport_->UnsubscribeWritableState(this);
  rtp_transport_->UnsubscribeSentPacket(this);
  rtp_transport_ = nullptr;
  media_send_channel()->SetInterface(nullptr);
  if (permanent_teardown) {
    media_receive_channel()->ClearReceiveSinks_n(std::nullopt);
  }
  media_receive_channel()->SetInterface(nullptr);
}

bool BaseChannel::SetRtpTransport(RtpTransportInternal* rtp_transport) {
  TRACE_EVENT0("webrtc", "BaseChannel::SetRtpTransport");
  RTC_DCHECK_RUN_ON(network_thread_);
  if (rtp_transport == rtp_transport_) {
    return true;
  }

  if (rtp_transport_) {
    DisconnectFromRtpTransport_n(rtp_transport == nullptr);
  }

  RTC_DCHECK(!rtp_transport_);

  if (!rtp_transport) {
    return true;  // We're done.
  }

  if (!ConnectToRtpTransport_n(rtp_transport)) {
    RTC_DCHECK(!rtp_transport_);
    return false;
  }

  RTC_DCHECK_EQ(rtp_transport_, rtp_transport);

  RTC_DCHECK(!media_send_channel()->HasNetworkInterface());
  media_send_channel()->SetInterface(this);
  media_receive_channel()->SetInterface(this);

  media_send_channel()->OnReadyToSend(rtp_transport_->IsReadyToSend());
  UpdateWritableState_n();

  // Set the cached socket options.
  for (const auto& pair : socket_options_) {
    rtp_transport_->SetRtpOption(pair.first, pair.second);
  }
  if (!rtp_transport_->rtcp_mux_enabled()) {
    for (const auto& pair : rtcp_socket_options_) {
      rtp_transport_->SetRtcpOption(pair.first, pair.second);
    }
  }

  return true;
}

void BaseChannel::Enable(bool enable) {
  RTC_DCHECK_RUN_ON(signaling_thread());

  if (enable == enabled_s_)
    return;

  enabled_s_ = enable;

  network_thread_->PostTask(SafeTask(alive_, [this, enable] {
    RTC_DCHECK_RUN_ON(network_thread_);
    // Sanity check to make sure that enabled_ and enabled_s_
    // stay in sync.
    RTC_DCHECK_NE(enabled_, enable);
    if (enable) {
      EnableMedia_n();
    } else {
      DisableMedia_n();
    }
  }));
}

void BaseChannel::EnableSframe() {
  RTC_DCHECK_RUN_ON(signaling_thread());

  network_thread_->PostTask(SafeTask(alive_, [this] {
    RTC_DCHECK_RUN_ON(network_thread_);
    media_send_channel()->EnableSframe();
    media_receive_channel()->EnableSframe();
  }));
}

bool BaseChannel::IsReadyToSendMedia_n() const {
  // Send outgoing data if we are enabled, have local and remote content,
  // and we have had some form of connectivity.
  return enabled_ &&
         RtpTransceiverDirectionHasRecv(remote_content_direction_) &&
         RtpTransceiverDirectionHasSend(local_content_direction_) &&
         was_ever_writable_;
}

bool BaseChannel::SendPacket(CopyOnWriteBuffer* packet,
                             const AsyncSocketPacketOptions& options) {
  return SendPacket(false, packet, options);
}

bool BaseChannel::SendRtcp(CopyOnWriteBuffer* packet,
                           const AsyncSocketPacketOptions& options) {
  return SendPacket(true, packet, options);
}

int BaseChannel::SetOption(SocketType type, Socket::Option opt, int value) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK(network_initialized());
  RTC_DCHECK(rtp_transport_);
  switch (type) {
    case ST_RTP:
      socket_options_.push_back(std::pair<Socket::Option, int>(opt, value));
      return rtp_transport_->SetRtpOption(opt, value);
    case ST_RTCP:
      rtcp_socket_options_.push_back(
          std::pair<Socket::Option, int>(opt, value));
      return rtp_transport_->SetRtcpOption(opt, value);
  }
  return -1;
}

void BaseChannel::OnWritableState(bool writable) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK(network_initialized());
  if (writable) {
    ChannelWritable_n();
  } else {
    ChannelNotWritable_n();
  }
}

void BaseChannel::OnTransportReadyToSend(bool ready) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK(network_initialized());
  media_send_channel()->OnReadyToSend(ready);
}

bool BaseChannel::SendPacket(bool rtcp,
                             CopyOnWriteBuffer* packet,
                             const AsyncSocketPacketOptions& options) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK(network_initialized());
  TRACE_EVENT0("webrtc", "BaseChannel::SendPacket");

  // Until all the code is migrated to use RtpPacketType instead of bool.
  RtpPacketType packet_type = rtcp ? RtpPacketType::kRtcp : RtpPacketType::kRtp;

  // Ensure we have a place to send this packet before doing anything. We might
  // get RTCP packets that we don't intend to send. If we've negotiated RTCP
  // mux, send RTCP over the RTP transport.
  if (!rtp_transport_ || !rtp_transport_->IsWritable(rtcp)) {
    return false;
  }

  // Protect ourselves against crazy data.
  if (!IsValidRtpPacketSize(packet_type, packet->size())) {
    RTC_LOG(LS_ERROR) << "Dropping outgoing " << ToString() << " "
                      << RtpPacketTypeToString(packet_type)
                      << " packet: wrong size=" << packet->size();
    return false;
  }

  if (!srtp_active()) {
    if (srtp_required_) {
      // The audio/video engines may attempt to send RTCP packets as soon as the
      // streams are created, so don't treat this as an error for RTCP.
      // See: https://bugs.chromium.org/p/webrtc/issues/detail?id=6809
      // However, there shouldn't be any RTP packets sent before SRTP is set
      // up (and SetSend(true) is called).
      RTC_DCHECK(rtcp) << "Can't send outgoing RTP packet for " << ToString()
                       << " when SRTP is inactive and crypto is required";
      return false;
    }

    RTC_DLOG(LS_WARNING) << "Sending an " << (rtcp ? "RTCP" : "RTP")
                         << " packet without encryption for " << ToString()
                         << ".";
  }

  if (on_first_packet_sent_ && options.info_signaled_after_sent.is_media) {
    std::move(on_first_packet_sent_)();
    on_first_packet_sent_ = nullptr;
  }

  return rtcp ? rtp_transport_->SendRtcpPacket(packet, options, PF_SRTP_BYPASS)
              : rtp_transport_->SendRtpPacket(packet, options, PF_SRTP_BYPASS);
}

void BaseChannel::OnRtpPacket(const RtpPacketReceived& parsed_packet) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK(network_initialized());

  if (!srtp_active() && srtp_required_) {
    // Our session description indicates that SRTP is required, but we got a
    // packet before our SRTP filter is active. This means either that
    // a) we got SRTP packets before we received the SDES keys, in which case
    //    we can't decrypt it anyway, or
    // b) we got SRTP packets before DTLS completed on both the RTP and RTCP
    //    transports, so we haven't yet extracted keys, even if DTLS did
    //    complete on the transport that the packets are being sent on. It's
    //    really good practice to wait for both RTP and RTCP to be good to go
    //    before sending  media, to prevent weird failure modes, so it's fine
    //    for us to just eat packets here. This is all sidestepped if RTCP mux
    //    is used anyway.
    RTC_LOG(LS_WARNING) << "Can't process incoming RTP packet when "
                           "SRTP is inactive and crypto is required "
                        << ToString();
    return;
  }
  if (on_packet_received_n_) {
    on_packet_received_n_(parsed_packet);
  }
  media_receive_channel()->OnPacketReceived(parsed_packet);
}

RTCError BaseChannel::MaybeUpdateDemuxerAndRtpExtensions_n(
    bool update_demuxer,
    std::optional<flat_set<uint8_t>> payload_types,
    const RtpHeaderExtensions& extensions,
    std::optional<flat_set<uint32_t>> ssrcs) {
  RTC_DCHECK_RUN_ON(network_thread_);
  const bool pending_update =
      update_demuxer || payload_types.has_value() || ssrcs.has_value();
  if (pending_update) {
    media_receive_channel()->OnDemuxerCriteriaUpdatePending();
  }
  absl::Cleanup cleanup = [this, pending_update] {
    if (pending_update) {
      media_receive_channel()->OnDemuxerCriteriaUpdateComplete();
    }
  };

  RTCError error =
      rtp_transport_
          ? rtp_transport_->RegisterRtpHeaderExtensionMap(mid(), extensions)
          : (RTCError::InvalidState() << "No transport assigned.");
  if (!error.ok()) {
    error.string_builder() << " (mid=" << mid() << ")";
    return RTC_LOG_ERROR(error);
  }

  if (payload_types) {
    if (payload_types_ != *payload_types) {
      payload_types_ = std::move(*payload_types);
      update_demuxer = true;
    }
  }

  if (ssrcs) {
    if (ssrcs_ != *ssrcs) {
      ssrcs_ = std::move(*ssrcs);
      update_demuxer = true;
    }
  }

  if (!update_demuxer)
    return RTCError::OK();

  RtpDemuxerCriteria criteria = demuxer_criteria();
  if (!rtp_transport_->RegisterRtpDemuxerSink(criteria, this)) {
    return RTCError::InvalidParameter()
           << "Failed to apply demuxer criteria for '" << mid() << "': '"
           << criteria.ToString() << "'.";
  }
  return RTCError::OK();
}

bool BaseChannel::RegisterRtpDemuxerSink_n(
    const MediaContentDescription* content,
    std::vector<uint32_t> removed_ssrcs) {
  RTC_DCHECK_RUN_ON(network_thread_);
  bool clear_payload_types = false;
  if (!RtpTransceiverDirectionHasSend(content->direction())) {
    RTC_DLOG(LS_VERBOSE)
        << "RegisterRtpDemuxerSink_n: remote side will not send "
           "- disable payload type demuxing for "
        << ToString();
    clear_payload_types = true;
  }

  flat_set<uint32_t> ssrcs;
  for (const StreamParams& new_stream : content->streams()) {
    ssrcs.insert(new_stream.ssrcs.begin(), new_stream.ssrcs.end());
  }

  if (!rtp_transport_) {
    // Transport was disconnected before attempting to update the
    // criteria. This can happen while setting the remote description.
    // See chromium:1295469 for an example.
    return false;
  }

  if (!removed_ssrcs.empty()) {
    media_receive_channel()->ClearReceiveSinks_n(std::move(removed_ssrcs));
  }

  bool needs_re_registration = false;

  if (clear_payload_types && !payload_types_.empty()) {
    payload_types_.clear();
    needs_re_registration = true;
  }

  if (ssrcs_ != ssrcs) {
    ssrcs_ = std::move(ssrcs);
    needs_re_registration = true;
  }
  media_receive_channel()->SetReceiveSsrcs_n(ssrcs_);

  if (!needs_re_registration) {
    return true;
  }

  // Note that RegisterRtpDemuxerSink first unregisters the sink if
  // already registered. So this will change the state of the class
  // whether the call succeeds or not.
  RtpDemuxerCriteria criteria = demuxer_criteria();
  return rtp_transport_->RegisterRtpDemuxerSink(criteria, this);
}

RtpDemuxerCriteria BaseChannel::demuxer_criteria() const {
  RTC_DCHECK_RUN_ON(network_thread_);
  RtpDemuxerCriteria criteria(mid_);
  criteria.payload_types() = payload_types_;
  criteria.ssrcs() = ssrcs_;
  return criteria;
}

void BaseChannel::EnableMedia_n() {
  if (enabled_)
    return;

  RTC_LOG(LS_INFO) << "Channel enabled: " << ToString();
  enabled_ = true;
  UpdateMediaSendRecvState_n();
}

void BaseChannel::DisableMedia_n() {
  if (!enabled_)
    return;

  RTC_LOG(LS_INFO) << "Channel disabled: " << ToString();
  enabled_ = false;
  UpdateMediaSendRecvState_n();
}

void BaseChannel::UpdateMediaSendRecvState_n() {
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
  bool receive =
      enabled() && RtpTransceiverDirectionHasRecv(local_content_direction());
  media_receive_channel()->SetReceive(receive);

  bool send = IsReadyToSendMedia_n();
  media_send_channel()->SetSend(send);

  RTC_LOG(LS_INFO) << "Changing state, recv=" << receive << " send=" << send
                   << " for " << ToString();
}

RTCError BaseChannel::SetLocalContent(const MediaContentDescription* content,
                                      SdpType type) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
  TRACE_EVENT0("webrtc", "BaseChannel::SetLocalContent");
  RTC_DLOG(LS_INFO) << "Setting local description for " << ToString();

  RtpHeaderExtensions header_extensions =
      GetDeduplicatedRtpHeaderExtensions(content->rtp_header_extensions());

  std::optional<flat_set<uint8_t>> payload_types;
  if (RtpTransceiverDirectionHasRecv(content->direction())) {
    payload_types.emplace();
    for (const Codec& codec : content->codecs()) {
      payload_types->insert(codec.id);
    }
  }

  RTCError error = MaybeUpdateDemuxerAndRtpExtensions_n(
      /*update_demuxer=*/false, std::move(payload_types), header_extensions,
      /*ssrcs=*/std::nullopt);
  if (!error.ok()) {
    return error;
  }

  media_send_channel()->SetExtmapAllowMixed(content->extmap_allow_mixed());

  if (media_type_ == MediaType::VIDEO) {
    VideoReceiverParameters recv_params =
        std::get<VideoReceiverParameters>(last_recv_params_);
    MediaChannelParametersFromMediaDescription(
        content, header_extensions,
        RtpTransceiverDirectionHasRecv(content->direction()), &recv_params);
    recv_params.mid = mid();

    VideoSenderParameters send_params;
    if (type == SdpType::kAnswer || type == SdpType::kPrAnswer) {
      send_params = std::get<VideoSenderParameters>(last_send_params_);
      send_params.extensions = header_extensions;
      send_params.extmap_allow_mixed = content->extmap_allow_mixed();

      error = MaybeIgnorePacketization(recv_params, send_params);
      if (!error.ok())
        return error;
    }

    if (!video_media_receive_channel()->SetReceiverParameters(recv_params)) {
      StringBuilder sb;
      sb << "Failed to set local video description recv parameters for "
            "m-section with mid='"
         << mid() << "'.";
      error = RTCError::InvalidParameter(sb.Release());
      return error;
    }
    last_recv_params_ = std::move(recv_params);

    if (type == SdpType::kAnswer || type == SdpType::kPrAnswer) {
      if (!video_media_send_channel()->SetSenderParameters(send_params)) {
        StringBuilder sb;
        sb << "Failed to set send parameters for m-section with mid='" << mid()
           << "'.";
        error = RTCError::InvalidParameter(sb.Release());
        return error;
      }
      last_send_params_ = std::move(send_params);
    }
  } else {
    // Set the MID before UpdateLocalStreams_n() adds send streams, so that
    // they're created with it. The send parameters, which also carry the MID,
    // are only applied here for answers.
    voice_media_send_channel()->SetMid(mid());

    AudioReceiverParameters recv_params =
        std::get<AudioReceiverParameters>(last_recv_params_);
    MediaChannelParametersFromMediaDescription(
        content, header_extensions,
        RtpTransceiverDirectionHasRecv(content->direction()), &recv_params);
    recv_params.mid = mid();

    AudioSenderParameter send_params;
    if (type == SdpType::kAnswer || type == SdpType::kPrAnswer) {
      send_params = std::get<AudioSenderParameter>(last_send_params_);
      send_params.extensions = header_extensions;
      send_params.extmap_allow_mixed = content->extmap_allow_mixed();
    }

    if (!voice_media_receive_channel()->SetReceiverParameters(recv_params)) {
      StringBuilder sb;
      sb << "Failed to set local audio description recv parameters for "
            "m-section with mid='"
         << mid() << "'.";
      error = RTCError::InvalidParameter(sb.Release());
      return error;
    }
    last_recv_params_ = std::move(recv_params);

    if (type == SdpType::kAnswer || type == SdpType::kPrAnswer) {
      if (!voice_media_send_channel()->SetSenderParameters(send_params)) {
        StringBuilder sb;
        sb << "Failed to set send parameters for m-section with mid='" << mid()
           << "'.";
        error = RTCError::InvalidParameter(sb.Release());
        return error;
      }
      last_send_params_ = std::move(send_params);
    }
  }

  error = UpdateLocalStreams_n(content->streams(), type);
  if (!error.ok()) {
    return error;
  }

  set_local_content_direction(content->direction());
  UpdateMediaSendRecvState_n();

  return RTCError::OK();
}

RTCError BaseChannel::SetRemoteContent(const MediaContentDescription* content,
                                       SdpType type) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
  TRACE_EVENT0("webrtc", "BaseChannel::SetRemoteContent");
  RTC_LOG(LS_INFO) << "Setting remote description for " << ToString();

  RtpHeaderExtensions header_extensions =
      GetDeduplicatedRtpHeaderExtensions(content->rtp_header_extensions());

  RTCError error = MaybeUpdateDemuxerAndRtpExtensions_n(
      /*update_demuxer=*/false, /*payload_types=*/std::nullopt,
      header_extensions, /*ssrcs=*/std::nullopt);
  if (!error.ok()) {
    return error;
  }

  if (media_type_ == MediaType::VIDEO) {
    VideoSenderParameters send_params =
        std::get<VideoSenderParameters>(last_send_params_);
    RtpSendParametersFromMediaDescription(content, extensions_filter(),
                                          &send_params);
    send_params.mid = mid();
    send_params.conference_mode = content->conference_mode();

    VideoReceiverParameters recv_params;
    if (type == SdpType::kAnswer || type == SdpType::kPrAnswer) {
      recv_params = std::get<VideoReceiverParameters>(last_recv_params_);
      recv_params.extensions = send_params.extensions;
      recv_params.rtcp.reduced_size = send_params.rtcp.reduced_size;

      error = MaybeIgnorePacketization(send_params, recv_params);
      if (!error.ok())
        return error;
    }

    if (!video_media_send_channel()->SetSenderParameters(send_params)) {
      StringBuilder sb;
      sb << "Failed to set remote video description send parameters for "
            "m-section with mid='"
         << mid() << "'.";
      return RTCError::InvalidParameter(sb.Release());
    }
    last_send_params_ = std::move(send_params);

    if (type == SdpType::kAnswer || type == SdpType::kPrAnswer) {
      if (!video_media_receive_channel()->SetReceiverParameters(recv_params)) {
        StringBuilder sb;
        sb << "Failed to set recv parameters for m-section with mid='" << mid()
           << "'.";
        return RTCError::InvalidParameter(sb.Release());
      }
      last_recv_params_ = std::move(recv_params);
    }
  } else {
    AudioSenderParameter send_params =
        std::get<AudioSenderParameter>(last_send_params_);
    RtpSendParametersFromMediaDescription(content, extensions_filter(),
                                          &send_params);
    send_params.mid = mid();

    AudioReceiverParameters recv_params;
    if (type == SdpType::kAnswer || type == SdpType::kPrAnswer) {
      recv_params = std::get<AudioReceiverParameters>(last_recv_params_);
      recv_params.extensions = send_params.extensions;
    }

    if (!voice_media_send_channel()->SetSenderParameters(send_params)) {
      StringBuilder sb;
      sb << "Failed to set remote audio description send parameters for "
            "m-section with mid='"
         << mid() << "'.";
      return RTCError::InvalidParameter(sb.Release());
    }
    last_send_params_ = std::move(send_params);

    if (type == SdpType::kAnswer || type == SdpType::kPrAnswer) {
      if (!voice_media_receive_channel()->SetReceiverParameters(recv_params)) {
        StringBuilder sb;
        sb << "Failed to set recv parameters for m-section with mid='" << mid()
           << "'.";
        return RTCError::InvalidParameter(sb.Release());
      }
      last_recv_params_ = std::move(recv_params);
    }
  }

  // Non-sender RTT (RRTR/DLRR) is signaled either by the standard
  // a=rtcp-xr:rcvr-rtt or by the legacy non-standard a=rtcp-fb:<pt> rrtr.
  const bool receive_non_sender_rtt =
      content->receive_non_sender_rtt() ||
      absl::c_any_of(content->codecs(),
                     [](const Codec& codec) { return HasRrtr(codec); });

  if (media_type_ == MediaType::AUDIO) {
    voice_media_receive_channel()->SetRtcpMode(content->rtcp_reduced_size()
                                                   ? RtcpMode::kReducedSize
                                                   : RtcpMode::kCompound);
    voice_media_receive_channel()->SetReceiveNackEnabled(
        voice_media_send_channel()->SenderNackEnabled());
    voice_media_receive_channel()->SetReceiveNonSenderRttEnabled(
        receive_non_sender_rtt);
  }

  if (media_type_ == MediaType::VIDEO) {
    video_media_receive_channel()->SetReceiveNonSenderRttEnabled(
        receive_non_sender_rtt);
  }

  error = UpdateRemoteStreams_n(content, type);

  return error;
}

void BaseChannel::UpdateWritableState_n() {
  TRACE_EVENT0("webrtc", "BaseChannel::UpdateWritableState_n");
  if (rtp_transport_->IsWritable(/*rtcp=*/true) &&
      rtp_transport_->IsWritable(/*rtcp=*/false)) {
    ChannelWritable_n();
  } else {
    ChannelNotWritable_n();
  }
}

void BaseChannel::ChannelWritable_n() {
  RTC_DCHECK_RUN_ON(network_thread_);
  TRACE_EVENT0("webrtc", "BaseChannel::ChannelWritable_n");
  if (writable_) {
    return;
  }
  writable_ = true;
  RTC_LOG(LS_INFO) << "Channel writable (" << ToString() << ")"
                   << (was_ever_writable_ ? "" : " for the first time");
  if (!was_ever_writable_) {
    was_ever_writable_ = true;
    UpdateMediaSendRecvState_n();
  }
}

void BaseChannel::ChannelNotWritable_n() {
  TRACE_EVENT0("webrtc", "BaseChannel::ChannelNotWritable_n");
  if (!writable_) {
    return;
  }
  writable_ = false;
  RTC_LOG(LS_INFO) << "Channel not writable (" << ToString() << ")";
}

RTCError BaseChannel::UpdateLocalStreams_n(
    const std::vector<StreamParams>& streams,
    SdpType type) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
  // In the case of RIDs (where SSRCs are not negotiated), this method will
  // generate an SSRC for each layer in StreamParams. That representation will
  // be stored internally in `local_streams_`.
  // In subsequent offers, the same stream can appear in `streams` again
  // (without the SSRCs), so it should be looked up using RIDs (if available)
  // and then by primary SSRC.
  // In both scenarios, it is safe to assume that the media channel will be
  // created with a StreamParams object with SSRCs. However, it is not safe to
  // assume that `local_streams_` will always have SSRCs as there are scenarios
  // in which niether SSRCs or RIDs are negotiated.

  // Check for streams that have been removed.
  for (const StreamParams& old_stream : local_streams_) {
    if (!old_stream.has_ssrcs() ||
        GetStream(streams, StreamFinder(&old_stream))) {
      continue;
    }
    if (!media_send_channel()->RemoveSendStream(old_stream.first_ssrc())) {
      return RTCError::InvalidParameter()
             << "Failed to remove send stream with ssrc "
             << old_stream.first_ssrc() << " from m-section with mid='" << mid()
             << "'.";
    }
  }
  // Check for new streams.
  std::vector<StreamParams> all_streams;
  for (const StreamParams& stream : streams) {
    StreamParams* existing = GetStream(local_streams_, StreamFinder(&stream));
    if (existing) {
      // Parameters cannot change for an existing stream.
      all_streams.push_back(*existing);
      continue;
    }

    all_streams.push_back(stream);
    StreamParams& new_stream = all_streams.back();

    if (!new_stream.has_ssrcs() && !new_stream.has_rids()) {
      continue;
    }

    RTC_DCHECK(new_stream.has_ssrcs() || new_stream.has_rids());
    if (new_stream.has_ssrcs() && new_stream.has_rids()) {
      return RTCError::InvalidParameter()
             << "Failed to add send stream: " << new_stream.first_ssrc()
             << " into m-section with mid='" << mid()
             << "'. Stream has both SSRCs and RIDs.";
    }

    // At this point we use the legacy simulcast group in StreamParams to
    // indicate that we want multiple layers to the media channel.
    if (!new_stream.has_ssrcs()) {
      // TODO(bugs.webrtc.org/10250): Indicate if flex is desired here.
      new_stream.GenerateSsrcs(new_stream.rids().size(), /* rtx = */ true,
                               /* flex_fec = */ false, ssrc_generator_);
    }

    if (media_send_channel()->AddSendStream(new_stream)) {
      RTC_LOG(LS_INFO) << "Add send stream ssrc: " << new_stream.first_ssrc()
                       << " into " << ToString();
    } else {
      return RTCError::InvalidParameter()
             << "Failed to add send stream ssrc: " << new_stream.first_ssrc()
             << " into m-section with mid='" << mid() << "'";
    }
  }
  local_streams_ = std::move(all_streams);
  return RTCError::OK();
}

RTCError BaseChannel::UpdateRemoteStreams_n(
    const MediaContentDescription* content,
    SdpType type) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
  media_receive_channel()->OnDemuxerCriteriaUpdatePending();
  absl::Cleanup cleanup = [this] {
    media_receive_channel()->OnDemuxerCriteriaUpdateComplete();
  };

  const std::vector<StreamParams>& streams = content->streams();
  const bool new_has_unsignaled_ssrcs = HasStreamWithNoSsrcs(streams);
  const bool old_has_unsignaled_ssrcs = HasStreamWithNoSsrcs(remote_streams_);

  // Check for streams that have been removed.
  std::vector<uint32_t> removed_ssrcs;
  bool reset_unsignaled_streams = false;
  for (const StreamParams& old_stream : remote_streams_) {
    // If we no longer have an unsignaled stream, we would like to remove
    // the unsignaled stream params that are cached.
    if (!old_stream.has_ssrcs() && !new_has_unsignaled_ssrcs) {
      if (!reset_unsignaled_streams) {
        reset_unsignaled_streams = true;
        std::vector<uint32_t> unsignaled =
            media_receive_channel()->GetUnsignaledSsrcs();
        removed_ssrcs.insert(removed_ssrcs.end(), unsignaled.begin(),
                             unsignaled.end());
        RTC_LOG(LS_INFO) << "Reset unsignaled remote stream for " << ToString()
                         << ".";
      }
    } else if (old_stream.has_ssrcs() &&
               !GetStreamBySsrc(streams, old_stream.first_ssrc())) {
      removed_ssrcs.push_back(old_stream.first_ssrc());
    }
  }

  // Re-register the sink to update after changing the demuxer criteria first.
  if (!RegisterRtpDemuxerSink_n(content, std::move(removed_ssrcs))) {
    return RTCError::InvalidParameter()
           << "Failed to set up audio demuxing for mid='" << mid() << "'.";
  }

  if (reset_unsignaled_streams) {
    media_receive_channel()->ResetUnsignaledRecvStream();
  }

  // Now remove the old streams on the network thread.
  for (const StreamParams& old_stream : remote_streams_) {
    if (old_stream.has_ssrcs() &&
        !GetStreamBySsrc(streams, old_stream.first_ssrc())) {
      if (media_receive_channel()->RemoveRecvStream(old_stream.first_ssrc())) {
        RTC_LOG(LS_INFO) << "Remove remote ssrc: " << old_stream.first_ssrc()
                         << " from " << ToString() << ".";
      } else {
        return RTCError::InvalidParameter()
               << "Failed to remove remote stream with ssrc "
               << old_stream.first_ssrc() << " from m-section with mid='"
               << mid() << "'.";
      }
    }
  }

  // Check for new streams.
  for (const StreamParams& new_stream : streams) {
    // We allow a StreamParams with an empty list of SSRCs, in which case the
    // MediaChannel will cache the parameters and use them for any unsignaled
    // stream received later.
    if ((!new_stream.has_ssrcs() && !old_has_unsignaled_ssrcs) ||
        !GetStreamBySsrc(remote_streams_, new_stream.first_ssrc())) {
      if (media_receive_channel()->AddRecvStream(new_stream)) {
        RTC_LOG(LS_INFO) << "Add remote ssrc: "
                         << (new_stream.has_ssrcs()
                                 ? std::to_string(new_stream.first_ssrc())
                                 : "unsignaled")
                         << " to " << ToString();
      } else {
        return RTCError::InvalidParameter()
               << "Failed to add remote stream ssrc: "
               << (new_stream.has_ssrcs()
                       ? std::to_string(new_stream.first_ssrc())
                       : "unsignaled")
               << " to " << ToString();
      }
    }
  }

  remote_streams_ = streams;

  set_remote_content_direction(content->direction());
  UpdateMediaSendRecvState_n();

  return RTCError::OK();
}

RtpHeaderExtensions BaseChannel::GetDeduplicatedRtpHeaderExtensions(
    const RtpHeaderExtensions& extensions) {
  return RtpExtension::DeduplicateHeaderExtensions(extensions,
                                                   extensions_filter_);
}

void BaseChannel::SignalSentPacket_n(const SentPacketInfo& sent_packet) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK(network_initialized());
  media_send_channel()->OnPacketSent(sent_packet);
}

}  // namespace webrtc
