/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
// Support for _setmode
#include <fcntl.h>
#include <io.h>

#include "nsWindowsRestart.cpp"

// CommandLineToArgvW may return different values for argv[0] since it contains
// the path to the binary that was executed so we prepend an argument that is
// quoted with a space to prevent argv[1] being appended to argv[0].
#define DUMMY_ARG1 L"\"arg 1\" "

#ifndef MAXPATHLEN
#  ifdef PATH_MAX
#    define MAXPATHLEN PATH_MAX
#  elif defined(MAX_PATH)
#    define MAXPATHLEN MAX_PATH
#  elif defined(_MAX_PATH)
#    define MAXPATHLEN _MAX_PATH
#  elif defined(CCHMAXPATH)
#    define MAXPATHLEN CCHMAXPATH
#  else
#    define MAXPATHLEN 1024
#  endif
#endif

#define TEST_NAME L"XRE MakeCommandLine"
#define MAX_TESTS 100

// Verbose output can be enabled by defining VERBOSE 1
#define VERBOSE 0

// Compares compareCmdLine with the output of MakeCommandLine. This is
// accomplished by converting inCmdLine to an argument list with
// CommandLineToArgvW and converting it back to a command line with
// MakeCommandLine.
static int verifyCmdLineCreation(wchar_t* inCmdLine, wchar_t* compareCmdLine,
                                 bool passes, int testNum) {
  int rv = 0;
  int i;
  int inArgc;
  int outArgc;
  bool isEqual;

  // When debugging with command lines containing Unicode characters greater
  // than 255 you can set the mode for stdout to Unicode so the console will
  // receive the correct characters though it won't display them properly unless
  // the console's font has been set to one that can display the characters. You
  // can also redirect the console output to a file that has been saved as
  // Unicode to view the characters.
  //  _setmode(_fileno(stdout), _O_WTEXT);

  // Prepend an additional argument to the command line. CommandLineToArgvW
  // handles argv[0] differently than other arguments since argv[0] is the path
  // to the binary being executed and MakeCommandLine only handles argv[1] and
  // larger.
  wchar_t* inCmdLineNew = (wchar_t*)malloc(
      (wcslen(DUMMY_ARG1) + wcslen(inCmdLine) + 1) * sizeof(wchar_t));
  wcscpy(inCmdLineNew, DUMMY_ARG1);
  wcscat(inCmdLineNew, inCmdLine);
  LPWSTR* inArgv = CommandLineToArgvW(inCmdLineNew, &inArgc);

  auto outCmdLine = mozilla::MakeCommandLine(inArgc - 1, inArgv + 1);
  wchar_t* outCmdLineNew = (wchar_t*)malloc(
      (wcslen(DUMMY_ARG1) + wcslen(outCmdLine.get()) + 1) * sizeof(wchar_t));
  wcscpy(outCmdLineNew, DUMMY_ARG1);
  wcscat(outCmdLineNew, outCmdLine.get());
  LPWSTR* outArgv = CommandLineToArgvW(outCmdLineNew, &outArgc);

  if (VERBOSE) {
    wprintf(L"\n");
    wprintf(L"Verbose Output\n");
    wprintf(L"--------------\n");
    wprintf(L"Input command line   : >%s<\n", inCmdLine);
    wprintf(L"MakeComandLine output: >%s<\n", outCmdLine.get());
    wprintf(L"Expected command line: >%s<\n", compareCmdLine);

    wprintf(L"input argc : %d\n", inArgc - 1);
    wprintf(L"output argc: %d\n", outArgc - 1);

    for (i = 1; i < inArgc; ++i) {
      wprintf(L"input argv[%d] : >%s<\n", i - 1, inArgv[i]);
    }

    for (i = 1; i < outArgc; ++i) {
      wprintf(L"output argv[%d]: >%s<\n", i - 1, outArgv[i]);
    }
    wprintf(L"\n");
  }

  isEqual = (inArgc == outArgc);
  if (!isEqual) {
    wprintf(L"TEST-%s-FAIL | %s | ARGC Comparison (check %2d)\n",
            passes ? L"UNEXPECTED" : L"KNOWN", TEST_NAME, testNum);
    if (passes) {
      rv = 1;
    }
    LocalFree(inArgv);
    LocalFree(outArgv);
    free(inCmdLineNew);
    free(outCmdLineNew);
    return rv;
  }

  for (i = 1; i < inArgc; ++i) {
    isEqual = (wcscmp(inArgv[i], outArgv[i]) == 0);
    if (!isEqual) {
      wprintf(L"TEST-%s-FAIL | %s | ARGV Comparison (check %2d)\n",
              passes ? L"UNEXPECTED" : L"KNOWN", TEST_NAME, testNum);
      if (passes) {
        rv = 1;
      }
      LocalFree(inArgv);
      LocalFree(outArgv);
      free(inCmdLineNew);
      free(outCmdLineNew);
      return rv;
    }
  }

  isEqual = (wcscmp(outCmdLine.get(), compareCmdLine) == 0);
  if (!isEqual) {
    wprintf(L"TEST-%s-FAIL | %s | Command Line Comparison (check %2d)\n",
            passes ? L"UNEXPECTED" : L"KNOWN", TEST_NAME, testNum);
    if (passes) {
      rv = 1;
    }
    LocalFree(inArgv);
    LocalFree(outArgv);
    free(inCmdLineNew);
    free(outCmdLineNew);
    return rv;
  }

  if (rv == 0) {
    if (passes) {
      wprintf(L"TEST-PASS | %s | check %2d\n", TEST_NAME, testNum);
    } else {
      wprintf(L"TEST-UNEXPECTED-PASS | %s | check %2d\n", TEST_NAME, testNum);
      rv = 1;
    }
  }

  LocalFree(inArgv);
  LocalFree(outArgv);
  free(inCmdLineNew);
  free(outCmdLineNew);
  return rv;
}

