/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_RTP_TRANSCEIVER_H_
#define PC_RTP_TRANSCEIVER_H_

#include <stddef.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/audio_options.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/jsep.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "api/rtp_packet_infos.h"
#include "api/rtp_parameters.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/rtp_transceiver_direction.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/timestamp.h"
#include "api/video/video_bitrate_allocator_factory.h"
#include "media/base/codec.h"
#include "media/base/media_channel.h"
#include "media/base/media_config.h"
#include "media/base/media_engine.h"
#include "media/base/stream_params.h"
#include "pc/channel_interface.h"
#include "pc/codec_vendor.h"
#include "pc/connection_context.h"
#include "pc/legacy_stats_collector_interface.h"
#include "pc/proxy.h"
#include "pc/rtp_receiver.h"
#include "pc/rtp_receiver_proxy.h"
#include "pc/rtp_sender.h"
#include "pc/rtp_sender_proxy.h"
#include "pc/rtp_transport_internal.h"
#include "pc/scoped_operations_batcher.h"
#include "pc/session_description.h"
#include "pc/simulcast_description.h"
#include "rtc_base/checks.h"
#include "rtc_base/network_route.h"
#include "rtc_base/system/plan_b_only.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {

class DtlsTransport;

class PeerConnectionSdpMethods;
class ScopedOperationsBatcher;

// Implementation of the public RtpTransceiverInterface.
//
// The RtpTransceiverInterface is only intended to be used with a PeerConnection
// that enables Unified Plan SDP. Thus, the methods that only need to implement
// public API features and are not used internally can assume exactly one sender
// and receiver.
//
// Since the RtpTransceiver is used internally by PeerConnection for tracking
// RtpSenders, RtpReceivers, and BaseChannels, and PeerConnection needs to be
// backwards compatible with Plan B SDP, this implementation is more flexible
// than that required by the WebRTC specification.
//
// With Plan B SDP, an RtpTransceiver can have any number of senders and
// receivers which map to a=ssrc lines in the m= section.
// With Unified Plan SDP, an RtpTransceiver will have exactly one sender and one
// receiver which are encapsulated by the m= section.
//
// This class manages the RtpSenders, RtpReceivers, and BaseChannel associated
// with this m= section. Since the transceiver, senders, and receivers are
// reference counted and can be referenced from JavaScript (in Chromium), these
// objects must be ready to live for an arbitrary amount of time. The
// BaseChannel is not reference counted, so
// the PeerConnection must take care of creating/deleting the BaseChannel.
//
// The RtpTransceiver is specialized to either audio or video according to the
// MediaType specified in the constructor. Audio RtpTransceivers will have
// AudioRtpSenders, AudioRtpReceivers, and a VoiceChannel. Video RtpTransceivers
// will have VideoRtpSenders, VideoRtpReceivers, and a VideoChannel.
class RtpTransceiver : public RtpTransceiverInterface {
 public:
  // Construct a Plan B-style RtpTransceiver with no senders, receivers, or
  // channel set.
  // `media_type` specifies the type of RtpTransceiver (and, by transitivity,
  // the type of senders, receivers, and channel). Can either by audio or video.
  // This should be PLAN_B_ONLY; but this marking is deferred due to templating
  // issues
  RtpTransceiver(const Environment& env,
                 MediaType media_type,
                 ConnectionContext* context,
                 CodecLookupHelper* codec_lookup_helper,
                 LegacyStatsCollectorInterface* legacy_stats,
                 const AudioOptions& audio_options,
                 const VideoOptions& video_options);
  // Construct a Unified Plan-style RtpTransceiver with the given sender and
  // receiver. The media type will be derived from the media types of the sender
  // and receiver. The sender and receiver should have the same media type.
  // `HeaderExtensionsToNegotiate` is used for initializing the return value of
  // HeaderExtensionsToNegotiate().
  RtpTransceiver(
      const Environment& env,
      scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>> sender,
      scoped_refptr<RtpReceiverProxyWithInternal<RtpReceiverInternal>> receiver,
      ConnectionContext* context,
      CodecLookupHelper* codec_lookup_helper,
      std::vector<RtpHeaderExtensionCapability> HeaderExtensionsToNegotiate,
      absl::AnyInvocable<void()> on_negotiation_needed,
      const AudioOptions& audio_options,
      const VideoOptions& video_options);
  RtpTransceiver(
      const Environment& env,
      Call* call,
      const MediaConfig& media_config,
      absl::string_view sender_id,
      absl::string_view receiver_id,
      MediaType media_type,
      scoped_refptr<MediaStreamTrackInterface> track,
      const std::vector<std::string>& stream_ids,
      const std::vector<RtpEncodingParameters>& init_send_encodings,
      ConnectionContext* context,
      CodecLookupHelper* codec_lookup_helper,
      LegacyStatsCollectorInterface* legacy_stats,
      RtpSenderBase::SetStreamsObserver* set_streams_observer,
      const AudioOptions& audio_options,
      const VideoOptions& video_options,
      const CryptoOptions& crypto_options,
      VideoBitrateAllocatorFactory* video_bitrate_allocator_factory,
      std::vector<RtpHeaderExtensionCapability> header_extensions_to_negotiate,
      bool simulcast_rejected,
      const std::vector<SimulcastLayer>& initial_simulcast_layers,
      absl::AnyInvocable<void()> on_negotiation_needed);
  ~RtpTransceiver() override;

