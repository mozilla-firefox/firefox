/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/thread.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/field_trials_view.h"
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/task_queue/task_queue_test.h"
#include "api/units/time_delta.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/async_udp_socket.h"
#include "rtc_base/byte_order.h"
#include "rtc_base/checks.h"
#include "rtc_base/event.h"
#include "rtc_base/fake_clock.h"
#include "rtc_base/internal/default_socket_server.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/null_socket_server.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/socket_factory.h"
#include "rtc_base/socket_server.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/thread_annotations.h"
#include "rtc_base/time_utils.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"
#include "test/testsupport/rtc_expect_death.h"
#include "test/wait_until.h"

#if defined(WEBRTC_WIN)
#include <comdef.h>  // NOLINT

#endif

namespace webrtc {
namespace {

using ::testing::ElementsAre;
using ::testing::IsNull;
using ::testing::NotNull;

class ScopedThread : public Thread {
 public:
  ScopedThread()
      : Thread(CreateDefaultSocketServer(), /*do_init=*/false),
        previous_thread_(ThreadManager::Instance()->CurrentThread()) {
    DoInit();
    ThreadManager::Instance()->SetCurrentThread(this);
  }

  ~ScopedThread() override {
    Stop();
    DoDestroy();
    RTC_DCHECK_EQ(ThreadManager::Instance()->CurrentThread(), this);
    ThreadManager::Instance()->SetCurrentThread(previous_thread_);
  }

 private:
  Thread* const previous_thread_;
};

// Generates a sequence of numbers (collaboratively).
class TestGenerator {
 public:
  TestGenerator() : last(0), count(0) {}

  int Next(int prev) {
    int result = prev + last;
    last = result;
    count += 1;
    return result;
  }

  int last;
  int count;
};

// Receives messages and sends on a socket.
class MessageClient : public TestGenerator {
 public:
  MessageClient(Thread* pth, Socket* socket) : socket_(socket) {}

  ~MessageClient() { delete socket_; }

  void OnValue(int value) {
    std::array<uint8_t, sizeof(uint32_t)> octets;
    SetLE32(octets, Next(value));
    EXPECT_GE(socket_->Send(octets.data(), octets.size()), 0);
  }

 private:
  Socket* socket_;
};

// Receives on a socket and sends by posting messages.
class SocketClient : public TestGenerator {
 public:
  SocketClient(SocketFactory* socket_factory,
               const SocketAddress& addr,
               Thread* post_thread,
               MessageClient* phandler)
      : socket_(AsyncUDPSocket::Create(CreateTestEnvironment(),
                                       addr,
                                       *socket_factory)),
        post_thread_(post_thread),
        post_handler_(phandler) {
    socket_->RegisterReceivedPacketCallback(
        [&](AsyncPacketSocket* socket, const ReceivedIpPacket& packet) {
          OnPacket(socket, packet);
        });
  }

  ~SocketClient() = default;

  SocketAddress address() const { return socket_->GetLocalAddress(); }

  void OnPacket(AsyncPacketSocket* socket, const ReceivedIpPacket& packet) {
    EXPECT_EQ(packet.payload().size(), sizeof(uint32_t));
    uint32_t prev = GetLE32(packet.payload());
    uint32_t result = Next(prev);

    post_thread_->PostDelayedTask([post_handler_ = post_handler_,
                                   result] { post_handler_->OnValue(result); },
                                  TimeDelta::Millis(200));
  }

 private:
  std::unique_ptr<AsyncUDPSocket> socket_;
  Thread* post_thread_;
  MessageClient* post_handler_;
};

class CustomThread : public Thread {
 public:
  CustomThread()
      : Thread(std::unique_ptr<SocketServer>(new NullSocketServer())) {}
  ~CustomThread() override { Stop(); }
  bool Start() { return false; }

  bool WrapCurrent() { return Thread::WrapCurrent(); }
  void UnwrapCurrent() { Thread::UnwrapCurrent(); }
};

// A thread that does nothing when it runs and signals an event
// when it is destroyed.
class SignalWhenDestroyedThread : public Thread {
 public:
  SignalWhenDestroyedThread(Event* event)
      : Thread(std::unique_ptr<SocketServer>(new NullSocketServer())),
        event_(event) {}

  ~SignalWhenDestroyedThread() override {
    Stop();
    event_->Set();
  }

