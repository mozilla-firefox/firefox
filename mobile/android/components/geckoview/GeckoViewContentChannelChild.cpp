/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GeckoViewContentChannelChild.h"

#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/net/NeckoChild.h"
#include "nsContentSecurityManager.h"
#include "nsIBrowserChild.h"
#include "nsStringStream.h"
#include "SerializedLoadContext.h"

namespace mozilla::net {

NS_IMPL_ISUPPORTS_INHERITED(GeckoViewContentChannelChild, nsBaseChannel,
                            nsIChildChannel)

GeckoViewContentChannelChild::GeckoViewContentChannelChild(nsIURI* aURI)
    : mEventQ(new ChannelEventQueue(static_cast<nsIChildChannel*>(this))) {
  SetURI(aURI);
  SetOriginalURI(aURI);
}

NS_IMETHODIMP
GeckoViewContentChannelChild::ConnectParent(uint32_t aId) {
  mozilla::dom::BrowserChild* browserChild = nullptr;
  nsCOMPtr<nsIBrowserChild> iBrowserChild;
  NS_QueryNotificationCallbacks(mCallbacks, mLoadGroup,
                                NS_GET_IID(nsIBrowserChild),
                                getter_AddRefs(iBrowserChild));
  GetCallback(iBrowserChild);
  if (iBrowserChild) {
    browserChild =
        static_cast<mozilla::dom::BrowserChild*>(iBrowserChild.get());
  }

  GeckoViewContentChannelConnectArgs connectArgs(aId);
  if (!gNeckoChild->SendPGeckoViewContentChannelConstructor(
          this, browserChild, IPC::SerializedLoadContext(this), connectArgs)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

NS_IMETHODIMP
GeckoViewContentChannelChild::CompleteRedirectSetup(
    nsIStreamListener* aListener) {
  mListener = aListener;

  if (mLoadGroup) {
    mLoadGroup->AddRequest(this, nullptr);
  }

  return NS_OK;
}

void GeckoViewContentChannelChild::ActorDestroy(ActorDestroyReason why) {}

NS_IMETHODIMP
GeckoViewContentChannelChild::AsyncOpen(nsIStreamListener* aListener) {
  nsCOMPtr<nsIStreamListener> listener = aListener;

  nsresult rv =
      nsContentSecurityManager::doContentSecurityCheck(this, listener);
  if (MOZ_UNLIKELY(NS_FAILED(rv))) {
    return rv;
  }

  NS_ENSURE_TRUE(!!gNeckoChild, NS_ERROR_FAILURE);
  NS_ENSURE_TRUE(!dom::ContentChild::GetSingleton()->IsShuttingDown(),
                 NS_ERROR_FAILURE);
  NS_ENSURE_TRUE(!mWasOpened, NS_ERROR_ALREADY_OPENED);
  NS_ENSURE_ARG(listener);

  // Ensure that this is an allowed port before proceeding.
  rv = NS_CheckPortSafety(nsBaseChannel::URI());
  if (MOZ_UNLIKELY(NS_FAILED(rv))) {
    return rv;
  }

  if (mLoadGroup) {
    mLoadGroup->AddRequest(this, nullptr);
  }

  uint32_t loadFlags = 0;
  GetLoadFlags(&loadFlags);

  GeckoViewContentChannelOpenArgs openArgs;
  openArgs.uri() = nsBaseChannel::URI();
  openArgs.loadFlags() = loadFlags;

  nsCOMPtr<nsILoadInfo> loadInfo = nsBaseChannel::LoadInfo();
  rv = mozilla::ipc::LoadInfoToLoadInfoArgs(loadInfo, &openArgs.loadInfo());
  if (MOZ_UNLIKELY(NS_FAILED(rv))) {
    return rv;
  }

  mozilla::dom::BrowserChild* browserChild = nullptr;
  nsCOMPtr<nsIBrowserChild> iBrowserChild;
  NS_QueryNotificationCallbacks(mCallbacks, mLoadGroup,
                                NS_GET_IID(nsIBrowserChild),
                                getter_AddRefs(iBrowserChild));
  GetCallback(iBrowserChild);
  if (iBrowserChild) {
    browserChild =
        static_cast<mozilla::dom::BrowserChild*>(iBrowserChild.get());
  }

  if (!gNeckoChild->SendPGeckoViewContentChannelConstructor(
          this, browserChild, IPC::SerializedLoadContext(this), openArgs)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

NS_IMETHODIMP
GeckoViewContentChannelChild::Cancel(nsresult aStatus) {
  if (mCanceled) {
    return NS_OK;
  }

  mCanceled = true;
  mStatus = aStatus;

  if (CanSend()) {
    SendCancel(aStatus);
  }

  return NS_OK;
}

NS_IMETHODIMP
GeckoViewContentChannelChild::Suspend() {
  if (!mSuspendCount++) {
    SendSuspend();
    mSuspendSent = true;
  }
  mEventQ->Suspend();

  return NS_OK;
}

NS_IMETHODIMP
GeckoViewContentChannelChild::Resume() {
  if (!mSuspendCount) {
    return NS_ERROR_UNEXPECTED;
  }

  if (!--mSuspendCount && mSuspendSent) {
    SendResume();
  }
  mEventQ->Resume();

  return NS_OK;
}

nsresult GeckoViewContentChannelChild::OpenContentStream(
    bool aAsync, nsIInputStream** aStream, nsIChannel** aChannel) {
  MOZ_ASSERT_UNREACHABLE(
      "GeckoViewContentChannel*Child* should never have OpenContentStream "
      "called!");
  return NS_OK;
}

mozilla::ipc::IPCResult GeckoViewContentChannelChild::RecvOnStartRequest(
    const nsresult& aChannelStatus, const nsACString& aContentType,
    const nsACString& aEntityID, mozilla::NotNull<nsIURI*> aURI) {
  mEventQ->RunOrEnqueue(std::make_unique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<GeckoViewContentChannelChild>(this),
             aChannelStatus, aContentType = nsCString(aContentType),
             aEntityID = nsCString(aEntityID), aURI = RefPtr{aURI.get()}]() {
        self->DoOnStartRequest(aChannelStatus, aContentType, aEntityID, aURI);
      }));
  return IPC_OK();
}

void GeckoViewContentChannelChild::DoOnStartRequest(
    const nsresult& aChannelStatus, const nsCString& aContentType,
    const nsCString& aEntityID, nsIURI* aURI) {
  // content:// doesn't know data length at this time.
  mContentLength = -1;
  SetContentType(aContentType);

  // Changes nsBaseChannel::URI(). The parent has already parsed this URI, so
  // adopt it directly instead of re-parsing its spec.
  mURI = aURI;

  AutoEventEnqueuer ensureSerialDispatch(mEventQ);
  nsCOMPtr<nsIStreamListener> listener = mListener;
  nsresult rv =
      listener->OnStartRequest(reinterpret_cast<nsBaseChannel*>(this));
  if (MOZ_UNLIKELY(NS_FAILED(rv))) {
    Cancel(rv);
  }
}

mozilla::ipc::IPCResult GeckoViewContentChannelChild::RecvOnDataAvailable(
    const nsresult& aChannelStatus, const nsACString& aData,
    const uint64_t& aOffset) {
  mEventQ->RunOrEnqueue(std::make_unique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<GeckoViewContentChannelChild>(this),
             aChannelStatus, aData = nsCString(aData), aOffset]() {
        self->DoOnDataAvailable(aChannelStatus, aData, aOffset);
      }));
  return IPC_OK();
}

void GeckoViewContentChannelChild::DoOnDataAvailable(
    const nsresult& aChannelStatus, const nsCString& aData,
    const uint64_t& aOffset) {
  nsCOMPtr<nsIInputStream> stringStream;
  nsresult rv = NS_NewByteInputStream(getter_AddRefs(stringStream), Span(aData),
                                      NS_ASSIGNMENT_DEPEND);
  if (MOZ_UNLIKELY(NS_FAILED(rv))) {
    Cancel(rv);
    return;
  }

  AutoEventEnqueuer ensureSerialDispatch(mEventQ);
  nsCOMPtr<nsIStreamListener> listener = mListener;
  rv = listener->OnDataAvailable(reinterpret_cast<nsBaseChannel*>(this),
                                 stringStream, aOffset, aData.Length());
  stringStream->Close();
  if (MOZ_UNLIKELY(NS_FAILED(rv))) {
    Cancel(rv);
  }
}

mozilla::ipc::IPCResult GeckoViewContentChannelChild::RecvOnStopRequest(
    const nsresult& aChannelStatus) {
  mEventQ->RunOrEnqueue(std::make_unique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<GeckoViewContentChannelChild>(this),
             aChannelStatus]() { self->DoOnStopRequest(aChannelStatus); }));
  return IPC_OK();
}

