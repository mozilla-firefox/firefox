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

#ifndef mozilla_pkix_pkixtypes_h
#define mozilla_pkix_pkixtypes_h

#include <memory>

#include "mozpkix/Input.h"
#include "mozpkix/Time.h"
#include "stdint.h"

namespace mozilla {
namespace pkix {

enum class DigestAlgorithm {
  sha512 = 1,
  sha384 = 2,
  sha256 = 3,
  sha1 = 4,
  /* Used for pure-signature algorithms (ML-DSA) that have no separate hash. */
  none = 5,
};

enum class NamedCurve {
  // secp521r1 (OID 1.3.132.0.35, RFC 5480)
  secp521r1 = 1,

  // secp384r1 (OID 1.3.132.0.34, RFC 5480)
  secp384r1 = 2,

  // secp256r1 (OID 1.2.840.10045.3.1.7, RFC 5480)
  secp256r1 = 3,
};

enum class EndEntityOrCA { MustBeEndEntity = 0, MustBeCA = 1 };

enum class KeyUsage : uint8_t {
  digitalSignature = 0,
  nonRepudiation = 1,
  keyEncipherment = 2,
  dataEncipherment = 3,
  keyAgreement = 4,
  keyCertSign = 5,
  // cRLSign       = 6,
  // encipherOnly  = 7,
  // decipherOnly  = 8,
  noParticularKeyUsageRequired = 0xff,
};

enum class KeyPurposeId {
  anyExtendedKeyUsage = 0,
  id_kp_serverAuth = 1,       // id-kp-serverAuth
  id_kp_clientAuth = 2,       // id-kp-clientAuth
  id_kp_codeSigning = 3,      // id-kp-codeSigning
  id_kp_emailProtection = 4,  // id-kp-emailProtection
  id_kp_OCSPSigning = 9,      // id-kp-OCSPSigning
  // RFC 9336 - X.509 Certificate General-Purpose Extended Key Usage (EKU) for
  // Document Signing
  id_kp_documentSigning = 36,  // id-kp-documentSigning
  // Adobe Authentic Documents Trust (1.2.840.113583.1.1.5)
  id_kp_documentSigningAdobe,
  id_kp_documentSigningMicrosoft,
};

struct CertPolicyId final {
  uint16_t numBytes;
  static const uint16_t MAX_BYTES = 24;
  uint8_t bytes[MAX_BYTES];

  bool IsAnyPolicy() const;
  bool operator==(const CertPolicyId& other) const;

  static const CertPolicyId anyPolicy;
};

enum class TrustLevel {
  TrustAnchor = 1,         // certificate is a trusted root CA certificate or
                           // equivalent *for the given policy*.
  ActivelyDistrusted = 2,  // certificate is known to be bad
  InheritsTrust = 3        // certificate must chain to a trust anchor
};

// Extensions extracted during the verification flow.
// See TrustDomain::NoteAuxiliaryExtension.
enum class AuxiliaryExtension {
  // Certificate Transparency data, specifically Signed Certificate
  // Timestamps (SCTs). See RFC 6962.

  // SCT list embedded in the end entity certificate. Called by BuildCertChain
  // after the certificate containing the SCTs has passed the revocation checks.
  EmbeddedSCTList = 1,
  // SCT list from OCSP response. Called by VerifyEncodedOCSPResponse
  // when its result is a success and the SCT list is present.
  SCTListFromOCSPResponse = 2
};

// CertID references the information needed to do revocation checking for the
// certificate issued by the given issuer with the given serial number.
//
// issuer must be the DER-encoded issuer field from the certificate for which
// revocation checking is being done, **NOT** the subject field of the issuer
// certificate. (Those two fields must be equal to each other, but they may not
// be encoded exactly the same, and the encoding matters for OCSP.)
// issuerSubjectPublicKeyInfo is the entire DER-encoded subjectPublicKeyInfo
// field from the issuer's certificate. serialNumber is the entire DER-encoded
// serial number from the subject certificate (the certificate for which we are
// checking the revocation status).
struct CertID final {
 public:
  CertID(Input aIssuer, Input aIssuerSubjectPublicKeyInfo, Input aSerialNumber)
      : issuer(aIssuer),
        issuerSubjectPublicKeyInfo(aIssuerSubjectPublicKeyInfo),
        serialNumber(aSerialNumber) {}
  const Input issuer;
  const Input issuerSubjectPublicKeyInfo;
  const Input serialNumber;

