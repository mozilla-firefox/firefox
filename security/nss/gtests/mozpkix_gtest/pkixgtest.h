/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This code is made available to you under your choice of the following sets
 * of licensing terms:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/* Copyright 2014 Mozilla Contributors
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
#ifndef mozilla_pkix_pkixgtest_h
#define mozilla_pkix_pkixgtest_h

#include <ostream>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated"
#pragma clang diagnostic ignored "-Wmissing-noreturn"
#pragma clang diagnostic ignored "-Wshift-sign-overflow"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wundef"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wextra"
#elif defined(_MSC_VER)
#pragma warning(push, 3)
// C4224: Nonstandard extension used: formal parameter 'X' was previously
//        defined as a type.
#pragma warning(disable : 4224)
// C4826: Conversion from 'type1 ' to 'type_2' is sign - extended. This may
//        cause unexpected runtime behavior.
#pragma warning(disable : 4826)
#endif

#include "gtest/gtest.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "mozpkix/pkix.h"
#include "mozpkix/pkixder.h"
#include "mozpkix/test/pkixtestutil.h"

// PrintTo must be in the same namespace as the type we're overloading it for.
namespace mozilla {
namespace pkix {

inline void PrintTo(const Result& result, ::std::ostream* os) {
  const char* stringified = MapResultToName(result);
  if (stringified) {
    *os << stringified;
  } else {
    *os << "mozilla::pkix::Result(" << static_cast<unsigned int>(result) << ")";
  }
}
}  // namespace pkix
}  // namespace mozilla

namespace mozilla {
namespace pkix {
namespace test {

extern const std::time_t oneDayBeforeNow;
extern const std::time_t oneDayAfterNow;
extern const std::time_t twoDaysBeforeNow;
extern const std::time_t twoDaysAfterNow;
extern const std::time_t tenDaysBeforeNow;
extern const std::time_t tenDaysAfterNow;

class EverythingFailsByDefaultTrustDomain : public TrustDomain {
 public:
  Result GetCertTrust(EndEntityOrCA, const CertPolicyId&, Input,
                      /*out*/ TrustLevel&) override {
    ADD_FAILURE();
    return NotReached("GetCertTrust should not be called",
                      Result::FATAL_ERROR_LIBRARY_FAILURE);
  }

  Result FindIssuer(Input, IssuerChecker&, Time) override {
    ADD_FAILURE();
    return NotReached("FindIssuer should not be called",
                      Result::FATAL_ERROR_LIBRARY_FAILURE);
  }

  Result CheckRevocation(EndEntityOrCA, const CertID&, Time, Duration,
                         /*optional*/ const Input*,
                         /*optional*/ const Input*) override {
    ADD_FAILURE();
    return NotReached("CheckRevocation should not be called",
                      Result::FATAL_ERROR_LIBRARY_FAILURE);
  }

  Result IsChainValid(const DERArray&, Time, const CertPolicyId&) override {
    ADD_FAILURE();
    return NotReached("IsChainValid should not be called",
                      Result::FATAL_ERROR_LIBRARY_FAILURE);
  }

  Result DigestBuf(Input, DigestAlgorithm, /*out*/ uint8_t*, size_t) override {
    ADD_FAILURE();
    return NotReached("DigestBuf should not be called",
                      Result::FATAL_ERROR_LIBRARY_FAILURE);
  }

  Result CheckSignatureDigestAlgorithm(DigestAlgorithm, EndEntityOrCA,
                                       Time) override {
    ADD_FAILURE();
    return NotReached("CheckSignatureDigestAlgorithm should not be called",
                      Result::FATAL_ERROR_LIBRARY_FAILURE);
  }

  Result CheckECDSACurveIsAcceptable(EndEntityOrCA, NamedCurve) override {
    ADD_FAILURE();
    return NotReached("CheckECDSACurveIsAcceptable should not be called",
                      Result::FATAL_ERROR_LIBRARY_FAILURE);
  }

  Result VerifyECDSASignedData(Input, DigestAlgorithm, Input, Input) override {
    ADD_FAILURE();
    return NotReached("VerifyECDSASignedData should not be called",
                      Result::FATAL_ERROR_LIBRARY_FAILURE);
  }

  Result VerifyMLDSASignedData(Input, Input, Input) override {
    ADD_FAILURE();
    return NotReached("VerifyMLDSASignedData should not be called",
                      Result::FATAL_ERROR_LIBRARY_FAILURE);
  }

  Result CheckRSAPublicKeyModulusSizeInBits(EndEntityOrCA,
                                            unsigned int) override {
    ADD_FAILURE();
    return NotReached("CheckRSAPublicKeyModulusSizeInBits should not be called",
                      Result::FATAL_ERROR_LIBRARY_FAILURE);
  }

  Result VerifyRSAPKCS1SignedData(Input, DigestAlgorithm, Input,
                                  Input) override {
    ADD_FAILURE();
    return NotReached("VerifyRSAPKCS1SignedData should not be called",
                      Result::FATAL_ERROR_LIBRARY_FAILURE);
  }

  Result VerifyRSAPSSSignedData(Input, DigestAlgorithm, Input, Input) override {
    ADD_FAILURE();
    return NotReached("VerifyRSAPSSSignedData should not be called",
                      Result::FATAL_ERROR_LIBRARY_FAILURE);
  }

  Result CheckValidityIsAcceptable(Time, Time, EndEntityOrCA,
                                   KeyPurposeId) override {
    ADD_FAILURE();
    return NotReached("CheckValidityIsAcceptable should not be called",
                      Result::FATAL_ERROR_LIBRARY_FAILURE);
  }

  virtual void NoteAuxiliaryExtension(AuxiliaryExtension, Input) override {
    ADD_FAILURE();
  }
};

