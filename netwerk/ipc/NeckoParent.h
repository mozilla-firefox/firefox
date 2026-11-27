/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_NeckoParent_h
#define mozilla_net_NeckoParent_h

#include "mozilla/BasePrincipal.h"
#include "mozilla/MozPromise.h"
#include "mozilla/net/NeckoCommon.h"
#include "mozilla/net/PNeckoParent.h"
#include "nsIAuthPrompt2.h"
#include "nsIInterfaceRequestor.h"
#include "nsNetUtil.h"

namespace mozilla {
namespace net {

class RemoteStreamInfo;
using RemoteStreamPromise =
    mozilla::MozPromise<RemoteStreamInfo, nsresult, false>;

// Used to override channel Private Browsing status if needed.
enum PBOverrideStatus {
  kPBOverride_Unset = 0,
  kPBOverride_Private,
  kPBOverride_NotPrivate
};

// Header file contents
class NeckoParent : public PNeckoParent {
  friend class PNeckoParent;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(NeckoParent, override)

  NeckoParent();

  static void GetValidatedOriginAttributes(
      const SerializedLoadContext& aSerialized, PContentParent* aContent,
      nsIPrincipal* aRequestingPrincipal, mozilla::OriginAttributes& aAttrs);

  /*
   * Creates LoadContext for parent-side of an e10s channel.
   *
   * PContentParent corresponds to the process that is requesting the load.
   */
  static void CreateChannelLoadContext(PBrowserParent* aBrowser,
                                       PContentParent* aContent,
                                       const SerializedLoadContext& aSerialized,
                                       nsIPrincipal* aRequestingPrincipal,
                                       nsCOMPtr<nsILoadContext>& aResult);

  virtual void ActorDestroy(ActorDestroyReason aWhy) override;
  PCookieServiceParent* AllocPCookieServiceParent();
  virtual mozilla::ipc::IPCResult RecvPCookieServiceConstructor(
      PCookieServiceParent* aActor) override {
    return PNeckoParent::RecvPCookieServiceConstructor(aActor);
  }

  /*
   * Helper method to create a remote stream from a resolved file URI.
   * Shared by PageThumbProtocolHandler and MozNewTabWallpaperProtocolHandler.
   */
  static RefPtr<RemoteStreamPromise> CreateRemoteStreamForResolvedURI(
      nsIURI* aChildURI, const nsACString& aResolvedSpec,
      const nsACString& aDefaultMimeType);

 protected:
  virtual ~NeckoParent() = default;

  already_AddRefed<PHttpChannelParent> AllocPHttpChannelParent(
      PBrowserParent*, const SerializedLoadContext&,
      const HttpChannelCreationArgs& aOpenArgs);
  virtual mozilla::ipc::IPCResult RecvPHttpChannelConstructor(
      PHttpChannelParent* aActor, PBrowserParent* aBrowser,
      const SerializedLoadContext& aSerialized,
      const HttpChannelCreationArgs& aOpenArgs) override;

  already_AddRefed<PStunAddrsRequestParent> AllocPStunAddrsRequestParent();

  already_AddRefed<PWebrtcTCPSocketParent> AllocPWebrtcTCPSocketParent(
      const Maybe<TabId>& aTabId);

  PCacheEntryWriteHandleParent* AllocPCacheEntryWriteHandleParent(
      PHttpChannelParent* channel);
  bool DeallocPCacheEntryWriteHandleParent(
      PCacheEntryWriteHandleParent* aActor);

  PAltDataOutputStreamParent* AllocPAltDataOutputStreamParent(
      const nsACString& type, const int64_t& predictedSize,
      mozilla::Maybe<mozilla::NotNull<mozilla::net::PHttpChannelParent*>>&
          channel,
      mozilla::Maybe<mozilla::NotNull<PCacheEntryWriteHandleParent*>>& handle);
  bool DeallocPAltDataOutputStreamParent(PAltDataOutputStreamParent* aActor);

  bool DeallocPCookieServiceParent(PCookieServiceParent*);
  PWebSocketParent* AllocPWebSocketParent(
      PBrowserParent* browser, const SerializedLoadContext& aSerialized,
      const uint32_t& aSerial);
  bool DeallocPWebSocketParent(PWebSocketParent*);

  already_AddRefed<PDocumentChannelParent> AllocPDocumentChannelParent(
      const dom::MaybeDiscarded<dom::BrowsingContext>& aContext,
      const DocumentChannelCreationArgs& args);
  virtual mozilla::ipc::IPCResult RecvPDocumentChannelConstructor(
      PDocumentChannelParent* aActor,
      const dom::MaybeDiscarded<dom::BrowsingContext>& aContext,
      const DocumentChannelCreationArgs& aArgs) override;
  bool DeallocPDocumentChannelParent(PDocumentChannelParent* channel);

  already_AddRefed<PTCPServerSocketParent> AllocPTCPServerSocketParent(
      const uint16_t& aLocalPort, const uint16_t& aBacklog,
      const bool& aUseArrayBuffers);
  virtual mozilla::ipc::IPCResult RecvPTCPServerSocketConstructor(
      PTCPServerSocketParent*, const uint16_t& aLocalPort,
      const uint16_t& aBacklog, const bool& aUseArrayBuffers) override;

