/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "QuicSocketControl.h"

#include "Http3Session.h"
#include "SharedCertVerifier.h"
#include "nsISocketProvider.h"
#include "nsIWebProgressListener.h"
#include "nsNSSComponent.h"
#include "nsSocketTransportService2.h"
#include "nsThreadUtils.h"
#include "ssl.h"
#include "sslt.h"

namespace mozilla {
namespace net {

QuicSocketControl::QuicSocketControl(
    const nsCString& aHostName, int32_t aPort, uint32_t aProviderFlags,
    const mozilla::OriginAttributes& aOriginAttributes,
    Http3Session* aHttp3Session)
    : CommonSocketControl(aHostName, aPort, aProviderFlags) {
  mOriginAttributes = aOriginAttributes;
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mHttp3Session = do_GetWeakReference(
      static_cast<nsISupportsWeakReference*>(aHttp3Session));
}

void QuicSocketControl::SetCertVerificationResult(PRErrorCode errorCode) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();

  if (errorCode) {
    mFailedVerification = true;
    SetCanceled(errorCode);
  }

  CallAuthenticated();
}

NS_IMETHODIMP
QuicSocketControl::GetSSLVersionOffered(int16_t* aSSLVersionOffered) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  *aSSLVersionOffered = nsITLSSocketControl::TLS_VERSION_1_3;
  return NS_OK;
}

void QuicSocketControl::CallAuthenticated() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  RefPtr<Http3Session> http3Session = do_QueryReferent(mHttp3Session);
  if (http3Session) {
    http3Session->Authenticated(GetErrorCode());
  }
}

void QuicSocketControl::HandshakeCompleted() {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  uint32_t state = nsIWebProgressListener::STATE_IS_SECURE;

  // If we're here, the TLS handshake has succeeded. If the overridable error
  // category is nonzero, the user has added an override for a certificate
  // error.
  if (mOverridableErrorCategory.isSome() &&
      *mOverridableErrorCategory !=
          nsITransportSecurityInfo::OverridableErrorCategory::ERROR_UNSET) {
    state |= nsIWebProgressListener::STATE_CERT_USER_OVERRIDDEN;
  }

  SetSecurityState(state);
  mHandshakeCompleted = true;
}

void QuicSocketControl::SetNegotiatedNPN(const nsACString& aValue) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mNegotiatedNPN = aValue;
  mNPNCompleted = true;
}

void QuicSocketControl::SetInfo(uint16_t aCipherSuite,
                                uint16_t aProtocolVersion,
                                uint16_t aKeaGroupName,
                                uint16_t aSignatureScheme, bool aEchAccepted) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  SSLCipherSuiteInfo cipherInfo;
  if (SSL_GetCipherSuiteInfo(aCipherSuite, &cipherInfo, sizeof cipherInfo) ==
      SECSuccess) {
    mCipherSuite.emplace(aCipherSuite);
    mProtocolVersion.emplace(aProtocolVersion & 0xFF);
    mKeaGroupName.emplace(getKeaGroupName(aKeaGroupName));
    mSignatureSchemeName.emplace(getSignatureName(aSignatureScheme));
    mIsAcceptedEch.emplace(aEchAccepted);
  }
}

NS_IMETHODIMP
QuicSocketControl::GetEchConfig(nsACString& aEchConfig) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  aEchConfig = mEchConfig;
  return NS_OK;
}

NS_IMETHODIMP
QuicSocketControl::SetEchConfig(const nsACString& aEchConfig) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mEchConfig = aEchConfig;
  RefPtr<Http3Session> http3Session = do_QueryReferent(mHttp3Session);
  if (http3Session) {
    http3Session->DoSetEchConfig(mEchConfig);
  }
  return NS_OK;
}

NS_IMETHODIMP
QuicSocketControl::GetRetryEchConfig(nsACString& aEchConfig) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  aEchConfig = mRetryEchConfig;
  return NS_OK;
}

void QuicSocketControl::SetRetryEchConfig(const nsACString& aEchConfig) {
  COMMON_SOCKET_CONTROL_ASSERT_ON_OWNING_THREAD();
  mRetryEchConfig = aEchConfig;
}

bool QuicSocketControl::IsBuiltCertChainRootBuiltInRoot() const {
  return mIsBuiltCertChainRootBuiltInRoot;
}

}  // namespace net
}  // namespace mozilla
