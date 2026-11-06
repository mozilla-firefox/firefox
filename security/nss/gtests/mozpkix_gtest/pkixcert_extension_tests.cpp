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

#include "pkixgtest.h"

#include "mozpkix/pkixder.h"
#include "mozpkix/pkixutil.h"
#include "mozpkix/test/pkixtestutil.h"

using namespace mozilla::pkix;
using namespace mozilla::pkix::test;

// Creates a self-signed certificate with the given extension.
static ByteString
CreateCertWithExtensions(const char* subjectCN,
                         const ByteString* extensions)
{
  static long serialNumberValue = 0;
  ++serialNumberValue;
  ByteString serialNumber(CreateEncodedSerialNumber(serialNumberValue));
  EXPECT_FALSE(ENCODING_FAILED(serialNumber));
  ByteString issuerDER(CNToDERName(subjectCN));
  EXPECT_FALSE(ENCODING_FAILED(issuerDER));
  ByteString subjectDER(CNToDERName(subjectCN));
  EXPECT_FALSE(ENCODING_FAILED(subjectDER));
  ScopedTestKeyPair subjectKey(CloneReusedKeyPair());
  return CreateEncodedCertificate(v3, sha256WithRSAEncryption(),
                                  serialNumber, issuerDER,
                                  oneDayBeforeNow, oneDayAfterNow,
                                  subjectDER, *subjectKey, extensions,
                                  *subjectKey,
                                  sha256WithRSAEncryption());
}

// Creates a self-signed certificate with the given extension.
static ByteString
CreateCertWithOneExtension(const char* subjectStr, const ByteString& extension)
{
  const ByteString extensions[] = { extension, ByteString() };
  return CreateCertWithExtensions(subjectStr, extensions);
}

class TrustEverythingTrustDomain final : public DefaultCryptoTrustDomain
{
private:
  Result GetCertTrust(EndEntityOrCA, const CertPolicyId&, Input,
                      /*out*/ TrustLevel& trustLevel) override
  {
    trustLevel = TrustLevel::TrustAnchor;
    return Success;
  }

  Result CheckRevocation(EndEntityOrCA, const CertID&, Time, Duration,
                         /*optional*/ const Input*, /*optional*/ const Input*)
                         override
  {
    return Success;
  }

  Result IsChainValid(const DERArray&, Time, const CertPolicyId&) override
  {
    return Success;
  }
};

// python DottedOIDToCode.py --tlv unknownExtensionOID 1.3.6.1.4.1.13769.666.666.666.1.500.9.3
static const uint8_t tlv_unknownExtensionOID[] = {
  0x06, 0x12, 0x2b, 0x06, 0x01, 0x04, 0x01, 0xeb, 0x49, 0x85, 0x1a, 0x85, 0x1a,
  0x85, 0x1a, 0x01, 0x83, 0x74, 0x09, 0x03
};

// python DottedOIDToCode.py --tlv id-pe-authorityInformationAccess 1.3.6.1.5.5.7.1.1
static const uint8_t tlv_id_pe_authorityInformationAccess[] = {
  0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x01, 0x01
};

// python DottedOIDToCode.py --tlv wrongExtensionOID 1.3.6.6.1.5.5.7.1.1
// (there is an extra "6" that shouldn't be in this OID)
static const uint8_t tlv_wrongExtensionOID[] = {
  0x06, 0x09, 0x2b, 0x06, 0x06, 0x01, 0x05, 0x05, 0x07, 0x01, 0x01
};

// python DottedOIDToCode.py --tlv id-ce-unknown 2.5.29.55
// (this is a made-up OID for testing "id-ce"-prefixed OIDs that mozilla::pkix
// doesn't handle)
static const uint8_t tlv_id_ce_unknown[] = {
  0x06, 0x03, 0x55, 0x1d, 0x37
};

// python DottedOIDToCode.py --tlv id-ce-inhibitAnyPolicy 2.5.29.54
static const uint8_t tlv_id_ce_inhibitAnyPolicy[] = {
  0x06, 0x03, 0x55, 0x1d, 0x36
};

// python DottedOIDToCode.py --tlv id-pkix-ocsp-nocheck 1.3.6.1.5.5.7.48.1.5
static const uint8_t tlv_id_pkix_ocsp_nocheck[] = {
  0x06, 0x09, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x30, 0x01, 0x05
};

struct ExtensionTestcase
{
  ByteString extension;
  Result expectedResult;
};

::std::ostream& operator<<(::std::ostream& os, const ExtensionTestcase&)
{
  return os << "TODO (bug 1318770)";
}

