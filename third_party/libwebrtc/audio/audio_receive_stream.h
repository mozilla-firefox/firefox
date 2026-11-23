/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AUDIO_AUDIO_RECEIVE_STREAM_H_
#define AUDIO_AUDIO_RECEIVE_STREAM_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"
#include "api/audio/audio_frame.h"
#include "api/audio/audio_mixer.h"
#include "api/audio_codecs/audio_format.h"
#include "api/crypto/frame_decryptor_interface.h"
#include "api/environment/environment.h"
#include "api/frame_transformer_interface.h"
#include "api/neteq/neteq_factory.h"
#include "api/rtp_headers.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
// This can be removed after Bug 1768116 enables
// c++20 builds across the entire Mozilla tree.
#if !defined(WEBRTC_MOZILLA_BUILD)
#include "audio/audio_state.h"
#endif
#include "call/audio_receive_stream.h"
#include "call/audio_state.h"
#include "call/syncable.h"
#include "rtc_base/system/no_unique_address.h"
#include "rtc_base/thread_annotations.h"
#include "system_wrappers/include/ntp_time.h"

namespace webrtc {
class PacketRouter;
class RtpStreamReceiverControllerInterface;
class RtpStreamReceiverInterface;

// This can be removed after Bug 1768116 enables
// c++20 builds across the entire Mozilla tree.
#if defined(WEBRTC_MOZILLA_BUILD)
namespace internal {
class AudioState;
}
#endif

namespace voe {
class ChannelReceiveInterface;
}  // namespace voe

class AudioReceiveStreamImpl final : public webrtc::AudioReceiveStreamInterface,
                                     public AudioMixer::Source,
                                     public Syncable {
 public:
  AudioReceiveStreamImpl(const Environment& env,
                         PacketRouter* absl_nonnull packet_router,
                         NetEqFactory* absl_nullable neteq_factory,
                         webrtc::AudioReceiveStreamInterface::Config config,
                         const scoped_refptr<webrtc::AudioState>& audio_state);
  // For unit tests, which need to supply a mock channel receive.
  AudioReceiveStreamImpl(
      const Environment& env,
      webrtc::AudioReceiveStreamInterface::Config config,
      const scoped_refptr<webrtc::AudioState>& audio_state,
      absl_nonnull std::unique_ptr<voe::ChannelReceiveInterface>
          channel_receive);

  AudioReceiveStreamImpl() = delete;
  AudioReceiveStreamImpl(const AudioReceiveStreamImpl&) = delete;
  AudioReceiveStreamImpl& operator=(const AudioReceiveStreamImpl&) = delete;

  // Destruction happens on the network thread. Prior to destruction the caller
  // must ensure that a registration with the transport has been cleared. See
  // `RegisterWithTransport` for details.
  ~AudioReceiveStreamImpl() override;

  // Binds the stream to the transport.
  void RegisterWithTransport(
      RtpStreamReceiverControllerInterface* receiver_controller);
  // Unbinds the stream from the transport. Must be called prior to destruction
  // if RegisterWithTransport was called.
  void UnregisterFromTransport();

  // webrtc::AudioReceiveStreamInterface implementation.
  void Start() override;
  void Stop() override;
  bool IsRunning() const override;
  void SetDepacketizerToDecoderFrameTransformer(
      scoped_refptr<webrtc::FrameTransformerInterface> frame_transformer)
      override;
  void SetDecoderMap(std::map<int, SdpAudioFormat> decoder_map) override;
  void SetNackHistory(int history_ms) override;
  void SetRtcpMode(RtcpMode mode) override;
  void SetNonSenderRttMeasurement(bool enabled) override;
  void SetFrameDecryptor(
      scoped_refptr<webrtc::FrameDecryptorInterface> frame_decryptor) override;
  webrtc::AudioReceiveStreamInterface::Stats GetStats(
      bool get_and_clear_legacy_stats) const override;
  void SetSink(AudioSinkInterface* sink) override;
  void SetGain(float gain) override;
  void SetJitterBufferMaxPackets(size_t max_packets) override;
  void SetJitterBufferFastAccelerate(bool fast_accelerate) override;
  bool SetBaseMinimumPlayoutDelayMs(int delay_ms) override;
  int GetBaseMinimumPlayoutDelayMs() const override;
  AudioMixer::Source* source() override { return this; }

  // AudioMixer::Source
  AudioFrameInfo GetAudioFrameWithInfo(int sample_rate_hz,
                                       AudioFrame* audio_frame) override;
  int Ssrc() const override;
  int PreferredSampleRate() const override;

  // Syncable
  uint32_t id() const override;
  std::optional<Syncable::Info> GetInfo() const override;
  std::optional<Syncable::PlayoutInfo> GetPlayoutRtpTimestamp() const override;
  void SetEstimatedPlayoutNtpTimestamp(NtpTime ntp_time,
                                       Timestamp time) override;
  bool SetMinimumPlayoutDelay(TimeDelta delay) override;

  void DeliverRtcp(std::span<const uint8_t> packet);

  void SetSyncGroup(absl::string_view sync_group);

  uint32_t remote_ssrc() const override;

  // Returns a reference to the currently set sync group of the stream.
  // Must be called on the network thread.
  const std::string& sync_group() const;

 private:
  void Initialize();

  internal::AudioState* audio_state() const;

  const Environment env_;
  RTC_NO_UNIQUE_ADDRESS SequenceChecker network_thread_checker_;
  webrtc::AudioReceiveStreamInterface::Config config_;
  const scoped_refptr<webrtc::AudioState> audio_state_;
  const std::unique_ptr<voe::ChannelReceiveInterface> channel_receive_;

  bool playing_ RTC_GUARDED_BY(network_thread_checker_) = false;

  std::unique_ptr<RtpStreamReceiverInterface> rtp_stream_receiver_
      RTC_GUARDED_BY(network_thread_checker_);
};
}  // namespace webrtc

#endif  // AUDIO_AUDIO_RECEIVE_STREAM_H_
