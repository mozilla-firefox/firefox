/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "UDPSocketParent.h"

#include "UDPSocket.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/ipc/InputStreamUtils.h"
#include "mozilla/ipc/PBackgroundParent.h"
#include "mozilla/net/DNS.h"
#include "mozilla/net/NeckoCommon.h"
#include "mozilla/net/PNeckoParent.h"
#include "nsComponentManagerUtils.h"
#include "nsINetAddr.h"
#include "nsIPermissionManager.h"
#include "nsIUDPSocket.h"
#include "nsNetCID.h"
#include "transport/runnable_utils.h"

namespace mozilla {

using namespace net;

namespace dom {

NS_IMPL_ISUPPORTS(UDPSocketParent, nsIUDPSocketListener)

UDPSocketParent::UDPSocketParent(PBackgroundParent* aManager)
    : mBackgroundManager(aManager), mIPCOpen(true) {}

UDPSocketParent::UDPSocketParent(PNeckoParent* aManager)
    : mBackgroundManager(nullptr), mIPCOpen(true) {}

UDPSocketParent::~UDPSocketParent() = default;

bool UDPSocketParent::Init(nsIPrincipal* aPrincipal,
                           const nsACString& aFilter) {
  MOZ_ASSERT_IF(mBackgroundManager, !aPrincipal);
  // will be used once we move all UDPSocket to PBackground, or
  // if we add in Principal checking for dom/media/webrtc/transport
  (void)mBackgroundManager;

  mPrincipal = aPrincipal;

  if (!aFilter.IsEmpty()) {
    nsAutoCString contractId(NS_NETWORK_UDP_SOCKET_FILTER_HANDLER_PREFIX);
    contractId.Append(aFilter);
    nsCOMPtr<nsISocketFilterHandler> filterHandler =
        do_GetService(contractId.get());
    if (filterHandler) {
      nsresult rv = filterHandler->NewFilter(getter_AddRefs(mFilter));
      if (NS_FAILED(rv)) {
        printf_stderr(
            "Cannot create filter that content specified. "
            "filter name: %s, error code: %u.",
            PromiseFlatCString(aFilter).get(), static_cast<uint32_t>(rv));
        return false;
      }
    } else {
      printf_stderr(
          "Content doesn't have a valid filter. "
          "filter name: %s.",
          PromiseFlatCString(aFilter).get());
      return false;
    }
  }

  return true;
}

// PUDPSocketParent methods

mozilla::ipc::IPCResult UDPSocketParent::RecvBind(
    const UDPAddressInfo& aAddressInfo, const bool& aAddressReuse,
    const bool& aLoopback, const uint32_t& recvBufferSize,
    const uint32_t& sendBufferSize) {
  UDPSOCKET_LOG(("%s: %s:%u", __FUNCTION__, aAddressInfo.addr().get(),
                 aAddressInfo.port()));

  if (NS_FAILED(BindInternal(aAddressInfo.addr(), aAddressInfo.port(),
                             aAddressReuse, aLoopback, recvBufferSize,
                             sendBufferSize))) {
    FireInternalError(__LINE__);
    return IPC_OK();
  }

  nsCOMPtr<nsINetAddr> localAddr;
  mSocket->GetLocalAddr(getter_AddRefs(localAddr));

  nsCString addr;
  if (NS_FAILED(localAddr->GetAddress(addr))) {
    FireInternalError(__LINE__);
    return IPC_OK();
  }

  uint16_t port;
  if (NS_FAILED(localAddr->GetPort(&port))) {
    FireInternalError(__LINE__);
    return IPC_OK();
  }

  UDPSOCKET_LOG(
      ("%s: SendCallbackOpened: %s:%u", __FUNCTION__, addr.get(), port));
  mAddress = {addr, port};
  (void)SendCallbackOpened(UDPAddressInfo(addr, port));

  return IPC_OK();
}

nsresult UDPSocketParent::BindInternal(const nsCString& aHost,
                                       const uint16_t& aPort,
                                       const bool& aAddressReuse,
                                       const bool& aLoopback,
                                       const uint32_t& recvBufferSize,
                                       const uint32_t& sendBufferSize) {
  nsresult rv;

  UDPSOCKET_LOG(
      ("%s: [this=%p] %s:%u addressReuse: %d loopback: %d recvBufferSize: "
       "%" PRIu32 ", sendBufferSize: %" PRIu32,
       __FUNCTION__, this, nsCString(aHost).get(), aPort, aAddressReuse,
       aLoopback, recvBufferSize, sendBufferSize));

  nsCOMPtr<nsIUDPSocket> sock =
      do_CreateInstance("@mozilla.org/network/udp-socket;1", &rv);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (aHost.IsEmpty()) {
    rv = sock->Init(aPort, false, mPrincipal, aAddressReuse,
                    /* optional_argc = */ 1);
  } else {
    PRNetAddr prAddr;
    PR_InitializeNetAddr(PR_IpAddrAny, aPort, &prAddr);
    PRStatus status = PR_StringToNetAddr(aHost.get(), &prAddr);
    if (status != PR_SUCCESS) {
      return NS_ERROR_FAILURE;
    }

    mozilla::net::NetAddr addr(&prAddr);
    rv = sock->InitWithAddress(&addr, mPrincipal, aAddressReuse,
                               /* optional_argc = */ 1);
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<nsINetAddr> laddr;
  rv = sock->GetLocalAddr(getter_AddRefs(laddr));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  uint16_t family;
  rv = laddr->GetFamily(&family);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  if (family == nsINetAddr::FAMILY_INET) {
    rv = sock->SetMulticastLoopback(aLoopback);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }
  // TODO: once bug 1252759 is fixed query buffer first and only increase
  if (recvBufferSize != 0) {
    rv = sock->SetRecvBufferSize(recvBufferSize);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      UDPSOCKET_LOG(
          ("%s: [this=%p] %s:%u failed to set recv buffer size to: %" PRIu32,
           __FUNCTION__, this, nsCString(aHost).get(), aPort, recvBufferSize));
    }
  }
  if (sendBufferSize != 0) {
    rv = sock->SetSendBufferSize(sendBufferSize);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      UDPSOCKET_LOG(
          ("%s: [this=%p] %s:%u failed to set send buffer size to: %" PRIu32,
           __FUNCTION__, this, nsCString(aHost).get(), aPort, sendBufferSize));
    }
  }

