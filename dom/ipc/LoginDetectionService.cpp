/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LoginDetectionService.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/StaticPrefs_fission.h"
#include "mozilla/dom/ProcessIsolation.h"
#include "nsILoginInfo.h"
#include "nsILoginManager.h"
#include "nsIObserver.h"
#include "nsIPrincipal.h"
#include "nsIXULRuntime.h"
#include "nsServiceManagerUtils.h"
#include "nsXULAppAPI.h"

namespace mozilla::dom {

static StaticRefPtr<LoginDetectionService> gLoginDetectionService;

namespace {

void OnFissionPrefsChange(const char* aPrefName, void* aData) {
  MOZ_ASSERT(gLoginDetectionService);

  gLoginDetectionService->MaybeStartMonitoring();
}

}  // namespace

NS_IMPL_ISUPPORTS(LoginDetectionService, nsILoginDetectionService,
                  nsILoginSearchCallback, nsIObserver, nsISupportsWeakReference)

// static
already_AddRefed<LoginDetectionService> LoginDetectionService::GetSingleton() {
  if (gLoginDetectionService) {
    return do_AddRef(gLoginDetectionService);
  }

  gLoginDetectionService = new LoginDetectionService();
  ClearOnShutdown(&gLoginDetectionService);

  return do_AddRef(gLoginDetectionService);
}

LoginDetectionService::LoginDetectionService() : mIsLoginsLoaded(false) {}
LoginDetectionService::~LoginDetectionService() { UnregisterObserver(); }

void LoginDetectionService::MaybeStartMonitoring() {
  if (IsIsolateHighValueSiteEnabled()) {
    // We want to isolate sites with a saved password, so fetch saved logins
    // from the password manager, and then add the 'HighValue' permission.

    // Note that we don't monitor whether a login is added or removed after
    // logins are fetched. For adding logins, this will be covered by form
    // submission detection heuristic. As for removing logins, it doesn't
    // provide security benefit just to NOT isolate the removed site. The site
    // will not be isolated when its permission expired.
    FetchLogins();
  }

  if (IsIsolateHighValueSiteEnabled() ||
      StaticPrefs::fission_highValue_login_monitor()) {
    // When the pref is on, we monitor users' login attempt event when we
    // are not isolating high value sites. This is because We can't detect the
    // case where a user is already logged in to a site, and don't save a
    // password for it. So we want to start monitoring login attempts prior
    // to releasing the feature.
    if (!mObs) {
      mObs = mozilla::services::GetObserverService();
      mObs->AddObserver(this, "passwordmgr-form-submission-detected", false);
    }
  } else {
    UnregisterObserver();
  }
}

void LoginDetectionService::FetchLogins() {
  nsresult rv;
  nsCOMPtr<nsILoginManager> loginManager =
      do_GetService(NS_LOGINMANAGER_CONTRACTID, &rv);
  if (NS_WARN_IF(!loginManager)) {
    return;
  }

  (void)loginManager->GetAllLoginsWithCallback(this);
}

void LoginDetectionService::UnregisterObserver() {
  if (mObs) {
    mObs->RemoveObserver(this, "passwordmgr-form-submission-detected");
    mObs = nullptr;
  }
}

///////////////////////////////////////////////////////////////////////////////
// nsILoginDetectionService implementation
NS_IMETHODIMP LoginDetectionService::Init() {
  if (XRE_IsContentProcess()) {
    return NS_OK;
  }

  Preferences::RegisterCallback(OnFissionPrefsChange, "fission.autostart");
  Preferences::RegisterCallback(OnFissionPrefsChange,
                                "fission.webContentIsolationStrategy");

  MaybeStartMonitoring();

  return NS_OK;
}

NS_IMETHODIMP LoginDetectionService::IsLoginsLoaded(bool* aResult) {
  if (IsIsolateHighValueSiteEnabled()) {
    *aResult = mIsLoginsLoaded;
  } else {
    // When the feature is disabled, just returns true so testcases don't
    // block on waiting for us to load logins.
    *aResult = true;
  }
  return NS_OK;
}

///////////////////////////////////////////////////////////////////////////////
// nsILoginSearchObserver implementation
NS_IMETHODIMP
LoginDetectionService::OnSearchComplete(
    const nsTArray<RefPtr<nsILoginInfo>>& aLogins) {
  // Add all origins with saved passwords to the permission manager.
  for (const auto& login : aLogins) {
    nsString origin;
    login->GetOrigin(origin);

    AddHighValuePermission(NS_ConvertUTF16toUTF8(origin),
                           mozilla::dom::kHighValueHasSavedLoginPermission);
  }

  mIsLoginsLoaded = true;
  return NS_OK;
}

///////////////////////////////////////////////////////////////////////////////
// nsIObserver implementation
NS_IMETHODIMP
LoginDetectionService::Observe(nsISupports* aSubject, const char* aTopic,
                               const char16_t* aData) {
  if ("passwordmgr-form-submission-detected"_ns.Equals(aTopic)) {
    if (nsCOMPtr<nsIPrincipal> principal = do_QueryInterface(aSubject)) {
      AddHighValuePermission(principal,
                             mozilla::dom::kHighValueIsLoggedInPermission);
    }
  }

  return NS_OK;
}

}  // namespace mozilla::dom
