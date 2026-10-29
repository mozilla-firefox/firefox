/*
 *  Copyright 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// This file contains classes that implement RtpSenderInterface.
// An RtpSender associates a MediaStreamTrackInterface with an underlying
// transport (provided by AudioProviderInterface/VideoProviderInterface)

#ifndef PC_RTP_SENDER_H_
#define PC_RTP_SENDER_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/crypto/frame_encryptor_interface.h"
#include "api/dtls_transport_interface.h"
#include "api/dtmf_sender_interface.h"
#include "api/encoded_audio_frame_injector_interface.h"
#include "api/encoded_video_frame_injector_interface.h"
#include "api/environment/environment.h"
#include "api/frame_transformer_interface.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/rtp_sender_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/sframe/sframe_encryptor_interface.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "media/base/audio_source.h"
#include "media/base/codec.h"
#include "media/base/media_channel.h"
#include "modules/sframe/sframe_encryption_config.h"
#include "pc/dtmf_sender.h"
#include "pc/encoded_audio_frame_injector.h"
#include "pc/encoded_video_frame_injector.h"
#include "pc/legacy_stats_collector_interface.h"
#include "pc/scoped_operations_batcher.h"
#include "pc/simulcast_description.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/system/plan_b_only.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {

bool UnimplementedRtpParameterHasValue(const RtpParameters& parameters);

// Internal interface used by PeerConnection.
class RtpSenderInternal : public RtpSenderInterface {
 public:
  // Sets the underlying MediaEngine channel associated with this RtpSender.
  // A VoiceMediaChannel should be used for audio RtpSenders and
  // a VideoMediaChannel should be used for video RtpSenders.
  // Must call SetMediaChannel(nullptr) before the media channel is destroyed.
  virtual void SetMediaChannel(MediaSendChannelInterface* media_channel) = 0;

  // Used to set the SSRC of the sender, once a local description has been set.
  // If `ssrc` is 0, this indiates that the sender should disconnect from the
  // underlying transport (this occurs if the sender isn't seen in a local
  // description).
  PLAN_B_ONLY virtual void SetSsrc(uint32_t ssrc) = 0;

  // Same as `SetSsrc()` but performs the worker thread part of the work in a
  // task that the caller must run on the worker thread.
  // `layer_count` is the number of send layers that the applied local
  // description describes for this sender, i.e. the number of primary SSRCs in
  // the `StreamParams` for the stream the sender is being attached to. This is
  // the same value that the media channel derives its encodings from (see
  // `CreateRtpParametersWithEncodings()`), which allows the sender to
  // reconcile the encodings the application asked for with what was negotiated
  // without querying the media channel. Must be 0 when `ssrc` is 0.
  [[nodiscard]] virtual ScopedOperationsBatcher::BatchTaskWithFinalizer
  SetSsrcTask(uint32_t ssrc, size_t layer_count) = 0;

  virtual void set_stream_ids(const std::vector<std::string>& stream_ids) = 0;
  virtual void set_init_send_encodings(
      const std::vector<RtpEncodingParameters>& init_send_encodings) = 0;
  virtual void set_transport(
      scoped_refptr<DtlsTransportInterface> dtls_transport) = 0;

  virtual void Stop() = 0;

  // Cleans up the state on the signaling thread, as `Stop()` does, but does
  // not perform the worker thread cleanup directly. Instead, returns a task
  // that the caller must invoke on the worker thread to perform that work.
  // Note that if no worker thread needs to be done, the retuned task will be
  // empty.
  virtual absl::AnyInvocable<void() &&> DetachTrackAndGetStopTask() = 0;

  // `GetParameters` and `SetParameters` operate with a transactional model.
  // Allow access to get/set parameters without invalidating transaction id.
  virtual RtpParameters GetParametersInternal(bool with_all_layers) const = 0;
  virtual RTCError SetParametersInternal(const RtpParameters& parameters,
                                         SetParametersCallback,
                                         bool blocking) = 0;
  virtual void SetCachedParameters(std::optional<RtpParameters> parameters) = 0;

  // GetParameters and SetParameters will remove deactivated simulcast layers
  // and restore them on SetParameters. This is probably a Bad Idea, but we
  // do not know who depends on this behavior
  virtual RtpParameters GetParametersInternalWithAllLayers() const = 0;
  virtual RTCError SetParametersInternalWithAllLayers(
      const RtpParameters& parameters) = 0;

  // Returns an ID that changes every time SetTrack() is called, but
  // otherwise remains constant. Used to generate IDs for stats.
  // The special value zero means that no track is attached.
  virtual int AttachmentId() const = 0;

  // Disables the layers identified by the specified RIDs.
  // If the specified list is empty, this is a no-op.
  virtual RTCError DisableEncodingLayers(
      const std::vector<std::string>& rid) = 0;

  // Used by the owning transceiver to inform the sender on the currently
  // selected codecs.
  virtual void SetSendCodecs(std::vector<Codec> send_codecs) = 0;
  virtual std::vector<Codec> GetSendCodecs() const = 0;

  virtual void NotifyFirstPacketSent() = 0;
  virtual void OnParametersChanged() = 0;
};

// Shared implementation for RtpSenderInternal interface.
class RtpSenderBase : public RtpSenderInternal, public ObserverInterface {
 public:
  class SetStreamsObserver {
   public:
    virtual ~SetStreamsObserver() = default;
    virtual void OnSetStreams() = 0;
  };

  ~RtpSenderBase() override;

  // Sets the underlying MediaEngine channel associated with this RtpSender.
  // A VoiceMediaChannel should be used for audio RtpSenders and
  // a VideoMediaChannel should be used for video RtpSenders.
  // Must call SetMediaChannel(nullptr) before the media channel is destroyed.
  void SetMediaChannel(MediaSendChannelInterface* media_channel) override;

  bool SetTrack(MediaStreamTrackInterface* track) override;
  scoped_refptr<MediaStreamTrackInterface> track() const override {
    RTC_DCHECK_RUN_ON(signaling_thread_);
    return frame_injector_ ? nullptr : track_;
  }

  MediaType media_type() const final { return media_type_; }

  RtpParameters GetParameters() const override;
  RTCError SetParameters(const RtpParameters& parameters) override;
  void SetParametersAsync(const RtpParameters& parameters,
                          SetParametersCallback callback) override;

  // `GetParameters` and `SetParameters` operate with a transactional model.
  // Allow access to get/set parameters without invalidating transaction id.
  RtpParameters GetParametersInternal(
      bool with_all_layers = false) const override;
  RTCError SetParametersInternal(const RtpParameters& parameters,
                                 SetParametersCallback callback = nullptr,
                                 bool blocking = true) override;
  void SetCachedParameters(std::optional<RtpParameters> parameters) override;
  RTCError CheckSetParameters(const RtpParameters& parameters);
  RtpParameters GetParametersInternalWithAllLayers() const override;
  RTCError SetParametersInternalWithAllLayers(
      const RtpParameters& parameters) override;
  std::optional<RTCError> ValidateAndMaybeUpdateInitParameters(
      const RtpParameters& parameters) RTC_RUN_ON(signaling_thread_);

  // Used to set the SSRC of the sender, once a local description has been set.
  // If `ssrc` is 0, this indiates that the sender should disconnect from the
  // underlying transport (this occurs if the sender isn't seen in a local
  // description).
  PLAN_B_ONLY void SetSsrc(uint32_t ssrc) override;
  ScopedOperationsBatcher::BatchTaskWithFinalizer SetSsrcTask(
      uint32_t ssrc,
      size_t layer_count) override;

  uint32_t ssrc() const override {
    RTC_DCHECK_RUN_ON(signaling_thread_);
    return ssrc_;
  }

  std::vector<std::string> stream_ids() const override {
    RTC_DCHECK_RUN_ON(signaling_thread_);
    return stream_ids_;
  }

  // Set stream ids, eliminating duplicates in the process.
  void set_stream_ids(const std::vector<std::string>& stream_ids) override;
  void SetStreams(const std::vector<std::string>& stream_ids) override;

  std::string id() const override { return id_; }

  void set_init_send_encodings(
      const std::vector<RtpEncodingParameters>& init_send_encodings) override {
    init_parameters_.encodings = init_send_encodings;
  }
  std::vector<RtpEncodingParameters> init_send_encodings() const override {
    RTC_DCHECK_RUN_ON(signaling_thread_);
    return init_parameters_.encodings;
  }

  void set_transport(
      scoped_refptr<DtlsTransportInterface> dtls_transport) override {
    dtls_transport_ = dtls_transport;
  }
  scoped_refptr<DtlsTransportInterface> dtls_transport() const override {
    RTC_DCHECK_RUN_ON(signaling_thread_);
    return dtls_transport_;
  }

  void SetFrameEncryptor(
      scoped_refptr<FrameEncryptorInterface> frame_encryptor) override;

  scoped_refptr<FrameEncryptorInterface> GetFrameEncryptor() const override {
    return frame_encryptor_;
  }

  void Stop() override;
  absl::AnyInvocable<void() &&> DetachTrackAndGetStopTask() override;

  // Returns an ID that changes every time SetTrack() is called, but
  // otherwise remains constant. Used to generate IDs for stats.
  // The special value zero means that no track is attached.
  int AttachmentId() const override { return attachment_id_; }

  // Disables the layers identified by the specified RIDs.
  // If the specified list is empty, this is a no-op.
  RTCError DisableEncodingLayers(const std::vector<std::string>& rid) override;

  void SetFrameTransformer(
      scoped_refptr<FrameTransformerInterface> frame_transformer) override;

  scoped_refptr<EncodedVideoFrameInjectorInterface>
  CreateEncodedVideoFrameInjector(
      KeyFrameCallback keyframe_callback,
      BitrateInfoCallback bitrate_callback) override;

  scoped_refptr<EncodedAudioFrameInjectorInterface>
  CreateEncodedAudioFrameInjector(
      TargetBitrateCallback bitrate_callback) override;

  RTCErrorOr<scoped_refptr<SframeEncryptorInterface>>
  CreateSframeEncryptorOrError(const SframeEncryptorInit& options) override;

  void SetEncoderSelector(
      std::unique_ptr<VideoEncoderFactory::EncoderSelectorInterface>
          encoder_selector) override;

  void SetEncoderSelector(
      scoped_refptr<VideoEncoderFactory::EncoderSelectorInterface>
          encoder_selector) override;

  void SetEncoderSelectorOnChannel();

  void SetSendCodecs(std::vector<Codec> send_codecs) override {
    send_codecs_ = send_codecs;
  }
  std::vector<Codec> GetSendCodecs() const override { return send_codecs_; }

  void NotifyFirstPacketSent() override;
  void OnParametersChanged() override;
  void SetObserver(RtpSenderObserverInterface* observer) override;

 protected:
  // Resets `cached_parameters_`. Used by operations that change, or may
  // change, the parameters of the media channel without reading them back
  // while this thread is blocked.
  void InvalidateCache() {
    RTC_DCHECK_RUN_ON(signaling_thread_);
    cached_parameters_.reset();
    ++cache_generation_;
  }

  // Sets `cached_parameters_` to `parameters`, which must have been read from
  // the media channel while this thread was blocked waiting for them, i.e.
  // after any change that this thread has requested has been applied.
  void UpdateCache(RtpParameters parameters);

  // If `set_streams_observer` is not null, it is invoked when SetStreams()
  // is called. `set_streams_observer` is not owned by this object. If not
  // null, it must be valid at least until this sender becomes stopped.
  RtpSenderBase(const Environment& env,
                Thread* signaling_thread,
                Thread* worker_thread,
                absl::string_view id,
                MediaType media_type,
                SetStreamsObserver* set_streams_observer,
                absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
                MediaSendChannelInterface* media_channel,
                std::vector<std::string> stream_ids,
                std::vector<RtpEncodingParameters> init_send_encodings,
                std::vector<Codec> send_codecs);

  // TODO(bugs.webrtc.org/8694): Since SSRC == 0 is technically valid, figure
  // out some other way to test if we have a valid SSRC.
  bool can_send_track() const RTC_RUN_ON(signaling_thread_) {
    return track_ && ssrc_;
  }

  virtual std::string track_kind() const = 0;

  // Enable sending on the media channel.
  virtual void SetSend() = 0;
  // Disable sending on the media channel.
  virtual void ClearSend() = 0;
  virtual void ClearSend_w(uint32_t ssrc) RTC_RUN_ON(worker_thread_) = 0;

  // Template method pattern to allow subclasses to add custom behavior for
  // when tracks are attached, detached, and for adding tracks to statistics.
  virtual void AttachTrack() RTC_RUN_ON(signaling_thread_) {}
  virtual void DetachTrack() RTC_RUN_ON(signaling_thread_) {}
  virtual void AddTrackToStats() RTC_RUN_ON(signaling_thread_) {}
  virtual void RemoveTrackFromStats() RTC_RUN_ON(signaling_thread_) {}

  // Special case for downstream code that calls into this code with a
  // configuration where the signaling, worker and network threads are all
  // configured to be the same thread.
  RTCError SetParametersInternalWorkaround(const RtpParameters& parameters);

  // Helper function to extract the factory override from the injector.
  // At least one of the returned value is nullptr, both if no injetor is set.
  // Returns what CreateEncoderFactory() would return when called on the
  // injector.
  std::tuple<std::unique_ptr<VideoEncoderFactory>,
             scoped_refptr<AudioEncoderFactory>>
  MaybeCreateFactoryOverride();
  void ClearFrameInjector();

  // Applies the encodings and degradation preference the application
  // configured before negotiation to the send stream for `ssrc`, and clears
  // them. A nullopt `ssrc` means that there is no send stream to apply them
  // to. Returns the parameters the media channel ended up with, or nullopt if
  // the media channel was left untouched.
  std::optional<RtpParameters> ApplyInitParameters_w(
      std::optional<uint32_t> ssrc) RTC_RUN_ON(worker_thread_);

  const Environment env_;
  TaskQueueBase* const signaling_thread_;
  Thread* const worker_thread_;
  // TODO(tommi): The type for ssrc_ should be `std::optional<uint32_t>`
  // since 0 is a legal SSRC value.
  uint32_t ssrc_ RTC_GUARDED_BY(signaling_thread_) = 0;
  bool stopped_ RTC_GUARDED_BY(signaling_thread_) = false;
  int attachment_id_ = 0;
  const std::string id_;
  const MediaType media_type_;

  std::vector<std::string> stream_ids_;
  RtpParameters init_parameters_;
  // The parameters of the media channel for `ssrc_`, cached on the signaling
  // thread to avoid blocking calls to the worker thread. Operations that
  // change the parameters of the media channel either update the cache with
  // the resulting parameters (see `UpdateCache()`) or invalidate it (see
  // `InvalidateCache()`). `GetParametersInternal()` populates an empty cache.
  mutable std::optional<RtpParameters> cached_parameters_
      RTC_GUARDED_BY(signaling_thread_);
  // Incremented by `InvalidateCache()` and `UpdateCache()`. The completion of
  // a non-blocking `SetParametersInternal()` call delivers parameters that
  // were read on the worker thread before the completion was posted. Those
  // only populate the cache if the cache is empty and the generation hasn't
  // changed since the call was made, i.e. if no other operation has changed
  // the parameters in the meantime.
  uint64_t cache_generation_ RTC_GUARDED_BY(signaling_thread_) = 0;
  std::vector<Codec> send_codecs_;

  // TODO(tommi): Several member variables in this class (ssrc_, stopped_, etc)
  // are accessed from more than one thread without a guard or lock. Internally
  // there are also several Invoke()s that we could remove since the upstream
  // code may already be performing several operations on the worker thread. Add
  // RTC_GUARDED_BY(worker_thread_).
  MediaSendChannelInterface* media_channel_ RTC_GUARDED_BY(worker_thread_) =
      nullptr;
  // Apply RTC_GUARDED_BY(signaling_thread_) when not accessed from worker.
  scoped_refptr<MediaStreamTrackInterface> track_;

  scoped_refptr<DtlsTransportInterface> dtls_transport_;
  // Apply RTC_GUARDED_BY(worker_thread_) when no longer accessed from unknown
  // threads. Alternatively make const.
  scoped_refptr<FrameEncryptorInterface> frame_encryptor_;
  // `last_transaction_id_` is used to verify that `SetParameters` is receiving
  // the parameters object that was last returned from `GetParameters`.
  // As such, it is used for internal verification and is not observable by the
  // the client. It is marked as mutable to enable `GetParameters` to be a
  // const method.
  mutable std::optional<std::string> last_transaction_id_
      RTC_GUARDED_BY(signaling_thread_);
  std::vector<std::string> disabled_rids_;

  SetStreamsObserver* const set_streams_observer_ = nullptr;
  RtpSenderObserverInterface* observer_ RTC_GUARDED_BY(signaling_thread_) =
      nullptr;
  bool sent_first_packet_ = false;

  using FrameInjector =
      std::optional<std::variant<scoped_refptr<EncodedVideoFrameInjector>,
                                 scoped_refptr<EncodedAudioFrameInjector>>>;
  FrameInjector frame_injector_ RTC_GUARDED_BY(signaling_thread_);

  scoped_refptr<FrameTransformerInterface> frame_transformer_;
  scoped_refptr<VideoEncoderFactory::EncoderSelectorInterface>
      encoder_selector_;

  scoped_refptr<PendingTaskSafetyFlag> worker_safety_;
  ScopedTaskSafety signaling_safety_;

  absl::AnyInvocable<RTCError()> enable_sframe_at_owner_
      RTC_GUARDED_BY(signaling_thread_);

  std::optional<SframeEncryptionConfig> sframe_encryption_config_
      RTC_GUARDED_BY(worker_thread_);
};

// LocalAudioSinkAdapter receives data callback as a sink to the local
// AudioTrack, and passes the data to the sink of AudioSource.
class LocalAudioSinkAdapter : public AudioTrackSinkInterface,
                              public AudioSource {
 public:
  LocalAudioSinkAdapter();
  ~LocalAudioSinkAdapter() override;

 private:
  // AudioSinkInterface implementation.
  void OnData(const void* audio_data,
              int bits_per_sample,
              int sample_rate,
              size_t number_of_channels,
              size_t number_of_frames,
              std::optional<int64_t> absolute_capture_timestamp_ms) override;

  // AudioSinkInterface implementation.
  void OnData(const void* audio_data,
              int bits_per_sample,
              int sample_rate,
              size_t number_of_channels,
              size_t number_of_frames) override {
    OnData(audio_data, bits_per_sample, sample_rate, number_of_channels,
           number_of_frames,
           /*absolute_capture_timestamp_ms=*/std::nullopt);
  }

  // AudioSinkInterface implementation.
  int NumPreferredChannels() const override { return num_preferred_channels_; }

  // webrtc::AudioSource implementation.
  void SetSink(AudioSource::Sink* sink) override;

  AudioSource::Sink* sink_;
  // Critical section protecting `sink_`.
  Mutex lock_;
  int num_preferred_channels_ = -1;
};

