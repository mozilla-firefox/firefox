/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef P2P_BASE_PORT_INTERFACE_H_
#define P2P_BASE_PORT_INTERFACE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/packet_socket_factory.h"
#include "api/task_queue/task_queue_base.h"
#include "p2p/base/transport_description.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/dscp.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/network.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/span_helpers.h"

namespace webrtc {

class Connection;
class IceMessage;
class StunMessage;
class StunStats;

// Defines the interface for a port, which represents a local communication
// mechanism that can be used to create connections to similar mechanisms of
// the other client. Various types of ports will implement this interface.
class PortInterface {
 public:
  virtual ~PortInterface();

  virtual IceCandidateType Type() const = 0;
  virtual const Network* Network() const = 0;

  // Methods to set/get ICE role and tiebreaker values.
  virtual void SetIceRole(IceRole role) = 0;
  virtual IceRole GetIceRole() const = 0;

  virtual uint64_t IceTiebreaker() const = 0;

  virtual bool SharedSocket() const = 0;

  virtual bool SupportsProtocol(absl::string_view protocol) const = 0;

  // PrepareAddress will attempt to get an address for this port that other
  // clients can send to.  It may take some time before the address is ready.
  // Once it is ready, we will send SignalAddressReady.  If errors are
  // preventing the port from getting an address, it may send
  // SignalAddressError.
  virtual void PrepareAddress() = 0;

  // Returns the connection to the given address or NULL if none exists.
  virtual Connection* GetConnection(const SocketAddress& remote_addr) = 0;

  // Creates a new connection to the given address.
  enum CandidateOrigin { ORIGIN_THIS_PORT, ORIGIN_OTHER_PORT, ORIGIN_MESSAGE };
  virtual Connection* CreateConnection(const Candidate& remote_candidate,
                                       CandidateOrigin origin) = 0;

  // Functions on the underlying socket(s).
  virtual int SetOption(Socket::Option opt, int value) = 0;
  virtual int GetOption(Socket::Option opt, int* value) = 0;
  virtual int GetError() = 0;

  virtual ProtocolType GetProtocol() const = 0;

  virtual const std::vector<Candidate>& Candidates() const = 0;

  // Sends the given packet to the given address, provided that the address is
  // that of a connection or an address that has sent to us already.
  virtual int SendTo(std::span<const uint8_t> data,
                     const SocketAddress& addr,
                     const AsyncSocketPacketOptions& options,
                     bool payload) {
    // This function has a default implementation in order to support
    // downstream code that has subclasses with the old SendTo method.
    // If a subclass implements neither, this will cause a recursion,
    // which should be easy to detect.
    // TODO: bugs.webrtc.org/42225170 - make pure virtual when the function
    // below has been removed.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return SendTo(data.data(), data.size(), addr, options, payload);
#pragma clang diagnostic pop
  }

  // This function is not marked ABSL_DEPRECATE_AND_INLINE() because
  // blindly inlining it will lead to unexpected behavior in subclasses.
  [[deprecated("Use version with span")]] virtual int SendTo(
      const void* data,
      size_t size,
      const SocketAddress& addr,
      const AsyncSocketPacketOptions& options,
      bool payload) {
    return SendTo(AsUint8Span(std::span(static_cast<const char*>(data), size)),
                  addr, options, payload);
  }

  // Indicates that we received a successful STUN binding request from an
  // address that doesn't correspond to any current connection.  To turn this
  // into a real connection, call CreateConnection.
  virtual void SubscribeUnknownAddress(
      const void* tag,
      absl::AnyInvocable<void(PortInterface*,
                              const SocketAddress&,
                              ProtocolType,
                              IceMessage*,
                              const std::string&,
                              bool)> callback) = 0;

  virtual void NotifyUnknownAddress(PortInterface* port,
                                    const SocketAddress& address,
                                    ProtocolType proto,
                                    IceMessage* msg,
                                    const std::string& rf,
                                    bool port_muxed) = 0;
  // Sends a response message (normal or error) to the given request.  One of
  // these methods should be called as a response to SignalUnknownAddress.
  virtual void SendBindingErrorResponse(StunMessage* message,
                                        const SocketAddress& addr,
                                        int error_code,
                                        absl::string_view reason) = 0;

  // Signaled when this port decides to delete itself because it no longer has
  // any usefulness.
  virtual void SubscribePortDestroyed(
      const void* tag,
      std::function<void(PortInterface*)> callback) = 0;

