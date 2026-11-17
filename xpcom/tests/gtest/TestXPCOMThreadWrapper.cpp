/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <functional>
#include <string>

#include "VideoUtils.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/EventTargetCapability.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/TaskDispatcher.h"
#include "mozilla/TaskQueue.h"
#include "nsThreadUtils.h"

using testing::InSequence;
using testing::MockFunction;

namespace mozilla {

namespace {

class TestShutdownTask final : public nsITargetShutdownTask {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  explicit TestShutdownTask(std::function<void()> aCallback)
      : mCallback(std::move(aCallback)) {}

  void TargetShutdown() override { mCallback(); }

 private:
  ~TestShutdownTask() = default;
  const std::function<void()> mCallback;
};

NS_IMPL_ISUPPORTS(TestShutdownTask, nsITargetShutdownTask)

}  // namespace

// An XPCOM thread running a runnable that holds an AutoXPCOMThreadWrapper and
// spins a nested event loop, the way a worker thread does while it runs a
// worker. Tasks in these tests are dispatched with the plain nsIThread API, not
// through the wrapper, to cover tail dispatch from tasks the wrapper knows
// nothing about.
class XPCOMThreadWrapperTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MOZ_ALWAYS_SUCCEEDS(
        NS_NewNamedThread("XPCOMThreadWrap", getter_AddRefs(mThread)));
    StartRunnable();
  }

  void TearDown() override {
    StopRunnable();
    if (mThread) {
      MOZ_ALWAYS_SUCCEEDS(mThread->Shutdown());
    }
  }

  void StartRunnable() {
    AssertIsOnMainThread();
    MOZ_ASSERT(!mRunnableRunning);
    mRunnableRunning = true;
    MOZ_ALWAYS_SUCCEEDS(
        mThread->Dispatch(NS_NewRunnableFunction("Runnable", [this] {
          ASSERT_FALSE(AbstractThread::GetCurrent());
          {
            AutoXPCOMThreadWrapper wrapper(
                TailDispatchPolicy::ConsistentOrdering);
            ASSERT_EQ(AbstractThread::GetCurrent(), wrapper.get());
            mWrapperCapability.emplace(wrapper.get());
            mWrapperCapability->AssertOnCurrentThread();
            mStopRequested = false;
            MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(
                NS_NewRunnableFunction("Started", [this] {
                  AssertIsOnMainThread();
                  mStarted = true;
                })));
            MOZ_ALWAYS_TRUE(SpinEventLoopUntil(
                "XPCOMThreadWrapperTest::Runnable"_ns, [this] {
                  mWrapperCapability->AssertOnCurrentThread();
                  return mStopRequested;
                }));
            NS_ProcessPendingEvents(nullptr);
          }
          EXPECT_FALSE(AbstractThread::GetCurrent());
          MOZ_ALWAYS_SUCCEEDS(
              NS_DispatchToMainThread(NS_NewRunnableFunction("Ended", [this] {
                AssertIsOnMainThread();
                mRunnableRunning = false;
              })));
        })));
    MOZ_ALWAYS_TRUE(
        SpinEventLoopUntil("XPCOMThreadWrapperTest::Started"_ns, [this] {
          AssertIsOnMainThread();
          return mStarted;
        }));
  }

  void StopRunnable() {
    AssertIsOnMainThread();
    if (!mRunnableRunning) {
      return;
    }
    // Runs on mThread, where mStopRequested is read.
    MOZ_ALWAYS_SUCCEEDS(
        mThread->Dispatch(NS_NewRunnableFunction("Stop", [this] {
          mWrapperCapability->AssertOnCurrentThread();
          mStopRequested = true;
        })));
    MOZ_ALWAYS_TRUE(
        SpinEventLoopUntil("XPCOMThreadWrapperTest::Stopped"_ns, [this] {
          AssertIsOnMainThread();
          return !mRunnableRunning;
        }));
    mWrapperCapability = Nothing();
    mStarted = false;
  }

  // Runs aFunction on mThread as a plain task and waits for it to finish, so
  // that everything queued during aFunction, including its tail-dispatched
  // tasks, has run when this returns. While the runnable runs, aFunction
  // runs in its nested event loop.
  template <typename Function>
  void RunOnThread(Function&& aFunction) {
    MOZ_ALWAYS_SUCCEEDS(NS_DispatchAndSpinEventLoopUntilComplete(
        "XPCOMThreadWrapperTest::RunOnThread"_ns, mThread,
        NS_NewRunnableFunction(__func__, std::forward<Function>(aFunction))));
  }

  nsCOMPtr<nsIThread> mThread;
  // Set on mThread while the runnable's wrapper exists. Read on mThread, and
  // on the main thread once mStarted is set and once mRunnableRunning is clear.
  Maybe<EventTargetCapability<AbstractThread>> mWrapperCapability;
  // Only on the main thread.
  bool mStarted MOZ_GUARDED_BY(sMainThreadCapability) = false;
  bool mRunnableRunning MOZ_GUARDED_BY(sMainThreadCapability) = false;
  // Only on mThread.
  bool mStopRequested MOZ_GUARDED_BY(mWrapperCapability) = false;
  // Steps of a test, in the order they are expected to happen.
  MockFunction<void(const std::string&)> mCheckpoint;
};

