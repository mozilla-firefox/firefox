/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "call/call.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/functional/bind_front.h"
#include "absl/strings/string_view.h"
#include "api/adaptation/resource.h"
#include "api/environment/environment.h"
#include "api/fec_controller.h"
#include "api/media_types.h"
#include "api/rtc_event_log/rtc_event_log.h"
#include "api/rtp_headers.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/transport/bitrate_settings.h"
#include "api/transport/network_control.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/timing/video_jitter_timing_factory.h"
#include "api/video/timing/video_jitter_timing_interface.h"
#include "api/video/video_stream_encoder_settings.h"
#include "audio/audio_receive_stream.h"
#include "audio/audio_send_stream.h"
#include "audio/audio_state.h"
#include "call/adaptation/broadcast_resource_listener.h"
#include "call/bitrate_allocator.h"
#include "call/call_config.h"
#include "call/flexfec_receive_stream.h"
#include "call/flexfec_receive_stream_impl.h"
#include "call/packet_receiver.h"
#include "call/receive_stream.h"
#include "call/rtp_config.h"
#include "call/rtp_stream_receiver_controller.h"
#include "call/rtp_transport_controller_send.h"
#include "call/version.h"
#include "call/video_receive_stream.h"
#include "call/video_send_stream.h"
#include "logging/rtc_event_log/events/rtc_event_audio_receive_stream_config.h"
#include "logging/rtc_event_log/events/rtc_event_rtcp_packet_incoming.h"
#include "logging/rtc_event_log/events/rtc_event_rtp_packet_incoming.h"
#include "logging/rtc_event_log/events/rtc_event_video_receive_stream_config.h"
#include "logging/rtc_event_log/events/rtc_event_video_send_stream_config.h"
#include "logging/rtc_event_log/rtc_stream_config.h"
#include "modules/congestion_controller/include/receive_side_congestion_controller.h"
#include "modules/rtp_rtcp/include/flexfec_receiver.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_util.h"
#include "modules/video_coding/fec_controller_default.h"
#include "modules/video_coding/nack_requester.h"
#include "rtc_base/checks.h"
#include "rtc_base/containers/flat_set.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/cpu_info.h"
#include "rtc_base/logging.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/system/no_unique_address.h"
#include "rtc_base/task_utils/repeating_task.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/trace_event.h"
#include "system_wrappers/include/clock.h"
#include "system_wrappers/include/metrics.h"
#include "video/call_stats2.h"
#include "video/config/video_encoder_config.h"
#include "video/decode_synchronizer.h"
#include "video/send_delay_stats.h"
#include "video/stats_counter.h"
#include "video/video_receive_stream2.h"
#include "video/video_send_stream_impl.h"

namespace webrtc {

namespace {

const int* FindKeyByValue(const std::map<int, int>& m, int v) {
  for (const auto& kv : m) {
    if (kv.second == v)
      return &kv.first;
  }
  return nullptr;
}

std::unique_ptr<rtclog::StreamConfig> CreateRtcLogStreamConfig(
    const VideoReceiveStreamInterface::Config& config) {
  auto rtclog_config = std::make_unique<rtclog::StreamConfig>();
  rtclog_config->remote_ssrc = config.rtp.remote_ssrc;
  rtclog_config->rtx_ssrc = config.rtp.rtx_ssrc;
  rtclog_config->rtcp_mode = config.rtp.rtcp_mode;

  for (const auto& d : config.decoders) {
    const int* search =
        FindKeyByValue(config.rtp.rtx_associated_payload_types, d.payload_type);
    rtclog_config->codecs.emplace_back(d.video_format.name, d.payload_type,
                                       search ? *search : 0);
  }
  return rtclog_config;
}

std::unique_ptr<rtclog::StreamConfig> CreateRtcLogStreamConfig(
    const VideoSendStream::Config& config,
    size_t ssrc_index) {
  auto rtclog_config = std::make_unique<rtclog::StreamConfig>();
  if (ssrc_index < config.rtp.rtx.ssrcs.size()) {
    rtclog_config->rtx_ssrc = config.rtp.rtx.ssrcs[ssrc_index];
  }
  rtclog_config->rtcp_mode = config.rtp.rtcp_mode;
  rtclog_config->rtp_extensions = config.rtp.extensions;

  rtclog_config->codecs.emplace_back(config.rtp.payload_name,
                                     config.rtp.payload_type,
                                     config.rtp.rtx.payload_type);
  return rtclog_config;
}

std::unique_ptr<rtclog::StreamConfig> CreateRtcLogStreamConfig(
    const AudioReceiveStreamInterface::Config& config) {
  auto rtclog_config = std::make_unique<rtclog::StreamConfig>();
  rtclog_config->remote_ssrc = config.rtp.remote_ssrc;
  return rtclog_config;
}

std::unique_ptr<VideoJitterTimingInterface> MaybeCreateVideoJitterTiming(
    const Environment& env,
    VideoJitterTimingFactory* video_timing_factory) {
  if (video_timing_factory) {
    return video_timing_factory->Create(env);
  }
  return nullptr;  // DefaultVideoJitterTiming will be used.
}

}  // namespace

namespace internal {

// Wraps an injected resource in a BroadcastResourceListener and handles adding
// and removing adapter resources to individual VideoSendStreams.
class ResourceVideoSendStreamForwarder {
 public:
  explicit ResourceVideoSendStreamForwarder(
      scoped_refptr<webrtc::Resource> resource)
      : broadcast_resource_listener_(std::move(resource)) {
    broadcast_resource_listener_.StartListening();
  }
  ~ResourceVideoSendStreamForwarder() {
    RTC_DCHECK(adapter_resources_.empty());
    broadcast_resource_listener_.StopListening();
  }

  scoped_refptr<webrtc::Resource> Resource() const {
    return broadcast_resource_listener_.SourceResource();
  }

  void OnCreateVideoSendStream(VideoSendStream* video_send_stream) {
    RTC_DCHECK(adapter_resources_.find(video_send_stream) ==
               adapter_resources_.end());
    auto adapter_resource =
        broadcast_resource_listener_.CreateAdapterResource();
    video_send_stream->AddAdaptationResource(adapter_resource);
    adapter_resources_.insert(
        std::make_pair(video_send_stream, adapter_resource));
  }

  void OnDestroyVideoSendStream(VideoSendStream* video_send_stream) {
    auto it = adapter_resources_.find(video_send_stream);
    RTC_DCHECK(it != adapter_resources_.end());
    broadcast_resource_listener_.RemoveAdapterResource(it->second);
    adapter_resources_.erase(it);
  }

 private:
  BroadcastResourceListener broadcast_resource_listener_;
  std::map<VideoSendStream*, scoped_refptr<webrtc::Resource>>
      adapter_resources_;
};

// Tracks the currently registered receiver sinks (AudioReceiveStreamImpl,
// VideoReceiveStream2, FlexfecReceiveStreamImpl). Receiver controllers consult
// it before delivering an incoming RTP packet to a sink resolved by the
// demuxer.
class ReceiveSinkRegistry : public RtpSinkValidator {
 public:
  ReceiveSinkRegistry() = default;
  ~ReceiveSinkRegistry() override = default;

  void OnSinkAdded(RtpPacketSinkInterface* sink) override {
    registered_sinks_.insert(sink);
  }
  void OnSinkRemoved(RtpPacketSinkInterface* sink) override {
    registered_sinks_.erase(sink);
  }
  bool IsValidSink(RtpPacketSinkInterface* sink) const override {
    return sink != nullptr && registered_sinks_.contains(sink);
  }

  bool IsEmpty() const { return registered_sinks_.empty(); }

