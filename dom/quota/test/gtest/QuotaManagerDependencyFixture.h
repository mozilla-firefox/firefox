/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_QUOTA_TEST_GTEST_QUOTAMANAGERDEPENDENCYFIXTURE_H_
#define DOM_QUOTA_TEST_GTEST_QUOTAMANAGERDEPENDENCYFIXTURE_H_

#include "gtest/gtest.h"
#include "mozilla/MozPromise.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/dom/quota/ClientDirectoryLock.h"
#include "mozilla/dom/quota/ClientDirectoryLockHandle.h"
#include "mozilla/dom/quota/DirectoryLock.h"
#include "mozilla/dom/quota/DirectoryLockInlines.h"
#include "mozilla/dom/quota/ForwardDecls.h"
#include "mozilla/dom/quota/QuotaManager.h"

// ENSURE_NO_FATAL_FAILURE is useful in non-void functions where
// ASSERT_NO_FATAL_FAILURE can't be used.
#define ENSURE_NO_FATAL_FAILURE(expr, ret) \
  (expr);                                  \
  if (HasFatalFailure()) {                 \
    return ret;                            \
  }

#define QM_TEST_FAIL [](nsresult) { FAIL(); }

namespace mozilla::dom::quota::test {

class QuotaManagerDependencyFixture : public testing::Test {
 public:
  static void InitializeFixture();
  static void ShutdownFixture();

  static void InitializeStorage();
  static void StorageInitialized(bool* aResult);
  static void AssertStorageInitialized();
  static void AssertStorageNotInitialized();
  static void ClearStorage();
  static void ShutdownStorage();

  static void InitializeTemporaryStorage();
  static void TemporaryStorageInitialized(bool* aResult);
  static void AssertTemporaryStorageInitialized();
  static void AssertTemporaryStorageNotInitialized();
  static void ShutdownTemporaryStorage();

  static void InitializeTemporaryOrigin(const OriginMetadata& aOriginMetadata,
                                        bool aCreateIfNonExistent = true);
  static void TemporaryOriginInitialized(const OriginMetadata& aOriginMetadata,
                                         bool* aResult);
  static void AssertTemporaryOriginInitialized(
      const OriginMetadata& aOriginMetadata);
  static void AssertTemporaryOriginNotInitialized(
      const OriginMetadata& aOriginMetadata);
  static void GetOriginUsage(const OriginMetadata& aOriginMetadata,
                             UsageInfo* aResult);
  static void GetCachedOriginUsage(const OriginMetadata& aOriginMetadata,
                                   UsageInfo* aResult);
  static void ClearStoragesForOrigin(const OriginMetadata& aOriginMetadata);

  static void InitializeTemporaryClient(const ClientMetadata& aClientMetadata);

  static CStringArray ListOrigins();
  static CStringArray ListCachedOrigins();

  static void ClearStoragesForOriginAttributesPattern(
      const nsAString& aPattern);

  static uint64_t TotalDirectoryIterations();

  /* Convenience method for tasks which must be called on PBackground thread */
  template <class Invokable, class... Args>
  static auto PerformOnBackgroundThread(Invokable&& aInvokable, Args&&... aArgs)
      -> std::invoke_result_t<Invokable, Args...> {
    return PerformOnThread(BackgroundTargetStrongRef(),
                           std::forward<Invokable>(aInvokable),
                           std::forward<Args>(aArgs)...);
  }

  /* Convenience method for tasks which must be executed on IO thread */
  template <class Invokable, class... Args>
  static auto PerformOnIOThread(Invokable&& aInvokable, Args&&... aArgs)
      -> std::invoke_result_t<Invokable, Args...> {
    QuotaManager* quotaManager = QuotaManager::Get();
    MOZ_RELEASE_ASSERT(quotaManager);

    return PerformOnThread(quotaManager->IOThread(),
                           std::forward<Invokable>(aInvokable),
                           std::forward<Args>(aArgs)...);
  }

  template <class Invokable, class... Args,
            bool ReturnTypeIsVoid =
                std::is_same_v<std::invoke_result_t<Invokable, Args...>, void>>
  static auto PerformOnThread(nsISerialEventTarget* aTarget,
                              Invokable&& aInvokable, Args&&... aArgs)
      -> std::invoke_result_t<Invokable, Args...> {
    using ReturnType =
        std::conditional_t<ReturnTypeIsVoid, bool,
                           std::invoke_result_t<Invokable, Args...>>;

    bool done = false;
    auto boundTask =
        // For c++17, bind is cleaner than tuple for parameter pack forwarding
        // NOLINTNEXTLINE(modernize-avoid-bind)
        std::bind(std::forward<Invokable>(aInvokable),
                  std::forward<Args>(aArgs)...);
    Maybe<ReturnType> maybeReturnValue;
    InvokeAsync(
        aTarget, __func__,
        [boundTask = std::move(boundTask), &maybeReturnValue]() mutable {
          if constexpr (ReturnTypeIsVoid) {
            boundTask();
            (void)maybeReturnValue;
          } else {
            maybeReturnValue.emplace(boundTask());
          }
          return BoolPromise::CreateAndResolve(true, __func__);
        })
        ->Then(GetCurrentSerialEventTarget(), __func__,
               [&done](const BoolPromise::ResolveOrRejectValue& /* aValue */) {
                 done = true;
               });

    SpinEventLoopUntil("Promise is fulfilled"_ns, [&done]() { return done; });

    if constexpr (!ReturnTypeIsVoid) {
      return maybeReturnValue.extract();
    }
  }

