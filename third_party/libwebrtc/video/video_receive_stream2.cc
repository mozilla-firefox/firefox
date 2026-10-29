/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/video_receive_stream2.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/str_cat.h"
#include "api/crypto/frame_decryptor_interface.h"
#include "api/environment/environment.h"
#include "api/field_trials_view.h"
#include "api/frame_transformer_interface.h"
#include "api/rtp_headers.h"
#include "api/rtp_packet_infos.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/units/frequency.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/color_space.h"
#include "api/video/corruption_detection/frame_instrumentation_data.h"
#include "api/video/corruption_detection/frame_instrumentation_evaluation.h"
#include "api/video/encoded_frame.h"
#include "api/video/encoded_image.h"
#include "api/video/recordable_encoded_frame.h"
#include "api/video/render_resolution.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_content_type.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_rotation.h"
#include "api/video/video_sink_interface.h"
#include "api/video/video_timing.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "call/call.h"
#include "call/rtp_packet_sink_interface.h"
#include "call/rtp_stream_receiver_controller_interface.h"
#include "call/rtp_transport_controller_send_interface.h"
#include "call/rtx_receive_stream.h"
#include "call/syncable.h"
#include "call/video_receive_stream.h"
#include "modules/rtp_rtcp/include/receive_statistics.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_interface.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_coding_defines.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "modules/video_coding/nack_requester.h"
#include "modules/video_coding/timing/timing.h"
#include "modules/video_coding/utility/vp8_header_parser.h"
#include "rtc_base/checks.h"
#include "rtc_base/event.h"
#include "rtc_base/experiments/corruption_detection_frame_selector_settings.h"
#include "rtc_base/experiments/field_trial_parser.h"
#include "rtc_base/logging.h"
#include "rtc_base/race_checker.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/trace_event.h"
#include "system_wrappers/include/clock.h"
#include "system_wrappers/include/ntp_time.h"
#include "video/call_stats2.h"
#include "video/decode_synchronizer.h"
#include "video/frame_decode_scheduler.h"
#include "video/frame_dumping_decoder.h"
#include "video/null_video_decoder.h"
#include "video/receive_statistics_proxy.h"
#include "video/render/incoming_video_stream.h"
#include "video/rtp_video_stream_receiver2.h"
#include "video/task_queue_frame_decode_scheduler.h"
#include "video/video_stream_buffer_controller.h"
#include "video/video_stream_decoder2.h"

namespace webrtc {

namespace internal {

namespace {

// The default delay before re-requesting a key frame to be sent.
constexpr TimeDelta kMinBaseMinimumDelay = TimeDelta::Zero();
constexpr TimeDelta kMaxBaseMinimumDelay = TimeDelta::Seconds(10);

// Concrete instance of RecordableEncodedFrame wrapping needed content
// from EncodedFrame.
class WebRtcRecordableEncodedFrame : public RecordableEncodedFrame {
 public:
  explicit WebRtcRecordableEncodedFrame(
      const EncodedFrame& frame,
      RecordableEncodedFrame::EncodedResolution resolution)
      : buffer_(frame.GetEncodedData()),
        render_time_ms_(frame.RenderTime()),
        codec_(frame.CodecSpecific()->codecType),
        is_key_frame_(frame.IsKey()),
        resolution_(resolution) {
    if (frame.ColorSpace()) {
      color_space_ = *frame.ColorSpace();
    }
    if (frame.rotation() != VideoRotation::kVideoRotation_0) {
      video_rotation_ = frame.rotation();
    }
  }

  // VideoEncodedSinkInterface::FrameBuffer
  scoped_refptr<const EncodedImageBufferInterface> encoded_buffer()
      const override {
    return buffer_;
  }

  std::optional<ColorSpace> color_space() const override {
    return color_space_;
  }

  std::optional<VideoRotation> video_rotation() const override {
    return video_rotation_;
  }

  VideoCodecType codec() const override { return codec_; }

  bool is_key_frame() const override { return is_key_frame_; }

  EncodedResolution resolution() const override { return resolution_; }

  Timestamp render_time() const override {
    return Timestamp::Millis(render_time_ms_);
  }

