/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RuntimeExceptionModule.h"

#include "mozilla/ProcessType.h"
#include <stdlib.h>

#if defined(XP_WIN)
#  include <windows.h>
#  include <werapi.h>  // For WerRegisterRuntimeExceptionModule()
#  include <stdlib.h>
#endif

namespace CrashReporter {

#ifdef XP_WIN

const static size_t kModulePathLength = MAX_PATH + 1;
static wchar_t sModulePath[kModulePathLength];

bool GetRuntimeExceptionModulePath(wchar_t* aPath, const size_t aLength) {
  const wchar_t* kModuleName = L"mozwer.dll";
  DWORD res = ::GetModuleFileNameW(nullptr, aPath, aLength);
  if ((res > 0) && (res != aLength)) {
    wchar_t* last_backslash = wcsrchr(aPath, L'\\');
    if (last_backslash) {
      *(last_backslash + 1) = L'\0';
      if (wcscat_s(aPath, aLength, kModuleName) == 0) {
        return true;
      }
    }
  }

  return false;
}

#endif  // XP_WIN

void RegisterRuntimeExceptionModule() {
#ifdef XP_WIN
  if (!CrashReporterIsEnabled()) {
    return;
  }

  // If sModulePath is set we have already registerd the module.
  if (*sModulePath) {
    return;
  }

  // If we fail to get the path just return.
  if (!GetRuntimeExceptionModulePath(sModulePath, kModulePathLength)) {
    return;
  }

  if (FAILED(::WerRegisterRuntimeExceptionModule(
          sModulePath,
          reinterpret_cast<PVOID>(mozilla::GetGeckoProcessType())))) {
    // The registration failed null out sModulePath to record this.
    *sModulePath = L'\0';
    return;
  }
#endif  // XP_WIN
}

void UnregisterRuntimeExceptionModule() {
#ifdef XP_WIN
  // If sModulePath is set then we have registered the module.
  if (*sModulePath) {
    (void)::WerUnregisterRuntimeExceptionModule(
        sModulePath, reinterpret_cast<PVOID>(mozilla::GetGeckoProcessType()));
    *sModulePath = L'\0';
  }
#endif  // XP_WIN
}

bool CrashReporterIsEnabled(bool force) {
  if (force) {
    return true;
  }
#if defined(DEBUG)
  // In debug builds, disable the crash reporter by default, and allow to
  // enable it with the MOZ_CRASHREPORTER environment variable.
  const char* envvar = getenv("MOZ_CRASHREPORTER");
  return envvar && *envvar;
#else
  // In other builds, enable the crash reporter by default, and allow
  // disabling it with the MOZ_CRASHREPORTER_DISABLE environment variable.
  const char* envvar = getenv("MOZ_CRASHREPORTER_DISABLE");
  return !envvar || !*envvar;
#endif
}

}  // namespace CrashReporter