  void operator=(const CertID&) = delete;
};
typedef std::unique_ptr<CertID> ScopedCertID;

class DERArray {
 public:
  // Returns the number of DER-encoded items in the array.
  virtual size_t GetLength() const = 0;

  // Returns a weak (non-owning) pointer the ith DER-encoded item in the array
  // (0-indexed). The result is guaranteed to be non-null if i < GetLength(),
  // and the result is guaranteed to be nullptr if i >= GetLength().
  virtual const Input* GetDER(size_t i) const = 0;

 protected:
  DERArray() {}
  virtual ~DERArray() {}
};

// Applications control the behavior of path building and verification by
// implementing the TrustDomain interface. The TrustDomain is used for all
// cryptography and for determining which certificates are trusted or
// distrusted.
class TrustDomain {
 public:
  virtual ~TrustDomain() {}

  // Determine the level of trust in the given certificate for the given role.
  // This will be called for every certificate encountered during path
  // building.
  //
  // When policy.IsAnyPolicy(), then no policy-related checking should be done.
  // When !policy.IsAnyPolicy(), then GetCertTrust MUST NOT return with
  // trustLevel == TrustAnchor unless the given cert is considered a trust
  // anchor *for that policy*. In particular, if the user has marked an
  // intermediate certificate as trusted, but that intermediate isn't in the
  // list of EV roots, then GetCertTrust must result in
  // trustLevel == InheritsTrust instead of trustLevel == TrustAnchor
  // (assuming the candidate cert is not actively distrusted).
  virtual Result GetCertTrust(EndEntityOrCA endEntityOrCA,
                              const CertPolicyId& policy,
                              Input candidateCertDER,
                              /*out*/ TrustLevel& trustLevel) = 0;

  class IssuerChecker {
   public:
    // potentialIssuerDER is the complete DER encoding of the certificate to
    // be checked as a potential issuer.
    //
    // If additionalNameConstraints is not nullptr then it must point to an
    // encoded NameConstraints extension value; in that case, those name
    // constraints will be checked in addition to any any name constraints
    // contained in potentialIssuerDER.
    virtual Result Check(Input potentialIssuerDER,
                         /*optional*/ const Input* additionalNameConstraints,
                         /*out*/ bool& keepGoing) = 0;

   protected:
    IssuerChecker();
    virtual ~IssuerChecker();

    IssuerChecker(const IssuerChecker&) = delete;
    void operator=(const IssuerChecker&) = delete;
  };

  // Search for a CA certificate with the given name. The implementation must
  // call checker.Check with the DER encoding of the potential issuer
  // certificate. The implementation must follow these rules:
  //
  // * The implementation must be reentrant and must limit the amount of stack
  //   space it uses; see the note on reentrancy and stack usage below.
  // * When checker.Check does not return Success then immediately return its
  //   return value.
  // * When checker.Check returns Success and sets keepGoing = false, then
  //   immediately return Success.
  // * When checker.Check returns Success and sets keepGoing = true, then
  //   call checker.Check again with a different potential issuer certificate,
  //   if any more are available.
  // * When no more potential issuer certificates are available, return
  //   Success.
  // * Don't call checker.Check with the same potential issuer certificate more
  //   than once in a given call of FindIssuer.
  // * The given time parameter may be used to filter out certificates that are
  //   not valid at the given time, or it may be ignored.
  //
  // Note on reentrancy and stack usage: checker.Check will attempt to
  // recursively build a certificate path from the potential issuer it is given
  // to a trusted root, as determined by this TrustDomain. That means that
  // checker.Check may call any/all of the methods on this TrustDomain. In
  // particular, there will be call stacks that look like this:
  //
  //    BuildCertChain
  //      [...]
  //        TrustDomain::FindIssuer
  //          [...]
  //            IssuerChecker::Check
  //              [...]
  //                TrustDomain::FindIssuer
  //                  [...]
  //                    IssuerChecker::Check
  //                      [...]
  //
  // checker.Check is responsible for limiting the recursion to a reasonable
  // limit.
  //
  // checker.Check will verify that the subject's issuer field matches the
  // potential issuer's subject field. It will also check that the potential
  // issuer is valid at the given time. However, if the FindIssuer
  // implementation has an efficient way of filtering potential issuers by name
  // and/or validity period itself, then it is probably better for performance
  // for it to do so.
  virtual Result FindIssuer(Input encodedIssuerName, IssuerChecker& checker,
                            Time time) = 0;

