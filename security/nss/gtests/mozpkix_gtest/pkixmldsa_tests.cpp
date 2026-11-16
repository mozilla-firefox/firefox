/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "pkixgtest.h"

#include <algorithm>
#include <ctime>
#include <utility>
#include <vector>

#include "json_reader.h"
#include "keyhi.h"
#include "ml_dsat.h"
#include "pk11pub.h"
#include "mozpkix/nss_scoped_ptrs.h"
#include "mozpkix/pkixc.h"
#include "mozpkix/pkixder.h"
#include "mozpkix/pkixnss.h"
#include "mozpkix/pkixutil.h"

using namespace mozilla::pkix;
using namespace mozilla::pkix::test;

#define BS(s) ByteString(s, MOZILLA_PKIX_ARRAY_LENGTH(s))

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

// A P-256 SubjectPublicKeyInfo, used to check that a non-ML-DSA key is not
// mistaken for an ML-DSA one.
static const uint8_t kEC256SubjectPublicKeyInfo[] = {
  0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02,
  0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
  0x42, 0x00, 0x04, 0x6d, 0xd3, 0xc0, 0x54, 0xc6, 0x33, 0xd3, 0xff, 0x4a,
  0x43, 0x0c, 0x3a, 0x70, 0xdb, 0x97, 0xf6, 0xc9, 0x68, 0xa0, 0xb1, 0xb9,
  0x6e, 0x15, 0x20, 0x80, 0x50, 0x6f, 0x37, 0x8f, 0xcc, 0xe3, 0x47, 0x96,
  0x2c, 0x5d, 0xb8, 0x76, 0x8e, 0x67, 0x19, 0x1c, 0xc7, 0x64, 0xb4, 0xd5,
  0xda, 0xa6, 0x4d, 0xc5, 0x3d, 0xe6, 0xa7, 0xb3, 0xb0, 0x41, 0xfb, 0x29,
  0xb4, 0x2f, 0xe8, 0x72, 0xeb, 0xfb, 0xfe
};

// The message that gets signed by the round-trip tests below. mozilla::pkix
// hands the TBSCertificate to the TrustDomain verbatim for ML-DSA (there is no
// pre-hash step), so the content is arbitrary.
static const uint8_t kData[] = {
  0x53, 0x70, 0x68, 0x69, 0x6e, 0x78, 0x20, 0x6f, 0x66, 0x20, 0x62, 0x6c,
  0x61, 0x63, 0x6b, 0x20, 0x71, 0x75, 0x61, 0x72, 0x74, 0x7a, 0x2c, 0x20,
  0x6a, 0x75, 0x64, 0x67, 0x65, 0x20, 0x6d, 0x79, 0x20, 0x76, 0x6f, 0x77,
  0x2e
};

// The test fixtures below are parameterized on MLDSAParamSet, so it must not
// be in an anonymous namespace (GCC's -Wsubobject-linkage).
struct MLDSAParamSet
{
  const char* name;
  CK_ML_DSA_PARAMETER_SET_TYPE ckParamSet;
  MLDSAParameterSet pkixParamSet;
  ByteString algorithmIdentifier;  // just the OID; parameters must be absent
  size_t publicKeyLength;
  size_t signatureLength;
};

::std::ostream& operator<<(::std::ostream& os, const MLDSAParamSet& paramSet)
{
  return os << paramSet.name;
}

static const MLDSAParamSet ML_DSA_PARAM_SETS[] = {
  { "ML-DSA-44", CKP_ML_DSA_44, MLDSAParameterSet::ml_dsa_44,
    BS(tlv_id_ml_dsa_44), ML_DSA_44_PUBLICKEY_LEN, ML_DSA_44_SIGNATURE_LEN },
  { "ML-DSA-65", CKP_ML_DSA_65, MLDSAParameterSet::ml_dsa_65,
    BS(tlv_id_ml_dsa_65), ML_DSA_65_PUBLICKEY_LEN, ML_DSA_65_SIGNATURE_LEN },
  { "ML-DSA-87", CKP_ML_DSA_87, MLDSAParameterSet::ml_dsa_87,
    BS(tlv_id_ml_dsa_87), ML_DSA_87_PUBLICKEY_LEN, ML_DSA_87_SIGNATURE_LEN },
};

namespace {

// A freshly generated ML-DSA key pair, along with the DER encoding of its
// SubjectPublicKeyInfo.
class MLDSATestKey
{
public:
  bool Generate(CK_ML_DSA_PARAMETER_SET_TYPE ckParamSet)
  {
    ScopedPK11SlotInfo slot(PK11_GetInternalSlot());
    EXPECT_TRUE(slot);
    if (!slot) {
      return false;
    }
    SECKEYPublicKey* publicKeyTemp = nullptr;
    privateKey.reset(PK11_GenerateKeyPair(slot.get(), CKM_ML_DSA_KEY_PAIR_GEN,
                                          &ckParamSet, &publicKeyTemp,
                                          PR_FALSE, PR_FALSE, nullptr));
    publicKey.reset(publicKeyTemp);
    EXPECT_TRUE(privateKey) << PORT_ErrorToString(PORT_GetError());
    EXPECT_TRUE(publicKey);
    if (!privateKey || !publicKey) {
      return false;
    }

    ScopedSECItem spki(SECKEY_EncodeDERSubjectPublicKeyInfo(publicKey.get()));
    EXPECT_TRUE(spki) << PORT_ErrorToString(PORT_GetError());
    if (!spki) {
      return false;
    }
    subjectPublicKeyInfo.assign(spki->data, spki->len);
    return true;
  }

