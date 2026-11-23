/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Windows only app to show a modal debug dialog - launched by nsDebug.cpp */
#include <stdlib.h>
#include <strsafe.h>
#include <windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpszCmdLine, int nCmdShow) {
  /* support for auto answering based on words in the assertion.
   * the assertion message is sent as a series of arguements (words) to the
   * commandline. set a "word" to 0xffffffff to let the word not affect this
   * code. set a "word" to 0xfffffffe to show the dialog. set a "word" to 0x5 to
   * ignore (program should continue). set a "word" to 0x4 to retry (should fall
   * into debugger). set a "word" to 0x3 to abort (die).
   */
  DWORD regType;
  DWORD regValue = -1;
  DWORD regLength = sizeof regValue;
  HKEY hkeyCU, hkeyLM;
  RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\mozilla.org\\windbgdlg", 0,
                KEY_READ, &hkeyCU);
  RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\mozilla.org\\windbgdlg", 0,
                KEY_READ, &hkeyLM);
  for (int i = __argc - 1; regValue == (DWORD)-1 && i; --i) {
    bool ok = false;
    if (hkeyCU)
      ok = RegQueryValueExW(hkeyCU, __wargv[i], 0, &regType, (LPBYTE)&regValue,
                            &regLength) == ERROR_SUCCESS;
    if (!ok && hkeyLM)
      ok = RegQueryValueExW(hkeyLM, __wargv[i], 0, &regType, (LPBYTE)&regValue,
                            &regLength) == ERROR_SUCCESS;
    if (!ok) regValue = -1;
  }
  if (hkeyCU) RegCloseKey(hkeyCU);
  if (hkeyLM) RegCloseKey(hkeyLM);
  if (regValue != (DWORD)-1 && regValue != (DWORD)-2) return regValue;
  static const int size = 4096;
  static WCHAR msg[size];

  StringCchPrintfW(msg, size,
                   L"%s\n\nClick Abort to exit the Application.\n"
                   L"Click Retry to Debug the Application.\n"
                   L"Click Ignore to continue running the Application.",
                   lpszCmdLine);
  msg[size - 1] = L'\0';
  return MessageBoxW(
      nullptr, msg, L"NSGlue_Assertion",
      MB_ICONSTOP | MB_SYSTEMMODAL | MB_ABORTRETRYIGNORE | MB_DEFBUTTON3);
}
