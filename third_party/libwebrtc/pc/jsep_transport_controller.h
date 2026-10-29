/*
 *  Copyright 2017 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_JSEP_TRANSPORT_CONTROLLER_H_
#define PC_JSEP_TRANSPORT_CONTROLLER_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/async_dns_resolver.h"
#include "api/candidate.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/ice_transport_interface.h"
#include "api/jsep.h"
#include "api/local_network_access_permission.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_transport_factory.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/transport/data_channel_transport_interface.h"
#include "api/transport/ecn_marking.h"
#include "api/transport/sctp_transport_factory_interface.h"
#include "api/units/timestamp.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/packet_transport_internal.h"
#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/transport_info.h"
#include "p2p/dtls/dtls_transport_factory.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "pc/dtls_srtp_transport.h"
#include "pc/dtls_transport.h"
#include "pc/jsep_transport.h"
#include "pc/jsep_transport_collection.h"
#include "pc/rtp_transport.h"
#include "pc/rtp_transport_internal.h"
#include "pc/scoped_operations_batcher.h"
#include "pc/sctp_transport.h"
#include "pc/session_description.h"
#include "pc/transport_stats.h"
#include "rtc_base/containers/flat_map.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/ssl_certificate.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {

class JsepTransportController final {
 public:
  // Used when the RtpTransport/DtlsTransport of the m= section is changed
  // because the section is rejected or BUNDLE is enabled.
  class Observer {
   public:
    virtual ~Observer() {}

    // Returns true if media associated with `mid` was successfully set up to be
    // demultiplexed on `rtp_transport`. Could return false if two bundled m=
    // sections use the same SSRC, for example.
    //
    // If a data channel transport must be negotiated, `data_channel_transport`
    // and `negotiation_state` indicate negotiation status.  If
    // `data_channel_transport` is null, the data channel transport should not
    // be used.  Otherwise, the value is a pointer to the transport to be used
    // for data channels on `mid`, if any.
    //
    // The observer should not send data on `data_channel_transport` until
    // `negotiation_state` is provisional or final.  It should not delete
    // `data_channel_transport` or any fallback transport until
    // `negotiation_state` is final.
    virtual bool OnTransportChanged(
        absl::string_view mid,
        RtpTransportInternal* rtp_transport,
        scoped_refptr<DtlsTransport> dtls_transport,
        DataChannelTransportInterface* data_channel_transport) = 0;
  };

  struct Config {
    // If `redetermine_role_on_ice_restart` is true, ICE role is redetermined
    // upon setting a local transport description that indicates an ICE
    // restart.
    bool redetermine_role_on_ice_restart = true;
    SSLProtocolVersion ssl_max_version = SSL_PROTOCOL_DTLS_12;
    // `crypto_options` is used to determine if created DTLS transports
    // negotiate GCM crypto suites or not.
    CryptoOptions crypto_options;
    PeerConnectionInterface::BundlePolicy bundle_policy =
        PeerConnectionInterface::kBundlePolicyBalanced;
    PeerConnectionInterface::RtcpMuxPolicy rtcp_mux_policy =
        PeerConnectionInterface::kRtcpMuxPolicyRequire;
    bool disable_encryption = false;
    // Used to inject the ICE/DTLS/SRTP transports created externally.
    IceTransportFactory* ice_transport_factory = nullptr;
    DtlsTransportFactory* dtls_transport_factory = nullptr;
    RtpTransportFactory* rtp_transport_factory = nullptr;
    Observer* transport_observer = nullptr;
    // Must be provided and valid for the lifetime of the
    // JsepTransportController instance.
    absl::AnyInvocable<void(const CopyOnWriteBuffer& packet,
                            int64_t packet_time_us) const>
        rtcp_handler;
    absl::AnyInvocable<void(const RtpPacketReceived& parsed_packet) const>
        un_demuxable_packet_handler;

    // Factory for SCTP transports.
    SctpTransportFactoryInterface* sctp_factory = nullptr;
    absl::AnyInvocable<void(SSLHandshakeError) const> on_dtls_handshake_error =
        [](SSLHandshakeError s) {};
    absl::AnyInvocable<void(absl::string_view, const std::vector<Candidate>&)
                           const>
        signal_ice_candidates_gathered =
            [](absl::string_view, const std::vector<Candidate>&) {};
    absl::AnyInvocable<void(IceConnectionState) const>
        signal_ice_connection_state = [](IceConnectionState) {};
    absl::AnyInvocable<void(PeerConnectionInterface::PeerConnectionState) const>
        signal_connection_state =
            [](PeerConnectionInterface::PeerConnectionState) {};
    absl::AnyInvocable<void(PeerConnectionInterface::IceConnectionState) const>
        signal_standardized_ice_connection_state =
            [](PeerConnectionInterface::IceConnectionState) {};
    absl::AnyInvocable<void(webrtc::IceGatheringState) const>
        signal_ice_gathering_state = [](webrtc::IceGatheringState) {};
    absl::AnyInvocable<void(const webrtc::IceCandidateErrorEvent&) const>
        signal_ice_candidate_error =
            [](const webrtc::IceCandidateErrorEvent&) {};
    absl::AnyInvocable<void(IceTransportInternal*,
                            const std::vector<webrtc::Candidate>&) const>
        signal_ice_candidates_removed =
            [](IceTransportInternal*, const std::vector<webrtc::Candidate>&) {};
    absl::AnyInvocable<void(const webrtc::CandidatePairChangeEvent&) const>
        signal_ice_candidate_pair_changed =
            [](const webrtc::CandidatePairChangeEvent&) {};
  };

  struct TransportState {
    std::optional<SSLRole> dtls_role;
    bool needs_ice_restart = false;
    // The RTP DTLS transport that the mid uses, and the name of that
    // transport. With BUNDLE, several mids share a transport.
    absl_nonnull scoped_refptr<DtlsTransport> dtls_transport;
    std::string transport_name;
  };

  // Per-mid transport states, captured on the network thread. `version` is
  // incremented for every snapshot, which allows the signaling thread to
  // ignore snapshots that are older than the one it already has.
  struct TransportStateSnapshot {
    uint64_t version = 0;
    flat_map<std::string, TransportState> states;
  };

  // The ICE related events are fired on the `network_thread`.
  // All the transport related methods are called on the `network_thread`
  // and destruction of the JsepTransportController must occur on the
  // `network_thread`.
  JsepTransportController(
      const Environment& env,
      TaskQueueBase* signaling_thread,
      Thread* network_thread,
      PortAllocator* port_allocator,
      AsyncDnsResolverFactoryInterface* async_dns_resolver_factory,
      LocalNetworkAccessPermissionFactoryInterface* lna_permission_factory,
      Config config);
  ~JsepTransportController();

  JsepTransportController(const JsepTransportController&) = delete;
  JsepTransportController& operator=(const JsepTransportController&) = delete;

  // The main method to be called; applies a description at the transport
  // level, creating/destroying transport objects as needed and updating their
  // properties. This includes RTP, DTLS, and ICE (but not SCTP). At least not
  // yet? May make sense to in the future.
  //
  // `local_desc` must always be valid. If a remote description has previously
  // been set via a call to `SetRemoteDescription()` then `remote_desc` should
  // point to that description object in order to keep the current local and
  // remote session descriptions in sync.
  //
  // Must be called on the signaling thread.
  RTCError SetLocalDescription(SdpType type,
                               const SessionDescription* local_desc,
                               const SessionDescription* remote_desc);

  // Call to apply a remote description (See `SetLocalDescription()` for local).
  //
  // `remote_desc` must always be valid. If a local description has previously
  // been set via a call to `SetLocalDescription()` then `local_desc` should
  // point to that description object in order to keep the current local and
  // remote session descriptions in sync.
  //
  // Must be called on the signaling thread.
  RTCError SetRemoteDescription(SdpType type,
                                const SessionDescription* local_desc,
                                const SessionDescription* remote_desc);

  // Get transports to be used for the provided `mid`. If bundling is enabled,
  // calling GetRtpTransport for multiple MIDs may yield the same object.
  RtpTransportInternal* GetRtpTransport(absl::string_view mid) const;
  DtlsTransportInternal* GetDtlsTransport(absl::string_view mid);
  // Gets the externally sharable version of the DtlsTransport.
  scoped_refptr<DtlsTransport> LookupDtlsTransportByMid_n(
      absl::string_view mid);
  // Same as LookupDtlsTransportByMid_n(), but reads the transport states that
  // are cached on the signaling thread instead of blocking on the network
  // thread.
  //
  // Must be called on the signaling thread.
  scoped_refptr<DtlsTransport> LookupDtlsTransportByMid(absl::string_view mid);
  // Returns the cached state of the transport that `mid` uses, or nullopt if
  // `mid` doesn't use a transport (e.g. if it's rejected).
  //
  // Must be called on the signaling thread.
  std::optional<TransportState> GetTransportState(absl::string_view mid) const;
  scoped_refptr<SctpTransport> GetSctpTransport(absl::string_view mid) const;

  DataChannelTransportInterface* GetDataChannelTransport(
      absl::string_view mid) const;

  /*********************
   * ICE-related methods
   ********************/
  // This method is public to allow PeerConnection to update it from
  // SetConfiguration.
  void SetIceConfig(const IceConfig& config);
  // Set the "needs-ice-restart" flag as described in JSEP. After the flag is
  // set, offers should generate new ufrags/passwords until an ICE restart
  // occurs.
  void SetNeedsIceRestartFlag();
  // Returns true if the ICE restart flag above was set, and no ICE restart has
  // occurred yet for this transport (by applying a local description with
  // changed ufrag/password). If the transport has been deleted as a result of
  // bundling, returns false.
  //
  // Must be called on the signaling thread.
  bool NeedsIceRestart(absl::string_view mid) const;
  // Start gathering candidates for any new transports, or transports doing an
  // ICE restart. New transports take sessions from the port allocator's
  // candidate pool, if there are any. If `discard_candidate_pool` is true, the
  // sessions that are left in the pool are discarded afterwards. Doesn't
  // block: if `on_pooled_credentials` is set, it's called on the signaling
  // thread with the pooled ICE credentials that are left.
  //
  // Must be called on the signaling thread.
  void MaybeStartGathering(
      bool discard_candidate_pool,
      absl::AnyInvocable<void(std::vector<IceParameters>) &&>
          on_pooled_credentials);
  // Discards the sessions in the port allocator's candidate pool. Doesn't
  // block.
  //
  // Must be called on the signaling thread.
  void DiscardCandidatePool();
  RTCError AddRemoteCandidates(absl::string_view mid,
                               const std::vector<Candidate>& candidates);
  // Must be called on the signaling thread.
  bool RemoveRemoteCandidate(const IceCandidate* candidate);

  /**********************
   * DTLS-related methods
   *********************/
  // Specifies the identity to use in this session.
  // Can only be called once. Returns false if a certificate has already been
  // set or if `certificate` is null. Doesn't block: the certificate is applied
  // on the network thread, before any description that is set afterwards. If
  // the signaling and network threads are the same thread, it's applied before
  // this returns.
  //
  // Must be called on the signaling thread.
  bool SetLocalCertificate(const scoped_refptr<RTCCertificate>& certificate);
  scoped_refptr<RTCCertificate> GetLocalCertificate(
      absl::string_view mid) const;
  // Caller owns returned certificate chain. This method mainly exists for
  // stats reporting.
  std::unique_ptr<SSLCertChain> GetRemoteSSLCertChain(
      absl::string_view mid) const;
  // Get negotiated role, if one has been negotiated.
  //
  // Must be called on the signaling thread.
  std::optional<SSLRole> GetDtlsRole(absl::string_view mid) const;

  // Must be called on the signaling thread. A snapshot that is older than the
  // current one is ignored.
  void SetTransportStates(TransportStateSnapshot snapshot);

  // Must be called on the network thread.
  TransportStateSnapshot GetTransportStates_n();

  bool GetStats(absl::string_view transport_name, TransportStats* stats) const;

  // Returns a task for a network thread ScopedOperationsBatcher that rolls
  // back the transports to the last stable state. The task returns the result
  // of the rollback. Its finalizer applies the resulting transport states on
  // the signaling thread, also if the rollback fails.
  //
  // Must be called on the signaling thread.
  ScopedOperationsBatcher::BatchTaskWithFinalizer MakeRollbackTask();

  // Must be called on the signaling thread.
  absl::AnyInvocable<void() &&> MakeCloseTask();

 private:
  // Always called via a blocking call from the signaling thread.
  RTCError SetLocalDescription_n(SdpType type,
                                 const SessionDescription* local_desc,
                                 const SessionDescription* remote_desc)
      RTC_RUN_ON(network_thread_);

  bool HasIceRestart_n(const SessionDescription* local_desc) const
      RTC_RUN_ON(network_thread_);

  // Always called via a blocking call from the signaling thread.
  RTCError SetRemoteDescription_n(SdpType type,
                                  const SessionDescription* local_desc,
                                  const SessionDescription* remote_desc)
      RTC_RUN_ON(network_thread_);

  // Always called via a blocking call from the signaling thread.
  bool RemoveRemoteCandidate_n(const IceCandidate* candidate)
      RTC_RUN_ON(network_thread_);

  // Called from the task that MakeRollbackTask() returns.
  RTCError RollbackTransports_n() RTC_RUN_ON(network_thread_);

  // Called from a task that MaybeStartGathering() posts.
  void MaybeStartGathering_n() RTC_RUN_ON(network_thread_);

  // Runs `task` on the network thread without blocking. If the signaling and
  // network threads are the same thread, `task` runs right away, as a blocking
  // call would. Otherwise it's posted. Either way, it runs before any blocking
  // call to the network thread that is made afterwards. `task` doesn't run
  // once the transports have been destroyed.
  //
  // Must be called on the signaling thread.
  void RunOnNetworkThread(absl::AnyInvocable<void() &&> task);

  // Called from SetLocalCertificate(), via RunOnNetworkThread().
  void SetLocalCertificate_n(
      const absl_nonnull scoped_refptr<RTCCertificate>& certificate)
      RTC_RUN_ON(network_thread_);

  // Called from SetLocalDescription and SetRemoteDescription.
  // When `local` is true, local_desc must be valid. Similarly when
  // `local` is false, remote_desc must be valid. The description counterpart
  // to the one that's being applied, may be nullptr but when it's supplied
  // the counterpart description's content groups will  be kept up to date for
  // `type == SdpType::kAnswer`.
  RTCError ApplyDescription_n(bool local,
                              SdpType type,
                              const SessionDescription* local_desc,
                              const SessionDescription* remote_desc)
      RTC_RUN_ON(network_thread_);

  RTCError ValidateAndMaybeUpdateBundleGroups(
      bool local,
      SdpType type,
      const SessionDescription* local_desc,
      const SessionDescription* remote_desc) RTC_RUN_ON(network_thread_);
  RTCError ValidateContent(const ContentInfo& content_info);

  void HandleRejectedContent(const ContentInfo& content_info)
      RTC_RUN_ON(network_thread_);
  bool HandleBundledContent(const ContentInfo& content_info,
                            const ContentGroup& bundle_group)
      RTC_RUN_ON(network_thread_);

  JsepTransportDescription CreateJsepTransportDescription(
      const ContentInfo& content_info,
      const TransportInfo& transport_info,
      const std::vector<RtpHeaderExtensionId>& encrypted_extension_ids);

  std::map<const ContentGroup*, std::vector<RtpHeaderExtensionId>>
  MergeEncryptedHeaderExtensionIdsForBundles(
      const SessionDescription* description);
  std::vector<RtpHeaderExtensionId> GetEncryptedHeaderExtensionIds(
      const ContentInfo& content_info);

  // This method takes the BUNDLE group into account. If the JsepTransport is
  // destroyed because of BUNDLE, it would return the transport which other
  // transports are bundled on (In current implementation, it is the first
  // content in the BUNDLE group).
  const JsepTransport* GetJsepTransportForMid(absl::string_view mid) const
      RTC_RUN_ON(network_thread_);
  JsepTransport* GetJsepTransportForMid(absl::string_view mid)
      RTC_RUN_ON(network_thread_);

  // Get the JsepTransport without considering the BUNDLE group. Return nullptr
  // if the JsepTransport is destroyed.
  const JsepTransport* GetJsepTransportByName(
      absl::string_view transport_name) const RTC_RUN_ON(network_thread_);
  JsepTransport* GetJsepTransportByName(absl::string_view transport_name)
      RTC_RUN_ON(network_thread_);

  // Creates jsep transport. Noop if transport is already created.
  // Transport is created either during SetLocalDescription (`local` == true) or
  // during SetRemoteDescription (`local` == false). Passing `local` helps to
  // differentiate initiator (caller) from answerer (callee).
  RTCError MaybeCreateJsepTransport(bool local,
                                    const ContentInfo& content_info,
                                    const SessionDescription& description)
      RTC_RUN_ON(network_thread_);

  void DestroyAllJsepTransports_n() RTC_RUN_ON(network_thread_);

  void SetIceRole_n(IceRole ice_role) RTC_RUN_ON(network_thread_);

  IceRole DetermineIceRole(JsepTransport* jsep_transport,
                           const TransportInfo& transport_info,
                           SdpType type,
                           bool local);

  std::unique_ptr<DtlsTransportInternal> CreateDtlsTransport(
      const ContentInfo& content_info,
      bool rtcp);
  scoped_refptr<IceTransportInterface> CreateIceTransport(
      absl::string_view transport_name,
      bool rtcp);
  std::unique_ptr<RtpTransport> CreateUnencryptedRtpTransport(
      absl::string_view transport_name,
      std::unique_ptr<PacketTransportInternal> rtp_packet_transport,
      std::unique_ptr<PacketTransportInternal> rtcp_packet_transport);

  // Creates a DTLS SRTP transport.
  std::unique_ptr<DtlsSrtpTransport> CreateDtlsSrtpTransport(
      absl::string_view transport_name,
      std::unique_ptr<DtlsTransportInternal> rtp_dtls_transport,
      std::unique_ptr<DtlsTransportInternal> rtcp_dtls_transport);

  std::unique_ptr<RtpTransport> CreateRtpTransport(
      absl::string_view transport_name,
      std::unique_ptr<DtlsTransportInternal> rtp_dtls_transport,
      std::unique_ptr<DtlsTransportInternal> rtcp_dtls_transport);

  // Collect all the DtlsTransports, including RTP and RTCP, from the
  // JsepTransports, including those not mapped to a MID because they are being
  // kept alive in case of rollback.
  std::vector<DtlsTransportInternal*> GetDtlsTransports();
  // Same as the above, but doesn't include rollback transports.
  // JsepTransportController can iterate all the DtlsTransports and update the
  // aggregate states.
  std::vector<DtlsTransportInternal*> GetActiveDtlsTransports();

  // Handlers for signals from Transport.
  void OnTransportWritableState_n(PacketTransportInternal* transport)
      RTC_RUN_ON(network_thread_);
  void OnTransportReceivingState_n(PacketTransportInternal* transport)
      RTC_RUN_ON(network_thread_);
  void OnTransportGatheringState_n(IceTransportInternal* transport)
      RTC_RUN_ON(network_thread_);
  void OnTransportCandidateGathered_n(IceTransportInternal* transport,
                                      const Candidate& candidate)
      RTC_RUN_ON(network_thread_);
  void OnTransportCandidateError_n(IceTransportInternal* transport,
                                   const IceCandidateErrorEvent& event)
      RTC_RUN_ON(network_thread_);
  void OnTransportCandidatesRemoved_n(IceTransportInternal* transport,
                                      const Candidates& candidates)
      RTC_RUN_ON(network_thread_);
  void OnTransportRoleConflict_n(IceTransportInternal* transport)
      RTC_RUN_ON(network_thread_);
  void OnDtlsRoleChange_n(DtlsTransportInternal* transport, SSLRole role)
      RTC_RUN_ON(network_thread_);
  void OnTransportStateChanged_n(IceTransportInternal* transport)
      RTC_RUN_ON(network_thread_);
  void OnTransportCandidatePairChanged_n(const CandidatePairChangeEvent& event)
      RTC_RUN_ON(network_thread_);
  void UpdateAggregateStates_n() RTC_RUN_ON(network_thread_);

  void OnRtcpPacketReceived_n(CopyOnWriteBuffer packet,
                              std::optional<Timestamp> arrival_time,
                              EcnMarking ecn) RTC_RUN_ON(network_thread_);
  void OnUnDemuxableRtpPacketReceived_n(const RtpPacketReceived& packet)
      RTC_RUN_ON(network_thread_);

  void OnDtlsHandshakeError(SSLHandshakeError error);

  bool OnTransportChanged(absl::string_view mid, JsepTransport* transport);

  const Environment env_;
  TaskQueueBase* const signaling_thread_;
  Thread* const network_thread_;
  PortAllocator* const port_allocator_ = nullptr;
  AsyncDnsResolverFactoryInterface* const async_dns_resolver_factory_ = nullptr;
  LocalNetworkAccessPermissionFactoryInterface* const lna_permission_factory_ =
      nullptr;

  JsepTransportCollection transports_ RTC_GUARDED_BY(network_thread_);
  // Aggregate states for Transports.
  // standardized_ice_connection_state_ is intended to replace
  // ice_connection_state, see bugs.webrtc.org/9308
  IceConnectionState ice_connection_state_ = kIceConnectionConnecting;
  PeerConnectionInterface::IceConnectionState
      standardized_ice_connection_state_ =
          PeerConnectionInterface::kIceConnectionNew;
  PeerConnectionInterface::PeerConnectionState combined_connection_state_ =
      PeerConnectionInterface::PeerConnectionState::kNew;
  IceGatheringState ice_gathering_state_ = kIceGatheringNew;

  const Config config_;

  bool ice_role_initialized_ RTC_GUARDED_BY(network_thread_) = false;

  IceConfig ice_config_;
  IceRole ice_role_ = ICEROLE_CONTROLLING;
  scoped_refptr<RTCCertificate> certificate_;
  // True once SetLocalCertificate() has accepted a certificate. The
  // certificate itself is only used on the network thread (`certificate_`).
  bool local_certificate_set_ RTC_GUARDED_BY(signaling_thread_) = false;

  BundleManager bundles_;

  // Version of the most recent snapshot taken by GetTransportStates_n().
  uint64_t transport_states_version_n_ RTC_GUARDED_BY(network_thread_) = 0;
  TransportStateSnapshot transport_states_ RTC_GUARDED_BY(signaling_thread_);

  scoped_refptr<PendingTaskSafetyFlag> role_update_safety_flag_s_
      RTC_GUARDED_BY(signaling_thread_);
  scoped_refptr<PendingTaskSafetyFlag> role_update_safety_flag_n_
      RTC_GUARDED_BY(network_thread_);
  // Guards the tasks that are posted to the network thread. Set to not alive
  // on the network thread when the transports are destroyed.
  const absl_nonnull scoped_refptr<PendingTaskSafetyFlag> network_safety_;
};

}  // namespace webrtc

#endif  // PC_JSEP_TRANSPORT_CONTROLLER_H_
