/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NSSCertDBTrustDomain.h"

#include <stdint.h>
#include <utility>

#include "CRLiteTimestamp.h"
#include "ExtendedValidation.h"
#include "MultiLogCTVerifier.h"
#include "NSSErrorsService.h"
#include "PKCS11ModuleDB.h"
#include "PublicKeyPinningService.h"
#include "cert.h"
#include "cert_storage/src/cert_storage.h"
#include "certdb.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Assertions.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Logging.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/glean/SecurityCertverifierMetrics.h"
#include "mozpkix/Result.h"
#include "mozpkix/pkix.h"
#include "mozpkix/pkixcheck.h"
#include "mozpkix/pkixnss.h"
#include "mozpkix/pkixutil.h"
#include "nsCRTGlue.h"
#include "nsIObserverService.h"
#include "nsNSSCallbacks.h"
#include "nsNSSCertificate.h"
#include "nsNSSCertificateDB.h"
#include "nsNSSComponent.h"
#include "nsNSSIOLayer.h"
#include "nsNetCID.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nss.h"
#include "pk11pub.h"
#include "prerror.h"
#include "secder.h"
#include "secerr.h"

using namespace mozilla;
using namespace mozilla::ct;
using namespace mozilla::pkix;

extern LazyLogModule gCertVerifierLog;

static const uint64_t ServerFailureDelaySeconds = 5 * 60;

namespace mozilla {
namespace psm {

NSSCertDBTrustDomain::NSSCertDBTrustDomain(
    SECTrustType certDBTrustType, RevocationCheckMode ocspFetching,
    OCSPCache& ocspCache, SignatureCache* signatureCache,
    TrustCache* trustCache, /*optional but shouldn't be*/ void* pinArg,
    TimeDuration ocspTimeoutSoft, TimeDuration ocspTimeoutHard,
    uint32_t certShortLifetimeInDays, unsigned int minRSABits,
    CRLiteMode crliteMode, const OriginAttributes& originAttributes,
    const nsTArray<Input>& thirdPartyRootInputs,
    const nsTArray<Input>& thirdPartyIntermediateInputs,
    const Maybe<nsTArray<nsTArray<uint8_t>>>& extraCertificates,
    const mozilla::pkix::Input& encodedSCTsFromTLS,
    const UniquePtr<mozilla::ct::MultiLogCTVerifier>& ctVerifier,
    /*out*/ nsTArray<nsTArray<uint8_t>>& builtChain,
    /*optional*/ PinningTelemetryInfo* pinningTelemetryInfo,
    /*optional*/ const char* hostname)
    : mCertDBTrustType(certDBTrustType),
      mOCSPFetching(ocspFetching),
      mOCSPCache(ocspCache),
      mSignatureCache(signatureCache),
      mTrustCache(trustCache),
      mPinArg(pinArg),
      mOCSPTimeoutSoft(ocspTimeoutSoft),
      mOCSPTimeoutHard(ocspTimeoutHard),
      mCertShortLifetimeInDays(certShortLifetimeInDays),
      mMinRSABits(minRSABits),
      mCRLiteMode(crliteMode),
      mOriginAttributes(originAttributes),
      mThirdPartyRootInputs(thirdPartyRootInputs),
      mThirdPartyIntermediateInputs(thirdPartyIntermediateInputs),
      mExtraCertificates(extraCertificates),
      mEncodedSCTsFromTLS(encodedSCTsFromTLS),
      mCTVerifier(ctVerifier),
      mBuiltChain(builtChain),
      mIsBuiltChainRootBuiltInRoot(false),
      mPinningTelemetryInfo(pinningTelemetryInfo),
      mHostname(hostname),
      mCertStorage(do_GetService(NS_CERT_STORAGE_CID)),
      mOCSPStaplingStatus(CertVerifier::OCSP_STAPLING_NEVER_CHECKED),
      mBuiltInRootsModule(SECMOD_FindModule(kRootModuleName.get())),
      mOCSPFetchStatus(OCSPFetchStatus::NotFetched) {}

static void FindRootsWithSubject(UniqueSECMODModule& rootsModule,
                                 SECItem subject,
                                 /*out*/ nsTArray<nsTArray<uint8_t>>& roots) {
  MOZ_ASSERT(rootsModule);
  AutoSECMODListReadLock lock;
  for (int slotIndex = 0; slotIndex < rootsModule->slotCount; slotIndex++) {
    CERTCertificateList* rawResults = nullptr;
    if (PK11_FindRawCertsWithSubject(rootsModule->slots[slotIndex], &subject,
                                     &rawResults) != SECSuccess) {
      continue;
    }
    // rawResults == nullptr means we didn't find any matching certificates
    if (!rawResults) {
      continue;
    }
    UniqueCERTCertificateList results(rawResults);
    for (int certIndex = 0; certIndex < results->len; certIndex++) {
      nsTArray<uint8_t> root;
      root.AppendElements(results->certs[certIndex].data,
                          results->certs[certIndex].len);
      roots.AppendElement(std::move(root));
    }
  }
}

// A self-signed issuer certificate should never be necessary in order to build
// a trusted certificate chain unless it is a trust anchor. This is because if
// it were necessary, there would exist another certificate with the same
// subject and public key that is also a valid issing certificate. Given this
// certificate, it is possible to build another chain using just it instead of
// it and the self-signed certificate. This is only true as long as the
// certificate extensions we support are restrictive rather than additive in
// terms of the rest of the chain (for example, we don't support policy mapping
// and we ignore any SCT information in intermediates).
bool NSSCertDBTrustDomain::ShouldSkipSelfSignedNonTrustAnchor(Input certDER) {
  BackCert cert(certDER, EndEntityOrCA::MustBeCA, nullptr);
  if (cert.Init() != Success) {
    return false;  // turn any failures into "don't skip trying this cert"
  }
  // If subject != issuer, this isn't a self-signed cert.
  if (!InputsAreEqual(cert.GetSubject(), cert.GetIssuer())) {
    return false;
  }
  TrustLevel trust;
  if (GetCertTrust(EndEntityOrCA::MustBeCA, CertPolicyId::anyPolicy, certDER,
                   trust) != Success) {
    return false;
  }
  // If the trust for this certificate is anything other than "inherit", we want
  // to process it like normal.
  if (trust != TrustLevel::InheritsTrust) {
    return false;
  }
  if (VerifySignedData(*this, cert.GetSignedData(),
                       cert.GetSubjectPublicKeyInfo()) != Success) {
    return false;
  }
  // This is a self-signed, non-trust-anchor certificate, so we shouldn't use it
  // for path building. See bug 1056341.
  return true;
}

Result NSSCertDBTrustDomain::CheckCandidates(
    IssuerChecker& checker, nsTArray<IssuerCandidateWithSource>& candidates,
    Input* nameConstraintsInputPtr, bool& keepGoing) {
  for (const auto& candidate : candidates) {
    // Stop path building if the program is shutting down.
    if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
      keepGoing = false;
      return Success;
    }
    if (ShouldSkipSelfSignedNonTrustAnchor(candidate.mDER)) {
      continue;
    }
    Result rv =
        checker.Check(candidate.mDER, nameConstraintsInputPtr, keepGoing);
    if (rv != Success) {
      return rv;
    }
    if (!keepGoing) {
      mIssuerSources += candidate.mIssuerSource;
      return Success;
    }

    ResetCandidateBuiltChainState();
  }

  return Success;
}

Result NSSCertDBTrustDomain::FindIssuer(Input encodedIssuerName,
                                        IssuerChecker& checker, Time) {
  SECItem encodedIssuerNameItem = UnsafeMapInputToSECItem(encodedIssuerName);
  // Handle imposed name constraints, if any.
  ScopedAutoSECItem nameConstraints;
  Input nameConstraintsInput;
  Input* nameConstraintsInputPtr = nullptr;
  SECStatus srv =
      CERT_GetImposedNameConstraints(&encodedIssuerNameItem, &nameConstraints);
  if (srv == SECSuccess) {
    if (nameConstraintsInput.Init(nameConstraints.data, nameConstraints.len) !=
        Success) {
      return Result::FATAL_ERROR_LIBRARY_FAILURE;
    }
    nameConstraintsInputPtr = &nameConstraintsInput;
  } else if (PR_GetError() != SEC_ERROR_EXTENSION_NOT_FOUND) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  // First try all relevant certificates known to Gecko, which avoids calling
  // CERT_CreateSubjectCertList, because that can be expensive.
  nsTArray<IssuerCandidateWithSource> geckoRootCandidates;
  nsTArray<IssuerCandidateWithSource> geckoIntermediateCandidates;

  // We might not have this module if e.g. we're on a Linux distribution that
  // does something unexpected.
  nsTArray<nsTArray<uint8_t>> builtInRoots;
  if (mBuiltInRootsModule) {
    FindRootsWithSubject(mBuiltInRootsModule, encodedIssuerNameItem,
                         builtInRoots);
    for (const auto& root : builtInRoots) {
      Input rootInput;
      Result rv = rootInput.Init(root.Elements(), root.Length());
      if (rv != Success) {
        continue;  // probably too big
      }
      geckoRootCandidates.AppendElement(IssuerCandidateWithSource{
          rootInput, IssuerSource::BuiltInRootsModule});
    }
  } else {
    MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
            ("NSSCertDBTrustDomain::FindIssuer: no built-in roots module"));
  }

