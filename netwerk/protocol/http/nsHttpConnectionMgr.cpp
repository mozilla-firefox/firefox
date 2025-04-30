/* vim:set ts=4 sw=2 sts=2 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

// Log on level :5, instead of default :4.
#undef LOG
#define LOG(args) LOG5(args)
#undef LOG_ENABLED
#define LOG_ENABLED() LOG5_ENABLED()

#include <algorithm>
#include <utility>

#include "ConnectionHandle.h"
#include "HttpConnectionUDP.h"
#include "NullHttpTransaction.h"
#include "SpeculativeTransaction.h"
#include "mozilla/Components.h"
#include "mozilla/PerfStats.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "mozilla/Unused.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/net/DNS.h"
#include "mozilla/net/DashboardTypes.h"
#include "nsCOMPtr.h"
#include "nsHttpConnectionMgr.h"
#include "nsHttpHandler.h"
#include "nsIClassOfService.h"
#include "nsIDNSByTypeRecord.h"
#include "nsIDNSListener.h"
#include "nsIDNSRecord.h"
#include "nsIDNSService.h"
#include "nsIHttpChannelInternal.h"
#include "nsIPipe.h"
#include "nsIRequestContext.h"
#include "nsISocketTransport.h"
#include "nsISocketTransportService.h"
#include "nsITransport.h"
#include "nsIXPConnect.h"
#include "nsInterfaceRequestorAgg.h"
#include "nsNetCID.h"
#include "nsNetSegmentUtils.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"
#include "nsSocketTransportService2.h"
#include "nsStreamUtils.h"

using namespace mozilla;

namespace geckoprofiler::markers {

struct UrlMarker {
  static constexpr Span<const char> MarkerTypeName() {
    return MakeStringSpan("Url");
  }
  static void StreamJSONMarkerData(
      mozilla::baseprofiler::SpliceableJSONWriter& aWriter,
      const mozilla::ProfilerString8View& aURL, const TimeDuration& aDuration,
      uint64_t aChannelId) {
    if (aURL.Length() != 0) {
      aWriter.StringProperty("url", aURL);
    }
    if (!aDuration.IsZero()) {
      aWriter.DoubleProperty("duration", aDuration.ToMilliseconds());
    }
    aWriter.IntProperty("channelId", static_cast<int64_t>(aChannelId));
  }
  static MarkerSchema MarkerTypeDisplay() {
    using MS = MarkerSchema;
    MS schema(MS::Location::MarkerChart, MS::Location::MarkerTable);
    schema.SetTableLabel("{marker.name} - {marker.data.url}");
    schema.AddKeyFormatSearchable("url", MS::Format::Url,
                                  MS::Searchable::Searchable);
    schema.AddKeyLabelFormat("duration", "Duration", MS::Format::Duration);
    return schema;
  }
};

}  // namespace geckoprofiler::markers

namespace mozilla::net {

//-----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(nsHttpConnectionMgr, nsIObserver, nsINamed)

//-----------------------------------------------------------------------------

nsHttpConnectionMgr::nsHttpConnectionMgr() {
  LOG(("Creating nsHttpConnectionMgr @%p\n", this));
}

nsHttpConnectionMgr::~nsHttpConnectionMgr() {
  LOG(("Destroying nsHttpConnectionMgr @%p\n", this));
  MOZ_ASSERT(mCoalescingHash.Count() == 0);
  if (mTimeoutTick) mTimeoutTick->Cancel();
}

nsresult nsHttpConnectionMgr::EnsureSocketThreadTarget() {
  nsCOMPtr<nsIEventTarget> sts;
  nsCOMPtr<nsIIOService> ioService = components::IO::Service();
  if (ioService) {
    nsCOMPtr<nsISocketTransportService> realSTS =
        components::SocketTransport::Service();
    sts = do_QueryInterface(realSTS);
  }

  ReentrantMonitorAutoEnter mon(mReentrantMonitor);

  // do nothing if already initialized or if we've shut down
  if (mSocketThreadTarget || mIsShuttingDown) return NS_OK;

  mSocketThreadTarget = sts;

  return sts ? NS_OK : NS_ERROR_NOT_AVAILABLE;
}

nsresult nsHttpConnectionMgr::Init(
    uint16_t maxUrgentExcessiveConns, uint16_t maxConns,
    uint16_t maxPersistConnsPerHost, uint16_t maxPersistConnsPerProxy,
    uint16_t maxRequestDelay, bool throttleEnabled, uint32_t throttleSuspendFor,
    uint32_t throttleResumeFor, uint32_t throttleHoldTime,
    uint32_t throttleMaxTime, bool beConservativeForProxy) {
  LOG(("nsHttpConnectionMgr::Init\n"));

  {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);

    mMaxUrgentExcessiveConns = maxUrgentExcessiveConns;
    mMaxConns = maxConns;
    mMaxPersistConnsPerHost = maxPersistConnsPerHost;
    mMaxPersistConnsPerProxy = maxPersistConnsPerProxy;
    mMaxRequestDelay = maxRequestDelay;

    mThrottleEnabled = throttleEnabled;
    mThrottleSuspendFor = throttleSuspendFor;
    mThrottleResumeFor = throttleResumeFor;
    mThrottleHoldTime = throttleHoldTime;
    mThrottleMaxTime = TimeDuration::FromMilliseconds(throttleMaxTime);

    mBeConservativeForProxy = beConservativeForProxy;

    mIsShuttingDown = false;
  }

  return EnsureSocketThreadTarget();
}

class BoolWrapper : public ARefBase {
 public:
  BoolWrapper() = default;
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(BoolWrapper, override)

 public:  // intentional!
  bool mBool{false};

 private:
  virtual ~BoolWrapper() = default;
};

nsresult nsHttpConnectionMgr::Shutdown() {
  LOG(("nsHttpConnectionMgr::Shutdown\n"));

  RefPtr<BoolWrapper> shutdownWrapper = new BoolWrapper();
  {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);

    // do nothing if already shutdown
    if (!mSocketThreadTarget) return NS_OK;

    nsresult rv =
        PostEvent(&nsHttpConnectionMgr::OnMsgShutdown, 0, shutdownWrapper);

    // release our reference to the STS to prevent further events
    // from being posted.  this is how we indicate that we are
    // shutting down.
    mIsShuttingDown = true;
    mSocketThreadTarget = nullptr;

    if (NS_FAILED(rv)) {
      NS_WARNING("unable to post SHUTDOWN message");
      return rv;
    }
  }

  // wait for shutdown event to complete
  SpinEventLoopUntil("nsHttpConnectionMgr::Shutdown"_ns,
                     [&, shutdownWrapper]() { return shutdownWrapper->mBool; });

  return NS_OK;
}

class ConnEvent : public Runnable {
 public:
  ConnEvent(nsHttpConnectionMgr* mgr, nsConnEventHandler handler,
            int32_t iparam, ARefBase* vparam)
      : Runnable("net::ConnEvent"),
        mMgr(mgr),
        mHandler(handler),
        mIParam(iparam),
        mVParam(vparam) {}

  NS_IMETHOD Run() override {
    (mMgr->*mHandler)(mIParam, mVParam);
    return NS_OK;
  }

 private:
  virtual ~ConnEvent() = default;

  RefPtr<nsHttpConnectionMgr> mMgr;
  nsConnEventHandler mHandler;
  int32_t mIParam;
  RefPtr<ARefBase> mVParam;
};

nsresult nsHttpConnectionMgr::PostEvent(nsConnEventHandler handler,
                                        int32_t iparam, ARefBase* vparam) {
  Unused << EnsureSocketThreadTarget();

  nsCOMPtr<nsIEventTarget> target;
  {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    target = mSocketThreadTarget;
  }

  if (!target) {
    NS_WARNING("cannot post event if not initialized");
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsCOMPtr<nsIRunnable> event = new ConnEvent(this, handler, iparam, vparam);
  return target->Dispatch(event, NS_DISPATCH_NORMAL);
}

void nsHttpConnectionMgr::PruneDeadConnectionsAfter(uint32_t timeInSeconds) {
  LOG(("nsHttpConnectionMgr::PruneDeadConnectionsAfter\n"));

  if (!mTimer) mTimer = NS_NewTimer();

  // failure to create a timer is not a fatal error, but idle connections
  // will not be cleaned up until we try to use them.
  if (mTimer) {
    mTimeOfNextWakeUp = timeInSeconds + NowInSeconds();
    mTimer->Init(this, timeInSeconds * 1000, nsITimer::TYPE_ONE_SHOT);
  } else {
    NS_WARNING("failed to create: timer for pruning the dead connections!");
  }
}

void nsHttpConnectionMgr::ConditionallyStopPruneDeadConnectionsTimer() {
  // Leave the timer in place if there are connections that potentially
  // need management
  if (mNumIdleConns ||
      (mNumActiveConns && StaticPrefs::network_http_http2_enabled())) {
    return;
  }

  LOG(("nsHttpConnectionMgr::StopPruneDeadConnectionsTimer\n"));

  // Reset mTimeOfNextWakeUp so that we can find a new shortest value.
  mTimeOfNextWakeUp = UINT64_MAX;
  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }
}

void nsHttpConnectionMgr::ConditionallyStopTimeoutTick() {
  LOG(
      ("nsHttpConnectionMgr::ConditionallyStopTimeoutTick "
       "armed=%d active=%d\n",
       mTimeoutTickArmed, mNumActiveConns));

  if (!mTimeoutTickArmed) return;

  if (mNumActiveConns) return;

  LOG(("nsHttpConnectionMgr::ConditionallyStopTimeoutTick stop==true\n"));

  mTimeoutTick->Cancel();
  mTimeoutTickArmed = false;
}

//-----------------------------------------------------------------------------
// nsHttpConnectionMgr::nsINamed
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpConnectionMgr::GetName(nsACString& aName) {
  aName.AssignLiteral("nsHttpConnectionMgr");
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpConnectionMgr::nsIObserver
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpConnectionMgr::Observe(nsISupports* subject, const char* topic,
                             const char16_t* data) {
  LOG(("nsHttpConnectionMgr::Observe [topic=\"%s\"]\n", topic));

  if (0 == strcmp(topic, NS_TIMER_CALLBACK_TOPIC)) {
    nsCOMPtr<nsITimer> timer = do_QueryInterface(subject);
    if (timer == mTimer) {
      Unused << PruneDeadConnections();
    } else if (timer == mTimeoutTick) {
      TimeoutTick();
    } else if (timer == mTrafficTimer) {
      Unused << PruneNoTraffic();
    } else if (timer == mThrottleTicker) {
      ThrottlerTick();
    } else if (timer == mDelayedResumeReadTimer) {
      ResumeBackgroundThrottledTransactions();
    } else {
      MOZ_ASSERT(false, "unexpected timer-callback");
      LOG(("Unexpected timer object\n"));
      return NS_ERROR_UNEXPECTED;
    }
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------

nsresult nsHttpConnectionMgr::AddTransaction(HttpTransactionShell* trans,
                                             int32_t priority) {
  LOG(("nsHttpConnectionMgr::AddTransaction [trans=%p %d]\n", trans, priority));
  // Make sure a transaction is not in a pending queue.
  CheckTransInPendingQueue(trans->AsHttpTransaction());
  return PostEvent(&nsHttpConnectionMgr::OnMsgNewTransaction, priority,
                   trans->AsHttpTransaction());
}

class NewTransactionData : public ARefBase {
 public:
  NewTransactionData(nsHttpTransaction* trans, int32_t priority,
                     nsHttpTransaction* transWithStickyConn)
      : mTrans(trans),
        mPriority(priority),
        mTransWithStickyConn(transWithStickyConn) {}

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(NewTransactionData, override)

  RefPtr<nsHttpTransaction> mTrans;
  int32_t mPriority;
  RefPtr<nsHttpTransaction> mTransWithStickyConn;

 private:
  virtual ~NewTransactionData() = default;
};

nsresult nsHttpConnectionMgr::AddTransactionWithStickyConn(
    HttpTransactionShell* trans, int32_t priority,
    HttpTransactionShell* transWithStickyConn) {
  LOG(
      ("nsHttpConnectionMgr::AddTransactionWithStickyConn "
       "[trans=%p %d transWithStickyConn=%p]\n",
       trans, priority, transWithStickyConn));
  // Make sure a transaction is not in a pending queue.
  CheckTransInPendingQueue(trans->AsHttpTransaction());

  RefPtr<NewTransactionData> data =
      new NewTransactionData(trans->AsHttpTransaction(), priority,
                             transWithStickyConn->AsHttpTransaction());
  return PostEvent(&nsHttpConnectionMgr::OnMsgNewTransactionWithStickyConn, 0,
                   data);
}

nsresult nsHttpConnectionMgr::RescheduleTransaction(HttpTransactionShell* trans,
                                                    int32_t priority) {
  LOG(("nsHttpConnectionMgr::RescheduleTransaction [trans=%p %d]\n", trans,
       priority));
  return PostEvent(&nsHttpConnectionMgr::OnMsgReschedTransaction, priority,
                   trans->AsHttpTransaction());
}

void nsHttpConnectionMgr::UpdateClassOfServiceOnTransaction(
    HttpTransactionShell* trans, const ClassOfService& classOfService) {
  LOG(
      ("nsHttpConnectionMgr::UpdateClassOfServiceOnTransaction [trans=%p "
       "classOfService flags=%" PRIu32 " inc=%d]\n",
       trans, static_cast<uint32_t>(classOfService.Flags()),
       classOfService.Incremental()));

  Unused << EnsureSocketThreadTarget();

  nsCOMPtr<nsIEventTarget> target;
  {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    target = mSocketThreadTarget;
  }

  if (!target) {
    NS_WARNING("cannot post event if not initialized");
    return;
  }

  RefPtr<nsHttpConnectionMgr> self(this);
  Unused << target->Dispatch(NS_NewRunnableFunction(
      "nsHttpConnectionMgr::CallUpdateClassOfServiceOnTransaction",
      [cos{classOfService}, self{std::move(self)}, trans = RefPtr{trans}]() {
        self->OnMsgUpdateClassOfServiceOnTransaction(
            cos, trans->AsHttpTransaction());
      }));
}

nsresult nsHttpConnectionMgr::CancelTransaction(HttpTransactionShell* trans,
                                                nsresult reason) {
  LOG(("nsHttpConnectionMgr::CancelTransaction [trans=%p reason=%" PRIx32 "]\n",
       trans, static_cast<uint32_t>(reason)));
  return PostEvent(&nsHttpConnectionMgr::OnMsgCancelTransaction,
                   static_cast<int32_t>(reason), trans->AsHttpTransaction());
}

nsresult nsHttpConnectionMgr::PruneDeadConnections() {
  return PostEvent(&nsHttpConnectionMgr::OnMsgPruneDeadConnections);
}

//
// Called after a timeout. Check for active connections that have had no
// traffic since they were "marked" and nuke them.
nsresult nsHttpConnectionMgr::PruneNoTraffic() {
  LOG(("nsHttpConnectionMgr::PruneNoTraffic\n"));
  mPruningNoTraffic = true;
  return PostEvent(&nsHttpConnectionMgr::OnMsgPruneNoTraffic);
}

nsresult nsHttpConnectionMgr::VerifyTraffic() {
  LOG(("nsHttpConnectionMgr::VerifyTraffic\n"));
  return PostEvent(&nsHttpConnectionMgr::OnMsgVerifyTraffic);
}

nsresult nsHttpConnectionMgr::DoShiftReloadConnectionCleanup() {
  return PostEvent(&nsHttpConnectionMgr::OnMsgDoShiftReloadConnectionCleanup, 0,
                   nullptr);
}

nsresult nsHttpConnectionMgr::DoShiftReloadConnectionCleanupWithConnInfo(
    nsHttpConnectionInfo* aCI) {
  if (!aCI) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<nsHttpConnectionInfo> ci = aCI->Clone();
  return PostEvent(&nsHttpConnectionMgr::OnMsgDoShiftReloadConnectionCleanup, 0,
                   ci);
}

nsresult nsHttpConnectionMgr::DoSingleConnectionCleanup(
    nsHttpConnectionInfo* aCI) {
  if (!aCI) {
    return NS_ERROR_INVALID_ARG;
  }

  RefPtr<nsHttpConnectionInfo> ci = aCI->Clone();
  return PostEvent(&nsHttpConnectionMgr::OnMsgDoSingleConnectionCleanup, 0, ci);
}

class SpeculativeConnectArgs : public ARefBase {
 public:
  SpeculativeConnectArgs() = default;
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(SpeculativeConnectArgs, override)

 public:  // intentional!
  RefPtr<SpeculativeTransaction> mTrans;

  bool mFetchHTTPSRR{false};

 private:
  virtual ~SpeculativeConnectArgs() = default;
  NS_DECL_OWNINGTHREAD
};

nsresult nsHttpConnectionMgr::SpeculativeConnect(
    nsHttpConnectionInfo* ci, nsIInterfaceRequestor* callbacks, uint32_t caps,
    SpeculativeTransaction* aTransaction, bool aFetchHTTPSRR) {
  if (!IsNeckoChild() && NS_IsMainThread()) {
    // HACK: make sure PSM gets initialized on the main thread.
    net_EnsurePSMInit();
  }

  LOG(("nsHttpConnectionMgr::SpeculativeConnect [ci=%s]\n",
       ci->HashKey().get()));

  nsCOMPtr<nsISpeculativeConnectionOverrider> overrider =
      do_GetInterface(callbacks);

  bool allow1918 = overrider ? overrider->GetAllow1918() : false;

  // Hosts that are Local IP Literals should not be speculatively
  // connected - Bug 853423.
  if ((!allow1918) && ci && ci->HostIsLocalIPLiteral()) {
    LOG(
        ("nsHttpConnectionMgr::SpeculativeConnect skipping RFC1918 "
         "address [%s]",
         ci->Origin()));
    return NS_OK;
  }

  nsAutoCString url(ci->EndToEndSSL() ? "https://"_ns : "http://"_ns);
  url += ci->GetOrigin();
  PROFILER_MARKER("SpeculativeConnect", NETWORK, {}, UrlMarker, url,
                  TimeDuration::Zero(), 0);

  RefPtr<SpeculativeConnectArgs> args = new SpeculativeConnectArgs();

  // Wrap up the callbacks and the target to ensure they're released on the
  // target thread properly.
  nsCOMPtr<nsIInterfaceRequestor> wrappedCallbacks;
  NS_NewInterfaceRequestorAggregation(callbacks, nullptr,
                                      getter_AddRefs(wrappedCallbacks));

  caps |= ci->GetAnonymous() ? NS_HTTP_LOAD_ANONYMOUS : 0;
  caps |= NS_HTTP_ERROR_SOFTLY;
  args->mTrans = aTransaction
                     ? aTransaction
                     : new SpeculativeTransaction(ci, wrappedCallbacks, caps);
  args->mFetchHTTPSRR = aFetchHTTPSRR;

  if (overrider) {
    args->mTrans->SetParallelSpeculativeConnectLimit(
        overrider->GetParallelSpeculativeConnectLimit());
    args->mTrans->SetIgnoreIdle(overrider->GetIgnoreIdle());
    args->mTrans->SetIsFromPredictor(overrider->GetIsFromPredictor());
    args->mTrans->SetAllow1918(overrider->GetAllow1918());
  }

  return PostEvent(&nsHttpConnectionMgr::OnMsgSpeculativeConnect, 0, args);
}

nsresult nsHttpConnectionMgr::GetSocketThreadTarget(nsIEventTarget** target) {
  Unused << EnsureSocketThreadTarget();

  ReentrantMonitorAutoEnter mon(mReentrantMonitor);
  nsCOMPtr<nsIEventTarget> temp(mSocketThreadTarget);
  temp.forget(target);
  return NS_OK;
}

nsresult nsHttpConnectionMgr::ReclaimConnection(HttpConnectionBase* conn) {
  LOG(("nsHttpConnectionMgr::ReclaimConnection [conn=%p]\n", conn));

  Unused << EnsureSocketThreadTarget();

  nsCOMPtr<nsIEventTarget> target;
  {
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    target = mSocketThreadTarget;
  }

  if (!target) {
    NS_WARNING("cannot post event if not initialized");
    return NS_ERROR_NOT_INITIALIZED;
  }

  RefPtr<HttpConnectionBase> connRef(conn);
  RefPtr<nsHttpConnectionMgr> self(this);
  return target->Dispatch(NS_NewRunnableFunction(
      "nsHttpConnectionMgr::CallReclaimConnection",
      [conn{std::move(connRef)}, self{std::move(self)}]() {
        self->OnMsgReclaimConnection(conn);
      }));
}

// A structure used to marshall 6 pointers across the various necessary
// threads to complete an HTTP upgrade.
class nsCompleteUpgradeData : public ARefBase {
 public:
  nsCompleteUpgradeData(nsHttpTransaction* aTrans,
                        nsIHttpUpgradeListener* aListener, bool aJsWrapped)
      : mTrans(aTrans), mUpgradeListener(aListener), mJsWrapped(aJsWrapped) {}

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(nsCompleteUpgradeData, override)

  RefPtr<nsHttpTransaction> mTrans;
  nsCOMPtr<nsIHttpUpgradeListener> mUpgradeListener;

  nsCOMPtr<nsISocketTransport> mSocketTransport;
  nsCOMPtr<nsIAsyncInputStream> mSocketIn;
  nsCOMPtr<nsIAsyncOutputStream> mSocketOut;

  bool mJsWrapped;

 private:
  virtual ~nsCompleteUpgradeData() {
    NS_ReleaseOnMainThread("nsCompleteUpgradeData.mUpgradeListener",
                           mUpgradeListener.forget());
  }
};

nsresult nsHttpConnectionMgr::CompleteUpgrade(
    HttpTransactionShell* aTrans, nsIHttpUpgradeListener* aUpgradeListener) {
  // test if aUpgradeListener is a wrapped JsObject
  nsCOMPtr<nsIXPConnectWrappedJS> wrapper = do_QueryInterface(aUpgradeListener);

  bool wrapped = !!wrapper;

  RefPtr<nsCompleteUpgradeData> data = new nsCompleteUpgradeData(
      aTrans->AsHttpTransaction(), aUpgradeListener, wrapped);
  return PostEvent(&nsHttpConnectionMgr::OnMsgCompleteUpgrade, 0, data);
}

nsresult nsHttpConnectionMgr::UpdateParam(nsParamName name, uint16_t value) {
  uint32_t param = (uint32_t(name) << 16) | uint32_t(value);
  return PostEvent(&nsHttpConnectionMgr::OnMsgUpdateParam,
                   static_cast<int32_t>(param), nullptr);
}

nsresult nsHttpConnectionMgr::ProcessPendingQ(nsHttpConnectionInfo* aCI) {
  LOG(("nsHttpConnectionMgr::ProcessPendingQ [ci=%s]\n", aCI->HashKey().get()));
  RefPtr<nsHttpConnectionInfo> ci;
  if (aCI) {
    ci = aCI->Clone();
  }
  return PostEvent(&nsHttpConnectionMgr::OnMsgProcessPendingQ, 0, ci);
}

nsresult nsHttpConnectionMgr::ProcessPendingQ() {
  LOG(("nsHttpConnectionMgr::ProcessPendingQ [All CI]\n"));
  return PostEvent(&nsHttpConnectionMgr::OnMsgProcessPendingQ, 0, nullptr);
}

void nsHttpConnectionMgr::OnMsgUpdateRequestTokenBucket(int32_t,
                                                        ARefBase* param) {
  EventTokenBucket* tokenBucket = static_cast<EventTokenBucket*>(param);
  gHttpHandler->SetRequestTokenBucket(tokenBucket);
}

nsresult nsHttpConnectionMgr::UpdateRequestTokenBucket(
    EventTokenBucket* aBucket) {
  // Call From main thread when a new EventTokenBucket has been made in order
  // to post the new value to the socket thread.
  return PostEvent(&nsHttpConnectionMgr::OnMsgUpdateRequestTokenBucket, 0,
                   aBucket);
}

nsresult nsHttpConnectionMgr::ClearConnectionHistory() {
  return PostEvent(&nsHttpConnectionMgr::OnMsgClearConnectionHistory, 0,
                   nullptr);
}

void nsHttpConnectionMgr::OnMsgClearConnectionHistory(int32_t,
                                                      ARefBase* param) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  LOG(("nsHttpConnectionMgr::OnMsgClearConnectionHistory"));

  for (auto iter = mCT.Iter(); !iter.Done(); iter.Next()) {
    RefPtr<ConnectionEntry> ent = iter.Data();
    if (ent->IdleConnectionsLength() == 0 && ent->ActiveConnsLength() == 0 &&
        ent->DnsAndConnectSocketsLength() == 0 &&
        ent->UrgentStartQueueLength() == 0 && ent->PendingQueueLength() == 0 &&
        !ent->mDoNotDestroy) {
      iter.Remove();
    }
  }
}

nsresult nsHttpConnectionMgr::CloseIdleConnection(nsHttpConnection* conn) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("nsHttpConnectionMgr::CloseIdleConnection %p conn=%p", this, conn));

  if (!conn->ConnectionInfo()) {
    return NS_ERROR_UNEXPECTED;
  }

  ConnectionEntry* ent = mCT.GetWeak(conn->ConnectionInfo()->HashKey());

  if (!ent || NS_FAILED(ent->CloseIdleConnection(conn))) {
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

nsresult nsHttpConnectionMgr::RemoveIdleConnection(nsHttpConnection* conn) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  LOG(("nsHttpConnectionMgr::RemoveIdleConnection %p conn=%p", this, conn));

  if (!conn->ConnectionInfo()) {
    return NS_ERROR_UNEXPECTED;
  }

  ConnectionEntry* ent = mCT.GetWeak(conn->ConnectionInfo()->HashKey());

  if (!ent || NS_FAILED(ent->RemoveIdleConnection(conn))) {
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

HttpConnectionBase* nsHttpConnectionMgr::FindCoalescableConnectionByHashKey(
    ConnectionEntry* ent, const nsCString& key, bool justKidding, bool aNoHttp2,
    bool aNoHttp3) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!aNoHttp2 || !aNoHttp3);
  MOZ_ASSERT(ent->mConnInfo);
  nsHttpConnectionInfo* ci = ent->mConnInfo;

  nsTArray<nsWeakPtr>* listOfWeakConns = mCoalescingHash.Get(key);
  if (!listOfWeakConns) {
    return nullptr;
  }

  uint32_t listLen = listOfWeakConns->Length();
  for (uint32_t j = 0; j < listLen;) {
    RefPtr<HttpConnectionBase> potentialMatch =
        do_QueryReferent(listOfWeakConns->ElementAt(j));
    if (!potentialMatch) {
      // This is a connection that needs to be removed from the list
      LOG(
          ("FindCoalescableConnectionByHashKey() found old conn %p that has "
           "null weak ptr - removing\n",
           listOfWeakConns->ElementAt(j).get()));
      if (j != listLen - 1) {
        listOfWeakConns->Elements()[j] =
            listOfWeakConns->Elements()[listLen - 1];
      }
      listOfWeakConns->RemoveLastElement();
      MOZ_ASSERT(listOfWeakConns->Length() == listLen - 1);
      listLen--;
      continue;  // without adjusting iterator
    }

    if (aNoHttp3 && potentialMatch->UsingHttp3()) {
      j++;
      continue;
    }
    if (aNoHttp2 && potentialMatch->UsingSpdy()) {
      j++;
      continue;
    }
    bool couldJoin;
    if (justKidding) {
      couldJoin =
          potentialMatch->TestJoinConnection(ci->GetOrigin(), ci->OriginPort());
    } else {
      couldJoin =
          potentialMatch->JoinConnection(ci->GetOrigin(), ci->OriginPort());
    }
    if (couldJoin) {
      LOG(
          ("FindCoalescableConnectionByHashKey() found match conn=%p key=%s "
           "newCI=%s matchedCI=%s join ok\n",
           potentialMatch.get(), key.get(), ci->HashKey().get(),
           potentialMatch->ConnectionInfo()->HashKey().get()));
      return potentialMatch.get();
    }
    LOG(
        ("FindCoalescableConnectionByHashKey() found match conn=%p key=%s "
         "newCI=%s matchedCI=%s join failed\n",
         potentialMatch.get(), key.get(), ci->HashKey().get(),
         potentialMatch->ConnectionInfo()->HashKey().get()));

    ++j;  // bypassed by continue when weakptr fails
  }

  if (!listLen) {  // shrunk to 0 while iterating
    LOG(("FindCoalescableConnectionByHashKey() removing empty list element\n"));
    mCoalescingHash.Remove(key);
  }
  return nullptr;
}

static void BuildOriginFrameHashKey(nsACString& newKey,
                                    nsHttpConnectionInfo* ci,
                                    const nsACString& host, int32_t port) {
  newKey.Assign(host);
  if (ci->GetAnonymous()) {
    newKey.AppendLiteral("~A:");
  } else {
    newKey.AppendLiteral("~.:");
  }
  if (ci->GetFallbackConnection()) {
    newKey.AppendLiteral("~F:");
  } else {
    newKey.AppendLiteral("~.:");
  }
  newKey.AppendInt(port);
  newKey.AppendLiteral("/[");
  nsAutoCString suffix;
  ci->GetOriginAttributes().CreateSuffix(suffix);
  newKey.Append(suffix);
  newKey.AppendLiteral("]viaORIGIN.FRAME");
}

HttpConnectionBase* nsHttpConnectionMgr::FindCoalescableConnection(
    ConnectionEntry* ent, bool justKidding, bool aNoHttp2, bool aNoHttp3) {
  MOZ_ASSERT(!aNoHttp2 || !aNoHttp3);
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(ent->mConnInfo);
  nsHttpConnectionInfo* ci = ent->mConnInfo;
  LOG(("FindCoalescableConnection %s\n", ci->HashKey().get()));

  if (ci->GetWebTransport()) {
    LOG(("Don't coalesce a WebTransport conn "));
    return nullptr;
  }
  // First try and look it up by origin frame
  nsCString newKey;
  BuildOriginFrameHashKey(newKey, ci, ci->GetOrigin(), ci->OriginPort());
  HttpConnectionBase* conn = FindCoalescableConnectionByHashKey(
      ent, newKey, justKidding, aNoHttp2, aNoHttp3);
  if (conn) {
    LOG(("FindCoalescableConnection(%s) match conn %p on frame key %s\n",
         ci->HashKey().get(), conn, newKey.get()));
    return conn;
  }

  // now check for DNS based keys
  // deleted conns (null weak pointers) are removed from list
  uint32_t keyLen = ent->mCoalescingKeys.Length();
  for (uint32_t i = 0; i < keyLen; ++i) {
    conn = FindCoalescableConnectionByHashKey(ent, ent->mCoalescingKeys[i],
                                              justKidding, aNoHttp2, aNoHttp3);

    auto usableEntry = [&](HttpConnectionBase* conn) {
      // This is allowed by the spec, but other browsers don't coalesce
      // so agressively, which surprises developers. See bug 1420777.
      if (StaticPrefs::network_http_http2_aggressive_coalescing()) {
        return true;
      }

      // Make sure that the connection's IP address is one that is in
      // the set of IP addresses in the entry's DNS response.
      NetAddr addr;
      nsresult rv = conn->GetPeerAddr(&addr);
      if (NS_FAILED(rv)) {
        // Err on the side of not coalescing
        return false;
      }
      // We don't care about remote port when matching entries.
      addr.inet.port = 0;
      return ent->mAddresses.Contains(addr);
    };

    if (conn) {
      LOG(("Found connection with matching hash"));
      if (usableEntry(conn)) {
        LOG(("> coalescing"));
        return conn;
      } else {
        LOG(("> not coalescing as remote address not present in DNS records"));
      }
    }
  }

  LOG(("FindCoalescableConnection(%s) no matching conn\n",
       ci->HashKey().get()));
  return nullptr;
}

void nsHttpConnectionMgr::UpdateCoalescingForNewConn(
    HttpConnectionBase* newConn, ConnectionEntry* ent, bool aNoHttp3) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(newConn);
  MOZ_ASSERT(newConn->ConnectionInfo());
  MOZ_ASSERT(ent);
  MOZ_ASSERT(mCT.GetWeak(newConn->ConnectionInfo()->HashKey()) == ent);
  LOG(("UpdateCoalescingForNewConn newConn=%p aNoHttp3=%d", newConn, aNoHttp3));
  if (newConn->ConnectionInfo()->GetWebTransport()) {
    LOG(("Don't coalesce a WebTransport conn %p", newConn));
    // TODO: implement this properly in bug 1815735.
    return;
  }

  HttpConnectionBase* existingConn =
      FindCoalescableConnection(ent, true, false, false);
  if (existingConn) {
    // Prefer http3 connection, but allow an HTTP/2 connection if it is used for
    // WebSocket.
    if (newConn->UsingHttp3() && existingConn->UsingSpdy()) {
      RefPtr<nsHttpConnection> connTCP = do_QueryObject(existingConn);
      if (connTCP && !connTCP->IsForWebSocket()) {
        LOG(
            ("UpdateCoalescingForNewConn() found existing active H2 conn that "
             "could have served newConn, but new connection is H3, therefore "
             "close the H2 conncetion"));
        existingConn->SetCloseReason(
            ConnectionCloseReason::CLOSE_EXISTING_CONN_FOR_COALESCING);
        existingConn->DontReuse();
      }
    } else if (existingConn->UsingHttp3() && newConn->UsingSpdy()) {
      RefPtr<nsHttpConnection> connTCP = do_QueryObject(newConn);
      if (connTCP && !connTCP->IsForWebSocket() && !aNoHttp3) {
        LOG(
            ("UpdateCoalescingForNewConn() found existing active H3 conn that "
             "could have served H2 newConn graceful close of newConn=%p to "
             "migrate to existingConn %p\n",
             newConn, existingConn));
        newConn->SetCloseReason(
            ConnectionCloseReason::CLOSE_NEW_CONN_FOR_COALESCING);
        newConn->DontReuse();
        return;
      }
    } else {
      LOG(
          ("UpdateCoalescingForNewConn() found existing active conn that could "
           "have served newConn "
           "graceful close of newConn=%p to migrate to existingConn %p\n",
           newConn, existingConn));
      newConn->SetCloseReason(
          ConnectionCloseReason::CLOSE_NEW_CONN_FOR_COALESCING);
      newConn->DontReuse();
      return;
    }
  }

  // This connection might go into the mCoalescingHash for new transactions to
  // be coalesced onto if it can accept new transactions
  if (!newConn->CanDirectlyActivate()) {
    return;
  }

  uint32_t keyLen = ent->mCoalescingKeys.Length();
  for (uint32_t i = 0; i < keyLen; ++i) {
    LOG((
        "UpdateCoalescingForNewConn() registering newConn %p %s under key %s\n",
        newConn, newConn->ConnectionInfo()->HashKey().get(),
        ent->mCoalescingKeys[i].get()));

    mCoalescingHash
        .LookupOrInsertWith(
            ent->mCoalescingKeys[i],
            [] {
              LOG(("UpdateCoalescingForNewConn() need new list element\n"));
              return MakeUnique<nsTArray<nsWeakPtr>>(1);
            })
        ->AppendElement(do_GetWeakReference(
            static_cast<nsISupportsWeakReference*>(newConn)));
  }

  // this is a new connection that can be coalesced onto. hooray!
  // if there are other connection to this entry (e.g.
  // some could still be handshaking, shutting down, etc..) then close
  // them down after any transactions that are on them are complete.
  // This probably happened due to the parallel connection algorithm
  // that is used only before the host is known to speak h2.
  ent->MakeAllDontReuseExcept(newConn);
}

// This function lets a connection, after completing the NPN phase,
// report whether or not it is using spdy through the usingSpdy
// argument. It would not be necessary if NPN were driven out of
// the connection manager. The connection entry associated with the
// connection is then updated to indicate whether or not we want to use
// spdy with that host and update the coalescing hash
// entries used for de-sharding hostsnames.
void nsHttpConnectionMgr::ReportSpdyConnection(nsHttpConnection* conn,
                                               bool usingSpdy,
                                               bool disallowHttp3) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (!conn->ConnectionInfo()) {
    return;
  }
  ConnectionEntry* ent = mCT.GetWeak(conn->ConnectionInfo()->HashKey());
  if (!ent || !usingSpdy) {
    return;
  }

  ent->mUsingSpdy = true;
  mNumSpdyHttp3ActiveConns++;

  // adjust timeout timer
  uint32_t ttl = conn->TimeToLive();
  uint64_t timeOfExpire = NowInSeconds() + ttl;
  if (!mTimer || timeOfExpire < mTimeOfNextWakeUp) {
    PruneDeadConnectionsAfter(ttl);
  }

  UpdateCoalescingForNewConn(conn, ent, disallowHttp3);

  nsresult rv = ProcessPendingQ(ent->mConnInfo);
  if (NS_FAILED(rv)) {
    LOG(
        ("ReportSpdyConnection conn=%p ent=%p "
         "failed to process pending queue (%08x)\n",
         conn, ent, static_cast<uint32_t>(rv)));
  }
  rv = PostEvent(&nsHttpConnectionMgr::OnMsgProcessAllSpdyPendingQ);
  if (NS_FAILED(rv)) {
    LOG(
        ("ReportSpdyConnection conn=%p ent=%p "
         "failed to post event (%08x)\n",
         conn, ent, static_cast<uint32_t>(rv)));
  }
}

void nsHttpConnectionMgr::ReportHttp3Connection(HttpConnectionBase* conn) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (!conn->ConnectionInfo()) {
    return;
  }
  ConnectionEntry* ent = mCT.GetWeak(conn->ConnectionInfo()->HashKey());
  if (!ent) {
    return;
  }

  mNumSpdyHttp3ActiveConns++;

  UpdateCoalescingForNewConn(conn, ent, false);
  nsresult rv = ProcessPendingQ(ent->mConnInfo);
  if (NS_FAILED(rv)) {
    LOG(
        ("ReportHttp3Connection conn=%p ent=%p "
         "failed to process pending queue (%08x)\n",
         conn, ent, static_cast<uint32_t>(rv)));
  }
  rv = PostEvent(&nsHttpConnectionMgr::OnMsgProcessAllSpdyPendingQ);
  if (NS_FAILED(rv)) {
    LOG(
        ("ReportHttp3Connection conn=%p ent=%p "
         "failed to post event (%08x)\n",
         conn, ent, static_cast<uint32_t>(rv)));
  }
}

//-----------------------------------------------------------------------------
bool nsHttpConnectionMgr::DispatchPendingQ(
    nsTArray<RefPtr<PendingTransactionInfo>>& pendingQ, ConnectionEntry* ent,
    bool considerAll) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  PendingTransactionInfo* pendingTransInfo = nullptr;
  nsresult rv;
  bool dispatchedSuccessfully = false;

  // if !considerAll iterate the pending list until one is dispatched
  // successfully. Keep iterating afterwards only until a transaction fails to
  // dispatch. if considerAll == true then try and dispatch all items.
  for (uint32_t i = 0; i < pendingQ.Length();) {
    pendingTransInfo = pendingQ[i];

    bool alreadyDnsAndConnectSocketOrWaitingForTLS =
        pendingTransInfo->IsAlreadyClaimedInitializingConn();

    rv = TryDispatchTransaction(ent, alreadyDnsAndConnectSocketOrWaitingForTLS,
                                pendingTransInfo);
    if (NS_SUCCEEDED(rv) || (rv != NS_ERROR_NOT_AVAILABLE)) {
      if (NS_SUCCEEDED(rv)) {
        LOG(("  dispatching pending transaction...\n"));
      } else {
        LOG(
            ("  removing pending transaction based on "
             "TryDispatchTransaction returning hard error %" PRIx32 "\n",
             static_cast<uint32_t>(rv)));
      }
      if (pendingQ.RemoveElement(pendingTransInfo)) {
        // pendingTransInfo is now potentially destroyed
        dispatchedSuccessfully = true;
        continue;  // dont ++i as we just made the array shorter
      }

      LOG(("  transaction not found in pending queue\n"));
    }

    if (dispatchedSuccessfully && !considerAll) break;

    ++i;
  }
  return dispatchedSuccessfully;
}

uint32_t nsHttpConnectionMgr::MaxPersistConnections(
    ConnectionEntry* ent) const {
  if (ent->mConnInfo->UsingHttpProxy() && !ent->mConnInfo->UsingConnect()) {
    return static_cast<uint32_t>(mMaxPersistConnsPerProxy);
  }

  return static_cast<uint32_t>(mMaxPersistConnsPerHost);
}

void nsHttpConnectionMgr::PreparePendingQForDispatching(
    ConnectionEntry* ent, nsTArray<RefPtr<PendingTransactionInfo>>& pendingQ,
    bool considerAll) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  pendingQ.Clear();

  uint32_t totalCount = ent->TotalActiveConnections();
  uint32_t maxPersistConns = MaxPersistConnections(ent);
  uint32_t availableConnections =
      maxPersistConns > totalCount ? maxPersistConns - totalCount : 0;

  // No need to try dispatching if we reach the active connection limit.
  if (!availableConnections) {
    return;
  }

  // Only have to get transactions from the queue whose window id is 0.
  if (!gHttpHandler->ActiveTabPriority()) {
    ent->AppendPendingQForFocusedWindow(0, pendingQ, availableConnections);
    return;
  }

  uint32_t maxFocusedWindowConnections =
      availableConnections * gHttpHandler->FocusedWindowTransactionRatio();
  MOZ_ASSERT(maxFocusedWindowConnections < availableConnections);

  if (!maxFocusedWindowConnections) {
    maxFocusedWindowConnections = 1;
  }

  // Only need to dispatch transactions for either focused or
  // non-focused window because considerAll is false.
  if (!considerAll) {
    ent->AppendPendingQForFocusedWindow(mCurrentBrowserId, pendingQ,
                                        maxFocusedWindowConnections);

    if (pendingQ.IsEmpty()) {
      ent->AppendPendingQForNonFocusedWindows(mCurrentBrowserId, pendingQ,
                                              availableConnections);
    }
    return;
  }

  uint32_t maxNonFocusedWindowConnections =
      availableConnections - maxFocusedWindowConnections;
  nsTArray<RefPtr<PendingTransactionInfo>> remainingPendingQ;

  ent->AppendPendingQForFocusedWindow(mCurrentBrowserId, pendingQ,
                                      maxFocusedWindowConnections);

  if (maxNonFocusedWindowConnections) {
    ent->AppendPendingQForNonFocusedWindows(
        mCurrentBrowserId, remainingPendingQ, maxNonFocusedWindowConnections);
  }

  // If the slots for either focused or non-focused window are not filled up
  // to the availability, try to use the remaining available connections
  // for the other slot (with preference for the focused window).
  if (remainingPendingQ.Length() < maxNonFocusedWindowConnections) {
    ent->AppendPendingQForFocusedWindow(
        mCurrentBrowserId, pendingQ,
        maxNonFocusedWindowConnections - remainingPendingQ.Length());
  } else if (pendingQ.Length() < maxFocusedWindowConnections) {
    ent->AppendPendingQForNonFocusedWindows(
        mCurrentBrowserId, remainingPendingQ,
        maxFocusedWindowConnections - pendingQ.Length());
  }

  MOZ_ASSERT(pendingQ.Length() + remainingPendingQ.Length() <=
             availableConnections);

  LOG(
      ("nsHttpConnectionMgr::PreparePendingQForDispatching "
       "focused window pendingQ.Length()=%zu"
       ", remainingPendingQ.Length()=%zu\n",
       pendingQ.Length(), remainingPendingQ.Length()));

  // Append elements in |remainingPendingQ| to |pendingQ|. The order in
  // |pendingQ| is like: [focusedWindowTrans...nonFocusedWindowTrans].
  pendingQ.AppendElements(std::move(remainingPendingQ));
}

bool nsHttpConnectionMgr::ProcessPendingQForEntry(ConnectionEntry* ent,
                                                  bool considerAll) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  LOG(
      ("nsHttpConnectionMgr::ProcessPendingQForEntry "
       "[ci=%s ent=%p active=%zu idle=%zu urgent-start-queue=%zu"
       " queued=%zu]\n",
       ent->mConnInfo->HashKey().get(), ent, ent->ActiveConnsLength(),
       ent->IdleConnectionsLength(), ent->UrgentStartQueueLength(),
       ent->PendingQueueLength()));

  if (LOG_ENABLED()) {
    ent->PrintPendingQ();
    ent->LogConnections();
  }

  if (!ent->PendingQueueLength() && !ent->UrgentStartQueueLength()) {
    return false;
  }
  ProcessSpdyPendingQ(ent);

  bool dispatchedSuccessfully = false;

  if (ent->UrgentStartQueueLength()) {
    nsTArray<RefPtr<PendingTransactionInfo>> pendingQ;
    ent->AppendPendingUrgentStartQ(pendingQ);
    dispatchedSuccessfully = DispatchPendingQ(pendingQ, ent, considerAll);
    for (const auto& transactionInfo : Reversed(pendingQ)) {
      ent->InsertTransaction(transactionInfo);
    }
  }

  if (dispatchedSuccessfully && !considerAll) {
    return dispatchedSuccessfully;
  }

  nsTArray<RefPtr<PendingTransactionInfo>> pendingQ;
  PreparePendingQForDispatching(ent, pendingQ, considerAll);

  // The only case that |pendingQ| is empty is when there is no
  // connection available for dispatching.
  if (pendingQ.IsEmpty()) {
    return dispatchedSuccessfully;
  }

  dispatchedSuccessfully |= DispatchPendingQ(pendingQ, ent, considerAll);

  // Put the leftovers into connection entry, in the same order as they
  // were before to keep the natural ordering.
  for (const auto& transactionInfo : Reversed(pendingQ)) {
    ent->InsertTransaction(transactionInfo, true);
  }

  // Only remove empty pendingQ when considerAll is true.
  if (considerAll) {
    ent->RemoveEmptyPendingQ();
  }

  return dispatchedSuccessfully;
}

bool nsHttpConnectionMgr::ProcessPendingQForEntry(nsHttpConnectionInfo* ci) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  ConnectionEntry* ent = mCT.GetWeak(ci->HashKey());
  if (ent) return ProcessPendingQForEntry(ent, false);
  return false;
}

// we're at the active connection limit if any one of the following conditions
// is true:
//  (1) at max-connections
//  (2) keep-alive enabled and at max-persistent-connections-per-server/proxy
//  (3) keep-alive disabled and at max-connections-per-server
bool nsHttpConnectionMgr::AtActiveConnectionLimit(ConnectionEntry* ent,
                                                  uint32_t caps) {
  nsHttpConnectionInfo* ci = ent->mConnInfo;
  uint32_t totalCount = ent->TotalActiveConnections();

  if (ci->IsHttp3()) {
    if (ci->GetWebTransport()) {
      // TODO: implement this properly in bug 1815735.
      return false;
    }
    return totalCount > 0;
  }

  uint32_t maxPersistConns = MaxPersistConnections(ent);

  LOG(
      ("nsHttpConnectionMgr::AtActiveConnectionLimit [ci=%s caps=%x,"
       "totalCount=%u, maxPersistConns=%u]\n",
       ci->HashKey().get(), caps, totalCount, maxPersistConns));

  if (caps & NS_HTTP_URGENT_START) {
    if (totalCount >= (mMaxUrgentExcessiveConns + maxPersistConns)) {
      LOG((
          "The number of total connections are greater than or equal to sum of "
          "max urgent-start queue length and the number of max persistent "
          "connections.\n"));
      return true;
    }
    return false;
  }

  // update maxconns if potentially limited by the max socket count
  // this requires a dynamic reduction in the max socket count to a point
  // lower than the max-connections pref.
  uint32_t maxSocketCount = gHttpHandler->MaxSocketCount();
  if (mMaxConns > maxSocketCount) {
    mMaxConns = maxSocketCount;
    LOG(("nsHttpConnectionMgr %p mMaxConns dynamically reduced to %u", this,
         mMaxConns));
  }

  // If there are more active connections than the global limit, then we're
  // done. Purging idle connections won't get us below it.
  if (mNumActiveConns >= mMaxConns) {
    LOG(("  num active conns == max conns\n"));
    return true;
  }

  bool result = (totalCount >= maxPersistConns);
  LOG(("AtActiveConnectionLimit result: %s", result ? "true" : "false"));
  return result;
}

// returns NS_OK if a connection was started
// return NS_ERROR_NOT_AVAILABLE if a new connection cannot be made due to
//        ephemeral limits
// returns other NS_ERROR on hard failure conditions
nsresult nsHttpConnectionMgr::MakeNewConnection(
    ConnectionEntry* ent, PendingTransactionInfo* pendingTransInfo) {
  LOG(("nsHttpConnectionMgr::MakeNewConnection %p ent=%p trans=%p", this, ent,
       pendingTransInfo->Transaction()));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (ent->FindConnToClaim(pendingTransInfo)) {
    return NS_OK;
  }

  nsHttpTransaction* trans = pendingTransInfo->Transaction();

  // If this host is trying to negotiate a SPDY session right now,
  // don't create any new connections until the result of the
  // negotiation is known.
  if (!(trans->Caps() & NS_HTTP_DISALLOW_SPDY) &&
      (trans->Caps() & NS_HTTP_ALLOW_KEEPALIVE) && ent->RestrictConnections()) {
    LOG(
        ("nsHttpConnectionMgr::MakeNewConnection [ci = %s] "
         "Not Available Due to RestrictConnections()\n",
         ent->mConnInfo->HashKey().get()));
    return NS_ERROR_NOT_AVAILABLE;
  }

  // We need to make a new connection. If that is going to exceed the
  // global connection limit then try and free up some room by closing
  // an idle connection to another host. We know it won't select "ent"
  // because we have already determined there are no idle connections
  // to our destination

  if ((mNumIdleConns + mNumActiveConns + 1 >= mMaxConns) && mNumIdleConns) {
    // If the global number of connections is preventing the opening of new
    // connections to a host without idle connections, then close them
    // regardless of their TTL.
    auto iter = mCT.ConstIter();
    while (mNumIdleConns + mNumActiveConns + 1 >= mMaxConns && !iter.Done()) {
      RefPtr<ConnectionEntry> entry = iter.Data();
      entry->CloseIdleConnections((mNumIdleConns + mNumActiveConns + 1) -
                                  mMaxConns);
      iter.Next();
    }
  }

  if ((mNumIdleConns + mNumActiveConns + 1 >= mMaxConns) && mNumActiveConns &&
      StaticPrefs::network_http_http2_enabled()) {
    // If the global number of connections is preventing the opening of new
    // connections to a host without idle connections, then close any spdy
    // ASAP.
    for (const RefPtr<ConnectionEntry>& entry : mCT.Values()) {
      while (entry->MakeFirstActiveSpdyConnDontReuse()) {
        // Stop on <= (particularly =) because this dontreuse
        // causes async close.
        if (mNumIdleConns + mNumActiveConns + 1 <= mMaxConns) {
          goto outerLoopEnd;
        }
      }
    }
  outerLoopEnd:;
  }

  if (AtActiveConnectionLimit(ent, trans->Caps())) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsresult rv = ent->CreateDnsAndConnectSocket(
      trans, trans->Caps(), false, false,
      trans->GetClassOfService().Flags() & nsIClassOfService::UrgentStart, true,
      pendingTransInfo);
  if (NS_FAILED(rv)) {
    /* hard failure */
    LOG(
        ("nsHttpConnectionMgr::MakeNewConnection [ci = %s trans = %p] "
         "CreateDnsAndConnectSocket() hard failure.\n",
         ent->mConnInfo->HashKey().get(), trans));
    trans->Close(rv);
    if (rv == NS_ERROR_NOT_AVAILABLE) rv = NS_ERROR_FAILURE;
    return rv;
  }

  return NS_OK;
}

