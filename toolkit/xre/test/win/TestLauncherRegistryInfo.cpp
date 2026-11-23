/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#define MOZ_USE_LAUNCHER_ERROR

#include "mozilla/LauncherRegistryInfo.h"
#include "mozilla/NativeNt.h"
#include "mozilla/ScopeExit.h"
#include "nsWindowsHelpers.h"

#include "LauncherRegistryInfo.cpp"

#include <string>

static const char kMsgStart[] = "TEST-FAILED | LauncherRegistryInfo | ";

static const wchar_t kRegKeyPath[] = L"SOFTWARE\\" EXPAND_STRING_MACRO(
    MOZ_APP_VENDOR) L"\\" EXPAND_STRING_MACRO(MOZ_APP_BASENAME) L"\\Launcher";
static const wchar_t kBrowserSuffix[] = L"|Browser";
static const wchar_t kLauncherSuffix[] = L"|Launcher";
static const wchar_t kImageSuffix[] = L"|Image";
static const wchar_t kTelemetrySuffix[] = L"|Telemetry";
static const wchar_t kCrashTimestampSuffix[] = L"|LauncherCrashTime";

constinit static std::wstring gBrowserValue;
constinit static std::wstring gLauncherValue;
constinit static std::wstring gImageValue;
constinit static std::wstring gTelemetryValue;
constinit static std::wstring gCrashTimestampValue;

static DWORD gMyImageTimestamp;

#define RUN_TEST(result, fn)                                                 \
  if ((result = fn()).isErr()) {                                             \
    const mozilla::LauncherError& err = result.inspectErr();                 \
    printf("%s%s | %08lx (%s:%d)\n", kMsgStart, #fn, err.mError.AsHResult(), \
           err.mFile, err.mLine);                                            \
    return 1;                                                                \
  }

#define EXPECT_COMMIT_IS_OK()                        \
  do {                                               \
    mozilla::LauncherVoidResult vr2 = info.Commit(); \
    if (vr2.isErr()) {                               \
      return vr2;                                    \
    }                                                \
  } while (0)

#define EXPECT_CHECK_RESULT_IS(desired, expected)                       \
  do {                                                                  \
    mozilla::LauncherResult<mozilla::LauncherRegistryInfo::ProcessType> \
        result = info.Check(mozilla::LauncherRegistryInfo::desired);    \
    if (result.isErr()) {                                               \
      return result.propagateErr();                                     \
    }                                                                   \
    if (result.unwrap() != mozilla::LauncherRegistryInfo::expected) {   \
      return LAUNCHER_ERROR_FROM_HRESULT(E_FAIL);                       \
    }                                                                   \
  } while (0)

#define EXPECT_ENABLED_STATE_IS(expected)                                \
  do {                                                                   \
    mozilla::LauncherResult<mozilla::LauncherRegistryInfo::EnabledState> \
        enabled = info.IsEnabled();                                      \
    if (enabled.isErr()) {                                               \
      return enabled.propagateErr();                                     \
    }                                                                    \
    if (enabled.unwrap() != mozilla::LauncherRegistryInfo::expected) {   \
      return LAUNCHER_ERROR_FROM_HRESULT(E_UNEXPECTED);                  \
    }                                                                    \
  } while (0)

#define EXPECT_TELEMETRY_IS_ENABLED(expected)                          \
  do {                                                                 \
    mozilla::LauncherResult<bool> enabled = info.IsTelemetryEnabled(); \
    if (enabled.isErr()) {                                             \
      return enabled.propagateErr();                                   \
    }                                                                  \
    if (enabled.unwrap() != expected) {                                \
      return LAUNCHER_ERROR_FROM_HRESULT(E_UNEXPECTED);                \
    }                                                                  \
  } while (0)

#define EXPECT_REG_DWORD_EXISTS_AND_EQ(name, expected)      \
  do {                                                      \
    mozilla::LauncherResult<mozilla::Maybe<DWORD>> result = \
        ReadRegistryValueData<DWORD>(name, REG_DWORD);      \
    if (result.isErr()) {                                   \
      return result.propagateErr();                         \
    }                                                       \
    if (result.inspect().isNothing() ||                     \
        result.inspect().value() != expected) {             \
      return LAUNCHER_ERROR_FROM_HRESULT(E_UNEXPECTED);     \
    }                                                       \
  } while (0)

