/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebrtcTCPSocket.h"

#include "WebrtcTCPSocketCallback.h"
#include "WebrtcTCPSocketLog.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/ContentProcessManager.h"
#include "nsHttpChannel.h"
#include "nsICancelable.h"
#include "nsIChannel.h"
#include "nsIClassOfService.h"
#include "nsIContentPolicy.h"
#include "nsICookieJarSettings.h"
#include "nsIIOService.h"
#include "nsILoadInfo.h"
#include "nsIProtocolProxyService.h"
#include "nsISocketTransport.h"
#include "nsISocketTransportService.h"
#include "nsIURIMutator.h"
#include "nsNetUtil.h"
#include "nsProxyRelease.h"
#include "nsSocketTransportService2.h"
#include "nsString.h"

namespace mozilla::net {

class WebrtcTCPData {
 public:
  explicit WebrtcTCPData(nsTArray<uint8_t>&& aData) : mData(std::move(aData)) {
    MOZ_COUNT_CTOR(WebrtcTCPData);
  }

  MOZ_COUNTED_DTOR(WebrtcTCPData)

  const nsTArray<uint8_t>& GetData() const { return mData; }

 private:
  nsTArray<uint8_t> mData;
};

NS_IMPL_ISUPPORTS(WebrtcTCPSocket, nsIAuthPromptProvider,
                  nsIHttpUpgradeListener, nsIInputStreamCallback,
                  nsIInterfaceRequestor, nsIOutputStreamCallback,
                  nsIRequestObserver, nsIStreamListener,
                  nsIProtocolProxyCallback)

WebrtcTCPSocket::WebrtcTCPSocket(WebrtcTCPSocketCallback* aCallbacks)
    : mProxyCallbacks(aCallbacks),
      mClosed(false),
      mOpened(false),
      mWriteOffset(0),
      mAuthProvider(nullptr),
      mTransport(nullptr),
      mSocketIn(nullptr),
      mSocketOut(nullptr) {
  LOG("WebrtcTCPSocket::WebrtcTCPSocket {}\n", fmt::ptr(this));
  mMainThread = GetMainThreadSerialEventTarget();
  mSocketThread = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID);
  MOZ_RELEASE_ASSERT(mMainThread, "no main thread");
  MOZ_RELEASE_ASSERT(mSocketThread, "no socket thread");
}

WebrtcTCPSocket::~WebrtcTCPSocket() {
  LOG("WebrtcTCPSocket::~WebrtcTCPSocket {}\n", fmt::ptr(this));

  NS_ProxyRelease("WebrtcTCPSocket::CleanUpAuthProvider", mMainThread,
                  mAuthProvider.forget());
}

void WebrtcTCPSocket::SetTabId(dom::TabId aTabId) {
  MOZ_ASSERT(NS_IsMainThread());
  dom::ContentProcessManager* cpm = dom::ContentProcessManager::GetSingleton();
  if (cpm) {
    dom::ContentParentId cpId = cpm->GetTabProcessId(aTabId);
    mAuthProvider = cpm->GetBrowserParentByProcessAndTabId(cpId, aTabId);
  }
}

nsresult WebrtcTCPSocket::Write(nsTArray<uint8_t>&& aWriteData) {
  LOG("WebrtcTCPSocket::Write {}\n", fmt::ptr(this));
  MOZ_ASSERT(NS_IsMainThread());
  nsresult rv = mSocketThread->Dispatch(NewRunnableMethod<nsTArray<uint8_t>&&>(
      "WebrtcTCPSocket::Write", this, &WebrtcTCPSocket::EnqueueWrite_s,
      std::move(aWriteData)));
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Failed to dispatch to STS");

  return rv;
}

nsresult WebrtcTCPSocket::Close() {
  LOG("WebrtcTCPSocket::Close {}\n", fmt::ptr(this));

  CloseWithReason(NS_OK);

  return NS_OK;
}