  if (mExtraCertificates.isSome()) {
    for (const auto& extraCert : *mExtraCertificates) {
      Input certInput;
      Result rv = certInput.Init(extraCert.Elements(), extraCert.Length());
      if (rv != Success) {
        continue;
      }
      BackCert cert(certInput, EndEntityOrCA::MustBeCA, nullptr);
      rv = cert.Init();
      if (rv != Success) {
        continue;
      }
      // Filter out certificates that can't be issuers we're looking for because
      // the subject distinguished name doesn't match. This prevents
      // mozilla::pkix from accumulating spurious errors during path building.
      if (!InputsAreEqual(encodedIssuerName, cert.GetSubject())) {
        continue;
      }
      // We assume that extra certificates (presumably from the TLS handshake)
      // are intermediates, since sending trust anchors would be superfluous.
      geckoIntermediateCandidates.AppendElement(
          IssuerCandidateWithSource{certInput, IssuerSource::TLSHandshake});
    }
  }

  for (const auto& thirdPartyRootInput : mThirdPartyRootInputs) {
    BackCert root(thirdPartyRootInput, EndEntityOrCA::MustBeCA, nullptr);
    Result rv = root.Init();
    if (rv != Success) {
      continue;
    }
    // Filter out 3rd party roots that can't be issuers we're looking for
    // because the subject distinguished name doesn't match. This prevents
    // mozilla::pkix from accumulating spurious errors during path building.
    if (!InputsAreEqual(encodedIssuerName, root.GetSubject())) {
      continue;
    }
    geckoRootCandidates.AppendElement(IssuerCandidateWithSource{
        thirdPartyRootInput, IssuerSource::ThirdPartyCertificates});
  }

  for (const auto& thirdPartyIntermediateInput :
       mThirdPartyIntermediateInputs) {
    BackCert intermediate(thirdPartyIntermediateInput, EndEntityOrCA::MustBeCA,
                          nullptr);
    Result rv = intermediate.Init();
    if (rv != Success) {
      continue;
    }
    // Filter out 3rd party intermediates that can't be issuers we're looking
    // for because the subject distinguished name doesn't match. This prevents
    // mozilla::pkix from accumulating spurious errors during path building.
    if (!InputsAreEqual(encodedIssuerName, intermediate.GetSubject())) {
      continue;
    }
    geckoIntermediateCandidates.AppendElement(IssuerCandidateWithSource{
        thirdPartyIntermediateInput, IssuerSource::ThirdPartyCertificates});
  }

  if (!mCertStorage) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }
  nsTArray<uint8_t> subject;
  subject.AppendElements(encodedIssuerName.UnsafeGetData(),
                         encodedIssuerName.GetLength());
  nsTArray<nsTArray<uint8_t>> certs;
  nsresult rv = mCertStorage->FindCertsBySubject(subject, certs);
  if (NS_FAILED(rv)) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }
  for (auto& cert : certs) {
    Input certDER;
    Result rv = certDER.Init(cert.Elements(), cert.Length());
    if (rv != Success) {
      continue;  // probably too big
    }
    // Currently we're only expecting intermediate certificates in cert storage.
    geckoIntermediateCandidates.AppendElement(IssuerCandidateWithSource{
        std::move(certDER), IssuerSource::PreloadedIntermediates});
  }

  // Try all root certs first and then all (presumably) intermediates.
  geckoRootCandidates.AppendElements(std::move(geckoIntermediateCandidates));

  bool keepGoing = true;
  Result result = CheckCandidates(checker, geckoRootCandidates,
                                  nameConstraintsInputPtr, keepGoing);
  if (result != Success) {
    return result;
  }
  if (!keepGoing) {
    return Success;
  }

  // Synchronously dispatch a task to the socket thread to find
  // CERTCertificates with the given subject. This involves querying NSS
  // structures and databases, so it should be done on the socket thread.
  nsTArray<nsTArray<uint8_t>> nssRootCandidates;
  nsTArray<nsTArray<uint8_t>> nssIntermediateCandidates;
  RefPtr<Runnable> getCandidatesTask =
      NS_NewRunnableFunction("NSSCertDBTrustDomain::FindIssuer", [&]() {
        if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
          return;
        }
        // NSS seems not to differentiate between "no potential issuers found"
        // and "there was an error trying to retrieve the potential issuers." We
        // assume there was no error if CERT_CreateSubjectCertList returns
        // nullptr.
        UniqueCERTCertList candidates(
            CERT_CreateSubjectCertList(nullptr, CERT_GetDefaultCertDB(),
                                       &encodedIssuerNameItem, 0, false));
        if (candidates) {
          for (CERTCertListNode* n = CERT_LIST_HEAD(candidates);
               !CERT_LIST_END(n, candidates); n = CERT_LIST_NEXT(n)) {
            nsTArray<uint8_t> candidate;
            candidate.AppendElements(n->cert->derCert.data,
                                     n->cert->derCert.len);
            if (n->cert->isRoot) {
              nssRootCandidates.AppendElement(std::move(candidate));
            } else {
              nssIntermediateCandidates.AppendElement(std::move(candidate));
            }
          }
        }
      });
  nsCOMPtr<nsIEventTarget> socketThread(
      do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID));
  if (!socketThread) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }
  rv = SyncRunnable::DispatchToThread(socketThread, getCandidatesTask);
  if (NS_FAILED(rv)) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  nsTArray<IssuerCandidateWithSource> nssCandidates;
  for (const auto& rootCandidate : nssRootCandidates) {
    Input certDER;
    Result rv = certDER.Init(rootCandidate.Elements(), rootCandidate.Length());
    if (rv != Success) {
      continue;  // probably too big
    }
    nssCandidates.AppendElement(
        IssuerCandidateWithSource{std::move(certDER), IssuerSource::NSSCertDB});
  }
  for (const auto& intermediateCandidate : nssIntermediateCandidates) {
    Input certDER;
    Result rv = certDER.Init(intermediateCandidate.Elements(),
                             intermediateCandidate.Length());
    if (rv != Success) {
      continue;  // probably too big
    }
    nssCandidates.AppendElement(
        IssuerCandidateWithSource{std::move(certDER), IssuerSource::NSSCertDB});
  }

  return CheckCandidates(checker, nssCandidates, nameConstraintsInputPtr,
                         keepGoing);
}

void HashTrustParams(EndEntityOrCA endEntityOrCA, const CertPolicyId& policy,
                     Input certDER, SECTrustType trustType,
                     /*out*/ Maybe<nsTArray<uint8_t>>& sha512Hash) {
  sha512Hash.reset();
  Digest digest;
  if (NS_FAILED(digest.Begin(SEC_OID_SHA512))) {
    return;
  }
  if (NS_FAILED(digest.Update(reinterpret_cast<const uint8_t*>(&endEntityOrCA),
                              sizeof(endEntityOrCA)))) {
    return;
  }
  if (NS_FAILED(
          digest.Update(reinterpret_cast<const uint8_t*>(&policy.numBytes),
                        sizeof(policy.numBytes)))) {
    return;
  }
  if (NS_FAILED(digest.Update(policy.bytes, policy.numBytes))) {
    return;
  }
  if (NS_FAILED(digest.Update(certDER.UnsafeGetData(), certDER.GetLength()))) {
    return;
  }
  if (NS_FAILED(digest.Update(reinterpret_cast<const uint8_t*>(&trustType),
                              sizeof(trustType)))) {
    return;
  }
  nsTArray<uint8_t> result;
  if (NS_FAILED(digest.End(result))) {
    return;
  }
  sha512Hash.emplace(std::move(result));
}

