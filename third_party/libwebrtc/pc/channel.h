/*
 *  Copyright 2004 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_CHANNEL_H_
#define PC_CHANNEL_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/crypto/crypto_options.h"
#include "api/jsep.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_direction.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "call/rtp_demuxer.h"
#include "call/rtp_packet_sink_interface.h"
#include "media/base/media_channel.h"
#include "media/base/stream_params.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "pc/channel_interface.h"
#include "pc/rtp_transport_internal.h"
#include "pc/session_description.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/checks.h"
#include "rtc_base/containers/flat_set.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/socket.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/unique_id_generator.h"

namespace webrtc {

// BaseChannel contains logic common to voice and video, including enable,
// connection and media monitors.
//
// All methods with _n suffix must be called on the network thread,
// and methods with _s suffix on the signaling thread.
//
class BaseChannel : public ChannelInterface,
                    public MediaChannelNetworkInterface,
                    public RtpPacketSinkInterface {
 public:
  // If `srtp_required` is true, the channel will not send or receive any
  // RTP/RTCP packets without using SRTP (either using SDES or DTLS-SRTP).
  // The BaseChannel does not own the UniqueRandomIdGenerator so it is the
  // responsibility of the user to ensure it outlives this object.
  // TODO(zhihuang:) Create a BaseChannel::Config struct for the parameter lists
  // which will make it easier to change the constructor.

  // Constructor for use when the MediaChannels are split
  BaseChannel(
      Thread* absl_nonnull network_thread,
      TaskQueueBase* absl_nonnull signaling_thread,
      std::unique_ptr<MediaSendChannelInterface> media_send_channel,
      std::unique_ptr<MediaReceiveChannelInterface> media_receive_channel,
      absl::string_view mid,
      MediaType media_type,
      bool srtp_required,
      CryptoOptions crypto_options,
      UniqueRandomIdGenerator* absl_nonnull ssrc_generator,
      ChannelCallbacks callbacks = {});
  ~BaseChannel() override;

  const std::string& mid() const override { return mid_; }
  MediaType media_type() const override { return media_type_; }
  // TODO(deadbeef): This is redundant; remove this.
  absl::string_view transport_name() const override {
    RTC_DCHECK_RUN_ON(network_thread_);
    if (rtp_transport_)
      return rtp_transport_->transport_name();
    return "";
  }

  // This function returns true if using SRTP (DTLS-based keying or SDES).
  bool srtp_active() const {
    RTC_DCHECK_RUN_ON(network_thread_);
    return rtp_transport_ && rtp_transport_->IsSrtpActive();
  }

  // Set an RTP level transport which could be an RtpTransport without
  // encryption, an SrtpTransport for SDES or a DtlsSrtpTransport for DTLS-SRTP.
  // Must be called on the network thread. It would replace the `SetTransports`
  // and its variants.
  bool SetRtpTransport(RtpTransportInternal* rtp_transport) override;

  RtpTransportInternal* rtp_transport() const {
    RTC_DCHECK_RUN_ON(network_thread_);
    return rtp_transport_;
  }

  // Channel control. Must be called on the network thread.
  RTCError SetLocalContent(const MediaContentDescription* content,
                           SdpType type) override;
  RTCError SetRemoteContent(const MediaContentDescription* content,
                            SdpType type) override;

  void Enable(bool enable) override;
  void EnableSframe() override;

  const std::vector<StreamParams>& local_streams() const override {
    return local_streams_;
  }
  const std::vector<StreamParams>& remote_streams() const override {
    return remote_streams_;
  }

  // From RtpTransport - public for testing only
  void OnTransportReadyToSend(bool ready);

  // Only public for unit tests.  Otherwise, consider protected.
  int SetOption(SocketType type, Socket::Option o, int val) override;

  // RtpPacketSinkInterface overrides.
  void OnRtpPacket(const RtpPacketReceived& packet) override;

  MediaSendChannelInterface* media_send_channel() override {
    return media_send_channel_.get();
  }
  MediaReceiveChannelInterface* media_receive_channel() override {
    return media_receive_channel_.get();
  }

  VideoMediaSendChannelInterface* video_media_send_channel() override {
    RTC_CHECK_EQ(media_type_, MediaType::VIDEO);
    return media_send_channel_->AsVideoSendChannel();
  }
  VoiceMediaSendChannelInterface* voice_media_send_channel() override {
    RTC_CHECK_EQ(media_type_, MediaType::AUDIO);
    return media_send_channel_->AsVoiceSendChannel();
  }
  VideoMediaReceiveChannelInterface* video_media_receive_channel() override {
    RTC_CHECK_EQ(media_type_, MediaType::VIDEO);
    return media_receive_channel_->AsVideoReceiveChannel();
  }
  VoiceMediaReceiveChannelInterface* voice_media_receive_channel() override {
    RTC_CHECK_EQ(media_type_, MediaType::AUDIO);
    return media_receive_channel_->AsVoiceReceiveChannel();
  }

 protected:
  void set_local_content_direction(RtpTransceiverDirection direction)
      RTC_RUN_ON(network_thread_) {
    local_content_direction_ = direction;
  }

  RtpTransceiverDirection local_content_direction() const
      RTC_RUN_ON(network_thread_) {
    return local_content_direction_;
  }

  void set_remote_content_direction(RtpTransceiverDirection direction)
      RTC_RUN_ON(network_thread_) {
    remote_content_direction_ = direction;
  }

  RtpTransceiverDirection remote_content_direction() const
      RTC_RUN_ON(network_thread_) {
    return remote_content_direction_;
  }

  RtpExtension::Filter extensions_filter() const { return extensions_filter_; }

  bool network_initialized() RTC_RUN_ON(network_thread_) {
    return media_send_channel()->HasNetworkInterface();
  }

  bool enabled() const RTC_RUN_ON(network_thread_) { return enabled_; }
  TaskQueueBase* signaling_thread() const { return signaling_thread_; }

  // Call to verify that:
  // * The required content description directions have been set.
  // * The channel is enabled.
  // * The SRTP filter is active if it's needed.
  // * The transport has been writable before, meaning it should be at least
  //   possible to succeed in sending a packet.
  //
  // When any of these properties change, UpdateMediaSendRecvState_n should be
  // called.
  bool IsReadyToSendMedia_n() const RTC_RUN_ON(network_thread_);

  // NetworkInterface implementation, called by MediaEngine
  bool SendPacket(CopyOnWriteBuffer* packet,
                  const AsyncSocketPacketOptions& options) override;
  bool SendRtcp(CopyOnWriteBuffer* packet,
                const AsyncSocketPacketOptions& options) override;

  // From RtpTransportInternal
  void OnWritableState(bool writable);

  bool SendPacket(bool rtcp,
                  CopyOnWriteBuffer* packet,
                  const AsyncSocketPacketOptions& options);

  void EnableMedia_n() RTC_RUN_ON(network_thread_);
  void DisableMedia_n() RTC_RUN_ON(network_thread_);

  // Performs actions if the RTP/RTCP writable state changed. This should
  // be called whenever a channel's writable state changes or when RTCP muxing
  // becomes active/inactive.
  void UpdateWritableState_n() RTC_RUN_ON(network_thread_);
  void ChannelWritable_n() RTC_RUN_ON(network_thread_);
  void ChannelNotWritable_n() RTC_RUN_ON(network_thread_);

  // Should be called whenever the conditions for
  // IsReadyToReceiveMedia/IsReadyToSendMedia are satisfied (or unsatisfied).
  // Updates the send/recv state of the media channel.
  void UpdateMediaSendRecvState_n() RTC_RUN_ON(network_thread_);

  RTCError UpdateLocalStreams_n(const std::vector<StreamParams>& streams,
                                SdpType type) RTC_RUN_ON(network_thread_);
  RTCError UpdateRemoteStreams_n(const MediaContentDescription* content,
                                 SdpType type) RTC_RUN_ON(network_thread_);

  // Returns a list of RTP header extensions where any extension URI is unique.
  // Encrypted extensions will be either preferred or discarded, depending on
  // the current crypto_options_.
  RtpHeaderExtensions GetDeduplicatedRtpHeaderExtensions(
      const RtpHeaderExtensions& extensions);

  // Returns `true` if either an update wasn't needed or one was successfully
  // applied. If the return value is `false`, then updating the demuxer criteria
  // failed, which needs to be treated as an error.
  RTCError MaybeUpdateDemuxerAndRtpExtensions_n(
      bool update_demuxer,
      std::optional<flat_set<uint8_t>> payload_types,
      const RtpHeaderExtensions& extensions,
      std::optional<flat_set<uint32_t>> ssrcs) RTC_RUN_ON(network_thread_);

  // Registers a demuxer criteria with the transport, on the network thread.
  // This function will fail if there's no transport of if a sink is already
  // registered for this channel's demuxer_critera().
  bool RegisterRtpDemuxerSink_n(const MediaContentDescription* content,
                                std::vector<uint32_t> removed_ssrcs = {})
      RTC_RUN_ON(network_thread_);

  // Return description of media channel to facilitate logging
  std::string ToString() const;

  const std::unique_ptr<MediaSendChannelInterface> media_send_channel_;
  const std::unique_ptr<MediaReceiveChannelInterface> media_receive_channel_;

 private:
  bool ConnectToRtpTransport_n(RtpTransportInternal* rtp_transport)
      RTC_RUN_ON(network_thread_);
  void DisconnectFromRtpTransport_n(bool permanent_teardown)
      RTC_RUN_ON(network_thread_);
  void SignalSentPacket_n(const SentPacketInfo& sent_packet);
  // Only called on the network thread.
  RtpDemuxerCriteria demuxer_criteria() const RTC_RUN_ON(network_thread_);

  Thread* const network_thread_;
  TaskQueueBase* const signaling_thread_;
  scoped_refptr<PendingTaskSafetyFlag> alive_;

  // The functions are deleted after they have been called.
  absl::AnyInvocable<void() &&> on_first_packet_sent_
      RTC_GUARDED_BY(network_thread_);

  // Used to unmute.
  absl::AnyInvocable<void(const RtpPacketReceived&)> on_packet_received_n_
      RTC_GUARDED_BY(network_thread_);

  RtpTransportInternal* rtp_transport_ RTC_GUARDED_BY(network_thread_) =
      nullptr;

  std::vector<std::pair<Socket::Option, int>> socket_options_
      RTC_GUARDED_BY(network_thread_);
  std::vector<std::pair<Socket::Option, int>> rtcp_socket_options_
      RTC_GUARDED_BY(network_thread_);
  bool writable_ RTC_GUARDED_BY(network_thread_) = false;
  bool was_ever_writable_ RTC_GUARDED_BY(network_thread_) = false;
  const bool srtp_required_ = true;

  // Set to either kPreferEncryptedExtension or kDiscardEncryptedExtension
  // based on the supplied CryptoOptions.
  const RtpExtension::Filter extensions_filter_;

  bool enabled_ RTC_GUARDED_BY(network_thread_) = false;
  bool enabled_s_ RTC_GUARDED_BY(signaling_thread()) = false;
  std::vector<StreamParams> local_streams_ RTC_GUARDED_BY(network_thread_);
  std::vector<StreamParams> remote_streams_ RTC_GUARDED_BY(network_thread_);
  RtpTransceiverDirection local_content_direction_
      RTC_GUARDED_BY(network_thread_) = RtpTransceiverDirection::kInactive;
  RtpTransceiverDirection remote_content_direction_
      RTC_GUARDED_BY(network_thread_) = RtpTransceiverDirection::kInactive;

  // Cached list of payload types, used if payload type demuxing is re-enabled.
  flat_set<uint8_t> payload_types_ RTC_GUARDED_BY(network_thread_);

  const std::string mid_;
  flat_set<uint32_t> ssrcs_ RTC_GUARDED_BY(network_thread_);

  using ReceiverParamsVariant =
      std::variant<AudioReceiverParameters, VideoReceiverParameters>;
  using SenderParamsVariant =
      std::variant<AudioSenderParameter, VideoSenderParameters>;

  ReceiverParamsVariant last_recv_params_;
  SenderParamsVariant last_send_params_;
  const MediaType media_type_;
  // This generator is used to generate SSRCs for local streams.
  // This is needed in cases where SSRCs are not negotiated or set explicitly
  // like in Simulcast.
  // This object is not owned by the channel so it must outlive it.
  UniqueRandomIdGenerator* const ssrc_generator_;
};

}  //  namespace webrtc

#endif  // PC_CHANNEL_H_
