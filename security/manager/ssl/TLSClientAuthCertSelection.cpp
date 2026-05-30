/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Implements the client authentication certificate selection callback for NSS.
// nsNSSIOLayer.cpp sets the callback by calling SSL_GetClientAuthDataHook and
// identifying SSLGetClientAuthDataHook as the function to call when a TLS
// server requests a client authentication certificate.
//
// In the general case, SSLGetClientAuthDataHook (running on the socket
// thread), does next to nothing. It may return early if it determines it would
// not be suitable to send a client authentication certificate on this
// connection (particularly for speculative connections, which also get
// canceled at this time), but otherwise it notes that a certificate was
// requested and returns an indication that the connection would block to NSS.
//
// When the server certificate verifies successfully, nsSSLIOLayerPoll (running
// on the socket thread) will see that a certificate has been requested on that
// connection, whereupon it calls DoSelectClientAuthCertificate to do the work
// of selecting a certificate. In general, this involves dispatching an event
// to the main thread to ask the user to select a client authentication
// certificate. When the user selects a client certificate (or opts not to send
// one), an event is dispatched to the socket thread that gives NSS the
// appropriate information to proceed with the TLS connection.
//
// If networking is being done on the socket process,
// DoSelectClientAuthCertificate sends an IPC call to the parent process to ask
// the user to select a certificate. When a certificate (or no certificate) has
// been selected, the parent process sends an IPC call back to the socket
// process, which causes an event to be dispatched to the socket thread to
// continue to the TLS connection.

#include "TLSClientAuthCertSelection.h"
#include "cert_storage/src/cert_storage.h"
#include "mozilla/Logging.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/glean/SecurityManagerSslMetrics.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/net/DocumentLoadListener.h"
#include "mozilla/net/SocketProcessBackgroundChild.h"
#include "mozilla/psm/SelectTLSClientAuthCertChild.h"
#include "mozilla/psm/SelectTLSClientAuthCertParent.h"
#include "nsArray.h"
#include "nsArrayUtils.h"
#include "nsNSSComponent.h"
#include "nsIClientAuthDialogService.h"
#include "nsIMutableArray.h"
#include "nsINSSComponent.h"
#include "NSSCertDBTrustDomain.h"
#include "nsIClientAuthRememberService.h"
#include "nsIX509CertDB.h"
#include "nsNSSHelper.h"
#include "mozpkix/pkixnss.h"
#include "mozpkix/pkixutil.h"
#include "mozpkix/pkix.h"
#include "secerr.h"
#include "sslerr.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/java/ClientAuthCertificateManagerWrappers.h"
#endif  // MOZ_WIDGET_ANDROID

using namespace mozilla;
using namespace mozilla::pkix;
using namespace mozilla::psm;

extern LazyLogModule gPIPNSSLog;

mozilla::pkix::Result BuildChainForCertificate(
    nsTArray<uint8_t>& certBytes, nsTArray<nsTArray<uint8_t>>& certChainBytes,
    const nsTArray<nsTArray<uint8_t>>& caNames,
    const nsTArray<nsTArray<uint8_t>>& enterpriseCertificates);

// Possible behaviors for choosing a cert for client auth.
enum class UserCertChoice {
  // Ask the user to choose a cert.
  Ask = 0,
  // Automatically choose a cert.
  Auto = 1,
};

// Returns the most appropriate user cert choice based on the value of the
// security.default_personal_cert preference.
UserCertChoice nsGetUserCertChoice() {
  nsAutoCString value;
  nsresult rv =
      Preferences::GetCString("security.default_personal_cert", value);
  if (NS_FAILED(rv)) {
    return UserCertChoice::Ask;
  }

  // There are three cases for what the preference could be set to:
  //   1. "Select Automatically" -> Auto.
  //   2. "Ask Every Time" -> Ask.
  //   3. Something else -> Ask. This might be a nickname from a migrated cert,
  //      but we no longer support this case.
  return value.EqualsLiteral("Select Automatically") ? UserCertChoice::Auto
                                                     : UserCertChoice::Ask;
}

static bool hasExplicitKeyUsageNonRepudiation(CERTCertificate* cert) {
  // There is no extension, v1 or v2 certificate
  if (!cert->extensions) return false;

  SECStatus srv;
  SECItem keyUsageItem;
  keyUsageItem.data = nullptr;

  srv = CERT_FindKeyUsageExtension(cert, &keyUsageItem);
  if (srv == SECFailure) return false;

  unsigned char keyUsage = keyUsageItem.data[0];
  PORT_Free(keyUsageItem.data);

  return !!(keyUsage & KU_NON_REPUDIATION);
}

ClientAuthInfo::ClientAuthInfo(const nsACString& hostName,
                               const OriginAttributes& originAttributes,
                               int32_t port, uint32_t providerFlags,
                               uint32_t providerTlsFlags)
    : mHostName(hostName),
      mOriginAttributes(originAttributes),
      mPort(port),
      mProviderFlags(providerFlags),
      mProviderTlsFlags(providerTlsFlags) {}

ClientAuthInfo::ClientAuthInfo(ClientAuthInfo&& aOther) noexcept
    : mHostName(std::move(aOther.mHostName)),
      mOriginAttributes(std::move(aOther.mOriginAttributes)),
      mPort(aOther.mPort),
      mProviderFlags(aOther.mProviderFlags),
      mProviderTlsFlags(aOther.mProviderTlsFlags) {}

const nsACString& ClientAuthInfo::HostName() const { return mHostName; }

const OriginAttributes& ClientAuthInfo::OriginAttributesRef() const {
  return mOriginAttributes;
}

int32_t ClientAuthInfo::Port() const { return mPort; }

uint32_t ClientAuthInfo::ProviderFlags() const { return mProviderFlags; }

uint32_t ClientAuthInfo::ProviderTlsFlags() const { return mProviderTlsFlags; }

nsTArray<nsTArray<uint8_t>> CollectCANames(CERTDistNames* caNames) {
  MOZ_ASSERT(caNames);

  nsTArray<nsTArray<uint8_t>> collectedCANames;
  if (!caNames) {
    return collectedCANames;
  }

  for (int i = 0; i < caNames->nnames; i++) {
    nsTArray<uint8_t> caName;
    caName.AppendElements(caNames->names[i].data, caNames->names[i].len);
    collectedCANames.AppendElement(std::move(caName));
  }
  return collectedCANames;
}

