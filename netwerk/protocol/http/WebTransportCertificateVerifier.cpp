/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebTransportCertificateVerifier.h"
#include "ScopedNSSTypes.h"
#include "nss/mozpkix/pkixutil.h"
#include "nss/mozpkix/pkixcheck.h"
#include "hasht.h"

namespace mozilla::psm {

class ServerCertHashesTrustDomain : public mozilla::pkix::TrustDomain {
 public:
  ServerCertHashesTrustDomain() = default;

  virtual mozilla::pkix::Result FindIssuer(
      mozilla::pkix::Input encodedIssuerName, IssuerChecker& checker,
      mozilla::pkix::Time time) override;

  virtual mozilla::pkix::Result GetCertTrust(
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      const mozilla::pkix::CertPolicyId& policy,
      mozilla::pkix::Input candidateCertDER,
      /*out*/ mozilla::pkix::TrustLevel& trustLevel) override;

  virtual mozilla::pkix::Result CheckSignatureDigestAlgorithm(
      mozilla::pkix::DigestAlgorithm digestAlg,
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      mozilla::pkix::Time notBefore) override;

  virtual mozilla::pkix::Result CheckRSAPublicKeyModulusSizeInBits(
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      unsigned int modulusSizeInBits) override;

  virtual mozilla::pkix::Result VerifyRSAPKCS1SignedData(
      mozilla::pkix::Input data, mozilla::pkix::DigestAlgorithm digestAlgorithm,
      mozilla::pkix::Input signature,
      mozilla::pkix::Input subjectPublicKeyInfo) override;

  virtual mozilla::pkix::Result VerifyRSAPSSSignedData(
      mozilla::pkix::Input data, mozilla::pkix::DigestAlgorithm digestAlgorithm,
      mozilla::pkix::Input signature,
      mozilla::pkix::Input subjectPublicKeyInfo) override;

  virtual mozilla::pkix::Result CheckECDSACurveIsAcceptable(
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      mozilla::pkix::NamedCurve curve) override;

  virtual mozilla::pkix::Result VerifyECDSASignedData(
      mozilla::pkix::Input data, mozilla::pkix::DigestAlgorithm digestAlgorithm,
      mozilla::pkix::Input signature,
      mozilla::pkix::Input subjectPublicKeyInfo) override;

  virtual mozilla::pkix::Result VerifyMLDSASignedData(
      mozilla::pkix::Input data, mozilla::pkix::Input signature,
      mozilla::pkix::Input subjectPublicKeyInfo) override;

  virtual mozilla::pkix::Result DigestBuf(
      mozilla::pkix::Input item, mozilla::pkix::DigestAlgorithm digestAlg,
      /*out*/ uint8_t* digestBuf, size_t digestBufLen) override;

  virtual mozilla::pkix::Result CheckValidityIsAcceptable(
      mozilla::pkix::Time notBefore, mozilla::pkix::Time notAfter,
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      mozilla::pkix::KeyPurposeId keyPurpose) override;

  virtual mozilla::pkix::Result CheckRevocation(
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      const mozilla::pkix::CertID& certID, mozilla::pkix::Time time,
      mozilla::pkix::Duration validityDuration,
      /*optional*/ const mozilla::pkix::Input* stapledOCSPResponse,
      /*optional*/ const mozilla::pkix::Input* aiaExtension) override;

  virtual mozilla::pkix::Result IsChainValid(
      const mozilla::pkix::DERArray& certChain, mozilla::pkix::Time time,
      const mozilla::pkix::CertPolicyId& requiredPolicy) override;