class AudioRtpSender : public DtmfProviderInterface, public RtpSenderBase {
 public:
  // Construct an RtpSender for audio with the given sender ID.
  // The sender is initialized with no track to send and no associated streams.
  // StatsCollector provided so that Add/RemoveLocalAudioTrack can be called
  // at the appropriate times.
  // If `set_streams_observer` is not null, it is invoked when SetStreams()
  // is called. `set_streams_observer` is not owned by this object. If not
  // null, it must be valid at least until this sender becomes stopped.
  static scoped_refptr<AudioRtpSender> Create(
      const Environment& env,
      Thread* signaling_thread,
      Thread* worker_thread,
      absl::string_view id,
      LegacyStatsCollectorInterface* stats,
      SetStreamsObserver* set_streams_observer,
      absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
      MediaSendChannelInterface* media_channel,
      std::vector<std::string> stream_ids = {},
      std::vector<RtpEncodingParameters> init_send_encodings =
          std::vector<RtpEncodingParameters>(1),
      std::vector<Codec> send_codecs = {});
  ~AudioRtpSender() override;

  // DtmfSenderProvider implementation.
  bool CanInsertDtmf() override;
  bool InsertDtmf(int code, int duration) override;

  // ObserverInterface implementation.
  void OnChanged() override;

