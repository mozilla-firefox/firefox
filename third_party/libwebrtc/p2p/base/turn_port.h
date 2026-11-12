/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef P2P_BASE_TURN_PORT_H_
#define P2P_BASE_TURN_PORT_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/functional/any_invocable.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "api/async_dns_resolver.h"
#include "api/candidate.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/transport/stun.h"
#include "p2p/base/connection.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/port_interface.h"
#include "p2p/base/stun_request.h"
#include "p2p/client/relay_port_factory_interface.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/dscp.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/ssl_certificate.h"

namespace webrtc {

class TurnCustomizer;

extern const int STUN_ATTR_TURN_LOGGING_ID;
extern const char TURN_PORT_TYPE[];
class TurnAllocateRequest;
class TurnEntry;

class TurnPort : public Port {
 public:
  enum PortState {
    STATE_CONNECTING,    // Initial state, cannot send any packets.
    STATE_CONNECTED,     // Socket connected, ready to send stun requests.
    STATE_READY,         // Received allocate success, can send any packets.
    STATE_RECEIVEONLY,   // Had REFRESH_REQUEST error, cannot send any packets.
    STATE_DISCONNECTED,  // TCP connection died, cannot send/receive any
                         // packets.
  };

  static bool Validate(const CreateRelayPortArgs& args) {
    // Do basic parameter validation.
    if (args.config->credentials.username.size() > kMaxTurnUsernameLength) {
      RTC_LOG(LS_ERROR) << "Attempt to use TURN with a too long username "
                        << "of length "
                        << args.config->credentials.username.size();
      return false;
    }
    // Do not connect to low-numbered ports. The default STUN port is 3478.
    if (!AllowedTurnPort(args.server_address->address.port())) {
      RTC_LOG(LS_ERROR) << "Attempt to use TURN to connect to port "
                        << args.server_address->address.port();
      return false;
    }
    return true;
  }

  // Create a TURN port using the shared UDP socket, `socket`.
  static std::unique_ptr<TurnPort> Create(CreateRelayPortArgs args,
                                          AsyncPacketSocket* socket) {
    if (!Validate(args)) {
      return nullptr;
    }
    // Using `new` to access a non-public constructor.
    return absl::WrapUnique(new TurnPort(
        {.env = args.env,
         .network_thread = args.network_thread,
         .socket_factory = args.socket_factory,
         .network = args.network,
         .ice_username_fragment = args.username,
         .ice_password = args.password,
         .content_name = args.content_name,
         .component = args.component,
         .generation = args.generation,
         .send_retransmit_count_attribute =
             args.send_retransmit_count_attribute,
         .lna_permission_factory = args.lna_permission_factory,
         .ice_tiebreaker = args.ice_tiebreaker,
         .callbacks = std::move(args.callbacks)},
        socket, *args.server_address, args.config->credentials,
        args.relative_priority, args.config->tls_alpn_protocols,
        args.config->tls_elliptic_curves, args.turn_customizer,
        args.config->tls_cert_verifier, args.config->tls_cert_policy,
        args.config->turn_logging_id));
  }

  // Create a TURN port that will use a new socket, bound to `network` and
  // using a port in the range between `min_port` and `max_port`.
  static std::unique_ptr<TurnPort> Create(CreateRelayPortArgs args,
                                          int min_port,
                                          int max_port) {
    if (!Validate(args)) {
      return nullptr;
    }
    // Using `new` to access a non-public constructor.
    return absl::WrapUnique(new TurnPort(
        {.env = args.env,
         .network_thread = args.network_thread,
         .socket_factory = args.socket_factory,
         .network = args.network,
         .ice_username_fragment = args.username,
         .ice_password = args.password,
         .content_name = args.content_name,
         .component = args.component,
         .generation = args.generation,
         .send_retransmit_count_attribute =
             args.send_retransmit_count_attribute,
         .lna_permission_factory = args.lna_permission_factory,
         .ice_tiebreaker = args.ice_tiebreaker,
         .callbacks = std::move(args.callbacks)},
        min_port, max_port, *args.server_address, args.config->credentials,
        args.relative_priority, args.config->tls_alpn_protocols,
        args.config->tls_elliptic_curves, args.turn_customizer,
        args.config->tls_cert_verifier, args.config->tls_cert_policy,
        args.config->turn_logging_id));
  }

  ~TurnPort() override;

  const ProtocolAddress& server_address() const { return server_address_; }
  // Returns an empty address if the local address has not been assigned.
  SocketAddress GetLocalAddress() const;

