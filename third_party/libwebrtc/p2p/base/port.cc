/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "p2p/base/port.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/functional/any_invocable.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/local_network_access_permission.h"
#include "api/sequence_checker.h"
#include "api/task_queue/task_queue_base.h"
#include "api/transport/stun.h"
#include "api/units/time_delta.h"
#include "p2p/base/connection.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port_interface.h"
#include "p2p/base/stun_request.h"
#include "p2p/base/transport_description.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/byte_buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/dscp.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/mdns_responder_interface.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/network.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/string_utils.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/trace_event.h"
#include "rtc_base/weak_ptr.h"

namespace webrtc {
namespace {

PacketInfoProtocolType ConvertProtocolTypeToPacketInfoProtocolType(
    ProtocolType type) {
  switch (type) {
    case ProtocolType::PROTO_UDP:
      return PacketInfoProtocolType::kUdp;
    case ProtocolType::PROTO_TCP:
      return PacketInfoProtocolType::kTcp;
    case ProtocolType::PROTO_SSLTCP:
      return PacketInfoProtocolType::kSsltcp;
    case ProtocolType::PROTO_TLS:
      return PacketInfoProtocolType::kTls;
    default:
      return PacketInfoProtocolType::kUnknown;
  }
}

// The delay before we begin checking if this port is useless. We set
// it to a little higher than a total STUN timeout.
const int kPortTimeoutDelay = STUN_TOTAL_TIMEOUT + 5000;

}  // namespace

Port::Port(PortParametersRef args, IceCandidateType type)
    : Port(std::move(args), type, 0, 0, true) {}

Port::Port(PortParametersRef args,
           IceCandidateType type,
           uint16_t min_port,
           uint16_t max_port,
           bool shared_socket /*= false*/)
    : env_(args.env),
      thread_(args.network_thread),
      factory_(args.socket_factory),
      lna_permission_factory_(args.lna_permission_factory),
      type_(type),
      send_retransmit_count_attribute_(args.send_retransmit_count_attribute),
      network_(args.network),
      min_port_(min_port),
      max_port_(max_port),
      content_name_(args.content_name),
      component_(args.component),
      generation_(args.generation),
      ice_username_fragment_(args.ice_username_fragment),
      password_(args.ice_password),
      timeout_delay_(kPortTimeoutDelay),
      enable_port_packets_(false),
      ice_role_(ICEROLE_UNKNOWN),
      ice_tiebreaker_(args.ice_tiebreaker),
      shared_socket_(shared_socket),
      network_cost_(args.network->GetCost(env_.field_trials())),
      role_conflict_callback_(nullptr),
      weak_factory_(this) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(factory_ != nullptr);
  // TODO(pthatcher): Remove this old behavior once we're sure no one
  // relies on it.  If the username_fragment and password are empty,
  // we should just create one.
  if (ice_username_fragment_.empty()) {
    RTC_DCHECK(password_.empty());
    ice_username_fragment_ = CreateRandomString(ICE_UFRAG_LENGTH);
    password_ = CreateRandomString(ICE_PWD_LENGTH);
  }
  // TODO: https://issues.webrtc.org/462023697 - remove need for const_cast
  const_cast<::webrtc::Network*>(network_)->SubscribeTypeChanged(
      this, [this](const ::webrtc::Network* network) {
        OnNetworkTypeChanged(network);
      });
  const_cast<::webrtc::Network*>(network_)->SubscribeNetworkSliceChanged(
      this, [this](const ::webrtc::Network* network) {
        OnNetworkSliceChanged(network);
      });

  RegisterCallbacks(std::move(args.callbacks));

  PostDestroyIfDead(/*delayed=*/true);
  RTC_LOG(LS_INFO) << ToString() << ": Port created with network cost "
                   << network_cost_;
}

void Port::RegisterCallbacks(Callbacks&& callbacks) {
  RTC_DCHECK_RUN_ON(thread_);
  candidate_ready_callback_ = std::move(callbacks.candidate_ready);
  candidate_error_callback_ = std::move(callbacks.candidate_error);
  port_complete_callback_ = std::move(callbacks.port_complete);
  if (callbacks.port_destroyed) {
    port_destroyed_callback_list_.AddReceiver(
        callbacks.tag, std::move(callbacks.port_destroyed));
  }
  port_error_callback_ = std::move(callbacks.port_error);
  if (callbacks.unknown_address) {
    unknown_address_callbacks_.AddReceiver(
        callbacks.tag, std::move(callbacks.unknown_address));
  }
}

Port::~Port() {
  RTC_DCHECK_RUN_ON(thread_);
  DestroyAllConnections();
  CancelPendingTasks();
  const_cast<::webrtc::Network*>(network_)->UnsubscribeTypeChanged(this);
  const_cast<::webrtc::Network*>(network_)->UnsubscribeNetworkSliceChanged(
      this);
}

IceCandidateType Port::Type() const {
  return type_;
}
const Network* Port::Network() const {
  return network_;
}

IceRole Port::GetIceRole() const {
  RTC_DCHECK_RUN_ON(thread_);
  return ice_role_;
}

void Port::SetIceRole(IceRole role) {
  RTC_DCHECK_RUN_ON(thread_);
  ice_role_ = role;
}

