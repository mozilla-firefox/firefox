/*
 *  Copyright 2017 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/jsep_transport_controller.h"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/async_dns_resolver.h"
#include "api/candidate.h"
#include "api/dtls_transport_interface.h"
#include "api/environment/environment.h"
#include "api/ice_transport_interface.h"
#include "api/jsep.h"
#include "api/local_network_access_permission.h"
#include "api/make_ref_counted.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/transport/data_channel_transport_interface.h"
#include "api/transport/ecn_marking.h"
#include "api/transport/enums.h"
#include "api/units/timestamp.h"
#include "media/sctp/sctp_transport_internal.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/p2p_transport_channel.h"
#include "p2p/base/packet_transport_internal.h"
#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/transport_info.h"
#include "p2p/dtls/dtls_transport.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "pc/dtls_srtp_transport.h"
#include "pc/dtls_transport.h"
#include "pc/jsep_transport.h"
#include "pc/media_protocol_names.h"
#include "pc/rtp_transport.h"
#include "pc/rtp_transport_internal.h"
#include "pc/sctp_transport.h"
#include "pc/session_description.h"
#include "pc/transport_stats.h"
#include "rtc_base/checks.h"
#include "rtc_base/containers/flat_map.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/logging.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/ssl_certificate.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/thread.h"
#include "rtc_base/trace_event.h"

namespace webrtc {
namespace {

bool BundleUsesCryptex(const SessionDescription& description,
                       const ContentGroup& bundle_group) {
  for (const std::string& mid : bundle_group.content_names()) {
    const ContentInfo* content = description.GetContentByName(mid);
    if (content && !content->rejected &&
        IsRtpProtocol(content->media_description()->protocol())) {
      return content->media_description()->cryptex();
    }
  }
  return false;
}

}  // namespace

JsepTransportController::JsepTransportController(
    const Environment& env,
    TaskQueueBase* signaling_thread,
    Thread* network_thread,
    PortAllocator* port_allocator,
    AsyncDnsResolverFactoryInterface* async_dns_resolver_factory,
    LocalNetworkAccessPermissionFactoryInterface* lna_permission_factory,
    Config config)
    : env_(env),
      signaling_thread_(signaling_thread),
      network_thread_(network_thread),
      port_allocator_(port_allocator),
      async_dns_resolver_factory_(async_dns_resolver_factory),
      lna_permission_factory_(lna_permission_factory),
      transports_(
          [this](absl::string_view mid, JsepTransport* transport) {
            return OnTransportChanged(mid, transport);
          },
          [this]() {
            RTC_DCHECK_RUN_ON(network_thread_);
            UpdateAggregateStates_n();
          }),
      config_(std::move(config)),
      bundles_(config_.bundle_policy),
      role_update_safety_flag_s_(
          PendingTaskSafetyFlag::CreateAttachedToTaskQueue(true,
                                                           signaling_thread_)),
      role_update_safety_flag_n_(role_update_safety_flag_s_),
      network_safety_(
          PendingTaskSafetyFlag::CreateAttachedToTaskQueue(true,
                                                           network_thread_)) {
  RTC_DCHECK(signaling_thread_);
  RTC_DCHECK(network_thread_);
  RTC_DCHECK(port_allocator_);
  // The `transport_observer` is assumed to be non-null.
  RTC_DCHECK(config_.transport_observer);
  RTC_DCHECK(config_.rtcp_handler);
  RTC_DCHECK(config_.ice_transport_factory);
  RTC_DCHECK(config_.on_dtls_handshake_error);
  RTC_DCHECK(config_.signal_ice_candidates_gathered);
  RTC_DCHECK(config_.signal_ice_connection_state);
  RTC_DCHECK(config_.signal_connection_state);
  RTC_DCHECK(config_.signal_standardized_ice_connection_state);
  RTC_DCHECK(config_.signal_ice_gathering_state);
  RTC_DCHECK(config_.signal_ice_candidate_error);
  RTC_DCHECK(config_.signal_ice_candidates_removed);
  RTC_DCHECK(config_.signal_ice_candidate_pair_changed);
}

JsepTransportController::~JsepTransportController() {
  // Channel destructors may try to send packets, so this needs to happen on
  // the network thread.
  RTC_DCHECK_RUN_ON(network_thread_);
  network_safety_->SetNotAlive();
  DestroyAllJsepTransports_n();
}

absl::AnyInvocable<void() &&> JsepTransportController::MakeCloseTask() {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  role_update_safety_flag_s_->SetNotAlive();

  return [this]() mutable {
    RTC_DCHECK_RUN_ON(network_thread_);
    network_safety_->SetNotAlive();
    DestroyAllJsepTransports_n();
  };
}

RTCError JsepTransportController::SetLocalDescription(
    SdpType type,
    const SessionDescription* local_desc,
    const SessionDescription* remote_desc) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  RTC_DCHECK(local_desc);
  TRACE_EVENT0("webrtc", "JsepTransportController::SetLocalDescription");

  TransportStateSnapshot new_states;
  RTCError error = network_thread_->BlockingCall([&] {
    RTC_DCHECK_RUN_ON(network_thread_);
    RTCError err = SetLocalDescription_n(type, local_desc, remote_desc);
    new_states = GetTransportStates_n();
    return err;
  });
  SetTransportStates(std::move(new_states));
  return error;
}

// RTC_RUN_ON(network_thread_)
RTCError JsepTransportController::SetLocalDescription_n(
    SdpType type,
    const SessionDescription* local_desc,
    const SessionDescription* remote_desc) {
  // Per RFC 8445 Section 6.1.1, the ICE role must remain fixed for the ICE
  // session unless an ICE restart occurs. Set the role based on the initial
  // SetLocalDescription (controlling for offer, controlled for answer).
  // Subsequent descriptions (e.g. renegotiation without ICE restart) must not
  // modify the existing role. When an ICE restart occurs, re-evaluate the role
  // for the new ICE session.
  // Note that ApplyDescription_n() will call DetermineIceRole() which handles
  // special cases such as when the remote endpoint uses ICE-Lite.
  if (config_.redetermine_role_on_ice_restart && HasIceRestart_n(local_desc)) {
    ice_role_initialized_ = false;
  }

  if (!ice_role_initialized_) {
    ice_role_initialized_ = true;
    const IceRole role =
        (type == SdpType::kOffer) ? ICEROLE_CONTROLLING : ICEROLE_CONTROLLED;
    SetIceRole_n(role);
  }
  return ApplyDescription_n(/*local=*/true, type, local_desc, remote_desc);
}

bool JsepTransportController::HasIceRestart_n(
    const SessionDescription* local_desc) const {
  RTC_DCHECK_RUN_ON(network_thread_);
  for (const ContentInfo& content_info : local_desc->contents()) {
    const JsepTransport* transport = GetJsepTransportForMid(content_info.mid());
    const TransportInfo* transport_info =
        local_desc->GetTransportInfoByName(content_info.mid());
    if (!transport || !transport->local_description() || !transport_info) {
      continue;
    }

    const TransportDescription& old_description =
        transport->local_description()->transport_desc;
    const TransportDescription& new_description = transport_info->description;
    if (IceCredentialsChanged(
            old_description.ice_ufrag, old_description.ice_pwd,
            new_description.ice_ufrag, new_description.ice_pwd)) {
      return true;
    }
  }
  return false;
}

RTCError JsepTransportController::SetRemoteDescription(
    SdpType type,
    const SessionDescription* local_desc,
    const SessionDescription* remote_desc) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  RTC_DCHECK(remote_desc);
  TRACE_EVENT0("webrtc", "JsepTransportController::SetRemoteDescription");

  TransportStateSnapshot new_states;
  RTCError error = network_thread_->BlockingCall([&] {
    RTC_DCHECK_RUN_ON(network_thread_);
    RTCError err = SetRemoteDescription_n(type, local_desc, remote_desc);
    new_states = GetTransportStates_n();
    return err;
  });
  SetTransportStates(std::move(new_states));
  return error;
}

RTCError JsepTransportController::SetRemoteDescription_n(
    SdpType type,
    const SessionDescription* local_desc,
    const SessionDescription* remote_desc) {
  return ApplyDescription_n(/*local=*/false, type, local_desc, remote_desc);
}

