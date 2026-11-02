/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_CookieStoreNotificationWatcher_h
#define mozilla_dom_CookieStoreNotificationWatcher_h

#include "mozilla/MoveOnlyFunction.h"
#include "mozilla/OriginAttributes.h"
#include "nsIObserver.h"
#include "nsWeakReference.h"

namespace mozilla::dom {

class CookieStoreNotificationWatcher final : public nsIObserver,
                                             public nsSupportsWeakReference {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  static already_AddRefed<CookieStoreNotificationWatcher> Create(
      bool aPrivateBrowsing);

  static void ReleaseOnMainThread(
      already_AddRefed<CookieStoreNotificationWatcher> aWatcher);

  void CallbackWhenNotified(const nsID& aOperationID,
                            MoveOnlyFunction<void()> aCallback);
  void ForgetOperationID(const nsID& aOperationID);

 private:
  CookieStoreNotificationWatcher() = default;
  ~CookieStoreNotificationWatcher() = default;

  void MaybeResolveOperations(const nsID* aOperationID);

  struct PendingOperation {
    MoveOnlyFunction<void()> mCallback;
    nsID mOperationID;
  };

  // This is a simple list because I don't think we will have so many concurrent
  // operations to motivate an hash table.
  nsTArray<PendingOperation> mPendingOperations;
};

}  // namespace mozilla::dom

#endif /* mozilla_dom_CookieStoreNotificationWatcher_h */