Result NSSCertDBTrustDomain::GetCertTrust(EndEntityOrCA endEntityOrCA,
                                          const CertPolicyId& policy,
                                          Input candidateCertDER,
                                          /*out*/ TrustLevel& trustLevel) {
  // Check the certificate against the OneCRL cert blocklist
  if (!mCertStorage) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  // The certificate blocklist currently only applies to TLS server
  // certificates.
  if (mCertDBTrustType == trustSSL) {
    int16_t revocationState;

    nsTArray<uint8_t> issuerBytes;
    nsTArray<uint8_t> serialBytes;
    nsTArray<uint8_t> subjectBytes;
    nsTArray<uint8_t> pubKeyBytes;

    Result result =
        BuildRevocationCheckArrays(candidateCertDER, endEntityOrCA, issuerBytes,
                                   serialBytes, subjectBytes, pubKeyBytes);
    if (result != Success) {
      return result;
    }

    nsresult nsrv = mCertStorage->GetRevocationState(
        issuerBytes, serialBytes, subjectBytes, pubKeyBytes, &revocationState);
    if (NS_FAILED(nsrv)) {
      return Result::FATAL_ERROR_LIBRARY_FAILURE;
    }

    if (revocationState == nsICertStorage::STATE_ENFORCE) {
      MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
              ("NSSCertDBTrustDomain: certificate is in blocklist"));
      mozilla::glean::cert_verifier::cert_revocation_mechanisms.Get("OneCRL"_ns)
          .Add(1);
      return Result::ERROR_REVOKED_CERTIFICATE;
    }
  }

  // This may be a third-party root.
  for (const auto& thirdPartyRootInput : mThirdPartyRootInputs) {
    if (InputsAreEqual(candidateCertDER, thirdPartyRootInput)) {
      trustLevel = TrustLevel::TrustAnchor;
      return Success;
    }
  }

  // This may be a third-party intermediate.
  for (const auto& thirdPartyIntermediateInput :
       mThirdPartyIntermediateInputs) {
    if (InputsAreEqual(candidateCertDER, thirdPartyIntermediateInput)) {
      trustLevel = TrustLevel::InheritsTrust;
      return Success;
    }
  }

  mozilla::glean::cert_trust_cache::total.Add(1);
  Maybe<nsTArray<uint8_t>> sha512Hash;
  HashTrustParams(endEntityOrCA, policy, candidateCertDER, mCertDBTrustType,
                  sha512Hash);
  uint8_t cachedTrust = 0;
  if (sha512Hash.isSome() &&
      trust_cache_get(mTrustCache, sha512Hash.ref().Elements(), &cachedTrust)) {
    mozilla::glean::cert_trust_cache::hits.AddToNumerator(1);
    trustLevel = static_cast<TrustLevel>(cachedTrust);
    return Success;
  }

  // Synchronously dispatch a task to the socket thread to construct a
  // CERTCertificate and get its trust from NSS. This involves querying NSS
  // structures and databases, so it should be done on the socket thread.
  Result result = Result::FATAL_ERROR_LIBRARY_FAILURE;
  RefPtr<Runnable> getTrustTask =
      NS_NewRunnableFunction("NSSCertDBTrustDomain::GetCertTrust", [&]() {
        if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
          result = Result::FATAL_ERROR_LIBRARY_FAILURE;
          return;
        }
        // This would be cleaner and more efficient if we could get the trust
        // information without constructing a CERTCertificate here, but NSS
        // doesn't expose it in any other easy-to-use fashion. The use of
        // CERT_NewTempCertificate to get a CERTCertificate shouldn't be a
        // performance problem for certificates already known to NSS because NSS
        // will just find the existing CERTCertificate in its in-memory cache
        // and return it. For certificates not already in NSS (namely
        // third-party roots and intermediates), we want to avoid calling
        // CERT_NewTempCertificate repeatedly, so we've already checked if the
        // candidate certificate is a third-party certificate, above.
        SECItem candidateCertDERSECItem =
            UnsafeMapInputToSECItem(candidateCertDER);

        UniqueCERTCertificate candidateCert(CERT_NewTempCertificate(
            CERT_GetDefaultCertDB(), &candidateCertDERSECItem, nullptr, false,
            true));
        if (!candidateCert) {
          result = MapPRErrorCodeToResult(PR_GetError());
          return;
        }
        // NB: CERT_GetCertTrust seems to be abusing SECStatus as a boolean,
        // where SECSuccess means that there is a trust record and SECFailure
        // means there is not a trust record. I looked at NSS's internal uses of
        // CERT_GetCertTrust, and all that code uses the result as a boolean
        // meaning "We have a trust record."

        CERTCertTrust trust;
        if (CERT_GetCertTrust(candidateCert.get(), &trust) == SECSuccess) {
          uint32_t flags = SEC_GET_TRUST_FLAGS(&trust, mCertDBTrustType);

          // For DISTRUST, we use the CERTDB_TRUSTED or CERTDB_TRUSTED_CA bit,
          // because we can have active distrust for either type of cert. Note
          // that CERTDB_TERMINAL_RECORD means "stop trying to inherit trust" so
          // if the relevant trust bit isn't set then that means the cert must
          // be considered distrusted.
          uint32_t relevantTrustBit = endEntityOrCA == EndEntityOrCA::MustBeCA
                                          ? CERTDB_TRUSTED_CA
                                          : CERTDB_TRUSTED;
          if (((flags & (relevantTrustBit | CERTDB_TERMINAL_RECORD))) ==
              CERTDB_TERMINAL_RECORD) {
            trustLevel = TrustLevel::ActivelyDistrusted;
            result = Success;
            return;
          }

          // For TRUST, we use the CERTDB_TRUSTED_CA bit.
          if (flags & CERTDB_TRUSTED_CA) {
            if (policy.IsAnyPolicy()) {
              trustLevel = TrustLevel::TrustAnchor;
              result = Success;
              return;
            }

            nsTArray<uint8_t> certBytes(candidateCert->derCert.data,
                                        candidateCert->derCert.len);
            if (CertIsAuthoritativeForEVPolicy(certBytes, policy)) {
              trustLevel = TrustLevel::TrustAnchor;
              result = Success;
              return;
            }
          }
        }
        trustLevel = TrustLevel::InheritsTrust;
        result = Success;
      });
  nsCOMPtr<nsIEventTarget> socketThread(
      do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID));
  if (!socketThread) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }
  nsresult rv = SyncRunnable::DispatchToThread(socketThread, getTrustTask);
  if (NS_FAILED(rv)) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }
  if (result == Success && sha512Hash.isSome()) {
    uint8_t trust = static_cast<uint8_t>(trustLevel);
    trust_cache_insert(mTrustCache, sha512Hash.ref().Elements(), trust);
  }
  return result;
}

Result NSSCertDBTrustDomain::DigestBuf(Input item, DigestAlgorithm digestAlg,
                                       /*out*/ uint8_t* digestBuf,
                                       size_t digestBufLen) {
  return DigestBufNSS(item, digestAlg, digestBuf, digestBufLen);
}

TimeDuration NSSCertDBTrustDomain::GetOCSPTimeout() const {
  switch (mOCSPFetching) {
    case NSSCertDBTrustDomain::RevocationCheckMayFetch:
      return mOCSPTimeoutSoft;
    case NSSCertDBTrustDomain::RevocationCheckRequired:
      return mOCSPTimeoutHard;
    // Reaching this case is an error. Assert in debug builds, but return the
    // soft timeout value in release builds.
    case NSSCertDBTrustDomain::RevocationCheckLocalOnly:
      MOZ_ASSERT_UNREACHABLE(
          "we should never see this RevocationCheckMode type here");
      break;
  }

  MOZ_ASSERT_UNREACHABLE("we're not handling every RevocationCheckMode type");
  return mOCSPTimeoutSoft;
}

// Copied and modified from CERT_GetOCSPAuthorityInfoAccessLocation and
// CERT_GetGeneralNameByType. Returns a non-Result::Success result on error,
// Success with result.IsVoid() == true when an OCSP URI was not found, and
// Success with result.IsVoid() == false when an OCSP URI was found.
static Result GetOCSPAuthorityInfoAccessLocation(const UniquePLArenaPool& arena,
                                                 Input aiaExtension,
                                                 /*out*/ nsCString& result) {
  MOZ_ASSERT(arena.get());
  if (!arena.get()) {
    return Result::FATAL_ERROR_INVALID_ARGS;
  }

  result.Assign(VoidCString());
  SECItem aiaExtensionSECItem = UnsafeMapInputToSECItem(aiaExtension);
  CERTAuthInfoAccess** aia =
      CERT_DecodeAuthInfoAccessExtension(arena.get(), &aiaExtensionSECItem);
  if (!aia) {
    return Result::ERROR_CERT_BAD_ACCESS_LOCATION;
  }
  for (size_t i = 0; aia[i]; ++i) {
    if (SECOID_FindOIDTag(&aia[i]->method) == SEC_OID_PKIX_OCSP) {
      // NSS chooses the **last** OCSP URL; we choose the **first**
      CERTGeneralName* current = aia[i]->location;
      if (!current) {
        continue;
      }
      do {
        if (current->type == certURI) {
          const SECItem& location = current->name.other;
          // (location.len + 1) must be small enough to fit into a uint32_t,
          // but we limit it to a smaller bound to reduce OOM risk.
          if (location.len > 1024 || memchr(location.data, 0, location.len)) {
            // Reject embedded nulls. (NSS doesn't do this)
            return Result::ERROR_CERT_BAD_ACCESS_LOCATION;
          }
          result.Assign(nsDependentCSubstring(
              reinterpret_cast<const char*>(location.data), location.len));
          return Success;
        }
        current = CERT_GetNextGeneralName(current);
      } while (current != aia[i]->location);
    }
  }

  return Success;
}

NS_IMPL_ISUPPORTS(CRLiteTimestamp, nsICRLiteTimestamp)

NS_IMETHODIMP
CRLiteTimestamp::GetLogID(nsTArray<uint8_t>& aLogID) {
  aLogID.Clear();
  aLogID.AppendElements(mLogID);
  return NS_OK;
}

NS_IMETHODIMP
CRLiteTimestamp::GetTimestamp(uint64_t* aTimestamp) {
  *aTimestamp = mTimestamp;
  return NS_OK;
}

static void RecordCRLiteNotCoveredCertAge(
    const nsTArray<RefPtr<nsICRLiteTimestamp>>& timestamps, Time time) {
  if (timestamps.IsEmpty()) {
    return;
  }
  uint64_t mostRecentMs = 0;
  for (const auto& ts : timestamps) {
    uint64_t timestampMs = 0;
    if (NS_SUCCEEDED(ts->GetTimestamp(&timestampMs))) {
      mostRecentMs = std::max(mostRecentMs, timestampMs);
    }
  }
  if (mostRecentMs == 0) {
    return;
  }
  uint64_t timeSeconds = 0;
  if (SecondsSinceEpochFromTime(time, &timeSeconds) != Success) {
    return;
  }
  uint64_t timeMs = timeSeconds * 1000;
  if (timeMs < mostRecentMs) {
    return;
  }
  mozilla::glean::cert_verifier::crlite_not_covered_cert_age
      .AccumulateRawDuration(TimeDuration::FromMilliseconds(
          static_cast<double>(timeMs - mostRecentMs)));
}