int wmain(int argc, wchar_t* argv[]) {
  int i;
  int rv = 0;

  if (argc > 1 && (_wcsicmp(argv[1], L"-check-one") != 0 || argc != 3)) {
    fwprintf(stderr,
             L"Displays and validates output from MakeCommandLine.\n\n");
    fwprintf(stderr, L"Usage: %s -check-one <test number>\n\n", argv[0]);
    fwprintf(stderr,
             L"  <test number>\tSpecifies the test number to run from the\n");
    fwprintf(stderr, L"\t\tTestXREMakeCommandLineWin.ini file.\n");
    return 255;
  }

  wchar_t inifile[MAXPATHLEN];
  if (!::GetModuleFileNameW(0, inifile, MAXPATHLEN)) {
    wprintf(L"TEST-UNEXPECTED-FAIL | %s | GetModuleFileNameW\n", TEST_NAME);
    return 2;
  }

  WCHAR* slash = wcsrchr(inifile, '\\');
  if (!slash) {
    wprintf(L"TEST-UNEXPECTED-FAIL | %s | wcsrchr\n", TEST_NAME);
    return 3;
  }

  wcscpy(slash + 1, L"TestXREMakeCommandLineWin.ini\0");

  for (i = 0; i < MAX_TESTS; ++i) {
    wchar_t sInputVal[MAXPATHLEN];
    wchar_t sOutputVal[MAXPATHLEN];
    wchar_t sPassesVal[MAXPATHLEN];
    wchar_t sInputKey[MAXPATHLEN];
    wchar_t sOutputKey[MAXPATHLEN];
    wchar_t sPassesKey[MAXPATHLEN];

    if (argc > 2 && _wcsicmp(argv[1], L"-check-one") == 0 && argc == 3) {
      i = _wtoi(argv[2]);
    }

    _snwprintf(sInputKey, MAXPATHLEN, L"input_%d", i);
    _snwprintf(sOutputKey, MAXPATHLEN, L"output_%d", i);
    _snwprintf(sPassesKey, MAXPATHLEN, L"passes_%d", i);

    if (!GetPrivateProfileStringW(L"MakeCommandLineTests", sInputKey, nullptr,
                                  sInputVal, MAXPATHLEN, inifile)) {
      if (i == 0 || (argc > 2 && _wcsicmp(argv[1], L"-check-one") == 0)) {
        wprintf(L"TEST-UNEXPECTED-FAIL | %s | see following explanation:\n",
                TEST_NAME);
        wprintf(
            L"ERROR: Either the TestXREMakeCommandLineWin.ini file doesn't "
            L"exist\n");
        if (argc > 1 && _wcsicmp(argv[1], L"-check-one") == 0 && argc == 3) {
          wprintf(
              L"ERROR: or the test is not defined in the MakeCommandLineTests "
              L"section.\n");
        } else {
          wprintf(
              L"ERROR: or it has no tests defined in the MakeCommandLineTests "
              L"section.\n");
        }
        wprintf(L"ERROR: File: %s\n", inifile);
        return 4;
      }
      break;
    }

    GetPrivateProfileStringW(L"MakeCommandLineTests", sOutputKey, nullptr,
                             sOutputVal, MAXPATHLEN, inifile);
    GetPrivateProfileStringW(L"MakeCommandLineTests", sPassesKey, nullptr,
                             sPassesVal, MAXPATHLEN, inifile);

    rv |= verifyCmdLineCreation(
        sInputVal, sOutputVal,
        (_wcsicmp(sPassesVal, L"false") == 0) ? FALSE : TRUE, i);

    if (argc > 2 && _wcsicmp(argv[1], L"-check-one") == 0) {
      break;
    }
  }

  if (rv == 0) {
    wprintf(L"TEST-PASS | %s | all checks passed\n", TEST_NAME);
  } else {
    wprintf(L"TEST-UNEXPECTED-FAIL | %s | some checks failed\n", TEST_NAME);
  }

  return rv;
}