// This TrustDomain only exists to facilitate the mozilla::pkix path building
// algorithm. It considers any certificate with an issuer distinguished name in
// the set of given CA names to be a trust anchor. It does essentially no
// validation or verification (in particular, the signature checking function
// always returns "Success").
class ClientAuthCertNonverifyingTrustDomain final : public TrustDomain {
 public:
  ClientAuthCertNonverifyingTrustDomain(
      const nsTArray<nsTArray<uint8_t>>& caNames,
      const nsTArray<nsTArray<uint8_t>>& thirdPartyCertificates)
      : mCANames(caNames),
        mCertStorage(do_GetService(NS_CERT_STORAGE_CID)),
        mThirdPartyCertificates(thirdPartyCertificates) {}

  virtual mozilla::pkix::Result GetCertTrust(
      pkix::EndEntityOrCA endEntityOrCA, const pkix::CertPolicyId& policy,
      pkix::Input candidateCertDER,
      /*out*/ pkix::TrustLevel& trustLevel) override;
  virtual mozilla::pkix::Result FindIssuer(pkix::Input encodedIssuerName,
                                           IssuerChecker& checker,
                                           pkix::Time time) override;

  virtual mozilla::pkix::Result CheckRevocation(
      EndEntityOrCA endEntityOrCA, const pkix::CertID& certID, Time time,
      mozilla::pkix::Duration validityDuration,
      /*optional*/ const Input* stapledOCSPresponse,
      /*optional*/ const Input* aiaExtension) override {
    return pkix::Success;
  }

  virtual mozilla::pkix::Result IsChainValid(
      const pkix::DERArray& certChain, pkix::Time time,
      const pkix::CertPolicyId& requiredPolicy) override;

  virtual mozilla::pkix::Result CheckSignatureDigestAlgorithm(
      pkix::DigestAlgorithm digestAlg, pkix::EndEntityOrCA endEntityOrCA,
      pkix::Time notBefore) override {
    return pkix::Success;
  }
  virtual mozilla::pkix::Result CheckRSAPublicKeyModulusSizeInBits(
      pkix::EndEntityOrCA endEntityOrCA,
      unsigned int modulusSizeInBits) override {
    return pkix::Success;
  }
  virtual mozilla::pkix::Result VerifyRSAPKCS1SignedData(
      pkix::Input data, pkix::DigestAlgorithm, pkix::Input signature,
      pkix::Input subjectPublicKeyInfo) override {
    return pkix::Success;
  }
  virtual mozilla::pkix::Result VerifyRSAPSSSignedData(
      pkix::Input data, pkix::DigestAlgorithm, pkix::Input signature,
      pkix::Input subjectPublicKeyInfo) override {
    return pkix::Success;
  }
  virtual mozilla::pkix::Result CheckECDSACurveIsAcceptable(
      pkix::EndEntityOrCA endEntityOrCA, pkix::NamedCurve curve) override {
    return pkix::Success;
  }
  virtual mozilla::pkix::Result VerifyECDSASignedData(
      pkix::Input data, pkix::DigestAlgorithm, pkix::Input signature,
      pkix::Input subjectPublicKeyInfo) override {
    return pkix::Success;
  }
  virtual mozilla::pkix::Result VerifyMLDSASignedData(
      pkix::Input data, pkix::Input signature,
      pkix::Input subjectPublicKeyInfo) override {
    return pkix::Success;
  }
  virtual mozilla::pkix::Result CheckValidityIsAcceptable(
      pkix::Time notBefore, pkix::Time notAfter,
      pkix::EndEntityOrCA endEntityOrCA,
      pkix::KeyPurposeId keyPurpose) override {
    return pkix::Success;
  }
  virtual void NoteAuxiliaryExtension(pkix::AuxiliaryExtension extension,
                                      pkix::Input extensionData) override {}
  virtual mozilla::pkix::Result DigestBuf(pkix::Input item,
                                          pkix::DigestAlgorithm digestAlg,
                                          /*out*/ uint8_t* digestBuf,
                                          size_t digestBufLen) override {
    return pkix::DigestBufNSS(item, digestAlg, digestBuf, digestBufLen);
  }

  nsTArray<nsTArray<uint8_t>> TakeBuiltChain() {
    return std::move(mBuiltChain);
  }

 private:
  const nsTArray<nsTArray<uint8_t>>& mCANames;  // non-owning
  nsCOMPtr<nsICertStorage> mCertStorage;
  const nsTArray<nsTArray<uint8_t>>& mThirdPartyCertificates;  // non-owning
  nsTArray<nsTArray<uint8_t>> mBuiltChain;
};

mozilla::pkix::Result ClientAuthCertNonverifyingTrustDomain::GetCertTrust(
    pkix::EndEntityOrCA endEntityOrCA, const pkix::CertPolicyId& policy,
    pkix::Input candidateCertDER,
    /*out*/ pkix::TrustLevel& trustLevel) {
  // If the server did not specify any CA names, all client certificates are
  // acceptable.
  if (mCANames.Length() == 0) {
    trustLevel = pkix::TrustLevel::TrustAnchor;
    return pkix::Success;
  }
  BackCert cert(candidateCertDER, endEntityOrCA, nullptr);
  mozilla::pkix::Result rv = cert.Init();
  if (rv != pkix::Success) {
    return rv;
  }
  // If this certificate's issuer distinguished name is in the set of acceptable
  // CA names, we say this is a trust anchor so that the client certificate
  // issued from this certificate will be presented as an option for the user.
  // We also check the certificate's subject distinguished name to account for
  // the case where client certificates that have the id-kp-OCSPSigning EKU
  // can't be trust anchors according to mozilla::pkix, and thus we may be
  // looking directly at the issuer.
  pkix::Input issuer(cert.GetIssuer());
  pkix::Input subject(cert.GetSubject());
  for (const auto& caName : mCANames) {
    pkix::Input caNameInput;
    rv = caNameInput.Init(caName.Elements(), caName.Length());
    if (rv != pkix::Success) {
      continue;  // probably too big
    }
    if (InputsAreEqual(issuer, caNameInput) ||
        InputsAreEqual(subject, caNameInput)) {
      trustLevel = pkix::TrustLevel::TrustAnchor;
      return pkix::Success;
    }
  }
  trustLevel = pkix::TrustLevel::InheritsTrust;
  return pkix::Success;
}

