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
#include "sslexp.h"
#include "sslproto.h"

extern "C" {
// This is not something that should make you happy.
#include "libssl_internals.h"
}

#include "gtest_utils.h"
#include "nss_scoped_ptrs.h"
#include "scoped_ptrs_ssl.h"
#include "tls_connect.h"
#include "tls_filter.h"
#include "tls_parser.h"
#include "tls_protect.h"
#include "pk11_import.h"

namespace nss_test {

class TlsServerKeyExchangeEcdhe {
 public:
  bool Parse(const DataBuffer& buffer) {
    TlsParser parser(buffer);

    uint8_t curve_type;
    if (!parser.Read(&curve_type)) {
      return false;
    }

    if (curve_type != 3) {  // named_curve
      return false;
    }

    uint32_t named_curve;
    if (!parser.Read(&named_curve, 2)) {
      return false;
    }

    return parser.ReadVariable(&public_key_, 1);
  }

  DataBuffer public_key_;
};

TEST_P(TlsConnectGenericPre13, ConnectResumed) {
  ConfigureSessionCache(RESUME_SESSIONID, RESUME_SESSIONID);
  Connect();

  Reset();
  ExpectResumption(RESUME_SESSIONID);
  Connect();
}

TEST_P(TlsConnectGenericResumption, ConnectClientCacheDisabled) {
  ConfigureSessionCache(RESUME_NONE, RESUME_SESSIONID);
  Connect();
  SendReceive();

  Reset();
  ExpectResumption(RESUME_NONE);
  Connect();
  SendReceive();
}

TEST_P(TlsConnectGenericResumption, ConnectServerCacheDisabled) {
  ConfigureSessionCache(RESUME_SESSIONID, RESUME_NONE);
  Connect();
  SendReceive();

  Reset();
  ExpectResumption(RESUME_NONE);
  Connect();
  SendReceive();
}

TEST_P(TlsConnectGenericResumption, ConnectSessionCacheDisabled) {
  ConfigureSessionCache(RESUME_NONE, RESUME_NONE);
  Connect();
  SendReceive();

  Reset();
  ExpectResumption(RESUME_NONE);
  Connect();
  SendReceive();
}

TEST_P(TlsConnectGenericResumption, ConnectResumeSupportBoth) {
  // This prefers tickets.
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  Connect();
  SendReceive();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ExpectResumption(RESUME_TICKET);
  Connect();
  SendReceive();
}

TEST_P(TlsConnectGenericResumption, ConnectResumeClientTicketServerBoth) {
  // This causes no resumption because the client needs the
  // session cache to resume even with tickets.
  ConfigureSessionCache(RESUME_TICKET, RESUME_BOTH);
  Connect();
  SendReceive();

  Reset();
  ConfigureSessionCache(RESUME_TICKET, RESUME_BOTH);
  ExpectResumption(RESUME_NONE);
  Connect();
  SendReceive();
}

TEST_P(TlsConnectGenericResumption, ConnectResumeClientBothTicketServerTicket) {
  // This causes a ticket resumption.
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  Connect();
  SendReceive();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ExpectResumption(RESUME_TICKET);
  Connect();
  SendReceive();
}

TEST_P(TlsConnectGenericResumption, ConnectResumeClientServerTicketOnly) {
  // This causes no resumption because the client needs the
  // session cache to resume even with tickets.
  ConfigureSessionCache(RESUME_TICKET, RESUME_TICKET);
  Connect();
  SendReceive();

  Reset();
  ConfigureSessionCache(RESUME_TICKET, RESUME_TICKET);
  ExpectResumption(RESUME_NONE);
  Connect();
  SendReceive();
}

TEST_P(TlsConnectGenericResumption, ConnectResumeClientBothServerNone) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_NONE);
  Connect();
  SendReceive();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_NONE);
  ExpectResumption(RESUME_NONE);
  Connect();
  SendReceive();
}

TEST_P(TlsConnectGenericResumption, ConnectResumeClientNoneServerBoth) {
  ConfigureSessionCache(RESUME_NONE, RESUME_BOTH);
  Connect();
  SendReceive();

  Reset();
  ConfigureSessionCache(RESUME_NONE, RESUME_BOTH);
  ExpectResumption(RESUME_NONE);
  Connect();
  SendReceive();
}

TEST_P(TlsConnectGenericPre13, ResumeWithHigherVersionTls13) {
  uint16_t lower_version = version_;
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  Connect();
  SendReceive();
  CheckKeys();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  EnsureTlsSetup();
  auto psk_ext = std::make_shared<TlsExtensionCapture>(
      client_, ssl_tls13_pre_shared_key_xtn);
  auto ticket_ext =
      std::make_shared<TlsExtensionCapture>(client_, ssl_session_ticket_xtn);
  client_->SetFilter(std::make_shared<ChainedPacketFilter>(
      ChainedPacketFilterInit({psk_ext, ticket_ext})));
  SetExpectedVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  client_->SetVersionRange(lower_version, SSL_LIBRARY_VERSION_TLS_1_3);
  server_->SetVersionRange(lower_version, SSL_LIBRARY_VERSION_TLS_1_3);
  ExpectResumption(RESUME_NONE);
  Connect();

  // The client shouldn't have sent a PSK, though it will send a ticket.
  EXPECT_FALSE(psk_ext->captured());
  EXPECT_TRUE(ticket_ext->captured());
}

class CaptureSessionId : public TlsHandshakeFilter {
 public:
  CaptureSessionId(const std::shared_ptr<TlsAgent>& a)
      : TlsHandshakeFilter(
            a, {kTlsHandshakeClientHello, kTlsHandshakeServerHello}),
        sid_() {}

  const DataBuffer& sid() const { return sid_; }

 protected:
  PacketFilter::Action FilterHandshake(const HandshakeHeader& header,
                                       const DataBuffer& input,
                                       DataBuffer* output) override {
    // The session_id is in the same place in both Hello messages:
    size_t offset = 2 + 32;  // Version(2) + Random(32)
    uint32_t len = 0;
    EXPECT_TRUE(input.Read(offset, 1, &len));
    offset++;
    if (input.len() < offset + len) {
      ADD_FAILURE() << "session_id overflows the Hello message";
      return KEEP;
    }
    sid_.Assign(input.data() + offset, len);
    return KEEP;
  }

 private:
  DataBuffer sid_;
};

// Attempting to resume from TLS 1.2 when 1.3 is possible should not result in
// resumption, though it will appear to be TLS 1.3 compatibility mode if the
// server uses a session ID.
TEST_P(TlsConnectGenericPre13, ResumeWithHigherVersionTls13SessionId) {
  uint16_t lower_version = version_;
  ConfigureSessionCache(RESUME_SESSIONID, RESUME_SESSIONID);
  auto original_sid = MakeTlsFilter<CaptureSessionId>(server_);
  Connect();
  CheckKeys();
  EXPECT_EQ(32U, original_sid->sid().len());

  // The client should now attempt to resume with the session ID from the last
  // connection.  This looks like compatibility mode, we just want to ensure
  // that we get TLS 1.3 rather than 1.2 (and no resumption).
  Reset();
  auto client_sid = MakeTlsFilter<CaptureSessionId>(client_);
  auto server_sid = MakeTlsFilter<CaptureSessionId>(server_);
  ConfigureSessionCache(RESUME_SESSIONID, RESUME_SESSIONID);
  SetExpectedVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  client_->SetVersionRange(lower_version, SSL_LIBRARY_VERSION_TLS_1_3);
  server_->SetVersionRange(lower_version, SSL_LIBRARY_VERSION_TLS_1_3);
  ExpectResumption(RESUME_NONE);

  Connect();
  SendReceive();

  EXPECT_EQ(client_sid->sid(), original_sid->sid());
  if (variant_ == ssl_variant_stream) {
    EXPECT_EQ(client_sid->sid(), server_sid->sid());
  } else {
    // DTLS servers don't echo the session ID.
    EXPECT_EQ(0U, server_sid->sid().len());
  }
}

TEST_P(TlsConnectPre12, ResumeWithHigherVersionTls12) {
  uint16_t lower_version = version_;
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  Connect();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  EnsureTlsSetup();
  SetExpectedVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  client_->SetVersionRange(lower_version, SSL_LIBRARY_VERSION_TLS_1_3);
  server_->SetVersionRange(lower_version, SSL_LIBRARY_VERSION_TLS_1_3);
  ExpectResumption(RESUME_NONE);
  Connect();
}

TEST_P(TlsConnectGenericPre13, ResumeWithLowerVersionFromTls13) {
  uint16_t original_version = version_;
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  Connect();
  SendReceive();
  CheckKeys();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ConfigureVersion(original_version);
  ExpectResumption(RESUME_NONE);
  Connect();
  SendReceive();
}