Result NSSCertDBTrustDomain::CheckCRLite(
    const nsTArray<uint8_t>& issuerSubjectPublicKeyInfoBytes,
    const nsTArray<uint8_t>& serialNumberBytes,
    const nsTArray<RefPtr<nsICRLiteTimestamp>>& timestamps, Time time,
    /*out*/ bool& filterCoversCertificate) {
  filterCoversCertificate = false;
  int16_t crliteRevocationState;
  nsresult rv = mCertStorage->GetCRLiteRevocationState(
      issuerSubjectPublicKeyInfoBytes, serialNumberBytes, timestamps,
      &crliteRevocationState);
  if (NS_FAILED(rv)) {
    MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
            ("NSSCertDBTrustDomain::CheckCRLite: CRLite call failed"));
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }
  MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
          ("NSSCertDBTrustDomain::CheckCRLite: CRLite check returned "
           "state=%hd",
           crliteRevocationState));

  switch (crliteRevocationState) {
    case nsICertStorage::STATE_ENFORCE:
      filterCoversCertificate = true;
      mozilla::glean::cert_verifier::crlite_status.Get("revoked_in_filter"_ns)
          .Add(1);
      return Result::ERROR_REVOKED_CERTIFICATE;
    case nsICertStorage::STATE_UNSET:
      filterCoversCertificate = true;
      mozilla::glean::cert_verifier::crlite_status.Get("not_revoked"_ns).Add(1);
      return Success;
    case nsICertStorage::STATE_NOT_ENROLLED:
      filterCoversCertificate = false;
      mozilla::glean::cert_verifier::crlite_status.Get("not_enrolled"_ns)
          .Add(1);
      return Success;
    case nsICertStorage::STATE_NOT_COVERED:
      filterCoversCertificate = false;
      mozilla::glean::cert_verifier::crlite_status.Get("not_covered"_ns).Add(1);
      RecordCRLiteNotCoveredCertAge(timestamps, time);
      return Success;
    case nsICertStorage::STATE_NO_FILTER:
      filterCoversCertificate = false;
      mozilla::glean::cert_verifier::crlite_status.Get("no_filter"_ns).Add(1);
      return Success;
    default:
      MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
              ("NSSCertDBTrustDomain::CheckCRLite: Unknown CRLite revocation "
               "state"));
      return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }
}

Result NSSCertDBTrustDomain::CheckRevocation(
    EndEntityOrCA endEntityOrCA, const CertID& certID, Time time,
    Duration validityDuration,
    /*optional*/ const Input* stapledOCSPResponse,
    /*optional*/ const Input* aiaExtension) {
  // Actively distrusted certificates will have already been blocked by
  // GetCertTrust.

  MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
          ("NSSCertDBTrustDomain: Top of CheckRevocation\n"));

  // None of the revocation methods in this function are consulted for CA
  // certificates. Revocation for CAs is handled by GetCertTrust.
  if (endEntityOrCA == EndEntityOrCA::MustBeCA) {
    return Success;
  }

  bool crliteCoversCertificate = false;
  Result crliteResult = Success;
  if (mCRLiteMode != CRLiteMode::Disabled) {
    crliteResult =
        CheckRevocationByCRLite(certID, time, crliteCoversCertificate);

    // If CheckCRLite returned an error other than "revoked certificate",
    // propagate that error.
    if (crliteResult != Success &&
        crliteResult != Result::ERROR_REVOKED_CERTIFICATE) {
      return crliteResult;
    }

    if (crliteCoversCertificate) {
      mozilla::glean::cert_verifier::cert_revocation_mechanisms.Get("CRLite"_ns)
          .Add(1);
      if (mCRLiteMode == CRLiteMode::Enforce) {
        return crliteResult;
      }
    }
  }

  nsCString aiaLocation(VoidCString());
  if (aiaExtension) {
    UniquePLArenaPool arena(PORT_NewArena(DER_DEFAULT_CHUNKSIZE));
    if (!arena) {
      return Result::FATAL_ERROR_NO_MEMORY;
    }
    Result rv =
        GetOCSPAuthorityInfoAccessLocation(arena, *aiaExtension, aiaLocation);
    if (rv != Success) {
      return rv;
    }
  }

  Result ocspResult = CheckRevocationByOCSP(certID, time, validityDuration,
                                            aiaLocation, stapledOCSPResponse);

  MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
          ("NSSCertDBTrustDomain: end of CheckRevocation"));

  return ocspResult;
}

Result NSSCertDBTrustDomain::CheckRevocationByCRLite(
    const CertID& certID, Time time, /*out*/ bool& crliteCoversCertificate) {
  crliteCoversCertificate = false;
  MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
          ("NSSCertDBTrustDomain::CheckRevocation: checking CRLite"));

  nsTArray<uint8_t> issuerSubjectPublicKeyInfoBytes;
  issuerSubjectPublicKeyInfoBytes.AppendElements(
      certID.issuerSubjectPublicKeyInfo.UnsafeGetData(),
      certID.issuerSubjectPublicKeyInfo.GetLength());
  nsTArray<uint8_t> serialNumberBytes;
  serialNumberBytes.AppendElements(certID.serialNumber.UnsafeGetData(),
                                   certID.serialNumber.GetLength());

  nsTArray<RefPtr<nsICRLiteTimestamp>> timestamps;

  // CRLite relies on timestamps from SCTs, so we should check signatures on
  // SCTs before calling into CRLite. However, the risk of using unverified
  // timestamps (particularly from embedded SCTs) is marginal, and if CT is
  // disabled we will pass unverified timestamps from embedded SCTs to CRLite.
  if (GetCertificateTransparencyMode() ==
      CertVerifier::CertificateTransparencyMode::Disabled) {
    size_t decodingErrors;
    std::vector<SignedCertificateTimestamp> decodedSCTsFromExtension;
    DecodeSCTs(GetSCTListFromCertificate(), decodedSCTsFromExtension,
               decodingErrors);
    (void)decodingErrors;
    for (const auto& sct : decodedSCTsFromExtension) {
      timestamps.AppendElement(new CRLiteTimestamp(sct));
    }

    return CheckCRLite(issuerSubjectPublicKeyInfoBytes, serialNumberBytes,
                       timestamps, time, crliteCoversCertificate);
  }

  // When CT is enabled, we verify the signatures on all available SCTs and
  // cache the verification result in the trust domain so that it can be used
  // for CT policy enforcement. The verification result only depends on the end
  // entity certificate and the issuer SPKI, so it is path-independent and we
  // only need to compute it once.
  if (mCTVerifyResult.isNothing()) {
    MOZ_ASSERT(mBuiltChain.Length() > 0);

    CTVerifyResult ctVerifyResult;
    Input leafCertificate;
    const nsTArray<uint8_t>& endEntityBytes = mBuiltChain.ElementAt(0);
    Result rv = leafCertificate.Init(endEntityBytes.Elements(),
                                     endEntityBytes.Length());
    if (rv != Success) {
      return rv;
    }

    Input encodedSCTsFromOCSP;  // empty since we haven't done OCSP yet.
    rv = mCTVerifier->Verify(leafCertificate, certID.issuerSubjectPublicKeyInfo,
                             GetSCTListFromCertificate(), encodedSCTsFromOCSP,
                             mEncodedSCTsFromTLS, time, GetDistrustAfterTime(),
                             ctVerifyResult);
    if (rv != Success) {
      MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
              ("SCT verification failed with fatal error %" PRId32 "\n",
               static_cast<uint32_t>(rv)));
      return rv;
    }

    mCTVerifyResult.emplace(std::move(ctVerifyResult));
  }

  if (mCTVerifyResult.isSome()) {
    for (const auto& sct : mCTVerifyResult->verifiedScts) {
      timestamps.AppendElement(new CRLiteTimestamp(sct));
    }
  }

  return CheckCRLite(issuerSubjectPublicKeyInfoBytes, serialNumberBytes,
                     timestamps, time, crliteCoversCertificate);
}

