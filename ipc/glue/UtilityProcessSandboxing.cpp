/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "UtilityProcessSandboxing.h"

#include <vector>
#include <string>

#include "prenv.h"

namespace mozilla::ipc {

std::vector<std::string> split(const std::string& str, char s) {
  std::vector<std::string> rv;
  size_t last = 0;
  size_t i;
  size_t c = str.size();
  for (i = 0; i <= c; ++i) {
    if (i == c || str[i] == s) {
      rv.push_back(str.substr(last, i - last));
      last = i + 1;
    }
  }
  return rv;
}

bool IsUtilitySandboxEnabled(const char* envVar, SandboxingKind aKind) {
#ifdef XP_WIN
  // Sandboxing the Windows file dialog is probably not useful.
  //
  // (Additionally, it causes failures in our test environments: when running
  // tests on windows-11-2009-qr machines, sandboxed child processes can't see
  // or interact with any other process's windows -- which means they can't
  // select a window from the parent process as the file dialog's parent. This
  // occurs regardless of the sandbox preferences, which is why we disable
  // sandboxing entirely rather than use a maximally permissive preference-set.
  // This behavior has not been seen in user-facing environments.)
  if (aKind == SandboxingKind::WINDOWS_FILE_DIALOG) {
    return false;
  }
#endif

#if !defined(MOZ_NO_SMART_CARDS)
  // For now, don't enable sandboxing for the pkcs11 module loader process.
  // pkcs11 modules have historically been loaded in the parent process and may
  // have sandboxing-compatibility problems.
  if (aKind == SandboxingKind::PKCS11_MODULE) {
    return false;
  }
#endif  // !MOZ_NO_SMART_CARDS

  if (envVar == nullptr) {
    return true;
  }

  const std::string disableUtility(envVar);
  if (disableUtility == "1") {
    return false;
  }

  std::vector<std::string> components = split(disableUtility, ',');
  const std::string thisKind = "utility:" + std::to_string(aKind);
  for (const std::string& thisOne : components) {
    if (thisOne == thisKind) {
      return false;
    }
  }

  return true;
}

bool IsUtilitySandboxEnabled(SandboxingKind aKind) {
  return IsUtilitySandboxEnabled(PR_GetEnv("MOZ_DISABLE_UTILITY_SANDBOX"),
                                 aKind);
}

}  // namespace mozilla::ipc
