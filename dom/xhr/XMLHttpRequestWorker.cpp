/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "XMLHttpRequestWorker.h"

#include <memory>

#include "GeckoProfiler.h"
#include "XMLHttpRequestMainThread.h"
#include "XMLHttpRequestUpload.h"
#include "js/ArrayBuffer.h"  // JS::Is{,Detached}ArrayBufferObject
#include "js/GCPolicyAPI.h"
#include "js/JSON.h"
#include "js/RootingAPI.h"  // JS::{Handle,Heap,PersistentRooted}
#include "js/TracingAPI.h"
#include "js/Value.h"  // JS::{Undefined,}Value
#include "jsfriendapi.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/Exceptions.h"
#include "mozilla/dom/File.h"
#include "mozilla/dom/FormData.h"
#include "mozilla/dom/ProgressEvent.h"
#include "mozilla/dom/SerializedStackHolder.h"
#include "mozilla/dom/StreamBlobImpl.h"
#include "mozilla/dom/StructuredCloneHolder.h"
#include "mozilla/dom/URLSearchParams.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/dom/XMLHttpRequestBinding.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsIDOMEventListener.h"
#include "nsJSUtils.h"
#include "nsThreadUtils.h"

extern mozilla::LazyLogModule gXMLHttpRequestLog;

namespace mozilla::dom {

using EventType = XMLHttpRequest::EventType;
using Events = XMLHttpRequest::Events;

/**
 *  XMLHttpRequest in workers
 *
 *  XHR in workers is implemented by proxying calls/events/etc between the
 *  worker thread and an XMLHttpRequest on the main thread.  The glue
 *  object here is the Proxy, which lives on both threads.  All other objects
 *  live on either the main thread (the XMLHttpRequest) or the worker thread
 *  (the worker and XHR private objects).
 *
 *  The main thread XHR is always operated in async mode, even for sync XHR
 *  in workers.  Calls made on the worker thread are proxied to the main thread
 *  synchronously (meaning the worker thread is blocked until the call
 *  returns).  Each proxied call spins up a sync queue, which captures any
 *  synchronously dispatched events and ensures that they run synchronously
 *  on the worker as well.  Asynchronously dispatched events are posted to the
 *  worker thread to run asynchronously.  Some of the XHR state is mirrored on
 *  the worker thread to avoid needing a cross-thread call on every property
 *  access.
 *
 *  The XHR private is stored in the private slot of the XHR JSObject on the
 *  worker thread.  It is destroyed when that JSObject is GCd.  The private
 *  roots its JSObject while network activity is in progress.  It also
 *  adds itself as a feature to the worker to give itself a chance to clean up
 *  if the worker goes away during an XHR call.  It is important that the
 *  rooting and feature registration (collectively called pinning) happens at
 *  the proper times.  If we pin for too long we can cause memory leaks or even
 *  shutdown hangs.  If we don't pin for long enough we introduce a GC hazard.
 *
 *  The XHR is pinned from the time Send is called to roughly the time loadend
 *  is received.  There are some complications involved with Abort and XHR
 *  reuse.  We maintain a counter on the main thread of how many times Send was
 *  called on this XHR, and we decrement the counter every time we receive a
 *  loadend event.  When the counter reaches zero we dispatch a runnable to the
 *  worker thread to unpin the XHR.  We only decrement the counter if the
 *  dispatch was successful, because the worker may no longer be accepting
 *  regular runnables.  In the event that we reach Proxy::Teardown and there
 *  the outstanding Send count is still non-zero, we dispatch a control
 *  runnable which is guaranteed to run.
 *
 *  NB: Some of this could probably be simplified now that we have the
 *  inner/outer channel ids.
 */

class Proxy final : public nsIDOMEventListener {
 public:
  // Read on multiple threads.
  RefPtr<ThreadSafeWorkerRef> mWorkerRef;
  const ClientInfo mClientInfo;
  const Maybe<ServiceWorkerDescriptor> mController;

  // Only ever dereferenced and/or checked on the worker thread. Cleared
  // explicitly on the worker thread inside XMLHttpRequestWorker::ReleaseProxy.
  WeakPtr<XMLHttpRequestWorker> mXMLHttpRequestPrivate;

  // XHR Params:
  bool mMozAnon;
  bool mMozSystem;

  // Only touched on the main thread.
  RefPtr<XMLHttpRequestMainThread> mXHR;
  RefPtr<XMLHttpRequestUpload> mXHRUpload;
  nsCOMPtr<nsIEventTarget> mSyncLoopTarget;
  nsCOMPtr<nsIEventTarget> mSyncEventResponseTarget;
  uint32_t mInnerEventStreamId;
  uint32_t mInnerChannelId;
  uint32_t mOutstandingSendCount;

  // Only touched on the worker thread.
  uint32_t mOuterChannelId;
  uint32_t mOpenCount;
  uint64_t mLastLoaded;
  uint64_t mLastTotal;
  uint64_t mLastUploadLoaded;
  uint64_t mLastUploadTotal;
  nsresult mLastErrorDetailAtLoadend;
  bool mIsSyncXHR;
  bool mLastLengthComputable;
  bool mLastUploadLengthComputable;
  bool mSeenUploadLoadStart;
  bool mSeenUploadLoadEnd;

  // Only touched on the main thread.
  bool mUploadEventListenersAttached;
  bool mMainThreadSeenLoadStart;
  bool mInOpen;

 public:
  Proxy(XMLHttpRequestWorker* aXHRPrivate, const ClientInfo& aClientInfo,
        const Maybe<ServiceWorkerDescriptor>& aController, bool aMozAnon,
        bool aMozSystem)
      : mClientInfo(aClientInfo),
        mController(aController),
        mXMLHttpRequestPrivate(aXHRPrivate),
        mMozAnon(aMozAnon),
        mMozSystem(aMozSystem),
        mInnerEventStreamId(aXHRPrivate->EventStreamId()),
        mInnerChannelId(0),
        mOutstandingSendCount(0),
        mOuterChannelId(0),
        mOpenCount(0),
        mLastLoaded(0),
        mLastTotal(0),
        mLastUploadLoaded(0),
        mLastUploadTotal(0),
        mLastErrorDetailAtLoadend(NS_OK),
        mIsSyncXHR(false),
        mLastLengthComputable(false),
        mLastUploadLengthComputable(false),
        mSeenUploadLoadStart(false),
        mSeenUploadLoadEnd(false),
        mUploadEventListenersAttached(false),
        mMainThreadSeenLoadStart(false),
        mInOpen(false) {}

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIDOMEVENTLISTENER

  // This method is called in OpenRunnable::MainThreadRunInternal(). The
  // OpenRunnable has to provide a valid WorkerPrivate for the Proxy's
  // initialization since OpenRunnable is a WorkerMainThreadRunnable, which
  // holds a ThreadSafeWorkerRef and blocks Worker's shutdown until the
  // execution returns back to the worker thread.
  bool Init(WorkerPrivate* aWorkerPrivate);

  void Teardown();

  bool AddRemoveEventListeners(bool aUpload, bool aAdd);

  void Reset() {
    AssertIsOnMainThread();

    if (mUploadEventListenersAttached) {
      AddRemoveEventListeners(true, false);
    }
  }

  already_AddRefed<nsIEventTarget> GetEventTarget() {
    AssertIsOnMainThread();

    nsCOMPtr<nsIEventTarget> target =
        mSyncEventResponseTarget ? mSyncEventResponseTarget : mSyncLoopTarget;
    return target.forget();
  }

  WorkerPrivate* Private() const {
    if (mWorkerRef) {
      return mWorkerRef->Private();
    }
    return nullptr;
  }

#ifdef DEBUG
  void DebugStoreWorkerRef(RefPtr<ThreadSafeWorkerRef>& aWorkerRef) {
    MOZ_ASSERT(!NS_IsMainThread());
    MutexAutoLock lock(mXHR->mTSWorkerRefMutex);
    mXHR->mTSWorkerRef = aWorkerRef;
  }

  void DebugForgetWorkerRef() {
    MOZ_ASSERT(!NS_IsMainThread());
    MutexAutoLock lock(mXHR->mTSWorkerRefMutex);
    mXHR->mTSWorkerRef = nullptr;
  }
#endif

 private:
  ~Proxy() {
    MOZ_ASSERT(!mXHR);
    MOZ_ASSERT(!mXHRUpload);
    MOZ_ASSERT(!mOutstandingSendCount);
  }
};

class WorkerThreadProxySyncRunnable : public WorkerMainThreadRunnable {
 protected:
  RefPtr<Proxy> mProxy;

 private:
  // mErrorCode is set on the main thread by MainThreadRun and it's used at the
  // end of the Dispatch() to return the error code.
  nsresult mErrorCode;

 public:
  WorkerThreadProxySyncRunnable(WorkerPrivate* aWorkerPrivate, Proxy* aProxy)
      : WorkerMainThreadRunnable(aWorkerPrivate, "XHR"_ns),
        mProxy(aProxy),
        mErrorCode(NS_OK) {
    MOZ_ASSERT(aWorkerPrivate);
    MOZ_ASSERT(aProxy);
    aWorkerPrivate->AssertIsOnWorkerThread();
  }

  void Dispatch(WorkerPrivate* aWorkerPrivate, WorkerStatus aFailStatus,
                ErrorResult& aRv) {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();

    WorkerMainThreadRunnable::Dispatch(aWorkerPrivate, aFailStatus, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return;
    }

    if (NS_FAILED(mErrorCode)) {
      aRv.Throw(mErrorCode);
    }
  }

 protected:
  virtual ~WorkerThreadProxySyncRunnable() = default;

  virtual void RunOnMainThread(ErrorResult& aRv) = 0;

