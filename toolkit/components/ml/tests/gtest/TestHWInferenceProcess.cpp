/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/Preferences.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/gtest/WaitFor.h"
#include "mozilla/gtest/ipc/TestUtilityProcess.h"
#include "mozilla/hwinference/HWInferenceParent.h"
#include "mozilla/hwinference/HWInferenceProcess.h"
#include "mozilla/ipc/UtilityProcessManager.h"
#include "nsIProcessToolsService.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"

using namespace mozilla;
using namespace mozilla::hwinference;

namespace {

using ipc::UtilityProcessKeepAlive;

constexpr char kMaxRestartsPref[] = "browser.ml.hwinference.max_restarts";
constexpr char kStartupTimeoutPref[] =
    "dom.ipc.utilityProcess.startup_timeout_ms";
constexpr char kSkipInitCompletedPref[] =
    "dom.ipc.utilityProcess.testing.skipInitCompleted";

// Generous for debug CI.
constexpr uint32_t kStepTimeoutSeconds = 120;

template <typename Condition>
bool SpinUntil(const char* aName, Condition&& aCondition) {
  TimeStamp deadline =
      TimeStamp::Now() + TimeDuration::FromSeconds(kStepTimeoutSeconds);
  bool held = false;
  SpinEventLoopUntil<ProcessFailureBehavior::IgnoreAndContinue>(
      nsDependentCString(aName), [&] {
        held = aCondition();
        return held || TimeStamp::Now() > deadline;
      });
  return held;
}

size_t AliveProcesses() {
  auto manager = ipc::UtilityProcessManager::GetIfExists();
  return manager ? manager->AliveProcesses() : 0;
}

// Err if the process never came up.
Result<bool, nsresult> WaitForReady(HWInferenceProcess& aProcess) {
  RefPtr<HWInferenceParent> actor = aProcess.Actor();
  if (!actor) {
    return Err(NS_ERROR_NOT_AVAILABLE);
  }
  return WaitFor(actor->WhenReady());
}

Maybe<base::ProcessId> PidOf(UtilityProcessKeepAlive* aKeepAlive) {
  RefPtr<ipc::UtilityProcessParent> parent = aKeepAlive->GetProcessParent();
  return parent ? Some(parent->OtherPid()) : Nothing();
}

bool Kill(const Maybe<base::ProcessId>& aPid) {
  nsCOMPtr<nsIProcessToolsService> tools =
      do_GetService("@mozilla.org/processtools-service;1");
  return aPid.isSome() && tools && NS_SUCCEEDED(tools->Kill(*aPid));
}

}  // namespace

// A fresh HWInferenceProcess per case, so no restart budget carries over.
class HWInferenceProcessTest : public mozilla::gtest::ipc::TestUtilityProcess {
 protected:
  void SetUp() override { Preferences::SetUint(kMaxRestartsPref, 3); }
  void TearDown() override {
    Preferences::ClearUser(kMaxRestartsPref);
    Preferences::ClearUser(kStartupTimeoutPref);
    Preferences::ClearUser(kSkipInitCompletedPref);
  }

  bool WaitForExit() {
    return SpinUntil("Process exit", [&] { return !mProcess.IsUp(); });
  }

  HWInferenceProcess mProcess;
};

class BrowserHWInferenceProcessTest : public HWInferenceProcessTest {
 protected:
  bool WaitForBrowserExit() {
    return SpinUntil("Browser process exit", [&] { return !mBrowser.IsUp(); });
  }

  HWInferenceProcess mBrowser;
};

