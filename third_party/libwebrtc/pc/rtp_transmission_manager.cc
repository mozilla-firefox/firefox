/*
 *  Copyright 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/rtp_transmission_manager.h"

#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/audio_options.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/rtp_transceiver_direction.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "media/base/media_channel.h"
#include "media/base/media_config.h"
#include "media/base/media_engine.h"
#include "pc/audio_rtp_receiver.h"
#include "pc/channel_interface.h"
#include "pc/codec_vendor.h"
#include "pc/connection_context.h"
#include "pc/legacy_stats_collector_interface.h"
#include "pc/rtp_receiver.h"
#include "pc/rtp_receiver_proxy.h"
#include "pc/rtp_sender.h"
#include "pc/rtp_sender_proxy.h"
#include "pc/rtp_transceiver.h"
#include "pc/simulcast_description.h"
#include "pc/usage_pattern.h"
#include "pc/video_rtp_receiver.h"
#include "rtc_base/checks.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/logging.h"
#include "rtc_base/system/plan_b_only.h"

namespace webrtc {

namespace {

const char kDefaultAudioSenderId[] = "defaulta0";
const char kDefaultVideoSenderId[] = "defaultv0";

template <typename T>
MediaType TrackType(T& track) {
  return track->kind() == MediaStreamTrackInterface::kAudioKind
             ? MediaType::AUDIO
             : MediaType::VIDEO;
}

std::optional<uint32_t> GetSenderSsrc(const std::vector<RtpSenderInfo>& infos,
                                      absl::string_view stream_id,
                                      absl::string_view sender_id) {
  auto it = absl::c_find_if(infos, [&](const RtpSenderInfo& info) {
    return info.stream_id == stream_id && info.sender_id == sender_id;
  });
  return it == infos.end() ? std::nullopt
                           : std::optional<uint32_t>((*it).first_ssrc);
}
}  // namespace

RtpTransmissionManager::RtpTransmissionManager(
    const Environment& env,
    Call* call,
    bool is_unified_plan,
    ConnectionContext* context,
    CodecLookupHelper* codec_lookup_helper,
    UsagePattern* usage_pattern,
    PeerConnectionObserver* observer,
    LegacyStatsCollectorInterface* legacy_stats,
    absl::AnyInvocable<void()> on_negotiation_needed)
    : env_(env),
      is_unified_plan_(is_unified_plan),
      call_(call),
      context_(context),
      codec_lookup_helper_(codec_lookup_helper),
      usage_pattern_(usage_pattern),
      observer_(observer),
      legacy_stats_(legacy_stats),
      on_negotiation_needed_(std::move(on_negotiation_needed)),
      weak_ptr_factory_(this) {}

void RtpTransmissionManager::Close() {
  closed_ = true;
  observer_ = nullptr;
}

// Implementation of SetStreamsObserver
void RtpTransmissionManager::OnSetStreams() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(IsUnifiedPlan());
  OnNegotiationNeeded();
}

// Function to call back to the PeerConnection when negotiation is needed
void RtpTransmissionManager::OnNegotiationNeeded() {
  on_negotiation_needed_();
}

std::vector<RtpHeaderExtensionCapability>
RtpTransmissionManager::GetDefaultHeaderExtensions(MediaType media_type) {
  if (media_type == MediaType::AUDIO) {
    return media_engine()->voice().GetRtpHeaderExtensions(&env_.field_trials());
  }
  RTC_DCHECK_EQ(media_type, MediaType::VIDEO);
  return media_engine()->video().GetRtpHeaderExtensions(&env_.field_trials());
}

void RtpTransmissionManager::RunWithObserver(
    absl::AnyInvocable<void(PeerConnectionObserver*) &&> task) {  // NOLINT
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(observer_);
  std::move(task)(observer_);
}

PLAN_B_ONLY VoiceMediaSendChannelInterface*
RtpTransmissionManager::voice_media_send_channel() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(!IsUnifiedPlan());
  return GetAudioTransceiver()->internal()->voice_media_send_channel();
}

PLAN_B_ONLY VideoMediaSendChannelInterface*
RtpTransmissionManager::video_media_send_channel() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(!IsUnifiedPlan());
  return GetVideoTransceiver()->internal()->video_media_send_channel();
}
PLAN_B_ONLY VoiceMediaReceiveChannelInterface*
RtpTransmissionManager::voice_media_receive_channel() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(!IsUnifiedPlan());
  return GetAudioTransceiver()->internal()->voice_media_receive_channel();
}

PLAN_B_ONLY VideoMediaReceiveChannelInterface*
RtpTransmissionManager::video_media_receive_channel() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(!IsUnifiedPlan());
  return GetVideoTransceiver()->internal()->video_media_receive_channel();
}

PLAN_B_ONLY RTCErrorOr<scoped_refptr<RtpSenderInterface>>
RtpTransmissionManager::AddTrackPlanB(
    scoped_refptr<MediaStreamTrackInterface> track,
    const std::vector<std::string>& stream_ids,
    const std::vector<RtpEncodingParameters>* init_send_encodings) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(!IsUnifiedPlan());
  if (stream_ids.size() > 1u) {
    return RTC_LOG_ERROR(
        RTCError::UnsupportedOperation()
        << "AddTrack with more than one stream is not supported "
           "with Plan B semantics.");
  }
  std::vector<std::string> adjusted_stream_ids;
  if (stream_ids.empty()) {
    adjusted_stream_ids.push_back(CreateRandomUuid());
  } else {
    adjusted_stream_ids.reserve(stream_ids.size());
    absl::c_copy_if(stream_ids, std::back_inserter(adjusted_stream_ids),
                    [&](const auto& id) {
                      return !absl::c_linear_search(adjusted_stream_ids, id);
                    });
  }

  MediaType media_type = TrackType(track);
  RtpTransceiver* transceiver = media_type == MediaType::AUDIO
                                    ? GetAudioTransceiver()->internal()
                                    : GetVideoTransceiver()->internal();
  scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>> new_sender =
      transceiver->AddSenderPlanB(track, track->id(), adjusted_stream_ids,
                                  init_send_encodings
                                      ? *init_send_encodings
                                      : std::vector<RtpEncodingParameters>(1));
  RTC_DCHECK(new_sender->internal()->stream_ids() == adjusted_stream_ids);
  std::optional<uint32_t> ssrc =
      GetSenderSsrc(media_type == MediaType::AUDIO ? local_audio_sender_infos_
                                                   : local_video_sender_infos_,
                    adjusted_stream_ids[0], track->id());
  if (ssrc) {
    new_sender->internal()->SetSsrc(*ssrc);
  }
  NoteUsageEvent(media_type == MediaType::AUDIO ? UsageEvent::AUDIO_ADDED
                                                : UsageEvent::VIDEO_ADDED);
  return scoped_refptr<RtpSenderInterface>(new_sender);
}

RTCErrorOr<scoped_refptr<RtpSenderInterface>>
RtpTransmissionManager::AddTrackUnifiedPlan(
    const MediaConfig& media_config,
    const AudioOptions& audio_options,
    const VideoOptions& video_options,
    const CryptoOptions& crypto_options,
    VideoBitrateAllocatorFactory* video_bitrate_allocator_factory,
    scoped_refptr<MediaStreamTrackInterface> track,
    const std::vector<std::string>& stream_ids,
    const std::vector<RtpEncodingParameters>* init_send_encodings) {
  RTC_DCHECK(IsUnifiedPlan());
  auto transceiver =
      FindFirstTransceiverForAddedTrack(track, init_send_encodings);
  if (transceiver) {
    RTC_LOG(LS_INFO) << "Reusing an existing "
                     << MediaTypeToString(transceiver->media_type())
                     << " transceiver for AddTrack.";
    if (transceiver->stopping()) {
      return RTC_LOG_ERROR(RTCError::InvalidParameter()
                           << "The existing transceiver is stopping.");
    }

    if (transceiver->direction() == RtpTransceiverDirection::kRecvOnly) {
      transceiver->internal()->set_direction(
          RtpTransceiverDirection::kSendRecv);
    } else if (transceiver->direction() == RtpTransceiverDirection::kInactive) {
      transceiver->internal()->set_direction(
          RtpTransceiverDirection::kSendOnly);
    }
    transceiver->sender()->SetTrack(track.get());
    transceiver->internal()->sender_internal()->set_stream_ids(stream_ids);
    transceiver->internal()->set_reused_for_addtrack(true);
  } else {
    MediaType media_type = TrackType(track);
    RTC_LOG(LS_INFO) << "Adding " << MediaTypeToString(media_type)
                     << " transceiver in response to a call to AddTrack.";
    std::string sender_id = track->id();
    // Avoid creating a sender with an existing ID by generating a random ID.
    // This can happen if this is the second time AddTrack has created a sender
    // for this track.
    if (FindSenderById(sender_id)) {
      sender_id = CreateRandomUuid();
    }
    transceiver = CreateAndAddTransceiver(
        media_config, audio_options, video_options, crypto_options,
        video_bitrate_allocator_factory, media_type, track, stream_ids,
        init_send_encodings
            ? *init_send_encodings
            : std::vector<RtpEncodingParameters>(1, RtpEncodingParameters{}),
        /*header_extensions_to_negotiate=*/{},
        /*simulcast_rejected=*/false, /*initial_simulcast_layers=*/{},
        sender_id, /*receiver_id=*/"");
    transceiver->internal()->set_created_by_addtrack(true);
    transceiver->internal()->set_direction(RtpTransceiverDirection::kSendRecv);
  }
  return transceiver->sender();
}

scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>
RtpTransmissionManager::CreateAndAddTransceiver(
    const MediaConfig& media_config,
    const AudioOptions& audio_options,
    const VideoOptions& video_options,
    const CryptoOptions& crypto_options,
    VideoBitrateAllocatorFactory* video_bitrate_allocator_factory,
    MediaType media_type,
    scoped_refptr<MediaStreamTrackInterface> track,
    const std::vector<std::string>& stream_ids,
    const std::vector<RtpEncodingParameters>& init_send_encodings,
    const std::vector<RtpHeaderExtensionCapability>&
        header_extensions_to_negotiate,
    bool simulcast_rejected,
    const std::vector<SimulcastLayer>& initial_simulcast_layers,
    absl::string_view sender_id,
    absl::string_view receiver_id) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  // Ensure that the new sender does not have an ID that is already in use by
  // another sender.
  // Allow receiver IDs to conflict since those come from remote SDP (which
  // could be invalid, but should not cause a crash).
  RTC_DCHECK(!FindSenderById(sender_id));
  std::vector<RtpHeaderExtensionCapability> header_extensions =
      std::move(header_extensions_to_negotiate);
  if (!env_.field_trials().IsDisabled(
          "WebRTC-HeaderExtensionNegotiateMemory")) {
    // If we have already negotiated header extensions for this type,
    // and it is not stopped,
    // reuse the negotiated state for new transceivers of the same type.
    for (const auto& transceiver : transceivers()->List()) {
      if (transceiver->media_type() == media_type && !transceiver->stopping()) {
        header_extensions = transceiver->GetHeaderExtensionsToNegotiate();
        break;
      }
    }
  }
  if (header_extensions.empty()) {
    header_extensions = GetDefaultHeaderExtensions(media_type);
  }

  RtpSenderBase::SetStreamsObserver* observer =
      IsUnifiedPlan() ? this : nullptr;
  auto transceiver = RtpTransceiverProxyWithInternal<RtpTransceiver>::Create(
      signaling_thread(),
      make_ref_counted<RtpTransceiver>(
          env_, call_, media_config, sender_id, receiver_id, media_type, track,
          stream_ids, std::move(init_send_encodings), context_,
          codec_lookup_helper_, legacy_stats_, observer, audio_options,
          video_options, crypto_options, video_bitrate_allocator_factory,
          std::move(header_extensions), simulcast_rejected,
          initial_simulcast_layers,
          [this_weak_ptr = weak_ptr_factory_.GetWeakPtr()]() {
            if (this_weak_ptr) {
              this_weak_ptr->OnNegotiationNeeded();
            }
          }));
  transceivers()->Add(transceiver);
  NoteUsageEvent(media_type == MediaType::AUDIO ? UsageEvent::AUDIO_ADDED
                                                : UsageEvent::VIDEO_ADDED);
  return transceiver;
}

scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>
RtpTransmissionManager::FindFirstTransceiverForAddedTrack(
    scoped_refptr<MediaStreamTrackInterface> track,
    const std::vector<RtpEncodingParameters>* init_send_encodings) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(track);
  if (init_send_encodings != nullptr) {
    return nullptr;
  }
  const MediaType media_type = TrackType(track);
  for (auto& transceiver : transceivers()->List()) {
    if (!transceiver->sender()->track() &&
        transceiver->media_type() == media_type && !transceiver->stopped() &&
        !transceiver->internal()->has_ever_been_used_to_send()) {
      return transceiver;
    }
  }
  return nullptr;
}

std::vector<scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>>>
RtpTransmissionManager::GetSendersInternal() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  std::vector<scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>>>
      all_senders;
  for (const auto& transceiver : transceivers_.List()) {
    if (IsUnifiedPlan() && transceiver->internal()->stopped())
      continue;

    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN()
    auto senders = transceiver->internal()->senders();
    RTC_ALLOW_PLAN_B_DEPRECATION_END()
    all_senders.insert(all_senders.end(), senders.begin(), senders.end());
  }
  return all_senders;
}

std::vector<scoped_refptr<RtpReceiverProxyWithInternal<RtpReceiverInternal>>>
RtpTransmissionManager::GetReceiversInternal() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  std::vector<scoped_refptr<RtpReceiverProxyWithInternal<RtpReceiverInternal>>>
      all_receivers;
  for (const auto& transceiver : transceivers_.List()) {
    if (IsUnifiedPlan() && transceiver->internal()->stopped())
      continue;

    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN()
    auto receivers = transceiver->internal()->receivers();
    RTC_ALLOW_PLAN_B_DEPRECATION_END()
    all_receivers.insert(all_receivers.end(), receivers.begin(),
                         receivers.end());
  }
  return all_receivers;
}

