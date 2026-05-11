/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaUtils.h"

#include "mozilla/AppShutdown.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerRef.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsNetUtil.h"

namespace mozilla::media {

bool HostnameInValue(const nsACString& aList, const nsCString& aHostName) {
  auto HostInDomain = [](const nsCString& aHost, const nsCString& aPattern) {
    int32_t patternOffset = 0;
    int32_t hostOffset = 0;

    // Act on '*.' wildcard in the left-most position in a domain pattern.
    if (StringBeginsWith(aPattern, nsCString("*."))) {
      patternOffset = 2;

      // Ignore the lowest level sub-domain for the hostname.
      hostOffset = aHost.FindChar('.') + 1;

      if (hostOffset <= 1) {
        // Reject a match between a wildcard and a TLD or '.foo' form.
        return false;
      }
    }

    nsDependentCString hostRoot(aHost, hostOffset);
    return hostRoot.EqualsIgnoreCase(aPattern.get() + patternOffset);
  };

  nsCString domainList(aList);
  domainList.StripWhitespace();

  if (domainList.IsEmpty() || aHostName.IsEmpty()) {
    return false;
  }

  // Test each domain name in the comma separated list
  // after converting from UTF8 to ASCII. Each domain
  // must match exactly or have a single leading '*.' wildcard.
  for (const nsACString& each : domainList.Split(',')) {
    nsCString domainPattern;
    nsresult rv = NS_DomainToASCIIAllowAnyGlyphfulASCII(each, domainPattern);
    if (NS_SUCCEEDED(rv)) {
      if (HostInDomain(aHostName, domainPattern)) {
        return true;
      }
    } else {
      NS_WARNING("Failed to convert UTF-8 host to ASCII");
    }
  }
  return false;
}

bool HostnameInPref(const char* aPref, const nsCString& aHostName) {
  nsCString domainList;
  if (NS_FAILED(Preferences::GetCString(aPref, domainList))) {
    return false;
  }
  return HostnameInValue(domainList, aHostName);
}

nsCOMPtr<nsIAsyncShutdownClient> GetShutdownBarrier() {
  nsCOMPtr<nsIAsyncShutdownService> svc = services::GetAsyncShutdownService();
  if (!svc) {
    // We can fail to get the shutdown service if we're already shutting down.
    return nullptr;
  }

  nsCOMPtr<nsIAsyncShutdownClient> barrier;
  nsresult rv = svc->GetProfileBeforeChange(getter_AddRefs(barrier));
  if (!barrier) {
    // We are probably in a content process. We need to do cleanup at
    // XPCOM shutdown in leakchecking builds.
    rv = svc->GetXpcomWillShutdown(getter_AddRefs(barrier));
  }
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
  MOZ_RELEASE_ASSERT(barrier);
  return barrier;
}

nsCOMPtr<nsIAsyncShutdownClient> MustGetShutdownBarrier() {
  nsCOMPtr<nsIAsyncShutdownClient> barrier = GetShutdownBarrier();
  MOZ_RELEASE_ASSERT(barrier);
  return barrier;
}

NS_IMPL_ISUPPORTS(ShutdownBlocker, nsIAsyncShutdownBlocker)

namespace {
class TicketBlocker : public ShutdownBlocker {
  using ShutdownMozPromise = ShutdownBlockingTicket::ShutdownMozPromise;

 public:
  explicit TicketBlocker(const nsAString& aName)
      : ShutdownBlocker(aName), mPromise(mHolder.Ensure(__func__)) {}

  NS_IMETHOD
  BlockShutdown(nsIAsyncShutdownClient* aProfileBeforeChange) override {
    mHolder.Resolve(true, __func__);
    return NS_OK;
  }

  void RejectIfExists() { mHolder.RejectIfExists(false, __func__); }

  ShutdownMozPromise* ShutdownPromise() { return mPromise; }

 private:
  ~TicketBlocker() = default;

  MozPromiseHolder<ShutdownMozPromise> mHolder;
  const RefPtr<ShutdownMozPromise> mPromise;
};

class ShutdownBlockingTicketImpl : public ShutdownBlockingTicket {
 private:
  RefPtr<TicketBlocker> mBlocker;

 public:
  explicit ShutdownBlockingTicketImpl(RefPtr<TicketBlocker> aBlocker)
      : mBlocker(std::move(aBlocker)) {}