Result NSSCertDBTrustDomain::CheckRevocationByOCSP(
    const CertID& certID, Time time, Duration validityDuration,
    const nsCString& aiaLocation,
    /*optional*/ const Input* stapledOCSPResponse) {
  const uint16_t maxOCSPLifetimeInDays = 10;
  // If we have a stapled OCSP response then the verification of that response
  // determines the result unless the OCSP response is expired. We make an
  // exception for expired responses because some servers, nginx in particular,
  // are known to serve expired responses due to bugs.
  // We keep track of the result of verifying the stapled response but don't
  // immediately return failure if the response has expired.
  Result stapledOCSPResponseResult = Success;
  if (stapledOCSPResponse) {
    bool expired;
    stapledOCSPResponseResult = VerifyAndMaybeCacheEncodedOCSPResponse(
        certID, time, maxOCSPLifetimeInDays, *stapledOCSPResponse,
        ResponseWasStapled, expired);
    mozilla::glean::cert_verifier::cert_revocation_mechanisms
        .Get("StapledOCSP"_ns)
        .Add(1);
    if (stapledOCSPResponseResult == Success) {
      // stapled OCSP response present and good
      mOCSPStaplingStatus = CertVerifier::OCSP_STAPLING_GOOD;
      MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
              ("NSSCertDBTrustDomain: stapled OCSP response: good"));
      return Success;
    }
    if (stapledOCSPResponseResult == Result::ERROR_OCSP_OLD_RESPONSE ||
        expired) {
      // stapled OCSP response present but expired
      mOCSPStaplingStatus = CertVerifier::OCSP_STAPLING_EXPIRED;
      MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
              ("NSSCertDBTrustDomain: expired stapled OCSP response"));
    } else if (stapledOCSPResponseResult ==
                   Result::ERROR_OCSP_TRY_SERVER_LATER ||
               stapledOCSPResponseResult ==
                   Result::ERROR_OCSP_INVALID_SIGNING_CERT ||
               stapledOCSPResponseResult ==
                   Result::ERROR_OCSP_RESPONSE_FOR_CERT_MISSING) {
      // Stapled OCSP response present but invalid for a small number of reasons
      // CAs/servers commonly get wrong. This will be treated similarly to an
      // expired stapled response.
      mOCSPStaplingStatus = CertVerifier::OCSP_STAPLING_INVALID;
      MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
              ("NSSCertDBTrustDomain: stapled OCSP response: "
               "failure (allowed for compatibility)"));
    } else {
      // stapled OCSP response present but invalid for some reason
      mOCSPStaplingStatus = CertVerifier::OCSP_STAPLING_INVALID;
      MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
              ("NSSCertDBTrustDomain: stapled OCSP response: failure"));
      return stapledOCSPResponseResult;
    }
  } else {
    // no stapled OCSP response
    mOCSPStaplingStatus = CertVerifier::OCSP_STAPLING_NONE;
    MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
            ("NSSCertDBTrustDomain: no stapled OCSP response"));
  }

  Result cachedResponseResult = Success;
  Time cachedResponseValidThrough(Time::uninitialized);
  bool cachedResponsePresent =
      mOCSPCache.Get(certID, mOriginAttributes, cachedResponseResult,
                     cachedResponseValidThrough);
  if (cachedResponsePresent) {
    mozilla::glean::cert_verifier::cert_revocation_mechanisms
        .Get("CachedOCSP"_ns)
        .Add(1);
    if (cachedResponseResult == Success && cachedResponseValidThrough >= time) {
      MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
              ("NSSCertDBTrustDomain: cached OCSP response: good"));
      return Success;
    }
    // If we have a cached revoked response, use it.
    if (cachedResponseResult == Result::ERROR_REVOKED_CERTIFICATE) {
      MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
              ("NSSCertDBTrustDomain: cached OCSP response: revoked"));
      return Result::ERROR_REVOKED_CERTIFICATE;
    }
    // The cached response may indicate an unknown certificate or it may be
    // expired. Don't return with either of these statuses yet - we may be
    // able to fetch a more recent one.
    MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
            ("NSSCertDBTrustDomain: cached OCSP response: error %d",
             static_cast<int>(cachedResponseResult)));
    // When a good cached response has expired, it is more convenient
    // to convert that to an error code and just deal with
    // cachedResponseResult from here on out.
    if (cachedResponseResult == Success && cachedResponseValidThrough < time) {
      cachedResponseResult = Result::ERROR_OCSP_OLD_RESPONSE;
    }
    // We may have a cached indication of server failure. Ignore it if
    // it has expired.
    if (cachedResponseResult != Success &&
        cachedResponseResult != Result::ERROR_OCSP_UNKNOWN_CERT &&
        cachedResponseResult != Result::ERROR_OCSP_OLD_RESPONSE &&
        cachedResponseValidThrough < time) {
      cachedResponseResult = Success;
      cachedResponsePresent = false;
    }
  } else {
    MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
            ("NSSCertDBTrustDomain: no cached OCSP response"));
  }
  // At this point, if and only if cachedErrorResult is Success, there was no
  // cached response.
  MOZ_ASSERT((!cachedResponsePresent && cachedResponseResult == Success) ||
             (cachedResponsePresent && cachedResponseResult != Success));

  // TODO: We still need to handle the fallback for invalid stapled responses.
  // But, if/when we disable OCSP fetching by default, it would be ambiguous
  // whether security.OCSP.enable==0 means "I want the default" or "I really
  // never want you to ever fetch OCSP."
  // Additionally, this doesn't properly handle OCSP-must-staple when OCSP
  // fetching is disabled.
  Duration shortLifetime(mCertShortLifetimeInDays * Time::ONE_DAY_IN_SECONDS);
  if (validityDuration < shortLifetime) {
    mozilla::glean::cert_verifier::cert_revocation_mechanisms
        .Get("ShortValidity"_ns)
        .Add(1);
  }
  if ((mOCSPFetching == RevocationCheckLocalOnly) ||
      (validityDuration < shortLifetime)) {
    // We're not going to be doing any fetching, so if there was a cached
    // "unknown" response, say so.
    if (cachedResponseResult == Result::ERROR_OCSP_UNKNOWN_CERT) {
      return Result::ERROR_OCSP_UNKNOWN_CERT;
    }
    // If we're doing hard-fail, we want to know if we have a cached response
    // that has expired.
    if (mOCSPFetching == RevocationCheckRequired &&
        cachedResponseResult == Result::ERROR_OCSP_OLD_RESPONSE) {
      return Result::ERROR_OCSP_OLD_RESPONSE;
    }

    return Success;
  }

  // There are a few situations where the user's CRLite data may not cover a
  // certificate that chains to our root store, e.g.
  //  1) the user has not yet downloaded CRLite filters, or
  //  2) the user's CRLite filters are out-of-date, or
  //  3) the certificate has been in CT for < 1 MMD interval.
  // If we're configured to enforce CRLite and we're configured to tolerate OCSP
  // soft failures, then it's reasonable to skip the synchronous OCSP request
  // here. In effect, we're choosing to preserve the privacy of the user at the
  // risk of potentially allowing them to navigate to a site that is serving a
  // revoked certificate.
  if (mOCSPFetching == RevocationCheckMayFetch &&
      mCRLiteMode == CRLiteMode::Enforce && mIsBuiltChainRootBuiltInRoot) {
    return Success;
  }

  if (aiaLocation.IsVoid()) {
    if (cachedResponseResult == Result::ERROR_OCSP_UNKNOWN_CERT) {
      return Result::ERROR_OCSP_UNKNOWN_CERT;
    }
    if (cachedResponseResult == Result::ERROR_OCSP_OLD_RESPONSE) {
      return Result::ERROR_OCSP_OLD_RESPONSE;
    }
    if (stapledOCSPResponseResult != Success) {
      return stapledOCSPResponseResult;
    }

    // Nothing to do if we don't have an OCSP responder URI for the cert; just
    // assume it is good. Note that this is the confusing, but intended,
    // interpretation of "strict" revocation checking in the face of a
    // certificate that lacks an OCSP responder URI.
    return Success;
  }

  if (cachedResponseResult == Success ||
      cachedResponseResult == Result::ERROR_OCSP_UNKNOWN_CERT ||
      cachedResponseResult == Result::ERROR_OCSP_OLD_RESPONSE) {
    // Only send a request to, and process a response from, the server if we
    // didn't have a cached indication of failure.  Also, don't keep requesting
    // responses from a failing server.
    return SynchronousCheckRevocationWithServer(
        certID, aiaLocation, time, maxOCSPLifetimeInDays, cachedResponseResult,
        stapledOCSPResponseResult);
  }

  return HandleOCSPFailure(cachedResponseResult, stapledOCSPResponseResult,
                           cachedResponseResult);
}

Result NSSCertDBTrustDomain::SynchronousCheckRevocationWithServer(
    const CertID& certID, const nsCString& aiaLocation, Time time,
    uint16_t maxOCSPLifetimeInDays, const Result cachedResponseResult,
    const Result stapledOCSPResponseResult) {
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }

  uint8_t ocspRequestBytes[OCSP_REQUEST_MAX_LENGTH];
  size_t ocspRequestLength;
  Result rv = CreateEncodedOCSPRequest(*this, certID, ocspRequestBytes,
                                       ocspRequestLength);
  if (rv != Success) {
    return rv;
  }

  Vector<uint8_t> ocspResponse;
  Input response;
  mOCSPFetchStatus = OCSPFetchStatus::Fetched;
  rv = DoOCSPRequest(aiaLocation, mOriginAttributes, ocspRequestBytes,
                     ocspRequestLength, GetOCSPTimeout(), ocspResponse);
  mozilla::glean::cert_verifier::cert_revocation_mechanisms.Get("OCSP"_ns).Add(
      1);
  if (rv == Success &&
      response.Init(ocspResponse.begin(), ocspResponse.length()) != Success) {
    rv = Result::ERROR_OCSP_MALFORMED_RESPONSE;  // too big
  }

  if (rv != Success) {
    Time timeout(time);
    if (timeout.AddSeconds(ServerFailureDelaySeconds) != Success) {
      return Result::FATAL_ERROR_LIBRARY_FAILURE;  // integer overflow
    }

    Result cacheRV =
        mOCSPCache.Put(certID, mOriginAttributes, rv, time, timeout);
    if (cacheRV != Success) {
      return cacheRV;
    }

    return HandleOCSPFailure(cachedResponseResult, stapledOCSPResponseResult,
                             rv);
  }

  // If the response from the network has expired but indicates a revoked
  // or unknown certificate, PR_GetError() will return the appropriate error.
  // We actually ignore expired here.
  bool expired;
  rv = VerifyAndMaybeCacheEncodedOCSPResponse(certID, time,
                                              maxOCSPLifetimeInDays, response,
                                              ResponseIsFromNetwork, expired);
  if (rv == Success || mOCSPFetching == RevocationCheckRequired) {
    MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
            ("NSSCertDBTrustDomain: returning after "
             "VerifyEncodedOCSPResponse"));
    return rv;
  }

  if (rv == Result::ERROR_OCSP_UNKNOWN_CERT ||
      rv == Result::ERROR_REVOKED_CERTIFICATE) {
    return rv;
  }

  if (stapledOCSPResponseResult != Success) {
    MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
            ("NSSCertDBTrustDomain: returning SECFailure from expired/invalid "
             "stapled response after OCSP request verification failure"));
    return stapledOCSPResponseResult;
  }

  return Success;  // Soft fail -> success :(
}

