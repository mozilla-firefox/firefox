/* * This Source Code Form is subject to the terms of the Mozilla Public
 * * License, v. 2.0. If a copy of the MPL was not distributed with this
 * * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GTestRunner.h"
#include "gtest/gtest.h"
#include "mozilla/Attributes.h"
#include "mozilla/FOG.h"
#include "mozilla/Preferences.h"
#include "nsICrashReporter.h"
#include "nsString.h"
#include "testing/TestHarness.h"
#include "prenv.h"
#include "prtime.h"
#ifdef ANDROID
#  include <android/log.h>
#endif
#ifdef XP_WIN
#  include "mozilla/ipc/WindowsMessageLoop.h"
#endif

using ::testing::EmptyTestEventListener;
using ::testing::InitGoogleTest;
using ::testing::TestEventListeners;
using ::testing::TestInfo;
using ::testing::TestPartResult;
using ::testing::TestSuite;
using ::testing::UnitTest;

namespace mozilla {

#ifdef ANDROID
#  define MOZ_STDOUT_PRINT(...) \
    __android_log_print(ANDROID_LOG_INFO, "gtest", __VA_ARGS__);
#else
#  define MOZ_STDOUT_PRINT(...) printf(__VA_ARGS__);
#endif

#define MOZ_PRINT(...)              \
  MOZ_STDOUT_PRINT(__VA_ARGS__);    \
  if (mLogFile) {                   \
    fprintf(mLogFile, __VA_ARGS__); \
  }

#define MOZ_LOG_ACTION(action, fmt, ...) \
  MOZ_PRINT("{\"action\":\"" action "\"" fmt "}\n", ##__VA_ARGS__)

// Emit mozlog-compatible structured JSON to stdout (and optionally a log file).
class MozillaPrinter : public EmptyTestEventListener {
 public:
  MozillaPrinter() : mLogFile(nullptr) {
    char* path = PR_GetEnv("MOZ_GTEST_LOG_PATH");
    if (path) {
      mLogFile = fopen(path, "w");
    }
  }

  virtual void OnTestProgramStart(const UnitTest& /* aUnitTest */) override {
    MOZ_LOG_ACTION("suite_start",
                   ",\"source\":\"gtest\",\"name\":\"gtest\",\"tests\":{}");
  }

  virtual void OnTestProgramEnd(const UnitTest& /*aUnitTest*/) override {
    MOZ_LOG_ACTION("suite_end", ",\"source\":\"gtest\"");
    if (mLogFile) {
      fclose(mLogFile);
      mLogFile = nullptr;
    }
  }

  virtual void OnTestSuiteStart(const TestSuite& aTestSuite) override {
    nsCString name;
    JsonEscape(nsDependentCString(aTestSuite.name()), name);
    MOZ_LOG_ACTION("group_start", ",\"name\":\"%s\"", name.get());
  }

  virtual void OnTestSuiteEnd(const TestSuite& aTestSuite) override {
    nsCString name;
    JsonEscape(nsDependentCString(aTestSuite.name()), name);
    MOZ_LOG_ACTION("group_end", ",\"name\":\"%s\"", name.get());
  }

  virtual void OnTestStart(const TestInfo& aTestInfo) override {
    mTestInfo = &aTestInfo;
    nsCString test = TestName(aTestInfo);
    MOZ_LOG_ACTION("test_start", ",\"test\":\"%s\"", test.get());
  }

  virtual void OnTestPartResult(
      const TestPartResult& aTestPartResult) override {
    const char* caseName = mTestInfo ? mTestInfo->test_case_name() : "?";
    const char* testName = mTestInfo ? mTestInfo->name() : "?";
    nsCString test;
    JsonEscape(nsDependentCString(caseName), test);
    test.Append('.');
    {
      nsCString escapedName;
      JsonEscape(nsDependentCString(testName), escapedName);
      test.Append(escapedName);
    }
    nsCString message;
    JsonEscape(nsDependentCString(aTestPartResult.summary()), message);
    if (aTestPartResult.failed()) {
      nsCString file;
      if (const char* fileName = aTestPartResult.file_name()) {
        JsonEscape(nsDependentCString(fileName), file);
      }
      MOZ_LOG_ACTION("test_status",
                     ",\"test\":\"%s\",\"subtest\":\"\","
                     "\"status\":\"FAIL\",\"expected\":\"PASS\","
                     "\"message\":\"%s\",\"stack\":\"%s:%i\"",
                     test.get(), message.get(), file.get(),
                     aTestPartResult.line_number());
    } else {
      MOZ_LOG_ACTION("test_status",
                     ",\"test\":\"%s\",\"subtest\":\"\","
                     "\"status\":\"PASS\",\"message\":\"%s\"",
                     test.get(), message.get());
    }
  }

  virtual void OnTestEnd(const TestInfo& aTestInfo) override {
    nsCString test = TestName(aTestInfo);
    if (aTestInfo.result()->Passed()) {
      MOZ_LOG_ACTION("test_end", ",\"test\":\"%s\",\"status\":\"PASS\"",
                     test.get());
    } else if (aTestInfo.result()->Skipped()) {
      MOZ_LOG_ACTION("test_end", ",\"test\":\"%s\",\"status\":\"FAIL\"",
                     test.get());
    } else {
      MOZ_LOG_ACTION("test_end",
                     ",\"test\":\"%s\",\"status\":\"FAIL\""
                     ",\"expected\":\"PASS\"",
                     test.get());
    }
    MOZ_ASSERT(&aTestInfo == mTestInfo);
    mTestInfo = nullptr;
  }

  const TestInfo* mTestInfo;
  FILE* mLogFile;

 private:
  static nsCString TestName(const TestInfo& aTestInfo) {
    nsCString test;
    JsonEscape(nsDependentCString(aTestInfo.test_case_name()), test);
    test.Append('.');
    nsCString escapedName;
    JsonEscape(nsDependentCString(aTestInfo.name()), escapedName);
    test.Append(escapedName);
    return test;
  }

  // Escape characters that are special in JSON strings.
  static void JsonEscape(const nsACString& aInput, nsACString& aOutput) {
    aOutput.Truncate();
    for (auto iter = aInput.BeginReading(); iter != aInput.EndReading();
         ++iter) {
      char c = *iter;
      switch (c) {
        case '"':
          aOutput.AppendLiteral("\\\"");
          break;
        case '\\':
          aOutput.AppendLiteral("\\\\");
          break;
        case '\n':
          aOutput.AppendLiteral("\\n");
          break;
        case '\r':
          aOutput.AppendLiteral("\\r");
          break;
        case '\t':
          aOutput.AppendLiteral("\\t");
          break;
        default:
          aOutput.Append(c);
          break;
      }
    }
  }
};