 private:
  virtual bool MainThreadRun() override;
};

class SendRunnable final : public WorkerThreadProxySyncRunnable {
  RefPtr<BlobImpl> mBlobImpl;
  // WorkerMainThreadRunnable has a member mSyncLoopTarget to perform the
  // synchronous dispatch. The mSyncLoopTarget will be released after
  // WorkerMainThreadRunnable::Dispatch().
  // However, to perform sync XHR, an additional sync loop is needed to wait
  // for the sync XHR response. This is because XMLHttpRequestMainThread
  // performs xhr in async way, and it causes the response to not be
  // available before SendRunnable returns back to the worker thread.
  // This is the event target to the additional sync loop.
  nsCOMPtr<nsIEventTarget> mSyncXHRSyncLoopTarget;
  bool mHasUploadListeners;

 public:
  SendRunnable(WorkerPrivate* aWorkerPrivate, Proxy* aProxy,
               BlobImpl* aBlobImpl)
      : WorkerThreadProxySyncRunnable(aWorkerPrivate, aProxy),
        mBlobImpl(aBlobImpl),
        mHasUploadListeners(false) {}

  void SetHaveUploadListeners(bool aHasUploadListeners) {
    mHasUploadListeners = aHasUploadListeners;
  }

  void SetSyncXHRSyncLoopTarget(nsIEventTarget* aSyncXHRSyncLoopTarget) {
    mSyncXHRSyncLoopTarget = aSyncXHRSyncLoopTarget;
  }

 private:
  ~SendRunnable() = default;

  virtual void RunOnMainThread(ErrorResult& aRv) override;
};

namespace {

class MainThreadProxyRunnable : public MainThreadWorkerSyncRunnable {
 protected:
  RefPtr<Proxy> mProxy;

  MainThreadProxyRunnable(WorkerPrivate* aWorkerPrivate, Proxy* aProxy,
                          const char* aName = "MainThreadProxyRunnable")
      : MainThreadWorkerSyncRunnable(aProxy->GetEventTarget(), aName),
        mProxy(aProxy) {
    MOZ_ASSERT(aProxy);
  }

  virtual ~MainThreadProxyRunnable() = default;
};

class AsyncTeardownRunnable final : public Runnable {
  RefPtr<Proxy> mProxy;

 public:
  explicit AsyncTeardownRunnable(Proxy* aProxy)
      : Runnable("dom::AsyncTeardownRunnable"), mProxy(aProxy) {
    MOZ_ASSERT(aProxy);
  }

 private:
  ~AsyncTeardownRunnable() = default;

  NS_IMETHOD
  Run() override {
    AssertIsOnMainThread();

    mProxy->Teardown();
    mProxy = nullptr;

    return NS_OK;
  }
};

class LoadStartDetectionRunnable final : public Runnable,
                                         public nsIDOMEventListener {
  RefPtr<Proxy> mProxy;
  RefPtr<XMLHttpRequest> mXHR;
  uint32_t mChannelId;
  bool mReceivedLoadStart;

  class ProxyCompleteRunnable final : public MainThreadProxyRunnable {
    uint32_t mChannelId;

   public:
    ProxyCompleteRunnable(WorkerPrivate* aWorkerPrivate, Proxy* aProxy,
                          uint32_t aChannelId)
        : MainThreadProxyRunnable(aWorkerPrivate, aProxy,
                                  "ProxyCompleteRunnable"),
          mChannelId(aChannelId) {}

   private:
    ~ProxyCompleteRunnable() = default;

    virtual bool WorkerRun(JSContext* aCx,
                           WorkerPrivate* aWorkerPrivate) override {
      if (mChannelId != mProxy->mOuterChannelId) {
        // Threads raced, this event is now obsolete.
        return true;
      }

      if (mSyncLoopTarget) {
        aWorkerPrivate->StopSyncLoop(mSyncLoopTarget, NS_OK);
      }

      XMLHttpRequestWorker* xhrw = mProxy->mXMLHttpRequestPrivate.get();
      if (xhrw && xhrw->SendInProgress()) {
        xhrw->Unpin();
      }

      return true;
    }

    nsresult Cancel() override { return Run(); }
  };

 public:
  explicit LoadStartDetectionRunnable(Proxy* aProxy)
      : Runnable("dom::LoadStartDetectionRunnable"),
        mProxy(aProxy),
        mXHR(aProxy->mXHR),
        mChannelId(mProxy->mInnerChannelId),
        mReceivedLoadStart(false) {
    AssertIsOnMainThread();
  }

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIRUNNABLE
  NS_DECL_NSIDOMEVENTLISTENER

  bool RegisterAndDispatch() {
    AssertIsOnMainThread();

    if (NS_FAILED(
            mXHR->AddEventListener(Events::loadstart, this, false, false))) {
      NS_WARNING("Failed to add event listener!");
      return false;
    }

    MOZ_ASSERT_DEBUG_OR_FUZZING(mProxy && mProxy->Private());

    return NS_SUCCEEDED(mProxy->Private()->DispatchToMainThread(this));
  }

 private:
  ~LoadStartDetectionRunnable() { AssertIsOnMainThread(); }
};

class EventRunnable final : public MainThreadProxyRunnable {
  const EventType& mType;
  UniquePtr<XMLHttpRequestWorker::ResponseData> mResponseData;
  nsCString mResponseURL;
  nsCString mStatusText;
  uint64_t mLoaded;
  uint64_t mTotal;
  uint32_t mEventStreamId;
  uint32_t mStatus;
  uint16_t mReadyState;
  bool mUploadEvent;
  bool mProgressEvent;
  bool mLengthComputable;
  nsresult mStatusResult;
  nsresult mErrorDetail;
  // mScopeObj is used in PreDispatch only.  We init it in our constructor, and
  // reset() in PreDispatch, to ensure that it's not still linked into the
  // runtime once we go off-thread.
  JS::PersistentRooted<JSObject*> mScopeObj;

 public:
  EventRunnable(Proxy* aProxy, bool aUploadEvent, const EventType& aType,
                bool aLengthComputable, uint64_t aLoaded, uint64_t aTotal,
                JS::Handle<JSObject*> aScopeObj)
      : MainThreadProxyRunnable(aProxy->Private(), aProxy, "EventRunnable"),
        mType(aType),
        mResponseData(new XMLHttpRequestWorker::ResponseData()),
        mLoaded(aLoaded),
        mTotal(aTotal),
        mEventStreamId(aProxy->mInnerEventStreamId),
        mStatus(0),
        mReadyState(0),
        mUploadEvent(aUploadEvent),
        mProgressEvent(true),
        mLengthComputable(aLengthComputable),
        mStatusResult(NS_OK),
        mErrorDetail(NS_OK),
        mScopeObj(RootingCx(), aScopeObj) {}

  EventRunnable(Proxy* aProxy, bool aUploadEvent, const EventType& aType,
                JS::Handle<JSObject*> aScopeObj)
      : MainThreadProxyRunnable(aProxy->Private(), aProxy, "EventRunnable"),
        mType(aType),
        mResponseData(new XMLHttpRequestWorker::ResponseData()),
        mLoaded(0),
        mTotal(0),
        mEventStreamId(aProxy->mInnerEventStreamId),
        mStatus(0),
        mReadyState(0),
        mUploadEvent(aUploadEvent),
        mProgressEvent(false),
        mLengthComputable(false),
        mStatusResult(NS_OK),
        mErrorDetail(NS_OK),
        mScopeObj(RootingCx(), aScopeObj) {}

 private:
  ~EventRunnable() = default;

  bool PreDispatch(WorkerPrivate* /* unused */) final;
  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override;
};

class SyncTeardownRunnable final : public WorkerThreadProxySyncRunnable {
 public:
  SyncTeardownRunnable(WorkerPrivate* aWorkerPrivate, Proxy* aProxy)
      : WorkerThreadProxySyncRunnable(aWorkerPrivate, aProxy) {}

 private:
  ~SyncTeardownRunnable() = default;

  virtual void RunOnMainThread(ErrorResult& aRv) override {
    mProxy->Teardown();
    MOZ_ASSERT(!mProxy->mSyncLoopTarget);
  }
};

class SetBackgroundRequestRunnable final
    : public WorkerThreadProxySyncRunnable {
  bool mValue;

 public:
  SetBackgroundRequestRunnable(WorkerPrivate* aWorkerPrivate, Proxy* aProxy,
                               bool aValue)
      : WorkerThreadProxySyncRunnable(aWorkerPrivate, aProxy), mValue(aValue) {}

 private:
  ~SetBackgroundRequestRunnable() = default;

  virtual void RunOnMainThread(ErrorResult& aRv) override {
    // XXXedgar, do we intend to ignore the errors?
    mProxy->mXHR->SetMozBackgroundRequest(mValue, aRv);
  }
};

class SetWithCredentialsRunnable final : public WorkerThreadProxySyncRunnable {
  bool mValue;

 public:
  SetWithCredentialsRunnable(WorkerPrivate* aWorkerPrivate, Proxy* aProxy,
                             bool aValue)
      : WorkerThreadProxySyncRunnable(aWorkerPrivate, aProxy), mValue(aValue) {}

 private:
  ~SetWithCredentialsRunnable() = default;

  virtual void RunOnMainThread(ErrorResult& aRv) override {
    mProxy->mXHR->SetWithCredentials(mValue, aRv);
  }
};

class SetResponseTypeRunnable final : public WorkerThreadProxySyncRunnable {
  XMLHttpRequestResponseType mResponseType;

 public:
  SetResponseTypeRunnable(WorkerPrivate* aWorkerPrivate, Proxy* aProxy,
                          XMLHttpRequestResponseType aResponseType)
      : WorkerThreadProxySyncRunnable(aWorkerPrivate, aProxy),
        mResponseType(aResponseType) {}

  XMLHttpRequestResponseType ResponseType() { return mResponseType; }

 private:
  ~SetResponseTypeRunnable() = default;

  virtual void RunOnMainThread(ErrorResult& aRv) override {
    mProxy->mXHR->SetResponseTypeRaw(mResponseType);
    mResponseType = mProxy->mXHR->ResponseType();
  }
};

class SetTimeoutRunnable final : public WorkerThreadProxySyncRunnable {
  uint32_t mTimeout;

 public:
  SetTimeoutRunnable(WorkerPrivate* aWorkerPrivate, Proxy* aProxy,
                     uint32_t aTimeout)
      : WorkerThreadProxySyncRunnable(aWorkerPrivate, aProxy),
        mTimeout(aTimeout) {}

