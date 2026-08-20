/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWindowWatcher.h"
#include "nsAutoWindowStateHelper.h"

#include "nsCRT.h"
#include "nsDebug.h"
#include "nsNetUtil.h"
#include "nsIAuthPrompt.h"
#include "nsIAuthPrompt2.h"
#include "nsISimpleEnumerator.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsJSUtils.h"

#include "nsDocShell.h"
#include "nsGlobalWindowInner.h"
#include "nsGlobalWindowOuter.h"
#include "nsHashPropertyBag.h"
#include "nsIBaseWindow.h"
#include "nsIBrowserDOMWindow.h"
#include "nsIDocShell.h"
#include "nsDocShellLoadState.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDocShellTreeOwner.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/BrowsingContextGroup.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/UserActivation.h"
#include "nsIDragService.h"
#include "nsIPrompt.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsIScreen.h"
#include "nsIScreenManager.h"
#include "nsIScriptContext.h"
#include "nsIObserverService.h"
#include "nsXPCOM.h"
#include "nsIURI.h"
#include "nsIWebBrowser.h"
#include "nsIWebBrowserChrome.h"
#include "nsIWebNavigation.h"
#include "nsIWindowCreator.h"
#include "nsIXULRuntime.h"
#include "nsPIDOMWindow.h"
#include "nsIWindowProvider.h"
#include "nsIMutableArray.h"
#include "nsIDOMStorageManager.h"
#include "nsIWidget.h"
#include "nsFocusManager.h"
#include "nsOpenWindowInfo.h"
#include "nsPresContext.h"
#include "nsContentUtils.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsSandboxFlags.h"
#include "nsSimpleEnumerator.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_middlemouse.h"
#include "mozilla/StaticPrefs_full_screen_api.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Storage.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/BrowserHost.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/dom/SessionStorageManager.h"
#include "mozilla/widget/ScreenManager.h"
#include "nsIAppWindow.h"
#include "nsIXULBrowserWindow.h"
#include "ReferrerInfo.h"

using namespace mozilla;
using namespace mozilla::dom;

/****************************************************************
 ******************** nsWatcherWindowEntry **********************
 ****************************************************************/

class nsWindowWatcher;

struct nsWatcherWindowEntry {
  nsWatcherWindowEntry(mozIDOMWindowProxy* aWindow,
                       nsIWebBrowserChrome* aChrome)
      : mWindow(do_GetWeakReference(aWindow)),
        mChrome(do_GetWeakReference(aChrome)) {
    ReferenceSelf();
  }
  ~nsWatcherWindowEntry() = default;

  void InsertAfter(nsWatcherWindowEntry* aOlder);
  void Unlink();
  void ReferenceSelf();

  nsWeakPtr mWindow;
  nsWeakPtr mChrome;
  // each struct is in a circular, doubly-linked list
  nsWatcherWindowEntry* mYounger;  // next younger in sequence
  nsWatcherWindowEntry* mOlder;
};

void nsWatcherWindowEntry::InsertAfter(nsWatcherWindowEntry* aOlder) {
  if (aOlder) {
    mOlder = aOlder;
    mYounger = aOlder->mYounger;
    mOlder->mYounger = this;
    if (mOlder->mOlder == mOlder) {
      mOlder->mOlder = this;
    }
    mYounger->mOlder = this;
    if (mYounger->mYounger == mYounger) {
      mYounger->mYounger = this;
    }
  }
}

void nsWatcherWindowEntry::Unlink() {
  mOlder->mYounger = mYounger;
  mYounger->mOlder = mOlder;
  ReferenceSelf();
}

void nsWatcherWindowEntry::ReferenceSelf() {
  mYounger = this;
  mOlder = this;
}

/****************************************************************
 ****************** nsWatcherWindowEnumerator *******************
 ****************************************************************/

class nsWatcherWindowEnumerator : public nsSimpleEnumerator {
 public:
  explicit nsWatcherWindowEnumerator(nsWindowWatcher* aWatcher);
  NS_IMETHOD HasMoreElements(bool* aResult) override;
  NS_IMETHOD GetNext(nsISupports** aResult) override;

 protected:
  ~nsWatcherWindowEnumerator() override;

 private:
  friend class nsWindowWatcher;

  nsWatcherWindowEntry* FindNext();
  void WindowRemoved(nsWatcherWindowEntry* aInfo);

  nsWindowWatcher* mWindowWatcher;
  nsWatcherWindowEntry* mCurrentPosition;
};

nsWatcherWindowEnumerator::nsWatcherWindowEnumerator(nsWindowWatcher* aWatcher)
    : mWindowWatcher(aWatcher), mCurrentPosition(aWatcher->mOldestWindow) {
  mWindowWatcher->AddEnumerator(this);
  mWindowWatcher->AddRef();
}

nsWatcherWindowEnumerator::~nsWatcherWindowEnumerator() {
  mWindowWatcher->RemoveEnumerator(this);
  mWindowWatcher->Release();
}

NS_IMETHODIMP
nsWatcherWindowEnumerator::HasMoreElements(bool* aResult) {
  if (!aResult) {
    return NS_ERROR_INVALID_ARG;
  }

  *aResult = !!mCurrentPosition;
  return NS_OK;
}

NS_IMETHODIMP
nsWatcherWindowEnumerator::GetNext(nsISupports** aResult) {
  if (!aResult) {
    return NS_ERROR_INVALID_ARG;
  }

  *aResult = nullptr;

  while (mCurrentPosition) {
    nsCOMPtr<mozIDOMWindowProxy> window =
        do_QueryReferent(mCurrentPosition->mWindow);
    if (window) {
      CallQueryInterface(window, aResult);
      mCurrentPosition = FindNext();
      return NS_OK;
    }
    mCurrentPosition = FindNext();
  }
  return NS_ERROR_FAILURE;
}

nsWatcherWindowEntry* nsWatcherWindowEnumerator::FindNext() {
  nsWatcherWindowEntry* info;

  if (!mCurrentPosition) {
    return nullptr;
  }

  info = mCurrentPosition->mYounger;
  return info == mWindowWatcher->mOldestWindow ? nullptr : info;
}

// if a window is being removed adjust the iterator's current position
void nsWatcherWindowEnumerator::WindowRemoved(nsWatcherWindowEntry* aInfo) {
  if (mCurrentPosition == aInfo) {
    mCurrentPosition =
        mCurrentPosition != aInfo->mYounger ? aInfo->mYounger : nullptr;
  }
}

/****************************************************************
 *********************** nsWindowWatcher ************************
 ****************************************************************/

NS_IMPL_ADDREF(nsWindowWatcher)
NS_IMPL_RELEASE(nsWindowWatcher)
NS_IMPL_QUERY_INTERFACE(nsWindowWatcher, nsIWindowWatcher, nsIPromptFactory,
                        nsPIWindowWatcher)

nsWindowWatcher::nsWindowWatcher()
    : mOldestWindow(nullptr), mListLock("nsWindowWatcher.mListLock") {}

nsWindowWatcher::~nsWindowWatcher() {
  // delete data
  while (mOldestWindow) {
    RemoveWindow(mOldestWindow);
  }
}

nsresult nsWindowWatcher::Init() { return NS_OK; }

/**
 * Convert aArguments into either an nsIArray or nullptr.
 *
 *  - If aArguments is nullptr, return nullptr.
 *  - If aArguments is an nsArray, return nullptr if it's empty, or otherwise
 *    return the array.
 *  - If aArguments is an nsIArray, return nullptr if it's empty, or
 *    otherwise just return the array.
 *  - Otherwise, return an nsIArray with one element: aArguments.
 */
static already_AddRefed<nsIArray> ConvertArgsToArray(nsISupports* aArguments) {
  if (!aArguments) {
    return nullptr;
  }

  nsCOMPtr<nsIArray> array = do_QueryInterface(aArguments);
  if (array) {
    uint32_t argc = 0;
    array->GetLength(&argc);
    if (argc == 0) {
      return nullptr;
    }

    return array.forget();
  }

  nsCOMPtr<nsIMutableArray> singletonArray =
      do_CreateInstance(NS_ARRAY_CONTRACTID);
  NS_ENSURE_TRUE(singletonArray, nullptr);

  nsresult rv = singletonArray->AppendElement(aArguments);
  NS_ENSURE_SUCCESS(rv, nullptr);

  return singletonArray.forget();
}

NS_IMETHODIMP
nsWindowWatcher::OpenWindow(mozIDOMWindowProxy* aParent, const nsACString& aUrl,
                            const nsACString& aName,
                            const nsACString& aFeatures,
                            nsISupports* aArguments,
                            mozIDOMWindowProxy** aResult) {
  nsCOMPtr<nsIArray> argv = ConvertArgsToArray(aArguments);

  uint32_t argc = 0;
  if (argv) {
    argv->GetLength(&argc);
  }
  bool dialog = (argc != 0);

  RefPtr<BrowsingContext> bc;
  MOZ_TRY(OpenWindowInternal(aParent, aUrl, aName, aFeatures,
                             mozilla::dom::UserActivation::Modifiers::None(),
                             /* calledFromJS = */ false, dialog,
                             /* navigate = */ true, argv,
                             /* aIsPopupSpam = */ false,
                             /* aForceNoOpener = */ false,
                             /* aForceNoReferrer = */ false, PRINT_NONE,
                             /* aLoadState = */ nullptr, getter_AddRefs(bc)));
  if (bc) {
    nsCOMPtr<mozIDOMWindowProxy> win(bc->GetDOMWindow());
    win.forget(aResult);
  }
  return NS_OK;
}

struct SizeSpec {
  SizeSpec() = default;

  // We store these in screen-independent pixels.
  Maybe<DesktopIntCoord> mLeft;
  Maybe<DesktopIntCoord> mTop;
  Maybe<CSSIntCoord> mOuterWidth;   // Total window width
  Maybe<CSSIntCoord> mOuterHeight;  // Total window height
  Maybe<CSSIntCoord> mInnerWidth;   // Content area width
  Maybe<CSSIntCoord> mInnerHeight;  // Content area height

  bool mLockAspectRatio = false;

  bool PositionSpecified() const { return mLeft.isSome() || mTop.isSome(); }

  bool SizeSpecified() const { return WidthSpecified() || HeightSpecified(); }

  bool WidthSpecified() const {
    return mOuterWidth.isSome() || mInnerWidth.isSome();
  }

  bool HeightSpecified() const {
    return mOuterHeight.isSome() || mInnerHeight.isSome();
  }

  void ScaleBy(float aOpenerZoom) {
    if (aOpenerZoom == 1.0f) {
      return;
    }
    auto Scale = [&aOpenerZoom](auto& aValue) {
      if (aValue) {
        *aValue = NSToIntRound(*aValue * aOpenerZoom);
      }
    };
    // Scaling the position is needed to make sure that the window position is
    // what the caller expects.
    Scale(mLeft);
    Scale(mTop);

    // Scaling these CSS sizes by the zoom factor might be a bit dubious, as the
    // created window should not be zoomed, but we've done that historically...
    Scale(mOuterWidth);
    Scale(mOuterHeight);
    Scale(mInnerWidth);
    Scale(mInnerHeight);
  }
};

static void SizeOpenedWindow(nsIDocShellTreeOwner* aTreeOwner,
                             mozIDOMWindowProxy* aParent, bool aIsCallerChrome,
                             const SizeSpec&);
static SizeSpec CalcSizeSpec(const WindowFeatures&, bool aHasChromeParent,
                             CSSToDesktopScale);

NS_IMETHODIMP
nsWindowWatcher::OpenWindow2(
    mozIDOMWindowProxy* aParent, nsIURI* aUri, const nsACString& aName,
    const nsACString& aFeatures, const UserActivation::Modifiers& aModifiers,
    bool aCalledFromScript, bool aDialog, bool aNavigate, nsIArray* aArguments,
    bool aIsPopupSpam, bool aForceNoOpener, bool aForceNoReferrer,
    PrintKind aPrintKind, nsDocShellLoadState* aLoadState,
    BrowsingContext** aResult) {
  nsCOMPtr<nsIArray> argv = ConvertArgsToArray(aArguments);

  uint32_t argc = 0;
  if (argv) {
    argv->GetLength(&argc);
  }

  // This is extremely messed up, but this behavior is necessary because
  // callers lie about whether they're a dialog window and whether they're
  // called from script.  Fixing this is bug 779939.
  bool dialog = aDialog;
  if (!aCalledFromScript) {
    dialog = argc > 0;
  }

  return OpenWindowInternal(aParent, aUri, aName, aFeatures, aModifiers,
                            aCalledFromScript, dialog, aNavigate, argv,
                            aIsPopupSpam, aForceNoOpener, aForceNoReferrer,
                            aPrintKind, aLoadState, aResult);
}

