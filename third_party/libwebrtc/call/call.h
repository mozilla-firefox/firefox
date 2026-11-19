/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef CALL_CALL_H_
#define CALL_CALL_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "api/adaptation/resource.h"
#include "api/fec_controller.h"
#include "api/media_types.h"
#include "api/rtp_headers.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/task_queue_base.h"
#include "api/transport/bitrate_settings.h"
#include "api/video/video_stream_encoder_settings.h"
#include "call/audio_receive_stream.h"
#include "call/audio_send_stream.h"
#include "call/call_config.h"
#include "call/flexfec_receive_stream.h"
#include "call/packet_receiver.h"
#include "call/rtp_transport_controller_send_interface.h"
#include "call/video_receive_stream.h"
#include "call/video_send_stream.h"
#include "modules/congestion_controller/rtp/congestion_controller_feedback_stats.h"
#include "rtc_base/containers/flat_map.h"
#include "rtc_base/network/sent_packet.h"
#include "video/config/video_encoder_config.h"

namespace webrtc {

// A Call represents a two-way connection carrying zero or more outgoing
// and incoming media streams, transported over one or more RTP transports.

// Creation of Call is thread-agnostic (can be done on any thread), but it must
// be destroyed on the worker thread.

// A Call instance can contain several send and/or receive streams. All streams
// are assumed to have the same remote endpoint and will share bitrate estimates
// etc.

// When using the PeerConnection API, there is a one-to-one relationship
// between the PeerConnection and a Call object.

class Call {
 public:
  struct Stats {
    std::string ToString(int64_t time_ms) const;

    int send_bandwidth_bps = 0;       // Estimated available send bandwidth.
    int max_padding_bitrate_bps = 0;  // Cumulative configured max padding.
    int recv_bandwidth_bps = 0;       // Estimated available receive bandwidth.
    int64_t pacer_delay_ms = 0;
    int64_t rtt_ms = -1;
    std::optional<int64_t> ccfb_messages_received = std::nullopt;
    flat_map<uint32_t, SentCongestionControllerFeedbackStats>
      sent_ccfb_stats_per_ssrc;
    flat_map<uint32_t, ReceivedCongestionControlFeedbackStats>
      received_ccfb_stats_per_ssrc;
  };

  static std::unique_ptr<Call> Create(CallConfig config);

  virtual AudioSendStream* CreateAudioSendStream(
      const AudioSendStream::Config& config) = 0;

  virtual void DestroyAudioSendStream(AudioSendStream* send_stream) = 0;

  virtual AudioReceiveStreamInterface* CreateAudioReceiveStream(
      AudioReceiveStreamInterface::Config config) = 0;
  virtual void DestroyAudioReceiveStream(
      AudioReceiveStreamInterface* receive_stream) = 0;

  virtual VideoSendStream* CreateVideoSendStream(
      VideoSendStream::Config config,
      VideoEncoderConfig encoder_config,
      EncoderSwitchRequestCallback encoder_switch_request_callback =
          nullptr) = 0;
  virtual VideoSendStream* CreateVideoSendStream(
      VideoSendStream::Config config,
      VideoEncoderConfig encoder_config,
      EncoderSwitchRequestCallback encoder_switch_request_callback,
      std::unique_ptr<FecController> fec_controller) = 0;
  virtual void DestroyVideoSendStream(VideoSendStream* send_stream) = 0;

  virtual VideoReceiveStreamInterface* CreateVideoReceiveStream(
      VideoReceiveStreamInterface::Config configuration) = 0;
  virtual void DestroyVideoReceiveStream(
      VideoReceiveStreamInterface* receive_stream) = 0;

  // In order for a created VideoReceiveStreamInterface to be aware that it is
  // protected by a FlexfecReceiveStream, the latter should be created before
  // the former.
  virtual FlexfecReceiveStream* CreateFlexfecReceiveStream(
      const FlexfecReceiveStream::Config config) = 0;
  virtual void DestroyFlexfecReceiveStream(
      FlexfecReceiveStream* receive_stream) = 0;

  // When a resource is overused, the Call will try to reduce the load on the
  // sysem, for example by reducing the resolution or frame rate of encoded
  // streams.
  virtual void AddAdaptationResource(scoped_refptr<Resource> resource) = 0;

  // All received RTP and RTCP packets for the call should be inserted to this
  // PacketReceiver. The PacketReceiver pointer is valid as long as the
  // Call instance exists.
  virtual PacketReceiver* Receiver() = 0;

  // This is used to access the transport controller send instance owned by
  // Call. The send transport controller is currently owned by Call for legacy
  // reasons. (for instance  variants of call tests are built on this assumtion)
  // TODO(srte): Move ownership of transport controller send out of Call and
  // remove this method interface.
  virtual RtpTransportControllerSendInterface* GetTransportControllerSend() = 0;

  // Returns the call statistics, such as estimated send and receive bandwidth,
  // pacing delay, etc.
  virtual Stats GetStats() const = 0;

  // TODO(skvlad): When the unbundled case with multiple streams for the same
  // media type going over different networks is supported, track the state
  // for each stream separately. Right now it's global per media type.
  virtual void SignalChannelNetworkState(MediaType media,
                                         NetworkState state) = 0;

  virtual void OnUpdateSyncGroup(AudioReceiveStreamInterface& stream,
                                 absl::string_view sync_group) = 0;

  virtual void OnSentPacket(const SentPacketInfo& sent_packet) = 0;

  virtual void SetClientBitratePreferences(
      const BitrateSettings& preferences) = 0;

  // Decides which RTCP feedback type to use for congestion control.
  virtual void SetPreferredRtcpCcAckType(
      RtcpFeedbackType preferred_rtcp_cc_ack_type) = 0;
  virtual std::optional<int> FeedbackAccordingToRfc8888Count() = 0;
  virtual std::optional<int> FeedbackAccordingToTransportCcCount() = 0;

  virtual void DisconnectFromNetworkThread() = 0;

  virtual TaskQueueBase* network_thread() const = 0;
  [[deprecated("worker_thread() is deprecated; use network_thread() instead.")]]
  virtual TaskQueueBase* worker_thread() const = 0;

  virtual ~Call() = default;
};

}  // namespace webrtc

#endif  // CALL_CALL_H_
