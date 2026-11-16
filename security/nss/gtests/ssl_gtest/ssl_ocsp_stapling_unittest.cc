/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string>

#include "cert.h"
#include "ocsp.h"
#include "secerr.h"
#include "ssl.h"

#include "nss_scoped_ptrs.h"
#include "tls_connect.h"

namespace nss_test {

static const std::string kStaplingCa = "rsa_ca";
static const std::string kStaplingServer = "rsa_chain_kex";

static char* UnreachableOCSPResponder(CERTCertificate*) {
  return PORT_Strdup("http://ocsp.invalid/");
}

static SECStatus FailToCreateHttpSession(const char*, PRUint16,
                                         SEC_HTTP_SERVER_SESSION*) {
  return SECFailure;
}

static const SEC_HttpClientFcn kFailingHttpClient = {
    1,
    {{FailToCreateHttpSession, nullptr, nullptr, nullptr, nullptr, nullptr,
      nullptr, nullptr, nullptr}}};

// Verifies the server certificate with SSL_AuthCertificate, which caches the
// stapled response with CERT_CacheOCSPResponseFromSideChannel and checks
// revocation with CERT_CheckOCSPStatus. The server certificate is given an
// OCSP responder that can't be reached, so verification fails unless the
// stapled response is used.
//
// N.B.: libpkix is disabled.
class TlsStapledOCSPAuthCertificateTest : public TlsConnectStreamTls13 {
 protected:
  enum class Staple {
    kNone,
    kGood,
    kRevoked,
    kUnknown,
    kTryLater,
    kBadSignature,
    kCorrupted
  };

