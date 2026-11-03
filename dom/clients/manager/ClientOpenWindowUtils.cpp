/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClientOpenWindowUtils.h"

#include "ClientInfo.h"
#include "ClientManager.h"
#include "ClientState.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/nsCSPContext.h"
#include "nsContentUtils.h"
#include "nsDocShell.h"
#include "nsDocShellLoadState.h"
#include "nsFocusManager.h"
#include "nsGlobalWindowOuter.h"
#include "nsIBrowser.h"
#include "nsIBrowserDOMWindow.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeOwner.h"
#include "nsIMutableArray.h"
#include "nsISupportsPrimitives.h"
#include "nsIURI.h"
#include "nsIWebProgress.h"
#include "nsIWebProgressListener.h"
#include "nsIWindowMediator.h"
#include "nsIWindowWatcher.h"
#include "nsIXPConnect.h"
#include "nsNetUtil.h"
#include "nsOpenWindowInfo.h"
#include "nsPIDOMWindow.h"
#include "nsPIWindowWatcher.h"
#include "nsPrintfCString.h"
#include "nsWindowWatcher.h"

#ifdef MOZ_GECKOVIEW
#  include "mozilla/dom/Promise-inl.h"
#  include "nsIGeckoViewServiceWorker.h"
#  include "nsImportModule.h"
#endif

namespace mozilla::dom {

namespace {

class WebProgressListener final : public nsIWebProgressListener,
                                  public nsSupportsWeakReference {
 public:
  NS_DECL_ISUPPORTS

  WebProgressListener(BrowsingContext* aBrowsingContext, nsIURI* aBaseURI,
                      already_AddRefed<ClientOpPromise::Private> aPromise)
      : mPromise(aPromise),
        mBaseURI(aBaseURI),
        mBrowserId(aBrowsingContext->GetBrowserId()) {
    MOZ_ASSERT(mBrowserId != 0);
    MOZ_ASSERT(aBaseURI);
    MOZ_ASSERT(NS_IsMainThread());
  }

  NS_IMETHOD
  OnStateChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                uint32_t aStateFlags, nsresult aStatus) override {
    if (!(aStateFlags & STATE_IS_WINDOW) ||
        !(aStateFlags & (STATE_STOP | STATE_TRANSFERRING))) {
      return NS_OK;
    }

    // Our browsing context may have been discarded before finishing the load,
    // this is a navigation error.
    RefPtr<CanonicalBrowsingContext> browsingContext =
        CanonicalBrowsingContext::Cast(
            BrowsingContext::GetCurrentTopByBrowserId(mBrowserId));
    if (!browsingContext || browsingContext->IsDiscarded()) {
      CopyableErrorResult rv;
      rv.ThrowInvalidStateError("Unable to open window");
      mPromise->Reject(rv, __func__);
      mPromise = nullptr;
      if (browsingContext) {
        if (nsCOMPtr<nsIWebProgress> webProgress =
                browsingContext->GetWebProgress()) {
          (void)webProgress->RemoveProgressListener(this);
        }
      }
      return NS_OK;
    }

    // Our caller keeps a strong reference, so it is safe to remove the listener
    // from the BrowsingContext's nsIWebProgress.
    auto RemoveListener = [&] {
      nsCOMPtr<nsIWebProgress> webProgress = browsingContext->GetWebProgress();
      webProgress->RemoveProgressListener(this);
    };

    RefPtr<dom::WindowGlobalParent> wgp =
        browsingContext->GetCurrentWindowGlobal();
    if (NS_WARN_IF(!wgp)) {
      CopyableErrorResult rv;
      rv.ThrowInvalidStateError("Unable to open window");
      mPromise->Reject(rv, __func__);
      mPromise = nullptr;
      RemoveListener();
      return NS_OK;
    }

    if (NS_WARN_IF(wgp->IsInitialDocument())) {
      // This is the load of the initial document, which is not the document we
      // care about for the purposes of checking same-originness of the URL.
      return NS_OK;
    }

    RemoveListener();

    // Check same origin. If the origins do not match, resolve with null (per
    // step 7.2.7.1 of the openWindow spec).
    nsCOMPtr<nsIScriptSecurityManager> securityManager =
        nsContentUtils::GetSecurityManager();
    bool isPrivateWin =
        wgp->DocumentPrincipal()->OriginAttributesRef().IsPrivateBrowsing();
    nsresult rv = securityManager->CheckSameOriginURI(
        wgp->GetDocumentURI(), mBaseURI, false, isPrivateWin);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      mPromise->Resolve(CopyableErrorResult(), __func__);
      mPromise = nullptr;
      return NS_OK;
    }

