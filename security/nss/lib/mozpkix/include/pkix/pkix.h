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

#ifndef mozilla_pkix_pkix_h
#define mozilla_pkix_pkix_h

#include "mozpkix/pkixtypes.h"

namespace mozilla {
namespace pkix {

// ----------------------------------------------------------------------------
// LIMITED SUPPORT FOR CERTIFICATE POLICIES
//
// If SEC_OID_X509_ANY_POLICY is passed as the value of the requiredPolicy
// parameter then all policy validation will be skipped. Otherwise, path
// building and validation will be done for the given policy.
//
// In RFC 5280 terms:
//
//    * user-initial-policy-set = { requiredPolicy }.
//    * initial-explicit-policy = true
//    * initial-any-policy-inhibit = false
//
// We allow intermediate cerificates to use this extension but since
// we do not process the inhibit anyPolicy extesion we will fail if this
// extension is present. TODO(bug 989051)
// Because we force explicit policy and because we prohibit policy mapping, we
// do not bother processing the policy mapping, or policy constraint.
//
// ----------------------------------------------------------------------------
// ERROR RANKING
//
// BuildCertChain prioritizes certain checks ahead of others so that when a
// certificate chain has multiple errors, the "most serious" error is
// returned. In practice, this ranking of seriousness is tied directly to how
// Firefox's certificate error override mechanism.
//
// The ranking is:
//
//    1. Active distrust (Result::ERROR_UNTRUSTED_CERT).
//    2. Problems with issuer-independent properties for CA certificates.
//    3. Unknown issuer (Result::ERROR_UNKNOWN_ISSUER).
//    4. Problems with issuer-independent properties for EE certificates.
//    5. Revocation.
//
// In particular, if BuildCertChain returns Result::ERROR_UNKNOWN_ISSUER then
// the caller can call CERT_CheckCertValidTimes to determine if the certificate
// is ALSO expired.
//
// It would be better if revocation were prioritized above expiration and
// unknown issuer. However, it is impossible to do revocation checking without
// knowing the issuer, since the issuer information is needed to validate the
// revocation information. Also, generally revocation checking only works
// during the validity period of the certificate.
//
// In general, when path building fails, BuildCertChain will return
// Result::ERROR_UNKNOWN_ISSUER. However, if all attempted paths resulted in
// the same error (which is trivially true when there is only one potential
// path), more specific errors will be returned.
//
// ----------------------------------------------------------------------------
// Meanings of specific error codes can be found in Result.h

// This function attempts to find a trustworthy path from the supplied
// certificate to a trust anchor. In the event that no trusted path is found,
// the method returns an error result; the error ranking is described above.
//
// Parameters:
//  time:
//         Timestamp for which the chain should be valid; this is useful to
//         analyze whether a record was trustworthy when it was made.
//  requiredKeyUsageIfPresent:
//         What key usage bits must be set, if the extension is present at all,
//         to be considered a valid chain. Multiple values should be OR'd
//         together. If you don't want to specify anything, use
//         KeyUsage::noParticularKeyUsageRequired.
//  requiredEKUIfPresent:
//         What extended key usage bits must be set, if the EKU extension
//         exists, to be considered a valid chain. Multiple values should be
//         OR'd together. If you don't want to specify anything, use
//         KeyPurposeId::anyExtendedKeyUsage.
//  requiredPolicy:
//         This is the policy to apply; typically included in EV certificates.
//         If there is no policy, pass in CertPolicyId::anyPolicy.
Result BuildCertChain(TrustDomain& trustDomain, Input cert, Time time,
                      EndEntityOrCA endEntityOrCA,
                      KeyUsage requiredKeyUsageIfPresent,
                      KeyPurposeId requiredEKUIfPresent,
                      const CertPolicyId& requiredPolicy,
                      /*optional*/ const Input* stapledOCSPResponse);

// Verify that the given end-entity cert, which is assumed to have been already
// validated with BuildCertChain, is valid for the given hostname. The matching
// function attempts to implement RFC 6125 with a couple of differences:
// - IP addresses are out of scope of RFC 6125, but this method accepts them for
//   backward compatibility (see SearchNames in pkixnames.cpp)
// - A wildcard in a DNS-ID may only appear as the entirety of the first label.
// If the NameMatchingPolicy is omitted, a StrictNameMatchingPolicy is used.
Result CheckCertHostname(Input cert, Input hostname);
Result CheckCertHostname(Input cert, Input hostname,
                         NameMatchingPolicy& nameMatchingPolicy);

// Construct an RFC-6960-encoded OCSP request, ready for submission to a
// responder, for the provided CertID. The request has no extensions.
static const size_t OCSP_REQUEST_MAX_LENGTH = 127;
Result CreateEncodedOCSPRequest(TrustDomain& trustDomain, const CertID& certID,
                                /*out*/ uint8_t (&out)[OCSP_REQUEST_MAX_LENGTH],
                                /*out*/ size_t& outLen);

// The out parameter expired will be true if the response has expired. If the
// response also indicates a revoked or unknown certificate, that error
// will be returned. Otherwise, Result::ERROR_OCSP_OLD_RESPONSE will be
// returned for an expired response.
//
// The optional parameter thisUpdate will be the thisUpdate value of
// the encoded response if it is considered trustworthy. Only
// good, unknown, or revoked responses that verify correctly are considered
// trustworthy. If the response is not trustworthy, thisUpdate will be 0.
// Similarly, the optional parameter validThrough will be the time through
// which the encoded response is considered trustworthy (that is, as long as
// the given time at which to validate is less than or equal to validThrough,
// the response will be considered trustworthy).
Result VerifyEncodedOCSPResponse(
    TrustDomain& trustDomain, const CertID& certID, Time time,
    uint16_t maxLifetimeInDays, Input encodedResponse,
    /* out */ bool& expired,
    /* optional out */ Time* thisUpdate = nullptr,
    /* optional out */ Time* validThrough = nullptr);

// Check that the TLSFeature extensions in a given end-entity cert (which is
// assumed to have been already validated with BuildCertChain) are satisfied.
// The only feature which we cancurrently process a requirement for is
// status_request (OCSP stapling) so we reject any extension that specifies a
// requirement for another value. Empty extensions are also rejected.
Result CheckTLSFeaturesAreSatisfied(Input& cert,
                                    const Input* stapledOCSPResponse);

// Parse a DER-encoded certificate with BackCert::Init and return the
// certificate's subjectPublicKeyInfo.
Result FindSubjectPublicKeyInfo(Input certDER,
                                /*out*/ Input& subjectPublicKeyInfo);
}  // namespace pkix
}  // namespace mozilla

#endif  // mozilla_pkix_pkix_h