uint64_t Port::IceTiebreaker() const {
  RTC_DCHECK_RUN_ON(thread_);
  return ice_tiebreaker_;
}

bool Port::SharedSocket() const {
  RTC_DCHECK_RUN_ON(thread_);
  return shared_socket_;
}

void Port::SetIceParameters(int component,
                            absl::string_view username_fragment,
                            absl::string_view password) {
  RTC_DCHECK_RUN_ON(thread_);
  component_ = component;
  ice_username_fragment_ = std::string(username_fragment);
  password_ = std::string(password);
  for (Candidate& c : candidates_) {
    c.set_component(component);
    c.set_username(username_fragment);
    c.set_password(password);
  }

  // In case any connections exist make sure we update them too.
  for (auto& [unused, connection] : connections_) {
    connection->UpdateLocalIceParameters(component, username_fragment,
                                         password);
  }
}

const std::vector<Candidate>& Port::Candidates() const {
  return candidates_;
}

Connection* Port::GetConnection(const SocketAddress& remote_addr) {
  RTC_DCHECK_RUN_ON(thread_);
  AddressMap::const_iterator iter = connections_.find(remote_addr);
  if (iter != connections_.end())
    return iter->second.get();
  else
    return nullptr;
}

void Port::AddAddress(const SocketAddress& address,
                      const SocketAddress& base_address,
                      const SocketAddress& related_address,
                      absl::string_view protocol,
                      absl::string_view relay_protocol,
                      absl::string_view tcptype,
                      IceCandidateType type,
                      uint32_t type_preference,
                      uint32_t relay_preference,
                      absl::string_view url,
                      bool is_final) {
  RTC_DCHECK_RUN_ON(thread_);

  // TODO(tommi): Set relay_protocol and optionally provide the base address
  // to automatically compute the foundation in the ctor? It would be a good
  // thing for the Candidate class to know the base address and keep it const.
  Candidate c(component_, protocol, address, 0U, username_fragment(), password_,
              type, generation_, "", network_->id(), network_cost_);
  // Set the relay protocol before computing the foundation field.
  c.set_relay_protocol(relay_protocol);
  c.ComputeFoundation(base_address, ice_tiebreaker_);

  c.set_priority(
      c.GetPriority(type_preference, network_->preference(), relay_preference,
                    env().field_trials().IsEnabled(
                        "WebRTC-IncreaseIceCandidatePriorityHostSrflx")));
#if RTC_DCHECK_IS_ON
  if (protocol == TCP_PROTOCOL_NAME && c.is_local()) {
    RTC_DCHECK(!tcptype.empty());
  }
#endif
  c.set_tcptype(tcptype);
  c.set_network_name(network_->name());
  c.set_network_type(network_->type());
  c.set_underlying_type_for_vpn(network_->underlying_type_for_vpn());
  c.set_url(url);
  c.set_related_address(related_address);
  c.set_network_slice(network_->network_slice());

  bool pending = MaybeObfuscateAddress(c, is_final);

  if (!pending) {
    FinishAddingAddress(c, is_final);
  }
}

bool Port::MaybeObfuscateAddress(const Candidate& c, bool is_final) {
  // TODO(bugs.webrtc.org/9723): Use a config to control the feature of IP
  // handling with mDNS.
  if (network_->GetMdnsResponder() == nullptr) {
    return false;
  }
  if (!c.is_local()) {
    return false;
  }

  auto copy = c;
  auto weak_ptr = weak_factory_.GetWeakPtr();
  auto callback = [weak_ptr, copy, is_final](const IPAddress& addr,
                                             absl::string_view name) mutable {
    RTC_DCHECK(copy.address().ipaddr() == addr);
    SocketAddress hostname_address(name, copy.address().port());
    // In Port and Connection, we need the IP address information to
    // correctly handle the update of candidate type to prflx. The removal
    // of IP address when signaling this candidate will take place in
    // BasicPortAllocatorSession::OnCandidateReady, via SanitizeCandidate.
    hostname_address.SetResolvedIP(addr);
    copy.set_address(hostname_address);
    copy.set_related_address(SocketAddress());
    if (weak_ptr != nullptr) {
      RTC_DCHECK_RUN_ON(weak_ptr->thread_);
      weak_ptr->set_mdns_name_registration_status(
          MdnsNameRegistrationStatus::kCompleted);
      weak_ptr->FinishAddingAddress(copy, is_final);
    }
  };
  set_mdns_name_registration_status(MdnsNameRegistrationStatus::kInProgress);
  network_->GetMdnsResponder()->CreateNameForAddress(copy.address().ipaddr(),
                                                     callback);
  return true;
}

void Port::FinishAddingAddress(const Candidate& c, bool is_final) {
  candidates_.push_back(c);
  NotifyCandidateReady(this, c);

  PostAddAddress(is_final);
}

void Port::PostAddAddress(bool is_final) {
  if (is_final) {
    NotifyPortComplete(this);
  }
}

void Port::SendCandidateError(const IceCandidateErrorEvent& event) {
  RTC_DCHECK_RUN_ON(thread_);
  if (candidate_error_callback_) {
    candidate_error_callback_(this, event);
  }
}

