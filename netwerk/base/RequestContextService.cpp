/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RequestContextService.h"

#include "../protocol/http/nsHttpHandler.h"
#include "mozilla/Atomics.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Logging.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/Document.h"
#include "mozilla/net/NeckoChild.h"
#include "mozilla/net/NeckoCommon.h"
#include "nsComponentManagerUtils.h"
#include "nsIDocShell.h"
#include "nsIDocumentLoader.h"
#include "nsIObserverService.h"
#include "nsITimer.h"
#include "nsIXULRuntime.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"

namespace mozilla {
namespace net {

LazyLogModule gRequestContextLog("RequestContext");
#undef LOG
#define LOG(args) MOZ_LOG(gRequestContextLog, LogLevel::Info, args)

static StaticRefPtr<RequestContextService> gSingleton;

// This is used to prevent adding tail pending requests after shutdown
static bool sShutdown = false;

// nsIRequestContext
class RequestContext final : public nsIRequestContext,
                             public nsITimerCallback,
                             public nsINamed {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIREQUESTCONTEXT
  NS_DECL_NSITIMERCALLBACK
  NS_DECL_NSINAMED

  explicit RequestContext(const uint64_t id);

 private:
  virtual ~RequestContext();

  void ProcessTailQueue(nsresult aResult);
  // Reschedules the timer if needed
  void ScheduleUnblock();
  // Hard-reschedules the timer
  void RescheduleUntailTimer(TimeStamp const& now);

  uint64_t mID;
  Atomic<uint32_t> mBlockingTransactionCount;

  using PendingTailRequest = nsCOMPtr<nsIRequestTailUnblockCallback>;
  // Number of known opened non-tailed requets
  uint32_t mNonTailRequests;
  // Queue of requests that have been tailed, when conditions are met
  // we call each of them to unblock and drop the reference
  nsTArray<PendingTailRequest> mTailQueue;
  // Loosly scheduled timer, never scheduled further to the future than
  // mUntailAt time
  nsCOMPtr<nsITimer> mUntailTimer;
  // Timestamp when the timer is expected to fire,
  // always less than or equal to mUntailAt
  TimeStamp mTimerScheduledAt;
  // Timestamp when we want to actually untail queued requets based on
  // the number of request count change in the past; iff this timestamp
  // is set, we tail requests
  TimeStamp mUntailAt;

  // Timestamp of the navigation start time, set to Now() in BeginLoad().
  // This is used to progressively lower the maximum delay time so that
  // we can't get to a situation when a number of repetitive requests
  // on the page causes forever tailing.
  TimeStamp mBeginLoadTime;

  // This member is true only between DOMContentLoaded notification and
  // next document load beginning for this request context.
  // Top level request contexts are recycled.
  bool mAfterDOMContentLoaded;
};

NS_IMPL_ISUPPORTS(RequestContext, nsIRequestContext, nsITimerCallback, nsINamed)

RequestContext::RequestContext(const uint64_t aID)
    : mID(aID),
      mBlockingTransactionCount(0),
      mNonTailRequests(0),
      mAfterDOMContentLoaded(false) {
  LOG(("RequestContext::RequestContext this=%p id=%" PRIx64, this, mID));
}

RequestContext::~RequestContext() {
  MOZ_ASSERT(mTailQueue.Length() == 0);

  LOG(("RequestContext::~RequestContext this=%p blockers=%u", this,
       mBlockingTransactionCount.load()));
}

NS_IMETHODIMP
RequestContext::BeginLoad() {
  MOZ_ASSERT(NS_IsMainThread());

  LOG(("RequestContext::BeginLoad %p", this));

  if (IsNeckoChild()) {
    // Tailing is not supported on the child process
    if (gNeckoChild) {
      gNeckoChild->SendRequestContextLoadBegin(mID);
    }
    return NS_OK;
  }

  mAfterDOMContentLoaded = false;
  mBeginLoadTime = TimeStamp::NowLoRes();
  return NS_OK;
}

NS_IMETHODIMP
RequestContext::DOMContentLoaded() {
  MOZ_ASSERT(NS_IsMainThread());

  LOG(("RequestContext::DOMContentLoaded %p", this));

  if (IsNeckoChild()) {
    // Tailing is not supported on the child process
    if (gNeckoChild) {
      gNeckoChild->SendRequestContextAfterDOMContentLoaded(mID);
    }
    return NS_OK;
  }

  if (mAfterDOMContentLoaded) {
    // There is a possibility of a duplicate notification
    return NS_OK;
  }

  mAfterDOMContentLoaded = true;

  // Conditions for the delay calculation has changed.
  ScheduleUnblock();
  return NS_OK;
}

NS_IMETHODIMP
RequestContext::GetBlockingTransactionCount(
    uint32_t* aBlockingTransactionCount) {
  NS_ENSURE_ARG_POINTER(aBlockingTransactionCount);
  *aBlockingTransactionCount = mBlockingTransactionCount;
  return NS_OK;
}

NS_IMETHODIMP
RequestContext::AddBlockingTransaction() {
  mBlockingTransactionCount++;
  LOG(("RequestContext::AddBlockingTransaction this=%p blockers=%u", this,
       mBlockingTransactionCount.load()));
  return NS_OK;
}

NS_IMETHODIMP
RequestContext::RemoveBlockingTransaction(uint32_t* outval) {
  NS_ENSURE_ARG_POINTER(outval);
  mBlockingTransactionCount--;
  LOG(("RequestContext::RemoveBlockingTransaction this=%p blockers=%u", this,
       mBlockingTransactionCount.load()));
  *outval = mBlockingTransactionCount;
  return NS_OK;
}

uint64_t RequestContext::GetID() { return mID; }

NS_IMETHODIMP
RequestContext::AddNonTailRequest() {
  MOZ_ASSERT(NS_IsMainThread());

  ++mNonTailRequests;
  LOG(("RequestContext::AddNonTailRequest this=%p, cnt=%u", this,
       mNonTailRequests));

  ScheduleUnblock();
  return NS_OK;
}

NS_IMETHODIMP
RequestContext::RemoveNonTailRequest() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mNonTailRequests > 0);

