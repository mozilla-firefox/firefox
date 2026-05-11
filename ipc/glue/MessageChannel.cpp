/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ipc/MessageChannel.h"

#include <math.h>

#include <utility>

#include "CrashAnnotations.h"
#include "base/waitable_event.h"
#include "mozilla/Assertions.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/FlowMarkers.h"
#include "mozilla/IntentionalCrash.h"
#include "mozilla/Logging.h"
#include "mozilla/Monitor.h"
#include "mozilla/Mutex.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/glean/IpcMetrics.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/ipc/NodeController.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "nsAppRunner.h"
#include "nsContentUtils.h"
#include "nsIDirectTaskDispatcher.h"
#include "nsTHashMap.h"
#include "nsDebug.h"
#include "nsExceptionHandler.h"
#include "nsIMemoryReporter.h"
#include "nsISupportsImpl.h"
#include "nsPrintfCString.h"
#include "nsThreadUtils.h"

#ifdef XP_WIN
#  include "mozilla/gfx/Logging.h"
#endif

#ifdef FUZZING_SNAPSHOT
#  include "mozilla/fuzzing/IPCFuzzController.h"
#endif

// Undo the damage done by mozzconf.h
#undef compress

static mozilla::LazyLogModule sLogModule("ipc");
#define IPC_LOG(...) MOZ_LOG(sLogModule, LogLevel::Debug, (__VA_ARGS__))

/*
 * IPC design:
 *
 * There are two kinds of messages: async and sync. Sync messages are blocking.
 *
 * Terminology: To dispatch a message Foo is to run the RecvFoo code for
 * it. This is also called "handling" the message.
 *
 * Sync and async messages can sometimes "nest" inside other sync messages
 * (i.e., while waiting for the sync reply, we can dispatch the inner
 * message). The three possible nesting levels are NOT_NESTED,
 * NESTED_INSIDE_SYNC, and NESTED_INSIDE_CPOW.  The intended uses are:
 *   NOT_NESTED - most messages.
 *   NESTED_INSIDE_SYNC - CPOW-related messages, which are always sync
 *                        and can go in either direction.
 *   NESTED_INSIDE_CPOW - messages where we don't want to dispatch
 *                        incoming CPOWs while waiting for the response.
 * These nesting levels are ordered: NOT_NESTED, NESTED_INSIDE_SYNC,
 * NESTED_INSIDE_CPOW.  Async messages cannot be NESTED_INSIDE_SYNC but they can
 * be NESTED_INSIDE_CPOW.
 *
 * To avoid jank, the parent process is not allowed to send NOT_NESTED sync
 * messages. When a process is waiting for a response to a sync message M0, it
 * will dispatch an incoming message M if:
 *   1. M has a higher nesting level than M0, or
 *   2. if M has the same nesting level as M0 and we're in the child, or
 *   3. if M has the same nesting level as M0 and it was sent by the other side
 *      while dispatching M0.
 * The idea is that messages with higher nesting should take precendence. The
 * purpose of rule 2 is to handle a race where both processes send to each other
 * simultaneously. In this case, we resolve the race in favor of the parent (so
 * the child dispatches first).
 *
 * Messages satisfy the following properties:
 *   A. When waiting for a response to a sync message, we won't dispatch any
 *      messages of a lower nesting level.
 *   B. Messages of the same nesting level will be dispatched roughly in the
 *      order they were sent. The exception is when the parent and child send
 *      sync messages to each other simulataneously. In this case, the parent's
 *      message is dispatched first. While it is dispatched, the child may send
 *      further nested messages, and these messages may be dispatched before the
 *      child's original message. We can consider ordering to be preserved here
 *      because we pretend that the child's original message wasn't sent until
 *      after the parent's message is finished being dispatched.
 *
 * When waiting for a sync message reply, we dispatch an async message only if
 * it is NESTED_INSIDE_CPOW. Normally NESTED_INSIDE_CPOW async
 * messages are sent only from the child. However, the parent can send
 * NESTED_INSIDE_CPOW async messages when it is creating a bridged protocol.
 */

using namespace mozilla;
using namespace mozilla::ipc;

using mozilla::MonitorAutoLock;
using mozilla::MonitorAutoUnlock;
using mozilla::dom::AutoNoJSAPI;

