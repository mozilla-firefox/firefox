/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NSSSocketControl.h"

#include "mozilla/Base64.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/glean/SecurityManagerSslMetrics.h"
#include "nsISocketProvider.h"
#include "nsNSSCallbacks.h"
#include "nsNSSComponent.h"
#include "nsNetCID.h"
#include "nsProxyRelease.h"
#include "secerr.h"
#include "ssl.h"
#include "sslexp.h"

using namespace mozilla;
using namespace mozilla::psm;

extern LazyLogModule gPIPNSSLog;

NSSSocketControl::NSSSocketControl(
    const nsCString& aHostName, int32_t aPort,
    already_AddRefed<nsSSLIOLayerHelpers> aSSLIOLayerHelpers,
    uint32_t providerFlags, uint32_t providerTlsFlags)
    : CommonSocketControl(aHostName, aPort, providerFlags),
      mFd(nullptr),
      mCertVerificationState(BeforeCertVerification),
      mSSLIOLayerHelpers(aSSLIOLayerHelpers),
      mForSTARTTLS(false),
      mTLSVersionRange{0, 0},
      mHandshakePending(true),
      mPreliminaryHandshakeDone(false),
      mEarlyDataAccepted(false),
      mDenyClientCert(false),
      mFalseStartCallbackCalled(false),
      mFalseStarted(false),
      mIsFullHandshake(false),
      mDrewResumptionToken(false),
      mNotedTimeUntilReady(false),
      mEchExtensionStatus(EchExtensionStatus::kNotPresent),
      mMaybeShortWrite(Nothing()),
      mKEAUsed(nsITLSSocketControl::KEY_EXCHANGE_UNKNOWN),
      mKEAKeyBits(0),
      mMACAlgorithmUsed(nsITLSSocketControl::SSL_MAC_UNKNOWN),
      mProviderTlsFlags(providerTlsFlags),
      mSocketCreationTimestamp(TimeStamp::Now()),
      mPlaintextBytesRead(0),
      mClaimed(!(providerFlags & nsISocketProvider::IS_SPECULATIVE_CONNECTION)),
      mClientAuthCertificateRequest(Nothing()),
      mBrowserId(0) {}

NS_IMETHODIMP
NSSSocketControl::GetKEAUsed(int16_t* aKea) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  *aKea = mKEAUsed;
  return NS_OK;
}

NS_IMETHODIMP
NSSSocketControl::GetKEAKeyBits(uint32_t* aKeyBits) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  *aKeyBits = mKEAKeyBits;
  return NS_OK;
}

NS_IMETHODIMP
NSSSocketControl::GetSSLVersionOffered(int16_t* aSSLVersionOffered) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  *aSSLVersionOffered = mTLSVersionRange.max;
  return NS_OK;
}

NS_IMETHODIMP
NSSSocketControl::GetMACAlgorithmUsed(int16_t* aMac) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  *aMac = mMACAlgorithmUsed;
  return NS_OK;
}

void NSSSocketControl::NoteTimeUntilReady() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  if (mNotedTimeUntilReady) {
    return;
  }
  mNotedTimeUntilReady = true;

  auto duration = TimeStamp::Now() - mSocketCreationTimestamp;
  if (!(mProviderFlags & nsISocketProvider::IS_RETRY)) {
    glean::ssl::time_until_ready_first_try.AccumulateRawDuration(duration);
  }

  if (mProviderFlags & nsISocketProvider::BE_CONSERVATIVE) {
    glean::ssl::time_until_ready_conservative.AccumulateRawDuration(duration);
  }

  switch (GetEchExtensionStatus()) {
    case EchExtensionStatus::kGREASE:
      glean::ssl::time_until_ready_ech_grease.AccumulateRawDuration(duration);
      break;
    case EchExtensionStatus::kReal:
      glean::ssl::time_until_ready_ech.AccumulateRawDuration(duration);
      break;
    default:
      break;
  }
  // This will include TCP and proxy tunnel wait time
  glean::ssl::time_until_ready.AccumulateRawDuration(duration);

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("[%p] NSSSocketControl::NoteTimeUntilReady\n", mFd));
}

