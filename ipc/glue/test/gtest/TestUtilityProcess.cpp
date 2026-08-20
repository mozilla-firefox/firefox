/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <type_traits>

#include "gtest/gtest.h"
#include "mozilla/gtest/ipc/TestUtilityProcess.h"
#include "mozilla/gtest/WaitFor.h"
#include "nsThreadUtils.h"

#include "mozilla/ipc/UtilityProcessManager.h"

#if defined(MOZ_WIDGET_ANDROID) || defined(XP_MACOSX)
#  include "nsIAppShellService.h"
#  include "nsServiceManagerUtils.h"
#endif  // defined(MOZ_WIDGET_ANDROID) || defined(XP_MACOSX)

#if defined(XP_WIN)
#  include "mozilla/gtest/MozHelpers.h"
#  include "mozilla/ipc/UtilityProcessImpl.h"
#endif  // defined(XP_WIN)

#ifdef MOZ_WIDGET_ANDROID
#  define NS_APPSHELLSERVICE_CONTRACTID "@mozilla.org/widget/appshell/android;1"
#endif  // MOZ_WIDGET_ANDROID

#ifdef XP_MACOSX
#  define NS_APPSHELLSERVICE_CONTRACTID "@mozilla.org/widget/appshell/mac;1"
#endif  // XP_MACOSX

using namespace mozilla;
using namespace mozilla::gtest::ipc;
using namespace mozilla::ipc;

// Note that some test suites inherit TestUtilityProcess, so any change here
// will propagate there. Please ensure compatibility.
/* static */ void TestUtilityProcess::SetUpTestSuite() {
#if defined(XP_WIN) && defined(MOZ_SANDBOX)
  // Ensure only one execution even with GTEST_REPEAT>1
  static bool sOnce = false;
  if (!sOnce) {
    mozilla::SandboxBroker::GeckoDependentInitialize();
    sOnce = true;
  }
#endif  // defined(XP_WIN) && defined(MOZ_SANDBOX)

#if defined(MOZ_WIDGET_ANDROID) || defined(XP_MACOSX)
  // Ensure that the app shell service is running
  nsCOMPtr<nsIAppShellService> appShell =
      do_GetService(NS_APPSHELLSERVICE_CONTRACTID);
#endif  // defined(MOZ_WIDGET_ANDROID) || defined(XP_MACOSX)
}

TEST_F(TestUtilityProcess, LaunchAllKinds) {
  using kind_t = std::underlying_type<SandboxingKind>::type;

  auto manager = UtilityProcessManager::GetSingleton();
  ASSERT_TRUE(manager);

  auto currentPid = base::GetCurrentProcId();
  ASSERT_GE(currentPid, base::ProcessId(1));

  // Launch all kinds
  for (kind_t i = 0; i < SandboxingKind::COUNT; ++i) {
    auto kind = static_cast<SandboxingKind>(i);
    auto res = WaitFor(manager->LaunchProcess(kind));
    ASSERT_TRUE(res.isOk())
    << "First launch LaunchError: " << res.inspectErr().FunctionName() << ", "
    << res.inspectErr().ErrorCode();
  }

  // Collect process identifiers
  std::array<base::ProcessId, SandboxingKind::COUNT> pids{};
  for (kind_t i = 0; i < SandboxingKind::COUNT; ++i) {
    auto kind = static_cast<SandboxingKind>(i);
    auto utilityPid = manager->ProcessPid(kind);
    ASSERT_TRUE(utilityPid.isSome())
    << "No PID for kind " << kind;
    ASSERT_GE(*utilityPid, base::ProcessId(1));
    ASSERT_NE(*utilityPid, currentPid);

    printf_stderr("Utility process running as PID %" PRIPID "\n", *utilityPid);

    pids[i] = *utilityPid;
  }

  // Re-launching should resolve immediately with process identifiers unchanged
  for (kind_t i = 0; i < SandboxingKind::COUNT; ++i) {
    auto kind = static_cast<SandboxingKind>(i);
    auto res = WaitFor(manager->LaunchProcess(kind));
    ASSERT_TRUE(res.isOk())
    << "Second launch LaunchError: " << res.inspectErr().FunctionName() << ", "
    << res.inspectErr().ErrorCode();

    ASSERT_TRUE(manager->ProcessPid(kind) == Some(pids[i]));
  }

  // Check that every process identifier is unique
  std::sort(pids.begin(), pids.end());
  auto adjacentEqualPids = std::adjacent_find(pids.begin(), pids.end());
  ASSERT_TRUE(adjacentEqualPids == pids.end());

  // After being individually shut down, a process is no longer referenced
  for (kind_t i = 0; i < SandboxingKind::COUNT; ++i) {
    auto kind = static_cast<SandboxingKind>(i);
    manager->CleanShutdown(kind);
    ASSERT_TRUE(manager->ProcessPid(kind).isNothing());
  }

  // Drain the event queue.
  NS_ProcessPendingEvents(nullptr);
}