// One process for all consumers, gone with the last keep-alive.
TEST_F(HWInferenceProcessTest, SharesOneProcess) {
  EXPECT_FALSE(mProcess.IsUp());
  EXPECT_FALSE(mProcess.Actor());

  RefPtr<UtilityProcessKeepAlive> first = mProcess.Acquire();
  RefPtr<UtilityProcessKeepAlive> second = mProcess.Acquire();
  ASSERT_TRUE(first);
  ASSERT_EQ(first.get(), second.get());
  RefPtr<HWInferenceParent> actor = mProcess.Actor();
  ASSERT_TRUE(actor);

  ASSERT_TRUE(WaitForReady(mProcess).isOk());
  EXPECT_TRUE(mProcess.IsUp());
  EXPECT_EQ(mProcess.Actor().get(), actor.get());
  EXPECT_TRUE(actor->CanSend());

  RefPtr<UtilityProcessKeepAlive> third = mProcess.Acquire();
  EXPECT_EQ(first.get(), third.get());
  EXPECT_EQ(AliveProcesses(), 1u);

  first = nullptr;
  second = nullptr;
  EXPECT_TRUE(third->IsAlive());

  third = nullptr;
  EXPECT_FALSE(mProcess.IsUp());
  EXPECT_EQ(AliveProcesses(), 0u);

  // ActorDestroy runs once the shutdown handshake completes.
  EXPECT_TRUE(SpinUntil("ActorDestroy", [&] { return !actor->CanSend(); }));
  EXPECT_FALSE(mProcess.Actor());

  NS_ProcessPendingEvents(nullptr);
}

// An acquire right after the last release gets a fresh process and actor.
TEST_F(HWInferenceProcessTest, RelaunchesAfterShutdown) {
  RefPtr<UtilityProcessKeepAlive> keepAlive = mProcess.Acquire();
  ASSERT_TRUE(keepAlive);
  ASSERT_TRUE(WaitForReady(mProcess).isOk());
  RefPtr<HWInferenceParent> first = mProcess.Actor();
  Maybe<base::ProcessId> pid = PidOf(keepAlive);
  ASSERT_TRUE(pid.isSome());

  // Same turn: the old actor has not seen ActorDestroy yet.
  keepAlive = nullptr;
  ASSERT_TRUE(first->CanSend());
  keepAlive = mProcess.Acquire();
  ASSERT_TRUE(keepAlive);

  RefPtr<HWInferenceParent> second = mProcess.Actor();
  ASSERT_TRUE(second);
  EXPECT_NE(first.get(), second.get());
  ASSERT_TRUE(WaitForReady(mProcess).isOk());
  EXPECT_NE(PidOf(keepAlive), pid);
  EXPECT_TRUE(second->CanSend());

  // The old actor's ActorDestroy does not touch the new one.
  EXPECT_TRUE(
      SpinUntil("Stale ActorDestroy", [&] { return !first->CanSend(); }));
  EXPECT_EQ(mProcess.Actor().get(), second.get());

  keepAlive = nullptr;
  EXPECT_TRUE(WaitForExit());

  NS_ProcessPendingEvents(nullptr);
}

TEST_F(HWInferenceProcessTest, ProcessDeathLaunchesAfresh) {
  RefPtr<UtilityProcessKeepAlive> keepAlive = mProcess.Acquire();
  ASSERT_TRUE(keepAlive);
  ASSERT_TRUE(WaitForReady(mProcess).isOk());
  RefPtr<HWInferenceParent> actor = mProcess.Actor();
  Maybe<base::ProcessId> pid = PidOf(keepAlive);

  ASSERT_TRUE(Kill(pid));
  ASSERT_TRUE(SpinUntil("Crash noticed", [&] { return !mProcess.IsUp(); }));
  EXPECT_FALSE(keepAlive->IsAlive());
  // The actor died before the keep-alive.
  EXPECT_FALSE(actor->CanSend());
  EXPECT_FALSE(mProcess.Actor());

  // Relaunch while the dead keep-alive is still held.
  RefPtr<UtilityProcessKeepAlive> second = mProcess.Acquire();
  ASSERT_TRUE(second);
  EXPECT_NE(second.get(), keepAlive.get());
  ASSERT_TRUE(WaitForReady(mProcess).isOk());
  Maybe<base::ProcessId> newPid = PidOf(second);
  ASSERT_TRUE(newPid.isSome());
  EXPECT_NE(newPid, pid);

  // Dropping the dead keep-alive does not touch the new process.
  keepAlive = nullptr;
  EXPECT_EQ(PidOf(second), newPid);

  second = nullptr;
  EXPECT_TRUE(WaitForExit());

  NS_ProcessPendingEvents(nullptr);
}

