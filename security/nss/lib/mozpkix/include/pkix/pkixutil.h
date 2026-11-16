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

#ifndef mozilla_pkix_pkixutil_h
#define mozilla_pkix_pkixutil_h

#include "mozpkix/pkixder.h"

namespace mozilla {
namespace pkix {

// During path building and verification, we build a linked list of BackCerts
// from the current cert toward the end-entity certificate. The linked list
// is used to verify properties that aren't local to the current certificate
// and/or the direct link between the current certificate and its issuer,
// such as name constraints.
//
// Each BackCert contains pointers to all the given certificate's extensions
// so that we can parse the extension block once and then process the
// extensions in an order that may be different than they appear in the cert.
class BackCert final {
 public:
  // certDER and childCert must be valid for the lifetime of BackCert.
  BackCert(Input aCertDER, EndEntityOrCA aEndEntityOrCA,
           const BackCert* aChildCert)
      : der(aCertDER),
        endEntityOrCA(aEndEntityOrCA),
        childCert(aChildCert),
        version(der::Version::Uninitialized) {}

  Result Init();

  const Input GetDER() const { return der; }
  const der::SignedDataWithSignature& GetSignedData() const {
    return signedData;
  }

  der::Version GetVersion() const { return version; }
  const Input GetSerialNumber() const { return serialNumber; }
  const Input GetSignature() const { return signature; }
  const Input GetIssuer() const { return issuer; }
  // XXX: "validity" is a horrible name for the structure that holds
  // notBefore & notAfter, but that is the name used in RFC 5280 and we use the
  // RFC 5280 names for everything.
  const Input GetValidity() const { return validity; }
  const Input GetSubject() const { return subject; }
  const Input GetSubjectPublicKeyInfo() const { return subjectPublicKeyInfo; }
  const Input* GetAuthorityInfoAccess() const {
    return MaybeInput(authorityInfoAccess);
  }
  const Input* GetBasicConstraints() const {
    return MaybeInput(basicConstraints);
  }
  const Input* GetCertificatePolicies() const {
    return MaybeInput(certificatePolicies);
  }
  const Input* GetExtKeyUsage() const { return MaybeInput(extKeyUsage); }
  const Input* GetKeyUsage() const { return MaybeInput(keyUsage); }
  const Input* GetInhibitAnyPolicy() const {
    return MaybeInput(inhibitAnyPolicy);
  }
  const Input* GetNameConstraints() const {
    return MaybeInput(nameConstraints);
  }
  const Input* GetQCStatements() const { return MaybeInput(qcStatements); }
  const Input* GetSubjectAltName() const { return MaybeInput(subjectAltName); }
  const Input* GetRequiredTLSFeatures() const {
    return MaybeInput(requiredTLSFeatures);
  }
  const Input* GetSignedCertificateTimestamps() const {
    return MaybeInput(signedCertificateTimestamps);
  }

 private:
  const Input der;

 public:
  const EndEntityOrCA endEntityOrCA;
  BackCert const* const childCert;

 private:
  // When parsing certificates in BackCert::Init, we don't accept empty
  // extensions. Consequently, we don't have to store a distinction between
  // empty extensions and extensions that weren't included. However, when
  // *processing* extensions, we distinguish between whether an extension was
  // included or not based on whetehr the GetXXX function for the extension
  // returns nullptr.
  static inline const Input* MaybeInput(const Input& item) {
    return item.GetLength() > 0 ? &item : nullptr;
  }

  der::SignedDataWithSignature signedData;

  der::Version version;
  Input serialNumber;
  Input signature;
  Input issuer;
  // XXX: "validity" is a horrible name for the structure that holds
  // notBefore & notAfter, but that is the name used in RFC 5280 and we use the
  // RFC 5280 names for everything.
  Input validity;
  Input subject;
  Input subjectPublicKeyInfo;

  Input authorityInfoAccess;
  Input basicConstraints;
  Input certificatePolicies;
  Input extKeyUsage;
  Input inhibitAnyPolicy;
  Input keyUsage;
  Input nameConstraints;
  Input subjectAltName;
  Input criticalNetscapeCertificateType;
  Input qcStatements;
  Input requiredTLSFeatures;
  Input signedCertificateTimestamps;  // RFC 6962 (Certificate Transparency)

  Result RememberExtension(Reader& extnID, Input extnValue, bool critical,
                           /*out*/ bool& understood);

