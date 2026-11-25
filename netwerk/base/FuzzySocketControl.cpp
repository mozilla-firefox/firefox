/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FuzzySocketControl.h"

#include "FuzzySecurityInfo.h"
#include "ipc/IPCMessageUtils.h"
#include "nsITlsHandshakeListener.h"
#include "sslt.h"

namespace mozilla {
namespace net {

FuzzySocketControl::FuzzySocketControl() {}

FuzzySocketControl::~FuzzySocketControl() {}

NS_IMPL_ISUPPORTS(FuzzySocketControl, nsITLSSocketControl)

NS_IMETHODIMP
FuzzySocketControl::GetProviderFlags(uint32_t* aProviderFlags) {
  MOZ_CRASH("Unused");
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::GetKEAUsed(int16_t* aKea) {
  // Can be ssl_kea_dh or ssl_kea_ecdh for HTTP2
  *aKea = ssl_kea_ecdh;
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::GetKEAKeyBits(uint32_t* aKeyBits) {
  // Must be >= 224 for ecdh and >= 2048 for dh when using HTTP2
  *aKeyBits = 256;
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::GetSSLVersionUsed(int16_t* aSSLVersionUsed) {
  // Must be >= TLS 1.2 for HTTP2
  *aSSLVersionUsed = nsITLSSocketControl::TLS_VERSION_1_2;
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::GetSSLVersionOffered(int16_t* aSSLVersionOffered) {
  *aSSLVersionOffered = nsITLSSocketControl::TLS_VERSION_1_2;
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::GetMACAlgorithmUsed(int16_t* aMac) {
  // The only valid choice for HTTP2 is SSL_MAC_AEAD
  *aMac = nsITLSSocketControl::SSL_MAC_AEAD;
  return NS_OK;
}

bool FuzzySocketControl::GetDenyClientCert() { return false; }

void FuzzySocketControl::SetDenyClientCert(bool aDenyClientCert) {
  // Called by mozilla::net::nsHttpConnection::StartSpdy
}

NS_IMETHODIMP
FuzzySocketControl::GetClientCertSent(bool* arg) {
  *arg = false;
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::GetFailedVerification(bool* arg) {
  *arg = false;
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::GetAlpnEarlySelection(nsACString& aAlpnSelected) {
  // TODO: For now we don't support early selection
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
FuzzySocketControl::GetEarlyDataAccepted(bool* aAccepted) {
  *aAccepted = false;
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::GetResumptionTokenPresent(bool* aPresent) {
  *aPresent = false;
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::DriveHandshake() { return NS_OK; }

NS_IMETHODIMP
FuzzySocketControl::IsAcceptableForHost(const nsACString& hostname,
                                        bool* _retval) {
  NS_ENSURE_ARG(_retval);
  *_retval = true;
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::TestJoinConnection(const nsACString& npnProtocol,
                                       const nsACString& hostname, int32_t port,
                                       bool* _retval) {
  *_retval = false;
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::JoinConnection(const nsACString& npnProtocol,
                                   const nsACString& hostname, int32_t port,
                                   bool* _retval) {
  *_retval = false;
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::ProxyStartSSL() { return NS_OK; }

NS_IMETHODIMP
FuzzySocketControl::StartTLS() { return NS_OK; }

NS_IMETHODIMP
FuzzySocketControl::AsyncStartTLS(JSContext* aCx,
                                  mozilla::dom::Promise** aPromise) {
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::SetNPNList(nsTArray<nsCString>& protocolArray) {
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::GetEsniTxt(nsACString& aEsniTxt) { return NS_OK; }

NS_IMETHODIMP
FuzzySocketControl::SetEsniTxt(const nsACString& aEsniTxt) {
  MOZ_CRASH("Unused");
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::GetEchConfig(nsACString& aEchConfig) { return NS_OK; }

NS_IMETHODIMP
FuzzySocketControl::SetEchConfig(const nsACString& aEchConfig) {
  MOZ_CRASH("Unused");
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::GetRetryEchConfig(nsACString& aEchConfig) { return NS_OK; }

NS_IMETHODIMP
FuzzySocketControl::GetPeerId(nsACString& aResult) {
  aResult.Truncate();
  return NS_OK;
}

NS_IMETHODIMP FuzzySocketControl::SetHandshakeCallbackListener(
    nsITlsHandshakeCallbackListener* callback) {
  if (callback) {
    callback->HandshakeDone();
  }
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::DisableEarlyData(void) { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP FuzzySocketControl::GetSecurityInfo(
    nsITransportSecurityInfo** aSecurityInfo) {
  nsCOMPtr<nsITransportSecurityInfo> securityInfo(new FuzzySecurityInfo());
  securityInfo.forget(aSecurityInfo);
  return NS_OK;
}

NS_IMETHODIMP
FuzzySocketControl::AsyncGetSecurityInfo(JSContext* aCx,
                                         mozilla::dom::Promise** aPromise) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP FuzzySocketControl::Claim() { return NS_OK; }

NS_IMETHODIMP FuzzySocketControl::SetBrowserId(uint64_t) { return NS_OK; }

NS_IMETHODIMP FuzzySocketControl::GetBrowserId(uint64_t*) {
  MOZ_CRASH("Unused");
  return NS_ERROR_NOT_IMPLEMENTED;
}

}  // namespace net
}  // namespace mozilla
