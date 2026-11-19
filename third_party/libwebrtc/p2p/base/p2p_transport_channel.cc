/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "p2p/base/p2p_transport_channel.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/functional/any_invocable.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "api/async_dns_resolver.h"
#include "api/candidate.h"
#include "api/environment/environment.h"
#include "api/field_trials_view.h"
#include "api/ice_transport_interface.h"
#include "api/local_network_access_permission.h"
#include "api/rtc_error.h"
#include "api/sequence_checker.h"
#include "api/task_queue/task_queue_base.h"
#include "api/transport/enums.h"
#include "api/transport/stun.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "logging/rtc_event_log/events/rtc_event_ice_candidate_pair_config.h"
#include "logging/rtc_event_log/ice_logger.h"
#include "p2p/base/active_ice_controller_factory_interface.h"
#include "p2p/base/candidate_pair_interface.h"
#include "p2p/base/connection.h"
#include "p2p/base/connection_info.h"
#include "p2p/base/ice_controller_factory_interface.h"
#include "p2p/base/ice_switch_reason.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/port_interface.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/wrapping_active_ice_controller.h"
#include "p2p/dtls/dtls_stun_piggyback_callbacks.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/checks.h"
#include "rtc_base/dscp.h"
#include "rtc_base/experiments/rfc8888_congestion_control_feedback_experiment.h"
#include "rtc_base/experiments/struct_parameters_parser.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/network.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/network_constants.h"
#include "rtc_base/network_route.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/task_utils/repeating_task.h"
#include "rtc_base/trace_event.h"
#include "system_wrappers/include/metrics.h"

namespace webrtc {
namespace {

PortInterface::CandidateOrigin GetOrigin(PortInterface* port,
                                         PortInterface* origin_port) {
  if (!origin_port)
    return PortInterface::ORIGIN_MESSAGE;
  else if (port == origin_port)
    return PortInterface::ORIGIN_THIS_PORT;
  else
    return PortInterface::ORIGIN_OTHER_PORT;
}

RouteEndpoint CreateRouteEndpointFromCandidate(bool local,
                                               const Candidate& candidate,
                                               bool uses_turn) {
  auto adapter_type = candidate.network_type();
  if (!local && adapter_type == ADAPTER_TYPE_UNKNOWN) {
    std::tie(adapter_type, std::ignore, std::ignore) =
        Network::GuessAdapterFromNetworkCost(candidate.network_cost());
  }

  // TODO(bugs.webrtc.org/9446) : Rewrite if information about remote network
  // adapter becomes available. The implication of this implementation is that
  // we will only ever report 1 adapter per type. In practice this is probably
  // fine, since the endpoint also contains network-id.
  uint16_t adapter_id = static_cast<int>(adapter_type);
  return RouteEndpoint(adapter_type, adapter_id, candidate.network_id(),
                       uses_turn);
}

}  // unnamed namespace

bool IceCredentialsChanged(absl::string_view old_ufrag,
                           absl::string_view old_pwd,
                           absl::string_view new_ufrag,
                           absl::string_view new_pwd) {
  // The standard (RFC 5245 Section 9.1.1.1) says that ICE restarts MUST change
  // both the ufrag and password. However, section 9.2.1.1 says changing the
  // ufrag OR password indicates an ICE restart. So, to keep compatibility with
  // endpoints that only change one, we'll treat this as an ICE restart.
  return (old_ufrag != new_ufrag) || (old_pwd != new_pwd);
}

std::unique_ptr<P2PTransportChannel> P2PTransportChannel::Create(
    absl::string_view transport_name,
    int component,
    IceTransportInit init) {
  return absl::WrapUnique(new P2PTransportChannel(
      init.env(), transport_name, component, init.port_allocator(),
      init.async_dns_resolver_factory(), /*owned_dns_resolver_factory=*/nullptr,
      init.lna_permission_factory(), init.ice_controller_factory(),
      init.active_ice_controller_factory()));
}

P2PTransportChannel::P2PTransportChannel(const Environment& env,
                                         absl::string_view transport_name,
                                         int component,
                                         PortAllocator* allocator)
    : P2PTransportChannel(env,
                          transport_name,
                          component,
                          allocator,
                          /* async_dns_resolver_factory= */ nullptr,
                          /* owned_dns_resolver_factory= */ nullptr,
                          /* lna_permission_factory= */ nullptr,
                          /* ice_controller_factory= */ nullptr,
                          /* active_ice_controller_factory= */ nullptr) {}

// Private constructor, called from Create()
P2PTransportChannel::P2PTransportChannel(
    const Environment& env,
    absl::string_view transport_name,
    int component,
    PortAllocator* allocator,
    AsyncDnsResolverFactoryInterface* async_dns_resolver_factory,
    std::unique_ptr<AsyncDnsResolverFactoryInterface>
        owned_dns_resolver_factory,
    LocalNetworkAccessPermissionFactoryInterface* lna_permission_factory,
    IceControllerFactoryInterface* ice_controller_factory,
    ActiveIceControllerFactoryInterface* active_ice_controller_factory)
    : env_(env),
      transport_name_(transport_name),
      component_(component),
      allocator_(allocator),
      // If owned_dns_resolver_factory is given, async_dns_resolver_factory is
      // ignored.
      async_dns_resolver_factory_(owned_dns_resolver_factory
                                      ? owned_dns_resolver_factory.get()
                                      : async_dns_resolver_factory),
      owned_dns_resolver_factory_(std::move(owned_dns_resolver_factory)),
      lna_permission_factory_(lna_permission_factory),
      incoming_only_(false),
      error_(0),
      remote_ice_mode_(ICEMODE_FULL),
      ice_role_(ICEROLE_UNKNOWN),
      gathering_state_(kIceGatheringNew),
      config_(kReceivingTimeout,
              kBackupConnectionPingInterval,
              GATHER_ONCE /* continual_gathering_policy */,
              false /* prioritize_most_likely_candidate_pairs */,
              kStrongAndStableWritableConnectionPingInterval,
              true /* presume_writable_when_fully_relayed */,
              kRegatherOnFailedNetworksInterval,
              kReceivingSwitchingDelay) {
  TRACE_EVENT0("webrtc", "P2PTransportChannel::P2PTransportChannel");
  RTC_DCHECK(allocator_ != nullptr);
  RTC_DCHECK(!transport_name_.empty());
  // Validate IceConfig even for mostly built-in constant default values in case
  // we change them.
  RTC_DCHECK(config_.IsValid().ok());
  // We populate the change in the candidate filter to the session taken by
  // the transport.
  allocator_->SubscribeCandidateFilterChanged(
      this, [this](uint32_t prev_filter, uint32_t cur_filter) {
        OnCandidateFilterChanged(prev_filter, cur_filter);
      });
  ice_event_log_.set_event_log(&env_.event_log());

  ParseFieldTrials(env_.field_trials());

  IceControllerFactoryArgs args{
      .env = env_,
      .ice_transport_state_func = [this] { return GetState(); },
      .ice_role_func = [this] { return GetIceRole(); },
      .is_connection_pruned_func =
          [this](const Connection* connection) {
            return IsPortPruned(connection->port()) ||
                   IsRemoteCandidatePruned(connection->remote_candidate());
          },
      .ice_field_trials = &ice_field_trials_,
      .ice_controller_field_trials =
          env_.field_trials().Lookup("WebRTC-IceControllerFieldTrials")};

  if (active_ice_controller_factory) {
    ActiveIceControllerFactoryArgs active_args{.legacy_args = args,
                                               .ice_agent = this};
    ice_controller_ = active_ice_controller_factory->Create(active_args);
  } else {
    ice_controller_ = std::make_unique<WrappingActiveIceController>(
        /* ice_agent= */ this, ice_controller_factory, args);
  }
}

P2PTransportChannel::~P2PTransportChannel() {
  TRACE_EVENT0("webrtc", "P2PTransportChannel::~P2PTransportChannel");
  RTC_DCHECK_RUN_ON(&network_thread_);
  regathering_task_handle_.Stop();
  std::vector<Connection*> copy(connections_.begin(), connections_.end());
  for (Connection* connection : copy) {
    connection->UnsubscribeDestroyed(this);
    RemoveConnection(connection);
    connection->Destroy();
  }
  allocator_->UnsubscribeCandidateFilterChanged(this);
  resolvers_.clear();
}

// Add the allocator session to our list so that we know which sessions
// are still active.
void P2PTransportChannel::AddAllocatorSession(
    std::unique_ptr<PortAllocatorSession> session) {
  RTC_DCHECK_RUN_ON(&network_thread_);

  session->set_generation(static_cast<uint32_t>(allocator_sessions_.size()));
  session->SubscribePortReady(
      this, [this](PortAllocatorSession* session, PortInterface* port) {
        OnPortReady(session, port);
      });

  session->SubscribePortsPruned(
      this, [this](PortAllocatorSession* session,
                   const std::vector<PortInterface*>& ports) {
        OnPortsPruned(session, ports);
      });

  session->SubscribeCandidatesReady(
      this, [this](PortAllocatorSession* session,
                   const std::vector<Candidate>& candidate) {
        OnCandidatesReady(session, candidate);
      });
  session->SubscribeCandidateError(this,
                                   [this](PortAllocatorSession* session,
                                          const IceCandidateErrorEvent& event) {
                                     OnCandidateError(session, event);
                                   });
  session->SubscribeCandidatesRemoved(
      this, [this](PortAllocatorSession* session,
                   const std::vector<Candidate>& candidates) {
        OnCandidatesRemoved(session, candidates);
      });
  session->SubscribeCandidatesAllocationDone(
      this, [this](PortAllocatorSession* session) {
        OnCandidatesAllocationDone(session);
      });
  if (!allocator_sessions_.empty()) {
    allocator_session()->PruneAllPorts();
  }
  allocator_sessions_.push_back(std::move(session));

  // We now only want to apply new candidates that we receive to the ports
  // created by this new session because these are replacing those of the
  // previous sessions.
  PruneAllPorts();
}

void P2PTransportChannel::AddConnection(Connection* connection) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  connection->SetReceivingTimeout(config_.receiving_timeout);
  connection->SetUnwritableTimeout(config_.ice_unwritable_timeout);
  connection->set_unwritable_min_checks(config_.ice_unwritable_min_checks);
  connection->SetInactiveTimeout(config_.ice_inactive_timeout);
  connection->RegisterReceivedPacketCallback(
      [&](Connection* connection, const ReceivedIpPacket& packet) {
        OnReadPacket(connection, packet);
      });
  connection->SubscribeReadyToSend(
      this, [this](Connection* connection) { OnReadyToSend(connection); });
  connection->SubscribeStateChange(this, [this](Connection* connection) {
    OnConnectionStateChange(connection);
  });
  connection->SubscribeDestroyed(this, [this](Connection* connection) {
    OnConnectionDestroyed(connection);
  });
  connection->SubscribeNominated(
      this, [this](Connection* connection) { OnNominated(connection); });

  had_connection_ = true;