  // Signaled when Port discovers ice role conflict with the peer.
  virtual void SubscribeRoleConflict(absl::AnyInvocable<void()> callback) = 0;
  virtual void NotifyRoleConflict() = 0;

  // Normally, packets arrive through a connection (or they result signaling of
  // unknown address).  Calling this method turns off delivery of packets
  // through this port.
  virtual void EnablePortPackets() = 0;
  virtual void SubscribeReadPacket(
      const void* tag,
      absl::AnyInvocable<void(PortInterface*,
                              std::span<const uint8_t>,
                              const SocketAddress&)> callback) = 0;

  ABSL_DEPRECATE_AND_INLINE()
  virtual void SubscribeReadPacket(
      absl::AnyInvocable<
          void(PortInterface*, const char*, size_t, const SocketAddress&)>
          callback) {
    SubscribeReadPacket(nullptr, std::move(callback));
  }

  ABSL_DEPRECATE_AND_INLINE()
  virtual void SubscribeReadPacket(
      const void* tag,
      absl::AnyInvocable<
          void(PortInterface*, const char*, size_t, const SocketAddress&)>
          callback) {
    SubscribeReadPacket(
        tag, [cb = std::move(callback)](PortInterface* port,
                                        std::span<const uint8_t> data,
                                        const SocketAddress& addr) mutable {
          cb(port, AsCharSpan(data).data(), data.size(), addr);
        });
  }
  virtual void NotifyReadPacket(PortInterface* port_interface,
                                std::span<const uint8_t> data,
                                const SocketAddress& addr) = 0;

  // Emitted each time a packet is sent on this port.
  virtual void SubscribeSentPacket(
      const void* tag,
      absl::AnyInvocable<void(const SentPacketInfo&)> callback) = 0;
  virtual void NotifySentPacket(const SentPacketInfo& packet) = 0;

  virtual std::string ToString() const = 0;

  virtual void GetStunStats(std::optional<StunStats>* stats) = 0;

  // Removes and deletes a connection object. `DestroyConnection` will
  // delete the connection object directly whereas `DestroyConnectionAsync`
  // defers the `delete` operation to when the call stack has been unwound.
  // Async may be needed when deleting a connection object from within a
  // callback.
  virtual void DestroyConnection(Connection* conn) = 0;

  virtual void DestroyConnectionAsync(Connection* conn) = 0;

  // The thread on which this port performs its I/O.
  virtual TaskQueueBase* thread() = 0;

  // The factory used to create the sockets of this port.
  virtual PacketSocketFactory* socket_factory() const = 0;

  // Identifies the generation that this port was created in.
  virtual uint32_t generation() const = 0;
  virtual bool send_retransmit_count_attribute() const = 0;
  // For debugging purposes.
  virtual const std::string& content_name() const = 0;

  // Called when the Connection discovers a local peer reflexive candidate.
  virtual void AddPrflxCandidate(const Candidate& local) = 0;

 protected:
  PortInterface();
  virtual void UpdateNetworkCost() = 0;

  // Returns DSCP value packets generated by the port itself should use.
  virtual DiffServCodePoint StunDscpValue() const = 0;

  // If the given data comprises a complete and correct STUN message then the
  // return value is true, otherwise false. If the message username corresponds
  // with this port's username fragment, msg will contain the parsed STUN
  // message.  Otherwise, the function may send a STUN response internally.
  // remote_username contains the remote fragment of the STUN username.
  virtual bool GetStunMessage(std::span<const uint8_t> data,
                              const SocketAddress& addr,
                              std::unique_ptr<IceMessage>* out_msg,
                              std::string* out_username) = 0;

  // This method will return local and remote username fragements from the
  // stun username attribute if present.
  virtual bool ParseStunUsername(const StunMessage* stun_msg,
                                 std::string* local_username,
                                 std::string* remote_username) const = 0;
  virtual std::string CreateStunUsername(
      absl::string_view remote_username) const = 0;

  virtual bool MaybeIceRoleConflict(const SocketAddress& addr,
                                    IceMessage* stun_msg,
                                    absl::string_view remote_ufrag) = 0;

  virtual int16_t network_cost() const = 0;
  // Connection and Port are entangled; functions exposed to Port only
  // should not be public.
  friend class Connection;
};

}  //  namespace webrtc

#endif  // P2P_BASE_PORT_INTERFACE_H_
