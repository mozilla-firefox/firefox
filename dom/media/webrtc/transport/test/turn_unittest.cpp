/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

// Some code copied from nICEr. License is:
/*
Copyright (c) 2007, Adobe Systems, Incorporated
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

* Neither the name of Adobe Systems, Network Resonance nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <charconv>
#include <iostream>

#include "runnable_utils.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"

#define USE_TURN

// nICEr includes
// clang-format off
#include "nr_api.h"
#include "transport_addr.h"
#include "nr_crypto.h"
#include "nr_socket.h"
#include "nr_socket_local.h"
#include "nr_socket_buffered_stun.h"
#include "stun_client_ctx.h"
#include "turn_client_ctx.h"
// clang-format on

#include "nricectx.h"

using namespace mozilla;

MOZ_RUNINIT static std::string kDummyTurnServer("192.0.2.1");  // From RFC 5737

class TurnClient : public MtransportTest {
 public:
  TurnClient()
      : real_socket_(nullptr),
        net_socket_(nullptr),
        buffered_socket_(nullptr),
        net_fd_(nullptr),
        turn_ctx_(nullptr),
        allocated_(false),
        received_(0),
        protocol_(IPPROTO_UDP) {}

  ~TurnClient() = default;

  static void SetUpTestCase() {
    NrIceCtx::InitializeGlobals(NrIceCtx::GlobalConfig());
  }

  void SetTcp() { protocol_ = IPPROTO_TCP; }

  void Init_s() {
    int r;
    nr_transport_addr addr;
    r = nr_ip4_port_to_transport_addr(0, 0, protocol_, &addr);
    ASSERT_EQ(0, r);

    r = nr_socket_local_create(nullptr, &addr, &real_socket_);
    ASSERT_EQ(0, r);

    if (protocol_ == IPPROTO_TCP) {
      int r = nr_socket_buffered_stun_create(
          real_socket_, 100000, TURN_TCP_FRAMING, &buffered_socket_);
      ASSERT_EQ(0, r);
      net_socket_ = buffered_socket_;
    } else {
      net_socket_ = real_socket_;
    }

    r = nr_str_port_to_transport_addr(turn_server_.c_str(), 3478, protocol_,
                                      &addr);
    ASSERT_EQ(0, r);

    std::vector<unsigned char> password_vec(turn_password_.begin(),
                                            turn_password_.end());
    Data password;
    INIT_DATA(password, &password_vec[0], password_vec.size());
    r = nr_turn_client_ctx_create("test", net_socket_, turn_user_.c_str(),
                                  &password, &addr, nullptr, &turn_ctx_);
    ASSERT_EQ(0, r);

    r = nr_socket_getfd(net_socket_, &net_fd_);
    ASSERT_EQ(0, r);

    NR_ASYNC_WAIT(net_fd_, NR_ASYNC_WAIT_READ, socket_readable_cb, (void*)this);
  }

  void TearDown_s() {
    nr_turn_client_ctx_destroy(&turn_ctx_);
    if (net_fd_) {
      NR_ASYNC_CANCEL(net_fd_, NR_ASYNC_WAIT_READ);
    }

    nr_socket_destroy(&buffered_socket_);
  }

  void TearDown() {
    test_utils_->SyncDispatchToSTS(WrapRunnable(this, &TurnClient::TearDown_s));
    MtransportTest::TearDown();
  }

  void Allocate_s() {
    Init_s();
    ASSERT_TRUE(turn_ctx_);

    int r = nr_turn_client_allocate(turn_ctx_, allocate_success_cb, this);
    ASSERT_EQ(0, r);
  }

  void Allocate(bool expect_success = true) {
    test_utils_->SyncDispatchToSTS(WrapRunnable(this, &TurnClient::Allocate_s));

    if (expect_success) {
      ASSERT_TRUE_WAIT(allocated_, 5000);
    } else {
      PR_Sleep(10000);
      ASSERT_FALSE(allocated_);
    }
  }

  void Allocated() {
    if (turn_ctx_->state != NR_TURN_CLIENT_STATE_ALLOCATED) {
      std::cerr << "Allocation failed" << std::endl;
      return;
    }
    allocated_ = true;

    int r;
    nr_transport_addr addr;

    r = nr_turn_client_get_relayed_address(turn_ctx_, &addr);
    ASSERT_EQ(0, r);

    relay_addr_ = addr.as_string;

    std::cerr << "Allocation succeeded with addr=" << relay_addr_ << std::endl;
  }

  void Deallocate_s() {
    ASSERT_TRUE(turn_ctx_);

    std::cerr << "De-Allocating..." << std::endl;
    int r = nr_turn_client_deallocate(turn_ctx_);
    ASSERT_EQ(0, r);
  }

  void Deallocate() {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &TurnClient::Deallocate_s));
  }

  void RequestPermission_s(const std::string& target) {
    nr_transport_addr addr;
    int r;

    // Expected pattern here is "IP4:127.0.0.1:3487"
    ASSERT_EQ(0, target.compare(0, 4, "IP4:"));

    size_t offset = target.rfind(':');
    ASSERT_NE(std::string::npos, offset);

    std::string host = target.substr(4, offset - 4);
    std::string port = target.substr(offset + 1);

    // Parse port string using from_chars
    uint16_t port_val = 0;
    auto res =
        std::from_chars(port.data(), port.data() + port.size(), port_val, 10);

    // Ensure conversion succeeded and reached the end of the string
    ASSERT_EQ(res.ec, std::errc{});
    ASSERT_EQ(res.ptr, port.data() + port.size());

    r = nr_str_port_to_transport_addr(host.c_str(), port_val, IPPROTO_UDP,
                                      &addr);
    ASSERT_EQ(0, r);

    r = nr_turn_client_ensure_perm(turn_ctx_, &addr);
    ASSERT_EQ(0, r);
  }

  void RequestPermission(const std::string& target) {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &TurnClient::RequestPermission_s, target));
  }

  void Readable(NR_SOCKET s, int how, void* arg) {
    // Re-arm
    std::cerr << "Socket is readable" << std::endl;
    NR_ASYNC_WAIT(s, how, socket_readable_cb, arg);

    UCHAR buf[8192];
    size_t len_s;
    nr_transport_addr addr;

    int r = nr_socket_recvfrom(net_socket_, buf, sizeof(buf), &len_s, 0, &addr);
    if (r) {
      std::cerr << "Error reading from socket" << std::endl;
      return;
    }

    ASSERT_LT(len_s, (size_t)INT_MAX);
    int len = (int)len_s;

    if (nr_is_stun_response_message(buf, len)) {
      std::cerr << "STUN response" << std::endl;
      r = nr_turn_client_process_response(turn_ctx_, buf, len, &addr);

      if (r && r != R_REJECTED && r != R_RETRY) {
        std::cerr << "Error processing STUN: " << r << std::endl;
      }
    } else if (nr_is_stun_indication_message(buf, len)) {
      std::cerr << "STUN indication" << std::endl;

      /* Process the indication */
      unsigned char data[NR_STUN_MAX_MESSAGE_SIZE];
      size_t datal;
      nr_transport_addr remote_addr;

      r = nr_turn_client_parse_data_indication(
          turn_ctx_, &addr, buf, len, data, &datal, sizeof(data), &remote_addr);
      ASSERT_EQ(0, r);
      std::cerr << "Received " << datal << " bytes from "
                << remote_addr.as_string << std::endl;

      received_ += datal;

      for (size_t i = 0; i < datal; i++) {
        ASSERT_EQ(i & 0xff, data[i]);
      }
    } else {
      if (nr_is_stun_message(buf, len)) {
        std::cerr << "STUN message of unexpected type" << std::endl;
      } else {
        std::cerr << "Not a STUN message" << std::endl;
      }
      return;
    }
  }

  void SendTo_s(const std::string& target, int expect_return) {
    nr_transport_addr addr;
    int r;

    // Expected pattern here is "IP4:127.0.0.1:3487"
    ASSERT_EQ(0, target.compare(0, 4, "IP4:"));

    size_t offset = target.rfind(':');
    ASSERT_NE(std::string::npos, offset);

    std::string host = target.substr(4, offset - 4);
    std::string port = target.substr(offset + 1);

    // Parse port string using from_chars
    uint16_t port_val = 0;
    auto res =
        std::from_chars(port.data(), port.data() + port.size(), port_val, 10);

    // Ensure conversion succeeded and reached the end of the string
    ASSERT_EQ(res.ec, std::errc{});
    ASSERT_EQ(res.ptr, port.data() + port.size());

    r = nr_str_port_to_transport_addr(host.c_str(), port_val, IPPROTO_UDP,
                                      &addr);
    ASSERT_EQ(0, r);

    unsigned char test[100];
    for (size_t i = 0; i < sizeof(test); i++) {
      test[i] = i & 0xff;
    }

    std::cerr << "Sending test message to " << target << " ..." << std::endl;

    r = nr_turn_client_send_indication(turn_ctx_, test, sizeof(test), 0, &addr);
    if (expect_return >= 0) {
      ASSERT_EQ(expect_return, r);
    }
  }

  void SendTo(const std::string& target, int expect_return = 0) {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &TurnClient::SendTo_s, target, expect_return));
  }

  int received() const { return received_; }

  static void socket_readable_cb(NR_SOCKET s, int how, void* arg) {
    static_cast<TurnClient*>(arg)->Readable(s, how, arg);
  }

  static void allocate_success_cb(NR_SOCKET s, int how, void* arg) {
    static_cast<TurnClient*>(arg)->Allocated();
  }

 protected:
  std::string turn_server_;
  nr_socket* real_socket_;
  nr_socket* net_socket_;
  nr_socket* buffered_socket_;
  NR_SOCKET net_fd_;
  nr_turn_client_ctx* turn_ctx_;
  std::string relay_addr_;
  bool allocated_;
  int received_;
  int protocol_;
};

