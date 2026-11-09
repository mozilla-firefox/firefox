/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "tls_agent.h"
#include "databuffer.h"
#include "keyhi.h"
#include "pk11func.h"
#include "ssl.h"
#include "sslerr.h"
#include "sslexp.h"
#include "sslproto.h"
#include "tls_filter.h"
#include "tls_parser.h"

extern "C" {
// This is not something that should make you happy.
#include "libssl_internals.h"
}

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"
#include "nss_scoped_ptrs.h"

extern std::string g_working_dir_path;

namespace nss_test {

const char* TlsAgent::states[] = {"INIT", "CONNECTING", "CONNECTED", "ERROR"};

const std::string TlsAgent::kClient = "client";    // both sign and encrypt
const std::string TlsAgent::kRsa2048 = "rsa2048";  // bigger
const std::string TlsAgent::kRsa8192 = "rsa8192";  // biggest allowed
const std::string TlsAgent::kServerRsa = "rsa";    // both sign and encrypt
const std::string TlsAgent::kServerRsaSign = "rsa_sign";
const std::string TlsAgent::kServerRsaPss = "rsa_pss";
const std::string TlsAgent::kServerRsaDecrypt = "rsa_decrypt";
const std::string TlsAgent::kServerEcdsa256 = "ecdsa256";
const std::string TlsAgent::kServerEcdsa384 = "ecdsa384";
const std::string TlsAgent::kServerEcdsa521 = "ecdsa521";
const std::string TlsAgent::kServerEcdhRsa = "ecdh_rsa";
const std::string TlsAgent::kServerEcdhEcdsa = "ecdh_ecdsa";
const std::string TlsAgent::kServerDsa = "dsa";
const std::string TlsAgent::kServerMlDsa44 = "mldsa44";
const std::string TlsAgent::kServerMlDsa65 = "mldsa65";
const std::string TlsAgent::kServerMlDsa87 = "mldsa87";
const std::string TlsAgent::kServerUnknownExt = "unknown_ext";
const std::string TlsAgent::kServerUnknownCriticalExt = "unknown_critical_ext";
const std::string TlsAgent::kDelegatorEcdsa256 = "delegator_ecdsa256";
const std::string TlsAgent::kDelegatorRsae2048 = "delegator_rsae2048";
const std::string TlsAgent::kDelegatorRsaPss2048 = "delegator_rsa_pss2048";
const std::string TlsAgent::kDelegatorMlDsa44 = "delegator_mldsa44";
const std::string TlsAgent::kDelegatorMlDsa65 = "delegator_mldsa65";
const std::string TlsAgent::kDelegatorMlDsa87 = "delegator_mldsa87";

static const uint8_t kCannedTls13ServerHello[] = {
    0x03, 0x03, 0x9c, 0xbc, 0x14, 0x9b, 0x0e, 0x2e, 0xfa, 0x0d, 0xf3,
    0xf0, 0x5c, 0x70, 0x7a, 0xe0, 0xd1, 0x9b, 0x3e, 0x5a, 0x44, 0x6b,
    0xdf, 0xe5, 0xc2, 0x28, 0x64, 0xf7, 0x00, 0xc1, 0x9c, 0x08, 0x76,
    0x08, 0x00, 0x13, 0x01, 0x00, 0x00, 0x2e, 0x00, 0x33, 0x00, 0x24,
    0x00, 0x1d, 0x00, 0x20, 0xc2, 0xcf, 0x23, 0x17, 0x64, 0x23, 0x03,
    0xf0, 0xfb, 0x45, 0x98, 0x26, 0xd1, 0x65, 0x24, 0xa1, 0x6c, 0xa9,
    0x80, 0x8f, 0x2c, 0xac, 0x0a, 0xea, 0x53, 0x3a, 0xcb, 0xe3, 0x08,
    0x84, 0xae, 0x19, 0x00, 0x2b, 0x00, 0x02, 0x03, 0x04};

TlsAgent::TlsAgent(const std::string& nm, Role rl, SSLProtocolVariant var)
    : name_(nm),
      variant_(var),
      role_(rl),
      server_key_bits_(0),
      adapter_(new DummyPrSocket(role_str(), var)),
      ssl_fd_(nullptr),
      state_(STATE_INIT),
      timer_handle_(nullptr),
      falsestart_enabled_(false),
      expected_version_(0),
      expected_cipher_suite_(0),
      expect_client_auth_(false),
      expect_ech_(false),
      expect_psk_(ssl_psk_none),
      can_falsestart_hook_called_(false),
      sni_hook_called_(false),
      auth_certificate_hook_called_(false),
      expected_received_alert_(kTlsAlertCloseNotify),
      expected_received_alert_level_(kTlsAlertWarning),
      expected_sent_alert_(kTlsAlertCloseNotify),
      expected_sent_alert_level_(kTlsAlertWarning),
      handshake_callback_called_(false),
      resumption_callback_called_(false),
      error_code_(0),
      send_ctr_(0),
      recv_ctr_(0),
      expect_readwrite_error_(false),
      handshake_callback_(),
      auth_certificate_callback_(),
      sni_callback_(),
      skip_version_checks_(false),
      resumption_token_(),
      policy_() {
  memset(&info_, 0, sizeof(info_));
  memset(&csinfo_, 0, sizeof(csinfo_));
  SECStatus rv = SSL_VersionRangeGetDefault(variant_, &vrange_);
  EXPECT_EQ(SECSuccess, rv);
}

TlsAgent::~TlsAgent() {
  if (timer_handle_) {
    timer_handle_->Cancel();
  }

  if (adapter_) {
    Poller::Instance()->Cancel(READABLE_EVENT, adapter_);
  }

  // Add failures manually, if any, so we don't throw in a destructor.
  if (expected_received_alert_ != kTlsAlertCloseNotify ||
      expected_received_alert_level_ != kTlsAlertWarning) {
    ADD_FAILURE() << "Wrong expected_received_alert status: " << role_str();
  }
  if (expected_sent_alert_ != kTlsAlertCloseNotify ||
      expected_sent_alert_level_ != kTlsAlertWarning) {
    ADD_FAILURE() << "Wrong expected_sent_alert status: " << role_str();
  }
}

void TlsAgent::SetState(State s) {
  if (state_ == s) return;

  LOG("Changing state from " << state_ << " to " << s);
  state_ = s;
}

/*static*/ bool TlsAgent::LoadCertificate(const std::string& name,
                                          ScopedCERTCertificate* cert,
                                          ScopedSECKEYPrivateKey* priv) {
  cert->reset(PK11_FindCertFromNickname(name.c_str(), nullptr));
  EXPECT_NE(nullptr, cert);
  if (!cert) return false;
  EXPECT_NE(nullptr, cert->get());
  if (!cert->get()) return false;

  priv->reset(PK11_FindKeyByAnyCert(cert->get(), nullptr));
  EXPECT_NE(nullptr, priv);
  if (!priv) return false;
  EXPECT_NE(nullptr, priv->get());
  if (!priv->get()) return false;

  return true;
}

// Loads a key pair from the certificate identified by |id|.
/*static*/ bool TlsAgent::LoadKeyPairFromCert(const std::string& name,
                                              ScopedSECKEYPublicKey* pub,
                                              ScopedSECKEYPrivateKey* priv) {
  ScopedCERTCertificate cert;
  if (!TlsAgent::LoadCertificate(name, &cert, priv)) {
    return false;
  }

  pub->reset(SECKEY_ExtractPublicKey(&cert->subjectPublicKeyInfo));
  if (!pub->get()) {
    return false;
  }

  return true;
}

void TlsAgent::DelegateCredential(const std::string& name,
                                  const ScopedSECKEYPublicKey& dc_pub,
                                  SSLSignatureScheme dc_cert_verify_alg,
                                  PRUint32 dc_valid_for, PRTime now,
                                  SECItem* dc) {
  ScopedCERTCertificate cert;
  ScopedSECKEYPrivateKey cert_priv;
  EXPECT_TRUE(TlsAgent::LoadCertificate(name, &cert, &cert_priv))
      << "Could not load delegate certificate: " << name
      << "; test db corrupt?";

  EXPECT_EQ(SECSuccess,
            SSL_DelegateCredential(cert.get(), cert_priv.get(), dc_pub.get(),
                                   dc_cert_verify_alg, dc_valid_for, now, dc));
}

void TlsAgent::EnableDelegatedCredentials() {
  ASSERT_TRUE(EnsureTlsSetup());
  SetOption(SSL_ENABLE_DELEGATED_CREDENTIALS, PR_TRUE);
}

void TlsAgent::AddDelegatedCredential(const std::string& dc_name,
                                      SSLSignatureScheme dc_cert_verify_alg,
                                      PRUint32 dc_valid_for, PRTime now) {
  ASSERT_TRUE(EnsureTlsSetup());

  ScopedSECKEYPublicKey pub;
  ScopedSECKEYPrivateKey priv;
  EXPECT_TRUE(TlsAgent::LoadKeyPairFromCert(dc_name, &pub, &priv));

  StackSECItem dc;
  TlsAgent::DelegateCredential(name_, pub, dc_cert_verify_alg, dc_valid_for,
                               now, &dc);

  SSLExtraServerCertData extra_data = {ssl_auth_null, nullptr, nullptr,
                                       nullptr,       &dc,     priv.get()};
  EXPECT_TRUE(ConfigServerCert(name_, true, &extra_data));
}

bool TlsAgent::ConfigServerCert(const std::string& id, bool updateKeyBits,
                                const SSLExtraServerCertData* serverCertData) {
  ScopedCERTCertificate cert;
  ScopedSECKEYPrivateKey priv;
  if (!TlsAgent::LoadCertificate(id, &cert, &priv)) {
    return false;
  }

  if (updateKeyBits) {
    ScopedSECKEYPublicKey pub(CERT_ExtractPublicKey(cert.get()));
    EXPECT_NE(nullptr, pub.get());
    if (!pub.get()) return false;
    server_key_bits_ = SECKEY_PublicKeyStrengthInBits(pub.get());
  }

  SECStatus rv =
      SSL_ConfigSecureServer(ssl_fd(), nullptr, nullptr, ssl_kea_null);
  EXPECT_EQ(SECFailure, rv);
  rv = SSL_ConfigServerCert(ssl_fd(), cert.get(), priv.get(), serverCertData,
                            serverCertData ? sizeof(*serverCertData) : 0);
  return rv == SECSuccess;
}

bool TlsAgent::EnsureTlsSetup(PRFileDesc* modelSocket) {
  // Don't set up twice
  if (ssl_fd_) return true;
  NssManagePolicy policyManage(policy_, option_);

  ScopedPRFileDesc dummy_fd(adapter_->CreateFD());
  EXPECT_NE(nullptr, dummy_fd);
  if (!dummy_fd) {
    return false;
  }
  if (adapter_->variant() == ssl_variant_stream) {
    ssl_fd_.reset(SSL_ImportFD(modelSocket, dummy_fd.get()));
  } else {
    ssl_fd_.reset(DTLS_ImportFD(modelSocket, dummy_fd.get()));
  }

  EXPECT_NE(nullptr, ssl_fd_);
  if (!ssl_fd_) {
    return false;
  }
  dummy_fd.release();  // Now subsumed by ssl_fd_.

  SECStatus rv;
  if (!skip_version_checks_) {
    rv = SSL_VersionRangeSet(ssl_fd(), &vrange_);
    EXPECT_EQ(SECSuccess, rv);
    if (rv != SECSuccess) return false;
  }

  ScopedCERTCertList anchors(CERT_NewCertList());
  rv = SSL_SetTrustAnchors(ssl_fd(), anchors.get());
  if (rv != SECSuccess) return false;

  if (role_ == SERVER) {
    EXPECT_TRUE(ConfigServerCert(name_, true));

    rv = SSL_SNISocketConfigHook(ssl_fd(), SniHook, this);
    EXPECT_EQ(SECSuccess, rv);
    if (rv != SECSuccess) return false;

    rv = SSL_SetMaxEarlyDataSize(ssl_fd(), 1024);
    EXPECT_EQ(SECSuccess, rv);
    if (rv != SECSuccess) return false;
  } else {
    rv = SSL_SetURL(ssl_fd(), "server");
    EXPECT_EQ(SECSuccess, rv);
    if (rv != SECSuccess) return false;
  }

  rv = SSL_AuthCertificateHook(ssl_fd(), AuthCertificateHook, this);
  EXPECT_EQ(SECSuccess, rv);
  if (rv != SECSuccess) return false;

  rv = SSL_AlertReceivedCallback(ssl_fd(), AlertReceivedCallback, this);
  EXPECT_EQ(SECSuccess, rv);
  if (rv != SECSuccess) return false;

  rv = SSL_AlertSentCallback(ssl_fd(), AlertSentCallback, this);
  EXPECT_EQ(SECSuccess, rv);
  if (rv != SECSuccess) return false;

  rv = SSL_HandshakeCallback(ssl_fd(), HandshakeCallback, this);
  EXPECT_EQ(SECSuccess, rv);
  if (rv != SECSuccess) return false;

  // All these tests depend on having this disabled to start with.
  SetOption(SSL_ENABLE_EXTENDED_MASTER_SECRET, PR_FALSE);

  return true;
}

bool TlsAgent::MaybeSetResumptionToken() {
  if (!resumption_token_.empty()) {
    LOG("setting external resumption token");
    SECStatus rv = SSL_SetResumptionToken(ssl_fd(), resumption_token_.data(),
                                          resumption_token_.size());

    // rv is SECFailure with error set to SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR
    // if the resumption token was bad (expired/malformed/etc.).
    if (expect_psk_ == ssl_psk_resume) {
      // Only in case we expect resumption this has to be successful. We might
      // not expect resumption due to some reason but the token is totally fine.
      EXPECT_EQ(SECSuccess, rv);
    }
    if (rv != SECSuccess) {
      EXPECT_EQ(SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR, PORT_GetError());
      resumption_token_.clear();
      EXPECT_FALSE(expect_psk_ == ssl_psk_resume);
      if (expect_psk_ == ssl_psk_resume) return false;
    }
  }

  return true;
}

void TlsAgent::SetAntiReplayContext(ScopedSSLAntiReplayContext& ctx) {
  EXPECT_EQ(SECSuccess, SSL_SetAntiReplayContext(ssl_fd(), ctx.get()));
}

// Defaults to a Sync callback returning success
void TlsAgent::SetupClientAuth(ClientAuthCallbackType callbackType,
                               bool callbackSuccess) {
  EXPECT_TRUE(EnsureTlsSetup());
  ASSERT_EQ(CLIENT, role_);

  client_auth_callback_type_ = callbackType;
  client_auth_callback_success_ = callbackSuccess;

  if (callbackType == ClientAuthCallbackType::kNone && !callbackSuccess) {
    // Don't set a callback for this case.
    return;
  }
  EXPECT_EQ(SECSuccess,
            SSL_GetClientAuthDataHook(ssl_fd(), GetClientAuthDataHook,
                                      reinterpret_cast<void*>(this)));
}

void CheckCertReqAgainstDefaultCAs(const CERTDistNames* caNames) {
  ScopedCERTDistNames expected(CERT_GetSSLCACerts(nullptr));

  ASSERT_EQ(expected->nnames, caNames->nnames);

  for (size_t i = 0; i < static_cast<size_t>(expected->nnames); ++i) {
    EXPECT_EQ(SECEqual,
              SECITEM_CompareItem(&(expected->names[i]), &(caNames->names[i])));
  }
}

// Complete processing of Client Certificate Selection
// A No-op if the agent is using synchronous client cert selection.
// Otherwise, calls SSL_ClientCertCallbackComplete.
// kAsyncDelay triggers a call to SSL_ForceHandshake prior to completion to
// ensure that the socket is correctly blocked.
void TlsAgent::ClientAuthCallbackComplete() {
  ASSERT_EQ(CLIENT, role_);

  if (client_auth_callback_type_ != ClientAuthCallbackType::kAsyncDelay &&
      client_auth_callback_type_ != ClientAuthCallbackType::kAsyncImmediate) {
    return;
  }
  client_auth_callback_fired_++;
  EXPECT_TRUE(client_auth_callback_awaiting_);

  std::cerr << "client: calling SSL_ClientCertCallbackComplete with status "
            << (client_auth_callback_success_ ? "success" : "failed")
            << std::endl;

  client_auth_callback_awaiting_ = false;

  if (client_auth_callback_type_ == ClientAuthCallbackType::kAsyncDelay) {
    std::cerr
        << "Running Handshake prior to running SSL_ClientCertCallbackComplete"
        << std::endl;
    SECStatus rv = SSL_ForceHandshake(ssl_fd());
    EXPECT_EQ(rv, SECFailure);
    EXPECT_EQ(PORT_GetError(), PR_WOULD_BLOCK_ERROR);
  }

  ScopedCERTCertificate cert;
  ScopedSECKEYPrivateKey priv;
  if (client_auth_callback_success_) {
    ASSERT_TRUE(TlsAgent::LoadCertificate(name(), &cert, &priv));
    EXPECT_EQ(SECSuccess,
              SSL_ClientCertCallbackComplete(ssl_fd(), SECSuccess,
                                             priv.release(), cert.release()));
  } else {
    EXPECT_EQ(SECSuccess, SSL_ClientCertCallbackComplete(ssl_fd(), SECFailure,
                                                         nullptr, nullptr));
  }
}

SECStatus TlsAgent::GetClientAuthDataHook(void* self, PRFileDesc* fd,
                                          CERTDistNames* caNames,
                                          CERTCertificate** clientCert,
                                          SECKEYPrivateKey** clientKey) {
  TlsAgent* agent = reinterpret_cast<TlsAgent*>(self);
  EXPECT_EQ(CLIENT, agent->role_);
  agent->client_auth_callback_fired_++;

  switch (agent->client_auth_callback_type_) {
    case ClientAuthCallbackType::kAsyncDelay:
    case ClientAuthCallbackType::kAsyncImmediate:
      std::cerr << "Waiting for complete call" << std::endl;
      agent->client_auth_callback_awaiting_ = true;
      return SECWouldBlock;
    case ClientAuthCallbackType::kSync:
    case ClientAuthCallbackType::kNone:
      // Handle the sync case. None && Success is treated as Sync and Success.
      if (!agent->client_auth_callback_success_) {
        return SECFailure;
      }
      ScopedCERTCertificate peerCert(SSL_PeerCertificate(agent->ssl_fd()));
      EXPECT_TRUE(peerCert) << "Client should be able to see the server cert";

      // See bug 1573945
      // CheckCertReqAgainstDefaultCAs(caNames);

      ScopedCERTCertificate cert;
      ScopedSECKEYPrivateKey priv;
      if (!TlsAgent::LoadCertificate(agent->name(), &cert, &priv)) {
        return SECFailure;
      }

      *clientCert = cert.release();
      *clientKey = priv.release();
      return SECSuccess;
  }
  /* This is unreachable, but some old compilers can't tell that. */
  PORT_Assert(0);
  PORT_SetError(SEC_ERROR_LIBRARY_FAILURE);
  return SECFailure;
}

// Increments by 1 for each callback
bool TlsAgent::CheckClientAuthCallbacksCompleted(uint8_t expected) {
  EXPECT_EQ(CLIENT, role_);
  return expected == client_auth_callback_fired_;
}

bool TlsAgent::GetPeerChainLength(size_t* count) {
  SECItemArray* rawChain = nullptr;
  SECStatus rv = SSL_PeerCertificateChainDER(ssl_fd(), &rawChain);
  ScopedSECItemArray chain(rawChain);
  if (rv != SECSuccess) return false;

  *count = chain->len;
  return true;
}

void TlsAgent::CheckPeerChainFunctionConsistency() {
  SECItemArray* rawDerChain = nullptr;
  SECStatus rv = SSL_PeerCertificateChainDER(ssl_fd(), &rawDerChain);
  ScopedSECItemArray derChain(rawDerChain);
  PRErrorCode err1 = PR_GetError();
  ScopedCERTCertList chain(SSL_PeerCertificateChain(ssl_fd()));
  PRErrorCode err2 = PR_GetError();
  if (rv != SECSuccess) {
    ASSERT_EQ(nullptr, chain);
    ASSERT_EQ(nullptr, derChain);
    ASSERT_EQ(err1, SSL_ERROR_NO_CERTIFICATE);
    ASSERT_EQ(err2, SSL_ERROR_NO_CERTIFICATE);
    return;
  }
  ASSERT_NE(nullptr, chain);
  ASSERT_NE(nullptr, derChain);

  unsigned int count = 0;
  for (PRCList* cursor = PR_NEXT_LINK(&chain->list);
       count < derChain->len && cursor != &chain->list;
       cursor = PR_NEXT_LINK(cursor)) {
    CERTCertListNode* node = (CERTCertListNode*)cursor;
    EXPECT_TRUE(
        SECITEM_ItemsAreEqual(&node->cert->derCert, &derChain->items[count]));
    ++count;
  }
  ASSERT_EQ(count, derChain->len);
}

void TlsAgent::CheckCipherSuite(uint16_t suite) {
  EXPECT_EQ(csinfo_.cipherSuite, suite);
}

void TlsAgent::RequestClientAuth(bool requireAuth) {
  ASSERT_EQ(SERVER, role_);

  SetOption(SSL_REQUEST_CERTIFICATE, PR_TRUE);
  SetOption(SSL_REQUIRE_CERTIFICATE, requireAuth ? PR_TRUE : PR_FALSE);

  EXPECT_EQ(SECSuccess, SSL_AuthCertificateHook(
                            ssl_fd(), &TlsAgent::ClientAuthenticated, this));
  expect_client_auth_ = true;
}

void TlsAgent::StartConnect(PRFileDesc* model) {
  EXPECT_TRUE(EnsureTlsSetup(model));

  SECStatus rv;
  rv = SSL_ResetHandshake(ssl_fd(), role_ == SERVER ? PR_TRUE : PR_FALSE);
  EXPECT_EQ(SECSuccess, rv);
  SetState(STATE_CONNECTING);
}

void TlsAgent::DisableAllCiphers() {
  for (size_t i = 0; i < SSL_NumImplementedCiphers; ++i) {
    SECStatus rv =
        SSL_CipherPrefSet(ssl_fd(), SSL_ImplementedCiphers[i], PR_FALSE);
    EXPECT_EQ(SECSuccess, rv);
  }
}

// Not actually all groups, just the ones that we are actually willing
// to use.
const std::vector<SSLNamedGroup> kAllDHEGroups = {
    ssl_grp_ec_curve25519,
    ssl_grp_ec_secp256r1,
    ssl_grp_ec_secp384r1,
    ssl_grp_ec_secp521r1,
    ssl_grp_ffdhe_2048,
    ssl_grp_ffdhe_3072,
    ssl_grp_ffdhe_4096,
    ssl_grp_ffdhe_6144,
    ssl_grp_ffdhe_8192,
    ssl_grp_kem_mlkem768x25519,
    ssl_grp_kem_secp256r1mlkem768,
    ssl_grp_kem_secp384r1mlkem1024,
    ssl_grp_kem_mlkem1024,
};

const std::vector<SSLNamedGroup> kNonPQDHEGroups = {
    ssl_grp_ec_curve25519, ssl_grp_ec_secp256r1, ssl_grp_ec_secp384r1,
    ssl_grp_ec_secp521r1,  ssl_grp_ffdhe_2048,   ssl_grp_ffdhe_3072,
    ssl_grp_ffdhe_4096,    ssl_grp_ffdhe_6144,   ssl_grp_ffdhe_8192,
};

const std::vector<SSLNamedGroup> kECDHEGroups = {
    ssl_grp_ec_curve25519,          ssl_grp_ec_secp256r1,
    ssl_grp_ec_secp384r1,           ssl_grp_ec_secp521r1,
    ssl_grp_kem_mlkem768x25519,     ssl_grp_kem_secp256r1mlkem768,
    ssl_grp_kem_secp384r1mlkem1024,
};

const std::vector<SSLNamedGroup> kFFDHEGroups = {
    ssl_grp_ffdhe_2048, ssl_grp_ffdhe_3072, ssl_grp_ffdhe_4096,
    ssl_grp_ffdhe_6144, ssl_grp_ffdhe_8192};

// Defined because the big DHE groups are ridiculously slow.
const std::vector<SSLNamedGroup> kFasterDHEGroups = {
    ssl_grp_ec_curve25519,
    ssl_grp_ec_secp256r1,
    ssl_grp_ec_secp384r1,
    ssl_grp_ffdhe_2048,
    ssl_grp_ffdhe_3072,
    ssl_grp_kem_mlkem768x25519,
    ssl_grp_kem_secp256r1mlkem768,
    ssl_grp_kem_secp384r1mlkem1024,
    ssl_grp_kem_mlkem1024,
};

const std::vector<SSLNamedGroup> kEcdhHybridGroups = {
    ssl_grp_kem_mlkem768x25519,
    ssl_grp_kem_secp256r1mlkem768,
    ssl_grp_kem_secp384r1mlkem1024,
};

void TlsAgent::EnableCiphersByKeyExchange(SSLKEAType kea) {
  EXPECT_TRUE(EnsureTlsSetup());

  for (size_t i = 0; i < SSL_NumImplementedCiphers; ++i) {
    SSLCipherSuiteInfo csinfo;

    SECStatus rv = SSL_GetCipherSuiteInfo(SSL_ImplementedCiphers[i], &csinfo,
                                          sizeof(csinfo));
    ASSERT_EQ(SECSuccess, rv);
    EXPECT_EQ(sizeof(csinfo), csinfo.length);

    if ((csinfo.keaType == kea) || (csinfo.keaType == ssl_kea_tls13_any)) {
      rv = SSL_CipherPrefSet(ssl_fd(), SSL_ImplementedCiphers[i], PR_TRUE);
      EXPECT_EQ(SECSuccess, rv);
    }
  }
}

void TlsAgent::EnableGroupsByKeyExchange(SSLKEAType kea) {
  switch (kea) {
    case ssl_kea_dh:
      ConfigNamedGroups(kFFDHEGroups);
      break;
    case ssl_kea_ecdh:
      ConfigNamedGroups(kECDHEGroups);
      break;
    case ssl_kea_ecdh_hybrid:
      ConfigNamedGroups(kEcdhHybridGroups);
      break;
    default:
      break;
  }
}

void TlsAgent::EnableGroupsByAuthType(SSLAuthType authType) {
  if (authType == ssl_auth_ecdh_rsa || authType == ssl_auth_ecdh_ecdsa ||
      authType == ssl_auth_ecdsa || authType == ssl_auth_tls13_any ||
      authType == ssl_auth_mldsa44 || authType == ssl_auth_mldsa65 ||
      authType == ssl_auth_mldsa87) {
    ConfigNamedGroups(kECDHEGroups);
  }
}

void TlsAgent::EnableCiphersByAuthType(SSLAuthType authType) {
  EXPECT_TRUE(EnsureTlsSetup());

  for (size_t i = 0; i < SSL_NumImplementedCiphers; ++i) {
    SSLCipherSuiteInfo csinfo;

    SECStatus rv = SSL_GetCipherSuiteInfo(SSL_ImplementedCiphers[i], &csinfo,
                                          sizeof(csinfo));
    ASSERT_EQ(SECSuccess, rv);

    if ((csinfo.authType == authType) ||
        (csinfo.keaType == ssl_kea_tls13_any)) {
      rv = SSL_CipherPrefSet(ssl_fd(), SSL_ImplementedCiphers[i], PR_TRUE);
      EXPECT_EQ(SECSuccess, rv);
    }
  }
}

void TlsAgent::EnableSingleCipher(uint16_t cipher) {
  DisableAllCiphers();
  SECStatus rv = SSL_CipherPrefSet(ssl_fd(), cipher, PR_TRUE);
  EXPECT_EQ(SECSuccess, rv);
}

void TlsAgent::ConfigNamedGroups(const std::vector<SSLNamedGroup>& groups) {
  EXPECT_TRUE(EnsureTlsSetup());
  SECStatus rv = SSL_NamedGroupConfig(ssl_fd(), &groups[0], groups.size());
  EXPECT_EQ(SECSuccess, rv);
}

void TlsAgent::Set0RttEnabled(bool en) {
  SetOption(SSL_ENABLE_0RTT_DATA, en ? PR_TRUE : PR_FALSE);
}

void TlsAgent::SetVersionRange(uint16_t minver, uint16_t maxver) {
  vrange_.min = minver;
  vrange_.max = maxver;

  if (ssl_fd()) {
    SECStatus rv = SSL_VersionRangeSet(ssl_fd(), &vrange_);
    EXPECT_EQ(SECSuccess, rv);
  }
}

SECStatus ResumptionTokenCallback(PRFileDesc* fd,
                                  const PRUint8* resumptionToken,
                                  unsigned int len, void* ctx) {
  EXPECT_NE(nullptr, resumptionToken);
  if (!resumptionToken) {
    return SECFailure;
  }

  std::vector<uint8_t> new_token(resumptionToken, resumptionToken + len);
  reinterpret_cast<TlsAgent*>(ctx)->SetResumptionToken(new_token);
  reinterpret_cast<TlsAgent*>(ctx)->SetResumptionCallbackCalled();
  return SECSuccess;
}

void TlsAgent::SetResumptionTokenCallback() {
  EXPECT_TRUE(EnsureTlsSetup());
  SECStatus rv =
      SSL_SetResumptionTokenCallback(ssl_fd(), ResumptionTokenCallback, this);
  EXPECT_EQ(SECSuccess, rv);
}

void TlsAgent::GetVersionRange(uint16_t* minver, uint16_t* maxver) {
  *minver = vrange_.min;
  *maxver = vrange_.max;
}

void TlsAgent::SetExpectedVersion(uint16_t ver) { expected_version_ = ver; }

void TlsAgent::SetServerKeyBits(uint16_t bits) { server_key_bits_ = bits; }

void TlsAgent::ExpectReadWriteError() { expect_readwrite_error_ = true; }

void TlsAgent::SkipVersionChecks() { skip_version_checks_ = true; }

void TlsAgent::SetSignatureSchemes(const SSLSignatureScheme* schemes,
                                   size_t count) {
  EXPECT_TRUE(EnsureTlsSetup());
  EXPECT_LE(count, SSL_SignatureMaxCount());
  EXPECT_EQ(SECSuccess,
            SSL_SignatureSchemePrefSet(ssl_fd(), schemes,
                                       static_cast<unsigned int>(count)));
  EXPECT_EQ(SECFailure, SSL_SignatureSchemePrefSet(ssl_fd(), schemes, 0))
      << "setting no schemes should fail and do nothing";

  std::vector<SSLSignatureScheme> configuredSchemes(count);
  unsigned int configuredCount;
  EXPECT_EQ(SECFailure,
            SSL_SignatureSchemePrefGet(ssl_fd(), nullptr, &configuredCount, 1))
      << "get schemes, schemes is nullptr";
  EXPECT_EQ(SECFailure,
            SSL_SignatureSchemePrefGet(ssl_fd(), &configuredSchemes[0],
                                       &configuredCount, 0))
      << "get schemes, too little space";
  EXPECT_EQ(SECFailure,
            SSL_SignatureSchemePrefGet(ssl_fd(), &configuredSchemes[0], nullptr,
                                       configuredSchemes.size()))
      << "get schemes, countOut is nullptr";

  EXPECT_EQ(SECSuccess, SSL_SignatureSchemePrefGet(
                            ssl_fd(), &configuredSchemes[0], &configuredCount,
                            configuredSchemes.size()));
  // SignatureSchemePrefSet drops unsupported algorithms silently, so the
  // number that are configured might be fewer.
  EXPECT_LE(configuredCount, count);
  unsigned int i = 0;
  for (unsigned int j = 0; j < count && i < configuredCount; ++j) {
    if (i < configuredCount && schemes[j] == configuredSchemes[i]) {
      ++i;
    }
  }
  EXPECT_EQ(i, configuredCount) << "schemes in use were all set";
}

void TlsAgent::CheckKEA(SSLKEAType kea, SSLNamedGroup kea_group,
                        size_t kea_size) const {
  EXPECT_EQ(STATE_CONNECTED, state_);
  EXPECT_EQ(kea, info_.keaType);
  if (kea_size == 0) {
    switch (kea_group) {
      case ssl_grp_ec_curve25519:
      case ssl_grp_kem_mlkem768x25519:
        kea_size = 255;
        break;
      case ssl_grp_kem_secp256r1mlkem768:
      case ssl_grp_ec_secp256r1:
      case ssl_grp_kem_mlkem1024:
        kea_size = 256;
        break;
      case ssl_grp_kem_secp384r1mlkem1024:
      case ssl_grp_ec_secp384r1:
        kea_size = 384;
        break;
      case ssl_grp_ffdhe_2048:
        kea_size = 2048;
        break;
      case ssl_grp_ffdhe_3072:
        kea_size = 3072;
        break;
      case ssl_grp_ffdhe_custom:
        break;
      default:
        if (kea == ssl_kea_rsa) {
          kea_size = server_key_bits_;
        } else {
          EXPECT_TRUE(false) << "need to update group sizes";
        }
    }
  }
  if (kea_group != ssl_grp_ffdhe_custom) {
    EXPECT_EQ(kea_size, info_.keaKeyBits);
    EXPECT_EQ(kea_group, info_.keaGroup);
  }
}

void TlsAgent::CheckOriginalKEA(SSLNamedGroup kea_group) const {
  if (kea_group != ssl_grp_ffdhe_custom) {
    EXPECT_EQ(kea_group, info_.originalKeaGroup);
  }
}

void TlsAgent::CheckAuthType(SSLAuthType auth,
                             SSLSignatureScheme sig_scheme) const {
  EXPECT_EQ(STATE_CONNECTED, state_);
  EXPECT_EQ(auth, info_.authType);
  if (auth != ssl_auth_psk) {
    EXPECT_EQ(server_key_bits_, info_.authKeyBits);
  }
  if (expected_version_ < SSL_LIBRARY_VERSION_TLS_1_2) {
    switch (auth) {
      case ssl_auth_rsa_sign:
        sig_scheme = ssl_sig_rsa_pkcs1_sha1md5;
        break;
      case ssl_auth_ecdsa:
        sig_scheme = ssl_sig_ecdsa_sha1;
        break;
      default:
        break;
    }
  }
  EXPECT_EQ(sig_scheme, info_.signatureScheme);

  if (info_.protocolVersion >= SSL_LIBRARY_VERSION_TLS_1_3) {
    return;
  }

  // Check authAlgorithm, which is the old value for authType.  This is a second
  // switch statement because default label is different.
  switch (auth) {
    case ssl_auth_rsa_sign:
    case ssl_auth_rsa_pss:
      EXPECT_EQ(ssl_auth_rsa_decrypt, csinfo_.authAlgorithm)
          << "authAlgorithm for RSA is always decrypt";
      break;
    case ssl_auth_ecdh_rsa:
      EXPECT_EQ(ssl_auth_rsa_decrypt, csinfo_.authAlgorithm)
          << "authAlgorithm for ECDH_RSA is RSA decrypt (i.e., wrong)";
      break;
    case ssl_auth_ecdh_ecdsa:
      EXPECT_EQ(ssl_auth_ecdsa, csinfo_.authAlgorithm)
          << "authAlgorithm for ECDH_ECDSA is ECDSA (i.e., wrong)";
      break;
    default:
      EXPECT_EQ(auth, csinfo_.authAlgorithm)
          << "authAlgorithm is (usually) the same as authType";
      break;
  }
}

void TlsAgent::EnableFalseStart() {
  EXPECT_TRUE(EnsureTlsSetup());

  falsestart_enabled_ = true;
  EXPECT_EQ(SECSuccess, SSL_SetCanFalseStartCallback(
                            ssl_fd(), CanFalseStartCallback, this));
  SetOption(SSL_ENABLE_FALSE_START, PR_TRUE);
}

void TlsAgent::ExpectEch(bool expected) { expect_ech_ = expected; }

void TlsAgent::ExpectPsk(SSLPskType psk) { expect_psk_ = psk; }

void TlsAgent::ExpectResumption() { expect_psk_ = ssl_psk_resume; }

void TlsAgent::EnableAlpn(const uint8_t* val, size_t len) {
  EXPECT_TRUE(EnsureTlsSetup());
  EXPECT_EQ(SECSuccess, SSL_SetNextProtoNego(ssl_fd(), val, len));
}

void TlsAgent::AddPsk(const ScopedPK11SymKey& psk, std::string label,
                      SSLHashType hash, uint16_t zeroRttSuite) {
  EXPECT_TRUE(EnsureTlsSetup());
  EXPECT_EQ(SECSuccess, SSL_AddExternalPsk0Rtt(
                            ssl_fd(), psk.get(),
                            reinterpret_cast<const uint8_t*>(label.data()),
                            label.length(), hash, zeroRttSuite, 1000));
}

void TlsAgent::RemovePsk(std::string label) {
  EXPECT_EQ(SECSuccess,
            SSL_RemoveExternalPsk(
                ssl_fd(), reinterpret_cast<const uint8_t*>(label.data()),
                label.length()));
}

void TlsAgent::CheckAlpn(SSLNextProtoState expected_state,
                         const std::string& expected) const {
  SSLNextProtoState alpn_state;
  char chosen[10];
  unsigned int chosen_len;
  SECStatus rv = SSL_GetNextProto(ssl_fd(), &alpn_state,
                                  reinterpret_cast<unsigned char*>(chosen),
                                  &chosen_len, sizeof(chosen));
  EXPECT_EQ(SECSuccess, rv);
  EXPECT_EQ(expected_state, alpn_state);
  if (alpn_state == SSL_NEXT_PROTO_NO_SUPPORT) {
    EXPECT_EQ("", expected);
  } else {
    EXPECT_NE("", expected);
    EXPECT_EQ(expected, std::string(chosen, chosen_len));
  }
}

void TlsAgent::CheckEpochs(uint16_t expected_read,
                           uint16_t expected_write) const {
  uint16_t read_epoch = 0;
  uint16_t write_epoch = 0;
  EXPECT_EQ(SECSuccess,
            SSL_GetCurrentEpoch(ssl_fd(), &read_epoch, &write_epoch));
  EXPECT_EQ(expected_read, read_epoch) << role_str() << " read epoch";
  EXPECT_EQ(expected_write, write_epoch) << role_str() << " write epoch";
}

void TlsAgent::EnableSrtp() {
  EXPECT_TRUE(EnsureTlsSetup());
  const uint16_t ciphers[] = {SRTP_AES128_CM_HMAC_SHA1_80,
                              SRTP_AES128_CM_HMAC_SHA1_32};
  EXPECT_EQ(SECSuccess,
            SSL_SetSRTPCiphers(ssl_fd(), ciphers, PR_ARRAY_SIZE(ciphers)));
}

void TlsAgent::CheckSrtp() const {
  uint16_t actual;
  EXPECT_EQ(SECSuccess, SSL_GetSRTPCipher(ssl_fd(), &actual));
  EXPECT_EQ(SRTP_AES128_CM_HMAC_SHA1_80, actual);
}

void TlsAgent::CheckErrorCode(int32_t expected) const {
  EXPECT_EQ(STATE_ERROR, state_);
  EXPECT_EQ(expected, error_code_)
      << "Got error code " << PORT_ErrorToName(error_code_) << " expecting "
      << PORT_ErrorToName(expected) << std::endl;
}

static uint8_t GetExpectedAlertLevel(uint8_t alert) {
  if (alert == kTlsAlertCloseNotify) {
    return kTlsAlertWarning;
  }
  return kTlsAlertFatal;
}

void TlsAgent::ExpectReceiveAlert(uint8_t alert, uint8_t level) {
  expected_received_alert_ = alert;
  if (level == 0) {
    expected_received_alert_level_ = GetExpectedAlertLevel(alert);
  } else {
    expected_received_alert_level_ = level;
  }
}

void TlsAgent::ExpectSendAlert(uint8_t alert, uint8_t level) {
  expected_sent_alert_ = alert;
  if (level == 0) {
    expected_sent_alert_level_ = GetExpectedAlertLevel(alert);
  } else {
    expected_sent_alert_level_ = level;
  }
}

void TlsAgent::CheckAlert(bool sent, const SSLAlert* alert) {
  LOG(((alert->level == kTlsAlertWarning) ? "Warning" : "Fatal")
      << " alert " << (sent ? "sent" : "received") << ": "
      << static_cast<int>(alert->description));

  auto& expected = sent ? expected_sent_alert_ : expected_received_alert_;
  auto& expected_level =
      sent ? expected_sent_alert_level_ : expected_received_alert_level_;
  /* Silently pass close_notify in case the test has already ended. */
  if (expected == kTlsAlertCloseNotify && expected_level == kTlsAlertWarning &&
      alert->description == expected && alert->level == expected_level) {
    return;
  }

  EXPECT_EQ(expected, alert->description);
  EXPECT_EQ(expected_level, alert->level);
  expected = kTlsAlertCloseNotify;
  expected_level = kTlsAlertWarning;
}

void TlsAgent::WaitForErrorCode(int32_t expected, uint32_t delay) const {
  ASSERT_EQ(0, error_code_);
  WAIT_(error_code_ != 0, delay);
  EXPECT_EQ(expected, error_code_)
      << "Got error code " << PORT_ErrorToName(error_code_) << " expecting "
      << PORT_ErrorToName(expected) << std::endl;
}

void TlsAgent::CheckPreliminaryInfo() {
  SSLPreliminaryChannelInfo preinfo;
  EXPECT_EQ(SECSuccess,
            SSL_GetPreliminaryChannelInfo(ssl_fd(), &preinfo, sizeof(preinfo)));
  EXPECT_EQ(sizeof(preinfo), preinfo.length);
  EXPECT_TRUE(preinfo.valuesSet & ssl_preinfo_version);

  // A version of 0 is invalid and indicates no expectation.  This value is
  // initialized to 0 so that tests that don't explicitly set an expected
  // version can negotiate a version.
  if (!expected_version_) {
    expected_version_ = preinfo.protocolVersion;
  }
  EXPECT_EQ(expected_version_, preinfo.protocolVersion);

  // As with the version; 0 is the null cipher suite (and also invalid).
  if (!expected_cipher_suite_) {
    expected_cipher_suite_ = preinfo.cipherSuite;
  }
  EXPECT_EQ(expected_cipher_suite_, preinfo.cipherSuite);
}

// Check that all the expected callbacks have been called.
void TlsAgent::CheckCallbacks() const {
  // If false start happens, the handshake is reported as being complete at the
  // point that false start happens.
  if (expect_psk_ == ssl_psk_resume || !falsestart_enabled_) {
    EXPECT_TRUE(handshake_callback_called_);
  }

  // These callbacks shouldn't fire if we are resuming, except on TLS 1.3.
  if (role_ == SERVER) {
    PRBool have_sni = SSLInt_ExtensionNegotiated(ssl_fd(), ssl_server_name_xtn);
    EXPECT_EQ(((expect_psk_ != ssl_psk_resume && have_sni) ||
               expected_version_ >= SSL_LIBRARY_VERSION_TLS_1_3),
              sni_hook_called_);
  } else {
    EXPECT_EQ(expect_psk_ == ssl_psk_none, auth_certificate_hook_called_);
    // Note that this isn't unconditionally called, even with false start on.
    // But the callback is only skipped if a cipher that is ridiculously weak
    // (80 bits) is chosen.  Don't test that: plan to remove bad ciphers.
    EXPECT_EQ(falsestart_enabled_ && expect_psk_ != ssl_psk_resume,
              can_falsestart_hook_called_);
  }
}

void TlsAgent::ResetPreliminaryInfo() {
  expected_version_ = 0;
  expected_cipher_suite_ = 0;
}

void TlsAgent::UpdatePreliminaryChannelInfo() {
  SECStatus rv =
      SSL_GetPreliminaryChannelInfo(ssl_fd(), &pre_info_, sizeof(pre_info_));
  EXPECT_EQ(SECSuccess, rv);
  EXPECT_EQ(sizeof(pre_info_), pre_info_.length);
}

void TlsAgent::ValidateCipherSpecs() {
  PRInt32 cipherSpecs = SSLInt_CountCipherSpecs(ssl_fd());
  // We use one ciphersuite in each direction.
  PRInt32 expected = 2;
  if (variant_ == ssl_variant_datagram) {
    // For DTLS 1.3, the client retains the cipher spec for the handshake so
    // that it can retransmit its final flight. It also retains the handshake
    // read cipher spec so that it can read ACKs from the server. The server
    // retains the handshake read cipher spec so it can read the client's
    // retransmitted Finished.
    if (expected_version_ >= SSL_LIBRARY_VERSION_TLS_1_3) {
      if (role_ == CLIENT) {
        expected = 4;
      } else {
        expected = 3;
      }
    } else {
      // For DTLS 1.1 and 1.2, the last endpoint to send maintains a cipher spec
      // until the holddown timer runs down.
      if (expect_psk_ == ssl_psk_resume) {
        if (role_ == CLIENT) {
          expected = 3;
        }
      } else {
        if (role_ == SERVER) {
          expected = 3;
        }
      }
    }
  }
  // This function will be run before the handshake completes if false start is
  // enabled.  In that case, the client will still be reading cleartext, but
  // will have a spec prepared for reading ciphertext.  With DTLS, the client
  // will also have a spec retained for retransmission of handshake messages.
  if (role_ == CLIENT && falsestart_enabled_ && !handshake_callback_called_) {
    EXPECT_GT(SSL_LIBRARY_VERSION_TLS_1_3, expected_version_);
    expected = (variant_ == ssl_variant_datagram) ? 4 : 3;
  }
  EXPECT_EQ(expected, cipherSpecs);
  if (expected != cipherSpecs) {
    SSLInt_PrintCipherSpecs(role_str().c_str(), ssl_fd());
  }
}

void TlsAgent::Connected() {
  if (state_ == STATE_CONNECTED) {
    return;
  }

  LOG("Handshake success");
  CheckPreliminaryInfo();
  CheckCallbacks();

  SECStatus rv = SSL_GetChannelInfo(ssl_fd(), &info_, sizeof(info_));
  EXPECT_EQ(SECSuccess, rv);
  EXPECT_EQ(sizeof(info_), info_.length);

  EXPECT_EQ(expect_psk_ == ssl_psk_resume, info_.resumed == PR_TRUE);
  EXPECT_EQ(expect_psk_, info_.pskType);
  EXPECT_EQ(expect_ech_, info_.echAccepted);

  // Preliminary values are exposed through callbacks during the handshake.
  // If either expected values were set or the callbacks were called, check
  // that the final values are correct.
  UpdatePreliminaryChannelInfo();
  EXPECT_EQ(expected_version_, info_.protocolVersion);
  EXPECT_EQ(expected_cipher_suite_, info_.cipherSuite);

  rv = SSL_GetCipherSuiteInfo(info_.cipherSuite, &csinfo_, sizeof(csinfo_));
  EXPECT_EQ(SECSuccess, rv);
  EXPECT_EQ(sizeof(csinfo_), csinfo_.length);

  ValidateCipherSpecs();

  SetState(STATE_CONNECTED);
}

void TlsAgent::CheckClientAuthCompleted(uint8_t handshakes) {
  EXPECT_FALSE(client_auth_callback_awaiting_);
  switch (client_auth_callback_type_) {
    case ClientAuthCallbackType::kNone:
      if (!client_auth_callback_success_) {
        EXPECT_TRUE(CheckClientAuthCallbacksCompleted(0));
        break;
      }
    case ClientAuthCallbackType::kSync:
      EXPECT_TRUE(CheckClientAuthCallbacksCompleted(handshakes));
      break;
    case ClientAuthCallbackType::kAsyncDelay:
    case ClientAuthCallbackType::kAsyncImmediate:
      EXPECT_TRUE(CheckClientAuthCallbacksCompleted(2 * handshakes));
      break;
  }
}

void TlsAgent::EnableExtendedMasterSecret() {
  SetOption(SSL_ENABLE_EXTENDED_MASTER_SECRET, PR_TRUE);
}

void TlsAgent::CheckExtendedMasterSecret(bool expected) {
  if (version() >= SSL_LIBRARY_VERSION_TLS_1_3) {
    expected = PR_TRUE;
  }
  ASSERT_EQ(expected, info_.extendedMasterSecretUsed != PR_FALSE)
      << "unexpected extended master secret state for " << name_;
}

void TlsAgent::CheckEarlyDataAccepted(bool expected) {
  if (version() < SSL_LIBRARY_VERSION_TLS_1_3) {
    expected = false;
  }
  ASSERT_EQ(expected, info_.earlyDataAccepted != PR_FALSE)
      << "unexpected early data state for " << name_;
}

void TlsAgent::CheckSecretsDestroyed() {
  ASSERT_EQ(PR_TRUE, SSLInt_CheckSecretsDestroyed(ssl_fd()));
}

void TlsAgent::SetDowngradeCheckVersion(uint16_t ver) {
  ASSERT_TRUE(EnsureTlsSetup());

  SECStatus rv = SSL_SetDowngradeCheckVersion(ssl_fd(), ver);
  ASSERT_EQ(SECSuccess, rv);
}

void TlsAgent::Handshake() {
  LOGV("Handshake");
  SECStatus rv = SSL_ForceHandshake(ssl_fd());
  if (client_auth_callback_awaiting_) {
    ClientAuthCallbackComplete();
    rv = SSL_ForceHandshake(ssl_fd());
  }
  if (rv == SECSuccess) {
    Connected();
    Poller::Instance()->Wait(READABLE_EVENT, adapter_, this,
                             &TlsAgent::ReadableCallback);
    return;
  }

  int32_t err = PR_GetError();
  if (err == PR_WOULD_BLOCK_ERROR) {
    LOGV("Would have blocked");
    if (variant_ == ssl_variant_datagram) {
      if (timer_handle_) {
        timer_handle_->Cancel();
        timer_handle_ = nullptr;
      }

      PRIntervalTime timeout;
      rv = DTLS_GetHandshakeTimeout(ssl_fd(), &timeout);
      if (rv == SECSuccess) {
        Poller::Instance()->SetTimer(
            timeout + 1, this, &TlsAgent::ReadableCallback, &timer_handle_);
      }
    }
    Poller::Instance()->Wait(READABLE_EVENT, adapter_, this,
                             &TlsAgent::ReadableCallback);
    return;
  }

  if (err != 0) {
    LOG("Handshake failed with error " << PORT_ErrorToName(err) << ": "
                                       << PORT_ErrorToString(err));
  }

  error_code_ = err;
  SetState(STATE_ERROR);
}

void TlsAgent::PrepareForRenegotiate() {
  EXPECT_EQ(STATE_CONNECTED, state_);

  SetState(STATE_CONNECTING);
}

void TlsAgent::StartRenegotiate() {
  PrepareForRenegotiate();

  SECStatus rv = SSL_ReHandshake(ssl_fd(), PR_TRUE);
  EXPECT_EQ(SECSuccess, rv);
}

void TlsAgent::SendDirect(const DataBuffer& buf) {
  LOG("Send Direct " << buf);
  auto peer = adapter_->peer().lock();
  if (peer) {
    peer->PacketReceived(buf);
  } else {
    LOG("Send Direct peer absent");
  }
}

void TlsAgent::SendRecordDirect(const TlsRecord& record) {
  DataBuffer buf;

  auto rv = record.header.Write(&buf, 0, record.buffer);
  EXPECT_EQ(record.header.header_length() + record.buffer.len(), rv);
  SendDirect(buf);
}

static bool ErrorIsFatal(PRErrorCode code) {
  return code != PR_WOULD_BLOCK_ERROR && code != SSL_ERROR_RX_SHORT_DTLS_READ;
}

void TlsAgent::SendData(size_t bytes, size_t blocksize) {
  uint8_t block[16385];  // One larger than the maximum record size.

  ASSERT_LE(blocksize, sizeof(block));

  while (bytes) {
    size_t tosend = std::min(blocksize, bytes);

    for (size_t i = 0; i < tosend; ++i) {
      block[i] = 0xff & send_ctr_;
      ++send_ctr_;
    }

    SendBuffer(DataBuffer(block, tosend));
    bytes -= tosend;
  }
}

void TlsAgent::SendBuffer(const DataBuffer& buf) {
  LOGV("Writing " << buf.len() << " bytes");
  int32_t rv = PR_Write(ssl_fd(), buf.data(), buf.len());
  if (expect_readwrite_error_) {
    EXPECT_GT(0, rv);
    EXPECT_NE(PR_WOULD_BLOCK_ERROR, error_code_);
    error_code_ = PR_GetError();
    expect_readwrite_error_ = false;
  } else {
    ASSERT_EQ(buf.len(), static_cast<size_t>(rv));
  }
}

bool TlsAgent::SendEncryptedRecord(const std::shared_ptr<TlsCipherSpec>& spec,
                                   uint64_t seq, uint8_t ct,
                                   const DataBuffer& buf) {
  // Ensure that we are doing TLS 1.3.
  EXPECT_GE(expected_version_, SSL_LIBRARY_VERSION_TLS_1_3);
  if (variant_ != ssl_variant_datagram) {
    ADD_FAILURE();
    return false;
  }

  LOGV("Encrypting " << buf.len() << " bytes");
  uint8_t dtls13_ct = kCtDtlsCiphertext | kCtDtlsCiphertext16bSeqno |
                      kCtDtlsCiphertextLengthPresent;
  TlsRecordHeader header(variant_, expected_version_, dtls13_ct, seq);
  TlsRecordHeader out_header(header);
  DataBuffer padded = buf;
  padded.Write(padded.len(), ct, 1);
  DataBuffer ciphertext;
  if (!spec->Protect(header, padded, &ciphertext, &out_header)) {
    return false;
  }

  DataBuffer record;
  auto rv = out_header.Write(&record, 0, ciphertext);
  EXPECT_EQ(out_header.header_length() + ciphertext.len(), rv);
  SendDirect(record);
  return true;
}

void TlsAgent::ReadBytes(size_t amount) {
  uint8_t block[16384];

  size_t remaining = amount;
  while (remaining > 0) {
    int32_t rv = PR_Read(ssl_fd(), block, (std::min)(amount, sizeof(block)));
    LOGV("ReadBytes " << rv);

    if (rv > 0) {
      size_t count = static_cast<size_t>(rv);
      for (size_t i = 0; i < count; ++i) {
        ASSERT_EQ(recv_ctr_ & 0xff, block[i]);
        recv_ctr_++;
      }
      remaining -= rv;
    } else {
      PRErrorCode err = 0;
      if (rv < 0) {
        err = PR_GetError();
        if (err != 0) {
          LOG("Read error " << PORT_ErrorToName(err) << ": "
                            << PORT_ErrorToString(err));
        }
        if (err != PR_WOULD_BLOCK_ERROR && expect_readwrite_error_) {
          if (ErrorIsFatal(err)) {
            SetState(STATE_ERROR);
          }
          error_code_ = err;
          expect_readwrite_error_ = false;
        }
      }
      if (err != 0 && ErrorIsFatal(err)) {
        // If we hit a fatal error, we're done.
        remaining = 0;
      }
      break;
    }
  }

  // If closed, then don't bother waiting around.
  if (remaining) {
    LOGV("Re-arming");
    Poller::Instance()->Wait(READABLE_EVENT, adapter_, this,
                             &TlsAgent::ReadableCallback);
  }
}

void TlsAgent::ResetSentBytes(size_t bytes) { send_ctr_ = bytes; }

void TlsAgent::SetOption(int32_t option, int value) {
  ASSERT_TRUE(EnsureTlsSetup());
  EXPECT_EQ(SECSuccess, SSL_OptionSet(ssl_fd(), option, value));
}

void TlsAgent::ConfigureSessionCache(SessionResumptionMode mode) {
  SetOption(SSL_NO_CACHE, mode & RESUME_SESSIONID ? PR_FALSE : PR_TRUE);
  SetOption(SSL_ENABLE_SESSION_TICKETS,
            mode & RESUME_TICKET ? PR_TRUE : PR_FALSE);
}

void TlsAgent::EnableECDHEServerKeyReuse() {
  ASSERT_EQ(TlsAgent::SERVER, role_);
  SetOption(SSL_REUSE_SERVER_ECDHE_KEY, PR_TRUE);
}

static const std::string kTlsRolesAllArr[] = {"CLIENT", "SERVER"};
::testing::internal::ParamGenerator<std::string>
    TlsAgentTestBase::kTlsRolesAll = ::testing::ValuesIn(kTlsRolesAllArr);

void TlsAgentTestBase::SetUp() {
  SSL_ConfigServerSessionIDCache(1024, 0, 0, g_working_dir_path.c_str());
}

void TlsAgentTestBase::TearDown() {
  agent_ = nullptr;
  SSL_ClearSessionCache();
  SSL_ShutdownServerSessionIDCache();
}

void TlsAgentTestBase::Reset(const std::string& server_name) {
  agent_.reset(
      new TlsAgent(role_ == TlsAgent::CLIENT ? TlsAgent::kClient : server_name,
                   role_, variant_));
  if (version_) {
    agent_->SetVersionRange(version_, version_);
  }
  agent_->adapter()->SetPeer(sink_adapter_);
  agent_->StartConnect();
}

void TlsAgentTestBase::EnsureInit() {
  if (!agent_) {
    Reset();
  }
  const std::vector<SSLNamedGroup> groups = {
      ssl_grp_ec_curve25519, ssl_grp_ec_secp256r1, ssl_grp_ec_secp384r1,
      ssl_grp_ffdhe_2048};
  agent_->ConfigNamedGroups(groups);
}

void TlsAgentTestBase::ExpectAlert(uint8_t alert) {
  EnsureInit();
  agent_->ExpectSendAlert(alert);
}

void TlsAgentTestBase::ProcessMessage(const DataBuffer& buffer,
                                      TlsAgent::State expected_state,
                                      int32_t error_code) {
  std::cerr << "Process message: " << buffer << std::endl;
  EnsureInit();
  agent_->adapter()->PacketReceived(buffer);
  agent_->Handshake();

  ASSERT_EQ(expected_state, agent_->state());

  if (expected_state == TlsAgent::STATE_ERROR) {
    ASSERT_EQ(error_code, agent_->error_code());
  }
}

void TlsAgentTestBase::MakeRecord(SSLProtocolVariant variant, uint8_t type,
                                  uint16_t version, const uint8_t* buf,
                                  size_t len, DataBuffer* out,
                                  uint64_t sequence_number) {
  // Fixup the content type for DTLSCiphertext
  if (variant == ssl_variant_datagram &&
      version >= SSL_LIBRARY_VERSION_TLS_1_3 &&
      type == ssl_ct_application_data) {
    type = kCtDtlsCiphertext | kCtDtlsCiphertext16bSeqno |
           kCtDtlsCiphertextLengthPresent;
  }

  size_t index = 0;
  if (variant == ssl_variant_stream) {
    index = out->Write(index, type, 1);
    index = out->Write(index, version, 2);
  } else if (version >= SSL_LIBRARY_VERSION_TLS_1_3 &&
             (type & kCtDtlsCiphertextMask) == kCtDtlsCiphertext) {
    uint32_t epoch = (sequence_number >> 48) & 0x3;
    index = out->Write(index, type | epoch, 1);
    uint32_t seqno = sequence_number & ((1ULL << 16) - 1);
    index = out->Write(index, seqno, 2);
  } else {
    index = out->Write(index, type, 1);
    index = out->Write(index, TlsVersionToDtlsVersion(version), 2);
    index = out->Write(index, sequence_number >> 32, 4);
    index = out->Write(index, sequence_number & PR_UINT32_MAX, 4);
  }
  index = out->Write(index, len, 2);
  out->Write(index, buf, len);
}

void TlsAgentTestBase::MakeRecord(uint8_t type, uint16_t version,
                                  const uint8_t* buf, size_t len,
                                  DataBuffer* out, uint64_t seq_num) const {
  MakeRecord(variant_, type, version, buf, len, out, seq_num);
}

void TlsAgentTestBase::MakeHandshakeMessage(uint8_t hs_type,
                                            const uint8_t* data, size_t hs_len,
                                            DataBuffer* out,
                                            uint64_t seq_num) const {
  return MakeHandshakeMessageFragment(hs_type, data, hs_len, out, seq_num, 0,
                                      0);
}

void TlsAgentTestBase::MakeHandshakeMessageFragment(
    uint8_t hs_type, const uint8_t* data, size_t hs_len, DataBuffer* out,
    uint64_t seq_num, uint32_t fragment_offset,
    uint32_t fragment_length) const {
  size_t index = 0;
  if (!fragment_length) fragment_length = hs_len;
  index = out->Write(index, hs_type, 1);  // Handshake record type.
  index = out->Write(index, hs_len, 3);   // Handshake length
  if (variant_ == ssl_variant_datagram) {
    index = out->Write(index, seq_num, 2);
    index = out->Write(index, fragment_offset, 3);
    index = out->Write(index, fragment_length, 3);
  }
  if (data) {
    index = out->Write(index, data, fragment_length);
  } else {
    for (size_t i = 0; i < fragment_length; ++i) {
      index = out->Write(index, 1, 1);
    }
  }
}

void TlsAgentTestBase::MakeTrivialHandshakeRecord(uint8_t hs_type,
                                                  size_t hs_len,
                                                  DataBuffer* out) {
  size_t index = 0;
  index = out->Write(index, ssl_ct_handshake, 1);  // Content Type
  index = out->Write(index, 3, 1);                 // Version high
  index = out->Write(index, 1, 1);                 // Version low
  index = out->Write(index, 4 + hs_len, 2);        // Length

  index = out->Write(index, hs_type, 1);  // Handshake record type.
  index = out->Write(index, hs_len, 3);   // Handshake length
  for (size_t i = 0; i < hs_len; ++i) {
    index = out->Write(index, 1, 1);
  }
}

DataBuffer TlsAgentTestBase::MakeCannedTls13ServerHello() {
  DataBuffer sh(kCannedTls13ServerHello, sizeof(kCannedTls13ServerHello));
  if (variant_ == ssl_variant_datagram) {
    sh.Write(0, SSL_LIBRARY_VERSION_DTLS_1_2_WIRE, 2);
    // The version should be at the end.
    uint32_t v;
    EXPECT_TRUE(sh.Read(sh.len() - 2, 2, &v));
    EXPECT_EQ(static_cast<uint32_t>(SSL_LIBRARY_VERSION_TLS_1_3), v);
    sh.Write(sh.len() - 2, SSL_LIBRARY_VERSION_DTLS_1_3_WIRE, 2);
  }
  return sh;
}

}  // namespace nss_test
