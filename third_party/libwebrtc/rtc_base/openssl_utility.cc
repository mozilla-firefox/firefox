/*
 *  Copyright 2018 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/openssl_utility.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/ssl3.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string>

#include "absl/algorithm/container.h"
#include "absl/strings/string_view.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "rtc_base/openssl.h"
#include "rtc_base/ssl_identity.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/strings/string_builder.h"

#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/bytestring.h>
#include <openssl/pool.h>
#endif

#ifndef WEBRTC_EXCLUDE_BUILT_IN_SSL_ROOT_CERTS
#include "rtc_base/ssl_roots.h"
#endif  // WEBRTC_EXCLUDE_BUILT_IN_SSL_ROOT_CERTS

namespace webrtc {
namespace openssl {

// Holds various helper methods.
namespace {

// Hex-encoded SHA-256 hashes of SubjectPublicKeyInfo for distrusted
// certificates. Matching SPKI covers cross-signed and reissued
// certificates with the same key.
// TODO(bugs.webrtc.org/576525998): Remove an entry once every certificate known
// to use its key has expired.
constexpr absl::string_view kDistrustedSpkiSha256[] = {
    // eMudhra intermediates with compromised P-384 private keys, cross-signed
    // by "AAA Certificate Services" (Sectigo).
    // https://bugzilla.mozilla.org/show_bug.cgi?id=2083449
    // https://go.dev/issue/82345
    // C=NL, O=eMudhra BV, CN=EM DV TLS CA - G3B
    "ebed6475c2c2d80fcb199f3fe8dd6e6b3ce60b77eb15d4e42c3dec0c6f5b5105",
    // C=US, O=eMudhra Inc, CN=EM DV TLS CA - G3C
    "49dd353dc2b919a3770645117d0e60c16115e7d1bc259eefe734b00ea434fe5f",
    // C=NL, O=eMudhra BV, CN=EM EV TLS CA - G3B
    "e0254c7fcdc265e2f0f8a428dabb75ab4ae039b4872d62f5ee57f0877d5a8a41",
    // C=US, O=eMudhra Inc, CN=EM EV TLS CA - G3C
    "5c0660a2b164910d8ff589c18e14552e9e0ec237dcefe2cb736d41a4811f81a0",
    // C=NL, O=eMudhra BV, CN=EM OV TLS CA - G3B
    "43bffa19d6ef651451f6841e019931e96f6f2eaeff28ab4b6f1ce4576de2717b",
    // C=US, O=eMudhra Inc, CN=EM OV TLS CA - G3C
    "63ecefee431d49523bc1136c4e72d0206a477086caf606cd5cf025686c109cf8",
    // C=NL, O=eMudhra BV, CN=EM OV CS CA - G3B
    "ec4ffa1ad0ac88081314b7aaffef6a91f97cd57291bb5ba686c5415f5eb4f6b6",
    // C=US, O=eMudhra Inc, CN=EM OV CS CA - G3C
    "92c063d67e8f14db71d288e11266ffdaf69cbf115f7bab14f3bc2f68d25e0e30",
};

// Returns the hex SHA-256 of `cert_der`'s SubjectPublicKeyInfo, or nullopt
// on parse failure.
std::optional<std::string> GetSpkiSha256(std::span<const uint8_t> cert_der) {
#ifdef OPENSSL_IS_BORINGSSL
  // Extract SubjectPublicKeyInfo without re-encoding.
  CBS cbs;
  CBS_init(&cbs, cert_der.data(), cert_der.size());
  CBS certificate;
  CBS tbs_certificate;
  CBS spki;
  if (!CBS_get_asn1(&cbs, &certificate, CBS_ASN1_SEQUENCE) ||
      !CBS_get_asn1(&certificate, &tbs_certificate, CBS_ASN1_SEQUENCE) ||
      // version [0] EXPLICIT Version DEFAULT v1
      !CBS_get_optional_asn1(
          &tbs_certificate, nullptr, nullptr,
          CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC) ||
      // serialNumber
      !CBS_get_asn1(&tbs_certificate, nullptr, CBS_ASN1_INTEGER) ||
      // signature
      !CBS_get_asn1(&tbs_certificate, nullptr, CBS_ASN1_SEQUENCE) ||
      // issuer
      !CBS_get_asn1(&tbs_certificate, nullptr, CBS_ASN1_SEQUENCE) ||
      // validity
      !CBS_get_asn1(&tbs_certificate, nullptr, CBS_ASN1_SEQUENCE) ||
      // subject
      !CBS_get_asn1(&tbs_certificate, nullptr, CBS_ASN1_SEQUENCE) ||
      // subjectPublicKeyInfo
      !CBS_get_asn1_element(&tbs_certificate, &spki, CBS_ASN1_SEQUENCE)) {
    return std::nullopt;
  }
  const uint8_t* spki_data = CBS_data(&spki);
  size_t spki_len = CBS_len(&spki);
#else   // OPENSSL_IS_BORINGSSL
  const unsigned char* cert_ptr = cert_der.data();
  X509* x509 = d2i_X509(nullptr, &cert_ptr,
                        checked_cast<long>(cert_der.size()));  // NOLINT
  if (x509 == nullptr) {
    return std::nullopt;
  }
  unsigned char* spki_data = nullptr;
  int spki_len = i2d_X509_PUBKEY(X509_get_X509_PUBKEY(x509), &spki_data);
  X509_free(x509);
  if (spki_len <= 0) {
    return std::nullopt;
  }
#endif  // !defined(OPENSSL_IS_BORINGSSL)

  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digest_len = 0;
  bool ok = EVP_Digest(spki_data, spki_len, digest, &digest_len, EVP_sha256(),
                       nullptr) == 1;
#ifndef OPENSSL_IS_BORINGSSL
  OPENSSL_free(spki_data);
#endif
  if (!ok) {
    return std::nullopt;
  }
  return hex_encode(
      absl::string_view(reinterpret_cast<const char*>(digest), digest_len));
}

// TODO(crbug.com/webrtc/11710): When OS certificate verification is available,
// and we don't need VerifyPeerCertMatchesHost, don't compile this in order to
// avoid a dependency on OpenSSL X509 objects (see crbug.com/webrtc/11410).
void LogCertificates([[maybe_unused]] SSL* ssl,
                     [[maybe_unused]] X509* certificate) {
// Logging certificates is extremely verbose. So it is disabled by default.
#ifdef LOG_CERTIFICATES
  BIO* mem = BIO_new(BIO_s_mem());
  if (mem == nullptr) {
    RTC_DLOG(LS_ERROR) << "BIO_new() failed to allocate memory.";
    return;
  }

  RTC_DLOG(LS_INFO) << "Certificate from server:";
  X509_print_ex(mem, certificate, XN_FLAG_SEP_CPLUS_SPC, X509_FLAG_NO_HEADER);
  BIO_write(mem, "\0", 1);

  char* buffer = nullptr;
  BIO_get_mem_data(mem, &buffer);
  if (buffer != nullptr) {
    RTC_DLOG(LS_INFO) << buffer;
  } else {
    RTC_DLOG(LS_ERROR) << "BIO_get_mem_data() failed to get buffer.";
  }
  BIO_free(mem);

  const char* cipher_name = SSL_CIPHER_get_name(SSL_get_current_cipher(ssl));
  if (cipher_name != nullptr) {
    RTC_DLOG(LS_INFO) << "Cipher: " << cipher_name;
  } else {
    RTC_DLOG(LS_ERROR) << "SSL_CIPHER_DESCRIPTION() failed to get cipher_name.";
  }
#endif
}
}  // namespace

#ifdef OPENSSL_IS_BORINGSSL
bool ParseCertificate(CRYPTO_BUFFER* cert_buffer,
                      CBS* signature_algorithm_oid,
                      int64_t* expiration_time) {
  CBS cbs;
  CRYPTO_BUFFER_init_CBS(cert_buffer, &cbs);

  //   Certificate  ::=  SEQUENCE  {
  CBS certificate;
  if (!CBS_get_asn1(&cbs, &certificate, CBS_ASN1_SEQUENCE)) {
    return false;
  }
  //        tbsCertificate       TBSCertificate,
  CBS tbs_certificate;
  if (!CBS_get_asn1(&certificate, &tbs_certificate, CBS_ASN1_SEQUENCE)) {
    return false;
  }
  //        signatureAlgorithm   AlgorithmIdentifier,
  CBS signature_algorithm;
  if (!CBS_get_asn1(&certificate, &signature_algorithm, CBS_ASN1_SEQUENCE)) {
    return false;
  }
  if (!CBS_get_asn1(&signature_algorithm, signature_algorithm_oid,
                    CBS_ASN1_OBJECT)) {
    return false;
  }
  //        signatureValue       BIT STRING  }
  if (!CBS_get_asn1(&certificate, nullptr, CBS_ASN1_BITSTRING)) {
    return false;
  }
  if (CBS_len(&certificate)) {
    return false;
  }

  // Now parse the inner TBSCertificate.
  //        version         [0]  EXPLICIT Version DEFAULT v1,
  if (!CBS_get_optional_asn1(
          &tbs_certificate, nullptr, nullptr,
          CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC)) {
    return false;
  }
  //        serialNumber         CertificateSerialNumber,
  if (!CBS_get_asn1(&tbs_certificate, nullptr, CBS_ASN1_INTEGER)) {
    return false;
  }
  //        signature            AlgorithmIdentifier
  if (!CBS_get_asn1(&tbs_certificate, nullptr, CBS_ASN1_SEQUENCE)) {
    return false;
  }
  //        issuer               Name,
  if (!CBS_get_asn1(&tbs_certificate, nullptr, CBS_ASN1_SEQUENCE)) {
    return false;
  }
  //        validity             Validity,
  CBS validity;
  if (!CBS_get_asn1(&tbs_certificate, &validity, CBS_ASN1_SEQUENCE)) {
    return false;
  }
  // Skip over notBefore.
  if (!CBS_get_any_asn1_element(&validity, nullptr, nullptr, nullptr)) {
    return false;
  }
  // Parse notAfter.
  CBS not_after;
  unsigned not_after_tag;
  if (!CBS_get_any_asn1(&validity, &not_after, &not_after_tag)) {
    return false;
  }
  bool long_format;
  if (not_after_tag == CBS_ASN1_UTCTIME) {
    long_format = false;
  } else if (not_after_tag == CBS_ASN1_GENERALIZEDTIME) {
    long_format = true;
  } else {
    return false;
  }
  if (expiration_time) {
    *expiration_time =
        ASN1TimeToSec(CBS_data(&not_after), CBS_len(&not_after), long_format);
  }
  //        subject              Name,
  if (!CBS_get_asn1_element(&tbs_certificate, nullptr, CBS_ASN1_SEQUENCE)) {
    return false;
  }
  //        subjectPublicKeyInfo SubjectPublicKeyInfo,
  if (!CBS_get_asn1(&tbs_certificate, nullptr, CBS_ASN1_SEQUENCE)) {
    return false;
  }
  //        issuerUniqueID  [1]  IMPLICIT UniqueIdentifier OPTIONAL
  if (!CBS_get_optional_asn1(&tbs_certificate, nullptr, nullptr,
                             0x01 | CBS_ASN1_CONTEXT_SPECIFIC)) {
    return false;
  }
  //        subjectUniqueID [2]  IMPLICIT UniqueIdentifier OPTIONAL
  if (!CBS_get_optional_asn1(&tbs_certificate, nullptr, nullptr,
                             0x02 | CBS_ASN1_CONTEXT_SPECIFIC)) {
    return false;
  }
  //        extensions      [3]  EXPLICIT Extensions OPTIONAL
  if (!CBS_get_optional_asn1(
          &tbs_certificate, nullptr, nullptr,
          0x03 | CBS_ASN1_CONSTRUCTED | CBS_ASN1_CONTEXT_SPECIFIC)) {
    return false;
  }
  if (CBS_len(&tbs_certificate)) {
    return false;
  }

  return true;
}
#endif  // OPENSSL_IS_BORINGSSL

bool VerifyPeerCertMatchesHost(SSL* ssl, absl::string_view host) {
  if (host.empty()) {
    RTC_DLOG(LS_ERROR) << "Hostname is empty. Cannot verify peer certificate.";
    return false;
  }

  if (ssl == nullptr) {
    RTC_DLOG(LS_ERROR) << "SSL is nullptr. Cannot verify peer certificate.";
    return false;
  }

#ifdef OPENSSL_IS_BORINGSSL
  // We can't grab a X509 object directly, as the SSL context may have been
  // initialized with TLS_with_buffers_method.
  const STACK_OF(CRYPTO_BUFFER)* chain = SSL_get0_peer_certificates(ssl);
  if (chain == nullptr || sk_CRYPTO_BUFFER_num(chain) == 0) {
    RTC_LOG(LS_ERROR)
        << "SSL_get0_peer_certificates failed. This should never happen.";
    return false;
  }
  CRYPTO_BUFFER* leaf = sk_CRYPTO_BUFFER_value(chain, 0);
  bssl::UniquePtr<X509> x509(X509_parse_from_buffer(leaf));
  if (!x509) {
    RTC_LOG(LS_ERROR) << "Failed to parse certificate to X509 object.";
    return false;
  }
  LogCertificates(ssl, x509.get());
  return X509_check_host(x509.get(), host.data(), host.size(), 0, nullptr) == 1;
#else   // OPENSSL_IS_BORINGSSL
  X509* certificate = SSL_get_peer_certificate(ssl);
  if (certificate == nullptr) {
    RTC_LOG(LS_ERROR)
        << "SSL_get_peer_certificate failed. This should never happen.";
    return false;
  }

  LogCertificates(ssl, certificate);

  bool is_valid_cert_name =
      X509_check_host(certificate, host.data(), host.size(), 0, nullptr) == 1;
  X509_free(certificate);
  return is_valid_cert_name;
#endif  // !defined(OPENSSL_IS_BORINGSSL)
}

bool IsDistrustedCertificate(std::span<const uint8_t> cert_der) {
  std::optional<std::string> spki_sha256 = GetSpkiSha256(cert_der);
  if (!spki_sha256) {
    // Fail closed, see the comment in the header.
    return true;
  }
  return absl::c_linear_search(kDistrustedSpkiSha256, *spki_sha256);
}

void LogSSLErrors(absl::string_view prefix) {
  char error_buf[200];
  unsigned long err;  // NOLINT

  while ((err = ERR_get_error()) != 0) {
    ERR_error_string_n(err, error_buf, sizeof(error_buf));
    RTC_LOG(LS_ERROR) << prefix << ": " << error_buf << "\n";
  }
}

void SSLInfoCallback(const SSL* ssl, int where, int ret) {
  switch (where) {
    case SSL_CB_LOOP:
    case SSL_CB_READ:
    case SSL_CB_WRITE:
      return;
    default:
      break;
  }
  StringBuilder ss;
  ss << SSL_state_string_long(ssl);
  if (ret == 0) {
    RTC_LOG(LS_ERROR) << "Error during " << ss.str() << "\n";
    return;
  }
  // See SSL_alert_type_string_long.
  int severity_class = where >> 8;
  switch (severity_class) {
    case SSL3_AL_WARNING:
    case SSL3_AL_FATAL:
      ss << " " << SSL_alert_type_string_long(ret);
      ss << " " << SSL_alert_desc_string_long(ret);
      RTC_LOG(LS_WARNING) << ss.str();
      break;
    default:
      RTC_LOG(LS_INFO) << ss.str();
      break;
  }
}

#ifndef WEBRTC_EXCLUDE_BUILT_IN_SSL_ROOT_CERTS
bool LoadBuiltinSSLRootCertificates(SSL_CTX* ctx) {
  int count_of_added_certs = 0;
  for (size_t i = 0; i < std::size(kSSLCertCertificateList); i++) {
    const unsigned char* cert_buffer = kSSLCertCertificateList[i];
    size_t cert_buffer_len = kSSLCertCertificateSizeList[i];
    X509* cert = d2i_X509(nullptr, &cert_buffer,
                          checked_cast<long>(cert_buffer_len));  // NOLINT
    if (cert) {
      int return_value = X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx), cert);
      if (return_value == 0) {
        RTC_LOG(LS_WARNING) << "Unable to add certificate.";
      } else {
        count_of_added_certs++;
      }
      X509_free(cert);
    }
  }
  return count_of_added_certs > 0;
}
#endif  // WEBRTC_EXCLUDE_BUILT_IN_SSL_ROOT_CERTS

#ifdef OPENSSL_IS_BORINGSSL
CRYPTO_BUFFER_POOL* GetBufferPool() {
  static CRYPTO_BUFFER_POOL* instance = CRYPTO_BUFFER_POOL_new();
  return instance;
}
#endif

}  // namespace openssl
}  // namespace webrtc
