/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ipc_CrashReporterHelper_h
#define mozilla_ipc_CrashReporterHelper_h

#include "CrashReporterHost.h"
#include "mozilla/UniquePtr.h"
#include "nsIAppStartup.h"
#include "nsExceptionHandler.h"
#include "nsICrashService.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"

namespace mozilla::ipc {

/**
 * This class encapsulates the common elements of crash report handling for
 * toplevel protocols representing processes. To use this class, you should:
 *
 * 1. Declare a method to initialize the crash reporter in your IPDL:
 *    `async InitCrashReporter(NativeThreadId threadId)`
 *
 * 2. Inherit from this class with the name of your derived class as the
 *    type parameter. Ex: `class MyClass : public CrashReporterHelper<MyClass>`
 *
 * 3. Provide a public `PROCESS_TYPE` constant for your class. Ex:
 *
 *    ```
 *    public:
 *      static constexpr GeckoProcessType PROCESS_TYPE =
 *          GeckoProcessType_GMPlugin;
 *    ```
 *
 * 4. When your protocol actor is destroyed with a reason of `AbnormalShutdown`,
 *    you should call `GenerateCrashReport()`. If you need the crash
 *    report ID it will be copied in the second optional parameter upon
 *    successful crash report generation.
 */
template <class Derived>
class CrashReporterHelper {
 public:
  CrashReporterHelper() : mCrashReporter(nullptr) {}
  IPCResult RecvInitCrashReporter(
      const CrashReporter::CrashReporterInitArgs& aInitArgs) {
    GeckoChildID child_id = static_cast<Derived*>(this)->OtherChildID();
    mCrashReporter = std::make_unique<ipc::CrashReporterHost>(
        Derived::PROCESS_TYPE, child_id, aInitArgs);
    return IPC_OK();
  }

 protected:
  void GenerateCrashReport(nsString* aMinidumpId = nullptr) {
    nsAutoString minidumpId;
    if (!mCrashReporter) {
      HandleOrphanedMinidump(minidumpId);
    } else if (mCrashReporter->GenerateCrashReport()) {
      minidumpId = mCrashReporter->MinidumpID();
    } else {
      NS_WARNING(
          nsPrintfCString(
              "child process id = %d crashed but no minidump could be taken",
              static_cast<Derived*>(this)->OtherChildID())
              .get());
    }

    if (aMinidumpId) {
      *aMinidumpId = minidumpId;
    }

    mCrashReporter = nullptr;
  }

  /**
   * Under MOZ_CRASHREPORTER_SHUTDOWN, quit for a child crash that left a
   * minidump, or for any child crash when the crash reporter cannot produce
   * one (disabled, or the dummy implementation).
   */
  void MaybeTerminateProcess(const nsAString& aMinidumpId) {
    if (!PR_GetEnv("MOZ_CRASHREPORTER_SHUTDOWN")) {
      return;
    }
    if (aMinidumpId.IsEmpty() && CrashReporter::GetEnabled()) {
      return;
    }
    NS_WARNING(nsPrintfCString("Shutting down due to %s process crash.",
                               XRE_GetProcessTypeString())
                   .get());
    nsCOMPtr<nsIAppStartup> appService =
        do_GetService("@mozilla.org/toolkit/app-startup;1");
    if (appService) {
      appService->Quit(nsIAppStartup::eForceQuit, 1);
    }
  }

 private:
  void HandleOrphanedMinidump(nsString& aMinidumpId) {
    GeckoChildID child_id = static_cast<Derived*>(this)->OtherChildID();
    if (CrashReporter::FinalizeOrphanedMinidump(child_id, Derived::PROCESS_TYPE,
                                                &aMinidumpId)) {
      CrashReporterHost::RecordCrash(Derived::PROCESS_TYPE,
                                     nsICrashService::CRASH_TYPE_CRASH,
                                     aMinidumpId);
    } else {
      NS_WARNING(nsPrintfCString("child process id = %d"
                                 " crashed without leaving a minidump behind",
                                 child_id)
                     .get());
    }
  }

 protected:
  UniquePtr<ipc::CrashReporterHost> mCrashReporter;
};

}  // namespace mozilla::ipc

#endif  // mozilla_ipc_CrashReporterHelper_h
