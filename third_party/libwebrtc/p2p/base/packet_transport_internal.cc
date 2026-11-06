/*
 *  Copyright 2017 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "p2p/base/packet_transport_internal.h"

#include "api/sequence_checker.h"
#include "rtc_base/network/received_packet.h"

namespace rtc {

PacketTransportInternal::PacketTransportInternal() = default;

PacketTransportInternal::~PacketTransportInternal() = default;

bool PacketTransportInternal::GetOption(rtc::Socket::Option opt, int* value) {
  return false;
}

absl::optional<NetworkRoute> PacketTransportInternal::network_route() const {
  return absl::optional<NetworkRoute>();
}

void PacketTransportInternal::RegisterReceivedPacketCallback(
    void* id,
    absl::AnyInvocable<void(PacketTransportInternal*,
                            const rtc::ReceivedPacket&)> callback) {
  RTC_DCHECK_RUN_ON(&network_checker_);
  received_packet_callback_list_.AddReceiver(id, std::move(callback));
}

void PacketTransportInternal::DeregisterReceivedPacketCallback(void* id) {
  RTC_DCHECK_RUN_ON(&network_checker_);
  received_packet_callback_list_.RemoveReceivers(id);
}

void PacketTransportInternal::SetOnCloseCallback(
    absl::AnyInvocable<void() &&> callback) {
  RTC_DCHECK_RUN_ON(&network_checker_);
  RTC_DCHECK(!on_close_ || !callback);
  on_close_ = std::move(callback);
}

void PacketTransportInternal::NotifyPacketReceived(
    const rtc::ReceivedPacket& packet) {
  RTC_DCHECK_RUN_ON(&network_checker_);
  received_packet_callback_list_.Send(this, packet);
}

void PacketTransportInternal::NotifyOnClose() {
  RTC_DCHECK_RUN_ON(&network_checker_);
  if (on_close_) {
    std::move(on_close_)();
    on_close_ = nullptr;
  }
}

}  // namespace rtc
