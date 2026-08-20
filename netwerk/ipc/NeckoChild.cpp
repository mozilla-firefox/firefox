/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/net/NeckoChild.h"

#include "SerializedLoadContext.h"
#include "SimpleChannel.h"
#include "mozilla/Components.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/network/TCPSocketChild.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/net/AltDataOutputStreamChild.h"
#include "mozilla/net/CacheEntryWriteHandleChild.h"
#include "mozilla/net/ChildDNSService.h"
#include "mozilla/net/CookieServiceChild.h"
#include "mozilla/net/DNSRequestChild.h"
#include "mozilla/net/HttpChannelChild.h"
#include "mozilla/net/IPCTransportProvider.h"
#include "mozilla/net/SocketProcessBridgeChild.h"
#include "mozilla/net/WebSocketChannelChild.h"
#include "mozilla/net/WebSocketEventListenerChild.h"
#include "nsGlobalWindowInner.h"
#include "nsHttp.h"
#include "nsINetworkLinkService.h"
#include "nsIOService.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"

using mozilla::dom::TCPSocketChild;

namespace mozilla {
namespace net {

PNeckoChild* gNeckoChild = nullptr;

// C++ file contents

NeckoChild::~NeckoChild() {
  // Send__delete__(gNeckoChild);
  gNeckoChild = nullptr;
}

void NeckoChild::InitNeckoChild() {
  if (!IsNeckoChild()) {
    MOZ_ASSERT(false, "InitNeckoChild called by non-child!");
    return;
  }

  if (!gNeckoChild) {
    mozilla::dom::ContentChild* cpc =
        mozilla::dom::ContentChild::GetSingleton();
    NS_ASSERTION(cpc, "Content Protocol is NULL!");
    if (NS_WARN_IF(cpc->IsShuttingDown())) {
      return;
    }
    RefPtr<NeckoChild> child = new NeckoChild();
    gNeckoChild = cpc->SendPNeckoConstructor(child);
    NS_ASSERTION(gNeckoChild, "PNecko Protocol init failed!");
  }
}

PCacheEntryWriteHandleChild* NeckoChild::AllocPCacheEntryWriteHandleChild(
    PHttpChannelChild* channel) {
  // We don't allocate here: see HttpChannelChild::GetCacheEntryWriteHandle()
  MOZ_ASSERT_UNREACHABLE(
      "AllocPCacheEntryWriteHandleChild should not be called");
  return nullptr;
}

bool NeckoChild::DeallocPCacheEntryWriteHandleChild(
    PCacheEntryWriteHandleChild* aActor) {
  CacheEntryWriteHandleChild* child =
      static_cast<CacheEntryWriteHandleChild*>(aActor);
  child->ReleaseIPDLReference();
  return true;
}

PAltDataOutputStreamChild* NeckoChild::AllocPAltDataOutputStreamChild(
    const nsACString& type, const int64_t& predictedSize,
    const mozilla::Maybe<mozilla::NotNull<PHttpChannelChild*>>& channel,
    const mozilla::Maybe<mozilla::NotNull<PCacheEntryWriteHandleChild*>>&
        handle) {
  // We don't allocate here: see HttpChannelChild::OpenAlternativeOutputStream()
  MOZ_ASSERT_UNREACHABLE("AllocPAltDataOutputStreamChild should not be called");
  return nullptr;
}

bool NeckoChild::DeallocPAltDataOutputStreamChild(
    PAltDataOutputStreamChild* aActor) {
  AltDataOutputStreamChild* child =
      mozilla::ipc::ActorCast<AltDataOutputStreamChild>(aActor);
  child->ReleaseIPDLReference();
  return true;
}

PCookieServiceChild* NeckoChild::AllocPCookieServiceChild() {
  // We don't allocate here: see CookieService::GetSingleton()
  MOZ_ASSERT_UNREACHABLE("AllocPCookieServiceChild should not be called");
  return nullptr;
}

bool NeckoChild::DeallocPCookieServiceChild(PCookieServiceChild* cs) {
  NS_ASSERTION(IsNeckoChild(),
               "DeallocPCookieServiceChild called by non-child!");

  CookieServiceChild* p = mozilla::ipc::ActorCast<CookieServiceChild>(cs);
  p->Release();
  return true;
}

PWebSocketChild* NeckoChild::AllocPWebSocketChild(
    PBrowserChild* browser, const SerializedLoadContext& aSerialized,
    const uint32_t& aSerial) {
  MOZ_ASSERT_UNREACHABLE("AllocPWebSocketChild should not be called");
  return nullptr;
}

bool NeckoChild::DeallocPWebSocketChild(PWebSocketChild* child) {
  WebSocketChannelChild* p =
      mozilla::ipc::ActorCast<WebSocketChannelChild>(child);
  p->ReleaseIPDLReference();
  return true;
}

PWebSocketEventListenerChild* NeckoChild::AllocPWebSocketEventListenerChild(
    const uint64_t& aInnerWindowID) {
  RefPtr<WebSocketEventListenerChild> c = new WebSocketEventListenerChild(
      aInnerWindowID, GetMainThreadSerialEventTarget());
  return c.forget().take();
}

bool NeckoChild::DeallocPWebSocketEventListenerChild(
    PWebSocketEventListenerChild* aActor) {
  RefPtr<WebSocketEventListenerChild> c =
      dont_AddRef(mozilla::ipc::ActorCast<WebSocketEventListenerChild>(aActor));
  MOZ_ASSERT(c);
  return true;
}

PTCPSocketChild* NeckoChild::AllocPTCPSocketChild(const nsAString& host,
                                                  const uint16_t& port) {
  TCPSocketChild* p = new TCPSocketChild(host, port, nullptr);
  p->AddIPDLReference();
  return p;
}

bool NeckoChild::DeallocPTCPSocketChild(PTCPSocketChild* child) {
  TCPSocketChild* p = static_cast<TCPSocketChild*>(child);
  p->ReleaseIPDLReference();
  return true;
}

PTransportProviderChild* NeckoChild::AllocPTransportProviderChild() {
  // This refcount is transferred to the receiver of the message that
  // includes the PTransportProviderChild actor.
  RefPtr<TransportProviderChild> res = new TransportProviderChild();

  return res.forget().take();
}

bool NeckoChild::DeallocPTransportProviderChild(
    PTransportProviderChild* aActor) {
  return true;
}

mozilla::ipc::IPCResult NeckoChild::RecvSpeculativeConnectRequest() {
  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  if (obsService) {
    obsService->NotifyObservers(nullptr, "speculative-connect-request",
                                nullptr);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoChild::RecvNetworkChangeNotification(
    nsCString const& type) {
  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  if (obsService) {
    obsService->NotifyObservers(nullptr, NS_NETWORK_LINK_TOPIC,
                                NS_ConvertUTF8toUTF16(type).get());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoChild::RecvSetTRRDomain(const nsCString& domain) {
  RefPtr<net::ChildDNSService> dnsServiceChild =
      dont_AddRef(net::ChildDNSService::GetSingleton());
  if (dnsServiceChild) {
    dnsServiceChild->SetTRRDomain(domain);
  }
  return IPC_OK();
}

}  // namespace net
}  // namespace mozilla