#define IPC_ASSERT(_cond, ...)                                           \
  do {                                                                   \
    AssertWorkerThread();                                                \
    mMonitor->AssertCurrentThreadOwns();                                 \
    if (!(_cond)) DebugAbort(__FILE__, __LINE__, #_cond, ##__VA_ARGS__); \
  } while (0)

static MessageChannel* gParentProcessBlocker = nullptr;

namespace mozilla {
namespace ipc {

// static
bool MessageChannel::sIsPumpingMessages = false;

class AutoEnterTransaction {
 public:
  explicit AutoEnterTransaction(MessageChannel* aChan,
                                IPC::Message::seqno_t aMsgSeqno,
                                IPC::Message::seqno_t aTransactionID,
                                int aNestedLevel) MOZ_REQUIRES(*aChan->mMonitor)
      : mChan(aChan),
        mActive(true),
        mOutgoing(true),
        mNestedLevel(aNestedLevel),
        mSeqno(aMsgSeqno),
        mTransaction(aTransactionID),
        mNext(mChan->mTransactionStack) {
    mChan->mMonitor->AssertCurrentThreadOwns();
    mChan->mTransactionStack = this;
  }

  explicit AutoEnterTransaction(MessageChannel* aChan,
                                const IPC::Message& aMessage)
      MOZ_REQUIRES(*aChan->mMonitor)
      : mChan(aChan),
        mActive(true),
        mOutgoing(false),
        mNestedLevel(aMessage.nested_level()),
        mSeqno(aMessage.seqno()),
        mTransaction(aMessage.transaction_id()),
        mNext(mChan->mTransactionStack) {
    mChan->mMonitor->AssertCurrentThreadOwns();

    if (!aMessage.is_sync()) {
      mActive = false;
      return;
    }

    mChan->mTransactionStack = this;
  }

  ~AutoEnterTransaction() {
    mChan->mMonitor->AssertCurrentThreadOwns();
    if (mActive) {
      mChan->mTransactionStack = mNext;
    }
  }

  void Cancel() {
    mChan->mMonitor->AssertCurrentThreadOwns();
    AutoEnterTransaction* cur = mChan->mTransactionStack;
    MOZ_RELEASE_ASSERT(cur == this);
    while (cur && cur->mNestedLevel != IPC::Message::NOT_NESTED) {
      // Note that, in the following situation, we will cancel multiple
      // transactions:
      // 1. Parent sends NESTED_INSIDE_SYNC message P1 to child.
      // 2. Child sends NESTED_INSIDE_SYNC message C1 to child.
      // 3. Child dispatches P1, parent blocks.
      // 4. Child cancels.
      // In this case, both P1 and C1 are cancelled. The parent will
      // remove C1 from its queue when it gets the cancellation message.
      MOZ_RELEASE_ASSERT(cur->mActive);
      cur->mActive = false;
      cur = cur->mNext;
    }

    mChan->mTransactionStack = cur;

    MOZ_RELEASE_ASSERT(IsComplete());
  }

  bool AwaitingSyncReply() const {
    MOZ_RELEASE_ASSERT(mActive);
    if (mOutgoing) {
      return true;
    }
    return mNext ? mNext->AwaitingSyncReply() : false;
  }

  int AwaitingSyncReplyNestedLevel() const {
    MOZ_RELEASE_ASSERT(mActive);
    if (mOutgoing) {
      return mNestedLevel;
    }
    return mNext ? mNext->AwaitingSyncReplyNestedLevel() : 0;
  }

  bool DispatchingSyncMessage() const {
    MOZ_RELEASE_ASSERT(mActive);
    if (!mOutgoing) {
      return true;
    }
    return mNext ? mNext->DispatchingSyncMessage() : false;
  }

  int DispatchingSyncMessageNestedLevel() const {
    MOZ_RELEASE_ASSERT(mActive);
    if (!mOutgoing) {
      return mNestedLevel;
    }
    return mNext ? mNext->DispatchingSyncMessageNestedLevel() : 0;
  }

  int NestedLevel() const {
    MOZ_RELEASE_ASSERT(mActive);
    return mNestedLevel;
  }

  IPC::Message::seqno_t SequenceNumber() const {
    MOZ_RELEASE_ASSERT(mActive);
    return mSeqno;
  }

  IPC::Message::seqno_t TransactionID() const {
    MOZ_RELEASE_ASSERT(mActive);
    return mTransaction;
  }

  void ReceivedReply(UniquePtr<IPC::Message> aMessage) {
    MOZ_RELEASE_ASSERT(aMessage->seqno() == mSeqno);
    MOZ_RELEASE_ASSERT(aMessage->transaction_id() == mTransaction);
    MOZ_RELEASE_ASSERT(!mReply);
    IPC_LOG("Reply received on worker thread: seqno=%" PRId64, mSeqno);
    mReply = std::move(aMessage);
    MOZ_RELEASE_ASSERT(IsComplete());
  }

  void HandleReply(UniquePtr<IPC::Message> aMessage) {
    mChan->mMonitor->AssertCurrentThreadOwns();
    AutoEnterTransaction* cur = mChan->mTransactionStack;
    MOZ_RELEASE_ASSERT(cur == this);
    while (cur) {
      MOZ_RELEASE_ASSERT(cur->mActive);
      if (aMessage->seqno() == cur->mSeqno) {
        cur->ReceivedReply(std::move(aMessage));
        break;
      }
      cur = cur->mNext;
      MOZ_RELEASE_ASSERT(cur);
    }
  }

  bool IsComplete() { return !mActive || mReply; }

  bool IsOutgoing() { return mOutgoing; }

  bool IsCanceled() { return !mActive; }

  bool IsBottom() const { return !mNext; }

  bool IsError() {
    MOZ_RELEASE_ASSERT(mReply);
    return mReply->is_reply_error();
  }

  UniquePtr<IPC::Message> GetReply() { return std::move(mReply); }

 private:
  MessageChannel* mChan;

  // Active is true if this transaction is on the mChan->mTransactionStack
  // stack. Generally we're not on the stack if the transaction was canceled
  // or if it was for a message that doesn't require transactions (an async
  // message).
  bool mActive;

  // Is this stack frame for an outgoing message?
  bool mOutgoing;

  // Properties of the message being sent/received.
  int mNestedLevel;
  IPC::Message::seqno_t mSeqno;
  IPC::Message::seqno_t mTransaction;

  // Next item in mChan->mTransactionStack.
  AutoEnterTransaction* mNext;

  // Pointer the a reply received for this message, if one was received.
  UniquePtr<IPC::Message> mReply;
};

class ChannelCountReporter final : public nsIMemoryReporter {
  ~ChannelCountReporter() = default;

  struct ChannelCounts {
    size_t mNow;
    size_t mMax;

    ChannelCounts() : mNow(0), mMax(0) {}

    void Inc() {
      ++mNow;
      if (mMax < mNow) {
        mMax = mNow;
      }
    }

    void Dec() {
      MOZ_ASSERT(mNow > 0);
      --mNow;
    }
  };

  using CountTable = nsTHashMap<nsDepCharHashKey, ChannelCounts>;

  static StaticMutex sChannelCountMutex;
  static CountTable* sChannelCounts MOZ_GUARDED_BY(sChannelCountMutex);

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  NS_IMETHOD
  CollectReports(nsIHandleReportCallback* aHandleReport, nsISupports* aData,
                 bool aAnonymize) override {
    AutoTArray<std::pair<const char*, ChannelCounts>, 16> counts;
    {
      StaticMutexAutoLock countLock(sChannelCountMutex);
      if (!sChannelCounts) {
        return NS_OK;
      }
      counts.SetCapacity(sChannelCounts->Count());
      for (const auto& entry : *sChannelCounts) {
        counts.AppendElement(std::pair{entry.GetKey(), entry.GetData()});
      }
    }

    for (const auto& entry : counts) {
      nsPrintfCString pathNow("ipc-channels/%s", entry.first);
      nsPrintfCString pathMax("ipc-channels-peak/%s", entry.first);
      nsPrintfCString descNow(
          "Number of IPC channels for"
          " top-level actor type %s",
          entry.first);
      nsPrintfCString descMax(
          "Peak number of IPC channels for"
          " top-level actor type %s",
          entry.first);

      aHandleReport->Callback(""_ns, pathNow, KIND_OTHER, UNITS_COUNT,
                              entry.second.mNow, descNow, aData);
      aHandleReport->Callback(""_ns, pathMax, KIND_OTHER, UNITS_COUNT,
                              entry.second.mMax, descMax, aData);
    }
    return NS_OK;
  }

  static void Increment(const char* aName) {
    StaticMutexAutoLock countLock(sChannelCountMutex);
    if (!sChannelCounts) {
      sChannelCounts = new CountTable;
    }
    sChannelCounts->LookupOrInsert(aName).Inc();
  }

  static void Decrement(const char* aName) {
    StaticMutexAutoLock countLock(sChannelCountMutex);
    MOZ_ASSERT(sChannelCounts);
    sChannelCounts->LookupOrInsert(aName).Dec();
  }
};

StaticMutex ChannelCountReporter::sChannelCountMutex;
ChannelCountReporter::CountTable* ChannelCountReporter::sChannelCounts;

NS_IMPL_ISUPPORTS(ChannelCountReporter, nsIMemoryReporter)

// In child processes, the first MessageChannel is created before
// XPCOM is initialized enough to construct the memory reporter
// manager.  This retries every time a MessageChannel is constructed,
// which is good enough in practice.
template <class Reporter>
static void TryRegisterStrongMemoryReporter() {
  static Atomic<bool> registered;
  if (registered.compareExchange(false, true)) {
    if (NS_FAILED(RegisterStrongMemoryReporter(MakeAndAddRef<Reporter>()))) {
      registered = false;
    }
  }
}

MessageChannel::MessageChannel(const char* aName, IToplevelProtocol* aListener)
    : mName(aName), mListener(aListener), mMonitor(new RefCountedMonitor()) {
  MOZ_COUNT_CTOR(ipc::MessageChannel);

#ifdef XP_WIN
  mEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  MOZ_RELEASE_ASSERT(mEvent, "CreateEvent failed! Nothing is going to work!");
#endif

  TryRegisterStrongMemoryReporter<ChannelCountReporter>();
}

MessageChannel::~MessageChannel() {
  MOZ_COUNT_DTOR(ipc::MessageChannel);
  MonitorAutoLock lock(*mMonitor);
  MOZ_RELEASE_ASSERT(!mOnCxxStack,
                     "MessageChannel destroyed while code on CxxStack");
#ifdef XP_WIN
  if (mEvent) {
    BOOL ok = CloseHandle(mEvent);
    mEvent = nullptr;

    if (!ok) {
      gfxDevCrash(mozilla::gfx::LogReason::MessageChannelCloseFailure)
          << "MessageChannel failed to close. GetLastError: " << GetLastError();
    }
    MOZ_RELEASE_ASSERT(ok);
  } else {
    gfxDevCrash(mozilla::gfx::LogReason::MessageChannelCloseFailure)
        << "MessageChannel destructor ran without an mEvent Handle";
  }
#endif

  // Make sure that the MessageChannel was closed (and therefore cleared) before
  // it was destroyed. We can't properly close the channel at this point, as it
  // would be unsafe to invoke our listener's callbacks, and we may be being
  // destroyed on a thread other than `mWorkerThread`.
  if (!IsClosedLocked()) {
    CrashReporter::RecordAnnotationCString(
        CrashReporter::Annotation::IPCFatalErrorProtocol, mName);
    switch (mChannelState) {
      case ChannelConnected:
        MOZ_CRASH(
            "MessageChannel destroyed without being closed "
            "(mChannelState == ChannelConnected).");
        break;
      case ChannelClosing:
        MOZ_CRASH(
            "MessageChannel destroyed without being closed "
            "(mChannelState == ChannelClosing).");
        break;
      case ChannelError:
        MOZ_CRASH(
            "MessageChannel destroyed without being closed "
            "(mChannelState == ChannelError).");
        break;
      default:
        MOZ_CRASH("MessageChannel destroyed without being closed.");
    }
  }

  // Double-check other properties for thoroughness.
  MOZ_RELEASE_ASSERT(!mLink);
  MOZ_RELEASE_ASSERT(!mChannelErrorTask);
  MOZ_RELEASE_ASSERT(mPending.isEmpty());
  MOZ_RELEASE_ASSERT(!mShutdownTask);
}

#ifdef DEBUG
void MessageChannel::AssertMaybeDeferredCountCorrect() {
  mMonitor->AssertCurrentThreadOwns();

  size_t count = 0;
  for (MessageTask* task : mPending) {
    task->AssertMonitorHeld(*mMonitor);
    if (!IsAlwaysDeferred(*task->Msg())) {
      count++;
    }
  }

  MOZ_ASSERT(count == mMaybeDeferredPendingCount);
}
#endif

// This function returns the current transaction ID. Since the notion of a
// "current transaction" can be hard to define when messages race with each
// other and one gets canceled and the other doesn't, we require that this
// function is only called when the current transaction is known to be for a
// NESTED_INSIDE_SYNC message. In that case, we know for sure what the caller is
// looking for.
auto MessageChannel::CurrentNestedInsideSyncTransaction() const -> seqno_t {
  mMonitor->AssertCurrentThreadOwns();
  if (!mTransactionStack) {
    return 0;
  }
  MOZ_RELEASE_ASSERT(mTransactionStack->NestedLevel() ==
                     IPC::Message::NESTED_INSIDE_SYNC);
  return mTransactionStack->TransactionID();
}

bool MessageChannel::TestOnlyIsTransactionComplete() const {
  AssertWorkerThread();
  MonitorAutoLock lock(*mMonitor);
  return !mTransactionStack || mTransactionStack->IsComplete();
}

bool MessageChannel::AwaitingSyncReply() const {
  mMonitor->AssertCurrentThreadOwns();
  return mTransactionStack ? mTransactionStack->AwaitingSyncReply() : false;
}

int MessageChannel::AwaitingSyncReplyNestedLevel() const {
  mMonitor->AssertCurrentThreadOwns();
  return mTransactionStack ? mTransactionStack->AwaitingSyncReplyNestedLevel()
                           : 0;
}

bool MessageChannel::DispatchingSyncMessage() const {
  mMonitor->AssertCurrentThreadOwns();
  return mTransactionStack ? mTransactionStack->DispatchingSyncMessage()
                           : false;
}

int MessageChannel::DispatchingSyncMessageNestedLevel() const {
  mMonitor->AssertCurrentThreadOwns();
  return mTransactionStack
             ? mTransactionStack->DispatchingSyncMessageNestedLevel()
             : 0;
}

static void PrintErrorMessage(Side side, const char* channelName,
                              const char* msg) {
  printf_stderr("\n###!!! [%s][%s] Error: %s\n\n", StringFromIPCSide(side),
                channelName, msg);
}

bool MessageChannel::Connected() const {
  mMonitor->AssertCurrentThreadOwns();
  return ChannelConnected == mChannelState;
}

bool MessageChannel::ConnectedOrClosing() const {
  mMonitor->AssertCurrentThreadOwns();
  return ChannelConnected == mChannelState || ChannelClosing == mChannelState;
}

bool MessageChannel::CanSend() const {
  if (!mMonitor) {
    return false;
  }
  MonitorAutoLock lock(*mMonitor);
  return Connected();
}

void MessageChannel::Clear() {
  AssertWorkerThread();
  mMonitor->AssertCurrentThreadOwns();
  MOZ_DIAGNOSTIC_ASSERT(IsClosedLocked(), "MessageChannel cleared too early?");
  MOZ_ASSERT(ChannelClosed == mChannelState || ChannelError == mChannelState);

  // Don't clear mWorkerThread; we use it in AssertWorkerThread().
  //
  // Also don't clear mListener.  If we clear it, then sending a message
  // through this channel after it's Clear()'ed can cause this process to
  // crash.

  if (mShutdownTask) {
    mShutdownTask->Clear();
    mWorkerThread->UnregisterShutdownTask(mShutdownTask);
  }
  mShutdownTask = nullptr;

  if (NS_IsMainThread() && gParentProcessBlocker == this) {
    gParentProcessBlocker = nullptr;
  }

  SetIsCrossProcess(false);

  mLink = nullptr;

  if (mChannelErrorTask) {
    mChannelErrorTask->Cancel();
    mChannelErrorTask = nullptr;
  }

  if (mFlushLazySendTask) {
    mFlushLazySendTask->Cancel();
    mFlushLazySendTask = nullptr;
  }

  // Free up any memory used by pending messages.
  mPending.clear();

  mMaybeDeferredPendingCount = 0;
}

bool MessageChannel::Open(ScopedPort aPort, Side aSide,
                          const nsID& aMessageChannelId,
                          nsISerialEventTarget* aEventTarget) {
  nsCOMPtr<nsISerialEventTarget> eventTarget =
      aEventTarget ? aEventTarget : GetCurrentSerialEventTarget();
  MOZ_RELEASE_ASSERT(eventTarget,
                     "Must open MessageChannel on a nsISerialEventTarget");
  MOZ_RELEASE_ASSERT(eventTarget->IsOnCurrentThread(),
                     "Must open MessageChannel from worker thread");

  auto shutdownTask = MakeRefPtr<WorkerTargetShutdownTask>(eventTarget, this);
  nsresult rv = eventTarget->RegisterShutdownTask(shutdownTask);
  MOZ_ASSERT(rv != NS_ERROR_NOT_IMPLEMENTED,
             "target for MessageChannel must support shutdown tasks");
  if (rv == NS_ERROR_UNEXPECTED) {
    // If shutdown tasks have already started running, dispatch our shutdown
    // task manually.
    NS_WARNING("Opening MessageChannel on EventTarget in shutdown");
    rv = eventTarget->Dispatch(shutdownTask->AsRunnable());
  }
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv),
                     "error registering ShutdownTask for MessageChannel");

  {
    MonitorAutoLock lock(*mMonitor);
    MOZ_RELEASE_ASSERT(!mLink, "Open() called > once");
    MOZ_RELEASE_ASSERT(ChannelClosed == mChannelState, "Not currently closed");
    MOZ_ASSERT(mSide == UnknownSide);

    mMessageChannelId = aMessageChannelId;
    mWorkerThread = std::move(eventTarget);
    mShutdownTask = shutdownTask;
    mLink = MakeUnique<PortLink>(this, std::move(aPort));
    mChannelState = ChannelConnected;
    mSide = aSide;
  }

  // Notify our listener that the underlying IPC channel has been established.
  // IProtocol will use this callback to create the ActorLifecycleProxy, and
  // perform an `AddRef` call to keep the actor alive until the channel is
  // disconnected.
  //
  // We unlock our monitor before calling `OnIPCChannelOpened` to ensure that
  // any calls back into `MessageChannel` do not deadlock. At this point, we may
  // be receiving messages on the IO thread, however we cannot process them on
  // the worker thread or have notified our listener until after this function
  // returns.
  mListener->OnIPCChannelOpened();
  return true;
}

static Side GetOppSide(Side aSide) {
  switch (aSide) {
    case ChildSide:
      return ParentSide;
    case ParentSide:
      return ChildSide;
    default:
      return UnknownSide;
  }
}

bool MessageChannel::Open(MessageChannel* aTargetChan,
                          nsISerialEventTarget* aEventTarget, Side aSide) {
  // Opens a connection to another thread in the same process.

  MOZ_ASSERT(aTargetChan, "Need a target channel");

  nsID channelId = nsID::GenerateUUID();

  std::pair<ScopedPort, ScopedPort> ports =
      NodeController::GetSingleton()->CreatePortPair();

  // NOTE: This dispatch must be sync as it captures locals by non-owning
  // reference, however we can't use `NS_DispatchAndSpinEventLoopUntilComplete`
  // as that will spin a nested event loop, and doesn't work with certain types
  // of calling event targets.
  base::WaitableEvent event(/* manual_reset */ true,
                            /* initially_signaled */ false);
  MOZ_ALWAYS_SUCCEEDS(aEventTarget->Dispatch(NS_NewCancelableRunnableFunction(
      "ipc::MessageChannel::OpenAsOtherThread", [&]() {
        aTargetChan->Open(std::move(ports.second), GetOppSide(aSide), channelId,
                          aEventTarget);
        event.Signal();
      })));
  bool ok = event.Wait();
  MOZ_RELEASE_ASSERT(ok);

  // Now that the other side has connected, open the port on our side.
  return Open(std::move(ports.first), aSide, channelId);
}

bool MessageChannel::OpenOnSameThread(MessageChannel* aTargetChan,
                                      mozilla::ipc::Side aSide) {
  auto [porta, portb] = NodeController::GetSingleton()->CreatePortPair();

  nsID channelId = nsID::GenerateUUID();

  aTargetChan->mIsSameThreadChannel = true;
  mIsSameThreadChannel = true;

  auto* currentThread = GetCurrentSerialEventTarget();
  return aTargetChan->Open(std::move(portb), GetOppSide(aSide), channelId,
                           currentThread) &&
         Open(std::move(porta), aSide, channelId, currentThread);
}

bool MessageChannel::Send(UniquePtr<Message> aMsg, seqno_t* aSeqno) {
  MOZ_RELEASE_ASSERT(!aMsg->is_sync());
  MOZ_RELEASE_ASSERT(aMsg->nested_level() != IPC::Message::NESTED_INSIDE_SYNC);
  MOZ_RELEASE_ASSERT(aMsg->routing_id() != MSG_ROUTING_NONE);
  AssertWorkerThread();
  mMonitor->AssertNotCurrentThreadOwns();

  AutoSetValue<bool> setOnCxxStack(mOnCxxStack, true);

  if (aMsg->seqno() == 0) {
    aMsg->set_seqno(NextSeqno());
  }
  if (aSeqno) {
    *aSeqno = aMsg->seqno();
  }

  MonitorAutoLock lock(*mMonitor);
  if (!Connected()) {
    ReportConnectionError("Send", aMsg->type());
    return false;
  }

  AddProfilerMarker(*aMsg, MessageDirection::eSending);
  SendMessageToLink(std::move(aMsg));
  return true;
}

void MessageChannel::SendMessageToLink(UniquePtr<Message> aMsg) {
  AssertWorkerThread();
  mMonitor->AssertCurrentThreadOwns();

  // If the channel is not cross-process, there's no reason to be lazy, so we
  // ignore the flag in that case.
  if (aMsg->is_lazy_send() && mIsCrossProcess) {
    // If this is the first lazy message in the queue and our worker thread
    // supports direct task dispatch, dispatch a task to flush messages,
    // ensuring we don't leave them pending forever.
    if (!mFlushLazySendTask) {
      if (nsCOMPtr<nsIDirectTaskDispatcher> dispatcher =
              do_QueryInterface(mWorkerThread)) {
        mFlushLazySendTask = new FlushLazySendMessagesRunnable(this);
        MOZ_ALWAYS_SUCCEEDS(
            dispatcher->DispatchDirectTask(do_AddRef(mFlushLazySendTask)));
      }
    }
    if (mFlushLazySendTask) {
      mFlushLazySendTask->PushMessage(std::move(aMsg));
      return;
    }
  }

  if (mFlushLazySendTask) {
    FlushLazySendMessages();
  }
  mLink->SendMessage(std::move(aMsg));
}

void MessageChannel::FlushLazySendMessages() {
  AssertWorkerThread();
  mMonitor->AssertCurrentThreadOwns();

  // Clean up any SendLazyTask which might be pending.
  auto messages = mFlushLazySendTask->TakeMessages();
  mFlushLazySendTask = nullptr;

  // Send all lazy messages, then clear the queue.
  for (auto& msg : messages) {
    mLink->SendMessage(std::move(msg));
  }
}

class BuildIDsMatchMessage : public IPC::Message {
 public:
  BuildIDsMatchMessage()
      : IPC::Message(MSG_ROUTING_NONE, BUILD_IDS_MATCH_MESSAGE_TYPE) {}
  void Log(const std::string& aPrefix, FILE* aOutf) const {
    fputs("(special `Build IDs match' message)", aOutf);
  }
};

// Send the parent a special async message to confirm when the parent and child
// are of the same buildID. Skips sending the message and returns false if the
// buildIDs don't match. This is a minor variation on
// MessageChannel::Send(Message* aMsg).
bool MessageChannel::SendBuildIDsMatchMessage(const char* aParentBuildID) {
  MOZ_ASSERT(!XRE_IsParentProcess());

  nsCString parentBuildID(aParentBuildID);
  nsCString childBuildID(mozilla::PlatformBuildID());

  if (parentBuildID != childBuildID) {
    // The build IDs didn't match, usually because an update occurred in the
    // background.
    return false;
  }

  auto msg = MakeUnique<BuildIDsMatchMessage>();

  MOZ_RELEASE_ASSERT(!msg->is_sync());
  MOZ_RELEASE_ASSERT(msg->nested_level() != IPC::Message::NESTED_INSIDE_SYNC);

  AssertWorkerThread();
  mMonitor->AssertNotCurrentThreadOwns();
  // Don't check for MSG_ROUTING_NONE.

  MonitorAutoLock lock(*mMonitor);
  if (!Connected()) {
    ReportConnectionError("SendBuildIDsMatchMessage", msg->type());
    return false;
  }

#if defined(MOZ_DEBUG) && defined(ENABLE_TESTS)
  // Technically, the behavior is interesting for any kind of process
  // but when exercising tests, we want to crash only a content process and
  // avoid making noise with other kind of processes crashing
  if (const char* dontSend = PR_GetEnv("MOZ_BUILDID_MATCH_DONTSEND")) {
    if (dontSend[0] == '1') {
      // Bug 1732999: We are going to crash, so we need to advise leak check
      // tooling to avoid intermittent missing leakcheck
      NoteIntentionalCrash(XRE_GetProcessTypeString());
      if (XRE_IsContentProcess()) {
        return false;
      }
    }
  }
#endif

  SendMessageToLink(std::move(msg));
  return true;
}

class CancelMessage : public IPC::Message {
 public:
  explicit CancelMessage(seqno_t transaction)
      : IPC::Message(MSG_ROUTING_NONE, CANCEL_MESSAGE_TYPE) {
    set_transaction_id(transaction);
  }
  static bool Read(const Message* msg) { return true; }
  void Log(const std::string& aPrefix, FILE* aOutf) const {
    fputs("(special `Cancel' message)", aOutf);
  }
};

bool MessageChannel::MaybeInterceptSpecialIOMessage(const Message& aMsg) {
  mMonitor->AssertCurrentThreadOwns();

  if (MSG_ROUTING_NONE == aMsg.routing_id()) {
    if (GOODBYE_MESSAGE_TYPE == aMsg.type()) {
      // We've received a GOODBYE message, close the connection and mark
      // ourselves as "Closing".
      mLink->Close();
      mChannelState = ChannelClosing;
      if (LoggingEnabledFor(mListener->GetProtocolName(), mSide)) {
        printf(
            "[%s %u] NOTE: %s%s actor received `Goodbye' message.  Closing "
            "channel.\n",
            XRE_GeckoProcessTypeToString(XRE_GetProcessType()),
            static_cast<uint32_t>(base::GetCurrentProcId()),
            mListener->GetProtocolName(), StringFromIPCSide(mSide));
      }

      // Notify the worker thread that the connection has been closed, as we
      // will not receive an `OnChannelErrorFromLink` after calling
      // `mLink->Close()`.
      if (AwaitingSyncReply()) {
        NotifyWorkerThread();
      }
      PostErrorNotifyTask();
      return true;
    } else if (CANCEL_MESSAGE_TYPE == aMsg.type()) {
      IPC_LOG("Cancel from message");
      CancelTransaction(aMsg.transaction_id());
      NotifyWorkerThread();
      return true;
    } else if (BUILD_IDS_MATCH_MESSAGE_TYPE == aMsg.type()) {
      IPC_LOG("Build IDs match message");
      mBuildIDsConfirmedMatch = true;
      return true;
    } else if (IMPENDING_SHUTDOWN_MESSAGE_TYPE == aMsg.type()) {
      IPC_LOG("Impending Shutdown received");
      ProcessChild::NotifiedImpendingShutdown();
      return true;
    }
  }
  return false;
}

/* static */
bool MessageChannel::IsAlwaysDeferred(const Message& aMsg) {
  // If a message is not NESTED_INSIDE_CPOW and not sync, then we always defer
  // it.
  return aMsg.nested_level() != IPC::Message::NESTED_INSIDE_CPOW &&
         !aMsg.is_sync();
}

bool MessageChannel::ShouldDeferMessage(const Message& aMsg) {
  // Never defer messages that have the highest nested level, even async
  // ones. This is safe because only the child can send these messages, so
  // they can never nest.
  if (aMsg.nested_level() == IPC::Message::NESTED_INSIDE_CPOW) {
    MOZ_ASSERT(!IsAlwaysDeferred(aMsg));
    return false;
  }

  // Unless they're NESTED_INSIDE_CPOW, we always defer async messages.
  // Note that we never send an async NESTED_INSIDE_SYNC message.
  if (!aMsg.is_sync()) {
    MOZ_RELEASE_ASSERT(aMsg.nested_level() == IPC::Message::NOT_NESTED);
    MOZ_ASSERT(IsAlwaysDeferred(aMsg));
    return true;
  }

  MOZ_ASSERT(!IsAlwaysDeferred(aMsg));

  int msgNestedLevel = aMsg.nested_level();
  int waitingNestedLevel = AwaitingSyncReplyNestedLevel();

  // Always defer if the nested level of the incoming message is less than the
  // nested level of the message we're awaiting.
  if (msgNestedLevel < waitingNestedLevel) return true;

  // Never defer if the message has strictly greater nested level.
  if (msgNestedLevel > waitingNestedLevel) return false;

  // When both sides send sync messages of the same nested level, we resolve the
  // race by dispatching in the child and deferring the incoming message in
  // the parent. However, the parent still needs to dispatch nested sync
  // messages.
  //
  // Deferring in the parent only sort of breaks message ordering. When the
  // child's message comes in, we can pretend the child hasn't quite
  // finished sending it yet. Since the message is sync, we know that the
  // child hasn't moved on yet.
  return mSide == ParentSide &&
         aMsg.transaction_id() != CurrentNestedInsideSyncTransaction();
}

class IPCFlowMarker : public BaseMarkerType<IPCFlowMarker> {
 public:
  static constexpr const char* Name = "IPCFlowMarker";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {
          "name",
          MS::InputType::CString,
          "Details",
          MS::Format::String,
      },
      {
          "flow",
          MS::InputType::Uint64,
          "Flow",
          MS::Format::Flow,
      }};

  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable};
  static constexpr const char* TableLabel =
      "{marker.data.name}(flow={marker.data.flow})";
  static constexpr const char* ChartLabel = "{marker.name}";

  static constexpr MS::ETWMarkerGroup Group = MS::ETWMarkerGroup::Generic;

  static constexpr bool IsStackBased = true;

  static void StreamJSONMarkerData(
      mozilla::baseprofiler::SpliceableJSONWriter& aWriter,
      IPC::Message::msgid_t aMessageType, Flow aFlow) {
    aWriter.StringProperty(
        "name",
        mozilla::MakeStringSpan(IPC::StringFromIPCMessageType(aMessageType)));
    aWriter.FlowProperty("flow", aFlow);
  }
};