  connection->set_ice_event_log(&ice_event_log_);
  connection->SetIceFieldTrials(&ice_field_trials_);
  connection->SetStunDictConsumer(
      [this](const StunByteStringAttribute* delta) {
        return GoogDeltaReceived(delta);
      },
      [this](RTCErrorOr<const StunUInt64Attribute*> delta_ack) {
        GoogDeltaAckReceived(std::move(delta_ack));
      });
  if (!dtls_stun_piggyback_callbacks_.empty()) {
    connection->RegisterDtlsPiggyback(DtlsStunPiggybackCallbacks(
        [&](auto request) {
          return dtls_stun_piggyback_callbacks_.send_data(request);
        },
        [&](auto data, auto ack) {
          dtls_stun_piggyback_callbacks_.recv_data(data, std::move(ack));
        }));
  }

  LogCandidatePairConfig(connection, IceCandidatePairConfigType::kAdded);

  connections_.push_back(connection);
  ice_controller_->OnConnectionAdded(connection);
}

void P2PTransportChannel::ForgetLearnedStateForConnections(
    std::span<const Connection* const> connections) {
  for (const Connection* con : connections) {
    FromIceController(con)->ForgetLearnedState();
  }
}

void P2PTransportChannel::SetIceRole(IceRole ice_role) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (ice_role_ != ice_role) {
    ice_role_ = ice_role;
    for (PortInterface* port : ports_) {
      port->SetIceRole(ice_role);
    }
    // Update role on pruned ports as well, because they may still have
    // connections alive that should be using the correct role.
    for (PortInterface* port : pruned_ports_) {
      port->SetIceRole(ice_role);
    }
  }
}

IceRole P2PTransportChannel::GetIceRole() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return ice_role_;
}

IceTransportStateInternal P2PTransportChannel::GetState() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return state_;
}

IceTransportState P2PTransportChannel::GetIceTransportState() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return standardized_state_;
}

const std::string& P2PTransportChannel::transport_name() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return transport_name_;
}

int P2PTransportChannel::component() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return component_;
}

bool P2PTransportChannel::writable() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return writable_;
}

bool P2PTransportChannel::receiving() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return receiving_;
}

IceGatheringState P2PTransportChannel::gathering_state() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return gathering_state_;
}

std::optional<int> P2PTransportChannel::GetRttEstimate() {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (selected_connection_ != nullptr &&
      selected_connection_->rtt_samples() > 0) {
    return selected_connection_->Rtt().ms();
  } else {
    return std::nullopt;
  }
}

std::optional<const CandidatePair>
P2PTransportChannel::GetSelectedCandidatePair() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (selected_connection_ == nullptr) {
    return std::nullopt;
  }

  CandidatePair pair;
  pair.local = SanitizeLocalCandidate(selected_connection_->local_candidate());
  pair.remote =
      SanitizeRemoteCandidate(selected_connection_->remote_candidate());
  return pair;
}

// A channel is considered ICE completed once there is at most one active
// connection per network and at least one active connection.
IceTransportStateInternal P2PTransportChannel::ComputeState() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (!had_connection_) {
    return IceTransportStateInternal::STATE_INIT;
  }

  std::vector<Connection*> active_connections;
  for (Connection* connection : connections_) {
    if (connection->active()) {
      active_connections.push_back(connection);
    }
  }
  if (active_connections.empty()) {
    return IceTransportStateInternal::STATE_FAILED;
  }

  std::set<const Network*> networks;
  for (Connection* connection : active_connections) {
    const Network* network = connection->network();
    if (networks.find(network) == networks.end()) {
      networks.insert(network);
    } else {
      RTC_LOG(LS_VERBOSE) << ToString()
                          << ": Ice not completed yet for this channel as "
                          << network->ToString()
                          << " has more than 1 connection.";
      return IceTransportStateInternal::STATE_CONNECTING;
    }
  }

  ice_event_log_.DumpCandidatePairDescriptionToMemoryAsConfigEvents();
  return IceTransportStateInternal::STATE_COMPLETED;
}

// Compute the current RTCIceTransportState as described in
// https://www.w3.org/TR/webrtc/#dom-rtcicetransportstate
// TODO(bugs.webrtc.org/9218): Start signaling kCompleted once we have
// implemented end-of-candidates signalling.
IceTransportState P2PTransportChannel::ComputeIceTransportState() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  bool has_connection = false;
  for (Connection* connection : connections_) {
    if (connection->active()) {
      has_connection = true;
      break;
    }
  }

  if (had_connection_ && !has_connection) {
    return IceTransportState::kFailed;
  }

  if (!writable() && has_been_writable_) {
    return IceTransportState::kDisconnected;
  }

  if (!had_connection_ && !has_connection) {
    return IceTransportState::kNew;
  }

  if (has_connection && !writable()) {
    // A candidate pair has been formed by adding a remote candidate
    // and gathering a local candidate.
    return IceTransportState::kChecking;
  }

  return IceTransportState::kConnected;
}

void P2PTransportChannel::SetIceParameters(const IceParameters& ice_params) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  RTC_LOG(LS_INFO) << "Set ICE ufrag: " << ice_params.ufrag
                   << " pwd: " << ice_params.pwd << " on transport "
                   << transport_name();
  ice_parameters_ = ice_params;
  // Note: Candidate gathering will restart when MaybeStartGathering is next
  // called.
}

void P2PTransportChannel::SetRemoteIceParameters(
    const IceParameters& ice_params) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  RTC_LOG(LS_INFO) << "Received remote ICE parameters: ufrag="
                   << ice_params.ufrag << ", renomination "
                   << (ice_params.renomination ? "enabled" : "disabled");
  const IceParameters* current_ice = remote_ice_parameters();
  if (!current_ice || *current_ice != ice_params) {
    // Keep the ICE credentials so that newer connections
    // are prioritized over the older ones.
    remote_ice_parameters_.push_back(ice_params);
  }

  // Update the pwd of remote candidate if needed.
  for (RemoteCandidate& candidate : remote_candidates_) {
    if (candidate.username() == ice_params.ufrag &&
        candidate.password().empty()) {
      candidate.set_password(ice_params.pwd);
    }
  }
  // We need to update the credentials and generation for any peer reflexive
  // candidates.
  for (Connection* conn : connections_) {
    conn->MaybeSetRemoteIceParametersAndGeneration(
        ice_params, static_cast<int>(remote_ice_parameters_.size() - 1));
  }
  // Updating the remote ICE candidate generation could change the sort order.
  ice_controller_->OnSortAndSwitchRequest(
      IceSwitchReason::REMOTE_CANDIDATE_GENERATION_CHANGE);
}

void P2PTransportChannel::SetRemoteIceMode(IceMode mode) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  remote_ice_mode_ = mode;
}

// TODO(qingsi): We apply the convention that setting a std::optional parameter
// to null restores its default value in the implementation. However, some
// std::optional parameters are only processed below if non-null, e.g.,
// regather_on_failed_networks_interval, and thus there is no way to restore the
// defaults. Fix this issue later for consistency.
void P2PTransportChannel::SetIceConfig(const IceConfig& config) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (config_.continual_gathering_policy != config.continual_gathering_policy) {
    if (!allocator_sessions_.empty()) {
      RTC_LOG(LS_ERROR) << "Trying to change continual gathering policy "
                           "when gathering has already started!";
    } else {
      config_.continual_gathering_policy = config.continual_gathering_policy;
      RTC_LOG(LS_INFO) << "Set continual_gathering_policy to "
                       << config_.continual_gathering_policy;
    }
  }

  if (config_.backup_connection_ping_interval !=
      config.backup_connection_ping_interval) {
    config_.backup_connection_ping_interval =
        config.backup_connection_ping_interval;
    RTC_LOG(LS_INFO) << "Set backup connection ping interval to "
                     << config_.backup_connection_ping_interval_or_default()
                     << " milliseconds.";
  }
  if (config_.receiving_timeout != config.receiving_timeout) {
    config_.receiving_timeout = config.receiving_timeout;
    for (Connection* connection : connections_) {
      connection->SetReceivingTimeout(config_.receiving_timeout);
    }
    RTC_LOG(LS_INFO) << "Set ICE receiving timeout to "
                     << config_.receiving_timeout_or_default()
                     << " milliseconds";
  }

  config_.prioritize_most_likely_candidate_pairs =
      config.prioritize_most_likely_candidate_pairs;
  RTC_LOG(LS_INFO) << "Set ping most likely connection to "
                   << config_.prioritize_most_likely_candidate_pairs;

  if (config_.stable_writable_connection_ping_interval !=
      config.stable_writable_connection_ping_interval) {
    config_.stable_writable_connection_ping_interval =
        config.stable_writable_connection_ping_interval;
    RTC_LOG(LS_INFO)
        << "Set stable_writable_connection_ping_interval to "
        << config_.stable_writable_connection_ping_interval_or_default();
  }

  if (config_.presume_writable_when_fully_relayed !=
      config.presume_writable_when_fully_relayed) {
    if (!connections_.empty()) {
      RTC_LOG(LS_ERROR) << "Trying to change 'presume writable' "
                           "while connections already exist!";
    } else {
      config_.presume_writable_when_fully_relayed =
          config.presume_writable_when_fully_relayed;
      RTC_LOG(LS_INFO) << "Set presume writable when fully relayed to "
                       << config_.presume_writable_when_fully_relayed;
    }
  }

  config_.surface_ice_candidates_on_ice_transport_type_changed =
      config.surface_ice_candidates_on_ice_transport_type_changed;
  if (config_.surface_ice_candidates_on_ice_transport_type_changed &&
      config_.continual_gathering_policy != GATHER_CONTINUALLY) {
    RTC_LOG(LS_WARNING)
        << "surface_ice_candidates_on_ice_transport_type_changed is "
           "ineffective since we do not gather continually.";
  }

  if (config_.regather_on_failed_networks_interval !=
      config.regather_on_failed_networks_interval) {
    config_.regather_on_failed_networks_interval =
        config.regather_on_failed_networks_interval;
    RTC_LOG(LS_INFO)
        << "Set regather_on_failed_networks_interval to "
        << config_.regather_on_failed_networks_interval_or_default();
    if (regathering_task_handle_.Running()) {
      OnStartedPinging();  // Restart the regathering timer.
    }
  }

  if (config_.receiving_switching_delay != config.receiving_switching_delay) {
    config_.receiving_switching_delay = config.receiving_switching_delay;
    RTC_LOG(LS_INFO) << "Set receiving_switching_delay to "
                     << config_.receiving_switching_delay_or_default();
  }

  if (config_.default_nomination_mode != config.default_nomination_mode) {
    config_.default_nomination_mode = config.default_nomination_mode;
    RTC_LOG(LS_INFO) << "Set default nomination mode to "
                     << static_cast<int>(config_.default_nomination_mode);
  }

  if (config_.ice_check_interval_strong_connectivity !=
      config.ice_check_interval_strong_connectivity) {
    config_.ice_check_interval_strong_connectivity =
        config.ice_check_interval_strong_connectivity;
    RTC_LOG(LS_INFO)
        << "Set strong ping interval to "
        << config_.ice_check_interval_strong_connectivity_or_default();
  }

  if (config_.ice_check_interval_weak_connectivity !=
      config.ice_check_interval_weak_connectivity) {
    config_.ice_check_interval_weak_connectivity =
        config.ice_check_interval_weak_connectivity;
    RTC_LOG(LS_INFO)
        << "Set weak ping interval to "
        << config_.ice_check_interval_weak_connectivity_or_default();
  }

  if (config_.ice_check_min_interval != config.ice_check_min_interval) {
    config_.ice_check_min_interval = config.ice_check_min_interval;
    RTC_LOG(LS_INFO) << "Set min ping interval to "
                     << config_.ice_check_min_interval_or_default();
  }

  if (config_.ice_unwritable_timeout != config.ice_unwritable_timeout) {
    config_.ice_unwritable_timeout = config.ice_unwritable_timeout;
    for (Connection* conn : connections_) {
      conn->SetUnwritableTimeout(config_.ice_unwritable_timeout);
    }
    RTC_LOG(LS_INFO) << "Set unwritable timeout to "
                     << config_.ice_unwritable_timeout_or_default();
  }

  if (config_.ice_unwritable_min_checks != config.ice_unwritable_min_checks) {
    config_.ice_unwritable_min_checks = config.ice_unwritable_min_checks;
    for (Connection* conn : connections_) {
      conn->set_unwritable_min_checks(config_.ice_unwritable_min_checks);
    }
    RTC_LOG(LS_INFO) << "Set unwritable min checks to "
                     << config_.ice_unwritable_min_checks_or_default();
  }

  if (config_.ice_inactive_timeout != config.ice_inactive_timeout) {
    config_.ice_inactive_timeout = config.ice_inactive_timeout;
    for (Connection* conn : connections_) {
      conn->SetInactiveTimeout(config_.ice_inactive_timeout);
    }
    RTC_LOG(LS_INFO) << "Set inactive timeout to "
                     << config_.ice_inactive_timeout_or_default();
  }

  if (config_.network_preference != config.network_preference) {
    config_.network_preference = config.network_preference;
    ice_controller_->OnSortAndSwitchRequest(
        IceSwitchReason::NETWORK_PREFERENCE_CHANGE);
    RTC_LOG(LS_INFO) << "Set network preference to "
                     << (config_.network_preference.has_value()
                             ? config_.network_preference.value()
                             : -1);  // network_preference cannot be bound to
                                     // int with value_or.
  }

  // TODO(qingsi): Resolve the naming conflict of stun_keepalive_delay in
  // UDPPort and stun_keepalive_interval.
  if (config_.stun_keepalive_interval != config.stun_keepalive_interval) {
    config_.stun_keepalive_interval = config.stun_keepalive_interval;
    allocator_session()->SetStunKeepaliveIntervalForReadyPorts(
        config_.stun_keepalive_interval);
    RTC_LOG(LS_INFO) << "Set STUN keepalive interval to "
                     << config.stun_keepalive_interval_or_default();
  }

  config_.vpn_preference = config.vpn_preference;
  allocator_->SetVpnPreference(config_.vpn_preference);

  ice_controller_->SetIceConfig(config_);
  if (config_.dtls_handshake_in_stun != config.dtls_handshake_in_stun) {
    config_.dtls_handshake_in_stun = config.dtls_handshake_in_stun;
    RTC_LOG(LS_INFO) << "Set DTLS handshake in STUN to "
                     << config.dtls_handshake_in_stun;
  }

  RTC_DCHECK(config_.IsValid().ok());
}

