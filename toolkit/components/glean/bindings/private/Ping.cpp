/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/glean/bindings/Ping.h"

#include "jsapi.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/glean/bindings/GleanJSPingsLookup.h"
#include "nsIClassInfoImpl.h"
#include "nsTHashMap.h"
#include "nsString.h"

#include <memory>

namespace mozilla::glean {

namespace impl {

using CallbackMapType =
    nsTHashMap<NoMemMoveKey<nsCStringHashKey>, FalliblePingTestCallback>;
using MetricIdToCallbackMutex = StaticDataMutex<UniquePtr<CallbackMapType>>;
static Maybe<MetricIdToCallbackMutex::AutoLock> GetCallbackMapLock() {
  static MetricIdToCallbackMutex sCallbacks("sCallbacks");
  auto lock = sCallbacks.Lock();
  // Test callbacks will continue to work until the end of AppShutdownTelemetry
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMWillShutdown)) {
    return Nothing();
  }
  if (!*lock) {
    *lock = std::make_unique<CallbackMapType>();
    RunOnShutdown(
        [&] {
          auto lock = sCallbacks.Lock();
          *lock = nullptr;  // deletes, see UniquePtr.h
        },
        ShutdownPhase::XPCOMWillShutdown);
  }
  return Some(std::move(lock));
}

void Ping::Submit(const nsACString& aReason) const {
  (void)SubmitInternal(aReason, nsDependentCString(GetPingNameById(mId)));
}

nsresult Ping::SubmitInternal(const nsACString& aReason,
                              const nsACString& aPingName) const {
  nsresult rv = NS_OK;
  if (NS_WARN_IF(aPingName.IsEmpty())) {
    return NS_ERROR_FAILURE;
  }
  auto callback = Maybe<FalliblePingTestCallback>();
  GetCallbackMapLock().apply(
      [&](const auto& lock) { callback = lock.ref()->Extract(aPingName); });
  // Calling the callback outside of the lock allows it to register a new
  // callback itself.
  if (callback) {
    rv = callback.extract()(aReason);
  }
  fog_submit_ping_by_id(mId, &aReason);
  return rv;
}

void Ping::SetEnabled(bool aValue) const {
  fog_set_ping_enabled_by_id(mId, aValue);
}

void Ping::TestBeforeNextSubmit(PingTestCallback&& aCallback) const {
  TestBeforeNextSubmitFallible(
      [callback = std::move(aCallback)](const nsACString& aReason) -> nsresult {
        callback(aReason);
        return NS_OK;
      },
      nsDependentCString(GetPingNameById(mId)));
}

void Ping::TestBeforeNextSubmitFallible(FalliblePingTestCallback&& aCallback,
                                        const nsACString& aPingName) const {
  MOZ_ASSERT(aPingName.Length());
  {
    GetCallbackMapLock().apply([&](const auto& lock) {
      lock.ref()->InsertOrUpdate(aPingName, aCallback);
    });
  }
}

bool Ping::TestSubmission(PingTestCallback&& aTestCallback,
                          PingSubmitCallback&& aSubmitCallback) const {
  // We could probably get away with passing a reference to the callbacks, but
  // if aSubmitCallback does not actually submit the ping, then we will have
  // created a reference to stack memory that will persist after this function
  // returns.
  auto didCall = std::make_shared<bool>(false);

  TestBeforeNextSubmit([callback = std::move(aTestCallback),
                        didCall](const nsACString& aReason) {
    *didCall = true;
    callback(aReason);
  });

  aSubmitCallback();

  return *didCall;
}

}  // namespace impl

NS_IMPL_CLASSINFO(GleanPing, nullptr, 0, {0})
NS_IMPL_ISUPPORTS_CI(GleanPing, nsIGleanPing)

NS_IMETHODIMP
GleanPing::Submit(const nsACString& aReason) {
  return mPing.SubmitInternal(aReason, mName);
}

NS_IMETHODIMP
GleanPing::SetEnabled(bool aValue) {
  mPing.SetEnabled(aValue);
  return NS_OK;
}

