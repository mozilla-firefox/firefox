/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CertVerifier.h"

#include <stdint.h>

#include "AppTrustDomain.h"
#include "CTKnownLogs.h"
#include "CTLogVerifier.h"
#include "ExtendedValidation.h"
#include "MultiLogCTVerifier.h"
#include "NSSCertDBTrustDomain.h"
#include "NSSErrorsService.h"
#include "cert.h"
#include "mozilla/Assertions.h"
#include "mozilla/Casting.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/Logging.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/SyncRunnable.h"
#include "mozpkix/pkix.h"
#include "mozpkix/pkixcheck.h"
#include "mozpkix/pkixnss.h"
#include "mozpkix/pkixutil.h"
#include "nsNSSComponent.h"
#include "nsNetCID.h"
#include "nsPromiseFlatString.h"
#include "nsServiceManagerUtils.h"
#include "pk11pub.h"
#include "secmod.h"

using namespace mozilla::ct;
using namespace mozilla::pkix;
using namespace mozilla::psm;

mozilla::LazyLogModule gCertVerifierLog("certverifier");

namespace mozilla {
namespace psm {

const CertVerifier::Flags CertVerifier::FLAG_LOCAL_ONLY = 1;
const CertVerifier::Flags CertVerifier::FLAG_MUST_BE_EV = 2;
const CertVerifier::Flags CertVerifier::FLAG_TLS_IGNORE_STATUS_REQUEST = 4;
static const unsigned int MIN_RSA_BITS = 2048;
static const unsigned int MIN_RSA_BITS_WEAK = 1024;

void CertificateTransparencyInfo::Reset() {
  enabled = false;
  verifyResult.Reset();
  policyCompliance.reset();
}

CertVerifier::CertVerifier(OcspDownloadConfig odc, OcspStrictConfig osc,
                           mozilla::TimeDuration ocspTimeoutSoft,
                           mozilla::TimeDuration ocspTimeoutHard,
                           uint32_t certShortLifetimeInDays,
                           CertificateTransparencyConfig&& ctConfig,
                           CRLiteMode crliteMode,
                           const nsTArray<EnterpriseCert>& thirdPartyCerts)
    : mOCSPDownloadConfig(odc),
      mOCSPStrict(osc == ocspStrict),
      mOCSPTimeoutSoft(ocspTimeoutSoft),
      mOCSPTimeoutHard(ocspTimeoutHard),
      mCertShortLifetimeInDays(certShortLifetimeInDays),
      mCTConfig(std::move(ctConfig)),
      mCRLiteMode(crliteMode),
      mSignatureCache(
          signature_cache_new(
              StaticPrefs::security_pki_cert_signature_cache_size()),
          signature_cache_free),
      mTrustCache(
          trust_cache_new(StaticPrefs::security_pki_cert_trust_cache_size()),
          trust_cache_free) {
  LoadKnownCTLogs();
  mThirdPartyCerts = thirdPartyCerts.Clone();
  for (const auto& root : mThirdPartyCerts) {
    Input input;
    if (root.GetInput(input) == Success) {
      // mThirdPartyCerts consists of roots and intermediates.
      if (root.GetIsRoot()) {
        mThirdPartyRootInputs.AppendElement(input);
      } else {
        mThirdPartyIntermediateInputs.AppendElement(input);
      }
    }
  }
}

CertVerifier::~CertVerifier() = default;

Result IsDelegatedCredentialAcceptable(const DelegatedCredentialInfo& dcInfo) {
  bool isEcdsa = dcInfo.scheme == ssl_sig_ecdsa_secp256r1_sha256 ||
                 dcInfo.scheme == ssl_sig_ecdsa_secp384r1_sha384 ||
                 dcInfo.scheme == ssl_sig_ecdsa_secp521r1_sha512;

  // Firefox currently does not advertise any RSA schemes for use
  // with Delegated Credentials. As a secondary (on top of NSS)
  // check, disallow any RSA SPKI here. When ssl_sig_rsa_pss_pss_*
  // schemes are supported, check the modulus size and allow RSA here.
  if (!isEcdsa) {
    return Result::ERROR_INVALID_KEY;
  }

  return Result::Success;
}

// The term "builtin root" traditionally refers to a root CA certificate that
// has been added to the NSS trust store, because it has been approved
// for inclusion according to the Mozilla CA policy, and might be accepted
// by Mozilla applications as an issuer for certificates seen on the public web.
Result IsCertBuiltInRoot(Input certInput, bool& result) {
  result = false;

  if (NS_FAILED(BlockUntilLoadableCertsLoaded())) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

#ifdef DEBUG
  nsCOMPtr<nsINSSComponent> component(do_GetService(PSM_COMPONENT_CONTRACTID));
  if (!component) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }
  nsTArray<uint8_t> certBytes;
  certBytes.AppendElements(certInput.UnsafeGetData(), certInput.GetLength());
  if (NS_FAILED(component->IsCertTestBuiltInRoot(certBytes, &result))) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }
  if (result) {
    return Success;
  }
#endif  // DEBUG
  SECItem certItem(UnsafeMapInputToSECItem(certInput));
  AutoSECMODListReadLock lock;
  for (SECMODModuleList* list = SECMOD_GetDefaultModuleList(); list;
       list = list->next) {
    for (int i = 0; i < list->module->slotCount; i++) {
      PK11SlotInfo* slot = list->module->slots[i];
      // We're searching for the "builtin root module", which is a module that
      // contains an object with a CKA_CLASS of CKO_NETSCAPE_BUILTIN_ROOT_LIST.
      // We use PK11_HasRootCerts() to identify a module with that property.
      // In the past, we exclusively used the PKCS#11 module named nssckbi,
      // which is provided by the NSS library.
      // Nowadays, some distributions use a replacement module, which contains
      // the builtin roots, but which also contains additional CA certificates,
      // such as CAs trusted in a local deployment.
      // We want to be able to distinguish between these two categories,
      // because a CA, which may issue certificates for the public web,
      // is expected to comply with additional requirements.
      // If the certificate has attribute CKA_NSS_MOZILLA_CA_POLICY set to true,
      // then we treat it as a "builtin root".
      if (!PK11_IsPresent(slot) || !PK11_HasRootCerts(slot)) {
        continue;
      }
      CK_OBJECT_HANDLE handle =
          PK11_FindEncodedCertInSlot(slot, &certItem, nullptr);
      if (handle == CK_INVALID_HANDLE) {
        continue;
      }
      if (PK11_HasAttributeSet(slot, handle, CKA_NSS_MOZILLA_CA_POLICY,
                               false)) {
        // Attribute was found, and is set to true
        result = true;
        break;
      }
    }
  }
  return Success;
}