Result NSSCertDBTrustDomain::HandleOCSPFailure(
    const Result cachedResponseResult, const Result stapledOCSPResponseResult,
    const Result error) {
  if (mOCSPFetching == RevocationCheckRequired) {
    MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
            ("NSSCertDBTrustDomain: returning SECFailure after OCSP request "
             "failure"));
    return error;
  }

  if (cachedResponseResult == Result::ERROR_OCSP_UNKNOWN_CERT) {
    MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
            ("NSSCertDBTrustDomain: returning SECFailure from cached response "
             "after OCSP request failure"));
    return cachedResponseResult;
  }

  if (stapledOCSPResponseResult != Success) {
    MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
            ("NSSCertDBTrustDomain: returning SECFailure from expired/invalid "
             "stapled response after OCSP request failure"));
    return stapledOCSPResponseResult;
  }

  MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
          ("NSSCertDBTrustDomain: returning SECSuccess after OCSP request "
           "failure"));

  return Success;  // Soft fail -> success :(
}

Result NSSCertDBTrustDomain::VerifyAndMaybeCacheEncodedOCSPResponse(
    const CertID& certID, Time time, uint16_t maxLifetimeInDays,
    Input encodedResponse, EncodedResponseSource responseSource,
    /*out*/ bool& expired) {
  Time thisUpdate(Time::uninitialized);
  Time validThrough(Time::uninitialized);

  Result rv = VerifyEncodedOCSPResponse(*this, certID, time, maxLifetimeInDays,
                                        encodedResponse, expired, &thisUpdate,
                                        &validThrough);
  // If a response was stapled and expired, we don't want to cache it. Return
  // early to simplify the logic here.
  if (responseSource == ResponseWasStapled && expired) {
    MOZ_ASSERT(rv != Success);
    return rv;
  }
  // validThrough is only trustworthy if the response successfully verifies
  // or it indicates a revoked or unknown certificate.
  // If this isn't the case, store an indication of failure (to prevent
  // repeatedly requesting a response from a failing server).
  if (rv != Success && rv != Result::ERROR_REVOKED_CERTIFICATE &&
      rv != Result::ERROR_OCSP_UNKNOWN_CERT) {
    validThrough = time;
    if (validThrough.AddSeconds(ServerFailureDelaySeconds) != Success) {
      return Result::FATAL_ERROR_LIBRARY_FAILURE;  // integer overflow
    }
  }
  if (responseSource == ResponseIsFromNetwork || rv == Success ||
      rv == Result::ERROR_REVOKED_CERTIFICATE ||
      rv == Result::ERROR_OCSP_UNKNOWN_CERT) {
    MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
            ("NSSCertDBTrustDomain: caching OCSP response"));
    Result putRV =
        mOCSPCache.Put(certID, mOriginAttributes, rv, thisUpdate, validThrough);
    if (putRV != Success) {
      return putRV;
    }
  }

  return rv;
}

nsresult IsDistrustedCertificateChain(
    const nsTArray<nsTArray<uint8_t>>& certArray,
    const SECTrustType certDBTrustType, bool& isDistrusted,
    Maybe<mozilla::pkix::Time>& distrustAfterTimeOut) {
  if (certArray.Length() == 0) {
    return NS_ERROR_FAILURE;
  }

  // Set the default result to be distrusted.
  isDistrusted = true;

  CK_ATTRIBUTE_TYPE attrType;
  switch (certDBTrustType) {
    case trustSSL:
      attrType = CKA_NSS_SERVER_DISTRUST_AFTER;
      break;
    case trustEmail:
      attrType = CKA_NSS_EMAIL_DISTRUST_AFTER;
      break;
    default:
      // There is no distrust to set if the certDBTrustType is not SSL or Email.
      isDistrusted = false;
      return NS_OK;
  }

  Input endEntityDER;
  mozilla::pkix::Result rv = endEntityDER.Init(
      certArray.ElementAt(0).Elements(), certArray.ElementAt(0).Length());
  if (rv != Success) {
    return NS_ERROR_FAILURE;
  }

  BackCert endEntityBackCert(endEntityDER, EndEntityOrCA::MustBeEndEntity,
                             nullptr);
  rv = endEntityBackCert.Init();
  if (rv != Success) {
    return NS_ERROR_FAILURE;
  }

  Time endEntityNotBefore(Time::uninitialized);
  rv = ParseValidity(endEntityBackCert.GetValidity(), &endEntityNotBefore,
                     nullptr);
  if (rv != Success) {
    return NS_ERROR_FAILURE;
  }

  Input rootDER;
  rv = rootDER.Init(certArray.LastElement().Elements(),
                    certArray.LastElement().Length());
  if (rv != Success) {
    return NS_ERROR_FAILURE;
  }
  SECItem rootDERItem(UnsafeMapInputToSECItem(rootDER));

  PRBool distrusted;
  PRTime distrustAfter;  // time since epoch in microseconds
  bool foundDistrust = false;

  // This strategy for searching for the builtins module is borrowed
  // from CertVerifier::IsCertBuiltInRoot. See the comment on that
  // function for more information.
  AutoSECMODListReadLock lock;
  for (SECMODModuleList* list = SECMOD_GetDefaultModuleList();
       list && !foundDistrust; list = list->next) {
    for (int i = 0; i < list->module->slotCount; i++) {
      PK11SlotInfo* slot = list->module->slots[i];
      if (!PK11_IsPresent(slot) || !PK11_HasRootCerts(slot)) {
        continue;
      }
      CK_OBJECT_HANDLE handle =
          PK11_FindEncodedCertInSlot(slot, &rootDERItem, nullptr);
      if (handle == CK_INVALID_HANDLE) {
        continue;
      }
      // Distrust attributes are only set on builtin roots, so ensure this
      // certificate has the CKA_NSS_MOZILLA_CA_POLICY attribute.
      if (!PK11_HasAttributeSet(slot, handle, CKA_NSS_MOZILLA_CA_POLICY,
                                false)) {
        continue;
      }
      SECStatus srv = PK11_ReadDistrustAfterAttribute(
          slot, handle, attrType, &distrusted, &distrustAfter);
      if (srv == SECSuccess) {
        foundDistrust = true;
      }
    }
  }

  if (!foundDistrust || distrusted == PR_FALSE) {
    isDistrusted = false;
    return NS_OK;
  }

  Time distrustAfterTime =
      mozilla::pkix::TimeFromEpochInSeconds(distrustAfter / PR_USEC_PER_SEC);
  distrustAfterTimeOut.emplace(distrustAfterTime);
  if (endEntityNotBefore <= distrustAfterTime) {
    isDistrusted = false;
  }

  return NS_OK;
}

Result NSSCertDBTrustDomain::IsChainValid(const DERArray& reversedDERArray,
                                          Time time,
                                          const CertPolicyId& requiredPolicy) {
  MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
          ("NSSCertDBTrustDomain: IsChainValid"));

  size_t numCerts = reversedDERArray.GetLength();
  if (numCerts < 1) {
    return Result::FATAL_ERROR_LIBRARY_FAILURE;
  }
  nsTArray<nsTArray<uint8_t>> certArray;
  for (size_t i = numCerts; i > 0; --i) {
    const Input* derInput = reversedDERArray.GetDER(i - 1);
    certArray.EmplaceBack(derInput->UnsafeGetData(), derInput->GetLength());
  }

  const nsTArray<uint8_t>& rootBytes = certArray.LastElement();
  Input rootInput;
  Result rv = rootInput.Init(rootBytes.Elements(), rootBytes.Length());
  if (rv != Success) {
    return rv;
  }
  rv = IsCertBuiltInRoot(rootInput, mIsBuiltChainRootBuiltInRoot);
  if (rv != Result::Success) {
    return rv;
  }
  nsresult nsrv;
  // If mHostname isn't set, we're not verifying in the context of a TLS
  // handshake, so don't verify key pinning in those cases.
  if (mHostname) {
    nsTArray<Span<const uint8_t>> derCertSpanList;
    for (const auto& certDER : certArray) {
      derCertSpanList.EmplaceBack(certDER.Elements(), certDER.Length());
    }

    bool chainHasValidPins;
    nsrv = PublicKeyPinningService::ChainHasValidPins(
        derCertSpanList, mHostname, time, mIsBuiltChainRootBuiltInRoot,
        chainHasValidPins, mPinningTelemetryInfo);
    if (NS_FAILED(nsrv)) {
      return Result::FATAL_ERROR_LIBRARY_FAILURE;
    }
    if (!chainHasValidPins) {
      return Result::ERROR_KEY_PINNING_FAILURE;
    }
  }

  // Check that the childs' certificate NotBefore date is anterior to
  // the NotAfter value of the parent when the root is a builtin.
  if (mIsBuiltChainRootBuiltInRoot) {
    bool isDistrusted;
    nsrv = IsDistrustedCertificateChain(certArray, mCertDBTrustType,
                                        isDistrusted, mDistrustAfterTime);
    if (NS_FAILED(nsrv)) {
      return Result::FATAL_ERROR_LIBRARY_FAILURE;
    }
    if (isDistrusted) {
      // Check if this root is also a third-party root. If so, distrust after
      // doesn't apply to it.
      bool isThirdPartyRoot = false;
      for (const auto& thirdPartyRoot : mThirdPartyRootInputs) {
        if (InputsAreEqual(rootInput, thirdPartyRoot)) {
          isThirdPartyRoot = true;
          break;
        }
      }
      if (!isThirdPartyRoot) {
        MOZ_LOG(
            gCertVerifierLog, LogLevel::Debug,
            ("certificate has notBefore after distrust after value for root"));
        return Result::ERROR_ISSUER_NO_LONGER_TRUSTED;
      }
      MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
              ("ignoring built-in distrust after for third-party root"));
    }
  }

  mBuiltChain = std::move(certArray);

  return Success;
}

