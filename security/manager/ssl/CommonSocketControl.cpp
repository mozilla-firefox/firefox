/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CommonSocketControl.h"

#include "PublicKeyPinningService.h"
#include "SharedCertVerifier.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/dom/Promise.h"
#include "nsICertOverrideService.h"
#include "nsISocketProvider.h"
#include "nsITlsHandshakeListener.h"
#include "nsNSSCertificate.h"
#include "nsNSSComponent.h"
#include "nsNetCID.h"
#include "secerr.h"
#include "ssl.h"
#include "sslt.h"

using namespace mozilla;

extern LazyLogModule gPIPNSSLog;

NS_IMPL_ISUPPORTS(CommonSocketControl, nsITLSSocketControl)

CommonSocketControl::CommonSocketControl(const nsCString& aHostName,
                                         int32_t aPort, uint32_t aProviderFlags)
    : mHostName(aHostName),
      mPort(aPort),
      mCanceled(false),
      mHandshakeCompleted(false),
      mJoined(false),
      mSentClientCert(false),
      mFailedVerification(false),
      mSSLVersionUsed(nsITLSSocketControl::SSL_VERSION_UNKNOWN),
      mProviderFlags(aProviderFlags),
      mSecurityState(0),
      mErrorCode(0),
      mServerCert(nullptr),
      mCertificateTransparencyStatus(0),
      mMadeOCSPRequests(false),
      mUsedPrivateDNS(aProviderFlags & nsISocketProvider::USED_PRIVATE_DNS),
      mNPNCompleted(false),
      mResumed(false),
      mIsBuiltCertChainRootBuiltInRoot(false) {
#if defined(MOZ_DIAGNOSTIC_ASSERT_ENABLED)
  mOwningThread = PR_GetCurrentThread();
#endif
}

void CommonSocketControl::SetStatusErrorBits(
    nsITransportSecurityInfo::OverridableErrorCategory
        overridableErrorCategory) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mOverridableErrorCategory = Some(overridableErrorCategory);
}

static void CreateCertChain(nsTArray<RefPtr<nsIX509Cert>>& aOutput,
                            nsTArray<nsTArray<uint8_t>>&& aCertList) {
  nsTArray<nsTArray<uint8_t>> certList = std::move(aCertList);
  aOutput.Clear();
  for (auto& certBytes : certList) {
    RefPtr cert = MakeRefPtr<nsNSSCertificate>(std::move(certBytes));
    aOutput.AppendElement(std::move(cert));
  }
}

void CommonSocketControl::SetServerCert(
    const nsCOMPtr<nsIX509Cert>& aServerCert,
    mozilla::psm::EVStatus aEVStatus) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mServerCert = aServerCert;
  mIsEV = Some(aEVStatus == mozilla::psm::EVStatus::EV);
}

void CommonSocketControl::SetSucceededCertChain(
    nsTArray<nsTArray<uint8_t>>&& aCertList) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  return CreateCertChain(mSucceededCertChain, std::move(aCertList));
}

void CommonSocketControl::SetHandshakeCertificates(
    nsTArray<nsTArray<uint8_t>>&& aCertList) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  return CreateCertChain(mHandshakeCertificates, std::move(aCertList));
}

void CommonSocketControl::SetCanceled(PRErrorCode errorCode) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  MOZ_ASSERT(errorCode != 0);
  if (errorCode == 0) {
    errorCode = SEC_ERROR_LIBRARY_FAILURE;
  }

  mErrorCode = errorCode;
  mCanceled = true;
}

// NB: GetErrorCode may be called before an error code is set (if ever). In that
// case, this returns 0, which is treated as a successful value.
int32_t CommonSocketControl::GetErrorCode() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  // We're in an inconsistent state if we think we've been canceled but no error
  // code was set or we haven't been canceled but an error code was set.
  MOZ_ASSERT(
      !((mCanceled && mErrorCode == 0) || (!mCanceled && mErrorCode != 0)));
  if ((mCanceled && mErrorCode == 0) || (!mCanceled && mErrorCode != 0)) {
    mCanceled = true;
    mErrorCode = SEC_ERROR_LIBRARY_FAILURE;
  }

  return mErrorCode;
}