void WebrtcTCPSocket::CloseWithReason(nsresult aReason) {
  LOG("WebrtcTCPSocket::CloseWithReason {} reason={}\n", fmt::ptr(this),
      static_cast<uint32_t>(aReason));

  if (!OnSocketThread()) {
    MOZ_ASSERT(NS_IsMainThread(), "not on main thread");

    // Let's pretend we got an open even if we didn't to prevent an Open later.
    mOpened = true;

    DebugOnly<nsresult> rv =
        mSocketThread->Dispatch(NewRunnableMethod<nsresult>(
            "WebrtcTCPSocket::CloseWithReason", this,
            &WebrtcTCPSocket::CloseWithReason, aReason));

    // This was MOZ_ALWAYS_SUCCEEDS, but that now uses NS_WARNING_ASSERTION.
    // In order to convert this back to MOZ_ALWAYS_SUCCEEDS we would need
    // OnSocketThread to return true if we're shutting down and doing the
    // "running all of STS's queued events on main" thing.
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Failed to dispatch to STS");

    return;
  }

  if (mClosed) {
    return;
  }

  mClosed = true;

  if (mSocketIn) {
    mSocketIn->AsyncWait(nullptr, 0, 0, nullptr);
    mSocketIn = nullptr;
  }

  if (mSocketOut) {
    mSocketOut->AsyncWait(nullptr, 0, 0, nullptr);
    mSocketOut = nullptr;
  }

  if (mTransport) {
    mTransport->Close(NS_BASE_STREAM_CLOSED);
    mTransport = nullptr;
  }

  NS_ProxyRelease("WebrtcTCPSocket::CleanUpAuthProvider", mMainThread,
                  mAuthProvider.forget());
  InvokeOnClose(aReason);
}

nsresult WebrtcTCPSocket::Open(
    const nsACString& aHost, const uint16_t& aPort,
    const nsACString& aLocalAddress, const uint16_t& aLocalPort, bool aUseTls,
    const Maybe<net::WebrtcProxyConfig>& aProxyConfig) {
  LOG("WebrtcTCPSocket::Open {} remote-host={} local-addr={} local-port={}",
      fmt::ptr(this), PromiseFlatCString(aHost).get(),
      PromiseFlatCString(aLocalAddress).get(), aLocalPort);
  MOZ_ASSERT(NS_IsMainThread());

  if (NS_WARN_IF(mOpened)) {
    LOG("WebrtcTCPSocket {}: TCP socket already open\n", fmt::ptr(this));
    CloseWithReason(NS_ERROR_FAILURE);
    return NS_ERROR_FAILURE;
  }

  mOpened = true;
  const nsLiteralCString schemePrefix = aUseTls ? "https://"_ns : "http://"_ns;
  nsAutoCString spec(schemePrefix);

  bool ipv6Literal = aHost.Find(":") != kNotFound;
  if (ipv6Literal) {
    spec += "[";
    spec += aHost;
    spec += "]";
  } else {
    spec += aHost;
  }

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), spec);
  if (NS_SUCCEEDED(rv)) {
    rv = NS_MutateURI(uri).SetPort(aPort).Finalize(mURI);
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    CloseWithReason(NS_ERROR_FAILURE);
    return NS_ERROR_FAILURE;
  }

  mTls = aUseTls;
  mLocalAddress = aLocalAddress;
  mLocalPort = aLocalPort;
  mProxyConfig = aProxyConfig;

  if (!mProxyConfig.isSome()) {
    OpenWithoutHttpProxy(nullptr);
    return NS_OK;
  }

  // We need to figure out whether a proxy needs to be used for mURI before
  // we can start on establishing a connection.
  rv = DoProxyConfigLookup();

  if (NS_WARN_IF(NS_FAILED(rv))) {
    CloseWithReason(rv);
  }

  return rv;
}