  void Run() override {
    // Do nothing.
  }

 private:
  Event* event_;
};

// See: https://code.google.com/p/webrtc/issues/detail?id=2409
TEST(ThreadTest, DISABLED_Main) {
  ScopedThread main_thread;
  const SocketAddress addr("127.0.0.1", 0);

  // Create the messaging client on its own thread.
  std::unique_ptr<Thread> th1 = Thread::CreateWithSocketServer();
  Socket* socket = th1->socketserver()->CreateSocket(addr.family(), SOCK_DGRAM);
  MessageClient msg_client(th1.get(), socket);

  // Create the socket client on its own thread.
  std::unique_ptr<Thread> th2 = Thread::CreateWithSocketServer();
  SocketClient sock_client(th2->socketserver(), addr, th1.get(), &msg_client);

  socket->Connect(sock_client.address());

  th1->Start();
  th2->Start();

  // Get the messages started.
  th1->PostDelayedTask([&msg_client] { msg_client.OnValue(1); },
                       TimeDelta::Millis(100));

  // Give the clients a little while to run.
  // Messages will be processed at 100, 300, 500, 700, 900.
  Thread* th_main = Thread::Current();
  th_main->ProcessMessages(1000);

  // Stop the sending client. Give the receiver a bit longer to run, in case
  // it is running on a machine that is under load (e.g. the build machine).
  th1->Stop();
  th_main->ProcessMessages(200);
  th2->Stop();

  // Make sure the results were correct
  EXPECT_EQ(msg_client.count, 5);
  EXPECT_EQ(msg_client.last, 34);
  EXPECT_EQ(sock_client.count, 5);
  EXPECT_EQ(sock_client.last, 55);
}

// Tests that the implementation behind
// `RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS` doesn't cause problems (crash or
// DCHECK) when used on a thread that does not have an attached current
// `Thread*` instance.
TEST(ThreadTest, DisallowBlockingCallsNoThread) {
  ASSERT_THAT(Thread::Current(), IsNull());
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
}

TEST(ThreadTest, DisallowBlockingCallsWithThread) {
  ScopedThread current;
  RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
}

TEST(ThreadTest, CountBlockingCalls) {
  ScopedThread current;

  // When the test runs, this will print out:
  //   (thread_unittest.cc:262): Blocking TestBody: total=2 (actual=1, could=1)
  RTC_LOG_THREAD_BLOCK_COUNT();
#if RTC_DCHECK_IS_ON
  Thread::ScopedCountBlockingCalls blocked_calls(
      [&](uint32_t actual_block, uint32_t could_block, TimeDelta duration) {
        EXPECT_EQ(actual_block, 1u);
        EXPECT_EQ(could_block, 1u);
      });

  EXPECT_EQ(blocked_calls.GetBlockingCallCount(), 0u);
  EXPECT_EQ(blocked_calls.GetCouldBeBlockingCallCount(), 0u);
  EXPECT_EQ(blocked_calls.GetTotalBlockedCallCount(), 0u);

  // Test invoking on the current thread. This should not count as an 'actual'
  // invoke, but should still count as an invoke that could block since we
  // that the call to `BlockingCall` serves a purpose in some configurations
  // (and should not be used a general way to call methods on the same thread).
  current.BlockingCall([]() {});
  EXPECT_EQ(blocked_calls.GetBlockingCallCount(), 0u);
  EXPECT_EQ(blocked_calls.GetCouldBeBlockingCallCount(), 1u);
  EXPECT_EQ(blocked_calls.GetTotalBlockedCallCount(), 1u);

  // Create a new thread to invoke on.
  std::unique_ptr<Thread> thread = Thread::Create();
  thread->Start();
  EXPECT_EQ(thread->BlockingCall([]() { return 42; }), 42);
  EXPECT_EQ(blocked_calls.GetBlockingCallCount(), 1u);
  EXPECT_EQ(blocked_calls.GetCouldBeBlockingCallCount(), 1u);
  EXPECT_EQ(blocked_calls.GetTotalBlockedCallCount(), 2u);
  thread->Stop();
  RTC_DCHECK_BLOCK_COUNT_NO_MORE_THAN(2);
#else
  RTC_DCHECK_BLOCK_COUNT_NO_MORE_THAN(0);
  RTC_LOG(LS_INFO) << "Test not active in this config";
#endif
}

#if RTC_DCHECK_IS_ON
TEST(ThreadTest, CountBlockingCallsOneCallback) {
  ScopedThread current;
  bool was_called_back = false;
  {
    Thread::ScopedCountBlockingCalls blocked_calls(
        [&](uint32_t actual_block, uint32_t could_block, TimeDelta duration) {
          was_called_back = true;
        });
    current.BlockingCall([]() {});
  }
  EXPECT_TRUE(was_called_back);
}

TEST(ThreadTest, CountBlockingCallsSkipCallback) {
  ScopedThread current;
  bool was_called_back = false;
  {
    Thread::ScopedCountBlockingCalls blocked_calls(
        [&](uint32_t actual_block, uint32_t could_block, TimeDelta duration) {
          was_called_back = true;
        });
    // Changed `blocked_calls` to not issue the callback if there are 1 or
    // fewer blocking calls (i.e. we set the minimum required number to 2).
    blocked_calls.set_minimum_call_count_for_callback(2);
    current.BlockingCall([]() {});
  }
  // We should not have gotten a call back.
  EXPECT_FALSE(was_called_back);
}
#endif

// Test that setting thread names doesn't cause a malfunction.
// There's no easy way to verify the name was set properly at this time.
TEST(ThreadTest, Names) {
  // Default name
  std::unique_ptr<Thread> thread = Thread::Create();
  EXPECT_TRUE(thread->Start());
  thread->Stop();
  // Name with no object parameter
  thread = Thread::Create();
  EXPECT_TRUE(thread->SetName("No object", nullptr));
  EXPECT_TRUE(thread->Start());
  thread->Stop();
  // Really long name
  thread = Thread::Create();
  EXPECT_TRUE(thread->SetName("Abcdefghijklmnopqrstuvwxyz1234567890", this));
  EXPECT_TRUE(thread->Start());
  thread->Stop();
}

TEST(ThreadTest, Wrap) {
  Thread* current_thread = Thread::Current();
  ThreadManager::Instance()->SetCurrentThread(nullptr);

  {
    CustomThread cthread;
    EXPECT_TRUE(cthread.WrapCurrent());
    EXPECT_EQ(&cthread, Thread::Current());
    EXPECT_TRUE(cthread.RunningForTest());
    EXPECT_FALSE(cthread.IsOwned());
    cthread.UnwrapCurrent();
    EXPECT_FALSE(cthread.RunningForTest());
  }
  ThreadManager::Instance()->SetCurrentThread(current_thread);
}

#if (!defined(NDEBUG) || RTC_DCHECK_IS_ON)
TEST(ThreadTest, InvokeToThreadAllowedReturnsTrueWithoutPolicies) {
  ScopedThread main_thread;
  // Create and start the thread.
  std::unique_ptr<Thread> thread1 = Thread::Create();
  std::unique_ptr<Thread> thread2 = Thread::Create();

  thread1->PostTask(
      [&]() { EXPECT_TRUE(thread1->IsInvokeToThreadAllowed(thread2.get())); });
  main_thread.ProcessMessages(100);
}

TEST(ThreadTest, InvokeAllowedWhenThreadsAdded) {
  ScopedThread main_thread;
  // Create and start the thread.
  std::unique_ptr<Thread> thread1 = Thread::Create();
  std::unique_ptr<Thread> thread2 = Thread::Create();
  std::unique_ptr<Thread> thread3 = Thread::Create();
  std::unique_ptr<Thread> thread4 = Thread::Create();

  thread1->AllowInvokesToThread(thread2.get());
  thread1->AllowInvokesToThread(thread3.get());

  thread1->PostTask([&]() {
    EXPECT_TRUE(thread1->IsInvokeToThreadAllowed(thread2.get()));
    EXPECT_TRUE(thread1->IsInvokeToThreadAllowed(thread3.get()));
    EXPECT_FALSE(thread1->IsInvokeToThreadAllowed(thread4.get()));
  });
  main_thread.ProcessMessages(100);
}

TEST(ThreadTest, InvokesDisallowedWhenDisallowAllInvokes) {
  ScopedThread main_thread;
  // Create and start the thread.
  std::unique_ptr<Thread> thread1 = Thread::Create();
  std::unique_ptr<Thread> thread2 = Thread::Create();

  thread1->DisallowAllInvokes();

  thread1->PostTask(
      [&]() { EXPECT_FALSE(thread1->IsInvokeToThreadAllowed(thread2.get())); });
  main_thread.ProcessMessages(100);
}
#endif  // (!defined(NDEBUG) || RTC_DCHECK_IS_ON)

TEST(ThreadTest, InvokesAllowedByDefault) {
  ScopedThread main_thread;
  // Create and start the thread.
  std::unique_ptr<Thread> thread1 = Thread::Create();
  std::unique_ptr<Thread> thread2 = Thread::Create();

  thread1->PostTask(
      [&]() { EXPECT_TRUE(thread1->IsInvokeToThreadAllowed(thread2.get())); });
  main_thread.ProcessMessages(100);
}

TEST(ThreadTest, BlockingCall) {
  // Create and start the thread.
  std::unique_ptr<Thread> thread = Thread::Create();
  thread->Start();
  // Try calling functors.
  EXPECT_EQ(thread->BlockingCall([] { return 42; }), 42);
  bool called = false;
  thread->BlockingCall([&] { called = true; });
  EXPECT_TRUE(called);

  // Try calling bare functions.
  struct LocalFuncs {
    static int Func1() { return 999; }
    static void Func2() {}
  };
  EXPECT_EQ(thread->BlockingCall(&LocalFuncs::Func1), 999);
  thread->BlockingCall(&LocalFuncs::Func2);
}

// Verifies that two threads calling Invoke on each other at the same time does
// not deadlock but crash.
#if RTC_DCHECK_IS_ON && GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)
TEST(ThreadTest, TwoThreadsInvokeDeathTest) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  ScopedThread thread;
  Thread* main_thread = Thread::Current();
  std::unique_ptr<Thread> other_thread = Thread::Create();
  other_thread->Start();
  other_thread->BlockingCall([main_thread] {
    RTC_EXPECT_DEATH(main_thread->BlockingCall([] {}), "loop");
  });
}

TEST(ThreadTest, ThreeThreadsInvokeDeathTest) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  ScopedThread thread;
  Thread* first = Thread::Current();

