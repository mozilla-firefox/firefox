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

#include "mozpkix/pkixcheck.h"

#include "mozpkix/pkixder.h"
#include "mozpkix/pkixutil.h"

namespace mozilla { namespace pkix {

// 4.1.1.2 signatureAlgorithm
// 4.1.2.3 signature

Result
CheckSignatureAlgorithm(TrustDomain& trustDomain,
                        EndEntityOrCA endEntityOrCA,
                        Time notBefore,
                        const der::SignedDataWithSignature& signedData,
                        Input signatureValue)
{
  // 4.1.1.2. signatureAlgorithm
  der::PublicKeyAlgorithm publicKeyAlg;
  DigestAlgorithm digestAlg;
  Reader signatureAlgorithmReader(signedData.algorithm);
  Result rv = der::SignatureAlgorithmIdentifierValue(signatureAlgorithmReader,
                                                     publicKeyAlg, digestAlg);
  if (rv != Success) {
    return rv;
  }
  rv = der::End(signatureAlgorithmReader);
  if (rv != Success) {
    return rv;
  }

  // 4.1.2.3. Signature
  der::PublicKeyAlgorithm signedPublicKeyAlg;
  DigestAlgorithm signedDigestAlg;
  Reader signedSignatureAlgorithmReader(signatureValue);
  rv = der::SignatureAlgorithmIdentifierValue(signedSignatureAlgorithmReader,
                                              signedPublicKeyAlg,
                                              signedDigestAlg);
  if (rv != Success) {
    return rv;
  }
  rv = der::End(signedSignatureAlgorithmReader);
  if (rv != Success) {
    return rv;
  }

  // "This field MUST contain the same algorithm identifier as the
  // signatureAlgorithm field in the sequence Certificate." However, it may
  // be encoded differently. In particular, one of the fields may have a NULL
  // parameter while the other one may omit the parameter field altogether, and
  // these are considered equivalent. Some certificates generation software
  // actually generates certificates like that, so we compare the parsed values
  // instead of comparing the encoded values byte-for-byte.
  //
  // Along the same lines, we accept two different OIDs for RSA-with-SHA1, and
  // we consider those OIDs to be equivalent here.
  if (publicKeyAlg != signedPublicKeyAlg || digestAlg != signedDigestAlg) {
    return Result::ERROR_SIGNATURE_ALGORITHM_MISMATCH;
  }

  // During the time of the deprecation of SHA-1 and the deprecation of RSA
  // keys of less than 2048 bits, we will encounter many certs signed using
  // SHA-1 and/or too-small RSA keys. With this in mind, we ask the trust
  // domain early on if it knows it will reject the signature purely based on
  // the digest algorithm and/or the RSA key size (if an RSA signature). This
  // is a good optimization because it completely avoids calling
  // trustDomain.FindIssuers (which may be slow) for such rejected certs, and
  // more generally it short-circuits any path building with them (which, of
  // course, is even slower).

  /* ML-DSA is a pure signature with no separate hash algorithm; skip the
   * digest-algorithm policy check entirely for this key type. */
  if (publicKeyAlg != der::PublicKeyAlgorithm::ML_DSA) {
    rv = trustDomain.CheckSignatureDigestAlgorithm(digestAlg, endEntityOrCA,
                                                   notBefore);
    if (rv != Success) {
      return rv;
    }
  }

  switch (publicKeyAlg) {
    case der::PublicKeyAlgorithm::RSA_PKCS1:
    case der::PublicKeyAlgorithm::RSA_PSS:
    {
      // The RSA computation may give a result that requires fewer bytes to
      // encode than the modulus (since it is modular arithmetic). However,
      // the last step of generating a RSA-PKCS#1.5 or -PSS signature is the
      // I2OSP procedure, which pads any such shorter result with zeros so that
      // it is exactly the same length as the modulus.
      unsigned int signatureSizeInBits = signedData.signature.GetLength() * 8u;
      return trustDomain.CheckRSAPublicKeyModulusSizeInBits(
               endEntityOrCA, signatureSizeInBits);
    }

    case der::PublicKeyAlgorithm::ECDSA:
      // In theory, we could implement a similar early-pruning optimization for
      // ECDSA curves. However, since there has been no similar deprecation for
      // for any curve that we support, the chances of us encountering a curve
      // during path building is too low to be worth bothering with.
      break;

    case der::PublicKeyAlgorithm::ML_DSA:
      /* Key size is fixed by the parameter set (ML-DSA-87); no modulus check. */
      break;

    MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM
  }

  return Success;
}

// 4.1.2.4 Issuer

Result
CheckIssuer(Input encodedIssuer)
{
  // "The issuer field MUST contain a non-empty distinguished name (DN)."
  Reader issuer(encodedIssuer);
  Reader rdns;
  Result rv = der::ExpectTagAndGetValueAtEnd(issuer, der::SEQUENCE, rdns);
  if (rv != Success) {
    return rv;
  }
  // Check that the issuer name contains at least one RDN
  // (Note: this does not check related grammar rules, such as there being one
  // or more AVAs in each RDN, or the values in AVAs not being empty strings)
  if (rdns.AtEnd()) {
    return Result::ERROR_EMPTY_ISSUER_NAME;
  }
  return Success;
}

// 4.1.2.5 Validity

Result
ParseValidity(Input encodedValidity,
              /*optional out*/ Time* notBeforeOut,
              /*optional out*/ Time* notAfterOut)
{
  Reader validity(encodedValidity);
  Time notBefore(Time::uninitialized);
  if (der::TimeChoice(validity, notBefore) != Success) {
    return Result::ERROR_INVALID_DER_TIME;
  }

  Time notAfter(Time::uninitialized);
  if (der::TimeChoice(validity, notAfter) != Success) {
    return Result::ERROR_INVALID_DER_TIME;
  }

  if (der::End(validity) != Success) {
    return Result::ERROR_INVALID_DER_TIME;
  }

  if (notBefore > notAfter) {
    return Result::ERROR_INVALID_DER_TIME;
  }

  if (notBeforeOut) {
    *notBeforeOut = notBefore;
  }
  if (notAfterOut) {
    *notAfterOut = notAfter;
  }

  return Success;
}

Result
CheckValidity(Time time, Time notBefore, Time notAfter)
{
  if (time < notBefore) {
    return Result::ERROR_NOT_YET_VALID_CERTIFICATE;
  }

  if (time > notAfter) {
    return Result::ERROR_EXPIRED_CERTIFICATE;
  }

  return Success;
}

// 4.1.2.7 Subject Public Key Info

Result
CheckSubjectPublicKeyInfoContents(Reader& input, TrustDomain& trustDomain,
                                  EndEntityOrCA endEntityOrCA)
{
  // Here, we validate the syntax and do very basic semantic validation of the
  // public key of the certificate. The intention here is to filter out the
  // types of bad inputs that are most likely to trigger non-mathematical
  // security vulnerabilities in the TrustDomain, like buffer overflows or the
  // use of unsafe elliptic curves.
  //
  // We don't check (all of) the mathematical properties of the public key here
  // because it is more efficient for the TrustDomain to do it during signature
  // verification and/or other use of the public key. In particular, we
  // delegate the arithmetic validation of the public key, as specified in
  // NIST SP800-56A section 5.6.2, to the TrustDomain, at least for now.

  Reader algorithm;
  Input subjectPublicKey;
  Result rv = der::ExpectTagAndGetValue(input, der::SEQUENCE, algorithm);
  if (rv != Success) {
    return rv;
  }
  rv = der::BitStringWithNoUnusedBits(input, subjectPublicKey);
  if (rv != Success) {
    return rv;
  }
  rv = der::End(input);
  if (rv != Success) {
    return rv;
  }

  Reader subjectPublicKeyReader(subjectPublicKey);

  Reader algorithmOID;
  rv = der::ExpectTagAndGetValue(algorithm, der::OIDTag, algorithmOID);
  if (rv != Success) {
    return rv;
  }

  // RFC 3279 Section 2.3.1
  // python DottedOIDToCode.py rsaEncryption 1.2.840.113549.1.1.1
  static const uint8_t rsaEncryption[] = {
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01
  };

  // RFC 3279 Section 2.3.5 and RFC 5480 Section 2.1.1
  // python DottedOIDToCode.py id-ecPublicKey 1.2.840.10045.2.1
  static const uint8_t id_ecPublicKey[] = {
    0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01
  };

  // NIST FIPS 204, draft-ietf-lamps-dilithium-certificates
  // python DottedOIDToCode.py id-ML-DSA-87 2.16.840.1.101.3.4.3.19
  static const uint8_t id_ML_DSA_87[] = {
    0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x03, 0x13
  };

  if (algorithmOID.MatchRest(id_ecPublicKey)) {
    // An id-ecPublicKey AlgorithmIdentifier has a parameter that identifes
    // the curve being used. Although RFC 5480 specifies multiple forms, we
    // only supported the NamedCurve form, where the curve is identified by an
    // OID.

    Reader namedCurveOIDValue;
    rv = der::ExpectTagAndGetValue(algorithm, der::OIDTag,
                                   namedCurveOIDValue);
    if (rv != Success) {
      return rv;
    }

    // RFC 5480
    // python DottedOIDToCode.py secp256r1 1.2.840.10045.3.1.7
    static const uint8_t secp256r1[] = {
      0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07
    };

    // RFC 5480
    // python DottedOIDToCode.py secp384r1 1.3.132.0.34
    static const uint8_t secp384r1[] = {
      0x2b, 0x81, 0x04, 0x00, 0x22
    };

    // RFC 5480
    // python DottedOIDToCode.py secp521r1 1.3.132.0.35
    static const uint8_t secp521r1[] = {
      0x2b, 0x81, 0x04, 0x00, 0x23
    };

    // Matching is attempted based on a rough estimate of the commonality of the
    // elliptic curve, to minimize the number of MatchRest calls.
    NamedCurve curve;
    unsigned int bits;
    if (namedCurveOIDValue.MatchRest(secp256r1)) {
      curve = NamedCurve::secp256r1;
      bits = 256;
    } else if (namedCurveOIDValue.MatchRest(secp384r1)) {
      curve = NamedCurve::secp384r1;
      bits = 384;
    } else if (namedCurveOIDValue.MatchRest(secp521r1)) {
      curve = NamedCurve::secp521r1;
      bits = 521;
    } else {
      return Result::ERROR_UNSUPPORTED_ELLIPTIC_CURVE;
    }

    rv = trustDomain.CheckECDSACurveIsAcceptable(endEntityOrCA, curve);
    if (rv != Success) {
      return rv;
    }

    // RFC 5480 Section 2.2 says that the first octet will be 0x04 to indicate
    // an uncompressed point, which is the only encoding we support.
    uint8_t compressedOrUncompressed;
    rv = subjectPublicKeyReader.Read(compressedOrUncompressed);
    if (rv != Success) {
      return rv;
    }
    if (compressedOrUncompressed != 0x04) {
      return Result::ERROR_UNSUPPORTED_EC_POINT_FORM;
    }

    // The point is encoded as two raw (not DER-encoded) integers, each padded
    // to the bit length (rounded up to the nearest byte).
    Input point;
    rv = subjectPublicKeyReader.SkipToEnd(point);
    if (rv != Success) {
      return rv;
    }
    if (point.GetLength() != ((bits + 7) / 8u) * 2u) {
      return Result::ERROR_BAD_DER;
    }

    // XXX: We defer the mathematical verification of the validity of the point
    // until signature verification. This means that if we never verify a
    // signature, we'll never fully check whether the public key is valid.
  } else if (algorithmOID.MatchRest(rsaEncryption)) {
    // RFC 3279 Section 2.3.1 says "The parameters field MUST have ASN.1 type
    // NULL for this algorithm identifier."
    rv = der::ExpectTagAndEmptyValue(algorithm, der::NULLTag);
    if (rv != Success) {
      return rv;
    }

    // RSAPublicKey :: = SEQUENCE{
    //    modulus            INTEGER,    --n
    //    publicExponent     INTEGER  }  --e
    rv = der::Nested(subjectPublicKeyReader, der::SEQUENCE,
                     [&trustDomain, endEntityOrCA](Reader& r) {
      Input modulus;
      Input::size_type modulusSignificantBytes;
      Result nestedRv =
        der::PositiveInteger(r, modulus, &modulusSignificantBytes);
      if (nestedRv != Success) {
        return nestedRv;
      }
      // XXX: Should we do additional checks of the modulus?
      nestedRv = trustDomain.CheckRSAPublicKeyModulusSizeInBits(
        endEntityOrCA, modulusSignificantBytes * 8u);
      if (nestedRv != Success) {
        return nestedRv;
      }

      // XXX: We don't allow the TrustDomain to validate the exponent.
      // XXX: We don't do our own sanity checking of the exponent.
      Input exponent;
      return der::PositiveInteger(r, exponent);
    });
    if (rv != Success) {
      return rv;
    }
  } else if (algorithmOID.MatchRest(id_ML_DSA_87)) {
    // NIST FIPS 204, draft-ietf-lamps-dilithium-certificates.
    // AlgorithmIdentifier parameters MUST be absent for ML-DSA-87.
    // The subjectPublicKey is a raw 2592-byte ML-DSA-87 public key.
    Input rawKey;
    rv = subjectPublicKeyReader.SkipToEnd(rawKey);
    if (rv != Success) {
      return rv;
    }
  } else {
    return Result::ERROR_UNSUPPORTED_KEYALG;
  }

  rv = der::End(algorithm);
  if (rv != Success) {
    return rv;
  }
  rv = der::End(subjectPublicKeyReader);
  if (rv != Success) {
    return rv;
  }

  return Success;
}

Result
CheckSubjectPublicKeyInfo(Input subjectPublicKeyInfo, TrustDomain& trustDomain,
                          EndEntityOrCA endEntityOrCA)
{
  Reader spkiReader(subjectPublicKeyInfo);
  Result rv = der::Nested(spkiReader, der::SEQUENCE, [&](Reader& r) {
    return CheckSubjectPublicKeyInfoContents(r, trustDomain, endEntityOrCA);
  });
  if (rv != Success) {
    return rv;
  }
  return der::End(spkiReader);
}

// 4.2.1.3. Key Usage (id-ce-keyUsage)

// As explained in the comment in CheckKeyUsage, bit 0 is the most significant
// bit and bit 7 is the least significant bit.
inline uint8_t KeyUsageToBitMask(KeyUsage keyUsage)
{
  assert(keyUsage != KeyUsage::noParticularKeyUsageRequired);
  return 0x80u >> static_cast<uint8_t>(keyUsage);
}

Result
CheckKeyUsage(EndEntityOrCA endEntityOrCA, const Input* encodedKeyUsage,
              KeyUsage requiredKeyUsageIfPresent)
{
  if (!encodedKeyUsage) {
    // TODO(bug 970196): Reject certificates that are being used to verify
    // certificate signatures unless the certificate is a trust anchor, to
    // reduce the chances of an end-entity certificate being abused as a CA
    // certificate.
    // if (endEntityOrCA == EndEntityOrCA::MustBeCA && !isTrustAnchor) {
    //   return Result::ERROR_INADEQUATE_KEY_USAGE;
    // }
    //
    // TODO: Users may configure arbitrary certificates as trust anchors, not
    // just roots. We should only allow a certificate without a key usage to be
    // used as a CA when it is self-issued and self-signed.
    return Success;
  }

  Reader input(*encodedKeyUsage);
  Reader value;
  if (der::ExpectTagAndGetValueAtEnd(input, der::BIT_STRING, value) != Success) {
    return Result::ERROR_INADEQUATE_KEY_USAGE;
  }

  uint8_t numberOfPaddingBits;
  if (value.Read(numberOfPaddingBits) != Success) {
    return Result::ERROR_INADEQUATE_KEY_USAGE;
  }
  if (numberOfPaddingBits > 7) {
    return Result::ERROR_INADEQUATE_KEY_USAGE;
  }

  uint8_t bits;
  if (value.Read(bits) != Success) {
    // Reject empty bit masks.
    return Result::ERROR_INADEQUATE_KEY_USAGE;
  }

  // The most significant bit is numbered 0 (digitalSignature) and the least
  // significant bit is numbered 7 (encipherOnly), and the padding is in the
  // least significant bits of the last byte. The numbering of bits in a byte
  // is backwards from how we usually interpret them.
  //
  // For example, let's say bits is encoded in one byte with of value 0xB0 and
  // numberOfPaddingBits == 4. Then, bits is 10110000 in binary:
  //
  //      bit 0  bit 3
  //          |  |
  //          v  v
  //          10110000
  //              ^^^^
  //               |
  //               4 padding bits
  //
  // Since bits is the last byte, we have to consider the padding by ensuring
  // that the least significant 4 bits are all zero, since DER rules require
  // all padding bits to be zero. Then we have to look at the bit N bits to the
  // right of the most significant bit, where N is a value from the KeyUsage
  // enumeration.
  //
  // Let's say we're interested in the keyCertSign (5) bit. We'd need to look
  // at bit 5, which is zero, so keyCertSign is not asserted. (Since we check
  // that the padding is all zeros, it is OK to read from the padding bits.)
  //
  // Let's say we're interested in the digitalSignature (0) bit. We'd need to
  // look at the bit 0 (the most significant bit), which is set, so that means
  // digitalSignature is asserted. Similarly, keyEncipherment (2) and
  // dataEncipherment (3) are asserted.
  //
  // Note that since the KeyUsage enumeration is limited to values 0-7, we
  // only ever need to examine the first byte test for
  // requiredKeyUsageIfPresent.

  if (requiredKeyUsageIfPresent != KeyUsage::noParticularKeyUsageRequired) {
    // Check that the required key usage bit is set.
    if ((bits & KeyUsageToBitMask(requiredKeyUsageIfPresent)) == 0) {
      return Result::ERROR_INADEQUATE_KEY_USAGE;
    }
  }

  // RFC 5280 says "The keyCertSign bit is asserted when the subject public
  // key is used for verifying signatures on public key certificates. If the
  // keyCertSign bit is asserted, then the cA bit in the basic constraints
  // extension (Section 4.2.1.9) MUST also be asserted."
  // However, we allow end-entity certificates (i.e. certificates without
  // basicConstraints.cA set to TRUE) to claim keyCertSign for compatibility
  // reasons. This does not compromise security because we only allow
  // certificates with basicConstraints.cA set to TRUE to act as CAs.
  if (requiredKeyUsageIfPresent == KeyUsage::keyCertSign &&
      endEntityOrCA != EndEntityOrCA::MustBeCA) {
    return Result::ERROR_INADEQUATE_KEY_USAGE;
  }

  // The padding applies to the last byte, so skip to the last byte.
  while (!value.AtEnd()) {
    if (value.Read(bits) != Success) {
      return Result::ERROR_INADEQUATE_KEY_USAGE;
    }
  }

  // All of the padding bits must be zero, according to DER rules.
  uint8_t paddingMask = static_cast<uint8_t>((1 << numberOfPaddingBits) - 1);
  if ((bits & paddingMask) != 0) {
    return Result::ERROR_INADEQUATE_KEY_USAGE;
  }

  return Success;
}

// RFC5820 4.2.1.4. Certificate Policies

// "The user-initial-policy-set contains the special value any-policy if the
// user is not concerned about certificate policy."
//
// python DottedOIDToCode.py anyPolicy 2.5.29.32.0

static const uint8_t anyPolicy[] = {
  0x55, 0x1d, 0x20, 0x00
};

/*static*/ const CertPolicyId CertPolicyId::anyPolicy = {
  4, { 0x55, 0x1d, 0x20, 0x00 }
};

bool
CertPolicyId::IsAnyPolicy() const {
  if (this == &CertPolicyId::anyPolicy) {
    return true;
  }
  return numBytes == sizeof(::mozilla::pkix::anyPolicy) &&
         std::equal(bytes, bytes + numBytes, ::mozilla::pkix::anyPolicy);
}

bool
CertPolicyId::operator==(const CertPolicyId& other) const
{
  return numBytes == other.numBytes &&
         std::equal(bytes, bytes + numBytes, other.bytes);
}

// certificatePolicies ::= SEQUENCE SIZE (1..MAX) OF PolicyInformation
Result
CheckCertificatePolicies(EndEntityOrCA endEntityOrCA,
                         const Input* encodedCertificatePolicies,
                         const Input* encodedInhibitAnyPolicy,
                         TrustLevel trustLevel,
                         const CertPolicyId& requiredPolicy)
{
  if (requiredPolicy.numBytes == 0 ||
      requiredPolicy.numBytes > sizeof requiredPolicy.bytes) {
    return Result::FATAL_ERROR_INVALID_ARGS;
  }

  bool requiredPolicyFound = requiredPolicy.IsAnyPolicy();
  if (requiredPolicyFound) {
    return Success;
  }

  // Bug 989051. Until we handle inhibitAnyPolicy we will fail close when
  // inhibitAnyPolicy extension is present and we are validating for a policy.
  if (!requiredPolicyFound && encodedInhibitAnyPolicy) {
    return Result::ERROR_POLICY_VALIDATION_FAILED;
  }

  // The root CA certificate may omit the policies that it has been
  // trusted for, so we cannot require the policies to be present in those
  // certificates. Instead, the determination of which roots are trusted for
  // which policies is made by the TrustDomain's GetCertTrust method.
  if (trustLevel == TrustLevel::TrustAnchor &&
      endEntityOrCA == EndEntityOrCA::MustBeCA) {
    requiredPolicyFound = true;
  }

  Input requiredPolicyDER;
  if (requiredPolicyDER.Init(requiredPolicy.bytes, requiredPolicy.numBytes)
        != Success) {
    return Result::FATAL_ERROR_INVALID_ARGS;
  }

  if (encodedCertificatePolicies) {
    Reader extension(*encodedCertificatePolicies);
    Reader certificatePolicies;
    Result rv = der::ExpectTagAndGetValue(extension, der::SEQUENCE,
                                          certificatePolicies);
    if (rv != Success) {
      return Result::ERROR_POLICY_VALIDATION_FAILED;
    }
    if (!extension.AtEnd()) {
      return Result::ERROR_POLICY_VALIDATION_FAILED;
    }

    do {
      // PolicyInformation ::= SEQUENCE {
      //         policyIdentifier   CertPolicyId,
      //         policyQualifiers   SEQUENCE SIZE (1..MAX) OF
      //                                 PolicyQualifierInfo OPTIONAL }
      Reader policyInformation;
      rv = der::ExpectTagAndGetValue(certificatePolicies, der::SEQUENCE,
                                     policyInformation);
      if (rv != Success) {
        return Result::ERROR_POLICY_VALIDATION_FAILED;
      }

      Reader policyIdentifier;
      rv = der::ExpectTagAndGetValue(policyInformation, der::OIDTag,
                                     policyIdentifier);
      if (rv != Success) {
        return rv;
      }

      if (policyIdentifier.MatchRest(requiredPolicyDER)) {
        requiredPolicyFound = true;
      } else if (endEntityOrCA == EndEntityOrCA::MustBeCA &&
                 policyIdentifier.MatchRest(anyPolicy)) {
        requiredPolicyFound = true;
      }

      // RFC 5280 Section 4.2.1.4 says "Optional qualifiers, which MAY be
      // present, are not expected to change the definition of the policy." Also,
      // it seems that Section 6, which defines validation, does not require any
      // matching of qualifiers. Thus, doing anything with the policy qualifiers
      // would be a waste of time and a source of potential incompatibilities, so
      // we just ignore them.
    } while (!requiredPolicyFound && !certificatePolicies.AtEnd());
  }

  if (!requiredPolicyFound) {
    return Result::ERROR_POLICY_VALIDATION_FAILED;
  }

  return Success;
}

static const long UNLIMITED_PATH_LEN = -1; // must be less than zero

//  BasicConstraints ::= SEQUENCE {
//          cA                      BOOLEAN DEFAULT FALSE,
//          pathLenConstraint       INTEGER (0..MAX) OPTIONAL }

// RFC5280 4.2.1.9. Basic Constraints (id-ce-basicConstraints)
Result
CheckBasicConstraints(EndEntityOrCA endEntityOrCA,
                      const Input* encodedBasicConstraints,
                      const der::Version version, TrustLevel trustLevel,
                      unsigned int subCACount)
{
  bool isCA = false;
  long pathLenConstraint = UNLIMITED_PATH_LEN;

  if (encodedBasicConstraints) {
    Reader input(*encodedBasicConstraints);
    Result rv = der::Nested(input, der::SEQUENCE,
                            [&isCA, &pathLenConstraint](Reader& r) {
      Result nestedRv = der::OptionalBoolean(r, isCA);
      if (nestedRv != Success) {
        return nestedRv;
      }
      // TODO(bug 985025): If isCA is false, pathLenConstraint
      // MUST NOT be included (as per RFC 5280 section
      // 4.2.1.9), but for compatibility reasons, we don't
      // check this.
      return der::OptionalInteger(r, UNLIMITED_PATH_LEN, pathLenConstraint);
    });
    if (rv != Success) {
      return Result::ERROR_EXTENSION_VALUE_INVALID;
    }
    if (der::End(input) != Success) {
      return Result::ERROR_EXTENSION_VALUE_INVALID;
    }
  } else {
    // "If the basic constraints extension is not present in a version 3
    //  certificate, or the extension is present but the cA boolean is not
    //  asserted, then the certified public key MUST NOT be used to verify
    //  certificate signatures."
    //
    // For compatibility, we must accept v1 trust anchors without basic
    // constraints as CAs.
    //
    // There are devices with v1 certificates that are unlikely to be trust
    // anchors. In order to allow applications to treat this case differently
    // from other basic constraints violations (e.g. allowing certificate error
    // overrides for only this case), we return a different error code.
    //
    // TODO: add check for self-signedness?
    if (endEntityOrCA == EndEntityOrCA::MustBeCA && version == der::Version::v1) {
      if (trustLevel == TrustLevel::TrustAnchor) {
        isCA = true;
      } else {
        return Result::ERROR_V1_CERT_USED_AS_CA;
      }
    }
  }

  if (endEntityOrCA == EndEntityOrCA::MustBeEndEntity) {
    // CA certificates are not trusted as EE certs.

    if (isCA) {
      // Note that this check prevents a delegated OCSP response signing
      // certificate with the CA bit from successfully validating when we check
      // it from pkixocsp.cpp, which is a good thing.
      return Result::ERROR_CA_CERT_USED_AS_END_ENTITY;
    }

    return Success;
  }

  assert(endEntityOrCA == EndEntityOrCA::MustBeCA);

  // End-entity certificates are not allowed to act as CA certs.
  if (!isCA) {
    return Result::ERROR_CA_CERT_INVALID;
  }

  if (pathLenConstraint >= 0 &&
      static_cast<long>(subCACount) > pathLenConstraint) {
    return Result::ERROR_PATH_LEN_CONSTRAINT_INVALID;
  }

  return Success;
}

// 4.2.1.12. Extended Key Usage (id-ce-extKeyUsage)

static Result
MatchEKU(Reader& value, KeyPurposeId requiredEKU,
         /*in/out*/ bool& found, /*in/out*/ bool& foundOCSPSigning)
{
  // See Section 5.9 of "A Layman's Guide to a Subset of ASN.1, BER, and DER"
  // for a description of ASN.1 DER encoding of OIDs.

  // id-pkix  OBJECT IDENTIFIER  ::=
  //            { iso(1) identified-organization(3) dod(6) internet(1)
  //                    security(5) mechanisms(5) pkix(7) }
  // id-kp OBJECT IDENTIFIER ::= { id-pkix 3 }
  // id-kp-serverAuth      OBJECT IDENTIFIER ::= { id-kp 1 }
  // id-kp-clientAuth      OBJECT IDENTIFIER ::= { id-kp 2 }
  // id-kp-codeSigning     OBJECT IDENTIFIER ::= { id-kp 3 }
  // id-kp-emailProtection OBJECT IDENTIFIER ::= { id-kp 4 }
  // id-kp-OCSPSigning     OBJECT IDENTIFIER ::= { id-kp 9 }
  static const uint8_t server[] = { (40*1)+3, 6, 1, 5, 5, 7, 3, 1 };
  static const uint8_t client[] = { (40*1)+3, 6, 1, 5, 5, 7, 3, 2 };
  static const uint8_t code  [] = { (40*1)+3, 6, 1, 5, 5, 7, 3, 3 };
  static const uint8_t email [] = { (40*1)+3, 6, 1, 5, 5, 7, 3, 4 };
  static const uint8_t ocsp  [] = { (40*1)+3, 6, 1, 5, 5, 7, 3, 9 };
  // RFC 9336 - X.509 Certificate General-Purpose Extended Key Usage (EKU) for
  // Document Signing id-kp-documentSigning OBJECT IDENTIFIER ::= { id-kp 36 }
  static const uint8_t documentSigning[] = { (40*1)+3, 6, 1, 5, 5, 7, 3, 36 };
  // 1.2.840.113583.1.1.5 (Adobe Authentic Documents Trust)
  // {iso(1) member-body(2) us(840) adbe(113583) acrobat(1) security(1) 5}
  static const uint8_t documentSigningAdobe[] = { (40*1)+2, 128+6, 72, 128+6, 128+119, 47, 1, 1, 5 };
  // 1.3.6.1.4.1.311.10.3.12 (Microsoft Document Signing OID)
  // {iso(1) identified-organization(3) dod(6) internet(1) private(4) enterprise(1) 311 10 3 12}
  static const uint8_t documentSigningMicrosoft[] = { (40*1)+3, 6, 1, 4, 1, 128+2, 55, 10, 3, 12 };

  bool match = false;

  if (!found) {
    switch (requiredEKU) {
      case KeyPurposeId::id_kp_serverAuth:
        match = value.MatchRest(server);
        break;

      case KeyPurposeId::id_kp_clientAuth:
        match = value.MatchRest(client);
        break;

      case KeyPurposeId::id_kp_codeSigning:
        match = value.MatchRest(code);
        break;

      case KeyPurposeId::id_kp_emailProtection:
        match = value.MatchRest(email);
        break;

      case KeyPurposeId::id_kp_OCSPSigning:
        match = value.MatchRest(ocsp);
        break;

      case KeyPurposeId::id_kp_documentSigning:
        match = value.MatchRest(documentSigning);
        break;

      case KeyPurposeId::id_kp_documentSigningAdobe:
        match = value.MatchRest(documentSigningAdobe);
        break;

      case KeyPurposeId::id_kp_documentSigningMicrosoft:
        match = value.MatchRest(documentSigningMicrosoft);
        break;

      case KeyPurposeId::anyExtendedKeyUsage:
        return NotReached("anyExtendedKeyUsage should start with found==true",
                          Result::FATAL_ERROR_LIBRARY_FAILURE);
    }
  }

  if (match) {
    found = true;
    if (requiredEKU == KeyPurposeId::id_kp_OCSPSigning) {
      foundOCSPSigning = true;
    }
  } else if (value.MatchRest(ocsp)) {
    foundOCSPSigning = true;
  }

  value.SkipToEnd(); // ignore unmatched OIDs.

  return Success;
}

Result
CheckExtendedKeyUsage(EndEntityOrCA endEntityOrCA,
                      const Input* encodedExtendedKeyUsage,
                      KeyPurposeId requiredEKU)
{
  // XXX: We're using Result::ERROR_INADEQUATE_CERT_TYPE here so that callers
  // can distinguish EKU mismatch from KU mismatch from basic constraints
  // mismatch. We should probably add a new error code that is more clear for
  // this type of problem.

  bool foundOCSPSigning = false;

  if (encodedExtendedKeyUsage) {
    bool found = requiredEKU == KeyPurposeId::anyExtendedKeyUsage;

    Reader input(*encodedExtendedKeyUsage);
    Result rv = der::NestedOf(input, der::SEQUENCE, der::OIDTag,
                              der::EmptyAllowed::No, [&](Reader& r) {
      return MatchEKU(r, requiredEKU, found, foundOCSPSigning);
    });
    if (rv != Success) {
      return Result::ERROR_INADEQUATE_CERT_TYPE;
    }
    if (der::End(input) != Success) {
      return Result::ERROR_INADEQUATE_CERT_TYPE;
    }

    // If the EKU extension was included, then the required EKU must be in the
    // list.
    if (!found) {
      return Result::ERROR_INADEQUATE_CERT_TYPE;
    }
  }

  // pkixocsp.cpp depends on the following additional checks.

  if (endEntityOrCA == EndEntityOrCA::MustBeEndEntity) {
    // When validating anything other than an delegated OCSP signing cert,
    // reject any cert that also claims to be an OCSP responder, because such
    // a cert does not make sense. For example, if an SSL certificate were to
    // assert id-kp-OCSPSigning then it could sign OCSP responses for itself,
    // if not for this check.
    // That said, we accept CA certificates with id-kp-OCSPSigning because
    // some CAs in Mozilla's CA program have issued such intermediate
    // certificates, and because some CAs have reported some Microsoft server
    // software wrongly requires CA certificates to have id-kp-OCSPSigning.
    // Allowing this exception does not cause any security issues because we
    // require delegated OCSP response signing certificates to be end-entity
    // certificates.
    if (foundOCSPSigning && requiredEKU != KeyPurposeId::id_kp_OCSPSigning) {
      return Result::ERROR_INADEQUATE_CERT_TYPE;
    }
    // http://tools.ietf.org/html/rfc6960#section-4.2.2.2:
    // "OCSP signing delegation SHALL be designated by the inclusion of
    // id-kp-OCSPSigning in an extended key usage certificate extension
    // included in the OCSP response signer's certificate."
    //
    // id-kp-OCSPSigning is the only EKU that isn't implicitly assumed when the
    // EKU extension is missing from an end-entity certificate. However, any CA
    // certificate can issue a delegated OCSP response signing certificate, so
    // we can't require the EKU be explicitly included for CA certificates.
    if (!foundOCSPSigning && requiredEKU == KeyPurposeId::id_kp_OCSPSigning) {
      return Result::ERROR_INADEQUATE_CERT_TYPE;
    }
  }

  return Success;
}

Result
CheckTLSFeatures(const BackCert& subject, BackCert& potentialIssuer)
{
  const Input* issuerTLSFeatures = potentialIssuer.GetRequiredTLSFeatures();
  if (!issuerTLSFeatures) {
    return Success;
  }

  const Input* subjectTLSFeatures = subject.GetRequiredTLSFeatures();
  if (issuerTLSFeatures->GetLength() == 0 ||
      !subjectTLSFeatures ||
      !InputsAreEqual(*issuerTLSFeatures, *subjectTLSFeatures)) {
    return Result::ERROR_REQUIRED_TLS_FEATURE_MISSING;
  }

  return Success;
}

Result
TLSFeaturesSatisfiedInternal(const Input* requiredTLSFeatures,
                             const Input* stapledOCSPResponse)
{
  if (!requiredTLSFeatures) {
    return Success;
  }

  // RFC 6066 10.2: ExtensionType status_request
  const static uint8_t status_request = 5;
  const static uint8_t status_request_bytes[] = { status_request };

  Reader input(*requiredTLSFeatures);
  Result rv = der::NestedOf(input, der::SEQUENCE, der::INTEGER,
                       der::EmptyAllowed::No, [&](Reader& r) {
    if (!r.MatchRest(status_request_bytes)) {
      return Result::ERROR_REQUIRED_TLS_FEATURE_MISSING;
    }

    if (!stapledOCSPResponse) {
      return Result::ERROR_REQUIRED_TLS_FEATURE_MISSING;
    }

    return Result::Success;
  });
  if (rv != Success) {
    return rv;
  }
  return der::End(input);
}

Result
CheckTLSFeaturesAreSatisfied(Input& cert,
                             const Input* stapledOCSPResponse)
{
  BackCert backCert(cert, EndEntityOrCA::MustBeEndEntity, nullptr);
  Result rv = backCert.Init();
  if (rv != Success) {
    return rv;
  }

  return TLSFeaturesSatisfiedInternal(backCert.GetRequiredTLSFeatures(),
                                      stapledOCSPResponse);
}

Result
CheckIssuerIndependentProperties(TrustDomain& trustDomain,
                                 const BackCert& cert,
                                 Time time,
                                 KeyUsage requiredKeyUsageIfPresent,
                                 KeyPurposeId requiredEKUIfPresent,
                                 const CertPolicyId& requiredPolicy,
                                 unsigned int subCACount,
                                 /*out*/ TrustLevel& trustLevel)
{
  Result rv;

  const EndEntityOrCA endEntityOrCA = cert.endEntityOrCA;

  // Check the cert's trust first, because we want to minimize the amount of
  // processing we do on a distrusted cert, in case it is trying to exploit
  // some bug in our processing.
  rv = trustDomain.GetCertTrust(endEntityOrCA, requiredPolicy, cert.GetDER(),
                                trustLevel);
  if (rv != Success) {
    return rv;
  }

  // IMPORTANT: We parse the validity interval here, so that we can use the
  // notBefore and notAfter values in checks for things that might be deprecated
  // over time. However, we must not fail for semantic errors until the end of
  // this method, in order to preserve error ranking.
  Time notBefore(Time::uninitialized);
  Time notAfter(Time::uninitialized);
  rv = ParseValidity(cert.GetValidity(), &notBefore, &notAfter);
  if (rv != Success) {
    return rv;
  }

  if (trustLevel == TrustLevel::TrustAnchor &&
      endEntityOrCA == EndEntityOrCA::MustBeEndEntity &&
      requiredEKUIfPresent == KeyPurposeId::id_kp_OCSPSigning) {
    // OCSP signer certificates can never be trust anchors, especially
    // since we don't support designated OCSP responders. All of the checks
    // below that are dependent on trustLevel rely on this overriding of the
    // trust level for OCSP signers.
    trustLevel = TrustLevel::InheritsTrust;
  }

  switch (trustLevel) {
    case TrustLevel::InheritsTrust:
      rv = CheckSignatureAlgorithm(trustDomain, endEntityOrCA, notBefore,
                                   cert.GetSignedData(), cert.GetSignature());
      if (rv != Success) {
        return rv;
      }
      break;

    case TrustLevel::TrustAnchor:
      // We don't even bother checking signatureAlgorithm or signature for
      // syntactic validity for trust anchors, because we don't use those
      // fields for anything, and because the trust anchor might be signed
      // with a signature algorithm we don't actually support.
      break;

    case TrustLevel::ActivelyDistrusted:
      return Result::ERROR_UNTRUSTED_CERT;
  }

  // Check the SPKI early, because it is one of the most selective properties
  // of the certificate due to SHA-1 deprecation and the deprecation of
  // certificates with keys weaker than RSA 2048.
  rv = CheckSubjectPublicKeyInfo(cert.GetSubjectPublicKeyInfo(), trustDomain,
                                 endEntityOrCA);
  if (rv != Success) {
    return rv;
  }

  // 4.1.2.4. Issuer
  rv = CheckIssuer(cert.GetIssuer());
  if (rv != Success) {
    return rv;
  }

  // 4.2.1.1. Authority Key Identifier is ignored (see bug 965136).

  // 4.2.1.2. Subject Key Identifier is ignored (see bug 965136).

  // 4.2.1.3. Key Usage
  rv = CheckKeyUsage(endEntityOrCA, cert.GetKeyUsage(),
                     requiredKeyUsageIfPresent);
  if (rv != Success) {
    return rv;
  }

  // 4.2.1.4. Certificate Policies
  rv = CheckCertificatePolicies(endEntityOrCA, cert.GetCertificatePolicies(),
                                cert.GetInhibitAnyPolicy(), trustLevel,
                                requiredPolicy);
  if (rv != Success) {
    return rv;
  }

  // 4.2.1.5. Policy Mappings are not supported; see the documentation about
  //          policy enforcement in pkix.h.

  // 4.2.1.6. Subject Alternative Name dealt with during name constraint
  //          checking and during name verification (CERT_VerifyCertName).

  // 4.2.1.7. Issuer Alternative Name is not something that needs checking.

  // 4.2.1.8. Subject Directory Attributes is not something that needs
  //          checking.

  // 4.2.1.9. Basic Constraints.
  rv = CheckBasicConstraints(endEntityOrCA, cert.GetBasicConstraints(),
                             cert.GetVersion(), trustLevel, subCACount);
  if (rv != Success) {
    return rv;
  }

  // 4.2.1.10. Name Constraints is dealt with in during path building.

  // 4.2.1.11. Policy Constraints are implicitly supported; see the
  //           documentation about policy enforcement in pkix.h.

  // 4.2.1.12. Extended Key Usage
  rv = CheckExtendedKeyUsage(endEntityOrCA, cert.GetExtKeyUsage(),
                             requiredEKUIfPresent);
  if (rv != Success) {
    return rv;
  }

  // 4.2.1.13. CRL Distribution Points is not supported, though the
  //           TrustDomain's CheckRevocation method may parse it and process it
  //           on its own.

  // 4.2.1.14. Inhibit anyPolicy is implicitly supported; see the documentation
  //           about policy enforcement in pkix.h.

  // IMPORTANT: Even though we parse validity above, we wait until this point to
  // check it, so that error ranking works correctly.
  rv = CheckValidity(time, notBefore, notAfter);
  if (rv != Success) {
    return rv;
  }

  rv = trustDomain.CheckValidityIsAcceptable(notBefore, notAfter, endEntityOrCA,
                                             requiredEKUIfPresent);
  if (rv != Success) {
    return rv;
  }

  return Success;
}

} } // namespace mozilla::pkix