  std::string track_kind() const override {
    return MediaStreamTrackInterface::kAudioKind;
  }

  scoped_refptr<DtmfSenderInterface> GetDtmfSender() const override;
  RTCError GenerateKeyFrame(const std::vector<std::string>& rids) override;

 protected:
  AudioRtpSender(const Environment& env,
                 Thread* signaling_thread,
                 Thread* worker_thread,
                 absl::string_view id,
                 LegacyStatsCollectorInterface* legacy_stats,
                 SetStreamsObserver* set_streams_observer,
                 absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
                 MediaSendChannelInterface* media_channel,
                 std::vector<std::string> stream_ids,
                 std::vector<RtpEncodingParameters> init_send_encodings,
                 std::vector<Codec> send_codecs);

  void SetSend() override;
  void ClearSend() override;
  void ClearSend_w(uint32_t ssrc) RTC_RUN_ON(worker_thread_) override;

  // Hooks to allow custom logic when tracks are attached and detached.
  void AttachTrack() RTC_RUN_ON(signaling_thread_) override;
  void DetachTrack() RTC_RUN_ON(signaling_thread_) override;
  void AddTrackToStats() RTC_RUN_ON(signaling_thread_) override;
  void RemoveTrackFromStats() RTC_RUN_ON(signaling_thread_) override;