void P2PTransportChannel::ParseFieldTrials(
    const FieldTrialsView& field_trials) {
  if (field_trials.IsEnabled("WebRTC-ExtraICEPing")) {
    RTC_LOG(LS_INFO) << "Set WebRTC-ExtraICEPing: Enabled";
  }

  StructParametersParser::Create(
      // go/skylift-light
      "skip_relay_to_non_relay_connections",
      &ice_field_trials_.skip_relay_to_non_relay_connections,
      // Limiting pings sent.
      "max_outstanding_pings", &ice_field_trials_.max_outstanding_pings,
      // Delay initial selection of connection.
      "initial_select_dampening", &ice_field_trials_.initial_select_dampening,
      // Delay initial selection of connections, that are receiving.
      "initial_select_dampening_ping_received",
      &ice_field_trials_.initial_select_dampening_ping_received,
      // Reply that we support goog ping.
      "announce_goog_ping", &ice_field_trials_.announce_goog_ping,
      // Use goog ping if remote support it.
      "enable_goog_ping", &ice_field_trials_.enable_goog_ping,
      // How fast does a RTT sample decay.
      "rtt_estimate_halftime_ms", &ice_field_trials_.rtt_estimate_halftime_ms,
      // Make sure that nomination reaching ICE controlled asap.
      "send_ping_on_switch_ice_controlling",
      &ice_field_trials_.send_ping_on_switch_ice_controlling,
      // Make sure that nomination reaching ICE controlled asap.
      "send_ping_on_selected_ice_controlling",
      &ice_field_trials_.send_ping_on_selected_ice_controlling,
      // Reply to nomination ASAP.
      "send_ping_on_nomination_ice_controlled",
      &ice_field_trials_.send_ping_on_nomination_ice_controlled,
      // Allow connections to live untouched longer that 30s.
      "dead_connection_timeout_ms",
      &ice_field_trials_.dead_connection_timeout_ms,
      // Stop gathering on strongly connected.
      "stop_gather_on_strongly_connected",
      &ice_field_trials_.stop_gather_on_strongly_connected,
      // GOOG_DELTA
      "enable_goog_delta", &ice_field_trials_.enable_goog_delta,
      "answer_goog_delta", &ice_field_trials_.answer_goog_delta)
      ->Parse(field_trials.Lookup("WebRTC-IceFieldTrials"));

  if (ice_field_trials_.dead_connection_timeout_ms < 30000) {
    RTC_LOG(LS_WARNING) << "dead_connection_timeout_ms set to "
                        << ice_field_trials_.dead_connection_timeout_ms
                        << " increasing it to 30000";
    ice_field_trials_.dead_connection_timeout_ms = 30000;
  }

  if (ice_field_trials_.skip_relay_to_non_relay_connections) {
    RTC_LOG(LS_INFO) << "Set skip_relay_to_non_relay_connections";
  }

  if (ice_field_trials_.max_outstanding_pings.has_value()) {
    RTC_LOG(LS_INFO) << "Set max_outstanding_pings: "
                     << *ice_field_trials_.max_outstanding_pings;
  }

  if (ice_field_trials_.initial_select_dampening.has_value()) {
    RTC_LOG(LS_INFO) << "Set initial_select_dampening: "
                     << *ice_field_trials_.initial_select_dampening;
  }

  if (ice_field_trials_.initial_select_dampening_ping_received.has_value()) {
    RTC_LOG(LS_INFO)
        << "Set initial_select_dampening_ping_received: "
        << *ice_field_trials_.initial_select_dampening_ping_received;
  }

  // DSCP override, allow user to specify (any) int value
  // that will be used for tagging all packets.
  StructParametersParser::Create("override_dscp",
                                 &ice_field_trials_.override_dscp)
      ->Parse(field_trials.Lookup("WebRTC-DscpFieldTrial"));

  if (ice_field_trials_.override_dscp) {
    SetOption(Socket::OPT_DSCP, *ice_field_trials_.override_dscp);
  }

  std::string field_trial_string =
      field_trials.Lookup("WebRTC-SetSocketReceiveBuffer");
  int receive_buffer_size_kb = 0;
  sscanf(field_trial_string.c_str(), "Enabled-%d", &receive_buffer_size_kb);
  if (receive_buffer_size_kb > 0) {
    RTC_LOG(LS_INFO) << "Set WebRTC-SetSocketReceiveBuffer: Enabled and set to "
                     << receive_buffer_size_kb << "kb";
    SetOption(Socket::OPT_RCVBUF, receive_buffer_size_kb * 1024);
  }

  ice_field_trials_.piggyback_ice_check_acknowledgement =
      field_trials.IsEnabled("WebRTC-PiggybackIceCheckAcknowledgement");

  ice_field_trials_.extra_ice_ping =
      field_trials.IsEnabled("WebRTC-ExtraICEPing");

  if (!ice_field_trials_.enable_goog_delta) {
    stun_dict_writer_.Disable();
  }

  if (IsRfc8888CongestionControlFeedbackEnabled(field_trials)) {
    int desired_recv_esn = 1;
    RTC_LOG(LS_INFO) << "Set WebRTC-RFC8888CongestionControlFeedback: Enable "
                        "and set ECN recving mode";
    SetOption(Socket::OPT_RECV_ECN, desired_recv_esn);
  }
}

const IceConfig& P2PTransportChannel::config() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return config_;
}

const Connection* P2PTransportChannel::selected_connection() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return selected_connection_;
}

TimeDelta P2PTransportChannel::check_receiving_interval() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return std::max(kMinCheckReceivingInterval,
                  config_.receiving_timeout_or_default() / 10);
}

void P2PTransportChannel::MaybeStartGathering() {
  RTC_DCHECK_RUN_ON(&network_thread_);
  // TODO(bugs.webrtc.org/14605): ensure tie_breaker_ is set.
  if (ice_parameters_.ufrag.empty() || ice_parameters_.pwd.empty()) {
    RTC_LOG(LS_ERROR)
        << "Cannot gather candidates because ICE parameters are empty"
           " ufrag: "
        << ice_parameters_.ufrag << " pwd: " << ice_parameters_.pwd;
    return;
  }
  // Start gathering if we never started before, or if an ICE restart occurred.
  if (allocator_sessions_.empty() ||
      IceCredentialsChanged(allocator_sessions_.back()->ice_ufrag(),
                            allocator_sessions_.back()->ice_pwd(),
                            ice_parameters_.ufrag, ice_parameters_.pwd)) {
    if (gathering_state_ != kIceGatheringGathering) {
      gathering_state_ = kIceGatheringGathering;
      SendGatheringStateEvent();
    }

    for (const auto& session : allocator_sessions_) {
      if (session->IsStopped()) {
        continue;
      }
      session->StopGettingPorts();
    }

    // Time for a new allocator.
    std::unique_ptr<PortAllocatorSession> pooled_session =
        allocator_->TakePooledSession(transport_name(), component(),
                                      ice_parameters_.ufrag,
                                      ice_parameters_.pwd);
    if (pooled_session) {
      PortAllocatorSession* raw_session = pooled_session.get();
      AddAllocatorSession(std::move(pooled_session));
      RTC_DCHECK_EQ(raw_session, allocator_sessions_.back().get());
      // Process the pooled session's existing candidates/ports, if they exist.
      OnCandidatesReady(raw_session, raw_session->ReadyCandidates());
      for (PortInterface* port : raw_session->ReadyPorts()) {
        OnPortReady(raw_session, port);
      }
      if (raw_session->CandidatesAllocationDone()) {
        OnCandidatesAllocationDone(raw_session);
      }
    } else {
      AddAllocatorSession(allocator_->CreateSession(
          transport_name(), component(), ice_parameters_.ufrag,
          ice_parameters_.pwd));
      allocator_sessions_.back()->StartGettingPorts();
    }
  }
}

