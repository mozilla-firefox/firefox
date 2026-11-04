/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "tls_connect.h"
#include "sslexp.h"
extern "C" {
#include "libssl_internals.h"
}

#include <iostream>

#include "databuffer.h"
#include "gtest_utils.h"
#include "nss_scoped_ptrs.h"
#include "sslproto.h"

extern std::string g_working_dir_path;

namespace nss_test {

static const SSLProtocolVariant kTlsVariantsStreamArr[] = {ssl_variant_stream};
::testing::internal::ParamGenerator<SSLProtocolVariant>
    TlsConnectTestBase::kTlsVariantsStream =
        ::testing::ValuesIn(kTlsVariantsStreamArr);
static const SSLProtocolVariant kTlsVariantsDatagramArr[] = {
    ssl_variant_datagram};
::testing::internal::ParamGenerator<SSLProtocolVariant>
    TlsConnectTestBase::kTlsVariantsDatagram =
        ::testing::ValuesIn(kTlsVariantsDatagramArr);
static const SSLProtocolVariant kTlsVariantsAllArr[] = {ssl_variant_stream,
                                                        ssl_variant_datagram};
::testing::internal::ParamGenerator<SSLProtocolVariant>
    TlsConnectTestBase::kTlsVariantsAll =
        ::testing::ValuesIn(kTlsVariantsAllArr);

static const uint16_t kTlsV10Arr[] = {SSL_LIBRARY_VERSION_TLS_1_0};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV10 =
    ::testing::ValuesIn(kTlsV10Arr);
static const uint16_t kTlsV11Arr[] = {SSL_LIBRARY_VERSION_TLS_1_1};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV11 =
    ::testing::ValuesIn(kTlsV11Arr);
static const uint16_t kTlsV12Arr[] = {SSL_LIBRARY_VERSION_TLS_1_2};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV12 =
    ::testing::ValuesIn(kTlsV12Arr);
static const uint16_t kTlsV10V11Arr[] = {SSL_LIBRARY_VERSION_TLS_1_0,
                                         SSL_LIBRARY_VERSION_TLS_1_1};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV10V11 =
    ::testing::ValuesIn(kTlsV10V11Arr);
static const uint16_t kTlsV10ToV12Arr[] = {SSL_LIBRARY_VERSION_TLS_1_0,
                                           SSL_LIBRARY_VERSION_TLS_1_1,
                                           SSL_LIBRARY_VERSION_TLS_1_2};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV10ToV12 =
    ::testing::ValuesIn(kTlsV10ToV12Arr);
static const uint16_t kTlsV11V12Arr[] = {SSL_LIBRARY_VERSION_TLS_1_1,
                                         SSL_LIBRARY_VERSION_TLS_1_2};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV11V12 =
    ::testing::ValuesIn(kTlsV11V12Arr);

static const uint16_t kTlsV11PlusArr[] = {
#ifndef NSS_DISABLE_TLS_1_3
    SSL_LIBRARY_VERSION_TLS_1_3,
#endif
    SSL_LIBRARY_VERSION_TLS_1_2, SSL_LIBRARY_VERSION_TLS_1_1};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV11Plus =
    ::testing::ValuesIn(kTlsV11PlusArr);
static const uint16_t kTlsV12PlusArr[] = {
#ifndef NSS_DISABLE_TLS_1_3
    SSL_LIBRARY_VERSION_TLS_1_3,
#endif
    SSL_LIBRARY_VERSION_TLS_1_2};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV12Plus =
    ::testing::ValuesIn(kTlsV12PlusArr);
static const uint16_t kTlsV13Arr[] = {SSL_LIBRARY_VERSION_TLS_1_3};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsV13 =
    ::testing::ValuesIn(kTlsV13Arr);
static const uint16_t kTlsVAllArr[] = {
#ifndef NSS_DISABLE_TLS_1_3
    SSL_LIBRARY_VERSION_TLS_1_3,
#endif
    SSL_LIBRARY_VERSION_TLS_1_2, SSL_LIBRARY_VERSION_TLS_1_1,
    SSL_LIBRARY_VERSION_TLS_1_0};
::testing::internal::ParamGenerator<uint16_t> TlsConnectTestBase::kTlsVAll =
    ::testing::ValuesIn(kTlsVAllArr);

std::string VersionString(uint16_t version) {
  switch (version) {
    case 0:
      return "(no version)";
    case SSL_LIBRARY_VERSION_3_0:
      return "1.0";
    case SSL_LIBRARY_VERSION_TLS_1_0:
      return "1.0";
    case SSL_LIBRARY_VERSION_TLS_1_1:
      return "1.1";
    case SSL_LIBRARY_VERSION_TLS_1_2:
      return "1.2";
    case SSL_LIBRARY_VERSION_TLS_1_3:
      return "1.3";
    default:
      std::cerr << "Invalid version: " << version << std::endl;
      EXPECT_TRUE(false);
      return "";
  }
}

// The default anti-replay window for tests.  Tests that rely on a different
// value call ResetAntiReplay directly.
static PRTime kAntiReplayWindow = 100 * PR_USEC_PER_SEC;

TlsConnectTestBase::TlsConnectTestBase(SSLProtocolVariant variant,
                                       uint16_t version)
    : variant_(variant),
      client_(new TlsAgent(TlsAgent::kClient, TlsAgent::CLIENT, variant_)),
      server_(new TlsAgent(TlsAgent::kServerRsa, TlsAgent::SERVER, variant_)),
      client_model_(nullptr),
      server_model_(nullptr),
      version_(version),
      expected_resumption_mode_(RESUME_NONE),
      expected_resumptions_(0),
      session_ids_(),
      expect_extended_master_secret_(false),
      expect_early_data_accepted_(false),
      skip_version_checks_(false) {
  std::string v;
  if (variant_ == ssl_variant_datagram &&
      version_ == SSL_LIBRARY_VERSION_TLS_1_1) {
    v = "1.0";
  } else {
    v = VersionString(version_);
  }
  std::cerr << "Version: " << variant_ << " " << v << std::endl;
}

TlsConnectTestBase::~TlsConnectTestBase() {}

// Check the group of each of the supported groups
void TlsConnectTestBase::CheckGroups(
    const DataBuffer& groups, std::function<void(SSLNamedGroup)> check_group) {
  DuplicateGroupChecker group_set;
  uint32_t tmp = 0;
  EXPECT_TRUE(groups.Read(0, 2, &tmp));
  EXPECT_EQ(groups.len() - 2, static_cast<size_t>(tmp));
  for (size_t i = 2; i < groups.len(); i += 2) {
    EXPECT_TRUE(groups.Read(i, 2, &tmp));
    SSLNamedGroup group = static_cast<SSLNamedGroup>(tmp);
    group_set.AddAndCheckGroup(group);
    check_group(group);
  }
}

// Check the group of each of the shares
void TlsConnectTestBase::CheckShares(
    const DataBuffer& shares, std::function<void(SSLNamedGroup)> check_group) {
  DuplicateGroupChecker group_set;
  uint32_t tmp = 0;
  EXPECT_TRUE(shares.Read(0, 2, &tmp));
  EXPECT_EQ(shares.len() - 2, static_cast<size_t>(tmp));
  size_t i;
  for (i = 2; i < shares.len(); i += 4 + tmp) {
    ASSERT_TRUE(shares.Read(i, 2, &tmp));
    SSLNamedGroup group = static_cast<SSLNamedGroup>(tmp);
    group_set.AddAndCheckGroup(group);
    check_group(group);
    ASSERT_TRUE(shares.Read(i + 2, 2, &tmp));
  }
  EXPECT_EQ(shares.len(), i);
}

void TlsConnectTestBase::CheckEpochs(uint16_t client_epoch,
                                     uint16_t server_epoch) const {
  client_->CheckEpochs(server_epoch, client_epoch);
  server_->CheckEpochs(client_epoch, server_epoch);
}

void TlsConnectTestBase::ClearStats() {
  // Clear statistics.
  SSL3Statistics* stats = SSL_GetStatistics();
  memset(stats, 0, sizeof(*stats));
}

void TlsConnectTestBase::ClearServerCache() {
  SSL_ShutdownServerSessionIDCache();
  SSLInt_ClearSelfEncryptKey();
  SSL_ConfigServerSessionIDCache(1024, 0, 0, g_working_dir_path.c_str());
}

void TlsConnectTestBase::SaveAlgorithmPolicy() {
  saved_policies_.clear();
  for (auto it = algorithms_.begin(); it != algorithms_.end(); ++it) {
    uint32_t policy;
    SECStatus rv = NSS_GetAlgorithmPolicy(*it, &policy);
    ASSERT_EQ(SECSuccess, rv);
    saved_policies_.push_back(std::make_tuple(*it, policy));
  }
  saved_options_.clear();
  for (auto it : options_) {
    int32_t option;
    SECStatus rv = NSS_OptionGet(it, &option);
    ASSERT_EQ(SECSuccess, rv);
    saved_options_.push_back(std::make_tuple(it, option));
  }
}

void TlsConnectTestBase::RestoreAlgorithmPolicy() {
  for (auto it = saved_policies_.begin(); it != saved_policies_.end(); ++it) {
    auto algorithm = std::get<0>(*it);
    auto policy = std::get<1>(*it);
    SECStatus rv = NSS_SetAlgorithmPolicy(
        algorithm, policy, NSS_USE_POLICY_IN_SSL | NSS_USE_ALG_IN_SSL_KX);
    ASSERT_EQ(SECSuccess, rv);
  }
  for (auto it = saved_options_.begin(); it != saved_options_.end(); ++it) {
    auto option_id = std::get<0>(*it);
    auto option = std::get<1>(*it);
    SECStatus rv = NSS_OptionSet(option_id, option);
    ASSERT_EQ(SECSuccess, rv);
  }
}

PRTime TlsConnectTestBase::TimeFunc(void* arg) {
  return *reinterpret_cast<PRTime*>(arg);
}

void TlsConnectTestBase::SetUp() {
  SSL_ConfigServerSessionIDCache(1024, 0, 0, g_working_dir_path.c_str());
  SSLInt_ClearSelfEncryptKey();
  now_ = PR_Now();
  ResetAntiReplay(kAntiReplayWindow);
  ClearStats();
  SaveAlgorithmPolicy();
  Init();
}

void TlsConnectTestBase::TearDown() {
  client_ = nullptr;
  server_ = nullptr;

  SSL_ClearSessionCache();
  SSLInt_ClearSelfEncryptKey();
  SSL_ShutdownServerSessionIDCache();
  RestoreAlgorithmPolicy();
}

void TlsConnectTestBase::Init() {
  client_->SetPeer(server_);
  server_->SetPeer(client_);

  if (version_) {
    ConfigureVersion(version_);
  }
}

void TlsConnectTestBase::ResetAntiReplay(PRTime window) {
  SSLAntiReplayContext* p_anti_replay = nullptr;
  EXPECT_EQ(SECSuccess,
            SSL_CreateAntiReplayContext(now_, window, 1, 3, &p_anti_replay));
  EXPECT_NE(nullptr, p_anti_replay);
  anti_replay_.reset(p_anti_replay);
}

ScopedSECItem TlsConnectTestBase::MakeEcKeyParams(SSLNamedGroup group) {
  auto groupDef = ssl_LookupNamedGroup(group);
  EXPECT_NE(nullptr, groupDef);

  auto oidData = SECOID_FindOIDByTag(groupDef->oidTag);
  EXPECT_NE(nullptr, oidData);
  ScopedSECItem params(
      SECITEM_AllocItem(nullptr, nullptr, (2 + oidData->oid.len)));
  EXPECT_TRUE(!!params);
  params->data[0] = SEC_ASN1_OBJECT_ID;
  params->data[1] = oidData->oid.len;
  memcpy(params->data + 2, oidData->oid.data, oidData->oid.len);
  return params;
}

void TlsConnectTestBase::GenerateEchConfig(
    HpkeKemId kem_id, const std::vector<HpkeSymmetricSuite>& cipher_suites,
    const std::string& public_name, uint16_t max_name_len, DataBuffer& record,
    ScopedSECKEYPublicKey& pubKey, ScopedSECKEYPrivateKey& privKey) {
  bool gen_keys = !pubKey && !privKey;

  SECKEYPublicKey* pub = nullptr;
  SECKEYPrivateKey* priv = nullptr;

  if (gen_keys) {
    ScopedSECItem ecParams = MakeEcKeyParams(ssl_grp_ec_curve25519);
    priv = SECKEY_CreateECPrivateKey(ecParams.get(), &pub, nullptr);
  } else {
    priv = privKey.get();
    pub = pubKey.get();
  }
  ASSERT_NE(nullptr, priv);
  PRUint8 encoded[1024];
  unsigned int encoded_len = 0;
  SECStatus rv = SSL_EncodeEchConfigId(
      77, public_name.c_str(), max_name_len, kem_id, pub, cipher_suites.data(),
      cipher_suites.size(), encoded, &encoded_len, sizeof(encoded));
  EXPECT_EQ(SECSuccess, rv);
  EXPECT_GT(encoded_len, 0U);

  if (gen_keys) {
    pubKey.reset(pub);
    privKey.reset(priv);
  }
  record.Truncate(0);
  record.Write(0, encoded, encoded_len);
}

void TlsConnectTestBase::SetupEch(std::shared_ptr<TlsAgent>& client,
                                  std::shared_ptr<TlsAgent>& server,
                                  HpkeKemId kem_id, bool expect_ech,
                                  bool set_client_config,
                                  bool set_server_config, int max_name_len,
                                  bool compress_xtns) {
  EXPECT_TRUE(set_server_config || set_client_config);
  ScopedSECKEYPublicKey pub;
  ScopedSECKEYPrivateKey priv;
  DataBuffer record;
  static const std::vector<HpkeSymmetricSuite> kDefaultSuites = {
      {HpkeKdfHkdfSha256, HpkeAeadChaCha20Poly1305},
      {HpkeKdfHkdfSha256, HpkeAeadAes128Gcm}};

  GenerateEchConfig(kem_id, kDefaultSuites, "public.name", max_name_len, record,
                    pub, priv);
  ASSERT_NE(0U, record.len());
  SECStatus rv;
  if (set_server_config) {
    rv = SSL_SetServerEchConfigs(server->ssl_fd(), pub.get(), priv.get(),
                                 record.data(), record.len());
    ASSERT_EQ(SECSuccess, rv);
  }
  if (set_client_config) {
    rv = SSL_SetClientEchConfigs(client->ssl_fd(), record.data(), record.len());
    ASSERT_EQ(SECSuccess, rv);
    if (!compress_xtns) {
      rv = SSLInt_SetEnableEchXtnCompression(client->ssl_fd(), PR_FALSE);
      ASSERT_EQ(SECSuccess, rv);
    }
  }

  /* Filter expect_ech, which typically defaults to true. Parameterized tests
   * running DTLS or TLS < 1.3 should expect only a non-ECH result. */
  bool expect = expect_ech && variant_ != ssl_variant_datagram &&
                version_ >= SSL_LIBRARY_VERSION_TLS_1_3 && set_client_config &&
                set_server_config;
  client->ExpectEch(expect);
  server->ExpectEch(expect);
}

void TlsConnectTestBase::Reset() {
  // Take a copy of the names because they are about to disappear.
  std::string server_name = server_->name();
  std::string client_name = client_->name();
  Reset(server_name, client_name);
}

void TlsConnectTestBase::Reset(const std::string& server_name,
                               const std::string& client_name) {
  auto token = client_->GetResumptionToken();
  client_.reset(new TlsAgent(client_name, TlsAgent::CLIENT, variant_));
  client_->SetResumptionToken(token);
  server_.reset(new TlsAgent(server_name, TlsAgent::SERVER, variant_));
  if (skip_version_checks_) {
    client_->SkipVersionChecks();
    server_->SkipVersionChecks();
  }

  std::cerr << "Reset server:" << server_name << ", client:" << client_name
            << std::endl;
  Init();
}

void TlsConnectTestBase::MakeNewServer() {
  auto replacement = std::make_shared<TlsAgent>(
      server_->name(), TlsAgent::SERVER, server_->variant());
  server_ = replacement;
  if (version_) {
    server_->SetVersionRange(version_, version_);
  }
  client_->SetPeer(server_);
  server_->SetPeer(client_);
  server_->StartConnect();
}

void TlsConnectTestBase::ExpectResumption(SessionResumptionMode expected,
                                          uint8_t num_resumptions) {
  expected_resumption_mode_ = expected;
  if (expected != RESUME_NONE) {
    client_->ExpectResumption();
    server_->ExpectResumption();
    expected_resumptions_ = num_resumptions;
  }
  EXPECT_EQ(expected_resumptions_ == 0, expected == RESUME_NONE);
}

void TlsConnectTestBase::EnsureTlsSetup() {
  EXPECT_TRUE(server_->EnsureTlsSetup(
      server_model_ ? server_model_->ssl_fd().get() : nullptr));
  EXPECT_TRUE(client_->EnsureTlsSetup(
      client_model_ ? client_model_->ssl_fd().get() : nullptr));
  server_->SetAntiReplayContext(anti_replay_);
  EXPECT_EQ(SECSuccess, SSL_SetTimeFunc(client_->ssl_fd(),
                                        TlsConnectTestBase::TimeFunc, &now_));
  EXPECT_EQ(SECSuccess, SSL_SetTimeFunc(server_->ssl_fd(),
                                        TlsConnectTestBase::TimeFunc, &now_));
}

void TlsConnectTestBase::Handshake() {
  client_->SetServerKeyBits(server_->server_key_bits());
  client_->Handshake();
  server_->Handshake();

  ASSERT_TRUE_WAIT((client_->state() != TlsAgent::STATE_CONNECTING) &&
                       (server_->state() != TlsAgent::STATE_CONNECTING),
                   5000);
}

void TlsConnectTestBase::EnableExtendedMasterSecret() {
  client_->EnableExtendedMasterSecret();
  server_->EnableExtendedMasterSecret();
  ExpectExtendedMasterSecret(true);
}

void TlsConnectTestBase::Connect() {
  StartConnect();
  client_->MaybeSetResumptionToken();
  Handshake();
  CheckConnected();
}

void TlsConnectTestBase::StartConnect() {
  EnsureTlsSetup();
  server_->StartConnect();
  client_->StartConnect();
}

void TlsConnectTestBase::ConnectWithCipherSuite(uint16_t cipher_suite) {
  EnsureTlsSetup();
  client_->EnableSingleCipher(cipher_suite);

  Connect();
  SendReceive();

  // Check that we used the right cipher suite.
  uint16_t actual;
  EXPECT_TRUE(client_->cipher_suite(&actual));
  EXPECT_EQ(cipher_suite, actual);
  EXPECT_TRUE(server_->cipher_suite(&actual));
  EXPECT_EQ(cipher_suite, actual);
}

void TlsConnectTestBase::CheckConnected() {
  // Read until the client processes the ACK and releases its handshake cipher
  // specs.  If the server retransmits its HS flight (e.g. due to timer expiry),
  // out-of-epoch epoch2 records precede the NST+ACK in the client's buffer and
  // are silently dropped one per Handshake() call.  The server flight has 5
  // records, so 10 iterations is enough to handle one retransmit with margin.
  if (client_->version() >= SSL_LIBRARY_VERSION_TLS_1_3 &&
      variant_ == ssl_variant_datagram) {
    static const int kMaxDtlsHandshakeDrainIterations = 10;
    for (int i = 0; i < kMaxDtlsHandshakeDrainIterations; i++) {
      client_->Handshake();
      if (SSLInt_CountCipherSpecs(client_->ssl_fd()) == 2) break;
    }
    auto suites = SSLInt_CountCipherSpecs(client_->ssl_fd());
    // Verify that we dropped the client's retransmission cipher suites.
    EXPECT_EQ(2, suites) << "Client has the wrong number of suites";
    if (suites != 2) {
      SSLInt_PrintCipherSpecs("client", client_->ssl_fd());
    }
  }
  EXPECT_EQ(client_->version(), server_->version());
  if (!skip_version_checks_) {
    // Check the version is as expected
    EXPECT_EQ(std::min(client_->max_version(), server_->max_version()),
              client_->version());
  }

  EXPECT_EQ(TlsAgent::STATE_CONNECTED, client_->state());
  EXPECT_EQ(TlsAgent::STATE_CONNECTED, server_->state());

  uint16_t cipher_suite1, cipher_suite2;
  ASSERT_TRUE(client_->cipher_suite(&cipher_suite1));
  ASSERT_TRUE(server_->cipher_suite(&cipher_suite2));
  EXPECT_EQ(cipher_suite1, cipher_suite2);

  std::cerr << "Connected with version " << client_->version()
            << " cipher suite " << client_->cipher_suite_name() << std::endl;

  if (client_->version() < SSL_LIBRARY_VERSION_TLS_1_3) {
    // Check and store session ids.
    std::vector<uint8_t> sid_c1 = client_->session_id();
    EXPECT_EQ(32U, sid_c1.size());
    std::vector<uint8_t> sid_s1 = server_->session_id();
    EXPECT_EQ(32U, sid_s1.size());
    EXPECT_EQ(sid_c1, sid_s1);
    session_ids_.push_back(sid_c1);
  }

  CheckExtendedMasterSecret();
  CheckEarlyDataAccepted();
  CheckResumption(expected_resumption_mode_);
  client_->CheckSecretsDestroyed();
  server_->CheckSecretsDestroyed();
}

void TlsConnectTestBase::CheckEarlyDataLimit(
    const std::shared_ptr<TlsAgent>& agent, size_t expected_size) {
  SSLPreliminaryChannelInfo preinfo;
  SECStatus rv =
      SSL_GetPreliminaryChannelInfo(agent->ssl_fd(), &preinfo, sizeof(preinfo));
  EXPECT_EQ(SECSuccess, rv);
  EXPECT_EQ(expected_size, static_cast<size_t>(preinfo.maxEarlyDataSize));
}

SSLKEAType TlsConnectTestBase::GetDefaultKEA(void) const {
  if (version_ >= SSL_LIBRARY_VERSION_TLS_1_3) {
    return ssl_kea_ecdh_hybrid;
  }
  return ssl_kea_ecdh;
}

SSLAuthType TlsConnectTestBase::GetDefaultAuth(void) const {
  return ssl_auth_rsa_sign;
}

SSLNamedGroup TlsConnectTestBase::GetDefaultGroupFromKEA(
    SSLKEAType kea_type) const {
  SSLNamedGroup group;
  switch (kea_type) {
    case ssl_kea_ecdh_hybrid:
      group = ssl_grp_kem_mlkem768x25519;
      break;
    case ssl_kea_ecdh:
      group = ssl_grp_ec_curve25519;
      break;
    case ssl_kea_dh:
      group = ssl_grp_ffdhe_2048;
      break;
    case ssl_kea_rsa:
      group = ssl_grp_none;
      break;
    default:
      EXPECT_TRUE(false) << "unexpected KEA";
      group = ssl_grp_none;
      break;
  }
  return group;
}

SSLSignatureScheme TlsConnectTestBase::GetDefaultSchemeFromAuth(
    SSLAuthType auth_type) const {
  SSLSignatureScheme scheme;
  switch (auth_type) {
    case ssl_auth_rsa_decrypt:
      scheme = ssl_sig_none;
      break;
    case ssl_auth_rsa_sign:
      if (version_ >= SSL_LIBRARY_VERSION_TLS_1_2) {
        scheme = ssl_sig_rsa_pss_rsae_sha256;
      } else {
        scheme = ssl_sig_rsa_pkcs1_sha256;
      }
      break;
    case ssl_auth_rsa_pss:
      scheme = ssl_sig_rsa_pss_rsae_sha256;
      break;
    case ssl_auth_ecdsa:
      scheme = ssl_sig_ecdsa_secp256r1_sha256;
      break;
    case ssl_auth_dsa:
      scheme = ssl_sig_dsa_sha1;
      break;
    default:
      EXPECT_TRUE(false) << "unexpected auth type";
      scheme = static_cast<SSLSignatureScheme>(0x0100);
      break;
  }
  return scheme;
}

void TlsConnectTestBase::CheckKeys(SSLKEAType kea_type, SSLNamedGroup kea_group,
                                   SSLAuthType auth_type,
                                   SSLSignatureScheme sig_scheme) const {
  if (kea_group != ssl_grp_none) {
    client_->CheckKEA(kea_type, kea_group);
    server_->CheckKEA(kea_type, kea_group);
  }
  server_->CheckAuthType(auth_type, sig_scheme);
  client_->CheckAuthType(auth_type, sig_scheme);
}

void TlsConnectTestBase::CheckKeys(SSLKEAType kea_type,
                                   SSLNamedGroup kea_group) const {
  SSLAuthType auth_type = GetDefaultAuth();
  SSLSignatureScheme scheme = GetDefaultSchemeFromAuth(auth_type);
  CheckKeys(kea_type, kea_group, auth_type, scheme);
}

void TlsConnectTestBase::CheckKeys(SSLAuthType auth_type,
                                   SSLSignatureScheme sig_scheme) const {
  SSLKEAType kea_type = GetDefaultKEA();
  SSLNamedGroup group = GetDefaultGroupFromKEA(kea_type);
  CheckKeys(kea_type, group, auth_type, sig_scheme);
}

void TlsConnectTestBase::CheckKeys(SSLKEAType kea_type,
                                   SSLAuthType auth_type) const {
  SSLNamedGroup group = GetDefaultGroupFromKEA(kea_type);
  SSLSignatureScheme scheme = GetDefaultSchemeFromAuth(auth_type);

  CheckKeys(kea_type, group, auth_type, scheme);
}

void TlsConnectTestBase::CheckKeys(SSLKEAType kea_type) const {
  SSLAuthType auth_type = GetDefaultAuth();
  CheckKeys(kea_type, auth_type);
}

void TlsConnectTestBase::CheckKeys(SSLAuthType auth_type) const {
  SSLKEAType kea_type = GetDefaultKEA();
  CheckKeys(kea_type, auth_type);
}

void TlsConnectTestBase::CheckKeys() const {
  SSLKEAType kea_type = GetDefaultKEA();
  SSLAuthType auth_type = GetDefaultAuth();
  CheckKeys(kea_type, auth_type);
}

void TlsConnectTestBase::CheckKeysResumption(SSLKEAType kea_type,
                                             SSLNamedGroup kea_group,
                                             SSLNamedGroup original_kea_group,
                                             SSLAuthType auth_type,
                                             SSLSignatureScheme sig_scheme) {
  CheckKeys(kea_type, kea_group, auth_type, sig_scheme);
  EXPECT_TRUE(expected_resumption_mode_ != RESUME_NONE);
  client_->CheckOriginalKEA(original_kea_group);
  server_->CheckOriginalKEA(original_kea_group);
}

void TlsConnectTestBase::ConnectExpectFail() {
  StartConnect();
  Handshake();
  ASSERT_EQ(TlsAgent::STATE_ERROR, client_->state());
  ASSERT_EQ(TlsAgent::STATE_ERROR, server_->state());
}

void TlsConnectTestBase::ExpectAlert(std::shared_ptr<TlsAgent>& sender,
                                     uint8_t alert) {
  EnsureTlsSetup();
  auto receiver = (sender == client_) ? server_ : client_;
  sender->ExpectSendAlert(alert);
  receiver->ExpectReceiveAlert(alert);
}

void TlsConnectTestBase::ConnectExpectAlert(std::shared_ptr<TlsAgent>& sender,
                                            uint8_t alert) {
  ExpectAlert(sender, alert);
  ConnectExpectFail();
}

void TlsConnectTestBase::ConnectExpectFailOneSide(TlsAgent::Role failing_side) {
  StartConnect();
  client_->SetServerKeyBits(server_->server_key_bits());
  client_->Handshake();
  server_->Handshake();

  auto failing_agent = server_;
  if (failing_side == TlsAgent::CLIENT) {
    failing_agent = client_;
  }
  ASSERT_TRUE_WAIT(failing_agent->state() == TlsAgent::STATE_ERROR, 5000);
}

void TlsConnectTestBase::ConfigureVersion(uint16_t version) {
  version_ = version;
  client_->SetVersionRange(version, version);
  server_->SetVersionRange(version, version);
}

void TlsConnectTestBase::SetExpectedVersion(uint16_t version) {
  client_->SetExpectedVersion(version);
  server_->SetExpectedVersion(version);
}

void TlsConnectTestBase::AddPsk(const ScopedPK11SymKey& psk, std::string label,
                                SSLHashType hash, uint16_t zeroRttSuite) {
  client_->AddPsk(psk, label, hash, zeroRttSuite);
  server_->AddPsk(psk, label, hash, zeroRttSuite);
  client_->ExpectPsk();
  server_->ExpectPsk();
}

void TlsConnectTestBase::DisableAllCiphers() {
  EnsureTlsSetup();
  client_->DisableAllCiphers();
  server_->DisableAllCiphers();
}

void TlsConnectTestBase::EnableOnlyStaticRsaCiphers() {
  DisableAllCiphers();

  client_->EnableCiphersByKeyExchange(ssl_kea_rsa);
  server_->EnableCiphersByKeyExchange(ssl_kea_rsa);
}

void TlsConnectTestBase::EnableOnlyDheCiphers() {
  if (version_ < SSL_LIBRARY_VERSION_TLS_1_3) {
    DisableAllCiphers();
    client_->EnableCiphersByKeyExchange(ssl_kea_dh);
    server_->EnableCiphersByKeyExchange(ssl_kea_dh);
  } else {
    client_->ConfigNamedGroups(kFFDHEGroups);
    server_->ConfigNamedGroups(kFFDHEGroups);
  }
}

void TlsConnectTestBase::EnableSomeEcdhCiphers() {
  if (version_ < SSL_LIBRARY_VERSION_TLS_1_3) {
    client_->EnableCiphersByAuthType(ssl_auth_ecdh_rsa);
    client_->EnableCiphersByAuthType(ssl_auth_ecdh_ecdsa);
    server_->EnableCiphersByAuthType(ssl_auth_ecdh_rsa);
    server_->EnableCiphersByAuthType(ssl_auth_ecdh_ecdsa);
  } else {
    client_->ConfigNamedGroups(kECDHEGroups);
    server_->ConfigNamedGroups(kECDHEGroups);
  }
}

void TlsConnectTestBase::ConfigureSelfEncrypt() {
  ScopedCERTCertificate cert;
  ScopedSECKEYPrivateKey privKey;
  ASSERT_TRUE(
      TlsAgent::LoadCertificate(TlsAgent::kServerRsaDecrypt, &cert, &privKey));

  ScopedSECKEYPublicKey pubKey(CERT_ExtractPublicKey(cert.get()));
  ASSERT_TRUE(pubKey);

  EXPECT_EQ(SECSuccess,
            SSL_SetSessionTicketKeyPair(pubKey.get(), privKey.get()));
}

void TlsConnectTestBase::ConfigureSessionCache(SessionResumptionMode client,
                                               SessionResumptionMode server) {
  client_->ConfigureSessionCache(client);
  server_->ConfigureSessionCache(server);
  if ((server & RESUME_TICKET) != 0) {
    ConfigureSelfEncrypt();
  }
}

void TlsConnectTestBase::CheckResumption(SessionResumptionMode expected) {
  EXPECT_NE(RESUME_BOTH, expected);

  int resume_count = expected ? expected_resumptions_ : 0;
  int stateless_count = (expected & RESUME_TICKET) ? expected_resumptions_ : 0;

  // Note: hch == server counter; hsh == client counter.
  SSL3Statistics* stats = SSL_GetStatistics();
  EXPECT_EQ(resume_count, stats->hch_sid_cache_hits);
  EXPECT_EQ(resume_count, stats->hsh_sid_cache_hits);

  EXPECT_EQ(stateless_count, stats->hch_sid_stateless_resumes);
  EXPECT_EQ(stateless_count, stats->hsh_sid_stateless_resumes);

  if (expected != RESUME_NONE) {
    if (client_->version() < SSL_LIBRARY_VERSION_TLS_1_3 &&
        client_->GetResumptionToken().size() == 0) {
      // Check that the last two session ids match.
      ASSERT_EQ(1U + expected_resumptions_, session_ids_.size());
      EXPECT_EQ(session_ids_[session_ids_.size() - 1],
                session_ids_[session_ids_.size() - 2]);
    } else {
      // We've either chosen TLS 1.3 or are using an external resumption token,
      // both of which only use tickets.
      EXPECT_TRUE(expected & RESUME_TICKET);
    }
  }
}

static SECStatus NextProtoCallbackServer(void* arg, PRFileDesc* fd,
                                         const unsigned char* protos,
                                         unsigned int protos_len,
                                         unsigned char* protoOut,
                                         unsigned int* protoOutLen,
                                         unsigned int protoMaxLen) {
  EXPECT_EQ(protoMaxLen, 255U);
  TlsAgent* agent = reinterpret_cast<TlsAgent*>(arg);
  // Check that agent->alpn_value_to_use_ is in protos.
  if (protos_len < 1) {
    return SECFailure;
  }
  for (size_t i = 0; i < protos_len;) {
    size_t l = protos[i];
    EXPECT_LT(i + l, protos_len);
    if (i + l >= protos_len) {
      return SECFailure;
    }
    std::string protos_s(reinterpret_cast<const char*>(protos + i + 1), l);
    if (protos_s == agent->alpn_value_to_use_) {
      size_t s_len = agent->alpn_value_to_use_.size();
      EXPECT_LE(s_len, 255U);
      memcpy(protoOut, &agent->alpn_value_to_use_[0], s_len);
      *protoOutLen = s_len;
      return SECSuccess;
    }
    i += l + 1;
  }
  return SECFailure;
}

void TlsConnectTestBase::EnableAlpn() {
  client_->EnableAlpn(alpn_dummy_val_, sizeof(alpn_dummy_val_));
  server_->EnableAlpn(alpn_dummy_val_, sizeof(alpn_dummy_val_));
}

void TlsConnectTestBase::EnableAlpnWithCallback(
    const std::vector<uint8_t>& client_vals, std::string server_choice) {
  EnsureTlsSetup();
  server_->alpn_value_to_use_ = server_choice;
  EXPECT_EQ(SECSuccess,
            SSL_SetNextProtoNego(client_->ssl_fd(), client_vals.data(),
                                 client_vals.size()));
  SECStatus rv = SSL_SetNextProtoCallback(
      server_->ssl_fd(), NextProtoCallbackServer, server_.get());
  EXPECT_EQ(SECSuccess, rv);
}

void TlsConnectTestBase::EnableAlpn(const std::vector<uint8_t>& vals) {
  client_->EnableAlpn(vals.data(), vals.size());
  server_->EnableAlpn(vals.data(), vals.size());
}

void TlsConnectTestBase::EnsureModelSockets() {
  // Make sure models agents are available.
  if (!client_model_) {
    ASSERT_EQ(server_model_, nullptr);
    client_model_.reset(
        new TlsAgent(TlsAgent::kClient, TlsAgent::CLIENT, variant_));
    server_model_.reset(
        new TlsAgent(TlsAgent::kServerRsa, TlsAgent::SERVER, variant_));
    if (skip_version_checks_) {
      client_model_->SkipVersionChecks();
      server_model_->SkipVersionChecks();
    }
  }
}

void TlsConnectTestBase::CheckAlpn(const std::string& val) {
  client_->CheckAlpn(SSL_NEXT_PROTO_SELECTED, val);
  server_->CheckAlpn(SSL_NEXT_PROTO_NEGOTIATED, val);
}

void TlsConnectTestBase::EnableSrtp() {
  client_->EnableSrtp();
  server_->EnableSrtp();
}

void TlsConnectTestBase::CheckSrtp() const {
  client_->CheckSrtp();
  server_->CheckSrtp();
}

void TlsConnectTestBase::SendReceive(size_t total) {
  ASSERT_GT(total, client_->received_bytes());
  ASSERT_GT(total, server_->received_bytes());
  client_->SendData(total - server_->received_bytes());
  server_->SendData(total - client_->received_bytes());
  Receive(total);  // Receive() is cumulative
}

// Do a first connection so we can do 0-RTT on the second one.
void TlsConnectTestBase::SetupForZeroRtt() {
  // Force rollover of the anti-replay window.
  // If we don't do this, then all 0-RTT attempts will be rejected.
  RolloverAntiReplay();

  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  ConfigureVersion(SSL_LIBRARY_VERSION_TLS_1_3);
  server_->Set0RttEnabled(true);  // So we signal that we allow 0-RTT.
  Connect();
  SendReceive();  // Need to read so that we absorb the session ticket.
  CheckKeys();

  Reset();
  StartConnect();
}

// Do a first connection so we can do resumption
void TlsConnectTestBase::SetupForResume() {
  EnsureTlsSetup();
  ConfigureSessionCache(RESUME_BOTH, RESUME_TICKET);
  Connect();
  SendReceive();  // Need to read so that we absorb the session ticket.
  CheckKeys();

  Reset();
}

void TlsConnectTestBase::ZeroRttSendReceive(
    bool expect_writable, bool expect_readable,
    std::function<bool()> post_clienthello_check) {
  const char* k0RttData = "ABCDEF";
  const PRInt32 k0RttDataLen = static_cast<PRInt32>(strlen(k0RttData));

  client_->Handshake();  // Send ClientHello.
  if (post_clienthello_check) {
    if (!post_clienthello_check()) return;
  }
  PRInt32 rv =
      PR_Write(client_->ssl_fd(), k0RttData, k0RttDataLen);  // 0-RTT write.
  if (expect_writable) {
    EXPECT_EQ(k0RttDataLen, rv);
  } else {
    EXPECT_EQ(SECFailure, rv);
  }
  server_->Handshake();  // Consume ClientHello

  std::vector<uint8_t> buf(k0RttDataLen);
  rv = PR_Read(server_->ssl_fd(), buf.data(), k0RttDataLen);  // 0-RTT read
  if (expect_readable) {
    std::cerr << "0-RTT read " << rv << " bytes\n";
    EXPECT_EQ(k0RttDataLen, rv);
  } else {
    EXPECT_EQ(SECFailure, rv);
    EXPECT_EQ(PR_WOULD_BLOCK_ERROR, PORT_GetError())
        << "Unexpected error: " << PORT_ErrorToName(PORT_GetError());
  }

  // Do a second read. This should fail.
  rv = PR_Read(server_->ssl_fd(), buf.data(), k0RttDataLen);
  EXPECT_EQ(SECFailure, rv);
  EXPECT_EQ(PR_WOULD_BLOCK_ERROR, PORT_GetError());
}

void TlsConnectTestBase::Receive(size_t amount) {
  WAIT_(client_->received_bytes() == amount &&
            server_->received_bytes() == amount,
        2000);
  ASSERT_EQ(amount, client_->received_bytes());
  ASSERT_EQ(amount, server_->received_bytes());
}

void TlsConnectTestBase::ExpectExtendedMasterSecret(bool expected) {
  expect_extended_master_secret_ = expected;
}

void TlsConnectTestBase::CheckExtendedMasterSecret() {
  client_->CheckExtendedMasterSecret(expect_extended_master_secret_);
  server_->CheckExtendedMasterSecret(expect_extended_master_secret_);
}

void TlsConnectTestBase::ExpectEarlyDataAccepted(bool expected) {
  expect_early_data_accepted_ = expected;
}

void TlsConnectTestBase::CheckEarlyDataAccepted() {
  client_->CheckEarlyDataAccepted(expect_early_data_accepted_);
  server_->CheckEarlyDataAccepted(expect_early_data_accepted_);
}

void TlsConnectTestBase::EnableECDHEServerKeyReuse() {
  server_->EnableECDHEServerKeyReuse();
}

void TlsConnectTestBase::SkipVersionChecks() {
  skip_version_checks_ = true;
  client_->SkipVersionChecks();
  server_->SkipVersionChecks();
}

// Shift the DTLS timers, to the minimum time necessary to let the next timer
// run on either client or server.  This allows tests to skip waiting without
// having timers run out of order.
void TlsConnectTestBase::ShiftDtlsTimers() {
  PRIntervalTime time_shift = PR_INTERVAL_NO_TIMEOUT;
  PRIntervalTime time;
  SECStatus rv = DTLS_GetHandshakeTimeout(client_->ssl_fd(), &time);
  if (rv == SECSuccess) {
    time_shift = time;
  }
  rv = DTLS_GetHandshakeTimeout(server_->ssl_fd(), &time);
  if (rv == SECSuccess &&
      (time < time_shift || time_shift == PR_INTERVAL_NO_TIMEOUT)) {
    time_shift = time;
  }

  if (time_shift != PR_INTERVAL_NO_TIMEOUT) {
    AdvanceTime(PR_IntervalToMicroseconds(time_shift));
    EXPECT_EQ(SECSuccess,
              SSLInt_ShiftDtlsTimers(client_->ssl_fd(), time_shift));
    EXPECT_EQ(SECSuccess,
              SSLInt_ShiftDtlsTimers(server_->ssl_fd(), time_shift));
  }
}

void TlsConnectTestBase::AdvanceTime(PRTime time_shift) { now_ += time_shift; }

// Advance time by a full anti-replay window.
void TlsConnectTestBase::RolloverAntiReplay() {
  AdvanceTime(kAntiReplayWindow);
}

TlsConnectGeneric::TlsConnectGeneric()
    : TlsConnectTestBase(std::get<0>(GetParam()), std::get<1>(GetParam())) {}

TlsConnectPre12::TlsConnectPre12()
    : TlsConnectTestBase(std::get<0>(GetParam()), std::get<1>(GetParam())) {}

TlsConnectTls12::TlsConnectTls12()
    : TlsConnectTestBase(GetParam(), SSL_LIBRARY_VERSION_TLS_1_2) {}

TlsConnectTls12Plus::TlsConnectTls12Plus()
    : TlsConnectTestBase(std::get<0>(GetParam()), std::get<1>(GetParam())) {}

TlsConnectTls13::TlsConnectTls13()
    : TlsConnectTestBase(GetParam(), SSL_LIBRARY_VERSION_TLS_1_3) {}

TlsConnectGenericResumption::TlsConnectGenericResumption()
    : TlsConnectTestBase(std::get<0>(GetParam()), std::get<1>(GetParam())),
      external_cache_(std::get<2>(GetParam())) {}

void SetTokenShape(PRFileDesc* fd, TokenShape shape) {
  EXPECT_EQ(SECSuccess,
            SSL_EnablePlaintextResumptionExport(
                fd, shape == TokenShape::kPlaintext ? PR_TRUE : PR_FALSE));
}

TlsConnectTls13ResumptionToken::TlsConnectTls13ResumptionToken()
    : TlsConnectTestBase(std::get<0>(GetParam()), SSL_LIBRARY_VERSION_TLS_1_3),
      shape_(std::get<1>(GetParam())) {}

TlsConnectGenericResumptionToken::TlsConnectGenericResumptionToken()
    : TlsConnectTestBase(std::get<0>(GetParam()), std::get<1>(GetParam())) {}

TlsConnectResumptionTokenShape::TlsConnectResumptionTokenShape()
    : TlsConnectTestBase(std::get<0>(GetParam()), std::get<1>(GetParam())),
      shape_(std::get<2>(GetParam())) {}

void TlsKeyExchangeTest::EnsureKeyShareSetup() {
  EnsureTlsSetup();
  groups_capture_ =
      std::make_shared<TlsExtensionCapture>(client_, ssl_supported_groups_xtn);
  shares_capture_ =
      std::make_shared<TlsExtensionCapture>(client_, ssl_tls13_key_share_xtn);
  shares_capture2_ = std::make_shared<TlsExtensionCapture>(
      client_, ssl_tls13_key_share_xtn, true);
  std::vector<std::shared_ptr<PacketFilter>> captures = {
      groups_capture_, shares_capture_, shares_capture2_};
  client_->SetFilter(std::make_shared<ChainedPacketFilter>(captures));
  capture_hrr_ = MakeTlsFilter<TlsHandshakeRecorder>(
      server_, kTlsHandshakeHelloRetryRequest);
}

void TlsKeyExchangeTest::ConfigNamedGroups(
    const std::vector<SSLNamedGroup>& groups) {
  client_->ConfigNamedGroups(groups);
  server_->ConfigNamedGroups(groups);
}

std::vector<SSLNamedGroup> TlsKeyExchangeTest::GetGroupDetails(
    const std::shared_ptr<TlsExtensionCapture>& capture) {
  EXPECT_TRUE(capture->captured());
  const DataBuffer& ext = capture->extension();

  uint32_t tmp = 0;
  EXPECT_TRUE(ext.Read(0, 2, &tmp));
  EXPECT_EQ(ext.len() - 2, static_cast<size_t>(tmp));
  EXPECT_TRUE(ext.len() % 2 == 0);

  std::vector<SSLNamedGroup> groups;
  for (size_t i = 1; i < ext.len() / 2; i += 1) {
    EXPECT_TRUE(ext.Read(2 * i, 2, &tmp));
    groups.push_back(static_cast<SSLNamedGroup>(tmp));
  }
  return groups;
}

std::vector<SSLNamedGroup> TlsKeyExchangeTest::GetShareDetails(
    const std::shared_ptr<TlsExtensionCapture>& capture) {
  EXPECT_TRUE(capture->captured());
  const DataBuffer& ext = capture->extension();

  uint32_t tmp = 0;
  EXPECT_TRUE(ext.Read(0, 2, &tmp));
  EXPECT_EQ(ext.len() - 2, static_cast<size_t>(tmp));

  std::vector<SSLNamedGroup> shares;
  size_t i = 2;
  while (i < ext.len()) {
    EXPECT_TRUE(ext.Read(i, 2, &tmp));
    shares.push_back(static_cast<SSLNamedGroup>(tmp));
    EXPECT_TRUE(ext.Read(i + 2, 2, &tmp));
    i += 4 + tmp;
  }
  EXPECT_EQ(ext.len(), i);
  return shares;
}

void TlsKeyExchangeTest::CheckKEXDetails(
    const std::vector<SSLNamedGroup>& expected_groups,
    const std::vector<SSLNamedGroup>& expected_shares, bool expect_hrr) {
  std::vector<SSLNamedGroup> groups = GetGroupDetails(groups_capture_);
  EXPECT_EQ(expected_groups, groups);

  if (version_ >= SSL_LIBRARY_VERSION_TLS_1_3) {
    ASSERT_LT(0U, expected_shares.size());
    std::vector<SSLNamedGroup> shares = GetShareDetails(shares_capture_);
    EXPECT_EQ(expected_shares, shares);
  } else {
    EXPECT_FALSE(shares_capture_->captured());
  }

  EXPECT_EQ(expect_hrr, capture_hrr_->buffer().len() != 0);
}

void TlsKeyExchangeTest::CheckKEXDetails(
    const std::vector<SSLNamedGroup>& expected_groups,
    const std::vector<SSLNamedGroup>& expected_shares) {
  CheckKEXDetails(expected_groups, expected_shares, false);
}

void TlsKeyExchangeTest::CheckKEXDetails(
    const std::vector<SSLNamedGroup>& expected_groups,
    const std::vector<SSLNamedGroup>& expected_shares,
    SSLNamedGroup expected_share2) {
  CheckKEXDetails(expected_groups, expected_shares, true);

  for (auto it : expected_shares) {
    EXPECT_NE(expected_share2, it);
  }
  std::vector<SSLNamedGroup> expected_shares2 = {expected_share2};
  EXPECT_EQ(expected_shares2, GetShareDetails(shares_capture2_));
}
}  // namespace nss_test
