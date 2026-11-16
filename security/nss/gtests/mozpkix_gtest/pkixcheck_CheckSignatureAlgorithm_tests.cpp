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

#include "pkixgtest.h"

#include "mozpkix/pkixder.h"

using namespace mozilla::pkix;
using namespace mozilla::pkix::test;

namespace mozilla { namespace pkix {

extern Result CheckSignatureAlgorithm(
                TrustDomain& trustDomain, EndEntityOrCA endEntityOrCA,
                Time notBefore,
                const der::SignedDataWithSignature& signedData,
                Input signatureValue);

} } // namespace mozilla::pkix

struct CheckSignatureAlgorithmTestParams
{
  ByteString signatureAlgorithmValue;
  ByteString signatureValue;
  unsigned int signatureLengthInBytes;
  Result expectedResult;
};

::std::ostream& operator<<(::std::ostream& os,
                           const CheckSignatureAlgorithmTestParams&)
{
  return os << "TODO (bug 1318770)";
}

#define BS(s) ByteString(s, MOZILLA_PKIX_ARRAY_LENGTH(s))

// python DottedOIDToCode.py --tlv sha256WithRSAEncryption 1.2.840.113549.1.1.11
static const uint8_t tlv_sha256WithRSAEncryption[] = {
  0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b
};

// Same as tlv_sha256WithRSAEncryption, except one without the "0x0b" and with
// the DER length decreased accordingly.
static const uint8_t tlv_sha256WithRSAEncryption_truncated[] = {
  0x06, 0x08, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01
};

// python DottedOIDToCode.py --tlv sha-1WithRSAEncryption 1.2.840.113549.1.1.5
static const uint8_t tlv_sha_1WithRSAEncryption[] = {
  0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05
};

// python DottedOIDToCode.py --tlv sha1WithRSASignature 1.3.14.3.2.29
static const uint8_t tlv_sha1WithRSASignature[] = {
  0x06, 0x05, 0x2b, 0x0e, 0x03, 0x02, 0x1d
};

// python DottedOIDToCode.py --tlv md5WithRSAEncryption 1.2.840.113549.1.1.4
static const uint8_t tlv_md5WithRSAEncryption[] = {
  0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x04
};

// CA/B Forum BR 1.8.1 Section 7.1.3.2.1
// Params for RSA-PSS with SHA-256, MGF-1 with SHA-256, and a salt length
// of 32 bytes:
static const uint8_t rsaPSSWithSHA256[] = {
  0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0a, 0x30,
  0x34, 0xa0, 0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
  0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0xa1, 0x1c, 0x30, 0x1a, 0x06, 0x09,
  0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30, 0x0d, 0x06,
  0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00,
  0xa2, 0x03, 0x02, 0x01, 0x20
};

// python DottedOIDToCode.py --tlv id-ml-dsa-44 2.16.840.1.101.3.4.3.17
static const uint8_t tlv_id_ml_dsa_44[] = {
  0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x11
};

// python DottedOIDToCode.py --tlv id-ml-dsa-65 2.16.840.1.101.3.4.3.18
static const uint8_t tlv_id_ml_dsa_65[] = {
  0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x12
};

// python DottedOIDToCode.py --tlv id-ml-dsa-87 2.16.840.1.101.3.4.3.19
static const uint8_t tlv_id_ml_dsa_87[] = {
  0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x13
};