  // Produces an ML-DSA signature with an empty context string, which is what
  // certificate signatures use.
  bool Sign(const ByteString& data, /*out*/ ByteString& signature) const
  {
    CK_SIGN_ADDITIONAL_CONTEXT signContext;
    signContext.hedgeVariant = CKH_HEDGE_PREFERRED;
    signContext.pContext = nullptr;
    signContext.ulContextLen = 0;
    SECItem param = { siBuffer,
                      reinterpret_cast<unsigned char*>(&signContext),
                      sizeof(signContext) };

    std::vector<unsigned char> buffer(MAX_ML_DSA_SIGNATURE_LEN);
    SECItem signatureItem = { siBuffer, buffer.data(),
                              static_cast<unsigned int>(buffer.size()) };
    SECItem dataItem = { siBuffer, const_cast<unsigned char*>(data.data()),
                         static_cast<unsigned int>(data.length()) };
    EXPECT_EQ(SECSuccess,
              PK11_SignWithMechanism(privateKey.get(), CKM_ML_DSA, &param,
                                     &signatureItem, &dataItem))
      << PORT_ErrorToString(PORT_GetError());
    if (signatureItem.len == 0) {
      return false;
    }
    signature.assign(signatureItem.data, signatureItem.len);
    return true;
  }

  ScopedSECKEYPrivateKey privateKey;
  ScopedSECKEYPublicKey publicKey;
  ByteString subjectPublicKeyInfo;
};

Result CallVerifyMLDSASignedDataNSS(const ByteString& data,
                                    MLDSAParameterSet parameterSet,
                                    const ByteString& signature,
                                    const ByteString& subjectPublicKeyInfo)
{
  Input dataInput;
  EXPECT_EQ(Success, dataInput.Init(data.data(), data.length()));
  Input signatureInput;
  EXPECT_EQ(Success,
            signatureInput.Init(signature.data(), signature.length()));
  Input spkiInput;
  EXPECT_EQ(Success, spkiInput.Init(subjectPublicKeyInfo.data(),
                                    subjectPublicKeyInfo.length()));
  return VerifyMLDSASignedDataNSS(dataInput, parameterSet, signatureInput,
                                  spkiInput, nullptr);
}

// Builds a SubjectPublicKeyInfo out of an AlgorithmIdentifier and a raw public
// key, without going through NSS, so that malformed encodings can be tested.
ByteString CreateSPKI(const ByteString& algorithmIdentifier,
                      const ByteString& rawPublicKey)
{
  return TLV(der::SEQUENCE,
             TLV(der::SEQUENCE, algorithmIdentifier) +
               TLV(der::BIT_STRING, ByteString(1, 0x00) + rawPublicKey));
}

}  // namespace

///////////////////////////////////////////////////////////////////////////////
// VerifyMLDSASignedDataNSS

class pkixmldsa_VerifyMLDSASignedDataNSS
  : public ::testing::Test
  , public ::testing::WithParamInterface<MLDSAParamSet>
{
};

TEST_P(pkixmldsa_VerifyMLDSASignedDataNSS, RoundTrip)
{
  const MLDSAParamSet& paramSet(GetParam());
  MLDSATestKey key;
  ASSERT_TRUE(key.Generate(paramSet.ckParamSet));

  ByteString data(BS(kData));
  ByteString signature;
  ASSERT_TRUE(key.Sign(data, signature));
  ASSERT_EQ(paramSet.signatureLength, signature.length());

  ASSERT_EQ(Success,
            CallVerifyMLDSASignedDataNSS(data, paramSet.pkixParamSet,
                                         signature, key.subjectPublicKeyInfo));
}

// The issuer's key must be for the parameter set that the signature algorithm
// names, even though the signature would verify under it.
TEST_P(pkixmldsa_VerifyMLDSASignedDataNSS, ParameterSetMismatch)
{
  const MLDSAParamSet& paramSet(GetParam());
  MLDSATestKey key;
  ASSERT_TRUE(key.Generate(paramSet.ckParamSet));

  ByteString data(BS(kData));
  ByteString signature;
  ASSERT_TRUE(key.Sign(data, signature));

  for (const MLDSAParamSet& other : ML_DSA_PARAM_SETS) {
    if (other.pkixParamSet == paramSet.pkixParamSet) {
      continue;
    }
    EXPECT_EQ(Result::ERROR_SIGNATURE_ALGORITHM_MISMATCH,
              CallVerifyMLDSASignedDataNSS(data, other.pkixParamSet,
                                           signature,
                                           key.subjectPublicKeyInfo))
      << "an " << paramSet.name << " key was accepted for an " << other.name
      << " signature algorithm";
  }
}

// A certificate can carry an empty signature BIT STRING, so this is
// attacker-reachable. It must be reported as a bad signature rather than as a
// fatal error, which would indicate a bug or a library failure.
TEST_P(pkixmldsa_VerifyMLDSASignedDataNSS, EmptySignature)
{
  const MLDSAParamSet& paramSet(GetParam());
  MLDSATestKey key;
  ASSERT_TRUE(key.Generate(paramSet.ckParamSet));

  ASSERT_EQ(Result::ERROR_BAD_SIGNATURE,
            CallVerifyMLDSASignedDataNSS(BS(kData), paramSet.pkixParamSet,
                                         ByteString(),
                                         key.subjectPublicKeyInfo));
}

