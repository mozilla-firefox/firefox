/*
 *  Copyright 2023 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/async_packet_socket.h"

#include "rtc_base/socket_address.h"
#include "rtc_base/third_party/sigslot/sigslot.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace rtc {
namespace {

using ::testing::MockFunction;

class MockAsyncPacketSocket : public rtc::AsyncPacketSocket {
 public:
  ~MockAsyncPacketSocket() = default;

  MOCK_METHOD(SocketAddress, GetLocalAddress, (), (const, override));
  MOCK_METHOD(SocketAddress, GetRemoteAddress, (), (const, override));
  MOCK_METHOD(int,
              Send,
              (const void* pv, size_t cb, const rtc::PacketOptions& options),
              (override));

  MOCK_METHOD(int,
              SendTo,
              (const void* pv,
               size_t cb,
               const SocketAddress& addr,
               const rtc::PacketOptions& options),
              (override));
  MOCK_METHOD(int, Close, (), (override));
  MOCK_METHOD(State, GetState, (), (const, override));
  MOCK_METHOD(int,
              GetOption,
              (rtc::Socket::Option opt, int* value),
              (override));
  MOCK_METHOD(int, SetOption, (rtc::Socket::Option opt, int value), (override));
  MOCK_METHOD(int, GetError, (), (const, override));
  MOCK_METHOD(void, SetError, (int error), (override));

  using AsyncPacketSocket::NotifyPacketReceived;
};

TEST(AsyncPacketSocket, RegisteredCallbackReceivePacketsFromNotify) {
  MockAsyncPacketSocket mock_socket;
  MockFunction<void(AsyncPacketSocket*, const rtc::ReceivedPacket&)>
      received_packet;

  EXPECT_CALL(received_packet, Call);
  mock_socket.RegisterReceivedPacketCallback(received_packet.AsStdFunction());
  mock_socket.NotifyPacketReceived(ReceivedPacket({}, SocketAddress()));
}

}  // namespace
}  // namespace rtc
