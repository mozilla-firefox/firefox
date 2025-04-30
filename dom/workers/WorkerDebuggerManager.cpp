/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WorkerDebuggerManager.h"

#include "nsSimpleEnumerator.h"

#include "mozilla/dom/JSExecutionManager.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPtr.h"

#include "WorkerDebugger.h"
#include "WorkerPrivate.h"
#include "nsIObserverService.h"

namespace mozilla::dom {

namespace {

class RegisterDebuggerMainThreadRunnable final : public mozilla::Runnable {
  WorkerPrivate* mWorkerPrivate;
  bool mNotifyListeners;

 public:
  RegisterDebuggerMainThreadRunnable(WorkerPrivate* aWorkerPrivate,
                                     bool aNotifyListeners)
      : mozilla::Runnable("RegisterDebuggerMainThreadRunnable"),
        mWorkerPrivate(aWorkerPrivate),
        mNotifyListeners(aNotifyListeners) {}

 private:
  ~RegisterDebuggerMainThreadRunnable() = default;

  NS_IMETHOD
  Run() override {
    WorkerDebuggerManager* manager = WorkerDebuggerManager::Get();
    MOZ_ASSERT(manager);

    manager->RegisterDebuggerMainThread(mWorkerPrivate, mNotifyListeners);
    return NS_OK;
  }
};

class UnregisterDebuggerMainThreadRunnable final : public mozilla::Runnable {
  WorkerPrivate* mWorkerPrivate;

 public:
  explicit UnregisterDebuggerMainThreadRunnable(WorkerPrivate* aWorkerPrivate)
      : mozilla::Runnable("UnregisterDebuggerMainThreadRunnable"),
        mWorkerPrivate(aWorkerPrivate) {}

 private:
  ~UnregisterDebuggerMainThreadRunnable() = default;

  NS_IMETHOD
  Run() override {
    WorkerDebuggerManager* manager = WorkerDebuggerManager::Get();
    MOZ_ASSERT(manager);

    manager->UnregisterDebuggerMainThread(mWorkerPrivate);
    return NS_OK;
  }
};

static StaticRefPtr<WorkerDebuggerManager> gWorkerDebuggerManager;

} /* anonymous namespace */

class WorkerDebuggerEnumerator final : public nsSimpleEnumerator {
  nsTArray<nsCOMPtr<nsIWorkerDebugger>> mDebuggers;
  uint32_t mIndex;

 public:
  explicit WorkerDebuggerEnumerator(
      const nsTArray<nsCOMPtr<nsIWorkerDebugger>>& aDebuggers)
      : mIndex(0) {
    for (auto debugger : aDebuggers) {
      bool isRemote;
      Unused << debugger->GetIsRemote(&isRemote);
      if (!isRemote) {
        mDebuggers.AppendElement(debugger);
      }
    }
  }

  NS_DECL_NSISIMPLEENUMERATOR

  const nsID& DefaultInterface() override {
    return NS_GET_IID(nsIWorkerDebugger);
  }