  std::unique_ptr<Thread> second = Thread::Create();
  second->Start();
  std::unique_ptr<Thread> third = Thread::Create();
  third->Start();

  second->BlockingCall([&] {
    third->BlockingCall(
        [&] { RTC_EXPECT_DEATH(first->BlockingCall([] {}), "loop"); });
  });
}

TEST(ThreadTest, DisallowBlockingCallDeathTest) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  ScopedThread thread;
  ASSERT_THAT(Thread::Current(), NotNull());
  std::unique_ptr<Thread> other_thread = Thread::Create();
  other_thread->Start();
  {
    RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
    RTC_EXPECT_DEATH(other_thread->BlockingCall([] {}),
                     "blocking_calls_allowed_");
  }
}
#endif

#if GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)
TEST(ThreadTest, JoinOnCurrentThreadDeathTest) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  RTC_EXPECT_DEATH(
      {
        std::unique_ptr<Thread> thread = Thread::Create();
        thread->Start();
        Thread* thread_ptr = thread.release();
        thread_ptr->PostTask([thread_ptr] { delete thread_ptr; });
        Thread::SleepMs(10000);
      },
      "IsCurrent");
}
#endif

// Verifies that if thread A invokes a call on thread B and thread C is trying
// to invoke A at the same time, thread A does not handle C's invoke while
// invoking B.
TEST(ThreadTest, ThreeThreadsBlockingCall) {
  ScopedThread thread;
  Thread* thread_a = Thread::Current();
  std::unique_ptr<Thread> thread_b = Thread::Create();
  std::unique_ptr<Thread> thread_c = Thread::Create();
  thread_b->Start();
  thread_c->Start();

  class LockedBool {
   public:
    explicit LockedBool(bool value) : value_(value) {}

    void Set(bool value) {
      MutexLock lock(&mutex_);
      value_ = value;
    }

    bool Get() {
      MutexLock lock(&mutex_);
      return value_;
    }

   private:
    Mutex mutex_;
    bool value_ RTC_GUARDED_BY(mutex_);
  };

  struct LocalFuncs {
    static void Set(LockedBool* out) { out->Set(true); }
    static void InvokeSet(Thread* thread, LockedBool* out) {
      thread->BlockingCall([out] { Set(out); });
    }

    // Set `out` true and call InvokeSet on `thread`.
    static void SetAndInvokeSet(LockedBool* out,
                                Thread* thread,
                                LockedBool* out_inner) {
      out->Set(true);
      InvokeSet(thread, out_inner);
    }

    // Asynchronously invoke SetAndInvokeSet on `thread1` and wait until
    // `thread1` starts the call.
    static void AsyncInvokeSetAndWait(Thread* thread1,
                                      Thread* thread2,
                                      LockedBool* out) {
      LockedBool async_invoked(false);

      thread1->PostTask([&async_invoked, thread2, out] {
        SetAndInvokeSet(&async_invoked, thread2, out);
      });

      EXPECT_TRUE(WaitUntil([&] { return async_invoked.Get(); }));
    }
  };

  LockedBool thread_a_called(false);

  // Start the sequence A --(invoke)--> B --(async invoke)--> C --(invoke)--> A.
  // Thread B returns when C receives the call and C should be blocked until A
  // starts to process messages.
  Thread* thread_c_ptr = thread_c.get();
  thread_b->BlockingCall([thread_c_ptr, thread_a, &thread_a_called] {
    LocalFuncs::AsyncInvokeSetAndWait(thread_c_ptr, thread_a, &thread_a_called);
  });
  EXPECT_FALSE(thread_a_called.Get());

  EXPECT_TRUE(WaitUntil([&] { return thread_a_called.Get(); }));
}