// In theory this implementation should only need to consider intermediate
// certificates, since in theory it should only need to look at the issuer
// distinguished name of each certificate to determine if the client
// certificate is considered acceptable to the server.
// However, because we need to account for client certificates with the
// id-kp-OCSPSigning EKU, and because mozilla::pkix doesn't allow such
// certificates to be trust anchors, we need to consider the issuers of such
// certificates directly. These issuers could be roots, so we have to consider
// roots here.
mozilla::pkix::Result ClientAuthCertNonverifyingTrustDomain::FindIssuer(
    pkix::Input encodedIssuerName, IssuerChecker& checker, pkix::Time time) {
  // First try all relevant certificates known to Gecko, which avoids calling
  // CERT_CreateSubjectCertList, because that can be expensive.
  Vector<pkix::Input> geckoCandidates;
  if (!mCertStorage) {
    return mozilla::pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }
  nsTArray<uint8_t> subject;
  subject.AppendElements(encodedIssuerName.UnsafeGetData(),
                         encodedIssuerName.GetLength());
  nsTArray<nsTArray<uint8_t>> certs;
  nsresult rv = mCertStorage->FindCertsBySubject(subject, certs);
  if (NS_FAILED(rv)) {
    return mozilla::pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
  }
  for (auto& cert : certs) {
    pkix::Input certDER;
    mozilla::pkix::Result rv = certDER.Init(cert.Elements(), cert.Length());
    if (rv != pkix::Success) {
      continue;  // probably too big
    }
    if (!geckoCandidates.append(certDER)) {
      return mozilla::pkix::Result::FATAL_ERROR_NO_MEMORY;
    }
  }

  for (const auto& thirdPartyCertificate : mThirdPartyCertificates) {
    pkix::Input thirdPartyCertificateInput;
    mozilla::pkix::Result rv = thirdPartyCertificateInput.Init(
        thirdPartyCertificate.Elements(), thirdPartyCertificate.Length());
    if (rv != pkix::Success) {
      continue;  // probably too big
    }
    if (!geckoCandidates.append(thirdPartyCertificateInput)) {
      return mozilla::pkix::Result::FATAL_ERROR_NO_MEMORY;
    }
  }

  bool keepGoing = true;
  for (pkix::Input candidate : geckoCandidates) {
    mozilla::pkix::Result rv = checker.Check(candidate, nullptr, keepGoing);
    if (rv != pkix::Success) {
      return rv;
    }
    if (!keepGoing) {
      return pkix::Success;
    }
  }

  SECItem encodedIssuerNameItem =
      pkix::UnsafeMapInputToSECItem(encodedIssuerName);
  // NSS seems not to differentiate between "no potential issuers found" and
  // "there was an error trying to retrieve the potential issuers." We assume
  // there was no error if CERT_CreateSubjectCertList returns nullptr.
  UniqueCERTCertList candidates(CERT_CreateSubjectCertList(
      nullptr, CERT_GetDefaultCertDB(), &encodedIssuerNameItem, 0, false));
  Vector<pkix::Input> nssCandidates;
  if (candidates) {
    for (CERTCertListNode* n = CERT_LIST_HEAD(candidates);
         !CERT_LIST_END(n, candidates); n = CERT_LIST_NEXT(n)) {
      pkix::Input certDER;
      mozilla::pkix::Result rv =
          certDER.Init(n->cert->derCert.data, n->cert->derCert.len);
      if (rv != pkix::Success) {
        continue;  // probably too big
      }
      if (!nssCandidates.append(certDER)) {
        return mozilla::pkix::Result::FATAL_ERROR_NO_MEMORY;
      }
    }
  }

  for (pkix::Input candidate : nssCandidates) {
    mozilla::pkix::Result rv = checker.Check(candidate, nullptr, keepGoing);
    if (rv != pkix::Success) {
      return rv;
    }
    if (!keepGoing) {
      return pkix::Success;
    }
  }
  return pkix::Success;
}

mozilla::pkix::Result ClientAuthCertNonverifyingTrustDomain::IsChainValid(
    const pkix::DERArray& certArray, pkix::Time, const pkix::CertPolicyId&) {
  mBuiltChain.Clear();

  size_t numCerts = certArray.GetLength();
  for (size_t i = 0; i < numCerts; ++i) {
    nsTArray<uint8_t> certBytes;
    const pkix::Input* certInput = certArray.GetDER(i);
    MOZ_ASSERT(certInput != nullptr);
    if (!certInput) {
      return mozilla::pkix::Result::FATAL_ERROR_LIBRARY_FAILURE;
    }
    certBytes.AppendElements(certInput->UnsafeGetData(),
                             certInput->GetLength());
    mBuiltChain.AppendElement(std::move(certBytes));
  }

  return pkix::Success;
}

nsTArray<nsTArray<uint8_t>> GetEnterpriseCertificates() {
  nsTArray<nsTArray<uint8_t>> enterpriseCertificates;
  nsCOMPtr<nsINSSComponent> component(do_GetService(PSM_COMPONENT_CONTRACTID));
  if (!component) {
    return nsTArray<nsTArray<uint8_t>>{};
  }
  nsresult rv = component->GetEnterpriseIntermediates(enterpriseCertificates);
  if (NS_FAILED(rv)) {
    return nsTArray<nsTArray<uint8_t>>{};
  }
  nsTArray<nsTArray<uint8_t>> enterpriseRoots;
  rv = component->GetEnterpriseRoots(enterpriseRoots);
  if (NS_FAILED(rv)) {
    return nsTArray<nsTArray<uint8_t>>{};
  }
  enterpriseCertificates.AppendElements(std::move(enterpriseRoots));
  return enterpriseCertificates;
}

