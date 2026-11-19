/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ipc_glue_MessageChannel_h
#define ipc_glue_MessageChannel_h

#include "ETWTools.h"
#include "ipc/EnumSerializer.h"
#include "mozilla/BaseProfilerMarkers.h"
#include "mozilla/DefineEnum.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Monitor.h"
#include "mozilla/MoveOnlyFunction.h"
#if defined(XP_WIN)
#  include "mozilla/ipc/Neutering.h"
#endif  // defined(XP_WIN)

#include <functional>

#include "MessageLink.h"  // for HasResultCodes
#include "mozilla/ipc/ScopedPort.h"
#include "nsITargetShutdownTask.h"

#ifdef FUZZING_SNAPSHOT
#  include "mozilla/fuzzing/IPCFuzzController.h"
#endif

class MessageLoop;

namespace IPC {
template <typename T>
struct ParamTraits;
}

namespace mozilla {
namespace ipc {

class IToplevelProtocol;
class ActorLifecycleProxy;

class RefCountedMonitor : public Monitor {
 public:
  RefCountedMonitor() : Monitor("mozilla.ipc.MessageChannel.mMonitor") {}

  void AssertSameMonitor(const RefCountedMonitor& aOther) const
      MOZ_REQUIRES(*this) MOZ_ASSERT_CAPABILITY(aOther) {
    MOZ_ASSERT(this == &aOther);
  }

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RefCountedMonitor)

 private:
  ~RefCountedMonitor() = default;
};

enum class MessageDirection {
  eSending,
  eReceiving,
};

enum class MessagePhase {
  Endpoint,
  TransferStart,
  TransferEnd,
};

enum class SyncSendError {
  SendSuccess,
  PreviousTimeout,
  SendingCPOWWhileDispatchingSync,
  SendingCPOWWhileDispatchingUrgent,
  NotConnectedBeforeSend,
  DisconnectedDuringSend,
  CancelledBeforeSend,
  CancelledAfterSend,
  TimedOut,
  ReplyError,
};

MOZ_DEFINE_ENUM_CLASS_WITH_TOSTRING(ResponseRejectReason,
                                    (SendError, ChannelClosed, HandlerRejected,
                                     ActorDestroyed, ResolverDestroyed,
                                     EndGuard_));

template <typename T>
using ResolveCallback = MoveOnlyFunction<void(T&&)>;

using RejectCallback = MoveOnlyFunction<void(ResponseRejectReason)>;

enum ChannelState {
  ChannelClosed,
  ChannelConnected,
  ChannelClosing,
  ChannelError
};

class AutoEnterTransaction;

class MessageChannel : HasResultCodes {
  friend class PortLink;

  typedef mozilla::Monitor Monitor;

 public:
  using Message = IPC::Message;
  using seqno_t = Message::seqno_t;

  static constexpr int32_t kNoTimeout = INT32_MIN;

  using ScopedPort = mozilla::ipc::ScopedPort;

  explicit MessageChannel(const char* aName, IToplevelProtocol* aListener);
  ~MessageChannel();

  IToplevelProtocol* Listener() const { return mListener; }

  // Returns the event target which the worker lives on and must be used for
  // operations on the current thread. Only safe to access after the
  // MessageChannel has been opened.
  nsISerialEventTarget* GetWorkerEventTarget() const { return mWorkerThread; }

  // "Open" a connection using an existing ScopedPort. The ScopedPort must be
  // valid and connected to a remote.
  //
  // The `aEventTarget` parameter must be on the current thread.
  bool Open(ScopedPort aPort, Side aSide, const nsID& aMessageChannelId,
            nsISerialEventTarget* aEventTarget = nullptr);

  // "Open" a connection to another thread in the same process.
  //
  // Returns true if the transport layer was successfully connected,
  // i.e., mChannelState == ChannelConnected.
  //
  // For more details on the process of opening a channel between
  // threads, see the extended comment on this function
  // in MessageChannel.cpp.
  bool Open(MessageChannel* aTargetChan, nsISerialEventTarget* aEventTarget,
            Side aSide);

  // "Open" a connection to an actor on the current thread.
  //
  // Returns true if the transport layer was successfully connected,
  // i.e., mChannelState == ChannelConnected.
  //
  // Same-thread channels may not perform synchronous or blocking message
  // sends, to avoid deadlocks.
  bool OpenOnSameThread(MessageChannel* aTargetChan, Side aSide);