TEST_F(XPCOMThreadWrapperTest, DirectTaskFromPlainTask) {
  InSequence sequence;
  EXPECT_CALL(mCheckpoint, Call("plain task"));
  EXPECT_CALL(mCheckpoint, Call("direct task"));
  EXPECT_CALL(mCheckpoint, Call("next task"));
  RunOnThread([&] {
    AbstractThread* current = AbstractThread::GetCurrent();
    ASSERT_EQ(current, mWrapperCapability->GetEventTarget());
    ASSERT_TRUE(current->IsTailDispatcherAvailable());

    // Queued before the direct task is added, but must run after it.
    MOZ_ALWAYS_SUCCEEDS(mThread->Dispatch(NS_NewRunnableFunction(
        "NextTask", [&] { mCheckpoint.Call("next task"); })));
    current->TailDispatcher().AddDirectTask(NS_NewRunnableFunction(
        "DirectTask", [&] { mCheckpoint.Call("direct task"); }));
    mCheckpoint.Call("plain task");
  });
}

TEST_F(XPCOMThreadWrapperTest, DirectTaskRunsBeforeNestedEventLoop) {
  InSequence sequence;
  EXPECT_CALL(mCheckpoint, Call("direct task"));
  EXPECT_CALL(mCheckpoint, Call("nested task"));
  EXPECT_CALL(mCheckpoint, Call("after nested event loop"));
  RunOnThread([&] {
    AbstractThread* current = AbstractThread::GetCurrent();
    current->TailDispatcher().AddDirectTask(NS_NewRunnableFunction(
        "DirectTask", [&] { mCheckpoint.Call("direct task"); }));
    // Spinning a nested event loop fires the tail dispatcher first.
    MOZ_ALWAYS_SUCCEEDS(NS_DispatchAndSpinEventLoopUntilComplete(
        "Nested"_ns, mThread, NS_NewRunnableFunction("Nested", [&] {
          mCheckpoint.Call("nested task");
        })));
    mCheckpoint.Call("after nested event loop");
  });
}

TEST_F(XPCOMThreadWrapperTest, DispatchToTaskQueueIsTailDispatched) {
  RefPtr<TaskQueue> queue = TaskQueue::Create(
      GetMediaThreadPool(MediaThreadType::SUPERVISOR), "TestXPCOMThreadWrapper",
      TailDispatchPolicy::ConsistentOrdering);
  // A plain dispatch to an AbstractThread supporting tail dispatch, from a
  // plain task on the wrapped thread, is tail dispatched: it goes out when
  // this task finishes.
  InSequence sequence;
  EXPECT_CALL(mCheckpoint, Call("end of plain task"));
  EXPECT_CALL(mCheckpoint, Call("queue task"));
  RunOnThread([&] {
    MOZ_ALWAYS_SUCCEEDS(queue->Dispatch(NS_NewRunnableFunction(
        "OnQueue", [&] { mCheckpoint.Call("queue task"); })));
    mCheckpoint.Call("end of plain task");
  });
  // The tail dispatcher fires after the task above has returned, so wait for
  // the next task on the thread before waiting for the queue.
  RunOnThread([] {});
  queue->AwaitIdle();
  queue->BeginShutdown();
  queue->AwaitShutdownAndIdle();
}