void NSSSocketControl::SetHandshakeCompleted() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  if (!mHandshakeCompleted) {
    // This will include TCP and proxy tunnel wait time
    if (mKeaGroupName.isSome()) {
      glean::ssl::time_until_handshake_finished_keyed_by_ka.Get(*mKeaGroupName)
          .AccumulateRawDuration(TimeStamp::Now() - mSocketCreationTimestamp);
    }

    // If the handshake is completed for the first time from just 1 callback
    // that means that TLS session resumption must have been used.
    glean::ssl::resumed_session
        .EnumGet(
            static_cast<glean::ssl::ResumedSessionLabel>(!IsFullHandshake()))
        .Add();

    if (mDrewResumptionToken) {
      glean::network::ssl_token_resumption_outcome
          .Get(mSessionCacheInfo.isNothing() ? "rejected"_ns
               : IsFullHandshake()           ? "not_resumed"_ns
                                             : "resumed"_ns)
          .Add();
    }

    using glean::tls_handshake::CompletedLabel;
    CompletedLabel handshakeType =
        !IsFullHandshake()          ? CompletedLabel::eResumed
        : mFalseStarted             ? CompletedLabel::eFalseStarted
        : mFalseStartCallbackCalled ? CompletedLabel::eFalseStartNotChosen
                                    : CompletedLabel::eFalseStartNotAllowed;
    glean::tls_handshake::completed.EnumGet(handshakeType).Add();
  }

  // Remove the plaintext layer as it is not needed anymore.
  // The plaintext layer is not always present - so it's not a fatal error if it
  // cannot be removed.
  // Note that PR_PopIOLayer may modify its stack, so a pointer returned by
  // PR_GetIdentitiesLayer may not point to what we think it points to after
  // calling PR_PopIOLayer. We must operate on the pointer returned by
  // PR_PopIOLayer.
  if (PR_GetIdentitiesLayer(mFd,
                            nsSSLIOLayerHelpers::nsSSLPlaintextLayerIdentity)) {
    PRFileDesc* poppedPlaintext =
        PR_PopIOLayer(mFd, nsSSLIOLayerHelpers::nsSSLPlaintextLayerIdentity);
    poppedPlaintext->dtor(poppedPlaintext);
  }

  mHandshakeCompleted = true;

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("[%p] NSSSocketControl::SetHandshakeCompleted\n", (void*)mFd));

  mIsFullHandshake = false;  // reset for next handshake on this connection

  if (mTlsHandshakeCallback) {
    auto callback = std::move(mTlsHandshakeCallback);
    (void)callback->HandshakeDone();
  }
}

void NSSSocketControl::SetNegotiatedNPN(const char* value, uint32_t length) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  if (!value) {
    mNegotiatedNPN.Truncate();
  } else {
    mNegotiatedNPN.Assign(value, length);
  }
  mNPNCompleted = true;
}

#define MAX_ALPN_LENGTH 255

NS_IMETHODIMP
NSSSocketControl::GetAlpnEarlySelection(nsACString& aAlpnSelected) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  aAlpnSelected.Truncate();

  SSLPreliminaryChannelInfo info;
  SECStatus rv = SSL_GetPreliminaryChannelInfo(mFd, &info, sizeof(info));
  if (rv != SECSuccess || !info.canSendEarlyData) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  SSLNextProtoState alpnState;
  unsigned char chosenAlpn[MAX_ALPN_LENGTH];
  unsigned int chosenAlpnLen;
  rv = SSL_GetNextProto(mFd, &alpnState, chosenAlpn, &chosenAlpnLen,
                        AssertedCast<unsigned int>(std::size(chosenAlpn)));

  if (rv != SECSuccess) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (alpnState == SSL_NEXT_PROTO_EARLY_VALUE) {
    aAlpnSelected.Assign(BitwiseCast<char*, unsigned char*>(chosenAlpn),
                         chosenAlpnLen);
  }

  return NS_OK;
}

NS_IMETHODIMP
NSSSocketControl::GetEarlyDataAccepted(bool* aAccepted) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  *aAccepted = mEarlyDataAccepted;
  return NS_OK;
}

void NSSSocketControl::SetEarlyDataAccepted(bool aAccepted) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mEarlyDataAccepted = aAccepted;
}

bool NSSSocketControl::GetDenyClientCert() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  return mDenyClientCert;
}

void NSSSocketControl::SetDenyClientCert(bool aDenyClientCert) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mDenyClientCert = aDenyClientCert;
}