class DefaultCryptoTrustDomain : public EverythingFailsByDefaultTrustDomain {
  Result DigestBuf(Input item, DigestAlgorithm digestAlg,
                   /*out*/ uint8_t* digestBuf, size_t digestBufLen) override {
    return TestDigestBuf(item, digestAlg, digestBuf, digestBufLen);
  }

  Result CheckSignatureDigestAlgorithm(DigestAlgorithm, EndEntityOrCA,
                                       Time) override {
    return Success;
  }

  Result CheckECDSACurveIsAcceptable(EndEntityOrCA, NamedCurve) override {
    return Success;
  }

  Result VerifyECDSASignedData(Input data, DigestAlgorithm digestAlgorithm,
                               Input signature,
                               Input subjectPublicKeyInfo) override {
    return TestVerifyECDSASignedData(data, digestAlgorithm, signature,
                                     subjectPublicKeyInfo);
  }

  Result CheckRSAPublicKeyModulusSizeInBits(EndEntityOrCA,
                                            unsigned int) override {
    return Success;
  }

  Result VerifyRSAPKCS1SignedData(Input data, DigestAlgorithm digestAlgorithm,
                                  Input signature,
                                  Input subjectPublicKeyInfo) override {
    return TestVerifyRSAPKCS1SignedData(data, digestAlgorithm, signature,
                                        subjectPublicKeyInfo);
  }

  Result CheckValidityIsAcceptable(Time, Time, EndEntityOrCA,
                                   KeyPurposeId) override {
    return Success;
  }

  void NoteAuxiliaryExtension(AuxiliaryExtension, Input) override {}
};

class DefaultNameMatchingPolicy : public NameMatchingPolicy {
 public:
  virtual Result FallBackToCommonName(
      Time,
      /*out*/ FallBackToSearchWithinSubject& fallBackToCommonName) override {
    fallBackToCommonName = FallBackToSearchWithinSubject::Yes;
    return Success;
  }

  virtual HandleInvalidSubjectAlternativeNamesBy
  HandleInvalidSubjectAlternativeNames() override {
    return HandleInvalidSubjectAlternativeNamesBy::Halting;
  }
};

// python DottedOIDToCode.py --tlv id-kp-clientAuth 1.3.6.1.5.5.7.3.2
const uint8_t tlv_id_kp_clientAuth[] = {0x06, 0x08, 0x2b, 0x06, 0x01,
                                        0x05, 0x05, 0x07, 0x03, 0x02};

// python DottedOIDToCode.py --tlv id-kp-codeSigning 1.3.6.1.5.5.7.3.3
const uint8_t tlv_id_kp_codeSigning[] = {0x06, 0x08, 0x2b, 0x06, 0x01,
                                         0x05, 0x05, 0x07, 0x03, 0x03};

// python DottedOIDToCode.py --tlv id-ce-extKeyUsage 2.5.29.37
const uint8_t tlv_id_ce_extKeyUsage[] = {0x06, 0x03, 0x55, 0x1d, 0x25};

// python DottedOIDToCode.py --tlv id-kp-documentSigning 1.3.6.1.5.5.7.3.36
static const uint8_t tlv_id_kp_documentSigning[] = {
    0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x24};

// python DottedOIDToCode.py --tlv
// id-kp-documentSigningAdobe 1.2.840.113583.1.1.5
static const uint8_t tlv_id_kp_documentSigningAdobe[] = {
    0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x2f, 0x01, 0x01, 0x05};

// python DottedOIDToCode.py --tlv
// id-kp-documentSigningMicrosoft 1.3.6.1.4.1.311.10.3.12
static const uint8_t tlv_id_kp_documentSigningMicrosoft[] = {
    0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x0a, 0x03, 0x0c};

inline ByteString CreateEKUExtension(ByteString ekuOIDs) {
  return TLV(der::SEQUENCE,
             BytesToByteString(tlv_id_ce_extKeyUsage) +
                 TLV(der::OCTET_STRING, TLV(der::SEQUENCE, ekuOIDs)));
}

}  // namespace test
}  // namespace pkix
}  // namespace mozilla

#endif  // mozilla_pkix_pkixgtest_h
