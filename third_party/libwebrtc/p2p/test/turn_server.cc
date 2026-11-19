/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "p2p/test/turn_server.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <tuple>  // for std::tie
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "api/environment/environment.h"
#include "api/packet_socket_factory.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/transport/ecn_marking.h"
#include "api/transport/stun.h"
#include "api/units/time_delta.h"
#include "p2p/base/async_stun_tcp_socket.h"
#include "p2p/base/port_interface.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/byte_buffer.h"
#include "rtc_base/byte_order.h"
#include "rtc_base/checks.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/message_digest.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/span_helpers.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/strings/string_builder.h"

namespace webrtc {
namespace {

// TODO(juberti): Move this all to a future turnmessage.h
//  static const int IPPROTO_UDP = 17;
constexpr TimeDelta kNonceTimeout = TimeDelta::Minutes(60);
constexpr TimeDelta kDefaultAllocationTimeout = TimeDelta::Minutes(10);
constexpr TimeDelta kPermissionTimeout = TimeDelta::Minutes(5);
constexpr TimeDelta kChannelTimeout = TimeDelta::Minutes(10);

constexpr size_t kNonceKeySize = 16;
constexpr size_t kNonceSize = 48;

constexpr size_t TURN_CHANNEL_HEADER_SIZE = 4U;

// TODO(mallinath): Move these to a common place.
bool IsTurnChannelData(uint16_t msg_type) {
  // The first two bits of a channel data message are 0b01.
  return ((msg_type & 0xC000) == 0x4000);
}

}  // namespace

int GetStunSuccessResponseTypeOrZero(const StunMessage& req) {
  const int resp_type = GetStunSuccessResponseType(req.type());
  return resp_type == -1 ? 0 : resp_type;
}

int GetStunErrorResponseTypeOrZero(const StunMessage& req) {
  const int resp_type = GetStunErrorResponseType(req.type());
  return resp_type == -1 ? 0 : resp_type;
}

static void InitErrorResponse(int code,
                              absl::string_view reason,
                              StunMessage* resp) {
  resp->AddAttribute(std::make_unique<StunErrorCodeAttribute>(
      STUN_ATTR_ERROR_CODE, code, std::string(reason)));
}

TurnServer::TurnServer(const Environment& env, TaskQueueBase* thread)
    : env_(env),
      thread_(thread),
      nonce_key_(CreateRandomString(kNonceKeySize)),
      auth_hook_(nullptr),
      redirect_hook_(nullptr),
      enable_otu_nonce_(false) {}

TurnServer::~TurnServer() {
  RTC_DCHECK_RUN_ON(thread_);
}

void TurnServer::AddInternalSocket(std::unique_ptr<AsyncPacketSocket> socket,
                                   ProtocolType protocol) {
  RTC_DCHECK_RUN_ON(thread_);
  auto [it, inserted] = server_sockets_.emplace(std::move(socket), protocol);
  RTC_DCHECK(inserted);
  it->first->RegisterReceivedPacketCallback(
      [&](AsyncPacketSocket* socket, const ReceivedIpPacket& packet) {
        RTC_DCHECK_RUN_ON(thread_);
        OnInternalPacket(socket, packet);
      });
}

void TurnServer::AddInternalServerSocket(
    std::unique_ptr<Socket> socket,
    ProtocolType protocol,
    std::unique_ptr<SSLAdapterFactory> ssl_adapter_factory) {
  RTC_DCHECK_RUN_ON(thread_);
  auto [iter, inserted] = server_listen_sockets_.emplace(
      std::move(socket),
      ServerSocketInfo{.proto = protocol,
                       .ssl_adapter_factory = std::move(ssl_adapter_factory)});
  RTC_DCHECK(inserted);
  iter->first->SubscribeReadEvent(
      this, [this](Socket* socket) { OnNewInternalConnection(socket); });
}

void TurnServer::SetExternalSocketFactory(PacketSocketFactory* factory,
                                          const SocketAddress& external_addr) {
  RTC_DCHECK_RUN_ON(thread_);
  external_socket_factory_.reset(factory);
  external_addr_ = external_addr;
}

void TurnServer::OnNewInternalConnection(Socket* socket) {
  RTC_DCHECK_RUN_ON(thread_);
  auto iter = server_listen_sockets_.find(socket);
  RTC_DCHECK(iter != server_listen_sockets_.end());

  // Check if someone is trying to connect to us.
  SocketAddress accept_addr;
  std::unique_ptr<Socket> accepted_socket =
      absl::WrapUnique(socket->Accept(&accept_addr));
  if (accepted_socket != nullptr) {
    const ServerSocketInfo& info = iter->second;
    if (info.ssl_adapter_factory) {
      std::unique_ptr<SSLAdapter> ssl_adapter = absl::WrapUnique(
          info.ssl_adapter_factory->CreateAdapter(accepted_socket.release()));
      ssl_adapter->StartSSL("");
      accepted_socket = std::move(ssl_adapter);
    }
    auto tcp_socket =
        std::make_unique<AsyncStunTCPSocket>(env_, std::move(accepted_socket));

    tcp_socket->SubscribeCloseEvent(this,
                                    [this](AsyncPacketSocket* s, int err) {
                                      OnInternalSocketClose(s, err);
                                    });
    // Finally add the socket so it can start communicating with the client.
    AddInternalSocket(std::move(tcp_socket), info.proto);
  }
}

void TurnServer::OnInternalSocketClose(AsyncPacketSocket* socket, int err) {
  RTC_DCHECK_RUN_ON(thread_);
  if (auto iter = server_sockets_.find(socket); iter != server_sockets_.end()) {
    DestroyInternalSocket(iter);
  }
}

void TurnServer::OnInternalPacket(AsyncPacketSocket* socket,
                                  const ReceivedIpPacket& packet) {
  RTC_DCHECK_RUN_ON(thread_);
  // Fail if the packet is too small to even contain a channel header.
  std::span<const uint8_t> payload = packet.payload();
  if (payload.size() < TURN_CHANNEL_HEADER_SIZE) {
    return;
  }
  auto iter = server_sockets_.find(socket);
  RTC_DCHECK(iter != server_sockets_.end());
  TurnServerConnection conn(packet.source_address(), iter->second, socket);
  uint16_t msg_type = GetBE16(payload);
  if (!IsTurnChannelData(msg_type)) {
    // This is a STUN message.
    HandleStunMessage(&conn, payload, packet.ecn());
  } else {
    // This is a channel message; let the allocation handle it.
    TurnServerAllocation* allocation = FindAllocation(&conn);
    if (allocation) {
      allocation->HandleChannelData(payload, packet.ecn());
    }
    if (stun_message_observer_ != nullptr) {
      stun_message_observer_->ReceivedChannelData(payload);
    }
  }
}

void TurnServer::HandleStunMessage(TurnServerConnection* conn,
                                   std::span<const uint8_t> payload,
                                   EcnMarking ecn) {
  RTC_DCHECK_RUN_ON(thread_);
  TurnMessage msg;
  ByteBufferReader buf(payload);
  if (!msg.Read(&buf) || (buf.Length() > 0)) {
    RTC_LOG(LS_WARNING) << "Received invalid STUN message";
    return;
  }

  if (stun_message_observer_ != nullptr) {
    stun_message_observer_->ReceivedMessage(&msg);
  }

  // If it's a STUN binding request, handle that specially.
  if (msg.type() == STUN_BINDING_REQUEST) {
    HandleBindingRequest(conn, &msg);
    return;
  }

  if (redirect_hook_ != nullptr && msg.type() == STUN_ALLOCATE_REQUEST) {
    SocketAddress address;
    if (redirect_hook_->ShouldRedirect(conn->src(), &address)) {
      SendErrorResponseWithAlternateServer(conn, &msg, address);
      return;
    }
  }

  // Look up the key that we'll use to validate the M-I. If we have an
  // existing allocation, the key will already be cached.
  TurnServerAllocation* allocation = FindAllocation(conn);
  std::string key;
  if (!allocation) {
    GetKey(&msg, &key);
  } else {
    key = allocation->key();
  }

  // Ensure the message is authorized; only needed for requests.
  if (IsStunRequestType(msg.type())) {
    if (!CheckAuthorization(conn, &msg, key)) {
      return;
    }
  }

  if (!allocation && msg.type() == STUN_ALLOCATE_REQUEST) {
    HandleAllocateRequest(conn, &msg, key, ecn);
  } else if (allocation &&
             (msg.type() != STUN_ALLOCATE_REQUEST ||
              msg.transaction_id() == allocation->transaction_id())) {
    // This is a non-allocate request, or a retransmit of an allocate.
    // Check that the username matches the previous username used.
    if (IsStunRequestType(msg.type()) &&
        msg.GetByteString(STUN_ATTR_USERNAME)->string_view() !=
            allocation->username()) {
      SendErrorResponse(conn, &msg, STUN_ERROR_WRONG_CREDENTIALS,
                        STUN_ERROR_REASON_WRONG_CREDENTIALS);
      return;
    }
    allocation->HandleTurnMessage(&msg, ecn);
  } else {
    // Allocation mismatch.
    SendErrorResponse(conn, &msg, STUN_ERROR_ALLOCATION_MISMATCH,
                      STUN_ERROR_REASON_ALLOCATION_MISMATCH);
  }
}

bool TurnServer::GetKey(const StunMessage* msg, std::string* key) {
  const StunByteStringAttribute* username_attr =
      msg->GetByteString(STUN_ATTR_USERNAME);
  if (!username_attr) {
    return false;
  }

  return (auth_hook_ != nullptr &&
          auth_hook_->GetKey(std::string(username_attr->string_view()), realm_,
                             key));
}

bool TurnServer::CheckAuthorization(TurnServerConnection* conn,
                                    StunMessage* msg,
                                    absl::string_view key) {
  // RFC 5389, 10.2.2.
  RTC_DCHECK(IsStunRequestType(msg->type()));
  const StunByteStringAttribute* mi_attr =
      msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY);
  const StunByteStringAttribute* username_attr =
      msg->GetByteString(STUN_ATTR_USERNAME);
  const StunByteStringAttribute* realm_attr =
      msg->GetByteString(STUN_ATTR_REALM);
  const StunByteStringAttribute* nonce_attr =
      msg->GetByteString(STUN_ATTR_NONCE);