// A new port is available, attempt to make connections for it
void P2PTransportChannel::OnPortReady(PortAllocatorSession* /* session */,
                                      PortInterface* port) {
  RTC_DCHECK_RUN_ON(&network_thread_);

  // Set in-effect options on the new port
  for (OptionMap::const_iterator it = options_.begin(); it != options_.end();
       ++it) {
    int val = port->SetOption(it->first, it->second);
    if (val < 0) {
      // Errors are frequent, so use LS_INFO. bugs.webrtc.org/9221
      RTC_LOG(LS_INFO) << port->ToString() << ": SetOption(" << it->first
                       << ", " << it->second
                       << ") failed: " << port->GetError();
    }
  }

  // Remember the ports and candidates, and signal that candidates are ready.
  // The session will handle this, and send an initiate/accept/modify message
  // if one is pending.

  port->SetIceRole(ice_role_);
  ports_.push_back(port);
  port->SubscribeUnknownAddress(
      this, [this](PortInterface* port, const SocketAddress& address,
                   ProtocolType proto, IceMessage* stun_msg,
                   const std::string& remote_username, bool port_muxed) {
        OnUnknownAddress(port, address, proto, stun_msg, remote_username,
                         port_muxed);
      });
  port->SubscribeSentPacket(this, [this](const SentPacketInfo& sent_packet) {
    OnSentPacket(sent_packet);
  });

  port->SubscribePortDestroyed(
      this, [this](PortInterface* port) { OnPortDestroyed(port); });
  port->SubscribeRoleConflict([this] { NotifyRoleConflictInternal(); });

  // Attempt to create a connection from this new port to all of the remote
  // candidates that we were given so far.
  std::vector<RemoteCandidate>::iterator iter;
  for (iter = remote_candidates_.begin(); iter != remote_candidates_.end();
       ++iter) {
    CreateConnection(port, *iter, iter->origin_port());
  }

  ice_controller_->OnImmediateSortAndSwitchRequest(
      IceSwitchReason::NEW_CONNECTION_FROM_LOCAL_CANDIDATE);
}

// A new candidate is available, let listeners know
void P2PTransportChannel::OnCandidatesReady(
    PortAllocatorSession* /* session */,
    const std::vector<Candidate>& candidates) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  for (size_t i = 0; i < candidates.size(); ++i) {
    NotifyCandidateGathered(this, candidates[i]);
  }
}

void P2PTransportChannel::OnCandidateError(
    PortAllocatorSession* /* session */,
    const IceCandidateErrorEvent& event) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (candidate_error_callback_) {
    candidate_error_callback_(this, event);
  }
}

void P2PTransportChannel::OnCandidatesAllocationDone(
    PortAllocatorSession* /* session */) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (config_.gather_continually()) {
    RTC_LOG(LS_INFO) << "P2PTransportChannel: " << transport_name()
                     << ", component " << component()
                     << " gathering complete, but using continual "
                        "gathering so not changing gathering state.";
    return;
  }
  gathering_state_ = kIceGatheringComplete;
  RTC_LOG(LS_INFO) << "P2PTransportChannel: " << transport_name()
                   << ", component " << component() << " gathering complete";
  SendGatheringStateEvent();
}

// Handle stun packets
void P2PTransportChannel::OnUnknownAddress(PortInterface* port,
                                           const SocketAddress& address,
                                           ProtocolType proto,
                                           IceMessage* stun_msg,
                                           const std::string& remote_username,
                                           bool port_muxed) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  RTC_DCHECK(stun_msg);

  // Port has received a valid stun packet from an address that no Connection
  // is currently available for. See if we already have a candidate with the
  // address. If it isn't we need to create new candidate for it.
  //
  // TODO(qingsi): There is a caveat of the logic below if we have remote
  // candidates with hostnames. We could create a prflx candidate that is
  // identical to a host candidate that are currently in the process of name
  // resolution. We would not have a duplicate candidate since when adding the
  // resolved host candidate, FinishingAddingRemoteCandidate does
  // MaybeUpdatePeerReflexiveCandidate, and the prflx candidate would be updated
  // to a host candidate. As a result, for a brief moment we would have a prflx
  // candidate showing a private IP address, though we do not signal prflx
  // candidates to applications and we could obfuscate the IP addresses of prflx
  // candidates in P2PTransportChannel::GetStats. The difficulty of preventing
  // creating the prflx from the beginning is that we do not have a reliable way
  // to claim two candidates are identical without the address information. If
  // we always pause the addition of a prflx candidate when there is ongoing
  // name resolution and dedup after we have a resolved address, we run into the
  // risk of losing/delaying the addition of a non-identical candidate that
  // could be the only way to have a connection, if the resolution never
  // completes or is significantly delayed.
  const Candidate* candidate = nullptr;
  for (const Candidate& c : remote_candidates_) {
    if (c.username() == remote_username && c.address() == address &&
        c.protocol() == ProtoToString(proto)) {
      candidate = &c;
      break;
    }
  }

  uint32_t remote_generation = 0;
  std::string remote_password;
  // The STUN binding request may arrive after setRemoteDescription and before
  // adding remote candidate, so we need to set the password to the shared
  // password and set the generation if the user name matches.
  const IceParameters* ice_param =
      FindRemoteIceFromUfrag(remote_username, &remote_generation);
  // Note: if not found, the remote_generation will still be 0.
  if (ice_param != nullptr) {
    remote_password = ice_param->pwd;
  }

  Candidate remote_candidate;
  bool remote_candidate_is_new = (candidate == nullptr);
  if (!remote_candidate_is_new) {
    remote_candidate = *candidate;
  } else {
    // Create a new candidate with this address.
    // The priority of the candidate is set to the PRIORITY attribute
    // from the request.
    const StunUInt32Attribute* priority_attr =
        stun_msg->GetUInt32(STUN_ATTR_PRIORITY);
    if (!priority_attr) {
      RTC_LOG(LS_WARNING) << "P2PTransportChannel::OnUnknownAddress - "
                             "No STUN_ATTR_PRIORITY found in the "
                             "stun request message";
      port->SendBindingErrorResponse(stun_msg, address, STUN_ERROR_BAD_REQUEST,
                                     STUN_ERROR_REASON_BAD_REQUEST);
      return;
    }
    int remote_candidate_priority = priority_attr->value();

    uint16_t network_id = 0;
    uint16_t network_cost = 0;
    const StunUInt32Attribute* network_attr =
        stun_msg->GetUInt32(STUN_ATTR_GOOG_NETWORK_INFO);
    if (network_attr) {
      uint32_t network_info = network_attr->value();
      network_id = static_cast<uint16_t>(network_info >> 16);
      network_cost = static_cast<uint16_t>(network_info);
    }

    // RFC 5245
    // If the source transport address of the request does not match any
    // existing remote candidates, it represents a new peer reflexive remote
    // candidate.
    remote_candidate = Candidate(
        component(), ProtoToString(proto), address, remote_candidate_priority,
        remote_username, remote_password, IceCandidateType::kPrflx,
        remote_generation, "", network_id, network_cost);
    if (proto == PROTO_TCP) {
      remote_candidate.set_tcptype(TCPTYPE_ACTIVE_STR);
    }

    // From RFC 5245, section-7.2.1.3:
    // The foundation of the candidate is set to an arbitrary value, different
    // from the foundation for all other remote candidates.
    remote_candidate.ComputePrflxFoundation();
  }

  // RFC5245, the agent constructs a pair whose local candidate is equal to
  // the transport address on which the STUN request was received, and a
  // remote candidate equal to the source transport address where the
  // request came from.

  // There shouldn't be an existing connection with this remote address.
  // When ports are muxed, this channel might get multiple unknown address
  // signals. In that case if the connection is already exists, we should
  // simply ignore the signal otherwise send server error.
  if (port->GetConnection(remote_candidate.address())) {
    if (port_muxed) {
      RTC_LOG(LS_INFO) << "Connection already exists for peer reflexive "
                          "candidate: "
                       << remote_candidate.ToSensitiveString();
      return;
    } else {
      RTC_DCHECK_NOTREACHED();
      port->SendBindingErrorResponse(stun_msg, address, STUN_ERROR_SERVER_ERROR,
                                     STUN_ERROR_REASON_SERVER_ERROR);
      return;
    }
  }

  Connection* connection =
      port->CreateConnection(remote_candidate, PortInterface::ORIGIN_THIS_PORT);
  if (!connection) {
    // This could happen in some scenarios. For example, a TurnPort may have
    // had a refresh request timeout, so it won't create connections.
    port->SendBindingErrorResponse(stun_msg, address, STUN_ERROR_SERVER_ERROR,
                                   STUN_ERROR_REASON_SERVER_ERROR);
    return;
  }

  RTC_LOG(LS_INFO) << "Adding connection from "
                   << (remote_candidate_is_new ? "peer reflexive"
                                               : "resurrected")
                   << " candidate: " << remote_candidate.ToSensitiveString();
  AddConnection(connection);
  connection->HandleStunBindingOrGoogPingRequest(stun_msg);

  // Update the list of connections since we just added another.  We do this
  // after sending the response since it could (in principle) delete the
  // connection in question.
  ice_controller_->OnImmediateSortAndSwitchRequest(
      IceSwitchReason::NEW_CONNECTION_FROM_UNKNOWN_REMOTE_ADDRESS);
}

void P2PTransportChannel::OnCandidateFilterChanged(uint32_t prev_filter,
                                                   uint32_t cur_filter) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (prev_filter == cur_filter || allocator_session() == nullptr) {
    return;
  }
  if (config_.surface_ice_candidates_on_ice_transport_type_changed) {
    allocator_session()->SetCandidateFilter(cur_filter);
  }
}

void P2PTransportChannel::NotifyRoleConflictInternal() {
  NotifyRoleConflict(this);  // STUN ping will be sent when SetRole is called
                             // from Transport.
}

const IceParameters* P2PTransportChannel::FindRemoteIceFromUfrag(
    absl::string_view ufrag,
    uint32_t* generation) const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  const auto& params = remote_ice_parameters_;
  auto it = std::find_if(
      params.rbegin(), params.rend(),
      [ufrag](const IceParameters& param) { return param.ufrag == ufrag; });
  if (it == params.rend()) {
    // Not found.
    return nullptr;
  }
  *generation = params.rend() - it - 1;
  return &(*it);
}

void P2PTransportChannel::OnNominated(Connection* conn) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  RTC_DCHECK(ice_role_ == ICEROLE_CONTROLLED);

  if (selected_connection_ == conn) {
    return;
  }

  if (ice_field_trials_.send_ping_on_nomination_ice_controlled &&
      conn != nullptr) {
    SendPingRequestInternal(conn);
  }

  // TODO(qingsi): RequestSortAndStateUpdate will eventually call
  // MaybeSwitchSelectedConnection again. Rewrite this logic.
  if (ice_controller_->OnImmediateSwitchRequest(
          IceSwitchReason::NOMINATION_ON_CONTROLLED_SIDE, conn)) {
    // Now that we have selected a connection, it is time to prune other
    // connections and update the read/write state of the channel.
    ice_controller_->OnSortAndSwitchRequest(
        IceSwitchReason::NOMINATION_ON_CONTROLLED_SIDE);
  } else {
    RTC_LOG(LS_INFO)
        << "Not switching the selected connection on controlled side yet: "
        << conn->ToString();
  }
}