  /**
   * This sends a special message that is processed on the IO thread, so that
   * other actors can know that the process will soon shutdown.
   */
  void NotifyImpendingShutdown() MOZ_EXCLUDES(*mMonitor);

  // Close the underlying transport channel.
  void Close() MOZ_EXCLUDES(*mMonitor);

  // Induce an error in this MessageChannel's connection.
  //
  // After this method is called, no more message notifications will be
  // delivered to the listener, and the channel will be unable to send or
  // receive future messages, as if the peer dropped the connection
  // unexpectedly.
  //
  // The OnChannelError notification will be delivered either asynchronously or
  // during an explicit call to Close(), whichever happens first.
  //
  // NOTE: If SetAbortOnError(true) has been called on this MessageChannel,
  // calling this function will immediately exit the current process.
  void InduceConnectionError() MOZ_EXCLUDES(*mMonitor);

  void SetAbortOnError(bool abort) MOZ_EXCLUDES(*mMonitor) {
    MonitorAutoLock lock(*mMonitor);
    mAbortOnError = abort;
  }

  // Call aInvoke for each pending message until it returns false.
  // XXX: You must get permission from an IPC peer to use this function
  //      since it requires custom deserialization and re-orders events.
  void PeekMessages(const std::function<bool(const Message& aMsg)>& aInvoke)
      MOZ_EXCLUDES(*mMonitor);

  // Misc. behavioral traits consumers can request for this channel
  enum ChannelFlags {
    REQUIRE_DEFAULT = 0,
    // Windows: if this channel operates on the UI thread, indicates
    // WindowsMessageLoop code should enable deferred native message
    // handling to prevent deadlocks. Should only be used for protocols
    // that manage child processes which might create native UI, like
    // plugins.
    REQUIRE_DEFERRED_MESSAGE_PROTECTION = 1 << 0,
  };
  void SetChannelFlags(ChannelFlags aFlags) { mFlags = aFlags; }
  ChannelFlags GetChannelFlags() { return mFlags; }

  // Asynchronously send a message to the other side of the channel.
  // If aSeqno is non-null, it will be set to the seqno of the sent message.
  bool Send(UniquePtr<Message> aMsg, seqno_t* aSeqno = nullptr)
      MOZ_EXCLUDES(*mMonitor);

  bool SendBuildIDsMatchMessage(const char* aParentBuildID)
      MOZ_EXCLUDES(*mMonitor);
  bool DoBuildIDsMatch() MOZ_EXCLUDES(*mMonitor) {
    MonitorAutoLock lock(*mMonitor);
    return mBuildIDsConfirmedMatch;
  }

  // Synchronously send |aMsg| (i.e., wait for |aReply|)
  bool Send(UniquePtr<Message> aMsg, UniquePtr<Message>* aReply)
      MOZ_EXCLUDES(*mMonitor);

  bool CanSend() const MOZ_EXCLUDES(*mMonitor);

  // If sending a sync message returns an error, this function gives a more
  // descriptive error message.
  SyncSendError LastSendError() const {
    AssertWorkerThread();
    return mLastSendError;
  }

  void SetReplyTimeoutMs(int32_t aTimeoutMs);

  bool IsOnCxxStack() const { return mOnCxxStack; }

  void CancelCurrentTransaction() MOZ_EXCLUDES(*mMonitor);

  // Return whether the current transaction is complete.
  //
  // This is intended only for tests.
  bool TestOnlyIsTransactionComplete() const MOZ_EXCLUDES(*mMonitor);

  // IsClosed and NumQueuedMessages are safe to call from any thread, but
  // may provide an out-of-date value.
  bool IsClosed() MOZ_EXCLUDES(*mMonitor) {
    MonitorAutoLock lock(*mMonitor);
    return IsClosedLocked();
  }
  bool IsClosedLocked() const MOZ_REQUIRES(*mMonitor) {
    mMonitor->AssertCurrentThreadOwns();
    return mLink ? mLink->IsClosed() : true;
  }

  static bool IsPumpingMessages() { return sIsPumpingMessages; }
  static void SetIsPumpingMessages(bool aIsPumping) {
    sIsPumpingMessages = aIsPumping;
  }

  /**
   * Does this MessageChannel currently cross process boundaries?
   */
  bool IsCrossProcess() const MOZ_REQUIRES(*mMonitor);
  void SetIsCrossProcess(bool aIsCrossProcess) MOZ_REQUIRES(*mMonitor);