  // Acquire and await a client directory lock and pass it to the given task.
  template <class Task>
  static void PerformClientDirectoryLockTest(
      const ClientMetadata& aClientMetadata, Task&& aTask) {
    PerformOnBackgroundThread(
        [clientMetadata = aClientMetadata, task = std::forward<Task>(aTask)]() {
          QuotaManager* quotaManager = QuotaManager::Get();
          ASSERT_TRUE(quotaManager);

          RefPtr<ClientDirectoryLock> directoryLock =
              quotaManager->CreateDirectoryLock(clientMetadata,
                                                /* aExclusive */ false);

          auto value = Await(directoryLock->Acquire());
          ASSERT_TRUE(value.IsResolve());

          task(std::move(directoryLock));
        });
  }

  template <class Task>
  static void PerformClientDirectoryTest(const ClientMetadata& aClientMetadata,
                                         Task&& aTask) {
    PerformOnBackgroundThread([clientMetadata = aClientMetadata,
                               task = std::forward<Task>(aTask)]() mutable {
      ClientDirectoryLockHandle directoryLockHandle;

      QuotaManager* quotaManager = QuotaManager::Get();
      ASSERT_TRUE(quotaManager);

      bool done = false;

      quotaManager->OpenClientDirectory(clientMetadata)
          ->Then(
              GetCurrentSerialEventTarget(), __func__,
              [&directoryLockHandle,
               &done](ClientDirectoryLockHandle&& aResolveValue) {
                directoryLockHandle = std::move(aResolveValue);

                done = true;
              },
              [&done](const nsresult aRejectValue) {
                ASSERT_TRUE(false);

                done = true;
              });

      SpinEventLoopUntil("Promise is fulfilled"_ns, [&done]() { return done; });

      ASSERT_TRUE(directoryLockHandle);

      PerformOnIOThread(std::move(task), directoryLockHandle->Id());

      {
        auto destroyingDirectoryLockHandle = std::move(directoryLockHandle);
      }
    });
  }

  /* Convenience method for defering execution of code until the promise has
   * been resolved or rejected */
  template <typename ResolveValueT, typename RejectValueT, bool IsExclusive>
  static typename MozPromise<ResolveValueT, RejectValueT,
                             IsExclusive>::ResolveOrRejectValue
  Await(RefPtr<MozPromise<ResolveValueT, RejectValueT, IsExclusive>> aPromise) {
    using PromiseType = MozPromise<ResolveValueT, RejectValueT, IsExclusive>;
    using ResolveOrRejectValue = typename PromiseType::ResolveOrRejectValue;

    ResolveOrRejectValue value;

    bool done = false;

    auto SelectResolveOrRejectCallback = [&value, &done]() {
      if constexpr (IsExclusive) {
        return [&value, &done](ResolveOrRejectValue&& aValue) {
          value = std::move(aValue);

          done = true;
        };
      } else {
        return [&value, &done](const ResolveOrRejectValue& aValue) {
          value = aValue;

          done = true;
        };
      }
    };

    aPromise->Then(GetCurrentSerialEventTarget(), __func__,
                   SelectResolveOrRejectCallback());

    SpinEventLoopUntil("Promise is fulfilled"_ns, [&done]() { return done; });

    return value;
  }

  static const nsCOMPtr<nsISerialEventTarget>& BackgroundTargetStrongRef() {
    return sBackgroundTarget;
  }

  static PrincipalMetadata GetTestPrincipalMetadata();
  static OriginMetadata GetTestPersistentOriginMetadata();
  static OriginMetadata GetTestOriginMetadata();
  static ClientMetadata GetTestClientMetadata();

  static PrincipalMetadata GetOtherTestPrincipalMetadata();
  static OriginMetadata GetOtherTestOriginMetadata();
  static ClientMetadata GetOtherTestClientMetadata();

 private:
  static void EnsureQuotaManager();

  static nsCOMPtr<nsISerialEventTarget> sBackgroundTarget;
};

}  // namespace mozilla::dom::quota::test

#endif  // DOM_QUOTA_TEST_GTEST_QUOTAMANAGERDEPENDENCYFIXTURE_H_
