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

#include <functional>
#include <optional>
#include <vector>

#include "pkixgtest.h"

#include "mozpkix/pkixtypes.h"
#include "mozpkix/pkixder.h"

using namespace mozilla::pkix;
using namespace mozilla::pkix::der;
using namespace mozilla::pkix::test;

class pkixder_pki_types_tests : public ::testing::Test { };

TEST_F(pkixder_pki_types_tests, CertificateSerialNumber)
{
  const uint8_t DER_CERT_SERIAL[] = {
    0x02,                       // INTEGER
    8,                          // length
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef
  };
  Input input(DER_CERT_SERIAL);
  Reader reader(input);

  Input item;
  ASSERT_EQ(Success, CertificateSerialNumber(reader, item));

  Input expected;
  ASSERT_EQ(Success,
            expected.Init(DER_CERT_SERIAL + 2, sizeof DER_CERT_SERIAL - 2));
  ASSERT_TRUE(InputsAreEqual(expected, item));
}

TEST_F(pkixder_pki_types_tests, CertificateSerialNumberLongest)
{
  const uint8_t DER_CERT_SERIAL_LONGEST[] = {
    0x02,                       // INTEGER
    20,                         // length
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
  };
  Input input(DER_CERT_SERIAL_LONGEST);
  Reader reader(input);

  Input item;
  ASSERT_EQ(Success, CertificateSerialNumber(reader, item));

  Input expected;
  ASSERT_EQ(Success,
            expected.Init(DER_CERT_SERIAL_LONGEST + 2,
                          sizeof DER_CERT_SERIAL_LONGEST - 2));
  ASSERT_TRUE(InputsAreEqual(expected, item));
}

TEST_F(pkixder_pki_types_tests, CertificateSerialNumberCrazyLong)
{
  const uint8_t DER_CERT_SERIAL_CRAZY_LONG[] = {
    0x02,                       // INTEGER
    32,                         // length
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
    17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32
  };
  Input input(DER_CERT_SERIAL_CRAZY_LONG);
  Reader reader(input);

  Input item;
  ASSERT_EQ(Success, CertificateSerialNumber(reader, item));
}

TEST_F(pkixder_pki_types_tests, CertificateSerialNumberZeroLength)
{
  const uint8_t DER_CERT_SERIAL_ZERO_LENGTH[] = {
    0x02,                       // INTEGER
    0x00                        // length
  };
  Input input(DER_CERT_SERIAL_ZERO_LENGTH);
  Reader reader(input);

  Input item;
  ASSERT_EQ(Result::ERROR_INVALID_INTEGER_ENCODING,
            CertificateSerialNumber(reader, item));
}

TEST_F(pkixder_pki_types_tests, OptionalVersionV1ExplicitEncodingAllowed)
{
  const uint8_t DER_OPTIONAL_VERSION_V1[] = {
    0xa0, 0x03,                   // context specific 0
    0x02, 0x01, 0x00              // INTEGER(0)
  };
  Input input(DER_OPTIONAL_VERSION_V1);
  Reader reader(input);

  // XXX(bug 1031093): We shouldn't accept an explicit encoding of v1, but we
  // do here for compatibility reasons.
  // Version version;
  // ASSERT_EQ(Result::ERROR_BAD_DER, OptionalVersion(reader, version));
  der::Version version = der::Version::v3;
  ASSERT_EQ(Success, OptionalVersion(reader, version));
  ASSERT_EQ(der::Version::v1, version);
}

TEST_F(pkixder_pki_types_tests, OptionalVersionV2)
{
  const uint8_t DER_OPTIONAL_VERSION_V2[] = {
    0xa0, 0x03,                   // context specific 0
    0x02, 0x01, 0x01              // INTEGER(1)
  };
  Input input(DER_OPTIONAL_VERSION_V2);
  Reader reader(input);

  der::Version version = der::Version::v1;
  ASSERT_EQ(Success, OptionalVersion(reader, version));
  ASSERT_EQ(der::Version::v2, version);
}

