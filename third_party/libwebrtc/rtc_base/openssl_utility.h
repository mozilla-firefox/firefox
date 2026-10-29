/*
 *  Copyright 2018 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef RTC_BASE_OPENSSL_UTILITY_H_
#define RTC_BASE_OPENSSL_UTILITY_H_

#include <openssl/ossl_typ.h>

#include <cstdint>
#include <span>

#include "absl/strings/string_view.h"

namespace webrtc {
// The openssl namespace holds static helper methods. All methods related
// to OpenSSL that are commonly used and don't require global state should be
// placed here.
namespace openssl {

#ifdef OPENSSL_IS_BORINGSSL
// Does minimal parsing of a certificate (only verifying the presence of major
// fields), primarily for the purpose of extracting the relevant out
// parameters. Any that the caller is uninterested in can be null.
bool ParseCertificate(CRYPTO_BUFFER* cert_buffer,
                      CBS* signature_algorithm_oid,
                      int64_t* expiration_time);
#endif

// Verifies that the hostname provided matches that in the peer certificate
// attached to this SSL state.
// TODO(crbug.com/webrtc/11710): When OS certificate verification is available,
// skip compiling this as it adds a dependency on OpenSSL X509 objects, which we
// are trying to avoid in favor of CRYPTO_BUFFERs (see crbug.com/webrtc/11410).
bool VerifyPeerCertMatchesHost(SSL* ssl, absl::string_view host);

// Returns true if the SubjectPublicKeyInfo of the DER-encoded certificate
// `cert_der` is explicitly distrusted (e.g. because the private key of a CA was
// compromised). Matching on the public key also covers cross-signed and
// reissued certificates for the same key.
//
// Fails closed: also returns true if `cert_der` cannot be parsed. WebPKI
// certificates must be DER-encoded, and a lenient custom verifier could
// otherwise accept a BER re-encoding of a distrusted certificate whose outer
// SEQUENCE (not covered by the issuer's signature) this parser rejects.
bool IsDistrustedCertificate(std::span<const uint8_t> cert_der);

// Logs all the errors in the OpenSSL errror queue from the current thread. A
// prefix can be provided for context.
void LogSSLErrors(absl::string_view prefix);

// Logs a human-readable description of the SSL connection's handshake state
// and any alerts. Intended to be installed via SSL_CTX_set_info_callback().
void SSLInfoCallback(const SSL* ssl, int where, int ret);

#ifndef WEBRTC_EXCLUDE_BUILT_IN_SSL_ROOT_CERTS
// Attempt to add the certificates from the loader into the SSL_CTX. False is
// returned only if there are no certificates returned from the loader or none
// of them can be added to the TrustStore for the provided context.
bool LoadBuiltinSSLRootCertificates(SSL_CTX* ssl_ctx);
#endif  // WEBRTC_EXCLUDE_BUILT_IN_SSL_ROOT_CERTS

#ifdef OPENSSL_IS_BORINGSSL
CRYPTO_BUFFER_POOL* GetBufferPool();
#endif

}  // namespace openssl
}  // namespace webrtc


#endif  // RTC_BASE_OPENSSL_UTILITY_H_