TEST_P(TlsConnectPre12, ResumeWithLowerVersionFromTls12) {
  uint16_t original_version = version_;
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_2);
  Connect();
  SendReceive();
  CheckKeys();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ConfigureVersion(original_version);
  ExpectResumption(RESUME_NONE);
  Connect();
  SendReceive();
}

TEST_P(TlsConnectGeneric, ConnectResumeClientBothTicketServerTicketForget) {
  // This causes a ticket resumption.
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  Connect();
  SendReceive();

  Reset();
  ClearServerCache();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ExpectResumption(RESUME_NONE);
  Connect();
  SendReceive();
}

// Tickets last two days maximum; this is a time longer than that.
static const PRTime kLongerThanTicketLifetime =
    3LL * 24 * 60 * 60 * PR_USEC_PER_SEC;

TEST_P(TlsConnectGenericResumption, ConnectWithExpiredTicketAtClient) {
  // This causes a ticket resumption.
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  Connect();
  SendReceive();

  AdvanceTime(kLongerThanTicketLifetime);

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ExpectResumption(RESUME_NONE);

  // TLS 1.3 uses the pre-shared key extension instead.
  SSLExtensionType xtn = (version_ >= SSL_LIBRARY_VERSION_TLS_1_3)
                             ? ssl_tls13_pre_shared_key_xtn
                             : ssl_session_ticket_xtn;
  auto capture = MakeTlsFilter<TlsExtensionCapture>(client_, xtn);
  Connect();

  if (version_ >= SSL_LIBRARY_VERSION_TLS_1_3) {
    EXPECT_FALSE(capture->captured());
  } else {
    EXPECT_TRUE(capture->captured());
    EXPECT_EQ(0U, capture->extension().len());
  }
}

TEST_P(TlsConnectGeneric, ConnectWithExpiredTicketAtServer) {
  // This causes a ticket resumption.
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  Connect();
  SendReceive();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ExpectResumption(RESUME_NONE);

  SSLExtensionType xtn = (version_ >= SSL_LIBRARY_VERSION_TLS_1_3)
                             ? ssl_tls13_pre_shared_key_xtn
                             : ssl_session_ticket_xtn;
  auto capture = MakeTlsFilter<TlsExtensionCapture>(client_, xtn);
  StartConnect();
  client_->Handshake();
  EXPECT_TRUE(capture->captured());
  EXPECT_LT(0U, capture->extension().len());

  AdvanceTime(kLongerThanTicketLifetime);

  Handshake();
  CheckConnected();
}

TEST_P(TlsConnectGeneric, ConnectResumeCorruptTicket) {
  // This causes a ticket resumption.
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  Connect();
  SendReceive();

  Reset();
  static const uint8_t kHmacKey1Buf[32] = {0};
  static const DataBuffer kHmacKey1(kHmacKey1Buf, sizeof(kHmacKey1Buf));

  SECItem key_item = {siBuffer, const_cast<uint8_t*>(kHmacKey1Buf),
                      sizeof(kHmacKey1Buf)};

  ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
  PK11SymKey* hmac_key =
      nss_test::ImportSymKey(slot.get(), CKM_SHA256_HMAC, PK11_OriginUnwrap,
                             CKA_SIGN, &key_item, nullptr);
  ASSERT_NE(nullptr, hmac_key);
  SSLInt_SetSelfEncryptMacKey(hmac_key);
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  if (version_ >= SSL_LIBRARY_VERSION_TLS_1_3) {
    ExpectResumption(RESUME_NONE);
    Connect();
  } else {
    ConnectExpectAlert(server_, illegal_parameter);
    server_->CheckErrorCode(SSL_ERROR_RX_MALFORMED_CLIENT_HELLO);
  }
}

// This callback switches out the "server" cert used on the server with
// the "client" certificate, which should be the same type.
static int32_t SwitchCertificates(TlsAgent* agent, const SECItem* srvNameArr,
                                  uint32_t srvNameArrSize) {
  bool ok = agent->ConfigServerCert("client");
  if (!ok) return SSL_SNI_SEND_ALERT;

  return 0;  // first config
};

TEST_P(TlsConnectGeneric, ServerSNICertSwitch) {
  Connect();
  ScopedCERTCertificate cert1(SSL_PeerCertificate(client_->ssl_fd()));
  ASSERT_NE(nullptr, cert1.get());

  Reset();
  ConfigureSessionCache(RESUME_NONE, RESUME_NONE);

  server_->SetSniCallback(SwitchCertificates);

  Connect();
  ScopedCERTCertificate cert2(SSL_PeerCertificate(client_->ssl_fd()));
  ASSERT_NE(nullptr, cert2.get());
  CheckKeys();
  EXPECT_FALSE(SECITEM_ItemsAreEqual(&cert1->derCert, &cert2->derCert));
}

TEST_P(TlsConnectGeneric, ServerSNICertTypeSwitch) {
  Reset(TlsAgent::kServerEcdsa256);
  Connect();
  ScopedCERTCertificate cert1(SSL_PeerCertificate(client_->ssl_fd()));
  ASSERT_NE(nullptr, cert1.get());

  Reset();
  ConfigureSessionCache(RESUME_NONE, RESUME_NONE);

  // Because we configure an RSA certificate here, it only adds a second, unused
  // certificate, which has no effect on what the server uses.
  server_->SetSniCallback(SwitchCertificates);

  Connect();
  ScopedCERTCertificate cert2(SSL_PeerCertificate(client_->ssl_fd()));
  ASSERT_NE(nullptr, cert2.get());
  CheckKeys(ssl_auth_ecdsa);
  EXPECT_TRUE(SECITEM_ItemsAreEqual(&cert1->derCert, &cert2->derCert));
}

TEST_P(TlsConnectGenericPre13, ConnectEcdheTwiceReuseKey) {
  auto filter = MakeTlsFilter<TlsHandshakeRecorder>(
      server_, kTlsHandshakeServerKeyExchange);
  EnableECDHEServerKeyReuse();
  Connect();
  CheckKeys();
  TlsServerKeyExchangeEcdhe dhe1;
  EXPECT_TRUE(dhe1.Parse(filter->buffer()));

  // Restart
  Reset();
  EnableECDHEServerKeyReuse();
  auto filter2 = MakeTlsFilter<TlsHandshakeRecorder>(
      server_, kTlsHandshakeServerKeyExchange);
  ConfigureSessionCache(RESUME_NONE, RESUME_NONE);
  Connect();
  CheckKeys();

  TlsServerKeyExchangeEcdhe dhe2;
  EXPECT_TRUE(dhe2.Parse(filter2->buffer()));

  // Make sure they are the same.
  EXPECT_EQ(dhe1.public_key_.len(), dhe2.public_key_.len());
  EXPECT_TRUE(!memcmp(dhe1.public_key_.data(), dhe2.public_key_.data(),
                      dhe1.public_key_.len()));
}

// This test parses the ServerKeyExchange, which isn't in 1.3
TEST_P(TlsConnectGenericPre13, ConnectEcdheTwiceNewKey) {
  auto filter = MakeTlsFilter<TlsHandshakeRecorder>(
      server_, kTlsHandshakeServerKeyExchange);
  Connect();
  CheckKeys();
  TlsServerKeyExchangeEcdhe dhe1;
  EXPECT_TRUE(dhe1.Parse(filter->buffer()));

  // Restart
  Reset();
  auto filter2 = MakeTlsFilter<TlsHandshakeRecorder>(
      server_, kTlsHandshakeServerKeyExchange);
  ConfigureSessionCache(RESUME_NONE, RESUME_NONE);
  Connect();
  CheckKeys();

  TlsServerKeyExchangeEcdhe dhe2;
  EXPECT_TRUE(dhe2.Parse(filter2->buffer()));

  // Make sure they are different.
  EXPECT_FALSE((dhe1.public_key_.len() == dhe2.public_key_.len()) &&
               (!memcmp(dhe1.public_key_.data(), dhe2.public_key_.data(),
                        dhe1.public_key_.len())));
}

// Verify that TLS 1.3 reports an accurate group on resumption.
TEST_P(TlsConnectTls13, TestTls13ResumeDifferentGroup) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  Connect();
  SendReceive();  // Need to read so that we absorb the session ticket.
  CheckKeys();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ExpectResumption(RESUME_TICKET);
  client_->ConfigNamedGroups(kFFDHEGroups);
  server_->ConfigNamedGroups(kFFDHEGroups);
  Connect();
  CheckKeys(ssl_kea_dh, ssl_grp_ffdhe_2048, ssl_auth_rsa_sign,
            ssl_sig_rsa_pss_rsae_sha256);
}

