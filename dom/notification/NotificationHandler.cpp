/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NotificationHandler.h"

#include "NotificationUtils.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/dom/ClientIPCTypes.h"
#include "mozilla/dom/ClientOpenWindowUtils.h"
#include "mozilla/dom/DOMTypes.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ServiceWorkerManager.h"
#include "mozilla/dom/ServiceWorkerUtils.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "nsNetUtil.h"
#include "xpcprivate.h"

namespace mozilla::dom::notification {

nsresult RespondOnClick(nsIPrincipal* aPrincipal, const nsAString& aScope,
                        const IPCNotification& aNotification,
                        const nsAString& aActionName) {
  if (StaticPrefs::dom_webnotifications_navigate_enabled()) {
    nsIURI* navigate = nullptr;
    if (aActionName.IsEmpty()) {
      navigate = aNotification.options().navigate();
    } else {
      for (const IPCNotificationAction& action :
           aNotification.options().actions()) {
        if (action.name() == aActionName) {
          navigate = action.navigate();
          break;
        }
      }
    }
    if (navigate) {
      return OpenWindowFor(aPrincipal, navigate);
    }
  }
  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  if (!swm) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString originSuffix;
  MOZ_TRY(aPrincipal->GetOriginSuffix(originSuffix));

  nsresult rv = swm->SendNotificationClickEvent(originSuffix, aScope,
                                                aNotification, aActionName);
  if (NS_FAILED(rv)) {
    // No active service worker, let's do the last resort
    // TODO(krosylight): We should prevent entering this path as much as
    // possible and ultimately remove this. See bug 1972120.
    return OpenWindowFor(aPrincipal);
  }
  return NS_OK;
}

nsresult OpenWindowFor(nsIPrincipal* aPrincipal, nsIURI* aURI) {
  if (!aPrincipal->GetIsOriginPotentiallyTrustworthy()) {
    // We expect only secure context origins for web notifications.
    return NS_ERROR_INVALID_ARG;
  }

  // XXX: We should be able to just pass nsIPrincipal directly
  mozilla::ipc::PrincipalInfo info{};
  MOZ_TRY(PrincipalToPrincipalInfo(aPrincipal, &info));

  nsAutoCString origin;
  MOZ_TRY(aPrincipal->GetOriginNoSuffix(origin));
  nsCOMPtr<nsIURI> baseURI;
  MOZ_TRY(NS_NewURI(getter_AddRefs(baseURI), origin));
  nsCOMPtr<nsIURI> uri = aURI ? aURI : baseURI.get();

  NS_DispatchToCurrentThread(NS_NewRunnableFunction(
      "OpenWindowFor",
      [info = std::move(info), uri = std::move(uri),
       baseURI = std::move(baseURI)]() MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
        (void)ClientOpenWindow(
            nullptr,
            ClientOpenWindowArgs(info, Nothing(), WrapNotNull(uri.get()),
                                 WrapNotNull(baseURI.get())));
      }));
  return NS_OK;
}

NS_IMPL_ISUPPORTS(NotificationHandler, nsINotificationHandler);

StaticRefPtr<NotificationHandler> sHandler;

already_AddRefed<NotificationHandler> NotificationHandler::GetSingleton() {
  if (!sHandler) {
    sHandler = new NotificationHandler();
    ClearOnShutdown(&sHandler);
  }

  return do_AddRef(sHandler);
}

struct NotificationActionComparator {
  bool Equals(const IPCNotificationAction& aAction,
              const nsAString& aActionName) const {
    return aAction.name() == aActionName;
  }
};

NS_IMETHODIMP NotificationHandler::RespondOnClick(
    nsIPrincipal* aPrincipal, const nsAString& aNotificationId,
    const nsAString& aActionName, bool aAutoClosed, Promise** aResult) {
  if (aPrincipal->IsSystemPrincipal()) {
    // This function is only designed for web notifications.
    return NS_ERROR_INVALID_ARG;
  }

  nsAutoCString origin;
  MOZ_TRY(aPrincipal->GetOrigin(origin));

  bool isPrivate = aPrincipal->GetIsInPrivateBrowsing();
  nsCOMPtr<nsINotificationStorage> storage = GetNotificationStorage(isPrivate);

  RefPtr<Promise> promise;
  storage->GetById(origin, aNotificationId, getter_AddRefs(promise));

  if (aAutoClosed) {
    // The system already closed the notification, let's purge the entry here.
    //
    // It is guaranteed that Delete will happen only immediately after GetById
    // as NotificationDB manages each request with an internal job queue.
    //
    // XXX(krosylight): We should use AUTF8String for all NotificationStorage
    // methods.
    storage->Delete(NS_ConvertUTF8toUTF16(origin), aNotificationId);
  }

  RefPtr<Promise> result = MOZ_TRY(promise->ThenWithoutCycleCollection(
      [actionName = nsString(aActionName), principal = nsCOMPtr(aPrincipal)](
          JSContext* aCx, JS::Handle<JS::Value> aValue,
          ErrorResult& aRv) mutable -> already_AddRefed<Promise> {
        auto tryable = [&]() -> nsresult {
          if (aValue.isUndefined()) {
            // No storage entry, open a new window as a fallback
            return OpenWindowFor(principal);
          }

          MOZ_ASSERT(aValue.isObject());
          JSObject* obj = &aValue.toObject();

          nsCOMPtr<nsINotificationStorageEntry> entry;
          MOZ_TRY(nsXPConnect::XPConnect()->WrapJS(
              aCx, obj, NS_GET_IID(nsINotificationStorageEntry),
              getter_AddRefs(entry)));
          if (!entry) {
            return NS_ERROR_FAILURE;
          }

          nsAutoString scope;
          MOZ_TRY(entry->GetServiceWorkerRegistrationScope(scope));

          nsCOMPtr<nsIURI> scopeURI;
          MOZ_TRY(NS_NewURI(getter_AddRefs(scopeURI), scope));

          ServiceWorkerScopeIsValid(principal, scopeURI, aRv);
          if (aRv.Failed()) {
            return NS_OK;
          }

          IPCNotification notification =
              MOZ_TRY(NotificationStorageEntry::ToIPC(*entry));

          if (!actionName.IsEmpty()) {
            bool contains = notification.options().actions().Contains(
                actionName, NotificationActionComparator());
            if (!contains) {
              // Invalid action, so pretend it had no action
              actionName.Truncate();
            }
          }

          return notification::RespondOnClick(principal, scope, notification,
                                              actionName);
        };

        nsresult rv = tryable();
        if (NS_FAILED(rv)) {
          aRv.Throw(rv);
          return nullptr;
        }

        return nullptr;
      }));

  if (aResult) {
    result.forget(aResult);
  }

  return NS_OK;
}

}  // namespace mozilla::dom::notification