static const ExtensionTestcase EXTENSION_TESTCASES[] =
{
  // Tests that a non-critical extension not in the id-ce or id-pe arcs (which
  // is thus unknown to us) verifies successfully even if empty (extensions we
  // know about aren't normally allowed to be empty).
  { TLV(der::SEQUENCE,
        BytesToByteString(tlv_unknownExtensionOID) +
        TLV(der::OCTET_STRING, ByteString())),
    Success
  },

  // Tests that a critical extension not in the id-ce or id-pe arcs (which is
  // thus unknown to us) is detected and that verification fails with the
  // appropriate error.
  { TLV(der::SEQUENCE,
        BytesToByteString(tlv_unknownExtensionOID) +
        Boolean(true) +
        TLV(der::OCTET_STRING, ByteString())),
    Result::ERROR_UNKNOWN_CRITICAL_EXTENSION
  },

  // Tests that a id-pe-authorityInformationAccess critical extension
  // is detected and that verification succeeds.
  // XXX: According to RFC 5280 an AIA that consists of an empty sequence is
  // not legal, but we accept it and that is not what we're testing here.
  { TLV(der::SEQUENCE,
        BytesToByteString(tlv_id_pe_authorityInformationAccess) +
        Boolean(true) +
        TLV(der::OCTET_STRING, TLV(der::SEQUENCE, ByteString()))),
    Success
  },

  // Tests that an incorrect OID for id-pe-authorityInformationAccess
  // (when marked critical) is detected and that verification fails.
  // (Until bug 1020993 was fixed, this wrong value was used for
  // id-pe-authorityInformationAccess.)
  { TLV(der::SEQUENCE,
        BytesToByteString(tlv_wrongExtensionOID) +
        Boolean(true) +
        TLV(der::OCTET_STRING, ByteString())),
    Result::ERROR_UNKNOWN_CRITICAL_EXTENSION
  },

  // We know about some id-ce extensions (OID arc 2.5.29), but not all of them.
  // Tests that an unknown id-ce extension is detected and that verification
  // fails.
  { TLV(der::SEQUENCE,
        BytesToByteString(tlv_id_ce_unknown) +
        Boolean(true) +
        TLV(der::OCTET_STRING, ByteString())),
    Result::ERROR_UNKNOWN_CRITICAL_EXTENSION
  },

  // Tests that a certificate with a known critical id-ce extension (in this
  // case, OID 2.5.29.54, which is id-ce-inhibitAnyPolicy), verifies
  // successfully.
  { TLV(der::SEQUENCE,
        BytesToByteString(tlv_id_ce_inhibitAnyPolicy) +
        Boolean(true) +
        TLV(der::OCTET_STRING, Integer(0))),
    Success
  },

  // Tests that a certificate with the id-pkix-ocsp-nocheck extension (marked
  // critical) verifies successfully.
  // RFC 6960:
  //   ext-ocsp-nocheck EXTENSION ::= { SYNTAX NULL IDENTIFIED
  //                                    BY id-pkix-ocsp-nocheck }
  { TLV(der::SEQUENCE,
        BytesToByteString(tlv_id_pkix_ocsp_nocheck) +
        Boolean(true) +
        TLV(der::OCTET_STRING, TLV(der::NULLTag, ByteString()))),
    Success
  },

  // Tests that a certificate with another representation of the
  // id-pkix-ocsp-nocheck extension (marked critical) verifies successfully.
  // According to http://comments.gmane.org/gmane.ietf.x509/30947,
  // some code creates certificates where value of the extension is
  // an empty OCTET STRING.
  { TLV(der::SEQUENCE,
        BytesToByteString(tlv_id_pkix_ocsp_nocheck) +
        Boolean(true) +
        TLV(der::OCTET_STRING, ByteString())),
    Success
  },
};

class pkixcert_extension
  : public ::testing::Test
  , public ::testing::WithParamInterface<ExtensionTestcase>
{
protected:
  static TrustEverythingTrustDomain trustDomain;
};

/*static*/ TrustEverythingTrustDomain pkixcert_extension::trustDomain;

TEST_P(pkixcert_extension, ExtensionHandledProperly)
{
  const ExtensionTestcase& testcase(GetParam());
  const char* cn = "Cert Extension Test";
  ByteString cert(CreateCertWithOneExtension(cn, testcase.extension));
  ASSERT_FALSE(ENCODING_FAILED(cert));
  Input certInput;
  ASSERT_EQ(Success, certInput.Init(cert.data(), cert.length()));
  ASSERT_EQ(testcase.expectedResult,
            BuildCertChain(trustDomain, certInput, Now(),
                           EndEntityOrCA::MustBeEndEntity,
                           KeyUsage::noParticularKeyUsageRequired,
                           KeyPurposeId::anyExtendedKeyUsage,
                           CertPolicyId::anyPolicy,
                           nullptr/*stapledOCSPResponse*/));
}