 private:
  ~WorkerDebuggerEnumerator() override = default;
};

NS_IMETHODIMP
WorkerDebuggerEnumerator::HasMoreElements(bool* aResult) {
  *aResult = mIndex < mDebuggers.Length();
  return NS_OK;
};

NS_IMETHODIMP
WorkerDebuggerEnumerator::GetNext(nsISupports** aResult) {
  if (mIndex == mDebuggers.Length()) {
    return NS_ERROR_FAILURE;
  }

  mDebuggers.ElementAt(mIndex++).forget(aResult);
  return NS_OK;
};

WorkerDebuggerManager::WorkerDebuggerManager()
    : mMutex("WorkerDebuggerManager::mMutex") {
  AssertIsOnMainThread();
}

WorkerDebuggerManager::~WorkerDebuggerManager() { AssertIsOnMainThread(); }

// static
already_AddRefed<WorkerDebuggerManager> WorkerDebuggerManager::GetInstance() {
  RefPtr<WorkerDebuggerManager> manager = WorkerDebuggerManager::GetOrCreate();
  return manager.forget();
}

// static
WorkerDebuggerManager* WorkerDebuggerManager::GetOrCreate() {
  AssertIsOnMainThread();

  if (!gWorkerDebuggerManager) {
    // The observer service now owns us until shutdown.
    gWorkerDebuggerManager = new WorkerDebuggerManager();
    if (NS_SUCCEEDED(gWorkerDebuggerManager->Init())) {
      ClearOnShutdown(&gWorkerDebuggerManager);
    } else {
      NS_WARNING("Failed to initialize worker debugger manager!");
      gWorkerDebuggerManager = nullptr;
    }
  }

  return gWorkerDebuggerManager;
}

WorkerDebuggerManager* WorkerDebuggerManager::Get() {
  MOZ_ASSERT(gWorkerDebuggerManager);
  return gWorkerDebuggerManager;
}

NS_IMPL_ISUPPORTS(WorkerDebuggerManager, nsIObserver, nsIWorkerDebuggerManager);

NS_IMETHODIMP
WorkerDebuggerManager::Observe(nsISupports* aSubject, const char* aTopic,
                               const char16_t* aData) {
  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    Shutdown();
    return NS_OK;
  }

  MOZ_ASSERT_UNREACHABLE("Unknown observer topic!");
  return NS_OK;
}

NS_IMETHODIMP
WorkerDebuggerManager::GetWorkerDebuggerEnumerator(
    nsISimpleEnumerator** aResult) {
  AssertIsOnMainThread();

  RefPtr<WorkerDebuggerEnumerator> enumerator =
      new WorkerDebuggerEnumerator(mDebuggers);
  enumerator.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
WorkerDebuggerManager::AddListener(
    nsIWorkerDebuggerManagerListener* aListener) {
  AssertIsOnMainThread();

  MutexAutoLock lock(mMutex);

  if (mListeners.Contains(aListener)) {
    return NS_ERROR_INVALID_ARG;
  }

  mListeners.AppendElement(aListener);
  return NS_OK;
}

NS_IMETHODIMP
WorkerDebuggerManager::RemoveListener(
    nsIWorkerDebuggerManagerListener* aListener) {
  AssertIsOnMainThread();

  MutexAutoLock lock(mMutex);

  if (!mListeners.Contains(aListener)) {
    return NS_OK;
  }

  mListeners.RemoveElement(aListener);
  return NS_OK;
}

nsresult WorkerDebuggerManager::Init() {
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  NS_ENSURE_TRUE(obs, NS_ERROR_FAILURE);

  nsresult rv = obs->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

void WorkerDebuggerManager::Shutdown() {
  AssertIsOnMainThread();

  MutexAutoLock lock(mMutex);

  mListeners.Clear();
}

void WorkerDebuggerManager::RegisterDebugger(WorkerPrivate* aWorkerPrivate) {
  aWorkerPrivate->AssertIsOnParentThread();

  if (NS_IsMainThread()) {
    // When the parent thread is the main thread, it will always block until all
    // register liseners have been called, since it cannot continue until the
    // call to RegisterDebuggerMainThread returns.
    //
    // In this case, it is always safe to notify all listeners on the main
    // thread, even if there were no listeners at the time this method was
    // called, so we can always pass true for the value of aNotifyListeners.
    // This avoids having to lock mMutex to check whether mListeners is empty.
    RegisterDebuggerMainThread(aWorkerPrivate, true);
  } else {
    // We guarantee that if any register listeners are called, the worker does
    // not start running until all register listeners have been called. To
    // guarantee this, the parent thread should block until all register
    // listeners have been called.
    //
    // However, to avoid overhead when the debugger is not being used, the
    // parent thread will only block if there were any listeners at the time
    // this method was called. As a result, we should not notify any listeners
    // on the main thread if there were no listeners at the time this method was
    // called, because the parent will not be blocking in that case.
    bool hasListeners = false;
    {
      MutexAutoLock lock(mMutex);

      hasListeners = !mListeners.IsEmpty();
    }

    nsCOMPtr<nsIRunnable> runnable =
        new RegisterDebuggerMainThreadRunnable(aWorkerPrivate, hasListeners);
    MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(runnable, NS_DISPATCH_NORMAL));

    if (hasListeners) {
      aWorkerPrivate->WaitForIsDebuggerRegistered(true);
    }
  }
}

void WorkerDebuggerManager::UnregisterDebugger(WorkerPrivate* aWorkerPrivate) {
  aWorkerPrivate->AssertIsOnParentThread();

  if (NS_IsMainThread()) {
    UnregisterDebuggerMainThread(aWorkerPrivate);
  } else {
    nsCOMPtr<nsIRunnable> runnable =
        new UnregisterDebuggerMainThreadRunnable(aWorkerPrivate);
    MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(runnable, NS_DISPATCH_NORMAL));

    aWorkerPrivate->WaitForIsDebuggerRegistered(false);
  }
}

