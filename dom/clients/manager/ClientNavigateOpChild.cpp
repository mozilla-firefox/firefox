/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClientNavigateOpChild.h"

#include "ClientSource.h"
#include "ClientSourceChild.h"
#include "ClientState.h"
#include "ReferrerInfo.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/PolicyContainer.h"
#include "nsDocShellLoadState.h"
#include "nsIDocShell.h"
#include "nsIWebNavigation.h"
#include "nsIWebProgress.h"
#include "nsIWebProgressListener.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"
#include "nsPIDOMWindowInlines.h"

namespace mozilla::dom {

namespace {

class NavigateLoadListener final : public nsIWebProgressListener,
                                   public nsSupportsWeakReference {
  RefPtr<ClientOpPromise::Private> mPromise;
  RefPtr<nsPIDOMWindowOuter> mOuterWindow;
  nsCOMPtr<nsIURI> mBaseURL;

  ~NavigateLoadListener() = default;

 public:
  NavigateLoadListener(ClientOpPromise::Private* aPromise,
                       nsPIDOMWindowOuter* aOuterWindow, nsIURI* aBaseURL)
      : mPromise(aPromise), mOuterWindow(aOuterWindow), mBaseURL(aBaseURL) {
    MOZ_DIAGNOSTIC_ASSERT(mPromise);
    MOZ_DIAGNOSTIC_ASSERT(mOuterWindow);
    MOZ_DIAGNOSTIC_ASSERT(mBaseURL);
  }

  NS_IMETHOD
  OnStateChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                uint32_t aStateFlags, nsresult aResult) override {
    if (!(aStateFlags & STATE_IS_DOCUMENT) ||
        !(aStateFlags & (STATE_STOP | STATE_TRANSFERRING))) {
      return NS_OK;
    }

    aWebProgress->RemoveProgressListener(this);

    nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
    if (!channel) {
      // This is not going to happen; how could it?
      CopyableErrorResult result;
      result.ThrowInvalidStateError("Bad request");
      mPromise->Reject(result, __func__);
      return NS_OK;
    }

    nsCOMPtr<nsIURI> channelURL;
    nsresult rv = NS_GetFinalChannelURI(channel, getter_AddRefs(channelURL));
    if (NS_FAILED(rv)) {
      CopyableErrorResult result;
      // XXXbz We can't actually get here; NS_GetFinalChannelURI never fails in
      // practice!
      result.Throw(rv);
      mPromise->Reject(result, __func__);
      return NS_OK;
    }

    nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
    MOZ_DIAGNOSTIC_ASSERT(ssm);

    // If the resulting window is not same origin, then resolve immediately
    // without returning any information about the new Client.  This is
    // step 6.10 in the Client.navigate(url) spec.
    // todo: if you intend to update CheckSameOriginURI to log the error to the
    // console you also need to update the 'aFromPrivateWindow' argument.
    rv = ssm->CheckSameOriginURI(mBaseURL, channelURL, false, false);
    if (NS_FAILED(rv)) {
      mPromise->Resolve(CopyableErrorResult(), __func__);
      return NS_OK;
    }

    nsPIDOMWindowInner* innerWindow = mOuterWindow->GetCurrentInnerWindow();
    MOZ_DIAGNOSTIC_ASSERT(innerWindow);

    Maybe<ClientInfo> clientInfo = innerWindow->GetClientInfo();
    MOZ_DIAGNOSTIC_ASSERT(clientInfo.isSome());

    Maybe<ClientState> clientState = innerWindow->GetClientState();
    MOZ_DIAGNOSTIC_ASSERT(clientState.isSome());

    // Otherwise, if the new window is same-origin we want to return a
    // ClientInfoAndState object so we can provide a Client snapshot
    // to the caller.  This is step 6.11 and 6.12 in the Client.navigate(url)
    // spec.
    mPromise->Resolve(
        ClientInfoAndState(clientInfo.ref().ToIPC(), clientState.ref().ToIPC()),
        __func__);

    return NS_OK;
  }