// returns OK if a connection is found for the transaction
//   and the transaction is started.
// returns ERROR_NOT_AVAILABLE if no connection can be found and it
//   should be queued until circumstances change
// returns other ERROR when transaction has a hard failure and should
//   not remain in the pending queue
nsresult nsHttpConnectionMgr::TryDispatchTransaction(
    ConnectionEntry* ent, bool onlyReusedConnection,
    PendingTransactionInfo* pendingTransInfo) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  nsHttpTransaction* trans = pendingTransInfo->Transaction();

  LOG(
      ("nsHttpConnectionMgr::TryDispatchTransaction without conn "
       "[trans=%p ci=%p ci=%s caps=%x onlyreused=%d active=%zu "
       "idle=%zu]\n",
       trans, ent->mConnInfo.get(), ent->mConnInfo->HashKey().get(),
       uint32_t(trans->Caps()), onlyReusedConnection, ent->ActiveConnsLength(),
       ent->IdleConnectionsLength()));

  uint32_t caps = trans->Caps();

  // 0 - If this should use spdy then dispatch it post haste.
  // 1 - If there is connection pressure then see if we can pipeline this on
  //     a connection of a matching type instead of using a new conn
  // 2 - If there is an idle connection, use it!
  // 3 - if class == reval or script and there is an open conn of that type
  //     then pipeline onto shortest pipeline of that class if limits allow
  // 4 - If we aren't up against our connection limit,
  //     then open a new one
  // 5 - Try a pipeline if we haven't already - this will be unusual because
  //     it implies a low connection pressure situation where
  //     MakeNewConnection() failed.. that is possible, but unlikely, due to
  //     global limits
  // 6 - no connection is available - queue it

  RefPtr<HttpConnectionBase> unusedSpdyPersistentConnection;

  // step 0
  // look for existing spdy connection - that's always best because it is
  // essentially pipelining without head of line blocking

  RefPtr<HttpConnectionBase> conn = GetH2orH3ActiveConn(
      ent,
      (!StaticPrefs::network_http_http2_enabled() ||
       (caps & NS_HTTP_DISALLOW_SPDY)),
      (!nsHttpHandler::IsHttp3Enabled() || (caps & NS_HTTP_DISALLOW_HTTP3)));
  if (conn) {
    LOG(("TryingDispatchTransaction: an active h2 connection exists"));
    ExtendedCONNECTSupport extendedConnect = conn->GetExtendedCONNECTSupport();
    if (trans->IsWebsocketUpgrade() || trans->IsForWebTransport()) {
      RefPtr<nsHttpConnection> connTCP = do_QueryObject(conn);
      if (connTCP) {
        LOG(("TryingDispatchTransaction: extended CONNECT"));
        if (extendedConnect == ExtendedCONNECTSupport::NO_SUPPORT) {
          LOG((
              "TryingDispatchTransaction: no support for extended CONNECT over "
              "Http2"));
          // This is a transaction wants to do extended CONNECT and we already
          // have a h2 connection that do not support it, we should disable h2
          // for this transaction.
          trans->DisableSpdy();
          caps &= NS_HTTP_DISALLOW_SPDY;
          trans->MakeSticky();
        } else if (extendedConnect == ExtendedCONNECTSupport::SUPPORTED) {
          LOG(("TryingDispatchTransaction: extended CONNECT supported"));

          // No limit for number of websockets, dispatch transaction to the
          // tunnel
          RefPtr<nsHttpConnection> connToTunnel;
          connTCP->CreateTunnelStream(trans, getter_AddRefs(connToTunnel),
                                      true);
          ent->InsertIntoExtendedCONNECTConns(connToTunnel);
          trans->SetConnection(nullptr);
          connToTunnel
              ->SetInSpdyTunnel();  // tells conn it is already in tunnel
          if (trans->IsWebsocketUpgrade()) {
            trans->SetIsHttp2Websocket(true);
          }
          nsresult rv = DispatchTransaction(ent, trans, connToTunnel);
          // need to undo NonSticky bypass for transaction reset to continue
          // for correct websocket upgrade handling
          trans->MakeSticky();
          trans->SetResettingForTunnelConn(false);
          return rv;
        } else {
          // if we aren't sure that extended CONNECT is supported yet or we are
          // already at the connection limit then we queue the transaction
          LOG(
              ("TryingDispatchTransaction: unsure if extended CONNECT "
               "supported"));
          return NS_ERROR_NOT_AVAILABLE;
        }
      }
    } else {
      if ((caps & NS_HTTP_ALLOW_KEEPALIVE) ||
          (caps & NS_HTTP_ALLOW_SPDY_WITHOUT_KEEPALIVE) ||
          !conn->IsExperienced()) {
        LOG(("   dispatch to spdy: [conn=%p]\n", conn.get()));
        trans->RemoveDispatchedAsBlocking(); /* just in case */
        nsresult rv = DispatchTransaction(ent, trans, conn);
        NS_ENSURE_SUCCESS(rv, rv);
        return NS_OK;
      }
      unusedSpdyPersistentConnection = conn;
    }
  }

  // If this is not a blocking transaction and the request context for it is
  // currently processing one or more blocking transactions then we
  // need to just leave it in the queue until those are complete unless it is
  // explicitly marked as unblocked.
  if (!(caps & NS_HTTP_LOAD_AS_BLOCKING)) {
    if (!(caps & NS_HTTP_LOAD_UNBLOCKED)) {
      nsIRequestContext* requestContext = trans->RequestContext();
      if (requestContext) {
        uint32_t blockers = 0;
        if (NS_SUCCEEDED(
                requestContext->GetBlockingTransactionCount(&blockers)) &&
            blockers) {
          // need to wait for blockers to clear
          LOG(("   blocked by request context: [rc=%p trans=%p blockers=%d]\n",
               requestContext, trans, blockers));
          return NS_ERROR_NOT_AVAILABLE;
        }
      }
    }
  } else {
    // Mark the transaction and its load group as blocking right now to prevent
    // other transactions from being reordered in the queue due to slow syns.
    trans->DispatchedAsBlocking();
  }

  // step 1
  // If connection pressure, then we want to favor pipelining of any kind
  // h1 pipelining has been removed

  // Subject most transactions at high parallelism to rate pacing.
  // It will only be actually submitted to the
  // token bucket once, and if possible it is granted admission synchronously.
  // It is important to leave a transaction in the pending queue when blocked by
  // pacing so it can be found on cancel if necessary.
  // Transactions that cause blocking or bypass it (e.g. js/css) are not rate
  // limited.
  if (gHttpHandler->UseRequestTokenBucket()) {
    // submit even whitelisted transactions to the token bucket though they will
    // not be slowed by it
    bool runNow = trans->TryToRunPacedRequest();
    if (!runNow) {
      if ((mNumActiveConns - mNumSpdyHttp3ActiveConns) <=
          gHttpHandler->RequestTokenBucketMinParallelism()) {
        runNow = true;  // white list it
      } else if (caps & (NS_HTTP_LOAD_AS_BLOCKING | NS_HTTP_LOAD_UNBLOCKED)) {
        runNow = true;  // white list it
      }
    }
    if (!runNow) {
      LOG(("   blocked due to rate pacing trans=%p\n", trans));
      return NS_ERROR_NOT_AVAILABLE;
    }
  }

  // step 2
  // consider an idle persistent connection
  bool idleConnsAllUrgent = false;
  if (caps & NS_HTTP_ALLOW_KEEPALIVE) {
    nsresult rv = TryDispatchTransactionOnIdleConn(ent, pendingTransInfo, true,
                                                   &idleConnsAllUrgent);
    if (NS_SUCCEEDED(rv)) {
      LOG(("   dispatched step 2 (idle) trans=%p\n", trans));
      return NS_OK;
    }
  }

  // step 3
  // consider pipelining scripts and revalidations
  // h1 pipelining has been removed

  // Don't dispatch if this transaction is waiting for HTTPS RR.
  // This usually happens when the pref "network.dns.force_waiting_https_rr" is
  // true or when echConfig is enabled.
  if (trans->WaitingForHTTPSRR()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // step 4
  if (!onlyReusedConnection) {
    nsresult rv = MakeNewConnection(ent, pendingTransInfo);
    if (NS_SUCCEEDED(rv)) {
      // this function returns NOT_AVAILABLE for asynchronous connects
      LOG(("   dispatched step 4 (async new conn) trans=%p\n", trans));
      return NS_ERROR_NOT_AVAILABLE;
    }

    if (rv != NS_ERROR_NOT_AVAILABLE) {
      // not available return codes should try next step as they are
      // not hard errors. Other codes should stop now
      LOG(("   failed step 4 (%" PRIx32 ") trans=%p\n",
           static_cast<uint32_t>(rv), trans));
      return rv;
    }

    // repeat step 2 when there are only idle connections and all are urgent,
    // don't respect urgency so that non-urgent transaction will be allowed
    // to dispatch on an urgent-start-only marked connection to avoid
    // dispatch deadlocks
    if (!(trans->GetClassOfService().Flags() &
          nsIClassOfService::UrgentStart) &&
        idleConnsAllUrgent &&
        ent->ActiveConnsLength() < MaxPersistConnections(ent)) {
      rv = TryDispatchTransactionOnIdleConn(ent, pendingTransInfo, false);
      if (NS_SUCCEEDED(rv)) {
        LOG(("   dispatched step 2a (idle, reuse urgent) trans=%p\n", trans));
        return NS_OK;
      }
    }
  }

  // step 5
  // previously pipelined anything here if allowed but h1 pipelining has been
  // removed

  // step 6
  if (unusedSpdyPersistentConnection) {
    // to avoid deadlocks, we need to throw away this perfectly valid SPDY
    // connection to make room for a new one that can service a no KEEPALIVE
    // request
    unusedSpdyPersistentConnection->DontReuse();
  }

  LOG(("   not dispatched (queued) trans=%p\n", trans));
  return NS_ERROR_NOT_AVAILABLE; /* queue it */
}

