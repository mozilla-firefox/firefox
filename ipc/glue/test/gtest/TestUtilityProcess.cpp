/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <type_traits>

#include "gtest/gtest.h"
#include "mozilla/gtest/ipc/TestUtilityProcess.h"
#include "mozilla/gtest/WaitFor.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "nsThreadUtils.h"

#include "mozilla/ipc/UtilityProcessManager.h"
#include "nsIProcessToolsService.h"
#include "nsServiceManagerUtils.h"

#if defined(MOZ_WIDGET_ANDROID) || defined(XP_MACOSX)
#  include "nsIAppShellService.h"
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

// Android declares a single `utility` service, so it cannot keep several
// utility processes alive at once.
#ifndef ANDROID

// A keep-alive whose process crashed turns inert: letting go of it must not
// touch anything else.
TEST_F(TestUtilityProcess, KeepAliveOutlivesCrashedProcess) {
  auto manager = UtilityProcessManager::GetSingleton();
  ASSERT_TRUE(manager);

  // An unrelated process stays up throughout, so the crash cannot be confused
  // with everything having gone away.
  auto other = WaitFor(manager->LaunchProcess(SandboxingKind::GENERIC_UTILITY));
  ASSERT_TRUE(other.isOk());

  RefPtr<UtilityProcessKeepAlive> keepAlive =
      manager->LaunchIndependentProcess(SandboxingKind::GENERIC_UTILITY);
  ASSERT_TRUE(keepAlive);
  auto res = WaitFor(keepAlive->GetLaunchPromise());
  ASSERT_TRUE(res.isOk())
  << "Launch LaunchError: " << res.inspectErr().FunctionName() << ", "
  << res.inspectErr().ErrorCode();
  ASSERT_EQ(manager->AliveProcesses(), 2u);

  nsCOMPtr<nsIProcessToolsService> processTools =
      do_GetService("@mozilla.org/processtools-service;1");
  ASSERT_TRUE(processTools);
  ASSERT_TRUE(NS_SUCCEEDED(
      processTools->Kill(keepAlive->GetProcessParent()->OtherPid())));

  ASSERT_TRUE(SpinEventLoopUntil("KeepAliveOutlivesCrashedProcess"_ns,
                                 [&]() { return !keepAlive->IsAlive(); }));
  ASSERT_FALSE(keepAlive->GetProcessParent());
  ASSERT_EQ(manager->AliveProcesses(), 1u);

  keepAlive = nullptr;
  ASSERT_EQ(manager->AliveProcesses(), 1u);
  ASSERT_TRUE(manager->ProcessPid(SandboxingKind::GENERIC_UTILITY).isSome());

  manager->CleanShutdown(SandboxingKind::GENERIC_UTILITY);

  // Drain the event queue.
  NS_ProcessPendingEvents(nullptr);
}

// Independent processes are distinct from the shared process of their kind and
// from each other, and each one lives exactly as long as its keep-alive.
TEST_F(TestUtilityProcess, IndependentProcesses) {
  auto manager = UtilityProcessManager::GetSingleton();
  ASSERT_TRUE(manager);

  auto shared =
      WaitFor(manager->LaunchProcess(SandboxingKind::GENERIC_UTILITY));
  ASSERT_TRUE(shared.isOk());
  auto sharedPid = manager->ProcessPid(SandboxingKind::GENERIC_UTILITY);
  ASSERT_TRUE(sharedPid.isSome());

  RefPtr<UtilityProcessKeepAlive> first =
      manager->LaunchIndependentProcess(SandboxingKind::GENERIC_UTILITY);
  RefPtr<UtilityProcessKeepAlive> second =
      manager->LaunchIndependentProcess(SandboxingKind::GENERIC_UTILITY);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ASSERT_NE(first.get(), second.get());
  for (const auto& keepAlive : {first, second}) {
    auto res = WaitFor(keepAlive->GetLaunchPromise());
    ASSERT_TRUE(res.isOk())
    << "Launch LaunchError: " << res.inspectErr().FunctionName() << ", "
    << res.inspectErr().ErrorCode();
  }

  // Three processes of the same kind, all with distinct pids.
  ASSERT_EQ(manager->AliveProcesses(), 3u);
  nsTArray<base::ProcessId> pids;
  for (const auto& parent : manager->GetAllProcessesProcessParent()) {
    pids.AppendElement(parent->OtherPid());
  }
  ASSERT_EQ(pids.Length(), 3u);
  ASSERT_TRUE(pids.Contains(*sharedPid));
  pids.Sort();
  ASSERT_TRUE(std::adjacent_find(pids.begin(), pids.end()) == pids.end());

  // Only the process a keep-alive owns goes away with it; the shared one and
  // the per-kind lookups are unaffected.
  first = nullptr;
  ASSERT_EQ(manager->AliveProcesses(), 2u);
  ASSERT_TRUE(manager->ProcessPid(SandboxingKind::GENERIC_UTILITY) ==
              sharedPid);

  second = nullptr;
  ASSERT_EQ(manager->AliveProcesses(), 1u);
  ASSERT_TRUE(manager->ProcessPid(SandboxingKind::GENERIC_UTILITY) ==
              sharedPid);

  manager->CleanShutdown(SandboxingKind::GENERIC_UTILITY);
  ASSERT_TRUE(manager->ProcessPid(SandboxingKind::GENERIC_UTILITY).isNothing());

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