  virtual void NoteAuxiliaryExtension(
      mozilla::pkix::AuxiliaryExtension extension,
      mozilla::pkix::Input extensionData) override;
};

mozilla::pkix::Result ServerCertHashesTrustDomain::FindIssuer(
    mozilla::pkix::Input encodedIssuerName, IssuerChecker& checker,
    mozilla::pkix::Time time) {
  MOZ_ASSERT_UNREACHABLE("not expecting this to be called");

  return mozilla::pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
}

mozilla::pkix::Result ServerCertHashesTrustDomain::GetCertTrust(
    mozilla::pkix::EndEntityOrCA endEntityOrCA,
    const mozilla::pkix::CertPolicyId& policy,
    mozilla::pkix::Input candidateCertDER,
    /*out*/ mozilla::pkix::TrustLevel& trustLevel) {
  MOZ_ASSERT_UNREACHABLE("not expecting this to be called");

  return mozilla::pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
}

mozilla::pkix::Result
ServerCertHashesTrustDomain::CheckSignatureDigestAlgorithm(
    mozilla::pkix::DigestAlgorithm digestAlg,
    mozilla::pkix::EndEntityOrCA endEntityOrCA, mozilla::pkix::Time notBefore) {
  MOZ_ASSERT_UNREACHABLE("not expecting this to be called");

  return mozilla::pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
}

mozilla::pkix::Result
ServerCertHashesTrustDomain::CheckRSAPublicKeyModulusSizeInBits(
    mozilla::pkix::EndEntityOrCA endEntityOrCA,
    unsigned int modulusSizeInBits) {
  return mozilla::pkix::Result::
      ERROR_UNSUPPORTED_KEYALG;  // RSA is not supported for
                                 // serverCertificateHashes,
  // Chromium does only support it for an intermediate period due to spec
  // change, we do not support it.
}

mozilla::pkix::Result ServerCertHashesTrustDomain::VerifyRSAPKCS1SignedData(
    mozilla::pkix::Input data, mozilla::pkix::DigestAlgorithm digestAlgorithm,
    mozilla::pkix::Input signature, mozilla::pkix::Input subjectPublicKeyInfo) {
  MOZ_ASSERT_UNREACHABLE("not expecting this to be called");

  return mozilla::pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
}

mozilla::pkix::Result ServerCertHashesTrustDomain::VerifyRSAPSSSignedData(
    mozilla::pkix::Input data, mozilla::pkix::DigestAlgorithm digestAlgorithm,
    mozilla::pkix::Input signature, mozilla::pkix::Input subjectPublicKeyInfo) {
  MOZ_ASSERT_UNREACHABLE("not expecting this to be called");

  return mozilla::pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
}

mozilla::pkix::Result ServerCertHashesTrustDomain::CheckECDSACurveIsAcceptable(
    mozilla::pkix::EndEntityOrCA endEntityOrCA,
    mozilla::pkix::NamedCurve curve) {
  return mozilla::pkix::Result::Success;
}

mozilla::pkix::Result ServerCertHashesTrustDomain::VerifyECDSASignedData(
    mozilla::pkix::Input data, mozilla::pkix::DigestAlgorithm digestAlgorithm,
    mozilla::pkix::Input signature, mozilla::pkix::Input subjectPublicKeyInfo) {
  MOZ_ASSERT_UNREACHABLE("not expecting this to be called");

  return mozilla::pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
}

mozilla::pkix::Result ServerCertHashesTrustDomain::VerifyMLDSASignedData(
    mozilla::pkix::Input data, mozilla::pkix::Input signature,
    mozilla::pkix::Input subjectPublicKeyInfo) {
  MOZ_ASSERT_UNREACHABLE("not expecting this to be called");

  return mozilla::pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
}

mozilla::pkix::Result ServerCertHashesTrustDomain::DigestBuf(
    mozilla::pkix::Input item, mozilla::pkix::DigestAlgorithm digestAlg,
    /*out*/ uint8_t* digestBuf, size_t digestBufLen) {
  MOZ_ASSERT_UNREACHABLE("not expecting this to be called");

  return mozilla::pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
}

mozilla::pkix::Result ServerCertHashesTrustDomain::CheckValidityIsAcceptable(
    mozilla::pkix::Time notBefore, mozilla::pkix::Time notAfter,
    mozilla::pkix::EndEntityOrCA endEntityOrCA,
    mozilla::pkix::KeyPurposeId keyPurpose) {
  MOZ_ASSERT_UNREACHABLE("not expecting this to be called");

  return mozilla::pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
}

mozilla::pkix::Result ServerCertHashesTrustDomain::CheckRevocation(
    mozilla::pkix::EndEntityOrCA endEntityOrCA,
    const mozilla::pkix::CertID& certID, mozilla::pkix::Time time,
    mozilla::pkix::Duration validityDuration,
    /*optional*/ const mozilla::pkix::Input* stapledOCSPResponse,
    /*optional*/ const mozilla::pkix::Input* aiaExtension) {
  MOZ_ASSERT_UNREACHABLE("not expecting this to be called");

  return mozilla::pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
}

mozilla::pkix::Result ServerCertHashesTrustDomain::IsChainValid(
    const mozilla::pkix::DERArray& certChain, mozilla::pkix::Time time,
    const mozilla::pkix::CertPolicyId& requiredPolicy) {
  MOZ_ASSERT_UNREACHABLE("not expecting this to be called");

  return mozilla::pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
}

void ServerCertHashesTrustDomain::NoteAuxiliaryExtension(
    mozilla::pkix::AuxiliaryExtension extension,
    mozilla::pkix::Input extensionData) {
  MOZ_ASSERT_UNREACHABLE("not expecting this to be called");
}

}  // namespace mozilla::psm