  // Fail if no MESSAGE_INTEGRITY.
  if (!mi_attr) {
    SendErrorResponseWithRealmAndNonce(conn, msg, STUN_ERROR_UNAUTHORIZED,
                                       STUN_ERROR_REASON_UNAUTHORIZED);
    return false;
  }

  // Fail if there is MESSAGE_INTEGRITY but no username, nonce, or realm.
  if (!username_attr || !realm_attr || !nonce_attr) {
    SendErrorResponse(conn, msg, STUN_ERROR_BAD_REQUEST,
                      STUN_ERROR_REASON_BAD_REQUEST);
    return false;
  }

  // Fail if bad nonce.
  if (!ValidateNonce(nonce_attr->string_view())) {
    SendErrorResponseWithRealmAndNonce(conn, msg, STUN_ERROR_STALE_NONCE,
                                       STUN_ERROR_REASON_STALE_NONCE);
    return false;
  }

  // Fail if bad MESSAGE_INTEGRITY.
  if (key.empty() || msg->ValidateMessageIntegrity(std::string(key)) !=
                         StunMessage::IntegrityStatus::kIntegrityOk) {
    SendErrorResponseWithRealmAndNonce(conn, msg, STUN_ERROR_UNAUTHORIZED,
                                       STUN_ERROR_REASON_UNAUTHORIZED);
    return false;
  }

