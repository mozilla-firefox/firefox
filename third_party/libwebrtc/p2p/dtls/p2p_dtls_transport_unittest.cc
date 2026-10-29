/*
 *  Copyright 2011 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "api/crypto/crypto_options.h"
#include "api/dtls_transport_interface.h"
#include "api/environment/environment.h"
#include "api/field_trials.h"
#include "api/ice_transport_interface.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/test/rtc_error_matchers.h"
#include "api/transport/stun.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/packet_transport_internal.h"
#include "p2p/base/transport_description.h"
#include "p2p/dtls/dtls_transport.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "p2p/dtls/dtls_utils.h"
#include "p2p/test/fake_ice_transport.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/buffer.h"
#include "rtc_base/byte_order.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/logging.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/rtc_certificate.h"
#include "rtc_base/ssl_certificate.h"
#include "rtc_base/ssl_fingerprint.h"
#include "rtc_base/ssl_identity.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/stream.h"
#include "system_wrappers/include/metrics.h"
#include "test/create_test_environment.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/time_controller/simulated_time_controller.h"
#include "test/wait_until.h"

namespace webrtc {
namespace {
using ::testing::Eq;
using ::testing::IsTrue;

const size_t kPacketNumOffset = 8;
const size_t kPacketHeaderLen = 12;
const int kFakePacketId = 0x1234;
const int kTimeout = 10000;

constexpr uint8_t kRtpLeadByte = 0x80;

bool IsRtpLeadByte(uint8_t b) {
  return b == kRtpLeadByte;
}

// `modify_digest` is used to set modified fingerprints that are meant to fail
// validation.
void SetRemoteFingerprintFromCert(DtlsTransportInternalImpl* transport,
                                  const scoped_refptr<RTCCertificate>& cert,
                                  bool modify_digest = false) {
  std::unique_ptr<SSLFingerprint> fingerprint =
      SSLFingerprint::CreateFromCertificate(*cert);
  if (modify_digest) {
    ++fingerprint->digest.MutableData()[0];
  }

  // Even if digest is verified to be incorrect, should fail asynchronously.
  EXPECT_TRUE(
      transport
          ->SetRemoteParameters(
              fingerprint->algorithm,
              reinterpret_cast<const uint8_t*>(fingerprint->digest.data()),
              fingerprint->digest.size(), std::nullopt)
          .ok());
}

class DtlsTestClient {
 public:
  explicit DtlsTestClient(absl::string_view name) : name_(name) {}
  void CreateCertificate(KeyType key_type) {
    certificate_ = RTCCertificate::Create(SSLIdentity::Create(name_, key_type));
  }
  const scoped_refptr<RTCCertificate>& certificate() { return certificate_; }
  void SetupMaxProtocolVersion(SSLProtocolVersion version) {
    ssl_max_version_ = version;
  }
  void SetPqc(bool value) { pqc_ = value; }
  void set_async_delay(int async_delay_ms) { async_delay_ms_ = async_delay_ms; }
  void set_ssl_stream_factory(
      DtlsTransportInternalImpl::SslStreamFactory factory) {
    ssl_stream_factory_ = std::move(factory);
  }

  // Set up fake ICE transport and real DTLS transport under test.
  void SetupTransports(const Environment& env,
                       IceRole role,
                       bool rtt_estimate = true,
                       bool dtls_in_stun = false) {
    dtls_transport_ = nullptr;
    ice_transport_ = nullptr;

    CryptoOptions crypto_options;
    if (pqc_) {
      FieldTrials field_trials =
          CreateTestFieldTrials("WebRTC-EnableDtlsPqc/Enabled/");
      crypto_options.ephemeral_key_exchange_cipher_groups.Update(&field_trials);
    }

    auto fake_ice_transport = std::make_unique<FakeIceTransportInternal>(
        env, absl::StrCat("fake-", name_), 0,
        /* network_thread= */ nullptr);
    if (rtt_estimate) {
      fake_ice_transport->set_rtt_estimate(
          async_delay_ms_ ? std::optional<int>(async_delay_ms_) : std::nullopt,
          /* async= */ true);
    } else if (async_delay_ms_) {
      fake_ice_transport->SetAsync(async_delay_ms_);
      fake_ice_transport->SetAsyncDelay(async_delay_ms_);
    }
    fake_ice_transport->SetIceRole(role);
    if (dtls_in_stun) {
      auto ice_config = fake_ice_transport->config();
      ice_config.dtls_handshake_in_stun = true;
      fake_ice_transport->SetIceConfig(ice_config);
    }
    // Hook the raw packets so that we can verify they are encrypted.
    fake_ice_transport->RegisterReceivedPacketCallback(
        this, [&](PacketTransportInternal* transport,
                  const ReceivedIpPacket& packet) {
          OnFakeIceTransportReadPacket(transport, packet);
        });

    ice_transport_ =
        make_ref_counted<FakeIceTransport>(std::move(fake_ice_transport));

    dtls_transport_ = std::make_unique<DtlsTransportInternalImpl>(
        env, ice_transport_, crypto_options, ssl_max_version_,
        ssl_stream_factory_);
    // Note: Certificate may be null here if testing passthrough.
    dtls_transport_->SetLocalCertificate(certificate_);
    dtls_transport_->SubscribeWritableState(
        this, [this](PacketTransportInternal* transport) {
          OnTransportWritableState(transport);
        });
    dtls_transport_->RegisterReceivedPacketCallback(
        this, [&](PacketTransportInternal* transport,
                  const ReceivedIpPacket& packet) {
          OnTransportReadPacket(transport, packet);
        });
    dtls_transport_->SubscribeSentPacket(
        this,
        [this](PacketTransportInternal* transport, const SentPacketInfo& info) {
          OnTransportSentPacket(transport, info);
        });
  }

  FakeIceTransportInternal* fake_ice_transport() {
    return static_cast<FakeIceTransportInternal*>(ice_transport_->internal());
  }

  DtlsTransportInternalImpl* dtls_transport() { return dtls_transport_.get(); }
  void ResetDtlsTransport() { dtls_transport_ = nullptr; }

  // Simulate fake ICE transports connecting.
  bool Connect(DtlsTestClient* peer, bool asymmetric) {
    fake_ice_transport()->SetDestination(peer->fake_ice_transport(),
                                         asymmetric);
    return true;
  }

  // Connect the fake ICE transports so that packets flows from one to other.
  bool ConnectIceTransport(DtlsTestClient* peer) {
    fake_ice_transport()->SetDestinationNotWritable(peer->fake_ice_transport());
    fake_ice_transport()->set_drop_non_stun_unless_writable(true);
    return true;
  }

  bool SendIcePing(int n = 1) {
    for (int i = 0; i < n; i++) {
      if (!fake_ice_transport()->SendIcePing()) {
        return false;
      }
    }
    return true;
  }

  bool SendIcePingConf(int n = 1) {
    for (int i = 0; i < n; i++) {
      if (!fake_ice_transport()->SendIcePingConf()) {
        return false;
      }
    }
    return true;
  }

  int received_dtls_client_hellos() const {
    return received_dtls_client_hellos_;
  }

  int received_dtls_server_hellos() const {
    return received_dtls_server_hellos_;
  }

  int received_dtls_ciphertext_packets() const {
    return received_dtls_ciphertext_packets_;
  }

  std::optional<int> GetVersionBytes() {
    int value;
    if (dtls_transport_->GetSslVersionBytes(&value)) {
      return value;
    }
    return std::nullopt;
  }

  void CheckRole(SSLRole role) {
    if (role == SSL_CLIENT) {
      ASSERT_EQ(0, received_dtls_client_hellos_);
      ASSERT_GT(received_dtls_server_hellos_, 0);
    } else {
      ASSERT_GT(received_dtls_client_hellos_, 0);
      ASSERT_EQ(0, received_dtls_server_hellos_);
    }
  }

  void CheckSrtp(int expected_crypto_suite) {
    int crypto_suite;
    bool rv = dtls_transport_->GetSrtpCryptoSuite(&crypto_suite);
    if (dtls_transport_->IsDtlsActive() && expected_crypto_suite) {
      ASSERT_TRUE(rv);
      ASSERT_EQ(crypto_suite, expected_crypto_suite);
    } else {
      ASSERT_FALSE(rv);
    }
  }

  void CheckSsl() {
    int cipher;
    bool rv = dtls_transport_->GetSslCipherSuite(&cipher);
    if (dtls_transport_->IsDtlsActive()) {
      ASSERT_TRUE(rv);
      EXPECT_TRUE(SSLStreamAdapter::IsAcceptableCipher(cipher, KT_DEFAULT));
    } else {
      ASSERT_FALSE(rv);
    }
  }

  int SendPacket(size_t size, bool srtp, AsyncSocketPacketOptions options) {
    std::unique_ptr<char[]> packet(new char[size]);
    memset(packet.get(), 0xff, size);
    int flags = (certificate_ && srtp) ? PF_SRTP_BYPASS : 0;
    return dtls_transport_->SendPacket(packet.get(), size, options, flags);
  }

  void SendPackets(size_t size, size_t count, bool srtp) {
    std::unique_ptr<char[]> packet(new char[size]);
    size_t sent = 0;
    do {
      // Fill the packet with a known value and a sequence number to check
      // against, and make sure that it doesn't look like DTLS.
      memset(packet.get(), sent & 0xff, size);
      packet[0] = (srtp) ? kRtpLeadByte : 0x00;
      SetBE32(
          std::span<uint8_t>(
              reinterpret_cast<uint8_t*>(packet.get() + kPacketNumOffset), 4),
          static_cast<uint32_t>(sent));

      // Only set the bypass flag if we've activated DTLS.
      int flags = (certificate_ && srtp) ? PF_SRTP_BYPASS : 0;
      AsyncSocketPacketOptions packet_options;
      packet_options.packet_id = kFakePacketId;
      int rv = dtls_transport_->SendPacket(packet.get(), size, packet_options,
                                           flags);
      ASSERT_GT(rv, 0);
      ASSERT_EQ(size, static_cast<size_t>(rv));
      ++sent;
    } while (sent < count);
  }

  int SendInvalidSrtpPacket(size_t size) {
    std::unique_ptr<char[]> packet(new char[size]);
    // Fill the packet with 0 to form an invalid SRTP packet.
    memset(packet.get(), 0, size);

    AsyncSocketPacketOptions packet_options;
    return dtls_transport_->SendPacket(packet.get(), size, packet_options,
                                       PF_SRTP_BYPASS);
  }

  void ExpectPackets(size_t size) {
    packet_size_ = size;
    received_.clear();
  }

  size_t NumPacketsReceived() { return received_.size(); }

  // Inverse of SendPackets.
  bool VerifyPacket(std::span<const uint8_t> payload, uint32_t* out_num) {
    const uint8_t* data = payload.data();
    size_t size = payload.size();

    if (size != packet_size_ || (data[0] != 0 && (data[0]) != 0x80)) {
      return false;
    }
    uint32_t packet_num =
        GetBE32(std::span<const uint8_t>(data + kPacketNumOffset, 4));
    for (size_t i = kPacketHeaderLen; i < size; ++i) {
      if (data[i] != (packet_num & 0xff)) {
        return false;
      }
    }
    if (out_num) {
      *out_num = packet_num;
    }
    return true;
  }
  bool VerifyEncryptedPacket(const uint8_t* data, size_t size) {
    // This is an encrypted data packet; let's make sure it's mostly random;
    // less than 10% of the bytes should be equal to the cleartext packet.
    if (size <= packet_size_) {
      return false;
    }
    uint32_t packet_num =
        GetBE32(std::span<const uint8_t>(data + kPacketNumOffset, 4));
    int num_matches = 0;
    for (size_t i = kPacketNumOffset; i < size; ++i) {
      if (data[i] == (packet_num & 0xff)) {
        ++num_matches;
      }
    }
    return (num_matches < ((static_cast<int>(size) - 5) / 10));
  }

  // Transport callbacks
  void set_writable_callback(absl::AnyInvocable<void()> func) {
    writable_func_ = std::move(func);
  }
  void OnTransportWritableState(PacketTransportInternal* transport) {
    RTC_LOG(LS_INFO) << name_ << ": Transport '" << transport->transport_name()
                     << "' is writable";
    if (writable_func_) {
      writable_func_();
    }
  }

  void OnTransportReadPacket(PacketTransportInternal* /* transport */,
                             const ReceivedIpPacket& packet) {
    uint32_t packet_num = 0;
    ASSERT_TRUE(VerifyPacket(packet.payload(), &packet_num));
    received_.insert(packet_num);
    switch (packet.decryption_info()) {
      case ReceivedIpPacket::kSrtpEncrypted:
        ASSERT_TRUE(certificate_ && IsRtpLeadByte(packet.payload()[0]));
        break;
      case ReceivedIpPacket::kDtlsDecrypted:
        ASSERT_TRUE(certificate_ && !IsRtpLeadByte(packet.payload()[0]));
        break;
      case ReceivedIpPacket::kNotDecrypted:
        ASSERT_FALSE(certificate_);
        break;
    }
  }

  void OnTransportSentPacket(PacketTransportInternal* /* transport */,
                             const SentPacketInfo& sent_packet) {
    sent_packet_ = sent_packet;
  }

  SentPacketInfo sent_packet() const { return sent_packet_; }

  bool IsDtlsCiphertextPacket(std::span<const uint8_t> payload) {
    return IsDtlsPacket(payload) &&
           (payload.data()[0] > 31 && payload.data()[0] < 64);
  }

  // Hook into the raw packet stream to make sure DTLS packets are encrypted.
  void OnFakeIceTransportReadPacket(PacketTransportInternal* /* transport */,
                                    const ReceivedIpPacket& packet) {
    // Packets should not be decrypted on the underlying Transport packets.
    ASSERT_EQ(packet.decryption_info(), ReceivedIpPacket::kNotDecrypted);

    // Look at the handshake packets to see what role we played.
    // Check that non-handshake packets are DTLS data or SRTP bypass.
    const uint8_t* data = packet.payload().data();
    if (IsDtlsHandshakePacket(packet.payload())) {
      if (IsDtlsClientHelloPacket(packet.payload())) {
        ++received_dtls_client_hellos_;
      } else if (data[13] == 2) {
        ++received_dtls_server_hellos_;
      }
    } else if (IsDtlsCiphertextPacket(packet.payload())) {
      ++received_dtls_ciphertext_packets_;
    } else if (data[0] == 26) {
      RTC_LOG(LS_INFO) << "Found DTLS ACK";
    } else if (dtls_transport_->IsDtlsActive()) {
      if (IsRtpLeadByte(data[0])) {
        ASSERT_TRUE(VerifyPacket(packet.payload(), nullptr));
      } else if (packet_size_ && packet.payload().size() >= packet_size_) {
        ASSERT_TRUE(VerifyEncryptedPacket(data, packet.payload().size()));
      }
    }
  }

  absl::string_view name() { return name_; }

 private:
  std::string name_;
  scoped_refptr<RTCCertificate> certificate_;
  scoped_refptr<IceTransportInterface> ice_transport_;
  std::unique_ptr<DtlsTransportInternalImpl> dtls_transport_;
  size_t packet_size_ = 0u;
  std::set<int> received_;
  SSLProtocolVersion ssl_max_version_ = SSL_PROTOCOL_DTLS_12;
  int received_dtls_client_hellos_ = 0;
  int received_dtls_server_hellos_ = 0;
  int received_dtls_ciphertext_packets_ = 0;
  SentPacketInfo sent_packet_;
  absl::AnyInvocable<void()> writable_func_;
  int async_delay_ms_ = 100;
  bool pqc_ = false;
  DtlsTransportInternalImpl::SslStreamFactory ssl_stream_factory_;
};