TEST_F(pkixder_pki_types_tests, OptionalVersionV3)
{
  const uint8_t DER_OPTIONAL_VERSION_V3[] = {
    0xa0, 0x03,                   // context specific 0
    0x02, 0x01, 0x02              // INTEGER(2)
  };
  Input input(DER_OPTIONAL_VERSION_V3);
  Reader reader(input);

  der::Version version = der::Version::v1;
  ASSERT_EQ(Success, OptionalVersion(reader, version));
  ASSERT_EQ(der::Version::v3, version);
}

TEST_F(pkixder_pki_types_tests, OptionalVersionUnknown)
{
  const uint8_t DER_OPTIONAL_VERSION_INVALID[] = {
    0xa0, 0x03,                   // context specific 0
    0x02, 0x01, 0x42              // INTEGER(0x42)
  };
  Input input(DER_OPTIONAL_VERSION_INVALID);
  Reader reader(input);

  der::Version version = der::Version::v1;
  ASSERT_EQ(Result::ERROR_BAD_DER, OptionalVersion(reader, version));
}

TEST_F(pkixder_pki_types_tests, OptionalVersionInvalidTooLong)
{
  const uint8_t DER_OPTIONAL_VERSION_INVALID_TOO_LONG[] = {
    0xa0, 0x03,                   // context specific 0
    0x02, 0x02, 0x12, 0x34        // INTEGER(0x1234)
  };
  Input input(DER_OPTIONAL_VERSION_INVALID_TOO_LONG);
  Reader reader(input);

  der::Version version;
  ASSERT_EQ(Result::ERROR_BAD_DER, OptionalVersion(reader, version));
}

TEST_F(pkixder_pki_types_tests, OptionalVersionMissing)
{
  const uint8_t DER_OPTIONAL_VERSION_MISSING[] = {
    0x02, 0x11, 0x22              // INTEGER
  };
  Input input(DER_OPTIONAL_VERSION_MISSING);
  Reader reader(input);

  der::Version version = der::Version::v3;
  ASSERT_EQ(Success, OptionalVersion(reader, version));
  ASSERT_EQ(der::Version::v1, version);
}

static const size_t MAX_ALGORITHM_OID_DER_LENGTH = 65;

struct InvalidAlgorithmIdentifierTestInfo
{
  uint8_t der[MAX_ALGORITHM_OID_DER_LENGTH];
  size_t derLength;
};

struct ValidDigestAlgorithmIdentifierTestInfo
{
  DigestAlgorithm algorithm;
  uint8_t der[MAX_ALGORITHM_OID_DER_LENGTH];
  size_t derLength;
};

class pkixder_DigestAlgorithmIdentifier_Valid
  : public ::testing::Test
  , public ::testing::WithParamInterface<ValidDigestAlgorithmIdentifierTestInfo>
{
};

static const ValidDigestAlgorithmIdentifierTestInfo
  VALID_DIGEST_ALGORITHM_TEST_INFO[] =
{
  { DigestAlgorithm::sha512,
    { 0x30, 0x0b, 0x06, 0x09,
      0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03 },
    13
  },
  { DigestAlgorithm::sha384,
    { 0x30, 0x0b, 0x06, 0x09,
      0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02 },
    13
  },
  { DigestAlgorithm::sha256,
    { 0x30, 0x0b, 0x06, 0x09,
      0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01 },
    13
  },
  { DigestAlgorithm::sha1,
    { 0x30, 0x07, 0x06, 0x05,
      0x2b, 0x0e, 0x03, 0x02, 0x1a },
    9
  },
};

