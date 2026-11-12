/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/channel_send.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/audio_codecs/audio_encoder.h"
#include "api/audio_codecs/audio_format.h"
#include "api/call/bitrate_allocation.h"
#include "api/call/transport.h"
#include "api/crypto/crypto_options.h"
#include "api/crypto/frame_encryptor_interface.h"
#include "api/environment/environment.h"
#include "api/frame_transformer_interface.h"
#include "api/function_view.h"
#include "api/make_ref_counted.h"
#include "api/media_types.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_headers.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "audio/channel_send_frame_transformer_delegate.h"
#include "audio/utility/audio_frame_operations.h"
#include "call/rtp_transport_controller_send_interface.h"
#include "call/sframe_options.h"
#include "modules/audio_coding/include/audio_coding_module.h"
#include "modules/audio_coding/include/audio_coding_module_typedefs.h"
#include "modules/audio_processing/rms_level.h"
#include "modules/pacing/packet_router.h"
#include "modules/rtp_rtcp/include/report_block_data.h"
#include "modules/rtp_rtcp/include/rtcp_statistics.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_impl2.h"
#include "modules/rtp_rtcp/source/rtp_sender_audio.h"
#include "modules/sframe/sframe_encryption_config.h"
#include "rtc_base/buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/event.h"
#include "rtc_base/logging.h"
#include "rtc_base/race_checker.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/system/no_unique_address.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/trace_event.h"
#include "system_wrappers/include/metrics.h"

namespace webrtc {
namespace voe {

namespace {

class RtpPacketSenderProxy;

class RtcpCounterObserver : public RtcpPacketTypeCounterObserver {
 public:
  explicit RtcpCounterObserver(uint32_t ssrc) : ssrc_(ssrc) {}

  void RtcpPacketTypesCounterUpdated(
      uint32_t ssrc,
      const RtcpPacketTypeCounter& packet_counter) override {
    if (ssrc_ != ssrc) {
      return;
    }

    MutexLock lock(&mutex_);
    packet_counter_ = packet_counter;
  }

  RtcpPacketTypeCounter GetCounts() {
    MutexLock lock(&mutex_);
    return packet_counter_;
  }

 private:
  Mutex mutex_;
  const uint32_t ssrc_;
  RtcpPacketTypeCounter packet_counter_;
};

class AudioBitrateAccountant {
 public:
  void RegisterPacketOverhead(int packet_byte_overhead) {
    packet_overhead_ = DataSize::Bytes(packet_byte_overhead);
  }

  void Reset() {
    rate_last_frame_ = DataRate::BitsPerSec(0);
    next_frame_duration_ = TimeDelta::Millis(0);
    report_rate_ = std::nullopt;
  }

  // A new frame is formed when bytesize is nonzero.
  void UpdateBpsEstimate(DataSize payload_size, TimeDelta frame_duration) {
    next_frame_duration_ += frame_duration;
    // Do not have a full frame yet.
    if (payload_size.bytes() == 0)
      return;

    // We report the larger of the rates computed using the last frame, and
    // second last frame. Under DTX, frame sizes sometimes alternate, it is
    // preferable to report the upper envelop.
    DataRate rate_cur_frame =
        (payload_size + packet_overhead_) / next_frame_duration_;

    report_rate_ =
        (rate_cur_frame > rate_last_frame_) ? rate_cur_frame : rate_last_frame_;

    rate_last_frame_ = rate_cur_frame;
    next_frame_duration_ = TimeDelta::Millis(0);
  }

  std::optional<DataRate> GetUsedRate() const { return report_rate_; }