static uint64_t LossyNarrowChannelId(const nsID& aID) {
  // We xor both halves of the UUID together so that the parts of the id where
  // the variant (m2) and version (m3[0]) get xored with random bits from the
  // other halve.
  uint64_t bits[2];
  memcpy(&bits, &aID, sizeof(bits));

  return bits[0] ^ bits[1];
}

void MessageChannel::OnMessageReceivedFromLink(UniquePtr<Message> aMsg) {
  mMonitor->AssertCurrentThreadOwns();
  MOZ_ASSERT(mChannelState == ChannelConnected);

  if (MaybeInterceptSpecialIOMessage(*aMsg)) {
    return;
  }

  mListener->OnChannelReceivedMessage(*aMsg);

  // If we're awaiting a sync reply, we know that it needs to be immediately
  // handled to unblock us.
  if (aMsg->is_sync() && aMsg->is_reply()) {
    IPC_LOG("Received reply seqno=%" PRId64 " xid=%" PRId64, aMsg->seqno(),
            aMsg->transaction_id());

    if (aMsg->seqno() == mTimedOutMessageSeqno) {
      // Drop the message, but allow future sync messages to be sent.
      IPC_LOG("Received reply to timedout message; igoring; xid=%" PRId64,
              mTimedOutMessageSeqno);
      EndTimeout();
      return;
    }

    MOZ_RELEASE_ASSERT(AwaitingSyncReply());
    MOZ_RELEASE_ASSERT(!mTimedOutMessageSeqno);

    mTransactionStack->HandleReply(std::move(aMsg));
    NotifyWorkerThread();
    return;
  }

  // Nested messages cannot be compressed.
  MOZ_RELEASE_ASSERT(aMsg->compress_type() == IPC::Message::COMPRESSION_NONE ||
                     aMsg->nested_level() == IPC::Message::NOT_NESTED);

  if (aMsg->compress_type() == IPC::Message::COMPRESSION_ENABLED &&
      !mPending.isEmpty()) {
    auto* last = mPending.getLast();
    last->AssertMonitorHeld(*mMonitor);
    bool compress = last->Msg()->type() == aMsg->type() &&
                    last->Msg()->routing_id() == aMsg->routing_id();
    if (compress) {
      // This message type has compression enabled, and the back of the
      // queue was the same message type and routed to the same destination.
      // Replace it with the newer message.
      MOZ_RELEASE_ASSERT(last->Msg()->compress_type() ==
                         IPC::Message::COMPRESSION_ENABLED);
      last->Msg() = std::move(aMsg);
      return;
    }
  } else if (aMsg->compress_type() == IPC::Message::COMPRESSION_ALL &&
             !mPending.isEmpty()) {
    for (MessageTask* p = mPending.getLast(); p; p = p->getPrevious()) {
      p->AssertMonitorHeld(*mMonitor);
      if (p->Msg()->type() == aMsg->type() &&
          p->Msg()->routing_id() == aMsg->routing_id()) {
        // This message type has compression enabled, and the queue
        // holds a message with the same message type and routed to the
        // same destination. Erase it. Note that, since we always
        // compress these redundancies, There Can Be Only One.
        MOZ_RELEASE_ASSERT(p->Msg()->compress_type() ==
                           IPC::Message::COMPRESSION_ALL);
        MOZ_RELEASE_ASSERT(IsAlwaysDeferred(*p->Msg()));
        p->remove();
        break;
      }
    }
  }

  bool alwaysDeferred = IsAlwaysDeferred(*aMsg);

  bool shouldWakeUp = AwaitingSyncReply() && !ShouldDeferMessage(*aMsg);

  IPC_LOG("Receive from link; seqno=%" PRId64 ", xid=%" PRId64
          ", shouldWakeUp=%d",
          aMsg->seqno(), aMsg->transaction_id(), shouldWakeUp);

  struct FlowMarkerDispatch {
    FlowMarkerDispatch(msgid_t type, Flow flow) : type(type), flow(flow) {
      if (profiler_feature_active(ProfilerFeature::Flows)) {
        options.Set(MarkerTiming::InstantNow());
      }
    }
    ~FlowMarkerDispatch() {
      if (!options.IsTimingUnspecified()) {
        options.TimingRef().SetIntervalEnd();
        profiler_add_marker("IPCDispatch", baseprofiler::category::OTHER,
                            std::move(options), IPCFlowMarker{}, type, flow);
      }
    }
    msgid_t type;
    Flow flow;
    MarkerOptions options;
  };

  // We want this marker to span the time when Post is called so that we
  // can inherit the connection to the runnable.
  FlowMarkerDispatch marker(
      aMsg->type(), Flow::Global(static_cast<uint64_t>(aMsg->seqno()) ^
                                 LossyNarrowChannelId(mMessageChannelId)));

  // There are two cases we're concerned about, relating to the state of the
  // worker thread:
  //
  // (1) We are waiting on a sync reply - worker thread is blocked on the
  //     IPC monitor.
  //   - If the message is NESTED_INSIDE_SYNC, we wake up the worker thread to
  //     deliver the message depending on ShouldDeferMessage. Otherwise, we
  //     leave it in the mPending queue, posting a task to the worker event
  //     loop, where it will be processed once the synchronous reply has been
  //     received.
  //
  // (2) We are not waiting on a reply.
  //   - We post a task to the worker event loop.
  //
  // Note that, we may notify the worker thread even though the monitor is not
  // blocked. This is okay, since we always check for pending events before
  // blocking again.

  RefPtr<MessageTask> task = new MessageTask(this, std::move(aMsg));
  mPending.insertBack(task);

  if (!alwaysDeferred) {
    mMaybeDeferredPendingCount++;
  }

  if (shouldWakeUp) {
    NotifyWorkerThread();
  }

  // Although we usually don't need to post a message task if
  // shouldWakeUp is true, it's easier to post anyway than to have to
  // guarantee that every Send call processes everything it's supposed to
  // before returning.
  task->AssertMonitorHeld(*mMonitor);
  task->Post();
}

