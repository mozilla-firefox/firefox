/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/EventSource.h"

#include "ReferrerInfo.h"
#include "mozilla/Components.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/DataMutex.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Encoding.h"
#include "mozilla/GlobalFreezeObserver.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/Try.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/EventSourceBinding.h"
#include "mozilla/dom/EventSourceEventService.h"
#include "mozilla/dom/MessageEvent.h"
#include "mozilla/dom/MessageEventBinding.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/ServiceWorkerDescriptor.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/dom/WorkerScope.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsError.h"
#include "nsGlobalWindowInner.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIAuthPrompt.h"
#include "nsIAuthPrompt2.h"
#include "nsIConsoleService.h"
#include "nsIHttpChannel.h"
#include "nsIInputStream.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIPromptFactory.h"
#include "nsIScriptError.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsIStringBundle.h"
#include "nsIThreadRetargetableRequest.h"
#include "nsIThreadRetargetableStreamListener.h"
#include "nsIWindowWatcher.h"
#include "nsJSUtils.h"
#include "nsMimeTypes.h"
#include "nsNetUtil.h"
#include "nsPresContext.h"
#include "nsProxyRelease.h"
#include "nsWrapperCacheInlines.h"
#include "xpcpublic.h"

namespace mozilla::dom {

#ifdef DEBUG
static LazyLogModule gEventSourceLog("EventSource");
#endif

#define SPACE_CHAR (char16_t)0x0020
#define CR_CHAR (char16_t)0x000D
#define LF_CHAR (char16_t)0x000A
#define COLON_CHAR (char16_t)0x003A

// Reconnection time related values in milliseconds. The default one is equal
// to the default value of the pref dom.serverEvents.defaultReconnectionTime
#define MIN_RECONNECTION_TIME_VALUE 500
#define DEFAULT_RECONNECTION_TIME_VALUE 5000
#define MAX_RECONNECTION_TIME_VALUE \
  PR_IntervalToMilliseconds(DELAY_INTERVAL_LIMIT)

class EventSourceImpl final : public nsIChannelEventSink,
                              public nsIInterfaceRequestor,
                              public nsISerialEventTarget,
                              public nsITimerCallback,
                              public nsINamed,
                              public nsIThreadRetargetableStreamListener,
                              public GlobalTeardownObserver,
                              public GlobalFreezeObserver {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSICHANNELEVENTSINK
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSIEVENTTARGET_FULL
  NS_DECL_NSITIMERCALLBACK
  NS_DECL_NSINAMED
  NS_DECL_NSITHREADRETARGETABLESTREAMLISTENER

  EventSourceImpl(EventSource* aEventSource,
                  nsICookieJarSettings* aCookieJarSettings);

  enum { CONNECTING = 0U, OPEN = 1U, CLOSED = 2U };

  void Close();

  void Init(nsIGlobalObject* aWindowGlobal, nsIPrincipal* aPrincipal,
            const nsAString& aURL, ErrorResult& aRv);

  nsresult GetBaseURI(nsIURI** aBaseURI);

  void SetupHttpChannel();
  nsresult SetupReferrerInfo(const nsCOMPtr<Document>& aDocument);
  nsresult InitChannelAndRequestEventSource(bool aEventTargetAccessAllowed);
  nsresult ResetConnection();
  void ResetDecoder();
  nsresult SetReconnectionTimeout();

  void AnnounceConnection();
  void DispatchAllMessageEvents();
  nsresult RestartConnection();
  void ReestablishConnection();
  void DispatchFailConnection();
  void FailConnection();

  void DisconnectFromOwner() override {
    Close();
    GlobalTeardownObserver::DisconnectFromOwner();
  }
  void FrozenCallback(nsIGlobalObject* aGlobal) override {
    DebugOnly<nsresult> rv = Freeze();
    MOZ_ASSERT(NS_SUCCEEDED(rv), "Freeze() failed");
  }
  void ThawedCallback(nsIGlobalObject* aGlobal) override {
    DebugOnly<nsresult> rv = Thaw();
    MOZ_ASSERT(NS_SUCCEEDED(rv), "Thaw() failed");
  }

  nsresult Thaw();
  nsresult Freeze();

  nsresult PrintErrorOnConsole(const char* aBundleURI, const char* aError,
                               const nsTArray<nsString>& aFormatStrings);
  nsresult ConsoleError();

  static nsresult StreamReaderFunc(nsIInputStream* aInputStream, void* aClosure,
                                   const char* aFromRawSegment,
                                   uint32_t aToOffset, uint32_t aCount,
                                   uint32_t* aWriteCount);
  void ParseSegment(const char* aBuffer, uint32_t aLength);
  nsresult SetFieldAndClear();
  void ClearFields();
  nsresult ResetEvent();
  nsresult DispatchCurrentMessageEvent();
  nsresult ParseCharacter(char16_t aChr);
  nsresult CheckHealthOfRequestCallback(nsIRequest* aRequestCallback);
  nsresult OnRedirectVerifyCallback(nsresult result);
  nsresult ParseURL(const nsAString& aURL);
  nsresult AddGlobalObservers(nsIGlobalObject* aGlobal);
  void RemoveWindowObservers();

  void CloseInternal();
  void CleanupOnMainThread();

  bool CreateWorkerRef(WorkerPrivate* aWorkerPrivate);
  void ReleaseWorkerRef();

  void AssertIsOnTargetThread() const {
    MOZ_DIAGNOSTIC_ASSERT(IsTargetThread());
  }

  bool IsTargetThread() const { return NS_GetCurrentThread() == mTargetThread; }

  bool DispatchesToMainThread() const { return mIsMainThread || mIsShutDown; }

  uint16_t ReadyState() {
    auto lock = mSharedData.Lock();
    if (lock->mEventSource) {
      return lock->mEventSource->mReadyState;
    }
    // EventSourceImpl keeps EventSource alive. If mEventSource is null, it
    // means that the EventSource has been closed.
    return CLOSED;
  }

  void SetReadyState(uint16_t aReadyState) {
    auto lock = mSharedData.Lock();
    MOZ_ASSERT(lock->mEventSource);
    MOZ_ASSERT(!mIsShutDown);
    lock->mEventSource->mReadyState = aReadyState;
  }

  bool IsClosed() { return ReadyState() == CLOSED; }

  RefPtr<EventSource> GetEventSource() {
    AssertIsOnTargetThread();
    auto lock = mSharedData.Lock();
    return lock->mEventSource;
  }

  /**
   * A simple state machine used to manage the event-source's line buffer
   *
   * PARSE_STATE_OFF              -> PARSE_STATE_BEGIN_OF_STREAM
   *
   * PARSE_STATE_BEGIN_OF_STREAM     -> PARSE_STATE_CR_CHAR |
   *                                 PARSE_STATE_BEGIN_OF_LINE |
   *                                 PARSE_STATE_COMMENT |
   *                                 PARSE_STATE_FIELD_NAME
   *
   * PARSE_STATE_CR_CHAR -> PARSE_STATE_CR_CHAR |
   *                        PARSE_STATE_COMMENT |
   *                        PARSE_STATE_FIELD_NAME |
   *                        PARSE_STATE_BEGIN_OF_LINE
   *
   * PARSE_STATE_COMMENT -> PARSE_STATE_CR_CHAR |
   *                        PARSE_STATE_BEGIN_OF_LINE
   *
   * PARSE_STATE_FIELD_NAME   -> PARSE_STATE_CR_CHAR |
   *                             PARSE_STATE_BEGIN_OF_LINE |
   *                             PARSE_STATE_FIRST_CHAR_OF_FIELD_VALUE
   *
   * PARSE_STATE_FIRST_CHAR_OF_FIELD_VALUE  -> PARSE_STATE_FIELD_VALUE |
   *                                           PARSE_STATE_CR_CHAR |
   *                                           PARSE_STATE_BEGIN_OF_LINE
   *
   * PARSE_STATE_FIELD_VALUE      -> PARSE_STATE_CR_CHAR |
   *                                 PARSE_STATE_BEGIN_OF_LINE
   *
   * PARSE_STATE_BEGIN_OF_LINE   -> PARSE_STATE_CR_CHAR |
   *                                PARSE_STATE_COMMENT |
   *                                PARSE_STATE_FIELD_NAME |
   *                                PARSE_STATE_BEGIN_OF_LINE
   *
   * Whenever the parser find an empty line or the end-of-file
   * it dispatches the stacked event.
   *
   */
  enum ParserStatus {
    PARSE_STATE_OFF = 0,
    PARSE_STATE_BEGIN_OF_STREAM,
    PARSE_STATE_CR_CHAR,
    PARSE_STATE_COMMENT,
    PARSE_STATE_FIELD_NAME,
    PARSE_STATE_FIRST_CHAR_OF_FIELD_VALUE,
    PARSE_STATE_FIELD_VALUE,
    PARSE_STATE_IGNORE_FIELD_VALUE,
    PARSE_STATE_BEGIN_OF_LINE
  };

  // Connection related data members. Should only be accessed on main thread.
  nsCOMPtr<nsIURI> mSrc;
  uint32_t mReconnectionTime;  // in ms
  nsCOMPtr<nsIPrincipal> mPrincipal;
  Maybe<ClientInfo> mClientInfo;
  Maybe<ServiceWorkerDescriptor> mController;
  nsCOMPtr<nsITimer> mTimer;
  nsCOMPtr<nsIHttpChannel> mHttpChannel;

  struct Message {
    nsString mEventName;
    // We need to be able to distinguish between different states of id field:
    // 1) is not given at all
    // 2) is given but is empty
    // 3) is given and has a value
    // We can't check for the 1st state with a simple nsString.
    Maybe<nsString> mLastEventID;
    nsString mData;
    // Origin of the connection this message was parsed from. Copied per
    // message because a reconnect can change it while older messages are
    // still queued.
    nsString mOrigin;
  };

  // Message related data members. May be set / initialized when initializing
  // EventSourceImpl on target thread but should only be used on target thread.
  nsString mLastEventID;
  UniquePtr<Message> mCurrentMessage;
  nsDeque<Message> mMessagesToDispatch;
  ParserStatus mStatus;
  mozilla::UniquePtr<mozilla::Decoder> mUnicodeDecoder;
  nsString mLastFieldName;
  nsString mLastFieldValue;

  // EventSourceImpl internal states.
  // WorkerRef to keep the worker alive.
  DataMutex<RefPtr<ThreadSafeWorkerRef>> mWorkerRef;

  // Whether the window is frozen. May be set on main thread and read on target
  // thread.
  Atomic<bool> mFrozen;
  // There are some messages are going to be dispatched when thaw.
  bool mGoingToDispatchAllMessages;
  // Whether the EventSource is run on main thread.
  const bool mIsMainThread;
  // Whether the EventSourceImpl is going to be destroyed.
  Atomic<bool> mIsShutDown;

  class EventSourceServiceNotifier final {
   public:
    EventSourceServiceNotifier(RefPtr<EventSourceImpl>&& aEventSourceImpl,
                               uint64_t aHttpChannelId, uint64_t aInnerWindowID)
        : mEventSourceImpl(std::move(aEventSourceImpl)),
          mHttpChannelId(aHttpChannelId),
          mInnerWindowID(aInnerWindowID),
          mConnectionOpened(false) {
      AssertIsOnMainThread();
      mService = EventSourceEventService::GetOrCreate();
    }

    void ConnectionOpened() {
      mEventSourceImpl->AssertIsOnTargetThread();
      mService->EventSourceConnectionOpened(mHttpChannelId, mInnerWindowID);
      mConnectionOpened = true;
    }

    void EventReceived(const nsAString& aEventName,
                       const nsAString& aLastEventID, const nsAString& aData,
                       uint32_t aRetry, DOMHighResTimeStamp aTimeStamp) {
      mEventSourceImpl->AssertIsOnTargetThread();
      mService->EventReceived(mHttpChannelId, mInnerWindowID, aEventName,
                              aLastEventID, aData, aRetry, aTimeStamp);
    }

    ~EventSourceServiceNotifier() {
      // It is safe to call this on any thread because
      // EventSourceConnectionClosed method is thread safe and
      // NS_ReleaseOnMainThread explicitly releases the service on the main
      // thread.
      if (mConnectionOpened) {
        // We want to notify about connection being closed only if we told
        // it was ever opened. The check is needed if OnStartRequest is called
        // on the main thread while close() is called on a worker thread.
        mService->EventSourceConnectionClosed(mHttpChannelId, mInnerWindowID);
      }
      NS_ReleaseOnMainThread("EventSourceServiceNotifier::mService",
                             mService.forget());
    }

   private:
    RefPtr<EventSourceEventService> mService;
    RefPtr<EventSourceImpl> mEventSourceImpl;
    uint64_t mHttpChannelId;
    uint64_t mInnerWindowID;
    bool mConnectionOpened;
  };

  struct SharedData {
    RefPtr<EventSource> mEventSource;
    UniquePtr<EventSourceServiceNotifier> mServiceNotifier;
    // Origin of the current connection's final URL, i.e. after redirects.
    nsString mOrigin;
  };

  DataMutex<SharedData> mSharedData;

  // Event Source owner information:
  // - the script file name
  // - source code line number and column number where the Event Source object
  //   was constructed.
  // - the ID of the inner window where the script lives. Note that this may not
  //   be the same as the Event Source owner window.
  // These attributes are used for error reporting. Should only be accessed on
  // target thread
  JSCallingLocation mCallingLocation;
  uint64_t mInnerWindowID;

  // prevent bad usage
  EventSourceImpl(const EventSourceImpl& x) = delete;
  EventSourceImpl& operator=(const EventSourceImpl& x) = delete;

 private:
  nsCOMPtr<nsICookieJarSettings> mCookieJarSettings;

  // Pointer to the target thread for checking whether we are
  // on the target thread. This is intentionally a non-owning
  // pointer in order not to affect the thread destruction
  // sequence. This pointer must only be compared for equality
  // and must not be dereferenced.
  nsIThread* mTargetThread;

  ~EventSourceImpl() {
    if (IsClosed()) {
      return;
    }
    // If we threw during Init we never called Close
    SetReadyState(CLOSED);
    CloseInternal();
  }
};

NS_IMPL_ISUPPORTS(EventSourceImpl, nsIStreamListener, nsIRequestObserver,
                  nsIChannelEventSink, nsIInterfaceRequestor,
                  nsISerialEventTarget, nsIEventTarget,
                  nsIThreadRetargetableStreamListener, nsITimerCallback,
                  nsINamed)

EventSourceImpl::EventSourceImpl(EventSource* aEventSource,
                                 nsICookieJarSettings* aCookieJarSettings)
    : mReconnectionTime(0),
      mStatus(PARSE_STATE_OFF),
      mWorkerRef(nullptr, "EventSourceImpl::mWorkerRef"),
      mFrozen(false),
      mGoingToDispatchAllMessages(false),
      mIsMainThread(NS_IsMainThread()),
      mIsShutDown(false),
      mSharedData(SharedData{aEventSource}, "EventSourceImpl::mSharedData"),
      mInnerWindowID(0),
      mCookieJarSettings(aCookieJarSettings),
      mTargetThread(NS_GetCurrentThread()) {
  MOZ_ASSERT(aEventSource);
  SetReadyState(CONNECTING);
}

class CleanupRunnable final : public WorkerMainThreadRunnable {
 public:
  explicit CleanupRunnable(RefPtr<EventSourceImpl>&& aEventSourceImpl)
      : WorkerMainThreadRunnable(GetCurrentThreadWorkerPrivate(),
                                 "EventSource :: Cleanup"_ns),
        mESImpl(std::move(aEventSourceImpl)) {
    MOZ_ASSERT(mESImpl);
  }