  // Fail if one-time-use nonce feature is enabled.
  TurnServerAllocation* allocation = FindAllocation(conn);
  if (enable_otu_nonce_ && allocation &&
      allocation->last_nonce() == nonce_attr->string_view()) {
    SendErrorResponseWithRealmAndNonce(conn, msg, STUN_ERROR_STALE_NONCE,
                                       STUN_ERROR_REASON_STALE_NONCE);
    return false;
  }

  if (allocation) {
    allocation->set_last_nonce(nonce_attr->string_view());
  }
  // Success.
  return true;
}

void TurnServer::HandleBindingRequest(TurnServerConnection* conn,
                                      const StunMessage* req) {
  StunMessage response(GetStunSuccessResponseTypeOrZero(*req),
                       req->transaction_id());
  // Tell the user the address that we received their request from.
  auto mapped_addr_attr = std::make_unique<StunXorAddressAttribute>(
      STUN_ATTR_XOR_MAPPED_ADDRESS, conn->src());
  response.AddAttribute(std::move(mapped_addr_attr));

  SendStun(conn, &response, EcnMarking::kNotEct);
}

void TurnServer::HandleAllocateRequest(TurnServerConnection* conn,
                                       const TurnMessage* msg,
                                       absl::string_view key,
                                       EcnMarking ecn) {
  // Check the parameters in the request.
  const StunUInt32Attribute* transport_attr =
      msg->GetUInt32(STUN_ATTR_REQUESTED_TRANSPORT);
  if (!transport_attr) {
    SendErrorResponse(conn, msg, STUN_ERROR_BAD_REQUEST,
                      STUN_ERROR_REASON_BAD_REQUEST);
    return;
  }

  // Only UDP is supported right now.
  int proto = transport_attr->value() >> 24;
  if (proto != IPPROTO_UDP) {
    SendErrorResponse(conn, msg, STUN_ERROR_UNSUPPORTED_PROTOCOL,
                      STUN_ERROR_REASON_UNSUPPORTED_PROTOCOL);
    return;
  }

  // Create the allocation and let it send the success response.
  // If the actual socket allocation fails, send an internal error.
  TurnServerAllocation* alloc = CreateAllocation(conn, proto, key);
  if (alloc) {
    alloc->HandleTurnMessage(msg, ecn);
  } else {
    SendErrorResponse(conn, msg, STUN_ERROR_SERVER_ERROR,
                      "Failed to allocate socket");
  }
}