INSTANTIATE_TEST_SUITE_P(pkixmldsa_VerifyMLDSASignedDataNSS,
                         pkixmldsa_VerifyMLDSASignedDataNSS,
                         testing::ValuesIn(ML_DSA_PARAM_SETS));

class pkixmldsa_tests : public ::testing::Test { };

// VerifySignedData dispatches on the signature AlgorithmIdentifier, not on the
// type of the issuer's key, so VerifyMLDSASignedDataNSS can be reached with a
// SubjectPublicKeyInfo that holds any kind of key. It must not treat such a
// key as an ML-DSA key.
TEST_F(pkixmldsa_tests, VerifyMLDSASignedDataNSSWithNonMLDSAKey)
{
  // The key is rejected before the signature is looked at.
  ByteString data(BS(kData));
  ByteString signature(ML_DSA_44_SIGNATURE_LEN, 0x00);

  ScopedTestKeyPair rsaKeyPair(CloneReusedKeyPair());
  ASSERT_TRUE(rsaKeyPair.get());
  EXPECT_EQ(Result::ERROR_UNSUPPORTED_KEYALG,
            CallVerifyMLDSASignedDataNSS(data, MLDSAParameterSet::ml_dsa_44,
                                         signature,
                                         rsaKeyPair->subjectPublicKeyInfo));

  EXPECT_EQ(Result::ERROR_UNSUPPORTED_KEYALG,
            CallVerifyMLDSASignedDataNSS(data, MLDSAParameterSet::ml_dsa_44,
                                         signature,
                                         BS(kEC256SubjectPublicKeyInfo)));
}

///////////////////////////////////////////////////////////////////////////////
// CheckSubjectPublicKeyInfo

namespace {

// Records the arguments of CheckMLDSAParameterSetIsAcceptable and answers it
// with a fixed result. EverythingFailsByDefaultTrustDomain calls ADD_FAILURE()
// from CheckRSAPublicKeyModulusSizeInBits and CheckECDSACurveIsAcceptable, so
// this also catches an ML-DSA key being routed through the RSA or ECDSA key
// checks.
class MLDSAKeyCheckTrustDomain final
  : public EverythingFailsByDefaultTrustDomain
{
public:
  explicit MLDSAKeyCheckTrustDomain(Result aResult = Success)
    : result(aResult)
    , called(false)
    , endEntityOrCA(EndEntityOrCA::MustBeEndEntity)
    , parameterSet(MLDSAParameterSet::ml_dsa_44)
  {
  }

  Result CheckMLDSAParameterSetIsAcceptable(
    EndEntityOrCA aEndEntityOrCA, MLDSAParameterSet aParameterSet) override
  {
    EXPECT_FALSE(called);
    called = true;
    endEntityOrCA = aEndEntityOrCA;
    parameterSet = aParameterSet;
    return result;
  }

  const Result result;
  bool called;
  EndEntityOrCA endEntityOrCA;
  MLDSAParameterSet parameterSet;
};

Result CallCheckSubjectPublicKeyInfo(const ByteString& spki,
                                     TrustDomain& trustDomain,
                                     EndEntityOrCA endEntityOrCA)
{
  Input spkiInput;
  EXPECT_EQ(Success, spkiInput.Init(spki.data(), spki.length()));
  return CheckSubjectPublicKeyInfo(spkiInput, trustDomain, endEntityOrCA);
}

Result CallCheckSubjectPublicKeyInfo(const ByteString& spki)
{
  MLDSAKeyCheckTrustDomain trustDomain;
  return CallCheckSubjectPublicKeyInfo(spki, trustDomain,
                                       EndEntityOrCA::MustBeEndEntity);
}

}  // namespace

class pkixmldsa_CheckSubjectPublicKeyInfo
  : public ::testing::Test
  , public ::testing::WithParamInterface<MLDSAParamSet>
{
};

// An SPKI produced by NSS for a real ML-DSA key must be accepted.
TEST_P(pkixmldsa_CheckSubjectPublicKeyInfo, GeneratedKeyIsAccepted)
{
  const MLDSAParamSet& paramSet(GetParam());
  MLDSATestKey key;
  ASSERT_TRUE(key.Generate(paramSet.ckParamSet));

  ASSERT_EQ(Success, CallCheckSubjectPublicKeyInfo(key.subjectPublicKeyInfo));
}