  bool MainThreadRun() override {
    MOZ_ASSERT(mESImpl);
    mESImpl->CleanupOnMainThread();
    // We want to ensure the shortest possible remaining lifetime
    // and not depend on the Runnable's destruction.
    mESImpl = nullptr;
    return true;
  }

 protected:
  RefPtr<EventSourceImpl> mESImpl;
};

void EventSourceImpl::Close() {
  if (IsClosed()) {
    return;
  }

  SetReadyState(CLOSED);
  // CloseInternal potentially kills ourself, ensure
  // to not access any members afterwards.
  CloseInternal();
}

void EventSourceImpl::CloseInternal() {
  AssertIsOnTargetThread();
  MOZ_ASSERT(IsClosed());

  RefPtr<EventSource> myES;
  {
    auto lock = mSharedData.Lock();
    // We want to ensure to release ourself even if we have
    // the shutdown case, thus we put aside a pointer
    // to the EventSource and null it out right now.
    myES = std::move(lock->mEventSource);
    lock->mEventSource = nullptr;
    lock->mServiceNotifier = nullptr;
  }

  MOZ_ASSERT(!mIsShutDown);
  if (mIsShutDown) {
    return;
  }

  // Invoke CleanupOnMainThread before cleaning any members. It will call
  // ShutDown, which is supposed to be called before cleaning any members.
  if (NS_IsMainThread()) {
    CleanupOnMainThread();
  } else {
    ErrorResult rv;
    // run CleanupOnMainThread synchronously on main thread since it touches
    // observers and members only can be accessed on main thread.
    RefPtr<CleanupRunnable> runnable = new CleanupRunnable(this);
    runnable->Dispatch(GetCurrentThreadWorkerPrivate(), Killing, rv);
    MOZ_ASSERT(!rv.Failed());
    ReleaseWorkerRef();
  }

  while (mMessagesToDispatch.GetSize() != 0) {
    delete mMessagesToDispatch.PopFront();
  }
  mFrozen = false;
  ResetDecoder();
  mUnicodeDecoder = nullptr;
  // Release the object on its owner. Don't access to any members
  // after it.
  myES->mESImpl = nullptr;
}

void EventSourceImpl::CleanupOnMainThread() {
  AssertIsOnMainThread();
  MOZ_ASSERT(IsClosed());

  MOZ_ASSERT(!mIsShutDown);
  // Cancel first: once mIsShutDown is set, Dispatch() forwards channel events
  // to the main thread.
  ResetConnection();
  mIsShutDown = true;

  if (mIsMainThread) {
    RemoveWindowObservers();
  }

  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }

  mPrincipal = nullptr;
  mSrc = nullptr;
}

class ConnectRunnable final : public WorkerMainThreadRunnable {
 public:
  explicit ConnectRunnable(WorkerPrivate* aWorkerPrivate,
                           RefPtr<EventSourceImpl> aEventSourceImpl)
      : WorkerMainThreadRunnable(aWorkerPrivate, "EventSource :: Connect"_ns),
        mESImpl(std::move(aEventSourceImpl)) {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(mESImpl);
  }

  bool MainThreadRun() override {
    MOZ_ASSERT(mESImpl);
    // We are allowed to access the event target since this runnable is
    // synchronized with the thread the event target lives on.
    mESImpl->InitChannelAndRequestEventSource(true);
    // We want to ensure the shortest possible remaining lifetime
    // and not depend on the Runnable's destruction.
    mESImpl = nullptr;
    return true;
  }

 private:
  RefPtr<EventSourceImpl> mESImpl;
};

nsresult EventSourceImpl::ParseURL(const nsAString& aURL) {
  MOZ_ASSERT(!mIsShutDown);
  // get the src
  nsCOMPtr<nsIURI> baseURI;
  nsresult rv = GetBaseURI(getter_AddRefs(baseURI));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> srcURI;
  nsCOMPtr<Document> doc =
      mIsMainThread ? GetEventSource()->GetDocumentIfCurrent() : nullptr;
  if (doc) {
    rv = NS_NewURI(getter_AddRefs(srcURI), aURL, doc->GetDocumentCharacterSet(),
                   baseURI);
  } else {
    rv = NS_NewURI(getter_AddRefs(srcURI), aURL, nullptr, baseURI);
  }

  NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_SYNTAX_ERR);

  nsAutoCString spec;
  rv = srcURI->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  // This assignment doesn't require extra synchronization because this function
  // is only ever called from EventSourceImpl::Init(), which is either called
  // directly if mEventSource was created on the main thread, or via a
  // synchronous runnable if it was created on a worker thread.
  {
    // We can't use GetEventSource() here because it would modify the refcount,
    // and that's not allowed off the owning thread.
    auto lock = mSharedData.Lock();
    lock->mEventSource->mOriginalURL = NS_ConvertUTF8toUTF16(spec);
  }
  mSrc = std::move(srcURI);
  return NS_OK;
}

nsresult EventSourceImpl::AddGlobalObservers(nsIGlobalObject* aGlobal) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mIsMainThread);
  MOZ_ASSERT(!mIsShutDown);

  GlobalTeardownObserver::BindToGlobal(aGlobal);
  GlobalFreezeObserver::BindToGlobal(aGlobal);

  return NS_OK;
}

void EventSourceImpl::RemoveWindowObservers() {
  AssertIsOnMainThread();
  MOZ_ASSERT(mIsMainThread);
  MOZ_ASSERT(IsClosed());
  GlobalTeardownObserver::DisconnectFromOwner();
  DisconnectFreezeObserver();
}

void EventSourceImpl::Init(nsIGlobalObject* aWindowGlobal,
                           nsIPrincipal* aPrincipal, const nsAString& aURL,
                           ErrorResult& aRv) {
  // aWindowGlobal should only exist for main-thread EventSource
  MOZ_ASSERT_IF(aWindowGlobal, mIsMainThread);
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(ReadyState() == CONNECTING);
  mPrincipal = aPrincipal;
  aRv = ParseURL(aURL);
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }
  // The conditional here is historical and not necessarily sane.
  if (JSContext* cx = nsContentUtils::GetCurrentJSContext()) {
    mCallingLocation = JSCallingLocation::Get();
    if (mIsMainThread) {
      mInnerWindowID = nsJSUtils::GetCurrentlyRunningCodeInnerWindowID(cx);
    }
  }

  if (mIsMainThread) {
    // We observe when the window freezes and thaws.
    //
    // This will store raw pointer of us in GTO, which sounds scary as the
    // object can be accessed cross thread. But it should be safe as GTO and
    // EventSourceImpl lifetime should be managed in the same main thread.
    //
    // XXX(krosylight): But what about workers? See bug 1910585.
    aRv = AddGlobalObservers(aWindowGlobal);
    if (NS_WARN_IF(aRv.Failed())) {
      return;
    }
  }

  mReconnectionTime = StaticPrefs::dom_serverEvents_defaultReconnectionTime();
  if (!mReconnectionTime) {
    mReconnectionTime = DEFAULT_RECONNECTION_TIME_VALUE;
  }

  mUnicodeDecoder = UTF_8_ENCODING->NewDecoderWithBOMRemoval();
}

//-----------------------------------------------------------------------------
// EventSourceImpl::nsIStreamListener
//-----------------------------------------------------------------------------

NS_IMETHODIMP
EventSourceImpl::OnStartRequest(nsIRequest* aRequest) {
  AssertIsOnMainThread();
  if (IsClosed()) {
    return NS_ERROR_ABORT;
  }
  nsresult rv = CheckHealthOfRequestCallback(aRequest);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aRequest, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsresult status;
  rv = aRequest->GetStatus(&status);
  NS_ENSURE_SUCCESS(rv, rv);

  if (NS_FAILED(status)) {
    // EventSource::OnStopRequest will evaluate if it shall either reestablish
    // or fail the connection, based on the status.
    return status;
  }

  uint32_t httpStatus;
  rv = httpChannel->GetResponseStatus(&httpStatus);
  NS_ENSURE_SUCCESS(rv, rv);

  if (httpStatus != 200) {
    DispatchFailConnection();
    return NS_ERROR_ABORT;
  }

  nsAutoCString contentType;
  rv = httpChannel->GetContentType(contentType);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!contentType.EqualsLiteral(TEXT_EVENT_STREAM)) {
    DispatchFailConnection();
    return NS_ERROR_ABORT;
  }

  if (!mIsMainThread) {
    // Try to retarget to worker thread, otherwise fall back to main thread.
    nsCOMPtr<nsIThreadRetargetableRequest> rr = do_QueryInterface(httpChannel);
    if (rr) {
      rv = rr->RetargetDeliveryTo(this);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        NS_WARNING("Retargeting failed");
      }
    }
  }

  nsCOMPtr<nsIURI> finalURI;
  rv = NS_GetFinalChannelURI(httpChannel, getter_AddRefs(finalURI));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString origin;
  rv = nsContentUtils::GetWebExposedOriginSerialization(finalURI, origin);
  NS_ENSURE_SUCCESS(rv, rv);

  {
    auto lock = mSharedData.Lock();
    lock->mOrigin = origin;
    lock->mServiceNotifier = std::make_unique<EventSourceServiceNotifier>(
        this, mHttpChannel->ChannelId(), mInnerWindowID);
  }
  rv = Dispatch(NewRunnableMethod("dom::EventSourceImpl::AnnounceConnection",
                                  this, &EventSourceImpl::AnnounceConnection),
                NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);
  mStatus = PARSE_STATE_BEGIN_OF_STREAM;
  return NS_OK;
}

// this method parses the characters as they become available instead of
// buffering them.
nsresult EventSourceImpl::StreamReaderFunc(nsIInputStream* aInputStream,
                                           void* aClosure,
                                           const char* aFromRawSegment,
                                           uint32_t aToOffset, uint32_t aCount,
                                           uint32_t* aWriteCount) {
  // The EventSourceImpl instance is hold alive on the
  // synchronously calling stack, so raw pointer is fine here.
  EventSourceImpl* thisObject = static_cast<EventSourceImpl*>(aClosure);
  if (!thisObject || !aWriteCount) {
    NS_WARNING(
        "EventSource cannot read from stream: no aClosure or aWriteCount");
    return NS_ERROR_FAILURE;
  }
  thisObject->AssertIsOnTargetThread();
  MOZ_ASSERT(!thisObject->mIsShutDown);
  thisObject->ParseSegment((const char*)aFromRawSegment, aCount);
  *aWriteCount = aCount;
  return NS_OK;
}

void EventSourceImpl::ParseSegment(const char* aBuffer, uint32_t aLength) {
  AssertIsOnTargetThread();
  if (IsClosed()) {
    return;
  }
  char16_t buffer[1024];
  auto dst = Span(buffer);
  auto src = AsBytes(Span(aBuffer, aLength));
  // XXX EOF handling is https://bugzilla.mozilla.org/show_bug.cgi?id=1369018
  for (;;) {
    uint32_t result;
    size_t read;
    size_t written;
    std::tie(result, read, written, std::ignore) =
        mUnicodeDecoder->DecodeToUTF16(src, dst, false);
    for (auto c : dst.To(written)) {
      nsresult rv = ParseCharacter(c);
      NS_ENSURE_SUCCESS_VOID(rv);
    }
    if (result == kInputEmpty) {
      return;
    }
    src = src.From(read);
  }
}

