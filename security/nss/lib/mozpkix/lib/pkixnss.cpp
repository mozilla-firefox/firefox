/*- *- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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

#include "mozpkix/pkixnss.h"

#include <limits>

#include "cryptohi.h"
#include "keyhi.h"
#include "pk11pub.h"
#include "mozpkix/nss_scoped_ptrs.h"
#include "mozpkix/pkix.h"
#include "mozpkix/pkixutil.h"
#include "secerr.h"
#include "sslerr.h"

namespace mozilla { namespace pkix {

namespace {

Result
SubjectPublicKeyInfoToSECKEYPublicKey(Input subjectPublicKeyInfo,
    ScopedSECKEYPublicKey& publicKey)
{
  SECItem subjectPublicKeyInfoSECItem(
      UnsafeMapInputToSECItem(subjectPublicKeyInfo));
  ScopedCERTSubjectPublicKeyInfo spki(
      SECKEY_DecodeDERSubjectPublicKeyInfo(&subjectPublicKeyInfoSECItem));
  if (!spki) {
    return MapPRErrorCodeToResult(PR_GetError());
  }
  publicKey.reset(SECKEY_ExtractPublicKey(spki.get()));
  if (!publicKey) {
    return MapPRErrorCodeToResult(PR_GetError());
  }
  return Success;
}

template<size_t N>
Result
VerifySignedData(SECKEYPublicKey* publicKey, CK_MECHANISM_TYPE mechanism,
    SECItem* params, SECItem* signature, SECItem* data,
    SECOidTag (&policyTags)[N], void* pkcs11PinArg)
{
  // Hash and signature algorithms can be disabled by policy in NSS. However,
  // the policy engine in NSS is not currently sophisticated enough to, for
  // example, infer that disabling SEC_OID_SHA1 (i.e. the hash algorithm SHA1)
  // should also disable SEC_OID_PKCS1_SHA1_WITH_RSA_ENCRYPTION. Thus, this
  // implementation checks the signature algorithm, the hash algorithm, and the
  // signature algorithm with the hash algorithm together.
  for (size_t i = 0; i < sizeof(policyTags) / sizeof(policyTags[0]); i++) {
    SECOidTag policyTag = policyTags[i];
    uint32_t policyFlags;
    if (NSS_GetAlgorithmPolicy(policyTag, &policyFlags) != SECSuccess) {
      return MapPRErrorCodeToResult(PR_GetError());
    }
    if (!(policyFlags & NSS_USE_ALG_IN_ANY_SIGNATURE)) {
      return Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED;
    }
  }
  SECStatus srv = PK11_VerifyWithMechanism(publicKey, mechanism, params,
      signature, data, pkcs11PinArg);
  if (srv != SECSuccess) {
    return MapPRErrorCodeToResult(PR_GetError());
  }
  return Success;
}
} // namespace

Result
VerifyRSAPKCS1SignedDataNSS(Input data, DigestAlgorithm digestAlgorithm,
    Input signature, Input subjectPublicKeyInfo, void* pkcs11PinArg)
{
  ScopedSECKEYPublicKey publicKey;
  Result rv = SubjectPublicKeyInfoToSECKEYPublicKey(subjectPublicKeyInfo,
      publicKey);
  if (rv != Success) {
    return rv;
  }
  SECItem signatureItem(UnsafeMapInputToSECItem(signature));
  SECItem dataItem(UnsafeMapInputToSECItem(data));
  CK_MECHANISM_TYPE mechanism;
  SECOidTag signaturePolicyTag = SEC_OID_PKCS1_RSA_ENCRYPTION;
  SECOidTag hashPolicyTag;
  SECOidTag combinedPolicyTag;
  switch (digestAlgorithm) {
    case DigestAlgorithm::sha512:
      mechanism = CKM_SHA512_RSA_PKCS;
      hashPolicyTag = SEC_OID_SHA512;
      combinedPolicyTag = SEC_OID_PKCS1_SHA512_WITH_RSA_ENCRYPTION;
      break;
    case DigestAlgorithm::sha384:
      mechanism = CKM_SHA384_RSA_PKCS;
      hashPolicyTag = SEC_OID_SHA384;
      combinedPolicyTag = SEC_OID_PKCS1_SHA384_WITH_RSA_ENCRYPTION;
      break;
    case DigestAlgorithm::sha256:
      mechanism = CKM_SHA256_RSA_PKCS;
      hashPolicyTag = SEC_OID_SHA256;
      combinedPolicyTag = SEC_OID_PKCS1_SHA256_WITH_RSA_ENCRYPTION;
      break;
    case DigestAlgorithm::sha1:
      mechanism = CKM_SHA1_RSA_PKCS;
      hashPolicyTag = SEC_OID_SHA1;
      combinedPolicyTag = SEC_OID_PKCS1_SHA1_WITH_RSA_ENCRYPTION;
      break;
    MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM
  }
  SECOidTag policyTags[3] =
      { signaturePolicyTag, hashPolicyTag, combinedPolicyTag };
  return VerifySignedData(publicKey.get(), mechanism, nullptr, &signatureItem,
      &dataItem, policyTags, pkcs11PinArg);
}

Result
VerifyRSAPSSSignedDataNSS(Input data, DigestAlgorithm digestAlgorithm,
    Input signature, Input subjectPublicKeyInfo, void* pkcs11PinArg)
{
  ScopedSECKEYPublicKey publicKey;
  Result rv = SubjectPublicKeyInfoToSECKEYPublicKey(subjectPublicKeyInfo,
      publicKey);
  if (rv != Success) {
    return rv;
  }
  SECItem signatureItem(UnsafeMapInputToSECItem(signature));
  SECItem dataItem(UnsafeMapInputToSECItem(data));
  CK_MECHANISM_TYPE mechanism;
  SECOidTag signaturePolicyTag = SEC_OID_PKCS1_RSA_PSS_SIGNATURE;
  SECOidTag hashPolicyTag;
  CK_RSA_PKCS_PSS_PARAMS rsaPSSParams;
  switch (digestAlgorithm) {
    case DigestAlgorithm::sha512:
      mechanism = CKM_SHA512_RSA_PKCS_PSS;
      hashPolicyTag = SEC_OID_SHA512;
      rsaPSSParams.hashAlg = CKM_SHA512;
      rsaPSSParams.mgf = CKG_MGF1_SHA512;
      rsaPSSParams.sLen = 64;
      break;
    case DigestAlgorithm::sha384:
      mechanism = CKM_SHA384_RSA_PKCS_PSS;
      hashPolicyTag = SEC_OID_SHA384;
      rsaPSSParams.hashAlg = CKM_SHA384;
      rsaPSSParams.mgf = CKG_MGF1_SHA384;
      rsaPSSParams.sLen = 48;
      break;
    case DigestAlgorithm::sha256:
      mechanism = CKM_SHA256_RSA_PKCS_PSS;
      hashPolicyTag = SEC_OID_SHA256;
      rsaPSSParams.hashAlg = CKM_SHA256;
      rsaPSSParams.mgf = CKG_MGF1_SHA256;
      rsaPSSParams.sLen = 32;
      break;
    case DigestAlgorithm::sha1:
      return Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED;
      break;
    MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM
  }
  SECItem params;
  params.data = reinterpret_cast<unsigned char*>(&rsaPSSParams);
  params.len = sizeof(CK_RSA_PKCS_PSS_PARAMS);
  SECOidTag policyTags[2] = { signaturePolicyTag, hashPolicyTag };
  return VerifySignedData(publicKey.get(), mechanism, &params, &signatureItem,
      &dataItem, policyTags, pkcs11PinArg);
}

Result
EncodedECDSASignatureToRawPoint(Input signature,
    const ScopedSECKEYPublicKey& publicKey, ScopedSECItem& result) {
  Input r;
  Input s;
  Result rv = der::ECDSASigValue(signature, r, s);
  if (rv != Success) {
    return Result::ERROR_BAD_SIGNATURE;
  }
  size_t signatureLength = SECKEY_SignatureLen(publicKey.get());
  if (signatureLength == 0) {
    return MapPRErrorCodeToResult(PR_GetError());
  }
  if (signatureLength % 2 != 0) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }
  size_t coordinateLength = signatureLength / 2;
  if (r.GetLength() > coordinateLength || s.GetLength() > coordinateLength) {
    return Result::ERROR_BAD_SIGNATURE;
  }
  ScopedSECItem signatureItem(
      SECITEM_AllocItem(nullptr, nullptr, signatureLength));
  if (!signatureItem) {
    return Result::FATAL_ERROR_NO_MEMORY;
  }
  memset(signatureItem->data, 0, signatureLength);
  memcpy(signatureItem->data + (coordinateLength - r.GetLength()),
      r.UnsafeGetData(), r.GetLength());
  memcpy(signatureItem->data + (2 * coordinateLength - s.GetLength()),
      s.UnsafeGetData(), s.GetLength());
  result.swap(signatureItem);
  return Success;
}

Result
VerifyECDSASignedDataNSS(Input data, DigestAlgorithm digestAlgorithm,
    Input signature, Input subjectPublicKeyInfo, void* pkcs11PinArg)
{
  ScopedSECKEYPublicKey publicKey;
  Result rv = SubjectPublicKeyInfoToSECKEYPublicKey(subjectPublicKeyInfo,
      publicKey);
  if (rv != Success) {
    return rv;
  }

  ScopedSECItem signatureItem;
  rv = EncodedECDSASignatureToRawPoint(signature, publicKey, signatureItem);
  if (rv != Success) {
    return rv;
  }

  SECItem dataItem(UnsafeMapInputToSECItem(data));
  CK_MECHANISM_TYPE mechanism;
  SECOidTag signaturePolicyTag = SEC_OID_ANSIX962_EC_PUBLIC_KEY;
  SECOidTag hashPolicyTag;
  SECOidTag combinedPolicyTag;
  switch (digestAlgorithm) {
    case DigestAlgorithm::sha512:
      mechanism = CKM_ECDSA_SHA512;
      hashPolicyTag = SEC_OID_SHA512;
      combinedPolicyTag = SEC_OID_ANSIX962_ECDSA_SHA512_SIGNATURE;
      break;
    case DigestAlgorithm::sha384:
      mechanism = CKM_ECDSA_SHA384;
      hashPolicyTag = SEC_OID_SHA384;
      combinedPolicyTag = SEC_OID_ANSIX962_ECDSA_SHA384_SIGNATURE;
      break;
    case DigestAlgorithm::sha256:
      mechanism = CKM_ECDSA_SHA256;
      hashPolicyTag = SEC_OID_SHA256;
      combinedPolicyTag = SEC_OID_ANSIX962_ECDSA_SHA256_SIGNATURE;
      break;
    case DigestAlgorithm::sha1:
      mechanism = CKM_ECDSA_SHA1;
      hashPolicyTag = SEC_OID_SHA1;
      combinedPolicyTag = SEC_OID_ANSIX962_ECDSA_SHA1_SIGNATURE;
      break;
    MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM
  }
  SECOidTag policyTags[3] =
      { signaturePolicyTag, hashPolicyTag, combinedPolicyTag };
  return VerifySignedData(publicKey.get(), mechanism, nullptr,
      signatureItem.get(), &dataItem, policyTags, pkcs11PinArg);
}

Result
VerifyMLDSASignedDataNSS(Input data, Input signature,
    Input subjectPublicKeyInfo, void* pkcs11PinArg)
{
  ScopedSECKEYPublicKey publicKey;
  Result rv = SubjectPublicKeyInfoToSECKEYPublicKey(subjectPublicKeyInfo,
      publicKey);
  if (rv != Success) {
    return rv;
  }
  SECItem signatureItem(UnsafeMapInputToSECItem(signature));
  SECItem dataItem(UnsafeMapInputToSECItem(data));
  /* ML-DSA is a pure signature — pass raw message bytes, no pre-hash.
   * CKM_ML_DSA uses the paramset embedded in the key object. */
  SECOidTag policyTags[1] = { SEC_OID_ML_DSA_87 };
  return VerifySignedData(publicKey.get(), CKM_ML_DSA, nullptr,
      &signatureItem, &dataItem, policyTags, pkcs11PinArg);
}