// Verify that TLS 1.3 server doesn't request certificate in the main
// handshake, after resumption.
TEST_P(TlsConnectTls13, TestTls13ResumeNoCertificateRequest) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  client_->SetupClientAuth();
  server_->RequestClientAuth(true);
  Connect();
  SendReceive();  // Need to read so that we absorb the session ticket.
  ScopedCERTCertificate cert1(SSL_LocalCertificate(client_->ssl_fd()));
  ASSERT_NE(nullptr, cert1.get());

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ExpectResumption(RESUME_TICKET);
  server_->RequestClientAuth(false);
  auto cr_capture =
      MakeTlsFilter<TlsHandshakeRecorder>(server_, ssl_hs_certificate_request);
  cr_capture->EnableDecryption();
  Connect();
  SendReceive();
  EXPECT_EQ(0U, cr_capture->buffer().len()) << "expect nothing captured yet";

  // Sanity check whether the client certificate matches the one
  // decrypted from ticket.
  ScopedCERTCertificate cert2(SSL_PeerCertificate(server_->ssl_fd()));
  ASSERT_NE(nullptr, cert2.get());
  EXPECT_TRUE(SECITEM_ItemsAreEqual(&cert1->derCert, &cert2->derCert));
}

// Here we test that 0.5 RTT is available at the server when resuming, even if
// configured to request a client certificate.  The resumed handshake relies on
// the authentication from the original handshake, so no certificate is
// requested this time around.  The server can write before the handshake
// completes because the PSK binder is sufficient authentication for the client.
TEST_P(TlsConnectTls13, WriteBeforeHandshakeCompleteOnResumption) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  client_->SetupClientAuth();
  server_->RequestClientAuth(true);
  Connect();
  SendReceive();  // Absorb the session ticket.
  ScopedCERTCertificate cert1(SSL_LocalCertificate(client_->ssl_fd()));
  ASSERT_NE(nullptr, cert1.get());

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ExpectResumption(RESUME_TICKET);
  server_->RequestClientAuth(false);
  StartConnect();
  client_->Handshake();  // ClientHello
  server_->Handshake();  // ServerHello

  server_->SendData(10);
  client_->ReadBytes(10);  // Client should emit the Finished as a side-effect.
  server_->Handshake();    // Server consumes the Finished.
  CheckConnected();

  // Check whether the client certificate matches the one from the ticket.
  ScopedCERTCertificate cert2(SSL_PeerCertificate(server_->ssl_fd()));
  ASSERT_NE(nullptr, cert2.get());
  EXPECT_TRUE(SECITEM_ItemsAreEqual(&cert1->derCert, &cert2->derCert));
}

// We need to enable different cipher suites at different times in the following
// tests.  Those cipher suites need to be suited to the version.
static uint16_t ChooseOneCipher(uint16_t version) {
  if (version >= SSL_LIBRARY_VERSION_TLS_1_3) {
    return TLS_AES_128_GCM_SHA256;
  }
  return TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA;
}

static uint16_t ChooseIncompatibleCipher(uint16_t version) {
  if (version >= SSL_LIBRARY_VERSION_TLS_1_3) {
    return TLS_AES_256_GCM_SHA384;
  }
  return TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA;
}

// Test that we don't resume when we can't negotiate the same cipher.  Note that
// for TLS 1.3, resumption is allowed between compatible ciphers, that is those
// with the same KDF hash, but we choose an incompatible one here.
TEST_P(TlsConnectGenericResumption, ResumeClientIncompatibleCipher) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  client_->EnableSingleCipher(ChooseOneCipher(version_));
  Connect();
  SendReceive();
  CheckKeys();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ExpectResumption(RESUME_NONE);
  client_->EnableSingleCipher(ChooseIncompatibleCipher(version_));
  uint16_t ticket_extension;
  if (version_ >= SSL_LIBRARY_VERSION_TLS_1_3) {
    ticket_extension = ssl_tls13_pre_shared_key_xtn;
  } else {
    ticket_extension = ssl_session_ticket_xtn;
  }
  auto ticket_capture =
      MakeTlsFilter<TlsExtensionCapture>(client_, ticket_extension);
  Connect();
  CheckKeys();
  EXPECT_EQ(0U, ticket_capture->extension().len());
}

// Test that we don't resume when we can't negotiate the same cipher.
TEST_P(TlsConnectGenericResumption, ResumeServerIncompatibleCipher) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  server_->EnableSingleCipher(ChooseOneCipher(version_));
  Connect();
  SendReceive();  // Absorb the session ticket.
  CheckKeys();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ExpectResumption(RESUME_NONE);
  server_->EnableSingleCipher(ChooseIncompatibleCipher(version_));
  Connect();
  CheckKeys();
}

// Test that the client doesn't tolerate the server picking a different cipher
// suite for resumption.
TEST_P(TlsConnectStream, ResumptionOverrideCipher) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  server_->EnableSingleCipher(ChooseOneCipher(version_));
  Connect();
  SendReceive();
  CheckKeys();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  MakeTlsFilter<SelectedCipherSuiteReplacer>(
      server_, ChooseIncompatibleCipher(version_));

  if (version_ >= SSL_LIBRARY_VERSION_TLS_1_3) {
    client_->ExpectSendAlert(kTlsAlertIllegalParameter);
    server_->ExpectSendAlert(kTlsAlertUnexpectedMessage);
  } else {
    ExpectAlert(client_, kTlsAlertHandshakeFailure);
  }
  ConnectExpectFail();
  client_->CheckErrorCode(SSL_ERROR_RX_MALFORMED_SERVER_HELLO);
  if (version_ >= SSL_LIBRARY_VERSION_TLS_1_3) {
    // The reason this test is stream only: the server is unable to decrypt
    // the alert that the client sends, see bug 1304603.
    server_->CheckErrorCode(SSL_ERROR_RX_UNEXPECTED_RECORD_TYPE);
  } else {
    server_->CheckErrorCode(SSL_ERROR_HANDSHAKE_FAILURE_ALERT);
  }
}

// In TLS 1.3, it is possible to resume with a different cipher if it has the
// same hash.
TEST_P(TlsConnectTls13, ResumeClientCompatibleCipher) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  client_->EnableSingleCipher(TLS_AES_128_GCM_SHA256);
  Connect();
  SendReceive();  // Absorb the session ticket.
  CheckKeys();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ExpectResumption(RESUME_TICKET);
  client_->EnableSingleCipher(TLS_CHACHA20_POLY1305_SHA256);
  Connect();
  CheckKeys();
}

TEST_P(TlsConnectTls13, ResumeServerCompatibleCipher) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  server_->EnableSingleCipher(TLS_AES_128_GCM_SHA256);
  Connect();
  SendReceive();  // Absorb the session ticket.
  CheckKeys();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ExpectResumption(RESUME_TICKET);
  server_->EnableSingleCipher(TLS_CHACHA20_POLY1305_SHA256);
  Connect();
  CheckKeys();
}

class SelectedVersionReplacer : public TlsHandshakeFilter {
 public:
  SelectedVersionReplacer(const std::shared_ptr<TlsAgent>& a, uint16_t version)
      : TlsHandshakeFilter(a, {kTlsHandshakeServerHello}), version_(version) {}

 protected:
  PacketFilter::Action FilterHandshake(const HandshakeHeader& header,
                                       const DataBuffer& input,
                                       DataBuffer* output) override {
    *output = input;
    output->Write(0, static_cast<uint32_t>(version_), 2);
    return CHANGE;
  }

 private:
  uint16_t version_;
};

// Test how the client handles the case where the server picks a
// lower version number on resumption.
TEST_P(TlsConnectGenericPre13, TestResumptionOverrideVersion) {
  uint16_t override_version = 0;
  if (variant_ == ssl_variant_stream) {
    switch (version_) {
      case SSL_LIBRARY_VERSION_TLS_1_0:
        GTEST_SKIP();
      case SSL_LIBRARY_VERSION_TLS_1_1:
        override_version = SSL_LIBRARY_VERSION_TLS_1_0;
        break;
      case SSL_LIBRARY_VERSION_TLS_1_2:
        override_version = SSL_LIBRARY_VERSION_TLS_1_1;
        break;
      default:
        ASSERT_TRUE(false) << "unknown version";
    }
  } else {
    if (version_ == SSL_LIBRARY_VERSION_TLS_1_2) {
      override_version = SSL_LIBRARY_VERSION_DTLS_1_0_WIRE;
    } else {
      ASSERT_EQ(SSL_LIBRARY_VERSION_TLS_1_1, version_);
      GTEST_SKIP();
    }
  }

  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  // Need to use a cipher that is plausible for the lower version.
  server_->EnableSingleCipher(TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA);
  Connect();
  CheckKeys();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  // Enable the lower version on the client.
  client_->SetVersionRange(version_ - 1, version_);
  server_->EnableSingleCipher(TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA);
  MakeTlsFilter<SelectedVersionReplacer>(server_, override_version);

  ConnectExpectAlert(client_, kTlsAlertHandshakeFailure);
  client_->CheckErrorCode(SSL_ERROR_RX_MALFORMED_SERVER_HELLO);
  server_->CheckErrorCode(SSL_ERROR_HANDSHAKE_FAILURE_ALERT);
}