  // Called as soon as we think we have a valid chain but before revocation
  // checks are done. This function can be used to compute additional checks,
  // especially checks that require the entire certificate chain. This callback
  // can also be used to save a copy of the built certificate chain for later
  // use.
  //
  // This function may be called multiple times, regardless of whether it
  // returns success or failure. It is guaranteed that BuildCertChain will not
  // return Success unless the last call to IsChainValid returns Success.
  // Further,
  // it is guaranteed that when BuildCertChain returns Success the last chain
  // passed to IsChainValid is the valid chain that should be used for further
  // operations that require the whole chain.
  //
  // Keep in mind, in particular, that if the application saves a copy of the
  // certificate chain the last invocation of IsChainValid during a validation,
  // it is still possible for BuildCertChain to fail, in which case the
  // application must not assume anything about the validity of the last
  // certificate chain passed to IsChainValid; especially, it would be very
  // wrong to assume that the certificate chain is valid.
  //
  // certChain.GetDER(0) is the trust anchor.
  virtual Result IsChainValid(const DERArray& certChain, Time time,
                              const CertPolicyId& requiredPolicy) = 0;

  virtual Result CheckRevocation(EndEntityOrCA endEntityOrCA,
                                 const CertID& certID, Time time,
                                 Duration validityDuration,
                                 /*optional*/ const Input* stapledOCSPresponse,
                                 /*optional*/ const Input* aiaExtension) = 0;

  // Check that the given digest algorithm is acceptable for use in signatures.
  //
  // Return Success if the algorithm is acceptable,
  // Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED if the algorithm is not
  // acceptable, or another error code if another error occurred.
  virtual Result CheckSignatureDigestAlgorithm(DigestAlgorithm digestAlg,
                                               EndEntityOrCA endEntityOrCA,
                                               Time notBefore) = 0;

  // Check that the RSA public key size is acceptable.
  //
  // Return Success if the key size is acceptable,
  // Result::ERROR_INADEQUATE_KEY_SIZE if the key size is not acceptable,
  // or another error code if another error occurred.
  virtual Result CheckRSAPublicKeyModulusSizeInBits(
      EndEntityOrCA endEntityOrCA, unsigned int modulusSizeInBits) = 0;

  // Verify the given RSA PKCS#1.5 signature on the given digest using the
  // given RSA public key.
  //
  // CheckRSAPublicKeyModulusSizeInBits will be called before calling this
  // function, so it is not necessary to repeat those checks here. However,
  // VerifyRSAPKCS1SignedData *is* responsible for doing the mathematical
  // verification of the public key validity as specified in NIST SP 800-56A.
  virtual Result VerifyRSAPKCS1SignedData(Input data,
                                          DigestAlgorithm digestAlgorithm,
                                          Input signature,
                                          Input subjectPublicKeyInfo) = 0;

  // Verify the given RSA-PSS signature on the given digest using the
  // given RSA public key.
  //
  // CheckRSAPublicKeyModulusSizeInBits will be called before calling this
  // function, so it is not necessary to repeat those checks here.
  virtual Result VerifyRSAPSSSignedData(Input data,
                                        DigestAlgorithm digestAlgorithm,
                                        Input signature,
                                        Input subjectPublicKeyInfo) = 0;

  // Check that the given named ECC curve is acceptable for ECDSA signatures.
  //
  // Return Success if the curve is acceptable,
  // Result::ERROR_UNSUPPORTED_ELLIPTIC_CURVE if the curve is not acceptable,
  // or another error code if another error occurred.
  virtual Result CheckECDSACurveIsAcceptable(EndEntityOrCA endEntityOrCA,
                                             NamedCurve curve) = 0;

