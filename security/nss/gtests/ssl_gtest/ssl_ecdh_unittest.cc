/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <functional>
#include <memory>
#include "secerr.h"
#include "ssl.h"
#include "sslerr.h"
#include "sslproto.h"

extern "C" {
// This is not something that should make you happy.
#include "libssl_internals.h"
}

#include "gtest_utils.h"
#include "nss_scoped_ptrs.h"
#include "tls_connect.h"
#include "tls_filter.h"
#include "tls_parser.h"

namespace nss_test {

TEST_P(TlsConnectGenericPre13, ConnectEcdh) {
  SetExpectedVersion(std::get<1>(GetParam()));
  Reset(TlsAgent::kServerEcdhEcdsa);
  DisableAllCiphers();
  EnableSomeEcdhCiphers();

  Connect();
  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp256r1, ssl_auth_ecdh_ecdsa,
            ssl_sig_none);
}

TEST_P(TlsConnectGenericPre13, ConnectEcdhWithoutDisablingSuites) {
  SetExpectedVersion(std::get<1>(GetParam()));
  Reset(TlsAgent::kServerEcdhEcdsa);
  EnableSomeEcdhCiphers();

  Connect();
  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp256r1, ssl_auth_ecdh_ecdsa,
            ssl_sig_none);
}

TEST_P(TlsConnectGeneric, ConnectEcdhe) {
  Connect();
  CheckKeys();
}

// If we pick a 256-bit cipher suite and use a P-384 certificate, the server
// should choose P-384 for key exchange too.  Only valid for TLS == 1.2 because
// we don't have 256-bit ciphers before then and 1.3 doesn't try to couple
// DHE size to symmetric size.
TEST_P(TlsConnectTls12, ConnectEcdheP384) {
  Reset(TlsAgent::kServerEcdsa384);
  ConnectWithCipherSuite(TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256);
  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp384r1, ssl_auth_ecdsa,
            ssl_sig_ecdsa_secp256r1_sha256);
}

TEST_P(TlsConnectGeneric, ConnectEcdheP384Client) {
  EnsureTlsSetup();
  const std::vector<SSLNamedGroup> groups = {ssl_grp_ec_secp384r1,
                                             ssl_grp_ffdhe_2048};
  client_->ConfigNamedGroups(groups);
  server_->ConfigNamedGroups(groups);
  Connect();
  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp384r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);
}

// The bug https://bugzilla.mozilla.org/show_bug.cgi?id=1818487 updates the
// generation of transcript for DTLS1.3
// The following three tests are used to check the correctness of the
// transcript.
TEST_P(TlsConnectGeneric,
       ClientOfferTls11_Tls13ServerNegotiateEachVersionOneByOne_HRR) {
  EnsureTlsSetup();
  auto hrr_capture = MakeTlsFilter<TlsHandshakeRecorder>(
      server_, kTlsHandshakeHelloRetryRequest);
  const std::vector<SSLNamedGroup> groups = {ssl_grp_ec_secp384r1,
                                             ssl_grp_ffdhe_2048};
  server_->ConfigNamedGroups(groups);
  // DTLS does not support 1.0
  if (variant_ == ssl_variant_datagram) {
    client_->SetVersionRange(SSL_LIBRARY_VERSION_TLS_1_1,
                             SSL_LIBRARY_VERSION_TLS_1_3);
  } else {
    client_->SetVersionRange(SSL_LIBRARY_VERSION_TLS_1_0,
                             SSL_LIBRARY_VERSION_TLS_1_3);
  }
  server_->SetVersionRange(version_, version_);
  Connect();
  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp384r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);

  EXPECT_EQ(version_ == SSL_LIBRARY_VERSION_TLS_1_3,
            hrr_capture->buffer().len() != 0);
}

// This causes a HelloRetryRequest in TLS 1.3.  Earlier versions don't care.
TEST_P(TlsConnectGeneric, ConnectEcdheP384Server) {
  EnsureTlsSetup();
  auto hrr_capture = MakeTlsFilter<TlsHandshakeRecorder>(
      server_, kTlsHandshakeHelloRetryRequest);
  const std::vector<SSLNamedGroup> groups = {ssl_grp_ec_secp384r1};
  server_->ConfigNamedGroups(groups);
  Connect();
  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp384r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);
  EXPECT_EQ(version_ == SSL_LIBRARY_VERSION_TLS_1_3,
            hrr_capture->buffer().len() != 0);
}