NS_IMETHODIMP
NSSSocketControl::DriveHandshake() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  if (!mFd) {
    return NS_ERROR_FAILURE;
  }
  if (IsCanceled()) {
    PRErrorCode errorCode = GetErrorCode();
    MOZ_DIAGNOSTIC_ASSERT(errorCode, "handshake cancelled without error code");
    return GetXPCOMFromNSSError(errorCode);
  }

  SECStatus rv = SSL_ForceHandshake(mFd);

  if (rv != SECSuccess) {
    PRErrorCode errorCode = PR_GetError();
    MOZ_ASSERT(errorCode, "handshake failed without error code");
    // There is a bug in NSS. Sometimes SSL_ForceHandshake will return
    // SECFailure without setting an error code. In these cases, cancel
    // the connection with SEC_ERROR_LIBRARY_FAILURE.
    if (!errorCode) {
      errorCode = SEC_ERROR_LIBRARY_FAILURE;
    }
    if (errorCode == PR_WOULD_BLOCK_ERROR) {
      return NS_BASE_STREAM_WOULD_BLOCK;
    }

    SetCanceled(errorCode);
    return GetXPCOMFromNSSError(errorCode);
  }
  return NS_OK;
}

bool NSSSocketControl::GetForSTARTTLS() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  return mForSTARTTLS;
}

void NSSSocketControl::SetForSTARTTLS(bool aForSTARTTLS) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mForSTARTTLS = aForSTARTTLS;
}

NS_IMETHODIMP
NSSSocketControl::ProxyStartSSL() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  return ActivateSSL();
}

NS_IMETHODIMP
NSSSocketControl::StartTLS() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  return ActivateSSL();
}

NS_IMETHODIMP
NSSSocketControl::AsyncStartTLS(JSContext* aCx,
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

  nsCOMPtr<nsIEventTarget> target(
      do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID));
  if (!target) {
    return NS_ERROR_UNEXPECTED;
  }

  auto promiseHolder = MakeRefPtr<nsMainThreadPtrHolder<dom::Promise>>(
      "AsyncStartTLS promise", promise);

  nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(
      "AsyncStartTLS::StartTLS",
      [promiseHolder = std::move(promiseHolder), self = RefPtr{this}]() {
        nsresult rv = self->StartTLS();
        NS_DispatchToMainThread(NS_NewRunnableFunction(
            "AsyncStartTLS::Resolve", [rv, promiseHolder]() {
              dom::Promise* promise = promiseHolder.get()->get();
              if (NS_FAILED(rv)) {
                promise->MaybeReject(rv);
              } else {
                promise->MaybeResolveWithUndefined();
              }
            }));
      }));

  nsresult rv = target->Dispatch(runnable, NS_DISPATCH_NORMAL);
  if (NS_FAILED(rv)) {
    return rv;
  }

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
NSSSocketControl::SetNPNList(nsTArray<nsCString>& protocolArray) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  if (!mFd) return NS_ERROR_FAILURE;

  // the npn list is a concatenated list of 8 bit byte strings.
  nsCString npnList;

  for (uint32_t index = 0; index < protocolArray.Length(); ++index) {
    if (protocolArray[index].IsEmpty() || protocolArray[index].Length() > 255)
      return NS_ERROR_ILLEGAL_VALUE;

    npnList.Append(protocolArray[index].Length());
    npnList.Append(protocolArray[index]);
  }

  if (SSL_SetNextProtoNego(
          mFd, BitwiseCast<const unsigned char*, const char*>(npnList.get()),
          npnList.Length()) != SECSuccess)
    return NS_ERROR_FAILURE;

  return NS_OK;
}

nsresult NSSSocketControl::ActivateSSL() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  if (SECSuccess != SSL_OptionSet(mFd, SSL_SECURITY, true))
    return NS_ERROR_FAILURE;
  if (SECSuccess != SSL_ResetHandshake(mFd, false)) return NS_ERROR_FAILURE;

  mHandshakePending = true;

  return SetResumptionTokenFromExternalCache(mFd);
}

nsresult NSSSocketControl::GetFileDescPtr(PRFileDesc** aFilePtr) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  *aFilePtr = mFd;
  return NS_OK;
}

nsresult NSSSocketControl::SetFileDescPtr(PRFileDesc* aFilePtr) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mFd = aFilePtr;
  return NS_OK;
}

void NSSSocketControl::SetCertVerificationWaiting() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  // mCertVerificationState may be BeforeCertVerification for the first
  // handshake on the connection, or AfterCertVerification for subsequent
  // renegotiation handshakes.
  MOZ_ASSERT(mCertVerificationState != WaitingForCertVerification,
             "Invalid state transition to WaitingForCertVerification");
  mCertVerificationState = WaitingForCertVerification;
}