void DelayedPostsWithIdenticalTimesAreProcessedInFifoOrder(FakeClock& clock,
                                                           Thread& q) {
  std::vector<int> run_order;

  Event done;
  int64_t now = TimeMillis();
  q.PostDelayedTask([&] { run_order.push_back(3); }, TimeDelta::Millis(3));
  q.PostDelayedTask([&] { run_order.push_back(0); }, TimeDelta::Millis(1));
  q.PostDelayedTask([&] { run_order.push_back(1); }, TimeDelta::Millis(2));
  q.PostDelayedTask([&] { run_order.push_back(4); }, TimeDelta::Millis(3));
  q.PostDelayedTask([&] { run_order.push_back(2); }, TimeDelta::Millis(2));
  q.PostDelayedTask([&] { done.Set(); }, TimeDelta::Millis(4));
  // Validate time was frozen while tasks were posted.
  RTC_DCHECK_EQ(TimeMillis(), now);

  // Change time to make all tasks ready to run and wait for them.
  clock.AdvanceTime(TimeDelta::Millis(4));
  ASSERT_TRUE(done.Wait(TimeDelta::Seconds(1)));

  EXPECT_THAT(run_order, ElementsAre(0, 1, 2, 3, 4));
}

TEST(ThreadTest, DelayedPostsWithIdenticalTimesAreProcessedInFifoOrder) {
  ScopedFakeClock clock;
  Thread q(CreateDefaultSocketServer(), true);
  q.Start();
  DelayedPostsWithIdenticalTimesAreProcessedInFifoOrder(clock, q);

  NullSocketServer nullss;
  Thread q_nullss(&nullss, true);
  q_nullss.Start();
  DelayedPostsWithIdenticalTimesAreProcessedInFifoOrder(clock, q_nullss);
}

