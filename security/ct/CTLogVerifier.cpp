/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CTLogVerifier.h"

#include <stdint.h>

#include "CTSerialization.h"
#include "CertVerifier.h"
#include "hasht.h"
#include "mozpkix/Result.h"
#include "mozpkix/pkixnss.h"
#include "mozpkix/pkixutil.h"
#include "mozilla/glean/SecurityCtMetrics.h"

using namespace mozilla::pkix;

namespace mozilla {
namespace ct {

// A TrustDomain used to extract the SCT log signature parameters
// given its subjectPublicKeyInfo.
// Only RSASSA-PKCS1v15 with SHA-256 and ECDSA (using the NIST P-256 curve)
// with SHA-256 are allowed.
// RSA keys must be at least 2048 bits.
// See See RFC 6962, Section 2.1.4.
class SignatureParamsTrustDomain final : public TrustDomain {
 public:
  SignatureParamsTrustDomain()
      : mSignatureAlgorithm(DigitallySigned::SignatureAlgorithm::Anonymous) {}

  pkix::Result GetCertTrust(EndEntityOrCA, const CertPolicyId&, Input,
                            TrustLevel&) override {
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result FindIssuer(Input, IssuerChecker&, Time) override {
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result CheckRevocation(EndEntityOrCA, const CertID&, Time, Duration,
                               const Input*, const Input*) override {
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result IsChainValid(const DERArray&, Time,
                            const CertPolicyId&) override {
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result DigestBuf(Input, DigestAlgorithm, uint8_t*, size_t) override {
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result CheckSignatureDigestAlgorithm(DigestAlgorithm, EndEntityOrCA,
                                             Time) override {
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result CheckECDSACurveIsAcceptable(EndEntityOrCA,
                                           NamedCurve curve) override {
    assert(mSignatureAlgorithm ==
           DigitallySigned::SignatureAlgorithm::Anonymous);
    if (curve != NamedCurve::secp256r1) {
      return pkix::Result::ERROR_UNSUPPORTED_ELLIPTIC_CURVE;
    }
    mSignatureAlgorithm = DigitallySigned::SignatureAlgorithm::ECDSA;
    return Success;
  }

  pkix::Result VerifyECDSASignedData(Input, DigestAlgorithm, Input,
                                     Input) override {
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result VerifyMLDSASignedData(Input, Input, Input) override {
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result CheckRSAPublicKeyModulusSizeInBits(
      EndEntityOrCA, unsigned int modulusSizeInBits) override {
    assert(mSignatureAlgorithm ==
           DigitallySigned::SignatureAlgorithm::Anonymous);
    // Require RSA keys of at least 2048 bits. See RFC 6962, Section 2.1.4.
    if (modulusSizeInBits < 2048) {
      return pkix::Result::ERROR_INADEQUATE_KEY_SIZE;
    }
    mSignatureAlgorithm = DigitallySigned::SignatureAlgorithm::RSA;
    return Success;
  }

  pkix::Result VerifyRSAPKCS1SignedData(Input, DigestAlgorithm, Input,
                                        Input) override {
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result VerifyRSAPSSSignedData(Input, DigestAlgorithm, Input,
                                      Input) override {
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  pkix::Result CheckValidityIsAcceptable(Time, Time, EndEntityOrCA,
                                         KeyPurposeId) override {
    return pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  void NoteAuxiliaryExtension(AuxiliaryExtension, Input) override {}

  DigitallySigned::SignatureAlgorithm mSignatureAlgorithm;
};

CTLogVerifier::CTLogVerifier(CTLogOperatorId operatorId, CTLogState state,
                             CTLogFormat format, uint64_t timestamp)
    : mSignatureAlgorithm(DigitallySigned::SignatureAlgorithm::Anonymous),
      mOperatorId(operatorId),
      mState(state),
      mFormat(format),
      mTimestamp(timestamp) {}

pkix::Result CTLogVerifier::Init(Input subjectPublicKeyInfo) {
  SignatureParamsTrustDomain trustDomain;
  pkix::Result rv = CheckSubjectPublicKeyInfo(subjectPublicKeyInfo, trustDomain,
                                              EndEntityOrCA::MustBeEndEntity);
  if (rv != Success) {
    return rv;
  }
  mSignatureAlgorithm = trustDomain.mSignatureAlgorithm;

  InputToBuffer(subjectPublicKeyInfo, mSubjectPublicKeyInfo);

  mKeyId.resize(SHA256_LENGTH);
  rv = DigestBufNSS(subjectPublicKeyInfo, DigestAlgorithm::sha256,
                    mKeyId.data(), mKeyId.size());
  if (rv != Success) {
    return rv;
  }

  return Success;
}

pkix::Result CTLogVerifier::Verify(const LogEntry& entry,
                                   const SignedCertificateTimestamp& sct,
                                   SignatureCache* signatureCache) {
  if (mKeyId.empty() || sct.logId != mKeyId || !signatureCache) {
    return pkix::Result::FATAL_ERROR_INVALID_ARGS;
  }
  if (!SignatureParametersMatch(sct.signature)) {
    return pkix::Result::FATAL_ERROR_INVALID_ARGS;
  }

  Buffer serializedLogEntry;
  pkix::Result rv = EncodeLogEntry(entry, serializedLogEntry);
  if (rv != Success) {
    return rv;
  }

  Input logEntryInput;
  rv = BufferToInput(serializedLogEntry, logEntryInput);
  if (rv != Success) {
    return rv;
  }

  // sct.extensions may be empty.  If it is, sctExtensionsInput will remain in
  // its default state, which is valid but of length 0.
  Input sctExtensionsInput;
  if (!sct.extensions.empty()) {
    rv = sctExtensionsInput.Init(sct.extensions.data(), sct.extensions.size());
    if (rv != Success) {
      return rv;
    }
  }

  Buffer serializedData;
  rv = EncodeV1SCTSignedData(sct.timestamp, logEntryInput, sctExtensionsInput,
                             serializedData);
  if (rv != Success) {
    return rv;
  }
  return VerifySignature(serializedData, sct.signature.signatureData,
                         signatureCache);
}

bool CTLogVerifier::SignatureParametersMatch(const DigitallySigned& signature) {
  return signature.SignatureParametersMatch(
      DigitallySigned::HashAlgorithm::SHA256, mSignatureAlgorithm);
}

pkix::Result CTLogVerifier::VerifySignature(Input data, Input signature,
                                            SignatureCache* signatureCache) {
  Input spki;
  pkix::Result rv = BufferToInput(mSubjectPublicKeyInfo, spki);
  if (rv != Success) {
    return rv;
  }

  switch (mSignatureAlgorithm) {
    case DigitallySigned::SignatureAlgorithm::RSA:
      rv = psm::VerifySignedDataWithCache(
          der::PublicKeyAlgorithm::RSA_PKCS1,
          mozilla::glean::sct_signature_cache::total,
          mozilla::glean::sct_signature_cache::hits, data,
          DigestAlgorithm::sha256, signature, spki, signatureCache, nullptr);
      break;
    case DigitallySigned::SignatureAlgorithm::ECDSA:
      rv = psm::VerifySignedDataWithCache(
          der::PublicKeyAlgorithm::ECDSA,
          mozilla::glean::sct_signature_cache::total,
          mozilla::glean::sct_signature_cache::hits, data,
          DigestAlgorithm::sha256, signature, spki, signatureCache, nullptr);
      break;
    // We do not expect new values added to this enum any time soon,
    // so just listing all the available ones seems to be the easiest way
    // to suppress warning C4061 on MSVC (which expects all values of the
    // enum to be explicitly handled).
    case DigitallySigned::SignatureAlgorithm::Anonymous:
    case DigitallySigned::SignatureAlgorithm::DSA:
    default:
      assert(false);
      return pkix::Result::FATAL_ERROR_INVALID_ARGS;
  }
  if (rv != Success) {
    if (IsFatalError(rv)) {
      return rv;
    }
    // If the error is non-fatal, we assume the signature was invalid.
    return pkix::Result::ERROR_BAD_SIGNATURE;
  }
  return Success;
}

pkix::Result CTLogVerifier::VerifySignature(const Buffer& data,
                                            const Buffer& signature,
                                            SignatureCache* signatureCache) {
  Input dataInput;
  pkix::Result rv = BufferToInput(data, dataInput);
  if (rv != Success) {
    return rv;
  }
  Input signatureInput;
  rv = BufferToInput(signature, signatureInput);
  if (rv != Success) {
    return rv;
  }
  return VerifySignature(dataInput, signatureInput, signatureCache);
}

}  // namespace ct
}  // namespace mozilla
