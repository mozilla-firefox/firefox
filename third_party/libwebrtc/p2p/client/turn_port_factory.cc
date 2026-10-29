/*
 *  Copyright 2017 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "p2p/client/turn_port_factory.h"

#include <memory>
#include <utility>

#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/turn_port.h"
#include "p2p/client/relay_port_factory_interface.h"
#include "rtc_base/async_packet_socket.h"

namespace webrtc {

TurnPortFactory::~TurnPortFactory() {}

std::unique_ptr<Port> TurnPortFactory::Create(CreateRelayPortArgs&& args,
                                              AsyncPacketSocket* udp_socket) {
  const RelayServerConfig* config = args.config;
  std::unique_ptr<TurnPort> port =
      TurnPort::Create(std::move(args), udp_socket);
  if (port == nullptr) {
    return nullptr;
  }
  port->SetTlsCertPolicy(config->tls_cert_policy);
  port->SetTurnLoggingId(config->turn_logging_id);
  return port;
}

std::unique_ptr<Port> TurnPortFactory::Create(CreateRelayPortArgs&& args,
                                              int min_port,
                                              int max_port) {
  const RelayServerConfig* config = args.config;
  std::unique_ptr<TurnPort> port =
      TurnPort::Create(std::move(args), min_port, max_port);
  if (port == nullptr) {
    return nullptr;
  }
  port->SetTlsCertPolicy(config->tls_cert_policy);
  port->SetTurnLoggingId(config->turn_logging_id);
  return port;
}

}  // namespace webrtc