TEST_P(pkixder_DigestAlgorithmIdentifier_Valid, Valid)
{
  const ValidDigestAlgorithmIdentifierTestInfo& param(GetParam());

  {
    Input input;
    ASSERT_EQ(Success, input.Init(param.der, param.derLength));
    Reader reader(input);
    DigestAlgorithm alg;
    ASSERT_EQ(Success, DigestAlgorithmIdentifier(reader, alg));
    ASSERT_EQ(param.algorithm, alg);
    ASSERT_EQ(Success, End(reader));
  }

  {
    uint8_t derWithNullParam[MAX_ALGORITHM_OID_DER_LENGTH + 2];
    memcpy(derWithNullParam, param.der, param.derLength);
    derWithNullParam[1] += 2; // we're going to expand the value by 2 bytes
    derWithNullParam[param.derLength] = 0x05; // NULL tag
    derWithNullParam[param.derLength + 1] = 0x00; // length zero

    Input input;
    ASSERT_EQ(Success, input.Init(derWithNullParam, param.derLength + 2));
    Reader reader(input);
    DigestAlgorithm alg;
    ASSERT_EQ(Success, DigestAlgorithmIdentifier(reader, alg));
    ASSERT_EQ(param.algorithm, alg);
    ASSERT_EQ(Success, End(reader));
  }
}

INSTANTIATE_TEST_SUITE_P(pkixder_DigestAlgorithmIdentifier_Valid,
                        pkixder_DigestAlgorithmIdentifier_Valid,
                        testing::ValuesIn(VALID_DIGEST_ALGORITHM_TEST_INFO));

class pkixder_DigestAlgorithmIdentifier_Invalid
  : public ::testing::Test
  , public ::testing::WithParamInterface<InvalidAlgorithmIdentifierTestInfo>
{
};

static const InvalidAlgorithmIdentifierTestInfo
  INVALID_DIGEST_ALGORITHM_TEST_INFO[] =
{
  { // MD5
    { 0x30, 0x0a, 0x06, 0x08,
      0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x05 },
    12,
  },
  { // ecdsa-with-SHA256 (1.2.840.10045.4.3.2) (not a hash algorithm)
    { 0x30, 0x0a, 0x06, 0x08,
      0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02 },
    12,
  },
};

TEST_P(pkixder_DigestAlgorithmIdentifier_Invalid, Invalid)
{
  const InvalidAlgorithmIdentifierTestInfo& param(GetParam());
  Input input;
  ASSERT_EQ(Success, input.Init(param.der, param.derLength));
  Reader reader(input);
  DigestAlgorithm alg;
  ASSERT_EQ(Result::ERROR_INVALID_ALGORITHM,
            DigestAlgorithmIdentifier(reader, alg));
}

INSTANTIATE_TEST_SUITE_P(pkixder_DigestAlgorithmIdentifier_Invalid,
                        pkixder_DigestAlgorithmIdentifier_Invalid,
                        testing::ValuesIn(INVALID_DIGEST_ALGORITHM_TEST_INFO));

struct ValidSignatureAlgorithmIdentifierValueTestInfo
{
  PublicKeyAlgorithm publicKeyAlg;
  // std::nullopt for signature algorithms that have no pre-hash step (i.e.
  // pure ML-DSA).
  std::optional<DigestAlgorithm> digestAlg;
  uint8_t der[MAX_ALGORITHM_OID_DER_LENGTH];
  size_t derLength;
  // The result of SignatureAlgorithmIdentifierValue when the parameters are
  // an explicitly encoded NULL.
  Result explicitNullResult;
};

