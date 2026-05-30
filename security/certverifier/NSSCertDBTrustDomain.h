/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NSSCertDBTrustDomain_h
#define NSSCertDBTrustDomain_h

#include "CertVerifier.h"
#include "CRLiteTimestamp.h"
#include "ScopedNSSTypes.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/TimeStamp.h"
#include "mozpkix/pkixtypes.h"
#include "nsICertStorage.h"
#include "nsString.h"
#include "secmodt.h"

namespace mozilla {
namespace psm {

enum class NSSDBConfig {
  ReadWrite = 0,
  ReadOnly = 1,
};

enum class PKCS11DBConfig {
  DoNotLoadModules = 0,
  LoadModules = 1,
};

enum class OCSPFetchStatus : uint16_t {
  NotFetched = 0,
  Fetched = 1,
};

// Helper struct to associate the DER bytes of a potential issuer certificate
// with its source (i.e. where it came from).
struct IssuerCandidateWithSource {
  mozilla::pkix::Input mDER;  // non-owning
  IssuerSource mIssuerSource;
};

SECStatus InitializeNSS(const nsACString& dir, NSSDBConfig nssDbConfig,
                        PKCS11DBConfig pkcs11DbConfig);

void DisableMD5();

/**
 * Loads root certificates from a module.
 *
 * @param dir
 *        The path to the directory containing the NSS builtin roots module.
 *        Usually the same as the path to the other NSS shared libraries.
 *        If empty, the (library) path will be searched.
 * @return true if the roots were successfully loaded, false otherwise.
 */

bool LoadLoadableRoots(const nsCString& dir);

/**
 * Loads root certificates from libxul.
 *
 * @return true if the roots were successfully loaded, false otherwise.
 */
bool LoadLoadableRootsFromXul();

/**
 * Loads the OS client certs module.
 *
 * @return true if the module was successfully loaded, false otherwise.
 */
bool LoadOSClientCertsModule();

/**
 * Loads the IPC client certs module.
 *
 * @param dir
 *        The path to the directory containing the module. This should be the
 *        same as where all of the other gecko libraries live.
 * @return true if the module was successfully loaded, false otherwise.
 */
bool LoadIPCClientCertsModule();

nsresult DefaultServerNicknameForCert(const CERTCertificate* cert,
                                      /*out*/ nsCString& nickname);

/**
 * Build nsTArray<uint8_t>s out of the issuer, serial, subject and public key
 * data from the supplied certificate for use in revocation checks.
 *
 * @param certDER
 *        The Input that references the encoded bytes of the certificate.
 * @param endEntityOrCA
 *        Whether the certificate is an end-entity or CA.
 * @param out encIssuer
 *        The array to populate with issuer data.
 * @param out encSerial
 *        The array to populate with serial number data.
 * @param out encSubject
 *        The array to populate with subject data.
 * @param out encPubKey
 *        The array to populate with public key data.
 * @return
 *        Result::Success, unless there's a problem decoding the certificate.
 */
pkix::Result BuildRevocationCheckArrays(pkix::Input certDER,
                                        pkix::EndEntityOrCA endEntityOrCA,
                                        /*out*/ nsTArray<uint8_t>& issuerBytes,
                                        /*out*/ nsTArray<uint8_t>& serialBytes,
                                        /*out*/ nsTArray<uint8_t>& subjectBytes,
                                        /*out*/ nsTArray<uint8_t>& pubKeyBytes);

class NSSCertDBTrustDomain : public mozilla::pkix::TrustDomain {
 public:
  typedef mozilla::pkix::Result Result;

  enum RevocationCheckMode {
    RevocationCheckLocalOnly = 0,
    RevocationCheckMayFetch = 1,
    RevocationCheckRequired = 2,
  };

  NSSCertDBTrustDomain(
      SECTrustType certDBTrustType, RevocationCheckMode ocspFetching,
      OCSPCache& ocspCache, SignatureCache* signatureCache,
      TrustCache* trustCache, void* pinArg,
      mozilla::TimeDuration ocspTimeoutSoft,
      mozilla::TimeDuration ocspTimeoutHard, uint32_t certShortLifetimeInDays,
      unsigned int minRSABits, CRLiteMode crliteMode,
      const OriginAttributes& originAttributes,
      const nsTArray<mozilla::pkix::Input>& thirdPartyRootInputs,
      const nsTArray<mozilla::pkix::Input>& thirdPartyIntermediateInputs,
      const Maybe<nsTArray<nsTArray<uint8_t>>>& extraCertificates,
      const mozilla::pkix::Input& encodedSCTsFromTLS,
      const UniquePtr<mozilla::ct::MultiLogCTVerifier>& ctVerifier,
      /*out*/ nsTArray<nsTArray<uint8_t>>& builtChain,
      /*optional*/ PinningTelemetryInfo* pinningTelemetryInfo = nullptr,
      /*optional*/ const char* hostname = nullptr);

