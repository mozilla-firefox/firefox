/*
 *  Copyright 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_TEST_INTEGRATION_TEST_HELPERS_H_
#define PC_TEST_INTEGRATION_TEST_HELPERS_H_

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/data_channel_interface.h"
#include "api/dtls_transport_interface.h"
#include "api/environment/environment.h"
#include "api/ice_transport_interface.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/metronome/metronome.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtc_event_log_output.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/test/mock_async_dns_resolver.h"
#include "api/test/time_controller.h"
#include "api/units/time_delta.h"
#include "api/video/video_rotation.h"
#include "logging/rtc_event_log/fake_rtc_event_log_factory.h"
#include "media/base/stream_params.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "p2p/test/fake_ice_transport.h"
#include "p2p/test/test_stun_server.h"
#include "p2p/test/test_turn_customizer.h"
#include "p2p/test/test_turn_server.h"
#include "pc/peer_connection.h"
#include "pc/peer_connection_factory.h"
#include "pc/session_description.h"
#include "pc/test/fake_audio_capture_module.h"
#include "pc/test/fake_periodic_video_source.h"
#include "pc/test/fake_video_track_renderer.h"
#include "pc/test/mock_peer_connection_observers.h"
#include "pc/video_track_source.h"
#include "rtc_base/fake_mdns_responder.h"
#include "rtc_base/fake_network.h"
#include "rtc_base/firewall_socket_server.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/socket_server.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/system/plan_b_only.h"
#include "rtc_base/thread.h"
#include "rtc_base/virtual_socket_server.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"
#include "test/time_controller/simulated_time_controller.h"
#include "test/wait_until.h"

namespace webrtc {

namespace internal {
class PeerConnectionIntegrationTestBase;
}  // namespace internal

using ::testing::_;
using ::testing::Combine;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::UnorderedElementsAreArray;
using ::testing::Values;
using RTCConfiguration = PeerConnectionInterface::RTCConfiguration;

constexpr TimeDelta kDefaultTimeout = TimeDelta::Millis(10000);
constexpr TimeDelta kLongTimeout = TimeDelta::Millis(60000);
constexpr TimeDelta kMaxWaitForStats = TimeDelta::Millis(3000);
constexpr TimeDelta kMaxWaitForActivation = TimeDelta::Millis(5000);
constexpr TimeDelta kMaxWaitForFrames = TimeDelta::Millis(10000);
// Default number of audio/video frames to wait for before considering a test
// successful.
static const int kDefaultExpectedAudioFrameCount = 3;
static const int kDefaultExpectedVideoFrameCount = 3;

static const char kDataChannelLabel[] = "data_channel";

// SRTP cipher name negotiated by the tests. This must be updated if the
// default changes.
static const int kDefaultSrtpCryptoSuite = kSrtpAes128CmSha1_80;
static const int kDefaultSrtpCryptoSuiteGcm = kSrtpAeadAes256Gcm;

static const SocketAddress kDefaultLocalAddress("192.168.1.1", 0);

// Helper function for constructing offer/answer options to initiate an ICE
// restart.
PeerConnectionInterface::RTCOfferAnswerOptions IceRestartOfferAnswerOptions();

// Remove all stream information (SSRCs, track IDs, etc.) and "msid-semantic"
// attribute from received SDP, simulating a legacy endpoint.
void RemoveSsrcsAndMsids(std::unique_ptr<SessionDescriptionInterface>& desc);

// Removes all stream information besides the stream ids, simulating an
// endpoint that only signals a=msid lines to convey stream_ids.
void RemoveSsrcsAndKeepMsids(
    std::unique_ptr<SessionDescriptionInterface>& desc);

// Set SdpType.
void SetSdpType(std::unique_ptr<SessionDescriptionInterface>& sdp,
                SdpType sdpType);

// Replaces the stream's primary SSRC and updates the first SSRC of all
// ssrc-groups.
void ReplaceFirstSsrc(StreamParams& stream, uint32_t ssrc);

int FindFirstMediaStatsIndexByKind(
    const std::string& kind,
    const std::vector<const RTCInboundRtpStreamStats*>& inbound_rtps);

// Tests whether a session description contains conflicting descriptions
// for a payload type within a bundle.
RTCError ValidateBundledPayloadTypes(const SessionDescription& description);

class TaskQueueMetronome : public Metronome {
 public:
  explicit TaskQueueMetronome(TimeDelta tick_period);
  ~TaskQueueMetronome() override;

  // Metronome implementation.
  void RequestCallOnNextTick(absl::AnyInvocable<void() &&> callback) override;
  TimeDelta TickPeriod() const override;

 private:
  const TimeDelta tick_period_;
  SequenceChecker sequence_checker_{SequenceChecker::kDetached};
  std::vector<absl::AnyInvocable<void() &&>> callbacks_;
  ScopedTaskSafetyDetached safety_;
};

class SignalingMessageReceiver {
 public:
  virtual void ReceiveSdpMessage(SdpType type, const std::string& msg) = 0;
  virtual void ReceiveIceMessage(const std::string& sdp_mid,
                                 int sdp_mline_index,
                                 const std::string& msg) = 0;

 protected:
  SignalingMessageReceiver() {}
  virtual ~SignalingMessageReceiver() {}
};

class MockRtpReceiverObserver : public RtpReceiverObserverInterface {
 public:
  explicit MockRtpReceiverObserver(webrtc::MediaType media_type)
      : expected_media_type_(media_type) {}

  void OnFirstPacketReceived(webrtc::MediaType media_type) override {
    ASSERT_EQ(expected_media_type_, media_type);
    first_packet_received_ = true;
  }
  void OnFirstPacketReceivedAfterReceptiveChange(
      webrtc::MediaType media_type) override {
    ASSERT_EQ(expected_media_type_, media_type);
    first_packet_received_after_receptive_change_ = true;
  }
  void OnSourceChanged(bool ssrc_changed, bool csrc_changed) override {
    source_changed_ = true;
    last_ssrc_changed_ = ssrc_changed;
    last_csrc_changed_ = csrc_changed;
  }

  bool first_packet_received() const { return first_packet_received_; }
  bool first_packet_received_after_receptive_change() const {
    return first_packet_received_after_receptive_change_;
  }
  bool source_changed() const { return source_changed_; }
  bool last_ssrc_changed() const { return last_ssrc_changed_; }
  bool last_csrc_changed() const { return last_csrc_changed_; }

  ~MockRtpReceiverObserver() override {}

 private:
  bool first_packet_received_ = false;
  bool first_packet_received_after_receptive_change_ = false;
  bool source_changed_ = false;
  bool last_ssrc_changed_ = false;
  bool last_csrc_changed_ = false;
  webrtc::MediaType expected_media_type_;
};

class MockRtpSenderObserver : public RtpSenderObserverInterface {
 public:
  explicit MockRtpSenderObserver(webrtc::MediaType media_type)
      : expected_media_type_(media_type) {}

  void OnFirstPacketSent(webrtc::MediaType media_type) override {
    ASSERT_EQ(expected_media_type_, media_type);
    first_packet_sent_ = true;
  }

  bool first_packet_sent() const { return first_packet_sent_; }

  ~MockRtpSenderObserver() override {}

 private:
  bool first_packet_sent_ = false;
  webrtc::MediaType expected_media_type_;
};

// Helper class that wraps a peer connection, observes it, and can accept
// signaling messages from another wrapper.
//
// Uses a fake network, fake A/V capture, and optionally fake
// encoders/decoders, though they aren't used by default since they don't
// advertise support of any codecs.
// TODO(steveanton): See how this could become a subclass of
// PeerConnectionWrapper defined in peerconnectionwrapper.h.
class PeerConnectionIntegrationWrapper : public PeerConnectionObserver,
                                         public SignalingMessageReceiver {
 public:
  explicit PeerConnectionIntegrationWrapper(
      const std::string& debug_name,
      Environment env,
      internal::PeerConnectionIntegrationTestBase* test);

  PeerConnectionFactoryInterface* pc_factory() const {
    return peer_connection_factory_.get();
  }

  PeerConnectionInterface* pc() const { return peer_connection_.get(); }

  // Return the PC implementation, so that non-public interfaces
  // can be used in tests.
  PeerConnection* pc_internal() const;

  // If a signaling message receiver is set (via ConnectFakeSignaling), this
  // will set the whole offer/answer exchange in motion. Just need to wait for
  // the signaling state to reach "stable".
  void CreateAndSetAndSignalOffer();

  // Sets the options to be used when CreateAndSetAndSignalOffer is called, or
  // when a remote offer is received (via fake signaling) and an answer is
  // generated. By default, uses default options.
  void SetOfferAnswerOptions(
      const PeerConnectionInterface::RTCOfferAnswerOptions& options) {
    offer_answer_options_ = options;
  }

  // Set a callback to be invoked when SDP is received via the fake signaling
  // channel, which provides an opportunity to munge (modify) the SDP. This is
  // used to test SDP being applied that a PeerConnection would normally not
  // generate, but a non-JSEP endpoint might.
  void SetReceivedSdpMunger(
      std::function<void(std::unique_ptr<SessionDescriptionInterface>&)>
          munger) {
    received_sdp_munger_ = std::move(munger);
  }

  // Similar to the above, but this is run on SDP immediately after it's
  // generated.
  void SetGeneratedSdpMunger(
      std::function<void(std::unique_ptr<SessionDescriptionInterface>&)>
          munger) {
    generated_sdp_munger_ = std::move(munger);
  }

  // Set a callback to be invoked when a remote offer is received via the fake
  // signaling channel. This provides an opportunity to change the
  // PeerConnection state before an answer is created and sent to the caller.
  void SetRemoteOfferHandler(std::function<void()> handler) {
    remote_offer_handler_ = std::move(handler);
  }

  void SetRemoteAsyncResolver(MockAsyncDnsResolver* resolver) {
    remote_async_dns_resolver_ = resolver;
  }

  // Every ICE connection state in order that has been seen by the observer.
  std::vector<PeerConnectionInterface::IceConnectionState>
  ice_connection_state_history() const {
    return ice_connection_state_history_;
  }
  void clear_ice_connection_state_history() {
    ice_connection_state_history_.clear();
  }

  // Every standardized ICE connection state in order that has been seen by the
  // observer.
  std::vector<PeerConnectionInterface::IceConnectionState>
  standardized_ice_connection_state_history() const {
    return standardized_ice_connection_state_history_;
  }

  // Every PeerConnection state in order that has been seen by the observer.
  std::vector<PeerConnectionInterface::PeerConnectionState>
  peer_connection_state_history() const {
    return peer_connection_state_history_;
  }

  // Every ICE gathering state in order that has been seen by the observer.
  std::vector<PeerConnectionInterface::IceGatheringState>
  ice_gathering_state_history() const {
    return ice_gathering_state_history_;
  }
  std::vector<CandidatePairChangeEvent> ice_candidate_pair_change_history()
      const {
    return ice_candidate_pair_change_history_;
  }

  // Every PeerConnection signaling state in order that has been seen by the
  // observer.
  std::vector<PeerConnectionInterface::SignalingState>
  peer_connection_signaling_state_history() const {
    return peer_connection_signaling_state_history_;
  }

  void AddAudioVideoTracks();

  scoped_refptr<RtpSenderInterface> AddAudioTrack() {
    return AddTrack(CreateLocalAudioTrack());
  }

  scoped_refptr<RtpSenderInterface> AddVideoTrack() {
    return AddTrack(CreateLocalVideoTrack());
  }

  scoped_refptr<AudioTrackInterface> CreateLocalAudioTrack();

  scoped_refptr<VideoTrackInterface> CreateLocalVideoTrack();

  scoped_refptr<VideoTrackInterface> CreateLocalVideoTrackWithConfig(
      FakePeriodicVideoSource::Config config);

  scoped_refptr<VideoTrackInterface> CreateLocalVideoTrackWithRotation(
      VideoRotation rotation);

  scoped_refptr<RtpSenderInterface> AddTrack(
      scoped_refptr<MediaStreamTrackInterface> track,
      const std::vector<std::string>& stream_ids = {});

  std::vector<scoped_refptr<RtpReceiverInterface>> GetReceiversOfType(
      webrtc::MediaType media_type);

  scoped_refptr<RtpTransceiverInterface> GetFirstTransceiverOfType(
      webrtc::MediaType media_type);

  bool SignalingStateStable();

  bool IceGatheringStateComplete();

  void CreateDataChannel();

  void CreateDataChannel(const DataChannelInit* init);

  void CreateDataChannel(const std::string& label, const DataChannelInit* init);

  // Return the last observed data channel.
  DataChannelInterface* data_channel();
  // Return all data channels.
  std::vector<scoped_refptr<DataChannelInterface>>& data_channels() {
    return data_channels_;
  }

  MockDataChannelObserver* data_observer() const;

  std::vector<std::unique_ptr<MockDataChannelObserver>>& data_observers() {
    return data_observers_;
  }

  std::unique_ptr<SessionDescriptionInterface> CreateAnswerForTest();

  int audio_frames_received() const;

  // Takes minimum of video frames received for each track.
  //
  // Can be used like:
  // EXPECT_GE(expected_frames, min_video_frames_received_per_track());
  //
  // To ensure that all video tracks received at least a certain number of
  // frames.
  int min_video_frames_received_per_track() const;

  // Returns a MockStatsObserver in a state after stats gathering finished,
  // which can be used to access the gathered stats.
  scoped_refptr<MockStatsObserver> OldGetStatsForTrack(
      MediaStreamTrackInterface* track);

  // Version that doesn't take a track "filter", and gathers all stats.
  scoped_refptr<MockStatsObserver> OldGetStats();

  // Synchronously gets stats and returns them. If it times out, fails the test
  // and returns null.
  // TODO(bugs.webrtc.org/501310910): Unify NewGetStats implementations by
  // making RunLoop an interface that can be implemented for simulated time too.
  // TODO(tommi) - Remove this method in favor of the one that supports RunLoop.
  scoped_refptr<const RTCStatsReport> NewGetStats(
      WaitUntilSettings settings = {});

  scoped_refptr<const RTCStatsReport> NewGetStats(test::RunLoop& run_loop);

  // Gets stats filtered by the stats selection algorithm for `selector`, i.e.
  // the equivalent of RTCRtpReceiver.getStats().
  scoped_refptr<const RTCStatsReport> NewGetStats(
      scoped_refptr<RtpReceiverInterface> selector,
      test::RunLoop& run_loop);

  std::string DtlsCipher();

  std::string SrtpCipher();

  int rendered_width();

  int rendered_height();

  double rendered_aspect_ratio();

  VideoRotation rendered_rotation();

  int local_rendered_width();

  int local_rendered_height();

  double local_rendered_aspect_ratio();

  PLAN_B_ONLY size_t number_of_remote_streams();

  PLAN_B_ONLY StreamCollectionInterface* remote_streams() const;

  PLAN_B_ONLY StreamCollectionInterface* local_streams();

  PeerConnectionInterface::SignalingState signaling_state();

  PeerConnectionInterface::IceConnectionState ice_connection_state();

  PeerConnectionInterface::IceConnectionState
  standardized_ice_connection_state();

  PeerConnectionInterface::IceGatheringState ice_gathering_state();

  // Returns a MockRtpReceiverObserver for each RtpReceiver returned by
  // GetReceivers. They're updated automatically when a remote offer/answer
  // from the fake signaling channel is applied, or when
  // ResetRtpReceiverObservers below is called.
  const std::vector<std::unique_ptr<MockRtpReceiverObserver>>&
  rtp_receiver_observers() {
    return rtp_receiver_observers_;
  }

  void ResetRtpReceiverObservers();

  const std::vector<std::unique_ptr<MockRtpSenderObserver>>&
  rtp_sender_observers() {
    return rtp_sender_observers_;
  }

  void ResetRtpSenderObservers();

  FakeNetworkManager* network_manager() const { return fake_network_manager_; }

  FakeRtcEventLogFactory* event_log_factory() const {
    return event_log_factory_;
  }

  Candidate last_candidate_gathered() const;
  const IceCandidate* last_gathered_ice_candidate() const {
    return last_gathered_ice_candidate_.get();
  }
  const IceCandidateErrorEvent& error_event() const { return error_event_; }

  // Sets the mDNS responder for the owned fake network manager and keeps a
  // reference to the responder.
  void SetMdnsResponder(std::unique_ptr<FakeMdnsResponder> mdns_responder);

  // Returns null on failure.
  std::unique_ptr<SessionDescriptionInterface> CreateOfferAndWait();
  bool Rollback();

  // Functions for querying stats.
  void StartWatchingDelayStats();

  void UpdateDelayStats(std::string tag, int desc_size);

  bool AudioDelayStatsPercentageChecked() const {
    return audio_delay_stats_percentage_checked_;
  }

  // Sets number of candidates expected
  void ExpectCandidates(int candidate_count) {
    candidates_expected_ = candidate_count;
  }

  bool SetRemoteDescription(std::unique_ptr<SessionDescriptionInterface> desc);

  void NegotiateCorruptionDetectionHeader();

  uint32_t GetCorruptionScoreCount();

  uint32_t GetReceivedFrameCount();

  void set_connection_change_callback(
      std::function<void(PeerConnectionInterface::PeerConnectionState)> func) {
    connection_change_callback_ = std::move(func);
  }

  std::optional<int> tls_version();

  std::optional<DtlsTransportTlsRole> dtls_transport_role();

  DtlsTransportInformation dtls_transport_information();

  // Setting the local description and sending the SDP message over the fake
  // signaling channel are combined into the same method because the SDP
  // message needs to be sent as soon as SetLocalDescription finishes, without
  // waiting for the observer to be called. This ensures that ICE candidates
  // don't outrace the description.
  bool SetLocalDescriptionAndSendSdpMessage(
      std::unique_ptr<SessionDescriptionInterface> desc);

 public:
  bool Init(const PeerConnectionFactory::Options* options,
            const PeerConnectionInterface::RTCConfiguration* config,
            PeerConnectionDependencies dependencies,
            SocketServer* socket_server,
            Thread* network_thread,
            std::unique_ptr<FakeRtcEventLogFactory> event_log_factory,
            bool reset_encoder_factory,
            bool reset_decoder_factory,
            bool create_media_engine);

  scoped_refptr<PeerConnectionInterface> CreatePeerConnection(
      const PeerConnectionInterface::RTCConfiguration* config,
      PeerConnectionDependencies dependencies);

  void set_signaling_delay_ms(int delay_ms) { signaling_delay_ms_ = delay_ms; }

  void set_signal_ice_candidates(bool signal) {
    signal_ice_candidates_ = signal;
  }

  scoped_refptr<VideoTrackInterface> CreateLocalVideoTrackInternal(
      FakePeriodicVideoSource::Config config);

  void HandleIncomingOffer(const std::string& msg);

  void HandleIncomingAnswer(SdpType type, const std::string& msg);

  // Returns null on failure.
  std::unique_ptr<SessionDescriptionInterface> CreateAnswer();

  // This is a work around to remove unused fake_video_renderers from
  // transceivers that have either stopped or are no longer receiving.
  void RemoveUnusedVideoRenderers();

  // Simulate sending a blob of SDP with delay `signaling_delay_ms_` (0 by
  // default).
  void SendSdpMessage(SdpType type, const std::string& msg);

  void RelaySdpMessageIfReceiverExists(SdpType type, const std::string& msg);

  // Simulate trickling an ICE candidate with delay `signaling_delay_ms_` (0 by
  // default).
  void SendIceMessage(const std::string& sdp_mid,
                      int sdp_mline_index,
                      const std::string& msg);

  void RelayIceMessageIfReceiverExists(const std::string& sdp_mid,
                                       int sdp_mline_index,
                                       const std::string& msg);

  // SignalingMessageReceiver callbacks.
 public:
  void set_signaling_message_receiver(
      SignalingMessageReceiver* signaling_message_receiver) {
    signaling_message_receiver_ = signaling_message_receiver;
  }

  void ReceiveSdpMessage(SdpType type, const std::string& msg) override;

  void ReceiveIceMessage(const std::string& sdp_mid,
                         int sdp_mline_index,
                         const std::string& msg) override;

 private:
  // PeerConnectionObserver callbacks.
  void OnSignalingChange(
      PeerConnectionInterface::SignalingState new_state) override;
  void OnAddTrack(
      scoped_refptr<RtpReceiverInterface> receiver,
      const std::vector<scoped_refptr<MediaStreamInterface>>& streams) override;
  void OnRemoveTrack(scoped_refptr<RtpReceiverInterface> receiver) override;
  void OnRenegotiationNeeded() override;
  void OnIceConnectionChange(
      PeerConnectionInterface::IceConnectionState new_state) override;
  void OnStandardizedIceConnectionChange(
      PeerConnectionInterface::IceConnectionState new_state) override;

  void OnConnectionChange(
      PeerConnectionInterface::PeerConnectionState new_state) override;

  void OnIceGatheringChange(
      PeerConnectionInterface::IceGatheringState new_state) override;

  void OnIceSelectedCandidatePairChanged(
      const CandidatePairChangeEvent& event) override {
    ice_candidate_pair_change_history_.push_back(event);
  }

  void OnIceCandidate(const IceCandidate* candidate) override;

  void OnIceCandidateError(const std::string& address,
                           int port,
                           const std::string& url,
                           int error_code,
                           const std::string& error_text) override;

  void OnIceCandidateRemoved(const IceCandidate* candidate) override {}

  void OnDataChannel(scoped_refptr<DataChannelInterface> data_channel) override;
  bool IdExists(const RtpHeaderExtensions& extensions, RtpHeaderExtensionId id);

 private:
  std::string debug_name_;
  const Environment env_;

  // Network manager is owned by the `peer_connection_factory_`.
  FakeNetworkManager* fake_network_manager_ = nullptr;
  Thread* network_thread_;

  // Reference to the mDNS responder owned by `fake_network_manager_` after set.
  FakeMdnsResponder* mdns_responder_ = nullptr;

  scoped_refptr<PeerConnectionInterface> peer_connection_;
  scoped_refptr<PeerConnectionFactoryInterface> peer_connection_factory_;

  // Needed to keep track of number of frames sent.
  scoped_refptr<FakeAudioCaptureModule> fake_audio_capture_module_;
  // Needed to keep track of number of frames received.
  std::map<std::string, std::unique_ptr<FakeVideoTrackRenderer>>
      fake_video_renderers_;
  // Needed to ensure frames aren't received for removed tracks.
  std::vector<std::unique_ptr<FakeVideoTrackRenderer>>
      removed_fake_video_renderers_;

  // For remote peer communication.
  SignalingMessageReceiver* signaling_message_receiver_ = nullptr;
  int signaling_delay_ms_ = 0;
  bool signal_ice_candidates_ = true;
  std::unique_ptr<IceCandidate> last_gathered_ice_candidate_;
  IceCandidateErrorEvent error_event_;

  // Store references to the video sources we've created, so that we can stop
  // them, if required.
  std::vector<scoped_refptr<VideoTrackSource>> video_track_sources_;
  // `local_video_renderer_` attached to the first created local video track.
  std::unique_ptr<FakeVideoTrackRenderer> local_video_renderer_;

  SdpSemantics sdp_semantics_;
  PeerConnectionInterface::RTCOfferAnswerOptions offer_answer_options_;
  std::function<void(std::unique_ptr<SessionDescriptionInterface>&)>
      received_sdp_munger_;
  std::function<void(std::unique_ptr<SessionDescriptionInterface>&)>
      generated_sdp_munger_;
  std::function<void()> remote_offer_handler_;
  MockAsyncDnsResolver* remote_async_dns_resolver_ = nullptr;
  // Result variables for the mock DNS resolver
  NiceMock<MockAsyncDnsResolverResult> remote_async_dns_resolver_result_;
  SocketAddress remote_async_dns_resolved_addr_;

  // All data channels either created or observed on this peerconnection
  std::vector<scoped_refptr<DataChannelInterface>> data_channels_;
  std::vector<std::unique_ptr<MockDataChannelObserver>> data_observers_;

  std::vector<std::unique_ptr<MockRtpReceiverObserver>> rtp_receiver_observers_;
  std::vector<std::unique_ptr<MockRtpSenderObserver>> rtp_sender_observers_;

  std::vector<PeerConnectionInterface::IceConnectionState>
      ice_connection_state_history_;
  std::vector<PeerConnectionInterface::IceConnectionState>
      standardized_ice_connection_state_history_;
  std::vector<PeerConnectionInterface::PeerConnectionState>
      peer_connection_state_history_;
  std::vector<PeerConnectionInterface::IceGatheringState>
      ice_gathering_state_history_;
  std::vector<CandidatePairChangeEvent> ice_candidate_pair_change_history_;
  std::vector<PeerConnectionInterface::SignalingState>
      peer_connection_signaling_state_history_;
  FakeRtcEventLogFactory* event_log_factory_;

  // Number of ICE candidates expected. The default is no limit.
  int candidates_expected_ = std::numeric_limits<int>::max();

  // Variables for tracking delay stats on an audio track
  int audio_packets_stat_ = 0;
  double audio_delay_stat_ = 0.0;
  // Trailing counters from the most recent renegotiation step.
  uint64_t audio_samples_stat_ = 0;
  uint64_t audio_concealed_stat_ = 0;
  // Baseline counters captured at the start of watching delay stats, used to
  // compute cumulative sample metrics across all renegotiation steps.
  uint64_t initial_audio_samples_stat_ = 0;
  uint64_t initial_audio_concealed_stat_ = 0;
  std::string rtp_stats_id_;
  bool audio_delay_stats_percentage_checked_ = false;

  std::function<void(PeerConnectionInterface::PeerConnectionState)>
      connection_change_callback_ = nullptr;

  ScopedTaskSafety task_safety_;
  internal::PeerConnectionIntegrationTestBase* test_ = nullptr;
};

class MockRtcEventLogOutput : public RtcEventLogOutput {
 public:
  ~MockRtcEventLogOutput() override = default;
  MOCK_METHOD(bool, IsActive, (), (const, override));
  MOCK_METHOD(bool, Write, (absl::string_view), (override));
};

// This helper object is used for both specifying how many audio/video frames
// are expected to be received for a caller/callee. It provides helper functions
// to specify these expectations. The object initially starts in a state of no
// expectations.
class MediaExpectations {
 public:
  enum ExpectFrames {
    kExpectSomeFrames,
    kExpectNoFrames,
    kNoExpectation,
  };

  void ExpectBidirectionalAudioAndVideo() {
    ExpectBidirectionalAudio();
    ExpectBidirectionalVideo();
  }

  void ExpectBidirectionalAudio() {
    CallerExpectsSomeAudio();
    CalleeExpectsSomeAudio();
  }

  void ExpectNoAudio() {
    CallerExpectsNoAudio();
    CalleeExpectsNoAudio();
  }

  void ExpectBidirectionalVideo() {
    CallerExpectsSomeVideo();
    CalleeExpectsSomeVideo();
  }

  void ExpectNoVideo() {
    CallerExpectsNoVideo();
    CalleeExpectsNoVideo();
  }

  void CallerExpectsSomeAudioAndVideo() {
    CallerExpectsSomeAudio();
    CallerExpectsSomeVideo();
  }

  void CalleeExpectsSomeAudioAndVideo() {
    CalleeExpectsSomeAudio();
    CalleeExpectsSomeVideo();
  }

  // Caller's audio functions.
  void CallerExpectsSomeAudio(
      int expected_audio_frames = kDefaultExpectedAudioFrameCount) {
    caller_audio_expectation_ = kExpectSomeFrames;
    caller_audio_frames_expected_ = expected_audio_frames;
  }

  void CallerExpectsNoAudio() {
    caller_audio_expectation_ = kExpectNoFrames;
    caller_audio_frames_expected_ = 0;
  }

  // Caller's video functions.
  void CallerExpectsSomeVideo(
      int expected_video_frames = kDefaultExpectedVideoFrameCount) {
    caller_video_expectation_ = kExpectSomeFrames;
    caller_video_frames_expected_ = expected_video_frames;
  }

  void CallerExpectsNoVideo() {
    caller_video_expectation_ = kExpectNoFrames;
    caller_video_frames_expected_ = 0;
  }

  // Callee's audio functions.
  void CalleeExpectsSomeAudio(
      int expected_audio_frames = kDefaultExpectedAudioFrameCount) {
    callee_audio_expectation_ = kExpectSomeFrames;
    callee_audio_frames_expected_ = expected_audio_frames;
  }

  void CalleeExpectsNoAudio() {
    callee_audio_expectation_ = kExpectNoFrames;
    callee_audio_frames_expected_ = 0;
  }

  // Callee's video functions.
  void CalleeExpectsSomeVideo(
      int expected_video_frames = kDefaultExpectedVideoFrameCount) {
    callee_video_expectation_ = kExpectSomeFrames;
    callee_video_frames_expected_ = expected_video_frames;
  }

  void CalleeExpectsNoVideo() {
    callee_video_expectation_ = kExpectNoFrames;
    callee_video_frames_expected_ = 0;
  }

  ExpectFrames caller_audio_expectation_ = kNoExpectation;
  ExpectFrames caller_video_expectation_ = kNoExpectation;
  ExpectFrames callee_audio_expectation_ = kNoExpectation;
  ExpectFrames callee_video_expectation_ = kNoExpectation;
  int caller_audio_frames_expected_ = 0;
  int caller_video_frames_expected_ = 0;
  int callee_audio_frames_expected_ = 0;
  int callee_video_frames_expected_ = 0;
};

class MockIceTransport : public IceTransportInterface {
 public:
  MockIceTransport(const Environment& env,
                   absl::string_view name,
                   int component)
      : internal_(std::make_unique<FakeIceTransportInternal>(env,
                                                             name,
                                                             component,
                                                             nullptr)) {}
  ~MockIceTransport() override = default;
  IceTransportInternal* internal() override { return internal_.get(); }

 private:
  std::unique_ptr<FakeIceTransportInternal> internal_;
};

class MockIceTransportFactory : public IceTransportFactory {
 public:
  ~MockIceTransportFactory() override = default;
  scoped_refptr<IceTransportInterface> CreateIceTransport(
      const std::string& transport_name,
      int component,
      IceTransportInit init) override {
    RecordIceTransportCreated();
    return make_ref_counted<MockIceTransport>(init.env(), transport_name,
                                              component);
  }
  MOCK_METHOD(void, RecordIceTransportCreated, ());
};

// Tests two PeerConnections connecting to each other end-to-end, using a
// virtual network, fake A/V capture and fake encoder/decoders. The
// PeerConnections share the threads/socket servers, but use separate versions
// of everything else (including "PeerConnectionFactory"s).
namespace internal {
class PeerConnectionIntegrationTestBase : public ::testing::Test {
 public:
  static constexpr char kCallerName[] = "Caller";
  static constexpr char kCalleeName[] = "Callee";

  explicit PeerConnectionIntegrationTestBase(Environment env,
                                             SdpSemantics sdp_semantics);
  PeerConnectionIntegrationTestBase(Environment env,
                                    SdpSemantics sdp_semantics,
                                    TimeController* time_controller);
  ~PeerConnectionIntegrationTestBase() override;

  virtual std::unique_ptr<PeerConnectionIntegrationWrapper>
  CreatePeerConnectionWrapperInternal(const std::string& debug_name,
                                      Environment env);

  virtual Waiter GetWaiter(WaitUntilSettings overrides = {}) = 0;
  virtual TimeController* time_controller() { return nullptr; }
  virtual std::unique_ptr<Thread> CreateThread(absl::string_view name) = 0;

  bool SignalingStateStable() {
    return caller_->SignalingStateStable() && callee_->SignalingStateStable();
  }

  bool PeerConnectionStateIs(
      PeerConnectionInterface::PeerConnectionState state) {
    return caller_->pc()->peer_connection_state() == state &&
           callee_->pc()->peer_connection_state() == state;
  }

  bool DtlsConnected();

  // Sets field trials to pass to created PeerConnectionWrapper.
  // Must be called before PeerConnectionWrappers are created.
  void SetFieldTrials(absl::string_view field_trials);

  // Sets field trials to pass to created PeerConnectionWrapper key:ed on
  // debug_name. Must be called before PeerConnectionWrappers are created.
  void SetFieldTrials(absl::string_view debug_name,
                      absl::string_view field_trials);

  // When `event_log_factory` is null, the default implementation of the event
  // log factory will be used.
  std::unique_ptr<PeerConnectionIntegrationWrapper> CreatePeerConnectionWrapper(
      const std::string& debug_name,
      const PeerConnectionFactory::Options* options,
      const RTCConfiguration* config,
      PeerConnectionDependencies dependencies,
      std::unique_ptr<FakeRtcEventLogFactory> event_log_factory,
      bool reset_encoder_factory,
      bool reset_decoder_factory,
      bool create_media_engine = true);

  std::unique_ptr<PeerConnectionIntegrationWrapper>
  CreatePeerConnectionWrapperWithFakeRtcEventLog(
      const std::string& debug_name,
      const PeerConnectionFactory::Options* options,
      const RTCConfiguration* config,
      PeerConnectionDependencies dependencies);

  bool CreatePeerConnectionWrappers() {
    return CreatePeerConnectionWrappersWithConfig(
        PeerConnectionInterface::RTCConfiguration(),
        PeerConnectionInterface::RTCConfiguration());
  }

  bool CreatePeerConnectionWrappersWithSdpSemantics(
      SdpSemantics caller_semantics,
      SdpSemantics callee_semantics);

  bool CreatePeerConnectionWrappersWithConfig(
      const PeerConnectionInterface::RTCConfiguration& caller_config,
      const PeerConnectionInterface::RTCConfiguration& callee_config,
      bool create_media_engine = true);

  bool CreatePeerConnectionWrappersWithConfigAndDeps(
      const PeerConnectionInterface::RTCConfiguration& caller_config,
      PeerConnectionDependencies caller_dependencies,
      const PeerConnectionInterface::RTCConfiguration& callee_config,
      PeerConnectionDependencies callee_dependencies);

  bool CreatePeerConnectionWrappersWithOptions(
      const PeerConnectionFactory::Options& caller_options,
      const PeerConnectionFactory::Options& callee_options);

  bool CreatePeerConnectionWrappersWithFakeRtcEventLog();

  std::unique_ptr<PeerConnectionIntegrationWrapper>
  CreatePeerConnectionWrapperWithAlternateKey();

  bool CreateOneDirectionalPeerConnectionWrappers(bool caller_to_callee);

  bool CreatePeerConnectionWrappersWithoutMediaEngine();

  TestTurnServer* CreateTurnServer(
      SocketAddress internal_address,
      SocketAddress external_address,
      ProtocolType type = ProtocolType::PROTO_UDP,
      const std::string& common_name = "test turn server");

  TestTurnCustomizer* CreateTurnCustomizer();

  // Checks that the function counters for a TestTurnCustomizer are greater than
  // 0.
  void ExpectTurnCustomizerCountersIncremented(
      TestTurnCustomizer* turn_customizer);

  // Once called, SDP blobs and ICE candidates will be automatically signaled
  // between PeerConnections.
  void ConnectFakeSignaling();

  // Once called, SDP blobs will be automatically signaled between
  // PeerConnections. Note that ICE candidates will not be signaled unless they
  // are in the exchanged SDP blobs.
  void ConnectFakeSignalingForSdpOnly();

  void SetSignalingDelayMs(int delay_ms);

  void SetSignalIceCandidates(bool signal);

  // Messages may get lost on the unreliable DataChannel, so we send multiple
  // times to avoid test flakiness.
  void SendRtpDataWithRetries(DataChannelInterface* dc,
                              const std::string& data,
                              int retries);

  Thread* network_thread() { return network_thread_.get(); }

  VirtualSocketServer* virtual_socket_server() { return ss_.get(); }

  PeerConnectionIntegrationWrapper* caller() { return caller_.get(); }

  // Destroy peer connections.
  // This can be used to ensure that all pointers to on-stack mocks
  // get dropped before exit.
  void DestroyPeerConnections();

  void DestroyTurnServers();
  void DestroyThreads();

  // Set the `caller_` to the `wrapper` passed in and return the
  // original `caller_`.
  PeerConnectionIntegrationWrapper* SetCallerPcWrapperAndReturnCurrent(
      std::unique_ptr<PeerConnectionIntegrationWrapper> wrapper);

  PeerConnectionIntegrationWrapper* callee() { return callee_.get(); }

  // Set the `callee_` to the `wrapper` passed in and return the
  // original `callee_`.
  PeerConnectionIntegrationWrapper* SetCalleePcWrapperAndReturnCurrent(
      std::unique_ptr<PeerConnectionIntegrationWrapper> wrapper);

  FirewallSocketServer* firewall() const { return fss_.get(); }

  // Expects the provided number of new frames to be received within
  // kMaxWaitForFramesMs. The new expected frames are specified in
  // `media_expectations`. Returns false if any of the expectations were
  // not met.
  bool ExpectNewFrames(const MediaExpectations& media_expectations);

  void ClosePeerConnections();

  void TestNegotiatedCipherSuite(const RTCConfiguration& caller_config,
                                 const RTCConfiguration& callee_config,
                                 int expected_cipher_suite);

  void TestGcmNegotiationUsesCipherSuite(bool local_gcm_enabled,
                                         bool remote_gcm_enabled,
                                         bool aes_ctr_enabled,
                                         int expected_cipher_suite);

 protected:
  SdpSemantics sdp_semantics_;
  const Environment env_;

  virtual void ExecuteTask(TaskQueueBase& task_queue,
                           absl::AnyInvocable<void()> task) = 0;

 private:
  // `ss_` is used by `network_thread_` so it must be destroyed later.
  std::unique_ptr<VirtualSocketServer> ss_;
  std::unique_ptr<FirewallSocketServer> fss_;

  // `network_thread_` is used by both `caller_` and `callee_` so it must be
  // destroyed later.
  std::unique_ptr<Thread> network_thread_;
  // The turn servers and turn customizers should be accessed & deleted on the
  // network thread to avoid a race with the socket read/write that occurs
  // on the network thread.
  std::vector<std::unique_ptr<TestTurnServer>> turn_servers_;
  std::vector<std::unique_ptr<TestTurnCustomizer>> turn_customizers_;
  std::unique_ptr<PeerConnectionIntegrationWrapper> caller_;
  std::unique_ptr<PeerConnectionIntegrationWrapper> callee_;
  std::string field_trials_;
  std::map<std::string, std::string> field_trials_overrides_;
};
}  // namespace internal

class PeerConnectionIntegrationBaseTest
    : public internal::PeerConnectionIntegrationTestBase {
 public:
  explicit PeerConnectionIntegrationBaseTest(SdpSemantics sdp_semantics);
  ~PeerConnectionIntegrationBaseTest() override;

  test::RunLoop& run_loop() { return run_loop_; }

  void ExecuteTask(TaskQueueBase& task_queue,
                   absl::AnyInvocable<void()> task) override;

  Waiter GetWaiter(WaitUntilSettings overrides = {}) override;

  std::unique_ptr<Thread> CreateThread(absl::string_view name) override;

 protected:
  test::RunLoop run_loop_;
};

class PeerConnectionIntegrationTestWithSimulatedTime
    : public internal::PeerConnectionIntegrationTestBase {
 public:
  explicit PeerConnectionIntegrationTestWithSimulatedTime(
      SdpSemantics sdp_semantics);

  void ExecuteTask(TaskQueueBase& task_queue,
                   absl::AnyInvocable<void()> task) override;

  Waiter GetWaiter(WaitUntilSettings overrides = {}) override;
  TimeController* time_controller() { return time_controller_.get(); }
  std::unique_ptr<Thread> CreateThread(absl::string_view name) override;

  ~PeerConnectionIntegrationTestWithSimulatedTime() override;

 private:
  PeerConnectionIntegrationTestWithSimulatedTime(
      SdpSemantics sdp_semantics,
      std::unique_ptr<GlobalSimulatedTimeController> time_controller);

 protected:
  std::unique_ptr<GlobalSimulatedTimeController> time_controller_;
};

template <typename Base>
class PeerConnectionIntegrationIceStatesTestBase
    : public Base,
      public ::testing::WithParamInterface<
          std::tuple<SdpSemantics, std::tuple<std::string, uint32_t>>> {
 protected:
  PeerConnectionIntegrationIceStatesTestBase()
      : Base(std::get<0>(this->GetParam())) {
    port_allocator_flags_ = std::get<1>(std::get<1>(this->GetParam()));
  }

  void StartStunServer(const SocketAddress& server_address) {
    stun_server_ = TestStunServer::Create(
        this->env_, server_address, *this->firewall(), *this->network_thread());
  }

  bool TestIPv6() {
    return (port_allocator_flags_ & PORTALLOCATOR_ENABLE_IPV6);
  }

  std::vector<SocketAddress> CallerAddresses() {
    std::vector<SocketAddress> addresses;
    addresses.push_back(SocketAddress("1.1.1.1", 0));
    if (TestIPv6()) {
      addresses.push_back(SocketAddress("1111:0:a:b:c:d:e:f", 0));
    }
    return addresses;
  }

  std::vector<SocketAddress> CalleeAddresses() {
    std::vector<SocketAddress> addresses;
    addresses.push_back(SocketAddress("2.2.2.2", 0));
    if (TestIPv6()) {
      addresses.push_back(SocketAddress("2222:0:a:b:c:d:e:f", 0));
    }
    return addresses;
  }

  void SetUpNetworkInterfaces() {
    // Remove the default interfaces added by the test infrastructure.
    this->caller()->network_manager()->RemoveInterface(kDefaultLocalAddress);
    this->callee()->network_manager()->RemoveInterface(kDefaultLocalAddress);

    // Add network addresses for test.
    for (const auto& caller_address : CallerAddresses()) {
      this->caller()->network_manager()->AddInterface(caller_address);
    }
    for (const auto& callee_address : CalleeAddresses()) {
      this->callee()->network_manager()->AddInterface(callee_address);
    }
  }

  uint32_t port_allocator_flags() const { return port_allocator_flags_; }

 private:
  uint32_t port_allocator_flags_;
  TestStunServer::StunServerPtr stun_server_;
};

}  // namespace webrtc

#endif  // PC_TEST_INTEGRATION_TEST_HELPERS_H_