nsresult nsHttpConnectionMgr::TryDispatchTransactionOnIdleConn(
    ConnectionEntry* ent, PendingTransactionInfo* pendingTransInfo,
    bool respectUrgency, bool* allUrgent) {
  bool onlyUrgent = !!ent->IdleConnectionsLength();

  nsHttpTransaction* trans = pendingTransInfo->Transaction();
  bool urgentTrans =
      trans->GetClassOfService().Flags() & nsIClassOfService::UrgentStart;

  LOG(
      ("nsHttpConnectionMgr::TryDispatchTransactionOnIdleConn, ent=%p, "
       "trans=%p, urgent=%d",
       ent, trans, urgentTrans));

  RefPtr<nsHttpConnection> conn =
      ent->GetIdleConnection(respectUrgency, urgentTrans, &onlyUrgent);

  if (allUrgent) {
    *allUrgent = onlyUrgent;
  }

  if (conn) {
    // This will update the class of the connection to be the class of
    // the transaction dispatched on it.
    ent->InsertIntoActiveConns(conn);
    nsresult rv = DispatchTransaction(ent, trans, conn);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  return NS_ERROR_NOT_AVAILABLE;
}

nsresult nsHttpConnectionMgr::DispatchTransaction(ConnectionEntry* ent,
                                                  nsHttpTransaction* trans,
                                                  HttpConnectionBase* conn) {
  uint32_t caps = trans->Caps();
  int32_t priority = trans->Priority();
  nsresult rv;

  LOG(
      ("nsHttpConnectionMgr::DispatchTransaction "
       "[ent-ci=%s %p trans=%p caps=%x conn=%p priority=%d isHttp2=%d "
       "isHttp3=%d]\n",
       ent->mConnInfo->HashKey().get(), ent, trans, caps, conn, priority,
       conn->UsingSpdy(), conn->UsingHttp3()));

  // It is possible for a rate-paced transaction to be dispatched independent
  // of the token bucket when the amount of parallelization has changed or
  // when a muxed connection (e.g. h2) becomes available.
  trans->CancelPacing(NS_OK);

  TimeStamp now = TimeStamp::Now();
  TimeDuration elapsed = now - trans->GetPendingTime();
  auto recordPendingTimeForHTTPSRR = [&](nsCString& aKey) {
    uint32_t stage = trans->HTTPSSVCReceivedStage();
    if (HTTPS_RR_IS_USED(stage)) {
      glean::networking::transaction_wait_time_https_rr.AccumulateRawDuration(
          elapsed);

    } else {
      glean::networking::transaction_wait_time.AccumulateRawDuration(elapsed);
    }
  };

  PerfStats::RecordMeasurement(PerfStats::Metric::HttpTransactionWaitTime,
                               elapsed);

  PROFILER_MARKER(
      "DispatchTransaction", NETWORK,
      MarkerOptions(MarkerThreadId::MainThread(),
                    MarkerTiming::Interval(trans->GetPendingTime(), now)),
      UrlMarker, trans->GetUrl(), elapsed, trans->ChannelId());

  nsAutoCString httpVersionkey("h1"_ns);
  if (conn->UsingSpdy() || conn->UsingHttp3()) {
    LOG(
        ("Spdy Dispatch Transaction via Activate(). Transaction host = %s, "
         "Connection host = %s\n",
         trans->ConnectionInfo()->Origin(), conn->ConnectionInfo()->Origin()));
    rv = conn->Activate(trans, caps, priority);
    if (NS_SUCCEEDED(rv) && !trans->GetPendingTime().IsNull()) {
      if (conn->UsingSpdy()) {
        httpVersionkey = "h2"_ns;
        glean::http::transaction_wait_time_spdy.AccumulateRawDuration(
            now - trans->GetPendingTime());
      } else {
        httpVersionkey = "h3"_ns;
        glean::http::transaction_wait_time_http3.AccumulateRawDuration(
            now - trans->GetPendingTime());
      }
      recordPendingTimeForHTTPSRR(httpVersionkey);
      trans->SetPendingTime(false);
    }
    return rv;
  }

  MOZ_ASSERT(conn && !conn->Transaction(),
             "DispatchTranaction() on non spdy active connection");

  rv = DispatchAbstractTransaction(ent, trans, caps, conn, priority);

  if (NS_SUCCEEDED(rv) && !trans->GetPendingTime().IsNull()) {
    glean::http::transaction_wait_time_http.AccumulateRawDuration(
        now - trans->GetPendingTime());
    recordPendingTimeForHTTPSRR(httpVersionkey);
    trans->SetPendingTime(false);
  }
  return rv;
}

// Use this method for dispatching nsAHttpTransction's. It can only safely be
// used upon first use of a connection when NPN has not negotiated SPDY vs
// HTTP/1 yet as multiplexing onto an existing SPDY session requires a
// concrete nsHttpTransaction
nsresult nsHttpConnectionMgr::DispatchAbstractTransaction(
    ConnectionEntry* ent, nsAHttpTransaction* aTrans, uint32_t caps,
    HttpConnectionBase* conn, int32_t priority) {
  MOZ_ASSERT(ent);

  nsresult rv;
  MOZ_ASSERT(!conn->UsingSpdy(),
             "Spdy Must Not Use DispatchAbstractTransaction");
  LOG(
      ("nsHttpConnectionMgr::DispatchAbstractTransaction "
       "[ci=%s trans=%p caps=%x conn=%p]\n",
       ent->mConnInfo->HashKey().get(), aTrans, caps, conn));

  RefPtr<nsAHttpTransaction> transaction(aTrans);
  RefPtr<ConnectionHandle> handle = new ConnectionHandle(conn);

  // give the transaction the indirect reference to the connection.
  transaction->SetConnection(handle);

  rv = conn->Activate(transaction, caps, priority);
  if (NS_FAILED(rv)) {
    LOG(("  conn->Activate failed [rv=%" PRIx32 "]\n",
         static_cast<uint32_t>(rv)));
    DebugOnly<nsresult> rv_remove = ent->RemoveActiveConnection(conn);
    MOZ_ASSERT(NS_SUCCEEDED(rv_remove));

    // sever back references to connection, and do so without triggering
    // a call to ReclaimConnection ;-)
    transaction->SetConnection(nullptr);
    handle->Reset();  // destroy the connection
  }

  return rv;
}

void nsHttpConnectionMgr::ReportProxyTelemetry(ConnectionEntry* ent) {
  enum { PROXY_NONE = 1, PROXY_HTTP = 2, PROXY_SOCKS = 3, PROXY_HTTPS = 4 };

  if (!ent->mConnInfo->UsingProxy()) {
    glean::http::proxy_type.AccumulateSingleSample(PROXY_NONE);
  } else if (ent->mConnInfo->UsingHttpsProxy()) {
    glean::http::proxy_type.AccumulateSingleSample(PROXY_HTTPS);
  } else if (ent->mConnInfo->UsingHttpProxy()) {
    glean::http::proxy_type.AccumulateSingleSample(PROXY_HTTP);
  } else {
    glean::http::proxy_type.AccumulateSingleSample(PROXY_SOCKS);
  }
}

nsresult nsHttpConnectionMgr::ProcessNewTransaction(nsHttpTransaction* trans) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  // since "adds" and "cancels" are processed asynchronously and because
  // various events might trigger an "add" directly on the socket thread,
  // we must take care to avoid dispatching a transaction that has already
  // been canceled (see bug 190001).
  if (NS_FAILED(trans->Status())) {
    LOG(("  transaction was canceled... dropping event!\n"));
    return NS_OK;
  }

  // Make sure a transaction is not in a pending queue.
  CheckTransInPendingQueue(trans);

  trans->SetPendingTime();

  PROFILER_MARKER("ProcessNewTransaction", NETWORK,
                  MarkerThreadId::MainThread(), UrlMarker, trans->GetUrl(),
                  TimeDuration::Zero(), trans->ChannelId());

  nsresult rv = NS_OK;
  nsHttpConnectionInfo* ci = trans->ConnectionInfo();
  MOZ_ASSERT(ci);
  MOZ_ASSERT(!ci->IsHttp3() || !(trans->Caps() & NS_HTTP_DISALLOW_HTTP3));

  bool isWildcard = false;
  ConnectionEntry* ent = GetOrCreateConnectionEntry(
      ci, trans->Caps() & NS_HTTP_DISALLOW_HTTP2_PROXY,
      trans->Caps() & NS_HTTP_DISALLOW_SPDY,
      trans->Caps() & NS_HTTP_DISALLOW_HTTP3, &isWildcard);
  MOZ_ASSERT(ent);

  if (nsHttpHandler::EchConfigEnabled(ci->IsHttp3())) {
    ent->MaybeUpdateEchConfig(ci);
  }

  ReportProxyTelemetry(ent);

  // Check if the transaction already has a sticky reference to a connection.
  // If so, then we can just use it directly by transferring its reference
  // to the new connection variable instead of searching for a new one

  nsAHttpConnection* wrappedConnection = trans->Connection();
  RefPtr<HttpConnectionBase> conn;
  RefPtr<PendingTransactionInfo> pendingTransInfo;
  if (wrappedConnection) conn = wrappedConnection->TakeHttpConnection();

  if (conn) {
    MOZ_ASSERT(trans->Caps() & NS_HTTP_STICKY_CONNECTION);
    LOG(
        ("nsHttpConnectionMgr::ProcessNewTransaction trans=%p "
         "sticky connection=%p\n",
         trans, conn.get()));

    if (!ent->IsInActiveConns(conn)) {
      LOG(
          ("nsHttpConnectionMgr::ProcessNewTransaction trans=%p "
           "sticky connection=%p needs to go on the active list\n",
           trans, conn.get()));

      // make sure it isn't on the idle list - we expect this to be an
      // unknown fresh connection
      MOZ_ASSERT(!ent->IsInIdleConnections(conn));
      MOZ_ASSERT(!conn->IsExperienced());

      ent->InsertIntoActiveConns(conn);  // make it active
    }

    trans->SetConnection(nullptr);
    rv = DispatchTransaction(ent, trans, conn);
  } else if (isWildcard) {
    // We have a HTTP/2 session to the proxy, create a new tunneled
    // connection.
    RefPtr<HttpConnectionBase> conn = GetH2orH3ActiveConn(ent, false, true);
    RefPtr<nsHttpConnection> connTCP = do_QueryObject(conn);
    if (ci->UsingHttpsProxy() && ci->UsingConnect()) {
      LOG(("About to create new tunnel conn from [%p]", connTCP.get()));
      ConnectionEntry* specificEnt = mCT.GetWeak(ci->HashKey());

      if (!specificEnt) {
        RefPtr<nsHttpConnectionInfo> clone(ci->Clone());
        specificEnt = new ConnectionEntry(clone);
        mCT.InsertOrUpdate(clone->HashKey(), RefPtr{specificEnt});
      }

      ent = specificEnt;
      bool atLimit = AtActiveConnectionLimit(ent, trans->Caps());
      if (atLimit) {
        rv = NS_ERROR_NOT_AVAILABLE;
      } else {
        RefPtr<nsHttpConnection> newTunnel;
        connTCP->CreateTunnelStream(trans, getter_AddRefs(newTunnel));

        ent->InsertIntoActiveConns(newTunnel);
        trans->SetConnection(nullptr);
        newTunnel->SetInSpdyTunnel();
        rv = DispatchTransaction(ent, trans, newTunnel);
        // need to undo the bypass for transaction reset for proxy
        trans->MakeNonRestartable();
      }
    } else {
      rv = DispatchTransaction(ent, trans, connTCP);
    }
  } else {
    if (!ent->AllowHttp2()) {
      trans->DisableSpdy();
    }
    pendingTransInfo = new PendingTransactionInfo(trans);
    rv = TryDispatchTransaction(ent, false, pendingTransInfo);
  }

  if (NS_SUCCEEDED(rv)) {
    LOG(("  ProcessNewTransaction Dispatch Immediately trans=%p\n", trans));
    return rv;
  }

  if (rv == NS_ERROR_NOT_AVAILABLE) {
    if (!pendingTransInfo) {
      pendingTransInfo = new PendingTransactionInfo(trans);
    }

    ent->InsertTransaction(pendingTransInfo);
    return NS_OK;
  }

  LOG(("  ProcessNewTransaction Hard Error trans=%p rv=%" PRIx32 "\n", trans,
       static_cast<uint32_t>(rv)));
  return rv;
}