  nsID GetMessageChannelId() const {
    MonitorAutoLock lock(*mMonitor);
    return mMessageChannelId;
  }

  /**
   * In some cases, such as when losing connection to another process over IPC,
   * a large number of actors may be torn down simultaneously.
   * To minimize the overhead of dispatching a potentially-large number of IPC
   * messages to the same event target, we batch error notification tasks on our
   * MessageChannel instances for each event target together.
   * This RAII type defines the scope of this batching.
   * If multiple ErrorNotifyBatcher instances are on the stack, the outermost
   * batcher takes precidence.
   */
  struct MOZ_RAII ErrorNotifyBatcher {
    ErrorNotifyBatcher();
    ~ErrorNotifyBatcher();

    // If a batch dispatcher is available, uses it; otherwise directly
    // dispatches.
    static void BatchDispatch(nsIEventTarget* aTarget,
                              already_AddRefed<CancelableRunnable> aRunnable);

   private:
    [[nodiscard]] static bool TryBatchDispatch(
        nsIEventTarget* aTarget, RefPtr<CancelableRunnable>& aRunnable);

    // NOTE: Only ever accessed/set on the IPC I/O thread.
    static ErrorNotifyBatcher* sCurrent;

    class BatchTask;
    AutoTArray<RefPtr<BatchTask>, 8> mToNotify;
  };

#ifdef FUZZING_SNAPSHOT
  Maybe<mojo::core::ports::PortName> GetPortName() {
    MonitorAutoLock lock(*mMonitor);
    return mLink ? mLink->GetPortName() : Nothing();
  }
#endif

#ifdef XP_WIN
  struct MOZ_STACK_CLASS SyncStackFrame {
    explicit SyncStackFrame(MessageChannel* channel);
    ~SyncStackFrame();

    bool mSpinNestedEvents;
    bool mListenerNotified;
    MessageChannel* mChannel;

    // The previous stack frame for this channel.
    SyncStackFrame* mPrev;

    // The previous stack frame on any channel.
    SyncStackFrame* mStaticPrev;
  };
  friend struct MessageChannel::SyncStackFrame;

  static bool IsSpinLoopActive() {
    for (SyncStackFrame* frame = sStaticTopFrame; frame; frame = frame->mPrev) {
      if (frame->mSpinNestedEvents) return true;
    }
    return false;
  }

 protected:
  // The deepest sync stack frame for this channel.
  SyncStackFrame* mTopFrame = nullptr;

  bool mIsSyncWaitingOnNonMainThread = false;

  // The deepest sync stack frame on any channel.
  static SyncStackFrame* sStaticTopFrame;

 public:
  void ProcessNativeEventsInInterruptCall();
  static void NotifyGeckoEventDispatch();
#endif  // defined(XP_WIN)

 private:
  void PostErrorNotifyTask() MOZ_REQUIRES(*mMonitor);
  void OnNotifyMaybeChannelError() MOZ_EXCLUDES(*mMonitor);
  void ReportConnectionError(const char* aFunctionName,
                             const uint32_t aMsgTyp) const
      MOZ_REQUIRES(*mMonitor);
  bool MaybeHandleError(Result code, const Message& aMsg,
                        const char* channelName) MOZ_EXCLUDES(*mMonitor);

  void Clear() MOZ_REQUIRES(*mMonitor);

  bool HasPendingEvents() MOZ_REQUIRES(*mMonitor);

  void ProcessPendingRequests(ActorLifecycleProxy* aProxy,
                              AutoEnterTransaction& aTransaction)
      MOZ_REQUIRES(*mMonitor);
  bool ProcessPendingRequest(ActorLifecycleProxy* aProxy,
                             UniquePtr<Message> aUrgent)
      MOZ_REQUIRES(*mMonitor);

  void EnqueuePendingMessages() MOZ_REQUIRES(*mMonitor);

  // Dispatches an incoming message to its appropriate handler.
  void DispatchMessage(ActorLifecycleProxy* aProxy, UniquePtr<Message> aMsg)
      MOZ_REQUIRES(*mMonitor);

  // DispatchMessage will route to one of these functions depending on the
  // protocol type of the message.
  void DispatchSyncMessage(ActorLifecycleProxy* aProxy, const Message& aMsg,
                           UniquePtr<Message>& aReply) MOZ_EXCLUDES(*mMonitor);
  void DispatchAsyncMessage(ActorLifecycleProxy* aProxy, const Message& aMsg)
      MOZ_EXCLUDES(*mMonitor);