  NS_IMETHOD
  OnProgressChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                   int32_t aCurSelfProgress, int32_t aMaxSelfProgress,
                   int32_t aCurTotalProgress,
                   int32_t aMaxTotalProgress) override {
    MOZ_CRASH("Unexpected notification.");
    return NS_OK;
  }

  NS_IMETHOD
  OnLocationChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                   nsIURI* aLocation, uint32_t aFlags) override {
    MOZ_CRASH("Unexpected notification.");
    return NS_OK;
  }

  NS_IMETHOD
  OnStatusChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                 nsresult aStatus, const char16_t* aMessage) override {
    MOZ_CRASH("Unexpected notification.");
    return NS_OK;
  }

  NS_IMETHOD
  OnSecurityChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                   uint32_t aState) override {
    MOZ_CRASH("Unexpected notification.");
    return NS_OK;
  }

  NS_IMETHOD
  OnContentBlockingEvent(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                         uint32_t aEvent) override {
    MOZ_CRASH("Unexpected notification.");
    return NS_OK;
  }

  NS_DECL_ISUPPORTS
};

NS_IMPL_ISUPPORTS(NavigateLoadListener, nsIWebProgressListener,
                  nsISupportsWeakReference);

}  // anonymous namespace

RefPtr<ClientOpPromise> ClientNavigateOpChild::DoNavigate(
    const ClientNavigateOpConstructorArgs& aArgs,
    mozilla::ipc::ActorLifecycleProxy* aProxy) {
  nsCOMPtr<nsPIDOMWindowInner> window;

  // Navigating the target client window will result in the original
  // ClientSource being destroyed.  To avoid potential UAF mistakes
  // we use a small scope to access the ClientSource object.  Once
  // we have a strong reference to the window object we should not
  // access the ClientSource again.
  {
    ClientSourceChild* targetActor =
        static_cast<ClientSourceChild*>(aArgs.target().AsChild().get());
    MOZ_DIAGNOSTIC_ASSERT(targetActor);

    ClientSource* target = targetActor->GetSource();
    if (!target) {
      CopyableErrorResult rv;
      rv.ThrowInvalidStateError("Unknown Client");
      return ClientOpPromise::CreateAndReject(rv, __func__);
    }

    window = target->GetInnerWindow();
    if (!window) {
      CopyableErrorResult rv;
      rv.ThrowInvalidStateError("Client load for a destroyed Window");
      return ClientOpPromise::CreateAndReject(rv, __func__);
    }
  }

  MOZ_ASSERT(NS_IsMainThread());

  mSerialEventTarget = GetMainThreadSerialEventTarget();

  nsCOMPtr<nsIURI> url = aArgs.uri().get();
  nsCOMPtr<nsIURI> baseURL = aArgs.baseURI().get();

  RefPtr<Document> doc = window->GetExtantDoc();
  if (!doc || !doc->IsActive()) {
    CopyableErrorResult result;
    result.ThrowInvalidStateError("Document is not active.");
    return ClientOpPromise::CreateAndReject(result, __func__);
  }

  nsCOMPtr<nsIPrincipal> principal = doc->NodePrincipal();

  nsresult rv = nsContentUtils::GetSecurityManager()->CheckLoadURIWithPrincipal(
      principal, url, nsIScriptSecurityManager::STANDARD, doc->InnerWindowID());
  if (NS_FAILED(rv)) {
    nsPrintfCString err("Navigation to \"%s\" is not allowed",
                        url->GetSpecOrDefault().get());
    CopyableErrorResult result;
    result.ThrowTypeError(err);
    return ClientOpPromise::CreateAndReject(result, __func__);
  }

  nsCOMPtr<nsIDocShell> docShell = window->GetDocShell();
  nsCOMPtr<nsIWebProgress> webProgress = do_GetInterface(docShell);
  if (!docShell || !webProgress) {
    CopyableErrorResult result;
    result.ThrowInvalidStateError(
        "Document's browsing context has been discarded");
    return ClientOpPromise::CreateAndReject(result, __func__);
  }

  RefPtr<nsDocShellLoadState> loadState = new nsDocShellLoadState(url);
  loadState->SetTriggeringPrincipal(principal);
  loadState->SetTriggeringSandboxFlags(doc->GetSandboxFlags());
  loadState->SetPolicyContainer(doc->GetPolicyContainer());

  auto referrerInfo = MakeRefPtr<ReferrerInfo>(*doc);
  loadState->SetReferrerInfo(referrerInfo);
  loadState->SetLoadType(LOAD_STOP_CONTENT);
  loadState->SetSourceBrowsingContext(docShell->GetBrowsingContext());
  loadState->SetLoadFlags(nsIWebNavigation::LOAD_FLAGS_NONE);
  loadState->SetFirstParty(true);
  loadState->SetHasValidUserGestureActivation(
      doc->HasValidTransientUserGestureActivation());
  rv = docShell->LoadURI(loadState, false);
  if (NS_FAILED(rv)) {
    /// There are tests that try sending file:/// and mixed-content URLs
    /// in here and expect them to reject with a TypeError.  This does not match
    /// the spec, but does match the current behavior of both us and Chrome.
    /// https://github.com/w3c/ServiceWorker/issues/1500 tracks sorting that
    /// out.
    /// We now run security checks asynchronously, so these tests now
    /// just fail to load rather than hitting this failure path. I've
    /// marked them as failing for now until they get fixed to match the
    /// spec.
    nsPrintfCString err("Invalid URL \"%s\"", url->GetSpecOrDefault().get());
    CopyableErrorResult result;
    result.ThrowTypeError(err);
    return ClientOpPromise::CreateAndReject(result, __func__);
  }

  if (!aProxy->Get() || !CanSend()) {
    CopyableErrorResult result;
    result.ThrowInvalidStateError("Unknown Client");
    return ClientOpPromise::CreateAndReject(result, __func__);
  }

  RefPtr<ClientOpPromise::Private> promise =
      new ClientOpPromise::Private(__func__);

  nsCOMPtr<nsIWebProgressListener> listener =
      new NavigateLoadListener(promise, window->GetOuterWindow(), baseURL);

  rv = webProgress->AddProgressListener(listener,
                                        nsIWebProgress::NOTIFY_STATE_DOCUMENT);
  if (NS_FAILED(rv)) {
    CopyableErrorResult result;
    // XXXbz Can we throw something better here?
    result.Throw(rv);
    promise->Reject(result, __func__);
    return promise;
  }

  return promise->Then(
      mSerialEventTarget, __func__,
      [listener](const ClientOpPromise::ResolveOrRejectValue& aValue) {
        return ClientOpPromise::CreateAndResolveOrReject(aValue, __func__);
      });
}