static const ValidSignatureAlgorithmIdentifierValueTestInfo
  VALID_SIGNATURE_ALGORITHM_VALUE_TEST_INFO[] =
{
  // ECDSA
  { PublicKeyAlgorithm::ECDSA,
    DigestAlgorithm::sha512,
    { 0x06, 0x08,
      0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x04 },
    10,
    Success,
  },
  { PublicKeyAlgorithm::ECDSA,
    DigestAlgorithm::sha384,
    { 0x06, 0x08,
      0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x03 },
    10,
    Success,
  },
  { PublicKeyAlgorithm::ECDSA,
    DigestAlgorithm::sha256,
    { 0x06, 0x08,
      0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03, 0x02 },
    10,
    Success,
  },
  { PublicKeyAlgorithm::ECDSA,
    DigestAlgorithm::sha1,
    { 0x06, 0x07,
      0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x01 },
    9,
    Success,
  },

  // RSA PKCS#1 1.5
  { PublicKeyAlgorithm::RSA_PKCS1,
    DigestAlgorithm::sha512,
    { 0x06, 0x09,
      0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0d },
    11,
    Success,
  },
  { PublicKeyAlgorithm::RSA_PKCS1,
    DigestAlgorithm::sha384,
    { 0x06, 0x09,
      0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0c },
    11,
    Success,
  },
  { PublicKeyAlgorithm::RSA_PKCS1,
    DigestAlgorithm::sha256,
    { 0x06, 0x09,
      0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b },
    11,
    Success,
  },
  { PublicKeyAlgorithm::RSA_PKCS1,
    DigestAlgorithm::sha1,
    // IETF Standard OID
    { 0x06, 0x09,
      0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05 },
    11,
    Success,
  },
  { PublicKeyAlgorithm::RSA_PKCS1,
    DigestAlgorithm::sha1,
    // Legacy OIW OID (bug 1042479)
    { 0x06, 0x05,
      0x2b, 0x0e, 0x03, 0x02, 0x1d },
    7,
    Success,
  },

  // RSA-PSS
  { PublicKeyAlgorithm::RSA_PSS,
    DigestAlgorithm::sha256,
    { 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0a, 0x30,
      0x34, 0xa0, 0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
      0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0xa1, 0x1c, 0x30, 0x1a, 0x06, 0x09,
      0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30, 0x0d, 0x06,
      0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00,
      0xa2, 0x03, 0x02, 0x01, 0x20  },
    65,
    Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED,
  },

  { PublicKeyAlgorithm::RSA_PSS,
    DigestAlgorithm::sha384,
    { 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0a, 0x30,
      0x34, 0xa0, 0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
      0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0xa1, 0x1c, 0x30, 0x1a, 0x06, 0x09,
      0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30, 0x0d, 0x06,
      0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00,
      0xa2, 0x03, 0x02, 0x01, 0x30 },
    65,
    Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED,
  },

  { PublicKeyAlgorithm::RSA_PSS,
    DigestAlgorithm::sha512,
    { 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0a, 0x30,
      0x34, 0xa0, 0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
      0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0xa1, 0x1c, 0x30, 0x1a, 0x06, 0x09,
      0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30, 0x0d, 0x06,
      0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00,
      0xa2, 0x03, 0x02, 0x01, 0x40 },
    65,
    Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED,
  },

  // ML-DSA. These are "pure" (non-prehash) signature algorithms, so there is
  // no digest algorithm to report. RFC 9881 Section 2 says that the parameters
  // MUST be absent, so unlike for the other algorithms, an explicitly encoded
  // NULL is rejected.
  // python DottedOIDToCode.py --tlv id-ml-dsa-44 2.16.840.1.101.3.4.3.17
  { PublicKeyAlgorithm::MLDSA_44,
    std::nullopt,
    { 0x06, 0x09,
      0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x11 },
    11,
    Result::ERROR_BAD_DER,
  },
  // python DottedOIDToCode.py --tlv id-ml-dsa-65 2.16.840.1.101.3.4.3.18
  { PublicKeyAlgorithm::MLDSA_65,
    std::nullopt,
    { 0x06, 0x09,
      0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x12 },
    11,
    Result::ERROR_BAD_DER,
  },
  // python DottedOIDToCode.py --tlv id-ml-dsa-87 2.16.840.1.101.3.4.3.19
  { PublicKeyAlgorithm::MLDSA_87,
    std::nullopt,
    { 0x06, 0x09,
      0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x13 },
    11,
    Result::ERROR_BAD_DER,
  },
};

class pkixder_SignatureAlgorithmIdentifierValue_Valid
  : public ::testing::Test
  , public ::testing::WithParamInterface<
             ValidSignatureAlgorithmIdentifierValueTestInfo>
{
};

