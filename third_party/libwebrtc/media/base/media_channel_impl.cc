/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/base/media_channel_impl.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "api/audio_options.h"
#include "api/call/transport.h"
#include "api/media_stream_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_sender_interface.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "media/base/media_channel.h"
#include "media/base/rtp_utils.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/dscp.h"
#include "rtc_base/socket.h"

namespace webrtc {

VideoOptions::VideoOptions()
    : content_hint(VideoTrackInterface::ContentHint::kNone) {}
VideoOptions::~VideoOptions() = default;

MediaChannelUtil::MediaChannelUtil(TaskQueueBase* network_thread,
                                   bool enable_dscp)
    : transport_(network_thread, enable_dscp) {}

MediaChannelUtil::~MediaChannelUtil() {}

void MediaChannelUtil::SetInterface(MediaChannelNetworkInterface* iface) {
  transport_.SetInterface(iface);
}

int MediaChannelUtil::GetRtpSendTimeExtnId() const {
  return -1;
}

bool MediaChannelUtil::SendPacket(CopyOnWriteBuffer* packet,
                                  const AsyncSocketPacketOptions& options) {
  return transport_.DoSendPacket(packet, false, options);
}

bool MediaChannelUtil::SendRtcp(CopyOnWriteBuffer* packet,
                                const AsyncSocketPacketOptions& options) {
  return transport_.DoSendPacket(packet, true, options);
}

int MediaChannelUtil::SetOption(MediaChannelNetworkInterface::SocketType type,
                                Socket::Option opt,
                                int option) {
  return transport_.SetOption(type, opt, option);
}

// Corresponds to the SDP attribute extmap-allow-mixed, see RFC8285.
// Set to true if it's allowed to mix one- and two-byte RTP header extensions
// in the same stream. The setter and getter must only be called from
// worker_thread.
void MediaChannelUtil::SetExtmapAllowMixed(bool extmap_allow_mixed) {
  extmap_allow_mixed_ = extmap_allow_mixed;
}

bool MediaChannelUtil::ExtmapAllowMixed() const {
  return extmap_allow_mixed_;
}

bool MediaChannelUtil::HasNetworkInterface() const {
  return transport_.HasNetworkInterface();
}

bool MediaChannelUtil::DscpEnabled() const {
  return transport_.DscpEnabled();
}

void MediaChannelUtil::SetPreferredDscp(DiffServCodePoint new_dscp) {
  transport_.SetPreferredDscp(new_dscp);
}

MediaSenderInfo::MediaSenderInfo() = default;
MediaSenderInfo::~MediaSenderInfo() = default;

MediaReceiverInfo::MediaReceiverInfo() = default;
MediaReceiverInfo::~MediaReceiverInfo() = default;

VoiceSenderInfo::VoiceSenderInfo() = default;
VoiceSenderInfo::~VoiceSenderInfo() = default;

VoiceReceiverInfo::VoiceReceiverInfo() = default;
VoiceReceiverInfo::~VoiceReceiverInfo() = default;

VideoSenderInfo::VideoSenderInfo() = default;
VideoSenderInfo::~VideoSenderInfo() = default;

VideoReceiverInfo::VideoReceiverInfo() = default;
VideoReceiverInfo::~VideoReceiverInfo() = default;

VoiceMediaInfo::VoiceMediaInfo() = default;
VoiceMediaInfo::~VoiceMediaInfo() = default;

VideoMediaInfo::VideoMediaInfo() = default;
VideoMediaInfo::~VideoMediaInfo() = default;

VideoMediaSendInfo::VideoMediaSendInfo() = default;
VideoMediaSendInfo::~VideoMediaSendInfo() = default;

VoiceMediaSendInfo::VoiceMediaSendInfo() = default;
VoiceMediaSendInfo::~VoiceMediaSendInfo() = default;

VideoMediaReceiveInfo::VideoMediaReceiveInfo() = default;
VideoMediaReceiveInfo::~VideoMediaReceiveInfo() = default;

VoiceMediaReceiveInfo::VoiceMediaReceiveInfo() = default;
VoiceMediaReceiveInfo::~VoiceMediaReceiveInfo() = default;

AudioSenderParameter::AudioSenderParameter() = default;
AudioSenderParameter::~AudioSenderParameter() = default;

std::map<std::string, std::string> AudioSenderParameter::ToStringMap() const {
  auto params = SenderParameters::ToStringMap();
  params["options"] = options.ToString();
  return params;
}

VideoSenderParameters::VideoSenderParameters() = default;
VideoSenderParameters::~VideoSenderParameters() = default;

std::map<std::string, std::string> VideoSenderParameters::ToStringMap() const {
  auto params = SenderParameters::ToStringMap();
  params["conference_mode"] = (conference_mode ? "yes" : "no");
  return params;
}

// --------------------- MediaChannelUtil::TransportForMediaChannels -----

MediaChannelUtil::TransportForMediaChannels::TransportForMediaChannels(
    TaskQueueBase* network_thread,
    bool enable_dscp)
    : network_safety_(PendingTaskSafetyFlag::CreateDetachedInactive()),
      network_thread_(network_thread),