// Test that two TLS resumptions work and produce the same ticket.
// This will change after bug 1257047 is fixed.
TEST_F(TlsConnectTest, TestTls13ResumptionTwice) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);

  Connect();
  SendReceive();  // Need to read so that we absorb the session ticket.
  CheckKeys();
  uint16_t original_suite;
  EXPECT_TRUE(client_->cipher_suite(&original_suite));

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  ExpectResumption(RESUME_TICKET);
  auto c1 =
      MakeTlsFilter<TlsExtensionCapture>(client_, ssl_tls13_pre_shared_key_xtn);
  Connect();
  SendReceive();
  CheckKeys(ssl_auth_rsa_sign, ssl_sig_rsa_pss_rsae_sha256);
  // The filter will go away when we reset, so save the captured extension.
  DataBuffer initialTicket(c1->extension());
  ASSERT_LT(0U, initialTicket.len());

  ScopedCERTCertificate cert1(SSL_PeerCertificate(client_->ssl_fd()));
  ASSERT_NE(nullptr, cert1.get());

  Reset();
  ClearStats();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  auto c2 =
      MakeTlsFilter<TlsExtensionCapture>(client_, ssl_tls13_pre_shared_key_xtn);
  ExpectResumption(RESUME_TICKET);
  Connect();
  SendReceive();
  CheckKeys(ssl_auth_rsa_sign, ssl_sig_rsa_pss_rsae_sha256);
  ASSERT_LT(0U, c2->extension().len());

  ScopedCERTCertificate cert2(SSL_PeerCertificate(client_->ssl_fd()));
  ASSERT_NE(nullptr, cert2.get());

  // Check that the cipher suite is reported the same on both sides, though in
  // TLS 1.3 resumption actually negotiates a different cipher suite.
  uint16_t resumed_suite;
  EXPECT_TRUE(server_->cipher_suite(&resumed_suite));
  EXPECT_EQ(original_suite, resumed_suite);
  EXPECT_TRUE(client_->cipher_suite(&resumed_suite));
  EXPECT_EQ(original_suite, resumed_suite);

  ASSERT_NE(initialTicket, c2->extension());
}

// Check that resumption works after receiving two NST messages.
TEST_F(TlsConnectTest, TestTls13ResumptionDuplicateNST) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  Connect();

  // Clear the session ticket keys to invalidate the old ticket.
  ClearServerCache();
  EXPECT_EQ(SECSuccess, SSL_SendSessionTicket(server_->ssl_fd(), NULL, 0));

  SendReceive();  // Need to read so that we absorb the session tickets.
  CheckKeys();

  // Resume the connection.
  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  ExpectResumption(RESUME_TICKET);
  Connect();
  SendReceive();
}

// Check that the value captured in a NewSessionTicket message matches the value
// captured from a pre_shared_key extension.
void NstTicketMatchesPskIdentity(const DataBuffer& nst, const DataBuffer& psk) {
  uint32_t len;

  size_t offset = 4 + 4;  // Skip ticket_lifetime and ticket_age_add.
  ASSERT_TRUE(nst.Read(offset, 1, &len));
  offset += 1 + len;  // Skip ticket_nonce.

  ASSERT_TRUE(nst.Read(offset, 2, &len));
  offset += 2;  // Skip the ticket length.
  ASSERT_LE(offset + len, nst.len());
  DataBuffer nst_ticket(nst.data() + offset, static_cast<size_t>(len));

  offset = 2;  // Skip the identities length.
  ASSERT_TRUE(psk.Read(offset, 2, &len));
  offset += 2;  // Skip the identity length.
  ASSERT_LE(offset + len, psk.len());
  DataBuffer psk_ticket(psk.data() + offset, static_cast<size_t>(len));

  EXPECT_EQ(nst_ticket, psk_ticket);
}

TEST_F(TlsConnectTest, TestTls13ResumptionDuplicateNSTWithToken) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);

  auto nst_capture =
      MakeTlsFilter<TlsHandshakeRecorder>(server_, ssl_hs_new_session_ticket);
  nst_capture->EnableDecryption();
  Connect();

  // Clear the session ticket keys to invalidate the old ticket.
  ClearServerCache();
  nst_capture->Reset();
  uint8_t token[] = {0x20, 0x20, 0xff, 0x00};
  EXPECT_EQ(SECSuccess,
            SSL_SendSessionTicket(server_->ssl_fd(), token, sizeof(token)));

  SendReceive();  // Need to read so that we absorb the session tickets.
  CheckKeys();
  EXPECT_LT(0U, nst_capture->buffer().len());

  // Resume the connection.
  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  ExpectResumption(RESUME_TICKET);

  auto psk_capture =
      MakeTlsFilter<TlsExtensionCapture>(client_, ssl_tls13_pre_shared_key_xtn);
  Connect();
  SendReceive();

  NstTicketMatchesPskIdentity(nst_capture->buffer(), psk_capture->extension());
}

// Disable SSL_ENABLE_SESSION_TICKETS but ensure that tickets can still be sent
// by invoking SSL_SendSessionTicket directly (and that the ticket is usable).
TEST_F(TlsConnectTest, SendSessionTicketWithTicketsDisabled) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);

  server_->SetOption(SSL_ENABLE_SESSION_TICKETS, PR_FALSE);

  auto nst_capture =
      MakeTlsFilter<TlsHandshakeRecorder>(server_, ssl_hs_new_session_ticket);
  nst_capture->EnableDecryption();
  Connect();

  EXPECT_EQ(0U, nst_capture->buffer().len()) << "expect nothing captured yet";

  EXPECT_EQ(SECSuccess, SSL_SendSessionTicket(server_->ssl_fd(), NULL, 0));
  EXPECT_LT(0U, nst_capture->buffer().len()) << "should capture now";

  SendReceive();  // Ensure that the client reads the ticket.

  // Resume the connection.
  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  ExpectResumption(RESUME_TICKET);

  auto psk_capture =
      MakeTlsFilter<TlsExtensionCapture>(client_, ssl_tls13_pre_shared_key_xtn);
  Connect();
  SendReceive();

  NstTicketMatchesPskIdentity(nst_capture->buffer(), psk_capture->extension());
}

// Successfully send a session ticket after resuming and then use it.
TEST_F(TlsConnectTest, SendTicketAfterResumption) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  Connect();

  SendReceive();  // Need to read so that we absorb the session tickets.
  CheckKeys();

  // Resume the connection.
  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  ExpectResumption(RESUME_TICKET);

  // We need to capture just one ticket, so
  // disable automatic sending of tickets at the server.
  // ConfigureSessionCache enables this option, so revert that.
  server_->SetOption(SSL_ENABLE_SESSION_TICKETS, PR_FALSE);
  auto nst_capture =
      MakeTlsFilter<TlsHandshakeRecorder>(server_, ssl_hs_new_session_ticket);
  nst_capture->EnableDecryption();
  Connect();

  ClearServerCache();
  EXPECT_EQ(SECSuccess, SSL_SendSessionTicket(server_->ssl_fd(), NULL, 0));
  SendReceive();

  // Reset stats so that the counters for resumptions match up.
  ClearStats();
  // Resume again and ensure that we get the same ticket.
  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  ExpectResumption(RESUME_TICKET);

  auto psk_capture =
      MakeTlsFilter<TlsExtensionCapture>(client_, ssl_tls13_pre_shared_key_xtn);
  Connect();
  SendReceive();

  NstTicketMatchesPskIdentity(nst_capture->buffer(), psk_capture->extension());
}

// Test calling SSL_SendSessionTicket in inappropriate conditions.
TEST_F(TlsConnectTest, SendSessionTicketInappropriate) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_2);

  EXPECT_EQ(SECFailure, SSL_SendSessionTicket(client_->ssl_fd(), NULL, 0))
      << "clients can't send tickets";
  EXPECT_EQ(SEC_ERROR_INVALID_ARGS, PORT_GetError());

  StartConnect();

  EXPECT_EQ(SECFailure, SSL_SendSessionTicket(server_->ssl_fd(), NULL, 0))
      << "no ticket before the handshake has started";
  EXPECT_EQ(SEC_ERROR_INVALID_ARGS, PORT_GetError());
  Handshake();
  EXPECT_EQ(SECFailure, SSL_SendSessionTicket(server_->ssl_fd(), NULL, 0))
      << "no special tickets in TLS 1.2";
  EXPECT_EQ(SEC_ERROR_INVALID_ARGS, PORT_GetError());
}