 private:
  scoped_refptr<EncodedImageBufferInterface> buffer_;
  int64_t render_time_ms_;
  VideoCodecType codec_;
  bool is_key_frame_;
  EncodedResolution resolution_;
  std::optional<ColorSpace> color_space_;
  std::optional<VideoRotation> video_rotation_;
};

RenderResolution InitialDecoderResolution(const FieldTrialsView& field_trials) {
  FieldTrialOptional<int> width("w");
  FieldTrialOptional<int> height("h");
  ParseFieldTrial({&width, &height},
                  field_trials.Lookup("WebRTC-Video-InitialDecoderResolution"));
  if (width && height) {
    return RenderResolution(width.Value(), height.Value());
  }

  return RenderResolution(320, 180);
}

bool IsKeyFrameAndUnspecifiedResolution(const EncodedFrame& frame) {
  return frame.IsKey() && frame.EncodedImage()._encodedWidth == 0 &&
         frame.EncodedImage()._encodedHeight == 0;
}

std::string OptionalDelayToLogString(std::optional<TimeDelta> opt) {
  return opt.has_value() ? absl::StrCat(*opt) : "<unset>";
}

std::unique_ptr<VideoStreamBufferController> CreateBuffer(
    const Environment& env,
    Call* call,
    VCMTiming* timing,
    ReceiveStatisticsProxy* stats_proxy,
    FrameSchedulingReceiver* receiver,
    TimeDelta max_wait_for_keyframe,
    TimeDelta max_wait_for_frame,
    DecodeSynchronizer* decode_sync) {
  std::unique_ptr<FrameDecodeScheduler> scheduler =
      decode_sync ? decode_sync->CreateSynchronizedFrameScheduler()
                  : std::make_unique<TaskQueueFrameDecodeScheduler>(
                        &env.clock(), call->worker_thread());
  return std::make_unique<VideoStreamBufferController>(
      &env.clock(), call->worker_thread(), timing, stats_proxy, receiver,
      max_wait_for_keyframe, max_wait_for_frame, std::move(scheduler),
      env.field_trials());
}

void VerifyNoDuplicatePayloadTypes(
    const std::vector<VideoReceiveStreamInterface::Decoder>& decoders) {
  std::set<int> decoder_payload_types;
  for (const auto& decoder : decoders) {
    RTC_CHECK(decoder_payload_types.insert(decoder.payload_type).second)
        << "Duplicate payload type (" << decoder.payload_type
        << ") for different decoders.";
  }
}

}  // namespace

TimeDelta DetermineMaxWaitForFrame(TimeDelta rtp_history, bool is_keyframe) {
  // A (arbitrary) conversion factor between the remotely signalled NACK buffer
  // time (if not present defaults to 1000ms) and the maximum time we wait for a
  // remote frame. Chosen to not change existing defaults when using not
  // rtx-time.
  const int conversion_factor = 3;
  if (rtp_history > TimeDelta::Zero() &&
      conversion_factor * rtp_history < kMaxWaitForFrame) {
    return is_keyframe ? rtp_history : conversion_factor * rtp_history;
  }
  return is_keyframe ? kMaxWaitForKeyFrame : kMaxWaitForFrame;
}

VideoReceiveStream2::VideoReceiveStream2(
    const Environment& env,
    Call* call,
    int num_cpu_cores,
    PacketRouter* packet_router,
    VideoReceiveStreamInterface::Config config,
    CallStats* call_stats,
    std::unique_ptr<VCMTiming> timing,
    NackPeriodicProcessor* nack_periodic_processor,
    DecodeSynchronizer* decode_sync)
    : env_(env),
      decode_sequence_checker_(SequenceChecker::kDetached),
      transport_adapter_(config.rtcp_send_transport),
      config_(std::move(config)),
      remote_ssrc_(config_.rtp.remote_ssrc),
      renderer_(config_.renderer),
      on_frame_delivered_callback_(
          std::move(config_.on_frame_delivered_callback)),
      decoder_factory_(config_.decoder_factory),
      require_frame_encryption_(
          config_.crypto_options.sframe.require_frame_encryption),
      num_cpu_cores_(num_cpu_cores),
      call_(call),
      call_stats_(call_stats),
      stats_proxy_(remote_ssrc(), &env_.clock(), call->worker_thread()),
      rtp_receive_statistics_(ReceiveStatistics::Create(&env_.clock())),
      timing_(std::move(timing)),
      video_receiver_(&env_.clock(), timing_.get(), env_.field_trials(), this),
      max_wait_for_keyframe_(DetermineMaxWaitForFrame(
          TimeDelta::Millis(config_.rtp.nack.rtp_history_ms),
          true)),
      max_wait_for_frame_(DetermineMaxWaitForFrame(
          TimeDelta::Millis(config_.rtp.nack.rtp_history_ms),
          false)),
      rtp_video_stream_receiver_(env_,
                                 call->worker_thread(),
                                 &transport_adapter_,
                                 call_stats->AsRtcpRttStats(),
                                 packet_router,
                                 &config_,
                                 max_wait_for_keyframe_,
                                 rtp_receive_statistics_.get(),
                                 &stats_proxy_,
                                 &stats_proxy_,
                                 nack_periodic_processor,
                                 &stats_proxy_,
                                 this,  // OnCompleteFrameCallback
                                 std::move(config_.frame_decryptor),
                                 std::move(config_.frame_transformer),
                                 std::move(config_.on_first_packet)),
      rtp_stream_sync_(env_, call->worker_thread(), this),
      decode_sync_(decode_sync),
      buffer_(CreateBuffer(env_,
                           call_,
                           timing_.get(),
                           &stats_proxy_,
                           this,
                           max_wait_for_keyframe_,
                           max_wait_for_frame_,
                           decode_sync_)),
      frame_evaluator_(FrameInstrumentationEvaluation::Create(&stats_proxy_)),
      post_decode_queue_(
          CorruptionDetectionFrameSelectorSettings(env.field_trials())
                  .use_asynchronous_evaluation()
              ? env_.task_queue_factory().CreateTaskQueue(
                    "VideoPostDecodeQueue",
                    TaskQueueFactory::Priority::kNormal)
              : nullptr),
      decode_queue_(env_.task_queue_factory().CreateTaskQueue(
          "VideoDecoderQueue",
          !env_.field_trials().IsDisabled("WebRTC-MediaTaskQueuePriorities")
              ? TaskQueueFactory::Priority::kVideo
              : TaskQueueFactory::Priority::kHigh)) {
  RTC_LOG(LS_INFO) << "VideoReceiveStream2: " << config_.ToString();

  RTC_DCHECK(call_->worker_thread());
  RTC_DCHECK(renderer_);
  RTC_DCHECK(call_stats_);

  RTC_DCHECK(!config_.decoders.empty());
  RTC_CHECK(decoder_factory_);
  VerifyNoDuplicatePayloadTypes(config_.decoders);

  if (!config_.rtp.rtx_associated_payload_types.empty()) {
    rtx_receive_stream_ = std::make_unique<RtxReceiveStream>(
        env_, &rtp_video_stream_receiver_,
        std::move(config_.rtp.rtx_associated_payload_types), remote_ssrc(),
        rtp_receive_statistics_.get());
  } else {
    rtp_receive_statistics_->EnableRetransmitDetection(remote_ssrc(), true);
  }
}

VideoReceiveStream2::~VideoReceiveStream2() {
  RTC_LOG(LS_INFO) << "~VideoReceiveStream2: " << config_.ToString();
  RTC_DCHECK(!media_receiver_);
  RTC_DCHECK(!rtx_receiver_);
  Stop();
}

void VideoReceiveStream2::RegisterWithTransport(
    RtpStreamReceiverControllerInterface* receiver_controller) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  RTC_DCHECK(!media_receiver_);
  RTC_DCHECK(!rtx_receiver_);
  receiver_controller_ = receiver_controller;

  // Register with RtpStreamReceiverController.
  media_receiver_ = receiver_controller->CreateReceiver(
      remote_ssrc(), &rtp_video_stream_receiver_);
  if (rtx_ssrc() && rtx_receive_stream_ != nullptr) {
    rtx_receiver_ = receiver_controller->CreateReceiver(
        rtx_ssrc(), rtx_receive_stream_.get());
  }
}

void VideoReceiveStream2::UnregisterFromTransport() {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  media_receiver_.reset();
  rtx_receiver_.reset();
  receiver_controller_ = nullptr;
}

const std::string& VideoReceiveStream2::sync_group() const {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  return config_.sync_group;
}

void VideoReceiveStream2::SignalNetworkState(NetworkState state) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  rtp_video_stream_receiver_.SignalNetworkState(state);
}

bool VideoReceiveStream2::DeliverRtcp(std::span<const uint8_t> packet) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  return rtp_video_stream_receiver_.DeliverRtcp(packet);
}

void VideoReceiveStream2::SetSync(Syncable* audio_syncable) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  rtp_stream_sync_.ConfigureSync(audio_syncable);
}