  // Return true if the wait ended because a notification was received.
  //
  // Return false if the time elapsed from when we started the process of
  // waiting until afterwards exceeded the currently allotted timeout.
  // That *DOES NOT* mean false => "no event" (== timeout); there are many
  // circumstances that could cause the measured elapsed time to exceed the
  // timeout EVEN WHEN we were notified.
  //
  // So in sum: true is a meaningful return value; false isn't,
  // necessarily.
  bool WaitForSyncNotify() MOZ_REQUIRES(*mMonitor);

  bool WaitResponse(bool aWaitTimedOut);

  bool ShouldContinueFromTimeout() MOZ_REQUIRES(*mMonitor);

  void EndTimeout() MOZ_REQUIRES(*mMonitor);
  void CancelTransaction(seqno_t transaction) MOZ_REQUIRES(*mMonitor);

  void RepostAllMessages() MOZ_REQUIRES(*mMonitor);

  seqno_t NextSeqno() {
    AssertWorkerThread();
    MOZ_RELEASE_ASSERT(mozilla::Abs(mNextSeqno) < INT64_MAX, "seqno overflow");
    return (mSide == ChildSide) ? --mNextSeqno : ++mNextSeqno;
  }

  void DebugAbort(const char* file, int line, const char* cond, const char* why,
                  bool reply = false) MOZ_REQUIRES(*mMonitor);

  void AddProfilerMarker(const IPC::Message& aMessage,
                         MessageDirection aDirection) MOZ_REQUIRES(*mMonitor);

 private:
  // Returns true if we're dispatching an async message's callback.
  bool DispatchingAsyncMessage() const {
    AssertWorkerThread();
    return mDispatchingAsyncMessage;
  }

  int DispatchingAsyncMessageNestedLevel() const {
    AssertWorkerThread();
    return mDispatchingAsyncMessageNestedLevel;
  }

  // Check if there is still a live connection to our peer. This may change to
  // `false` at any time due to the connection to our peer being closed or
  // dropped (e.g. due to a crash).
  bool Connected() const MOZ_REQUIRES(*mMonitor);

  // Check if there is either still a live connection to our peer, or we have
  // received a `Goodbye` from our peer, and are actively shutting down our
  // connection with our peer.
  bool ConnectedOrClosing() const MOZ_REQUIRES(*mMonitor);

 private:
  // Executed on the IO thread.
  void NotifyWorkerThread() MOZ_REQUIRES(*mMonitor);

  // Return true if |aMsg| is a special message targeted at the IO
  // thread, in which case it shouldn't be delivered to the worker.
  bool MaybeInterceptSpecialIOMessage(const Message& aMsg)
      MOZ_REQUIRES(*mMonitor);

  // Returns true if ShouldDeferMessage(aMsg) is guaranteed to return true.
  // Otherwise, the result of ShouldDeferMessage(aMsg) may be true or false,
  // depending on context.
  static bool IsAlwaysDeferred(const Message& aMsg);

  // Helper for sending a message via the link. If the message is [LazySend], it
  // will be queued, and if the message is not-[LazySend], it will flush any
  // pending [LazySend] messages.
  void SendMessageToLink(UniquePtr<Message> aMsg) MOZ_REQUIRES(*mMonitor);

  // Called to flush [LazySend] messages to the link.
  void FlushLazySendMessages() MOZ_REQUIRES(*mMonitor);

  bool ShouldDeferMessage(const Message& aMsg) MOZ_REQUIRES(*mMonitor);
  void OnMessageReceivedFromLink(UniquePtr<Message> aMsg)
      MOZ_REQUIRES(*mMonitor);
  void OnChannelErrorFromLink() MOZ_REQUIRES(*mMonitor);

 private:
  // Clear this channel, and notify the listener that the channel has either
  // closed or errored.
  //
  // These methods must be called on the worker thread, passing in a
  // `ReleasableMonitorAutoLock`. This lock guard will be reset before the
  // listener is called, allowing for the monitor to be unlocked before the
  // MessageChannel is potentially destroyed.
  void NotifyChannelClosed(ReleasableMonitorAutoLock& aLock)
      MOZ_REQUIRES(*mMonitor);
  void NotifyMaybeChannelError(ReleasableMonitorAutoLock& aLock)
      MOZ_REQUIRES(*mMonitor);