void nsHttpConnectionMgr::IncrementActiveConnCount() {
  mNumActiveConns++;
  ActivateTimeoutTick();
}

void nsHttpConnectionMgr::DecrementActiveConnCount(HttpConnectionBase* conn) {
  MOZ_DIAGNOSTIC_ASSERT(mNumActiveConns > 0);
  if (mNumActiveConns > 0) {
    mNumActiveConns--;
  }

  RefPtr<nsHttpConnection> connTCP = do_QueryObject(conn);
  if (!connTCP || connTCP->EverUsedSpdy()) mNumSpdyHttp3ActiveConns--;
  ConditionallyStopTimeoutTick();
}

void nsHttpConnectionMgr::StartedConnect() {
  mNumActiveConns++;
  ActivateTimeoutTick();  // likely disabled by RecvdConnect()
}

void nsHttpConnectionMgr::RecvdConnect() {
  MOZ_DIAGNOSTIC_ASSERT(mNumActiveConns > 0);
  if (mNumActiveConns > 0) {
    mNumActiveConns--;
  }

  ConditionallyStopTimeoutTick();
}

void nsHttpConnectionMgr::DispatchSpdyPendingQ(
    nsTArray<RefPtr<PendingTransactionInfo>>& pendingQ, ConnectionEntry* ent,
    HttpConnectionBase* connH2, HttpConnectionBase* connH3) {
  if (pendingQ.Length() == 0) {
    return;
  }

  nsTArray<RefPtr<PendingTransactionInfo>> leftovers;
  uint32_t index;
  // Dispatch all the transactions we can
  for (index = 0; index < pendingQ.Length() &&
                  ((connH3 && connH3->CanDirectlyActivate()) ||
                   (connH2 && connH2->CanDirectlyActivate()));
       ++index) {
    PendingTransactionInfo* pendingTransInfo = pendingQ[index];

    // We can not dispatch NS_HTTP_ALLOW_KEEPALIVE transactions.
    // For WebTransport case, we can't dispatch the transaction here. We need to
    // put the transaction back, so TryDispatchTransaction can be called later.
    // TODO: this is hacky and should be improved.
    if (!(pendingTransInfo->Transaction()->Caps() & NS_HTTP_ALLOW_KEEPALIVE) ||
        pendingTransInfo->Transaction()->IsResettingForTunnelConn()) {
      leftovers.AppendElement(pendingTransInfo);
      continue;
    }

    // Try dispatching on HTTP3 first
    HttpConnectionBase* conn = nullptr;
    if (!(pendingTransInfo->Transaction()->Caps() & NS_HTTP_DISALLOW_HTTP3) &&
        connH3 && connH3->CanDirectlyActivate()) {
      conn = connH3;
    } else if (!(pendingTransInfo->Transaction()->Caps() &
                 NS_HTTP_DISALLOW_SPDY) &&
               connH2 && connH2->CanDirectlyActivate()) {
      conn = connH2;
    } else {
      leftovers.AppendElement(pendingTransInfo);
      continue;
    }

    nsresult rv =
        DispatchTransaction(ent, pendingTransInfo->Transaction(), conn);
    if (NS_FAILED(rv)) {
      // this cannot happen, but if due to some bug it does then
      // close the transaction
      MOZ_ASSERT(false, "Dispatch SPDY Transaction");
      LOG(("ProcessSpdyPendingQ Dispatch Transaction failed trans=%p\n",
           pendingTransInfo->Transaction()));
      pendingTransInfo->Transaction()->Close(rv);
    }
  }

  // Slurp up the rest of the pending queue into our leftovers bucket (we
  // might have some left if conn->CanDirectlyActivate returned false)
  for (; index < pendingQ.Length(); ++index) {
    PendingTransactionInfo* pendingTransInfo = pendingQ[index];
    leftovers.AppendElement(pendingTransInfo);
  }

  // Put the leftovers back in the pending queue and get rid of the
  // transactions we dispatched
  pendingQ = std::move(leftovers);
}