void GeckoViewContentChannelChild::DoOnStopRequest(
    const nsresult& aChannelStatus) {
  if (!mCanceled) {
    mStatus = aChannelStatus;
  }

  {
    AutoEventEnqueuer ensureSerialDispatch(mEventQ);
    nsCOMPtr<nsIStreamListener> listener = mListener;
    listener->OnStopRequest(reinterpret_cast<nsBaseChannel*>(this),
                            aChannelStatus);
    mListener = nullptr;

    if (mLoadGroup) {
      mLoadGroup->RemoveRequest(this, nullptr, aChannelStatus);
    }
  }

  Send__delete__(this);
}

mozilla::ipc::IPCResult GeckoViewContentChannelChild::RecvOnAsyncOpenFailed(
    const nsresult& aChannelStatus) {
  mEventQ->RunOrEnqueue(std::make_unique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<GeckoViewContentChannelChild>(this),
             aChannelStatus]() { self->DoOnAsyncOpenFailed(aChannelStatus); }));
  return IPC_OK();
}

void GeckoViewContentChannelChild::DoOnAsyncOpenFailed(
    const nsresult& aChannelStatus) {
  mStatus = aChannelStatus;

  if (mLoadGroup) {
    mLoadGroup->RemoveRequest(this, nullptr, aChannelStatus);
  }

  if (nsCOMPtr<nsIStreamListener> listener = mListener) {
    listener->OnStartRequest(reinterpret_cast<nsBaseChannel*>(this));
    listener->OnStopRequest(reinterpret_cast<nsBaseChannel*>(this),
                            aChannelStatus);
  }

  mListener = nullptr;

  if (CanSend()) {
    Send__delete__(this);
  }
}

mozilla::ipc::IPCResult GeckoViewContentChannelChild::RecvDeleteSelf() {
  mEventQ->RunOrEnqueue(std::make_unique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<GeckoViewContentChannelChild>(this)]() {
        self->DoDeleteSelf();
      }));
  return IPC_OK();
}

void GeckoViewContentChannelChild::DoDeleteSelf() {
  if (CanSend()) {
    Send__delete__(this);
  }
}

}  // namespace mozilla::net