#define EXPECT_REG_QWORD_EXISTS(name)                          \
  do {                                                         \
    mozilla::LauncherResult<mozilla::Maybe<uint64_t>> result = \
        ReadRegistryValueData<uint64_t>(name, REG_QWORD);      \
    if (result.isErr()) {                                      \
      return result.propagateErr();                            \
    }                                                          \
    if (result.inspect().isNothing()) {                        \
      return LAUNCHER_ERROR_FROM_HRESULT(E_UNEXPECTED);        \
    }                                                          \
  } while (0)

#define EXPECT_REG_QWORD_EXISTS_AND_EQ(name, expected)         \
  do {                                                         \
    mozilla::LauncherResult<mozilla::Maybe<uint64_t>> result = \
        ReadRegistryValueData<uint64_t>(name, REG_QWORD);      \
    if (result.isErr()) {                                      \
      return result.propagateErr();                            \
    }                                                          \
    if (result.inspect().isNothing() ||                        \
        result.inspect().value() != expected) {                \
      return LAUNCHER_ERROR_FROM_HRESULT(E_UNEXPECTED);        \
    }                                                          \
  } while (0)

#define EXPECT_REG_DWORD_DOES_NOT_EXIST(name)               \
  do {                                                      \
    mozilla::LauncherResult<mozilla::Maybe<DWORD>> result = \
        ReadRegistryValueData<DWORD>(name, REG_DWORD);      \
    if (result.isErr()) {                                   \
      return result.propagateErr();                         \
    }                                                       \
    if (result.inspect().isSome()) {                        \
      return LAUNCHER_ERROR_FROM_HRESULT(E_UNEXPECTED);     \
    }                                                       \
  } while (0)

#define EXPECT_REG_QWORD_DOES_NOT_EXIST(name)                  \
  do {                                                         \
    mozilla::LauncherResult<mozilla::Maybe<uint64_t>> result = \
        ReadRegistryValueData<uint64_t>(name, REG_QWORD);      \
    if (result.isErr()) {                                      \
      return result.propagateErr();                            \
    }                                                          \
    if (result.inspect().isSome()) {                           \
      return LAUNCHER_ERROR_FROM_HRESULT(E_UNEXPECTED);        \
    }                                                          \
  } while (0)

template <typename T>
static mozilla::LauncherResult<mozilla::Maybe<T>> ReadRegistryValueData(
    const std::wstring& name, DWORD expectedType) {
  T data;
  DWORD dataLen = sizeof(data);
  DWORD type;
  LSTATUS status = ::RegGetValueW(HKEY_CURRENT_USER, kRegKeyPath, name.c_str(),
                                  RRF_RT_ANY, &type, &data, &dataLen);
  if (status == ERROR_FILE_NOT_FOUND) {
    return mozilla::Maybe<T>();
  }

  if (status != ERROR_SUCCESS) {
    return LAUNCHER_ERROR_FROM_WIN32(status);
  }

  if (type != expectedType) {
    return LAUNCHER_ERROR_FROM_WIN32(ERROR_DATATYPE_MISMATCH);
  }

  return mozilla::Some(data);
}

template <typename T>
static mozilla::LauncherVoidResult WriteRegistryValueData(
    const std::wstring& name, DWORD type, T data) {
  LSTATUS status = ::RegSetKeyValueW(HKEY_CURRENT_USER, kRegKeyPath,
                                     name.c_str(), type, &data, sizeof(T));
  if (status != ERROR_SUCCESS) {
    return LAUNCHER_ERROR_FROM_WIN32(status);
  }

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult DeleteRegistryValueData(
    const std::wstring& name) {
  LSTATUS status =
      ::RegDeleteKeyValueW(HKEY_CURRENT_USER, kRegKeyPath, name.c_str());
  if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND) {
    return mozilla::Ok();
  }

  return LAUNCHER_ERROR_FROM_WIN32(status);
}

static mozilla::LauncherVoidResult DeleteAllRegstryValues() {
  // Unblock commit via ReflectPrefToRegistry
  // (We need to set false, and then true to bypass the early return)
  mozilla::LauncherRegistryInfo info;
  mozilla::LauncherVoidResult vr = info.ReflectPrefToRegistry(false);
  vr = info.ReflectPrefToRegistry(true);
  if (vr.isErr()) {
    return vr;
  }

  vr = DeleteRegistryValueData(gImageValue);
  if (vr.isErr()) {
    return vr;
  }

  vr = DeleteRegistryValueData(gLauncherValue);
  if (vr.isErr()) {
    return vr;
  }

  vr = DeleteRegistryValueData(gBrowserValue);
  if (vr.isErr()) {
    return vr;
  }

  vr = DeleteRegistryValueData(gTelemetryValue);
  if (vr.isErr()) {
    return vr;
  }

  return DeleteRegistryValueData(gCrashTimestampValue);
}