RtpTransportInternal* JsepTransportController::GetRtpTransport(
    absl::string_view mid) const {
  RTC_DCHECK_RUN_ON(network_thread_);
  auto jsep_transport = GetJsepTransportForMid(mid);
  if (!jsep_transport) {
    return nullptr;
  }
  return jsep_transport->rtp_transport();
}

DataChannelTransportInterface* JsepTransportController::GetDataChannelTransport(
    absl::string_view mid) const {
  RTC_DCHECK_RUN_ON(network_thread_);
  auto jsep_transport = GetJsepTransportForMid(mid);
  if (!jsep_transport) {
    return nullptr;
  }
  return jsep_transport->data_channel_transport();
}

DtlsTransportInternal* JsepTransportController::GetDtlsTransport(
    absl::string_view mid) {
  RTC_DCHECK_RUN_ON(network_thread_);
  auto jsep_transport = GetJsepTransportForMid(mid);
  if (!jsep_transport) {
    return nullptr;
  }
  return jsep_transport->rtp_dtls_transport();
}

scoped_refptr<DtlsTransport>
JsepTransportController::LookupDtlsTransportByMid_n(absl::string_view mid) {
  RTC_DCHECK_RUN_ON(network_thread_);
  auto jsep_transport = GetJsepTransportForMid(mid);
  if (!jsep_transport) {
    return nullptr;
  }
  return jsep_transport->RtpDtlsTransport();
}

scoped_refptr<DtlsTransport> JsepTransportController::LookupDtlsTransportByMid(
    absl::string_view mid) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  return network_thread_->BlockingCall([&] {
    RTC_DCHECK_RUN_ON(network_thread_);
    return LookupDtlsTransportByMid_n(mid);
  });
}

scoped_refptr<SctpTransport> JsepTransportController::GetSctpTransport(
    absl::string_view mid) const {
  RTC_DCHECK_RUN_ON(network_thread_);
  auto jsep_transport = GetJsepTransportForMid(mid);
  if (!jsep_transport) {
    return nullptr;
  }
  return jsep_transport->SctpTransport();
}

void JsepTransportController::SetIceConfig(const IceConfig& config) {
  RTC_DCHECK_RUN_ON(network_thread_);
  ice_config_ = config;
  for (auto& dtls : GetDtlsTransports()) {
    dtls->ice_transport()->SetIceConfig(ice_config_);
  }
}

void JsepTransportController::SetNeedsIceRestartFlag() {
  RTC_DCHECK_RUN_ON(network_thread_);
  transports_.ForEachTransport(
      [](JsepTransport& t) { t.SetNeedsIceRestartFlag(); });
}

bool JsepTransportController::NeedsIceRestart(
    absl::string_view transport_name) const {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  auto it = transport_states_.states.find(std::string(transport_name));
  if (it != transport_states_.states.end()) {
    return it->second.needs_ice_restart;
  }
  return false;
}

std::optional<SSLRole> JsepTransportController::GetDtlsRole(
    absl::string_view mid) const {
  if (signaling_thread_->IsCurrent()) {
    RTC_DCHECK_RUN_ON(signaling_thread_);
    auto it = transport_states_.states.find(std::string(mid));
    if (it != transport_states_.states.end()) {
      return it->second.dtls_role;
    }
    return std::nullopt;
  }

  RTC_DCHECK_RUN_ON(network_thread_);

  const JsepTransport* t = GetJsepTransportForMid(mid);
  if (!t) {
    return std::optional<SSLRole>();
  }
  return t->GetDtlsRole();
}

void JsepTransportController::SetTransportStates(
    TransportStateSnapshot snapshot) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  // Snapshots posted from the network thread (see OnDtlsRoleChange_n()) can
  // arrive after a newer snapshot has been applied, e.g. when they were taken
  // while a description was being applied.
  if (snapshot.version <= transport_states_.version) {
    return;
  }
  transport_states_ = std::move(snapshot);
}

void JsepTransportController::RunOnNetworkThread(
    absl::AnyInvocable<void() &&> task) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  if (network_thread_->IsCurrent()) {
    // Running right away keeps the order with blocking calls, which also run
    // right away when the signaling and network threads are the same thread.
    if (network_safety_->alive()) {
      std::move(task)();
    }
    return;
  }
  // Tasks posted from the signaling thread run in order on the network thread,
  // and before any blocking call that is made later.
  network_thread_->PostTask(SafeTask(network_safety_, std::move(task)));
}

bool JsepTransportController::SetLocalCertificate(
    const scoped_refptr<RTCCertificate>& certificate) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();

  // Can't change a certificate, or set a null certificate.
  if (local_certificate_set_ || !certificate) {
    return false;
  }
  local_certificate_set_ = true;

  // The certificate is applied before any description that is set later.
  RunOnNetworkThread([this, certificate] {
    RTC_DCHECK_RUN_ON(network_thread_);
    SetLocalCertificate_n(certificate);
  });
  return true;
}

void JsepTransportController::SetLocalCertificate_n(
    const absl_nonnull scoped_refptr<RTCCertificate>& certificate) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK(!certificate_);
  certificate_ = certificate;

  // Set certificate for JsepTransport, which verifies it matches the
  // fingerprint in SDP, and DTLS transport.
  // Fallback from DTLS to SDES is not supported.
  transports_.ForEachTransport(
      [&](JsepTransport& t) { t.SetLocalCertificate(certificate_); });

  for (auto& dtls : GetDtlsTransports()) {
    bool set_cert_success = dtls->SetLocalCertificate(certificate_);
    RTC_DCHECK(set_cert_success);
  }
}

scoped_refptr<RTCCertificate> JsepTransportController::GetLocalCertificate(
    absl::string_view transport_name) const {
  RTC_DCHECK_RUN_ON(network_thread_);

  const JsepTransport* t = GetJsepTransportByName(transport_name);
  if (!t) {
    return nullptr;
  }
  return t->GetLocalCertificate();
}

std::unique_ptr<SSLCertChain> JsepTransportController::GetRemoteSSLCertChain(
    absl::string_view transport_name) const {
  RTC_DCHECK_RUN_ON(network_thread_);

  // Get the certificate from the RTP transport's DTLS handshake. Should be
  // identical to the RTCP transport's, since they were given the same remote
  // fingerprint.
  auto jsep_transport = GetJsepTransportByName(transport_name);
  if (!jsep_transport) {
    return nullptr;
  }
  auto dtls = jsep_transport->rtp_dtls_transport();
  if (!dtls) {
    return nullptr;
  }

  return dtls->GetRemoteSSLCertChain();
}

std::vector<IceParameters> JsepTransportController::MaybeStartGathering(
    bool discard_candidate_pool) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  return network_thread_->BlockingCall([&] {
    RTC_DCHECK_RUN_ON(network_thread_);
    MaybeStartGathering_n();
    // Discard the pool only after gathering has started, so that new
    // transports can take pooled sessions first (RFC 8829, section 5.11).
    if (discard_candidate_pool) {
      port_allocator_->DiscardCandidatePool();
    }
    return port_allocator_->GetPooledIceCredentials();
  });
}

void JsepTransportController::MaybeStartGathering_n() {
  RTC_DCHECK_RUN_ON(network_thread_);
  for (auto& dtls : GetDtlsTransports()) {
    dtls->ice_transport()->MaybeStartGathering();
  }
}

RTCError JsepTransportController::AddRemoteCandidates(
    absl::string_view transport_name,
    const Candidates& candidates) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK(VerifyCandidates(candidates).ok());
  auto jsep_transport = GetJsepTransportByName(transport_name);
  if (!jsep_transport) {
    RTC_LOG(LS_WARNING) << "Not adding candidate because the JsepTransport "
                           "doesn't exist. Ignore it.";
    return RTCError::OK();
  }
  return jsep_transport->AddRemoteCandidates(candidates);
}

bool JsepTransportController::RemoveRemoteCandidate(const IceCandidate* c) {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  return network_thread_->BlockingCall([&] {
    RTC_DCHECK_RUN_ON(network_thread_);
    return RemoveRemoteCandidate_n(c);
  });
}