TEST_F(TlsConnectTest, SendSessionTicketMassiveToken) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  Connect();
  // It should be safe to set length with a NULL token because the length should
  // be checked before reading token.
  EXPECT_EQ(SECFailure, SSL_SendSessionTicket(server_->ssl_fd(), NULL, 0x1ffff))
      << "this is clearly too big";
  EXPECT_EQ(SEC_ERROR_INVALID_ARGS, PORT_GetError());

  static const uint8_t big_token[0xffff] = {1};
  EXPECT_EQ(SECFailure, SSL_SendSessionTicket(server_->ssl_fd(), big_token,
                                              sizeof(big_token)))
      << "this is too big, but that's not immediately obvious";
  EXPECT_EQ(SEC_ERROR_INVALID_ARGS, PORT_GetError());
}

TEST_F(TlsConnectDatagram13, SendSessionTicketDtls) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  Connect();
  EXPECT_EQ(SECFailure, SSL_SendSessionTicket(server_->ssl_fd(), NULL, 0))
      << "no extra tickets in DTLS until we have Ack support";
  EXPECT_EQ(SSL_ERROR_FEATURE_NOT_SUPPORTED_FOR_VERSION, PORT_GetError());
}

TEST_F(TlsConnectStreamTls13, ExternalResumptionUseSecondTicket) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);

  struct ResumptionTicketState {
    std::vector<uint8_t> ticket;
    size_t invoked = 0;
  } ticket_state;
  auto cb = [](PRFileDesc* fd, const PRUint8* ticket, unsigned int ticket_len,
               void* arg) -> SECStatus {
    auto state = reinterpret_cast<ResumptionTicketState*>(arg);
    state->ticket.assign(ticket, ticket + ticket_len);
    state->invoked++;
    return SECSuccess;
  };
  EXPECT_EQ(SECSuccess, SSL_SetResumptionTokenCallback(client_->ssl_fd(), cb,
                                                       &ticket_state));

  Connect();
  EXPECT_EQ(SECSuccess, SSL_SendSessionTicket(server_->ssl_fd(), nullptr, 0));
  SendReceive();
  EXPECT_EQ(2U, ticket_state.invoked);

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  client_->SetResumptionToken(ticket_state.ticket);
  ExpectResumption(RESUME_TICKET);
  Connect();
  SendReceive();
}

TEST_F(TlsConnectTest, TestTls13ResumptionDowngrade) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  Connect();

  SendReceive();  // Need to read so that we absorb the session tickets.
  CheckKeys();

  // Try resuming the connection. This will fail resuming the 1.3 session
  // from before, but will successfully establish a 1.2 connection.
  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  client_->SetVersionRange(SSL_LIBRARY_VERSION_TLS_1_2,
                           SSL_LIBRARY_VERSION_TLS_1_3);
  server_->SetVersionRange(SSL_LIBRARY_VERSION_TLS_1_2,
                           SSL_LIBRARY_VERSION_TLS_1_2);
  Connect();

  // Renegotiate to ensure we don't carryover any state
  // from the 1.3 resumption attempt.
  client_->SetExpectedVersion(SSL_LIBRARY_VERSION_TLS_1_2);
  client_->PrepareForRenegotiate();
  server_->StartRenegotiate();
  Handshake();

  SendReceive();
  CheckKeys(ssl_kea_ecdh);
}

TEST_F(TlsConnectTest, TestTls13ResumptionForcedDowngrade) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  Connect();

  SendReceive();  // Need to read so that we absorb the session tickets.
  CheckKeys();

  // Try resuming the connection.
  Reset();
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  // Enable the lower version on the client.
  client_->SetVersionRange(SSL_LIBRARY_VERSION_TLS_1_2,
                           SSL_LIBRARY_VERSION_TLS_1_3);

  // Add filters that set downgrade SH.version to 1.2 and the cipher suite
  // to one that works with 1.2, so that we don't run into early sanity checks.
  // We will eventually fail the (sid.version == SH.version) check.
  std::vector<std::shared_ptr<PacketFilter>> filters;
  filters.push_back(std::make_shared<SelectedCipherSuiteReplacer>(
      server_, TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256));
  filters.push_back(std::make_shared<SelectedVersionReplacer>(
      server_, SSL_LIBRARY_VERSION_TLS_1_2));

  // Drop a bunch of extensions so that we get past the SH processing.  The
  // version extension says TLS 1.3, which is counter to our goal, the others
  // are not permitted in TLS 1.2 handshakes.
  filters.push_back(std::make_shared<TlsExtensionDropper>(
      server_, ssl_tls13_supported_versions_xtn));
  filters.push_back(
      std::make_shared<TlsExtensionDropper>(server_, ssl_tls13_key_share_xtn));
  filters.push_back(std::make_shared<TlsExtensionDropper>(
      server_, ssl_tls13_pre_shared_key_xtn));
  server_->SetFilter(std::make_shared<ChainedPacketFilter>(filters));

  // The client here generates an unexpected_message alert when it receives an
  // encrypted handshake message from the server (EncryptedExtension).  The
  // client expects to receive an unencrypted TLS 1.2 Certificate message.
  // The server can't decrypt the alert.
  client_->ExpectSendAlert(kTlsAlertUnexpectedMessage);
  server_->ExpectSendAlert(kTlsAlertUnexpectedMessage);  // Server can't read
  ConnectExpectFail();
  client_->CheckErrorCode(SSL_ERROR_RX_UNEXPECTED_APPLICATION_DATA);
  server_->CheckErrorCode(SSL_ERROR_RX_UNEXPECTED_RECORD_TYPE);
}

TEST_P(TlsConnectGenericResumption, ReConnectTicket) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  server_->EnableSingleCipher(ChooseOneCipher(version_));
  Connect();
  SendReceive();
  CheckKeys(ssl_auth_rsa_sign, ssl_sig_rsa_pss_rsae_sha256);
  // Resume
  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ExpectResumption(RESUME_TICKET);
  Connect();
  // Only the client knows this.
  CheckKeysResumption(GetDefaultKEA(), ssl_grp_none,
                      GetDefaultGroupFromKEA(GetDefaultKEA()),
                      ssl_auth_rsa_sign, ssl_sig_rsa_pss_rsae_sha256);
}

TEST_P(TlsConnectGenericPre13, ReConnectCache) {
  ConfigureSessionCache(RESUME_SESSIONID, RESUME_SESSIONID);
  server_->EnableSingleCipher(ChooseOneCipher(version_));
  Connect();
  SendReceive();
  CheckKeys(ssl_auth_rsa_sign, ssl_sig_rsa_pss_rsae_sha256);
  // Resume
  Reset();
  ExpectResumption(RESUME_SESSIONID);
  Connect();
  CheckKeysResumption(GetDefaultKEA(), ssl_grp_none,
                      GetDefaultGroupFromKEA(GetDefaultKEA()),
                      ssl_auth_rsa_sign, ssl_sig_rsa_pss_rsae_sha256);
}

TEST_P(TlsConnectGenericResumption, ReConnectAgainTicket) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  server_->EnableSingleCipher(ChooseOneCipher(version_));
  Connect();
  SendReceive();
  CheckKeys(ssl_auth_rsa_sign, ssl_sig_rsa_pss_rsae_sha256);
  // Resume
  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ExpectResumption(RESUME_TICKET);
  Connect();
  // Only the client knows this.
  CheckKeysResumption(GetDefaultKEA(), ssl_grp_none,
                      GetDefaultGroupFromKEA(GetDefaultKEA()),
                      ssl_auth_rsa_sign, ssl_sig_rsa_pss_rsae_sha256);
  // Resume connection again
  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ExpectResumption(RESUME_TICKET, 2);
  Connect();
  // Only the client knows this.
  CheckKeysResumption(GetDefaultKEA(), ssl_grp_none,
                      GetDefaultGroupFromKEA(GetDefaultKEA()),
                      ssl_auth_rsa_sign, ssl_sig_rsa_pss_rsae_sha256);
}

void CheckGetInfoResult(PRTime now, uint32_t alpnSize, uint32_t earlyDataSize,
                        ScopedCERTCertificate& cert,
                        ScopedSSLResumptionTokenInfo& token) {
  ASSERT_TRUE(cert);
  ASSERT_TRUE(token->peerCert);

  // Check that the server cert is the correct one.
  ASSERT_EQ(cert->derCert.len, token->peerCert->derCert.len);
  EXPECT_EQ(0, memcmp(cert->derCert.data, token->peerCert->derCert.data,
                      cert->derCert.len));

  ASSERT_EQ(alpnSize, token->alpnSelectionLen);
  EXPECT_EQ(0, memcmp("a", token->alpnSelection, token->alpnSelectionLen));

  ASSERT_EQ(earlyDataSize, token->maxEarlyDataSize);

  ASSERT_LT(now, token->expirationTime);
}