 private:
  void AssertWorkerThread() const {
    MOZ_ASSERT(mWorkerThread, "Channel hasn't been opened yet");
    MOZ_RELEASE_ASSERT(mWorkerThread && mWorkerThread->IsOnCurrentThread(),
                       "not on worker thread!");
  }

 private:
  class MessageTask : public CancelableRunnable,
                      public LinkedListElement<RefPtr<MessageTask>>,
                      public nsIRunnablePriority,
                      public nsIRunnableIPCMessageType {
   public:
    explicit MessageTask(MessageChannel* aChannel, UniquePtr<Message> aMessage);
    MessageTask() = delete;
    MessageTask(const MessageTask&) = delete;

    NS_DECL_ISUPPORTS_INHERITED

    NS_IMETHOD Run() override;
    nsresult Cancel() override;
    NS_IMETHOD GetPriority(uint32_t* aPriority) override;
    NS_DECL_NSIRUNNABLEIPCMESSAGETYPE
    void Post() MOZ_REQUIRES(*mMonitor);

    bool IsScheduled() const MOZ_REQUIRES(*mMonitor) {
      mMonitor->AssertCurrentThreadOwns();
      return mScheduled;
    }

    UniquePtr<Message>& Msg() MOZ_REQUIRES(*mMonitor) {
      MOZ_DIAGNOSTIC_ASSERT(mMessage, "message was moved");
      return mMessage;
    }
    const UniquePtr<Message>& Msg() const MOZ_REQUIRES(*mMonitor) {
      MOZ_DIAGNOSTIC_ASSERT(mMessage, "message was moved");
      return mMessage;
    }

    void AssertMonitorHeld(const RefCountedMonitor& aMonitor)
        MOZ_REQUIRES(aMonitor) MOZ_ASSERT_CAPABILITY(*mMonitor) {
      aMonitor.AssertSameMonitor(*mMonitor);
    }

   private:
    ~MessageTask();

    MessageChannel* Channel() MOZ_REQUIRES(*mMonitor) {
      mMonitor->AssertCurrentThreadOwns();
      MOZ_RELEASE_ASSERT(isInList());
      return mChannel;
    }

    // The connected MessageChannel's monitor. Guards `mChannel` and
    // `mScheduled`.
    RefPtr<RefCountedMonitor> const mMonitor;
    // The channel which this MessageTask is associated with. Only valid while
    // `mMonitor` is held, and this MessageTask `isInList()`.
    MessageChannel* const mChannel;
    UniquePtr<Message> mMessage MOZ_GUARDED_BY(*mMonitor);
    uint32_t const mPriority;
    bool mScheduled : 1 MOZ_GUARDED_BY(*mMonitor);
#ifdef FUZZING_SNAPSHOT
    const bool mIsFuzzMsg;
    bool mFuzzStopped MOZ_GUARDED_BY(*mMonitor);
#endif
  };

  bool ShouldRunMessage(const Message& aMsg) MOZ_REQUIRES(*mMonitor);
  void RunMessage(ActorLifecycleProxy* aProxy, MessageTask& aTask)
      MOZ_REQUIRES(*mMonitor);

  class WorkerTargetShutdownTask final : public nsITargetShutdownTask {
   public:
    NS_DECL_THREADSAFE_ISUPPORTS

    WorkerTargetShutdownTask(nsISerialEventTarget* aTarget,
                             MessageChannel* aChannel);

    void TargetShutdown() override;
    void Clear();

   private:
    ~WorkerTargetShutdownTask() = default;

    const nsCOMPtr<nsISerialEventTarget> mTarget;
    // Cleared by MessageChannel before it is destroyed.
    MessageChannel* MOZ_NON_OWNING_REF mChannel;
  };

  class FlushLazySendMessagesRunnable final : public CancelableRunnable {
   public:
    explicit FlushLazySendMessagesRunnable(MessageChannel* aChannel);

    NS_DECL_ISUPPORTS_INHERITED

    NS_IMETHOD Run() override;
    nsresult Cancel() override;

    void PushMessage(UniquePtr<Message> aMsg);
    nsTArray<UniquePtr<Message>> TakeMessages();

   private:
    ~FlushLazySendMessagesRunnable() = default;

    // Cleared by MessageChannel before it is destroyed.
    MessageChannel* MOZ_NON_OWNING_REF mChannel;

    // LazySend messages which haven't been sent yet, but will be sent as soon
    // as a non-LazySend message is sent, or this runnable is executed.
    nsTArray<UniquePtr<Message>> mQueue;
  };

