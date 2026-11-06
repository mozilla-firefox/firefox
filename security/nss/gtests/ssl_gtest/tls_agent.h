/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef tls_agent_h_
#define tls_agent_h_

#include "prio.h"
#include "ssl.h"
#include "sslproto.h"

#include <functional>
#include <iostream>

#include "nss_policy.h"
#include "test_io.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "nss_scoped_ptrs.h"
#include "scoped_ptrs_ssl.h"

extern bool g_ssl_gtest_verbose;

namespace nss_test {

#define LOG(msg) std::cerr << role_str() << ": " << msg << std::endl
#define LOGV(msg)                      \
  do {                                 \
    if (g_ssl_gtest_verbose) LOG(msg); \
  } while (false)

enum SessionResumptionMode {
  RESUME_NONE = 0,
  RESUME_SESSIONID = 1,
  RESUME_TICKET = 2,
  RESUME_BOTH = RESUME_SESSIONID | RESUME_TICKET
};

enum class ClientAuthCallbackType {
  kAsyncImmediate,
  kAsyncDelay,
  kSync,
  kNone,
};

class PacketFilter;
class TlsAgent;
class TlsCipherSpec;
struct TlsRecord;

const extern std::vector<SSLNamedGroup> kAllDHEGroups;
const extern std::vector<SSLNamedGroup> kNonPQDHEGroups;
const extern std::vector<SSLNamedGroup> kECDHEGroups;
const extern std::vector<SSLNamedGroup> kFFDHEGroups;
const extern std::vector<SSLNamedGroup> kFasterDHEGroups;
const extern std::vector<SSLNamedGroup> kEcdhHybridGroups;

// These functions are called from callbacks.  They use bare pointers because
// TlsAgent sets up the callback and it doesn't know who owns it.
typedef std::function<SECStatus(TlsAgent* agent, bool checksig, bool isServer)>
    AuthCertificateCallbackFunction;

typedef std::function<void(TlsAgent* agent)> HandshakeCallbackFunction;

typedef std::function<int32_t(TlsAgent* agent, const SECItem* srvNameArr,
                              PRUint32 srvNameArrSize)>
    SniCallbackFunction;

class TlsAgent : public PollTarget {
 public:
  enum Role { CLIENT, SERVER };
  enum State { STATE_INIT, STATE_CONNECTING, STATE_CONNECTED, STATE_ERROR };

  static const std::string kClient;     // the client key is sign only
  static const std::string kRsa2048;    // bigger sign and encrypt for either
  static const std::string kRsa8192;    // biggest sign and encrypt for either
  static const std::string kServerRsa;  // both sign and encrypt
  static const std::string kServerRsaSign;
  static const std::string kServerRsaPss;
  static const std::string kServerRsaDecrypt;
  static const std::string kServerEcdsa256;
  static const std::string kServerEcdsa384;
  static const std::string kServerEcdsa521;
  static const std::string kServerEcdhEcdsa;
  static const std::string kServerEcdhRsa;
  static const std::string kServerDsa;
  static const std::string kServerMlDsa44;
  static const std::string kServerMlDsa65;
  static const std::string kServerMlDsa87;
  static const std::string kServerUnknownExt;
  static const std::string kServerUnknownCriticalExt;
  static const std::string kDelegatorEcdsa256;    // draft-ietf-tls-subcerts
  static const std::string kDelegatorRsae2048;    // draft-ietf-tls-subcerts
  static const std::string kDelegatorRsaPss2048;  // draft-ietf-tls-subcerts
  static const std::string kDelegatorMlDsa44;     // draft-ietf-tls-subcerts
  static const std::string kDelegatorMlDsa65;     // draft-ietf-tls-subcerts
  static const std::string kDelegatorMlDsa87;     // draft-ietf-tls-subcerts

  TlsAgent(const std::string& name, Role role, SSLProtocolVariant variant);
  virtual ~TlsAgent();

  void SetPeer(std::shared_ptr<TlsAgent>& peer) {
    adapter_->SetPeer(peer->adapter_);
  }

