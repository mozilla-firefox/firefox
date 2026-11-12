/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/openssl_stream_adapter.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/stack.h>
#include <openssl/tls1.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "api/environment/environment.h"
#include "api/field_trials_view.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "rtc_base/buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "rtc_base/openssl_digest.h"
#include "rtc_base/openssl_utility.h"
#include "rtc_base/span_helpers.h"
#include "rtc_base/ssl_certificate.h"
#include "rtc_base/ssl_identity.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/stream.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/task_utils/repeating_task.h"
#include "system_wrappers/include/clock.h"

#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/digest.h>
#include <openssl/dtls1.h>
#include <openssl/pool.h>
#include <openssl/ssl.h>

#include "rtc_base/boringssl_certificate.h"
#include "rtc_base/boringssl_identity.h"
#include "rtc_base/openssl.h"
#else
#include "rtc_base/openssl_identity.h"
#endif

#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
#error "webrtc requires at least OpenSSL version 1.1.0, to support DTLS-SRTP"
#endif

namespace {
// Value specified in RFC 5764.
constexpr absl::string_view kDtlsSrtpExporterLabel = "EXTRACTOR-dtls_srtp";
}  // namespace

namespace webrtc {
namespace {
// SRTP cipher suite table. `internal_name` is used to construct a
// colon-separated profile strings which is needed by
// SSL_CTX_set_tlsext_use_srtp().
struct SrtpCipherMapEntry {
  const char* internal_name;
  const int id;
};

// This isn't elegant, but it's better than an external reference
// Note: these are not the IANA names but the OpenSSL/BoringSSL ones.
constexpr SrtpCipherMapEntry kSrtpCipherMap[] = {
    {.internal_name = "SRTP_AES128_CM_SHA1_80", .id = kSrtpAes128CmSha1_80},
    {.internal_name = "SRTP_AES128_CM_SHA1_32", .id = kSrtpAes128CmSha1_32},
    {.internal_name = "SRTP_AEAD_AES_128_GCM", .id = kSrtpAeadAes128Gcm},
    {.internal_name = "SRTP_AEAD_AES_256_GCM", .id = kSrtpAeadAes256Gcm}};

#ifdef OPENSSL_IS_BORINGSSL
// Enabled by EnableTimeCallbackForTesting. Should never be set in production
// code.
constinit bool g_use_time_callback_for_testing = false;
constinit size_t g_num_clock_for_testing_users = 0;
constinit Clock* g_clock_for_testing = nullptr;
Mutex& GlobalClockMutex() {
  static absl::NoDestructor<Mutex> m;
  return *m;
}
#endif

#ifdef DTLS1_3_VERSION
#define MAX_SSL_PROTOCOL_DTLS SSL_PROTOCOL_DTLS_13
#else
#define MAX_SSL_PROTOCOL_DTLS SSL_PROTOCOL_DTLS_12
#endif

uint16_t GetMaxVersion(SSLMode ssl_mode, SSLProtocolVersion version) {
  switch (ssl_mode) {
    case SSL_MODE_TLS:
      switch (version) {
        default:
        case SSL_PROTOCOL_NOT_GIVEN:
        case SSL_PROTOCOL_TLS_10:
        case SSL_PROTOCOL_TLS_11:
        case SSL_PROTOCOL_TLS_12:
          return TLS1_2_VERSION;
        case SSL_PROTOCOL_TLS_13:
#ifdef TLS1_3_VERSION
          return TLS1_3_VERSION;
#else
          return TLS1_2_VERSION;
#endif
      }
    case SSL_MODE_DTLS:
      switch (version) {
        default:
        case SSL_PROTOCOL_DTLS_10:
        case SSL_PROTOCOL_DTLS_12:
          return DTLS1_2_VERSION;
        case SSL_PROTOCOL_NOT_GIVEN:
        case SSL_PROTOCOL_DTLS_13:
#ifdef DTLS1_3_VERSION
          return DTLS1_3_VERSION;
#else
          return DTLS1_2_VERSION;
#endif
      }
  }
}

#ifdef OPENSSL_IS_BORINGSSL
std::string GetOpenSslError() {
  std::string error;
  int64_t error_id;
  const char* error_file;
  int error_line;
  while ((error_id = ERR_get_error_line(&error_file, &error_line)) != 0) {
    char buf[ERR_ERROR_STRING_BUF_LEN];
    ERR_error_string_n(error_id, buf, sizeof(buf));
    absl::StrAppendFormat(&error, "\nOpenSSL error (file: %s, line: %d): %s",
                          error_file, error_line, buf);
  }
  return error;
}
#endif

//////////////////////////////////////////////////////////////////////
// StreamBIO
//////////////////////////////////////////////////////////////////////

int stream_write(BIO* h, const char* buf, int num);
int stream_read(BIO* h, char* buf, int size);
int stream_puts(BIO* h, const char* str);
long stream_ctrl(BIO* h, int cmd, long arg1, void* arg2);
int stream_new(BIO* h);
int stream_free(BIO* data);

BIO_METHOD* BIO_stream_method() {
  static BIO_METHOD* method = [] {
    BIO_METHOD* method = BIO_meth_new(BIO_TYPE_BIO, "stream");
    BIO_meth_set_write(method, stream_write);
    BIO_meth_set_read(method, stream_read);
    BIO_meth_set_puts(method, stream_puts);
    BIO_meth_set_ctrl(method, stream_ctrl);
    BIO_meth_set_create(method, stream_new);
    BIO_meth_set_destroy(method, stream_free);
    return method;
  }();
  return method;
}

BIO* BIO_new_stream(StreamInterface* stream) {
  BIO* ret = BIO_new(BIO_stream_method());
  if (ret == nullptr) {
    return nullptr;
  }
  BIO_set_data(ret, stream);
  return ret;
}

// bio methods return 1 (or at least non-zero) on success and 0 on failure.

int stream_new(BIO* b) {
  BIO_set_shutdown(b, 0);
  BIO_set_init(b, 1);
  BIO_set_data(b, nullptr);
  return 1;
}

int stream_free(BIO* b) {
  if (b == nullptr) {
    return 0;
  }
  return 1;
}

int stream_read(BIO* b, char* out, int outl) {
  if (!out) {
    return -1;
  }
  StreamInterface* stream = static_cast<StreamInterface*>(BIO_get_data(b));
  BIO_clear_retry_flags(b);
  size_t read;
  int error;
  StreamResult result =
      stream->Read(AsWritableUint8Span(std::span(out, outl)), read, error);
  if (result == SR_SUCCESS) {
    return checked_cast<int>(read);
  } else if (result == SR_BLOCK) {
    BIO_set_retry_read(b);
  }
  return -1;
}

int stream_write(BIO* b, const char* in, int inl) {
  if (!in) {
    return -1;
  }
  StreamInterface* stream = static_cast<StreamInterface*>(BIO_get_data(b));
  BIO_clear_retry_flags(b);
  size_t written;
  int error;
  StreamResult result =
      stream->Write(AsUint8Span(std::span(in, inl)), written, error);
  if (result == SR_SUCCESS) {
    return checked_cast<int>(written);
  } else if (result == SR_BLOCK) {
    BIO_set_retry_write(b);
  }
  return -1;
}

int stream_puts(BIO* b, const char* str) {
  return stream_write(b, str, checked_cast<int>(strlen(str)));
}

long stream_ctrl(BIO* b, int cmd, long num, void* ptr) {
  switch (cmd) {
    case BIO_CTRL_RESET:
      return 0;
    case BIO_CTRL_EOF: {
      StreamInterface* stream = static_cast<StreamInterface*>(ptr);
      // 1 means end-of-stream.
      return (stream->GetState() == SS_CLOSED) ? 1 : 0;
    }
    case BIO_CTRL_WPENDING:
    case BIO_CTRL_PENDING:
      return 0;
    case BIO_CTRL_FLUSH: {
      // Flush signals the end of a flight of handshake packets.
      StreamInterface* stream = static_cast<StreamInterface*>(BIO_get_data(b));
      RTC_DCHECK(stream);
      if (stream->Flush()) {
        RTC_LOG(LS_WARNING) << "Failed to flush stream";
        return 0;
      }
      return 1;
    }
    case BIO_CTRL_DGRAM_QUERY_MTU:
      // openssl defaults to mtu=256 unless we return something here.
      // The handshake doesn't actually need to send packets above 1k,
      // so this seems like a sensible value that should work in most cases.
      // Webrtc uses the same value for video packets.
      RTC_DCHECK_NOTREACHED()
          << "We should be using SSL_set_mtu instead of this!";
      return 1200;
    default:
      return 0;
  }
}

}  // namespace

/////////////////////////////////////////////////////////////////////////////
// OpenSSLStreamAdapter
/////////////////////////////////////////////////////////////////////////////

OpenSSLStreamAdapter::OpenSSLStreamAdapter(
    std::optional<Environment> env,
    std::unique_ptr<StreamInterface> stream,
    absl::AnyInvocable<void(SSLHandshakeError)> handshake_error)
    : env_(std::move(env)),
      stream_(std::move(stream)),
      handshake_error_(std::move(handshake_error)),
      owner_(TaskQueueBase::Current()),
      state_(SSL_NONE),
      role_(SSL_CLIENT),
      ssl_read_needs_write_(false),
      ssl_write_needs_read_(false),
      ssl_(nullptr),
      ssl_ctx_(nullptr),
      ssl_mode_(SSL_MODE_DTLS),
      ssl_max_version_(MAX_SSL_PROTOCOL_DTLS),
      disable_ssl_group_ids_(
          env_.has_value() &&
          env_->field_trials().IsEnabled("WebRTC-DisableSslGroupIds")) {
  stream_->SetEventCallback(
      [this](int events, int err) { OnEvent(events, err); });
}

OpenSSLStreamAdapter::~OpenSSLStreamAdapter() {
  timeout_task_.Stop();
  Cleanup(0);
}

void OpenSSLStreamAdapter::SetIdentity(std::unique_ptr<SSLIdentity> identity) {
  RTC_DCHECK(!identity_);
#ifdef OPENSSL_IS_BORINGSSL
  identity_.reset(static_cast<BoringSSLIdentity*>(identity.release()));
#else
  identity_.reset(static_cast<OpenSSLIdentity*>(identity.release()));
#endif
}

SSLIdentity* OpenSSLStreamAdapter::GetIdentityForTesting() const {
  return identity_.get();
}

void OpenSSLStreamAdapter::SetServerRole(SSLRole role) {
  role_ = role;
}

SSLPeerCertificateDigestError OpenSSLStreamAdapter::SetPeerCertificateDigest(
    absl::string_view digest_alg,
    std::span<const uint8_t> digest_val) {
  RTC_DCHECK(!peer_certificate_verified_);
  RTC_DCHECK(!HasPeerCertificateDigest());
  size_t expected_len;

  if (!OpenSSLDigest::GetDigestSize(digest_alg, &expected_len)) {
    RTC_LOG(LS_WARNING) << "Unknown digest algorithm: " << digest_alg;
    return SSLPeerCertificateDigestError::UNKNOWN_ALGORITHM;
  }
  if (expected_len != digest_val.size()) {
    return SSLPeerCertificateDigestError::INVALID_LENGTH;
  }

  peer_certificate_digest_value_.SetData(digest_val);
  peer_certificate_digest_algorithm_ = std::string(digest_alg);

  if (!peer_cert_chain_) {
    // Normal case, where the digest is set before we obtain the certificate
    // from the handshake.
    return SSLPeerCertificateDigestError::NONE;
  }

  if (!VerifyPeerCertificate()) {
    Error("SetPeerCertificateDigest", -1, SSL_AD_BAD_CERTIFICATE, false);
    return SSLPeerCertificateDigestError::VERIFICATION_FAILED;
  }

  if (state_ == SSL_CONNECTED) {
    // Post the event asynchronously to unwind the stack. The caller
    // of ContinueSSL may be the same object listening for these
    // events and may not be prepared for reentrancy.
    PostEvent(SE_OPEN | SE_READ | SE_WRITE, 0);
  }
  return SSLPeerCertificateDigestError::NONE;
}

std::optional<absl::string_view> OpenSSLStreamAdapter::GetTlsCipherSuiteName()
    const {
  if (state_ != SSL_CONNECTED) {
    return std::nullopt;
  }

  const SSL_CIPHER* current_cipher = SSL_get_current_cipher(ssl_);
  return SSL_CIPHER_standard_name(current_cipher);
}

bool OpenSSLStreamAdapter::GetSslCipherSuite(int* cipher_suite) const {
  if (state_ != SSL_CONNECTED) {
    return false;
  }

  const SSL_CIPHER* current_cipher = SSL_get_current_cipher(ssl_);
  if (current_cipher == nullptr) {
    return false;
  }

  *cipher_suite = static_cast<uint16_t>(SSL_CIPHER_get_id(current_cipher));
  return true;
}

SSLProtocolVersion OpenSSLStreamAdapter::GetSslVersion() const {
  if (state_ != SSL_CONNECTED) {
    return SSL_PROTOCOL_NOT_GIVEN;
  }

  int ssl_version = SSL_version(ssl_);
  if (ssl_mode_ == SSL_MODE_DTLS) {
    if (ssl_version == DTLS1_VERSION) {
      return SSL_PROTOCOL_DTLS_10;
    } else if (ssl_version == DTLS1_2_VERSION) {
      return SSL_PROTOCOL_DTLS_12;
    }
#ifdef DTLS1_3_VERSION
    if (ssl_version == DTLS1_3_VERSION) {
      return SSL_PROTOCOL_DTLS_13;
    }
#endif
  } else {
    if (ssl_version == TLS1_VERSION) {
      return SSL_PROTOCOL_TLS_10;
    } else if (ssl_version == TLS1_1_VERSION) {
      return SSL_PROTOCOL_TLS_11;
    } else if (ssl_version == TLS1_2_VERSION) {
      return SSL_PROTOCOL_TLS_12;
    }
#ifdef TLS1_3_VERSION
    if (ssl_version == TLS1_3_VERSION) {
      return SSL_PROTOCOL_TLS_13;
    }
#endif
  }

  return SSL_PROTOCOL_NOT_GIVEN;
}

bool OpenSSLStreamAdapter::GetSslVersionBytes(int* version) const {
  if (state_ != SSL_CONNECTED) {
    return false;
  }
  *version = SSL_version(ssl_);
  return true;
}

uint16_t OpenSSLStreamAdapter::GetSslGroupId() const {
  if (state_ != SSL_CONNECTED) {
    return 0;
  }
#ifdef OPENSSL_IS_BORINGSSL
  return SSL_get_group_id(ssl_);
#else
  return 0;
#endif
}

bool OpenSSLStreamAdapter::ExportSrtpKeyingMaterial(
    ZeroOnFreeBuffer<uint8_t>& keying_material) {
  // Arguments are:
  // keying material/len -- a buffer to hold the keying material.
  // label               -- the exporter label.
  //                        part of the RFC defining each exporter
  //                        usage. We only use RFC 5764 for DTLS-SRTP.
  // context/context_len -- a context to bind to for this connection;
  // use_context            optional, can be null, 0 (IN). Not used by WebRTC.
  if (SSL_export_keying_material(
          ssl_, keying_material.data(), keying_material.size(),
          kDtlsSrtpExporterLabel.data(), kDtlsSrtpExporterLabel.size(), nullptr,
          0, false) != 1) {
    return false;
  }
  return true;
}

bool OpenSSLStreamAdapter::AppendSrtpKeyingMaterial(
    ZeroOnFreeBuffer<uint8_t>& key_buffer) {
  // Compute size of keying material.
  int selected_crypto_suite;
  if (!GetDtlsSrtpCryptoSuite(&selected_crypto_suite)) {
    RTC_LOG(LS_ERROR) << "No crypto suite";
    return false;
  }
  int key_len, salt_len;
  if (!GetSrtpKeyAndSaltLengths(selected_crypto_suite, &key_len, &salt_len)) {
    RTC_LOG(LS_ERROR) << "Unable to get key and salt len from crypto suite";
    return false;
  }
  int key_material_size = key_len * 2 + salt_len * 2;
  // Arguments are:
  // keying material/len -- a buffer to hold the keying material.
  // label               -- the exporter label.
  //                        part of the RFC defining each exporter
  //                        usage. We only use RFC 5764 for DTLS-SRTP.
  // context/context_len -- a context to bind to for this connection;
  // use_context            optional, can be null, 0 (IN). Not used by WebRTC.
  bool success = false;
  key_buffer.AppendData(
      key_material_size, [&](std::span<uint8_t> keying_material) -> size_t {
        int result = SSL_export_keying_material(
            ssl_, keying_material.data(), keying_material.size(),
            kDtlsSrtpExporterLabel.data(), kDtlsSrtpExporterLabel.size(),
            nullptr, 0, false);
        if (result != 0) {
          success = true;
          return keying_material.size();
        } else {
          return 0;  // Consider no data appended
        }
      });
  return success;
}

uint16_t OpenSSLStreamAdapter::GetPeerSignatureAlgorithm() const {
  if (state_ != SSL_CONNECTED) {
    return 0;
  }
#ifdef OPENSSL_IS_BORINGSSL
  return SSL_get_peer_signature_algorithm(ssl_);
#else
  return kSslSignatureAlgorithmUnknown;
#endif
}

bool OpenSSLStreamAdapter::SetDtlsSrtpCryptoSuites(
    const std::vector<int>& ciphers) {
  if (state_ != SSL_NONE) {
    return false;
  }

  std::string internal_ciphers;
  for (const int cipher : ciphers) {
    bool found = false;
    for (const auto& entry : kSrtpCipherMap) {
      if (cipher == entry.id) {
        found = true;
        if (!internal_ciphers.empty()) {
          internal_ciphers += ":";
        }
        internal_ciphers += entry.internal_name;
        break;
      }
    }

    if (!found) {
      RTC_LOG(LS_ERROR) << "Could not find cipher: " << cipher;
      return false;
    }
  }

  if (internal_ciphers.empty()) {
    return false;
  }

  srtp_ciphers_ = internal_ciphers;
  return true;
}

bool OpenSSLStreamAdapter::GetDtlsSrtpCryptoSuite(int* crypto_suite) const {
  RTC_DCHECK(state_ == SSL_CONNECTED);
  if (state_ != SSL_CONNECTED) {
    return false;
  }

  const SRTP_PROTECTION_PROFILE* srtp_profile =
      SSL_get_selected_srtp_profile(ssl_);

  if (!srtp_profile) {
    return false;
  }

  *crypto_suite = srtp_profile->id;
  RTC_DCHECK(!SrtpCryptoSuiteToName(*crypto_suite).empty());
  return true;
}

bool OpenSSLStreamAdapter::IsTlsConnected() {
  return state_ == SSL_CONNECTED;
}

int OpenSSLStreamAdapter::StartSSL() {
  // Don't allow StartSSL to be called twice.
  if (state_ != SSL_NONE) {
    return -1;
  }

  if (stream_->GetState() != SS_OPEN) {
    state_ = SSL_WAIT;
    return 0;
  }

  state_ = SSL_CONNECTING;
  if (int err = BeginSSL()) {
    Error("BeginSSL", err, 0, false);
    return err;
  }

  return 0;
}

void OpenSSLStreamAdapter::SetMode(SSLMode mode) {
  RTC_DCHECK(state_ == SSL_NONE);
  ssl_mode_ = mode;
}

void OpenSSLStreamAdapter::SetMaxProtocolVersion(SSLProtocolVersion version) {
  RTC_DCHECK(ssl_ctx_ == nullptr);
  ssl_max_version_ = version;
}

void OpenSSLStreamAdapter::SetInitialRetransmissionTimeout(int timeout_ms) {
  dtls_handshake_timeout_ms_ = timeout_ms;
#ifdef OPENSSL_IS_BORINGSSL
  if (ssl_ctx_ != nullptr && ssl_mode_ == SSL_MODE_DTLS) {
    DTLSv1_set_initial_timeout_duration(ssl_, dtls_handshake_timeout_ms_);
  }
#endif
}

void OpenSSLStreamAdapter::UpdateRetransmissionTimeout(int timeout_ms) {
  dtls_handshake_timeout_ms_ = timeout_ms;
#ifdef OPENSSL_IS_BORINGSSL
  if (ssl_ctx_ != nullptr && ssl_mode_ == SSL_MODE_DTLS) {
    DTLSv1_set_initial_timeout_duration(ssl_, dtls_handshake_timeout_ms_);
    // Clear the DTLS timer
    timeout_task_.Stop();
    MaybeSetTimeout();
  }
#endif
}

void OpenSSLStreamAdapter::SetMTU(int mtu) {
  dtls_mtu_ = mtu;
  if (ssl_) {
    RTC_CHECK(SSL_set_mtu(ssl_, dtls_mtu_)) << "Call to SSL_set_mtu failed.";
  }
}

//
// StreamInterface Implementation
//
StreamResult OpenSSLStreamAdapter::Write(std::span<const uint8_t> data,
                                         size_t& written,
                                         int& error) {
  RTC_DLOG(LS_VERBOSE) << "OpenSSLStreamAdapter::Write(" << data.size() << ")";

  switch (state_) {
    case SSL_NONE:
      // pass-through in clear text
      return stream_->Write(data, written, error);
    case SSL_WAIT:
    case SSL_CONNECTING:
      return SR_BLOCK;
    case SSL_CONNECTED:
      if (WaitingToVerifyPeerCertificate()) {
        return SR_BLOCK;
      }
      break;
    case SSL_ERROR:
    case SSL_CLOSED:
    default:
      error = ssl_error_code_;
      return SR_ERROR;
  }

  // OpenSSL will return an error if we try to write zero bytes
  if (data.empty()) {
    written = 0;
    return SR_SUCCESS;
  }

  ssl_write_needs_read_ = false;

  int code = SSL_write(ssl_, data.data(), checked_cast<int>(data.size()));
  int ssl_error = SSL_get_error(ssl_, code);
  MaybeSetTimeout();
  switch (ssl_error) {
    case SSL_ERROR_NONE:
      RTC_DLOG(LS_VERBOSE) << " -- success";
      RTC_DCHECK_GT(code, 0);
      RTC_DCHECK_LE(code, data.size());
      written = code;
      return SR_SUCCESS;
    case SSL_ERROR_WANT_READ:
      RTC_DLOG(LS_VERBOSE) << " -- error want read";
      ssl_write_needs_read_ = true;
      return SR_BLOCK;
    case SSL_ERROR_WANT_WRITE:
      RTC_DLOG(LS_VERBOSE) << " -- error want write";
      return SR_BLOCK;
    case SSL_ERROR_ZERO_RETURN:
    default:
      Error("SSL_write", (ssl_error ? ssl_error : -1), 0, false);
      error = ssl_error_code_;
      return SR_ERROR;
  }
  // not reached
}

StreamResult OpenSSLStreamAdapter::Read(std::span<uint8_t> data,
                                        size_t& read,
                                        int& error) {
  RTC_DLOG(LS_VERBOSE) << "OpenSSLStreamAdapter::Read(" << data.size() << ")";
  switch (state_) {
    case SSL_NONE:
      // pass-through in clear text
      return stream_->Read(data, read, error);
    case SSL_WAIT:
    case SSL_CONNECTING:
      return SR_BLOCK;
    case SSL_CONNECTED:
      if (WaitingToVerifyPeerCertificate()) {
        return SR_BLOCK;
      }
      break;
    case SSL_CLOSED:
      return SR_EOS;
    case SSL_ERROR:
    default:
      error = ssl_error_code_;
      return SR_ERROR;
  }

  // Don't trust OpenSSL with zero byte reads
  if (data.empty()) {
    read = 0;
    return SR_SUCCESS;
  }

  ssl_read_needs_write_ = false;

  const int code = SSL_read(ssl_, data.data(), checked_cast<int>(data.size()));
  const int ssl_error = SSL_get_error(ssl_, code);
  MaybeSetTimeout();

  switch (ssl_error) {
    case SSL_ERROR_NONE:
      RTC_DLOG(LS_VERBOSE) << " -- success";
      RTC_DCHECK_GT(code, 0);
      RTC_DCHECK_LE(code, data.size());
      read = code;

      if (ssl_mode_ == SSL_MODE_DTLS) {
        // Enforce atomic reads -- this is a short read
        unsigned int pending = SSL_pending(ssl_);

        if (pending) {
          RTC_DLOG(LS_INFO) << " -- short DTLS read. flushing";
          FlushInput(pending);
          error = SSE_MSG_TRUNC;
          return SR_ERROR;
        }
      }
      return SR_SUCCESS;
    case SSL_ERROR_WANT_READ:
      RTC_DLOG(LS_VERBOSE) << " -- error want read";
      return SR_BLOCK;
    case SSL_ERROR_WANT_WRITE:
      RTC_DLOG(LS_VERBOSE) << " -- error want write";
      ssl_read_needs_write_ = true;
      return SR_BLOCK;
    case SSL_ERROR_ZERO_RETURN:
      RTC_DLOG(LS_VERBOSE) << " -- remote side closed";
      Close();
      return SR_EOS;
    default:
      Error("SSL_read", (ssl_error ? ssl_error : -1), 0, false);
      error = ssl_error_code_;
      return SR_ERROR;
  }
  // not reached
}

void OpenSSLStreamAdapter::FlushInput(unsigned int left) {
  unsigned char buf[2048];

  while (left) {
    // This should always succeed
    const int toread = (sizeof(buf) < left) ? sizeof(buf) : left;
    const int code = SSL_read(ssl_, buf, toread);
    const int ssl_error = SSL_get_error(ssl_, code);
    RTC_DCHECK(ssl_error == SSL_ERROR_NONE);

    MaybeSetTimeout();

    if (ssl_error != SSL_ERROR_NONE) {
      RTC_DLOG(LS_VERBOSE) << " -- error " << code;
      Error("SSL_read", (ssl_error ? ssl_error : -1), 0, false);
      return;
    }

    RTC_DLOG(LS_VERBOSE) << " -- flushed " << code << " bytes";
    left -= code;
  }
}

void OpenSSLStreamAdapter::Close() {
  Cleanup(0);
  RTC_DCHECK(state_ == SSL_CLOSED || state_ == SSL_ERROR);
  // When we're closed at SSL layer, also close the stream level which
  // performs necessary clean up. Otherwise, a new incoming packet after
  // this could overflow the stream buffer.
  stream_->Close();
}

StreamState OpenSSLStreamAdapter::GetState() const {
  switch (state_) {
    case SSL_WAIT:
    case SSL_CONNECTING:
      return SS_OPENING;
    case SSL_CONNECTED:
      if (WaitingToVerifyPeerCertificate()) {
        return SS_OPENING;
      }
      return SS_OPEN;
    default:
      return SS_CLOSED;
  }
  // not reached
}

void OpenSSLStreamAdapter::OnEvent(int events, int err) {
  RTC_DCHECK_RUN_ON(&callback_sequence_);
  int events_to_signal = 0;
  int signal_error = 0;

  if ((events & SE_OPEN)) {
    RTC_DLOG(LS_VERBOSE) << "OpenSSLStreamAdapter::OnEvent SE_OPEN";
    if (state_ != SSL_WAIT) {
      RTC_DCHECK(state_ == SSL_NONE);
      events_to_signal |= SE_OPEN;
    } else {
      state_ = SSL_CONNECTING;
      if (int error = BeginSSL()) {
        Error("BeginSSL", error, 0, true);
        return;
      }
    }
  }

  if ((events & (SE_READ | SE_WRITE))) {
    RTC_DLOG(LS_VERBOSE) << "OpenSSLStreamAdapter::OnEvent"
                         << ((events & SE_READ) ? " SE_READ" : "")
                         << ((events & SE_WRITE) ? " SE_WRITE" : "");
    if (state_ == SSL_NONE) {
      events_to_signal |= events & (SE_READ | SE_WRITE);
    } else if (state_ == SSL_CONNECTING) {
      if (int error = ContinueSSL()) {
        Error("ContinueSSL", error, 0, true);
        return;
      }
    } else if (state_ == SSL_CONNECTED) {
      if (((events & SE_READ) && ssl_write_needs_read_) ||
          (events & SE_WRITE)) {
        RTC_DLOG(LS_VERBOSE) << " -- onStreamWriteable";
        events_to_signal |= SE_WRITE;
      }
      if (((events & SE_WRITE) && ssl_read_needs_write_) ||
          (events & SE_READ)) {
        RTC_DLOG(LS_VERBOSE) << " -- onStreamReadable";
        events_to_signal |= SE_READ;
      }
    }
  }

  if ((events & SE_CLOSE)) {
    RTC_DLOG(LS_VERBOSE) << "OpenSSLStreamAdapter::OnEvent(SE_CLOSE, " << err
                         << ")";
    Cleanup(0);
    events_to_signal |= SE_CLOSE;
    // SE_CLOSE is the only event that uses the final parameter to OnEvent().
    RTC_DCHECK(signal_error == 0);
    signal_error = err;
  }

  if (events_to_signal) {
    // Note that the adapter presents itself as the origin of the stream events,
    // since users of the adapter may not recognize the adapted object.
    FireEvent(events_to_signal, signal_error);
  }
}

void OpenSSLStreamAdapter::PostEvent(int events, int err) {
  owner_->PostTask(SafeTask(task_safety_.flag(), [this, events, err]() {
    RTC_DCHECK_RUN_ON(&callback_sequence_);
    FireEvent(events, err);
  }));
}

void OpenSSLStreamAdapter::SetTimeout(int delay_ms) {
  // We need to accept 0 delay here as well as >0 delay, because
  // DTLSv1_get_timeout seems to frequently return 0 ms.
  RTC_DCHECK_GE(delay_ms, 0);
  RTC_DCHECK(!timeout_task_.Running());

  timeout_task_ = RepeatingTaskHandle::DelayedStart(
      owner_, TimeDelta::Millis(delay_ms),
      [flag = task_safety_.flag(), this]() {
        if (flag->alive()) {
          RTC_DLOG(LS_INFO) << "DTLS timeout expired";
          timeout_task_.Stop();
          int res = DTLSv1_handle_timeout(ssl_);
          if (res > 0) {
            retransmission_count_++;
            RTC_LOG(LS_INFO) << "DTLS retransmission";
          } else if (res < 0) {
            Error("DTLSv1_handle_timeout", res, SSL_AD_INTERNAL_ERROR, true);
            return TimeDelta::PlusInfinity();
          }
          // We check the timer even after SSL_CONNECTED,
          // but ContinueSSL() is only needed when SSL_CONNECTING
          if (state_ == SSL_CONNECTING) {
            // Note: timeout is set inside ContinueSSL()
            ContinueSSL();
          } else if (state_ == SSL_CONNECTED) {
            MaybeSetTimeout();
          } else {
            RTC_DCHECK_NOTREACHED() << "state_: " << state_;
          }
        } else {
          RTC_DCHECK_NOTREACHED() << "flag->alive() == false";
        }
        // This callback will never run again (stopped above).
        return TimeDelta::PlusInfinity();
      });
}

int OpenSSLStreamAdapter::BeginSSL() {
  RTC_DCHECK(state_ == SSL_CONNECTING);
  // The underlying stream has opened.
  RTC_DLOG(LS_INFO) << "BeginSSL with peer.";

  BIO* bio = nullptr;

  // First set up the context.
  RTC_DCHECK(ssl_ctx_ == nullptr);
  ssl_ctx_ = SetupSSLContext();
  if (!ssl_ctx_) {
    return -1;
  }

  bio = BIO_new_stream(stream_.get());
  if (!bio) {
    return -1;
  }

  ssl_ = SSL_new(ssl_ctx_);
  if (!ssl_) {
    BIO_free(bio);
    return -1;
  }

  SSL_set_app_data(ssl_, this);

  SSL_set_bio(ssl_, bio, bio);  // the SSL object owns the bio now.

  // Use SSL_set_mtu to configure MTU insted of
  // BIO_CTRL_DGRAM_QUERY_MTU
  SSL_set_options(ssl_, SSL_OP_NO_QUERY_MTU);
  SSL_set_mtu(ssl_, dtls_mtu_);

#ifdef OPENSSL_IS_BORINGSSL
  if (ssl_mode_ == SSL_MODE_DTLS) {
    DTLSv1_set_initial_timeout_duration(ssl_, dtls_handshake_timeout_ms_);
  }

  if (!disable_ssl_group_ids_) {
    if (!SSL_set1_group_ids(ssl_, ssl_cipher_groups_.data(),
                            ssl_cipher_groups_.size())) {
      RTC_LOG(LS_WARNING) << "Failed to call SSL_set1_group_ids.";
      return -1;
    }
  }
#endif

  SSL_set_mode(ssl_, SSL_MODE_ENABLE_PARTIAL_WRITE |
                         SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

  // Do the connect
  return ContinueSSL();
}

int OpenSSLStreamAdapter::ContinueSSL() {
  RTC_DCHECK_RUN_ON(&callback_sequence_);
  RTC_DLOG(LS_VERBOSE) << "ContinueSSL";
  RTC_DCHECK_EQ(state_, SSL_CONNECTING);

  // Clear the DTLS timer
  timeout_task_.Stop();

  const int code = (role_ == SSL_CLIENT) ? SSL_connect(ssl_) : SSL_accept(ssl_);
  const int ssl_error = SSL_get_error(ssl_, code);

  switch (ssl_error) {
    case SSL_ERROR_NONE:
      RTC_DLOG(LS_INFO) << " -- success";
      // By this point, OpenSSL should have given us a certificate, or errored
      // out if one was missing.
      RTC_DCHECK(peer_cert_chain_ || !GetClientAuthEnabled());

      state_ = SSL_CONNECTED;
      if (!WaitingToVerifyPeerCertificate()) {
        // We have everything we need to start the connection, so signal
        // SE_OPEN. If we need a client certificate fingerprint and don't have
        // it yet, we'll instead signal SE_OPEN in SetPeerCertificateDigest.
        //
        // TODO(deadbeef): Post this event asynchronously to unwind the stack.
        // The caller of ContinueSSL may be the same object listening for these
        // events and may not be prepared for reentrancy.
        // PostEvent(SE_OPEN | SE_READ | SE_WRITE, 0);
        FireEvent(SE_OPEN | SE_READ | SE_WRITE, 0);
      }
      break;
    case SSL_ERROR_WANT_READ:
      RTC_DLOG(LS_INFO) << " -- error when we want to read";
      break;
    case SSL_ERROR_WANT_WRITE:
      RTC_DLOG(LS_INFO) << " -- error when we want to write";
      break;
    case SSL_ERROR_ZERO_RETURN:
    default: {
      SSLHandshakeError ssl_handshake_err = SSLHandshakeError::UNKNOWN;
      int err_code = ERR_peek_last_error();
      if (err_code != 0 && ERR_GET_REASON(err_code) == SSL_R_NO_SHARED_CIPHER) {
        ssl_handshake_err = SSLHandshakeError::INCOMPATIBLE_CIPHERSUITE;
      }
      RTC_DLOG(LS_VERBOSE) << " -- error " << code << ", " << err_code << ", "
                           << ERR_GET_REASON(err_code);
      if (handshake_error_) {
        handshake_error_(ssl_handshake_err);
      }
      return (ssl_error != 0) ? ssl_error : -1;
    }
  }

  MaybeSetTimeout();

  return 0;
}

void OpenSSLStreamAdapter::MaybeSetTimeout() {
  if (ssl_ != nullptr && !timeout_task_.Running()) {
    struct timeval timeout;
    if (DTLSv1_get_timeout(ssl_, &timeout)) {
      int delay = timeout.tv_sec * 1000 + timeout.tv_usec / 1000;
      SetTimeout(delay);
    }
  }
}

void OpenSSLStreamAdapter::Error(absl::string_view context,
                                 int err,
                                 uint8_t alert,
                                 bool signal) {
  RTC_DCHECK_RUN_ON(&callback_sequence_);
  RTC_LOG(LS_WARNING) << "OpenSSLStreamAdapter::Error(" << context << ", "
                      << err << ", " << static_cast<int>(alert) << ")";
#ifdef OPENSSL_IS_BORINGSSL
  if (err == SSL_ERROR_SSL) {
    RTC_LOG(LS_WARNING) << "GetOpenSslError: " << GetOpenSslError();
  }
#endif
  state_ = SSL_ERROR;
  ssl_error_code_ = err;
  Cleanup(alert);
  if (signal) {
    FireEvent(SE_CLOSE, err);
  }
}

void OpenSSLStreamAdapter::Cleanup(uint8_t alert) {
  RTC_DLOG(LS_INFO) << "Cleanup";

  if (state_ != SSL_ERROR) {
    state_ = SSL_CLOSED;
    ssl_error_code_ = 0;
  }

  if (ssl_) {
    int ret;
// SSL_send_fatal_alert is only available in BoringSSL.
#ifdef OPENSSL_IS_BORINGSSL
    if (alert) {
      ret = SSL_send_fatal_alert(ssl_, alert);
      if (ret < 0) {
        RTC_LOG(LS_WARNING) << "SSL_send_fatal_alert failed, error = "
                            << SSL_get_error(ssl_, ret);
      }
    } else {
#endif
      ret = SSL_shutdown(ssl_);
      if (ret < 0) {
        RTC_LOG(LS_WARNING)
            << "SSL_shutdown failed, error = " << SSL_get_error(ssl_, ret);
      }
#ifdef OPENSSL_IS_BORINGSSL
    }
#endif
    SSL_free(ssl_);
    ssl_ = nullptr;
  }
  if (ssl_ctx_) {
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
  }
#ifdef OPENSSL_IS_BORINGSSL
  clock_for_testing_.reset();
#endif
  identity_.reset();
  peer_cert_chain_.reset();

  // Clear the DTLS timer
  timeout_task_.Stop();
}

#ifdef OPENSSL_IS_BORINGSSL
OpenSSLStreamAdapter::ScopedClockForTesting::ScopedClockForTesting(
    SSL_CTX* ctx,
    Clock* clock) {
  MutexLock lock(&GlobalClockMutex());
  if (g_num_clock_for_testing_users == 0) {
    g_clock_for_testing = clock;
  } else {
    RTC_CHECK(g_clock_for_testing == clock)
        << "Multiple SSL clocks for testing is not implemented";
  }
  ++g_num_clock_for_testing_users;

  // Not used in production code. Actual time should be relative to Jan 1, 1970.
  SSL_CTX_set_current_time_cb(
      ctx, +[](const SSL*, timeval* out_clock) {
        Timestamp time = g_clock_for_testing->CurrentTime();
        out_clock->tv_sec = time.us() / TimeDelta::Seconds(1).us();
        out_clock->tv_usec = time.us() % TimeDelta::Seconds(1).us();
      });
}
OpenSSLStreamAdapter::ScopedClockForTesting::~ScopedClockForTesting() {
  MutexLock lock(&GlobalClockMutex());
  --g_num_clock_for_testing_users;
  if (g_num_clock_for_testing_users == 0) {
    g_clock_for_testing = nullptr;
  }
}
#endif

SSL_CTX* OpenSSLStreamAdapter::SetupSSLContext() {
#ifdef OPENSSL_IS_BORINGSSL
  // If X509 objects aren't used, we can use these methods to avoid
  // linking the sizable crypto/x509 code, using CRYPTO_BUFFER instead.
  SSL_CTX* ctx =
      SSL_CTX_new(ssl_mode_ == SSL_MODE_DTLS ? DTLS_with_buffers_method()
                                             : TLS_with_buffers_method());
#else
  SSL_CTX* ctx =
      SSL_CTX_new(ssl_mode_ == SSL_MODE_DTLS ? DTLS_method() : TLS_method());
#endif
  if (ctx == nullptr) {
    return nullptr;
  }

  auto min_version =
      ssl_mode_ == SSL_MODE_DTLS ? DTLS1_2_VERSION : TLS1_2_VERSION;
  auto max_version = GetMaxVersion(ssl_mode_, ssl_max_version_);

  SSL_CTX_set_min_proto_version(ctx, min_version);
  SSL_CTX_set_max_proto_version(ctx, max_version);

#ifdef OPENSSL_IS_BORINGSSL
  // SSL_CTX_set_current_time_cb is only supported in BoringSSL.
  if (g_use_time_callback_for_testing) {
    // Clock should be injected to set one for testing.
    RTC_CHECK(env_.has_value());
    clock_for_testing_.emplace(ctx, &env_->clock());
  }
  SSL_CTX_set0_buffer_pool(ctx, openssl::GetBufferPool());
#endif

  if (identity_ && !identity_->ConfigureIdentity(ctx)) {
    SSL_CTX_free(ctx);
    return nullptr;
  }

  SSL_CTX_set_info_callback(ctx, openssl::SSLInfoCallback);

  int mode = SSL_VERIFY_PEER;
  if (GetClientAuthEnabled()) {
    // Require a certificate from the client.
    // Note: Normally this is always true in production, but it may be disabled
    // for testing purposes (e.g. SSLAdapter unit tests).
    mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
  }

  // Configure a custom certificate verification callback to check the peer
  // certificate digest.
#ifdef OPENSSL_IS_BORINGSSL
  // Use CRYPTO_BUFFER version of the callback if building with BoringSSL.
  SSL_CTX_set_custom_verify(ctx, mode, SSLVerifyCallback);
#else
  // Note the second argument to SSL_CTX_set_verify is to override individual
  // errors in the default verification logic, which is not what we want here.
  SSL_CTX_set_verify(ctx, mode, nullptr);
  SSL_CTX_set_cert_verify_callback(ctx, SSLVerifyCallback, nullptr);
#endif

  // Select list of available ciphers. Note that !SHA256 and !SHA384 only
  // remove HMAC-SHA256 and HMAC-SHA384 cipher suites, not GCM cipher suites
  // with SHA256 or SHA384 as the handshake hash.
  // This matches the list of SSLClientSocketImpl in Chromium.
  SSL_CTX_set_cipher_list(
      ctx,
      "DEFAULT:!NULL:!aNULL:!SHA256:!SHA384:!aECDH:!AESGCM+AES256:!aPSK:!3DES");

  if (!srtp_ciphers_.empty()) {
    if (SSL_CTX_set_tlsext_use_srtp(ctx, srtp_ciphers_.c_str())) {
      SSL_CTX_free(ctx);
      return nullptr;
    }
  }

#ifdef OPENSSL_IS_BORINGSSL
  SSL_CTX_set_permute_extensions(ctx, true);
#endif

#if defined(OPENSSL_IS_BORINGSSL) || (OPENSSL_VERSION_NUMBER >= 0x30000000L)
  SSL_CTX_set_options(ctx, SSL_OP_NO_TICKET);
#endif

  return ctx;
}

bool OpenSSLStreamAdapter::VerifyPeerCertificate() {
  if (!HasPeerCertificateDigest() || !peer_cert_chain_ ||
      !peer_cert_chain_->GetSize()) {
    RTC_LOG(LS_WARNING) << "Missing digest or peer certificate.";
    return false;
  }

  Buffer computed_digest = Buffer::CreateWithCapacity(EVP_MAX_MD_SIZE);
  if (!peer_cert_chain_->Get(0).ComputeDigest(
          peer_certificate_digest_algorithm_, computed_digest)) {
    RTC_LOG(LS_WARNING) << "Failed to compute peer cert digest.";
    return false;
  }

  if (computed_digest != peer_certificate_digest_value_) {
    RTC_LOG(LS_WARNING)
        << "Rejected peer certificate due to mismatched digest using "
        << peer_certificate_digest_algorithm_ << ". Expected "
        << HexEncodeWithDelimiter(peer_certificate_digest_value_, ':')
        << " got " << HexEncodeWithDelimiter(computed_digest, ':');
    return false;
  }
  // Ignore any verification error if the digest matches, since there is no
  // value in checking the validity of a self-signed cert issued by untrusted
  // sources.
  RTC_DLOG(LS_INFO) << "Accepted peer certificate.";
  peer_certificate_verified_ = true;
  return true;
}

std::unique_ptr<SSLCertChain> OpenSSLStreamAdapter::GetPeerSSLCertChain()
    const {
  return peer_cert_chain_ ? peer_cert_chain_->Clone() : nullptr;
}

#ifdef OPENSSL_IS_BORINGSSL
enum ssl_verify_result_t OpenSSLStreamAdapter::SSLVerifyCallback(
    SSL* ssl,
    uint8_t* out_alert) {
  // Get our OpenSSLStreamAdapter from the context.
  OpenSSLStreamAdapter* stream =
      reinterpret_cast<OpenSSLStreamAdapter*>(SSL_get_app_data(ssl));
  const STACK_OF(CRYPTO_BUFFER)* chain = SSL_get0_peer_certificates(ssl);
  // Creates certificate chain.
  std::vector<std::unique_ptr<SSLCertificate>> cert_chain;
  for (CRYPTO_BUFFER* cert : chain) {
    cert_chain.emplace_back(new BoringSSLCertificate(bssl::UpRef(cert)));
  }
  stream->peer_cert_chain_.reset(new SSLCertChain(std::move(cert_chain)));

  // If the peer certificate digest isn't known yet, we'll wait to verify
  // until it's known, and for now just return a success status.
  if (stream->peer_certificate_digest_algorithm_.empty()) {
    RTC_LOG(LS_INFO) << "Waiting to verify certificate until digest is known.";
    // TODO(deadbeef): Use ssl_verify_retry?
    return ssl_verify_ok;
  }

  if (!stream->VerifyPeerCertificate()) {
    return ssl_verify_invalid;
  }

  return ssl_verify_ok;
}
#else   // OPENSSL_IS_BORINGSSL
int OpenSSLStreamAdapter::SSLVerifyCallback(X509_STORE_CTX* store, void* arg) {
  // Get our SSL structure and OpenSSLStreamAdapter from the store.
  SSL* ssl = reinterpret_cast<SSL*>(
      X509_STORE_CTX_get_ex_data(store, SSL_get_ex_data_X509_STORE_CTX_idx()));
  OpenSSLStreamAdapter* stream =
      reinterpret_cast<OpenSSLStreamAdapter*>(SSL_get_app_data(ssl));

  // Record the peer's certificate.
  X509* cert = X509_STORE_CTX_get0_cert(store);
  stream->peer_cert_chain_.reset(
      new SSLCertChain(std::make_unique<OpenSSLCertificate>(cert)));

  // If the peer certificate digest isn't known yet, we'll wait to verify
  // until it's known, and for now just return a success status.
  if (stream->peer_certificate_digest_algorithm_.empty()) {
    RTC_DLOG(LS_INFO) << "Waiting to verify certificate until digest is known.";
    return 1;
  }

  if (!stream->VerifyPeerCertificate()) {
    X509_STORE_CTX_set_error(store, X509_V_ERR_CERT_REJECTED);
    return 0;
  }

  return 1;
}
#endif  // !OPENSSL_IS_BORINGSSL

bool OpenSSLStreamAdapter::IsBoringSsl() {
#ifdef OPENSSL_IS_BORINGSSL
  return true;
#else
  return false;
#endif
}

#define CDEF(X) \
  { static_cast<uint16_t>(TLS1_CK_##X & 0xffff), "TLS_" #X }

struct cipher_list {
  uint16_t cipher;
  const char* cipher_str;
};

// TODO(torbjorng): Perhaps add more cipher suites to these lists.
static const cipher_list OK_RSA_ciphers[] = {
    CDEF(ECDHE_RSA_WITH_AES_128_CBC_SHA),
    CDEF(ECDHE_RSA_WITH_AES_256_CBC_SHA),
    CDEF(ECDHE_RSA_WITH_AES_128_GCM_SHA256),
#ifdef TLS1_CK_ECDHE_RSA_WITH_AES_256_GCM_SHA256
    CDEF(ECDHE_RSA_WITH_AES_256_GCM_SHA256),
#endif
#ifdef TLS1_CK_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256  // BoringSSL.
    CDEF(ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256),
#elif defined(TLS1_RFC_ECDHE_ECDSA_WITH_CHACHA20_POLY1305)  // OpenSSL.
    CDEF(ECDHE_RSA_WITH_CHACHA20_POLY1305),
#endif
};

static const cipher_list OK_ECDSA_ciphers[] = {
    CDEF(ECDHE_ECDSA_WITH_AES_128_CBC_SHA),
    CDEF(ECDHE_ECDSA_WITH_AES_256_CBC_SHA),
    CDEF(ECDHE_ECDSA_WITH_AES_128_GCM_SHA256),
#ifdef TLS1_CK_ECDHE_ECDSA_WITH_AES_256_GCM_SHA256
    CDEF(ECDHE_ECDSA_WITH_AES_256_GCM_SHA256),
#endif
#ifdef TLS1_CK_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256  // BoringSSL.
    CDEF(ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256),
#elif defined(TLS1_CK_ECDHE_ECDSA_WITH_CHACHA20_POLY1305)  // OpenSSL.
    CDEF(ECDHE_ECDSA_WITH_CHACHA20_POLY1305),
#endif
};
#undef CDEF

static const cipher_list OK_DTLS13_ciphers[] = {
#ifdef TLS1_3_CK_AES_128_GCM_SHA256  // BoringSSL TLS 1.3
    {.cipher = static_cast<uint16_t>(TLS1_3_CK_AES_128_GCM_SHA256 & 0xffff),
     .cipher_str = "TLS_AES_128_GCM_SHA256"},
#endif
#ifdef TLS1_3_CK_AES_256_GCM_SHA256  // BoringSSL TLS 1.3
    {static_cast<uint16_t>(TLS1_3_CK_AES_256_GCM_SHA256 & 0xffff),
     "TLS_AES_256_GCM_SHA256"},
#endif
#ifdef TLS1_3_CK_CHACHA20_POLY1305_SHA256  // BoringSSL TLS 1.3
    {.cipher =
         static_cast<uint16_t>(TLS1_3_CK_CHACHA20_POLY1305_SHA256 & 0xffff),
     .cipher_str = "TLS_CHACHA20_POLY1305_SHA256"},
#endif
};

bool OpenSSLStreamAdapter::IsAcceptableCipher(int cipher, KeyType key_type) {
  if (key_type == KT_RSA) {
    for (const cipher_list& c : OK_RSA_ciphers) {
      if (cipher == c.cipher) {
        return true;
      }
    }
  }

  if (key_type == KT_ECDSA) {
    for (const cipher_list& c : OK_ECDSA_ciphers) {
      if (cipher == c.cipher) {
        return true;
      }
    }
  }

  for (const cipher_list& c : OK_DTLS13_ciphers) {
    if (cipher == c.cipher) {
      return true;
    }
  }

  return false;
}

bool OpenSSLStreamAdapter::IsAcceptableCipher(absl::string_view cipher,
                                              KeyType key_type) {
  if (key_type == KT_RSA) {
    for (const cipher_list& c : OK_RSA_ciphers) {
      if (cipher == c.cipher_str) {
        return true;
      }
    }
  }

  if (key_type == KT_ECDSA) {
    for (const cipher_list& c : OK_ECDSA_ciphers) {
      if (cipher == c.cipher_str) {
        return true;
      }
    }
  }

  for (const cipher_list& c : OK_DTLS13_ciphers) {
    if (cipher == c.cipher_str) {
      return true;
    }
  }

  return false;
}

void OpenSSLStreamAdapter::EnableTimeCallbackForTesting() {
#ifdef OPENSSL_IS_BORINGSSL
  g_use_time_callback_for_testing = true;
#endif
}

SSLProtocolVersion OpenSSLStreamAdapter::GetMaxSupportedDTLSProtocolVersion() {
#if defined(OPENSSL_IS_BORINGSSL) && defined(DTLS1_3_VERSION)
  return SSL_PROTOCOL_DTLS_13;
#else
  return SSL_PROTOCOL_DTLS_12;
#endif
}

bool OpenSSLStreamAdapter::SetSslGroupIds(const std::vector<uint16_t>& groups) {
  if (state_ != SSL_NONE) {
    return false;
  }
  ssl_cipher_groups_ = groups;
  return true;
}

}  // namespace webrtc