 private:
  ~SetTimeoutRunnable() = default;

  virtual void RunOnMainThread(ErrorResult& aRv) override {
    mProxy->mXHR->SetTimeout(mTimeout, aRv);
  }
};

class AbortRunnable final : public WorkerThreadProxySyncRunnable {
 public:
  AbortRunnable(WorkerPrivate* aWorkerPrivate, Proxy* aProxy)
      : WorkerThreadProxySyncRunnable(aWorkerPrivate, aProxy) {}

 private:
  ~AbortRunnable() = default;

  virtual void RunOnMainThread(ErrorResult& aRv) override;
};

class GetAllResponseHeadersRunnable final
    : public WorkerThreadProxySyncRunnable {
  nsCString& mResponseHeaders;

 public:
  GetAllResponseHeadersRunnable(WorkerPrivate* aWorkerPrivate, Proxy* aProxy,
                                nsCString& aResponseHeaders)
      : WorkerThreadProxySyncRunnable(aWorkerPrivate, aProxy),
        mResponseHeaders(aResponseHeaders) {}

 private:
  ~GetAllResponseHeadersRunnable() = default;

  virtual void RunOnMainThread(ErrorResult& aRv) override {
    mProxy->mXHR->GetAllResponseHeaders(mResponseHeaders, aRv);
  }
};

class GetResponseHeaderRunnable final : public WorkerThreadProxySyncRunnable {
  const nsCString mHeader;
  nsCString& mValue;

 public:
  GetResponseHeaderRunnable(WorkerPrivate* aWorkerPrivate, Proxy* aProxy,
                            const nsACString& aHeader, nsCString& aValue)
      : WorkerThreadProxySyncRunnable(aWorkerPrivate, aProxy),
        mHeader(aHeader),
        mValue(aValue) {}

 private:
  ~GetResponseHeaderRunnable() = default;

  virtual void RunOnMainThread(ErrorResult& aRv) override {
    mProxy->mXHR->GetResponseHeader(mHeader, mValue, aRv);
  }
};

class OpenRunnable final : public WorkerThreadProxySyncRunnable {
  nsCString mMethod;
  nsCString mURL;
  Optional<nsACString> mUser;
  nsCString mUserStr;
  Optional<nsACString> mPassword;
  nsCString mPasswordStr;
  bool mBackgroundRequest;
  bool mWithCredentials;
  uint32_t mTimeout;
  XMLHttpRequestResponseType mResponseType;
  const nsString mMimeTypeOverride;

  // Remember the worker thread's stack when the XHR was opened, so that it can
  // be passed on to the net monitor.
  UniquePtr<SerializedStackHolder> mOriginStack;

  // Remember the worker thread's stack when the XHR was opened for profiling
  // purposes.
  UniquePtr<ProfileChunkedBuffer> mSource;

 public:
  OpenRunnable(WorkerPrivate* aWorkerPrivate, Proxy* aProxy,
               const nsACString& aMethod, const nsACString& aURL,
               const Optional<nsACString>& aUser,
               const Optional<nsACString>& aPassword, bool aBackgroundRequest,
               bool aWithCredentials, uint32_t aTimeout,
               XMLHttpRequestResponseType aResponseType,
               const nsString& aMimeTypeOverride,
               UniquePtr<SerializedStackHolder> aOriginStack,
               UniquePtr<ProfileChunkedBuffer> aSource = nullptr)
      : WorkerThreadProxySyncRunnable(aWorkerPrivate, aProxy),
        mMethod(aMethod),
        mURL(aURL),
        mBackgroundRequest(aBackgroundRequest),
        mWithCredentials(aWithCredentials),
        mTimeout(aTimeout),
        mResponseType(aResponseType),
        mMimeTypeOverride(aMimeTypeOverride),
        mOriginStack(std::move(aOriginStack)),
        mSource(std::move(aSource)) {
    if (aUser.WasPassed()) {
      mUserStr = aUser.Value();
      mUser = &mUserStr;
    }
    if (aPassword.WasPassed()) {
      mPasswordStr = aPassword.Value();
      mPassword = &mPasswordStr;
    }
  }

 private:
  ~OpenRunnable() = default;

  virtual void RunOnMainThread(ErrorResult& aRv) override {
    MOZ_ASSERT_IF(mProxy->mWorkerRef,
                  mProxy->mWorkerRef->Private() == mWorkerRef->Private());

    // mProxy wants a valid ThreadSafeWorkerRef for the duration of our call,
    // but mProxy->mWorkerRef may be null if a send is not currently active,
    // so save the existing value for the duration of the call.
    RefPtr<ThreadSafeWorkerRef> oldWorker = std::move(mProxy->mWorkerRef);

    // WorkerMainThreadRunnable::mWorkerRef must not be nullptr here, since
    // when get here, it means this WorkerMainThreadRunnable had already be
    // dispatched successfully and the execution is on the main thread.
    MOZ_ASSERT_DEBUG_OR_FUZZING(mWorkerRef);

    // Set mProxy->mWorkerRef as OpenRunnable::mWorkerRef which is from
    // WorkerMainThreadRunnable during the runnable execution.
    // Let OpenRunnable keep a reference for dispatching
    // MainThreadStopSyncRunnable back to the Worker thread after the main
    // thread execution completes.
    mProxy->mWorkerRef = mWorkerRef;

    MainThreadRunInternal(aRv);

    // Restore the previous activated WorkerRef for send.
    mProxy->mWorkerRef = std::move(oldWorker);
  }

  void MainThreadRunInternal(ErrorResult& aRv);
};

class SetRequestHeaderRunnable final : public WorkerThreadProxySyncRunnable {
  nsCString mHeader;
  nsCString mValue;

 public:
  SetRequestHeaderRunnable(WorkerPrivate* aWorkerPrivate, Proxy* aProxy,
                           const nsACString& aHeader, const nsACString& aValue)
      : WorkerThreadProxySyncRunnable(aWorkerPrivate, aProxy),
        mHeader(aHeader),
        mValue(aValue) {}

 private:
  ~SetRequestHeaderRunnable() = default;

  virtual void RunOnMainThread(ErrorResult& aRv) override {
    mProxy->mXHR->SetRequestHeader(mHeader, mValue, aRv);
  }
};

class OverrideMimeTypeRunnable final : public WorkerThreadProxySyncRunnable {
  nsString mMimeType;

 public:
  OverrideMimeTypeRunnable(WorkerPrivate* aWorkerPrivate, Proxy* aProxy,
                           const nsAString& aMimeType)
      : WorkerThreadProxySyncRunnable(aWorkerPrivate, aProxy),
        mMimeType(aMimeType) {}

 private:
  ~OverrideMimeTypeRunnable() = default;

  virtual void RunOnMainThread(ErrorResult& aRv) override {
    mProxy->mXHR->OverrideMimeType(mMimeType, aRv);
  }
};

class AutoUnpinXHR {
  XMLHttpRequestWorker* mXMLHttpRequestPrivate;

 public:
  explicit AutoUnpinXHR(XMLHttpRequestWorker* aXMLHttpRequestPrivate)
      : mXMLHttpRequestPrivate(aXMLHttpRequestPrivate) {
    MOZ_ASSERT(aXMLHttpRequestPrivate);
  }

  ~AutoUnpinXHR() {
    if (mXMLHttpRequestPrivate) {
      mXMLHttpRequestPrivate->Unpin();
    }
  }

  void Clear() { mXMLHttpRequestPrivate = nullptr; }
};

}  // namespace

bool Proxy::Init(WorkerPrivate* aWorkerPrivate) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aWorkerPrivate);

  if (mXHR) {
    return true;
  }

  nsPIDOMWindowInner* ownerWindow = aWorkerPrivate->GetWindow();
  if (ownerWindow && !ownerWindow->IsCurrentInnerWindow()) {
    NS_WARNING("Window has navigated, cannot create XHR here.");
    return false;
  }

  mXHR = new XMLHttpRequestMainThread(ownerWindow ? ownerWindow->AsGlobal()
                                                  : nullptr);
  mXHR->Construct(aWorkerPrivate->GetPrincipal(),
                  aWorkerPrivate->CookieJarSettings(), true,
                  aWorkerPrivate->GetBaseURI(), aWorkerPrivate->GetLoadGroup(),
                  aWorkerPrivate->GetPerformanceStorage(),
                  aWorkerPrivate->CSPEventListener());

  mXHR->SetParameters(mMozAnon, mMozSystem);
  mXHR->SetClientInfoAndController(mClientInfo, mController);
  mXHR->SetAssociatedBrowsingContextID(
      aWorkerPrivate->AssociatedBrowsingContextID());

  ErrorResult rv;
  mXHRUpload = mXHR->GetUpload(rv);
  if (NS_WARN_IF(rv.Failed())) {
    mXHR = nullptr;
    return false;
  }

  if (!AddRemoveEventListeners(false, true)) {
    mXHR = nullptr;
    mXHRUpload = nullptr;
    return false;
  }

  return true;
}

void Proxy::Teardown() {
  AssertIsOnMainThread();

  if (mXHR) {
    Reset();

    // NB: We are intentionally dropping events coming from xhr.abort on the
    // floor.
    AddRemoveEventListeners(false, false);

    ErrorResult rv;
    mXHR->Abort(rv);
    if (NS_WARN_IF(rv.Failed())) {
      rv.SuppressException();
    }

    if (mOutstandingSendCount) {
      if (mSyncLoopTarget) {
        // We have an unclosed sync loop.  Fix that now.
        RefPtr<MainThreadStopSyncLoopRunnable> runnable =
            new MainThreadStopSyncLoopRunnable(std::move(mSyncLoopTarget),
                                               NS_ERROR_FAILURE);
        MOZ_ALWAYS_TRUE(runnable->Dispatch(mWorkerRef->Private()));
      }

      mOutstandingSendCount = 0;
    }

    mWorkerRef = nullptr;
    mXHRUpload = nullptr;
    mXHR = nullptr;
  }

  MOZ_ASSERT(!mWorkerRef);
  MOZ_ASSERT(!mSyncLoopTarget);
  // If there are rare edge cases left that violate our invariants
  // just ensure that they won't harm us too much.
  mWorkerRef = nullptr;
  mSyncLoopTarget = nullptr;
}