  void SetFilter(std::shared_ptr<PacketFilter> filter) {
    adapter_->SetPacketFilter(filter);
  }
  void ClearFilter() { adapter_->SetPacketFilter(nullptr); }

  void StartConnect(PRFileDesc* model = nullptr);
  void CheckKEA(SSLKEAType kea_type, SSLNamedGroup group,
                size_t kea_size = 0) const;
  void CheckOriginalKEA(SSLNamedGroup kea_group) const;
  void CheckAuthType(SSLAuthType auth_type,
                     SSLSignatureScheme sig_scheme) const;

  void DisableAllCiphers();
  void EnableCiphersByAuthType(SSLAuthType authType);
  void EnableCiphersByKeyExchange(SSLKEAType kea);
  void EnableGroupsByKeyExchange(SSLKEAType kea);
  void EnableGroupsByAuthType(SSLAuthType authType);
  void EnableSingleCipher(uint16_t cipher);

  void Handshake();
  // Marks the internal state as CONNECTING in anticipation of renegotiation.
  void PrepareForRenegotiate();
  // Prepares for renegotiation, then actually triggers it.
  void StartRenegotiate();
  void SetAntiReplayContext(ScopedSSLAntiReplayContext& ctx);

  static bool LoadCertificate(const std::string& name,
                              ScopedCERTCertificate* cert,
                              ScopedSECKEYPrivateKey* priv);
  static bool LoadKeyPairFromCert(const std::string& name,
                                  ScopedSECKEYPublicKey* pub,
                                  ScopedSECKEYPrivateKey* priv);

  // Delegated credentials.
  //
  // Generate a delegated credential and sign it using the certificate
  // associated with |name|.
  static void DelegateCredential(const std::string& name,
                                 const ScopedSECKEYPublicKey& dcPub,
                                 SSLSignatureScheme dcCertVerifyAlg,
                                 PRUint32 dcValidFor, PRTime now, SECItem* dc);
  // Indicate support for the delegated credentials extension.
  void EnableDelegatedCredentials();
  // Generate and configure a delegated credential to use in the handshake with
  // clients that support this extension..
  void AddDelegatedCredential(const std::string& dc_name,
                              SSLSignatureScheme dcCertVerifyAlg,
                              PRUint32 dcValidFor, PRTime now);
  void UpdatePreliminaryChannelInfo();

  bool ConfigServerCert(const std::string& name, bool updateKeyBits = false,
                        const SSLExtraServerCertData* serverCertData = nullptr);
  bool ConfigServerCertWithChain(const std::string& name);
  bool EnsureTlsSetup(PRFileDesc* modelSocket = nullptr);