std::string TurnServer::GenerateNonce(int64_t now) const {
  // Generate a nonce of the form hex(now + HMAC-MD5(nonce_key_, now))
  std::string input(reinterpret_cast<const char*>(&now), sizeof(now));
  std::string nonce = hex_encode(input);
  nonce += ComputeHmac(DIGEST_MD5, nonce_key_, input);
  RTC_DCHECK(nonce.size() == kNonceSize);

  return nonce;
}

bool TurnServer::ValidateNonce(absl::string_view nonce) const {
  // Check the size.
  if (nonce.size() != kNonceSize) {
    return false;
  }

  // Decode the timestamp.
  int64_t then;
  std::span<uint8_t> p =
      HexDecode(std::span(reinterpret_cast<uint8_t*>(&then), sizeof(then)),
                nonce.substr(0, sizeof(then) * 2));
  if (p.size() != sizeof(then)) {
    return false;
  }

  // Verify the HMAC.
  if (nonce.substr(sizeof(then) * 2) !=
      ComputeHmac(DIGEST_MD5, nonce_key_, AsStringView(p))) {
    return false;
  }

  // Validate the timestamp.
  return TimeDelta::Millis(env_.clock().TimeInMilliseconds() - then) <
         kNonceTimeout;
}

TurnServerAllocation* TurnServer::FindAllocation(TurnServerConnection* conn) {
  AllocationMap::const_iterator it = allocations_.find(*conn);
  return (it != allocations_.end()) ? it->second.get() : nullptr;
}

TurnServerAllocation* TurnServer::CreateAllocation(TurnServerConnection* conn,
                                                   int proto,
                                                   absl::string_view key) {
  std::unique_ptr<AsyncPacketSocket> external_socket =
      (external_socket_factory_) ? external_socket_factory_->CreateUdpSocket(
                                       env_, external_addr_, 0, 0)
                                 : nullptr;
  if (!external_socket) {
    return nullptr;
  }
  external_socket->SetOption(Socket::OPT_RECV_ECN, 1);

  // The Allocation takes ownership of the socket.
  TurnServerAllocation* allocation = new TurnServerAllocation(
      this, thread_, *conn, std::move(external_socket), key);
  allocations_[*conn].reset(allocation);
  return allocation;
}

void TurnServer::SendErrorResponse(TurnServerConnection* conn,
                                   const StunMessage* req,
                                   int code,
                                   absl::string_view reason) {
  RTC_DCHECK_RUN_ON(thread_);
  TurnMessage resp(GetStunErrorResponseTypeOrZero(*req), req->transaction_id());
  InitErrorResponse(code, reason, &resp);

  RTC_LOG(LS_INFO) << "Sending error response, type=" << resp.type()
                   << ", code=" << code << ", reason=" << reason;
  SendStun(conn, &resp, EcnMarking::kNotEct);
}

void TurnServer::SendErrorResponseWithRealmAndNonce(TurnServerConnection* conn,
                                                    const StunMessage* msg,
                                                    int code,
                                                    absl::string_view reason) {
  TurnMessage resp(GetStunErrorResponseTypeOrZero(*msg), msg->transaction_id());
  InitErrorResponse(code, reason, &resp);

  int64_t timestamp = env_.clock().TimeInMilliseconds();
  if (ts_for_next_nonce_) {
    timestamp = ts_for_next_nonce_;
    ts_for_next_nonce_ = 0;
  }
  resp.AddAttribute(std::make_unique<StunByteStringAttribute>(
      STUN_ATTR_NONCE, GenerateNonce(timestamp)));
  resp.AddAttribute(
      std::make_unique<StunByteStringAttribute>(STUN_ATTR_REALM, realm_));
  SendStun(conn, &resp, EcnMarking::kNotEct);
}