namespace mozilla::net {
// Does certificate verificate as required for serverCertificateHashes
// This function is currently only used for Quic, but may be used later also for
// http/2
mozilla::pkix::Result AuthCertificateWithServerCertificateHashes(
    nsTArray<uint8_t>& peerCert,
    const nsTArray<RefPtr<nsIWebTransportHash>>& aServerCertHashes) {
  using namespace mozilla::pkix;
  Input certDER;
  mozilla::pkix::Result rv =
      certDER.Init(peerCert.Elements(), peerCert.Length());
  if (rv != Success) {
    return rv;
  }
  BackCert cert(certDER, EndEntityOrCA::MustBeEndEntity, nullptr);
  rv = cert.Init();
  if (rv != Success) {
    return rv;
  }

  Time notBefore(Time::uninitialized);
  Time notAfter(Time::uninitialized);
  rv = ParseValidity(cert.GetValidity(), &notBefore, &notAfter);
  if (rv != Success) {
    return rv;
  }
  // now we check that validity is not greater than 14 days
  Duration certDuration(notBefore, notAfter);
  if (certDuration > Duration(60 * 60 * 24 * 14)) {
    return mozilla::pkix::Result::ERROR_VALIDITY_TOO_LONG;
  }
  Time now = Now();
  // and if the certificate is actually valid?
  rv = CheckValidity(now, notBefore, notAfter);
  if (rv != Success) {
    return rv;
  }

  mozilla::psm::ServerCertHashesTrustDomain trustDomain;
  rv = CheckSubjectPublicKeyInfo(
      cert.GetSubjectPublicKeyInfo(), trustDomain,
      EndEntityOrCA::MustBeEndEntity /* should be ignored*/);
  if (rv != Success) {
    return rv;
  }

  // ok now the final check, calculate the hash and compare it:
  // https://w3c.github.io/webtransport/#compute-a-certificate-hash
  nsTArray<uint8_t> certHash;
  if (NS_FAILED(Digest::DigestBuf(SEC_OID_SHA256, peerCert, certHash)) ||
      certHash.Length() != SHA256_LENGTH) {
    return mozilla::pkix::Result::ERROR_INVALID_ALGORITHM;
  }

  // https://w3c.github.io/webtransport/#verify-a-certificate-hash
  for (const auto& hash : aServerCertHashes) {
    nsCString algorithm;
    if (NS_FAILED(hash->GetAlgorithm(algorithm)) || algorithm != "sha-256") {
      continue;
    }

    nsTArray<uint8_t> value;
    if (NS_FAILED(hash->GetValue(value))) {
      continue;
    }

    if (certHash == value) {
      return Success;
    }
  }
  return mozilla::pkix::Result::ERROR_UNTRUSTED_CERT;
}
}  // namespace mozilla::net