  void SetupClientAuth(
      ClientAuthCallbackType callbackType = ClientAuthCallbackType::kSync,
      bool callbackSuccess = true);
  void RequestClientAuth(bool requireAuth);
  void ClientAuthCallbackComplete();
  bool CheckClientAuthCallbacksCompleted(uint8_t expected);
  void CheckClientAuthCompleted(uint8_t handshakes = 1);
  void SetOption(int32_t option, int value);
  void ConfigureSessionCache(SessionResumptionMode mode);
  void Set0RttEnabled(bool en);
  void SetFallbackSCSVEnabled(bool en);
  void SetVersionRange(uint16_t minver, uint16_t maxver);
  void GetVersionRange(uint16_t* minver, uint16_t* maxver);
  void CheckPreliminaryInfo();
  void ResetPreliminaryInfo();
  void SetExpectedVersion(uint16_t version);
  void SetServerKeyBits(uint16_t bits);
  void ExpectReadWriteError();
  void EnableFalseStart();
  void ExpectEch(bool expected = true);
  bool GetEchExpected() const { return expect_ech_; }
  void ExpectPsk(SSLPskType psk = ssl_psk_external);
  void ExpectResumption();
  void SkipVersionChecks();
  void SetSignatureSchemes(const SSLSignatureScheme* schemes, size_t count);
  void EnableAlpn(const uint8_t* val, size_t len);
  void CheckAlpn(SSLNextProtoState expected_state,
                 const std::string& expected = "") const;
  void EnableSrtp();
  void CheckSrtp() const;
  void CheckEpochs(uint16_t expected_read, uint16_t expected_write) const;
  void CheckErrorCode(int32_t expected) const;
  void WaitForErrorCode(int32_t expected, uint32_t delay) const;
  // Send data on the socket, encrypting it.
  void SendData(size_t bytes, size_t blocksize = 1024);
  void SendBuffer(const DataBuffer& buf);
  bool SendEncryptedRecord(const std::shared_ptr<TlsCipherSpec>& spec,
                           uint64_t seq, uint8_t ct, const DataBuffer& buf);
  // Send data directly to the underlying socket, skipping the TLS layer.
  void SendDirect(const DataBuffer& buf);
  void SendRecordDirect(const TlsRecord& record);
  void AddPsk(const ScopedPK11SymKey& psk, std::string label, SSLHashType hash,
              uint16_t zeroRttSuite = TLS_NULL_WITH_NULL_NULL);
  void RemovePsk(std::string label);
  void ReadBytes(size_t max = 16384U);
  void ResetSentBytes(size_t bytes = 0);  // Hack to test drops.
  void EnableExtendedMasterSecret();
  void CheckExtendedMasterSecret(bool expected);
  void CheckEarlyDataAccepted(bool expected);
  void CheckEchAccepted(bool expected);
  void SetDowngradeCheckVersion(uint16_t version);
  void CheckSecretsDestroyed();
  void ConfigNamedGroups(const std::vector<SSLNamedGroup>& groups);
  void EnableECDHEServerKeyReuse();
  bool GetPeerChainLength(size_t* count);
  void CheckPeerChainFunctionConsistency();
  void CheckCipherSuite(uint16_t cipher_suite);
  void SetResumptionTokenCallback();
  bool MaybeSetResumptionToken();
  void SetResumptionToken(const std::vector<uint8_t>& resumption_token) {
    resumption_token_ = resumption_token;
  }
  const std::vector<uint8_t>& GetResumptionToken() const {
    return resumption_token_;
  }
  void GetTokenInfo(ScopedSSLResumptionTokenInfo& token) {
    SECStatus rv = SSL_GetResumptionTokenInfo(
        resumption_token_.data(), resumption_token_.size(), token.get(),
        sizeof(SSLResumptionTokenInfo));
    ASSERT_EQ(SECSuccess, rv);
  }
  void SetResumptionCallbackCalled() { resumption_callback_called_ = true; }
  bool resumption_callback_called() const {
    return resumption_callback_called_;
  }

  const std::string& name() const { return name_; }

  Role role() const { return role_; }
  std::string role_str() const { return role_ == SERVER ? "server" : "client"; }

  SSLProtocolVariant variant() const { return variant_; }

  State state() const { return state_; }

  const CERTCertificate* peer_cert() const {
    return SSL_PeerCertificate(ssl_fd_.get());
  }

  const char* state_str() const { return state_str(state()); }

  static const char* state_str(State state) { return states[state]; }

  NssManagedFileDesc ssl_fd() const {
    return NssManagedFileDesc(ssl_fd_.get(), policy_, option_);
  }
  std::shared_ptr<DummyPrSocket>& adapter() { return adapter_; }

  const SSLChannelInfo& info() const {
    EXPECT_EQ(STATE_CONNECTED, state_);
    return info_;
  }

  const SSLPreliminaryChannelInfo& pre_info() const { return pre_info_; }

  bool is_compressed() const {
    return info().compressionMethod != ssl_compression_null;
  }
  uint16_t server_key_bits() const { return server_key_bits_; }
  uint16_t min_version() const { return vrange_.min; }
  uint16_t max_version() const { return vrange_.max; }
  uint16_t version() const { return info().protocolVersion; }

  bool cipher_suite(uint16_t* suite) const {
    if (state_ != STATE_CONNECTED) return false;

    *suite = info_.cipherSuite;
    return true;
  }

