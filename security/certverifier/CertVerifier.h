/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CertVerifier_h
#define CertVerifier_h

#include "CTPolicyEnforcer.h"
#include "CTVerifyResult.h"
#include "EnterpriseRoots.h"
#include "OCSPCache.h"
#include "RootCertificateTelemetryUtils.h"
#include "ScopedNSSTypes.h"
#include "mozilla/EnumSet.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/glean/bindings/MetricTypes.h"
#include "mozpkix/pkixder.h"
#include "mozpkix/pkixtypes.h"
#include "nsString.h"
#include "signature_cache_ffi.h"
#include "sslt.h"

#if defined(_MSC_VER)
#  pragma warning(push)
// Silence "RootingAPI.h(718): warning C4324: 'js::DispatchWrapper<T>':
// structure was padded due to alignment specifier with [ T=void * ]"
#  pragma warning(disable : 4324)
#endif /* defined(_MSC_VER) */
#include "mozilla/BasePrincipal.h"
#if defined(_MSC_VER)
#  pragma warning(pop) /* popping the pragma in this file */
#endif                 /* defined(_MSC_VER) */

namespace mozilla {
namespace ct {

// Including the headers of the classes below would bring along all of their
// dependent headers and force us to export them in moz.build.
// Just forward-declare the classes here instead.
class MultiLogCTVerifier;

}  // namespace ct
}  // namespace mozilla

namespace mozilla {
namespace psm {

typedef mozilla::pkix::Result Result;

enum class EVStatus : uint8_t {
  NotEV = 0,
  EV = 1,
};

// These values correspond to the CERT_CHAIN_KEY_SIZE_STATUS telemetry.
enum class KeySizeStatus {
  NeverChecked = 0,
  LargeMinimumSucceeded = 1,
  CompatibilityRisk = 2,
  AlreadyBad = 3,
};

enum class CRLiteMode {
  Disabled = 0,
  TelemetryOnly = 1,
  Enforce = 2,
};

enum class VerifyUsage {
  TLSServer = 1,
  TLSServerCA = 2,
  TLSClient = 3,
  TLSClientCA = 4,
  EmailSigner = 5,
  EmailRecipient = 6,
  EmailCA = 7,
};

// Describes the source of the associated issuer.
enum class IssuerSource {
  TLSHandshake,            // included by the peer in the TLS handshake
  PreloadedIntermediates,  // a preloaded intermediate (via remote settings)
  ThirdPartyCertificates,  // a third-party certificate gleaned from the OS
  NSSCertDB,  // a certificate found in the profile's NSS certificate DB
  BuiltInRootsModule,  // a root from the built-in roots module
  PKCS11Module,        // a certificate found in a third-party PKCS#11 module
};

using IssuerSources = EnumSet<IssuerSource>;

class PinningTelemetryInfo {
 public:
  PinningTelemetryInfo()
      : certPinningResultBucket(0), rootBucket(ROOT_CERTIFICATE_UNKNOWN) {
    Reset();
  }

  // Should we accumulate pinning telemetry for the result?
  bool accumulateResult;
  bool isMoz;
  bool testMode;
  int32_t certPinningResultBucket;
  // Should we accumulate telemetry for the root?
  bool accumulateForRoot;
  int32_t rootBucket;

  void Reset() {
    accumulateForRoot = false;
    accumulateResult = false;
    isMoz = false;
    testMode = false;
  }
};

class CertificateTransparencyInfo {
 public:
  CertificateTransparencyInfo() : enabled(false), policyCompliance(Nothing()) {
    Reset();
  }

  // Was CT enabled?
  bool enabled;
  // Verification result of the processed SCTs.
  mozilla::ct::CTVerifyResult verifyResult;
  // Connection compliance to the CT Policy.
  Maybe<mozilla::ct::CTPolicyCompliance> policyCompliance;

  void Reset();
};

class DelegatedCredentialInfo {
 public:
  DelegatedCredentialInfo() : scheme(ssl_sig_none), authKeyBits(0) {}
  DelegatedCredentialInfo(SSLSignatureScheme scheme, uint32_t authKeyBits)
      : scheme(scheme), authKeyBits(authKeyBits) {}

  // The signature scheme to be used in CertVerify. This tells us
  // whether to interpret |authKeyBits| in an RSA or ECDSA context.
  SSLSignatureScheme scheme;