// This function tries to dispatch the pending h2 or h3 transactions on
// the connection entry sent in as an argument. It will do so on the
// active h2 or h3 connection either in that same entry or from the
// coalescing hash table
void nsHttpConnectionMgr::ProcessSpdyPendingQ(ConnectionEntry* ent) {
  // Look for one HTTP2 and one HTTP3 connection.
  // We may have transactions that have NS_HTTP_DISALLOW_SPDY/HTTP3 set
  // and we may need an alternative.
  HttpConnectionBase* connH3 = GetH2orH3ActiveConn(ent, true, false);
  HttpConnectionBase* connH2 = GetH2orH3ActiveConn(ent, false, true);
  if ((!connH3 || !connH3->CanDirectlyActivate()) &&
      (!connH2 || !connH2->CanDirectlyActivate())) {
    return;
  }

  nsTArray<RefPtr<PendingTransactionInfo>> urgentQ;
  ent->AppendPendingUrgentStartQ(urgentQ);
  DispatchSpdyPendingQ(urgentQ, ent, connH2, connH3);
  for (const auto& transactionInfo : Reversed(urgentQ)) {
    ent->InsertTransaction(transactionInfo);
  }

  if ((!connH3 || !connH3->CanDirectlyActivate()) &&
      (!connH2 || !connH2->CanDirectlyActivate())) {
    return;
  }

  nsTArray<RefPtr<PendingTransactionInfo>> pendingQ;
  // XXX Get all transactions for SPDY currently.
  ent->AppendPendingQForNonFocusedWindows(0, pendingQ);
  DispatchSpdyPendingQ(pendingQ, ent, connH2, connH3);

  // Put the leftovers back in the pending queue.
  for (const auto& transactionInfo : pendingQ) {
    ent->InsertTransaction(transactionInfo);
  }
}

void nsHttpConnectionMgr::OnMsgProcessAllSpdyPendingQ(int32_t, ARefBase*) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("nsHttpConnectionMgr::OnMsgProcessAllSpdyPendingQ\n"));
  for (const auto& entry : mCT.Values()) {
    ProcessSpdyPendingQ(entry.get());
  }
}

// Given a connection entry, return an active h2 or h3 connection
// that can be directly activated or null.
HttpConnectionBase* nsHttpConnectionMgr::GetH2orH3ActiveConn(
    ConnectionEntry* ent, bool aNoHttp2, bool aNoHttp3) {
  if (aNoHttp2 && aNoHttp3) {
    return nullptr;
  }
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(ent);

  // First look at ent. If protocol that ent provides is no forbidden,
  // i.e. ent use HTTP3 and !aNoHttp3 or en uses HTTP over TCP and !aNoHttp2.
  if ((!aNoHttp3 && ent->IsHttp3()) || (!aNoHttp2 && !ent->IsHttp3())) {
    HttpConnectionBase* conn = ent->GetH2orH3ActiveConn();
    if (conn) {
      return conn;
    }
  }

  nsHttpConnectionInfo* ci = ent->mConnInfo;

  // there was no active HTTP2/3 connection in the connection entry, but
  // there might be one in the hash table for coalescing
  HttpConnectionBase* existingConn =
      FindCoalescableConnection(ent, false, aNoHttp2, aNoHttp3);
  if (existingConn) {
    LOG(
        ("GetH2orH3ActiveConn() request for ent %p %s "
         "found an active connection %p in the coalescing hashtable\n",
         ent, ci->HashKey().get(), existingConn));
    return existingConn;
  }

  LOG(
      ("GetH2orH3ActiveConn() request for ent %p %s "
       "did not find an active connection\n",
       ent, ci->HashKey().get()));
  return nullptr;
}

//-----------------------------------------------------------------------------

void nsHttpConnectionMgr::AbortAndCloseAllConnections(int32_t, ARefBase*) {
  if (!OnSocketThread()) {
    Unused << PostEvent(&nsHttpConnectionMgr::AbortAndCloseAllConnections);
    return;
  }

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("nsHttpConnectionMgr::AbortAndCloseAllConnections\n"));
  for (auto iter = mCT.Iter(); !iter.Done(); iter.Next()) {
    RefPtr<ConnectionEntry> ent = iter.Data();

    // Close all active connections.
    ent->CloseActiveConnections();

    // Close all idle connections.
    ent->CloseIdleConnections();

    // Close tunneled connections
    ent->CloseExtendedCONNECTConnections();

    ent->ClosePendingConnections();

    // Close all pending transactions.
    ent->CancelAllTransactions(NS_ERROR_ABORT);

    // Close all half open tcp connections.
    ent->CloseAllDnsAndConnectSockets();

    MOZ_ASSERT(!ent->mDoNotDestroy);
    iter.Remove();
  }

  mActiveTransactions[false].Clear();
  mActiveTransactions[true].Clear();
}

void nsHttpConnectionMgr::OnMsgShutdown(int32_t, ARefBase* param) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("nsHttpConnectionMgr::OnMsgShutdown\n"));

  gHttpHandler->StopRequestTokenBucket();
  AbortAndCloseAllConnections(0, nullptr);

  // If all idle connections are removed we can stop pruning dead
  // connections.
  ConditionallyStopPruneDeadConnectionsTimer();

  if (mTimeoutTick) {
    mTimeoutTick->Cancel();
    mTimeoutTick = nullptr;
    mTimeoutTickArmed = false;
  }
  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }
  if (mTrafficTimer) {
    mTrafficTimer->Cancel();
    mTrafficTimer = nullptr;
  }
  DestroyThrottleTicker();

  mCoalescingHash.Clear();

  // signal shutdown complete
  nsCOMPtr<nsIRunnable> runnable =
      new ConnEvent(this, &nsHttpConnectionMgr::OnMsgShutdownConfirm, 0, param);
  NS_DispatchToMainThread(runnable);
}

void nsHttpConnectionMgr::OnMsgShutdownConfirm(int32_t priority,
                                               ARefBase* param) {
  MOZ_ASSERT(NS_IsMainThread());
  LOG(("nsHttpConnectionMgr::OnMsgShutdownConfirm\n"));

  BoolWrapper* shutdown = static_cast<BoolWrapper*>(param);
  shutdown->mBool = true;
}

void nsHttpConnectionMgr::OnMsgNewTransaction(int32_t priority,
                                              ARefBase* param) {
  nsHttpTransaction* trans = static_cast<nsHttpTransaction*>(param);

  LOG(("nsHttpConnectionMgr::OnMsgNewTransaction [trans=%p]\n", trans));
  trans->SetPriority(priority);
  nsresult rv = ProcessNewTransaction(trans);
  if (NS_FAILED(rv)) trans->Close(rv);  // for whatever its worth
}

void nsHttpConnectionMgr::OnMsgNewTransactionWithStickyConn(int32_t priority,
                                                            ARefBase* param) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  NewTransactionData* data = static_cast<NewTransactionData*>(param);
  LOG(
      ("nsHttpConnectionMgr::OnMsgNewTransactionWithStickyConn "
       "[trans=%p, transWithStickyConn=%p, conn=%p]\n",
       data->mTrans.get(), data->mTransWithStickyConn.get(),
       data->mTransWithStickyConn->Connection()));

  MOZ_ASSERT(data->mTransWithStickyConn &&
             data->mTransWithStickyConn->Caps() & NS_HTTP_STICKY_CONNECTION);

  data->mTrans->SetPriority(data->mPriority);

  RefPtr<nsAHttpConnection> conn = data->mTransWithStickyConn->Connection();
  if (conn && conn->IsPersistent()) {
    // This is so far a workaround to only reuse persistent
    // connection for authentication retry. See bug 459620 comment 4
    // for details.
    LOG((" Reuse connection [%p] for transaction [%p]", conn.get(),
         data->mTrans.get()));
    data->mTrans->SetConnection(conn);
  }

  nsresult rv = ProcessNewTransaction(data->mTrans);
  if (NS_FAILED(rv)) {
    data->mTrans->Close(rv);  // for whatever its worth
  }
}

void nsHttpConnectionMgr::OnMsgReschedTransaction(int32_t priority,
                                                  ARefBase* param) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("nsHttpConnectionMgr::OnMsgReschedTransaction [trans=%p]\n", param));

  RefPtr<nsHttpTransaction> trans = static_cast<nsHttpTransaction*>(param);
  trans->SetPriority(priority);

  if (!trans->ConnectionInfo()) {
    return;
  }
  ConnectionEntry* ent = mCT.GetWeak(trans->ConnectionInfo()->HashKey());

  if (ent) {
    ent->ReschedTransaction(trans);
  }
}

void nsHttpConnectionMgr::OnMsgUpdateClassOfServiceOnTransaction(
    ClassOfService cos, ARefBase* param) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(
      ("nsHttpConnectionMgr::OnMsgUpdateClassOfServiceOnTransaction "
       "[trans=%p]\n",
       param));

  nsHttpTransaction* trans = static_cast<nsHttpTransaction*>(param);

  ClassOfService previous = trans->GetClassOfService();
  trans->SetClassOfService(cos);

  // incremental change alone will not trigger a reschedule
  if ((previous.Flags() ^ cos.Flags()) &
      (NS_HTTP_LOAD_AS_BLOCKING | NS_HTTP_LOAD_UNBLOCKED)) {
    Unused << RescheduleTransaction(trans, trans->Priority());
  }
}

void nsHttpConnectionMgr::OnMsgCancelTransaction(int32_t reason,
                                                 ARefBase* param) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("nsHttpConnectionMgr::OnMsgCancelTransaction [trans=%p]\n", param));

  nsresult closeCode = static_cast<nsresult>(reason);

  // caller holds a ref to param/trans on stack
  nsHttpTransaction* trans = static_cast<nsHttpTransaction*>(param);

  //
  // if the transaction owns a connection and the transaction is not done,
  // then ask the connection to close the transaction.  otherwise, close the
  // transaction directly (removing it from the pending queue first).
  //
  RefPtr<nsAHttpConnection> conn(trans->Connection());
  if (conn && !trans->IsDone()) {
    conn->CloseTransaction(trans, closeCode);
  } else {
    ConnectionEntry* ent = nullptr;
    if (trans->ConnectionInfo()) {
      ent = mCT.GetWeak(trans->ConnectionInfo()->HashKey());
    }
    if (ent && ent->RemoveTransFromPendingQ(trans)) {
      LOG(
          ("nsHttpConnectionMgr::OnMsgCancelTransaction [trans=%p]"
           " removed from pending queue\n",
           trans));
    }

    trans->Close(closeCode);

    // Cancel is a pretty strong signal that things might be hanging
    // so we want to cancel any null transactions related to this connection
    // entry. They are just optimizations, but they aren't hooked up to
    // anything that might get canceled from the rest of gecko, so best
    // to assume that's what was meant by the cancel we did receive if
    // it only applied to something in the queue.
    if (ent) {
      ent->CloseAllActiveConnsWithNullTransactcion(closeCode);
    }
  }
}

void nsHttpConnectionMgr::OnMsgProcessPendingQ(int32_t, ARefBase* param) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  nsHttpConnectionInfo* ci = static_cast<nsHttpConnectionInfo*>(param);

  if (!ci) {
    LOG(("nsHttpConnectionMgr::OnMsgProcessPendingQ [ci=nullptr]\n"));
    // Try and dispatch everything
    for (const auto& entry : mCT.Values()) {
      Unused << ProcessPendingQForEntry(entry.get(), true);
    }
    return;
  }

  LOG(("nsHttpConnectionMgr::OnMsgProcessPendingQ [ci=%s]\n",
       ci->HashKey().get()));

  // start by processing the queue identified by the given connection info.
  ConnectionEntry* ent = mCT.GetWeak(ci->HashKey());
  if (!(ent && ProcessPendingQForEntry(ent, false))) {
    // if we reach here, it means that we couldn't dispatch a transaction
    // for the specified connection info.  walk the connection table...
    for (const auto& entry : mCT.Values()) {
      if (ProcessPendingQForEntry(entry.get(), false)) {
        break;
      }
    }
  }
}

nsresult nsHttpConnectionMgr::CancelTransactions(nsHttpConnectionInfo* ci,
                                                 nsresult code) {
  LOG(("nsHttpConnectionMgr::CancelTransactions %s\n", ci->HashKey().get()));

  int32_t intReason = static_cast<int32_t>(code);
  return PostEvent(&nsHttpConnectionMgr::OnMsgCancelTransactions, intReason,
                   ci);
}

void nsHttpConnectionMgr::OnMsgCancelTransactions(int32_t code,
                                                  ARefBase* param) {
  nsresult reason = static_cast<nsresult>(code);
  nsHttpConnectionInfo* ci = static_cast<nsHttpConnectionInfo*>(param);
  ConnectionEntry* ent = mCT.GetWeak(ci->HashKey());
  LOG(("nsHttpConnectionMgr::OnMsgCancelTransactions %s %p\n",
       ci->HashKey().get(), ent));
  if (ent) {
    ent->CancelAllTransactions(reason);
  }
}

void nsHttpConnectionMgr::OnMsgPruneDeadConnections(int32_t, ARefBase*) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("nsHttpConnectionMgr::OnMsgPruneDeadConnections\n"));

  // Reset mTimeOfNextWakeUp so that we can find a new shortest value.
  mTimeOfNextWakeUp = UINT64_MAX;

  // check canreuse() for all idle connections plus any active connections on
  // connection entries that are using spdy.
  if (mNumIdleConns ||
      (mNumActiveConns && StaticPrefs::network_http_http2_enabled())) {
    for (auto iter = mCT.Iter(); !iter.Done(); iter.Next()) {
      RefPtr<ConnectionEntry> ent = iter.Data();

      LOG(("  pruning [ci=%s]\n", ent->mConnInfo->HashKey().get()));

      // Find out how long it will take for next idle connection to not
      // be reusable anymore.
      uint32_t timeToNextExpire = ent->PruneDeadConnections();

      // If time to next expire found is shorter than time to next
      // wake-up, we need to change the time for next wake-up.
      if (timeToNextExpire != UINT32_MAX) {
        uint32_t now = NowInSeconds();
        uint64_t timeOfNextExpire = now + timeToNextExpire;
        // If pruning of dead connections is not already scheduled to
        // happen or time found for next connection to expire is is
        // before mTimeOfNextWakeUp, we need to schedule the pruning to
        // happen after timeToNextExpire.
        if (!mTimer || timeOfNextExpire < mTimeOfNextWakeUp) {
          PruneDeadConnectionsAfter(timeToNextExpire);
        }
      } else {
        ConditionallyStopPruneDeadConnectionsTimer();
      }

      ent->RemoveEmptyPendingQ();

      // If this entry is empty, we have too many entries busy then
      // we can clean it up and restart
      if (mCT.Count() > 125 && ent->IdleConnectionsLength() == 0 &&
          ent->ActiveConnsLength() == 0 &&
          ent->DnsAndConnectSocketsLength() == 0 &&
          ent->PendingQueueLength() == 0 &&
          ent->UrgentStartQueueLength() == 0 && !ent->mDoNotDestroy &&
          (!ent->mUsingSpdy || mCT.Count() > 300)) {
        LOG(("    removing empty connection entry\n"));
        iter.Remove();
        continue;
      }

      // Otherwise use this opportunity to compact our arrays...
      ent->Compact();
    }
  }
}