 private:
  flat_set<RtpPacketSinkInterface*> registered_sinks_;
};

class Call final : public webrtc::Call,
                   public PacketReceiver,
                   public TargetTransferRateObserver,
                   public BitrateAllocator::LimitObserver {
 public:
  Call(CallConfig config,
       std::unique_ptr<RtpTransportControllerSendInterface> transport_send);
  ~Call() override;

  Call(const Call&) = delete;
  Call& operator=(const Call&) = delete;

  // Implements webrtc::Call.
  PacketReceiver* Receiver() override;

  webrtc::AudioSendStream* CreateAudioSendStream(
      const webrtc::AudioSendStream::Config& config) override;
  void DestroyAudioSendStream(webrtc::AudioSendStream* send_stream) override;

  webrtc::AudioReceiveStreamInterface* CreateAudioReceiveStream(
      webrtc::AudioReceiveStreamInterface::Config config) override;
  void DestroyAudioReceiveStream(
      webrtc::AudioReceiveStreamInterface* receive_stream) override;

  webrtc::VideoSendStream* CreateVideoSendStream(
      webrtc::VideoSendStream::Config config,
      VideoEncoderConfig encoder_config,
      EncoderSwitchRequestCallback encoder_switch_request_callback =
          nullptr) override;
  webrtc::VideoSendStream* CreateVideoSendStream(
      webrtc::VideoSendStream::Config config,
      VideoEncoderConfig encoder_config,
      EncoderSwitchRequestCallback encoder_switch_request_callback,
      std::unique_ptr<FecController> fec_controller) override;
  void DestroyVideoSendStream(webrtc::VideoSendStream* send_stream) override;

  webrtc::VideoReceiveStreamInterface* CreateVideoReceiveStream(
      webrtc::VideoReceiveStreamInterface::Config configuration) override;
  void DestroyVideoReceiveStream(
      webrtc::VideoReceiveStreamInterface* receive_stream) override;

  FlexfecReceiveStream* CreateFlexfecReceiveStream(
      const FlexfecReceiveStream::Config config) override;
  void DestroyFlexfecReceiveStream(
      FlexfecReceiveStream* receive_stream) override;

  void AddAdaptationResource(scoped_refptr<Resource> resource) override;

  RtpTransportControllerSendInterface* GetTransportControllerSend() override;

  Stats GetStats() const override;

  void SetPreferredRtcpCcAckType(
      RtcpFeedbackType preferred_rtcp_cc_ack_type) override;
  std::optional<int> FeedbackAccordingToRfc8888Count() override;
  std::optional<int> FeedbackAccordingToTransportCcCount() override;

  void DisconnectFromNetworkThread() override;

  TaskQueueBase* network_thread() const override;
  TaskQueueBase* worker_thread() const override;

  void DeliverRtcpPacket(CopyOnWriteBuffer packet) override;

  void DeliverRtpPacket(MediaType media_type,
                        RtpPacketReceived packet,
                        absl_nonnull OnUndemuxablePacketHandler
                            undemuxable_packet_handler) override;

  void SignalChannelNetworkState(MediaType media, NetworkState state) override;

  void OnUpdateSyncGroup(webrtc::AudioReceiveStreamInterface& stream,
                         absl::string_view sync_group) override;

  void OnSentPacket(const SentPacketInfo& sent_packet) override;

  // Implements TargetTransferRateObserver,
  void OnTargetTransferRate(TargetTransferRate msg) override;
  void OnStartRateUpdate(DataRate start_rate) override;
  void OnTransportOverheadChanged(DataSize transport_overhead) override;

  // Implements BitrateAllocator::LimitObserver.
  void OnAllocationLimitsChanged(BitrateAllocationLimits limits) override;

  void SetClientBitratePreferences(const BitrateSettings& preferences) override;

 private:
  // Thread-compatible class that collects received packet stats and exposes
  // them as UMA histograms on destruction.
  class ReceiveStats {
   public:
    explicit ReceiveStats(Clock* clock);
    ~ReceiveStats();

    void AddReceivedRtcpBytes(int bytes);
    void AddReceivedAudioBytes(int bytes, Timestamp arrival_time);
    void AddReceivedVideoBytes(int bytes, Timestamp arrival_time);

   private:
    RTC_NO_UNIQUE_ADDRESS SequenceChecker sequence_checker_;
    RateCounter received_bytes_per_second_counter_
        RTC_GUARDED_BY(sequence_checker_);
    RateCounter received_audio_bytes_per_second_counter_
        RTC_GUARDED_BY(sequence_checker_);
    RateCounter received_video_bytes_per_second_counter_
        RTC_GUARDED_BY(sequence_checker_);
    RateCounter received_rtcp_bytes_per_second_counter_
        RTC_GUARDED_BY(sequence_checker_);
    std::optional<Timestamp> first_received_rtp_audio_timestamp_
        RTC_GUARDED_BY(sequence_checker_);
    std::optional<Timestamp> last_received_rtp_audio_timestamp_
        RTC_GUARDED_BY(sequence_checker_);
    std::optional<Timestamp> first_received_rtp_video_timestamp_
        RTC_GUARDED_BY(sequence_checker_);
    std::optional<Timestamp> last_received_rtp_video_timestamp_
        RTC_GUARDED_BY(sequence_checker_);
  };

  // Thread-compatible class that collects sent packet stats and exposes
  // them as UMA histograms on destruction, provided SetFirstPacketTime was
  // called with a non-empty packet timestamp before the destructor.
  class SendStats {
   public:
    explicit SendStats(Clock* clock);

    void ReportStats(std::optional<Timestamp> first_sent_packet_time,
                     Timestamp now);
    void PauseSendAndPacerBitrateCounters();
    void AddTargetBitrateSample(uint32_t target_bitrate_bps);
    void SetMinAllocatableRate(BitrateAllocationLimits limits);

   private:
    RTC_NO_UNIQUE_ADDRESS SequenceChecker worker_thread_checker_{
        SequenceChecker::kDetached};
    AvgCounter estimated_send_bitrate_kbps_counter_
        RTC_GUARDED_BY(worker_thread_checker_);
    AvgCounter pacer_bitrate_kbps_counter_
        RTC_GUARDED_BY(worker_thread_checker_);
    uint32_t min_allocated_send_bitrate_bps_
        RTC_GUARDED_BY(worker_thread_checker_){0};
  };

  void OnSentPacket_w(const SentPacketInfo& sent_packet, Timestamp sent_time);

  AudioReceiveStreamImpl* FindAudioStreamForSyncGroup(
      absl::string_view sync_group) RTC_RUN_ON(worker_thread_);
  void ConfigureSync(absl::string_view sync_group) RTC_RUN_ON(worker_thread_);

  void NotifyBweOfReceivedPacket(const RtpPacketReceived& packet,
                                 MediaType media_type)
      RTC_RUN_ON(worker_thread_);

  bool RegisterReceiveStream(uint32_t ssrc, ReceiveStreamInterface* stream);
  bool UnregisterReceiveStream(uint32_t ssrc);

  void UpdateAggregateNetworkState();

  // Ensure that necessary process threads are started, and any required
  // callbacks have been registered.
  void EnsureStarted() RTC_RUN_ON(worker_thread_);

  const Environment env_;
  TaskQueueBase* const worker_thread_;
  TaskQueueBase* const network_thread_;
  const ScopedTaskSafety task_safety_;
  ReceiveSinkRegistry receive_sink_registry_ RTC_GUARDED_BY(worker_thread_);
  const std::unique_ptr<DecodeSynchronizer> decode_sync_;

  const int num_cpu_cores_;
  const std::unique_ptr<CallStats> call_stats_;
  const std::unique_ptr<BitrateAllocator> bitrate_allocator_;
  const CallConfig config_ RTC_GUARDED_BY(worker_thread_);

  NetworkState audio_network_state_ RTC_GUARDED_BY(worker_thread_);
  NetworkState video_network_state_ RTC_GUARDED_BY(worker_thread_);
  // TODO(bugs.webrtc.org/11993): Move aggregate_network_up_ over to the
  // network thread.
  bool aggregate_network_up_ RTC_GUARDED_BY(worker_thread_);

  // Schedules nack periodic processing on behalf of all streams.
  NackPeriodicProcessor nack_periodic_processor_;

  // Audio, Video, and FlexFEC receive streams are owned by the client that
  // creates them.
  // TODO(bugs.webrtc.org/11993): Move audio_receive_streams_,
  // video_receive_streams_ over to the network thread.
  std::set<AudioReceiveStreamImpl*> audio_receive_streams_
      RTC_GUARDED_BY(worker_thread_);
  std::set<VideoReceiveStream2*> video_receive_streams_
      RTC_GUARDED_BY(worker_thread_);
  // TODO(bugs.webrtc.org/7135, bugs.webrtc.org/9719): Should eventually be
  // injected at creation, with a single object in the bundled case.
  RtpStreamReceiverController audio_receiver_controller_{
      network_thread_, &receive_sink_registry_};
  RtpStreamReceiverController video_receiver_controller_{
      network_thread_, &receive_sink_registry_};

  // This extra map is used for receive processing which is
  // independent of media type.

  RTC_NO_UNIQUE_ADDRESS SequenceChecker receive_11993_checker_;

  // Audio and Video send streams are owned by the client that creates them.
  // TODO(bugs.webrtc.org/11993): `audio_send_ssrcs_` and `video_send_ssrcs_`
  // should be accessed on the network thread.
  std::map<uint32_t, AudioSendStream*> audio_send_ssrcs_
      RTC_GUARDED_BY(worker_thread_);
  std::map<uint32_t, VideoSendStreamImpl*> video_send_ssrcs_
      RTC_GUARDED_BY(worker_thread_);
  std::set<VideoSendStreamImpl*> video_send_streams_
      RTC_GUARDED_BY(worker_thread_);

  // Each forwarder wraps an adaptation resource that was added to the call.
  std::vector<std::unique_ptr<ResourceVideoSendStreamForwarder>>
      adaptation_resource_forwarders_ RTC_GUARDED_BY(worker_thread_);

  using RtpStateMap = std::map<uint32_t, RtpState>;
  RtpStateMap suspended_audio_send_ssrcs_ RTC_GUARDED_BY(worker_thread_);
  RtpStateMap suspended_video_send_ssrcs_ RTC_GUARDED_BY(worker_thread_);

  using RtpPayloadStateMap = std::map<uint32_t, RtpPayloadState>;
  RtpPayloadStateMap suspended_video_payload_states_
      RTC_GUARDED_BY(worker_thread_);

  ReceiveStats receive_stats_ RTC_GUARDED_BY(network_thread_);
  SendStats send_stats_ RTC_GUARDED_BY(worker_thread_);
  uint32_t last_bandwidth_bps_ RTC_GUARDED_BY(worker_thread_) = 0;
  uint32_t configured_max_padding_bitrate_bps_ RTC_GUARDED_BY(worker_thread_) =
      0;

  ReceiveSideCongestionController receive_side_cc_;
  RepeatingTaskHandle receive_side_cc_periodic_task_;
  RepeatingTaskHandle elastic_bandwidth_allocation_task_;

  const std::unique_ptr<SendDelayStats> video_send_delay_stats_;
  const Timestamp start_of_call_;

  bool is_started_ RTC_GUARDED_BY(worker_thread_) = false;

  std::optional<SentPacketInfo> last_sent_packet_
      RTC_GUARDED_BY(network_thread_);
  // Declared last since it will issue callbacks from a task queue. Declaring it
  // last ensures that it is destroyed first and any running tasks are finished.
  const std::unique_ptr<RtpTransportControllerSendInterface> transport_send_;
};
}  // namespace internal

std::string Call::Stats::ToString(int64_t time_ms) const {
  StringBuilder ss;
  ss << "Call stats: " << time_ms << ", {";
  ss << "send_bw_bps: " << send_bandwidth_bps << ", ";
  ss << "recv_bw_bps: " << recv_bandwidth_bps << ", ";
  ss << "max_pad_bps: " << max_padding_bitrate_bps << ", ";
  ss << "pacer_delay_ms: " << pacer_delay_ms << ", ";
  ss << "rtt_ms: " << rtt_ms;
  ss << '}';
  return ss.Release();
}

std::unique_ptr<Call> Call::Create(CallConfig config) {
  RTC_CHECK(config.worker_task_queue != nullptr);
  RTC_CHECK(config.network_task_queue_ != nullptr);
  // Received packets are delivered synchronously on the network thread and
  // access state that is guarded by the worker thread.
  RTC_CHECK_EQ(config.worker_task_queue, config.network_task_queue_);
  auto transport_send = std::make_unique<RtpTransportControllerSend>(
      config.ExtractTransportConfig());

  return std::make_unique<internal::Call>(std::move(config),
                                          std::move(transport_send));
}

// This method here to avoid subclasses has to implement this method.
// Call perf test will use Internal::Call::CreateVideoSendStream() to inject
// FecController.
VideoSendStream* Call::CreateVideoSendStream(
    VideoSendStream::Config /* config */,
    VideoEncoderConfig /* encoder_config */,
    EncoderSwitchRequestCallback /* encoder_switch_request_callback */,
    std::unique_ptr<FecController> /* fec_controller */) {
  return nullptr;
}

namespace internal {

Call::ReceiveStats::ReceiveStats(Clock* clock)
    : received_bytes_per_second_counter_(clock, nullptr, false),
      received_audio_bytes_per_second_counter_(clock, nullptr, false),
      received_video_bytes_per_second_counter_(clock, nullptr, false),
      received_rtcp_bytes_per_second_counter_(clock, nullptr, false) {
  sequence_checker_.Detach();
}

void Call::ReceiveStats::AddReceivedRtcpBytes(int bytes) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  if (received_bytes_per_second_counter_.HasSample()) {
    // First RTP packet has been received.
    received_bytes_per_second_counter_.Add(static_cast<int>(bytes));
    received_rtcp_bytes_per_second_counter_.Add(static_cast<int>(bytes));
  }
}

void Call::ReceiveStats::AddReceivedAudioBytes(int bytes,
                                               Timestamp arrival_time) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  received_bytes_per_second_counter_.Add(bytes);
  received_audio_bytes_per_second_counter_.Add(bytes);
  if (!first_received_rtp_audio_timestamp_)
    first_received_rtp_audio_timestamp_ = arrival_time;
  last_received_rtp_audio_timestamp_ = arrival_time;
}

