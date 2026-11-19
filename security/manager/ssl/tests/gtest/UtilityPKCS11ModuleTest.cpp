/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if defined(MOZ_NO_SMART_CARDS)
#  error This file should only be used when MOZ_NO_SMART_CARDS is not defined.
#endif  // MOZ_NO_SMART_CARDS

#include "gtest/gtest.h"
#include "mozilla/gtest/WaitFor.h"
#include "mozilla/gtest/ipc/TestUtilityProcess.h"
#include "mozilla/ipc/UtilityProcessManager.h"
#include "mozilla/psm/PKCS11ModuleParent.h"
#include "nsThreadUtils.h"

using namespace mozilla;
using namespace mozilla::gtest::ipc;
using namespace mozilla::ipc;
using namespace mozilla::psm;

class psm_UtilityPKCS11Module : public TestUtilityProcess {};

TEST_F(psm_UtilityPKCS11Module, Launch) {
  auto manager = UtilityProcessManager::GetSingleton();
  ASSERT_TRUE(manager);

  auto res = WaitFor(manager->StartPKCS11Module());
  ASSERT_TRUE(res.isOk())
  << "LaunchError: " << res.inspectErr().FunctionName() << ", "
  << res.inspectErr().ErrorCode();

  auto parent = res.unwrap();
  ASSERT_TRUE(parent);
  ASSERT_TRUE(parent->CanSend());

  auto utilityPid = manager->ProcessPid(SandboxingKind::PKCS11_MODULE);
  ASSERT_TRUE(utilityPid.isSome());
  ASSERT_GE(*utilityPid, base::ProcessId(1));

  manager->CleanShutdown(SandboxingKind::PKCS11_MODULE);

  utilityPid = manager->ProcessPid(SandboxingKind::PKCS11_MODULE);
  ASSERT_TRUE(utilityPid.isNothing());

  // Drain the event queue.
  NS_ProcessPendingEvents(nullptr);
}