// Ensure that ProcessAllMessageQueues does its essential function; process
// all messages (both delayed and non delayed) up until the current time, on
// all registered message queues.
TEST(ThreadManager, ProcessAllMessageQueues) {
  ScopedThread main_thread;
  Event entered_process_all_message_queues(true, false);
  std::unique_ptr<Thread> a = Thread::Create();
  std::unique_ptr<Thread> b = Thread::Create();
  a->Start();
  b->Start();

  std::atomic<int> messages_processed(0);
  auto incrementer = [&messages_processed,
                      &entered_process_all_message_queues] {
    // Wait for event as a means to ensure Increment doesn't occur outside
    // of ProcessAllMessageQueues. The event is set by a message posted to
    // the main thread, which is guaranteed to be handled inside
    // ProcessAllMessageQueues.
    entered_process_all_message_queues.Wait(Event::kForever);
    messages_processed.fetch_add(1);
  };
  auto event_signaler = [&entered_process_all_message_queues] {
    entered_process_all_message_queues.Set();
  };

  // Post messages (both delayed and non delayed) to both threads.
  a->PostTask(incrementer);
  b->PostTask(incrementer);
  a->PostDelayedTask(incrementer, TimeDelta::Zero());
  b->PostDelayedTask(incrementer, TimeDelta::Zero());
  main_thread.PostTask(event_signaler);

  ThreadManager::ProcessAllMessageQueuesForTesting();
  EXPECT_EQ(messages_processed.load(std::memory_order_acquire), 4);
}