void nsHttpConnectionMgr::OnMsgPruneNoTraffic(int32_t, ARefBase*) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("nsHttpConnectionMgr::OnMsgPruneNoTraffic\n"));

  // Prune connections without traffic
  for (const RefPtr<ConnectionEntry>& ent : mCT.Values()) {
    // Close the connections with no registered traffic.
    ent->PruneNoTraffic();
  }

  mPruningNoTraffic = false;  // not pruning anymore
}

void nsHttpConnectionMgr::OnMsgVerifyTraffic(int32_t, ARefBase*) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("nsHttpConnectionMgr::OnMsgVerifyTraffic\n"));

  if (mPruningNoTraffic) {
    // Called in the time gap when the timeout to prune notraffic
    // connections has triggered but the pruning hasn't happened yet.
    return;
  }

  mCoalescingHash.Clear();

  // Mark connections for traffic verification
  for (const auto& entry : mCT.Values()) {
    entry->ResetIPFamilyPreference();
    entry->VerifyTraffic();
  }

  // If the timer is already there. we just re-init it
  if (!mTrafficTimer) {
    mTrafficTimer = NS_NewTimer();
  }

  // failure to create a timer is not a fatal error, but dead
  // connections will not be cleaned up as nicely
  if (mTrafficTimer) {
    // Give active connections time to get more traffic before killing
    // them off. Default: 5000 milliseconds
    mTrafficTimer->Init(this, gHttpHandler->NetworkChangedTimeout(),
                        nsITimer::TYPE_ONE_SHOT);
  } else {
    NS_WARNING("failed to create timer for VerifyTraffic!");
  }
  // Calling ActivateTimeoutTick to ensure the next timeout tick is 1s.
  ActivateTimeoutTick();
}

void nsHttpConnectionMgr::OnMsgDoShiftReloadConnectionCleanup(int32_t,
                                                              ARefBase* param) {
  LOG(("nsHttpConnectionMgr::OnMsgDoShiftReloadConnectionCleanup\n"));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  mCoalescingHash.Clear();

  nsHttpConnectionInfo* ci = static_cast<nsHttpConnectionInfo*>(param);

  for (const auto& entry : mCT.Values()) {
    entry->ClosePersistentConnections();
  }

  if (ci) ResetIPFamilyPreference(ci);
}

void nsHttpConnectionMgr::OnMsgDoSingleConnectionCleanup(int32_t,
                                                         ARefBase* param) {
  LOG(("nsHttpConnectionMgr::OnMsgDoSingleConnectionCleanup\n"));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  nsHttpConnectionInfo* ci = static_cast<nsHttpConnectionInfo*>(param);

  if (!ci) {
    return;
  }

  ConnectionEntry* entry = mCT.GetWeak(ci->HashKey());
  if (entry) {
    entry->ClosePersistentConnections();
  }

  ResetIPFamilyPreference(ci);
}

void nsHttpConnectionMgr::OnMsgReclaimConnection(HttpConnectionBase* conn) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  //
  // 1) remove the connection from the active list
  // 2) if keep-alive, add connection to idle list
  // 3) post event to process the pending transaction queue
  //

  MOZ_ASSERT(conn);
  ConnectionEntry* ent = conn->ConnectionInfo()
                             ? mCT.GetWeak(conn->ConnectionInfo()->HashKey())
                             : nullptr;

  if (!ent) {
    // this can happen if the connection is made outside of the
    // connection manager and is being "reclaimed" for use with
    // future transactions. HTTP/2 tunnels work like this.
    bool isWildcard = false;
    ent = GetOrCreateConnectionEntry(conn->ConnectionInfo(), true, false, false,
                                     &isWildcard);
    LOG(
        ("nsHttpConnectionMgr::OnMsgReclaimConnection conn %p "
         "forced new hash entry %s\n",
         conn, conn->ConnectionInfo()->HashKey().get()));
  }

  MOZ_ASSERT(ent);
  RefPtr<nsHttpConnectionInfo> ci(ent->mConnInfo);

  LOG(("nsHttpConnectionMgr::OnMsgReclaimConnection [ent=%p conn=%p]\n", ent,
       conn));

  // If the connection is in the active list, remove that entry
  // and the reference held by the mActiveConns list.
  // This is never the final reference on conn as the event context
  // is also holding one that is released at the end of this function.

  RefPtr<nsHttpConnection> connTCP = do_QueryObject(conn);
  if (!connTCP || connTCP->EverUsedSpdy()) {
    // Spdyand Http3 connections aren't reused in the traditional HTTP way in
    // the idleconns list, they are actively multplexed as active
    // conns. Even when they have 0 transactions on them they are
    // considered active connections. So when one is reclaimed it
    // is really complete and is meant to be shut down and not
    // reused.
    conn->DontReuse();
  }

  // a connection that still holds a reference to a transaction was
  // not closed naturally (i.e. it was reset or aborted) and is
  // therefore not something that should be reused.
  if (conn->Transaction()) {
    conn->DontReuse();
  }

  if (NS_SUCCEEDED(ent->RemoveActiveConnection(conn)) ||
      NS_SUCCEEDED(ent->RemovePendingConnection(conn))) {
  } else if (!connTCP || connTCP->EverUsedSpdy()) {
    LOG(("HttpConnectionBase %p not found in its connection entry, try ^anon",
         conn));
    // repeat for flipped anon flag as we share connection entries for spdy
    // connections.
    RefPtr<nsHttpConnectionInfo> anonInvertedCI(ci->Clone());
    anonInvertedCI->SetAnonymous(!ci->GetAnonymous());

    ConnectionEntry* ent = mCT.GetWeak(anonInvertedCI->HashKey());
    if (ent) {
      if (NS_SUCCEEDED(ent->RemoveActiveConnection(conn))) {
      } else {
        LOG(
            ("HttpConnectionBase %p could not be removed from its entry's "
             "active list",
             conn));
      }
    }
  }

  if (connTCP && connTCP->CanReuse()) {
    LOG(("  adding connection to idle list\n"));
    // Keep The idle connection list sorted with the connections that
    // have moved the largest data pipelines at the front because these
    // connections have the largest cwnds on the server.

    // The linear search is ok here because the number of idleconns
    // in a single entry is generally limited to a small number (i.e. 6)

    ent->InsertIntoIdleConnections(connTCP);
  } else {
    if (ent->IsInExtendedCONNECTConns(conn)) {
      ent->RemoveExtendedCONNECTConns(conn);
    }
    LOG(("  connection cannot be reused; closing connection\n"));
    conn->SetCloseReason(ConnectionCloseReason::CANT_REUSED);
    conn->Close(NS_ERROR_ABORT);
  }

  OnMsgProcessPendingQ(0, ci);
}

void nsHttpConnectionMgr::OnMsgCompleteUpgrade(int32_t, ARefBase* param) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  nsresult rv = NS_OK;
  nsCompleteUpgradeData* data = static_cast<nsCompleteUpgradeData*>(param);
  MOZ_ASSERT(data->mTrans && data->mTrans->Caps() & NS_HTTP_STICKY_CONNECTION);

  RefPtr<nsAHttpConnection> conn(data->mTrans->Connection());
  LOG(
      ("nsHttpConnectionMgr::OnMsgCompleteUpgrade "
       "conn=%p listener=%p wrapped=%d\n",
       conn.get(), data->mUpgradeListener.get(), data->mJsWrapped));

  if (!conn) {
    // Delay any error reporting to happen in transportAvailableFunc
    rv = NS_ERROR_UNEXPECTED;
  } else {
    MOZ_ASSERT(!data->mSocketTransport);
    rv = conn->TakeTransport(getter_AddRefs(data->mSocketTransport),
                             getter_AddRefs(data->mSocketIn),
                             getter_AddRefs(data->mSocketOut));

    if (NS_FAILED(rv)) {
      LOG(("  conn->TakeTransport failed with %" PRIx32,
           static_cast<uint32_t>(rv)));
    }
  }

  RefPtr<nsCompleteUpgradeData> upgradeData(data);

  nsCOMPtr<nsIAsyncInputStream> socketIn;
  nsCOMPtr<nsIAsyncOutputStream> socketOut;

  // If this is for JS, the input and output sockets need to be piped over the
  // socket thread. Otherwise, the JS may attempt to read and/or write the
  // sockets on the main thread, which could cause network I/O on the main
  // thread. This is particularly bad in the case of TLS connections, because
  // PSM and NSS rely on those connections only being used on the socket
  // thread.
  if (data->mJsWrapped) {
    nsCOMPtr<nsIAsyncInputStream> pipeIn;
    uint32_t segsize = 0;
    uint32_t segcount = 0;
    net_ResolveSegmentParams(segsize, segcount);
    if (NS_SUCCEEDED(rv)) {
      NS_NewPipe2(getter_AddRefs(pipeIn), getter_AddRefs(socketOut), true, true,
                  segsize, segcount);
      rv = NS_AsyncCopy(pipeIn, data->mSocketOut, gSocketTransportService,
                        NS_ASYNCCOPY_VIA_READSEGMENTS, segsize);
    }

    nsCOMPtr<nsIAsyncOutputStream> pipeOut;
    if (NS_SUCCEEDED(rv)) {
      NS_NewPipe2(getter_AddRefs(socketIn), getter_AddRefs(pipeOut), true, true,
                  segsize, segcount);
      rv = NS_AsyncCopy(data->mSocketIn, pipeOut, gSocketTransportService,
                        NS_ASYNCCOPY_VIA_WRITESEGMENTS, segsize);
    }
  } else {
    socketIn = upgradeData->mSocketIn;
    socketOut = upgradeData->mSocketOut;
  }

  auto transportAvailableFunc = [upgradeData{std::move(upgradeData)}, socketIn,
                                 socketOut, aRv(rv)]() {
    // Handle any potential previous errors first
    // and call OnUpgradeFailed if necessary.
    nsresult rv = aRv;

    if (NS_FAILED(rv)) {
      rv = upgradeData->mUpgradeListener->OnUpgradeFailed(rv);
      if (NS_FAILED(rv)) {
        LOG(
            ("nsHttpConnectionMgr::OnMsgCompleteUpgrade OnUpgradeFailed failed."
             " listener=%p\n",
             upgradeData->mUpgradeListener.get()));
      }
      return;
    }

    rv = upgradeData->mUpgradeListener->OnTransportAvailable(
        upgradeData->mSocketTransport, socketIn, socketOut);
    if (NS_FAILED(rv)) {
      LOG(
          ("nsHttpConnectionMgr::OnMsgCompleteUpgrade OnTransportAvailable "
           "failed. listener=%p\n",
           upgradeData->mUpgradeListener.get()));
    }
  };

  if (data->mJsWrapped) {
    LOG(
        ("nsHttpConnectionMgr::OnMsgCompleteUpgrade "
         "conn=%p listener=%p wrapped=%d pass to main thread\n",
         conn.get(), data->mUpgradeListener.get(), data->mJsWrapped));
    NS_DispatchToMainThread(
        NS_NewRunnableFunction("net::nsHttpConnectionMgr::OnMsgCompleteUpgrade",
                               transportAvailableFunc));
  } else {
    transportAvailableFunc();
  }
}

void nsHttpConnectionMgr::OnMsgUpdateParam(int32_t inParam, ARefBase*) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  uint32_t param = static_cast<uint32_t>(inParam);
  uint16_t name = ((param) & 0xFFFF0000) >> 16;
  uint16_t value = param & 0x0000FFFF;

  switch (name) {
    case MAX_CONNECTIONS:
      mMaxConns = value;
      break;
    case MAX_URGENT_START_Q:
      mMaxUrgentExcessiveConns = value;
      break;
    case MAX_PERSISTENT_CONNECTIONS_PER_HOST:
      mMaxPersistConnsPerHost = value;
      break;
    case MAX_PERSISTENT_CONNECTIONS_PER_PROXY:
      mMaxPersistConnsPerProxy = value;
      break;
    case MAX_REQUEST_DELAY:
      mMaxRequestDelay = value;
      break;
    case THROTTLING_ENABLED:
      SetThrottlingEnabled(!!value);
      break;
    case THROTTLING_SUSPEND_FOR:
      mThrottleSuspendFor = value;
      break;
    case THROTTLING_RESUME_FOR:
      mThrottleResumeFor = value;
      break;
    case THROTTLING_HOLD_TIME:
      mThrottleHoldTime = value;
      break;
    case THROTTLING_MAX_TIME:
      mThrottleMaxTime = TimeDuration::FromMilliseconds(value);
      break;
    case PROXY_BE_CONSERVATIVE:
      mBeConservativeForProxy = !!value;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unexpected parameter name");
  }
}

// Read Timeout Tick handlers

void nsHttpConnectionMgr::ActivateTimeoutTick() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(
      ("nsHttpConnectionMgr::ActivateTimeoutTick() "
       "this=%p mTimeoutTick=%p\n",
       this, mTimeoutTick.get()));

  // The timer tick should be enabled if it is not already pending.
  // Upon running the tick will rearm itself if there are active
  // connections available.

  if (mTimeoutTick && mTimeoutTickArmed) {
    // make sure we get one iteration on a quick tick
    if (mTimeoutTickNext > 1) {
      mTimeoutTickNext = 1;
      mTimeoutTick->SetDelay(1000);
    }
    return;
  }

  if (!mTimeoutTick) {
    mTimeoutTick = NS_NewTimer();
    if (!mTimeoutTick) {
      NS_WARNING("failed to create timer for http timeout management");
      return;
    }
    ReentrantMonitorAutoEnter mon(mReentrantMonitor);
    if (!mSocketThreadTarget) {
      NS_WARNING("cannot activate timout if not initialized or shutdown");
      return;
    }
    mTimeoutTick->SetTarget(mSocketThreadTarget);
  }

  if (mIsShuttingDown) {  // Atomic
    // don't set a timer to generate an event if we're shutting down
    // (and mSocketThreadTarget might be null or garbage if we're shutting down)
    return;
  }
  MOZ_ASSERT(!mTimeoutTickArmed, "timer tick armed");
  mTimeoutTickArmed = true;
  mTimeoutTick->Init(this, 1000, nsITimer::TYPE_REPEATING_SLACK);
}

class UINT64Wrapper : public ARefBase {
 public:
  explicit UINT64Wrapper(uint64_t aUint64) : mUint64(aUint64) {}
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(UINT64Wrapper, override)

  uint64_t GetValue() { return mUint64; }

 private:
  uint64_t mUint64;
  virtual ~UINT64Wrapper() = default;
};

nsresult nsHttpConnectionMgr::UpdateCurrentBrowserId(uint64_t aId) {
  RefPtr<UINT64Wrapper> idWrapper = new UINT64Wrapper(aId);
  return PostEvent(&nsHttpConnectionMgr::OnMsgUpdateCurrentBrowserId, 0,
                   idWrapper);
}

void nsHttpConnectionMgr::SetThrottlingEnabled(bool aEnable) {
  LOG(("nsHttpConnectionMgr::SetThrottlingEnabled enable=%d", aEnable));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  mThrottleEnabled = aEnable;

  if (mThrottleEnabled) {
    EnsureThrottleTickerIfNeeded();
  } else {
    DestroyThrottleTicker();
    ResumeReadOf(mActiveTransactions[false]);
    ResumeReadOf(mActiveTransactions[true]);
  }
}

bool nsHttpConnectionMgr::InThrottlingTimeWindow() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (mThrottlingWindowEndsAt.IsNull()) {
    return true;
  }
  return TimeStamp::NowLoRes() <= mThrottlingWindowEndsAt;
}

void nsHttpConnectionMgr::TouchThrottlingTimeWindow(bool aEnsureTicker) {
  LOG(("nsHttpConnectionMgr::TouchThrottlingTimeWindow"));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  mThrottlingWindowEndsAt = TimeStamp::NowLoRes() + mThrottleMaxTime;

  if (!mThrottleTicker && MOZ_LIKELY(aEnsureTicker) &&
      MOZ_LIKELY(mThrottleEnabled)) {
    EnsureThrottleTickerIfNeeded();
  }
}

void nsHttpConnectionMgr::LogActiveTransactions(char operation) {
  if (!LOG_ENABLED()) {
    return;
  }

  nsTArray<RefPtr<nsHttpTransaction>>* trs = nullptr;
  uint32_t au, at, bu = 0, bt = 0;

  trs = mActiveTransactions[false].Get(mCurrentBrowserId);
  au = trs ? trs->Length() : 0;
  trs = mActiveTransactions[true].Get(mCurrentBrowserId);
  at = trs ? trs->Length() : 0;

  for (const auto& data : mActiveTransactions[false].Values()) {
    bu += data->Length();
  }
  bu -= au;
  for (const auto& data : mActiveTransactions[true].Values()) {
    bt += data->Length();
  }
  bt -= at;

  // Shows counts of:
  // - unthrottled transaction for the active tab
  // - throttled transaction for the active tab
  // - unthrottled transaction for background tabs
  // - throttled transaction for background tabs
  LOG(("Active transactions %c[%u,%u,%u,%u]", operation, au, at, bu, bt));
}

void nsHttpConnectionMgr::AddActiveTransaction(nsHttpTransaction* aTrans) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  uint64_t tabId = aTrans->BrowserId();
  bool throttled = aTrans->EligibleForThrottling();

  nsTArray<RefPtr<nsHttpTransaction>>* transactions =
      mActiveTransactions[throttled].GetOrInsertNew(tabId);

  MOZ_ASSERT(!transactions->Contains(aTrans));

  transactions->AppendElement(aTrans);

  LOG(("nsHttpConnectionMgr::AddActiveTransaction    t=%p tabid=%" PRIx64
       "(%d) thr=%d",
       aTrans, tabId, tabId == mCurrentBrowserId, throttled));
  LogActiveTransactions('+');

  if (tabId == mCurrentBrowserId) {
    mActiveTabTransactionsExist = true;
    if (!throttled) {
      mActiveTabUnthrottledTransactionsExist = true;
    }
  }

  // Shift the throttling window to the future (actually, makes sure
  // that throttling will engage when there is anything to throttle.)
  // The |false| argument means we don't need this call to ensure
  // the ticker, since we do it just below.  Calling
  // EnsureThrottleTickerIfNeeded directly does a bit more than call
  // from inside of TouchThrottlingTimeWindow.
  TouchThrottlingTimeWindow(false);

  if (!mThrottleEnabled) {
    return;
  }

  EnsureThrottleTickerIfNeeded();
}

