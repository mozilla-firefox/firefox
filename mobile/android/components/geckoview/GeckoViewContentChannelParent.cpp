/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GeckoViewContentChannelParent.h"

#include "GeckoViewContentChannel.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/net/NeckoParent.h"
#include "mozilla/NotNull.h"
#include "nsIAuthPromptProvider.h"
#include "nsISecureBrowserUI.h"

namespace mozilla::net {

NS_IMPL_ISUPPORTS(GeckoViewContentChannelParent, nsIParentChannel,
                  nsIStreamListener, nsIInterfaceRequestor)

GeckoViewContentChannelParent::GeckoViewContentChannelParent(
    dom::BrowserParent* aIframeEmbedding, nsILoadContext* aLoadContext)
    : mLoadContext(aLoadContext), mBrowserParent(aIframeEmbedding) {}

NS_IMETHODIMP
GeckoViewContentChannelParent::SetParentListener(
    ParentChannelListener* aListener) {
  // Nothing to do.
  return NS_OK;
}

NS_IMETHODIMP
GeckoViewContentChannelParent::NotifyClassificationFlags(
    uint32_t aClassificationFlags, bool aIsThirdParty) {
  // Nothing to do.
  return NS_OK;
}

NS_IMETHODIMP
GeckoViewContentChannelParent::SetClassifierMatchedInfo(
    const nsACString& aList, const nsACString& aProvider,
    const nsACString& aFullHash) {
  // nothing to do
  return NS_OK;
}

NS_IMETHODIMP
GeckoViewContentChannelParent::SetClassifierMatchedTrackingInfo(
    const nsACString& aLists, const nsACString& aFullHashes) {
  // nothing to do
  return NS_OK;
}

NS_IMETHODIMP
GeckoViewContentChannelParent::Delete() {
  if (!CanSend()) {
    return NS_ERROR_UNEXPECTED;
  }
  (void)SendDeleteSelf();
  return NS_OK;
}

NS_IMETHODIMP
GeckoViewContentChannelParent::GetRemoteType(dom::RemoteType& aRemoteType) {
  if (!CanSend()) {
    return NS_ERROR_UNEXPECTED;
  }

  dom::PContentParent* pcp = Manager()->Manager();
  aRemoteType = static_cast<dom::ContentParent*>(pcp)->GetRemoteType();
  return NS_OK;
}

NS_IMETHODIMP
GeckoViewContentChannelParent::OnStartRequest(nsIRequest* aRequest) {
  if (!CanSend()) {
    return NS_ERROR_UNEXPECTED;
  }
  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);

  nsCString contentType;
  channel->GetContentType(contentType);

  nsresult channelStatus = NS_OK;
  channel->GetStatus(&channelStatus);

  nsCString entityID;

  nsCOMPtr<nsIURI> uri;
  channel->GetURI(getter_AddRefs(uri));
  if (MOZ_UNLIKELY(!uri)) {
    return NS_ERROR_UNEXPECTED;
  }

  (void)SendOnStartRequest(channelStatus, contentType, entityID,
                           WrapNotNull(uri));

  return NS_OK;
}