  virtual Result FindIssuer(mozilla::pkix::Input encodedIssuerName,
                            IssuerChecker& checker,
                            mozilla::pkix::Time time) override;

  virtual Result GetCertTrust(
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      const mozilla::pkix::CertPolicyId& policy,
      mozilla::pkix::Input candidateCertDER,
      /*out*/ mozilla::pkix::TrustLevel& trustLevel) override;

  virtual Result CheckSignatureDigestAlgorithm(
      mozilla::pkix::DigestAlgorithm digestAlg,
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      mozilla::pkix::Time notBefore) override;

  virtual Result CheckRSAPublicKeyModulusSizeInBits(
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      unsigned int modulusSizeInBits) override;

  virtual Result VerifyRSAPKCS1SignedData(
      mozilla::pkix::Input data, mozilla::pkix::DigestAlgorithm digestAlgorithm,
      mozilla::pkix::Input signature,
      mozilla::pkix::Input subjectPublicKeyInfo) override;

  virtual Result VerifyRSAPSSSignedData(
      mozilla::pkix::Input data, mozilla::pkix::DigestAlgorithm digestAlgorithm,
      mozilla::pkix::Input signature,
      mozilla::pkix::Input subjectPublicKeyInfo) override;

  virtual Result CheckECDSACurveIsAcceptable(
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      mozilla::pkix::NamedCurve curve) override;

  virtual Result VerifyECDSASignedData(
      mozilla::pkix::Input data, mozilla::pkix::DigestAlgorithm digestAlgorithm,
      mozilla::pkix::Input signature,
      mozilla::pkix::Input subjectPublicKeyInfo) override;

  virtual Result VerifyMLDSASignedData(
      mozilla::pkix::Input data, mozilla::pkix::Input signature,
      mozilla::pkix::Input subjectPublicKeyInfo) override;

  virtual Result DigestBuf(mozilla::pkix::Input item,
                           mozilla::pkix::DigestAlgorithm digestAlg,
                           /*out*/ uint8_t* digestBuf,
                           size_t digestBufLen) override;

  virtual Result CheckValidityIsAcceptable(
      mozilla::pkix::Time notBefore, mozilla::pkix::Time notAfter,
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      mozilla::pkix::KeyPurposeId keyPurpose) override;

  virtual Result CheckRevocation(
      mozilla::pkix::EndEntityOrCA endEntityOrCA,
      const mozilla::pkix::CertID& certID, mozilla::pkix::Time time,
      mozilla::pkix::Duration validityDuration,
      /*optional*/ const mozilla::pkix::Input* stapledOCSPResponse,
      /*optional*/ const mozilla::pkix::Input* aiaExtension) override;

  virtual Result IsChainValid(
      const mozilla::pkix::DERArray& certChain, mozilla::pkix::Time time,
      const mozilla::pkix::CertPolicyId& requiredPolicy) override;

  virtual void NoteAuxiliaryExtension(
      mozilla::pkix::AuxiliaryExtension extension,
      mozilla::pkix::Input extensionData) override;

  // Resets the OCSP stapling status and SCT lists accumulated during
  // the chain building.
  void ResetAccumulatedState();
  // Resets state related to a fully-built candidate chain that becomes invalid
  // if that chain is found to not be acceptable.
  void ResetCandidateBuiltChainState();

  CertVerifier::OCSPStaplingStatus GetOCSPStaplingStatus() const {
    return mOCSPStaplingStatus;
  }

  // SCT lists (see Certificate Transparency) extracted during
  // certificate verification. Note that the returned Inputs are invalidated
  // the next time a chain is built and by ResetAccumulatedState method
  // (and when the TrustDomain object is destroyed).

  mozilla::pkix::Input GetSCTListFromCertificate() const;
  mozilla::pkix::Input GetSCTListFromOCSPStapling() const;

  Maybe<ct::CTVerifyResult>& GetCachedCTVerifyResult();

  bool GetIsBuiltChainRootBuiltInRoot() const;