// Test that ProcessAllMessageQueues doesn't hang if a thread is quitting.
TEST(ThreadManager, ProcessAllMessageQueuesWithQuittingThread) {
  std::unique_ptr<Thread> t = Thread::Create();
  t->Start();
  t->Quit();
  ThreadManager::ProcessAllMessageQueuesForTesting();
}

void WaitAndSetEvent(Event* wait_event, Event* set_event) {
  wait_event->Wait(Event::kForever);
  set_event->Set();
}

// A functor that keeps track of the number of copies and moves.
class LifeCycleFunctor {
 public:
  struct Stats {
    size_t copy_count = 0;
    size_t move_count = 0;
  };

  LifeCycleFunctor(Stats* stats, Event* event) : stats_(stats), event_(event) {}
  LifeCycleFunctor(const LifeCycleFunctor& other) { *this = other; }
  LifeCycleFunctor(LifeCycleFunctor&& other) { *this = std::move(other); }

  LifeCycleFunctor& operator=(const LifeCycleFunctor& other) {
    stats_ = other.stats_;
    event_ = other.event_;
    ++stats_->copy_count;
    return *this;
  }

  LifeCycleFunctor& operator=(LifeCycleFunctor&& other) {
    stats_ = other.stats_;
    event_ = other.event_;
    ++stats_->move_count;
    return *this;
  }

  void operator()() { event_->Set(); }

 private:
  Stats* stats_;
  Event* event_;
};

// A functor that verifies the thread it was destroyed on.
class DestructionFunctor {
 public:
  DestructionFunctor(Thread* thread, bool* thread_was_current, Event* event)
      : thread_(thread),
        thread_was_current_(thread_was_current),
        event_(event) {}
  ~DestructionFunctor() {
    // Only signal the event if this was the functor that was invoked to avoid
    // the event being signaled due to the destruction of temporary/moved
    // versions of this object.
    if (was_invoked_) {
      *thread_was_current_ = thread_->IsCurrent();
      event_->Set();
    }
  }

  void operator()() { was_invoked_ = true; }

 private:
  Thread* thread_;
  bool* thread_was_current_;
  Event* event_;
  bool was_invoked_ = false;
};

TEST(ThreadPostTaskTest, InvokesWithLambda) {
  std::unique_ptr<Thread> background_thread(Thread::Create());
  background_thread->Start();

  Event event;
  background_thread->PostTask([&event] { event.Set(); });
  event.Wait(Event::kForever);
}

TEST(ThreadPostTaskTest, InvokesWithCopiedFunctor) {
  std::unique_ptr<Thread> background_thread(Thread::Create());
  background_thread->Start();

  LifeCycleFunctor::Stats stats;
  Event event;
  LifeCycleFunctor functor(&stats, &event);
  background_thread->PostTask(functor);
  event.Wait(Event::kForever);

  EXPECT_EQ(stats.copy_count, 1u);
  EXPECT_EQ(stats.move_count, 0u);
}

TEST(ThreadPostTaskTest, InvokesWithMovedFunctor) {
  std::unique_ptr<Thread> background_thread(Thread::Create());
  background_thread->Start();

  LifeCycleFunctor::Stats stats;
  Event event;
  LifeCycleFunctor functor(&stats, &event);
  background_thread->PostTask(std::move(functor));
  event.Wait(Event::kForever);

  EXPECT_EQ(stats.copy_count, 0u);
  EXPECT_EQ(stats.move_count, 1u);
}