  void expected_cipher_suite(uint16_t suite) { expected_cipher_suite_ = suite; }

  std::string cipher_suite_name() const {
    if (state_ != STATE_CONNECTED) return "UNKNOWN";

    return csinfo_.cipherSuiteName;
  }

  std::vector<uint8_t> session_id() const {
    return std::vector<uint8_t>(info_.sessionID,
                                info_.sessionID + info_.sessionIDLength);
  }

  bool auth_type(SSLAuthType* a) const {
    if (state_ != STATE_CONNECTED) return false;

    *a = info_.authType;
    return true;
  }

  bool kea_type(SSLKEAType* k) const {
    if (state_ != STATE_CONNECTED) return false;

    *k = info_.keaType;
    return true;
  }

  size_t received_bytes() const { return recv_ctr_; }
  PRErrorCode error_code() const { return error_code_; }

  bool can_falsestart_hook_called() const {
    return can_falsestart_hook_called_;
  }

  void SetHandshakeCallback(HandshakeCallbackFunction handshake_callback) {
    handshake_callback_ = handshake_callback;
  }

  void SetAuthCertificateCallback(
      AuthCertificateCallbackFunction auth_certificate_callback) {
    auth_certificate_callback_ = auth_certificate_callback;
  }

  void SetSniCallback(SniCallbackFunction sni_callback) {
    sni_callback_ = sni_callback;
  }

  void ExpectReceiveAlert(uint8_t alert, uint8_t level = 0);
  void ExpectSendAlert(uint8_t alert, uint8_t level = 0);

  std::string alpn_value_to_use_ = "";
  // set the given policy before this agent runs
  void SetPolicy(SECOidTag oid, PRUint32 set, PRUint32 clear) {
    policy_ = NssPolicy(oid, set, clear);
  }
  void SetNssOption(PRInt32 id, PRInt32 value) {
    option_ = NssOption(id, value);
  }

 private:
  const static char* states[];

  void SetState(State state);
  void ValidateCipherSpecs();

  // Dummy auth certificate hook.
  static SECStatus AuthCertificateHook(void* arg, PRFileDesc* fd,
                                       PRBool checksig, PRBool isServer) {
    TlsAgent* agent = reinterpret_cast<TlsAgent*>(arg);
    agent->CheckPreliminaryInfo();
    agent->auth_certificate_hook_called_ = true;
    if (agent->auth_certificate_callback_) {
      return agent->auth_certificate_callback_(agent, checksig ? true : false,
                                               isServer ? true : false);
    }
    return SECSuccess;
  }

  // Client auth certificate hook.
  static SECStatus ClientAuthenticated(void* arg, PRFileDesc* fd,
                                       PRBool checksig, PRBool isServer) {
    TlsAgent* agent = reinterpret_cast<TlsAgent*>(arg);
    EXPECT_TRUE(agent->expect_client_auth_);
    EXPECT_EQ(PR_TRUE, isServer);
    if (agent->auth_certificate_callback_) {
      return agent->auth_certificate_callback_(agent, checksig ? true : false,
                                               isServer ? true : false);
    }
    return SECSuccess;
  }

  static SECStatus GetClientAuthDataHook(void* self, PRFileDesc* fd,
                                         CERTDistNames* caNames,
                                         CERTCertificate** cert,
                                         SECKEYPrivateKey** privKey);

  static void ReadableCallback(PollTarget* self, Event event) {
    TlsAgent* agent = static_cast<TlsAgent*>(self);
    if (event == TIMER_EVENT) {
      agent->timer_handle_ = nullptr;
    }
    agent->ReadableCallback_int();
  }

  void ReadableCallback_int() {
    LOGV("Readable");
    switch (state_) {
      case STATE_CONNECTING:
        Handshake();
        break;
      case STATE_CONNECTED:
        ReadBytes();
        break;
      default:
        break;
    }
  }