void Call::ReceiveStats::AddReceivedVideoBytes(int bytes,
                                               Timestamp arrival_time) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  received_bytes_per_second_counter_.Add(bytes);
  received_video_bytes_per_second_counter_.Add(bytes);
  if (!first_received_rtp_video_timestamp_)
    first_received_rtp_video_timestamp_ = arrival_time;
  last_received_rtp_video_timestamp_ = arrival_time;
}

Call::ReceiveStats::~ReceiveStats() {
  // Destruction is allowed to happen on a different thread than
  // `sequence_checker_` represents. It is guaranteed that no more calls will
  // arrive from the network thread because all receive streams must be
  // unregistered and destroyed before `Call` is destroyed, and the transport
  // layer (PeerConnection) stops packet delivery before tearing down `Call`.

  if (first_received_rtp_audio_timestamp_) {
    RTC_HISTOGRAM_COUNTS_100000(
        "WebRTC.Call.TimeReceivingAudioRtpPacketsInSeconds",
        (*last_received_rtp_audio_timestamp_ -
         *first_received_rtp_audio_timestamp_)
            .seconds());
  }
  if (first_received_rtp_video_timestamp_) {
    RTC_HISTOGRAM_COUNTS_100000(
        "WebRTC.Call.TimeReceivingVideoRtpPacketsInSeconds",
        (*last_received_rtp_video_timestamp_ -
         *first_received_rtp_video_timestamp_)
            .seconds());
  }
  const int kMinRequiredPeriodicSamples = 5;
  AggregatedStats video_bytes_per_sec =
      received_video_bytes_per_second_counter_.GetStats();
  if (video_bytes_per_sec.num_samples > kMinRequiredPeriodicSamples) {
    RTC_HISTOGRAM_COUNTS_100000("WebRTC.Call.VideoBitrateReceivedInKbps",
                                video_bytes_per_sec.average * 8 / 1000);
    RTC_LOG(LS_INFO) << "WebRTC.Call.VideoBitrateReceivedInBps, "
                     << video_bytes_per_sec.ToStringWithMultiplier(8);
  }
  AggregatedStats audio_bytes_per_sec =
      received_audio_bytes_per_second_counter_.GetStats();
  if (audio_bytes_per_sec.num_samples > kMinRequiredPeriodicSamples) {
    RTC_HISTOGRAM_COUNTS_100000("WebRTC.Call.AudioBitrateReceivedInKbps",
                                audio_bytes_per_sec.average * 8 / 1000);
    RTC_LOG(LS_INFO) << "WebRTC.Call.AudioBitrateReceivedInBps, "
                     << audio_bytes_per_sec.ToStringWithMultiplier(8);
  }
  AggregatedStats rtcp_bytes_per_sec =
      received_rtcp_bytes_per_second_counter_.GetStats();
  if (rtcp_bytes_per_sec.num_samples > kMinRequiredPeriodicSamples) {
    RTC_HISTOGRAM_COUNTS_100000("WebRTC.Call.RtcpBitrateReceivedInBps",
                                rtcp_bytes_per_sec.average * 8);
    RTC_LOG(LS_INFO) << "WebRTC.Call.RtcpBitrateReceivedInBps, "
                     << rtcp_bytes_per_sec.ToStringWithMultiplier(8);
  }
  AggregatedStats recv_bytes_per_sec =
      received_bytes_per_second_counter_.GetStats();
  if (recv_bytes_per_sec.num_samples > kMinRequiredPeriodicSamples) {
    RTC_HISTOGRAM_COUNTS_100000("WebRTC.Call.BitrateReceivedInKbps",
                                recv_bytes_per_sec.average * 8 / 1000);
    RTC_LOG(LS_INFO) << "WebRTC.Call.BitrateReceivedInBps, "
                     << recv_bytes_per_sec.ToStringWithMultiplier(8);
  }
}