void P2PTransportChannel::ResolveHostnameCandidate(const Candidate& candidate) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (!async_dns_resolver_factory_) {
    RTC_LOG(LS_WARNING) << "Dropping ICE candidate with hostname address "
                           "(no AsyncResolverFactory)";
    return;
  }

  auto resolver = async_dns_resolver_factory_->Create();
  auto resptr = resolver.get();
  resolvers_.emplace_back(candidate, std::move(resolver));
  resptr->Start(candidate.address(),
                [this, resptr]() { OnCandidateResolved(resptr); });
  RTC_LOG(LS_INFO) << "Asynchronously resolving ICE candidate hostname "
                   << candidate.address().HostAsSensitiveURIString();
}

void P2PTransportChannel::AddRemoteCandidate(const Candidate& candidate) {
  RTC_DCHECK_RUN_ON(&network_thread_);

  uint32_t generation = GetRemoteCandidateGeneration(candidate);
  // If a remote candidate with a previous generation arrives, drop it.
  if (generation < remote_ice_generation()) {
    RTC_LOG(LS_WARNING) << "Dropping a remote candidate because its ufrag "
                        << candidate.username()
                        << " indicates it was for a previous generation.";
    return;
  }

  Candidate new_remote_candidate(candidate);
  new_remote_candidate.set_generation(generation);
  // ICE candidates don't need to have username and password set, but
  // the code below this (specifically, ConnectionRequest::Prepare in
  // port.cc) uses the remote candidates's username.  So, we set it
  // here.
  if (remote_ice_parameters()) {
    if (candidate.username().empty()) {
      new_remote_candidate.set_username(remote_ice_parameters()->ufrag);
    }
    if (new_remote_candidate.username() == remote_ice_parameters()->ufrag) {
      if (candidate.password().empty()) {
        new_remote_candidate.set_password(remote_ice_parameters()->pwd);
      }
    } else {
      // The candidate belongs to the next generation. Its pwd will be set
      // when the new remote ICE credentials arrive.
      RTC_LOG(LS_WARNING)
          << "A remote candidate arrives with an unknown ufrag: "
          << candidate.username();
    }
  }

  if (new_remote_candidate.address().IsUnresolvedIP()) {
    // Don't do DNS lookups if the IceTransportPolicy is "none" or "relay".
    bool sharing_host = ((allocator_->candidate_filter() & CF_HOST) != 0);
    bool sharing_stun = ((allocator_->candidate_filter() & CF_REFLEXIVE) != 0);
    if (sharing_host || sharing_stun) {
      ResolveHostnameCandidate(new_remote_candidate);
    }
    return;
  }

  CheckLocalNetworkAccessPermission(new_remote_candidate);
}

P2PTransportChannel::CandidateAndResolver::CandidateAndResolver(
    const Candidate& candidate,
    std::unique_ptr<AsyncDnsResolverInterface>&& resolver)
    : candidate(candidate), resolver(std::move(resolver)) {}

P2PTransportChannel::CandidateAndResolver::~CandidateAndResolver() {}

void P2PTransportChannel::OnCandidateResolved(
    AsyncDnsResolverInterface* resolver) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  auto p =
      absl::c_find_if(resolvers_, [resolver](const CandidateAndResolver& cr) {
        return cr.resolver.get() == resolver;
      });
  if (p == resolvers_.end()) {
    RTC_LOG(LS_ERROR) << "Unexpected AsyncDnsResolver return";
    RTC_DCHECK_NOTREACHED();
    return;
  }
  Candidate candidate = p->candidate;
  AddRemoteCandidateWithResult(candidate, resolver->result());
  // Now we can delete the resolver.
  resolvers_.erase(p);
}

void P2PTransportChannel::AddRemoteCandidateWithResult(
    Candidate candidate,
    const AsyncDnsResolverResult& result) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (result.GetError()) {
    RTC_LOG(LS_WARNING) << "Failed to resolve ICE candidate hostname "
                        << candidate.address().HostAsSensitiveURIString()
                        << " with error " << result.GetError();
    return;
  }

  SocketAddress resolved_address;
  // Prefer IPv6 to IPv4 if we have it (see RFC 5245 Section 15.1).
  // TODO(zstein): This won't work if we only have IPv4 locally but receive an
  // AAAA DNS record.
  bool have_address = result.GetResolvedAddress(AF_INET6, &resolved_address) ||
                      result.GetResolvedAddress(AF_INET, &resolved_address);
  if (!have_address) {
    RTC_LOG(LS_INFO) << "ICE candidate hostname "
                     << candidate.address().HostAsSensitiveURIString()
                     << " could not be resolved";
    return;
  }

  RTC_LOG(LS_INFO) << "Resolved ICE candidate hostname "
                   << candidate.address().HostAsSensitiveURIString() << " to "
                   << resolved_address.ipaddr().ToSensitiveString();
  candidate.set_address(resolved_address);

  CheckLocalNetworkAccessPermission(candidate);
}

void P2PTransportChannel::CheckLocalNetworkAccessPermission(
    const Candidate& candidate) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (!lna_permission_factory_) {
    RTC_LOG(LS_VERBOSE) << "No LocalNetworkAccessPermissionFactory";
    FinishAddingRemoteCandidate(candidate);
    return;
  }

  std::unique_ptr<LocalNetworkAccessPermissionInterface> permission_query =
      lna_permission_factory_->Create();
  if (!permission_query->ShouldRequestPermission(candidate.address())) {
    RTC_LOG(LS_VERBOSE) << "No need to request permission for candidate: "
                        << candidate.address().ipaddr().ToSensitiveString()
                        << ".";
    FinishAddingRemoteCandidate(candidate);
    return;
  }

  auto permission_query_ptr = permission_query.get();
  permission_queries_.emplace_back(candidate, std::move(permission_query));

  RTC_LOG(LS_VERBOSE) << "Asynchronously requesting LNA permission."
                      << candidate.address().HostAsSensitiveURIString();
  permission_query_ptr->RequestPermission(
      candidate.address(),
      [this, permission_query_ptr](LocalNetworkAccessPermissionStatus status) {
        OnLocalNetworkAccessResult(permission_query_ptr, status);
      });
}

void P2PTransportChannel::OnLocalNetworkAccessResult(
    LocalNetworkAccessPermissionInterface* permission_query,
    LocalNetworkAccessPermissionStatus status) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  auto p =
      absl::c_find_if(permission_queries_,
                      [permission_query](const CandidateAndPermission& cr) {
                        return cr.permission_query.get() == permission_query;
                      });

  if (p == permission_queries_.end()) {
    RTC_LOG(LS_ERROR) << "Unexpected LocalNetworkAccessPermission return";
    RTC_DCHECK_NOTREACHED();
    return;
  }

  Candidate candidate = std::move(p->candidate);
  permission_queries_.erase(p);

  if (status != LocalNetworkAccessPermissionStatus::kGranted) {
    RTC_LOG(LS_INFO) << "LNA Permission denied for "
                     << candidate.address().HostAsSensitiveURIString() << ".";
    return;
  }

  FinishAddingRemoteCandidate(std::move(candidate));
}

void P2PTransportChannel::FinishAddingRemoteCandidate(
    const Candidate& new_remote_candidate) {
  RTC_DCHECK_RUN_ON(&network_thread_);

  RTC_HISTOGRAM_ENUMERATION(
      "WebRTC.PeerConnection.CandidateAddressType",
      static_cast<int>(new_remote_candidate.address().GetIPAddressType()),
      static_cast<int>(IPAddressType::kMaxValue));

  // If this candidate matches what was thought to be a peer reflexive
  // candidate, we need to update the candidate priority/etc.
  for (Connection* conn : connections_) {
    conn->MaybeUpdatePeerReflexiveCandidate(new_remote_candidate);
  }

  // Create connections to this remote candidate.
  CreateConnections(new_remote_candidate, nullptr);

  // Resort the connections list, which may have new elements.
  ice_controller_->OnImmediateSortAndSwitchRequest(
      IceSwitchReason::NEW_CONNECTION_FROM_REMOTE_CANDIDATE);
}

void P2PTransportChannel::RemoveRemoteCandidate(
    const Candidate& cand_to_remove) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  size_t num_erased = std::erase_if(
      remote_candidates_, [cand_to_remove](const Candidate& candidate) {
        return cand_to_remove.MatchesForRemoval(candidate);
      });
  if (num_erased > 0) {
    RTC_LOG(LS_VERBOSE) << "Removed remote candidate "
                        << cand_to_remove.ToSensitiveString();
  }
}

void P2PTransportChannel::RemoveAllRemoteCandidates() {
  RTC_DCHECK_RUN_ON(&network_thread_);
  remote_candidates_.clear();
}

// Creates connections from all of the ports that we care about to the given
// remote candidate.  The return value is true if we created a connection from
// the origin port.
bool P2PTransportChannel::CreateConnections(const Candidate& remote_candidate,
                                            PortInterface* origin_port) {
  RTC_DCHECK_RUN_ON(&network_thread_);

  // If we've already seen the new remote candidate (in the current candidate
  // generation), then we shouldn't try creating connections for it.
  // We either already have a connection for it, or we previously created one
  // and then later pruned it. If we don't return, the channel will again
  // re-create any connections that were previously pruned, which will then
  // immediately be re-pruned, churning the network for no purpose.
  // This only applies to candidates received over signaling (i.e. origin_port
  // is NULL).
  if (!origin_port && IsDuplicateRemoteCandidate(remote_candidate)) {
    // return true to indicate success, without creating any new connections.
    return true;
  }

  // Add a new connection for this candidate to every port that allows such a
  // connection (i.e., if they have compatible protocols) and that does not
  // already have a connection to an equivalent candidate.  We must be careful
  // to make sure that the origin port is included, even if it was pruned,
  // since that may be the only port that can create this connection.
  bool created = false;
  std::vector<PortInterface*>::reverse_iterator it;
  for (it = ports_.rbegin(); it != ports_.rend(); ++it) {
    if (CreateConnection(*it, remote_candidate, origin_port)) {
      if (*it == origin_port)
        created = true;
    }
  }

  if ((origin_port != nullptr) && !absl::c_linear_search(ports_, origin_port)) {
    if (CreateConnection(origin_port, remote_candidate, origin_port))
      created = true;
  }

  // Remember this remote candidate so that we can add it to future ports.
  RememberRemoteCandidate(remote_candidate, origin_port);

  return created;
}