    Maybe<ClientInfo> info = wgp->GetClientInfo();
    if (info.isNothing()) {
      CopyableErrorResult rv;
      rv.ThrowInvalidStateError("Unable to open window");
      mPromise->Reject(rv, __func__);
      mPromise = nullptr;
      return NS_OK;
    }

    const nsID& id = info.ref().Id();
    const mozilla::ipc::PrincipalInfo& principal = info.ref().PrincipalInfo();
    ClientManager::GetInfoAndState(ClientGetInfoAndStateArgs(id, principal),
                                   GetCurrentSerialEventTarget())
        ->ChainTo(mPromise.forget(), __func__);

    return NS_OK;
  }

  NS_IMETHOD
  OnProgressChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                   int32_t aCurSelfProgress, int32_t aMaxSelfProgress,
                   int32_t aCurTotalProgress,
                   int32_t aMaxTotalProgress) override {
    MOZ_ASSERT(false, "Unexpected notification.");
    return NS_OK;
  }

  NS_IMETHOD
  OnLocationChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                   nsIURI* aLocation, uint32_t aFlags) override {
    MOZ_ASSERT(false, "Unexpected notification.");
    return NS_OK;
  }

  NS_IMETHOD
  OnStatusChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                 nsresult aStatus, const char16_t* aMessage) override {
    MOZ_ASSERT(false, "Unexpected notification.");
    return NS_OK;
  }

  NS_IMETHOD
  OnSecurityChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                   uint32_t aState) override {
    MOZ_ASSERT(false, "Unexpected notification.");
    return NS_OK;
  }

  NS_IMETHOD
  OnContentBlockingEvent(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                         uint32_t aEvent) override {
    MOZ_ASSERT(false, "Unexpected notification.");
    return NS_OK;
  }

 private:
  ~WebProgressListener() {
    if (mPromise) {
      CopyableErrorResult rv;
      rv.ThrowAbortError("openWindow aborted");
      mPromise->Reject(rv, __func__);
      mPromise = nullptr;
    }
  }

  RefPtr<ClientOpPromise::Private> mPromise;
  nsCOMPtr<nsIURI> mBaseURI;
  uint64_t mBrowserId;
};

NS_IMPL_ISUPPORTS(WebProgressListener, nsIWebProgressListener,
                  nsISupportsWeakReference);

struct ClientOpenWindowArgsParsed {
  nsCOMPtr<nsIURI> uri;
  nsCOMPtr<nsIURI> baseURI;
  nsCOMPtr<nsIPrincipal> principal;
  nsCOMPtr<nsIPolicyContainer> policyContainer;
  RefPtr<ThreadsafeContentParentHandle> originContent;
};

#ifndef MOZ_GECKOVIEW

MOZ_CAN_RUN_SCRIPT
static Result<Ok, nsresult> OpenNewWindow(
    const ClientOpenWindowArgsParsed& aArgsValidated,
    nsOpenWindowInfo* aOpenWindowInfo) {
  nsresult rv;

  // XXX(krosylight): In an ideal world we should be able to pass the nsIURI
  // directly. See bug 1485961.
  nsAutoCString uriToLoad;
  MOZ_TRY(aArgsValidated.uri->GetSpec(uriToLoad));

  nsCOMPtr<nsISupportsCString> nsUriToLoad =
      do_CreateInstance(NS_SUPPORTS_CSTRING_CONTRACTID, &rv);
  MOZ_TRY(rv);
  MOZ_TRY(nsUriToLoad->SetData(uriToLoad));

  nsCOMPtr<nsISupportsPRBool> nsFalse =
      do_CreateInstance(NS_SUPPORTS_PRBOOL_CONTRACTID, &rv);
  MOZ_TRY(rv);
  MOZ_TRY(nsFalse->SetData(false));

  nsCOMPtr<nsISupportsPRUint32> userContextId =
      do_CreateInstance(NS_SUPPORTS_PRUINT32_CONTRACTID, &rv);
  MOZ_TRY(rv);
  MOZ_TRY(userContextId->SetData(aArgsValidated.principal->GetUserContextId()));

  nsCOMPtr<nsIMutableArray> args = do_CreateInstance(NS_ARRAY_CONTRACTID);
  // https://searchfox.org/mozilla-central/rev/02d33f4bf984f65bd394bfd2d19d66569ae2cfe1/browser/base/content/browser-init.js#725-735
  args->AppendElement(nsUriToLoad);               // 0: uriToLoad
  args->AppendElement(nullptr);                   // 1: extraOptions
  args->AppendElement(nullptr);                   // 2: referrerInfo
  args->AppendElement(nullptr);                   // 3: postData
  args->AppendElement(nsFalse);                   // 4: allowThirdPartyFixup
  args->AppendElement(userContextId);             // 5: userContextId
  args->AppendElement(nullptr);                   // 6: originPrincipal
  args->AppendElement(nullptr);                   // 7: originStoragePrincipal
  args->AppendElement(aArgsValidated.principal);  // 8: triggeringPrincipal
  args->AppendElement(nsFalse);                   // 9: allowInheritPrincipal
  args->AppendElement(aArgsValidated.policyContainer);  // 10: policyContainer
  args->AppendElement(aOpenWindowInfo);                 // 11: nsOpenWindowInfo

  nsCOMPtr<nsIWindowWatcher> ww = do_GetService(NS_WINDOWWATCHER_CONTRACTID);
  nsCString features = "chrome,all,dialog=no"_ns;

  if (aArgsValidated.principal->GetIsInPrivateBrowsing()) {
    // Private browsing would generally have a window, but with
    // browser.privatebrowsing.autostart=true it's still possible to get into
    // this path. See also bug 1972335.
    features += ",private";
  }

  nsCOMPtr<mozIDOMWindowProxy> win;
  MOZ_TRY(ww->OpenWindow(nullptr, nsDependentCString(BROWSER_CHROME_URL_QUOTED),
                         u"_blank"_ns, features, args, getter_AddRefs(win)));
  return Ok();
}