Call::SendStats::SendStats(Clock* clock)
    : estimated_send_bitrate_kbps_counter_(clock, nullptr, true),
      pacer_bitrate_kbps_counter_(clock, nullptr, true) {}

void Call::SendStats::ReportStats(
    std::optional<Timestamp> first_sent_packet_time,
    Timestamp now) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  if (!first_sent_packet_time)
    return;

  TimeDelta elapsed = now - *first_sent_packet_time;
  if (elapsed < metrics::kMinRunTime)
    return;

  const int kMinRequiredPeriodicSamples = 5;
  AggregatedStats send_bitrate_stats =
      estimated_send_bitrate_kbps_counter_.ProcessAndGetStats();
  if (send_bitrate_stats.num_samples > kMinRequiredPeriodicSamples) {
    RTC_HISTOGRAM_COUNTS_100000("WebRTC.Call.EstimatedSendBitrateInKbps",
                                send_bitrate_stats.average);
    RTC_LOG(LS_INFO) << "WebRTC.Call.EstimatedSendBitrateInKbps, "
                     << send_bitrate_stats.ToString();
  }
  AggregatedStats pacer_bitrate_stats =
      pacer_bitrate_kbps_counter_.ProcessAndGetStats();
  if (pacer_bitrate_stats.num_samples > kMinRequiredPeriodicSamples) {
    RTC_HISTOGRAM_COUNTS_100000("WebRTC.Call.PacerBitrateInKbps",
                                pacer_bitrate_stats.average);
    RTC_LOG(LS_INFO) << "WebRTC.Call.PacerBitrateInKbps, "
                     << pacer_bitrate_stats.ToString();
  }
}

void Call::SendStats::PauseSendAndPacerBitrateCounters() {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  estimated_send_bitrate_kbps_counter_.ProcessAndPause();
  pacer_bitrate_kbps_counter_.ProcessAndPause();
}

void Call::SendStats::AddTargetBitrateSample(uint32_t target_bitrate_bps) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  estimated_send_bitrate_kbps_counter_.Add(target_bitrate_bps / 1000);
  // Pacer bitrate may be higher than bitrate estimate if enforcing min
  // bitrate.
  uint32_t pacer_bitrate_bps =
      std::max(target_bitrate_bps, min_allocated_send_bitrate_bps_);
  pacer_bitrate_kbps_counter_.Add(pacer_bitrate_bps / 1000);
}

void Call::SendStats::SetMinAllocatableRate(BitrateAllocationLimits limits) {
  RTC_DCHECK_RUN_ON(&worker_thread_checker_);
  min_allocated_send_bitrate_bps_ = limits.min_allocatable_rate.bps();
}

Call::Call(CallConfig config,
           std::unique_ptr<RtpTransportControllerSendInterface> transport_send)
    : env_(config.env),
      worker_thread_(config.worker_task_queue),
      network_thread_(config.network_task_queue_),
      decode_sync_(
          config.decode_metronome
              ? std::make_unique<DecodeSynchronizer>(&env_.clock(),
                                                     config.decode_metronome,
                                                     worker_thread_)
              : nullptr),
      num_cpu_cores_(cpu_info::DetectNumberOfCores()),
      call_stats_(new CallStats(&env_.clock(), worker_thread_)),
      bitrate_allocator_(new BitrateAllocator(
          this,
          GetElasticRateAllocationFieldTrialParameter(env_.field_trials()))),
      config_(std::move(config)),
      audio_network_state_(kNetworkDown),
      video_network_state_(kNetworkDown),
      aggregate_network_up_(false),
      receive_stats_(&env_.clock()),
      send_stats_(&env_.clock()),
      receive_side_cc_(env_,
                       absl::bind_front(&PacketRouter::SendCombinedRtcpPacket,
                                        transport_send->packet_router()),
                       absl::bind_front(&PacketRouter::SendRemb,
                                        transport_send->packet_router())),
      video_send_delay_stats_(new SendDelayStats(&env_.clock())),
      start_of_call_(env_.clock().CurrentTime()),
      transport_send_(std::move(transport_send)) {
  RTC_DCHECK(network_thread_);
  receive_11993_checker_.Detach();

  // Do not remove this call; it is here to convince the compiler that the
  // WebRTC source timestamp string needs to be in the final binary.
  LoadWebRTCVersionInRegister();
}