bool FindRememberedDecision(
    const ClientAuthInfo& clientAuthInfo,
    const nsTArray<nsTArray<uint8_t>>& caNames,
    const nsTArray<nsTArray<uint8_t>>& enterpriseCertificates,
    nsTArray<uint8_t>& rememberedCertBytes,
    nsTArray<nsTArray<uint8_t>>& rememberedCertChainBytes) {
  rememberedCertBytes.Clear();
  rememberedCertChainBytes.Clear();

  if (clientAuthInfo.ProviderTlsFlags() != 0) {
    return false;
  }

  nsCOMPtr<nsIClientAuthRememberService> clientAuthRememberService(
      do_GetService(NS_CLIENTAUTHREMEMBERSERVICE_CONTRACTID));
  if (!clientAuthRememberService) {
    return false;
  }

  nsCString rememberedDBKey;
  bool found;
  nsresult rv = clientAuthRememberService->HasRememberedDecision(
      clientAuthInfo.HostName(), clientAuthInfo.OriginAttributesRef(),
      rememberedDBKey, &found);
  if (NS_FAILED(rv)) {
    return false;
  }
  if (!found) {
    return false;
  }
  // An empty dbKey indicates that the user chose not to use a certificate
  // and chose to remember this decision
  if (rememberedDBKey.IsEmpty()) {
    return true;
  }
  nsCOMPtr<nsIX509CertDB> certdb(do_GetService(NS_X509CERTDB_CONTRACTID));
  if (!certdb) {
    return false;
  }
  nsCOMPtr<nsIX509Cert> foundCert;
  rv = certdb->FindCertByDBKey(rememberedDBKey, getter_AddRefs(foundCert));
  if (NS_FAILED(rv)) {
    return false;
  }
  if (!foundCert) {
    return false;
  }
  rv = foundCert->GetRawDER(rememberedCertBytes);
  if (NS_FAILED(rv)) {
    return false;
  }
  if (BuildChainForCertificate(rememberedCertBytes, rememberedCertChainBytes,
                               caNames, enterpriseCertificates) != Success) {
    return false;
  }
  return true;
}

// Filter potential client certificates by the specified CA names, if any. This
// operation potentially builds a certificate chain for each candidate client
// certificate. Keeping those chains around means they don't have to be
// re-built later when the user selects a particular client certificate.
void FilterPotentialClientCertificatesByCANames(
    UniqueCERTCertList& potentialClientCertificates,
    const nsTArray<nsTArray<uint8_t>>& caNames,
    const nsTArray<nsTArray<uint8_t>>& enterpriseCertificates,
    nsTArray<nsTArray<nsTArray<uint8_t>>>& potentialClientCertificateChains) {
  if (!potentialClientCertificates) {
    return;
  }

  CERTCertListNode* n = CERT_LIST_HEAD(potentialClientCertificates);
  while (!CERT_LIST_END(n, potentialClientCertificates)) {
    nsTArray<nsTArray<uint8_t>> builtChain;
    nsTArray<uint8_t> certBytes;
    certBytes.AppendElements(n->cert->derCert.data, n->cert->derCert.len);
    mozilla::pkix::Result result = BuildChainForCertificate(
        certBytes, builtChain, caNames, enterpriseCertificates);
    if (result != pkix::Success) {
      MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
              ("removing cert '%s'", n->cert->subjectName));
      CERTCertListNode* toRemove = n;
      n = CERT_LIST_NEXT(n);
      CERT_RemoveCertListNode(toRemove);
      continue;
    }
    potentialClientCertificateChains.AppendElement(std::move(builtChain));
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("keeping cert '%s'\n", n->cert->subjectName));
    n = CERT_LIST_NEXT(n);
  }
}

void ClientAuthCertificateSelectedBase::SetSelectedClientAuthData(
    nsTArray<uint8_t>&& selectedCertBytes,
    nsTArray<nsTArray<uint8_t>>&& selectedCertChainBytes) {
  mSelectedCertBytes = std::move(selectedCertBytes);
  mSelectedCertChainBytes = std::move(selectedCertChainBytes);
}

NS_IMETHODIMP
ClientAuthCertificateSelected::Run() {
  mSocketInfo->ClientAuthCertificateSelected(mSelectedCertBytes,
                                             mSelectedCertChainBytes);
  return NS_OK;
}

void SelectClientAuthCertificate::DispatchContinuation(
    nsTArray<uint8_t>&& selectedCertBytes) {
  nsTArray<nsTArray<uint8_t>> selectedCertChainBytes;
  // Attempt to find a pre-built certificate chain corresponding to the
  // selected certificate.
  // On Android, there are no pre-built certificate chains, so use what the OS
  // says is the issuer certificate chain.
#ifdef MOZ_WIDGET_ANDROID
  if (jni::IsAvailable()) {
    jni::ByteArray::LocalRef certBytes = jni::ByteArray::New(
        reinterpret_cast<const int8_t*>(selectedCertBytes.Elements()),
        selectedCertBytes.Length());
    jni::ObjectArray::LocalRef issuersBytes =
        java::ClientAuthCertificateManager::GetCertificateIssuersBytes(
            certBytes);
    if (issuersBytes) {
      for (size_t i = 0; i < issuersBytes->Length(); i++) {
        jni::ByteArray::LocalRef issuer = issuersBytes->GetElement(i);
        nsTArray<uint8_t> issuerBytes(
            reinterpret_cast<uint8_t*>(issuer->GetElements().Elements()),
            issuer->Length());
        selectedCertChainBytes.AppendElement(std::move(issuerBytes));
      }
    }
  }
#else
  for (const auto& clientCertificateChain : mPotentialClientCertificateChains) {
    if (clientCertificateChain.Length() > 0 &&
        clientCertificateChain[0] == selectedCertBytes) {
      for (const auto& certificateBytes : clientCertificateChain) {
        selectedCertChainBytes.AppendElement(certificateBytes.Clone());
      }
      break;
    }
  }
#endif  // MOZ_WIDGET_ANDROID
  mContinuation->SetSelectedClientAuthData(std::move(selectedCertBytes),
                                           std::move(selectedCertChainBytes));
  nsCOMPtr<nsIEventTarget> socketThread(
      do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID));
  if (socketThread) {
    (void)socketThread->Dispatch(mContinuation, NS_DISPATCH_NORMAL);
  }
}

