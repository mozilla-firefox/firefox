/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef AppTrustDomain_h
#define AppTrustDomain_h

#include "mozilla/Span.h"
#include "mozpkix/pkixtypes.h"
#include "nsCOMPtr.h"
#include "nsDebug.h"
#include "nsICertStorage.h"
#include "nsIX509CertDB.h"
#include "nsTArray.h"

namespace mozilla {
namespace psm {

class AppTrustDomain final : public mozilla::pkix::TrustDomain {
 public:
  typedef mozilla::pkix::Result Result;

  explicit AppTrustDomain(nsTArray<Span<const uint8_t>>&& collectedCerts);

  nsresult SetTrustedRoot(AppTrustedRoot trustedRoot);

  virtual Result GetCertTrust(
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      const mozilla::pkix::CertPolicyId& policy,
      mozilla::pkix::Input candidateCertDER,
      /*out*/ mozilla::pkix::TrustLevel& trustLevel) override;
  virtual Result FindIssuer(mozilla::pkix::Input encodedIssuerName,
                            IssuerChecker& checker,
                            mozilla::pkix::Time time) override;
  virtual Result CheckRevocation(
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      const mozilla::pkix::CertID& certID, mozilla::pkix::Time time,
      mozilla::pkix::Duration validityDuration,
      /*optional*/ const mozilla::pkix::Input* stapledOCSPresponse,
      /*optional*/ const mozilla::pkix::Input* aiaExtension) override;
  virtual Result IsChainValid(
      const mozilla::pkix::DERArray& certChain, mozilla::pkix::Time time,
      const mozilla::pkix::CertPolicyId& requiredPolicy) override;
  virtual Result CheckSignatureDigestAlgorithm(
      mozilla::pkix::DigestAlgorithm digestAlg,
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      mozilla::pkix::Time notBefore) override;
  virtual Result CheckRSAPublicKeyModulusSizeInBits(
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      unsigned int modulusSizeInBits) override;
  virtual Result VerifyRSAPKCS1SignedData(
      mozilla::pkix::Input data, mozilla::pkix::DigestAlgorithm digestAlgorithm,
      mozilla::pkix::Input signature,
      mozilla::pkix::Input subjectPublicKeyInfo) override;
  virtual Result VerifyRSAPSSSignedData(
      mozilla::pkix::Input data, mozilla::pkix::DigestAlgorithm digestAlgorithm,
      mozilla::pkix::Input signature,
      mozilla::pkix::Input subjectPublicKeyInfo) override;
  virtual Result CheckECDSACurveIsAcceptable(
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      mozilla::pkix::NamedCurve curve) override;
  virtual Result VerifyECDSASignedData(
      mozilla::pkix::Input data, mozilla::pkix::DigestAlgorithm digestAlgorithm,
      mozilla::pkix::Input signature,
      mozilla::pkix::Input subjectPublicKeyInfo) override;
  virtual Result VerifyMLDSASignedData(
      mozilla::pkix::Input data, mozilla::pkix::Input signature,
      mozilla::pkix::Input subjectPublicKeyInfo) override;
  virtual Result CheckValidityIsAcceptable(
      mozilla::pkix::Time notBefore, mozilla::pkix::Time notAfter,
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      mozilla::pkix::KeyPurposeId keyPurpose) override;
  virtual void NoteAuxiliaryExtension(
      mozilla::pkix::AuxiliaryExtension extension,
      mozilla::pkix::Input extensionData) override;
  virtual Result DigestBuf(mozilla::pkix::Input item,
                           mozilla::pkix::DigestAlgorithm digestAlg,
                           /*out*/ uint8_t* digestBuf,
                           size_t digestBufLen) override;

 private:
  nsTArray<Span<const uint8_t>> mTrustedRoots;
  nsTArray<Span<const uint8_t>> mAddonsIntermediates;
  nsTArray<Span<const uint8_t>> mIntermediates;
  nsCOMPtr<nsICertStorage> mCertBlocklist;
};

}  // namespace psm
}  // namespace mozilla

#endif  // AppTrustDomain_h