Result NSSCertDBTrustDomain::CheckSignatureDigestAlgorithm(
    DigestAlgorithm aAlg, EndEntityOrCA /*endEntityOrCA*/, Time /*notBefore*/) {
  switch (aAlg) {
    case DigestAlgorithm::sha256:  // fall through
    case DigestAlgorithm::sha384:  // fall through
    case DigestAlgorithm::sha512:
      return Success;
    case DigestAlgorithm::sha1:
      return Result::ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED;
  }
  return Result::FATAL_ERROR_LIBRARY_FAILURE;
}

Result NSSCertDBTrustDomain::CheckRSAPublicKeyModulusSizeInBits(
    EndEntityOrCA /*endEntityOrCA*/, unsigned int modulusSizeInBits) {
  if (modulusSizeInBits < mMinRSABits) {
    return Result::ERROR_INADEQUATE_KEY_SIZE;
  }
  return Success;
}

Result NSSCertDBTrustDomain::VerifyRSAPKCS1SignedData(
    Input data, DigestAlgorithm digestAlgorithm, Input signature,
    Input subjectPublicKeyInfo) {
  return VerifySignedDataWithCache(
      der::PublicKeyAlgorithm::RSA_PKCS1,
      mozilla::glean::cert_signature_cache::total,
      mozilla::glean::cert_signature_cache::hits, data, digestAlgorithm,
      signature, subjectPublicKeyInfo, mSignatureCache, mPinArg);
}

Result NSSCertDBTrustDomain::VerifyRSAPSSSignedData(
    Input data, DigestAlgorithm digestAlgorithm, Input signature,
    Input subjectPublicKeyInfo) {
  return VerifySignedDataWithCache(
      der::PublicKeyAlgorithm::RSA_PSS,
      mozilla::glean::cert_signature_cache::total,
      mozilla::glean::cert_signature_cache::hits, data, digestAlgorithm,
      signature, subjectPublicKeyInfo, mSignatureCache, mPinArg);
}

Result NSSCertDBTrustDomain::CheckECDSACurveIsAcceptable(
    EndEntityOrCA /*endEntityOrCA*/, NamedCurve curve) {
  switch (curve) {
    case NamedCurve::secp256r1:  // fall through
    case NamedCurve::secp384r1:  // fall through
    case NamedCurve::secp521r1:
      return Success;
  }

  return Result::ERROR_UNSUPPORTED_ELLIPTIC_CURVE;
}

Result NSSCertDBTrustDomain::VerifyECDSASignedData(
    Input data, DigestAlgorithm digestAlgorithm, Input signature,
    Input subjectPublicKeyInfo) {
  return VerifySignedDataWithCache(
      der::PublicKeyAlgorithm::ECDSA,
      mozilla::glean::cert_signature_cache::total,
      mozilla::glean::cert_signature_cache::hits, data, digestAlgorithm,
      signature, subjectPublicKeyInfo, mSignatureCache, mPinArg);
}

Result NSSCertDBTrustDomain::VerifyMLDSASignedData(Input data, Input signature,
    Input subjectPublicKeyInfo) {
  return VerifySignedDataWithCache(
      der::PublicKeyAlgorithm::ML_DSA,
      mozilla::glean::cert_signature_cache::total,
      mozilla::glean::cert_signature_cache::hits, data,
      DigestAlgorithm::none, signature, subjectPublicKeyInfo,
      mSignatureCache, mPinArg);
}

Result NSSCertDBTrustDomain::CheckValidityIsAcceptable(
    Time notBefore, Time notAfter, EndEntityOrCA endEntityOrCA,
    KeyPurposeId keyPurpose) {
  return Success;
}

void NSSCertDBTrustDomain::ResetAccumulatedState() {
  mOCSPStaplingStatus = CertVerifier::OCSP_STAPLING_NEVER_CHECKED;
  mSCTListFromOCSPStapling = nullptr;
  mSCTListFromCertificate = nullptr;
  mIssuerSources.clear();
  ResetCandidateBuiltChainState();
}

void NSSCertDBTrustDomain::ResetCandidateBuiltChainState() {
  mIsBuiltChainRootBuiltInRoot = false;
  mDistrustAfterTime.reset();
}

static Input SECItemToInput(const UniqueSECItem& item) {
  Input result;
  if (item) {
    MOZ_ASSERT(item->type == siBuffer);
    Result rv = result.Init(item->data, item->len);
    // As used here, |item| originally comes from an Input,
    // so there should be no issues converting it back.
    MOZ_ASSERT(rv == Success);
    (void)rv;  // suppresses warnings in release builds
  }
  return result;
}

Input NSSCertDBTrustDomain::GetSCTListFromCertificate() const {
  return SECItemToInput(mSCTListFromCertificate);
}

Input NSSCertDBTrustDomain::GetSCTListFromOCSPStapling() const {
  return SECItemToInput(mSCTListFromOCSPStapling);
}

Maybe<CTVerifyResult>& NSSCertDBTrustDomain::GetCachedCTVerifyResult() {
  return mCTVerifyResult;
}

bool NSSCertDBTrustDomain::GetIsBuiltChainRootBuiltInRoot() const {
  return mIsBuiltChainRootBuiltInRoot;
}

void NSSCertDBTrustDomain::NoteAuxiliaryExtension(AuxiliaryExtension extension,
                                                  Input extensionData) {
  UniqueSECItem* out = nullptr;
  switch (extension) {
    case AuxiliaryExtension::EmbeddedSCTList:
      out = &mSCTListFromCertificate;
      break;
    case AuxiliaryExtension::SCTListFromOCSPResponse:
      out = &mSCTListFromOCSPStapling;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unhandled AuxiliaryExtension");
  }
  if (out) {
    SECItem extensionDataItem = UnsafeMapInputToSECItem(extensionData);
    out->reset(SECITEM_DupItem(&extensionDataItem));
  }
}

SECStatus InitializeNSS(const nsACString& dir, NSSDBConfig nssDbConfig,
                        PKCS11DBConfig pkcs11DbConfig) {
  // In the parent process, this must be called on the main thread. In the
  // utility process, this will be called on a background thread.
  MOZ_ASSERT(NS_IsMainThread() || XRE_IsUtilityProcess());
  // If this is the utility process, the pref to enable the utility process
  // better be true (or this is a gtest).
  MOZ_ASSERT(
      !XRE_IsUtilityProcess() ||
      StaticPrefs::security_utility_pkcs11_module_process_enabled_AtStartup() ||
      PR_GetEnv("MOZ_RUN_GTEST"));

  // The NSS_INIT_NOROOTINIT flag turns off the loading of the root certs
  // module by NSS_Initialize because we will load it in LoadLoadableRoots
  // later.  It also allows us to work around a bug in the system NSS in
  // Ubuntu 8.04, which loads any nonexistent "<configdir>/libnssckbi.so" as
  // "/usr/lib/nss/libnssckbi.so".
  uint32_t flags = NSS_INIT_NOROOTINIT | NSS_INIT_OPTIMIZESPACE;
  if (nssDbConfig == NSSDBConfig::ReadOnly) {
    flags |= NSS_INIT_READONLY;
  }
  // If we've been configured to not load any modules or if this is the parent
  // process and the PKCS#11 utility process is enabled, don't load the NSS
  // PKCS#11 module DB.
  bool isParentProcessButLoadingModulesInUtilityProcess =
      XRE_IsParentProcess() &&
      StaticPrefs::security_utility_pkcs11_module_process_enabled_AtStartup();
  if (pkcs11DbConfig == PKCS11DBConfig::DoNotLoadModules ||
      isParentProcessButLoadingModulesInUtilityProcess) {
    flags |= NSS_INIT_NOMODDB;
  }
  // If this is the utility process, only load the NSS PKCS#11 module DB, not
  // the key/certificate DB.
  if (XRE_IsUtilityProcess()) {
    flags |= NSS_INIT_NOCERTDB;
  }
  nsAutoCString dbTypeAndDirectory("sql:");
  dbTypeAndDirectory.Append(dir);
  MOZ_LOG(gCertVerifierLog, LogLevel::Debug,
          ("InitializeNSS(%s, %d, %d)", dbTypeAndDirectory.get(),
           (int)nssDbConfig, (int)pkcs11DbConfig));
  SECStatus srv =
      NSS_Initialize(dbTypeAndDirectory.get(), "", "", SECMOD_DB, flags);
  if (srv != SECSuccess) {
    return srv;
  }

  // If the key DB doesn't have a password set, PK11_NeedUserInit will return
  // true. For the SQL DB, we need to set a password or we won't be able to
  // import any certificates or change trust settings. This only applies to the
  // parent process and when we're in read/write mode.
  if (nssDbConfig == NSSDBConfig::ReadWrite && XRE_IsParentProcess()) {
    UniquePK11SlotInfo slot(PK11_GetInternalKeySlot());
    if (!slot) {
      return SECFailure;
    }
    if (PK11_NeedUserInit(slot.get())) {
      srv = PK11_InitPin(slot.get(), nullptr, nullptr);
      MOZ_ALWAYS_TRUE(srv == SECSuccess);
    }
  }

  // Collect third party PKCS#11 module telemetry if this is the parent process
  // and the utility process is not enabled or if this is the utility process.
  bool isParentProcessAndNotLoadingModulesInUtilityProcess =
      XRE_IsParentProcess() &&
      !StaticPrefs::security_utility_pkcs11_module_process_enabled_AtStartup();
  if (isParentProcessAndNotLoadingModulesInUtilityProcess ||
      XRE_IsUtilityProcess()) {
    CollectThirdPartyPKCS11ModuleTelemetry(/*aIsInitialization=*/true);
  }

  return SECSuccess;
}