void VideoReceiveStream2::Start() {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);

  if (decoder_running_) {
    return;
  }

  if (buffer_->stopped()) {
    // Reset old buffer before creating a new one to unregister its scheduler
    // from DecodeSynchronizer before registering the new scheduler.
    // Dynamic internal state (e.g. RTT) will reset to defaults until the next
    // update.
    buffer_.reset();
    buffer_ =
        CreateBuffer(env_, call_, timing_.get(), &stats_proxy_, this,
                     max_wait_for_keyframe_, max_wait_for_frame_, decode_sync_);
  }

  const bool protected_by_fec =
      config_.rtp.protected_by_flexfec ||
      rtp_video_stream_receiver_.ulpfec_payload_type() != -1;

  if (config_.rtp.nack.rtp_history_ms > 0 && protected_by_fec) {
    buffer_->SetProtectionMode(kProtectionNackFEC);
  }

  transport_adapter_.Enable();
  VideoSinkInterface<VideoFrame>* renderer = nullptr;
  if (config_.enable_prerenderer_smoothing) {
    incoming_video_stream_ = std::make_unique<IncomingVideoStream>(
        env_, config_.render_delay_ms, this);
    renderer = incoming_video_stream_.get();
  } else {
    renderer = this;
  }

  ConfigureCodecs();

  RTC_DCHECK(renderer != nullptr);
  video_stream_decoder_.reset(
      new VideoStreamDecoder(&video_receiver_, &stats_proxy_, renderer));

  // Make sure we register as a stats observer *after* we've prepared the
  // `video_stream_decoder_`.
  call_stats_->RegisterStatsObserver(this);

  // Start decoding on task queue.
  stats_proxy_.DecoderThreadStarting();
  video_receiver_.SetDecodeQueue(decode_queue_.get());
  decode_queue_->PostTask([this, decoders = config_.decoders]() mutable {
    RTC_DCHECK_RUN_ON(&decode_sequence_checker_);
    decoder_stopped_ = false;
    active_decoders_ = std::move(decoders);
  });
  buffer_->StartNextDecode(true);
  decoder_running_ = true;

  rtp_video_stream_receiver_.StartReceive();
}

void VideoReceiveStream2::Stop() {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);

  rtp_video_stream_receiver_.StopReceive();

  stats_proxy_.OnUniqueFramesCounted(
      rtp_video_stream_receiver_.GetUniqueFramesSeen());

  // Stop frame decode scheduling. The buffer_ is kept alive while in a stopped
  // state to safely absorb late incoming frames and to be able to assume that
  // the buffer itself is always valid.
  buffer_->Stop();
  call_stats_->DeregisterStatsObserver(this);

  if (decoder_running_) {
    Event done;
    decode_queue_->PostTask([this, &done] {
      RTC_DCHECK_RUN_ON(&decode_sequence_checker_);
      // Set `decoder_stopped_` before deregistering all decoders. This means
      // that any pending encoded frame will return early without trying to
      // access the decoder database.
      decoder_stopped_ = true;
      for (const Decoder& decoder : active_decoders_) {
        video_receiver_.RegisterExternalDecoder(nullptr, decoder.payload_type);
      }
      done.Set();
    });
    done.Wait(Event::kForever);

    decoder_running_ = false;
    video_receiver_.SetDecodeQueue(nullptr);
    stats_proxy_.DecoderThreadStopped();

    UpdateHistograms();
  }

  rtp_video_stream_receiver_.RemoveReceiveCodecs();
  video_receiver_.DeregisterReceiveCodecs();

  video_stream_decoder_.reset();
  incoming_video_stream_.reset();
  transport_adapter_.Disable();

  // Reset task_safety_ after video_stream_decoder_ and incoming_video_stream_
  // have been destroyed to safely invalidate any pending worker queue tasks
  // from the stopped stream run without data races.
  task_safety_.reset();
}

void VideoReceiveStream2::SetRtcpMode(RtcpMode mode) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  config_.rtp.rtcp_mode = mode;
  rtp_video_stream_receiver_.SetRtcpMode(mode);
}

void VideoReceiveStream2::SetFlexFecProtection(
    RtpPacketSinkInterface* flexfec_sink) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  rtp_video_stream_receiver_.SetPacketSink(flexfec_sink);
  config_.rtp.packet_sink_ = flexfec_sink;
  config_.rtp.protected_by_flexfec = (flexfec_sink != nullptr);
}

void VideoReceiveStream2::SetLossNotificationEnabled(bool enabled) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  config_.rtp.lntf.enabled = enabled;
  rtp_video_stream_receiver_.SetLossNotificationEnabled(enabled);
}

void VideoReceiveStream2::SetNackHistory(TimeDelta history) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  RTC_DCHECK_GE(history.ms(), 0);

  if (config_.rtp.nack.rtp_history_ms == history.ms())
    return;

  config_.rtp.nack.rtp_history_ms = history.ms();

  const bool protected_by_fec =
      config_.rtp.protected_by_flexfec ||
      rtp_video_stream_receiver_.ulpfec_payload_type() != -1;

  buffer_->SetProtectionMode(history.ms() > 0 && protected_by_fec
                                 ? kProtectionNackFEC
                                 : kProtectionNack);

  rtp_video_stream_receiver_.SetNackHistory(history);
  TimeDelta max_wait_for_keyframe = DetermineMaxWaitForFrame(history, true);
  TimeDelta max_wait_for_frame = DetermineMaxWaitForFrame(history, false);

  rtp_video_stream_receiver_.SetMaxWaitForKeyframe(max_wait_for_keyframe);
  max_wait_for_keyframe_ = max_wait_for_keyframe;
  max_wait_for_frame_ = max_wait_for_frame;

  buffer_->SetMaxWaits(max_wait_for_keyframe, max_wait_for_frame);
}

void VideoReceiveStream2::SetProtectionPayloadTypes(int red_payload_type,
                                                    int ulpfec_payload_type) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  rtp_video_stream_receiver_.SetProtectionPayloadTypes(red_payload_type,
                                                       ulpfec_payload_type);
}

void VideoReceiveStream2::SetRtcpXr(Config::Rtp::RtcpXr rtcp_xr) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  rtp_video_stream_receiver_.SetReferenceTimeReport(
      rtcp_xr.receiver_reference_time_report);
}

void VideoReceiveStream2::SetAssociatedPayloadTypes(
    std::map<int, int> associated_payload_types) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  if (!rtx_receive_stream_)
    return;

  rtx_receive_stream_->SetAssociatedPayloadTypes(
      std::move(associated_payload_types));
}

std::vector<RtpVideoStreamReceiver2::ReceiveCodec>
VideoReceiveStream2::GetReceiveCodecConfig() const {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  std::vector<RtpVideoStreamReceiver2::ReceiveCodec> receive_codecs;
  receive_codecs.reserve(config_.decoders.size());
  for (const Decoder& decoder : config_.decoders) {
    VideoDecoder::Settings settings;
    settings.set_codec_type(
        PayloadStringToCodecType(decoder.video_format.name));
    settings.set_max_render_resolution(
        InitialDecoderResolution(env_.field_trials()));
    settings.set_number_of_cores(num_cpu_cores_);

    const bool raw_payload =
        config_.rtp.raw_payload_types.count(decoder.payload_type) > 0;
    receive_codecs.push_back({
        .payload_type = static_cast<uint8_t>(decoder.payload_type),
        .video_codec = settings.codec_type(),
        .codec_params = decoder.video_format.parameters,
        .raw_payload = raw_payload,
    });
  }
  return receive_codecs;
}