  // Not copyable or movable.
  RtpTransceiver(const RtpTransceiver&) = delete;
  RtpTransceiver& operator=(const RtpTransceiver&) = delete;
  RtpTransceiver(RtpTransceiver&&) = delete;
  RtpTransceiver& operator=(RtpTransceiver&&) = delete;

  // Creates the Voice/VideoChannel and sets it.
  // Note: Tasks added to `worker_tasks` must be executed before tasks added to
  // `network_batcher`.
  void CreateChannel(
      absl::string_view mid,
      Call* call_ptr,
      const MediaConfig& media_config,
      bool srtp_required,
      CryptoOptions crypto_options,
      VideoBitrateAllocatorFactory* video_bitrate_allocator_factory,
      absl::AnyInvocable<RtpTransportInternal*() &&> transport_lookup,
      ScopedOperationsBatcher& worker_tasks,
      ScopedOperationsBatcher& network_tasks);

  // Sets the Voice/VideoChannel. The caller must pass in the correct channel
  // implementation based on the type of the transceiver.  The call must
  // furthermore be made on the signaling thread.
  //
  // `channel`: The channel instance to be associated with the transceiver.
  //     This must be a valid pointer.
  //     The state of the object
  //     is expected to be newly constructed and not initalized for network
  //     activity (see next parameter for more).
  //
  //     The transceiver takes ownership of `channel`.
  //
  // `transport_lookup`: This
  //     callback function will be used to look up the `RtpTransport` object
  //     to associate with the channel via `BaseChannel::SetRtpTransport`.
  //     The lookup function will be called on the network thread, synchronously
  //     during the call to `SetChannel`.  This means that the caller of
  //     `SetChannel()` may provide a callback function that references state
  //     that exists within the calling scope of SetChannel (e.g. a variable
  //     on the stack).
  //     The reason for this design is to limit the number of times we jump
  //     synchronously to the network thread from the signaling thread.
  //     The callback allows us to combine the transport lookup with network
  //     state initialization of the channel object.
  // ClearChannel() must be used before calling SetChannel() again.
  RTCError SetChannelForTest(
      std::unique_ptr<ChannelInterface> channel,
      absl::AnyInvocable<RtpTransportInternal*() &&> transport_lookup = []() {
        return nullptr;
      });

  // Clear the association between the transceiver and the channel.
  void ClearChannel();

  // Returns a task that clears the channel's network related state.
  // The task must be executed on the network thread.
  // This is used by SdpOfferAnswerHandler::GetMediaChannelTeardownTasks to
  // batch network thread operations.
  absl::AnyInvocable<void() &&> GetClearChannelNetworkTask();