NS_IMETHODIMP
EventSourceImpl::OnDataAvailable(nsIRequest* aRequest,
                                 nsIInputStream* aInputStream, uint64_t aOffset,
                                 uint32_t aCount) {
  AssertIsOnTargetThread();
  NS_ENSURE_ARG_POINTER(aInputStream);
  if (IsClosed()) {
    return NS_ERROR_ABORT;
  }

  nsresult rv = CheckHealthOfRequestCallback(aRequest);
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t totalRead;
  return aInputStream->ReadSegments(EventSourceImpl::StreamReaderFunc, this,
                                    aCount, &totalRead);
}

NS_IMETHODIMP
EventSourceImpl::OnStopRequest(nsIRequest* aRequest, nsresult aStatusCode) {
  AssertIsOnMainThread();

  if (IsClosed()) {
    return NS_ERROR_ABORT;
  }
  MOZ_ASSERT(mSrc);
  // "Network errors that prevents the connection from being established in the
  //  first place (e.g. DNS errors), must cause the user agent to asynchronously
  //  reestablish the connection.
  //
  //  (...) the cancelation of the fetch algorithm by the user agent (e.g. in
  //  response to window.stop() or the user canceling the network connection
  //  manually) must cause the user agent to fail the connection.
  // There could be additional network errors that are not covered in the above
  // checks
  //  See Bug 1808511
  if (aStatusCode == NS_BINDING_ABORTED) {
    nsAutoCString cancelReason;
    if (mHttpChannel) {
      mHttpChannel->GetCanceledReason(cancelReason);
    }
    if (cancelReason.EqualsLiteral("navigation")) {
      nsresult rv = Dispatch(NewRunnableMethod("dom::EventSourceImpl::Close",
                                               this, &EventSourceImpl::Close),
                             NS_DISPATCH_NORMAL);
      NS_ENSURE_SUCCESS(rv, rv);
    } else {
      // window.stop() or manual cancellation: fail the connection per spec
      // (fires onerror + CLOSED) but suppress the console error.
      nsresult rv =
          Dispatch(NewRunnableMethod("dom::EventSourceImpl::FailConnection",
                                     this, &EventSourceImpl::FailConnection),
                   NS_DISPATCH_NORMAL);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    return NS_OK;
  }
  if (NS_FAILED(aStatusCode) && aStatusCode != NS_ERROR_CONNECTION_REFUSED &&
      aStatusCode != NS_ERROR_NET_TIMEOUT &&
      aStatusCode != NS_ERROR_NET_RESET &&
      aStatusCode != NS_ERROR_NET_INTERRUPT &&
      aStatusCode != NS_ERROR_NET_PARTIAL_TRANSFER &&
      aStatusCode != NS_ERROR_NET_TIMEOUT_EXTERNAL &&
      aStatusCode != NS_ERROR_PROXY_CONNECTION_REFUSED &&
      aStatusCode != NS_ERROR_DNS_LOOKUP_QUEUE_FULL &&
      aStatusCode != NS_ERROR_INVALID_CONTENT_ENCODING) {
    DispatchFailConnection();
    return NS_ERROR_ABORT;
  }

  nsresult rv = CheckHealthOfRequestCallback(aRequest);
  NS_ENSURE_SUCCESS(rv, rv);

  rv =
      Dispatch(NewRunnableMethod("dom::EventSourceImpl::ReestablishConnection",
                                 this, &EventSourceImpl::ReestablishConnection),
               NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

//-----------------------------------------------------------------------------
// EventSourceImpl::nsIChannelEventSink
//-----------------------------------------------------------------------------

NS_IMETHODIMP
EventSourceImpl::AsyncOnChannelRedirect(
    nsIChannel* aOldChannel, nsIChannel* aNewChannel, uint32_t aFlags,
    nsIAsyncVerifyRedirectCallback* aCallback) {
  AssertIsOnMainThread();
  if (IsClosed()) {
    return NS_ERROR_ABORT;
  }
  nsCOMPtr<nsIRequest> aOldRequest = aOldChannel;
  MOZ_ASSERT(aOldRequest, "Redirect from a null request?");

  nsresult rv = CheckHealthOfRequestCallback(aOldRequest);
  NS_ENSURE_SUCCESS(rv, rv);

  MOZ_ASSERT(aNewChannel, "Redirect without a channel?");

  nsCOMPtr<nsIURI> newURI;
  rv = NS_GetFinalChannelURI(aNewChannel, getter_AddRefs(newURI));
  NS_ENSURE_SUCCESS(rv, rv);

  bool isValidScheme = net::SchemeIsHttpOrHttps(newURI);

  rv =
      mIsMainThread ? GetEventSource()->CheckCurrentGlobalCorrectness() : NS_OK;
  if (NS_FAILED(rv) || !isValidScheme) {
    DispatchFailConnection();
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  // update our channel

  mHttpChannel = do_QueryInterface(aNewChannel);
  NS_ENSURE_STATE(mHttpChannel);

  SetupHttpChannel();
  // The HTTP impl already copies over the referrer info on
  // redirects, so we don't need to SetupReferrerInfo().

  if ((aFlags & nsIChannelEventSink::REDIRECT_PERMANENT) != 0) {
    rv = NS_GetFinalChannelURI(mHttpChannel, getter_AddRefs(mSrc));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  aCallback->OnRedirectVerifyCallback(NS_OK);

  return NS_OK;
}

//-----------------------------------------------------------------------------
// EventSourceImpl::nsIInterfaceRequestor
//-----------------------------------------------------------------------------

NS_IMETHODIMP
EventSourceImpl::GetInterface(const nsIID& aIID, void** aResult) {
  AssertIsOnMainThread();

  if (IsClosed()) {
    return NS_ERROR_FAILURE;
  }

  if (aIID.Equals(NS_GET_IID(nsIChannelEventSink))) {
    *aResult = static_cast<nsIChannelEventSink*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }

  if (aIID.Equals(NS_GET_IID(nsIAuthPrompt)) ||
      aIID.Equals(NS_GET_IID(nsIAuthPrompt2))) {
    nsresult rv;
    nsCOMPtr<nsIPromptFactory> wwatch =
        do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsPIDOMWindowOuter> window;

    // To avoid a data race we may only access the event target if it lives on
    // the main thread.
    if (mIsMainThread) {
      auto lock = mSharedData.Lock();
      rv = lock->mEventSource->CheckCurrentGlobalCorrectness();
      NS_ENSURE_SUCCESS(rv, NS_ERROR_UNEXPECTED);

      if (nsGlobalWindowInner* win = lock->mEventSource->GetOwnerWindow()) {
        window = win->GetOuterWindow();
      }
    }

    // Get the an auth prompter for our window so that the parenting
    // of the dialogs works as it should when using tabs.

    return wwatch->GetPrompt(window, aIID, aResult);
  }

  return QueryInterface(aIID, aResult);
}

NS_IMETHODIMP
EventSourceImpl::IsOnCurrentThread(bool* aResult) {
  *aResult = IsOnCurrentThreadInfallible();
  return NS_OK;
}

NS_IMETHODIMP_(bool)
EventSourceImpl::IsOnCurrentThreadInfallible() {
  return DispatchesToMainThread() ? NS_IsMainThread() : IsTargetThread();
}

nsresult EventSourceImpl::GetBaseURI(nsIURI** aBaseURI) {
  MOZ_ASSERT(!mIsShutDown);
  NS_ENSURE_ARG_POINTER(aBaseURI);

  *aBaseURI = nullptr;

  nsCOMPtr<nsIURI> baseURI;

  // first we try from document->GetBaseURI()
  nsCOMPtr<Document> doc =
      mIsMainThread ? GetEventSource()->GetDocumentIfCurrent() : nullptr;
  if (doc) {
    baseURI = doc->GetBaseURI();
  }

  // otherwise we get from the doc's principal
  if (!baseURI) {
    auto* basePrin = BasePrincipal::Cast(mPrincipal);
    nsresult rv = basePrin->GetURI(getter_AddRefs(baseURI));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  NS_ENSURE_STATE(baseURI);

  baseURI.forget(aBaseURI);
  return NS_OK;
}

void EventSourceImpl::SetupHttpChannel() {
  AssertIsOnMainThread();
  MOZ_ASSERT(!mIsShutDown);
  nsresult rv = mHttpChannel->SetRequestMethod("GET"_ns);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  /* set the http request headers */

  rv = mHttpChannel->SetRequestHeader(
      "Accept"_ns, nsLiteralCString(TEXT_EVENT_STREAM), false);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  // LOAD_BYPASS_CACHE already adds the Cache-Control: no-cache header

  if (mLastEventID.IsEmpty()) {
    return;
  }
  NS_ConvertUTF16toUTF8 eventId(mLastEventID);
  rv = mHttpChannel->SetRequestHeader("Last-Event-ID"_ns, eventId, false);
#ifdef DEBUG
  if (NS_FAILED(rv)) {
    MOZ_LOG(gEventSourceLog, LogLevel::Warning,
            ("SetupHttpChannel. rv=%x (%s)", uint32_t(rv), eventId.get()));
  }
#endif
  (void)rv;
}

nsresult EventSourceImpl::SetupReferrerInfo(
    const nsCOMPtr<Document>& aDocument) {
  AssertIsOnMainThread();
  MOZ_ASSERT(!mIsShutDown);

  if (aDocument) {
    auto referrerInfo = MakeRefPtr<ReferrerInfo>(*aDocument);
    nsresult rv = mHttpChannel->SetReferrerInfoWithoutClone(referrerInfo);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult EventSourceImpl::InitChannelAndRequestEventSource(
    const bool aEventTargetAccessAllowed) {
  AssertIsOnMainThread();
  if (IsClosed()) {
    return NS_ERROR_ABORT;
  }

  bool isValidScheme = net::SchemeIsHttpOrHttps(mSrc);

  MOZ_ASSERT_IF(mIsMainThread, aEventTargetAccessAllowed);

  nsresult rv = aEventTargetAccessAllowed ? [this]() {
    // We can't call GetEventSource() because we're not
    // allowed to touch the refcount off the worker thread
    // due to an assertion, event if it would have otherwise
    // been safe.
    auto lock = mSharedData.Lock();
    return lock->mEventSource->CheckCurrentGlobalCorrectness();
  }()
                                          : NS_OK;
  if (NS_FAILED(rv) || !isValidScheme) {
    DispatchFailConnection();
    return NS_ERROR_DOM_SECURITY_ERR;
  }

  nsCOMPtr<Document> doc;
  nsSecurityFlags securityFlags =
      nsILoadInfo::SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT;
  {
    auto lock = mSharedData.Lock();
    doc = aEventTargetAccessAllowed ? lock->mEventSource->GetDocumentIfCurrent()
                                    : nullptr;

    if (lock->mEventSource->mWithCredentials) {
      securityFlags |= nsILoadInfo::SEC_COOKIES_INCLUDE;
    }
  }

  // The html spec requires we use fetch cache mode of "no-store".  This
  // maps to LOAD_BYPASS_CACHE and LOAD_INHIBIT_CACHING in necko.
  nsLoadFlags loadFlags;
  loadFlags = nsIRequest::LOAD_BACKGROUND | nsIRequest::LOAD_BYPASS_CACHE |
              nsIRequest::INHIBIT_CACHING;

  nsCOMPtr<nsIChannel> channel;
  // If we have the document, use it
  if (doc) {
    MOZ_ASSERT(mCookieJarSettings == doc->CookieJarSettings());

    nsCOMPtr<nsILoadGroup> loadGroup = doc->GetDocumentLoadGroup();
    rv = NS_NewChannel(getter_AddRefs(channel), mSrc, doc, securityFlags,
                       nsIContentPolicy::TYPE_INTERNAL_EVENTSOURCE,
                       nullptr,  // aPerformanceStorage
                       loadGroup,
                       nullptr,     // aCallbacks
                       loadFlags);  // aLoadFlags
  } else if (mClientInfo.isSome()) {
    // Use the ClientInfo overload so the channel is associated with the
    // correct client (e.g. the worker global that created this EventSource).
    rv = NS_NewChannel(getter_AddRefs(channel), mSrc, mPrincipal,
                       mClientInfo.ref(), mController, securityFlags,
                       nsIContentPolicy::TYPE_INTERNAL_EVENTSOURCE,
                       mCookieJarSettings,
                       nullptr,     // aPerformanceStorage
                       nullptr,     // loadGroup
                       nullptr,     // aCallbacks
                       loadFlags);  // aLoadFlags
    NS_ENSURE_SUCCESS(rv, rv);

    auto workerRef = mWorkerRef.Lock();

    if (*workerRef) {
      nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
      loadInfo->SetIsInThirdPartyContext(
          (*workerRef)->Private()->IsThirdPartyContext());
    }
  } else {
    // otherwise use the principal
    rv = NS_NewChannel(getter_AddRefs(channel), mSrc, mPrincipal, securityFlags,
                       nsIContentPolicy::TYPE_INTERNAL_EVENTSOURCE,
                       mCookieJarSettings,
                       nullptr,     // aPerformanceStorage
                       nullptr,     // loadGroup
                       nullptr,     // aCallbacks
                       loadFlags);  // aLoadFlags
  }

  NS_ENSURE_SUCCESS(rv, rv);

  mHttpChannel = do_QueryInterface(channel);
  NS_ENSURE_TRUE(mHttpChannel, NS_ERROR_NO_INTERFACE);

  SetupHttpChannel();
  rv = SetupReferrerInfo(doc);
  NS_ENSURE_SUCCESS(rv, rv);

#ifdef DEBUG
  {
    nsCOMPtr<nsIInterfaceRequestor> notificationCallbacks;
    mHttpChannel->GetNotificationCallbacks(
        getter_AddRefs(notificationCallbacks));
    MOZ_ASSERT(!notificationCallbacks);
  }
#endif

  mHttpChannel->SetNotificationCallbacks(this);

  // Start reading from the channel
  rv = mHttpChannel->AsyncOpen(this);
  if (NS_FAILED(rv)) {
    DispatchFailConnection();
    return rv;
  }

  return rv;
}

void EventSourceImpl::AnnounceConnection() {
  AssertIsOnTargetThread();
  if (ReadyState() != CONNECTING) {
    NS_WARNING("Unexpected mReadyState!!!");
    return;
  }

  {
    auto lock = mSharedData.Lock();
    if (lock->mServiceNotifier) {
      lock->mServiceNotifier->ConnectionOpened();
    }
  }

  // When a user agent is to announce the connection, the user agent must set
  // the readyState attribute to OPEN and queue a task to fire a simple event
  // named open at the EventSource object.

  SetReadyState(OPEN);

  nsresult rv = GetEventSource()->CheckCurrentGlobalCorrectness();
  if (NS_FAILED(rv)) {
    return;
  }
  // We can't hold the mutex while dispatching the event because the mutex is
  // not reentrant, and content might call back into our code.
  rv = GetEventSource()->CreateAndDispatchSimpleEvent(u"open"_ns);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to dispatch the error event!!!");
    return;
  }
}

nsresult EventSourceImpl::ResetConnection() {
  AssertIsOnMainThread();
  if (mHttpChannel) {
    mHttpChannel->Cancel(NS_ERROR_ABORT);
    mHttpChannel = nullptr;
  }
  return NS_OK;
}

void EventSourceImpl::ResetDecoder() {
  AssertIsOnTargetThread();
  if (mUnicodeDecoder) {
    UTF_8_ENCODING->NewDecoderWithBOMRemovalInto(*mUnicodeDecoder);
  }
  mStatus = PARSE_STATE_OFF;
  ClearFields();
}

class CallRestartConnection final : public WorkerMainThreadRunnable {
 public:
  explicit CallRestartConnection(RefPtr<EventSourceImpl>&& aEventSourceImpl)
      : WorkerMainThreadRunnable(GetCurrentThreadWorkerPrivate(),
                                 "EventSource :: RestartConnection"_ns),
        mESImpl(std::move(aEventSourceImpl)) {
    MOZ_ASSERT(mESImpl);
  }

  bool MainThreadRun() override {
    MOZ_ASSERT(mESImpl);
    mESImpl->RestartConnection();
    // We want to ensure the shortest possible remaining lifetime
    // and not depend on the Runnable's destruction.
    mESImpl = nullptr;
    return true;
  }

 protected:
  RefPtr<EventSourceImpl> mESImpl;
};

nsresult EventSourceImpl::RestartConnection() {
  AssertIsOnMainThread();
  if (IsClosed()) {
    return NS_ERROR_ABORT;
  }

  nsresult rv = ResetConnection();
  NS_ENSURE_SUCCESS(rv, rv);
  rv = SetReconnectionTimeout();
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

void EventSourceImpl::ReestablishConnection() {
  AssertIsOnTargetThread();
  if (IsClosed()) {
    return;
  }

  nsresult rv;
  if (mIsMainThread) {
    rv = RestartConnection();
  } else {
    RefPtr<CallRestartConnection> runnable = new CallRestartConnection(this);
    ErrorResult result;
    runnable->Dispatch(GetCurrentThreadWorkerPrivate(), Canceling, result);
    MOZ_ASSERT(!result.Failed());
    rv = result.StealNSResult();
  }
  if (NS_FAILED(rv)) {
    return;
  }

  RefPtr<EventSource> source = GetEventSource();
  if (!source) {
    NS_WARNING("Event source is null");
    return;
  }

  rv = source->CheckCurrentGlobalCorrectness();
  if (NS_FAILED(rv)) {
    return;
  }

  SetReadyState(CONNECTING);
  ResetDecoder();
  // We can't hold the mutex while dispatching the event because the mutex is
  // not reentrant, and content might call back into our code.
  rv = source->CreateAndDispatchSimpleEvent(u"error"_ns);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to dispatch the error event!!!");
    return;
  }
}

nsresult EventSourceImpl::SetReconnectionTimeout() {
  AssertIsOnMainThread();
  if (IsClosed()) {
    return NS_ERROR_ABORT;
  }

  // the timer will be used whenever the requests are going finished.
  if (!mTimer) {
    mTimer = NS_NewTimer();
    NS_ENSURE_STATE(mTimer);
  }

  MOZ_TRY(mTimer->InitWithCallback(this, mReconnectionTime,
                                   nsITimer::TYPE_ONE_SHOT));

  return NS_OK;
}

nsresult EventSourceImpl::PrintErrorOnConsole(
    const char* aBundleURI, const char* aError,
    const nsTArray<nsString>& aFormatStrings) {
  AssertIsOnMainThread();
  MOZ_ASSERT(!mIsShutDown);
  nsCOMPtr<nsIStringBundleService> bundleService =
      mozilla::components::StringBundle::Service();
  NS_ENSURE_STATE(bundleService);

  nsCOMPtr<nsIStringBundle> strBundle;
  nsresult rv =
      bundleService->CreateBundle(aBundleURI, getter_AddRefs(strBundle));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIConsoleService> console(
      do_GetService(NS_CONSOLESERVICE_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIScriptError> errObj(
      do_CreateInstance(NS_SCRIPTERROR_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  // Localize the error message
  nsAutoString message;
  if (!aFormatStrings.IsEmpty()) {
    rv = strBundle->FormatStringFromName(aError, aFormatStrings, message);
  } else {
    rv = strBundle->GetStringFromName(aError, message);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  rv = errObj->InitWithWindowID(
      message, mCallingLocation.FileName(), mCallingLocation.mLine,
      mCallingLocation.mColumn, nsIScriptError::errorFlag, "Event Source",
      mInnerWindowID);
  NS_ENSURE_SUCCESS(rv, rv);

  // print the error message directly to the JS console
  rv = console->LogMessage(errObj);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult EventSourceImpl::ConsoleError() {
  AssertIsOnMainThread();
  MOZ_ASSERT(!mIsShutDown);
  nsAutoCString targetSpec;
  nsresult rv = mSrc->GetSpec(targetSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  AutoTArray<nsString, 1> formatStrings;
  CopyUTF8toUTF16(targetSpec, *formatStrings.AppendElement());

  if (ReadyState() == CONNECTING) {
    rv = PrintErrorOnConsole("chrome://global/locale/appstrings.properties",
                             "connectionFailure", formatStrings);
  } else {
    rv = PrintErrorOnConsole("chrome://global/locale/appstrings.properties",
                             "netInterrupt", formatStrings);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

void EventSourceImpl::DispatchFailConnection() {
  AssertIsOnMainThread();
  if (IsClosed()) {
    return;
  }
  nsresult rv = ConsoleError();
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to print to the console error");
  }
  rv = Dispatch(NewRunnableMethod("dom::EventSourceImpl::FailConnection", this,
                                  &EventSourceImpl::FailConnection),
                NS_DISPATCH_NORMAL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    // if the worker is shutting down, the dispatching of normal WorkerRunnables
    // fails.
    return;
  }
}

void EventSourceImpl::FailConnection() {
  AssertIsOnTargetThread();
  if (IsClosed()) {
    return;
  }
  // Must change state to closed before firing event to content.
  SetReadyState(CLOSED);
  // When a user agent is to fail the connection, the user agent must set the
  // readyState attribute to CLOSED and queue a task to fire a simple event
  // named error at the EventSource object.
  nsresult rv = GetEventSource()->CheckCurrentGlobalCorrectness();
  if (NS_SUCCEEDED(rv)) {
    // We can't hold the mutex while dispatching the event because the mutex
    // is not reentrant, and content might call back into our code.
    rv = GetEventSource()->CreateAndDispatchSimpleEvent(u"error"_ns);
    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to dispatch the error event!!!");
    }
  }
  // Call CloseInternal in the end of function because it may release
  // EventSourceImpl.
  CloseInternal();
}

NS_IMETHODIMP EventSourceImpl::Notify(nsITimer* aTimer) {
  AssertIsOnMainThread();
  if (IsClosed()) {
    return NS_OK;
  }

  MOZ_ASSERT(!mHttpChannel, "the channel hasn't been cancelled!!");

  if (!mFrozen) {
    nsresult rv = InitChannelAndRequestEventSource(mIsMainThread);
    if (NS_FAILED(rv)) {
      NS_WARNING("InitChannelAndRequestEventSource() failed");
    }
  }
  return NS_OK;
}

NS_IMETHODIMP EventSourceImpl::GetName(nsACString& aName) {
  aName.AssignLiteral("EventSourceImpl");
  return NS_OK;
}

nsresult EventSourceImpl::Thaw() {
  AssertIsOnMainThread();
  if (IsClosed() || !mFrozen) {
    return NS_OK;
  }

  MOZ_ASSERT(!mHttpChannel, "the connection hasn't been closed!!!");

  mFrozen = false;
  nsresult rv;
  if (!mGoingToDispatchAllMessages && mMessagesToDispatch.GetSize() > 0) {
    nsCOMPtr<nsIRunnable> event =
        NewRunnableMethod("dom::EventSourceImpl::DispatchAllMessageEvents",
                          this, &EventSourceImpl::DispatchAllMessageEvents);
    NS_ENSURE_STATE(event);

    mGoingToDispatchAllMessages = true;

    rv = Dispatch(event.forget(), NS_DISPATCH_NORMAL);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = InitChannelAndRequestEventSource(mIsMainThread);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult EventSourceImpl::Freeze() {
  AssertIsOnMainThread();
  if (IsClosed() || mFrozen) {
    return NS_OK;
  }

  MOZ_ASSERT(!mHttpChannel, "the connection hasn't been closed!!!");
  mFrozen = true;
  return NS_OK;
}

nsresult EventSourceImpl::DispatchCurrentMessageEvent() {
  AssertIsOnTargetThread();
  MOZ_ASSERT(!mIsShutDown);
  UniquePtr<Message> message(std::move(mCurrentMessage));
  ClearFields();

  if (!message || message->mData.IsEmpty()) {
    return NS_OK;
  }

  // removes the trailing LF from mData
  MOZ_ASSERT(message->mData.CharAt(message->mData.Length() - 1) == LF_CHAR,
             "Invalid trailing character! LF was expected instead.");
  message->mData.SetLength(message->mData.Length() - 1);

  if (message->mEventName.IsEmpty()) {
    message->mEventName.AssignLiteral("message");
  }

  {
    auto lock = mSharedData.Lock();
    message->mOrigin = lock->mOrigin;
  }

  mMessagesToDispatch.Push(message.release());

  if (!mGoingToDispatchAllMessages) {
    nsCOMPtr<nsIRunnable> event =
        NewRunnableMethod("dom::EventSourceImpl::DispatchAllMessageEvents",
                          this, &EventSourceImpl::DispatchAllMessageEvents);
    NS_ENSURE_STATE(event);

    mGoingToDispatchAllMessages = true;

    return Dispatch(event.forget(), NS_DISPATCH_NORMAL);
  }

  return NS_OK;
}

void EventSourceImpl::DispatchAllMessageEvents() {
  AssertIsOnTargetThread();
  mGoingToDispatchAllMessages = false;

  if (IsClosed() || mFrozen) {
    return;
  }

  nsresult rv;
  AutoJSAPI jsapi;
  {
    auto lock = mSharedData.Lock();
    rv = lock->mEventSource->CheckCurrentGlobalCorrectness();
    if (NS_FAILED(rv)) {
      return;
    }

    if (NS_WARN_IF(!jsapi.Init(lock->mEventSource->GetRelevantGlobal()))) {
      return;
    }
  }

  JSContext* cx = jsapi.cx();

  while (mMessagesToDispatch.GetSize() > 0) {
    UniquePtr<Message> message(mMessagesToDispatch.PopFront());

    if (message->mLastEventID.isSome()) {
      mLastEventID.Assign(message->mLastEventID.value());
    }

    if (message->mLastEventID.isNothing() && !mLastEventID.IsEmpty()) {
      message->mLastEventID = Some(mLastEventID);
    }

    {
      auto lock = mSharedData.Lock();
      if (lock->mServiceNotifier) {
        lock->mServiceNotifier->EventReceived(message->mEventName, mLastEventID,
                                              message->mData, mReconnectionTime,
                                              PR_Now());
      }
    }

    // Now we can turn our string into a jsval
    JS::Rooted<JS::Value> jsData(cx);
    {
      JSString* jsString;
      jsString = JS_NewUCStringCopyN(cx, message->mData.get(),
                                     message->mData.Length());
      NS_ENSURE_TRUE_VOID(jsString);

      jsData.setString(jsString);
    }

    // create an event that uses the MessageEvent interface,
    // which does not bubble, is not cancelable, and has no default action

    RefPtr<EventSource> eventSource = GetEventSource();
    RefPtr<MessageEvent> event =
        new MessageEvent(eventSource, nullptr, nullptr);

    event->InitMessageEvent(nullptr, message->mEventName, CanBubble::eNo,
                            Cancelable::eNo, jsData, message->mOrigin,
                            mLastEventID, nullptr,
                            Sequence<OwningNonNull<MessagePort>>());
    event->SetTrusted(true);

    // We can't hold the mutex while dispatching the event because the mutex is
    // not reentrant, and content might call back into our code.
    IgnoredErrorResult err;
    eventSource->DispatchEvent(*event, err);
    if (err.Failed()) {
      NS_WARNING("Failed to dispatch the message event!!!");
      return;
    }

    if (IsClosed() || mFrozen) {
      return;
    }
  }
}

void EventSourceImpl::ClearFields() {
  AssertIsOnTargetThread();
  mCurrentMessage = nullptr;
  mLastFieldName.Truncate();
  mLastFieldValue.Truncate();
}

nsresult EventSourceImpl::SetFieldAndClear() {
  MOZ_ASSERT(!mIsShutDown);
  AssertIsOnTargetThread();
  if (mLastFieldName.IsEmpty()) {
    mLastFieldValue.Truncate();
    return NS_OK;
  }
  if (!mCurrentMessage) {
    mCurrentMessage = std::make_unique<Message>();
  }
  char16_t first_char;
  first_char = mLastFieldName.CharAt(0);

  // with no case folding performed
  switch (first_char) {
    case char16_t('d'):
      if (mLastFieldName.EqualsLiteral("data")) {
        // If the field name is "data" append the field value to the data
        // buffer, then append a single U+000A LINE FEED (LF) character
        // to the data buffer.
        mCurrentMessage->mData.Append(mLastFieldValue);
        mCurrentMessage->mData.Append(LF_CHAR);
      }
      break;

    case char16_t('e'):
      if (mLastFieldName.EqualsLiteral("event")) {
        mCurrentMessage->mEventName.Assign(mLastFieldValue);
      }
      break;

    case char16_t('i'):
      if (mLastFieldName.EqualsLiteral("id")) {
        mCurrentMessage->mLastEventID = Some(mLastFieldValue);
      }
      break;

    case char16_t('r'):
      if (mLastFieldName.EqualsLiteral("retry")) {
        uint32_t newValue = 0;
        uint32_t i = 0;  // we must ensure that there are only digits
        bool assign = true;
        for (i = 0; i < mLastFieldValue.Length(); ++i) {
          if (mLastFieldValue.CharAt(i) < (char16_t)'0' ||
              mLastFieldValue.CharAt(i) > (char16_t)'9') {
            assign = false;
            break;
          }
          newValue = newValue * 10 + (((uint32_t)mLastFieldValue.CharAt(i)) -
                                      ((uint32_t)((char16_t)'0')));
        }

        if (assign) {
          if (newValue < MIN_RECONNECTION_TIME_VALUE) {
            mReconnectionTime = MIN_RECONNECTION_TIME_VALUE;
          } else if (newValue > MAX_RECONNECTION_TIME_VALUE) {
            mReconnectionTime = MAX_RECONNECTION_TIME_VALUE;
          } else {
            mReconnectionTime = newValue;
          }
        }
        break;
      }
      break;
  }

  mLastFieldName.Truncate();
  mLastFieldValue.Truncate();

  return NS_OK;
}

nsresult EventSourceImpl::CheckHealthOfRequestCallback(
    nsIRequest* aRequestCallback) {
  // This function could be run on target thread if http channel support
  // nsIThreadRetargetableRequest. otherwise, it's run on main thread.

  // check if we have been closed or if the request has been canceled
  // or if we have been frozen
  if (IsClosed() || mFrozen || !mHttpChannel) {
    return NS_ERROR_ABORT;
  }

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aRequestCallback);
  NS_ENSURE_STATE(httpChannel);

  if (httpChannel != mHttpChannel) {
    NS_WARNING("wrong channel from request callback");
    return NS_ERROR_ABORT;
  }

  return NS_OK;
}

nsresult EventSourceImpl::ParseCharacter(char16_t aChr) {
  AssertIsOnTargetThread();
  nsresult rv;

  if (IsClosed()) {
    return NS_ERROR_ABORT;
  }

  switch (mStatus) {
    case PARSE_STATE_OFF:
      NS_ERROR("Invalid state");
      return NS_ERROR_FAILURE;
      break;

    case PARSE_STATE_BEGIN_OF_STREAM:
      if (aChr == CR_CHAR) {
        mStatus = PARSE_STATE_CR_CHAR;
      } else if (aChr == LF_CHAR) {
        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      } else if (aChr == COLON_CHAR) {
        mStatus = PARSE_STATE_COMMENT;
      } else {
        mLastFieldName += aChr;
        mStatus = PARSE_STATE_FIELD_NAME;
      }
      break;

    case PARSE_STATE_CR_CHAR:
      if (aChr == CR_CHAR) {
        rv = DispatchCurrentMessageEvent();  // there is an empty line (CRCR)
        NS_ENSURE_SUCCESS(rv, rv);
      } else if (aChr == LF_CHAR) {
        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      } else if (aChr == COLON_CHAR) {
        mStatus = PARSE_STATE_COMMENT;
      } else {
        mLastFieldName += aChr;
        mStatus = PARSE_STATE_FIELD_NAME;
      }

      break;

    case PARSE_STATE_COMMENT:
      if (aChr == CR_CHAR) {
        mStatus = PARSE_STATE_CR_CHAR;
      } else if (aChr == LF_CHAR) {
        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      }

      break;

    case PARSE_STATE_FIELD_NAME:
      if (aChr == CR_CHAR) {
        rv = SetFieldAndClear();
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_CR_CHAR;
      } else if (aChr == LF_CHAR) {
        rv = SetFieldAndClear();
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      } else if (aChr == COLON_CHAR) {
        mStatus = PARSE_STATE_FIRST_CHAR_OF_FIELD_VALUE;
      } else {
        mLastFieldName += aChr;
      }

      break;

    case PARSE_STATE_FIRST_CHAR_OF_FIELD_VALUE:
      if (aChr == CR_CHAR) {
        rv = SetFieldAndClear();
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_CR_CHAR;
      } else if (aChr == LF_CHAR) {
        rv = SetFieldAndClear();
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      } else if (aChr == SPACE_CHAR) {
        mStatus = PARSE_STATE_FIELD_VALUE;
      } else {
        mLastFieldValue += aChr;
        mStatus = PARSE_STATE_FIELD_VALUE;
      }

      break;

    case PARSE_STATE_FIELD_VALUE:
      if (aChr == CR_CHAR) {
        rv = SetFieldAndClear();
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_CR_CHAR;
      } else if (aChr == LF_CHAR) {
        rv = SetFieldAndClear();
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      } else if (aChr != 0) {
        // Avoid appending the null char to the field value.
        mLastFieldValue += aChr;
      } else if (mLastFieldName.EqualsLiteral("id")) {
        // Ignore the whole id field if aChr is null
        mStatus = PARSE_STATE_IGNORE_FIELD_VALUE;
        mLastFieldValue.Truncate();
      }

      break;

    case PARSE_STATE_IGNORE_FIELD_VALUE:
      if (aChr == CR_CHAR) {
        mStatus = PARSE_STATE_CR_CHAR;
      } else if (aChr == LF_CHAR) {
        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      }
      break;

    case PARSE_STATE_BEGIN_OF_LINE:
      if (aChr == CR_CHAR) {
        rv = DispatchCurrentMessageEvent();  // there is an empty line
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_CR_CHAR;
      } else if (aChr == LF_CHAR) {
        rv = DispatchCurrentMessageEvent();  // there is an empty line
        NS_ENSURE_SUCCESS(rv, rv);

        mStatus = PARSE_STATE_BEGIN_OF_LINE;
      } else if (aChr == COLON_CHAR) {
        mStatus = PARSE_STATE_COMMENT;
      } else if (aChr != 0) {
        // Avoid appending the null char to the field name.
        mLastFieldName += aChr;
        mStatus = PARSE_STATE_FIELD_NAME;
      }

      break;
  }

  return NS_OK;
}

namespace {

class WorkerRunnableDispatcher final : public WorkerThreadRunnable {
  RefPtr<EventSourceImpl> mEventSourceImpl;

 public:
  WorkerRunnableDispatcher(RefPtr<EventSourceImpl>&& aImpl,
                           WorkerPrivate* aWorkerPrivate,
                           already_AddRefed<nsIRunnable> aEvent)
      : WorkerThreadRunnable("WorkerRunnableDispatcher"),
        mEventSourceImpl(std::move(aImpl)),
        mEvent(std::move(aEvent)) {}

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->AssertIsOnWorkerThread();
    return !NS_FAILED(mEvent->Run());
  }

  void PostRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate,
               bool aRunResult) override {
    // Ensure we drop the RefPtr on the worker thread
    // and to not keep us alive longer than needed.
    mEventSourceImpl = nullptr;
  }

  bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
    // We don't call WorkerRunnable::PreDispatch because it would assert the
    // wrong thing about which thread we're on.  We're on whichever thread the
    // channel implementation is running on (probably the main thread or
    // transport thread).
    return true;
  }

  void PostDispatch(WorkerPrivate* aWorkerPrivate,
                    bool aDispatchResult) override {
    // We don't call WorkerRunnable::PostDispatch because it would assert the
    // wrong thing about which thread we're on.  We're on whichever thread the
    // channel implementation is running on (probably the main thread or
    // transport thread).
  }

 private:
  nsCOMPtr<nsIRunnable> mEvent;
};

}  // namespace

bool EventSourceImpl::CreateWorkerRef(WorkerPrivate* aWorkerPrivate) {
  auto tsWorkerRef = mWorkerRef.Lock();
  MOZ_ASSERT(!*tsWorkerRef);
  MOZ_ASSERT(aWorkerPrivate);
  aWorkerPrivate->AssertIsOnWorkerThread();

  if (mIsShutDown) {
    return false;
  }

  RefPtr<EventSourceImpl> self = this;
  RefPtr<StrongWorkerRef> workerRef = StrongWorkerRef::Create(
      aWorkerPrivate, "EventSource", [self]() { self->Close(); });

  if (NS_WARN_IF(!workerRef)) {
    return false;
  }

  *tsWorkerRef = new ThreadSafeWorkerRef(workerRef);
  return true;
}

void EventSourceImpl::ReleaseWorkerRef() {
  MOZ_ASSERT(IsClosed());
  MOZ_ASSERT(IsCurrentThreadRunningWorker());
  auto workerRef = mWorkerRef.Lock();
  *workerRef = nullptr;
}

//-----------------------------------------------------------------------------
// EventSourceImpl::nsIEventTarget
//-----------------------------------------------------------------------------
NS_IMETHODIMP
EventSourceImpl::DispatchFromScript(nsIRunnable* aEvent, DispatchFlags aFlags) {
  nsCOMPtr<nsIRunnable> event(aEvent);
  return Dispatch(event.forget(), aFlags);
}

NS_IMETHODIMP
EventSourceImpl::Dispatch(already_AddRefed<nsIRunnable> aEvent,
                          DispatchFlags aFlags) {
  // FIXME: This dispatch implementation has inconsistent leaking behaviour when
  // `NS_DISPATCH_FALLIBLE` is not specified.
  nsCOMPtr<nsIRunnable> event_ref(aEvent);
  if (DispatchesToMainThread()) {
    return NS_DispatchToMainThread(event_ref.forget(), aFlags);
  }

  // If the target is a worker, we have to use a custom WorkerRunnableDispatcher
  // runnable.
  auto workerRef = mWorkerRef.Lock();
  // The worker ref is released only after shutdown.
  if (!*workerRef) {
    return NS_DispatchToMainThread(event_ref.forget(), aFlags);
  }
  RefPtr<WorkerRunnableDispatcher> event = new WorkerRunnableDispatcher(
      this, (*workerRef)->Private(), event_ref.forget());

  if (!event->Dispatch((*workerRef)->Private())) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
EventSourceImpl::DelayedDispatch(already_AddRefed<nsIRunnable> aEvent,
                                 uint32_t aDelayMs) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
EventSourceImpl::RegisterShutdownTask(nsITargetShutdownTask*) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
EventSourceImpl::UnregisterShutdownTask(nsITargetShutdownTask*) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsIEventTarget::FeatureFlags EventSourceImpl::GetFeatures() {
  return SUPPORTS_BASE;
}

//-----------------------------------------------------------------------------
// EventSourceImpl::nsIThreadRetargetableStreamListener
//-----------------------------------------------------------------------------
NS_IMETHODIMP
EventSourceImpl::CheckListenerChain() {
  MOZ_ASSERT(NS_IsMainThread(), "Should be on the main thread!");
  return NS_OK;
}

NS_IMETHODIMP
EventSourceImpl::OnDataFinished(nsresult) { return NS_OK; }

////////////////////////////////////////////////////////////////////////////////
// EventSource
////////////////////////////////////////////////////////////////////////////////

EventSource::EventSource(nsIGlobalObject* aGlobal,
                         nsICookieJarSettings* aCookieJarSettings,
                         bool aWithCredentials)
    : DOMEventTargetHelper(aGlobal),
      mWithCredentials(aWithCredentials),
      mIsMainThread(NS_IsMainThread()) {
  MOZ_ASSERT(aGlobal);
  MOZ_ASSERT(aCookieJarSettings);
  mESImpl = new EventSourceImpl(this, aCookieJarSettings);
}

EventSource::~EventSource() = default;

nsresult EventSource::CreateAndDispatchSimpleEvent(const nsAString& aName) {
  RefPtr<Event> event = NS_NewDOMEvent(this, nullptr, nullptr);
  // it doesn't bubble, and it isn't cancelable
  event->InitEvent(aName, false, false);
  event->SetTrusted(true);
  ErrorResult rv;
  DispatchEvent(*event, rv);
  return rv.StealNSResult();
}

/* static */
already_AddRefed<EventSource> EventSource::Constructor(
    const GlobalObject& aGlobal, const nsAString& aURL,
    const EventSourceInit& aEventSourceInitDict, ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  if (NS_WARN_IF(!global)) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  nsCOMPtr<nsPIDOMWindowInner> ownerWindow = do_QueryInterface(global);
  if (ownerWindow) {
    Document* doc = ownerWindow->GetExtantDoc();
    if (NS_WARN_IF(!doc)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    cookieJarSettings = doc->CookieJarSettings();
  } else {
    // Worker side.
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    if (!workerPrivate) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }

    cookieJarSettings = workerPrivate->CookieJarSettings();
  }

  RefPtr<EventSource> eventSource = new EventSource(
      global, cookieJarSettings, aEventSourceInitDict.mWithCredentials);

  if (NS_IsMainThread()) {
    // Get principal from document and init EventSourceImpl
    nsCOMPtr<nsIScriptObjectPrincipal> scriptPrincipal =
        do_QueryInterface(aGlobal.GetAsSupports());
    if (!scriptPrincipal) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }
    nsCOMPtr<nsIPrincipal> principal = scriptPrincipal->GetPrincipal();
    if (!principal) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }
    eventSource->mESImpl->Init(global, principal, aURL, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }

    eventSource->mESImpl->InitChannelAndRequestEventSource(true);
    return eventSource.forget();
  }

  // Worker side.
  {
    // Scope for possible failures that need cleanup
    auto guardESImpl = MakeScopeExit([&] { eventSource->mESImpl = nullptr; });

    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(workerPrivate);

    eventSource->mESImpl->mInnerWindowID = workerPrivate->WindowID();
    eventSource->mESImpl->mClientInfo =
        workerPrivate->GlobalScope()->GetClientInfo();
    eventSource->mESImpl->mController =
        workerPrivate->GlobalScope()->GetController();

    eventSource->mESImpl->Init(nullptr, workerPrivate->GetPrincipal(), aURL,
                               aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }

    // In workers we have to keep the worker alive using a WorkerRef in order
    // to dispatch messages correctly.
    if (!eventSource->mESImpl->CreateWorkerRef(workerPrivate)) {
      // The worker is already shutting down. Let's return an already closed
      // object, but marked as Connecting.
      if (eventSource->mESImpl) {
        // mESImpl is nulled by this call such that EventSourceImpl is
        // released before returning the object, otherwise
        // it will set EventSource to a CLOSED state in its DTOR..
        eventSource->mESImpl->Close();
      }
      eventSource->mReadyState = EventSourceImpl::CONNECTING;

      guardESImpl.release();
      return eventSource.forget();
    }

    // Let's connect to the server.
    RefPtr<ConnectRunnable> connectRunnable =
        new ConnectRunnable(workerPrivate, eventSource->mESImpl);
    connectRunnable->Dispatch(workerPrivate, Canceling, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }

    // End of scope for possible failures
    guardESImpl.release();
  }

  return eventSource.forget();
}

// nsWrapperCache
JSObject* EventSource::WrapObject(JSContext* aCx,
                                  JS::Handle<JSObject*> aGivenProto) {
  return EventSource_Binding::Wrap(aCx, this, aGivenProto);
}

void EventSource::Close() {
  AssertIsOnTargetThread();
  if (mESImpl) {
    // Close potentially kills ourself, ensure
    // to not access any members afterwards.
    mESImpl->Close();
  }
}

//-----------------------------------------------------------------------------
// EventSource::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_CYCLE_COLLECTION_CLASS(EventSource)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(EventSource,
                                                  DOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(EventSource,
                                                DOMEventTargetHelper)
  if (tmp->mESImpl) {
    // IsCertainlyaliveForCC will return true and cause the cycle
    // collector to skip this instance when mESImpl is non-null and
    // points back to ourself.
    // mESImpl is initialized to be non-null in the constructor
    // and should have been wiped out in our close function.
    MOZ_ASSERT_UNREACHABLE("Paranoia cleanup that should never happen.");
    tmp->Close();
  }
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

bool EventSource::IsCertainlyAliveForCC() const {
  // Until we are double linked forth and back, we want to stay alive.
  if (!mESImpl) {
    return false;
  }
  auto lock = mESImpl->mSharedData.Lock();
  return lock->mEventSource == this;
}

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(EventSource)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(EventSource, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(EventSource, DOMEventTargetHelper)

}  // namespace mozilla::dom