class FakeSSLStreamAdapter : public SSLStreamAdapter {
 public:
  explicit FakeSSLStreamAdapter(std::unique_ptr<SSLStreamAdapter> impl_)
      : impl_(std::move(impl_)) {}

  void Init() {
    impl_->SetEventCallback([this](int events, int err) {
      RTC_DCHECK_RUN_ON(&callback_sequence_);
      FireEvent(events, err);
    });
  }

  void SetWriteError(std::optional<int> error) { write_error_ = error; }

  // SSLStreamAdapter overrides.
  void SetIdentity(std::unique_ptr<SSLIdentity> identity) override {
    impl_->SetIdentity(std::move(identity));
  }
  SSLIdentity* GetIdentityForTesting() const override {
    return impl_->GetIdentityForTesting();
  }
  void SetServerRole(SSLRole role) override { impl_->SetServerRole(role); }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  void SetMode(SSLMode mode) override { impl_->SetMode(mode); }
  SSLProtocolVersion GetSslVersion() const override {
    return impl_->GetSslVersion();
  }
#pragma clang diagnostic pop
  void SetMaxProtocolVersion(SSLProtocolVersion version) override {
    impl_->SetMaxProtocolVersion(version);
  }
  void SetInitialRetransmissionTimeout(int timeout_ms) override {
    impl_->SetInitialRetransmissionTimeout(timeout_ms);
  }
  void SetMTU(int mtu) override { impl_->SetMTU(mtu); }
  int StartSSL() override { return impl_->StartSSL(); }
  SSLPeerCertificateDigestError SetPeerCertificateDigest(
      absl::string_view digest_alg,
      std::span<const uint8_t> digest_val) override {
    return impl_->SetPeerCertificateDigest(digest_alg, digest_val);
  }
  std::unique_ptr<SSLCertChain> GetPeerSSLCertChain() const override {
    return impl_->GetPeerSSLCertChain();
  }
  bool GetSslCipherSuite(int* cipher_suite) const override {
    return impl_->GetSslCipherSuite(cipher_suite);
  }
  std::optional<absl::string_view> GetTlsCipherSuiteName() const override {
    return impl_->GetTlsCipherSuiteName();
  }
  bool GetSslVersionBytes(int* version) const override {
    return impl_->GetSslVersionBytes(version);
  }
  [[deprecated]] bool ExportSrtpKeyingMaterial(
      ZeroOnFreeBuffer<uint8_t>& keying_material) override {
    return impl_->ExportSrtpKeyingMaterial(keying_material);
  }
  bool AppendSrtpKeyingMaterial(
      ZeroOnFreeBuffer<uint8_t>& keying_material) override {
    return impl_->AppendSrtpKeyingMaterial(keying_material);
  }
  uint16_t GetPeerSignatureAlgorithm() const override {
    return impl_->GetPeerSignatureAlgorithm();
  }
  bool SetDtlsSrtpCryptoSuites(const std::vector<int>& crypto_suites) override {
    return impl_->SetDtlsSrtpCryptoSuites(crypto_suites);
  }
  bool GetDtlsSrtpCryptoSuite(int* crypto_suite) const override {
    return impl_->GetDtlsSrtpCryptoSuite(crypto_suite);
  }
  bool IsTlsConnected() override { return impl_->IsTlsConnected(); }
  int GetRetransmissionCount() const override {
    return impl_->GetRetransmissionCount();
  }
  bool SetSslGroupIds(const std::vector<uint16_t>& group_ids) override {
    return impl_->SetSslGroupIds(group_ids);
  }
  uint16_t GetSslGroupId() const override { return impl_->GetSslGroupId(); }

  // StreamInterface overrides.
  StreamState GetState() const override { return impl_->GetState(); }
  void Close() override { impl_->Close(); }
  StreamResult Read(std::span<uint8_t> buffer,
                    size_t& read,
                    int& error) override {
    return impl_->Read(buffer, read, error);
  }
  StreamResult Write(std::span<const uint8_t> data,
                     size_t& written,
                     int& error) override {
    if (write_error_) {
      error = *write_error_;
      return SR_ERROR;
    }
    return impl_->Write(data, written, error);
  }
  bool Flush() override { return impl_->Flush(); }

 private:
  std::unique_ptr<StreamInterface> stream_;
  std::unique_ptr<SSLStreamAdapter> impl_;
  std::optional<int> write_error_;
};

// Base class for DtlsTransportInternalImplTest and DtlsEventOrderingTest, which
// inherit from different variants of ::testing::Test.
//
// Note that this test always uses a FakeClock, due to the `fake_clock_` member
// variable.
class DtlsTransportInternalImplTestBase {
 public:
  DtlsTransportInternalImplTestBase()
      : time_controller_(Timestamp::Millis(0)),
        env_(CreateTestEnvironment({.time = &time_controller_})),
        client1_("P1"),
        client2_("P2"),
        use_dtls_(false),
        start_time_(time_controller_.GetClock()->CurrentTime()) {}

  void SetPqc(bool value) {
    client1_.SetPqc(value);
    client2_.SetPqc(value);
  }

  void SetMaxProtocolVersions(SSLProtocolVersion c1, SSLProtocolVersion c2) {
    client1_.SetupMaxProtocolVersion(c1);
    client2_.SetupMaxProtocolVersion(c2);
  }

  // If not called, DtlsTransportInternalImpl will be used in SRTP bypass mode.
  void PrepareDtls(KeyType key_type) {
    client1_.CreateCertificate(key_type);
    client2_.CreateCertificate(key_type);
    use_dtls_ = true;
  }

  // This test negotiates DTLS parameters before the underlying transports are
  // writable. DtlsEventOrderingTest is responsible for exercising differerent
  // orderings.
  bool Connect(bool client1_server = true) {
    Negotiate(client1_server);
    EXPECT_TRUE(client1_.Connect(&client2_, false));

    EXPECT_THAT(webrtc::WaitUntil(
                    [&] {
                      return client1_.dtls_transport()->writable() &&
                             client2_.dtls_transport()->writable();
                    },
                    IsTrue(),
                    {.timeout = TimeDelta::Millis(kTimeout),
                     .clock = &time_controller_}),
                IsRtcOk());
    if (!client1_.dtls_transport()->writable() ||
        !client2_.dtls_transport()->writable())
      return false;

    // Check that we used the right roles.
    if (use_dtls_) {
      client1_.CheckRole(client1_server ? SSL_SERVER : SSL_CLIENT);
      client2_.CheckRole(client1_server ? SSL_CLIENT : SSL_SERVER);
    }

    if (use_dtls_) {
      // Check that we negotiated the right ciphers. Since GCM ciphers are not
      // negotiated by default, we should end up with kSrtpAes128CmSha1_80.
      client1_.CheckSrtp(kSrtpAes128CmSha1_80);
      client2_.CheckSrtp(kSrtpAes128CmSha1_80);
    } else {
      // If DTLS isn't actually being used, GetSrtpCryptoSuite should return
      // false.
      client1_.CheckSrtp(kSrtpInvalidCryptoSuite);
      client2_.CheckSrtp(kSrtpInvalidCryptoSuite);
    }

    client1_.CheckSsl();
    client2_.CheckSsl();

    return true;
  }

  void Negotiate(bool client1_server = true) {
    client1_.SetupTransports(env_, ICEROLE_CONTROLLING);
    client2_.SetupTransports(env_, ICEROLE_CONTROLLED);
    client1_.dtls_transport()->SetDtlsRole(client1_server ? SSL_SERVER
                                                          : SSL_CLIENT);
    client2_.dtls_transport()->SetDtlsRole(client1_server ? SSL_CLIENT
                                                          : SSL_SERVER);
    if (client2_.certificate()) {
      SetRemoteFingerprintFromCert(client1_.dtls_transport(),
                                   client2_.certificate());
    }
    if (client1_.certificate()) {
      SetRemoteFingerprintFromCert(client2_.dtls_transport(),
                                   client1_.certificate());
    }
  }

  void TestTransfer(size_t size, size_t count, bool srtp) {
    RTC_LOG(LS_INFO) << "Expect packets, size=" << size;
    client2_.ExpectPackets(size);
    client1_.SendPackets(size, count, srtp);
    EXPECT_THAT(webrtc::WaitUntil([&] { return client2_.NumPacketsReceived(); },
                                  Eq(count),
                                  {.timeout = TimeDelta::Millis(kTimeout),
                                   .clock = &time_controller_}),
                IsRtcOk());
  }

  int client1_recv_packets = 0;
  int client2_recv_packets = 0;
  void AddPacketLogging() {
    client1_.fake_ice_transport()->set_packet_recv_filter(
        [&](auto packet, auto timestamp_us) {
          client1_recv_packets++;
          return LogRecv(client1_.name(), packet);
        });
    client2_.fake_ice_transport()->set_packet_recv_filter(
        [&](auto packet, auto timestamp_us) {
          client2_recv_packets++;
          return LogRecv(client2_.name(), packet);
        });
    client1_.set_writable_callback([&]() {});
    client2_.set_writable_callback([&]() {});

    client1_.fake_ice_transport()->set_packet_send_filter(
        [&](auto data, auto len, auto options, auto flags) {
          return LogSend(client1_.name(), /* drop=*/false, data, len);
        });
    client2_.fake_ice_transport()->set_packet_send_filter(
        [&](auto data, auto len, auto options, auto flags) {
          return LogSend(client2_.name(), /* drop=*/false, data, len);
        });
  }

  void ClearPacketFilters() {
    client1_.fake_ice_transport()->set_packet_send_filter(nullptr);
    client2_.fake_ice_transport()->set_packet_send_filter(nullptr);
    client1_.fake_ice_transport()->set_packet_recv_filter(nullptr);
    client2_.fake_ice_transport()->set_packet_recv_filter(nullptr);
  }

  bool LogRecv(absl::string_view name, const CopyOnWriteBuffer& packet) {
    auto timestamp_ms =
        (time_controller_.GetClock()->CurrentTime() - start_time_).ms();
    RTC_LOG(LS_INFO) << "time=" << timestamp_ms << " : " << name
                     << ": ReceivePacket packet len=" << packet.size()
                     << ", data[0]: " << static_cast<uint8_t>(packet.data()[0]);
    return false;
  }