TEST_P(pkixder_SignatureAlgorithmIdentifierValue_Valid, Valid)
{
  const ValidSignatureAlgorithmIdentifierValueTestInfo& param(GetParam());

  {
    Input input;
    ASSERT_EQ(Success, input.Init(param.der, param.derLength));
    Reader reader(input);
    PublicKeyAlgorithm publicKeyAlg;
    // Not empty, so that the ML-DSA cases check that it is cleared.
    std::optional<DigestAlgorithm> digestAlg(DigestAlgorithm::sha1);
    ASSERT_EQ(Success,
              SignatureAlgorithmIdentifierValue(reader, publicKeyAlg,
                                                digestAlg));
    ASSERT_EQ(param.publicKeyAlg, publicKeyAlg);
    ASSERT_EQ(param.digestAlg.has_value(), digestAlg.has_value());
    if (param.digestAlg.has_value()) {
      ASSERT_EQ(param.digestAlg.value(), digestAlg.value());
    }
    ASSERT_EQ(Success, End(reader));
  }

  uint8_t derWithNullParam[MAX_ALGORITHM_OID_DER_LENGTH + 2];
  memcpy(derWithNullParam, param.der, param.derLength);
  derWithNullParam[param.derLength] = 0x05; // NULL tag
  derWithNullParam[param.derLength + 1] = 0x00; // length zero

  Input input;
  ASSERT_EQ(Success, input.Init(derWithNullParam, param.derLength + 2));
  Reader reader(input);
  PublicKeyAlgorithm publicKeyAlg;
  std::optional<DigestAlgorithm> digestAlg;
  ASSERT_EQ(param.explicitNullResult,
            SignatureAlgorithmIdentifierValue(reader, publicKeyAlg,
                                              digestAlg));
  if (param.explicitNullResult == Success) {
    ASSERT_EQ(param.publicKeyAlg, publicKeyAlg);
    ASSERT_EQ(param.digestAlg.has_value(), digestAlg.has_value());
    if (param.digestAlg.has_value()) {
      ASSERT_EQ(param.digestAlg.value(), digestAlg.value());
    }
    ASSERT_EQ(Success, End(reader));
  }
}

INSTANTIATE_TEST_SUITE_P(
  pkixder_SignatureAlgorithmIdentifierValue_Valid,
  pkixder_SignatureAlgorithmIdentifierValue_Valid,
  testing::ValuesIn(VALID_SIGNATURE_ALGORITHM_VALUE_TEST_INFO));

static const InvalidAlgorithmIdentifierTestInfo
  INVALID_SIGNATURE_ALGORITHM_VALUE_TEST_INFO[] =
{
  // id-dsa-with-sha256 (2.16.840.1.101.3.4.3.2)
  { { 0x06, 0x09,
      0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x02 },
    11,
  },

  // id-dsa-with-sha1 (1.2.840.10040.4.3)
  { { 0x06, 0x07,
      0x2a, 0x86, 0x48, 0xce, 0x38, 0x04, 0x03 },
    9,
  },

  // RSA-with-MD5 (1.2.840.113549.1.1.4)
  { { 0x06, 0x09,
      0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x04 },
    11,
  },

  // id-sha256 (2.16.840.1.101.3.4.2.1). It is invalid because SHA-256 is not
  // a signature algorithm.
  { { 0x06, 0x09,
      0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01 },
    11,
  },

  // RSA-PSS with SHA-256, MGF-1 with SHA-256, and a salt length of 48 bytes
  { { 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0a, 0x30,
      0x34, 0xa0, 0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
      0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0xa1, 0x1c, 0x30, 0x1a, 0x06, 0x09,
      0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30, 0x0d, 0x06,
      0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00,
      0xa2, 0x03, 0x02, 0x01, 0x30 },
    65,
  },

  // RSA-PSS with SHA-512, MGF-1 with SHA-256, and a salt length of 32 bytes
  { { 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0a, 0x30,
      0x34, 0xa0, 0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
      0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0xa1, 0x1c, 0x30, 0x1a, 0x06, 0x09,
      0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30, 0x0d, 0x06,
      0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00,
      0xa2, 0x03, 0x02, 0x01, 0x20 },
    65,
  },

  // RSA-PSS with omitted parameters
  { { 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0a },
    11,
  },

  // RSA-PSS with NULL parameters
  { { 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0a,
      0x05, 0x00 },
    13,
  },

  // The pre-hash variants of ML-DSA are deliberately not supported.
  // id-hash-ml-dsa-44-with-sha512 (2.16.840.1.101.3.4.3.32)
  { { 0x06, 0x09,
      0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x20 },
    11,
  },

  // id-hash-ml-dsa-65-with-sha512 (2.16.840.1.101.3.4.3.33)
  { { 0x06, 0x09,
      0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x21 },
    11,
  },

  // id-hash-ml-dsa-87-with-sha512 (2.16.840.1.101.3.4.3.34)
  { { 0x06, 0x09,
      0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x22 },
    11,
  },
};