Result
DigestBufNSS(Input item,
             DigestAlgorithm digestAlg,
             /*out*/ uint8_t* digestBuf,
             size_t digestBufLen)
{
  SECOidTag oid;
  size_t bits;
  switch (digestAlg) {
    case DigestAlgorithm::sha512: oid = SEC_OID_SHA512; bits = 512; break;
    case DigestAlgorithm::sha384: oid = SEC_OID_SHA384; bits = 384; break;
    case DigestAlgorithm::sha256: oid = SEC_OID_SHA256; bits = 256; break;
    case DigestAlgorithm::sha1: oid = SEC_OID_SHA1; bits = 160; break;
    MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM
  }
  if (digestBufLen != bits / 8) {
    return Result::FATAL_ERROR_INVALID_ARGS;
  }

  SECItem itemSECItem = UnsafeMapInputToSECItem(item);
  if (itemSECItem.len >
        static_cast<decltype(itemSECItem.len)>(
          std::numeric_limits<int32_t>::max())) {
    PR_NOT_REACHED("large items should not be possible here");
    return Result::FATAL_ERROR_INVALID_ARGS;
  }
  SECStatus srv = PK11_HashBuf(oid, digestBuf, itemSECItem.data,
                               static_cast<int32_t>(itemSECItem.len));
  if (srv != SECSuccess) {
    return MapPRErrorCodeToResult(PR_GetError());
  }
  return Success;
}