  static UniquePtr<ShutdownBlockingTicket> Create(const nsAString& aName,
                                                  const nsAString& aFileName,
                                                  int32_t aLineNr) {
    auto blocker = MakeRefPtr<TicketBlocker>(aName);
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "ShutdownBlockingTicketImpl::AddBlocker",
        [blocker, file = nsString(aFileName), aLineNr] {
          MustGetShutdownBarrier()->AddBlocker(blocker, file, aLineNr, u""_ns);
        }));
    if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdown)) {
      // Adding a blocker is not guaranteed to succeed. Remove the blocker in
      // case it succeeded anyway, and bail.
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "ShutdownBlockingTicketImpl::RemoveBlocker", [blocker] {
            MustGetShutdownBarrier()->RemoveBlocker(blocker);
            blocker->RejectIfExists();
          }));
      return nullptr;
    }

    // Adding a blocker is now guaranteed to succeed:
    // - If AppShutdown::IsInOrBeyond(AppShutdown) returned false,
    // - then the AddBlocker main thread task was queued before AppShutdown's
    //   sCurrentShutdownPhase is set to ShutdownPhase::AppShutdown,
    // - which is before AppShutdown will drain the (main thread) event queue to
    //   run the AddBlocker task, if not already run,
    // - which is before profile-before-change (the earliest barrier we'd add a
    //   blocker to, see GetShutdownBarrier()) is notified,
    // - which is when AsyncShutdown prevents further conditions (blockers)
    //   being added to the profile-before-change barrier.
    return MakeUnique<ShutdownBlockingTicketImpl>(std::move(blocker));
  }

  ~ShutdownBlockingTicketImpl() {
    MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(
        NS_NewRunnableFunction(__func__, [blocker = std::move(mBlocker)] {
          GetShutdownBarrier()->RemoveBlocker(blocker);
          blocker->RejectIfExists();
        })));
  }

  ShutdownMozPromise* ShutdownPromise() override {
    return mBlocker->ShutdownPromise();
  }
};
}  // namespace

UniquePtr<ShutdownBlockingTicket> ShutdownBlockingTicket::Create(
    const nsAString& aName, const nsAString& aFileName, int32_t aLineNr) {
  return ShutdownBlockingTicketImpl::Create(aName, aFileName, aLineNr);
}

class MainShutdownWatcher final : public ShutdownWatcher, public nsIObserver {
 public:
  NS_DECL_ISUPPORTS

  explicit MainShutdownWatcher(ShutdownConsumer* aConsumer)
      : ShutdownWatcher(aConsumer) {}

  bool Initialize() {
    if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
      mConsumer = nullptr;
      return false;
    }

    nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
    if (NS_WARN_IF(!obsService)) {
      mConsumer = nullptr;
      return false;
    }

    if (NS_WARN_IF(NS_FAILED(obsService->AddObserver(
            this, NS_XPCOM_WILL_SHUTDOWN_OBSERVER_ID, false)))) {
      mConsumer = nullptr;
      return false;
    }

    mRegistered = true;
    return true;
  }

  void Destroy() override {
    if (!mRegistered) {
      return;
    }

    mRegistered = false;
    mConsumer = nullptr;

    if (nsCOMPtr<nsIObserverService> obsService =
            services::GetObserverService()) {
      obsService->RemoveObserver(this, NS_XPCOM_WILL_SHUTDOWN_OBSERVER_ID);
    }
  }

  NS_IMETHODIMP Observe(nsISupports* aSubject, const char* aTopic,
                        const char16_t* aData) override {
    MOZ_ASSERT(strcmp(aTopic, NS_XPCOM_WILL_SHUTDOWN_OBSERVER_ID) == 0);
    if (mConsumer) {
      mConsumer->OnShutdown();
    }
    Destroy();
    return NS_OK;
  }

 private:
  ~MainShutdownWatcher() override { Destroy(); }

  bool mRegistered = false;
};

NS_IMPL_ISUPPORTS(MainShutdownWatcher, nsIObserver);

class WorkerShutdownWatcher final : public ShutdownWatcher {
 public:
  NS_DECL_ISUPPORTS

  explicit WorkerShutdownWatcher(ShutdownConsumer* aConsumer)
      : ShutdownWatcher(aConsumer) {}

  bool Initialize(dom::WorkerPrivate* aWorkerPrivate) {
    if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
      mConsumer = nullptr;
      return false;
    }

    mWorkerRef = dom::WeakWorkerRef::Create(
        aWorkerPrivate, [self = RefPtr{this}] { self->OnShutdown(); });
    if (NS_WARN_IF(!mWorkerRef)) {
      mConsumer = nullptr;
      return false;
    }

    return true;
  }

  void OnShutdown() {
    if (mConsumer) {
      mConsumer->OnShutdown();
    }
    Destroy();
  }

  void Destroy() override {
    mWorkerRef = nullptr;
    mConsumer = nullptr;
  }

 private:
  ~WorkerShutdownWatcher() override { Destroy(); }

  RefPtr<dom::WeakWorkerRef> mWorkerRef;
};

NS_IMPL_ISUPPORTS0(WorkerShutdownWatcher);

/* static */
already_AddRefed<ShutdownWatcher> ShutdownWatcher::Create(
    ShutdownConsumer* aConsumer) {
  if (NS_IsMainThread()) {
    auto watcher = MakeRefPtr<MainShutdownWatcher>(aConsumer);
    if (watcher->Initialize()) {
      return watcher.forget().downcast<ShutdownWatcher>();
    }
  } else if (dom::WorkerPrivate* workerPrivate =
                 dom::GetCurrentThreadWorkerPrivate()) {
    auto watcher = MakeRefPtr<WorkerShutdownWatcher>(aConsumer);
    if (watcher->Initialize(workerPrivate)) {
      return watcher.forget().downcast<ShutdownWatcher>();
    }
  }

  return nullptr;
}

}  // namespace mozilla::media