bool Proxy::AddRemoveEventListeners(bool aUpload, bool aAdd) {
  AssertIsOnMainThread();

  NS_ASSERTION(!aUpload || (mUploadEventListenersAttached && !aAdd) ||
                   (!mUploadEventListenersAttached && aAdd),
               "Messed up logic for upload listeners!");

  RefPtr<DOMEventTargetHelper> targetHelper =
      aUpload ? static_cast<XMLHttpRequestUpload*>(mXHRUpload.get())
              : static_cast<XMLHttpRequestEventTarget*>(mXHR.get());
  MOZ_ASSERT(targetHelper, "This should never fail!");

  for (const EventType* type : Events::All) {
    if (aUpload && *type == Events::readystatechange) {
      continue;
    }
    if (aAdd) {
      if (NS_FAILED(targetHelper->AddEventListener(*type, this, false))) {
        return false;
      }
    } else {
      targetHelper->RemoveEventListener(*type, this, false);
    }
  }

  if (aUpload) {
    mUploadEventListenersAttached = aAdd;
  }

  return true;
}

NS_IMPL_ISUPPORTS(Proxy, nsIDOMEventListener)

NS_IMETHODIMP
Proxy::HandleEvent(Event* aEvent) {
  AssertIsOnMainThread();

  // EventRunnable::WorkerRun will bail out if mXMLHttpRequestWorker is null,
  // so we do not need to prevent the dispatch from the main thread such that
  // we do not need to touch it off-worker-thread.
  if (!mWorkerRef) {
    NS_ERROR("Shouldn't get here!");
    return NS_OK;
  }

  nsAutoString _type;
  aEvent->GetType(_type);
  const EventType* typePtr = Events::Find(_type);
  MOZ_DIAGNOSTIC_ASSERT(typePtr, "Shouldn't get non-XMLHttpRequest events");
  const EventType& type = *typePtr;

  bool isUploadTarget = mXHR != aEvent->GetTarget();
  ProgressEvent* progressEvent = aEvent->AsProgressEvent();

  if (mInOpen && type == Events::readystatechange) {
    if (mXHR->ReadyState() == 1) {
      mInnerEventStreamId++;
    }
  }

  {
    AutoJSAPI jsapi;
    JSObject* junkScope = xpc::UnprivilegedJunkScope(fallible);
    if (!junkScope || !jsapi.Init(junkScope)) {
      return NS_ERROR_FAILURE;
    }
    JSContext* cx = jsapi.cx();

    JS::Rooted<JS::Value> value(cx);
    if (!GetOrCreateDOMReflectorNoWrap(cx, mXHR, &value)) {
      return NS_ERROR_FAILURE;
    }

    JS::Rooted<JSObject*> scope(cx, &value.toObject());

    RefPtr<EventRunnable> runnable;
    if (progressEvent) {
      if (!mIsSyncXHR || type != Events::progress) {
        runnable = new EventRunnable(
            this, isUploadTarget, type, progressEvent->LengthComputable(),
            progressEvent->Loaded(), progressEvent->Total(), scope);
      }
    } else {
      runnable = new EventRunnable(this, isUploadTarget, type, scope);
    }

    if (runnable) {
      runnable->Dispatch(mWorkerRef->Private());
    }
  }

  if (!isUploadTarget) {
    if (type == Events::loadstart) {
      mMainThreadSeenLoadStart = true;
    } else if (mMainThreadSeenLoadStart && type == Events::loadend) {
      mMainThreadSeenLoadStart = false;

      RefPtr<LoadStartDetectionRunnable> runnable =
          new LoadStartDetectionRunnable(this);
      if (!runnable->RegisterAndDispatch()) {
        NS_WARNING("Failed to dispatch LoadStartDetectionRunnable!");
      }
    }
  }

  return NS_OK;
}

NS_IMPL_ISUPPORTS_INHERITED(LoadStartDetectionRunnable, Runnable,
                            nsIDOMEventListener)

NS_IMETHODIMP
LoadStartDetectionRunnable::Run() {
  AssertIsOnMainThread();

  mXHR->RemoveEventListener(Events::loadstart, this, false);

  if (!mReceivedLoadStart) {
    if (mProxy->mOutstandingSendCount > 1) {
      mProxy->mOutstandingSendCount--;
    } else if (mProxy->mOutstandingSendCount == 1) {
      mProxy->Reset();

      RefPtr<ProxyCompleteRunnable> runnable =
          new ProxyCompleteRunnable(mProxy->Private(), mProxy, mChannelId);
      if (runnable->Dispatch(mProxy->Private())) {
        mProxy->mWorkerRef = nullptr;
        mProxy->mSyncLoopTarget = nullptr;
        mProxy->mOutstandingSendCount--;
      }
    }
  }

  mProxy = nullptr;
  mXHR = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
LoadStartDetectionRunnable::HandleEvent(Event* aEvent) {
  AssertIsOnMainThread();

#ifdef DEBUG
  {
    nsAutoString type;
    aEvent->GetType(type);
    MOZ_ASSERT(type == Events::loadstart);
  }
#endif

  mReceivedLoadStart = true;
  return NS_OK;
}

bool EventRunnable::PreDispatch(WorkerPrivate* /* unused */) {
  AssertIsOnMainThread();

  AutoJSAPI jsapi;
  DebugOnly<bool> ok = jsapi.Init(xpc::NativeGlobal(mScopeObj));
  MOZ_ASSERT(ok);
  JSContext* cx = jsapi.cx();
  // Now keep the mScopeObj alive for the duration
  JS::Rooted<JSObject*> scopeObj(cx, mScopeObj);
  // And reset mScopeObj now, before we have a chance to run its destructor on
  // some background thread.
  mScopeObj.reset();

  RefPtr<XMLHttpRequestMainThread>& xhr = mProxy->mXHR;
  MOZ_ASSERT(xhr);

  ErrorResult rv;

  XMLHttpRequestResponseType type = xhr->ResponseType();

  // We want to take the result data only if this is available.
  if (mType == Events::readystatechange) {
    switch (type) {
      case XMLHttpRequestResponseType::_empty:
      case XMLHttpRequestResponseType::Text: {
        xhr->GetResponseText(mResponseData->mResponseText, rv);
        mResponseData->mResponseResult = rv.StealNSResult();
        break;
      }

      case XMLHttpRequestResponseType::Blob: {
        mResponseData->mResponseBlobImpl = xhr->GetResponseBlobImpl();
        break;
      }

      case XMLHttpRequestResponseType::Arraybuffer: {
        mResponseData->mResponseArrayBufferBuilder =
            xhr->GetResponseArrayBufferBuilder();
        break;
      }

      case XMLHttpRequestResponseType::Json: {
        mResponseData->mResponseResult =
            xhr->GetResponseTextForJSON(mResponseData->mResponseJSON);
        break;
      }

      default:
        MOZ_ASSERT_UNREACHABLE("Invalid response type");
        return false;
    }
  }

  mStatus = xhr->GetStatus(rv);
  mStatusResult = rv.StealNSResult();

  mErrorDetail = xhr->ErrorDetail();

  xhr->GetStatusText(mStatusText, rv);
  MOZ_ASSERT(!rv.Failed());

  mReadyState = xhr->ReadyState();

  xhr->GetResponseURL(mResponseURL);

  return true;
}

bool EventRunnable::WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) {
  if (!mProxy->mXMLHttpRequestPrivate) {
    // Object was finalized, bail.
    return true;
  }

  if (mEventStreamId != mProxy->mXMLHttpRequestPrivate->EventStreamId()) {
    // Threads raced, this event is now obsolete.
    return true;
  }

  if (mType == Events::loadend) {
    mProxy->mLastErrorDetailAtLoadend = mErrorDetail;
  }

  bool isLoadStart = mType == Events::loadstart;
  if (mUploadEvent) {
    if (isLoadStart) {
      MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
              ("Saw upload.loadstart event on main thread"));
      mProxy->mSeenUploadLoadStart = true;
    } else if (mType == Events::loadend) {
      MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
              ("Saw upload.loadend event on main thread"));
      mProxy->mSeenUploadLoadEnd = true;
    }
  }

  if (mProgressEvent) {
    // Cache these in case we need them for an error event.
    if (mUploadEvent) {
      mProxy->mLastUploadLengthComputable = mLengthComputable;
      mProxy->mLastUploadLoaded = mLoaded;
      mProxy->mLastUploadTotal = mTotal;
    } else {
      mProxy->mLastLengthComputable = mLengthComputable;
      mProxy->mLastLoaded = mLoaded;
      mProxy->mLastTotal = mTotal;
    }
  }

  auto state = std::make_unique<XMLHttpRequestWorker::StateData>();

  state->mStatusResult = mStatusResult;
  state->mStatus = mStatus;

  state->mStatusText = mStatusText;

  state->mReadyState = mReadyState;

  state->mResponseURL = mResponseURL;

  XMLHttpRequestWorker* xhr = mProxy->mXMLHttpRequestPrivate;
  xhr->UpdateState(std::move(state), mType == Events::readystatechange
                                         ? std::move(mResponseData)
                                         : nullptr);

  if (mUploadEvent && !xhr->GetUploadObjectNoCreate()) {
    return true;
  }

  XMLHttpRequestEventTarget* target;
  if (mUploadEvent) {
    target = xhr->GetUploadObjectNoCreate();
  } else {
    target = xhr;
  }

  MOZ_ASSERT(target);

  RefPtr<Event> event;
  if (mProgressEvent) {
    ProgressEventInit init;
    init.mBubbles = false;
    init.mCancelable = false;
    init.mLengthComputable = mLengthComputable;
    init.mLoaded = mLoaded;
    init.mTotal = mTotal;

    event = ProgressEvent::Constructor(target, mType, init);
  } else {
    event = NS_NewDOMEvent(target, nullptr, nullptr);

    if (event) {
      event->InitEvent(mType, false, false);
    }
  }

  if (!event) {
    MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
            ("%p unable to fire %s event (%u,%u,%" PRIu64 ",%" PRIu64 ")",
             mProxy->mXHR.get(), mType.cStr, mUploadEvent, mLengthComputable,
             mLoaded, mTotal));
    return false;
  }

  event->SetTrusted(true);

  MOZ_LOG(
      gXMLHttpRequestLog, LogLevel::Debug,
      ("%p firing %s event (%u,%u,%" PRIu64 ",%" PRIu64 ")", mProxy->mXHR.get(),
       mType.cStr, mUploadEvent, mLengthComputable, mLoaded, mTotal));

  target->DispatchEvent(*event);

  return true;
}