void TurnServer::SendErrorResponseWithAlternateServer(
    TurnServerConnection* conn,
    const StunMessage* msg,
    const SocketAddress& addr) {
  TurnMessage resp(GetStunErrorResponseTypeOrZero(*msg), msg->transaction_id());
  InitErrorResponse(STUN_ERROR_TRY_ALTERNATE,
                    STUN_ERROR_REASON_TRY_ALTERNATE_SERVER, &resp);
  resp.AddAttribute(
      std::make_unique<StunAddressAttribute>(STUN_ATTR_ALTERNATE_SERVER, addr));
  SendStun(conn, &resp, EcnMarking::kNotEct);
}

void TurnServer::SendStun(TurnServerConnection* conn,
                          StunMessage* msg,
                          EcnMarking ecn) {
  RTC_DCHECK_RUN_ON(thread_);
  ByteBufferWriter buf;
  msg->Write(&buf);
  Send(conn, buf, ecn);
}

void TurnServer::Send(TurnServerConnection* conn,
                      const ByteBufferWriter& buf,
                      EcnMarking ecn) {
  RTC_DCHECK_RUN_ON(thread_);
  AsyncSocketPacketOptions options;
  // TODO: bugs.webrtc.org/453581251  - If we want to properly test with TURN,
  // the server must copy `ecn`.
  RTC_LOG_IF(LS_WARNING, ecn == EcnMarking::kCe)
      << "TurnServer does not properly support forwarding "
         "ECN.";
  options.ect_1 = (ecn == EcnMarking::kEct1);
  conn->socket()->SendTo(buf.Data(), buf.Length(), conn->src(), options);
}

void TurnServer::DestroyAllocation(TurnServerAllocation* allocation) {
  // Removing the internal socket if the connection is not udp.
  AsyncPacketSocket* socket = allocation->conn()->socket();
  auto iter = server_sockets_.find(socket);
  // Skip if the socket serving this allocation is UDP, as this will be shared
  // by all allocations.
  // Note: We may not find a socket if it's a TCP socket that was closed, and
  // the allocation is only now timing out.
  if (iter != server_sockets_.end() && iter->second != PROTO_UDP) {
    DestroyInternalSocket(iter);
  }

  allocations_.erase(*(allocation->conn()));
}

void TurnServer::DestroyInternalSocket(ServerSocketMap::iterator iter) {
  RTC_DCHECK(iter != server_sockets_.end());
  auto node = server_sockets_.extract(iter);
  AsyncPacketSocket* server_socket = node.key().get();
  server_socket->UnsubscribeCloseEvent(this);
  server_socket->DeregisterReceivedPacketCallback();
  // We must destroy the socket async to avoid invalidating the callback list
  // iterator. (In other words, deleting an object from within a callback from
  // that object).
  thread_->PostTask([node = std::move(node)] {});
}

TurnServerConnection::TurnServerConnection(const SocketAddress& src,
                                           ProtocolType proto,
                                           AsyncPacketSocket* socket)
    : src_(src),
      // UDP sockets are not connected and have no remote address. Asking for
      // it anyway would cost a failing system call for every received packet.
      dst_(proto == PROTO_UDP ? SocketAddress() : socket->GetRemoteAddress()),
      proto_(proto),
      socket_(socket) {}

bool TurnServerConnection::operator==(const TurnServerConnection& c) const {
  return src_ == c.src_ && dst_ == c.dst_ && proto_ == c.proto_;
}

bool TurnServerConnection::operator<(const TurnServerConnection& c) const {
  return std::tie(src_, dst_, proto_) < std::tie(c.src_, c.dst_, c.proto_);
}

std::string TurnServerConnection::ToString() const {
  StringBuilder ost;
  ost << src_.ToSensitiveString() << "-" << dst_.ToSensitiveString() << ":"
      << ProtoToString(proto_);
  return ost.Release();
}

TurnServerAllocation::TurnServerAllocation(
    TurnServer* server,
    TaskQueueBase* thread,
    const TurnServerConnection& conn,
    std::unique_ptr<AsyncPacketSocket> server_socket,
    absl::string_view key)
    : server_(server),
      thread_(thread),
      conn_(conn),
      external_socket_(std::move(server_socket)),
      key_(key) {
  external_socket_->RegisterReceivedPacketCallback(
      [&](AsyncPacketSocket* socket, const ReceivedIpPacket& packet) {
        RTC_DCHECK_RUN_ON(thread_);
        OnExternalPacket(socket, packet);
      });
}

