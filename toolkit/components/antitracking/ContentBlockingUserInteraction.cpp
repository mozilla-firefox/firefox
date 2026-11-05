/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AntiTrackingLog.h"
#include "ContentBlockingUserInteraction.h"
#include "AntiTrackingUtils.h"

#include "mozilla/BounceTrackingProtection.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/PermissionManager.h"
#include "nsIPrincipal.h"
#include "nsXULAppAPI.h"
#include "prtime.h"

namespace mozilla {

/* static */
void ContentBlockingUserInteraction::Observe(nsIPrincipal* aPrincipal) {
  if (!aPrincipal || aPrincipal->IsSystemPrincipal()) {
    // The content process may have sent us garbage data.
    return;
  }

  if (XRE_IsParentProcess()) {
    LOG_PRIN(("Saving the userInteraction for %s", _spec), aPrincipal);

    RefPtr<PermissionManager> permManager = PermissionManager::GetInstance();
    if (NS_WARN_IF(!permManager)) {
      LOG(("Permission manager is null, bailing out early"));
      return;
    }

    // Remember that this pref is stored in seconds!
    uint32_t expirationType = nsIPermissionManager::EXPIRE_TIME;
    uint32_t expirationTime =
        StaticPrefs::privacy_userInteraction_expiration() * 1000;
    int64_t when = (PR_Now() / PR_USEC_PER_MSEC) + expirationTime;

    uint32_t privateBrowsingId = 0;
    nsresult rv = aPrincipal->GetPrivateBrowsingId(&privateBrowsingId);
    if (!NS_WARN_IF(NS_FAILED(rv)) && privateBrowsingId > 0) {
      // If we are coming from a private window, make sure to store a
      // session-only permission which won't get persisted to disk.
      expirationType = nsIPermissionManager::EXPIRE_SESSION;
      when = 0;
    }

    rv = permManager->AddFromPrincipal(aPrincipal, USER_INTERACTION_PERM,
                                       nsIPermissionManager::ALLOW_ACTION,
                                       expirationType, when);
    (void)NS_WARN_IF(NS_FAILED(rv));

    if (StaticPrefs::privacy_antitracking_testing()) {
      nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
      obs->NotifyObservers(
          nullptr, "antitracking-test-user-interaction-perm-added", nullptr);
    }
    return;
  }

  dom::ContentChild* cc = dom::ContentChild::GetSingleton();
  MOZ_ASSERT(cc);

  LOG_PRIN(("Asking the parent process to save the user-interaction for us: %s",
            _spec),
           aPrincipal);
  cc->SendStoreUserInteractionAsPermission(WrapNotNull(aPrincipal));
}

/* static */
bool ContentBlockingUserInteraction::Exists(nsIPrincipal* aPrincipal) {
  RefPtr<PermissionManager> permManager = PermissionManager::GetInstance();
  if (NS_WARN_IF(!permManager)) {
    return false;
  }

  uint32_t result = 0;
  nsresult rv = permManager->TestPermissionWithoutDefaultsFromPrincipal(
      aPrincipal, USER_INTERACTION_PERM, &result);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  return result == nsIPermissionManager::ALLOW_ACTION;
}

}  // namespace mozilla