class pkixder_SignatureAlgorithmIdentifier_Invalid
  : public ::testing::Test
  , public ::testing::WithParamInterface<InvalidAlgorithmIdentifierTestInfo>
{
};

TEST_P(pkixder_SignatureAlgorithmIdentifier_Invalid, Invalid)
{
  const InvalidAlgorithmIdentifierTestInfo& param(GetParam());
  Input input;
  ASSERT_EQ(Success, input.Init(param.der, param.derLength));
  Reader reader(input);
  der::PublicKeyAlgorithm publicKeyAlg;
  std::optional<DigestAlgorithm> digestAlg;
  ASSERT_EQ(Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED,
            SignatureAlgorithmIdentifierValue(reader, publicKeyAlg, digestAlg));
}

INSTANTIATE_TEST_SUITE_P(
  pkixder_SignatureAlgorithmIdentifier_Invalid,
  pkixder_SignatureAlgorithmIdentifier_Invalid,
  testing::ValuesIn(INVALID_SIGNATURE_ALGORITHM_VALUE_TEST_INFO));

struct EncodedECDSASignatureValidParams {
  std::vector<uint8_t> signature;
  std::vector<uint8_t> rExpected;
  std::vector<uint8_t> sExpected;
};

::std::ostream& operator<<(::std::ostream& os, const EncodedECDSASignatureValidParams&)
{
  return os << "TODO (bug 1318770)";
}

static const EncodedECDSASignatureValidParams
  ENCODEDECDSASIGNATURE_VALID_PARAMS[] =
{
  {
    { 0x30, 0x07, // SEQUENCE
        0x02, 0x01, 0x01, // INTEGER (0x01)
        0x02, 0x02, 0x05, 0x06 }, // INTEGER ([0x05, 0x06])
    { 0x01 },
    { 0x05, 0x06 },
  },
  {
    { 0x30, 0x08, // SEQUENCE
        0x02, 0x03, 0x00, 0xb7, 0x0a, // INTEGER ([0xb7, 0x0a]) highest bit set
        0x02, 0x01, 0x02 }, // INTEGER (0x02)
    { 0xb7, 0x0a },
    { 0x02 },
  },
  {
    { 0x30, 0x09, // SEQUENCE
        0x02, 0x03, 0x23, 0x00, 0x55, // INTEGER ([0x23, 0x00, 0x55])
        0x02, 0x02, 0x00, 0xf0 }, // INTEGER (0xf0) highest bit set
    { 0x23, 0x00, 0x55 },
    { 0xf0 },
  },
  {
    { 0x30, 0x09, // SEQUENCE
        0x02, 0x03, 0x00, 0x93, 0x10, // INTEGER ([0x93, 0xl0]) highest bit set
        0x02, 0x02, 0x00, 0xcf }, // INTEGER (0xcf) highest bit set
    { 0x93, 0x10, },
    { 0xcf },
  },
};

class pkixder_ECDSASigValue_valid
  : public ::testing::Test
  , public ::testing::WithParamInterface<EncodedECDSASignatureValidParams>
{
};


