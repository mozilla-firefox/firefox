/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsComponentManagerUtils.h"
#include "nsSystemAlertsService.h"
#include "nsAlertsIconListener.h"
#include "mozilla/Components.h"
#include "mozilla/WidgetUtilsGtk.h"

#ifdef MOZ_ENABLE_DBUS
#  include "rust/src/PortalAlertsService.h"
#endif

NS_IMPL_COMPONENT_FACTORY(nsIAlertsService) {
#ifdef MOZ_ENABLE_DBUS
  if (mozilla::widget::ShouldUsePortal(
          mozilla::widget::PortalKind::Notification)) {
    nsCOMPtr<nsIAlertsService> service;
    if (NS_SUCCEEDED(new_portal_alerts_service(NS_GET_IID(nsIAlertsService),
                                               getter_AddRefs(service)))) {
      return service.forget();
    }
    return nullptr;
  }

  RefPtr service = mozilla::MakeRefPtr<nsSystemAlertsService>();
  if (NS_FAILED(service->Init())) {
    return nullptr;
  }
  return service.forget().downcast<nsIAlertsService>();
#else
  // The libnotify backend requires D-Bus as well.
  return nullptr;
#endif
}

NS_IMPL_ADDREF(nsSystemAlertsService)
NS_IMPL_RELEASE(nsSystemAlertsService)

NS_INTERFACE_MAP_BEGIN(nsSystemAlertsService)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIAlertsService)
  NS_INTERFACE_MAP_ENTRY(nsIAlertsService)
  NS_INTERFACE_MAP_ENTRY(nsIAlertsDoNotDisturb)
NS_INTERFACE_MAP_END

nsSystemAlertsService::nsSystemAlertsService() = default;

nsSystemAlertsService::~nsSystemAlertsService() = default;

nsresult nsSystemAlertsService::Init() { return NS_OK; }

NS_IMETHODIMP nsSystemAlertsService::ShowAlert(nsIAlertNotification* aAlert,
                                               nsIObserver* aAlertListener) {
  return NS_ERROR_NOT_IMPLEMENTED;  // Implemented in nsAlertsService
}

NS_IMETHODIMP nsSystemAlertsService::ShowAlertWithCallbacks(
    nsIAlertNotification* aAlert, nsIAlertCallbacks* aAlertCallbacks) {
  NS_ENSURE_ARG(aAlert);

  nsAutoString alertName;
  nsresult rv = aAlert->GetName(alertName);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<nsAlertsIconListener> alertListener =
      new nsAlertsIconListener(this, aAlert, alertName);
  if (!alertListener) return NS_ERROR_OUT_OF_MEMORY;

  if (mSuppressForScreenSharing) {
    alertListener->SendClosed();
    return NS_OK;
  }

  AddListener(alertName, alertListener);
  return alertListener->InitAlert(aAlert, aAlertCallbacks);
}

NS_IMETHODIMP nsSystemAlertsService::CloseAlert(const nsAString& aAlertName,
                                                bool aContextClosed) {
  RefPtr<nsAlertsIconListener> listener = mActiveListeners.Get(aAlertName);
  if (!listener) {
    return NS_OK;
  }
  mActiveListeners.Remove(aAlertName);
  return listener->Close();
}

NS_IMETHODIMP nsSystemAlertsService::GetHistory(nsTArray<nsString>& aResult) {
  // Neither org.freedesktop.Notifications nor
  // org.freedesktop.portal.Notification supports getting the previous
  // notifications
  // https://specifications.freedesktop.org/notification-spec/latest/protocol.html
  // https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Notification.html
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP nsSystemAlertsService::GetManualDoNotDisturb(bool* aRetVal) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsSystemAlertsService::SetManualDoNotDisturb(bool aDoNotDisturb) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsSystemAlertsService::GetSuppressForScreenSharing(
    bool* aRetVal) {
  NS_ENSURE_ARG(aRetVal);
  *aRetVal = mSuppressForScreenSharing;
  return NS_OK;
}

NS_IMETHODIMP nsSystemAlertsService::SetSuppressForScreenSharing(
    bool aSuppress) {
  mSuppressForScreenSharing = aSuppress;
  return NS_OK;
}

NS_IMETHODIMP nsSystemAlertsService::Teardown() {
  for (auto iter = mActiveListeners.Iter(); !iter.Done(); iter.Next()) {
    RefPtr<nsAlertsIconListener> listener = iter.Data().forget();
    iter.Remove();
    listener->Disconnect();
  }
  return NS_OK;
}

NS_IMETHODIMP nsSystemAlertsService::PbmTeardown() {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsSystemAlertsService::IsFullscreen(bool* aRetVal) {
  *aRetVal = false;
  return NS_ERROR_NOT_IMPLEMENTED;
}

bool nsSystemAlertsService::IsActiveListener(const nsAString& aAlertName,
                                             nsAlertsIconListener* aListener) {
  return mActiveListeners.Get(aAlertName) == aListener;
}

void nsSystemAlertsService::AddListener(const nsAString& aAlertName,
                                        nsAlertsIconListener* aListener) {
  const auto oldListener =
      mActiveListeners.WithEntryHandle(aAlertName, [&](auto&& entry) {
        RefPtr<nsAlertsIconListener> oldListener =
            entry ? entry.Data() : nullptr;
        entry.InsertOrUpdate(aListener);
        return oldListener;
      });
  if (oldListener) {
    // If an alert with this name already exists, close it.
    oldListener->Close();
  }
}

void nsSystemAlertsService::RemoveListener(const nsAString& aAlertName,
                                           nsAlertsIconListener* aListener) {
  auto entry = mActiveListeners.Lookup(aAlertName);
  if (entry && entry.Data() == aListener) {
    // The alert may have been replaced; only remove it from the active
    // listeners map if it's the same.
    entry.Remove();
  }
}