Call::~Call() {
  RTC_DCHECK_RUN_ON(worker_thread_);

  RTC_CHECK(audio_send_ssrcs_.empty());
  RTC_CHECK(video_send_ssrcs_.empty());
  RTC_CHECK(video_send_streams_.empty());
  RTC_CHECK(audio_receive_streams_.empty());
  RTC_CHECK(video_receive_streams_.empty());
  RTC_CHECK(receive_sink_registry_.IsEmpty());

  if (is_started_) {
    receive_side_cc_periodic_task_.Stop();
    elastic_bandwidth_allocation_task_.Stop();
    call_stats_->DeregisterStatsObserver(&receive_side_cc_);
  }
  Timestamp now = env_.clock().CurrentTime();
  send_stats_.ReportStats(transport_send_->GetFirstPacketTime(), now);

  RTC_HISTOGRAM_COUNTS_100000("WebRTC.Call.LifetimeInSeconds",
                              (now - start_of_call_).seconds());
}

void Call::EnsureStarted() {
  if (is_started_) {
    return;
  }
  is_started_ = true;

  call_stats_->RegisterStatsObserver(&receive_side_cc_);

  ReceiveSideCongestionController* receive_side_cc = &receive_side_cc_;
  receive_side_cc_periodic_task_ = RepeatingTaskHandle::Start(
      worker_thread_,
      [receive_side_cc] { return receive_side_cc->MaybeProcess(); },
      TaskQueueBase::DelayPrecision::kLow, &env_.clock());

  // TODO(b/350555527): Remove after experiment
  if (GetElasticRateAllocationFieldTrialParameter(env_.field_trials()) !=
      DataRate::Zero()) {
    elastic_bandwidth_allocation_task_ = RepeatingTaskHandle::Start(
        worker_thread_,
        [this] {
          TimeDelta next_schedule_interval = TimeDelta::Millis(25);
          if (bitrate_allocator_) {
            if (!bitrate_allocator_->RecomputeAllocationIfNeeded())
              next_schedule_interval = TimeDelta::Millis(300);
          }
          return next_schedule_interval;
        },
        TaskQueueBase::DelayPrecision::kLow, &env_.clock());
  }

  call_stats_->EnsureStarted();

  // This call seems to kick off a number of things, so probably better left
  // off being kicked off on request rather than in the ctor.
  transport_send_->RegisterTargetTransferRateObserver(this);

  transport_send_->EnsureStarted();
}

void Call::SetClientBitratePreferences(const BitrateSettings& preferences) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  GetTransportControllerSend()->SetClientBitratePreferences(preferences);
}

PacketReceiver* Call::Receiver() {
  return this;
}

webrtc::AudioSendStream* Call::CreateAudioSendStream(
    const webrtc::AudioSendStream::Config& config) {
  TRACE_EVENT0("webrtc", "Call::CreateAudioSendStream");
  RTC_DCHECK_RUN_ON(worker_thread_);

  EnsureStarted();

  // Stream config is logged in AudioSendStream::ConfigureStream, as it may
  // change during the stream's lifetime.
  std::optional<RtpState> suspended_rtp_state;
  {
    const auto& iter = suspended_audio_send_ssrcs_.find(config.rtp.ssrc);
    if (iter != suspended_audio_send_ssrcs_.end()) {
      suspended_rtp_state.emplace(iter->second);
    }
  }

  AudioSendStream* send_stream =
      new AudioSendStream(env_, config, config_.audio_state,
                          transport_send_.get(), bitrate_allocator_.get(),
                          call_stats_->AsRtcpRttStats(), suspended_rtp_state);
  RTC_DCHECK(audio_send_ssrcs_.find(config.rtp.ssrc) ==
             audio_send_ssrcs_.end());
  audio_send_ssrcs_[config.rtp.ssrc] = send_stream;

  UpdateAggregateNetworkState();

  return send_stream;
}

void Call::DestroyAudioSendStream(webrtc::AudioSendStream* send_stream) {
  TRACE_EVENT0("webrtc", "Call::DestroyAudioSendStream");
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DCHECK(send_stream != nullptr);

  send_stream->Stop();

  const uint32_t ssrc = send_stream->GetConfig().rtp.ssrc;
  webrtc::internal::AudioSendStream* audio_send_stream =
      static_cast<webrtc::internal::AudioSendStream*>(send_stream);
  suspended_audio_send_ssrcs_[ssrc] = audio_send_stream->GetRtpState();

  size_t num_deleted = audio_send_ssrcs_.erase(ssrc);
  RTC_DCHECK_EQ(1, num_deleted);

  UpdateAggregateNetworkState();

  delete send_stream;
}

webrtc::AudioReceiveStreamInterface* Call::CreateAudioReceiveStream(
    webrtc::AudioReceiveStreamInterface::Config config) {
  TRACE_EVENT0("webrtc", "Call::CreateAudioReceiveStream");
  RTC_DCHECK_RUN_ON(worker_thread_);
  EnsureStarted();
  env_.event_log().Log(std::make_unique<RtcEventAudioReceiveStreamConfig>(
      CreateRtcLogStreamConfig(config)));

  AudioReceiveStreamImpl* receive_stream = new AudioReceiveStreamImpl(
      env_, transport_send_->packet_router(), config_.neteq_factory,
      std::move(config), config_.audio_state);
  audio_receive_streams_.insert(receive_stream);

  // TODO(bugs.webrtc.org/11993): Make the registration on the network thread
  // (asynchronously). The registration and `audio_receiver_controller_` need
  // to live on the network thread.
  receive_stream->RegisterWithTransport(&audio_receiver_controller_);

  ConfigureSync(receive_stream->sync_group());

  UpdateAggregateNetworkState();
  return receive_stream;
}

void Call::DestroyAudioReceiveStream(
    webrtc::AudioReceiveStreamInterface* receive_stream) {
  TRACE_EVENT0("webrtc", "Call::DestroyAudioReceiveStream");
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DCHECK(receive_stream != nullptr);
  webrtc::AudioReceiveStreamImpl* audio_receive_stream =
      static_cast<webrtc::AudioReceiveStreamImpl*>(receive_stream);

  // TODO(bugs.webrtc.org/11993): Access the map, rtp config, call ConfigureSync
  // and UpdateAggregateNetworkState on the network thread. The call to
  // `UnregisterFromTransport` should also happen on the network thread.
  audio_receive_stream->UnregisterFromTransport();

  uint32_t ssrc = audio_receive_stream->remote_ssrc();
  receive_side_cc_.RemoveStream(ssrc);

  audio_receive_streams_.erase(audio_receive_stream);

  // After calling erase(), call ConfigureSync. This will clear associated
  // video streams or associate them with a different audio stream if one exists
  // for this sync_group.
  ConfigureSync(audio_receive_stream->sync_group());

  UpdateAggregateNetworkState();
  // TODO(bugs.webrtc.org/11993): Consider if deleting `audio_receive_stream`
  // on the network thread would be better or if we'd need to tear down the
  // state in two phases.
  delete audio_receive_stream;
}