  bool LogSend(absl::string_view name,
               bool drop,
               const char* data,
               size_t len) {
    auto timestamp_ms =
        (time_controller_.GetClock()->CurrentTime() - start_time_).ms();
    if (drop) {
      RTC_LOG(LS_INFO) << "time=" << timestamp_ms << " : " << name
                       << ": dropping packet len=" << len
                       << ", data[0]: " << static_cast<uint8_t>(data[0]);
    } else {
      RTC_LOG(LS_INFO) << "time=" << timestamp_ms << " : " << name
                       << ": SendPacket, len=" << len
                       << ", data[0]: " << static_cast<uint8_t>(data[0]);
    }
    return drop;
  }

  bool LogBlock(absl::string_view name,
                bool block,
                const char* data,
                size_t len) {
    auto timestamp_ms =
        (time_controller_.GetClock()->CurrentTime() - start_time_).ms();
    if (block) {
      RTC_LOG(LS_INFO) << "time=" << timestamp_ms << " : " << name
                       << ": blocking packet len=" << len
                       << ", data[0]: " << static_cast<uint8_t>(data[0]);
    } else {
      RTC_LOG(LS_INFO) << "time=" << timestamp_ms << " : " << name
                       << ": SendPacket, len=" << len
                       << ", data[0]: " << static_cast<uint8_t>(data[0]);
    }
    return block;
  }

  template <typename Fn>
  bool WaitUntil(Fn func) {
    return webrtc::WaitUntil(func, IsTrue(),
                             {.timeout = TimeDelta::Millis(kTimeout),
                              .clock = &time_controller_})
        .ok();
  }

 protected:
  GlobalSimulatedTimeController time_controller_;
  const Environment env_;
  DtlsTestClient client1_;
  DtlsTestClient client2_;
  bool use_dtls_;
  bool pqc_ = false;
  Timestamp start_time_;
  SSLProtocolVersion ssl_expected_version_;
};

class DtlsTransportInternalImplTest : public DtlsTransportInternalImplTestBase,
                                      public ::testing::Test {};

// Connect without DTLS, and transfer RTP data.
TEST_F(DtlsTransportInternalImplTest, TestTransferRtp) {
  ASSERT_TRUE(Connect());
  TestTransfer(1000, 100, /*srtp=*/false);
}

// Test that the SignalSentPacket signal is wired up.
TEST_F(DtlsTransportInternalImplTest, TestSignalSentPacket) {
  ASSERT_TRUE(Connect());
  // Sanity check default value (-1).
  ASSERT_EQ(client1_.sent_packet().send_time_ms, -1);
  TestTransfer(1000, 100, false);
  // Check that we get the expected fake packet ID, and a time of 0 from the
  // fake clock.
  EXPECT_EQ(kFakePacketId, client1_.sent_packet().packet_id);
  EXPECT_GE(client1_.sent_packet().send_time_ms, 0);
}

// Test that packet options are propagated to the underlying transport.
TEST_F(DtlsTransportInternalImplTest, TestSendPacketWithOptions) {
  PrepareDtls(KT_DEFAULT);
  ASSERT_TRUE(Connect());

  const size_t size = 1000;
  std::unique_ptr<char[]> packet(new char[size]);
  memset(packet.get(), 0, size);
  packet[0] = 0x00;
  SetBE32(std::span<uint8_t>(
              reinterpret_cast<uint8_t*>(packet.get() + kPacketNumOffset), 4),
          0);

  AsyncSocketPacketOptions packet_options;
  packet_options.packet_id = kFakePacketId;
  int rv = client1_.dtls_transport()->SendPacket(packet.get(), size,
                                                 packet_options, 0);
  ASSERT_EQ(static_cast<size_t>(rv), size);

  // Wait for the packet to be received by client2 to ensure it was sent.
  client2_.ExpectPackets(size);
  EXPECT_THAT(
      webrtc::WaitUntil(
          [&] { return client2_.NumPacketsReceived(); }, Eq(1u),
          {.timeout = TimeDelta::Millis(kTimeout), .clock = &time_controller_}),
      IsRtcOk());

  // Now check the sent packet info on client1.
  EXPECT_EQ(kFakePacketId, client1_.sent_packet().packet_id);
  EXPECT_GE(client1_.sent_packet().send_time_ms, 0);
}

// Test that packet options are propagated for SRTP bypass packets.
TEST_F(DtlsTransportInternalImplTest, TestSendSrtpBypassPacketWithOptions) {
  PrepareDtls(KT_DEFAULT);
  ASSERT_TRUE(Connect());

  const size_t size = 1000;
  std::unique_ptr<char[]> packet(new char[size]);
  memset(packet.get(), 0, size);
  packet[0] = kRtpLeadByte;  // Make it look like an SRTP packet.
  SetBE32(std::span<uint8_t>(
              reinterpret_cast<uint8_t*>(packet.get() + kPacketNumOffset), 4),
          0);

  AsyncSocketPacketOptions packet_options;
  packet_options.packet_id = kFakePacketId;
  int rv = client1_.dtls_transport()->SendPacket(
      packet.get(), size, packet_options, PF_SRTP_BYPASS);
  ASSERT_EQ(static_cast<size_t>(rv), size);

  // Wait for the packet to be received by client2 to ensure it was sent.
  client2_.ExpectPackets(size);
  EXPECT_THAT(
      webrtc::WaitUntil(
          [&] { return client2_.NumPacketsReceived(); }, Eq(1u),
          {.timeout = TimeDelta::Millis(kTimeout), .clock = &time_controller_}),
      IsRtcOk());

  // Now check the sent packet info on client1.
  EXPECT_EQ(kFakePacketId, client1_.sent_packet().packet_id);
  EXPECT_GE(client1_.sent_packet().send_time_ms, 0);
}

TEST_F(DtlsTransportInternalImplTest,
       DestructionDeregistersCallbacksFromIceTransport) {
  PrepareDtls(KT_DEFAULT);
  ASSERT_TRUE(Connect());
  client1_.ResetDtlsTransport();
  client1_.fake_ice_transport()->NotifyWritableState(
      client1_.fake_ice_transport());
  client1_.fake_ice_transport()->NotifyReceivingState(
      client1_.fake_ice_transport());
  client1_.fake_ice_transport()->NotifyReadyToSend(
      client1_.fake_ice_transport());
  client1_.fake_ice_transport()->NotifySentPacket(client1_.fake_ice_transport(),
                                                  SentPacketInfo(0, 0));
  client1_.fake_ice_transport()->NotifyNetworkRouteChanged(std::nullopt);
}

TEST_F(DtlsTransportInternalImplTest, TestWriteError) {
  PrepareDtls(KT_DEFAULT);
  FakeSSLStreamAdapter* fake_stream = nullptr;
  client1_.set_ssl_stream_factory(
      [&](const Environment& env, std::unique_ptr<StreamInterface> stream,
          absl::AnyInvocable<void(SSLHandshakeError)>
              handshake_error_callback) {
        auto fake =
            std::make_unique<FakeSSLStreamAdapter>(SSLStreamAdapter::Create(
                env, std::move(stream), std::move(handshake_error_callback)));
        fake->Init();
        fake_stream = fake.get();
        return fake;
      });
  ASSERT_TRUE(Connect());
  ASSERT_TRUE(fake_stream);

  fake_stream->SetWriteError(1);
  AsyncSocketPacketOptions packet_options;
  EXPECT_EQ(client1_.SendPacket(1000, /*srtp=*/false, packet_options), -1);
}

TEST_F(DtlsTransportInternalImplTest, TestPacketOptionsResetAfterWriteError) {
  PrepareDtls(KT_DEFAULT);
  FakeSSLStreamAdapter* fake_stream = nullptr;
  client1_.set_ssl_stream_factory(
      [&](const Environment& env, std::unique_ptr<StreamInterface> stream,
          absl::AnyInvocable<void(SSLHandshakeError)>
              handshake_error_callback) {
        auto fake =
            std::make_unique<FakeSSLStreamAdapter>(SSLStreamAdapter::Create(
                env, std::move(stream), std::move(handshake_error_callback)));
        fake->Init();
        fake_stream = fake.get();
        return fake;
      });
  ASSERT_TRUE(Connect());
  ASSERT_TRUE(fake_stream);

  // Inject a failure to write one packet.
  fake_stream->SetWriteError(1);
  AsyncSocketPacketOptions packet_options;
  packet_options.packet_id = kFakePacketId;
  ASSERT_LT(client1_.SendPacket(1000, /*srtp=*/false, packet_options), 0);

  // Succeed sending a later packet and verify that the packet options used are
  // those of the second packet.
  fake_stream->SetWriteError(std::nullopt);
  AsyncSocketPacketOptions packet_options_2;
  packet_options_2.packet_id = kFakePacketId + 1;
  ASSERT_GT(client1_.SendPacket(1000, /*srtp=*/false, packet_options_2), 0);
  EXPECT_EQ(packet_options_2.packet_id, client1_.sent_packet().packet_id);
}

// Connect without DTLS, and transfer SRTP data.
TEST_F(DtlsTransportInternalImplTest, TestTransferSrtp) {
  ASSERT_TRUE(Connect());
  TestTransfer(1000, 100, /*srtp=*/true);
}

// Connect with DTLS, and transfer data over DTLS.
TEST_F(DtlsTransportInternalImplTest, TestTransferDtls) {
  PrepareDtls(KT_DEFAULT);
  ASSERT_TRUE(Connect());
  TestTransfer(1000, 100, /*srtp=*/false);
}

// Connect with DTLS, combine multiple DTLS records into one packet.
// Our DTLS implementation doesn't do this, but other implementations may;
// see https://tools.ietf.org/html/rfc6347#section-4.1.1.
// This has caused interoperability problems with ORTCLib in the past.
TEST_F(DtlsTransportInternalImplTest, TestTransferDtlsCombineRecords) {
  PrepareDtls(KT_DEFAULT);
  ASSERT_TRUE(Connect());
  // Our DTLS implementation always sends one record per packet, so to simulate
  // an endpoint that sends multiple records per packet, we configure the fake
  // ICE transport to combine every two consecutive packets into a single
  // packet.
  FakeIceTransportInternal* transport = client1_.fake_ice_transport();
  transport->combine_outgoing_packets(true);
  TestTransfer(500, 100, /*srtp=*/false);
}

TEST_F(DtlsTransportInternalImplTest, KeyingMaterialExporter) {
  PrepareDtls(KT_DEFAULT);
  ASSERT_TRUE(Connect());

  ZeroOnFreeBuffer<uint8_t> client1_out;
  ZeroOnFreeBuffer<uint8_t> client2_out;
  EXPECT_TRUE(client1_.dtls_transport()->AppendSrtpKeyingMaterial(client1_out));
  EXPECT_TRUE(client2_.dtls_transport()->AppendSrtpKeyingMaterial(client2_out));
  EXPECT_EQ(client1_out, client2_out);
}

enum HandshakeTestEvent {
  EV_CLIENT_SEND = 0,
  EV_SERVER_SEND = 1,
  EV_CLIENT_RECV = 2,
  EV_SERVER_RECV = 3,
  EV_CLIENT_WRITABLE = 4,
  EV_SERVER_WRITABLE = 5,

  EV_CLIENT_SEND_DROPPED = 6,
  EV_SERVER_SEND_DROPPED = 7,
};

template <typename Sink>
void AbslStringify(Sink& sink, HandshakeTestEvent event) {
  switch (event) {
    case EV_CLIENT_SEND:
      sink.Append("C-SEND");
      return;
    case EV_SERVER_SEND:
      sink.Append("S-SEND");
      return;
    case EV_CLIENT_RECV:
      sink.Append("C-RECV");
      return;
    case EV_SERVER_RECV:
      sink.Append("S-RECV");
      return;
    case EV_CLIENT_WRITABLE:
      sink.Append("C-WRITABLE");
      return;
    case EV_SERVER_WRITABLE:
      sink.Append("S-WRITABLE");
      return;
    case EV_CLIENT_SEND_DROPPED:
      sink.Append("C-SEND-DROPPED");
      return;
    case EV_SERVER_SEND_DROPPED:
      sink.Append("S-SEND-DROPPED");
      return;
  }
}

const std::vector<HandshakeTestEvent> dtls_12_handshake_events{
    // Flight 1
    EV_CLIENT_SEND,
    EV_SERVER_RECV,
    EV_SERVER_SEND,
    EV_CLIENT_RECV,

    // Flight 2
    EV_CLIENT_SEND,
    EV_SERVER_RECV,
    EV_SERVER_SEND,
    EV_SERVER_WRITABLE,
    EV_CLIENT_RECV,
    EV_CLIENT_WRITABLE,
};

const std::vector<HandshakeTestEvent> dtls_13_handshake_events{
    // Flight 1
    EV_CLIENT_SEND,
    EV_SERVER_RECV,
    EV_SERVER_SEND,
    EV_CLIENT_RECV,

    // Flight 2
    EV_CLIENT_SEND,
    EV_CLIENT_WRITABLE,
    EV_SERVER_RECV,
    EV_SERVER_SEND,
    EV_SERVER_WRITABLE,
};