// SandboxingKind::HW_INFERENCE does not exist on Android, and the test below
// also keeps a second utility process alive alongside it, which Android cannot
// do: it declares a single `utility` service.
#ifndef ANDROID

// Checks that a request arriving in the window right after teardown launches
// a fresh process. HWInferenceParent is bound to PHWInference, a separate
// toplevel from PUtilityProcess, so once DestroyProcess has removed the
// UtilityProcessManager entry the cached actor still reports CanSend() until
// the peer dies and the channel errors, a main-thread dispatch later.
// GetSingleton evicts it in that window so StartUtility relaunches instead of
// taking its CanSend() fast path.
TEST_F(TestUtilityProcess, HWInferenceRelaunchesAfterShutdown) {
  auto manager = UtilityProcessManager::GetSingleton();
  ASSERT_TRUE(manager);

  // An unrelated kind stays up for the whole test, so the manager singleton
  // survives shutting HWInference down: DestroyProcess drops the singleton once
  // no utility process is left.
  auto keepAlive =
      WaitFor(manager->LaunchProcess(SandboxingKind::GENERIC_UTILITY));
  ASSERT_TRUE(keepAlive.isOk());

  // StartHWInference binds HWInferenceParent and caches it in sInstance, which
  // is what puts an actor there to go stale.
  auto res = WaitFor(manager->StartHWInference());
  ASSERT_TRUE(res.isOk())
  << "Launch LaunchError: " << res.inspectErr().FunctionName() << ", "
  << res.inspectErr().ErrorCode();

  auto firstPid = manager->ProcessPid(SandboxingKind::HW_INFERENCE);
  ASSERT_TRUE(firstPid.isSome());

  // Nothing spins the event loop between these two, so the actor's
  // ActorDestroy cannot have run yet: this is exactly the stale window.
  manager->CleanShutdown(SandboxingKind::HW_INFERENCE);
  auto relaunch = WaitFor(manager->StartHWInference());
  ASSERT_TRUE(relaunch.isOk())
  << "Relaunch LaunchError: " << relaunch.inspectErr().FunctionName() << ", "
  << relaunch.inspectErr().ErrorCode();

  // A fresh process is running, with a different pid.
  auto secondPid = manager->ProcessPid(SandboxingKind::HW_INFERENCE);
  ASSERT_TRUE(secondPid.isSome());
  ASSERT_NE(*firstPid, *secondPid);

  manager->CleanShutdown(SandboxingKind::HW_INFERENCE);
  manager->CleanShutdown(SandboxingKind::GENERIC_UTILITY);

  // Drain the event queue.
  NS_ProcessPendingEvents(nullptr);
}