bool GetInstallHash(const char16_t*, mozilla::UniquePtr<NS_tchar[]>& result) {
  return true;
}

static mozilla::LauncherVoidResult SetupEnabledScenario() {
  // Reset the registry state to an enabled state. First, we delete all existing
  // registry values (if any).
  mozilla::LauncherVoidResult vr = DeleteAllRegstryValues();
  if (vr.isErr()) {
    return vr;
  }

  // Now we run Check(Launcher)...
  mozilla::LauncherRegistryInfo info;
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Launcher);
  EXPECT_COMMIT_IS_OK();
  // ...and Check(Browser)
  EXPECT_CHECK_RESULT_IS(ProcessType::Browser, ProcessType::Browser);
  EXPECT_COMMIT_IS_OK();

  // By this point we are considered to be fully enabled.
  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestEmptyRegistry() {
  mozilla::LauncherVoidResult vr = DeleteAllRegstryValues();
  if (vr.isErr()) {
    return vr;
  }

  mozilla::LauncherRegistryInfo info;
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Launcher);
  EXPECT_COMMIT_IS_OK();

  // LauncherRegistryInfo should have created Launcher and Image values
  EXPECT_REG_DWORD_EXISTS_AND_EQ(gImageValue, gMyImageTimestamp);
  EXPECT_REG_QWORD_EXISTS(gLauncherValue);
  EXPECT_REG_QWORD_DOES_NOT_EXIST(gBrowserValue);

  EXPECT_ENABLED_STATE_IS(EnabledState::FailDisabled);

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestNormal() {
  mozilla::LauncherVoidResult vr = DeleteAllRegstryValues();
  if (vr.isErr()) {
    return vr;
  }
  vr = WriteRegistryValueData<DWORD>(gImageValue, REG_DWORD, gMyImageTimestamp);
  if (vr.isErr()) {
    return vr;
  }
  vr = WriteRegistryValueData<uint64_t>(gLauncherValue, REG_QWORD, QPCNowRaw());
  if (vr.isErr()) {
    return vr;
  }

  mozilla::LauncherRegistryInfo info;
  EXPECT_CHECK_RESULT_IS(ProcessType::Browser, ProcessType::Browser);
  EXPECT_COMMIT_IS_OK();

  // Make sure the browser timestamp is newer than the launcher's
  mozilla::LauncherResult<mozilla::Maybe<uint64_t>> launcherTs =
      ReadRegistryValueData<uint64_t>(gLauncherValue, REG_QWORD);
  if (launcherTs.isErr()) {
    return launcherTs.propagateErr();
  }
  mozilla::LauncherResult<mozilla::Maybe<uint64_t>> browserTs =
      ReadRegistryValueData<uint64_t>(gBrowserValue, REG_QWORD);
  if (browserTs.isErr()) {
    return browserTs.propagateErr();
  }
  if (launcherTs.inspect().isNothing() || browserTs.inspect().isNothing() ||
      browserTs.inspect().value() <= launcherTs.inspect().value()) {
    return LAUNCHER_ERROR_FROM_HRESULT(E_FAIL);
  }

  EXPECT_ENABLED_STATE_IS(EnabledState::Enabled);

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestBrowserNoLauncher() {
  mozilla::LauncherVoidResult vr = SetupEnabledScenario();
  if (vr.isErr()) {
    return vr;
  }
  vr = DeleteRegistryValueData(gLauncherValue);
  if (vr.isErr()) {
    return vr;
  }

  mozilla::LauncherRegistryInfo info;
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Browser);
  EXPECT_COMMIT_IS_OK();

  // Verify that we still don't have a launcher timestamp
  EXPECT_REG_QWORD_DOES_NOT_EXIST(gLauncherValue);
  // Verify that the browser timestamp is now zero
  EXPECT_REG_QWORD_EXISTS_AND_EQ(gBrowserValue, 0ULL);

  EXPECT_ENABLED_STATE_IS(EnabledState::ForceDisabled);

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestLauncherNoBrowser() {
  constexpr uint64_t launcherTs = 0x77777777;
  mozilla::LauncherVoidResult vr = DeleteAllRegstryValues();
  if (vr.isErr()) {
    return vr;
  }
  vr = WriteRegistryValueData<DWORD>(gImageValue, REG_DWORD, gMyImageTimestamp);
  if (vr.isErr()) {
    return vr;
  }
  vr = WriteRegistryValueData<uint64_t>(gLauncherValue, REG_QWORD, launcherTs);
  if (vr.isErr()) {
    return vr;
  }

  mozilla::LauncherRegistryInfo info;
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Browser);
  EXPECT_COMMIT_IS_OK();

  // Launcher's timestamps is kept intact while browser's is set to 0.
  EXPECT_REG_QWORD_EXISTS_AND_EQ(gLauncherValue, launcherTs);
  EXPECT_REG_QWORD_EXISTS_AND_EQ(gBrowserValue, 0ULL);

  EXPECT_ENABLED_STATE_IS(EnabledState::FailDisabled);

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestBrowserLessThanLauncher() {
  constexpr uint64_t launcherTs = 0x77777777, browserTs = 0x66666666;
  mozilla::LauncherVoidResult vr = DeleteAllRegstryValues();
  if (vr.isErr()) {
    return vr;
  }
  vr = WriteRegistryValueData<DWORD>(gImageValue, REG_DWORD, gMyImageTimestamp);
  if (vr.isErr()) {
    return vr;
  }
  vr = WriteRegistryValueData<uint64_t>(gLauncherValue, REG_QWORD, launcherTs);
  if (vr.isErr()) {
    return vr;
  }
  vr = WriteRegistryValueData<uint64_t>(gBrowserValue, REG_QWORD, browserTs);
  if (vr.isErr()) {
    return vr;
  }

  mozilla::LauncherRegistryInfo info;
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Browser);
  EXPECT_COMMIT_IS_OK();

  // Launcher's timestamps is kept intact while browser's is set to 0.
  EXPECT_REG_QWORD_EXISTS_AND_EQ(gLauncherValue, launcherTs);
  EXPECT_REG_QWORD_EXISTS_AND_EQ(gBrowserValue, 0ULL);

  EXPECT_ENABLED_STATE_IS(EnabledState::FailDisabled);

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestImageTimestampChange() {
  // This should reset the timestamps and then essentially run like
  // TestEmptyRegistry
  mozilla::LauncherVoidResult vr = DeleteAllRegstryValues();
  if (vr.isErr()) {
    return vr;
  }
  vr = WriteRegistryValueData<DWORD>(gImageValue, REG_DWORD, 0x12345678);
  if (vr.isErr()) {
    return vr;
  }
  vr = WriteRegistryValueData<uint64_t>(gLauncherValue, REG_QWORD, 1ULL);
  if (vr.isErr()) {
    return vr;
  }
  vr = WriteRegistryValueData<uint64_t>(gBrowserValue, REG_QWORD, 2ULL);
  if (vr.isErr()) {
    return vr;
  }

  mozilla::LauncherRegistryInfo info;
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Launcher);
  EXPECT_COMMIT_IS_OK();

  EXPECT_REG_DWORD_EXISTS_AND_EQ(gImageValue, gMyImageTimestamp);
  EXPECT_REG_QWORD_EXISTS(gLauncherValue);
  EXPECT_REG_QWORD_DOES_NOT_EXIST(gBrowserValue);

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestImageTimestampChangeWhenDisabled() {
  mozilla::LauncherVoidResult vr = DeleteAllRegstryValues();
  if (vr.isErr()) {
    return vr;
  }
  vr = WriteRegistryValueData<DWORD>(gImageValue, REG_DWORD, 0x12345678);
  if (vr.isErr()) {
    return vr;
  }
  vr = WriteRegistryValueData<uint64_t>(gBrowserValue, REG_QWORD, 0ULL);
  if (vr.isErr()) {
    return vr;
  }

  mozilla::LauncherRegistryInfo info;
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Browser);
  EXPECT_COMMIT_IS_OK();

  EXPECT_REG_DWORD_EXISTS_AND_EQ(gImageValue, gMyImageTimestamp);
  EXPECT_REG_QWORD_DOES_NOT_EXIST(gLauncherValue);
  EXPECT_REG_QWORD_EXISTS_AND_EQ(gBrowserValue, 0);

  EXPECT_ENABLED_STATE_IS(EnabledState::ForceDisabled);

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestDisableDueToFailure() {
  mozilla::LauncherVoidResult vr = SetupEnabledScenario();
  if (vr.isErr()) {
    return vr;
  }

  // Check that we are indeed enabled.
  mozilla::LauncherRegistryInfo info;
  EXPECT_ENABLED_STATE_IS(EnabledState::Enabled);

  // Now call DisableDueToFailure
  mozilla::LauncherVoidResult lvr = info.DisableDueToFailure();
  if (lvr.isErr()) {
    return lvr.propagateErr();
  }

  // We should now be FailDisabled
  EXPECT_ENABLED_STATE_IS(EnabledState::FailDisabled);

  // If we delete the launcher timestamp, IsEnabled should then return
  // ForceDisabled.
  vr = DeleteRegistryValueData(gLauncherValue);
  if (vr.isErr()) {
    return vr;
  }
  EXPECT_ENABLED_STATE_IS(EnabledState::ForceDisabled);

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestPrefReflection() {
  // Reset the registry to a known good state.
  mozilla::LauncherVoidResult vr = SetupEnabledScenario();
  if (vr.isErr()) {
    return vr;
  }

  // Let's see what happens when we flip the pref to OFF.
  mozilla::LauncherRegistryInfo info;
  mozilla::LauncherVoidResult reflectOk = info.ReflectPrefToRegistry(false);
  if (reflectOk.isErr()) {
    return reflectOk.propagateErr();
  }

  // Launcher timestamp should be non-existent.
  EXPECT_REG_QWORD_DOES_NOT_EXIST(gLauncherValue);
  // Browser timestamp should be zero
  EXPECT_REG_QWORD_EXISTS_AND_EQ(gBrowserValue, 0ULL);
  // IsEnabled should give us ForceDisabled
  EXPECT_ENABLED_STATE_IS(EnabledState::ForceDisabled);

  // Now test to see what happens when the pref is set to ON.
  reflectOk = info.ReflectPrefToRegistry(true);
  if (reflectOk.isErr()) {
    return reflectOk.propagateErr();
  }

  // Launcher and browser timestamps should be non-existent.
  EXPECT_REG_QWORD_DOES_NOT_EXIST(gLauncherValue);
  EXPECT_REG_QWORD_DOES_NOT_EXIST(gBrowserValue);

  // IsEnabled should give us Enabled.
  EXPECT_ENABLED_STATE_IS(EnabledState::Enabled);

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestTelemetryConfig() {
  mozilla::LauncherVoidResult vr = DeleteAllRegstryValues();
  if (vr.isErr()) {
    return vr;
  }

  mozilla::LauncherRegistryInfo info;
  EXPECT_TELEMETRY_IS_ENABLED(false);

  mozilla::LauncherVoidResult reflectOk =
      info.ReflectTelemetryPrefToRegistry(false);
  if (reflectOk.isErr()) {
    return reflectOk.propagateErr();
  }
  EXPECT_TELEMETRY_IS_ENABLED(false);

  reflectOk = info.ReflectTelemetryPrefToRegistry(true);
  if (reflectOk.isErr()) {
    return reflectOk.propagateErr();
  }
  EXPECT_TELEMETRY_IS_ENABLED(true);

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestCommitAbort() {
  mozilla::LauncherVoidResult vr = SetupEnabledScenario();
  if (vr.isErr()) {
    return vr;
  }

  // Retrieve the current timestamps to compare later
  mozilla::LauncherResult<mozilla::Maybe<uint64_t>> launcherValue =
      ReadRegistryValueData<uint64_t>(gLauncherValue, REG_QWORD);
  if (launcherValue.isErr() || launcherValue.inspect().isNothing()) {
    return launcherValue.propagateErr();
  }
  mozilla::LauncherResult<mozilla::Maybe<uint64_t>> browserValue =
      ReadRegistryValueData<uint64_t>(gBrowserValue, REG_QWORD);
  if (browserValue.isErr() || browserValue.inspect().isNothing()) {
    return browserValue.propagateErr();
  }
  uint64_t launcherTs = launcherValue.inspect().value();
  uint64_t browserTs = browserValue.inspect().value();

  vr = []() -> mozilla::LauncherVoidResult {
    mozilla::LauncherRegistryInfo info;
    EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Launcher);
    // No commit
    return mozilla::Ok();
  }();
  if (vr.isErr()) {
    return vr;
  }

  // Exiting the scope discards the change.
  mozilla::LauncherRegistryInfo info;
  EXPECT_REG_DWORD_EXISTS_AND_EQ(gImageValue, gMyImageTimestamp);
  EXPECT_REG_QWORD_EXISTS_AND_EQ(gLauncherValue, launcherTs);
  EXPECT_REG_QWORD_EXISTS_AND_EQ(gBrowserValue, browserTs);

  // Commit -> Check -> Abort -> Commit
  EXPECT_COMMIT_IS_OK();
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Launcher);
  info.Abort();
  EXPECT_COMMIT_IS_OK();

  // Nothing is changed.
  EXPECT_REG_DWORD_EXISTS_AND_EQ(gImageValue, gMyImageTimestamp);
  EXPECT_REG_QWORD_EXISTS_AND_EQ(gLauncherValue, launcherTs);
  EXPECT_REG_QWORD_EXISTS_AND_EQ(gBrowserValue, browserTs);
  EXPECT_ENABLED_STATE_IS(EnabledState::Enabled);

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestDisableDuringLauncherLaunch() {
  mozilla::LauncherVoidResult vr = SetupEnabledScenario();
  if (vr.isErr()) {
    return vr;
  }

  mozilla::LauncherResult<mozilla::Maybe<uint64_t>> launcherTs =
      ReadRegistryValueData<uint64_t>(gLauncherValue, REG_QWORD);
  if (launcherTs.isErr()) {
    return launcherTs.propagateErr();
  }
  if (launcherTs.inspect().isNothing()) {
    return LAUNCHER_ERROR_FROM_HRESULT(E_UNEXPECTED);
  }

  vr = []() -> mozilla::LauncherVoidResult {
    mozilla::LauncherRegistryInfo info;
    EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Launcher);

    // Call DisableDueToFailure with a different instance
    mozilla::LauncherVoidResult vr = []() -> mozilla::LauncherVoidResult {
      mozilla::LauncherRegistryInfo info;
      mozilla::LauncherVoidResult vr = info.DisableDueToFailure();
      if (vr.isErr()) {
        return vr.propagateErr();
      }
      return mozilla::Ok();
    }();
    if (vr.isErr()) {
      return vr;
    }

    // Commit after disable.
    EXPECT_COMMIT_IS_OK();

    return mozilla::Ok();
  }();
  if (vr.isErr()) {
    return vr;
  }

  // Make sure we're still FailDisabled and the launcher's timestamp is not
  // updated
  mozilla::LauncherRegistryInfo info;
  EXPECT_ENABLED_STATE_IS(EnabledState::FailDisabled);
  EXPECT_REG_QWORD_EXISTS_AND_EQ(gLauncherValue, launcherTs.inspect().value());

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestDisableDuringBrowserLaunch() {
  mozilla::LauncherVoidResult vr = SetupEnabledScenario();
  if (vr.isErr()) {
    return vr;
  }

  mozilla::LauncherRegistryInfo info;
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Launcher);
  EXPECT_COMMIT_IS_OK();

  vr = []() -> mozilla::LauncherVoidResult {
    mozilla::LauncherRegistryInfo info;
    EXPECT_CHECK_RESULT_IS(ProcessType::Browser, ProcessType::Browser);

    // Call DisableDueToFailure with a different instance
    mozilla::LauncherVoidResult vr = []() -> mozilla::LauncherVoidResult {
      mozilla::LauncherRegistryInfo info;
      mozilla::LauncherVoidResult vr = info.DisableDueToFailure();
      if (vr.isErr()) {
        return vr.propagateErr();
      }
      return mozilla::Ok();
    }();
    if (vr.isErr()) {
      return vr;
    }

    // Commit after disable.
    EXPECT_COMMIT_IS_OK();

    return mozilla::Ok();
  }();
  if (vr.isErr()) {
    return vr;
  }

  // Make sure we're still FailDisabled
  EXPECT_ENABLED_STATE_IS(EnabledState::FailDisabled);

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestReEnable() {
  mozilla::LauncherVoidResult vr = SetupEnabledScenario();
  if (vr.isErr()) {
    return vr;
  }

  // Make FailDisabled
  mozilla::LauncherRegistryInfo info;
  vr = info.DisableDueToFailure();
  if (vr.isErr()) {
    return vr.propagateErr();
  }
  EXPECT_ENABLED_STATE_IS(EnabledState::FailDisabled);

  // Attempt to launch when FailDisabled: Still be FailDisabled
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Browser);
  EXPECT_COMMIT_IS_OK();
  EXPECT_ENABLED_STATE_IS(EnabledState::FailDisabled);

  // Change the timestamp
  vr = WriteRegistryValueData<DWORD>(gImageValue, REG_DWORD, 0x12345678);
  if (vr.isErr()) {
    return vr;
  }

  // Attempt to launch again: Launcher comes back
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Launcher);
  EXPECT_COMMIT_IS_OK();

  // Make ForceDisabled
  vr = info.ReflectPrefToRegistry(false);
  if (vr.isErr()) {
    return vr.propagateErr();
  }
  EXPECT_ENABLED_STATE_IS(EnabledState::ForceDisabled);

  // Attempt to launch when ForceDisabled: Still be ForceDisabled
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Browser);
  EXPECT_COMMIT_IS_OK();
  EXPECT_ENABLED_STATE_IS(EnabledState::ForceDisabled);

  // Change the timestamp
  vr = WriteRegistryValueData<DWORD>(gImageValue, REG_DWORD, 0x12345678);
  if (vr.isErr()) {
    return vr;
  }

  // Attempt to launch again: Still be ForceDisabled
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Browser);
  EXPECT_COMMIT_IS_OK();
  EXPECT_ENABLED_STATE_IS(EnabledState::ForceDisabled);

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestCrashTimeoutReenabling() {
  // First disable due to crash
  mozilla::LauncherVoidResult vr = SetupEnabledScenario();
  if (vr.isErr()) {
    return vr;
  }

  mozilla::LauncherRegistryInfo info;
  vr = info.DisableDueToFailure();
  if (vr.isErr()) {
    return vr.propagateErr();
  }

  // Verify we're FailDisabled initially
  EXPECT_ENABLED_STATE_IS(EnabledState::FailDisabled);

  // Delete launcher timestamp to make it force-disabled
  vr = DeleteRegistryValueData(gLauncherValue);
  if (vr.isErr()) {
    return vr;
  }
  EXPECT_ENABLED_STATE_IS(EnabledState::ForceDisabled);

  EXPECT_REG_QWORD_EXISTS(gCrashTimestampValue);

  // Test that launcher process is still disabled when trying to launch
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Browser);
  EXPECT_COMMIT_IS_OK();
  EXPECT_ENABLED_STATE_IS(EnabledState::ForceDisabled);

  // Now simulate 45+ days have passed by setting crash timestamp to an old time
  // Get current time
  FILETIME currentTime;
  ::GetSystemTimeAsFileTime(&currentTime);
  uint64_t currentTimestamp =
      (static_cast<uint64_t>(currentTime.dwHighDateTime) << 32) |
      currentTime.dwLowDateTime;

  // 46 days ago in 100-nanosecond intervals
  constexpr uint64_t kTimeoutInterval = 46ULL * 24 * 60 * 60 * 10000000ULL;
  uint64_t oldCrashTime = currentTimestamp - kTimeoutInterval;

  // Set the old crash time
  vr = WriteRegistryValueData<uint64_t>(gCrashTimestampValue, REG_QWORD,
                                        oldCrashTime);
  if (vr.isErr()) {
    return vr;
  }

  // Now attempt to launch - should re-enable launcher process
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Launcher);
  EXPECT_COMMIT_IS_OK();

  EXPECT_REG_QWORD_DOES_NOT_EXIST(gCrashTimestampValue);

  // Verify browser timestamp was cleared (not 0)
  EXPECT_REG_QWORD_DOES_NOT_EXIST(gBrowserValue);

  // Verify launcher timestamp exists (indicating successful launch attempt)
  EXPECT_REG_QWORD_EXISTS(gLauncherValue);

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestCrashTimeoutNotExpired() {
  // First disable due to crash
  mozilla::LauncherVoidResult vr = SetupEnabledScenario();
  if (vr.isErr()) {
    return vr;
  }

  mozilla::LauncherRegistryInfo info;
  vr = info.DisableDueToFailure();
  if (vr.isErr()) {
    return vr.propagateErr();
  }

  // Delete launcher timestamp to make it force-disabled
  vr = DeleteRegistryValueData(gLauncherValue);
  if (vr.isErr()) {
    return vr;
  }
  EXPECT_ENABLED_STATE_IS(EnabledState::ForceDisabled);

  // Simulate only 30 days have passed (less than 45)
  FILETIME currentTime;
  ::GetSystemTimeAsFileTime(&currentTime);
  uint64_t currentTimestamp =
      (static_cast<uint64_t>(currentTime.dwHighDateTime) << 32) |
      currentTime.dwLowDateTime;

  // 30 days ago in 100-nanosecond intervals
  constexpr uint64_t kPartialTimeoutInterval =
      30ULL * 24 * 60 * 60 * 10000000ULL;
  uint64_t recentCrashTime = currentTimestamp - kPartialTimeoutInterval;

  vr = WriteRegistryValueData<uint64_t>(gCrashTimestampValue, REG_QWORD,
                                        recentCrashTime);
  if (vr.isErr()) {
    return vr;
  }

  // Attempt to launch - should still be disabled
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Browser);
  EXPECT_COMMIT_IS_OK();
  EXPECT_ENABLED_STATE_IS(EnabledState::ForceDisabled);

  // Verify crash timestamp still exists (wasn't cleared)
  EXPECT_REG_QWORD_EXISTS_AND_EQ(gCrashTimestampValue, recentCrashTime);

  // Verify browser timestamp is still 0 (force-disabled)
  EXPECT_REG_QWORD_EXISTS_AND_EQ(gBrowserValue, 0ULL);

  return mozilla::Ok();
}