// This static function checks if the aDocShell uses an UserContextId equal to
// the userContextId of subjectPrincipal, if not null.
static bool CheckUserContextCompatibility(nsIDocShell* aDocShell) {
  MOZ_ASSERT(aDocShell);

  uint32_t userContextId =
      static_cast<nsDocShell*>(aDocShell)->GetOriginAttributes().mUserContextId;

  nsCOMPtr<nsIPrincipal> subjectPrincipal =
      nsContentUtils::GetCurrentJSContext() ? nsContentUtils::SubjectPrincipal()
                                            : nullptr;

  // If we don't have a valid principal, probably we are in e10s mode, parent
  // side.
  if (!subjectPrincipal) {
    return true;
  }

  // DocShell can have UsercontextID set but loading a document with system
  // principal. In this case, we consider everything ok.
  if (subjectPrincipal->IsSystemPrincipal()) {
    return true;
  }

  return subjectPrincipal->GetUserContextId() == userContextId;
}

nsresult nsWindowWatcher::CreateChromeWindow(nsIWebBrowserChrome* aParentChrome,
                                             uint32_t aChromeFlags,
                                             nsIOpenWindowInfo* aOpenWindowInfo,
                                             nsIWebBrowserChrome** aResult) {
  if (NS_WARN_IF(!mWindowCreator)) {
    return NS_ERROR_UNEXPECTED;
  }

  bool cancel = false;
  if (aChromeFlags & nsIWebBrowserChrome::CHROME_OPENAS_DIALOG) {
    // If there are any drag and drop operations in flight, try to end them.
    nsCOMPtr<nsIDragService> ds =
        do_GetService("@mozilla.org/widget/dragservice;1");
    if (ds) {
      nsCOMPtr<nsIDragSession> session;
      ds->GetCurrentSession(nullptr, getter_AddRefs(session));
      if (session) {
        session->EndDragSession(true, 0);
      }
    }
  }
  nsCOMPtr<nsIWebBrowserChrome> newWindowChrome;
  nsresult rv = mWindowCreator->CreateChromeWindow(
      aParentChrome, aChromeFlags, aOpenWindowInfo, &cancel,
      getter_AddRefs(newWindowChrome));

  if (NS_SUCCEEDED(rv) && cancel) {
    newWindowChrome = nullptr;
    return NS_ERROR_ABORT;
  }

  newWindowChrome.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsWindowWatcher::OpenWindowWithRemoteTab(nsIRemoteTab* aRemoteTab,
                                         const WindowFeatures& aFeatures,
                                         bool aCalledFromJS,
                                         float aOpenerFullZoom,
                                         nsIOpenWindowInfo* aOpenWindowInfo,
                                         nsIRemoteTab** aResult) {
#ifdef MOZ_GECKOVIEW
  MOZ_ASSERT(false, "GeckoView should use nsIBrowserDOMWindow instead");
  return NS_ERROR_NOT_IMPLEMENTED;
#else
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(mWindowCreator);

  if (!nsContentUtils::IsSafeToRunScript()) {
    nsContentUtils::WarnScriptWasIgnored(nullptr);
    return NS_ERROR_FAILURE;
  }

  if (NS_WARN_IF(!mWindowCreator)) {
    return NS_ERROR_UNEXPECTED;
  }

  bool isFissionWindow = FissionAutostart();
  bool isPrivateBrowsingWindow =
      StaticPrefs::browser_privatebrowsing_autostart();

  nsCOMPtr<nsPIDOMWindowOuter> parentWindowOuter;
  RefPtr<BrowsingContext> parentBC = aOpenWindowInfo->GetParent();
  if (parentBC) {
    RefPtr<Element> browserElement = parentBC->Top()->GetEmbedderElement();
    if (browserElement && browserElement->GetRelevantGlobal() &&
        browserElement->GetRelevantGlobal()->GetAsInnerWindow()) {
      parentWindowOuter = browserElement->GetRelevantGlobal()
                              ->GetAsInnerWindow()
                              ->GetOuterWindow();
    }

    isFissionWindow = parentBC->UseRemoteSubframes();
    isPrivateBrowsingWindow =
        isPrivateBrowsingWindow || parentBC->UsePrivateBrowsing();
  }

  if (!parentWindowOuter) {
    // We couldn't find a browser window for the opener, so either we
    // never were passed aRemoteTab, the window is closed,
    // or it's in the process of closing. Either way, we'll use
    // the most recently opened browser window instead.
    parentWindowOuter = nsContentUtils::GetMostRecentNonPBWindow();
  }

  if (NS_WARN_IF(!parentWindowOuter)) {
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsIDocShellTreeOwner> parentTreeOwner =
      parentWindowOuter->GetTreeOwner();
  if (NS_WARN_IF(!parentTreeOwner)) {
    return NS_ERROR_UNEXPECTED;
  }

  if (NS_WARN_IF(!mWindowCreator)) {
    return NS_ERROR_UNEXPECTED;
  }

  // We fall back to calculating in desktop pixels instead of CSS pixels
  // if we can somehow not query a nsIBaseWindow. That does not happen in
  // practice because parentTreeOwner is a nsIDocShellTreeOwner, and every
  // implementation of nsIDocShellTreeOwner also implements nsIBaseWindow.
  CSSToDesktopScale cssToDesktopScale(1.0f);
  if (nsCOMPtr<nsIBaseWindow> win = do_QueryInterface(parentTreeOwner)) {
    cssToDesktopScale = win->GetUnscaledCSSToDesktopScale();
  }
  SizeSpec sizeSpec = CalcSizeSpec(aFeatures, false, cssToDesktopScale);
  sizeSpec.ScaleBy(aOpenerFullZoom);

  // This is not initiated by window.open call in content context, and we
  // don't need to propagate isPopupRequested out-parameter to the resulting
  // browsing context.
  bool unused = false;
  uint32_t chromeFlags =
      CalculateChromeFlagsForContent(aFeatures, aCalledFromJS, &unused);

  if (isPrivateBrowsingWindow) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_PRIVATE_WINDOW;
  }

  // A content process has asked for a new window, which implies
  // that the new window will need to be remote.
  chromeFlags |= nsIWebBrowserChrome::CHROME_REMOTE_WINDOW;

  if (isFissionWindow) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_FISSION_WINDOW;
  }

  nsCOMPtr<nsIWebBrowserChrome> parentChrome(do_GetInterface(parentTreeOwner));
  nsCOMPtr<nsIWebBrowserChrome> newWindowChrome;

  CreateChromeWindow(parentChrome, chromeFlags, aOpenWindowInfo,
                     getter_AddRefs(newWindowChrome));

  if (NS_WARN_IF(!newWindowChrome)) {
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsIDocShellTreeItem> chromeTreeItem =
      do_GetInterface(newWindowChrome);
  if (NS_WARN_IF(!chromeTreeItem)) {
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsIDocShellTreeOwner> chromeTreeOwner;
  chromeTreeItem->GetTreeOwner(getter_AddRefs(chromeTreeOwner));
  if (NS_WARN_IF(!chromeTreeOwner)) {
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsILoadContext> chromeContext = do_QueryInterface(chromeTreeItem);
  if (NS_WARN_IF(!chromeContext)) {
    return NS_ERROR_UNEXPECTED;
  }

  MOZ_ASSERT(chromeContext->UsePrivateBrowsing() == isPrivateBrowsingWindow);
  MOZ_ASSERT(chromeContext->UseRemoteSubframes() == isFissionWindow);

  // Tabs opened from a content process can only open new windows
  // that will also run with out-of-process tabs.
  MOZ_ASSERT(chromeContext->UseRemoteTabs());

  SizeOpenedWindow(chromeTreeOwner, parentWindowOuter, false, sizeSpec);

  nsCOMPtr<nsIRemoteTab> newBrowserParent;
  chromeTreeOwner->GetPrimaryRemoteTab(getter_AddRefs(newBrowserParent));
  if (NS_WARN_IF(!newBrowserParent)) {
    return NS_ERROR_UNEXPECTED;
  }

  if (chromeFlags & nsIWebBrowserChrome::CHROME_DOCUMENT_PIP) {
    RefPtr<BrowserHost> newBrowserHost = BrowserHost::GetFrom(newBrowserParent);
    RefPtr<Element> rootElement =
        newBrowserHost->GetOwnerElement()->OwnerDoc()->GetRootElement();
    if (aFeatures.Exists("disallow_return_to_opener")) {
      rootElement->SetAttribute(u"disallowReturnToOpener"_ns, u""_ns,
                                IgnoreErrors());
    }
  }

  newBrowserParent.forget(aResult);
  return NS_OK;
#endif
}

nsresult nsWindowWatcher::OpenWindowInternal(
    mozIDOMWindowProxy* aParent, const nsACString& aUrl,
    const nsACString& aName, const nsACString& aFeatures,
    const mozilla::dom::UserActivation::Modifiers& aModifiers,
    bool aCalledFromJS, bool aDialog, bool aNavigate, nsIArray* aArgv,
    bool aIsPopupSpam, bool aForceNoOpener, bool aForceNoReferrer,
    PrintKind aPrintKind, nsDocShellLoadState* aLoadState,
    BrowsingContext** aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = nullptr;

  nsCOMPtr<nsIURI> uriToLoad;
  if (!aUrl.IsVoid()) {
    nsresult rv = URIfromURL(aUrl, aParent, getter_AddRefs(uriToLoad));
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  RefPtr<nsDocShellLoadState> loadState = aLoadState;
  if (!loadState && uriToLoad && aNavigate) {
    loadState = CreateLoadState(
        uriToLoad, aParent ? nsPIDOMWindowOuter::From(aParent) : nullptr);
  }

  return nsWindowWatcher::OpenWindowInternal(
      aParent, uriToLoad, aName, aFeatures, aModifiers, aCalledFromJS, aDialog,
      aNavigate, aArgv, aIsPopupSpam, aForceNoOpener, aForceNoReferrer,
      aPrintKind, loadState, aResult);
}

nsresult nsWindowWatcher::OpenWindowInternal(
    mozIDOMWindowProxy* aParent, nsIURI* aUri, const nsACString& aName,
    const nsACString& aFeatures,
    const mozilla::dom::UserActivation::Modifiers& aModifiers,
    bool aCalledFromJS, bool aDialog, bool aNavigate, nsIArray* aArgv,
    bool aIsPopupSpam, bool aForceNoOpener, bool aForceNoReferrer,
    PrintKind aPrintKind, nsDocShellLoadState* aLoadState,
    BrowsingContext** aResult) {
  MOZ_ASSERT_IF(aForceNoReferrer, aForceNoOpener);
  // XXXedgar aNavigate is now used for a sanity check only. We could consider
  // removing it at some point.
  MOZ_DIAGNOSTIC_ASSERT((aUri && aNavigate) == !!aLoadState);

  nsresult rv = NS_OK;
  bool isNewToplevelWindow = false;
  bool windowIsNew = false;
  bool windowNeedsName = false;
  bool windowIsModal = false;
  bool uriToLoadIsChrome = false;

  uint32_t chromeFlags;
  nsAutoString name;  // string version of aName
  nsCOMPtr<nsIDocShellTreeOwner>
      parentTreeOwner;               // from the parent window, if any
  RefPtr<BrowsingContext> targetBC;  // from the new window

  nsCOMPtr<nsPIDOMWindowOuter> parentOuterWin =
      aParent ? nsPIDOMWindowOuter::From(aParent) : nullptr;

  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = nullptr;

  if (!nsContentUtils::IsSafeToRunScript()) {
    nsContentUtils::WarnScriptWasIgnored(nullptr);
    return NS_ERROR_FAILURE;
  }

  if (parentOuterWin) {
    parentTreeOwner = parentOuterWin->GetTreeOwner();
  }

  if (aUri) {
    uriToLoadIsChrome = aUri->SchemeIs("chrome");

    if (aLoadState) {
      bool equal = false;
      aUri->Equals(aLoadState->URI(), &equal);
      MOZ_DIAGNOSTIC_ASSERT(
          equal,
          "aLoadState should contain the same URI passed to this function.");
    }
  }

  bool nameSpecified = false;
  if (!aName.IsEmpty()) {
    CopyUTF8toUTF16(aName, name);
    nameSpecified = true;
  } else {
    name.SetIsVoid(true);
  }

  WindowFeatures features;
  nsAutoCString featuresStr;
  if (!aFeatures.IsEmpty()) {
    featuresStr.Assign(aFeatures);
    features.Tokenize(featuresStr);
  } else {
    featuresStr.SetIsVoid(true);
  }

  RefPtr<BrowsingContext> parentBC(
      parentOuterWin ? parentOuterWin->GetBrowsingContext() : nullptr);
  nsCOMPtr<nsIDocShell> parentDocShell(parentBC ? parentBC->GetDocShell()
                                                : nullptr);
  RefPtr<Document> parentDoc(parentOuterWin ? parentOuterWin->GetDoc()
                                            : nullptr);
  nsCOMPtr<nsPIDOMWindowInner> parentInnerWin(
      parentOuterWin ? parentOuterWin->GetCurrentInnerWindow() : nullptr);

  // Return null for any attempt to trigger a load from a discarded browsing
  // context. The spec is non-normative, and doesn't specify what should happen
  // when window.open is called on a window with a null browsing context, but it
  // does give us broad discretion over when we can decide to ignore an open
  // request and return null.
  //
  // Regardless, we cannot trigger a cross-process load from a discarded
  // browsing context, and ideally we should behave consistently whether a load
  // is same-process or cross-process.
  if (parentBC && parentBC->IsDiscarded()) {
    return NS_ERROR_ABORT;
  }

  // If no parent, consider it chrome when running in the parent process.
  bool hasChromeParent = !XRE_IsContentProcess();
  if (aParent) {
    // Check if the parent document has chrome privileges.
    hasChromeParent = parentDoc && nsContentUtils::IsChromeDoc(parentDoc);
  }

  bool isCallerChrome = nsContentUtils::LegacyIsCallerChromeOrNativeCode();

  // try to find an extant browsing context with the given name
  if (!name.IsEmpty() &&
      (!aForceNoOpener || nsContentUtils::IsSpecialName(name))) {
    if (parentInnerWin && parentInnerWin->GetWindowGlobalChild()) {
      // If we have a parent window, perform the look-up relative to the parent
      // inner window.
      targetBC =
          parentInnerWin->GetWindowGlobalChild()->FindBrowsingContextWithName(
              name);
    } else if (hasChromeParent && isCallerChrome &&
               !nsContentUtils::IsSpecialName(name)) {
      // Otherwise, if this call is from chrome, perform the lookup relative
      // to the system group.
      nsCOMPtr<mozIDOMWindowProxy> chromeWindow;
      MOZ_ALWAYS_SUCCEEDS(GetWindowByName(name, getter_AddRefs(chromeWindow)));
      if (chromeWindow) {
        targetBC = nsPIDOMWindowOuter::From(chromeWindow)->GetBrowsingContext();
      }
    }
  }

  // Do sandbox checks here, instead of waiting until nsIDocShell::LoadURI.
  // The state of the window can change before this call and if we are blocked
  // because of sandboxing, we wouldn't want that to happen.
  if (parentBC && parentBC->IsSandboxedFrom(targetBC)) {
    return NS_ERROR_DOM_INVALID_ACCESS_ERR;
  }

  // If our target BrowsingContext is still pending initialization, ignore the
  // navigation request targeting it.
  if (targetBC && NS_WARN_IF(targetBC->GetPendingInitialization())) {
    return NS_ERROR_ABORT;
  }

  // no extant window? make a new one.

  CSSToDesktopScale cssToDesktopScale(1.0);
  if (nsCOMPtr<nsIBaseWindow> win = do_QueryInterface(parentDocShell)) {
    cssToDesktopScale = win->GetUnscaledCSSToDesktopScale();
  } else {
    RefPtr<widget::Screen> screen =
        widget::ScreenManager::GetSingleton().GetPrimaryScreen();
    cssToDesktopScale = screen->GetCSSToDesktopScale();
  }
  SizeSpec sizeSpec =
      CalcSizeSpec(features, hasChromeParent, cssToDesktopScale);
  sizeSpec.ScaleBy(parentBC ? parentBC->FullZoom() : 1.0f);

  bool isPopupRequested = false;

  // Make sure we calculate the chromeFlags *before* we push the
  // callee context onto the context stack so that
  // the calculation sees the actual caller when doing its
  // security checks.
  if (hasChromeParent && isCallerChrome && XRE_IsParentProcess()) {
    chromeFlags =
        CalculateChromeFlagsForSystem(features, aDialog, uriToLoadIsChrome);
  } else {
    MOZ_DIAGNOSTIC_ASSERT(parentBC && parentBC->IsContent(),
                          "content caller must provide content parent");
    chromeFlags = CalculateChromeFlagsForContent(features, aCalledFromJS,
                                                 &isPopupRequested);

    if (aDialog) {
      MOZ_ASSERT(XRE_IsParentProcess());
      chromeFlags |= nsIWebBrowserChrome::CHROME_OPENAS_DIALOG;
    }
  }

  bool windowTypeIsChrome =
      chromeFlags & nsIWebBrowserChrome::CHROME_OPENAS_CHROME;

  if (parentBC && !aForceNoOpener) {
    if (parentBC->IsChrome() && !windowTypeIsChrome) {
      NS_WARNING(
          "Content windows may never have chrome windows as their openers.");
      return NS_ERROR_INVALID_ARG;
    }
    if (parentBC->IsContent() && windowTypeIsChrome) {
      NS_WARNING(
          "Chrome windows may never have content windows as their openers.");
      return NS_ERROR_INVALID_ARG;
    }
  }

  // If we're opening a content window from a content window, we need to exactly
  // inherit fission and e10s status flags from parentBC. Only new toplevel
  // windows may change these options.
  if (parentBC && parentBC->IsContent() && !windowTypeIsChrome) {
    chromeFlags &= ~(nsIWebBrowserChrome::CHROME_REMOTE_WINDOW |
                     nsIWebBrowserChrome::CHROME_FISSION_WINDOW);
    if (parentBC->UseRemoteTabs()) {
      chromeFlags |= nsIWebBrowserChrome::CHROME_REMOTE_WINDOW;
    }
    if (parentBC->UseRemoteSubframes()) {
      chromeFlags |= nsIWebBrowserChrome::CHROME_FISSION_WINDOW;
    }
  }

  // XXXbz Why is an AutoJSAPI good enough here?  Wouldn't AutoEntryScript (so
  // we affect the entry global) make more sense?  Or do we just want to affect
  // GetSubjectPrincipal()?
  dom::AutoJSAPI jsapiChromeGuard;

  if (isCallerChrome && !hasChromeParent && !windowTypeIsChrome) {
    // open() is called from chrome on a non-chrome window, initialize an
    // AutoJSAPI with the callee to prevent the caller's privileges from leaking
    // into code that runs while opening the new window.
    //
    // The reasoning for this is in bug 289204. Basically, chrome sometimes does
    // someContentWindow.open(untrustedURL), and wants to be insulated from
    // nasty javascript: URLs and such. But there are also cases where we create
    // a window parented to a content window (such as a download dialog),
    // usually directly with nsIWindowWatcher. In those cases, we want the
    // principal of the initial about:blank document to be system, so that the
    // subsequent XUL load can reuse the inner window and avoid blowing away
    // expandos. As such, we decide whether to load with the principal of the
    // caller or of the parent based on whether the docshell type is chrome or
    // content.

    nsCOMPtr<nsIGlobalObject> parentGlobalObject = do_QueryInterface(aParent);
    if (!aParent) {
      jsapiChromeGuard.Init();
    } else if (NS_WARN_IF(!jsapiChromeGuard.Init(parentGlobalObject))) {
      return NS_ERROR_UNEXPECTED;
    }
  }

  // Now that the jsapiChromeGuard has been set, fetch the system principal
  // potentially configured by it. We want to make sure to respect any principal
  // changes imposed by that guard throughout this function.
  //
  // Note: The check for the current JSContext isn't necessarily sensical.
  // It's just designed to preserve old semantics during a mass-conversion
  // patch.
  // Bug 1498605 verify usages of systemPrincipal here
  JSContext* cx = nsContentUtils::GetCurrentJSContext();
  nsCOMPtr<nsIPrincipal> subjectPrincipal =
      cx ? nsContentUtils::SubjectPrincipal()
         : nsContentUtils::GetSystemPrincipal();
  MOZ_ASSERT(subjectPrincipal);

  // Information used when opening new content windows. This object will be
  // passed through to the inner nsFrameLoader.
  RefPtr<nsOpenWindowInfo> openWindowInfo = new nsOpenWindowInfo();

  if (!targetBC) {
    if (windowTypeIsChrome) {
      // If we are creating a chrome window, we must be called with a system
      // principal, and should inherit that for the new chrome window.
      MOZ_RELEASE_ASSERT(subjectPrincipal->IsSystemPrincipal(),
                         "Only system principals can create chrome windows");
      openWindowInfo->mPrincipalToInheritForAboutBlank = subjectPrincipal;
    } else if (nsContentUtils::IsSystemOrExpandedPrincipal(subjectPrincipal)) {
      // Don't allow initial about:blank documents to inherit a system or
      // expanded principal. We can't inherit origin attributes from the
      // system principal, so use the parent BC if it's available.
      // XXX This is wrong for popups from extensions, see bug 2053365.

      const bool isDocumentPiP =
          (chromeFlags & nsIWebBrowserChrome::CHROME_DOCUMENT_PIP);
      MOZ_ASSERT_IF(
          isDocumentPiP,
          parentDoc && parentDoc->NodePrincipal()->GetIsContentPrincipal());

      if (isDocumentPiP &&
          parentDoc->NodePrincipal()->GetIsContentPrincipal()) {
        // Document PiP should use this's relevant global object, which isn't
        // the same as subject principal if the request comes from an extension.
        openWindowInfo->mPrincipalToInheritForAboutBlank =
            parentDoc->NodePrincipal();
      } else if (parentBC) {
        openWindowInfo->mPrincipalToInheritForAboutBlank =
            NullPrincipal::Create(parentBC->OriginAttributesRef());
      } else {
        openWindowInfo->mPrincipalToInheritForAboutBlank =
            NullPrincipal::CreateWithoutOriginAttributes();
      }
    } else if (aForceNoOpener) {
      // If we're opening a new window with noopener, create a new opaque
      // principal for the new window, rather than re-using the existing
      // principal.
      openWindowInfo->mPrincipalToInheritForAboutBlank =
          NullPrincipal::CreateWithInheritedAttributes(subjectPrincipal);
    } else {
      // Finally, if there's an opener relationship and it's not a special
      // principal, we should inherit that principal for the new window.
      openWindowInfo->mPrincipalToInheritForAboutBlank = subjectPrincipal;
    }
  }

  if (!targetBC && !windowTypeIsChrome) {
    openWindowInfo->mForceNoOpener = aForceNoOpener;
    openWindowInfo->mParent = parentBC;
    openWindowInfo->mIsForPrinting = aPrintKind != PRINT_NONE;
    openWindowInfo->mIsForWindowDotPrint = aPrintKind == PRINT_WINDOW_DOT_PRINT;
    openWindowInfo->mIsTopLevelCreatedByWebContent =
        !nsContentUtils::IsSystemOrExpandedPrincipal(subjectPrincipal);

    // We're going to want the window to be immediately available, meaning we
    // want it to match the current remoteness.
    openWindowInfo->mIsRemote = XRE_IsContentProcess();

    // Inherit our OriginAttributes from the computed new window principal.
    MOZ_ASSERT(openWindowInfo->mPrincipalToInheritForAboutBlank &&
               !nsContentUtils::IsSystemOrExpandedPrincipal(
                   openWindowInfo->mPrincipalToInheritForAboutBlank));

    MOZ_DIAGNOSTIC_ASSERT(
        !parentBC || openWindowInfo->GetOriginAttributes().EqualsIgnoringFPD(
                         parentBC->OriginAttributesRef()),
        "subject principal origin attributes doesn't match opener");
  }

  uint32_t activeDocsSandboxFlags = 0;
  if (!targetBC) {
    // We're going to either open up a new window ourselves or ask a
    // nsIWindowProvider for one.  In either case, we'll want to set the right
    // name on it.
    windowNeedsName = true;

    // If the parent trying to open a new window is sandboxed
    // without 'allow-popups', this is not allowed and we fail here.
    if (parentDoc) {
      // Save sandbox flags for copying to new browsing context (docShell).
      activeDocsSandboxFlags = parentDoc->GetSandboxFlags();

      if (!aForceNoOpener) {
        // Inherit from the entry global, e.g. for window.open and fall
        // back to the parent, e.g. for link clicks.
        Document* creator = GetEntryDocument();
        if (!creator) {
          creator = parentDoc;
        }
        openWindowInfo->mPolicyContainerToInheritForAboutBlank =
            creator->GetPolicyContainer();
        openWindowInfo->mCoepToInheritForAboutBlank =
            creator->GetEmbedderPolicy();
        openWindowInfo->mBaseUriToInheritForAboutBlank = creator->GetBaseURI();
      }

      // Check to see if this frame is allowed to navigate, but don't check if
      // we're printing, as that's not a real navigation.
      if (aPrintKind == PRINT_NONE &&
          (activeDocsSandboxFlags & SANDBOXED_AUXILIARY_NAVIGATION)) {
        return NS_ERROR_DOM_INVALID_ACCESS_ERR;
      }
    }

    // Now check whether it's ok to ask a window provider for a window.  Don't
    // do it if we're opening a dialog or if our parent is a chrome window or
    // if we're opening something that has modal, dialog, or chrome flags set.
    if (parentTreeOwner && !aDialog && parentBC->IsContent() &&
        !(chromeFlags & (nsIWebBrowserChrome::CHROME_MODAL |
                         nsIWebBrowserChrome::CHROME_OPENAS_DIALOG |
                         nsIWebBrowserChrome::CHROME_OPENAS_CHROME))) {
      MOZ_ASSERT(openWindowInfo);

      nsCOMPtr<nsIWindowProvider> provider = do_GetInterface(parentTreeOwner);
      if (provider) {
        rv = provider->ProvideWindow(
            openWindowInfo, chromeFlags, aCalledFromJS, aUri, name, featuresStr,
            aModifiers, aForceNoOpener, aForceNoReferrer, isPopupRequested,
            aLoadState, &windowIsNew, getter_AddRefs(targetBC));

        if (NS_SUCCEEDED(rv) && targetBC) {
          nsCOMPtr<nsIDocShell> newDocShell = targetBC->GetDocShell();

          // If this is a new window, but it's incompatible with the current
          // userContextId, we ignore it and we pretend that nothing has been
          // returned by ProvideWindow.
          if (!windowIsNew && newDocShell) {
            if (!CheckUserContextCompatibility(newDocShell)) {
              targetBC = nullptr;
              windowIsNew = false;
            }
          }
        } else if (rv == NS_ERROR_ABORT) {
          // NS_ERROR_ABORT means the window provider has flat-out rejected
          // the open-window call and we should bail.  Don't return an error
          // here, because our caller may propagate that error, which might
          // cause e.g. window.open to throw!  Just return null for our out
          // param.
          return NS_OK;
        }
      }
    }
  }

  bool newWindowShouldBeModal = false;
  bool parentIsModal = false;
  if (!targetBC) {
    if (XRE_IsContentProcess()) {
      // If our window provider failed to provide a window in the content
      // process, we cannot recover. Reject the window open request and bail.
      return NS_OK;
    }

    windowIsNew = true;
    isNewToplevelWindow = true;

    nsCOMPtr<nsIWebBrowserChrome> parentChrome(
        do_GetInterface(parentTreeOwner));

    // is the parent (if any) modal? if so, we must be, too.
    bool weAreModal = (chromeFlags & nsIWebBrowserChrome::CHROME_MODAL) != 0;
    newWindowShouldBeModal = weAreModal;
    if (!weAreModal && parentChrome) {
      parentChrome->IsWindowModal(&weAreModal);
      parentIsModal = weAreModal;
    }

    if (weAreModal) {
      windowIsModal = true;
      // in case we added this because weAreModal
      chromeFlags |= nsIWebBrowserChrome::CHROME_MODAL |
                     nsIWebBrowserChrome::CHROME_DEPENDENT;
    }

    // Make sure to not create modal windows if our parent is invisible and
    // isn't a chrome window.  Otherwise we can end up in a bizarre situation
    // where we can't shut down because an invisible window is open.  If
    // someone tries to do this, throw.
    if (!hasChromeParent && (chromeFlags & nsIWebBrowserChrome::CHROME_MODAL)) {
      nsCOMPtr<nsIWidget> parentWidget;
      if (nsCOMPtr<nsIBaseWindow> parentWindow =
              do_GetInterface(parentTreeOwner)) {
        parentWidget = parentWindow->GetMainWidget();
      }
      // NOTE: the logic for this visibility check is duplicated in
      // nsIDOMWindowUtils::isParentWindowMainWidgetVisible - if we change
      // how a window is determined "visible" in this context then we should
      // also adjust that attribute and/or any consumers of it...
      if (parentWidget && !parentWidget->IsVisible()) {
        return NS_ERROR_NOT_AVAILABLE;
      }
    }

    NS_ASSERTION(mWindowCreator,
                 "attempted to open a new window with no WindowCreator");
    rv = NS_ERROR_FAILURE;
    if (mWindowCreator) {
      nsCOMPtr<nsIWebBrowserChrome> newChrome;

      nsCOMPtr<nsPIDOMWindowInner> parentTopInnerWindow;
      if (parentOuterWin) {
        nsCOMPtr<nsPIDOMWindowOuter> parentTopWindow =
            parentOuterWin->GetInProcessTop();
        if (parentTopWindow) {
          parentTopInnerWindow = parentTopWindow->GetCurrentInnerWindow();
        }
      }

      if (parentTopInnerWindow) {
        parentTopInnerWindow->Suspend();
      }

      /* We can give the window creator some hints. The only hint at this time
         is whether the opening window is in a situation that's likely to mean
         this is an unrequested popup window we're creating. However we're not
         completely honest: we clear that indicator if the opener is chrome, so
         that the downstream consumer can treat the indicator to mean simply
         that the new window is subject to popup control. */
      rv = CreateChromeWindow(
          parentChrome, chromeFlags,
          windowTypeIsChrome ? nullptr : openWindowInfo.get(),
          getter_AddRefs(newChrome));
      if (parentTopInnerWindow) {
        parentTopInnerWindow->Resume();
      }

      if (newChrome) {
        /* It might be a chrome AppWindow, in which case it won't have
            an nsIDOMWindow (primary content shell). But in that case, it'll
            be able to hand over an nsIDocShellTreeItem directly. */
        nsCOMPtr<nsPIDOMWindowOuter> newWindow(do_GetInterface(newChrome));
        nsCOMPtr<nsIDocShellTreeItem> newDocShellItem;
        if (newWindow) {
          newDocShellItem = newWindow->GetDocShell();
        }
        if (!newDocShellItem) {
          newDocShellItem = do_GetInterface(newChrome);
        }
        if (!newDocShellItem) {
          rv = NS_ERROR_FAILURE;
        }
        targetBC = newDocShellItem->GetBrowsingContext();
      }
    }
  }

  // better have a window to use by this point
  if (!targetBC) {
    return rv;
  }

  MOZ_DIAGNOSTIC_ASSERT(
      !windowIsNew || !targetBC->IsContent() ||
          nsContentUtils::IsSystemOrExpandedPrincipal(subjectPrincipal) ||
          targetBC->GetTopLevelCreatedByWebContent(),
      "New BC not marked as created by web content, but it was");

  // If our parent is sandboxed, set it as the one permitted sandboxed navigator
  // on the new window we're opening.
  if (activeDocsSandboxFlags && parentBC) {
    MOZ_ALWAYS_SUCCEEDS(targetBC->SetOnePermittedSandboxedNavigator(parentBC));
  }

  if (!aForceNoOpener && parentBC) {
    // If we've created a new content window, its opener should have been set
    // when its BrowsingContext was created, in order to ensure that the context
    // is loaded within the correct BrowsingContextGroup.
    if (windowIsNew && targetBC->IsContent()) {
      if (parentBC->IsDiscarded()) {
        // If the parent BC was discarded in a nested event loop before we got
        // to this point, we can't set it as the opener. Ideally we would still
        // set `HadOriginalOpener()` in that case, but that's somewhat
        // nontrivial, and not worth the effort given the nature of the corner
        // case (see comment in `nsFrameLoader::CreateBrowsingContext`.
        MOZ_RELEASE_ASSERT(targetBC->GetOpenerId() == parentBC->Id() ||
                           targetBC->GetOpenerId() == 0);
      } else {
        MOZ_RELEASE_ASSERT(targetBC->GetOpenerId() == parentBC->Id());
        MOZ_RELEASE_ASSERT(targetBC->HadOriginalOpener());
      }
    } else {
      // Update the opener for an existing or chrome BC.
      targetBC->SetOpener(parentBC);
    }
  }

  RefPtr<nsDocShell> targetDocShell(nsDocShell::Cast(targetBC->GetDocShell()));

  // As required by spec, new windows always start out same-process, even if the
  // URL being loaded will eventually load in a new process.
  MOZ_DIAGNOSTIC_ASSERT(!windowIsNew || targetDocShell);
  // New top-level windows are only opened in the parent process and are, by
  // definition, always in-process.
  MOZ_DIAGNOSTIC_ASSERT(!isNewToplevelWindow || targetDocShell);

  // Copy sandbox flags to the new window if activeDocsSandboxFlags says to do
  // so.  Note that it's only nonzero if the window is new, so clobbering
  // sandbox flags on the window makes sense in that case.
  if (activeDocsSandboxFlags &
      SANDBOX_PROPAGATES_TO_AUXILIARY_BROWSING_CONTEXTS) {
    MOZ_ASSERT(windowIsNew, "Should only get here for new windows");
    MOZ_ALWAYS_SUCCEEDS(targetBC->SetSandboxFlags(activeDocsSandboxFlags));
    MOZ_ALWAYS_SUCCEEDS(
        targetBC->SetInitialSandboxFlags(targetBC->GetSandboxFlags()));
  }

  RefPtr<nsGlobalWindowOuter> targetOuterWin(
      nsGlobalWindowOuter::Cast(targetBC->GetDOMWindow()));
#ifdef DEBUG
  if (targetOuterWin && windowIsNew) {
    // Assert that we're not loading things right now.  If we are, when
    // that load completes it will clobber whatever principals we set up
    // on this new window!
    nsCOMPtr<nsIChannel> chan;
    targetDocShell->GetDocumentChannel(getter_AddRefs(chan));
    MOZ_ASSERT(!chan, "Why is there a document channel?");

    if (RefPtr<Document> doc = targetOuterWin->GetExtantDoc()) {
      MOZ_ASSERT(doc->IsInitialDocument(),
                 "New window's document should be an initial document");
    }
  }
#endif

  MOZ_ASSERT(targetOuterWin || !windowIsNew,
             "New windows are always created in-process");

  *aResult = do_AddRef(targetBC).take();

  if (isNewToplevelWindow) {
    nsCOMPtr<nsIDocShellTreeOwner> newTreeOwner;
    targetDocShell->GetTreeOwner(getter_AddRefs(newTreeOwner));
    SizeOpenedWindow(newTreeOwner, aParent, isCallerChrome, sizeSpec);
  }

  if (aDialog && aArgv) {
    MOZ_ASSERT(targetOuterWin);
    NS_ENSURE_TRUE(targetOuterWin, NS_ERROR_UNEXPECTED);

    // Set the args on the new window.
    MOZ_TRY(targetOuterWin->SetArguments(aArgv));
  }

  /* allow a window that we found by name to keep its name (important for cases
     like _self where the given name is different (and invalid)).  Also, _blank
     is not a window name. */
  if (windowNeedsName) {
    if (nameSpecified && !name.LowerCaseEqualsLiteral("_blank")) {
      MOZ_ALWAYS_SUCCEEDS(targetBC->SetName(name));
    } else {
      MOZ_ALWAYS_SUCCEEDS(targetBC->SetName(u""_ns));
    }
  }

  // Now we have to set the right opener principal on the new window.  Note
  // that we have to do this _before_ starting any URI loads, thanks to the
  // sync nature of javascript: loads.

  if (windowIsNew) {
    MOZ_DIAGNOSTIC_ASSERT(
        !targetBC->IsContent() ||
        openWindowInfo->mPrincipalToInheritForAboutBlank->OriginAttributesRef()
            .EqualsIgnoringFPD(targetBC->OriginAttributesRef()));

    bool autoPrivateBrowsing = StaticPrefs::browser_privatebrowsing_autostart();

    if (!autoPrivateBrowsing &&
        (chromeFlags & nsIWebBrowserChrome::CHROME_NON_PRIVATE_WINDOW)) {
      if (targetBC->IsChrome()) {
        targetBC->SetUsePrivateBrowsing(false);
      }
      MOZ_DIAGNOSTIC_ASSERT(
          !targetBC->UsePrivateBrowsing(),
          "CHROME_NON_PRIVATE_WINDOW passed, but got private window");
    } else if (autoPrivateBrowsing ||
               (chromeFlags & nsIWebBrowserChrome::CHROME_PRIVATE_WINDOW)) {
      if (targetBC->IsChrome()) {
        targetBC->SetUsePrivateBrowsing(true);
      }
      MOZ_DIAGNOSTIC_ASSERT(
          targetBC->UsePrivateBrowsing(),
          "CHROME_PRIVATE_WINDOW passed, but got non-private window");
    }

    // Now set the opener principal on the new window.  Note that we need to do
    // this no matter whether we were opened from JS; if there is nothing on
    // the JS stack, just use the principal of our parent window.  In those
    // cases we do _not_ set the parent window principal as the owner of the
    // load--since we really don't know who the owner is, just leave it null.
    NS_ASSERTION(targetOuterWin == targetDocShell->GetWindow(),
                 "Different windows??");

    if (targetOuterWin) {
      MOZ_ASSERT(windowIsNew);
      MOZ_ASSERT(!targetOuterWin->GetSameProcessOpener() ||
                 targetOuterWin->GetSameProcessOpener() == aParent);
      Document* doc = targetBC->GetExtantDocument();
      if (doc) {
        // Previously, the principal was set here. Assert that we already have
        // the principal that we would have previously set here.
        MOZ_ASSERT(doc->GetPrincipal()->Equals(
                       openWindowInfo->mPrincipalToInheritForAboutBlank) ||
                       (doc->GetPrincipal()->GetIsNullPrincipal() &&
                        openWindowInfo->mPrincipalToInheritForAboutBlank
                            ->GetIsNullPrincipal()),
                   "Wrong principal!");
        // Setting the principal would've caused a location change event and
        // frontend code depends on that for setting browser.documentURI.
        if (nsIURI* uri = doc->GetDocumentURI()) {
          targetDocShell->FireOnLocationChange(targetDocShell, nullptr, uri, 0);
        }
      } else {
        MOZ_ASSERT_UNREACHABLE("How come there is no doc?");
      }

      if (aIsPopupSpam) {
        MOZ_ASSERT(!targetBC->GetIsPopupSpam(),
                   "Who marked it as popup spam already???");
        // Make sure we don't mess up our counter even if the above assert
        // fails.
        if (!targetBC->GetIsPopupSpam()) {
          MOZ_ALWAYS_SUCCEEDS(targetBC->SetIsPopupSpam(true));
        }
      }
    }

    // Copy the current session storage for the current domain. Don't perform
    // the copy if we're forcing noopener, however.
    if (!aForceNoOpener && subjectPrincipal && parentDocShell &&
        targetDocShell) {
      const RefPtr<SessionStorageManager> parentStorageManager =
          parentDocShell->GetBrowsingContext()->GetSessionStorageManager();
      const RefPtr<SessionStorageManager> newStorageManager =
          targetDocShell->GetBrowsingContext()->GetSessionStorageManager();

      if (parentStorageManager && newStorageManager) {
        nsCOMPtr<nsIPrincipal> storagePrincipal;
        if (parentDoc) {
          storagePrincipal = parentDoc->EffectiveStoragePrincipal();
        } else {
          storagePrincipal = subjectPrincipal;
        }
        RefPtr<Storage> storage;
        parentStorageManager->GetStorage(
            parentInnerWin, subjectPrincipal, storagePrincipal,
            targetBC->UsePrivateBrowsing(), getter_AddRefs(storage));
        if (storage) {
          newStorageManager->CloneStorage(storage);
        }
      }
    }
  }

  // We rely on CalculateChromeFlags to decide whether remote (out-of-process)
  // tabs should be used.
  MOZ_DIAGNOSTIC_ASSERT(
      targetBC->UseRemoteTabs() ==
      !!(chromeFlags & nsIWebBrowserChrome::CHROME_REMOTE_WINDOW));
  MOZ_DIAGNOSTIC_ASSERT(
      targetBC->UseRemoteSubframes() ==
      !!(chromeFlags & nsIWebBrowserChrome::CHROME_FISSION_WINDOW));

  // We need to distinguish the case where the no-URL-argument case behaves like
  // an explicit about:blank argument.
  RefPtr<nsDocShellLoadState> loadState = aLoadState;
  nsCOMPtr<nsIURI> uriToLoad = aUri;
  if (windowIsNew && !uriToLoad && aCalledFromJS && !loadState) {
    NS_NewURI(getter_AddRefs(uriToLoad), "about:blank"_ns);
    // Create loadState lazily for about:blank instead of before consuming
    // user activation in nsGlobalWindowOuter::OpenInternal. See Bug 1901139
    loadState = CreateLoadState(
        uriToLoad, aParent ? nsPIDOMWindowOuter::From(aParent) : nullptr);
  }

  if (loadState) {
    // TriggeringPrincipal and ReferrerInfo are set up here because we
    // rely on the `jsapiChromeGuard` set above to get proper value.
    // Ideally, loadState should contain that value when passed in.
    if (!loadState->TriggeringPrincipal()) {
      loadState->SetTriggeringPrincipal(subjectPrincipal);
#ifndef ANDROID
      MOZ_ASSERT(subjectPrincipal,
                 "nsWindowWatcher: triggeringPrincipal required");
#endif
    }

    if (!loadState->GetReferrerInfo() && !aForceNoReferrer) {
      /* use the URL from the *extant* document, if any. The usual accessor
         GetDocument will synchronously create an about:blank document if
         it has no better answer, and we only care about a real document.
         Also using GetDocument to force document creation seems to
         screw up focus in the hidden window; see bug 36016.
      */
      RefPtr<Document> doc = GetEntryDocument();
      if (!doc) {
        doc = parentDoc;
      }
      if (doc) {
        auto referrerInfo = MakeRefPtr<ReferrerInfo>(*doc);
        loadState->SetReferrerInfo(referrerInfo);
      }
    }

    if (cx) {
      nsGlobalWindowInner* win = xpc::CurrentWindowOrNull(cx);
      if (win) {
        nsCOMPtr<nsIPolicyContainer> policyContainer =
            win->GetPolicyContainer();
        loadState->SetPolicyContainer(policyContainer);
      }
    }
  }

  if (isNewToplevelWindow) {
    // Notify observers that the window is open and ready.
    // The window has not yet started to load a document.
    nsCOMPtr<nsIObserverService> obsSvc =
        mozilla::services::GetObserverService();
    if (obsSvc) {
      obsSvc->NotifyObservers(ToSupports(targetOuterWin),
                              "toplevel-window-ready", nullptr);
    }
  }

  // Before loading the URI we want to be 100% sure that we use the correct
  // userContextId.
  MOZ_ASSERT_IF(targetDocShell, CheckUserContextCompatibility(targetDocShell));

  // If this tab or window has been opened by a window.open call, we have to
  // provide all the data needed to send a
  // webNavigation.onCreatedNavigationTarget event.
  if (parentDocShell && windowIsNew) {
    nsCOMPtr<nsIObserverService> obsSvc =
        mozilla::services::GetObserverService();

    if (obsSvc) {
      RefPtr<nsHashPropertyBag> props = new nsHashPropertyBag();

      if (uriToLoad) {
        // The url notified in the webNavigation.onCreatedNavigationTarget
        // event.
        props->SetPropertyAsACString(u"url"_ns, uriToLoad->GetSpecOrDefault());
      }

      props->SetPropertyAsInterface(u"sourceTabDocShell"_ns, parentDocShell);
      props->SetPropertyAsInterface(u"createdTabDocShell"_ns,
                                    ToSupports(targetDocShell));

      obsSvc->NotifyObservers(static_cast<nsIPropertyBag2*>(props),
                              "webNavigation-createdNavigationTarget-from-js",
                              nullptr);
    }
  }

  if (loadState) {
    uint32_t loadFlags = nsIWebNavigation::LOAD_FLAGS_NONE;
    if (windowIsNew) {
      loadFlags |= nsIWebNavigation::LOAD_FLAGS_FIRST_LOAD;

      // Per spec, the explicit navigation to about:blank after the initial
      // about:blank document in a new window does not occur, so there is no
      // opportunity for it to inherit the source document's principal. This
      // doesn't perfectly model this, as a noopener creation of `about:blank`
      // will replace the global due to a principal mismatch, but it should be
      // unobservable (bug 1694993).
      // This isn't set for chrome windows, as an about:blank chrome document
      // needs to inherit its principal from its opener.
      if (aForceNoOpener && !windowTypeIsChrome) {
        loadFlags |= nsIWebNavigation::LOAD_FLAGS_DISALLOW_INHERIT_PRINCIPAL;
      }
    }
    loadState->SetLoadFlags(loadFlags);
    loadState->SetFirstParty(true);

    // Should this pay attention to errors returned by LoadURI?
    targetBC->LoadURI(loadState);
  }

  if (windowIsModal) {
    NS_ENSURE_TRUE(targetDocShell, NS_ERROR_NOT_IMPLEMENTED);

    nsCOMPtr<nsIDocShellTreeOwner> newTreeOwner;
    targetDocShell->GetTreeOwner(getter_AddRefs(newTreeOwner));
    nsCOMPtr<nsIWebBrowserChrome> newChrome(do_GetInterface(newTreeOwner));

    // Throw an exception here if no web browser chrome is available,
    // we need that to show a modal window.
    NS_ENSURE_TRUE(newChrome, NS_ERROR_NOT_AVAILABLE);

    // Dispatch dialog events etc, but we only want to do that if
    // we're opening a modal content window (the helper classes are
    // no-ops if given no window), for chrome dialogs we don't want to
    // do any of that (it's done elsewhere for us).
    // Make sure we maintain the state on an outer window, because
    // that's where it lives; inner windows assert if you try to
    // maintain the state on them.
    nsAutoWindowStateHelper windowStateHelper(parentOuterWin);

    if (!windowStateHelper.DefaultEnabled()) {
      // Default to cancel not opening the modal window.
      NS_RELEASE(*aResult);

      return NS_OK;
    }

    bool isAppModal = false;
    nsCOMPtr<nsIBaseWindow> parentWindow(do_GetInterface(newTreeOwner));
    nsCOMPtr<nsIWidget> parentWidget;
    if (parentWindow) {
      if ((parentWidget = parentWindow->GetMainWidget())) {
        isAppModal = parentWidget->IsRunningAppModal();
      }
    }
    if (parentWidget &&
        ((!newWindowShouldBeModal && parentIsModal) || isAppModal)) {
      parentWidget->SetModal(true);
    } else {
      // Reset popup state while opening a modal dialog, and firing
      // events about the dialog, to prevent the current state from
      // being active the whole time a modal dialog is open.
      AutoPopupStatePusher popupStatePusher(PopupBlocker::openAbused);

      auto rv = newChrome->ShowAsModal();
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }
  // If a website opens a popup exit DOM fullscreen
  if (StaticPrefs::full_screen_api_exit_on_windowOpen() &&
      (aCalledFromJS ||
       chromeFlags & nsIWebBrowserChrome::CHROME_DOCUMENT_PIP) &&
      !hasChromeParent && !isCallerChrome && parentOuterWin) {
    Document::AsyncExitFullscreen(parentOuterWin->GetDoc());
  }

  if (aForceNoOpener && windowIsNew) {
    NS_RELEASE(*aResult);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsWindowWatcher::RegisterNotification(nsIObserver* aObserver) {
  // just a convenience method; it delegates to nsIObserverService

  if (!aObserver) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (!os) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv = os->AddObserver(aObserver, "domwindowopened", false);
  if (NS_SUCCEEDED(rv)) {
    rv = os->AddObserver(aObserver, "domwindowclosed", false);
  }

  return rv;
}

NS_IMETHODIMP
nsWindowWatcher::UnregisterNotification(nsIObserver* aObserver) {
  // just a convenience method; it delegates to nsIObserverService

  if (!aObserver) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (!os) {
    return NS_ERROR_FAILURE;
  }

  os->RemoveObserver(aObserver, "domwindowopened");
  os->RemoveObserver(aObserver, "domwindowclosed");

  return NS_OK;
}

NS_IMETHODIMP
nsWindowWatcher::GetWindowEnumerator(nsISimpleEnumerator** aResult) {
  if (!aResult) {
    return NS_ERROR_INVALID_ARG;
  }

  MutexAutoLock lock(mListLock);
  RefPtr<nsWatcherWindowEnumerator> enumerator =
      new nsWatcherWindowEnumerator(this);
  enumerator.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsWindowWatcher::GetNewPrompter(mozIDOMWindowProxy* aParent,
                                nsIPrompt** aResult) {
  // This is for backwards compat only. Callers should just use the prompt
  // service directly.
  nsresult rv;
  nsCOMPtr<nsIPromptFactory> factory =
      do_GetService("@mozilla.org/prompter;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  return factory->GetPrompt(aParent, NS_GET_IID(nsIPrompt),
                            reinterpret_cast<void**>(aResult));
}

NS_IMETHODIMP
nsWindowWatcher::GetNewAuthPrompter(mozIDOMWindowProxy* aParent,
                                    nsIAuthPrompt** aResult) {
  // This is for backwards compat only. Callers should just use the prompt
  // service directly.
  nsresult rv;
  nsCOMPtr<nsIPromptFactory> factory =
      do_GetService("@mozilla.org/prompter;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  return factory->GetPrompt(aParent, NS_GET_IID(nsIAuthPrompt),
                            reinterpret_cast<void**>(aResult));
}

NS_IMETHODIMP
nsWindowWatcher::GetPrompt(mozIDOMWindowProxy* aParent, const nsIID& aIID,
                           void** aResult) {
  // This is for backwards compat only. Callers should just use the prompt
  // service directly.
  nsresult rv;
  nsCOMPtr<nsIPromptFactory> factory =
      do_GetService("@mozilla.org/prompter;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = factory->GetPrompt(aParent, aIID, aResult);

  // Allow for an embedding implementation to not support nsIAuthPrompt2.
  if (rv == NS_NOINTERFACE && aIID.Equals(NS_GET_IID(nsIAuthPrompt2))) {
    nsCOMPtr<nsIAuthPrompt> oldPrompt;
    rv = factory->GetPrompt(aParent, NS_GET_IID(nsIAuthPrompt),
                            getter_AddRefs(oldPrompt));
    NS_ENSURE_SUCCESS(rv, rv);

    NS_WrapAuthPrompt(oldPrompt, reinterpret_cast<nsIAuthPrompt2**>(aResult));
    if (!*aResult) {
      rv = NS_ERROR_NOT_AVAILABLE;
    }
  }
  return rv;
}

NS_IMETHODIMP
nsWindowWatcher::SetWindowCreator(nsIWindowCreator* aCreator) {
  mWindowCreator = aCreator;
  return NS_OK;
}

NS_IMETHODIMP
nsWindowWatcher::HasWindowCreator(bool* aResult) {
  *aResult = mWindowCreator;
  return NS_OK;
}

NS_IMETHODIMP
nsWindowWatcher::GetActiveWindow(mozIDOMWindowProxy** aActiveWindow) {
  *aActiveWindow = nullptr;
  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (fm) {
    return fm->GetActiveWindow(aActiveWindow);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsWindowWatcher::AddWindow(mozIDOMWindowProxy* aWindow,
                           nsIWebBrowserChrome* aChrome) {
  if (!aWindow) {
    return NS_ERROR_INVALID_ARG;
  }

  {
    nsWatcherWindowEntry* info;
    MutexAutoLock lock(mListLock);

    // if we already have an entry for this window, adjust
    // its chrome mapping and return
    info = FindWindowEntry(aWindow);
    if (info) {
      info->mChrome = do_GetWeakReference(aChrome);
      return NS_OK;
    }

    // create a window info struct and add it to the list of windows
    info = new nsWatcherWindowEntry(aWindow, aChrome);
    if (!info) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    if (mOldestWindow) {
      info->InsertAfter(mOldestWindow->mOlder);
    } else {
      mOldestWindow = info;
    }
  }  // leave the mListLock

  // a window being added to us signifies a newly opened window.
  // send notifications.
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (!os) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsISupports> domwin(do_QueryInterface(aWindow));
  return os->NotifyObservers(domwin, "domwindowopened", nullptr);
}

NS_IMETHODIMP
nsWindowWatcher::RemoveWindow(mozIDOMWindowProxy* aWindow) {
  // find the corresponding nsWatcherWindowEntry, remove it

  if (!aWindow) {
    return NS_ERROR_INVALID_ARG;
  }

  nsWatcherWindowEntry* info = FindWindowEntry(aWindow);
  if (info) {
    RemoveWindow(info);
    return NS_OK;
  }
  NS_WARNING("requested removal of nonexistent window");
  return NS_ERROR_INVALID_ARG;
}

nsWatcherWindowEntry* nsWindowWatcher::FindWindowEntry(
    mozIDOMWindowProxy* aWindow) {
  // find the corresponding nsWatcherWindowEntry
  nsWatcherWindowEntry* info;
  nsWatcherWindowEntry* listEnd;

  info = mOldestWindow;
  listEnd = nullptr;
  while (info != listEnd) {
    nsCOMPtr<mozIDOMWindowProxy> window = do_QueryReferent(info->mWindow);
    if (window && window == aWindow) {
      return info;
    }
    info = info->mYounger;
    listEnd = mOldestWindow;
  }
  return nullptr;
}

nsresult nsWindowWatcher::RemoveWindow(nsWatcherWindowEntry* aInfo) {
  uint32_t count = mEnumeratorList.Length();

  {
    // notify the enumerators
    MutexAutoLock lock(mListLock);
    for (uint32_t ctr = 0; ctr < count; ++ctr) {
      mEnumeratorList[ctr]->WindowRemoved(aInfo);
    }

    // remove the element from the list
    if (aInfo == mOldestWindow) {
      mOldestWindow =
          aInfo->mYounger == mOldestWindow ? nullptr : aInfo->mYounger;
    }
    aInfo->Unlink();
  }

  // a window being removed from us signifies a newly closed window.
  // send notifications.
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (os) {
    nsCOMPtr<mozIDOMWindowProxy> window = do_QueryReferent(aInfo->mWindow);
    if (window) {
      nsCOMPtr<nsISupports> domwin(do_QueryInterface(window));
      os->NotifyObservers(domwin, "domwindowclosed", nullptr);
    }
  }

  delete aInfo;
  return NS_OK;
}

NS_IMETHODIMP
nsWindowWatcher::GetChromeForWindow(mozIDOMWindowProxy* aWindow,
                                    nsIWebBrowserChrome** aResult) {
  if (!aWindow || !aResult) {
    return NS_ERROR_INVALID_ARG;
  }
  *aResult = nullptr;

  MutexAutoLock lock(mListLock);
  nsWatcherWindowEntry* info = FindWindowEntry(aWindow);
  if (info) {
    nsCOMPtr<nsIWebBrowserChrome> chrome = do_QueryReferent(info->mChrome);
    chrome.forget(aResult);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsWindowWatcher::GetWindowByName(const nsAString& aTargetName,
                                 mozIDOMWindowProxy** aResult) {
  if (!aResult) {
    return NS_ERROR_INVALID_ARG;
  }

  *aResult = nullptr;

  // We won't be able to find any windows with a special or empty name.
  if (aTargetName.IsEmpty() || nsContentUtils::IsSpecialName(aTargetName)) {
    return NS_OK;
  }

  // Search each toplevel in the chrome BrowsingContextGroup for a window with
  // the given name.
  for (const RefPtr<BrowsingContext>& toplevel :
       BrowsingContextGroup::GetChromeGroup()->Toplevels()) {
    BrowsingContext* context =
        toplevel->FindWithNameInSubtree(aTargetName, nullptr);
    if (context) {
      *aResult = do_AddRef(context->GetDOMWindow()).take();
      MOZ_ASSERT(*aResult);
      return NS_OK;
    }
  }

  return NS_OK;
}

bool nsWindowWatcher::AddEnumerator(nsWatcherWindowEnumerator* aEnumerator) {
  // (requires a lock; assumes it's called by someone holding the lock)
  // XXX(Bug 1631371) Check if this should use a fallible operation as it
  // pretended earlier, or change the return type to void.
  mEnumeratorList.AppendElement(aEnumerator);
  return true;
}

bool nsWindowWatcher::RemoveEnumerator(nsWatcherWindowEnumerator* aEnumerator) {
  // (requires a lock; assumes it's called by someone holding the lock)
  return mEnumeratorList.RemoveElement(aEnumerator);
}

nsresult nsWindowWatcher::URIfromURL(const nsACString& aURL,
                                     mozIDOMWindowProxy* aParent,
                                     nsIURI** aURI) {
  // Build the URI relative to the entry global.
  nsCOMPtr<nsPIDOMWindowInner> baseWindow = do_QueryInterface(GetEntryGlobal());

  // failing that, build it relative to the parent window, if possible
  if (!baseWindow && aParent) {
    baseWindow = nsPIDOMWindowOuter::From(aParent)->GetCurrentInnerWindow();
  }

  // failing that, use the given URL unmodified. It had better not be relative.

  nsIURI* baseURI = nullptr;

  // get baseWindow's document URI
  if (baseWindow) {
    if (Document* doc = baseWindow->GetDoc()) {
      baseURI = doc->GetDocBaseURI();
    }
  }

  // Build and return the absolute URI.
  // XXXedgar should we use the characterSet of the document to build the
  // absolute URI?
  return NS_NewURI(aURI, aURL, nullptr, baseURI);
}

// https://html.spec.whatwg.org/#popup-window-is-requested
bool nsWindowWatcher::ShouldOpenPopup(const WindowFeatures& aFeatures) {
  if (aFeatures.IsEmpty()) {
    return false;
  }

  // NOTE: This is different than chrome-only "popup" feature that is handled
  //       in nsWindowWatcher::CalculateChromeFlagsForSystem.
  if (aFeatures.Exists("popup")) {
    return aFeatures.GetBool("popup");
  }

  if (!aFeatures.GetBoolWithDefault("location", false) &&
      !aFeatures.GetBoolWithDefault("toolbar", false)) {
    return true;
  }

  if (!aFeatures.GetBoolWithDefault("menubar", false)) {
    return true;
  }

  if (!aFeatures.GetBoolWithDefault("resizable", true)) {
    return true;
  }

  if (!aFeatures.GetBoolWithDefault("scrollbars", false)) {
    return true;
  }

  if (!aFeatures.GetBoolWithDefault("status", false)) {
    return true;
  }

  return false;
}

/**
 * Calculate the chrome bitmask from a string list of features requested
 * from a child process. The feature string can only control whether to open a
 * new tab or a new popup.
 * @param aFeatures a string containing a list of named features
 * @param aCalledFromJS a bool indicating whether the features were provided by
 content JS. If not, we can expose non-standard, more powerful features to
 content callers.
 * @param aIsPopupRequested an out parameter that indicates whether a popup
 *        is requested by aFeatures
 * @return the chrome bitmask
 */
// static
uint32_t nsWindowWatcher::CalculateChromeFlagsForContent(
    const WindowFeatures& aFeatures, bool aCalledFromJS,
    bool* aIsPopupRequested) {
  if (!aCalledFromJS &&
      aFeatures.GetBoolWithDefault("pictureinpicture", false)) {
    return nsIWebBrowserChrome::CHROME_DOCUMENT_PICTURE_IN_PICTURE_FLAGS;
  }
  *aIsPopupRequested = ShouldOpenPopup(aFeatures);
  return *aIsPopupRequested ? nsIWebBrowserChrome::CHROME_MINIMAL_POPUP
                            : nsIWebBrowserChrome::CHROME_ALL;
}

/**
 * Calculate the chrome bitmask from a string list of features for a new
 * privileged window.
 * @param aFeatures a string containing a list of named chrome features
 * @param aDialog affects the assumptions made about unnamed features
 * @param aChromeURL true if the window is being sent to a chrome:// URL
 * @return the chrome bitmask
 */
// static
uint32_t nsWindowWatcher::CalculateChromeFlagsForSystem(
    const WindowFeatures& aFeatures, bool aDialog, bool aChromeURL) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(nsContentUtils::LegacyIsCallerChromeOrNativeCode());

  uint32_t chromeFlags = 0;

  // The features string is made void by OpenWindowInternal
  // if nullptr was originally passed as the features string.
  if (aFeatures.IsEmpty()) {
    chromeFlags = nsIWebBrowserChrome::CHROME_ALL;
    if (aDialog) {
      chromeFlags |= nsIWebBrowserChrome::CHROME_OPENAS_DIALOG |
                     nsIWebBrowserChrome::CHROME_OPENAS_CHROME;
    }
  }

  /* This function has become complicated since browser windows and
     dialogs diverged. The difference is, browser windows assume all
     chrome not explicitly mentioned is off, if the features string
     is not null. Exceptions are some OS border chrome new with Mozilla.
     Dialogs interpret a (mostly) empty features string to mean
     "OS's choice," and also support an "all" flag explicitly disallowed
     in the standards-compliant window.(normal)open. */

  bool presenceFlag = false;
  if (aDialog && aFeatures.GetBoolWithDefault("all", false, &presenceFlag)) {
    chromeFlags = nsIWebBrowserChrome::CHROME_ALL;
  }

  /* Next, allow explicitly named options to override the initial settings */
  if (aFeatures.GetBoolWithDefault("titlebar", false, &presenceFlag)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_TITLEBAR;
  }
  if (aFeatures.GetBoolWithDefault("toolbar", false, &presenceFlag)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_TOOLBAR;
  }
  if (aFeatures.GetBoolWithDefault("personalbar", false, &presenceFlag)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_PERSONAL_TOOLBAR;
  }
  if (aFeatures.GetBoolWithDefault("menubar", false, &presenceFlag)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_MENUBAR;
  }
  if (aFeatures.GetBoolWithDefault("resizable", false, &presenceFlag)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_WINDOW_RESIZE;
  }
  if (aFeatures.GetBoolWithDefault("minimizable", false, &presenceFlag)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_WINDOW_MINIMIZE;
  }
  if (aFeatures.GetBoolWithDefault("scrollbars", true, &presenceFlag)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_SCROLLBARS;
  }

  // Determine whether the window is a private browsing window
  if (aFeatures.GetBoolWithDefault("private", false, &presenceFlag)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_PRIVATE_WINDOW;
  }
  if (aFeatures.GetBoolWithDefault("non-private", false, &presenceFlag)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_NON_PRIVATE_WINDOW;
  }

  // Determine whether the window should have remote tabs.
  bool remote = BrowserTabsRemoteAutostart();

  if (remote) {
    remote = !aFeatures.GetBoolWithDefault("non-remote", false, &presenceFlag);
  } else {
    remote = aFeatures.GetBoolWithDefault("remote", false, &presenceFlag);
  }

  if (remote) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_REMOTE_WINDOW;
  }

  // Determine whether the window should have remote subframes
  bool fission = FissionAutostart();

  if (fission) {
    fission =
        !aFeatures.GetBoolWithDefault("non-fission", false, &presenceFlag);
  } else {
    fission = aFeatures.GetBoolWithDefault("fission", false, &presenceFlag);
  }

  if (fission) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_FISSION_WINDOW;
  }

  /* OK.
     Normal browser windows, in spite of a stated pattern of turning off
     all chrome not mentioned explicitly, will want the new OS chrome (window
     borders, titlebars, closebox) on, unless explicitly turned off.
     Dialogs, on the other hand, take the absence of any explicit settings
     to mean "OS' choice." */

  // default titlebar and closebox to "on," if not mentioned at all
  if (!aFeatures.Exists("titlebar")) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_TITLEBAR;
  }
  if (aDialog && !aFeatures.IsEmpty() && !presenceFlag) {
    chromeFlags = nsIWebBrowserChrome::CHROME_DEFAULT;
  }

  /* Finally, once all the above normal chrome has been divined, deal
     with the features that are more operating hints than appearance
     instructions. (Note modality implies dependence.) */
  if (aFeatures.GetBoolWithDefault("suppressanimation", false)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_SUPPRESS_ANIMATION;
  }
  if (aFeatures.GetBoolWithDefault("suppressinitialfullscreen", false)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_SUPPRESS_INITIAL_FULLSCREEN;
  }
  if (aFeatures.GetBoolWithDefault("alwaysontop", false)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_ALWAYS_ON_TOP;
  }
  if (aFeatures.GetBoolWithDefault("chrome", false)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_OPENAS_CHROME;
  }
  if (aFeatures.GetBoolWithDefault("extrachrome", false)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_EXTRA;
  }
  if (aFeatures.GetBoolWithDefault("centerscreen", false)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_CENTER_SCREEN;
  }
  if (aFeatures.GetBoolWithDefault("dependent", false)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_DEPENDENT;
  }
  if (aFeatures.GetBoolWithDefault("modal", false)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_MODAL |
                   nsIWebBrowserChrome::CHROME_DEPENDENT;
  }
  if (aFeatures.GetBoolWithDefault("dialog", false)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_OPENAS_DIALOG;
  }
  if (aFeatures.GetBoolWithDefault("alert", false)) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_ALERT;
  }

  /* dialogs need to have the last word. assume dialogs are dialogs,
     and opened as chrome, unless explicitly told otherwise. */
  if (aDialog) {
    if (!aFeatures.Exists("dialog")) {
      chromeFlags |= nsIWebBrowserChrome::CHROME_OPENAS_DIALOG;
    }
    if (!aFeatures.Exists("chrome")) {
      chromeFlags |= nsIWebBrowserChrome::CHROME_OPENAS_CHROME;
    }
  }

  return chromeFlags;
}

// public static
bool nsWindowWatcher::HaveSpecifiedSize(const WindowFeatures& features) {
  return CalcSizeSpec(features, false, CSSToDesktopScale()).SizeSpecified();
}

/* static */
already_AddRefed<nsDocShellLoadState> nsWindowWatcher::CreateLoadState(
    nsIURI* aUri, nsPIDOMWindowOuter* aParent, bool aIsWindowOpen) {
  MOZ_ASSERT(aUri);

  RefPtr<nsDocShellLoadState> loadState = new nsDocShellLoadState(aUri);
  loadState->SetAllowFocusMove(true);

  if (aParent) {
    if (nsCOMPtr<nsPIDOMWindowInner> parentInnerWin =
            aParent->GetCurrentInnerWindow()) {
      loadState->SetTriggeringWindowId(parentInnerWin->WindowID());
      loadState->SetTriggeringStorageAccess(
          parentInnerWin->UsingStorageAccess());
    }

    if (RefPtr<BrowsingContext> parentBC = aParent->GetBrowsingContext()) {
      loadState->SetSourceBrowsingContext(parentBC);
      loadState->SetTriggeringSandboxFlags(parentBC->GetSandboxFlags());
    }

    if (RefPtr<Document> parentDoc = aParent->GetDoc()) {
      loadState->SetHasValidUserGestureActivation(
          parentDoc->HasValidTransientUserGestureActivation());
      loadState->SetTextDirectiveUserActivation(
          parentDoc->ConsumeTextDirectiveUserActivation() ||
          loadState->HasValidUserGestureActivation());
      loadState->SetTriggeringClassificationFlags(
          parentDoc->GetScriptTrackingFlags());
    }
  }

  // If we're called from JS, i.e window.open, we need to set history handling
  // behavior here to be able to do push to replace conversion if needed.
  if (aIsWindowOpen) {
    loadState->SetHistoryBehavior(NavigationHistoryBehavior::Auto);
  }

  return loadState.forget();
}

// static
SizeSpec CalcSizeSpec(const WindowFeatures& aFeatures, bool aHasChromeParent,
                      CSSToDesktopScale aCSSToDesktopScale) {
  SizeSpec result;
  // https://drafts.csswg.org/cssom-view/#set-up-browsing-context-features
  // To set up browsing context features for a browsing context `target` given
  // a map `tokenizedFeatures`:

  // Step 1. Let `x` be null.
  // (implicit)

  // Step 2. Let `y` be null.
  // (implicit)

  // Step 3. Let `width` be null.
  // (implicit)

  // Step 4. Let `height` be null.
  // (implicit)

  // Step 5. If `tokenizedFeatures["left"]` exists:
  if (aFeatures.Exists("left")) {
    // Step 5.1. Set `x` to the result of invoking the rules for parsing
    // integers on `tokenizedFeatures["left"]`.
    //
    // Step 5.2. If `x` is an error, set `x` to 0.
    int32_t x = aFeatures.GetInt("left");

    // Step 5.3. Optionally, clamp `x` in a user-agent-defined manner so that
    // the window does not move outside the Web-exposed available screen area.
    // (done later)

    // Step 5.4. Optionally, move `target`’s window such that the window’s
    // left edge is at the horizontal coordinate `x` relative to the left edge
    // of the Web-exposed screen area, measured in CSS pixels of target.
    // The positive axis is rightward.
    result.mLeft.emplace((CSSCoord(x) * aCSSToDesktopScale).Rounded());
  }

  // Step 6. If `tokenizedFeatures["top"]` exists:
  if (aFeatures.Exists("top")) {
    // Step 6.1. Set `y` to the result of invoking the rules for parsing
    // integers on `tokenizedFeatures["top"]`.
    //
    // Step 6.2. If `y` is an error, set `y` to 0.
    int32_t y = aFeatures.GetInt("top");

    // Step 6.3. Optionally, clamp `y` in a user-agent-defined manner so that
    // the window does not move outside the Web-exposed available screen area.
    // (done later)

    // Step 6.4. Optionally, move `target`’s window such that the window’s top
    // edge is at the vertical coordinate `y` relative to the top edge of the
    // Web-exposed screen area, measured in CSS pixels of target. The positive
    // axis is downward.
    result.mTop.emplace((CSSCoord(y) * aCSSToDesktopScale).Rounded());
  }

  // Non-standard extension.
  // Not exposed to web content.
  if (aHasChromeParent && aFeatures.Exists("outerwidth")) {
    int32_t width = aFeatures.GetInt("outerwidth");
    if (width) {
      result.mOuterWidth.emplace(width);
    }
  }

  if (result.mOuterWidth.isNothing()) {
    // Step 7. If `tokenizedFeatures["width"]` exists:
    if (aFeatures.Exists("width")) {
      // Step 7.1. Set `width` to the result of invoking the rules for parsing
      // integers on `tokenizedFeatures["width"]`.
      //
      // Step 7.2. If `width` is an error, set `width` to 0.
      int32_t width = aFeatures.GetInt("width");

      // Step 7.3. If `width` is not 0:
      if (width) {
        // Step 7.3.1. Optionally, clamp `width` in a user-agent-defined manner
        // so that the window does not get too small or bigger than the
        // Web-exposed available screen area.
        // (done later)

        // Step 7.3.2. Optionally, size `target`’s window by moving its right
        // edge such that the distance between the left and right edges of the
        // viewport are `width` CSS pixels of target.
        result.mInnerWidth.emplace(width);

        // Step 7.3.3. Optionally, move target’s window in a user-agent-defined
        // manner so that it does not grow outside the Web-exposed available
        // screen area.
        // (done later)
      }
    }
  }

  // Non-standard extension.
  // Not exposed to web content.
  if (aHasChromeParent && aFeatures.Exists("outerheight")) {
    int32_t height = aFeatures.GetInt("outerheight");
    if (height) {
      result.mOuterHeight.emplace(height);
    }
  }

  if (result.mOuterHeight.isNothing()) {
    // Step 8. If `tokenizedFeatures["height"]` exists:
    if (aFeatures.Exists("height")) {
      // Step 8.1. Set `height` to the result of invoking the rules for parsing
      // integers on `tokenizedFeatures["height"]`.
      //
      // Step 8.2. If `height` is an error, set `height` to 0.
      int32_t height = aFeatures.GetInt("height");

      // Step 8.3. If `height` is not 0:
      if (height) {
        // Step 8.3.1. Optionally, clamp `height` in a user-agent-defined manner
        // so that the window does not get too small or bigger than the
        // Web-exposed available screen area.
        // (done later)

        // Step 8.3.2. Optionally, size `target`’s window by moving its bottom
        // edge such that the distance between the top and bottom edges of the
        // viewport are `height` CSS pixels of target.
        result.mInnerHeight.emplace(height);

        // Step 8.3.3. Optionally, move target’s window in a user-agent-defined
        // manner so that it does not grow outside the Web-exposed available
        // screen area.
        // (done later)
      }
    }
  }

  // NOTE: The value is handled only on chrome-priv code.
  // See SizeOpenedWindow.
  result.mLockAspectRatio =
      aFeatures.GetBoolWithDefault("lockaspectratio", false);
  return result;
}

/* Size and position a new window according to aSizeSpec. This method
   is assumed to be called after the window has already been given
   a default position and size; thus its current position and size are
   accurate defaults. The new window is made visible at method end.
   @param aTreeOwner
          The top-level nsIDocShellTreeOwner of the newly opened window.
   @param aParent
          The parent window, used to do security checks.
   @param aIsCallerChrome
          True if the code requesting the new window is privileged.
   @param aSizeSpec
          The size that the new window should be.
*/
static void SizeOpenedWindow(nsIDocShellTreeOwner* aTreeOwner,
                             mozIDOMWindowProxy* aParent, bool aIsCallerChrome,
                             const SizeSpec& aSizeSpec) {
  // We should only be sizing top-level windows if we're in the parent
  // process.
  MOZ_ASSERT(XRE_IsParentProcess());

  // get various interfaces for aDocShellItem, used throughout this method
  nsCOMPtr<nsIBaseWindow> treeOwnerAsWin(do_QueryInterface(aTreeOwner));
  if (!treeOwnerAsWin) {  // we'll need this to actually size the docshell
    return;
  }

  // The current position and size will be unchanged if not specified
  // (and they fit entirely onscreen). Also, calculate the difference
  // between chrome and content sizes on aDocShellItem's window.
  // This latter point becomes important if chrome and content
  // specifications are mixed in aFeatures, and when bringing the window
  // back from too far off the right or bottom edges of the screen.
  DesktopIntCoord left = 0;
  DesktopIntCoord top = 0;
  CSSIntCoord width = 0;
  CSSIntCoord height = 0;
  // difference between chrome and content size
  CSSIntCoord chromeWidth = 0;
  CSSIntCoord chromeHeight = 0;
  // whether the window size spec refers to chrome or content
  bool sizeChromeWidth = true;
  bool sizeChromeHeight = true;

  {
    CSSToLayoutDeviceScale cssToDevScale =
        treeOwnerAsWin->UnscaledDevicePixelsPerCSSPixel();
    DesktopToLayoutDeviceScale devToDesktopScale =
        treeOwnerAsWin->DevicePixelsPerDesktopPixel();

    LayoutDeviceIntRect devPxRect = treeOwnerAsWin->GetPositionAndSize();
    width = (LayoutDeviceCoord(devPxRect.width) / cssToDevScale).Rounded();
    height = (LayoutDeviceCoord(devPxRect.height) / cssToDevScale).Rounded();
    left = (LayoutDeviceCoord(devPxRect.x) / devToDesktopScale).Rounded();
    top = (LayoutDeviceCoord(devPxRect.y) / devToDesktopScale).Rounded();

    LayoutDeviceIntSize contentSize;
    bool hasPrimaryContent = false;
    aTreeOwner->GetHasPrimaryContent(&hasPrimaryContent);
    if (hasPrimaryContent) {
      aTreeOwner->GetPrimaryContentSize(&contentSize.width,
                                        &contentSize.height);
    } else {
      aTreeOwner->GetRootShellSize(&contentSize.width, &contentSize.height);
    }

    CSSIntSize contentSizeCSS = RoundedToInt(contentSize / cssToDevScale);
    chromeWidth = width - contentSizeCSS.width;
    chromeHeight = height - contentSizeCSS.height;
  }

  // Set up left/top
  if (aSizeSpec.mLeft) {
    left = *aSizeSpec.mLeft;
  }

  if (aSizeSpec.mTop) {
    top = *aSizeSpec.mTop;
  }

  // Set up width
  if (aSizeSpec.mOuterWidth) {
    width = *aSizeSpec.mOuterWidth;
  } else if (aSizeSpec.mInnerWidth) {
    sizeChromeWidth = false;
    width = *aSizeSpec.mInnerWidth;
  }

  // Set up height
  if (aSizeSpec.mOuterHeight) {
    height = *aSizeSpec.mOuterHeight;
  } else if (aSizeSpec.mInnerHeight) {
    sizeChromeHeight = false;
    height = *aSizeSpec.mInnerHeight;
  }

  bool positionSpecified = aSizeSpec.PositionSpecified();

  // Check security state for use in determining window dimensions
  bool enabled = false;
  if (aIsCallerChrome) {
    // Only enable special privileges for chrome when chrome calls
    // open() on a chrome window
    enabled = !aParent || nsGlobalWindowOuter::Cast(aParent)->IsChromeWindow();
  }

  const CSSIntCoord extraWidth = sizeChromeWidth ? CSSIntCoord(0) : chromeWidth;
  const CSSIntCoord extraHeight =
      sizeChromeHeight ? CSSIntCoord(0) : chromeHeight;

  if (!enabled) {
    // Security check failed.  Ensure all args meet minimum reqs.

    int32_t oldTop = top, oldLeft = left;

    // We'll also need the screen dimensions
    nsCOMPtr<nsIScreen> screen;
    nsCOMPtr<nsIScreenManager> screenMgr(
        do_GetService("@mozilla.org/gfx/screenmanager;1"));
    if (screenMgr) {
      // XXX width, height are in CSS rather than desktop pixels here.
      screenMgr->ScreenForRect(left, top, width, height,
                               getter_AddRefs(screen));
    }
    if (screen) {
      CSSIntCoord winWidth = width + extraWidth;
      CSSIntCoord winHeight = height + extraHeight;

      auto screenCssToDesktopScale = screen->GetCSSToDesktopScale();

      const DesktopIntRect screenDesktopRect = screen->GetAvailRectDisplayPix();
      // Get screen dimensions (in CSS pixels)
      const CSSSize screenCssSize =
          screenDesktopRect.Size() / screenCssToDesktopScale;

      if (aSizeSpec.SizeSpecified()) {
        if (!nsContentUtils::ShouldResistFingerprinting(
                "When RFP is enabled, we unconditionally round new window "
                "sizes. The code paths that create new windows are "
                "complicated, and this is a conservative behavior to avoid "
                "exempting something that shouldn't be. It also presents a "
                "uniform behavior for something that's very browser-related.",
                RFPTarget::RoundWindowSize)) {
          /* Unlike position, force size out-of-bounds check only if
             size actually was specified. Otherwise, intrinsically sized
             windows are broken. */
          if (height < 100) {
            height = 100;
            winHeight = height + extraHeight;
          }
          if (winHeight > screenCssSize.height) {
            height = static_cast<int32_t>(screenCssSize.height - extraHeight);
          }
          if (width < 100) {
            width = 100;
            winWidth = width + extraWidth;
          }
          if (winWidth > screenCssSize.width) {
            width = static_cast<int32_t>(screenCssSize.width - extraWidth);
          }
        } else {
          int32_t targetContentWidth = 0;
          int32_t targetContentHeight = 0;

          nsContentUtils::CalcRoundedWindowSizeForResistingFingerprinting(
              chromeWidth, chromeHeight, screenCssSize.width,
              screenCssSize.height, width, height, sizeChromeWidth,
              sizeChromeHeight, &targetContentWidth, &targetContentHeight);

          if (aSizeSpec.mInnerWidth || aSizeSpec.mOuterWidth) {
            width = targetContentWidth;
            winWidth = width + extraWidth;
          }

          if (aSizeSpec.mInnerHeight || aSizeSpec.mOuterHeight) {
            height = targetContentHeight;
            winHeight = height + extraHeight;
          }
        }
      }

      const DesktopIntCoord desktopWinWidth =
          (CSSCoord(winWidth) * screenCssToDesktopScale).Rounded();
      const DesktopIntCoord desktopWinHeight =
          (CSSCoord(winHeight) * screenCssToDesktopScale).Rounded();
      CheckedInt<int32_t> leftPlusWinWidth = int32_t(left);
      leftPlusWinWidth += int32_t(desktopWinWidth);
      if (!leftPlusWinWidth.isValid() ||
          leftPlusWinWidth.value() > screenDesktopRect.XMost()) {
        left = screenDesktopRect.XMost() - desktopWinWidth;
      }
      if (left < screenDesktopRect.x) {
        left = screenDesktopRect.x;
      }

      CheckedInt<int32_t> topPlusWinHeight = int32_t(top);
      topPlusWinHeight += int32_t(desktopWinHeight);
      if (!topPlusWinHeight.isValid() ||
          topPlusWinHeight.value() > screenDesktopRect.YMost()) {
        top = screenDesktopRect.YMost() - desktopWinHeight;
      }
      if (top < screenDesktopRect.y) {
        top = screenDesktopRect.y;
      }

      if (top != oldTop || left != oldLeft) {
        positionSpecified = true;
      }
    }
  }

  // size and position the window

  if (positionSpecified) {
    treeOwnerAsWin->SetPositionDesktopPix(left, top);
  }

  if (aSizeSpec.SizeSpecified()) {
    const CSSToLayoutDeviceScale scale =
        treeOwnerAsWin->UnscaledDevicePixelsPerCSSPixel();

    /* Prefer to trust the interfaces, which think in terms of pure
       chrome or content sizes. If we have a mix, use the chrome size
       adjusted by the chrome/content differences calculated earlier. */
    if (!sizeChromeWidth && !sizeChromeHeight) {
      const LayoutDeviceIntCoord widthDevPx =
          (CSSCoord(width) * scale).Rounded();
      const LayoutDeviceIntCoord heightDevPx =
          (CSSCoord(height) * scale).Rounded();
      bool hasPrimaryContent = false;
      aTreeOwner->GetHasPrimaryContent(&hasPrimaryContent);
      if (hasPrimaryContent) {
        aTreeOwner->SetPrimaryContentSize(widthDevPx, heightDevPx);
      } else {
        aTreeOwner->SetRootShellSize(widthDevPx, heightDevPx);
      }
    } else {
      const LayoutDeviceIntCoord widthDevPx =
          (CSSCoord(width + extraWidth) * scale).Rounded();
      const LayoutDeviceIntCoord heightDevPx =
          (CSSCoord(height + extraHeight) * scale).Rounded();
      treeOwnerAsWin->SetSize(widthDevPx, heightDevPx, false);
    }
  }

  if (aIsCallerChrome) {
    nsCOMPtr<nsIAppWindow> appWin = do_GetInterface(treeOwnerAsWin);
    if (appWin && aSizeSpec.mLockAspectRatio) {
      appWin->LockAspectRatio(true);
    }
  }

  treeOwnerAsWin->SetVisibility(true);
}

/* static */
bool nsWindowWatcher::IsWindowOpenLocationModified(
    const mozilla::dom::UserActivation::Modifiers& aModifiers,
    int32_t* aLocation) {
  // Perform the subset of BrowserUtils.whereToOpenLink in
  // toolkit/modules/BrowserUtils.sys.mjs for modifier key handling, and
  // URILoadingHelper.openLinkIn in browser/modules/URILoadingHelper.sys.mjs
  // for loadInBackground pref handling.
#ifdef XP_MACOSX
  bool metaKey = aModifiers.IsMeta();
#else
  bool metaKey = aModifiers.IsControl();
#endif
  bool shiftKey = aModifiers.IsShift();

  bool middleMouse = aModifiers.IsMiddleMouse();
  bool middleUsesTabs = StaticPrefs::browser_tabs_opentabfor_middleclick();

  if (metaKey || (middleMouse && middleUsesTabs)) {
    bool loadInBackground = StaticPrefs::browser_tabs_loadInBackground();
    if (shiftKey) {
      loadInBackground = !loadInBackground;
    }
    if (loadInBackground) {
      *aLocation = nsIBrowserDOMWindow::OPEN_NEWTAB_BACKGROUND;
    } else {
      *aLocation = nsIBrowserDOMWindow::OPEN_NEWTAB_FOREGROUND;
    }
    return true;
  }

#ifndef MOZ_GECKOVIEW
  // GeckoView doesn't support new window.
  bool middleUsesNewWindow = StaticPrefs::middlemouse_openNewWindow();
  if (shiftKey || (middleMouse && !middleUsesTabs && middleUsesNewWindow)) {
    *aLocation = nsIBrowserDOMWindow::OPEN_NEWWINDOW;
    return true;
  }
#endif

  // If both middleUsesTabs and middleUsesNewWindow are false, it means the
  // middle-click is used for different purpose, such as paste or scroll.
  // Webpage still can trigger `window.open` for the user activation, and in
  // that case use the `window.open`'s `features` parameter and other prefs to
  // decide where to open.

  return false;
}

/* static */
int32_t nsWindowWatcher::GetWindowOpenLocation(
    nsPIDOMWindowOuter* aParent, uint32_t aChromeFlags,
    const mozilla::dom::UserActivation::Modifiers& aModifiers,
    bool aCalledFromJS, bool aIsForPrinting) {
  // These windows are not actually visible to the user, so we return the thing
  // that we can always handle.
  if (aIsForPrinting) {
    return nsIBrowserDOMWindow::OPEN_PRINT_BROWSER;
  }

  int32_t modifiedLocation = 0;
  if (IsWindowOpenLocationModified(aModifiers, &modifiedLocation)) {
    return modifiedLocation;
  }

  // Where should we open this?
  int32_t containerPref;
  if (NS_FAILED(
          Preferences::GetInt("browser.link.open_newwindow", &containerPref))) {
    // We couldn't read the user preference, so fall back on the default.
    return nsIBrowserDOMWindow::OPEN_NEWTAB;
  }

  bool isDisabledOpenNewWindow =
      aParent->GetFullScreen() &&
      Preferences::GetBool(
          "browser.link.open_newwindow.disabled_in_fullscreen");

  if (isDisabledOpenNewWindow &&
      (containerPref == nsIBrowserDOMWindow::OPEN_NEWWINDOW)) {
    containerPref = nsIBrowserDOMWindow::OPEN_NEWTAB;
  }

  if (containerPref != nsIBrowserDOMWindow::OPEN_NEWTAB &&
      containerPref != nsIBrowserDOMWindow::OPEN_CURRENTWINDOW) {
#ifdef MOZ_GECKOVIEW
    // GeckoView doesn't support new window. Just open a new tab.
    return nsIBrowserDOMWindow::OPEN_NEWTAB;
#else
    // Just open a window normally
    return nsIBrowserDOMWindow::OPEN_NEWWINDOW;
#endif
  }

#ifndef MOZ_GECKOVIEW
  // GeckoView doesn't support new window, so don't check the preference for
  // restriction.
  if (aCalledFromJS) {
    /* Now check our restriction pref.  The restriction pref is a power-user's
       fine-tuning pref. values:
       0: no restrictions - divert everything
       1: don't divert window.open at all
       2: don't divert window.open with features
    */
    int32_t restrictionPref =
        Preferences::GetInt("browser.link.open_newwindow.restriction", 2);
    if (restrictionPref < 0 || restrictionPref > 2) {
      restrictionPref = 2;  // Sane default behavior
    }

    if (isDisabledOpenNewWindow) {
      // In browser fullscreen, the window should be opened
      // in the current window with no features (see bug 803675)
      restrictionPref = 0;
    }

    if (restrictionPref == 1) {
      return nsIBrowserDOMWindow::OPEN_NEWWINDOW;
    }

    if (restrictionPref == 2) {
      // Only continue if there is no special chrome flags - with the exception
      // of the remoteness and private flags, which might have been
      // automatically flipped by Gecko.
      int32_t uiChromeFlags = aChromeFlags;
      uiChromeFlags &= ~(nsIWebBrowserChrome::CHROME_REMOTE_WINDOW |
                         nsIWebBrowserChrome::CHROME_FISSION_WINDOW |
                         nsIWebBrowserChrome::CHROME_PRIVATE_WINDOW |
                         nsIWebBrowserChrome::CHROME_NON_PRIVATE_WINDOW);
      if (uiChromeFlags != nsIWebBrowserChrome::CHROME_ALL) {
        return nsIBrowserDOMWindow::OPEN_NEWWINDOW;
      }
    }
  }

  if (aChromeFlags & nsIWebBrowserChrome::CHROME_DOCUMENT_PIP) {
    return nsIBrowserDOMWindow::OPEN_NEWWINDOW;
  }
#endif

  return containerPref;
}
