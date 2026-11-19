/*
 *  Copyright 2016 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_RTC_STATS_COLLECTOR_H_
#define PC_RTC_STATS_COLLECTOR_H_

#include <stdint.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "api/audio/audio_device.h"
#include "api/data_channel_interface.h"
#include "api/environment/environment.h"
#include "api/media_types.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_direction.h"
#include "api/scoped_refptr.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/timestamp.h"
#include "call/call.h"
#include "media/base/media_channel.h"
#include "pc/data_channel_utils.h"
#include "pc/peer_connection_internal.h"
#include "pc/rtp_receiver.h"
#include "pc/rtp_sender.h"
#include "pc/rtp_transceiver.h"
#include "pc/track_media_info_map.h"
#include "pc/transport_stats.h"
#include "rtc_base/checks.h"
#include "rtc_base/containers/flat_set.h"
#include "rtc_base/ssl_certificate.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/time_utils.h"

namespace webrtc {

class RtpSenderInternal;
class RtpReceiverInternal;
class ScopedOperationsBatcher;

// Structure for tracking stats about each RtpTransceiver managed by the
// PeerConnection. This can either by a Plan B style or Unified Plan style
// transceiver (i.e., can have 0 or many senders and receivers).
// Some fields are copied from the RtpTransceiver/BaseChannel object so that
// they can be accessed safely on threads other than the signaling thread.
// If a BaseChannel is not available (e.g., if signaling has not started),
// then `mid` and `transport_name` will be null.
struct RtpTransceiverStatsInfo {
  const MediaType media_type;
  const std::optional<std::string> mid;
  std::optional<std::string> transport_name;
  std::vector<TrackMediaInfoMap::RtpSenderSignalInfo> sender_infos;
  std::vector<TrackMediaInfoMap::RtpReceiverSignalInfo> receiver_infos;
  std::unique_ptr<TrackMediaInfoMap> track_media_info_map;
  const std::optional<RtpTransceiverDirection> current_direction;
  bool has_receivers = false;
  const bool has_channel;
};

// References to objects used on the signaling and worker threads for populating
// RtpTransceiverStatsInfo but must always be released on the signaling thread
struct TransceiverReferences {
  scoped_refptr<RtpTransceiver> transceiver;
  std::vector<scoped_refptr<RtpReceiverInternal>> receivers;
  absl::AnyInvocable<std::optional<VoiceMediaSendInfo>()> get_send_stats_voice;
  absl::AnyInvocable<std::optional<VideoMediaSendInfo>()> get_send_stats_video;
  absl::AnyInvocable<std::optional<VoiceMediaReceiveInfo>()>
      get_receive_stats_voice;
  absl::AnyInvocable<std::optional<VideoMediaReceiveInfo>()>
      get_receive_stats_video;
  absl::AnyInvocable<RtpParameters(uint32_t ssrc)> get_send_parameters;
};

// All public methods of the collector are to be called on the signaling thread.
// Stats are gathered on the signaling, worker and network threads
// asynchronously. The callback is invoked on the signaling thread. Resulting
// reports are cached for `cache_lifetime_` ms.
class RTCStatsCollector {
 public:
  // Gets a recent stats report. If there is a report cached that is still fresh
  // it is returned, otherwise new stats are gathered and returned. A report is
  // considered fresh for `cache_lifetime_` ms. const RTCStatsReports are safe
  // to use across multiple threads and may be destructed on any thread.
  // If the optional selector argument is used, stats are filtered according to
  // stats selection algorithm before delivery.
  // https://w3c.github.io/webrtc-pc/#dfn-stats-selection-algorithm
  void GetStatsReport(scoped_refptr<RTCStatsCollectorCallback> callback);
  // If `selector` is null the selection algorithm is still applied (interpreted
  // as: no RTP streams are sent by selector). The result is empty.
  void GetStatsReport(scoped_refptr<RtpSenderInternal> selector,
                      scoped_refptr<RTCStatsCollectorCallback> callback);
  // If `selector` is null the selection algorithm is still applied (interpreted
  // as: no RTP streams are received by selector). The result is empty.
  void GetStatsReport(scoped_refptr<RtpReceiverInternal> selector,
                      scoped_refptr<RTCStatsCollectorCallback> callback);
  // Clears the cache's reference to the most recent stats report. Subsequently
  // calling `GetStatsReport` guarantees fresh stats. This method must be called
  // any time the PeerConnection visibly changes as a result of an API call as
  // per
  // https://w3c.github.io/webrtc-stats/#guidelines-for-getstats-results-caching-throttling
  // and it must be called any time negotiation happens.
  void ClearCachedStatsReport();

  // Cancels pending stats gathering operations and prepares for shutdown.
  // This method adds tasks that the caller needs to make sure is executed
  // on the worker and network threads before the RTCStatsCollector instance is
  // deleted.
  void CancelPendingRequestAndGetShutdownTasks(
      ScopedOperationsBatcher& network_tasks,
      ScopedOperationsBatcher& worker_tasks);

  // Called by the PeerConnection instance when data channel states change.
  void OnSctpDataChannelStateChanged(int channel_id,
                                     DataChannelInterface::DataState state);

  virtual ~RTCStatsCollector();

  RTCStatsCollector(PeerConnectionInternal* pc,
                    const Environment& env,
                    int64_t cache_lifetime_us = 50 * kNumMicrosecsPerMillisec);

 protected:
  struct CertificateStatsPair {
    std::unique_ptr<SSLCertificateStats> local;
    std::unique_ptr<SSLCertificateStats> remote;

    CertificateStatsPair Copy() const;
  };

  // Stats gathering on a particular thread. Virtual for the sake of testing.
  virtual void ProducePartialResultsOnSignalingThreadImpl(
      Timestamp timestamp,
      const std::vector<RtpTransceiverStatsInfo>& transceiver_stats_infos,
      const std::vector<TransceiverReferences>& transceiver_references,
      const std::optional<AudioDeviceModule::Stats>& audio_device_stats,
      RTCStatsReport* partial_report);

  void ProcessResultsFromNetworkThread(
      Timestamp timestamp,
      std::map<std::string, TransportStats> transport_stats_by_name,
      std::map<std::string, CertificateStatsPair> transport_cert_stats,
      std::vector<RtpTransceiverStatsInfo> transceiver_stats_infos,
      Call::Stats call_stats,
      std::optional<AudioDeviceModule::Stats> audio_device_stats,
      RTCStatsReport* partial_report);

 private:
  struct StatsGatheringResults {
    std::vector<RtpTransceiverStatsInfo> transceiver_stats_infos;
    Call::Stats call_stats;
    std::optional<AudioDeviceModule::Stats> audio_device_stats;
  };

  struct WorkerThreadResult {
    StatsGatheringResults results;
    std::vector<std::vector<RtpParameters>> sender_parameters;
    std::vector<TransceiverReferences> transceiver_references;
  };
  struct CollectionContext;
  class RequestInfo {
   public:
    enum class FilterMode { kAll, kSenderSelector, kReceiverSelector };

    // Constructs with FilterMode::kAll.
    explicit RequestInfo(scoped_refptr<RTCStatsCollectorCallback> callback);
    // Constructs with FilterMode::kSenderSelector. The selection algorithm is
    // applied even if `selector` is null, resulting in an empty report.
    RequestInfo(scoped_refptr<RtpSenderInternal> selector,
                scoped_refptr<RTCStatsCollectorCallback> callback);
    // Constructs with FilterMode::kReceiverSelector. The selection algorithm is
    // applied even if `selector` is null, resulting in an empty report.
    RequestInfo(scoped_refptr<RtpReceiverInternal> selector,
                scoped_refptr<RTCStatsCollectorCallback> callback);

    FilterMode filter_mode() const { return filter_mode_; }
    scoped_refptr<RTCStatsCollectorCallback> callback() const {
      return callback_;
    }
    scoped_refptr<RtpSenderInternal> sender_selector() const {
      RTC_DCHECK(filter_mode_ == FilterMode::kSenderSelector);
      return sender_selector_;
    }
    scoped_refptr<RtpReceiverInternal> receiver_selector() const {
      RTC_DCHECK(filter_mode_ == FilterMode::kReceiverSelector);
      return receiver_selector_;
    }

   private:
    RequestInfo(FilterMode filter_mode,
                scoped_refptr<RTCStatsCollectorCallback> callback,
                scoped_refptr<RtpSenderInternal> sender_selector,
                scoped_refptr<RtpReceiverInternal> receiver_selector);

    FilterMode filter_mode_;
    scoped_refptr<RTCStatsCollectorCallback> callback_;
    scoped_refptr<RtpSenderInternal> sender_selector_;
    scoped_refptr<RtpReceiverInternal> receiver_selector_;
  };

  void GetStatsReportInternal(RequestInfo request);

  // Invokes the completion callback for a pending request.
  static void DeliverReport(const RequestInfo& request,
                            const scoped_refptr<const RTCStatsReport>& report);

  // Produces `RTCCertificateStats`.
  void ProduceCertificateStats_s(
      Timestamp timestamp,
      const std::map<std::string, CertificateStatsPair>& transport_cert_stats,
      RTCStatsReport* report) const;
  // Produces `RTCDataChannelStats`.
  void ProduceDataChannelStats_s(
      Timestamp timestamp,
      const std::vector<DataChannelStats>& data_channel_stats,
      RTCStatsReport* report) const;
  // Produces `RTCIceCandidatePairStats` and `RTCIceCandidateStats`.
  void ProduceIceCandidateAndPairStats_s(
      Timestamp timestamp,
      const std::map<std::string, TransportStats>& transport_stats_by_name,
      const Call::Stats& call_stats,
      RTCStatsReport* report) const;
  // Produces RTCMediaSourceStats, including RTCAudioSourceStats and
  // RTCVideoSourceStats.
  void ProduceMediaSourceStats_s(
      Timestamp timestamp,
      const std::vector<RtpTransceiverStatsInfo>& transceiver_stats_infos,
      const std::vector<TransceiverReferences>& transceiver_references,
      RTCStatsReport* report) const;
  // Produces `RTCPeerConnectionStats`.
  void ProducePeerConnectionStats_s(Timestamp timestamp,
                                    RTCStatsReport* report) const;
  // Produces `RTCAudioPlayoutStats`.
  void ProduceAudioPlayoutStats_s(
      Timestamp timestamp,
      const std::optional<AudioDeviceModule::Stats>& audio_device_stats,
      RTCStatsReport* report) const;
  // Produces `RTCInboundRtpStreamStats`, `RTCOutboundRtpStreamStats`,
  // `RTCRemoteInboundRtpStreamStats`, `RTCRemoteOutboundRtpStreamStats` and any
  // referenced `RTCCodecStats`. This has to be invoked after transport stats
  // have been created because some metrics are calculated through lookup of
  // other metrics.
  void ProduceRTPStreamStats_s(
      Timestamp timestamp,
      const std::vector<RtpTransceiverStatsInfo>& transceiver_stats_infos,
      const Call::Stats& call_stats,
      const std::optional<AudioDeviceModule::Stats>& audio_device_stats,
      RTCStatsReport* report) const;
  void ProduceAudioRTPStreamStats_s(
      Timestamp timestamp,
      const RtpTransceiverStatsInfo& stats,
      const Call::Stats& call_stats,
      const std::optional<AudioDeviceModule::Stats>& audio_device_stats,
      RTCStatsReport* report) const;
  void ProduceVideoRTPStreamStats_s(Timestamp timestamp,
                                    const RtpTransceiverStatsInfo& stats,
                                    const Call::Stats& call_stats,
                                    RTCStatsReport* report) const;
  // Produces `RTCTransportStats`.
  void ProduceTransportStats_s(
      Timestamp timestamp,
      const std::map<std::string, TransportStats>& transport_stats_by_name,
      const std::map<std::string, CertificateStatsPair>& transport_cert_stats,
      const Call::Stats& call_stats,
      RTCStatsReport* report) const;

  // Helper function to stats-producing functions.
  std::map<std::string, CertificateStatsPair>
  PrepareTransportCertificateStats_n(
      const std::map<std::string, TransportStats>& transport_stats_by_name);
  // The results are stored in `transceiver_stats_infos_` and `call_stats_`.
  // Prepares the transceiver stats infos and call stats.
  // Returns a callback that should be executed on the worker thread to populate
  // the stats.
  absl::AnyInvocable<WorkerThreadResult()>
  PrepareTransceiverStatsInfosAndCallStats_s_w();

  // Stats gathering on a particular thread.
  void ProducePartialResultsOnSignalingThread(
      const std::vector<RtpTransceiverStatsInfo>& transceiver_stats_infos,
      const std::vector<TransceiverReferences>& transceiver_references,
      const std::optional<AudioDeviceModule::Stats>& audio_device_stats);
  void ProducePartialResultsOnNetworkThread(
      scoped_refptr<PendingTaskSafetyFlag> signaling_safety,
      Timestamp timestamp,
      std::set<std::string> transport_names,
      StatsGatheringResults results);
  // Merges `network_report` into `partial_report_` and completes the request.
  void OnNetworkReportReady(scoped_refptr<RTCStatsReport> network_report,
                            std::vector<DataChannelStats> data_channel_stats);

  static scoped_refptr<RTCStatsReport> CreateReportFilteredBySelector(
      bool filter_by_sender_selector,
      scoped_refptr<const RTCStatsReport> report,
      scoped_refptr<RtpSenderInternal> sender_selector,
      scoped_refptr<RtpReceiverInternal> receiver_selector);

  PeerConnectionInternal* const pc_;
  const bool is_unified_plan_;
  const Environment env_;
  const bool stats_timestamp_with_environment_clock_;
  TaskQueueBase* const signaling_thread_;
  Thread* const worker_thread_;
  Thread* const network_thread_;

  std::vector<RequestInfo> requests_ RTC_GUARDED_BY(signaling_thread_);

  // This cache avoids having to call webrtc::SSLCertChain::GetStats(), which
  // can relatively expensive. ClearCachedStatsReport() needs to be called on
  // negotiation to ensure the cache is not obsolete.
  std::map<std::string, CertificateStatsPair> cached_certificates_by_transport_
      RTC_GUARDED_BY(network_thread_);

  // A timestamp, in microseconds, that is based on a timer that is
  // monotonically increasing. That is, even if the system clock is modified the
  // difference between the timer and this timestamp is how fresh the cached
  // report is.
  int64_t cache_timestamp_us_;
  int64_t cache_lifetime_us_;
  scoped_refptr<const RTCStatsReport> cached_report_
      RTC_GUARDED_BY(signaling_thread_);

  // Data recorded and maintained by the stats collector during its lifetime.
  // Some stats are produced from this record instead of other components.
  struct InternalRecord {
    InternalRecord() : data_channels_opened(0), data_channels_closed(0) {}

    // The opened count goes up when a channel is fully opened and the closed
    // count goes up if a previously opened channel has fully closed. The opened
    // count does not go down when a channel closes, meaning (opened - closed)
    // is the number of channels currently opened. A channel that is closed
    // before reaching the open state does not affect these counters.
    uint32_t data_channels_opened;
    uint32_t data_channels_closed;
    // Identifies channels that have been opened, whose internal id is stored in
    // the set until they have been fully closed.
    flat_set<int> opened_data_channels;
  };
  InternalRecord internal_record_;
  const scoped_refptr<PendingTaskSafetyFlag> signaling_safety_;
  const scoped_refptr<PendingTaskSafetyFlag> worker_safety_;
  const scoped_refptr<PendingTaskSafetyFlag> network_safety_;

  std::unique_ptr<CollectionContext> collection_context_
      RTC_GUARDED_BY(signaling_thread_);
};

}  // namespace webrtc

#endif  // PC_RTC_STATS_COLLECTOR_H_