// Be careful that SetCertVerificationResult does NOT get called while we are
// processing a SSL callback function, because SSL_AuthCertificateComplete will
// attempt to acquire locks that are already held by libssl when it calls
// callbacks.
void NSSSocketControl::SetCertVerificationResult(PRErrorCode errorCode) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  MOZ_ASSERT(mCertVerificationState == WaitingForCertVerification,
             "Invalid state transition to AfterCertVerification");

  if (mFd) {
    SECStatus rv = SSL_AuthCertificateComplete(mFd, errorCode);
    // Only replace errorCode if there was originally no error.
    // SSL_AuthCertificateComplete will return SECFailure with the error code
    // set to PR_WOULD_BLOCK_ERROR if there is a pending event to select a
    // client authentication certificate. This is not an error.
    if (rv != SECSuccess && PR_GetError() != PR_WOULD_BLOCK_ERROR &&
        errorCode == 0) {
      errorCode = PR_GetError();
      if (errorCode == 0) {
        NS_ERROR("SSL_AuthCertificateComplete didn't set error code");
        errorCode = PR_INVALID_STATE_ERROR;
      }
    }
  }

  if (errorCode) {
    mFailedVerification = true;
    SetCanceled(errorCode);
  }

  if (mPlaintextBytesRead && !errorCode) {
    glean::ssl::bytes_before_cert_callback.Accumulate(
        AssertedCast<uint32_t>(mPlaintextBytesRead));
  }

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("[%p] SetCertVerificationResult to AfterCertVerification, "
           "mTlsHandshakeCallback=%p",
           (void*)mFd, mTlsHandshakeCallback.get()));

  mCertVerificationState = AfterCertVerification;
  if (mTlsHandshakeCallback) {
    (void)mTlsHandshakeCallback->CertVerificationDone();
  }
}

void NSSSocketControl::ClientAuthCertificateSelected(
    nsTArray<uint8_t>& certBytes, nsTArray<nsTArray<uint8_t>>& certChainBytes) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  // If mFd is nullptr, the connection has been closed already, so we don't
  // need to do anything here.
  if (!mFd) {
    return;
  }
  SECItem certItem = {
      siBuffer,
      const_cast<uint8_t*>(certBytes.Elements()),
      static_cast<unsigned int>(certBytes.Length()),
  };
  // Ensure that osclientcerts will populate its list of certificates and keys.
  AutoSearchingForClientAuthCertificates _;
  UniqueCERTCertificate cert(CERT_NewTempCertificate(
      CERT_GetDefaultCertDB(), &certItem, nullptr, false, true));
  UniqueSECKEYPrivateKey key;
  if (cert) {
    key.reset(PK11_FindKeyByAnyCert(cert.get(), nullptr));
    mClientCertChain.reset(CERT_NewCertList());
    if (key && mClientCertChain) {
      for (const auto& certBytes : certChainBytes) {
        SECItem certItem = {
            siBuffer,
            const_cast<uint8_t*>(certBytes.Elements()),
            static_cast<unsigned int>(certBytes.Length()),
        };
        UniqueCERTCertificate cert(CERT_NewTempCertificate(
            CERT_GetDefaultCertDB(), &certItem, nullptr, false, true));
        if (cert) {
          if (CERT_AddCertToListTail(mClientCertChain.get(), cert.get()) ==
              SECSuccess) {
            (void)cert.release();
          }
        }
      }
    }
  }

  bool sendingClientAuthCert = cert && key;
  if (sendingClientAuthCert) {
    mSentClientCert = true;
    glean::security::client_auth_cert_usage.Get("sent"_ns).Add(1);
  }

  (void)SSL_ClientCertCallbackComplete(
      mFd, sendingClientAuthCert ? SECSuccess : SECFailure,
      sendingClientAuthCert ? key.release() : nullptr,
      sendingClientAuthCert ? cert.release() : nullptr);

  MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
          ("[%p] ClientAuthCertificateSelected mTlsHandshakeCallback=%p",
           (void*)mFd, mTlsHandshakeCallback.get()));
  if (mTlsHandshakeCallback) {
    (void)mTlsHandshakeCallback->ClientAuthCertificateSelected();
  }
}

NS_IMETHODIMP
NSSSocketControl::DisableEarlyData() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  if (!mFd) {
    return NS_OK;
  }
  if (IsCanceled()) {
    return NS_OK;
  }

  if (SSL_OptionSet(mFd, SSL_ENABLE_0RTT_DATA, false) != SECSuccess) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
