/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GMPService.h"
#include "GMPServiceParent.h"
#include "GMPUtils.h"
#include "gtest/gtest.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Monitor.h"
#include "mozilla/MozPromise.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/gtest/MozHelpers.h"
#include "mozilla/gtest/WaitFor.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"

#ifdef XP_MACOSX
#  include "gfxPlatformMac.h"
#endif

// Each test below that is not a death test must be alone in its suite, because
// each has to be the first thing in its process to touch the GMP service. Each
// suite gets its own process, see testing/gtest/suites.py, except under
// --combine-suites (tsan, Android) where all but the first are vacuous rather
// than wrong.
//
// What they guard against is the GMP service being created off the main thread
// waiting for the main thread, while the main thread waits for that creation
// to finish. See bug 2081210.
//
// The tests that advance the shutdown phase run in a death test child process,
// as shutdown cannot be undone. They need the threadsafe death test style, as
// the child must not be a fork of this multithreaded process: it would have
// none of the threads the thread pools think they have.

namespace mozilla::gmp {

namespace {

constexpr char kContract[] = "@mozilla.org/gecko-media-plugin-service;1";

// A deadlock must fail the test, not hang it.
TimeDuration DeadlockTimeout() { return TimeDuration::FromSeconds(30); }

// Blocks without processing events. A one-shot latch.
class Gate {
 public:
  void Open() {
    MonitorAutoLock lock(mMonitor);
    mOpen = true;
    lock.NotifyAll();
  }

  // False on timeout.
  bool WaitOpen() {
    MonitorAutoLock lock(mMonitor);
    const TimeStamp deadline = TimeStamp::Now() + DeadlockTimeout();
    while (!mOpen) {
      const TimeStamp now = TimeStamp::Now();
      if (now >= deadline) {
        return false;
      }
      lock.Wait(deadline - now);
    }
    return true;
  }

 private:
  Monitor mMonitor{"GMPServiceCreationTest::Gate"};
  bool mOpen = false;
};

// Shared with threads that can outlive a test that failed on a deadlock.
struct SharedState final {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(SharedState)

  Gate mCreated;
  Gate mStart;
  static constexpr size_t kThreads = 8;
  Atomic<void*> mResults[kThreads];

