/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/voip/audio_channel.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "api/audio/audio_mixer.h"
#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/call/transport.h"
#include "api/environment/environment.h"
#include "api/rtp_headers.h"
#include "api/scoped_refptr.h"
#include "api/voip/voip_statistics.h"
#include "audio/voip/audio_egress.h"
#include "audio/voip/audio_ingress.h"
#include "modules/audio_coding/include/audio_coding_module_typedefs.h"
#include "modules/rtp_rtcp/include/receive_statistics.h"
#include "modules/rtp_rtcp/include/report_block_data.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_impl2.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_interface.h"
#include "rtc_base/checks.h"

namespace webrtc {

namespace {

constexpr int kRtcpReportIntervalMs = 5000;

}  // namespace

AudioChannel::AudioChannel(const Environment& env,
                           Transport* transport,
                           uint32_t local_ssrc,
                           AudioMixer* audio_mixer,
                           scoped_refptr<AudioDecoderFactory> decoder_factory)
    : audio_mixer_(audio_mixer) {
  RTC_DCHECK(audio_mixer);

  receive_statistics_ = ReceiveStatistics::Create(&env.clock());

  RtpRtcpInterface::Configuration rtp_config;
  rtp_config.audio = true;
  rtp_config.receive_statistics = receive_statistics_.get();
  rtp_config.report_block_data_observer = this;
  rtp_config.rtcp_report_interval_ms = kRtcpReportIntervalMs;
  rtp_config.outgoing_transport = transport;
  rtp_config.local_media_ssrc = local_ssrc;
  rtp_config.rtcp_mode = RtcpMode::kCompound;

  rtp_rtcp_ = ModuleRtpRtcpImpl2::CreateSendModule(env, rtp_config);

  rtp_rtcp_->SetSendingMediaStatus(false);

  ingress_ = std::make_unique<AudioIngress>(env, rtp_rtcp_.get(),
                                            receive_statistics_.get(),
                                            std::move(decoder_factory));
  egress_ = std::make_unique<AudioEgress>(env, rtp_rtcp_.get());

  // Set the instance of audio ingress to be part of audio mixer for ADM to
  // fetch audio samples to play.
  audio_mixer_->AddSource(ingress_.get());
}

AudioChannel::~AudioChannel() {
  if (egress_->IsSending()) {
    StopSend();
  }
  if (ingress_->IsPlaying()) {
    StopPlay();
  }

  audio_mixer_->RemoveSource(ingress_.get());

  // TODO(bugs.webrtc.org/11581): unclear if we still need to clear `egress_`
  // here.
  egress_.reset();
  ingress_.reset();
}

bool AudioChannel::StartSend() {
  // If encoder has not been set, return false.
  if (!egress_->StartSend()) {
    return false;
  }

  // Start sending with RTP stack if it has not been sending yet.
  if (!rtp_rtcp_->Sending()) {
    rtp_rtcp_->SetSendingStatus(true);
  }
  return true;
}

void AudioChannel::StopSend() {
  egress_->StopSend();

  // Deactivate RTP stack when both sending and receiving are stopped.
  // SetSendingStatus(false) triggers the transmission of RTCP BYE
  // message to remote endpoint.
  if (!ingress_->IsPlaying() && rtp_rtcp_->Sending()) {
    rtp_rtcp_->SetSendingStatus(false);
  }
}

bool AudioChannel::StartPlay() {
  // If decoders have not been set, return false.
  if (!ingress_->StartPlay()) {
    return false;
  }

  // If RTP stack is not sending then start sending as in recv-only mode, RTCP
  // receiver report is expected.
  if (!rtp_rtcp_->Sending()) {
    rtp_rtcp_->SetSendingStatus(true);
  }
  return true;
}

void AudioChannel::StopPlay() {
  ingress_->StopPlay();

  // Deactivate RTP stack only when both sending and receiving are stopped.
  if (!rtp_rtcp_->SendingMedia() && rtp_rtcp_->Sending()) {
    rtp_rtcp_->SetSendingStatus(false);
  }
}

IngressStatistics AudioChannel::GetIngressStatistics() {
  IngressStatistics ingress_stats;
  NetworkStatistics stats = ingress_->GetNetworkStatistics();
  ingress_stats.neteq_stats.total_samples_received = stats.totalSamplesReceived;
  ingress_stats.neteq_stats.concealed_samples = stats.concealedSamples;
  ingress_stats.neteq_stats.concealment_events = stats.concealmentEvents;
  ingress_stats.neteq_stats.jitter_buffer_delay_ms = stats.jitterBufferDelayMs;
  ingress_stats.neteq_stats.jitter_buffer_emitted_count =
      stats.jitterBufferEmittedCount;
  ingress_stats.neteq_stats.jitter_buffer_target_delay_ms =
      stats.jitterBufferTargetDelayMs;
  ingress_stats.neteq_stats.inserted_samples_for_deceleration =
      stats.insertedSamplesForDeceleration;
  ingress_stats.neteq_stats.removed_samples_for_acceleration =
      stats.removedSamplesForAcceleration;
  ingress_stats.neteq_stats.silent_concealed_samples =
      stats.silentConcealedSamples;
  ingress_stats.neteq_stats.fec_packets_received = stats.fecPacketsReceived;
  ingress_stats.neteq_stats.fec_packets_discarded = stats.fecPacketsDiscarded;
  ingress_stats.neteq_stats.delayed_packet_outage_samples =
      stats.delayedPacketOutageSamples;
  ingress_stats.neteq_stats.relative_packet_arrival_delay_ms =
      stats.relativePacketArrivalDelayMs;
  ingress_stats.neteq_stats.interruption_count = stats.interruptionCount;
  ingress_stats.neteq_stats.total_interruption_duration_ms =
      stats.totalInterruptionDurationMs;
  ingress_stats.total_duration = ingress_->GetOutputTotalDuration();
  return ingress_stats;
}

ChannelStatistics AudioChannel::GetChannelStatistics() {
  ChannelStatistics channel_stat = ingress_->GetChannelStatistics();

  StreamDataCounters rtp_stats, rtx_stats;
  rtp_rtcp_->GetSendStreamDataCounters(&rtp_stats, &rtx_stats);
  channel_stat.bytes_sent =
      rtp_stats.transmitted.payload_bytes + rtx_stats.transmitted.payload_bytes;
  channel_stat.packets_sent =
      rtp_stats.transmitted.packets + rtx_stats.transmitted.packets;

  return channel_stat;
}

void AudioChannel::OnReportBlockDataUpdated(ReportBlockData report_block) {
  if (egress_) {
    egress_->OnReceivedUplinkPacketLossFraction(report_block.fraction_lost());
  }
}

}  // namespace webrtc