NS_IMETHODIMP
CommonSocketControl::ProxyStartSSL(void) { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
CommonSocketControl::StartTLS(void) { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
CommonSocketControl::AsyncStartTLS(JSContext* aCx,
                                   mozilla::dom::Promise** aPromise) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CommonSocketControl::SetNPNList(nsTArray<nsCString>& aNPNList) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CommonSocketControl::GetAlpnEarlySelection(nsACString& _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CommonSocketControl::GetEarlyDataAccepted(bool* aEarlyDataAccepted) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CommonSocketControl::GetResumptionTokenPresent(bool* aPresent) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  *aPresent = mSessionCacheInfo.isSome();
  return NS_OK;
}

NS_IMETHODIMP
CommonSocketControl::DriveHandshake(void) { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
CommonSocketControl::JoinConnection(const nsACString& npnProtocol,
                                    const nsACString& hostname, int32_t port,
                                    bool* _retval) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  nsresult rv = TestJoinConnection(npnProtocol, hostname, port, _retval);
  if (NS_SUCCEEDED(rv) && *_retval) {
    // All tests pass - this is joinable
    mJoined = true;
  }
  return rv;
}

NS_IMETHODIMP
CommonSocketControl::TestJoinConnection(const nsACString& npnProtocol,
                                        const nsACString& hostname,
                                        int32_t port, bool* _retval) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  *_retval = false;

  // Different ports may not be joined together
  if (port != GetPort()) return NS_OK;

  // Make sure NPN has been completed and matches requested npnProtocol
  if (!mNPNCompleted || !mNegotiatedNPN.Equals(npnProtocol)) {
    return NS_OK;
  }

  IsAcceptableForHost(hostname, _retval);  // sets _retval
  return NS_OK;
}

NS_IMETHODIMP
CommonSocketControl::IsAcceptableForHost(const nsACString& hostname,
                                         bool* _retval) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  NS_ENSURE_ARG(_retval);

  *_retval = false;

  // If this is the same hostname then the certicate status does not
  // need to be considered. They are joinable.
  if (hostname.Equals(GetHostName())) {
    *_retval = true;
    return NS_OK;
  }

  // Before checking the server certificate we need to make sure the
  // handshake has completed.
  if (!mHandshakeCompleted || !HasServerCert()) {
    return NS_OK;
  }

  // Security checks can only be skipped when running xpcshell tests.
  if (PR_GetEnv("XPCSHELL_TEST_PROFILE_DIR")) {
    nsCOMPtr<nsICertOverrideService> overrideService =
        do_GetService(NS_CERTOVERRIDE_CONTRACTID);
    if (overrideService) {
      bool securityCheckDisabled = false;
      overrideService->GetSecurityCheckDisabled(&securityCheckDisabled);
      if (securityCheckDisabled) {
        *_retval = true;
        return NS_OK;
      }
    }
  }

  // If the cert has error bits (e.g. it is untrusted) then do not join.
  if (mOverridableErrorCategory.isSome()) {
    return NS_OK;
  }

  // If the connection is using client certificates then do not join
  // because the user decides on whether to send client certs to hosts on a
  // per-domain basis.
  if (mSentClientCert) return NS_OK;

  // Ensure that the server certificate covers the hostname that would
  // like to join this connection

  nsCOMPtr<nsIX509Cert> cert(GetServerCert());
  if (!cert) {
    return NS_OK;
  }
  nsTArray<uint8_t> certDER;
  if (NS_FAILED(cert->GetRawDER(certDER))) {
    return NS_OK;
  }

  // An empty mSucceededCertChain means the server certificate verification
  // failed before, so don't join in this case.
  if (mSucceededCertChain.IsEmpty()) {
    return NS_OK;
  }

  // See where CheckCertHostname() is called in
  // CertVerifier::VerifySSLServerCert. We are doing the same hostname-specific
  // checks here. If any hostname-specific checks are added to
  // CertVerifier::VerifySSLServerCert we need to add them here too.
  pkix::Input serverCertInput;
  mozilla::pkix::Result rv =
      serverCertInput.Init(certDER.Elements(), certDER.Length());
  if (rv != pkix::Success) {
    return NS_OK;
  }

  pkix::Input hostnameInput;
  rv = hostnameInput.Init(
      BitwiseCast<const uint8_t*, const char*>(hostname.BeginReading()),
      hostname.Length());
  if (rv != pkix::Success) {
    return NS_OK;
  }

  rv = CheckCertHostname(serverCertInput, hostnameInput);
  if (rv != pkix::Success) {
    return NS_OK;
  }

  nsTArray<nsTArray<uint8_t>> rawDerCertList;
  nsTArray<Span<const uint8_t>> derCertSpanList;
  for (const auto& cert : mSucceededCertChain) {
    rawDerCertList.EmplaceBack();
    nsresult nsrv = cert->GetRawDER(rawDerCertList.LastElement());
    if (NS_FAILED(nsrv)) {
      return nsrv;
    }
    derCertSpanList.EmplaceBack(rawDerCertList.LastElement());
  }
  bool chainHasValidPins;
  nsresult nsrv = mozilla::psm::PublicKeyPinningService::ChainHasValidPins(
      derCertSpanList, PromiseFlatCString(hostname).get(), pkix::Now(),
      mIsBuiltCertChainRootBuiltInRoot, chainHasValidPins, nullptr);
  if (NS_FAILED(nsrv)) {
    return NS_OK;
  }

  if (!chainHasValidPins) {
    return NS_OK;
  }

  // All tests pass
  *_retval = true;
  return NS_OK;
}

