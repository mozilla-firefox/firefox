/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This code is made available to you under your choice of the following sets
 * of licensing terms:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/* Copyright 2014 Mozilla Contributors
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

#include "mozpkix/pkixutil.h"

namespace mozilla { namespace pkix {

Result
BackCert::Init()
{
  Result rv;

  // Certificate  ::=  SEQUENCE  {
  //         tbsCertificate       TBSCertificate,
  //         signatureAlgorithm   AlgorithmIdentifier,
  //         signatureValue       BIT STRING  }

  Reader tbsCertificate;

  // The scope of |certificate| is limited to this block so we
  // don't accidentally confuse it for tbsCertificate later.
  {
    Reader certificate;
    rv = der::ExpectTagAndGetValueAtEnd(der, der::SEQUENCE, certificate);
    if (rv != Success) {
      return rv;
    }
    rv = der::SignedData(certificate, tbsCertificate, signedData);
    if (rv != Success) {
      return rv;
    }
    rv = der::End(certificate);
    if (rv != Success) {
      return rv;
    }
  }

  // TBSCertificate  ::=  SEQUENCE  {
  //      version         [0]  EXPLICIT Version DEFAULT v1,
  //      serialNumber         CertificateSerialNumber,
  //      signature            AlgorithmIdentifier,
  //      issuer               Name,
  //      validity             Validity,
  //      subject              Name,
  //      subjectPublicKeyInfo SubjectPublicKeyInfo,
  //      issuerUniqueID  [1]  IMPLICIT UniqueIdentifier OPTIONAL,
  //                           -- If present, version MUST be v2 or v3
  //      subjectUniqueID [2]  IMPLICIT UniqueIdentifier OPTIONAL,
  //                           -- If present, version MUST be v2 or v3
  //      extensions      [3]  EXPLICIT Extensions OPTIONAL
  //                           -- If present, version MUST be v3
  //      }
  rv = der::OptionalVersion(tbsCertificate, version);
  if (rv != Success) {
    return rv;
  }
  rv = der::CertificateSerialNumber(tbsCertificate, serialNumber);
  if (rv != Success) {
    return rv;
  }
  rv = der::ExpectTagAndGetValue(tbsCertificate, der::SEQUENCE, signature);
  if (rv != Success) {
    return rv;
  }
  rv = der::ExpectTagAndGetTLV(tbsCertificate, der::SEQUENCE, issuer);
  if (rv != Success) {
    return rv;
  }
  rv = der::ExpectTagAndGetValue(tbsCertificate, der::SEQUENCE, validity);
  if (rv != Success) {
    return rv;
  }
  // TODO(bug XXXXXXX): We rely on the the caller of mozilla::pkix to validate
  // that the name is syntactically valid, if they care. In Gecko we do this
  // implicitly by parsing the certificate into a CERTCertificate object.
  // Instead of relying on the caller to do this, we should do it ourselves.
  rv = der::ExpectTagAndGetTLV(tbsCertificate, der::SEQUENCE, subject);
  if (rv != Success) {
    return rv;
  }
  rv = der::ExpectTagAndGetTLV(tbsCertificate, der::SEQUENCE,
                               subjectPublicKeyInfo);
  if (rv != Success) {
    return rv;
  }

  // According to RFC 5280, all fields below this line are forbidden for
  // certificate versions less than v3.  However, for compatibility reasons,
  // we parse v1/v2 certificates in the same way as v3 certificates.  So if
  // these fields appear in a v1 certificate, they will be used.

  // Ignore issuerUniqueID if present.
  rv = der::SkipOptionalImplicitPrimitiveTag(tbsCertificate, 1);
  if (rv != Success) {
    return rv;
  }

  // Ignore subjectUniqueID if present.
  rv = der::SkipOptionalImplicitPrimitiveTag(tbsCertificate, 2);
  if (rv != Success) {
    return rv;
  }

  static const uint8_t CSC = der::CONTEXT_SPECIFIC | der::CONSTRUCTED;
  rv = der::OptionalExtensions(
         tbsCertificate, CSC | 3,
         [this](Reader& extnID, const Input& extnValue, bool critical,
                /*out*/ bool& understood) {
           return RememberExtension(extnID, extnValue, critical, understood);
         });
  if (rv != Success) {
    return rv;
  }

  // The Netscape Certificate Type extension is an obsolete
  // Netscape-proprietary mechanism that we ignore in favor of the standard
  // extensions. However, some CAs have issued certificates with the Netscape
  // Cert Type extension marked critical. Thus, for compatibility reasons, we
  // "understand" this extension by ignoring it when it is not critical, and
  // by ensuring that the equivalent standardized extensions are present when
  // it is marked critical, based on the assumption that the information in
  // the Netscape Cert Type extension is consistent with the information in
  // the standard extensions.
  //
  // Here is a mapping between the Netscape Cert Type extension and the
  // standard extensions:
  //
  // Netscape Cert Type  |  BasicConstraints.cA  |  Extended Key Usage
  // --------------------+-----------------------+----------------------
  // SSL Server          |  false                |  id_kp_serverAuth
  // SSL Client          |  false                |  id_kp_clientAuth
  // S/MIME Client       |  false                |  id_kp_emailProtection
  // Object Signing      |  false                |  id_kp_codeSigning
  // SSL Server CA       |  true                 |  id_kp_serverAuth
  // SSL Client CA       |  true                 |  id_kp_clientAuth
  // S/MIME CA           |  true                 |  id_kp_emailProtection
  // Object Signing CA   |  true                 |  id_kp_codeSigning
  if (criticalNetscapeCertificateType.GetLength() > 0 &&
      (basicConstraints.GetLength() == 0 || extKeyUsage.GetLength() == 0)) {
    return Result::ERROR_UNKNOWN_CRITICAL_EXTENSION;
  }

  return der::End(tbsCertificate);
}