  // The size of the key, in bits.
  uint32_t authKeyBits;
};

class SkipInvalidSANsForNonBuiltInRootsPolicy
    : public pkix::NameMatchingPolicy {
 public:
  explicit SkipInvalidSANsForNonBuiltInRootsPolicy(bool rootIsBuiltIn)
      : mRootIsBuiltIn(rootIsBuiltIn) {}

  virtual pkix::Result FallBackToCommonName(
      pkix::Time,
      /*out*/ pkix::FallBackToSearchWithinSubject& fallBackToCommonName)
      override {
    fallBackToCommonName = pkix::FallBackToSearchWithinSubject::No;
    return pkix::Success;
  }

  virtual pkix::HandleInvalidSubjectAlternativeNamesBy
  HandleInvalidSubjectAlternativeNames() override {
    return mRootIsBuiltIn
               ? pkix::HandleInvalidSubjectAlternativeNamesBy::Halting
               : pkix::HandleInvalidSubjectAlternativeNamesBy::Skipping;
  }

 private:
  bool mRootIsBuiltIn;
};

class NSSCertDBTrustDomain;

class CertVerifier {
 public:
  typedef unsigned int Flags;
  // XXX: FLAG_LOCAL_ONLY is ignored in the classic verification case
  static const Flags FLAG_LOCAL_ONLY;
  // Don't perform fallback DV validation on EV validation failure.
  static const Flags FLAG_MUST_BE_EV;
  // TLS feature request_status should be ignored
  static const Flags FLAG_TLS_IGNORE_STATUS_REQUEST;

  // These values correspond to the SSL_OCSP_STAPLING telemetry.
  enum OCSPStaplingStatus {
    OCSP_STAPLING_NEVER_CHECKED = 0,
    OCSP_STAPLING_GOOD = 1,
    OCSP_STAPLING_NONE = 2,
    OCSP_STAPLING_EXPIRED = 3,
    OCSP_STAPLING_INVALID = 4,
  };

  // *evOidPolicy == SEC_OID_UNKNOWN means the cert is NOT EV
  // Only one usage per verification is supported.
  mozilla::pkix::Result VerifyCert(
      const nsTArray<uint8_t>& certBytes, VerifyUsage usage,
      mozilla::pkix::Time time, void* pinArg, const char* hostname,
      /*out*/ nsTArray<nsTArray<uint8_t>>& builtChain, Flags flags = 0,
      /*optional in*/
      const Maybe<nsTArray<nsTArray<uint8_t>>>& extraCertificates = Nothing(),
      /*optional in*/ const Maybe<nsTArray<uint8_t>>& stapledOCSPResponseArg =
          Nothing(),
      /*optional in*/ const Maybe<nsTArray<uint8_t>>& sctsFromTLS = Nothing(),
      /*optional in*/ const OriginAttributes& originAttributes =
          OriginAttributes(),
      /*optional out*/ EVStatus* evStatus = nullptr,
      /*optional out*/ OCSPStaplingStatus* ocspStaplingStatus = nullptr,
      /*optional out*/ KeySizeStatus* keySizeStatus = nullptr,
      /*optional out*/ PinningTelemetryInfo* pinningTelemetryInfo = nullptr,
      /*optional out*/ CertificateTransparencyInfo* ctInfo = nullptr,
      /*optional out*/ bool* isBuiltChainRootBuiltInRoot = nullptr,
      /*optional out*/ bool* madeOCSPRequests = nullptr,
      /*optional out*/ IssuerSources* = nullptr);

  mozilla::pkix::Result VerifySSLServerCert(
      const nsTArray<uint8_t>& peerCert, mozilla::pkix::Time time, void* pinarg,
      const nsACString& hostname,
      /*out*/ nsTArray<nsTArray<uint8_t>>& builtChain,
      /*optional*/ Flags flags = 0,
      /*optional*/ const Maybe<nsTArray<nsTArray<uint8_t>>>& extraCertificates =
          Nothing(),
      /*optional*/ const Maybe<nsTArray<uint8_t>>& stapledOCSPResponse =
          Nothing(),
      /*optional*/ const Maybe<nsTArray<uint8_t>>& sctsFromTLS = Nothing(),
      /*optional*/ const Maybe<DelegatedCredentialInfo>& dcInfo = Nothing(),
      /*optional*/ const OriginAttributes& originAttributes =
          OriginAttributes(),
      /*optional out*/ EVStatus* evStatus = nullptr,
      /*optional out*/ OCSPStaplingStatus* ocspStaplingStatus = nullptr,
      /*optional out*/ KeySizeStatus* keySizeStatus = nullptr,
      /*optional out*/ PinningTelemetryInfo* pinningTelemetryInfo = nullptr,
      /*optional out*/ CertificateTransparencyInfo* ctInfo = nullptr,
      /*optional out*/ bool* isBuiltChainRootBuiltInRoot = nullptr,
      /*optional out*/ bool* madeOCSPRequests = nullptr,
      /*optional out*/ IssuerSources* = nullptr);

  enum OcspDownloadConfig { ocspOff = 0, ocspOn = 1, ocspEVOnly = 2 };
  enum OcspStrictConfig { ocspRelaxed = 0, ocspStrict };

  enum class CertificateTransparencyMode {
    Disabled = 0,
    TelemetryOnly = 1,
    Enforce = 2,
  };