// The TrustDomain decides whether the parameter set is acceptable, and is told
// whether the key belongs to an end-entity or a CA.
TEST_P(pkixmldsa_CheckSubjectPublicKeyInfo, TrustDomainChecksParameterSet)
{
  const MLDSAParamSet& paramSet(GetParam());
  ByteString spki(CreateSPKI(paramSet.algorithmIdentifier,
                             ByteString(paramSet.publicKeyLength, 0x00)));

  for (EndEntityOrCA endEntityOrCA :
       { EndEntityOrCA::MustBeEndEntity, EndEntityOrCA::MustBeCA }) {
    MLDSAKeyCheckTrustDomain trustDomain;
    ASSERT_EQ(Success,
              CallCheckSubjectPublicKeyInfo(spki, trustDomain, endEntityOrCA));
    ASSERT_TRUE(trustDomain.called);
    ASSERT_EQ(paramSet.pkixParamSet, trustDomain.parameterSet);
    ASSERT_EQ(endEntityOrCA, trustDomain.endEntityOrCA);
  }

  MLDSAKeyCheckTrustDomain rejectingTrustDomain(
    Result::ERROR_UNSUPPORTED_KEYALG);
  ASSERT_EQ(Result::ERROR_UNSUPPORTED_KEYALG,
            CallCheckSubjectPublicKeyInfo(spki, rejectingTrustDomain,
                                          EndEntityOrCA::MustBeEndEntity));
}

// The public key must be exactly the length that FIPS 204 gives for the
// parameter set named by the OID.
TEST_P(pkixmldsa_CheckSubjectPublicKeyInfo, PublicKeyLengthMustMatch)
{
  const MLDSAParamSet& paramSet(GetParam());

  ASSERT_EQ(Result::ERROR_BAD_DER,
            CallCheckSubjectPublicKeyInfo(
              CreateSPKI(paramSet.algorithmIdentifier, ByteString())));
  ASSERT_EQ(Result::ERROR_BAD_DER,
            CallCheckSubjectPublicKeyInfo(CreateSPKI(
              paramSet.algorithmIdentifier,
              ByteString(paramSet.publicKeyLength - 1, 0x00))));
  ASSERT_EQ(Result::ERROR_BAD_DER,
            CallCheckSubjectPublicKeyInfo(CreateSPKI(
              paramSet.algorithmIdentifier,
              ByteString(paramSet.publicKeyLength + 1, 0x00))));

  for (const MLDSAParamSet& other : ML_DSA_PARAM_SETS) {
    if (other.publicKeyLength == paramSet.publicKeyLength) {
      continue;
    }
    ASSERT_EQ(Result::ERROR_BAD_DER,
              CallCheckSubjectPublicKeyInfo(CreateSPKI(
                paramSet.algorithmIdentifier,
                ByteString(other.publicKeyLength, 0x00))))
      << "an " << paramSet.name << " SPKI with an " << other.name
      << "-sized public key was accepted";
  }
}

// RFC 9881 Section 2 says that the parameters MUST be absent, so even an
// explicit NULL is rejected.
TEST_P(pkixmldsa_CheckSubjectPublicKeyInfo, ParametersMustBeAbsent)
{
  const MLDSAParamSet& paramSet(GetParam());
  ByteString rawPublicKey(paramSet.publicKeyLength, 0x00);

  ASSERT_EQ(Success,
            CallCheckSubjectPublicKeyInfo(
              CreateSPKI(paramSet.algorithmIdentifier, rawPublicKey)));

  ASSERT_EQ(Result::ERROR_BAD_DER,
            CallCheckSubjectPublicKeyInfo(
              CreateSPKI(paramSet.algorithmIdentifier +
                           TLV(der::NULLTag, ByteString()),
                         rawPublicKey)));

  ASSERT_EQ(Result::ERROR_BAD_DER,
            CallCheckSubjectPublicKeyInfo(
              CreateSPKI(paramSet.algorithmIdentifier +
                           TLV(der::INTEGER, ByteString(1, 0x00)),
                         rawPublicKey)));
}

INSTANTIATE_TEST_SUITE_P(pkixmldsa_CheckSubjectPublicKeyInfo,
                         pkixmldsa_CheckSubjectPublicKeyInfo,
                         testing::ValuesIn(ML_DSA_PARAM_SETS));

///////////////////////////////////////////////////////////////////////////////
// VerifySignedData dispatch

namespace {

// Records what VerifySignedData passed along to the TrustDomain, so that the
// ML-DSA dispatch can be checked without doing any real cryptography.
class MLDSADispatchTrustDomain final
  : public EverythingFailsByDefaultTrustDomain
{
public:
  MLDSADispatchTrustDomain()
    : mldsaCalled(false)
    , parameterSet(MLDSAParameterSet::ml_dsa_44)
  {
  }

  Result VerifyMLDSASignedData(Input aData, MLDSAParameterSet aParameterSet,
                               Input aSignature,
                               Input aSubjectPublicKeyInfo) override
  {
    mldsaCalled = true;
    data.assign(aData.UnsafeGetData(), aData.GetLength());
    parameterSet = aParameterSet;
    signature.assign(aSignature.UnsafeGetData(), aSignature.GetLength());
    subjectPublicKeyInfo.assign(aSubjectPublicKeyInfo.UnsafeGetData(),
                                aSubjectPublicKeyInfo.GetLength());
    return Success;
  }

  bool mldsaCalled;
  ByteString data;
  MLDSAParameterSet parameterSet;
  ByteString signature;
  ByteString subjectPublicKeyInfo;
};

}  // namespace

class pkixmldsa_VerifySignedData
  : public ::testing::Test
  , public ::testing::WithParamInterface<MLDSAParamSet>
{
};