void CommonSocketControl::RebuildCertificateInfoFromSSLTokenCache() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  if (!mSessionCacheInfo) {
    MOZ_LOG(
        gPIPNSSLog, LogLevel::Debug,
        ("CommonSocketControl::RebuildCertificateInfoFromSSLTokenCache cannot "
         "find cached info."));
    return;
  }

  mozilla::net::SessionCacheInfo& info = *mSessionCacheInfo;
  nsCOMPtr<nsIX509Cert> cert(
      new nsNSSCertificate(std::move(info.mServerCertBytes)));
  SetServerCert(cert, info.mEVStatus);
  if (info.mOverridableErrorCategory !=
      nsITransportSecurityInfo::OverridableErrorCategory::ERROR_UNSET) {
    SetStatusErrorBits(info.mOverridableErrorCategory);
  }
  SetCertificateTransparencyStatus(info.mCertificateTransparencyStatus);
  if (info.mSucceededCertChainBytes) {
    SetSucceededCertChain(std::move(*info.mSucceededCertChainBytes));
  }

  if (info.mIsBuiltCertChainRootBuiltInRoot) {
    SetIsBuiltCertChainRootBuiltInRoot(*info.mIsBuiltCertChainRootBuiltInRoot);
  }

  if (info.mHandshakeCertificatesBytes) {
    SetHandshakeCertificates(std::move(*info.mHandshakeCertificatesBytes));
  }
}

NS_IMETHODIMP
CommonSocketControl::GetKEAUsed(int16_t* aKEAUsed) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CommonSocketControl::GetKEAKeyBits(uint32_t* aKEAKeyBits) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CommonSocketControl::GetProviderFlags(uint32_t* aProviderFlags) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  *aProviderFlags = mProviderFlags;
  return NS_OK;
}

NS_IMETHODIMP
CommonSocketControl::GetSSLVersionUsed(int16_t* aSSLVersionUsed) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  *aSSLVersionUsed = mSSLVersionUsed;
  return NS_OK;
}

NS_IMETHODIMP
CommonSocketControl::GetSSLVersionOffered(int16_t* aSSLVersionOffered) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CommonSocketControl::GetMACAlgorithmUsed(int16_t* aMACAlgorithmUsed) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

bool CommonSocketControl::GetDenyClientCert() { return true; }

void CommonSocketControl::SetDenyClientCert(bool aDenyClientCert) {}

NS_IMETHODIMP
CommonSocketControl::GetClientCertSent(bool* arg) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  *arg = mSentClientCert;
  return NS_OK;
}

NS_IMETHODIMP
CommonSocketControl::GetFailedVerification(bool* arg) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  *arg = mFailedVerification;
  return NS_OK;
}