  typedef LinkedList<RefPtr<MessageTask>> MessageQueue;
  typedef IPC::Message::msgid_t msgid_t;

 private:
  // This will be a string literal, so lifetime is not an issue.
  const char* const mName;

  // ID for each MessageChannel. Set when it is opened, and never cleared
  // afterwards.
  //
  // This ID is only intended for diagnostics, debugging, and reporting
  // purposes, and shouldn't be used for message routing or permissions checks.
  nsID mMessageChannelId MOZ_GUARDED_BY(*mMonitor) = {};

  // Based on presumption the listener owns and overlives the channel,
  // this is never nullified.
  IToplevelProtocol* const mListener;

  // This monitor guards all state in this MessageChannel, except where
  // otherwise noted. It is refcounted so a reference to it can be shared with
  // IPC listener objects which need to access weak references to this
  // `MessageChannel`.
  RefPtr<RefCountedMonitor> const mMonitor;

  ChannelState mChannelState MOZ_GUARDED_BY(*mMonitor) = ChannelClosed;
  Side mSide = UnknownSide;
  bool mIsCrossProcess MOZ_GUARDED_BY(*mMonitor) = false;
  UniquePtr<MessageLink> mLink MOZ_GUARDED_BY(*mMonitor);

  // NotifyMaybeChannelError runnable
  RefPtr<CancelableRunnable> mChannelErrorTask MOZ_GUARDED_BY(*mMonitor);

  // Thread we are allowed to send and receive on.  Set in Open(); never
  // changed, and we can only call Open() once.  We shouldn't be accessing
  // from multiple threads before Open().
  nsCOMPtr<nsISerialEventTarget> mWorkerThread;

  // Shutdown task to close the channel before mWorkerThread goes away.
  RefPtr<WorkerTargetShutdownTask> mShutdownTask MOZ_GUARDED_BY(*mMonitor);

  // Task to force sending lazy messages when mQueuedLazyMessages is non-empty.
  RefPtr<FlushLazySendMessagesRunnable> mFlushLazySendTask
      MOZ_GUARDED_BY(*mMonitor);

  // Timeout periods are broken up in two to prevent system suspension from
  // triggering an abort. This method (called by WaitForEvent with a 'did
  // timeout' flag) decides if we should wait again for half of mTimeoutMs
  // or give up.
  // only accessed on WorkerThread
  int32_t mTimeoutMs = kNoTimeout;
  bool mInTimeoutSecondHalf = false;

  // Worker-thread only; sequence numbers for messages that require
  // replies.
  seqno_t mNextSeqno = 0;

  static bool sIsPumpingMessages;

  // If ::Send returns false, this gives a more descriptive error.
  SyncSendError mLastSendError = SyncSendError::SendSuccess;

  template <class T>
  class AutoSetValue {
   public:
    explicit AutoSetValue(T& var, const T& newValue)
        : mVar(var), mPrev(var), mNew(newValue) {
      mVar = newValue;
    }
    ~AutoSetValue() {
      // The value may have been zeroed if the transaction was
      // canceled. In that case we shouldn't return it to its previous
      // value.
      if (mVar == mNew) {
        mVar = mPrev;
      }
    }

   private:
    T& mVar;
    T mPrev;
    T mNew;
  };

  bool mDispatchingAsyncMessage = false;
  int mDispatchingAsyncMessageNestedLevel = 0;

  // When we send an urgent request from the parent process, we could race
  // with an RPC message that was issued by the child beforehand. In this
  // case, if the parent were to wake up while waiting for the urgent reply,
  // and process the RPC, it could send an additional urgent message. The
  // child would wake up to process the urgent message (as it always will),
  // then send a reply, which could be received by the parent out-of-order
  // with respect to the first urgent reply.
  //
  // To address this problem, urgent or RPC requests are associated with a
  // "transaction". Whenever one side of the channel wishes to start a
  // chain of RPC/urgent messages, it allocates a new transaction ID. Any
  // messages the parent receives, not apart of this transaction, are
  // deferred. When issuing RPC/urgent requests on top of a started
  // transaction, the initiating transaction ID is used.
  //
  // To ensure IDs are unique, we use sequence numbers for transaction IDs,
  // which grow in opposite directions from child to parent.

  friend class AutoEnterTransaction;
  AutoEnterTransaction* mTransactionStack MOZ_GUARDED_BY(*mMonitor) = nullptr;