  LOG(("RequestContext::RemoveNonTailRequest this=%p, cnt=%u", this,
       mNonTailRequests - 1));

  --mNonTailRequests;

  ScheduleUnblock();
  return NS_OK;
}

void RequestContext::ScheduleUnblock() {
  MOZ_ASSERT(!IsNeckoChild());
  MOZ_ASSERT(NS_IsMainThread());

  if (!gHttpHandler) {
    return;
  }

  uint32_t quantum =
      gHttpHandler->TailBlockingDelayQuantum(mAfterDOMContentLoaded);
  uint32_t delayMax = gHttpHandler->TailBlockingDelayMax();
  uint32_t totalMax = gHttpHandler->TailBlockingTotalMax();

  if (!mBeginLoadTime.IsNull()) {
    // We decrease the maximum delay progressively with the time since the page
    // load begin.  This seems like a reasonable and clear heuristic allowing us
    // to start loading tailed requests in a deterministic time after the load
    // has started.

    uint32_t sinceBeginLoad = static_cast<uint32_t>(
        (TimeStamp::NowLoRes() - mBeginLoadTime).ToMilliseconds());
    uint32_t tillTotal = totalMax - std::min(sinceBeginLoad, totalMax);
    uint32_t proportion = totalMax  // values clamped between 0 and 60'000
                              ? (delayMax * tillTotal) / totalMax
                              : 0;
    delayMax = std::min(delayMax, proportion);
  }

  CheckedInt<uint32_t> delay = quantum * mNonTailRequests;

  if (!mAfterDOMContentLoaded) {
    // Before DOMContentLoaded notification we want to make sure that tailed
    // requests don't start when there is a short delay during which we may
    // not have any active requests on the page happening.
    delay += quantum;
  }

  if (!delay.isValid() || delay.value() > delayMax) {
    delay = delayMax;
  }

  LOG(
      ("RequestContext::ScheduleUnblock this=%p non-tails=%u tail-queue=%zu "
       "delay=%u after-DCL=%d",
       this, mNonTailRequests, mTailQueue.Length(), delay.value(),
       mAfterDOMContentLoaded));

  TimeStamp now = TimeStamp::NowLoRes();
  mUntailAt = now + TimeDuration::FromMilliseconds(delay.value());

  if (mTimerScheduledAt.IsNull() || mUntailAt < mTimerScheduledAt) {
    LOG(("RequestContext %p timer would fire too late, rescheduling", this));
    RescheduleUntailTimer(now);
  }
}