void Port::AddOrReplaceConnection(Connection* conn) {
  RTC_DCHECK_RUN_ON(thread_);
  // Calling insert directly here is not desirable. An insert call wraps the
  // input connection into a temporary unique_ptr. If the insert fails due to
  // existing key, the temporary unique_ptr is destroyed, deleting the
  // connection with it.

  auto [iter, inserted] =
      connections_.try_emplace(conn->remote_candidate().address(), nullptr);
  if (inserted) {
    // New address — map takes ownership.
    iter->second = absl::WrapUnique(conn);
    return;
  }

  // The same connection should not be passed more than once. Check for it.
  RTC_CHECK_NE(iter->second.get(), conn);
  // Same address, different connection — replace the old one.
  RTC_LOG(LS_WARNING)
      << ToString()
      << ": A new connection was created on an existing remote address. "
         "New remote candidate: "
      << conn->remote_candidate().ToSensitiveString();
  std::unique_ptr<Connection> old_conn = std::move(iter->second);
  iter->second = absl::WrapUnique(conn);
  HandleConnectionDestroyed(old_conn.get());
  old_conn->Shutdown();
}

void Port::OnReadPacket(const ReceivedIpPacket& packet, ProtocolType proto) {
  RTC_DCHECK_RUN_ON(thread_);

  const SocketAddress& addr = packet.source_address();
  std::span<const uint8_t> data = packet.payload();
  // If the user has enabled port packets, just hand this over.
  if (enable_port_packets_) {
    NotifyReadPacket(this, data, addr);
    return;
  }

  // If this is an authenticated STUN request, then signal unknown address and
  // send back a proper binding response.
  std::unique_ptr<IceMessage> msg;
  std::string remote_username;
  if (!GetStunMessage(data, addr, &msg, &remote_username)) {
    RTC_LOG(LS_ERROR) << ToString()
                      << ": Received non-STUN packet from unknown address: "
                      << addr.ToSensitiveString();
  } else if (!msg) {
    // STUN message handled already
  } else if (msg->type() == STUN_BINDING_REQUEST) {
    if (ice_role_ == ICEROLE_UNKNOWN) {
      // The port has not been assigned an ICE role, which means that it has
      // not been taken over by an ICE agent (yet), e.g. because it was pruned
      // before becoming ready. Without a role the request can't be handled
      // (see MaybeIceRoleConflict), so drop it.
      RTC_LOG(LS_WARNING) << ToString() << ": Dropping "
                          << StunMethodToString(msg->type())
                          << " id=" << hex_encode(msg->transaction_id())
                          << " from unknown address "
                          << addr.ToSensitiveString()
                          << " since the port has no ICE role.";
      return;
    }
    RTC_LOG(LS_INFO) << "Received " << StunMethodToString(msg->type())
                     << " id=" << hex_encode(msg->transaction_id())
                     << " from unknown address " << addr.ToSensitiveString();
    // We need to signal an unknown address before we handle any role conflict
    // below. Otherwise there would be no candidate pair and TURN entry created
    // to send the error response in case of a role conflict.
    NotifyUnknownAddress(this, addr, proto, msg.get(), remote_username, false);
    // Check for role conflicts.
    if (!MaybeIceRoleConflict(addr, msg.get(), remote_username)) {
      RTC_LOG(LS_INFO) << "Received conflicting role from the peer.";
      return;
    }
  } else if (msg->type() == GOOG_PING_REQUEST) {
    // This is a PING sent to a connection that was destroyed.
    // Send back that this is the case and a authenticated BINDING
    // is needed.
    SendBindingErrorResponse(msg.get(), addr, STUN_ERROR_BAD_REQUEST,
                             STUN_ERROR_REASON_BAD_REQUEST);
  } else {
    // NOTE(tschmelcher): STUN_BINDING_RESPONSE is benign. It occurs if we
    // pruned a connection for this port while it had STUN requests in flight,
    // because we then get back responses for them, which this code correctly
    // does not handle.
    if (msg->type() != STUN_BINDING_RESPONSE &&
        msg->type() != GOOG_PING_RESPONSE &&
        msg->type() != GOOG_PING_ERROR_RESPONSE) {
      RTC_LOG(LS_ERROR) << ToString()
                        << ": Received unexpected STUN message type: "
                        << msg->type() << " from unknown address: "
                        << addr.ToSensitiveString();
    }
  }
}

void Port::OnReadyToSend() {
  RTC_DCHECK_RUN_ON(thread_);
  AddressMap::iterator iter = connections_.begin();
  for (; iter != connections_.end(); ++iter) {
    iter->second->OnReadyToSend();
  }
}

void Port::AddPrflxCandidate(const Candidate& local) {
  RTC_DCHECK_RUN_ON(thread_);
  candidates_.push_back(local);
}

