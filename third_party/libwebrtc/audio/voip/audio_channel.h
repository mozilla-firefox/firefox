/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AUDIO_VOIP_AUDIO_CHANNEL_H_
#define AUDIO_VOIP_AUDIO_CHANNEL_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include "api/audio/audio_mixer.h"
#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/audio_codecs/audio_encoder.h"
#include "api/audio_codecs/audio_format.h"
#include "api/environment/environment.h"
#include "api/ref_count.h"
#include "api/scoped_refptr.h"
#include "api/voip/voip_base.h"
#include "api/voip/voip_statistics.h"
#include "audio/voip/audio_egress.h"
#include "audio/voip/audio_ingress.h"
#include "call/audio_sender.h"
#include "modules/rtp_rtcp/include/receive_statistics.h"
#include "modules/rtp_rtcp/include/report_block_data.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_impl2.h"
#include "rtc_base/checks.h"

namespace webrtc {

// AudioChannel represents a single media session and provides APIs over
// AudioIngress and AudioEgress. Note that a single RTP stack is shared with
// these two classes as it has both sending and receiving capabilities.
class AudioChannel : public RefCountInterface, public ReportBlockDataObserver {
 public:
  AudioChannel(const Environment& env,
               Transport* transport,
               uint32_t local_ssrc,
               AudioMixer* audio_mixer,
               scoped_refptr<AudioDecoderFactory> decoder_factory);
  ~AudioChannel() override;

  // Set and get ChannelId that this audio channel belongs for debugging and
  // logging purpose.
  void SetId(ChannelId id) { id_ = id; }
  ChannelId GetId() const { return id_; }

  // APIs to start/stop audio channel on each direction.
  // StartSend/StartPlay returns false if encoder/decoders
  // have not been set, respectively.
  bool StartSend();
  void StopSend();
  bool StartPlay();
  void StopPlay();

  // APIs relayed to AudioEgress.
  bool IsSendingMedia() const { return egress_->IsSending(); }
  AudioSender* GetAudioSender() { return egress_.get(); }
  void SetEncoder(int payload_type,
                  const SdpAudioFormat& encoder_format,
                  std::unique_ptr<AudioEncoder> encoder) {
    egress_->SetEncoder(payload_type, encoder_format, std::move(encoder));
  }
  std::optional<SdpAudioFormat> GetEncoderFormat() const {
    return egress_->GetEncoderFormat();
  }
  void RegisterTelephoneEventType(int rtp_payload_type, int sample_rate_hz) {
    egress_->RegisterTelephoneEventType(rtp_payload_type, sample_rate_hz);
  }
  bool SendTelephoneEvent(int dtmf_event, int duration_ms) {
    return egress_->SendTelephoneEvent(dtmf_event, duration_ms);
  }
  void SetMute(bool enable) { egress_->SetMute(enable); }

  // APIs relayed to AudioIngress.
  bool IsPlaying() const { return ingress_->IsPlaying(); }
  void ReceivedRTPPacket(std::span<const uint8_t> rtp_packet) {
    ingress_->ReceivedRTPPacket(rtp_packet);
  }
  void ReceivedRTCPPacket(std::span<const uint8_t> rtcp_packet) {
    ingress_->ReceivedRTCPPacket(rtcp_packet);
  }
  void SetReceiveCodecs(const std::map<int, SdpAudioFormat>& codecs) {
    ingress_->SetReceiveCodecs(codecs);
  }
  IngressStatistics GetIngressStatistics();
  ChannelStatistics GetChannelStatistics();

  // See comments on the methods used from AudioEgress and AudioIngress.
  // Conversion to double is following what is done in
  // DoubleAudioLevelFromIntAudioLevel method in rtc_stats_collector.cc to be
  // consistent.
  double GetInputAudioLevel() const {
    return egress_->GetInputAudioLevel() / 32767.0;
  }
  double GetInputTotalEnergy() const { return egress_->GetInputTotalEnergy(); }
  double GetInputTotalDuration() const {
    return egress_->GetInputTotalDuration();
  }
  double GetOutputAudioLevel() const {
    return ingress_->GetOutputAudioLevel() / 32767.0;
  }
  double GetOutputTotalEnergy() const {
    return ingress_->GetOutputTotalEnergy();
  }
  double GetOutputTotalDuration() const {
    return ingress_->GetOutputTotalDuration();
  }

  // Implementation of ReportBlockDataObserver.
  void OnReportBlockDataUpdated(ReportBlockData report_block) override;

  // Internal API for testing purpose.
  void SendRTCPReportForTesting(RTCPPacketType type) {
    int32_t result = rtp_rtcp_->SendRTCP(type);
    RTC_DCHECK(result == 0);
  }

 private:
  // ChannelId that this audio channel belongs for logging purpose.
  ChannelId id_;

  // Synchronization is handled internally by AudioMixer.
  AudioMixer* audio_mixer_;

  // Listed in order for safe destruction of AudioChannel object.
  // Synchronization for these are handled internally.
  std::unique_ptr<ReceiveStatistics> receive_statistics_;
  std::unique_ptr<ModuleRtpRtcpImpl2> rtp_rtcp_;
  std::unique_ptr<AudioIngress> ingress_;
  std::unique_ptr<AudioEgress> egress_;
};

}  // namespace webrtc

#endif  // AUDIO_VOIP_AUDIO_CHANNEL_H_