void RequestContext::RescheduleUntailTimer(TimeStamp const& now) {
  MOZ_ASSERT(mUntailAt >= now);

  if (mUntailTimer) {
    mUntailTimer->Cancel();
  }

  if (!mTailQueue.Length()) {
    mUntailTimer = nullptr;
    mTimerScheduledAt = TimeStamp();
    return;
  }

  TimeDuration interval = mUntailAt - now;
  if (!mTimerScheduledAt.IsNull() && mUntailAt < mTimerScheduledAt) {
    // When the number of untailed requests goes down,
    // let's half the interval, since it's likely we would
    // reschedule for a shorter time again very soon.
    // This will likely save rescheduling this timer.
    interval = interval / int64_t(2);
    mTimerScheduledAt = mUntailAt - interval;
  } else {
    mTimerScheduledAt = mUntailAt;
  }

  uint32_t delay = interval.ToMilliseconds();
  nsresult rv =
      NS_NewTimerWithCallback(getter_AddRefs(mUntailTimer), this, delay,
                              nsITimer::TYPE_ONE_SHOT, nullptr);
  if (NS_FAILED(rv)) {
    NS_WARNING("Could not reschedule untail timer");
  }

  LOG(("RequestContext::RescheduleUntailTimer %p in %d", this, delay));
}

NS_IMETHODIMP
RequestContext::Notify(nsITimer* timer) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(timer == mUntailTimer);
  MOZ_ASSERT(!mTimerScheduledAt.IsNull());
  MOZ_ASSERT(mTailQueue.Length());

  mUntailTimer = nullptr;

  TimeStamp now = TimeStamp::NowLoRes();
  if (mUntailAt > mTimerScheduledAt && mUntailAt > now) {
    LOG(("RequestContext %p timer fired too soon, rescheduling", this));
    RescheduleUntailTimer(now);
    return NS_OK;
  }

  // Must drop to allow re-engage of the timer
  mTimerScheduledAt = TimeStamp();

  ProcessTailQueue(NS_OK);

  return NS_OK;
}

NS_IMETHODIMP
RequestContext::GetName(nsACString& aName) {
  aName.AssignLiteral("RequestContext");
  return NS_OK;
}

NS_IMETHODIMP
RequestContext::IsContextTailBlocked(nsIRequestTailUnblockCallback* aRequest,
                                     bool* aBlocked) {
  MOZ_ASSERT(NS_IsMainThread());

  LOG(("RequestContext::IsContextTailBlocked this=%p, request=%p, queued=%zu",
       this, aRequest, mTailQueue.Length()));

  *aBlocked = false;

  if (sShutdown) {
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  if (mUntailAt.IsNull()) {
    LOG(("  untail time passed"));
    return NS_OK;
  }

  if (mAfterDOMContentLoaded && !mNonTailRequests) {
    LOG(("  after DOMContentLoaded and no untailed requests"));
    return NS_OK;
  }

  if (!gHttpHandler) {
    // Xpcshell tests may not have http handler
    LOG(("  missing gHttpHandler?"));
    return NS_OK;
  }

  *aBlocked = true;
  mTailQueue.AppendElement(aRequest);

  LOG(("  request queued"));

  if (!mUntailTimer) {
    ScheduleUnblock();
  }

  return NS_OK;
}

NS_IMETHODIMP
RequestContext::CancelTailedRequest(nsIRequestTailUnblockCallback* aRequest) {
  MOZ_ASSERT(NS_IsMainThread());

  bool removed = mTailQueue.RemoveElement(aRequest);

  LOG(("RequestContext::CancelTailedRequest %p req=%p removed=%d", this,
       aRequest, removed));

  // Stop untail timer if all tail requests are canceled.
  if (removed && mTailQueue.IsEmpty()) {
    if (mUntailTimer) {
      mUntailTimer->Cancel();
      mUntailTimer = nullptr;
    }

    // Must drop to allow re-engage of the timer
    mTimerScheduledAt = TimeStamp();
  }

  return NS_OK;
}

void RequestContext::ProcessTailQueue(nsresult aResult) {
  LOG(("RequestContext::ProcessTailQueue this=%p, queued=%zu, rv=%" PRIx32,
       this, mTailQueue.Length(), static_cast<uint32_t>(aResult)));

  if (mUntailTimer) {
    mUntailTimer->Cancel();
    mUntailTimer = nullptr;
  }

  // Must drop to stop tailing requests
  mUntailAt = TimeStamp();

  nsTArray<PendingTailRequest> queue = std::move(mTailQueue);

  for (const auto& request : queue) {
    LOG(("  untailing %p", request.get()));
    request->OnTailUnblock(aResult);
  }
}

NS_IMETHODIMP
RequestContext::CancelTailPendingRequests(nsresult aResult) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(NS_FAILED(aResult));

  ProcessTailQueue(aResult);
  return NS_OK;
}

// nsIRequestContextService
RequestContextService* RequestContextService::sSelf = nullptr;

NS_IMPL_ISUPPORTS(RequestContextService, nsIRequestContextService, nsIObserver)