void DisableMD5() {
  NSS_SetAlgorithmPolicy(
      SEC_OID_MD5, 0,
      NSS_USE_ALG_IN_CERT_SIGNATURE | NSS_USE_ALG_IN_CMS_SIGNATURE);
  NSS_SetAlgorithmPolicy(
      SEC_OID_PKCS1_MD5_WITH_RSA_ENCRYPTION, 0,
      NSS_USE_ALG_IN_CERT_SIGNATURE | NSS_USE_ALG_IN_CMS_SIGNATURE);
  NSS_SetAlgorithmPolicy(
      SEC_OID_PKCS5_PBE_WITH_MD5_AND_DES_CBC, 0,
      NSS_USE_ALG_IN_CERT_SIGNATURE | NSS_USE_ALG_IN_CMS_SIGNATURE);
}

// Load a given PKCS#11 module located in the given directory. It will be named
// the given module name. Optionally pass some string parameters to it via
// 'params'. This argument will be provided to C_Initialize when called on the
// module.
// |libraryName| and |dir| are encoded in UTF-8.
bool LoadUserModuleAt(const char* moduleName, const char* libraryName,
                      const nsCString& dir, /* optional */ const char* params) {
  // If a module exists with the same name, make a best effort attempt to delete
  // it. Note that it isn't possible to delete the internal module, so checking
  // the return value would be detrimental in that case.
  int unusedModType;
  (void)SECMOD_DeleteModule(moduleName, &unusedModType);

  nsAutoCString fullLibraryPath;
  if (!dir.IsEmpty()) {
    fullLibraryPath.Assign(dir);
    fullLibraryPath.AppendLiteral(FILE_PATH_SEPARATOR);
  }
  fullLibraryPath.Append(MOZ_DLL_PREFIX);
  fullLibraryPath.Append(libraryName);
  fullLibraryPath.Append(MOZ_DLL_SUFFIX);
  // Escape the \ and " characters.
  fullLibraryPath.ReplaceSubstring("\\", "\\\\");
  fullLibraryPath.ReplaceSubstring("\"", "\\\"");

  nsAutoCString pkcs11ModuleSpec("name=\"");
  pkcs11ModuleSpec.Append(moduleName);
  pkcs11ModuleSpec.AppendLiteral("\" library=\"");
  pkcs11ModuleSpec.Append(fullLibraryPath);
  pkcs11ModuleSpec.AppendLiteral("\"");
  if (params) {
    pkcs11ModuleSpec.AppendLiteral("\" parameters=\"");
    pkcs11ModuleSpec.Append(params);
    pkcs11ModuleSpec.AppendLiteral("\"");
  }

  UniqueSECMODModule userModule(SECMOD_LoadUserModule(
      const_cast<char*>(pkcs11ModuleSpec.get()), nullptr, false));
  if (!userModule) {
    return false;
  }

  if (!userModule->loaded) {
    return false;
  }

  return true;
}

bool LoadUserModuleFromXul(const char* moduleName,
                           CK_C_GetFunctionList fentry) {
  // If a module exists with the same name, make a best effort attempt to delete
  // it. Note that it isn't possible to delete the internal module, so checking
  // the return value would be detrimental in that case.
  int unusedModType;
  (void)SECMOD_DeleteModule(moduleName, &unusedModType);

  UniqueSECMODModule userModule(
      SECMOD_LoadUserModuleWithFunction(moduleName, fentry));
  if (!userModule) {
    return false;
  }

  if (!userModule->loaded) {
    return false;
  }

  return true;
}

extern "C" {
// Extern function to call ipcclientcerts module C_GetFunctionList.
// NSS calls it to obtain the list of functions comprising this module.
// ppFunctionList must be a valid pointer.
CK_RV IPCCC_GetFunctionList(CK_FUNCTION_LIST_PTR_PTR ppFunctionList);
}  // extern "C"

bool LoadIPCClientCertsModule() {
  // The IPC client certs module needs to be able to call back into gecko to be
  // able to communicate with the parent process over IPC. This is achieved by
  // calling the external to Rust module functions DoSign and DoFindObjects.

  if (!LoadUserModuleFromXul(kIPCClientCertsModuleName.get(),
                             IPCCC_GetFunctionList)) {
    return false;
  }
  RunOnShutdown(
      []() {
        UniqueSECMODModule ipcClientCertsModule(
            SECMOD_FindModule(kIPCClientCertsModuleName.get()));
        if (ipcClientCertsModule) {
          SECMOD_UnloadUserModule(ipcClientCertsModule.get());
        }
      },
      ShutdownPhase::XPCOMWillShutdown);
  return true;
}

extern "C" {
// Extern function to call osclientcerts module C_GetFunctionList.
// NSS calls it to obtain the list of functions comprising this module.
// ppFunctionList must be a valid pointer.
CK_RV OSClientCerts_C_GetFunctionList(CK_FUNCTION_LIST_PTR_PTR ppFunctionList);
}  // extern "C"

bool LoadOSClientCertsModule() {
// Corresponds to Rust cfg(any(
//  target_os = "macos",
//  target_os = "ios",
//  all(target_os = "windows", not(target_arch = "aarch64")),
//  target_os = "android"))]
#if defined(__APPLE__) || (defined WIN32 && !defined(__aarch64__)) || \
    defined(MOZ_WIDGET_ANDROID)
  return LoadUserModuleFromXul(kOSClientCertsModuleName.get(),
                               OSClientCerts_C_GetFunctionList);
#else
  return false;
#endif
}

bool LoadLoadableRoots(const nsCString& dir) {
  int unusedModType;
  (void)SECMOD_DeleteModule("Root Certs", &unusedModType);
  return LoadUserModuleAt(kRootModuleName.get(), "nssckbi", dir, nullptr);
}

extern "C" {
// Extern function to call trust-anchors module C_GetFunctionList.
// NSS calls it to obtain the list of functions comprising this module.
// ppFunctionList must be a valid pointer.
CK_RV TRUST_ANCHORS_GetFunctionList(CK_FUNCTION_LIST_PTR_PTR ppFunctionList);
}  // extern "C"

bool LoadLoadableRootsFromXul() {
  // Some NSS command-line utilities will load a roots module under the name
  // "Root Certs" if there happens to be a `MOZ_DLL_PREFIX "nssckbi"
  // MOZ_DLL_SUFFIX` file in the directory being operated on. In some cases this
  // can cause us to fail to load our roots module. In these cases, deleting the
  // "Root Certs" module allows us to load the correct one. See bug 1406396.
  int unusedModType;
  (void)SECMOD_DeleteModule("Root Certs", &unusedModType);

  if (!LoadUserModuleFromXul(kRootModuleName.get(),
                             TRUST_ANCHORS_GetFunctionList)) {
    return false;
  }
  return true;
}

nsresult DefaultServerNicknameForCert(const CERTCertificate* cert,
                                      /*out*/ nsCString& nickname) {
  MOZ_ASSERT(cert);
  NS_ENSURE_ARG_POINTER(cert);

  UniquePORTString baseName(CERT_GetCommonName(&cert->subject));
  if (!baseName) {
    baseName = UniquePORTString(CERT_GetOrgUnitName(&cert->subject));
  }
  if (!baseName) {
    baseName = UniquePORTString(CERT_GetOrgName(&cert->subject));
  }
  if (!baseName) {
    baseName = UniquePORTString(CERT_GetLocalityName(&cert->subject));
  }
  if (!baseName) {
    baseName = UniquePORTString(CERT_GetStateName(&cert->subject));
  }
  if (!baseName) {
    baseName = UniquePORTString(CERT_GetCountryName(&cert->subject));
  }
  if (!baseName) {
    return NS_ERROR_FAILURE;
  }

  // This function is only used in contexts where a failure to find a suitable
  // nickname does not block the overall task from succeeding.
  // As such, we use an arbitrary limit to prevent this nickname searching
  // process from taking forever.
  static const uint32_t ARBITRARY_LIMIT = 500;
  for (uint32_t count = 1; count < ARBITRARY_LIMIT; count++) {
    nickname = baseName.get();
    if (count != 1) {
      nickname.AppendPrintf(" #%u", count);
    }
    if (nickname.IsEmpty()) {
      return NS_ERROR_FAILURE;
    }

    bool conflict = SEC_CertNicknameConflict(nickname.get(), &cert->derSubject,
                                             cert->dbhandle);
    if (!conflict) {
      return NS_OK;
    }
  }

  return NS_ERROR_FAILURE;
}

Result BuildRevocationCheckArrays(Input certDER, EndEntityOrCA endEntityOrCA,
                                  /*out*/ nsTArray<uint8_t>& issuerBytes,
                                  /*out*/ nsTArray<uint8_t>& serialBytes,
                                  /*out*/ nsTArray<uint8_t>& subjectBytes,
                                  /*out*/ nsTArray<uint8_t>& pubKeyBytes) {
  BackCert cert(certDER, endEntityOrCA, nullptr);
  Result rv = cert.Init();
  if (rv != Success) {
    return rv;
  }
  issuerBytes.Clear();
  Input issuer(cert.GetIssuer());
  issuerBytes.AppendElements(issuer.UnsafeGetData(), issuer.GetLength());
  serialBytes.Clear();
  Input serial(cert.GetSerialNumber());
  serialBytes.AppendElements(serial.UnsafeGetData(), serial.GetLength());
  subjectBytes.Clear();
  Input subject(cert.GetSubject());
  subjectBytes.AppendElements(subject.UnsafeGetData(), subject.GetLength());
  pubKeyBytes.Clear();
  Input pubKey(cert.GetSubjectPublicKeyInfo());
  pubKeyBytes.AppendElements(pubKey.UnsafeGetData(), pubKey.GetLength());

  return Success;
}

}  // namespace psm
}  // namespace mozilla