static Result BuildCertChainForOneKeyUsage(
    NSSCertDBTrustDomain& trustDomain, Input certDER, Time time, KeyUsage ku1,
    KeyUsage ku2, KeyUsage ku3, KeyPurposeId eku,
    const CertPolicyId& requiredPolicy, const Input* stapledOCSPResponse,
    /*optional out*/ CertVerifier::OCSPStaplingStatus* ocspStaplingStatus) {
  trustDomain.ResetAccumulatedState();
  Result rv =
      BuildCertChain(trustDomain, certDER, time, EndEntityOrCA::MustBeEndEntity,
                     ku1, eku, requiredPolicy, stapledOCSPResponse);
  if (rv == Result::ERROR_INADEQUATE_KEY_USAGE) {
    trustDomain.ResetAccumulatedState();
    rv = BuildCertChain(trustDomain, certDER, time,
                        EndEntityOrCA::MustBeEndEntity, ku2, eku,
                        requiredPolicy, stapledOCSPResponse);
    if (rv == Result::ERROR_INADEQUATE_KEY_USAGE) {
      trustDomain.ResetAccumulatedState();
      rv = BuildCertChain(trustDomain, certDER, time,
                          EndEntityOrCA::MustBeEndEntity, ku3, eku,
                          requiredPolicy, stapledOCSPResponse);
      if (rv != Success) {
        rv = Result::ERROR_INADEQUATE_KEY_USAGE;
      }
    }
  }
  if (ocspStaplingStatus) {
    *ocspStaplingStatus = trustDomain.GetOCSPStaplingStatus();
  }
  return rv;
}

void CertVerifier::LoadKnownCTLogs() {
  mCTVerifier = MakeUnique<MultiLogCTVerifier>();
  for (const CTLogInfo& log : kCTLogList) {
    Input publicKey;
    Result rv = publicKey.Init(
        BitwiseCast<const uint8_t*, const char*>(log.key), log.keyLength);
    if (rv != Success) {
      MOZ_ASSERT_UNREACHABLE("Failed reading a log key for a known CT Log");
      continue;
    }

    const CTLogOperatorInfo& logOperator =
        kCTLogOperatorList[log.operatorIndex];
    CTLogVerifier logVerifier(logOperator.id, log.state, log.format,
                              log.timestamp);
    rv = logVerifier.Init(publicKey);
    if (rv != Success) {
      MOZ_ASSERT_UNREACHABLE("Failed initializing a known CT Log");
      continue;
    }

    mCTVerifier->AddLog(std::move(logVerifier));
  }
}

bool HostnameMatchesPolicy(const char* hostname, const nsCString& policy) {
  // Some contexts don't have a hostname (mostly tests), in which case the
  // policy doesn't apply.
  if (!hostname) {
    return false;
  }
  nsDependentCString hostnameString(hostname);
  // The policy is a comma-separated list of entries of the form
  // '.example.com', 'example.com', or an IP address.
  for (const auto& entry : policy.Split(',')) {
    if (entry.IsEmpty()) {
      continue;
    }
    // For '.example.com' entries, exact matches match the policy.
    if (entry[0] == '.' &&
        Substring(entry, 1).EqualsIgnoreCase(hostnameString)) {
      MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
              ("not enforcing CT for '%s' (matches policy '%s')", hostname,
               policy.get()));
      return true;
    }
    // For 'example.com' entries, exact matches or subdomains match the policy
    // (IP addresses match here too).
    if (StringEndsWith(hostnameString, entry) &&
        (hostnameString.Length() == entry.Length() ||
         hostnameString[hostnameString.Length() - entry.Length() - 1] == '.')) {
      MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
              ("not enforcing CT for '%s' (matches policy '%s')", hostname,
               policy.get()));
      return true;
    }
  }
  return false;
}

bool CertificateListHasSPKIHashIn(
    const nsTArray<nsTArray<uint8_t>>& certificates,
    const nsTArray<CopyableTArray<uint8_t>>& spkiHashes) {
  if (spkiHashes.IsEmpty()) {
    return false;
  }
  for (const auto& certificate : certificates) {
    Input certificateInput;
    if (certificateInput.Init(certificate.Elements(), certificate.Length()) !=
        Success) {
      return false;
    }
    // No path building is happening here, so this parameter doesn't matter.
    EndEntityOrCA notUsedForPathBuilding = EndEntityOrCA::MustBeEndEntity;
    BackCert decodedCertificate(certificateInput, notUsedForPathBuilding,
                                nullptr);
    if (decodedCertificate.Init() != Success) {
      return false;
    }
    Input spki(decodedCertificate.GetSubjectPublicKeyInfo());
    uint8_t spkiHash[SHA256_LENGTH];
    if (DigestBufNSS(spki, DigestAlgorithm::sha256, spkiHash,
                     sizeof(spkiHash)) != Success) {
      return false;
    }
    Span spkiHashSpan(reinterpret_cast<const uint8_t*>(spkiHash),
                      sizeof(spkiHash));
    for (const auto& candidateSPKIHash : spkiHashes) {
      if (Span(candidateSPKIHash) == spkiHashSpan) {
        MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
                ("found SPKI hash match - not enforcing CT"));
        return true;
      }
    }
  }
  return false;
}