bool WorkerThreadProxySyncRunnable::MainThreadRun() {
  AssertIsOnMainThread();

  nsCOMPtr<nsIEventTarget> tempTarget = mSyncLoopTarget;

  mProxy->mSyncEventResponseTarget.swap(tempTarget);

  ErrorResult rv;
  RunOnMainThread(rv);
  mErrorCode = rv.StealNSResult();

  mProxy->mSyncEventResponseTarget.swap(tempTarget);

  return true;
}

void AbortRunnable::RunOnMainThread(ErrorResult& aRv) {
  mProxy->mInnerEventStreamId++;

  MOZ_ASSERT(mWorkerRef);

  MOZ_ASSERT_IF(mProxy->mWorkerRef,
                mProxy->mWorkerRef->Private() == mWorkerRef->Private());

  // mProxy wants a valid ThreadSafeWorkerRef for the duration of our call,
  // but mProxy->mWorkerRef may be null if a send is not currently active,
  // so save the existing value for the duration of the call.
  RefPtr<ThreadSafeWorkerRef> oldWorker = std::move(mProxy->mWorkerRef);

  // WorkerMainThreadRunnable::mWorkerRef must not be nullptr here, since
  // when get here, it means this WorkerMainThreadRunnable had already be
  // dispatched successfully and the execution is on the main thread.
  MOZ_ASSERT_DEBUG_OR_FUZZING(mWorkerRef);

  // Set mProxy->mWorkerRef as AbortRunnable::mWorkerRef which is from
  // WorkerMainThreadRunnable during the runnable execution.
  // Let AbortRunnable keep a reference for dispatching
  // MainThreadStopSyncRunnable back to the Worker thread after the main thread
  // execution completes.
  mProxy->mWorkerRef = mWorkerRef;

  mProxy->mXHR->Abort(aRv);

  // Restore the activated WorkerRef to mProxy for the previous Send().
  mProxy->mWorkerRef = std::move(oldWorker);

  mProxy->Reset();
}

void OpenRunnable::MainThreadRunInternal(ErrorResult& aRv) {
  MOZ_ASSERT(mWorkerRef);
  if (!mProxy->Init(mWorkerRef->Private())) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  if (mBackgroundRequest) {
    mProxy->mXHR->SetMozBackgroundRequestExternal(mBackgroundRequest, aRv);
    if (aRv.Failed()) {
      return;
    }
  }

  if (mOriginStack) {
    mProxy->mXHR->SetOriginStack(std::move(mOriginStack));
  }

  if (mWithCredentials) {
    mProxy->mXHR->SetWithCredentials(mWithCredentials, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return;
    }
  }

  if (mTimeout) {
    mProxy->mXHR->SetTimeout(mTimeout, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return;
    }
  }

  if (!mMimeTypeOverride.IsVoid()) {
    mProxy->mXHR->OverrideMimeType(mMimeTypeOverride, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return;
    }
  }

  MOZ_ASSERT(!mProxy->mInOpen);
  mProxy->mInOpen = true;

  mProxy->mXHR->Open(
      mMethod, mURL, true, mUser.WasPassed() ? mUser.Value() : VoidCString(),
      mPassword.WasPassed() ? mPassword.Value() : VoidCString(), aRv);

  MOZ_ASSERT(mProxy->mInOpen);
  mProxy->mInOpen = false;

  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  if (mSource) {
    mProxy->mXHR->SetSource(std::move(mSource));
  }

  mProxy->mXHR->SetResponseType(mResponseType, aRv);
}

void SendRunnable::RunOnMainThread(ErrorResult& aRv) {
  // Before we change any state let's check if we can send.
  if (!mProxy->mXHR->CanSend(aRv)) {
    return;
  }

  Nullable<
      DocumentOrBlobOrArrayBufferViewOrArrayBufferOrFormDataOrURLSearchParamsOrUSVString>
      payload;

  if (!mBlobImpl) {
    payload.SetNull();
  } else {
    JS::Rooted<JSObject*> globalObject(RootingCx(),
                                       xpc::UnprivilegedJunkScope(fallible));
    if (NS_WARN_IF(!globalObject)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }

    nsCOMPtr<nsIGlobalObject> parent = xpc::NativeGlobal(globalObject);
    if (NS_WARN_IF(!parent)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }

    RefPtr<Blob> blob = Blob::Create(parent, mBlobImpl);
    MOZ_ASSERT(blob);

    DocumentOrBlobOrArrayBufferViewOrArrayBufferOrFormDataOrURLSearchParamsOrUSVString&
        ref = payload.SetValue();
    ref.SetAsBlob() = blob;
  }

  // Send() has been already called, reset the proxy.
  if (mProxy->mWorkerRef) {
    mProxy->Reset();
  }

  MOZ_ASSERT(mWorkerRef);
  mProxy->mWorkerRef = mWorkerRef;

  MOZ_ASSERT(!mProxy->mSyncLoopTarget);
  mProxy->mSyncLoopTarget.swap(mSyncXHRSyncLoopTarget);

  if (mHasUploadListeners) {
    // Send() can be called more than once before failure,
    // so don't attach the upload listeners more than once.
    if (!mProxy->mUploadEventListenersAttached &&
        !mProxy->AddRemoveEventListeners(true, true)) {
      MOZ_ASSERT(false, "This should never fail!");
    }
  }

  mProxy->mInnerChannelId++;

  mProxy->mXHR->Send(payload, aRv);

  if (!aRv.Failed()) {
    mProxy->mOutstandingSendCount++;

    if (!mHasUploadListeners) {
      // Send() can be called more than once before failure,
      // so don't attach the upload listeners more than once.
      if (!mProxy->mUploadEventListenersAttached &&
          !mProxy->AddRemoveEventListeners(true, true)) {
        MOZ_ASSERT(false, "This should never fail!");
      }
    }
  } else {
    // In case of failure we just break the sync loop
    mProxy->mSyncLoopTarget = nullptr;
    mSyncXHRSyncLoopTarget = nullptr;
  }
}

XMLHttpRequestWorker::XMLHttpRequestWorker(WorkerPrivate* aWorkerPrivate,
                                           nsIGlobalObject* aGlobalObject)
    : XMLHttpRequest(aGlobalObject),
      mResponseType(XMLHttpRequestResponseType::_empty),
      mStateData(new StateData()),
      mResponseData(new ResponseData()),
      mResponseArrayBufferValue(nullptr),
      mResponseJSONValue(JS::UndefinedValue()),
      mTimeout(0),
      mBackgroundRequest(false),
      mWithCredentials(false),
      mCanceled(false),
      mFlagSendActive(false),
      mMozAnon(false),
      mMozSystem(false),
      mMimeTypeOverride(VoidString()) {
  aWorkerPrivate->AssertIsOnWorkerThread();

  mozilla::HoldJSObjects(this);
}

XMLHttpRequestWorker::~XMLHttpRequestWorker() {
  ReleaseProxy(XHRIsGoingAway);

  MOZ_ASSERT(!mWorkerRef);

  mozilla::DropJSObjects(this);
}

XMLHttpRequestWorker::ResponseData::ResponseData() : mResponseResult(NS_OK) {}

NS_IMPL_ADDREF_INHERITED(XMLHttpRequestWorker, XMLHttpRequestEventTarget)
NS_IMPL_RELEASE_INHERITED(XMLHttpRequestWorker, XMLHttpRequestEventTarget)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(XMLHttpRequestWorker)
NS_INTERFACE_MAP_END_INHERITING(XMLHttpRequestEventTarget)

NS_IMPL_CYCLE_COLLECTION_CLASS(XMLHttpRequestWorker)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(XMLHttpRequestWorker,
                                                  XMLHttpRequestEventTarget)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mUpload)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mResponseBlob)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(XMLHttpRequestWorker,
                                                XMLHttpRequestEventTarget)
  tmp->ReleaseProxy(XHRIsGoingAway);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mUpload)
  tmp->mResponseData = nullptr;
  tmp->mResponseBlob = nullptr;
  tmp->mResponseArrayBufferValue = nullptr;
  tmp->mResponseJSONValue.setUndefined();
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_PTR
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(XMLHttpRequestWorker,
                                               XMLHttpRequestEventTarget)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mResponseArrayBufferValue)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mResponseJSONValue)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