// This method can be used for Call tests with external fec controller factory.
webrtc::VideoSendStream* Call::CreateVideoSendStream(
    webrtc::VideoSendStream::Config config,
    VideoEncoderConfig encoder_config,
    EncoderSwitchRequestCallback encoder_switch_request_callback,
    std::unique_ptr<FecController> fec_controller) {
  TRACE_EVENT0("webrtc", "Call::CreateVideoSendStream");
  RTC_DCHECK_RUN_ON(worker_thread_);

  EnsureStarted();

  video_send_delay_stats_->AddSsrcs(config);
  for (size_t ssrc_index = 0; ssrc_index < config.rtp.ssrcs.size();
       ++ssrc_index) {
    env_.event_log().Log(std::make_unique<RtcEventVideoSendStreamConfig>(
        CreateRtcLogStreamConfig(config, ssrc_index)));
  }

  // TODO(mflodman): Base the start bitrate on a current bandwidth estimate, if
  // the call has already started.
  // Copy ssrcs from `config` since `config` is moved.
  std::vector<uint32_t> ssrcs = config.rtp.ssrcs;

  VideoSendStreamImpl* send_stream = new VideoSendStreamImpl(
      env_, num_cpu_cores_, call_stats_->AsRtcpRttStats(),
      transport_send_.get(), config_.encode_metronome, bitrate_allocator_.get(),
      video_send_delay_stats_.get(), std::move(config),
      std::move(encoder_config), suspended_video_send_ssrcs_,
      suspended_video_payload_states_, std::move(fec_controller),
      std::move(encoder_switch_request_callback));

  for (uint32_t ssrc : ssrcs) {
    RTC_DCHECK(video_send_ssrcs_.find(ssrc) == video_send_ssrcs_.end());
    video_send_ssrcs_[ssrc] = send_stream;
  }
  video_send_streams_.insert(send_stream);

  // Forward resources that were previously added to the call to the new stream.
  for (const auto& resource_forwarder : adaptation_resource_forwarders_) {
    resource_forwarder->OnCreateVideoSendStream(send_stream);
  }

  UpdateAggregateNetworkState();

  return send_stream;
}

webrtc::VideoSendStream* Call::CreateVideoSendStream(
    webrtc::VideoSendStream::Config config,
    VideoEncoderConfig encoder_config,
    EncoderSwitchRequestCallback encoder_switch_request_callback) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  if (config_.fec_controller_factory) {
    RTC_LOG(LS_INFO) << "External FEC Controller will be used.";
  }
  std::unique_ptr<FecController> fec_controller =
      config_.fec_controller_factory
          ? config_.fec_controller_factory->CreateFecController(env_)
          : std::make_unique<FecControllerDefault>(env_);
  return CreateVideoSendStream(std::move(config), std::move(encoder_config),
                               std::move(encoder_switch_request_callback),
                               std::move(fec_controller));
}

void Call::DestroyVideoSendStream(webrtc::VideoSendStream* send_stream) {
  TRACE_EVENT0("webrtc", "Call::DestroyVideoSendStream");
  RTC_DCHECK(send_stream != nullptr);
  RTC_DCHECK_RUN_ON(worker_thread_);

  VideoSendStreamImpl* send_stream_impl =
      static_cast<VideoSendStreamImpl*>(send_stream);

  auto it = video_send_ssrcs_.begin();
  while (it != video_send_ssrcs_.end()) {
    if (it->second == static_cast<VideoSendStreamImpl*>(send_stream)) {
      send_stream_impl = it->second;
      video_send_ssrcs_.erase(it++);
    } else {
      ++it;
    }
  }

  // Stop forwarding resources to the stream being destroyed.
  for (const auto& resource_forwarder : adaptation_resource_forwarders_) {
    resource_forwarder->OnDestroyVideoSendStream(send_stream_impl);
  }
  video_send_streams_.erase(send_stream_impl);

  VideoSendStreamImpl::RtpStateMap rtp_states;
  VideoSendStreamImpl::RtpPayloadStateMap rtp_payload_states;
  send_stream_impl->StopPermanentlyAndGetRtpStates(&rtp_states,
                                                   &rtp_payload_states);
  for (const auto& kv : rtp_states) {
    suspended_video_send_ssrcs_[kv.first] = kv.second;
  }
  for (const auto& kv : rtp_payload_states) {
    suspended_video_payload_states_[kv.first] = kv.second;
  }

  UpdateAggregateNetworkState();
  // TODO(tommi): consider deleting on the same thread as runs
  // StopPermanentlyAndGetRtpStates.
  delete send_stream_impl;
}

webrtc::VideoReceiveStreamInterface* Call::CreateVideoReceiveStream(
    webrtc::VideoReceiveStreamInterface::Config configuration) {
  TRACE_EVENT0("webrtc", "Call::CreateVideoReceiveStream");
  RTC_DCHECK_RUN_ON(worker_thread_);

  EnsureStarted();

  env_.event_log().Log(std::make_unique<RtcEventVideoReceiveStreamConfig>(
      CreateRtcLogStreamConfig(configuration)));

  TimeDelta render_delay = TimeDelta::Millis(configuration.render_delay_ms);

  // TODO(bugs.webrtc.org/11993): Move the registration between `receive_stream`
  // and `video_receiver_controller_` out of VideoReceiveStream2 construction
  // and set it up asynchronously on the network thread (the registration and
  // `video_receiver_controller_` need to live on the network thread).
  // TODO(crbug.com/1381982): Re-enable decode synchronizer once the Chromium
  // API has adapted to the new Metronome interface.
  VideoReceiveStream2* receive_stream = new VideoReceiveStream2(
      env_, this, num_cpu_cores_, transport_send_->packet_router(),
      std::move(configuration), call_stats_.get(),
      std::make_unique<VCMTiming>(
          env_, render_delay,
          MaybeCreateVideoJitterTiming(env_,
                                       config_.video_jitter_timing_factory)),
      &nack_periodic_processor_, decode_sync_.get());
  // TODO(bugs.webrtc.org/11993): Set this up asynchronously on the network
  // thread.
  receive_stream->RegisterWithTransport(&video_receiver_controller_);
  video_receive_streams_.insert(receive_stream);

  ConfigureSync(receive_stream->sync_group());

  receive_stream->SignalNetworkState(video_network_state_);
  UpdateAggregateNetworkState();
  return receive_stream;
}

void Call::DestroyVideoReceiveStream(
    webrtc::VideoReceiveStreamInterface* receive_stream) {
  TRACE_EVENT0("webrtc", "Call::DestroyVideoReceiveStream");
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DCHECK(receive_stream != nullptr);
  VideoReceiveStream2* receive_stream_impl =
      static_cast<VideoReceiveStream2*>(receive_stream);
  // TODO(bugs.webrtc.org/11993): Unregister on the network thread.
  receive_stream_impl->UnregisterFromTransport();
  video_receive_streams_.erase(receive_stream_impl);
  ConfigureSync(receive_stream_impl->sync_group());

  receive_side_cc_.RemoveStream(receive_stream_impl->remote_ssrc());

  UpdateAggregateNetworkState();
  delete receive_stream_impl;
}

FlexfecReceiveStream* Call::CreateFlexfecReceiveStream(
    const FlexfecReceiveStream::Config config) {
  TRACE_EVENT0("webrtc", "Call::CreateFlexfecReceiveStream");
  RTC_DCHECK_RUN_ON(worker_thread_);

  // Unlike the video and audio receive streams, FlexfecReceiveStream implements
  // RtpPacketSinkInterface itself, and hence its constructor passes its `this`
  // pointer to video_receiver_controller_->CreateStream(). Calling the
  // constructor while on the worker thread ensures that we don't call
  // OnRtpPacket until the constructor is finished and the object is
  // in a valid state, since OnRtpPacket runs on the same thread.
  FlexfecReceiveStreamImpl* receive_stream = new FlexfecReceiveStreamImpl(
      env_, std::move(config), &video_receiver_controller_,
      transport_send_->packet_router(), call_stats_->AsRtcpRttStats());

  // TODO(bugs.webrtc.org/11993): Set this up asynchronously on the network
  // thread.
  receive_stream->RegisterWithTransport(&video_receiver_controller_);
  // TODO(brandtr): Store config in RtcEventLog here.

  return receive_stream;
}