nsresult WebrtcTCPSocket::DoProxyConfigLookup() {
  MOZ_ASSERT(NS_IsMainThread());
  nsresult rv;
  nsCOMPtr<nsIProtocolProxyService> pps =
      do_GetService(NS_PROTOCOLPROXYSERVICE_CONTRACTID, &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<nsIChannel> channel;
  rv = NS_NewChannel(getter_AddRefs(channel), mURI,
                     nsContentUtils::GetSystemPrincipal(),
                     nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
                     nsIContentPolicy::TYPE_OTHER);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = pps->AsyncResolve(channel,
                         nsIProtocolProxyService::RESOLVE_PREFER_HTTPS_PROXY |
                             nsIProtocolProxyService::RESOLVE_ALWAYS_TUNNEL,
                         this, nullptr, getter_AddRefs(mProxyRequest));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // We pick back up in OnProxyAvailable

  return NS_OK;
}

NS_IMETHODIMP WebrtcTCPSocket::OnProxyAvailable(nsICancelable* aRequest,
                                                nsIChannel* aChannel,
                                                nsIProxyInfo* aProxyinfo,
                                                nsresult aResult) {
  MOZ_ASSERT(NS_IsMainThread());
  mProxyRequest = nullptr;

  if (NS_SUCCEEDED(aResult) && aProxyinfo) {
    nsresult rv = aProxyinfo->GetType(mProxyType);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      CloseWithReason(rv);
      return rv;
    }

    if (mProxyType == "http" || mProxyType == "https" ||
        mProxyType == "masque") {
      rv = OpenWithHttpProxy();
      if (NS_WARN_IF(NS_FAILED(rv))) {
        CloseWithReason(rv);
      }
      return rv;
    }

    if (mProxyType == "socks" || mProxyType == "socks4" ||
        mProxyType == "direct") {
      OpenWithoutHttpProxy(aProxyinfo);
      return NS_OK;
    }
  }

  OpenWithoutHttpProxy(nullptr);

  return NS_OK;
}

void WebrtcTCPSocket::OpenWithoutHttpProxy(nsIProxyInfo* aSocksProxyInfo) {
  if (!OnSocketThread()) {
    DebugOnly<nsresult> rv =
        mSocketThread->Dispatch(NewRunnableMethod<nsCOMPtr<nsIProxyInfo>>(
            "WebrtcTCPSocket::OpenWithoutHttpProxy", this,
            &WebrtcTCPSocket::OpenWithoutHttpProxy, aSocksProxyInfo));
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Failed to dispatch to STS");
    return;
  }

  LOG("WebrtcTCPSocket::OpenWithoutHttpProxy {}\n", fmt::ptr(this));

  if (mClosed) {
    return;
  }

  if (NS_WARN_IF(mProxyConfig.isSome() && mProxyConfig->forceProxy() &&
                 !aSocksProxyInfo)) {
    CloseWithReason(NS_ERROR_FAILURE);
    return;
  }

  nsCString host;
  nsresult rv = mURI->GetHost(host);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    CloseWithReason(rv);
    return;
  }

  int32_t port = NS_GetRealPort(mURI);

  AutoTArray<nsCString, 1> socketTypes;
  if (mTls) {
    socketTypes.AppendElement("ssl"_ns);
  }

  nsCOMPtr<nsISocketTransportService> sts =
      do_GetService("@mozilla.org/network/socket-transport-service;1");
  rv = sts->CreateTransport(socketTypes, host, port, aSocksProxyInfo, nullptr,
                            getter_AddRefs(mTransport));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    CloseWithReason(rv);
    return;
  }

  mTransport->SetReuseAddrPort(true);

  PRNetAddr prAddr;
  if (NS_WARN_IF(PR_SUCCESS !=
                 PR_InitializeNetAddr(PR_IpAddrAny, mLocalPort, &prAddr))) {
    CloseWithReason(NS_ERROR_FAILURE);
    return;
  }

  if (NS_WARN_IF(PR_SUCCESS !=
                 PR_StringToNetAddr(mLocalAddress.get(), &prAddr))) {
    CloseWithReason(NS_ERROR_FAILURE);
    return;
  }

  mozilla::net::NetAddr addr(&prAddr);
  rv = mTransport->Bind(&addr);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    CloseWithReason(rv);
    return;
  }

  // Binding to a V4 address is not sufficient to cause this socket to use
  // V4, and the same goes for V6. So, we disable as needed here.
  uint32_t flags = 0;
  if (addr.raw.family == AF_INET) {
    flags |= nsISocketTransport::DISABLE_IPV6;
  } else if (addr.raw.family == AF_INET6) {
    flags |= nsISocketTransport::DISABLE_IPV4;
  } else {
    MOZ_CRASH();
  }

  mTransport->SetConnectionFlags(flags);

  nsCOMPtr<nsIInputStream> socketIn;
  rv = mTransport->OpenInputStream(0, 0, 0, getter_AddRefs(socketIn));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    CloseWithReason(rv);
    return;
  }
  mSocketIn = do_QueryInterface(socketIn);
  if (NS_WARN_IF(!mSocketIn)) {
    CloseWithReason(NS_ERROR_NULL_POINTER);
    return;
  }

  nsCOMPtr<nsIOutputStream> socketOut;
  rv = mTransport->OpenOutputStream(nsITransport::OPEN_UNBUFFERED, 0, 0,
                                    getter_AddRefs(socketOut));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    CloseWithReason(rv);
    return;
  }
  mSocketOut = do_QueryInterface(socketOut);
  if (NS_WARN_IF(!mSocketOut)) {
    CloseWithReason(NS_ERROR_NULL_POINTER);
    return;
  }

  FinishOpen();
}