bool JsepTransportController::RemoveRemoteCandidate_n(const IceCandidate* c) {
  RTC_DCHECK_RUN_ON(network_thread_);
  std::string mid = c->sdp_mid();
  if (!VerifyCandidate(c->candidate()).ok() || mid.empty()) {
    RTC_LOG(LS_ERROR) << "Candidate invalid or missing sdp_mid: "
                      << c->candidate().ToSensitiveString();
    return false;
  }
  JsepTransport* jsep_transport = GetJsepTransportForMid(mid);
  if (!jsep_transport) {
    RTC_LOG(LS_WARNING) << "No Transport for mid=" << mid;
    return false;
  }
  DtlsTransportInternal* dtls =
      c->candidate().component() == ICE_CANDIDATE_COMPONENT_RTP
          ? jsep_transport->rtp_dtls_transport()
          : jsep_transport->rtcp_dtls_transport();
  if (dtls) {
    dtls->ice_transport()->RemoveRemoteCandidate(c->candidate());
  }
  return true;
}

bool JsepTransportController::GetStats(absl::string_view transport_name,
                                       TransportStats* stats) const {
  RTC_DCHECK_RUN_ON(network_thread_);

  const JsepTransport* transport = GetJsepTransportByName(transport_name);
  if (!transport) {
    return false;
  }
  return transport->GetStats(stats);
}

RTCError JsepTransportController::RollbackTransports() {
  RTC_DCHECK_RUN_ON(signaling_thread_);
  scoped_refptr<PendingTaskSafetyFlag> new_flag =
      PendingTaskSafetyFlag::CreateAttachedToTaskQueue(true, signaling_thread_);
  TransportStateSnapshot new_states;
  RTCError error = network_thread_->BlockingCall([&] {
    RTC_DCHECK_RUN_ON(network_thread_);
    RTCError err = RollbackTransports_n();
    new_states = GetTransportStates_n();
    role_update_safety_flag_n_ = new_flag;
    return err;
  });
  SetTransportStates(std::move(new_states));
  role_update_safety_flag_s_->SetNotAlive();
  role_update_safety_flag_s_ = std::move(new_flag);
  return error;
}

RTCError JsepTransportController::RollbackTransports_n() {
  bundles_.Rollback();
  if (!transports_.RollbackTransports()) {
    return RTC_LOG_ERROR(RTCError(RTCErrorType::INTERNAL_ERROR)
                         << "Failed to roll back transport state.");
  }
  return RTCError::OK();
}

scoped_refptr<IceTransportInterface>
JsepTransportController::CreateIceTransport(absl::string_view transport_name,
                                            bool rtcp) {
  int component =
      rtcp ? ICE_CANDIDATE_COMPONENT_RTCP : ICE_CANDIDATE_COMPONENT_RTP;

  IceTransportInit init(env_);
  init.set_port_allocator(port_allocator_);
  init.set_async_dns_resolver_factory(async_dns_resolver_factory_);
  init.set_lna_permission_factory(lna_permission_factory_);
  scoped_refptr<IceTransportInterface> transport =
      config_.ice_transport_factory->CreateIceTransport(
          std::string(transport_name), component, std::move(init));

  // Ideally all of the below should be done as part of construction. That way
  // construction could happen on one thread and then safely be used on another
  // without there being an inherent race in the interface.
  IceTransportInternal* internal = transport->internal();

  internal->AddGatheringStateCallback(this,
                                      [this](IceTransportInternal* transport) {
                                        RTC_DCHECK_RUN_ON(network_thread_);
                                        OnTransportGatheringState_n(transport);
                                      });
  internal->SubscribeCandidateGathered(
      this,
      [this](IceTransportInternal* transport, const Candidate& candidate) {
        RTC_DCHECK_RUN_ON(network_thread_);
        OnTransportCandidateGathered_n(transport, candidate);
      });
  internal->SetCandidateErrorCallback(
      [this](IceTransportInternal* transport,
             const IceCandidateErrorEvent& error) {
        RTC_DCHECK_RUN_ON(network_thread_);
        OnTransportCandidateError_n(transport, error);
      });
  internal->SetCandidatesRemovedCallback(
      [this](IceTransportInternal* transport, const Candidates& candidates) {
        RTC_DCHECK_RUN_ON(network_thread_);
        OnTransportCandidatesRemoved_n(transport, candidates);
      });
  internal->SubscribeRoleConflict(this,
                                  [this](IceTransportInternal* transport) {
                                    RTC_DCHECK_RUN_ON(network_thread_);
                                    OnTransportRoleConflict_n(transport);
                                  });
  internal->SubscribeIceTransportStateChanged(
      this, [this](IceTransportInternal* transport) {
        RTC_DCHECK_RUN_ON(network_thread_);
        OnTransportStateChanged_n(transport);
      });
  internal->SetCandidatePairChangeCallback(
      [this](const CandidatePairChangeEvent& event) {
        RTC_DCHECK_RUN_ON(network_thread_);
        OnTransportCandidatePairChanged_n(event);
      });

  internal->SetIceRole(ice_role_);
  internal->SetIceConfig(ice_config_);

  return transport;
}

std::unique_ptr<DtlsTransportInternal>
JsepTransportController::CreateDtlsTransport(const ContentInfo& content_info,
                                             bool rtcp) {
  RTC_DCHECK_RUN_ON(network_thread_);

  scoped_refptr<IceTransportInterface> ice =
      CreateIceTransport(content_info.mid(), rtcp);
  std::unique_ptr<DtlsTransportInternal> dtls =
      config_.dtls_transport_factory
          ? config_.dtls_transport_factory->CreateDtlsTransport(
                std::move(ice), config_.crypto_options, config_.ssl_max_version)
          : std::make_unique<DtlsTransportInternalImpl>(
                env_, std::move(ice), config_.crypto_options,
                config_.ssl_max_version);

  // Ideally all of the below should be done as part of construction. That way
  // construction could happen on one thread and then safely be used on another
  // without there being an inherent race in the interface.
  if (certificate_) {
    bool set_cert_success = dtls->SetLocalCertificate(certificate_);
    RTC_DCHECK(set_cert_success);
  }

  // Connect to signals offered by the DTLS and ICE transport.
  dtls->SubscribeWritableState(this,
                               [this](PacketTransportInternal* transport) {
                                 RTC_DCHECK_RUN_ON(network_thread_);
                                 OnTransportWritableState_n(transport);
                               });
  dtls->SubscribeReceivingState(this,
                                [this](PacketTransportInternal* transport) {
                                  RTC_DCHECK_RUN_ON(network_thread_);
                                  OnTransportReceivingState_n(transport);
                                });
  dtls->SubscribeDtlsHandshakeError(
      this, [this](SSLHandshakeError error) { OnDtlsHandshakeError(error); });
  dtls->SubscribeDtlsRoleChange(
      [this](DtlsTransportInternal* transport, SSLRole role) {
        RTC_DCHECK_RUN_ON(network_thread_);
        OnDtlsRoleChange_n(transport, role);
      });
  return dtls;
}

std::unique_ptr<RtpTransport>
JsepTransportController::CreateUnencryptedRtpTransport(
    absl::string_view transport_name,
    std::unique_ptr<PacketTransportInternal> rtp_packet_transport,
    std::unique_ptr<PacketTransportInternal> rtcp_packet_transport) {
  RTC_DCHECK_RUN_ON(network_thread_);
  auto unencrypted_rtp_transport = std::make_unique<RtpTransport>(
      rtcp_packet_transport == nullptr, env_.field_trials());
  unencrypted_rtp_transport->SetRtpPacketTransportOwned(
      std::move(rtp_packet_transport));
  if (rtcp_packet_transport) {
    unencrypted_rtp_transport->SetRtcpPacketTransportOwned(
        std::move(rtcp_packet_transport));
  }
  return unencrypted_rtp_transport;
}

std::unique_ptr<DtlsSrtpTransport>
JsepTransportController::CreateDtlsSrtpTransport(
    absl::string_view transport_name,
    std::unique_ptr<DtlsTransportInternal> rtp_dtls_transport,
    std::unique_ptr<DtlsTransportInternal> rtcp_dtls_transport) {
  RTC_DCHECK_RUN_ON(network_thread_);
  auto dtls_srtp_transport = std::make_unique<DtlsSrtpTransport>(
      rtcp_dtls_transport == nullptr, env_.field_trials());

  dtls_srtp_transport->SetDtlsTransportsOwned(std::move(rtp_dtls_transport),
                                              std::move(rtcp_dtls_transport));
  // Capturing this in the callback because JsepTransportController will always
  // outlive the DtlsSrtpTransport.
  dtls_srtp_transport->SetOnDtlsStateChange([this]() {
    RTC_DCHECK_RUN_ON(this->network_thread_);
    this->UpdateAggregateStates_n();
  });
  return dtls_srtp_transport;
}