  static PRInt32 SniHook(PRFileDesc* fd, const SECItem* srvNameArr,
                         PRUint32 srvNameArrSize, void* arg) {
    TlsAgent* agent = reinterpret_cast<TlsAgent*>(arg);
    agent->CheckPreliminaryInfo();
    agent->sni_hook_called_ = true;
    EXPECT_EQ(1UL, srvNameArrSize);
    if (agent->sni_callback_) {
      return agent->sni_callback_(agent, srvNameArr, srvNameArrSize);
    }
    return 0;  // First configuration.
  }

  static SECStatus CanFalseStartCallback(PRFileDesc* fd, void* arg,
                                         PRBool* canFalseStart) {
    TlsAgent* agent = reinterpret_cast<TlsAgent*>(arg);
    agent->CheckPreliminaryInfo();
    EXPECT_TRUE(agent->falsestart_enabled_);
    EXPECT_FALSE(agent->can_falsestart_hook_called_);
    agent->can_falsestart_hook_called_ = true;
    *canFalseStart = true;
    return SECSuccess;
  }

  void CheckAlert(bool sent, const SSLAlert* alert);

  static void AlertReceivedCallback(const PRFileDesc* fd, void* arg,
                                    const SSLAlert* alert) {
    reinterpret_cast<TlsAgent*>(arg)->CheckAlert(false, alert);
  }

  static void AlertSentCallback(const PRFileDesc* fd, void* arg,
                                const SSLAlert* alert) {
    reinterpret_cast<TlsAgent*>(arg)->CheckAlert(true, alert);
  }

  static void HandshakeCallback(PRFileDesc* fd, void* arg) {
    TlsAgent* agent = reinterpret_cast<TlsAgent*>(arg);
    agent->handshake_callback_called_ = true;
    agent->Connected();
    if (agent->handshake_callback_) {
      agent->handshake_callback_(agent);
    }
  }

  void DisableLameGroups();
  void ConfigStrongECGroups(bool en);
  void ConfigAllDHGroups(bool en);
  void CheckCallbacks() const;
  void Connected();

  const std::string name_;
  SSLProtocolVariant variant_;
  Role role_;
  uint16_t server_key_bits_;
  std::shared_ptr<DummyPrSocket> adapter_;
  ScopedPRFileDesc ssl_fd_;
  State state_;
  std::shared_ptr<Poller::Timer> timer_handle_;
  bool falsestart_enabled_;
  uint16_t expected_version_;
  uint16_t expected_cipher_suite_;
  bool expect_client_auth_;
  bool expect_ech_;
  SSLPskType expect_psk_;
  bool can_falsestart_hook_called_;
  bool sni_hook_called_;
  bool auth_certificate_hook_called_;
  uint8_t expected_received_alert_;
  uint8_t expected_received_alert_level_;
  uint8_t expected_sent_alert_;
  uint8_t expected_sent_alert_level_;
  bool handshake_callback_called_;
  bool resumption_callback_called_;
  SSLChannelInfo info_;
  SSLPreliminaryChannelInfo pre_info_;
  SSLCipherSuiteInfo csinfo_;
  SSLVersionRange vrange_;
  PRErrorCode error_code_;
  size_t send_ctr_;
  size_t recv_ctr_;
  bool expect_readwrite_error_;
  HandshakeCallbackFunction handshake_callback_;
  AuthCertificateCallbackFunction auth_certificate_callback_;
  SniCallbackFunction sni_callback_;
  bool skip_version_checks_;
  std::vector<uint8_t> resumption_token_;
  NssPolicy policy_;
  NssOption option_;
  ClientAuthCallbackType client_auth_callback_type_ =
      ClientAuthCallbackType::kNone;
  bool client_auth_callback_success_ = false;
  uint8_t client_auth_callback_fired_ = 0;
  bool client_auth_callback_awaiting_ = false;
};

inline std::ostream& operator<<(std::ostream& stream,
                                const TlsAgent::State& state) {
  return stream << TlsAgent::state_str(state);
}

class TlsAgentTestBase : public ::testing::Test {
 public:
  static ::testing::internal::ParamGenerator<std::string> kTlsRolesAll;

