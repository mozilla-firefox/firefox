/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "Http3ConnectUDPStream.h"

#include "Http3Session.h"
#include "HttpConnectionUDP.h"
#include "HttpLog.h"
#include "mozilla/net/UriTemplate.h"
#include "nsHttpHandler.h"
#include "nsIOService.h"
#include "nsIPipe.h"
#include "nsNetAddr.h"
#include "nsProxyInfo.h"

namespace mozilla::net {

NS_IMPL_ISUPPORTS(Http3ConnectUDPStream, nsIUDPSocket)

Http3ConnectUDPStream::Http3ConnectUDPStream(nsAHttpTransaction* aTrans,
                                             Http3SessionBase* aSession,
                                             nsIEventTarget* aTarget)
    : Http3TunnelStreamBase(aTrans, aSession), mTarget(aTarget) {
  LOG(("Http3ConnectUDPStream ctor %p", this));
}

Http3ConnectUDPStream::~Http3ConnectUDPStream() {
  LOG(("Http3ConnectUDPStream dtor %p", this));
}

already_AddRefed<HttpConnectionUDP> Http3ConnectUDPStream::CreateUDPConnection(
    nsIInterfaceRequestor* aCallbacks) {
  LOG(("Http3ConnectUDPStream::CreateUDPConnection %p", this));
  RefPtr<HttpConnectionUDP> conn = new HttpConnectionUDP();
  NetAddr peerAddr;
  peerAddr.InitFromString("127.0.0.1"_ns);
  nsresult rv =
      conn->InitWithSocket(mTransaction->ConnectionInfo(), this, peerAddr,
                           aCallbacks, mTransaction->Caps());
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  return conn.forget();
}

void Http3ConnectUDPStream::Close(nsresult aResult) {
  LOG(("Http3ConnectUDPStream::Close %p aResult=%x", this,
       static_cast<uint32_t>(aResult)));
  if (mSyncListener) {
    (void)mSyncListener->OnStopListening(this, aResult);
  }
  mRecvState = RECV_DONE;
  mSendState = SEND_DONE;
  if (mTransaction) {
    mTransaction->Close(aResult);
    mTransaction = nullptr;
  }

  mSession = nullptr;
}

void Http3ConnectUDPStream::OnDatagramReceived(nsTArray<uint8_t>&& aData) {
  LOG(("Http3ConnectUDPStream::OnDatagramReceived %p", this));

  mByteReadCount += aData.Length();
  mReceivedData.Push(std::make_unique<UDPPayload>(std::move(aData)));
  if (mSyncListener) {
    mSyncListener->OnPacketReceived(this);
  }
}

bool Http3ConnectUDPStream::OnActivated() {
  LOG(("Http3ConnectUDPStream::OnActivated %p", this));
  mSession->FinishTunnelSetup(mTransaction);
  return false;
}

nsresult Http3ConnectUDPStream::OnProcessDatagram() {
  LOG(("Http3ConnectUDPStream::OnProcessDatagram %p", this));

  while (!mOutputData.IsEmpty()) {
    nsTArray<uint8_t> data = mOutputData.Pop()->TakeData();
    mSession->SendHTTPDatagram(mStreamId, data, mTrackingId++);
  }
  return NS_OK;
}

nsresult Http3ConnectUDPStream::TryActivating() {
  nsProxyInfo* info = mTransaction->ConnectionInfo()->ProxyInfo();
  if (!info) {
    return NS_ERROR_UNEXPECTED;
  }

  RefPtr<UriTemplateWrapper> builder;
  UriTemplateWrapper::Init(info->MasqueTemplate(), getter_AddRefs(builder));
  if (!builder) {
    return NS_ERROR_UNEXPECTED;
  }

  bool useRoutedHost =
      !mTransaction->ConnectionInfo()->GetRoutedHost().IsEmpty();
  nsresult rv = builder->Set(
      "target_host"_ns, useRoutedHost
                            ? mTransaction->ConnectionInfo()->GetRoutedHost()
                            : mTransaction->ConnectionInfo()->GetOrigin());
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = builder->Set("target_port"_ns,
                    useRoutedHost
                        ? mTransaction->ConnectionInfo()->RoutedPort()
                        : mTransaction->ConnectionInfo()->OriginPort());
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCString pathQuery;
  builder->Build(&pathQuery);
  LOG(("Http3ConnectUDPStream::TryActivating [host=%s pathQuery=%s]",
       info->Host().get(), pathQuery.get()));
  return mSession->TryActivating(""_ns, ""_ns, info->Host(), pathQuery,
                                 mFlatHttpRequestHeaders, &mStreamId, this);
}

void Http3ConnectUDPStream::OnSocketReady(PRFileDesc* fd, int16_t outFlags) {}

void Http3ConnectUDPStream::OnSocketDetached(PRFileDesc* fd) {}

void Http3ConnectUDPStream::IsLocal(bool* aIsLocal) {}

nsresult Http3ConnectUDPStream::GetRemoteAddr(NetAddr* addr) { return NS_OK; }

NS_IMETHODIMP Http3ConnectUDPStream::Init(int32_t aPort, bool aLoopbackOnly,
                                          nsIPrincipal* aPrincipal,
                                          bool aAddressReuse, uint8_t _argc) {
  return NS_OK;
}

NS_IMETHODIMP Http3ConnectUDPStream::Init2(const nsACString& aAddr,
                                           int32_t aPort,
                                           nsIPrincipal* aPrincipal,
                                           bool aAddressReuse, uint8_t _argc) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::InitWithAddress(
    const mozilla::net::NetAddr* aAddr, nsIPrincipal* aPrincipal,
    bool aAddressReuse, uint8_t _argc) {
  return NS_OK;
}

NS_IMETHODIMP Http3ConnectUDPStream::Close() {
  Close(NS_ERROR_ABORT);
  return NS_OK;
}

NS_IMETHODIMP Http3ConnectUDPStream::AsyncListen(
    nsIUDPSocketListener* aListener) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::SyncListen(
    nsIUDPSocketSyncListener* aListener) {
  mSyncListener = aListener;
  return NS_OK;
}

NS_IMETHODIMP Http3ConnectUDPStream::Connect(
    const mozilla::net::NetAddr* aAddr) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::GetLocalAddr(nsINetAddr** aLocalAddr) {
  NetAddr addr;
  addr.InitFromString("0.0.0.0"_ns);
  nsCOMPtr<nsINetAddr> result = new nsNetAddr(&addr);
  result.forget(aLocalAddr);
  return NS_OK;
}

NS_IMETHODIMP Http3ConnectUDPStream::GetPort(int32_t* aPort) { return NS_OK; }

NS_IMETHODIMP Http3ConnectUDPStream::GetAddress(
    mozilla::net::NetAddr* _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::Send(const nsACString& host, uint16_t port,
                                          const nsTArray<uint8_t>& data,
                                          uint32_t* _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::SendWithAddr(nsINetAddr* addr,
                                                  const nsTArray<uint8_t>& data,
                                                  uint32_t* _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::RecvWithAddr(mozilla::net::NetAddr* addr,
                                                  nsTArray<uint8_t>& data) {
  if (mReceivedData.IsEmpty()) {
    return NS_OK;
  }

  // TODO: should we use a real IP address here?
  addr->InitFromString("127.0.0.1"_ns);
  nsTArray<uint8_t> res = mReceivedData.Pop()->TakeData();
  data.AppendElements(std::move(res));
  return NS_OK;
}

NS_IMETHODIMP Http3ConnectUDPStream::SendWithAddress(
    const mozilla::net::NetAddr* addr, const uint8_t* data, uint32_t length,
    uint32_t* _retval) {
  LOG(("Http3ConnectUDPStream::SendWithAddress %p mSendState=%d length=%d",
       this, static_cast<uint32_t>(mSendState), length));
  if (mSendState != PROCESSING_DATAGRAM) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsTArray<uint8_t> datagram;
  datagram.AppendElements(data, length);
  mOutputData.Push(std::make_unique<UDPPayload>(std::move(datagram)));
  mByteWriteCount += length;
  mSession->StreamHasDataToWrite(this);
  return NS_OK;
}

NS_IMETHODIMP Http3ConnectUDPStream::SendBinaryStream(const nsACString& host,
                                                      uint16_t port,
                                                      nsIInputStream* stream) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::SendBinaryStreamWithAddress(
    const mozilla::net::NetAddr* addr, nsIInputStream* stream) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::JoinMulticast(const nsACString& addr,
                                                   const nsACString& iface) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::JoinMulticastAddr(
    const mozilla::net::NetAddr addr, const mozilla::net::NetAddr* iface) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::LeaveMulticast(const nsACString& addr,
                                                    const nsACString& iface) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::LeaveMulticastAddr(
    const mozilla::net::NetAddr addr, const mozilla::net::NetAddr* iface) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

int64_t Http3ConnectUDPStream::GetFileDescriptor() { return -1; }

void Http3ConnectUDPStream::EnableWritePoll() {}

bool Http3ConnectUDPStream::IsSocketClosed() { return mSendState == SEND_DONE; }

void Http3ConnectUDPStream::AddOutputBytes(uint32_t aBytes) {}

void Http3ConnectUDPStream::AddInputBytes(uint32_t aBytes) {}

NS_IMETHODIMP Http3ConnectUDPStream::GetMulticastLoopback(
    bool* aMulticastLoopback) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::SetMulticastLoopback(
    bool aMulticastLoopback) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::GetMulticastInterface(
    nsACString& aMulticastInterface) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::SetMulticastInterface(
    const nsACString& aMulticastInterface) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::GetMulticastInterfaceAddr(
    mozilla::net::NetAddr* aMulticastInterfaceAddr) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::SetMulticastInterfaceAddr(
    mozilla::net::NetAddr aMulticastInterfaceAddr) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::GetRecvBufferSize(
    int32_t* aRecvBufferSize) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::SetRecvBufferSize(
    int32_t aRecvBufferSize) {
  return NS_OK;
}

NS_IMETHODIMP Http3ConnectUDPStream::GetSendBufferSize(
    int32_t* aSendBufferSize) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::SetSendBufferSize(
    int32_t aSendBufferSize) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::GetDontFragment(bool* aDontFragment) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3ConnectUDPStream::SetDontFragment(bool aDontFragment) {
  return NS_OK;
}

void Http3ConnectUDPStream::MarkAsTRRServiceChannel() {
  mIsTRRServiceChannel = true;
}

bool Http3ConnectUDPStream::IsTRRServiceChannel() {
  return mIsTRRServiceChannel;
}

void Http3ConnectUDPStream::SetOriginAttributes(
    const mozilla::OriginAttributes& aOriginAttributes) {
  mOriginAttributes = aOriginAttributes;
}

}  // namespace mozilla::net