/* static */
already_AddRefed<XMLHttpRequest> XMLHttpRequestWorker::Construct(
    const GlobalObject& aGlobal, const MozXMLHttpRequestParameters& aParams,
    ErrorResult& aRv) {
  JSContext* cx = aGlobal.Context();
  WorkerPrivate* workerPrivate = GetWorkerPrivateFromContext(cx);
  MOZ_ASSERT(workerPrivate);

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (NS_WARN_IF(!global)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<XMLHttpRequestWorker> xhr =
      new XMLHttpRequestWorker(workerPrivate, global);

  if (workerPrivate->XHRParamsAllowed()) {
    if (aParams.mMozSystem) {
      xhr->mMozAnon = true;
    } else {
      xhr->mMozAnon =
          aParams.mMozAnon.WasPassed() ? aParams.mMozAnon.Value() : false;
    }
    xhr->mMozSystem = aParams.mMozSystem;
  }

  return xhr.forget();
}

void XMLHttpRequestWorker::ReleaseProxy(ReleaseType aType) {
  if (mProxy) {
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(workerPrivate);
    if (aType == XHRIsGoingAway) {
      // Coming here means the XHR was GC'd, so we can't be pinned.
      MOZ_ASSERT(!mProxy->mXMLHttpRequestPrivate ||
                 !mProxy->mXMLHttpRequestPrivate->mPinnedSelfRef);

      // We need to clear our weak pointer on the worker thread, let's do it now
      // before doing it implicitly in the Proxy dtor on the wrong thread.
      mProxy->mXMLHttpRequestPrivate = nullptr;

      // We're in a GC finalizer, so we can't do a sync call here (and we don't
      // need to).
      RefPtr<AsyncTeardownRunnable> runnable =
          new AsyncTeardownRunnable(mProxy);
      mProxy = nullptr;

      if (NS_FAILED(workerPrivate->DispatchToMainThread(runnable.forget()))) {
        NS_ERROR("Failed to dispatch teardown runnable!");
      }
    } else {
      // This isn't necessary if the worker is going away or the XHR is going
      // away.
      if (aType == Default) {
        // Don't let any more events run.
        mEventStreamId++;
      }

      // Ensure we are unpinned before we clear the weak reference.
      RefPtr<XMLHttpRequestWorker> self = this;
      if (mPinnedSelfRef) {
        Unpin();
      }
      mProxy->mXMLHttpRequestPrivate = nullptr;

      // We need to make a sync call here.
      RefPtr<SyncTeardownRunnable> runnable =
          new SyncTeardownRunnable(workerPrivate, mProxy);
      mProxy = nullptr;

      IgnoredErrorResult forAssertionsOnly;
      // This runnable _must_ be executed.
      // XXX This is a bit weird the failure status is Dead. Dispatching this
      // WorkerThreadRunnable in Killing status is not reasonable for Worker.
      runnable->Dispatch(workerPrivate, Dead, forAssertionsOnly);
      MOZ_DIAGNOSTIC_ASSERT(!forAssertionsOnly.Failed());
    }
  }
}

void XMLHttpRequestWorker::MaybePin(ErrorResult& aRv) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  if (mWorkerRef) {
    return;
  }

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();

  RefPtr<XMLHttpRequestWorker> self = this;
  RefPtr<StrongWorkerRef> workerRef =
      StrongWorkerRef::Create(workerPrivate, "XMLHttpRequestWorker", [self]() {
        if (!self->mCanceled) {
          self->mCanceled = true;
          self->ReleaseProxy(WorkerIsGoingAway);
        }
      });
  if (NS_WARN_IF(!workerRef)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }
  mWorkerRef = MakeRefPtr<ThreadSafeWorkerRef>(workerRef);

  mPinnedSelfRef = this;

#ifdef DEBUG
  mProxy->DebugStoreWorkerRef(mWorkerRef);
#endif
}

void XMLHttpRequestWorker::SetResponseToNetworkError() {
  MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug, ("SetResponseToNetworkError"));
  mStateData->mStatus = 0;
  mStateData->mStatusText.Truncate();
  if (mProxy) {
    mProxy->mLastLengthComputable = false;
    mProxy->mLastLoaded = 0;
    mProxy->mLastTotal = 0;
    mProxy->mLastUploadLengthComputable = false;
    mProxy->mLastUploadLoaded = 0;
    mProxy->mLastUploadTotal = 0;
  }
}

void XMLHttpRequestWorker::RequestErrorSteps(
    ErrorResult& aRv, const ErrorProgressEventType& aEventType,
    nsresult aException) {
  // https://xhr.spec.whatwg.org/#request-error-steps
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
          ("RequestErrorSteps(%s)", aEventType.cStr));

  MOZ_ASSERT(mProxy);

  // Step 1: Set xhr’s state to done.
  mStateData->mReadyState = XMLHttpRequest_Binding::DONE;

  // Step 2: Unset xhr’s send() flag.
  mFlagSend = false;

  // Step 3: Set xhr’s response to a network error.
  SetResponseToNetworkError();

  // Step 4: If xhr’s synchronous flag is set, then throw exception.
  if (!mProxy || mProxy->mIsSyncXHR) {
    aRv.Throw(aException);
    return;
  }

  // Step 5: Fire an event named readystatechange at xhr.
  if (!FireEvent(this, Events::readystatechange, false, aRv)) {
    return;
  }

  // Step 6: If xhr’s upload complete flag is unset, then:
  if (mUpload && mProxy && mProxy->mSeenUploadLoadStart &&
      !mProxy->mSeenUploadLoadEnd) {
    // Gecko-specific: we can only know whether the proxy XHR's upload
    // complete flag is set by waiting for the related upload loadend
    // event to happen (at which point upload complete has just been set,
    // either in Request Error Steps or processRequestEndOfBody.

    // Step 6.1: Set xhr’s upload complete flag.
    // We don't need to keep track of this.

    // Gecko-specific: we must Fire the loadstart event,
    // as we have not done so yet.
    if (!FireEvent(mUpload, Events::loadstart, true, aRv)) {
      return;
    }

    // Step 6.2: If xhr’s upload listener flag is set, then:
    // We know there must be listeners since we saw an upload loadstart.

    // Step 6.2.1: Fire a progress event named event at xhr’s upload object with
    // 0 and 0.
    if (!FireEvent(mUpload, aEventType, true, aRv)) {
      return;
    }

    // Step 6.2.2: Fire a progress event named loadend at xhr’s upload object
    // with 0 and 0.
    if (!FireEvent(mUpload, Events::loadend, true, aRv)) {
      return;
    }
  }

  // Step 7: Fire a progress event named event at xhr with 0 and 0.
  if (!FireEvent(this, aEventType, true, aRv)) {
    return;
  }

  // Step 8: Fire a progress event named loadend at xhr with 0 and 0.
  FireEvent(this, Events::loadend, true, aRv);
}

// A false return value here indicates that we should consider the XHR
// to have been re-opened, or something catastrophic to have happened,
// where we should stop running any code we normally would after firing
// the event (such as firing more events). This includes if an exception
// is thrown in aRv.
bool XMLHttpRequestWorker::FireEvent(EventTarget* aTarget,
                                     const EventType& aEventType,
                                     bool aUploadTarget, ErrorResult& aRv) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());
  MOZ_ASSERT(aTarget);

  if (!mProxy) {
    aRv.Throw(NS_ERROR_FAILURE);
    return false;
  }

  uint32_t currentEventStreamId = mEventStreamId;
  RefPtr<Event> event;
  if (aEventType == Events::readystatechange) {
    event = NS_NewDOMEvent(aTarget, nullptr, nullptr);
    event->InitEvent(aEventType, false, false);
  } else {
    if (mProxy->mIsSyncXHR && aEventType == Events::progress) {
      return true;
    }

    ProgressEventInit init;
    init.mBubbles = false;
    init.mCancelable = false;
    if (aUploadTarget) {
      init.mLengthComputable = mProxy->mLastUploadLengthComputable;
      init.mLoaded = mProxy->mLastUploadLoaded;
      init.mTotal = mProxy->mLastUploadTotal;
    } else {
      init.mLengthComputable = mProxy->mLastLengthComputable;
      init.mLoaded = mProxy->mLastLoaded;
      init.mTotal = mProxy->mLastTotal;
    }
    event = ProgressEvent::Constructor(aTarget, aEventType, init);
  }

  if (!event) {
    aRv.Throw(NS_ERROR_FAILURE);
    return false;
  }

  event->SetTrusted(true);

  MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
          ("%p firing %s pre-abort event (%u,%u,%" PRIu64 ",%" PRIu64, this,
           aEventType.cStr, aUploadTarget,
           aUploadTarget ? mProxy->mLastUploadLengthComputable
                         : mProxy->mLastLengthComputable,
           aUploadTarget ? mProxy->mLastUploadLoaded : mProxy->mLastLoaded,
           aUploadTarget ? mProxy->mLastUploadTotal : mProxy->mLastTotal));
  aTarget->DispatchEvent(*event);

  // if dispatching the event caused code to run which re-opened us, and
  // therefore changed  our event stream, return false.
  return currentEventStreamId == mEventStreamId;
}

void XMLHttpRequestWorker::Unpin() {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  MOZ_ASSERT(mWorkerRef, "Mismatched calls to Unpin!");

#ifdef DEBUG
  if (mProxy) {
    // The proxy will be gone if WorkerIsGoingAway
    mProxy->DebugForgetWorkerRef();
  }
#endif

  mWorkerRef = nullptr;

  mPinnedSelfRef = nullptr;
}

uint16_t XMLHttpRequestWorker::ReadyState() const {
  MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
          ("GetReadyState(%u)", mStateData->mReadyState));
  return mStateData->mReadyState;
}