  PTCPSocketParent* AllocPTCPSocketParent(const nsAString& host,
                                          const uint16_t& port);
  bool DeallocPTCPSocketParent(PTCPSocketParent*);

  already_AddRefed<PUDPSocketParent> AllocPUDPSocketParent(
      nsIPrincipal* aPrincipal, const nsACString& aFilter);
  virtual mozilla::ipc::IPCResult RecvPUDPSocketConstructor(
      PUDPSocketParent*, nsIPrincipal* aPrincipal,
      const nsACString& aFilter) override;

  already_AddRefed<PDNSRequestParent> AllocPDNSRequestParent(
      const nsACString& aHost, const nsACString& aTrrServer,
      const int32_t& aPort, const uint16_t& aType,
      const OriginAttributes& aOriginAttributes,
      const nsIDNSService::DNSFlags& aFlags);
  virtual mozilla::ipc::IPCResult RecvPDNSRequestConstructor(
      PDNSRequestParent* actor, const nsACString& aHost,
      const nsACString& trrServer, const int32_t& aPort, const uint16_t& type,
      const OriginAttributes& aOriginAttributes,
      const nsIDNSService::DNSFlags& flags) override;
  mozilla::ipc::IPCResult RecvSpeculativeConnect(
      PBrowserParent* aBrowser, const IPC::SerializedLoadContext& aSerialized,
      nsIURI* aURI, nsIPrincipal* aPrincipal,
      Maybe<OriginAttributes>&& aOriginAttributes, const bool& aAnonymous);
  mozilla::ipc::IPCResult RecvHTMLDNSPrefetch(
      const nsACString& hostname, const OriginAttributes& aOriginAttributes,
      const nsIDNSService::DNSFlags& flags);
  mozilla::ipc::IPCResult RecvHTMLDNSPrefetchBatch(
      nsTArray<HTMLDNSPrefetchArgs>&& aPrefetches);
  mozilla::ipc::IPCResult RecvCancelHTMLDNSPrefetch(
      const nsACString& hostname, const OriginAttributes& aOriginAttributes,
      const nsIDNSService::DNSFlags& flags, const nsresult& reason);
  PWebSocketEventListenerParent* AllocPWebSocketEventListenerParent(
      const uint64_t& aInnerWindowID);
  virtual mozilla::ipc::IPCResult RecvPWebSocketEventListenerConstructor(
      PWebSocketEventListenerParent* aActor,
      const uint64_t& aInnerWindowID) override;
  bool DeallocPWebSocketEventListenerParent(PWebSocketEventListenerParent*);

  mozilla::ipc::IPCResult RecvConnectBaseChannel(const uint32_t& channelId);

#ifdef MOZ_WIDGET_ANDROID
  already_AddRefed<PGeckoViewContentChannelParent>
  AllocPGeckoViewContentChannelParent(
      PBrowserParent* aBrowser, const SerializedLoadContext& aSerialized,
      const GeckoViewContentChannelArgs& aOpenArgs);

  virtual mozilla::ipc::IPCResult RecvPGeckoViewContentChannelConstructor(
      PGeckoViewContentChannelParent* aActor, PBrowserParent* aBrowser,
      const SerializedLoadContext& aSerialized,
      const GeckoViewContentChannelArgs& args) override;
#endif

  mozilla::ipc::IPCResult RecvNotifyFileChannelOpened(
      const FileChannelInfo& aInfo);

  PTransportProviderParent* AllocPTransportProviderParent();
  bool DeallocPTransportProviderParent(PTransportProviderParent* aActor);

  mozilla::ipc::IPCResult RecvRequestContextLoadBegin(const uint64_t& rcid);
  mozilla::ipc::IPCResult RecvRequestContextAfterDOMContentLoaded(
      const uint64_t& rcid);
  mozilla::ipc::IPCResult RecvRemoveRequestContext(const uint64_t& rcid);

  /* WebExtensions */
  mozilla::ipc::IPCResult RecvGetExtensionStream(
      nsIURI* aURI, GetExtensionStreamResolver&& aResolve);

  mozilla::ipc::IPCResult RecvGetExtensionFD(nsIURI* aURI,
                                             GetExtensionFDResolver&& aResolve);

  /* Page thumbnails remote resource loading */
  mozilla::ipc::IPCResult RecvGetPageThumbStream(
      nsIURI* aURI, const LoadInfoArgs& aLoadInfoArgs,
      GetPageThumbStreamResolver&& aResolve);

  /* Page icon remote resource loading */
  mozilla::ipc::IPCResult RecvGetPageIconStream(
      nsIURI* aURI, const LoadInfoArgs& aLoadInfoArgs,
      GetPageIconStreamResolver&& aResolve);

  /* New Tab wallpaper remote resource loading */
  mozilla::ipc::IPCResult RecvGetMozNewTabWallpaperStream(
      nsIURI* aURI, const LoadInfoArgs& aLoadInfoArgs,
      GetMozNewTabWallpaperStreamResolver&& aResolve);

#ifdef MOZ_THUNDERBIRD
  mozilla::ipc::IPCResult RecvGetMailMessageStream(
      nsIURI* aURI, const LoadInfoArgs& aLoadInfoArgs,
      GetMailMessageStreamResolver&& aResolve);
#endif

  mozilla::ipc::IPCResult RecvEnsureHSTSData(
      EnsureHSTSDataResolver&& aResolver);
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_NeckoParent_h