std::unique_ptr<RtpTransport> JsepTransportController::CreateRtpTransport(
    absl::string_view transport_name,
    std::unique_ptr<DtlsTransportInternal> rtp_dtls_transport,
    std::unique_ptr<DtlsTransportInternal> rtcp_dtls_transport) {
  std::unique_ptr<RtpTransport> rtp_transport;
  if (config_.disable_encryption) {
    RTC_LOG(LS_INFO)
        << "Creating UnencryptedRtpTransport, because encryption is disabled.";
    rtp_transport = CreateUnencryptedRtpTransport(
        transport_name, std::move(rtp_dtls_transport),
        std::move(rtcp_dtls_transport));
  } else if (config_.rtp_transport_factory) {
    RTC_LOG(LS_INFO) << "Creating RtpTransport from injected factory.";
    rtp_transport = config_.rtp_transport_factory->CreateRtpTransport(
        transport_name, std::move(rtp_dtls_transport),
        std::move(rtcp_dtls_transport));
  } else {
    RTC_LOG(LS_INFO) << "Creating DtlsSrtpTransport.";
    rtp_transport =
        CreateDtlsSrtpTransport(transport_name, std::move(rtp_dtls_transport),
                                std::move(rtcp_dtls_transport));
  }

  rtp_transport->SubscribeRtcpPacketReceived(
      this, [this](CopyOnWriteBuffer packet,
                   std::optional<Timestamp> arrival_time, EcnMarking ecn) {
        RTC_DCHECK_RUN_ON(network_thread_);
        OnRtcpPacketReceived_n(std::move(packet), arrival_time, ecn);
      });
  rtp_transport->SetUnDemuxableRtpPacketReceivedHandler(
      [this](RtpPacketReceived& packet) {
        RTC_DCHECK_RUN_ON(network_thread_);
        OnUnDemuxableRtpPacketReceived_n(packet);
      });
  return rtp_transport;
}

std::vector<DtlsTransportInternal*>
JsepTransportController::GetDtlsTransports() {
  RTC_DCHECK_RUN_ON(network_thread_);
  std::vector<DtlsTransportInternal*> dtls_transports;
  transports_.ForEachTransport([&](JsepTransport& t) {
    if (t.rtp_dtls_transport()) {
      dtls_transports.push_back(t.rtp_dtls_transport());
    }
    if (t.rtcp_dtls_transport()) {
      dtls_transports.push_back(t.rtcp_dtls_transport());
    }
  });
  return dtls_transports;
}

std::vector<DtlsTransportInternal*>
JsepTransportController::GetActiveDtlsTransports() {
  RTC_DCHECK_RUN_ON(network_thread_);
  std::vector<DtlsTransportInternal*> dtls_transports;
  transports_.ForEachActiveTransport([&](JsepTransport& t) {
    if (t.rtp_dtls_transport()) {
      dtls_transports.push_back(t.rtp_dtls_transport());
    }
    if (t.rtcp_dtls_transport()) {
      dtls_transports.push_back(t.rtcp_dtls_transport());
    }
  });
  return dtls_transports;
}

RTCError JsepTransportController::ApplyDescription_n(
    bool local,
    SdpType type,
    const SessionDescription* local_desc,
    const SessionDescription* remote_desc) {
  TRACE_EVENT0("webrtc", "JsepTransportController::ApplyDescription_n");

  // Stash away the description object that we'll be applying (since this
  // function is used for both local and remote).
  const SessionDescription* description = local ? local_desc : remote_desc;

  RTC_DCHECK(description);

  RTCError error;
  error =
      ValidateAndMaybeUpdateBundleGroups(local, type, local_desc, remote_desc);
  if (!error.ok()) {
    return error;
  }

  std::map<const ContentGroup*, std::vector<RtpHeaderExtensionId>>
      merged_encrypted_extension_ids_by_bundle;
  if (!bundles_.bundle_groups().empty()) {
    merged_encrypted_extension_ids_by_bundle =
        MergeEncryptedHeaderExtensionIdsForBundles(description);
  }

  for (const ContentInfo& content_info : description->contents()) {
    // Don't create transports for rejected m-lines and bundled m-lines.
    if (content_info.rejected ||
        !bundles_.IsFirstMidInGroup(content_info.mid())) {
      continue;
    }
    error = MaybeCreateJsepTransport(local, content_info, *description);
    if (!error.ok()) {
      return error;
    }
  }

  RTC_DCHECK(description->contents().size() ==
             description->transport_infos().size());
  for (size_t i = 0; i < description->contents().size(); ++i) {
    const ContentInfo& content_info = description->contents()[i];
    const TransportInfo& transport_info = description->transport_infos()[i];

    if (content_info.rejected) {
      // This may cause groups to be removed from |bundles_.bundle_groups()|.
      HandleRejectedContent(content_info);
      continue;
    }

    const ContentGroup* established_bundle_group =
        bundles_.LookupGroupByMid(content_info.mid());

    // For bundle members that are not BUNDLE-tagged (not first in the group),
    // configure their transport to be the same as the BUNDLE-tagged transport.
    if (established_bundle_group &&
        content_info.mid() != *established_bundle_group->FirstContentName()) {
      if (!HandleBundledContent(content_info, *established_bundle_group)) {
        return RTCError(RTCErrorType::INVALID_PARAMETER,
                        "Failed to process the bundled m= section with "
                        "mid='" +
                            content_info.mid() + "'.");
      }
    }

    JsepTransport* transport = GetJsepTransportForMid(content_info.mid());
    if (!transport) {
      return RTC_LOG_ERROR(
          RTCError(RTCErrorType::INVALID_PARAMETER)
          << "Could not find transport for m= section with mid='"
          << content_info.mid() << "'");
    }

    if (established_bundle_group &&
        content_info.mid() != *established_bundle_group->FirstContentName()) {
      continue;  // Skip code below.
    }
    // The code below this is only executed for contents that are the
    // leader of their bundle group, or is unbundled.
    error = ValidateContent(content_info);
    if (!error.ok()) {
      return error;
    }

    std::vector<RtpHeaderExtensionId> extension_ids;
    if (established_bundle_group) {
      // If bundled: Check that this is BUNDLE-tagged (first in the group).
      RTC_DCHECK(content_info.mid() ==
                 *established_bundle_group->FirstContentName());
      auto it = merged_encrypted_extension_ids_by_bundle.find(
          established_bundle_group);
      RTC_DCHECK(it != merged_encrypted_extension_ids_by_bundle.end());
      extension_ids = it->second;
    } else {
      extension_ids = GetEncryptedHeaderExtensionIds(content_info);
    }

    SetIceRole_n(DetermineIceRole(transport, transport_info, type, local));

    JsepTransportDescription jsep_description = CreateJsepTransportDescription(
        content_info, transport_info, extension_ids);
    jsep_description.transport_desc.cryptex =
        established_bundle_group
            ? BundleUsesCryptex(*description, *established_bundle_group)
            : content_info.media_description()->cryptex();
    if (local) {
      error =
          transport->SetLocalJsepTransportDescription(jsep_description, type);
    } else {
      error =
          transport->SetRemoteJsepTransportDescription(jsep_description, type);
    }

    if (!error.ok()) {
      return RTC_LOG_ERROR(
          RTCError(RTCErrorType::INVALID_PARAMETER)
          << "Failed to apply the description for m= section with mid='"
          << content_info.mid() << "': " << error.message());
    }
  }
  if (type == SdpType::kAnswer) {
    transports_.CommitTransports();
    bundles_.Commit();
  }
  return RTCError::OK();
}