  void SetUp() override {
    TlsConnectStreamTls13::SetUp();
    ca_.reset(
        CERT_FindCertByNickname(CERT_GetDefaultCertDB(), kStaplingCa.c_str()));
    ASSERT_TRUE(ca_);
    server_cert_.reset(CERT_FindCertByNickname(CERT_GetDefaultCertDB(),
                                               kStaplingServer.c_str()));
    ASSERT_TRUE(server_cert_);
    arena_.reset(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
    ASSERT_TRUE(arena_);
    if (CERT_GetUsePKIXForValidation()) {
      ASSERT_EQ(SECSuccess, CERT_SetUsePKIXForValidation(PR_FALSE));
      pkix_validation_disabled_ = true;
    }
    ASSERT_EQ(SECSuccess, CERT_RegisterAlternateOCSPAIAInfoCallBack(
                              UnreachableOCSPResponder, &previous_aia_fcn_));
    aia_fcn_registered_ = true;
    previous_http_client_ = SEC_GetRegisteredHttpClient();
    ASSERT_EQ(SECSuccess, SEC_RegisterDefaultHttpClient(&kFailingHttpClient));
    http_client_registered_ = true;
    ASSERT_EQ(SECSuccess, CERT_ClearOCSPCache());
    ASSERT_EQ(SECSuccess, CERT_EnableOCSPChecking(CERT_GetDefaultCertDB()));
    ocsp_enabled_ = true;
  }

  void TearDown() override {
    if (ocsp_enabled_) {
      EXPECT_EQ(SECSuccess, CERT_DisableOCSPChecking(CERT_GetDefaultCertDB()));
    }
    if (http_client_registered_) {
      EXPECT_EQ(SECSuccess,
                SEC_RegisterDefaultHttpClient(previous_http_client_));
    }
    if (aia_fcn_registered_) {
      EXPECT_EQ(SECSuccess, CERT_RegisterAlternateOCSPAIAInfoCallBack(
                                previous_aia_fcn_, nullptr));
    }
    if (pkix_validation_disabled_) {
      EXPECT_EQ(SECSuccess, CERT_SetUsePKIXForValidation(PR_TRUE));
    }
    TlsConnectStreamTls13::TearDown();
  }

  // |expected| is the error that SSL_AuthCertificate fails with and |alert| is
  // the alert the client then sends.
  void ConnectAndVerify(Staple staple, PRErrorCode expected = 0,
                        uint8_t alert = 0) {
    SECItem* response = CreateResponse(staple);
    ASSERT_EQ(staple == Staple::kNone, response == nullptr);

    Reset(kStaplingServer);
    EnsureTlsSetup();
    client_->SetOption(SSL_ENABLE_OCSP_STAPLING, PR_TRUE);
    EXPECT_EQ(SECSuccess,
              SSL_SetURL(client_->ssl_fd(), kStaplingServer.c_str()));

    SECItemArray responses = {response, 1};
    const SSLExtraServerCertData extraData = {
        ssl_auth_null, nullptr, &responses, nullptr, nullptr, nullptr};
    if (response) {
      EXPECT_TRUE(server_->ConfigServerCert(kStaplingServer, true, &extraData));
    }

    client_->SetAuthCertificateCallback(
        [response](TlsAgent* agent, bool checksig, bool isServer) -> SECStatus {
          const SECItemArray* stapled =
              SSL_PeerStapledOCSPResponses(agent->ssl_fd());
          EXPECT_EQ(response ? 1U : 0U, stapled->len);
          if (response && stapled->len == 1) {
            EXPECT_EQ(0, SECITEM_CompareItem(response, &stapled->items[0]));
          }
          return SSL_AuthCertificate(CERT_GetDefaultCertDB(), agent->ssl_fd(),
                                     checksig, isServer);
        });

    if (!expected) {
      Connect();
      return;
    }
    ConnectExpectAlert(client_, alert);
    client_->CheckErrorCode(expected);
  }

 private:
  SECItem* CreateResponse(Staple staple) {
    if (staple == Staple::kNone) {
      return nullptr;
    }

    if (staple == Staple::kTryLater) {
      return CERT_CreateEncodedOCSPErrorResponse(
          arena_.get(), SEC_ERROR_OCSP_TRY_SERVER_LATER);
    }
    if (staple == Staple::kCorrupted) {
      SECItem* response = SECITEM_AllocItem(arena_.get(), nullptr, 1);
      if (response) {
        response->data[0] = 0;
      }
      return response;
    }

    constexpr PRTime kOneDay = PRTime(24) * 60 * 60 * PR_USEC_PER_SEC;
    PRTime now = PR_Now();
    PRTime nextUpdate = now + kOneDay;
    CERTOCSPCertID* certID = CERT_CreateOCSPCertID(server_cert_.get(), now);
    EXPECT_NE(nullptr, certID);
    if (!certID) {
      return nullptr;
    }

    CERTOCSPSingleResponse* single;
    if (staple == Staple::kRevoked) {
      single = CERT_CreateOCSPSingleResponseRevoked(
          arena_.get(), certID, now, &nextUpdate, now - kOneDay, nullptr);
    } else if (staple == Staple::kUnknown) {
      single = CERT_CreateOCSPSingleResponseUnknown(arena_.get(), certID, now,
                                                    &nextUpdate);
    } else {
      single = CERT_CreateOCSPSingleResponseGood(arena_.get(), certID, now,
                                                 &nextUpdate);
    }
    EXPECT_NE(nullptr, single);

    SECItem* response = nullptr;
    if (single) {
      CERTOCSPSingleResponse* responses[] = {single, nullptr};
      response = CERT_CreateEncodedOCSPSuccessResponse(arena_.get(), ca_.get(),
                                                       ocspResponderID_byName,
                                                       now, responses, nullptr);
      EXPECT_NE(nullptr, response);
    }
    if (response && staple == Staple::kBadSignature) {
      response->data[response->len - 1] ^= 1;
    }
    CERT_DestroyOCSPCertID(certID);
    return response;
  }

  CERT_StringFromCertFcn previous_aia_fcn_ = nullptr;
  const SEC_HttpClientFcn* previous_http_client_ = nullptr;
  bool aia_fcn_registered_ = false;
  bool http_client_registered_ = false;
  bool ocsp_enabled_ = false;
  bool pkix_validation_disabled_ = false;
  ScopedCERTCertificate ca_;
  ScopedCERTCertificate server_cert_;
  ScopedPLArenaPool arena_;
};

// OCSP stapling, signed response, good status
TEST_F(TlsStapledOCSPAuthCertificateTest, Good) {
  ConnectAndVerify(Staple::kGood);
}

// OCSP stapling, signed response, revoked status
TEST_F(TlsStapledOCSPAuthCertificateTest, Revoked) {
  ConnectAndVerify(Staple::kRevoked, SEC_ERROR_REVOKED_CERTIFICATE,
                   kTlsAlertCertificateRevoked);
}

// OCSP stapling, signed response, unknown status
TEST_F(TlsStapledOCSPAuthCertificateTest, Unknown) {
  ConnectAndVerify(Staple::kUnknown, SEC_ERROR_OCSP_SERVER_ERROR,
                   kTlsAlertBadCertificate);
}

// OCSP stapling, unsigned failure response
TEST_F(TlsStapledOCSPAuthCertificateTest, TryLater) {
  ConnectAndVerify(Staple::kTryLater, SEC_ERROR_OCSP_SERVER_ERROR,
                   kTlsAlertBadCertificate);
}

// OCSP stapling, good status, bad signature
TEST_F(TlsStapledOCSPAuthCertificateTest, BadSignature) {
  ConnectAndVerify(Staple::kBadSignature, SEC_ERROR_OCSP_SERVER_ERROR,
                   kTlsAlertBadCertificate);
}

// OCSP stapling, invalid cert status data
TEST_F(TlsStapledOCSPAuthCertificateTest, Corrupted) {
  ConnectAndVerify(Staple::kCorrupted, SEC_ERROR_OCSP_SERVER_ERROR,
                   kTlsAlertBadCertificate);
}

// Valid cert, Server doesn't staple
TEST_F(TlsStapledOCSPAuthCertificateTest, NotStapled) {
  ConnectAndVerify(Staple::kNone, SEC_ERROR_OCSP_SERVER_ERROR,
                   kTlsAlertBadCertificate);
}

}  // namespace nss_test