// Setup a connection object for the local and remote candidate combination.
// And then listen to connection object for changes.
bool P2PTransportChannel::CreateConnection(PortInterface* port,
                                           const Candidate& remote_candidate,
                                           PortInterface* origin_port) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (!port->SupportsProtocol(remote_candidate.protocol())) {
    return false;
  }

  if (ice_field_trials_.skip_relay_to_non_relay_connections) {
    IceCandidateType port_type = port->Type();
    if ((port_type != remote_candidate.type()) &&
        (port_type == IceCandidateType::kRelay ||
         remote_candidate.is_relay())) {
      RTC_LOG(LS_INFO) << ToString() << ": skip creating connection "
                       << IceCandidateTypeToString(port_type) << " to "
                       << remote_candidate.type_name();
      return false;
    }
  }

  // Look for an existing connection with this remote address. If one is not
  // found or a connection exists with an older generation remote candidate and
  // is not selected, then we can create a new connection for this address.
  // Any selected connection is kept so that it can be used to send data during
  // ICE restart per RFC 8445. The skipped connection would be pruned later when
  // a new generation connection is selected at the end of ICE restart.
  Connection* connection = port->GetConnection(remote_candidate.address());
  if (connection == nullptr ||
      (!connection->selected() && connection->remote_candidate().generation() <
                                      remote_candidate.generation())) {
    // Don't create a connection if this is a candidate we received in a
    // message and we are not allowed to make outgoing connections.
    PortInterface::CandidateOrigin origin = GetOrigin(port, origin_port);
    if (origin == PortInterface::ORIGIN_MESSAGE && incoming_only_) {
      return false;
    }
    Connection* new_connection =
        port->CreateConnection(remote_candidate, origin);
    if (!new_connection) {
      return false;
    }
    AddConnection(new_connection);
    RTC_LOG(LS_INFO) << ToString()
                     << ": Created connection with origin: " << origin
                     << ", total: " << connections_.size();
    return true;
  }

  // No new connection was created. Log the cases.
  if (connection->selected()) {
    RTC_LOG(LS_INFO) << port->ToString()
                     << ": A selected connection exists. Skip creating "
                     << "connection for remote candidate: "
                     << remote_candidate.ToSensitiveString();
    return false;
  }

  // It is not legal to try to change any of the parameters of an existing
  // connection; however, the other side can send a duplicate candidate.
  if (!remote_candidate.IsEquivalent(connection->remote_candidate())) {
    RTC_LOG(LS_INFO) << "Attempt to change a remote candidate."
                        " Existing remote candidate: "
                     << connection->remote_candidate().ToSensitiveString()
                     << "New remote candidate: "
                     << remote_candidate.ToSensitiveString();
  }
  return false;
}

bool P2PTransportChannel::FindConnection(const Connection* connection) const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return absl::c_linear_search(connections_, connection);
}

uint32_t P2PTransportChannel::GetRemoteCandidateGeneration(
    const Candidate& candidate) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  // If the candidate has a ufrag, use it to find the generation.
  if (!candidate.username().empty()) {
    uint32_t generation = 0;
    if (!FindRemoteIceFromUfrag(candidate.username(), &generation)) {
      // If the ufrag is not found, assume the next/future generation.
      generation = static_cast<uint32_t>(remote_ice_parameters_.size());
    }
    return generation;
  }
  // If candidate generation is set, use that.
  if (candidate.generation() > 0) {
    return candidate.generation();
  }
  // Otherwise, assume the generation from remote ice parameters.
  return remote_ice_generation();
}

// Check if remote candidate is already cached.
bool P2PTransportChannel::IsDuplicateRemoteCandidate(
    const Candidate& candidate) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  for (size_t i = 0; i < remote_candidates_.size(); ++i) {
    if (remote_candidates_[i].IsEquivalent(candidate)) {
      return true;
    }
  }
  return false;
}

// Maintain our remote candidate list, adding this new remote one.
void P2PTransportChannel::RememberRemoteCandidate(
    const Candidate& remote_candidate,
    PortInterface* origin_port) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  // Remove any candidates whose generation is older than this one.  The
  // presence of a new generation indicates that the old ones are not useful.
  size_t i = 0;
  while (i < remote_candidates_.size()) {
    if (remote_candidates_[i].generation() < remote_candidate.generation()) {
      RTC_LOG(LS_INFO) << "Pruning candidate from old generation: "
                       << remote_candidates_[i].address().ToSensitiveString();
      remote_candidates_.erase(remote_candidates_.begin() + i);
    } else {
      i += 1;
    }
  }

  // Make sure this candidate is not a duplicate.
  if (IsDuplicateRemoteCandidate(remote_candidate)) {
    RTC_LOG(LS_INFO) << "Duplicate candidate: "
                     << remote_candidate.ToSensitiveString();
    return;
  }

  // Try this candidate for all future ports.
  remote_candidates_.push_back(RemoteCandidate(remote_candidate, origin_port));
}

// Set options on ourselves is simply setting options on all of our available
// port objects.
int P2PTransportChannel::SetOption(Socket::Option opt, int value) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (ice_field_trials_.override_dscp && opt == Socket::OPT_DSCP) {
    value = *ice_field_trials_.override_dscp;
  }

  OptionMap::iterator it = options_.find(opt);
  if (it == options_.end()) {
    options_.insert(std::make_pair(opt, value));
  } else if (it->second == value) {
    return 0;
  } else {
    it->second = value;
  }

  for (PortInterface* port : ports_) {
    int val = port->SetOption(opt, value);
    if (val < 0) {
      // Because this also occurs deferred, probably no point in reporting an
      // error
      RTC_LOG(LS_WARNING) << "SetOption(" << opt << ", " << value
                          << ") failed: " << port->GetError();
    }
  }
  return 0;
}

bool P2PTransportChannel::GetOption(Socket::Option opt, int* value) {
  RTC_DCHECK_RUN_ON(&network_thread_);

  const auto& found = options_.find(opt);
  if (found == options_.end()) {
    return false;
  }
  *value = found->second;
  return true;
}

int P2PTransportChannel::GetError() {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return error_;
}

// Send data to the other side, using our selected connection.
int P2PTransportChannel::SendPacket(const char* data,
                                    size_t len,
                                    const AsyncSocketPacketOptions& options,
                                    int flags) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (flags != 0) {
    error_ = EINVAL;
    return -1;
  }

  // If we don't think the connection is working yet, return ENOTCONN
  // instead of sending a packet that will probably be dropped.
  if (!ReadyToSend(selected_connection_)) {
    error_ = ENOTCONN;
    return -1;
  }

  packets_sent_++;
  last_sent_packet_id_ = options.packet_id;
  AsyncSocketPacketOptions modified_options(options);
  modified_options.info_signaled_after_sent.packet_type = PacketType::kData;
  int sent = selected_connection_->Send(
      std::span(reinterpret_cast<const uint8_t*>(data), len), modified_options);
  if (sent <= 0) {
    RTC_DCHECK(sent < 0);
    error_ = selected_connection_->GetError();
    return sent;
  }

  bytes_sent_ += sent;
  return sent;
}

bool P2PTransportChannel::GetStats(IceTransportStats* ice_transport_stats) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  // Gather candidate and candidate pair stats.
  ice_transport_stats->candidate_stats_list.clear();
  ice_transport_stats->connection_infos.clear();

  if (!allocator_sessions_.empty()) {
    allocator_session()->GetCandidateStatsFromReadyPorts(
        &ice_transport_stats->candidate_stats_list);
  }

  // TODO(qingsi): Remove naming inconsistency for candidate pair/connection.
  for (Connection* connection : connections_) {
    ConnectionInfo stats = connection->stats();
    stats.local_candidate = SanitizeLocalCandidate(stats.local_candidate);
    stats.remote_candidate = SanitizeRemoteCandidate(stats.remote_candidate);
    stats.best_connection = (selected_connection_ == connection);
    ice_transport_stats->connection_infos.push_back(std::move(stats));
  }

  ice_transport_stats->selected_candidate_pair_changes =
      selected_candidate_pair_changes_;

  ice_transport_stats->bytes_sent = bytes_sent_;
  ice_transport_stats->bytes_received = bytes_received_;
  ice_transport_stats->packets_sent = packets_sent_;
  ice_transport_stats->packets_received = packets_received_;

  ice_transport_stats->ice_role = GetIceRole();
  ice_transport_stats->ice_local_username_fragment = ice_parameters_.ufrag;
  ice_transport_stats->ice_state = ComputeIceTransportState();

  return true;
}

std::optional<NetworkRoute> P2PTransportChannel::network_route() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return network_route_;
}

DiffServCodePoint P2PTransportChannel::DefaultDscpValue() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  OptionMap::const_iterator it = options_.find(Socket::OPT_DSCP);
  if (it == options_.end()) {
    return DSCP_NO_CHANGE;
  }
  return static_cast<DiffServCodePoint>(it->second);
}

std::span<Connection* const> P2PTransportChannel::connections() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return std::span<Connection* const>(connections_.data(), connections_.size());
}

void P2PTransportChannel::RemoveConnectionForTest(Connection* connection) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  RTC_DCHECK(FindConnection(connection));
  connection->UnsubscribeDestroyed(this);
  RemoveConnection(connection);
  RTC_DCHECK(!FindConnection(connection));
  if (selected_connection_ == connection)
    selected_connection_ = nullptr;
  connection->Destroy();
}

// Monitor connection states.
void P2PTransportChannel::UpdateConnectionStates() {
  RTC_DCHECK_RUN_ON(&network_thread_);
  Timestamp now = env_.clock().CurrentTime();

  // We need to copy the list of connections since some may delete themselves
  // when we call UpdateState.
  // NOTE: We copy the connections() vector in case `UpdateState` triggers the
  // Connection to be destroyed (which will cause a callback that alters
  // the connections() vector).
  std::vector<Connection*> copy(connections_.begin(), connections_.end());
  for (Connection* c : copy) {
    c->UpdateState(now);
  }
}

void P2PTransportChannel::OnStartedPinging() {
  RTC_DCHECK_RUN_ON(&network_thread_);
  RTC_LOG(LS_INFO) << ToString()
                   << ": Have a pingable connection for the first time; "
                      "starting to ping.";
  regathering_task_handle_.Stop();
  regathering_task_handle_ = RepeatingTaskHandle::DelayedStart(
      TaskQueueBase::Current(),
      config_.regather_on_failed_networks_interval_or_default(), [this]() {
        RTC_DCHECK_RUN_ON(&network_thread_);
        if (allocator_session() && allocator_session()->IsCleared()) {
          // Only regather when the current session is in the CLEARED state
          // (i.e., not running or stopped). It is only possible to enter this
          // state when we gather continually, so there is an implicit check on
          // continual gathering here.
          allocator_session()->RegatherOnFailedNetworks();
        }
        return config_.regather_on_failed_networks_interval_or_default();
      });
}

bool P2PTransportChannel::IsPortPruned(const PortInterface* port) const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return !absl::c_linear_search(ports_, port);
}

bool P2PTransportChannel::IsRemoteCandidatePruned(const Candidate& cand) const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return !absl::c_linear_search(remote_candidates_, cand);
}

bool P2PTransportChannel::PresumedWritable(const Connection* conn) const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return (conn->write_state() == Connection::STATE_WRITE_INIT &&
          config_.presume_writable_when_fully_relayed &&
          conn->local_candidate().is_relay() &&
          (conn->remote_candidate().is_relay() ||
           conn->remote_candidate().is_prflx()));
}