TEST_F(TurnClient, Allocate) {
  if (WarnIfTurnNotConfigured()) return;

  Allocate();
}

TEST_F(TurnClient, AllocateTcp) {
  if (WarnIfTurnNotConfigured()) return;

  SetTcp();
  Allocate();
}

TEST_F(TurnClient, AllocateAndHold) {
  if (WarnIfTurnNotConfigured()) return;

  Allocate();
  PR_Sleep(20000);
  ASSERT_TRUE(turn_ctx_->state == NR_TURN_CLIENT_STATE_ALLOCATED);
}

TEST_F(TurnClient, SendToSelf) {
  if (WarnIfTurnNotConfigured()) return;

  Allocate();
  SendTo(relay_addr_);
  ASSERT_TRUE_WAIT(received() == 100, 5000);
  SendTo(relay_addr_);
  ASSERT_TRUE_WAIT(received() == 200, 1000);
}

TEST_F(TurnClient, SendToSelfTcp) {
  if (WarnIfTurnNotConfigured()) return;

  SetTcp();
  Allocate();
  SendTo(relay_addr_);
  ASSERT_TRUE_WAIT(received() == 100, 5000);
  SendTo(relay_addr_);
  ASSERT_TRUE_WAIT(received() == 200, 1000);
}

TEST_F(TurnClient, PermissionDenied) {
  if (WarnIfTurnNotConfigured()) return;

  Allocate();
  RequestPermission(relay_addr_);
  PR_Sleep(1000);

  /* Fake a 403 response */
  nr_turn_permission* perm;
  perm = STAILQ_FIRST(&turn_ctx_->permissions);
  ASSERT_TRUE(perm);
  while (perm) {
    perm->stun->last_error_code = 403;
    std::cerr << "Set 403's on permission" << std::endl;
    perm = STAILQ_NEXT(perm, entry);
  }

  SendTo(relay_addr_, R_NOT_PERMITTED);
  ASSERT_TRUE(received() == 0);

  // TODO: We should check if we can still send to a second destination, but
  //      we would need a second TURN client as one client can only handle one
  //      allocation (maybe as part of bug 1128128 ?).
}

