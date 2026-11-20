/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef VIDEO_VIDEO_RECEIVE_STREAM2_H_
#define VIDEO_VIDEO_RECEIVE_STREAM2_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "api/crypto/frame_decryptor_interface.h"
#include "api/environment/environment.h"
#include "api/frame_transformer_interface.h"
#include "api/rtp_headers.h"
#include "api/rtp_packet_infos.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/corruption_detection/frame_instrumentation_data.h"
#include "api/video/corruption_detection/frame_instrumentation_evaluation.h"
#include "api/video/encoded_frame.h"
#include "api/video/recordable_encoded_frame.h"
#include "api/video/video_content_type.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "call/call.h"
#include "call/rtp_packet_sink_interface.h"
#include "call/syncable.h"
#include "call/video_receive_stream.h"
#include "common_video/include/corruption_score_calculator.h"
#include "modules/include/module_common_types.h"
#include "modules/video_coding/nack_requester.h"
#include "modules/video_coding/video_receiver2.h"
#include "rtc_base/race_checker.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/system/no_unique_address.h"
#include "rtc_base/thread_annotations.h"
#include "system_wrappers/include/ntp_time.h"
#include "video/decode_synchronizer.h"
#include "video/receive_statistics_proxy.h"
#include "video/rtp_streams_synchronizer2.h"
#include "video/rtp_video_stream_receiver2.h"
#include "video/transport_adapter.h"
#include "video/video_stream_buffer_controller.h"
#include "video/video_stream_decoder2.h"

namespace webrtc {

class RtpStreamReceiverInterface;
class RtpStreamReceiverControllerInterface;
class RtxReceiveStream;
class VCMTiming;

constexpr TimeDelta kMaxWaitForKeyFrame = TimeDelta::Millis(200);
constexpr TimeDelta kMaxWaitForFrame = TimeDelta::Seconds(3);

namespace internal {

class CallStats;

// Utility struct for grabbing metadata from a VideoFrame and processing it
// asynchronously without needing the actual frame data.
// Additionally the caller can bundle information from the current clock
// when the metadata is captured, for accurate reporting and not needing
// multiple calls to clock->Now().
struct VideoFrameMetaData {
  VideoFrameMetaData(const webrtc::VideoFrame& frame, Timestamp now)
      : rtp_timestamp(frame.rtp_timestamp()),
        render_time(Timestamp::Micros(frame.timestamp_us())),
        ntp_time_ms(frame.ntp_time_ms()),
        width(frame.width()),
        height(frame.height()),
        decode_timestamp(now) {}

  const uint32_t rtp_timestamp;
  const Timestamp render_time;
  const int64_t ntp_time_ms;
  const int width;
  const int height;

