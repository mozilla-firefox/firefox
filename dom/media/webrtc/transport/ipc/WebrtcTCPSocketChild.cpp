/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebrtcTCPSocketChild.h"

#include "LoadInfo.h"
#include "WebrtcTCPSocketCallback.h"
#include "WebrtcTCPSocketLog.h"
#include "mozilla/net/NeckoChild.h"

using namespace mozilla::ipc;

namespace mozilla::net {

mozilla::ipc::IPCResult WebrtcTCPSocketChild::RecvOnClose(
    const nsresult& aReason) {
  LOG("WebrtcTCPSocketChild::RecvOnClose {}\n", fmt::ptr(this));

  MOZ_ASSERT(mProxyCallbacks, "webrtc TCP callbacks should be non-null");
  mProxyCallbacks->OnClose(aReason);
  mProxyCallbacks = nullptr;

  return IPC_OK();
}

mozilla::ipc::IPCResult WebrtcTCPSocketChild::RecvOnConnected(
    const nsACString& aProxyType) {
  LOG("WebrtcTCPSocketChild::RecvOnConnected {}\n", fmt::ptr(this));

  MOZ_ASSERT(mProxyCallbacks, "webrtc TCP callbacks should be non-null");
  mProxyCallbacks->OnConnected(aProxyType);

  return IPC_OK();
}

mozilla::ipc::IPCResult WebrtcTCPSocketChild::RecvOnRead(
    nsTArray<uint8_t>&& aReadData) {
  LOG("WebrtcTCPSocketChild::RecvOnRead {}\n", fmt::ptr(this));

  MOZ_ASSERT(mProxyCallbacks, "webrtc TCP callbacks should be non-null");
  mProxyCallbacks->OnRead(std::move(aReadData));

  return IPC_OK();
}

WebrtcTCPSocketChild::WebrtcTCPSocketChild(
    WebrtcTCPSocketCallback* aProxyCallbacks)
    : mProxyCallbacks(aProxyCallbacks) {
  LOG("WebrtcTCPSocketChild::WebrtcTCPSocketChild {}\n", fmt::ptr(this));
}

WebrtcTCPSocketChild::~WebrtcTCPSocketChild() {
  LOG("WebrtcTCPSocketChild::~WebrtcTCPSocketChild {}\n", fmt::ptr(this));
}

void WebrtcTCPSocketChild::AsyncOpen(
    const nsACString& aHost, const uint16_t& aPort,
    const nsACString& aLocalAddress, const uint16_t& aLocalPort, bool aUseTls,
    const std::shared_ptr<NrSocketProxyConfig>& aProxyConfig) {
  LOG("WebrtcTCPSocketChild::AsyncOpen {} {}:{}\n", fmt::ptr(this),
      PromiseFlatCString(aHost).get(), aPort);

  MOZ_ASSERT(NS_IsMainThread(), "not main thread");

  Maybe<net::WebrtcProxyConfig> proxyConfig;
  Maybe<dom::TabId> tabId;
  if (aProxyConfig) {
    proxyConfig = Some(aProxyConfig->GetConfig());
    tabId = Some(proxyConfig->tabId());
  }

  if (IsNeckoChild()) {
    // We're on a content process
    gNeckoChild->SendPWebrtcTCPSocketConstructor(this, tabId);
  }

  SendAsyncOpen(aHost, aPort, aLocalAddress, aLocalPort, aUseTls, proxyConfig);
}

}  // namespace mozilla::net