  // Verify the given ECDSA signature on the given digest using the given ECC
  // public key.
  //
  // CheckECDSACurveIsAcceptable will be called before calling this function,
  // so it is not necessary to repeat that check here. However,
  // VerifyECDSASignedData *is* responsible for doing the mathematical
  // verification of the public key validity as specified in NIST SP 800-56A.
  virtual Result VerifyECDSASignedData(Input data,
                                       DigestAlgorithm digestAlgorithm,
                                       Input signature,
                                       Input subjectPublicKeyInfo) = 0;

  // Verify the given ML-DSA-87 signature (NIST FIPS 204, pure-signature).
  // No digest algorithm — the full tbsCertificate bytes are passed directly.
  virtual Result VerifyMLDSASignedData(Input data,
                                       Input signature,
                                       Input subjectPublicKeyInfo) = 0;

  // Check that the validity duration is acceptable.
  //
  // Return Success if the validity duration is acceptable,
  // Result::ERROR_VALIDITY_TOO_LONG if the validity duration is not acceptable,
  // or another error code if another error occurred.
  virtual Result CheckValidityIsAcceptable(Time notBefore, Time notAfter,
                                           EndEntityOrCA endEntityOrCA,
                                           KeyPurposeId keyPurpose) = 0;

  // Some certificate or OCSP response extensions do not directly participate
  // in the verification flow, but might still be of interest to the clients
  // (notably Certificate Transparency data, RFC 6962). Such extensions are
  // extracted and passed to this function for further processing.
  virtual void NoteAuxiliaryExtension(AuxiliaryExtension extension,
                                      Input extensionData) = 0;

  // Compute a digest of the data in item using the given digest algorithm.
  //
  // item contains the data to hash.
  // digestBuf points to a buffer to where the digest will be written.
  // digestBufLen will be the size of the digest output (20 for SHA-1,
  // 32 for SHA-256, etc.).
  //
  // TODO: Taking the output buffer as (uint8_t*, size_t) is counter to our
  // other, extensive, memory safety efforts in mozilla::pkix, and we should
  // find a way to provide a more-obviously-safe interface.
  virtual Result DigestBuf(Input item, DigestAlgorithm digestAlg,
                           /*out*/ uint8_t* digestBuf, size_t digestBufLen) = 0;

 protected:
  TrustDomain() {}

  TrustDomain(const TrustDomain&) = delete;
  void operator=(const TrustDomain&) = delete;
};

enum class FallBackToSearchWithinSubject { No = 0, Yes = 1 };
enum class HandleInvalidSubjectAlternativeNamesBy { Halting = 0, Skipping = 1 };

// Applications control the behavior of matching presented name information from
// a certificate against a reference hostname by implementing the
// NameMatchingPolicy interface. Used in concert with CheckCertHostname.
class NameMatchingPolicy {
 public:
  virtual ~NameMatchingPolicy() {}

  // Given that the certificate in question has a notBefore field with the given
  // value, should name matching fall back to searching within the subject
  // common name field?
  virtual Result FallBackToCommonName(
      Time notBefore,
      /*out*/ FallBackToSearchWithinSubject& fallBackToCommonName) = 0;

  virtual HandleInvalidSubjectAlternativeNamesBy
  HandleInvalidSubjectAlternativeNames() = 0;

 protected:
  NameMatchingPolicy() {}

  NameMatchingPolicy(const NameMatchingPolicy&) = delete;
  void operator=(const NameMatchingPolicy&) = delete;
};

class StrictNameMatchingPolicy : public NameMatchingPolicy {
 public:
  virtual Result FallBackToCommonName(
      Time notBefore,
      /*out*/ FallBackToSearchWithinSubject& fallBacktoCommonName) override;

  virtual HandleInvalidSubjectAlternativeNamesBy
  HandleInvalidSubjectAlternativeNames() override;
};
}  // namespace pkix
}  // namespace mozilla

#endif  // mozilla_pkix_pkixtypes_h