/**
 * @return true when the caller need to load URI on the resulting browsing
 *         context, otherwise false
 */
MOZ_CAN_RUN_SCRIPT
bool OpenWindow(const ClientOpenWindowArgsParsed& aArgsValidated,
                nsOpenWindowInfo* aOpenInfo, BrowsingContext** aBC,
                ErrorResult& aRv) {
  MOZ_DIAGNOSTIC_ASSERT(aBC);

  // [[6.1 Open Window]]

  // Find the most recent browser window and open a new tab in it.
  WindowMediatorFilter filter = WindowMediatorFilter::SkipClosed;
  if (aArgsValidated.principal->GetIsInPrivateBrowsing()) {
    filter |= WindowMediatorFilter::SkipNonPrivateBrowsing;
  } else {
    filter |= WindowMediatorFilter::SkipPrivateBrowsing;
  }
  nsCOMPtr<nsPIDOMWindowOuter> browserWindow =
      nsContentUtils::GetMostRecentWindowBy(filter);
  if (!browserWindow) {
    // It is possible to be running without a browser window (either because
    // macOS hidden window or non-browser windows like Library), so we need to
    // open a new chrome window.
    auto result = OpenNewWindow(aArgsValidated, aOpenInfo);
    if (NS_WARN_IF(result.isErr())) {
      aRv.ThrowTypeError("Unable to open window");
      return false;
    }
    return false;
  }

  if (NS_WARN_IF(!nsGlobalWindowOuter::Cast(browserWindow)->IsChromeWindow())) {
    // XXXbz Can this actually happen?  Seems unlikely.
    aRv.ThrowTypeError("Unable to open window");
    return false;
  }

  nsCOMPtr<nsIBrowserDOMWindow> bwin =
      nsGlobalWindowOuter::Cast(browserWindow)->GetBrowserDOMWindow();

  if (NS_WARN_IF(!bwin)) {
    aRv.ThrowTypeError("Unable to open window");
    return false;
  }
  nsresult rv = bwin->CreateContentWindow(
      nullptr, aOpenInfo, nsIBrowserDOMWindow::OPEN_DEFAULTWINDOW,
      nsIBrowserDOMWindow::OPEN_NEW, aArgsValidated.principal,
      aArgsValidated.policyContainer, aBC);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.ThrowTypeError("Unable to open window");
    return false;
  }
  return true;
}
#endif