static mozilla::LauncherVoidResult TestCrashTimeoutInFuture() {
  // First disable due to crash
  mozilla::LauncherVoidResult vr = SetupEnabledScenario();
  if (vr.isErr()) {
    return vr;
  }

  mozilla::LauncherRegistryInfo info;
  vr = info.DisableDueToFailure();
  if (vr.isErr()) {
    return vr.propagateErr();
  }

  // Delete launcher timestamp to make it force-disabled
  vr = DeleteRegistryValueData(gLauncherValue);
  if (vr.isErr()) {
    return vr;
  }
  EXPECT_ENABLED_STATE_IS(EnabledState::ForceDisabled);

  // Simulate a crash "in the future"
  FILETIME currentTime;
  ::GetSystemTimeAsFileTime(&currentTime);
  uint64_t currentTimestamp =
      (static_cast<uint64_t>(currentTime.dwHighDateTime) << 32) |
      currentTime.dwLowDateTime;

  // 30 days ago in 100-nanosecond intervals
  constexpr uint64_t kPartialTimeoutInterval =
      30ULL * 24 * 60 * 60 * 10000000ULL;
  uint64_t recentCrashTime = currentTimestamp + kPartialTimeoutInterval;

  vr = WriteRegistryValueData<uint64_t>(gCrashTimestampValue, REG_QWORD,
                                        recentCrashTime);
  if (vr.isErr()) {
    return vr;
  }

  // Attempt to launch - we should re-enable
  EXPECT_CHECK_RESULT_IS(ProcessType::Launcher, ProcessType::Launcher);
  EXPECT_COMMIT_IS_OK();

  EXPECT_REG_QWORD_DOES_NOT_EXIST(gCrashTimestampValue);

  // Verify browser timestamp was cleared (not 0)
  EXPECT_REG_QWORD_DOES_NOT_EXIST(gBrowserValue);

  // Verify launcher timestamp exists (indicating successful launch attempt)
  EXPECT_REG_QWORD_EXISTS(gLauncherValue);

  return mozilla::Ok();
}

