/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PDFTrustDomain.h"

#include "cert_storage/src/cert_storage.h"
#include "mozpkix/pkixnss.h"
#include "mozpkix/pkixutil.h"
#include "NSSCertDBTrustDomain.h"
#include "nsComponentManagerUtils.h"
#include "nsServiceManagerUtils.h"
#include "pdf_trust_anchors/pdf_trust_anchors_ffi_generated.h"

using namespace mozilla::pkix;

extern mozilla::LazyLogModule gPIPNSSLog;

namespace mozilla {
namespace psm {

PDFTrustDomain::PDFTrustDomain(nsTArray<Span<const uint8_t>>&& collectedCerts)
    : mIntermediates(std::move(collectedCerts)),
      mCertBlocklist(do_GetService(NS_CERT_STORAGE_CID)) {}

pkix::Result PDFTrustDomain::FindIssuer(Input encodedIssuerName,
                                        IssuerChecker& checker, Time) {
  nsTArray<Input> candidates;

  nsTArray<uint8_t> subject(encodedIssuerName.UnsafeGetData(),
                            encodedIssuerName.GetLength());
  nsTArray<nsTArray<uint8_t>> pdfTrustAnchors;
  find_pdf_trust_anchors_by_subject(&subject, &pdfTrustAnchors);

  for (const auto& trustAnchor : pdfTrustAnchors) {
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
    // This is untrusted input, so skip any intermediates that are too large.
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

pkix::Result PDFTrustDomain::GetCertTrust(EndEntityOrCA endEntityOrCA,
                                          const CertPolicyId& policy,
                                          Input candidateCertDER,
                                          /*out*/ TrustLevel& trustLevel) {
  MOZ_ASSERT(policy.IsAnyPolicy());
  if (!policy.IsAnyPolicy()) {
    return pkix::Result::FATAL_ERROR_INVALID_ARGS;
  }

  // Check if the certificate is revoked via the cert blocklist.
  nsTArray<uint8_t> issuerBytes;
  nsTArray<uint8_t> serialBytes;
  nsTArray<uint8_t> subjectBytes;
  nsTArray<uint8_t> pubKeyBytes;

  pkix::Result result =
      BuildRevocationCheckArrays(candidateCertDER, endEntityOrCA, issuerBytes,
                                 serialBytes, subjectBytes, pubKeyBytes);
  if (result != Success) {
    return result;
  }

  int16_t revocationState;
  nsresult nsrv = mCertBlocklist->GetRevocationState(
      issuerBytes, serialBytes, subjectBytes, pubKeyBytes, &revocationState);
  if (NS_FAILED(nsrv)) {
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  if (revocationState == nsICertStorage::STATE_ENFORCE) {
    return pkix::Result::ERROR_REVOKED_CERTIFICATE;
  }

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
  if (is_pdf_trust_anchor(&subject, &candidateCert)) {
    trustLevel = TrustLevel::TrustAnchor;
  } else {
    trustLevel = TrustLevel::InheritsTrust;
  }

  return Success;
}

pkix::Result PDFTrustDomain::DigestBuf(Input item, DigestAlgorithm digestAlg,
                                       /*out*/ uint8_t* digestBuf,
                                       size_t digestBufLen) {
  return DigestBufNSS(item, digestAlg, digestBuf, digestBufLen);
}

pkix::Result PDFTrustDomain::CheckRevocation(EndEntityOrCA, const CertID&, Time,
                                             pkix::Duration,
                                             /*optional*/ const Input*,
                                             /*optional*/ const Input*) {
  return Success;
}

pkix::Result PDFTrustDomain::IsChainValid(const DERArray& certChain, Time time,
                                          const CertPolicyId& requiredPolicy) {
  MOZ_ASSERT(requiredPolicy.IsAnyPolicy());
  return Success;
}

pkix::Result PDFTrustDomain::CheckSignatureDigestAlgorithm(
    DigestAlgorithm digestAlg, EndEntityOrCA, Time) {
  switch (digestAlg) {
    case DigestAlgorithm::sha256:  // fall through
    case DigestAlgorithm::sha384:  // fall through
    case DigestAlgorithm::sha512:
      return Success;
    case DigestAlgorithm::sha1:
      return Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED;
  }
  return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
}

pkix::Result PDFTrustDomain::CheckRSAPublicKeyModulusSizeInBits(
    EndEntityOrCA /*endEntityOrCA*/, unsigned int modulusSizeInBits) {
  if (modulusSizeInBits < 2048u) {
    return pkix::Result::ERROR_INADEQUATE_KEY_SIZE;
  }
  return Success;
}

pkix::Result PDFTrustDomain::VerifyRSAPKCS1SignedData(
    Input data, DigestAlgorithm digestAlgorithm, Input signature,
    Input subjectPublicKeyInfo) {
  return VerifyRSAPKCS1SignedDataNSS(data, digestAlgorithm, signature,
                                     subjectPublicKeyInfo, nullptr);
}

pkix::Result PDFTrustDomain::VerifyRSAPSSSignedData(
    Input data, DigestAlgorithm digestAlgorithm, Input signature,
    Input subjectPublicKeyInfo) {
  return VerifyRSAPSSSignedDataNSS(data, digestAlgorithm, signature,
                                   subjectPublicKeyInfo, nullptr);
}

pkix::Result PDFTrustDomain::CheckECDSACurveIsAcceptable(
    EndEntityOrCA /*endEntityOrCA*/, NamedCurve curve) {
  switch (curve) {
    case NamedCurve::secp256r1:  // fall through
    case NamedCurve::secp384r1:  // fall through
    case NamedCurve::secp521r1:
      return Success;
  }

  return pkix::Result::ERROR_UNSUPPORTED_ELLIPTIC_CURVE;
}

pkix::Result PDFTrustDomain::VerifyECDSASignedData(
    Input data, DigestAlgorithm digestAlgorithm, Input signature,
    Input subjectPublicKeyInfo) {
  return VerifyECDSASignedDataNSS(data, digestAlgorithm, signature,
                                  subjectPublicKeyInfo, nullptr);
}

pkix::Result PDFTrustDomain::CheckValidityIsAcceptable(
    Time /*notBefore*/, Time /*notAfter*/, EndEntityOrCA /*endEntityOrCA*/,
    KeyPurposeId /*keyPurpose*/) {
  return Success;
}

void PDFTrustDomain::NoteAuxiliaryExtension(AuxiliaryExtension /*extension*/,
                                            Input /*extensionData*/) {}

}  // namespace psm
}  // namespace mozilla