TurnServerAllocation::~TurnServerAllocation() {
  channels_.clear();
  perms_.clear();
  RTC_LOG(LS_INFO) << ToString() << ": Allocation destroyed";
}

std::string TurnServerAllocation::ToString() const {
  StringBuilder ost;
  ost << "Alloc[" << conn_.ToString() << "]";
  return ost.Release();
}

void TurnServerAllocation::HandleTurnMessage(const TurnMessage* msg,
                                             EcnMarking ecn) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_DCHECK(msg != nullptr);
  switch (msg->type()) {
    case STUN_ALLOCATE_REQUEST:
      HandleAllocateRequest(msg);
      break;
    case TURN_REFRESH_REQUEST:
      HandleRefreshRequest(msg);
      break;
    case TURN_SEND_INDICATION:
      HandleSendIndication(msg, ecn);
      break;
    case TURN_CREATE_PERMISSION_REQUEST:
      HandleCreatePermissionRequest(msg);
      break;
    case TURN_CHANNEL_BIND_REQUEST:
      HandleChannelBindRequest(msg);
      break;
    default:
      // Not sure what to do with this, just eat it.
      RTC_LOG(LS_WARNING) << ToString()
                          << ": Invalid TURN message type received: "
                          << msg->type();
  }
}

void TurnServerAllocation::HandleAllocateRequest(const TurnMessage* msg) {
  // Copy the important info from the allocate request.
  transaction_id_ = msg->transaction_id();
  const StunByteStringAttribute* username_attr =
      msg->GetByteString(STUN_ATTR_USERNAME);
  RTC_DCHECK(username_attr != nullptr);
  username_ = std::string(username_attr->string_view());

  // Figure out the lifetime and start the allocation timer.
  TimeDelta lifetime = ComputeLifetime(*msg);
  PostDeleteSelf(lifetime);

  RTC_LOG(LS_INFO) << ToString() << ": Created allocation with lifetime="
                   << lifetime.seconds();

  // We've already validated all the important bits; just send a response here.
  TurnMessage response(GetStunSuccessResponseTypeOrZero(*msg),
                       msg->transaction_id());

  auto mapped_addr_attr = std::make_unique<StunXorAddressAttribute>(
      STUN_ATTR_XOR_MAPPED_ADDRESS, conn_.src());
  auto relayed_addr_attr = std::make_unique<StunXorAddressAttribute>(
      STUN_ATTR_XOR_RELAYED_ADDRESS, external_socket_->GetLocalAddress());
  auto lifetime_attr = std::make_unique<StunUInt32Attribute>(
      STUN_ATTR_LIFETIME, lifetime.seconds());
  response.AddAttribute(std::move(mapped_addr_attr));
  response.AddAttribute(std::move(relayed_addr_attr));
  response.AddAttribute(std::move(lifetime_attr));

  SendResponse(&response);
}

void TurnServerAllocation::HandleRefreshRequest(const TurnMessage* msg) {
  // Figure out the new lifetime.
  TimeDelta lifetime = ComputeLifetime(*msg);

  // Reset the expiration timer.
  safety_.reset();
  PostDeleteSelf(lifetime);

  RTC_LOG(LS_INFO) << ToString()
                   << ": Refreshed allocation, lifetime=" << lifetime.seconds();

  // Send a success response with a LIFETIME attribute.
  TurnMessage response(GetStunSuccessResponseTypeOrZero(*msg),
                       msg->transaction_id());

  auto lifetime_attr = std::make_unique<StunUInt32Attribute>(
      STUN_ATTR_LIFETIME, lifetime.seconds());
  response.AddAttribute(std::move(lifetime_attr));

  SendResponse(&response);
}

void TurnServerAllocation::HandleSendIndication(const TurnMessage* msg,
                                                EcnMarking ecn) {
  // Check mandatory attributes.
  const StunByteStringAttribute* data_attr = msg->GetByteString(STUN_ATTR_DATA);
  const StunAddressAttribute* peer_attr =
      msg->GetAddress(STUN_ATTR_XOR_PEER_ADDRESS);
  if (!data_attr || !peer_attr) {
    RTC_LOG(LS_WARNING) << ToString() << ": Received invalid send indication";
    return;
  }

  // If a permission exists, send the data on to the peer.
  if (HasPermission(peer_attr->GetAddress().ipaddr())) {
    SendExternal(reinterpret_cast<char*>(data_attr->array_view().data()),
                 data_attr->length(), peer_attr->GetAddress(), ecn);
  } else {
    RTC_LOG(LS_WARNING) << ToString()
                        << ": Received send indication without permission"
                           " peer="
                        << peer_attr->GetAddress().ToSensitiveString();
  }
}

