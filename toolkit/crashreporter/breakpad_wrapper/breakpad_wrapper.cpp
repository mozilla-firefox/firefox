/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <string>

#include "mozilla/ProcessType.h"

#if defined(XP_LINUX)
#  include <sys/signalfd.h>
#  include <sys/ucontext.h>
#  include "linux/crash_generation/client_info.h"
#  include "linux/crash_generation/crash_generation_server.h"
#  include "linux/handler/exception_handler.h"
#  include "linux/handler/minidump_descriptor.h"
#  include "mozilla/toolkit/crashreporter/rust_minidump_writer_linux_ffi_generated.h"
using breakpad_char = char;
using breakpad_string = std::string;
using breakpad_init_type = int;
using breakpad_pid = pid_t;
#elif defined(XP_WIN)
#  include "windows/crash_generation/client_info.h"
#  include "windows/crash_generation/crash_generation_server.h"
#  include "windows/handler/exception_handler.h"
using breakpad_char = wchar_t;
using breakpad_string = std::wstring;
using breakpad_init_type = HANDLE;
using breakpad_pid = DWORD;
using ExtraCrashData = void;
#elif defined(XP_MACOSX)
#  include <mach/mach_types.h>
#  include <unistd.h>
#  include "mac/crash_generation/client_info.h"
#  include "mac/crash_generation/crash_generation_server.h"
#  include "mac/handler/exception_handler.h"
using breakpad_char = char;
using breakpad_string = std::string;
using breakpad_init_type = const char*;
using breakpad_pid = pid_t;
using ExtraCrashData = void;
#else
#  error "Unsupported platform"
#endif

#ifdef MOZ_PHC

#  include "PHC.h"

namespace mozilla::phc {

// HACK: The breakpad code expects this global variable even though we don't
// use it in the wrapper.
constinit mozilla::phc::AddrInfo gAddrInfo;

}  // namespace mozilla::phc

#endif  // defined(MOZ_PHC)

using google_breakpad::ClientInfo;
using google_breakpad::CrashGenerationServer;
using google_breakpad::ExceptionHandler;

// These structs and the callback below must be kept in sync with the
// corresponding Rust code in crash_helper_server/src/crash_generation.rs.
struct BreakpadProcessId {
  breakpad_pid pid;
#if defined(XP_MACOSX)
  task_t task;
#elif defined(XP_WIN)
  HANDLE handle;
#endif
};

struct OpaqueCrashGenerator;

using RustDumpCallback = void (*)(const OpaqueCrashGenerator*,
                                  BreakpadProcessId, const ExtraCrashData*,
                                  const breakpad_char*);

struct BreakpadContext {
  RustDumpCallback callback;
  const OpaqueCrashGenerator* generator;
};

#if defined(XP_LINUX)
using RustAuxvCallback = bool (*)(breakpad_pid, DirectAuxvDumpInfo*);
#endif  // defined(XP_LINUX)

void onClientDumpRequestCallback(void* context, const ClientInfo& client_info,
                                 const breakpad_string& file_path) {
  BreakpadContext* breakpad_context = static_cast<BreakpadContext*>(context);
  RustDumpCallback callback = breakpad_context->callback;
  BreakpadProcessId process_id = {
      .pid = client_info.pid(),
#if defined(XP_MACOSX)
      .task = client_info.task(),
#elif defined(XP_WIN)
      .handle = client_info.process_handle(),
#endif
  };
#if defined(XP_LINUX)
  const ExtraCrashData* extra_data = client_info.extra_data();
#else
  const ExtraCrashData* extra_data = nullptr;
#endif  // XP_LINUX

  callback(breakpad_context->generator, process_id, extra_data,
           file_path.c_str());
}

#if defined(XP_LINUX)
bool getAuxvDumpInfo(RustAuxvCallback callback, GeckoChildID aId,
                     DirectAuxvDumpInfo* aAuxvInfo) {
  return callback(aId, aAuxvInfo);
}
#endif  // defined(XP_LINUX)

#ifdef XP_WIN