INSTANTIATE_TEST_SUITE_P(pkixcert_extension,
                        pkixcert_extension,
                        testing::ValuesIn(EXTENSION_TESTCASES));

// Two subjectAltNames must result in an error.
TEST_F(pkixcert_extension, DuplicateSubjectAltName)
{
  // python DottedOIDToCode.py --tlv id-ce-subjectAltName 2.5.29.17
  static const uint8_t tlv_id_ce_subjectAltName[] = {
    0x06, 0x03, 0x55, 0x1d, 0x11
  };

  ByteString subjectAltName(
    TLV(der::SEQUENCE,
        BytesToByteString(tlv_id_ce_subjectAltName) +
        TLV(der::OCTET_STRING, TLV(der::SEQUENCE, DNSName("example.com")))));
  static const ByteString extensions[] = { subjectAltName, subjectAltName,
                                           ByteString() };
  static const char* certCN = "Cert With Duplicate subjectAltName";
  ByteString cert(CreateCertWithExtensions(certCN, extensions));
  ASSERT_FALSE(ENCODING_FAILED(cert));
  Input certInput;
  ASSERT_EQ(Success, certInput.Init(cert.data(), cert.length()));
  ASSERT_EQ(Result::ERROR_EXTENSION_VALUE_INVALID,
            BuildCertChain(trustDomain, certInput, Now(),
                           EndEntityOrCA::MustBeEndEntity,
                           KeyUsage::noParticularKeyUsageRequired,
                           KeyPurposeId::anyExtendedKeyUsage,
                           CertPolicyId::anyPolicy,
                           nullptr/*stapledOCSPResponse*/));
}

TEST_F(pkixcert_extension, QCStatements)
{
  // python DottedOIDToCode.py --tlv id-pe-qcStatements 1.3.6.1.5.5.7.1.3
  const uint8_t tlv_id_pe_qcStatements[] = {
    0x06, 0x08, 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x01, 0x03
  };

  ByteString qcStatementsBytes(
    TLV(der::SEQUENCE,
        BytesToByteString(tlv_id_pe_qcStatements) +
        TLV(der::OCTET_STRING, ByteString(reinterpret_cast<const uint8_t*>("qcStatements contents")))));
  const ByteString extensions[] = { qcStatementsBytes, ByteString() };
  const char* certCN = "Cert With qcStatements";
  ByteString cert(CreateCertWithExtensions(certCN, extensions));
  ASSERT_FALSE(ENCODING_FAILED(cert));
  Input certInput;
  ASSERT_EQ(Success, certInput.Init(cert.data(), cert.length()));
  BackCert backCert(certInput, EndEntityOrCA::MustBeEndEntity, nullptr);
  ASSERT_EQ(Success, backCert.Init());
  const Input *qcStatements(backCert.GetQCStatements());
  ASSERT_TRUE(qcStatements);
  ByteString qcStatementsContents(qcStatements->UnsafeGetData(), qcStatements->GetLength());
  ASSERT_EQ(ByteString(reinterpret_cast<const uint8_t*>("qcStatements contents")), qcStatementsContents);
}

// FindSubjectPublicKeyInfo returns the subjectPublicKeyInfo of a certificate
// that BackCert::Init accepts, and BackCert::Init's error otherwise.
TEST_F(pkixcert_extension, FindSubjectPublicKeyInfo)
{
  const char* cn = "Cert Extension Test";
  ByteString cert(CreateCertWithExtensions(cn, nullptr));
  ASSERT_FALSE(ENCODING_FAILED(cert));
  Input certInput;
  ASSERT_EQ(Success, certInput.Init(cert.data(), cert.length()));
  Input spki;
  ASSERT_EQ(Success, FindSubjectPublicKeyInfo(certInput, spki));
  ScopedTestKeyPair key(CloneReusedKeyPair());
  ASSERT_TRUE(InputEqualsByteString(spki, key->subjectPublicKeyInfo));

  // An unknown critical extension is rejected.
  ByteString unknownCritical(
    TLV(der::SEQUENCE,
        BytesToByteString(tlv_unknownExtensionOID) +
        Boolean(true) +
        TLV(der::OCTET_STRING, ByteString())));
  ByteString unknownCriticalCert(
    CreateCertWithOneExtension(cn, unknownCritical));
  ASSERT_FALSE(ENCODING_FAILED(unknownCriticalCert));
  Input unknownCriticalInput;
  ASSERT_EQ(Success, unknownCriticalInput.Init(unknownCriticalCert.data(),
                                               unknownCriticalCert.length()));
  Input unknownCriticalSPKI;
  ASSERT_EQ(Result::ERROR_UNKNOWN_CRITICAL_EXTENSION,
            FindSubjectPublicKeyInfo(unknownCriticalInput, unknownCriticalSPKI));
}