void Call::DestroyFlexfecReceiveStream(FlexfecReceiveStream* receive_stream) {
  TRACE_EVENT0("webrtc", "Call::DestroyFlexfecReceiveStream");
  RTC_DCHECK_RUN_ON(worker_thread_);

  FlexfecReceiveStreamImpl* receive_stream_impl =
      static_cast<FlexfecReceiveStreamImpl*>(receive_stream);
  // TODO(bugs.webrtc.org/11993): Unregister on the network thread.
  receive_stream_impl->UnregisterFromTransport();

  auto ssrc = receive_stream_impl->remote_ssrc();
  // Remove all SSRCs pointing to the FlexfecReceiveStreamImpl to be
  // destroyed.
  receive_side_cc_.RemoveStream(ssrc);

  delete receive_stream_impl;
}

void Call::AddAdaptationResource(scoped_refptr<Resource> resource) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  adaptation_resource_forwarders_.push_back(
      std::make_unique<ResourceVideoSendStreamForwarder>(resource));
  const auto& resource_forwarder = adaptation_resource_forwarders_.back();
  for (VideoSendStream* send_stream : video_send_streams_) {
    resource_forwarder->OnCreateVideoSendStream(send_stream);
  }
}

RtpTransportControllerSendInterface* Call::GetTransportControllerSend() {
  return transport_send_.get();
}

Call::Stats Call::GetStats() const {
  RTC_DCHECK_RUN_ON(worker_thread_);

  Stats stats;
  // TODO(srte): It is unclear if we only want to report queues if network is
  // available.
  stats.pacer_delay_ms =
      aggregate_network_up_ ? transport_send_->GetPacerQueuingDelayMs() : 0;

  stats.rtt_ms = call_stats_->LastProcessedRtt();

  // Fetch available send/receive bitrates.
  stats.recv_bandwidth_bps = receive_side_cc_.LatestReceiveSideEstimate().bps();
  stats.send_bandwidth_bps = last_bandwidth_bps_;
  stats.max_padding_bitrate_bps = configured_max_padding_bitrate_bps_;

  // Congestion control feedback messages received.
  stats.ccfb_messages_received =
      transport_send_->ReceivedCongestionControlFeedbackCount();

  stats.sent_ccfb_stats_per_ssrc =
      receive_side_cc_.GetCongestionControllerStatsPerSsrc();
  stats.received_ccfb_stats_per_ssrc =
      transport_send_->GetCongestionControlFeedbackStatsPerSsrc();

  return stats;
}

void Call::SetPreferredRtcpCcAckType(
    RtcpFeedbackType preferred_rtcp_cc_ack_type) {
  receive_side_cc_.SetPreferredRtcpCcAckType(preferred_rtcp_cc_ack_type);
  transport_send_->SetPreferredRtcpCcAckType(preferred_rtcp_cc_ack_type);
}

std::optional<int> Call::FeedbackAccordingToRfc8888Count() {
  return transport_send_->ReceivedCongestionControlFeedbackCount();
}

std::optional<int> Call::FeedbackAccordingToTransportCcCount() {
  return transport_send_->ReceivedTransportCcFeedbackCount();
}

void Call::DisconnectFromNetworkThread() {
  RTC_DCHECK_RUN_ON(network_thread_);
  audio_receiver_controller_.DisconnectFromNetworkThread();
  video_receiver_controller_.DisconnectFromNetworkThread();
}

TaskQueueBase* Call::network_thread() const {
  return network_thread_;
}

TaskQueueBase* Call::worker_thread() const {
  return worker_thread_;
}

void Call::SignalChannelNetworkState(MediaType media, NetworkState state) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK(media == MediaType::AUDIO || media == MediaType::VIDEO);

  auto closure = [this, media, state]() {
    // TODO(bugs.webrtc.org/11993): Move this over to the network thread.
    RTC_DCHECK_RUN_ON(worker_thread_);
    if (media == MediaType::AUDIO) {
      audio_network_state_ = state;
    } else {
      RTC_DCHECK_EQ(media, MediaType::VIDEO);
      video_network_state_ = state;
    }

    // TODO(tommi): Is it necessary to always do this, including if there
    // was no change in state?
    UpdateAggregateNetworkState();

    // TODO(tommi): Is it right to do this if media == AUDIO?
    for (VideoReceiveStream2* video_receive_stream : video_receive_streams_) {
      video_receive_stream->SignalNetworkState(video_network_state_);
    }
  };

  if (network_thread_ == worker_thread_) {
    closure();
  } else {
    // TODO(bugs.webrtc.org/11993): Remove workaround when we no longer need to
    // post to the worker thread.
    worker_thread_->PostTask(SafeTask(task_safety_.flag(), std::move(closure)));
  }
}

void Call::UpdateAggregateNetworkState() {
  // TODO(bugs.webrtc.org/11993): Move this over to the network thread.
  // RTC_DCHECK_RUN_ON(network_thread_);

  RTC_DCHECK_RUN_ON(worker_thread_);

  bool have_audio =
      !audio_send_ssrcs_.empty() || !audio_receive_streams_.empty();
  bool have_video =
      !video_send_ssrcs_.empty() || !video_receive_streams_.empty();

  bool aggregate_network_up =
      ((have_video && video_network_state_ == kNetworkUp) ||
       (have_audio && audio_network_state_ == kNetworkUp));

  if (aggregate_network_up != aggregate_network_up_) {
    RTC_LOG(LS_INFO)
        << "UpdateAggregateNetworkState: aggregate_state change to "
        << (aggregate_network_up ? "up" : "down");
  } else {
    RTC_LOG(LS_VERBOSE)
        << "UpdateAggregateNetworkState: aggregate_state remains at "
        << (aggregate_network_up ? "up" : "down");
  }
  aggregate_network_up_ = aggregate_network_up;

  transport_send_->OnNetworkAvailability(aggregate_network_up);
}

void Call::OnUpdateSyncGroup(webrtc::AudioReceiveStreamInterface& stream,
                             absl::string_view sync_group) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  webrtc::AudioReceiveStreamImpl& receive_stream =
      static_cast<webrtc::AudioReceiveStreamImpl&>(stream);
  std::string old_sync_group(receive_stream.sync_group());
  receive_stream.SetSyncGroup(sync_group);
  if (old_sync_group != sync_group) {
    ConfigureSync(old_sync_group);
  }
  ConfigureSync(sync_group);
}

void Call::OnSentPacket(const SentPacketInfo& sent_packet) {
  RTC_DCHECK_RUN_ON(network_thread_);
  // When bundling is in effect, multiple senders may be sharing the same
  // transport. It means every |sent_packet| will be multiply notified from
  // different channels, WebRtcVoiceMediaChannel or WebRtcVideoChannel. Record
  // |last_sent_packet_| to deduplicate redundant notifications to downstream.
  // (https://crbug.com/webrtc/13437): Pass all packets without a |packet_id| to
  // downstream.
  if (last_sent_packet_.has_value() && last_sent_packet_->packet_id != -1 &&
      last_sent_packet_->packet_id == sent_packet.packet_id &&
      last_sent_packet_->send_time_ms == sent_packet.send_time_ms) {
    return;
  }
  last_sent_packet_ = sent_packet;

  Timestamp sent_time = env_.clock().CurrentTime();
  if (!worker_thread_->IsCurrent()) {
    worker_thread_->PostTask(
        SafeTask(task_safety_.flag(), [this, sent_packet, sent_time]() {
          OnSentPacket_w(sent_packet, sent_time);
        }));
  } else {
    OnSentPacket_w(sent_packet, sent_time);
  }
}

