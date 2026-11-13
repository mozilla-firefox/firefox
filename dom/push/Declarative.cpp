/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Declarative.h"

#include "js/JSON.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/dom/dom_push_rust_generated.h"
#include "mozilla/dom/notification/NotificationUtils.h"
#include "mozilla/glean/DomPushMetrics.h"
#include "mozilla/image/FetchDecodedImage.h"
#include "nsNetUtil.h"
#include "nsURLHelper.h"

namespace mozilla::dom {

using notification::NotificationCallbacksCommon;

class DWPNotificationCallbacks final : public NotificationCallbacksCommon {
 public:
  NS_INLINE_DECL_REFCOUNTING_INHERITED(DWPNotificationCallbacks,
                                       NotificationCallbacksCommon)

  DWPNotificationCallbacks(const nsAString& aScope, nsIPrincipal* aPrincipal,
                           IPCNotification aNotification)
      : NotificationCallbacksCommon(aScope, aPrincipal,
                                    std::move(aNotification)) {}

  RefPtr<DeclarativePushPromise> Promise() { return mPromise.Ensure(__func__); }

  NS_IMETHODIMP OnAlertShow() override {
    MOZ_ASSERT(!mPromise.IsEmpty());
    mPromise.Resolve(Ok(), __func__);
    MOZ_TRY(NotificationCallbacksCommon::OnAlertShow());
    PersistNotification();
    return NS_OK;
  }

  NS_IMETHODIMP OnAlertClick(
      nsIAlertAction* aAction,
      nsIAlertCallbacks::ApplicationState aState) override {
    MOZ_TRY(NotificationCallbacksCommon::OnAlertClick(aAction, aState));
    return RespondOnClick(aAction);
  }

  NS_IMETHODIMP OnAlertClosed() override {
    if (!mPromise.IsEmpty()) {
      mPromise.Resolve(Ok(), __func__);
    }
    MOZ_TRY(NotificationCallbacksCommon::OnAlertClosed());
    UnpersistNotification();
    return NS_OK;
  }

  NS_IMETHODIMP OnAlertFinished() override {
    if (!mPromise.IsEmpty()) {
      mPromise.Reject(NS_ERROR_FAILURE, __func__);
    }
    MOZ_TRY(NotificationCallbacksCommon::OnAlertFinished());
    UnpersistNotification();
    return NS_OK;
  }

 private:
  MozPromiseHolder<DeclarativePushPromise> mPromise;
  virtual ~DWPNotificationCallbacks() = default;
};

static void RecordTelemetry(const DeclarativePushData& aData) {
  glean::web_push::declarative.Add();
  if (aData.mutable_) {
    glean::web_push::declarative_mutable.Add();
  }
}

static NotificationDirection ConvertNotificationDirection(
    DeclarativePushDir aDir) {
  switch (aDir) {
    case DeclarativePushDir::Ltr:
      return NotificationDirection::Ltr;
    case DeclarativePushDir::Rtl:
      return NotificationDirection::Rtl;
    case DeclarativePushDir::Auto:
      return NotificationDirection::Auto;
  }
  MOZ_CRASH("Invalid DeclarativePushDir.");
  return NotificationDirection::Auto;
}

// Convert JSON to the base 64 format outputted by nsStructuredCloneContainer.
static nsString ConvertJSONToStructuredCloneBase64(const nsAString& aJSON) {
  if (aJSON.IsEmpty()) {
    return EmptyString();
  }
  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(xpc::PrivilegedJunkScope()))) {
    return EmptyString();
  }
  JSContext* cx = jsapi.cx();
  RefPtr<nsStructuredCloneContainer> dataObjectContainer =
      new nsStructuredCloneContainer();
  JS::Rooted<JS::Value> jsValue(cx);
  if (NS_WARN_IF(!JS_ParseJSON(cx, aJSON.Data(), aJSON.Length(), &jsValue))) {
    return EmptyString();
  }
  if (NS_WARN_IF(NS_FAILED(dataObjectContainer->InitFromJSVal(jsValue, cx)))) {
    return EmptyString();
  }
  nsString serialized;
  if (NS_WARN_IF(NS_FAILED(dataObjectContainer->GetDataAsBase64(serialized)))) {
    return EmptyString();
  }
  return serialized;
}

static Result<IPCNotificationOptions, nsresult>
GetNotificationOptionsForDeclarativePush(DeclarativePushData&& aPush) {
  IPCNotificationOptions options;
  MOZ_TRY(NS_NewURI(getter_AddRefs(options.navigate()), aPush.navigate));
  options.title() = std::move(aPush.title);
  options.body() = std::move(aPush.body);
  options.dir() = ConvertNotificationDirection(aPush.dir);
  options.silent() = aPush.silent;
  if (StaticPrefs::dom_webnotifications_requireinteraction_enabled()) {
    options.requireInteraction() = aPush.require_interaction;
  }
  options.tag() = std::move(aPush.tag);
  options.lang() = std::move(aPush.lang);
  options.dataSerialized() = ConvertJSONToStructuredCloneBase64(aPush.data);
  for (DeclarativePushAction& action : aPush.actions) {
    IPCNotificationAction ipcAction;
    MOZ_TRY(NS_NewURI(getter_AddRefs(ipcAction.navigate()), action.navigate));
    // We still need to do the check above, even if there are more
    // than kMaxActions actions. So we can't break out of the loop.
    if (options.actions().Length() < notification::kMaxActions) {
      ipcAction.title() = std::move(action.title);
      ipcAction.name() = std::move(action.action);
      options.actions().AppendElement(std::move(ipcAction));
    }
  }
  if (!aPush.icon.IsEmpty()) {
    MOZ_TRY(NS_NewURI(getter_AddRefs(options.icon()), aPush.icon));
  }
  return std::move(options);
}