JsepTransportController::TransportStateSnapshot
JsepTransportController::GetTransportStates_n() {
  RTC_DCHECK_RUN_ON(network_thread_);
  TransportStateSnapshot snapshot;
  snapshot.version = ++transport_states_version_n_;
  flat_map<std::string, TransportState>& states = snapshot.states;
  transports_.ForEachTransport([&](JsepTransport& t) {
    std::optional<SSLRole> role = t.GetDtlsRole();
    bool needs_ice_restart = t.needs_ice_restart();

    TransportState state;
    state.dtls_role = role;
    state.needs_ice_restart = needs_ice_restart;

    bool bundled = false;
    for (const std::unique_ptr<ContentGroup>& bundle_group :
         bundles_.bundle_groups()) {
      const std::string* first_mid = bundle_group->FirstContentName();
      if (first_mid && *first_mid == t.name()) {
        bundled = true;
        for (const std::string& mid : bundle_group->content_names()) {
          states[mid] = state;
        }
        break;
      }
    }
    if (!bundled) {
      states[std::string(t.name())] = state;
    }
  });
  return snapshot;
}

RTCError JsepTransportController::ValidateAndMaybeUpdateBundleGroups(
    bool local,
    SdpType type,
    const SessionDescription* local_desc,
    const SessionDescription* remote_desc) {
  const SessionDescription* description = local ? local_desc : remote_desc;

  RTC_DCHECK(description);

  std::vector<const ContentGroup*> new_bundle_groups =
      description->GetGroupsByName(GROUP_TYPE_BUNDLE);
  // Verify `new_bundle_groups`.
  std::map<std::string, const ContentGroup*> new_bundle_groups_by_mid;
  for (const ContentGroup* new_bundle_group : new_bundle_groups) {
    for (const std::string& content_name : new_bundle_group->content_names()) {
      // The BUNDLE group must not contain a MID that is a member of a different
      // BUNDLE group, or that contains the same MID multiple times.
      if (new_bundle_groups_by_mid.find(content_name) !=
          new_bundle_groups_by_mid.end()) {
        return RTCError(RTCErrorType::INVALID_PARAMETER,
                        "A BUNDLE group contains a MID='" + content_name +
                            "' that is already in a BUNDLE group.");
      }
      new_bundle_groups_by_mid.insert(
          std::make_pair(content_name, new_bundle_group));
      // The BUNDLE group must not contain a MID that no m= section has.
      if (!description->GetContentByName(content_name)) {
        return RTCError(RTCErrorType::INVALID_PARAMETER,
                        "A BUNDLE group contains a MID='" + content_name +
                            "' matching no m= section.");
      }
    }
  }

  if (type == SdpType::kOffer) {
    // For an offer, we need to verify that there is not a conflicting mapping
    // between existing and new bundle groups. For example, if the existing
    // groups are [[1,2],[3,4]] and new are [[1,3],[2,4]] or [[1,2,3,4]], or
    // vice versa. Switching things around like this requires a separate offer
    // that removes the relevant sections from their group, as per RFC 8843,
    // section 7.5.2.
    std::map<const ContentGroup*, const ContentGroup*>
        new_bundle_groups_by_existing_bundle_groups;
    std::map<const ContentGroup*, const ContentGroup*>
        existing_bundle_groups_by_new_bundle_groups;
    for (const ContentGroup* new_bundle_group : new_bundle_groups) {
      for (const std::string& mid : new_bundle_group->content_names()) {
        ContentGroup* existing_bundle_group = bundles_.LookupGroupByMid(mid);
        if (!existing_bundle_group) {
          continue;
        }
        auto it = new_bundle_groups_by_existing_bundle_groups.find(
            existing_bundle_group);
        if (it != new_bundle_groups_by_existing_bundle_groups.end() &&
            it->second != new_bundle_group) {
          return RTCError(RTCErrorType::INVALID_PARAMETER,
                          "MID " + mid + " in the offer has changed group.");
        }
        new_bundle_groups_by_existing_bundle_groups.insert(
            std::make_pair(existing_bundle_group, new_bundle_group));
        it = existing_bundle_groups_by_new_bundle_groups.find(new_bundle_group);
        if (it != existing_bundle_groups_by_new_bundle_groups.end() &&
            it->second != existing_bundle_group) {
          return RTCError(RTCErrorType::INVALID_PARAMETER,
                          "MID " + mid + " in the offer has changed group.");
        }
        existing_bundle_groups_by_new_bundle_groups.insert(
            std::make_pair(new_bundle_group, existing_bundle_group));
      }
    }
  } else if (type == SdpType::kAnswer) {
    if ((local && remote_desc) || (!local && local_desc)) {
      std::vector<const ContentGroup*> offered_bundle_groups =
          local ? remote_desc->GetGroupsByName(GROUP_TYPE_BUNDLE)
                : local_desc->GetGroupsByName(GROUP_TYPE_BUNDLE);

      std::map<std::string, const ContentGroup*> offered_bundle_groups_by_mid;
      for (const ContentGroup* offered_bundle_group : offered_bundle_groups) {
        for (const std::string& content_name :
             offered_bundle_group->content_names()) {
          offered_bundle_groups_by_mid[content_name] = offered_bundle_group;
        }
      }

      std::map<const ContentGroup*, const ContentGroup*>
          new_bundle_groups_by_offered_bundle_groups;
      for (const ContentGroup* new_bundle_group : new_bundle_groups) {
        if (!new_bundle_group->FirstContentName()) {
          // Empty groups could be a subset of any group.
          continue;
        }
        // The group in the answer (new_bundle_group) must have a corresponding
        // group in the offer (original_group), because the answer groups may
        // only be subsets of the offer groups.
        auto it = offered_bundle_groups_by_mid.find(
            *new_bundle_group->FirstContentName());
        if (it == offered_bundle_groups_by_mid.end()) {
          return RTCError(RTCErrorType::INVALID_PARAMETER,
                          "A BUNDLE group was added in the answer that did not "
                          "exist in the offer.");
        }
        const ContentGroup* offered_bundle_group = it->second;
        if (new_bundle_groups_by_offered_bundle_groups.find(
                offered_bundle_group) !=
            new_bundle_groups_by_offered_bundle_groups.end()) {
          return RTCError(RTCErrorType::INVALID_PARAMETER,
                          "A MID in the answer has changed group.");
        }
        new_bundle_groups_by_offered_bundle_groups.insert(
            std::make_pair(offered_bundle_group, new_bundle_group));
        for (const std::string& content_name :
             new_bundle_group->content_names()) {
          it = offered_bundle_groups_by_mid.find(content_name);
          // The BUNDLE group in answer should be a subset of offered group.
          if (it == offered_bundle_groups_by_mid.end() ||
              it->second != offered_bundle_group) {
            return RTCError(RTCErrorType::INVALID_PARAMETER,
                            "A BUNDLE group in answer contains a MID='" +
                                content_name +
                                "' that was not in the offered group.");
          }
        }
      }

      for (const auto& bundle_group : bundles_.bundle_groups()) {
        for (const std::string& content_name : bundle_group->content_names()) {
          // An answer that removes m= sections from pre-negotiated BUNDLE group
          // without rejecting it, is invalid.
          auto it = new_bundle_groups_by_mid.find(content_name);
          if (it == new_bundle_groups_by_mid.end()) {
            auto* content_info = description->GetContentByName(content_name);
            if (!content_info || !content_info->rejected) {
              return RTCError(RTCErrorType::INVALID_PARAMETER,
                              "Answer cannot remove m= section with mid='" +
                                  content_name +
                                  "' from already-established BUNDLE group.");
            }
          }
        }
      }
    }
  }

  if (config_.bundle_policy ==
          PeerConnectionInterface::kBundlePolicyMaxBundle &&
      !description->HasGroup(GROUP_TYPE_BUNDLE) &&
      description->contents().size() > 1) {
    return RTCError(RTCErrorType::INVALID_PARAMETER,
                    "max-bundle is used but no bundle group found.");
  }

  bundles_.Update(description, type);

  for (const auto& bundle_group : bundles_.bundle_groups()) {
    if (!bundle_group->FirstContentName())
      continue;

    // The first MID in a BUNDLE group is BUNDLE-tagged.
    auto bundled_content =
        description->GetContentByName(*bundle_group->FirstContentName());
    if (!bundled_content) {
      return RTCError(
          RTCErrorType::INVALID_PARAMETER,
          "An m= section associated with the BUNDLE-tag doesn't exist.");
    }

    // If the `bundled_content` is rejected, other contents in the bundle group
    // must also be rejected.
    if (bundled_content->rejected) {
      for (const auto& content_name : bundle_group->content_names()) {
        auto other_content = description->GetContentByName(content_name);
        if (!other_content) {
          return RTCError(RTCErrorType::INVALID_PARAMETER,
                          "A BUNDLE group contains a MID='" + content_name +
                              "' matching no m= section.");
        }
        if (!other_content->rejected) {
          return RTCError(RTCErrorType::INVALID_PARAMETER,
                          "The m= section with mid='" + content_name +
                              "' should be rejected.");
        }
      }
    }
  }
  return RTCError::OK();
}

