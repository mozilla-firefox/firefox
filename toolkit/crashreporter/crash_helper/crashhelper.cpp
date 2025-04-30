/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <cstdlib>

#if defined(XP_WIN)
#  include <windows.h>  // for HANDLE
#endif                  // defined(XP_WIN)

#if defined(XP_LINUX)
// For DirectAuxvDumpInfo
#  include "mozilla/toolkit/crashreporter/rust_minidump_writer_linux_ffi_generated.h"
#endif  // defined(XP_LINUX)
#include "mozilla/crash_helper_ffi_generated.h"

static int parse_int_or_exit(const char* aArg) {
  errno = 0;
  long value = strtol(aArg, nullptr, 10);

  if ((errno != 0) || (value < 0) || (value > INT_MAX)) {
    exit(EXIT_FAILURE);
  }

  return static_cast<int>(value);
}

static BreakpadRawData parse_breakpad_data(const char* aArg) {
#if defined(XP_MACOSX)
  return aArg;
#elif defined(XP_WIN)
  // This is always an ASCII string so we don't need a proper conversion.
  size_t len = strlen(aArg);
  uint16_t* data = new uint16_t[len + 1];
  for (size_t i = 0; i < len; i++) {
    data[i] = aArg[i];
  }
  data[len] = 0;

  return data;
#else  // Linux and friends
  return parse_int_or_exit(aArg);
#endif
}

static void free_breakpad_data(BreakpadRawData aData) {
#if defined(XP_WIN)
  delete aData;
#endif
}

int main(int argc, char* argv[]) {
  if (argc < 6) {
    exit(EXIT_FAILURE);
  }

  Pid client_pid = static_cast<Pid>(parse_int_or_exit(argv[1]));
  BreakpadRawData breakpad_data = parse_breakpad_data(argv[2]);
  char* minidump_path = argv[3];
  char* listener = argv[4];
  char* connector = argv[5];

  int res = crash_generator_logic_desktop(client_pid, breakpad_data,
                                          minidump_path, listener, connector);
  free_breakpad_data(breakpad_data);
  exit(res);
}