PLAN_B_ONLY scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>
RtpTransmissionManager::GetAudioTransceiver() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  // This method only works with Plan B SDP, where there is a single
  // audio/video transceiver.
  RTC_DCHECK(!IsUnifiedPlan());
  for (const auto& transceiver : transceivers_.List()) {
    if (transceiver->media_type() == MediaType::AUDIO) {
      return transceiver;
    }
  }
  RTC_DCHECK_NOTREACHED();
  return nullptr;
}

PLAN_B_ONLY scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>
RtpTransmissionManager::GetVideoTransceiver() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  // This method only works with Plan B SDP, where there is a single
  // audio/video transceiver.
  RTC_DCHECK(!IsUnifiedPlan());
  for (const auto& transceiver : transceivers_.List()) {
    if (transceiver->media_type() == MediaType::VIDEO) {
      return transceiver;
    }
  }
  RTC_DCHECK_NOTREACHED();
  return nullptr;
}

PLAN_B_ONLY void RtpTransmissionManager::AddTrackPlanB(
    MediaStreamTrackInterface* track,
    MediaStreamInterface* stream) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(track);
  RTC_DCHECK(stream);
  RTC_DCHECK(!IsUnifiedPlan());
  auto sender = FindSenderForTrack(track);
  if (sender) {
    // We already have a sender for this track, so just change the stream_id
    // so that it's correct in the next call to CreateOffer.
    sender->internal()->set_stream_ids({stream->id()});
    return;
  }

  // Normal case; we've never seen this track before.
  MediaType media_type = TrackType(track);
  RtpTransceiver* transceiver = media_type == MediaType::AUDIO
                                    ? GetAudioTransceiver()->internal()
                                    : GetVideoTransceiver()->internal();
  scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>> new_sender =
      transceiver->AddSenderPlanB(
          scoped_refptr<MediaStreamTrackInterface>(track), track->id(),
          {stream->id()}, {});
  // If the sender has already been configured in SDP, we call SetSsrc,
  // which will connect the sender to the underlying transport. This can
  // occur if a local session description that contains the ID of the sender
  // is set before AddStream is called. It can also occur if the local
  // session description is not changed and RemoveStream is called, and
  // later AddStream is called again with the same stream.
  std::optional<uint32_t> ssrc =
      GetSenderSsrc(media_type == MediaType::AUDIO ? local_audio_sender_infos_
                                                   : local_video_sender_infos_,
                    stream->id(), track->id());
  if (ssrc) {
    new_sender->internal()->SetSsrc(*ssrc);
  }
}