  struct CertificateTransparencyConfig {
    CertificateTransparencyConfig(
        CertificateTransparencyMode mode, nsCString&& skipForHosts,
        nsTArray<CopyableTArray<uint8_t>>&& skipForSPKIHashes)
        : mMode(mode),
          mSkipForHosts(std::move(skipForHosts)),
          mSkipForSPKIHashes(std::move(skipForSPKIHashes)) {}

    CertificateTransparencyMode mMode;
    nsCString mSkipForHosts;
    nsTArray<CopyableTArray<uint8_t>> mSkipForSPKIHashes;
  };

  CertVerifier(OcspDownloadConfig odc, OcspStrictConfig osc,
               mozilla::TimeDuration ocspTimeoutSoft,
               mozilla::TimeDuration ocspTimeoutHard,
               uint32_t certShortLifetimeInDays,
               CertificateTransparencyConfig&& ctConfig, CRLiteMode crliteMode,
               const nsTArray<EnterpriseCert>& thirdPartyCerts);
  ~CertVerifier();

  void ClearOCSPCache() { mOCSPCache.Clear(); }
  void ClearPrivateBrowsingOCSPCache() { mOCSPCache.ClearPrivateBrowsing(); }
  void ClearTrustCache() { trust_cache_clear(mTrustCache.get()); }

  const OcspDownloadConfig mOCSPDownloadConfig;
  const bool mOCSPStrict;
  const mozilla::TimeDuration mOCSPTimeoutSoft;
  const mozilla::TimeDuration mOCSPTimeoutHard;
  const uint32_t mCertShortLifetimeInDays;
  const CertificateTransparencyConfig mCTConfig;
  const CRLiteMode mCRLiteMode;

 private:
  OCSPCache mOCSPCache;
  // We keep a copy of the bytes of each third party root to own.
  nsTArray<EnterpriseCert> mThirdPartyCerts;
  // This is a reusable, precomputed list of Inputs corresponding to each root
  // in mThirdPartyCerts that wasn't too long to make an Input out of.
  nsTArray<mozilla::pkix::Input> mThirdPartyRootInputs;
  // Similarly, but with intermediates.
  nsTArray<mozilla::pkix::Input> mThirdPartyIntermediateInputs;

  // We only have a forward declarations of these classes (see above)
  // so we must allocate dynamically.
  UniquePtr<mozilla::ct::MultiLogCTVerifier> mCTVerifier;

  // If many connections are made to a site using a particular certificate,
  // this cache will speed up verifications after the first one by saving the
  // results of signature verification.
  // This will also be beneficial in situations where different sites use
  // different certificates that happen to be issued by the same intermediate.
  UniquePtr<SignatureCache, decltype(&signature_cache_free)> mSignatureCache;
  // Similarly, this caches the results of looking up the trust of a
  // certificate in NSS, which is slow.
  UniquePtr<TrustCache, decltype(&trust_cache_free)> mTrustCache;

  void LoadKnownCTLogs();
  mozilla::pkix::Result VerifyCertificateTransparencyPolicy(
      NSSCertDBTrustDomain& trustDomain,
      const nsTArray<nsTArray<uint8_t>>& builtChain,
      mozilla::pkix::Input sctsFromTLS, mozilla::pkix::Time time,
      const char* hostname,
      /*optional out*/ CertificateTransparencyInfo* ctInfo);
  mozilla::pkix::Result VerifyCertificateTransparencyPolicyInner(
      NSSCertDBTrustDomain& trustDomain,
      const nsTArray<nsTArray<uint8_t>>& builtChain,
      mozilla::pkix::Input sctsFromTLS, mozilla::pkix::Time time,
      /*optional out*/ CertificateTransparencyInfo* ctInfo);
};

mozilla::pkix::Result IsCertBuiltInRoot(pkix::Input certInput, bool& result);
mozilla::pkix::Result CertListContainsExpectedKeys(const CERTCertList* certList,
                                                   const char* hostname,
                                                   mozilla::pkix::Time time);

// Verify signed data, making use of the given SignatureCache. That is, if the
// (data, digestAlgorithm, signature, subjectPublicKeyInfo) tuple has already
// been verified and is in the cache, this skips the work of verifying the
// signature (which is slow) and returns the already-known result.
mozilla::pkix::Result VerifySignedDataWithCache(
    mozilla::pkix::der::PublicKeyAlgorithm publicKeyAlg,
    mozilla::glean::impl::DenominatorMetric telemetryDenominator,
    mozilla::glean::impl::NumeratorMetric telemetryNumerator,
    mozilla::pkix::Input data, mozilla::pkix::DigestAlgorithm digestAlgorithm,
    mozilla::pkix::Input signature, mozilla::pkix::Input subjectPublicKeyInfo,
    SignatureCache* signatureCache, void* pinArg);

}  // namespace psm
}  // namespace mozilla

#endif  // CertVerifier_h