bool VideoReceiveStream2::RegisterCodecsOnReceiver(
    const std::vector<Decoder>& old_decoders,
    const std::vector<Decoder>& new_decoders) {
  // Deregister decoders from the previous configuration that are removed or
  // updated in the new configuration. Unchanged decoders are left as is, so
  // that a decoder that is in use keeps running. Releasing and configuring it
  // again would require a keyframe and isn't supported by all implementations
  // (e.g. hardware decoders may fall back to software).
  bool removed_decoder_instance = false;
  for (const Decoder& old_decoder : old_decoders) {
    if (absl::c_linear_search(new_decoders, old_decoder)) {
      continue;
    }
    if (video_receiver_.IsExternalDecoderRegistered(old_decoder.payload_type)) {
      video_receiver_.RegisterExternalDecoder(nullptr,
                                              old_decoder.payload_type);
      removed_decoder_instance = true;
    }
    video_receiver_.DeregisterReceiveCodec(old_decoder.payload_type);
  }

  // Register settings for added or updated decoders.
  for (const Decoder& decoder : new_decoders) {
    if (absl::c_linear_search(old_decoders, decoder)) {
      continue;
    }
    VideoDecoder::Settings settings;
    settings.set_codec_type(
        PayloadStringToCodecType(decoder.video_format.name));
    settings.set_max_render_resolution(
        InitialDecoderResolution(env_.field_trials()));
    settings.set_number_of_cores(num_cpu_cores_);

    video_receiver_.RegisterReceiveCodec(decoder.payload_type, settings);
  }

  return removed_decoder_instance;
}

void VideoReceiveStream2::ConfigureCodecs() {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  RegisterCodecsOnReceiver(/*old_decoders=*/{}, config_.decoders);
  rtp_video_stream_receiver_.SetReceiveCodecs(GetReceiveCodecConfig());
}

void VideoReceiveStream2::SetDecoders(std::vector<Decoder> decoders) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  if (config_.decoders == decoders) {
    return;
  }

  VerifyNoDuplicatePayloadTypes(decoders);

  std::vector<Decoder> old_decoders = std::move(config_.decoders);
  config_.decoders = std::move(decoders);

  if (!decoder_running_) {
    return;
  }

  // Post codec registration task to decode_queue_ before updating
  // rtp_video_stream_receiver_ to ensure the codec database is updated on the
  // decode queue before any newly arriving RTP frames are processed. Note that
  // if Stop() is called, it synchronously flushes decode_queue_ before setting
  // decoder_running_ to false, avoiding a clash with ConfigureCodecs().
  decode_queue_->PostTask([this, old_decoders = std::move(old_decoders),
                           new_decoders = config_.decoders,
                           task_safety_flag = task_safety_.flag()]() mutable {
    RTC_DCHECK_RUN_ON(&decode_sequence_checker_);
    const bool removed_decoder_instance =
        RegisterCodecsOnReceiver(old_decoders, new_decoders);
    active_decoders_ = std::move(new_decoders);
    if (!removed_decoder_instance) {
      return;
    }
    // The decoder that was in use has been removed or replaced. Request a
    // keyframe right away since delta frames currently in the jitter buffer or
    // arriving before the keyframe will fail to decode due to missing state.
    call_->worker_thread()->PostTask(
        SafeTask(std::move(task_safety_flag), [this] {
          RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
          RequestKeyFrame(env_.clock().CurrentTime());
        }));
  });

  rtp_video_stream_receiver_.SetReceiveCodecs(GetReceiveCodecConfig());
}

void VideoReceiveStream2::SetRawPayloadTypes(std::set<int> raw_payload_types) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  if (config_.rtp.raw_payload_types == raw_payload_types) {
    return;
  }

  config_.rtp.raw_payload_types = std::move(raw_payload_types);
  rtp_video_stream_receiver_.SetRawPayloadTypes(config_.rtp.raw_payload_types);
}

void VideoReceiveStream2::CreateAndRegisterExternalDecoder(
    const Decoder& decoder) {
  TRACE_EVENT0("webrtc",
               "VideoReceiveStream2::CreateAndRegisterExternalDecoder");
  std::unique_ptr<VideoDecoder> video_decoder =
      decoder_factory_->Create(env_, decoder.video_format);
  // The factory can end up in this state either if the format is not supported
  // or because a creation step failed, e.g. HW is unavailable.
  if (!video_decoder) {
    video_decoder = std::make_unique<NullVideoDecoder>();
  }

  std::string decoded_output_file =
      env_.field_trials().Lookup("WebRTC-DecoderDataDumpDirectory");
  // Because '/' can't be used inside a field trial parameter, we use ';'
  // instead.
  // This is only relevant to WebRTC-DecoderDataDumpDirectory
  // field trial. ';' is chosen arbitrary. Even though it's a legal character
  // in some file systems, we can sacrifice ability to use it in the path to
  // dumped video, since it's developers-only feature for debugging.
  absl::c_replace(decoded_output_file, ';', '/');
  if (!decoded_output_file.empty()) {
    StringBuilder ssb;
    ssb << decoded_output_file << "/webrtc_receive_stream_" << remote_ssrc()
        << "-" << env_.clock().TimeInMicroseconds() << ".ivf";
    video_decoder = CreateFrameDumpingDecoderWrapper(std::move(video_decoder),
                                                     ssb.Release());
  }

  video_receiver_.RegisterExternalDecoder(std::move(video_decoder),
                                          decoder.payload_type);
}