NSSSocketControl::SetHandshakeCallbackListener(
    nsITlsHandshakeCallbackListener* callback) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mTlsHandshakeCallback = callback;
  return NS_OK;
}

PRStatus NSSSocketControl::CloseSocketAndDestroy() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();

  mClientAuthCertificateRequest.reset();

  PRFileDesc* popped = PR_PopIOLayer(mFd, PR_TOP_IO_LAYER);
  MOZ_ASSERT(
      popped && popped->identity == nsSSLIOLayerHelpers::nsSSLIOLayerIdentity,
      "SSL Layer not on top of stack");

  // The plaintext layer is not always present - so it's not a fatal error if it
  // cannot be removed.
  // Note that PR_PopIOLayer may modify its stack, so a pointer returned by
  // PR_GetIdentitiesLayer may not point to what we think it points to after
  // calling PR_PopIOLayer. We must operate on the pointer returned by
  // PR_PopIOLayer.
  if (PR_GetIdentitiesLayer(mFd,
                            nsSSLIOLayerHelpers::nsSSLPlaintextLayerIdentity)) {
    PRFileDesc* poppedPlaintext =
        PR_PopIOLayer(mFd, nsSSLIOLayerHelpers::nsSSLPlaintextLayerIdentity);
    poppedPlaintext->dtor(poppedPlaintext);
  }

  // We need to clear the callback to make sure the ssl layer cannot call the
  // callback after mFD is nulled.
  SSL_SetResumptionTokenCallback(mFd, nullptr, nullptr);

  PRStatus status = mFd->methods->close(mFd);

  // the NSSSocketControl instance can out-live the connection, so we need some
  // indication that the connection has been closed. mFd == nullptr is that
  // indication. This is needed, for example, when the connection is closed
  // before we have finished validating the server's certificate.
  mFd = nullptr;

  if (status != PR_SUCCESS) return status;

  popped->identity = PR_INVALID_IO_LAYER;
  popped->dtor(popped);

  return PR_SUCCESS;
}

NS_IMETHODIMP
NSSSocketControl::GetEsniTxt(nsACString& aEsniTxt) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  aEsniTxt = mEsniTxt;
  return NS_OK;
}

NS_IMETHODIMP
NSSSocketControl::SetEsniTxt(const nsACString& aEsniTxt) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mEsniTxt = aEsniTxt;

  if (mEsniTxt.Length()) {
    nsAutoCString esniBin;
    if (NS_OK != Base64Decode(mEsniTxt, esniBin)) {
      MOZ_LOG(gPIPNSSLog, LogLevel::Error,
              ("[%p] Invalid ESNIKeys record. Couldn't base64 decode\n",
               (void*)mFd));
      return NS_OK;
    }

    if (SECSuccess !=
        SSL_EnableESNI(mFd, reinterpret_cast<const PRUint8*>(esniBin.get()),
                       esniBin.Length(), nullptr)) {
      MOZ_LOG(gPIPNSSLog, LogLevel::Error,
              ("[%p] Invalid ESNIKeys record %s\n", (void*)mFd,
               PR_ErrorToName(PR_GetError())));
      return NS_OK;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
NSSSocketControl::GetEchConfig(nsACString& aEchConfig) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  aEchConfig = mEchConfig;
  return NS_OK;
}

NS_IMETHODIMP
NSSSocketControl::SetEchConfig(const nsACString& aEchConfig) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mEchConfig = aEchConfig;

  if (mEchConfig.Length()) {
    if (SECSuccess !=
        SSL_SetClientEchConfigs(
            mFd, reinterpret_cast<const PRUint8*>(aEchConfig.BeginReading()),
            aEchConfig.Length())) {
      MOZ_LOG(gPIPNSSLog, LogLevel::Error,
              ("[%p] Invalid EchConfig record %s\n", (void*)mFd,
               PR_ErrorToName(PR_GetError())));
      return NS_OK;
    }
    UpdateEchExtensionStatus(EchExtensionStatus::kReal);
  }
  return NS_OK;
}

NS_IMETHODIMP
NSSSocketControl::GetRetryEchConfig(nsACString& aEchConfig) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  if (!mFd) {
    return NS_ERROR_FAILURE;
  }

  ScopedAutoSECItem retryConfigItem;
  SECStatus rv = SSL_GetEchRetryConfigs(mFd, &retryConfigItem);
  if (rv != SECSuccess) {
    return NS_ERROR_FAILURE;
  }
  aEchConfig = nsCString(reinterpret_cast<const char*>(retryConfigItem.data),
                         retryConfigItem.len);
  return NS_OK;
}