int wmain(int argc, wchar_t* argv[]) {
  auto fullPath = mozilla::GetFullBinaryPath();
  if (!fullPath) {
    return 1;
  }

  // Global setup for all tests
  gBrowserValue = fullPath.get();
  gBrowserValue += kBrowserSuffix;

  gLauncherValue = fullPath.get();
  gLauncherValue += kLauncherSuffix;

  gImageValue = fullPath.get();
  gImageValue += kImageSuffix;

  gTelemetryValue = fullPath.get();
  gTelemetryValue += kTelemetrySuffix;

  gCrashTimestampValue = fullPath.get();
  gCrashTimestampValue += kCrashTimestampSuffix;

  mozilla::LauncherResult<DWORD> timestamp = 0;
  RUN_TEST(timestamp, GetCurrentImageTimestamp);
  gMyImageTimestamp = timestamp.unwrap();

  auto onExit =
      mozilla::MakeScopeExit([]() { (void)DeleteAllRegstryValues(); });

  mozilla::LauncherVoidResult vr = mozilla::Ok();

  // All testcases should call SetupEnabledScenario() or
  // DeleteAllRegstryValues() to be order-independent
  RUN_TEST(vr, TestEmptyRegistry);
  RUN_TEST(vr, TestNormal);
  RUN_TEST(vr, TestBrowserNoLauncher);
  RUN_TEST(vr, TestLauncherNoBrowser);
  RUN_TEST(vr, TestBrowserLessThanLauncher);
  RUN_TEST(vr, TestImageTimestampChange);
  RUN_TEST(vr, TestImageTimestampChangeWhenDisabled);
  RUN_TEST(vr, TestDisableDueToFailure);
  RUN_TEST(vr, TestPrefReflection);
  RUN_TEST(vr, TestTelemetryConfig);
  RUN_TEST(vr, TestCommitAbort);
  RUN_TEST(vr, TestDisableDuringLauncherLaunch);
  RUN_TEST(vr, TestDisableDuringBrowserLaunch);
  RUN_TEST(vr, TestReEnable);
  RUN_TEST(vr, TestCrashTimeoutReenabling);
  RUN_TEST(vr, TestCrashTimeoutNotExpired);
  RUN_TEST(vr, TestCrashTimeoutInFuture);

  return 0;
}