VideoReceiveStreamInterface::Stats VideoReceiveStream2::GetStats() const {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  VideoReceiveStream2::Stats stats = stats_proxy_.GetStats();
  stats.total_bitrate_bps = 0;
  StreamStatistician* statistician =
      rtp_receive_statistics_->GetStatistician(stats.ssrc);
  if (statistician) {
    stats.rtp_stats = statistician->GetStats();
    stats.total_bitrate_bps = statistician->BitrateReceived();
  }
  if (rtx_ssrc()) {
    StreamStatistician* rtx_statistician =
        rtp_receive_statistics_->GetStatistician(rtx_ssrc());
    if (rtx_statistician) {
      stats.total_bitrate_bps += rtx_statistician->BitrateReceived();
      stats.rtx_rtp_stats = rtx_statistician->GetStats();
    }
  }

  std::optional<RtpRtcpInterface::SenderReportStats> rtcp_sr_stats =
      rtp_video_stream_receiver_.GetSenderReportStats();
  if (rtcp_sr_stats) {
    stats.last_sender_report_timestamp = rtcp_sr_stats->last_arrival_timestamp;
    stats.last_sender_report_utc_timestamp =
        Clock::NtpToUtc(rtcp_sr_stats->last_arrival_ntp_timestamp);
    stats.last_sender_report_remote_utc_timestamp =
        Clock::NtpToUtc(rtcp_sr_stats->last_remote_ntp_timestamp);
    stats.sender_reports_packets_sent = rtcp_sr_stats->packets_sent;
    stats.sender_reports_bytes_sent = rtcp_sr_stats->bytes_sent;
    stats.sender_reports_reports_count = rtcp_sr_stats->reports_count;
  }

  // Non-sender RTT (RRTR/DLRR) for recvonly endpoints, reported via
  // RTCRemoteOutboundRtpStreamStats round-trip-time fields.
  std::optional<RtpRtcpInterface::NonSenderRttStats> non_sender_rtt_stats =
      rtp_video_stream_receiver_.GetNonSenderRttStats();
  if (non_sender_rtt_stats) {
    stats.round_trip_time = non_sender_rtt_stats->round_trip_time;
    stats.round_trip_time_measurements =
        non_sender_rtt_stats->round_trip_time_measurements;
    stats.total_round_trip_time = non_sender_rtt_stats->total_round_trip_time;
  }

  // Mozilla modification: VideoReceiveStream2 and friends do not surface RTCP
  // stats at all, and even on the most recent libwebrtc code there does not
  // seem to be any support for these stats right now. So, we hack this in.
  rtp_video_stream_receiver_.RemoteRTCPSenderInfo(
      &stats.rtcp_sender_packets_sent, &stats.rtcp_sender_octets_sent,
      &stats.rtcp_sender_ntp_timestamp_ms,
      &stats.rtcp_sender_remote_ntp_timestamp_ms);

  return stats;
}

void VideoReceiveStream2::UpdateHistograms() {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  std::optional<int> fraction_lost;
  StreamDataCounters rtp_stats;
  StreamStatistician* statistician =
      rtp_receive_statistics_->GetStatistician(remote_ssrc());
  if (statistician) {
    fraction_lost = statistician->GetFractionLostInPercent();
    rtp_stats = statistician->GetReceiveStreamDataCounters();
  }
  if (rtx_ssrc()) {
    StreamStatistician* rtx_statistician =
        rtp_receive_statistics_->GetStatistician(rtx_ssrc());
    if (rtx_statistician) {
      StreamDataCounters rtx_stats =
          rtx_statistician->GetReceiveStreamDataCounters();
      stats_proxy_.UpdateHistograms(fraction_lost, rtp_stats, &rtx_stats);
      return;
    }
  }
  stats_proxy_.UpdateHistograms(fraction_lost, rtp_stats, nullptr);
}

void VideoReceiveStream2::CalculateCorruptionScore(
    const VideoFrame& frame,
    FrameInstrumentationData frame_instrumentation_data,
    VideoContentType content_type) {
  RTC_DCHECK_RUNS_SERIALIZED(&decode_callback_race_checker_);

  if (post_decode_queue_) {
    // Set the max number of pending post decode tasks very conservative since
    // each one has a refcounted VideoFrame.
    constexpr int kMaxPendingPostDecodeFrames = 2;
    std::optional<VideoFrame> frame_to_evaluate;
    if (pending_post_decode_frames_.load() >= kMaxPendingPostDecodeFrames) {
      RTC_LOG(LS_WARNING)
          << "Post decode queue too long, discarding frame evaluation data.";
    } else {
      frame_to_evaluate = frame;
      pending_post_decode_frames_.fetch_add(1, std::memory_order_relaxed);
    }

    post_decode_queue_->PostTask(
        [this,
         frame_instrumentation_data = std::move(frame_instrumentation_data),
         frame_to_evaluate = std::move(frame_to_evaluate),
         content_type]() mutable {
          if (frame_to_evaluate) {
            frame_evaluator_->OnInstrumentedFrame(
                std::move(frame_instrumentation_data), *frame_to_evaluate,
                content_type);
            pending_post_decode_frames_.fetch_sub(1, std::memory_order_relaxed);
          } else {
            frame_evaluator_->OnSkippedInstrumentedFrame(
                std::move(frame_instrumentation_data));
          }
        });
  } else {
    // Do the frame evaluation synchronously.
    frame_evaluator_->OnInstrumentedFrame(std::move(frame_instrumentation_data),
                                          frame, content_type);
  }
}

bool VideoReceiveStream2::SetBaseMinimumPlayoutDelayMs(int delay_ms) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  TimeDelta delay = TimeDelta::Millis(delay_ms);
  if (delay < kMinBaseMinimumDelay || delay > kMaxBaseMinimumDelay) {
    return false;
  }

  base_minimum_playout_delay_ = delay;
  UpdatePlayoutDelays();
  return true;
}

int VideoReceiveStream2::GetBaseMinimumPlayoutDelayMs() const {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  constexpr TimeDelta kDefaultBaseMinPlayoutDelay = TimeDelta::Millis(-1);
  // Unset must be -1.
  static_assert(-1 == kDefaultBaseMinPlayoutDelay.ms(), "");
  return base_minimum_playout_delay_.value_or(kDefaultBaseMinPlayoutDelay).ms();
}

void VideoReceiveStream2::OnFrame(const VideoFrame& video_frame) {
  // Capture current time once for both source tracking and frame delay metrics
  // to ensure coherent delivery timestamp across the delivery callback and
  // metadata. The callback is synchronous and non-blocking, so any difference
  // to renderer hand-off time is negligible.
  Timestamp now = env_.clock().CurrentTime();

  // TODO: bugs.webrtc.org/42220804 - we should set local capture clock offset
  // for `packet_infos`.
  const RtpPacketInfos& packet_infos = video_frame.packet_infos();
  // Invoke delivery callback before passing the frame to the renderer to
  // ensure source tracker updates happen before downstream observers are
  // notified.
  if (on_frame_delivered_callback_ != nullptr && !packet_infos.empty()) {
    on_frame_delivered_callback_(packet_infos, now);
  }

  renderer_->OnFrame(video_frame);

  // For frame delay metrics, calculated in `OnRenderedFrame`, to better reflect
  // user experience measurements must be done as close as possible to frame
  // rendering moment. Frame may or may be not rendered by this time. This
  // results in inaccuracy but is still the best we can do in the absence of
  // "frame rendered" callback from the renderer.
  VideoFrameMetaData frame_meta(video_frame, now);
  call_->worker_thread()->PostTask(
      SafeTask(task_safety_.flag(), [frame_meta, this]() {
        RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
        int64_t video_playout_ntp_ms;
        int64_t sync_offset_ms;
        double estimated_freq_khz;
        if (rtp_stream_sync_.GetStreamSyncOffsetInMs(
                frame_meta.rtp_timestamp, frame_meta.render_time.ms(),
                &video_playout_ntp_ms, &sync_offset_ms, &estimated_freq_khz)) {
          stats_proxy_.OnSyncOffsetUpdated(video_playout_ntp_ms, sync_offset_ms,
                                           estimated_freq_khz);
        }
        stats_proxy_.OnRenderedFrame(frame_meta);
      }));

  MutexLock lock(&pending_resolution_mutex_);
  if (pending_resolution_.has_value()) {
    if (!pending_resolution_->empty() &&
        (video_frame.width() != static_cast<int>(pending_resolution_->width) ||
         video_frame.height() !=
             static_cast<int>(pending_resolution_->height))) {
      RTC_LOG(LS_WARNING)
          << "Recordable encoded frame stream resolution was reported as "
          << pending_resolution_->width << "x" << pending_resolution_->height
          << " but the stream is now " << video_frame.width()
          << video_frame.height();
    }
    pending_resolution_ = RecordableEncodedFrame::EncodedResolution{
        .width = static_cast<unsigned>(video_frame.width()),
        .height = static_cast<unsigned>(video_frame.height())};
  }
}

