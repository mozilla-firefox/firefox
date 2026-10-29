/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/network/fake_network_socket_server.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/test/network_emulation/network_emulation_interfaces.h"
#include "api/transport/ecn_marking.h"
#include "api/units/time_delta.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/event.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_annotations.h"
#include "test/network/network_emulation.h"

namespace webrtc {
namespace test {
namespace {
std::string ToString(const SocketAddress& addr) {
  return addr.HostAsURIString() + ":" + std::to_string(addr.port());
}

}  // namespace

// Represents a socket, which will operate with emulated network.
class FakeNetworkSocket : public Socket,
                          public EmulatedNetworkReceiverInterface {
 public:
  explicit FakeNetworkSocket(FakeNetworkSocketServer* scoket_manager,
                             Thread* thread);
  ~FakeNetworkSocket() override;

  // Will be invoked by EmulatedEndpoint to deliver packets into this socket.
  void OnPacketReceived(EmulatedIpPacket packet) override;

  // webrtc::Socket methods:
  SocketAddress GetLocalAddress() const override;
  SocketAddress GetRemoteAddress() const override;
  int Bind(const SocketAddress& addr) override;
  int Connect(const SocketAddress& addr) override;
  int Close() override;
  int Send(const void* pv, size_t cb) override;
  int SendTo(const void* pv, size_t cb, const SocketAddress& addr) override;
  int Recv(void* pv, size_t cb, int64_t* timestamp) override {
    RTC_DCHECK_NOTREACHED() << " Use RecvFrom instead.";
    return 0;
  }
  int RecvFrom(ReceiveBuffer& buffer) override;
  int Listen(int backlog) override;
  Socket* Accept(SocketAddress* paddr) override;
  int GetError() const override;
  void SetError(int error) override;
  ConnState GetState() const override;
  int GetOption(Option opt, int* value) override;
  int SetOption(Option opt, int value) override;

 private:
  FakeNetworkSocketServer* const socket_server_;
  Thread* const thread_;
  EmulatedEndpointImpl* endpoint_ RTC_GUARDED_BY(&thread_);
  SocketAddress local_addr_ RTC_GUARDED_BY(&thread_);
  SocketAddress remote_addr_ RTC_GUARDED_BY(&thread_);
  ConnState state_ RTC_GUARDED_BY(&thread_);
  int error_ RTC_GUARDED_BY(&thread_);
  std::map<Option, int> options_map_ RTC_GUARDED_BY(&thread_);

  std::optional<EmulatedIpPacket> pending_ RTC_GUARDED_BY(thread_);
  scoped_refptr<PendingTaskSafetyFlag> alive_;
};

FakeNetworkSocket::FakeNetworkSocket(FakeNetworkSocketServer* socket_server,
                                     Thread* thread)
    : socket_server_(socket_server),
      thread_(thread),
      state_(CS_CLOSED),
      error_(0),
      alive_(PendingTaskSafetyFlag::Create()) {}

FakeNetworkSocket::~FakeNetworkSocket() {
  // Abandon all pending packets.
  alive_->SetNotAlive();

  Close();
  socket_server_->Unregister(this);
}

void FakeNetworkSocket::OnPacketReceived(EmulatedIpPacket packet) {
  auto task = [this, packet = std::move(packet)]() mutable {
    RTC_DCHECK_RUN_ON(thread_);
    if (!endpoint_->Enabled())
      return;
    RTC_DCHECK(!pending_);
    pending_ = std::move(packet);
    // Note that we expect that this will trigger exactly one call to RecvFrom()
    // where pending_packet will be read and reset. This call is done without
    // any thread switch (see AsyncUDPSocket::OnReadEvent) so it's safe to
    // assume that SignalReadEvent() will block until the packet has been read.
    NotifyReadEvent(this);
    RTC_DCHECK(!pending_);
  };
  thread_->PostTask(SafeTask(alive_, std::move(task)));
  socket_server_->WakeUp();
}

SocketAddress FakeNetworkSocket::GetLocalAddress() const {
  RTC_DCHECK_RUN_ON(thread_);
  return local_addr_;
}

SocketAddress FakeNetworkSocket::GetRemoteAddress() const {
  RTC_DCHECK_RUN_ON(thread_);
  return remote_addr_;
}

int FakeNetworkSocket::Bind(const SocketAddress& addr) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_CHECK(local_addr_.IsNil())
      << "Socket already bound to address: " << ToString(local_addr_);
  local_addr_ = addr;
  endpoint_ = socket_server_->GetEndpointNode(local_addr_.ipaddr());
  if (!endpoint_) {
    local_addr_.Clear();
    RTC_LOG(LS_INFO) << "No endpoint for address: " << ToString(addr);
    error_ = EADDRNOTAVAIL;
    return 2;
  }
  std::optional<uint16_t> port =
      endpoint_->BindReceiver(local_addr_.port(), this);
  if (!port) {
    local_addr_.Clear();
    RTC_LOG(LS_INFO) << "Cannot bind to in-use address: " << ToString(addr);
    error_ = EADDRINUSE;
    return 1;
  }
  local_addr_.SetPort(port.value());
  return 0;
}

int FakeNetworkSocket::Connect(const SocketAddress& addr) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_CHECK(remote_addr_.IsNil())
      << "Socket already connected to address: " << ToString(remote_addr_);
  RTC_CHECK(!local_addr_.IsNil())
      << "Socket have to be bind to some local address";
  remote_addr_ = addr;
  state_ = CS_CONNECTED;
  return 0;
}