  seqno_t CurrentNestedInsideSyncTransaction() const MOZ_REQUIRES(*mMonitor);

  bool AwaitingSyncReply() const MOZ_REQUIRES(*mMonitor);
  int AwaitingSyncReplyNestedLevel() const MOZ_REQUIRES(*mMonitor);

  bool DispatchingSyncMessage() const MOZ_REQUIRES(*mMonitor);
  int DispatchingSyncMessageNestedLevel() const MOZ_REQUIRES(*mMonitor);

#ifdef DEBUG
  void AssertMaybeDeferredCountCorrect() MOZ_REQUIRES(*mMonitor);
#else
  void AssertMaybeDeferredCountCorrect() MOZ_REQUIRES(*mMonitor) {}
#endif

  // If a sync message times out, we store its sequence number here. Any
  // future sync messages will fail immediately. Once the reply for original
  // sync message is received, we allow sync messages again.
  //
  // When a message times out, nothing is done to inform the other side. The
  // other side will eventually dispatch the message and send a reply. Our
  // side is responsible for replying to all sync messages sent by the other
  // side when it dispatches the timed out message. The response is always an
  // error.
  //
  // A message is only timed out if it initiated a transaction. This avoids
  // hitting a lot of corner cases with message nesting that we don't really
  // care about.
  seqno_t mTimedOutMessageSeqno MOZ_GUARDED_BY(*mMonitor) = 0;
  int mTimedOutMessageNestedLevel MOZ_GUARDED_BY(*mMonitor) = 0;

  // Queue of all incoming messages.
  //
  // If both this side and the other side are functioning correctly, the other
  // side can send as many async messages as it wants before sending us a
  // blocking message.  After sending a blocking message, the other side must be
  // blocked, and thus can't send us any more messages until we process the sync
  // in-msg.
  //
  MessageQueue mPending MOZ_GUARDED_BY(*mMonitor);

  // The number of messages in mPending for which IsAlwaysDeferred is false
  // (i.e., the number of messages that might not be deferred, depending on
  // context).
  size_t mMaybeDeferredPendingCount MOZ_GUARDED_BY(*mMonitor) = 0;

  // Is there currently MessageChannel logic for this channel on the C++ stack?
  // This member is only accessed on the worker thread, and so is not protected
  // by mMonitor.
  bool mOnCxxStack = false;

#ifdef XP_WIN
  HANDLE mEvent;
#endif

  // Should the channel abort the process from the I/O thread when
  // a channel error occurs?
  bool mAbortOnError MOZ_GUARDED_BY(*mMonitor) = false;

  // True if the listener has already been notified of a channel close or
  // error.
  bool mNotifiedChannelDone MOZ_GUARDED_BY(*mMonitor) = false;

  // See SetChannelFlags
  ChannelFlags mFlags = REQUIRE_DEFAULT;

  bool mBuildIDsConfirmedMatch MOZ_GUARDED_BY(*mMonitor) = false;

  // If this is true, both ends of this message channel have event targets
  // on the same thread.
  bool mIsSameThreadChannel = false;
};

void CancelCPOWs();

}  // namespace ipc
}  // namespace mozilla

namespace IPC {
template <>
struct ParamTraits<mozilla::ipc::ResponseRejectReason>
    : public ContiguousEnumSerializer<
          mozilla::ipc::ResponseRejectReason,
          mozilla::ipc::ResponseRejectReason::SendError,
          mozilla::ipc::ResponseRejectReason::EndGuard_> {};
}  // namespace IPC

namespace geckoprofiler::markers {

struct IPCMarker : public mozilla::BaseMarkerType<IPCMarker> {
  static constexpr const char* Name = "IPC";
  static constexpr bool UseSpecialFrontendLocation = true;

  using MS = mozilla::MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      // This payload still streams a startTime and endTime property because it
      // made the migration to MarkerTiming on the front-end easier.
      {"startTime", MS::InputType::TimeStamp, nullptr, MS::Format::Time},
      {"endTime", MS::InputType::TimeStamp, nullptr, MS::Format::Time},
      {"otherPid", MS::InputType::Int32, nullptr, MS::Format::Integer},
      {"messageSeqno", MS::InputType::Int64, nullptr, MS::Format::Integer},
      // These four are plain strings rather than unique strings, because
      // profiler.firefox.com reads them straight off the raw payload when it
      // correlates IPC markers, before it resolves any unique string.
      {"messageType", MS::InputType::CString},
      {"side", MS::InputType::CString},
      {"direction", MS::InputType::CString},
      {"phase", MS::InputType::CString},
      {"sync", MS::InputType::Boolean},
      {"threadId", MS::InputType::Int64, nullptr, MS::Format::Integer},
  };