NS_IMETHODIMP
NSSSocketControl::GetPeerId(nsACString& aResult) {
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

  mPeerId.AppendPrintf("tlsflags0x%08x:", mProviderTlsFlags);

  mPeerId.Append(mHostName);
  mPeerId.Append(':');
  mPeerId.AppendInt(GetPort());
  nsAutoCString suffix;
  mOriginAttributes.CreateSuffix(suffix);
  mPeerId.Append(suffix);

  aResult.Assign(mPeerId);
  return NS_OK;
}

nsresult NSSSocketControl::SetResumptionTokenFromExternalCache(PRFileDesc* fd) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  if (!fd) {
    return NS_ERROR_INVALID_ARG;
  }

  // If SSL_NO_CACHE option was set, we must not use the cache
  PRIntn val;
  if (SSL_OptionGet(fd, SSL_NO_CACHE, &val) != SECSuccess) {
    return NS_ERROR_FAILURE;
  }

  if (val != 0) {
    return NS_OK;
  }

  nsAutoCString peerId;
  nsresult rv = GetPeerId(peerId);
  if (NS_FAILED(rv)) {
    return rv;
  }

  uint32_t maxAttempts =
      mozilla::StaticPrefs::network_ssl_tokens_cache_records_per_entry();
  for (uint32_t attempt = 0; attempt < maxAttempts; ++attempt) {
    nsTArray<uint8_t> token;
    uint64_t tokenId = 0;
    mozilla::net::SessionCacheInfo info;
    rv = mozilla::net::SSLTokensCache::Get(peerId, token, info, &tokenId);
    if (NS_FAILED(rv)) {
      if (rv == NS_ERROR_NOT_AVAILABLE) {
        // It's ok if we can't find the token.
        return NS_OK;
      }

      return rv;
    }
    mDrewResumptionToken = true;

    SECStatus srv =
        SSL_SetResumptionToken(fd, token.Elements(), token.Length());
    if (srv == SECSuccess) {
      SetSessionCacheInfo(std::move(info));
      return NS_OK;
    }

    PRErrorCode error = PR_GetError();
    mozilla::net::SSLTokensCache::Remove(peerId, tokenId);
    MOZ_LOG(gPIPNSSLog, LogLevel::Debug,
            ("Setting token failed with NSS error %d [id=%s, attempt=%u]",
             error, PromiseFlatCString(peerId).get(), attempt));

    if (error == SSL_ERROR_BAD_RESUMPTION_TOKEN_ERROR) {
      continue;
    }

    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

void NSSSocketControl::SetPreliminaryHandshakeInfo(
    const SSLChannelInfo& channelInfo, const SSLCipherSuiteInfo& cipherInfo) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mResumed = channelInfo.resumed;
  mCipherSuite.emplace(channelInfo.cipherSuite);
  mProtocolVersion.emplace(channelInfo.protocolVersion & 0xFF);
  mKeaGroupName.emplace(getKeaGroupName(channelInfo.keaGroup));
  mSignatureSchemeName.emplace(getSignatureName(channelInfo.signatureScheme));
  mIsDelegatedCredential.emplace(channelInfo.peerDelegCred);
  mIsAcceptedEch.emplace(channelInfo.echAccepted);
}

void NSSSocketControl::MaybeSelectClientAuthCertificate() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  if (!IsWaitingForCertVerification() && mClaimed &&
      mClientAuthCertificateRequest.isSome()) {
    MOZ_LOG(gPIPNSSLog, mozilla::LogLevel::Debug,
            ("[%p] selecting client auth certificate", (void*)mFd));
    ClientAuthCertificateRequest request(
        mClientAuthCertificateRequest.extract());
    DoSelectClientAuthCertificate(this, std::move(request.mServerCertificate),
                                  std::move(request.mCANames));
  }
}

NS_IMETHODIMP NSSSocketControl::Claim() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mClaimed = true;
  return NS_OK;
}

NS_IMETHODIMP NSSSocketControl::SetBrowserId(uint64_t browserId) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mBrowserId = browserId;
  return NS_OK;
}

NS_IMETHODIMP NSSSocketControl::GetBrowserId(uint64_t* browserId) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  if (!browserId) {
    return NS_ERROR_INVALID_ARG;
  }
  *browserId = mBrowserId;
  return NS_OK;
}