bool Port::GetStunMessage(std::span<const uint8_t> data,
                          const SocketAddress& addr,
                          std::unique_ptr<IceMessage>* out_msg,
                          std::string* out_username) {
  RTC_DCHECK_RUN_ON(thread_);
  // NOTE: This could clearly be optimized to avoid allocating any memory.
  //       However, at the data rates we'll be looking at on the client side,
  //       this probably isn't worth worrying about.
  RTC_DCHECK(out_msg != nullptr);
  RTC_DCHECK(out_username != nullptr);
  out_username->clear();

  // Don't bother parsing the packet if we can tell it's not STUN.
  // In ICE mode, all STUN packets will have a valid fingerprint.
  // Except GOOG_PING_REQUEST/RESPONSE that does not send fingerprint.
  int types[] = {GOOG_PING_REQUEST, GOOG_PING_RESPONSE,
                 GOOG_PING_ERROR_RESPONSE};
  if (!StunMessage::IsStunMethod(types, data) &&
      !StunMessage::ValidateFingerprint(data)) {
    return false;
  }

  // Parse the request message.  If the packet is not a complete and correct
  // STUN message, then ignore it.
  std::unique_ptr<IceMessage> stun_msg(new IceMessage());
  ByteBufferReader buf(data);
  if (!stun_msg->Read(&buf) || (buf.Length() > 0)) {
    return false;
  }

  // Get list of attributes in the "comprehension-required" range that were not
  // comprehended. If one or more is found, the behavior differs based on the
  // type of the incoming message; see below.
  std::vector<uint16_t> unknown_attributes =
      stun_msg->GetNonComprehendedAttributes();

  if (stun_msg->type() == STUN_BINDING_REQUEST) {
    // Check for the presence of USERNAME and MESSAGE-INTEGRITY (if ICE) first.
    // If not present, fail with a 400 Bad Request.
    if (!stun_msg->GetByteString(STUN_ATTR_USERNAME) ||
        !stun_msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY)) {
      RTC_LOG(LS_ERROR) << ToString() << ": Received "
                        << StunMethodToString(stun_msg->type())
                        << " without username/M-I from: "
                        << addr.ToSensitiveString();
      SendBindingErrorResponse(stun_msg.get(), addr, STUN_ERROR_BAD_REQUEST,
                               STUN_ERROR_REASON_BAD_REQUEST);
      return true;
    }

    // If the username is bad or unknown, fail with a 401 Unauthorized.
    std::string local_ufrag;
    std::string remote_ufrag;
    if (!ParseStunUsername(stun_msg.get(), &local_ufrag, &remote_ufrag) ||
        local_ufrag != username_fragment()) {
      RTC_LOG(LS_ERROR) << ToString() << ": Received "
                        << StunMethodToString(stun_msg->type())
                        << " with bad local username " << local_ufrag
                        << " from " << addr.ToSensitiveString();
      SendBindingErrorResponse(stun_msg.get(), addr, STUN_ERROR_UNAUTHORIZED,
                               STUN_ERROR_REASON_UNAUTHORIZED);
      return true;
    }

    // If ICE, and the MESSAGE-INTEGRITY is bad, fail with a 401 Unauthorized
    if (stun_msg->ValidateMessageIntegrity(password_) !=
        StunMessage::IntegrityStatus::kIntegrityOk) {
      RTC_LOG(LS_ERROR) << ToString() << ": Received "
                        << StunMethodToString(stun_msg->type())
                        << " with bad M-I from " << addr.ToSensitiveString()
                        << ", password_=" << password_;
      SendBindingErrorResponse(stun_msg.get(), addr, STUN_ERROR_UNAUTHORIZED,
                               STUN_ERROR_REASON_UNAUTHORIZED);
      return true;
    }

    // If a request contains unknown comprehension-required attributes, reply
    // with an error. See RFC5389 section 7.3.1.
    if (!unknown_attributes.empty()) {
      SendUnknownAttributesErrorResponse(stun_msg.get(), addr,
                                         unknown_attributes);
      return true;
    }

    out_username->assign(remote_ufrag);
  } else if ((stun_msg->type() == STUN_BINDING_RESPONSE) ||
             (stun_msg->type() == STUN_BINDING_ERROR_RESPONSE)) {
    if (stun_msg->type() == STUN_BINDING_ERROR_RESPONSE) {
      if (const StunErrorCodeAttribute* error_code = stun_msg->GetErrorCode()) {
        RTC_LOG(LS_ERROR) << ToString() << ": Received "
                          << StunMethodToString(stun_msg->type())
                          << ": class=" << error_code->eclass()
                          << " number=" << error_code->number() << " reason='"
                          << error_code->reason() << "' from "
                          << addr.ToSensitiveString();
        // Return message to allow error-specific processing
      } else {
        RTC_LOG(LS_ERROR) << ToString() << ": Received "
                          << StunMethodToString(stun_msg->type())
                          << " without a error code from "
                          << addr.ToSensitiveString();
        return true;
      }
    }
    // If a response contains unknown comprehension-required attributes, it's
    // simply discarded and the transaction is considered failed. See RFC5389
    // sections 7.3.3 and 7.3.4.
    if (!unknown_attributes.empty()) {
      RTC_LOG(LS_ERROR) << ToString()
                        << ": Discarding STUN response due to unknown "
                           "comprehension-required attribute";
      return true;
    }
    // NOTE: Username should not be used in verifying response messages.
    out_username->clear();
  } else if (stun_msg->type() == STUN_BINDING_INDICATION) {
    RTC_LOG(LS_VERBOSE) << ToString() << ": Received "
                        << StunMethodToString(stun_msg->type()) << ": from "
                        << addr.ToSensitiveString();
    out_username->clear();

    // If an indication contains unknown comprehension-required attributes,[]
    // it's simply discarded. See RFC5389 section 7.3.2.
    if (!unknown_attributes.empty()) {
      RTC_LOG(LS_ERROR) << ToString()
                        << ": Discarding STUN indication due to "
                           "unknown comprehension-required attribute";
      return true;
    }
    // No stun attributes will be verified, if it's stun indication message.
    // Returning from end of the this method.
  } else if (stun_msg->type() == GOOG_PING_REQUEST) {
    if (stun_msg->ValidateMessageIntegrity(password_) !=
        StunMessage::IntegrityStatus::kIntegrityOk) {
      RTC_LOG(LS_ERROR) << ToString() << ": Received "
                        << StunMethodToString(stun_msg->type())
                        << " with bad M-I from " << addr.ToSensitiveString()
                        << ", password_=" << password_;
      SendBindingErrorResponse(stun_msg.get(), addr, STUN_ERROR_UNAUTHORIZED,
                               STUN_ERROR_REASON_UNAUTHORIZED);
      return true;
    }
    RTC_LOG(LS_VERBOSE) << ToString() << ": Received "
                        << StunMethodToString(stun_msg->type()) << " from "
                        << addr.ToSensitiveString();
    out_username->clear();
  } else if (stun_msg->type() == GOOG_PING_RESPONSE ||
             stun_msg->type() == GOOG_PING_ERROR_RESPONSE) {
    // note: the MessageIntegrity32 will be verified in Connection.cc
    RTC_LOG(LS_VERBOSE) << ToString() << ": Received "
                        << StunMethodToString(stun_msg->type()) << " from "
                        << addr.ToSensitiveString();
    out_username->clear();
  } else {
    RTC_LOG(LS_ERROR) << ToString()
                      << ": Received STUN packet with invalid type ("
                      << stun_msg->type() << ") from "
                      << addr.ToSensitiveString();
    return true;
  }

  // Return the STUN message found.
  *out_msg = std::move(stun_msg);
  return true;
}