  // register listener
  rv = sock->AsyncListen(this);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  mSocket = std::move(sock);

  return NS_OK;
}

static nsCOMPtr<nsIEventTarget> GetSTSThread() {
  nsresult rv;

  nsCOMPtr<nsIEventTarget> sts_thread;

  sts_thread = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  return sts_thread;
}

static void CheckSTSThread() {
  DebugOnly<nsCOMPtr<nsIEventTarget>> sts_thread = GetSTSThread();

  ASSERT_ON_THREAD(sts_thread.value);
}

// Proxy the Connect() request to the STS thread, since it may block and
// should be done there.
mozilla::ipc::IPCResult UDPSocketParent::RecvConnect(
    const UDPAddressInfo& aAddressInfo) {
  nsCOMPtr<nsIEventTarget> target = GetCurrentSerialEventTarget();
  (void)NS_WARN_IF(NS_FAILED(GetSTSThread()->Dispatch(
      WrapRunnable(RefPtr<UDPSocketParent>(this), &UDPSocketParent::DoConnect,
                   mSocket, target, aAddressInfo),
      NS_DISPATCH_NORMAL)));
  return IPC_OK();
}

void UDPSocketParent::DoSendConnectResponse(
    const UDPAddressInfo& aAddressInfo) {
  // can't use directly with WrapRunnable due to warnings
  (void)SendCallbackConnected(aAddressInfo);
}

void UDPSocketParent::SendConnectResponse(
    const nsCOMPtr<nsIEventTarget>& aThread,
    const UDPAddressInfo& aAddressInfo) {
  (void)NS_WARN_IF(NS_FAILED(aThread->Dispatch(
      WrapRunnable(RefPtr<UDPSocketParent>(this),
                   &UDPSocketParent::DoSendConnectResponse, aAddressInfo),
      NS_DISPATCH_NORMAL)));
}

// Runs on STS thread
void UDPSocketParent::DoConnect(const nsCOMPtr<nsIUDPSocket>& aSocket,
                                const nsCOMPtr<nsIEventTarget>& aReturnThread,
                                const UDPAddressInfo& aAddressInfo) {
  UDPSOCKET_LOG(("%s: %s:%u", __FUNCTION__, aAddressInfo.addr().get(),
                 aAddressInfo.port()));
  if (NS_FAILED(
          ConnectInternal(aSocket, aAddressInfo.addr(), aAddressInfo.port()))) {
    SendInternalError(aReturnThread, __LINE__);
    return;
  }
  CheckSTSThread();

  nsCOMPtr<nsINetAddr> localAddr;
  aSocket->GetLocalAddr(getter_AddRefs(localAddr));

  nsCString addr;
  if (NS_FAILED(localAddr->GetAddress(addr))) {
    SendInternalError(aReturnThread, __LINE__);
    return;
  }

  uint16_t port;
  if (NS_FAILED(localAddr->GetPort(&port))) {
    SendInternalError(aReturnThread, __LINE__);
    return;
  }

  UDPSOCKET_LOG(
      ("%s: SendConnectResponse: %s:%u", __FUNCTION__, addr.get(), port));
  SendConnectResponse(aReturnThread, UDPAddressInfo(addr, port));
}

nsresult UDPSocketParent::ConnectInternal(const nsCOMPtr<nsIUDPSocket>& aSocket,
                                          const nsCString& aHost,
                                          const uint16_t& aPort) {
  nsresult rv;

  UDPSOCKET_LOG(("%s: %s:%u", __FUNCTION__, nsCString(aHost).get(), aPort));

  if (!aSocket) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  PRNetAddr prAddr;
  memset(&prAddr, 0, sizeof(prAddr));
  PR_InitializeNetAddr(PR_IpAddrAny, aPort, &prAddr);
  PRStatus status = PR_StringToNetAddr(aHost.get(), &prAddr);
  if (status != PR_SUCCESS) {
    return NS_ERROR_FAILURE;
  }

  mozilla::net::NetAddr addr(&prAddr);
  rv = aSocket->Connect(&addr);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

mozilla::ipc::IPCResult UDPSocketParent::RecvOutgoingData(
    const UDPData& aData, const UDPSocketAddr& aAddr) {
  if (!mSocket) {
    NS_WARNING("sending socket is closed");
    FireInternalError(__LINE__);
    return IPC_OK();
  }

  if (!mFilter && aData.type() == UDPData::TIPCStream) {
    return IPC_FAIL(this, "IPCStream payload requires a filter");
  }

  nsresult rv;
  if (mFilter) {
    if (aAddr.type() != UDPSocketAddr::TNetAddr) {
      return IPC_OK();
    }

    // TODO, Packet filter doesn't support input stream yet.
    if (aData.type() != UDPData::TArrayOfuint8_t) {
      return IPC_OK();
    }

    bool allowed;
    const nsTArray<uint8_t>& data(aData.get_ArrayOfuint8_t());
    UDPSOCKET_LOG(("%s(%s:%d): Filtering outgoing packet", __FUNCTION__,
                   mAddress.addr().get(), mAddress.port()));

    rv = mFilter->FilterPacket(&aAddr.get_NetAddr(), data.Elements(),
                               data.Length(), nsISocketFilter::SF_OUTGOING,
                               &allowed);

    // Sending unallowed data, kill content.
    if (NS_WARN_IF(NS_FAILED(rv)) || !allowed) {
      return IPC_FAIL(this, "Content tried to send non STUN packet");
    }
  }

  switch (aData.type()) {
    case UDPData::TArrayOfuint8_t:
      Send(aData.get_ArrayOfuint8_t(), aAddr);
      break;
    case UDPData::TIPCStream:
      Send(aData.get_IPCStream(), aAddr);
      break;
    default:
      MOZ_ASSERT(false, "Invalid data type!");
      return IPC_OK();
  }

  return IPC_OK();
}

void UDPSocketParent::Send(const nsTArray<uint8_t>& aData,
                           const UDPSocketAddr& aAddr) {
  nsresult rv;
  uint32_t count;
  switch (aAddr.type()) {
    case UDPSocketAddr::TUDPAddressInfo: {
      const UDPAddressInfo& addrInfo(aAddr.get_UDPAddressInfo());
      rv = mSocket->Send(addrInfo.addr(), addrInfo.port(), aData, &count);
      break;
    }
    case UDPSocketAddr::TNetAddr: {
      const NetAddr& addr(aAddr.get_NetAddr());
      rv = mSocket->SendWithAddress(&addr, aData.Elements(), aData.Length(),
                                    &count);
      break;
    }
    default:
      MOZ_ASSERT(false, "Invalid address type!");
      return;
  }

  if (NS_WARN_IF(NS_FAILED(rv)) || count == 0) {
    FireInternalError(__LINE__);
  }
}

void UDPSocketParent::Send(const IPCStream& aStream,
                           const UDPSocketAddr& aAddr) {
  nsCOMPtr<nsIInputStream> stream = DeserializeIPCStream(aStream);

  if (NS_WARN_IF(!stream)) {
    return;
  }

  nsresult rv;
  switch (aAddr.type()) {
    case UDPSocketAddr::TUDPAddressInfo: {
      const UDPAddressInfo& addrInfo(aAddr.get_UDPAddressInfo());
      rv = mSocket->SendBinaryStream(addrInfo.addr(), addrInfo.port(), stream);
      break;
    }
    case UDPSocketAddr::TNetAddr: {
      const NetAddr& addr(aAddr.get_NetAddr());
      rv = mSocket->SendBinaryStreamWithAddress(&addr, stream);
      break;
    }
    default:
      MOZ_ASSERT(false, "Invalid address type!");
      return;
  }

  if (NS_FAILED(rv)) {
    FireInternalError(__LINE__);
  }
}

mozilla::ipc::IPCResult UDPSocketParent::RecvJoinMulticast(
    const nsACString& aMulticastAddress, const nsACString& aInterface) {
  if (!mSocket) {
    NS_WARNING("multicast socket is closed");
    FireInternalError(__LINE__);
    return IPC_OK();
  }

  nsresult rv = mSocket->JoinMulticast(aMulticastAddress, aInterface);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    FireInternalError(__LINE__);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult UDPSocketParent::RecvLeaveMulticast(
    const nsACString& aMulticastAddress, const nsACString& aInterface) {
  if (!mSocket) {
    NS_WARNING("multicast socket is closed");
    FireInternalError(__LINE__);
    return IPC_OK();
  }

  nsresult rv = mSocket->LeaveMulticast(aMulticastAddress, aInterface);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    FireInternalError(__LINE__);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult UDPSocketParent::RecvClose() {
  if (!mSocket) {
    return IPC_OK();
  }

  nsresult rv = mSocket->Close();
  mSocket = nullptr;

  (void)NS_WARN_IF(NS_FAILED(rv));

  return IPC_OK();
}

mozilla::ipc::IPCResult UDPSocketParent::RecvRequestDelete() {
  (void)Send__delete__(this);
  return IPC_OK();
}

void UDPSocketParent::ActorDestroy(ActorDestroyReason why) {
  MOZ_ASSERT(mIPCOpen);
  mIPCOpen = false;
  if (mSocket) {
    mSocket->Close();
  }
  mSocket = nullptr;
}

// nsIUDPSocketListener

NS_IMETHODIMP
UDPSocketParent::OnPacketReceived(nsIUDPSocket* aSocket,
                                  nsIUDPMessage* aMessage) {
  // receiving packet from remote host, forward the message content to child
  // process
  if (!mIPCOpen) {
    return NS_OK;
  }

  uint16_t port;
  nsCString ip;
  nsCOMPtr<nsINetAddr> fromAddr;
  aMessage->GetFromAddr(getter_AddRefs(fromAddr));
  fromAddr->GetPort(&port);
  fromAddr->GetAddress(ip);

  nsCString data;
  aMessage->GetData(data);

  const char* buffer = data.get();
  uint32_t len = data.Length();
  UDPSOCKET_LOG(("%s: %s:%u, length %u", __FUNCTION__, ip.get(), port, len));

  if (mFilter) {
    bool allowed;
    mozilla::net::NetAddr addr;
    fromAddr->GetNetAddr(&addr);
    UDPSOCKET_LOG(("%s(%s:%d): Filtering incoming packet", __FUNCTION__,
                   mAddress.addr().get(), mAddress.port()));
    nsresult rv = mFilter->FilterPacket(&addr, (const uint8_t*)buffer, len,
                                        nsISocketFilter::SF_INCOMING, &allowed);
    // Receiving unallowed data, drop.
    if (NS_WARN_IF(NS_FAILED(rv)) || !allowed) {
      if (!allowed) {
        UDPSOCKET_LOG(("%s: not allowed", __FUNCTION__));
      }
      return NS_OK;
    }
  }

  FallibleTArray<uint8_t> fallibleArray;
  if (!fallibleArray.InsertElementsAt(0, buffer, len, fallible)) {
    FireInternalError(__LINE__);
    return NS_ERROR_OUT_OF_MEMORY;
  }
  nsTArray<uint8_t> infallibleArray{std::move(fallibleArray)};

  // compose callback
  (void)SendCallbackReceivedData(UDPAddressInfo(ip, port), infallibleArray);

  return NS_OK;
}

NS_IMETHODIMP
UDPSocketParent::OnStopListening(nsIUDPSocket* aSocket, nsresult aStatus) {
  // underlying socket is dead, send state update to child process
  if (mIPCOpen) {
    (void)SendCallbackClosed();
  }
  return NS_OK;
}

void UDPSocketParent::FireInternalError(uint32_t aLineNo) {
  if (!mIPCOpen) {
    return;
  }

  (void)SendCallbackError("Internal error"_ns, nsLiteralCString(__FILE__),
                          aLineNo);
}

void UDPSocketParent::SendInternalError(const nsCOMPtr<nsIEventTarget>& aThread,
                                        uint32_t aLineNo) {
  UDPSOCKET_LOG(("SendInternalError: %u", aLineNo));
  (void)NS_WARN_IF(NS_FAILED(aThread->Dispatch(
      WrapRunnable(RefPtr<UDPSocketParent>(this),
                   &UDPSocketParent::FireInternalError, aLineNo),
      NS_DISPATCH_NORMAL)));
}

}  // namespace dom
}  // namespace mozilla