void MessageChannel::PeekMessages(
    const std::function<bool(const Message& aMsg)>& aInvoke) {
  // FIXME: We shouldn't be holding the lock for aInvoke!
  MonitorAutoLock lock(*mMonitor);

  for (MessageTask* it : mPending) {
    it->AssertMonitorHeld(*mMonitor);
    const Message& msg = *it->Msg();
    if (!aInvoke(msg)) {
      break;
    }
  }
}

void MessageChannel::ProcessPendingRequests(
    ActorLifecycleProxy* aProxy, AutoEnterTransaction& aTransaction) {
  mMonitor->AssertCurrentThreadOwns();

  AssertMaybeDeferredCountCorrect();
  if (mMaybeDeferredPendingCount == 0) {
    return;
  }

  IPC_LOG("ProcessPendingRequests for seqno=%" PRId64 ", xid=%" PRId64,
          aTransaction.SequenceNumber(), aTransaction.TransactionID());

  // Loop until there aren't any more nested messages to process.
  for (;;) {
    // If we canceled during ProcessPendingRequest, then we need to leave
    // immediately because the results of ShouldDeferMessage will be
    // operating with weird state (as if no Send is in progress). That could
    // cause even NOT_NESTED sync messages to be processed (but not
    // NOT_NESTED async messages), which would break message ordering.
    if (aTransaction.IsCanceled()) {
      return;
    }

    Vector<UniquePtr<Message>> toProcess;

    for (MessageTask* p = mPending.getFirst(); p;) {
      p->AssertMonitorHeld(*mMonitor);
      UniquePtr<Message>& msg = p->Msg();

      MOZ_RELEASE_ASSERT(!aTransaction.IsCanceled(),
                         "Calling ShouldDeferMessage when cancelled");
      bool defer = ShouldDeferMessage(*msg);

      // Only log the interesting messages.
      if (msg->is_sync() ||
          msg->nested_level() == IPC::Message::NESTED_INSIDE_CPOW) {
        IPC_LOG("ShouldDeferMessage(seqno=%" PRId64 ") = %d", msg->seqno(),
                defer);
      }

      if (!defer) {
        MOZ_ASSERT(!IsAlwaysDeferred(*msg));

        if (!toProcess.append(std::move(msg))) MOZ_CRASH();

        mMaybeDeferredPendingCount--;

        p = p->removeAndGetNext();
        continue;
      }
      p = p->getNext();
    }

    if (toProcess.empty()) {
      break;
    }

    // Processing these messages could result in more messages, so we
    // loop around to check for more afterwards.

    for (auto& msg : toProcess) {
      ProcessPendingRequest(aProxy, std::move(msg));
    }
  }

  AssertMaybeDeferredCountCorrect();
}