RTCError JsepTransportController::ValidateContent(
    const ContentInfo& content_info) {
  if (config_.rtcp_mux_policy ==
          PeerConnectionInterface::kRtcpMuxPolicyRequire &&
      content_info.type == MediaProtocolType::kRtp &&
      !content_info.bundle_only &&
      !content_info.media_description()->rtcp_mux()) {
    return RTCError(RTCErrorType::INVALID_PARAMETER,
                    "The m= section with mid='" + content_info.mid() +
                        "' is invalid. RTCP-MUX is not "
                        "enabled when it is required.");
  }
  return RTCError::OK();
}

void JsepTransportController::HandleRejectedContent(
    const ContentInfo& content_info) {
  // If the content is rejected, let the
  // BaseChannel/SctpTransport change the RtpTransport/DtlsTransport first,
  // then destroy the JsepTransport.
  ContentGroup* bundle_group = bundles_.LookupGroupByMid(content_info.mid());
  if (bundle_group && !bundle_group->content_names().empty() &&
      content_info.mid() == *bundle_group->FirstContentName()) {
    // Rejecting a BUNDLE group's first mid means we are rejecting the entire
    // group.
    for (const auto& content_name : bundle_group->content_names()) {
      transports_.RemoveTransportForMid(content_name);
    }
    // Delete the BUNDLE group.
    bundles_.DeleteGroup(bundle_group);
  } else {
    transports_.RemoveTransportForMid(content_info.mid());
    if (bundle_group) {
      // Remove the rejected content from the `bundle_group`.
      bundles_.DeleteMid(bundle_group, content_info.mid());
    }
  }
}

bool JsepTransportController::HandleBundledContent(
    const ContentInfo& content_info,
    const ContentGroup& bundle_group) {
  TRACE_EVENT0("webrtc", "JsepTransportController::HandleBundledContent");
  RTC_DCHECK(bundle_group.FirstContentName());
  auto jsep_transport =
      GetJsepTransportByName(*bundle_group.FirstContentName());
  RTC_DCHECK(jsep_transport);
  // If the content is bundled, let the
  // BaseChannel/SctpTransport change the RtpTransport/DtlsTransport first,
  // then destroy the JsepTransport.
  // TODO(bugs.webrtc.org/9719) For media transport this is far from ideal,
  // because it means that we first create media transport and start
  // connecting it, and then we destroy it. We will need to address it before
  // video path is enabled.
  return transports_.SetTransportForMid(content_info.mid(), jsep_transport);
}

JsepTransportDescription
JsepTransportController::CreateJsepTransportDescription(
    const ContentInfo& content_info,
    const TransportInfo& transport_info,
    const std::vector<RtpHeaderExtensionId>& encrypted_extension_ids) {
  TRACE_EVENT0("webrtc",
               "JsepTransportController::CreateJsepTransportDescription");
  const MediaContentDescription* content_desc =
      content_info.media_description();
  RTC_DCHECK(content_desc);
  bool rtcp_mux_enabled = content_info.type == MediaProtocolType::kSctp
                              ? true
                              : content_desc->rtcp_mux();

  return JsepTransportDescription(rtcp_mux_enabled, encrypted_extension_ids,
                                  transport_info.description);
}

std::vector<RtpHeaderExtensionId>
JsepTransportController::GetEncryptedHeaderExtensionIds(
    const ContentInfo& content_info) {
  const MediaContentDescription* content_desc =
      content_info.media_description();

  if (!config_.crypto_options.srtp.enable_encrypted_rtp_header_extensions) {
    return std::vector<RtpHeaderExtensionId>();
  }

  std::vector<RtpHeaderExtensionId> encrypted_header_extension_ids;
  for (const auto& extension : content_desc->rtp_header_extensions()) {
    if (!extension.encrypt) {
      continue;
    }
    if (!absl::c_linear_search(encrypted_header_extension_ids, extension.id)) {
      encrypted_header_extension_ids.push_back(extension.id);
    }
  }
  return encrypted_header_extension_ids;
}

std::map<const ContentGroup*, std::vector<RtpHeaderExtensionId>>
JsepTransportController::MergeEncryptedHeaderExtensionIdsForBundles(
    const SessionDescription* description) {
  RTC_DCHECK(description);
  RTC_DCHECK(!bundles_.bundle_groups().empty());
  std::map<const ContentGroup*, std::vector<RtpHeaderExtensionId>>
      merged_encrypted_extension_ids_by_bundle;
  // Union the encrypted header IDs in the group when bundle is enabled.
  for (const ContentInfo& content_info : description->contents()) {
    auto group = bundles_.LookupGroupByMid(content_info.mid());
    if (!group)
      continue;
    // Get or create list of IDs for the BUNDLE group.
    std::vector<RtpHeaderExtensionId>& merged_ids =
        merged_encrypted_extension_ids_by_bundle[group];
    // Add IDs not already in the list.
    std::vector<RtpHeaderExtensionId> extension_ids =
        GetEncryptedHeaderExtensionIds(content_info);
    for (RtpHeaderExtensionId id : extension_ids) {
      if (!absl::c_linear_search(merged_ids, id)) {
        merged_ids.push_back(id);
      }
    }
  }
  return merged_encrypted_extension_ids_by_bundle;
}

const JsepTransport* JsepTransportController::GetJsepTransportForMid(
    absl::string_view mid) const {
  return transports_.GetTransportForMid(mid);
}

JsepTransport* JsepTransportController::GetJsepTransportForMid(
    absl::string_view mid) {
  return transports_.GetTransportForMid(mid);
}

const JsepTransport* JsepTransportController::GetJsepTransportByName(
    absl::string_view transport_name) const {
  return transports_.GetTransportByName(transport_name);
}

JsepTransport* JsepTransportController::GetJsepTransportByName(
    absl::string_view transport_name) {
  return transports_.GetTransportByName(transport_name);
}

RTCError JsepTransportController::MaybeCreateJsepTransport(
    bool local,
    const ContentInfo& content_info,
    const SessionDescription& description) {
  JsepTransport* transport = GetJsepTransportByName(content_info.mid());
  if (transport) {
    return RTCError::OK();
  }

  std::unique_ptr<DtlsTransportInternal> rtp_dtls_transport =
      CreateDtlsTransport(content_info, /*rtcp=*/false);
  DtlsTransportInternal* rtp_dtls_transport_ptr = rtp_dtls_transport.get();

  std::unique_ptr<DtlsTransportInternal> rtcp_dtls_transport;
  if (config_.rtcp_mux_policy !=
          PeerConnectionInterface::kRtcpMuxPolicyRequire &&
      content_info.type == MediaProtocolType::kRtp) {
    rtcp_dtls_transport = CreateDtlsTransport(content_info, /*rtcp=*/true);
  }

  std::unique_ptr<RtpTransport> rtp_transport =
      CreateRtpTransport(content_info.mid(), std::move(rtp_dtls_transport),
                         std::move(rtcp_dtls_transport));

  std::unique_ptr<SctpTransportInternal> sctp_transport;
  if (config_.sctp_factory) {
    sctp_transport =
        config_.sctp_factory->CreateSctpTransport(env_, rtp_dtls_transport_ptr);
  }

  scoped_refptr<DtlsTransport> dtls_transport =
      make_ref_counted<DtlsTransport>(rtp_dtls_transport_ptr);

  std::unique_ptr<JsepTransport> jsep_transport =
      std::make_unique<JsepTransport>(
          certificate_, std::move(rtp_transport), std::move(dtls_transport),
          std::move(sctp_transport),
          [&]() {
            RTC_DCHECK_RUN_ON(network_thread_);
            UpdateAggregateStates_n();
          },
          config_.crypto_options.srtp.cryptex_policy);

  // Object hierarchy for objects injected into JsepTransport:
  //
  //  rtp_transport
  //    -> dtls_srtp_transport / unencrypted_rtp_transport
  //      -> rtp_dtls_transport
  //        -> ice->internal()  <- ownership
  //      -> rtcp_dtls_transport
  //        -> ice->internal() <- ownership (rtcp_ice)
  //    -> sctp_transport
  //      -> rtp_dtls_transport <- raw pointer.
  //  dtls_transport (reference counted)
  //    -> rtp_dtls_transport <- raw pointer.
  //
  // TODO(tommi): At this point `jsep_transport` has been constructed
  // and initialized with callbacks subscribed to. Write and call
  // a new method, e.g. jsep_transport->InitializationDone().
  // At that point, all the transport objects should detach any sequence
  // checkers and be ready for use on a different TQ.
  //
  // Since there are still raw pointers handed to some of the transports,
  // and since `dtls_transport` is reference counted, teardown then needs
  // to be explicit as it currently is in ~JsepTransport() where Clear() is
  // called.

  transports_.RegisterTransport(std::move(jsep_transport));
  UpdateAggregateStates_n();
  return RTCError::OK();
}

