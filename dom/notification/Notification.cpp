/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/Notification.h"

#include <utility>

#include "mozilla/Encoding.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/OwningNonNull.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/ServiceWorkerGlobalScopeBinding.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/glean/DomNotificationMetrics.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "Navigator.h"
#include "NotificationUtils.h"
#include "nsContentPermissionHelper.h"
#include "nsContentUtils.h"
#include "nsIContentPermissionPrompt.h"
#include "nsIScriptError.h"
#include "nsNetUtil.h"
#include "nsStructuredCloneContainer.h"

namespace mozilla::dom {

using namespace notification;

struct NotificationStrings {
  const nsString mID;
  const nsString mTitle;
  const nsString mDir;
  const nsString mLang;
  const nsString mBody;
  const nsString mTag;
  const nsString mIcon;
  const nsString mData;
  const nsString mServiceWorkerRegistrationScope;
};

class NotificationPermissionRequest : public ContentPermissionRequestBase,
                                      public nsIRunnable,
                                      public nsINamed {
 public:
  NS_DECL_NSIRUNNABLE
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(NotificationPermissionRequest,
                                           ContentPermissionRequestBase)

  // nsIContentPermissionRequest
  NS_IMETHOD Cancel(void) override;
  NS_IMETHOD Allow(JS::Handle<JS::Value> choices) override;

  NotificationPermissionRequest(nsIPrincipal* aPrincipal,
                                nsIPrincipal* aEffectiveStoragePrincipal,
                                nsPIDOMWindowInner* aWindow, Promise* aPromise,
                                NotificationPermissionCallback* aCallback)
      : ContentPermissionRequestBase(aPrincipal, aWindow, "notification"_ns,
                                     "desktop-notification"_ns),
        mEffectiveStoragePrincipal(aEffectiveStoragePrincipal),
        mPermission(NotificationPermission::Default),
        mPromise(aPromise),
        mCallback(aCallback) {
    MOZ_ASSERT(aPromise);
  }

  NS_IMETHOD GetName(nsACString& aName) override {
    aName.AssignLiteral("NotificationPermissionRequest");
    return NS_OK;
  }

 protected:
  ~NotificationPermissionRequest() = default;

  MOZ_CAN_RUN_SCRIPT nsresult ResolvePromise();
  nsresult DispatchResolvePromise();
  nsCOMPtr<nsIPrincipal> mEffectiveStoragePrincipal;
  NotificationPermission mPermission;
  RefPtr<Promise> mPromise;
  RefPtr<NotificationPermissionCallback> mCallback;
};

namespace {
class GetPermissionRunnable final : public WorkerMainThreadRunnable {
 public:
  explicit GetPermissionRunnable(WorkerPrivate* aWorker,
                                 bool aUseRegularPrincipal,
                                 PermissionCheckPurpose aPurpose)
      : WorkerMainThreadRunnable(aWorker, "Notification :: Get Permission"_ns),
        mUseRegularPrincipal(aUseRegularPrincipal),
        mPurpose(aPurpose) {}

  bool MainThreadRun() override {
    MOZ_ASSERT(mWorkerRef);
    WorkerPrivate* workerPrivate = mWorkerRef->Private();
    nsIPrincipal* principal = workerPrivate->GetPrincipal();
    nsIPrincipal* effectiveStoragePrincipal =
        mUseRegularPrincipal ? principal
                             : workerPrivate->GetPartitionedPrincipal();
    mPermission =
        GetNotificationPermission(principal, effectiveStoragePrincipal,
                                  workerPrivate->IsSecureContext(), mPurpose);
    return true;
  }

  NotificationPermission GetPermission() { return mPermission; }