bool Port::IsCompatibleAddress(const SocketAddress& addr) {
  // Get a representative IP for the Network this port is configured to use.
  IPAddress ip = network_->GetBestIP();
  // We use single-stack sockets, so families must match.
  if (addr.family() != ip.family()) {
    return false;
  }
  // Link-local IPv6 ports can only connect to other link-local IPv6 ports.
  if (ip.family() == AF_INET6 &&
      (IPIsLinkLocal(ip) != IPIsLinkLocal(addr.ipaddr()))) {
    return false;
  }
  return true;
}

DiffServCodePoint Port::StunDscpValue() const {
  // By default, inherit from whatever the MediaChannel sends.
  return DSCP_NO_CHANGE;
}

void Port::DestroyAllConnections() {
  RTC_DCHECK_RUN_ON(thread_);
  for (auto& [unused, connection] : connections_) {
    connection->Shutdown();
  }
  connections_.clear();
}

void Port::set_timeout_delay(int delay) {
  RTC_DCHECK_RUN_ON(thread_);
  // Although this method is meant to only be used by tests, some downstream
  // projects have started using it. Ideally we should update our tests to not
  // require to modify this state and instead use a testing harness that allows
  // adjusting the clock and then just use the kPortTimeoutDelay constant
  // directly.
  timeout_delay_ = delay;
}

bool Port::ParseStunUsername(const StunMessage* stun_msg,
                             std::string* local_ufrag,
                             std::string* remote_ufrag) const {
  // The packet must include a username that either begins or ends with our
  // fragment.  It should begin with our fragment if it is a request and it
  // should end with our fragment if it is a response.
  local_ufrag->clear();
  remote_ufrag->clear();
  const StunByteStringAttribute* username_attr =
      stun_msg->GetByteString(STUN_ATTR_USERNAME);
  if (username_attr == nullptr)
    return false;

  // RFRAG:LFRAG
  const absl::string_view username = username_attr->string_view();
  size_t colon_pos = username.find(':');
  if (colon_pos == absl::string_view::npos) {
    return false;
  }

  *local_ufrag = std::string(username.substr(0, colon_pos));
  *remote_ufrag = std::string(username.substr(colon_pos + 1, username.size()));
  return true;
}