// An ML-DSA signature algorithm must reach TrustDomain::VerifyMLDSASignedData
// (and not any of the digest-taking Verify*SignedData methods, which the base
// trust domain fails), with the parameter set it names, and with the data,
// signature and key passed through unmodified.
TEST_P(pkixmldsa_VerifySignedData, DispatchesToVerifyMLDSASignedData)
{
  const MLDSAParamSet& paramSet(GetParam());

  ByteString data(BS(kData));
  ByteString signature(paramSet.signatureLength, 0xDE);
  ByteString spki(CreateSPKI(paramSet.algorithmIdentifier,
                             ByteString(paramSet.publicKeyLength, 0x00)));

  der::SignedDataWithSignature signedData;
  ASSERT_EQ(Success,
            signedData.data.Init(data.data(), data.length()));
  // The contents of the AlgorithmIdentifier SEQUENCE, i.e. just the OID TLV,
  // which is what SignatureAlgorithmIdentifierValue expects.
  ASSERT_EQ(Success,
            signedData.algorithm.Init(paramSet.algorithmIdentifier.data(),
                                      paramSet.algorithmIdentifier.length()));
  ASSERT_EQ(Success,
            signedData.signature.Init(signature.data(), signature.length()));

  Input spkiInput;
  ASSERT_EQ(Success, spkiInput.Init(spki.data(), spki.length()));

  MLDSADispatchTrustDomain trustDomain;
  ASSERT_EQ(Success,
            VerifySignedData(trustDomain, signedData, spkiInput));
  ASSERT_TRUE(trustDomain.mldsaCalled);
  ASSERT_EQ(paramSet.pkixParamSet, trustDomain.parameterSet);
  ASSERT_EQ(data, trustDomain.data);
  ASSERT_EQ(signature, trustDomain.signature);
  ASSERT_EQ(spki, trustDomain.subjectPublicKeyInfo);
}

INSTANTIATE_TEST_SUITE_P(pkixmldsa_VerifySignedData,
                         pkixmldsa_VerifySignedData,
                         testing::ValuesIn(ML_DSA_PARAM_SETS));

///////////////////////////////////////////////////////////////////////////////
// Path building

namespace {

ByteString CreateTestCert(const char* issuerCN, const char* subjectCN,
                          EndEntityOrCA endEntityOrCA,
                          const TestKeyPair& subjectKeyPair,
                          const TestKeyPair& issuerKeyPair,
                          const TestSignatureAlgorithm& signatureAlgorithm,
                          const ByteString& extraExtension = ByteString())
{
  static long serialNumberValue = 0;
  ++serialNumberValue;
  ByteString serialNumber(CreateEncodedSerialNumber(serialNumberValue));
  EXPECT_FALSE(ENCODING_FAILED(serialNumber));

  std::vector<ByteString> extensions;
  if (endEntityOrCA == EndEntityOrCA::MustBeCA) {
    extensions.push_back(
      CreateEncodedBasicConstraints(true, nullptr, Critical::Yes));
  }
  if (!extraExtension.empty()) {
    extensions.push_back(extraExtension);
  }
  extensions.push_back(ByteString());  // marks the end of the list

  ByteString cert(CreateEncodedCertificate(
    v3, signatureAlgorithm, serialNumber, CNToDERName(issuerCN),
    oneDayBeforeNow, oneDayAfterNow, CNToDERName(subjectCN), subjectKeyPair,
    extensions.data(), issuerKeyPair, signatureAlgorithm));
  EXPECT_FALSE(ENCODING_FAILED(cert));
  return cert;
}

// Trusts root, offers the intermediate (if any) and the root as issuers, and
// records the arguments of every CheckMLDSAParameterSetIsAcceptable call. The
// signatures are verified for real, by DefaultCryptoTrustDomain.
class MLDSAChainTrustDomain final : public DefaultCryptoTrustDomain
{
public:
  MLDSAChainTrustDomain(const ByteString& aRoot,
                        const ByteString& aIntermediate = ByteString())
    : root(aRoot)
    , intermediate(aIntermediate)
  {
  }

  Result GetCertTrust(EndEntityOrCA, const CertPolicyId&, Input cert,
                      /*out*/ TrustLevel& trustLevel) override
  {
    trustLevel = InputEqualsByteString(cert, root)
               ? TrustLevel::TrustAnchor
               : TrustLevel::InheritsTrust;
    return Success;
  }