// TODO(deadbeef): Don't destroy RtpSenders here; they should be kept around
// indefinitely, when we have unified plan SDP.
PLAN_B_ONLY void RtpTransmissionManager::RemoveTrackPlanB(
    MediaStreamTrackInterface* track,
    MediaStreamInterface* stream) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(!IsUnifiedPlan());
  auto sender = FindSenderForTrack(track);
  if (!sender) {
    RTC_LOG(LS_WARNING) << "RtpSender for track with id " << track->id()
                        << " doesn't exist.";
    return;
  }
  RtpTransceiver* transceiver = TrackType(track) == MediaType::AUDIO
                                    ? GetAudioTransceiver()->internal()
                                    : GetVideoTransceiver()->internal();
  transceiver->RemoveSenderPlanB(sender.get());
}

PLAN_B_ONLY void RtpTransmissionManager::CreateAudioReceiverPlanB(
    MediaStreamInterface* stream,
    const RtpSenderInfo& remote_sender_info) {
  RTC_DCHECK(!IsUnifiedPlan());
  RTC_DCHECK(!closed_);
  std::vector<scoped_refptr<MediaStreamInterface>> streams;
  streams.push_back(scoped_refptr<MediaStreamInterface>(stream));
  // TODO(https://crbug.com/webrtc/9480): When we remove remote_streams(), use
  // the constructor taking stream IDs instead.
  auto audio_receiver = make_ref_counted<AudioRtpReceiver>(
      network_thread(), remote_sender_info.sender_id, streams, false,
      voice_media_receive_channel(), &env_.clock());
  auto task = (remote_sender_info.sender_id == kDefaultAudioSenderId)
                  ? audio_receiver->GetSetupForUnsignaledMediaChannel()
                  : audio_receiver->GetSetupForMediaChannel(
                        remote_sender_info.first_ssrc);
  network_thread()->BlockingCall([&]() mutable { std::move(task)(); });

  auto receiver = RtpReceiverProxyWithInternal<RtpReceiverInternal>::Create(
      signaling_thread(), network_thread(), std::move(audio_receiver));
  GetAudioTransceiver()->internal()->AddReceiverPlanB(receiver);
  RunWithObserver(
      [&](auto observer) { observer->OnAddTrack(receiver, streams); });
  NoteUsageEvent(UsageEvent::AUDIO_ADDED);
}

PLAN_B_ONLY void RtpTransmissionManager::CreateVideoReceiverPlanB(
    MediaStreamInterface* stream,
    const RtpSenderInfo& remote_sender_info) {
  RTC_DCHECK(!IsUnifiedPlan());
  RTC_DCHECK(!closed_);
  std::vector<scoped_refptr<MediaStreamInterface>> streams;
  streams.push_back(scoped_refptr<MediaStreamInterface>(stream));
  // TODO(https://crbug.com/webrtc/9480): When we remove remote_streams(), use
  // the constructor taking stream IDs instead.
  auto video_receiver = make_ref_counted<VideoRtpReceiver>(
      network_thread(), remote_sender_info.sender_id, streams,
      /*enable_sframe_at_owner=*/nullptr,
      /*media_channel=*/nullptr, &env_.clock());

  auto task = video_receiver->GetSetupForMediaChannel(
      remote_sender_info.sender_id == kDefaultVideoSenderId
          ? std::nullopt
          : std::optional<uint32_t>(remote_sender_info.first_ssrc),
      video_media_receive_channel());
  network_thread()->BlockingCall([&]() mutable { std::move(task)(); });

  auto receiver = RtpReceiverProxyWithInternal<RtpReceiverInternal>::Create(
      signaling_thread(), network_thread(), std::move(video_receiver));
  GetVideoTransceiver()->internal()->AddReceiverPlanB(receiver);
  RunWithObserver(
      [&](auto observer) { observer->OnAddTrack(receiver, streams); });
  NoteUsageEvent(UsageEvent::VIDEO_ADDED);
}