NS_IMETHODIMP
CommonSocketControl::GetEsniTxt(nsACString& aEsniTxt) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CommonSocketControl::SetEsniTxt(const nsACString& aEsniTxt) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CommonSocketControl::GetEchConfig(nsACString& aEchConfig) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CommonSocketControl::SetEchConfig(const nsACString& aEchConfig) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CommonSocketControl::GetRetryEchConfig(nsACString& aEchConfig) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CommonSocketControl::SetHandshakeCallbackListener(
    nsITlsHandshakeCallbackListener* callback) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CommonSocketControl::DisableEarlyData(void) { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
CommonSocketControl::GetPeerId(nsACString& aResult) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  if (!mPeerId.IsEmpty()) {
    aResult.Assign(mPeerId);
    return NS_OK;
  }

  if (mProviderFlags &
      nsISocketProvider::ANONYMOUS_CONNECT) {  // See bug 466080
    mPeerId.AppendLiteral("anon:");
  }
  if (mProviderFlags & nsISocketProvider::NO_PERMANENT_STORAGE) {
    mPeerId.AppendLiteral("private:");
  }
  if (mProviderFlags & nsISocketProvider::BE_CONSERVATIVE) {
    mPeerId.AppendLiteral("beConservative:");
  }

  mPeerId.Append(mHostName);
  mPeerId.Append(':');
  mPeerId.AppendInt(GetPort());
  nsAutoCString suffix;
  mOriginAttributes.CreateSuffix(suffix);
  mPeerId.Append(suffix);

  aResult.Assign(mPeerId);
  return NS_OK;
}

NS_IMETHODIMP
CommonSocketControl::GetSecurityInfo(nsITransportSecurityInfo** aSecurityInfo) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  // Make sure peerId is set.
  nsAutoCString unused;
  nsresult rv = GetPeerId(unused);
  if (NS_FAILED(rv)) {
    return rv;
  }
  nsCOMPtr<nsITransportSecurityInfo> securityInfo(
      new psm::TransportSecurityInfo(
          mSecurityState, mErrorCode, mHandshakeCertificates.Clone(),
          mServerCert, mSucceededCertChain.Clone(), mCipherSuite, mKeaGroupName,
          mSignatureSchemeName, mProtocolVersion,
          mCertificateTransparencyStatus, mIsAcceptedEch,
          mIsDelegatedCredential, mOverridableErrorCategory, mMadeOCSPRequests,
          mUsedPrivateDNS, mIsEV, mNPNCompleted, mNegotiatedNPN, mResumed,
          mIsBuiltCertChainRootBuiltInRoot, mPeerId));
  securityInfo.forget(aSecurityInfo);
  return NS_OK;
}

NS_IMETHODIMP
CommonSocketControl::AsyncGetSecurityInfo(JSContext* aCx,
                                          mozilla::dom::Promise** aPromise) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  NS_ENSURE_ARG_POINTER(aCx);
  NS_ENSURE_ARG_POINTER(aPromise);

  nsIGlobalObject* globalObject = xpc::CurrentNativeGlobal(aCx);
  if (!globalObject) {
    return NS_ERROR_UNEXPECTED;
  }

  ErrorResult result;
  RefPtr<mozilla::dom::Promise> promise =
      mozilla::dom::Promise::Create(globalObject, result);
  if (result.Failed()) {
    return result.StealNSResult();
  }
  nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(
      "CommonSocketControl::AsyncGetSecurityInfo",
      [promise, self = RefPtr{this}]() mutable {
        nsCOMPtr<nsITransportSecurityInfo> securityInfo;
        nsresult rv = self->GetSecurityInfo(getter_AddRefs(securityInfo));
        nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(
            "CommonSocketControl::AsyncGetSecurityInfoResolve",
            [rv, promise = std::move(promise),
             securityInfo = std::move(securityInfo)]() {
              if (NS_FAILED(rv)) {
                promise->MaybeReject(rv);
              } else {
                promise->MaybeResolve(securityInfo);
              }
            }));
        NS_DispatchToMainThread(runnable.forget());
      }));
  nsCOMPtr<nsIEventTarget> target(
      do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID));
  if (!target) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv = target->Dispatch(runnable, NS_DISPATCH_NORMAL);
  if (NS_FAILED(rv)) {
    return rv;
  }

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP CommonSocketControl::Claim() { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP CommonSocketControl::SetBrowserId(uint64_t) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP CommonSocketControl::GetBrowserId(uint64_t*) {
  return NS_ERROR_NOT_IMPLEMENTED;
}