static const CheckSignatureAlgorithmTestParams
  CHECKSIGNATUREALGORITHM_TEST_PARAMS[] =
{
  { // Both algorithm IDs are empty
    ByteString(),
    ByteString(),
    2048 / 8,
    Result::ERROR_BAD_DER,
  },
  { // signatureAlgorithm is empty, signature is supported.
    ByteString(),
    BS(tlv_sha256WithRSAEncryption),
    2048 / 8,
    Result::ERROR_BAD_DER,
  },
  { // signatureAlgorithm is supported, signature is empty.
    BS(tlv_sha256WithRSAEncryption),
    ByteString(),
    2048 / 8,
    Result::ERROR_BAD_DER,
  },
  { // Algorithms match, both are supported.
    BS(tlv_sha256WithRSAEncryption),
    BS(tlv_sha256WithRSAEncryption),
    2048 / 8,
    Success
  },
  { // Algorithms do not match because signatureAlgorithm is truncated.
    BS(tlv_sha256WithRSAEncryption_truncated),
    BS(tlv_sha256WithRSAEncryption),
    2048 / 8,
    Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED
  },
  { // Algorithms do not match because signature is truncated.
    BS(tlv_sha256WithRSAEncryption),
    BS(tlv_sha256WithRSAEncryption_truncated),
    2048 / 8,
    Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED
  },
  { // Algorithms do not match, both are supported.
    BS(tlv_sha_1WithRSAEncryption),
    BS(tlv_sha256WithRSAEncryption),
    2048 / 8,
    Result::ERROR_SIGNATURE_ALGORITHM_MISMATCH,
  },
  { // Algorithms do not match, both are supported.
    BS(tlv_sha256WithRSAEncryption),
    BS(tlv_sha_1WithRSAEncryption),
    2048 / 8,
    Result::ERROR_SIGNATURE_ALGORITHM_MISMATCH,
  },
  { // Algorithms match, both are unsupported.
    BS(tlv_md5WithRSAEncryption),
    BS(tlv_md5WithRSAEncryption),
    2048 / 8,
    Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED
  },
  { // signatureAlgorithm is unsupported, signature is supported.
    BS(tlv_md5WithRSAEncryption),
    BS(tlv_sha256WithRSAEncryption),
    2048 / 8,
    Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED
  },
  { // signatureAlgorithm is supported, signature is unsupported.
    BS(tlv_sha256WithRSAEncryption),
    BS(tlv_md5WithRSAEncryption),
    2048 / 8,
    Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED
  },
  { // Both have the optional NULL parameter.
    BS(tlv_sha256WithRSAEncryption) + TLV(der::NULLTag, ByteString()),
    BS(tlv_sha256WithRSAEncryption) + TLV(der::NULLTag, ByteString()),
    2048 / 8,
    Success
  },
  { // signatureAlgorithm has the optional NULL parameter, signature doesn't.
    BS(tlv_sha256WithRSAEncryption) + TLV(der::NULLTag, ByteString()),
    BS(tlv_sha256WithRSAEncryption),
    2048 / 8,
    Success
  },
  { // signatureAlgorithm does not have the optional NULL parameter, signature
    // does.
    BS(tlv_sha256WithRSAEncryption),
    BS(tlv_sha256WithRSAEncryption) + TLV(der::NULLTag, ByteString()),
    2048 / 8,
    Success
  },
  { // The different OIDs for RSA-with-SHA1 we support are semantically
    // equivalent.
    BS(tlv_sha1WithRSASignature),
    BS(tlv_sha_1WithRSAEncryption),
    2048 / 8,
    Success,
  },
  { // The different OIDs for RSA-with-SHA1 we support are semantically
    // equivalent (opposite order).
    BS(tlv_sha_1WithRSAEncryption),
    BS(tlv_sha1WithRSASignature),
    2048 / 8,
    Success,
  },
  { // Algorithms match, both are supported, key size is not a multile of 128
    // bits. This test verifies that we're not wrongly rounding up the
    // signature size like we did in the original patch for bug 1131767.
    BS(tlv_sha256WithRSAEncryption),
    BS(tlv_sha256WithRSAEncryption),
    (2048 / 8) - 1,
    Success
  },
  {
    // signatureAlgorithm and signature are RSA-PSS
    BS(rsaPSSWithSHA256),
    BS(rsaPSSWithSHA256),
    2048 / 8,
    Success
  },
  {
    // signatureAlgorithm is RSA-PSS, signature is RSA PKCS#1v1.5
    BS(rsaPSSWithSHA256),
    BS(tlv_sha256WithRSAEncryption),
    2048 / 8,
    Result::ERROR_SIGNATURE_ALGORITHM_MISMATCH
  },
  { // signatureAlgorithm is ML-DSA, signature is RSA PKCS#1v1.5
    BS(tlv_id_ml_dsa_44),
    BS(tlv_sha256WithRSAEncryption),
    0,
    Result::ERROR_SIGNATURE_ALGORITHM_MISMATCH
  },
  { // signatureAlgorithm is RSA PKCS#1v1.5, signature is ML-DSA
    BS(tlv_sha256WithRSAEncryption),
    BS(tlv_id_ml_dsa_44),
    0,
    Result::ERROR_SIGNATURE_ALGORITHM_MISMATCH
  },
  { // The parameter set is part of the algorithm, so RFC 5280 4.1.1.2's
    // requirement that the two fields name the same algorithm is not met by
    // two different ML-DSA parameter sets.
    BS(tlv_id_ml_dsa_44),
    BS(tlv_id_ml_dsa_87),
    0,
    Result::ERROR_SIGNATURE_ALGORITHM_MISMATCH
  },
  { // Different ML-DSA parameter sets (opposite order).
    BS(tlv_id_ml_dsa_87),
    BS(tlv_id_ml_dsa_44),
    0,
    Result::ERROR_SIGNATURE_ALGORITHM_MISMATCH
  },
};

class pkixcheck_CheckSignatureAlgorithm
  : public ::testing::Test
  , public ::testing::WithParamInterface<CheckSignatureAlgorithmTestParams>
{
};

