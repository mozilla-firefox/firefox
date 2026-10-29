/*
 *  Copyright 2017 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef P2P_CLIENT_RELAY_PORT_FACTORY_INTERFACE_H_
#define P2P_CLIENT_RELAY_PORT_FACTORY_INTERFACE_H_

#include <cstdint>
#include <memory>
#include <string>

#include "api/environment/environment.h"
#include "api/local_network_access_permission.h"
#include "api/packet_socket_factory.h"
#include "api/task_queue/task_queue_base.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/network.h"

namespace webrtc {
class TurnCustomizer;
class FieldTrialsView;
}  // namespace webrtc

namespace webrtc {

// A struct containing arguments to RelayPortFactory::Create()
struct CreateRelayPortArgs {
  Environment env;
  TaskQueueBase* network_thread;
  PacketSocketFactory* socket_factory;
  const Network* network;
  const ProtocolAddress* server_address;
  const RelayServerConfig* config;
  std::string username;
  std::string password;
  std::string content_name;
  TurnCustomizer* turn_customizer = nullptr;
  // Relative priority of candidates from this TURN server in relation
  // to the candidates from other servers. Required because ICE priorities
  // need to be unique.
  int relative_priority = 0;
  LocalNetworkAccessPermissionFactoryInterface* lna_permission_factory =
      nullptr;
  uint64_t ice_tiebreaker = 0;
  int component = ICE_CANDIDATE_COMPONENT_DEFAULT;
  uint32_t generation = 0;
  bool send_retransmit_count_attribute = false;
  Port::Callbacks callbacks;
};

// A factory for creating RelayPort's.
class RelayPortFactoryInterface {
 public:
  virtual ~RelayPortFactoryInterface() {}

  // This variant is used for UDP connection to the relay server
  // using a already existing shared socket.
  virtual std::unique_ptr<Port> Create(CreateRelayPortArgs&& args,
                                       AsyncPacketSocket* udp_socket) = 0;

  // This variant is used for the other cases.
  virtual std::unique_ptr<Port> Create(CreateRelayPortArgs&& args,
                                       int min_port,
                                       int max_port) = 0;

 protected:
  RelayPortFactoryInterface() = default;
};

}  //  namespace webrtc


#endif  // P2P_CLIENT_RELAY_PORT_FACTORY_INTERFACE_H_