 private:
  TimeDelta next_frame_duration_ = TimeDelta::Millis(0);
  DataSize packet_overhead_ = DataSize::Bytes(72);
  DataRate rate_last_frame_ = DataRate::BitsPerSec(0);
  std::optional<DataRate> report_rate_;
};

class ChannelSend : public ChannelSendInterface,
                    public AudioPacketizationCallback,  // receive encoded
                                                        // packets from the ACM
                    public RtcpPacketTypeCounterObserver,
                    public ReportBlockDataObserver {
 public:
  ChannelSend(const Environment& env,
              Transport* rtp_transport,
              RtcpRttStats* rtcp_rtt_stats,
              FrameEncryptorInterface* frame_encryptor,
              const CryptoOptions& crypto_options,
              bool extmap_allow_mixed,
              int rtcp_report_interval_ms,
              uint32_t ssrc,
              const std::optional<RtpState>& rtp_state,
              absl::string_view mid,
              scoped_refptr<FrameTransformerInterface> frame_transformer,
              RtpTransportControllerSendInterface* transport_controller);

  ~ChannelSend() override;

  // Send using this encoder, with this payload type.
  void SetEncoder(int payload_type,
                  const SdpAudioFormat& encoder_format,
                  std::unique_ptr<AudioEncoder> encoder) override;
  void ModifyEncoder(
      FunctionView<void(std::unique_ptr<AudioEncoder>*)> modifier) override;
  void CallEncoder(FunctionView<void(AudioEncoder*)> modifier) override;

  // API methods
  void StartSend() override;
  void StopSend() override;

  // Codecs
  void OnBitrateAllocation(BitrateAllocationUpdate update) override;
  int GetTargetBitrate() const override;

  // Network
  void ReceivedRTCPPacket(const uint8_t* data, size_t length) override;

  // Muting, Volume and Level.
  void SetInputMute(bool enable) override;

  // CSRCs.
  void SetCsrcs(std::span<const uint32_t> csrcs) override;

  // Stats.
  ANAStats GetANAStatistics() const override;

  // Used by AudioSendStream.
  RtpRtcpInterface* GetRtpRtcp() const override;

  void RegisterCngPayloadType(int payload_type, int payload_frequency) override;

  // DTMF.
  bool SendTelephoneEventOutband(int event, int duration_ms) override;
  void SetSendTelephoneEventPayloadType(int payload_type,
                                        int payload_frequency) override;

  // RTP+RTCP
  void SetSendAudioLevelIndicationStatus(RtpHeaderExtensionId id) override;

  void RegisterSenderCongestionControlObjects(
      RtpTransportControllerSendInterface* transport) override;
  void ResetSenderCongestionControlObjects() override;
  void SetRTCP_CNAME(absl::string_view c_name) override;
  std::vector<ReportBlockData> GetRemoteRTCPReportBlocks() const override;
  ChannelSendStatistics GetRTCPStatistics() const override;

  // ProcessAndEncodeAudio() posts a task on the shared encoder task queue,
  // which in turn calls (on the queue) ProcessAndEncodeAudioOnTaskQueue() where
  // the actual processing of the audio takes place. The processing mainly
  // consists of encoding and preparing the result for sending by adding it to a
  // send queue.
  // The main reason for using a task queue here is to release the native,
  // OS-specific, audio capture thread as soon as possible to ensure that it
  // can go back to sleep and be prepared to deliver an new captured audio
  // packet.
  void ProcessAndEncodeAudio(std::unique_ptr<AudioFrame> audio_frame) override;

  // E2EE Custom Audio Frame Encryption
  void SetFrameEncryptor(
      scoped_refptr<FrameEncryptorInterface> frame_encryptor) override;

  void SetSframeOptions(const SframeSendOptions& options) override;

  // Sets a frame transformer between encoder and packetizer, to transform
  // encoded frames before sending them out the network.
  void SetEncoderToPacketizerFrameTransformer(
      scoped_refptr<FrameTransformerInterface> frame_transformer) override;

  // RtcpPacketTypeCounterObserver.
  void RtcpPacketTypesCounterUpdated(
      uint32_t ssrc,
      const RtcpPacketTypeCounter& packet_counter) override;

  // ReportBlockDataObserver.
  void OnReportBlockDataUpdated(ReportBlockData report_block) override;

  // Reports actual bitrate used (vs allocated).
  std::optional<DataRate> GetUsedRate() const override {
    MutexLock lock(&bitrate_accountant_mutex_);
    return bitrate_accountant_.GetUsedRate();
  }

  void RegisterPacketOverhead(int packet_byte_overhead) override {
    MutexLock lock(&bitrate_accountant_mutex_);
    bitrate_accountant_.RegisterPacketOverhead(packet_byte_overhead);
  }

 private:
  // From AudioPacketizationCallback in the ACM
  int32_t SendData(
      AudioFrameType frameType,
      uint8_t payloadType,
      uint32_t rtp_timestamp,
      const uint8_t* payloadData,
      size_t payloadSize,
      int64_t absolute_capture_timestamp_ms,
      std::optional<uint8_t> audio_level_dbov_override,
      std::optional<std::vector<uint32_t>> csrcs_override) override;

  bool InputMute() const;

  int32_t SendRtpAudio(AudioFrameType frameType,
                       uint8_t payloadType,
                       uint32_t rtp_timestamp_without_offset,
                       std::span<const uint8_t> payload,
                       int64_t absolute_capture_timestamp_ms,
                       std::span<const uint32_t> csrcs,
                       std::optional<uint8_t> audio_level_dbov)
      RTC_RUN_ON(worker_thread_);

  void OnReceivedRtt(int64_t rtt_ms);

  void InitFrameTransformerDelegate(
      scoped_refptr<FrameTransformerInterface> frame_transformer);

  // Calls the encoder on the encoder queue (instead of blocking).
  void CallEncoderAsync(absl::AnyInvocable<void(AudioEncoder*)> modifier);

  const Environment env_;

  // Thread checkers document and lock usage of some methods on voe::Channel to
  // specific threads we know about. The goal is to eventually split up
  // voe::Channel into parts with single-threaded semantics, and thereby reduce
  // the need for locks.
  TaskQueueBase* worker_thread_;
  const ScopedTaskSafety task_safety_;
  // Methods accessed from audio and video threads are checked for sequential-
  // only access. We don't necessarily own and control these threads, so thread
  // checkers cannot be used. E.g. Chromium may transfer "ownership" from one
  // audio thread to another, but access is still sequential.
  RaceChecker audio_thread_race_checker_;

  mutable Mutex volume_settings_mutex_;

  const uint32_t ssrc_;
  bool sending_ RTC_GUARDED_BY(worker_thread_) = false;

  std::unique_ptr<ModuleRtpRtcpImpl2> rtp_rtcp_;
  std::unique_ptr<RTPSenderAudio> rtp_sender_audio_;

  std::unique_ptr<AudioCodingModule> audio_coding_;

  // This is just an offset, RTP module will add its own random offset.
  uint32_t timestamp_ RTC_GUARDED_BY(audio_thread_race_checker_) = 0;
  std::optional<int64_t> last_capture_timestamp_ms_
      RTC_GUARDED_BY(audio_thread_race_checker_);

  RmsLevel rms_level_ RTC_GUARDED_BY(encoder_queue_checker_);
  bool input_mute_ RTC_GUARDED_BY(volume_settings_mutex_) = false;
  bool previous_frame_muted_ RTC_GUARDED_BY(encoder_queue_checker_) = false;

  const std::unique_ptr<RtcpCounterObserver> rtcp_counter_observer_;

  PacketRouter* packet_router_ RTC_GUARDED_BY(worker_thread_) = nullptr;
  const std::unique_ptr<RtpPacketSenderProxy> rtp_packet_pacer_proxy_;

  RTC_NO_UNIQUE_ADDRESS SequenceChecker construction_thread_;

  std::atomic<bool> include_audio_level_indication_ = false;
  std::atomic<bool> encoder_queue_is_active_ = false;
  std::atomic<bool> first_frame_ = true;

  // E2EE Audio Frame Encryption
  scoped_refptr<FrameEncryptorInterface> frame_encryptor_
      RTC_GUARDED_BY(worker_thread_);
  // E2EE Frame Encryption Options
  const CryptoOptions crypto_options_;

  // Sframe end-to-end encryption settings for this stream.
  bool sframe_required_by_negotiation_ RTC_GUARDED_BY(worker_thread_) = false;
  std::optional<SframeEncryptionConfig> sframe_encryption_config_
      RTC_GUARDED_BY(worker_thread_);

  // Delegates calls to a frame transformer to transform audio, and
  // receives callbacks with the transformed frames; delegates calls to
  // ChannelSend::SendRtpAudio to send the transformed audio.
  scoped_refptr<ChannelSendFrameTransformerDelegate> frame_transformer_delegate_
      RTC_GUARDED_BY(encoder_queue_checker_);

  mutable Mutex rtcp_counter_mutex_;
  RtcpPacketTypeCounter rtcp_packet_type_counter_
      RTC_GUARDED_BY(rtcp_counter_mutex_);

  std::unique_ptr<TaskQueueBase, TaskQueueDeleter> encoder_queue_;
  RTC_NO_UNIQUE_ADDRESS SequenceChecker encoder_queue_checker_;

  SdpAudioFormat encoder_format_;

  mutable Mutex bitrate_accountant_mutex_;
  AudioBitrateAccountant bitrate_accountant_
      RTC_GUARDED_BY(bitrate_accountant_mutex_);

  std::vector<uint32_t> csrcs_ RTC_GUARDED_BY(encoder_queue_checker_);
};

const int kTelephoneEventAttenuationdB = 10;

class RtpPacketSenderProxy : public RtpPacketSender {
 public:
  RtpPacketSenderProxy() : rtp_packet_pacer_(nullptr) {}