TEST_F(XPCOMThreadWrapperTest, UnregisteredWrapperRefusesWork) {
  EXPECT_CALL(mCheckpoint, Call("refused task")).Times(0);
  RefPtr<AbstractThread> wrapper = mWrapperCapability->GetEventTarget();
  StopRunnable();
  RunOnThread([&] {
    EXPECT_FALSE(AbstractThread::GetCurrent());
    EXPECT_TRUE(wrapper->IsCurrentThreadIn());
  });
  EXPECT_EQ(wrapper->Dispatch(NS_NewRunnableFunction(
                "Refused", [&] { mCheckpoint.Call("refused task"); })),
            NS_ERROR_FAILURE);
  RunOnThread([] {});
  // The last reference goes away on another thread than the wrapped one.
  wrapper = nullptr;
}

TEST_F(XPCOMThreadWrapperTest, ThreadCanBeReused) {
  // Kept alive so that a new wrapper cannot reuse its address.
  RefPtr<AbstractThread> first = mWrapperCapability->GetEventTarget();
  StopRunnable();
  StartRunnable();
  RunOnThread([&] {
    AbstractThread* current = AbstractThread::GetCurrent();
    EXPECT_EQ(current, mWrapperCapability->GetEventTarget());
    EXPECT_TRUE(current->IsCurrentThreadIn());
  });
  EXPECT_NE(mWrapperCapability->GetEventTarget(), first.get());
}

TEST_F(XPCOMThreadWrapperTest, ShutdownTasksAreRegisteredWithTheThread) {
  auto task = MakeRefPtr<TestShutdownTask>([] { ADD_FAILURE(); });
  MOZ_ALWAYS_SUCCEEDS(
      mWrapperCapability->GetEventTarget()->RegisterShutdownTask(task));
  EXPECT_EQ(mThread->UnregisterShutdownTask(task), NS_OK);
}

TEST_F(XPCOMThreadWrapperTest, ShutdownTasksRunWhenTheThreadShutsDown) {
  InSequence sequence;
  EXPECT_CALL(mCheckpoint, Call("after runnable"));
  EXPECT_CALL(mCheckpoint, Call("shutdown task"));
  auto task = MakeRefPtr<TestShutdownTask>([this] {
    EXPECT_FALSE(AbstractThread::GetCurrent());
    mCheckpoint.Call("shutdown task");
  });
  MOZ_ALWAYS_SUCCEEDS(
      mWrapperCapability->GetEventTarget()->RegisterShutdownTask(task));
  StopRunnable();
  mCheckpoint.Call("after runnable");
  MOZ_ALWAYS_SUCCEEDS(mThread->Shutdown());
  mThread = nullptr;
}

TEST_F(XPCOMThreadWrapperTest,
       DelayedDispatchIsCanceledWhenTheThreadShutsDown) {
  EXPECT_CALL(mCheckpoint, Call("delayed task")).Times(0);
  auto canary = std::make_shared<bool>();
  MOZ_ALWAYS_SUCCEEDS(mWrapperCapability->GetEventTarget()->DelayedDispatch(
      NS_NewRunnableFunction(
          "Delayed", [this, canary] { mCheckpoint.Call("delayed task"); }),
      60 * 60 * 1000));
  StopRunnable();
  MOZ_ALWAYS_SUCCEEDS(mThread->Shutdown());
  mThread = nullptr;
  // The DelayedRunnable's shutdown task has released the runnable.
  EXPECT_EQ(canary.use_count(), 1);
}

}  // namespace mozilla