 private:
  VoiceMediaSendChannelInterface* voice_media_channel()
      RTC_RUN_ON(worker_thread_) {
    return media_channel_->AsVoiceSendChannel();
  }
  scoped_refptr<AudioTrackInterface> audio_track() const
      RTC_RUN_ON(signaling_thread_) {
    return scoped_refptr<AudioTrackInterface>(
        static_cast<AudioTrackInterface*>(track_.get()));
  }

  LegacyStatsCollectorInterface* const legacy_stats_ = nullptr;
  const scoped_refptr<DtmfSender> dtmf_sender_;
  const scoped_refptr<DtmfSenderInterface> dtmf_sender_proxy_;
  bool cached_track_enabled_ = false;

  // Used to pass the data callback from the `track_` to the other end of
  // webrtc::AudioSource.
  const std::unique_ptr<LocalAudioSinkAdapter> sink_adapter_;
};

class VideoRtpSender : public RtpSenderBase {
 public:
  // Construct an RtpSender for video with the given sender ID.
  // The sender is initialized with no track to send and no associated streams.
  // If `set_streams_observer` is not null, it is invoked when SetStreams()
  // is called. `set_streams_observer` is not owned by this object. If not
  // null, it must be valid at least until this sender becomes stopped.
  // `initial_simulcast_layers` filters the initial encodings by RID and sets
  // their active state. Works with `simulcast_rejected` to determine the final
  // set of layers.
  static scoped_refptr<VideoRtpSender> Create(
      const Environment& env,
      Thread* signaling_thread,
      Thread* worker_thread,
      absl::string_view id,
      SetStreamsObserver* set_streams_observer,
      absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
      MediaSendChannelInterface* media_channel,
      const std::vector<RtpEncodingParameters>& init_send_encodings,
      bool simulcast_rejected,
      const std::vector<SimulcastLayer>& initial_simulcast_layers,
      std::vector<std::string> stream_ids = {},
      std::vector<Codec> send_codecs = {});
  ~VideoRtpSender() override;

