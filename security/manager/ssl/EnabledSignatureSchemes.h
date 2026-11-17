/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_psm_EnabledSignatureSchemes_h_
#define mozilla_psm_EnabledSignatureSchemes_h_

// NOTE: Should this list ever include ssl_sig_rsa_pss_pss_sha* (or should
// it become possible to enable this scheme via a pref), it is required
// to test that a Delegated Credential containing a small-modulus RSA-PSS SPKI
// is properly rejected. NSS will not advertise PKCS1 or RSAE schemes (which
// the |ssl_sig_rsa_pss_*| defines alias, meaning we will not currently accept
// any RSA DC.

#define FOR_EACH_ENABLED_SIGNATURE_SCHEME(SCHEME)             \
  SCHEME(ssl_sig_mldsa44, "ML-DSA-44")                        \
  SCHEME(ssl_sig_mldsa65, "ML-DSA-65")                        \
  SCHEME(ssl_sig_mldsa87, "ML-DSA-87")                        \
  SCHEME(ssl_sig_ecdsa_secp256r1_sha256, "ECDSA-P256-SHA256") \
  SCHEME(ssl_sig_ecdsa_secp384r1_sha384, "ECDSA-P384-SHA384") \
  SCHEME(ssl_sig_ecdsa_secp521r1_sha512, "ECDSA-P521-SHA512") \
  SCHEME(ssl_sig_rsa_pss_rsae_sha256, "RSA-PSS-SHA256")       \
  SCHEME(ssl_sig_rsa_pss_rsae_sha384, "RSA-PSS-SHA384")       \
  SCHEME(ssl_sig_rsa_pss_rsae_sha512, "RSA-PSS-SHA512")       \
  SCHEME(ssl_sig_rsa_pkcs1_sha256, "RSA-PKCS1-SHA256")        \
  SCHEME(ssl_sig_rsa_pkcs1_sha384, "RSA-PKCS1-SHA384")        \
  SCHEME(ssl_sig_rsa_pkcs1_sha512, "RSA-PKCS1-SHA512")        \
  SCHEME(ssl_sig_ecdsa_sha1, "ECDSA-SHA1")                    \
  SCHEME(ssl_sig_rsa_pkcs1_sha1, "RSA-PKCS1-SHA1")
#endif