const std::vector<HandshakeTestEvent> dtls_pqc_handshake_events{
    // Flight 1
    EV_CLIENT_SEND,
    EV_CLIENT_SEND,
    EV_SERVER_RECV,
    EV_SERVER_RECV,
    EV_SERVER_SEND,
    EV_SERVER_SEND,
    EV_CLIENT_RECV,
    EV_CLIENT_RECV,

    // Flight 2
    EV_CLIENT_SEND,
    EV_CLIENT_WRITABLE,
    EV_SERVER_RECV,
    EV_SERVER_SEND,
    EV_SERVER_WRITABLE,
};

const struct {
  int version_bytes;
  const std::vector<HandshakeTestEvent>& events;
} kEventsPerVersion[] = {
    {.version_bytes = kDtls12VersionBytes, .events = dtls_12_handshake_events},
    {.version_bytes = kDtls13VersionBytes, .events = dtls_13_handshake_events},
};

struct EndpointConfig {
  SSLProtocolVersion max_protocol_version;
  bool dtls_in_stun = false;
  std::optional<IceRole> ice_role;
  std::optional<SSLRole> ssl_role;
  bool pqc = false;

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const EndpointConfig& config) {
    sink.Append("[ dtls: ");
    sink.Append(config.ssl_role == SSL_SERVER ? "server/" : "client/");
    switch (config.max_protocol_version) {
      case SSL_PROTOCOL_DTLS_10:
        sink.Append("1.0");
        break;
      case SSL_PROTOCOL_DTLS_12:
        sink.Append("1.2");
        break;
      case SSL_PROTOCOL_DTLS_13:
        sink.Append("1.3");
        break;
      default:
        sink.Append("<unknown>");
        break;
    }
    absl::Format(&sink, " dtls_in_stun: %u ice: ", config.dtls_in_stun);
    sink.Append(config.ice_role == ICEROLE_CONTROLLED ? "controlled"
                                                      : "controlling");
    absl::Format(&sink, " pqc: %u", config.pqc);
    sink.Append(" ]");
  }

  int GetFirstFlightPackets() const {
    if (pqc) {
      return 2;
    } else {
      return 1;
    }
  }
};

class DtlsTransportInternalImplVersionTest
    : public DtlsTransportInternalImplTestBase,
      public ::testing::TestWithParam<
          std::tuple<EndpointConfig, EndpointConfig>> {
 public:
  void Prepare(bool rtt_estimate = true) {
    PrepareDtls(KT_DEFAULT);
    const auto& config1 = std::get<0>(GetParam());
    const auto& config2 = std::get<1>(GetParam());
    SetMaxProtocolVersions(config1.max_protocol_version,
                           config2.max_protocol_version);

    client1_.set_async_delay(50);
    client2_.set_async_delay(50);

    client1_.SetPqc(config1.pqc);
    client2_.SetPqc(config2.pqc);

    client1_.SetupTransports(
        env_, config1.ice_role.value_or(ICEROLE_CONTROLLING), rtt_estimate);
    client2_.SetupTransports(
        env_, config2.ice_role.value_or(ICEROLE_CONTROLLED), rtt_estimate);
    client1_.dtls_transport()->SetDtlsRole(
        config1.ssl_role.value_or(SSL_CLIENT));
    client2_.dtls_transport()->SetDtlsRole(
        config2.ssl_role.value_or(SSL_SERVER));

    // No SDP exchange in this fixture; stand in for JsepTransport's call.
    if (config1.dtls_in_stun) {
      auto config = client1_.fake_ice_transport()->config();
      config.dtls_handshake_in_stun = true;
      client1_.fake_ice_transport()->SetIceConfig(config);
      client1_.dtls_transport()->MaybeStartDtlsInStun();
    }
    if (config2.dtls_in_stun) {
      auto config = client2_.fake_ice_transport()->config();
      config.dtls_handshake_in_stun = true;
      client2_.fake_ice_transport()->SetIceConfig(config);
      client2_.dtls_transport()->MaybeStartDtlsInStun();
    }

    SetRemoteFingerprintFromCert(client1_.dtls_transport(),
                                 client2_.certificate());
    SetRemoteFingerprintFromCert(client2_.dtls_transport(),
                                 client1_.certificate());
  }

  // Run DTLS handshake.
  // - store events in `events`
  // - drop packets as specified in `packets_to_drop`
  std::pair</* dtls_version_bytes*/ int, std::vector<HandshakeTestEvent>>
  RunHandshake(std::set<unsigned> packets_to_drop) {
    std::vector<HandshakeTestEvent> events;
    client1_.fake_ice_transport()->set_packet_recv_filter(
        [&](auto packet, auto timestamp_us) {
          events.push_back(EV_CLIENT_RECV);
          return LogRecv("client", packet);
        });
    client2_.fake_ice_transport()->set_packet_recv_filter(
        [&](auto packet, auto timestamp_us) {
          events.push_back(EV_SERVER_RECV);
          return LogRecv("server", packet);
        });
    client1_.set_writable_callback(
        [&]() { events.push_back(EV_CLIENT_WRITABLE); });
    client2_.set_writable_callback(
        [&]() { events.push_back(EV_SERVER_WRITABLE); });

    unsigned packet_num = 0;
    client1_.fake_ice_transport()->set_packet_send_filter(
        [&](auto data, auto len, auto options, auto flags) {
          auto packet_type = options.info_signaled_after_sent.packet_type;
          if (packet_type == PacketType::kIceConnectivityCheck ||
              packet_type == PacketType::kIceConnectivityCheckResponse) {
            // Ignore stun pings for now.
            return LogSend("client-stun", /* drop= */ false, data, len);
          }
          bool drop = packets_to_drop.find(packet_num) != packets_to_drop.end();
          packet_num++;
          if (!drop) {
            events.push_back(EV_CLIENT_SEND);
          } else {
            events.push_back(EV_CLIENT_SEND_DROPPED);
          }
          return LogSend("client", drop, data, len);
        });
    client2_.fake_ice_transport()->set_packet_send_filter(
        [&](auto data, auto len, auto options, auto flags) {
          auto packet_type = options.info_signaled_after_sent.packet_type;
          if (packet_type == PacketType::kIceConnectivityCheck ||
              packet_type == PacketType::kIceConnectivityCheckResponse) {
            // Ignore stun pings for now.
            return LogSend("server-stun", /* drop= */ false, data, len);
          }
          bool drop = packets_to_drop.find(packet_num) != packets_to_drop.end();
          packet_num++;
          if (!drop) {
            events.push_back(EV_SERVER_SEND);
          } else {
            events.push_back(EV_SERVER_SEND_DROPPED);
          }
          return LogSend("server", drop, data, len);
        });

    EXPECT_TRUE(client1_.ConnectIceTransport(&client2_));
    client1_.SendIcePing(std::get<0>(GetParam()).GetFirstFlightPackets());
    client2_.SendIcePingConf(std::get<0>(GetParam()).GetFirstFlightPackets());
    client2_.SendIcePing();
    client1_.SendIcePingConf();

    EXPECT_TRUE(WaitUntil([&] {
      return client1_.dtls_transport()->writable() &&
             client2_.dtls_transport()->writable();
    }));

    ClearPacketFilters();

    auto dtls_version_bytes = client1_.GetVersionBytes();
    EXPECT_EQ(dtls_version_bytes, client2_.GetVersionBytes());
    return std::make_pair(dtls_version_bytes.value_or(0), std::move(events));
  }

  int GetExpectedDtlsVersionBytes() {
    int version = std::min(
        static_cast<int>(std::get<0>(GetParam()).max_protocol_version),
        static_cast<int>(std::get<1>(GetParam()).max_protocol_version));
    if (version == SSL_PROTOCOL_DTLS_13) {
      return kDtls13VersionBytes;
    } else {
      return kDtls12VersionBytes;
    }
  }

  std::vector<HandshakeTestEvent> GetExpectedEvents(int dtls_version_bytes,
                                                    bool pqc = false) {
    if (pqc) {
      return dtls_pqc_handshake_events;
    }
    for (const auto e : kEventsPerVersion) {
      if (e.version_bytes == dtls_version_bytes) {
        return e.events;
      }
    }
    return {};
  }
};

const EndpointConfig kEndpointVariants[] = {
    {
        .max_protocol_version = SSL_PROTOCOL_DTLS_10,
        .dtls_in_stun = false,
    },
    {
        .max_protocol_version = SSL_PROTOCOL_DTLS_12,
        .dtls_in_stun = false,
    },
    {
        .max_protocol_version = SSL_PROTOCOL_DTLS_13,
        .dtls_in_stun = false,
    },
    {
        .max_protocol_version = SSL_PROTOCOL_DTLS_13,
        .dtls_in_stun = false,
        .pqc = true,
    },
    {
        .max_protocol_version = SSL_PROTOCOL_DTLS_10,
        .dtls_in_stun = true,
    },
    {
        .max_protocol_version = SSL_PROTOCOL_DTLS_12,
        .dtls_in_stun = true,
    },
    {
        .max_protocol_version = SSL_PROTOCOL_DTLS_13,
        .dtls_in_stun = true,
    },
    {
        .max_protocol_version = SSL_PROTOCOL_DTLS_13,
        .dtls_in_stun = true,
        .pqc = true,
    },
};

// Will test every combination of 1.0/1.2/1.3 on the client and server.
// DTLS will negotiate an effective version (the min of client & sewrver).
INSTANTIATE_TEST_SUITE_P(
    DtlsTransportInternalImplVersionTest,
    DtlsTransportInternalImplVersionTest,
    ::testing::Combine(testing::ValuesIn(kEndpointVariants),
                       testing::ValuesIn(kEndpointVariants)));

// Test that an acceptable cipher suite is negotiated when different versions
// of DTLS are supported. Note that it's IsAcceptableCipher that does the actual
// work.
TEST_P(DtlsTransportInternalImplVersionTest, CipherSuiteNegotiation) {
  Prepare();
  ASSERT_TRUE(Connect());
}

TEST_P(DtlsTransportInternalImplVersionTest, HandshakeFlights) {
  if (!SSLStreamAdapter::IsBoringSsl()) {
    GTEST_SKIP() << "Needs boringssl.";
  }
  if (std::get<0>(GetParam()).dtls_in_stun ||
      (std::get<0>(GetParam()).dtls_in_stun &&
       std::get<1>(GetParam()).dtls_in_stun)) {
    GTEST_SKIP() << "This test does not support dtls in stun";
  }
  if ((std::get<0>(GetParam()).GetFirstFlightPackets() > 1) !=
      (std::get<1>(GetParam()).GetFirstFlightPackets() > 1)) {
    GTEST_SKIP() << "This test does not support one sided pqc";
  }
  bool pqc = std::get<0>(GetParam()).GetFirstFlightPackets() > 1;

  if (pqc && std::get<1>(GetParam()).dtls_in_stun) {
    // TODO(jonaso,webrtc:367395350): Remove once we have more clever MTU
    // handling.
    GTEST_SKIP() << "This test does not support pqc with dtls-in-stun.";
  }

  Prepare();
  auto [dtls_version_bytes, events] = RunHandshake({});

  RTC_LOG(LS_INFO) << "Verifying events with ssl version bytes= "
                   << dtls_version_bytes;
  auto expect = GetExpectedEvents(dtls_version_bytes, pqc);
  EXPECT_EQ(events, expect);
}

TEST_P(DtlsTransportInternalImplVersionTest, HandshakeLoseFirstClientPacket) {
  if (!SSLStreamAdapter::IsBoringSsl()) {
    GTEST_SKIP() << "Needs boringssl.";
  }
  if (std::get<0>(GetParam()).dtls_in_stun ||
      (std::get<0>(GetParam()).dtls_in_stun &&
       std::get<1>(GetParam()).dtls_in_stun)) {
    GTEST_SKIP() << "This test does not support dtls in stun";
  }
  if (std::get<0>(GetParam()).GetFirstFlightPackets() > 1) {
    GTEST_SKIP() << "This test does not support pqc";
  }

  Prepare();
  auto [dtls_version_bytes, events] = RunHandshake({/* packet_num= */ 0});

  auto expect = GetExpectedEvents(dtls_version_bytes);

  // If first packet is lost...it is simply retransmitted by client,
  // nothing else changes.
  expect.insert(expect.begin(), EV_CLIENT_SEND_DROPPED);

  EXPECT_EQ(events, expect);
}