TEST_F(TurnClient, DeallocateReceiveFailure) {
  if (WarnIfTurnNotConfigured()) return;

  Allocate();
  SendTo(relay_addr_);
  ASSERT_TRUE_WAIT(received() == 100, 5000);
  Deallocate();
  turn_ctx_->state = NR_TURN_CLIENT_STATE_ALLOCATED;
  SendTo(relay_addr_);
  PR_Sleep(1000);
  ASSERT_TRUE(received() == 100);
}

TEST_F(TurnClient, DeallocateReceiveFailureTcp) {
  if (WarnIfTurnNotConfigured()) return;

  SetTcp();
  Allocate();
  SendTo(relay_addr_);
  ASSERT_TRUE_WAIT(received() == 100, 5000);
  Deallocate();
  turn_ctx_->state = NR_TURN_CLIENT_STATE_ALLOCATED;
  /* Either the connection got closed by the TURN server already, then the send
   * is going to fail, which we simply ignore. Or the connection is still alive
   * and we cand send the data, but it should not get forwarded to us. In either
   * case we should not receive more data. */
  SendTo(relay_addr_, -1);
  PR_Sleep(1000);
  ASSERT_TRUE(received() == 100);
}

TEST_F(TurnClient, AllocateDummyServer) {
  if (WarnIfTurnNotConfigured()) return;

  turn_server_ = kDummyTurnServer;
  Allocate(false);
}
