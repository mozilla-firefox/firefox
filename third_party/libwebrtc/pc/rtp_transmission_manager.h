/*
 *  Copyright 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_RTP_TRANSMISSION_MANAGER_H_
#define PC_RTP_TRANSMISSION_MANAGER_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/audio_options.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/video/video_bitrate_allocator_factory.h"
#include "call/call.h"
#include "media/base/media_channel.h"
#include "media/base/media_config.h"
#include "media/base/media_engine.h"
#include "pc/codec_vendor.h"
#include "pc/connection_context.h"
#include "pc/legacy_stats_collector_interface.h"
#include "pc/rtp_receiver.h"
#include "pc/rtp_receiver_proxy.h"
#include "pc/rtp_sender.h"
#include "pc/rtp_sender_proxy.h"
#include "pc/rtp_transceiver.h"
#include "pc/simulcast_description.h"
#include "pc/transceiver_list.h"
#include "pc/usage_pattern.h"
#include "rtc_base/system/plan_b_only.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/unique_id_generator.h"
#include "rtc_base/weak_ptr.h"

namespace webrtc {

// This class contains information about
// an RTPSender, used for things like looking it up by SSRC.
struct RtpSenderInfo {
  RtpSenderInfo() : first_ssrc(0) {}
  RtpSenderInfo(absl::string_view stream_id,
                absl::string_view sender_id,
                uint32_t ssrc)
      : stream_id(stream_id), sender_id(sender_id), first_ssrc(ssrc) {}
  bool operator==(const RtpSenderInfo& other) {
    return this->stream_id == other.stream_id &&
           this->sender_id == other.sender_id &&
           this->first_ssrc == other.first_ssrc;
  }
  std::string stream_id;
  std::string sender_id;
  // An RtpSender can have many SSRCs. The first one is used as a sort of ID
  // for communicating with the lower layers.
  uint32_t first_ssrc;
};

// The RtpTransmissionManager class is responsible for managing the lifetime
// and relationships between objects of type RtpSender, RtpReceiver and
// RtpTransceiver.
class RtpTransmissionManager : public RtpSenderBase::SetStreamsObserver {
 public:
  RtpTransmissionManager(const Environment& env,
                         Call* call,
                         bool is_unified_plan,
                         ConnectionContext* context,
                         CodecLookupHelper* codec_lookup_helper,
                         UsagePattern* usage_pattern,
                         PeerConnectionObserver* observer,
                         LegacyStatsCollectorInterface* legacy_stats,
                         absl::AnyInvocable<void()> on_negotiation_needed);

  // No move or copy permitted.
  RtpTransmissionManager(const RtpTransmissionManager&) = delete;
  RtpTransmissionManager& operator=(const RtpTransmissionManager&) = delete;

  // Stop activity. In particular, don't call observer_ any more.
  void Close();

  // RtpSenderBase::SetStreamsObserver override.
  void OnSetStreams() override;

  scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>
  CreateAndAddTransceiver(
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
      absl::string_view receiver_id = "");

  // Returns the list of senders currently associated with some
  // registered transceiver
  std::vector<scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>>>
  GetSendersInternal() const;

  // Returns the list of receivers currently associated with a transceiver
  std::vector<scoped_refptr<RtpReceiverProxyWithInternal<RtpReceiverInternal>>>
  GetReceiversInternal() const;

  // Plan B: Get the transceiver containing all audio senders and receivers
  PLAN_B_ONLY scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>
  GetAudioTransceiver() const;
  // Plan B: Get the transceiver containing all video senders and receivers
  PLAN_B_ONLY scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>
  GetVideoTransceiver() const;

  // Plan B: Add an audio/video track, reusing or creating the sender.
  PLAN_B_ONLY void AddTrackPlanB(MediaStreamTrackInterface* track,
                                 MediaStreamInterface* stream);
  // Plan B: Remove an audio/video track, removing the sender.
  PLAN_B_ONLY void RemoveTrackPlanB(MediaStreamTrackInterface* track,
                                    MediaStreamInterface* stream);

  // Triggered when a remote sender has been seen for the first time in a remote
  // session description. It creates a remote MediaStreamTrackInterface
  // implementation and triggers CreateAudioReceiverPlanB or
  // CreateVideoReceiverPlanB.
  PLAN_B_ONLY void OnRemoteSenderAddedPlanB(const RtpSenderInfo& sender_info,
                                            MediaStreamInterface* stream,
                                            MediaType media_type);

  // Triggered when a remote sender has been removed from a remote session
  // description. It removes the remote sender with id `sender_id` from a remote
  // MediaStream and triggers DestroyAudioReceiver or DestroyVideoReceiver.
  PLAN_B_ONLY void OnRemoteSenderRemovedPlanB(const RtpSenderInfo& sender_info,
                                              MediaStreamInterface* stream,
                                              MediaType media_type);

  // Triggered when a local sender has been seen for the first time in a local
  // session description.
  // This method triggers CreateAudioSender or CreateVideoSender if the rtp
  // streams in the local SessionDescription can be mapped to a MediaStreamTrack
  // in a MediaStream in `local_streams_`
  PLAN_B_ONLY void OnLocalSenderAdded(const RtpSenderInfo& sender_info,
                                      MediaType media_type);

  // Triggered when a local sender has been removed from a local session
  // description.
  // This method triggers DestroyAudioSender or DestroyVideoSender if a stream
  // has been removed from the local SessionDescription and the stream can be
  // mapped to a MediaStreamTrack in a MediaStream in `local_streams_`.
  PLAN_B_ONLY void OnLocalSenderRemoved(const RtpSenderInfo& sender_info,
                                        MediaType media_type);

  std::vector<RtpSenderInfo>* GetRemoteSenderInfos(MediaType media_type);
  std::vector<RtpSenderInfo>* GetLocalSenderInfos(MediaType media_type);

  // Return the RtpSender with the given track attached.
  scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>>
  FindSenderForTrack(MediaStreamTrackInterface* track) const;

  // Return the RtpSender with the given id, or null if none exists.
  scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>> FindSenderById(
      absl::string_view sender_id) const;

  // Return the RtpReceiver with the given id, or null if none exists.
  PLAN_B_ONLY scoped_refptr<RtpReceiverProxyWithInternal<RtpReceiverInternal>>
  FindReceiverById(absl::string_view receiver_id) const;

  TransceiverList* transceivers() { return &transceivers_; }
  const TransceiverList* transceivers() const { return &transceivers_; }

  // Plan B helpers for getting the voice/video media channels for the single
  // audio/video transceiver, if it exists.
  PLAN_B_ONLY VoiceMediaSendChannelInterface* voice_media_send_channel() const;
  PLAN_B_ONLY VideoMediaSendChannelInterface* video_media_send_channel() const;
  PLAN_B_ONLY VoiceMediaReceiveChannelInterface* voice_media_receive_channel()
      const;
  PLAN_B_ONLY VideoMediaReceiveChannelInterface* video_media_receive_channel()
      const;

  PLAN_B_ONLY RTCErrorOr<scoped_refptr<RtpSenderInterface>> AddTrackPlanB(
      scoped_refptr<MediaStreamTrackInterface> track,
      const std::vector<std::string>& stream_ids,
      const std::vector<RtpEncodingParameters>* init_send_encodings);

  // Add a new audio or video track, creating transceiver if required.
  RTCErrorOr<scoped_refptr<RtpSenderInterface>> AddTrackUnifiedPlan(
      const MediaConfig& media_config,
      const AudioOptions& audio_options,
      const VideoOptions& video_options,
      const CryptoOptions& crypto_options,
      VideoBitrateAllocatorFactory* video_bitrate_allocator_factory,
      scoped_refptr<MediaStreamTrackInterface> track,
      const std::vector<std::string>& stream_ids,
      const std::vector<RtpEncodingParameters>* init_send_encodings);

 private:
  Thread* signaling_thread() const { return context_->signaling_thread(); }
  Thread* worker_thread() const { return context_->network_thread(); }
  bool IsUnifiedPlan() const { return is_unified_plan_; }
  std::vector<RtpHeaderExtensionCapability> GetDefaultHeaderExtensions(
      MediaType media_type);
  void NoteUsageEvent(UsageEvent event) {
    usage_pattern_->NoteUsageEvent(event);
  }

  // Returns the first RtpTransceiver suitable for a newly added track, if such
  // transceiver is available.
  scoped_refptr<RtpTransceiverProxyWithInternal<RtpTransceiver>>
  FindFirstTransceiverForAddedTrack(
      scoped_refptr<MediaStreamTrackInterface> track,
      const std::vector<RtpEncodingParameters>* init_send_encodings);

  // Create an RtpReceiver that sources an audio track.
  PLAN_B_ONLY void CreateAudioReceiverPlanB(
      MediaStreamInterface* stream,
      const RtpSenderInfo& remote_sender_info) RTC_RUN_ON(signaling_thread());

  // Create an RtpReceiver that sources a video track.
  PLAN_B_ONLY void CreateVideoReceiverPlanB(
      MediaStreamInterface* stream,
      const RtpSenderInfo& remote_sender_info) RTC_RUN_ON(signaling_thread());
  PLAN_B_ONLY scoped_refptr<RtpReceiverInterface> RemoveAndStopReceiver(
      const RtpSenderInfo& remote_sender_info) RTC_RUN_ON(signaling_thread());

  void RunWithObserver(
      absl::AnyInvocable<void(PeerConnectionObserver*) &&>);  // NOLINT
  void OnNegotiationNeeded();

  const MediaEngineInterface* media_engine() const;

  UniqueRandomIdGenerator* ssrc_generator() const {
    return context_->ssrc_generator();
  }

  const Environment env_;
  TransceiverList transceivers_;

  // These lists store sender info seen in local/remote descriptions.
  std::vector<RtpSenderInfo> remote_audio_sender_infos_
      RTC_GUARDED_BY(signaling_thread());
  std::vector<RtpSenderInfo> remote_video_sender_infos_
      RTC_GUARDED_BY(signaling_thread());
  std::vector<RtpSenderInfo> local_audio_sender_infos_
      RTC_GUARDED_BY(signaling_thread());
  std::vector<RtpSenderInfo> local_video_sender_infos_
      RTC_GUARDED_BY(signaling_thread());

  bool closed_ = false;
  bool const is_unified_plan_;
  Call* const call_;
  ConnectionContext* context_;
  CodecLookupHelper* codec_lookup_helper_;
  UsagePattern* usage_pattern_;
  PeerConnectionObserver* observer_;
  LegacyStatsCollectorInterface* const legacy_stats_;
  absl::AnyInvocable<void()> on_negotiation_needed_;
  WeakPtrFactory<RtpTransmissionManager> weak_ptr_factory_
      RTC_GUARDED_BY(signaling_thread());
};

}  // namespace webrtc

#endif  // PC_RTP_TRANSMISSION_MANAGER_H_