nsresult WebrtcTCPSocket::OpenWithHttpProxy() {
  MOZ_ASSERT(NS_IsMainThread(), "not on main thread");
  LOG("WebrtcTCPSocket::OpenWithHttpProxy {}\n", fmt::ptr(this));
  nsresult rv;
  nsCOMPtr<nsIIOService> ioService;
  ioService = do_GetService(NS_IOSERVICE_CONTRACTID, &rv);
  if (NS_FAILED(rv)) {
    LOG("WebrtcTCPSocket {}: io service missing\n", fmt::ptr(this));
    return rv;
  }

  nsCOMPtr<nsILoadInfo> loadInfo;

  // FIXME: We don't know the remote type of the process which provided these
  // LoadInfoArgs. Pass in `RemoteType::NotRemote()` as the origin process to
  // blindly accept whatever value was passed by the other side for now, as we
  // aren't using it for security checks here.
  // If this code ever starts checking the triggering remote type, this needs to
  // be changed.
  rv = ipc::LoadInfoArgsToLoadInfo(mProxyConfig->loadInfoArgs(),
                                   dom::RemoteType::NotRemote(),
                                   getter_AddRefs(loadInfo));
  if (NS_FAILED(rv)) {
    LOG("WebrtcTCPSocket {}: could not init load info\n", fmt::ptr(this));
    return rv;
  }

  // -need to always tunnel since we're using a proxy
  // -there shouldn't be an opportunity to send cookies, but explicitly disallow
  // them anyway.
  // -the previous proxy tunnel didn't support redirects e.g. 307. don't need to
  // introduce new behavior. can't follow redirects on connect anyway.
  nsCOMPtr<nsIChannel> localChannel;
  rv = ioService->NewChannelFromURIWithProxyFlags(
      mURI, mURI,
      nsIProtocolProxyService::RESOLVE_PREFER_HTTPS_PROXY |
          nsIProtocolProxyService::RESOLVE_ALWAYS_TUNNEL,
      loadInfo->LoadingNode(), loadInfo->GetLoadingPrincipal(),
      loadInfo->TriggeringPrincipal(),
      // We need this flag to allow loads from any origin since this channel
      // is being used to CONNECT to an HTTP proxy.
      nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
      nsIContentPolicy::TYPE_PROXIED_WEBRTC_MEDIA,
      getter_AddRefs(localChannel));
  if (NS_FAILED(rv)) {
    LOG("WebrtcTCPSocket {}: bad open channel\n", fmt::ptr(this));
    return rv;
  }

  nsCOMPtr<nsILoadInfo> channelLoadInfo = localChannel->LoadInfo();
  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  loadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
  channelLoadInfo->SetCookieJarSettings(cookieJarSettings);

  RefPtr<nsHttpChannel> httpChannel;
  CallQueryInterface(localChannel, httpChannel.StartAssignment());

  if (!httpChannel) {
    LOG("WebrtcTCPSocket {}: not an http channel\n", fmt::ptr(this));
    return NS_ERROR_FAILURE;
  }

  if (!mTls &&
      mozilla::StaticPrefs::media_webrtc_disallow_HTTPS_upgrade_for_TURN()) {
    loadInfo->SetSkipHTTPSUpgrade(true);
  }

  rv = localChannel->SetLoadInfo(loadInfo);
  NS_ENSURE_SUCCESS(rv, rv);

  httpChannel->SetNotificationCallbacks(this);

  // don't block webrtc proxy setup with other requests
  // often more than one of these channels will be created all at once by ICE
  nsCOMPtr<nsIClassOfService> cos = do_QueryInterface(localChannel);
  if (cos) {
    cos->AddClassFlags(nsIClassOfService::Unblocked |
                       nsIClassOfService::DontThrottle);
  } else {
    LOG("WebrtcTCPSocket {}: could not set class of service\n", fmt::ptr(this));
    return NS_ERROR_FAILURE;
  }

  rv = httpChannel->HTTPUpgrade("webrtc,c-webrtc"_ns, this);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  rv = httpChannel->SetConnectOnly(
      mozilla::StaticPrefs::media_webrtc_tls_tunnel_for_all_proxy());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = httpChannel->AsyncOpen(this);

  if (NS_FAILED(rv)) {
    LOG("WebrtcTCPSocket {}: cannot async open\n", fmt::ptr(this));
    return rv;
  }

  // This picks back up in OnTransportAvailable once we have connected to the
  // proxy, and performed the http upgrade to switch the proxy into passthrough
  // mode.

  return NS_OK;
}

