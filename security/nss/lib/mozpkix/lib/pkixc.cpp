/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "mozpkix/pkixc.h"

#include "mozpkix/pkix.h"
#include "mozpkix/pkixnss.h"
#include "mozpkix/pkixtypes.h"
#include "secerr.h"

using namespace mozilla::pkix;

const size_t SHA256_DIGEST_LENGTH = 256 / 8;

class CodeSigningTrustDomain final : public TrustDomain {
 public:
  explicit CodeSigningTrustDomain(const uint8_t** certificates,
                                  const uint16_t* certificateLengths,
                                  size_t numCertificates,
                                  const uint8_t* rootSHA256Digest)
      : mCertificates(certificates),
        mCertificateLengths(certificateLengths),
        mNumCertificates(numCertificates),
        mRootSHA256Digest(rootSHA256Digest) {}

  virtual Result GetCertTrust(EndEntityOrCA endEntityOrCA,
                              const CertPolicyId& policy,
                              Input candidateCertDER,
                              /*out*/ TrustLevel& trustLevel) override {
    uint8_t digestBuf[SHA256_DIGEST_LENGTH] = {0};
    Result rv = DigestBufNSS(candidateCertDER, DigestAlgorithm::sha256,
                             digestBuf, SHA256_DIGEST_LENGTH);
    if (rv != Success) {
      return rv;
    }
    Input candidateDigestInput;
    rv = candidateDigestInput.Init(digestBuf, SHA256_DIGEST_LENGTH);
    if (rv != Success) {
      return rv;
    }
    Input rootDigestInput;
    rv = rootDigestInput.Init(mRootSHA256Digest, SHA256_DIGEST_LENGTH);
    if (rv != Success) {
      return rv;
    }
    if (InputsAreEqual(candidateDigestInput, rootDigestInput)) {
      trustLevel = TrustLevel::TrustAnchor;
    } else {
      trustLevel = TrustLevel::InheritsTrust;
    }
    return Success;
  }

  virtual Result FindIssuer(Input encodedIssuerName, IssuerChecker& checker,
                            Time time) override {
    for (size_t i = 0; i < mNumCertificates; i++) {
      Input certInput;
      Result rv = certInput.Init(mCertificates[i], mCertificateLengths[i]);
      if (rv != Success) {
        return rv;
      }
      bool keepGoing;
      rv = checker.Check(certInput, nullptr /*additionalNameConstraints*/,
                         keepGoing);
      if (rv != Success) {
        return rv;
      }
      if (!keepGoing) {
        break;
      }
    }

    return Success;
  }

  virtual Result CheckRevocation(
      EndEntityOrCA endEntityOrCA, const CertID& certID, Time time,
      Duration validityDuration,
      /*optional*/ const Input* stapledOCSPresponse,
      /*optional*/ const Input* aiaExtension) override {
    return Success;
  }

  virtual Result IsChainValid(const DERArray& certChain, Time time,
                              const CertPolicyId& requiredPolicy) override {
    return Success;
  }

  virtual Result CheckSignatureDigestAlgorithm(DigestAlgorithm digestAlg,
                                               EndEntityOrCA endEntityOrCA,
                                               Time notBefore) override {
    switch (digestAlg) {
      case DigestAlgorithm::sha256:  // fall through
      case DigestAlgorithm::sha384:  // fall through
      case DigestAlgorithm::sha512:
        return Success;
      default:
        return Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED;
    }
  }

  virtual Result CheckRSAPublicKeyModulusSizeInBits(
      EndEntityOrCA endEntityOrCA, unsigned int modulusSizeInBits) override {
    if (modulusSizeInBits < 2048) {
      return Result::ERROR_INADEQUATE_KEY_SIZE;
    }
    return Success;
  }

  virtual Result VerifyRSAPKCS1SignedData(
      Input data, DigestAlgorithm digestAlgorithm, Input signature,
      Input subjectPublicKeyInfo) override {
    return VerifyRSAPKCS1SignedDataNSS(data, digestAlgorithm, signature,
        subjectPublicKeyInfo, nullptr);
  }

  virtual Result VerifyRSAPSSSignedData(
      Input data, DigestAlgorithm digestAlgorithm, Input signature,
      Input subjectPublicKeyInfo) override {
    return VerifyRSAPSSSignedDataNSS(data, digestAlgorithm, signature,
        subjectPublicKeyInfo, nullptr);
  }