void P2PTransportChannel::UpdateState() {
  RTC_DCHECK_RUN_ON(&network_thread_);

  // Check if all connections are timedout.
  bool all_connections_timedout = true;
  for (const Connection* conn : connections_) {
    if (conn->write_state() != Connection::STATE_WRITE_TIMEOUT) {
      all_connections_timedout = false;
      break;
    }
  }

  // Now update the writable state of the channel with the information we have
  // so far.
  if (all_connections_timedout) {
    HandleAllTimedOut();
  }

  // Update the state of this channel.
  UpdateTransportState();
}

bool P2PTransportChannel::AllowedToPruneConnections() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return ice_role_ == ICEROLE_CONTROLLING ||
         (selected_connection_ && selected_connection_->nominated());
}

bool P2PTransportChannel::PruneConnections(
    std::span<const Connection* const> connections) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (!AllowedToPruneConnections()) {
    RTC_LOG(LS_WARNING) << "Not allowed to prune connections";
    return false;
  }
  for (const Connection* conn : connections) {
    FromIceController(conn)->Prune();
  }
  return true;
}

NetworkRoute P2PTransportChannel::ConfigureNetworkRoute(
    const Connection* conn) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return {.connected = ReadyToSend(conn),
          .local = CreateRouteEndpointFromCandidate(
              /* local= */ true, conn->local_candidate(),
              /* uses_turn= */
              conn->port()->Type() == IceCandidateType::kRelay),
          .remote = CreateRouteEndpointFromCandidate(
              /* local= */ false, conn->remote_candidate(),
              /* uses_turn= */ conn->remote_candidate().is_relay()),
          .last_sent_packet_id = last_sent_packet_id_,
          .packet_overhead =
              conn->local_candidate().address().ipaddr().overhead() +
              GetProtocolOverhead(conn->local_candidate().protocol())};
}

void P2PTransportChannel::SwitchSelectedConnection(
    const Connection* new_connection,
    IceSwitchReason reason) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  SwitchSelectedConnectionInternal(FromIceController(new_connection), reason);
}

// Change the selected connection, and let listeners know.
void P2PTransportChannel::SwitchSelectedConnectionInternal(
    Connection* conn,
    IceSwitchReason reason) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  // Note: if conn is NULL, the previous `selected_connection_` has been
  // destroyed, so don't use it.
  Connection* old_selected_connection = selected_connection_;
  selected_connection_ = conn;
  LogCandidatePairConfig(conn, IceCandidatePairConfigType::kSelected);
  network_route_.reset();
  if (old_selected_connection) {
    old_selected_connection->set_selected(false);
  }
  if (selected_connection_) {
    ++nomination_;
    selected_connection_->set_selected(true);
    if (old_selected_connection) {
      RTC_LOG(LS_INFO) << ToString() << ": Previous selected connection: "
                       << old_selected_connection->ToString();
    }
    RTC_LOG(LS_INFO) << ToString() << ": New selected connection: "
                     << selected_connection_->ToString();
    // This is a temporary, but safe fix to webrtc issue 5705.
    // TODO(honghaiz): Make all ENOTCONN error routed through the transport
    // channel so that it knows whether the media channel is allowed to
    // send; then it will only signal ready-to-send if the media channel
    // has been disallowed to send.
    if (selected_connection_->writable() ||
        PresumedWritable(selected_connection_)) {
      NotifyReadyToSend(this);
    }

    network_route_.emplace(ConfigureNetworkRoute(selected_connection_));
  } else {
    RTC_LOG(LS_INFO) << ToString() << ": No selected connection";
  }

  if (conn != nullptr && ice_role_ == ICEROLE_CONTROLLING &&
      ((ice_field_trials_.send_ping_on_switch_ice_controlling &&
        old_selected_connection != nullptr) ||
       ice_field_trials_.send_ping_on_selected_ice_controlling)) {
    SendPingRequestInternal(conn);
  }

  NotifyNetworkRouteChanged(network_route_);

  // Create event for candidate pair change.
  if (selected_connection_ && candidate_pair_change_callback_) {
    CandidatePairChangeEvent pair_change = {
        .transport_name = transport_name(),
        .selected_candidate_pair = *GetSelectedCandidatePair(),
        .last_data_received_ms = selected_connection_->LastDataReceived().ms(),
        .reason = IceSwitchReasonToString(reason),
        .estimated_disconnected_time_ms =
            old_selected_connection != nullptr
                ? ComputeEstimatedDisconnectedTime(old_selected_connection).ms()
                : 0};
    candidate_pair_change_callback_(pair_change);
  }

  ++selected_candidate_pair_changes_;

  ice_controller_->OnConnectionSwitched(selected_connection_);
}

TimeDelta P2PTransportChannel::ComputeEstimatedDisconnectedTime(
    Connection* old_connection) {
  Timestamp now = env_.clock().CurrentTime();
  // TODO(jonaso): nicer keeps estimate of how frequently data _should_ be
  // received, this could be used to give better estimate (if needed).
  Timestamp last_data_or_old_ping =
      std::max(old_connection->LastReceived(), last_data_received_);
  return now - last_data_or_old_ping;
}

// Warning: UpdateTransportState should eventually be called whenever a
// connection is added, deleted, or the write state of any connection changes so
// that the transport controller will get the up-to-date channel state. However
// it should not be called too often; in the case that multiple connection
// states change, it should be called after all the connection states have
// changed. For example, we call this at the end of
// SortConnectionsAndUpdateState.
void P2PTransportChannel::UpdateTransportState() {
  RTC_DCHECK_RUN_ON(&network_thread_);
  // If our selected connection is "presumed writable" (TURN-TURN with no
  // CreatePermission required), act like we're already writable to the upper
  // layers, so they can start media quicker.
  bool writable =
      selected_connection_ && (selected_connection_->writable() ||
                               PresumedWritable(selected_connection_));
  SetWritable(writable);

  bool receiving = false;
  for (const Connection* connection : connections_) {
    if (connection->receiving()) {
      receiving = true;
      break;
    }
  }
  SetReceiving(receiving);

  IceTransportStateInternal state = ComputeState();
  IceTransportState current_standardized_state = ComputeIceTransportState();

  if (state_ != state) {
    RTC_LOG(LS_INFO) << ToString() << ": Transport channel state changed from "
                     << static_cast<int>(state_) << " to "
                     << static_cast<int>(state);
    // Check that the requested transition is allowed. Note that
    // P2PTransportChannel does not (yet) implement a direct mapping of the
    // ICE states from the standard; the difference is covered by
    // TransportController and PeerConnection.
    switch (state_) {
      case IceTransportStateInternal::STATE_INIT:
        // TODO(deadbeef): Once we implement end-of-candidates signaling,
        // we shouldn't go from INIT to COMPLETED.
        RTC_DCHECK(state == IceTransportStateInternal::STATE_CONNECTING ||
                   state == IceTransportStateInternal::STATE_COMPLETED ||
                   state == IceTransportStateInternal::STATE_FAILED);
        break;
      case IceTransportStateInternal::STATE_CONNECTING:
        RTC_DCHECK(state == IceTransportStateInternal::STATE_COMPLETED ||
                   state == IceTransportStateInternal::STATE_FAILED);
        break;
      case IceTransportStateInternal::STATE_COMPLETED:
        // TODO(deadbeef): Once we implement end-of-candidates signaling,
        // we shouldn't go from COMPLETED to CONNECTING.
        // Though we *can* go from COMPlETED to FAILED, if consent expires.
        RTC_DCHECK(state == IceTransportStateInternal::STATE_CONNECTING ||
                   state == IceTransportStateInternal::STATE_FAILED);
        break;
      case IceTransportStateInternal::STATE_FAILED:
        // TODO(deadbeef): Once we implement end-of-candidates signaling,
        // we shouldn't go from FAILED to CONNECTING or COMPLETED.
        RTC_DCHECK(state == IceTransportStateInternal::STATE_CONNECTING ||
                   state == IceTransportStateInternal::STATE_COMPLETED);
        break;
      default:
        RTC_DCHECK_NOTREACHED();
        break;
    }
  }

  if (standardized_state_ != current_standardized_state || state_ != state) {
    standardized_state_ = current_standardized_state;
    state_ = state;
    // Unconditionally signal change, no matter what changed.
    // TODO: issues.webrtc.org/42234495 - remove nonstandard state_
    NotifyIceTransportStateChanged(this);
  }
}

void P2PTransportChannel::MaybeStopPortAllocatorSessions() {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (!IsGettingPorts()) {
    return;
  }

  for (const auto& session : allocator_sessions_) {
    if (session->IsStopped()) {
      continue;
    }
    // If gathering continually, keep the last session running so that
    // it can gather candidates if the networks change.
    if (config_.gather_continually() && session == allocator_sessions_.back()) {
      session->ClearGettingPorts();
    } else {
      session->StopGettingPorts();
    }
  }
}

void P2PTransportChannel::OnSelectedConnectionDestroyed() {
  RTC_DCHECK_RUN_ON(&network_thread_);
  RTC_LOG(LS_INFO) << "Selected connection destroyed. Will choose a new one.";
  IceSwitchReason reason = IceSwitchReason::SELECTED_CONNECTION_DESTROYED;
  SwitchSelectedConnectionInternal(nullptr, reason);
  ice_controller_->OnSortAndSwitchRequest(reason);
}

// If all connections timed out, delete them all.
void P2PTransportChannel::HandleAllTimedOut() {
  RTC_DCHECK_RUN_ON(&network_thread_);
  bool update_selected_connection = false;
  std::vector<Connection*> copy(connections_.begin(), connections_.end());
  for (Connection* connection : copy) {
    if (selected_connection_ == connection) {
      selected_connection_ = nullptr;
      update_selected_connection = true;
    }
    connection->UnsubscribeDestroyed(this);
    RemoveConnection(connection);
    connection->Destroy();
  }

  if (update_selected_connection)
    OnSelectedConnectionDestroyed();
}

bool P2PTransportChannel::ReadyToSend(const Connection* connection) const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  // Note that we allow sending on an unreliable connection, because it's
  // possible that it became unreliable simply due to bad chance.
  // So this shouldn't prevent attempting to send media.
  return connection != nullptr &&
         (connection->writable() ||
          connection->write_state() == Connection::STATE_WRITE_UNRELIABLE ||
          PresumedWritable(connection));
}

// This method is only for unit testing.
Connection* P2PTransportChannel::FindNextPingableConnection() {
  RTC_DCHECK_RUN_ON(&network_thread_);
  const Connection* conn = ice_controller_->FindNextPingableConnection();
  if (conn) {
    return FromIceController(conn);
  } else {
    return nullptr;
  }
}

Timestamp P2PTransportChannel::GetLastPingSent() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return last_ping_sent_;
}

void P2PTransportChannel::SendPingRequest(const Connection* connection) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  SendPingRequestInternal(FromIceController(connection));
}

void P2PTransportChannel::SendPingRequestInternal(Connection* connection) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  PingConnection(connection);
  MarkConnectionPinged(connection);
}

// A connection is considered a backup connection if the channel state
// is completed, the connection is not the selected connection and it is
// active.
void P2PTransportChannel::MarkConnectionPinged(Connection* conn) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  ice_controller_->OnConnectionPinged(conn);
}

