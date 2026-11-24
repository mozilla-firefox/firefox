/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_SharedWorkerManager_h
#define mozilla_dom_SharedWorkerManager_h

#include "SharedWorkerParent.h"
#include "mozilla/dom/RemoteWorkerController.h"
#include "mozilla/dom/quota/CheckedUnsafePtr.h"
#include "nsISupportsImpl.h"
#include "nsTArray.h"

class nsIPrincipal;

namespace mozilla::dom {

class UniqueMessagePortId;
class RemoteWorkerData;
class SharedWorkerManager;
class SharedWorkerService;

// Main-thread only object that keeps a manager and the service alive.
// When the last SharedWorkerManagerHolder is released, the corresponding
// manager unregisters itself from the service and terminates the worker.
class SharedWorkerManagerHolder final
    : public SupportsCheckedUnsafePtr<CheckIf<DiagnosticAssertEnabled>> {
 public:
  NS_INLINE_DECL_REFCOUNTING(SharedWorkerManagerHolder);

  SharedWorkerManagerHolder(SharedWorkerManager* aManager,
                            SharedWorkerService* aService);

  SharedWorkerManager* Manager() const { return mManager; }

  SharedWorkerService* Service() const { return mService; }

 private:
  ~SharedWorkerManagerHolder();

  const RefPtr<SharedWorkerManager> mManager;
  const RefPtr<SharedWorkerService> mService;
};

// Thread-safe wrapper for SharedWorkerManagerHolder.
class SharedWorkerManagerWrapper final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(SharedWorkerManagerWrapper);

  explicit SharedWorkerManagerWrapper(
      already_AddRefed<SharedWorkerManagerHolder> aHolder);

  SharedWorkerManager* Manager() const { return mHolder->Manager(); }

 private:
  ~SharedWorkerManagerWrapper();

  RefPtr<SharedWorkerManagerHolder> mHolder;
};

/**
 * PBackground instance that corresponds to a single logical Shared Worker that
 * exists somewhere in the process tree. Referenced/owned by multiple
 * SharedWorkerParent instances on the PBackground thread. Holds/owns a single
 * RemoteWorkerController to interact with the actual shared worker thread,
 * wherever it is located. Creates the RemoteWorkerController via
 * RemoteWorkerController::Create which uses RemoteWorkerManager::Launch under
 * the hood.
 */
class SharedWorkerManager final : public RemoteWorkerObserver {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(SharedWorkerManager, override);

  // Called on main-thread thread methods

  static already_AddRefed<SharedWorkerManagerHolder> Create(
      SharedWorkerService* aService, nsIEventTarget* aPBackgroundEventTarget,
      const RemoteWorkerData& aData, nsIPrincipal* aLoadingPrincipal,
      const OriginAttributes& aEffectiveStoragePrincipalAttrs);

  // Returns a holder if this manager matches. The holder blocks the shutdown of
  // the manager.
  already_AddRefed<SharedWorkerManagerHolder> MatchOnMainThread(
      SharedWorkerService* aService, const RemoteWorkerData& aData,
      nsIURI* aScriptURL, nsIPrincipal* aLoadingPrincipal,
      const OriginAttributes& aEffectiveStoragePrincipalAttrs,
      bool* aMatchNameButNotOptions);

  // RemoteWorkerObserver

  void CreationFailed() override;

  void CreationSucceeded() override;

  void ErrorReceived(const ErrorValue& aValue) override;

  void LockNotified(bool aCreated) final;

  void WebTransportNotified(bool aCreated) final;

  void Terminated() override;

  // Called on PBackground thread methods

  bool MaybeCreateRemoteWorker(const RemoteWorkerData& aData,
                               uint64_t aWindowID,
                               UniqueMessagePortId& aPortIdentifier,
                               GeckoChildID aChildID);

  void AddActor(SharedWorkerParent* aParent);

  void RemoveActor(SharedWorkerParent* aParent);

  void UpdateSuspend();

  void UpdateFrozen();

  void SetLocaleOverride(const nsACString& aLanguageOverride,
                         const nsTArray<nsString>& aLanguages);

  void UpdateTimezoneOverride(const nsAString& aTimezoneOverride);

  bool IsSecureContext() const;

  void Terminate();

  // Called on main-thread only.

  void RegisterHolder(SharedWorkerManagerHolder* aHolder);

  void UnregisterHolder(SharedWorkerManagerHolder* aHolder);

 private:
  SharedWorkerManager(nsIEventTarget* aPBackgroundEventTarget,
                      const RemoteWorkerData& aData,
                      nsIPrincipal* aLoadingPrincipal,
                      const OriginAttributes& aEffectiveStoragePrincipalAttrs);

  ~SharedWorkerManager();

  nsCOMPtr<nsIEventTarget> mPBackgroundEventTarget;

  nsCOMPtr<nsIPrincipal> mLoadingPrincipal;
  const nsCString mDomain;
  const OriginAttributes mEffectiveStoragePrincipalAttrs;
  const nsCOMPtr<nsIURI> mResolvedScriptURL;
  const WorkerOptions mWorkerOptions;
  const bool mIsSecureContext;
  bool mSuspended;
  bool mFrozen;
  uint32_t mLockCount = 0;
  uint32_t mWebTransportCount = 0;

  // Raw pointers because SharedWorkerParent unregisters itself in
  // ActorDestroy().
  nsTArray<CheckedUnsafePtr<SharedWorkerParent>> mActors;

  RefPtr<RemoteWorkerController> mRemoteWorkerController;

  // Main-thread only. Raw Pointers because holders keep the manager alive and
  // they unregister themselves in their DTOR.
  nsTArray<CheckedUnsafePtr<SharedWorkerManagerHolder>> mHolders;
};

}  // namespace mozilla::dom

#endif  // mozilla_dom_SharedWorkerManager_h