      enable_dscp_(enable_dscp) {}

MediaChannelUtil::TransportForMediaChannels::~TransportForMediaChannels() {
  RTC_DCHECK(!network_interface_);
}

AsyncSocketPacketOptions
MediaChannelUtil::TransportForMediaChannels::TranslatePacketOptions(
    const PacketOptions& options) {
  AsyncSocketPacketOptions rtc_options;
  rtc_options.packet_id = options.packet_id;
  if (DscpEnabled()) {
    rtc_options.dscp = PreferredDscp();
  }
  rtc_options.info_signaled_after_sent.included_in_feedback =
      options.included_in_feedback;
  rtc_options.info_signaled_after_sent.included_in_allocation =
      options.included_in_allocation;
  rtc_options.info_signaled_after_sent.is_media = options.is_media;
  rtc_options.ect_1 = options.send_as_ect1;
  rtc_options.batchable = options.batchable;
  rtc_options.last_packet_in_batch = options.last_packet_in_batch;
  return rtc_options;
}

bool MediaChannelUtil::TransportForMediaChannels::SendRtcp(
    std::span<const uint8_t> packet,
    const PacketOptions& options) {
  auto send = [this, packet = CopyOnWriteBuffer(packet, kMaxRtpPacketLen),
               options]() mutable {
    DoSendPacket(&packet, true, TranslatePacketOptions(options));
  };

  if (network_thread_->IsCurrent()) {
    send();
  } else {
    network_thread_->PostTask(SafeTask(network_safety_, std::move(send)));
  }
  return true;
}

bool MediaChannelUtil::TransportForMediaChannels::SendRtp(
    std::span<const uint8_t> packet,
    const PacketOptions& options) {
  RTC_DCHECK_RUN_ON(network_thread_);
  CopyOnWriteBuffer send_packet(packet, kMaxRtpPacketLen);
  DoSendPacket(&send_packet, false, TranslatePacketOptions(options));
  return true;
}

void MediaChannelUtil::TransportForMediaChannels::SetInterface(
    MediaChannelNetworkInterface* iface) {
  RTC_DCHECK_RUN_ON(network_thread_);
  iface ? network_safety_->SetAlive() : network_safety_->SetNotAlive();
  network_interface_ = iface;
  UpdateDscp();
}

void MediaChannelUtil::TransportForMediaChannels::UpdateDscp() {
  DiffServCodePoint value = enable_dscp_ ? preferred_dscp_ : DSCP_DEFAULT;
  int ret = SetOptionLocked(MediaChannelNetworkInterface::ST_RTP,
                            Socket::OPT_DSCP, value);
  if (ret == 0)
    SetOptionLocked(MediaChannelNetworkInterface::ST_RTCP, Socket::OPT_DSCP,
                    value);
}

bool MediaChannelUtil::TransportForMediaChannels::DoSendPacket(
    CopyOnWriteBuffer* packet,
    bool rtcp,
    const AsyncSocketPacketOptions& options) {
  RTC_DCHECK_RUN_ON(network_thread_);
  if (!network_interface_)
    return false;

  return (!rtcp) ? network_interface_->SendPacket(packet, options)
                 : network_interface_->SendRtcp(packet, options);
}

int MediaChannelUtil::TransportForMediaChannels::SetOption(
    MediaChannelNetworkInterface::SocketType type,
    Socket::Option opt,
    int option) {
  RTC_DCHECK_RUN_ON(network_thread_);
  return SetOptionLocked(type, opt, option);
}

int MediaChannelUtil::TransportForMediaChannels::SetOptionLocked(
    MediaChannelNetworkInterface::SocketType type,
    Socket::Option opt,
    int option) {
  if (!network_interface_)
    return -1;
  return network_interface_->SetOption(type, opt, option);
}

void MediaChannelUtil::TransportForMediaChannels::SetPreferredDscp(
    DiffServCodePoint new_dscp) {
  if (!network_thread_->IsCurrent()) {
    // This is currently the common path as the derived channel classes
    // get called on the worker thread. There are still some tests though
    // that call directly on the network thread.
    network_thread_->PostTask(SafeTask(
        network_safety_, [this, new_dscp]() { SetPreferredDscp(new_dscp); }));
    return;
  }

  RTC_DCHECK_RUN_ON(network_thread_);
  if (new_dscp == preferred_dscp_)
    return;

  preferred_dscp_ = new_dscp;
  UpdateDscp();
}

}  // namespace webrtc