bool Port::MaybeIceRoleConflict(const SocketAddress& addr,
                                IceMessage* stun_msg,
                                absl::string_view remote_ufrag) {
  RTC_DCHECK_RUN_ON(thread_);
  // Validate ICE_CONTROLLING or ICE_CONTROLLED attributes.
  bool ret = true;
  IceRole remote_ice_role = ICEROLE_UNKNOWN;
  uint64_t remote_tiebreaker = 0;
  const StunUInt64Attribute* stun_attr =
      stun_msg->GetUInt64(STUN_ATTR_ICE_CONTROLLING);
  if (stun_attr) {
    remote_ice_role = ICEROLE_CONTROLLING;
    remote_tiebreaker = stun_attr->value();
  }

  // If `remote_ufrag` is same as port local username fragment and
  // tie breaker value received in the ping message matches port
  // tiebreaker value this must be a loopback call.
  // We will treat this as valid scenario.
  if (remote_ice_role == ICEROLE_CONTROLLING &&
      username_fragment() == remote_ufrag &&
      remote_tiebreaker == IceTiebreaker()) {
    return true;
  }

  stun_attr = stun_msg->GetUInt64(STUN_ATTR_ICE_CONTROLLED);
  if (stun_attr) {
    remote_ice_role = ICEROLE_CONTROLLED;
    remote_tiebreaker = stun_attr->value();
  }

  switch (ice_role_) {
    case ICEROLE_CONTROLLING:
      if (ICEROLE_CONTROLLING == remote_ice_role) {
        if (remote_tiebreaker >= ice_tiebreaker_) {
          NotifyRoleConflict();
        } else {
          // Send Role Conflict (487) error response.
          SendBindingErrorResponse(stun_msg, addr, STUN_ERROR_ROLE_CONFLICT,
                                   STUN_ERROR_REASON_ROLE_CONFLICT);
          ret = false;
        }
      }
      break;
    case ICEROLE_CONTROLLED:
      if (ICEROLE_CONTROLLED == remote_ice_role) {
        if (remote_tiebreaker < ice_tiebreaker_) {
          NotifyRoleConflict();
        } else {
          // Send Role Conflict (487) error response.
          SendBindingErrorResponse(stun_msg, addr, STUN_ERROR_ROLE_CONFLICT,
                                   STUN_ERROR_REASON_ROLE_CONFLICT);
          ret = false;
        }
      }
      break;
    default:
      RTC_DCHECK_NOTREACHED();
  }
  return ret;
}

std::string Port::CreateStunUsername(absl::string_view remote_username) const {
  RTC_DCHECK_RUN_ON(thread_);
  return std::string(remote_username) + ":" + username_fragment();
}

bool Port::HandleIncomingPacket(AsyncPacketSocket* /* socket */,
                                const ReceivedIpPacket& /* packet */) {
  RTC_DCHECK_NOTREACHED();
  return false;
}

bool Port::CanHandleIncomingPacketsFrom(const SocketAddress&) const {
  return false;
}

void Port::SendBindingErrorResponse(StunMessage* message,
                                    const SocketAddress& addr,
                                    int error_code,
                                    absl::string_view reason) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(message->type() == STUN_BINDING_REQUEST ||
             message->type() == GOOG_PING_REQUEST);

  // Fill in the response message.
  StunMessage response(message->type() == STUN_BINDING_REQUEST
                           ? STUN_BINDING_ERROR_RESPONSE
                           : GOOG_PING_ERROR_RESPONSE,
                       message->transaction_id());

  // When doing GICE, we need to write out the error code incorrectly to
  // maintain backwards compatibility.
  auto error_attr = StunAttribute::CreateErrorCode();
  error_attr->SetCode(error_code);
  error_attr->SetReason(std::string(reason));
  response.AddAttribute(std::move(error_attr));

  // Per Section 10.1.2, certain error cases don't get a MESSAGE-INTEGRITY,
  // because we don't have enough information to determine the shared secret.
  if (error_code != STUN_ERROR_BAD_REQUEST &&
      error_code != STUN_ERROR_UNAUTHORIZED &&
      message->type() != GOOG_PING_REQUEST) {
    if (message->type() == STUN_BINDING_REQUEST) {
      response.AddMessageIntegrity(password_);
    } else {
      response.AddMessageIntegrity32(password_);
    }
  }

  if (message->type() == STUN_BINDING_REQUEST) {
    response.AddFingerprint();
  }

  // Send the response message.
  ByteBufferWriter buf;
  response.Write(&buf);
  AsyncSocketPacketOptions options(StunDscpValue());
  options.info_signaled_after_sent.packet_type =
      PacketType::kIceConnectivityCheckResponse;
  SendTo(buf.DataView(), addr, options, false);
  RTC_LOG(LS_INFO) << ToString() << ": Sending STUN "
                   << StunMethodToString(response.type())
                   << ": reason=" << reason << " to "
                   << addr.ToSensitiveString();
}

void Port::SendUnknownAttributesErrorResponse(
    StunMessage* message,
    const SocketAddress& addr,
    const std::vector<uint16_t>& unknown_types) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(message->type() == STUN_BINDING_REQUEST);

  // Fill in the response message.
  StunMessage response(STUN_BINDING_ERROR_RESPONSE, message->transaction_id());

  auto error_attr = StunAttribute::CreateErrorCode();
  error_attr->SetCode(STUN_ERROR_UNKNOWN_ATTRIBUTE);
  error_attr->SetReason(STUN_ERROR_REASON_UNKNOWN_ATTRIBUTE);
  response.AddAttribute(std::move(error_attr));

  std::unique_ptr<StunUInt16ListAttribute> unknown_attr =
      StunAttribute::CreateUnknownAttributes();
  for (uint16_t type : unknown_types) {
    unknown_attr->AddType(type);
  }
  response.AddAttribute(std::move(unknown_attr));

  response.AddMessageIntegrity(password_);
  response.AddFingerprint();

  // Send the response message.
  ByteBufferWriter buf;
  response.Write(&buf);
  AsyncSocketPacketOptions options(StunDscpValue());
  options.info_signaled_after_sent.packet_type =
      PacketType::kIceConnectivityCheckResponse;
  SendTo(buf.DataView(), addr, options, false);
  RTC_LOG(LS_ERROR) << ToString() << ": Sending STUN binding error: reason="
                    << STUN_ERROR_UNKNOWN_ATTRIBUTE << " to "
                    << addr.ToSensitiveString();
}

