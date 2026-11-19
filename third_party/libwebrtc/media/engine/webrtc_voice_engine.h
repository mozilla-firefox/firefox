/*
 *  Copyright (c) 2004 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MEDIA_ENGINE_WEBRTC_VOICE_ENGINE_H_
#define MEDIA_ENGINE_WEBRTC_VOICE_ENGINE_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/audio/audio_device.h"
#include "api/audio/audio_frame_processor.h"
#include "api/audio/audio_mixer.h"
#include "api/audio/audio_processing.h"
#include "api/audio_codecs/audio_decoder_factory.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_options.h"
#include "api/call/audio_sink.h"
#include "api/crypto/crypto_options.h"
#include "api/crypto/frame_decryptor_interface.h"
#include "api/crypto/frame_encryptor_interface.h"
#include "api/environment/environment.h"
#include "api/field_trials_view.h"
#include "api/frame_transformer_interface.h"
#include "api/media_types.h"
#include "api/payload_type.h"
#include "api/rtc_error.h"
#include "api/rtp_headers.h"
#include "api/rtp_packet_infos.h"
#include "api/rtp_parameters.h"
#include "api/rtp_sender_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/timestamp.h"
#include "call/audio_send_stream.h"
#include "call/audio_state.h"
#include "call/call.h"
#include "call/sframe_options.h"
#include "media/base/audio_source.h"
#include "media/base/codec.h"
#include "media/base/media_channel.h"
#include "media/base/media_channel_impl.h"
#include "media/base/media_config.h"
#include "media/base/media_engine.h"
#include "media/base/stream_params.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/sframe/sframe_encryption_config.h"
#include "rtc_base/checks.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/network_route.h"
#include "rtc_base/system/file_wrapper.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {

class AudioFrameProcessor;
class WebRtcVoiceSendChannel;

// WebRtcVoiceEngine is a class to be used with CompositeMediaEngine.
// It uses the WebRtc VoiceEngine library for audio handling.
class WebRtcVoiceEngine final : public VoiceEngineInterface {
 public:
  WebRtcVoiceEngine(const Environment& env,
                    scoped_refptr<AudioDeviceModule> adm,
                    scoped_refptr<AudioEncoderFactory> encoder_factory,
                    scoped_refptr<AudioDecoderFactory> decoder_factory,
                    scoped_refptr<AudioMixer> audio_mixer,
                    scoped_refptr<AudioProcessing> audio_processing,
                    std::unique_ptr<AudioFrameProcessor> audio_frame_processor);

  WebRtcVoiceEngine() = delete;
  WebRtcVoiceEngine(const WebRtcVoiceEngine&) = delete;
  WebRtcVoiceEngine& operator=(const WebRtcVoiceEngine&) = delete;

  ~WebRtcVoiceEngine() override;

  // Does initialization that needs to occur on the worker thread.
  void Init() override;
  void Terminate() override;
  scoped_refptr<AudioState> GetAudioState() const override;

  std::unique_ptr<VoiceMediaSendChannelInterface> CreateSendChannel(
      const Environment& env,
      Call* call,
      const MediaConfig& config,
      const AudioOptions& options,
      const CryptoOptions& crypto_options,
      absl::AnyInvocable<void()> parameters_changed_callback =
          nullptr) override;

  std::unique_ptr<VoiceMediaReceiveChannelInterface> CreateReceiveChannel(
      const Environment& env,
      Call* call,
      const MediaConfig& config,
      const AudioOptions& options,
      const CryptoOptions& crypto_options,
      absl::AnyInvocable<void(uint32_t ssrc)> on_first_packet,
      absl::AnyInvocable<void(uint32_t ssrc, const RtpPacketInfos&, Timestamp)
                             const> on_frame_delivered_callback) override;

  const std::vector<Codec>& LegacySendCodecs() const override;
  const std::vector<Codec>& LegacyRecvCodecs() const override;

  const scoped_refptr<AudioEncoderFactory>& encoder_factory() const override {
    return encoder_factory_;
  }
  const scoped_refptr<AudioDecoderFactory>& decoder_factory() const override {
    return decoder_factory_;
  }

  // Applies global audio processing options to engine. Internal helper
  // called by Init() and ApplyGlobalOptions().
  void ApplyOptions(const AudioOptions& options);
  // Applies global engine-level processing options (e.g. APM settings like AEC,
  // AGC, NS). Global options govern all processing; local channel-level
  // settings for these fields are ignored and do not override the global
  // configuration.
  void ApplyGlobalOptions(const AudioOptions& options) override;

  AudioDeviceModule* adm();
  AudioProcessing* apm() const;

  // Registers `channel` when its first send stream is added on the worker
  // thread, and unregisters it when `channel` is destroyed. Registration
  // happens on stream addition rather than CreateSendChannel() because
  // CreateSendChannel() runs on the signaling thread without blocking access to
  // `worker_thread_`.
  void RegisterSendChannel(WebRtcVoiceSendChannel* channel);
  void UnregisterSendChannel(WebRtcVoiceSendChannel* channel);

  // Recomputes aggregate mute state across `send_channels_` and updates
  // AudioProcessing::set_output_will_be_muted() if the effective boolean mute
  // state changed.
  void OnChannelMuteStateChange();

  std::vector<RtpHeaderExtensionCapability> GetRtpHeaderExtensions(
      const FieldTrialsView* field_trials) const override;

  // Starts AEC dump using an existing file. A maximum file size in bytes can be
  // specified. When the maximum file size is reached, logging is stopped and
  // the file is closed. If max_size_bytes is set to <= 0, no limit will be
  // used.
  bool StartAecDump(FileWrapper file, int64_t max_size_bytes) override;

  // Stops AEC dump.
  void StopAecDump() override;

  std::optional<AudioDeviceModule::Stats> GetAudioDeviceStats() override;

  bool NeedsAuxiliaryCodecsAdded() const override { return true; }

 private:
  const Environment env_;
  std::unique_ptr<TaskQueueBase, TaskQueueDeleter> low_priority_worker_queue_;

  AudioState* audio_state();

  SequenceChecker signal_thread_checker_{SequenceChecker::kDetached};
  SequenceChecker worker_thread_checker_{SequenceChecker::kDetached};

  // Field trial flags.
  const bool minimized_resampling_on_mobile_trial_enabled_;
  const bool payload_types_in_transport_trial_enabled_;

  // The audio device module.
  const scoped_refptr<AudioDeviceModule> adm_;
  const scoped_refptr<AudioEncoderFactory> encoder_factory_;
  const scoped_refptr<AudioDecoderFactory> decoder_factory_;
  // The audio processing module.
  scoped_refptr<AudioProcessing> apm_ RTC_GUARDED_BY(worker_thread_checker_);
  // The primary instance of WebRtc VoiceEngine.
  const scoped_refptr<AudioState> audio_state_;
  const std::vector<Codec> legacy_send_codecs_;
  const std::vector<Codec> legacy_recv_codecs_;
  // Send channels that have had at least one send stream added. Populated on
  // the worker thread via RegisterSendChannel() and removed in
  // UnregisterSendChannel().
  absl::flat_hash_set<WebRtcVoiceSendChannel*> send_channels_
      RTC_GUARDED_BY(worker_thread_checker_);
  // Last boolean mute value forwarded to `apm_->set_output_will_be_muted()`.
  std::optional<bool> output_will_be_muted_
      RTC_GUARDED_BY(worker_thread_checker_);
  bool initialized_ RTC_GUARDED_BY(worker_thread_checker_) = false;
};

class WebRtcVoiceSendChannel final : public MediaChannelUtil,
                                     public VoiceMediaSendChannelInterface {
 public:
  WebRtcVoiceSendChannel(
      const Environment& env,
      WebRtcVoiceEngine* engine,
      const MediaConfig& config,
      const AudioOptions& options,
      const CryptoOptions& crypto_options,
      Call* call,
      absl::AnyInvocable<void()> parameters_changed_callback = nullptr);

  WebRtcVoiceSendChannel() = delete;
  WebRtcVoiceSendChannel(const WebRtcVoiceSendChannel&) = delete;
  WebRtcVoiceSendChannel& operator=(const WebRtcVoiceSendChannel&) = delete;

  ~WebRtcVoiceSendChannel() override;

  MediaType media_type() const override { return MediaType::AUDIO; }
  VideoMediaSendChannelInterface* AsVideoSendChannel() override {
    RTC_CHECK_NOTREACHED();
    return nullptr;
  }
  VoiceMediaSendChannelInterface* AsVoiceSendChannel() override { return this; }

  std::optional<Codec> GetSendCodec() const override;

  // Functions imported from MediaChannelUtil
  void SetInterface(MediaChannelNetworkInterface* iface) override {
    MediaChannelUtil::SetInterface(iface);
  }

  bool HasNetworkInterface() const override {
    return MediaChannelUtil::HasNetworkInterface();
  }
  void SetExtmapAllowMixed(bool extmap_allow_mixed) override {
    MediaChannelUtil::SetExtmapAllowMixed(extmap_allow_mixed);
  }

  const AudioOptions& options() const { return options_; }

  bool SetSenderParameters(const AudioSenderParameter& params) override;
  void SetMid(absl::string_view mid) override;
  RtpParameters GetRtpSendParameters(uint32_t ssrc) const override;
  absl::AnyInvocable<RtpParameters(uint32_t)> GetRtpSendParametersCallback()
      const override;
  RTCError SetRtpSendParameters(uint32_t ssrc,
                                const RtpParameters& parameters,
                                SetParametersCallback callback) override;
  bool SetSend(bool send) override;
  bool SetAudioSend(uint32_t ssrc,
                    bool enable,
                    const AudioOptions* options,
                    AudioSource* source) override;
  bool AddSendStream(const StreamParams& sp) override;
  bool RemoveSendStream(uint32_t ssrc) override;

  void SetSsrcListChangedCallback(
      absl::AnyInvocable<void(const std::set<uint32_t>&)> callback) override;

  // E2EE Frame API
  // Set a frame encryptor to a particular ssrc that will intercept all
  // outgoing audio payloads frames and attempt to encrypt them and forward the
  // result to the packetizer.
  void SetFrameEncryptor(
      uint32_t ssrc,
      scoped_refptr<FrameEncryptorInterface> frame_encryptor) override;

  void SetSframeEncryptionConfig(
      SframeEncryptionConfig sframe_encryption_config) override;

  void EnableSframe() override;

  bool CanInsertDtmf() override;
  bool InsertDtmf(uint32_t ssrc, int event, int duration) override;

  void OnPacketSent(const SentPacketInfo& sent_packet) override;
  void OnNetworkRouteChanged(absl::string_view transport_name,
                             const NetworkRoute& network_route) override;
  void OnReadyToSend(bool ready) override;
  bool GetStats(VoiceMediaSendInfo* info) override;
  absl::AnyInvocable<std::optional<VoiceMediaSendInfo>()> GetStatsTask()
      override;

  // Sets a frame transformer between encoder and packetizer, to transform
  // encoded frames before sending them out the network.
  void SetEncoderToPacketizerFrameTransformer(
      uint32_t ssrc,
      scoped_refptr<FrameTransformerInterface> frame_transformer) override;

  bool SenderNackEnabled() const override;
  bool SenderNonSenderRttEnabled() const override;
  bool SetEncoderFactoryOverride(
      uint32_t ssrc,
      absl_nonnull scoped_refptr<AudioEncoderFactory> encoder_factory) override;
  void ResetEncoderFactoryOverride(uint32_t ssrc) override;

  // Mute state of the channel: true if muted, false if unmuted, or std::nullopt
  // if unset.
  std::optional<bool> muted() const;

 private:
  bool SetOptions(const AudioOptions& options);
  bool SetSendCodecs(const std::vector<Codec>& codecs,
                     std::optional<Codec> preferred_codec);
  bool SetLocalSource(uint32_t ssrc, AudioSource* source);
  bool MuteStream(uint32_t ssrc, bool mute);

  WebRtcVoiceEngine* engine() { return engine_; }
  bool SetMaxSendBitrate(int bps);
  void SetupRecording();
  void FillSendCodecStats(VoiceMediaSendInfo* voice_media_info);

  const Environment env_;
  TaskQueueBase* const worker_thread_;
  ScopedTaskSafety task_safety_;
  SequenceChecker network_thread_checker_{SequenceChecker::kDetached};

  WebRtcVoiceEngine* const engine_ = nullptr;
  std::vector<Codec> send_codecs_ RTC_GUARDED_BY(worker_thread_);

  int max_send_bitrate_bps_ RTC_GUARDED_BY(worker_thread_) = 0;
  AudioOptions options_ RTC_GUARDED_BY(worker_thread_);
  PayloadType dtmf_payload_type_ RTC_GUARDED_BY(worker_thread_);
  int dtmf_payload_freq_ RTC_GUARDED_BY(worker_thread_) = -1;
  bool send_ RTC_GUARDED_BY(worker_thread_) = false;
  Call* const call_ = nullptr;

  const MediaConfig::Audio audio_config_;

  class WebRtcAudioSendStream;
  std::map<uint32_t, WebRtcAudioSendStream*> send_streams_
      RTC_GUARDED_BY(worker_thread_);
  std::vector<RtpExtension> send_rtp_extensions_ RTC_GUARDED_BY(worker_thread_);
  std::optional<RtcpFeedbackType> rtcp_cc_ack_type_
      RTC_GUARDED_BY(worker_thread_);
  std::string mid_ RTC_GUARDED_BY(worker_thread_);
  RtcpMode rtcp_mode_ RTC_GUARDED_BY(worker_thread_);

  std::optional<AudioSendStream::Config::SendCodecSpec> send_codec_spec_
      RTC_GUARDED_BY(worker_thread_);

  // Per peer connection crypto options that last for the lifetime of the peer
  // connection.
  const CryptoOptions crypto_options_;
  SframeSendOptions sframe_options_ RTC_GUARDED_BY(worker_thread_);

  // Callback invoked whenever the list of SSRCs changes.
  absl::AnyInvocable<void(const std::set<uint32_t>&)>
      ssrc_list_changed_callback_ RTC_GUARDED_BY(worker_thread_);

  absl::AnyInvocable<void()> parameters_changed_callback_
      RTC_GUARDED_BY(worker_thread_);
};

class WebRtcVoiceReceiveChannel final
    : public MediaChannelUtil,
      public VoiceMediaReceiveChannelInterface {
 public:
  WebRtcVoiceReceiveChannel(
      const Environment& env,
      WebRtcVoiceEngine* absl_nonnull engine,
      const MediaConfig& config,
      const AudioOptions& options,
      const CryptoOptions& crypto_options,
      Call* absl_nonnull call,
      absl::AnyInvocable<void(uint32_t ssrc)> on_first_packet,
      absl::AnyInvocable<void(uint32_t ssrc, const RtpPacketInfos&, Timestamp)
                             const> on_frame_delivered_callback);

  WebRtcVoiceReceiveChannel() = delete;
  WebRtcVoiceReceiveChannel(const WebRtcVoiceReceiveChannel&) = delete;
  WebRtcVoiceReceiveChannel& operator=(const WebRtcVoiceReceiveChannel&) =
      delete;

  ~WebRtcVoiceReceiveChannel() override;

  MediaType media_type() const override { return MediaType::AUDIO; }

  VideoMediaReceiveChannelInterface* AsVideoReceiveChannel() override {
    RTC_CHECK_NOTREACHED();
    return nullptr;
  }
  VoiceMediaReceiveChannelInterface* AsVoiceReceiveChannel() override {
    return this;
  }

  const AudioOptions& options() const { return options_; }

  void SetInterface(MediaChannelNetworkInterface* iface) override {
    RTC_DCHECK_RUN_ON(&network_thread_checker_);
    iface ? network_thread_safety_->SetAlive()
          : network_thread_safety_->SetNotAlive();
    MediaChannelUtil::SetInterface(iface);
  }
  bool SetReceiverParameters(const AudioReceiverParameters& params) override;
  RtpParameters GetRtpReceiverParameters(uint32_t ssrc) const override;
  RtpParameters GetDefaultRtpReceiveParameters() const override;

  void SetReceive(bool receive) override;
  bool AddRecvStream(const StreamParams& sp) override;
  bool RemoveRecvStream(uint32_t ssrc) override;
  void ResetUnsignaledRecvStream() override;
  absl::AnyInvocable<void() &&> GetResetUnsignaledRecvStreamTask() override;
  std::optional<uint32_t> GetUnsignaledSsrc() const override;
  std::vector<uint32_t> GetUnsignaledSsrcs() const override;

  void OnDemuxerCriteriaUpdatePending() override;
  void OnDemuxerCriteriaUpdateComplete() override;

  // E2EE Frame API
  // Set a frame decryptor to a particular ssrc that will intercept all
  // incoming audio payloads and attempt to decrypt them before forwarding the
  // result.
  void SetFrameDecryptor(
      uint32_t ssrc,
      scoped_refptr<FrameDecryptorInterface> frame_decryptor) override;

  void EnableSframe() override;

  bool SetOutputVolume(uint32_t ssrc, double volume) override;
  // Applies the new volume to current and future unsignaled streams.
  bool SetDefaultOutputVolume(double volume) override;

  bool SetBaseMinimumPlayoutDelayMs(uint32_t ssrc, int delay_ms) override;
  std::optional<int> GetBaseMinimumPlayoutDelayMs(uint32_t ssrc) const override;

  void OnPacketReceived(RtpPacketReceived packet) override;
  bool GetStats(VoiceMediaReceiveInfo* info,
                bool get_and_clear_legacy_stats) override;
  absl::AnyInvocable<std::optional<VoiceMediaReceiveInfo>()> GetStatsTask(
      bool reset_legacy) override;

  // Set the audio sink for an existing stream.
  void SetRawAudioSink(uint32_t ssrc,
                       std::unique_ptr<AudioSinkInterface> sink) override;
  // Will set the audio sink on the latest unsignaled stream, future or
  // current. Only one stream at a time will use the sink.
  void SetDefaultRawAudioSink(
      std::unique_ptr<AudioSinkInterface> sink) override;

  void SetDepacketizerToDecoderFrameTransformer(
      uint32_t ssrc,
      scoped_refptr<FrameTransformerInterface> frame_transformer) override;

  void SetRtcpMode(enum RtcpMode mode) override;
  void SetReceiveNackEnabled(bool enabled) override;
  void SetReceiveNonSenderRttEnabled(bool enabled) override;

 private:
  bool SetRecvCodecs(const std::vector<Codec>& codecs);
  bool SetLocalSource(uint32_t ssrc, AudioSource* source);
  bool MuteStream(uint32_t ssrc, bool mute);

  WebRtcVoiceEngine* engine() { return engine_; }
  void SetupRecording();

  // Expected to be invoked once per packet that belongs to this channel that
  // can not be demuxed. Returns true if a default receive stream has been
  // created.
  bool MaybeCreateDefaultReceiveStream(const RtpPacketReceived& packet);
  // Check if 'ssrc' is an unsignaled stream, and if so mark it as not being
  // unsignaled anymore (i.e. it is now removed, or signaled), and return true.
  bool MaybeDeregisterUnsignaledRecvStream(uint32_t ssrc);
  void FillReceiveCodecStats(VoiceMediaReceiveInfo* voice_media_info);

  const Environment env_;
  TaskQueueBase* const worker_thread_;
  ScopedTaskSafety task_safety_;
  scoped_refptr<PendingTaskSafetyFlag> network_thread_safety_;
  SequenceChecker network_thread_checker_{SequenceChecker::kDetached};

  WebRtcVoiceEngine* const engine_ = nullptr;

  std::map<int, SdpAudioFormat> decoder_map_ RTC_GUARDED_BY(worker_thread_);

  const AudioOptions options_;
  bool recv_nack_enabled_ RTC_GUARDED_BY(worker_thread_) = false;
  enum RtcpMode recv_rtcp_mode_ RTC_GUARDED_BY(worker_thread_) =
      RtcpMode::kCompound;
  bool enable_non_sender_rtt_ RTC_GUARDED_BY(worker_thread_) = false;
  bool playout_ RTC_GUARDED_BY(worker_thread_) = false;
  Call* const call_ = nullptr;

  MediaConfig::Audio audio_config_;

  // Queue of unsignaled SSRCs; oldest at the beginning.
  std::vector<uint32_t> unsignaled_recv_ssrcs_ RTC_GUARDED_BY(worker_thread_);

  // This is a stream param that comes from the remote description, but wasn't
  // signaled with any a=ssrc lines. It holds the information that was signaled
  // before the unsignaled receive stream is created when the first packet is
  // received.
  StreamParams unsignaled_stream_params_ RTC_GUARDED_BY(worker_thread_);

  // Volume for unsignaled streams, which may be set before the stream exists.
  double default_recv_volume_ RTC_GUARDED_BY(worker_thread_) = 1.0;

  // Delay for unsignaled streams, which may be set before the stream exists.
  int default_recv_base_minimum_delay_ms_ RTC_GUARDED_BY(worker_thread_) = 0;

  // Sink for latest unsignaled stream - may be set before the stream exists.
  std::unique_ptr<AudioSinkInterface> default_sink_
      RTC_GUARDED_BY(worker_thread_);
  std::string mid_ RTC_GUARDED_BY(worker_thread_);

  class WebRtcAudioReceiveStream;
  std::map<uint32_t, WebRtcAudioReceiveStream*> recv_streams_
      RTC_GUARDED_BY(worker_thread_);

  AudioReceiverParameters recv_params_ RTC_GUARDED_BY(worker_thread_);

  // Per peer connection crypto options that last for the lifetime of the peer
  // connection.
  const CryptoOptions crypto_options_;
  SframeReceiveOptions sframe_options_ RTC_GUARDED_BY(worker_thread_);
  // Unsignaled streams have an option to have a frame decryptor set on them.
  scoped_refptr<FrameDecryptorInterface> unsignaled_frame_decryptor_
      RTC_GUARDED_BY(worker_thread_);
  scoped_refptr<FrameTransformerInterface> unsignaled_frame_transformer_
      RTC_GUARDED_BY(worker_thread_);

  // Channel-level callback invoked when a receive stream on this channel
  // receives its first packet.
  absl::AnyInvocable<void(uint32_t ssrc)> on_first_packet_
      RTC_GUARDED_BY(worker_thread_);
  const absl::AnyInvocable<void(uint32_t ssrc, const RtpPacketInfos&, Timestamp)
                               const>
      on_frame_delivered_callback_;
};

}  //  namespace webrtc


#endif  // MEDIA_ENGINE_WEBRTC_VOICE_ENGINE_H_