  const Timestamp decode_timestamp;
};

class VideoReceiveStream2
    : public webrtc::VideoReceiveStreamInterface,
      public VideoSinkInterface<VideoFrame>,
      public RtpVideoStreamReceiver2::OnCompleteFrameCallback,
      public Syncable,
      public CallStatsObserver,
      public FrameSchedulingReceiver,
      public CorruptionScoreCalculator {
 public:
  // The maximum number of buffered encoded frames when encoded output is
  // configured.
  static constexpr size_t kBufferedEncodedFramesMaxSize = 60;

  VideoReceiveStream2(const Environment& env,
                      Call* call,
                      int num_cpu_cores,
                      PacketRouter* packet_router,
                      VideoReceiveStreamInterface::Config config,
                      CallStats* call_stats,
                      std::unique_ptr<VCMTiming> timing,
                      NackPeriodicProcessor* nack_periodic_processor,
                      DecodeSynchronizer* decode_sync);
  // Destruction happens on the worker thread. Prior to destruction the caller
  // must ensure that a registration with the transport has been cleared. See
  // `RegisterWithTransport` for details.
  // TODO(tommi): As a further improvement to this, performing the full
  // destruction on the network thread could be made the default.
  ~VideoReceiveStream2() override;

  // Called on `worker_sequence_checker_` to register/unregister with the
  // network transport.
  void RegisterWithTransport(
      RtpStreamReceiverControllerInterface* receiver_controller);
  // If registration has previously been done (via `RegisterWithTransport`) then
  // `UnregisterFromTransport` must be called prior to destruction, on the
  // network thread.
  void UnregisterFromTransport();

  // Accessor for the a/v sync group. This value may change and the caller
  // must be on the worker thread.
  const std::string& sync_group() const;

  // Getters for const remote SSRC values that won't change throughout the
  // object's lifetime.
  uint32_t remote_ssrc() const { return remote_ssrc_; }
  // RTX ssrc can be updated.
  uint32_t rtx_ssrc() const {
    RTC_DCHECK_RUN_ON(&worker_sequence_checker_);
    return updated_rtx_ssrc_.value_or(config_.rtp.rtx_ssrc);
  }

  void SignalNetworkState(NetworkState state);
  bool DeliverRtcp(std::span<const uint8_t> packet);

  void SetSync(Syncable* audio_syncable);

  // Implements webrtc::VideoReceiveStreamInterface.
  void Start() override;
  void Stop() override;

  void SetRtcpMode(RtcpMode mode) override;
  void SetFlexFecProtection(RtpPacketSinkInterface* flexfec_sink) override;
  void SetNackHistory(TimeDelta history) override;
  void SetProtectionPayloadTypes(int red_payload_type,
                                 int ulpfec_payload_type) override;
  void SetRtcpXr(Config::Rtp::RtcpXr rtcp_xr) override;
  void SetAssociatedPayloadTypes(
      std::map<int, int> associated_payload_types) override;
  void SetRawPayloadTypes(std::set<int> raw_payload_types) override;
  void SetDecoders(std::vector<Decoder> decoders) override;

  webrtc::VideoReceiveStreamInterface::Stats GetStats() const override;

  // SetBaseMinimumPlayoutDelayMs and GetBaseMinimumPlayoutDelayMs are called
  // from webrtc/api level and requested by user code. For e.g. blink/js layer
  // in Chromium.
  bool SetBaseMinimumPlayoutDelayMs(int delay_ms) override;
  int GetBaseMinimumPlayoutDelayMs() const override;

  void SetFrameDecryptor(
      scoped_refptr<FrameDecryptorInterface> frame_decryptor) override;
  void SetDepacketizerToDecoderFrameTransformer(
      scoped_refptr<FrameTransformerInterface> frame_transformer) override;

  // Implements webrtc::VideoSinkInterface<VideoFrame>.
  void OnFrame(const VideoFrame& video_frame) override;

  // Implements RtpVideoStreamReceiver2::OnCompleteFrameCallback.
  void OnCompleteFrame(std::unique_ptr<EncodedFrame> frame) override;

  // Implements CallStatsObserver::OnRttUpdate
  void OnRttUpdate(int64_t avg_rtt_ms, int64_t max_rtt_ms) override;

  // Implements Syncable.
  uint32_t id() const override;
  std::optional<Syncable::Info> GetInfo() const override;
  std::optional<Syncable::PlayoutInfo> GetPlayoutRtpTimestamp() const override;
  void SetEstimatedPlayoutNtpTimestamp(NtpTime ntp_time,
                                       Timestamp time) override;

  // SetMinimumPlayoutDelay is only called by A/V sync.
  bool SetMinimumPlayoutDelay(TimeDelta delay) override;

  RecordingState SetAndGetRecordingState(RecordingState state,
                                         bool generate_key_frame) override;
  void GenerateKeyFrame() override;

  void UpdateRtxSsrc(uint32_t ssrc) override;

 private:
  // FrameSchedulingReceiver implementation.
  // Called on packet sequence.
  void OnEncodedFrame(std::unique_ptr<EncodedFrame> frame) override;
  // Called on packet sequence.
  void OnDecodableFrameTimeout(TimeDelta wait) override;

  void CreateAndRegisterExternalDecoder(const Decoder& decoder);

  struct DecodeFrameResult {
    // True if the decoder returned code WEBRTC_VIDEO_CODEC_OK_REQUEST_KEYFRAME,
    // or if the decoder failed and a keyframe is required. When true, a
    // keyframe request should be sent even if a keyframe request was sent
    // recently.
    bool force_request_key_frame;

    // The picture id of the frame that was decoded, or nullopt if the frame was
    // not decoded.
    std::optional<int64_t> decoded_frame_picture_id;

    // True if the next frame decoded must be a keyframe. This value will set
    // the value of `keyframe_required_`, which will force the frame buffer to
    // drop all frames that are not keyframes.
    bool keyframe_required;
  };

  DecodeFrameResult HandleEncodedFrameOnDecodeQueue(
      std::unique_ptr<EncodedFrame> frame,
      bool keyframe_request_is_due,
      bool keyframe_required) RTC_RUN_ON(decode_sequence_checker_);
  void UpdatePlayoutDelays() const
      RTC_EXCLUSIVE_LOCKS_REQUIRED(worker_sequence_checker_);
  void RequestKeyFrame(Timestamp now) RTC_RUN_ON(worker_sequence_checker_);
  void HandleKeyFrameGeneration(bool received_frame_is_keyframe,
                                Timestamp now,
                                bool always_request_key_frame,
                                bool keyframe_request_is_due)
      RTC_RUN_ON(worker_sequence_checker_);
  bool IsReceivingKeyFrame(Timestamp timestamp) const
      RTC_RUN_ON(worker_sequence_checker_);
  int DecodeAndMaybeDispatchEncodedFrame(std::unique_ptr<EncodedFrame> frame)
      RTC_RUN_ON(decode_sequence_checker_);

  void UpdateHistograms();
  void ConfigureCodecs() RTC_RUN_ON(worker_sequence_checker_);
  // Applies the difference between `old_decoders` and `new_decoders` to the
  // internal VideoReceiver2 database. Removed or updated decoders, including
  // their external decoder instances, are deregistered and added or updated
  // decoders are registered. Unchanged decoders are left as is. Returns true if
  // an external decoder instance was deregistered.
  bool RegisterCodecsOnReceiver(const std::vector<Decoder>& old_decoders,
                                const std::vector<Decoder>& new_decoders);
  std::vector<RtpVideoStreamReceiver2::ReceiveCodec> GetReceiveCodecConfig()
      const RTC_RUN_ON(worker_sequence_checker_);
  void CalculateCorruptionScore(
      const VideoFrame& frame,
      FrameInstrumentationData frame_instrumentation_data,
      VideoContentType content_type) override;

  const Environment env_;

  RTC_NO_UNIQUE_ADDRESS SequenceChecker worker_sequence_checker_;
  RTC_NO_UNIQUE_ADDRESS SequenceChecker decode_sequence_checker_;

  // Checks that only one decoder callback at a time happens, regardless of
  // which actual threads are used (e.g. decode vs media sequence).
  RaceChecker decode_callback_race_checker_;

  TransportAdapter transport_adapter_;
  VideoReceiveStreamInterface::Config config_
      RTC_GUARDED_BY(worker_sequence_checker_);
  const uint32_t remote_ssrc_;
  VideoSinkInterface<VideoFrame>* const renderer_;
  const absl::AnyInvocable<void(const RtpPacketInfos&, Timestamp) const>
      on_frame_delivered_callback_;
  VideoDecoderFactory* const decoder_factory_;
  const bool require_frame_encryption_;
  const int num_cpu_cores_;
  Call* const call_;

  CallStats* const call_stats_;

  bool decoder_running_ RTC_GUARDED_BY(worker_sequence_checker_) = false;
  bool decoder_stopped_ RTC_GUARDED_BY(decode_sequence_checker_) = true;

  ReceiveStatisticsProxy stats_proxy_;
  // Shared by media and rtx stream receivers, since the latter has no RtpRtcp
  // module of its own.
  const std::unique_ptr<ReceiveStatistics> rtp_receive_statistics_;

  const std::unique_ptr<VCMTiming> timing_;  // Jitter buffer experiment.
  VideoReceiver2 video_receiver_;
  std::unique_ptr<VideoSinkInterface<VideoFrame>> incoming_video_stream_;

  // Maximum wait times for keyframes and frames. Configurable through field
  // trials and may be adjusted dynamically (see `DetermineMaxWaitForFrame`).
  TimeDelta max_wait_for_keyframe_ RTC_GUARDED_BY(worker_sequence_checker_);
  TimeDelta max_wait_for_frame_ RTC_GUARDED_BY(worker_sequence_checker_);

  RtpVideoStreamReceiver2 rtp_video_stream_receiver_;
  std::unique_ptr<VideoStreamDecoder> video_stream_decoder_;
  RtpStreamsSynchronizer rtp_stream_sync_;

  DecodeSynchronizer* const decode_sync_;

  std::unique_ptr<VideoStreamBufferController> buffer_
      RTC_GUARDED_BY(worker_sequence_checker_);

  // `receiver_controller_` is valid from when RegisterWithTransport is invoked
  //  until UnregisterFromTransport.
  RtpStreamReceiverControllerInterface* receiver_controller_
      RTC_GUARDED_BY(worker_sequence_checker_) = nullptr;

  std::unique_ptr<RtpStreamReceiverInterface> media_receiver_
      RTC_GUARDED_BY(worker_sequence_checker_);
  std::unique_ptr<RtxReceiveStream> rtx_receive_stream_
      RTC_GUARDED_BY(worker_sequence_checker_);
  std::optional<uint32_t> updated_rtx_ssrc_
      RTC_GUARDED_BY(worker_sequence_checker_);
  std::unique_ptr<RtpStreamReceiverInterface> rtx_receiver_
      RTC_GUARDED_BY(worker_sequence_checker_);

  // Whenever we are in an undecodable state (stream has just started or due to
  // a decoding error) we require a keyframe to restart the stream.
  bool keyframe_required_ RTC_GUARDED_BY(worker_sequence_checker_) = true;

  // If we have successfully decoded any frame.
  bool frame_decoded_ RTC_GUARDED_BY(decode_sequence_checker_) = false;

  std::optional<Timestamp> last_keyframe_request_
      RTC_GUARDED_BY(worker_sequence_checker_);

  // All of them tries to change current min_playout_delay on `timing_` but
  // source of the change request is different in each case. Among them the
  // biggest delay is used. -1 means use default value from the `timing_`.
  //
  // Minimum delay as decided by the RTP playout delay extension.
  std::optional<TimeDelta> frame_minimum_playout_delay_
      RTC_GUARDED_BY(worker_sequence_checker_);
  // Minimum delay as decided by the setLatency function in "webrtc/api".
  std::optional<TimeDelta> base_minimum_playout_delay_
      RTC_GUARDED_BY(worker_sequence_checker_);
  // Minimum delay as decided by the A/V synchronization feature.
  std::optional<TimeDelta> syncable_minimum_playout_delay_
      RTC_GUARDED_BY(worker_sequence_checker_);

  // Maximum delay as decided by the RTP playout delay extension.
  std::optional<TimeDelta> frame_maximum_playout_delay_
      RTC_GUARDED_BY(worker_sequence_checker_);

  // Function that is triggered with encoded frames, if not empty.
  std::function<void(const RecordableEncodedFrame&)>
      encoded_frame_buffer_function_ RTC_GUARDED_BY(decode_sequence_checker_);
  std::vector<VideoReceiveStreamInterface::Decoder> active_decoders_
      RTC_GUARDED_BY(decode_sequence_checker_);
  // Set to true while we're requesting keyframes but not yet received one.
  bool keyframe_generation_requested_ RTC_GUARDED_BY(worker_sequence_checker_) =
      false;
  // Lock to avoid unnecessary per-frame idle wakeups in the code.
  webrtc::Mutex pending_resolution_mutex_;
  // Signal from decode queue to OnFrame callback to fill pending_resolution_.
  // std::nullopt - no resolution needed. 0x0 - next OnFrame to fill with
  // received resolution. Not 0x0 - OnFrame has filled a resolution.
  std::optional<RecordableEncodedFrame::EncodedResolution> pending_resolution_
      RTC_GUARDED_BY(pending_resolution_mutex_);
  // Buffered encoded frames held while waiting for decoded resolution.
  std::vector<std::unique_ptr<EncodedFrame>> buffered_encoded_frames_
      RTC_GUARDED_BY(decode_sequence_checker_);

  std::unique_ptr<FrameInstrumentationEvaluation> frame_evaluator_;

  // Used to signal destruction to potentially pending tasks.
  ScopedTaskSafety task_safety_;

  // A lower priority task queue used for tasks associated with decoding but
  // that are not time critical and should not block the `decode_queue_`.
  // Declared before the `decoder_queue_` so it's destroyed after, since the
  // decoder queue might post tasks to this queue but not the other way around.
  const std::unique_ptr<TaskQueueBase, TaskQueueDeleter> post_decode_queue_;

  // A counter used to keep track of the number of reference counted video
  // frames and make sure this queue does not grow too long if the system is
  // heavily loaded (the decode_queue_ has a higher priority and may run at a
  // higher rate). Always incremented on the decoder callback sequence, but
  // decremented on the post decode queue sequence.
  std::atomic<int> pending_post_decode_frames_ = 0;

  // Defined last so they are destroyed before all other members, in particular
  // `decode_queue_` should be stopped before `decode_sequence_checker_` is
  // destructed to avoid races when running tasks on the `decode_queue_` during
  // VideoReceiveStream2 destruction.
  const std::unique_ptr<TaskQueueBase, TaskQueueDeleter> decode_queue_;

  std::optional<uint32_t> last_decoded_rtp_timestamp_;
};

}  // namespace internal
}  // namespace webrtc

#endif  // VIDEO_VIDEO_RECEIVE_STREAM2_H_
