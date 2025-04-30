/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_WebTaskScheduler_h
#define mozilla_dom_WebTaskScheduler_h

#include "nsThreadUtils.h"
#include "nsPIDOMWindow.h"
#include "nsWrapperCache.h"
#include "nsClassHashtable.h"

#include "TaskSignal.h"
#include "mozilla/Variant.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/AbortFollower.h"
#include "mozilla/dom/TimeoutHandler.h"
#include "mozilla/dom/WebTaskSchedulingBinding.h"

namespace mozilla::dom {

// Keep tracks of the number of same-event-loop-high-priority-queues
// (User_blocking or User_visible) that have at least one task scheduled.
MOZ_CONSTINIT extern uint32_t
    gNumNormalOrHighPriorityQueuesHaveTaskScheduledMainThread;

// https://wicg.github.io/scheduling-apis/#scheduling-state
class WebTaskSchedulingState {
 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebTaskSchedulingState)
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(WebTaskSchedulingState)

  void Reset() {
    mAbortSource = nullptr;
    mPrioritySource = nullptr;
  }

  void SetAbortSource(AbortSignal* aAbortSource) {
    mAbortSource = aAbortSource;
  }

  AbortSignal* GetAbortSource() { return mAbortSource; }
  TaskSignal* GetPrioritySource() { return mPrioritySource; }

  void SetPrioritySource(TaskSignal* aPrioritySource) {
    MOZ_ASSERT(aPrioritySource->IsTaskSignal());
    mPrioritySource = aPrioritySource;
  }

 private:
  ~WebTaskSchedulingState() = default;

  RefPtr<AbortSignal> mAbortSource;
  RefPtr<TaskSignal> mPrioritySource;
};

class WebTaskQueueHashKey : public PLDHashEntryHdr {
 public:
  enum { ALLOW_MEMMOVE = false };

  typedef const WebTaskQueueHashKey& KeyType;
  typedef const WebTaskQueueHashKey* KeyTypePointer;

  using StaticPriorityTaskQueueKey = uint32_t;
  using DynamicPriorityTaskQueueKey = RefPtr<TaskSignal>;

  // When WebTaskQueueTypeKey is RefPtr<TaskSignal>, this
  // class holds a strong reference to a cycle collectable
  // objects.
  using WebTaskQueueTypeKey =
      mozilla::Variant<StaticPriorityTaskQueueKey, DynamicPriorityTaskQueueKey>;

  WebTaskQueueHashKey(StaticPriorityTaskQueueKey aKey, bool aIsContinuation)
      : mKey(aKey), mIsContinuation(aIsContinuation) {}

  WebTaskQueueHashKey(DynamicPriorityTaskQueueKey aKey, bool aIsContinuation)
      : mKey(aKey), mIsContinuation(aIsContinuation) {}

  explicit WebTaskQueueHashKey(KeyTypePointer aKey)
      : mKey(aKey->mKey), mIsContinuation(aKey->mIsContinuation) {}

  explicit WebTaskQueueHashKey(KeyType aKey)
      : mKey(aKey.mKey), mIsContinuation(aKey.mIsContinuation) {}

  WebTaskQueueHashKey(WebTaskQueueHashKey&& aToMove) = default;

  ~WebTaskQueueHashKey() = default;

  KeyType GetKey() const { return *this; }

  bool KeyEquals(KeyTypePointer aKey) const {
    return aKey->mKey == mKey && aKey->mIsContinuation == mIsContinuation;
  }

  // https://wicg.github.io/scheduling-apis/#scheduler-task-queue-effective-priority
  uint8_t EffectivePriority() const {
    switch (Priority()) {
      case TaskPriority::Background:
        return mIsContinuation ? 1 : 0;
      case TaskPriority::User_visible:
        return mIsContinuation ? 3 : 2;
      case TaskPriority::User_blocking:
        return mIsContinuation ? 5 : 4;
      default:
        MOZ_ASSERT_UNREACHABLE("Unexpected priority");
        return 0;
    }
  }

  TaskPriority Priority() const {
    return mKey.match(
        [&](const StaticPriorityTaskQueueKey& aStaticKey) {
          return static_cast<TaskPriority>(aStaticKey);
        },
        [&](const DynamicPriorityTaskQueueKey& aDynamicKey) {
          return aDynamicKey->Priority();
        });
  }

  static KeyTypePointer KeyToPointer(KeyType& aKey) { return &aKey; }