class pkixcheck_CheckSignatureAlgorithm_TrustDomain final
  : public EverythingFailsByDefaultTrustDomain
{
public:
  explicit pkixcheck_CheckSignatureAlgorithm_TrustDomain(
             unsigned int aPublicKeySizeInBits)
    : publicKeySizeInBits(aPublicKeySizeInBits)
    , checkedDigestAlgorithm(false)
    , checkedModulusSizeInBits(false)
  {
  }

  Result CheckSignatureDigestAlgorithm(DigestAlgorithm, EndEntityOrCA, Time)
    override
  {
    checkedDigestAlgorithm = true;
    return Success;
  }

  Result CheckRSAPublicKeyModulusSizeInBits(EndEntityOrCA endEntityOrCA,
                                            unsigned int modulusSizeInBits)
    override
  {
    EXPECT_EQ(EndEntityOrCA::MustBeEndEntity, endEntityOrCA);
    EXPECT_EQ(publicKeySizeInBits, modulusSizeInBits);
    checkedModulusSizeInBits = true;
    return Success;
  }

  const unsigned int publicKeySizeInBits;
  bool checkedDigestAlgorithm;
  bool checkedModulusSizeInBits;
};

TEST_P(pkixcheck_CheckSignatureAlgorithm, CheckSignatureAlgorithm)
{
  const Time now(Now());
  const CheckSignatureAlgorithmTestParams& params(GetParam());

  Input signatureValueInput;
  ASSERT_EQ(Success,
            signatureValueInput.Init(params.signatureValue.data(),
                                     params.signatureValue.length()));

  pkixcheck_CheckSignatureAlgorithm_TrustDomain
    trustDomain(params.signatureLengthInBytes * 8);

  der::SignedDataWithSignature signedData;
  ASSERT_EQ(Success,
            signedData.algorithm.Init(params.signatureAlgorithmValue.data(),
                                      params.signatureAlgorithmValue.length()));

  ByteString dummySignature(params.signatureLengthInBytes, 0xDE);
  ASSERT_EQ(Success,
            signedData.signature.Init(dummySignature.data(),
                                      dummySignature.length()));

  ASSERT_EQ(params.expectedResult,
            CheckSignatureAlgorithm(trustDomain, EndEntityOrCA::MustBeEndEntity,
                                    now, signedData, signatureValueInput));
  ASSERT_EQ(params.expectedResult == Success,
            trustDomain.checkedDigestAlgorithm);
  ASSERT_EQ(params.expectedResult == Success,
            trustDomain.checkedModulusSizeInBits);
}

INSTANTIATE_TEST_SUITE_P(
  pkixcheck_CheckSignatureAlgorithm, pkixcheck_CheckSignatureAlgorithm,
  testing::ValuesIn(CHECKSIGNATUREALGORITHM_TEST_PARAMS));

namespace {

struct MLDSATestOutcome
{
  Result result;
  bool checkedDigestAlgorithm;
  bool checkedModulusSizeInBits;
};

MLDSATestOutcome CheckMLDSASignatureAlgorithm(
                   const ByteString& outerSignatureAlgorithm,
                   const ByteString& tbsSignatureAlgorithm)
{
  pkixcheck_CheckSignatureAlgorithm_TrustDomain trustDomain(0);

  Input tbsSignatureAlgorithmInput;
  EXPECT_EQ(Success,
            tbsSignatureAlgorithmInput.Init(tbsSignatureAlgorithm.data(),
                                            tbsSignatureAlgorithm.length()));

  der::SignedDataWithSignature signedData;
  EXPECT_EQ(Success,
            signedData.algorithm.Init(outerSignatureAlgorithm.data(),
                                      outerSignatureAlgorithm.length()));

  MLDSATestOutcome outcome;
  outcome.result =
    CheckSignatureAlgorithm(trustDomain, EndEntityOrCA::MustBeEndEntity, Now(),
                            signedData, tbsSignatureAlgorithmInput);
  outcome.checkedDigestAlgorithm = trustDomain.checkedDigestAlgorithm;
  outcome.checkedModulusSizeInBits = trustDomain.checkedModulusSizeInBits;
  return outcome;
}

}  // namespace

TEST_F(pkixcheck_CheckSignatureAlgorithm, MLDSAMatchingAlgorithms)
{
  static const ByteString kAlgorithms[] = {
    BS(tlv_id_ml_dsa_44), BS(tlv_id_ml_dsa_65), BS(tlv_id_ml_dsa_87)
  };
  for (const ByteString& algorithm : kAlgorithms) {
    MLDSATestOutcome outcome(
      CheckMLDSASignatureAlgorithm(algorithm, algorithm));
    EXPECT_EQ(Success, outcome.result);
    EXPECT_FALSE(outcome.checkedDigestAlgorithm);
    EXPECT_FALSE(outcome.checkedModulusSizeInBits);
  }
}