Result CertVerifier::VerifyCertificateTransparencyPolicy(
    NSSCertDBTrustDomain& trustDomain,
    const nsTArray<nsTArray<uint8_t>>& builtChain, Input sctsFromTLS, Time time,
    const char* hostname,
    /*optional out*/ CertificateTransparencyInfo* ctInfo) {
  if (builtChain.IsEmpty()) {
    return Result::FATAL_ERROR_INVALID_ARGS;
  }
  if (ctInfo) {
    ctInfo->Reset();
  }
  if (mCTConfig.mMode == CertificateTransparencyMode::Disabled ||
      !trustDomain.GetIsBuiltChainRootBuiltInRoot()) {
    return Success;
  }
  if (time > TimeFromEpochInSeconds(kCTExpirationTime / PR_USEC_PER_SEC)) {
    MOZ_LOG(gCertVerifierLog, LogLevel::Warning,
            ("skipping CT - built-in information has expired"));
    return Success;
  }
  if (ctInfo) {
    ctInfo->enabled = true;
  }

  Result rv = VerifyCertificateTransparencyPolicyInner(
      trustDomain, builtChain, sctsFromTLS, time, ctInfo);
  if (rv == Result::ERROR_INSUFFICIENT_CERTIFICATE_TRANSPARENCY &&
      (mCTConfig.mMode != CertificateTransparencyMode::Enforce ||
       HostnameMatchesPolicy(hostname, mCTConfig.mSkipForHosts) ||
       CertificateListHasSPKIHashIn(builtChain,
                                    mCTConfig.mSkipForSPKIHashes))) {
    return Success;
  }

  return rv;
}

Result CertVerifier::VerifyCertificateTransparencyPolicyInner(
    NSSCertDBTrustDomain& trustDomain,
    const nsTArray<nsTArray<uint8_t>>& builtChain, Input sctsFromTLS, Time time,
    /*optional out*/ CertificateTransparencyInfo* ctInfo) {
  if (builtChain.Length() == 1) {
    // Issuer certificate is required for SCT verification.
    // If we've arrived here, we probably have a "trust chain" with only one
    // certificate (i.e. a self-signed end-entity that has been set as a trust
    // anchor either by a third party modifying our trust DB or via the
    // enterprise roots feature). If this is the case, certificate transparency
    // information will probably not be present, and it certainly won't verify
    // correctly. To simplify things, we return an empty CTVerifyResult and a
    // "not enough SCTs" CTPolicyCompliance result.
    if (ctInfo) {
      CTVerifyResult emptyResult;
      ctInfo->verifyResult = std::move(emptyResult);
      ctInfo->policyCompliance.emplace(CTPolicyCompliance::NotEnoughScts);
    }
    return Result::ERROR_INSUFFICIENT_CERTIFICATE_TRANSPARENCY;
  }

  const nsTArray<uint8_t>& endEntityBytes = builtChain.ElementAt(0);
  Input endEntityInput;
  Result rv =
      endEntityInput.Init(endEntityBytes.Elements(), endEntityBytes.Length());
  if (rv != Success) {
    return rv;
  }

  // We evaluated embedded SCTs and SCTs from the TLS handshake before we
  // performed revocation checks, and we should have a cached CTVerifyResult in
  // the trust domain. If we later received SCTs from OCSP (very rare), then
  // we'll re-check all of the SCTs. Otherwise we'll use the cached
  // CTVerifyResult.
  Input sctsFromOCSP = trustDomain.GetSCTListFromOCSPStapling();
  if (sctsFromOCSP.GetLength() > 0) {
    MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
            ("Got OCSP SCT data of length %zu",
             static_cast<size_t>(sctsFromOCSP.GetLength())));
  }

  CTVerifyResult result;
  if (trustDomain.GetCachedCTVerifyResult().isSome() &&
      sctsFromOCSP.GetLength() == 0) {
    result = trustDomain.GetCachedCTVerifyResult().extract();
  } else {
    // invalidate the cached result if it exists
    trustDomain.GetCachedCTVerifyResult().reset();

    Input embeddedSCTs = trustDomain.GetSCTListFromCertificate();
    if (embeddedSCTs.GetLength() > 0) {
      MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
              ("Got embedded SCT data of length %zu",
               static_cast<size_t>(embeddedSCTs.GetLength())));
    }
    if (sctsFromTLS.GetLength() > 0) {
      MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
              ("Got TLS SCT data of length %zu",
               static_cast<size_t>(sctsFromTLS.GetLength())));
    }

    const nsTArray<uint8_t>& issuerBytes = builtChain.ElementAt(1);
    Input issuerInput;
    rv = issuerInput.Init(issuerBytes.Elements(), issuerBytes.Length());
    if (rv != Success) {
      return rv;
    }

    BackCert issuerBackCert(issuerInput, EndEntityOrCA::MustBeCA, nullptr);
    rv = issuerBackCert.Init();
    if (rv != Success) {
      return rv;
    }
    Input issuerPublicKeyInput = issuerBackCert.GetSubjectPublicKeyInfo();

    rv = mCTVerifier->Verify(endEntityInput, issuerPublicKeyInput, embeddedSCTs,
                             sctsFromOCSP, sctsFromTLS, time,
                             trustDomain.GetDistrustAfterTime(), result);
    if (rv != Success) {
      MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
              ("SCT verification failed with fatal error %" PRId32 "\n",
               static_cast<uint32_t>(rv)));
      return rv;
    }
  }

  if (MOZ_LOG_TEST(gCertVerifierLog, LogLevel::Debug)) {
    size_t validCount = 0;
    size_t retiredLogCount = 0;
    for (const VerifiedSCT& verifiedSct : result.verifiedScts) {
      switch (verifiedSct.logState) {
        case CTLogState::Admissible:
          validCount++;
          break;
        case CTLogState::Retired:
          retiredLogCount++;
          break;
      }
    }
    MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
            ("SCT verification result: "
             "valid=%zu unknownLog=%zu retiredLog=%zu "
             "invalidSignature=%zu invalidTimestamp=%zu "
             "distrustedTimestamp=%zu decodingErrors=%zu\n",
             validCount, result.sctsFromUnknownLogs, retiredLogCount,
             result.sctsWithInvalidSignatures, result.sctsWithInvalidTimestamps,
             result.sctsWithDistrustedTimestamps, result.decodingErrors));
  }

  BackCert endEntityBackCert(endEntityInput, EndEntityOrCA::MustBeEndEntity,
                             nullptr);
  rv = endEntityBackCert.Init();
  if (rv != Success) {
    return rv;
  }
  Time notBefore(Time::uninitialized);
  Time notAfter(Time::uninitialized);
  rv = ParseValidity(endEntityBackCert.GetValidity(), &notBefore, &notAfter);
  if (rv != Success) {
    return rv;
  }
  Duration certLifetime(notBefore, notAfter);

  CTPolicyCompliance ctPolicyCompliance =
      CheckCTPolicyCompliance(result.verifiedScts, certLifetime);

  if (ctInfo) {
    ctInfo->verifyResult = std::move(result);
    ctInfo->policyCompliance.emplace(ctPolicyCompliance);
  }

  if (ctPolicyCompliance != CTPolicyCompliance::Compliant) {
    return Result::ERROR_INSUFFICIENT_CERTIFICATE_TRANSPARENCY;
  }

  return Success;
}