bool MessageChannel::Send(UniquePtr<Message> aMsg, UniquePtr<Message>* aReply) {
  mozilla::TimeStamp start = TimeStamp::Now();

  // Sanity checks.
  AssertWorkerThread();
  mMonitor->AssertNotCurrentThreadOwns();
  MOZ_RELEASE_ASSERT(!mIsSameThreadChannel,
                     "sync send over same-thread channel will deadlock!");

  RefPtr<ActorLifecycleProxy> proxy = Listener()->GetLifecycleProxy();

#ifdef XP_WIN
  SyncStackFrame frame(this);
  NeuteredWindowRegion neuteredRgn(mFlags &
                                   REQUIRE_DEFERRED_MESSAGE_PROTECTION);
#endif

  AutoSetValue<bool> setOnCxxStack(mOnCxxStack, true);

  MonitorAutoLock lock(*mMonitor);

  if (mTimedOutMessageSeqno) {
    // Don't bother sending another sync message if a previous one timed out
    // and we haven't received a reply for it. Once the original timed-out
    // message receives a reply, we'll be able to send more sync messages
    // again.
    IPC_LOG("Send() failed due to previous timeout");
    mLastSendError = SyncSendError::PreviousTimeout;
    return false;
  }

  if (DispatchingSyncMessageNestedLevel() == IPC::Message::NOT_NESTED &&
      aMsg->nested_level() > IPC::Message::NOT_NESTED) {
    // Don't allow sending CPOWs while we're dispatching a sync message.
    IPC_LOG("Nested level forbids send");
    mLastSendError = SyncSendError::SendingCPOWWhileDispatchingSync;
    return false;
  }

  if (DispatchingSyncMessageNestedLevel() == IPC::Message::NESTED_INSIDE_CPOW ||
      DispatchingAsyncMessageNestedLevel() ==
          IPC::Message::NESTED_INSIDE_CPOW) {
    // Generally only the parent dispatches urgent messages. And the only
    // sync messages it can send are NESTED_INSIDE_SYNC. Mainly we want to
    // ensure here that we don't return false for non-CPOW messages.
    MOZ_RELEASE_ASSERT(aMsg->nested_level() ==
                       IPC::Message::NESTED_INSIDE_SYNC);
    IPC_LOG("Sending while dispatching urgent message");
    mLastSendError = SyncSendError::SendingCPOWWhileDispatchingUrgent;
    return false;
  }

  if (aMsg->nested_level() < DispatchingSyncMessageNestedLevel() ||
      aMsg->nested_level() < AwaitingSyncReplyNestedLevel()) {
    MOZ_RELEASE_ASSERT(DispatchingSyncMessage() || DispatchingAsyncMessage());
    IPC_LOG("Cancel from Send");
    auto cancel =
        MakeUnique<CancelMessage>(CurrentNestedInsideSyncTransaction());
    CancelTransaction(CurrentNestedInsideSyncTransaction());
    SendMessageToLink(std::move(cancel));
  }

  IPC_ASSERT(aMsg->is_sync(), "can only Send() sync messages here");

  IPC_ASSERT(aMsg->nested_level() >= DispatchingSyncMessageNestedLevel(),
             "can't send sync message of a lesser nested level than what's "
             "being dispatched");
  IPC_ASSERT(AwaitingSyncReplyNestedLevel() <= aMsg->nested_level(),
             "nested sync message sends must be of increasing nested level");
  IPC_ASSERT(
      DispatchingSyncMessageNestedLevel() != IPC::Message::NESTED_INSIDE_CPOW,
      "not allowed to send messages while dispatching urgent messages");

  IPC_ASSERT(
      DispatchingAsyncMessageNestedLevel() != IPC::Message::NESTED_INSIDE_CPOW,
      "not allowed to send messages while dispatching urgent messages");

  if (!Connected()) {
    ReportConnectionError("SendAndWait", aMsg->type());
    mLastSendError = SyncSendError::NotConnectedBeforeSend;
    return false;
  }

  aMsg->set_seqno(NextSeqno());

  seqno_t seqno = aMsg->seqno();
  int nestedLevel = aMsg->nested_level();
  msgid_t replyType = aMsg->type() + 1;

  AutoEnterTransaction* stackTop = mTransactionStack;

  // If the most recent message on the stack is NESTED_INSIDE_SYNC, then our
  // message should nest inside that and we use the same transaction
  // ID. Otherwise we need a new transaction ID (so we use the seqno of the
  // message we're sending).
  bool nest =
      stackTop && stackTop->NestedLevel() == IPC::Message::NESTED_INSIDE_SYNC;
  seqno_t transaction = nest ? stackTop->TransactionID() : seqno;
  aMsg->set_transaction_id(transaction);

  AutoEnterTransaction transact(this, seqno, transaction, nestedLevel);

  IPC_LOG("Send seqno=%" PRId64 ", xid=%" PRId64, seqno, transaction);

  // aMsg will be destroyed soon, let's keep its type.
  const char* msgName = aMsg->name();
  const msgid_t msgType = aMsg->type();

  AddProfilerMarker(*aMsg, MessageDirection::eSending);
  SendMessageToLink(std::move(aMsg));

  while (true) {
    MOZ_RELEASE_ASSERT(!transact.IsCanceled());
    ProcessPendingRequests(proxy, transact);
    if (transact.IsComplete()) {
      break;
    }
    if (!Connected()) {
      ReportConnectionError("Send", msgType);
      mLastSendError = SyncSendError::DisconnectedDuringSend;
      return false;
    }

    MOZ_RELEASE_ASSERT(!mTimedOutMessageSeqno);
    MOZ_RELEASE_ASSERT(!transact.IsComplete());
    MOZ_RELEASE_ASSERT(mTransactionStack == &transact);

    bool maybeTimedOut = !WaitForSyncNotify();

    if (mListener->NeedArtificialSleep()) {
      MonitorAutoUnlock unlock(*mMonitor);
      mListener->ArtificialSleep();
    }

    if (!Connected()) {
      ReportConnectionError("SendAndWait", msgType);
      mLastSendError = SyncSendError::DisconnectedDuringSend;
      return false;
    }

    if (transact.IsCanceled()) {
      break;
    }

    MOZ_RELEASE_ASSERT(mTransactionStack == &transact);

    // We only time out a message if it initiated a new transaction (i.e.,
    // if neither side has any other message Sends on the stack).
    bool canTimeOut = transact.IsBottom();
    if (maybeTimedOut && canTimeOut && !ShouldContinueFromTimeout()) {
      // Since ShouldContinueFromTimeout drops the lock, we need to
      // re-check all our conditions here. We shouldn't time out if any of
      // these things happen because there won't be a reply to the timed
      // out message in these cases.
      if (transact.IsComplete()) {
        break;
      }

      IPC_LOG("Timing out Send: xid=%" PRId64, transaction);

      mTimedOutMessageSeqno = seqno;
      mTimedOutMessageNestedLevel = nestedLevel;
      mLastSendError = SyncSendError::TimedOut;
      return false;
    }

    if (transact.IsCanceled()) {
      break;
    }
  }

  if (transact.IsCanceled()) {
    IPC_LOG("Other side canceled seqno=%" PRId64 ", xid=%" PRId64, seqno,
            transaction);
    mLastSendError = SyncSendError::CancelledAfterSend;
    return false;
  }

  if (transact.IsError()) {
    IPC_LOG("Error: seqno=%" PRId64 ", xid=%" PRId64, seqno, transaction);
    mLastSendError = SyncSendError::ReplyError;
    return false;
  }

  uint32_t latencyMs = round((TimeStamp::Now() - start).ToMilliseconds());
  IPC_LOG("Got reply: seqno=%" PRId64 ", xid=%" PRId64
          ", msgName=%s, latency=%ums",
          seqno, transaction, msgName, latencyMs);

  UniquePtr<Message> reply = transact.GetReply();

  MOZ_RELEASE_ASSERT(reply);
  MOZ_RELEASE_ASSERT(reply->is_reply(), "expected reply");
  MOZ_RELEASE_ASSERT(!reply->is_reply_error());
  MOZ_RELEASE_ASSERT(reply->seqno() == seqno);
  MOZ_RELEASE_ASSERT(reply->type() == replyType, "wrong reply type");
  MOZ_RELEASE_ASSERT(reply->is_sync());

  AddProfilerMarker(*reply, MessageDirection::eReceiving);

  *aReply = std::move(reply);
  return true;
}

bool MessageChannel::HasPendingEvents() {
  AssertWorkerThread();
  mMonitor->AssertCurrentThreadOwns();
  return ConnectedOrClosing() && !mPending.isEmpty();
}

bool MessageChannel::ProcessPendingRequest(ActorLifecycleProxy* aProxy,
                                           UniquePtr<Message> aUrgent) {
  AssertWorkerThread();
  mMonitor->AssertCurrentThreadOwns();

  IPC_LOG("Process pending: seqno=%" PRId64 ", xid=%" PRId64, aUrgent->seqno(),
          aUrgent->transaction_id());

  // keep the error relevant information
  msgid_t msgType = aUrgent->type();

  DispatchMessage(aProxy, std::move(aUrgent));
  if (!ConnectedOrClosing()) {
    ReportConnectionError("ProcessPendingRequest", msgType);
    return false;
  }

  return true;
}

bool MessageChannel::ShouldRunMessage(const Message& aMsg) {
  if (!mTimedOutMessageSeqno) {
    return true;
  }

  // If we've timed out a message and we're awaiting the reply to the timed
  // out message, we have to be careful what messages we process. Here's what
  // can go wrong:
  // 1. child sends a NOT_NESTED sync message S
  // 2. parent sends a NESTED_INSIDE_SYNC sync message H at the same time
  // 3. parent times out H
  // 4. child starts processing H and sends a NESTED_INSIDE_SYNC message H'
  //    nested within the same transaction
  // 5. parent dispatches S and sends reply
  // 6. child asserts because it instead expected a reply to H'.
  //
  // To solve this, we refuse to process S in the parent until we get a reply
  // to H. More generally, let the timed out message be M. We don't process a
  // message unless the child would need the response to that message in order
  // to process M. Those messages are the ones that have a higher nested level
  // than M or that are part of the same transaction as M.
  if (aMsg.nested_level() < mTimedOutMessageNestedLevel ||
      (aMsg.nested_level() == mTimedOutMessageNestedLevel &&
       aMsg.transaction_id() != mTimedOutMessageSeqno)) {
    return false;
  }

  return true;
}

void MessageChannel::RunMessage(ActorLifecycleProxy* aProxy,
                                MessageTask& aTask) {
  AssertWorkerThread();
  mMonitor->AssertCurrentThreadOwns();
  aTask.AssertMonitorHeld(*mMonitor);

  UniquePtr<Message>& msg = aTask.Msg();

  if (!ConnectedOrClosing()) {
    ReportConnectionError("RunMessage", msg->type());
    return;
  }

  // Check that we're going to run the first message that's valid to run.
#if 0
#  ifdef DEBUG
    for (MessageTask* task : mPending) {
        if (task == &aTask) {
            break;
        }

        MOZ_ASSERT(!ShouldRunMessage(*task->Msg()) ||
                   aTask.Msg()->priority() != task->Msg()->priority());

    }
#  endif
#endif

  if (!ShouldRunMessage(*msg)) {
    return;
  }

  MOZ_RELEASE_ASSERT(aTask.isInList());
  aTask.remove();

  if (!IsAlwaysDeferred(*msg)) {
    mMaybeDeferredPendingCount--;
  }

  DispatchMessage(aProxy, std::move(msg));
}

NS_IMPL_ISUPPORTS_INHERITED(MessageChannel::MessageTask, CancelableRunnable,
                            nsIRunnablePriority, nsIRunnableIPCMessageType)

static uint32_t ToRunnablePriority(IPC::Message::PriorityValue aPriority) {
  switch (aPriority) {
    case IPC::Message::LOW_PRIORITY:
      return nsIRunnablePriority::PRIORITY_LOW;
    case IPC::Message::NORMAL_PRIORITY:
      return nsIRunnablePriority::PRIORITY_NORMAL;
    case IPC::Message::INPUT_PRIORITY:
      return nsIRunnablePriority::PRIORITY_INPUT_HIGH;
    case IPC::Message::VSYNC_PRIORITY:
      return nsIRunnablePriority::PRIORITY_VSYNC;
    case IPC::Message::MEDIUMHIGH_PRIORITY:
      return nsIRunnablePriority::PRIORITY_MEDIUMHIGH;
    case IPC::Message::CONTROL_PRIORITY:
      return nsIRunnablePriority::PRIORITY_CONTROL;
    default:
      MOZ_ASSERT_UNREACHABLE();
      return nsIRunnablePriority::PRIORITY_NORMAL;
  }
}

MessageChannel::MessageTask::MessageTask(MessageChannel* aChannel,
                                         UniquePtr<Message> aMessage)
    : CancelableRunnable(aMessage->name()),
      mMonitor(aChannel->mMonitor),
      mChannel(aChannel),
      mMessage(std::move(aMessage)),
      mPriority(ToRunnablePriority(mMessage->priority())),
      mScheduled(false)
#ifdef FUZZING_SNAPSHOT
      ,
      mIsFuzzMsg(mMessage->IsFuzzMsg()),
      mFuzzStopped(false)
#endif
{
  MOZ_DIAGNOSTIC_ASSERT(mMessage, "message may not be null");
#ifdef FUZZING_SNAPSHOT
  if (mIsFuzzMsg) {
    MOZ_FUZZING_IPC_MT_CTOR();
  }
#endif
}

MessageChannel::MessageTask::~MessageTask() {
#ifdef FUZZING_SNAPSHOT
  // We track fuzzing messages until their run is complete. To make sure
  // that we don't miss messages that are for some reason destroyed without
  // being run (e.g. canceled), we catch this condition in the destructor.
  if (mIsFuzzMsg && !mFuzzStopped) {
    MOZ_FUZZING_IPC_MT_STOP();
  } else if (!mIsFuzzMsg && !fuzzing::Nyx::instance().started()) {
    MOZ_FUZZING_IPC_PRE_FUZZ_MT_STOP();
  }
#endif
}