// This enables only P-256 on the client and disables it on the server.
// This test will fail when we add other groups that identify as ECDHE.
TEST_P(TlsConnectGeneric, ConnectEcdheGroupMismatch) {
  EnsureTlsSetup();
  const std::vector<SSLNamedGroup> client_groups = {ssl_grp_ec_secp256r1,
                                                    ssl_grp_ffdhe_2048};
  const std::vector<SSLNamedGroup> server_groups = {ssl_grp_ffdhe_2048};
  client_->ConfigNamedGroups(client_groups);
  server_->ConfigNamedGroups(server_groups);

  Connect();
  CheckKeys(ssl_kea_dh, ssl_auth_rsa_sign);
}

TEST_P(TlsKeyExchangeTest, P384Priority) {
  // P256, P384 and P521 are enabled. Both prefer P384.
  const std::vector<SSLNamedGroup> groups = {
      ssl_grp_ec_secp384r1, ssl_grp_ec_secp256r1, ssl_grp_ec_secp521r1};
  EnsureKeyShareSetup();
  ConfigNamedGroups(groups);
  client_->DisableAllCiphers();
  client_->EnableCiphersByKeyExchange(ssl_kea_ecdh);
  Connect();

  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp384r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);

  std::vector<SSLNamedGroup> shares = {ssl_grp_ec_secp384r1};
  CheckKEXDetails(groups, shares);
}

TEST_P(TlsKeyExchangeTest, DuplicateGroupConfig) {
  const std::vector<SSLNamedGroup> groups = {
      ssl_grp_ec_secp384r1, ssl_grp_ec_secp384r1, ssl_grp_ec_secp384r1,
      ssl_grp_ec_secp256r1, ssl_grp_ec_secp256r1};
  EnsureKeyShareSetup();
  ConfigNamedGroups(groups);
  client_->DisableAllCiphers();
  client_->EnableCiphersByKeyExchange(ssl_kea_ecdh);
  Connect();

  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp384r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);

  std::vector<SSLNamedGroup> shares = {ssl_grp_ec_secp384r1};
  std::vector<SSLNamedGroup> expectedGroups = {ssl_grp_ec_secp384r1,
                                               ssl_grp_ec_secp256r1};
  CheckKEXDetails(expectedGroups, shares);
}

TEST_P(TlsKeyExchangeTest, P384PriorityDHEnabled) {
  // P256, P384,  P521, and FFDHE2048 are enabled. Both prefer P384.
  const std::vector<SSLNamedGroup> groups = {
      ssl_grp_ec_secp384r1, ssl_grp_ffdhe_2048, ssl_grp_ec_secp256r1,
      ssl_grp_ec_secp521r1};
  EnsureKeyShareSetup();
  ConfigNamedGroups(groups);
  Connect();

  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp384r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);

  if (version_ >= SSL_LIBRARY_VERSION_TLS_1_3) {
    std::vector<SSLNamedGroup> shares = {ssl_grp_ec_secp384r1};
    CheckKEXDetails(groups, shares);
  } else {
    std::vector<SSLNamedGroup> oldtlsgroups = {
        ssl_grp_ec_secp384r1, ssl_grp_ec_secp256r1, ssl_grp_ec_secp521r1};
    CheckKEXDetails(oldtlsgroups, std::vector<SSLNamedGroup>());
  }
}

TEST_P(TlsConnectGenericPre13, P384PriorityOnServer) {
  EnsureTlsSetup();
  client_->DisableAllCiphers();
  client_->EnableCiphersByKeyExchange(ssl_kea_ecdh);

  // The server prefers P384. It has to win.
  const std::vector<SSLNamedGroup> server_groups = {
      ssl_grp_ec_secp384r1, ssl_grp_ec_secp256r1, ssl_grp_ec_secp521r1};
  server_->ConfigNamedGroups(server_groups);

  Connect();

  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp384r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);
}

TEST_P(TlsConnectGenericPre13, P384PriorityFromModelSocket) {
  EnsureModelSockets();

  /* Both prefer P384, set on the model socket. */
  const std::vector<SSLNamedGroup> groups = {
      ssl_grp_ec_secp384r1, ssl_grp_ec_secp256r1, ssl_grp_ec_secp521r1,
      ssl_grp_ffdhe_2048};
  client_model_->ConfigNamedGroups(groups);
  server_model_->ConfigNamedGroups(groups);

  Connect();

  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp384r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);
}

class TlsKeyExchangeGroupCapture : public TlsHandshakeFilter {
 public:
  TlsKeyExchangeGroupCapture(const std::shared_ptr<TlsAgent>& a)
      : TlsHandshakeFilter(a, {kTlsHandshakeServerKeyExchange}),
        group_(ssl_grp_none) {}

