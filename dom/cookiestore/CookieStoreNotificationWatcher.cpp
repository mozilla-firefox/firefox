/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CookieStoreNotificationWatcher.h"

#include "mozilla/Services.h"
#include "mozilla/UniquePtrExtensions.h"
#include "nsICookie.h"
#include "nsICookieNotification.h"
#include "nsIObserverService.h"

namespace mozilla::dom {

NS_IMPL_ISUPPORTS(CookieStoreNotificationWatcher, nsIObserver,
                  nsISupportsWeakReference)

// static
already_AddRefed<CookieStoreNotificationWatcher>
CookieStoreNotificationWatcher::Create(bool aPrivateBrowsing) {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<CookieStoreNotificationWatcher> watcher =
      new CookieStoreNotificationWatcher();

  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (NS_WARN_IF(!os)) {
    return nullptr;
  }

  nsresult rv = os->AddObserver(
      watcher, aPrivateBrowsing ? "private-cookie-changed" : "cookie-changed",
      true);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  return watcher.forget();
}

NS_IMETHODIMP
CookieStoreNotificationWatcher::Observe(nsISupports* aSubject,
                                        const char* aTopic,
                                        const char16_t* aData) {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsICookieNotification> notification = do_QueryInterface(aSubject);
  NS_ENSURE_TRUE(notification, NS_ERROR_FAILURE);

  UniqueFreePtr<nsID> operationID;
  nsresult rv = notification->GetOperationID(TempPtrToSetter(&operationID));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_OK;
  }

  if (!operationID) {
    return NS_OK;
  }

  for (uint32_t i = 0; i < mPendingOperations.Length(); ++i) {
    PendingOperation& pendingOperation = mPendingOperations[i];
    if (pendingOperation.mOperationID.Equals(*operationID)) {
      pendingOperation.mCallback();
      mPendingOperations.RemoveElementAt(i);
      break;
    }
  }

  return NS_OK;
}

void CookieStoreNotificationWatcher::CallbackWhenNotified(
    const nsID& aOperationID, MoveOnlyFunction<void()> aCallback) {
  MOZ_ASSERT(NS_IsMainThread());

  mPendingOperations.AppendElement(
      PendingOperation{std::move(aCallback), aOperationID});
}

void CookieStoreNotificationWatcher::ForgetOperationID(
    const nsID& aOperationID) {
  MOZ_ASSERT(NS_IsMainThread());

  for (uint32_t i = 0; i < mPendingOperations.Length(); ++i) {
    PendingOperation& pendingOperation = mPendingOperations[i];
    if (pendingOperation.mOperationID.Equals(aOperationID)) {
      mPendingOperations.RemoveElementAt(i);
      return;
    }
  }
}

// static
void CookieStoreNotificationWatcher::ReleaseOnMainThread(
    already_AddRefed<CookieStoreNotificationWatcher> aWatcher) {
  RefPtr<CookieStoreNotificationWatcher> watcher(aWatcher);

  if (!watcher || NS_IsMainThread()) {
    return;
  }

  class ReleaseWatcher final : public Runnable {
   public:
    explicit ReleaseWatcher(
        already_AddRefed<CookieStoreNotificationWatcher> aWatcher)
        : Runnable("ReleaseWatcher"), mDoomed(std::move(aWatcher)) {}

    NS_IMETHOD Run() override {
      mDoomed = nullptr;
      return NS_OK;
    }

   private:
    ~ReleaseWatcher() {
      // If we still have to release the watcher, better to leak it.
      if (mDoomed) {
        mDoomed.forget().leak();
      }
    }

    RefPtr<CookieStoreNotificationWatcher> mDoomed;
  };

  RefPtr<ReleaseWatcher> runnable(new ReleaseWatcher(watcher.forget()));
  NS_DispatchToMainThread(runnable);
}

}  // namespace mozilla::dom