void JsepTransportController::DestroyAllJsepTransports_n() {
  transports_.DestroyAllTransports();
}

void JsepTransportController::SetIceRole_n(IceRole ice_role) {
  ice_role_ = ice_role;
  auto dtls_transports = GetDtlsTransports();
  for (auto& dtls : dtls_transports) {
    dtls->ice_transport()->SetIceRole(ice_role_);
  }
}

IceRole JsepTransportController::DetermineIceRole(
    JsepTransport* jsep_transport,
    const TransportInfo& transport_info,
    SdpType type,
    bool local) {
  IceRole ice_role = ice_role_;
  auto tdesc = transport_info.description;
  if (local) {
    // The initial offer side may use ICE Lite, in which case, per RFC5245
    // Section 5.1.1, the answer side should take the controlling role if it is
    // in the full ICE mode.
    //
    // When both sides use ICE Lite, the initial offer side must take the
    // controlling role, and this is the default logic implemented in
    // SetLocalDescription in JsepTransportController.
    if (jsep_transport->remote_description() &&
        jsep_transport->remote_description()->transport_desc.ice_mode ==
            ICEMODE_LITE &&
        ice_role_ == ICEROLE_CONTROLLED && tdesc.ice_mode == ICEMODE_FULL) {
      ice_role = ICEROLE_CONTROLLING;
    }
  } else {
    // If our role is ICEROLE_CONTROLLED and the remote endpoint
    // supports only ice_lite, this local endpoint should take the CONTROLLING
    // role.
    // TODO(deadbeef): This is a session-level attribute, so it really shouldn't
    // be in a TransportDescription in the first place...
    if (ice_role_ == ICEROLE_CONTROLLED && tdesc.ice_mode == ICEMODE_LITE) {
      ice_role = ICEROLE_CONTROLLING;
    }

    // If we use ICE Lite and the remote endpoint uses the full implementation
    // of ICE, the local endpoint must take the controlled role, and the other
    // side must be the controlling role.
    if (jsep_transport->local_description() &&
        jsep_transport->local_description()->transport_desc.ice_mode ==
            ICEMODE_LITE &&
        ice_role_ == ICEROLE_CONTROLLING && tdesc.ice_mode == ICEMODE_FULL) {
      ice_role = ICEROLE_CONTROLLED;
    }
  }

  return ice_role;
}

void JsepTransportController::OnTransportWritableState_n(
    PacketTransportInternal* transport) {
  RTC_LOG(LS_INFO) << " Transport " << transport->transport_name()
                   << " writability changed to " << transport->writable()
                   << ".";
  UpdateAggregateStates_n();
}

void JsepTransportController::OnTransportReceivingState_n(
    PacketTransportInternal* transport) {
  UpdateAggregateStates_n();
}

void JsepTransportController::OnTransportGatheringState_n(
    IceTransportInternal* transport) {
  UpdateAggregateStates_n();
}

void JsepTransportController::OnTransportCandidateGathered_n(
    IceTransportInternal* transport,
    const Candidate& candidate) {
  // We should never signal peer-reflexive candidates.
  if (candidate.is_prflx()) {
    RTC_DCHECK_NOTREACHED();
    return;
  }

  config_.signal_ice_candidates_gathered(transport->transport_name(),
                                         std::vector<Candidate>{candidate});
}

void JsepTransportController::OnTransportCandidateError_n(
    IceTransportInternal* transport,
    const IceCandidateErrorEvent& event) {
  config_.signal_ice_candidate_error(event);
}
void JsepTransportController::OnTransportCandidatesRemoved_n(
    IceTransportInternal* transport,
    const Candidates& candidates) {
  config_.signal_ice_candidates_removed(transport, candidates);
}
void JsepTransportController::OnTransportCandidatePairChanged_n(
    const CandidatePairChangeEvent& event) {
  RTC_DCHECK(!event.transport_name.empty());
  config_.signal_ice_candidate_pair_changed(event);
}

void JsepTransportController::OnTransportRoleConflict_n(
    IceTransportInternal* transport) {
  // Note: since the role conflict is handled entirely on the network thread,
  // we don't need to worry about role conflicts occurring on two ports at
  // once. The first one encountered should immediately reverse the role.
  IceRole reversed_role = (ice_role_ == ICEROLE_CONTROLLING)
                              ? ICEROLE_CONTROLLED
                              : ICEROLE_CONTROLLING;
  RTC_LOG(LS_INFO) << "Got role conflict; switching to "
                   << (reversed_role == ICEROLE_CONTROLLING ? "controlling"
                                                            : "controlled")
                   << " role.";
  SetIceRole_n(reversed_role);
}

void JsepTransportController::OnTransportStateChanged_n(
    IceTransportInternal* transport) {
  RTC_LOG(LS_INFO) << transport->transport_name() << " Transport "
                   << transport->component()
                   << " state changed. Check if state is complete.";
  UpdateAggregateStates_n();
}