  SSLNamedGroup group() const { return group_; }

 protected:
  virtual PacketFilter::Action FilterHandshake(const HandshakeHeader& header,
                                               const DataBuffer& input,
                                               DataBuffer* output) {
    uint32_t value = 0;
    EXPECT_TRUE(input.Read(0, 1, &value));
    EXPECT_EQ(3U, value) << "curve type has to be 3";

    EXPECT_TRUE(input.Read(1, 2, &value));
    group_ = static_cast<SSLNamedGroup>(value);

    return KEEP;
  }

 private:
  SSLNamedGroup group_;
};

// If we strip the client's supported groups extension, the server should assume
// P-256 is supported by the client (<= 1.2 only).
TEST_P(TlsConnectGenericPre13, DropSupportedGroupExtensionP256) {
  EnsureTlsSetup();
  MakeTlsFilter<TlsExtensionDropper>(client_, ssl_supported_groups_xtn);
  auto group_capture = MakeTlsFilter<TlsKeyExchangeGroupCapture>(server_);

  ConnectExpectAlert(server_, kTlsAlertDecryptError);
  client_->CheckErrorCode(SSL_ERROR_DECRYPT_ERROR_ALERT);
  server_->CheckErrorCode(SSL_ERROR_BAD_HANDSHAKE_HASH_VALUE);

  EXPECT_EQ(ssl_grp_ec_secp256r1, group_capture->group());
}

// Supported groups is mandatory in TLS 1.3.
TEST_P(TlsConnectTls13, DropSupportedGroupExtension) {
  EnsureTlsSetup();
  MakeTlsFilter<TlsExtensionDropper>(client_, ssl_supported_groups_xtn);
  ConnectExpectAlert(server_, kTlsAlertMissingExtension);
  client_->CheckErrorCode(SSL_ERROR_MISSING_EXTENSION_ALERT);
  server_->CheckErrorCode(SSL_ERROR_MISSING_SUPPORTED_GROUPS_EXTENSION);
}

// If we only have a lame group, we fall back to static RSA.
TEST_P(TlsConnectGenericPre13, UseLameGroup) {
  const std::vector<SSLNamedGroup> groups = {ssl_grp_ec_secp192r1};
  client_->ConfigNamedGroups(groups);
  server_->ConfigNamedGroups(groups);
  Connect();
  CheckKeys(ssl_kea_rsa, ssl_grp_none, ssl_auth_rsa_decrypt, ssl_sig_none);
}

// In TLS 1.3, we can't generate the ClientHello.
TEST_P(TlsConnectTls13, UseLameGroup) {
  const std::vector<SSLNamedGroup> groups = {ssl_grp_ec_sect283k1};
  client_->ConfigNamedGroups(groups);
  server_->ConfigNamedGroups(groups);
  client_->StartConnect();
  client_->Handshake();
  client_->CheckErrorCode(SSL_ERROR_NO_CIPHERS_SUPPORTED);
}

TEST_P(TlsConnectStreamPre13, ConfiguredGroupsRenegotiate) {
  EnsureTlsSetup();
  client_->DisableAllCiphers();
  client_->EnableCiphersByKeyExchange(ssl_kea_ecdh);

  const std::vector<SSLNamedGroup> client_groups = {ssl_grp_ec_secp256r1};
  const std::vector<SSLNamedGroup> server_groups = {ssl_grp_ec_secp256r1,
                                                    ssl_grp_ec_secp256r1};
  client_->ConfigNamedGroups(client_groups);
  server_->ConfigNamedGroups(server_groups);

  Connect();

  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp256r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);
  CheckConnected();

  // The renegotiation has to use the same preferences as the original session.
  server_->PrepareForRenegotiate();
  client_->StartRenegotiate();
  Handshake();
  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp256r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);
}

TEST_P(TlsKeyExchangeTest, Curve25519) {
  Reset(TlsAgent::kServerEcdsa256);
  const std::vector<SSLNamedGroup> groups = {
      ssl_grp_ec_curve25519, ssl_grp_ec_secp256r1, ssl_grp_ec_secp521r1};
  EnsureKeyShareSetup();
  ConfigNamedGroups(groups);
  Connect();

  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_curve25519, ssl_auth_ecdsa,
            ssl_sig_ecdsa_secp256r1_sha256);
  const std::vector<SSLNamedGroup> shares = {ssl_grp_ec_curve25519};
  CheckKEXDetails(groups, shares);
}