  static PLDHashNumber HashKey(KeyTypePointer aKey) {
    const WebTaskQueueTypeKey& key = aKey->mKey;
    return key.match(
        [&](const StaticPriorityTaskQueueKey& aStaticKey) {
          return mozilla::HashGeneric(aStaticKey, aKey->mIsContinuation);
        },
        [&](const DynamicPriorityTaskQueueKey& aDynamicKey) {
          return mozilla::HashGeneric(aDynamicKey.get(), aKey->mIsContinuation);
        });
  }

  WebTaskQueueTypeKey& GetTypeKey() { return mKey; }
  const WebTaskQueueTypeKey& GetTypeKey() const { return mKey; }

 private:
  WebTaskQueueTypeKey mKey;
  const bool mIsContinuation;
};

class WebTask : public LinkedListElement<RefPtr<WebTask>>,
                public AbortFollower,
                public SupportsWeakPtr {
  friend class WebTaskScheduler;

 public:
  MOZ_CAN_RUN_SCRIPT bool Run();

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  NS_DECL_CYCLE_COLLECTION_CLASS(WebTask)
  WebTask(uint32_t aEnqueueOrder,
          const Maybe<SchedulerPostTaskCallback&>& aCallback,
          WebTaskSchedulingState* aSchedulingState, Promise* aPromise,
          WebTaskScheduler* aWebTaskScheduler,
          const WebTaskQueueHashKey& aHashKey);

  void RunAbortAlgorithm() override;

  bool HasScheduled() const { return mHasScheduled; }

  uint32_t EnqueueOrder() const { return mEnqueueOrder; }

  void ClearWebTaskScheduler() { mScheduler = nullptr; }

  const WebTaskQueueHashKey& TaskQueueHashKey() const {
    return mWebTaskQueueHashKey;
  }

  TaskPriority Priority() const { return mWebTaskQueueHashKey.Priority(); }

 private:
  void SetHasScheduled() {
    MOZ_ASSERT(!mHasScheduled);
    mHasScheduled = true;
  }

  uint32_t mEnqueueOrder;

  RefPtr<SchedulerPostTaskCallback> mCallback;
  RefPtr<Promise> mPromise;

  bool mHasScheduled;

  RefPtr<WebTaskSchedulingState> mSchedulingState;

  // WebTaskScheduler owns WebTaskQueue, and WebTaskQueue owns WebTask, so it's
  // okay to use a raw pointer
  WebTaskScheduler* mScheduler;

  // Depending on whether this task was scheduled with static priority
  // or dynamic priority, it could hold a reference reference to TaskSignal
  // (cycle collectable object).
  WebTaskQueueHashKey mWebTaskQueueHashKey;

  ~WebTask() = default;
};

class WebTaskQueue {
 public:
  static constexpr int EffectivePriorityCount = 6;

  explicit WebTaskQueue(WebTaskScheduler* aScheduler) : mScheduler(aScheduler) {
    MOZ_ASSERT(aScheduler);
  }

  WebTaskQueue(WebTaskQueue&& aWebTaskQueue) = default;

  ~WebTaskQueue();

  TaskPriority Priority() const { return mPriority; }
  void SetPriority(TaskPriority aNewPriority) { mPriority = aNewPriority; }

  LinkedList<RefPtr<WebTask>>& Tasks() { return mTasks; }
  const LinkedList<RefPtr<WebTask>>& Tasks() const { return mTasks; }

  void AddTask(WebTask* aTask) { mTasks.insertBack(aTask); }

  bool IsEmpty() const { return mTasks.isEmpty(); }

  // TODO: To optimize it, we could have the scheduled and unscheduled
  // tasks stored separately.
  WebTask* GetFirstScheduledTask() {
    for (const auto& task : mTasks) {
      if (task->HasScheduled()) {
        return task;
      }
    }
    return nullptr;
  }

  bool HasScheduledTasks() const {
    if (mTasks.isEmpty()) {
      return false;
    }

    for (const auto& task : mTasks) {
      if (task->HasScheduled()) {
        return true;
      }
    }
    return false;
  }

 private:
  TaskPriority mPriority = TaskPriority::User_visible;
  LinkedList<RefPtr<WebTask>> mTasks;

  // WebTaskScheduler owns WebTaskQueue as a hashtable value, so using a raw
  // pointer points to WebTaskScheduler is ok.
  WebTaskScheduler* mScheduler;
};

