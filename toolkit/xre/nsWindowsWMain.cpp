/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsWindowsWMain_cpp
#define nsWindowsWMain_cpp

// This file is a .cpp file meant to be included in nsBrowserApp.cpp and other
// similar bootstrap code. It converts wide-character windows wmain into UTF-8
// narrow-character strings.

#ifndef XP_WIN
#  error This file only makes sense on Windows.
#endif

#include "mozilla/Char16.h"
#include "nsUTF8Utils.h"

#include <windows.h>

#define main NS_internal_main

#ifndef XRE_WANT_ENVIRON
int main(int argc, char** argv);
#else
int main(int argc, char** argv, char** envp);
#endif

// SanitizeEnvironmentVariables()
//
// Mitigate CVE-2007-6753 (binary planting via unexpanded environment variable
// references) by forcibly expanding all environment variable references in
// %PATH%.
//
// CVE-2007-6753 is documented to have affected all active mainline Windows
// versions at the time of its announcement (i.e., up to Windows 7). Microsoft
// has never formally stated that later versions of Windows are free of this
// issue; out of an abundance of caution, we continue to mitigate it on Windows
// 8 and beyond.
static void SanitizeEnvironmentVariables() {
  DWORD bufferSize = GetEnvironmentVariableW(L"PATH", nullptr, 0);
  if (bufferSize) {
    wchar_t* originalPath = new wchar_t[bufferSize];
    if (bufferSize - 1 ==
        GetEnvironmentVariableW(L"PATH", originalPath, bufferSize)) {
      bufferSize = ExpandEnvironmentStringsW(originalPath, nullptr, 0);
      // The maximum length of a Windows environment variable and the maximum
      // length of a string returned by ExpandEnvironmentStringsW are both
      // documented to be 32,767 characters. Under some versions of Windows,
      // however, both may be longer, with delayed but deleterious consequences.
      //
      // We therefore cap the size manually, in case the user has a sufficiently
      // problematic %PATH%. (See bug 1753910.) This is unlikely to occur unless
      // there is some form of recursive referencing involved, in which case
      // expansion is no mitigation anyway.
      if (bufferSize && bufferSize < 32768) {
        wchar_t* newPath = new wchar_t[bufferSize];
        if (ExpandEnvironmentStringsW(originalPath, newPath, bufferSize)) {
          SetEnvironmentVariableW(L"PATH", newPath);
        }
        delete[] newPath;
      }
    }
    delete[] originalPath;
  }
}

static char* AllocConvertUTF16toUTF8(char16ptr_t arg) {
  // be generous... UTF16 units can expand up to 3 UTF8 units
  size_t len = wcslen(arg);
  // ConvertUTF16toUTF8 requires +1. Let's do that here, too, lacking
  // knowledge of Windows internals.
  size_t dstLen = len * 3 + 1;
  char* s = new char[dstLen + 1];  // Another +1 for zero terminator
  if (!s) return nullptr;

  int written =
      ::WideCharToMultiByte(CP_UTF8, 0, arg, len, s, dstLen, nullptr, nullptr);
  s[written] = 0;
  return s;
}

static void FreeAllocStrings(int argc, char** argv) {
  while (argc) {
    --argc;
    delete[] argv[argc];
  }

  delete[] argv;
}

int wmain(int argc, WCHAR** argv) {
  SanitizeEnvironmentVariables();
  SetDllDirectoryW(L"");

  // Only run this code if LauncherProcessWin.h was included beforehand, thus
  // signalling that the hosting process should support launcher mode.
#if defined(mozilla_LauncherProcessWin_h)
  mozilla::Maybe<int> launcherResult = mozilla::LauncherMain(argc, argv);
  if (launcherResult) {
    return launcherResult.value();
  }
#endif  // defined(mozilla_LauncherProcessWin_h)

  char** argvConverted = new char*[argc + 1];
  if (!argvConverted) return 127;

  for (int i = 0; i < argc; ++i) {
    argvConverted[i] = AllocConvertUTF16toUTF8(argv[i]);
    if (!argvConverted[i]) {
      return 127;
    }
  }
  argvConverted[argc] = nullptr;

  // need to save argvConverted copy for later deletion.
  char** deleteUs = new char*[argc + 1];
  if (!deleteUs) {
    FreeAllocStrings(argc, argvConverted);
    return 127;
  }
  for (int i = 0; i < argc; i++) deleteUs[i] = argvConverted[i];
#ifndef XRE_WANT_ENVIRON
  int result = main(argc, argvConverted);
#else
  // Force creation of the multibyte _environ variable.
  getenv("PATH");
  int result = main(argc, argvConverted, _environ);
#endif

  delete[] argvConverted;
  FreeAllocStrings(argc, deleteUs);

  return result;
}

#endif  // nsWindowsWMain_cpp