int FakeNetworkSocket::Send(const void* pv, size_t cb) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_CHECK(state_ == CS_CONNECTED) << "Socket cannot send: not connected";
  return SendTo(pv, cb, remote_addr_);
}

int FakeNetworkSocket::SendTo(const void* pv,
                              size_t cb,
                              const SocketAddress& addr) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_CHECK(!local_addr_.IsNil())
      << "Socket have to be bind to some local address";
  if (!endpoint_->Enabled()) {
    error_ = ENETDOWN;
    return -1;
  }
  CopyOnWriteBuffer packet(std::span(static_cast<const uint8_t*>(pv), cb));
  EcnMarking ecn = EcnMarking::kNotEct;
  auto it = options_map_.find(OPT_SEND_ECN);
  if (it != options_map_.end() && it->second == 1) {
    ecn = EcnMarking::kEct1;
  }

  endpoint_->SendPacket(local_addr_, addr, packet, /*application_overhead=*/0,
                        ecn);
  return cb;
}

int FakeNetworkSocket::RecvFrom(ReceiveBuffer& buffer) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_CHECK(pending_);
  buffer.source_address = pending_->from;
  buffer.arrival_time = pending_->arrival_time;
  buffer.payload.SetData(pending_->cdata(), pending_->size());
  buffer.ecn = pending_->ecn;
  pending_.reset();
  return buffer.payload.size();
}

int FakeNetworkSocket::Listen(int backlog) {
  RTC_CHECK(false) << "Listen() isn't valid for SOCK_DGRAM";
}

Socket* FakeNetworkSocket::Accept(SocketAddress* /*paddr*/) {
  RTC_CHECK(false) << "Accept() isn't valid for SOCK_DGRAM";
}

int FakeNetworkSocket::Close() {
  RTC_DCHECK_RUN_ON(thread_);
  state_ = CS_CLOSED;
  if (!local_addr_.IsNil()) {
    endpoint_->UnbindReceiver(local_addr_.port());
  }
  local_addr_.Clear();
  remote_addr_.Clear();
  return 0;
}

int FakeNetworkSocket::GetError() const {
  RTC_DCHECK_RUN_ON(thread_);
  return error_;
}

void FakeNetworkSocket::SetError(int error) {
  RTC_DCHECK_RUN_ON(thread_);
  RTC_CHECK(error == 0);
  error_ = error;
}

Socket::ConnState FakeNetworkSocket::GetState() const {
  RTC_DCHECK_RUN_ON(thread_);
  return state_;
}

int FakeNetworkSocket::GetOption(Option opt, int* value) {
  RTC_DCHECK_RUN_ON(thread_);
  auto it = options_map_.find(opt);
  if (it == options_map_.end()) {
    return -1;
  }
  *value = it->second;
  return 0;
}

int FakeNetworkSocket::SetOption(Option opt, int value) {
  RTC_DCHECK_RUN_ON(thread_);
  options_map_[opt] = value;
  return 0;
}

FakeNetworkSocketServer::FakeNetworkSocketServer(
    EndpointsContainer* endpoints_container)
    : endpoints_container_(endpoints_container),
      wakeup_(/*manual_reset=*/false, /*initially_signaled=*/false) {}
FakeNetworkSocketServer::~FakeNetworkSocketServer() = default;

EmulatedEndpointImpl* FakeNetworkSocketServer::GetEndpointNode(
    const IPAddress& ip) {
  return endpoints_container_->LookupByLocalAddress(ip);
}

void FakeNetworkSocketServer::Unregister(FakeNetworkSocket* socket) {
  MutexLock lock(&lock_);
  sockets_.erase(absl::c_find(sockets_, socket));
}

Socket* FakeNetworkSocketServer::CreateSocket(int family, int type) {
  RTC_DCHECK(family == AF_INET || family == AF_INET6);
  // We support only UDP sockets for now.
  RTC_DCHECK(type == SOCK_DGRAM) << "Only UDP sockets are supported";
  RTC_DCHECK(thread_) << "must be attached to thread before creating sockets";
  FakeNetworkSocket* out = new FakeNetworkSocket(this, thread_);
  {
    MutexLock lock(&lock_);
    sockets_.push_back(out);
  }
  return out;
}

void FakeNetworkSocketServer::SetMessageQueue(Thread* thread) {
  thread_ = thread;
}

// Always returns true (if return false, it won't be invoked again...)
bool FakeNetworkSocketServer::Wait(TimeDelta max_wait_duration,
                                   bool process_io) {
  RTC_DCHECK(thread_ == Thread::Current());
  if (!max_wait_duration.IsZero())
    wakeup_.Wait(max_wait_duration);

  return true;
}

void FakeNetworkSocketServer::WakeUp() {
  wakeup_.Set();
}

}  // namespace test
}  // namespace webrtc