  void SetPacketPacer(RtpPacketSender* rtp_packet_pacer) {
    RTC_DCHECK(thread_checker_.IsCurrent());
    MutexLock lock(&mutex_);
    rtp_packet_pacer_ = rtp_packet_pacer;
  }

  void EnqueuePackets(
      std::vector<std::unique_ptr<RtpPacketToSend>> packets) override {
    MutexLock lock(&mutex_);

    // Since we allow having an instance with no rtp_packet_pacer_ set we
    // should handle calls to member functions in this state gracefully rather
    // than null dereferencing.
    if (!rtp_packet_pacer_) {
      RTC_DLOG(LS_WARNING)
          << "Dropping packets queued while rtp_packet_pacer_ is null.";
      return;
    }
    rtp_packet_pacer_->EnqueuePackets(std::move(packets));
  }

  void RemovePacketsForSsrc(uint32_t ssrc) override {
    MutexLock lock(&mutex_);

    // Since we allow having an instance with no rtp_packet_pacer_ set we
    // should handle calls to member functions in this state gracefully rather
    // than null dereferencing.
    if (!rtp_packet_pacer_) {
      return;
    }
    rtp_packet_pacer_->RemovePacketsForSsrc(ssrc);
  }

 private:
  RTC_NO_UNIQUE_ADDRESS SequenceChecker thread_checker_;
  Mutex mutex_;
  RtpPacketSender* rtp_packet_pacer_ RTC_GUARDED_BY(&mutex_);
};

int32_t ChannelSend::SendData(
    AudioFrameType frameType,
    uint8_t payloadType,
    uint32_t rtp_timestamp,
    const uint8_t* payloadData,
    size_t payloadSize,
    int64_t absolute_capture_timestamp_ms,
    std::optional<uint8_t> audio_level_dbov_override,
    std::optional<std::vector<uint32_t>> csrcs_override) {
  RTC_DCHECK_RUN_ON(&encoder_queue_checker_);

  std::optional<uint8_t> audio_level_dbov;
  if (include_audio_level_indication_.load()) {
    // Take the averaged audio levels from rms_level_ and reset it before
    // invoking any async transformer.
    audio_level_dbov = rms_level_.Average();
    if (audio_level_dbov_override.has_value()) {
      audio_level_dbov = audio_level_dbov_override;
    }
  }

  if (frame_transformer_delegate_) {
    // Asynchronously transform the payload before sending it. After the payload
    // is transformed, the delegate will call SendRtpAudio to send it.
    StringBuilder mime_type;
    mime_type << MediaTypeToString(MediaType::AUDIO) << "/"
              << encoder_format_.name;
    frame_transformer_delegate_->Transform(
        frameType, payloadType, rtp_timestamp + rtp_rtcp_->StartTimestamp(),
        payloadData, payloadSize, absolute_capture_timestamp_ms,
        rtp_rtcp_->SSRC(), mime_type.str(), audio_level_dbov,
        csrcs_override.has_value() ? *csrcs_override : csrcs_);
    return 0;
  }
  Buffer payload(payloadData, payloadSize);
  std::vector<uint32_t> csrcs = csrcs_override.value_or(csrcs_);
  worker_thread_->PostTask(SafeTask(
      task_safety_.flag(),
      [this, frameType, payloadType, rtp_timestamp,
       payload = std::move(payload), absolute_capture_timestamp_ms,
       csrcs = std::move(csrcs), audio_level_dbov] {
        RTC_DCHECK_RUN_ON(worker_thread_);
        SendRtpAudio(frameType, payloadType, rtp_timestamp, payload,
                     absolute_capture_timestamp_ms, csrcs, audio_level_dbov);
      }));
  return 0;
}

int32_t ChannelSend::SendRtpAudio(AudioFrameType frameType,
                                  uint8_t payloadType,
                                  uint32_t rtp_timestamp_without_offset,
                                  std::span<const uint8_t> payload,
                                  int64_t absolute_capture_timestamp_ms,
                                  std::span<const uint32_t> csrcs,
                                  std::optional<uint8_t> audio_level_dbov) {
  // E2EE Custom Audio Frame Encryption (This is optional).
  // Keep this buffer around for the lifetime of the send call.
  Buffer encrypted_audio_payload;
  // We don't invoke encryptor if payload is empty, which means we are to send
  // DTMF, or the encoder entered DTX.
  // TODO(minyue): see whether DTMF packets should be encrypted or not. In
  // current implementation, they are not.
  if (!payload.empty()) {
    if (sframe_required_by_negotiation_) {
      // TODO(bugs.webrtc.org/479862368): Wire up the Sframe encryptor
      RTC_DLOG(LS_ERROR) << "Dropping audio frame: Sframe is required but no "
                            "Sframe encryptor is set up for this stream.";
      return -1;
    }

    if (frame_encryptor_ != nullptr) {
      // TODO(benwright@webrtc.org) - Allocate enough to always encrypt inline.
      // Allocate a buffer to hold the maximum possible encrypted payload.
      size_t max_ciphertext_size = frame_encryptor_->GetMaxCiphertextByteSize(
          MediaType::AUDIO, payload.size());
      encrypted_audio_payload.SetSize(max_ciphertext_size);

      // Encrypt the audio payload into the buffer.
      size_t bytes_written = 0;
      int encrypt_status =
          frame_encryptor_->Encrypt(MediaType::AUDIO, rtp_rtcp_->SSRC(),
                                    /*additional_data=*/{}, payload,
                                    encrypted_audio_payload, &bytes_written);
      if (encrypt_status != 0) {
        RTC_DLOG(LS_ERROR)
            << "Channel::SendData() failed encrypt audio payload: "
            << encrypt_status;
        return -1;
      }
      // Resize the buffer to the exact number of bytes actually used.
      encrypted_audio_payload.SetSize(bytes_written);
      // Rewrite the payloadData and size to the new encrypted payload.
      payload = encrypted_audio_payload;
    } else if (crypto_options_.sframe.require_frame_encryption) {
      RTC_DLOG(LS_ERROR) << "Channel::SendData() failed sending audio payload: "
                            "A frame encryptor is required but one is not set.";
      return -1;
    }
  }

  // Push data from ACM to RTP/RTCP-module to deliver audio frame for
  // packetization.
  if (!rtp_rtcp_->OnSendingRtpFrame(rtp_timestamp_without_offset,
                                    absolute_capture_timestamp_ms, payloadType,
                                    /*force_sender_report=*/false)) {
    return -1;
  }

  // RTCPSender has it's own copy of the timestamp offset, added in
  // RTCPSender::BuildSR, hence we must not add the in the offset for the above
  // call.
  // TODO(nisse): Delete RTCPSender:timestamp_offset_, and see if we can confine
  // knowledge of the offset to a single place.

  // This call will trigger Transport::SendPacket() from the RTP/RTCP module.
  RTPSenderAudio::RtpAudioFrame frame = {
      .type = frameType,
      .payload = payload,
      .payload_id = payloadType,
      .rtp_timestamp =
          rtp_timestamp_without_offset + rtp_rtcp_->StartTimestamp(),
      .csrcs = csrcs};
  if (absolute_capture_timestamp_ms > 0) {
    frame.capture_time = Timestamp::Millis(absolute_capture_timestamp_ms);
  }
  if (include_audio_level_indication_.load() && audio_level_dbov) {
    frame.audio_level_dbov = *audio_level_dbov;
  }
  if (!rtp_sender_audio_->SendAudio(frame)) {
    RTC_DLOG(LS_ERROR)
        << "ChannelSend::SendData() failed to send data to RTP/RTCP module";
    return -1;
  }

  return 0;
}

ChannelSend::ChannelSend(
    const Environment& env,
    Transport* rtp_transport,
    RtcpRttStats* rtcp_rtt_stats,
    FrameEncryptorInterface* frame_encryptor,
    const CryptoOptions& crypto_options,
    bool extmap_allow_mixed,
    int rtcp_report_interval_ms,
    uint32_t ssrc,
    const std::optional<RtpState>& rtp_state,
    absl::string_view mid,
    scoped_refptr<FrameTransformerInterface> frame_transformer,
    RtpTransportControllerSendInterface* transport_controller)
    : env_(env),
      worker_thread_(TaskQueueBase::Current()),
      ssrc_(ssrc),
      rtcp_counter_observer_(new RtcpCounterObserver(ssrc)),
      rtp_packet_pacer_proxy_(new RtpPacketSenderProxy()),
      frame_encryptor_(frame_encryptor),
      crypto_options_(crypto_options),
      encoder_queue_(env_.task_queue_factory().CreateTaskQueue(
          "AudioEncoderQueue",
          !env_.field_trials().IsDisabled("WebRTC-MediaTaskQueuePriorities")
              ? TaskQueueFactory::Priority::kAudio
              : TaskQueueFactory::Priority::kNormal)),
      encoder_queue_checker_(encoder_queue_.get()),
      encoder_format_("x-unknown", 0, 0) {
  audio_coding_ = AudioCodingModule::Create();

  RtpRtcpInterface::Configuration configuration;
  configuration.report_block_data_observer = this;
  configuration.network_link_rtcp_observer =
      transport_controller->GetRtcpObserver();
  configuration.audio = true;
  configuration.outgoing_transport = rtp_transport;

  configuration.paced_sender = rtp_packet_pacer_proxy_.get();
  configuration.rtt_stats = rtcp_rtt_stats;
  configuration.rtcp_packet_type_counter_observer =
      rtcp_counter_observer_.get();
  configuration.extmap_allow_mixed = extmap_allow_mixed;
  configuration.rtcp_report_interval_ms = rtcp_report_interval_ms;
  configuration.rtcp_packet_type_counter_observer = this;
  configuration.local_media_ssrc = ssrc;
  configuration.rtp_state = rtp_state;
  configuration.mid = std::string(mid);
  configuration.rtcp_mode = RtcpMode::kCompound;

  rtp_rtcp_ = ModuleRtpRtcpImpl2::CreateSendModule(env_, configuration);
  rtp_rtcp_->SetSendingMediaStatus(false);

  rtp_sender_audio_ =
      std::make_unique<RTPSenderAudio>(&env_.clock(), rtp_rtcp_->RtpSender());

  int error = audio_coding_->RegisterTransportCallback(this);
  RTC_DCHECK_EQ(0, error);
}

ChannelSend::~ChannelSend() {
  RTC_DCHECK(construction_thread_.IsCurrent());

  // Reset and clear the frame_transformer_delegate_ on the encoder queue
  // to avoid race conditions.
  Event delegate_reset_event;
  encoder_queue_->PostTask([this, &delegate_reset_event] {
    RTC_DCHECK_RUN_ON(&encoder_queue_checker_);
    if (frame_transformer_delegate_) {
      frame_transformer_delegate_->Reset();
      frame_transformer_delegate_ = nullptr;
    }
    delegate_reset_event.Set();
  });
  delegate_reset_event.Wait(Event::kForever);

  StopSend();
  int error = audio_coding_->RegisterTransportCallback(nullptr);
  RTC_DCHECK_EQ(0, error);

  // Delete the encoder task queue first to ensure that there are no running
  // tasks when the other members are destroyed.
  encoder_queue_ = nullptr;
}

void ChannelSend::StartSend() {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DCHECK(!sending_);
  sending_ = true;

  RTC_DCHECK(packet_router_);
  packet_router_->AddSendRtpModule(rtp_rtcp_.get(), /*remb_candidate=*/false);
  rtp_rtcp_->SetSendingMediaStatus(true);
  int ret = rtp_rtcp_->SetSendingStatus(true);
  RTC_DCHECK_EQ(0, ret);

  // It is now OK to start processing on the encoder task queue.
  first_frame_.store(true);
  encoder_queue_is_active_.store(true);
}

void ChannelSend::StopSend() {
  RTC_DCHECK_RUN_ON(worker_thread_);
  if (!sending_) {
    return;
  }
  sending_ = false;
  encoder_queue_is_active_.store(false);

  // Wait until all pending encode tasks are executed and clear any remaining
  // buffers in the encoder.
  Event flush;
  encoder_queue_->PostTask([this, &flush]() {
    RTC_DCHECK_RUN_ON(&encoder_queue_checker_);
    audio_coding_->Reset();
    flush.Set();
  });
  flush.Wait(Event::kForever);

  // Reset sending SSRC and sequence number and triggers direct transmission
  // of RTCP BYE
  if (rtp_rtcp_->SetSendingStatus(false) == -1) {
    RTC_DLOG(LS_ERROR) << "StartSend() RTP/RTCP failed to stop sending";
  }
  rtp_rtcp_->SetSendingMediaStatus(false);

  RTC_DCHECK(packet_router_);
  packet_router_->RemoveSendRtpModule(rtp_rtcp_.get());
  rtp_packet_pacer_proxy_->RemovePacketsForSsrc(rtp_rtcp_->SSRC());
}

void ChannelSend::SetEncoder(int payload_type,
                             const SdpAudioFormat& encoder_format,
                             std::unique_ptr<AudioEncoder> encoder) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DCHECK_GE(payload_type, 0);
  RTC_DCHECK_LE(payload_type, 127);