// The client should generate a new, randomized session_id
// when resuming using an external token.
// Bytes to skip at the start of the token to reach the peerCert length
// prefix.
static size_t TokenHeaderSize(const std::vector<uint8_t>& token) {
  if (token[0] != SSLResumptionTokenVersion) {
    ADD_FAILURE() << "unhandled resumption token version "
                  << unsigned(token[0]);
    return 0;
  }
  return 1 /* version */ + 1 /* feature bitfield */ + 8 /* expirationTime */ +
         8 /* received_timestamp */ + 4 /* ticket_lifetime_hint */ +
         4 /* flags */ + 4 /* ticket_age_add */ + 4 /* max_early_data_size */;
}

TEST_P(TlsConnectGenericResumptionToken, CheckSessionId) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  auto original_sid = MakeTlsFilter<CaptureSessionId>(client_);
  Connect();
  SendReceive();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ExpectResumption(RESUME_TICKET);

  StartConnect();
  ASSERT_TRUE(client_->MaybeSetResumptionToken());
  auto resumed_sid = MakeTlsFilter<CaptureSessionId>(client_);

  Handshake();
  CheckConnected();
  SendReceive();

  if (version_ < SSL_LIBRARY_VERSION_TLS_1_3) {
    EXPECT_NE(resumed_sid->sid(), original_sid->sid());
    EXPECT_EQ(32U, resumed_sid->sid().len());
  } else {
    EXPECT_EQ(0U, resumed_sid->sid().len());
  }
}

TEST_P(TlsConnectResumptionTokenShape, ConnectResumeGetInfo) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  Connect();
  SendReceive();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ExpectResumption(RESUME_TICKET);

  StartConnect();
  ASSERT_TRUE(client_->MaybeSetResumptionToken());

  // Get resumption token infos
  SSLResumptionTokenInfo tokenInfo = {0};
  ScopedSSLResumptionTokenInfo token(&tokenInfo);
  client_->GetTokenInfo(token);
  ScopedCERTCertificate cert(
      PK11_FindCertFromNickname(server_->name().c_str(), nullptr));
  ASSERT_NE(nullptr, cert.get());

  CheckGetInfoResult(now(), 0, 0, cert, token);

  Handshake();
  CheckConnected();

  SendReceive();
}

TEST_P(TlsConnectGenericResumptionToken, RefuseLongWrappedMasterSecret) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  client_->SetResumptionTokenCallback();
  Connect();
  SendReceive();

  auto& token = client_->GetResumptionToken();
  ASSERT_FALSE(token.empty());

  // skip fixed-length fields at beginning
  size_t off = TokenHeaderSize(token);
  // skip peerCert (3-byte length prefix)
  ASSERT_GE(token.size(), off + 3);
  off += 3 + ((size_t(token[off]) << 16) | (size_t(token[off + 1]) << 8) |
              size_t(token[off + 2]));
  // skip peerCertStatus (2-byte length prefix)
  ASSERT_GE(token.size(), off + 2);
  off += 2 + ((size_t(token[off]) << 8) | size_t(token[off + 1]));
  // skip urlSvrName (1-byte length prefix)
  ASSERT_GE(token.size(), off + 1);
  off += 1 + size_t(token[off]);
  // skip localCert (3-byte length prefix)
  ASSERT_GE(token.size(), off + 3);
  off += 3 + ((size_t(token[off]) << 16) | (size_t(token[off + 1]) << 8) |
              size_t(token[off + 2]));
  // skip version, creationTime, authType, authKeyBits, keaType, keaKeyBits,
  // keaGroup, and sigScheme (28 bytes)
  off += 28;
  // skip cipherSuite and extendedMasterSecretUsed (3 bytes)
  off += 3;
  // skip wrapped_master_secret (48 raw bytes)
  off += 48;
  ASSERT_GE(token.size(), off + 1);
  // alter wrapped_master_secret_len to be too large
  std::vector<uint8_t> crafted(token.begin(),
                               token.begin() + static_cast<ptrdiff_t>(off));
  crafted.push_back(0xff);
  crafted.insert(crafted.end(), token.begin() + static_cast<ptrdiff_t>(off + 1),
                 token.end());

  Reset();
  StartConnect();
  ASSERT_EQ(SECFailure,
            SSL_SetResumptionToken(client_->ssl_fd(), crafted.data(),
                                   static_cast<unsigned int>(crafted.size())));
  EXPECT_EQ(SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR, PORT_GetError());
}

TEST_P(TlsConnectGenericResumptionToken, RefuseExpiredTicketClient) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  Connect();
  SendReceive();

  // Move the clock to the expiration time of the ticket.
  SSLResumptionTokenInfo tokenInfo = {0};
  ScopedSSLResumptionTokenInfo token(&tokenInfo);
  client_->GetTokenInfo(token);
  AdvanceTime(token->expirationTime - now());

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ExpectResumption(RESUME_TICKET);

  StartConnect();
  ASSERT_EQ(SECFailure,
            SSL_SetResumptionToken(client_->ssl_fd(),
                                   client_->GetResumptionToken().data(),
                                   client_->GetResumptionToken().size()));
  EXPECT_EQ(SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR, PORT_GetError());
}

TEST_P(TlsConnectGenericResumptionToken, RefuseExpiredTicketServer) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  Connect();
  SendReceive();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ExpectResumption(RESUME_NONE);

  // Start the handshake and send the ClientHello.
  StartConnect();
  ASSERT_EQ(SECSuccess,
            SSL_SetResumptionToken(client_->ssl_fd(),
                                   client_->GetResumptionToken().data(),
                                   client_->GetResumptionToken().size()));
  client_->Handshake();

  // Move the clock to the expiration time of the ticket.
  SSLResumptionTokenInfo tokenInfo = {0};
  ScopedSSLResumptionTokenInfo token(&tokenInfo);
  client_->GetTokenInfo(token);
  AdvanceTime(token->expirationTime - now());

  Handshake();
  CheckConnected();
}

TEST_P(TlsConnectGenericResumptionToken, ConnectResumeGetInfoAlpn) {
  EnableAlpn();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  Connect();
  CheckAlpn("a");
  SendReceive();

  Reset();
  EnableAlpn();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ExpectResumption(RESUME_TICKET);

  StartConnect();
  ASSERT_TRUE(client_->MaybeSetResumptionToken());

  // Get resumption token infos
  SSLResumptionTokenInfo tokenInfo = {0};
  ScopedSSLResumptionTokenInfo token(&tokenInfo);
  client_->GetTokenInfo(token);
  ScopedCERTCertificate cert(
      PK11_FindCertFromNickname(server_->name().c_str(), nullptr));
  ASSERT_NE(nullptr, cert.get());

  CheckGetInfoResult(now(), 1, 0, cert, token);

  Handshake();
  CheckConnected();
  CheckAlpn("a");

  SendReceive();
}

TEST_P(TlsConnectResumptionTokenShape, ConnectResumeTokenFormat) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  Connect();
  SendReceive();

  const std::vector<uint8_t>& token = client_->GetResumptionToken();
  ASSERT_FALSE(token.empty());

  ASSERT_GE(token.size(), 2U);
  EXPECT_EQ(static_cast<uint8_t>(SSLResumptionTokenVersion), token[0]);

  if (shape() == TokenShape::kWrapped) {
    EXPECT_EQ(0, token[1]);
    return;
  }

  // FIPS does not allow raw key export, so the token falls back to the
  // wrapped form.
  if (PK11_IsFIPS()) {
    EXPECT_EQ(0, token[1]);
    return;
  }

  ASSERT_GE(token.size(), 2U);
  EXPECT_EQ(static_cast<uint8_t>(SSLResumptionTokenVersion), token[0]);
  EXPECT_EQ(SSLResumptionTokenFeatureRawMS, token[1]);
}

TEST_P(TlsConnectTls13ResumptionToken, ConnectResumeGetInfoZeroRtt) {
  EnableAlpn();
  RolloverAntiReplay();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  server_->Set0RttEnabled(true);
  Connect();
  CheckAlpn("a");
  SendReceive();

  Reset();
  EnableAlpn();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ExpectResumption(RESUME_TICKET);

  StartConnect();
  server_->Set0RttEnabled(true);
  client_->Set0RttEnabled(true);
  ASSERT_TRUE(client_->MaybeSetResumptionToken());

  // Get resumption token infos
  SSLResumptionTokenInfo tokenInfo = {0};
  ScopedSSLResumptionTokenInfo token(&tokenInfo);
  client_->GetTokenInfo(token);
  ScopedCERTCertificate cert(
      PK11_FindCertFromNickname(server_->name().c_str(), nullptr));
  ASSERT_NE(nullptr, cert.get());
  CheckGetInfoResult(now(), 1, 1024, cert, token);

  ZeroRttSendReceive(true, true);
  Handshake();
  ExpectEarlyDataAccepted(true);
  CheckConnected();
  CheckAlpn("a");

  SendReceive();
}

