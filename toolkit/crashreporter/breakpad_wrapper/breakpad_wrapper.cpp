/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string>

#if defined(XP_LINUX)
#  include <sys/signalfd.h>
#  include <sys/ucontext.h>
#  include "linux/crash_generation/client_info.h"
#  include "linux/crash_generation/crash_generation_server.h"
using breakpad_char = char;
using breakpad_string = std::string;
using breakpad_init_type = int;
using breakpad_pid = pid_t;
#elif defined(XP_WIN)
#  include "windows/crash_generation/client_info.h"
#  include "windows/crash_generation/crash_generation_server.h"
using breakpad_char = wchar_t;
using breakpad_string = std::wstring;
using breakpad_init_type = wchar_t*;
using breakpad_pid = DWORD;
#elif defined(XP_MACOSX)
#  include <mach/mach_types.h>
#  include <unistd.h>
#  include "mac/crash_generation/client_info.h"
#  include "mac/crash_generation/crash_generation_server.h"
using breakpad_char = char;
using breakpad_string = std::string;
using breakpad_init_type = const char*;
using breakpad_pid = pid_t;
#else
#  error "Unsupported platform"
#endif

#ifdef MOZ_PHC

#  include "PHC.h"

namespace mozilla::phc {

// HACK: The breakpad code expects this global variable even though we don't
// use it in the wrapper.
MOZ_RUNINIT mozilla::phc::AddrInfo gAddrInfo;

}  // namespace mozilla::phc

#endif  // defined(MOZ_PHC)

using google_breakpad::ClientInfo;
using google_breakpad::CrashGenerationServer;

// This struct and the callback that uses it need to be kept in sync with the
// corresponding Rust code in src/crash_generation.rs.
struct BreakpadProcessId {
  breakpad_pid pid;
#if defined(XP_MACOSX)
  task_t task;
#elif defined(XP_WIN)
  HANDLE handle;
#endif
};

using RustDumpCallback = void (*)(BreakpadProcessId, const char*,
                                  const breakpad_char*);
#if defined(XP_LINUX)
using RustAuxvCallback = bool (*)(breakpad_pid, DirectAuxvDumpInfo*);
#endif  // defined(XP_LINUX)

void onClientDumpRequestCallback(void* context, const ClientInfo& client_info,
                                 const breakpad_string& file_path) {
  RustDumpCallback callback = reinterpret_cast<RustDumpCallback>(context);
  BreakpadProcessId process_id = {
      .pid = client_info.pid(),
#if defined(XP_MACOSX)
      .task = client_info.task(),
#elif defined(XP_WIN)
      .handle = client_info.process_handle(),
#endif
  };
  const char* error_msg =
#if defined(XP_LINUX)
      client_info.error_msg();
#else
      nullptr;
#endif  // XP_LINUX

  callback(process_id, error_msg, file_path.c_str());
}

#if defined(XP_LINUX)
bool getAuxvDumpInfo(RustAuxvCallback callback, breakpad_pid aPid,
                     DirectAuxvDumpInfo* aAuxvInfo) {
  return callback(aPid, aAuxvInfo);
}
#endif  // defined(XP_LINUX)

#ifdef XP_WIN

extern "C" void* CrashGenerationServer_init(breakpad_init_type aBreakpadData,
                                            const breakpad_char* aMinidumpPath,
                                            RustDumpCallback aDumpCallback) {
  breakpad_string minidumpPath(aMinidumpPath);
  breakpad_string breakpadData(aBreakpadData);

  CrashGenerationServer* server = new CrashGenerationServer(
      breakpadData,
      /* pipe_sec_attrs */ nullptr,
      /* connect_callback */ nullptr,
      /* connect_context */ nullptr, onClientDumpRequestCallback,
      reinterpret_cast<void*>(aDumpCallback),
      /* written_callback */ nullptr,
      /* exit_callback */ nullptr,
      /* exit_context */ nullptr,
      /* upload_request_callback */ nullptr,
      /* upload_context */ nullptr,
      /* generate_dumps */ true, &minidumpPath);

  if (!server->Start()) {
    delete server;
    return nullptr;
  }

  return server;
}

#elif defined(XP_MACOSX)

extern "C" void* CrashGenerationServer_init(breakpad_init_type aBreakpadData,
                                            const breakpad_char* aMinidumpPath,
                                            RustDumpCallback aDumpCallback) {
  breakpad_string minidumpPath(aMinidumpPath);
  breakpad_init_type breakpadData = aBreakpadData;

  CrashGenerationServer* server = new CrashGenerationServer(
      breakpadData,
      /* filter */ nullptr,
      /* filter_context */ nullptr, onClientDumpRequestCallback,
      reinterpret_cast<void*>(aDumpCallback),
      /* exit_callback */ nullptr,
      /* exit_context */ nullptr,
      /* generate_dumps */ true, minidumpPath);

  if (!server->Start()) {
    delete server;
    return nullptr;
  }

  return server;
}

#elif defined(XP_LINUX)

extern "C" void* CrashGenerationServer_init(breakpad_init_type aBreakpadData,
                                            const breakpad_char* aMinidumpPath,
                                            RustDumpCallback aDumpCallback,
                                            RustAuxvCallback aAuxvCallback) {
  breakpad_string minidumpPath(aMinidumpPath);
  breakpad_init_type breakpadData = aBreakpadData;

  CrashGenerationServer* server = new CrashGenerationServer(
      breakpadData,
      [aAuxvCallback](pid_t aPid, DirectAuxvDumpInfo* aAuxvInfo) {
        return getAuxvDumpInfo(aAuxvCallback, aPid, aAuxvInfo);
      },
      [aDumpCallback](void* dump_context, const ClientInfo& aClientInfo,
                      const breakpad_string& aFilePath) {
        onClientDumpRequestCallback(reinterpret_cast<void*>(aDumpCallback),
                                    aClientInfo, aFilePath);
      },
      /* dump_context */ nullptr, &minidumpPath);

  if (!server->Start()) {
    delete server;
    return nullptr;
  }

  return server;
}

#endif

extern "C" void CrashGenerationServer_shutdown(void* aServer) {
  CrashGenerationServer* server = static_cast<CrashGenerationServer*>(aServer);
  delete server;
}

extern "C" void CrashGenerationServer_set_path(
    void* aServer, const breakpad_char* aMinidumpPath) {
  CrashGenerationServer* server = static_cast<CrashGenerationServer*>(aServer);
  server->SetPath(aMinidumpPath);
}