// Helper function to build a certificate chain from the given certificate to a
// trust anchor in the set indicated by the peer (mCANames). This is essentially
// best-effort, so no signature verification occurs.
mozilla::pkix::Result BuildChainForCertificate(
    nsTArray<uint8_t>& certBytes, nsTArray<nsTArray<uint8_t>>& certChainBytes,
    const nsTArray<nsTArray<uint8_t>>& caNames,
    const nsTArray<nsTArray<uint8_t>>& enterpriseCertificates) {
  ClientAuthCertNonverifyingTrustDomain trustDomain(caNames,
                                                    enterpriseCertificates);
  pkix::Input certDER;
  mozilla::pkix::Result result =
      certDER.Init(certBytes.Elements(), certBytes.Length());
  if (result != pkix::Success) {
    return result;
  }
  // Client certificates shouldn't be CAs, but for interoperability reasons we
  // attempt to build a path with each certificate as an end entity and then as
  // a CA if that fails.
  const pkix::EndEntityOrCA kEndEntityOrCAParams[] = {
      pkix::EndEntityOrCA::MustBeEndEntity, pkix::EndEntityOrCA::MustBeCA};
  // mozilla::pkix rejects certificates with id-kp-OCSPSigning unless it is
  // specifically required. A client certificate should never have this EKU.
  // Unfortunately, there are some client certificates in private PKIs that
  // have this EKU. For interoperability, we attempt to work around this
  // restriction in mozilla::pkix by first building the certificate chain with
  // no particular EKU required and then again with id-kp-OCSPSigning required
  // if that fails.
  const pkix::KeyPurposeId kKeyPurposeIdParams[] = {
      pkix::KeyPurposeId::anyExtendedKeyUsage,
      pkix::KeyPurposeId::id_kp_OCSPSigning};
  for (const auto& endEntityOrCAParam : kEndEntityOrCAParams) {
    for (const auto& keyPurposeIdParam : kKeyPurposeIdParams) {
      mozilla::pkix::Result result = BuildCertChain(
          trustDomain, certDER, Now(), endEntityOrCAParam,
          KeyUsage::noParticularKeyUsageRequired, keyPurposeIdParam,
          pkix::CertPolicyId::anyPolicy, nullptr);
      if (result == pkix::Success) {
        certChainBytes = trustDomain.TakeBuiltChain();
        return pkix::Success;
      }
    }
  }
  return mozilla::pkix::Result::ERROR_UNKNOWN_ISSUER;
}

class ClientAuthDialogCallback : public nsIClientAuthDialogCallback {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICLIENTAUTHDIALOGCALLBACK

  explicit ClientAuthDialogCallback(
      SelectClientAuthCertificate* selectClientAuthCertificate)
      : mSelectClientAuthCertificate(selectClientAuthCertificate) {}

 private:
  virtual ~ClientAuthDialogCallback() = default;

  RefPtr<SelectClientAuthCertificate> mSelectClientAuthCertificate;
};

NS_IMPL_ISUPPORTS(ClientAuthDialogCallback, nsIClientAuthDialogCallback)

NS_IMETHODIMP
ClientAuthDialogCallback::CertificateChosen(
    nsIX509Cert* cert,
    nsIClientAuthRememberService::Duration rememberDuration) {
  MOZ_ASSERT(mSelectClientAuthCertificate);
  if (!mSelectClientAuthCertificate) {
    return NS_ERROR_FAILURE;
  }
  const ClientAuthInfo& info = mSelectClientAuthCertificate->Info();
  nsCOMPtr<nsIClientAuthRememberService> clientAuthRememberService(
      do_GetService(NS_CLIENTAUTHREMEMBERSERVICE_CONTRACTID));
  if (info.ProviderTlsFlags() == 0 && clientAuthRememberService) {
    (void)clientAuthRememberService->RememberDecision(
        info.HostName(), info.OriginAttributesRef(), cert, rememberDuration);
  }
  nsTArray<uint8_t> selectedCertBytes;
  if (cert) {
    nsresult rv = cert->GetRawDER(selectedCertBytes);
    if (NS_FAILED(rv)) {
      selectedCertBytes.Clear();
      mSelectClientAuthCertificate->DispatchContinuation(
          std::move(selectedCertBytes));
      return rv;
    }
  }
  mSelectClientAuthCertificate->DispatchContinuation(
      std::move(selectedCertBytes));
  return NS_OK;
}