nsresult MessageChannel::MessageTask::Run() {
  mMonitor->AssertNotCurrentThreadOwns();

  // Drop the toplevel actor's lifecycle proxy outside of our monitor if we take
  // it, as destroying our ActorLifecycleProxy reference can acquire the
  // monitor.
  RefPtr<ActorLifecycleProxy> proxy;

  MonitorAutoLock lock(*mMonitor);

  // In case we choose not to run this message, we may need to be able to Post
  // it again.
  mScheduled = false;

  if (!isInList()) {
    return NS_OK;
  }

  Channel()->AssertWorkerThread();
  mMonitor->AssertSameMonitor(*Channel()->mMonitor);

#ifdef FUZZING_SNAPSHOT
  if (!mIsFuzzMsg) {
    if (fuzzing::Nyx::instance().started() && XRE_IsParentProcess() &&
        Channel()->IsCrossProcess()) {
      // Once we started fuzzing, prevent non-fuzzing tasks from being
      // run and potentially blocking worker threads.
      //
      // TODO: This currently blocks all MessageTasks from running, not
      // just those belonging to the target process pair. We currently
      // do this for performance reasons, but it should be re-evaluated
      // at a later stage when we found a better snapshot point.
      return NS_OK;
    }
    // Record all running tasks prior to fuzzing, so we can wait for
    // them to settle before snapshotting.
    MOZ_FUZZING_IPC_PRE_FUZZ_MT_RUN();
  }
#endif

  proxy = Channel()->Listener()->GetLifecycleProxy();
  Channel()->RunMessage(proxy, *this);

#ifdef FUZZING_SNAPSHOT
  if (mIsFuzzMsg && !mFuzzStopped) {
    MOZ_FUZZING_IPC_MT_STOP();
    mFuzzStopped = true;
  }
#endif
  return NS_OK;
}

// Warning: This method removes the receiver from whatever list it might be in.
nsresult MessageChannel::MessageTask::Cancel() {
  mMonitor->AssertNotCurrentThreadOwns();

  MonitorAutoLock lock(*mMonitor);

  if (!isInList()) {
    return NS_OK;
  }

  Channel()->AssertWorkerThread();
  mMonitor->AssertSameMonitor(*Channel()->mMonitor);
  if (!IsAlwaysDeferred(*Msg())) {
    Channel()->mMaybeDeferredPendingCount--;
  }

  remove();

#ifdef FUZZING_SNAPSHOT
  if (mIsFuzzMsg && !mFuzzStopped) {
    MOZ_FUZZING_IPC_MT_STOP();
    mFuzzStopped = true;
  }
#endif

  return NS_OK;
}

void MessageChannel::MessageTask::Post() {
  mMonitor->AssertCurrentThreadOwns();
  mMonitor->AssertSameMonitor(*Channel()->mMonitor);
  MOZ_RELEASE_ASSERT(!mScheduled);
  MOZ_RELEASE_ASSERT(isInList());

  mScheduled = true;

  Channel()->mWorkerThread->Dispatch(do_AddRef(this));
}

NS_IMETHODIMP
MessageChannel::MessageTask::GetPriority(uint32_t* aPriority) {
  *aPriority = mPriority;
  return NS_OK;
}

NS_IMETHODIMP
MessageChannel::MessageTask::GetType(uint32_t* aType) {
  mMonitor->AssertNotCurrentThreadOwns();

  MonitorAutoLock lock(*mMonitor);
  if (!mMessage) {
    // If mMessage has been moved already elsewhere, we can't know what the type
    // has been.
    return NS_ERROR_FAILURE;
  }

  *aType = mMessage->type();
  return NS_OK;
}

void MessageChannel::DispatchMessage(ActorLifecycleProxy* aProxy,
                                     UniquePtr<Message> aMsg) {
  AssertWorkerThread();
  mMonitor->AssertCurrentThreadOwns();

  Maybe<AutoNoJSAPI> nojsapi;
  if (NS_IsMainThread() && CycleCollectedJSContext::Get()) {
    nojsapi.emplace();
  }

  UniquePtr<Message> reply;

#ifdef FUZZING_SNAPSHOT
  if (IsCrossProcess()) {
    aMsg = mozilla::fuzzing::IPCFuzzController::instance().replaceIPCMessage(
        std::move(aMsg));
  }
#endif

  IPC_LOG("DispatchMessage: seqno=%" PRId64 ", xid=%" PRId64, aMsg->seqno(),
          aMsg->transaction_id());
  AddProfilerMarker(*aMsg, MessageDirection::eReceiving);

  {
    AutoEnterTransaction transaction(this, *aMsg);

    seqno_t id = aMsg->transaction_id();
    MOZ_RELEASE_ASSERT(!aMsg->is_sync() || id == transaction.TransactionID());

    {
      MonitorAutoUnlock unlock(*mMonitor);
      AutoSetValue<bool> setOnCxxStack(mOnCxxStack, true);

      mListener->ArtificialSleep();

      if (aMsg->is_sync()) {
        DispatchSyncMessage(aProxy, *aMsg, reply);
      } else {
        DispatchAsyncMessage(aProxy, *aMsg);
      }

      mListener->ArtificialSleep();
    }

    if (reply && transaction.IsCanceled()) {
      // The transaction has been canceled. Don't send a reply.
      IPC_LOG("Nulling out reply due to cancellation, seqno=%" PRId64
              ", xid=%" PRId64,
              aMsg->seqno(), id);
      reply = nullptr;
    }
  }

#ifdef FUZZING_SNAPSHOT
  if (aMsg->IsFuzzMsg()) {
    mozilla::fuzzing::IPCFuzzController::instance().syncAfterReplace();
  }
#endif

  if (reply && ChannelConnected == mChannelState) {
    IPC_LOG("Sending reply seqno=%" PRId64 ", xid=%" PRId64, aMsg->seqno(),
            aMsg->transaction_id());
    AddProfilerMarker(*reply, MessageDirection::eSending);

    SendMessageToLink(std::move(reply));
  }
}

void MessageChannel::DispatchSyncMessage(ActorLifecycleProxy* aProxy,
                                         const Message& aMsg,
                                         UniquePtr<Message>& aReply) {
  AssertWorkerThread();

  int nestedLevel = aMsg.nested_level();

  MOZ_RELEASE_ASSERT(nestedLevel == IPC::Message::NOT_NESTED ||
                     NS_IsMainThread());

  MessageChannel* dummy;
  MessageChannel*& blockingVar =
      mSide == ChildSide && NS_IsMainThread() ? gParentProcessBlocker : dummy;

  Result rv;
  {
    AutoSetValue<MessageChannel*> blocked(blockingVar, this);
    rv = aProxy->Get()->OnMessageReceived(aMsg, aReply);
  }

  if (!MaybeHandleError(rv, aMsg, "DispatchSyncMessage")) {
    aReply = Message::ForSyncDispatchError(aMsg.nested_level());
  }
  aReply->set_seqno(aMsg.seqno());
  aReply->set_transaction_id(aMsg.transaction_id());
}

void MessageChannel::DispatchAsyncMessage(ActorLifecycleProxy* aProxy,
                                          const Message& aMsg) {
  AssertWorkerThread();
  MOZ_RELEASE_ASSERT(!aMsg.is_sync());

  if (aMsg.routing_id() == MSG_ROUTING_NONE) {
    NS_WARNING("unhandled special message!");
    MaybeHandleError(MsgNotKnown, aMsg, "DispatchAsyncMessage");
    return;
  }

  Result rv;
  {
    int nestedLevel = aMsg.nested_level();
    AutoSetValue<bool> async(mDispatchingAsyncMessage, true);
    AutoSetValue<int> nestedLevelSet(mDispatchingAsyncMessageNestedLevel,
                                     nestedLevel);
    rv = aProxy->Get()->OnMessageReceived(aMsg);
  }
  MaybeHandleError(rv, aMsg, "DispatchAsyncMessage");
}

void MessageChannel::EnqueuePendingMessages() {
  AssertWorkerThread();
  mMonitor->AssertCurrentThreadOwns();

  // XXX performance tuning knob: could process all or k pending
  // messages here, rather than enqueuing for later processing

  RepostAllMessages();
}

bool MessageChannel::WaitResponse(bool aWaitTimedOut) {
  AssertWorkerThread();
  if (aWaitTimedOut) {
    if (mInTimeoutSecondHalf) {
      // We've really timed out this time.
      return false;
    }
    // Try a second time.
    mInTimeoutSecondHalf = true;
  } else {
    mInTimeoutSecondHalf = false;
  }
  return true;
}

#ifndef XP_WIN
bool MessageChannel::WaitForSyncNotify() {
  AssertWorkerThread();
#  ifdef DEBUG
  // WARNING: We don't release the lock here. We can't because the link
  // could signal at this time and we would miss it. Instead we require
  // ArtificialTimeout() to be extremely simple.
  if (mListener->ArtificialTimeout()) {
    return false;
  }
#  endif

  MOZ_RELEASE_ASSERT(!mIsSameThreadChannel,
                     "Wait on same-thread channel will deadlock!");

  TimeDuration timeout = (kNoTimeout == mTimeoutMs)
                             ? TimeDuration::Forever()
                             : TimeDuration::FromMilliseconds(mTimeoutMs);
  CVStatus status = mMonitor->Wait(timeout);

  // If the timeout didn't expire, we know we received an event. The
  // converse is not true.
  return WaitResponse(status == CVStatus::Timeout);
}

void MessageChannel::NotifyWorkerThread() { mMonitor->Notify(); }
#endif

bool MessageChannel::ShouldContinueFromTimeout() {
  AssertWorkerThread();
  mMonitor->AssertCurrentThreadOwns();

  bool cont;
  {
    MonitorAutoUnlock unlock(*mMonitor);
    cont = mListener->ShouldContinueFromReplyTimeout();
    mListener->ArtificialSleep();
  }

  static enum {
    UNKNOWN,
    NOT_DEBUGGING,
    DEBUGGING
  } sDebuggingChildren = UNKNOWN;

  if (sDebuggingChildren == UNKNOWN) {
    sDebuggingChildren =
        getenv("MOZ_DEBUG_CHILD_PROCESS") || getenv("MOZ_DEBUG_CHILD_PAUSE")
            ? DEBUGGING
            : NOT_DEBUGGING;
  }
  if (sDebuggingChildren == DEBUGGING) {
    return true;
  }

  return cont;
}

void MessageChannel::SetReplyTimeoutMs(int32_t aTimeoutMs) {
  // Set channel timeout value. Since this is broken up into
  // two period, the minimum timeout value is 2ms.
  AssertWorkerThread();
  mTimeoutMs =
      (aTimeoutMs <= 0) ? kNoTimeout : (int32_t)ceil((double)aTimeoutMs / 2.0);
}

void MessageChannel::ReportConnectionError(const char* aFunctionName,
                                           const uint32_t aMsgType) const {
  AssertWorkerThread();
  mMonitor->AssertCurrentThreadOwns();

  const char* errorMsg = nullptr;
  switch (mChannelState) {
    case ChannelClosed:
      errorMsg = "Closed channel: cannot send/recv";
      break;
    case ChannelClosing:
      errorMsg = "Channel closing: too late to send, messages will be lost";
      break;
    case ChannelError:
      errorMsg = "Channel error: cannot send/recv";
      break;

    default:
      MOZ_CRASH("unreached");
  }

  // IPC connection errors are fairly common, especially "Channel closing: too
  // late to send/recv, messages will be lost", so shouldn't be being reported
  // on release builds, as that's misleading as to their severity.
  NS_WARNING(nsPrintfCString("IPC Connection Error: [%s][%s] %s(msgname=%s) %s",
                             StringFromIPCSide(mSide), mName, aFunctionName,
                             IPC::StringFromIPCMessageType(aMsgType), errorMsg)
                 .get());

  MonitorAutoUnlock unlock(*mMonitor);
  mListener->ProcessingError(MsgDropped, errorMsg);
}