void VideoReceiveStream2::SetFrameDecryptor(
    scoped_refptr<FrameDecryptorInterface> frame_decryptor) {
  rtp_video_stream_receiver_.SetFrameDecryptor(std::move(frame_decryptor));
}

void VideoReceiveStream2::SetDepacketizerToDecoderFrameTransformer(
    scoped_refptr<FrameTransformerInterface> frame_transformer) {
  rtp_video_stream_receiver_.SetDepacketizerToDecoderFrameTransformer(
      std::move(frame_transformer));
}

void VideoReceiveStream2::RequestKeyFrame(Timestamp now) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  // Called from RtpVideoStreamReceiver (rtp_video_stream_receiver_ is
  // ultimately responsible).
  rtp_video_stream_receiver_.RequestKeyFrame();
  last_keyframe_request_ = now;
}

void VideoReceiveStream2::OnCompleteFrame(std::unique_ptr<EncodedFrame> frame) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);

  std::optional<VideoPlayoutDelay> playout_delay =
      frame->EncodedImage().PlayoutDelay();
  stats_proxy_.OnPlayoutDelay(playout_delay);
  if (playout_delay) {
    frame_minimum_playout_delay_ = playout_delay->min();
    frame_maximum_playout_delay_ = playout_delay->max();
    UpdatePlayoutDelays();
  }

  auto last_continuous_pid = buffer_->InsertFrame(std::move(frame));
  if (last_continuous_pid.has_value()) {
    rtp_video_stream_receiver_.FrameContinuous(*last_continuous_pid);
  }
}

void VideoReceiveStream2::OnRttUpdate(int64_t avg_rtt_ms, int64_t max_rtt_ms) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  // TODO(bugs.webrtc.org/13757): Replace with TimeDelta.
  buffer_->UpdateRtt(max_rtt_ms);
  rtp_video_stream_receiver_.UpdateRtt(max_rtt_ms);
  stats_proxy_.OnRttUpdate(avg_rtt_ms);
}

uint32_t VideoReceiveStream2::id() const {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  return remote_ssrc();
}

std::optional<Syncable::Info> VideoReceiveStream2::GetInfo() const {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  std::optional<Syncable::Info> info = rtp_video_stream_receiver_.GetSyncInfo();

  if (!info)
    return std::nullopt;

  info->current_delay = timing_->TargetVideoDelay();
  return info;
}

std::optional<Syncable::PlayoutInfo>
VideoReceiveStream2::GetPlayoutRtpTimestamp() const {
  RTC_DCHECK_NOTREACHED();
  return std::nullopt;
}

void VideoReceiveStream2::SetEstimatedPlayoutNtpTimestamp(NtpTime ntp_time,
                                                          Timestamp time) {
  RTC_DCHECK_NOTREACHED();
}

bool VideoReceiveStream2::SetMinimumPlayoutDelay(TimeDelta delay) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  syncable_minimum_playout_delay_ = delay;
  UpdatePlayoutDelays();
  return true;
}

void VideoReceiveStream2::OnEncodedFrame(std::unique_ptr<EncodedFrame> frame) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  Timestamp now = env_.clock().CurrentTime();
  const bool keyframe_request_is_due =
      !last_keyframe_request_ ||
      now >= (*last_keyframe_request_ + max_wait_for_keyframe_);
  const bool received_frame_is_keyframe = frame->IsKey();

  // Current OnPreDecode only cares about QP for VP8.
  // TODO(brandtr): Move to stats_proxy_.OnDecodableFrame in VSBC, or deprecate.
  int qp = -1;
  if (frame->CodecSpecific()->codecType == kVideoCodecVP8) {
    if (!vp8::GetQp(frame->data(), frame->size(), &qp)) {
      RTC_LOG(LS_WARNING) << "Failed to extract QP from VP8 video frame";
    }
  }
  stats_proxy_.OnPreDecode(frame->CodecSpecific()->codecType, qp);

  decode_queue_->PostTask([this, now, keyframe_request_is_due,
                           received_frame_is_keyframe, frame = std::move(frame),
                           keyframe_required = keyframe_required_,
                           task_safety_flag = task_safety_.flag()]() mutable {
    RTC_DCHECK_RUN_ON(&decode_sequence_checker_);
    if (decoder_stopped_)
      return;
    uint32_t rtp_timestamp = frame->RtpTimestamp();
    DecodeFrameResult result = HandleEncodedFrameOnDecodeQueue(
        std::move(frame), keyframe_request_is_due, keyframe_required);

    call_->worker_thread()->PostTask(
        SafeTask(std::move(task_safety_flag),
                 [this, now, rtp_timestamp, result = std::move(result),
                  received_frame_is_keyframe, keyframe_request_is_due]() {
                   RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
                   keyframe_required_ = result.keyframe_required;

                   if (result.decoded_frame_picture_id) {
                     rtp_video_stream_receiver_.FrameDecoded(
                         *result.decoded_frame_picture_id);
                   }
                   last_decoded_rtp_timestamp_ = rtp_timestamp;

                   HandleKeyFrameGeneration(received_frame_is_keyframe, now,
                                            result.force_request_key_frame,
                                            keyframe_request_is_due);
                   buffer_->StartNextDecode(keyframe_required_);
                 }));
  });
}