Result
MapPRErrorCodeToResult(PRErrorCode error)
{
  switch (error)
  {
#define MOZILLA_PKIX_MAP(mozilla_pkix_result, value, nss_result) \
    case nss_result: return Result::mozilla_pkix_result;

    MOZILLA_PKIX_MAP_LIST

#undef MOZILLA_PKIX_MAP

    default:
      return Result::ERROR_UNKNOWN_ERROR;
  }
}

PRErrorCode
MapResultToPRErrorCode(Result result)
{
  switch (result)
  {
#define MOZILLA_PKIX_MAP(mozilla_pkix_result, value, nss_result) \
    case Result::mozilla_pkix_result: return nss_result;

    MOZILLA_PKIX_MAP_LIST

#undef MOZILLA_PKIX_MAP

    MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM
  }
}

void
RegisterErrorTable()
{
  // Note that these error strings are not localizable.
  // When these strings change, update the localization information too.
  static const PRErrorMessage ErrorTableText[] = {
    { "MOZILLA_PKIX_ERROR_KEY_PINNING_FAILURE",
      "The server uses key pinning (HPKP) but no trusted certificate chain "
      "could be constructed that matches the pinset. Key pinning violations "
      "cannot be overridden." },
    { "MOZILLA_PKIX_ERROR_CA_CERT_USED_AS_END_ENTITY",
      "The server uses a certificate with a basic constraints extension "
      "identifying it as a certificate authority. For a properly-issued "
      "certificate, this should not be the case." },
    { "MOZILLA_PKIX_ERROR_INADEQUATE_KEY_SIZE",
      "The server presented a certificate with a key size that is too small "
      "to establish a secure connection." },
    { "MOZILLA_PKIX_ERROR_V1_CERT_USED_AS_CA",
      "An X.509 version 1 certificate that is not a trust anchor was used to "
      "issue the server's certificate. X.509 version 1 certificates are "
      "deprecated and should not be used to sign other certificates." },
    { "MOZILLA_PKIX_ERROR_NO_RFC822NAME_MATCH",
      "The certificate is not valid for the given email address." },
    { "MOZILLA_PKIX_ERROR_NOT_YET_VALID_CERTIFICATE",
      "The server presented a certificate that is not yet valid." },
    { "MOZILLA_PKIX_ERROR_NOT_YET_VALID_ISSUER_CERTIFICATE",
      "A certificate that is not yet valid was used to issue the server's "
      "certificate." },
    { "MOZILLA_PKIX_ERROR_SIGNATURE_ALGORITHM_MISMATCH",
      "The signature algorithm in the signature field of the certificate does "
      "not match the algorithm in its signatureAlgorithm field." },
    { "MOZILLA_PKIX_ERROR_OCSP_RESPONSE_FOR_CERT_MISSING",
      "The OCSP response does not include a status for the certificate being "
      "verified." },
    { "MOZILLA_PKIX_ERROR_VALIDITY_TOO_LONG",
      "The server presented a certificate that is valid for too long." },
    { "MOZILLA_PKIX_ERROR_REQUIRED_TLS_FEATURE_MISSING",
      "A required TLS feature is missing." },
    { "MOZILLA_PKIX_ERROR_INVALID_INTEGER_ENCODING",
      "The server presented a certificate that contains an invalid encoding of "
      "an integer. Common causes include negative serial numbers, negative RSA "
      "moduli, and encodings that are longer than necessary." },
    { "MOZILLA_PKIX_ERROR_EMPTY_ISSUER_NAME",
      "The server presented a certificate with an empty issuer distinguished "
      "name." },
    { "MOZILLA_PKIX_ERROR_ADDITIONAL_POLICY_CONSTRAINT_FAILED",
      "An additional policy constraint failed when validating this "
      "certificate." },
    { "MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT",
      "The certificate is not trusted because it is self-signed." },
    { "MOZILLA_PKIX_ERROR_MITM_DETECTED",
      "Your connection is being intercepted by a TLS proxy. Uninstall it if "
      "possible or configure your device to trust its root certificate." },
    { "MOZILLA_PKIX_ERROR_INSUFFICIENT_CERTIFICATE_TRANSPARENCY",
      "The server presented insufficient certificate transparency information."
      " Its certificate may not have been publicly disclosed, and it may have "
      "been misissued." },
    { "MOZILLA_PKIX_ERROR_ISSUER_NO_LONGER_TRUSTED",
      "The certificate was issued by a certificate authority that is no longer"
      " trusted to issue new certificates." },
  };
  // Note that these error strings are not localizable.
  // When these strings change, update the localization information too.

  static const PRErrorTable ErrorTable = {
    ErrorTableText,
    "pkixerrors",
    ERROR_BASE,
    PR_ARRAY_SIZE(ErrorTableText)
  };

  (void) PR_ErrorInstallTable(&ErrorTable);
}

} } // namespace mozilla::pkix
