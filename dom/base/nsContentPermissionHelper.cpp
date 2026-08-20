/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsContentPermissionHelper.h"

#include <map>

#include "js/PropertyAndElement.h"  // JS_GetProperty, JS_SetProperty
#include "mozilla/Attributes.h"
#include "mozilla/Preferences.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/PContentPermission.h"
#include "mozilla/dom/PContentPermissionRequestParent.h"
#include "mozilla/dom/PermissionMessageUtils.h"
#include "mozilla/dom/ScriptSettings.h"
#include "nsArrayUtils.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsGlobalWindowInner.h"
#include "nsIMutableArray.h"
#include "nsIPrincipal.h"
#include "nsISupportsPrimitives.h"
#include "nsIWeakReferenceUtils.h"
#include "nsJSUtils.h"
#include "nsPIDOMWindowInlines.h"
#include "nsServiceManagerUtils.h"

using namespace mozilla::dom;
using namespace mozilla;
using DelegateInfo = PermissionDelegateHandler::PermissionDelegateInfo;

namespace mozilla::dom {

class ContentPermissionRequestParent : public PContentPermissionRequestParent {
 public:
  // @param aIsRequestDelegatedToUnsafeThirdParty see
  // mIsRequestDelegatedToUnsafeThirdParty.
  ContentPermissionRequestParent(
      Element* aElement, nsIPrincipal* aPrincipal,
      nsIPrincipal* aTopLevelPrincipal,
      const bool aHasValidTransientUserGestureActivation,
      const bool aIsRequestDelegatedToUnsafeThirdParty,
      const bool aIgnoreAllowSitePermission = false);
  virtual ~ContentPermissionRequestParent();

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void Init(nsTArray<PermissionRequest>&& aRequests);

  bool IsBeingDestroyed();

  nsCOMPtr<nsIPrincipal> mPrincipal;
  nsCOMPtr<nsIPrincipal> mTopLevelPrincipal;
  nsCOMPtr<Element> mElement;
  bool mHasValidTransientUserGestureActivation;

  // See nsIPermissionDelegateHandler.maybeUnsafePermissionDelegate.
  bool mIsRequestDelegatedToUnsafeThirdParty;

  bool mIgnoreAllowSitePermission;

  RefPtr<nsContentPermissionRequestProxy> mProxy;
  nsTArray<PermissionRequest> mRequests;