void VideoReceiveStream2::OnDecodableFrameTimeout(TimeDelta wait) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  Timestamp now = env_.clock().CurrentTime();

  std::optional<int64_t> last_packet_ms =
      rtp_video_stream_receiver_.LastReceivedPacketMs();

  // To avoid spamming keyframe requests for a stream that is not active we
  // check if we have received a packet within the last 5 seconds.
  constexpr TimeDelta kInactiveDuration = TimeDelta::Seconds(5);
  const bool stream_is_active =
      last_packet_ms &&
      now - Timestamp::Millis(*last_packet_ms) < kInactiveDuration;
  if (!stream_is_active)
    stats_proxy_.OnStreamInactive();

  if (stream_is_active && !IsReceivingKeyFrame(now) &&
      (!require_frame_encryption_ ||
       rtp_video_stream_receiver_.IsDecryptable())) {
    std::optional<uint32_t> last_timestamp =
        rtp_video_stream_receiver_.LastReceivedFrameRtpTimestamp();
    RTC_LOG(LS_WARNING) << "No decodable frame in " << wait
                        << " requesting keyframe. Last RTP timestamp "
                        << (last_timestamp ? absl::StrCat(*last_timestamp)
                                           : "<not set>")
                        << ", last decoded frame RTP timestamp "
                        << (last_decoded_rtp_timestamp_
                                ? absl::StrCat(*last_decoded_rtp_timestamp_)
                                : "<not set>")
                        << ".";
    RequestKeyFrame(now);
  }

  buffer_->StartNextDecode(keyframe_required_);
}

VideoReceiveStream2::DecodeFrameResult
VideoReceiveStream2::HandleEncodedFrameOnDecodeQueue(
    std::unique_ptr<EncodedFrame> frame,
    bool keyframe_request_is_due,
    bool keyframe_required) {
  RTC_DCHECK_RUN_ON(&decode_sequence_checker_);

  bool force_request_key_frame = false;
  std::optional<int64_t> decoded_frame_picture_id;

  if (!video_receiver_.IsExternalDecoderRegistered(frame->PayloadType())) {
    // Look for the decoder with this payload type.
    for (const Decoder& decoder : active_decoders_) {
      if (decoder.payload_type == frame->PayloadType()) {
        CreateAndRegisterExternalDecoder(decoder);
      } else {
        // Unregister any external decoder not from this payload type.
        // If not, any previous video decoder will be released when the next
        // frame is decoded but the decoder wrapper will not.
        // This will cause the decoder to be reused if we switch back to that
        // payload in the future, failing to configure it and causing to
        // fallback to the software decoder.
        video_receiver_.RegisterExternalDecoder(nullptr, decoder.payload_type);
      }
    }
  }

  int64_t frame_id = frame->Id();
  int decode_result = DecodeAndMaybeDispatchEncodedFrame(std::move(frame));
  if (decode_result == WEBRTC_VIDEO_CODEC_OK ||
      decode_result == WEBRTC_VIDEO_CODEC_OK_REQUEST_KEYFRAME) {
    keyframe_required = false;
    frame_decoded_ = true;

    decoded_frame_picture_id = frame_id;

    if (decode_result == WEBRTC_VIDEO_CODEC_OK_REQUEST_KEYFRAME)
      force_request_key_frame = true;
  } else if (!frame_decoded_ || !keyframe_required || keyframe_request_is_due) {
    keyframe_required = true;
    // TODO(philipel): Remove this keyframe request when downstream project
    //                 has been fixed.
    force_request_key_frame = true;
  }

  return DecodeFrameResult{
      .force_request_key_frame = force_request_key_frame,
      .decoded_frame_picture_id = std::move(decoded_frame_picture_id),
      .keyframe_required = keyframe_required,
  };
}

int VideoReceiveStream2::DecodeAndMaybeDispatchEncodedFrame(
    std::unique_ptr<EncodedFrame> frame) {
  RTC_DCHECK_RUN_ON(&decode_sequence_checker_);

  // If `buffered_encoded_frames_` grows out of control (=60 queued frames),
  // maybe due to a stuck decoder, we just halt the process here and log the
  // error.
  const bool encoded_frame_output_enabled =
      encoded_frame_buffer_function_ != nullptr &&
      buffered_encoded_frames_.size() < kBufferedEncodedFramesMaxSize;
  EncodedFrame* frame_ptr = frame.get();
  if (encoded_frame_output_enabled) {
    // If we receive a key frame with unset resolution, hold on dispatching the
    // frame and following ones until we know a resolution of the stream.
    // NOTE: The code below has a race where it can report the wrong
    // resolution for keyframes after an initial keyframe of other resolution.
    // However, the only known consumer of this information is the W3C
    // MediaRecorder and it will only use the resolution in the first encoded
    // keyframe from WebRTC, so misreporting is fine.
    buffered_encoded_frames_.push_back(std::move(frame));
    if (buffered_encoded_frames_.size() == kBufferedEncodedFramesMaxSize)
      RTC_LOG(LS_ERROR) << "About to halt recordable encoded frame output due "
                           "to too many buffered frames.";

    MutexLock lock(&pending_resolution_mutex_);
    if (IsKeyFrameAndUnspecifiedResolution(*frame_ptr) &&
        !pending_resolution_.has_value())
      pending_resolution_.emplace();
  }

  int decode_result = video_receiver_.Decode(frame_ptr);
  if (decode_result < WEBRTC_VIDEO_CODEC_OK) {
    // Asynchronous decoders may delay error reporting, potentially resulting in
    // error reports reflecting issues that occurred several frames back.
    RTC_LOG(LS_WARNING)
        << "Failed to decode frame. Return code: " << decode_result
        << ", SSRC: " << remote_ssrc()
        << ", frame RTP timestamp: " << frame_ptr->RtpTimestamp()
        << ", type: " << VideoFrameTypeToString(frame_ptr->frame_type())
        << ", size: " << frame_ptr->size()
        << ", width: " << frame_ptr->_encodedWidth
        << ", height: " << frame_ptr->_encodedHeight
        << ", spatial idx: " << frame_ptr->SpatialIndex().value_or(-1)
        << ", temporal idx: " << frame_ptr->TemporalIndex().value_or(-1)
        << ", id: " << frame_ptr->Id();
  }

  if (encoded_frame_output_enabled) {
    std::optional<RecordableEncodedFrame::EncodedResolution> pending_resolution;
    {
      // Fish out `pending_resolution_` to avoid taking the mutex on every lap
      // or dispatching under the mutex in the flush loop.
      MutexLock lock(&pending_resolution_mutex_);
      if (pending_resolution_.has_value())
        pending_resolution = *pending_resolution_;
    }
    if (!pending_resolution.has_value() || !pending_resolution->empty()) {
      // Flush the buffered frames.
      for (const auto& buffered_frame : buffered_encoded_frames_) {
        RecordableEncodedFrame::EncodedResolution resolution{
            .width = buffered_frame->EncodedImage()._encodedWidth,
            .height = buffered_frame->EncodedImage()._encodedHeight};
        if (IsKeyFrameAndUnspecifiedResolution(*buffered_frame)) {
          RTC_DCHECK(!pending_resolution->empty());
          resolution = *pending_resolution;
        }
        encoded_frame_buffer_function_(
            WebRtcRecordableEncodedFrame(*buffered_frame, resolution));
      }
      buffered_encoded_frames_.clear();
    }
  }
  return decode_result;
}