TEST_P(DtlsTransportInternalImplVersionTest,
       PqcHandshakeLoseFirstClientPacket) {
  if (!SSLStreamAdapter::IsBoringSsl()) {
    GTEST_SKIP() << "Needs boringssl.";
  }
  if (std::get<0>(GetParam()).dtls_in_stun ||
      std::get<1>(GetParam()).dtls_in_stun) {
    GTEST_SKIP() << "This test does not support dtls in stun";
  }
  if (std::get<0>(GetParam()).GetFirstFlightPackets() == 1 ||
      std::get<1>(GetParam()).GetFirstFlightPackets() == 1) {
    GTEST_SKIP() << "This test need not support pqc";
  }

  Prepare();
  auto [dtls_version_bytes, events] = RunHandshake({/* packet_num= */ 0});

  const std::vector<HandshakeTestEvent> expect = {
      EV_CLIENT_SEND_DROPPED,  // p1
      EV_CLIENT_SEND,          // p2
      EV_SERVER_RECV,          // p2

      EV_CLIENT_SEND,  // p1 (retransmit)
      EV_CLIENT_SEND,  // p2 (retransmit)

      EV_SERVER_RECV,  // p1
      EV_SERVER_SEND,
      EV_SERVER_SEND,
      EV_SERVER_RECV,  // p2 (retransmit)
      EV_CLIENT_RECV,
      EV_CLIENT_RECV,

      // Flight 2
      EV_CLIENT_SEND,
      EV_CLIENT_WRITABLE,

      EV_SERVER_SEND,  // unknown??

      EV_SERVER_RECV,
      EV_SERVER_SEND,
      EV_SERVER_WRITABLE,

      EV_CLIENT_RECV,  // unknown??
  };

  EXPECT_EQ(events, expect);
}

TEST_P(DtlsTransportInternalImplVersionTest,
       PqcHandshakeLoseSecondClientPacket) {
  if (!SSLStreamAdapter::IsBoringSsl()) {
    GTEST_SKIP() << "Needs boringssl.";
  }
  if (std::get<0>(GetParam()).dtls_in_stun ||
      std::get<1>(GetParam()).dtls_in_stun) {
    GTEST_SKIP() << "This test does not support dtls in stun";
  }
  if (std::get<0>(GetParam()).GetFirstFlightPackets() == 1 ||
      std::get<1>(GetParam()).GetFirstFlightPackets() == 1) {
    GTEST_SKIP() << "This test need not support pqc";
  }

  Prepare();
  auto [dtls_version_bytes, events] = RunHandshake({/* packet_num= */ 1});

  const std::vector<HandshakeTestEvent> expect = {
      EV_CLIENT_SEND,          // p1
      EV_CLIENT_SEND_DROPPED,  // p2
      EV_SERVER_RECV,          // p1

      EV_CLIENT_SEND,  // p1 (retransmit)
      EV_CLIENT_SEND,  // p2 (retransmit)

      EV_SERVER_RECV,  // p1
      EV_SERVER_RECV,  // p2
      EV_SERVER_SEND,
      EV_SERVER_SEND,
      EV_CLIENT_RECV,
      EV_CLIENT_RECV,

      // Flight 2
      EV_CLIENT_SEND,
      EV_CLIENT_WRITABLE,

      EV_SERVER_RECV,
      EV_SERVER_SEND,
      EV_SERVER_WRITABLE,
  };

  EXPECT_EQ(events, expect);
}

TEST_P(DtlsTransportInternalImplVersionTest, HandshakeLoseSecondClientPacket) {
  if (!SSLStreamAdapter::IsBoringSsl()) {
    GTEST_SKIP() << "Needs boringssl.";
  }
  if (std::get<0>(GetParam()).dtls_in_stun ||
      (std::get<0>(GetParam()).dtls_in_stun &&
       std::get<1>(GetParam()).dtls_in_stun)) {
    GTEST_SKIP() << "This test does not support dtls in stun";
  }
  if (std::get<0>(GetParam()).GetFirstFlightPackets() > 1) {
    GTEST_SKIP() << "This test does not support pqc";
  }

  Prepare();
  auto [dtls_version_bytes, events] = RunHandshake({/* packet_num= */ 2});

  std::vector<HandshakeTestEvent> expect;

  switch (dtls_version_bytes) {
    case kDtls12VersionBytes:
      expect = {
          // Flight 1
          EV_CLIENT_SEND,
          EV_SERVER_RECV,
          EV_SERVER_SEND,
          EV_CLIENT_RECV,

          // Flight 2
          EV_CLIENT_SEND_DROPPED,

          // Server retransmit.
          EV_SERVER_SEND,
          // Client retransmit.
          EV_CLIENT_SEND,
          // Client receive retransmit => Do nothing, has already retransmitted.
          EV_CLIENT_RECV,
          // Handshake resume.
          EV_SERVER_RECV,
          EV_SERVER_SEND,
          EV_SERVER_WRITABLE,
          EV_CLIENT_RECV,
          EV_CLIENT_WRITABLE,
      };
      break;
    case kDtls13VersionBytes:
      expect = {
          // Flight 1
          EV_CLIENT_SEND,
          EV_SERVER_RECV,
          EV_SERVER_SEND,
          EV_CLIENT_RECV,

          // Flight 2
          EV_CLIENT_SEND_DROPPED,
          // Client doesn't know packet it is dropped, so it becomes writable.
          EV_CLIENT_WRITABLE,

          // Server retransmit.
          EV_SERVER_SEND,
          // Client retransmit.
          EV_CLIENT_SEND,

          // Client receive retransmit => Do nothing, has already retransmitted.
          EV_CLIENT_RECV,
          // Handshake resume.
          EV_SERVER_RECV,
          EV_SERVER_SEND,
          EV_SERVER_WRITABLE,
      };
      break;
    default:
      FAIL() << "Unknown dtls version bytes: " << dtls_version_bytes;
  }
  EXPECT_EQ(events, expect);
}

// Connect with DTLS, negotiating DTLS-SRTP, and transfer SRTP using bypass.
TEST_F(DtlsTransportInternalImplTest, TestTransferDtlsSrtp) {
  PrepareDtls(KT_DEFAULT);
  ASSERT_TRUE(Connect());
  TestTransfer(1000, 100, /*srtp=*/true);
}

// Connect with DTLS-SRTP, transfer an invalid SRTP packet, and expects -1
// returned.
TEST_F(DtlsTransportInternalImplTest, TestTransferDtlsInvalidSrtpPacket) {
  PrepareDtls(KT_DEFAULT);
  ASSERT_TRUE(Connect());
  EXPECT_EQ(-1, client1_.SendInvalidSrtpPacket(100));
}

// Create a single transport with DTLS, and send normal data and SRTP data on
// it.
TEST_F(DtlsTransportInternalImplTest, TestTransferDtlsSrtpDemux) {
  PrepareDtls(KT_DEFAULT);
  ASSERT_TRUE(Connect());
  TestTransfer(1000, 100, /*srtp=*/false);
  TestTransfer(1000, 100, /*srtp=*/true);
}

// Test transferring when the "answerer" has the server role.
TEST_F(DtlsTransportInternalImplTest, TestTransferDtlsSrtpAnswererIsPassive) {
  PrepareDtls(KT_DEFAULT);
  ASSERT_TRUE(Connect(/*client1_server=*/false));
  TestTransfer(1000, 100, /*srtp=*/true);
}

// Test that renegotiation (setting same role and fingerprint again) can be
// started before the clients become connected in the first negotiation.
TEST_F(DtlsTransportInternalImplTest, TestRenegotiateBeforeConnect) {
  PrepareDtls(KT_DEFAULT);
  // Note: This is doing the same thing Connect normally does, minus some
  // additional checks not relevant for this test.
  Negotiate();
  Negotiate();
  EXPECT_TRUE(client1_.Connect(&client2_, false));
  EXPECT_TRUE(WaitUntil([&] {
    return client1_.dtls_transport()->writable() &&
           client2_.dtls_transport()->writable();
  }));
  TestTransfer(1000, 100, true);
}

// Test Certificates state after negotiation but before connection.
TEST_F(DtlsTransportInternalImplTest, TestCertificatesBeforeConnect) {
  PrepareDtls(KT_DEFAULT);
  Negotiate();

  // After negotiation, each side has a distinct local certificate, but still no
  // remote certificate, because connection has not yet occurred.
  auto certificate1 =
      client1_.dtls_transport()->GetLocalCertificateForTesting();
  auto certificate2 =
      client2_.dtls_transport()->GetLocalCertificateForTesting();
  ASSERT_NE(certificate1->GetSSLCertificate().ToPEMString(),
            certificate2->GetSSLCertificate().ToPEMString());
  ASSERT_FALSE(client1_.dtls_transport()->GetRemoteSSLCertChain());
  ASSERT_FALSE(client2_.dtls_transport()->GetRemoteSSLCertChain());
}

// Test Certificates state after connection.
TEST_F(DtlsTransportInternalImplTest, TestCertificatesAfterConnect) {
  PrepareDtls(KT_DEFAULT);
  ASSERT_TRUE(Connect());

  // After connection, each side has a distinct local certificate.
  auto certificate1 =
      client1_.dtls_transport()->GetLocalCertificateForTesting();
  auto certificate2 =
      client2_.dtls_transport()->GetLocalCertificateForTesting();
  ASSERT_NE(certificate1->GetSSLCertificate().ToPEMString(),
            certificate2->GetSSLCertificate().ToPEMString());

  // Each side's remote certificate is the other side's local certificate.
  std::unique_ptr<SSLCertChain> remote_cert1 =
      client1_.dtls_transport()->GetRemoteSSLCertChain();
  ASSERT_TRUE(remote_cert1);
  ASSERT_EQ(1u, remote_cert1->GetSize());
  ASSERT_EQ(remote_cert1->Get(0).ToPEMString(),
            certificate2->GetSSLCertificate().ToPEMString());
  std::unique_ptr<SSLCertChain> remote_cert2 =
      client2_.dtls_transport()->GetRemoteSSLCertChain();
  ASSERT_TRUE(remote_cert2);
  ASSERT_EQ(1u, remote_cert2->GetSize());
  ASSERT_EQ(remote_cert2->Get(0).ToPEMString(),
            certificate1->GetSSLCertificate().ToPEMString());
}

TEST_F(DtlsTransportInternalImplTest, TestImplicitRoleDetection) {
  PrepareDtls(KT_DEFAULT);

  client1_.SetupTransports(env_, ICEROLE_CONTROLLING);
  client2_.SetupTransports(env_, ICEROLE_CONTROLLED);

  client2_.dtls_transport()->SetDtlsRole(SSL_CLIENT);

  SetRemoteFingerprintFromCert(client2_.dtls_transport(),
                               client1_.certificate());

  client2_.Connect(&client1_, false);

  EXPECT_THAT(
      webrtc::WaitUntil(
          [&] {
            SSLRole role;
            return client1_.dtls_transport()->GetDtlsRole(&role) &&
                   role == SSL_SERVER;
          },
          IsTrue(),
          {.timeout = TimeDelta::Millis(kTimeout), .clock = &time_controller_}),
      IsRtcOk());
}

TEST_F(DtlsTransportInternalImplTest,
       NoEarlyDtlsInStunStartWithoutPeerSupport) {
  if (!SSLStreamAdapter::IsBoringSsl()) {
    GTEST_SKIP() << "DTLS-in-STUN requires BoringSSL.";
  }
  PrepareDtls(KT_DEFAULT);

  client1_.SetupTransports(env_, ICEROLE_CONTROLLING);

  auto ice_config = client1_.fake_ice_transport()->config();
  ice_config.dtls_handshake_in_stun = true;
  client1_.fake_ice_transport()->SetIceConfig(ice_config);
  ASSERT_FALSE(client1_.fake_ice_transport()->writable());

  client1_.dtls_transport()->SetDtlsRole(SSL_SERVER);
  SetRemoteFingerprintFromCert(client1_.dtls_transport(),
                               client2_.certificate());

  // Without DTLS-in-STUN, DTLS starts after ICE becomes writable.
  EXPECT_EQ(client1_.dtls_transport()->dtls_state(), DtlsTransportState::kNew);

  client1_.fake_ice_transport()->SetWritable(true);
  EXPECT_EQ(client1_.dtls_transport()->dtls_state(),
            DtlsTransportState::kConnecting);
}

// MaybeStartDtlsInStun() confirms peer support; DTLS starts before ICE is
// writable.
TEST_F(DtlsTransportInternalImplTest,
       EarlyDtlsInStunStartAfterPeerSupportConfirmed) {
  if (!SSLStreamAdapter::IsBoringSsl()) {
    GTEST_SKIP() << "DTLS-in-STUN requires BoringSSL.";
  }
  PrepareDtls(KT_DEFAULT);

  client1_.SetupTransports(env_, ICEROLE_CONTROLLING);

  auto ice_config = client1_.fake_ice_transport()->config();
  ice_config.dtls_handshake_in_stun = true;
  client1_.fake_ice_transport()->SetIceConfig(ice_config);
  ASSERT_FALSE(client1_.fake_ice_transport()->writable());

  client1_.dtls_transport()->MaybeStartDtlsInStun();

  client1_.dtls_transport()->SetDtlsRole(SSL_SERVER);
  SetRemoteFingerprintFromCert(client1_.dtls_transport(),
                               client2_.certificate());

  // With DTLS-in-STUN, DTLS starts immediately.
  EXPECT_EQ(client1_.dtls_transport()->dtls_state(),
            DtlsTransportState::kConnecting);
}

