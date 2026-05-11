/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "QWACTrustDomain.h"

#include "mozpkix/pkixnss.h"
#include "mozpkix/pkixutil.h"
#include "qwac_trust_anchors/qwac_trust_anchors_ffi_generated.h"

using namespace mozilla::pkix;

namespace mozilla {
namespace psm {

QWACTrustDomain::QWACTrustDomain(
    nsTArray<RefPtr<nsIX509Cert>>& collectedCerts) {
  for (const auto& cert : collectedCerts) {
    nsTArray<uint8_t> der;
    if (NS_SUCCEEDED(cert->GetRawDER(der))) {
      mIntermediates.AppendElement(std::move(der));
    }
  }
}

pkix::Result QWACTrustDomain::FindIssuer(Input encodedIssuerName,
                                         IssuerChecker& checker, Time) {
  nsTArray<Input> candidates;

  nsTArray<uint8_t> subject(encodedIssuerName.UnsafeGetData(),
                            encodedIssuerName.GetLength());
  nsTArray<nsTArray<uint8_t>> qwacTrustAnchors;
  find_qwac_trust_anchors_by_subject(&subject, &qwacTrustAnchors);

  for (const auto& trustAnchor : qwacTrustAnchors) {
    Input trustAnchorInput;
    pkix::Result rv =
        trustAnchorInput.Init(trustAnchor.Elements(), trustAnchor.Length());
    // This should never fail, since the possible trust anchors are all
    // hard-coded and they should never be too long.
    if (rv != Success) {
      return rv;
    }
    candidates.AppendElement(std::move(trustAnchorInput));
  }

  for (const auto& intermediate : mIntermediates) {
    Input intermediateInput;
    pkix::Result rv =
        intermediateInput.Init(intermediate.Elements(), intermediate.Length());
    // These intermediates are from the TLS handshake and could be too long.
    if (rv != Success) {
      continue;
    }
    candidates.AppendElement(std::move(intermediateInput));
  }

  for (const auto& candidate : candidates) {
    bool keepGoing;
    pkix::Result rv = checker.Check(
        candidate, nullptr /*additionalNameConstraints*/, keepGoing);
    if (rv != Success) {
      return rv;
    }
    if (!keepGoing) {
      break;
    }
  }

  return Success;
}

pkix::Result QWACTrustDomain::GetCertTrust(EndEntityOrCA endEntityOrCA,
                                           const CertPolicyId& policy,
                                           Input candidateCertDER,
                                           /*out*/ TrustLevel& trustLevel) {
  BackCert backCert(candidateCertDER, endEntityOrCA, nullptr);
  Result rv = backCert.Init();
  if (rv != Success) {
    return rv;
  }
  Input subjectInput(backCert.GetSubject());
  nsTArray<uint8_t> subject(subjectInput.UnsafeGetData(),
                            subjectInput.GetLength());
  nsTArray<uint8_t> candidateCert(candidateCertDER.UnsafeGetData(),
                                  candidateCertDER.GetLength());
  if (is_qwac_trust_anchor(&subject, &candidateCert)) {
    trustLevel = TrustLevel::TrustAnchor;
  } else {
    trustLevel = TrustLevel::InheritsTrust;
  }

  return Success;
}

pkix::Result QWACTrustDomain::DigestBuf(Input item, DigestAlgorithm digestAlg,
                                        /*out*/ uint8_t* digestBuf,
                                        size_t digestBufLen) {
  return DigestBufNSS(item, digestAlg, digestBuf, digestBufLen);
}

pkix::Result QWACTrustDomain::CheckRevocation(EndEntityOrCA, const CertID&,
                                              Time, pkix::Duration,
                                              /*optional*/ const Input*,
                                              /*optional*/ const Input*) {
  return Success;
}

pkix::Result QWACTrustDomain::IsChainValid(const DERArray& certChain, Time time,
                                           const CertPolicyId& requiredPolicy) {
  return Success;
}

pkix::Result QWACTrustDomain::CheckSignatureDigestAlgorithm(
    DigestAlgorithm digestAlg, EndEntityOrCA, Time) {
  return Success;
}

pkix::Result QWACTrustDomain::CheckRSAPublicKeyModulusSizeInBits(
    EndEntityOrCA /*endEntityOrCA*/, unsigned int modulusSizeInBits) {
  return Success;
}

pkix::Result QWACTrustDomain::VerifyRSAPKCS1SignedData(
    Input data, DigestAlgorithm digestAlgorithm, Input signature,
    Input subjectPublicKeyInfo) {
  return VerifyRSAPKCS1SignedDataNSS(data, digestAlgorithm, signature,
                                     subjectPublicKeyInfo, nullptr);
}

pkix::Result QWACTrustDomain::VerifyRSAPSSSignedData(
    Input data, DigestAlgorithm digestAlgorithm, Input signature,
    Input subjectPublicKeyInfo) {
  return VerifyRSAPSSSignedDataNSS(data, digestAlgorithm, signature,
                                   subjectPublicKeyInfo, nullptr);
}

pkix::Result QWACTrustDomain::CheckECDSACurveIsAcceptable(
    EndEntityOrCA /*endEntityOrCA*/, NamedCurve curve) {
  return Success;
}

pkix::Result QWACTrustDomain::VerifyECDSASignedData(
    Input data, DigestAlgorithm digestAlgorithm, Input signature,
    Input subjectPublicKeyInfo) {
  return VerifyECDSASignedDataNSS(data, digestAlgorithm, signature,
                                  subjectPublicKeyInfo, nullptr);
}

pkix::Result QWACTrustDomain::CheckValidityIsAcceptable(
    Time /*notBefore*/, Time /*notAfter*/, EndEntityOrCA /*endEntityOrCA*/,
    KeyPurposeId /*keyPurpose*/) {
  return Success;
}

void QWACTrustDomain::NoteAuxiliaryExtension(AuxiliaryExtension /*extension*/,
                                             Input /*extensionData*/) {}

}  // namespace psm
}  // namespace mozilla