  Result FindIssuer(Input, IssuerChecker& checker, Time) override
  {
    for (const ByteString* candidate : { &intermediate, &root }) {
      if (candidate->empty()) {
        continue;
      }
      Input candidateInput;
      EXPECT_EQ(Success,
                candidateInput.Init(candidate->data(), candidate->length()));
      bool keepGoing;
      Result rv = checker.Check(candidateInput, nullptr, keepGoing);
      if (rv != Success) {
        return rv;
      }
      if (!keepGoing) {
        break;
      }
    }
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

  Result CheckMLDSAParameterSetIsAcceptable(
    EndEntityOrCA endEntityOrCA, MLDSAParameterSet parameterSet) override
  {
    std::pair<EndEntityOrCA, MLDSAParameterSet> call(endEntityOrCA,
                                                    parameterSet);
    checked.push_back(call);
    return std::find(rejected.begin(), rejected.end(), call) == rejected.end()
           ? Success
           : Result::ERROR_UNSUPPORTED_KEYALG;
  }

  bool Checked(EndEntityOrCA endEntityOrCA,
               MLDSAParameterSet parameterSet) const
  {
    return std::find(checked.begin(), checked.end(),
                     std::make_pair(endEntityOrCA, parameterSet)) !=
           checked.end();
  }

  const ByteString root;
  const ByteString intermediate;
  std::vector<std::pair<EndEntityOrCA, MLDSAParameterSet>> rejected;
  std::vector<std::pair<EndEntityOrCA, MLDSAParameterSet>> checked;
};

Result BuildMLDSACertChain(TrustDomain& trustDomain,
                           const ByteString& endEntity)
{
  Input endEntityInput;
  EXPECT_EQ(Success, endEntityInput.Init(endEntity.data(), endEntity.length()));
  return BuildCertChain(trustDomain, endEntityInput, Now(),
                        EndEntityOrCA::MustBeEndEntity,
                        KeyUsage::noParticularKeyUsageRequired,
                        KeyPurposeId::anyExtendedKeyUsage,
                        CertPolicyId::anyPolicy, nullptr);
}

}  // namespace

class pkixmldsa_BuildCertChain
  : public ::testing::Test
  , public ::testing::WithParamInterface<MLDSAParamSet>
{
};

// A root, an intermediate, and an end-entity, all with keys of the same
// parameter set.
TEST_P(pkixmldsa_BuildCertChain, Valid)
{
  const MLDSAParameterSet parameterSet(GetParam().pkixParamSet);
  ScopedTestKeyPair rootKey(GenerateMLDSAKeyPair(parameterSet));
  ASSERT_TRUE(rootKey);
  ScopedTestKeyPair intermediateKey(GenerateMLDSAKeyPair(parameterSet));
  ASSERT_TRUE(intermediateKey);
  ScopedTestKeyPair endEntityKey(GenerateMLDSAKeyPair(parameterSet));
  ASSERT_TRUE(endEntityKey);
  const TestSignatureAlgorithm signatureAlgorithm(mlDSA(parameterSet));

  ByteString root(CreateTestCert("root", "root", EndEntityOrCA::MustBeCA,
                                 *rootKey, *rootKey, signatureAlgorithm));
  ByteString intermediate(
    CreateTestCert("root", "intermediate", EndEntityOrCA::MustBeCA,
                   *intermediateKey, *rootKey, signatureAlgorithm));
  ByteString endEntity(
    CreateTestCert("intermediate", "end-entity",
                   EndEntityOrCA::MustBeEndEntity, *endEntityKey,
                   *intermediateKey, signatureAlgorithm));

  MLDSAChainTrustDomain trustDomain(root, intermediate);
  ASSERT_EQ(Success, BuildMLDSACertChain(trustDomain, endEntity));
  ASSERT_TRUE(trustDomain.Checked(EndEntityOrCA::MustBeEndEntity,
                                  parameterSet));
  ASSERT_TRUE(trustDomain.Checked(EndEntityOrCA::MustBeCA, parameterSet));
}

INSTANTIATE_TEST_SUITE_P(pkixmldsa_BuildCertChain, pkixmldsa_BuildCertChain,
                         testing::ValuesIn(ML_DSA_PARAM_SETS));

class pkixmldsa_BuildCertChainMixed : public ::testing::Test
{
protected:
  void SetUp() override
  {
    key87.reset(GenerateMLDSAKeyPair(MLDSAParameterSet::ml_dsa_87));
    ASSERT_TRUE(key87);
    key65.reset(GenerateMLDSAKeyPair(MLDSAParameterSet::ml_dsa_65));
    ASSERT_TRUE(key65);
    key44.reset(GenerateMLDSAKeyPair(MLDSAParameterSet::ml_dsa_44));
    ASSERT_TRUE(key44);

    root = CreateTestCert("root", "root", EndEntityOrCA::MustBeCA,
                          *key87, *key87,
                          mlDSA(MLDSAParameterSet::ml_dsa_87));
    intermediate = CreateTestCert("root", "intermediate",
                                  EndEntityOrCA::MustBeCA, *key65,
                                  *key87,
                                  mlDSA(MLDSAParameterSet::ml_dsa_87));
    endEntity = CreateTestCert("intermediate", "end-entity",
                               EndEntityOrCA::MustBeEndEntity, *key44,
                               *key65,
                               mlDSA(MLDSAParameterSet::ml_dsa_65));
  }