  // Returns a task that deletes the channel.
  // The task must be executed on the network thread.
  // This is used by SdpOfferAnswerHandler::GetMediaChannelTeardownTasks to
  // batch network thread operations.
  absl::AnyInvocable<void() &&> GetDeleteChannelWorkerTask(bool stop_senders);

  // Adds an RtpSender of the appropriate type to be owned by this transceiver.
  PLAN_B_ONLY scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>>
  AddSenderPlanB(scoped_refptr<MediaStreamTrackInterface> track,
                 absl::string_view sender_id,
                 const std::vector<std::string>& stream_ids,
                 const std::vector<RtpEncodingParameters>& send_encodings);

  // Adds an RtpSender of the appropriate type to be owned by this transceiver.
  // Must not be null.
  PLAN_B_ONLY void AddSenderPlanB(
      scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>> sender);

  // Removes the given RtpSender. Returns false if the sender is not owned by
  // this transceiver.
  PLAN_B_ONLY bool RemoveSenderPlanB(RtpSenderInterface* sender);

  // Returns a vector of the senders owned by this transceiver.
  PLAN_B_ONLY const
      std::vector<scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>>>&
      senders() const {
    return senders_;
  }

  // Adds an RtpReceiver of the appropriate type to be owned by this
  // transceiver. Must not be null.
  PLAN_B_ONLY void AddReceiverPlanB(
      scoped_refptr<RtpReceiverProxyWithInternal<RtpReceiverInternal>>
          receiver);

  // Removes the given RtpReceiver. Returns false if the receiver is not owned
  // by this transceiver.
  PLAN_B_ONLY bool RemoveReceiverPlanB(RtpReceiverInterface* receiver);

  // Returns a vector of the receivers owned by this transceiver.
  PLAN_B_ONLY const std::vector<
      scoped_refptr<RtpReceiverProxyWithInternal<RtpReceiverInternal>>>&
  receivers() const {
    return receivers_;
  }

  // Returns the backing object for the transceiver's Unified Plan sender.
  scoped_refptr<RtpSenderInternal> sender_internal() const;

  // Returns the backing object for the transceiver's Unified Plan receiver.
  scoped_refptr<RtpReceiverInternal> receiver_internal() const;

  // RtpTransceivers are not associated until they have a corresponding media
  // section set in SetLocalDescription or SetRemoteDescription. Therefore,
  // when setting a local offer we need a way to remember which transceiver was
  // used to create which media section in the offer. Storing the mline index
  // in CreateOffer is specified in JSEP to allow us to do that.
  std::optional<size_t> mline_index() const { return mline_index_; }
  void set_mline_index(std::optional<size_t> mline_index) {
    mline_index_ = mline_index;
  }

  const std::optional<std::string>& transport_name() const {
    RTC_DCHECK_RUN_ON(thread_);
    RTC_DCHECK(!transport_name_ || HasChannel());
    return transport_name_;
  }

  // Sets or clears the transport for the sender and receiver.
  // This method is assumed to be called in tandem with transport changes being
  // applied to the channel. Must be called on the signaling thread.
  void SetTransport(scoped_refptr<DtlsTransport> transport,
                    std::optional<std::string> transport_name);

  // Sets the MID for this transceiver. If the MID is not null, then the
  // transceiver is considered "associated" with the media section that has the
  // same MID.
  void set_mid(const std::optional<std::string>& mid) { mid_ = std::move(mid); }

  // Sets the intended direction for this transceiver. Intended to be used
  // internally over SetDirection since this does not trigger a negotiation
  // needed callback.
  void set_direction(RtpTransceiverDirection direction) {
    direction_ = direction;
  }

  // Sets the current direction for this transceiver as negotiated in an offer/
  // answer exchange. The current direction is null before an answer with this
  // transceiver has been set.
  void set_current_direction(RtpTransceiverDirection direction);

