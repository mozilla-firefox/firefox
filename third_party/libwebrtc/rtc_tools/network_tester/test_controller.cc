/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_tools/network_tester/test_controller.h"

#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>

#include "api/environment/environment.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/units/timestamp.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/checks.h"
#include "rtc_base/internal/default_socket_server.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/thread.h"
#include "rtc_tools/network_tester/packet_sender.h"

namespace webrtc {

TestController::TestController(const Environment& env,
                               int min_port,
                               int max_port,
                               const std::string& config_file_path,
                               const std::string& log_file_path)
    : env_(env),
      socket_server_(CreateDefaultSocketServer()),
      packet_sender_thread_(std::make_unique<Thread>(socket_server_.get())),
      socket_factory_(socket_server_.get()),
      config_file_path_(config_file_path),
      packet_logger_(log_file_path),
      local_test_done_(false),
      remote_test_done_(false),
      task_safety_flag_(PendingTaskSafetyFlag::CreateDetached()) {
  RTC_DCHECK_RUN_ON(&test_controller_thread_checker_);
  send_data_.fill(42);
  packet_sender_thread_->SetName("PacketSender", nullptr);
  packet_sender_thread_->Start();
  local_port_ = packet_sender_thread_->BlockingCall([&] {
    RTC_DCHECK_RUN_ON(packet_sender_thread_.get());
    udp_socket_ = socket_factory_.CreateUdpSocket(
        env_, SocketAddress(GetAnyIP(AF_INET), 0), min_port, max_port);
    RTC_CHECK(udp_socket_ != nullptr);
    udp_socket_->RegisterReceivedPacketCallback(
        [&](AsyncPacketSocket* socket, const ReceivedIpPacket& packet) {
          OnReadPacket(socket, packet);
        });
    return udp_socket_->GetLocalAddress().port();
  });
}

TestController::~TestController() {
  RTC_DCHECK_RUN_ON(&test_controller_thread_checker_);
  packet_sender_thread_->BlockingCall(
      [this]() { task_safety_flag_->SetNotAlive(); });
}

void TestController::SendConnectTo(const std::string& hostname, int port) {
  RTC_DCHECK_RUN_ON(&test_controller_thread_checker_);
  remote_address_ = SocketAddress(hostname, port);
  NetworkTesterPacket packet;
  packet.set_type(NetworkTesterPacket::HAND_SHAKING);
  SendData(packet, std::nullopt);
  MutexLock scoped_lock(&test_done_lock_);
  local_test_done_ = false;
  remote_test_done_ = false;
}

void TestController::SendData(const NetworkTesterPacket& packet,
                              std::optional<size_t> data_size) {
  if (!packet_sender_thread_->IsCurrent()) {
    packet_sender_thread_->PostTask(SafeTask(
        task_safety_flag_,
        [this, packet, data_size]() { this->SendData(packet, data_size); }));
    return;
  }
  RTC_DCHECK_RUN_ON(packet_sender_thread_.get());
  RTC_LOG(LS_VERBOSE) << "SendData";

  // Can be call from packet_sender or from test_controller thread.
  size_t packet_size = packet.ByteSizeLong();
  send_data_[0] = packet_size;
  packet_size++;
  packet.SerializeToArray(&send_data_[1], std::numeric_limits<char>::max());
  if (data_size && *data_size > packet_size)
    packet_size = *data_size;
  udp_socket_->SendTo((const void*)send_data_.data(), packet_size,
                      remote_address_, AsyncSocketPacketOptions());
}

void TestController::OnTestDone() {
  RTC_DCHECK_RUN_ON(packet_sender_thread_.get());
  NetworkTesterPacket packet;
  packet.set_type(NetworkTesterPacket::TEST_DONE);
  SendData(packet, std::nullopt);
  MutexLock scoped_lock(&test_done_lock_);
  local_test_done_ = true;
}

bool TestController::IsTestDone() {
  RTC_DCHECK_RUN_ON(&test_controller_thread_checker_);
  MutexLock scoped_lock(&test_done_lock_);
  return local_test_done_ && remote_test_done_;
}

void TestController::OnReadPacket(AsyncPacketSocket* socket,
                                  const ReceivedIpPacket& received_packet) {
  RTC_DCHECK_RUN_ON(packet_sender_thread_.get());
  RTC_LOG(LS_VERBOSE) << "OnReadPacket";
  size_t packet_size = received_packet.payload()[0];
  std::string receive_data(
      reinterpret_cast<const char*>(&received_packet.payload()[1]),
      packet_size);
  NetworkTesterPacket packet;
  packet.ParseFromString(receive_data);
  RTC_CHECK(packet.has_type());
  switch (packet.type()) {
    case NetworkTesterPacket::HAND_SHAKING: {
      NetworkTesterPacket start_packet;
      start_packet.set_type(NetworkTesterPacket::TEST_START);
      remote_address_ = received_packet.source_address();
      SendData(start_packet, std::nullopt);
      packet_sender_.reset(
          new PacketSender(env_, this, packet_sender_thread_.get(),
                           task_safety_flag_, config_file_path_));
      packet_sender_->StartSending();
      MutexLock scoped_lock(&test_done_lock_);
      local_test_done_ = false;
      remote_test_done_ = false;
      break;
    }
    case NetworkTesterPacket::TEST_START: {
      packet_sender_.reset(
          new PacketSender(env_, this, packet_sender_thread_.get(),
                           task_safety_flag_, config_file_path_));
      packet_sender_->StartSending();
      MutexLock scoped_lock(&test_done_lock_);
      local_test_done_ = false;
      remote_test_done_ = false;
      break;
    }
    case NetworkTesterPacket::TEST_DATA: {
      packet.set_arrival_timestamp(
          received_packet.arrival_time().value_or(Timestamp::Zero()).us());
      packet.set_packet_size(received_packet.payload().size());
      packet_logger_.LogPacket(packet);
      break;
    }
    case NetworkTesterPacket::TEST_DONE: {
      MutexLock scoped_lock(&test_done_lock_);
      remote_test_done_ = true;
      break;
    }
    default: {
      RTC_DCHECK_NOTREACHED();
    }
  }
}

}  // namespace webrtc