  // ObserverInterface implementation
  void OnChanged() override;

  std::string track_kind() const override {
    return MediaStreamTrackInterface::kVideoKind;
  }

  scoped_refptr<DtmfSenderInterface> GetDtmfSender() const override;
  RTCError GenerateKeyFrame(const std::vector<std::string>& rids) override;

 protected:
  VideoRtpSender(const Environment& env,
                 Thread* signaling_thread,
                 Thread* worker_thread,
                 absl::string_view id,
                 SetStreamsObserver* set_streams_observer,
                 absl::AnyInvocable<RTCError()> enable_sframe_at_owner,
                 MediaSendChannelInterface* media_channel,
                 const std::vector<RtpEncodingParameters>& init_send_encodings,
                 bool simulcast_rejected,
                 const std::vector<SimulcastLayer>& initial_simulcast_layers,
                 std::vector<std::string> stream_ids,
                 std::vector<Codec> send_codecs);

  void SetSend() override;
  void ClearSend() override;
  void ClearSend_w(uint32_t ssrc) RTC_RUN_ON(worker_thread_) override;

  // Hook to allow custom logic when tracks are attached.
  void AttachTrack() RTC_RUN_ON(signaling_thread_) override;

 private:
  VideoMediaSendChannelInterface* video_media_channel()
      RTC_RUN_ON(worker_thread_) {
    return media_channel_->AsVideoSendChannel();
  }
  scoped_refptr<VideoTrackInterface> video_track() const
      RTC_RUN_ON(signaling_thread_) {
    return scoped_refptr<VideoTrackInterface>(
        static_cast<VideoTrackInterface*>(track_.get()));
  }

  VideoTrackInterface::ContentHint cached_track_content_hint_ =
      VideoTrackInterface::ContentHint::kNone;
};

}  // namespace webrtc

#endif  // PC_RTP_SENDER_H_