void ClientNavigateOpChild::ActorDestroy(ActorDestroyReason aReason) {
  mPromiseRequestHolder.DisconnectIfExists();
}

void ClientNavigateOpChild::Init(const ClientNavigateOpConstructorArgs& aArgs,
                                 mozilla::ipc::ActorLifecycleProxy* aProxy) {
  RefPtr<ClientOpPromise> promise = DoNavigate(aArgs, aProxy);
  if (!aProxy->Get() || !CanSend()) {
    return;
  }

  // Normally we get the event target from the window in DoNavigate().  If a
  // failure occurred, though, we may need to fall back to the current thread
  // target.
  if (!mSerialEventTarget) {
    mSerialEventTarget = GetCurrentSerialEventTarget();
  }

  // Capturing `this` is safe here since we clear the mPromiseRequestHolder in
  // ActorDestroy.
  promise
      ->Then(
          mSerialEventTarget, __func__,
          [this](const ClientOpResult& aResult) {
            mPromiseRequestHolder.Complete();
            PClientNavigateOpChild::Send__delete__(this, aResult);
          },
          [this](const CopyableErrorResult& aResult) {
            mPromiseRequestHolder.Complete();
            PClientNavigateOpChild::Send__delete__(this, aResult);
          })
      ->Track(mPromiseRequestHolder);
}

}  // namespace mozilla::dom