void WorkerDebuggerManager::RegisterDebugger(
    nsIWorkerDebugger* aRemoteWorkerDebugger) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess());
  AssertIsOnMainThread();

  mDebuggers.AppendElement(aRemoteWorkerDebugger);
  //  for (const auto& listener : CloneListeners()) {
  //    listener->OnRegister(aRemoteWorkerDebugger);
  //  }
}

void WorkerDebuggerManager::UnregisterDebugger(
    nsIWorkerDebugger* aRemoteWorkerDebugger) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(XRE_IsParentProcess());
  AssertIsOnMainThread();

  mDebuggers.RemoveElement(aRemoteWorkerDebugger);
  //  for (const auto& listener : CloneListeners()) {
  //    listener->OnUnregister(aRemoteWorkerDebugger);
  //  }
}

void WorkerDebuggerManager::RegisterDebuggerMainThread(
    WorkerPrivate* aWorkerPrivate, bool aNotifyListeners) {
  AssertIsOnMainThread();

  RefPtr<WorkerDebugger> debugger = new WorkerDebugger(aWorkerPrivate);
  mDebuggers.AppendElement(debugger);

  aWorkerPrivate->SetDebugger(debugger);

  if (aNotifyListeners) {
    for (const auto& listener : CloneListeners()) {
      listener->OnRegister(debugger);
    }
  }

  aWorkerPrivate->SetIsDebuggerRegistered(true);
}

void WorkerDebuggerManager::UnregisterDebuggerMainThread(
    WorkerPrivate* aWorkerPrivate) {
  AssertIsOnMainThread();

  // There is nothing to do here if the debugger was never succesfully
  // registered. We need to check this on the main thread because the worker
  // does not wait for the registration to complete if there were no listeners
  // installed when it started.
  if (!aWorkerPrivate->IsDebuggerRegistered()) {
    return;
  }

  RefPtr<WorkerDebugger> debugger = aWorkerPrivate->Debugger();
  mDebuggers.RemoveElement(debugger);

  aWorkerPrivate->SetDebugger(nullptr);

  for (const auto& listener : CloneListeners()) {
    listener->OnUnregister(debugger);
  }

  debugger->Close();
  aWorkerPrivate->SetIsDebuggerRegistered(false);
}

uint32_t WorkerDebuggerManager::GetDebuggersLength() const {
  return mDebuggers.Length();
}

nsIWorkerDebugger* WorkerDebuggerManager::GetDebuggerAt(uint32_t aIndex) const {
  return mDebuggers.SafeElementAt(aIndex, nullptr);
}

nsCOMPtr<nsIWorkerDebugger> WorkerDebuggerManager::GetDebuggerById(
    const nsString& aWorkerId) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(!aWorkerId.IsEmpty());
  for (auto debugger : mDebuggers) {
    nsAutoString workerId;
    bool isRemote;
    debugger->GetId(workerId);
    debugger->GetIsRemote(&isRemote);
    if (workerId.Equals(aWorkerId) && isRemote) {
      return debugger;
    }
  }
  return nullptr;
}

nsTArray<nsCOMPtr<nsIWorkerDebuggerManagerListener>>
WorkerDebuggerManager::CloneListeners() {
  MutexAutoLock lock(mMutex);

  return mListeners.Clone();
}

}  // namespace mozilla::dom