NS_IMETHODIMP
SelectClientAuthCertificate::Run() {
  // We check the value of a pref, so this should only be run on the main
  // thread.
  MOZ_ASSERT(NS_IsMainThread());

  nsTArray<uint8_t> selectedCertBytes;

  // find valid user cert and key pair
  if (nsGetUserCertChoice() == UserCertChoice::Auto) {
    // automatically find the right cert
    UniqueCERTCertificate lowPrioNonrepCert;
    // loop through the list until we find a cert with a key
    for (CERTCertListNode* node = CERT_LIST_HEAD(mPotentialClientCertificates);
         !CERT_LIST_END(node, mPotentialClientCertificates);
         node = CERT_LIST_NEXT(node)) {
      UniqueSECKEYPrivateKey tmpKey(PK11_FindKeyByAnyCert(node->cert, nullptr));
      if (tmpKey) {
        if (hasExplicitKeyUsageNonRepudiation(node->cert)) {
          // Not a preferred cert
          if (!lowPrioNonrepCert) {  // did not yet find a low prio cert
            lowPrioNonrepCert.reset(CERT_DupCertificate(node->cert));
          }
        } else {
          // this is a good cert to present
          selectedCertBytes.AppendElements(node->cert->derCert.data,
                                           node->cert->derCert.len);
          DispatchContinuation(std::move(selectedCertBytes));
          return NS_OK;
        }
      }
      if (PR_GetError() == SEC_ERROR_BAD_PASSWORD) {
        // problem with password: bail
        break;
      }
    }

    if (lowPrioNonrepCert) {
      selectedCertBytes.AppendElements(lowPrioNonrepCert->derCert.data,
                                       lowPrioNonrepCert->derCert.len);
    }
    DispatchContinuation(std::move(selectedCertBytes));
    return NS_OK;
  }

  // Not Auto => ask the user to select a certificate
  nsTArray<RefPtr<nsIX509Cert>> certArray;
  for (CERTCertListNode* node = CERT_LIST_HEAD(mPotentialClientCertificates);
       !CERT_LIST_END(node, mPotentialClientCertificates);
       node = CERT_LIST_NEXT(node)) {
    RefPtr<nsIX509Cert> tempCert(new nsNSSCertificate(node->cert));
    certArray.AppendElement(tempCert);
  }

  nsCOMPtr<nsIClientAuthDialogService> clientAuthDialogService(
      do_GetService(NS_CLIENTAUTHDIALOGSERVICE_CONTRACTID));
  if (!clientAuthDialogService) {
    DispatchContinuation(std::move(selectedCertBytes));
    return NS_ERROR_FAILURE;
  }

  RefPtr<mozilla::dom::BrowsingContext> browsingContext;
  if (mBrowserId) {
    browsingContext =
        mozilla::dom::BrowsingContext::GetCurrentTopByBrowserId(mBrowserId);
  }

  // Prevent HTTPS-Only/-First from downgrading the load in the browsing context
  // while the dialog is open by setting HTTPS_ONLY_TOP_LEVEL_LOAD_IN_PROGRESS
  // early. Otherwise this would only get set once
  // DocumentLoadListener::DoOnStartRequest is reached.
  if (browsingContext) {
    RefPtr<net::DocumentLoadListener> loadListener =
        browsingContext->Canonical()->GetCurrentLoad();
    if (loadListener) {
      nsCOMPtr<nsIHttpChannel> channel =
          do_QueryInterface(loadListener->GetChannel());
      if (channel) {
        nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
        uint32_t httpsOnlyStatus = loadInfo->GetHttpsOnlyStatus();
        httpsOnlyStatus |= nsILoadInfo::HTTPS_ONLY_TOP_LEVEL_LOAD_IN_PROGRESS;
        loadInfo->SetHttpsOnlyStatus(httpsOnlyStatus);
      }
    }
  }

  RefPtr<nsIClientAuthDialogCallback> callback(
      new ClientAuthDialogCallback(this));
  nsresult rv = clientAuthDialogService->ChooseCertificate(
      mInfo.HostName(), certArray, browsingContext, mCANames, callback);
  if (NS_FAILED(rv)) {
    DispatchContinuation(std::move(selectedCertBytes));
    return rv;
  }
  return NS_OK;
}

SECStatus SSLGetClientAuthDataHook(void* arg, PRFileDesc* socket,
                                   CERTDistNames* caNamesDecoded,
                                   CERTCertificate** pRetCert,
                                   SECKEYPrivateKey** pRetKey) {
  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("[%p][%p] SSLGetClientAuthDataHook", socket, arg));

  if (!arg || !socket || !caNamesDecoded || !pRetCert || !pRetKey) {
    PR_SetError(PR_INVALID_ARGUMENT_ERROR, 0);
    return SECFailure;
  }

  *pRetCert = nullptr;
  *pRetKey = nullptr;

  RefPtr<NSSSocketControl> info(static_cast<NSSSocketControl*>(arg));
  glean::security::client_auth_cert_usage.Get("requested"_ns).Add(1);

  if (info->GetDenyClientCert()) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("[%p] Not returning client cert due to denyClientCert attribute",
             socket));
    return SECSuccess;
  }

  if (info->GetJoined()) {
    // We refuse to send a client certificate when there are multiple hostnames
    // joined on this connection, because we only show the user one hostname
    // (mHostName) in the client certificate UI.
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("[%p] Not returning client cert due to previous join", socket));
    return SECSuccess;
  }

  // If the connection corresponding to this socket hasn't been claimed, it is
  // a speculative connection. The connection will block until the "choose a
  // client auth certificate" dialog has been shown. The dialog will only be
  // shown when this connection gets claimed. However, necko will never claim
  // the connection as long as it is blocking. Thus, this connection can't
  // proceed, so it's best to cancel it. Necko will create a new,
  // non-speculative connection instead.
  if (info->CancelIfNotClaimed()) {
    MOZ_LOG(
        gPIPNSSLog, LogLevel::Debug,
        ("[%p] Cancelling unclaimed connection with client certificate request",
         socket));
    return SECSuccess;
  }

  UniqueCERTCertificate serverCert(SSL_PeerCertificate(socket));
  if (!serverCert) {
    PR_SetError(SSL_ERROR_NO_CERTIFICATE, 0);
    return SECFailure;
  }

  nsTArray<nsTArray<uint8_t>> caNames(CollectCANames(caNamesDecoded));
  info->SetClientAuthCertificateRequest(std::move(serverCert),
                                        std::move(caNames));
  PR_SetError(PR_WOULD_BLOCK_ERROR, 0);
  return SECWouldBlock;
}