// TODO(deadbeef): Keep RtpReceivers around even if track goes away in remote
// description.
PLAN_B_ONLY scoped_refptr<RtpReceiverInterface>
RtpTransmissionManager::RemoveAndStopReceiver(
    const RtpSenderInfo& remote_sender_info) {
  RTC_DCHECK(!IsUnifiedPlan());
  auto receiver = FindReceiverById(remote_sender_info.sender_id);
  if (!receiver) {
    RTC_LOG(LS_WARNING) << "RtpReceiver for track with id "
                        << remote_sender_info.sender_id << " doesn't exist.";
    return nullptr;
  }
  if (receiver->media_type() == MediaType::AUDIO) {
    GetAudioTransceiver()->internal()->RemoveReceiverPlanB(receiver.get());
  } else {
    GetVideoTransceiver()->internal()->RemoveReceiverPlanB(receiver.get());
  }
  return receiver;
}

PLAN_B_ONLY void RtpTransmissionManager::OnRemoteSenderAddedPlanB(
    const RtpSenderInfo& sender_info,
    MediaStreamInterface* stream,
    MediaType media_type) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(!IsUnifiedPlan());
  RTC_LOG(LS_INFO) << "Creating " << MediaTypeToString(media_type)
                   << " receiver for track_id=" << sender_info.sender_id
                   << " and stream_id=" << sender_info.stream_id;

  if (media_type == MediaType::AUDIO) {
    CreateAudioReceiverPlanB(stream, sender_info);
  } else if (media_type == MediaType::VIDEO) {
    CreateVideoReceiverPlanB(stream, sender_info);
  } else {
    RTC_DCHECK_NOTREACHED() << "Invalid media type";
  }
}

PLAN_B_ONLY void RtpTransmissionManager::OnRemoteSenderRemovedPlanB(
    const RtpSenderInfo& sender_info,
    MediaStreamInterface* stream,
    MediaType media_type) {
  RTC_DCHECK(!IsUnifiedPlan());
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_LOG(LS_INFO) << "Removing " << MediaTypeToString(media_type)
                   << " receiver for track_id=" << sender_info.sender_id
                   << " and stream_id=" << sender_info.stream_id;

  scoped_refptr<RtpReceiverInterface> receiver;
  if (media_type == MediaType::AUDIO) {
    // When the MediaEngine audio channel is destroyed, the RemoteAudioSource
    // will be notified which will end the AudioRtpReceiver::track().
    receiver = RemoveAndStopReceiver(sender_info);
    scoped_refptr<AudioTrackInterface> audio_track =
        stream->FindAudioTrack(sender_info.sender_id);
    if (audio_track) {
      stream->RemoveTrack(audio_track);
    }
  } else if (media_type == MediaType::VIDEO) {
    // Stopping or destroying a VideoRtpReceiver will end the
    // VideoRtpReceiver::track().
    receiver = RemoveAndStopReceiver(sender_info);
    scoped_refptr<VideoTrackInterface> video_track =
        stream->FindVideoTrack(sender_info.sender_id);
    if (video_track) {
      // There's no guarantee the track is still available, e.g. the track may
      // have been removed from the stream by an application.
      stream->RemoveTrack(video_track);
    }
  } else {
    RTC_DCHECK_NOTREACHED() << "Invalid media type";
  }
  if (receiver) {
    RTC_DCHECK(!closed_);
    RunWithObserver([&](auto observer) { observer->OnRemoveTrack(receiver); });
  }
}

