/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This code is made available to you under your choice of the following sets
 * of licensing terms:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/* Copyright 2013 Mozilla Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef mozilla_pkix_pkixnss_h
#define mozilla_pkix_pkixnss_h

#include <seccomon.h>
#include "mozpkix/pkixtypes.h"
#include "prerror.h"

namespace mozilla {
namespace pkix {

// Verifies the PKCS#1.5 signature on the given data using the given RSA public
// key.
Result VerifyRSAPKCS1SignedDataNSS(Input data, DigestAlgorithm digestAlgorithm,
                                   Input signature, Input subjectPublicKeyInfo,
                                   void* pkcs11PinArg);

// Verifies the RSA-PSS signature on the given data using the given RSA
// public key.
Result VerifyRSAPSSSignedDataNSS(Input data, DigestAlgorithm digestAlgorithm,
                                 Input signature, Input subjectPublicKeyInfo,
                                 void* pkcs11PinArg);

// Verifies the ECDSA signature on the given data using the given ECC public
// key.
Result VerifyECDSASignedDataNSS(Input data, DigestAlgorithm digestAlgorithm,
                                Input signature, Input subjectPublicKeyInfo,
                                void* pkcs11PinArg);

// Verifies an ML-DSA-87 signature (NIST FIPS 204, pure-signature, no hash).
Result VerifyMLDSASignedDataNSS(Input data, Input signature,
                                Input subjectPublicKeyInfo,
                                void* pkcs11PinArg);

// Computes the digest of the given data using the given digest algorithm.
//
// item contains the data to hash.
// digestBuf must point to a buffer to where the digest will be written.
// digestBufLen must be the size of the buffer, which must be exactly equal
//              to the size of the digest output (20 for SHA-1, 32 for SHA-256,
//              etc.)
//
// TODO: Taking the output buffer as (uint8_t*, size_t) is counter to our
// other, extensive, memory safety efforts in mozilla::pkix, and we should find
// a way to provide a more-obviously-safe interface.
Result DigestBufNSS(Input item, DigestAlgorithm digestAlg,
                    /*out*/ uint8_t* digestBuf, size_t digestBufLen);

Result MapPRErrorCodeToResult(PRErrorCode errorCode);
PRErrorCode MapResultToPRErrorCode(Result result);

// The error codes within each module must fit in 16 bits. We want these
// errors to fit in the same module as the NSS errors but not overlap with
// any of them. Converting an NSS SEC, NSS SSL, or PSM error to an NS error
// involves negating the value of the error and then synthesizing an error
// in the NS_ERROR_MODULE_SECURITY module. Hence, PSM errors will start at
// a negative value that both doesn't overlap with the current value
// ranges for NSS errors and that will fit in 16 bits when negated.
static const PRErrorCode ERROR_BASE = -0x4000;
static const PRErrorCode ERROR_LIMIT = ERROR_BASE + 1000;

enum ErrorCode {
  MOZILLA_PKIX_ERROR_KEY_PINNING_FAILURE = ERROR_BASE + 0,
  MOZILLA_PKIX_ERROR_CA_CERT_USED_AS_END_ENTITY = ERROR_BASE + 1,
  MOZILLA_PKIX_ERROR_INADEQUATE_KEY_SIZE = ERROR_BASE + 2,
  MOZILLA_PKIX_ERROR_V1_CERT_USED_AS_CA = ERROR_BASE + 3,
  MOZILLA_PKIX_ERROR_NO_RFC822NAME_MATCH = ERROR_BASE + 4,
  MOZILLA_PKIX_ERROR_NOT_YET_VALID_CERTIFICATE = ERROR_BASE + 5,
  MOZILLA_PKIX_ERROR_NOT_YET_VALID_ISSUER_CERTIFICATE = ERROR_BASE + 6,
  MOZILLA_PKIX_ERROR_SIGNATURE_ALGORITHM_MISMATCH = ERROR_BASE + 7,
  MOZILLA_PKIX_ERROR_OCSP_RESPONSE_FOR_CERT_MISSING = ERROR_BASE + 8,
  MOZILLA_PKIX_ERROR_VALIDITY_TOO_LONG = ERROR_BASE + 9,
  MOZILLA_PKIX_ERROR_REQUIRED_TLS_FEATURE_MISSING = ERROR_BASE + 10,
  MOZILLA_PKIX_ERROR_INVALID_INTEGER_ENCODING = ERROR_BASE + 11,
  MOZILLA_PKIX_ERROR_EMPTY_ISSUER_NAME = ERROR_BASE + 12,
  MOZILLA_PKIX_ERROR_ADDITIONAL_POLICY_CONSTRAINT_FAILED = ERROR_BASE + 13,
  MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT = ERROR_BASE + 14,
  MOZILLA_PKIX_ERROR_MITM_DETECTED = ERROR_BASE + 15,
  MOZILLA_PKIX_ERROR_INSUFFICIENT_CERTIFICATE_TRANSPARENCY = ERROR_BASE + 16,
  MOZILLA_PKIX_ERROR_ISSUER_NO_LONGER_TRUSTED = ERROR_BASE + 17,
  END_OF_LIST
};

void RegisterErrorTable();

inline SECItem UnsafeMapInputToSECItem(Input input) {
  SECItem result = {siBuffer, const_cast<uint8_t*>(input.UnsafeGetData()),
                    input.GetLength()};
  static_assert(sizeof(decltype(input.GetLength())) <= sizeof(result.len),
                "input.GetLength() must fit in a SECItem");
  return result;
}
}  // namespace pkix
}  // namespace mozilla

#endif  // mozilla_pkix_pkixnss_h
