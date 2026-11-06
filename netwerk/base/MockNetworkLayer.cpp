/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MockNetworkLayer.h"

#include "MockNetworkLayerController.h"
#include "nsSocketTransportService2.h"
#include "prio.h"
#include "prmem.h"

namespace mozilla::net {

static PRDescIdentity sMockNetworkLayerIdentity;
static PRIOMethods sMockNetworkLayeyMethods;
static PRIOMethods* sMockNetworkLayeyMethodsPtr = nullptr;

// Not used for now.
class MockNetworkSecret {
 public:
  MockNetworkSecret() = default;
};

static PRStatus MockNetworkConnect(PRFileDesc* fd, const PRNetAddr* addr,
                                   PRIntervalTime to) {
  PRStatus status;
  mozilla::net::NetAddr netAddr(addr);
  MockNetworkSecret* secret = reinterpret_cast<MockNetworkSecret*>(fd->secret);
  nsAutoCString addrPort;
  netAddr.ToAddrPortString(addrPort);
  SOCKET_LOG(
      ("MockNetworkConnect %p connect to [%s]\n", secret, addrPort.get()));
  if (FindBlockedTCPConnect(netAddr)) {
    SOCKET_LOG(("MockNetworkConnect %p connect to [%s] blocked\n", secret,
                addrPort.get()));
    PR_SetError(PR_CONNECT_REFUSED_ERROR, 0);
    return PR_FAILURE;
  }
  if (FindPausedTCPConnect(netAddr)) {
    SOCKET_LOG(("MockNetworkConnect %p connect to [%s] paused\n", secret,
                addrPort.get()));
    PR_SetError(PR_IN_PROGRESS_ERROR, 0);
    return PR_FAILURE;
  }
  mozilla::net::NetAddr redirected;
  if (FindNetAddrOverride(netAddr, redirected)) {
    redirected.ToAddrPortString(addrPort);
    SOCKET_LOG(
        ("MockNetworkConnect %p redirect to [%s]\n", secret, addrPort.get()));
    PRNetAddr prAddr;
    NetAddrToPRNetAddr(&redirected, &prAddr);
    status = fd->lower->methods->connect(fd->lower, &prAddr, to);
  } else {
    status = fd->lower->methods->connect(fd->lower, addr, to);
  }

  return status;
}

static PRInt32 MockNetworkSend(PRFileDesc* fd, const void* buf, PRInt32 amount,
                               PRIntn flags, PRIntervalTime timeout) {
  MOZ_RELEASE_ASSERT(fd->identity == sMockNetworkLayerIdentity);

  MockNetworkSecret* secret = reinterpret_cast<MockNetworkSecret*>(fd->secret);
  SOCKET_LOG(("MockNetworkSend %p\n", secret));

  PRInt32 rv =
      (fd->lower->methods->send)(fd->lower, buf, amount, flags, timeout);
  return rv;
}

static PRInt32 MockNetworkWrite(PRFileDesc* fd, const void* buf,
                                PRInt32 amount) {
  return MockNetworkSend(fd, buf, amount, 0, PR_INTERVAL_NO_WAIT);
}

static PRInt32 MockNetworkRecv(PRFileDesc* fd, void* buf, PRInt32 amount,
                               PRIntn flags, PRIntervalTime timeout) {
  MOZ_RELEASE_ASSERT(fd->identity == sMockNetworkLayerIdentity);

  MockNetworkSecret* secret = reinterpret_cast<MockNetworkSecret*>(fd->secret);
  SOCKET_LOG(("MockNetworkRecv %p\n", secret));

  PRInt32 rv =
      (fd->lower->methods->recv)(fd->lower, buf, amount, flags, timeout);
  return rv;
}

static PRInt32 MockNetworkRead(PRFileDesc* fd, void* buf, PRInt32 amount) {
  return MockNetworkRecv(fd, buf, amount, 0, PR_INTERVAL_NO_WAIT);
}

static PRStatus MockNetworkClose(PRFileDesc* fd) {
  if (!fd) {
    return PR_FAILURE;
  }

  PRFileDesc* layer = PR_PopIOLayer(fd, PR_TOP_IO_LAYER);

  MOZ_RELEASE_ASSERT(layer && layer->identity == sMockNetworkLayerIdentity,
                     "MockNetwork Layer not on top of stack");

  MockNetworkSecret* secret =
      reinterpret_cast<MockNetworkSecret*>(layer->secret);
  SOCKET_LOG(("MockNetworkClose %p\n", secret));
  layer->secret = nullptr;
  layer->dtor(layer);
  delete secret;
  return fd->methods->close(fd);
}

static PRInt32 MockNetworkSendTo(PRFileDesc* fd, const void* buf,
                                 PRInt32 amount, PRIntn flags,
                                 const PRNetAddr* addr,
                                 PRIntervalTime timeout) {
  MOZ_RELEASE_ASSERT(fd->identity == sMockNetworkLayerIdentity);

  MockNetworkSecret* secret = reinterpret_cast<MockNetworkSecret*>(fd->secret);
  SOCKET_LOG(("MockNetworkSendTo %p", secret));
  mozilla::net::NetAddr netAddr(addr);
  if (FindFailedUDPAddr(netAddr)) {
    nsAutoCString addrPort;
    netAddr.ToAddrPortString(addrPort);
    SOCKET_LOG(
        ("MockNetworkSendTo %p addr [%s] failed", secret, addrPort.get()));
    PR_SetError(PR_CONNECT_REFUSED_ERROR, 0);
    return -1;
  }
  if (FindWouldBlockUDPAddr(netAddr)) {
    nsAutoCString addrPort;
    netAddr.ToAddrPortString(addrPort);
    SOCKET_LOG(
        ("MockNetworkSendTo %p addr [%s] would block", secret, addrPort.get()));
    PR_SetError(PR_WOULD_BLOCK_ERROR, 0);
    return -1;
  }
  if (FindBlockedUDPAddr(netAddr)) {
    nsAutoCString addrPort;
    netAddr.ToAddrPortString(addrPort);
    SOCKET_LOG(
        ("MockNetworkSendTo %p addr [%s] is blocked", secret, addrPort.get()));
    return PR_SUCCESS;
  }
  return (fd->lower->methods->sendto)(fd->lower, buf, amount, flags, addr,
                                      timeout);
}

static PRInt32 PR_CALLBACK MockNetworkRecvFrom(PRFileDesc* fd, void* buf,
                                               PRInt32 amount, PRIntn flags,
                                               PRNetAddr* addr,
                                               PRIntervalTime timeout) {
  MOZ_RELEASE_ASSERT(fd->identity == sMockNetworkLayerIdentity);

  MockNetworkSecret* secret = reinterpret_cast<MockNetworkSecret*>(fd->secret);
  SOCKET_LOG(("MockNetworkRecvFrom %p\n", secret));
  mozilla::net::NetAddr netAddr(addr);
  if (FindBlockedUDPAddr(netAddr)) {
    nsAutoCString addrPort;
    netAddr.ToAddrPortString(addrPort);
    SOCKET_LOG(("MockNetworkRecvFrom %p addr [%s] is blocked", secret,
                addrPort.get()));
    return PR_SUCCESS;
  }
  return (fd->lower->methods->recvfrom)(fd->lower, buf, amount, flags, addr,
                                        timeout);
}

nsresult AttachMockNetworkLayer(PRFileDesc* fd) {
  if (!sMockNetworkLayeyMethodsPtr) {
    sMockNetworkLayerIdentity = PR_GetUniqueIdentity("MockNetwork Layer");
    sMockNetworkLayeyMethods = *PR_GetDefaultIOMethods();
    sMockNetworkLayeyMethods.connect = MockNetworkConnect;
    sMockNetworkLayeyMethods.send = MockNetworkSend;
    sMockNetworkLayeyMethods.write = MockNetworkWrite;
    sMockNetworkLayeyMethods.recv = MockNetworkRecv;
    sMockNetworkLayeyMethods.read = MockNetworkRead;
    sMockNetworkLayeyMethods.close = MockNetworkClose;
    sMockNetworkLayeyMethods.sendto = MockNetworkSendTo;
    sMockNetworkLayeyMethods.recvfrom = MockNetworkRecvFrom;
    sMockNetworkLayeyMethodsPtr = &sMockNetworkLayeyMethods;
  }

  PRFileDesc* layer = PR_CreateIOLayerStub(sMockNetworkLayerIdentity,
                                           sMockNetworkLayeyMethodsPtr);

  if (!layer) {
    return NS_ERROR_FAILURE;
  }

  MockNetworkSecret* secret = new MockNetworkSecret();

  layer->secret = reinterpret_cast<PRFilePrivate*>(secret);

  PRStatus status = PR_PushIOLayer(fd, PR_NSPR_IO_LAYER, layer);

  if (status == PR_FAILURE) {
    delete secret;
    PR_Free(layer);  // PR_CreateIOLayerStub() uses PR_Malloc().
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

}  // namespace mozilla::net
