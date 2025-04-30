/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/BasePrincipal.h"
#include "mozilla/net/PNeckoParent.h"
#include "mozilla/net/NeckoCommon.h"
#include "nsIAuthPrompt2.h"
#include "nsINetworkPredictor.h"
#include "nsNetUtil.h"

#ifndef mozilla_net_NeckoParent_h
#  define mozilla_net_NeckoParent_h

namespace mozilla {
namespace net {

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

 protected:
  virtual ~NeckoParent() = default;

  bool mSocketProcessBridgeInited;

  already_AddRefed<PHttpChannelParent> AllocPHttpChannelParent(
      PBrowserParent*, const SerializedLoadContext&,
      const HttpChannelCreationArgs& aOpenArgs);
  virtual mozilla::ipc::IPCResult RecvPHttpChannelConstructor(
      PHttpChannelParent* aActor, PBrowserParent* aBrowser,
      const SerializedLoadContext& aSerialized,
      const HttpChannelCreationArgs& aOpenArgs) override;

  PStunAddrsRequestParent* AllocPStunAddrsRequestParent();
  bool DeallocPStunAddrsRequestParent(PStunAddrsRequestParent* aActor);

  PWebrtcTCPSocketParent* AllocPWebrtcTCPSocketParent(
      const Maybe<TabId>& aTabId);
  bool DeallocPWebrtcTCPSocketParent(PWebrtcTCPSocketParent* aActor);

  PAltDataOutputStreamParent* AllocPAltDataOutputStreamParent(
      const nsACString& type, const int64_t& predictedSize,
      PHttpChannelParent* channel);
  bool DeallocPAltDataOutputStreamParent(PAltDataOutputStreamParent* aActor);

  bool DeallocPCookieServiceParent(PCookieServiceParent*);
  PWebSocketParent* AllocPWebSocketParent(
      PBrowserParent* browser, const SerializedLoadContext& aSerialized,
      const uint32_t& aSerial);
  bool DeallocPWebSocketParent(PWebSocketParent*);
  PTCPSocketParent* AllocPTCPSocketParent(const nsAString& host,
                                          const uint16_t& port);

  already_AddRefed<PDocumentChannelParent> AllocPDocumentChannelParent(
      const dom::MaybeDiscarded<dom::BrowsingContext>& aContext,
      const DocumentChannelCreationArgs& args);
  virtual mozilla::ipc::IPCResult RecvPDocumentChannelConstructor(
      PDocumentChannelParent* aActor,
      const dom::MaybeDiscarded<dom::BrowsingContext>& aContext,
      const DocumentChannelCreationArgs& aArgs) override;
  bool DeallocPDocumentChannelParent(PDocumentChannelParent* channel);

  bool DeallocPTCPSocketParent(PTCPSocketParent*);
  PTCPServerSocketParent* AllocPTCPServerSocketParent(
      const uint16_t& aLocalPort, const uint16_t& aBacklog,
      const bool& aUseArrayBuffers);
  virtual mozilla::ipc::IPCResult RecvPTCPServerSocketConstructor(
      PTCPServerSocketParent*, const uint16_t& aLocalPort,
      const uint16_t& aBacklog, const bool& aUseArrayBuffers) override;
  bool DeallocPTCPServerSocketParent(PTCPServerSocketParent*);
  PUDPSocketParent* AllocPUDPSocketParent(nsIPrincipal* aPrincipal,
                                          const nsACString& aFilter);
  virtual mozilla::ipc::IPCResult RecvPUDPSocketConstructor(
      PUDPSocketParent*, nsIPrincipal* aPrincipal,
      const nsACString& aFilter) override;
  bool DeallocPUDPSocketParent(PUDPSocketParent*);
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
      nsIURI* aURI, nsIPrincipal* aPrincipal,
      Maybe<OriginAttributes>&& aOriginAttributes, const bool& aAnonymous);
  mozilla::ipc::IPCResult RecvHTMLDNSPrefetch(
      const nsAString& hostname, const bool& isHttps,
      const OriginAttributes& aOriginAttributes,
      const nsIDNSService::DNSFlags& flags);
  mozilla::ipc::IPCResult RecvCancelHTMLDNSPrefetch(
      const nsAString& hostname, const bool& isHttps,
      const OriginAttributes& aOriginAttributes,
      const nsIDNSService::DNSFlags& flags, const nsresult& reason);
  PWebSocketEventListenerParent* AllocPWebSocketEventListenerParent(
      const uint64_t& aInnerWindowID);
  bool DeallocPWebSocketEventListenerParent(PWebSocketEventListenerParent*);

  already_AddRefed<PDataChannelParent> AllocPDataChannelParent(
      const uint32_t& channelId);

  virtual mozilla::ipc::IPCResult RecvPDataChannelConstructor(
      PDataChannelParent* aActor, const uint32_t& channelId) override;
#  ifdef MOZ_WIDGET_GTK
  PGIOChannelParent* AllocPGIOChannelParent(
      PBrowserParent* aBrowser, const SerializedLoadContext& aSerialized,
      const GIOChannelCreationArgs& aOpenArgs);
  bool DeallocPGIOChannelParent(PGIOChannelParent* channel);

  virtual mozilla::ipc::IPCResult RecvPGIOChannelConstructor(
      PGIOChannelParent* aActor, PBrowserParent* aBrowser,
      const SerializedLoadContext& aSerialized,
      const GIOChannelCreationArgs& aOpenArgs) override;
#  endif
  PSimpleChannelParent* AllocPSimpleChannelParent(const uint32_t& channelId);
  bool DeallocPSimpleChannelParent(PSimpleChannelParent* actor);

  virtual mozilla::ipc::IPCResult RecvPSimpleChannelConstructor(
      PSimpleChannelParent* aActor, const uint32_t& channelId) override;

  already_AddRefed<PFileChannelParent> AllocPFileChannelParent();

  virtual mozilla::ipc::IPCResult RecvPFileChannelConstructor(
      PFileChannelParent* aActor) override;

  PTransportProviderParent* AllocPTransportProviderParent();
  bool DeallocPTransportProviderParent(PTransportProviderParent* aActor);

  /* Predictor Messages */
  mozilla::ipc::IPCResult RecvPredPredict(
      nsIURI* aTargetURI, nsIURI* aSourceURI,
      const PredictorPredictReason& aReason,
      const OriginAttributes& aOriginAttributes, const bool& hasVerifier);

  mozilla::ipc::IPCResult RecvPredLearn(
      nsIURI* aTargetURI, nsIURI* aSourceURI,
      const PredictorPredictReason& aReason,
      const OriginAttributes& aOriginAttributes);
  mozilla::ipc::IPCResult RecvPredReset();

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

  mozilla::ipc::IPCResult RecvInitSocketProcessBridge(
      InitSocketProcessBridgeResolver&& aResolver);
  mozilla::ipc::IPCResult RecvResetSocketProcessBridge();

  mozilla::ipc::IPCResult RecvEnsureHSTSData(
      EnsureHSTSDataResolver&& aResolver);
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_NeckoParent_h