  // Sets the fired direction for this transceiver. The fired direction is null
  // until SetRemoteDescription is called or an answer is set (either local or
  // remote) after which the only valid reason to go back to null is rollback.
  void set_fired_direction(std::optional<RtpTransceiverDirection> direction);

  void set_receptive(bool receptive);

  // According to JSEP rules for SetRemoteDescription, RtpTransceivers can be
  // reused only if they were added by AddTrack.
  void set_created_by_addtrack(bool created_by_addtrack) {
    created_by_addtrack_ = created_by_addtrack;
  }
  // If AddTrack has been called then transceiver can't be removed during
  // rollback.
  void set_reused_for_addtrack(bool reused_for_addtrack) {
    reused_for_addtrack_ = reused_for_addtrack;
  }

  bool created_by_addtrack() const { return created_by_addtrack_; }

  bool reused_for_addtrack() const { return reused_for_addtrack_; }

  // Returns true if this transceiver has ever had the current direction set to
  // sendonly or sendrecv.
  bool has_ever_been_used_to_send() const {
    return has_ever_been_used_to_send_;
  }

  // Executes the "stop the RTCRtpTransceiver" procedure from
  // the webrtc-pc specification, described under the stop() method.
  // The task must be executed on the network thread.
  // This is used by SdpOfferAnswerHandler to batch network thread operations.
  [[nodiscard]] absl_nullable absl::AnyInvocable<void() &&>
  GetStopTransceiverProcedure();

  // Returns a task that stops the transceiver.
  // The task must be executed on the network thread.
  // This is used by SdpOfferAnswerHandler to batch network thread operations.
  ScopedOperationsBatcher::BatchTaskWithFinalizer StopStandardAsync();

  // RtpTransceiverInterface implementation.
  MediaType media_type() const override;
  std::optional<std::string> mid() const override;
  absl_nonnull scoped_refptr<RtpSenderInterface> sender() const override;
  absl_nonnull scoped_refptr<RtpReceiverInterface> receiver() const override;

  bool stopped() const override;
  bool stopping() const override;
  RtpTransceiverDirection direction() const override;
  RTCError SetDirectionWithError(
      RtpTransceiverDirection new_direction) override;
  std::optional<RtpTransceiverDirection> current_direction() const override;
  std::optional<RtpTransceiverDirection> fired_direction() const override;
  // Records the user's intent to use Sframe and fires negotiation needed.
  // Triggered by the sender/receiver when
  // CreateSframeEncryptorOrError/CreateSframeDecryptorOrError is called.
  // Returns an error if Sframe has already been locked to false by a
  // completed negotiation.
  RTCError TryToEnableSframe();
  // Called from SdpOfferAnswerHandler to apply the Sframe state derived
  // from the media description. Used when applying local descriptions
  // (offers and answers) to sync the transceiver, and when associating
  // new transceivers created from remote offers.
  void ApplySframeEnabled(bool sframe_enabled);
  // Returns the current Sframe state.
  std::optional<bool> SframeEnabled() const override;
  bool receptive() const override;
  RTCError StopStandard() override;
  void StopInternal() override;
  RTCError SetCodecPreferences(std::span<RtpCodecCapability> codecs) override;
  // TODO(https://crbug.com/webrtc/391275081): Delete codec_preferences() in
  // favor of filtered_codec_preferences() because it's not used anywhere.
  std::vector<RtpCodecCapability> codec_preferences() const override;
  // A direction()-filtered view of codec_preferences(). If this filtering
  // results in not having any media codecs, an empty list is returned to mean
  // "no preferences".
  std::vector<RtpCodecCapability> filtered_codec_preferences() const;
  std::vector<RtpHeaderExtensionCapability> GetHeaderExtensionsToNegotiate()
      const override;
  std::vector<RtpHeaderExtensionCapability> GetNegotiatedHeaderExtensions()
      const override;

  RTCError SetHeaderExtensionsToNegotiate(
      std::span<const RtpHeaderExtensionCapability> header_extensions) override;