RequestContextService::RequestContextService() {
  MOZ_ASSERT(!sSelf, "multiple rcs instances!");
  MOZ_ASSERT(NS_IsMainThread());
  sSelf = this;

  nsCOMPtr<nsIXULRuntime> runtime = do_GetService("@mozilla.org/xre/runtime;1");
  runtime->GetProcessID(&mRCIDNamespace);
}

RequestContextService::~RequestContextService() {
  MOZ_ASSERT(NS_IsMainThread());
  Shutdown();
  sSelf = nullptr;
}

nsresult RequestContextService::Init() {
  nsresult rv;

  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (!obs) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  rv = obs->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false);
  if (NS_FAILED(rv)) {
    return rv;
  }
  obs->AddObserver(this, "content-document-interactive", false);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_OK;
}

void RequestContextService::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  // We need to do this to prevent the requests from being scheduled after
  // shutdown.
  for (const auto& data : mTable.Values()) {
    data->CancelTailPendingRequests(NS_ERROR_ABORT);
  }
  mTable.Clear();
  sShutdown = true;
}

/* static */
already_AddRefed<nsIRequestContextService>
RequestContextService::GetOrCreate() {
  MOZ_ASSERT(NS_IsMainThread());

  if (sShutdown) {
    return nullptr;
  }

  RefPtr<RequestContextService> svc;
  if (gSingleton) {
    svc = gSingleton;
  } else {
    svc = new RequestContextService();
    nsresult rv = svc->Init();
    NS_ENSURE_SUCCESS(rv, nullptr);
    gSingleton = svc;
    ClearOnShutdown(&gSingleton);
  }

  return svc.forget();
}

NS_IMETHODIMP
RequestContextService::GetRequestContext(const uint64_t rcID,
                                         nsIRequestContext** rc) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_ARG_POINTER(rc);
  *rc = nullptr;

  if (sShutdown) {
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  if (!rcID) {
    return NS_ERROR_INVALID_ARG;
  }

  *rc = do_AddRef(mTable.LookupOrInsertWith(rcID, [&] {
          nsCOMPtr<nsIRequestContext> newSC = new RequestContext(rcID);
          return newSC;
        })).take();

  return NS_OK;
}

NS_IMETHODIMP
RequestContextService::GetRequestContextFromLoadGroup(nsILoadGroup* aLoadGroup,
                                                      nsIRequestContext** rc) {
  nsresult rv;

  uint64_t rcID;
  rv = aLoadGroup->GetRequestContextID(&rcID);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return GetRequestContext(rcID, rc);
}

NS_IMETHODIMP
RequestContextService::NewRequestContext(nsIRequestContext** rc) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_ARG_POINTER(rc);
  *rc = nullptr;

  if (sShutdown) {
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  uint64_t rcID =
      ((static_cast<uint64_t>(mRCIDNamespace) << 32) & 0xFFFFFFFF00000000LL) |
      mNextRCID++;

  nsCOMPtr<nsIRequestContext> newSC = new RequestContext(rcID);
  mTable.InsertOrUpdate(rcID, newSC);
  newSC.swap(*rc);

  return NS_OK;
}

NS_IMETHODIMP
RequestContextService::RemoveRequestContext(const uint64_t rcID) {
  MOZ_ASSERT(NS_IsMainThread());
  mTable.Remove(rcID);
  return NS_OK;
}

NS_IMETHODIMP
RequestContextService::Observe(nsISupports* subject, const char* topic,
                               const char16_t* data_unicode) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!strcmp(NS_XPCOM_SHUTDOWN_OBSERVER_ID, topic)) {
    Shutdown();
    return NS_OK;
  }

  if (!strcmp("content-document-interactive", topic)) {
    nsCOMPtr<dom::Document> document(do_QueryInterface(subject));
    MOZ_ASSERT(document);
    // We want this be triggered also for iframes, since those track their
    // own request context ids.
    if (!document) {
      return NS_OK;
    }
    nsIDocShell* ds = document->GetDocShell();
    // XML documents don't always have a docshell assigned
    if (!ds) {
      return NS_OK;
    }
    nsCOMPtr<nsIDocumentLoader> dl(do_QueryInterface(ds));
    if (!dl) {
      return NS_OK;
    }
    nsCOMPtr<nsILoadGroup> lg;
    dl->GetLoadGroup(getter_AddRefs(lg));
    if (!lg) {
      return NS_OK;
    }
    nsCOMPtr<nsIRequestContext> rc;
    GetRequestContextFromLoadGroup(lg, getter_AddRefs(rc));
    if (rc) {
      rc->DOMContentLoaded();
    }

    return NS_OK;
  }

  MOZ_ASSERT(false, "Unexpected observer topic");
  return NS_OK;
}

}  // namespace net
}  // namespace mozilla

#undef LOG