  // The RTP/RTCP module needs to know the RTP timestamp rate (i.e. clockrate)
  // as well as some other things, so we collect this info and send it along.
  rtp_rtcp_->RegisterSendPayloadFrequency(payload_type,
                                          encoder->RtpTimestampRateHz());
  rtp_sender_audio_->RegisterAudioPayload("audio", payload_type,
                                          encoder->RtpTimestampRateHz(),
                                          encoder->NumChannels(), 0);

  encoder_format_ = encoder_format;
  audio_coding_->SetEncoder(std::move(encoder));
}

void ChannelSend::ModifyEncoder(
    FunctionView<void(std::unique_ptr<AudioEncoder>*)> modifier) {
  // This method can be called on the worker thread, module process thread
  // or network thread. Audio coding is thread safe, so we do not need to
  // enforce the calling thread.
  audio_coding_->ModifyEncoder(modifier);
}

void ChannelSend::CallEncoder(FunctionView<void(AudioEncoder*)> modifier) {
  ModifyEncoder([modifier](std::unique_ptr<AudioEncoder>* encoder_ptr) {
    if (*encoder_ptr) {
      modifier(encoder_ptr->get());
    } else {
      RTC_DLOG(LS_WARNING) << "Trying to call unset encoder.";
    }
  });
}

void ChannelSend::CallEncoderAsync(
    absl::AnyInvocable<void(AudioEncoder*)> modifier) {
  encoder_queue_->PostTask([this, modifier = std::move(modifier)]() mutable {
    CallEncoder(modifier);
  });
}

void ChannelSend::OnBitrateAllocation(BitrateAllocationUpdate update) {
  CallEncoderAsync([update](AudioEncoder* encoder) {
    encoder->OnReceivedUplinkAllocation(update);
  });
}

int ChannelSend::GetTargetBitrate() const {
  return audio_coding_->GetTargetBitrate();
}

void ChannelSend::OnReportBlockDataUpdated(ReportBlockData report_block) {
  float packet_loss_rate = report_block.fraction_lost();
  CallEncoderAsync([packet_loss_rate](AudioEncoder* encoder) {
    encoder->OnReceivedUplinkPacketLossFraction(packet_loss_rate);
  });
}

void ChannelSend::ReceivedRTCPPacket(const uint8_t* data, size_t length) {
  RTC_DCHECK_RUN_ON(worker_thread_);

  // Deliver RTCP packet to RTP/RTCP module for parsing
  rtp_rtcp_->IncomingRtcpPacket(std::span(data, length));

  std::optional<TimeDelta> rtt = rtp_rtcp_->LastRtt();
  if (!rtt.has_value()) {
    // Waiting for valid RTT.
    return;
  }

  OnReceivedRtt(rtt->ms());
}

void ChannelSend::SetInputMute(bool enable) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  MutexLock lock(&volume_settings_mutex_);
  input_mute_ = enable;
}

bool ChannelSend::InputMute() const {
  MutexLock lock(&volume_settings_mutex_);
  return input_mute_;
}

void ChannelSend::SetCsrcs(std::span<const uint32_t> csrcs) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  std::vector<uint32_t> csrcs_copy(
      csrcs.begin(),
      csrcs.begin() + std::min<size_t>(csrcs.size(), kRtpCsrcSize));
  encoder_queue_->PostTask([this, csrcs = std::move(csrcs_copy)]() mutable {
    RTC_DCHECK_RUN_ON(&encoder_queue_checker_);
    csrcs_ = csrcs;
  });
}

