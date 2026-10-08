/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/DebugOnly.h"

#include "nsLoadGroup.h"

#include "nsArrayEnumerator.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "mozilla/Logging.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsITimedChannel.h"
#include "nsIInterfaceRequestor.h"
#include "nsIRequestObserver.h"
#include "CacheObserver.h"
#include "MainThreadUtils.h"
#include "RequestContextService.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/net/NeckoCommon.h"
#include "mozilla/net/NeckoChild.h"
#include "mozilla/StaticPrefs_network.h"

namespace mozilla {
namespace net {

//
// Log module for nsILoadGroup logging...
//
// To enable logging (see prlog.h for full details):
//
//    set MOZ_LOG=LoadGroup:5
//    set MOZ_LOG_FILE=network.log
//
// This enables LogLevel::Debug level information and places all output in
// the file network.log.
//
static LazyLogModule gLoadGroupLog("LoadGroup");
#undef LOG
#define LOG(args) MOZ_LOG(gLoadGroupLog, mozilla::LogLevel::Debug, args)

////////////////////////////////////////////////////////////////////////////////

static void RescheduleRequest(nsIRequest* aRequest, int32_t delta) {
  nsCOMPtr<nsISupportsPriority> p = do_QueryInterface(aRequest);
  if (p) p->AdjustPriority(delta);
}

nsLoadGroup::nsLoadGroup() { LOG(("LOADGROUP [%p]: Created.\n", this)); }

nsLoadGroup::~nsLoadGroup() {
  DebugOnly<nsresult> rv =
      CancelWithReason(NS_BINDING_ABORTED, "nsLoadGroup::~nsLoadGroup"_ns);
  NS_ASSERTION(NS_SUCCEEDED(rv), "Cancel failed");

  mDefaultLoadRequest = nullptr;

  if (mRequestContext && !mExternalRequestContext) {
    mRequestContextService->RemoveRequestContext(mRequestContext->GetID());
    if (IsNeckoChild() && gNeckoChild && gNeckoChild->CanSend()) {
      gNeckoChild->SendRemoveRequestContext(mRequestContext->GetID());
    }
  }

  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (os) {
    (void)os->RemoveObserver(this, "last-pb-context-exited");
  }

  if (mPageSize) {
    glean::network::page_load_size.Get("page"_ns).Accumulate(mPageSize);
  }
  if (mTotalSubresourcesSize) {
    glean::network::page_load_size.Get("subresources"_ns)
        .Accumulate(mTotalSubresourcesSize);
  }

  LOG(("LOADGROUP [%p]: Destroyed.\n", this));
}

////////////////////////////////////////////////////////////////////////////////
// nsISupports methods:

NS_IMPL_ISUPPORTS(nsLoadGroup, nsILoadGroup, nsILoadGroupChild, nsIRequest,
                  nsISupportsPriority, nsISupportsWeakReference, nsIObserver)

////////////////////////////////////////////////////////////////////////////////
// nsIRequest methods:

NS_IMETHODIMP
nsLoadGroup::GetName(nsACString& result) {
  // XXX is this the right "name" for a load group?

  if (!mDefaultLoadRequest) {
    result.Truncate();
    return NS_OK;
  }

  return mDefaultLoadRequest->GetName(result);
}

NS_IMETHODIMP
nsLoadGroup::IsPending(bool* aResult) {
  *aResult = mForegroundCount > 0;
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetStatus(nsresult* status) {
  if (NS_SUCCEEDED(mStatus) && mDefaultLoadRequest) {
    return mDefaultLoadRequest->GetStatus(status);
  }

  *status = mStatus;
  return NS_OK;
}

NS_IMETHODIMP nsLoadGroup::SetCanceledReason(const nsACString& aReason) {
  return SetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP nsLoadGroup::GetCanceledReason(nsACString& aReason) {
  return GetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP nsLoadGroup::CancelWithReason(nsresult aStatus,
                                            const nsACString& aReason) {
  return CancelWithReasonImpl(aStatus, aReason);
}

NS_IMETHODIMP
nsLoadGroup::Cancel(nsresult status) {
  MOZ_ASSERT(NS_IsMainThread());

  NS_ASSERTION(NS_FAILED(status), "shouldn't cancel with a success code");
  nsresult rv;
  uint32_t count = mRequests.Count();

  auto requests = ToTArray<AutoTArray<nsCOMPtr<nsIRequest>, 8>>(mRequests);
  MOZ_ASSERT(requests.Length() == count);

  // set the load group status to our cancel status while we cancel
  // all our requests...once the cancel is done, we'll reset it...
  //
  mStatus = status;

  // Set the flag indicating that the loadgroup is being canceled...  This
  // prevents any new channels from being added during the operation.
  //
  mIsCanceling = true;

  nsresult firstError = NS_OK;
  while (count > 0) {
    nsCOMPtr<nsIRequest> request = requests.ElementAt(--count);

    NS_ASSERTION(request, "NULL request found in list.");

    if (!mRequests.Contains(request)) {
      // |request| was removed already
      // We need to null out the entry in the request array so we don't try
      // to notify the observers for this request.
      requests.ElementAt(count) = nullptr;

      continue;
    }

    if (MOZ_LOG_TEST(gLoadGroupLog, LogLevel::Debug)) {
      nsAutoCString nameStr;
      request->GetName(nameStr);
      LOG(("LOADGROUP [%p]: Canceling request %p %s.\n", this, request.get(),
           nameStr.get()));
    }

    // Cancel the request...
    rv = request->CancelWithReason(status, mCanceledReason);

    // Remember the first failure and return it...
    if (NS_FAILED(rv) && NS_SUCCEEDED(firstError)) firstError = rv;

    if (NS_FAILED(RemoveRequestFromHashtable(request, status))) {
      // It's possible that request->Cancel causes the request to be removed
      // from the loadgroup causing RemoveRequestFromHashtable to fail.
      // In that case we shouldn't call NotifyRemovalObservers or decrement
      // mForegroundCount since that has already happened.
      requests.ElementAt(count) = nullptr;

      continue;
    }
  }

  for (count = requests.Length(); count > 0;) {
    nsCOMPtr<nsIRequest> request = requests.ElementAt(--count).forget();
    (void)NotifyRemovalObservers(request, status);
  }

  if (mRequestContext) {
    (void)mRequestContext->CancelTailPendingRequests(status);
  }

#if defined(DEBUG)
  NS_ASSERTION(mRequests.IsEmpty(), "Request list is not empty.");
  NS_ASSERTION(mForegroundCount == 0, "Foreground URLs are active.");
#endif

  mStatus = NS_OK;
  mIsCanceling = false;
  mCanceledReason.Truncate();

  return firstError;
}

nsresult nsLoadGroup::CancelRequest(nsIRequest* aRequest,
                                    const nsACString& aReason,
                                    nsresult aStatus) {
  MOZ_ASSERT(NS_FAILED(aStatus));
  mStatus = aStatus;
  mIsCanceling = true;
  MOZ_ASSERT(mRequests.Contains(aRequest));
  nsresult result = aRequest->CancelWithReason(aStatus, aReason);
  if (NS_SUCCEEDED(RemoveRequestFromHashtable(aRequest, aStatus))) {
    (void)NotifyRemovalObservers(aRequest, aStatus);
  }
  mIsCanceling = false;
  mStatus = NS_OK;
  return result;
}

NS_IMETHODIMP
nsLoadGroup::Suspend() {
  nsresult rv, firstError;
  uint32_t count = mRequests.Count();

  auto requests = ToTArray<AutoTArray<nsCOMPtr<nsIRequest>, 8>>(mRequests);

  firstError = NS_OK;
  //
  // Operate the elements from back to front so that if items get
  // get removed from the list it won't affect our iteration
  //
  while (count > 0) {
    nsCOMPtr<nsIRequest> request = requests.ElementAt(--count).forget();

    NS_ASSERTION(request, "NULL request found in list.");
    if (!request) continue;

    if (MOZ_LOG_TEST(gLoadGroupLog, LogLevel::Debug)) {
      nsAutoCString nameStr;
      request->GetName(nameStr);
      LOG(("LOADGROUP [%p]: Suspending request %p %s.\n", this, request.get(),
           nameStr.get()));
    }

    // Suspend the request...
    rv = request->Suspend();

    // Remember the first failure and return it...
    if (NS_FAILED(rv) && NS_SUCCEEDED(firstError)) firstError = rv;
  }

  return firstError;
}

NS_IMETHODIMP
nsLoadGroup::Resume() {
  nsresult rv, firstError;
  uint32_t count = mRequests.Count();

  auto requests = ToTArray<AutoTArray<nsCOMPtr<nsIRequest>, 8>>(mRequests);

  firstError = NS_OK;
  //
  // Operate the elements from back to front so that if items get
  // get removed from the list it won't affect our iteration
  //
  while (count > 0) {
    nsCOMPtr<nsIRequest> request = requests.ElementAt(--count).forget();

    NS_ASSERTION(request, "NULL request found in list.");
    if (!request) continue;

    if (MOZ_LOG_TEST(gLoadGroupLog, LogLevel::Debug)) {
      nsAutoCString nameStr;
      request->GetName(nameStr);
      LOG(("LOADGROUP [%p]: Resuming request %p %s.\n", this, request.get(),
           nameStr.get()));
    }

    // Resume the request...
    rv = request->Resume();

    // Remember the first failure and return it...
    if (NS_FAILED(rv) && NS_SUCCEEDED(firstError)) firstError = rv;
  }

  return firstError;
}

NS_IMETHODIMP
nsLoadGroup::GetLoadFlags(uint32_t* aLoadFlags) {
  *aLoadFlags = mLoadFlags;
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetLoadFlags(uint32_t aLoadFlags) {
  mLoadFlags = aLoadFlags;
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetTRRMode(nsIRequest::TRRMode* aTRRMode) {
  return GetTRRModeImpl(aTRRMode);
}

NS_IMETHODIMP
nsLoadGroup::SetTRRMode(nsIRequest::TRRMode aTRRMode) {
  return SetTRRModeImpl(aTRRMode);
}

NS_IMETHODIMP
nsLoadGroup::GetLoadGroup(nsILoadGroup** loadGroup) {
  nsCOMPtr<nsILoadGroup> result = mLoadGroup;
  result.forget(loadGroup);
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetLoadGroup(nsILoadGroup* loadGroup) {
  mLoadGroup = loadGroup;
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsILoadGroup methods:

NS_IMETHODIMP
nsLoadGroup::GetDefaultLoadRequest(nsIRequest** aRequest) {
  nsCOMPtr<nsIRequest> result = mDefaultLoadRequest;
  result.forget(aRequest);
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetDefaultLoadRequest(nsIRequest* aRequest) {
  LOG(("nsLoadGroup::SetDefaultLoadRequest this=%p default-request=%p", this,
       aRequest));

  mDefaultLoadRequest = aRequest;
  // Inherit the group load flags from the default load request
  if (mDefaultLoadRequest) {
    mDefaultLoadRequest->GetLoadFlags(&mLoadFlags);
    //
    // Mask off any bits that are not part of the nsIRequest flags.
    // in particular, nsIChannel::LOAD_DOCUMENT_URI...
    //
    mLoadFlags &= nsIRequest::LOAD_INHERIT_MASK;

    nsCOMPtr<nsITimedChannel> timedChannel = do_QueryInterface(aRequest);
    mDefaultLoadIsTimed = timedChannel != nullptr;
    if (mDefaultLoadIsTimed) {
      timedChannel->GetChannelCreation(&mDefaultRequestCreationTime);
    }
  }
  // Else, do not change the group's load flags (see bug 95981)
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::AddRequest(nsIRequest* request, nsISupports* ctxt) {
  nsresult rv;

  if (MOZ_LOG_TEST(gLoadGroupLog, LogLevel::Debug)) {
    nsAutoCString nameStr;
    request->GetName(nameStr);
    LOG(("LOADGROUP [%p]: Adding request %p %s (count=%d).\n", this, request,
         nameStr.get(), mRequests.Count()));
  }

  NS_ASSERTION(!mRequests.Contains(request),
               "Entry added to loadgroup twice, don't do that");

  //
  // Do not add the channel, if the loadgroup is being canceled...
  //
  if (mIsCanceling) {
    LOG(
        ("LOADGROUP [%p]: AddChannel() ABORTED because LoadGroup is"
         " being canceled!!\n",
         this));

    return NS_BINDING_ABORTED;
  }

  nsLoadFlags flags;
  // if the request is the default load request or if the default load
  // request is null, then the load group should inherit its load flags from
  // the request, but also we need to enforce defaultLoadFlags.
  if (mDefaultLoadRequest == request || !mDefaultLoadRequest) {
    rv = MergeDefaultLoadFlags(request, flags);
  } else {
    rv = MergeLoadFlags(request, flags);
  }
  if (NS_FAILED(rv)) return rv;

  //
  // Add the request to the list of active requests...
  //

  mRequests.Insert(request);

  if (mPriority != 0) RescheduleRequest(request, mPriority);

  bool foreground = !(flags & nsIRequest::LOAD_BACKGROUND);
  if (foreground) {
    // Update the count of foreground URIs..
    mForegroundCount += 1;
  }

  if (foreground || mNotifyObserverAboutBackgroundRequests) {
    //
    // Fire the OnStartRequest notification out to the observer...
    //
    // If the notification fails then DO NOT add the request to
    // the load group.
    //
    nsCOMPtr<nsIRequestObserver> observer = do_QueryReferent(mObserver);
    RefPtr<nsLoadGroup> self{this};
    if (observer) {
      LOG(
          ("LOADGROUP [%p]: Firing OnStartRequest for request %p."
           "(foreground count=%d).\n",
           this, request, mForegroundCount));

      rv = observer->OnStartRequest(request);
      if (NS_FAILED(rv)) {
        LOG(("LOADGROUP [%p]: OnStartRequest for request %p FAILED.\n", this,
             request));
        //
        // The URI load has been canceled by the observer.  Clean up
        // the damage...
        //

        mRequests.Remove(request);

        rv = NS_OK;

        if (foreground) {
          mForegroundCount -= 1;
        }
      }
    }

    // Ensure that we're part of our loadgroup while pending
    if (foreground && mForegroundCount == 1 && mLoadGroup) {
      mLoadGroup->AddRequest(this, nullptr);
    }
  }

  return rv;
}

NS_IMETHODIMP
nsLoadGroup::RemoveRequest(nsIRequest* request, nsISupports* ctxt,
                           nsresult aStatus) {
  // Make sure we have a owning reference to the request we're about
  // to remove.
  nsCOMPtr<nsIRequest> kungFuDeathGrip(request);

  nsresult rv = RemoveRequestFromHashtable(request, aStatus);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NotifyRemovalObservers(request, aStatus);
}

static uint64_t GetTransferSize(nsITimedChannel* aTimedChannel) {
  if (nsCOMPtr<nsIHttpChannel> channel = do_QueryInterface(aTimedChannel)) {
    uint64_t size = 0;
    (void)channel->GetTransferSize(&size);
    return size;
  }

  return 0;
}

nsresult nsLoadGroup::RemoveRequestFromHashtable(nsIRequest* request,
                                                 nsresult aStatus) {
  NS_ENSURE_ARG_POINTER(request);
  nsresult rv;

  if (MOZ_LOG_TEST(gLoadGroupLog, LogLevel::Debug)) {
    nsAutoCString nameStr;
    request->GetName(nameStr);
    LOG(("LOADGROUP [%p]: Removing request %p %s status %" PRIx32
         " (count=%d).\n",
         this, request, nameStr.get(), static_cast<uint32_t>(aStatus),
         mRequests.Count() - 1));
  }

  //
  // Remove the request from the group.  If this fails, it means that
  // the request was *not* in the group so do not update the foreground
  // count or it will get messed up...
  //
  bool found = mRequests.EnsureRemoved(request);

  if (!found) {
    LOG(("LOADGROUP [%p]: Unable to remove request %p. Not in group!\n", this,
         request));

    return NS_ERROR_FAILURE;
  }

  // Cache the status of mDefaultLoadRequest, It'll be used later in
  // TelemetryReport.
  if (request == mDefaultLoadRequest) {
    mDefaultStatus = aStatus;
  }

  // Collect telemetry stats only when default request is a timed channel.
  // Don't include failed requests in the timing statistics.
  if (mDefaultLoadIsTimed && NS_SUCCEEDED(aStatus)) {
    nsCOMPtr<nsITimedChannel> timedChannel = do_QueryInterface(request);
    if (timedChannel) {
      // Figure out if this request was served from the cache
      ++mTimedRequests;
      TimeStamp timeStamp;
      rv = timedChannel->GetCacheReadStart(&timeStamp);
      if (NS_SUCCEEDED(rv) && !timeStamp.IsNull()) {
        ++mCachedRequests;
      }

      if (request == mDefaultLoadRequest) {
        TelemetryReportChannel(timedChannel, true);
        mPageSize = GetTransferSize(timedChannel);
      } else {
        rv = timedChannel->GetAsyncOpen(&timeStamp);
        if (NS_SUCCEEDED(rv) && !timeStamp.IsNull()) {
          glean::http::subitem_open_latency_time.AccumulateRawDuration(
              timeStamp - mDefaultRequestCreationTime);
        }

        rv = timedChannel->GetResponseStart(&timeStamp);
        if (NS_SUCCEEDED(rv) && !timeStamp.IsNull()) {
          glean::http::subitem_first_byte_latency_time.AccumulateRawDuration(
              timeStamp - mDefaultRequestCreationTime);
        }

        TelemetryReportChannel(timedChannel, false);
        mTotalSubresourcesSize += GetTransferSize(timedChannel);
      }
    }
  }

  if (mRequests.Count() == 0) {
    TelemetryReport();
  }

  return NS_OK;
}

nsresult nsLoadGroup::NotifyRemovalObservers(nsIRequest* request,
                                             nsresult aStatus) {
  NS_ENSURE_ARG_POINTER(request);
  // Undo any group priority delta...
  if (mPriority != 0) RescheduleRequest(request, -mPriority);

  nsLoadFlags flags;
  nsresult rv = request->GetLoadFlags(&flags);
  if (NS_FAILED(rv)) return rv;

  bool foreground = !(flags & nsIRequest::LOAD_BACKGROUND);
  if (foreground) {
    NS_ASSERTION(mForegroundCount > 0, "ForegroundCount messed up");
    mForegroundCount -= 1;
  }

  if (foreground || mNotifyObserverAboutBackgroundRequests) {
    // Fire the OnStopRequest out to the observer...
    nsCOMPtr<nsIRequestObserver> observer = do_QueryReferent(mObserver);
    RefPtr<nsLoadGroup> self{this};
    if (observer) {
      LOG(
          ("LOADGROUP [%p]: Firing OnStopRequest for request %p."
           "(foreground count=%d).\n",
           this, request, mForegroundCount));

      rv = observer->OnStopRequest(request, aStatus);

      if (NS_FAILED(rv)) {
        LOG(("LOADGROUP [%p]: OnStopRequest for request %p FAILED.\n", this,
             request));
      }
    }

    // If that was the last request -> remove ourselves from loadgroup
    if (foreground && mForegroundCount == 0 && mLoadGroup) {
      mLoadGroup->RemoveRequest(this, nullptr, aStatus);
    }
  }

  return rv;
}

NS_IMETHODIMP
nsLoadGroup::GetRequests(nsISimpleEnumerator** aRequests) {
  nsCOMArray<nsIRequest> requests;
  requests.SetCapacity(mRequests.Count());

  for (nsIRequest* request : mRequests) {
    requests.AppendObject(request);
  }

  return NS_NewArrayEnumerator(aRequests, requests, NS_GET_IID(nsIRequest));
}

NS_IMETHODIMP
nsLoadGroup::GetTotalKeepAliveBytes(uint64_t* aTotalKeepAliveBytes) {
  MOZ_ASSERT(aTotalKeepAliveBytes);
  *aTotalKeepAliveBytes = mPendingKeepaliveRequestSize;
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetTotalKeepAliveBytes(uint64_t aTotalKeepAliveBytes) {
  mPendingKeepaliveRequestSize = aTotalKeepAliveBytes;
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetGroupObserver(nsIRequestObserver* aObserver) {
  SetGroupObserver(aObserver, false);
  return NS_OK;
}

void nsLoadGroup::SetGroupObserver(nsIRequestObserver* aObserver,
                                   bool aIncludeBackgroundRequests) {
  mObserver = do_GetWeakReference(aObserver);
  mNotifyObserverAboutBackgroundRequests = aIncludeBackgroundRequests;
}

NS_IMETHODIMP
nsLoadGroup::GetGroupObserver(nsIRequestObserver** aResult) {
  nsCOMPtr<nsIRequestObserver> observer = do_QueryReferent(mObserver);
  observer.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetActiveCount(uint32_t* aResult) {
  *aResult = mForegroundCount;
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetNotificationCallbacks(nsIInterfaceRequestor** aCallbacks) {
  NS_ENSURE_ARG_POINTER(aCallbacks);
  nsCOMPtr<nsIInterfaceRequestor> callbacks = mCallbacks;
  callbacks.forget(aCallbacks);
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetNotificationCallbacks(nsIInterfaceRequestor* aCallbacks) {
  mCallbacks = aCallbacks;
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetRequestContextID(uint64_t* aRCID) {
  if (!mRequestContext) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  *aRCID = mRequestContext->GetID();
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsILoadGroupChild methods:

NS_IMETHODIMP
nsLoadGroup::GetParentLoadGroup(nsILoadGroup** aParentLoadGroup) {
  *aParentLoadGroup = nullptr;
  nsCOMPtr<nsILoadGroup> parent = do_QueryReferent(mParentLoadGroup);
  if (!parent) return NS_OK;
  parent.forget(aParentLoadGroup);
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetParentLoadGroup(nsILoadGroup* aParentLoadGroup) {
  mParentLoadGroup = do_GetWeakReference(aParentLoadGroup);
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetChildLoadGroup(nsILoadGroup** aChildLoadGroup) {
  *aChildLoadGroup = do_AddRef(this).take();
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetRootLoadGroup(nsILoadGroup** aRootLoadGroup) {
  // first recursively try the root load group of our parent
  nsCOMPtr<nsILoadGroupChild> ancestor = do_QueryReferent(mParentLoadGroup);
  if (ancestor) return ancestor->GetRootLoadGroup(aRootLoadGroup);

  // next recursively try the root load group of our own load grop
  ancestor = do_QueryInterface(mLoadGroup);
  if (ancestor) return ancestor->GetRootLoadGroup(aRootLoadGroup);

  // finally just return this
  *aRootLoadGroup = do_AddRef(this).take();
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsISupportsPriority methods:

NS_IMETHODIMP
nsLoadGroup::GetPriority(int32_t* aValue) {
  *aValue = mPriority;
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetPriority(int32_t aValue) {
  return AdjustPriority(aValue - mPriority);
}

NS_IMETHODIMP
nsLoadGroup::AdjustPriority(int32_t aDelta) {
  // Update the priority for each request that supports nsISupportsPriority
  if (aDelta != 0) {
    mPriority += aDelta;
    for (nsIRequest* request : mRequests) {
      RescheduleRequest(request, aDelta);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetDefaultLoadFlags(uint32_t* aFlags) {
  *aFlags = mDefaultLoadFlags;
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::SetDefaultLoadFlags(uint32_t aFlags) {
  mDefaultLoadFlags = aFlags;
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////

void nsLoadGroup::TelemetryReport() {
  // We should only report HTTP_PAGE_* telemetry if the defaultRequest was
  // actually successful.
  if (mDefaultLoadIsTimed && NS_SUCCEEDED(mDefaultStatus)) {
    glean::http::request_per_page.AccumulateSingleSample(mTimedRequests);
    if (mTimedRequests) {
      glean::http::request_per_page_from_cache.AccumulateSingleSample(
          mCachedRequests * 100 / mTimedRequests);
    }
  }

  mTimedRequests = 0;
  mCachedRequests = 0;
  mDefaultLoadIsTimed = false;
}

void nsLoadGroup::TelemetryReportChannel(nsITimedChannel* aTimedChannel,
                                         bool aDefaultRequest) {
  nsresult rv;

  TimeStamp asyncOpen;
  rv = aTimedChannel->GetAsyncOpen(&asyncOpen);
  // We do not check !asyncOpen.IsNull() bellow, prevent ASSERTIONs this way
  if (NS_FAILED(rv) || asyncOpen.IsNull()) return;

  TimeStamp cacheReadStart;
  rv = aTimedChannel->GetCacheReadStart(&cacheReadStart);
  if (NS_FAILED(rv)) return;

  TimeStamp cacheReadEnd;
  rv = aTimedChannel->GetCacheReadEnd(&cacheReadEnd);
  if (NS_FAILED(rv)) return;

  TimeStamp domainLookupStart;
  rv = aTimedChannel->GetDomainLookupStart(&domainLookupStart);
  if (NS_FAILED(rv)) return;

  TimeStamp domainLookupEnd;
  rv = aTimedChannel->GetDomainLookupEnd(&domainLookupEnd);
  if (NS_FAILED(rv)) return;

  TimeStamp connectStart;
  rv = aTimedChannel->GetConnectStart(&connectStart);
  if (NS_FAILED(rv)) return;

  TimeStamp secureConnectionStart;
  rv = aTimedChannel->GetSecureConnectionStart(&secureConnectionStart);
  if (NS_FAILED(rv)) return;

  TimeStamp connectEnd;
  rv = aTimedChannel->GetConnectEnd(&connectEnd);
  if (NS_FAILED(rv)) return;

  TimeStamp requestStart;
  rv = aTimedChannel->GetRequestStart(&requestStart);
  if (NS_FAILED(rv)) return;

  TimeStamp responseStart;
  rv = aTimedChannel->GetResponseStart(&responseStart);
  if (NS_FAILED(rv)) return;

  TimeStamp responseEnd;
  rv = aTimedChannel->GetResponseEnd(&responseEnd);
  if (NS_FAILED(rv)) return;
#ifndef ANDROID
  bool useHttp3 = false;
#endif
  bool supportHttp3 = false;
  nsCOMPtr<nsIHttpChannelInternal> httpChannel =
      do_QueryInterface(aTimedChannel);
  if (httpChannel) {
    uint32_t major;
    uint32_t minor;
    if (NS_SUCCEEDED(httpChannel->GetResponseVersion(&major, &minor))) {
#ifndef ANDROID
      useHttp3 = major == 3;
#endif
      if (major == 2) {
        if (NS_FAILED(httpChannel->GetSupportsHTTP3(&supportHttp3))) {
          supportHttp3 = false;
        }
      }
    }
  }

  // Glean instrumentation of metrics previously collected via Geckoview
  // Streaming.
  if (!domainLookupStart.IsNull()) {
    if (aDefaultRequest) {
      mozilla::glean::network::dns_start.AccumulateRawDuration(
          domainLookupStart - asyncOpen);
      if (!domainLookupEnd.IsNull()) {
        mozilla::glean::network::dns_end.AccumulateRawDuration(
            domainLookupEnd - domainLookupStart);
      }
    }
#ifndef ANDROID
    else {
      mozilla::glean::network::sub_dns_start.AccumulateRawDuration(
          domainLookupStart - asyncOpen);
      if (!domainLookupEnd.IsNull()) {
        mozilla::glean::network::sub_dns_end.AccumulateRawDuration(
            domainLookupEnd - domainLookupStart);
      }
    }
#endif
  }
  if (!connectEnd.IsNull()) {
    if (!connectStart.IsNull()) {
      if (aDefaultRequest) {
        mozilla::glean::network::tcp_connection.AccumulateRawDuration(
            connectEnd - connectStart);
      }
#ifndef ANDROID
      else {
        mozilla::glean::network::sub_tcp_connection.AccumulateRawDuration(
            connectEnd - connectStart);
      }
#endif
    }
    if (!secureConnectionStart.IsNull()) {
      if (aDefaultRequest) {
        mozilla::glean::network::tls_handshake.AccumulateRawDuration(
            connectEnd - secureConnectionStart);
      }
#ifndef ANDROID
      else {
        mozilla::glean::network::sub_tls_handshake.AccumulateRawDuration(
            connectEnd - secureConnectionStart);
      }
#endif
    }
  }
  if (!requestStart.IsNull() && !responseEnd.IsNull()) {
    if (aDefaultRequest) {
      mozilla::glean::network::open_to_first_sent.AccumulateRawDuration(
          requestStart - asyncOpen);
      mozilla::glean::network::first_sent_to_last_received
          .AccumulateRawDuration(responseEnd - requestStart);

      if (cacheReadStart.IsNull() && !responseStart.IsNull()) {
        mozilla::glean::network::open_to_first_received.AccumulateRawDuration(
            responseStart - asyncOpen);
      }
    }
#ifndef ANDROID
    else {
      mozilla::glean::network::sub_open_to_first_sent.AccumulateRawDuration(
          requestStart - asyncOpen);
      mozilla::glean::network::sub_first_sent_to_last_received
          .AccumulateRawDuration(responseEnd - requestStart);
      if (cacheReadStart.IsNull() && !responseStart.IsNull()) {
        mozilla::glean::network::sub_open_to_first_received
            .AccumulateRawDuration(responseStart - asyncOpen);
      }
    }
#endif
  }
  if (!cacheReadStart.IsNull() && !cacheReadEnd.IsNull()) {
    if (aDefaultRequest) {
      mozilla::glean::network::first_from_cache.AccumulateRawDuration(
          cacheReadStart - asyncOpen);
#ifndef ANDROID
      mozilla::glean::network::cache_read_time.AccumulateRawDuration(
          cacheReadEnd - cacheReadStart);
      if (!requestStart.IsNull() && !responseEnd.IsNull()) {
        mozilla::glean::network::http_revalidation.AccumulateRawDuration(
            responseEnd - requestStart);
      }
#endif
    }
#ifndef ANDROID
    else {
      mozilla::glean::network::sub_first_from_cache.AccumulateRawDuration(
          cacheReadStart - asyncOpen);
      mozilla::glean::network::sub_cache_read_time.AccumulateRawDuration(
          cacheReadEnd - cacheReadStart);
      if (!requestStart.IsNull() && !responseEnd.IsNull()) {
        mozilla::glean::network::sub_http_revalidation.AccumulateRawDuration(
            responseEnd - requestStart);
      }
    }
#endif
  }
#ifndef ANDROID
  if (!cacheReadEnd.IsNull()) {
    if (aDefaultRequest) {
      mozilla::glean::network::complete_load.AccumulateRawDuration(
          cacheReadEnd - asyncOpen);
      mozilla::glean::network::complete_load_cached.AccumulateRawDuration(
          cacheReadEnd - asyncOpen);
    } else {
      mozilla::glean::network::sub_complete_load.AccumulateRawDuration(
          cacheReadEnd - asyncOpen);
      mozilla::glean::network::sub_complete_load_cached.AccumulateRawDuration(
          cacheReadEnd - asyncOpen);
    }
  } else if (!responseEnd.IsNull()) {
    if (aDefaultRequest) {
      mozilla::glean::network::complete_load.AccumulateRawDuration(responseEnd -
                                                                   asyncOpen);
      mozilla::glean::network::complete_load_net.AccumulateRawDuration(
          responseEnd - asyncOpen);
    } else {
      mozilla::glean::network::sub_complete_load.AccumulateRawDuration(
          responseEnd - asyncOpen);
      mozilla::glean::network::sub_complete_load_net.AccumulateRawDuration(
          responseEnd - asyncOpen);
    }
  }
#endif

#ifndef ANDROID
  if ((useHttp3 || supportHttp3) && cacheReadStart.IsNull() &&
      cacheReadEnd.IsNull()) {
    nsCString key = (useHttp3) ? ((aDefaultRequest) ? "uses_http3_page"_ns
                                                    : "uses_http3_sub"_ns)
                               : ((aDefaultRequest) ? "supports_http3_page"_ns
                                                    : "supports_http3_sub"_ns);

    if (!secureConnectionStart.IsNull() && !connectEnd.IsNull()) {
      mozilla::glean::network::http3_tls_handshake.Get(key)
          .AccumulateRawDuration(connectEnd - secureConnectionStart);
    }

    if (supportHttp3 && !connectStart.IsNull() && !connectEnd.IsNull()) {
      mozilla::glean::network::sup_http3_tcp_connection.Get(key)
          .AccumulateRawDuration(connectEnd - connectStart);
    }

    if (!requestStart.IsNull() && !responseEnd.IsNull()) {
      mozilla::glean::network::http3_open_to_first_sent.Get(key)
          .AccumulateRawDuration(requestStart - asyncOpen);

      mozilla::glean::network::http3_first_sent_to_last_received.Get(key)
          .AccumulateRawDuration(responseEnd - requestStart);

      if (!responseStart.IsNull()) {
        mozilla::glean::network::http3_open_to_first_received.Get(key)
            .AccumulateRawDuration(responseStart - asyncOpen);
      }

      if (!responseEnd.IsNull()) {
        mozilla::glean::network::http3_complete_load.Get(key)
            .AccumulateRawDuration(responseEnd - asyncOpen);
      }
    }
  }
#endif

  bool hasHTTPSRR = false;
  if (httpChannel && NS_SUCCEEDED(httpChannel->GetHasHTTPSRR(&hasHTTPSRR)) &&
      cacheReadStart.IsNull() && cacheReadEnd.IsNull() &&
      !requestStart.IsNull()) {
    TimeDuration elapsed = requestStart - asyncOpen;
    if (hasHTTPSRR) {
      if (aDefaultRequest) {
        glean::networking::http_channel_page_open_to_first_sent_https_rr
            .AccumulateRawDuration(elapsed);
      } else {
        glean::networking::http_channel_sub_open_to_first_sent_https_rr
            .AccumulateRawDuration(elapsed);
      }
    } else {
      if (aDefaultRequest) {
        glean::networking::http_channel_page_open_to_first_sent
            .AccumulateRawDuration(elapsed);
      } else {
        glean::networking::http_channel_sub_open_to_first_sent
            .AccumulateRawDuration(elapsed);
      }
    }
  }
}

nsresult nsLoadGroup::MergeLoadFlags(nsIRequest* aRequest,
                                     nsLoadFlags& outFlags) {
  nsresult rv;
  nsLoadFlags flags, oldFlags;

  rv = aRequest->GetLoadFlags(&flags);
  if (NS_FAILED(rv)) {
    return rv;
  }

  oldFlags = flags;

  // Inherit some bits...
  flags |= mLoadFlags & kInheritedLoadFlags;

  // ... and force the default flags.
  flags |= mDefaultLoadFlags;

  if (flags != oldFlags) {
    rv = aRequest->SetLoadFlags(flags);
  }

  outFlags = flags;
  return rv;
}

nsresult nsLoadGroup::MergeDefaultLoadFlags(nsIRequest* aRequest,
                                            nsLoadFlags& outFlags) {
  nsresult rv;
  nsLoadFlags flags, oldFlags;

  rv = aRequest->GetLoadFlags(&flags);
  if (NS_FAILED(rv)) {
    return rv;
  }

  oldFlags = flags;
  // ... and force the default flags.
  flags |= mDefaultLoadFlags;

  if (flags != oldFlags) {
    rv = aRequest->SetLoadFlags(flags);
  }
  outFlags = flags;
  return rv;
}

nsresult nsLoadGroup::Init() {
  mRequestContextService = RequestContextService::GetOrCreate();
  if (mRequestContextService) {
    (void)mRequestContextService->NewRequestContext(
        getter_AddRefs(mRequestContext));
  }

  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  NS_ENSURE_STATE(os);

  (void)os->AddObserver(this, "last-pb-context-exited", true);

  return NS_OK;
}

nsresult nsLoadGroup::InitWithRequestContextId(
    const uint64_t& aRequestContextId) {
  mRequestContextService = RequestContextService::GetOrCreate();
  if (mRequestContextService) {
    (void)mRequestContextService->GetRequestContext(
        aRequestContextId, getter_AddRefs(mRequestContext));
  }
  mExternalRequestContext = true;

  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  NS_ENSURE_STATE(os);

  (void)os->AddObserver(this, "last-pb-context-exited", true);

  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::Observe(nsISupports* aSubject, const char* aTopic,
                     const char16_t* aData) {
  MOZ_ASSERT(!strcmp(aTopic, "last-pb-context-exited"));

  OriginAttributes attrs;
  StoragePrincipalHelper::GetRegularPrincipalOriginAttributes(this, attrs);
  if (!attrs.IsPrivateBrowsing()) {
    return NS_OK;
  }

  mBrowsingContextDiscarded = true;
  return NS_OK;
}

NS_IMETHODIMP
nsLoadGroup::GetIsBrowsingContextDiscarded(bool* aIsBrowsingContextDiscarded) {
  *aIsBrowsingContextDiscarded = mBrowsingContextDiscarded;
  return NS_OK;
}

}  // namespace net
}  // namespace mozilla

#undef LOG