  BackCert(const BackCert&) = delete;
  void operator=(const BackCert&) = delete;
};

class NonOwningDERArray final : public DERArray {
 public:
  NonOwningDERArray() : numItems(0) {
    // we don't need to initialize the items array because we always check
    // numItems before accessing i.
  }

  size_t GetLength() const override { return numItems; }

  const Input* GetDER(size_t i) const override {
    return i < numItems ? &items[i] : nullptr;
  }

  Result Append(Input der) {
    if (numItems >= MAX_LENGTH) {
      return Result::FATAL_ERROR_INVALID_ARGS;
    }
    Result rv = items[numItems].Init(der);  // structure assignment
    if (rv != Success) {
      return rv;
    }
    ++numItems;
    return Success;
  }

  // Public so we can static_assert on this. Keep in sync with MAX_SUBCA_COUNT.
  static const size_t MAX_LENGTH = 8;

 private:
  Input items[MAX_LENGTH];  // avoids any heap allocations
  size_t numItems;

  NonOwningDERArray(const NonOwningDERArray&) = delete;
  void operator=(const NonOwningDERArray&) = delete;
};

// Extracts the SignedCertificateTimestampList structure which is encoded as an
// OCTET STRING within the X.509v3 / OCSP extensions (see RFC 6962 section 3.3).
Result ExtractSignedCertificateTimestampListFromExtension(Input extnValue,
                                                          Input& sctList);

inline unsigned int DaysBeforeYear(unsigned int year) {
  assert(year <= 9999);
  return ((year - 1u) * 365u) +
         ((year - 1u) / 4u)       // leap years are every 4 years,
         - ((year - 1u) / 100u)   // except years divisible by 100,
         + ((year - 1u) / 400u);  // except years divisible by 400.
}

static const size_t MAX_DIGEST_SIZE_IN_BYTES = 512 / 8;  // sha-512

Result VerifySignedData(TrustDomain& trustDomain,
                        const der::SignedDataWithSignature& signedData,
                        Input signerSubjectPublicKeyInfo);

// Extracts the key parameters from |subjectPublicKeyInfo|, invoking
// the relevant methods of |trustDomain|.
Result CheckSubjectPublicKeyInfo(Input subjectPublicKeyInfo,
                                 TrustDomain& trustDomain,
                                 EndEntityOrCA endEntityOrCA);

// In a switch over an enum, sometimes some compilers are not satisfied that
// all control flow paths have been considered unless there is a default case.
// However, in our code, such a default case is almost always unreachable dead
// code. That can be particularly problematic when the compiler wants the code
// to choose a value, such as a return value, for the default case, but there's
// no appropriate "impossible case" value to choose.
//
// MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM accounts for this. Example:
//
//     // In xy.cpp
//     #include "xt.h"
//
//     enum class XY { X, Y };
//
//     int func(XY xy) {
//       switch (xy) {
//         case XY::X: return 1;
//         case XY::Y; return 2;
//         MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM
//       }
//     }
#if defined(__clang__)
// Clang will warn if not all cases are covered (-Wswitch-enum) AND it will
// warn if a switch statement that covers every enum label has a default case
// (-W-covered-switch-default). Versions prior to 3.5 warned about unreachable
// code in such default cases (-Wunreachable-code) even when
// -W-covered-switch-default was disabled, but that changed in Clang 3.5.
#define MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM  // empty
#elif defined(__GNUC__)
// GCC will warn if not all cases are covered (-Wswitch-enum). It does not
// assume that the default case is unreachable.
#define MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM \
  default:                                    \
    assert(false);                            \
    __builtin_unreachable();
#elif defined(_MSC_VER)
// MSVC will warn if not all cases are covered (C4061, level 4). It does not
// assume that the default case is unreachable.
#define MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM \
  default:                                    \
    assert(false);                            \
    __assume(0);
#else
#error Unsupported compiler for MOZILLA_PKIX_UNREACHABLE_DEFAULT.
#endif

inline size_t DigestAlgorithmToSizeInBytes(DigestAlgorithm digestAlgorithm) {
  switch (digestAlgorithm) {
    case DigestAlgorithm::sha1:
      return 160 / 8;
    case DigestAlgorithm::sha256:
      return 256 / 8;
    case DigestAlgorithm::sha384:
      return 384 / 8;
    case DigestAlgorithm::sha512:
      return 512 / 8;
      MOZILLA_PKIX_UNREACHABLE_DEFAULT_ENUM
  }
}
}  // namespace pkix
}  // namespace mozilla

#endif  // mozilla_pkix_pkixutil_h