// Past the budget: no relaunch, no pending actor.
TEST_F(HWInferenceProcessTest, RestartBudgetRefusesRelaunch) {
  Preferences::SetUint(kMaxRestartsPref, 1);

  RefPtr<UtilityProcessKeepAlive> keepAlive = mProcess.Acquire();
  ASSERT_TRUE(keepAlive);
  ASSERT_TRUE(WaitForReady(mProcess).isOk());
  ASSERT_TRUE(Kill(PidOf(keepAlive)));
  ASSERT_TRUE(SpinUntil("Crash noticed", [&] { return !mProcess.IsUp(); }));

  RefPtr<UtilityProcessKeepAlive> refused = mProcess.Acquire();
  EXPECT_FALSE(refused);
  EXPECT_FALSE(mProcess.Actor());
  EXPECT_EQ(AliveProcesses(), 0u);

  NS_ProcessPendingEvents(nullptr);
}

// A launch every consumer let go of was cancelled, so it does not spend the
// budget.
TEST_F(HWInferenceProcessTest, CancelledLaunchKeepsBudget) {
  Preferences::SetUint(kMaxRestartsPref, 1);

  RefPtr<UtilityProcessKeepAlive> keepAlive = mProcess.Acquire();
  ASSERT_TRUE(keepAlive);
  RefPtr<HWInferenceParent> cancelled = mProcess.Actor();
  ASSERT_TRUE(cancelled);

  keepAlive = nullptr;
  EXPECT_TRUE(WaitFor(cancelled->WhenReady()).isErr());
  EXPECT_FALSE(mProcess.Actor()) << "the launch failure was not reported";

  keepAlive = mProcess.Acquire();
  ASSERT_TRUE(keepAlive)
  << "the cancelled launch spent the budget";
  ASSERT_TRUE(WaitForReady(mProcess).isOk());

  keepAlive = nullptr;
  EXPECT_TRUE(WaitForExit());

  NS_ProcessPendingEvents(nullptr);
}

// A failed launch spends the budget even when the next acquire comes before
// the actor hears about the failure.
TEST_F(HWInferenceProcessTest, FailedLaunchSpendsBudgetBeforeNotification) {
  Preferences::SetUint(kMaxRestartsPref, 1);
  Preferences::SetBool(kSkipInitCompletedPref, true);
  Preferences::SetUint(kStartupTimeoutPref, 1);

  RefPtr<UtilityProcessKeepAlive> keepAlive = mProcess.Acquire();
  ASSERT_TRUE(keepAlive);
  RefPtr<HWInferenceParent> failed = mProcess.Actor();
  ASSERT_TRUE(failed);

  // The launch is torn down in a single event, and its failure reaches the
  // actor only through events queued after it.
  ASSERT_TRUE(
      SpinUntil("Launch abandoned", [&] { return !keepAlive->IsAlive(); }));
  ASSERT_EQ(mProcess.Actor().get(), failed.get())
      << "the failure notification arrived before the acquire";

  RefPtr<UtilityProcessKeepAlive> refused = mProcess.Acquire();
  EXPECT_FALSE(refused) << "the failed launch did not spend the budget";
  EXPECT_FALSE(mProcess.Actor());

  EXPECT_TRUE(WaitFor(failed->WhenReady()).isErr());

  NS_ProcessPendingEvents(nullptr);
}

