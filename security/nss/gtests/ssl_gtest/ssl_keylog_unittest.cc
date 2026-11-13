/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "gtest_utils.h"
#include "tls_connect.h"

namespace nss_test {

static const std::string kKeylogFilePath = "keylog.txt";
static const std::string kKeylogBlankEnv = "SSLKEYLOGFILE=";
static const std::string kKeylogSetEnv = kKeylogBlankEnv + kKeylogFilePath;

extern "C" {
extern FILE* ssl_keylog_iob;
}

class KeyLogFileTestBase : public TlsConnectGeneric {
 private:
  std::string env_to_set_;

 public:
  virtual void CheckKeyLog() = 0;

  KeyLogFileTestBase(std::string env) : env_to_set_(env) {}

  void SetUp() override {
    TlsConnectGeneric::SetUp();
    // Remove previous results (if any).
    (void)remove(kKeylogFilePath.c_str());
    PR_SetEnv(env_to_set_.c_str());
  }

  void ConnectAndCheck() {
    // This is a child process, ensure that error messages immediately
    // propagate or else it will not be visible.
    ::testing::GTEST_FLAG(throw_on_failure) = true;

    if (version_ == SSL_LIBRARY_VERSION_TLS_1_3) {
      SetupForZeroRtt();
      client_->Set0RttEnabled(true);
      server_->Set0RttEnabled(true);
      SetupEch(client_, server_);
      ExpectResumption(RESUME_TICKET);
      ZeroRttSendReceive(true, true);
      Handshake();
      ExpectEarlyDataAccepted(true);
      CheckConnected();
      SendReceive();
    } else {
      Connect();
    }
    CheckKeyLog();
    _exit(0);
  }
};

class KeyLogFileTest : public KeyLogFileTestBase {
 public:
  KeyLogFileTest() : KeyLogFileTestBase(kKeylogSetEnv) {}

  void CheckKeyLog() override {
    std::ifstream f(kKeylogFilePath);
    std::map<std::string, size_t> labels;
    std::set<std::string> client_randoms;
    for (std::string line; std::getline(f, line);) {
      if (line[0] == '#') {
        continue;
      }

      std::istringstream iss(line);
      std::string label, client_random, secret;
      iss >> label >> client_random >> secret;

      ASSERT_EQ(64U, client_random.size());
      client_randoms.insert(client_random);
      labels[label]++;
    }

    if (version_ < SSL_LIBRARY_VERSION_TLS_1_3) {
      ASSERT_EQ(1U, client_randoms.size());
    } else {
      /* two handshakes for 0-RTT and ClientRandom from Outer ClientHello */
      ASSERT_EQ(3U, client_randoms.size());
    }

    // Every entry occurs twice (one log from server, one from client).
    if (version_ < SSL_LIBRARY_VERSION_TLS_1_3) {
      ASSERT_EQ(2U, labels["CLIENT_RANDOM"]);
    } else {
      ASSERT_EQ(2U, labels["CLIENT_EARLY_TRAFFIC_SECRET"]);
      ASSERT_EQ(2U, labels["EARLY_EXPORTER_SECRET"]);
      ASSERT_EQ(2U, labels["ECH_SECRET"]);
      ASSERT_EQ(2U, labels["ECH_CONFIG"]);
      ASSERT_EQ(4U, labels["CLIENT_HANDSHAKE_TRAFFIC_SECRET"]);
      ASSERT_EQ(4U, labels["SERVER_HANDSHAKE_TRAFFIC_SECRET"]);
      ASSERT_EQ(4U, labels["CLIENT_TRAFFIC_SECRET_0"]);
      ASSERT_EQ(4U, labels["SERVER_TRAFFIC_SECRET_0"]);
      ASSERT_EQ(4U, labels["EXPORTER_SECRET"]);
    }
  }
};

// Tests are run in a separate process to ensure that NSS is not initialized yet
// and can process the SSLKEYLOGFILE environment variable.

TEST_P(KeyLogFileTest, KeyLogFile) {
  if (PK11_IsFIPS()) {
    GTEST_SKIP() << "FIPS tokens don't allow secrets to be logged";
  }
#if GTEST_HAS_DEATH_TEST
  testing::GTEST_FLAG(death_test_style) = "threadsafe";

  ASSERT_EXIT(ConnectAndCheck(), ::testing::ExitedWithCode(0), "");
#else
  GTEST_SKIP() << "needs death tests, which gtest lacks on this platform";
#endif
}

INSTANTIATE_TEST_SUITE_P(
    KeyLogFileDTLS12, KeyLogFileTest,
    ::testing::Combine(TlsConnectTestBase::kTlsVariantsDatagram,
                       TlsConnectTestBase::kTlsV11V12));
INSTANTIATE_TEST_SUITE_P(
    KeyLogFileTLS12, KeyLogFileTest,
    ::testing::Combine(TlsConnectTestBase::kTlsVariantsStream,
                       TlsConnectTestBase::kTlsV10ToV12));
#ifndef NSS_DISABLE_TLS_1_3
INSTANTIATE_TEST_SUITE_P(
    KeyLogFileTLS13, KeyLogFileTest,
    ::testing::Combine(TlsConnectTestBase::kTlsVariantsStream,
                       TlsConnectTestBase::kTlsV13));
#endif

class KeyLogFileUnsetTest : public KeyLogFileTestBase {
 public:
  KeyLogFileUnsetTest() : KeyLogFileTestBase(kKeylogBlankEnv) {}

  void CheckKeyLog() override {
    std::ifstream f(kKeylogFilePath);
    EXPECT_FALSE(f.good());

    EXPECT_EQ(nullptr, ssl_keylog_iob);
  }
};

TEST_P(KeyLogFileUnsetTest, KeyLogFile) {
#if GTEST_HAS_DEATH_TEST
  testing::GTEST_FLAG(death_test_style) = "threadsafe";

  ASSERT_EXIT(ConnectAndCheck(), ::testing::ExitedWithCode(0), "");
#else
  GTEST_SKIP() << "needs death tests, which gtest lacks on this platform";
#endif
}

INSTANTIATE_TEST_SUITE_P(
    KeyLogFileDTLS12, KeyLogFileUnsetTest,
    ::testing::Combine(TlsConnectTestBase::kTlsVariantsDatagram,
                       TlsConnectTestBase::kTlsV11V12));
INSTANTIATE_TEST_SUITE_P(
    KeyLogFileTLS12, KeyLogFileUnsetTest,
    ::testing::Combine(TlsConnectTestBase::kTlsVariantsStream,
                       TlsConnectTestBase::kTlsV10ToV12));
#ifndef NSS_DISABLE_TLS_1_3
INSTANTIATE_TEST_SUITE_P(
    KeyLogFileTLS13, KeyLogFileUnsetTest,
    ::testing::Combine(TlsConnectTestBase::kTlsVariantsStream,
                       TlsConnectTestBase::kTlsV13));
#endif

}  // namespace nss_test