TEST_P(TlsConnectGenericPre13, GroupPreferenceServerPriority) {
  EnsureTlsSetup();
  client_->DisableAllCiphers();
  client_->EnableCiphersByKeyExchange(ssl_kea_ecdh);

  // The client prefers P256 while the server prefers 25519.
  // The server's preference has to win.
  const std::vector<SSLNamedGroup> client_groups = {ssl_grp_ec_secp256r1,
                                                    ssl_grp_ec_curve25519};
  const std::vector<SSLNamedGroup> server_groups = {ssl_grp_ec_curve25519,
                                                    ssl_grp_ec_secp256r1};
  client_->ConfigNamedGroups(client_groups);
  server_->ConfigNamedGroups(server_groups);

  Connect();

  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_curve25519, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);
}

#ifndef NSS_DISABLE_TLS_1_3
TEST_P(TlsKeyExchangeTest13, Curve25519P256EqualPriorityClient13) {
  EnsureKeyShareSetup();

  // The client sends a P256 key share while the server prefers 25519.
  // We have to accept P256 without retry.
  const std::vector<SSLNamedGroup> client_groups = {ssl_grp_ec_secp256r1,
                                                    ssl_grp_ec_curve25519};
  const std::vector<SSLNamedGroup> server_groups = {ssl_grp_ec_curve25519,
                                                    ssl_grp_ec_secp256r1};
  client_->ConfigNamedGroups(client_groups);
  server_->ConfigNamedGroups(server_groups);

  Connect();

  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp256r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);
  const std::vector<SSLNamedGroup> shares = {ssl_grp_ec_secp256r1};
  CheckKEXDetails(client_groups, shares);
}

TEST_P(TlsKeyExchangeTest13, Curve25519P256EqualPriorityServer13) {
  EnsureKeyShareSetup();

  // The client sends a 25519 key share while the server prefers P256.
  // We have to accept 25519 without retry.
  const std::vector<SSLNamedGroup> client_groups = {ssl_grp_ec_curve25519,
                                                    ssl_grp_ec_secp256r1};
  const std::vector<SSLNamedGroup> server_groups = {ssl_grp_ec_secp256r1,
                                                    ssl_grp_ec_curve25519};
  client_->ConfigNamedGroups(client_groups);
  server_->ConfigNamedGroups(server_groups);

  Connect();

  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_curve25519, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);
  const std::vector<SSLNamedGroup> shares = {ssl_grp_ec_curve25519};
  CheckKEXDetails(client_groups, shares);
}

TEST_P(TlsKeyExchangeTest13, EqualPriorityTestRetryECServer13) {
  EnsureKeyShareSetup();

  // The client sends a 25519 key share while the server prefers P256.
  // The server prefers P-384 over x25519, so it must not consider P-256 and
  // x25519 to be equivalent. It will therefore request a P-256 share
  // with a HelloRetryRequest.
  const std::vector<SSLNamedGroup> client_groups = {
      ssl_grp_ec_curve25519, ssl_grp_ec_secp256r1, ssl_grp_ec_secp384r1};
  const std::vector<SSLNamedGroup> server_groups = {
      ssl_grp_ec_secp256r1, ssl_grp_ec_secp384r1, ssl_grp_ec_curve25519};
  client_->ConfigNamedGroups(client_groups);
  server_->ConfigNamedGroups(server_groups);

  Connect();

  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp256r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);
  const std::vector<SSLNamedGroup> shares = {ssl_grp_ec_curve25519};
  CheckKEXDetails(client_groups, shares, ssl_grp_ec_secp256r1);
}

TEST_P(TlsKeyExchangeTest13, NotEqualPriorityWithIntermediateGroup13) {
  EnsureKeyShareSetup();

  // The client sends a 25519 key share while the server prefers P256.
  // The server prefers ffdhe_2048 over x25519, so it must not consider the
  // P-256 and x25519 to be equivalent. It will therefore request a P-256 share
  // with a HelloRetryRequest.
  const std::vector<SSLNamedGroup> client_groups = {
      ssl_grp_ec_curve25519, ssl_grp_ec_secp256r1, ssl_grp_ffdhe_2048};
  const std::vector<SSLNamedGroup> server_groups = {
      ssl_grp_ec_secp256r1, ssl_grp_ffdhe_2048, ssl_grp_ec_curve25519};
  client_->ConfigNamedGroups(client_groups);
  server_->ConfigNamedGroups(server_groups);

  Connect();

  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp256r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);
  const std::vector<SSLNamedGroup> shares = {ssl_grp_ec_curve25519};
  CheckKEXDetails(client_groups, shares, ssl_grp_ec_secp256r1);
}