// A piggybacked ClientHello arriving before the answer has been applied starts
// DTLS even though the ice-option has not been negotiated.
TEST_F(DtlsTransportInternalImplTest, EarlyDtlsInStunStartOnPiggybackedHello) {
  if (!SSLStreamAdapter::IsBoringSsl()) {
    GTEST_SKIP() << "DTLS-in-STUN requires BoringSSL.";
  }
  PrepareDtls(KT_DEFAULT);

  // Deliver packets synchronously.
  client1_.set_async_delay(0);
  client2_.set_async_delay(0);

  // The ice config must be set before the DTLS transport is created since that
  // is when the piggybacking callbacks are registered.
  client1_.SetupTransports(env_, ICEROLE_CONTROLLING, /*rtt_estimate=*/true,
                           /*dtls_in_stun=*/true);
  client2_.SetupTransports(env_, ICEROLE_CONTROLLED, /*rtt_estimate=*/true,
                           /*dtls_in_stun=*/true);

  // client2_ applied the answer and starts the handshake as the DTLS client.
  // client1_ has not applied it so it knows neither the remote fingerprint nor
  // that SPED was negotiated.
  client2_.dtls_transport()->MaybeStartDtlsInStun();
  client2_.dtls_transport()->SetDtlsRole(SSL_CLIENT);
  SetRemoteFingerprintFromCert(client2_.dtls_transport(),
                               client1_.certificate());
  ASSERT_EQ(client2_.dtls_transport()->dtls_state(),
            DtlsTransportState::kConnecting);
  ASSERT_EQ(client1_.dtls_transport()->dtls_state(), DtlsTransportState::kNew);

  ASSERT_TRUE(client2_.ConnectIceTransport(&client1_));
  ASSERT_TRUE(client2_.SendIcePing());
  ASSERT_FALSE(client1_.fake_ice_transport()->writable());

  EXPECT_EQ(client1_.dtls_transport()->dtls_state(),
            DtlsTransportState::kConnecting);
}

// Test that packets are retransmitted according to the expected schedule.
// Each time a timeout occurs, the retransmission timer should be doubled up to
// 60 seconds. The timer defaults to 1 second, but for WebRTC we should be
// initializing it to 50ms.
TEST_F(DtlsTransportInternalImplTest, TestRetransmissionSchedule) {
  if (!SSLStreamAdapter::IsBoringSsl()) {
    // We can only change the retransmission schedule with a recently-added
    // BoringSSL API. Skip the test if not built with BoringSSL.
    GTEST_SKIP() << "Needs boringssl.";
  }
  PrepareDtls(KT_DEFAULT);

  // This test is written with assumption of 0 delay
  // which affect the hard coded schedule below.
  client1_.set_async_delay(0);
  client2_.set_async_delay(0);

  // Exchange fingerprints and set SSL roles.
  Negotiate();

  // Make client2_ writable, but not client1_.
  // This means client1_ will send DTLS client hellos but get no response.
  EXPECT_TRUE(client2_.Connect(&client1_, true));
  EXPECT_TRUE(
      WaitUntil([&] { return client2_.fake_ice_transport()->writable(); }));

  // Wait for the first client hello to be sent.
  EXPECT_TRUE(
      WaitUntil([&] { return client1_.received_dtls_client_hellos(); }));
  EXPECT_FALSE(client1_.fake_ice_transport()->writable());

  static int timeout_schedule_ms[] = {50,   100,  200,   400,   800,   1600,
                                      3200, 6400, 12800, 25600, 51200, 60000};

  int expected_hellos = 1;
  for (size_t i = 0;
       i < (sizeof(timeout_schedule_ms) / sizeof(timeout_schedule_ms[0]));
       ++i) {
    // For each expected retransmission time, advance the fake clock a
    // millisecond before the expected time and verify that no unexpected
    // retransmissions were sent. Then advance it the final millisecond and
    // verify that the expected retransmission was sent.
    time_controller_.AdvanceTime(TimeDelta::Millis(timeout_schedule_ms[i] - 1));
    EXPECT_EQ(expected_hellos, client1_.received_dtls_client_hellos());
    time_controller_.AdvanceTime(TimeDelta::Millis(1));
    EXPECT_EQ(++expected_hellos, client1_.received_dtls_client_hellos());
  }
}

TEST_F(DtlsTransportInternalImplTest, DtlsConnectionTimeMetric) {
  metrics::Reset();
  PrepareDtls(KT_DEFAULT);
  EXPECT_METRIC_EQ(
      metrics::NumSamples("WebRTC.PeerConnection.DtlsClientRoleConnectionTime"),
      0);
  EXPECT_METRIC_EQ(
      metrics::NumSamples("WebRTC.PeerConnection.DtlsServerRoleConnectionTime"),
      0);
  ASSERT_TRUE(Connect());
  EXPECT_METRIC_EQ(
      metrics::NumSamples("WebRTC.PeerConnection.DtlsClientRoleConnectionTime"),
      1);
  EXPECT_METRIC_EQ(
      metrics::NumSamples("WebRTC.PeerConnection.DtlsServerRoleConnectionTime"),
      1);
}

TEST_F(DtlsTransportInternalImplTest, DtlsVersionMetric) {
  metrics::Reset();
  PrepareDtls(KT_DEFAULT);
  EXPECT_METRIC_EQ(
      metrics::NumSamples("WebRTC.PeerConnection.DtlsVersionClientRole"), 0);
  EXPECT_METRIC_EQ(
      metrics::NumSamples("WebRTC.PeerConnection.DtlsVersionServerRole"), 0);
  ASSERT_TRUE(Connect());
  EXPECT_METRIC_EQ(
      metrics::NumSamples("WebRTC.PeerConnection.DtlsVersionClientRole"), 1);
  EXPECT_METRIC_EQ(
      metrics::NumSamples("WebRTC.PeerConnection.DtlsVersionServerRole"), 1);
}

// The following events can occur in many different orders:
// 1. Caller receives remote fingerprint.
// 2. Caller is writable.
// 3. Caller receives ClientHello.
// 4. DTLS handshake finishes.
//
// The tests below cover all causally consistent permutations of these events;
// the caller must be writable and receive a ClientHello before the handshake
// finishes, but otherwise any ordering is possible.
//
// For each permutation, the test verifies that a connection is established and
// fingerprint verified without any DTLS packet needing to be retransmitted.
//
// Each permutation is also tested with valid and invalid fingerprints,
// ensuring that the handshake fails with an invalid fingerprint.
enum DtlsTransportInternalImplEvent {
  CALLER_RECEIVES_FINGERPRINT,
  CALLER_WRITABLE,
  CALLER_RECEIVES_CLIENTHELLO,
  HANDSHAKE_FINISHES
};

class DtlsEventOrderingTest
    : public DtlsTransportInternalImplTestBase,
      public ::testing::TestWithParam<
          ::testing::tuple<std::vector<DtlsTransportInternalImplEvent>,
                           bool /* valid_fingerprint */,
                           SSLProtocolVersion,
                           bool /* pqc */>> {
 protected:
  // If `valid_fingerprint` is false, the caller will receive a fingerprint
  // that doesn't match the callee's certificate, so the handshake should fail.
  void TestEventOrdering(
      const std::vector<DtlsTransportInternalImplEvent>& events,
      bool valid_fingerprint) {
    bool pqc = ::testing::get<3>(GetParam());
    if (pqc && ::testing::get<2>(GetParam()) != SSL_PROTOCOL_DTLS_13) {
      GTEST_SKIP() << "PQC requires DTLS1.3";
    }
    if (pqc && !SSLStreamAdapter::IsBoringSsl()) {
      GTEST_SKIP() << "PQC needs boringssl.";
    }

    SetPqc(::testing::get<3>(GetParam()));
    SetMaxProtocolVersions(::testing::get<2>(GetParam()),
                           ::testing::get<2>(GetParam()));

    // Pre-setup: Set local certificate on both caller and callee, and
    // remote fingerprint on callee, but neither is writable and the caller
    // doesn't have the callee's fingerprint.
    PrepareDtls(KT_DEFAULT);
    client1_.SetupTransports(env_, ICEROLE_CONTROLLING);
    client2_.SetupTransports(env_, ICEROLE_CONTROLLED);
    // Similar to how NegotiateOrdering works.
    client1_.dtls_transport()->SetDtlsRole(SSL_SERVER);
    client2_.dtls_transport()->SetDtlsRole(SSL_CLIENT);
    SetRemoteFingerprintFromCert(client2_.dtls_transport(),
                                 client1_.certificate());

    for (DtlsTransportInternalImplEvent e : events) {
      switch (e) {
        case CALLER_RECEIVES_FINGERPRINT:
          if (valid_fingerprint) {
            SetRemoteFingerprintFromCert(client1_.dtls_transport(),
                                         client2_.certificate());
          } else {
            SetRemoteFingerprintFromCert(client1_.dtls_transport(),
                                         client2_.certificate(),
                                         true /*modify_digest*/);
          }
          break;
        case CALLER_WRITABLE:
          EXPECT_TRUE(client1_.Connect(&client2_, true));
          EXPECT_TRUE(WaitUntil(
              [&] { return client1_.fake_ice_transport()->writable(); }));
          break;
        case CALLER_RECEIVES_CLIENTHELLO:
          // Sanity check that a ClientHello hasn't already been received.
          EXPECT_EQ(0, client1_.received_dtls_client_hellos());
          // Making client2_ writable will cause it to send the ClientHello.
          EXPECT_TRUE(client2_.Connect(&client1_, true));
          EXPECT_TRUE(WaitUntil(
              [&] { return client2_.fake_ice_transport()->writable(); }));
          EXPECT_TRUE(WaitUntil(
              [&] { return client1_.received_dtls_client_hellos() >= 1; }));
          break;
        case HANDSHAKE_FINISHES:
          // Sanity check that the handshake hasn't already finished.
          EXPECT_FALSE(client1_.dtls_transport()->IsDtlsConnected() ||
                       client1_.dtls_transport()->dtls_state() ==
                           DtlsTransportState::kFailed);
          EXPECT_TRUE(WaitUntil([&] {
            return client1_.dtls_transport()->IsDtlsConnected() ||
                   client1_.dtls_transport()->dtls_state() ==
                       DtlsTransportState::kFailed;
          }));
          break;
      }
    }

    DtlsTransportState expected_final_state =
        valid_fingerprint ? DtlsTransportState::kConnected
                          : DtlsTransportState::kFailed;
    EXPECT_TRUE(WaitUntil([&] {
      return client1_.dtls_transport()->dtls_state() == expected_final_state;
    }));
    EXPECT_TRUE(WaitUntil([&] {
      return client2_.dtls_transport()->dtls_state() == expected_final_state ||
             // Unlike BoringSSL, OpenSSL can not send a fatal alert to the peer
             // so the peer will be stuck in kConnecting.
             (!SSLStreamAdapter::IsBoringSsl() &&
              expected_final_state == DtlsTransportState::kFailed &&
              client2_.dtls_transport()->dtls_state() ==
                  DtlsTransportState::kConnecting);
    }));

    // Transports should be writable iff there was a valid fingerprint.
    EXPECT_EQ(valid_fingerprint, client1_.dtls_transport()->writable());
    EXPECT_EQ(valid_fingerprint, client2_.dtls_transport()->writable());

    int count = pqc ? 2 : 1;
    // Check that no hello needed to be retransmitted.
    EXPECT_EQ(count, client1_.received_dtls_client_hellos());
    EXPECT_LE(count, client2_.received_dtls_server_hellos() +
                         client2_.received_dtls_ciphertext_packets());

    if (valid_fingerprint) {
      TestTransfer(1000, 100, false);
    }
  }
};