  virtual Result CheckECDSACurveIsAcceptable(EndEntityOrCA endEntityOrCA,
                                             NamedCurve curve) override {
    switch (curve) {
      case NamedCurve::secp256r1:  // fall through
      case NamedCurve::secp384r1:  // fall through
      case NamedCurve::secp521r1:
        return Success;
    }

    return Result::ERROR_UNSUPPORTED_ELLIPTIC_CURVE;
  }

  virtual Result VerifyECDSASignedData(
      Input data, DigestAlgorithm digestAlgorithm, Input signature,
      Input subjectPublicKeyInfo) override {
    return VerifyECDSASignedDataNSS(data, digestAlgorithm, signature,
        subjectPublicKeyInfo, nullptr);
  }

  virtual Result VerifyMLDSASignedData(Input data, Input signature,
                                       Input subjectPublicKeyInfo) override {
    return VerifyMLDSASignedDataNSS(data, signature, subjectPublicKeyInfo,
                                    nullptr);
  }

  virtual Result CheckValidityIsAcceptable(Time notBefore, Time notAfter,
                                           EndEntityOrCA endEntityOrCA,
                                           KeyPurposeId keyPurpose) override {
    return Success;
  }

  virtual void NoteAuxiliaryExtension(AuxiliaryExtension extension,
                                      Input extensionData) override {}

  virtual Result DigestBuf(Input item, DigestAlgorithm digestAlg,
                           /*out*/ uint8_t* digestBuf,
                           size_t digestBufLen) override {
    return DigestBufNSS(item, digestAlg, digestBuf, digestBufLen);
  }

 private:
  const uint8_t** mCertificates;
  const uint16_t* mCertificateLengths;
  size_t mNumCertificates;
  const uint8_t* mRootSHA256Digest;
};

class CodeSigningNameMatchingPolicy : public NameMatchingPolicy {
 public:
  virtual Result FallBackToCommonName(
      Time notBefore,
      /*out*/ FallBackToSearchWithinSubject& fallBackToCommonName) override {
    fallBackToCommonName = FallBackToSearchWithinSubject::No;
    return Success;
  }

  virtual HandleInvalidSubjectAlternativeNamesBy
  HandleInvalidSubjectAlternativeNames() override {
    return HandleInvalidSubjectAlternativeNamesBy::Halting;
  }
};

bool VerifyCodeSigningCertificateChain(
    const uint8_t** certificates, const uint16_t* certificateLengths,
    size_t numCertificates, uint64_t secondsSinceEpoch,
    const uint8_t* rootSHA256Digest, const uint8_t* hostname,
    size_t hostnameLength, PRErrorCode* error) {
  if (!error) {
    return false;
  }
  if (!certificates || !certificateLengths || numCertificates < 1 ||
      !rootSHA256Digest) {
    *error = SEC_ERROR_INVALID_ARGS;
    return false;
  }

  CodeSigningTrustDomain trustDomain(certificates, certificateLengths,
                                     numCertificates, rootSHA256Digest);
  Input certificate;
  Result rv = certificate.Init(certificates[0], certificateLengths[0]);
  if (rv != Success) {
    *error = MapResultToPRErrorCode(rv);
    return false;
  }
  Time time = TimeFromEpochInSeconds(secondsSinceEpoch);
  rv = BuildCertChain(
      trustDomain, certificate, time, EndEntityOrCA::MustBeEndEntity,
      KeyUsage::noParticularKeyUsageRequired, KeyPurposeId::id_kp_codeSigning,
      CertPolicyId::anyPolicy, nullptr);
  if (rv != Success) {
    *error = MapResultToPRErrorCode(rv);
    return false;
  }
  Input hostnameInput;
  rv = hostnameInput.Init(hostname, hostnameLength);
  if (rv != Success) {
    *error = MapResultToPRErrorCode(rv);
    return false;
  }
  CodeSigningNameMatchingPolicy nameMatchingPolicy;
  rv = CheckCertHostname(certificate, hostnameInput, nameMatchingPolicy);
  if (rv != Success) {
    *error = MapResultToPRErrorCode(rv);
    return false;
  }
  return true;
}