  bool ready() const { return state_ == STATE_READY; }
  bool connected() const {
    return state_ == STATE_READY || state_ == STATE_CONNECTED;
  }
  const RelayCredentials& credentials() const { return credentials_; }

  ProtocolType GetProtocol() const override;

  virtual TlsCertPolicy GetTlsCertPolicy() const;
  virtual void SetTlsCertPolicy(TlsCertPolicy tls_cert_policy);

  virtual const std::vector<std::string>& GetTlsAlpnProtocols() const;
  virtual const std::vector<std::string>& GetTlsEllipticCurves() const;

  // Release a TURN allocation by sending a refresh with lifetime 0.
  // Sets state to STATE_RECEIVEONLY.
  void Release();

  void PrepareAddress() override;
  Connection* CreateConnection(const Candidate& c,
                               PortInterface::CandidateOrigin origin) override;
  int SendTo(std::span<const uint8_t> data,
             const SocketAddress& addr,
             const AsyncSocketPacketOptions& options,
             bool payload) override;

  ABSL_DEPRECATE_AND_INLINE()
  int SendTo(const void* data,
             size_t size,
             const SocketAddress& addr,
             const AsyncSocketPacketOptions& options,
             bool payload) override {
    return SendTo(std::span(reinterpret_cast<const uint8_t*>(data), size), addr,
                  options, payload);
  }

  int SetOption(Socket::Option opt, int value) override;

  int GetOption(Socket::Option opt, int* value) override;
  int GetError() override;

  bool HandleIncomingPacket(AsyncPacketSocket* socket,
                            const ReceivedIpPacket& packet) override;
  bool CanHandleIncomingPacketsFrom(const SocketAddress& addr) const override;

  // Checks if a connection exists for `addr` before forwarding the call to
  // the base class.
  void SendBindingErrorResponse(StunMessage* message,
                                const SocketAddress& addr,
                                int error_code,
                                absl::string_view reason) override;

  virtual void OnReadPacket(AsyncPacketSocket* socket,
                            const ReceivedIpPacket& packet);

  void OnSentPacket(AsyncPacketSocket* socket,
                    const SentPacketInfo& sent_packet) override;
  virtual void OnReadyToSend(AsyncPacketSocket* socket);
  bool SupportsProtocol(absl::string_view protocol) const override;

  void OnSocketConnect(AsyncPacketSocket* socket);
  void OnSocketClose(AsyncPacketSocket* socket, int error);

  const std::string& hash() const { return hash_; }
  const std::string& nonce() const { return nonce_; }

  int error() const { return error_; }

  void OnAllocateMismatch();

  AsyncPacketSocket* socket() const { return socket_; }
  StunRequestManager& request_manager() { return request_manager_; }

  bool HasRequests() { return !request_manager_.empty(); }
  void set_credentials(const RelayCredentials& credentials) {
    credentials_ = credentials;
  }

  void HandleConnectionDestroyed(Connection* conn) override;

  void CloseForTest() { Close(); }

 protected:
  TurnPort(
      PortParametersRef args,
      AsyncPacketSocket* socket,
      const ProtocolAddress& server_address,
      const RelayCredentials& credentials,
      int server_priority,
      const std::vector<std::string>& tls_alpn_protocols,
      const std::vector<std::string>& tls_elliptic_curves,
      TurnCustomizer* customizer,
      SSLCertificateVerifier* tls_cert_verifier = nullptr,
      TlsCertPolicy tls_cert_policy = TlsCertPolicy::TLS_CERT_POLICY_SECURE,
      absl::string_view turn_logging_id = "");

  TurnPort(
      PortParametersRef args,
      uint16_t min_port,
      uint16_t max_port,
      const ProtocolAddress& server_address,
      const RelayCredentials& credentials,
      int server_priority,
      const std::vector<std::string>& tls_alpn_protocols,
      const std::vector<std::string>& tls_elliptic_curves,
      TurnCustomizer* customizer,
      SSLCertificateVerifier* tls_cert_verifier = nullptr,
      TlsCertPolicy tls_cert_policy = TlsCertPolicy::TLS_CERT_POLICY_SECURE,
      absl::string_view turn_logging_id = "");

  // NOTE: This method needs to be accessible for StunPort
  // return true if entry was created (i.e channel_number consumed).
  bool CreateOrRefreshEntry(Connection* conn, int channel_number);

  DiffServCodePoint StunDscpValue() const override;

  // Shuts down the turn port, frees requests and deletes connections.
  void Close();

 private:
  typedef std::map<Socket::Option, int> SocketOptionsMap;
  typedef std::set<SocketAddress> AttemptedServerSet;

  static bool AllowedTurnPort(int port);
  void TryAlternateServer();