TEST_P(pkixder_ECDSASigValue_valid, pkixder_ECDSASigValue_valid)
{
  const EncodedECDSASignatureValidParams& params(GetParam());

  Input signature;
  ASSERT_EQ(Success,
            signature.Init(params.signature.data(), params.signature.size()));
  Input r;
  Input s;
  ASSERT_EQ(Success, ECDSASigValue(signature, r, s));

  Input rExpected;
  ASSERT_EQ(Success,
            rExpected.Init(params.rExpected.data(), params.rExpected.size()));
  ASSERT_TRUE(InputsAreEqual(r, rExpected));

  Input sExpected;
  ASSERT_EQ(Success,
            sExpected.Init(params.sExpected.data(), params.sExpected.size()));
  ASSERT_TRUE(InputsAreEqual(s, sExpected));
}

INSTANTIATE_TEST_SUITE_P(pkixder_ECDSASigValue_valid,
    pkixder_ECDSASigValue_valid,
    testing::ValuesIn(ENCODEDECDSASIGNATURE_VALID_PARAMS));

struct EncodedECDSASignatureInvalidParams {
  std::vector<uint8_t> signature;
  Result expectedResult;
};

::std::ostream& operator<<(::std::ostream& os, const EncodedECDSASignatureInvalidParams&)
{
  return os << "TODO (bug 1318770)";
}

static const EncodedECDSASignatureInvalidParams
  ENCODEDECDSASIGNATURE_INVALID_PARAMS[] =
{
  {
    { 0x05, 0x00 }, // not a SEQUENCE
    Result::ERROR_BAD_DER
  },
  {
    { 0x30, 0x00 }, // empty SEQUENCE
    Result::ERROR_BAD_DER
  },
  {
    { 0x30, 0x06,
        0x05, 0x01, 0x01, // NULL, not INTEGER
        0x02, 0x01, 0x01 },
    Result::ERROR_BAD_DER
  },
  {
    { 0x30, 0x08,
        0x02, 0x01, 0x01,
        0x02, 0x01, 0x01,
        0x05, 0x00 }, // trailing data in SEQUENCE
    Result::ERROR_BAD_DER
  },
  { { 0x30, 0x06,
        0x02, 0x01, 0x01,
        0x02, 0x01, 0x01,
      0x05, 0x00 }, // trailing data after SEQUENCE
    Result::ERROR_BAD_DER
  },
  {
    { 0x30, 0x07,
        0x02, 0x01, 0x00, // not a positive INTEGER
        0x02, 0x02, 0x0f, 0x02 },
    Result::ERROR_INVALID_INTEGER_ENCODING
  },
  {
    { 0x30, 0x08,
        0x02, 0x02, 0x00, 0x01, // unnecessary zero padding
        0x02, 0x02, 0x0f, 0x02 },
    Result::ERROR_INVALID_INTEGER_ENCODING
  },
  {
    { 0x30, 0x07,
        0x02, 0x01, 0x01,
        0x02, 0x02, 0xff, 0x02 }, // negative INTEGER
    Result::ERROR_INVALID_INTEGER_ENCODING
  },
  {
    { 0x30, 0x06,
        0x02, 0x01, 0x01,
        0x02, 0x01, 0xf0 }, // negative INTEGER
    Result::ERROR_INVALID_INTEGER_ENCODING
  },
};

class pkixder_ECDSASigValue_invalid
  : public ::testing::Test
  , public ::testing::WithParamInterface<EncodedECDSASignatureInvalidParams>
{
};


TEST_P(pkixder_ECDSASigValue_invalid, pkixder_ECDSASigValue_invalid)
{
  const EncodedECDSASignatureInvalidParams& params(GetParam());

  Input signature;
  ASSERT_EQ(Success,
            signature.Init(params.signature.data(), params.signature.size()));
  Input r;
  Input s;
  ASSERT_EQ(params.expectedResult, ECDSASigValue(signature, r, s));
}

INSTANTIATE_TEST_SUITE_P(
  pkixder_ECDSASigValue_invalid, pkixder_ECDSASigValue_invalid,
  testing::ValuesIn(ENCODEDECDSASIGNATURE_INVALID_PARAMS));