Result
FindSubjectPublicKeyInfo(Input certDER, /*out*/ Input& subjectPublicKeyInfo)
{
  // BackCert::Init doesn't depend on the end-entity/CA distinction or on the
  // child certificate, so these don't matter here.
  BackCert cert(certDER, EndEntityOrCA::MustBeEndEntity, nullptr);
  Result rv = cert.Init();
  if (rv != Success) {
    return rv;
  }
  return subjectPublicKeyInfo.Init(cert.GetSubjectPublicKeyInfo());
}

Result
BackCert::RememberExtension(Reader& extnID, Input extnValue,
                            bool critical, /*out*/ bool& understood)
{
  understood = false;

  // python DottedOIDToCode.py id-ce-keyUsage 2.5.29.15
  static const uint8_t id_ce_keyUsage[] = {
    0x55, 0x1d, 0x0f
  };
  // python DottedOIDToCode.py id-ce-subjectAltName 2.5.29.17
  static const uint8_t id_ce_subjectAltName[] = {
    0x55, 0x1d, 0x11
  };
  // python DottedOIDToCode.py id-ce-basicConstraints 2.5.29.19
  static const uint8_t id_ce_basicConstraints[] = {
    0x55, 0x1d, 0x13
  };
  // python DottedOIDToCode.py id-ce-nameConstraints 2.5.29.30
  static const uint8_t id_ce_nameConstraints[] = {
    0x55, 0x1d, 0x1e
  };
  // python DottedOIDToCode.py id-ce-certificatePolicies 2.5.29.32
  static const uint8_t id_ce_certificatePolicies[] = {
    0x55, 0x1d, 0x20
  };
  // python DottedOIDToCode.py id-ce-policyConstraints 2.5.29.36
  static const uint8_t id_ce_policyConstraints[] = {
    0x55, 0x1d, 0x24
  };
  // python DottedOIDToCode.py id-ce-extKeyUsage 2.5.29.37
  static const uint8_t id_ce_extKeyUsage[] = {
    0x55, 0x1d, 0x25
  };
  // python DottedOIDToCode.py id-ce-inhibitAnyPolicy 2.5.29.54
  static const uint8_t id_ce_inhibitAnyPolicy[] = {
    0x55, 0x1d, 0x36
  };
  // python DottedOIDToCode.py id-pe-authorityInfoAccess 1.3.6.1.5.5.7.1.1
  static const uint8_t id_pe_authorityInfoAccess[] = {
    0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x01, 0x01
  };
  // python DottedOIDToCode.py id-pkix-ocsp-nocheck 1.3.6.1.5.5.7.48.1.5
  static const uint8_t id_pkix_ocsp_nocheck[] = {
    0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x30, 0x01, 0x05
  };
  // python DottedOIDToCode.py Netscape-certificate-type 2.16.840.1.113730.1.1
  static const uint8_t Netscape_certificate_type[] = {
    0x60, 0x86, 0x48, 0x01, 0x86, 0xf8, 0x42, 0x01, 0x01
  };
  // python DottedOIDToCode.py id-pe-tlsfeature 1.3.6.1.5.5.7.1.24
  static const uint8_t id_pe_tlsfeature[] = {
    0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x01, 0x18
  };
  // python DottedOIDToCode.py id-embeddedSctList 1.3.6.1.4.1.11129.2.4.2
  // See Section 3.3 of RFC 6962.
  static const uint8_t id_embeddedSctList[] = {
    0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02, 0x04, 0x02
  };
  // python DottedOIDToCode.py id-pe-qcStatements 1.3.6.1.5.5.7.1.3
  static const uint8_t id_pe_qcStatements[] = {
    0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x01, 0x03
  };

  Input* out = nullptr;

  // We already enforce the maximum possible constraints for policies so we
  // can safely ignore even critical policy constraint extensions.
  //
  // XXX: Doing it this way won't allow us to detect duplicate
  // policyConstraints extensions, but that's OK because (and only because) we
  // ignore the extension.
  Input dummyPolicyConstraints;

  // We don't need to save the contents of this extension if it is present. We
  // just need to handle its presence (it is essentially ignored right now).
  Input dummyOCSPNocheck;

  // For compatibility reasons, for some extensions we have to allow empty
  // extension values. This would normally interfere with our duplicate
  // extension checking code. However, as long as the extensions we allow to
  // have empty values are also the ones we implicitly allow duplicates of,
  // this will work fine.
  bool emptyValueAllowed = false;

  // RFC says "Conforming CAs MUST mark this extension as non-critical" for
  // both authorityKeyIdentifier and subjectKeyIdentifier, and we do not use
  // them for anything, so we totally ignore them here.

  if (extnID.MatchRest(id_ce_keyUsage)) {
    out = &keyUsage;
  } else if (extnID.MatchRest(id_ce_subjectAltName)) {
    out = &subjectAltName;
  } else if (extnID.MatchRest(id_ce_basicConstraints)) {
    out = &basicConstraints;
  } else if (extnID.MatchRest(id_ce_nameConstraints)) {
    out = &nameConstraints;
  } else if (extnID.MatchRest(id_ce_certificatePolicies)) {
    out = &certificatePolicies;
  } else if (extnID.MatchRest(id_ce_policyConstraints)) {
    out = &dummyPolicyConstraints;
  } else if (extnID.MatchRest(id_ce_extKeyUsage)) {
    out = &extKeyUsage;
  } else if (extnID.MatchRest(id_ce_inhibitAnyPolicy)) {
    out = &inhibitAnyPolicy;
  } else if (extnID.MatchRest(id_pe_authorityInfoAccess)) {
    out = &authorityInfoAccess;
  } else if (extnID.MatchRest(id_pe_tlsfeature)) {
    out = &requiredTLSFeatures;
  } else if (extnID.MatchRest(id_embeddedSctList)) {
    out = &signedCertificateTimestamps;
  } else if (extnID.MatchRest(id_pe_qcStatements)) {
    out = &qcStatements;
  } else if (extnID.MatchRest(id_pkix_ocsp_nocheck) && critical) {
    // We need to make sure we don't reject delegated OCSP response signing
    // certificates that contain the id-pkix-ocsp-nocheck extension marked as
    // critical when validating OCSP responses. Without this, an application
    // that implements soft-fail OCSP might ignore a valid Revoked or Unknown
    // response, and an application that implements hard-fail OCSP might fail
    // to connect to a server given a valid Good response.
    out = &dummyOCSPNocheck;
    // We allow this extension to have an empty value.
    // See http://comments.gmane.org/gmane.ietf.x509/30947
    emptyValueAllowed = true;
  } else if (extnID.MatchRest(Netscape_certificate_type) && critical) {
    out = &criticalNetscapeCertificateType;
  }

  if (out) {
    // Don't allow an empty value for any extension we understand. This way, we
    // can test out->GetLength() != 0 or out->Init() to check for duplicates.
    if (extnValue.GetLength() == 0 && !emptyValueAllowed) {
      return Result::ERROR_EXTENSION_VALUE_INVALID;
    }
    if (out->Init(extnValue) != Success) {
      // Duplicate extension
      return Result::ERROR_EXTENSION_VALUE_INVALID;
    }
    understood = true;
  }

  return Success;
}

Result
ExtractSignedCertificateTimestampListFromExtension(Input extnValue,
                                                   Input& sctList)
{
  Reader decodedValue;
  Result rv = der::ExpectTagAndGetValueAtEnd(extnValue, der::OCTET_STRING,
                                             decodedValue);
  if (rv != Success) {
    return rv;
  }
  return decodedValue.SkipToEnd(sctList);
}

} } // namespace mozilla::pkix