  ScopedTestKeyPair key87;
  ScopedTestKeyPair key65;
  ScopedTestKeyPair key44;
  // An ML-DSA-87 root, an ML-DSA-65 intermediate, and an ML-DSA-44
  // end-entity.
  ByteString root;
  ByteString intermediate;
  ByteString endEntity;
};

// Each signature is checked against the parameter set of its issuer's key, not
// against any parameter set that the rest of the chain uses.
TEST_F(pkixmldsa_BuildCertChainMixed, Valid)
{
  MLDSAChainTrustDomain trustDomain(root, intermediate);
  ASSERT_EQ(Success, BuildMLDSACertChain(trustDomain, endEntity));
  ASSERT_TRUE(trustDomain.Checked(EndEntityOrCA::MustBeEndEntity,
                                  MLDSAParameterSet::ml_dsa_44));
  ASSERT_TRUE(trustDomain.Checked(EndEntityOrCA::MustBeCA,
                                  MLDSAParameterSet::ml_dsa_65));
}

// The TrustDomain can refuse a parameter set, for end-entity keys and for CA
// keys separately.
TEST_F(pkixmldsa_BuildCertChainMixed, TrustDomainRejectsParameterSet)
{
  MLDSAChainTrustDomain rejectsEndEntity(root, intermediate);
  rejectsEndEntity.rejected.push_back(
    std::make_pair(EndEntityOrCA::MustBeEndEntity,
                   MLDSAParameterSet::ml_dsa_44));
  ASSERT_EQ(Result::ERROR_UNSUPPORTED_KEYALG,
            BuildMLDSACertChain(rejectsEndEntity, endEntity));

  MLDSAChainTrustDomain rejectsCA(root, intermediate);
  rejectsCA.rejected.push_back(
    std::make_pair(EndEntityOrCA::MustBeCA, MLDSAParameterSet::ml_dsa_65));
  ASSERT_EQ(Result::ERROR_UNSUPPORTED_KEYALG,
            BuildMLDSACertChain(rejectsCA, endEntity));
}

// A signature made with the ML-DSA-65 intermediate's key, but labelled as
// ML-DSA-44 (in both signature algorithm fields), must not be accepted, even
// though it would verify under the key.
TEST_F(pkixmldsa_BuildCertChainMixed, MislabelledSignatureAlgorithm)
{
  ByteString mislabelled(
    CreateTestCert("intermediate", "end-entity",
                   EndEntityOrCA::MustBeEndEntity, *key44, *key65,
                   mlDSA(MLDSAParameterSet::ml_dsa_44)));
  MLDSAChainTrustDomain trustDomain(root, intermediate);
  ASSERT_EQ(Result::ERROR_SIGNATURE_ALGORITHM_MISMATCH,
            BuildMLDSACertChain(trustDomain, mislabelled));
}

// ML-DSA and RSA keys can be mixed in one chain: an RSA root issues an ML-DSA
// intermediate, which issues an RSA end-entity.
TEST_F(pkixmldsa_tests, BuildCertChainMixedWithRSA)
{
  ScopedTestKeyPair rsaKey(CloneReusedKeyPair());
  ASSERT_TRUE(rsaKey);
  ScopedTestKeyPair mldsaKey(
    GenerateMLDSAKeyPair(MLDSAParameterSet::ml_dsa_65));
  ASSERT_TRUE(mldsaKey);

  ByteString root(CreateTestCert("root", "root", EndEntityOrCA::MustBeCA,
                                 *rsaKey, *rsaKey,
                                 sha256WithRSAEncryption()));
  ByteString intermediate(
    CreateTestCert("root", "intermediate", EndEntityOrCA::MustBeCA,
                   *mldsaKey, *rsaKey, sha256WithRSAEncryption()));
  ByteString endEntity(
    CreateTestCert("intermediate", "end-entity",
                   EndEntityOrCA::MustBeEndEntity, *rsaKey, *mldsaKey,
                   mlDSA(MLDSAParameterSet::ml_dsa_65)));

  MLDSAChainTrustDomain trustDomain(root, intermediate);
  ASSERT_EQ(Success, BuildMLDSACertChain(trustDomain, endEntity));
}

///////////////////////////////////////////////////////////////////////////////
// VerifyCodeSigningCertificateChain

class pkixmldsa_VerifyCodeSigningCertificateChain
  : public ::testing::Test
  , public ::testing::WithParamInterface<MLDSAParamSet>
{
};

// The C API, whose TrustDomain (in pkixc.cpp) accepts every ML-DSA parameter
// set. The largest parameter set also checks that the certificates still fit
// in its 16-bit lengths.
TEST_P(pkixmldsa_VerifyCodeSigningCertificateChain, Valid)
{
  const MLDSAParameterSet parameterSet(GetParam().pkixParamSet);
  ScopedTestKeyPair rootKey(GenerateMLDSAKeyPair(parameterSet));
  ASSERT_TRUE(rootKey);
  ScopedTestKeyPair endEntityKey(GenerateMLDSAKeyPair(parameterSet));
  ASSERT_TRUE(endEntityKey);
  const TestSignatureAlgorithm signatureAlgorithm(mlDSA(parameterSet));

  ByteString root(CreateTestCert("root", "root", EndEntityOrCA::MustBeCA,
                                 *rootKey, *rootKey, signatureAlgorithm));
  ByteString endEntity(CreateTestCert(
    "root", "end-entity", EndEntityOrCA::MustBeEndEntity, *endEntityKey,
    *rootKey, signatureAlgorithm,
    CreateEncodedSubjectAltName(DNSName("example.com"))));

  const uint8_t* certificates[] = { endEntity.data(), root.data() };
  const uint16_t certificateLengths[] = {
    static_cast<uint16_t>(endEntity.length()),
    static_cast<uint16_t>(root.length())
  };
  ASSERT_EQ(endEntity.length(), certificateLengths[0]);
  ASSERT_EQ(root.length(), certificateLengths[1]);

  uint8_t rootSHA256Digest[32] = { 0 };
  Input rootInput;
  ASSERT_EQ(Success, rootInput.Init(root.data(), root.length()));
  ASSERT_EQ(Success, DigestBufNSS(rootInput, DigestAlgorithm::sha256,
                                  rootSHA256Digest, sizeof(rootSHA256Digest)));

  static const uint8_t hostname[] = "example.com";
  PRErrorCode error = 0;
  ASSERT_TRUE(VerifyCodeSigningCertificateChain(
    certificates, certificateLengths, MOZILLA_PKIX_ARRAY_LENGTH(certificates),
    static_cast<uint64_t>(std::time(nullptr)), rootSHA256Digest, hostname,
    sizeof(hostname) - 1, &error))
    << PORT_ErrorToString(error);
  ASSERT_EQ(0, error);
}

INSTANTIATE_TEST_SUITE_P(pkixmldsa_VerifyCodeSigningCertificateChain,
                         pkixmldsa_VerifyCodeSigningCertificateChain,
                         testing::ValuesIn(ML_DSA_PARAM_SETS));

///////////////////////////////////////////////////////////////////////////////
// Wycheproof

namespace {

struct MLDSAVerifyTestVector
{
  uint64_t id;
  bool valid;
  ByteString msg;
  ByteString ctx;
  ByteString sig;
  bool incorrectPublicKeyLength = false;
};

ByteString ToByteString(const std::vector<uint8_t>& v)
{
  return ByteString(v.data(), v.size());
}

void ReadMLDSAVerifyTestAttr(MLDSAVerifyTestVector& t, const std::string& n,
                             JsonReader& r)
{
  if (n == "msg") {
    t.msg = ToByteString(r.ReadHex());
  } else if (n == "ctx") {
    t.ctx = ToByteString(r.ReadHex());
  } else if (n == "sig") {
    t.sig = ToByteString(r.ReadHex());
  } else {
    FAIL() << "unsupported test case field: " << n;
  }
}

void ProcessMLDSAVerifyTestFlags(MLDSAVerifyTestVector& t, const std::string&,
                                 const std::vector<std::string>& flags)
{
  t.incorrectPublicKeyLength =
    std::find(flags.begin(), flags.end(), "IncorrectPublicKeyLength") !=
    flags.end();
}

void RunMLDSAVerifyTestGroup(const MLDSAParamSet& paramSet, JsonReader& r)
{
  ByteString spki;
  std::vector<MLDSAVerifyTestVector> tests;
  while (r.NextItem()) {
    std::string n = r.ReadLabel();
    if (n == "") {
      break;
    }
    if (n == "publicKeyDer") {
      spki = ToByteString(r.ReadHex());
    } else if (n == "publicKey" || n == "type" || n == "source") {
      r.SkipValue();
    } else if (n == "tests") {
      WycheproofReadTests(r, &tests, ReadMLDSAVerifyTestAttr, false,
                          ProcessMLDSAVerifyTestFlags);
    } else {
      FAIL() << "unknown group label: " << n;
    }
  }

  for (const MLDSAVerifyTestVector& t : tests) {
    SCOPED_TRACE(testing::Message() << "tcId " << t.id);

    // Certificates use pure ML-DSA with an empty context string, so there is
    // no way to pass a context string through mozilla::pkix, and a signature
    // made with a non-empty one must not verify.
    if (!t.ctx.empty()) {
      if (t.valid) {
        EXPECT_EQ(Result::ERROR_BAD_SIGNATURE,
                  CallVerifyMLDSASignedDataNSS(t.msg, paramSet.pkixParamSet,
                                               t.sig, spki));
      }
      continue;
    }

    Result rv = CallVerifyMLDSASignedDataNSS(t.msg, paramSet.pkixParamSet,
                                             t.sig, spki);
    if (t.valid) {
      EXPECT_EQ(Success, rv);
      EXPECT_EQ(Success, CallCheckSubjectPublicKeyInfo(spki));
    } else {
      // An invalid signature or key must be reported as an error, but not as
      // a fatal one, which would indicate a bug or a library failure.
      EXPECT_NE(Success, rv);
      EXPECT_FALSE(IsFatalError(rv)) << ::testing::PrintToString(rv);
    }
    if (t.incorrectPublicKeyLength) {
      EXPECT_EQ(Result::ERROR_BAD_DER, CallCheckSubjectPublicKeyInfo(spki));
    }
  }
}

std::string MLDSAVerifyTestFile(MLDSAParameterSet parameterSet)
{
  switch (parameterSet) {
    case MLDSAParameterSet::ml_dsa_44:
      return "mldsa_44_verify";
    case MLDSAParameterSet::ml_dsa_65:
      return "mldsa_65_verify";
    case MLDSAParameterSet::ml_dsa_87:
      return "mldsa_87_verify";
    MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM
  }
}

}  // namespace

class pkixmldsa_Wycheproof
  : public ::testing::Test
  , public ::testing::WithParamInterface<MLDSAParamSet>
{
};

// The Wycheproof ML-DSA verification vectors, run through
// VerifyMLDSASignedDataNSS. The keys are also run through
// CheckSubjectPublicKeyInfo.
TEST_P(pkixmldsa_Wycheproof, Verify)
{
  const MLDSAParamSet& paramSet(GetParam());
  WycheproofHeader(MLDSAVerifyTestFile(paramSet.pkixParamSet), paramSet.name,
                   "mldsa_verify_schema.json", [&paramSet](JsonReader& r) {
                     RunMLDSAVerifyTestGroup(paramSet, r);
                   });
}

INSTANTIATE_TEST_SUITE_P(pkixmldsa_Wycheproof, pkixmldsa_Wycheproof,
                         testing::ValuesIn(ML_DSA_PARAM_SETS));