  // Called on the signaling thread when the local or remote content description
  // is updated. Used to update the negotiated header extensions.
  // TODO(tommi): The implementation of this method is currently very simple and
  // only used for updating the negotiated headers. However, we're planning to
  // move all the updates done on the channel from the transceiver into this
  // method. This will happen with the ownership of the channel object being
  // moved into the transceiver.
  void OnNegotiationUpdate(SdpType sdp_type,
                           const MediaContentDescription* content);

  // Wrappers for ChannelInterface
  bool HasChannel() const {
    RTC_DCHECK_RUN_ON(thread_);
    return channel_ != nullptr;
  }

  bool SetRtpTransport(RtpTransportInternal* rtp_transport);

  // Configures the channel with local content description.
  // Pushes a multi-stage execution task into the provided
  // `ScopedOperationsBatcher`. See `SetChannelContent` for details on the
  // execution model.
  void SetChannelLocalContent(const MediaContentDescription* content,
                              SdpType type,
                              ScopedOperationsBatcher& batcher);
  // Configures the channel with remote content description.
  // Pushes a multi-stage execution task into the provided
  // `ScopedOperationsBatcher`. See `SetChannelContent` for details on the
  // execution model.
  void SetChannelRemoteContent(const MediaContentDescription* content,
                               SdpType type,
                               ScopedOperationsBatcher& batcher);
  void EnableChannel(bool enable);

  const std::vector<StreamParams>& channel_local_streams() const;
  const std::vector<StreamParams>& channel_remote_streams() const;
  absl::string_view channel_transport_name() const;
  // Returns the MID that the channel was created for, or an empty string if
  // there is no channel. The channel stays bound to the transport of this MID
  // for its lifetime. Must be called on the network thread.
  absl::string_view channel_mid() const;

  // Accessors for media channels. These return null if there is no channel.
  MediaSendChannelInterface* media_send_channel();
  const MediaSendChannelInterface* media_send_channel() const;
  MediaReceiveChannelInterface* media_receive_channel();
  const MediaReceiveChannelInterface* media_receive_channel() const;
  VideoMediaSendChannelInterface* video_media_send_channel();
  VoiceMediaSendChannelInterface* voice_media_send_channel();
  VideoMediaReceiveChannelInterface* video_media_receive_channel();
  VoiceMediaReceiveChannelInterface* voice_media_receive_channel();

 private:
  void OnFrameDeliveredOnSignalingThread(uint32_t ssrc,
                                         const RtpPacketInfos& infos,
                                         Timestamp timestamp);
  absl::AnyInvocable<void(uint32_t ssrc, const RtpPacketInfos&, Timestamp)
                         const>
  GetOnFrameDeliveredCallback();

  VoiceChannelFactoryInterface* voice_channel_factory() const {
    return context_->voice_channel_factory();
  }
  VideoChannelFactoryInterface* video_channel_factory() const {
    return context_->video_channel_factory();
  }
  ConnectionContext* context() const { return context_; }
  CodecVendor& codec_vendor() {
    return *codec_lookup_helper_->GetCodecVendor();
  }
  std::vector<Codec> GetSendCodecs();
  void OnFirstPacketReceived_s(uint32_t ssrc);
  void OnPacketReceived(uint32_t ssrc,
                        scoped_refptr<PendingTaskSafetyFlag> safety)
      RTC_RUN_ON(context()->network_thread());
  void OnFirstPacketSent();
  RTCErrorOr<std::optional<std::string>> InitializeOnNetworkThread(
      absl::AnyInvocable<RtpTransportInternal*() &&> transport_lookup)
      RTC_RUN_ON(context()->network_thread());
  void OnNetworkRouteChanged(ChannelInterface* channel,
                             std::optional<NetworkRoute> network_route)
      RTC_RUN_ON(context()->network_thread());
  void ClearRtpTransportState() RTC_RUN_ON(context()->network_thread());
  void SetRtpTransportState(RtpTransportInternal* transport)
      RTC_RUN_ON(context()->network_thread());

  // Stops the receivers synchronously and returns a task that stops the
  // senders. The returned task must be executed on the network thread.
  [[nodiscard]] absl_nonnull absl::AnyInvocable<void() &&>
  GetStopSendingAndReceiving();