Result CertVerifier::VerifyCert(
    const nsTArray<uint8_t>& certBytes, VerifyUsage usage, Time time,
    void* pinArg, const char* hostname,
    /*out*/ nsTArray<nsTArray<uint8_t>>& builtChain,
    /*optional*/ const Flags flags,
    /*optional*/ const Maybe<nsTArray<nsTArray<uint8_t>>>& extraCertificates,
    /*optional*/ const Maybe<nsTArray<uint8_t>>& stapledOCSPResponseArg,
    /*optional*/ const Maybe<nsTArray<uint8_t>>& sctsFromTLS,
    /*optional*/ const OriginAttributes& originAttributes,
    /*optional out*/ EVStatus* evStatus,
    /*optional out*/ OCSPStaplingStatus* ocspStaplingStatus,
    /*optional out*/ KeySizeStatus* keySizeStatus,
    /*optional out*/ PinningTelemetryInfo* pinningTelemetryInfo,
    /*optional out*/ CertificateTransparencyInfo* ctInfo,
    /*optional out*/ bool* isBuiltChainRootBuiltInRoot,
    /*optional out*/ bool* madeOCSPRequests,
    /*optional out*/ IssuerSources* issuerSources) {
  MOZ_LOG(gCertVerifierLog, LogLevel::Debug, ("Top of VerifyCert\n"));

  MOZ_ASSERT(usage == VerifyUsage::TLSServer || !(flags & FLAG_MUST_BE_EV));
  MOZ_ASSERT(usage == VerifyUsage::TLSServer || !keySizeStatus);

  if (NS_FAILED(BlockUntilLoadableCertsLoaded())) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }
  if (NS_FAILED(CheckForSmartCardChanges())) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  if (evStatus) {
    *evStatus = EVStatus::NotEV;
  }
  if (ocspStaplingStatus) {
    if (usage != VerifyUsage::TLSServer) {
      return Result::FATAL_ERROR_INVALID_ARGS;
    }
    *ocspStaplingStatus = OCSP_STAPLING_NEVER_CHECKED;
  }

  if (keySizeStatus) {
    if (usage != VerifyUsage::TLSServer) {
      return Result::FATAL_ERROR_INVALID_ARGS;
    }
    *keySizeStatus = KeySizeStatus::NeverChecked;
  }

  if (usage != VerifyUsage::TLSServer && (flags & FLAG_MUST_BE_EV)) {
    return Result::FATAL_ERROR_INVALID_ARGS;
  }

  if (isBuiltChainRootBuiltInRoot) {
    *isBuiltChainRootBuiltInRoot = false;
  }

  if (madeOCSPRequests) {
    *madeOCSPRequests = false;
  }

  if (issuerSources) {
    issuerSources->clear();
  }

  Input certDER;
  Result rv = certDER.Init(certBytes.Elements(), certBytes.Length());
  if (rv != Success) {
    return rv;
  }

  // We configure the OCSP fetching modes separately for EV and non-EV
  // verifications.
  NSSCertDBTrustDomain::RevocationCheckMode defaultRevCheckMode =
      (mOCSPDownloadConfig == ocspOff) || (mOCSPDownloadConfig == ocspEVOnly) ||
              (flags & FLAG_LOCAL_ONLY)
          ? NSSCertDBTrustDomain::RevocationCheckLocalOnly
      : !mOCSPStrict ? NSSCertDBTrustDomain::RevocationCheckMayFetch
                     : NSSCertDBTrustDomain::RevocationCheckRequired;

  Input stapledOCSPResponseInput;
  const Input* stapledOCSPResponse = nullptr;
  if (stapledOCSPResponseArg) {
    rv = stapledOCSPResponseInput.Init(stapledOCSPResponseArg->Elements(),
                                       stapledOCSPResponseArg->Length());
    if (rv != Success) {
      // The stapled OCSP response was too big.
      return Result::ERROR_OCSP_MALFORMED_RESPONSE;
    }
    stapledOCSPResponse = &stapledOCSPResponseInput;
  }

  Input sctsFromTLSInput;
  if (sctsFromTLS) {
    rv = sctsFromTLSInput.Init(sctsFromTLS->Elements(), sctsFromTLS->Length());
    if (rv != Success && sctsFromTLSInput.GetLength() != 0) {
      return Result::FATAL_ERROR_LIBRARY_FAILURE;
    }
  }

  switch (usage) {
    case VerifyUsage::TLSClient: {
      // XXX: We don't really have a trust bit for SSL client authentication so
      // just use trustEmail as it is the closest alternative.
      NSSCertDBTrustDomain trustDomain(
          trustEmail, defaultRevCheckMode, mOCSPCache, mSignatureCache.get(),
          mTrustCache.get(), pinArg, mOCSPTimeoutSoft, mOCSPTimeoutHard,
          mCertShortLifetimeInDays, MIN_RSA_BITS_WEAK, mCRLiteMode,
          originAttributes, mThirdPartyRootInputs,
          mThirdPartyIntermediateInputs, extraCertificates, sctsFromTLSInput,
          mCTVerifier, builtChain, nullptr, nullptr);
      rv = BuildCertChain(
          trustDomain, certDER, time, EndEntityOrCA::MustBeEndEntity,
          KeyUsage::digitalSignature, KeyPurposeId::id_kp_clientAuth,
          CertPolicyId::anyPolicy, stapledOCSPResponse);
      if (madeOCSPRequests) {
        *madeOCSPRequests |=
            trustDomain.GetOCSPFetchStatus() == OCSPFetchStatus::Fetched;
      }
      break;
    }

    case VerifyUsage::TLSServer: {
      // TODO: When verifying a certificate in an SSL handshake, we should
      // restrict the acceptable key usage based on the key exchange method
      // chosen by the server.

      // Try to validate for EV first.
      NSSCertDBTrustDomain::RevocationCheckMode evRevCheckMode =
          (mOCSPDownloadConfig == ocspOff) || (flags & FLAG_LOCAL_ONLY)
              ? NSSCertDBTrustDomain::RevocationCheckLocalOnly
          : !mOCSPStrict ? NSSCertDBTrustDomain::RevocationCheckMayFetch
                         : NSSCertDBTrustDomain::RevocationCheckRequired;

      nsTArray<CertPolicyId> evPolicies;
      GetKnownEVPolicies(certBytes, evPolicies);
      rv = Result::ERROR_UNKNOWN_ERROR;
      for (const auto& evPolicy : evPolicies) {
        NSSCertDBTrustDomain trustDomain(
            trustSSL, evRevCheckMode, mOCSPCache, mSignatureCache.get(),
            mTrustCache.get(), pinArg, mOCSPTimeoutSoft, mOCSPTimeoutHard,
            mCertShortLifetimeInDays, MIN_RSA_BITS, mCRLiteMode,
            originAttributes, mThirdPartyRootInputs,
            mThirdPartyIntermediateInputs, extraCertificates, sctsFromTLSInput,
            mCTVerifier, builtChain, pinningTelemetryInfo, hostname);
        rv = BuildCertChainForOneKeyUsage(
            trustDomain, certDER, time,
            KeyUsage::digitalSignature,  // (EC)DHE
            KeyUsage::keyEncipherment,   // RSA
            KeyUsage::keyAgreement,      // (EC)DH
            KeyPurposeId::id_kp_serverAuth, evPolicy, stapledOCSPResponse,
            ocspStaplingStatus);
        if (madeOCSPRequests) {
          *madeOCSPRequests |=
              trustDomain.GetOCSPFetchStatus() == OCSPFetchStatus::Fetched;
        }
        if (issuerSources) {
          *issuerSources = trustDomain.GetIssuerSources();
        }
        if (rv == Success) {
          rv = VerifyCertificateTransparencyPolicy(trustDomain, builtChain,
                                                   sctsFromTLSInput, time,
                                                   hostname, ctInfo);
        }
        if (rv == Success) {
          if (evStatus) {
            *evStatus = EVStatus::EV;
          }
          if (isBuiltChainRootBuiltInRoot) {
            *isBuiltChainRootBuiltInRoot =
                trustDomain.GetIsBuiltChainRootBuiltInRoot();
          }
          break;
        }
      }
      if (rv == Success) {
        break;
      }
      if (flags & FLAG_MUST_BE_EV) {
        rv = Result::ERROR_POLICY_VALIDATION_FAILED;
        break;
      }

      // Now try non-EV.
      unsigned int keySizeOptions[] = {MIN_RSA_BITS, MIN_RSA_BITS_WEAK};

      KeySizeStatus keySizeStatuses[] = {KeySizeStatus::LargeMinimumSucceeded,
                                         KeySizeStatus::CompatibilityRisk};

      static_assert(std::size(keySizeOptions) == std::size(keySizeStatuses),
                    "keySize array lengths differ");

      size_t keySizeOptionsCount = std::size(keySizeStatuses);

      for (size_t i = 0; i < keySizeOptionsCount && rv != Success; i++) {
        // invalidate any telemetry info relating to failed chains
        if (pinningTelemetryInfo) {
          pinningTelemetryInfo->Reset();
        }

        NSSCertDBTrustDomain trustDomain(
            trustSSL, defaultRevCheckMode, mOCSPCache, mSignatureCache.get(),
            mTrustCache.get(), pinArg, mOCSPTimeoutSoft, mOCSPTimeoutHard,
            mCertShortLifetimeInDays, keySizeOptions[i], mCRLiteMode,
            originAttributes, mThirdPartyRootInputs,
            mThirdPartyIntermediateInputs, extraCertificates, sctsFromTLSInput,
            mCTVerifier, builtChain, pinningTelemetryInfo, hostname);
        rv = BuildCertChainForOneKeyUsage(
            trustDomain, certDER, time,
            KeyUsage::digitalSignature,  //(EC)DHE
            KeyUsage::keyEncipherment,   // RSA
            KeyUsage::keyAgreement,      //(EC)DH
            KeyPurposeId::id_kp_serverAuth, CertPolicyId::anyPolicy,
            stapledOCSPResponse, ocspStaplingStatus);
        if (madeOCSPRequests) {
          *madeOCSPRequests |=
              trustDomain.GetOCSPFetchStatus() == OCSPFetchStatus::Fetched;
        }
        if (issuerSources) {
          *issuerSources = trustDomain.GetIssuerSources();
        }
        if (rv == Success) {
          rv = VerifyCertificateTransparencyPolicy(trustDomain, builtChain,
                                                   sctsFromTLSInput, time,
                                                   hostname, ctInfo);
        }
        if (rv == Success) {
          if (keySizeStatus) {
            *keySizeStatus = keySizeStatuses[i];
          }
          if (isBuiltChainRootBuiltInRoot) {
            *isBuiltChainRootBuiltInRoot =
                trustDomain.GetIsBuiltChainRootBuiltInRoot();
          }
          // Enforce the minimum (non-weak) RSA key size for certificates
          // issued from built-in roots.
          if (keySizeOptions[i] < MIN_RSA_BITS &&
              trustDomain.GetIsBuiltChainRootBuiltInRoot()) {
            return Result::ERROR_INADEQUATE_KEY_SIZE;
          }
          break;
        }
      }

      if (rv != Success && keySizeStatus) {
        *keySizeStatus = KeySizeStatus::AlreadyBad;
      }

      break;
    }

    case VerifyUsage::EmailCA:
    case VerifyUsage::TLSClientCA:
    case VerifyUsage::TLSServerCA: {
      KeyPurposeId purpose;
      SECTrustType trustType;

      if (usage == VerifyUsage::EmailCA || usage == VerifyUsage::TLSClientCA) {
        purpose = KeyPurposeId::id_kp_clientAuth;
        trustType = trustEmail;
      } else if (usage == VerifyUsage::TLSServerCA) {
        purpose = KeyPurposeId::id_kp_serverAuth;
        trustType = trustSSL;
      } else {
        MOZ_ASSERT_UNREACHABLE("coding error");
        return Result::FATAL_ERROR_LIBRARY_FAILURE;
      }

      NSSCertDBTrustDomain trustDomain(
          trustType, defaultRevCheckMode, mOCSPCache, mSignatureCache.get(),
          mTrustCache.get(), pinArg, mOCSPTimeoutSoft, mOCSPTimeoutHard,
          mCertShortLifetimeInDays, MIN_RSA_BITS_WEAK, mCRLiteMode,
          originAttributes, mThirdPartyRootInputs,
          mThirdPartyIntermediateInputs, extraCertificates, sctsFromTLSInput,
          mCTVerifier, builtChain, nullptr, nullptr);
      rv = BuildCertChain(trustDomain, certDER, time, EndEntityOrCA::MustBeCA,
                          KeyUsage::keyCertSign, purpose,
                          CertPolicyId::anyPolicy, stapledOCSPResponse);
      if (madeOCSPRequests) {
        *madeOCSPRequests |=
            trustDomain.GetOCSPFetchStatus() == OCSPFetchStatus::Fetched;
      }
      break;
    }

    case VerifyUsage::EmailSigner: {
      NSSCertDBTrustDomain trustDomain(
          trustEmail, defaultRevCheckMode, mOCSPCache, mSignatureCache.get(),
          mTrustCache.get(), pinArg, mOCSPTimeoutSoft, mOCSPTimeoutHard,
          mCertShortLifetimeInDays, MIN_RSA_BITS_WEAK, mCRLiteMode,
          originAttributes, mThirdPartyRootInputs,
          mThirdPartyIntermediateInputs, extraCertificates, sctsFromTLSInput,
          mCTVerifier, builtChain, nullptr, nullptr);
      rv = BuildCertChain(
          trustDomain, certDER, time, EndEntityOrCA::MustBeEndEntity,
          KeyUsage::digitalSignature, KeyPurposeId::id_kp_emailProtection,
          CertPolicyId::anyPolicy, stapledOCSPResponse);
      if (rv == Result::ERROR_INADEQUATE_KEY_USAGE) {
        rv = BuildCertChain(
            trustDomain, certDER, time, EndEntityOrCA::MustBeEndEntity,
            KeyUsage::nonRepudiation, KeyPurposeId::id_kp_emailProtection,
            CertPolicyId::anyPolicy, stapledOCSPResponse);
      }
      if (madeOCSPRequests) {
        *madeOCSPRequests |=
            trustDomain.GetOCSPFetchStatus() == OCSPFetchStatus::Fetched;
      }
      break;
    }

    case VerifyUsage::EmailRecipient: {
      // TODO: The higher level S/MIME processing should pass in which key
      // usage it is trying to verify for, and base its algorithm choices
      // based on the result of the verification(s).
      NSSCertDBTrustDomain trustDomain(
          trustEmail, defaultRevCheckMode, mOCSPCache, mSignatureCache.get(),
          mTrustCache.get(), pinArg, mOCSPTimeoutSoft, mOCSPTimeoutHard,
          mCertShortLifetimeInDays, MIN_RSA_BITS_WEAK, mCRLiteMode,
          originAttributes, mThirdPartyRootInputs,
          mThirdPartyIntermediateInputs, extraCertificates, sctsFromTLSInput,
          mCTVerifier, builtChain, nullptr, nullptr);
      rv = BuildCertChain(trustDomain, certDER, time,
                          EndEntityOrCA::MustBeEndEntity,
                          KeyUsage::keyEncipherment,  // RSA
                          KeyPurposeId::id_kp_emailProtection,
                          CertPolicyId::anyPolicy, stapledOCSPResponse);
      if (rv == Result::ERROR_INADEQUATE_KEY_USAGE) {
        rv = BuildCertChain(trustDomain, certDER, time,
                            EndEntityOrCA::MustBeEndEntity,
                            KeyUsage::keyAgreement,  // ECDH/DH
                            KeyPurposeId::id_kp_emailProtection,
                            CertPolicyId::anyPolicy, stapledOCSPResponse);
      }
      if (madeOCSPRequests) {
        *madeOCSPRequests |=
            trustDomain.GetOCSPFetchStatus() == OCSPFetchStatus::Fetched;
      }
      break;
    }

    default:
      rv = Result::FATAL_ERROR_INVALID_ARGS;
  }

  if (rv != Success) {
    return rv;
  }

  return Success;
}