class WebTaskSchedulerMainThread;
class WebTaskSchedulerWorker;

class WebTaskScheduler : public nsWrapperCache,
                         public SupportsWeakPtr,
                         public LinkedListElement<WebTaskScheduler> {
  friend class DelayedWebTaskHandler;

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebTaskScheduler)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(WebTaskScheduler)

  static already_AddRefed<WebTaskSchedulerMainThread> CreateForMainThread(
      nsGlobalWindowInner* aWindow);

  static already_AddRefed<WebTaskSchedulerWorker> CreateForWorker(
      WorkerPrivate* aWorkerPrivate);

  explicit WebTaskScheduler(nsIGlobalObject* aParent);

  already_AddRefed<Promise> PostTask(SchedulerPostTaskCallback& aCallback,
                                     const SchedulerPostTaskOptions& aOptions);

  already_AddRefed<Promise> YieldImpl();

  nsIGlobalObject* GetParentObject() const { return mParent; }

  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> aGivenProto) override;

  WebTask* GetNextTask(bool aIsMainThread);

  virtual void Disconnect();

  void RunTaskSignalPriorityChange(TaskSignal* aTaskSignal);

  void DeleteEntryFromWebTaskQueueMap(const WebTaskQueueHashKey& aKey) {
    DebugOnly<bool> result = mWebTaskQueues.Remove(aKey);
    MOZ_ASSERT(result);
  }

  void NotifyTaskWillBeRunOrAborted(const WebTask* aWebTask);
  virtual void IncreaseNumNormalOrHighPriorityQueuesHaveTaskScheduled() = 0;
  virtual void DecreaseNumNormalOrHighPriorityQueuesHaveTaskScheduled() = 0;

 protected:
  virtual ~WebTaskScheduler() = default;
  nsCOMPtr<nsIGlobalObject> mParent;

 private:
  struct SelectedTaskQueueData {
    WebTaskQueueHashKey mSelectedQueueHashKey;
    WebTaskQueue& mSelectedTaskQueue;
  };

  already_AddRefed<WebTask> CreateTask(
      AbortSignal* aAbortSignal, TaskSignal* aTaskSignal,
      const Optional<TaskPriority>& aPriority, const bool aIsContinuation,
      const Maybe<SchedulerPostTaskCallback&>& aCallback,
      WebTaskSchedulingState* aSchedulingState, Promise* aPromise);

  bool DispatchTask(WebTask* aTask, EventQueuePriority aPriority);

  SelectedTaskQueueData SelectTaskQueue(TaskSignal* aSignal,
                                        const Optional<TaskPriority>& aPriority,
                                        const bool aIsContinuation);

  virtual nsresult SetTimeoutForDelayedTask(WebTask* aTask, uint64_t aDelay,
                                            EventQueuePriority aPriority) = 0;
  virtual bool DispatchEventLoopRunnable(EventQueuePriority aPriority) = 0;

  EventQueuePriority GetEventQueuePriority(const TaskPriority& aPriority,
                                           bool aIsContinuation) const;

  nsTHashMap<WebTaskQueueHashKey, WebTaskQueue>& GetWebTaskQueues() {
    return mWebTaskQueues;
  }

  nsTHashMap<WebTaskQueueHashKey, WebTaskQueue> mWebTaskQueues;
};

class DelayedWebTaskHandler final : public TimeoutHandler {
 public:
  DelayedWebTaskHandler(JSContext* aCx, WebTaskScheduler* aScheduler,
                        WebTask* aTask, EventQueuePriority aPriority)
      : TimeoutHandler(aCx), mScheduler(aScheduler), mWebTask(aTask) {}

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(DelayedWebTaskHandler)

  MOZ_CAN_RUN_SCRIPT bool Call(const char* /* unused */) override {
    if (mScheduler && mWebTask && mWebTask->isInList()) {
      MOZ_ASSERT(!mWebTask->HasScheduled());
      if (!mScheduler->DispatchTask(mWebTask, mPriority)) {
        return false;
      }
    }
    return true;
  }

 private:
  ~DelayedWebTaskHandler() override = default;
  WeakPtr<WebTaskScheduler> mScheduler;
  // WebTask gets added to WebTaskQueue, and WebTaskQueue keeps its alive.
  WeakPtr<WebTask> mWebTask;
  EventQueuePriority mPriority;
};
}  // namespace mozilla::dom
#endif