TEST(ThreadPostTaskTest, InvokesWithReferencedFunctorShouldCopy) {
  std::unique_ptr<Thread> background_thread(Thread::Create());
  background_thread->Start();

  LifeCycleFunctor::Stats stats;
  Event event;
  LifeCycleFunctor functor(&stats, &event);
  LifeCycleFunctor& functor_ref = functor;
  background_thread->PostTask(functor_ref);
  event.Wait(Event::kForever);

  EXPECT_EQ(stats.copy_count, 1u);
  EXPECT_EQ(stats.move_count, 0u);
}

TEST(ThreadPostTaskTest, InvokesWithCopiedFunctorDestroyedOnTargetThread) {
  std::unique_ptr<Thread> background_thread(Thread::Create());
  background_thread->Start();

  Event event;
  bool was_invoked_on_background_thread = false;
  DestructionFunctor functor(background_thread.get(),
                             &was_invoked_on_background_thread, &event);
  background_thread->PostTask(functor);
  event.Wait(Event::kForever);

  EXPECT_TRUE(was_invoked_on_background_thread);
}

TEST(ThreadPostTaskTest, InvokesWithMovedFunctorDestroyedOnTargetThread) {
  std::unique_ptr<Thread> background_thread(Thread::Create());
  background_thread->Start();

  Event event;
  bool was_invoked_on_background_thread = false;
  DestructionFunctor functor(background_thread.get(),
                             &was_invoked_on_background_thread, &event);
  background_thread->PostTask(std::move(functor));
  event.Wait(Event::kForever);

  EXPECT_TRUE(was_invoked_on_background_thread);
}

TEST(ThreadPostTaskTest,
     InvokesWithReferencedFunctorShouldCopyAndDestroyedOnTargetThread) {
  std::unique_ptr<Thread> background_thread(Thread::Create());
  background_thread->Start();

  Event event;
  bool was_invoked_on_background_thread = false;
  DestructionFunctor functor(background_thread.get(),
                             &was_invoked_on_background_thread, &event);
  DestructionFunctor& functor_ref = functor;
  background_thread->PostTask(functor_ref);
  event.Wait(Event::kForever);

  EXPECT_TRUE(was_invoked_on_background_thread);
}

TEST(ThreadPostTaskTest, InvokesOnBackgroundThread) {
  std::unique_ptr<Thread> background_thread(Thread::Create());
  background_thread->Start();

  Event event;
  bool was_invoked_on_background_thread = false;
  Thread* background_thread_ptr = background_thread.get();
  background_thread->PostTask(
      [background_thread_ptr, &was_invoked_on_background_thread, &event] {
        was_invoked_on_background_thread = background_thread_ptr->IsCurrent();
        event.Set();
      });
  event.Wait(Event::kForever);

  EXPECT_TRUE(was_invoked_on_background_thread);
}

TEST(ThreadPostTaskTest, InvokesAsynchronously) {
  std::unique_ptr<Thread> background_thread(Thread::Create());
  background_thread->Start();

  // The first event ensures that SendSingleMessage() is not blocking this
  // thread. The second event ensures that the message is processed.
  Event event_set_by_test_thread;
  Event event_set_by_background_thread;
  background_thread->PostTask([&event_set_by_test_thread,
                               &event_set_by_background_thread] {
    WaitAndSetEvent(&event_set_by_test_thread, &event_set_by_background_thread);
  });
  event_set_by_test_thread.Set();
  event_set_by_background_thread.Wait(Event::kForever);
}

TEST(ThreadPostTaskTest, InvokesInPostedOrder) {
  std::unique_ptr<Thread> background_thread(Thread::Create());
  background_thread->Start();

  Event first;
  Event second;
  Event third;
  Event fourth;

  background_thread->PostTask(
      [&first, &second] { WaitAndSetEvent(&first, &second); });
  background_thread->PostTask(
      [&second, &third] { WaitAndSetEvent(&second, &third); });
  background_thread->PostTask(
      [&third, &fourth] { WaitAndSetEvent(&third, &fourth); });

  // All tasks have been posted before the first one is unblocked.
  first.Set();
  // Only if the chain is invoked in posted order will the last event be set.
  fourth.Wait(Event::kForever);
}