bool ChannelSend::SendTelephoneEventOutband(int event, int duration_ms) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DCHECK_LE(0, event);
  RTC_DCHECK_GE(255, event);
  RTC_DCHECK_LE(0, duration_ms);
  RTC_DCHECK_GE(65535, duration_ms);
  if (!sending_) {
    return false;
  }
  if (rtp_sender_audio_->SendTelephoneEvent(
          event, duration_ms, kTelephoneEventAttenuationdB) != 0) {
    RTC_DLOG(LS_ERROR) << "SendTelephoneEvent() failed to send event";
    return false;
  }
  return true;
}

void ChannelSend::RegisterCngPayloadType(int payload_type,
                                         int payload_frequency) {
  rtp_rtcp_->RegisterSendPayloadFrequency(payload_type, payload_frequency);
  rtp_sender_audio_->RegisterAudioPayload("CN", payload_type, payload_frequency,
                                          1, 0);
}

void ChannelSend::SetSendTelephoneEventPayloadType(int payload_type,
                                                   int payload_frequency) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DCHECK_LE(0, payload_type);
  RTC_DCHECK_GE(127, payload_type);
  rtp_rtcp_->RegisterSendPayloadFrequency(payload_type, payload_frequency);
  rtp_sender_audio_->RegisterAudioPayload("telephone-event", payload_type,
                                          payload_frequency, 0, 0);
}