void XMLHttpRequestWorker::SendInternal(const BodyExtractorBase* aBody,
                                        ErrorResult& aRv) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  // We don't really need to keep the same body-type when we proxy the send()
  // call to the main-thread XHR. Let's extract the nsIInputStream from the
  // aBody and let's wrap it into a StreamBlobImpl.

  RefPtr<BlobImpl> blobImpl;

  if (aBody) {
    nsAutoCString charset;
    nsAutoCString defaultContentType;
    nsCOMPtr<nsIInputStream> uploadStream;

    uint64_t size_u64;
    aRv = aBody->GetAsStream(getter_AddRefs(uploadStream), &size_u64,
                             defaultContentType, charset);
    if (NS_WARN_IF(aRv.Failed())) {
      return;
    }

    blobImpl = StreamBlobImpl::Create(uploadStream.forget(),
                                      NS_ConvertUTF8toUTF16(defaultContentType),
                                      size_u64, u"StreamBlobImpl"_ns);
    MOZ_ASSERT(blobImpl);
  }

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();

  RefPtr<SendRunnable> sendRunnable =
      new SendRunnable(workerPrivate, mProxy, blobImpl);

  // No send() calls when open is running.
  if (mProxy->mOpenCount) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  bool hasUploadListeners = mUpload ? mUpload->HasListeners() : false;

  MaybePin(aRv);
  if (aRv.Failed()) {
    return;
  }

  RefPtr<XMLHttpRequestWorker> selfRef = this;
  AutoUnpinXHR autoUnpin(this);
  Maybe<AutoSyncLoopHolder> syncXHRSyncLoop;

  nsCOMPtr<nsISerialEventTarget> syncXHRSyncLoopTarget;
  bool isSyncXHR = mProxy->mIsSyncXHR;
  if (isSyncXHR) {
    syncXHRSyncLoop.emplace(workerPrivate, Canceling);
    syncXHRSyncLoopTarget = syncXHRSyncLoop->GetSerialEventTarget();
    if (!syncXHRSyncLoopTarget) {
      aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
      return;
    }
  }

  mProxy->mOuterChannelId++;

  sendRunnable->SetSyncXHRSyncLoopTarget(syncXHRSyncLoopTarget);
  sendRunnable->SetHaveUploadListeners(hasUploadListeners);

  mFlagSend = true;

  sendRunnable->Dispatch(workerPrivate, Canceling, aRv);
  if (aRv.Failed()) {
    // Dispatch() may have spun the event loop and we may have already unrooted.
    // If so we don't want autoUnpin to try again.
    if (!mWorkerRef) {
      autoUnpin.Clear();
    }
    return;
  }

  if (!isSyncXHR) {
    autoUnpin.Clear();
    MOZ_ASSERT(!syncXHRSyncLoop);
    return;
  }

  autoUnpin.Clear();

  bool succeeded = NS_SUCCEEDED(syncXHRSyncLoop->Run());

  // Throw appropriately If a sync XHR failed per spec's RequestErrorSteps
  if (isSyncXHR && mProxy) {
    nsresult error = mProxy->mLastErrorDetailAtLoadend;
    if (error == NS_ERROR_DOM_ABORT_ERR) {
      MOZ_LOG(gXMLHttpRequestLog, LogLevel::Info,
              ("%p throwing NS_ERROR_DOM_ABORT_ERR", this));
      aRv.Throw(error);
      return;
    }
    if (error == NS_ERROR_DOM_TIMEOUT_ERR) {
      MOZ_LOG(gXMLHttpRequestLog, LogLevel::Info,
              ("%p throwing NS_ERROR_DOM_TIMEOUT_ERR", this));
      aRv.Throw(error);
      return;
    }
    if (error == NS_ERROR_DOM_NETWORK_ERR ||
        NS_ERROR_GET_MODULE(error) == NS_ERROR_MODULE_NETWORK) {
      MOZ_LOG(gXMLHttpRequestLog, LogLevel::Info,
              ("%p throwing NS_ERROR_DOM_NETWORK_ERR (0x%" PRIx32 ")", this,
               static_cast<uint32_t>(error)));
      aRv.Throw(NS_ERROR_DOM_NETWORK_ERR);
      return;
    }
  }

  // Don't clobber an existing exception that we may have thrown on aRv
  // already... though can there really be one?  In any case, it seems to me
  // that this autoSyncLoop->Run() can never fail, since the StopSyncLoop call
  // for it will come from ProxyCompleteRunnable and that always passes true for
  // the second arg.
  if (!succeeded && !aRv.Failed()) {
    MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
            ("%p SendInternal failed; throwing NS_ERROR_FAILURE", this));
    aRv.Throw(NS_ERROR_FAILURE);
  }
}

void XMLHttpRequestWorker::Open(const nsACString& aMethod,
                                const nsACString& aUrl, bool aAsync,
                                const Optional<nsACString>& aUser,
                                const Optional<nsACString>& aPassword,
                                ErrorResult& aRv) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
          ("%p Open(%s,%s,%d)", this, PromiseFlatCString(aMethod).get(),
           PromiseFlatCString(aUrl).get(), aAsync));

  if (mCanceled) {
    aRv.ThrowUncatchableException();
    return;
  }

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();

  mFlagSend = false;

  bool alsoOverrideMimeType = false;
  if (!mProxy) {
    Maybe<ClientInfo> clientInfo(workerPrivate->GlobalScope()->GetClientInfo());
    if (clientInfo.isNothing()) {
      aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
      return;
    }
    mProxy = new Proxy(this, clientInfo.ref(),
                       workerPrivate->GlobalScope()->GetController(), mMozAnon,
                       mMozSystem);
    alsoOverrideMimeType = true;
  }

  mProxy->mSeenUploadLoadStart = false;
  mProxy->mSeenUploadLoadEnd = false;
  SetResponseToNetworkError();

  mEventStreamId++;

  UniquePtr<SerializedStackHolder> stack;
  if (workerPrivate->IsWatchedByDevTools()) {
    if (JSContext* cx = nsContentUtils::GetCurrentJSContext()) {
      stack = GetCurrentStackForNetMonitor(cx);
    }
  }

  RefPtr<OpenRunnable> runnable = new OpenRunnable(
      workerPrivate, mProxy, aMethod, aUrl, aUser, aPassword,
      mBackgroundRequest, mWithCredentials, mTimeout, mResponseType,
      alsoOverrideMimeType ? mMimeTypeOverride : VoidString(), std::move(stack),
      profiler_capture_backtrace());

  ++mProxy->mOpenCount;
  runnable->Dispatch(workerPrivate, Canceling, aRv);
  if (aRv.Failed()) {
    if (mProxy && !--mProxy->mOpenCount) {
      ReleaseProxy();
    }

    return;
  }

  // We have been released in one of the nested Open() calls.
  if (!mProxy) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  --mProxy->mOpenCount;
  mProxy->mIsSyncXHR = !aAsync;
}

void XMLHttpRequestWorker::SetRequestHeader(const nsACString& aHeader,
                                            const nsACString& aValue,
                                            ErrorResult& aRv) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  if (mCanceled) {
    aRv.ThrowUncatchableException();
    return;
  }

  if (!mProxy) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();

  RefPtr<SetRequestHeaderRunnable> runnable =
      new SetRequestHeaderRunnable(workerPrivate, mProxy, aHeader, aValue);
  runnable->Dispatch(workerPrivate, Canceling, aRv);
}

void XMLHttpRequestWorker::SetTimeout(uint32_t aTimeout, ErrorResult& aRv) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  if (mCanceled) {
    aRv.ThrowUncatchableException();
    return;
  }

  mTimeout = aTimeout;

  if (!mProxy) {
    // Open might not have been called yet, in which case we'll handle the
    // timeout in OpenRunnable.
    return;
  }

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();

  RefPtr<SetTimeoutRunnable> runnable =
      new SetTimeoutRunnable(workerPrivate, mProxy, aTimeout);
  runnable->Dispatch(workerPrivate, Canceling, aRv);
}

void XMLHttpRequestWorker::SetWithCredentials(bool aWithCredentials,
                                              ErrorResult& aRv) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  if (mCanceled) {
    aRv.ThrowUncatchableException();
    return;
  }

  mWithCredentials = aWithCredentials;

  if (!mProxy) {
    // Open might not have been called yet, in which case we'll handle the
    // credentials in OpenRunnable.
    return;
  }

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();

  RefPtr<SetWithCredentialsRunnable> runnable =
      new SetWithCredentialsRunnable(workerPrivate, mProxy, aWithCredentials);
  runnable->Dispatch(workerPrivate, Canceling, aRv);
}

void XMLHttpRequestWorker::SetMozBackgroundRequest(bool aBackgroundRequest,
                                                   ErrorResult& aRv) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  if (mCanceled) {
    aRv.ThrowUncatchableException();
    return;
  }

  mBackgroundRequest = aBackgroundRequest;

  if (!mProxy) {
    // Open might not have been called yet, in which case we'll handle the
    // background request in OpenRunnable.
    return;
  }

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();

  RefPtr<SetBackgroundRequestRunnable> runnable =
      new SetBackgroundRequestRunnable(workerPrivate, mProxy,
                                       aBackgroundRequest);
  runnable->Dispatch(workerPrivate, Canceling, aRv);
}

XMLHttpRequestUpload* XMLHttpRequestWorker::GetUpload(ErrorResult& aRv) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  if (mCanceled) {
    aRv.ThrowUncatchableException();
    return nullptr;
  }

  if (!mUpload) {
    mUpload = new XMLHttpRequestUpload(this);
  }

  return mUpload;
}

void XMLHttpRequestWorker::Send(
    const Nullable<
        DocumentOrBlobOrArrayBufferViewOrArrayBufferOrFormDataOrURLSearchParamsOrUSVString>&
        aData,
    ErrorResult& aRv) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug, ("Send()"));

  if (mFlagSendActive) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_XHR_HAS_INVALID_CONTEXT);
    return;
  }
  mFlagSendActive = true;
  auto clearRecursionFlag = MakeScopeExit([&]() {
    // No one else should have touched this flag.
    MOZ_ASSERT(mFlagSendActive);
    mFlagSendActive = false;
  });

  if (mCanceled) {
    aRv.ThrowUncatchableException();
    return;
  }

  if (mStateData->mReadyState != XMLHttpRequest_Binding::OPENED) {
    aRv.ThrowInvalidStateError("XMLHttpRequest state must be OPENED.");
    return;
  }

  if (!mProxy || !mProxy->mXMLHttpRequestPrivate || mFlagSend) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  if (aData.IsNull()) {
    SendInternal(nullptr, aRv);
    return;
  }

  if (aData.Value().IsDocument()) {
    MOZ_ASSERT_UNREACHABLE("Documents are not exposed to workers.");
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  if (aData.Value().IsBlob()) {
    BodyExtractor<const Blob> body(&aData.Value().GetAsBlob());
    SendInternal(&body, aRv);
    return;
  }

  if (aData.Value().IsArrayBuffer()) {
    BodyExtractor<const ArrayBuffer> body(&aData.Value().GetAsArrayBuffer());
    SendInternal(&body, aRv);
    return;
  }

  if (aData.Value().IsArrayBufferView()) {
    BodyExtractor<const ArrayBufferView> body(
        &aData.Value().GetAsArrayBufferView());
    SendInternal(&body, aRv);
    return;
  }

  if (aData.Value().IsFormData()) {
    BodyExtractor<const FormData> body(&aData.Value().GetAsFormData());
    SendInternal(&body, aRv);
    return;
  }

  if (aData.Value().IsURLSearchParams()) {
    BodyExtractor<const URLSearchParams> body(
        &aData.Value().GetAsURLSearchParams());
    SendInternal(&body, aRv);
    return;
  }

  if (aData.Value().IsUSVString()) {
    BodyExtractor<const nsAString> body(&aData.Value().GetAsUSVString());
    SendInternal(&body, aRv);
    return;
  }
}