 private:
  ~SharedState() = default;
};

already_AddRefed<nsISerialEventTarget> MakeQueue() {
  nsCOMPtr<nsISerialEventTarget> queue;
  MOZ_ALWAYS_SUCCEEDS(
      NS_CreateBackgroundTaskQueue(__func__, getter_AddRefs(queue)));
  return queue.forget();
}

// The service the way libxul code gets it, through the component manager. This
// is what waits for a creation that is pending on another thread.
GeckoMediaPluginService* GetServiceViaXPCOM() {
  nsCOMPtr<mozIGeckoMediaPluginService> mps = do_GetService(kContract);
  // The component manager keeps the service alive.
  return static_cast<GeckoMediaPluginService*>(mps.get());
}

// The storage directory is set by the main thread setup, so it tells whether
// that has run.
bool MainThreadInitDone(GeckoMediaPluginService* aService) {
  nsCOMPtr<nsIFile> dir;
  return NS_SUCCEEDED(
      static_cast<GeckoMediaPluginServiceParent*>(aService)->GetStorageDir(
          getter_AddRefs(dir)));
}

void SpinUntilMainThreadInit(GeckoMediaPluginService* aService) {
  SpinEventLoopUntil("GMPServiceCreation::SpinUntilMainThreadInit"_ns,
                     [&] { return MainThreadInitDone(aService); });
}

}  // namespace

TEST(GMPServiceCreation, OffMainFirstCallerWithMainBlocked)
{
  auto state = MakeRefPtr<SharedState>();
  nsCOMPtr<nsISerialEventTarget> queue = MakeQueue();
  MOZ_ALWAYS_SUCCEEDS(queue->Dispatch(NS_NewRunnableFunction(__func__, [state] {
    state->mResults[0] = GetServiceViaXPCOM();
    state->mCreated.Open();
  })));

  // Not processing events here, as in the bug. The creating thread must not
  // need the main thread to be able to return.
  ASSERT_TRUE(state->mCreated.WaitOpen())
  << "The GMP service creation is waiting for the main thread";

  GeckoMediaPluginService* service = GetServiceViaXPCOM();
  ASSERT_TRUE(service);
  EXPECT_EQ(static_cast<void*>(state->mResults[0]),
            static_cast<void*>(service));

  SpinUntilMainThreadInit(service);
}

TEST(GMPServiceCreationGMPThread, AvailableToOffMainFirstCaller)
{
  auto state = MakeRefPtr<SharedState>();
  nsCOMPtr<nsISerialEventTarget> queue = MakeQueue();
  MOZ_ALWAYS_SUCCEEDS(queue->Dispatch(NS_NewRunnableFunction(__func__, [state] {
    RefPtr<GeckoMediaPluginService> service =
        GeckoMediaPluginService::GetGeckoMediaPluginService();
    // The main thread is blocked, so its setup has not run. Callers such as
    // GMPEncoderModule::CreateVideoEncoder() still expect the thread to exist.
    EXPECT_TRUE(nsCOMPtr<nsISerialEventTarget>(service->GetGMPThread()));
    state->mCreated.Open();
  })));
  ASSERT_TRUE(state->mCreated.WaitOpen())
  << "The GMP service creation is waiting for the main thread";
}

TEST(GMPServiceCreationMainAfterOffMain, RunsPendingMainThreadInitInline)
{
  auto state = MakeRefPtr<SharedState>();
  nsCOMPtr<nsISerialEventTarget> queue = MakeQueue();
  MOZ_ALWAYS_SUCCEEDS(queue->Dispatch(NS_NewRunnableFunction(__func__, [state] {
    state->mResults[0] = GetServiceViaXPCOM();
    state->mCreated.Open();
  })));
  ASSERT_TRUE(state->mCreated.WaitOpen())
  << "The GMP service creation is waiting for the main thread";

  // The task that sets the service up on the main thread is queued behind us.
  // The main thread must not have to wait for it to get a usable service.
  RefPtr<GeckoMediaPluginService> service =
      GeckoMediaPluginService::GetGeckoMediaPluginService();
  ASSERT_TRUE(service);
  EXPECT_EQ(static_cast<void*>(state->mResults[0]),
            static_cast<void*>(service.get()));
  EXPECT_TRUE(MainThreadInitDone(service));
}

TEST(GMPServiceCreationOnMain, RunsMainThreadInitInline)
{
  RefPtr<GeckoMediaPluginService> service =
      GeckoMediaPluginService::GetGeckoMediaPluginService();
  ASSERT_TRUE(service);
  EXPECT_TRUE(MainThreadInitDone(service));
}

TEST(GMPServiceCreationManyThreads, ConcurrentFirstCalls)
{
  auto state = MakeRefPtr<SharedState>();

  nsTArray<nsCOMPtr<nsISerialEventTarget>> queues;
  nsTArray<RefPtr<GenericPromise>> done;
  for (size_t i = 0; i < SharedState::kThreads; i++) {
    queues.AppendElement(MakeQueue());
    done.AppendElement(InvokeAsync(queues.LastElement(), __func__, [state, i] {
      (void)state->mStart.WaitOpen();
      GeckoMediaPluginService* service = GetServiceViaXPCOM();
      state->mResults[i] = service;
      // Whichever thread created the service, it must already have its GMP
      // thread when it is handed out.
      EXPECT_TRUE(nsCOMPtr<nsISerialEventTarget>(service->GetGMPThread())) << i;
      return GenericPromise::CreateAndResolve(true, __func__);
    }));
  }

  state->mStart.Open();
  GeckoMediaPluginService* service = GetServiceViaXPCOM();
  ASSERT_TRUE(service);

  (void)WaitFor(GenericPromise::All(GetCurrentSerialEventTarget(), done));
  for (size_t i = 0; i < SharedState::kThreads; i++) {
    EXPECT_EQ(static_cast<void*>(state->mResults[i]),
              static_cast<void*>(service))
        << i;
  }

  SpinUntilMainThreadInit(service);
}

// Spawning the death test child process aborts on Android.
#if !defined(ANDROID) && defined(GTEST_HAS_DEATH_TEST)

void ShutdownXPCOM() {
  AppShutdown::AdvanceShutdownPhase(ShutdownPhase::AppShutdownNetTeardown);
  AppShutdown::AdvanceShutdownPhase(ShutdownPhase::AppShutdownTeardown);
  AppShutdown::AdvanceShutdownPhase(ShutdownPhase::AppShutdown);
  AppShutdown::AdvanceShutdownPhase(ShutdownPhase::AppShutdownQM);
  AppShutdown::AdvanceShutdownPhase(ShutdownPhase::AppShutdownTelemetry);

  NS_ShutdownXPCOM(nullptr);

#  ifdef XP_MACOSX
  gfxPlatformMac::WaitForFontRegistration();
#  endif
}

void DoMainThreadInitSkippedWhenShutdownStartsAfterPublish() {
  auto reporter =
      gtest::ScopedTestResultReporter::Create(gtest::ExitMode::ExitOnDtor);

  nsCOMPtr<nsISerialEventTarget> queue = MakeQueue();
  auto state = MakeRefPtr<SharedState>();
  MOZ_ALWAYS_SUCCEEDS(queue->Dispatch(NS_NewRunnableFunction(__func__, [state] {
    state->mResults[0] = GetServiceViaXPCOM();
    state->mCreated.Open();
  })));
  ASSERT_TRUE(state->mCreated.WaitOpen())
  << "The GMP service creation is waiting for the main thread";

  // The service is published, and the task that sets it up on the main thread
  // is queued. Entering AppShutdownConfirmed does not process events, so
  // shutdown gets in before the task runs.
  AppShutdown::AdvanceShutdownPhase(ShutdownPhase::AppShutdownConfirmed);

  // The component manager hands out what the factory returned, so what a
  // caller racing shutdown gets is an inert service, not null.
  GeckoMediaPluginService* service = GetServiceViaXPCOM();
  ASSERT_TRUE(service);
  EXPECT_EQ(static_cast<void*>(state->mResults[0]),
            static_cast<void*>(service));

  // Nobody can be left waiting for a scan that will never start. This settles
  // once the queued task has run and found out that shutdown has started.
  auto* parent = static_cast<GeckoMediaPluginServiceParent*>(service);
  (void)WaitFor(parent->EnsureInitialized());

  EXPECT_FALSE(MainThreadInitDone(service));
  EXPECT_FALSE(HaveGMPFor("decode-video"_ns, {"h264"_ns}));
  EXPECT_FALSE(HaveGMPFor("encode-video"_ns, {"h264"_ns}));

  nsCOMPtr<nsIThread> gmpThread;
  EXPECT_TRUE(NS_FAILED(service->GetThread(getter_AddRefs(gmpThread))));
  EXPECT_FALSE(gmpThread);

  // Shutting down with the inert service still held by the component manager
  // must not assert or leak.
  ShutdownXPCOM();
}

void DoCreationAfterShutdownReturnsNull() {
  auto reporter =
      gtest::ScopedTestResultReporter::Create(gtest::ExitMode::ExitOnDtor);

  AppShutdown::AdvanceShutdownPhase(ShutdownPhase::AppShutdownConfirmed);

  RefPtr<GeckoMediaPluginService> service =
      GeckoMediaPluginService::GetGeckoMediaPluginService();
  EXPECT_FALSE(service);

  nsCOMPtr<nsISerialEventTarget> queue = MakeQueue();
  auto state = MakeRefPtr<SharedState>();
  MOZ_ALWAYS_SUCCEEDS(queue->Dispatch(NS_NewRunnableFunction(__func__, [state] {
    RefPtr<GeckoMediaPluginService> offMain =
        GeckoMediaPluginService::GetGeckoMediaPluginService();
    state->mResults[0] = offMain.get();
    state->mCreated.Open();
  })));
  ASSERT_TRUE(state->mCreated.WaitOpen())
  << "Getting the GMP service after shutdown is waiting for the main thread";
  EXPECT_FALSE(static_cast<void*>(state->mResults[0]));

  ShutdownXPCOM();
}

TEST(GMPServiceCreationRacesShutdownDeathTest,
     MainThreadInitSkippedWhenShutdownStartsAfterPublish)
{
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT(DoMainThreadInitSkippedWhenShutdownStartsAfterPublish(),
              testing::ExitedWithCode(0), "");
}

TEST(GMPServiceCreationAfterShutdownDeathTest,
     ReturnsNullWithoutWaitingForMainThread)
{
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  EXPECT_EXIT(DoCreationAfterShutdownReturnsNull(), testing::ExitedWithCode(0),
              "");
}

#endif

}  // namespace mozilla::gmp
