/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "prio.h"
#include "prnetdb.h"
#include "ssl.h"
#include "sslexp.h"

#include "gtest_utils.h"
#include "nss_scoped_ptrs.h"

namespace nss_test {

class MiscTest : public ::testing::Test {};

TEST_F(MiscTest, NonExistentExperimentalAPI) {
  EXPECT_EQ(nullptr, SSL_GetExperimentalAPI("blah"));
  EXPECT_EQ(SSL_ERROR_UNSUPPORTED_EXPERIMENTAL_API, PORT_GetError());
}

class AcceptPollTest : public ::testing::Test {
 protected:
  // Accepts a loopback TCP connection on an SSL listen socket, without calling
  // SSL_ResetHandshake on the accepted socket.
  void Accept(PRBool handshakeAsClient) {
    listener_.reset(SSL_ImportFD(nullptr, PR_OpenTCPSocket(PR_AF_INET)));
    ASSERT_TRUE(listener_);
    ASSERT_EQ(SECSuccess,
              SSL_OptionSet(listener_.get(), SSL_HANDSHAKE_AS_CLIENT,
                            handshakeAsClient));

    PRNetAddr addr;
    ASSERT_EQ(PR_SUCCESS, PR_InitializeNetAddr(PR_IpAddrLoopback, 0, &addr));
    ASSERT_EQ(PR_SUCCESS, PR_Bind(listener_.get(), &addr));
    ASSERT_EQ(PR_SUCCESS, PR_Listen(listener_.get(), 1));
    ASSERT_EQ(PR_SUCCESS, PR_GetSockName(listener_.get(), &addr));

    peer_.reset(PR_OpenTCPSocket(PR_AF_INET));
    ASSERT_TRUE(peer_);
    ASSERT_EQ(PR_SUCCESS,
              PR_Connect(peer_.get(), &addr, PR_INTERVAL_NO_TIMEOUT));

    accepted_.reset(
        PR_Accept(listener_.get(), nullptr, PR_INTERVAL_NO_TIMEOUT));
    ASSERT_TRUE(accepted_);
  }

  PRInt16 Poll(PRInt16 how) {
    PRInt16 out = 0;
    return accepted_->methods->poll(accepted_.get(), how, &out);
  }

  ScopedPRFileDesc listener_;
  ScopedPRFileDesc peer_;
  ScopedPRFileDesc accepted_;
};

TEST_F(AcceptPollTest, ServerPollsForRead) {
  ASSERT_NO_FATAL_FAILURE(Accept(PR_FALSE));
  EXPECT_EQ(PR_POLL_READ, Poll(PR_POLL_WRITE));
}

TEST_F(AcceptPollTest, ClientPollsForWrite) {
  ASSERT_NO_FATAL_FAILURE(Accept(PR_TRUE));
  EXPECT_EQ(PR_POLL_WRITE, Poll(PR_POLL_READ));
}

}  // namespace nss_test
