/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_UDPSocketParent_h_
#define mozilla_dom_UDPSocketParent_h_

#include "mozilla/dom/PermissionMessageUtils.h"
#include "mozilla/net/PUDPSocketParent.h"
#include "nsCOMPtr.h"
#include "nsISocketFilter.h"
#include "nsIUDPSocket.h"

namespace mozilla {
namespace net {
class PNeckoParent;
}  // namespace net

namespace dom {

class UDPSocketParent : public mozilla::net::PUDPSocketParent,
                        public nsIUDPSocketListener {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIUDPSOCKETLISTENER

  explicit UDPSocketParent(PBackgroundParent* aManager);
  explicit UDPSocketParent(PNeckoParent* aManager);

  bool Init(nsIPrincipal* aPrincipal, const nsACString& aFilter);

  mozilla::ipc::IPCResult RecvBind(const UDPAddressInfo& aAddressInfo,
                                   const bool& aAddressReuse,
                                   const bool& aLoopback,
                                   const uint32_t& recvBufferSize,
                                   const uint32_t& sendBufferSize) override;
  mozilla::ipc::IPCResult RecvConnect(
      const UDPAddressInfo& aAddressInfo) override;
  void DoSendConnectResponse(const UDPAddressInfo& aAddressInfo);
  void SendConnectResponse(const nsCOMPtr<nsIEventTarget>& aThread,
                           const UDPAddressInfo& aAddressInfo);
  void DoConnect(const nsCOMPtr<nsIUDPSocket>& aSocket,
                 const nsCOMPtr<nsIEventTarget>& aReturnThread,
                 const UDPAddressInfo& aAddressInfo);

  mozilla::ipc::IPCResult RecvOutgoingData(const UDPData& aData,
                                           const UDPSocketAddr& aAddr) override;

  mozilla::ipc::IPCResult RecvClose() override;
  mozilla::ipc::IPCResult RecvRequestDelete() override;
  mozilla::ipc::IPCResult RecvJoinMulticast(
      const nsACString& aMulticastAddress,
      const nsACString& aInterface) override;
  mozilla::ipc::IPCResult RecvLeaveMulticast(
      const nsACString& aMulticastAddress,
      const nsACString& aInterface) override;

 private:
  virtual ~UDPSocketParent();

  virtual void ActorDestroy(ActorDestroyReason why) override;
  void Send(const nsTArray<uint8_t>& aData, const UDPSocketAddr& aAddr);
  void Send(const IPCStream& aStream, const UDPSocketAddr& aAddr);
  nsresult BindInternal(const nsCString& aHost, const uint16_t& aPort,
                        const bool& aAddressReuse, const bool& aLoopback,
                        const uint32_t& recvBufferSize,
                        const uint32_t& sendBufferSize);
  nsresult ConnectInternal(const nsCOMPtr<nsIUDPSocket>& aSocket,
                           const nsCString& aHost, const uint16_t& aPort);
  void FireInternalError(uint32_t aLineNo);
  void SendInternalError(const nsCOMPtr<nsIEventTarget>& aThread,
                         uint32_t aLineNo);

  PBackgroundParent* mBackgroundManager;

  bool mIPCOpen;
  nsCOMPtr<nsIUDPSocket> mSocket;
  nsCOMPtr<nsISocketFilter> mFilter;
  nsCOMPtr<nsIPrincipal> mPrincipal;
  UDPAddressInfo mAddress;
};

}  // namespace dom
}  // namespace mozilla

#endif  // !defined(mozilla_dom_UDPSocketParent_h_)