 private:
  virtual mozilla::ipc::IPCResult RecvDestroy() override;
  virtual void ActorDestroy(ActorDestroyReason why) override;
};

ContentPermissionRequestParent::ContentPermissionRequestParent(
    Element* aElement, nsIPrincipal* aPrincipal,
    nsIPrincipal* aTopLevelPrincipal,
    const bool aHasValidTransientUserGestureActivation,
    const bool aIsRequestDelegatedToUnsafeThirdParty,
    const bool aIgnoreAllowSitePermission)
    : mPrincipal(aPrincipal),
      mTopLevelPrincipal(aTopLevelPrincipal),
      mElement(aElement),
      mHasValidTransientUserGestureActivation(
          aHasValidTransientUserGestureActivation),
      mIsRequestDelegatedToUnsafeThirdParty(
          aIsRequestDelegatedToUnsafeThirdParty),
      mIgnoreAllowSitePermission(aIgnoreAllowSitePermission) {
  MOZ_COUNT_CTOR(ContentPermissionRequestParent);
}

ContentPermissionRequestParent::~ContentPermissionRequestParent() {
  MOZ_COUNT_DTOR(ContentPermissionRequestParent);
}

void ContentPermissionRequestParent::Init(
    nsTArray<PermissionRequest>&& aRequests) {
  mRequests = std::move(aRequests);
  mProxy = new nsContentPermissionRequestProxy(this);
  if (NS_FAILED(mProxy->Init(mRequests))) {
    RefPtr<nsContentPermissionRequestProxy> proxy(mProxy);
    proxy->Cancel();
  }
}

mozilla::ipc::IPCResult ContentPermissionRequestParent::RecvDestroy() {
  (void)PContentPermissionRequestParent::Send__delete__(this);
  return IPC_OK();
}

void ContentPermissionRequestParent::ActorDestroy(ActorDestroyReason why) {
  if (mProxy) {
    mProxy->OnParentDestroyed();
  }
}

bool ContentPermissionRequestParent::IsBeingDestroyed() {
  // When ContentParent::MarkAsDead() is called, we are being destroyed.
  // It's unsafe to send out any message now.
  ContentParent* contentParent =
      mozilla::ipc::ActorCast<ContentParent>(Manager());
  return !contentParent->IsAlive();
}

NS_IMPL_ISUPPORTS(ContentPermissionType, nsIContentPermissionType)

ContentPermissionType::ContentPermissionType(
    const nsACString& aType, const nsTArray<nsString>& aOptions) {
  mType = aType;
  mOptions = aOptions.Clone();
}

ContentPermissionType::~ContentPermissionType() = default;

NS_IMETHODIMP
ContentPermissionType::GetType(nsACString& aType) {
  aType = mType;
  return NS_OK;
}

NS_IMETHODIMP
ContentPermissionType::GetOptions(nsIArray** aOptions) {
  NS_ENSURE_ARG_POINTER(aOptions);

  *aOptions = nullptr;

  nsresult rv;
  nsCOMPtr<nsIMutableArray> options =
      do_CreateInstance(NS_ARRAY_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // copy options into JS array
  for (uint32_t i = 0; i < mOptions.Length(); ++i) {
    nsCOMPtr<nsISupportsString> isupportsString =
        do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = isupportsString->SetData(mOptions[i]);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = options->AppendElement(isupportsString);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  options.forget(aOptions);
  return NS_OK;
}

// nsContentPermissionUtils

/* static */
uint32_t nsContentPermissionUtils::ConvertPermissionRequestToArray(
    nsTArray<PermissionRequest>& aSrcArray, nsIMutableArray* aDesArray) {
  uint32_t len = aSrcArray.Length();
  for (uint32_t i = 0; i < len; i++) {
    RefPtr<ContentPermissionType> cpt =
        new ContentPermissionType(aSrcArray[i].type(), aSrcArray[i].options());
    aDesArray->AppendElement(cpt);
  }
  return len;
}

/* static */
void nsContentPermissionUtils::ConvertArrayToPermissionRequest(
    nsIArray* aSrcArray, nsTArray<PermissionRequest>& aDesArray) {
  uint32_t len = 0;
  aSrcArray->GetLength(&len);
  for (uint32_t i = 0; i < len; i++) {
    nsCOMPtr<nsIContentPermissionType> cpt = do_QueryElementAt(aSrcArray, i);
    nsAutoCString type;
    cpt->GetType(type);

    nsCOMPtr<nsIArray> optionArray;
    cpt->GetOptions(getter_AddRefs(optionArray));
    uint32_t optionsLength = 0;
    if (optionArray) {
      optionArray->GetLength(&optionsLength);
    }
    nsTArray<nsString> options;
    for (uint32_t j = 0; j < optionsLength; ++j) {
      nsCOMPtr<nsISupportsString> isupportsString =
          do_QueryElementAt(optionArray, j);
      if (isupportsString) {
        nsString option;
        isupportsString->GetData(option);
        options.AppendElement(option);
      }
    }

    aDesArray.AppendElement(PermissionRequest(type, options));
  }
}

static std::map<PContentPermissionRequestParent*, TabId>&
ContentPermissionRequestParentMap() {
  MOZ_ASSERT(NS_IsMainThread());
  static std::map<PContentPermissionRequestParent*, TabId>
      sPermissionRequestParentMap;
  return sPermissionRequestParentMap;
}

static std::map<PContentPermissionRequestChild*, TabId>&
ContentPermissionRequestChildMap() {
  MOZ_ASSERT(NS_IsMainThread());
  static std::map<PContentPermissionRequestChild*, TabId>
      sPermissionRequestChildMap;
  return sPermissionRequestChildMap;
}

/* static */
nsresult nsContentPermissionUtils::CreatePermissionArray(
    const nsACString& aType, const nsTArray<nsString>& aOptions,
    nsIArray** aTypesArray) {
  nsCOMPtr<nsIMutableArray> types = do_CreateInstance(NS_ARRAY_CONTRACTID);
  RefPtr<ContentPermissionType> permType =
      new ContentPermissionType(aType, aOptions);
  types->AppendElement(permType);
  types.forget(aTypesArray);

  return NS_OK;
}

/* static */
PContentPermissionRequestParent*
nsContentPermissionUtils::CreateContentPermissionRequestParent(
    Element* aElement, nsIPrincipal* aPrincipal,
    nsIPrincipal* aTopLevelPrincipal,
    const bool aHasValidTransientUserGestureActivation,
    const bool aIsRequestDelegatedToUnsafeThirdParty, const TabId& aTabId,
    const bool aIgnoreAllowSitePermission) {
  PContentPermissionRequestParent* parent = new ContentPermissionRequestParent(
      aElement, aPrincipal, aTopLevelPrincipal,
      aHasValidTransientUserGestureActivation,
      aIsRequestDelegatedToUnsafeThirdParty, aIgnoreAllowSitePermission);
  ContentPermissionRequestParentMap()[parent] = aTabId;

  return parent;
}

/* static */
void nsContentPermissionUtils::InitContentPermissionRequestParent(
    PContentPermissionRequestParent* aActor,
    nsTArray<PermissionRequest>&& aRequests) {
  static_cast<ContentPermissionRequestParent*>(aActor)->Init(
      std::move(aRequests));
}

/* static */
nsresult nsContentPermissionUtils::AskPermission(
    nsIContentPermissionRequest* aRequest, nsPIDOMWindowInner* aWindow) {
  // for content process
  if (XRE_IsContentProcess()) {
    NS_ENSURE_STATE(aWindow && aWindow->IsCurrentInnerWindow());

    RefPtr<RemotePermissionRequest> req =
        new RemotePermissionRequest(aRequest, aWindow);

    MOZ_ASSERT(NS_IsMainThread());  // IPC can only be execute on main thread.

    BrowserChild* child = BrowserChild::GetFrom(aWindow->GetDocShell());
    NS_ENSURE_TRUE(child, NS_ERROR_FAILURE);

    nsCOMPtr<nsIArray> typeArray;
    nsresult rv = aRequest->GetTypes(getter_AddRefs(typeArray));
    NS_ENSURE_SUCCESS(rv, rv);

    nsTArray<PermissionRequest> permArray;
    ConvertArrayToPermissionRequest(typeArray, permArray);

    nsCOMPtr<nsIPrincipal> principal;
    rv = aRequest->GetPrincipal(getter_AddRefs(principal));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIPrincipal> topLevelPrincipal;
    rv = aRequest->GetTopLevelPrincipal(getter_AddRefs(topLevelPrincipal));
    NS_ENSURE_SUCCESS(rv, rv);

    bool hasValidTransientUserGestureActivation;
    rv = aRequest->GetHasValidTransientUserGestureActivation(
        &hasValidTransientUserGestureActivation);
    NS_ENSURE_SUCCESS(rv, rv);

    bool isRequestDelegatedToUnsafeThirdParty;
    rv = aRequest->GetIsRequestDelegatedToUnsafeThirdParty(
        &isRequestDelegatedToUnsafeThirdParty);
    NS_ENSURE_SUCCESS(rv, rv);

    bool ignoreAllowSitePermission;
    rv = aRequest->GetIgnoreAllowSitePermission(&ignoreAllowSitePermission);
    NS_ENSURE_SUCCESS(rv, rv);

    req->IPDLAddRef();
    if (!ContentChild::GetSingleton()->SendPContentPermissionRequestConstructor(
            req, permArray, principal, topLevelPrincipal,
            hasValidTransientUserGestureActivation,
            isRequestDelegatedToUnsafeThirdParty, child->GetTabId(),
            ignoreAllowSitePermission)) {
      return NS_ERROR_FAILURE;
    }
    ContentPermissionRequestChildMap()[req.get()] = child->GetTabId();

    return NS_OK;
  }

  // for chrome process
  nsCOMPtr<nsIContentPermissionPrompt> prompt =
      do_GetService(NS_CONTENT_PERMISSION_PROMPT_CONTRACTID);
  if (prompt) {
    if (NS_FAILED(prompt->Prompt(aRequest))) {
      return NS_ERROR_FAILURE;
    }
  }
  return NS_OK;
}

/* static */
nsTArray<PContentPermissionRequestParent*>
nsContentPermissionUtils::GetContentPermissionRequestParentById(
    const TabId& aTabId) {
  nsTArray<PContentPermissionRequestParent*> parentArray;
  for (auto& it : ContentPermissionRequestParentMap()) {
    if (it.second == aTabId) {
      parentArray.AppendElement(it.first);
    }
  }

  return parentArray;
}

/* static */
void nsContentPermissionUtils::NotifyRemoveContentPermissionRequestParent(
    PContentPermissionRequestParent* aParent) {
  auto it = ContentPermissionRequestParentMap().find(aParent);
  MOZ_ASSERT(it != ContentPermissionRequestParentMap().end());

  ContentPermissionRequestParentMap().erase(it);
}

/* static */
nsTArray<PContentPermissionRequestChild*>
nsContentPermissionUtils::GetContentPermissionRequestChildById(
    const TabId& aTabId) {
  nsTArray<PContentPermissionRequestChild*> childArray;
  for (auto& it : ContentPermissionRequestChildMap()) {
    if (it.second == aTabId) {
      childArray.AppendElement(it.first);
    }
  }

  return childArray;
}

/* static */
void nsContentPermissionUtils::NotifyRemoveContentPermissionRequestChild(
    PContentPermissionRequestChild* aChild) {
  auto it = ContentPermissionRequestChildMap().find(aChild);
  MOZ_ASSERT(it != ContentPermissionRequestChildMap().end());

  ContentPermissionRequestChildMap().erase(it);
}

static nsIPrincipal* GetTopLevelPrincipal(nsPIDOMWindowInner* aWindow) {
  MOZ_ASSERT(aWindow);

  BrowsingContext* top = aWindow->GetBrowsingContext()->Top();
  MOZ_ASSERT(top);

  nsPIDOMWindowOuter* outer = top->GetDOMWindow();
  if (!outer) {
    return nullptr;
  }

  nsPIDOMWindowInner* inner = outer->GetCurrentInnerWindow();
  if (!inner) {
    return nullptr;
  }

  return nsGlobalWindowInner::Cast(inner)->GetPrincipal();
}

NS_IMPL_CYCLE_COLLECTION(ContentPermissionRequestBase, mPrincipal,
                         mTopLevelPrincipal, mWindow)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ContentPermissionRequestBase)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(nsISupports)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(nsIContentPermissionRequest)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(ContentPermissionRequestBase)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ContentPermissionRequestBase)

ContentPermissionRequestBase::ContentPermissionRequestBase(
    nsIPrincipal* aPrincipal, nsPIDOMWindowInner* aWindow,
    const nsACString& aPrefName, const nsACString& aType)
    : mPrincipal(aPrincipal),
      mTopLevelPrincipal(aWindow ? ::GetTopLevelPrincipal(aWindow) : nullptr),
      mWindow(aWindow),
      mPrefName(aPrefName),
      mType(aType),
      mHasValidTransientUserGestureActivation(false),
      mIsRequestDelegatedToUnsafeThirdParty(false) {
  if (!aWindow) {
    return;
  }

  Document* doc = aWindow->GetExtantDoc();
  if (!doc) {
    return;
  }

  mHasValidTransientUserGestureActivation =
      doc->HasValidTransientUserGestureActivation();

  mPermissionHandler = doc->GetPermissionDelegateHandler();
  if (mPermissionHandler) {
    nsTArray<nsCString> types;
    types.AppendElement(mType);
    mPermissionHandler->MaybeUnsafePermissionDelegate(
        types, &mIsRequestDelegatedToUnsafeThirdParty);
  }
}

NS_IMETHODIMP
ContentPermissionRequestBase::GetPrincipal(
    nsIPrincipal** aRequestingPrincipal) {
  NS_IF_ADDREF(*aRequestingPrincipal = mPrincipal);
  return NS_OK;
}

NS_IMETHODIMP
ContentPermissionRequestBase::GetDelegatePrincipal(
    const nsACString& aType, nsIPrincipal** aRequestingPrincipal) {
  return PermissionDelegateHandler::GetDelegatePrincipal(aType, this,
                                                         aRequestingPrincipal);
}

NS_IMETHODIMP
ContentPermissionRequestBase::GetIsRequestDelegatedToUnsafeThirdParty(
    bool* aIsRequestDelegatedToUnsafeThirdParty) {
  *aIsRequestDelegatedToUnsafeThirdParty =
      mIsRequestDelegatedToUnsafeThirdParty;
  return NS_OK;
}

NS_IMETHODIMP
ContentPermissionRequestBase::GetIgnoreAllowSitePermission(
    bool* aIgnoreAllowSitePermission) {
  *aIgnoreAllowSitePermission = false;
  return NS_OK;
}

NS_IMETHODIMP
ContentPermissionRequestBase::GetTopLevelPrincipal(
    nsIPrincipal** aRequestingPrincipal) {
  if (!mTopLevelPrincipal) {
    *aRequestingPrincipal = nullptr;
    return NS_OK;
  }

  NS_IF_ADDREF(*aRequestingPrincipal = mTopLevelPrincipal);
  return NS_OK;
}

NS_IMETHODIMP
ContentPermissionRequestBase::GetWindow(mozIDOMWindow** aRequestingWindow) {
  NS_IF_ADDREF(*aRequestingWindow = mWindow);
  return NS_OK;
}

NS_IMETHODIMP
ContentPermissionRequestBase::GetElement(Element** aElement) {
  NS_ENSURE_ARG_POINTER(aElement);
  *aElement = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
ContentPermissionRequestBase::NotifyShown() {
  // Default implementation does nothing.
  // Subclasses can override to perform actions when prompt is shown.
  return NS_OK;
}

NS_IMETHODIMP
ContentPermissionRequestBase::GetHasValidTransientUserGestureActivation(
    bool* aHasValidTransientUserGestureActivation) {
  *aHasValidTransientUserGestureActivation =
      mHasValidTransientUserGestureActivation;
  return NS_OK;
}

NS_IMETHODIMP
ContentPermissionRequestBase::GetTypes(nsIArray** aTypes) {
  nsTArray<nsString> emptyOptions;
  return nsContentPermissionUtils::CreatePermissionArray(mType, emptyOptions,
                                                         aTypes);
}

ContentPermissionRequestBase::PromptResult
ContentPermissionRequestBase::CheckPromptPrefs() const {
  MOZ_ASSERT(!mPrefName.IsEmpty(),
             "This derived class must support checking pref types");

  nsAutoCString prefName(mPrefName);
  prefName.AppendLiteral(".prompt.testing");
  if (Preferences::GetBool(PromiseFlatCString(prefName).get(), false)) {
    prefName.AppendLiteral(".allow");
    if (Preferences::GetBool(PromiseFlatCString(prefName).get(), true)) {
      return PromptResult::Granted;
    }
    return PromptResult::Denied;
  }

  return PromptResult::Pending;
}

bool ContentPermissionRequestBase::CheckPermissionDelegate() const {
  // There is case that ContentPermissionRequestBase is constructed without
  // window, then mPermissionHandler will be null. So we only check permission
  // delegate if we have non-null mPermissionHandler
  if (mPermissionHandler &&
      !mPermissionHandler->HasPermissionDelegated(mType)) {
    return false;
  }

  return true;
}

nsresult ContentPermissionRequestBase::ShowPrompt(
    ContentPermissionRequestBase::PromptResult& aResult) {
  if (!CheckPermissionDelegate()) {
    aResult = PromptResult::Denied;
    return NS_OK;
  }

  aResult = CheckPromptPrefs();

  if (aResult != PromptResult::Pending) {
    return NS_OK;
  }

  return nsContentPermissionUtils::AskPermission(this, mWindow);
}

class RequestPromptEvent : public Runnable {
 public:
  RequestPromptEvent(ContentPermissionRequestBase* aRequest,
                     nsPIDOMWindowInner* aWindow)
      : mozilla::Runnable("RequestPromptEvent"),
        mRequest(aRequest),
        mWindow(aWindow) {}

  NS_IMETHOD Run() override {
    nsContentPermissionUtils::AskPermission(mRequest, mWindow);
    return NS_OK;
  }

 private:
  RefPtr<ContentPermissionRequestBase> mRequest;
  nsCOMPtr<nsPIDOMWindowInner> mWindow;
};

class RequestAllowEvent : public Runnable {
 public:
  RequestAllowEvent(bool allow, ContentPermissionRequestBase* request)
      : mozilla::Runnable("RequestAllowEvent"),
        mAllow(allow),
        mRequest(request) {}

  // Not MOZ_CAN_RUN_SCRIPT because we can't annotate the thing we override yet.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  NS_IMETHOD Run() override {
    // MOZ_KnownLive is OK, because we never drop the ref to mRequest.
    if (mAllow) {
      MOZ_KnownLive(mRequest)->Allow(JS::UndefinedHandleValue);
    } else {
      MOZ_KnownLive(mRequest)->Cancel();
    }
    return NS_OK;
  }

 private:
  bool mAllow;
  RefPtr<ContentPermissionRequestBase> mRequest;
};

void ContentPermissionRequestBase::RequestDelayedTask(
    nsIEventTarget* aTarget,
    ContentPermissionRequestBase::DelayedTaskType aType) {
  nsCOMPtr<nsIRunnable> r;
  switch (aType) {
    case DelayedTaskType::Allow:
      r = new RequestAllowEvent(true, this);
      break;
    case DelayedTaskType::Deny:
      r = new RequestAllowEvent(false, this);
      break;
    default:
      r = new RequestPromptEvent(this, mWindow);
      break;
  }

  aTarget->Dispatch(r.forget());
}

nsresult TranslateChoices(
    JS::Handle<JS::Value> aChoices,
    const nsTArray<PermissionRequest>& aPermissionRequests,
    nsTArray<PermissionChoice>& aTranslatedChoices) {
  if (aChoices.isNullOrUndefined()) {
    // No choice is specified.
  } else if (aChoices.isObject()) {
    // Iterate through all permission types.
    for (uint32_t i = 0; i < aPermissionRequests.Length(); ++i) {
      nsCString type = aPermissionRequests[i].type();

      JS::Rooted<JSObject*> obj(RootingCx(), &aChoices.toObject());
      // People really shouldn't be passing WindowProxy or Location
      // objects for the choices here.
      obj = js::CheckedUnwrapStatic(obj);
      if (!obj) {
        return NS_ERROR_FAILURE;
      }

      AutoJSAPI jsapi;
      jsapi.Init();

      JSContext* cx = jsapi.cx();
      JSAutoRealm ar(cx, obj);

      JS::Rooted<JS::Value> val(cx);

      if (!JS_GetProperty(cx, obj, type.get(), &val) || !val.isString()) {
        // no setting for the permission type, clear exception and skip it
        jsapi.ClearException();
      } else {
        nsAutoJSString choice;
        if (!choice.init(cx, val)) {
          jsapi.ClearException();
          return NS_ERROR_FAILURE;
        }
        aTranslatedChoices.AppendElement(PermissionChoice(type, choice));
      }
    }
  } else {
    MOZ_ASSERT(false, "SelectedChoices should be undefined or an JS object");
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

}  // namespace mozilla::dom

nsContentPermissionRequestProxy::nsContentPermissionRequestProxy(
    ContentPermissionRequestParent* parent)
    : mParent(parent) {
  NS_ASSERTION(mParent, "null parent");
}

nsContentPermissionRequestProxy::~nsContentPermissionRequestProxy() = default;

nsresult nsContentPermissionRequestProxy::Init(
    const nsTArray<PermissionRequest>& requests) {
  mPermissionRequests = requests.Clone();

  nsCOMPtr<nsIContentPermissionPrompt> prompt =
      do_GetService(NS_CONTENT_PERMISSION_PROMPT_CONTRACTID);
  if (!prompt) {
    return NS_ERROR_FAILURE;
  }

  prompt->Prompt(this);
  return NS_OK;
}

void nsContentPermissionRequestProxy::OnParentDestroyed() { mParent = nullptr; }

NS_IMPL_ISUPPORTS(nsContentPermissionRequestProxy, nsIContentPermissionRequest)

NS_IMETHODIMP
nsContentPermissionRequestProxy::GetTypes(nsIArray** aTypes) {
  nsCOMPtr<nsIMutableArray> types = do_CreateInstance(NS_ARRAY_CONTRACTID);
  if (mozilla::dom::nsContentPermissionUtils::ConvertPermissionRequestToArray(
          mPermissionRequests, types)) {
    types.forget(aTypes);
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsContentPermissionRequestProxy::GetWindow(mozIDOMWindow** aRequestingWindow) {
  NS_ENSURE_ARG_POINTER(aRequestingWindow);
  *aRequestingWindow = nullptr;  // ipc doesn't have a window
  return NS_OK;
}

NS_IMETHODIMP
nsContentPermissionRequestProxy::GetPrincipal(
    nsIPrincipal** aRequestingPrincipal) {
  NS_ENSURE_ARG_POINTER(aRequestingPrincipal);
  if (mParent == nullptr) {
    return NS_ERROR_FAILURE;
  }

  NS_IF_ADDREF(*aRequestingPrincipal = mParent->mPrincipal);
  return NS_OK;
}

NS_IMETHODIMP
nsContentPermissionRequestProxy::GetTopLevelPrincipal(
    nsIPrincipal** aRequestingPrincipal) {
  NS_ENSURE_ARG_POINTER(aRequestingPrincipal);
  if (mParent == nullptr) {
    return NS_ERROR_FAILURE;
  }

  NS_IF_ADDREF(*aRequestingPrincipal = mParent->mTopLevelPrincipal);
  return NS_OK;
}

NS_IMETHODIMP
nsContentPermissionRequestProxy::GetDelegatePrincipal(
    const nsACString& aType, nsIPrincipal** aRequestingPrincipal) {
  NS_ENSURE_ARG_POINTER(aRequestingPrincipal);
  if (mParent == nullptr) {
    return NS_ERROR_FAILURE;
  }

  return PermissionDelegateHandler::GetDelegatePrincipal(aType, this,
                                                         aRequestingPrincipal);
}

NS_IMETHODIMP
nsContentPermissionRequestProxy::GetElement(Element** aRequestingElement) {
  NS_ENSURE_ARG_POINTER(aRequestingElement);
  if (mParent == nullptr) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<Element> elem = mParent->mElement;
  elem.forget(aRequestingElement);
  return NS_OK;
}

NS_IMETHODIMP
nsContentPermissionRequestProxy::GetHasValidTransientUserGestureActivation(
    bool* aHasValidTransientUserGestureActivation) {
  NS_ENSURE_ARG_POINTER(aHasValidTransientUserGestureActivation);
  if (mParent == nullptr) {
    return NS_ERROR_FAILURE;
  }
  *aHasValidTransientUserGestureActivation =
      mParent->mHasValidTransientUserGestureActivation;
  return NS_OK;
}

NS_IMETHODIMP
nsContentPermissionRequestProxy::GetIsRequestDelegatedToUnsafeThirdParty(
    bool* aIsRequestDelegatedToUnsafeThirdParty) {
  NS_ENSURE_ARG_POINTER(aIsRequestDelegatedToUnsafeThirdParty);
  if (mParent == nullptr) {
    return NS_ERROR_FAILURE;
  }
  *aIsRequestDelegatedToUnsafeThirdParty =
      mParent->mIsRequestDelegatedToUnsafeThirdParty;
  return NS_OK;
}

NS_IMETHODIMP
nsContentPermissionRequestProxy::GetIgnoreAllowSitePermission(
    bool* aIgnoreAllowSitePermission) {
  NS_ENSURE_ARG_POINTER(aIgnoreAllowSitePermission);
  if (mParent == nullptr) {
    return NS_ERROR_FAILURE;
  }
  *aIgnoreAllowSitePermission = mParent->mIgnoreAllowSitePermission;
  return NS_OK;
}

NS_IMETHODIMP
nsContentPermissionRequestProxy::NotifyShown() {
  // This is a proxy class that forwards to content process.
  // The actual NotifyShown() logic is handled by the real implementation
  // in the content process, so we don't need to do anything here.
  return NS_OK;
}

NS_IMETHODIMP
nsContentPermissionRequestProxy::Cancel() {
  if (mParent == nullptr) {
    return NS_ERROR_FAILURE;
  }

  // Don't send out the delete message when the managing protocol (PBrowser) is
  // being destroyed and PContentPermissionRequest will soon be.
  if (mParent->IsBeingDestroyed()) {
    return NS_ERROR_FAILURE;
  }

  nsTArray<PermissionChoice> emptyChoices;

  (void)mParent->SendNotifyResult(false, emptyChoices);
  return NS_OK;
}

NS_IMETHODIMP
nsContentPermissionRequestProxy::Allow(JS::Handle<JS::Value> aChoices) {
  if (mParent == nullptr) {
    return NS_ERROR_FAILURE;
  }

  // Don't send out the delete message when the managing protocol (PBrowser) is
  // being destroyed and PContentPermissionRequest will soon be.
  if (mParent->IsBeingDestroyed()) {
    return NS_ERROR_FAILURE;
  }

  nsTArray<PermissionChoice> choices;
  nsresult rv = TranslateChoices(aChoices, mPermissionRequests, choices);
  if (NS_FAILED(rv)) {
    return rv;
  }

  (void)mParent->SendNotifyResult(true, choices);
  return NS_OK;
}

// RemotePermissionRequest

RemotePermissionRequest::RemotePermissionRequest(
    nsIContentPermissionRequest* aRequest, nsPIDOMWindowInner* aWindow)
    : mRequest(aRequest),
      mWindow(aWindow),
      mIPCOpen(false),
      mDestroyed(false) {}

RemotePermissionRequest::~RemotePermissionRequest() {
  MOZ_ASSERT(
      !mIPCOpen,
      "Protocol must not be open when RemotePermissionRequest is destroyed.");
}

void RemotePermissionRequest::DoCancel() {
  NS_ASSERTION(mRequest, "We need a request");
  nsCOMPtr<nsIContentPermissionRequest> request(mRequest);
  request->Cancel();
}

void RemotePermissionRequest::DoAllow(JS::Handle<JS::Value> aChoices) {
  NS_ASSERTION(mRequest, "We need a request");
  nsCOMPtr<nsIContentPermissionRequest> request(mRequest);
  request->Allow(aChoices);
}

// PContentPermissionRequestChild
mozilla::ipc::IPCResult RemotePermissionRequest::RecvNotifyResult(
    const bool& aAllow, nsTArray<PermissionChoice>&& aChoices) {
  Destroy();

  if (aAllow && mWindow->IsCurrentInnerWindow()) {
    // Use 'undefined' if no choice is provided.
    if (aChoices.IsEmpty()) {
      DoAllow(JS::UndefinedHandleValue);
      return IPC_OK();
    }

    // Convert choices to a JS val if any.
    // {"type1": "choice1", "type2": "choiceA"}
    AutoJSAPI jsapi;
    if (NS_WARN_IF(!jsapi.Init(mWindow))) {
      return IPC_OK();  // This is not an IPC error.
    }

    JSContext* cx = jsapi.cx();
    JS::Rooted<JSObject*> obj(cx);
    obj = JS_NewPlainObject(cx);
    for (uint32_t i = 0; i < aChoices.Length(); ++i) {
      const nsString& choice = aChoices[i].choice();
      const nsCString& type = aChoices[i].type();
      JS::Rooted<JSString*> jChoice(
          cx, JS_NewUCStringCopyN(cx, choice.get(), choice.Length()));
      JS::Rooted<JS::Value> vChoice(cx, StringValue(jChoice));
      if (!JS_SetProperty(cx, obj, type.get(), vChoice)) {
        return IPC_FAIL_NO_REASON(this);
      }
    }
    JS::Rooted<JS::Value> val(cx, JS::ObjectValue(*obj));
    DoAllow(val);
  } else {
    DoCancel();
  }
  return IPC_OK();
}

void RemotePermissionRequest::Destroy() {
  if (!IPCOpen()) {
    return;
  }
  (void)this->SendDestroy();
  mDestroyed = true;
}