void VideoReceiveStream2::HandleKeyFrameGeneration(
    bool received_frame_is_keyframe,
    Timestamp now,
    bool always_request_key_frame,
    bool keyframe_request_is_due) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  bool request_key_frame = always_request_key_frame;

  // Repeat sending keyframe requests if we've requested a keyframe.
  if (keyframe_generation_requested_) {
    if (received_frame_is_keyframe) {
      keyframe_generation_requested_ = false;
    } else if (keyframe_request_is_due) {
      if (!IsReceivingKeyFrame(now)) {
        request_key_frame = true;
      }
    } else {
      // It hasn't been long enough since the last keyframe request, do nothing.
    }
  }

  if (request_key_frame) {
    // HandleKeyFrameGeneration is initiated from the decode thread -
    // RequestKeyFrame() triggers a call back to the decode thread.
    // Perhaps there's a way to avoid that.
    RequestKeyFrame(now);
  }
}

bool VideoReceiveStream2::IsReceivingKeyFrame(Timestamp now) const {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  std::optional<int64_t> last_keyframe_packet_ms =
      rtp_video_stream_receiver_.LastReceivedKeyframePacketMs();

  // If we recently have been receiving packets belonging to a keyframe then
  // we assume a keyframe is currently being received.
  bool receiving_keyframe = last_keyframe_packet_ms &&
                            now - Timestamp::Millis(*last_keyframe_packet_ms) <
                                max_wait_for_keyframe_;
  return receiving_keyframe;
}

void VideoReceiveStream2::UpdatePlayoutDelays() const {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  const std::initializer_list<std::optional<TimeDelta>> min_delays = {
      frame_minimum_playout_delay_, base_minimum_playout_delay_,
      syncable_minimum_playout_delay_};

  // Since nullopt < anything, this will return the largest of the minumum
  // delays, or nullopt if all are nullopt.
  std::optional<TimeDelta> minimum_delay = std::max(min_delays);
  if (!minimum_delay.has_value()) {
    // `frame_maximum_playout_delay_` and `frame_minimum_delay_value_` are set
    // together. Thus absence of the `minimum_delay` implies absene of the
    // `frame_minimum_playout_delay_` and thus implies absence of the
    // `frame_maximum_playout_delay_`.
    RTC_DCHECK(!frame_maximum_playout_delay_.has_value());
    return;
  }

  // When maximum delay is smaller than minimum delay, maximum delay takes
  // priority. It arrived with the frame, and thus is an explicit request to
  // limit the delay.
  if (frame_maximum_playout_delay_.has_value() &&
      minimum_delay > *frame_maximum_playout_delay_) {
    minimum_delay = *frame_maximum_playout_delay_;
    if (timing_->min_playout_delay() != *minimum_delay) {
      RTC_LOG(LS_WARNING)
          << "Maximum playout delay " << *frame_maximum_playout_delay_
          << " overrides minimum delay. frame min delay="
          << OptionalDelayToLogString(frame_minimum_playout_delay_)
          << " base min delay="
          << OptionalDelayToLogString(base_minimum_playout_delay_)
          << " sync min delay="
          << OptionalDelayToLogString(syncable_minimum_playout_delay_);
    }
  }

  auto num_playout_delays_set =
      absl::c_count_if(min_delays, [](auto opt) { return opt.has_value(); });
  if (num_playout_delays_set > 1 &&
      timing_->min_playout_delay() != *minimum_delay) {
    RTC_LOG(LS_WARNING)
        << "Multiple playout delays set. Actual delay value set to "
        << *minimum_delay << " frame min delay="
        << OptionalDelayToLogString(frame_minimum_playout_delay_)
        << " base min delay="
        << OptionalDelayToLogString(base_minimum_playout_delay_)
        << " sync min delay="
        << OptionalDelayToLogString(syncable_minimum_playout_delay_);
  }
  if (frame_maximum_playout_delay_.has_value()) {
    timing_->set_playout_delay({*minimum_delay, *frame_maximum_playout_delay_});
  } else {
    timing_->set_min_playout_delay(*minimum_delay);
  }
  if (frame_minimum_playout_delay_ == TimeDelta::Zero() &&
      frame_maximum_playout_delay_ > TimeDelta::Zero()) {
    // TODO(kron): Estimate frame rate from video stream.
    constexpr Frequency kFrameRate = Frequency::Hertz(60);
    // Convert playout delay to number of frames.
    int max_composition_delay_in_frames =
        std::lrint(*frame_maximum_playout_delay_ * kFrameRate);
    // Subtract frames in buffer.
    max_composition_delay_in_frames =
        std::max(max_composition_delay_in_frames - buffer_->Size(), 0);
    timing_->SetMaxCompositionDelayInFrames(max_composition_delay_in_frames);
  }
}

VideoReceiveStream2::RecordingState
VideoReceiveStream2::SetAndGetRecordingState(RecordingState state,
                                             bool generate_key_frame) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  Event event;

  // Save old state, set the new state.
  RecordingState old_state;

  std::optional<Timestamp> last_keyframe_request = last_keyframe_request_;
  last_keyframe_request_ =
      generate_key_frame
          ? env_.clock().CurrentTime()
          : Timestamp::Millis(state.last_keyframe_request_ms.value_or(0));

  decode_queue_->PostTask(
      [this, &event, &old_state, callback = std::move(state.callback),
       last_keyframe_request = std::move(last_keyframe_request)]() mutable {
        RTC_DCHECK_RUN_ON(&decode_sequence_checker_);
        old_state.callback = std::move(encoded_frame_buffer_function_);
        encoded_frame_buffer_function_ = std::move(callback);

        old_state.last_keyframe_request_ms =
            last_keyframe_request.value_or(Timestamp::Zero()).ms();

        event.Set();
      });

  if (generate_key_frame) {
    rtp_video_stream_receiver_.RequestKeyFrame();
    keyframe_generation_requested_ = true;
  }

  event.Wait(Event::kForever);
  return old_state;
}

void VideoReceiveStream2::GenerateKeyFrame() {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  RequestKeyFrame(env_.clock().CurrentTime());
  keyframe_generation_requested_ = true;
}

void VideoReceiveStream2::UpdateRtxSsrc(uint32_t ssrc) {
  RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
  RTC_DCHECK(rtx_receive_stream_);

  rtx_receiver_.reset();
  updated_rtx_ssrc_ = ssrc;
  rtx_receiver_ = receiver_controller_->CreateReceiver(
      rtx_ssrc(), rtx_receive_stream_.get());
}

}  // namespace internal
}  // namespace webrtc