void JsepTransportController::UpdateAggregateStates_n() {
  TRACE_EVENT0("webrtc", "JsepTransportController::UpdateAggregateStates_n");
  auto dtls_transports = GetActiveDtlsTransports();
  IceConnectionState new_connection_state = kIceConnectionConnecting;
  PeerConnectionInterface::IceConnectionState new_ice_connection_state =
      PeerConnectionInterface::IceConnectionState::kIceConnectionNew;
  PeerConnectionInterface::PeerConnectionState new_combined_state =
      PeerConnectionInterface::PeerConnectionState::kNew;
  IceGatheringState new_gathering_state = kIceGatheringNew;
  bool any_failed = false;
  bool all_connected = !dtls_transports.empty();
  bool all_completed = !dtls_transports.empty();
  bool any_gathering = false;
  bool all_done_gathering = !dtls_transports.empty();

  std::map<IceTransportState, int> ice_state_counts;
  std::map<DtlsTransportState, int> dtls_state_counts;

  for (const auto& dtls : dtls_transports) {
    any_failed = any_failed || dtls->ice_transport()->GetState() ==
                                   IceTransportStateInternal::STATE_FAILED;
    all_connected = all_connected && dtls->writable();
    all_completed =
        all_completed && dtls->writable() &&
        dtls->ice_transport()->GetState() ==
            IceTransportStateInternal::STATE_COMPLETED &&
        dtls->ice_transport()->GetIceRole() == ICEROLE_CONTROLLING &&
        dtls->ice_transport()->gathering_state() == kIceGatheringComplete;
    any_gathering = any_gathering || dtls->ice_transport()->gathering_state() !=
                                         kIceGatheringNew;
    all_done_gathering =
        all_done_gathering &&
        dtls->ice_transport()->gathering_state() == kIceGatheringComplete;

    dtls_state_counts[dtls->dtls_state()]++;
    ice_state_counts[dtls->ice_transport()->GetIceTransportState()]++;
  }

  if (any_failed) {
    new_connection_state = kIceConnectionFailed;
  } else if (all_completed) {
    new_connection_state = kIceConnectionCompleted;
  } else if (all_connected) {
    new_connection_state = kIceConnectionConnected;
  }
  if (ice_connection_state_ != new_connection_state) {
    ice_connection_state_ = new_connection_state;
    config_.signal_ice_connection_state(new_connection_state);
  }

  // Compute the current RTCIceConnectionState as described in
  // https://www.w3.org/TR/webrtc/#dom-rtciceconnectionstate.
  // The PeerConnection is responsible for handling the "closed" state.
  int total_ice_checking = ice_state_counts[IceTransportState::kChecking];
  int total_ice_connected = ice_state_counts[IceTransportState::kConnected];
  int total_ice_completed = ice_state_counts[IceTransportState::kCompleted];
  int total_ice_failed = ice_state_counts[IceTransportState::kFailed];
  int total_ice_disconnected =
      ice_state_counts[IceTransportState::kDisconnected];
  int total_ice_closed = ice_state_counts[IceTransportState::kClosed];
  int total_ice_new = ice_state_counts[IceTransportState::kNew];
  int total_ice = dtls_transports.size();

  if (total_ice_failed > 0) {
    // Any RTCIceTransports are in the "failed" state.
    new_ice_connection_state = PeerConnectionInterface::kIceConnectionFailed;
  } else if (total_ice_disconnected > 0) {
    // None of the previous states apply and any RTCIceTransports are in the
    // "disconnected" state.
    new_ice_connection_state =
        PeerConnectionInterface::kIceConnectionDisconnected;
  } else if (total_ice_new + total_ice_closed == total_ice) {
    // None of the previous states apply and all RTCIceTransports are in the
    // "new" or "closed" state, or there are no transports.
    new_ice_connection_state = PeerConnectionInterface::kIceConnectionNew;
  } else if (total_ice_new + total_ice_checking > 0) {
    // None of the previous states apply and any RTCIceTransports are in the
    // "new" or "checking" state.
    new_ice_connection_state = PeerConnectionInterface::kIceConnectionChecking;
  } else if (total_ice_completed + total_ice_closed == total_ice ||
             all_completed) {
    // None of the previous states apply and all RTCIceTransports are in the
    // "completed" or "closed" state.
    //
    // TODO(https://bugs.webrtc.org/10356): The all_completed condition is added
    // to mimic the behavior of the old ICE connection state, and should be
    // removed once we get end-of-candidates signaling in place.
    new_ice_connection_state = PeerConnectionInterface::kIceConnectionCompleted;
  } else if (total_ice_connected + total_ice_completed + total_ice_closed ==
             total_ice) {
    // None of the previous states apply and all RTCIceTransports are in the
    // "connected", "completed" or "closed" state.
    new_ice_connection_state = PeerConnectionInterface::kIceConnectionConnected;
  } else {
    RTC_DCHECK_NOTREACHED();
  }

  if (standardized_ice_connection_state_ != new_ice_connection_state) {
    if (standardized_ice_connection_state_ ==
            PeerConnectionInterface::kIceConnectionChecking &&
        new_ice_connection_state ==
            PeerConnectionInterface::kIceConnectionCompleted) {
      // Ensure that we never skip over the "connected" state.
      config_.signal_standardized_ice_connection_state(
          PeerConnectionInterface::kIceConnectionConnected);
    }
    standardized_ice_connection_state_ = new_ice_connection_state;
    config_.signal_standardized_ice_connection_state(new_ice_connection_state);
  }

  // Compute the current RTCPeerConnectionState as described in
  // https://www.w3.org/TR/webrtc/#dom-rtcpeerconnectionstate.
  // The PeerConnection is responsible for handling the "closed" state.
  // Note that "connecting" is only a valid state for DTLS transports while
  // "checking", "completed" and "disconnected" are only valid for ICE
  // transports.
  int total_connected =
      total_ice_connected + dtls_state_counts[DtlsTransportState::kConnected];
  int total_dtls_connecting =
      dtls_state_counts[DtlsTransportState::kConnecting];
  int total_failed =
      total_ice_failed + dtls_state_counts[DtlsTransportState::kFailed];
  int total_closed =
      total_ice_closed + dtls_state_counts[DtlsTransportState::kClosed];
  int total_new = total_ice_new + dtls_state_counts[DtlsTransportState::kNew];
  int total_transports = total_ice * 2;

  if (total_failed > 0) {
    // Any of the RTCIceTransports or RTCDtlsTransports are in a "failed" state.
    new_combined_state = PeerConnectionInterface::PeerConnectionState::kFailed;
  } else if (total_ice_disconnected > 0) {
    // None of the previous states apply and any RTCIceTransports or
    // RTCDtlsTransports are in the "disconnected" state.
    new_combined_state =
        PeerConnectionInterface::PeerConnectionState::kDisconnected;
  } else if (total_new + total_closed == total_transports) {
    // None of the previous states apply and all RTCIceTransports and
    // RTCDtlsTransports are in the "new" or "closed" state, or there are no
    // transports.
    new_combined_state = PeerConnectionInterface::PeerConnectionState::kNew;
  } else if (total_new + total_dtls_connecting + total_ice_checking > 0) {
    // None of the previous states apply and all RTCIceTransports or
    // RTCDtlsTransports are in the "new", "connecting" or "checking" state.
    new_combined_state =
        PeerConnectionInterface::PeerConnectionState::kConnecting;
  } else if (total_connected + total_ice_completed + total_closed ==
             total_transports) {
    // None of the previous states apply and all RTCIceTransports and
    // RTCDtlsTransports are in the "connected", "completed" or "closed" state.
    new_combined_state =
        PeerConnectionInterface::PeerConnectionState::kConnected;
  } else {
    RTC_DCHECK_NOTREACHED();
  }

  if (combined_connection_state_ != new_combined_state) {
    combined_connection_state_ = new_combined_state;
    config_.signal_connection_state(new_combined_state);
  }

  // Compute the gathering state.
  if (dtls_transports.empty()) {
    new_gathering_state = kIceGatheringNew;
  } else if (all_done_gathering) {
    new_gathering_state = kIceGatheringComplete;
  } else if (any_gathering) {
    new_gathering_state = kIceGatheringGathering;
  }
  if (ice_gathering_state_ != new_gathering_state) {
    ice_gathering_state_ = new_gathering_state;
    config_.signal_ice_gathering_state(new_gathering_state);
  }
}

void JsepTransportController::OnRtcpPacketReceived_n(
    CopyOnWriteBuffer packet,
    std::optional<Timestamp> arrival_time,
    EcnMarking ecn) {
  RTC_DCHECK(config_.rtcp_handler);
  config_.rtcp_handler(std::move(packet),
                       arrival_time.has_value() ? arrival_time->us() : -1);
}

void JsepTransportController::OnUnDemuxableRtpPacketReceived_n(
    const RtpPacketReceived& packet) {
  RTC_DCHECK(config_.un_demuxable_packet_handler);
  config_.un_demuxable_packet_handler(packet);
}

void JsepTransportController::OnDtlsRoleChange_n(
    DtlsTransportInternal* transport,
    SSLRole role) {
  RTC_DCHECK_RUN_ON(network_thread_);
  TransportStateSnapshot new_states = GetTransportStates_n();
  signaling_thread_->PostTask(
      SafeTask(role_update_safety_flag_n_,
               [this, new_states = std::move(new_states)]() mutable {
                 RTC_DCHECK_RUN_ON(signaling_thread_);
                 SetTransportStates(std::move(new_states));
               }));
}

void JsepTransportController::OnDtlsHandshakeError(SSLHandshakeError error) {
  config_.on_dtls_handshake_error(error);
}

bool JsepTransportController::OnTransportChanged(
    absl::string_view mid,
    JsepTransport* jsep_transport) {
  if (config_.transport_observer) {
    if (jsep_transport) {
      return config_.transport_observer->OnTransportChanged(
          mid, jsep_transport->rtp_transport(),
          jsep_transport->RtpDtlsTransport(),
          jsep_transport->data_channel_transport());
    } else {
      return config_.transport_observer->OnTransportChanged(mid, nullptr,
                                                            nullptr, nullptr);
    }
  }
  return false;
}

}  // namespace webrtc