// Apart from sending ping from `conn` this method also updates
// `use_candidate_attr` and `nomination` flags. One of the flags is set to
// nominate `conn` if this channel is in CONTROLLING.
void P2PTransportChannel::PingConnection(Connection* conn) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  bool use_candidate_attr = false;
  uint32_t nomination = 0;
  if (ice_role_ == ICEROLE_CONTROLLING) {
    bool renomination_supported = ice_parameters_.renomination &&
                                  !remote_ice_parameters_.empty() &&
                                  remote_ice_parameters_.back().renomination;
    if (renomination_supported) {
      nomination = GetNominationAttr(conn);
    } else {
      use_candidate_attr = GetUseCandidateAttr(conn);
    }
  }
  conn->set_nomination(nomination);
  conn->set_use_candidate_attr(use_candidate_attr);
  last_ping_sent_ = env_.clock().CurrentTime();
  conn->Ping(last_ping_sent_, stun_dict_writer_.CreateDelta());
}

uint32_t P2PTransportChannel::GetNominationAttr(Connection* conn) const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return (conn == selected_connection_) ? nomination_ : 0;
}

// Nominate a connection based on the NominationMode.
bool P2PTransportChannel::GetUseCandidateAttr(Connection* conn) const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return ice_controller_->GetUseCandidateAttribute(
      conn, config_.default_nomination_mode, remote_ice_mode_);
}

// When a connection's state changes, we need to figure out who to use as
// the selected connection again.  It could have become usable, or become
// unusable.
void P2PTransportChannel::OnConnectionStateChange(Connection* connection) {
  RTC_DCHECK_RUN_ON(&network_thread_);

  // May stop the allocator session when at least one connection becomes
  // strongly connected after starting to get ports and the local candidate of
  // the connection is at the latest generation. It is not enough to check
  // that the connection becomes weakly connected because the connection may
  // be changing from (writable, receiving) to (writable, not receiving).
  if (ice_field_trials_.stop_gather_on_strongly_connected) {
    bool strongly_connected = !connection->weak();
    bool latest_generation = connection->local_candidate().generation() >=
                             allocator_session()->generation();
    if (strongly_connected && latest_generation) {
      MaybeStopPortAllocatorSessions();
    }
  }
  // We have to unroll the stack before doing this because we may be changing
  // the state of connections while sorting.
  ice_controller_->OnSortAndSwitchRequest(
      IceSwitchReason::CONNECT_STATE_CHANGE);  // "candidate pair state
                                               // changed");

  ice_controller_->OnConnectionUpdated(connection);
}

// When a connection is removed, edit it out, and then update our best
// connection.
void P2PTransportChannel::OnConnectionDestroyed(Connection* connection) {
  RTC_DCHECK_RUN_ON(&network_thread_);

  // Note: the previous selected_connection_ may be destroyed by now, so don't
  // use it.

  // Remove this connection from the list.
  RemoveConnection(connection);

  RTC_LOG(LS_INFO) << ToString() << ": Removed connection " << connection
                   << " (" << connections_.size() << " remaining)";

  // If this is currently the selected connection, then we need to pick a new
  // one. The call to SortConnectionsAndUpdateState will pick a new one. It
  // looks at the current selected connection in order to avoid switching
  // between fairly similar ones. Since this connection is no longer an
  // option, we can just set selected to nullptr and re-choose a best assuming
  // that there was no selected connection.
  if (selected_connection_ == connection) {
    OnSelectedConnectionDestroyed();
  } else {
    // If a non-selected connection was destroyed, we don't need to re-sort but
    // we do need to update state, because we could be switching to "failed" or
    // "completed".
    UpdateTransportState();
  }
}

void P2PTransportChannel::RemoveConnection(Connection* connection) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  auto it = absl::c_find(connections_, connection);
  RTC_DCHECK(it != connections_.end());
  connection->DeregisterReceivedPacketCallback();
  connections_.erase(it);
  connection->ClearStunDictConsumer();
  connection->DeregisterDtlsPiggyback();
  ice_controller_->OnConnectionDestroyed(connection);
}

// When a port is destroyed, remove it from our list of ports to use for
// connection attempts.
void P2PTransportChannel::OnPortDestroyed(PortInterface* port) {
  RTC_DCHECK_RUN_ON(&network_thread_);

  std::erase(ports_, port);
  std::erase(pruned_ports_, port);
  RTC_LOG(LS_INFO) << "Removed port because it is destroyed: " << ports_.size()
                   << " remaining";
}

void P2PTransportChannel::OnPortsPruned(
    PortAllocatorSession* /* session */,
    const std::vector<PortInterface*>& ports) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  for (PortInterface* port : ports) {
    if (PrunePort(port)) {
      RTC_LOG(LS_INFO) << "Removed port: " << port->ToString() << " "
                       << ports_.size() << " remaining";
    }
  }
}

void P2PTransportChannel::OnCandidatesRemoved(
    PortAllocatorSession* session,
    const std::vector<Candidate>& candidates) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  // Do not signal candidate removals if continual gathering is not enabled,
  // or if this is not the last session because an ICE restart would have
  // signaled the remote side to remove all candidates in previous sessions.
  if (!config_.gather_continually() || session != allocator_session()) {
    return;
  }
  if (candidates_removed_callback_) {
    candidates_removed_callback_(this, candidates);
  }
}

void P2PTransportChannel::PruneAllPorts() {
  RTC_DCHECK_RUN_ON(&network_thread_);
  pruned_ports_.insert(pruned_ports_.end(), ports_.begin(), ports_.end());
  ports_.clear();
}

bool P2PTransportChannel::PrunePort(PortInterface* port) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  auto it = absl::c_find(ports_, port);
  // Don't need to do anything if the port has been deleted from the port
  // list.
  if (it == ports_.end()) {
    return false;
  }
  ports_.erase(it);
  pruned_ports_.push_back(port);
  return true;
}

// We data is available, let listeners know
void P2PTransportChannel::OnReadPacket(Connection* connection,
                                       const ReceivedIpPacket& packet) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (connection != selected_connection_ && !FindConnection(connection)) {
    // Do not deliver, if packet doesn't belong to the correct transport
    // channel.
    RTC_DCHECK_NOTREACHED();
    return;
  }

  // Let the client know of an incoming packet
  packets_received_++;
  bytes_received_ += packet.payload().size();
  RTC_DCHECK_GE(connection->LastDataReceived(), last_data_received_);
  last_data_received_ =
      std::max(last_data_received_, connection->LastDataReceived());

  NotifyPacketReceived(packet);

  // May need to switch the sending connection based on the receiving media
  // path if this is the controlled side.
  if (ice_role_ == ICEROLE_CONTROLLED && connection != selected_connection_) {
    ice_controller_->OnImmediateSwitchRequest(IceSwitchReason::DATA_RECEIVED,
                                              connection);
  }
}

void P2PTransportChannel::OnSentPacket(const SentPacketInfo& sent_packet) {
  RTC_DCHECK_RUN_ON(&network_thread_);

  NotifySentPacket(this, sent_packet);
}

void P2PTransportChannel::OnReadyToSend(Connection* connection) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (connection == selected_connection_ && writable()) {
    NotifyReadyToSend(this);
  }
}

void P2PTransportChannel::SetWritable(bool writable) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (writable_ == writable) {
    return;
  }
  RTC_LOG(LS_VERBOSE) << ToString() << ": Changed writable_ to " << writable;
  writable_ = writable;
  if (writable_) {
    has_been_writable_ = true;
    NotifyReadyToSend(this);
  }
  NotifyWritableState(this);
}

void P2PTransportChannel::SetReceiving(bool receiving) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (receiving_ == receiving) {
    return;
  }
  receiving_ = receiving;
  NotifyReceivingState(this);
}

Candidate P2PTransportChannel::SanitizeLocalCandidate(
    const Candidate& c) const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  // Delegates to the port allocator.
  return allocator_->SanitizeCandidate(c);
}

Candidate P2PTransportChannel::SanitizeRemoteCandidate(
    const Candidate& c) const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  // If the remote endpoint signaled us an mDNS candidate, we assume it
  // is supposed to be sanitized.
  bool use_hostname_address = IsMdnsHostname(c.address().hostname());
  // Remove the address for prflx remote candidates. See
  // https://w3c.github.io/webrtc-stats/#dom-rtcicecandidatestats.
  use_hostname_address |= c.is_prflx();
  // Filter remote ufrag of peer-reflexive candidates before any ICE parameters
  // are known.
  uint32_t remote_generation = 0;
  bool filter_ufrag =
      c.is_prflx() &&
      FindRemoteIceFromUfrag(c.username(), &remote_generation) == nullptr;
  return c.ToSanitizedCopy(use_hostname_address,
                           false /* filter_related_address */, filter_ufrag);
}

void P2PTransportChannel::LogCandidatePairConfig(
    Connection* conn,
    IceCandidatePairConfigType type) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  if (conn == nullptr) {
    return;
  }
  ice_event_log_.LogCandidatePairConfig(type, conn->id(),
                                        conn->ToLogDescription());
}

std::unique_ptr<StunAttribute> P2PTransportChannel::GoogDeltaReceived(
    const StunByteStringAttribute* delta) {
  auto error = stun_dict_view_.ApplyDelta(*delta);
  if (error.ok()) {
    auto& result = error.value();
    RTC_LOG(LS_INFO) << "Applied GOOG_DELTA";
    dictionary_view_updated_callback_list_.Send(this, stun_dict_view_,
                                                result.second);
    return std::move(result.first);
  } else {
    RTC_LOG(LS_ERROR) << "Failed to apply GOOG_DELTA: "
                      << error.error().message();
  }
  return nullptr;
}

void P2PTransportChannel::GoogDeltaAckReceived(
    RTCErrorOr<const StunUInt64Attribute*> error_or_ack) {
  if (error_or_ack.ok()) {
    RTC_LOG(LS_ERROR) << "Applied GOOG_DELTA_ACK";
    auto ack = error_or_ack.value();
    stun_dict_writer_.ApplyDeltaAck(*ack);
    dictionary_writer_synced_callback_list_.Send(this, stun_dict_writer_);
  } else {
    stun_dict_writer_.Disable();
    RTC_LOG(LS_ERROR) << "Failed GOOG_DELTA_ACK: "
                      << error_or_ack.error().message();
  }
}

void P2PTransportChannel::SetDtlsStunPiggybackCallbacks(
    DtlsStunPiggybackCallbacks&& callbacks) {
  RTC_DCHECK_RUN_ON(&network_thread_);
  RTC_DCHECK(connections_.empty());
  RTC_DCHECK(!callbacks.empty());
  dtls_stun_piggyback_callbacks_ = std::move(callbacks);
}

void P2PTransportChannel::ResetDtlsStunPiggybackCallbacks() {
  RTC_DCHECK_RUN_ON(&network_thread_);
  dtls_stun_piggyback_callbacks_.reset();
  for (auto& connection : connections_) {
    connection->DeregisterDtlsPiggyback();
  }
}

size_t P2PTransportChannel::PermissionQueriesOutstandingForTesting() const {
  RTC_DCHECK_RUN_ON(&network_thread_);
  return permission_queries_.size();
}

}  // namespace webrtc