NS_IMETHODIMP
GeckoViewContentChannelParent::OnStopRequest(nsIRequest* aRequest,
                                             nsresult aStatusCode) {
  if (!CanSend() || !SendOnStopRequest(aStatusCode)) {
    return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;
}

NS_IMETHODIMP
GeckoViewContentChannelParent::OnDataAvailable(nsIRequest* aRequest,
                                               nsIInputStream* aInputStream,
                                               uint64_t aOffset,
                                               uint32_t aCount) {
  if (!CanSend()) {
    return NS_ERROR_UNEXPECTED;
  }
  nsCString data;
  nsresult rv = NS_ReadInputStreamToString(aInputStream, data, aCount);
  if (MOZ_UNLIKELY(NS_FAILED(rv))) {
    return rv;
  }

  nsresult channelStatus = NS_OK;
  mChannel->GetStatus(&channelStatus);

  (void)SendOnDataAvailable(channelStatus, data, aOffset);

  return NS_OK;
}

NS_IMETHODIMP
GeckoViewContentChannelParent::GetInterface(const nsIID& uuid, void** result) {
  if (uuid.Equals(NS_GET_IID(nsIAuthPromptProvider)) ||
      uuid.Equals(NS_GET_IID(nsISecureBrowserUI))) {
    if (mBrowserParent) {
      return mBrowserParent->QueryInterface(uuid, result);
    }
  }

  // Only support nsILoadContext if child channel's callbacks did too
  if (uuid.Equals(NS_GET_IID(nsILoadContext)) && mLoadContext) {
    nsCOMPtr<nsILoadContext> copy = mLoadContext;
    copy.forget(result);
    return NS_OK;
  }

  return QueryInterface(uuid, result);
}

bool GeckoViewContentChannelParent::Init(
    const GeckoViewContentChannelOpenArgs& aArgs) {
  nsresult rv = NS_OK;

  auto scopeExit = MakeScopeExit([&rv, this] {
    if (NS_FAILED(rv)) {
      (void)SendOnAsyncOpenFailed(rv);
    }
  });

  nsCOMPtr<nsIURI> uri(aArgs.uri());

  if (!uri || !uri->SchemeIs("content")) {
    rv = NS_ERROR_UNKNOWN_PROTOCOL;
    return false;
  }

  dom::RemoteType remoteType;
  rv = GetRemoteType(remoteType);
  if (MOZ_UNLIKELY(NS_FAILED(rv))) {
    return false;
  }

  nsCOMPtr<nsILoadInfo> loadInfo;
  rv = mozilla::ipc::LoadInfoArgsToLoadInfo(aArgs.loadInfo(), remoteType,
                                            getter_AddRefs(loadInfo));
  if (MOZ_UNLIKELY(NS_FAILED(rv))) {
    return false;
  }

  nsCOMPtr<nsIIOService> ios(do_GetIOService(&rv));
  if (MOZ_UNLIKELY(NS_FAILED(rv))) {
    return false;
  }

  nsCOMPtr<nsIChannel> channel;
  rv = NS_NewChannelInternal(getter_AddRefs(channel), uri, loadInfo, nullptr,
                             nullptr, nullptr, aArgs.loadFlags(), ios);
  if (MOZ_UNLIKELY(NS_FAILED(rv))) {
    return false;
  }

  rv = channel->AsyncOpen(this);
  if (MOZ_UNLIKELY(NS_FAILED(rv))) {
    return false;
  }

  mChannel = channel;

  return true;
}

bool GeckoViewContentChannelParent::Init(
    const GeckoViewContentChannelConnectArgs& aArgs) {
  nsCOMPtr<nsIChannel> channel;
  dom::ContentParentId cpId =
      static_cast<dom::ContentParent*>(Manager()->Manager())->ChildID();
  nsresult rv = NS_LinkRedirectChannels(aArgs.channelId(), cpId, this,
                                        getter_AddRefs(channel));
  if (NS_SUCCEEDED(rv)) {
    mChannel = channel;
  }
  return true;
}

bool GeckoViewContentChannelParent::Init(
    const GeckoViewContentChannelArgs& aArgs) {
  switch (aArgs.type()) {
    case GeckoViewContentChannelArgs::TGeckoViewContentChannelOpenArgs:
      return Init(aArgs.get_GeckoViewContentChannelOpenArgs());
    case GeckoViewContentChannelArgs::TGeckoViewContentChannelConnectArgs:
      return Init(aArgs.get_GeckoViewContentChannelConnectArgs());
    default:
      return false;
  }
}

mozilla::ipc::IPCResult GeckoViewContentChannelParent::RecvCancel(
    const nsresult& status) {
  if (mChannel) {
    mChannel->Cancel(status);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult GeckoViewContentChannelParent::RecvSuspend() {
  if (mChannel) {
    mChannel->Suspend();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult GeckoViewContentChannelParent::RecvResume() {
  if (mChannel) {
    mChannel->Resume();
  }
  return IPC_OK();
}

}  // namespace mozilla::net