extern "C" void* CrashGenerationServer_init(breakpad_init_type aBreakpadData,
                                            const breakpad_char* aMinidumpPath,
                                            BreakpadContext* aContext) {
  breakpad_string minidumpPath(aMinidumpPath);

  CrashGenerationServer* server = new CrashGenerationServer(
      aBreakpadData,
      /* connect_callback */ nullptr,
      /* connect_context */ nullptr, onClientDumpRequestCallback,
      reinterpret_cast<void*>(aContext),
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
                                            BreakpadContext* aContext) {
  breakpad_string minidumpPath(aMinidumpPath);
  breakpad_init_type breakpadData = aBreakpadData;

  CrashGenerationServer* server = new CrashGenerationServer(
      breakpadData,
      /* filter */ nullptr,
      /* filter_context */ nullptr, onClientDumpRequestCallback,
      reinterpret_cast<void*>(aContext),
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
                                            BreakpadContext* aContext,
                                            RustAuxvCallback aAuxvCallback) {
  breakpad_string minidumpPath(aMinidumpPath);
  breakpad_init_type breakpadData = aBreakpadData;

  CrashGenerationServer* server = new CrashGenerationServer(
      breakpadData,
      [aAuxvCallback](GeckoChildID aId, DirectAuxvDumpInfo* aAuxvInfo) {
        return getAuxvDumpInfo(aAuxvCallback, aId, aAuxvInfo);
      },
      [aContext]([[maybe_unused]] void* dump_context,
                 const ClientInfo& aClientInfo,
                 const breakpad_string& aFilePath) {
        onClientDumpRequestCallback(aContext, aClientInfo, aFilePath);
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

struct MinidumpCallbackResult {
  breakpad_char* path;
  const size_t len;
};

#ifdef XP_WIN

static bool WriteMinidumpCallback(const wchar_t* aDumpPath,
                                  const wchar_t* aMinidumpId, void* aContext,
                                  EXCEPTION_POINTERS* aExInfo,
                                  MDRawAssertionInfo* aAssertion,
                                  const mozilla::phc::AddrInfo* aAddrInfo,
                                  bool aSucceeded) {
  if (aSucceeded) {
    MinidumpCallbackResult* result =
        reinterpret_cast<MinidumpCallbackResult*>(aContext);

    swprintf(result->path, result->len, L"%ls\\%ls.dmp", aDumpPath,
             aMinidumpId);
  }

  return aSucceeded;
}

extern "C" bool WriteMinidumpForProcess(GeckoChildID aID, HANDLE aProcess,
                                        DWORD aThread,
                                        const breakpad_char* aDumpPath,
                                        breakpad_char* aResultPath,
                                        size_t aResultPathLen) {
  MinidumpCallbackResult result{aResultPath, aResultPathLen};
  breakpad_string dump_path(aDumpPath);

  bool res = ExceptionHandler::WriteMinidumpForChild(
      aProcess, aThread, dump_path, WriteMinidumpCallback, &result);

  return res;
}

#elif defined(XP_DARWIN)

bool WriteMinidumpCallback(const char* aDumpDir, const char* aMinidumpId,
                           void* aContext,
                           const mozilla::phc::AddrInfo* aAddrInfo,
                           bool aSucceeded) {
  if (aSucceeded) {
    MinidumpCallbackResult* result =
        reinterpret_cast<MinidumpCallbackResult*>(aContext);

    snprintf(result->path, result->len, "%s/%s.dmp", aDumpDir, aMinidumpId);
  }

  return aSucceeded;
}

extern "C" bool WriteMinidumpForProcess(GeckoChildID aID, mach_port_t aProcess,
                                        mach_port_t aThread,
                                        const breakpad_char* aDumpPath,
                                        breakpad_char* aResultPath,
                                        size_t aResultPathLen) {
  MinidumpCallbackResult result{aResultPath, aResultPathLen};
  breakpad_string dump_path(aDumpPath);

  // Fully qualified to avoid conflicts with OS header `MachineExceptions.h`.
  bool res = google_breakpad::ExceptionHandler::WriteMinidumpForChild(
      aProcess, aThread, dump_path, WriteMinidumpCallback, &result);

  return res;
}

#elif defined(XP_LINUX)

using google_breakpad::MinidumpDescriptor;

bool WriteMinidumpCallback(const MinidumpDescriptor& aDescriptor,
                           void* aContext,
                           const mozilla::phc::AddrInfo* /*aAddrInfo*/,
                           bool aSucceeded) {
  if (aSucceeded) {
    MinidumpCallbackResult* result =
        reinterpret_cast<MinidumpCallbackResult*>(aContext);

    snprintf(result->path, result->len, "%s", aDescriptor.path());
  }

  return aSucceeded;
}

extern "C" bool WriteMinidumpForProcess(GeckoChildID aId, pid_t aProcess,
                                        pid_t aThread,
                                        RustAuxvCallback aAuxvCallback,
                                        const breakpad_char* aDumpPath,
                                        breakpad_char* aResultPath,
                                        size_t aResultPathLen) {
  MinidumpCallbackResult result{aResultPath, aResultPathLen};
  DirectAuxvDumpInfo auxv_info = {};
  DirectAuxvDumpInfo* auxv_info_ptr = &auxv_info;

  if (!getAuxvDumpInfo(aAuxvCallback, aId, auxv_info_ptr)) {
    auxv_info_ptr = nullptr;
  }

  breakpad_string dump_path(aDumpPath);

  bool res = ExceptionHandler::WriteMinidumpForChild(
      aProcess, aThread, auxv_info_ptr, dump_path, WriteMinidumpCallback,
      &result);

  return res;
}

#endif