static RefPtr<DeclarativePushPromise> ShowNotification(
    const nsAString& aScope, const IPCNotificationOptions& aOptions,
    nsIPrincipal& aPrincipal, imgIContainer* aIcon) {
  auto result =
      notification::CreateAlertForNotification(aOptions, aPrincipal, aIcon);
  if (result.isErr()) {
    return DeclarativePushPromise::CreateAndReject(result.unwrapErr(),
                                                   __func__);
  }
  nsCOMPtr<nsIAlertNotification> notification = result.unwrap();
  nsAutoString id;
  nsresult rv = notification->GetId(id);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return DeclarativePushPromise::CreateAndReject(rv, __func__);
  }
  RefPtr<DWPNotificationCallbacks> dwpCallbacks = new DWPNotificationCallbacks(
      aScope, &aPrincipal, IPCNotification(id, aOptions));
  RefPtr promise = dwpCallbacks->Promise();
  rv = notification::ShowAlertWithCleanup(notification, dwpCallbacks);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return DeclarativePushPromise::CreateAndReject(rv, __func__);
  }
  return promise;
}

static RefPtr<DeclarativePushPromise> FetchIconAndShowNotification(
    const nsAString& aScope, IPCNotificationOptions&& aOptions,
    nsIPrincipal& aPrincipal) {
  nsIURI* iconURI = aOptions.icon();
  if (!iconURI) {
    return ShowNotification(aScope, aOptions, aPrincipal, nullptr);
  }
  nsAutoCString iconURL;
  nsresult rv = iconURI->GetSpec(iconURL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return DeclarativePushPromise::CreateAndReject(rv, __func__);
  }
  URLParams urlParams;
  urlParams.Set("url"_ns, iconURL);
  nsAutoCString principalJSON;
  rv = BasePrincipal::Cast(aPrincipal).ToJSON(principalJSON);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return DeclarativePushPromise::CreateAndReject(rv, __func__);
  }
  urlParams.Set("principal"_ns, principalJSON);
  // Fetch icon image through moz-remote-image protocol to avoid
  // decoding the image in the parent process.
  nsAutoCString remoteImageURL("moz-remote-image://?"_ns);
  nsAutoCString paramsString;
  urlParams.Serialize(paramsString, true);
  remoteImageURL.Append(paramsString);
  nsCOMPtr<nsIURI> remoteImageURI;
  rv = NS_NewURI(getter_AddRefs(remoteImageURI), remoteImageURL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return DeclarativePushPromise::CreateAndReject(rv, __func__);
  }
  return image::FetchDecodedImage(remoteImageURI, gfx::IntSize(),
                                  nsContentUtils::GetSystemPrincipal())
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [scope = nsString(aScope), options = std::move(aOptions),
           principal = RefPtr(&aPrincipal)](
              image::FetchDecodedImagePromise::ResolveOrRejectValue&& aResult) {
            nsCOMPtr<imgIContainer> icon;
            if (aResult.IsResolve()) {
              icon = std::move(aResult.ResolveValue());
            }
            return ShowNotification(scope, options, *principal, icon);
          });
}

RefPtr<DeclarativePushPromise> ParseDeclarativePushAndShowNotification(
    Span<const uint8_t> aData, nsIPrincipal* aPrincipal,
    const nsACString& aScope) {
  DeclarativePushData declarativePush;
  if (!parse_declarative_push(aData.Elements(), aData.Length(), &aScope,
                              &declarativePush)) {
    // Not a declarative push message.
    return nullptr;
  }
  RecordTelemetry(declarativePush);
  Result options =
      GetNotificationOptionsForDeclarativePush(std::move(declarativePush));
  if (NS_WARN_IF(options.isErr())) {
    return DeclarativePushPromise::CreateAndReject(options.unwrapErr(),
                                                   __func__);
  }
  RefPtr permissionPromise = notification::EnsureValidNotificationPermission(
      aPrincipal, aPrincipal, aPrincipal->GetIsOriginPotentiallyTrustworthy());
  return permissionPromise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [options = options.unwrap(), scope = NS_ConvertUTF8toUTF16(aScope),
       principal = RefPtr(aPrincipal)](
          const notification::NotificationPermissionPromise::
              ResolveOrRejectValue& aResult) mutable {
        if (aResult.IsReject()) {
          // Don't have permission
          return DeclarativePushPromise::CreateAndReject(aResult.RejectValue(),
                                                         __func__);
        }
        return FetchIconAndShowNotification(scope, std::move(options),
                                            *principal);
      });
}

}  // namespace mozilla::dom
