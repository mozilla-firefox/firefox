/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef webrtc_tcp_socket_wrapper_
#define webrtc_tcp_socket_wrapper_

#include <memory>

#include "mozilla/net/WebrtcTCPSocketCallback.h"
#include "nsCOMPtr.h"
#include "nsTArray.h"

class nsIEventTarget;

namespace mozilla {

class NrSocketProxyConfig;

namespace net {

class WebrtcTCPSocketChild;

/**
 * WebrtcTCPSocketWrapper is a protector class for mtransport and IPDL.
 * mtransport and IPDL cannot include headers from each other due to conflicting
 * typedefs. Also it helps users by dispatching calls to the appropriate thread
 * based on mtransport's and IPDL's threading requirements.
 *
 * WebrtcTCPSocketWrapper is only used in the child process.
 * WebrtcTCPSocketWrapper does not dispatch for the parent process.
 * WebrtcTCPSocketCallback calls are dispatched to the STS thread.
 * IPDL calls are dispatched to the main thread.
 */
class WebrtcTCPSocketWrapper : public WebrtcTCPSocketCallback {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WebrtcTCPSocketWrapper, override)

  explicit WebrtcTCPSocketWrapper(WebrtcTCPSocketCallback* aCallbacks);

  virtual void AsyncOpen(const nsCString& aHost, const uint16_t& aPort,
                         const nsCString& aLocalAddress,
                         const uint16_t& aLocalPort, bool aUseTls,
                         const std::shared_ptr<NrSocketProxyConfig>& aConfig);
  virtual void SendWrite(nsTArray<uint8_t>&& aReadData);
  virtual void Close();

  // WebrtcTCPSocketCallback
  virtual void OnClose(nsresult aReason) override;
  virtual void OnConnected(const nsACString& aProxyType) override;
  virtual void OnRead(nsTArray<uint8_t>&& aReadData) override;

 protected:
  RefPtr<WebrtcTCPSocketCallback> mProxyCallbacks;
  RefPtr<WebrtcTCPSocketChild> mWebrtcTCPSocket;

  nsCOMPtr<nsIEventTarget> mMainThread;
  nsCOMPtr<nsIEventTarget> mSocketThread;

  virtual ~WebrtcTCPSocketWrapper();
};

}  // namespace net
}  // namespace mozilla

#endif  // webrtc_tcp_socket_wrapper_