static bool CertIsSelfSigned(const BackCert& backCert, void* pinarg) {
  if (!InputsAreEqual(backCert.GetIssuer(), backCert.GetSubject())) {
    return false;
  }

  nsTArray<Span<const uint8_t>> emptyCertList;
  // AppTrustDomain is only used for its signature verification callbacks
  // (AppTrustDomain::Verify{ECDSA,RSAPKCS1,RSAPSS}SignedData).
  mozilla::psm::AppTrustDomain trustDomain(std::move(emptyCertList));
  Result rv = VerifySignedData(trustDomain, backCert.GetSignedData(),
                               backCert.GetSubjectPublicKeyInfo());
  return rv == Success;
}

static Result CheckCertHostnameHelper(Input peerCertInput,
                                      const nsACString& hostname,
                                      bool rootIsBuiltIn) {
  Input hostnameInput;
  Result rv = hostnameInput.Init(
      BitwiseCast<const uint8_t*, const char*>(hostname.BeginReading()),
      hostname.Length());
  if (rv != Success) {
    return Result::FATAL_ERROR_INVALID_ARGS;
  }

  SkipInvalidSANsForNonBuiltInRootsPolicy nameMatchingPolicy(rootIsBuiltIn);
  rv = CheckCertHostname(peerCertInput, hostnameInput, nameMatchingPolicy);
  // Treat malformed name information as a domain mismatch.
  if (rv == Result::ERROR_BAD_DER) {
    return Result::ERROR_BAD_CERT_DOMAIN;
  }
  return rv;
}

