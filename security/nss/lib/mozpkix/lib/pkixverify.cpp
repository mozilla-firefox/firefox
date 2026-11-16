/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This code is made available to you under your choice of the following sets
 * of licensing terms:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/* Copyright 2015 Mozilla Contributors
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

#include <optional>

#include "mozpkix/pkixutil.h"

namespace mozilla { namespace pkix {

Result
VerifySignedData(TrustDomain& trustDomain,
                 const der::SignedDataWithSignature& signedData,
                 Input signerSubjectPublicKeyInfo)
{
  der::PublicKeyAlgorithm publicKeyAlg;
  std::optional<DigestAlgorithm> digestAlgorithm = std::nullopt;
  Reader signatureAlg(signedData.algorithm);
  Result rv = der::SignatureAlgorithmIdentifierValue(
                signatureAlg, publicKeyAlg, digestAlgorithm);
  if (rv != Success) {
    return rv;
  }
  if (!signatureAlg.AtEnd()) {
    return Result::ERROR_BAD_DER;
  }

  if (!der::HasConsistentDigest(publicKeyAlg, digestAlgorithm)) {
    return Result::ERROR_INVALID_ALGORITHM;
  }

  switch (publicKeyAlg) {
    case der::PublicKeyAlgorithm::ECDSA:
      return trustDomain.VerifyECDSASignedData(signedData.data,
          digestAlgorithm.value(), signedData.signature, signerSubjectPublicKeyInfo);
    case der::PublicKeyAlgorithm::RSA_PKCS1:
      return trustDomain.VerifyRSAPKCS1SignedData(signedData.data,
          digestAlgorithm.value(), signedData.signature, signerSubjectPublicKeyInfo);
    case der::PublicKeyAlgorithm::RSA_PSS:
      return trustDomain.VerifyRSAPSSSignedData(signedData.data,
          digestAlgorithm.value(), signedData.signature, signerSubjectPublicKeyInfo);
    case der::PublicKeyAlgorithm::MLDSA_44:
      return trustDomain.VerifyMLDSASignedData(signedData.data,
          MLDSAParameterSet::ml_dsa_44, signedData.signature,
          signerSubjectPublicKeyInfo);
    case der::PublicKeyAlgorithm::MLDSA_65:
      return trustDomain.VerifyMLDSASignedData(signedData.data,
          MLDSAParameterSet::ml_dsa_65, signedData.signature,
          signerSubjectPublicKeyInfo);
    case der::PublicKeyAlgorithm::MLDSA_87:
      return trustDomain.VerifyMLDSASignedData(signedData.data,
          MLDSAParameterSet::ml_dsa_87, signedData.signature,
          signerSubjectPublicKeyInfo);
    MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM
  }
}

} } // namespace mozilla::pkix