MOZ_CAN_RUN_SCRIPT
void WaitForLoad(const ClientOpenWindowArgsParsed& aArgsValidated,
                 BrowsingContext* aBrowsingContext,
                 ClientOpPromise::Private* aPromise, bool aShouldLoadURI) {
  MOZ_DIAGNOSTIC_ASSERT(aBrowsingContext);

  RefPtr<ClientOpPromise::Private> promise = aPromise;
  // We can get a WebProgress off of
  // the BrowsingContext for the <xul:browser> to listen for content
  // events. Note that this WebProgress filters out events which don't have
  // STATE_IS_NETWORK or STATE_IS_REDIRECTED_DOCUMENT set on them, and so this
  // listener will only see some web progress events.
  nsCOMPtr<nsIWebProgress> webProgress =
      aBrowsingContext->Canonical()->GetWebProgress();
  if (NS_WARN_IF(!webProgress)) {
    CopyableErrorResult result;
    result.ThrowInvalidStateError("Unable to watch window for navigation");
    promise->Reject(result, __func__);
    return;
  }

  // Add a progress listener before we start the load of the requested URI
  RefPtr<WebProgressListener> listener = new WebProgressListener(
      aBrowsingContext, aArgsValidated.baseURI, do_AddRef(promise));

  nsresult rv = webProgress->AddProgressListener(
      listener, nsIWebProgress::NOTIFY_STATE_WINDOW);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    CopyableErrorResult result;
    // XXXbz Can we throw something better here?
    result.Throw(rv);
    promise->Reject(result, __func__);
    return;
  }

  if (aShouldLoadURI) {
    // Load the requested URI
    RefPtr<nsDocShellLoadState> loadState =
        new nsDocShellLoadState(aArgsValidated.uri);
    loadState->SetTriggeringPrincipal(aArgsValidated.principal);
    loadState->SetFirstParty(true);
    loadState->SetLoadFlags(
        nsIWebNavigation::LOAD_FLAGS_DISALLOW_INHERIT_PRINCIPAL);
    loadState->SetTriggeringRemoteType(
        aArgsValidated.originContent
            ? aArgsValidated.originContent->GetRemoteType()
            : RemoteType::NotRemote());

    rv = aBrowsingContext->LoadURI(loadState, true);
    if (NS_FAILED(rv)) {
      CopyableErrorResult result;
      result.ThrowInvalidStateError(
          "Unable to start the load of the actual URI");
      promise->Reject(result, __func__);
      return;
    }
  }

  // Hold the listener alive until the promise settles.
  promise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [listener](const ClientOpResult& aResult) {},
      [listener](const CopyableErrorResult& aResult) {});
}

#ifdef MOZ_GECKOVIEW
void GeckoViewOpenWindow(const ClientOpenWindowArgsParsed& aArgsValidated,
                         nsOpenWindowInfo* aOpenInfo, ErrorResult& aRv) {
  MOZ_ASSERT(aOpenInfo);

  // passes the request to open a new window to GeckoView. Allowing the
  // application to decide how to hand the open window request.
  nsCOMPtr<nsIGeckoViewServiceWorker> sw = do_ImportESModule(
      "moz-src:///mobile/shared/components/geckoview/"
      "GeckoViewServiceWorker.sys.mjs");
  MOZ_ASSERT(sw);

  RefPtr<dom::Promise> promise;
  nsresult rv =
      sw->OpenWindow(aArgsValidated.uri, aOpenInfo, getter_AddRefs(promise));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }

  promise->AddCallbacksWithCycleCollectedArgs(
      [](JSContext* aCx, JS::Handle<JS::Value> aValue, ErrorResult& aRv,
         nsOpenWindowInfo* aOpenWindowInfo) {
        if (aValue.isNull()) {
          // nsIBrowsingContextReadyCallback will be called when browsing
          // context is ready
          return;
        }

        auto cancelOpen =
            MakeScopeExit([&aOpenWindowInfo] { aOpenWindowInfo->Cancel(); });

        RefPtr<BrowsingContext> browsingContext;
        if (NS_WARN_IF(!aValue.isObject()) ||
            NS_WARN_IF(NS_FAILED(
                UNWRAP_OBJECT(BrowsingContext, aValue, browsingContext)))) {
          return;
        }

        if (nsIBrowsingContextReadyCallback* callback =
                aOpenWindowInfo->BrowsingContextReadyCallback()) {
          callback->BrowsingContextReady(browsingContext);
        }
        cancelOpen.release();
      },
      [](JSContext* aContext, JS::Handle<JS::Value> aValue, ErrorResult& aRv,
         nsOpenWindowInfo* aOpenWindowInfo) { aOpenWindowInfo->Cancel(); },
      RefPtr(aOpenInfo));
}
#endif  // MOZ_GECKOVIEW

}  // anonymous namespace