void XMLHttpRequestWorker::Abort(ErrorResult& aRv) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  if (mCanceled) {
    MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug, ("Abort(canceled)"));
    aRv.ThrowUncatchableException();
    return;
  }

  if (!mProxy) {
    MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug, ("Abort(no proxy)"));
    return;
  }

  // Spec step 1
  MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug, ("Abort(step 1))"));
  mEventStreamId++;

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  RefPtr<AbortRunnable> runnable = new AbortRunnable(workerPrivate, mProxy);
  runnable->Dispatch(workerPrivate, Canceling, aRv);
  if (aRv.Failed()) {
    return;
  }

  // Dispatch() spun the event loop, so we may have been canceled and released.
  if (!mProxy) {
    MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug, ("Abort(no proxy)"));
    return;
  }

  // Spec step 2
  if ((mStateData->mReadyState == XMLHttpRequest_Binding::OPENED &&
       mFlagSend) ||
      mStateData->mReadyState == XMLHttpRequest_Binding::HEADERS_RECEIVED ||
      mStateData->mReadyState == XMLHttpRequest_Binding::LOADING) {
    MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug, ("Abort(step 2)"));
    RequestErrorSteps(aRv, Events::abort);
    if (aRv.Failed()) {
      return;
    }
  }

  // Spec step 3
  if (mStateData->mReadyState == XMLHttpRequest_Binding::DONE) {
    MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug, ("Abort(step 3)"));
    mStateData->mReadyState = XMLHttpRequest_Binding::UNSENT;
  }
}

void XMLHttpRequestWorker::GetResponseHeader(const nsACString& aHeader,
                                             nsACString& aResponseHeader,
                                             ErrorResult& aRv) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  if (mCanceled) {
    aRv.ThrowUncatchableException();
    return;
  }

  if (!mProxy) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();

  nsCString responseHeader;
  RefPtr<GetResponseHeaderRunnable> runnable = new GetResponseHeaderRunnable(
      workerPrivate, mProxy, aHeader, responseHeader);
  runnable->Dispatch(workerPrivate, Canceling, aRv);
  if (aRv.Failed()) {
    return;
  }
  aResponseHeader = std::move(responseHeader);
}

void XMLHttpRequestWorker::GetAllResponseHeaders(nsACString& aResponseHeaders,
                                                 ErrorResult& aRv) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  if (mCanceled) {
    aRv.ThrowUncatchableException();
    return;
  }

  if (!mProxy) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();

  nsCString responseHeaders;
  RefPtr<GetAllResponseHeadersRunnable> runnable =
      new GetAllResponseHeadersRunnable(workerPrivate, mProxy, responseHeaders);
  runnable->Dispatch(workerPrivate, Canceling, aRv);
  if (aRv.Failed()) {
    return;
  }

  aResponseHeaders = std::move(responseHeaders);
}

void XMLHttpRequestWorker::OverrideMimeType(const nsAString& aMimeType,
                                            ErrorResult& aRv) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  if (mCanceled) {
    aRv.ThrowUncatchableException();
    return;
  }

  // We're supposed to throw if the state is LOADING or DONE.
  if (mStateData->mReadyState == XMLHttpRequest_Binding::LOADING ||
      mStateData->mReadyState == XMLHttpRequest_Binding::DONE) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  mMimeTypeOverride = aMimeType;

  if (mProxy) {
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    RefPtr<OverrideMimeTypeRunnable> runnable =
        new OverrideMimeTypeRunnable(workerPrivate, mProxy, aMimeType);
    runnable->Dispatch(workerPrivate, Canceling, aRv);
  }
}

void XMLHttpRequestWorker::SetResponseType(
    XMLHttpRequestResponseType aResponseType, ErrorResult& aRv) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(IsCurrentThreadRunningWorker());

  // "document" is fine for the main thread but not for a worker. Short-circuit
  // that here.
  if (aResponseType == XMLHttpRequestResponseType::Document) {
    return;
  }

  if (!mProxy) {
    // Open() has not been called yet. We store the responseType and we will use
    // it later in Open().
    mResponseType = aResponseType;
    return;
  }

  if (mStateData->mReadyState == XMLHttpRequest_Binding::LOADING ||
      mStateData->mReadyState == XMLHttpRequest_Binding::DONE) {
    aRv.ThrowInvalidStateError(
        "Cannot set 'responseType' property on XMLHttpRequest after 'send()' "
        "(when its state is LOADING or DONE).");
    return;
  }

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  RefPtr<SetResponseTypeRunnable> runnable =
      new SetResponseTypeRunnable(workerPrivate, mProxy, aResponseType);
  runnable->Dispatch(workerPrivate, Canceling, aRv);
  if (aRv.Failed()) {
    return;
  }

  mResponseType = runnable->ResponseType();
}

void XMLHttpRequestWorker::GetResponse(JSContext* aCx,
                                       JS::MutableHandle<JS::Value> aResponse,
                                       ErrorResult& aRv) {
  if (NS_FAILED(mResponseData->mResponseResult)) {
    MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug, ("GetResponse(none)"));
    aRv.Throw(mResponseData->mResponseResult);
    return;
  }

  switch (mResponseType) {
    case XMLHttpRequestResponseType::_empty:
    case XMLHttpRequestResponseType::Text: {
      MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug, ("GetResponse(text)"));

      JSString* str;

      if (mResponseData->mResponseText.IsEmpty()) {
        aResponse.set(JS_GetEmptyStringValue(aCx));
        return;
      }

      str = mResponseData->mResponseText.GetAsJSStringCopy(aCx);
      if (!str) {
        aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
        return;
      }

      aResponse.setString(str);
      return;
    }

    case XMLHttpRequestResponseType::Arraybuffer: {
      if (!mResponseData->mResponseArrayBufferBuilder) {
        MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
                ("GetResponse(arraybuffer, null)"));
        aResponse.setNull();
        return;
      }

      if (!mResponseArrayBufferValue) {
        MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
                ("GetResponse(arraybuffer)"));
        mResponseArrayBufferValue =
            mResponseData->mResponseArrayBufferBuilder->TakeArrayBuffer(aCx);
        if (!mResponseArrayBufferValue) {
          aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
          return;
        }
      }

      aResponse.setObject(*mResponseArrayBufferValue);
      return;
    }

    case XMLHttpRequestResponseType::Blob: {
      if (!mResponseData->mResponseBlobImpl) {
        MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
                ("GetResponse(blob, none)"));
        aResponse.setNull();
        return;
      }

      if (!mResponseBlob) {
        mResponseBlob =
            Blob::Create(GetRelevantGlobal(), mResponseData->mResponseBlobImpl);
      }

      if (!mResponseBlob ||
          !GetOrCreateDOMReflector(aCx, mResponseBlob, aResponse)) {
        MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
                ("GetResponse(blob, null)"));
        aResponse.setNull();
      } else {
        MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug, ("GetResponse(blob)"));
      }

      return;
    }

    case XMLHttpRequestResponseType::Json: {
      if (mResponseData->mResponseJSON.IsVoid()) {
        aResponse.setNull();
        MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
                ("GetResponse(json, none)"));
        return;
      }

      if (mResponseJSONValue.isUndefined()) {
        // The Unicode converter has already zapped the BOM if there was one
        JS::Rooted<JS::Value> value(aCx);
        if (!JS_ParseJSON(aCx, mResponseData->mResponseJSON.BeginReading(),
                          mResponseData->mResponseJSON.Length(), &value)) {
          JS_ClearPendingException(aCx);
          MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
                  ("GetResponse(json, null)"));
          mResponseJSONValue.setNull();
        } else {
          MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug, ("GetResponse(json)"));
          mResponseJSONValue = value;
        }

        mResponseData->mResponseJSON.Truncate();
      }

      aResponse.set(mResponseJSONValue);
      return;
    }

    default:
      MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
              ("GetResponse(invalid type)"));
      MOZ_ASSERT_UNREACHABLE("Invalid type");
      aResponse.setNull();
      return;
  }
}

void XMLHttpRequestWorker::GetResponseText(DOMString& aResponseText,
                                           ErrorResult& aRv) {
  MOZ_DIAGNOSTIC_ASSERT(mResponseData);

  if (mResponseType != XMLHttpRequestResponseType::_empty &&
      mResponseType != XMLHttpRequestResponseType::Text) {
    aRv.ThrowInvalidStateError(
        "responseText is only available if responseType is '' or 'text'.");
    return;
  }

  if (!mResponseData->mResponseText.GetAsString(aResponseText)) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }
}

void XMLHttpRequestWorker::UpdateState(
    UniquePtr<StateData>&& aStateData,
    UniquePtr<ResponseData>&& aResponseData) {
  mStateData = std::move(aStateData);

  UniquePtr<ResponseData> responseData = std::move(aResponseData);
  if (responseData) {
    MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
            ("UpdateState(readyState=%u, new response data)",
             mStateData->mReadyState));
    ResetResponseData();
    mResponseData = std::move(responseData);
  } else {
    MOZ_LOG(gXMLHttpRequestLog, LogLevel::Debug,
            ("UpdateState(readyState=%u)", mStateData->mReadyState));
  }

  XMLHttpRequest_Binding::ClearCachedResponseTextValue(this);
}

void XMLHttpRequestWorker::ResetResponseData() {
  mResponseBlob = nullptr;
  mResponseArrayBufferValue = nullptr;
  mResponseJSONValue.setUndefined();
}

}  // namespace mozilla::dom