TEST_P(TlsKeyExchangeTest13,
       NotEqualPriorityWithUnsupportedFFIntermediateGroup13) {
  EnsureKeyShareSetup();

  // As in the previous test, the server prefers ffdhe_2048. Thus, even though
  // the client doesn't support this group, the server must not regard x25519 as
  // equivalent to P-256.
  const std::vector<SSLNamedGroup> client_groups = {ssl_grp_ec_curve25519,
                                                    ssl_grp_ec_secp256r1};
  const std::vector<SSLNamedGroup> server_groups = {
      ssl_grp_ec_secp256r1, ssl_grp_ffdhe_2048, ssl_grp_ec_curve25519};
  client_->ConfigNamedGroups(client_groups);
  server_->ConfigNamedGroups(server_groups);

  Connect();

  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp256r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);
  const std::vector<SSLNamedGroup> shares = {ssl_grp_ec_curve25519};
  CheckKEXDetails(client_groups, shares, ssl_grp_ec_secp256r1);
}

TEST_P(TlsKeyExchangeTest13,
       NotEqualPriorityWithUnsupportedECIntermediateGroup13) {
  EnsureKeyShareSetup();

  // As in the previous test, the server prefers P-384. Thus, even though
  // the client doesn't support this group, the server must not regard x25519 as
  // equivalent to P-256. The server sends a HelloRetryRequest.
  const std::vector<SSLNamedGroup> client_groups = {ssl_grp_ec_curve25519,
                                                    ssl_grp_ec_secp256r1};
  const std::vector<SSLNamedGroup> server_groups = {
      ssl_grp_ec_secp256r1, ssl_grp_ec_secp384r1, ssl_grp_ec_curve25519};
  client_->ConfigNamedGroups(client_groups);
  server_->ConfigNamedGroups(server_groups);

  Connect();

  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp256r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);
  const std::vector<SSLNamedGroup> shares = {ssl_grp_ec_curve25519};
  CheckKEXDetails(client_groups, shares, ssl_grp_ec_secp256r1);
}

TEST_P(TlsKeyExchangeTest13, EqualPriority13) {
  EnsureKeyShareSetup();

  // The client sends a 25519 key share while the server prefers P256.
  // We have to accept 25519 without retry because it's considered equivalent to
  // P256 by the server.
  const std::vector<SSLNamedGroup> client_groups = {
      ssl_grp_ec_curve25519, ssl_grp_ffdhe_2048, ssl_grp_ec_secp256r1};
  const std::vector<SSLNamedGroup> server_groups = {ssl_grp_ec_secp256r1,
                                                    ssl_grp_ec_curve25519};
  client_->ConfigNamedGroups(client_groups);
  server_->ConfigNamedGroups(server_groups);

  Connect();

  CheckKeys(ssl_kea_ecdh, ssl_auth_rsa_sign);
  const std::vector<SSLNamedGroup> shares = {ssl_grp_ec_curve25519};
  CheckKEXDetails(client_groups, shares);
}
#endif

TEST_P(TlsConnectGeneric, P256ClientAndCurve25519Server) {
  EnsureTlsSetup();
  client_->DisableAllCiphers();
  client_->EnableCiphersByKeyExchange(ssl_kea_ecdh);

  // The client sends a P256 key share while the server prefers 25519.
  const std::vector<SSLNamedGroup> client_groups = {ssl_grp_ec_secp256r1};
  const std::vector<SSLNamedGroup> server_groups = {ssl_grp_ec_curve25519};

  client_->ConfigNamedGroups(client_groups);
  server_->ConfigNamedGroups(server_groups);

  ConnectExpectAlert(server_, kTlsAlertHandshakeFailure);
  client_->CheckErrorCode(SSL_ERROR_NO_CYPHER_OVERLAP);
  server_->CheckErrorCode(SSL_ERROR_NO_CYPHER_OVERLAP);
}