bool MessageChannel::MaybeHandleError(Result code, const Message& aMsg,
                                      const char* channelName) {
  if (MsgProcessed == code) return true;

#ifdef FUZZING_SNAPSHOT
  mozilla::fuzzing::IPCFuzzController::instance().OnMessageError(code, aMsg);
#endif

  const char* errorMsg = nullptr;
  switch (code) {
    case MsgDropped:
      errorMsg = "Message dropped: message could not be delivered";
      break;
    case MsgNotKnown:
      errorMsg = "Unknown message: not processed";
      break;
    case MsgNotAllowed:
      errorMsg = "Message not allowed: cannot be sent/recvd in this state";
      break;
    case MsgPayloadError:
      errorMsg = "Payload error: message could not be deserialized";
      break;
    case MsgProcessingError:
      errorMsg =
          "Processing error: message was deserialized, but the handler "
          "returned false (indicating failure)";
      break;
    case MsgValueError:
      errorMsg =
          "Value error: message was deserialized, but contained an illegal "
          "value";
      break;

    default:
      MOZ_CRASH("unknown Result code");
      return false;
  }

  char reason[512];
  const char* msgname = aMsg.name();
  if (msgname[0] == '?') {
    SprintfLiteral(reason, "(msgtype=0x%X) %s", aMsg.type(), errorMsg);
  } else {
    SprintfLiteral(reason, "%s %s", msgname, errorMsg);
  }

  PrintErrorMessage(mSide, channelName, reason);

  // Error handled in mozilla::ipc::IPCResult.
  if (code == MsgProcessingError) {
    return false;
  }

  mListener->ProcessingError(code, reason);

  return false;
}

void MessageChannel::OnChannelErrorFromLink() {
  mMonitor->AssertCurrentThreadOwns();
  MOZ_ASSERT(mChannelState == ChannelConnected);

  IPC_LOG("OnChannelErrorFromLink");

  if (AwaitingSyncReply()) {
    NotifyWorkerThread();
  }

  if (mAbortOnError) {
    // mAbortOnError is set by main actors (e.g., ContentChild) to ensure
    // that the process terminates even if normal shutdown is prevented.
    // A MOZ_CRASH() here is not helpful because crash reporting relies
    // on the parent process which we know is dead or otherwise unusable.
    //
    // Additionally, the parent process can (and often is) killed on Android
    // when apps are backgrounded. We don't need to report a crash for
    // normal behavior in that case.
    printf_stderr("Exiting due to channel error.\n");
    ProcessChild::QuickExit();
  }
  mChannelState = ChannelError;
  mMonitor->Notify();

  PostErrorNotifyTask();
}

void MessageChannel::NotifyMaybeChannelError(ReleasableMonitorAutoLock& aLock) {
  AssertWorkerThread();
  mMonitor->AssertCurrentThreadOwns();
  aLock.AssertCurrentThreadOwns();
  MOZ_ASSERT(mChannelState != ChannelConnected);

  if (ChannelClosing == mChannelState || ChannelClosed == mChannelState) {
    // the channel closed, but we received a "Goodbye" message warning us
    // about it. no worries
    mChannelState = ChannelClosed;
    NotifyChannelClosed(aLock);
    return;
  }

  MOZ_ASSERT(ChannelError == mChannelState);

  Clear();

  // IPDL assumes these notifications do not fire twice, so we do not let
  // that happen.
  if (mNotifiedChannelDone) {
    return;
  }
  mNotifiedChannelDone = true;

  // Let our listener know that the channel errored. This may cause the
  // channel to be deleted. Release our caller's `MonitorAutoLock` before
  // invoking the listener, as this may call back into MessageChannel, and/or
  // cause the channel to be destroyed.
  aLock.Unlock();
  mListener->OnChannelError();
}

void MessageChannel::OnNotifyMaybeChannelError() {
  AssertWorkerThread();
  mMonitor->AssertNotCurrentThreadOwns();

  // This lock guard may be reset by `NotifyMaybeChannelError` before invoking
  // listener callbacks which may destroy this `MessageChannel`.
  //
  // Acquiring the lock here also allows us to ensure that
  // `OnChannelErrorFromLink` has finished running before this task is allowed
  // to continue.
  ReleasableMonitorAutoLock lock(*mMonitor);

  mChannelErrorTask = nullptr;

  if (IsOnCxxStack()) {
    // This used to post a 10ms delayed task; however not all
    // nsISerialEventTarget implementations support delayed dispatch.
    // The delay being completely arbitrary, we may not as well have any.
    PostErrorNotifyTask();
    return;
  }

  // This call may destroy `this`.
  NotifyMaybeChannelError(lock);
}

class MessageChannel::ErrorNotifyBatcher::BatchTask
    : public CancelableRunnable {
 public:
  explicit BatchTask(nsIEventTarget* aEventTarget)
      : CancelableRunnable("MessageChannel::ErrorNotifyBatcher"),
        mEventTarget(aEventTarget) {}

  NS_IMETHOD Run() override {
    // NOTE: This is running all error notify tasks within a single runnable. If
    // this ends up causing latency issues, we can change this logic to
    // re-dispatch between tasks.
    AUTO_PROFILER_LABEL("MessageChannel::ErrorNotifyBatchTask", IPC);
    for (auto& task : mTasks) {
      task->Run();
    }
    mTasks.Clear();
    return NS_OK;
  }

  nsresult Cancel() override {
    for (auto& task : mTasks) {
      task->Cancel();
    }
    mTasks.Clear();
    return NS_OK;
  }

  nsCOMPtr<nsIEventTarget> mEventTarget;
  AutoTArray<RefPtr<CancelableRunnable>, 1> mTasks;
};

MessageChannel::ErrorNotifyBatcher*
    MessageChannel::ErrorNotifyBatcher::sCurrent = nullptr;

MessageChannel::ErrorNotifyBatcher::ErrorNotifyBatcher() {
  AssertIOThread();
  if (sCurrent == nullptr) {
    sCurrent = this;
  }
}

MessageChannel::ErrorNotifyBatcher::~ErrorNotifyBatcher() {
  AssertIOThread();
  if (sCurrent == this) {
    sCurrent = nullptr;
  }

  for (auto& task : mToNotify) {
    nsCOMPtr<nsIEventTarget> target = task->mEventTarget.forget();
    target->Dispatch(task.forget());
  }
}

/* static */
void MessageChannel::ErrorNotifyBatcher::BatchDispatch(
    nsIEventTarget* aTarget, already_AddRefed<CancelableRunnable> aRunnable) {
  RefPtr<CancelableRunnable> runnable(std::move(aRunnable));
  if (!ErrorNotifyBatcher::TryBatchDispatch(aTarget, runnable)) {
    aTarget->Dispatch(runnable.forget());
  }
}

/* static */
bool MessageChannel::ErrorNotifyBatcher::TryBatchDispatch(
    nsIEventTarget* aTarget, RefPtr<CancelableRunnable>& aRunnable) {
  MessageLoop* curLoop = MessageLoop::current();
  if (!curLoop || curLoop->type() != MessageLoop::TYPE_IO) {
    return false;
  }

  AssertIOThread();
  if (!ErrorNotifyBatcher::sCurrent) {
    return false;
  }

  RefPtr<BatchTask> batchTask;
  for (auto& task : ErrorNotifyBatcher::sCurrent->mToNotify) {
    if (task->mEventTarget == aTarget) {
      batchTask = task;
      break;
    }
  }
  if (!batchTask) {
    batchTask = new BatchTask(aTarget);
    ErrorNotifyBatcher::sCurrent->mToNotify.AppendElement(batchTask);
  }

  batchTask->mTasks.AppendElement(aRunnable.forget());
  return true;
}

void MessageChannel::PostErrorNotifyTask() {
  mMonitor->AssertCurrentThreadOwns();

  if (mChannelErrorTask) {
    return;
  }

  // This must be the last code that runs on this thread!
  mChannelErrorTask = NewNonOwningCancelableRunnableMethod(
      "ipc::MessageChannel::OnNotifyMaybeChannelError", this,
      &MessageChannel::OnNotifyMaybeChannelError);

  // Check if we have a notify batcher which we want to use. This will only ever
  // be the case on the IPC I/O thread. If we do, we'll queue up the task to be
  // notified by it instead of directly dispatching.
  ErrorNotifyBatcher::BatchDispatch(mWorkerThread,
                                    do_AddRef(mChannelErrorTask));
}

// Special async message.
class GoodbyeMessage : public IPC::Message {
 public:
  GoodbyeMessage() : IPC::Message(MSG_ROUTING_NONE, GOODBYE_MESSAGE_TYPE) {}
  static bool Read(const Message* msg) { return true; }
  void Log(const std::string& aPrefix, FILE* aOutf) const {
    fputs("(special `Goodbye' message)", aOutf);
  }
};

void MessageChannel::InduceConnectionError() {
  MonitorAutoLock lock(*mMonitor);

  // Either connected or closing, immediately convert to an error and notify.
  switch (mChannelState) {
    case ChannelConnected:
      // The channel is still actively connected. Immediately shut down the
      // connection with our peer and simulate it invoking
      // OnChannelErrorFromLink on us.
      //
      // This will update the state to ChannelError, preventing new messages
      // from being processed, leading to an error being reported asynchronously
      // to our listener.
      mLink->Close();
      OnChannelErrorFromLink();
      return;

    case ChannelClosing:
      // An notify task has already been posted. Update mChannelState to stop
      // processing new messages and treat the notification as an error.
      mChannelState = ChannelError;
      return;

    default:
      // Either already closed or errored. Nothing to do.
      MOZ_ASSERT(mChannelState == ChannelClosed ||
                 mChannelState == ChannelError);
      return;
  }
}

void MessageChannel::NotifyImpendingShutdown() {
  UniquePtr<Message> msg =
      MakeUnique<Message>(MSG_ROUTING_NONE, IMPENDING_SHUTDOWN_MESSAGE_TYPE);
  MonitorAutoLock lock(*mMonitor);
  if (Connected()) {
    SendMessageToLink(std::move(msg));
  }
}

void MessageChannel::Close() {
  AssertWorkerThread();
  mMonitor->AssertNotCurrentThreadOwns();

  // This lock guard may be reset by `Notify{ChannelClosed,MaybeChannelError}`
  // before invoking listener callbacks which may destroy this `MessageChannel`.
  ReleasableMonitorAutoLock lock(*mMonitor);

  switch (mChannelState) {
    case ChannelError:
      // See bug 538586: if the listener gets deleted while the
      // IO thread's NotifyChannelError event is still enqueued
      // and subsequently deletes us, then the error event will
      // also be deleted and the listener will never be notified
      // of the channel error.
      NotifyMaybeChannelError(lock);
      return;
    case ChannelClosed:
      // Slightly unexpected but harmless; ignore.  See bug 1554244.
      return;

    default:
      // Notify the other side that we're about to close our socket. If we've
      // already received a Goodbye from the other side (and our state is
      // ChannelClosing), there's no reason to send one.
      if (ChannelConnected == mChannelState) {
        SendMessageToLink(MakeUnique<GoodbyeMessage>());
      }
      mLink->Close();
      mChannelState = ChannelClosed;
      NotifyChannelClosed(lock);
      return;
  }
}

void MessageChannel::NotifyChannelClosed(ReleasableMonitorAutoLock& aLock) {
  AssertWorkerThread();
  mMonitor->AssertCurrentThreadOwns();
  aLock.AssertCurrentThreadOwns();

  if (ChannelClosed != mChannelState) {
    MOZ_CRASH("channel should have been closed!");
  }

  Clear();

  // IPDL assumes these notifications do not fire twice, so we do not let
  // that happen.
  if (mNotifiedChannelDone) {
    return;
  }
  mNotifiedChannelDone = true;

  // Let our listener know that the channel was closed. This may cause the
  // channel to be deleted. Release our caller's `MonitorAutoLock` before
  // invoking the listener, as this may call back into MessageChannel, and/or
  // cause the channel to be destroyed.
  aLock.Unlock();
  mListener->OnChannelClose();
}