// Consumers decide the HWInference process' lifetime: it must go away as soon
// as the last keep-alive is released, rather than living until browser
// shutdown.
TEST_F(TestUtilityProcess, HWInferenceKeepAlive) {
  auto manager = UtilityProcessManager::GetSingleton();
  ASSERT_TRUE(manager);

  // Two consumers sharing the process' single keep-alive: it must survive
  // until *both* are gone.
  RefPtr<UtilityProcessKeepAlive> first =
      manager->LaunchProcessWithKeepAlive(SandboxingKind::HW_INFERENCE);
  RefPtr<UtilityProcessKeepAlive> second =
      manager->LaunchProcessWithKeepAlive(SandboxingKind::HW_INFERENCE);
  ASSERT_TRUE(first);
  ASSERT_EQ(first.get(), second.get());

  auto res = WaitFor(first->GetLaunchPromise());
  ASSERT_TRUE(res.isOk())
  << "Launch LaunchError: " << res.inspectErr().FunctionName() << ", "
  << res.inspectErr().ErrorCode();

  auto pid = manager->ProcessPid(SandboxingKind::HW_INFERENCE);
  ASSERT_TRUE(pid.isSome());

  first = nullptr;
  ASSERT_TRUE(manager->ProcessPid(SandboxingKind::HW_INFERENCE) == pid);

  second = nullptr;
  ASSERT_TRUE(manager->ProcessPid(SandboxingKind::HW_INFERENCE).isNothing());

  // Drain the event queue.
  NS_ProcessPendingEvents(nullptr);
}

// A keep-alive that outlives the process it was taken on must not shut down the
// process that replaced it.
TEST_F(TestUtilityProcess, HWInferenceKeepAliveOutlivingItsProcess) {
  auto manager = UtilityProcessManager::GetSingleton();
  ASSERT_TRUE(manager);

  // An unrelated kind keeps the manager singleton alive across shutting
  // HWInference down, as DestroyProcess drops it once no utility process is
  // left.
  auto other = WaitFor(manager->LaunchProcess(SandboxingKind::GENERIC_UTILITY));
  ASSERT_TRUE(other.isOk());

  auto launch = [&manager]() {
    RefPtr<UtilityProcessKeepAlive> keepAlive =
        manager->LaunchProcessWithKeepAlive(SandboxingKind::HW_INFERENCE);
    if (keepAlive && WaitFor(keepAlive->GetLaunchPromise()).isErr()) {
      keepAlive = nullptr;
    }
    return keepAlive;
  };

  RefPtr<UtilityProcessKeepAlive> stale = launch();
  ASSERT_TRUE(stale);

  manager->CleanShutdown(SandboxingKind::HW_INFERENCE);

  RefPtr<UtilityProcessKeepAlive> current = launch();
  ASSERT_TRUE(current);
  ASSERT_NE(stale.get(), current.get());

  auto pid = manager->ProcessPid(SandboxingKind::HW_INFERENCE);
  ASSERT_TRUE(pid.isSome());

  stale = nullptr;
  ASSERT_TRUE(manager->ProcessPid(SandboxingKind::HW_INFERENCE) == pid);

  current = nullptr;
  ASSERT_TRUE(manager->ProcessPid(SandboxingKind::HW_INFERENCE).isNothing());

  manager->CleanShutdown(SandboxingKind::GENERIC_UTILITY);

  // Drain the event queue.
  NS_ProcessPendingEvents(nullptr);
}

#endif  // !ANDROID

#if defined(XP_WIN)
static void LoadLibraryCrash_Test() {
  mozilla::gtest::DisableCrashReporter();
  // Just a uuidgen name to have something random
  UtilityProcessImpl::LoadLibraryOrCrash(
      L"2b49036e-6ba3-400c-a297-38fa1f6c5255.dll");
}

TEST_F(TestUtilityProcess, LoadLibraryCrash) {
  ASSERT_DEATH_IF_SUPPORTED(LoadLibraryCrash_Test(), "");
}
#endif  // defined(XP_WIN)