void DoSelectClientAuthCertificate(NSSSocketControl* info,
                                   UniqueCERTCertificate&& serverCert,
                                   nsTArray<nsTArray<uint8_t>>&& caNames) {
  MOZ_ASSERT(info);
  uint64_t browserId;
  if (NS_FAILED(info->GetBrowserId(&browserId))) {
    info->SetCanceled(SEC_ERROR_LIBRARY_FAILURE);
    return;
  }

  RefPtr<ClientAuthCertificateSelected> continuation(
      new ClientAuthCertificateSelected(info));
  // If this is the socket process, dispatch an IPC call to select a client
  // authentication certificate in the parent process.
  // Otherwise, dispatch an event to the main thread to do the selection.
  // When those events finish, they will run the continuation, which gives the
  // appropriate information to the NSSSocketControl, which then calls
  // SSL_ClientCertCallbackComplete to continue the connection.
  if (XRE_IsSocketProcess()) {
    RefPtr<SelectTLSClientAuthCertChild> selectClientAuthCertificate(
        new SelectTLSClientAuthCertChild(continuation));
    nsAutoCString hostname(info->GetHostName());
    nsTArray<uint8_t> serverCertBytes;
    nsTArray<ByteArray> caNamesBytes;
    for (const auto& caName : caNames) {
      caNamesBytes.AppendElement(ByteArray(std::move(caName)));
    }
    serverCertBytes.AppendElements(serverCert->derCert.data,
                                   serverCert->derCert.len);
    OriginAttributes originAttributes(info->GetOriginAttributes());
    int32_t port(info->GetPort());
    uint32_t providerFlags(info->GetProviderFlags());
    uint32_t providerTlsFlags(info->GetProviderTlsFlags());
    nsCOMPtr<nsIRunnable> remoteSelectClientAuthCertificate(
        NS_NewRunnableFunction(
            "RemoteSelectClientAuthCertificate",
            [selectClientAuthCertificate(
                 std::move(selectClientAuthCertificate)),
             hostname(std::move(hostname)),
             originAttributes(std::move(originAttributes)), port, providerFlags,
             providerTlsFlags, serverCertBytes(std::move(serverCertBytes)),
             caNamesBytes(std::move(caNamesBytes)),
             browserId(browserId)]() mutable {
              ipc::Endpoint<PSelectTLSClientAuthCertParent> parentEndpoint;
              ipc::Endpoint<PSelectTLSClientAuthCertChild> childEndpoint;
              PSelectTLSClientAuthCert::CreateEndpoints(&parentEndpoint,
                                                        &childEndpoint);
              if (NS_FAILED(net::SocketProcessBackgroundChild::WithActor(
                      "SendInitSelectTLSClientAuthCert",
                      [endpoint = std::move(parentEndpoint),
                       hostname(std::move(hostname)),
                       originAttributes(std::move(originAttributes)), port,
                       providerFlags, providerTlsFlags,
                       serverCertBytes(std::move(serverCertBytes)),
                       caNamesBytes(std::move(caNamesBytes)), browserId](
                          net::SocketProcessBackgroundChild* aActor) mutable {
                        (void)aActor->SendInitSelectTLSClientAuthCert(
                            std::move(endpoint), hostname, originAttributes,
                            port, providerFlags, providerTlsFlags,
                            ByteArray(serverCertBytes), caNamesBytes,
                            browserId);
                      }))) {
                return;
              }

              if (!childEndpoint.Bind(selectClientAuthCertificate)) {
                return;
              }
            }));
    (void)NS_DispatchToMainThread(remoteSelectClientAuthCertificate);
    return;
  }

  ClientAuthInfo authInfo(info->GetHostName(), info->GetOriginAttributes(),
                          info->GetPort(), info->GetProviderFlags(),
                          info->GetProviderTlsFlags());
  nsTArray<nsTArray<uint8_t>> enterpriseCertificates(
      GetEnterpriseCertificates());
  nsTArray<uint8_t> rememberedCertBytes;
  nsTArray<nsTArray<uint8_t>> rememberedCertChainBytes;
  if (FindRememberedDecision(authInfo, caNames, enterpriseCertificates,
                             rememberedCertBytes, rememberedCertChainBytes)) {
    continuation->SetSelectedClientAuthData(
        std::move(rememberedCertBytes), std::move(rememberedCertChainBytes));
    (void)continuation->Run();
    return;
  }

  // Instantiating certificates in NSS is not thread-safe and has performance
  // implications, so search for them here (on the socket thread).
  UniqueCERTCertList potentialClientCertificates(
      FindClientCertificatesWithPrivateKeys());
  if (!potentialClientCertificates) {
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("[%p] FindClientCertificatesWithPrivateKeys() returned null (out "
             "of memory?)",
             &info));
    info->SetCanceled(SEC_ERROR_LIBRARY_FAILURE);
    return;
  }

  nsTArray<nsTArray<nsTArray<uint8_t>>> potentialClientCertificateChains;

  // On Android, gathering potential client certificates and filtering them by
  // issuer is handled by the OS, so `potentialClientCertificates` is expected
  // to be empty here.
#ifndef MOZ_WIDGET_ANDROID
  FilterPotentialClientCertificatesByCANames(potentialClientCertificates,
                                             caNames, enterpriseCertificates,
                                             potentialClientCertificateChains);
  if (CERT_LIST_EMPTY(potentialClientCertificates)) {
    MOZ_LOG(
        gPIPNSSLog, LogLevel::Debug,
        ("[%p] no client certificates available after filtering by CA", &info));
    // By default, the continuation will continue the connection with no client
    // auth certificate.
    (void)continuation->Run();
    return;
  }
#endif  // MOZ_WIDGET_ANDROID

  nsCOMPtr<nsIRunnable> selectClientAuthCertificate(
      new SelectClientAuthCertificate(
          std::move(authInfo), std::move(serverCert),
          std::move(potentialClientCertificates),
          std::move(potentialClientCertificateChains), std::move(caNames),
          continuation, browserId));
  (void)NS_DispatchToMainThread(selectClientAuthCertificate);
}

// Helper continuation for when a client authentication certificate has been
// selected in the parent process and the information needs to be sent to the
// socket process.
class RemoteClientAuthCertificateSelected
    : public ClientAuthCertificateSelectedBase {
 public:
  explicit RemoteClientAuthCertificateSelected(
      SelectTLSClientAuthCertParent* selectTLSClientAuthCertParent)
      : mSelectTLSClientAuthCertParent(selectTLSClientAuthCertParent),
        mEventTarget(GetCurrentSerialEventTarget()) {}

  NS_IMETHOD Run() override;

 private:
  RefPtr<SelectTLSClientAuthCertParent> mSelectTLSClientAuthCertParent;
  nsCOMPtr<nsISerialEventTarget> mEventTarget;
};