void MessageChannel::DebugAbort(const char* file, int line, const char* cond,
                                const char* why, bool reply) {
  AssertWorkerThread();
  mMonitor->AssertCurrentThreadOwns();

  printf_stderr(
      "###!!! [MessageChannel][%s][%s:%d] "
      "Assertion (%s) failed.  %s %s\n",
      StringFromIPCSide(mSide), file, line, cond, why, reply ? "(reply)" : "");

  MessageQueue pending = std::move(mPending);
  while (!pending.isEmpty()) {
    pending.getFirst()->AssertMonitorHeld(*mMonitor);
    printf_stderr("    [ %s%s ]\n",
                  pending.getFirst()->Msg()->is_sync() ? "sync" : "async",
                  pending.getFirst()->Msg()->is_reply() ? "reply" : "");
    pending.popFirst();
  }

  MOZ_CRASH_UNSAFE(why);
}

void MessageChannel::AddProfilerMarker(const IPC::Message& aMessage,
                                       MessageDirection aDirection) {
  mMonitor->AssertCurrentThreadOwns();

  if (profiler_feature_active(ProfilerFeature::Flows) &&
      // If one of the profiler mutexes is locked on this thread, don't
      // record markers, because we don't want to expose profiler IPCs due to
      // the profiler itself, and also to avoid possible re-entrancy issues.
      !profiler_is_locked_on_current_thread()) {
    if (aDirection == MessageDirection::eSending) {
      auto flow = Flow::Global(aMessage.seqno() ^
                               LossyNarrowChannelId(mMessageChannelId));
      profiler_add_marker("IPC", baseprofiler::category::OTHER,
                          MarkerTiming::InstantNow(), IPCFlowMarker{},
                          aMessage.type(), flow);
    }
  }

  if (profiler_feature_active(ProfilerFeature::IPCMessages)) {
    base::ProcessId pid = mListener->OtherPidMaybeInvalid();
    // Only record markers for IPCs with a valid pid.
    // And if one of the profiler mutexes is locked on this thread, don't record
    // markers, because we don't want to expose profiler IPCs due to the
    // profiler itself, and also to avoid possible re-entrancy issues.
    if (pid != base::kInvalidProcessId &&
        !profiler_is_locked_on_current_thread()) {
      // The current timestamp must be given to the `IPCMarker` payload.
      const TimeStamp now = TimeStamp::Now();
      bool isThreadBeingProfiled =
          profiler_thread_is_being_profiled_for_markers();
      PROFILER_MARKER(
          "IPC", IPC,
          mozilla::MarkerOptions(
              mozilla::MarkerTiming::InstantAt(now),
              // If the thread is being profiled, add the marker to
              // the current thread. If the thread is not being
              // profiled, add the marker to the main thread. It
              // will appear in the main thread's IPC track. Profiler analysis
              // UI correlates all the IPC markers from different threads and
              // generates processed markers.
              isThreadBeingProfiled ? mozilla::MarkerThreadId::CurrentThread()
                                    : mozilla::MarkerThreadId::MainThread()),
          IPCMarker, now, now, pid, aMessage.seqno(), aMessage.type(), mSide,
          aDirection, MessagePhase::Endpoint, aMessage.is_sync(),
          // aOriginThreadId: If the thread is being profiled, do not include a
          // thread ID, as it's the same as the markers. Only include this field
          // when the marker is being sent from another thread.
          isThreadBeingProfiled ? mozilla::MarkerThreadId{}
                                : mozilla::MarkerThreadId::CurrentThread());
    }
  }
}

void MessageChannel::EndTimeout() {
  mMonitor->AssertCurrentThreadOwns();

  IPC_LOG("Ending timeout of seqno=%" PRId64, mTimedOutMessageSeqno);
  mTimedOutMessageSeqno = 0;
  mTimedOutMessageNestedLevel = 0;

  RepostAllMessages();
}

void MessageChannel::RepostAllMessages() {
  mMonitor->AssertCurrentThreadOwns();

  bool needRepost = false;
  for (MessageTask* task : mPending) {
    task->AssertMonitorHeld(*mMonitor);
    if (!task->IsScheduled()) {
      needRepost = true;
      break;
    }
  }
  if (!needRepost) {
    // If everything is already scheduled to run, do nothing.
    return;
  }

  // In some cases we may have deferred dispatch of some messages in the
  // queue. Now we want to run them again. However, we can't just re-post
  // those messages since the messages after them in mPending would then be
  // before them in the event queue. So instead we cancel everything and
  // re-post all messages in the correct order.
  MessageQueue queue = std::move(mPending);
  while (RefPtr<MessageTask> task = queue.popFirst()) {
    task->AssertMonitorHeld(*mMonitor);
    RefPtr<MessageTask> newTask = new MessageTask(this, std::move(task->Msg()));
    newTask->AssertMonitorHeld(*mMonitor);
    mPending.insertBack(newTask);
    newTask->Post();
  }

  AssertMaybeDeferredCountCorrect();
}

void MessageChannel::CancelTransaction(seqno_t transaction) {
  mMonitor->AssertCurrentThreadOwns();

  // When we cancel a transaction, we need to behave as if there's no longer
  // any IPC on the stack. Anything we were dispatching or sending will get
  // canceled. Consequently, we have to update the state variables below.
  //
  // We also need to ensure that when any IPC functions on the stack return,
  // they don't reset these values using an RAII class like AutoSetValue. To
  // avoid that, these RAII classes check if the variable they set has been
  // tampered with (by us). If so, they don't reset the variable to the old
  // value.

  IPC_LOG("CancelTransaction: xid=%" PRId64, transaction);

  // An unusual case: We timed out a transaction which the other side then
  // cancelled. In this case we just leave the timedout state and try to
  // forget this ever happened.
  if (transaction == mTimedOutMessageSeqno) {
    IPC_LOG("Cancelled timed out message %" PRId64, mTimedOutMessageSeqno);
    EndTimeout();

    // Normally mCurrentTransaction == 0 here. But it can be non-zero if:
    // 1. Parent sends NESTED_INSIDE_SYNC message H.
    // 2. Parent times out H.
    // 3. Child dispatches H and sends nested message H' (same transaction).
    // 4. Parent dispatches H' and cancels.
    MOZ_RELEASE_ASSERT(!mTransactionStack ||
                       mTransactionStack->TransactionID() == transaction);
    if (mTransactionStack) {
      mTransactionStack->Cancel();
    }
  } else {
    MOZ_RELEASE_ASSERT(mTransactionStack->TransactionID() == transaction);
    mTransactionStack->Cancel();
  }

  bool foundSync = false;
  for (MessageTask* p = mPending.getFirst(); p;) {
    p->AssertMonitorHeld(*mMonitor);
    UniquePtr<Message>& msg = p->Msg();

    // If there was a race between the parent and the child, then we may
    // have a queued sync message. We want to drop this message from the
    // queue since if will get cancelled along with the transaction being
    // cancelled. This happens if the message in the queue is
    // NESTED_INSIDE_SYNC.
    if (msg->is_sync() && msg->nested_level() != IPC::Message::NOT_NESTED) {
      MOZ_RELEASE_ASSERT(!foundSync);
      MOZ_RELEASE_ASSERT(msg->transaction_id() != transaction);
      IPC_LOG("Removing msg from queue seqno=%" PRId64 " xid=%" PRId64,
              msg->seqno(), msg->transaction_id());
      foundSync = true;
      if (!IsAlwaysDeferred(*msg)) {
        mMaybeDeferredPendingCount--;
      }
      p = p->removeAndGetNext();
      continue;
    }

    p = p->getNext();
  }

  AssertMaybeDeferredCountCorrect();
}

void MessageChannel::CancelCurrentTransaction() {
  MonitorAutoLock lock(*mMonitor);
  if (DispatchingSyncMessageNestedLevel() >= IPC::Message::NESTED_INSIDE_SYNC) {
    if (DispatchingSyncMessageNestedLevel() ==
            IPC::Message::NESTED_INSIDE_CPOW ||
        DispatchingAsyncMessageNestedLevel() ==
            IPC::Message::NESTED_INSIDE_CPOW) {
      mListener->IntentionalCrash();
    }

    IPC_LOG("Cancel requested: current xid=%" PRId64,
            CurrentNestedInsideSyncTransaction());
    MOZ_RELEASE_ASSERT(DispatchingSyncMessage());
    auto cancel =
        MakeUnique<CancelMessage>(CurrentNestedInsideSyncTransaction());
    CancelTransaction(CurrentNestedInsideSyncTransaction());
    SendMessageToLink(std::move(cancel));
  }
}

void CancelCPOWs() {
  MOZ_ASSERT(NS_IsMainThread());

  if (gParentProcessBlocker) {
    mozilla::glean::ipc::transaction_cancel
        .EnumGet(mozilla::glean::ipc::TransactionCancelLabel::eTrue)
        .Add();
    gParentProcessBlocker->CancelCurrentTransaction();
  }
}

bool MessageChannel::IsCrossProcess() const {
  mMonitor->AssertCurrentThreadOwns();
  return mIsCrossProcess;
}

void MessageChannel::SetIsCrossProcess(bool aIsCrossProcess) {
  mMonitor->AssertCurrentThreadOwns();
  if (aIsCrossProcess == mIsCrossProcess) {
    return;
  }
  mIsCrossProcess = aIsCrossProcess;
  if (mIsCrossProcess) {
    ChannelCountReporter::Increment(mName);
  } else {
    ChannelCountReporter::Decrement(mName);
  }
}

NS_IMPL_ISUPPORTS(MessageChannel::WorkerTargetShutdownTask,
                  nsITargetShutdownTask)

MessageChannel::WorkerTargetShutdownTask::WorkerTargetShutdownTask(
    nsISerialEventTarget* aTarget, MessageChannel* aChannel)
    : mTarget(aTarget), mChannel(aChannel) {}

void MessageChannel::WorkerTargetShutdownTask::TargetShutdown() {
  MOZ_RELEASE_ASSERT(mTarget->IsOnCurrentThread());
  IPC_LOG("Closing channel due to event target shutdown");
  if (MessageChannel* channel = std::exchange(mChannel, nullptr)) {
    channel->Close();
  }
}

void MessageChannel::WorkerTargetShutdownTask::Clear() {
  MOZ_RELEASE_ASSERT(mTarget->IsOnCurrentThread());
  mChannel = nullptr;
}

NS_IMPL_ISUPPORTS_INHERITED0(MessageChannel::FlushLazySendMessagesRunnable,
                             CancelableRunnable)

MessageChannel::FlushLazySendMessagesRunnable::FlushLazySendMessagesRunnable(
    MessageChannel* aChannel)
    : CancelableRunnable("MessageChannel::FlushLazyMessagesRunnable"),
      mChannel(aChannel) {}

NS_IMETHODIMP MessageChannel::FlushLazySendMessagesRunnable::Run() {
  if (mChannel) {
    MonitorAutoLock lock(*mChannel->mMonitor);
    MOZ_ASSERT(mChannel->mFlushLazySendTask == this);
    mChannel->FlushLazySendMessages();
  }
  return NS_OK;
}

nsresult MessageChannel::FlushLazySendMessagesRunnable::Cancel() {
  mQueue.Clear();
  mChannel = nullptr;
  return NS_OK;
}

void MessageChannel::FlushLazySendMessagesRunnable::PushMessage(
    UniquePtr<Message> aMsg) {
  MOZ_ASSERT(mChannel);
  mQueue.AppendElement(std::move(aMsg));
}

nsTArray<UniquePtr<IPC::Message>>
MessageChannel::FlushLazySendMessagesRunnable::TakeMessages() {
  mChannel = nullptr;
  return std::move(mQueue);
}

}  // namespace ipc
}  // namespace mozilla
