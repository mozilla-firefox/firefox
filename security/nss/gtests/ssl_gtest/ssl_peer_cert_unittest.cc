/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "cert.h"
#include "keyhi.h"
#include "secerr.h"
#include "ssl.h"
#include "sslimpl.h"

#include "gtest_utils.h"
#include "nss_scoped_ptrs.h"
#include "tls_connect.h"

namespace nss_test {

static void CheckSPKIMatches(CERTCertificate* cert,
                             CERTSubjectPublicKeyInfo* spki) {
  ScopedSECKEYPublicKey expectedKey(CERT_ExtractPublicKey(cert));
  ASSERT_TRUE(expectedKey);
  ScopedSECItem expected(
      SECKEY_EncodeDERSubjectPublicKeyInfo(expectedKey.get()));
  ASSERT_TRUE(expected);

  ScopedSECKEYPublicKey key(SECKEY_ExtractPublicKey(spki));
  ASSERT_TRUE(key);
  ScopedSECItem actual(SECKEY_EncodeDERSubjectPublicKeyInfo(key.get()));
  ASSERT_TRUE(actual);
  EXPECT_TRUE(SECITEM_ItemsAreEqual(expected.get(), actual.get()));
}

TEST(SslExtractSPKIFromDER, ExtractsOwnedSPKI) {
  ScopedCERTCertificate cert;
  ScopedSECKEYPrivateKey priv;
  ASSERT_TRUE(TlsAgent::LoadCertificate(TlsAgent::kServerRsa, &cert, &priv));

  ScopedSECItem input(SECITEM_DupItem(&cert->derCert));
  ASSERT_TRUE(input);
  ScopedCERTSubjectPublicKeyInfo spki(ssl_ExtractSPKIFromDER(input.get()));
  ASSERT_TRUE(spki);

  // The decoded SPKI must own its data independently of the input certificate.
  PORT_Memset(input->data, 0, input->len);
  input.reset();

  CheckSPKIMatches(cert.get(), spki.get());
}

TEST(SslExtractSPKIFromDER, BadInput) {
  EXPECT_EQ(nullptr, ssl_ExtractSPKIFromDER(nullptr));
  EXPECT_EQ(SEC_ERROR_INVALID_ARGS, PORT_GetError());

  SECItem missingData = {siBuffer, nullptr, 1};
  EXPECT_EQ(nullptr, ssl_ExtractSPKIFromDER(&missingData));
  EXPECT_EQ(SEC_ERROR_INVALID_ARGS, PORT_GetError());

  unsigned char data = 0;
  SECItem empty = {siBuffer, &data, 0};
  EXPECT_EQ(nullptr, ssl_ExtractSPKIFromDER(&empty));
  EXPECT_EQ(SEC_ERROR_INVALID_ARGS, PORT_GetError());

  // An empty SEQUENCE is not a certificate.
  unsigned char emptySequence[] = {0x30, 0x00};
  SECItem notACert = {siBuffer, emptySequence, sizeof(emptySequence)};
  EXPECT_EQ(nullptr, ssl_ExtractSPKIFromDER(&notACert));
  EXPECT_EQ(SEC_ERROR_BAD_DER, PORT_GetError());
}

TEST(SslExtractSPKIFromDER, UnknownExtension) {
  ScopedCERTCertificate cert;
  ScopedSECKEYPrivateKey priv;
  ASSERT_TRUE(
      TlsAgent::LoadCertificate(TlsAgent::kServerUnknownExt, &cert, &priv));

  ScopedCERTSubjectPublicKeyInfo spki(ssl_ExtractSPKIFromDER(&cert->derCert));
  ASSERT_TRUE(spki);
  CheckSPKIMatches(cert.get(), spki.get());
}

// A server certificate with an unknown critical extension is rejected when the
// client parses it, before the auth certificate hook runs, so an application
// can't override the failure.
TEST_P(TlsConnectGeneric, UnknownCriticalExtensionInServerCert) {
  Reset(TlsAgent::kServerUnknownCriticalExt);

  bool hookCalled = false;
  client_->SetAuthCertificateCallback(
      [&hookCalled](TlsAgent*, bool, bool) -> SECStatus {
        hookCalled = true;
        return SECSuccess;
      });

  ConnectExpectAlert(client_, kTlsAlertBadCertificate);
  client_->CheckErrorCode(SEC_ERROR_UNKNOWN_CRITICAL_EXTENSION);
  server_->CheckErrorCode(SSL_ERROR_BAD_CERT_ALERT);
  EXPECT_FALSE(hookCalled);
}

}  // namespace nss_test