PLAN_B_ONLY void RtpTransmissionManager::OnLocalSenderAdded(
    const RtpSenderInfo& sender_info,
    MediaType media_type) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(!IsUnifiedPlan());
  auto sender = FindSenderById(sender_info.sender_id);
  if (!sender) {
    RTC_LOG(LS_WARNING) << "An unknown RtpSender with id "
                        << sender_info.sender_id
                        << " has been configured in the local description.";
    return;
  }

  if (sender->media_type() != media_type) {
    RTC_LOG(LS_WARNING) << "An RtpSender has been configured in the local"
                           " description with an unexpected media type.";
    return;
  }

  sender->internal()->set_stream_ids({sender_info.stream_id});
  sender->internal()->SetSsrc(sender_info.first_ssrc);
}

PLAN_B_ONLY void RtpTransmissionManager::OnLocalSenderRemoved(
    const RtpSenderInfo& sender_info,
    MediaType media_type) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  auto sender = FindSenderById(sender_info.sender_id);
  if (!sender) {
    // This is the normal case. I.e., RemoveStream has been called and the
    // SessionDescriptions has been renegotiated.
    return;
  }

  // A sender has been removed from the SessionDescription but it's still
  // associated with the PeerConnection. This only occurs if the SDP doesn't
  // match with the calls to CreateSender, AddStream and RemoveStream.
  if (sender->media_type() != media_type) {
    RTC_LOG(LS_WARNING) << "An RtpSender has been configured in the local"
                           " description with an unexpected media type.";
    return;
  }

  sender->internal()->SetSsrc(0);
}

std::vector<RtpSenderInfo>* RtpTransmissionManager::GetRemoteSenderInfos(
    MediaType media_type) {
  RTC_DCHECK(media_type == MediaType::AUDIO || media_type == MediaType::VIDEO);
  return (media_type == MediaType::AUDIO) ? &remote_audio_sender_infos_
                                          : &remote_video_sender_infos_;
}

std::vector<RtpSenderInfo>* RtpTransmissionManager::GetLocalSenderInfos(
    MediaType media_type) {
  RTC_DCHECK(media_type == MediaType::AUDIO || media_type == MediaType::VIDEO);
  return (media_type == MediaType::AUDIO) ? &local_audio_sender_infos_
                                          : &local_video_sender_infos_;
}

scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>>
RtpTransmissionManager::FindSenderForTrack(
    MediaStreamTrackInterface* track) const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  for (const auto& transceiver : transceivers_.List()) {
    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN()
    for (const auto& sender : transceiver->internal()->senders()) {
      if (sender->track() == track) {
        return sender;
      }
    }
    RTC_ALLOW_PLAN_B_DEPRECATION_END()
  }
  return nullptr;
}

scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>>
RtpTransmissionManager::FindSenderById(absl::string_view sender_id) const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  for (const auto& transceiver : transceivers_.List()) {
    RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN();
    // Under Unified Plan, senders() always has exactly one entry,
    // and one can use sender() not senders().
    // Since this function is used both in Plan B and Unified, this is
    // left as-is for now.
    for (const auto& sender : transceiver->internal()->senders()) {
      if (sender->id() == sender_id) {
        return sender;
      }
    }
    RTC_ALLOW_PLAN_B_DEPRECATION_END();
  }
  return nullptr;
}

PLAN_B_ONLY scoped_refptr<RtpReceiverProxyWithInternal<RtpReceiverInternal>>
RtpTransmissionManager::FindReceiverById(absl::string_view receiver_id) const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  for (const auto& transceiver : transceivers_.List()) {
    for (const auto& receiver : transceiver->internal()->receivers()) {
      if (receiver->id() == receiver_id) {
        return receiver;
      }
    }
  }
  return nullptr;
}

const MediaEngineInterface* RtpTransmissionManager::media_engine() const {
  return context_->media_engine();
}

}  // namespace webrtc