Result CertVerifier::VerifySSLServerCert(
    const nsTArray<uint8_t>& peerCertBytes, Time time,
    /*optional*/ void* pinarg, const nsACString& hostname,
    /*out*/ nsTArray<nsTArray<uint8_t>>& builtChain,
    /*optional*/ Flags flags,
    /*optional*/ const Maybe<nsTArray<nsTArray<uint8_t>>>& extraCertificates,
    /*optional*/ const Maybe<nsTArray<uint8_t>>& stapledOCSPResponse,
    /*optional*/ const Maybe<nsTArray<uint8_t>>& sctsFromTLS,
    /*optional*/ const Maybe<DelegatedCredentialInfo>& dcInfo,
    /*optional*/ const OriginAttributes& originAttributes,
    /*optional out*/ EVStatus* evStatus,
    /*optional out*/ OCSPStaplingStatus* ocspStaplingStatus,
    /*optional out*/ KeySizeStatus* keySizeStatus,
    /*optional out*/ PinningTelemetryInfo* pinningTelemetryInfo,
    /*optional out*/ CertificateTransparencyInfo* ctInfo,
    /*optional out*/ bool* isBuiltChainRootBuiltInRoot,
    /*optional out*/ bool* madeOCSPRequests,
    /*optional out*/ IssuerSources* issuerSources) {
  // XXX: MOZ_ASSERT(pinarg);
  MOZ_ASSERT(!hostname.IsEmpty());

  if (isBuiltChainRootBuiltInRoot) {
    *isBuiltChainRootBuiltInRoot = false;
  }

  if (evStatus) {
    *evStatus = EVStatus::NotEV;
  }

  if (hostname.IsEmpty()) {
    return Result::FATAL_ERROR_INVALID_ARGS;
  }

  // CreateCertErrorRunnable assumes that CheckCertHostname is only called
  // if VerifyCert succeeded.
  Input peerCertInput;
  Result rv =
      peerCertInput.Init(peerCertBytes.Elements(), peerCertBytes.Length());
  if (rv != Success) {
    return rv;
  }
  bool isBuiltChainRootBuiltInRootLocal;
  rv = VerifyCert(
      peerCertBytes, VerifyUsage::TLSServer, time, pinarg,
      PromiseFlatCString(hostname).get(), builtChain, flags, extraCertificates,
      stapledOCSPResponse, sctsFromTLS, originAttributes, evStatus,
      ocspStaplingStatus, keySizeStatus, pinningTelemetryInfo, ctInfo,
      &isBuiltChainRootBuiltInRootLocal, madeOCSPRequests, issuerSources);
  if (rv != Success) {
    // we don't use the certificate for path building, so this parameter doesn't
    // matter
    EndEntityOrCA notUsedForPaths = EndEntityOrCA::MustBeEndEntity;
    BackCert peerBackCert(peerCertInput, notUsedForPaths, nullptr);
    if (peerBackCert.Init() != Success) {
      return rv;
    }
    if ((rv == Result::ERROR_UNKNOWN_ISSUER ||
         rv == Result::ERROR_BAD_SIGNATURE ||
         rv == Result::ERROR_INADEQUATE_KEY_USAGE) &&
        CertIsSelfSigned(peerBackCert, pinarg)) {
      // In this case we didn't find any issuer for the certificate, or we did
      // find other certificates with the same subject but different keys, and
      // the certificate is self-signed.
      return Result::ERROR_SELF_SIGNED_CERT;
    }
    if (rv == Result::ERROR_UNKNOWN_ISSUER) {
      // In this case we didn't get any valid path for the cert. Let's see if
      // the issuer is the same as the issuer for our canary probe. If yes, this
      // connection is connecting via a misconfigured proxy.
      // Note: The MitM canary might not be set. In this case we consider this
      // an unknown issuer error.
      nsCOMPtr<nsINSSComponent> component(
          do_GetService(PSM_COMPONENT_CONTRACTID));
      if (!component) {
        return Result::FATAL_ERROR_LIBRARY_FAILURE;
      }
      // IssuerMatchesMitmCanary succeeds if the issuer matches the canary and
      // the feature is enabled.
      Input issuerNameInput = peerBackCert.GetIssuer();
      SECItem issuerNameItem = UnsafeMapInputToSECItem(issuerNameInput);
      UniquePORTString issuerName(CERT_DerNameToAscii(&issuerNameItem));
      if (!issuerName) {
        return Result::ERROR_BAD_DER;
      }
      nsresult rv = component->IssuerMatchesMitmCanary(issuerName.get());
      if (NS_SUCCEEDED(rv)) {
        return Result::ERROR_MITM_DETECTED;
      }
    }
    // If the certificate is expired or not yet valid, first check whether or
    // not it is valid for the indicated hostname, because that would be a more
    // serious error.
    if (rv == Result::ERROR_EXPIRED_CERTIFICATE ||
        rv == Result::ERROR_NOT_YET_VALID_CERTIFICATE ||
        rv == Result::ERROR_INVALID_DER_TIME) {
      Result hostnameResult =
          CheckCertHostnameHelper(peerCertInput, hostname, false);
      if (hostnameResult != Success) {
        return hostnameResult;
      }
    }
    return rv;
  }

  if (dcInfo) {
    rv = IsDelegatedCredentialAcceptable(*dcInfo);
    if (rv != Success) {
      return rv;
    }
  }

  Input stapledOCSPResponseInput;
  Input* responseInputPtr = nullptr;
  if (stapledOCSPResponse) {
    rv = stapledOCSPResponseInput.Init(stapledOCSPResponse->Elements(),
                                       stapledOCSPResponse->Length());
    if (rv != Success) {
      // The stapled OCSP response was too big.
      return Result::ERROR_OCSP_MALFORMED_RESPONSE;
    }
    responseInputPtr = &stapledOCSPResponseInput;
  }

  if (!(flags & FLAG_TLS_IGNORE_STATUS_REQUEST)) {
    rv = CheckTLSFeaturesAreSatisfied(peerCertInput, responseInputPtr);
    if (rv != Success) {
      return rv;
    }
  }

  rv = CheckCertHostnameHelper(peerCertInput, hostname,
                               isBuiltChainRootBuiltInRootLocal);
  if ((rv == Success || rv == Result::ERROR_BAD_CERT_DOMAIN) &&
      isBuiltChainRootBuiltInRoot) {
    *isBuiltChainRootBuiltInRoot = isBuiltChainRootBuiltInRootLocal;
  }
  if (rv != Success) {
    return rv;
  }

  return Success;
}