  bool CreateTurnClientSocket();

  void set_nonce(absl::string_view nonce) { nonce_ = std::string(nonce); }
  void set_realm(absl::string_view realm);

  void OnRefreshError();
  void HandleRefreshError();
  bool SetAlternateServer(const SocketAddress& address);
  void ResolveTurnAddress(const SocketAddress& address);
  void OnResolveResult(const AsyncDnsResolverResult& result);
  // Requests Local Network Access permission for connecting to the current
  // server address, if needed. Runs `on_granted` if the permission is granted
  // or not needed, and fails the allocation otherwise.
  void MaybeRequestLocalNetworkAccessPermissionForServer(
      absl::AnyInvocable<void() &&> on_granted);
  void OnLocalNetworkAccessPermissionGranted();

  void AddRequestAuthInfo(StunMessage* msg);
  void OnSendStunPacket(std::span<const uint8_t> data, StunRequest* request);
  // Stun address from allocate success response.
  // Currently used only for testing.
  void OnStunAddress(const SocketAddress& address);
  void OnAllocateSuccess(const SocketAddress& address,
                         const SocketAddress& stun_address);
  void OnAllocateError(int error_code, absl::string_view reason);
  void OnAllocateRequestTimeout();

  void HandleDataIndication(const ReceivedIpPacket& packet);
  void HandleChannelData(uint16_t channel_id, const ReceivedIpPacket& packet);
  void DispatchPacket(const ReceivedIpPacket& packet, ProtocolType proto);

  bool ScheduleRefresh(uint32_t lifetime);
  void SendRequest(StunRequest* request, int delay);
  int Send(std::span<const uint8_t> data,
           const AsyncSocketPacketOptions& options);
  void UpdateHash();
  bool UpdateNonce(StunMessage* response);
  void ResetNonce();

  bool HasPermission(const IPAddress& ipaddr) const;
  TurnEntry* FindEntry(const SocketAddress& address) const;
  TurnEntry* FindEntry(uint16_t channel_id) const;

  // Marks the connection with remote address `address` failed and
  // pruned (a.k.a. write-timed-out). Returns true if a connection is found.
  bool FailAndPruneConnection(const SocketAddress& address);

  void MaybeAddTurnLoggingId(StunMessage* message);

  void TurnCustomizerMaybeModifyOutgoingStunMessage(StunMessage* message);
  bool TurnCustomizerAllowChannelData(std::span<const uint8_t> data,
                                      bool payload);

  ProtocolAddress server_address_;
  // Reconstruct the URL of the server which the candidate is gathered from.
  // A copy needs to be stored as server_address_ will resolve and clear its
  // hostname field.
  std::string ReconstructServerUrl();
  const std::string server_url_;

  TlsCertPolicy tls_cert_policy_ = TlsCertPolicy::TLS_CERT_POLICY_SECURE;
  const std::vector<std::string> tls_alpn_protocols_;
  const std::vector<std::string> tls_elliptic_curves_;
  SSLCertificateVerifier* const tls_cert_verifier_;
  RelayCredentials credentials_;
  AttemptedServerSet attempted_server_addresses_;

  AsyncPacketSocket* socket_;
  std::unique_ptr<AsyncPacketSocket> owned_socket_;
  SocketOptionsMap socket_options_;
  std::unique_ptr<AsyncDnsResolverInterface> resolver_;
  int error_;
  DiffServCodePoint stun_dscp_value_;

  StunRequestManager request_manager_;
  std::string realm_;  // From 401/438 response message.
  std::string nonce_;  // From 401/438 response message.
  std::string hash_;   // Digest of username:realm:password

  int next_channel_number_;
  std::vector<std::unique_ptr<TurnEntry>> entries_;

  PortState state_;
  // By default the value will be set to 0. This value will be used in
  // calculating the candidate priority.
  const int server_priority_;

  // The number of retries made due to allocate mismatch error.
  size_t allocate_mismatch_retries_;

  // Optional TurnCustomizer that can modify outgoing messages. Once set, this
  // must outlive the TurnPort's lifetime.
  TurnCustomizer* const turn_customizer_;

  // Optional TurnLoggingId.
  // An identifier set by application that is added to TURN_ALLOCATE_REQUEST
  // and can be used to match client/backend logs.
  const std::string turn_logging_id_;

  ScopedTaskSafety task_safety_;

  friend class TurnEntry;
  friend class TurnAllocateRequest;
  friend class TurnRefreshRequest;
  friend class TurnCreatePermissionRequest;
  friend class TurnChannelBindRequest;
};

}  // namespace webrtc


#endif  // P2P_BASE_TURN_PORT_H_