  static void TranslateMarkerInputToSchema(
      void* aContext, mozilla::TimeStamp aStart, mozilla::TimeStamp aEnd,
      int32_t aOtherPid, IPC::Message::seqno_t aMessageSeqno,
      IPC::Message::msgid_t aMessageType, mozilla::ipc::Side aSide,
      mozilla::ipc::MessageDirection aDirection,
      mozilla::ipc::MessagePhase aPhase, bool aSync,
      mozilla::MarkerThreadId aOriginThreadId) {
    ETW::OutputMarkerSchema(
        aContext, IPCMarker{}, aStart, aEnd, aOtherPid,
        static_cast<int64_t>(aMessageSeqno),
        mozilla::ProfilerString8View::WrapNullTerminatedString(
            IPC::StringFromIPCMessageType(aMessageType)),
        IPCSideToString(aSide), IPCDirectionToString(aDirection),
        IPCPhaseToString(aPhase), aSync,
        static_cast<int64_t>(aOriginThreadId.ThreadId().ToNumber()));
  }

  static void StreamJSONMarkerData(
      mozilla::baseprofiler::SpliceableJSONWriter& aWriter,
      mozilla::TimeStamp aStart, mozilla::TimeStamp aEnd, int32_t aOtherPid,
      IPC::Message::seqno_t aMessageSeqno, IPC::Message::msgid_t aMessageType,
      mozilla::ipc::Side aSide, mozilla::ipc::MessageDirection aDirection,
      mozilla::ipc::MessagePhase aPhase, bool aSync,
      mozilla::MarkerThreadId aOriginThreadId) {
    StreamJSONMarkerDataImpl(
        aWriter, aStart, aEnd, aOtherPid, static_cast<int64_t>(aMessageSeqno),
        mozilla::ProfilerString8View::WrapNullTerminatedString(
            IPC::StringFromIPCMessageType(aMessageType)),
        IPCSideToString(aSide), IPCDirectionToString(aDirection),
        IPCPhaseToString(aPhase), aSync);
    if (!aOriginThreadId.IsUnspecified()) {
      // Tech note: If `ToNumber()` returns a uint64_t, the conversion to
      // int64_t is "implementation-defined" before C++20. This is acceptable
      // here, because this is a one-way conversion to a unique identifier
      // that's used to visually separate data by thread on the front-end.
      aWriter.IntProperty(
          "threadId",
          static_cast<int64_t>(aOriginThreadId.ThreadId().ToNumber()));
    }
  }

 private:
  static mozilla::ProfilerString8View IPCSideToString(
      mozilla::ipc::Side aSide) {
    switch (aSide) {
      case mozilla::ipc::ParentSide:
        return "parent";
      case mozilla::ipc::ChildSide:
        return "child";
      case mozilla::ipc::UnknownSide:
        return "unknown";
      default:
        MOZ_ASSERT_UNREACHABLE("Invalid IPC side");
        return "<invalid IPC side>";
    }
  }

  static mozilla::ProfilerString8View IPCDirectionToString(
      mozilla::ipc::MessageDirection aDirection) {
    switch (aDirection) {
      case mozilla::ipc::MessageDirection::eSending:
        return "sending";
      case mozilla::ipc::MessageDirection::eReceiving:
        return "receiving";
      default:
        MOZ_ASSERT_UNREACHABLE("Invalid IPC direction");
        return "<invalid IPC direction>";
    }
  }

  static mozilla::ProfilerString8View IPCPhaseToString(
      mozilla::ipc::MessagePhase aPhase) {
    switch (aPhase) {
      case mozilla::ipc::MessagePhase::Endpoint:
        return "endpoint";
      case mozilla::ipc::MessagePhase::TransferStart:
        return "transferStart";
      case mozilla::ipc::MessagePhase::TransferEnd:
        return "transferEnd";
      default:
        MOZ_ASSERT_UNREACHABLE("Invalid IPC phase");
        return "<invalid IPC phase>";
    }
  }
};

}  // namespace geckoprofiler::markers

#endif  // ifndef ipc_glue_MessageChannel_h