static void ReplaceGTestLogger() {
  // Replace the GTest logger so that it can be passed
  // by the mozilla test parsers.
  // Code is based on:
  // http://googletest.googlecode.com/svn/trunk/samples/sample9_unittest.cc
  UnitTest& unitTest = *UnitTest::GetInstance();
  TestEventListeners& listeners = unitTest.listeners();
  delete listeners.Release(listeners.default_result_printer());

  listeners.Append(new MozillaPrinter);
}

int RunGTestFunc(int* argc, char** argv) {
  InitGoogleTest(argc, argv);

  ReplaceGTestLogger();

  PR_SetEnv("XPCOM_DEBUG_BREAK=stack-and-abort");

  ScopedXPCOM xpcom("GTest");

#ifdef XP_WIN
  mozilla::ipc::windows::InitUIThread();
#endif
#ifdef ANDROID
  // On Android, gtest is running in an application, which uses a
  // current working directory of '/' by default. Desktop tests
  // sometimes assume that support files are in the current
  // working directory. For compatibility with desktop, the Android
  // harness pushes test support files to the device at the location
  // specified by MOZ_GTEST_CWD and gtest changes the cwd to that
  // location.
  char* path = PR_GetEnv("MOZ_GTEST_CWD");
  chdir(path);
#endif
  nsCOMPtr<nsICrashReporter> crashreporter;
  char* crashreporterStr = PR_GetEnv("MOZ_CRASHREPORTER");
  if (crashreporterStr && !strcmp(crashreporterStr, "1")) {
    // TODO: move this to an even-more-common location to use in all
    // C++ unittests
    crashreporter = do_GetService("@mozilla.org/toolkit/crash-reporter;1");
    if (crashreporter) {
      printf_stderr("Setting up crash reporting\n");
      char* path = PR_GetEnv("MOZ_GTEST_MINIDUMPS_PATH");
      nsCOMPtr<nsIFile> file;
      if (path) {
        nsresult rv =
            NS_NewUTF8LocalFile(nsDependentCString(path), getter_AddRefs(file));
        if (NS_FAILED(rv)) {
          printf_stderr("Ignoring invalid MOZ_GTEST_MINIDUMPS_PATH\n");
        }
      }
      if (!file) {
        nsCOMPtr<nsIProperties> dirsvc =
            do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID);
        nsresult rv = dirsvc->Get(NS_OS_CURRENT_WORKING_DIR,
                                  NS_GET_IID(nsIFile), getter_AddRefs(file));
        MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
      }
      crashreporter->SetEnabled(true);
      crashreporter->SetMinidumpPath(file);
    }
  }

  // FOG should init exactly once, as early into running as possible, to enable
  // instrumentation tests to work properly.
  // However, at init, Glean may decide to send a ping. So let's first tell FOG
  // that these pings shouldn't actually be uploaded.
  Preferences::SetInt("telemetry.fog.test.localhost_port", -1);
  // Though the default user-activity limits ought to be longer than a test,
  // ensure that they don't trigger unnecessary ping submission (which clears
  // storage, making it hard to test instrumentation).
  Preferences::SetInt("telemetry.fog.test.activity_limit", -1);
  Preferences::SetInt("telemetry.fog.test.inactivity_limit", -1);
  // Slow down early "events" pings to make testing easier.
  Preferences::SetBool("telemetry.fog.test.decelerate_early_events", true);
  // Prevent idle-daily from firing during tests.
  Preferences::SetInt("idle.lastDailyNotification",
                      int32_t(PR_Now() / PR_USEC_PER_SEC));
  const nsCString empty;
  RefPtr<FOG>(FOG::GetSingleton())->InitializeFOG(empty, empty, false);

  return RUN_ALL_TESTS();
}

// We use a static var 'RunGTest' defined in nsAppRunner.cpp.
// RunGTest is initialized to nullptr but if GTest (this file)
// is linked in then RunGTest will be set here indicating
// GTest is supported.
MOZ_RUNINIT class _InitRunGTest {
 public:
  _InitRunGTest() { RunGTest = RunGTestFunc; }
} InitRunGTest;

}  // namespace mozilla