void ChannelSend::SetSendAudioLevelIndicationStatus(RtpHeaderExtensionId id) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  include_audio_level_indication_.store(id.IsSet());
  if (id.IsSet()) {
    rtp_rtcp_->RegisterRtpHeaderExtension(AudioLevelExtension::Uri(), id);
  } else {
    rtp_rtcp_->DeregisterSendRtpHeaderExtension(AudioLevelExtension::Uri());
  }
}

void ChannelSend::RegisterSenderCongestionControlObjects(
    RtpTransportControllerSendInterface* transport) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RtpPacketSender* rtp_packet_pacer = transport->packet_sender();
  PacketRouter* packet_router = transport->packet_router();

  RTC_DCHECK(rtp_packet_pacer);
  RTC_DCHECK(packet_router);
  RTC_DCHECK(!packet_router_);
  rtp_packet_pacer_proxy_->SetPacketPacer(rtp_packet_pacer);
  rtp_rtcp_->SetStorePacketsStatus(true, 600);
  packet_router_ = packet_router;
}

void ChannelSend::ResetSenderCongestionControlObjects() {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DCHECK(packet_router_);
  rtp_rtcp_->SetStorePacketsStatus(false, 600);
  packet_router_ = nullptr;
  rtp_packet_pacer_proxy_->SetPacketPacer(nullptr);
}