// Resumption on sessions with client authentication only works with internal
// caching.
TEST_P(TlsConnectGenericResumption, ConnectResumeClientAuth) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  client_->SetupClientAuth();
  server_->RequestClientAuth(true);
  Connect();
  SendReceive();
  EXPECT_FALSE(client_->resumption_callback_called());

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  if (use_external_cache()) {
    ExpectResumption(RESUME_NONE);
  } else {
    ExpectResumption(RESUME_TICKET);
  }
  Connect();
  SendReceive();
}

// Check that resumption is blocked if the server requires client auth.
TEST_P(TlsConnectGenericResumption, ClientAuthRequiredOnResumption) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  server_->RequestClientAuth(false);
  Connect();
  SendReceive();

  Reset();
  client_->SetupClientAuth();
  server_->RequestClientAuth(true);
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ExpectResumption(RESUME_NONE);
  Connect();
  SendReceive();
}

// Check that resumption is blocked if the server requires client auth and
// the client fails to provide a certificate.
TEST_P(TlsConnectGenericResumption, ClientAuthRequiredOnResumptionNoCert) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  server_->RequestClientAuth(false);
  Connect();
  SendReceive();

  Reset();
  server_->RequestClientAuth(true);
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  // Drive handshake manually because TLS 1.3 needs it.
  StartConnect();
  client_->Handshake();  // CH
  server_->Handshake();  // SH.. (no resumption)
  client_->Handshake();  // ...
  if (version_ >= SSL_LIBRARY_VERSION_TLS_1_3) {
    // In TLS 1.3, the client thinks that everything is OK here.
    ASSERT_EQ(TlsAgent::STATE_CONNECTED, client_->state());
    ExpectAlert(server_, kTlsAlertCertificateRequired);
    server_->Handshake();  // Alert
    client_->Handshake();  // Receive Alert
    client_->CheckErrorCode(SSL_ERROR_RX_CERTIFICATE_REQUIRED_ALERT);
  } else {
    ExpectAlert(server_, kTlsAlertBadCertificate);
    server_->Handshake();  // Alert
    client_->Handshake();  // Receive Alert
    client_->CheckErrorCode(SSL_ERROR_BAD_CERT_ALERT);
  }
  server_->CheckErrorCode(SSL_ERROR_NO_CERTIFICATE);
}

TEST_F(TlsConnectStreamTls13, ExternalTokenAfterHrr) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  Connect();
  SendReceive();

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ExpectResumption(RESUME_TICKET);

  static const std::vector<SSLNamedGroup> groups = {ssl_grp_ec_secp384r1,
                                                    ssl_grp_ec_secp521r1};
  server_->ConfigNamedGroups(groups);

  StartConnect();
  ASSERT_TRUE(client_->MaybeSetResumptionToken());

  client_->Handshake();  // Send ClientHello.
  server_->Handshake();  // Process ClientHello, send HelloRetryRequest.

  auto& token = client_->GetResumptionToken();
  SECStatus rv =
      SSL_SetResumptionToken(client_->ssl_fd(), token.data(), token.size());
  ASSERT_EQ(SECFailure, rv);
  ASSERT_EQ(SEC_ERROR_INVALID_ARGS, PORT_GetError());

  Handshake();
  CheckConnected();
  SendReceive();
}

TEST_F(TlsConnectStreamTls13, ExternalTokenWithPeerId) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  EXPECT_EQ(SECSuccess, SSL_SetSockPeerID(client_->ssl_fd(), "testPeerId"));
  std::vector<uint8_t> ticket_state;
  auto cb = [](PRFileDesc* fd, const PRUint8* ticket, unsigned int ticket_len,
               void* arg) -> SECStatus {
    EXPECT_NE(0U, ticket_len);
    EXPECT_NE(nullptr, ticket);
    auto ticket_state_ = reinterpret_cast<std::vector<uint8_t>*>(arg);
    ticket_state_->assign(ticket, ticket + ticket_len);
    return SECSuccess;
  };
  EXPECT_EQ(SECSuccess, SSL_SetResumptionTokenCallback(client_->ssl_fd(), cb,
                                                       &ticket_state));

  Connect();
  SendReceive();
  EXPECT_NE(0U, ticket_state.size());

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  EXPECT_EQ(SECSuccess, SSL_SetSockPeerID(client_->ssl_fd(), "testPeerId"));
  client_->SetResumptionToken(ticket_state);
  ASSERT_TRUE(client_->MaybeSetResumptionToken());
  ExpectResumption(RESUME_TICKET);
  Connect();
  SendReceive();
}

TEST_P(TlsConnectStreamTls13TokenHeader,
       ResumptionTokenUrlSvrNameNotNullTerminated) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  client_->SetResumptionTokenCallback();
  Connect();
  SendReceive();

  const auto& token = client_->GetResumptionToken();
  ASSERT_FALSE(token.empty());

  size_t off = TokenHeaderSize(token);
  // skip peerCert (3-byte length prefix)
  ASSERT_GE(token.size(), off + 3);
  off += 3 + ((size_t(token[off]) << 16) | (size_t(token[off + 1]) << 8) |
              size_t(token[off + 2]));
  // skip peerCertStatus (2-byte length prefix)
  ASSERT_GE(token.size(), off + 2);
  off += 2 + ((size_t(token[off]) << 8) | size_t(token[off + 1]));
  // off is now the 1-byte urlSvrName length prefix.
  ASSERT_GE(token.size(), off + 1);
  size_t urlLen = token[off];
  ASSERT_GT(urlLen, 0U);

  // Overwrite urlSvrName bytes with 0x41 (no null terminator) and truncate
  // right after the urlSvrName data. Without the fix, PORT_Strdup would scan
  // past the end of the allocated buffer looking for a null byte.
  std::vector<uint8_t> crafted(
      token.begin(), token.begin() + static_cast<ptrdiff_t>(off + 1 + urlLen));
  std::fill(crafted.begin() + static_cast<ptrdiff_t>(off + 1), crafted.end(),
            0x41);

  Reset();
  StartConnect();
  EXPECT_EQ(SECFailure,
            SSL_SetResumptionToken(client_->ssl_fd(), crafted.data(),
                                   static_cast<unsigned int>(crafted.size())));
  EXPECT_EQ(SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR, PORT_GetError());
}

TEST_F(TlsConnectStreamTls13, ResumptionTokenUnknownFeatureBit) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  client_->SetResumptionTokenCallback();
  Connect();
  SendReceive();

  const auto& token = client_->GetResumptionToken();
  ASSERT_GE(token.size(), 2U);
  ASSERT_EQ(static_cast<uint8_t>(SSLResumptionTokenVersion), token[0]);

  // A bit outside the supported set changes the layout of what follows, so the
  // token has to be rejected rather than parsed.
  std::vector<uint8_t> crafted(token);
  crafted[1] = static_cast<uint8_t>(~SSLResumptionTokenSupportedFeatures);

  Reset();
  StartConnect();
  EXPECT_EQ(SECFailure,
            SSL_SetResumptionToken(client_->ssl_fd(), crafted.data(),
                                   static_cast<unsigned int>(crafted.size())));
  EXPECT_EQ(SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR, PORT_GetError());
}

TEST_F(TlsConnectStreamTls13, ResumptionTokenZeroFeatureBitfield) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  client_->SetResumptionTokenCallback();
  Connect();
  SendReceive();

  const std::vector<uint8_t> token = client_->GetResumptionToken();
  ASSERT_GE(token.size(), 2U);
  ASSERT_EQ(static_cast<uint8_t>(SSLResumptionTokenVersion), token[0]);
  ASSERT_EQ(0, token[1]);

  Reset();
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  ExpectResumption(RESUME_TICKET);
  StartConnect();
  ASSERT_EQ(SECSuccess,
            SSL_SetResumptionToken(client_->ssl_fd(), token.data(),
                                   static_cast<unsigned int>(token.size())));
  Handshake();
  CheckConnected();
  SendReceive();
}