  OCSPFetchStatus GetOCSPFetchStatus() { return mOCSPFetchStatus; }
  IssuerSources GetIssuerSources() { return mIssuerSources; }
  Maybe<mozilla::pkix::Time> GetDistrustAfterTime() {
    return mDistrustAfterTime;
  }

 private:
  Result CheckCRLite(
      const nsTArray<uint8_t>& issuerSubjectPublicKeyInfoBytes,
      const nsTArray<uint8_t>& serialNumberBytes,
      const nsTArray<RefPtr<nsICRLiteTimestamp>>& crliteTimestamps,
      mozilla::pkix::Time time, bool& filterCoversCertificate);

  enum EncodedResponseSource {
    ResponseIsFromNetwork = 1,
    ResponseWasStapled = 2
  };
  Result VerifyAndMaybeCacheEncodedOCSPResponse(
      const mozilla::pkix::CertID& certID, mozilla::pkix::Time time,
      uint16_t maxLifetimeInDays, mozilla::pkix::Input encodedResponse,
      EncodedResponseSource responseSource, /*out*/ bool& expired);
  TimeDuration GetOCSPTimeout() const;

  Result CheckRevocationByCRLite(const mozilla::pkix::CertID& certID,
                                 mozilla::pkix::Time time,
                                 /*out*/ bool& crliteCoversCertificate);

  Result CheckRevocationByOCSP(
      const mozilla::pkix::CertID& certID, mozilla::pkix::Time time,
      mozilla::pkix::Duration validityDuration, const nsCString& aiaLocation,
      /*optional*/ const mozilla::pkix::Input* stapledOCSPResponse);

  Result SynchronousCheckRevocationWithServer(
      const mozilla::pkix::CertID& certID, const nsCString& aiaLocation,
      mozilla::pkix::Time time, uint16_t maxOCSPLifetimeInDays,
      const Result cachedResponseResult,
      const Result stapledOCSPResponseResult);
  Result HandleOCSPFailure(const Result cachedResponseResult,
                           const Result stapledOCSPResponseResult,
                           const Result error);

  bool ShouldSkipSelfSignedNonTrustAnchor(mozilla::pkix::Input certDER);
  Result CheckCandidates(IssuerChecker& checker,
                         nsTArray<IssuerCandidateWithSource>& candidates,
                         mozilla::pkix::Input* nameConstraintsInputPtr,
                         bool& keepGoing);

  const SECTrustType mCertDBTrustType;
  const RevocationCheckMode mOCSPFetching;
  OCSPCache& mOCSPCache;            // non-owning!
  SignatureCache* mSignatureCache;  // non-owning!
  TrustCache* mTrustCache;          // non-owning!
  void* mPinArg;                    // non-owning!
  const mozilla::TimeDuration mOCSPTimeoutSoft;
  const mozilla::TimeDuration mOCSPTimeoutHard;
  const uint32_t mCertShortLifetimeInDays;
  const unsigned int mMinRSABits;
  CRLiteMode mCRLiteMode;
  const OriginAttributes& mOriginAttributes;
  const nsTArray<mozilla::pkix::Input>& mThirdPartyRootInputs;  // non-owning
  const nsTArray<mozilla::pkix::Input>&
      mThirdPartyIntermediateInputs;                              // non-owning
  const Maybe<nsTArray<nsTArray<uint8_t>>>& mExtraCertificates;   // non-owning
  const mozilla::pkix::Input& mEncodedSCTsFromTLS;                // non-owning
  const UniquePtr<mozilla::ct::MultiLogCTVerifier>& mCTVerifier;  // non-owning
  nsTArray<nsTArray<uint8_t>>& mBuiltChain;                       // non-owning
  bool mIsBuiltChainRootBuiltInRoot;
  PinningTelemetryInfo* mPinningTelemetryInfo;
  const char* mHostname;  // non-owning - only used for pinning checks
  nsCOMPtr<nsICertStorage> mCertStorage;
  CertVerifier::OCSPStaplingStatus mOCSPStaplingStatus;
  // Certificate Transparency data extracted during certificate verification
  UniqueSECItem mSCTListFromCertificate;
  UniqueSECItem mSCTListFromOCSPStapling;

  // The built-in roots module, if available.
  UniqueSECMODModule mBuiltInRootsModule;

  OCSPFetchStatus mOCSPFetchStatus;
  IssuerSources mIssuerSources;
  Maybe<mozilla::pkix::Time> mDistrustAfterTime;
  Maybe<mozilla::ct::CTVerifyResult> mCTVerifyResult;
};

}  // namespace psm
}  // namespace mozilla

#endif  // NSSCertDBTrustDomain_h