// A clean shutdown resets the budget, even if re-acquired in the same turn.
TEST_F(HWInferenceProcessTest, DeliberateShutdownResetsBudget) {
  Preferences::SetUint(kMaxRestartsPref, 2);

  RefPtr<UtilityProcessKeepAlive> keepAlive = mProcess.Acquire();
  ASSERT_TRUE(keepAlive);
  ASSERT_TRUE(WaitForReady(mProcess).isOk());
  ASSERT_TRUE(Kill(PidOf(keepAlive)));
  ASSERT_TRUE(SpinUntil("First crash", [&] { return !mProcess.IsUp(); }));

  keepAlive = mProcess.Acquire();
  ASSERT_TRUE(keepAlive);
  ASSERT_TRUE(WaitForReady(mProcess).isOk());
  RefPtr<HWInferenceParent> released = mProcess.Actor();

  keepAlive = nullptr;
  keepAlive = mProcess.Acquire();
  ASSERT_TRUE(keepAlive);
  ASSERT_TRUE(WaitForReady(mProcess).isOk());
  EXPECT_TRUE(
      SpinUntil("Released ActorDestroy", [&] { return !released->CanSend(); }));

  ASSERT_TRUE(Kill(PidOf(keepAlive)));
  ASSERT_TRUE(SpinUntil("Second crash", [&] { return !mProcess.IsUp(); }));

  // One crash since the reset: within the budget of two.
  keepAlive = mProcess.Acquire();
  ASSERT_TRUE(keepAlive)
  << "the deliberate shutdown did not reset the budget";
  ASSERT_TRUE(WaitForReady(mProcess).isOk());

  keepAlive = nullptr;
  EXPECT_TRUE(WaitForExit());

  NS_ProcessPendingEvents(nullptr);
}

// Browser and content processes are separate and die independently.
TEST_F(BrowserHWInferenceProcessTest, SeparateFromTheContentProcess) {
  RefPtr<UtilityProcessKeepAlive> browser = mBrowser.Acquire();
  ASSERT_TRUE(browser);
  ASSERT_TRUE(WaitForReady(mBrowser).isOk());

  RefPtr<UtilityProcessKeepAlive> content = mProcess.Acquire();
  ASSERT_TRUE(content);
  ASSERT_TRUE(WaitForReady(mProcess).isOk());

  EXPECT_NE(browser.get(), content.get());
  EXPECT_NE(mBrowser.Actor().get(), mProcess.Actor().get());
  EXPECT_NE(PidOf(browser), PidOf(content));
  EXPECT_EQ(AliveProcesses(), 2u);

  browser = nullptr;
  EXPECT_TRUE(WaitForBrowserExit());
  EXPECT_TRUE(mProcess.IsUp());
  EXPECT_EQ(AliveProcesses(), 1u);

  content = nullptr;
  EXPECT_TRUE(WaitForExit());
  EXPECT_EQ(AliveProcesses(), 0u);

  NS_ProcessPendingEvents(nullptr);
}

TEST_F(BrowserHWInferenceProcessTest, RestartBudgetsAreIndependent) {
  Preferences::SetUint(kMaxRestartsPref, 1);
  RefPtr<UtilityProcessKeepAlive> browser = mBrowser.Acquire();
  ASSERT_TRUE(browser);
  ASSERT_TRUE(WaitForReady(mBrowser).isOk());
  ASSERT_TRUE(Kill(PidOf(browser)));
  ASSERT_TRUE(WaitForBrowserExit());

  RefPtr<UtilityProcessKeepAlive> refused = mBrowser.Acquire();
  EXPECT_FALSE(refused);
  EXPECT_FALSE(mBrowser.Actor());

  RefPtr<UtilityProcessKeepAlive> content = mProcess.Acquire();
  ASSERT_TRUE(content);
  ASSERT_TRUE(WaitForReady(mProcess).isOk());
  content = nullptr;
  EXPECT_TRUE(WaitForExit());
  NS_ProcessPendingEvents(nullptr);
}