NS_IMETHODIMP
GleanPing::TestBeforeNextSubmit(nsIGleanPingTestCallback* aCallback) {
  if (NS_WARN_IF(!aCallback)) {
    return NS_ERROR_INVALID_ARG;
  }
  // Throw the bare ptr into a COM ptr to keep it around in the lambda.
  mPing.TestBeforeNextSubmitFallible(
      [callback = nsCOMPtr(aCallback)](const nsACString& aReason) -> nsresult {
        return callback->Call(aReason);
      },
      mName);
  return NS_OK;
}

NS_IMETHODIMP
GleanPing::TestSubmission(nsIGleanPingTestCallback* aTestCallback,
                          nsIGleanPingSubmitCallback* aSubmitCallback,
                          uint32_t aSubmitTimeoutMs, JSContext* aCx,
                          dom::Promise** aOutPromise) {
  if (NS_WARN_IF(!aTestCallback || !aSubmitCallback)) {
    return NS_ERROR_INVALID_ARG;
  }

  IgnoredErrorResult err;
  // This promise will be resolved if `aTestCallback` is called (i.e., the ping
  // is submitted) and doesn't throw.
  RefPtr<dom::Promise> submittedPromise =
      dom::Promise::Create(xpc::CurrentNativeGlobal(aCx), err);
  if (err.Failed()) {
    return err.StealNSResult();
  }

  // Wrap the callback with one that will resolve or reject `submittedPromise`.
  mPing.TestBeforeNextSubmitFallible(
      [testCallback = nsCOMPtr{aTestCallback},
       submittedPromise](const nsACString& aReason) {
        nsresult rv = testCallback->Call(aReason);
        if (NS_SUCCEEDED(rv)) {
          submittedPromise->MaybeResolveWithUndefined();
        } else {
          // If the callback threw we need to pass that along as a promise
          // rejection.
          submittedPromise->MaybeReject(rv);
        }
        return NS_OK;
      },
      mName);

  // Call `aSubmitCallback` to trigger ping submission. This function may or may
  // not be async, but becuase it *can* be, XPConnect will always promise wrap
  // its return value.
  RefPtr<dom::Promise> submitPromise;
  MOZ_TRY(aSubmitCallback->Call(getter_AddRefs(submitPromise)));

  RefPtr<dom::Promise> thenPromise =
      MOZ_TRY(submitPromise->ThenWithCycleCollectedArgs(
          [aSubmitTimeoutMs](JSContext*, JS::Handle<JS::Value>,
                             ErrorResult& aRv,
                             RefPtr<dom::Promise> aSubmittedPromise)
              -> already_AddRefed<dom::Promise> {
            // The submit callback has finished successfully now
            // (whether or not it was async).
            if (aSubmitTimeoutMs) {
              // We have a submit timeout, which means that
              // `aSubmitCallback` has triggered the submission through
              // some async means that does not complete before it
              // returns (e.g., idle dispatch from c++). Reject
              // `submittedPromise` after the given timeout to make sure
              // we don't hang forever waiting for a submission that
              // might not happen.
              nsresult rv = NS_DelayedDispatchToCurrentThread(
                  NS_NewRunnableFunction(
                      __func__,
                      [submittedPromise = RefPtr(aSubmittedPromise)]() {
                        submittedPromise->MaybeRejectWithTimeoutError(
                            "Ping was not submitted after timeout");
                      }),
                  aSubmitTimeoutMs);

              if (NS_FAILED(rv)) {
                aSubmittedPromise->MaybeReject(rv);
              }
            } else {
              // If there is no timeout then the ping should have been
              // submitted already. We attempt to reject the promise,
              // which will have no affect if the promise is already
              // settled.
              aSubmittedPromise->MaybeRejectWithOperationError(
                  "Ping did not submit immediately");
            }

            // Chain into `submittedPromise` so that we will
            // resolve/reject appropriately.
            return aSubmittedPromise.forget();
          },
          std::move(submittedPromise)));

  thenPromise.forget(aOutPromise);
  return NS_OK;
}

}  // namespace mozilla::glean