TEST_P(DtlsEventOrderingTest, TestEventOrdering) {
  TestEventOrdering(::testing::get<0>(GetParam()),
                    ::testing::get<1>(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(
    TestEventOrdering,
    DtlsEventOrderingTest,
    ::testing::Combine(
        ::testing::Values(
            std::vector<DtlsTransportInternalImplEvent>{
                CALLER_RECEIVES_FINGERPRINT, CALLER_WRITABLE,
                CALLER_RECEIVES_CLIENTHELLO, HANDSHAKE_FINISHES},
            std::vector<DtlsTransportInternalImplEvent>{
                CALLER_WRITABLE, CALLER_RECEIVES_FINGERPRINT,
                CALLER_RECEIVES_CLIENTHELLO, HANDSHAKE_FINISHES},
            std::vector<DtlsTransportInternalImplEvent>{
                CALLER_WRITABLE, CALLER_RECEIVES_CLIENTHELLO,
                CALLER_RECEIVES_FINGERPRINT, HANDSHAKE_FINISHES},
            std::vector<DtlsTransportInternalImplEvent>{
                CALLER_WRITABLE, CALLER_RECEIVES_CLIENTHELLO,
                HANDSHAKE_FINISHES, CALLER_RECEIVES_FINGERPRINT},
            std::vector<DtlsTransportInternalImplEvent>{
                CALLER_RECEIVES_FINGERPRINT, CALLER_RECEIVES_CLIENTHELLO,
                CALLER_WRITABLE, HANDSHAKE_FINISHES},
            std::vector<DtlsTransportInternalImplEvent>{
                CALLER_RECEIVES_CLIENTHELLO, CALLER_RECEIVES_FINGERPRINT,
                CALLER_WRITABLE, HANDSHAKE_FINISHES},
            std::vector<DtlsTransportInternalImplEvent>{
                CALLER_RECEIVES_CLIENTHELLO, CALLER_WRITABLE,
                CALLER_RECEIVES_FINGERPRINT, HANDSHAKE_FINISHES},
            std::vector<DtlsTransportInternalImplEvent>{
                CALLER_RECEIVES_CLIENTHELLO, CALLER_WRITABLE,
                HANDSHAKE_FINISHES, CALLER_RECEIVES_FINGERPRINT}),
        /*valid_fingerprint=*/::testing::Bool(),
        ::testing::Values(SSL_PROTOCOL_DTLS_12, SSL_PROTOCOL_DTLS_13),
        /*pqc=*/::testing::Bool()));

class DtlsTransportInternalImplDtlsInStunTest
    : public DtlsTransportInternalImplVersionTest {
 public:
  DtlsTransportInternalImplDtlsInStunTest() {}

  void CheckRetransmissions() {
    EXPECT_EQ(client1_.dtls_transport()->GetRetransmissionCount(), 0);
    if (std::get<0>(GetParam()).dtls_in_stun ==
        std::get<1>(GetParam()).dtls_in_stun) {
      EXPECT_EQ(client2_.dtls_transport()->GetRetransmissionCount(), 0);
      return;
    }
    if (std::get<0>(GetParam()).ssl_role == SSL_CLIENT) {
      EXPECT_EQ(client2_.dtls_transport()->GetRetransmissionCount(), 0);
      return;
    }

    // If peers does not have same setting for DTLS in STUN
    // current implementation does sometimes introduce a DTLS
    // retransmit on client2 (activing as SSL_CLIENT).
    //
    // TODO: bugs.webrtc.org/367395350 - It would be nice to remove these!
    EXPECT_LE(client2_.dtls_transport()->GetRetransmissionCount(), 1);
  }
};

std::vector<std::tuple<EndpointConfig, EndpointConfig>> AllEndpointVariants() {
  std::vector<std::tuple<EndpointConfig, EndpointConfig>> v;
  for (auto ice_role : {ICEROLE_CONTROLLING, ICEROLE_CONTROLLED}) {
    for (auto ssl_role : {SSL_CLIENT, SSL_SERVER}) {
      for (auto version1 : {
               SSL_PROTOCOL_DTLS_12,
               SSL_PROTOCOL_DTLS_13,
           }) {
        for (auto version2 : {
                 SSL_PROTOCOL_DTLS_12,
                 SSL_PROTOCOL_DTLS_13,
             }) {
          for (auto dtls_in_stun1 : {false, true}) {
            for (auto dtls_in_stun2 : {false, true}) {
              // The endpoint receiving the remote certificate directly (callee)
              // knows if the caller supports dtls_in_stun.
              bool caller_dtls_in_stun = (ice_role == ICEROLE_CONTROLLING)
                                             ? dtls_in_stun1
                                             : dtls_in_stun2;
              bool callee_dtls_in_stun = (ice_role == ICEROLE_CONTROLLING)
                                             ? dtls_in_stun2
                                             : dtls_in_stun1;
              if (caller_dtls_in_stun != callee_dtls_in_stun) {
                continue;
              }
              v.push_back(std::make_tuple(
                  EndpointConfig{
                      .max_protocol_version = version1,
                      .dtls_in_stun = dtls_in_stun1,
                      .ice_role = ice_role,
                      .ssl_role = ssl_role,
                  },
                  EndpointConfig{
                      .max_protocol_version = version2,
                      .dtls_in_stun = dtls_in_stun2,
                      .ice_role = ice_role == ICEROLE_CONTROLLING
                                      ? ICEROLE_CONTROLLED
                                      : ICEROLE_CONTROLLING,
                      .ssl_role =
                          ssl_role == SSL_CLIENT ? SSL_SERVER : SSL_CLIENT,
                  }));
            }
          }
        }
      }
    }
  }
  return v;
}

TEST_P(DtlsTransportInternalImplDtlsInStunTest, Handshake1) {
  Prepare(/* rtt_estimate= */ false);
  AddPacketLogging();

  RTC_LOG(LS_INFO) << "client1: " << std::get<0>(GetParam());
  RTC_LOG(LS_INFO) << "client2: " << std::get<1>(GetParam());

  ASSERT_TRUE(client1_.ConnectIceTransport(&client2_));

  for (int i = 1; i < 3; i++) {
    client1_.SendIcePing();
    ASSERT_TRUE(WaitUntil([&] {
      return client2_.fake_ice_transport()->GetCountOfReceivedStunMessages(
                 STUN_BINDING_REQUEST) == i;
    }));
    client2_.SendIcePingConf();
    ASSERT_TRUE(WaitUntil([&] {
      return client1_.fake_ice_transport()->GetCountOfReceivedStunMessages(
                 STUN_BINDING_RESPONSE) == i;
    }));
    client2_.SendIcePing();
    ASSERT_TRUE(WaitUntil([&] {
      return client1_.fake_ice_transport()->GetCountOfReceivedStunMessages(
                 STUN_BINDING_REQUEST) == i;
    }));
    client1_.SendIcePingConf();
    ASSERT_TRUE(WaitUntil([&] {
      return client2_.fake_ice_transport()->GetCountOfReceivedStunMessages(
                 STUN_BINDING_RESPONSE) == i;
    }));
    if (client1_.dtls_transport()->writable() &&
        client2_.dtls_transport()->writable()) {
      break;
    }
  }

  EXPECT_TRUE(WaitUntil([&] {
    return client1_.dtls_transport()->writable() &&
           client2_.dtls_transport()->writable();
  }));

  EXPECT_TRUE(client1_.dtls_transport()->writable());
  EXPECT_TRUE(client2_.dtls_transport()->writable());

  CheckRetransmissions();
  ClearPacketFilters();
}

TEST_P(DtlsTransportInternalImplDtlsInStunTest, Handshake2) {
  Prepare(/* rtt_estimate= */ false);
  AddPacketLogging();

  RTC_LOG(LS_INFO) << "client1: " << std::get<0>(GetParam());
  RTC_LOG(LS_INFO) << "client2: " << std::get<1>(GetParam());

  ASSERT_TRUE(client1_.ConnectIceTransport(&client2_));

  for (int i = 1; i < 3; i++) {
    client1_.SendIcePing();
    client2_.SendIcePing();
    ASSERT_TRUE(WaitUntil([&] {
      return client1_.fake_ice_transport()->GetCountOfReceivedStunMessages(
                 STUN_BINDING_REQUEST) == i;
    }));
    ASSERT_TRUE(WaitUntil([&] {
      return client2_.fake_ice_transport()->GetCountOfReceivedStunMessages(
                 STUN_BINDING_REQUEST) == i;
    }));
    client1_.SendIcePingConf();
    client2_.SendIcePingConf();

    ASSERT_TRUE(WaitUntil([&] {
      return client1_.fake_ice_transport()->GetCountOfReceivedStunMessages(
                 STUN_BINDING_RESPONSE) == i;
    }));
    ASSERT_TRUE(WaitUntil([&] {
      return client2_.fake_ice_transport()->GetCountOfReceivedStunMessages(
                 STUN_BINDING_RESPONSE) == i;
    }));
    if (client1_.dtls_transport()->writable() &&
        client2_.dtls_transport()->writable()) {
      break;
    }
  }

  EXPECT_TRUE(WaitUntil([&] {
    return client1_.dtls_transport()->writable() &&
           client2_.dtls_transport()->writable();
  }));

  EXPECT_TRUE(client1_.dtls_transport()->writable());
  EXPECT_TRUE(client2_.dtls_transport()->writable());

  CheckRetransmissions();
  ClearPacketFilters();
}

// Test scenario where DTLS is partially transferred with
// STUN and the "rest" of the handshake is transported
// by DtlsTransportInternalImpl.
TEST_P(DtlsTransportInternalImplDtlsInStunTest, PartiallyPiggybacked) {
  Prepare(/* rtt_estimate= */ false);
  AddPacketLogging();

  RTC_LOG(LS_INFO) << "client1: " << std::get<0>(GetParam());
  RTC_LOG(LS_INFO) << "client2: " << std::get<1>(GetParam());

  ASSERT_TRUE(client1_.ConnectIceTransport(&client2_));

  for (int i = 1; i < 2; i++) {
    client1_.SendIcePing();
    client2_.SendIcePing();
    ASSERT_TRUE(WaitUntil([&] {
      return client1_.fake_ice_transport()->GetCountOfReceivedStunMessages(
                 STUN_BINDING_REQUEST) == i;
    }));
    ASSERT_TRUE(WaitUntil([&] {
      return client2_.fake_ice_transport()->GetCountOfReceivedStunMessages(
                 STUN_BINDING_REQUEST) == i;
    }));
    client1_.SendIcePingConf();
    client2_.SendIcePingConf();

    ASSERT_TRUE(WaitUntil([&] {
      return client1_.fake_ice_transport()->GetCountOfReceivedStunMessages(
                 STUN_BINDING_RESPONSE) == i;
    }));
    ASSERT_TRUE(WaitUntil([&] {
      return client2_.fake_ice_transport()->GetCountOfReceivedStunMessages(
                 STUN_BINDING_RESPONSE) == i;
    }));
    if (client1_.dtls_transport()->writable() &&
        client2_.dtls_transport()->writable()) {
      break;
    }
  }

  EXPECT_FALSE(client1_.dtls_transport()->writable() &&
               client2_.dtls_transport()->writable());

  EXPECT_TRUE(WaitUntil([&] {
    return client1_.dtls_transport()->writable() &&
           client2_.dtls_transport()->writable();
  }));

  EXPECT_TRUE(client1_.dtls_transport()->writable());
  EXPECT_TRUE(client2_.dtls_transport()->writable());

  CheckRetransmissions();
  ClearPacketFilters();
}

TEST_P(DtlsTransportInternalImplDtlsInStunTest,
       DtlsDoesNotSignalWritableUnlessIceWritableOnce) {
  if (!SSLStreamAdapter::IsBoringSsl() &&
      std::get<0>(GetParam()).dtls_in_stun &&
      std::get<1>(GetParam()).dtls_in_stun) {
    GTEST_SKIP() << "DTLS-in-STUN needs boringssl.";
  }

  Prepare(/* rtt_estimate= */ false);
  AddPacketLogging();

  RTC_LOG(LS_INFO) << "client1: " << std::get<0>(GetParam());
  RTC_LOG(LS_INFO) << "client2: " << std::get<1>(GetParam());

  ASSERT_TRUE(client1_.ConnectIceTransport(&client2_));

  client1_.SendIcePing();
  ASSERT_TRUE(WaitUntil([&] {
    return client2_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_REQUEST) == 1;
  }));
  client2_.SendIcePingConf();
  ASSERT_TRUE(WaitUntil([&] {
    return client1_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_RESPONSE) == 1;
  }));
  client1_.SendIcePing();
  ASSERT_TRUE(WaitUntil([&] {
    return client2_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_REQUEST) == 2;
  }));
  client2_.SendIcePingConf();
  ASSERT_TRUE(WaitUntil([&] {
    return client1_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_RESPONSE) == 2;
  }));

  bool dtls_in_stun = std::get<0>(GetParam()).dtls_in_stun &&
                      std::get<1>(GetParam()).dtls_in_stun;
  if (dtls_in_stun) {
    ASSERT_TRUE(client1_.dtls_transport()->writable());
  }
  // Ice has never been writable on client2.
  ASSERT_FALSE(client2_.dtls_transport()->writable());

  client2_.SendIcePing();
  ASSERT_TRUE(WaitUntil([&] {
    return client1_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_REQUEST) == 1;
  }));
  client1_.SendIcePingConf();
  ASSERT_TRUE(WaitUntil([&] {
    return client2_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_RESPONSE) == 1;
  }));

  EXPECT_TRUE(WaitUntil([&] {
    return client1_.dtls_transport()->writable() &&
           client2_.dtls_transport()->writable();
  }));

  EXPECT_TRUE(client1_.dtls_transport()->writable());
  EXPECT_TRUE(client2_.dtls_transport()->writable());

  if (dtls_in_stun) {
    EXPECT_EQ(client1_.dtls_transport()->GetRetransmissionCount(), 0);
    EXPECT_EQ(client2_.dtls_transport()->GetRetransmissionCount(), 0);
  }

  ClearPacketFilters();
}

INSTANTIATE_TEST_SUITE_P(DtlsTransportInternalImplDtlsInStunTest,
                         DtlsTransportInternalImplDtlsInStunTest,
                         testing::ValuesIn(AllEndpointVariants()));

class DtlsInStunTest : public DtlsTransportInternalImplDtlsInStunTest {};

