/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TlsHandshaker_h_
#define TlsHandshaker_h_

#include "nsITlsHandshakeListener.h"
#include "nsString.h"

class nsISocketTransport;
class nsITLSSocketControl;

namespace mozilla::net {

class nsHttpConnection;
class nsHttpConnectionInfo;

class TlsHandshaker : public nsITlsHandshakeCallbackListener {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSITLSHANDSHAKECALLBACKLISTENER

  TlsHandshaker(nsHttpConnectionInfo* aInfo, nsHttpConnection* aOwner);

  void SetupSSL(bool aInSpdyTunnel, bool aForcePlainText);
  [[nodiscard]] nsresult InitSSLParams(bool connectingToProxy,
                                       bool ProxyStartSSL);
  [[nodiscard]] nsresult SetupNPNList(nsITLSSocketControl* ssl, uint32_t caps,
                                      bool connectingToProxy);
  // Makes certain the SSL handshake is complete and NPN negotiation
  // has had a chance to happen
  [[nodiscard]] bool EnsureNPNComplete();
  void FinishNPNSetup(bool handshakeSucceeded, bool hasSecurityInfo);
  bool EarlyDataAvailable() const {
    return mEarlyDataState == EarlyData::USED ||
           mEarlyDataState == EarlyData::CANNOT_BE_USED;
  }
  bool EarlyDataWasAvailable() const {
    return mEarlyDataState != EarlyData::NOT_AVAILABLE &&
           mEarlyDataState != EarlyData::DONE_NOT_AVAILABLE;
  }
  bool EarlyDataUsed() const { return mEarlyDataState == EarlyData::USED; }
  bool EarlyDataCanNotBeUsed() const {
    return mEarlyDataState == EarlyData::CANNOT_BE_USED;
  }
  void EarlyDataDone();
  // The early data allowance of the session ticket is used up, so nothing more
  // can be written until the handshake completes.
  bool EarlyDataLimitReached() const { return mEarlyDataLimitReached; }
  void SetEarlyDataLimitReached() { mEarlyDataLimitReached = true; }

#ifndef ANDROID
  void EarlyDataTelemetry(int16_t tlsVersion, bool earlyDataAccepted,
                          int64_t aContentBytesWritten0RTT);
#endif

  bool NPNComplete() const { return mNPNComplete; }
  bool SetupSSLCalled() const { return mSetupSSLCalled; }
  bool TlsHandshakeComplitionPending() const {
    return mTlsHandshakeComplitionPending;
  }
  const nsCString& EarlyNegotiatedALPN() const { return mEarlyNegotiatedALPN; }
  void SetNPNComplete() { mNPNComplete = true; }
  void NotifyClose();

 private:
  virtual ~TlsHandshaker();

  void Check0RttEnabled(nsITLSSocketControl* ssl);
  void ReportSecureConnectionStart();

  // SPDY related
  bool mSetupSSLCalled{false};
  bool mNPNComplete{false};

  bool mSecureConnectionStartReported{false};
  bool mTlsHandshakeComplitionPending{false};
  bool mEarlyDataLimitReached{false};
  // Helper variable for 0RTT handshake;
  // Possible 0RTT has been checked.
  bool m0RTTChecked{false};
  // 0RTT data state.
  enum EarlyData {
    NOT_AVAILABLE,
    USED,
    CANNOT_BE_USED,
    DONE_NOT_AVAILABLE,
    DONE_USED,
    DONE_CANNOT_BE_USED,
  };
  EarlyData mEarlyDataState{EarlyData::NOT_AVAILABLE};
  nsCString mEarlyNegotiatedALPN;
  RefPtr<nsHttpConnectionInfo> mConnInfo;
  // nsHttpConnection and TlsHandshaker create a reference cycle. To break this
  // cycle, NotifyClose() needs to be called in nsHttpConnection::Close().
  RefPtr<nsHttpConnection> mOwner;
};

}  // namespace mozilla::net

#endif  // TlsHandshaker_h_