// Take the (data, signature, subjectPublicKeyInfo, publicKeyAlgorithm,
// digestAlgorithm) tuple that defines a signature and derive a hash that
// uniquely identifies it. This is done by prefixing each variable-length
// component (data, signature, and subjectPublicKeyInfo) with
// sizeof(pkix::Input::size_type) bytes (currently 2) indicating the length of
// that component and concatenating them together, followed by one byte for the
// digestAlgorithm. The concatenation is then hashed with sha512.
// It should be computationally infeasible to find two distinct sets of inputs
// that have the same sha512 hash (and if it were possible, then it would be
// possible to break the signature scheme itself).
void HashSignatureParams(pkix::Input data, pkix::Input signature,
                         pkix::Input subjectPublicKeyInfo,
                         pkix::der::PublicKeyAlgorithm publicKeyAlgorithm,
                         pkix::DigestAlgorithm digestAlgorithm,
                         /*out*/ Maybe<nsTArray<uint8_t>>& sha512Hash) {
  sha512Hash.reset();
  Digest digest;
  if (NS_FAILED(digest.Begin(SEC_OID_SHA512))) {
    return;
  }
  pkix::Input::size_type dataLength = data.GetLength();
  if (NS_FAILED(digest.Update(reinterpret_cast<const uint8_t*>(&dataLength),
                              sizeof(dataLength)))) {
    return;
  }
  if (NS_FAILED(digest.Update(data.UnsafeGetData(), dataLength))) {
    return;
  }
  pkix::Input::size_type signatureLength = signature.GetLength();
  if (NS_FAILED(
          digest.Update(reinterpret_cast<const uint8_t*>(&signatureLength),
                        sizeof(signatureLength)))) {
    return;
  }
  if (NS_FAILED(digest.Update(signature.UnsafeGetData(), signatureLength))) {
    return;
  }
  pkix::Input::size_type spkiLength = subjectPublicKeyInfo.GetLength();
  if (NS_FAILED(digest.Update(reinterpret_cast<const uint8_t*>(&spkiLength),
                              sizeof(spkiLength)))) {
    return;
  }
  if (NS_FAILED(
          digest.Update(subjectPublicKeyInfo.UnsafeGetData(), spkiLength))) {
    return;
  }
  if (NS_FAILED(
          digest.Update(reinterpret_cast<const uint8_t*>(&publicKeyAlgorithm),
                        sizeof(publicKeyAlgorithm)))) {
    return;
  }
  if (NS_FAILED(
          digest.Update(reinterpret_cast<const uint8_t*>(&digestAlgorithm),
                        sizeof(digestAlgorithm)))) {
    return;
  }
  nsTArray<uint8_t> result;
  if (NS_FAILED(digest.End(result))) {
    return;
  }
  sha512Hash.emplace(std::move(result));
}