TEST_F(TlsConnectStreamTls13, ResumptionTokenRawMasterSecretOverflow) {
  // FIPS does not allow raw key export, so no RawMS token is produced.
  if (PK11_IsFIPS()) {
    GTEST_SKIP();
  }

  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  client_->SetResumptionTokenCallback();
  EXPECT_EQ(SECSuccess,
            SSL_EnablePlaintextResumptionExport(client_->ssl_fd(), PR_TRUE));
  Connect();
  SendReceive();

  const auto& token = client_->GetResumptionToken();
  ASSERT_GE(token.size(), 2U);
  ASSERT_EQ(SSLResumptionTokenFeatureRawMS, token[1]);

  // Walk past the fixed and variable-length fields to reach the 1-byte length
  // prefix of the raw master secret in the encoded token.
  size_t off = TokenHeaderSize(token);
  // peerCert (3-byte big-endian length prefix)
  ASSERT_GE(token.size(), off + 3);
  off += 3 + ((size_t(token[off]) << 16) | (size_t(token[off + 1]) << 8) |
              size_t(token[off + 2]));
  // peerCertStatus (2-byte big-endian length prefix)
  ASSERT_GE(token.size(), off + 2);
  off += 2 + ((size_t(token[off]) << 8) | size_t(token[off + 1]));
  // urlSvrName (1-byte length prefix)
  ASSERT_GE(token.size(), off + 1);
  off += 1 + size_t(token[off]);
  // localCert (3-byte big-endian length prefix)
  ASSERT_GE(token.size(), off + 3);
  off += 3 + ((size_t(token[off]) << 16) | (size_t(token[off + 1]) << 8) |
              size_t(token[off + 2]));
  // Fixed: version(2) + creationTime(8) + authType(2) + authKeyBits(4) +
  //        keaType(2) + keaKeyBits(4) + keaGroup(3) + sigScheme(3) +
  //        cipherSuite(2) + extendedMasterSecretUsed(1) = 31 bytes
  off += 31;
  // off now points to the 1-byte length prefix of the raw master secret.
  ASSERT_GE(token.size(), off + 1);
  size_t msLen = token[off];
  ASSERT_GT(msLen, 0U);
  ASSERT_LE(msLen, static_cast<size_t>(SSL3_MASTER_SECRET_LENGTH));
  ASSERT_GE(token.size(), off + 1 + msLen);

  // Grow the master secret to 0xff bytes (255 > SSL3_MASTER_SECRET_LENGTH),
  // padding it in place so the fields after it still parse.
  std::vector<uint8_t> crafted(token.begin(),
                               token.begin() + static_cast<ptrdiff_t>(off));
  crafted.push_back(0xff);
  crafted.insert(crafted.end(), token.begin() + static_cast<ptrdiff_t>(off + 1),
                 token.begin() + static_cast<ptrdiff_t>(off + 1 + msLen));
  crafted.resize(crafted.size() + (0xff - msLen), 0);
  crafted.insert(crafted.end(),
                 token.begin() + static_cast<ptrdiff_t>(off + 1 + msLen),
                 token.end());

  SSLResumptionTokenInfo info;
  EXPECT_EQ(SECFailure,
            SSL_GetResumptionTokenInfo(
                crafted.data(), static_cast<unsigned int>(crafted.size()),
                &info, sizeof(info)));
  EXPECT_EQ(SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR, PORT_GetError());

  Reset();
  StartConnect();
  EXPECT_EQ(SECFailure,
            SSL_SetResumptionToken(client_->ssl_fd(), crafted.data(),
                                   static_cast<unsigned int>(crafted.size())));
  EXPECT_EQ(SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR, PORT_GetError());
}

// A token carrying the master secret in cleartext can't be imported in FIPS
// mode. FIPS mode never produces such a token, so build one from a wrapped
// token by replacing the wrapped secret fields with a raw secret.
TEST_F(TlsConnectStreamTls13, ResumptionTokenRawMasterSecretFips) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  client_->SetResumptionTokenCallback();
  Connect();
  SendReceive();

  const auto& token = client_->GetResumptionToken();
  ASSERT_GE(token.size(), 2U);
  ASSERT_EQ(0, token[1]);

  size_t off = TokenHeaderSize(token);
  // peerCert (3-byte big-endian length prefix)
  ASSERT_GE(token.size(), off + 3);
  off += 3 + ((size_t(token[off]) << 16) | (size_t(token[off + 1]) << 8) |
              size_t(token[off + 2]));
  // peerCertStatus (2-byte big-endian length prefix)
  ASSERT_GE(token.size(), off + 2);
  off += 2 + ((size_t(token[off]) << 8) | size_t(token[off + 1]));
  // urlSvrName (1-byte length prefix)
  ASSERT_GE(token.size(), off + 1);
  off += 1 + size_t(token[off]);
  // localCert (3-byte big-endian length prefix)
  ASSERT_GE(token.size(), off + 3);
  off += 3 + ((size_t(token[off]) << 16) | (size_t(token[off + 1]) << 8) |
              size_t(token[off + 2]));
  // Fixed: version(2) + creationTime(8) + authType(2) + authKeyBits(4) +
  //        keaType(2) + keaKeyBits(4) + keaGroup(3) + sigScheme(3) +
  //        cipherSuite(2) + extendedMasterSecretUsed(1) = 31 bytes
  off += 31;
  // off now points to the wrapped secret fields: wrapped_master_secret(48),
  // wrapped_master_secret_len(1), masterWrapMech(8), masterModuleID(8),
  // masterSlotID(8), masterWrapIndex(4), masterWrapSeries(2) = 79 bytes
  const size_t wrappedFieldsLen = 79;
  ASSERT_GE(token.size(), off + wrappedFieldsLen);
  size_t msLen = token[off + 48];
  ASSERT_GT(msLen, 0U);
  ASSERT_LE(msLen, static_cast<size_t>(SSL3_MASTER_SECRET_LENGTH));

  std::vector<uint8_t> crafted(token.begin(),
                               token.begin() + static_cast<ptrdiff_t>(off));
  crafted[1] = SSLResumptionTokenFeatureRawMS;
  crafted.push_back(static_cast<uint8_t>(msLen));
  crafted.insert(crafted.end(), msLen, 0x5a);
  crafted.insert(crafted.end(),
                 token.begin() + static_cast<ptrdiff_t>(off + wrappedFieldsLen),
                 token.end());

  Reset();
  StartConnect();
  SECStatus rv =
      SSL_SetResumptionToken(client_->ssl_fd(), crafted.data(),
                             static_cast<unsigned int>(crafted.size()));
  if (PK11_IsFIPS()) {
    EXPECT_EQ(SECFailure, rv);
    EXPECT_EQ(SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR, PORT_GetError());
  } else {
    EXPECT_EQ(SECSuccess, rv);
  }
}

// Every proper prefix of a valid token has to be rejected, whichever field it
// ends in.
TEST_P(TlsConnectStreamTls13TokenHeader, ResumptionTokenTruncated) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  Connect();
  SendReceive();

  const std::vector<uint8_t> token = client_->GetResumptionToken();
  ASSERT_GE(token.size(), 2U);

  for (size_t len = 1; len < token.size(); ++len) {
    SSLResumptionTokenInfo info;
    EXPECT_EQ(SECFailure, SSL_GetResumptionTokenInfo(
                              token.data(), static_cast<unsigned int>(len),
                              &info, sizeof(info)))
        << "prefix of length " << len << " was accepted";
  }

  Reset();
  StartConnect();
  EXPECT_EQ(SECFailure, SSL_SetResumptionToken(
                            client_->ssl_fd(), token.data(),
                            static_cast<unsigned int>(token.size() - 1)));
  EXPECT_EQ(SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR, PORT_GetError());
}

TEST_F(TlsConnectStreamTls13, ResumptionTokenEmptyPeerCert) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  client_->SetResumptionTokenCallback();
  Connect();
  SendReceive();

  const std::vector<uint8_t> token = client_->GetResumptionToken();
  size_t off = TokenHeaderSize(token);
  // peerCert (3-byte big-endian length prefix)
  ASSERT_GE(token.size(), off + 3);
  size_t certLen = (size_t(token[off]) << 16) | (size_t(token[off + 1]) << 8) |
                   size_t(token[off + 2]);
  ASSERT_GT(certLen, 0U);
  ASSERT_GE(token.size(), off + 3 + certLen);

  std::vector<uint8_t> crafted(token.begin(),
                               token.begin() + static_cast<ptrdiff_t>(off));
  crafted.insert(crafted.end(), 3, 0);
  crafted.insert(crafted.end(),
                 token.begin() + static_cast<ptrdiff_t>(off + 3 + certLen),
                 token.end());

  SSLResumptionTokenInfo info;
  EXPECT_EQ(SECFailure,
            SSL_GetResumptionTokenInfo(
                crafted.data(), static_cast<unsigned int>(crafted.size()),
                &info, sizeof(info)));
}

TEST_F(TlsConnectStreamTls13, ResumptionTokenUnknownVersion) {
  ConfigureSessionCache(RESUME_BOTH, RESUME_BOTH);
  client_->SetResumptionTokenCallback();
  Connect();
  SendReceive();

  const auto& token = client_->GetResumptionToken();
  ASSERT_FALSE(token.empty());

  std::vector<uint8_t> crafted(token);
  crafted[0] = SSLResumptionTokenVersion + 1;

  Reset();
  StartConnect();
  EXPECT_EQ(SECFailure,
            SSL_SetResumptionToken(client_->ssl_fd(), crafted.data(),
                                   static_cast<unsigned int>(crafted.size())));
  EXPECT_EQ(SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR, PORT_GetError());
}

}  // namespace nss_test