void Port::KeepAliveUntilPruned() {
  RTC_DCHECK_RUN_ON(thread_);

  // If it is pruned, we won't bring it up again.
  if (state_ == State::INIT) {
    state_ = State::KEEP_ALIVE_UNTIL_PRUNED;
  }
}

void Port::Prune() {
  RTC_DCHECK_RUN_ON(thread_);
  state_ = State::PRUNED;
  PostDestroyIfDead(/*delayed=*/false);
}

// Call to stop any currently pending operations from running.
void Port::CancelPendingTasks() {
  TRACE_EVENT0("webrtc", "Port::CancelPendingTasks");
  RTC_DCHECK_RUN_ON(thread_);
  weak_factory_.InvalidateWeakPtrs();
}

void Port::PostDestroyIfDead(bool delayed) {
  RTC_DCHECK_RUN_ON(thread_);
  WeakPtr<Port> weak_ptr = NewWeakPtr();
  auto task = [weak_ptr = std::move(weak_ptr)] {
    if (weak_ptr) {
      weak_ptr->DestroyIfDead();
    }
  };
  if (delayed) {
    thread_->PostDelayedTask(std::move(task),
                             TimeDelta::Millis(timeout_delay_));
  } else {
    thread_->PostTask(std::move(task));
  }
}

void Port::DestroyIfDead() {
  RTC_DCHECK_RUN_ON(thread_);
  bool dead =
      (state_ == State::INIT || state_ == State::PRUNED) &&
      connections_.empty() &&
      env_.clock().TimeInMilliseconds() - last_time_all_connections_removed_ >=
          timeout_delay_;
  if (dead) {
    Destroy();
  }
}

void Port::SubscribePortDestroyed(
    const void* tag,
    std::function<void(PortInterface*)> callback) {
  RTC_DCHECK_RUN_ON(thread_);
  port_destroyed_callback_list_.AddReceiver(tag, std::move(callback));
}

void Port::SendPortDestroyed(Port* port) {
  RTC_DCHECK_RUN_ON(thread_);
  port_destroyed_callback_list_.Send(port);
}
void Port::OnNetworkTypeChanged(const ::webrtc::Network* network) {
  RTC_DCHECK(network == network_);

  UpdateNetworkCost();
}

void Port::OnNetworkSliceChanged(const ::webrtc::Network* network) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(network == network_);

  for (Candidate& candidate : candidates_) {
    candidate.set_network_slice(network_->network_slice());
  }

  // The network slice affects the network cost as well, so also update the cost
  // when the slice changes.
  UpdateNetworkCost();
}

std::string Port::ToString() const {
  RTC_DCHECK_RUN_ON(thread_);
  StringBuilder ss;
  ss << "Port[" << ToHex(reinterpret_cast<uintptr_t>(this)) << ":"
     << content_name_ << ":" << component_ << ":" << generation_ << ":"
     << IceCandidateTypeToString(type_) << ":" << network_->ToString() << "]";
  return ss.Release();
}

// TODO(honghaiz): Make the network cost configurable from user setting.
void Port::UpdateNetworkCost() {
  RTC_DCHECK_RUN_ON(thread_);
  uint16_t new_cost = network_->GetCost(env().field_trials());
  if (network_cost_ == new_cost) {
    return;
  }
  RTC_LOG(LS_INFO) << "Network cost changed from " << network_cost_ << " to "
                   << new_cost
                   << ". Number of candidates created: " << candidates_.size()
                   << ". Number of connections created: "
                   << connections_.size();
  network_cost_ = new_cost;
  for (Candidate& candidate : candidates_)
    candidate.set_network_cost(network_cost_);

  for (auto& [unused, connection] : connections_)
    connection->SetLocalCandidateNetworkCost(network_cost_);
}

void Port::EnablePortPackets() {
  RTC_DCHECK_RUN_ON(thread_);
  enable_port_packets_ = true;
}