  void SetMediaChannels(MediaSendChannelInterface* send,
                        MediaReceiveChannelInterface* receive)
      RTC_RUN_ON(context()->network_thread());
  void ClearMediaChannelReferences() RTC_RUN_ON(context()->network_thread());

  VideoMediaSendChannelInterface::EncoderSwitchRequestCallback
  GetEncoderSwitchRequestCallback();

  absl::AnyInvocable<void()> GetParametersChangedCallback();

  RTCError UpdateCodecPreferencesCaches(
      const std::vector<RtpCodecCapability>& codecs);
  // Helper function for handling extensions during O/A
  std::vector<RtpHeaderExtensionCapability>
  GetOfferedAndImplementedHeaderExtensions(
      const MediaContentDescription* content) const;

  // Configures the channel with the provided content description.
  // Pushes a multi-stage execution task into the provided
  // `ScopedOperationsBatcher`.
  //
  //  Signaling Thread                   Network Thread
  //  +-------------------+              +-------------------+
  //  | SetChannelContent |              |                   |
  //  | - Capture SSRC    |              |                   |
  //  | - Push Outer -----|------------->| [Outer Lambda]    |
  //  |                   |              | - Run set_content |
  //  |                   |              | - GetRtpSendParams|
  //  | [Inner Lambda] <--|--------------| - Return Inner    |
  //  | - SetCachedParams |              |                   |
  //  +-------------------+              +-------------------+
  //
  // Execution Stages:
  // 1. **Preparation (Signaling Thread)**: Called when this method is invoked.
  //    Captures current sender SSRC and cached parameters.
  //
  // 2. **Execution (Network Thread)**: The pushed outer lambda is executed
  //    by the ScopedOperationsBatcher on the network thread. Runs
  //    `set_content()`. If successful, fetches updated `RtpSendParameters`
  //    from the media channel and returns an inner completion lambda.
  //
  // 3. **Completion (Signaling Thread)**: The inner lambda returned by the
  //    network thread task is collected by `ScopedOperationsBatcher::Run()`.
  //    After all batched operations on the network thread are complete,
  //    `Run()` executes these inner lambdas on the thread that called
  //    `Run()` (in this case, the Signaling Thread). This stage updates
  //    the cached parameters on the senders.
  void SetChannelContent(absl::AnyInvocable<RTCError() &&> set_content,
                         ScopedOperationsBatcher& batcher);

  const Environment env_;
  // Enforce that this object is created, used and destroyed on one thread.
  // This TQ typically represents the signaling thread.
  TaskQueueBase* const thread_;
  const bool unified_plan_;
  const MediaType media_type_;
  scoped_refptr<PendingTaskSafetyFlag> signaling_thread_safety_;
  scoped_refptr<PendingTaskSafetyFlag> network_thread_safety_;
  std::vector<scoped_refptr<RtpSenderProxyWithInternal<RtpSenderInternal>>>
      senders_;
  std::vector<scoped_refptr<RtpReceiverProxyWithInternal<RtpReceiverInternal>>>
      receivers_;

  bool stopped_ RTC_GUARDED_BY(thread_) = false;
  bool stopping_ RTC_GUARDED_BY(thread_) = false;
  RtpTransceiverDirection direction_ = RtpTransceiverDirection::kInactive;
  std::optional<RtpTransceiverDirection> current_direction_;
  std::optional<RtpTransceiverDirection> fired_direction_;
  std::optional<bool> sframe_enabled_ RTC_GUARDED_BY(thread_) = std::nullopt;
  std::optional<std::string> mid_;
  std::optional<std::string> transport_name_ RTC_GUARDED_BY(thread_) =
      std::nullopt;
  std::optional<size_t> mline_index_;
  bool created_by_addtrack_ = false;
  bool reused_for_addtrack_ = false;
  bool has_ever_been_used_to_send_ = false;
  bool receptive_ RTC_GUARDED_BY(thread_) = false;
  bool receptive_n_ RTC_GUARDED_BY(context()->network_thread()) = false;
  bool packet_notified_after_receptive_
      RTC_GUARDED_BY(context()->network_thread()) = false;
  RtpTransportInternal* rtp_transport_
      RTC_GUARDED_BY(context()->network_thread()) = nullptr;