void ChannelSend::SetRTCP_CNAME(absl::string_view c_name) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  // Note: SetCNAME() accepts a c string of length at most 255.
  const std::string c_name_limited(c_name.substr(0, 255));
  int ret = rtp_rtcp_->SetCNAME(c_name_limited.c_str()) != 0;
  RTC_DCHECK_EQ(0, ret) << "SetRTCP_CNAME() failed to set RTCP CNAME";
}

std::vector<ReportBlockData> ChannelSend::GetRemoteRTCPReportBlocks() const {
  RTC_DCHECK_RUN_ON(worker_thread_);
  // Get the report blocks from the latest received RTCP Sender or Receiver
  // Report. Each element in the vector contains the sender's SSRC and a
  // report block according to RFC 3550.
  return rtp_rtcp_->GetLatestReportBlockData();
}

ChannelSendStatistics ChannelSend::GetRTCPStatistics() const {
  RTC_DCHECK_RUN_ON(worker_thread_);
  ChannelSendStatistics stats = {
      .round_trip_time = rtp_rtcp_->LastRtt().value_or(TimeDelta::Zero())};
  stats.rtcp_packet_type_counts = rtcp_counter_observer_->GetCounts();

  StreamDataCounters rtp_stats;
  StreamDataCounters rtx_stats;
  rtp_rtcp_->GetSendStreamDataCounters(&rtp_stats, &rtx_stats);
  stats.payload_bytes_sent =
      rtp_stats.transmitted.payload_bytes + rtx_stats.transmitted.payload_bytes;
  stats.header_and_padding_bytes_sent =
      rtp_stats.transmitted.padding_bytes + rtp_stats.transmitted.header_bytes +
      rtx_stats.transmitted.padding_bytes + rtx_stats.transmitted.header_bytes;

  // TODO(https://crbug.com/webrtc/10555): RTX retransmissions should show up in
  // separate outbound-rtp stream objects.
  stats.retransmitted_bytes_sent = rtp_stats.retransmitted.payload_bytes;
  stats.packets_sent =
      rtp_stats.transmitted.packets + rtx_stats.transmitted.packets;
  stats.packets_sent_with_ect1 = rtp_stats.transmitted.packets_with_ect1 +
                                 rtx_stats.transmitted.packets_with_ect1;
  stats.total_packet_send_delay = rtp_stats.transmitted.total_packet_delay;
  stats.retransmitted_packets_sent = rtp_stats.retransmitted.packets;
  stats.report_block_datas = rtp_rtcp_->GetLatestReportBlockData();

  {
    MutexLock lock(&rtcp_counter_mutex_);
    stats.nacks_received = rtcp_packet_type_counter_.nack_packets;
  }

  return stats;
}

void ChannelSend::RtcpPacketTypesCounterUpdated(
    uint32_t ssrc,
    const RtcpPacketTypeCounter& packet_counter) {
  if (ssrc != ssrc_) {
    return;
  }
  MutexLock lock(&rtcp_counter_mutex_);
  rtcp_packet_type_counter_ = packet_counter;
}

void ChannelSend::ProcessAndEncodeAudio(
    std::unique_ptr<AudioFrame> audio_frame) {
  TRACE_EVENT0("webrtc", "ChannelSend::ProcessAndEncodeAudio");

  RTC_DCHECK_RUNS_SERIALIZED(&audio_thread_race_checker_);
  RTC_DCHECK_GT(audio_frame->samples_per_channel_, 0);
  RTC_DCHECK_LE(audio_frame->num_channels_, 8);

  if (!encoder_queue_is_active_.load()) {
    return;
  }

  // Update `timestamp_` based on the capture timestamp for the first frame
  // after sending is resumed.
  if (first_frame_.load()) {
    first_frame_.store(false);
    if (last_capture_timestamp_ms_ &&
        audio_frame->absolute_capture_timestamp_ms()) {
      int64_t diff_ms = *audio_frame->absolute_capture_timestamp_ms() -
                        *last_capture_timestamp_ms_;
      // Truncate to whole frames and subtract one since `timestamp_` was
      // incremented after the last frame.
      int64_t diff_frames = diff_ms * audio_frame->sample_rate_hz() / 1000 /
                                audio_frame->samples_per_channel() -
                            1;
      timestamp_ += std::max<int64_t>(
          diff_frames * audio_frame->samples_per_channel(), 0);
    }
  }

  audio_frame->timestamp_ = timestamp_;
  timestamp_ += audio_frame->samples_per_channel_;
  last_capture_timestamp_ms_ = audio_frame->absolute_capture_timestamp_ms();

  // Profile time between when the audio frame is added to the task queue and
  // when the task is actually executed.
  Timestamp post_task_time = env_.clock().CurrentTime();
  encoder_queue_->PostTask(
      [this, post_task_time, audio_frame = std::move(audio_frame)]() mutable {
        RTC_DCHECK_RUN_ON(&encoder_queue_checker_);
        if (!encoder_queue_is_active_.load()) {
          return;
        }
        // Measure time between when the audio frame is added to the task queue
        // and when the task is actually executed. Goal is to keep track of
        // unwanted extra latency added by the task queue.
        TimeDelta latency = post_task_time - env_.clock().CurrentTime();
        RTC_HISTOGRAM_COUNTS_10000("WebRTC.Audio.EncodingTaskQueueLatencyMs",
                                   latency.ms());

        bool is_muted = InputMute();
        AudioFrameOperations::Mute(audio_frame.get(), previous_frame_muted_,
                                   is_muted);

        if (include_audio_level_indication_.load()) {
          size_t length =
              audio_frame->samples_per_channel_ * audio_frame->num_channels_;
          RTC_CHECK_LE(length, AudioFrame::kMaxDataSizeBytes);
          if (is_muted && previous_frame_muted_) {
            rms_level_.AnalyzeMuted(length);
          } else {
            rms_level_.Analyze(
                std::span<const int16_t>(audio_frame->data(), length));
          }
        }
        previous_frame_muted_ = is_muted;

        // This call will trigger AudioPacketizationCallback::SendData if
        // encoding is done and payload is ready for packetization and
        // transmission. Otherwise, it will return without invoking the
        // callback.
        int32_t encoded_bytes = audio_coding_->Add10MsData(*audio_frame);
        MutexLock lock(&bitrate_accountant_mutex_);
        if (encoded_bytes < 0) {
          RTC_DLOG(LS_ERROR) << "ACM::Add10MsData() failed.";
          bitrate_accountant_.Reset();
          return;
        }
        bitrate_accountant_.UpdateBpsEstimate(DataSize::Bytes(encoded_bytes),
                                              TimeDelta::Millis(10));
      });
}