 private:
  NotificationPermission mPermission = NotificationPermission::Denied;
  bool mUseRegularPrincipal;
  PermissionCheckPurpose mPurpose;
};

}  // anonymous namespace

NS_IMPL_CYCLE_COLLECTION_INHERITED(NotificationPermissionRequest,
                                   ContentPermissionRequestBase, mCallback)
NS_IMPL_ADDREF_INHERITED(NotificationPermissionRequest,
                         ContentPermissionRequestBase)
NS_IMPL_RELEASE_INHERITED(NotificationPermissionRequest,
                          ContentPermissionRequestBase)

NS_IMPL_QUERY_INTERFACE_CYCLE_COLLECTION_INHERITED(
    NotificationPermissionRequest, ContentPermissionRequestBase, nsIRunnable,
    nsINamed)

NS_IMETHODIMP
NotificationPermissionRequest::Run() {
  if (IsNotificationAllowedFor(mPrincipal)) {
    mPermission = NotificationPermission::Granted;
  } else if (IsNotificationForbiddenFor(
                 mPrincipal, mEffectiveStoragePrincipal,
                 mWindow->IsSecureContext(),
                 PermissionCheckPurpose::PermissionRequest,
                 mWindow->GetExtantDoc())) {
    mPermission = NotificationPermission::Denied;
  }

  // We can't call ShowPrompt() directly here since our logic for determining
  // whether to display a prompt depends on the checks above as well as the
  // result of CheckPromptPrefs().  So we have to manually check the prompt
  // prefs and decide what to do based on that.
  PromptResult pr = CheckPromptPrefs();
  switch (pr) {
    case PromptResult::Granted:
      mPermission = NotificationPermission::Granted;
      break;
    case PromptResult::Denied:
      mPermission = NotificationPermission::Denied;
      break;
    default:
      // ignore
      break;
  }

  if (!mHasValidTransientUserGestureActivation &&
      !StaticPrefs::dom_webnotifications_requireuserinteraction()) {
    nsCOMPtr<Document> doc = mWindow->GetExtantDoc();
    if (doc) {
      doc->WarnOnceAbout(Document::eNotificationsRequireUserGestureDeprecation);
    }
  }

  if (mPermission != NotificationPermission::Default) {
    return DispatchResolvePromise();
  }

  return nsContentPermissionUtils::AskPermission(this, mWindow);
}

NS_IMETHODIMP
NotificationPermissionRequest::Cancel() {
  // `Cancel` is called if the user denied permission or dismissed the
  // permission request. To distinguish between the two, we set the
  // permission to "default" and query the permission manager in
  // `ResolvePromise`.
  mPermission = NotificationPermission::Default;
  return DispatchResolvePromise();
}

NS_IMETHODIMP
NotificationPermissionRequest::Allow(JS::Handle<JS::Value> aChoices) {
  MOZ_ASSERT(aChoices.isUndefined());

  mPermission = NotificationPermission::Granted;
  return DispatchResolvePromise();
}

inline nsresult NotificationPermissionRequest::DispatchResolvePromise() {
  nsCOMPtr<nsIRunnable> resolver =
      NewRunnableMethod("NotificationPermissionRequest::DispatchResolvePromise",
                        this, &NotificationPermissionRequest::ResolvePromise);
  return nsGlobalWindowInner::Cast(mWindow.get())->Dispatch(resolver.forget());
}

nsresult NotificationPermissionRequest::ResolvePromise() {
  nsresult rv = NS_OK;
  // This will still be "default" if the user dismissed the doorhanger,
  // or "denied" otherwise.
  if (mPermission == NotificationPermission::Default) {
    // When the front-end has decided to deny the permission request
    // automatically and we are not handling user input, then log a
    // warning in the current document that this happened because
    // Notifications require a user gesture.
    if (!mHasValidTransientUserGestureActivation &&
        StaticPrefs::dom_webnotifications_requireuserinteraction()) {
      nsCOMPtr<Document> doc = mWindow->GetExtantDoc();
      if (doc) {
        nsContentUtils::ReportToConsole(nsIScriptError::errorFlag, "DOM"_ns,
                                        doc, nsContentUtils::eDOM_PROPERTIES,
                                        "NotificationsRequireUserGesture");
      }
    }

    mPermission = GetRawNotificationPermission(mPrincipal);
  }
  if (mCallback) {
    ErrorResult error;
    RefPtr<NotificationPermissionCallback> callback(mCallback);
    callback->Call(mPermission, error);
    rv = error.StealNSResult();
  }
  mPromise->MaybeResolve(mPermission);
  return rv;
}

// static
bool Notification::PrefEnabled(JSContext* aCx, JSObject* aObj) {
  return StaticPrefs::dom_webnotifications_enabled();
}

Notification::Notification(nsIGlobalObject* aGlobal,
                           const IPCNotification& aIPCNotification,
                           const nsAString& aScope)
    : DOMEventTargetHelper(aGlobal),
      mIPCNotification(aIPCNotification),
      mData(JS::NullValue()),
      mScope(aScope) {
  KeepAliveIfHasListenersFor(nsGkAtoms::onclick);
  KeepAliveIfHasListenersFor(nsGkAtoms::onshow);
  KeepAliveIfHasListenersFor(nsGkAtoms::onerror);
  KeepAliveIfHasListenersFor(nsGkAtoms::onclose);
}

// May be called on any thread.
// static
already_AddRefed<Notification> Notification::Constructor(
    const GlobalObject& aGlobal, const nsAString& aTitle,
    const NotificationOptions& aOptions, ErrorResult& aRv) {
  // FIXME(nsm): If the sticky flag is set, throw an error.
  RefPtr<ServiceWorkerGlobalScope> scope;
  UNWRAP_OBJECT(ServiceWorkerGlobalScope, aGlobal.Get(), scope);
  if (scope) {
    aRv.ThrowTypeError(
        "Notification constructor cannot be used in ServiceWorkerGlobalScope. "
        "Use registration.showNotification() instead.");
    return nullptr;
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<Notification> notification = ValidateAndCreate(
      aGlobal.Context(), global, aTitle, aOptions, u""_ns, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::CreateInfallible(global);
  promise->AddCallbacksWithCycleCollectedArgs(
      [](JSContext*, JS::Handle<JS::Value>, ErrorResult&,
         Notification* aNotification) {
        aNotification->DispatchTrustedEvent(u"show"_ns);
      },
      [](JSContext*, JS::Handle<JS::Value> aValue, ErrorResult&,
         Notification* aNotification) {
        aNotification->DispatchTrustedEvent(u"error"_ns);
        aNotification->Deactivate();
      },
      notification);
  if (!notification->CreateActor() || !notification->SendShow(promise)) {
    notification->Deactivate();
    return nullptr;
  }

  notification->KeepAliveIfHasListenersFor(nsGkAtoms::onclick);
  notification->KeepAliveIfHasListenersFor(nsGkAtoms::onshow);
  notification->KeepAliveIfHasListenersFor(nsGkAtoms::onerror);
  notification->KeepAliveIfHasListenersFor(nsGkAtoms::onclose);

  return notification.forget();
}

// NOTE(krosylight): Maybe move this check to the parent process?
Result<Ok, nsresult> ValidateBase64Data(const nsAString& aData) {
  if (aData.IsEmpty()) {
    return Ok();
  }

  // To and from to ensure it is valid base64.
  RefPtr<nsStructuredCloneContainer> container =
      new nsStructuredCloneContainer();
  MOZ_TRY(container->InitFromBase64(aData, JS_STRUCTURED_CLONE_VERSION));

  nsString result;
  MOZ_TRY(container->GetDataAsBase64(result));

  return Ok();
}

// static
Result<already_AddRefed<Notification>, nsresult> Notification::ConstructFromIPC(
    nsIGlobalObject* aGlobal, const IPCNotification& aIPCNotification,
    const nsAString& aServiceWorkerRegistrationScope) {
  MOZ_ASSERT(aGlobal);

  MOZ_TRY(ValidateBase64Data(aIPCNotification.options().dataSerialized()));

  RefPtr<Notification> notification = new Notification(
      aGlobal, aIPCNotification, aServiceWorkerRegistrationScope);

  return notification.forget();
}

void Notification::MaybeNotifyClose() {
  if (mIsClosed) {
    return;
  }
  mIsClosed = true;
  DispatchTrustedEvent(u"close"_ns);
}

static Result<nsString, nsresult> SerializeDataAsBase64(
    JSContext* aCx, JS::Handle<JS::Value> aData) {
  if (aData.isNull()) {
    return nsString();
  }
  RefPtr<nsStructuredCloneContainer> dataObjectContainer =
      new nsStructuredCloneContainer();
  MOZ_TRY(dataObjectContainer->InitFromJSVal(aData, aCx));

  nsString result;
  MOZ_TRY(dataObjectContainer->GetDataAsBase64(result));

  return result;
}

/* static */
// https://notifications.spec.whatwg.org/#create-a-notification
already_AddRefed<Notification> Notification::ValidateAndCreate(
    JSContext* aCx, nsIGlobalObject* aGlobal, const nsAString& aTitle,
    const NotificationOptions& aOptions, const nsAString& aScope,
    ErrorResult& aRv) {
  MOZ_ASSERT(aGlobal);

  // Step 4: Set notification’s data to
  // StructuredSerializeForStorage(options["data"]).
  JS::Rooted<JS::Value> data(aCx, aOptions.mData);
  Result<nsString, nsresult> dataResult = SerializeDataAsBase64(aCx, data);
  if (dataResult.isErr()) {
    aRv = dataResult.unwrapErr();
    return nullptr;
  }

  // Step 17: Set notification’s silent preference to options["silent"].
  bool silent = false;
  if (StaticPrefs::dom_webnotifications_silent_enabled()) {
    silent = aOptions.mSilent;
  }

  nsTArray<uint32_t> vibrate;
  if (StaticPrefs::dom_webnotifications_vibrate_enabled() &&
      aOptions.mVibrate.WasPassed()) {
    // Step 2: If options["silent"] is true and options["vibrate"] exists, then
    // throw a TypeError.
    if (silent) {
      aRv.ThrowTypeError(
          "Silent notifications must not specify vibration patterns.");
      return nullptr;
    }

    // Step 14: If options["vibrate"] exists, then validate and normalize it and
    // set notification’s vibration pattern to the return value.
    const OwningUnsignedLongOrUnsignedLongSequence& value =
        aOptions.mVibrate.Value();
    if (value.IsUnsignedLong()) {
      AutoTArray<uint32_t, 1> array;
      array.AppendElement(value.GetAsUnsignedLong());
      vibrate = SanitizeVibratePattern(array);
    } else {
      vibrate = SanitizeVibratePattern(value.GetAsUnsignedLongSequence());
    }
  }

  // Step 12: If options["icon"] exists, then parse it using baseURL, and if
  // that does not return failure, set notification’s icon URL to the return
  // value. (Otherwise icon URL is not set.)
  nsAutoString iconUrl;
  ResolveIconURL(aGlobal, aOptions.mIcon, iconUrl);

  // Step 19: Set notification’s actions to « ».
  nsTArray<IPCNotificationAction> actions;
  if (StaticPrefs::dom_webnotifications_actions_enabled()) {
    // Step 20: For each entry in options["actions"], up to the maximum number
    // of actions supported (skip any excess entries):
    for (const auto& entry : aOptions.mActions) {
      // Step 20.1: Let action be a new notification action.
      IPCNotificationAction action;
      // Step 20.2: Set action’s name to entry["action"].
      action.name() = entry.mAction;
      // Step 20.3: Set action’s title to entry["title"].
      action.title() = entry.mTitle;
      // Step 20.4: (Skipping icon support, see
      // https://github.com/whatwg/notifications/issues/233)
      // Step 20.5: Append action to notification’s actions.
      actions.AppendElement(std::move(action));
      if (actions.Length() == kMaxActions) {
        break;
      }
    }
  }

  IPCNotification ipcNotification(
      nsString(), IPCNotificationOptions(
                      nsString(aTitle), aOptions.mDir, nsString(aOptions.mLang),
                      nsString(aOptions.mBody), nsString(aOptions.mTag),
                      iconUrl, aOptions.mRequireInteraction, silent, vibrate,
                      nsString(dataResult.unwrap()), std::move(actions)));

  RefPtr<Notification> notification =
      new Notification(aGlobal, ipcNotification, aScope);
  return notification.forget();
}

Notification::~Notification() { mozilla::DropJSObjects(this); }

NS_IMPL_CYCLE_COLLECTION_CLASS(Notification)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(Notification,
                                                DOMEventTargetHelper)
  tmp->mData.setUndefined();
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_PTR
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(Notification,
                                                  DOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(Notification,
                                               DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mData)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_ADDREF_INHERITED(Notification, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(Notification, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(Notification)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

/* static */
bool Notification::RequestPermissionEnabledForScope(JSContext* aCx,
                                                    JSObject* /* unused */) {
  // requestPermission() is not allowed on workers. The calling page should ask
  // for permission on the worker's behalf. This is to prevent 'which window
  // should show the browser pop-up'. See discussion:
  // http://lists.whatwg.org/pipermail/whatwg-whatwg.org/2013-October/041272.html
  return NS_IsMainThread();
}

// static
already_AddRefed<Promise> Notification::RequestPermission(
    const GlobalObject& aGlobal,
    const Optional<OwningNonNull<NotificationPermissionCallback>>& aCallback,
    ErrorResult& aRv) {
  AssertIsOnMainThread();

  // Get principal from global to make permission request for notifications.
  nsCOMPtr<nsPIDOMWindowInner> window =
      do_QueryInterface(aGlobal.GetAsSupports());
  nsCOMPtr<nsIScriptObjectPrincipal> sop =
      do_QueryInterface(aGlobal.GetAsSupports());
  if (!sop || !window) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  nsCOMPtr<nsIPrincipal> principal = sop->GetPrincipal();
  nsCOMPtr<nsIPrincipal> effectiveStoragePrincipal =
      sop->GetEffectiveStoragePrincipal();
  if (!principal || !effectiveStoragePrincipal) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(window->AsGlobal(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  NotificationPermissionCallback* permissionCallback = nullptr;
  if (aCallback.WasPassed()) {
    permissionCallback = &aCallback.Value();
  }
  nsCOMPtr<nsIRunnable> request =
      new NotificationPermissionRequest(principal, effectiveStoragePrincipal,
                                        window, promise, permissionCallback);

  window->AsGlobal()->Dispatch(request.forget());

  return promise.forget();
}

// static
NotificationPermission Notification::GetPermission(const GlobalObject& aGlobal,
                                                   ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  return GetPermission(global, PermissionCheckPurpose::PermissionAttribute,
                       aRv);
}

// static
NotificationPermission Notification::GetPermission(
    nsIGlobalObject* aGlobal, PermissionCheckPurpose aPurpose,
    ErrorResult& aRv) {
  if (NS_IsMainThread()) {
    return GetPermissionInternal(aGlobal->GetAsInnerWindow(), aPurpose, aRv);
  }

  WorkerPrivate* worker = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT(worker);
  RefPtr<GetPermissionRunnable> r = new GetPermissionRunnable(
      worker, worker->UseRegularPrincipal(), aPurpose);
  r->Dispatch(worker, Canceling, aRv);
  if (aRv.Failed()) {
    return NotificationPermission::Denied;
  }

  return r->GetPermission();
}

/* static */
NotificationPermission Notification::GetPermissionInternal(
    nsPIDOMWindowInner* aWindow, PermissionCheckPurpose aPurpose,
    ErrorResult& aRv) {
  // Get principal from global to check permission for notifications.
  nsCOMPtr<nsIScriptObjectPrincipal> sop = do_QueryInterface(aWindow);
  if (!sop) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return NotificationPermission::Denied;
  }

  nsCOMPtr<nsIPrincipal> principal = sop->GetPrincipal();
  nsCOMPtr<nsIPrincipal> effectiveStoragePrincipal =
      sop->GetEffectiveStoragePrincipal();
  if (!principal || !effectiveStoragePrincipal) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return NotificationPermission::Denied;
  }

  return GetNotificationPermission(principal, effectiveStoragePrincipal,
                                   aWindow->IsSecureContext(), aPurpose);
}

uint32_t Notification::MaxActions(const GlobalObject& aGlobal) {
  return kMaxActions;
}

nsresult Notification::ResolveIconURL(nsIGlobalObject* aGlobal,
                                      const nsAString& aIconUrl,
                                      nsString& aDecodedUrl) {
  nsresult rv = NS_OK;

  if (aIconUrl.IsEmpty()) {
    return rv;
  }

  nsCOMPtr<nsIURI> baseUri = nullptr;

  // XXXnsm If I understand correctly, the character encoding for resolving
  // URIs in new specs is dictated by the URL spec, which states that unless
  // the URL parser is passed an override encoding, the charset to be used is
  // UTF-8. The new Notification icon/sound specification just says to use the
  // Fetch API, where the Request constructor defers to URL parsing specifying
  // the API base URL and no override encoding. So we've to use UTF-8 on
  // workers, but for backwards compat keeping it document charset on main
  // thread.
  auto encoding = UTF_8_ENCODING;

  if (nsCOMPtr<nsPIDOMWindowInner> window = aGlobal->GetAsInnerWindow()) {
    if (RefPtr<Document> doc = window->GetExtantDoc()) {
      baseUri = doc->GetBaseURI();
      encoding = doc->GetDocumentCharacterSet();
    } else {
      NS_WARNING("No document found for main thread notification!");
      return NS_ERROR_FAILURE;
    }
  } else if (WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate()) {
    baseUri = workerPrivate->GetBaseURI();
  }

  if (!baseUri) {
    return rv;
  }

  nsCOMPtr<nsIURI> srcUri;
  rv = NS_NewURI(getter_AddRefs(srcUri), aIconUrl, encoding, baseUri);
  if (NS_SUCCEEDED(rv)) {
    nsAutoCString src;
    srcUri->GetSpec(src);
    // XXX(krosylight): We should be able to pass UTF8 as-is, or ideally the URI object itself.
    CopyUTF8toUTF16(src, aDecodedUrl);
  }

  if (encoding == UTF_8_ENCODING) {
    return rv;
  }

  // If it was not UTF8, let's try UTF8 and see whether the result differs. If
  // no difference is found then we can just use UTF8 everywhere.
  // See: https://github.com/whatwg/notifications/issues/209
  glean::web_notification::IconUrlEncodingLabel label =
      glean::web_notification::IconUrlEncodingLabel::eNeitherWay;

  nsCOMPtr<nsIURI> srcUriUtf8;
  nsresult rvUtf8 =
      NS_NewURI(getter_AddRefs(srcUriUtf8), aIconUrl, UTF_8_ENCODING, baseUri);

  if (NS_SUCCEEDED(rv)) {
    if (NS_SUCCEEDED(rvUtf8)) {
      bool equals = false;
      if (NS_SUCCEEDED(srcUri->Equals(srcUriUtf8, &equals))) {
        if (equals) {
          // Okay to be parsed with UTF8
          label = glean::web_notification::IconUrlEncodingLabel::eUtf8;
        } else {
          // Can be parsed either way but with difference, unclear which one is
          // intended without fetching
          label = glean::web_notification::IconUrlEncodingLabel::eEitherWay;
        }
      }
    } else {
      label = glean::web_notification::IconUrlEncodingLabel::eDocumentCharset;
    }
  } else if (NS_SUCCEEDED(rvUtf8)) {
    // Can be only parsed with UTF8
    label = glean::web_notification::IconUrlEncodingLabel::eUtf8;
  }

  glean::web_notification::icon_url_encoding.EnumGet(label).Add();

  return rv;
}

JSObject* Notification::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  return mozilla::dom::Notification_Binding::Wrap(aCx, this, aGivenProto);
}

void Notification::Close() {
  if (mIsClosed) {
    return;
  }
  if (!mActor) {
    CreateActor();
  }
  if (mActor) {
    (void)mActor->SendClose();
  }
}

bool Notification::RequireInteraction() const {
  return mIPCNotification.options().requireInteraction();
}

bool Notification::Silent() const {
  return mIPCNotification.options().silent();
}

void Notification::GetVibrate(nsTArray<uint32_t>& aRetval) const {
  aRetval = mIPCNotification.options().vibrate().Clone();
}

void Notification::GetData(JSContext* aCx,
                           JS::MutableHandle<JS::Value> aRetval) {
  const nsString& dataSerialized = mIPCNotification.options().dataSerialized();
  if (mData.isNull() && !dataSerialized.IsEmpty()) {
    nsresult rv;
    RefPtr<nsStructuredCloneContainer> container =
        new nsStructuredCloneContainer();
    rv = container->InitFromBase64(dataSerialized, JS_STRUCTURED_CLONE_VERSION);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRetval.setNull();
      return;
    }

    JS::Rooted<JS::Value> data(aCx);
    rv = container->DeserializeToJsval(aCx, &data);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRetval.setNull();
      return;
    }

    if (data.isGCThing()) {
      mozilla::HoldJSObjects(this);
    }
    mData = data;
  }
  if (mData.isNull()) {
    aRetval.setNull();
    return;
  }

  aRetval.set(mData);
}

void Notification::GetActions(nsTArray<NotificationAction>& aRetVal) {
  aRetVal.Clear();
  for (const IPCNotificationAction& entry :
       mIPCNotification.options().actions()) {
    RootedDictionary<NotificationAction> action(RootingCx());
    action.mAction = entry.name();
    action.mTitle = entry.title();
    aRetVal.AppendElement(action);
  }
}

// Steps 2-5 of
// https://notifications.spec.whatwg.org/#dom-serviceworkerregistration-shownotification
/* static */
already_AddRefed<Promise> Notification::ShowPersistentNotification(
    JSContext* aCx, nsIGlobalObject* aGlobal, const nsAString& aScope,
    const nsAString& aTitle, const NotificationOptions& aOptions,
    const ServiceWorkerRegistrationDescriptor& aDescriptor, ErrorResult& aRv) {
  MOZ_ASSERT(aGlobal);

  // Step 2: Let promise be a new promise in this’s relevant Realm.
  RefPtr<Promise> p = Promise::Create(aGlobal, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  // Step 3: If this’s active worker is null, then reject promise with a
  // TypeError and return promise.
  if (!aDescriptor.GetActive()) {
    aRv.ThrowTypeError<MSG_NO_ACTIVE_WORKER>(NS_ConvertUTF16toUTF8(aScope));
    return nullptr;
  }

  // Step 4: Let notification be the result of creating a notification with a
  // settings object given title, options, and this’s relevant settings object.
  // If this threw an exception, then reject promise with that exception and
  // return promise.
  //
  // Step 5: Set notification’s service worker registration to this.
  //
  // Note: We currently use the scope as the unique identifier for the
  // registration (and there currently is no durable registration identifier,
  // so this is necessary), which is why we pass in the scope.  See
  // https://github.com/whatwg/notifications/issues/205 for some scope-related
  // discussion.
  //
  // XXX: We create Notification object almost solely to share the parameter
  // normalization steps. It would be nice to export that and skip creating
  // object here.
  RefPtr<Notification> notification =
      ValidateAndCreate(aCx, aGlobal, aTitle, aOptions, aScope, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  if (!notification->CreateActor() || !notification->SendShow(p)) {
    return nullptr;
  }

  return p.forget();
}

bool Notification::CreateActor() {
  mozilla::ipc::PBackgroundChild* backgroundActor =
      mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();

  // Note: We are not using the typical PBackground managed actor here as we
  // want the actor to be in the main thread of the main process. Instead we
  // pass the endpoint to PBackground, it dispatches a runnable to the main
  // thread, and the endpoint is bound there.

  mozilla::ipc::Endpoint<notification::PNotificationParent> parentEndpoint;
  mozilla::ipc::Endpoint<notification::PNotificationChild> childEndpoint;
  notification::PNotification::CreateEndpoints(&parentEndpoint, &childEndpoint);

  bool persistent = !mScope.IsEmpty();
  RefPtr<nsPIDOMWindowInner> window = GetOwnerWindow();
  mActor = new notification::NotificationChild(
      persistent ? nullptr : this,
      window ? window->GetWindowGlobalChild() : nullptr);

  nsISerialEventTarget* target = nullptr;
  nsIPrincipal* principal;
  nsIPrincipal* effectiveStoragePrincipal;
  bool isSecureContext;

  // TODO: Should get nsIGlobalObject methods for each method
  if (WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate()) {
    target = workerPrivate->HybridEventTarget();
    principal = workerPrivate->GetPrincipal();
    effectiveStoragePrincipal = workerPrivate->GetEffectiveStoragePrincipal();
    isSecureContext = workerPrivate->IsSecureContext();
  } else {
    nsGlobalWindowInner* win = GetOwnerWindow();
    NS_ENSURE_TRUE(win, false);
    principal = win->GetPrincipal();
    effectiveStoragePrincipal = win->GetEffectiveStoragePrincipal();
    isSecureContext = win->IsSecureContext();
  }

  if (!childEndpoint.Bind(mActor, target)) {
    return false;
  }

  (void)backgroundActor->SendCreateNotificationParent(
      std::move(parentEndpoint), WrapNotNull(principal),
      WrapNotNull(effectiveStoragePrincipal), isSecureContext, mScope,
      mIPCNotification);

  return true;
}

bool Notification::SendShow(Promise* aPromise) {
  mActor->SendShow()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr{this}, promise = RefPtr(aPromise)](
          notification::PNotificationChild::ShowPromise::ResolveOrRejectValue&&
              aResult) {
        if (aResult.IsReject()) {
          promise->MaybeRejectWithUnknownError("Failed to open notification");
          self->Deactivate();
          return;
        }

        CopyableErrorResult rv = aResult.ResolveValue();
        if (rv.Failed()) {
          promise->MaybeReject(std::move(rv));
          self->Deactivate();
          return;
        }

        if (promise) {
          promise->MaybeResolveWithUndefined();
        } else {
          self->DispatchTrustedEvent(u"show"_ns);
        }
      });

  return true;
}

void Notification::Deactivate() {
  IgnoreKeepAliveIfHasListenersFor(nsGkAtoms::onclick);
  IgnoreKeepAliveIfHasListenersFor(nsGkAtoms::onshow);
  IgnoreKeepAliveIfHasListenersFor(nsGkAtoms::onerror);
  IgnoreKeepAliveIfHasListenersFor(nsGkAtoms::onclose);
  mIsClosed = true;
  if (mActor) {
    mActor->Close();
    mActor = nullptr;
  }
}

nsresult Notification::DispatchToMainThread(
    already_AddRefed<nsIRunnable>&& aRunnable) {
  if (WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate()) {
    return workerPrivate->DispatchToMainThread(std::move(aRunnable));
  }
  AssertIsOnMainThread();
  return NS_DispatchToCurrentThread(std::move(aRunnable));
}

}  // namespace mozilla::dom