TEST_P(TlsKeyExchangeTest13, MultipleClientShares) {
  EnsureKeyShareSetup();

  // The client sends 25519 and P256 key shares. The server prefers P256,
  // which must be chosen here.
  const std::vector<SSLNamedGroup> client_groups = {ssl_grp_ec_curve25519,
                                                    ssl_grp_ec_secp256r1};
  const std::vector<SSLNamedGroup> server_groups = {ssl_grp_ec_secp256r1,
                                                    ssl_grp_ec_curve25519};
  client_->ConfigNamedGroups(client_groups);
  server_->ConfigNamedGroups(server_groups);

  // Generate a key share on the client for both curves.
  EXPECT_EQ(SECSuccess, SSL_SendAdditionalKeyShares(client_->ssl_fd(), 1));

  Connect();

  // The server would accept 25519 but its preferred group (P256) has to win.
  CheckKeys(ssl_kea_ecdh, ssl_grp_ec_secp256r1, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);
  const std::vector<SSLNamedGroup> shares = {ssl_grp_ec_curve25519,
                                             ssl_grp_ec_secp256r1};
  CheckKEXDetails(client_groups, shares);
}

// Resetting a client after it has sent a ClientHello must not carry the old
// key shares into the next ClientHello.
TEST_P(TlsKeyExchangeTest13, ResetHandshakeAfterClientHello) {
  if (variant_ == ssl_variant_datagram) {
    GTEST_SKIP() << "DTLS reset after ClientHello is fixed separately";
  }
  EnsureKeyShareSetup();
  const std::vector<SSLNamedGroup> groups = {ssl_grp_ec_curve25519};
  ConfigNamedGroups(groups);

  client_->StartConnect();
  client_->Handshake();
  ASSERT_TRUE(shares_capture_->captured());
  EXPECT_EQ(groups, GetShareDetails(shares_capture_));

  EXPECT_EQ(SECSuccess, SSL_ResetHandshake(client_->ssl_fd(), PR_FALSE));
  // SSL_ForceHandshake doesn't restart the handshake here, but a read does.
  uint8_t buf[1];
  EXPECT_GT(0, PR_Read(client_->ssl_fd(), buf, sizeof(buf)));
  EXPECT_EQ(PR_WOULD_BLOCK_ERROR, PORT_GetError());

  EXPECT_EQ(groups, GetShareDetails(shares_capture2_));
  EXPECT_NE(shares_capture_->extension(), shares_capture2_->extension());
}

// A client imported from a model that has already sent a ClientHello must
// generate its own key shares.
TEST_P(TlsKeyExchangeTest13, ImportFromModelAfterClientHello) {
  EnsureKeyShareSetup();
  const std::vector<SSLNamedGroup> groups = {ssl_grp_ec_curve25519};
  ConfigNamedGroups(groups);

  client_->StartConnect();
  client_->Handshake();
  ASSERT_TRUE(shares_capture_->captured());
  DataBuffer first_shares = shares_capture_->extension();

  auto model = client_;
  Reset();
  ASSERT_TRUE(client_->EnsureTlsSetup(model->ssl_fd().get()));
  EnsureKeyShareSetup();
  server_->ConfigNamedGroups(groups);

  Connect();
  EXPECT_EQ(groups, GetShareDetails(shares_capture_));
  EXPECT_NE(first_shares, shares_capture_->extension());
}

// Replace the point in the client key exchange message with an empty one
class ECCClientKEXFilter : public TlsHandshakeFilter {
 public:
  ECCClientKEXFilter(const std::shared_ptr<TlsAgent>& client)
      : TlsHandshakeFilter(client, {kTlsHandshakeClientKeyExchange}) {}

 protected:
  virtual PacketFilter::Action FilterHandshake(const HandshakeHeader& header,
                                               const DataBuffer& input,
                                               DataBuffer* output) {
    // Replace the client key exchange message with an empty point
    output->Allocate(1);
    output->Write(0, 0U, 1);  // set point length 0
    return CHANGE;
  }
};

// Replace the point in the server key exchange message with an empty one
class ECCServerKEXFilter : public TlsHandshakeFilter {
 public:
  ECCServerKEXFilter(const std::shared_ptr<TlsAgent>& server)
      : TlsHandshakeFilter(server, {kTlsHandshakeServerKeyExchange}) {}

 protected:
  virtual PacketFilter::Action FilterHandshake(const HandshakeHeader& header,
                                               const DataBuffer& input,
                                               DataBuffer* output) {
    // Replace the server key exchange message with an empty point
    output->Allocate(4);
    output->Write(0, 3U, 1);  // named curve
    uint32_t curve = 0;
    EXPECT_TRUE(input.Read(1, 2, &curve));  // get curve id
    output->Write(1, curve, 2);             // write curve id
    output->Write(3, 0U, 1);                // point length 0
    return CHANGE;
  }
};