ANAStats ChannelSend::GetANAStatistics() const {
  RTC_DCHECK_RUN_ON(worker_thread_);
  return audio_coding_->GetANAStats();
}

RtpRtcpInterface* ChannelSend::GetRtpRtcp() const {
  return rtp_rtcp_.get();
}

void ChannelSend::SetFrameEncryptor(
    scoped_refptr<FrameEncryptorInterface> frame_encryptor) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  frame_encryptor_ = std::move(frame_encryptor);
}

void ChannelSend::SetSframeOptions(const SframeSendOptions& options) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  sframe_required_by_negotiation_ = options.required;
  sframe_encryption_config_ = options.encryption_config;
}

void ChannelSend::SetEncoderToPacketizerFrameTransformer(
    scoped_refptr<FrameTransformerInterface> frame_transformer) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  if (!frame_transformer)
    return;

  encoder_queue_->PostTask(
      [this, frame_transformer = std::move(frame_transformer)]() mutable {
        RTC_DCHECK_RUN_ON(&encoder_queue_checker_);
        InitFrameTransformerDelegate(std::move(frame_transformer));
      });
}

void ChannelSend::OnReceivedRtt(int64_t rtt_ms) {
  CallEncoderAsync(
      [rtt_ms](AudioEncoder* encoder) { encoder->OnReceivedRtt(rtt_ms); });
}

void ChannelSend::InitFrameTransformerDelegate(
    scoped_refptr<FrameTransformerInterface> frame_transformer) {
  RTC_DCHECK_RUN_ON(&encoder_queue_checker_);
  RTC_DCHECK(frame_transformer);
  if (frame_transformer_delegate_) {
    frame_transformer_delegate_->Reset();
    frame_transformer_delegate_ = nullptr;
  }

  // Pass a callback to ChannelSend::SendRtpAudio, to be called by the delegate
  // to send the transformed audio.
  ChannelSendFrameTransformerDelegate::SendFrameCallback send_audio_callback =
      [this](AudioFrameType frameType, uint8_t payloadType,
             RtpTimestampInfo rtp_timestamp_info,
             std::span<const uint8_t> payload,
             int64_t absolute_capture_timestamp_ms,
             std::span<const uint32_t> csrcs,
             std::optional<uint8_t> audio_level_dbov) {
        RTC_DCHECK_RUN_ON(worker_thread_);
        uint32_t timestamp_without_offset;
        if (std::holds_alternative<RtpTimestampWithoutOffset>(
                rtp_timestamp_info)) {
          timestamp_without_offset =
              std::get<RtpTimestampWithoutOffset>(rtp_timestamp_info);
        } else {
          timestamp_without_offset =
              std::get<RtpTimestampWithOffset>(rtp_timestamp_info) -
              rtp_rtcp_->StartTimestamp();
        }
        return SendRtpAudio(frameType, payloadType, timestamp_without_offset,
                            payload, absolute_capture_timestamp_ms, csrcs,
                            audio_level_dbov);
      };
  frame_transformer_delegate_ =
      make_ref_counted<ChannelSendFrameTransformerDelegate>(
          std::move(send_audio_callback), std::move(frame_transformer),
          worker_thread_);
  frame_transformer_delegate_->Init();
}

}  // namespace

std::unique_ptr<ChannelSendInterface> CreateChannelSend(
    const Environment& env,
    Transport* rtp_transport,
    RtcpRttStats* rtcp_rtt_stats,
    FrameEncryptorInterface* frame_encryptor,
    const CryptoOptions& crypto_options,
    bool extmap_allow_mixed,
    int rtcp_report_interval_ms,
    uint32_t ssrc,
    const std::optional<RtpState>& rtp_state,
    absl::string_view mid,
    scoped_refptr<FrameTransformerInterface> frame_transformer,
    RtpTransportControllerSendInterface* transport_controller) {
  return std::make_unique<ChannelSend>(
      env, rtp_transport, rtcp_rtt_stats, frame_encryptor, crypto_options,
      extmap_allow_mixed, rtcp_report_interval_ms, ssrc, rtp_state, mid,
      std::move(frame_transformer), transport_controller);
}

}  // namespace voe
}  // namespace webrtc