TEST(ThreadPostDelayedTaskTest, InvokesAsynchronously) {
  std::unique_ptr<Thread> background_thread(Thread::Create());
  background_thread->Start();

  // The first event ensures that SendSingleMessage() is not blocking this
  // thread. The second event ensures that the message is processed.
  Event event_set_by_test_thread;
  Event event_set_by_background_thread;
  background_thread->PostDelayedTask(
      [&event_set_by_test_thread, &event_set_by_background_thread] {
        WaitAndSetEvent(&event_set_by_test_thread,
                        &event_set_by_background_thread);
      },
      TimeDelta::Millis(10));
  event_set_by_test_thread.Set();
  event_set_by_background_thread.Wait(Event::kForever);
}

TEST(ThreadPostDelayedTaskTest, InvokesInDelayOrder) {
  ScopedFakeClock clock;
  std::unique_ptr<Thread> background_thread = Thread::Create();
  background_thread->Start();

  Event first;
  Event second;
  Event third;
  Event fourth;

  background_thread->PostDelayedTask(
      [&third, &fourth] { WaitAndSetEvent(&third, &fourth); },
      TimeDelta::Millis(11));
  background_thread->PostDelayedTask(
      [&first, &second] { WaitAndSetEvent(&first, &second); },
      TimeDelta::Millis(9));
  background_thread->PostDelayedTask(
      [&second, &third] { WaitAndSetEvent(&second, &third); },
      TimeDelta::Millis(10));

  // All tasks have been posted before the first one is unblocked.
  first.Set();
  // Only if the chain is invoked in delay order will the last event be set.
  clock.AdvanceTime(TimeDelta::Millis(11));
  ThreadManager::ProcessAllMessageQueuesForTesting();
  EXPECT_TRUE(fourth.Wait(TimeDelta::Zero()));
}

TEST(ThreadPostDelayedTaskTest, IsCurrentTaskQueue) {
  TaskQueueBase* current_tq = TaskQueueBase::Current();
  {
    std::unique_ptr<Thread> thread = Thread::Create();
    thread->WrapCurrent();
    EXPECT_EQ(TaskQueueBase::Current(), thread.get());
    thread->UnwrapCurrent();
  }
  EXPECT_EQ(TaskQueueBase::Current(), current_tq);
}

// Uses `HasPendingTasks()` to detect when to yield to another posted task.
TEST(ThreadCooperativeTest, TaskTriggersHasPendingTasks) {
  test::RunLoop loop;
  std::unique_ptr<Thread> thread = Thread::Create();
  thread->Start();

  bool was_interrupted = false;
  Event task_started;

  // Post a long running task that checks for pending tasks.
  thread->PostTask(
      [&was_interrupted, &loop, &task_started, thread = thread.get()] {
        task_started.Set();
        while (!thread->HasPendingTasks()) {
          // Busy loop/simulated work
        }
        loop.PostTask([&was_interrupted, &loop] {
          was_interrupted = true;
          loop.Quit();
        });
      });

  // Wait for the task to start to ensure that the task doesn't
  // run first.
  task_started.Wait(Event::kForever);

  // Post a task that interrupts the busy loop.
  thread->PostTask([] {});

  loop.Run();
  EXPECT_TRUE(was_interrupted);
}

TEST(ThreadCooperativeTest, HasPendingTasksClearedAfterTask) {
  std::unique_ptr<Thread> thread(Thread::Create());
  thread->Start();

  // Initially false.
  thread->BlockingCall([&] { EXPECT_FALSE(thread->HasPendingTasks()); });

  // Post task.
  thread->PostTask([&] {});

  // Use `BlockingCall` to post normal task which implicitly blocks and waits
  // for its functor to run, at which point the queue will be empty again.
  thread->BlockingCall([&] { EXPECT_FALSE(thread->HasPendingTasks()); });
}

class ThreadFactory : public TaskQueueFactory {
 public:
  std::unique_ptr<TaskQueueBase, TaskQueueDeleter> CreateTaskQueue(
      absl::string_view /* name */,
      Priority /*priority*/) const override {
    std::unique_ptr<Thread> thread = Thread::Create();
    thread->Start();
    return std::unique_ptr<TaskQueueBase, TaskQueueDeleter>(thread.release());
  }
};

std::unique_ptr<TaskQueueFactory> CreateDefaultThreadFactory(
    const FieldTrialsView*) {
  return std::make_unique<ThreadFactory>();
}

INSTANTIATE_TEST_SUITE_P(RtcThread,
                         TaskQueueTest,
                         ::testing::Values(CreateDefaultThreadFactory));

}  // namespace
}  // namespace webrtc