// RFC 9881 Section 2 says that the parameters MUST be absent, so unlike for
// the other algorithms whose parameters must be omitted, an explicitly encoded
// NULL is rejected along with anything else, in either field.
TEST_F(pkixcheck_CheckSignatureAlgorithm, MLDSAParametersMustBeAbsent)
{
  const ByteString withoutParams(BS(tlv_id_ml_dsa_65));
  static const ByteString kParameters[] = {
    TLV(der::NULLTag, ByteString()),
    TLV(der::INTEGER, ByteString(1, 0x00)),
  };
  for (const ByteString& parameters : kParameters) {
    const ByteString withParams(withoutParams + parameters);
    EXPECT_EQ(Result::ERROR_BAD_DER,
              CheckMLDSASignatureAlgorithm(withParams, withoutParams).result);
    EXPECT_EQ(Result::ERROR_BAD_DER,
              CheckMLDSASignatureAlgorithm(withoutParams, withParams).result);
    EXPECT_EQ(Result::ERROR_BAD_DER,
              CheckMLDSASignatureAlgorithm(withParams, withParams).result);
  }
}

class pkixcheck_CheckSignatureAlgorithm_BuildCertChain_TrustDomain
  : public DefaultCryptoTrustDomain
{
public:
  explicit pkixcheck_CheckSignatureAlgorithm_BuildCertChain_TrustDomain(
             const ByteString& aIssuer)
    : issuer(aIssuer)
  {
  }

  Result GetCertTrust(EndEntityOrCA, const CertPolicyId&,
                      Input cert, /*out*/ TrustLevel& trustLevel) override
  {
    trustLevel = InputEqualsByteString(cert, issuer)
               ? TrustLevel::TrustAnchor
               : TrustLevel::InheritsTrust;
    return Success;
  }

  Result FindIssuer(Input, IssuerChecker& checker, Time) override
  {
    EXPECT_FALSE(ENCODING_FAILED(issuer));

    Input issuerInput;
    EXPECT_EQ(Success, issuerInput.Init(issuer.data(), issuer.length()));

    bool keepGoing;
    EXPECT_EQ(Success, checker.Check(issuerInput, nullptr, keepGoing));
    EXPECT_FALSE(keepGoing);

    return Success;
  }

  Result CheckRevocation(EndEntityOrCA, const CertID&, Time, Duration,
                         /*optional*/ const Input*,
                         /*optional*/ const Input*) override
  {
    return Success;
  }

  Result IsChainValid(const DERArray&, Time, const CertPolicyId&) override
  {
    return Success;
  }

  ByteString issuer;
};

// Test that CheckSignatureAlgorithm actually gets called at some point when
// BuildCertChain is called.
TEST_F(pkixcheck_CheckSignatureAlgorithm, BuildCertChain)
{
  ScopedTestKeyPair keyPair(CloneReusedKeyPair());
  ASSERT_TRUE(keyPair.get());

  ByteString issuerExtensions[2];
  issuerExtensions[0] = CreateEncodedBasicConstraints(true, nullptr,
                                                      Critical::No);
  ASSERT_FALSE(ENCODING_FAILED(issuerExtensions[0]));

  ByteString issuer(CreateEncodedCertificate(3,
                                             sha256WithRSAEncryption(),
                                             CreateEncodedSerialNumber(1),
                                             CNToDERName("issuer"),
                                             oneDayBeforeNow, oneDayAfterNow,
                                             CNToDERName("issuer"),
                                             *keyPair,
                                             issuerExtensions,
                                             *keyPair,
                                             sha256WithRSAEncryption()));
  ASSERT_FALSE(ENCODING_FAILED(issuer));

  ByteString subject(CreateEncodedCertificate(3,
                                              sha1WithRSAEncryption(),
                                              CreateEncodedSerialNumber(2),
                                              CNToDERName("issuer"),
                                              oneDayBeforeNow, oneDayAfterNow,
                                              CNToDERName("subject"),
                                              *keyPair,
                                              nullptr,
                                              *keyPair,
                                              sha256WithRSAEncryption()));
  ASSERT_FALSE(ENCODING_FAILED(subject));

  Input subjectInput;
  ASSERT_EQ(Success, subjectInput.Init(subject.data(), subject.length()));
  pkixcheck_CheckSignatureAlgorithm_BuildCertChain_TrustDomain
    trustDomain(issuer);
  Result rv = BuildCertChain(trustDomain, subjectInput, Now(),
                             EndEntityOrCA::MustBeEndEntity,
                             KeyUsage::noParticularKeyUsageRequired,
                             KeyPurposeId::anyExtendedKeyUsage,
                             CertPolicyId::anyPolicy,
                             nullptr);
  ASSERT_EQ(Result::ERROR_SIGNATURE_ALGORITHM_MISMATCH, rv);
}