void Call::OnSentPacket_w(const SentPacketInfo& sent_packet,
                          Timestamp sent_time) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  video_send_delay_stats_->OnSentPacket(sent_packet.packet_id, sent_time);
  transport_send_->OnSentPacket(sent_packet);
}

void Call::OnStartRateUpdate(DataRate start_rate) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  bitrate_allocator_->UpdateStartRate(start_rate.bps<uint32_t>());
}

void Call::OnTransportOverheadChanged(DataSize transport_overhead) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  bitrate_allocator_->OnTransportOverheadChanged(transport_overhead);
}

void Call::OnTargetTransferRate(TargetTransferRate msg) {
  RTC_DCHECK_RUN_ON(worker_thread_);

  last_bandwidth_bps_ = msg.target_rate.bps();
  // For controlling the rate of feedback messages.
  receive_side_cc_.OnBitrateChanged(msg.target_rate, msg.is_bandwidth_limited,
                                    transport_send_->GetTransportOverhead());
  bitrate_allocator_->OnNetworkEstimateChanged(msg);

  // Ignore updates if bitrate is zero (the aggregate network state is
  // down) or if we're not sending video.
  if (last_bandwidth_bps_ == 0 || video_send_streams_.empty()) {
    send_stats_.PauseSendAndPacerBitrateCounters();
  } else {
    send_stats_.AddTargetBitrateSample(last_bandwidth_bps_);
  }
}

void Call::OnAllocationLimitsChanged(BitrateAllocationLimits limits) {
  RTC_DCHECK_RUN_ON(worker_thread_);

  transport_send_->SetAllocatedSendBitrateLimits(limits);
  send_stats_.SetMinAllocatableRate(limits);
  configured_max_padding_bitrate_bps_ = limits.max_padding_rate.bps();
}

AudioReceiveStreamImpl* Call::FindAudioStreamForSyncGroup(
    absl::string_view sync_group) {
  RTC_DCHECK_RUN_ON(worker_thread_);
  RTC_DCHECK_RUN_ON(&receive_11993_checker_);
  if (!sync_group.empty()) {
    for (AudioReceiveStreamImpl* stream : audio_receive_streams_) {
      if (stream->sync_group() == sync_group)
        return stream;
    }
  }

  return nullptr;
}

void Call::ConfigureSync(absl::string_view sync_group) {
  // TODO(bugs.webrtc.org/11993): Expect to be called on the network thread.
  RTC_DCHECK_RUN_ON(worker_thread_);
  // `audio_stream` may be nullptr when clearing the audio stream for a group.
  AudioReceiveStreamImpl* audio_stream =
      FindAudioStreamForSyncGroup(sync_group);

  size_t num_synced_streams = 0;
  for (VideoReceiveStream2* video_stream : video_receive_streams_) {
    if (video_stream->sync_group() != sync_group)
      continue;
    ++num_synced_streams;
    // TODO(bugs.webrtc.org/4762): Support synchronizing more than one A/V pair.
    // Attempting to sync more than one audio/video pair within the same sync
    // group is not supported in the current implementation.
    // Only sync the first A/V pair within this sync group.
    if (num_synced_streams == 1) {
      // sync_audio_stream may be null and that's ok.
      video_stream->SetSync(audio_stream);
    } else {
      video_stream->SetSync(nullptr);
    }
  }
}

void Call::DeliverRtcpPacket(CopyOnWriteBuffer packet) {
  TRACE_EVENT0("webrtc", "Call::DeliverRtcp");
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK_RUN_ON(worker_thread_);  // Same queue, see Call::Create().
  RTC_DCHECK(IsRtcpPacket(packet));
  receive_stats_.AddReceivedRtcpBytes(static_cast<int>(packet.size()));

  bool rtcp_delivered = false;
  std::span<const uint8_t> packet_view(packet.cdata(), packet.size());
  for (VideoReceiveStream2* stream : video_receive_streams_) {
    if (stream->DeliverRtcp(packet_view))
      rtcp_delivered = true;
  }

  for (AudioReceiveStreamImpl* stream : audio_receive_streams_) {
    stream->DeliverRtcp(packet_view);
    rtcp_delivered = true;
  }

  for (VideoSendStreamImpl* stream : video_send_streams_) {
    stream->DeliverRtcp(packet);
    rtcp_delivered = true;
  }

  for (auto& kv : audio_send_ssrcs_) {
    kv.second->DeliverRtcp(packet_view);
    rtcp_delivered = true;
  }

  if (rtcp_delivered) {
    env_.event_log().Log(std::make_unique<RtcEventRtcpPacketIncoming>(packet));
  }
}

void Call::DeliverRtpPacket(MediaType media_type,
                            RtpPacketReceived packet,
                            absl_nonnull OnUndemuxablePacketHandler
                                undemuxable_packet_handler) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK_RUN_ON(worker_thread_);  // Same queue, see Call::Create().
  RTC_DCHECK(packet.arrival_time().IsFinite());

  int length = static_cast<int>(packet.size());
  if (media_type == MediaType::AUDIO) {
    receive_stats_.AddReceivedAudioBytes(length, packet.arrival_time());
  }
  if (media_type == MediaType::VIDEO) {
    receive_stats_.AddReceivedVideoBytes(length, packet.arrival_time());
  }

  NotifyBweOfReceivedPacket(packet, media_type);

  // Packets that are successfully demuxed are logged by their respective
  // streams. Packets that fail to demux are logged here.
  if (media_type != MediaType::AUDIO && media_type != MediaType::VIDEO) {
    env_.event_log().Log(std::make_unique<RtcEventRtpPacketIncoming>(packet));
    return;
  }

  RtpStreamReceiverController& receiver_controller =
      media_type == MediaType::AUDIO ? audio_receiver_controller_
                                     : video_receiver_controller_;
  RtpPacketSinkInterface* sink = receiver_controller.ResolveSink(packet);

  const TimeDelta nw_to_deliver_delay =
      env_.clock().CurrentTime() - packet.arrival_time();
  RTC_HISTOGRAM_COUNTS_100000("WebRTC.TimeFromNetworkToDeliverRtpPacketUs",
                              nw_to_deliver_delay.us());

  if (receive_sink_registry_.IsValidSink(sink)) {
    sink->OnRtpPacket(packet);
    return;
  }

  if (!undemuxable_packet_handler(packet)) {
    env_.event_log().Log(std::make_unique<RtcEventRtpPacketIncoming>(packet));
    return;
  }

  RtpPacketSinkInterface* retry_sink = receiver_controller.ResolveSink(packet);
  if (receive_sink_registry_.IsValidSink(retry_sink)) {
    retry_sink->OnRtpPacket(packet);
    return;
  }
  env_.event_log().Log(std::make_unique<RtcEventRtpPacketIncoming>(packet));
  RTC_LOG(LS_INFO) << "Failed to demux packet " << packet.Ssrc();
}

void Call::NotifyBweOfReceivedPacket(const RtpPacketReceived& packet,
                                     MediaType media_type) {
  RTC_DCHECK_RUN_ON(worker_thread_);

  ReceivedPacket packet_msg;
  packet_msg.size = DataSize::Bytes(packet.payload_size());
  packet_msg.receive_time = packet.arrival_time();
  uint32_t time_24;
  if (packet.GetExtension<AbsoluteSendTime>(&time_24)) {
    packet_msg.send_time = AbsoluteSendTime::ToTimestamp(time_24);
  }
  transport_send_->OnReceivedPacket(packet_msg);

  receive_side_cc_.OnReceivedPacket(packet, media_type);
}

}  // namespace internal

}  // namespace webrtc
