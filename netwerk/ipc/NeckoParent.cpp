/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/net/NeckoParent.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/Components.h"
#include "mozilla/ContentPrincipal.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/ipc/IPCStreamUtils.h"
#include "mozilla/net/CookieServiceParent.h"
#include "mozilla/net/ExtensionProtocolHandler.h"
#include "mozilla/net/HttpChannelParent.h"
#include "mozilla/net/MozNewTabWallpaperProtocolHandler.h"
#include "mozilla/net/PageThumbProtocolHandler.h"
#include "mozilla/net/WebSocketChannelParent.h"
#include "mozilla/net/WebSocketEventListenerParent.h"
#include "nsBaseParentChannel.h"
#include "nsHttp.h"
#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/net/GeckoViewContentChannelParent.h"
#endif
#include "mozilla/net/AltDataOutputStreamParent.h"
#include "mozilla/net/CacheEntryWriteHandleParent.h"
#include "mozilla/net/DNSRequestParent.h"
#include "mozilla/net/DocumentChannelParent.h"
#include "mozilla/net/IPCTransportProvider.h"
#include "mozilla/net/PSocketProcessBridgeParent.h"
#include "mozilla/net/RemoteStreamGetter.h"
#include "mozilla/net/RequestContextService.h"
#include "mozilla/net/SocketProcessParent.h"
#ifdef MOZ_WEBRTC
#  include "mozilla/net/StunAddrsRequestParent.h"
#  include "mozilla/net/WebrtcTCPSocketParent.h"
#endif
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/MaybeDiscarded.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/network/TCPServerSocketParent.h"
#include "mozilla/dom/network/TCPSocketParent.h"
#include "mozilla/dom/network/UDPSocketParent.h"
#ifdef MOZ_PLACES
#  include "mozilla/places/PageIconProtocolHandler.h"
#endif
#include "SerializedLoadContext.h"
#include "mozilla/LoadContext.h"
#include "mozilla/MozPromise.h"
#include "mozilla/dom/HTMLDNSPrefetch.h"
#include "nsAuthInformationHolder.h"
#include "nsEscape.h"
#include "nsFileChannel.h"
#include "nsHttpHandler.h"
#include "nsIMIMEService.h"
#include "nsIOService.h"
#include "nsISpeculativeConnect.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"

using IPC::SerializedLoadContext;
using mozilla::dom::BrowserParent;
using mozilla::dom::ContentParent;
using mozilla::dom::TCPServerSocketParent;
using mozilla::dom::TCPSocketParent;
using mozilla::dom::UDPSocketParent;
using mozilla::ipc::LoadInfoArgsToLoadInfo;
using mozilla::ipc::PrincipalInfo;
#ifdef MOZ_PLACES
using mozilla::places::PageIconProtocolHandler;
#endif

namespace mozilla {
namespace net {

// C++ file contents

namespace {

class SpeculativeConnectCallbackWrapper final : public nsIInterfaceRequestor {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIINTERFACEREQUESTOR

  explicit SpeculativeConnectCallbackWrapper(nsILoadContext* aLoadContext)
      : mLoadContext(aLoadContext) {}

 private:
  virtual ~SpeculativeConnectCallbackWrapper() = default;