Result VerifySignedDataWithCache(
    der::PublicKeyAlgorithm publicKeyAlg,
    mozilla::glean::impl::DenominatorMetric telemetryDenominator,
    mozilla::glean::impl::NumeratorMetric telemetryNumerator, Input data,
    DigestAlgorithm digestAlgorithm, Input signature,
    Input subjectPublicKeyInfo, SignatureCache* signatureCache, void* pinArg) {
  telemetryDenominator.Add(1);
  Maybe<nsTArray<uint8_t>> sha512Hash;
  HashSignatureParams(data, signature, subjectPublicKeyInfo, publicKeyAlg,
                      digestAlgorithm, sha512Hash);
  // If hashing the signature parameters succeeded, see if this signature is in
  // the signature cache.
  if (sha512Hash.isSome() &&
      signature_cache_get(signatureCache, sha512Hash.ref().Elements())) {
    telemetryNumerator.AddToNumerator(1);
    return Success;
  }
  Result result;
  switch (publicKeyAlg) {
    case der::PublicKeyAlgorithm::ECDSA:
      result = VerifyECDSASignedDataNSS(data, digestAlgorithm, signature,
                                        subjectPublicKeyInfo, pinArg);
      break;
    case der::PublicKeyAlgorithm::RSA_PKCS1:
      result = VerifyRSAPKCS1SignedDataNSS(data, digestAlgorithm, signature,
                                           subjectPublicKeyInfo, pinArg);
      break;
    case der::PublicKeyAlgorithm::RSA_PSS:
      result = VerifyRSAPSSSignedDataNSS(data, digestAlgorithm, signature,
                                         subjectPublicKeyInfo, pinArg);
      break;
    case der::PublicKeyAlgorithm::ML_DSA:
      result = VerifyMLDSASignedDataNSS(data, signature, subjectPublicKeyInfo,
                                        pinArg);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unhandled public key algorithm");
      return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }
  // Add this signature to the signature cache.
  if (sha512Hash.isSome() && result == Success) {
    signature_cache_insert(signatureCache, sha512Hash.ref().Elements());
  }
  return result;
}

}  // namespace psm
}  // namespace mozilla