void Port::DestroyConnectionInternal(Connection* conn, bool async) {
  RTC_DCHECK_RUN_ON(thread_);
  auto iter = connections_.find(conn->remote_candidate().address());
  if (iter == connections_.end() || iter->second.get() != conn) {
    // This could indicate a programmer error outside of webrtc so while we
    // do have this check here to alert external developers, we also need to
    // handle it since it might be a corner case not caught in tests.
    RTC_DCHECK_NOTREACHED() << "Calling Destroy recursively?";
    return;
  }
  std::unique_ptr<Connection> owned_conn = std::move(iter->second);
  connections_.erase(iter);

  HandleConnectionDestroyed(owned_conn.get());
  owned_conn->Shutdown();

  if (async) {
    // Unwind the stack before deleting the object in case upstream callers
    // need to refer to the Connection's state as part of teardown.
    // NOTE: We move ownership of `conn` into the capture section of the lambda
    // so that the object will always be deleted, including if PostTask fails.
    // In such a case, deletion would happen inside of the call
    // to `DestroyConnection()`.
    thread_->PostTask([owned_conn = std::move(owned_conn)]() {});
  }

  // Ports time out after all connections fail if it is not marked as
  // "keep alive until pruned."
  // Note: If a new connection is added after this message is posted, but it
  // fails and is removed before kPortTimeoutDelay, then this message will
  // not cause the Port to be destroyed.
  if (connections_.empty()) {
    last_time_all_connections_removed_ = env_.clock().TimeInMilliseconds();
    PostDestroyIfDead(/*delayed=*/true);
  }
}

void Port::Destroy() {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(connections_.empty());
  RTC_LOG(LS_INFO) << ToString() << ": Port deleted";
  SendPortDestroyed(this);
  delete this;
}

const std::string& Port::username_fragment() const {
  RTC_DCHECK_RUN_ON(thread_);
  return ice_username_fragment_;
}

void Port::CopyPortInformationToPacketInfo(PacketInfo* info) const {
  info->protocol = ConvertProtocolTypeToPacketInfoProtocolType(GetProtocol());
  info->network_id = Network()->id();
}

void Port::MaybeRequestLocalNetworkAccessPermission(
    const SocketAddress& address,
    absl::AnyInvocable<void(LocalNetworkAccessPermissionStatus)> callback) {
  RTC_DCHECK_RUN_ON(thread_);
  if (!lna_permission_factory_) {
    std::move(callback)(LocalNetworkAccessPermissionStatus::kGranted);
    return;
  }

  std::unique_ptr<LocalNetworkAccessPermissionInterface> permission_query =
      lna_permission_factory_->Create();
  if (!permission_query->ShouldRequestPermission(address)) {
    std::move(callback)(LocalNetworkAccessPermissionStatus::kGranted);
    return;
  }

  RTC_LOG(LS_VERBOSE) << "Asynchronously requesting LNA permission."
                      << address.HostAsSensitiveURIString();

  auto* permission_query_ptr = permission_query.get();
  permission_queries_.push_back(std::move(permission_query));

  permission_query_ptr->RequestPermission(
      address, [this, permission_query_ptr, callback = std::move(callback)](
                   LocalNetworkAccessPermissionStatus status) mutable {
        OnRequestLocalNetworkAccessPermission(permission_query_ptr,
                                              std::move(callback), status);
      });
}

void Port::OnRequestLocalNetworkAccessPermission(
    LocalNetworkAccessPermissionInterface* permission_query,
    absl::AnyInvocable<void(LocalNetworkAccessPermissionStatus)> callback,
    LocalNetworkAccessPermissionStatus status) {
  RTC_DCHECK_RUN_ON(thread_);
  auto it =
      absl::c_find_if(permission_queries_, [permission_query](const auto& q) {
        return q.get() == permission_query;
      });
  if (it == permission_queries_.end()) {
    RTC_LOG(LS_ERROR) << "Unexpected LocalNetworkAccessPermission return";
    RTC_DCHECK_NOTREACHED();
    return;
  }

  permission_queries_.erase(it);
  std::move(callback)(status);
}

void Port::SubscribeRoleConflict(absl::AnyInvocable<void()> callback) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(callback);
  RTC_DCHECK(!role_conflict_callback_);
  role_conflict_callback_ = std::move(callback);
}

void Port::NotifyRoleConflict() {
  RTC_DCHECK_RUN_ON(thread_);
  role_conflict_callback_();
}

void Port::SubscribeUnknownAddress(const void* tag,
                                   absl::AnyInvocable<void(PortInterface*,
                                                           const SocketAddress&,
                                                           ProtocolType,
                                                           IceMessage*,
                                                           const std::string&,
                                                           bool)> callback) {
  unknown_address_callbacks_.AddReceiver(tag, std::move(callback));
}

void Port::NotifyUnknownAddress(PortInterface* port,
                                const SocketAddress& address,
                                ProtocolType proto,
                                IceMessage* msg,
                                const std::string& rf,
                                bool port_muxed) {
  unknown_address_callbacks_.Send(port, address, proto, msg, rf, port_muxed);
}

void Port::SubscribeReadPacket(
    const void* tag,
    absl::AnyInvocable<void(PortInterface*,
                            std::span<const uint8_t>,
                            const SocketAddress&)> callback) {
  read_packet_callbacks_.AddReceiver(tag, std::move(callback));
}

void Port::NotifyReadPacket(PortInterface* port,
                            std::span<const uint8_t> data,
                            const SocketAddress& remote_address) {
  read_packet_callbacks_.Send(port, data, remote_address);
}

void Port::SubscribeSentPacket(
    const void* tag,
    absl::AnyInvocable<void(const SentPacketInfo&)> callback) {
  sent_packet_callbacks_.AddReceiver(tag, std::move(callback));
}

void Port::NotifySentPacket(const SentPacketInfo& packet) {
  sent_packet_callbacks_.Send(packet);
}

}  // namespace webrtc