void TurnServerAllocation::HandleCreatePermissionRequest(
    const TurnMessage* msg) {
  RTC_DCHECK_RUN_ON(server_->thread_);
  // Check mandatory attributes.
  const StunAddressAttribute* peer_attr =
      msg->GetAddress(STUN_ATTR_XOR_PEER_ADDRESS);
  if (!peer_attr) {
    SendBadRequestResponse(msg);
    return;
  }

  if (server_->reject_private_addresses_ &&
      IPIsPrivate(peer_attr->GetAddress().ipaddr())) {
    SendErrorResponse(msg, STUN_ERROR_FORBIDDEN, STUN_ERROR_REASON_FORBIDDEN);
    return;
  }

  // Add this permission.
  AddPermission(peer_attr->GetAddress().ipaddr());

  RTC_LOG(LS_INFO) << ToString() << ": Created permission, peer="
                   << peer_attr->GetAddress().ToSensitiveString();

  // Send a success response.
  TurnMessage response(GetStunSuccessResponseTypeOrZero(*msg),
                       msg->transaction_id());
  SendResponse(&response);
}

void TurnServerAllocation::HandleChannelBindRequest(const TurnMessage* msg) {
  RTC_DCHECK_RUN_ON(server_->thread_);
  if (server_->reject_bind_requests_) {
    RTC_LOG(LS_ERROR) << "HandleChannelBindRequest: Rejecting bind requests";
    SendBadRequestResponse(msg);
    return;
  }

  // Check mandatory attributes.
  const StunUInt32Attribute* channel_attr =
      msg->GetUInt32(STUN_ATTR_CHANNEL_NUMBER);
  const StunAddressAttribute* peer_attr =
      msg->GetAddress(STUN_ATTR_XOR_PEER_ADDRESS);
  if (!channel_attr || !peer_attr) {
    SendBadRequestResponse(msg);
    return;
  }

  // Check that channel id is valid.
  uint16_t channel_id = static_cast<uint16_t>(channel_attr->value() >> 16);
  if (channel_id < kMinTurnChannelNumber ||
      channel_id > kMaxTurnChannelNumber) {
    SendBadRequestResponse(msg);
    return;
  }

  // Check that this channel id isn't bound to another transport address, and
  // that this transport address isn't bound to another channel id.
  auto channel1 = FindChannel(channel_id);
  auto channel2 = FindChannel(peer_attr->GetAddress());
  if (channel1 != channel2) {
    SendBadRequestResponse(msg);
    return;
  }

  // Add or refresh this channel.
  if (channel1 == channels_.end()) {
    channel1 = channels_.insert(
        channels_.end(), {.id = channel_id, .peer = peer_attr->GetAddress()});
  } else {
    channel1->pending_delete.reset();
  }
  thread_->PostDelayedTask(
      SafeTask(channel1->pending_delete.flag(),
               [this, channel1] { channels_.erase(channel1); }),
      kChannelTimeout);

  // Channel binds also refresh permissions.
  AddPermission(peer_attr->GetAddress().ipaddr());

  RTC_LOG(LS_INFO) << ToString() << ": Bound channel, id=" << channel_id
                   << ", peer=" << peer_attr->GetAddress().ToSensitiveString();

  // Send a success response.
  TurnMessage response(GetStunSuccessResponseTypeOrZero(*msg),
                       msg->transaction_id());
  SendResponse(&response);
}

void TurnServerAllocation::HandleChannelData(std::span<const uint8_t> payload,
                                             EcnMarking ecn) {
  // Extract the channel number from the data.
  uint16_t channel_id = GetBE16(payload);
  auto channel = FindChannel(channel_id);
  if (channel != channels_.end()) {
    // Send the data to the peer address.
    SendExternal(payload.data() + TURN_CHANNEL_HEADER_SIZE,
                 payload.size() - TURN_CHANNEL_HEADER_SIZE, channel->peer, ecn);
  } else {
    RTC_LOG(LS_WARNING) << ToString()
                        << ": Received channel data for invalid channel, id="
                        << channel_id;
  }
}