TEST_P(TlsConnectGenericPre13, ConnectECDHEmptyServerPoint) {
  MakeTlsFilter<ECCServerKEXFilter>(server_);
  ConnectExpectAlert(client_, kTlsAlertIllegalParameter);
  client_->CheckErrorCode(SSL_ERROR_RX_MALFORMED_SERVER_KEY_EXCH);
}

TEST_P(TlsConnectGenericPre13, ConnectECDHEmptyClientPoint) {
  MakeTlsFilter<ECCClientKEXFilter>(client_);
  ConnectExpectAlert(server_, kTlsAlertIllegalParameter);
  server_->CheckErrorCode(SSL_ERROR_RX_MALFORMED_CLIENT_KEY_EXCH);
}

// Damage ECParams/ECPoint of a SKE.
class ECCServerKEXDamager : public TlsHandshakeFilter {
 public:
  ECCServerKEXDamager(const std::shared_ptr<TlsAgent>& server, ECType ec_type,
                      SSLNamedGroup named_curve)
      : TlsHandshakeFilter(server, {kTlsHandshakeServerKeyExchange}),
        ec_type_(ec_type),
        named_curve_(named_curve) {}

 protected:
  virtual PacketFilter::Action FilterHandshake(const HandshakeHeader& header,
                                               const DataBuffer& input,
                                               DataBuffer* output) {
    size_t offset = 0;
    output->Allocate(5);
    offset = output->Write(offset, ec_type_, 1);
    offset = output->Write(offset, named_curve_, 2);
    // Write a point with fmt != EC_POINT_FORM_UNCOMPRESSED.
    offset = output->Write(offset, 1U, 1);
    (void)output->Write(offset, 0x02, 1);  // EC_POINT_FORM_COMPRESSED_Y0
    return CHANGE;
  }

 private:
  ECType ec_type_;
  SSLNamedGroup named_curve_;
};