void nsHttpConnectionMgr::RemoveActiveTransaction(
    nsHttpTransaction* aTrans, Maybe<bool> const& aOverride) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  uint64_t tabId = aTrans->BrowserId();
  bool forActiveTab = tabId == mCurrentBrowserId;
  bool throttled = aOverride.valueOr(aTrans->EligibleForThrottling());

  nsTArray<RefPtr<nsHttpTransaction>>* transactions =
      mActiveTransactions[throttled].Get(tabId);

  if (!transactions || !transactions->RemoveElement(aTrans)) {
    // Was not tracked as active, probably just ignore.
    return;
  }

  LOG(("nsHttpConnectionMgr::RemoveActiveTransaction t=%p tabid=%" PRIx64
       "(%d) thr=%d",
       aTrans, tabId, forActiveTab, throttled));

  if (!transactions->IsEmpty()) {
    // There are still transactions of the type, hence nothing in the throttling
    // conditions has changed and we don't need to update "Exists" caches nor we
    // need to wake any now throttled transactions.
    LogActiveTransactions('-');
    return;
  }

  // To optimize the following logic, always remove the entry when the array is
  // empty.
  mActiveTransactions[throttled].Remove(tabId);
  LogActiveTransactions('-');

  if (forActiveTab) {
    // Update caches of the active tab transaction existence, since it's now
    // affected
    if (!throttled) {
      mActiveTabUnthrottledTransactionsExist = false;
    }
    if (mActiveTabTransactionsExist) {
      mActiveTabTransactionsExist =
          mActiveTransactions[!throttled].Contains(tabId);
    }
  }

  if (!mThrottleEnabled) {
    return;
  }

  bool unthrottledExist = !mActiveTransactions[false].IsEmpty();
  bool throttledExist = !mActiveTransactions[true].IsEmpty();

  if (!unthrottledExist && !throttledExist) {
    // Nothing active globally, just get rid of the timer completely and we are
    // done.
    MOZ_ASSERT(!mActiveTabUnthrottledTransactionsExist);
    MOZ_ASSERT(!mActiveTabTransactionsExist);

    DestroyThrottleTicker();
    return;
  }

  if (!mThrottlingInhibitsReading) {
    // There is then nothing to wake up.  Affected transactions will not be
    // put to sleep automatically on next tick.
    LOG(("  reading not currently inhibited"));
    return;
  }

  if (mActiveTabUnthrottledTransactionsExist) {
    // There are still unthrottled transactions for the active tab, hence the
    // state is unaffected and we don't need to do anything (nothing to wake).
    LOG(("  there are unthrottled for the active tab"));
    return;
  }

  if (mActiveTabTransactionsExist) {
    // There are only trottled transactions for the active tab.
    // If the last transaction we just removed was a non-throttled for the
    // active tab we can wake the throttled transactions for the active tab.
    if (forActiveTab && !throttled) {
      LOG(("  resuming throttled for active tab"));
      ResumeReadOf(mActiveTransactions[true].Get(mCurrentBrowserId));
    }
    return;
  }

  if (!unthrottledExist) {
    // There are no unthrottled transactions for any tab.  Resume all throttled,
    // all are only for background tabs.
    LOG(("  delay resuming throttled for background tabs"));
    DelayedResumeBackgroundThrottledTransactions();
    return;
  }

  if (forActiveTab) {
    // Removing the last transaction for the active tab frees up the unthrottled
    // background tabs transactions.
    LOG(("  delay resuming unthrottled for background tabs"));
    DelayedResumeBackgroundThrottledTransactions();
    return;
  }

  LOG(("  not resuming anything"));
}

void nsHttpConnectionMgr::UpdateActiveTransaction(nsHttpTransaction* aTrans) {
  LOG(("nsHttpConnectionMgr::UpdateActiveTransaction ENTER t=%p", aTrans));

  // First remove then add.  In case of a download that is the only active
  // transaction and has just been marked as download (goes unthrottled to
  // throttled), adding first would cause it to be throttled for first few
  // milliseconds - becuause it would appear as if there were both throttled
  // and unthrottled transactions at the time.

  Maybe<bool> reversed;
  reversed.emplace(!aTrans->EligibleForThrottling());
  RemoveActiveTransaction(aTrans, reversed);

  AddActiveTransaction(aTrans);

  LOG(("nsHttpConnectionMgr::UpdateActiveTransaction EXIT t=%p", aTrans));
}

bool nsHttpConnectionMgr::ShouldThrottle(nsHttpTransaction* aTrans) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  LOG(("nsHttpConnectionMgr::ShouldThrottle trans=%p", aTrans));

  if (!mThrottlingInhibitsReading || !mThrottleEnabled) {
    return false;
  }

  uint64_t tabId = aTrans->BrowserId();
  bool forActiveTab = tabId == mCurrentBrowserId;
  bool throttled = aTrans->EligibleForThrottling();

  bool stop = [=]() {
    if (mActiveTabTransactionsExist) {
      if (!tabId) {
        // Chrome initiated and unidentified transactions just respect
        // their throttle flag, when something for the active tab is happening.
        // This also includes downloads.
        LOG(("  active tab loads, trans is tab-less, throttled=%d", throttled));
        return throttled;
      }
      if (!forActiveTab) {
        // This is a background tab request, we want them to always throttle
        // when there are transactions running for the ative tab.
        LOG(("  active tab loads, trans not of the active tab"));
        return true;
      }

      if (mActiveTabUnthrottledTransactionsExist) {
        // Unthrottled transactions for the active tab take precedence
        LOG(("  active tab loads unthrottled, trans throttled=%d", throttled));
        return throttled;
      }

      LOG(("  trans for active tab, don't throttle"));
      return false;
    }

    MOZ_ASSERT(!forActiveTab);

    if (!mActiveTransactions[false].IsEmpty()) {
      // This means there are unthrottled active transactions for background
      // tabs. If we are here, there can't be any transactions for the active
      // tab. (If there is no transaction for a tab id, there is no entry for it
      // in the hashtable.)
      LOG(("  backround tab(s) load unthrottled, trans throttled=%d",
           throttled));
      return throttled;
    }

    // There are only unthrottled transactions for background tabs: don't
    // throttle.
    LOG(("  backround tab(s) load throttled, don't throttle"));
    return false;
  }();

  if (forActiveTab && !stop) {
    // This is an active-tab transaction and is allowed to read.  Hence,
    // prolong the throttle time window to make sure all 'lower-decks'
    // transactions will actually throttle.
    TouchThrottlingTimeWindow();
    return false;
  }

  // Only stop reading when in the configured throttle max-time (aka time
  // window). This window is prolonged (restarted) by a call to
  // TouchThrottlingTimeWindow called on new transaction activation or on
  // receive of response bytes of an active tab transaction.
  bool inWindow = InThrottlingTimeWindow();

  LOG(("  stop=%d, in-window=%d, delayed-bck-timer=%d", stop, inWindow,
       !!mDelayedResumeReadTimer));

  if (!forActiveTab) {
    // If the delayed background resume timer exists, background transactions
    // are scheduled to be woken after a delay, hence leave them throttled.
    inWindow = inWindow || mDelayedResumeReadTimer;
  }

  return stop && inWindow;
}

bool nsHttpConnectionMgr::IsConnEntryUnderPressure(
    nsHttpConnectionInfo* connInfo) {
  ConnectionEntry* ent = mCT.GetWeak(connInfo->HashKey());
  if (!ent) {
    // No entry, no pressure.
    return false;
  }

  return ent->PendingQueueLengthForWindow(mCurrentBrowserId) > 0;
}

bool nsHttpConnectionMgr::IsThrottleTickerNeeded() {
  LOG(("nsHttpConnectionMgr::IsThrottleTickerNeeded"));

  if (mActiveTabUnthrottledTransactionsExist &&
      mActiveTransactions[false].Count() > 1) {
    LOG(("  there are unthrottled transactions for both active and bck"));
    return true;
  }

  if (mActiveTabTransactionsExist && mActiveTransactions[true].Count() > 1) {
    LOG(("  there are throttled transactions for both active and bck"));
    return true;
  }

  if (!mActiveTransactions[true].IsEmpty() &&
      !mActiveTransactions[false].IsEmpty()) {
    LOG(("  there are both throttled and unthrottled transactions"));
    return true;
  }

  LOG(("  nothing to throttle"));
  return false;
}

void nsHttpConnectionMgr::EnsureThrottleTickerIfNeeded() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  LOG(("nsHttpConnectionMgr::EnsureThrottleTickerIfNeeded"));
  if (!IsThrottleTickerNeeded()) {
    return;
  }

  // There is a new demand to throttle, hence unschedule delayed resume
  // of background throttled transastions.
  CancelDelayedResumeBackgroundThrottledTransactions();

  if (mThrottleTicker) {
    return;
  }

  mThrottleTicker = NS_NewTimer();
  if (mThrottleTicker) {
    MOZ_ASSERT(!mThrottlingInhibitsReading);

    mThrottleTicker->Init(this, mThrottleSuspendFor, nsITimer::TYPE_ONE_SHOT);
    mThrottlingInhibitsReading = true;
  }

  LogActiveTransactions('^');
}

// Can be called with or without the monitor held
void nsHttpConnectionMgr::DestroyThrottleTicker() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  // Nothing to throttle, hence no need for this timer anymore.
  CancelDelayedResumeBackgroundThrottledTransactions();

  MOZ_ASSERT(!mThrottleEnabled || !IsThrottleTickerNeeded());

  if (!mThrottleTicker) {
    return;
  }

  LOG(("nsHttpConnectionMgr::DestroyThrottleTicker"));
  mThrottleTicker->Cancel();
  mThrottleTicker = nullptr;

  mThrottlingInhibitsReading = false;

  LogActiveTransactions('v');
}

void nsHttpConnectionMgr::ThrottlerTick() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  mThrottlingInhibitsReading = !mThrottlingInhibitsReading;

  LOG(("nsHttpConnectionMgr::ThrottlerTick inhibit=%d",
       mThrottlingInhibitsReading));

  // If there are only background transactions to be woken after a delay, keep
  // the ticker so that we woke them only for the resume-for interval and then
  // throttle them again until the background-resume delay passes.
  if (!mThrottlingInhibitsReading && !mDelayedResumeReadTimer &&
      (!IsThrottleTickerNeeded() || !InThrottlingTimeWindow())) {
    LOG(("  last tick"));
    mThrottleTicker = nullptr;
  }

  if (mThrottlingInhibitsReading) {
    if (mThrottleTicker) {
      mThrottleTicker->Init(this, mThrottleSuspendFor, nsITimer::TYPE_ONE_SHOT);
    }
  } else {
    if (mThrottleTicker) {
      mThrottleTicker->Init(this, mThrottleResumeFor, nsITimer::TYPE_ONE_SHOT);
    }

    ResumeReadOf(mActiveTransactions[false], true);
    ResumeReadOf(mActiveTransactions[true]);
  }
}

void nsHttpConnectionMgr::DelayedResumeBackgroundThrottledTransactions() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (mDelayedResumeReadTimer) {
    return;
  }

  LOG(("nsHttpConnectionMgr::DelayedResumeBackgroundThrottledTransactions"));
  NS_NewTimerWithObserver(getter_AddRefs(mDelayedResumeReadTimer), this,
                          mThrottleHoldTime, nsITimer::TYPE_ONE_SHOT);
}

void nsHttpConnectionMgr::CancelDelayedResumeBackgroundThrottledTransactions() {
  if (!mDelayedResumeReadTimer) {
    return;
  }

  LOG(
      ("nsHttpConnectionMgr::"
       "CancelDelayedResumeBackgroundThrottledTransactions"));
  mDelayedResumeReadTimer->Cancel();
  mDelayedResumeReadTimer = nullptr;
}

void nsHttpConnectionMgr::ResumeBackgroundThrottledTransactions() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  LOG(("nsHttpConnectionMgr::ResumeBackgroundThrottledTransactions"));
  mDelayedResumeReadTimer = nullptr;

  if (!IsThrottleTickerNeeded()) {
    DestroyThrottleTicker();
  }

  if (!mActiveTransactions[false].IsEmpty()) {
    ResumeReadOf(mActiveTransactions[false], true);
  } else {
    ResumeReadOf(mActiveTransactions[true], true);
  }
}

void nsHttpConnectionMgr::ResumeReadOf(
    nsClassHashtable<nsUint64HashKey, nsTArray<RefPtr<nsHttpTransaction>>>&
        hashtable,
    bool excludeForActiveTab) {
  for (const auto& entry : hashtable) {
    if (excludeForActiveTab && entry.GetKey() == mCurrentBrowserId) {
      // These have never been throttled (never stopped reading)
      continue;
    }
    ResumeReadOf(entry.GetWeak());
  }
}

void nsHttpConnectionMgr::ResumeReadOf(
    nsTArray<RefPtr<nsHttpTransaction>>* transactions) {
  MOZ_ASSERT(transactions);

  for (const auto& trans : *transactions) {
    trans->ResumeReading();
  }
}

void nsHttpConnectionMgr::NotifyConnectionOfBrowserIdChange(
    uint64_t previousId) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  nsTArray<RefPtr<nsHttpTransaction>>* transactions = nullptr;
  nsTArray<RefPtr<nsAHttpConnection>> connections;

  auto addConnectionHelper =
      [&connections](nsTArray<RefPtr<nsHttpTransaction>>* trans) {
        if (!trans) {
          return;
        }

        for (const auto& t : *trans) {
          RefPtr<nsAHttpConnection> conn = t->Connection();
          if (conn && !connections.Contains(conn)) {
            connections.AppendElement(conn);
          }
        }
      };

  // Get unthrottled transactions with the previous and current window id.
  transactions = mActiveTransactions[false].Get(previousId);
  addConnectionHelper(transactions);
  transactions = mActiveTransactions[false].Get(mCurrentBrowserId);
  addConnectionHelper(transactions);

  // Get throttled transactions with the previous and current window id.
  transactions = mActiveTransactions[true].Get(previousId);
  addConnectionHelper(transactions);
  transactions = mActiveTransactions[true].Get(mCurrentBrowserId);
  addConnectionHelper(transactions);

  for (const auto& conn : connections) {
    conn->CurrentBrowserIdChanged(mCurrentBrowserId);
  }
}

void nsHttpConnectionMgr::OnMsgUpdateCurrentBrowserId(int32_t aLoading,
                                                      ARefBase* param) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  uint64_t id = static_cast<UINT64Wrapper*>(param)->GetValue();

  if (mCurrentBrowserId == id) {
    // duplicate notification
    return;
  }

  bool activeTabWasLoading = mActiveTabTransactionsExist;

  uint64_t previousId = mCurrentBrowserId;
  mCurrentBrowserId = id;

  if (gHttpHandler->ActiveTabPriority()) {
    NotifyConnectionOfBrowserIdChange(previousId);
  }

  LOG(
      ("nsHttpConnectionMgr::OnMsgUpdateCurrentBrowserId"
       " id=%" PRIx64 "\n",
       mCurrentBrowserId));

  nsTArray<RefPtr<nsHttpTransaction>>* transactions = nullptr;

  // Update the "Exists" caches and resume any transactions that now deserve it,
  // changing the active tab changes the conditions for throttling.
  transactions = mActiveTransactions[false].Get(mCurrentBrowserId);
  mActiveTabUnthrottledTransactionsExist = !!transactions;

  if (!mActiveTabUnthrottledTransactionsExist) {
    transactions = mActiveTransactions[true].Get(mCurrentBrowserId);
  }
  mActiveTabTransactionsExist = !!transactions;

  if (transactions) {
    // This means there are some transactions for this newly activated tab,
    // resume them but anything else.
    LOG(("  resuming newly activated tab transactions"));
    ResumeReadOf(transactions);
    return;
  }

  if (!activeTabWasLoading) {
    // There were no transactions for the previously active tab, hence
    // all remaning transactions, if there were, were all unthrottled,
    // no need to wake them.
    return;
  }

  if (!mActiveTransactions[false].IsEmpty()) {
    LOG(("  resuming unthrottled background transactions"));
    ResumeReadOf(mActiveTransactions[false]);
    return;
  }

  if (!mActiveTransactions[true].IsEmpty()) {
    LOG(("  resuming throttled background transactions"));
    ResumeReadOf(mActiveTransactions[true]);
    return;
  }

  DestroyThrottleTicker();
}

void nsHttpConnectionMgr::TimeoutTick() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(mTimeoutTick, "no readtimeout tick");

  LOG(("nsHttpConnectionMgr::TimeoutTick active=%d\n", mNumActiveConns));
  // The next tick will be between 1 second and 1 hr
  // Set it to the max value here, and the TimeoutTick()s can
  // reduce it to their local needs.
  mTimeoutTickNext = 3600;  // 1hr

  for (const RefPtr<ConnectionEntry>& ent : mCT.Values()) {
    uint32_t timeoutTickNext = ent->TimeoutTick();
    mTimeoutTickNext = std::min(mTimeoutTickNext, timeoutTickNext);
  }

  if (mTimeoutTick) {
    mTimeoutTickNext = std::max(mTimeoutTickNext, 1U);
    mTimeoutTick->SetDelay(mTimeoutTickNext * 1000);
  }
}

// GetOrCreateConnectionEntry finds a ent for a particular CI for use in
// dispatching a transaction according to these rules
// 1] use an ent that matches the ci that can be dispatched immediately
// 2] otherwise use an ent of wildcard(ci) than can be dispatched immediately
// 3] otherwise create an ent that matches ci and make new conn on it

ConnectionEntry* nsHttpConnectionMgr::GetOrCreateConnectionEntry(
    nsHttpConnectionInfo* specificCI, bool prohibitWildCard, bool aNoHttp2,
    bool aNoHttp3, bool* aIsWildcard, bool* aAvailableForDispatchNow) {
  if (aAvailableForDispatchNow) {
    *aAvailableForDispatchNow = false;
  }
  *aIsWildcard = false;

  // step 1
  ConnectionEntry* specificEnt = mCT.GetWeak(specificCI->HashKey());
  if (specificEnt && specificEnt->AvailableForDispatchNow()) {
    if (aAvailableForDispatchNow) {
      *aAvailableForDispatchNow = true;
    }
    return specificEnt;
  }

  // step 1 repeated for an inverted anonymous flag; we return an entry
  // only when it has an h2 established connection that is not authenticated
  // with a client certificate.
  RefPtr<nsHttpConnectionInfo> anonInvertedCI(specificCI->Clone());
  anonInvertedCI->SetAnonymous(!specificCI->GetAnonymous());
  ConnectionEntry* invertedEnt = mCT.GetWeak(anonInvertedCI->HashKey());
  if (invertedEnt) {
    HttpConnectionBase* h2orh3conn =
        GetH2orH3ActiveConn(invertedEnt, aNoHttp2, aNoHttp3);
    if (h2orh3conn && h2orh3conn->IsExperienced() &&
        h2orh3conn->NoClientCertAuth()) {
      MOZ_ASSERT(h2orh3conn->UsingSpdy() || h2orh3conn->UsingHttp3());
      LOG(
          ("GetOrCreateConnectionEntry is coalescing h2/3 an/onymous "
           "connections, ent=%p",
           invertedEnt));
      return invertedEnt;
    }
  }

  if (!specificCI->UsingHttpsProxy()) {
    prohibitWildCard = true;
  }

  // step 2
  if (!prohibitWildCard && aNoHttp3) {
    RefPtr<nsHttpConnectionInfo> wildCardProxyCI;
    DebugOnly<nsresult> rv =
        specificCI->CreateWildCard(getter_AddRefs(wildCardProxyCI));
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    ConnectionEntry* wildCardEnt = mCT.GetWeak(wildCardProxyCI->HashKey());
    if (wildCardEnt && wildCardEnt->AvailableForDispatchNow()) {
      if (aAvailableForDispatchNow) {
        *aAvailableForDispatchNow = true;
      }
      *aIsWildcard = true;
      return wildCardEnt;
    }
  }

  // step 3
  if (!specificEnt) {
    RefPtr<nsHttpConnectionInfo> clone(specificCI->Clone());
    specificEnt = new ConnectionEntry(clone);
    mCT.InsertOrUpdate(clone->HashKey(), RefPtr{specificEnt});
  }
  return specificEnt;
}