RefPtr<ClientOpPromise> ClientOpenWindow(
    ThreadsafeContentParentHandle* aOriginContent,
    const ClientOpenWindowArgs& aArgs) {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess());

  RefPtr<ClientOpPromise::Private> promise =
      new ClientOpPromise::Private(__func__);

  // [[1. Let url be the result of parsing url with entry settings object's API
  //   base URL.]]
  nsCOMPtr<nsIURI> baseURI;
  nsresult rv = NS_NewURI(getter_AddRefs(baseURI), aArgs.baseURL());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    nsPrintfCString err("Invalid base URL \"%s\"", aArgs.baseURL().get());
    CopyableErrorResult errResult;
    errResult.ThrowTypeError(err);
    promise->Reject(errResult, __func__);
    return promise;
  }

  nsCOMPtr<nsIURI> uri;
  rv = NS_NewURI(getter_AddRefs(uri), aArgs.url(), nullptr, baseURI);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    nsPrintfCString err("Invalid URL \"%s\"", aArgs.url().get());
    CopyableErrorResult errResult;
    errResult.ThrowTypeError(err);
    promise->Reject(errResult, __func__);
    return promise;
  }

  auto principalOrErr = PrincipalInfoToPrincipal(aArgs.principalInfo());
  if (NS_WARN_IF(principalOrErr.isErr())) {
    CopyableErrorResult errResult;
    errResult.ThrowTypeError("Failed to obtain principal");
    promise->Reject(errResult, __func__);
    return promise;
  }
  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();
  MOZ_DIAGNOSTIC_ASSERT(principal);

  rv = nsContentUtils::GetSecurityManager()->CheckLoadURIWithPrincipal(
      principal, uri, nsIScriptSecurityManager::DONT_REPORT_ERRORS, 0);
  if (NS_FAILED(rv)) {
    nsPrintfCString err("Opening \"%s\" is not allowed", aArgs.url().get());
    CopyableErrorResult errResult;
    errResult.ThrowTypeError(err);
    promise->Reject(errResult, __func__);
    return promise;
  }

  nsCOMPtr<nsIContentSecurityPolicy> csp;
  nsCOMPtr<PolicyContainer> policyContainer;
  if (aArgs.cspInfo().isSome()) {
    csp = CSPInfoToCSP(aArgs.cspInfo().ref(), nullptr);
    policyContainer = new PolicyContainer();
    PolicyContainer::Cast(policyContainer)->SetCSP(csp);
  }
  ClientOpenWindowArgsParsed argsValidated{
      .uri = uri,
      .baseURI = baseURI,
      .principal = principal,
      .policyContainer = policyContainer,
      .originContent = aOriginContent,
  };

  RefPtr<BrowsingContextCallbackReceivedPromise::Private>
      browsingContextReadyPromise =
          new BrowsingContextCallbackReceivedPromise::Private(__func__);
  RefPtr<nsIBrowsingContextReadyCallback> callback =
      new nsBrowsingContextReadyCallback(browsingContextReadyPromise);

  RefPtr<nsOpenWindowInfo> openInfo = new nsOpenWindowInfo();
  openInfo->mBrowsingContextReadyCallback = callback;
  nsCOMPtr<nsIURI> nullPrincipalURI = NullPrincipal::CreateURI(nullptr);
  nsCOMPtr<nsIPrincipal> initialPrincipal =
      NullPrincipal::Create(principal->OriginAttributesRef(), nullPrincipalURI);
  openInfo->mPrincipalToInheritForAboutBlank = std::move(initialPrincipal);
  openInfo->mIsRemote = true;

  RefPtr<BrowsingContext> bc;
  IgnoredErrorResult errResult;
  bool shouldLoadURI = true;
#ifdef MOZ_GECKOVIEW
  // GeckoView has a delegation for service worker window.
  GeckoViewOpenWindow(argsValidated, openInfo, errResult);
#else
  shouldLoadURI =
      OpenWindow(argsValidated, openInfo, getter_AddRefs(bc), errResult);
#endif
  if (NS_WARN_IF(errResult.Failed())) {
    promise->Reject(errResult, __func__);
    return promise;
  }

  browsingContextReadyPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [argsValidated, promise,
       shouldLoadURI](const RefPtr<BrowsingContext>& aBC) MOZ_CAN_RUN_SCRIPT {
        WaitForLoad(argsValidated, aBC, promise, shouldLoadURI);
      },
      [promise]() {
        // in case of failure, reject the original promise
        CopyableErrorResult result;
        result.ThrowTypeError("Unable to open window");
        promise->Reject(result, __func__);
      });
  if (bc) {
    browsingContextReadyPromise->Resolve(bc, __func__);
  }
  return promise;
}

}  // namespace mozilla::dom