void WebrtcTCPSocket::EnqueueWrite_s(nsTArray<uint8_t>&& aWriteData) {
  LOG("WebrtcTCPSocket::EnqueueWrite {}\n", fmt::ptr(this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (mClosed) {
    return;
  }

  mWriteQueue.emplace_back(std::move(aWriteData));

  if (mSocketOut) {
    mSocketOut->AsyncWait(this, 0, 0, nullptr);
  }
}

void WebrtcTCPSocket::InvokeOnClose(nsresult aReason) {
  LOG("WebrtcTCPSocket::InvokeOnClose {}\n", fmt::ptr(this));

  if (!NS_IsMainThread()) {
    MOZ_ALWAYS_SUCCEEDS(mMainThread->Dispatch(
        NewRunnableMethod<nsresult>("WebrtcTCPSocket::InvokeOnClose", this,
                                    &WebrtcTCPSocket::InvokeOnClose, aReason)));
    return;
  }

  MOZ_ASSERT(mProxyCallbacks, "webrtc TCP callback should be non-null");

  if (mProxyRequest) {
    mProxyRequest->Cancel(aReason);
    mProxyRequest = nullptr;
  }

  mProxyCallbacks->OnClose(aReason);
  mProxyCallbacks = nullptr;
}

void WebrtcTCPSocket::InvokeOnConnected() {
  LOG("WebrtcTCPSocket::InvokeOnConnected {}\n", fmt::ptr(this));

  if (!NS_IsMainThread()) {
    MOZ_ALWAYS_SUCCEEDS(mMainThread->Dispatch(
        NewRunnableMethod("WebrtcTCPSocket::InvokeOnConnected", this,
                          &WebrtcTCPSocket::InvokeOnConnected)));
    return;
  }

  MOZ_ASSERT(mProxyCallbacks, "webrtc TCP callback should be non-null");

  mProxyCallbacks->OnConnected(mProxyType);
}

void WebrtcTCPSocket::InvokeOnRead(nsTArray<uint8_t>&& aReadData) {
  LOG("WebrtcTCPSocket::InvokeOnRead {} count={}\n", fmt::ptr(this),
      aReadData.Length());

  if (!NS_IsMainThread()) {
    MOZ_ALWAYS_SUCCEEDS(
        mMainThread->Dispatch(NewRunnableMethod<nsTArray<uint8_t>&&>(
            "WebrtcTCPSocket::InvokeOnRead", this,
            &WebrtcTCPSocket::InvokeOnRead, std::move(aReadData))));
    return;
  }

  MOZ_ASSERT(mProxyCallbacks, "webrtc TCP callback should be non-null");

  mProxyCallbacks->OnRead(std::move(aReadData));
}

// nsIHttpUpgradeListener
NS_IMETHODIMP
WebrtcTCPSocket::OnTransportAvailable(nsISocketTransport* aTransport,
                                      nsIAsyncInputStream* aSocketIn,
                                      nsIAsyncOutputStream* aSocketOut) {
  // This is called only in the http proxy case, once we have connected to the
  // http proxy and performed the http upgrade to switch it over to passthrough
  // mode. That process is started async by OpenWithHttpProxy.
  LOG("WebrtcTCPSocket::OnTransportAvailable {}\n", fmt::ptr(this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!mTransport,
             "already called transport available on webrtc TCP socket");

  // Cancel any pending callbacks. The caller doesn't always cancel these
  // awaits. We need to make sure they don't get them.
  aSocketIn->AsyncWait(nullptr, 0, 0, nullptr);
  aSocketOut->AsyncWait(nullptr, 0, 0, nullptr);

  if (mClosed) {
    LOG("WebrtcTCPSocket::OnTransportAvailable {} closed\n", fmt::ptr(this));
    return NS_OK;
  }

  mTransport = aTransport;
  mSocketIn = aSocketIn;
  mSocketOut = aSocketOut;

  // pulled from nr_socket_prsock.cpp
  uint32_t minBufferSize = 256 * 1024;
  nsresult rv = mTransport->SetSendBufferSize(minBufferSize);
  if (NS_FAILED(rv)) {
    LOG("WebrtcProxyChannel::OnTransportAvailable {} send failed\n",
        fmt::ptr(this));
    CloseWithReason(rv);
    return rv;
  }
  rv = mTransport->SetRecvBufferSize(minBufferSize);
  if (NS_FAILED(rv)) {
    LOG("WebrtcProxyChannel::OnTransportAvailable {} recv failed\n",
        fmt::ptr(this));
    CloseWithReason(rv);
    return rv;
  }

  FinishOpen();
  return NS_OK;
}

void WebrtcTCPSocket::FinishOpen() {
  MOZ_ASSERT(OnSocketThread());
  // mTransport, mSocketIn, and mSocketOut are all set. We may have set them in
  // OnTransportAvailable (in the http/https proxy case), or in
  // OpenWithoutHttpProxy. From here on out, this class functions the same for
  // these two cases.

  mSocketIn->AsyncWait(this, 0, 0, nullptr);

  InvokeOnConnected();
}

NS_IMETHODIMP
WebrtcTCPSocket::OnUpgradeFailed(nsresult aErrorCode) {
  LOG("WebrtcTCPSocket::OnUpgradeFailed {}\n", fmt::ptr(this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!mTransport,
             "already called transport available on webrtc TCP socket");

  if (mClosed) {
    LOG("WebrtcTCPSocket::OnUpgradeFailed {} closed\n", fmt::ptr(this));
    return NS_OK;
  }

  CloseWithReason(aErrorCode);

  return NS_OK;
}

// nsIRequestObserver (from nsIStreamListener)
NS_IMETHODIMP
WebrtcTCPSocket::OnStartRequest(nsIRequest* aRequest) {
  LOG("WebrtcTCPSocket::OnStartRequest {}\n", fmt::ptr(this));

  return NS_OK;
}

NS_IMETHODIMP
WebrtcTCPSocket::OnStopRequest(nsIRequest* aRequest, nsresult aStatusCode) {
  LOG("WebrtcTCPSocket::OnStopRequest {} status={}\n", fmt::ptr(this),
      static_cast<uint32_t>(aStatusCode));

  // see nsHttpChannel::ProcessFailedProxyConnect for most error codes
  if (NS_FAILED(aStatusCode)) {
    CloseWithReason(aStatusCode);
    return aStatusCode;
  }

  return NS_OK;
}

// nsIStreamListener
NS_IMETHODIMP
WebrtcTCPSocket::OnDataAvailable(nsIRequest* aRequest,
                                 nsIInputStream* aInputStream, uint64_t aOffset,
                                 uint32_t aCount) {
  LOG("WebrtcTCPSocket::OnDataAvailable {} count={}\n", fmt::ptr(this), aCount);
  MOZ_ASSERT(0, "unreachable data available");
  return NS_OK;
}

// nsIInputStreamCallback
NS_IMETHODIMP
WebrtcTCPSocket::OnInputStreamReady(nsIAsyncInputStream* in) {
  LOG("WebrtcTCPSocket::OnInputStreamReady {} unwritten={}\n", fmt::ptr(this),
      CountUnwrittenBytes());
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!mClosed, "webrtc TCP socket closed");
  MOZ_ASSERT(mTransport, "webrtc TCP socket not connected");
  MOZ_ASSERT(mSocketIn == in, "wrong input stream");

  while (true) {
    char buffer[9216];
    uint32_t remainingCapacity = sizeof(buffer);
    uint32_t read = 0;

    while (remainingCapacity > 0) {
      uint32_t count = 0;
      nsresult rv = mSocketIn->Read(buffer + read, remainingCapacity, &count);
      if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
        break;
      }

      if (NS_FAILED(rv)) {
        LOG("WebrtcTCPSocket::OnInputStreamReady {} failed {}\n",
            fmt::ptr(this), static_cast<uint32_t>(rv));
        CloseWithReason(rv);
        return rv;
      }

      // base stream closed
      if (count == 0) {
        LOG("WebrtcTCPSocket::OnInputStreamReady {} connection closed\n",
            fmt::ptr(this));
        CloseWithReason(NS_ERROR_FAILURE);
        return NS_OK;
      }

      remainingCapacity -= count;
      read += count;
    }

    if (read > 0) {
      nsTArray<uint8_t> array(read);
      array.AppendElements(buffer, read);

      InvokeOnRead(std::move(array));
    }

    if (remainingCapacity != 0) {
      // Loop exited above, but not because we ran out of space. We're actually
      // done, break out of the while(true) loop.
      break;
    }
  }

  mSocketIn->AsyncWait(this, 0, 0, nullptr);
  return NS_OK;
}

// nsIOutputStreamCallback
NS_IMETHODIMP
WebrtcTCPSocket::OnOutputStreamReady(nsIAsyncOutputStream* out) {
  LOG("WebrtcTCPSocket::OnOutputStreamReady {} unwritten={}\n", fmt::ptr(this),
      CountUnwrittenBytes());
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!mClosed, "webrtc TCP socket closed");
  MOZ_ASSERT(mTransport, "webrtc TCP socket not connected");
  MOZ_ASSERT(mSocketOut == out, "wrong output stream");

  while (!mWriteQueue.empty()) {
    const WebrtcTCPData& data = mWriteQueue.front();

    char* buffer = reinterpret_cast<char*>(
                       const_cast<uint8_t*>(data.GetData().Elements())) +
                   mWriteOffset;
    uint32_t toWrite = data.GetData().Length() - mWriteOffset;

    uint32_t wrote = 0;
    nsresult rv = mSocketOut->Write(buffer, toWrite, &wrote);
    if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
      mSocketOut->AsyncWait(this, 0, 0, nullptr);
      return NS_OK;
    }

    if (NS_FAILED(rv)) {
      LOG("WebrtcTCPSocket::OnOutputStreamReady {} failed {}\n", fmt::ptr(this),
          static_cast<uint32_t>(rv));
      CloseWithReason(rv);
      return NS_OK;
    }

    mWriteOffset += wrote;

    if (toWrite == wrote) {
      mWriteOffset = 0;
      mWriteQueue.pop_front();
    }
  }

  return NS_OK;
}

// nsIInterfaceRequestor
NS_IMETHODIMP
WebrtcTCPSocket::GetInterface(const nsIID& iid, void** result) {
  LOG("WebrtcTCPSocket::GetInterface {}\n", fmt::ptr(this));

  return QueryInterface(iid, result);
}

size_t WebrtcTCPSocket::CountUnwrittenBytes() const {
  size_t count = 0;

  for (const WebrtcTCPData& data : mWriteQueue) {
    count += data.GetData().Length();
  }

  MOZ_ASSERT(count >= mWriteOffset, "offset exceeds write buffer length");

  count -= mWriteOffset;

  return count;
}

}  // namespace mozilla::net