NS_IMETHODIMP
RemoteClientAuthCertificateSelected::Run() {
  // When this runs, it dispatches an event to the IPC thread it originally came
  // from in order to send the IPC call to the socket process that a client
  // authentication certificate has been selected.
  return mEventTarget->Dispatch(
      NS_NewRunnableFunction(
          "psm::RemoteClientAuthCertificateSelected::Run",
          [parent(mSelectTLSClientAuthCertParent),
           certBytes(std::move(mSelectedCertBytes)),
           builtCertChain(std::move(mSelectedCertChainBytes))]() mutable {
            parent->TLSClientAuthCertSelected(certBytes,
                                              std::move(builtCertChain));
          }),
      NS_DISPATCH_NORMAL);
}

namespace mozilla::psm {

// Given some information from the socket process about a connection that
// requested a client authentication certificate, this function dispatches an
// event to the main thread to ask the user to select one. When the user does so
// (or selects no certificate), the continuation runs and sends the information
// back via IPC.
bool SelectTLSClientAuthCertParent::Dispatch(
    const nsACString& aHostName, const OriginAttributes& aOriginAttributes,
    const int32_t& aPort, const uint32_t& aProviderFlags,
    const uint32_t& aProviderTlsFlags, const ByteArray& aServerCertBytes,
    nsTArray<ByteArray>&& aCANames, const uint64_t& aBrowserId) {
  RefPtr<ClientAuthCertificateSelectedBase> continuation(
      new RemoteClientAuthCertificateSelected(this));
  ClientAuthInfo authInfo(aHostName, aOriginAttributes, aPort, aProviderFlags,
                          aProviderTlsFlags);
  nsCOMPtr<nsIEventTarget> socketThread =
      do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID);
  if (NS_WARN_IF(!socketThread)) {
    return false;
  }
  // Dispatch the work of instantiating a CERTCertificate and searching for
  // client certificates to the socket thread.
  nsresult rv = socketThread->Dispatch(NS_NewRunnableFunction(
      "SelectTLSClientAuthCertParent::Dispatch",
      [authInfo(std::move(authInfo)), continuation(std::move(continuation)),
       serverCertBytes(aServerCertBytes), caNames(std::move(aCANames)),
       browserId(aBrowserId)]() mutable {
        SECItem serverCertItem{
            siBuffer,
            const_cast<uint8_t*>(serverCertBytes.data().Elements()),
            static_cast<unsigned int>(serverCertBytes.data().Length()),
        };
        UniqueCERTCertificate serverCert(CERT_NewTempCertificate(
            CERT_GetDefaultCertDB(), &serverCertItem, nullptr, false, true));
        if (!serverCert) {
          return;
        }
        nsTArray<nsTArray<uint8_t>> caNamesArray;
        for (auto& caName : caNames) {
          caNamesArray.AppendElement(std::move(caName.data()));
        }
        nsTArray<nsTArray<uint8_t>> enterpriseCertificates(
            GetEnterpriseCertificates());
        nsTArray<uint8_t> rememberedCertBytes;
        nsTArray<nsTArray<uint8_t>> rememberedCertChainBytes;
        if (FindRememberedDecision(authInfo, caNamesArray,
                                   enterpriseCertificates, rememberedCertBytes,
                                   rememberedCertChainBytes)) {
          continuation->SetSelectedClientAuthData(
              std::move(rememberedCertBytes),
              std::move(rememberedCertChainBytes));
          (void)NS_DispatchToCurrentThread(continuation);
          return;
        }
        UniqueCERTCertList potentialClientCertificates(
            FindClientCertificatesWithPrivateKeys());
        nsTArray<nsTArray<nsTArray<uint8_t>>> potentialClientCertificateChains;
        FilterPotentialClientCertificatesByCANames(
            potentialClientCertificates, caNamesArray, enterpriseCertificates,
            potentialClientCertificateChains);
        RefPtr<SelectClientAuthCertificate> selectClientAuthCertificate(
            new SelectClientAuthCertificate(
                std::move(authInfo), std::move(serverCert),
                std::move(potentialClientCertificates),
                std::move(potentialClientCertificateChains),
                std::move(caNamesArray), continuation, browserId));
        (void)NS_DispatchToMainThread(selectClientAuthCertificate);
      }));
  return NS_SUCCEEDED(rv);
}

void SelectTLSClientAuthCertParent::TLSClientAuthCertSelected(
    const nsTArray<uint8_t>& aSelectedCertBytes,
    nsTArray<nsTArray<uint8_t>>&& aSelectedCertChainBytes) {
  if (!CanSend()) {
    return;
  }

  nsTArray<ByteArray> selectedCertChainBytes;
  for (auto& certBytes : aSelectedCertChainBytes) {
    selectedCertChainBytes.AppendElement(ByteArray(certBytes));
  }

  (void)SendTLSClientAuthCertSelected(aSelectedCertBytes,
                                      selectedCertChainBytes);
  Close();
}

void SelectTLSClientAuthCertParent::ActorDestroy(
    mozilla::ipc::IProtocol::ActorDestroyReason aWhy) {}

SelectTLSClientAuthCertChild::SelectTLSClientAuthCertChild(
    ClientAuthCertificateSelected* continuation)
    : mContinuation(continuation) {}

// When the user has selected (or not) a client authentication certificate in
// the parent, this function receives that information in the socket process and
// dispatches a continuation to the socket process to continue the connection.
ipc::IPCResult SelectTLSClientAuthCertChild::RecvTLSClientAuthCertSelected(
    ByteArray&& aSelectedCertBytes,
    nsTArray<ByteArray>&& aSelectedCertChainBytes) {
  nsTArray<uint8_t> selectedCertBytes(std::move(aSelectedCertBytes.data()));
  nsTArray<nsTArray<uint8_t>> selectedCertChainBytes;
  for (auto& certBytes : aSelectedCertChainBytes) {
    selectedCertChainBytes.AppendElement(std::move(certBytes.data()));
  }
  mContinuation->SetSelectedClientAuthData(std::move(selectedCertBytes),
                                           std::move(selectedCertChainBytes));

  nsCOMPtr<nsIEventTarget> socketThread =
      do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID);
  if (NS_WARN_IF(!socketThread)) {
    return IPC_OK();
  }
  nsresult rv = socketThread->Dispatch(mContinuation, NS_DISPATCH_NORMAL);
  (void)NS_WARN_IF(NS_FAILED(rv));

  return IPC_OK();
}

}  // namespace mozilla::psm