  // Accessed on both thread_ and the network thread. Considered safe
  // because all access on the network thread is within an invoke()
  // from thread_.
  std::unique_ptr<ChannelInterface> channel_ = nullptr;
  ConnectionContext* const context_;
  CodecLookupHelper* const codec_lookup_helper_;
  LegacyStatsCollectorInterface* const legacy_stats_;
  RtpSenderBase::SetStreamsObserver* const set_streams_observer_ = nullptr;
  std::vector<RtpCodecCapability> codec_preferences_;
  std::vector<RtpCodecCapability> sendrecv_codec_preferences_;
  std::vector<RtpCodecCapability> sendonly_codec_preferences_;
  std::vector<RtpCodecCapability> recvonly_codec_preferences_;
  std::vector<RtpHeaderExtensionCapability> header_extensions_to_negotiate_
      RTC_GUARDED_BY(thread_);
  std::vector<RtpHeaderExtensionCapability> header_extensions_for_rollback_
      RTC_GUARDED_BY(thread_);

  // `negotiated_header_extensions_` is read and written to on the signaling
  // thread from the SdpOfferAnswerHandler class (e.g.
  // PushdownMediaDescription().
  RtpHeaderExtensions negotiated_header_extensions_ RTC_GUARDED_BY(thread_);

  absl::AnyInvocable<void()> on_negotiation_needed_;
  std::unique_ptr<MediaSendChannelInterface> owned_send_channel_;
  std::unique_ptr<MediaReceiveChannelInterface> owned_receive_channel_;
  const AudioOptions audio_options_;
  const VideoOptions video_options_;
};

BEGIN_PRIMARY_PROXY_MAP(RtpTransceiver)

PROXY_PRIMARY_THREAD_DESTRUCTOR()
BYPASS_PROXY_CONSTMETHOD0(webrtc::MediaType, media_type)
PROXY_CONSTMETHOD0(std::optional<std::string>, mid)
PROXY_CONSTMETHOD0(scoped_refptr<RtpSenderInterface>, sender)
PROXY_CONSTMETHOD0(scoped_refptr<RtpReceiverInterface>, receiver)
PROXY_CONSTMETHOD0(bool, stopped)
PROXY_CONSTMETHOD0(bool, stopping)
PROXY_CONSTMETHOD0(RtpTransceiverDirection, direction)
PROXY_METHOD1(RTCError, SetDirectionWithError, RtpTransceiverDirection)
PROXY_CONSTMETHOD0(std::optional<RtpTransceiverDirection>, current_direction)
PROXY_CONSTMETHOD0(std::optional<RtpTransceiverDirection>, fired_direction)
PROXY_CONSTMETHOD0(bool, receptive)
PROXY_METHOD0(RTCError, StopStandard)
PROXY_METHOD0(void, StopInternal)
PROXY_METHOD1(RTCError, SetCodecPreferences, std::span<RtpCodecCapability>)
PROXY_CONSTMETHOD0(std::vector<RtpCodecCapability>, codec_preferences)
PROXY_CONSTMETHOD0(std::vector<RtpHeaderExtensionCapability>,
                   GetHeaderExtensionsToNegotiate)
PROXY_CONSTMETHOD0(std::vector<RtpHeaderExtensionCapability>,
                   GetNegotiatedHeaderExtensions)
PROXY_METHOD1(RTCError,
              SetHeaderExtensionsToNegotiate,
              std::span<const RtpHeaderExtensionCapability>)
PROXY_CONSTMETHOD0(std::optional<bool>, SframeEnabled)
END_PROXY_MAP(RtpTransceiver)

}  // namespace webrtc

#endif  // PC_RTP_TRANSCEIVER_H_