void nsHttpConnectionMgr::DoSpeculativeConnection(
    SpeculativeTransaction* aTrans, bool aFetchHTTPSRR) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(aTrans);

  bool isWildcard = false;
  ConnectionEntry* ent = GetOrCreateConnectionEntry(
      aTrans->ConnectionInfo(), false, aTrans->Caps() & NS_HTTP_DISALLOW_SPDY,
      aTrans->Caps() & NS_HTTP_DISALLOW_HTTP3, &isWildcard);
  if (!aFetchHTTPSRR &&
      nsHttpHandler::EchConfigEnabled(aTrans->ConnectionInfo()->IsHttp3())) {
    // This happens when this is called from
    // SpeculativeTransaction::OnHTTPSRRAvailable. We have to update this
    // entry's echConfig so that the newly created connection can use the latest
    // echConfig.
    ent->MaybeUpdateEchConfig(aTrans->ConnectionInfo());
  }
  DoSpeculativeConnectionInternal(ent, aTrans, aFetchHTTPSRR);
}

void nsHttpConnectionMgr::DoSpeculativeConnectionInternal(
    ConnectionEntry* aEnt, SpeculativeTransaction* aTrans, bool aFetchHTTPSRR) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(aTrans);
  MOZ_ASSERT(aEnt);
  if (!gHttpHandler->Active()) {
    // Do nothing if we are shutting down.
    return;
  }

  ProxyDNSStrategy strategy = GetProxyDNSStrategyHelper(
      aEnt->mConnInfo->ProxyType(), aEnt->mConnInfo->ProxyFlag());
  // Speculative connections can be triggered by non-Necko consumers,
  // so add an extra check to ensure HTTPS RR isn't fetched when a proxy is
  // used.
  if (aFetchHTTPSRR && strategy == ProxyDNSStrategy::ORIGIN &&
      NS_SUCCEEDED(aTrans->FetchHTTPSRR())) {
    // nsHttpConnectionMgr::DoSpeculativeConnection will be called again
    // when HTTPS RR is available.
    return;
  }

  uint32_t parallelSpeculativeConnectLimit =
      aTrans->ParallelSpeculativeConnectLimit()
          ? *aTrans->ParallelSpeculativeConnectLimit()
          : gHttpHandler->ParallelSpeculativeConnectLimit();
  bool ignoreIdle = aTrans->IgnoreIdle() ? *aTrans->IgnoreIdle() : false;
  bool isFromPredictor =
      aTrans->IsFromPredictor() ? *aTrans->IsFromPredictor() : false;
  bool allow1918 = aTrans->Allow1918() ? *aTrans->Allow1918() : false;

  bool keepAlive = aTrans->Caps() & NS_HTTP_ALLOW_KEEPALIVE;
  if (mNumDnsAndConnectSockets < parallelSpeculativeConnectLimit &&
      ((ignoreIdle &&
        (aEnt->IdleConnectionsLength() < parallelSpeculativeConnectLimit)) ||
       !aEnt->IdleConnectionsLength()) &&
      !(keepAlive && aEnt->RestrictConnections()) &&
      !AtActiveConnectionLimit(aEnt, aTrans->Caps())) {
    nsresult rv = aEnt->CreateDnsAndConnectSocket(aTrans, aTrans->Caps(), true,
                                                  isFromPredictor, false,
                                                  allow1918, nullptr);
    if (NS_FAILED(rv)) {
      glean::networking::speculative_connect_outcome
          .Get("aborted_socket_fail"_ns)
          .Add(1);
      LOG(
          ("DoSpeculativeConnectionInternal Transport socket creation "
           "failure: %" PRIx32 "\n",
           static_cast<uint32_t>(rv)));
    } else {
      glean::networking::speculative_connect_outcome.Get("successful"_ns)
          .Add(1);
    }
  } else {
    glean::networking::speculative_connect_outcome
        .Get("aborted_socket_limit"_ns)
        .Add(1);
    LOG(
        ("DoSpeculativeConnectionInternal Transport ci=%s "
         "not created due to existing connection count:%d",
         aEnt->mConnInfo->HashKey().get(), parallelSpeculativeConnectLimit));
  }
}

void nsHttpConnectionMgr::DoFallbackConnection(SpeculativeTransaction* aTrans,
                                               bool aFetchHTTPSRR) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(aTrans);

  LOG(("nsHttpConnectionMgr::DoFallbackConnection"));

  bool availableForDispatchNow = false;
  bool aIsWildcard = false;
  ConnectionEntry* ent = GetOrCreateConnectionEntry(
      aTrans->ConnectionInfo(), false, aTrans->Caps() & NS_HTTP_DISALLOW_SPDY,
      aTrans->Caps() & NS_HTTP_DISALLOW_HTTP3, &aIsWildcard,
      &availableForDispatchNow);

  if (availableForDispatchNow) {
    LOG(
        ("nsHttpConnectionMgr::DoFallbackConnection fallback connection is "
         "ready for dispatching ent=%p",
         ent));
    aTrans->InvokeCallback();
    return;
  }

  DoSpeculativeConnectionInternal(ent, aTrans, aFetchHTTPSRR);
}

void nsHttpConnectionMgr::OnMsgSpeculativeConnect(int32_t, ARefBase* param) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  SpeculativeConnectArgs* args = static_cast<SpeculativeConnectArgs*>(param);

  LOG(
      ("nsHttpConnectionMgr::OnMsgSpeculativeConnect [ci=%s, "
       "mFetchHTTPSRR=%d]\n",
       args->mTrans->ConnectionInfo()->HashKey().get(), args->mFetchHTTPSRR));
  DoSpeculativeConnection(args->mTrans, args->mFetchHTTPSRR);
}

bool nsHttpConnectionMgr::BeConservativeIfProxied(nsIProxyInfo* proxy) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (mBeConservativeForProxy) {
    // The pref says to be conservative for proxies.
    return true;
  }

  if (!proxy) {
    // There is no proxy, so be conservative by default.
    return true;
  }

  // Be conservative only if there is no proxy host set either.
  // This logic was copied from nsSSLIOLayerAddToSocket.
  nsAutoCString proxyHost;
  proxy->GetHost(proxyHost);
  return proxyHost.IsEmpty();
}

// register a connection to receive CanJoinConnection() for particular
// origin keys
void nsHttpConnectionMgr::RegisterOriginCoalescingKey(HttpConnectionBase* conn,
                                                      const nsACString& host,
                                                      int32_t port) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  nsHttpConnectionInfo* ci = conn ? conn->ConnectionInfo() : nullptr;
  if (!ci || !conn->CanDirectlyActivate()) {
    return;
  }

  nsCString newKey;
  BuildOriginFrameHashKey(newKey, ci, host, port);
  mCoalescingHash.GetOrInsertNew(newKey, 1)->AppendElement(
      do_GetWeakReference(static_cast<nsISupportsWeakReference*>(conn)));

  LOG(
      ("nsHttpConnectionMgr::RegisterOriginCoalescingKey "
       "Established New Coalescing Key %s to %p %s\n",
       newKey.get(), conn, ci->HashKey().get()));
}

bool nsHttpConnectionMgr::GetConnectionData(nsTArray<HttpRetParams>* aArg) {
  for (const RefPtr<ConnectionEntry>& ent : mCT.Values()) {
    if (ent->mConnInfo->GetPrivate()) {
      continue;
    }
    aArg->AppendElement(ent->GetConnectionData());
  }

  return true;
}

bool nsHttpConnectionMgr::GetHttp3ConnectionStatsData(
    nsTArray<Http3ConnectionStatsParams>* aArg) {
  for (const RefPtr<ConnectionEntry>& ent : mCT.Values()) {
    if (ent->mConnInfo->GetPrivate()) {
      continue;
    }
    aArg->AppendElement(ent->GetHttp3ConnectionStatsData());
  }

  return true;
}

void nsHttpConnectionMgr::ResetIPFamilyPreference(nsHttpConnectionInfo* ci) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  ConnectionEntry* ent = mCT.GetWeak(ci->HashKey());
  if (ent) {
    ent->ResetIPFamilyPreference();
  }
}

void nsHttpConnectionMgr::ExcludeHttp2(const nsHttpConnectionInfo* ci) {
  LOG(("nsHttpConnectionMgr::ExcludeHttp2 excluding ci %s",
       ci->HashKey().BeginReading()));
  ConnectionEntry* ent = mCT.GetWeak(ci->HashKey());
  if (!ent) {
    LOG(("nsHttpConnectionMgr::ExcludeHttp2 no entry found?!"));
    return;
  }

  ent->DisallowHttp2();
}

void nsHttpConnectionMgr::ExcludeHttp3(const nsHttpConnectionInfo* ci) {
  LOG(("nsHttpConnectionMgr::ExcludeHttp3 exclude ci %s",
       ci->HashKey().BeginReading()));
  ConnectionEntry* ent = mCT.GetWeak(ci->HashKey());
  if (!ent) {
    LOG(("nsHttpConnectionMgr::ExcludeHttp3 no entry found?!"));
    return;
  }

  ent->DontReuseHttp3Conn();
}

void nsHttpConnectionMgr::MoveToWildCardConnEntry(
    nsHttpConnectionInfo* specificCI, nsHttpConnectionInfo* wildCardCI,
    HttpConnectionBase* proxyConn) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(specificCI->UsingHttpsProxy());

  LOG(
      ("nsHttpConnectionMgr::MakeConnEntryWildCard conn %p has requested to "
       "change CI from %s to %s\n",
       proxyConn, specificCI->HashKey().get(), wildCardCI->HashKey().get()));

  ConnectionEntry* ent = mCT.GetWeak(specificCI->HashKey());
  LOG(
      ("nsHttpConnectionMgr::MakeConnEntryWildCard conn %p using ent %p (spdy "
       "%d)\n",
       proxyConn, ent, ent ? ent->mUsingSpdy : 0));

  if (!ent || !ent->mUsingSpdy) {
    return;
  }

  bool isWildcard = false;
  ConnectionEntry* wcEnt =
      GetOrCreateConnectionEntry(wildCardCI, true, false, false, &isWildcard);
  if (wcEnt == ent) {
    // nothing to do!
    return;
  }
  wcEnt->mUsingSpdy = true;

  LOG(
      ("nsHttpConnectionMgr::MakeConnEntryWildCard ent %p "
       "idle=%zu active=%zu half=%zu pending=%zu\n",
       ent, ent->IdleConnectionsLength(), ent->ActiveConnsLength(),
       ent->DnsAndConnectSocketsLength(), ent->PendingQueueLength()));

  LOG(
      ("nsHttpConnectionMgr::MakeConnEntryWildCard wc-ent %p "
       "idle=%zu active=%zu half=%zu pending=%zu\n",
       wcEnt, wcEnt->IdleConnectionsLength(), wcEnt->ActiveConnsLength(),
       wcEnt->DnsAndConnectSocketsLength(), wcEnt->PendingQueueLength()));

  ent->MoveConnection(proxyConn, wcEnt);
}

bool nsHttpConnectionMgr::RemoveTransFromConnEntry(nsHttpTransaction* aTrans,
                                                   const nsACString& aHashKey) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  LOG(("nsHttpConnectionMgr::RemoveTransFromConnEntry: trans=%p ci=%s", aTrans,
       PromiseFlatCString(aHashKey).get()));

  if (aHashKey.IsEmpty()) {
    return false;
  }

  // Step 1: Get the transaction's connection entry.
  ConnectionEntry* entry = mCT.GetWeak(aHashKey);
  if (!entry) {
    return false;
  }

  // Step 2: Try to find the undispatched transaction.
  return entry->RemoveTransFromPendingQ(aTrans);
}

void nsHttpConnectionMgr::IncreaseNumDnsAndConnectSockets() {
  mNumDnsAndConnectSockets++;
}

void nsHttpConnectionMgr::DecreaseNumDnsAndConnectSockets() {
  MOZ_ASSERT(mNumDnsAndConnectSockets);
  if (mNumDnsAndConnectSockets) {  // just in case
    mNumDnsAndConnectSockets--;
  }
}

already_AddRefed<PendingTransactionInfo>
nsHttpConnectionMgr::FindTransactionHelper(bool removeWhenFound,
                                           ConnectionEntry* aEnt,
                                           nsAHttpTransaction* aTrans) {
  nsTArray<RefPtr<PendingTransactionInfo>>* pendingQ =
      aEnt->GetTransactionPendingQHelper(aTrans);

  int32_t index =
      pendingQ ? pendingQ->IndexOf(aTrans, 0, PendingComparator()) : -1;

  RefPtr<PendingTransactionInfo> info;
  if (index != -1) {
    info = (*pendingQ)[index];
    if (removeWhenFound) {
      pendingQ->RemoveElementAt(index);
    }
  }
  return info.forget();
}

already_AddRefed<ConnectionEntry> nsHttpConnectionMgr::FindConnectionEntry(
    const nsHttpConnectionInfo* ci) {
  return mCT.Get(ci->HashKey());
}

nsHttpConnectionMgr* nsHttpConnectionMgr::AsHttpConnectionMgr() { return this; }

HttpConnectionMgrParent* nsHttpConnectionMgr::AsHttpConnectionMgrParent() {
  return nullptr;
}

void nsHttpConnectionMgr::NewIdleConnectionAdded(uint32_t timeToLive) {
  mNumIdleConns++;

  // If the added connection was first idle connection or has shortest
  // time to live among the watched connections, pruning dead
  // connections needs to be done when it can't be reused anymore.
  if (!mTimer || NowInSeconds() + timeToLive < mTimeOfNextWakeUp) {
    PruneDeadConnectionsAfter(timeToLive);
  }
}

void nsHttpConnectionMgr::DecrementNumIdleConns() {
  MOZ_ASSERT(mNumIdleConns);
  mNumIdleConns--;
  ConditionallyStopPruneDeadConnectionsTimer();
}

// A structure used to marshall objects necessary for ServerCertificateHashaes
class nsStoreServerCertHashesData : public ARefBase {
 public:
  nsStoreServerCertHashesData(
      nsHttpConnectionInfo* aConnInfo, bool aNoSpdy, bool aNoHttp3,
      nsTArray<RefPtr<nsIWebTransportHash>>&& aServerCertHashes)
      : mConnInfo(aConnInfo),
        mNoSpdy(aNoSpdy),
        mNoHttp3(aNoHttp3),
        mServerCertHashes(std::move(aServerCertHashes)) {}

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(nsStoreServerCertHashesData, override)

  RefPtr<nsHttpConnectionInfo> mConnInfo;
  bool mNoSpdy;
  bool mNoHttp3;
  nsTArray<RefPtr<nsIWebTransportHash>> mServerCertHashes;

 private:
  virtual ~nsStoreServerCertHashesData() = default;
};

// The connection manager needs to know the hashes used for a WebTransport
// connection authenticated with serverCertHashes
nsresult nsHttpConnectionMgr::StoreServerCertHashes(
    nsHttpConnectionInfo* aConnInfo, bool aNoSpdy, bool aNoHttp3,
    nsTArray<RefPtr<nsIWebTransportHash>>&& aServerCertHashes) {
  RefPtr<nsHttpConnectionInfo> ci = aConnInfo->Clone();
  RefPtr<nsStoreServerCertHashesData> data = new nsStoreServerCertHashesData(
      ci, aNoSpdy, aNoHttp3, std::move(aServerCertHashes));
  return PostEvent(&nsHttpConnectionMgr::OnMsgStoreServerCertHashes, 0, data);
}

void nsHttpConnectionMgr::OnMsgStoreServerCertHashes(int32_t, ARefBase* param) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  nsStoreServerCertHashesData* data =
      static_cast<nsStoreServerCertHashesData*>(param);

  bool isWildcard;
  ConnectionEntry* connEnt = GetOrCreateConnectionEntry(
      data->mConnInfo, true, data->mNoSpdy, data->mNoHttp3, &isWildcard);
  MOZ_ASSERT(!isWildcard, "No webtransport with wildcard");
  connEnt->SetServerCertHashes(std::move(data->mServerCertHashes));
}

const nsTArray<RefPtr<nsIWebTransportHash>>*
nsHttpConnectionMgr::GetServerCertHashes(nsHttpConnectionInfo* aConnInfo) {
  ConnectionEntry* connEnt = mCT.GetWeak(aConnInfo->HashKey());
  if (!connEnt) {
    MOZ_ASSERT(0);
    return nullptr;
  }
  return &connEnt->GetServerCertHashes();
}

void nsHttpConnectionMgr::CheckTransInPendingQueue(nsHttpTransaction* aTrans) {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  // We only do this check on socket thread. When this function is called on
  // main thread, the transaction is newly created, so we can skip this check.
  if (!OnSocketThread()) {
    return;
  }

  nsAutoCString hashKey;
  aTrans->GetHashKeyOfConnectionEntry(hashKey);
  if (hashKey.IsEmpty()) {
    return;
  }

  bool foundInPendingQ = RemoveTransFromConnEntry(aTrans, hashKey);
  MOZ_DIAGNOSTIC_ASSERT(!foundInPendingQ);
#endif
}

bool nsHttpConnectionMgr::AllowToRetryDifferentIPFamilyForHttp3(
    nsHttpConnectionInfo* ci, nsresult aError) {
  ConnectionEntry* ent = mCT.GetWeak(ci->HashKey());
  if (!ent) {
    return false;
  }

  return ent->AllowToRetryDifferentIPFamilyForHttp3(aError);
}

void nsHttpConnectionMgr::SetRetryDifferentIPFamilyForHttp3(
    nsHttpConnectionInfo* ci, uint16_t aIPFamily) {
  ConnectionEntry* ent = mCT.GetWeak(ci->HashKey());
  if (!ent) {
    return;
  }

  ent->SetRetryDifferentIPFamilyForHttp3(aIPFamily);
}

}  // namespace mozilla::net