  TlsAgentTestBase(TlsAgent::Role role, SSLProtocolVariant variant,
                   uint16_t version = 0)
      : agent_(nullptr),
        role_(role),
        variant_(variant),
        version_(version),
        sink_adapter_(new DummyPrSocket("sink", variant)) {}
  virtual ~TlsAgentTestBase() {}

  void SetUp();
  void TearDown();

  void ExpectAlert(uint8_t alert);

  static void MakeRecord(SSLProtocolVariant variant, uint8_t type,
                         uint16_t version, const uint8_t* buf, size_t len,
                         DataBuffer* out, uint64_t seq_num = 0);
  void MakeRecord(uint8_t type, uint16_t version, const uint8_t* buf,
                  size_t len, DataBuffer* out, uint64_t seq_num = 0) const;
  void MakeHandshakeMessage(uint8_t hs_type, const uint8_t* data, size_t hs_len,
                            DataBuffer* out, uint64_t seq_num = 0) const;
  void MakeHandshakeMessageFragment(uint8_t hs_type, const uint8_t* data,
                                    size_t hs_len, DataBuffer* out,
                                    uint64_t seq_num, uint32_t fragment_offset,
                                    uint32_t fragment_length) const;
  DataBuffer MakeCannedTls13ServerHello();
  static void MakeTrivialHandshakeRecord(uint8_t hs_type, size_t hs_len,
                                         DataBuffer* out);
  static inline TlsAgent::Role ToRole(const std::string& str) {
    return str == "CLIENT" ? TlsAgent::CLIENT : TlsAgent::SERVER;
  }

  void Init(const std::string& server_name = TlsAgent::kServerRsa);
  void Reset(const std::string& server_name = TlsAgent::kServerRsa);

 protected:
  void EnsureInit();
  void ProcessMessage(const DataBuffer& buffer, TlsAgent::State expected_state,
                      int32_t error_code = 0);

  std::shared_ptr<TlsAgent> agent_;
  TlsAgent::Role role_;
  SSLProtocolVariant variant_;
  uint16_t version_;
  // This adapter is here just to accept packets from this agent.
  std::shared_ptr<DummyPrSocket> sink_adapter_;
};

class TlsAgentTest
    : public TlsAgentTestBase,
      public ::testing::WithParamInterface<
          std::tuple<std::string, SSLProtocolVariant, uint16_t>> {
 public:
  TlsAgentTest()
      : TlsAgentTestBase(ToRole(std::get<0>(GetParam())),
                         std::get<1>(GetParam()), std::get<2>(GetParam())) {}
};

class TlsAgentTestClient : public TlsAgentTestBase,
                           public ::testing::WithParamInterface<
                               std::tuple<SSLProtocolVariant, uint16_t>> {
 public:
  TlsAgentTestClient()
      : TlsAgentTestBase(TlsAgent::CLIENT, std::get<0>(GetParam()),
                         std::get<1>(GetParam())) {}
};

class TlsAgentTestClient13 : public TlsAgentTestClient {};

class TlsAgentStreamTestClient13 : public TlsAgentTestClient {
 public:
  TlsAgentStreamTestClient13() { variant_ = ssl_variant_stream; }
};

class TlsAgentStreamTestClient : public TlsAgentTestBase {
 public:
  TlsAgentStreamTestClient()
      : TlsAgentTestBase(TlsAgent::CLIENT, ssl_variant_stream) {}
};

class TlsAgentStreamTestServer : public TlsAgentTestBase {
 public:
  TlsAgentStreamTestServer()
      : TlsAgentTestBase(TlsAgent::SERVER, ssl_variant_stream) {}
};

class TlsAgentDgramTestClient : public TlsAgentTestBase {
 public:
  TlsAgentDgramTestClient()
      : TlsAgentTestBase(TlsAgent::CLIENT, ssl_variant_datagram) {}
};

inline bool operator==(const SSLVersionRange& vr1, const SSLVersionRange& vr2) {
  return vr1.min == vr2.min && vr1.max == vr2.max;
}

}  // namespace nss_test

#endif