void TurnServerAllocation::OnExternalPacket(AsyncPacketSocket* socket,
                                            const ReceivedIpPacket& packet) {
  RTC_DCHECK(external_socket_.get() == socket);
  auto channel = FindChannel(packet.source_address());
  if (channel != channels_.end()) {
    // There is a channel bound to this address. Send as a channel message.
    ByteBufferWriter buf;
    buf.WriteUInt16(channel->id);
    buf.WriteUInt16(static_cast<uint16_t>(packet.payload().size()));
    buf.Write(std::span<const uint8_t>(packet.payload()));
    server_->Send(&conn_, buf, packet.ecn());
  } else if (!server_->enable_permission_checks_ ||
             HasPermission(packet.source_address().ipaddr())) {
    // No channel, but a permission exists. Send as a data indication.
    TurnMessage msg(TURN_DATA_INDICATION);
    msg.AddAttribute(std::make_unique<StunXorAddressAttribute>(
        STUN_ATTR_XOR_PEER_ADDRESS, packet.source_address()));
    msg.AddAttribute(std::make_unique<StunByteStringAttribute>(
        STUN_ATTR_DATA, packet.payload()));
    server_->SendStun(&conn_, &msg, packet.ecn());
  } else {
    RTC_LOG(LS_WARNING)
        << ToString() << ": Received external packet without permission, peer="
        << packet.source_address().ToSensitiveString();
  }
}

TimeDelta TurnServerAllocation::ComputeLifetime(const TurnMessage& msg) {
  if (const StunUInt32Attribute* attr = msg.GetUInt32(STUN_ATTR_LIFETIME)) {
    return std::min(TimeDelta::Seconds(static_cast<int>(attr->value())),
                    kDefaultAllocationTimeout);
  }
  return kDefaultAllocationTimeout;
}

bool TurnServerAllocation::HasPermission(const IPAddress& addr) {
  return FindPermission(addr) != perms_.end();
}

void TurnServerAllocation::AddPermission(const IPAddress& addr) {
  auto perm = FindPermission(addr);
  if (perm == perms_.end()) {
    perm = perms_.insert(perms_.end(), {.peer = addr});
  } else {
    perm->pending_delete.reset();
  }
  thread_->PostDelayedTask(SafeTask(perm->pending_delete.flag(),
                                    [this, perm] { perms_.erase(perm); }),
                           kPermissionTimeout);
}

TurnServerAllocation::PermissionList::iterator
TurnServerAllocation::FindPermission(const IPAddress& addr) {
  return absl::c_find_if(perms_,
                         [&](const Permission& p) { return p.peer == addr; });
}

TurnServerAllocation::ChannelList::iterator TurnServerAllocation::FindChannel(
    int channel_id) {
  return absl::c_find_if(channels_,
                         [&](const Channel& c) { return c.id == channel_id; });
}

TurnServerAllocation::ChannelList::iterator TurnServerAllocation::FindChannel(
    const SocketAddress& addr) {
  return absl::c_find_if(channels_,
                         [&](const Channel& c) { return c.peer == addr; });
}

void TurnServerAllocation::SendResponse(TurnMessage* msg) {
  // Success responses always have M-I.
  msg->AddMessageIntegrity(key_);
  server_->SendStun(&conn_, msg, EcnMarking::kNotEct);
}

void TurnServerAllocation::SendBadRequestResponse(const TurnMessage* req) {
  SendErrorResponse(req, STUN_ERROR_BAD_REQUEST, STUN_ERROR_REASON_BAD_REQUEST);
}

void TurnServerAllocation::SendErrorResponse(const TurnMessage* req,
                                             int code,
                                             absl::string_view reason) {
  server_->SendErrorResponse(&conn_, req, code, reason);
}

void TurnServerAllocation::SendExternal(const void* data,
                                        size_t size,
                                        const SocketAddress& peer,
                                        EcnMarking ecn) {
  AsyncSocketPacketOptions options;
  // TODO: bugs.webrtc.org/453581251  - If we want to properly test with TURN,
  // it must copy `ecn`.
  RTC_LOG_IF(LS_WARNING, ecn == EcnMarking::kCe)
      << "TurnServerAllocation does not properly support forwarding "
         "ECN.";
  options.ect_1 = (ecn == EcnMarking::kEct1);
  external_socket_->SendTo(data, size, peer, options);
}

void TurnServerAllocation::PostDeleteSelf(TimeDelta delay) {
  auto delete_self = [this] {
    RTC_DCHECK_RUN_ON(server_->thread_);
    server_->DestroyAllocation(this);
  };
  thread_->PostDelayedTask(SafeTask(safety_.flag(), std::move(delete_self)),
                           delay);
}

}  // namespace webrtc