  nsCOMPtr<nsILoadContext> mLoadContext;
};
NS_IMPL_ISUPPORTS(SpeculativeConnectCallbackWrapper, nsIInterfaceRequestor)

NS_IMETHODIMP
SpeculativeConnectCallbackWrapper::GetInterface(const nsIID& aIID,
                                                void** result) {
  if (mLoadContext && aIID.Equals(NS_GET_IID(nsILoadContext))) {
    return mLoadContext->QueryInterface(aIID, result);
  }
  return NS_ERROR_NO_INTERFACE;
}
}  // anonymous namespace

NeckoParent::NeckoParent() : mSocketProcessBridgeInited(false) {
  // Init HTTP protocol handler now since we need atomTable up and running very
  // early (IPDL argument handling for PHttpChannel constructor needs it) so
  // normal init (during 1st Http channel request) isn't early enough.
  nsCOMPtr<nsIProtocolHandler> proto =
      do_GetService("@mozilla.org/network/protocol;1?name=http");
}

static PBOverrideStatus PBOverrideStatusFromLoadContext(
    const SerializedLoadContext& aSerialized) {
  if (!aSerialized.IsNotNull() && aSerialized.IsPrivateBitValid()) {
    return aSerialized.mOriginAttributes.IsPrivateBrowsing()
               ? kPBOverride_Private
               : kPBOverride_NotPrivate;
  }
  return kPBOverride_Unset;
}

static already_AddRefed<nsIPrincipal> GetRequestingPrincipal(
    const LoadInfoArgs& aLoadInfoArgs) {
  const Maybe<PrincipalInfo>& optionalPrincipalInfo =
      aLoadInfoArgs.requestingPrincipalInfo();

  if (optionalPrincipalInfo.isNothing()) {
    return nullptr;
  }

  const PrincipalInfo& principalInfo = optionalPrincipalInfo.ref();

  auto principalOrErr = PrincipalInfoToPrincipal(principalInfo);
  return principalOrErr.isOk() ? principalOrErr.unwrap().forget() : nullptr;
}

static already_AddRefed<nsIPrincipal> GetRequestingPrincipal(
    const HttpChannelCreationArgs& aArgs) {
  if (aArgs.type() != HttpChannelCreationArgs::THttpChannelOpenArgs) {
    return nullptr;
  }

  const HttpChannelOpenArgs& args = aArgs.get_HttpChannelOpenArgs();
  return GetRequestingPrincipal(args.loadInfo());
}

void NeckoParent::GetValidatedOriginAttributes(
    const SerializedLoadContext& aSerialized, PContentParent* aContent,
    nsIPrincipal* aRequestingPrincipal, OriginAttributes& aAttrs) {
  if (!aSerialized.IsNotNull()) {
    // If serialized is null, we cannot validate anything. We have to assume
    // that this requests comes from a SystemPrincipal.
    aAttrs = OriginAttributes();
  } else {
    aAttrs = aSerialized.mOriginAttributes;
  }
}

void NeckoParent::CreateChannelLoadContext(
    PBrowserParent* aBrowser, PContentParent* aContent,
    const SerializedLoadContext& aSerialized,
    nsIPrincipal* aRequestingPrincipal, nsCOMPtr<nsILoadContext>& aResult) {
  OriginAttributes attrs;
  GetValidatedOriginAttributes(aSerialized, aContent, aRequestingPrincipal,
                               attrs);

  if (aSerialized.IsNotNull()) {
    attrs.SyncAttributesWithPrivateBrowsing(
        aSerialized.mOriginAttributes.IsPrivateBrowsing());

    RefPtr<BrowserParent> browserParent = BrowserParent::GetFrom(aBrowser);
    dom::Element* topFrameElement = nullptr;
    if (browserParent) {
      topFrameElement = browserParent->GetOwnerElement();
    }
    aResult = new LoadContext(aSerialized, topFrameElement, attrs);
  }
}

void NeckoParent::ActorDestroy(ActorDestroyReason aWhy) {
  // Nothing needed here. Called right before destructor since this is a
  // non-refcounted class.
}

already_AddRefed<PHttpChannelParent> NeckoParent::AllocPHttpChannelParent(
    PBrowserParent* aBrowser, const SerializedLoadContext& aSerialized,
    const HttpChannelCreationArgs& aOpenArgs) {
  nsCOMPtr<nsIPrincipal> requestingPrincipal =
      GetRequestingPrincipal(aOpenArgs);

  nsCOMPtr<nsILoadContext> loadContext;
  CreateChannelLoadContext(aBrowser, Manager(), aSerialized,
                           requestingPrincipal, loadContext);
  PBOverrideStatus overrideStatus =
      PBOverrideStatusFromLoadContext(aSerialized);
  RefPtr<HttpChannelParent> p = new HttpChannelParent(
      BrowserParent::GetFrom(aBrowser), loadContext, overrideStatus);
  return p.forget();
}

mozilla::ipc::IPCResult NeckoParent::RecvPHttpChannelConstructor(
    PHttpChannelParent* aActor, PBrowserParent* aBrowser,
    const SerializedLoadContext& aSerialized,
    const HttpChannelCreationArgs& aOpenArgs) {
  HttpChannelParent* p = static_cast<HttpChannelParent*>(aActor);
  if (!p->Init(aOpenArgs)) {
    return IPC_FAIL_NO_REASON(this);
  }
  return IPC_OK();
}

already_AddRefed<PStunAddrsRequestParent>
NeckoParent::AllocPStunAddrsRequestParent() {
#ifdef MOZ_WEBRTC
  return do_AddRef(new StunAddrsRequestParent());
#else
  return nullptr;
#endif
}

already_AddRefed<PWebrtcTCPSocketParent>
NeckoParent::AllocPWebrtcTCPSocketParent(const Maybe<TabId>& aTabId) {
#ifdef MOZ_WEBRTC
  return do_AddRef(new WebrtcTCPSocketParent(aTabId));
#else
  return nullptr;
#endif
}

PCacheEntryWriteHandleParent* NeckoParent::AllocPCacheEntryWriteHandleParent(
    PHttpChannelParent* channel) {
  HttpChannelParent* chan = static_cast<HttpChannelParent*>(channel);
  CacheEntryWriteHandleParent* parent = chan->AllocCacheEntryWriteHandle();
  parent->AddRef();
  return parent;
}

bool NeckoParent::DeallocPCacheEntryWriteHandleParent(
    PCacheEntryWriteHandleParent* aActor) {
  CacheEntryWriteHandleParent* parent =
      static_cast<CacheEntryWriteHandleParent*>(aActor);
  parent->Release();
  return true;
}

PAltDataOutputStreamParent* NeckoParent::AllocPAltDataOutputStreamParent(
    const nsACString& type, const int64_t& predictedSize,
    mozilla::Maybe<mozilla::NotNull<mozilla::net::PHttpChannelParent*>>&
        channel,
    mozilla::Maybe<mozilla::NotNull<PCacheEntryWriteHandleParent*>>& handle) {
  MOZ_ASSERT(channel || handle);

  nsresult rv;
  nsCOMPtr<nsIAsyncOutputStream> stream;
  if (channel) {
    HttpChannelParent* chan = static_cast<HttpChannelParent*>(channel->get());
    rv = chan->OpenAlternativeOutputStream(type, predictedSize,
                                           getter_AddRefs(stream));
  } else {
    CacheEntryWriteHandleParent* h =
        mozilla::ipc::ActorCast<CacheEntryWriteHandleParent>(handle->get());
    rv = h->OpenAlternativeOutputStream(type, predictedSize,
                                        getter_AddRefs(stream));
  }
  AltDataOutputStreamParent* parent = new AltDataOutputStreamParent(stream);
  parent->AddRef();
  // If the return value was not NS_OK, the error code will be sent
  // asynchronously to the child, after receiving the first message.
  parent->SetError(rv);
  return parent;
}

bool NeckoParent::DeallocPAltDataOutputStreamParent(
    PAltDataOutputStreamParent* aActor) {
  AltDataOutputStreamParent* parent =
      mozilla::ipc::ActorCast<AltDataOutputStreamParent>(aActor);
  parent->Release();
  return true;
}

already_AddRefed<PDocumentChannelParent>
NeckoParent::AllocPDocumentChannelParent(
    const dom::MaybeDiscarded<dom::BrowsingContext>& aContext,
    const DocumentChannelCreationArgs& args) {
  RefPtr<DocumentChannelParent> p = new DocumentChannelParent();
  return p.forget();
}

mozilla::ipc::IPCResult NeckoParent::RecvPDocumentChannelConstructor(
    PDocumentChannelParent* aActor,
    const dom::MaybeDiscarded<dom::BrowsingContext>& aContext,
    const DocumentChannelCreationArgs& aArgs) {
  DocumentChannelParent* p =
      mozilla::ipc::ActorCast<DocumentChannelParent>(aActor);

  if (aContext.IsNullOrDiscarded()) {
    (void)p->SendFailedAsyncOpen(NS_ERROR_FAILURE);
    return IPC_OK();
  }

  if (!p->Init(aContext.get_canonical(), aArgs)) {
    return IPC_FAIL(this, "Couldn't initialize DocumentChannel");
  }

  return IPC_OK();
}

PCookieServiceParent* NeckoParent::AllocPCookieServiceParent() {
  return new CookieServiceParent(
      mozilla::ipc::ActorCast<ContentParent>(Manager()));
}

bool NeckoParent::DeallocPCookieServiceParent(PCookieServiceParent* cs) {
  delete cs;
  return true;
}

PWebSocketParent* NeckoParent::AllocPWebSocketParent(
    PBrowserParent* browser, const SerializedLoadContext& serialized,
    const uint32_t& aSerial) {
  nsCOMPtr<nsILoadContext> loadContext;
  CreateChannelLoadContext(browser, Manager(), serialized, nullptr,
                           loadContext);

  RefPtr<BrowserParent> browserParent = BrowserParent::GetFrom(browser);
  PBOverrideStatus overrideStatus = PBOverrideStatusFromLoadContext(serialized);
  WebSocketChannelParent* p = new WebSocketChannelParent(
      browserParent, loadContext, overrideStatus, aSerial);
  p->AddRef();
  return p;
}

bool NeckoParent::DeallocPWebSocketParent(PWebSocketParent* actor) {
  WebSocketChannelParent* p =
      mozilla::ipc::ActorCast<WebSocketChannelParent>(actor);
  p->Release();
  return true;
}

PWebSocketEventListenerParent* NeckoParent::AllocPWebSocketEventListenerParent(
    const uint64_t& aInnerWindowID) {
  RefPtr<WebSocketEventListenerParent> c =
      new WebSocketEventListenerParent(aInnerWindowID);
  return c.forget().take();
}

mozilla::ipc::IPCResult NeckoParent::RecvPWebSocketEventListenerConstructor(
    PWebSocketEventListenerParent* aActor, const uint64_t& aInnerWindowID) {
  RefPtr<dom::WindowGlobalParent> wgp =
      dom::WindowGlobalParent::GetByInnerWindowId(aInnerWindowID);
  if (wgp && wgp->GetContentParent() == ContentParent::Cast(Manager())) {
    return IPC_OK();
  }

  if (wgp) {
    return IPC_FAIL(this, "Invalid aInnerWindowID");
  }

  (void)PWebSocketEventListenerParent::Send__delete__(aActor);
  return IPC_OK();
}

bool NeckoParent::DeallocPWebSocketEventListenerParent(
    PWebSocketEventListenerParent* aActor) {
  RefPtr<WebSocketEventListenerParent> c = dont_AddRef(
      mozilla::ipc::ActorCast<WebSocketEventListenerParent>(aActor));
  MOZ_ASSERT(c);
  return true;
}

mozilla::ipc::IPCResult NeckoParent::RecvConnectBaseChannel(
    const uint32_t& channelId) {
  RefPtr<nsBaseParentChannel> parentChannel =
      new nsBaseParentChannel(ContentParent::Cast(Manager())->GetRemoteType());

  nsCOMPtr<nsIChannel> channel;
  NS_LinkRedirectChannels(channelId, ContentParent::Cast(Manager())->ChildID(),
                          parentChannel, getter_AddRefs(channel));
  return IPC_OK();
}

#ifdef MOZ_WIDGET_ANDROID
static already_AddRefed<nsIPrincipal> GetRequestingPrincipal(
    const GeckoViewContentChannelArgs& aArgs) {
  if (aArgs.type() !=
      GeckoViewContentChannelArgs::TGeckoViewContentChannelOpenArgs) {
    return nullptr;
  }

  const GeckoViewContentChannelOpenArgs& args =
      aArgs.get_GeckoViewContentChannelOpenArgs();
  return GetRequestingPrincipal(args.loadInfo());
}

already_AddRefed<PGeckoViewContentChannelParent>
NeckoParent::AllocPGeckoViewContentChannelParent(
    PBrowserParent* aBrowser, const SerializedLoadContext& aSerialized,
    const GeckoViewContentChannelArgs& aOpenArgs) {
  nsCOMPtr<nsIPrincipal> requestingPrincipal =
      GetRequestingPrincipal(aOpenArgs);

  nsCOMPtr<nsILoadContext> loadContext;
  CreateChannelLoadContext(aBrowser, Manager(), aSerialized,
                           requestingPrincipal, loadContext);
  return MakeAndAddRef<GeckoViewContentChannelParent>(
      BrowserParent::GetFrom(aBrowser), loadContext);
}

mozilla::ipc::IPCResult NeckoParent::RecvPGeckoViewContentChannelConstructor(
    PGeckoViewContentChannelParent* actor, PBrowserParent* aBrowser,
    const SerializedLoadContext& aSerialized,
    const GeckoViewContentChannelArgs& aArgs) {
  GeckoViewContentChannelParent* p =
      static_cast<GeckoViewContentChannelParent*>(actor);
  if (!p->Init(aArgs)) {
    return IPC_FAIL(this, "Init is failed");
  }
  return IPC_OK();
}
#endif

mozilla::ipc::IPCResult NeckoParent::RecvNotifyFileChannelOpened(
    const FileChannelInfo& aInfo) {
  nsFileChannel::DoNotifyFileChannelOpened(
      ContentParent::Cast(Manager())->GetRemoteType(), aInfo);
  return IPC_OK();
}

PTCPSocketParent* NeckoParent::AllocPTCPSocketParent(
    const nsAString& /* host */, const uint16_t& /* port */) {
  // We actually don't need host/port to construct a TCPSocketParent since
  // TCPSocketParent will maintain an internal nsIDOMTCPSocket instance which
  // can be delegated to get the host/port.
  TCPSocketParent* p = new TCPSocketParent();
  p->AddIPDLReference();
  return p;
}

bool NeckoParent::DeallocPTCPSocketParent(PTCPSocketParent* actor) {
  TCPSocketParent* p = static_cast<TCPSocketParent*>(actor);
  p->ReleaseIPDLReference();
  return true;
}

already_AddRefed<PTCPServerSocketParent>
NeckoParent::AllocPTCPServerSocketParent(const uint16_t& aLocalPort,
                                         const uint16_t& aBacklog,
                                         const bool& aUseArrayBuffers) {
  return do_AddRef(
      new TCPServerSocketParent(this, aLocalPort, aBacklog, aUseArrayBuffers));
}

mozilla::ipc::IPCResult NeckoParent::RecvPTCPServerSocketConstructor(
    PTCPServerSocketParent* aActor, const uint16_t& aLocalPort,
    const uint16_t& aBacklog, const bool& aUseArrayBuffers) {
  if (!StaticPrefs::dom_tcpsocket_in_child_enabled()) {
    return IPC_FAIL(this, "tcp socket not enabled");
  }
  static_cast<TCPServerSocketParent*>(aActor)->Init();
  return IPC_OK();
}

already_AddRefed<PUDPSocketParent> NeckoParent::AllocPUDPSocketParent(
    nsIPrincipal* /* unused */, const nsACString& /* unused */) {
  return do_AddRef(new UDPSocketParent(this));
}

mozilla::ipc::IPCResult NeckoParent::RecvPUDPSocketConstructor(
    PUDPSocketParent* aActor, nsIPrincipal* aPrincipal,
    const nsACString& aFilter) {
  if (!StaticPrefs::dom_udpsocket_enabled() && aFilter.IsEmpty()) {
    return IPC_FAIL(this, "udp socket not enabled");
  }
  if (!static_cast<UDPSocketParent*>(aActor)->Init(aPrincipal, aFilter)) {
    return IPC_FAIL_NO_REASON(this);
  }
  return IPC_OK();
}

already_AddRefed<PDNSRequestParent> NeckoParent::AllocPDNSRequestParent(
    const nsACString& aHost, const nsACString& aTrrServer, const int32_t& aPort,
    const uint16_t& aType, const OriginAttributes& aOriginAttributes,
    const nsIDNSService::DNSFlags& aFlags) {
  RefPtr<DNSRequestHandler> handler = new DNSRequestHandler();
  RefPtr<DNSRequestParent> actor = new DNSRequestParent(handler);
  return actor.forget();
}

mozilla::ipc::IPCResult NeckoParent::RecvPDNSRequestConstructor(
    PDNSRequestParent* aActor, const nsACString& aHost,
    const nsACString& aTrrServer, const int32_t& aPort, const uint16_t& aType,
    const OriginAttributes& aOriginAttributes,
    const nsIDNSService::DNSFlags& aFlags) {
  if (!aTrrServer.IsEmpty()) {
    return IPC_FAIL(this, "Content process should not specify TRR server");
  }

  RefPtr<DNSRequestParent> actor =
      mozilla::ipc::ActorCast<DNSRequestParent>(aActor);
  RefPtr<DNSRequestHandler> handler =
      actor->GetDNSRequest()->AsDNSRequestHandler();
  handler->DoAsyncResolve(aHost, aTrrServer, aPort, aType, aOriginAttributes,
                          aFlags);
  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoParent::RecvSpeculativeConnect(
    PBrowserParent* aBrowser, const SerializedLoadContext& aSerialized,
    nsIURI* aURI, nsIPrincipal* aPrincipal,
    Maybe<OriginAttributes>&& aOriginAttributes, const bool& aAnonymous) {
  nsCOMPtr<nsISpeculativeConnect> speculator(gIOService);
  nsCOMPtr<nsIPrincipal> principal(aPrincipal);
  if (!aURI) {
    return IPC_FAIL(this, "aURI must not be null");
  }

  nsCOMPtr<nsILoadContext> loadContext;
  CreateChannelLoadContext(aBrowser, Manager(), aSerialized, principal,
                           loadContext);

  RefPtr<SpeculativeConnectCallbackWrapper> callback =
      new SpeculativeConnectCallbackWrapper(loadContext);

  if (speculator) {
    if (aOriginAttributes) {
      speculator->SpeculativeConnectWithOriginAttributesNative(
          aURI, std::move(aOriginAttributes.ref()), callback, aAnonymous);
    } else {
      speculator->SpeculativeConnect(aURI, principal, callback, aAnonymous);
    }
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoParent::RecvHTMLDNSPrefetch(
    const nsAString& hostname, const bool& isHttps,
    const OriginAttributes& aOriginAttributes,
    const nsIDNSService::DNSFlags& flags) {
  dom::HTMLDNSPrefetch::Prefetch(hostname, isHttps, aOriginAttributes, flags);
  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoParent::RecvHTMLDNSPrefetchBatch(
    nsTArray<HTMLDNSPrefetchArgs>&& aPrefetches) {
  for (const auto& entry : aPrefetches) {
    dom::HTMLDNSPrefetch::Prefetch(entry.hostname(), entry.isHttps(),
                                   entry.originAttributes(), entry.flags());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoParent::RecvCancelHTMLDNSPrefetch(
    const nsAString& hostname, const bool& isHttps,
    const OriginAttributes& aOriginAttributes,
    const nsIDNSService::DNSFlags& flags, const nsresult& reason) {
  dom::HTMLDNSPrefetch::CancelPrefetch(hostname, isHttps, aOriginAttributes,
                                       flags, reason);
  return IPC_OK();
}

PTransportProviderParent* NeckoParent::AllocPTransportProviderParent() {
  RefPtr<TransportProviderParent> res = new TransportProviderParent();
  return res.forget().take();
}

bool NeckoParent::DeallocPTransportProviderParent(
    PTransportProviderParent* aActor) {
  RefPtr<TransportProviderParent> provider =
      dont_AddRef(static_cast<TransportProviderParent*>(aActor));
  return true;
}

mozilla::ipc::IPCResult NeckoParent::RecvRequestContextLoadBegin(
    const uint64_t& rcid) {
  nsCOMPtr<nsIRequestContextService> rcsvc =
      RequestContextService::GetOrCreate();
  if (!rcsvc) {
    return IPC_OK();
  }
  nsCOMPtr<nsIRequestContext> rc;
  rcsvc->GetRequestContext(rcid, getter_AddRefs(rc));
  if (rc) {
    rc->BeginLoad();
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoParent::RecvRequestContextAfterDOMContentLoaded(
    const uint64_t& rcid) {
  nsCOMPtr<nsIRequestContextService> rcsvc =
      RequestContextService::GetOrCreate();
  if (!rcsvc) {
    return IPC_OK();
  }
  nsCOMPtr<nsIRequestContext> rc;
  rcsvc->GetRequestContext(rcid, getter_AddRefs(rc));
  if (rc) {
    rc->DOMContentLoaded();
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoParent::RecvRemoveRequestContext(
    const uint64_t& rcid) {
  nsCOMPtr<nsIRequestContextService> rcsvc =
      RequestContextService::GetOrCreate();
  if (!rcsvc) {
    return IPC_OK();
  }

  rcsvc->RemoveRequestContext(rcid);

  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoParent::RecvGetExtensionStream(
    nsIURI* aURI, GetExtensionStreamResolver&& aResolve) {
  if (!aURI) {
    return IPC_FAIL(this, "aURI must not be null");
  }

  RefPtr<ExtensionProtocolHandler> ph(ExtensionProtocolHandler::GetSingleton());
  MOZ_ASSERT(ph);

  // Ask the ExtensionProtocolHandler to give us a new input stream for
  // this URI. The request comes from an ExtensionProtocolHandler in the
  // child process, but is not guaranteed to be a valid moz-extension URI,
  // and not guaranteed to represent a resource that the child should be
  // allowed to access. The ExtensionProtocolHandler is responsible for
  // validating the request. Specifically, only URI's for local files that
  // an extension is allowed to access via moz-extension URI's should be
  // accepted.
  nsCOMPtr<nsIInputStream> inputStream;
  bool terminateSender = true;
  auto inputStreamOrReason = ph->NewStream(aURI, &terminateSender);
  if (inputStreamOrReason.isOk()) {
    inputStream = inputStreamOrReason.unwrap();
  }

  // If NewStream failed, we send back an invalid stream to the child so
  // it can handle the error. MozPromise rejection is reserved for channel
  // errors/disconnects.
  aResolve(inputStream);

  if (terminateSender) {
    return IPC_FAIL_NO_REASON(this);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoParent::RecvGetExtensionFD(
    nsIURI* aURI, GetExtensionFDResolver&& aResolve) {
  if (!aURI) {
    return IPC_FAIL(this, "aURI must not be null");
  }

  RefPtr<ExtensionProtocolHandler> ph(ExtensionProtocolHandler::GetSingleton());
  MOZ_ASSERT(ph);

  // Ask the ExtensionProtocolHandler to give us a new input stream for
  // this URI. The request comes from an ExtensionProtocolHandler in the
  // child process, but is not guaranteed to be a valid moz-extension URI,
  // and not guaranteed to represent a resource that the child should be
  // allowed to access. The ExtensionProtocolHandler is responsible for
  // validating the request. Specifically, only URI's for local files that
  // an extension is allowed to access via moz-extension URI's should be
  // accepted.
  bool terminateSender = true;
  auto result = ph->NewFD(aURI, &terminateSender, aResolve);

  if (result.isErr() && terminateSender) {
    return IPC_FAIL_NO_REASON(this);
  }

  if (result.isErr()) {
    FileDescriptor invalidFD;
    aResolve(invalidFD);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoParent::RecvInitSocketProcessBridge(
    InitSocketProcessBridgeResolver&& aResolver) {
  MOZ_ASSERT(NS_IsMainThread());

  // Initing the socket process bridge must be async here in order to
  // wait for the socket process launch before executing.
  auto task = [self = RefPtr{this}, resolver = std::move(aResolver)]() {
    // The content process might be already destroyed.
    if (!self->CanSend()) {
      return;
    }

    Endpoint<PSocketProcessBridgeChild> invalidEndpoint;
    if (NS_WARN_IF(self->mSocketProcessBridgeInited)) {
      resolver(std::move(invalidEndpoint));
      return;
    }

    RefPtr<SocketProcessParent> parent = SocketProcessParent::GetSingleton();
    if (NS_WARN_IF(!parent)) {
      resolver(std::move(invalidEndpoint));
      return;
    }

    Endpoint<PSocketProcessBridgeParent> parentEndpoint;
    Endpoint<PSocketProcessBridgeChild> childEndpoint;
    if (NS_WARN_IF(NS_FAILED(PSocketProcessBridge::CreateEndpoints(
            parent->OtherEndpointProcInfo(),
            self->Manager()->OtherEndpointProcInfo(), &parentEndpoint,
            &childEndpoint)))) {
      resolver(std::move(invalidEndpoint));
      return;
    }

    if (NS_WARN_IF(!parent->SendInitSocketProcessBridgeParent(
            self->Manager()->OtherPid(), std::move(parentEndpoint)))) {
      resolver(std::move(invalidEndpoint));
      return;
    }

    resolver(std::move(childEndpoint));
    self->mSocketProcessBridgeInited = true;
  };
  gIOService->CallOrWaitForSocketProcess(std::move(task));

  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoParent::RecvResetSocketProcessBridge() {
  // SendResetSocketProcessBridge is called from
  // SocketProcessBridgeChild::ActorDestroy if the socket process
  // crashes.  This is necessary in order to properly initialize the
  // restarted socket process.
  mSocketProcessBridgeInited = false;
  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoParent::RecvEnsureHSTSData(
    EnsureHSTSDataResolver&& aResolver) {
  auto callback = [aResolver{std::move(aResolver)}](bool aResult) {
    aResolver(aResult);
  };
  RefPtr<HSTSDataCallbackWrapper> wrapper =
      new HSTSDataCallbackWrapper(std::move(callback));
  gHttpHandler->EnsureHSTSDataReadyNative(wrapper);
  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoParent::RecvGetPageThumbStream(
    nsIURI* aURI, const LoadInfoArgs& aLoadInfoArgs,
    GetPageThumbStreamResolver&& aResolver) {
  // Only the privileged about content process is allowed to access
  // things over the moz-page-thumb protocol. Any other content process
  // that tries to send this should have been blocked via the
  // ScriptSecurityManager, but if somehow the process has been tricked into
  // sending this message, we send IPC_FAIL in order to crash that
  // likely-compromised content process.
  if (mozilla::ipc::ActorCast<ContentParent>(Manager())->GetRemoteType() !=
      PRIVILEGEDABOUT_REMOTE_TYPE) {
    return IPC_FAIL(this, "Wrong process type");
  }

  nsCOMPtr<nsILoadInfo> loadInfo;
  nsresult rv = mozilla::ipc::LoadInfoArgsToLoadInfo(
      aLoadInfoArgs, PRIVILEGEDABOUT_REMOTE_TYPE, getter_AddRefs(loadInfo));
  if (NS_FAILED(rv)) {
    return IPC_FAIL(this, "moz-page-thumb request must include loadInfo");
  }

  RefPtr<PageThumbProtocolHandler> ph(PageThumbProtocolHandler::GetSingleton());
  MOZ_ASSERT(ph);

  // Ask the PageThumbProtocolHandler to give us a new input stream for
  // this URI. The request comes from a PageThumbProtocolHandler in the
  // child process, but is not guaranteed to be a valid moz-page-thumb URI,
  // and not guaranteed to represent a resource that the child should be
  // allowed to access. The PageThumbProtocolHandler is responsible for
  // validating the request.
  nsCOMPtr<nsIInputStream> inputStream;
  bool terminateSender = true;
  auto inputStreamPromise = ph->NewStream(aURI, loadInfo, &terminateSender);

  if (terminateSender) {
    return IPC_FAIL(this, "Malformed moz-page-thumb request");
  }

  inputStreamPromise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [aResolver](const RemoteStreamInfo& aInfo) { aResolver(Some(aInfo)); },
      [aResolver](nsresult aRv) {
        // If NewStream failed, we send back an invalid stream to the child so
        // it can handle the error. MozPromise rejection is reserved for channel
        // errors/disconnects.
        (void)NS_WARN_IF(NS_FAILED(aRv));
        aResolver(Nothing());
      });

  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoParent::RecvGetMozNewTabWallpaperStream(
    nsIURI* aURI, const LoadInfoArgs& aLoadInfoArgs,
    GetMozNewTabWallpaperStreamResolver&& aResolver) {
  // Only the privileged about content process is allowed to access
  // things over the moz-newtab-wallpaper protocol. Any other content process
  // that tries to send this should have been blocked via the
  // ScriptSecurityManager, but if somehow the process has been tricked into
  // sending this message, we send IPC_FAIL in order to crash that
  // likely-compromised content process.
  if (mozilla::ipc::ActorCast<ContentParent>(Manager())->GetRemoteType() !=
      PRIVILEGEDABOUT_REMOTE_TYPE) {
    return IPC_FAIL(this, "Wrong process type");
  }

  nsCOMPtr<nsILoadInfo> loadInfo;
  nsresult rv = mozilla::ipc::LoadInfoArgsToLoadInfo(
      aLoadInfoArgs, PRIVILEGEDABOUT_REMOTE_TYPE, getter_AddRefs(loadInfo));
  if (NS_FAILED(rv)) {
    return IPC_FAIL(this, "moz-newtab-wallpaper request must include loadInfo");
  }

  RefPtr<net::MozNewTabWallpaperProtocolHandler> ph(
      net::MozNewTabWallpaperProtocolHandler::GetSingleton());
  MOZ_ASSERT(ph);

  // Ask the MozNewTabWallpaperProtocolHandler to give us a new input stream for
  // this URI. The request comes from a MozNewTabWallpaperProtocolHandler in the
  // child process, but is not guaranteed to be a valid moz-newtab-wallpaper
  // URI, and not guaranteed to represent a resource that the child should be
  // allowed to access. The MozNewTabWallpaperProtocolHandler is responsible for
  // validating the request.
  nsCOMPtr<nsIInputStream> inputStream;
  bool terminateSender = true;
  auto inputStreamPromise = ph->NewStream(aURI, loadInfo, &terminateSender);

  if (terminateSender) {
    return IPC_FAIL(this, "Malformed moz-newtab-wallpaper request");
  }

  inputStreamPromise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [aResolver](const RemoteStreamInfo& aInfo) { aResolver(Some(aInfo)); },
      [aResolver](nsresult aRv) {
        // If NewStream failed, we send back an invalid stream to the child so
        // it can handle the error. MozPromise rejection is reserved for channel
        // errors/disconnects.
        (void)NS_WARN_IF(NS_FAILED(aRv));
        aResolver(Nothing());
      });

  return IPC_OK();
}

mozilla::ipc::IPCResult NeckoParent::RecvGetPageIconStream(
    nsIURI* aURI, const LoadInfoArgs& aLoadInfoArgs,
    GetPageIconStreamResolver&& aResolver) {
#ifdef MOZ_PLACES
  const nsACString& remoteType =
      ContentParent::Cast(Manager())->GetRemoteType();

  // Only the privileged about content process is allowed to access
  // things over the page-icon protocol. Any other content process
  // that tries to send this should have been blocked via the
  // ScriptSecurityManager, but if somehow the process has been tricked into
  // sending this message, we send IPC_FAIL in order to crash that
  // likely-compromised content process.
  if (remoteType != PRIVILEGEDABOUT_REMOTE_TYPE) {
    return IPC_FAIL(this, "Wrong process type");
  }

  nsCOMPtr<nsILoadInfo> loadInfo;
  nsresult rv = mozilla::ipc::LoadInfoArgsToLoadInfo(aLoadInfoArgs, remoteType,
                                                     getter_AddRefs(loadInfo));
  if (NS_FAILED(rv)) {
    return IPC_FAIL(this, "Page-icon request must include loadInfo");
  }

  RefPtr<PageIconProtocolHandler> ph(PageIconProtocolHandler::GetSingleton());
  MOZ_ASSERT(ph);

  nsCOMPtr<nsIInputStream> inputStream;
  bool terminateSender = true;
  auto inputStreamPromise = ph->NewStream(aURI, loadInfo, &terminateSender);

  if (terminateSender) {
    return IPC_FAIL(this, "Malformed page-icon request");
  }

  inputStreamPromise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [aResolver](const RemoteStreamInfo& aInfo) { aResolver(Some(aInfo)); },
      [aResolver](nsresult aRv) {
        // If NewStream failed, we send back an invalid stream to the child so
        // it can handle the error. MozPromise rejection is reserved for channel
        // errors/disconnects.
        (void)NS_WARN_IF(NS_FAILED(aRv));
        aResolver(Nothing());
      });

  return IPC_OK();
#else
  return IPC_FAIL(this, "page-icon: protocol unavailable");
#endif
}

/* static */
RefPtr<RemoteStreamPromise> NeckoParent::CreateRemoteStreamForResolvedURI(
    nsIURI* aChildURI, const nsACString& aResolvedSpec,
    const nsACString& aDefaultMimeType) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsParentProcess());

  nsresult rv;

  nsAutoCString resolvedScheme;
  rv = net_ExtractURLScheme(aResolvedSpec, resolvedScheme);
  if (NS_FAILED(rv) || !resolvedScheme.EqualsLiteral("file")) {
    return RemoteStreamPromise::CreateAndReject(NS_ERROR_UNEXPECTED, __func__);
  }

  MOZ_ASSERT(resolvedScheme.EqualsLiteral("file"),
             "CreateRemoteStreamForResolvedURI requires file:// URI");

  nsCOMPtr<nsIIOService> ioService = do_GetIOService(&rv);
  if (NS_FAILED(rv)) {
    return RemoteStreamPromise::CreateAndReject(rv, __func__);
  }

  nsCOMPtr<nsIURI> resolvedURI;
  rv = ioService->NewURI(aResolvedSpec, nullptr, nullptr,
                         getter_AddRefs(resolvedURI));
  if (NS_FAILED(rv)) {
    return RemoteStreamPromise::CreateAndReject(rv, __func__);
  }

  // Load local file resources for internal protocol handlers (moz-page-thumb,
  // moz-newtab-wallpaper). resolvedURI must be file:// scheme pointing to the
  // profile directory. Callers validate the original URI scheme/host and use
  // internal path resolution (PageThumbsStorageService, profile/wallpaper/)
  // before calling this method.
  nsCOMPtr<nsIChannel> channel;
  nsCOMPtr<nsIPrincipal> nullPrincipal =
      NullPrincipal::CreateWithoutOriginAttributes();
  rv = NS_NewChannel(getter_AddRefs(channel), resolvedURI, nullPrincipal,
                     nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
                     nsIContentPolicy::TYPE_IMAGE);
  if (NS_FAILED(rv)) {
    return RemoteStreamPromise::CreateAndReject(rv, __func__);
  }

  auto promiseHolder = MakeUnique<MozPromiseHolder<RemoteStreamPromise>>();
  RefPtr<RemoteStreamPromise> promise = promiseHolder->Ensure(__func__);

  nsCOMPtr<nsIMIMEService> mime = do_GetService("@mozilla.org/mime;1", &rv);
  if (NS_FAILED(rv)) {
    return RemoteStreamPromise::CreateAndReject(rv, __func__);
  }

  nsAutoCString contentType;
  rv = mime->GetTypeFromURI(aChildURI, contentType);
  if (NS_FAILED(rv)) {
    if (!aDefaultMimeType.IsEmpty()) {
      contentType = aDefaultMimeType;
    } else {
      return RemoteStreamPromise::CreateAndReject(rv, __func__);
    }
  }

  rv = NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          "NeckoParent::CreateRemoteStreamForResolvedURI",
          [contentType, channel, holder = std::move(promiseHolder)]() {
            nsresult rv;

            nsCOMPtr<nsIFileChannel> fileChannel =
                do_QueryInterface(channel, &rv);
            if (NS_FAILED(rv)) {
              holder->Reject(rv, __func__);
              return;
            }

            nsCOMPtr<nsIFile> requestedFile;
            rv = fileChannel->GetFile(getter_AddRefs(requestedFile));
            if (NS_FAILED(rv)) {
              holder->Reject(rv, __func__);
              return;
            }

            nsCOMPtr<nsIInputStream> inputStream;
            rv = NS_NewLocalFileInputStream(getter_AddRefs(inputStream),
                                            requestedFile, PR_RDONLY, -1);
            if (NS_FAILED(rv)) {
              holder->Reject(rv, __func__);
              return;
            }

            RemoteStreamInfo info(inputStream, contentType, -1);

            holder->Resolve(std::move(info), __func__);
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

  if (NS_FAILED(rv)) {
    return RemoteStreamPromise::CreateAndReject(rv, __func__);
  }

  return promise;
}

}  // namespace net
}  // namespace mozilla
