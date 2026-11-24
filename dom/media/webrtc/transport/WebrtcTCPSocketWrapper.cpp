/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebrtcTCPSocketWrapper.h"

#include "mozilla/net/WebrtcTCPSocketChild.h"
#include "nr_socket_proxy_config.h"
#include "nsNetCID.h"
#include "nsProxyRelease.h"
#include "nsServiceManagerUtils.h"

namespace mozilla::net {

using std::shared_ptr;

WebrtcTCPSocketWrapper::WebrtcTCPSocketWrapper(
    WebrtcTCPSocketCallback* aCallbacks)
    : mProxyCallbacks(aCallbacks),
      mWebrtcTCPSocket(nullptr),
      mMainThread(nullptr),
      mSocketThread(nullptr) {
  mMainThread = GetMainThreadSerialEventTarget();
  mSocketThread = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID);
  MOZ_RELEASE_ASSERT(mMainThread, "no main thread");
  MOZ_RELEASE_ASSERT(mSocketThread, "no socket thread");
}

WebrtcTCPSocketWrapper::~WebrtcTCPSocketWrapper() {
  MOZ_ASSERT(!mWebrtcTCPSocket, "webrtc TCP socket non-null");

  // If we're never opened then we never get an OnClose from our parent process.
  // We need to release our callbacks here safely.
  NS_ProxyRelease("WebrtcTCPSocketWrapper::CleanUpCallbacks", mSocketThread,
                  mProxyCallbacks.forget());
}

void WebrtcTCPSocketWrapper::AsyncOpen(
    const nsCString& aHost, const uint16_t& aPort,
    const nsCString& aLocalAddress, const uint16_t& aLocalPort, bool aUseTls,
    const shared_ptr<NrSocketProxyConfig>& aConfig) {
  if (!NS_IsMainThread()) {
    MOZ_ALWAYS_SUCCEEDS(mMainThread->Dispatch(
        NewRunnableMethod<const nsCString, const uint16_t, const nsCString,
                          const uint16_t, bool,
                          const shared_ptr<NrSocketProxyConfig>>(
            "WebrtcTCPSocketWrapper::AsyncOpen", this,
            &WebrtcTCPSocketWrapper::AsyncOpen, aHost, aPort, aLocalAddress,
            aLocalPort, aUseTls, aConfig)));
    return;
  }

  MOZ_ASSERT(!mWebrtcTCPSocket, "wrapper already open");
  mWebrtcTCPSocket = new WebrtcTCPSocketChild(this);
  mWebrtcTCPSocket->AsyncOpen(aHost, aPort, aLocalAddress, aLocalPort, aUseTls,
                              aConfig);
}

void WebrtcTCPSocketWrapper::SendWrite(nsTArray<uint8_t>&& aReadData) {
  if (!NS_IsMainThread()) {
    MOZ_ALWAYS_SUCCEEDS(
        mMainThread->Dispatch(NewRunnableMethod<nsTArray<uint8_t>&&>(
            "WebrtcTCPSocketWrapper::SendWrite", this,
            &WebrtcTCPSocketWrapper::SendWrite, std::move(aReadData))));
    return;
  }

  MOZ_ASSERT(mWebrtcTCPSocket, "webrtc TCP socket should be non-null");
  mWebrtcTCPSocket->SendWrite(aReadData);
}

void WebrtcTCPSocketWrapper::Close() {
  if (!NS_IsMainThread()) {
    MOZ_ALWAYS_SUCCEEDS(mMainThread->Dispatch(
        NewRunnableMethod("WebrtcTCPSocketWrapper::Close", this,
                          &WebrtcTCPSocketWrapper::Close)));
    return;
  }

  // We're only open if we have a channel. Also Close() should be idempotent.
  if (mWebrtcTCPSocket) {
    RefPtr<WebrtcTCPSocketChild> child = mWebrtcTCPSocket;
    mWebrtcTCPSocket = nullptr;
    child->SendClose();
  }
}

void WebrtcTCPSocketWrapper::OnClose(nsresult aReason) {
  MOZ_ASSERT(NS_IsMainThread(), "not on main thread");
  MOZ_ASSERT(mProxyCallbacks, "webrtc TCP callbacks should be non-null");

  MOZ_ALWAYS_SUCCEEDS(mSocketThread->Dispatch(NewRunnableMethod<nsresult>(
      "WebrtcTCPSocketWrapper::OnClose", mProxyCallbacks,
      &WebrtcTCPSocketCallback::OnClose, aReason)));

  NS_ProxyRelease("WebrtcTCPSocketWrapper::CleanUpCallbacks", mSocketThread,
                  mProxyCallbacks.forget());
}

void WebrtcTCPSocketWrapper::OnRead(nsTArray<uint8_t>&& aReadData) {
  MOZ_ASSERT(NS_IsMainThread(), "not on main thread");
  MOZ_ASSERT(mProxyCallbacks, "webrtc TCP callbacks should be non-null");

  MOZ_ALWAYS_SUCCEEDS(
      mSocketThread->Dispatch(NewRunnableMethod<nsTArray<uint8_t>&&>(
          "WebrtcTCPSocketWrapper::OnRead", mProxyCallbacks,
          &WebrtcTCPSocketCallback::OnRead, std::move(aReadData))));
}

void WebrtcTCPSocketWrapper::OnConnected(const nsACString& aProxyType) {
  MOZ_ASSERT(NS_IsMainThread(), "not on main thread");
  MOZ_ASSERT(mProxyCallbacks, "webrtc TCP callbacks should be non-null");

  MOZ_ALWAYS_SUCCEEDS(mSocketThread->Dispatch(NewRunnableMethod<nsCString>(
      "WebrtcTCPSocketWrapper::OnConnected", mProxyCallbacks,
      &WebrtcTCPSocketCallback::OnConnected, aProxyType)));
}

}  // namespace mozilla::net