std::vector<std::tuple<EndpointConfig, EndpointConfig>> Dtls13WithDtlsInStun() {
  return {
      std::make_tuple(
          EndpointConfig{
              .max_protocol_version = SSL_PROTOCOL_DTLS_13,
              .dtls_in_stun = true,
              .ice_role = ICEROLE_CONTROLLING,
              .ssl_role = SSL_CLIENT,
              .pqc = false,
          },
          EndpointConfig{
              .max_protocol_version = SSL_PROTOCOL_DTLS_13,
              .dtls_in_stun = true,
              .ice_role = ICEROLE_CONTROLLED,
              .ssl_role = SSL_SERVER,
              .pqc = false,
          }),
      std::make_tuple(
          EndpointConfig{
              .max_protocol_version = SSL_PROTOCOL_DTLS_13,
              .dtls_in_stun = true,
              .ice_role = ICEROLE_CONTROLLING,
              .ssl_role = SSL_CLIENT,
              .pqc = true,
          },
          EndpointConfig{
              .max_protocol_version = SSL_PROTOCOL_DTLS_13,
              .dtls_in_stun = true,
              .ice_role = ICEROLE_CONTROLLED,
              .ssl_role = SSL_SERVER,
              .pqc = false,
          }),
      std::make_tuple(
          EndpointConfig{
              .max_protocol_version = SSL_PROTOCOL_DTLS_13,
              .dtls_in_stun = true,
              .ice_role = ICEROLE_CONTROLLING,
              .ssl_role = SSL_CLIENT,
              .pqc = false,
          },
          EndpointConfig{
              .max_protocol_version = SSL_PROTOCOL_DTLS_13,
              .dtls_in_stun = true,
              .ice_role = ICEROLE_CONTROLLED,
              .ssl_role = SSL_SERVER,
              .pqc = true,
          }),
      std::make_tuple(
          EndpointConfig{
              .max_protocol_version = SSL_PROTOCOL_DTLS_13,
              .dtls_in_stun = true,
              .ice_role = ICEROLE_CONTROLLING,
              .ssl_role = SSL_CLIENT,
              .pqc = true,
          },
          EndpointConfig{
              .max_protocol_version = SSL_PROTOCOL_DTLS_13,
              .dtls_in_stun = true,
              .ice_role = ICEROLE_CONTROLLED,
              .ssl_role = SSL_SERVER,
              .pqc = true,
          }),
  };
}

INSTANTIATE_TEST_SUITE_P(DtlsInStunTest,
                         DtlsInStunTest,
                         testing::ValuesIn(Dtls13WithDtlsInStun()));

TEST_P(DtlsInStunTest, OptimalDtls13Handshake) {
  if (!SSLStreamAdapter::IsBoringSsl()) {
    GTEST_SKIP() << "Needs boringssl.";
  }

  RTC_LOG(LS_INFO) << "client1: " << std::get<0>(GetParam());
  RTC_LOG(LS_INFO) << "client2: " << std::get<1>(GetParam());

  int client1_first_flight_packets =
      std::get<0>(GetParam()).GetFirstFlightPackets();
  int client2_first_flight_packets =
      std::get<1>(GetParam()).GetFirstFlightPackets();

  Prepare(/* rtt_estimate= */ true);
  AddPacketLogging();

  ASSERT_TRUE(client1_.ConnectIceTransport(&client2_));

  client1_.SendIcePing(client1_first_flight_packets);
  client2_.SendIcePing(client2_first_flight_packets);

  ASSERT_TRUE(WaitUntil([&] {
    return client1_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_REQUEST) == client2_first_flight_packets;
  }));
  ASSERT_TRUE(WaitUntil([&] {
    return client2_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_REQUEST) == client1_first_flight_packets;
  }));

  client2_.SendIcePingConf(client1_first_flight_packets);
  client1_.SendIcePingConf(client2_first_flight_packets);

  ASSERT_TRUE(WaitUntil([&] {
    return client1_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_RESPONSE) == client1_first_flight_packets;
  }));
  EXPECT_TRUE(client1_.dtls_transport()->writable());
  ASSERT_TRUE(WaitUntil([&] {
    return client2_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_RESPONSE) == client2_first_flight_packets;
  }));
  EXPECT_FALSE(client2_.dtls_transport()->writable());

  // Here client1 sends one more packet, which should make client2 (server) also
  // writable. Wait for that to arrive
  int expected_packets =
      1 + client2_.fake_ice_transport()->GetCountOfReceivedPackets();

  EXPECT_TRUE(WaitUntil([&] {
    return client2_.fake_ice_transport()->GetCountOfReceivedPackets() ==
           expected_packets;
  }));
  EXPECT_TRUE(client2_.dtls_transport()->writable());

  CheckRetransmissions();
  ClearPacketFilters();
}

EndpointConfig Vanilla12(bool client1) {
  return {
      .max_protocol_version = SSL_PROTOCOL_DTLS_12,
      .dtls_in_stun = false,
      .ice_role = client1 ? ICEROLE_CONTROLLING : ICEROLE_CONTROLLED,
      .ssl_role = client1 ? SSL_CLIENT : SSL_SERVER,
      .pqc = false,
  };
}

EndpointConfig Vanilla13(bool client1) {
  return {
      .max_protocol_version = SSL_PROTOCOL_DTLS_13,
      .dtls_in_stun = false,
      .ice_role = client1 ? ICEROLE_CONTROLLING : ICEROLE_CONTROLLED,
      .ssl_role = client1 ? SSL_CLIENT : SSL_SERVER,
      .pqc = false,
  };
}

struct DtlsTransportLastHandshakePacketTest
    : public DtlsTransportInternalImplVersionTest {
  int client1_packet_num = 0;
  int client2_packet_num = 0;

  int client1_packet_to_block = -1;
  int client1_packet_size = 0;
  std::unique_ptr<char[]> client1_packet = nullptr;

  int client2_packet_to_block = -1;
  int client2_packet_size = 0;
  std::unique_ptr<char[]> client2_packet = nullptr;

  void InstallBlock() {
    AddPacketLogging();
    client1_.fake_ice_transport()->set_packet_send_filter(
        [&](auto data, auto len, auto options, auto flags) {
          // Always allow STUN.
          bool stun = data[0] == 0 || data[0] == 1;

          if (stun) {
            return false;
          }
          if (client1_packet_num++ != client1_packet_to_block) {
            return LogBlock(client1_.name(), /* block=*/false, data, len);
          }
          client1_packet_size = len;
          client1_packet = std::unique_ptr<char[]>(new char[len]);
          memcpy(client1_packet.get(), data, len);
          return LogBlock(client1_.name(), /* block=*/true, data, len);
        });

    client2_.fake_ice_transport()->set_packet_send_filter(
        [&](auto data, auto len, auto options, auto flags) {
          // Always allow STUN.
          bool stun = data[0] == 0 || data[0] == 1;

          if (stun) {
            return false;
          }
          if (client2_packet_num++ != client2_packet_to_block) {
            return LogBlock(client2_.name(), /* block=*/false, data, len);
            return false;
          }
          client2_packet_size = len;
          client2_packet = std::unique_ptr<char[]>(new char[len]);
          memcpy(client2_packet.get(), data, len);
          return LogBlock(client2_.name(), /* block=*/true, data, len);
        });
  }

  void resend_blocked() {
    int flags = 0;
    AsyncSocketPacketOptions packet_options;
    if (client1_packet != nullptr) {
      int size = client1_packet_size;
      RTC_LOG(LS_INFO) << "client1 => client2: RESEND "
                       << client1_packet.get()[0] << " len= " << size;
      int rv = client1_.fake_ice_transport()->SendPacket(
          client1_packet.get(), size, packet_options, flags);
      ASSERT_EQ(size, rv);
      client1_packet = nullptr;
    }
    if (client2_packet != nullptr) {
      int size = client2_packet_size;
      RTC_LOG(LS_INFO) << "client2 => client1: RESEND "
                       << client2_packet.get()[0] << " len= " << size;
      int rv = client2_.fake_ice_transport()->SendPacket(
          client2_packet.get(), size, packet_options, flags);
      ASSERT_EQ(size, rv);
      client2_packet = nullptr;
    }
  }
};

TEST_P(DtlsTransportLastHandshakePacketTest,
       VerifyThatDtls12ClientCanNotDecodeBeforeBecomingWritable) {
  if (!SSLStreamAdapter::IsBoringSsl()) {
    GTEST_SKIP() << "Needs boringssl.";
  }

  if (std::get<0>(GetParam()).max_protocol_version != SSL_PROTOCOL_DTLS_12) {
    GTEST_SKIP() << "This is a DTLS 1.2 test.";
  }

  Prepare(false);
  EXPECT_TRUE(client1_.ConnectIceTransport(&client2_));

  // Block the last handshake message server to client.
  client2_packet_to_block = 1;
  InstallBlock();

  client1_.SendIcePing();
  client2_.SendIcePing();
  ASSERT_TRUE(WaitUntil([&] {
    return client1_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_REQUEST) == 1 &&
           client2_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_REQUEST) == 1;
  }));
  client1_.SendIcePingConf();
  client2_.SendIcePingConf();
  ASSERT_TRUE(WaitUntil([&] {
    return client1_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_RESPONSE) == 1 &&
           client2_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_RESPONSE) == 1;
  }));

  ASSERT_TRUE(WaitUntil([&] { return client2_packet != nullptr; }));

  // Now everything except the last message from server to client is exchanged.
  ASSERT_FALSE(client1_.dtls_transport()->writable());
  ASSERT_TRUE(client2_.dtls_transport()->writable());

  // Verify that messages sent from client2 can NOT be decoded at client1.
  int size = 100;
  int count = 1;
  client1_recv_packets = 0;
  client1_.ExpectPackets(size);
  client2_.SendPackets(size, count, /* srtp= */ false);
  ASSERT_TRUE(WaitUntil([&] { return client1_recv_packets == 1; }));

  // Last received packet was not decodable!
  ASSERT_EQ(client1_.NumPacketsReceived(), 0u);

  // Resend the blocked packet.
  client1_recv_packets = 0;
  resend_blocked();
  ASSERT_TRUE(WaitUntil([&] { return client1_recv_packets == 1; }));
  ASSERT_TRUE(client1_.dtls_transport()->writable());
  ClearPacketFilters();
}

INSTANTIATE_TEST_SUITE_P(DtlsTransportLastHandshakePacket12Test,
                         DtlsTransportLastHandshakePacketTest,
                         ::testing::Values(std::make_tuple(Vanilla12(true),
                                                           Vanilla12(false))));

TEST_P(DtlsTransportLastHandshakePacketTest,
       VerifyThatDtls13ServerCanNotDecodeBeforeBecomingWritable) {
  if (!SSLStreamAdapter::IsBoringSsl()) {
    GTEST_SKIP() << "Needs boringssl.";
  }

  if (std::get<0>(GetParam()).max_protocol_version != SSL_PROTOCOL_DTLS_13) {
    GTEST_SKIP() << "This is a DTLS 1.3 test.";
  }

  Prepare(false);
  EXPECT_TRUE(client1_.ConnectIceTransport(&client2_));

  // Block the ACK from client to server.
  client1_packet_to_block = 1;
  InstallBlock();

  client1_.SendIcePing();
  client2_.SendIcePing();
  ASSERT_TRUE(WaitUntil([&] {
    return client1_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_REQUEST) == 1 &&
           client2_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_REQUEST) == 1;
  }));
  client1_.SendIcePingConf();
  client2_.SendIcePingConf();
  ASSERT_TRUE(WaitUntil([&] {
    return client1_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_RESPONSE) == 1 &&
           client2_.fake_ice_transport()->GetCountOfReceivedStunMessages(
               STUN_BINDING_RESPONSE) == 1;
  }));

  ASSERT_TRUE(WaitUntil([&] { return client1_packet != nullptr; }));

  // Now everything except the last message from client to server is exchanged.
  ASSERT_TRUE(client1_.dtls_transport()->writable());
  ASSERT_FALSE(client2_.dtls_transport()->writable());

  // Verify that messages sent from client1 can NOT be decoded at server.
  int size = 100;
  int count = 1;
  client2_recv_packets = 0;
  client2_.ExpectPackets(size);
  client1_.SendPackets(size, count, /* srtp= */ false);
  ASSERT_TRUE(WaitUntil([&] { return client2_recv_packets == 1; }));

  // Last received packet was NOT decodable!
  ASSERT_EQ(client2_.NumPacketsReceived(), 0u);

  // Resend the blocked packet.
  client2_recv_packets = 0;
  resend_blocked();
  ASSERT_TRUE(WaitUntil([&] { return client2_recv_packets == 1; }));
  ASSERT_TRUE(client2_.dtls_transport()->writable());

  client2_recv_packets = 0;
  client1_.SendPackets(size, count, /* srtp= */ false);
  ASSERT_TRUE(WaitUntil([&] { return client2_recv_packets == 1; }));
  ASSERT_EQ(client2_.NumPacketsReceived(), 1u);
  ClearPacketFilters();
}

INSTANTIATE_TEST_SUITE_P(DtlsTransportLastHandshakePacket13Test,
                         DtlsTransportLastHandshakePacketTest,
                         ::testing::Values(std::make_tuple(Vanilla13(true),
                                                           Vanilla13(false))));

}  // namespace
}  // namespace webrtc