TEST_P(TlsConnectGenericPre13, ConnectUnsupportedCurveType) {
  EnsureTlsSetup();
  client_->DisableAllCiphers();
  client_->EnableCiphersByKeyExchange(ssl_kea_ecdh);

  MakeTlsFilter<ECCServerKEXDamager>(server_, ec_type_explicitPrime,
                                     ssl_grp_none);
  ConnectExpectAlert(client_, kTlsAlertHandshakeFailure);
  client_->CheckErrorCode(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
}

TEST_P(TlsConnectGenericPre13, ConnectUnsupportedCurve) {
  EnsureTlsSetup();
  client_->DisableAllCiphers();
  client_->EnableCiphersByKeyExchange(ssl_kea_ecdh);

  MakeTlsFilter<ECCServerKEXDamager>(server_, ec_type_named,
                                     ssl_grp_ffdhe_2048);
  ConnectExpectAlert(client_, kTlsAlertHandshakeFailure);
  client_->CheckErrorCode(SEC_ERROR_UNSUPPORTED_ELLIPTIC_CURVE);
}

TEST_P(TlsConnectGenericPre13, ConnectUnsupportedPointFormat) {
  EnsureTlsSetup();
  client_->DisableAllCiphers();
  client_->EnableCiphersByKeyExchange(ssl_kea_ecdh);

  MakeTlsFilter<ECCServerKEXDamager>(server_, ec_type_named,
                                     ssl_grp_ec_secp256r1);
  ConnectExpectAlert(client_, kTlsAlertHandshakeFailure);
  client_->CheckErrorCode(SEC_ERROR_UNSUPPORTED_EC_POINT_FORM);
}

TEST_P(TlsConnectTls12, ConnectUnsupportedSigAlg) {
  EnsureTlsSetup();
  client_->DisableAllCiphers();
  client_->EnableCiphersByKeyExchange(ssl_kea_ecdh);

  MakeTlsFilter<ECCServerKEXSigAlgReplacer>(server_, ssl_sig_none);
  ConnectExpectAlert(client_, kTlsAlertIllegalParameter);
  client_->CheckErrorCode(SSL_ERROR_UNSUPPORTED_SIGNATURE_ALGORITHM);
}

TEST_P(TlsConnectTls12, ConnectIncorrectSigAlg) {
  EnsureTlsSetup();
  client_->DisableAllCiphers();
  client_->EnableCiphersByKeyExchange(ssl_kea_ecdh);

  MakeTlsFilter<ECCServerKEXSigAlgReplacer>(server_,
                                            ssl_sig_ecdsa_secp256r1_sha256);
  ConnectExpectAlert(client_, kTlsAlertIllegalParameter);
  client_->CheckErrorCode(SSL_ERROR_INCORRECT_SIGNATURE_ALGORITHM);
}

static void CheckSkeSigScheme(
    std::shared_ptr<TlsHandshakeRecorder>& capture_ske,
    uint16_t expected_scheme) {
  TlsParser parser(capture_ske->buffer());
  uint32_t tmp = 0;
  EXPECT_TRUE(parser.Read(&tmp, 1)) << " read curve_type";
  EXPECT_EQ(3U, tmp) << "curve type has to be 3";
  EXPECT_TRUE(parser.Skip(2)) << " read namedcurve";
  EXPECT_TRUE(parser.SkipVariable(1)) << " read public";

  EXPECT_TRUE(parser.Read(&tmp, 2)) << " read sig_scheme";
  EXPECT_EQ(expected_scheme, static_cast<uint16_t>(tmp));
}

TEST_P(TlsConnectTls12, ConnectSigAlgEnabledByPolicy) {
  EnsureTlsSetup();
  client_->DisableAllCiphers();
  client_->EnableCiphersByKeyExchange(ssl_kea_ecdh);

  const std::vector<SSLSignatureScheme> schemes = {ssl_sig_rsa_pkcs1_sha1,
                                                   ssl_sig_rsa_pkcs1_sha384};

  client_->SetSignatureSchemes(schemes.data(), schemes.size());
  server_->SetSignatureSchemes(schemes.data(), schemes.size());
  auto capture_ske = MakeTlsFilter<TlsHandshakeRecorder>(
      server_, kTlsHandshakeServerKeyExchange);

  StartConnect();
  client_->Handshake();  // Send ClientHello

  // Enable SHA-1 by policy.
  SECStatus rv = NSS_SetAlgorithmPolicy(SEC_OID_SHA1, NSS_USE_ALG_IN_SSL_KX, 0);
  ASSERT_EQ(SECSuccess, rv);
  rv = NSS_SetAlgorithmPolicy(SEC_OID_APPLY_SSL_POLICY, NSS_USE_POLICY_IN_SSL,
                              0);
  ASSERT_EQ(SECSuccess, rv);

  Handshake();  // Remainder of handshake
  // The server should now report that it is connected
  EXPECT_EQ(TlsAgent::STATE_CONNECTED, server_->state());

  CheckSkeSigScheme(capture_ske, ssl_sig_rsa_pkcs1_sha1);
}

TEST_P(TlsConnectTls12, ConnectSigAlgDisabledByPolicy) {
  EnsureTlsSetup();
  client_->DisableAllCiphers();
  client_->EnableCiphersByKeyExchange(ssl_kea_ecdh);

  const std::vector<SSLSignatureScheme> schemes = {ssl_sig_rsa_pkcs1_sha1,
                                                   ssl_sig_rsa_pkcs1_sha384};

  client_->SetSignatureSchemes(schemes.data(), schemes.size());
  server_->SetSignatureSchemes(schemes.data(), schemes.size());
  auto capture_ske = MakeTlsFilter<TlsHandshakeRecorder>(
      server_, kTlsHandshakeServerKeyExchange);

  StartConnect();
  client_->Handshake();  // Send ClientHello

  // Disable SHA-1 by policy.
  SECStatus rv = NSS_SetAlgorithmPolicy(SEC_OID_SHA1, 0, NSS_USE_ALG_IN_SSL_KX);
  ASSERT_EQ(SECSuccess, rv);
  rv = NSS_SetAlgorithmPolicy(SEC_OID_APPLY_SSL_POLICY, NSS_USE_POLICY_IN_SSL,
                              0);
  ASSERT_EQ(SECSuccess, rv);

  Handshake();  // Remainder of handshake
  // The server should now report that it is connected
  EXPECT_EQ(TlsAgent::STATE_CONNECTED, server_->state());

  CheckSkeSigScheme(capture_ske, ssl_sig_rsa_pkcs1_sha384);
}

INSTANTIATE_TEST_SUITE_P(KeyExchangeTest, TlsKeyExchangeTest,
                         ::testing::Combine(TlsConnectTestBase::kTlsVariantsAll,
                                            TlsConnectTestBase::kTlsV11Plus));

#ifndef NSS_DISABLE_TLS_1_3
INSTANTIATE_TEST_SUITE_P(KeyExchangeTest, TlsKeyExchangeTest13,
                         ::testing::Combine(TlsConnectTestBase::kTlsVariantsAll,
                                            TlsConnectTestBase::kTlsV13));
#endif

}  // namespace nss_test
