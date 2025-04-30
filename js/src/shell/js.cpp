/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* JS shell. */

#include "mozilla/AlreadyAddRefed.h"  // mozilla::already_AddRefed
#include "mozilla/ArrayUtils.h"
#include "mozilla/Assertions.h"  // MOZ_ASSERT, MOZ_ASSERT_IF, MOZ_RELEASE_ASSERT, MOZ_CRASH
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/Compression.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/EnumSet.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/mozalloc.h"
#include "mozilla/PodOperations.h"
#include "mozilla/RandomNum.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Sprintf.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtrExtensions.h"  // UniqueFreePtr
#include "mozilla/Utf8.h"
#include "mozilla/Variant.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#ifdef XP_WIN
#  include <direct.h>
#  include <process.h>
#endif
#include <errno.h>
#include <fcntl.h>
#if defined(XP_WIN)
#  include <io.h> /* for isatty() */
#endif
#include <locale.h>
#if defined(MALLOC_H)
#  include MALLOC_H /* for malloc_usable_size, malloc_size, _msize */
#endif
#include <ctime>
#include <math.h>
#ifndef __wasi__
#  include <signal.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>
#ifdef XP_UNIX
#  ifndef __wasi__
#    include <sys/mman.h>
#    include <sys/wait.h>
#  endif
#  include <sys/stat.h>
#  include <unistd.h>
#endif
#ifdef XP_LINUX
#  include <sys/prctl.h>
#endif

#include "jsapi.h"
#include "jsfriendapi.h"
#include "jstypes.h"
#include "fmt/format.h"
#ifndef JS_WITHOUT_NSPR
#  include "prerror.h"
#  include "prlink.h"
#endif

#include "builtin/Array.h"
#include "builtin/MapObject.h"
#include "builtin/ModuleObject.h"
#include "builtin/RegExp.h"
#include "builtin/TestingFunctions.h"
#include "builtin/TestingUtility.h"  // js::ParseCompileOptions, js::ParseDebugMetadata, js::CreateScriptPrivate
#include "debugger/DebugAPI.h"
#include "frontend/CompilationStencil.h"
#include "frontend/FrontendContext.h"  // AutoReportFrontendContext
#include "frontend/ModuleSharedContext.h"
#include "frontend/Parser.h"
#include "frontend/ScopeBindingCache.h"  // js::frontend::ScopeBindingCache
#include "gc/GC.h"
#include "gc/PublicIterators.h"
#ifdef DEBUG
#  include "irregexp/RegExpAPI.h"
#endif

#ifdef JS_SIMULATOR_ARM
#  include "jit/arm/Simulator-arm.h"
#endif
#ifdef JS_SIMULATOR_MIPS64
#  include "jit/mips64/Simulator-mips64.h"
#endif
#ifdef JS_SIMULATOR_LOONG64
#  include "jit/loong64/Simulator-loong64.h"
#endif
#ifdef JS_SIMULATOR_RISCV64
#  include "jit/riscv64/Simulator-riscv64.h"
#endif
#include "jit/BaselineCompileQueue.h"
#include "jit/CacheIRHealth.h"
#include "jit/InlinableNatives.h"
#include "jit/Ion.h"
#include "jit/JitcodeMap.h"
#include "jit/JitZone.h"
#include "jit/shared/CodeGenerator-shared.h"
#include "js/Array.h"        // JS::NewArrayObject
#include "js/ArrayBuffer.h"  // JS::{CreateMappedArrayBufferContents,NewMappedArrayBufferWithContents,IsArrayBufferObject,GetArrayBufferLengthAndData}
#include "js/BuildId.h"      // JS::BuildIdCharVector, JS::SetProcessBuildIdOp
#include "js/CallAndConstruct.h"  // JS::Call, JS::IsCallable, JS_CallFunction, JS_CallFunctionValue
#include "js/CharacterEncoding.h"  // JS::StringIsASCII
#include "js/CompilationAndEvaluation.h"
#include "js/CompileOptions.h"  // JS::ReadOnlyCompileOptions, JS::CompileOptions, JS::OwningCompileOptions, JS::DecodeOptions, JS::InstantiateOptions
#include "js/ContextOptions.h"  // JS::ContextOptions{,Ref}
#include "js/Debug.h"  // JS::dbg::ShouldAvoidSideEffects, JS::ExecutionTrace
#include "js/EnvironmentChain.h"            // JS::EnvironmentChain
#include "js/Equality.h"                    // JS::SameValue
#include "js/ErrorReport.h"                 // JS::PrintError
#include "js/Exception.h"                   // JS::StealPendingExceptionStack
#include "js/experimental/BindingAllocs.h"  // JS_NewObjectWithGivenProtoAndUseAllocSite
#include "js/experimental/CodeCoverage.h"   // js::EnableCodeCoverage
#include "js/experimental/CompileScript.h"  // JS::NewFrontendContext, JS::DestroyFrontendContext, JS::HadFrontendErrors, JS::ConvertFrontendErrorsToRuntimeErrors, JS::CompileGlobalScriptToStencil, JS::CompileModuleScriptToStencil
#include "js/experimental/CTypes.h"         // JS::InitCTypesClass
#include "js/experimental/Intl.h"  // JS::AddMoz{DateTimeFormat,DisplayNames}Constructor
#include "js/experimental/JitInfo.h"  // JSJit{Getter,Setter,Method}CallArgs, JSJitGetterInfo, JSJit{Getter,Setter}Op, JSJitInfo
#include "js/experimental/JSStencil.h"  // JS::Stencil, JS::DecodeStencil, JS::InstantiateModuleStencil
#include "js/experimental/SourceHook.h"  // js::{Set,Forget,}SourceHook
#include "js/experimental/TypedData.h"   // JS_NewUint8Array
#include "js/friend/DumpFunctions.h"     // JS::FormatStackDump
#include "js/friend/ErrorMessages.h"     // js::GetErrorMessage, JSMSG_*
#include "js/friend/StackLimits.h"       // js::AutoCheckRecursionLimit
#include "js/friend/WindowProxy.h"  // js::IsWindowProxy, js::SetWindowProxyClass, js::ToWindowProxyIfWindow, js::ToWindowIfWindowProxy
#include "js/GCAPI.h"               // JS::AutoCheckCannotGC
#include "js/GCVector.h"
#include "js/GlobalObject.h"
#include "js/Initialization.h"
#include "js/Interrupt.h"
#include "js/JSON.h"
#include "js/MemoryCallbacks.h"
#include "js/MemoryFunctions.h"
#include "js/Modules.h"  // JS::GetModulePrivate, JS::SetModule{DynamicImport,Metadata,Resolve}Hook, JS::SetModulePrivate, JS::CompileModule
#include "js/Object.h"  // JS::GetClass, JS::GetCompartment, JS::GetReservedSlot, JS::SetReservedSlot
#include "js/Prefs.h"
#include "js/Principals.h"
#include "js/Printer.h"  // QuoteString
#include "js/Printf.h"
#include "js/PropertyAndElement.h"  // JS_DefineElement, JS_DefineFunction, JS_DefineFunctions, JS_DefineProperties, JS_DefineProperty, JS_GetElement, JS_GetProperty, JS_GetPropertyById, JS_HasProperty, JS_SetElement, JS_SetProperty, JS_SetPropertyById
#include "js/PropertySpec.h"
#include "js/Realm.h"
#include "js/RegExp.h"  // JS::ObjectIsRegExp
#include "js/ScriptPrivate.h"
#include "js/SourceText.h"  // JS::SourceText
#include "js/StableStringChars.h"
#include "js/Stack.h"
#include "js/StreamConsumer.h"
#include "js/StructuredClone.h"
#include "js/SweepingAPI.h"
#include "js/Transcoding.h"  // JS::TranscodeBuffer, JS::TranscodeRange, JS::IsTranscodeFailureResult
#include "js/Warnings.h"      // JS::SetWarningReporter
#include "js/WasmFeatures.h"  // JS_FOR_WASM_FEATURES
#include "js/WasmModule.h"    // JS::WasmModule
#include "js/Wrapper.h"
#include "proxy/DeadObjectProxy.h"  // js::IsDeadProxyObject
#include "shell/jsoptparse.h"
#include "shell/jsshell.h"
#include "shell/OSObject.h"
#include "shell/ShellModuleObjectWrapper.h"
#include "shell/WasmTesting.h"
#include "threading/ConditionVariable.h"
#include "threading/ExclusiveData.h"
#include "threading/LockGuard.h"
#include "threading/Thread.h"
#include "util/CompleteFile.h"  // js::FileContents, js::ReadCompleteFile
#include "util/DifferentialTesting.h"
#include "util/StringBuilder.h"
#include "util/Text.h"
#include "util/WindowsWrapper.h"
#include "vm/ArgumentsObject.h"
#include "vm/Compression.h"
#include "vm/ErrorObject.h"
#include "vm/ErrorReporting.h"
#include "vm/HelperThreads.h"
#include "vm/JSAtomUtils.h"  // AtomizeUTF8Chars, AtomizeString, ToAtom
#include "vm/JSContext.h"
#include "vm/JSFunction.h"
#include "vm/JSObject.h"
#include "vm/JSScript.h"
#include "vm/Logging.h"
#include "vm/ModuleBuilder.h"  // js::ModuleBuilder
#include "vm/Modules.h"
#include "vm/Monitor.h"
#include "vm/MutexIDs.h"
#include "vm/PromiseObject.h"  // js::PromiseObject
#include "vm/Shape.h"
#include "vm/SharedArrayObject.h"
#include "vm/StencilObject.h"  // js::StencilObject
#include "vm/Time.h"
#include "vm/ToSource.h"  // js::ValueToSource
#include "vm/TypedArrayObject.h"
#include "vm/WrapperObject.h"
#include "wasm/WasmFeatures.h"
#include "wasm/WasmJS.h"

#include "vm/Compartment-inl.h"
#include "vm/ErrorObject-inl.h"
#include "vm/Interpreter-inl.h"
#include "vm/JSObject-inl.h"
#include "vm/Realm-inl.h"
#include "vm/Stack-inl.h"

#undef compress

using namespace js;
using namespace js::cli;
using namespace js::shell;

using JS::AutoStableStringChars;
using JS::CompileOptions;

using js::shell::RCFile;

using mozilla::ArrayEqual;
using mozilla::AsVariant;
using mozilla::Atomic;
using mozilla::MakeScopeExit;
using mozilla::Maybe;
using mozilla::Nothing;
using mozilla::NumberEqualsInt32;
using mozilla::TimeDuration;
using mozilla::TimeStamp;
using mozilla::Utf8Unit;
using mozilla::Variant;

bool InitOptionParser(OptionParser& op);
bool SetGlobalOptionsPreJSInit(const OptionParser& op);
bool SetGlobalOptionsPostJSInit(const OptionParser& op);
bool SetContextOptions(JSContext* cx, const OptionParser& op);
bool SetContextWasmOptions(JSContext* cx, const OptionParser& op);
bool SetContextJITOptions(JSContext* cx, const OptionParser& op);
bool SetContextGCOptions(JSContext* cx, const OptionParser& op);
bool InitModuleLoader(JSContext* cx, const OptionParser& op);

#ifdef FUZZING_JS_FUZZILLI
#  define REPRL_CRFD 100
#  define REPRL_CWFD 101
#  define REPRL_DRFD 102
#  define REPRL_DWFD 103

#  define SHM_SIZE 0x100000
#  define MAX_EDGES ((SHM_SIZE - 4) * 8)

struct shmem_data {
  uint32_t num_edges;
  unsigned char edges[];
};

struct shmem_data* __shmem;

uint32_t *__edges_start, *__edges_stop;
void __sanitizer_cov_reset_edgeguards() {
  uint64_t N = 0;
  for (uint32_t* x = __edges_start; x < __edges_stop && N < MAX_EDGES; x++)
    *x = ++N;
}

extern "C" void __sanitizer_cov_trace_pc_guard_init(uint32_t* start,
                                                    uint32_t* stop) {
  // Avoid duplicate initialization
  if (start == stop || *start) return;

  if (__edges_start != NULL || __edges_stop != NULL) {
    fprintf(stderr,
            "Coverage instrumentation is only supported for a single module\n");
    _exit(-1);
  }

  __edges_start = start;
  __edges_stop = stop;

  // Map the shared memory region
  const char* shm_key = getenv("SHM_ID");
  if (!shm_key) {
    puts("[COV] no shared memory bitmap available, skipping");
    __shmem = (struct shmem_data*)malloc(SHM_SIZE);
  } else {
    int fd = shm_open(shm_key, O_RDWR, S_IREAD | S_IWRITE);
    if (fd <= -1) {
      fprintf(stderr, "Failed to open shared memory region: %s\n",
              strerror(errno));
      _exit(-1);
    }

    __shmem = (struct shmem_data*)mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, fd, 0);
    if (__shmem == MAP_FAILED) {
      fprintf(stderr, "Failed to mmap shared memory region\n");
      _exit(-1);
    }
  }

  __sanitizer_cov_reset_edgeguards();

  __shmem->num_edges = stop - start;
  printf("[COV] edge counters initialized. Shared memory: %s with %u edges\n",
         shm_key, __shmem->num_edges);
}

extern "C" void __sanitizer_cov_trace_pc_guard(uint32_t* guard) {
  // There's a small race condition here: if this function executes in two
  // threads for the same edge at the same time, the first thread might disable
  // the edge (by setting the guard to zero) before the second thread fetches
  // the guard value (and thus the index). However, our instrumentation ignores
  // the first edge (see libcoverage.c) and so the race is unproblematic.
  uint32_t index = *guard;
  // If this function is called before coverage instrumentation is properly
  // initialized we want to return early.
  if (!index) return;
  __shmem->edges[index / 8] |= 1 << (index % 8);
  *guard = 0;
}
#endif /* FUZZING_JS_FUZZILLI */

enum JSShellExitCode {
  EXITCODE_RUNTIME_ERROR = 3,
  EXITCODE_FILE_NOT_FOUND = 4,
  EXITCODE_OUT_OF_MEMORY = 5,
  EXITCODE_TIMEOUT = 6
};

struct ShellLogModule {
  // Since ShellLogModules have references to their levels created
  // we can't move them.
  ShellLogModule(ShellLogModule&&) = delete;

  const char* name;
  explicit ShellLogModule(const char* name) : name(name) {}
  mozilla::AtomicLogLevel level;
};

// If asserts related to this ever fail, simply bump this number.
//
// This is used to construct a mozilla::Array, which is used because a
// ShellLogModule cannot move once constructed to avoid invalidating
// a levelRef.
static const int MAX_LOG_MODULES = 64;
static int initialized_modules = 0;
mozilla::Array<mozilla::Maybe<ShellLogModule>, MAX_LOG_MODULES> logModules;

JS::OpaqueLogger GetLoggerByName(const char* name) {
  // Check for pre-existing module
  for (auto& logger : logModules) {
    if (logger) {
      if (logger->name == name) {
        return logger.ptr();
      }
    }
    // We've seen all initialized, not there, break out.
    if (!logger) break;
  }

  // Not found, allocate a new module.
  MOZ_RELEASE_ASSERT(initialized_modules < MAX_LOG_MODULES - 1);
  auto index = initialized_modules++;
  logModules[index].emplace(name);
  return logModules[index].ptr();
}

mozilla::AtomicLogLevel& GetLevelRef(JS::OpaqueLogger logger) {
  ShellLogModule* slm = static_cast<ShellLogModule*>(logger);
  return slm->level;
}

void LogPrintVA(const JS::OpaqueLogger logger, mozilla::LogLevel level,
                const char* fmt, va_list ap) {
  ShellLogModule* mod = static_cast<ShellLogModule*>(logger);
  fprintf(stderr, "[%s] ", mod->name);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
}

void LogPrintFmt(const JS::OpaqueLogger logger, mozilla::LogLevel level,
                 fmt::string_view fmt, fmt::format_args args) {
  ShellLogModule* mod = static_cast<ShellLogModule*>(logger);
  fmt::print(stderr, FMT_STRING("[{}] {}\n"), mod->name,
             fmt::vformat(fmt, args));
}

JS::LoggingInterface shellLoggingInterface = {GetLoggerByName, LogPrintVA,
                                              LogPrintFmt, GetLevelRef};

static void ToLower(const char* src, char* dest, size_t len) {
  for (size_t c = 0; c < len; c++) {
    dest[c] = (char)(tolower(src[c]));
  }
}

// Run this after initialiation!
void ParseLoggerOptions() {
  char* mixedCaseOpts = getenv("MOZ_LOG");
  if (!mixedCaseOpts) {
    return;
  }

  // Copy into a new buffer and lower case to do case insensitive matching.
  //
  // Done this way rather than just using strcasestr because Windows doesn't
  // have strcasestr as part of its base C library.
  size_t len = strlen(mixedCaseOpts);
  mozilla::UniqueFreePtr<char[]> logOpts(
      static_cast<char*>(calloc(len + 1, 1)));
  if (!logOpts) {
    return;
  }

  ToLower(mixedCaseOpts, logOpts.get(), len);

  // This is a really permissive parser, but will suffice!
  for (auto& logger : logModules) {
    if (logger) {
      // Lowercase the logger name for strstr
      size_t len = strlen(logger->name);
      mozilla::UniqueFreePtr<char[]> lowerName(
          static_cast<char*>(calloc(len + 1, 1)));
      ToLower(logger->name, lowerName.get(), len);

      if (char* needle = strstr(logOpts.get(), lowerName.get())) {
        // If the string to enable a logger is present, but no level is provided
        // then default to Debug level.
        int logLevel = static_cast<int>(mozilla::LogLevel::Debug);

        if (char* colon = strchr(needle, ':')) {
          // Parse character after colon as log level.
          if (*(colon + 1)) {
            logLevel = atoi(colon + 1);
          }
        }

        fprintf(stderr, "[JS_LOG] Enabling Logger %s at level %d\n",
                logger->name, logLevel);
        logger->level = mozilla::ToLogLevel(logLevel);
      }
    }
  }
}

/*
 * Limit the timeout to 30 minutes to prevent an overflow on platfoms
 * that represent the time internally in microseconds using 32-bit int.
 */
static const double MAX_TIMEOUT_SECONDS = 1800.0;

// Not necessarily in sync with the browser
#ifdef ENABLE_SHARED_MEMORY
#  define SHARED_MEMORY_DEFAULT 1
#else
#  define SHARED_MEMORY_DEFAULT 0
#endif

// Fuzzing support for JS runtime fuzzing
#ifdef FUZZING_INTERFACES
#  include "shell/jsrtfuzzing/jsrtfuzzing.h"
MOZ_RUNINIT static bool fuzzDoDebug = !!getenv("MOZ_FUZZ_DEBUG");
MOZ_RUNINIT static bool fuzzHaveModule = !!getenv("FUZZER");
#endif  // FUZZING_INTERFACES

// Code to support GCOV code coverage measurements on standalone shell
#ifdef MOZ_CODE_COVERAGE
#  if defined(__GNUC__) && !defined(__clang__)
extern "C" void __gcov_dump();
extern "C" void __gcov_reset();

void counters_dump(int) { __gcov_dump(); }

void counters_reset(int) { __gcov_reset(); }
#  else
void counters_dump(int) { /* Do nothing */ }

void counters_reset(int) { /* Do nothing */ }
#  endif

static void InstallCoverageSignalHandlers() {
#  ifndef XP_WIN
  fprintf(stderr, "[CodeCoverage] Setting handlers for process %d.\n",
          getpid());

  struct sigaction dump_sa;
  dump_sa.sa_handler = counters_dump;
  dump_sa.sa_flags = SA_RESTART;
  sigemptyset(&dump_sa.sa_mask);
  mozilla::DebugOnly<int> r1 = sigaction(SIGUSR1, &dump_sa, nullptr);
  MOZ_ASSERT(r1 == 0, "Failed to install GCOV SIGUSR1 handler");

  struct sigaction reset_sa;
  reset_sa.sa_handler = counters_reset;
  reset_sa.sa_flags = SA_RESTART;
  sigemptyset(&reset_sa.sa_mask);
  mozilla::DebugOnly<int> r2 = sigaction(SIGUSR2, &reset_sa, nullptr);
  MOZ_ASSERT(r2 == 0, "Failed to install GCOV SIGUSR2 handler");
#  endif
}
#endif

// An off-thread parse or decode job.
class js::shell::OffThreadJob {
  static constexpr size_t kCompileStackQuota = 128 * sizeof(size_t) * 1024;
  static constexpr size_t kThreadStackQuota =
      kCompileStackQuota + 128 * sizeof(size_t) * 1024;

  enum State {
    RUNNING,   // Working; no stencil.
    DONE,      // Finished; have stencil.
    CANCELLED  // Cancelled due to error.
  };

 public:
  enum class Kind {
    CompileScript,
    CompileModule,
    Decode,
  };

  OffThreadJob(ShellContext* sc, Kind kind, JS::SourceText<char16_t>&& srcBuf);
  OffThreadJob(ShellContext* sc, Kind kind, JS::TranscodeBuffer&& xdrBuf);

  ~OffThreadJob();

  bool init(JSContext* cx, const JS::ReadOnlyCompileOptions& options);
  bool dispatch();

  static void OffThreadMain(OffThreadJob* self);
  void run();

  void cancel();
  void waitUntilDone();

  already_AddRefed<JS::Stencil> stealStencil(JSContext* cx);

 public:
  const int32_t id;

 private:
  Kind kind_;
  State state_;

  JS::FrontendContext* fc_ = nullptr;
  JS::OwningCompileOptions options_;

  UniquePtr<Thread> thread_;

  JS::SourceText<char16_t> srcBuf_;
  JS::TranscodeBuffer xdrBuf_;

  RefPtr<JS::Stencil> stencil_;

  JS::TranscodeResult transcodeResult_ = JS::TranscodeResult::Ok;
};

template <typename T>
static OffThreadJob* NewOffThreadJob(JSContext* cx, OffThreadJob::Kind kind,
                                     JS::ReadOnlyCompileOptions& options,
                                     T&& source) {
  ShellContext* sc = GetShellContext(cx);
  if (sc->isWorker) {
    // Off-thread compilation/decode is used by main-thread, in order to improve
    // the responsiveness.  It's not used by worker in browser, and there's not
    // much reason to support worker here.
    JS_ReportErrorASCII(cx, "Off-thread job is not supported in worker");
    return nullptr;
  }

  UniquePtr<OffThreadJob> job(
      cx->new_<OffThreadJob>(sc, kind, std::move(source)));
  if (!job) {
    return nullptr;
  }

  if (!job->init(cx, options)) {
    return nullptr;
  }

  if (!sc->offThreadJobs.append(job.get())) {
    job->cancel();
    JS_ReportErrorASCII(cx, "OOM adding off-thread job");
    return nullptr;
  }

  return job.release();
}

static OffThreadJob* GetSingleOffThreadJob(JSContext* cx) {
  ShellContext* sc = GetShellContext(cx);
  const auto& jobs = sc->offThreadJobs;
  if (jobs.empty()) {
    JS_ReportErrorASCII(cx, "No off-thread jobs are pending");
    return nullptr;
  }

  if (jobs.length() > 1) {
    JS_ReportErrorASCII(
        cx, "Multiple off-thread jobs are pending: must specify job ID");
    return nullptr;
  }

  return jobs[0];
}

static OffThreadJob* LookupOffThreadJobByID(JSContext* cx, int32_t id) {
  if (id <= 0) {
    JS_ReportErrorASCII(cx, "Bad off-thread job ID");
    return nullptr;
  }

  ShellContext* sc = GetShellContext(cx);
  const auto& jobs = sc->offThreadJobs;
  if (jobs.empty()) {
    JS_ReportErrorASCII(cx, "No off-thread jobs are pending");
    return nullptr;
  }

  OffThreadJob* job = nullptr;
  for (auto someJob : jobs) {
    if (someJob->id == id) {
      job = someJob;
      break;
    }
  }

  if (!job) {
    JS_ReportErrorASCII(cx, "Off-thread job not found");
    return nullptr;
  }

  return job;
}

static OffThreadJob* LookupOffThreadJobForArgs(JSContext* cx,
                                               const CallArgs& args,
                                               size_t arg) {
  // If the optional ID argument isn't present, get the single pending job.
  if (args.length() <= arg) {
    return GetSingleOffThreadJob(cx);
  }

  // Lookup the job using the specified ID.
  int32_t id = 0;
  RootedValue value(cx, args[arg]);
  if (!ToInt32(cx, value, &id)) {
    return nullptr;
  }

  return LookupOffThreadJobByID(cx, id);
}

static void DeleteOffThreadJob(JSContext* cx, OffThreadJob* job) {
  ShellContext* sc = GetShellContext(cx);
  for (size_t i = 0; i < sc->offThreadJobs.length(); i++) {
    if (sc->offThreadJobs[i] == job) {
      sc->offThreadJobs.erase(&sc->offThreadJobs[i]);
      js_delete(job);
      return;
    }
  }

  MOZ_CRASH("Off-thread job not found");
}

static void CancelOffThreadJobsForRuntime(JSContext* cx) {
  ShellContext* sc = GetShellContext(cx);
  while (!sc->offThreadJobs.empty()) {
    OffThreadJob* job = sc->offThreadJobs.popCopy();
    job->waitUntilDone();
    js_delete(job);
  }
}

mozilla::Atomic<int32_t> gOffThreadJobSerial(1);

OffThreadJob::OffThreadJob(ShellContext* sc, Kind kind,
                           JS::SourceText<char16_t>&& srcBuf)
    : id(gOffThreadJobSerial++),
      kind_(kind),
      state_(RUNNING),
      options_(JS::OwningCompileOptions::ForFrontendContext()),
      srcBuf_(std::move(srcBuf)) {
  MOZ_RELEASE_ASSERT(id > 0, "Off-thread job IDs exhausted");
}

OffThreadJob::OffThreadJob(ShellContext* sc, Kind kind,
                           JS::TranscodeBuffer&& xdrBuf)
    : id(gOffThreadJobSerial++),
      kind_(kind),
      state_(RUNNING),
      options_(JS::OwningCompileOptions::ForFrontendContext()),
      xdrBuf_(std::move(xdrBuf)) {
  MOZ_RELEASE_ASSERT(id > 0, "Off-thread job IDs exhausted");
}

OffThreadJob::~OffThreadJob() {
  if (fc_) {
    JS::DestroyFrontendContext(fc_);
  }
  MOZ_ASSERT(state_ != RUNNING);
}

bool OffThreadJob::init(JSContext* cx,
                        const JS::ReadOnlyCompileOptions& options) {
  fc_ = JS::NewFrontendContext();
  if (!fc_) {
    ReportOutOfMemory(cx);
    state_ = CANCELLED;
    return false;
  }

  if (!options_.copy(cx, options)) {
    state_ = CANCELLED;
    return false;
  }

  return true;
}

bool OffThreadJob::dispatch() {
  thread_ =
      js::MakeUnique<Thread>(Thread::Options().setStackSize(kThreadStackQuota));
  if (!thread_) {
    state_ = CANCELLED;
    return false;
  }

  if (!thread_->init(OffThreadJob::OffThreadMain, this)) {
    state_ = CANCELLED;
    thread_ = nullptr;
    return false;
  }

  return true;
}

/* static */ void OffThreadJob::OffThreadMain(OffThreadJob* self) {
  self->run();
}

void OffThreadJob::run() {
  MOZ_ASSERT(state_ == RUNNING);
  MOZ_ASSERT(!stencil_);

  JS::SetNativeStackQuota(fc_, kCompileStackQuota);

  switch (kind_) {
    case Kind::CompileScript: {
      stencil_ = JS::CompileGlobalScriptToStencil(fc_, options_, srcBuf_);
      break;
    }
    case Kind::CompileModule: {
      stencil_ = JS::CompileModuleScriptToStencil(fc_, options_, srcBuf_);
      break;
    }
    case Kind::Decode: {
      JS::DecodeOptions decodeOptions(options_);
      JS::TranscodeRange range(xdrBuf_.begin(), xdrBuf_.length());
      transcodeResult_ = JS::DecodeStencil(fc_, decodeOptions, range,
                                           getter_AddRefs(stencil_));
      break;
    }
  }

  state_ = DONE;
}

void OffThreadJob::cancel() {
  MOZ_ASSERT(state_ == RUNNING);
  MOZ_ASSERT(!stencil_);
  MOZ_ASSERT(!thread_, "cannot cancel after starting a thread");

  state_ = CANCELLED;
}

void OffThreadJob::waitUntilDone() {
  MOZ_ASSERT(state_ != CANCELLED);
  thread_->join();
}

already_AddRefed<JS::Stencil> OffThreadJob::stealStencil(JSContext* cx) {
  JS::FrontendContext* fc = fc_;
  fc_ = nullptr;
  auto destroyFrontendContext =
      mozilla::MakeScopeExit([&]() { JS::DestroyFrontendContext(fc); });

  MOZ_ASSERT(fc);

  if (JS::HadFrontendErrors(fc)) {
    (void)JS::ConvertFrontendErrorsToRuntimeErrors(cx, fc, options_);
    return nullptr;
  }

  if (!stencil_ && JS::IsTranscodeFailureResult(transcodeResult_)) {
    JS_ReportErrorASCII(cx, "failed to decode cache");
    return nullptr;
  }

  // Report warnings.
  if (!JS::ConvertFrontendErrorsToRuntimeErrors(cx, fc, options_)) {
    return nullptr;
  }

  return stencil_.forget();
}

struct ShellCompartmentPrivate {
  GCPtr<ArrayObject*> blackRoot;
  GCPtr<ArrayObject*> grayRoot;
};

struct MOZ_STACK_CLASS EnvironmentPreparer
    : public js::ScriptEnvironmentPreparer {
  explicit EnvironmentPreparer(JSContext* cx) {
    js::SetScriptEnvironmentPreparer(cx, this);
  }
  void invoke(JS::HandleObject global, Closure& closure) override;
};

const char* shell::selfHostedXDRPath = nullptr;
bool shell::encodeSelfHostedCode = false;
bool shell::enableCodeCoverage = false;
bool shell::enableDisassemblyDumps = false;
bool shell::offthreadBaselineCompilation = false;
bool shell::offthreadIonCompilation = false;
JS::DelazificationOption shell::defaultDelazificationMode =
    JS::DelazificationOption::OnDemandOnly;
bool shell::enableAsmJS = false;
bool shell::enableWasm = false;
bool shell::enableSharedMemory = SHARED_MEMORY_DEFAULT;
bool shell::enableWasmBaseline = false;
bool shell::enableWasmOptimizing = false;
bool shell::enableWasmVerbose = false;
bool shell::enableTestWasmAwaitTier2 = false;
bool shell::enableSourcePragmas = true;
bool shell::enableAsyncStacks = false;
bool shell::enableAsyncStackCaptureDebuggeeOnly = false;
bool shell::enableToSource = false;
bool shell::enableImportAttributes = false;
#ifdef JS_GC_ZEAL
uint32_t shell::gZealBits = 0;
uint32_t shell::gZealFrequency = 0;
#endif
bool shell::printTiming = false;
RCFile* shell::gErrFile = nullptr;
RCFile* shell::gOutFile = nullptr;
bool shell::reportWarnings = true;
bool shell::compileOnly = false;
bool shell::disableOOMFunctions = false;
bool shell::defaultToSameCompartment = true;

#ifdef DEBUG
bool shell::dumpEntrainedVariables = false;
bool shell::OOM_printAllocationCount = false;
#endif

MOZ_RUNINIT UniqueChars shell::processWideModuleLoadPath;

static bool SetTimeoutValue(JSContext* cx, double t);

static void KillWatchdog(JSContext* cx);

static bool ScheduleWatchdog(JSContext* cx, double t);

static void CancelExecution(JSContext* cx);

enum class ShellGlobalKind {
  GlobalObject,
  WindowProxy,
};

static JSObject* NewGlobalObject(JSContext* cx, JS::RealmOptions& options,
                                 JSPrincipals* principals, ShellGlobalKind kind,
                                 bool immutablePrototype);

/*
 * A toy WindowProxy class for the shell. This is intended for testing code
 * where global |this| is a WindowProxy. All requests are forwarded to the
 * underlying global and no navigation is supported.
 */
const JSClass ShellWindowProxyClass =
    PROXY_CLASS_DEF("ShellWindowProxy", JSCLASS_HAS_RESERVED_SLOTS(1));

JSObject* NewShellWindowProxy(JSContext* cx, JS::HandleObject global) {
  MOZ_ASSERT(global->is<GlobalObject>());

  js::WrapperOptions options;
  options.setClass(&ShellWindowProxyClass);

  JSAutoRealm ar(cx, global);
  JSObject* obj =
      js::Wrapper::New(cx, global, &js::Wrapper::singleton, options);
  MOZ_ASSERT_IF(obj, js::IsWindowProxy(obj));
  return obj;
}

/*
 * A toy principals type for the shell.
 *
 * In the shell, a principal is simply a 32-bit mask: P subsumes Q if the
 * set bits in P are a superset of those in Q. Thus, the principal 0 is
 * subsumed by everything, and the principal ~0 subsumes everything.
 *
 * As a special case, a null pointer as a principal is treated like 0xffff.
 *
 * The 'newGlobal' function takes an option indicating which principal the
 * new global should have; 'evaluate' does for the new code.
 */
class ShellPrincipals final : public JSPrincipals {
  uint32_t bits;

  static uint32_t getBits(JSPrincipals* p) {
    if (!p) {
      return 0xffff;
    }
    return static_cast<ShellPrincipals*>(p)->bits;
  }

 public:
  explicit ShellPrincipals(uint32_t bits, int32_t refcount = 0) : bits(bits) {
    this->refcount = refcount;
  }

  bool write(JSContext* cx, JSStructuredCloneWriter* writer) override {
    // The shell doesn't have a read principals hook, so it doesn't really
    // matter what we write here, but we have to write something so the
    // fuzzer is happy.
    return JS_WriteUint32Pair(writer, bits, 0);
  }

  bool isSystemOrAddonPrincipal() override { return true; }

  static void destroy(JSPrincipals* principals) {
    MOZ_ASSERT(principals != &fullyTrusted);
    MOZ_ASSERT(principals->refcount == 0);
    js_delete(static_cast<const ShellPrincipals*>(principals));
  }

  static bool subsumes(JSPrincipals* first, JSPrincipals* second) {
    uint32_t firstBits = getBits(first);
    uint32_t secondBits = getBits(second);
    return (firstBits | secondBits) == firstBits;
  }

  static JSSecurityCallbacks securityCallbacks;

  // Fully-trusted principals singleton.
  static ShellPrincipals fullyTrusted;
};

JSSecurityCallbacks ShellPrincipals::securityCallbacks = {
    nullptr,  // contentSecurityPolicyAllows
    nullptr,  // codeForEvalGets
    subsumes};

// The fully-trusted principal subsumes all other principals.
MOZ_RUNINIT ShellPrincipals ShellPrincipals::fullyTrusted(-1, 1);

#ifdef EDITLINE
extern "C" {
extern MOZ_EXPORT char* readline(const char* prompt);
extern MOZ_EXPORT void add_history(char* line);
}  // extern "C"
#endif

ShellContext::ShellContext(JSContext* cx, IsWorkerEnum isWorker_)
    : cx_(nullptr),
      isWorker(isWorker_),
      lastWarningEnabled(false),
      trackUnhandledRejections(true),
      timeoutInterval(-1.0),
      startTime(PRMJ_Now()),
      serviceInterrupt(false),
      haveInterruptFunc(false),
      interruptFunc(cx, NullValue()),
      lastWarning(cx, NullValue()),
      promiseRejectionTrackerCallback(cx, NullValue()),
      unhandledRejectedPromises(cx),
      watchdogLock(mutexid::ShellContextWatchdog),
      exitCode(0),
      quitting(false),
      readLineBufPos(0),
      errFilePtr(nullptr),
      outFilePtr(nullptr),
      offThreadMonitor(mutexid::ShellOffThreadState),
      finalizationRegistryCleanupCallbacks(cx) {}

ShellContext* js::shell::GetShellContext(JSContext* cx) {
  ShellContext* sc = static_cast<ShellContext*>(JS_GetContextPrivate(cx));
  MOZ_ASSERT(sc);
  return sc;
}

static void TraceRootArrays(JSTracer* trc, gc::MarkColor color) {
  JSRuntime* rt = trc->runtime();
  for (ZonesIter zone(rt, SkipAtoms); !zone.done(); zone.next()) {
    for (CompartmentsInZoneIter comp(zone); !comp.done(); comp.next()) {
      auto priv = static_cast<ShellCompartmentPrivate*>(
          JS_GetCompartmentPrivate(comp.get()));
      if (!priv) {
        continue;
      }

      GCPtr<ArrayObject*>& array =
          (color == gc::MarkColor::Black) ? priv->blackRoot : priv->grayRoot;
      TraceNullableEdge(trc, &array, "shell root array");

      if (array) {
        // Trace the array elements as part of root marking.
        for (uint32_t i = 0; i < array->getDenseInitializedLength(); i++) {
          Value& value = const_cast<Value&>(array->getDenseElement(i));
          TraceManuallyBarrieredEdge(trc, &value, "shell root array element");
        }
      }
    }
  }
}

static void TraceBlackRoots(JSTracer* trc, void* data) {
  TraceRootArrays(trc, gc::MarkColor::Black);
}

static bool TraceGrayRoots(JSTracer* trc, JS::SliceBudget& budget, void* data) {
  TraceRootArrays(trc, gc::MarkColor::Gray);
  return true;
}

static inline JSString* NewStringCopyUTF8(JSContext* cx, const char* chars) {
  return JS_NewStringCopyUTF8N(cx, JS::UTF8Chars(chars, strlen(chars)));
}

static mozilla::UniqueFreePtr<char[]> GetLine(FILE* file, const char* prompt) {
#ifdef EDITLINE
  /*
   * Use readline only if file is stdin, because there's no way to specify
   * another handle.  Are other filehandles interactive?
   */
  if (file == stdin) {
    mozilla::UniqueFreePtr<char[]> linep(readline(prompt));
    /*
     * We set it to zero to avoid complaining about inappropriate ioctl
     * for device in the case of EOF. Looks like errno == 251 if line is
     * finished with EOF and errno == 25 (EINVAL on Mac) if there is
     * nothing left to read.
     */
    if (errno == 251 || errno == 25 || errno == EINVAL) {
      errno = 0;
    }
    if (!linep) {
      return nullptr;
    }
    if (linep[0] != '\0') {
      add_history(linep.get());
    }
    return linep;
  }
#endif

  size_t len = 0;
  if (*prompt != '\0' && gOutFile->isOpen()) {
    fprintf(gOutFile->fp, "%s", prompt);
    fflush(gOutFile->fp);
  }

  size_t size = 80;
  mozilla::UniqueFreePtr<char[]> buffer(static_cast<char*>(malloc(size)));
  if (!buffer) {
    return nullptr;
  }

  char* current = buffer.get();
  do {
    while (true) {
      if (fgets(current, size - len, file)) {
        break;
      }
      if (errno != EINTR) {
        return nullptr;
      }
    }

    len += strlen(current);
    char* t = buffer.get() + len - 1;
    if (*t == '\n') {
      /* Line was read. We remove '\n' and exit. */
      *t = '\0';
      break;
    }

    if (len + 1 == size) {
      size = size * 2;
      char* raw = buffer.release();
      char* tmp = static_cast<char*>(realloc(raw, size));
      if (!tmp) {
        free(raw);
        return nullptr;
      }
      buffer.reset(tmp);
    }
    current = buffer.get() + len;
  } while (true);
  return buffer;
}

static bool ShellInterruptCallback(JSContext* cx) {
  ShellContext* sc = GetShellContext(cx);
  if (!sc->serviceInterrupt) {
    return true;
  }

  // Reset serviceInterrupt. CancelExecution or InterruptIf will set it to
  // true to distinguish watchdog or user triggered interrupts.
  // Do this first to prevent other interrupts that may occur while the
  // user-supplied callback is executing from re-entering the handler.
  sc->serviceInterrupt = false;

  bool result;
  if (sc->haveInterruptFunc) {
    bool wasAlreadyThrowing = cx->isExceptionPending();
    JS::AutoSaveExceptionState savedExc(cx);
    JSAutoRealm ar(cx, &sc->interruptFunc.toObject());
    RootedValue rval(cx);

    // Report any exceptions thrown by the JS interrupt callback, but do
    // *not* keep it on the cx. The interrupt handler is invoked at points
    // that are not expected to throw catchable exceptions, like at
    // JSOp::RetRval.
    //
    // If the interrupted JS code was already throwing, any exceptions
    // thrown by the interrupt handler are silently swallowed.
    {
      Maybe<AutoReportException> are;
      if (!wasAlreadyThrowing) {
        are.emplace(cx);
      }
      result = JS_CallFunctionValue(cx, nullptr, sc->interruptFunc,
                                    JS::HandleValueArray::empty(), &rval);
    }
    savedExc.restore();

    if (rval.isBoolean()) {
      result = rval.toBoolean();
    } else {
      result = false;
    }
  } else {
    result = false;
  }

  if (!result && sc->exitCode == 0) {
    static const char msg[] = "Script terminated by interrupt handler.\n";
    fputs(msg, stderr);

    sc->exitCode = EXITCODE_TIMEOUT;
  }

  return result;
}

static void GCSliceCallback(JSContext* cx, JS::GCProgress progress,
                            const JS::GCDescription& desc) {
  if (progress == JS::GC_CYCLE_END) {
#if defined(MOZ_MEMORY)
    // We call this here to match the browser's DOMGCSliceCallback.
    jemalloc_free_dirty_pages();
#endif
  }
}

/*
 * Some UTF-8 files, notably those written using Notepad, have a Unicode
 * Byte-Order-Mark (BOM) as their first character. This is useless (byte-order
 * is meaningless for UTF-8) but causes a syntax error unless we skip it.
 */
static void SkipUTF8BOM(FILE* file) {
  int ch1 = fgetc(file);
  int ch2 = fgetc(file);
  int ch3 = fgetc(file);

  // Skip the BOM
  if (ch1 == 0xEF && ch2 == 0xBB && ch3 == 0xBF) {
    return;
  }

  // No BOM - revert
  if (ch3 != EOF) {
    ungetc(ch3, file);
  }
  if (ch2 != EOF) {
    ungetc(ch2, file);
  }
  if (ch1 != EOF) {
    ungetc(ch1, file);
  }
}

void EnvironmentPreparer::invoke(HandleObject global, Closure& closure) {
  MOZ_ASSERT(JS_IsGlobalObject(global));

  JSContext* cx = TlsContext.get();
  MOZ_ASSERT(!JS_IsExceptionPending(cx));

  AutoRealm ar(cx, global);
  AutoReportException are(cx);
  if (!closure(cx)) {
    return;
  }
}

static bool RegisterScriptPathWithModuleLoader(JSContext* cx,
                                               HandleScript script,
                                               const char* filename) {
  // Set the private value associated with a script to a object containing the
  // script's filename so that the module loader can use it to resolve
  // relative imports.

  RootedString path(cx, NewStringCopyUTF8(cx, filename));
  if (!path) {
    return false;
  }

  MOZ_ASSERT(JS::GetScriptPrivate(script).isUndefined());
  RootedObject infoObject(cx, js::CreateScriptPrivate(cx, path));
  if (!infoObject) {
    return false;
  }

  JS::SetScriptPrivate(script, ObjectValue(*infoObject));
  return true;
}

enum class CompileUtf8 {
  InflateToUtf16,
  DontInflate,
};

[[nodiscard]] static bool RunFile(JSContext* cx, const char* filename,
                                  FILE* file, CompileUtf8 compileMethod,
                                  bool compileOnly, bool fullParse) {
  SkipUTF8BOM(file);

  int64_t t1 = PRMJ_Now();
  RootedScript script(cx);

  if (!filename) filename = "-";

  {
    CompileOptions options(cx);
    options.setIntroductionType("js shell file")
        .setFileAndLine(filename, 1)
        .setIsRunOnce(true)
        .setNoScriptRval(true);

    if (fullParse) {
      options.setForceFullParse();
    } else {
      options.setEagerDelazificationStrategy(defaultDelazificationMode);
    }

    if (compileMethod == CompileUtf8::DontInflate) {
      script = JS::CompileUtf8File(cx, options, file);
    } else {
      fprintf(stderr, "(compiling '%s' after inflating to UTF-16)\n", filename);

      FileContents buffer(cx);
      if (!ReadCompleteFile(cx, file, buffer)) {
        return false;
      }

      size_t length = buffer.length();
      auto chars = UniqueTwoByteChars(
          UTF8CharsToNewTwoByteCharsZ(
              cx,
              JS::UTF8Chars(reinterpret_cast<const char*>(buffer.begin()),
                            buffer.length()),
              &length, js::MallocArena)
              .get());
      if (!chars) {
        return false;
      }

      JS::SourceText<char16_t> source;
      if (!source.init(cx, std::move(chars), length)) {
        return false;
      }

      script = JS::Compile(cx, options, source);
    }

    if (!script) {
      return false;
    }
  }

  if (!RegisterScriptPathWithModuleLoader(cx, script, filename)) {
    return false;
  }

#ifdef DEBUG
  if (dumpEntrainedVariables) {
    AnalyzeEntrainedVariables(cx, script);
  }
#endif
  if (!compileOnly) {
    if (!JS_ExecuteScript(cx, script)) {
      return false;
    }
    int64_t t2 = PRMJ_Now() - t1;
    if (printTiming) {
      printf("runtime = %.3f ms\n", double(t2) / PRMJ_USEC_PER_MSEC);
    }
  }
  return true;
}

[[nodiscard]] static bool RunModule(JSContext* cx, const char* filename,
                                    bool compileOnly) {
  ShellContext* sc = GetShellContext(cx);

  RootedString path(cx, NewStringCopyUTF8(cx, filename));
  if (!path) {
    return false;
  }

  path = ResolvePath(cx, path, RootRelative);
  if (!path) {
    return false;
  }

  return sc->moduleLoader->loadRootModule(cx, path);
}

static void ShellCleanupFinalizationRegistryCallback(JSFunction* doCleanup,
                                                     JSObject* incumbentGlobal,
                                                     void* data) {
  // In the browser this queues a task. Shell jobs correspond to microtasks so
  // we arrange for cleanup to happen after all jobs/microtasks have run. The
  // incumbent global is ignored in the shell.

  auto sc = static_cast<ShellContext*>(data);
  AutoEnterOOMUnsafeRegion oomUnsafe;
  if (!sc->finalizationRegistryCleanupCallbacks.append(doCleanup)) {
    oomUnsafe.crash("ShellCleanupFinalizationRegistryCallback");
  }
}

// Run any FinalizationRegistry cleanup tasks and return whether any ran.
static bool MaybeRunFinalizationRegistryCleanupTasks(JSContext* cx) {
  ShellContext* sc = GetShellContext(cx);
  MOZ_ASSERT(!sc->quitting);

  Rooted<ShellContext::FunctionVector> callbacks(cx);
  std::swap(callbacks.get(), sc->finalizationRegistryCleanupCallbacks.get());

  bool ranTasks = false;

  RootedFunction callback(cx);
  for (JSFunction* f : callbacks) {
    callback = f;

    JS::ExposeObjectToActiveJS(callback);
    AutoRealm ar(cx, callback);

    {
      AutoReportException are(cx);
      RootedValue unused(cx);
      (void)JS_CallFunction(cx, nullptr, callback, HandleValueArray::empty(),
                            &unused);
    }

    ranTasks = true;

    if (sc->quitting) {
      break;
    }
  }

  return ranTasks;
}

static bool EnqueueJob(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!IsFunctionObject(args.get(0))) {
    JS_ReportErrorASCII(cx, "EnqueueJob's first argument must be a function");
    return false;
  }

  args.rval().setUndefined();

  RootedObject job(cx, &args[0].toObject());
  return js::EnqueueJob(cx, job);
}

static void RunShellJobs(JSContext* cx) {
  ShellContext* sc = GetShellContext(cx);
  if (sc->quitting) {
    return;
  }

  while (true) {
    // Run microtasks.
    js::RunJobs(cx);
    if (sc->quitting) {
      return;
    }

    // Run tasks (only finalization registry clean tasks are possible).
    bool ranTasks = MaybeRunFinalizationRegistryCleanupTasks(cx);
    if (!ranTasks) {
      break;
    }
  }
}

static bool DrainJobQueue(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (GetShellContext(cx)->quitting) {
    JS_ReportErrorASCII(
        cx, "Mustn't drain the job queue when the shell is quitting");
    return false;
  }

  if (cx->isEvaluatingModule != 0) {
    JS_ReportErrorASCII(
        cx,
        "Can't drain the job queue when executing the top level of a module");
    return false;
  }

  RunShellJobs(cx);

  if (GetShellContext(cx)->quitting) {
    return false;
  }

  args.rval().setUndefined();
  return true;
}

static bool GlobalOfFirstJobInQueue(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  RootedObject job(cx, cx->internalJobQueue->maybeFront());
  if (!job) {
    JS_ReportErrorASCII(cx, "Job queue is empty");
    return false;
  }

  RootedObject global(cx, &job->nonCCWGlobal());
  if (!cx->compartment()->wrap(cx, &global)) {
    return false;
  }

  args.rval().setObject(*global);
  return true;
}

static bool TrackUnhandledRejections(JSContext* cx, JS::HandleObject promise,
                                     JS::PromiseRejectionHandlingState state) {
  ShellContext* sc = GetShellContext(cx);
  if (!sc->trackUnhandledRejections) {
    return true;
  }

#if defined(DEBUG) || defined(JS_OOM_BREAKPOINT)
  if (cx->runningOOMTest) {
    // When OOM happens, we cannot reliably track the set of unhandled
    // promise rejections. Throw error only when simulated OOM is used
    // *and* promises are used in the test.
    JS_ReportErrorASCII(
        cx,
        "Can't track unhandled rejections while running simulated OOM "
        "test. Call ignoreUnhandledRejections before using oomTest etc.");
    return false;
  }
#endif

  if (!sc->unhandledRejectedPromises) {
    sc->unhandledRejectedPromises = SetObject::create(cx);
    if (!sc->unhandledRejectedPromises) {
      return false;
    }
  }

  RootedValue promiseVal(cx, ObjectValue(*promise));

  AutoRealm ar(cx, sc->unhandledRejectedPromises);
  if (!cx->compartment()->wrap(cx, &promiseVal)) {
    return false;
  }

  switch (state) {
    case JS::PromiseRejectionHandlingState::Unhandled:
      if (!sc->unhandledRejectedPromises->add(cx, promiseVal)) {
        return false;
      }
      break;
    case JS::PromiseRejectionHandlingState::Handled:
      bool deleted = false;
      if (!sc->unhandledRejectedPromises->delete_(cx, promiseVal, &deleted)) {
        return false;
      }
      // We can't MOZ_ASSERT(deleted) here, because it's possible we failed to
      // add the promise in the first place, due to OOM.
      break;
  }

  return true;
}

static void ForwardingPromiseRejectionTrackerCallback(
    JSContext* cx, bool mutedErrors, JS::HandleObject promise,
    JS::PromiseRejectionHandlingState state, void* data) {
  AutoReportException are(cx);

  if (!TrackUnhandledRejections(cx, promise, state)) {
    return;
  }

  RootedValue callback(cx,
                       GetShellContext(cx)->promiseRejectionTrackerCallback);
  if (callback.isNull()) {
    return;
  }

  AutoRealm ar(cx, &callback.toObject());

  FixedInvokeArgs<2> args(cx);
  args[0].setObject(*promise);
  args[1].setInt32(static_cast<int32_t>(state));

  if (!JS_WrapValue(cx, args[0])) {
    return;
  }

  RootedValue rval(cx);
  (void)Call(cx, callback, UndefinedHandleValue, args, &rval);
}

static bool SetPromiseRejectionTrackerCallback(JSContext* cx, unsigned argc,
                                               Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!IsFunctionObject(args.get(0))) {
    JS_ReportErrorASCII(
        cx,
        "setPromiseRejectionTrackerCallback expects a function as its sole "
        "argument");
    return false;
  }

  GetShellContext(cx)->promiseRejectionTrackerCallback = args[0];

  args.rval().setUndefined();
  return true;
}

static const char* telemetryNames[static_cast<int>(JSMetric::Count)] = {
#define LIT(NAME, _) #NAME,
    FOR_EACH_JS_METRIC(LIT)
#undef LIT
};

// Telemetry can be executed from multiple threads, and the callback is
// responsible to avoid contention on the recorded telemetry data.
class MOZ_RAII AutoLockTelemetry : public LockGuard<Mutex> {
  using Base = LockGuard<Mutex>;

  Mutex& getMutex() {
    static Mutex mutex(mutexid::ShellTelemetry);
    return mutex;
  }

 public:
  AutoLockTelemetry() : Base(getMutex()) {}
};

using TelemetrySamples = mozilla::Vector<uint32_t, 0, js::SystemAllocPolicy>;
MOZ_CONSTINIT static mozilla::Array<UniquePtr<TelemetrySamples>,
                                    size_t(JSMetric::Count)>
    recordedTelemetrySamples;

static void AccumulateTelemetryDataCallback(JSMetric id, uint32_t sample) {
  AutoLockTelemetry alt;
  TelemetrySamples* samples = recordedTelemetrySamples[size_t(id)].get();
  if (!samples) {
    return;
  }

  AutoEnterOOMUnsafeRegion oomUnsafe;
  if (!samples->append(sample)) {
    oomUnsafe.crash("AccumulateTelemetrySamplesCallback");
  }
}

static bool LookupTelemetryName(JSContext* cx, Handle<Value> nameValue,
                                JSMetric* resultOut) {
  static_assert(size_t(JSMetric::Count) == std::size(telemetryNames));
  MOZ_ASSERT(resultOut);

  JSString* str = ToString(cx, nameValue);
  if (!str) {
    return false;
  }

  UniqueChars name = EncodeLatin1(cx, str);
  if (!name) {
    return false;
  }

  for (size_t i = 0; i < std::size(telemetryNames); i++) {
    if (strcmp(telemetryNames[i], name.get()) == 0) {
      *resultOut = JSMetric(i);
      return true;
    }
  }

  return false;
}

static bool StartRecordingTelemetry(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() != 1) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              JSSMSG_INVALID_ARGS, "startRecordingTelemetry");
    return false;
  }

  JSMetric id;
  if (!LookupTelemetryName(cx, args[0], &id)) {
    JS_ReportErrorASCII(cx, "Telemetry probe name not found");
    return false;
  }

  if (recordedTelemetrySamples[size_t(id)]) {
    JS_ReportErrorASCII(cx, "Already recording telemetry for this probe");
    return false;
  }

  auto samples = MakeUnique<TelemetrySamples>();
  if (!samples) {
    ReportOutOfMemory(cx);
    return false;
  }

  recordedTelemetrySamples[size_t(id)] = std::move(samples);
  args.rval().setUndefined();
  return true;
}

static bool StopRecordingTelemetry(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() != 1) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              JSSMSG_INVALID_ARGS, "stopRecordingTelemetry");
    return false;
  }

  JSMetric id;
  if (!LookupTelemetryName(cx, args[0], &id)) {
    JS_ReportErrorASCII(cx, "Telemetry probe name not found");
    return false;
  }

  if (!recordedTelemetrySamples[size_t(id)]) {
    JS_ReportErrorASCII(cx, "Not recording telemetry for this probe");
    return false;
  }

  recordedTelemetrySamples[size_t(id)].reset();
  args.rval().setUndefined();
  return true;
}

static bool GetTelemetrySamples(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() != 1) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              JSSMSG_INVALID_ARGS, "getTelemetrySamples");
    return false;
  }

  JSMetric id;
  if (!LookupTelemetryName(cx, args[0], &id)) {
    JS_ReportErrorASCII(cx, "Telemetry probe name not found");
    return false;
  }

  TelemetrySamples* samples = recordedTelemetrySamples[size_t(id)].get();
  if (!samples) {
    args.rval().setUndefined();
    return true;
  }

  Rooted<ArrayObject*> array(cx, NewDenseEmptyArray(cx));
  if (!array) {
    return false;
  }

  for (uint32_t sample : *samples) {
    if (!NewbornArrayPush(cx, array, NumberValue(sample))) {
      return false;
    }
  }

  args.rval().setObject(*array);
  return true;
}

// Use Counter introspection
MOZ_RUNINIT static Mutex useCounterLock(mutexid::ShellUseCounters);
class MOZ_RAII AutoLockUseCounters : public LockGuard<Mutex> {
  using Base = LockGuard<Mutex>;

 public:
  AutoLockUseCounters() : Base(useCounterLock) {}
};

using UseCounterArray =
    mozilla::Array<uint32_t, static_cast<size_t>(JSUseCounter::COUNT)>;
static UseCounterArray useCounterResults;
static void SetUseCounterCallback(JSObject* obj, JSUseCounter counter) {
  MOZ_RELEASE_ASSERT(obj);
  AutoLockUseCounters aluc;
  // Maybe should ensure obj is a global object?
  useCounterResults[static_cast<size_t>(counter)]++;
}

static bool GetUseCounterResults(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  Rooted<JSObject*> obj(cx, JS_NewPlainObject(cx));
  if (!obj) {
    return false;
  }

  // Make a private copy holding the lock then release, because we can't
  // hold this mutex while doing JS_DefineProperty, which holds MemoryTracker
  // mutex.
  UseCounterArray local;
  {
    AutoLockUseCounters aluc;
    local = useCounterResults;
  }

  RootedValue val(cx);
#define ADD_VALUE(ENUM, NAME)                                      \
  val.setInt32(local[static_cast<size_t>(JSUseCounter::ENUM)]);    \
  if (!JS_DefineProperty(cx, obj, #NAME, val, JSPROP_ENUMERATE)) { \
    return false;                                                  \
  }

  FOR_EACH_JS_USE_COUNTER(ADD_VALUE);

  args.rval().setObject(*obj);
  return true;
}

static bool BoundToAsyncStack(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  RootedValue function(cx, GetFunctionNativeReserved(&args.callee(), 0));
  RootedObject options(
      cx, &GetFunctionNativeReserved(&args.callee(), 1).toObject());

  Rooted<SavedFrame*> stack(cx, nullptr);
  bool isExplicit;

  RootedValue v(cx);

  if (!JS_GetProperty(cx, options, "stack", &v)) {
    return false;
  }
  if (!v.isObject() || !v.toObject().is<SavedFrame>()) {
    JS_ReportErrorASCII(cx,
                        "The 'stack' property must be a SavedFrame object.");
    return false;
  }
  stack = &v.toObject().as<SavedFrame>();

  if (!JS_GetProperty(cx, options, "cause", &v)) {
    return false;
  }
  RootedString causeString(cx, ToString(cx, v));
  if (!causeString) {
    return false;
  }

  UniqueChars cause = JS_EncodeStringToUTF8(cx, causeString);
  if (!cause) {
    MOZ_ASSERT(cx->isExceptionPending());
    return false;
  }

  if (!JS_GetProperty(cx, options, "explicit", &v)) {
    return false;
  }
  isExplicit = v.isUndefined() ? true : ToBoolean(v);

  auto kind =
      (isExplicit ? JS::AutoSetAsyncStackForNewCalls::AsyncCallKind::EXPLICIT
                  : JS::AutoSetAsyncStackForNewCalls::AsyncCallKind::IMPLICIT);

  JS::AutoSetAsyncStackForNewCalls asasfnckthxbye(cx, stack, cause.get(), kind);
  return Call(cx, UndefinedHandleValue, function, JS::HandleValueArray::empty(),
              args.rval());
}

static bool BindToAsyncStack(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() != 2) {
    JS_ReportErrorASCII(cx, "bindToAsyncStack takes exactly two arguments.");
    return false;
  }

  if (!args[0].isObject() || !IsCallable(args[0])) {
    JS_ReportErrorASCII(
        cx, "bindToAsyncStack's first argument should be a function.");
    return false;
  }

  if (!args[1].isObject()) {
    JS_ReportErrorASCII(
        cx, "bindToAsyncStack's second argument should be an object.");
    return false;
  }

  RootedFunction bound(cx, NewFunctionWithReserved(cx, BoundToAsyncStack, 0, 0,
                                                   "bindToAsyncStack thunk"));
  if (!bound) {
    return false;
  }
  SetFunctionNativeReserved(bound, 0, args[0]);
  SetFunctionNativeReserved(bound, 1, args[1]);

  args.rval().setObject(*bound);
  return true;
}

#ifdef JS_HAS_INTL_API
static bool AddIntlExtras(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (!args.get(0).isObject()) {
    JS_ReportErrorASCII(cx, "addIntlExtras must be passed an object");
    return false;
  }
  JS::RootedObject intl(cx, &args[0].toObject());

  static const JSFunctionSpec funcs[] = {
      JS_SELF_HOSTED_FN("getCalendarInfo", "Intl_getCalendarInfo", 1, 0),
      JS_FS_END,
  };

  if (!JS_DefineFunctions(cx, intl, funcs)) {
    return false;
  }

  if (!JS::AddMozDateTimeFormatConstructor(cx, intl)) {
    return false;
  }

  if (!JS::AddMozDisplayNamesConstructor(cx, intl)) {
    return false;
  }

  args.rval().setUndefined();
  return true;
}
#endif  // JS_HAS_INTL_API

[[nodiscard]] static bool EvalUtf8AndPrint(JSContext* cx, const char* bytes,
                                           size_t length, int lineno,
                                           bool compileOnly) {
  // Eval.
  JS::CompileOptions options(cx);
  options.setIntroductionType("js shell interactive")
      .setIsRunOnce(true)
      .setFileAndLine("typein", lineno)
      .setEagerDelazificationStrategy(defaultDelazificationMode);

  JS::SourceText<Utf8Unit> srcBuf;
  if (!srcBuf.init(cx, bytes, length, JS::SourceOwnership::Borrowed)) {
    return false;
  }

  RootedScript script(cx, JS::Compile(cx, options, srcBuf));
  if (!script) {
    return false;
  }
  if (compileOnly) {
    return true;
  }
  RootedValue result(cx);
  if (!JS_ExecuteScript(cx, script, &result)) {
    return false;
  }

  if (!result.isUndefined() && gOutFile->isOpen()) {
    // Print.
    RootedString str(cx, JS_ValueToSource(cx, result));
    if (!str) {
      return false;
    }

    UniqueChars utf8chars = JS_EncodeStringToUTF8(cx, str);
    if (!utf8chars) {
      return false;
    }
    fprintf(gOutFile->fp, "%s\n", utf8chars.get());
  }
  return true;
}

[[nodiscard]] static bool ReadEvalPrintLoop(JSContext* cx, FILE* in,
                                            bool compileOnly) {
  ShellContext* sc = GetShellContext(cx);
  int lineno = 1;
  bool hitEOF = false;

  do {
    /*
     * Accumulate lines until we get a 'compilable unit' - one that either
     * generates an error (before running out of source) or that compiles
     * cleanly.  This should be whenever we get a complete statement that
     * coincides with the end of a line.
     */
    int startline = lineno;
    using CharBuffer = Vector<char, 32>;
    RootedObject globalLexical(cx, &cx->global()->lexicalEnvironment());
    CharBuffer buffer(cx);
    do {
      ScheduleWatchdog(cx, -1);
      sc->serviceInterrupt = false;
      errno = 0;

      mozilla::UniqueFreePtr<char[]> line =
          GetLine(in, startline == lineno ? "js> " : "");
      if (!line) {
        if (errno) {
          if (UniqueChars error = SystemErrorMessage(cx, errno)) {
            JS_ReportErrorUTF8(cx, "%s", error.get());
          }
          return false;
        }
        hitEOF = true;
        break;
      }

      if (!buffer.append(line.get(), strlen(line.get())) ||
          !buffer.append('\n')) {
        return false;
      }

      lineno++;
      if (!ScheduleWatchdog(cx, sc->timeoutInterval)) {
        hitEOF = true;
        break;
      }
    } while (!JS_Utf8BufferIsCompilableUnit(cx, cx->global(), buffer.begin(),
                                            buffer.length()));

    if (hitEOF && buffer.empty()) {
      break;
    }

    {
      // Report exceptions but keep going.
      AutoReportException are(cx);
      (void)EvalUtf8AndPrint(cx, buffer.begin(), buffer.length(), startline,
                             compileOnly);
    }

    // If a let or const fail to initialize they will remain in an unusable
    // without further intervention. This call cleans up the global scope,
    // setting uninitialized lexicals to undefined so that they may still
    // be used. This behavior is _only_ acceptable in the context of the repl.
    if (JS::ForceLexicalInitialization(cx, globalLexical) &&
        gErrFile->isOpen()) {
      fputs(
          "Warning: According to the standard, after the above exception,\n"
          "Warning: the global bindings should be permanently uninitialized.\n"
          "Warning: We have non-standard-ly initialized them to `undefined`"
          "for you.\nWarning: This nicety only happens in the JS shell.\n",
          stderr);
    }

    RunShellJobs(cx);
  } while (!hitEOF && !sc->quitting);

  if (gOutFile->isOpen()) {
    fprintf(gOutFile->fp, "\n");
  }

  return true;
}

enum FileKind {
  PreludeScript,    // UTF-8 script, fully-parsed, to avoid conflicting
                    // configurations.
  FileScript,       // UTF-8, directly parsed as such
  FileScriptUtf16,  // FileScript, but inflate to UTF-16 before parsing
  FileModule,
};

[[nodiscard]] static bool Process(JSContext* cx, const char* filename,
                                  bool forceTTY, FileKind kind) {
  FILE* file;
  if (forceTTY || !filename || strcmp(filename, "-") == 0) {
    file = stdin;
  } else {
    file = OpenFile(cx, filename, "rb");
    if (!file) {
      return false;
    }
  }
  AutoCloseFile autoClose(file);

  bool fullParse = false;
  if (!forceTTY && !isatty(fileno(file))) {
    // It's not interactive - just execute it.
    switch (kind) {
      case PreludeScript:
        fullParse = true;
        if (!RunFile(cx, filename, file, CompileUtf8::DontInflate, compileOnly,
                     fullParse)) {
          return false;
        }
        break;
      case FileScript:
        if (!RunFile(cx, filename, file, CompileUtf8::DontInflate, compileOnly,
                     fullParse)) {
          return false;
        }
        break;
      case FileScriptUtf16:
        if (!RunFile(cx, filename, file, CompileUtf8::InflateToUtf16,
                     compileOnly, fullParse)) {
          return false;
        }
        break;
      case FileModule:
        if (!RunModule(cx, filename, compileOnly)) {
          return false;
        }
        break;
      default:
        MOZ_CRASH("Impossible FileKind!");
    }
  } else {
    // It's an interactive filehandle; drop into read-eval-print loop.
    MOZ_ASSERT(kind == FileScript);
    if (!ReadEvalPrintLoop(cx, file, compileOnly)) {
      return false;
    }
  }
#ifdef FUZZING_JS_FUZZILLI
  fprintf(stderr, "executionHash is 0x%x with %d inputs\n", cx->executionHash,
          cx->executionHashInputs);
#endif
  return true;
}

#ifdef XP_WIN
#  define GET_FD_FROM_FILE(a) int(_get_osfhandle(fileno(a)))
#else
#  define GET_FD_FROM_FILE(a) fileno(a)
#endif

static void freeExternalCallback(void* contents, void* userData) {
  MOZ_ASSERT(!userData);
  js_free(contents);
}

static bool CreateExternalArrayBuffer(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() != 1) {
    JS_ReportErrorNumberASCII(
        cx, my_GetErrorMessage, nullptr,
        args.length() < 1 ? JSSMSG_NOT_ENOUGH_ARGS : JSSMSG_TOO_MANY_ARGS,
        "createExternalArrayBuffer");
    return false;
  }

  int32_t bytes = 0;
  if (!ToInt32(cx, args[0], &bytes)) {
    return false;
  }

  if (bytes < 0) {
    JS_ReportErrorASCII(cx, "Size must be non-negative");
    return false;
  }

  void* buffer = js_calloc(bytes);
  if (!buffer) {
    JS_ReportOutOfMemory(cx);
    return false;
  }

  UniquePtr<void, JS::BufferContentsDeleter> ptr{buffer,
                                                 {&freeExternalCallback}};
  RootedObject arrayBuffer(
      cx, JS::NewExternalArrayBuffer(cx, bytes, std::move(ptr)));
  if (!arrayBuffer) {
    return false;
  }

  args.rval().setObject(*arrayBuffer);
  return true;
}

static bool CreateMappedArrayBuffer(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() < 1 || args.length() > 3) {
    JS_ReportErrorNumberASCII(
        cx, my_GetErrorMessage, nullptr,
        args.length() < 1 ? JSSMSG_NOT_ENOUGH_ARGS : JSSMSG_TOO_MANY_ARGS,
        "createMappedArrayBuffer");
    return false;
  }

  RootedString rawFilenameStr(cx, JS::ToString(cx, args[0]));
  if (!rawFilenameStr) {
    return false;
  }
  // It's a little bizarre to resolve relative to the script, but for testing
  // I need a file at a known location, and the only good way I know of to do
  // that right now is to include it in the repo alongside the test script.
  // Bug 944164 would introduce an alternative.
  Rooted<JSString*> filenameStr(
      cx, ResolvePath(cx, rawFilenameStr, ScriptRelative));
  if (!filenameStr) {
    return false;
  }
  UniqueChars filename = JS_EncodeStringToUTF8(cx, filenameStr);
  if (!filename) {
    return false;
  }

  uint32_t offset = 0;
  if (args.length() >= 2) {
    if (!JS::ToUint32(cx, args[1], &offset)) {
      return false;
    }
  }

  bool sizeGiven = false;
  uint32_t size;
  if (args.length() >= 3) {
    if (!JS::ToUint32(cx, args[2], &size)) {
      return false;
    }
    sizeGiven = true;
    if (size == 0) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_BAD_ARRAY_LENGTH);
      return false;
    }
  }

  FILE* file = OpenFile(cx, filename.get(), "rb");
  if (!file) {
    return false;
  }
  AutoCloseFile autoClose(file);

  struct stat st;
  if (fstat(fileno(file), &st) < 0) {
    JS_ReportErrorASCII(cx, "Unable to stat file");
    return false;
  }

  if ((st.st_mode & S_IFMT) != S_IFREG) {
    JS_ReportErrorASCII(cx, "Path is not a regular file");
    return false;
  }

  if (!sizeGiven) {
    if (off_t(offset) >= st.st_size) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_OFFSET_LARGER_THAN_FILESIZE);
      return false;
    }
    size = st.st_size - offset;
  }

  void* contents =
      JS::CreateMappedArrayBufferContents(GET_FD_FROM_FILE(file), offset, size);
  if (!contents) {
    JS_ReportErrorASCII(cx,
                        "failed to allocate mapped array buffer contents "
                        "(possibly due to bad alignment)");
    return false;
  }

  RootedObject obj(cx,
                   JS::NewMappedArrayBufferWithContents(cx, size, contents));
  if (!obj) {
    return false;
  }

  args.rval().setObject(*obj);
  return true;
}

#undef GET_FD_FROM_FILE

class UserBufferObject : public NativeObject {
  static const uint32_t BUFFER_SLOT = 0;
  static const uint32_t BYTE_LENGTH_SLOT = 1;
  static const uint32_t RESERVED_SLOTS = 2;

  static constexpr auto BufferMemoryUse = MemoryUse::Embedding1;

  static void finalize(JS::GCContext* gcx, JSObject* obj);

 public:
  static const JSClassOps classOps_;
  static const JSClass class_;

  [[nodiscard]] static UserBufferObject* create(JSContext* cx,
                                                size_t byteLength);

  void* buffer() const {
    auto& buffer = getReservedSlot(BUFFER_SLOT);
    if (buffer.isUndefined()) {
      return nullptr;
    }
    return buffer.toPrivate();
  }

  size_t byteLength() const {
    return size_t(getReservedSlot(BYTE_LENGTH_SLOT).toPrivate());
  }
};

const JSClassOps UserBufferObject::classOps_ = {
    nullptr,                     // addProperty
    nullptr,                     // delProperty
    nullptr,                     // enumerate
    nullptr,                     // newEnumerate
    nullptr,                     // resolve
    nullptr,                     // mayResolve
    UserBufferObject::finalize,  // finalize
    nullptr,                     // call
    nullptr,                     // construct
    nullptr,                     // trace
};

const JSClass UserBufferObject::class_ = {
    "UserBufferObject",
    JSCLASS_HAS_RESERVED_SLOTS(UserBufferObject::RESERVED_SLOTS) |
        JSCLASS_BACKGROUND_FINALIZE,
    &UserBufferObject::classOps_,
};

UserBufferObject* UserBufferObject::create(JSContext* cx, size_t byteLength) {
  void* buffer = js_calloc(byteLength);
  if (!buffer) {
    JS_ReportOutOfMemory(cx);
    return nullptr;
  }
  UniquePtr<void, JS::FreePolicy> ptr(buffer);

  auto* userBuffer = NewObjectWithGivenProto<UserBufferObject>(cx, nullptr);
  if (!userBuffer) {
    return nullptr;
  }

  InitReservedSlot(userBuffer, BUFFER_SLOT, ptr.release(), byteLength,
                   BufferMemoryUse);
  userBuffer->initReservedSlot(BYTE_LENGTH_SLOT, PrivateValue(byteLength));

  return userBuffer;
}

void UserBufferObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  auto* userBuffer = &obj->as<UserBufferObject>();
  if (auto* buffer = userBuffer->buffer()) {
    gcx->free_(userBuffer, buffer, userBuffer->byteLength(), BufferMemoryUse);
  }
}

static bool CreateUserArrayBuffer(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() != 1) {
    JS_ReportErrorNumberASCII(
        cx, my_GetErrorMessage, nullptr,
        args.length() < 1 ? JSSMSG_NOT_ENOUGH_ARGS : JSSMSG_TOO_MANY_ARGS,
        "createUserArrayBuffer");
    return false;
  }

  int32_t bytes = 0;
  if (!ToInt32(cx, args[0], &bytes)) {
    return false;
  }
  if (bytes < 0) {
    JS_ReportErrorASCII(cx, "Size must be non-negative");
    return false;
  }

  Rooted<UserBufferObject*> userBuffer(cx, UserBufferObject::create(cx, bytes));
  if (!userBuffer) {
    return false;
  }

  Rooted<JSObject*> arrayBuffer(
      cx, JS::NewArrayBufferWithUserOwnedContents(cx, userBuffer->byteLength(),
                                                  userBuffer->buffer()));
  if (!arrayBuffer) {
    return false;
  }

  // Create a strong reference from |arrayBuffer| to |userBuffer|. This ensures
  // |userBuffer| can't outlive |arrayBuffer|. That way we don't have to worry
  // about detaching the ArrayBuffer object when |userBuffer| gets finalized.
  // The reference is made through a private name, because we don't want to
  // expose |userBuffer| to user-code.

  auto* privateName = NewPrivateName(cx, cx->names().empty_.toHandle());
  if (!privateName) {
    return false;
  }

  Rooted<PropertyKey> id(cx, PropertyKey::Symbol(privateName));
  Rooted<JS::Value> userBufferVal(cx, ObjectValue(*userBuffer));
  if (!js::DefineDataProperty(cx, arrayBuffer, id, userBufferVal, 0)) {
    return false;
  }

  args.rval().setObject(*arrayBuffer);
  return true;
}

static bool AddPromiseReactions(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() != 3) {
    JS_ReportErrorNumberASCII(
        cx, my_GetErrorMessage, nullptr,
        args.length() < 3 ? JSSMSG_NOT_ENOUGH_ARGS : JSSMSG_TOO_MANY_ARGS,
        "addPromiseReactions");
    return false;
  }

  RootedObject promise(cx);
  if (args[0].isObject()) {
    promise = &args[0].toObject();
  }

  if (!promise || !JS::IsPromiseObject(promise)) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              JSSMSG_INVALID_ARGS, "addPromiseReactions");
    return false;
  }

  RootedObject onResolve(cx);
  if (args[1].isObject()) {
    onResolve = &args[1].toObject();
  }

  RootedObject onReject(cx);
  if (args[2].isObject()) {
    onReject = &args[2].toObject();
  }

  if (!onResolve || !onResolve->is<JSFunction>() || !onReject ||
      !onReject->is<JSFunction>()) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              JSSMSG_INVALID_ARGS, "addPromiseReactions");
    return false;
  }

  return JS::AddPromiseReactions(cx, promise, onResolve, onReject);
}

static bool IgnoreUnhandledRejections(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  ShellContext* sc = GetShellContext(cx);
  sc->trackUnhandledRejections = false;

  args.rval().setUndefined();
  return true;
}

static bool Options(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  JS::ContextOptions oldContextOptions = JS::ContextOptionsRef(cx);
  for (unsigned i = 0; i < args.length(); i++) {
    RootedString str(cx, JS::ToString(cx, args[i]));
    if (!str) {
      return false;
    }

    Rooted<JSLinearString*> opt(cx, str->ensureLinear(cx));
    if (!opt) {
      return false;
    }

    if (StringEqualsLiteral(opt, "throw_on_asmjs_validation_failure")) {
      JS::ContextOptionsRef(cx).toggleThrowOnAsmJSValidationFailure();
    } else {
      UniqueChars optChars = QuoteString(cx, opt, '"');
      if (!optChars) {
        return false;
      }

      JS_ReportErrorASCII(cx,
                          "unknown option name %s."
                          " The valid name is "
                          "throw_on_asmjs_validation_failure.",
                          optChars.get());
      return false;
    }
  }

  UniqueChars names = DuplicateString("");
  bool found = false;
  if (names && oldContextOptions.throwOnAsmJSValidationFailure()) {
    names = JS_sprintf_append(std::move(names), "%s%s", found ? "," : "",
                              "throw_on_asmjs_validation_failure");
    found = true;
  }
  if (!names) {
    JS_ReportOutOfMemory(cx);
    return false;
  }

  JSString* str = JS_NewStringCopyZ(cx, names.get());
  if (!str) {
    return false;
  }
  args.rval().setString(str);
  return true;
}

static bool LoadScript(JSContext* cx, unsigned argc, Value* vp,
                       bool scriptRelative) {
  CallArgs args = CallArgsFromVp(argc, vp);

  RootedString str(cx);
  for (unsigned i = 0; i < args.length(); i++) {
    str = JS::ToString(cx, args[i]);
    if (!str) {
      JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                                JSSMSG_INVALID_ARGS, "load");
      return false;
    }

    str = ResolvePath(cx, str, scriptRelative ? ScriptRelative : RootRelative);
    if (!str) {
      JS_ReportErrorASCII(cx, "unable to resolve path");
      return false;
    }

    UniqueChars filename = JS_EncodeStringToUTF8(cx, str);
    if (!filename) {
      return false;
    }

    errno = 0;

    CompileOptions opts(cx);
    opts.setIntroductionType("js shell load")
        .setIsRunOnce(true)
        .setNoScriptRval(true)
        .setEagerDelazificationStrategy(defaultDelazificationMode);

    RootedValue unused(cx);
    if (!(compileOnly
              ? JS::CompileUtf8Path(cx, opts, filename.get()) != nullptr
              : JS::EvaluateUtf8Path(cx, opts, filename.get(), &unused))) {
      return false;
    }
  }

  args.rval().setUndefined();
  return true;
}

static bool Load(JSContext* cx, unsigned argc, Value* vp) {
  return LoadScript(cx, argc, vp, false);
}

static bool LoadScriptRelativeToScript(JSContext* cx, unsigned argc,
                                       Value* vp) {
  return LoadScript(cx, argc, vp, true);
}

static void my_LargeAllocFailCallback() {
  JSContext* cx = TlsContext.get();
  if (!cx) {
    return;
  }

  MOZ_ASSERT(!JS::RuntimeHeapIsBusy());

  JS::PrepareForFullGC(cx);
  cx->runtime()->gc.gc(JS::GCOptions::Shrink,
                       JS::GCReason::SHARED_MEMORY_LIMIT);
}

static const uint32_t CacheEntry_SOURCE = 0;
static const uint32_t CacheEntry_BYTECODE = 1;
static const uint32_t CacheEntry_OPTIONS = 2;

// Some compile options can't be combined differently between save and load.
//
// CacheEntries store a CacheOption set, and on load an exception is thrown
// if the entries are incompatible.

enum CacheOptions : uint32_t {
  IsRunOnce,
  NoScriptRval,
  Global,
  NonSyntactic,
  SourceIsLazy,
  ForceFullParse,
};

struct CacheOptionSet : public mozilla::EnumSet<CacheOptions> {
  using mozilla::EnumSet<CacheOptions>::EnumSet;

  explicit CacheOptionSet(const CompileOptions& options) : EnumSet() {
    initFromOptions(options);
  }

  void initFromOptions(const CompileOptions& options) {
    if (options.noScriptRval) {
      *this += CacheOptions::NoScriptRval;
    }
    if (options.isRunOnce) {
      *this += CacheOptions::IsRunOnce;
    }
    if (options.sourceIsLazy) {
      *this += CacheOptions::SourceIsLazy;
    }
    if (options.forceFullParse()) {
      *this += CacheOptions::ForceFullParse;
    }
    if (options.nonSyntacticScope) {
      *this += CacheOptions::NonSyntactic;
    }
  }
};

static bool CacheOptionsCompatible(const CacheOptionSet& a,
                                   const CacheOptionSet& b) {
  // If the options are identical, they are trivially compatible.
  return a == b;
}

static const JSClass CacheEntry_class = {
    "CacheEntryObject",
    JSCLASS_HAS_RESERVED_SLOTS(3),
};

static bool CacheEntry(JSContext* cx, unsigned argc, JS::Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() != 1 || !args[0].isString()) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              JSSMSG_INVALID_ARGS, "CacheEntry");
    return false;
  }

  RootedObject obj(cx, JS_NewObject(cx, &CacheEntry_class));
  if (!obj) {
    return false;
  }

  JS::SetReservedSlot(obj, CacheEntry_SOURCE, args[0]);
  JS::SetReservedSlot(obj, CacheEntry_BYTECODE, UndefinedValue());

  // Fill in empty option set.
  CacheOptionSet defaultOptions;
  JS::SetReservedSlot(obj, CacheEntry_OPTIONS,
                      Int32Value(defaultOptions.serialize()));

  args.rval().setObject(*obj);
  return true;
}

static bool CacheEntry_isCacheEntry(JSObject* cache) {
  return cache->hasClass(&CacheEntry_class);
}

static JSString* CacheEntry_getSource(JSContext* cx, HandleObject cache) {
  MOZ_ASSERT(CacheEntry_isCacheEntry(cache));
  Value v = JS::GetReservedSlot(cache, CacheEntry_SOURCE);
  if (!v.isString()) {
    JS_ReportErrorASCII(
        cx, "CacheEntry_getSource: Unexpected type of source reserved slot.");
    return nullptr;
  }

  return v.toString();
}

static bool CacheEntry_compatible(JSContext* cx, HandleObject cache,
                                  const CacheOptionSet& currentOptionSet) {
  CacheOptionSet cacheEntryOptions;
  MOZ_ASSERT(CacheEntry_isCacheEntry(cache));
  Value v = JS::GetReservedSlot(cache, CacheEntry_OPTIONS);
  cacheEntryOptions.deserialize(v.toInt32());
  if (!CacheOptionsCompatible(cacheEntryOptions, currentOptionSet)) {
    JS_ReportErrorASCII(cx,
                        "CacheEntry_compatible: Incompatible cache contents");
    return false;
  }
  return true;
}

static uint8_t* CacheEntry_getBytecode(JSContext* cx, HandleObject cache,
                                       size_t* length) {
  MOZ_ASSERT(CacheEntry_isCacheEntry(cache));
  Value v = JS::GetReservedSlot(cache, CacheEntry_BYTECODE);
  if (!v.isObject() || !v.toObject().is<ArrayBufferObject>()) {
    JS_ReportErrorASCII(
        cx,
        "CacheEntry_getBytecode: Unexpected type of bytecode reserved slot.");
    return nullptr;
  }

  ArrayBufferObject* arrayBuffer = &v.toObject().as<ArrayBufferObject>();
  *length = arrayBuffer->byteLength();
  return arrayBuffer->dataPointer();
}

static bool CacheEntry_setBytecode(JSContext* cx, HandleObject cache,
                                   const CacheOptionSet& cacheOptions,
                                   uint8_t* buffer, uint32_t length) {
  MOZ_ASSERT(CacheEntry_isCacheEntry(cache));

  using BufferContents = ArrayBufferObject::BufferContents;

  BufferContents contents = BufferContents::createMallocedUnknownArena(buffer);
  Rooted<ArrayBufferObject*> arrayBuffer(
      cx, ArrayBufferObject::createForContents(cx, length, contents));
  if (!arrayBuffer) {
    return false;
  }

  JS::SetReservedSlot(cache, CacheEntry_BYTECODE, ObjectValue(*arrayBuffer));
  JS::SetReservedSlot(cache, CacheEntry_OPTIONS,
                      Int32Value(cacheOptions.serialize()));
  return true;
}

static bool ConvertTranscodeResultToJSException(JSContext* cx,
                                                JS::TranscodeResult rv) {
  switch (rv) {
    case JS::TranscodeResult::Ok:
      return true;

    default:
      [[fallthrough]];
    case JS::TranscodeResult::Failure:
      MOZ_ASSERT(!cx->isExceptionPending());
      JS_ReportErrorASCII(cx, "generic warning");
      return false;
    case JS::TranscodeResult::Failure_BadBuildId:
      MOZ_ASSERT(!cx->isExceptionPending());
      JS_ReportErrorASCII(cx, "the build-id does not match");
      return false;
    case JS::TranscodeResult::Failure_AsmJSNotSupported:
      MOZ_ASSERT(!cx->isExceptionPending());
      JS_ReportErrorASCII(cx, "Asm.js is not supported by XDR");
      return false;
    case JS::TranscodeResult::Failure_BadDecode:
      MOZ_ASSERT(!cx->isExceptionPending());
      JS_ReportErrorASCII(cx, "XDR data corruption");
      return false;

    case JS::TranscodeResult::Throw:
      MOZ_ASSERT(cx->isExceptionPending());
      return false;
  }
}

static void SetQuitting(JSContext* cx, int32_t code) {
  ShellContext* sc = GetShellContext(cx);
  js::StopDrainingJobQueue(cx);
  sc->exitCode = code;
  sc->quitting = true;
}

static void UnsetQuitting(JSContext* cx) {
  ShellContext* sc = GetShellContext(cx);
  js::RestartDrainingJobQueue(cx);
  sc->exitCode = 0;
  sc->quitting = false;
}

static bool Evaluate(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() < 1 || args.length() > 2) {
    JS_ReportErrorNumberASCII(
        cx, my_GetErrorMessage, nullptr,
        args.length() < 1 ? JSSMSG_NOT_ENOUGH_ARGS : JSSMSG_TOO_MANY_ARGS,
        "evaluate");
    return false;
  }

  RootedString code(cx, nullptr);
  RootedObject cacheEntry(cx, nullptr);
  if (args[0].isString()) {
    code = args[0].toString();
  } else if (args[0].isObject() &&
             CacheEntry_isCacheEntry(&args[0].toObject())) {
    cacheEntry = &args[0].toObject();
    code = CacheEntry_getSource(cx, cacheEntry);
    if (!code) {
      return false;
    }
  }

  if (!code || (args.length() == 2 && args[1].isPrimitive())) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              JSSMSG_INVALID_ARGS, "evaluate");
    return false;
  }

  RootedObject opts(cx);
  if (args.length() == 2) {
    if (!args[1].isObject()) {
      JS_ReportErrorASCII(cx, "evaluate: The 2nd argument must be an object");
      return false;
    }

    opts = &args[1].toObject();
  }

  RootedObject global(cx, JS::CurrentGlobalOrNull(cx));
  MOZ_ASSERT(global);

  // Check "global" property before everything to use the given global's
  // option as the default value.
  Maybe<CompileOptions> maybeOptions;
  if (opts) {
    RootedValue v(cx);
    if (!JS_GetProperty(cx, opts, "global", &v)) {
      return false;
    }
    if (!v.isUndefined()) {
      if (v.isObject()) {
        global = js::CheckedUnwrapDynamic(&v.toObject(), cx,
                                          /* stopAtWindowProxy = */ false);
        if (!global) {
          return false;
        }
      }
      if (!global || !(JS::GetClass(global)->flags & JSCLASS_IS_GLOBAL)) {
        JS_ReportErrorNumberASCII(
            cx, GetErrorMessage, nullptr, JSMSG_UNEXPECTED_TYPE,
            "\"global\" passed to evaluate()", "not a global object");
        return false;
      }

      JSAutoRealm ar(cx, global);
      maybeOptions.emplace(cx);
    }
  }
  if (!maybeOptions) {
    // If "global" property is not given, use the current global's option as
    // the default value.
    maybeOptions.emplace(cx);
  }

  CompileOptions& options = maybeOptions.ref();
  UniqueChars fileNameBytes;
  RootedString displayURL(cx);
  RootedString sourceMapURL(cx);
  bool catchTermination = false;
  bool loadBytecode = false;
  bool saveBytecodeWithDelazifications = false;
  bool execute = true;
  bool assertEqBytecode = false;
  JS::EnvironmentChain envChain(cx, JS::SupportUnscopables::No);
  RootedObject callerGlobal(cx, cx->global());

  options.setIntroductionType("js shell evaluate")
      .setFileAndLine("@evaluate", 1)
      .setDeferDebugMetadata();

  RootedValue privateValue(cx);
  RootedString elementAttributeName(cx);

  if (opts) {
    if (!js::ParseCompileOptions(cx, options, opts, &fileNameBytes)) {
      return false;
    }
    if (!ParseDebugMetadata(cx, opts, &privateValue, &elementAttributeName)) {
      return false;
    }
    if (!ParseSourceOptions(cx, opts, &displayURL, &sourceMapURL)) {
      return false;
    }

    RootedValue v(cx);
    if (!JS_GetProperty(cx, opts, "catchTermination", &v)) {
      return false;
    }
    if (!v.isUndefined()) {
      catchTermination = ToBoolean(v);
    }

    if (!JS_GetProperty(cx, opts, "loadBytecode", &v)) {
      return false;
    }
    if (!v.isUndefined()) {
      loadBytecode = ToBoolean(v);
    }

    if (!JS_GetProperty(cx, opts, "saveBytecodeWithDelazifications", &v)) {
      return false;
    }
    if (!v.isUndefined()) {
      saveBytecodeWithDelazifications = ToBoolean(v);
    }

    if (!JS_GetProperty(cx, opts, "execute", &v)) {
      return false;
    }
    if (!v.isUndefined()) {
      execute = ToBoolean(v);
    }

    if (!JS_GetProperty(cx, opts, "assertEqBytecode", &v)) {
      return false;
    }
    if (!v.isUndefined()) {
      assertEqBytecode = ToBoolean(v);
    }

    if (!JS_GetProperty(cx, opts, "envChainObject", &v)) {
      return false;
    }
    if (!v.isUndefined()) {
      if (!v.isObject()) {
        JS_ReportErrorNumberASCII(
            cx, GetErrorMessage, nullptr, JSMSG_UNEXPECTED_TYPE,
            "\"envChainObject\" passed to evaluate()", "not an object");
        return false;
      }

      RootedObject obj(cx, &v.toObject());
      {
        // This may be a CCW, so try to unwrap before checking
        // if it is an unqualified variables object. We still append
        // the original object to the environment chain however.
        JSObject* unwrappedObj = js::UncheckedUnwrap(obj, cx);
        if (unwrappedObj->isUnqualifiedVarObj()) {
          JS_ReportErrorASCII(
              cx,
              "\"envChainObject\" passed to evaluate() should not be an "
              "unqualified variables object");
          return false;
        }
      }

      if (!envChain.append(obj)) {
        return false;
      }
    }

    if (!JS_GetProperty(cx, opts, "supportUnscopables", &v)) {
      return false;
    }
    if (!v.isUndefined()) {
      envChain.setSupportUnscopables(JS::SupportUnscopables(ToBoolean(v)));
    }

    // We cannot load or save the bytecode if we have no object where the
    // bytecode cache is stored.
    if (loadBytecode || saveBytecodeWithDelazifications) {
      if (!cacheEntry) {
        JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                                  JSSMSG_INVALID_ARGS, "evaluate");
        return false;
      }
    }
  }

  if (envChain.length() != 0) {
    // Wrap the envChainObject list into target realm.
    JSAutoRealm ar(cx, global);
    for (size_t i = 0; i < envChain.length(); ++i) {
      if (!JS_WrapObject(cx, envChain.chain()[i])) {
        return false;
      }
    }

    options.setNonSyntacticScope(true);
  }

  // The `loadBuffer` we use below outlives the Stencil we generate so we can
  // use its contents directly in the Stencil.
  options.borrowBuffer = true;

  // We need to track the options used to generate bytecode for a CacheEntry to
  // avoid mismatches. This is primarily a concern when fuzzing the jsshell.
  CacheOptionSet cacheOptions;
  cacheOptions.initFromOptions(options);

  JS::TranscodeBuffer loadBuffer;
  JS::TranscodeBuffer saveBuffer;

  if (loadBytecode) {
    size_t loadLength = 0;
    uint8_t* loadData = nullptr;

    if (!CacheEntry_compatible(cx, cacheEntry, cacheOptions)) {
      return false;
    }

    loadData = CacheEntry_getBytecode(cx, cacheEntry, &loadLength);
    if (!loadData) {
      return false;
    }
    if (!loadBuffer.append(loadData, loadLength)) {
      JS_ReportOutOfMemory(cx);
      return false;
    }
  }

  {
    JSAutoRealm ar(cx, global);
    RefPtr<JS::Stencil> stencil;

    if (loadBytecode) {
      JS::TranscodeRange range(loadBuffer.begin(), loadBuffer.length());
      JS::DecodeOptions decodeOptions(options);

      JS::TranscodeResult rv =
          JS::DecodeStencil(cx, decodeOptions, range, getter_AddRefs(stencil));
      if (JS::IsTranscodeFailureResult(rv)) {
        JS_ReportErrorASCII(cx, "failed to decode cache");
        return false;
      }

      if (!ConvertTranscodeResultToJSException(cx, rv)) {
        return false;
      }
    } else {
      AutoStableStringChars linearChars(cx);
      if (!linearChars.initTwoByte(cx, code)) {
        return false;
      }

      JS::SourceText<char16_t> srcBuf;
      if (!srcBuf.initMaybeBorrowed(cx, linearChars)) {
        return false;
      }

      stencil = JS::CompileGlobalScriptToStencil(cx, options, srcBuf);
      if (!stencil) {
        return false;
      }
    }

    if (!js::ValidateLazinessOfStencilAndGlobal(cx, stencil.get())) {
      return false;
    }

    JS::InstantiateOptions instantiateOptions(options);
    RootedScript script(
        cx, JS::InstantiateGlobalStencil(cx, instantiateOptions, stencil));
    if (!script) {
      return false;
    }

    AutoReportFrontendContext fc(cx);
    if (!SetSourceOptions(cx, &fc, script->scriptSource(), displayURL,
                          sourceMapURL)) {
      return false;
    }

    if (!JS::UpdateDebugMetadata(cx, script, instantiateOptions, privateValue,
                                 elementAttributeName, nullptr, nullptr)) {
      return false;
    }

    if (saveBytecodeWithDelazifications) {
      bool alreadyStarted;
      if (!JS::StartCollectingDelazifications(cx, script, stencil,
                                              alreadyStarted)) {
        return false;
      }
      MOZ_ASSERT(!alreadyStarted);
    }

    if (execute) {
      if (!(envChain.empty()
                ? JS_ExecuteScript(cx, script, args.rval())
                : JS_ExecuteScript(cx, envChain, script, args.rval()))) {
        if (catchTermination && !JS_IsExceptionPending(cx)) {
          ShellContext* sc = GetShellContext(cx);
          if (sc->quitting) {
            UnsetQuitting(cx);
          }

          JSAutoRealm ar1(cx, callerGlobal);
          JSString* str = JS_NewStringCopyZ(cx, "terminated");
          if (!str) {
            return false;
          }
          args.rval().setString(str);
          return true;
        }
        return false;
      }
    }

    // Serialize the encoded bytecode, recorded before the execution, into a
    // buffer which can be deserialized linearly.
    if (saveBytecodeWithDelazifications) {
      if (!FinishCollectingDelazifications(cx, script, saveBuffer)) {
        return false;
      }
    }
  }

  if (saveBytecodeWithDelazifications) {
    // If we are both loading and saving, we assert that we are going to
    // replace the current bytecode by the same stream of bytes.
    if (loadBytecode && assertEqBytecode) {
      if (saveBuffer.length() != loadBuffer.length()) {
        char loadLengthStr[16];
        SprintfLiteral(loadLengthStr, "%zu", loadBuffer.length());
        char saveLengthStr[16];
        SprintfLiteral(saveLengthStr, "%zu", saveBuffer.length());

        JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                                  JSSMSG_CACHE_EQ_SIZE_FAILED, loadLengthStr,
                                  saveLengthStr);
        return false;
      }

      if (!ArrayEqual(loadBuffer.begin(), saveBuffer.begin(),
                      loadBuffer.length())) {
        JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                                  JSSMSG_CACHE_EQ_CONTENT_FAILED);
        return false;
      }
    }

    size_t saveLength = saveBuffer.length();
    if (saveLength >= INT32_MAX) {
      JS_ReportErrorASCII(cx, "Cannot save large cache entry content");
      return false;
    }
    uint8_t* saveData = saveBuffer.extractOrCopyRawBuffer();
    if (!CacheEntry_setBytecode(cx, cacheEntry, cacheOptions, saveData,
                                saveLength)) {
      js_free(saveData);
      return false;
    }
  }

  return JS_WrapValue(cx, args.rval());
}

JSString* js::shell::FileAsString(JSContext* cx, JS::HandleString pathnameStr) {
  UniqueChars pathname = JS_EncodeStringToUTF8(cx, pathnameStr);
  if (!pathname) {
    return nullptr;
  }

  FILE* file = OpenFile(cx, pathname.get(), "rb");
  if (!file) {
    return nullptr;
  }

  AutoCloseFile autoClose(file);

  struct stat st;
  if (fstat(fileno(file), &st) != 0) {
    JS_ReportErrorUTF8(cx, "can't stat %s", pathname.get());
    return nullptr;
  }

  if ((st.st_mode & S_IFMT) != S_IFREG) {
    JS_ReportErrorUTF8(cx, "can't read non-regular file %s", pathname.get());
    return nullptr;
  }

  size_t len;
  if (!FileSize(cx, pathname.get(), file, &len)) {
    return nullptr;
  }

  UniqueChars buf(js_pod_malloc<char>(len + 1));
  if (!buf) {
    JS_ReportErrorUTF8(cx, "out of memory reading %s", pathname.get());
    return nullptr;
  }

  if (!ReadFile(cx, pathname.get(), file, buf.get(), len)) {
    return nullptr;
  }

  UniqueTwoByteChars ucbuf(
      JS::LossyUTF8CharsToNewTwoByteCharsZ(cx, JS::UTF8Chars(buf.get(), len),
                                           &len, js::MallocArena)
          .get());
  if (!ucbuf) {
    JS_ReportErrorUTF8(cx, "Invalid UTF-8 in file '%s'", pathname.get());
    return nullptr;
  }

  return JS_NewUCStringCopyN(cx, ucbuf.get(), len);
}

/*
 * Function to run scripts and return compilation + execution time. Semantics
 * are closely modelled after the equivalent function in WebKit, as this is used
 * to produce benchmark timings by SunSpider.
 */
static bool Run(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() != 1) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              JSSMSG_INVALID_ARGS, "run");
    return false;
  }

  RootedString str(cx, JS::ToString(cx, args[0]));
  if (!str) {
    return false;
  }
  args[0].setString(str);

  str = FileAsString(cx, str);
  if (!str) {
    return false;
  }

  AutoStableStringChars linearChars(cx);
  if (!linearChars.initTwoByte(cx, str)) {
    return false;
  }

  JS::SourceText<char16_t> srcBuf;
  if (!srcBuf.initMaybeBorrowed(cx, linearChars)) {
    return false;
  }

  RootedScript script(cx);
  int64_t startClock = PRMJ_Now();
  {
    UniqueChars filename = JS_EncodeStringToUTF8(cx, str);
    if (!filename) {
      return false;
    }

    JS::CompileOptions options(cx);
    options.setIntroductionType("js shell run")
        .setFileAndLine(filename.get(), 1)
        .setIsRunOnce(true)
        .setNoScriptRval(true)
        .setEagerDelazificationStrategy(defaultDelazificationMode);

    script = JS::Compile(cx, options, srcBuf);
    if (!script) {
      return false;
    }
  }

  if (!JS_ExecuteScript(cx, script)) {
    return false;
  }

  int64_t endClock = PRMJ_Now();

  args.rval().setDouble((endClock - startClock) / double(PRMJ_USEC_PER_MSEC));
  return true;
}

static int js_fgets(char* buf, int size, FILE* file) {
  int n, i, c;
  bool crflag;

  n = size - 1;
  if (n < 0) {
    return -1;
  }

  // Use the fastest available getc.
  auto fast_getc =
#if defined(HAVE_GETC_UNLOCKED)
      getc_unlocked
#elif defined(HAVE__GETC_NOLOCK)
      _getc_nolock
#else
      getc
#endif
      ;

  crflag = false;
  for (i = 0; i < n && (c = fast_getc(file)) != EOF; i++) {
    buf[i] = c;
    if (c == '\n') {  // any \n ends a line
      i++;            // keep the \n; we know there is room for \0
      break;
    }
    if (crflag) {  // \r not followed by \n ends line at the \r
      ungetc(c, file);
      break;  // and overwrite c in buf with \0
    }
    crflag = (c == '\r');
  }

  buf[i] = '\0';
  return i;
}

/*
 * function readline()
 * Provides a hook for scripts to read a line from stdin.
 */
static bool ReadLine(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  static constexpr size_t BUFSIZE = 256;
  FILE* from = stdin;
  size_t buflength = 0;
  size_t bufsize = BUFSIZE;
  char* buf = (char*)JS_malloc(cx, bufsize);
  if (!buf) {
    JS_ReportOutOfMemory(cx);
    return false;
  }
  auto freeBuf = mozilla::MakeScopeExit([&]() {
    JS_free(cx, buf);
    buf = nullptr;
  });

  bool sawNewline = false;
  size_t gotlength;
  while ((gotlength = js_fgets(buf + buflength, bufsize - buflength, from)) >
         0) {
    buflength += gotlength;

    /* Are we done? */
    if (buf[buflength - 1] == '\n') {
      buf[buflength - 1] = '\0';
      sawNewline = true;
      break;
    } else if (buflength < bufsize - 1) {
      break;
    }

    /* Else, grow our buffer for another pass. */
    bufsize *= 2;
    if (bufsize <= buflength) {
      JS_ReportAllocationOverflow(cx);
      return false;
    }
    char* tmp = static_cast<char*>(JS_realloc(cx, buf, bufsize / 2, bufsize));
    if (!tmp) {
      JS_ReportOutOfMemory(cx);
      return false;
    }
    buf = tmp;
  }

  /* Treat the empty string specially. */
  if (buflength == 0) {
    args.rval().set(feof(from) ? NullValue() : JS_GetEmptyStringValue(cx));
    return true;
  }

  /*
   * Turn buf into a JSString. Note that buflength includes the trailing null
   * character.
   */
  JSString* str =
      JS_NewStringCopyN(cx, buf, sawNewline ? buflength - 1 : buflength);
  if (!str) {
    return false;
  }

  args.rval().setString(str);
  return true;
}

/*
 * function readlineBuf()
 * Provides a hook for scripts to emulate readline() using a string object.
 */
static bool ReadLineBuf(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  ShellContext* sc = GetShellContext(cx);

  if (!args.length()) {
    if (!sc->readLineBuf) {
      JS_ReportErrorASCII(cx,
                          "No source buffer set. You must initially "
                          "call readlineBuf with an argument.");
      return false;
    }

    char* currentBuf = sc->readLineBuf.get() + sc->readLineBufPos;
    size_t buflen = strlen(currentBuf);

    if (!buflen) {
      args.rval().setNull();
      return true;
    }

    size_t len = 0;
    while (len < buflen) {
      if (currentBuf[len] == '\n') {
        break;
      }
      len++;
    }

    JSString* str = JS_NewStringCopyUTF8N(cx, JS::UTF8Chars(currentBuf, len));
    if (!str) {
      return false;
    }

    if (currentBuf[len] == '\0') {
      sc->readLineBufPos += len;
    } else {
      sc->readLineBufPos += len + 1;
    }

    args.rval().setString(str);
    return true;
  }

  if (args.length() == 1) {
    sc->readLineBuf = nullptr;
    sc->readLineBufPos = 0;

    RootedString str(cx, JS::ToString(cx, args[0]));
    if (!str) {
      return false;
    }
    sc->readLineBuf = JS_EncodeStringToUTF8(cx, str);
    if (!sc->readLineBuf) {
      return false;
    }

    args.rval().setUndefined();
    return true;
  }

  JS_ReportErrorASCII(cx, "Must specify at most one argument");
  return false;
}

static bool PutStr(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() != 0) {
    if (!gOutFile->isOpen()) {
      JS_ReportErrorASCII(cx, "output file is closed");
      return false;
    }

    RootedString str(cx, JS::ToString(cx, args[0]));
    if (!str) {
      return false;
    }
    UniqueChars bytes = JS_EncodeStringToUTF8(cx, str);
    if (!bytes) {
      return false;
    }
    fputs(bytes.get(), gOutFile->fp);
    fflush(gOutFile->fp);
  }

  args.rval().setUndefined();
  return true;
}

static bool Now(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  double now = PRMJ_Now() / double(PRMJ_USEC_PER_MSEC);
  args.rval().setDouble(now);
  return true;
}

static bool CpuNow(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  double now = double(std::clock()) / double(CLOCKS_PER_SEC);
  args.rval().setDouble(now);
  return true;
}

static bool PrintInternal(JSContext* cx, const CallArgs& args, RCFile* file) {
  if (!file->isOpen()) {
    JS_ReportErrorASCII(cx, "output file is closed");
    return false;
  }

  for (unsigned i = 0; i < args.length(); i++) {
    RootedString str(cx, JS::ToString(cx, args[i]));
    if (!str) {
      return false;
    }
    UniqueChars bytes = JS_EncodeStringToUTF8(cx, str);
    if (!bytes) {
      return false;
    }
    fprintf(file->fp, "%s%s", i ? " " : "", bytes.get());
  }

  fputc('\n', file->fp);
  fflush(file->fp);

  args.rval().setUndefined();
  return true;
}

static bool Print(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
#ifdef FUZZING_INTERFACES
  if (fuzzHaveModule && !fuzzDoDebug) {
    // When fuzzing and not debugging, suppress any print() output,
    // as it slows down fuzzing and makes libFuzzer's output hard
    // to read.
    args.rval().setUndefined();
    return true;
  }
#endif  // FUZZING_INTERFACES
  return PrintInternal(cx, args, gOutFile);
}

static bool PrintErr(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return PrintInternal(cx, args, gErrFile);
}

static bool Help(JSContext* cx, unsigned argc, Value* vp);

static bool Quit(JSContext* cx, unsigned argc, Value* vp) {
  // Print a message to stderr in differential testing to help jsfunfuzz
  // find uncatchable-exception bugs.
  if (js::SupportDifferentialTesting()) {
    fprintf(stderr, "quit called\n");
  }

  CallArgs args = CallArgsFromVp(argc, vp);
  int32_t code;
  if (!ToInt32(cx, args.get(0), &code)) {
    return false;
  }

  // The fuzzers check the shell's exit code and assume a value >= 128 means
  // the process crashed (for instance, SIGSEGV will result in code 139). On
  // POSIX platforms, the exit code is 8-bit and negative values can also
  // result in an exit code >= 128. We restrict the value to range [0, 127] to
  // avoid false positives.
  if (code < 0 || code >= 128) {
    JS_ReportErrorASCII(cx, "quit exit code should be in range 0-127");
    return false;
  }

  SetQuitting(cx, code);
  JS::ReportUncatchableException(cx);
  return false;
}

static bool StartTimingMutator(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() > 0) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              JSSMSG_TOO_MANY_ARGS, "startTimingMutator");
    return false;
  }

  if (!cx->runtime()->gc.stats().startTimingMutator()) {
    JS_ReportErrorASCII(
        cx, "StartTimingMutator should only be called from outside of GC");
    return false;
  }

  args.rval().setUndefined();
  return true;
}

static bool StopTimingMutator(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() > 0) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              JSSMSG_TOO_MANY_ARGS, "stopTimingMutator");
    return false;
  }

  double mutator_ms, gc_ms;
  if (!cx->runtime()->gc.stats().stopTimingMutator(mutator_ms, gc_ms)) {
    JS_ReportErrorASCII(cx,
                        "stopTimingMutator called when not timing the mutator");
    return false;
  }
  double total_ms = mutator_ms + gc_ms;
  if (total_ms > 0 && gOutFile->isOpen()) {
    fprintf(gOutFile->fp, "Mutator: %.3fms (%.1f%%), GC: %.3fms (%.1f%%)\n",
            mutator_ms, mutator_ms / total_ms * 100.0, gc_ms,
            gc_ms / total_ms * 100.0);
  }

  args.rval().setUndefined();
  return true;
}

static const char* ToSource(JSContext* cx, HandleValue vp, UniqueChars* bytes) {
  RootedString str(cx, JS_ValueToSource(cx, vp));
  if (str) {
    *bytes = JS_EncodeStringToUTF8(cx, str);
    if (*bytes) {
      return bytes->get();
    }
  }
  JS_ClearPendingException(cx);
  return "<<error converting value to string>>";
}

static bool AssertEq(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (!(args.length() == 2 || (args.length() == 3 && args[2].isString()))) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              (args.length() < 2)    ? JSSMSG_NOT_ENOUGH_ARGS
                              : (args.length() == 3) ? JSSMSG_INVALID_ARGS
                                                     : JSSMSG_TOO_MANY_ARGS,
                              "assertEq");
    return false;
  }

  bool same;
  if (!JS::SameValue(cx, args[0], args[1], &same)) {
    return false;
  }
  if (!same) {
    UniqueChars bytes0, bytes1;
    const char* actual = ToSource(cx, args[0], &bytes0);
    const char* expected = ToSource(cx, args[1], &bytes1);
    if (args.length() == 2) {
      JS_ReportErrorNumberUTF8(cx, my_GetErrorMessage, nullptr,
                               JSSMSG_ASSERT_EQ_FAILED, actual, expected);
    } else {
      RootedString message(cx, args[2].toString());
      UniqueChars bytes2 = QuoteString(cx, message);
      if (!bytes2) {
        return false;
      }
      JS_ReportErrorNumberUTF8(cx, my_GetErrorMessage, nullptr,
                               JSSMSG_ASSERT_EQ_FAILED_MSG, actual, expected,
                               bytes2.get());
    }
    return false;
  }
  args.rval().setUndefined();
  return true;
}

static JSScript* GetTopScript(JSContext* cx) {
  NonBuiltinScriptFrameIter iter(cx);
  return iter.done() ? nullptr : iter.script();
}

static bool GetScriptAndPCArgs(JSContext* cx, CallArgs& args,
                               MutableHandleScript scriptp, int32_t* ip) {
  RootedScript script(cx, GetTopScript(cx));
  *ip = 0;
  if (!args.get(0).isUndefined()) {
    HandleValue v = args[0];
    unsigned intarg = 0;
    if (v.isObject() && JS::GetClass(&v.toObject())->isJSFunction()) {
      script = TestingFunctionArgumentToScript(cx, v);
      if (!script) {
        return false;
      }
      intarg++;
    }
    if (!args.get(intarg).isUndefined()) {
      if (!JS::ToInt32(cx, args[intarg], ip)) {
        return false;
      }
      if ((uint32_t)*ip >= script->length()) {
        JS_ReportErrorASCII(cx, "Invalid PC");
        return false;
      }
    }
  }

  scriptp.set(script);

  return true;
}

static bool LineToPC(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() == 0) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              JSSMSG_LINE2PC_USAGE);
    return false;
  }

  RootedScript script(cx, GetTopScript(cx));
  int32_t lineArg = 0;
  if (args[0].isObject() && args[0].toObject().is<JSFunction>()) {
    script = TestingFunctionArgumentToScript(cx, args[0]);
    if (!script) {
      return false;
    }
    lineArg++;
  }

  uint32_t lineno;
  if (!ToUint32(cx, args.get(lineArg), &lineno)) {
    return false;
  }

  jsbytecode* pc = LineNumberToPC(script, lineno);
  if (!pc) {
    return false;
  }
  args.rval().setInt32(script->pcToOffset(pc));
  return true;
}

static bool PCToLine(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedScript script(cx);
  int32_t i;
  unsigned lineno;

  if (!GetScriptAndPCArgs(cx, args, &script, &i)) {
    return false;
  }
  lineno = PCToLineNumber(script, script->offsetToPC(i));
  if (!lineno) {
    return false;
  }
  args.rval().setInt32(lineno);
  return true;
}

#if defined(DEBUG) || defined(JS_JITSPEW)

static bool Notes(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  JSSprinter sprinter(cx);
  if (!sprinter.init()) {
    return false;
  }

  for (unsigned i = 0; i < args.length(); i++) {
    RootedScript script(cx, TestingFunctionArgumentToScript(cx, args[i]));
    if (!script) {
      return false;
    }

    if (!JSScript::dumpSrcNotes(cx, script, &sprinter)) {
      return false;
    }
  }

  JSString* str = sprinter.release(cx);
  if (!str) {
    return false;
  }
  args.rval().setString(str);
  return true;
}

namespace {

struct DisassembleOptionParser {
  unsigned argc;
  Value* argv;
  JSScript::DumpOptions options;

  DisassembleOptionParser(unsigned argc, Value* argv)
      : argc(argc), argv(argv) {}

  bool parse(JSContext* cx) {
    options.recursive = false;

    /* Read options off early arguments */
    while (argc > 0 && argv[0].isString()) {
      JSString* str = argv[0].toString();
      JSLinearString* linearStr = JS_EnsureLinearString(cx, str);
      if (!linearStr) {
        return false;
      }
      if (JS_LinearStringEqualsLiteral(linearStr, "-r")) {
        options.recursive = true;
      } else {
        break;
      }
      argv++;
      argc--;
    }
    return true;
  }
};

} /* anonymous namespace */

static bool DisassembleToSprinter(JSContext* cx, unsigned argc, Value* vp,
                                  StringPrinter* sp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  DisassembleOptionParser p(args.length(), args.array());
  if (!p.parse(cx)) {
    return false;
  }

  if (p.argc == 0) {
    /* Without arguments, disassemble the current script. */
    RootedScript script(cx, GetTopScript(cx));
    if (script) {
      JSAutoRealm ar(cx, script);
      if (!JSScript::dump(cx, script, p.options, sp)) {
        return false;
      }
    }
  } else {
    for (unsigned i = 0; i < p.argc; i++) {
      RootedFunction fun(cx);
      RootedScript script(cx);
      RootedValue value(cx, p.argv[i]);
      if (value.isObject() && value.toObject().is<ShellModuleObjectWrapper>()) {
        auto* module = value.toObject().as<ShellModuleObjectWrapper>().get();
        script = module->maybeScript();
        if (!script) {
          JS_ReportErrorASCII(cx, "module does not have an associated script");
          return false;
        }
      } else {
        script = TestingFunctionArgumentToScript(cx, value, fun.address());
        if (!script) {
          return false;
        }
      }

      if (!JSScript::dump(cx, script, p.options, sp)) {
        return false;
      }
    }
  }

  return true;
}

static bool DisassembleToString(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  JSSprinter sprinter(cx);
  if (!sprinter.init()) {
    return false;
  }
  if (!DisassembleToSprinter(cx, args.length(), vp, &sprinter)) {
    return false;
  }

  JSString* str = sprinter.release(cx);
  if (!str) {
    return false;
  }
  args.rval().setString(str);
  return true;
}

static bool Disassemble(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!gOutFile->isOpen()) {
    JS_ReportErrorASCII(cx, "output file is closed");
    return false;
  }

  Sprinter sprinter(cx);
  if (!sprinter.init()) {
    return false;
  }
  if (!DisassembleToSprinter(cx, args.length(), vp, &sprinter)) {
    return false;
  }

  JS::UniqueChars str = sprinter.release();
  if (!str) {
    return false;
  }
  fprintf(gOutFile->fp, "%s\n", str.get());
  args.rval().setUndefined();
  return true;
}

static bool DisassFile(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!gOutFile->isOpen()) {
    JS_ReportErrorASCII(cx, "output file is closed");
    return false;
  }

  /* Support extra options at the start, just like Disassemble. */
  DisassembleOptionParser p(args.length(), args.array());
  if (!p.parse(cx)) {
    return false;
  }

  if (!p.argc) {
    args.rval().setUndefined();
    return true;
  }

  // We should change DisassembleOptionParser to store CallArgs.
  Rooted<JSString*> str(
      cx, JS::ToString(cx, HandleValue::fromMarkedLocation(&p.argv[0])));
  if (!str) {
    return false;
  }
  UniqueChars filename = JS_EncodeStringToUTF8(cx, str);
  if (!filename) {
    return false;
  }
  RootedScript script(cx);

  {
    CompileOptions options(cx);
    options.setIntroductionType("js shell disFile")
        .setFileAndLine(filename.get(), 1)
        .setIsRunOnce(true)
        .setNoScriptRval(true)
        .setEagerDelazificationStrategy(defaultDelazificationMode);

    script = JS::CompileUtf8Path(cx, options, filename.get());
    if (!script) {
      return false;
    }
  }

  Sprinter sprinter(cx);
  if (!sprinter.init()) {
    return false;
  }
  if (!JSScript::dump(cx, script, p.options, &sprinter)) {
    return false;
  }

  JS::UniqueChars chars = sprinter.release();
  if (!chars) {
    return false;
  }
  fprintf(gOutFile->fp, "%s\n", chars.get());

  args.rval().setUndefined();
  return true;
}

static bool DisassWithSrc(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!gOutFile->isOpen()) {
    JS_ReportErrorASCII(cx, "output file is closed");
    return false;
  }

  const size_t lineBufLen = 512;
  unsigned len, line1, line2, bupline;
  char linebuf[lineBufLen];
  static const char sep[] = ";-------------------------";

  RootedScript script(cx);
  for (unsigned i = 0; i < args.length(); i++) {
    script = TestingFunctionArgumentToScript(cx, args[i]);
    if (!script) {
      return false;
    }

    if (!script->filename()) {
      JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                                JSSMSG_FILE_SCRIPTS_ONLY);
      return false;
    }

    FILE* file = OpenFile(cx, script->filename(), "rb");
    if (!file) {
      return false;
    }
    auto closeFile = MakeScopeExit([file] { fclose(file); });

    jsbytecode* pc = script->code();
    jsbytecode* end = script->codeEnd();

    Sprinter sprinter(cx);
    if (!sprinter.init()) {
      return false;
    }

    /* burn the leading lines */
    line2 = PCToLineNumber(script, pc);
    for (line1 = 0; line1 < line2 - 1; line1++) {
      char* tmp = fgets(linebuf, lineBufLen, file);
      if (!tmp) {
        JS_ReportErrorUTF8(cx, "failed to read %s fully", script->filename());
        return false;
      }
    }

    bupline = 0;
    while (pc < end) {
      line2 = PCToLineNumber(script, pc);

      if (line2 < line1) {
        if (bupline != line2) {
          bupline = line2;
          sprinter.printf("%s %3u: BACKUP\n", sep, line2);
        }
      } else {
        if (bupline && line1 == line2) {
          sprinter.printf("%s %3u: RESTORE\n", sep, line2);
        }
        bupline = 0;
        while (line1 < line2) {
          if (!fgets(linebuf, lineBufLen, file)) {
            JS_ReportErrorNumberUTF8(cx, my_GetErrorMessage, nullptr,
                                     JSSMSG_UNEXPECTED_EOF, script->filename());
            return false;
          }
          line1++;
          sprinter.printf("%s %3u: %s", sep, line1, linebuf);
        }
      }

      len =
          Disassemble1(cx, script, pc, script->pcToOffset(pc), true, &sprinter);
      if (!len) {
        return false;
      }

      pc += len;
    }

    JS::UniqueChars str = sprinter.release();
    if (!str) {
      return false;
    }
    fprintf(gOutFile->fp, "%s\n", str.get());
  }

  args.rval().setUndefined();
  return true;
}

#endif /* defined(DEBUG) || defined(JS_JITSPEW) */

#ifdef JS_CACHEIR_SPEW
static bool CacheIRHealthReport(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  js::jit::CacheIRHealth cih;
  RootedScript script(cx);

  // In the case that we are calling this function from the shell and
  // the environment variable is not set, AutoSpewChannel automatically
  // sets and unsets the proper channel for the duration of spewing
  // a health report.
  AutoSpewChannel channel(cx, SpewChannel::CacheIRHealthReport, script);
  if (!argc) {
    // Calling CacheIRHealthReport without any arguments will create health
    // reports for all scripts in the zone.
    if (jit::JitZone* jitZone = cx->zone()->jitZone()) {
      jitZone->forEachJitScript([&](jit::JitScript* jitScript) {
        script = jitScript->owningScript();
        if (!script->selfHosted()) {
          cih.healthReportForScript(cx, script, js::jit::SpewContext::Shell);
        }
      });
    }
  } else {
    RootedValue value(cx, args.get(0));

    if (value.isObject() && value.toObject().is<ShellModuleObjectWrapper>()) {
      script =
          value.toObject().as<ShellModuleObjectWrapper>().get()->maybeScript();
      if (!script) {
        JS_ReportErrorASCII(cx, "module does not have an associated script");
        return false;
      }
    } else {
      script = TestingFunctionArgumentToScript(cx, args.get(0));
      if (!script) {
        return false;
      }
    }

    cih.healthReportForScript(cx, script, js::jit::SpewContext::Shell);
  }

  args.rval().setUndefined();
  return true;
}
#endif /* JS_CACHEIR_SPEW */

/* Pretend we can always preserve wrappers for dummy DOM objects. */
static bool DummyPreserveWrapperCallback(JSContext* cx, HandleObject obj) {
  return true;
}

static bool DummyHasReleasedWrapperCallback(HandleObject obj) { return true; }

#ifdef FUZZING_JS_FUZZILLI
static bool fuzzilli_hash(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setUndefined();

  if (argc != 1) {
    return true;
  }
  uint32_t hash;
  JS::Handle<JS::Value> v = args.get(0);
  if (v.isInt32()) {
    int32_t i = v.toInt32();
    hash = FuzzilliHashDouble((double)i);
  } else if (v.isDouble()) {
    double d = v.toDouble();
    d = JS::CanonicalizeNaN(d);
    hash = FuzzilliHashDouble(d);
  } else if (v.isNull()) {
    hash = FuzzilliHashDouble(1.0);
  } else if (v.isUndefined()) {
    hash = FuzzilliHashDouble(2.0);
  } else if (v.isBoolean()) {
    hash = FuzzilliHashDouble(3.0 + v.toBoolean());
  } else if (v.isBigInt()) {
    JS::BigInt* bigInt = v.toBigInt();
    hash = FuzzilliHashBigInt(bigInt);
  } else if (v.isObject()) {
    JSObject& obj = v.toObject();
    FuzzilliHashObject(cx, &obj);
    return true;
  } else {
    hash = 0;
  }

  cx->executionHashInputs += 1;
  cx->executionHash = mozilla::RotateLeft(cx->executionHash + hash, 1);
  return true;
}

// We have to assume that the fuzzer will be able to call this function e.g. by
// enumerating the properties of the global object and eval'ing them. As such
// this function is implemented in a way that requires passing some magic value
// as first argument (with the idea being that the fuzzer won't be able to
// generate this value) which then also acts as a selector for the operation
// to perform.
static bool Fuzzilli(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  RootedString arg(cx, JS::ToString(cx, args.get(0)));
  if (!arg) {
    return false;
  }
  Rooted<JSLinearString*> operation(cx, StringToLinearString(cx, arg));
  if (!operation) {
    return false;
  }

  if (StringEqualsAscii(operation, "FUZZILLI_CRASH")) {
    int type;
    if (!ToInt32(cx, args.get(1), &type)) {
      return false;
    }

    // With this, we can test the various ways the JS shell can crash and make
    // sure that Fuzzilli is able to detect all of these failures properly.
    switch (type) {
      case 0:
        *((int*)0x41414141) = 0x1337;
        break;
      case 1:
        MOZ_RELEASE_ASSERT(false);
        break;
      case 2:
        MOZ_ASSERT(false);
        break;
      case 3:
      #if defined(JS_CODEGEN_X64) || defined(JS_CODEGEN_X86)
        __asm__("int3");
      #elif defined(JS_CODEGEN_ARM64)
        __asm__("brk #0");
      #endif
        break;
      default:
        exit(1);
    }
  } else if (StringEqualsAscii(operation, "FUZZILLI_PRINT")) {
    static FILE* fzliout = fdopen(REPRL_DWFD, "w");
    if (!fzliout) {
      fprintf(
          stderr,
          "Fuzzer output channel not available, printing to stdout instead\n");
      fzliout = stdout;
    }

    RootedString str(cx, JS::ToString(cx, args.get(1)));
    if (!str) {
      return false;
    }
    UniqueChars bytes = JS_EncodeStringToUTF8(cx, str);
    if (!bytes) {
      return false;
    }
    fprintf(fzliout, "%s\n", bytes.get());
    fflush(fzliout);
  } else if (StringEqualsAscii(operation, "FUZZILLI_RANDOM")) {
    // This is an entropy source which can be called during fuzzing.
    // Its currently used to tests whether Fuzzilli detects non-deterministic
    // behavior.
    args.rval().setInt32(static_cast<uint32_t>(mozilla::RandomUint64OrDie()));
    return true;
  }

  args.rval().setUndefined();
  return true;
}

static bool FuzzilliReprlGetAndRun(JSContext* cx) {
  size_t scriptSize = 0;

  unsigned action;
  MOZ_RELEASE_ASSERT(read(REPRL_CRFD, &action, 4) == 4);
  if (action == 'cexe') {
    MOZ_RELEASE_ASSERT(read(REPRL_CRFD, &scriptSize, 8) == 8);
  } else {
    fprintf(stderr, "Unknown action: %u\n", action);
    _exit(-1);
  }

  CompileOptions options(cx);
  options.setIntroductionType("reprl")
      .setFileAndLine("reprl", 1)
      .setIsRunOnce(true)
      .setNoScriptRval(true)
      .setEagerDelazificationStrategy(defaultDelazificationMode);

  char* scriptSrc = static_cast<char*>(js_malloc(scriptSize));

  char* ptr = scriptSrc;
  size_t remaining = scriptSize;
  while (remaining > 0) {
    ssize_t rv = read(REPRL_DRFD, ptr, remaining);
    if (rv <= 0) {
      fprintf(stderr, "Failed to load script\n");
      _exit(-1);
    }
    remaining -= rv;
    ptr += rv;
  }

  JS::SourceText<Utf8Unit> srcBuf;
  if (!srcBuf.init(cx, scriptSrc, scriptSize,
                   JS::SourceOwnership::TakeOwnership)) {
    return false;
  }

  RootedScript script(cx, JS::Compile(cx, options, srcBuf));
  if (!script) {
    return false;
  }

  if (!JS_ExecuteScript(cx, script)) {
    return false;
  }

  return true;
}

#endif /* FUZZING_JS_FUZZILLI */

static bool FuzzilliUseReprlMode(OptionParser* op) {
#ifdef FUZZING_JS_FUZZILLI
  // Check if we should use REPRL mode
  bool reprl_mode = op->getBoolOption("reprl");
  if (reprl_mode) {
    // Check in with parent
    char helo[] = "HELO";
    if (write(REPRL_CWFD, helo, 4) != 4 || read(REPRL_CRFD, helo, 4) != 4) {
      reprl_mode = false;
    }

    if (memcmp(helo, "HELO", 4) != 0) {
      fprintf(stderr, "Invalid response from parent\n");
      _exit(-1);
    }
  }
  return reprl_mode;
#else
  return false;
#endif /* FUZZING_JS_FUZZILLI */
}

static bool Crash(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() == 0) {
    MOZ_CRASH("forced crash");
  }
  RootedString message(cx, JS::ToString(cx, args[0]));
  if (!message) {
    return false;
  }
  UniqueChars utf8chars = JS_EncodeStringToUTF8(cx, message);
  if (!utf8chars) {
    return false;
  }
  if (args.get(1).isObject()) {
    RootedValue v(cx);
    RootedObject opts(cx, &args[1].toObject());
    if (!JS_GetProperty(cx, opts, "suppress_minidump", &v)) {
      return false;
    }
    if (v.isBoolean() && v.toBoolean()) {
      js::NoteIntentionalCrash();
    }
  }
#ifndef DEBUG
  MOZ_ReportCrash(utf8chars.get(), __FILE__, __LINE__);
#endif
  MOZ_CRASH_UNSAFE(utf8chars.get());
}

static bool GetSLX(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedScript script(cx);

  script = TestingFunctionArgumentToScript(cx, args.get(0));
  if (!script) {
    return false;
  }
  args.rval().setInt32(GetScriptLineExtent(script));
  return true;
}

static bool ThrowError(JSContext* cx, unsigned argc, Value* vp) {
  JS_ReportErrorASCII(cx, "This is an error");
  return false;
}

static bool CopyErrorReportToObject(JSContext* cx, JSErrorReport* report,
                                    HandleObject obj) {
  RootedString nameStr(cx);
  if (report->exnType == JSEXN_WARN) {
    nameStr = JS_NewStringCopyZ(cx, "Warning");
    if (!nameStr) {
      return false;
    }
  } else {
    nameStr = GetErrorTypeName(cx, report->exnType);
    // GetErrorTypeName doesn't set an exception, but
    // can fail for InternalError or non-error objects.
    if (!nameStr) {
      nameStr = cx->runtime()->emptyString;
    }
  }
  RootedValue nameVal(cx, StringValue(nameStr));
  if (!DefineDataProperty(cx, obj, cx->names().name, nameVal)) {
    return false;
  }

  RootedString messageStr(cx, report->newMessageString(cx));
  if (!messageStr) {
    return false;
  }
  RootedValue messageVal(cx, StringValue(messageStr));
  if (!DefineDataProperty(cx, obj, cx->names().message, messageVal)) {
    return false;
  }

  RootedValue linenoVal(cx, Int32Value(report->lineno));
  if (!DefineDataProperty(cx, obj, cx->names().lineNumber, linenoVal)) {
    return false;
  }

  RootedValue columnVal(cx, Int32Value(report->column.oneOriginValue()));
  if (!DefineDataProperty(cx, obj, cx->names().columnNumber, columnVal)) {
    return false;
  }

  RootedObject notesArray(cx, CreateErrorNotesArray(cx, report));
  if (!notesArray) {
    return false;
  }

  RootedValue notesArrayVal(cx, ObjectValue(*notesArray));
  return DefineDataProperty(cx, obj, cx->names().notes, notesArrayVal);
}

static bool CreateErrorReport(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // We don't have a stack here, so just initialize with null.
  JS::ExceptionStack exnStack(cx, args.get(0), nullptr);
  JS::ErrorReportBuilder report(cx);
  if (!report.init(cx, exnStack, JS::ErrorReportBuilder::WithSideEffects)) {
    return false;
  }

  MOZ_ASSERT(!report.report()->isWarning());

  RootedObject obj(cx, JS_NewPlainObject(cx));
  if (!obj) {
    return false;
  }

  RootedString toString(cx, NewStringCopyUTF8Z(cx, report.toStringResult()));
  if (!toString) {
    return false;
  }

  if (!JS_DefineProperty(cx, obj, "toStringResult", toString,
                         JSPROP_ENUMERATE)) {
    return false;
  }

  if (!CopyErrorReportToObject(cx, report.report(), obj)) {
    return false;
  }

  args.rval().setObject(*obj);
  return true;
}

#define LAZY_STANDARD_CLASSES

/* A class for easily testing the inner/outer object callbacks. */
struct ComplexObject {
  bool isInner;
  bool frozen;
  JSObject* inner;
  JSObject* outer;
};

static bool sandbox_enumerate(JSContext* cx, JS::HandleObject obj,
                              JS::MutableHandleIdVector properties,
                              bool enumerableOnly) {
  RootedValue v(cx);

  if (!JS_GetProperty(cx, obj, "lazy", &v)) {
    return false;
  }

  if (!ToBoolean(v)) {
    return true;
  }

  return JS_NewEnumerateStandardClasses(cx, obj, properties, enumerableOnly);
}

static bool sandbox_resolve(JSContext* cx, HandleObject obj, HandleId id,
                            bool* resolvedp) {
  RootedValue v(cx);
  if (!JS_GetProperty(cx, obj, "lazy", &v)) {
    return false;
  }

  if (ToBoolean(v)) {
    return JS_ResolveStandardClass(cx, obj, id, resolvedp);
  }
  return true;
}

static const JSClassOps sandbox_classOps = {
    nullptr,                   // addProperty
    nullptr,                   // delProperty
    nullptr,                   // enumerate
    sandbox_enumerate,         // newEnumerate
    sandbox_resolve,           // resolve
    nullptr,                   // mayResolve
    nullptr,                   // finalize
    nullptr,                   // call
    nullptr,                   // construct
    JS_GlobalObjectTraceHook,  // trace
};

static const JSClass sandbox_class = {
    "sandbox",
    JSCLASS_GLOBAL_FLAGS,
    &sandbox_classOps,
};

static void SetStandardRealmOptions(JS::RealmOptions& options) {
  options.creationOptions()
      .setSharedMemoryAndAtomicsEnabled(enableSharedMemory)
      .setCoopAndCoepEnabled(false)
      .setToSourceEnabled(enableToSource);
}

[[nodiscard]] static bool CheckRealmOptions(JSContext* cx,
                                            JS::RealmOptions& options,
                                            JSPrincipals* principals) {
  JS::RealmCreationOptions& creationOptions = options.creationOptions();
  if (creationOptions.compartmentSpecifier() !=
      JS::CompartmentSpecifier::ExistingCompartment) {
    return true;
  }

  JS::Compartment* comp = creationOptions.compartment();

  // All realms in a compartment must be either system or non-system.
  bool isSystem =
      principals && principals == cx->runtime()->trustedPrincipals();
  if (isSystem != IsSystemCompartment(comp)) {
    JS_ReportErrorASCII(cx,
                        "Cannot create system and non-system realms in the "
                        "same compartment");
    return false;
  }

  // Debugger visibility is per-compartment, not per-realm, so make sure the
  // requested visibility matches the existing compartment's.
  if (creationOptions.invisibleToDebugger() != comp->invisibleToDebugger()) {
    JS_ReportErrorASCII(cx,
                        "All the realms in a compartment must have "
                        "the same debugger visibility");
    return false;
  }

  return true;
}

static JSObject* NewSandbox(JSContext* cx, bool lazy) {
  JS::RealmOptions options;
  SetStandardRealmOptions(options);

  if (defaultToSameCompartment) {
    options.creationOptions().setExistingCompartment(cx->global());
  } else {
    options.creationOptions().setNewCompartmentAndZone();
  }

  JSPrincipals* principals = nullptr;
  if (!CheckRealmOptions(cx, options, principals)) {
    return nullptr;
  }

  RootedObject obj(cx,
                   JS_NewGlobalObject(cx, &sandbox_class, principals,
                                      JS::DontFireOnNewGlobalHook, options));
  if (!obj) {
    return nullptr;
  }

  {
    JSAutoRealm ar(cx, obj);
    if (!lazy && !JS::InitRealmStandardClasses(cx)) {
      return nullptr;
    }

    RootedValue value(cx, BooleanValue(lazy));
    if (!JS_DefineProperty(cx, obj, "lazy", value,
                           JSPROP_PERMANENT | JSPROP_READONLY)) {
      return nullptr;
    }

    JS_FireOnNewGlobalObject(cx, obj);
  }

  if (!cx->compartment()->wrap(cx, &obj)) {
    return nullptr;
  }
  return obj;
}

static bool EvalInContext(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "evalcx", 1)) {
    return false;
  }

  RootedString str(cx, ToString(cx, args[0]));
  if (!str) {
    return false;
  }

  RootedObject sobj(cx);
  if (args.hasDefined(1)) {
    sobj = ToObject(cx, args[1]);
    if (!sobj) {
      return false;
    }
  }

  AutoStableStringChars strChars(cx);
  if (!strChars.initTwoByte(cx, str)) {
    return false;
  }

  mozilla::Range<const char16_t> chars = strChars.twoByteRange();
  size_t srclen = chars.length();
  const char16_t* src = chars.begin().get();

  bool lazy = false;
  if (srclen == 4) {
    if (src[0] == 'l' && src[1] == 'a' && src[2] == 'z' && src[3] == 'y') {
      lazy = true;
      srclen = 0;
    }
  }

  if (!sobj) {
    sobj = NewSandbox(cx, lazy);
    if (!sobj) {
      return false;
    }
  }

  if (srclen == 0) {
    args.rval().setObject(*sobj);
    return true;
  }

  JS::AutoFilename filename;
  uint32_t lineno;

  DescribeScriptedCaller(&filename, cx, &lineno);
  {
    sobj = UncheckedUnwrap(sobj, true);

    JSAutoRealm ar(cx, sobj);

    sobj = ToWindowIfWindowProxy(sobj);

    if (!JS_IsGlobalObject(sobj)) {
      JS_ReportErrorASCII(cx, "Invalid scope argument to evalcx");
      return false;
    }

    JS::CompileOptions opts(cx);
    opts.setFileAndLine(filename.get(), lineno)
        .setEagerDelazificationStrategy(defaultDelazificationMode);

    JS::SourceText<char16_t> srcBuf;
    if (!srcBuf.init(cx, src, srclen, JS::SourceOwnership::Borrowed) ||
        !JS::Evaluate(cx, opts, srcBuf, args.rval())) {
      return false;
    }
  }

  if (!cx->compartment()->wrap(cx, args.rval())) {
    return false;
  }

  return true;
}

static bool EnsureGeckoProfilingStackInstalled(JSContext* cx,
                                               ShellContext* sc) {
  if (cx->geckoProfiler().infraInstalled()) {
    MOZ_ASSERT(sc->geckoProfilingStack);
    return true;
  }

  MOZ_ASSERT(!sc->geckoProfilingStack);
  sc->geckoProfilingStack = MakeUnique<ProfilingStack>();
  if (!sc->geckoProfilingStack) {
    JS_ReportOutOfMemory(cx);
    return false;
  }

  SetContextProfilingStack(cx, sc->geckoProfilingStack.get());
  return true;
}

struct WorkerInput {
  JSRuntime* parentRuntime;
  UniqueTwoByteChars chars;
  size_t length;

  WorkerInput(JSRuntime* parentRuntime, UniqueTwoByteChars chars, size_t length)
      : parentRuntime(parentRuntime), chars(std::move(chars)), length(length) {}
};

static void DestroyShellCompartmentPrivate(JS::GCContext* gcx,
                                           JS::Compartment* compartment) {
  auto priv = static_cast<ShellCompartmentPrivate*>(
      JS_GetCompartmentPrivate(compartment));
  js_delete(priv);
}

static void SetWorkerContextOptions(JSContext* cx);
static bool ShellBuildId(JS::BuildIdCharVector* buildId);

static constexpr size_t gWorkerStackSize = 2 * 128 * sizeof(size_t) * 1024;

static void WorkerMain(UniquePtr<WorkerInput> input) {
  MOZ_ASSERT(input->parentRuntime);

  JSContext* cx = JS_NewContext(8L * 1024L * 1024L, input->parentRuntime);
  if (!cx) {
    return;
  }
  auto destroyContext = MakeScopeExit([cx] { JS_DestroyContext(cx); });

  UniquePtr<ShellContext> sc =
      MakeUnique<ShellContext>(cx, ShellContext::Worker);
  if (!sc || !sc->registerWithCx(cx)) {
    return;
  }

  if (!JS::InitSelfHostedCode(cx)) {
    return;
  }

  EnvironmentPreparer environmentPreparer(cx);

  do {
    JS::RealmOptions realmOptions;
    SetStandardRealmOptions(realmOptions);

    RootedObject global(cx, NewGlobalObject(cx, realmOptions, nullptr,
                                            ShellGlobalKind::WindowProxy,
                                            /* immutablePrototype = */ true));
    if (!global) {
      break;
    }

    JSAutoRealm ar(cx, global);

    JS::ConstUTF8CharsZ path(processWideModuleLoadPath.get(),
                             strlen(processWideModuleLoadPath.get()));
    RootedString moduleLoadPath(cx, JS_NewStringCopyUTF8Z(cx, path));
    if (!moduleLoadPath) {
      return;
    }
    sc->moduleLoader = js::MakeUnique<ModuleLoader>();
    if (!sc->moduleLoader || !sc->moduleLoader->init(cx, moduleLoadPath)) {
      return;
    }

    JS::CompileOptions options(cx);
    options.setFileAndLine("<string>", 1)
        .setIsRunOnce(true)
        .setEagerDelazificationStrategy(defaultDelazificationMode);

    AutoReportException are(cx);
    JS::SourceText<char16_t> srcBuf;
    if (!srcBuf.init(cx, input->chars.get(), input->length,
                     JS::SourceOwnership::Borrowed)) {
      break;
    }

    RootedScript script(cx, JS::Compile(cx, options, srcBuf));
    if (!script) {
      break;
    }
    RootedValue result(cx);
    JS_ExecuteScript(cx, script, &result);
  } while (0);

  KillWatchdog(cx);
}

// Workers can spawn other workers, so we need a lock to access workerThreads.
static Mutex* workerThreadsLock = nullptr;
MOZ_RUNINIT static Vector<UniquePtr<js::Thread>, 0, SystemAllocPolicy>
    workerThreads;

class MOZ_RAII AutoLockWorkerThreads : public LockGuard<Mutex> {
  using Base = LockGuard<Mutex>;

 public:
  AutoLockWorkerThreads() : Base(*workerThreadsLock) {
    MOZ_ASSERT(workerThreadsLock);
  }
};

static bool EvalInWorker(JSContext* cx, unsigned argc, Value* vp) {
  if (!CanUseExtraThreads()) {
    JS_ReportErrorASCII(cx, "Can't create threads with --no-threads");
    return false;
  }

  CallArgs args = CallArgsFromVp(argc, vp);
  if (!args.get(0).isString()) {
    JS_ReportErrorASCII(cx, "Invalid arguments");
    return false;
  }

#if defined(DEBUG) || defined(JS_OOM_BREAKPOINT)
  if (cx->runningOOMTest) {
    JS_ReportErrorASCII(
        cx, "Can't create threads while running simulated OOM test");
    return false;
  }
#endif

  if (!args[0].toString()->ensureLinear(cx)) {
    return false;
  }

  if (!workerThreadsLock) {
    workerThreadsLock = js_new<Mutex>(mutexid::ShellWorkerThreads);
    if (!workerThreadsLock) {
      ReportOutOfMemory(cx);
      return false;
    }
  }

  JSLinearString* str = &args[0].toString()->asLinear();

  UniqueTwoByteChars chars(js_pod_malloc<char16_t>(str->length()));
  if (!chars) {
    ReportOutOfMemory(cx);
    return false;
  }

  CopyChars(chars.get(), *str);

  auto input = js::MakeUnique<WorkerInput>(JS_GetParentRuntime(cx),
                                           std::move(chars), str->length());
  if (!input) {
    ReportOutOfMemory(cx);
    return false;
  }

  UniquePtr<Thread> thread;
  {
    AutoEnterOOMUnsafeRegion oomUnsafe;
    thread = js::MakeUnique<Thread>(
        Thread::Options().setStackSize(gWorkerStackSize + 512 * 1024));
    if (!thread || !thread->init(WorkerMain, std::move(input))) {
      oomUnsafe.crash("EvalInWorker");
    }
  }

  AutoLockWorkerThreads alwt;
  if (!workerThreads.append(std::move(thread))) {
    ReportOutOfMemory(cx);
    thread->join();
    return false;
  }

  args.rval().setUndefined();
  return true;
}

static bool ShapeOf(JSContext* cx, unsigned argc, JS::Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (!args.get(0).isObject()) {
    JS_ReportErrorASCII(cx, "shapeOf: object expected");
    return false;
  }
  JSObject* obj = &args[0].toObject();
  args.rval().set(JS_NumberValue(double(uintptr_t(obj->shape()) >> 3)));
  return true;
}

static bool Sleep_fn(JSContext* cx, unsigned argc, Value* vp) {
  ShellContext* sc = GetShellContext(cx);
  CallArgs args = CallArgsFromVp(argc, vp);

  TimeDuration duration = TimeDuration::FromSeconds(0.0);
  if (args.length() > 0) {
    double t_secs;
    if (!ToNumber(cx, args[0], &t_secs)) {
      return false;
    }
    if (std::isnan(t_secs)) {
      JS_ReportErrorASCII(cx, "sleep interval is not a number");
      return false;
    }

    duration = TimeDuration::FromSeconds(std::max(0.0, t_secs));
    const TimeDuration MAX_TIMEOUT_INTERVAL =
        TimeDuration::FromSeconds(MAX_TIMEOUT_SECONDS);
    if (duration > MAX_TIMEOUT_INTERVAL) {
      JS_ReportErrorASCII(cx, "Excessive sleep interval");
      return false;
    }
  }
  {
    LockGuard<Mutex> guard(sc->watchdogLock);
    TimeStamp toWakeup = TimeStamp::Now() + duration;
    for (;;) {
      sc->sleepWakeup.wait_for(guard, duration);
      if (sc->serviceInterrupt) {
        break;
      }
      auto now = TimeStamp::Now();
      if (now >= toWakeup) {
        break;
      }
      duration = toWakeup - now;
    }
  }

  if (sc->serviceInterrupt) {
    JS::ReportUncatchableException(cx);
    return false;
  }

  args.rval().setUndefined();
  return true;
}

static void KillWatchdog(JSContext* cx) {
  ShellContext* sc = GetShellContext(cx);
  Maybe<Thread> thread;

  {
    LockGuard<Mutex> guard(sc->watchdogLock);
    std::swap(sc->watchdogThread, thread);
    if (thread) {
      // The watchdog thread becoming Nothing is its signal to exit.
      sc->watchdogWakeup.notify_one();
    }
  }
  if (thread) {
    thread->join();
  }

  MOZ_ASSERT(!sc->watchdogThread);
}

static void WatchdogMain(JSContext* cx) {
  ThisThread::SetName("JS Watchdog");

  ShellContext* sc = GetShellContext(cx);

  {
    LockGuard<Mutex> guard(sc->watchdogLock);
    while (sc->watchdogThread) {
      auto now = TimeStamp::Now();
      if (sc->watchdogTimeout && now >= sc->watchdogTimeout.value()) {
        /*
         * The timeout has just expired. Request an interrupt callback
         * outside the lock.
         */
        sc->watchdogTimeout = Nothing();
        {
          UnlockGuard unlock(guard);
          CancelExecution(cx);
        }

        /* Wake up any threads doing sleep. */
        sc->sleepWakeup.notify_all();
      } else {
        if (sc->watchdogTimeout) {
          /*
           * Time hasn't expired yet. Simulate an interrupt callback
           * which doesn't abort execution.
           */
          JS_RequestInterruptCallback(cx);
        }

        TimeDuration sleepDuration = sc->watchdogTimeout
                                         ? TimeDuration::FromSeconds(0.1)
                                         : TimeDuration::Forever();
        sc->watchdogWakeup.wait_for(guard, sleepDuration);
      }
    }
  }
}

static bool ScheduleWatchdog(JSContext* cx, double t) {
  ShellContext* sc = GetShellContext(cx);

  if (t <= 0) {
    LockGuard<Mutex> guard(sc->watchdogLock);
    sc->watchdogTimeout = Nothing();
    return true;
  }

#ifdef __wasi__
  return false;
#endif

  auto interval = TimeDuration::FromSeconds(t);
  auto timeout = TimeStamp::Now() + interval;
  LockGuard<Mutex> guard(sc->watchdogLock);
  if (!sc->watchdogThread) {
    MOZ_ASSERT(!sc->watchdogTimeout);
    sc->watchdogThread.emplace();
    AutoEnterOOMUnsafeRegion oomUnsafe;
    if (!sc->watchdogThread->init(WatchdogMain, cx)) {
      oomUnsafe.crash("watchdogThread.init");
    }
  } else if (!sc->watchdogTimeout || timeout < sc->watchdogTimeout.value()) {
    sc->watchdogWakeup.notify_one();
  }
  sc->watchdogTimeout = Some(timeout);
  return true;
}

static void KillWorkerThreads(JSContext* cx) {
  MOZ_ASSERT_IF(!CanUseExtraThreads(), workerThreads.empty());

  if (!workerThreadsLock) {
    MOZ_ASSERT(workerThreads.empty());
    return;
  }

  while (true) {
    // We need to leave the AutoLockWorkerThreads scope before we call
    // js::Thread::join, to avoid deadlocks when AutoLockWorkerThreads is
    // used by the worker thread.
    UniquePtr<Thread> thread;
    {
      AutoLockWorkerThreads alwt;
      if (workerThreads.empty()) {
        break;
      }
      thread = std::move(workerThreads.back());
      workerThreads.popBack();
    }
    thread->join();
  }

  workerThreads.clearAndFree();

  js_delete(workerThreadsLock);
  workerThreadsLock = nullptr;
}

static void CancelExecution(JSContext* cx) {
  ShellContext* sc = GetShellContext(cx);
  sc->serviceInterrupt = true;
  JS_RequestInterruptCallback(cx);
}

static bool SetTimeoutValue(JSContext* cx, double t) {
  if (std::isnan(t)) {
    JS_ReportErrorASCII(cx, "timeout is not a number");
    return false;
  }
  const TimeDuration MAX_TIMEOUT_INTERVAL =
      TimeDuration::FromSeconds(MAX_TIMEOUT_SECONDS);
  if (TimeDuration::FromSeconds(t) > MAX_TIMEOUT_INTERVAL) {
    JS_ReportErrorASCII(cx, "Excessive timeout value");
    return false;
  }
  GetShellContext(cx)->timeoutInterval = t;
  if (!ScheduleWatchdog(cx, t)) {
    JS_ReportErrorASCII(cx, "Failed to create the watchdog");
    return false;
  }
  return true;
}

static bool Timeout(JSContext* cx, unsigned argc, Value* vp) {
  ShellContext* sc = GetShellContext(cx);
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() == 0) {
    args.rval().setNumber(sc->timeoutInterval);
    return true;
  }

  if (args.length() > 2) {
    JS_ReportErrorASCII(cx, "Wrong number of arguments");
    return false;
  }

  double t;
  if (!ToNumber(cx, args[0], &t)) {
    return false;
  }

  if (args.length() > 1) {
    RootedValue value(cx, args[1]);
    if (!value.isObject() || !value.toObject().is<JSFunction>()) {
      JS_ReportErrorASCII(cx, "Second argument must be a timeout function");
      return false;
    }
    sc->interruptFunc = value;
    sc->haveInterruptFunc = true;
  }

  args.rval().setUndefined();
  return SetTimeoutValue(cx, t);
}

static bool InterruptIf(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() != 1) {
    JS_ReportErrorASCII(cx, "Wrong number of arguments");
    return false;
  }

  if (ToBoolean(args[0])) {
    GetShellContext(cx)->serviceInterrupt = true;
    JS_RequestInterruptCallback(cx);
  }

  args.rval().setUndefined();
  return true;
}

static bool InvokeInterruptCallbackWrapper(JSContext* cx, unsigned argc,
                                           Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() != 1) {
    JS_ReportErrorASCII(cx, "Wrong number of arguments");
    return false;
  }

  GetShellContext(cx)->serviceInterrupt = true;
  JS_RequestInterruptCallback(cx);
  bool interruptRv = CheckForInterrupt(cx);

  // The interrupt handler could have set a pending exception. Since we call
  // back into JS, don't have it see the pending exception. If we have an
  // uncatchable exception that's not propagating a debug mode forced
  // return, return.
  if (!interruptRv && !cx->isExceptionPending() &&
      !cx->isPropagatingForcedReturn()) {
    return false;
  }

  JS::AutoSaveExceptionState savedExc(cx);

  FixedInvokeArgs<1> iargs(cx);

  iargs[0].setBoolean(interruptRv);

  RootedValue rv(cx);
  if (!js::Call(cx, args[0], UndefinedHandleValue, iargs, &rv)) {
    return false;
  }

  args.rval().setUndefined();
  return interruptRv;
}

static bool SetInterruptCallback(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() != 1) {
    JS_ReportErrorASCII(cx, "Wrong number of arguments");
    return false;
  }

  RootedValue value(cx, args[0]);
  if (!value.isObject() || !value.toObject().is<JSFunction>()) {
    JS_ReportErrorASCII(cx, "Argument must be a function");
    return false;
  }
  GetShellContext(cx)->interruptFunc = value;
  GetShellContext(cx)->haveInterruptFunc = true;

  args.rval().setUndefined();
  return true;
}

#ifdef DEBUG
// var s0 = "A".repeat(10*1024);
// interruptRegexp(/a(bc|bd)/, s0);
// first arg is regexp
// second arg is string
static bool InterruptRegexp(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  ShellContext* sc = GetShellContext(cx);
  RootedObject callee(cx, &args.callee());

  if (args.length() != 2) {
    ReportUsageErrorASCII(cx, callee, "Wrong number of arguments.");
    return false;
  }
  if (!(args[0].isObject() && args[0].toObject().is<RegExpObject>())) {
    ReportUsageErrorASCII(cx, callee,
                          "First argument must be a regular expression.");
    return false;
  }
  if (!args[1].isString()) {
    ReportUsageErrorASCII(cx, callee, "Second argument must be a String.");
    return false;
  }
  // Set interrupt flags
  sc->serviceInterrupt = true;
  js::irregexp::IsolateSetShouldSimulateInterrupt(cx->isolate);

  RootedObject regexp(cx, &args[0].toObject());
  RootedString string(cx, args[1].toString());
  int32_t lastIndex = 0;

  return js::RegExpMatcherRaw(cx, regexp, string, lastIndex, nullptr,
                              args.rval());
}
#endif

static bool CheckRegExpSyntax(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject callee(cx, &args.callee());

  if (args.length() != 1) {
    ReportUsageErrorASCII(cx, callee, "Wrong number of arguments.");
    return false;
  }
  if (!args[0].isString()) {
    ReportUsageErrorASCII(cx, callee, "First argument must be a string.");
    return false;
  }

  RootedString string(cx, args[0].toString());
  AutoStableStringChars stableChars(cx);
  if (!stableChars.initTwoByte(cx, string)) {
    return false;
  }

  const char16_t* chars = stableChars.twoByteRange().begin().get();
  size_t length = string->length();

  Rooted<JS::Value> error(cx);
  if (!JS::CheckRegExpSyntax(cx, chars, length, JS::RegExpFlag::NoFlags,
                             &error)) {
    return false;
  }

  args.rval().set(error);
  return true;
}

static bool IsPrefAvailable(const char* pref) {
  if (!fuzzingSafe) {
    // All prefs in fuzzing unsafe mode are enabled.
    return true;
  }
#define WASM_FEATURE(NAME, LOWER_NAME, COMPILE_PRED, COMPILER_PRED, FLAG_PRED, \
                     FLAG_FORCE_ON, FLAG_FUZZ_ON, PREF)                        \
  if constexpr (!FLAG_FUZZ_ON) {                                               \
    if (strcmp("wasm_" #PREF, pref) == 0) {                                    \
      return false;                                                            \
    }                                                                          \
  }
  JS_FOR_WASM_FEATURES(WASM_FEATURE)
#undef WASM_FEATURE
  return true;
}

template <typename T>
static bool ParsePrefValue(const char* name, const char* val, T* result) {
  if constexpr (std::is_same_v<T, bool>) {
    if (strcmp(val, "true") == 0) {
      *result = true;
      return true;
    }
    if (strcmp(val, "false") == 0) {
      *result = false;
      return true;
    }
    fprintf(stderr, "Invalid value for boolean pref %s: %s\n", name, val);
    return false;
  } else {
    static_assert(std::is_same_v<T, int32_t> || std::is_same_v<T, uint32_t>);
    char* end;
    long v = strtol(val, &end, 10);
    if (end != val + strlen(val) || static_cast<long>(static_cast<T>(v)) != v) {
      fprintf(stderr, "Invalid value for integer pref %s: %s\n", name, val);
      return false;
    }
    *result = static_cast<T>(v);
    return true;
  }
}

static bool SetPrefToTrueForBool(const char* name) {
  // Search for a matching pref and try to set it to a default value for the
  // type.
#define CHECK_PREF(NAME, CPP_NAME, TYPE, SETTER, IS_STARTUP_PREF)      \
  if (strcmp(name, NAME) == 0) {                                       \
    if constexpr (std::is_same_v<TYPE, bool>) {                        \
      JS::Prefs::SETTER(true);                                         \
      return true;                                                     \
    } else {                                                           \
      fprintf(stderr, "Pref %s must have a value specified.\n", name); \
      return false;                                                    \
    }                                                                  \
  }
  FOR_EACH_JS_PREF(CHECK_PREF)
#undef CHECK_PREF

  // Nothing matched. If --fuzzing-safe is used, return true after printing a
  // message, to continue execution without breaking fuzzing when a pref is
  // removed.
  if (fuzzingSafe) {
    fprintf(stderr, "Warning: Ignoring unknown pref name: %s\n", name);
    return true;
  }
  fprintf(stderr, "Invalid pref name: %s\n", name);
  return false;
}

template <typename T>
static bool ConvertPrefValue(JSContext* cx, HandleValue val, T* result) {
  if constexpr (std::is_same_v<T, bool>) {
    *result = ToBoolean(val);
    return true;
  } else if constexpr (std::is_same_v<T, int32_t>) {
    return ToInt32(cx, val, result);
  } else {
    static_assert(std::is_same_v<T, uint32_t>);
    return ToUint32(cx, val, result);
  }
}

static bool SetPrefValue(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "setPrefValue", 2)) {
    return false;
  }

  if (!args[0].isString()) {
    JS_ReportErrorASCII(cx, "expected string argument");
    return false;
  }

  Rooted<JSLinearString*> name(cx, args[0].toString()->ensureLinear(cx));
  if (!name) {
    return false;
  }

  // Search for a matching pref and try to set it to the provided value.
#define CHECK_PREF(NAME, CPP_NAME, TYPE, SETTER, IS_STARTUP_PREF)             \
  if (IsPrefAvailable(NAME) && StringEqualsAscii(name, NAME)) {               \
    if (IS_STARTUP_PREF) {                                                    \
      JS_ReportErrorASCII(cx, "%s is a startup pref and can't be set", NAME); \
      return false;                                                           \
    }                                                                         \
    TYPE v;                                                                   \
    if (!ConvertPrefValue<TYPE>(cx, args[1], &v)) {                           \
      return false;                                                           \
    }                                                                         \
    JS::Prefs::SETTER(v);                                                     \
    args.rval().setUndefined();                                               \
    return true;                                                              \
  }
  FOR_EACH_JS_PREF(CHECK_PREF)
#undef CHECK_PREF

  // Fuzzing ignores missing prefs so it doesn't break if we remove a pref
  if (fuzzingSafe) {
    args.rval().setUndefined();
    return true;
  }
  JS_ReportErrorASCII(cx, "invalid pref name");
  return false;
}

static bool SetPrefToValue(const char* name, size_t nameLen,
                           const char* value) {
  // Search for a matching pref and try to set it to the provided value.
#define CHECK_PREF(NAME, CPP_NAME, TYPE, SETTER, IS_STARTUP_PREF)         \
  if (nameLen == strlen(NAME) && memcmp(name, NAME, strlen(NAME)) == 0) { \
    TYPE v;                                                               \
    if (!ParsePrefValue<TYPE>(NAME, value, &v)) {                         \
      return false;                                                       \
    }                                                                     \
    JS::Prefs::SETTER(v);                                                 \
    return true;                                                          \
  }
  FOR_EACH_JS_PREF(CHECK_PREF)
#undef CHECK_PREF

  // Nothing matched. If --fuzzing-safe is used, return true after printing a
  // message, to continue execution without breaking fuzzing when a pref is
  // removed.
  if (fuzzingSafe) {
    fprintf(stderr, "Warning: Ignoring unknown pref name: %s\n", name);
    return true;
  }
  fprintf(stderr, "Invalid pref name: %s\n", name);
  return false;
}

static bool SetPref(const char* pref) {
  const char* assign = strchr(pref, '=');
  if (!assign) {
    if (IsPrefAvailable(pref) && !SetPrefToTrueForBool(pref)) {
      return false;
    }
    return true;
  }

  size_t nameLen = assign - pref;
  const char* valStart = assign + 1;  // Skip '='.

  if (IsPrefAvailable(pref) && !SetPrefToValue(pref, nameLen, valStart)) {
    return false;
  }
  return true;
}

static void ListPrefs() {
  auto printPref = [](const char* name, auto defaultVal) {
    if (!IsPrefAvailable(name)) {
      return;
    }
    using T = decltype(defaultVal);
    if constexpr (std::is_same_v<T, bool>) {
      fprintf(stderr, "%s=%s\n", name, defaultVal ? "true" : "false");
    } else if constexpr (std::is_same_v<T, int32_t>) {
      fprintf(stderr, "%s=%d\n", name, defaultVal);
    } else {
      static_assert(std::is_same_v<T, uint32_t>);
      fprintf(stderr, "%s=%u\n", name, defaultVal);
    }
  };

#define PRINT_PREF(NAME, CPP_NAME, TYPE, SETTER, IS_STARTUP_PREF) \
  printPref(NAME, JS::Prefs::CPP_NAME());
  FOR_EACH_JS_PREF(PRINT_PREF)
#undef PRINT_PREF
}

static bool SetJitCompilerOption(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject callee(cx, &args.callee());

  if (args.length() != 2) {
    ReportUsageErrorASCII(cx, callee, "Wrong number of arguments.");
    return false;
  }

  if (!args[0].isString()) {
    ReportUsageErrorASCII(cx, callee, "First argument must be a String.");
    return false;
  }

  if (!args[1].isInt32()) {
    ReportUsageErrorASCII(cx, callee, "Second argument must be an Int32.");
    return false;
  }

  // Disallow setting JIT options when there are worker threads, to avoid
  // races.
  if (workerThreadsLock) {
    ReportUsageErrorASCII(
        cx, callee, "Can't set JIT options when there are worker threads.");
    return false;
  }

  JSLinearString* strArg = JS_EnsureLinearString(cx, args[0].toString());
  if (!strArg) {
    return false;
  }

#define JIT_COMPILER_MATCH(key, string)                        \
  else if (JS_LinearStringEqualsLiteral(strArg, string)) opt = \
      JSJITCOMPILER_##key;

  JSJitCompilerOption opt = JSJITCOMPILER_NOT_AN_OPTION;
  if (false) {
  }
  JIT_COMPILER_OPTIONS(JIT_COMPILER_MATCH);
#undef JIT_COMPILER_MATCH

  if (opt == JSJITCOMPILER_NOT_AN_OPTION) {
    ReportUsageErrorASCII(
        cx, callee,
        "First argument does not name a valid option (see jsapi.h).");
    return false;
  }

  int32_t number = args[1].toInt32();
  if (number < 0) {
    number = -1;
  }

  // Disallow enabling or disabling the Baseline Interpreter at runtime.
  // Enabling is a problem because the Baseline Interpreter code is only
  // present if the interpreter was enabled when the JitRuntime was created.
  // To support disabling we would have to discard all JitScripts. Furthermore,
  // we really want JitOptions to be immutable after startup so it's better to
  // use shell flags.
  if (opt == JSJITCOMPILER_BASELINE_INTERPRETER_ENABLE &&
      bool(number) != jit::IsBaselineInterpreterEnabled()) {
    JS_ReportErrorASCII(cx,
                        "Enabling or disabling the Baseline Interpreter at "
                        "runtime is not supported.");
    return false;
  }

  // Throw if disabling the JITs and there's JIT code on the stack, to avoid
  // assertion failures.
  if ((opt == JSJITCOMPILER_BASELINE_ENABLE ||
       opt == JSJITCOMPILER_ION_ENABLE) &&
      number == 0) {
    js::jit::JitActivationIterator iter(cx);
    if (!iter.done()) {
      JS_ReportErrorASCII(cx,
                          "Can't turn off JITs with JIT code on the stack.");
      return false;
    }
  }

  // Changing code memory protection settings at runtime is not supported. Don't
  // throw if not changing the setting because some jit-tests depend on that.
  if (opt == JSJITCOMPILER_WRITE_PROTECT_CODE) {
    uint32_t writeProtect;
    MOZ_ALWAYS_TRUE(JS_GetGlobalJitCompilerOption(
        cx, JSJITCOMPILER_WRITE_PROTECT_CODE, &writeProtect));
    if (bool(number) != writeProtect) {
      JS_ReportErrorASCII(cx, "Can't change code write protection at runtime");
      return false;
    }
    return true;
  }

  // Throw if trying to disable all the Wasm compilers.  The logic here is that
  // if we're trying to disable a compiler that is currently enabled and that is
  // the last compiler enabled then we must throw.
  //
  // Note that this check does not prevent an error from being thrown later.
  // Actual compiler availability is dynamic and depends on other conditions,
  // such as other options set and whether a debugger is present.
  if ((opt == JSJITCOMPILER_WASM_JIT_BASELINE ||
       opt == JSJITCOMPILER_WASM_JIT_OPTIMIZING) &&
      number == 0) {
    uint32_t baseline, optimizing;
    MOZ_ALWAYS_TRUE(JS_GetGlobalJitCompilerOption(
        cx, JSJITCOMPILER_WASM_JIT_BASELINE, &baseline));
    MOZ_ALWAYS_TRUE(JS_GetGlobalJitCompilerOption(
        cx, JSJITCOMPILER_WASM_JIT_OPTIMIZING, &optimizing));
    if (baseline + optimizing == 1) {
      if ((opt == JSJITCOMPILER_WASM_JIT_BASELINE && baseline) ||
          (opt == JSJITCOMPILER_WASM_JIT_OPTIMIZING && optimizing)) {
        JS_ReportErrorASCII(
            cx,
            "Disabling all the Wasm compilers at runtime is not supported.");
        return false;
      }
    }
  }

  // JIT compiler options are process-wide, so we have to stop off-thread
  // compilations for all runtimes to avoid races.
  WaitForAllHelperThreads();

  // Only release JIT code for the current runtime because there's no good
  // way to discard code for other runtimes.
  ReleaseAllJITCode(cx->gcContext());

  JS_SetGlobalJitCompilerOption(cx, opt, uint32_t(number));

  args.rval().setUndefined();
  return true;
}

static bool EnableLastWarning(JSContext* cx, unsigned argc, Value* vp) {
  ShellContext* sc = GetShellContext(cx);
  CallArgs args = CallArgsFromVp(argc, vp);

  sc->lastWarningEnabled = true;
  sc->lastWarning.setNull();

  args.rval().setUndefined();
  return true;
}

static bool DisableLastWarning(JSContext* cx, unsigned argc, Value* vp) {
  ShellContext* sc = GetShellContext(cx);
  CallArgs args = CallArgsFromVp(argc, vp);

  sc->lastWarningEnabled = false;
  sc->lastWarning.setNull();

  args.rval().setUndefined();
  return true;
}

static bool GetLastWarning(JSContext* cx, unsigned argc, Value* vp) {
  ShellContext* sc = GetShellContext(cx);
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!sc->lastWarningEnabled) {
    JS_ReportErrorASCII(cx, "Call enableLastWarning first.");
    return false;
  }

  if (!JS_WrapValue(cx, &sc->lastWarning)) {
    return false;
  }

  args.rval().set(sc->lastWarning);
  return true;
}

static bool ClearLastWarning(JSContext* cx, unsigned argc, Value* vp) {
  ShellContext* sc = GetShellContext(cx);
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!sc->lastWarningEnabled) {
    JS_ReportErrorASCII(cx, "Call enableLastWarning first.");
    return false;
  }

  sc->lastWarning.setNull();

  args.rval().setUndefined();
  return true;
}

#if defined(DEBUG) || defined(JS_JITSPEW)
static bool StackDump(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!gOutFile->isOpen()) {
    JS_ReportErrorASCII(cx, "output file is closed");
    return false;
  }

  bool showArgs = ToBoolean(args.get(0));
  bool showLocals = ToBoolean(args.get(1));
  bool showThisProps = ToBoolean(args.get(2));

  JS::UniqueChars buf =
      JS::FormatStackDump(cx, showArgs, showLocals, showThisProps);
  if (!buf) {
    fputs("Failed to format JavaScript stack for dump\n", gOutFile->fp);
    JS_ClearPendingException(cx);
  } else {
    fputs(buf.get(), gOutFile->fp);
  }

  args.rval().setUndefined();
  return true;
}
#endif

MOZ_ASAN_IGNORE static bool StackPointerInfo(JSContext* cx, unsigned argc,
                                             Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Copy the truncated stack pointer to the result.  This value is not used
  // as a pointer but as a way to measure frame-size from JS.
  // The ASAN must be disabled for this function -- it may allocate `args`
  // not on the stack.
  args.rval().setInt32(int32_t(reinterpret_cast<size_t>(&args) & 0xfffffff));
  return true;
}

static bool Elapsed(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() == 0) {
    double d = PRMJ_Now() - GetShellContext(cx)->startTime;
    args.rval().setDouble(d);
    return true;
  }
  JS_ReportErrorASCII(cx, "Wrong number of arguments");
  return false;
}

static ShellCompartmentPrivate* EnsureShellCompartmentPrivate(JSContext* cx) {
  Compartment* comp = cx->compartment();
  auto priv =
      static_cast<ShellCompartmentPrivate*>(JS_GetCompartmentPrivate(comp));
  if (!priv) {
    priv = cx->new_<ShellCompartmentPrivate>();
    JS_SetCompartmentPrivate(cx->compartment(), priv);
  }
  return priv;
}

static bool ParseModule(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "parseModule", 1)) {
    return false;
  }

  if (!args[0].isString()) {
    const char* typeName = InformalValueTypeName(args[0]);
    JS_ReportErrorASCII(cx, "expected string to compile, got %s", typeName);
    return false;
  }

  JSString* scriptContents = args[0].toString();

  UniqueChars filename;
  CompileOptions options(cx);
  bool jsonModule = false;
  if (args.length() > 1) {
    if (!args[1].isString()) {
      const char* typeName = InformalValueTypeName(args[1]);
      JS_ReportErrorASCII(cx, "expected filename string, got %s", typeName);
      return false;
    }

    RootedString str(cx, args[1].toString());
    filename = JS_EncodeStringToUTF8(cx, str);
    if (!filename) {
      return false;
    }

    options.setFileAndLine(filename.get(), 1);

    // The 2nd argument is the module type string. "js" or "json" is expected.
    if (args.length() == 3) {
      if (!args[2].isString()) {
        const char* typeName = InformalValueTypeName(args[2]);
        JS_ReportErrorASCII(cx, "expected moduleType string, got %s", typeName);
        return false;
      }

      RootedString str(cx, args[2].toString());
      JSLinearString* linearStr = JS_EnsureLinearString(cx, str);
      if (!linearStr) {
        return false;
      }
      if (JS_LinearStringEqualsLiteral(linearStr, "json")) {
        jsonModule = true;
      } else if (!JS_LinearStringEqualsLiteral(linearStr, "js")) {
        JS_ReportErrorASCII(cx, "moduleType string ('js' or 'json') expected");
        return false;
      }
    }
  } else {
    options.setFileAndLine("<string>", 1);
  }

  AutoStableStringChars linearChars(cx);
  if (!linearChars.initTwoByte(cx, scriptContents)) {
    return false;
  }

  JS::SourceText<char16_t> srcBuf;
  if (!srcBuf.initMaybeBorrowed(cx, linearChars)) {
    return false;
  }

  RootedObject module(cx);
  if (jsonModule) {
    module = JS::CompileJsonModule(cx, options, srcBuf);
  } else {
    options.setModule();
    module = JS::CompileModule(cx, options, srcBuf);
  }
  if (!module) {
    return false;
  }

  Rooted<ShellModuleObjectWrapper*> wrapper(
      cx, ShellModuleObjectWrapper::create(cx, module.as<ModuleObject>()));
  if (!wrapper) {
    return false;
  }
  args.rval().setObject(*wrapper);
  return true;
}

// A JSObject that holds XDRBuffer.
class XDRBufferObject : public NativeObject {
  static const size_t VECTOR_SLOT = 0;
  static const unsigned RESERVED_SLOTS = 1;

 public:
  static const JSClassOps classOps_;
  static const JSClass class_;

  [[nodiscard]] inline static XDRBufferObject* create(
      JSContext* cx, JS::TranscodeBuffer&& buf);

  JS::TranscodeBuffer* data() const {
    Value value = getReservedSlot(VECTOR_SLOT);
    auto buf = static_cast<JS::TranscodeBuffer*>(value.toPrivate());
    MOZ_ASSERT(buf);
    return buf;
  }

  bool hasData() const {
    // Data may not be present if we hit OOM in initialization.
    return !getReservedSlot(VECTOR_SLOT).isUndefined();
  }

  static void finalize(JS::GCContext* gcx, JSObject* obj);
};

/*static */ const JSClassOps XDRBufferObject::classOps_ = {
    nullptr,                    // addProperty
    nullptr,                    // delProperty
    nullptr,                    // enumerate
    nullptr,                    // newEnumerate
    nullptr,                    // resolve
    nullptr,                    // mayResolve
    XDRBufferObject::finalize,  // finalize
    nullptr,                    // call
    nullptr,                    // construct
    nullptr,                    // trace
};

/*static */ const JSClass XDRBufferObject::class_ = {
    "XDRBufferObject",
    JSCLASS_HAS_RESERVED_SLOTS(XDRBufferObject::RESERVED_SLOTS) |
        JSCLASS_BACKGROUND_FINALIZE,
    &XDRBufferObject::classOps_,
};

XDRBufferObject* XDRBufferObject::create(JSContext* cx,
                                         JS::TranscodeBuffer&& buf) {
  XDRBufferObject* bufObj =
      NewObjectWithGivenProto<XDRBufferObject>(cx, nullptr);
  if (!bufObj) {
    return nullptr;
  }

  auto heapBuf = cx->make_unique<JS::TranscodeBuffer>(std::move(buf));
  if (!heapBuf) {
    return nullptr;
  }

  size_t len = heapBuf->length();
  InitReservedSlot(bufObj, VECTOR_SLOT, heapBuf.release(), len,
                   MemoryUse::XDRBufferElements);

  return bufObj;
}

void XDRBufferObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  XDRBufferObject* buf = &obj->as<XDRBufferObject>();
  if (buf->hasData()) {
    gcx->delete_(buf, buf->data(), buf->data()->length(),
                 MemoryUse::XDRBufferElements);
  }
}

static bool InstantiateModuleStencil(JSContext* cx, uint32_t argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!args.requireAtLeast(cx, "instantiateModuleStencil", 1)) {
    return false;
  }

  /* Prepare the input byte array. */
  if (!args[0].isObject()) {
    JS_ReportErrorASCII(cx,
                        "instantiateModuleStencil: Stencil object expected");
    return false;
  }
  Rooted<js::StencilObject*> stencilObj(
      cx, args[0].toObject().maybeUnwrapIf<js::StencilObject>());
  if (!stencilObj) {
    JS_ReportErrorASCII(cx,
                        "instantiateModuleStencil: Stencil object expected");
    return false;
  }

  if (!stencilObj->stencil()->getInitial()->isModule()) {
    JS_ReportErrorASCII(cx,
                        "instantiateModuleStencil: Module stencil expected");
    return false;
  }

  CompileOptions options(cx);
  UniqueChars fileNameBytes;
  if (args.length() == 2) {
    if (!args[1].isObject()) {
      JS_ReportErrorASCII(
          cx, "instantiateModuleStencil: The 2nd argument must be an object");
      return false;
    }

    RootedObject opts(cx, &args[1].toObject());
    if (!js::ParseCompileOptions(cx, options, opts, &fileNameBytes)) {
      return false;
    }
  }

  if (!js::ValidateLazinessOfStencilAndGlobal(cx, stencilObj->stencil())) {
    return false;
  }

  JS::InstantiateOptions instantiateOptions(options);
  Rooted<JSObject*> modObject(
      cx, JS::InstantiateModuleStencil(cx, instantiateOptions,
                                       stencilObj->stencil(), nullptr));
  if (!modObject) {
    return false;
  }

  Rooted<ModuleObject*> module(cx, &modObject->as<ModuleObject>());
  Rooted<ShellModuleObjectWrapper*> wrapper(
      cx, ShellModuleObjectWrapper::create(cx, module));
  if (!wrapper) {
    return false;
  }
  args.rval().setObject(*wrapper);
  return true;
}

static bool InstantiateModuleStencilXDR(JSContext* cx, uint32_t argc,
                                        Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!args.requireAtLeast(cx, "instantiateModuleStencilXDR", 1)) {
    return false;
  }

  /* Prepare the input byte array. */
  if (!args[0].isObject()) {
    JS_ReportErrorASCII(
        cx, "instantiateModuleStencilXDR: Stencil XDR object expected");
    return false;
  }
  Rooted<StencilXDRBufferObject*> xdrObj(
      cx, args[0].toObject().maybeUnwrapIf<StencilXDRBufferObject>());
  if (!xdrObj) {
    JS_ReportErrorASCII(
        cx, "instantiateModuleStencilXDR: Stencil XDR object expected");
    return false;
  }
  MOZ_ASSERT(xdrObj->hasBuffer());

  CompileOptions options(cx);
  UniqueChars fileNameBytes;
  if (args.length() == 2) {
    if (!args[1].isObject()) {
      JS_ReportErrorASCII(
          cx,
          "instantiateModuleStencilXDR: The 2nd argument must be an object");
      return false;
    }

    RootedObject opts(cx, &args[1].toObject());
    if (!js::ParseCompileOptions(cx, options, opts, &fileNameBytes)) {
      return false;
    }
  }

  JS::TranscodeRange xdrRange(xdrObj->buffer(), xdrObj->bufferLength());
  JS::DecodeOptions decodeOptions(options);
  RefPtr<JS::Stencil> stencil;
  auto result =
      JS::DecodeStencil(cx, decodeOptions, xdrRange, getter_AddRefs(stencil));
  if (result == JS::TranscodeResult::Throw) {
    return false;
  }
  if (JS::IsTranscodeFailureResult(result)) {
    JS_ReportErrorASCII(cx, "Decoding failure");
    return false;
  }

  if (!stencil->getInitial()->isModule()) {
    JS_ReportErrorASCII(cx,
                        "instantiateModuleStencilXDR: Module stencil expected");
    return false;
  }

  if (!js::ValidateLazinessOfStencilAndGlobal(cx, stencil.get())) {
    return false;
  }

  JS::InstantiateOptions instantiateOptions(options);
  Rooted<JSObject*> modObject(
      cx,
      JS::InstantiateModuleStencil(cx, instantiateOptions, stencil, nullptr));
  if (!modObject) {
    return false;
  }

  Rooted<ModuleObject*> module(cx, &modObject->as<ModuleObject>());
  Rooted<ShellModuleObjectWrapper*> wrapper(
      cx, ShellModuleObjectWrapper::create(cx, module));
  if (!wrapper) {
    return false;
  }
  args.rval().setObject(*wrapper);
  return true;
}

static bool RegisterModule(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "registerModule", 2)) {
    return false;
  }

  if (!args[0].isString()) {
    const char* typeName = InformalValueTypeName(args[0]);
    JS_ReportErrorASCII(cx, "Expected string, got %s", typeName);
    return false;
  }

  if (!args[1].isObject() ||
      !args[1].toObject().is<ShellModuleObjectWrapper>()) {
    const char* typeName = InformalValueTypeName(args[1]);
    JS_ReportErrorASCII(cx, "Expected module, got %s", typeName);
    return false;
  }

  ShellContext* sc = GetShellContext(cx);
  Rooted<ModuleObject*> module(
      cx, args[1].toObject().as<ShellModuleObjectWrapper>().get());
  if (module->realm() != cx->realm()) {
    JS_ReportErrorASCII(cx, "Module is in a different realm");
    return false;
  }

  Rooted<JSAtom*> specifier(cx, AtomizeString(cx, args[0].toString()));
  if (!specifier) {
    return false;
  }

  JS::ModuleType moduleType = JS::ModuleType::JavaScript;
  if (module->hasSyntheticModuleFields()) {
    moduleType = JS::ModuleType::JSON;
  }

  RootedObject moduleRequest(
      cx, ModuleRequestObject::create(cx, specifier, moduleType));
  if (!moduleRequest) {
    return false;
  }

  if (!sc->moduleLoader->registerTestModule(cx, moduleRequest, module)) {
    return false;
  }

  Rooted<ShellModuleObjectWrapper*> wrapper(
      cx, ShellModuleObjectWrapper::create(cx, module));
  if (!wrapper) {
    return false;
  }
  args.rval().setObject(*wrapper);
  return true;
}

static bool ClearModules(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  ShellContext* sc = GetShellContext(cx);
  sc->moduleLoader->clearModules(cx);
  args.rval().setUndefined();
  return true;
}

static bool ModuleLink(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() != 1 || !args[0].isObject()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_INVALID_ARGS,
                              "moduleLink");
    return false;
  }

  RootedObject object(cx, UncheckedUnwrap(&args[0].toObject()));
  if (!object->is<ShellModuleObjectWrapper>()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_INVALID_ARGS,
                              "moduleLink");
    return false;
  }

  AutoRealm ar(cx, object);

  Rooted<ModuleObject*> module(cx,
                               object->as<ShellModuleObjectWrapper>().get());
  if (!JS::ModuleLink(cx, module)) {
    return false;
  }

  args.rval().setUndefined();
  return true;
}

static bool ModuleEvaluate(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() != 1 || !args[0].isObject()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_INVALID_ARGS,
                              "moduleEvaluate");
    return false;
  }

  RootedObject object(cx, UncheckedUnwrap(&args[0].toObject()));
  if (!object->is<ShellModuleObjectWrapper>()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_INVALID_ARGS,
                              "moduleEvaluate");
    return false;
  }

  {
    AutoRealm ar(cx, object);

    Rooted<ModuleObject*> module(cx,
                                 object->as<ShellModuleObjectWrapper>().get());
    if (!JS::ModuleEvaluate(cx, module, args.rval())) {
      return false;
    }
  }

  return JS_WrapValue(cx, args.rval());
}

static ModuleEnvironmentObject* GetModuleInitialEnvironment(
    JSContext* cx, Handle<ModuleObject*> module) {
  // Use the initial environment so that tests can check bindings exists
  // before they have been instantiated.
  Rooted<ModuleEnvironmentObject*> env(cx, &module->initialEnvironment());
  MOZ_ASSERT(env);
  return env;
}

static bool GetModuleEnvironmentNames(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() != 1) {
    JS_ReportErrorASCII(cx, "Wrong number of arguments");
    return false;
  }

  if (!args[0].isObject() ||
      !args[0].toObject().is<ShellModuleObjectWrapper>()) {
    JS_ReportErrorASCII(cx,
                        "First argument should be a ShellModuleObjectWrapper");
    return false;
  }

  Rooted<ModuleObject*> module(
      cx, args[0].toObject().as<ShellModuleObjectWrapper>().get());
  if (module->hasSyntheticModuleFields()) {
    JS_ReportErrorASCII(cx,
                        "Operation is not supported on JSON module objects.");
    return false;
  }

  if (module->hadEvaluationError()) {
    JS_ReportErrorASCII(cx, "Module environment unavailable");
    return false;
  }

  Rooted<ModuleEnvironmentObject*> env(cx,
                                       GetModuleInitialEnvironment(cx, module));
  Rooted<IdVector> ids(cx, IdVector(cx));
  if (!JS_Enumerate(cx, env, &ids)) {
    return false;
  }

  // The "*namespace*" binding is a detail of current implementation so hide
  // it to give stable results in tests.
  ids.eraseIfEqual(NameToId(cx->names().star_namespace_star_));

  uint32_t length = ids.length();
  Rooted<ArrayObject*> array(cx, NewDenseFullyAllocatedArray(cx, length));
  if (!array) {
    return false;
  }

  array->setDenseInitializedLength(length);
  for (uint32_t i = 0; i < length; i++) {
    array->initDenseElement(i, StringValue(ids[i].toString()));
  }

  args.rval().setObject(*array);
  return true;
}

static bool GetModuleEnvironmentValue(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() != 2) {
    JS_ReportErrorASCII(cx, "Wrong number of arguments");
    return false;
  }

  if (!args[0].isObject() ||
      !args[0].toObject().is<ShellModuleObjectWrapper>()) {
    JS_ReportErrorASCII(cx,
                        "First argument should be a ShellModuleObjectWrapper");
    return false;
  }

  if (!args[1].isString()) {
    JS_ReportErrorASCII(cx, "Second argument should be a string");
    return false;
  }

  Rooted<ModuleObject*> module(
      cx, args[0].toObject().as<ShellModuleObjectWrapper>().get());
  if (module->hasSyntheticModuleFields()) {
    JS_ReportErrorASCII(cx,
                        "Operation is not supported on JSON module objects.");
    return false;
  }

  if (module->hadEvaluationError()) {
    JS_ReportErrorASCII(cx, "Module environment unavailable");
    return false;
  }

  Rooted<ModuleEnvironmentObject*> env(cx,
                                       GetModuleInitialEnvironment(cx, module));
  RootedString name(cx, args[1].toString());
  RootedId id(cx);
  if (!JS_StringToId(cx, name, &id)) {
    return false;
  }

  if (!GetProperty(cx, env, env, id, args.rval())) {
    return false;
  }

  if (args.rval().isMagic(JS_UNINITIALIZED_LEXICAL)) {
    ReportRuntimeLexicalError(cx, JSMSG_UNINITIALIZED_LEXICAL, id);
    return false;
  }

  return true;
}

enum class DumpType {
  ParseNode,
  Stencil,
};

template <typename Unit>
static bool DumpAST(JSContext* cx, const JS::ReadOnlyCompileOptions& options,
                    const Unit* units, size_t length,
                    js::frontend::CompilationState& compilationState,
                    js::frontend::ParseGoal goal) {
  using namespace js::frontend;

  AutoReportFrontendContext fc(cx);
  Parser<FullParseHandler, Unit> parser(&fc, options, units, length,
                                        compilationState,
                                        /* syntaxParser = */ nullptr);
  if (!parser.checkOptions()) {
    return false;
  }

  // Emplace the top-level stencil.
  MOZ_ASSERT(compilationState.scriptData.length() ==
             CompilationStencil::TopLevelIndex);
  if (!compilationState.appendScriptStencilAndData(&fc)) {
    return false;
  }

  js::frontend::ParseNode* pn;
  if (goal == frontend::ParseGoal::Script) {
    pn = parser.parse().unwrapOr(nullptr);
  } else {
    ModuleBuilder builder(&fc, &parser);

    SourceExtent extent = SourceExtent::makeGlobalExtent(length);
    ModuleSharedContext modulesc(&fc, options, builder, extent);
    pn = parser.moduleBody(&modulesc).unwrapOr(nullptr);
  }

  if (!pn) {
    return false;
  }

#if defined(DEBUG)
  js::Fprinter out(stderr);
  DumpParseTree(&parser, pn, out);
#endif

  return true;
}

template <typename Unit>
[[nodiscard]] static bool DumpStencil(JSContext* cx,
                                      const JS::ReadOnlyCompileOptions& options,
                                      const Unit* units, size_t length,
                                      js::frontend::ParseGoal goal) {
  JS::SourceText<Unit> srcBuf;
  if (!srcBuf.init(cx, units, length, JS::SourceOwnership::Borrowed)) {
    return false;
  }

  RefPtr<JS::Stencil> stencil;
  if (goal == frontend::ParseGoal::Script) {
    stencil = JS::CompileGlobalScriptToStencil(cx, options, srcBuf);
  } else {
    stencil = JS::CompileModuleScriptToStencil(cx, options, srcBuf);
  }

  if (!stencil) {
    return false;
  }

#if defined(DEBUG) || defined(JS_JITSPEW)
  stencil->dump();
#endif

  return true;
}

static bool FrontendTest(JSContext* cx, unsigned argc, Value* vp,
                         const char* funcName, DumpType dumpType) {
  using namespace js::frontend;

  CallArgs args = CallArgsFromVp(argc, vp);

  if (!args.requireAtLeast(cx, funcName, 1)) {
    return false;
  }
  if (!args[0].isString()) {
    const char* typeName = InformalValueTypeName(args[0]);
    JS_ReportErrorASCII(cx, "expected string to parse, got %s", typeName);
    return false;
  }

  frontend::ParseGoal goal = frontend::ParseGoal::Script;

  CompileOptions options(cx);
  options.setIntroductionType("js shell parse")
      .setFileAndLine("<string>", 1)
      .setIsRunOnce(true)
      .setNoScriptRval(true);

  if (args.length() >= 2) {
    if (!args[1].isObject()) {
      JS_ReportErrorASCII(cx, "The 2nd argument must be an object");
      return false;
    }

    RootedObject objOptions(cx, &args[1].toObject());

    RootedValue optionModule(cx);
    if (!JS_GetProperty(cx, objOptions, "module", &optionModule)) {
      return false;
    }

    if (optionModule.isBoolean()) {
      if (optionModule.toBoolean()) {
        goal = frontend::ParseGoal::Module;
      }
    } else if (!optionModule.isUndefined()) {
      const char* typeName = InformalValueTypeName(optionModule);
      JS_ReportErrorASCII(cx, "option `module` should be a boolean, got %s",
                          typeName);
      return false;
    }
    if (!js::ParseCompileOptions(cx, options, objOptions, nullptr)) {
      return false;
    }

    if (goal == frontend::ParseGoal::Module && options.lineno == 0) {
      JS_ReportErrorASCII(cx, "Module cannot be compiled with lineNumber == 0");
      return false;
    }
  }

  JSString* scriptContents = args[0].toString();
  Rooted<JSLinearString*> linearString(cx, scriptContents->ensureLinear(cx));
  if (!linearString) {
    return false;
  }

  bool isAscii = false;
  if (linearString->hasLatin1Chars()) {
    JS::AutoCheckCannotGC nogc;
    isAscii = JS::StringIsASCII(mozilla::Span(
        reinterpret_cast<const char*>(linearString->latin1Chars(nogc)),
        linearString->length()));
  }

  AutoStableStringChars stableChars(cx);
  if (isAscii) {
    if (!stableChars.init(cx, scriptContents)) {
      return false;
    }
    MOZ_ASSERT(stableChars.isLatin1());
  } else {
    if (!stableChars.initTwoByte(cx, scriptContents)) {
      return false;
    }
  }

  size_t length = scriptContents->length();

  if (goal == frontend::ParseGoal::Module) {
    // See frontend::CompileModule.
    options.setForceStrictMode();
    options.allowHTMLComments = false;
  }

  if (dumpType == DumpType::Stencil) {
    if (isAscii) {
      const Latin1Char* latin1 = stableChars.latin1Range().begin().get();
      auto utf8 = reinterpret_cast<const mozilla::Utf8Unit*>(latin1);
      if (!DumpStencil<mozilla::Utf8Unit>(cx, options, utf8, length, goal)) {
        return false;
      }
    } else {
      MOZ_ASSERT(stableChars.isTwoByte());
      const char16_t* chars = stableChars.twoByteRange().begin().get();
      if (!DumpStencil<char16_t>(cx, options, chars, length, goal)) {
        return false;
      }
    }

    args.rval().setUndefined();
    return true;
  }

  AutoReportFrontendContext fc(cx);
  Rooted<frontend::CompilationInput> input(cx,
                                           frontend::CompilationInput(options));
  if (goal == frontend::ParseGoal::Script) {
    if (!input.get().initForGlobal(&fc)) {
      return false;
    }
  } else {
    if (!input.get().initForModule(&fc)) {
      return false;
    }
  }

  LifoAllocScope allocScope(&cx->tempLifoAlloc());
  frontend::NoScopeBindingCache scopeCache;
  frontend::CompilationState compilationState(&fc, allocScope, input.get());
  if (!compilationState.init(&fc, &scopeCache)) {
    return false;
  }

  if (isAscii) {
    const Latin1Char* latin1 = stableChars.latin1Range().begin().get();
    auto utf8 = reinterpret_cast<const mozilla::Utf8Unit*>(latin1);
    if (!DumpAST<mozilla::Utf8Unit>(cx, options, utf8, length, compilationState,
                                    goal)) {
      return false;
    }
  } else {
    MOZ_ASSERT(stableChars.isTwoByte());
    const char16_t* chars = stableChars.twoByteRange().begin().get();
    if (!DumpAST<char16_t>(cx, options, chars, length, compilationState,
                           goal)) {
      return false;
    }
  }
  args.rval().setUndefined();
  return true;
}

static bool DumpStencil(JSContext* cx, unsigned argc, Value* vp) {
  return FrontendTest(cx, argc, vp, "dumpStencil", DumpType::Stencil);
}

static bool Parse(JSContext* cx, unsigned argc, Value* vp) {
  // Parse returns local scope information with variables ordered
  // differently, depending on the underlying JIT implementation.
  if (js::SupportDifferentialTesting()) {
    JS_ReportErrorASCII(cx,
                        "Function not available in differential testing mode.");
    return false;
  }

  return FrontendTest(cx, argc, vp, "parse", DumpType::ParseNode);
}

static bool SyntaxParse(JSContext* cx, unsigned argc, Value* vp) {
  using namespace js::frontend;

  CallArgs args = CallArgsFromVp(argc, vp);

  if (!args.requireAtLeast(cx, "syntaxParse", 1)) {
    return false;
  }
  if (!args[0].isString()) {
    const char* typeName = InformalValueTypeName(args[0]);
    JS_ReportErrorASCII(cx, "expected string to parse, got %s", typeName);
    return false;
  }

  JSString* scriptContents = args[0].toString();

  CompileOptions options(cx);
  options.setIntroductionType("js shell syntaxParse")
      .setFileAndLine("<string>", 1);

  AutoStableStringChars stableChars(cx);
  if (!stableChars.initTwoByte(cx, scriptContents)) {
    return false;
  }

  const char16_t* chars = stableChars.twoByteRange().begin().get();
  size_t length = scriptContents->length();

  AutoReportFrontendContext fc(cx);
  Rooted<frontend::CompilationInput> input(cx,
                                           frontend::CompilationInput(options));
  if (!input.get().initForGlobal(&fc)) {
    return false;
  }

  LifoAllocScope allocScope(&cx->tempLifoAlloc());
  frontend::NoScopeBindingCache scopeCache;
  frontend::CompilationState compilationState(&fc, allocScope, input.get());
  if (!compilationState.init(&fc, &scopeCache)) {
    return false;
  }

  Parser<frontend::SyntaxParseHandler, char16_t> parser(
      &fc, options, chars, length, compilationState,
      /* syntaxParser = */ nullptr);
  if (!parser.checkOptions()) {
    return false;
  }

  bool succeeded = parser.parse().isOk();
  if (fc.hadErrors()) {
    return false;
  }

  if (!succeeded && !parser.hadAbortedSyntaxParse()) {
    // If no exception is posted, either there was an OOM or a language
    // feature unhandled by the syntax parser was encountered.
    MOZ_ASSERT(fc.hadOutOfMemory());
    return false;
  }

  args.rval().setBoolean(succeeded);
  return true;
}

static bool OffThreadCompileToStencil(JSContext* cx, unsigned argc, Value* vp) {
  if (!CanUseExtraThreads()) {
    JS_ReportErrorASCII(
        cx, "Can't use offThreadCompileToStencil with --no-threads");
    return false;
  }

  CallArgs args = CallArgsFromVp(argc, vp);

  if (!args.requireAtLeast(cx, "offThreadCompileToStencil", 1)) {
    return false;
  }
  if (!args[0].isString()) {
    const char* typeName = InformalValueTypeName(args[0]);
    JS_ReportErrorASCII(cx, "expected string to parse, got %s", typeName);
    return false;
  }

  UniqueChars fileNameBytes;
  CompileOptions options(cx);
  options.setIntroductionType("js shell offThreadCompileToStencil")
      .setFileAndLine("<string>", 1);

  if (args.length() >= 2) {
    if (!args[1].isObject()) {
      JS_ReportErrorASCII(
          cx, "offThreadCompileToStencil: The 2nd argument must be an object");
      return false;
    }

    // Offthread compilation requires that the debug metadata be set when the
    // script is collected from offthread, rather than when compiled.
    RootedObject opts(cx, &args[1].toObject());
    if (!js::ParseCompileOptions(cx, options, opts, &fileNameBytes)) {
      return false;
    }
  }

  // This option setting must override whatever the caller requested.
  options.setIsRunOnce(true);

  JSString* scriptContents = args[0].toString();
  AutoStableStringChars stableChars(cx);
  if (!stableChars.initTwoByte(cx, scriptContents)) {
    return false;
  }

  size_t length = scriptContents->length();
  const char16_t* chars = stableChars.twoByteChars();

  // Make sure we own the string's chars, so that they are not freed before
  // the compilation is finished.
  UniqueTwoByteChars ownedChars;
  if (stableChars.maybeGiveOwnershipToCaller()) {
    ownedChars.reset(const_cast<char16_t*>(chars));
  } else {
    ownedChars.reset(cx->pod_malloc<char16_t>(length));
    if (!ownedChars) {
      return false;
    }

    mozilla::PodCopy(ownedChars.get(), chars, length);
  }

  if (!cx->runtime()->canUseParallelParsing() || !js::CanUseExtraThreads()) {
    JS_ReportErrorASCII(cx, "cannot compile code on helper thread");
    return false;
  }

  JS::SourceText<char16_t> srcBuf;
  if (!srcBuf.init(cx, std::move(ownedChars), length)) {
    return false;
  }

  OffThreadJob* job = NewOffThreadJob(cx, OffThreadJob::Kind::CompileScript,
                                      options, std::move(srcBuf));
  if (!job) {
    return false;
  }

  if (!job->dispatch()) {
    ReportOutOfMemory(cx);
    DeleteOffThreadJob(cx, job);
    return false;
  }

  args.rval().setInt32(job->id);
  return true;
}

static bool FinishOffThreadStencil(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  OffThreadJob* job = LookupOffThreadJobForArgs(cx, args, 0);
  if (!job) {
    return false;
  }

  job->waitUntilDone();

  RefPtr<JS::Stencil> stencil = job->stealStencil(cx);
  DeleteOffThreadJob(cx, job);
  if (!stencil) {
    return false;
  }
  RootedObject stencilObj(cx,
                          js::StencilObject::create(cx, std::move(stencil)));
  if (!stencilObj) {
    return false;
  }

  args.rval().setObject(*stencilObj);
  return true;
}

static bool OffThreadCompileModuleToStencil(JSContext* cx, unsigned argc,
                                            Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!args.requireAtLeast(cx, "offThreadCompileModuleToStencil", 1)) {
    return false;
  }
  if (!args[0].isString()) {
    const char* typeName = InformalValueTypeName(args[0]);
    JS_ReportErrorASCII(cx, "expected string to parse, got %s", typeName);
    return false;
  }

  UniqueChars fileNameBytes;
  CompileOptions options(cx);
  options.setIntroductionType("js shell offThreadCompileModuleToStencil")
      .setFileAndLine("<string>", 1);

  if (args.length() >= 2) {
    if (!args[1].isObject()) {
      JS_ReportErrorASCII(cx,
                          "offThreadCompileModuleToStencil: The 2nd argument "
                          "must be an object");
      return false;
    }

    // Offthread compilation requires that the debug metadata be set when the
    // script is collected from offthread, rather than when compiled.
    RootedObject opts(cx, &args[1].toObject());
    if (!js::ParseCompileOptions(cx, options, opts, &fileNameBytes)) {
      return false;
    }

    if (!ValidateModuleCompileOptions(cx, options)) {
      return false;
    }
  }

  options.setIsRunOnce(true).setSourceIsLazy(false);

  JSString* scriptContents = args[0].toString();
  AutoStableStringChars stableChars(cx);
  if (!stableChars.initTwoByte(cx, scriptContents)) {
    return false;
  }

  size_t length = scriptContents->length();
  const char16_t* chars = stableChars.twoByteChars();

  // Make sure we own the string's chars, so that they are not freed before
  // the compilation is finished.
  UniqueTwoByteChars ownedChars;
  if (stableChars.maybeGiveOwnershipToCaller()) {
    ownedChars.reset(const_cast<char16_t*>(chars));
  } else {
    ownedChars.reset(cx->pod_malloc<char16_t>(length));
    if (!ownedChars) {
      return false;
    }

    mozilla::PodCopy(ownedChars.get(), chars, length);
  }

  if (!cx->runtime()->canUseParallelParsing() || !js::CanUseExtraThreads()) {
    JS_ReportErrorASCII(cx, "cannot compile code on worker thread");
    return false;
  }

  JS::SourceText<char16_t> srcBuf;
  if (!srcBuf.init(cx, std::move(ownedChars), length)) {
    return false;
  }

  OffThreadJob* job = NewOffThreadJob(cx, OffThreadJob::Kind::CompileModule,
                                      options, std::move(srcBuf));
  if (!job) {
    return false;
  }

  if (!job->dispatch()) {
    ReportOutOfMemory(cx);
    DeleteOffThreadJob(cx, job);
    return false;
  }

  args.rval().setInt32(job->id);
  return true;
}

static bool OffThreadDecodeStencil(JSContext* cx, unsigned argc, Value* vp) {
  if (!CanUseExtraThreads()) {
    JS_ReportErrorASCII(cx,
                        "Can't use offThreadDecodeStencil with --no-threads");
    return false;
  }

  CallArgs args = CallArgsFromVp(argc, vp);

  if (!args.requireAtLeast(cx, "offThreadDecodeStencil", 1)) {
    return false;
  }
  if (!args[0].isObject() || !CacheEntry_isCacheEntry(&args[0].toObject())) {
    const char* typeName = InformalValueTypeName(args[0]);
    JS_ReportErrorASCII(cx, "expected cache entry, got %s", typeName);
    return false;
  }
  RootedObject cacheEntry(cx, &args[0].toObject());

  UniqueChars fileNameBytes;
  CompileOptions options(cx);
  options.setIntroductionType("js shell offThreadDecodeStencil")
      .setFileAndLine("<string>", 1);

  if (args.length() >= 2) {
    if (!args[1].isObject()) {
      JS_ReportErrorASCII(
          cx, "offThreadDecodeStencil: The 2nd argument must be an object");
      return false;
    }

    RootedObject opts(cx, &args[1].toObject());
    if (!js::ParseCompileOptions(cx, options, opts, &fileNameBytes)) {
      return false;
    }
  }

  // This option setting must override whatever the caller requested, and
  // this should match `Evaluate` that encodes the script.
  options.setIsRunOnce(false);

  JS::TranscodeBuffer loadBuffer;
  size_t loadLength = 0;
  uint8_t* loadData = nullptr;
  loadData = CacheEntry_getBytecode(cx, cacheEntry, &loadLength);
  if (!loadData) {
    return false;
  }
  if (!loadBuffer.append(loadData, loadLength)) {
    JS_ReportOutOfMemory(cx);
    return false;
  }

  if (!cx->runtime()->canUseParallelParsing() || !js::CanUseExtraThreads()) {
    JS_ReportErrorASCII(cx, "cannot compile code on worker thread");
    return false;
  }

  OffThreadJob* job = NewOffThreadJob(cx, OffThreadJob::Kind::Decode, options,
                                      std::move(loadBuffer));
  if (!job) {
    return false;
  }

  if (!job->dispatch()) {
    ReportOutOfMemory(cx);
    DeleteOffThreadJob(cx, job);
    return false;
  }

  args.rval().setInt32(job->id);
  return true;
}

class AutoCStringVector {
  Vector<char*> argv_;

 public:
  explicit AutoCStringVector(JSContext* cx) : argv_(cx) {}
  ~AutoCStringVector() {
    for (size_t i = 0; i < argv_.length(); i++) {
      js_free(argv_[i]);
    }
  }
  bool append(UniqueChars&& arg) {
    if (!argv_.append(arg.get())) {
      return false;
    }

    // Now owned by this vector.
    (void)arg.release();
    return true;
  }
  char* const* get() const { return argv_.begin(); }
  size_t length() const { return argv_.length(); }
  char* operator[](size_t i) const { return argv_[i]; }
  void replace(size_t i, UniqueChars arg) {
    js_free(argv_[i]);
    argv_[i] = arg.release();
  }
};

#if defined(XP_WIN)
static bool EscapeForShell(JSContext* cx, AutoCStringVector& argv) {
  // Windows will break arguments in argv by various spaces, so we wrap each
  // argument in quotes and escape quotes within. Even with quotes, \ will be
  // treated like an escape character, so inflate each \ to \\.

  for (size_t i = 0; i < argv.length(); i++) {
    if (!argv[i]) {
      continue;
    }

    size_t newLen = 3;  // quotes before and after and null-terminator
    for (char* p = argv[i]; *p; p++) {
      newLen++;
      if (*p == '\"' || *p == '\\') {
        newLen++;
      }
    }

    auto escaped = cx->make_pod_array<char>(newLen);
    if (!escaped) {
      return false;
    }

    char* src = argv[i];
    char* dst = escaped.get();
    *dst++ = '\"';
    while (*src) {
      if (*src == '\"' || *src == '\\') {
        *dst++ = '\\';
      }
      *dst++ = *src++;
    }
    *dst++ = '\"';
    *dst++ = '\0';
    MOZ_ASSERT(escaped.get() + newLen == dst);

    argv.replace(i, std::move(escaped));
  }
  return true;
}
#endif

#ifndef __wasi__
static bool ReadAll(int fd, wasm::Bytes* bytes) {
  size_t lastLength = bytes->length();
  while (true) {
    static const int ChunkSize = 64 * 1024;
    if (!bytes->growBy(ChunkSize)) {
      return false;
    }

    intptr_t readCount;
    while (true) {
      readCount = read(fd, bytes->begin() + lastLength, ChunkSize);
      if (readCount >= 0) {
        break;
      }
      if (errno != EINTR) {
        return false;
      }
    }

    if (readCount < ChunkSize) {
      bytes->shrinkTo(lastLength + readCount);
      if (readCount == 0) {
        return true;
      }
    }

    lastLength = bytes->length();
  }
}

static bool WriteAll(int fd, const uint8_t* bytes, size_t length) {
  while (length > 0) {
    int written = write(fd, bytes, length);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    MOZ_ASSERT(unsigned(written) <= length);
    length -= written;
    bytes += written;
  }

  return true;
}

class AutoPipe {
  int fds_[2];

 public:
  AutoPipe() {
    fds_[0] = -1;
    fds_[1] = -1;
  }

  ~AutoPipe() {
    if (fds_[0] != -1) {
      close(fds_[0]);
    }
    if (fds_[1] != -1) {
      close(fds_[1]);
    }
  }

  bool init() {
#  ifdef XP_WIN
    return !_pipe(fds_, 4096, O_BINARY);
#  else
    return !pipe(fds_);
#  endif
  }

  int reader() const {
    MOZ_ASSERT(fds_[0] != -1);
    return fds_[0];
  }

  int writer() const {
    MOZ_ASSERT(fds_[1] != -1);
    return fds_[1];
  }

  void closeReader() {
    MOZ_ASSERT(fds_[0] != -1);
    close(fds_[0]);
    fds_[0] = -1;
  }

  void closeWriter() {
    MOZ_ASSERT(fds_[1] != -1);
    close(fds_[1]);
    fds_[1] = -1;
  }
};
#endif  // __wasi__

int shell::sArgc;
char** shell::sArgv;

#ifndef __wasi__
static const char sWasmCompileAndSerializeFlag[] =
    "--wasm-compile-and-serialize";
MOZ_RUNINIT static Vector<const char*, 5, js::SystemAllocPolicy>
    sCompilerProcessFlags;

static bool CompileAndSerializeInSeparateProcess(JSContext* cx,
                                                 const uint8_t* bytecode,
                                                 size_t bytecodeLength,
                                                 wasm::Bytes* serialized) {
  AutoPipe stdIn, stdOut;
  if (!stdIn.init() || !stdOut.init()) {
    return false;
  }

  AutoCStringVector argv(cx);

  UniqueChars argv0 = DuplicateString(cx, sArgv[0]);
  if (!argv0 || !argv.append(std::move(argv0))) {
    return false;
  }

  // Put compiler flags first since they must precede the non-option
  // file-descriptor args (passed on Windows, below).
  for (unsigned i = 0; i < sCompilerProcessFlags.length(); i++) {
    UniqueChars flags = DuplicateString(cx, sCompilerProcessFlags[i]);
    if (!flags || !argv.append(std::move(flags))) {
      return false;
    }
  }

  UniqueChars arg;

  arg = DuplicateString(sWasmCompileAndSerializeFlag);
  if (!arg || !argv.append(std::move(arg))) {
    return false;
  }

#  ifdef XP_WIN
  // The spawned process will have all the stdIn/stdOut file handles open, but
  // without the power of fork, we need some other way to communicate the
  // integer fd values so we encode them in argv and WasmCompileAndSerialize()
  // has a matching #ifdef XP_WIN to parse them out. Communicate both ends of
  // both pipes so the child process can closed the unused ends.

  arg = JS_smprintf("%d", stdIn.reader());
  if (!arg || !argv.append(std::move(arg))) {
    return false;
  }

  arg = JS_smprintf("%d", stdIn.writer());
  if (!arg || !argv.append(std::move(arg))) {
    return false;
  }

  arg = JS_smprintf("%d", stdOut.reader());
  if (!arg || !argv.append(std::move(arg))) {
    return false;
  }

  arg = JS_smprintf("%d", stdOut.writer());
  if (!arg || !argv.append(std::move(arg))) {
    return false;
  }
#  endif

  // Required by both _spawnv and exec.
  if (!argv.append(nullptr)) {
    return false;
  }

#  ifdef XP_WIN
  if (!EscapeForShell(cx, argv)) {
    return false;
  }

  int childPid = _spawnv(P_NOWAIT, sArgv[0], argv.get());
  if (childPid == -1) {
    return false;
  }
#  else
  pid_t childPid = fork();
  switch (childPid) {
    case -1:
      return false;
    case 0:
      // In the child process. Redirect stdin/stdout to the respective ends of
      // the pipes. Closing stdIn.writer() is necessary for stdin to hit EOF.
      // This case statement must not return before exec() takes over. Rather,
      // exit(-1) is used to return failure to the parent process.
      if (dup2(stdIn.reader(), STDIN_FILENO) == -1) {
        exit(-1);
      }
      if (dup2(stdOut.writer(), STDOUT_FILENO) == -1) {
        exit(-1);
      }
      close(stdIn.reader());
      close(stdIn.writer());
      close(stdOut.reader());
      close(stdOut.writer());
      execv(sArgv[0], argv.get());
      exit(-1);
  }
#  endif

  // In the parent process. Closing stdOut.writer() is necessary for
  // stdOut.reader() below to hit EOF.
  stdIn.closeReader();
  stdOut.closeWriter();

  if (!WriteAll(stdIn.writer(), bytecode, bytecodeLength)) {
    return false;
  }

  stdIn.closeWriter();

  if (!ReadAll(stdOut.reader(), serialized)) {
    return false;
  }

  stdOut.closeReader();

  int status;
#  ifdef XP_WIN
  if (_cwait(&status, childPid, WAIT_CHILD) == -1) {
    return false;
  }
#  else
  while (true) {
    if (waitpid(childPid, &status, 0) >= 0) {
      break;
    }
    if (errno != EINTR) {
      return false;
    }
  }
#  endif

  return status == 0;
}

static bool WasmCompileAndSerialize(JSContext* cx) {
  MOZ_ASSERT(wasm::CodeCachingAvailable(cx));

#  ifdef XP_WIN
  // See CompileAndSerializeInSeparateProcess for why we've had to smuggle
  // these fd values through argv. Closing the writing ends is necessary for
  // the reading ends to hit EOF.
  int flagIndex = 0;
  for (; flagIndex < sArgc; flagIndex++) {
    if (!strcmp(sArgv[flagIndex], sWasmCompileAndSerializeFlag)) {
      break;
    }
  }
  MOZ_RELEASE_ASSERT(flagIndex < sArgc);

  int fdsIndex = flagIndex + 1;
  MOZ_RELEASE_ASSERT(fdsIndex + 4 == sArgc);

  int stdInReader = atoi(sArgv[fdsIndex + 0]);
  int stdInWriter = atoi(sArgv[fdsIndex + 1]);
  int stdOutReader = atoi(sArgv[fdsIndex + 2]);
  int stdOutWriter = atoi(sArgv[fdsIndex + 3]);

  int stdIn = stdInReader;
  close(stdInWriter);
  close(stdOutReader);
  int stdOut = stdOutWriter;
#  else
  int stdIn = STDIN_FILENO;
  int stdOut = STDOUT_FILENO;
#  endif

  wasm::MutableBytes bytecode = js_new<wasm::ShareableBytes>();
  if (!ReadAll(stdIn, &bytecode->vector)) {
    return false;
  }

  wasm::BytecodeSource bytecodeSource(bytecode->begin(), bytecode->length());
  wasm::Bytes serialized;
  if (!wasm::CompileAndSerialize(cx, bytecodeSource, &serialized)) {
    return false;
  }

  if (!WriteAll(stdOut, serialized.begin(), serialized.length())) {
    return false;
  }

  return true;
}

static bool WasmCompileInSeparateProcess(JSContext* cx, unsigned argc,
                                         Value* vp) {
  if (!wasm::CodeCachingAvailable(cx)) {
    JS_ReportErrorASCII(cx, "WebAssembly caching not supported");
    return false;
  }

  CallArgs args = CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "wasmCompileInSeparateProcess", 1)) {
    return false;
  }

  SharedMem<uint8_t*> bytecode;
  size_t numBytes;
  if (!args[0].isObject() ||
      !IsBufferSource(cx, &args[0].toObject(), /*allowShared*/ false,
                      /*allowResizable*/ false, &bytecode, &numBytes)) {
    RootedObject callee(cx, &args.callee());
    ReportUsageErrorASCII(cx, callee, "Argument must be a buffer source");
    return false;
  }

  wasm::Bytes serialized;
  if (!CompileAndSerializeInSeparateProcess(cx, bytecode.unwrap(), numBytes,
                                            &serialized)) {
    if (!cx->isExceptionPending()) {
      JS_ReportErrorASCII(cx, "creating and executing child process");
    }
    return false;
  }

  RootedObject module(cx);
  if (!wasm::DeserializeModule(cx, serialized, &module)) {
    return false;
  }

  args.rval().setObject(*module);
  return true;
}
#endif  // __wasi__

static bool DecompileFunction(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (args.length() < 1 || !args[0].isObject() ||
      !args[0].toObject().is<JSFunction>()) {
    args.rval().setUndefined();
    return true;
  }
  RootedFunction fun(cx, &args[0].toObject().as<JSFunction>());
  JSString* result = JS_DecompileFunction(cx, fun);
  if (!result) {
    return false;
  }
  args.rval().setString(result);
  return true;
}

static bool DecompileThisScript(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  NonBuiltinScriptFrameIter iter(cx);
  if (iter.done()) {
    args.rval().setString(cx->runtime()->emptyString);
    return true;
  }

  {
    JSAutoRealm ar(cx, iter.script());

    RootedScript script(cx, iter.script());
    JSString* result = JS_DecompileScript(cx, script);
    if (!result) {
      return false;
    }

    args.rval().setString(result);
  }

  return JS_WrapValue(cx, args.rval());
}

static bool ValueToSource(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  JSString* str = ValueToSource(cx, args.get(0));
  if (!str) {
    return false;
  }

  args.rval().setString(str);
  return true;
}

static bool ThisFilename(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  JS::AutoFilename filename;
  if (!DescribeScriptedCaller(&filename, cx) || !filename.get()) {
    args.rval().setString(cx->runtime()->emptyString);
    return true;
  }

  JSString* str = NewStringCopyUTF8(cx, filename.get());
  if (!str) {
    return false;
  }

  args.rval().setString(str);
  return true;
}

static bool WrapWithProto(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  Value obj = args.get(0);
  Value proto = args.get(1);
  if (!obj.isObject() || !proto.isObjectOrNull()) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              JSSMSG_INVALID_ARGS, "wrapWithProto");
    return false;
  }

  // Disallow constructing (deeply) nested wrapper chains, to avoid running
  // out of stack space in isCallable/isConstructor. See bug 1126105.
  if (IsWrapper(&obj.toObject())) {
    JS_ReportErrorASCII(cx, "wrapWithProto cannot wrap a wrapper");
    return false;
  }

  WrapperOptions options(cx);
  options.setProto(proto.toObjectOrNull());
  JSObject* wrapped = Wrapper::New(cx, &obj.toObject(),
                                   &Wrapper::singletonWithPrototype, options);
  if (!wrapped) {
    return false;
  }

  args.rval().setObject(*wrapped);
  return true;
}

static bool NewGlobal(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject callee(cx, &args.callee());

  JS::RealmOptions options;
  JS::RealmCreationOptions& creationOptions = options.creationOptions();
  JS::RealmBehaviors& behaviors = options.behaviors();
  ShellGlobalKind kind = ShellGlobalKind::WindowProxy;
  bool immutablePrototype = true;

  SetStandardRealmOptions(options);

  // Default to creating the global in the current compartment unless
  // --more-compartments is used.
  if (defaultToSameCompartment) {
    creationOptions.setExistingCompartment(cx->global());
  } else {
    creationOptions.setNewCompartmentAndZone();
  }

  // Ensure the target compartment/zone is kept alive when sameCompartmentAs or
  // sameZoneAs is used.
  Rooted<JSObject*> compartmentRoot(cx);

  JS::AutoHoldPrincipals principals(cx);

  if (args.length() == 1 && args[0].isObject()) {
    RootedObject opts(cx, &args[0].toObject());
    RootedValue v(cx);

    if (!JS_GetProperty(cx, opts, "invisibleToDebugger", &v)) {
      return false;
    }
    if (v.isBoolean()) {
      creationOptions.setInvisibleToDebugger(v.toBoolean());
    }

    if (!JS_GetProperty(cx, opts, "sameZoneAs", &v)) {
      return false;
    }
    if (v.isObject()) {
      compartmentRoot = UncheckedUnwrap(&v.toObject());
      creationOptions.setNewCompartmentInExistingZone(compartmentRoot);
    }

    if (!JS_GetProperty(cx, opts, "sameCompartmentAs", &v)) {
      return false;
    }
    if (v.isObject()) {
      compartmentRoot = UncheckedUnwrap(&v.toObject());
      creationOptions.setExistingCompartment(compartmentRoot);
    }

    if (!JS_GetProperty(cx, opts, "newCompartment", &v)) {
      return false;
    }
    if (v.isBoolean()) {
      if (v.toBoolean()) {
        creationOptions.setNewCompartmentAndZone();
      } else {
        creationOptions.setExistingCompartment(cx->global());
      }
    }

    if (!JS_GetProperty(cx, opts, "discardSource", &v)) {
      return false;
    }
    if (v.isBoolean()) {
      behaviors.setDiscardSource(v.toBoolean());
    }

    if (!JS_GetProperty(cx, opts, "useWindowProxy", &v)) {
      return false;
    }
    if (v.isBoolean()) {
      kind = v.toBoolean() ? ShellGlobalKind::WindowProxy
                           : ShellGlobalKind::GlobalObject;
    }

    if (!JS_GetProperty(cx, opts, "immutablePrototype", &v)) {
      return false;
    }
    if (v.isBoolean()) {
      immutablePrototype = v.toBoolean();
    }

    if (!JS_GetProperty(cx, opts, "systemPrincipal", &v)) {
      return false;
    }
    if (v.isBoolean()) {
      principals.reset(&ShellPrincipals::fullyTrusted);
    }

    if (!JS_GetProperty(cx, opts, "principal", &v)) {
      return false;
    }
    if (!v.isUndefined()) {
      uint32_t bits;
      if (!ToUint32(cx, v, &bits)) {
        return false;
      }
      JSPrincipals* newPrincipals = cx->new_<ShellPrincipals>(bits);
      if (!newPrincipals) {
        return false;
      }
      principals.reset(newPrincipals);
    }

    if (!JS_GetProperty(cx, opts, "enableCoopAndCoep", &v)) {
      return false;
    }
    if (v.isBoolean()) {
      creationOptions.setCoopAndCoepEnabled(v.toBoolean());
    }

    if (!JS_GetProperty(cx, opts, "freezeBuiltins", &v)) {
      return false;
    }
    if (v.isBoolean()) {
      creationOptions.setFreezeBuiltins(v.toBoolean());
    }

    // On the web, the SharedArrayBuffer constructor is not installed as a
    // global property in pages that aren't isolated in a separate process (and
    // thus can't allow the structured cloning of shared memory).  Specify false
    // for this option to reproduce this behavior.
    if (!JS_GetProperty(cx, opts, "defineSharedArrayBufferConstructor", &v)) {
      return false;
    }
    if (v.isBoolean()) {
      creationOptions.setDefineSharedArrayBufferConstructor(v.toBoolean());
    }

    if (!JS_GetProperty(cx, opts, "forceUTC", &v)) {
      return false;
    }
    if (v.isBoolean()) {
      creationOptions.setForceUTC(v.toBoolean());
    }

    if (!JS_GetProperty(cx, opts, "alwaysUseFdlibm", &v)) {
      return false;
    }
    if (v.isBoolean()) {
      creationOptions.setAlwaysUseFdlibm(v.toBoolean());
    }

    if (!JS_GetProperty(cx, opts, "locale", &v)) {
      return false;
    }
    if (v.isString()) {
      RootedString str(cx, v.toString());
      UniqueChars locale = StringToLocale(cx, callee, str);
      if (!locale) {
        return false;
      }
      creationOptions.setLocaleCopyZ(locale.get());
    }
  }

  if (!CheckRealmOptions(cx, options, principals.get())) {
    return false;
  }

  RootedObject global(cx, NewGlobalObject(cx, options, principals.get(), kind,
                                          immutablePrototype));
  if (!global) {
    return false;
  }

  RootedObject wrapped(cx, ToWindowProxyIfWindow(global));
  if (!JS_WrapObject(cx, &wrapped)) {
    return false;
  }

  args.rval().setObject(*wrapped);
  return true;
}

static bool NukeAllCCWs(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() != 0) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              JSSMSG_INVALID_ARGS, "nukeAllCCWs");
    return false;
  }

  NukeCrossCompartmentWrappers(cx, AllCompartments(), cx->realm(),
                               NukeWindowReferences, NukeAllReferences);
  args.rval().setUndefined();
  return true;
}

static bool RecomputeWrappers(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() > 2) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              JSSMSG_INVALID_ARGS, "recomputeWrappers");
    return false;
  }

  JS::Compartment* sourceComp = nullptr;
  if (args.get(0).isObject()) {
    sourceComp = JS::GetCompartment(UncheckedUnwrap(&args[0].toObject()));
  }

  JS::Compartment* targetComp = nullptr;
  if (args.get(1).isObject()) {
    targetComp = JS::GetCompartment(UncheckedUnwrap(&args[1].toObject()));
  }

  struct SingleOrAllCompartments final : public CompartmentFilter {
    JS::Compartment* comp;
    explicit SingleOrAllCompartments(JS::Compartment* c) : comp(c) {}
    virtual bool match(JS::Compartment* c) const override {
      return !comp || comp == c;
    }
  };

  if (!js::RecomputeWrappers(cx, SingleOrAllCompartments(sourceComp),
                             SingleOrAllCompartments(targetComp))) {
    return false;
  }

  args.rval().setUndefined();
  return true;
}

static bool DumpObjectWrappers(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  bool printedHeader = false;
  for (ZonesIter zone(cx->runtime(), WithAtoms); !zone.done(); zone.next()) {
    bool printedZoneInfo = false;
    for (CompartmentsInZoneIter comp(zone); !comp.done(); comp.next()) {
      bool printedCompartmentInfo = false;
      for (Compartment::ObjectWrapperEnum e(comp); !e.empty(); e.popFront()) {
        JSObject* wrapper = e.front().value().unbarrieredGet();
        JSObject* wrapped = e.front().key();
        if (!printedHeader) {
          fprintf(stderr, "Cross-compartment object wrappers:\n");
          printedHeader = true;
        }
        if (!printedZoneInfo) {
          fprintf(stderr, "  Zone %p:\n", zone.get());
          printedZoneInfo = true;
        }
        if (!printedCompartmentInfo) {
          fprintf(stderr, "    Compartment %p:\n", comp.get());
          printedCompartmentInfo = true;
        }
        fprintf(stderr,
                "      Object wrapper %p -> %p in zone %p compartment %p\n",
                wrapper, wrapped, wrapped->zone(), wrapped->compartment());
      }
    }
  }

  if (!printedHeader) {
    fprintf(stderr, "No cross-compartment object wrappers.\n");
  }

  args.rval().setUndefined();
  return true;
}

static bool GetMaxArgs(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setInt32(ARGS_LENGTH_MAX);
  return true;
}

static bool IsHTMLDDA_Call(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // These are the required conditions under which this object may be called
  // by test262 tests, and the required behavior under those conditions.
  if (args.length() == 0 ||
      (args[0].isString() && args[0].toString()->length() == 0)) {
    args.rval().setNull();
    return true;
  }

  JS_ReportErrorASCII(
      cx, "IsHTMLDDA object is being called in an impermissible manner");
  return false;
}

static bool CreateIsHTMLDDA(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  static const JSClassOps classOps = {
      nullptr,         // addProperty
      nullptr,         // delProperty
      nullptr,         // enumerate
      nullptr,         // newEnumerate
      nullptr,         // resolve
      nullptr,         // mayResolve
      nullptr,         // finalize
      IsHTMLDDA_Call,  // call
      nullptr,         // construct
      nullptr,         // trace
  };

  static const JSClass cls = {
      "IsHTMLDDA",
      JSCLASS_EMULATES_UNDEFINED,
      &classOps,
  };

  JSObject* obj = JS_NewObject(cx, &cls);
  if (!obj) {
    return false;
  }
  args.rval().setObject(*obj);
  return true;
}

static bool GetSelfHostedValue(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() != 1 || !args[0].isString()) {
    JS_ReportErrorNumberASCII(cx, my_GetErrorMessage, nullptr,
                              JSSMSG_INVALID_ARGS, "getSelfHostedValue");
    return false;
  }
  Rooted<JSAtom*> srcAtom(cx, ToAtom<CanGC>(cx, args[0]));
  if (!srcAtom) {
    return false;
  }
  Rooted<PropertyName*> srcName(cx, srcAtom->asPropertyName());
  return GlobalObject::getIntrinsicValue(cx, cx->global(), srcName,
                                         args.rval());
}

class ShellSourceHook : public SourceHook {
  // The function we should call to lazily retrieve source code.
  PersistentRootedFunction fun;

 public:
  ShellSourceHook(JSContext* cx, JSFunction& fun) : fun(cx, &fun) {}

  bool load(JSContext* cx, const char* filename, char16_t** twoByteSource,
            char** utf8Source, size_t* length) override {
    MOZ_ASSERT((twoByteSource != nullptr) != (utf8Source != nullptr),
               "must be called requesting only one of UTF-8 or UTF-16 source");

    RootedString str(cx);
    if (filename) {
      str = NewStringCopyUTF8(cx, filename);
      if (!str) {
        return false;
      }
    } else {
      str = JS_GetEmptyString(cx);
    }
    RootedValue filenameValue(cx, StringValue(str));

    RootedValue result(cx);
    if (!Call(cx, UndefinedHandleValue, fun, HandleValueArray(filenameValue),
              &result)) {
      return false;
    }

    str = JS::ToString(cx, result);
    if (!str) {
      return false;
    }

    Rooted<JSLinearString*> linear(cx, str->ensureLinear(cx));
    if (!linear) {
      return false;
    }

    if (twoByteSource) {
      *length = JS_GetStringLength(linear);

      *twoByteSource = cx->pod_malloc<char16_t>(*length);
      if (!*twoByteSource) {
        return false;
      }

      CopyChars(*twoByteSource, *linear);
    } else {
      MOZ_ASSERT(utf8Source != nullptr);

      *length = JS::GetDeflatedUTF8StringLength(linear);

      *utf8Source = cx->pod_malloc<char>(*length);
      if (!*utf8Source) {
        return false;
      }

      mozilla::DebugOnly<size_t> dstLen = JS::DeflateStringToUTF8Buffer(
          linear, mozilla::Span(*utf8Source, *length));
      MOZ_ASSERT(dstLen == *length);
    }

    return true;
  }
};

static bool WithSourceHook(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject callee(cx, &args.callee());

  if (args.length() != 2) {
    ReportUsageErrorASCII(cx, callee, "Wrong number of arguments.");
    return false;
  }

  if (!args[0].isObject() || !args[0].toObject().is<JSFunction>() ||
      !args[1].isObject() || !args[1].toObject().is<JSFunction>()) {
    ReportUsageErrorASCII(cx, callee,
                          "First and second arguments must be functions.");
    return false;
  }

  mozilla::UniquePtr<ShellSourceHook> hook =
      mozilla::MakeUnique<ShellSourceHook>(cx,
                                           args[0].toObject().as<JSFunction>());
  if (!hook) {
    return false;
  }

  mozilla::UniquePtr<SourceHook> savedHook = js::ForgetSourceHook(cx);
  js::SetSourceHook(cx, std::move(hook));

  RootedObject fun(cx, &args[1].toObject());
  bool result = Call(cx, UndefinedHandleValue, fun,
                     JS::HandleValueArray::empty(), args.rval());
  js::SetSourceHook(cx, std::move(savedHook));
  return result;
}

static void PrintProfilerEvents_Callback(mozilla::MarkerCategory,
                                         const char* msg, const char* details) {
  fprintf(stderr, "PROFILER EVENT: %s %s\n", msg, details);
}

static void PrintProfilerIntervals_Callback(mozilla::MarkerCategory,
                                            const char* msg,
                                            mozilla::TimeStamp start,
                                            const char* details) {
  fprintf(stderr, "PROFILER INTERVAL (%.2fms): %s %s\n",
          (TimeStamp::Now() - start).ToMilliseconds(), msg, details);
}

static bool PrintProfilerEvents(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (cx->runtime()->geckoProfiler().enabled()) {
    js::RegisterContextProfilingEventMarker(cx, &PrintProfilerEvents_Callback,
                                            &PrintProfilerIntervals_Callback);
  }
  args.rval().setUndefined();
  return true;
}

#ifdef SINGLESTEP_PROFILING
static void SingleStepCallback(void* arg, jit::Simulator* sim, void* pc) {
  JSContext* cx = reinterpret_cast<JSContext*>(arg);

  // If profiling is not enabled, don't do anything.
  if (!cx->runtime()->geckoProfiler().enabled()) {
    return;
  }

  JS::ProfilingFrameIterator::RegisterState state;
  state.pc = pc;
#  if defined(JS_SIMULATOR_ARM)
  state.sp = (void*)sim->get_register(jit::Simulator::sp);
  state.lr = (void*)sim->get_register(jit::Simulator::lr);
  state.fp = (void*)sim->get_register(jit::Simulator::fp);
  state.tempFP = (void*)sim->get_register(jit::Simulator::r7);
#  elif defined(JS_SIMULATOR_ARM64)
  state.sp = (void*)sim->get_sp();
  state.lr = (void*)sim->get_lr();
  state.fp = (void*)sim->get_fp();
  state.tempFP = (void*)sim->xreg(11);
#  elif defined(JS_SIMULATOR_MIPS64)
  state.sp = (void*)sim->getRegister(jit::Simulator::sp);
  state.lr = (void*)sim->getRegister(jit::Simulator::ra);
  state.fp = (void*)sim->getRegister(jit::Simulator::fp);
#  elif defined(JS_SIMULATOR_LOONG64)
  state.sp = (void*)sim->getRegister(jit::Simulator::sp);
  state.lr = (void*)sim->getRegister(jit::Simulator::ra);
  state.fp = (void*)sim->getRegister(jit::Simulator::fp);
#  else
#    error "NYI: Single-step profiling support"
#  endif

  mozilla::DebugOnly<void*> lastStackAddress = nullptr;
  StackChars stack;
  uint32_t frameNo = 0;
  AutoEnterOOMUnsafeRegion oomUnsafe;
  for (JS::ProfilingFrameIterator i(cx, state); !i.done(); ++i) {
    MOZ_ASSERT(i.stackAddress() != nullptr);
#  ifndef ENABLE_WASM_JSPI
    // The stack addresses are monotonically increasing, except when
    // suspendable stacks are present (e.g. when JS PI is enabled).
    MOZ_ASSERT(lastStackAddress <= i.stackAddress());
#  endif
    lastStackAddress = i.stackAddress();
    JS::ProfilingFrameIterator::Frame frames[16];
    uint32_t nframes = i.extractStack(frames, 0, 16);
    for (uint32_t i = 0; i < nframes; i++) {
#  ifndef ENABLE_WASM_JSPI
      // Assert endStackAddress never exceeds sp (bug 1782188).
      MOZ_ASSERT(frames[i].endStackAddress >= state.sp);
#  endif
      if (frameNo > 0) {
        if (!stack.append(",", 1)) {
          oomUnsafe.crash("stack.append");
        }
      }
      if (!stack.append(frames[i].label, strlen(frames[i].label))) {
        oomUnsafe.crash("stack.append");
      }
      frameNo++;
    }
  }

  ShellContext* sc = GetShellContext(cx);

  // Only append the stack if it differs from the last stack.
  if (sc->stacks.empty() || sc->stacks.back().length() != stack.length() ||
      !ArrayEqual(sc->stacks.back().begin(), stack.begin(), stack.length())) {
    if (!sc->stacks.append(std::move(stack))) {
      oomUnsafe.crash("stacks.append");
    }
  }
}
#endif

static bool EnableSingleStepProfiling(JSContext* cx, unsigned argc, Value* vp) {
#ifdef SINGLESTEP_PROFILING
  CallArgs args = CallArgsFromVp(argc, vp);

  jit::Simulator* sim = cx->simulator();
  sim->enable_single_stepping(SingleStepCallback, cx);

  args.rval().setUndefined();
  return true;
#else
  JS_ReportErrorASCII(cx, "single-step profiling not enabled on this platform");
  return false;
#endif
}

static bool DisableSingleStepProfiling(JSContext* cx, unsigned argc,
                                       Value* vp) {
#ifdef SINGLESTEP_PROFILING
  CallArgs args = CallArgsFromVp(argc, vp);

  jit::Simulator* sim = cx->simulator();
  sim->disable_single_stepping();

  ShellContext* sc = GetShellContext(cx);

  RootedValueVector elems(cx);
  for (size_t i = 0; i < sc->stacks.length(); i++) {
    JSString* stack =
        JS_NewUCStringCopyN(cx, sc->stacks[i].begin(), sc->stacks[i].length());
    if (!stack) {
      return false;
    }
    if (!elems.append(StringValue(stack))) {
      return false;
    }
  }

  JSObject* array = JS::NewArrayObject(cx, elems);
  if (!array) {
    return false;
  }

  sc->stacks.clear();
  args.rval().setObject(*array);
  return true;
#else
  JS_ReportErrorASCII(cx, "single-step profiling not enabled on this platform");
  return false;
#endif
}

static bool IsLatin1(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  bool isLatin1 =
      args.get(0).isString() && args[0].toString()->hasLatin1Chars();
  args.rval().setBoolean(isLatin1);
  return true;
}

static bool EnableGeckoProfiling(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!EnsureGeckoProfilingStackInstalled(cx, GetShellContext(cx))) {
    return false;
  }

  cx->runtime()->geckoProfiler().enableSlowAssertions(false);
  cx->runtime()->geckoProfiler().enable(true);

  args.rval().setUndefined();
  return true;
}

static bool EnableGeckoProfilingWithSlowAssertions(JSContext* cx, unsigned argc,
                                                   Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setUndefined();

  if (cx->runtime()->geckoProfiler().enabled()) {
    // If profiling already enabled with slow assertions disabled,
    // this is a no-op.
    if (cx->runtime()->geckoProfiler().slowAssertionsEnabled()) {
      return true;
    }

    // Slow assertions are off.  Disable profiling before re-enabling
    // with slow assertions on.
    cx->runtime()->geckoProfiler().enable(false);
  }

  if (!EnsureGeckoProfilingStackInstalled(cx, GetShellContext(cx))) {
    return false;
  }

  cx->runtime()->geckoProfiler().enableSlowAssertions(true);
  cx->runtime()->geckoProfiler().enable(true);

  return true;
}

static bool DisableGeckoProfiling(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setUndefined();

  if (!cx->runtime()->geckoProfiler().enabled()) {
    return true;
  }

  cx->runtime()->geckoProfiler().enable(false);
  return true;
}

// Global mailbox that is used to communicate a shareable object value from one
// worker to another.
//
// These object types are shareable:
//
//   - SharedArrayBuffer
//   - WasmMemoryObject (when constructed with shared:true)
//   - WasmModuleObject
//
// For the SharedArrayBuffer and WasmMemoryObject we transmit the underlying
// SharedArrayRawBuffer ("SARB"). For the WasmModuleObject we transmit the
// underlying JS::WasmModule.  The transmitted types are refcounted.  When they
// are in the mailbox their reference counts are at least 1, accounting for the
// reference from the mailbox.
//
// The lock guards the mailbox variable and prevents a race where two workers
// try to set the mailbox at the same time to replace an object that is only
// referenced from the mailbox: the workers will both decrement the reference
// count on the old object, and one of those decrements will be on a garbage
// object.  We could implement this with atomics and a CAS loop but it's not
// worth the bother.
//
// Note that if a thread reads the mailbox repeatedly it will get distinct
// objects on each read.  The alternatives are to cache created objects locally,
// but this retains storage we don't need to retain, or to somehow clear the
// mailbox locally, but this creates a coordination headache.  Buyer beware.

enum class MailboxTag {
  Empty,
  SharedArrayBuffer,
  WasmMemory,
  WasmModule,
  Number,
};

struct SharedObjectMailbox {
  union Value {
    struct {
      SharedArrayRawBuffer* buffer;
      size_t length;
      bool isHugeMemory;  // For a WasmMemory tag, otherwise false
      bool isGrowable;    // For GrowableSharedArrayBuffer, otherwise false
    } sarb;
    JS::WasmModule* module;
    double number;

    Value() : number(0.0) {}
  };

  MailboxTag tag = MailboxTag::Empty;
  Value val;
};

using SOMailbox = ExclusiveData<SharedObjectMailbox>;

// Never null after successful initialization.
static SOMailbox* sharedObjectMailbox;

static bool InitSharedObjectMailbox() {
  sharedObjectMailbox = js_new<SOMailbox>(mutexid::ShellObjectMailbox);
  return sharedObjectMailbox != nullptr;
}

static void DestructSharedObjectMailbox() {
  // All workers need to have terminated at this point.

  {
    auto mbx = sharedObjectMailbox->lock();
    switch (mbx->tag) {
      case MailboxTag::Empty:
      case MailboxTag::Number:
        break;
      case MailboxTag::SharedArrayBuffer:
      case MailboxTag::WasmMemory:
        mbx->val.sarb.buffer->dropReference();
        break;
      case MailboxTag::WasmModule:
        mbx->val.module->Release();
        break;
      default:
        MOZ_CRASH();
    }
  }

  js_delete(sharedObjectMailbox);
  sharedObjectMailbox = nullptr;
}

static bool GetSharedObject(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject newObj(cx);

  {
    auto mbx = sharedObjectMailbox->lock();
    switch (mbx->tag) {
      case MailboxTag::Empty: {
        break;
      }
      case MailboxTag::Number: {
        args.rval().setNumber(mbx->val.number);
        return true;
      }
      case MailboxTag::SharedArrayBuffer:
      case MailboxTag::WasmMemory: {
        // Flag was set in the sender; ensure it is set in the receiver.
        MOZ_ASSERT(
            cx->realm()->creationOptions().getSharedMemoryAndAtomicsEnabled());

        // The protocol for creating a SAB requires the refcount to be
        // incremented prior to the SAB creation.

        SharedArrayRawBuffer* buf = mbx->val.sarb.buffer;
        size_t length = mbx->val.sarb.length;
        if (!buf->addReference()) {
          JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                    JSMSG_SC_SAB_REFCNT_OFLO);
          return false;
        }

        // If the allocation fails we must decrement the refcount before
        // returning.

        Rooted<ArrayBufferObjectMaybeShared*> maybesab(cx);
        if (!mbx->val.sarb.isGrowable) {
          maybesab = SharedArrayBufferObject::New(cx, buf, length);
        } else {
          maybesab = SharedArrayBufferObject::NewGrowable(cx, buf, length);
        }
        if (!maybesab) {
          buf->dropReference();
          return false;
        }

        // At this point the SAB was created successfully and it owns the
        // refcount-increase on the buffer that we performed above.  So even
        // if we fail to allocate along any path below we must not decrement
        // the refcount; the garbage collector must be allowed to handle
        // that via finalization of the orphaned SAB object.

        if (mbx->tag == MailboxTag::SharedArrayBuffer) {
          newObj = maybesab;
        } else {
          if (!GlobalObject::ensureConstructor(cx, cx->global(),
                                               JSProto_WebAssembly)) {
            return false;
          }
          RootedObject proto(cx,
                             &cx->global()->getPrototype(JSProto_WasmMemory));
          newObj = WasmMemoryObject::create(cx, maybesab,
                                            mbx->val.sarb.isHugeMemory, proto);
          MOZ_ASSERT_IF(newObj, newObj->as<WasmMemoryObject>().isShared());
          if (!newObj) {
            return false;
          }
        }

        break;
      }
      case MailboxTag::WasmModule: {
        if (!GlobalObject::ensureConstructor(cx, cx->global(),
                                             JSProto_WebAssembly)) {
          return false;
        }

        // WasmModuleObject::create() increments the refcount on the module
        // and signals an error and returns null if that fails.
        newObj = mbx->val.module->createObject(cx);
        if (!newObj) {
          return false;
        }
        break;
      }
      default: {
        MOZ_CRASH();
      }
    }
  }

  args.rval().setObjectOrNull(newObj);
  return true;
}

static bool SetSharedObject(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  MailboxTag tag = MailboxTag::Empty;
  SharedObjectMailbox::Value value;

  // Increase refcounts when we obtain the value to avoid operating on dead
  // storage during self-assignment.

  if (args.get(0).isObject()) {
    RootedObject obj(cx, &args[0].toObject());
    if (obj->is<SharedArrayBufferObject>()) {
      Rooted<SharedArrayBufferObject*> sab(cx,
                                           &obj->as<SharedArrayBufferObject>());
      tag = MailboxTag::SharedArrayBuffer;
      value.sarb.buffer = sab->rawBufferObject();
      value.sarb.length = sab->byteLengthOrMaxByteLength();
      value.sarb.isHugeMemory = false;
      value.sarb.isGrowable = sab->isGrowable();
      if (!value.sarb.buffer->addReference()) {
        JS_ReportErrorASCII(cx,
                            "Reference count overflow on SharedArrayBuffer");
        return false;
      }
    } else if (obj->is<WasmMemoryObject>()) {
      // Here we must transmit sab.byteLength() as the length; the SARB has its
      // own notion of the length which may be greater, and that's fine.
      if (obj->as<WasmMemoryObject>().isShared()) {
        Rooted<SharedArrayBufferObject*> sab(
            cx, &obj->as<WasmMemoryObject>()
                     .buffer()
                     .as<SharedArrayBufferObject>());
        MOZ_ASSERT(!sab->isGrowable(), "unexpected growable shared buffer");
        tag = MailboxTag::WasmMemory;
        value.sarb.buffer = sab->rawBufferObject();
        value.sarb.length = sab->byteLength();
        value.sarb.isHugeMemory = obj->as<WasmMemoryObject>().isHuge();
        value.sarb.isGrowable = false;
        if (!value.sarb.buffer->addReference()) {
          JS_ReportErrorASCII(cx,
                              "Reference count overflow on SharedArrayBuffer");
          return false;
        }
      } else {
        JS_ReportErrorASCII(cx, "Invalid argument to SetSharedObject");
        return false;
      }
    } else if (JS::IsWasmModuleObject(obj)) {
      tag = MailboxTag::WasmModule;
      value.module = JS::GetWasmModule(obj).forget().take();
    } else {
      JS_ReportErrorASCII(cx, "Invalid argument to SetSharedObject");
      return false;
    }
  } else if (args.get(0).isNumber()) {
    tag = MailboxTag::Number;
    value.number = args.get(0).toNumber();
    // Nothing
  } else if (args.get(0).isNullOrUndefined()) {
    // Nothing
  } else {
    JS_ReportErrorASCII(cx, "Invalid argument to SetSharedObject");
    return false;
  }

  {
    auto mbx = sharedObjectMailbox->lock();

    switch (mbx->tag) {
      case MailboxTag::Empty:
      case MailboxTag::Number:
        break;
      case MailboxTag::SharedArrayBuffer:
      case MailboxTag::WasmMemory:
        mbx->val.sarb.buffer->dropReference();
        break;
      case MailboxTag::WasmModule:
        mbx->val.module->Release();
        break;
      default:
        MOZ_CRASH();
    }

    mbx->tag = tag;
    mbx->val = value;
  }

  args.rval().setUndefined();
  return true;
}

using Uint8Vector = Vector<uint8_t, 0, SystemAllocPolicy>;

class StreamCacheEntry : public AtomicRefCounted<StreamCacheEntry>,
                         public JS::OptimizedEncodingListener {
  using AtomicBase = AtomicRefCounted<StreamCacheEntry>;

  Uint8Vector bytes_;
  ExclusiveData<Uint8Vector> optimized_;

 public:
  explicit StreamCacheEntry(Uint8Vector&& original)
      : bytes_(std::move(original)),
        optimized_(mutexid::ShellStreamCacheEntryState) {}

  // Implement JS::OptimizedEncodingListener:

  MozExternalRefCountType MOZ_XPCOM_ABI AddRef() override {
    AtomicBase::AddRef();
    return 1;  // unused
  }
  MozExternalRefCountType MOZ_XPCOM_ABI Release() override {
    AtomicBase::Release();
    return 0;  // unused
  }

  const Uint8Vector& bytes() const { return bytes_; }

  void storeOptimizedEncoding(const uint8_t* srcBytes,
                              size_t srcLength) override {
    MOZ_ASSERT(srcLength > 0);

    // Tolerate races since a single StreamCacheEntry object can be used as
    // the source of multiple streaming compilations.
    auto dstBytes = optimized_.lock();
    if (dstBytes->length() > 0) {
      return;
    }

    if (!dstBytes->resize(srcLength)) {
      return;
    }
    memcpy(dstBytes->begin(), srcBytes, srcLength);
  }

  bool hasOptimizedEncoding() const { return !optimized_.lock()->empty(); }
  const Uint8Vector& optimizedEncoding() const {
    return optimized_.lock().get();
  }
};

using StreamCacheEntryPtr = RefPtr<StreamCacheEntry>;

class StreamCacheEntryObject : public NativeObject {
  static const unsigned CACHE_ENTRY_SLOT = 0;
  static const JSClassOps classOps_;
  static const JSPropertySpec properties_;

  static void finalize(JS::GCContext* gcx, JSObject* obj) {
    obj->as<StreamCacheEntryObject>().cache().Release();
  }

  static bool cachedGetter(JSContext* cx, unsigned argc, Value* vp) {
    CallArgs args = CallArgsFromVp(argc, vp);
    if (!args.thisv().isObject() ||
        !args.thisv().toObject().is<StreamCacheEntryObject>()) {
      JS_ReportErrorASCII(cx, "Expected StreamCacheEntry object");
      return false;
    }

    StreamCacheEntryObject& obj =
        args.thisv().toObject().as<StreamCacheEntryObject>();
    args.rval().setBoolean(obj.cache().hasOptimizedEncoding());
    return true;
  }
  static bool getBuffer(JSContext* cx, unsigned argc, Value* vp) {
    CallArgs args = CallArgsFromVp(argc, vp);
    if (!args.thisv().isObject() ||
        !args.thisv().toObject().is<StreamCacheEntryObject>()) {
      JS_ReportErrorASCII(cx, "Expected StreamCacheEntry object");
      return false;
    }

    auto& bytes =
        args.thisv().toObject().as<StreamCacheEntryObject>().cache().bytes();
    auto* buffer = ArrayBufferObject::createZeroed(cx, bytes.length());
    if (!buffer) {
      return false;
    }

    memcpy(buffer->dataPointer(), bytes.begin(), bytes.length());

    args.rval().setObject(*buffer);
    return true;
  }

 public:
  static const unsigned RESERVED_SLOTS = 1;
  static const JSClass class_;
  static const JSPropertySpec properties[];

  static bool construct(JSContext* cx, unsigned argc, Value* vp) {
    CallArgs args = CallArgsFromVp(argc, vp);
    if (!args.requireAtLeast(cx, "streamCacheEntry", 1)) {
      return false;
    }

    SharedMem<uint8_t*> ptr;
    size_t numBytes;
    if (!args[0].isObject() ||
        !IsBufferSource(cx, &args[0].toObject(), /*allowShared*/ true,
                        /*allowResizable*/ true, &ptr, &numBytes)) {
      RootedObject callee(cx, &args.callee());
      ReportUsageErrorASCII(cx, callee, "Argument must be an ArrayBuffer");
      return false;
    }

    Uint8Vector bytes;
    if (!bytes.resize(numBytes)) {
      ReportOutOfMemory(cx);
      return false;
    }

    memcpy(bytes.begin(), ptr.unwrap(), numBytes);

    RefPtr<StreamCacheEntry> cache =
        cx->new_<StreamCacheEntry>(std::move(bytes));
    if (!cache) {
      return false;
    }

    Rooted<NativeObject*> obj(
        cx, NewObjectWithGivenProto<StreamCacheEntryObject>(cx, nullptr));
    if (!obj) {
      return false;
    }
    obj->initReservedSlot(CACHE_ENTRY_SLOT,
                          PrivateValue(cache.forget().take()));

    if (!JS_DefineProperty(cx, obj, "cached", cachedGetter, nullptr, 0)) {
      return false;
    }
    if (!JS_DefineFunction(cx, obj, "getBuffer", getBuffer, 0, 0)) {
      return false;
    }

    args.rval().setObject(*obj);
    return true;
  }

  StreamCacheEntry& cache() const {
    return *(StreamCacheEntry*)getReservedSlot(CACHE_ENTRY_SLOT).toPrivate();
  }
};

const JSClassOps StreamCacheEntryObject::classOps_ = {
    nullptr,                           // addProperty
    nullptr,                           // delProperty
    nullptr,                           // enumerate
    nullptr,                           // newEnumerate
    nullptr,                           // resolve
    nullptr,                           // mayResolve
    StreamCacheEntryObject::finalize,  // finalize
    nullptr,                           // call
    nullptr,                           // construct
    nullptr,                           // trace
};

const JSClass StreamCacheEntryObject::class_ = {
    "StreamCacheEntryObject",
    JSCLASS_HAS_RESERVED_SLOTS(StreamCacheEntryObject::RESERVED_SLOTS) |
        JSCLASS_BACKGROUND_FINALIZE,
    &StreamCacheEntryObject::classOps_,
};

struct BufferStreamJob {
  Variant<Uint8Vector, StreamCacheEntryPtr> source;
  Thread thread;
  JS::StreamConsumer* consumer;

  BufferStreamJob(Uint8Vector&& source, JS::StreamConsumer* consumer)
      : source(AsVariant<Uint8Vector>(std::move(source))), consumer(consumer) {}
  BufferStreamJob(StreamCacheEntry& source, JS::StreamConsumer* consumer)
      : source(AsVariant<StreamCacheEntryPtr>(&source)), consumer(consumer) {}
};

struct BufferStreamState {
  Vector<UniquePtr<BufferStreamJob>, 0, SystemAllocPolicy> jobs;
  size_t delayMillis;
  size_t chunkSize;
  bool shutdown;

  BufferStreamState() : delayMillis(1), chunkSize(10), shutdown(false) {}

  ~BufferStreamState() { MOZ_ASSERT(jobs.empty()); }
};

static ExclusiveWaitableData<BufferStreamState>* bufferStreamState;

static void BufferStreamMain(BufferStreamJob* job) {
  const uint8_t* bytes;
  size_t byteLength;
  JS::OptimizedEncodingListener* listener;
  if (job->source.is<StreamCacheEntryPtr>()) {
    StreamCacheEntry& cache = *job->source.as<StreamCacheEntryPtr>();
    if (cache.hasOptimizedEncoding()) {
      const Uint8Vector& optimized = cache.optimizedEncoding();
      job->consumer->consumeOptimizedEncoding(optimized.begin(),
                                              optimized.length());
      goto done;
    }

    bytes = cache.bytes().begin();
    byteLength = cache.bytes().length();
    listener = &cache;
  } else {
    bytes = job->source.as<Uint8Vector>().begin();
    byteLength = job->source.as<Uint8Vector>().length();
    listener = nullptr;
  }

  size_t byteOffset;
  byteOffset = 0;
  while (true) {
    if (byteOffset == byteLength) {
      job->consumer->streamEnd(listener);
      break;
    }

    bool shutdown;
    size_t delayMillis;
    size_t chunkSize;
    {
      auto state = bufferStreamState->lock();
      shutdown = state->shutdown;
      delayMillis = state->delayMillis;
      chunkSize = state->chunkSize;
    }

    if (shutdown) {
      job->consumer->streamError(JSMSG_STREAM_CONSUME_ERROR);
      break;
    }

    ThisThread::SleepMilliseconds(delayMillis);

    chunkSize = std::min(chunkSize, byteLength - byteOffset);

    if (!job->consumer->consumeChunk(bytes + byteOffset, chunkSize)) {
      break;
    }

    byteOffset += chunkSize;
  }

done:
  auto state = bufferStreamState->lock();
  size_t jobIndex = 0;
  while (state->jobs[jobIndex].get() != job) {
    jobIndex++;
  }
  job->thread.detach();  // quiet assert in ~Thread() called by erase().
  state->jobs.erase(state->jobs.begin() + jobIndex);
  if (state->jobs.empty()) {
    state.notify_all(/* jobs empty */);
  }
}

static bool ConsumeBufferSource(JSContext* cx, JS::HandleObject obj,
                                JS::MimeType, JS::StreamConsumer* consumer) {
  {
    RootedValue url(cx);
    if (!JS_GetProperty(cx, obj, "url", &url)) {
      return false;
    }
    UniqueChars urlChars;
    if (url.isString()) {
      Rooted<JSString*> str(cx, url.toString());
      urlChars = JS_EncodeStringToUTF8(cx, str);
      if (!urlChars) {
        return false;
      }
    }

    RootedValue mapUrl(cx);
    if (!JS_GetProperty(cx, obj, "sourceMappingURL", &mapUrl)) {
      return false;
    }
    UniqueChars mapUrlChars;
    if (mapUrl.isString()) {
      Rooted<JSString*> str(cx, mapUrl.toString());
      mapUrlChars = JS_EncodeStringToUTF8(cx, str);
      if (!mapUrlChars) {
        return false;
      }
    }

    consumer->noteResponseURLs(urlChars.get(), mapUrlChars.get());
  }

  UniquePtr<BufferStreamJob> job;

  SharedMem<uint8_t*> dataPointer;
  size_t byteLength;
  if (IsBufferSource(cx, obj, /*allowShared*/ true, /*allowResizable*/ true,
                     &dataPointer, &byteLength)) {
    Uint8Vector bytes;
    if (!bytes.resize(byteLength)) {
      JS_ReportOutOfMemory(cx);
      return false;
    }

    memcpy(bytes.begin(), dataPointer.unwrap(), byteLength);
    job = cx->make_unique<BufferStreamJob>(std::move(bytes), consumer);
  } else if (obj->is<StreamCacheEntryObject>()) {
    job = cx->make_unique<BufferStreamJob>(
        obj->as<StreamCacheEntryObject>().cache(), consumer);
  } else {
    JS_ReportErrorASCII(
        cx,
        "shell streaming consumes a buffer source (buffer or view) "
        "or StreamCacheEntryObject");
    return false;
  }
  if (!job) {
    return false;
  }

  BufferStreamJob* jobPtr = job.get();

  {
    auto state = bufferStreamState->lock();
    MOZ_ASSERT(!state->shutdown);
    if (!state->jobs.append(std::move(job))) {
      JS_ReportOutOfMemory(cx);
      return false;
    }
  }

  {
    AutoEnterOOMUnsafeRegion oomUnsafe;
    if (!jobPtr->thread.init(BufferStreamMain, jobPtr)) {
      oomUnsafe.crash("ConsumeBufferSource");
    }
  }

  return true;
}

static void ReportStreamError(JSContext* cx, size_t errorNumber) {
  JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr, errorNumber);
}

static bool SetBufferStreamParams(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  if (!args.requireAtLeast(cx, "setBufferStreamParams", 2)) {
    return false;
  }

  double delayMillis;
  if (!ToNumber(cx, args[0], &delayMillis)) {
    return false;
  }

  double chunkSize;
  if (!ToNumber(cx, args[1], &chunkSize)) {
    return false;
  }

  {
    auto state = bufferStreamState->lock();
    state->delayMillis = delayMillis;
    state->chunkSize = chunkSize;
  }

  args.rval().setUndefined();
  return true;
}

static void ShutdownBufferStreams() {
  auto state = bufferStreamState->lock();
  state->shutdown = true;
  while (!state->jobs.empty()) {
    state.wait(/* jobs empty */);
  }
  state->jobs.clearAndFree();
}

static bool DumpScopeChain(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject callee(cx, &args.callee());

  if (js::SupportDifferentialTesting()) {
    ReportUsageErrorASCII(
        cx, callee, "Function not available in differential testing mode.");
    return false;
  }

  if (args.length() != 1) {
    ReportUsageErrorASCII(cx, callee, "Wrong number of arguments");
    return false;
  }

  if (!args[0].isObject() ||
      !(args[0].toObject().is<JSFunction>() ||
        args[0].toObject().is<ShellModuleObjectWrapper>())) {
    ReportUsageErrorASCII(
        cx, callee, "Argument must be an interpreted function or a module");
    return false;
  }

  RootedObject obj(cx, &args[0].toObject());
  RootedScript script(cx);

  if (obj->is<JSFunction>()) {
    RootedFunction fun(cx, &obj->as<JSFunction>());
    if (!fun->isInterpreted()) {
      ReportUsageErrorASCII(cx, callee,
                            "Argument must be an interpreted function");
      return false;
    }
    script = JSFunction::getOrCreateScript(cx, fun);
    if (!script) {
      return false;
    }
  } else {
    script = obj->as<ShellModuleObjectWrapper>().get()->maybeScript();
    if (!script) {
      JS_ReportErrorASCII(cx, "module does not have an associated script");
      return false;
    }
  }

  script->bodyScope()->dump();

  args.rval().setUndefined();
  return true;
}

// For testing GC marking, blackRoot() and grayRoot() will heap-allocate an
// array whose elements (as well as the array itself) will be marked as roots in
// subsequent GCs.
//
// Note that EnsureGrayRoot() will blacken the returned object, so it will not
// actually end up marked gray until the following GC clears the black bit
// (assuming nothing is holding onto it.)
//
// The idea is that you can set up a whole graph of objects to be marked gray,
// hanging off of the object returned from grayRoot(). Then you GC to clear the
// black bits and set the gray bits.
//
// To test grayness, register the objects of interest with addMarkObservers(),
// which takes an Array of objects (which will be marked black at the time
// they're passed in). Their mark bits may be retrieved at any time with
// getMarks(), in the form of an array of strings with each index corresponding
// to the original objects passed to addMarkObservers().

static bool EnsureRootArray(JSContext* cx, gc::MarkColor color, unsigned argc,
                            Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  auto priv = EnsureShellCompartmentPrivate(cx);
  if (!priv) {
    return false;
  }

  GCPtr<ArrayObject*>& root =
      (color == gc::MarkColor::Black) ? priv->blackRoot : priv->grayRoot;

  if (!root && !(root = NewTenuredDenseEmptyArray(cx))) {
    return false;
  }

  // Barrier to enforce the invariant that JS does not touch gray objects.
  JSObject* obj = root;
  JS::ExposeObjectToActiveJS(obj);

  args.rval().setObject(*obj);
  return true;
}

static bool EnsureBlackRoot(JSContext* cx, unsigned argc, Value* vp) {
  return EnsureRootArray(cx, gc::MarkColor::Black, argc, vp);
}

static bool EnsureGrayRoot(JSContext* cx, unsigned argc, Value* vp) {
  return EnsureRootArray(cx, gc::MarkColor::Gray, argc, vp);
}

static MarkBitObservers* EnsureMarkBitObservers(JSContext* cx) {
  ShellContext* sc = GetShellContext(cx);
  if (!sc->markObservers) {
    auto* observers =
        cx->new_<MarkBitObservers>(cx->runtime(), NonshrinkingGCObjectVector());
    if (!observers) {
      return nullptr;
    }
    sc->markObservers.reset(observers);
  }
  return sc->markObservers.get();
}

static bool ClearMarkObservers(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  auto markObservers = EnsureMarkBitObservers(cx);
  if (!markObservers) {
    return false;
  }

  markObservers->get().clear();

  args.rval().setUndefined();
  return true;
}

static bool AddMarkObservers(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  auto markObservers = EnsureMarkBitObservers(cx);
  if (!markObservers) {
    return false;
  }

  if (!args.get(0).isObject()) {
    JS_ReportErrorASCII(cx, "argument must be an Array of objects");
    return false;
  }

  RootedObject observersArg(cx, &args[0].toObject());
  uint64_t length;
  if (!GetLengthProperty(cx, observersArg, &length)) {
    return false;
  }

  if (length > UINT32_MAX) {
    JS_ReportErrorASCII(cx, "Invalid length for observers array");
    return false;
  }

  RootedValue value(cx);
  RootedObject object(cx);
  for (uint32_t i = 0; i < length; i++) {
    if (!JS_GetElement(cx, observersArg, i, &value)) {
      return false;
    }

    if (!value.isObject()) {
      JS_ReportErrorASCII(cx, "argument must be an Array of objects");
      return false;
    }

    object = &value.toObject();
    if (gc::IsInsideNursery(object)) {
      // WeakCaches are not swept during a minor GC. To prevent
      // nursery-allocated contents from having the mark bits be deceptively
      // black until the second GC, they would need to be marked weakly (cf
      // NurseryAwareHashMap). It is simpler to evict the nursery to prevent
      // nursery objects from being observed.
      cx->runtime()->gc.evictNursery();
    }

    if (!markObservers->get().append(object)) {
      ReportOutOfMemory(cx);
      return false;
    }
  }

  args.rval().setInt32(length);
  return true;
}

static bool GetMarks(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  auto& observers = GetShellContext(cx)->markObservers;
  if (!observers) {
    args.rval().setUndefined();
    return true;
  }

  size_t length = observers->get().length();
  Rooted<ArrayObject*> ret(cx, js::NewDenseEmptyArray(cx));
  if (!ret) {
    return false;
  }

  for (uint32_t i = 0; i < length; i++) {
    const char* color;
    JSObject* obj = observers->get()[i];
    if (!obj) {
      color = "dead";
    } else if (obj->zone()->isGCPreparing()) {
      color = "unmarked";
    } else {
      gc::TenuredCell* cell = &obj->asTenured();
      if (cell->isMarkedGray()) {
        color = "gray";
      } else if (cell->isMarkedBlack()) {
        color = "black";
      } else {
        color = "unmarked";
      }
    }
    JSString* s = JS_NewStringCopyZ(cx, color);
    if (!s) {
      return false;
    }
    if (!NewbornArrayPush(cx, ret, StringValue(s))) {
      return false;
    }
  }

  args.rval().setObject(*ret);
  return true;
}

#ifndef __wasi__
static bool WasmTextToBinary(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject callee(cx, &args.callee());

  if (!args.requireAtLeast(cx, "wasmTextToBinary", 1)) {
    return false;
  }

  if (!args[0].isString()) {
    ReportUsageErrorASCII(cx, callee, "First argument must be a String");
    return false;
  }

  size_t textLen = args[0].toString()->length();

  AutoStableStringChars twoByteChars(cx);
  if (!twoByteChars.initTwoByte(cx, args[0].toString())) {
    return false;
  }

  wasm::Bytes bytes;
  UniqueChars error;
  if (!wasm::TextToBinary(twoByteChars.twoByteChars(), textLen, &bytes,
                          &error)) {
    JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr, JSMSG_WASM_TEXT_FAIL,
                             error.get() ? error.get() : "out of memory");
    return false;
  }

  RootedObject binary(cx, JS_NewUint8Array(cx, bytes.length()));
  if (!binary) {
    return false;
  }

  memcpy(binary->as<TypedArrayObject>().dataPointerUnshared(), bytes.begin(),
         bytes.length());

  args.rval().setObject(*binary);
  return true;
}

#  ifndef __AFL_HAVE_MANUAL_CONTROL
#    define __AFL_LOOP(x) true
#  endif

static bool WasmLoop(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject callee(cx, &args.callee());

  if (args.length() < 1 || args.length() > 2) {
    ReportUsageErrorASCII(cx, callee, "Wrong number of arguments");
    return false;
  }

  if (!args[0].isString()) {
    ReportUsageErrorASCII(cx, callee, "First argument must be a String");
    return false;
  }

  RootedObject importObj(cx);
  if (!args.get(1).isUndefined()) {
    if (!args.get(1).isObject()) {
      ReportUsageErrorASCII(cx, callee,
                            "Second argument, if present, must be an Object");
      return false;
    }
    importObj = &args[1].toObject();
  }

  RootedString givenPath(cx, args[0].toString());
  RootedString filename(cx, ResolvePath(cx, givenPath, RootRelative));
  if (!filename) {
    return false;
  }

  while (__AFL_LOOP(1000)) {
    Rooted<JSObject*> ret(cx, FileAsTypedArray(cx, filename));
    if (!ret) {
      return false;
    }

    Rooted<TypedArrayObject*> typedArray(cx, &ret->as<TypedArrayObject>());
    Rooted<WasmInstanceObject*> instanceObj(cx);
    if (!wasm::Eval(cx, typedArray, importObj, &instanceObj)) {
      // Clear any pending exceptions, we don't care about them
      cx->clearPendingException();
    }
  }

#  ifdef __AFL_HAVE_MANUAL_CONTROL  // to silence unreachable code warning
  return true;
#  endif
}
#endif  // __wasi__

static constexpr uint32_t DOM_OBJECT_SLOT = 0;
static constexpr uint32_t DOM_OBJECT_SLOT2 = 1;

static const JSClass* GetDomClass();

static JSObject* GetDOMPrototype(JSContext* cx, JSObject* global);

static void TransplantableDOMObject_finalize(JS::GCContext* gcx,
                                             JSObject* obj) {
  // Dummy finalize method so we can swap with background finalized object.
}

static const JSClassOps TransplantableDOMObjectClassOps = {
    nullptr,  // addProperty
    nullptr,  // delProperty
    nullptr,  // enumerate
    nullptr,  // newEnumerate
    nullptr,  // resolve
    nullptr,  // mayResolve
    TransplantableDOMObject_finalize,
    nullptr,  // call
    nullptr,  // construct
    nullptr,
};

static const JSClass TransplantableDOMObjectClass = {
    "TransplantableDOMObject",
    JSCLASS_IS_DOMJSCLASS | JSCLASS_HAS_RESERVED_SLOTS(1) |
        JSCLASS_BACKGROUND_FINALIZE,
    &TransplantableDOMObjectClassOps};

static const JSClass TransplantableDOMProxyObjectClass =
    PROXY_CLASS_DEF("TransplantableDOMProxyObject",
                    JSCLASS_IS_DOMJSCLASS | JSCLASS_HAS_RESERVED_SLOTS(1));

class TransplantableDOMProxyHandler final : public ForwardingProxyHandler {
 public:
  static const TransplantableDOMProxyHandler singleton;
  static const char family;

  constexpr TransplantableDOMProxyHandler() : ForwardingProxyHandler(&family) {}

  // These two proxy traps are called in |js::DeadProxyTargetValue|, which in
  // turn is called when nuking proxies. Because this proxy can temporarily be
  // without an object in its private slot, see |EnsureExpandoObject|, the
  // default implementation inherited from ForwardingProxyHandler can't be used,
  // since it tries to derive the callable/constructible value from the target.
  bool isCallable(JSObject* obj) const override { return false; }
  bool isConstructor(JSObject* obj) const override { return false; }

  // Simplified implementation of |DOMProxyHandler::GetAndClearExpandoObject|.
  static JSObject* GetAndClearExpandoObject(JSObject* obj) {
    const Value& v = GetProxyPrivate(obj);
    if (v.isUndefined()) {
      return nullptr;
    }

    JSObject* expandoObject = &v.toObject();
    SetProxyPrivate(obj, UndefinedValue());
    return expandoObject;
  }

  // Simplified implementation of |DOMProxyHandler::EnsureExpandoObject|.
  static JSObject* EnsureExpandoObject(JSContext* cx, JS::HandleObject obj) {
    const Value& v = GetProxyPrivate(obj);
    if (v.isObject()) {
      return &v.toObject();
    }
    MOZ_ASSERT(v.isUndefined());

    JSObject* expando = JS_NewObjectWithGivenProto(cx, nullptr, nullptr);
    if (!expando) {
      return nullptr;
    }
    SetProxyPrivate(obj, ObjectValue(*expando));
    return expando;
  }
};

const TransplantableDOMProxyHandler TransplantableDOMProxyHandler::singleton;
const char TransplantableDOMProxyHandler::family = 0;

enum TransplantObjectSlots {
  TransplantSourceObject = 0,
};

static bool TransplantObject(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedFunction callee(cx, &args.callee().as<JSFunction>());

  if (args.length() != 1 || !args[0].isObject()) {
    JS_ReportErrorASCII(cx, "transplant() must be called with an object");
    return false;
  }

  // |newGlobal| needs to be a GlobalObject.
  RootedObject newGlobal(
      cx, js::CheckedUnwrapDynamic(&args[0].toObject(), cx,
                                   /* stopAtWindowProxy = */ false));
  if (!newGlobal) {
    ReportAccessDenied(cx);
    return false;
  }
  if (!JS_IsGlobalObject(newGlobal)) {
    JS_ReportErrorNumberASCII(
        cx, GetErrorMessage, nullptr, JSMSG_UNEXPECTED_TYPE,
        "\"global\" passed to transplant()", "not a global object");
    return false;
  }

  const Value& reserved =
      GetFunctionNativeReserved(callee, TransplantSourceObject);
  RootedObject source(cx, CheckedUnwrapStatic(&reserved.toObject()));
  if (!source) {
    ReportAccessDenied(cx);
    return false;
  }
  if (JS_IsDeadWrapper(source)) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_DEAD_OBJECT);
    return false;
  }
  MOZ_ASSERT(source->getClass()->isDOMClass());

  // The following steps aim to replicate the behavior of UpdateReflectorGlobal
  // in dom/bindings/BindingUtils.cpp. In detail:
  // 1. Check the recursion depth using checkConservative.
  // 2. Enter the target compartment.
  // 3. Clone the source object using JS_CloneObject.
  // 4. Check if new wrappers can be created if source and target are in
  //    different compartments.
  // 5. Copy all properties from source to a temporary holder object.
  // 6. Actually transplant the object.
  // 7. And finally copy the properties back to the source object.
  //
  // As an extension to the algorithm in UpdateReflectorGlobal, we also allow
  // to transplant an object into the same compartment as the source object to
  // cover all operations supported by JS_TransplantObject.

  AutoCheckRecursionLimit recursion(cx);
  if (!recursion.checkConservative(cx)) {
    return false;
  }

  bool isProxy = IsProxy(source);
  RootedObject expandoObject(cx);
  if (isProxy) {
    expandoObject =
        TransplantableDOMProxyHandler::GetAndClearExpandoObject(source);
  }

  JSAutoRealm ar(cx, newGlobal);

  RootedObject proto(cx);
  if (JS::GetClass(source) == GetDomClass()) {
    proto = GetDOMPrototype(cx, newGlobal);
  } else {
    proto = JS::GetRealmObjectPrototype(cx);
  }
  if (!proto) {
    return false;
  }

  RootedObject target(cx, JS_CloneObject(cx, source, proto));
  if (!target) {
    return false;
  }

  if (JS::GetCompartment(source) != JS::GetCompartment(target) &&
      !AllowNewWrapper(JS::GetCompartment(source), target)) {
    JS_ReportErrorASCII(cx, "Cannot transplant into nuked compartment");
    return false;
  }

  RootedObject copyFrom(cx, isProxy ? expandoObject : source);
  RootedObject propertyHolder(cx,
                              JS_NewObjectWithGivenProto(cx, nullptr, nullptr));
  if (!propertyHolder) {
    return false;
  }

  if (!JS_CopyOwnPropertiesAndPrivateFields(cx, propertyHolder, copyFrom)) {
    return false;
  }

  JS::SetReservedSlot(target, DOM_OBJECT_SLOT,
                      JS::GetReservedSlot(source, DOM_OBJECT_SLOT));
  JS::SetReservedSlot(source, DOM_OBJECT_SLOT, JS::PrivateValue(nullptr));
  if (JS::GetClass(source) == GetDomClass()) {
    JS::SetReservedSlot(target, DOM_OBJECT_SLOT2,
                        JS::GetReservedSlot(source, DOM_OBJECT_SLOT2));
    JS::SetReservedSlot(source, DOM_OBJECT_SLOT2, UndefinedValue());
  }

  source = JS_TransplantObject(cx, source, target);
  if (!source) {
    return false;
  }

  RootedObject copyTo(cx);
  if (isProxy) {
    copyTo = TransplantableDOMProxyHandler::EnsureExpandoObject(cx, source);
    if (!copyTo) {
      return false;
    }
  } else {
    copyTo = source;
  }
  if (!JS_CopyOwnPropertiesAndPrivateFields(cx, copyTo, propertyHolder)) {
    return false;
  }

  args.rval().setUndefined();
  return true;
}

static bool TransplantableObject(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject callee(cx, &args.callee());

  if (args.length() > 1) {
    ReportUsageErrorASCII(cx, callee, "Wrong number of arguments");
    return false;
  }

  bool createProxy = false;
  RootedObject source(cx);
  if (args.length() == 1 && !args[0].isUndefined()) {
    if (!args[0].isObject()) {
      ReportUsageErrorASCII(cx, callee, "Argument must be an object");
      return false;
    }

    RootedObject options(cx, &args[0].toObject());
    RootedValue value(cx);

    if (!JS_GetProperty(cx, options, "proxy", &value)) {
      return false;
    }
    createProxy = JS::ToBoolean(value);

    if (!JS_GetProperty(cx, options, "object", &value)) {
      return false;
    }
    if (!value.isUndefined()) {
      if (!value.isObject()) {
        ReportUsageErrorASCII(cx, callee, "'object' option must be an object");
        return false;
      }

      source = &value.toObject();
      if (JS::GetClass(source) != GetDomClass()) {
        ReportUsageErrorASCII(cx, callee, "Object not a FakeDOMObject");
        return false;
      }

      // |source| must be a tenured object to be transplantable.
      if (gc::IsInsideNursery(source)) {
        JS_GC(cx);

        MOZ_ASSERT(!gc::IsInsideNursery(source),
                   "Live objects should be tenured after one GC, because "
                   "the nursery has only a single generation");
      }
    }
  }

  if (!source) {
    if (!createProxy) {
      source = NewBuiltinClassInstance(cx, &TransplantableDOMObjectClass,
                                       TenuredObject);
      if (!source) {
        return false;
      }

      JS::SetReservedSlot(source, DOM_OBJECT_SLOT, JS::PrivateValue(nullptr));
    } else {
      JSObject* expando = JS_NewPlainObject(cx);
      if (!expando) {
        return false;
      }
      RootedValue expandoVal(cx, ObjectValue(*expando));

      ProxyOptions options;
      options.setClass(&TransplantableDOMProxyObjectClass);
      options.setLazyProto(true);

      source = NewProxyObject(cx, &TransplantableDOMProxyHandler::singleton,
                              expandoVal, nullptr, options);
      if (!source) {
        return false;
      }

      SetProxyReservedSlot(source, DOM_OBJECT_SLOT, JS::PrivateValue(nullptr));
    }
  }

  jsid emptyId = NameToId(cx->names().empty_);
  RootedObject transplant(
      cx, NewFunctionByIdWithReserved(cx, TransplantObject, 0, 0, emptyId));
  if (!transplant) {
    return false;
  }

  SetFunctionNativeReserved(transplant, TransplantSourceObject,
                            ObjectValue(*source));

  RootedObject result(cx, JS_NewPlainObject(cx));
  if (!result) {
    return false;
  }

  RootedValue sourceVal(cx, ObjectValue(*source));
  RootedValue transplantVal(cx, ObjectValue(*transplant));
  if (!JS_DefineProperty(cx, result, "object", sourceVal, 0) ||
      !JS_DefineProperty(cx, result, "transplant", transplantVal, 0)) {
    return false;
  }

  args.rval().setObject(*result);
  return true;
}

#ifdef DEBUG
static bool DebugGetQueuedJobs(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  JSObject* jobs = js::GetJobsInInternalJobQueue(cx);
  if (!jobs) {
    return false;
  }

  args.rval().setObject(*jobs);
  return true;
}
#endif

#ifdef FUZZING_INTERFACES
extern "C" {
size_t gluesmith(uint8_t* data, size_t size, uint8_t* out, size_t maxsize);
}

static bool GetWasmSmithModule(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject callee(cx, &args.callee());

  if (args.length() != 1) {
    ReportUsageErrorASCII(cx, callee, "Wrong number of arguments");
    return false;
  }

  if (!args[0].isObject() || !args[0].toObject().is<ArrayBufferObject>()) {
    ReportUsageErrorASCII(cx, callee, "Argument must be ArrayBuffer.");
    return false;
  }

  ArrayBufferObject* arrayBuffer = &args[0].toObject().as<ArrayBufferObject>();
  size_t length = arrayBuffer->byteLength();
  uint8_t* data = arrayBuffer->dataPointer();

  const size_t maxModuleSize = 4096;
  uint8_t tmp[maxModuleSize];

  size_t outSize = gluesmith(data, length, tmp, maxModuleSize);
  if (!outSize) {
    JS_ReportErrorASCII(cx, "Generated module is too large.");
    return false;
  }

  JS::Rooted<JSObject*> outArr(cx, JS_NewUint8ClampedArray(cx, outSize));
  if (!outArr) {
    return false;
  }

  {
    JS::AutoCheckCannotGC nogc;
    bool isShared;
    uint8_t* data = JS_GetUint8ClampedArrayData(outArr, &isShared, nogc);
    MOZ_RELEASE_ASSERT(!isShared);
    memcpy(data, tmp, outSize);
  }

  args.rval().setObject(*outArr);
  return true;
}

#endif

static bool IsValidJSON(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject callee(cx, &args.callee());

  if (!args.get(0).isString()) {
    ReportUsageErrorASCII(cx, callee, "First argument must be a String");
    return false;
  }

  JS::Rooted<JSLinearString*> input(cx, args[0].toString()->ensureLinear(cx));
  if (!input) {
    return false;
  }

  bool result;
  if (input->hasLatin1Chars()) {
    JS::AutoCheckCannotGC nogc;
    result = JS::IsValidJSON(input->latin1Chars(nogc), input->length());
  } else {
    JS::AutoCheckCannotGC nogc;
    result = JS::IsValidJSON(input->twoByteChars(nogc), input->length());
  }

  args.rval().setBoolean(result);
  return true;
}

// Quick file format for a LZ4 compressed file
static constexpr uint32_t LZ4MagicHeader = -1;
// A magic word and a length field
static constexpr size_t LZ4HeaderSize = sizeof(uint32_t) * 2;
static constexpr size_t LZ4MaxSize = UINT32_MAX;

static bool CompressLZ4(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject callee(cx, &args.callee());

  if (!args.get(0).isObject() ||
      !args.get(0).toObject().is<ArrayBufferObject>()) {
    ReportUsageErrorASCII(cx, callee, "First argument must be an ArrayBuffer");
    return false;
  }

  JS::Rooted<ArrayBufferObject*> bytes(
      cx, &args.get(0).toObject().as<ArrayBufferObject>());
  size_t byteLength = bytes->byteLength();
#ifdef JS_64BIT
  if (byteLength > LZ4MaxSize) {
    ReportOutOfMemory(cx);
    return false;
  }
#else
  static_assert(LZ4MaxSize == UINT32_MAX, "don't need to check max on 32-bit");
#endif

  // Create a buffer big enough for the header and the max amount of compressed
  // bytes.
  size_t outputCapacity =
      LZ4HeaderSize + mozilla::Compression::LZ4::maxCompressedSize(byteLength);

  mozilla::UniquePtr<void, JS::FreePolicy> output(js_malloc(outputCapacity));
  if (!output) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Write the magic header word and decompressed size in bytes.
  ((uint32_t*)(output.get()))[0] = LZ4MagicHeader;
  ((uint32_t*)(output.get()))[1] = byteLength;

  // Compress the bytes into the output
  char* compressedBytesStart = ((char*)output.get()) + LZ4HeaderSize;
  size_t compressedBytesLength = mozilla::Compression::LZ4::compress(
      (const char*)bytes->dataPointer(), byteLength, compressedBytesStart);
  size_t outputLength = compressedBytesLength + LZ4HeaderSize;

  // Create an ArrayBuffer wrapping the compressed bytes
  JSObject* outputArrayBuffer =
      NewArrayBufferWithContents(cx, outputLength, std::move(output));
  if (!outputArrayBuffer) {
    return false;
  }

  args.rval().setObject(*outputArrayBuffer);
  return true;
}

static bool DecompressLZ4(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  RootedObject callee(cx, &args.callee());

  if (!args.get(0).isObject() ||
      !args.get(0).toObject().is<ArrayBufferObject>()) {
    ReportUsageErrorASCII(cx, callee, "First argument must be an ArrayBuffer");
    return false;
  }

  JS::Rooted<ArrayBufferObject*> bytes(
      cx, &args.get(0).toObject().as<ArrayBufferObject>());
  size_t byteLength = bytes->byteLength();
  if (byteLength < LZ4HeaderSize) {
    JS_ReportErrorASCII(cx, "Invalid LZ4 buffer");
    return false;
  }

  // Check the magic header and get the decompressed byte length.
  uint32_t magicHeader = ((uint32_t*)(bytes->dataPointer()))[0];
  uint32_t decompressedBytesLength = ((uint32_t*)(bytes->dataPointer()))[1];
  if (magicHeader != LZ4MagicHeader) {
    JS_ReportErrorASCII(cx, "Invalid magic header");
    return false;
  }

  // Allocate a buffer to store the decompressed bytes.
  mozilla::UniquePtr<void, JS::FreePolicy> decompressedBytes(
      js_malloc(decompressedBytesLength));
  if (!decompressedBytes) {
    ReportOutOfMemory(cx);
    return false;
  }

  // Decompress the bytes into the output
  const char* compressedBytesStart =
      ((const char*)bytes->dataPointer()) + LZ4HeaderSize;
  size_t compressedBytesLength = byteLength - LZ4HeaderSize;
  size_t actualDecompressedBytesLength = 0;
  if (!mozilla::Compression::LZ4::decompress(
          compressedBytesStart, compressedBytesLength,
          (char*)decompressedBytes.get(), decompressedBytesLength,
          &actualDecompressedBytesLength) ||
      actualDecompressedBytesLength != decompressedBytesLength) {
    JS_ReportErrorASCII(cx, "Invalid LZ4 buffer");
    return false;
  }

  // Create an ArrayBuffer wrapping the decompressed bytes
  JSObject* outputArrayBuffer = NewArrayBufferWithContents(
      cx, decompressedBytesLength, std::move(decompressedBytes));
  if (!outputArrayBuffer) {
    return false;
  }

  args.rval().setObject(*outputArrayBuffer);
  return true;
}

static bool SideEffectfulResolveObject_enumerate(
    JSContext* cx, JS::HandleObject obj, JS::MutableHandleIdVector properties,
    bool enumerableOnly) {
  return properties.append(NameToId(cx->names().test));
}

static bool SideEffectfulResolveObject_resolve(JSContext* cx, HandleObject obj,
                                               HandleId id, bool* resolvedp) {
  *resolvedp = false;
  if (JS::dbg::ShouldAvoidSideEffects(cx)) {
    JS::ReportUncatchableException(cx);
    return false;
  }

  if (id == NameToId(cx->names().test)) {
    RootedValue value(cx, JS::NumberValue(42));
    if (!JS_DefinePropertyById(cx, obj, id, value, JSPROP_ENUMERATE)) {
      return false;
    }
    *resolvedp = true;
  }

  return true;
}

static const JSClassOps SideEffectfulResolveObject_classOps = {
    nullptr,                               // addProperty
    nullptr,                               // delProperty
    nullptr,                               // enumerate
    SideEffectfulResolveObject_enumerate,  // newEnumerate
    SideEffectfulResolveObject_resolve,    // resolve
    nullptr,                               // mayResolve
    nullptr,                               // finalize
    nullptr,                               // call
    nullptr,                               // construct
    nullptr,
};

static const JSClass SideEffectfulResolveObject_class = {
    "SideEffectfulResolveObject",
    0,
    &SideEffectfulResolveObject_classOps,
};

static bool CreateSideEffectfulResolveObject(JSContext* cx, unsigned argc,
                                             JS::Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  RootedObject obj(cx, JS_NewObject(cx, &SideEffectfulResolveObject_class));
  if (!obj) {
    return false;
  }

  args.rval().setObject(*obj);
  return true;
}

#ifdef MOZ_EXECUTION_TRACING

static bool EnableExecutionTracing(JSContext* cx, unsigned argc,
                                   JS::Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!JS_TracerBeginTracing(cx)) {
    return false;
  }

  args.rval().setUndefined();
  return true;
}

static bool GetExecutionTrace(JSContext* cx, unsigned argc, JS::Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  JS::ExecutionTrace trace;
  if (!JS_TracerSnapshotTrace(trace)) {
    JS_ReportErrorASCII(cx, "Failed to get the execution trace");
    return false;
  }

  JS::Rooted<JSString*> functionEnterStr(cx,
                                         JS_AtomizeString(cx, "FunctionEnter"));
  if (!functionEnterStr) {
    return false;
  }
  JS::Rooted<JSString*> functionLeaveStr(cx,
                                         JS_AtomizeString(cx, "FunctionLeave"));
  if (!functionLeaveStr) {
    return false;
  }
  JS::Rooted<JSString*> labelEnterStr(cx, JS_AtomizeString(cx, "LabelEnter"));
  if (!labelEnterStr) {
    return false;
  }
  JS::Rooted<JSString*> labelLeaveStr(cx, JS_AtomizeString(cx, "LabelLeave"));
  if (!labelLeaveStr) {
    return false;
  }
  JS::Rooted<JSString*> errorStr(cx, JS_AtomizeString(cx, "Error"));
  if (!errorStr) {
    return false;
  }

  JS::Rooted<JSString*> interpreterStr(cx, JS_AtomizeString(cx, "interpreter"));
  if (!interpreterStr) {
    return false;
  }
  JS::Rooted<JSString*> baselineStr(cx, JS_AtomizeString(cx, "baseline"));
  if (!baselineStr) {
    return false;
  }
  JS::Rooted<JSString*> ionStr(cx, JS_AtomizeString(cx, "ion"));
  if (!ionStr) {
    return false;
  }
  JS::Rooted<JSString*> wasmStr(cx, JS_AtomizeString(cx, "wasm"));
  if (!wasmStr) {
    return false;
  }

  JS::Rooted<JSString*> kindStr(cx, JS_AtomizeString(cx, "kind"));
  if (!kindStr) {
    return false;
  }
  JS::Rooted<JS::PropertyKey> kindId(cx, JS::PropertyKey::NonIntAtom(kindStr));

  JS::Rooted<JSString*> implementationStr(
      cx, JS_AtomizeString(cx, "implementation"));
  if (!implementationStr) {
    return false;
  }
  JS::Rooted<JS::PropertyKey> implementationId(
      cx, JS::PropertyKey::NonIntAtom(implementationStr));

  JS::Rooted<JSString*> timeStr(cx, JS_AtomizeString(cx, "time"));
  if (!timeStr) {
    return false;
  }
  JS::Rooted<JS::PropertyKey> timeId(cx, JS::PropertyKey::NonIntAtom(timeStr));

  JS::Rooted<JSString*> eventsStr(cx, JS_AtomizeString(cx, "events"));
  if (!eventsStr) {
    return false;
  }
  JS::Rooted<JS::PropertyKey> eventsId(cx,
                                       JS::PropertyKey::NonIntAtom(eventsStr));

  JS::Rooted<JS::PropertyKey> lineNumberId(cx,
                                           NameToId(cx->names().lineNumber));
  JS::Rooted<JS::PropertyKey> columnNumberId(
      cx, NameToId(cx->names().columnNumber));
  JS::Rooted<JS::PropertyKey> scriptId(cx, NameToId(cx->names().script));
  JS::Rooted<JS::PropertyKey> nameId(cx, NameToId(cx->names().name));
  JS::Rooted<JS::PropertyKey> labelId(cx, NameToId(cx->names().label));
  JS::Rooted<JSString*> realmIDStr(cx, JS_AtomizeString(cx, "realmID"));
  if (!realmIDStr) {
    return false;
  }
  JS::Rooted<JS::PropertyKey> realmIDId(
      cx, JS::PropertyKey::NonIntAtom(realmIDStr));

  JS::Rooted<JSObject*> contextObj(cx);
  JS::Rooted<ArrayObject*> eventsArray(cx);
  JS::Rooted<JSObject*> eventObj(cx);
  JS::Rooted<JSString*> str(cx);

  JS::Rooted<ArrayObject*> traceArray(cx, NewDenseEmptyArray(cx));
  if (!traceArray) {
    return false;
  }

  for (const auto& context : trace.contexts) {
    contextObj = JS_NewPlainObject(cx);
    if (!contextObj) {
      return false;
    }

    eventsArray = NewDenseEmptyArray(cx);
    if (!eventsArray) {
      return false;
    }

    for (const auto& event : context.events) {
      eventObj = JS_NewPlainObject(cx);
      if (!eventObj) {
        return false;
      }

      switch (event.kind) {
        case JS::ExecutionTrace::EventKind::FunctionEnter:
          str = functionEnterStr;
          break;
        case JS::ExecutionTrace::EventKind::FunctionLeave:
          str = functionLeaveStr;
          break;
        case JS::ExecutionTrace::EventKind::LabelEnter:
          str = labelEnterStr;
          break;
        case JS::ExecutionTrace::EventKind::LabelLeave:
          str = labelLeaveStr;
          break;
        case JS::ExecutionTrace::EventKind::Error:
          str = errorStr;
          break;
        default:
          MOZ_CRASH("Unexpected EventKind");
          break;
      }
      if (!JS_DefinePropertyById(cx, eventObj, kindId, str, JSPROP_ENUMERATE)) {
        return false;
      }

      if (event.kind == JS::ExecutionTrace::EventKind::FunctionEnter ||
          event.kind == JS::ExecutionTrace::EventKind::FunctionLeave) {
        switch (event.functionEvent.implementation) {
          case JS::ExecutionTrace::ImplementationType::Interpreter:
            str = interpreterStr;
            break;
          case JS::ExecutionTrace::ImplementationType::Baseline:
            str = baselineStr;
            break;
          case JS::ExecutionTrace::ImplementationType::Ion:
            str = ionStr;
            break;
          case JS::ExecutionTrace::ImplementationType::Wasm:
            str = wasmStr;
            break;
          default:
            MOZ_CRASH("Unexpected ImplementationType");
            break;
        }
        if (!JS_DefinePropertyById(cx, eventObj, implementationId, str,
                                   JSPROP_ENUMERATE)) {
          return false;
        }

        if (!JS_DefinePropertyById(cx, eventObj, lineNumberId,
                                   event.functionEvent.lineNumber,
                                   JSPROP_ENUMERATE)) {
          return false;
        }

        if (!JS_DefinePropertyById(cx, eventObj, columnNumberId,
                                   event.functionEvent.column,
                                   JSPROP_ENUMERATE)) {
          return false;
        }

        if (auto p = context.scriptUrls.lookup(event.functionEvent.scriptId)) {
          str = JS_NewStringCopyUTF8Z(
              cx, JS::ConstUTF8CharsZ(trace.stringBuffer.begin() + p->value()));
          if (!str) {
            return false;
          }

          if (!JS_DefinePropertyById(cx, eventObj, scriptId, str,
                                     JSPROP_ENUMERATE)) {
            return false;
          }
        } else {
          if (!JS_DefinePropertyById(cx, eventObj, scriptId,
                                     JS::UndefinedHandleValue,
                                     JSPROP_ENUMERATE)) {
            return false;
          }
        }

        if (!JS_DefinePropertyById(
                cx, eventObj, realmIDId,
                // We are converting a uint64_t into double which is lossy. But
                // this is okay because Firefox makes sure to only use 53 bits
                // so it can be converted to and from a JS value without loss of
                // precision. Additionally we don't set the realmID in JS shell.
                double(event.functionEvent.realmID), JSPROP_ENUMERATE)) {
          return false;
        }

        if (auto p = context.atoms.lookup(event.functionEvent.functionNameId)) {
          str = JS_NewStringCopyUTF8Z(
              cx, JS::ConstUTF8CharsZ(trace.stringBuffer.begin() + p->value()));
          if (!str) {
            return false;
          }

          if (!JS_DefinePropertyById(cx, eventObj, nameId, str,
                                     JSPROP_ENUMERATE)) {
            return false;
          }
        } else {
          if (!JS_DefinePropertyById(cx, eventObj, nameId,
                                     JS::UndefinedHandleValue,
                                     JSPROP_ENUMERATE)) {
            return false;
          }
        }
      } else if (event.kind == JS::ExecutionTrace::EventKind::LabelEnter ||
                 event.kind == JS::ExecutionTrace::EventKind::LabelLeave) {
        str = JS_NewStringCopyUTF8Z(
            cx, JS::ConstUTF8CharsZ(trace.stringBuffer.begin() +
                                    event.labelEvent.label));
        if (!str) {
          return false;
        }

        if (!JS_DefinePropertyById(cx, eventObj, labelId, str,
                                   JSPROP_ENUMERATE)) {
          return false;
        }
      }

      if (event.kind != JS::ExecutionTrace::EventKind::Error) {
        if (!JS_DefinePropertyById(cx, eventObj, timeId, event.time,
                                   JSPROP_ENUMERATE)) {
          return false;
        }
      }

      if (!NewbornArrayPush(cx, eventsArray, JS::ObjectValue(*eventObj))) {
        return false;
      }
    }

    if (!JS_DefinePropertyById(cx, contextObj, eventsId, eventsArray,
                               JSPROP_ENUMERATE)) {
      return false;
    }

    if (!NewbornArrayPush(cx, traceArray, JS::ObjectValue(*contextObj))) {
      return false;
    }
  }

  args.rval().setObject(*traceArray);
  return true;
}

static bool DisableExecutionTracing(JSContext* cx, unsigned argc,
                                    JS::Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!JS_TracerEndTracing(cx)) {
    return false;
  }

  args.rval().setUndefined();
  return true;
}

#endif  // MOZ_EXECUTION_TRACING

// clang-format off
static const JSFunctionSpecWithHelp shell_functions[] = {
    JS_FN_HELP("options", Options, 0, 0,
"options([option ...])",
"  Get or toggle JavaScript options."),

    JS_FN_HELP("load", Load, 1, 0,
"load(['foo.js' ...])",
"  Load files named by string arguments. Filename is relative to the\n"
"      current working directory."),

    JS_FN_HELP("loadRelativeToScript", LoadScriptRelativeToScript, 1, 0,
"loadRelativeToScript(['foo.js' ...])",
"  Load files named by string arguments. Filename is relative to the\n"
"      calling script."),

    JS_FN_HELP("evaluate", Evaluate, 2, 0,
"evaluate(code[, options])",
"  Evaluate code as though it were the contents of a file.\n"
"  options is an optional object that may have these properties:\n"
"      isRunOnce: use the isRunOnce compiler option (default: false)\n"
"      noScriptRval: use the no-script-rval compiler option (default: false)\n"
"      fileName: filename for error messages and debug info\n"
"      skipFileNameValidation: skip the filename-validation callback\n"
"      lineNumber: starting line number for error messages and debug info\n"
"      columnNumber: starting column number for error messages and debug info\n"
"      global: global in which to execute the code\n"
"      newContext: if true, create and use a new cx (default: false)\n"
"      catchTermination: if true, catch termination (failure without\n"
"         an exception value, as for slow scripts or out-of-memory)\n"
"         and return 'terminated'\n"
"      element: if present with value |v|, convert |v| to an object |o| and\n"
"         mark the source as being attached to the DOM element |o|. If the\n"
"         property is omitted or |v| is null, don't attribute the source to\n"
"         any DOM element.\n"
"      elementAttributeName: if present and not undefined, the name of\n"
"         property of 'element' that holds this code. This is what\n"
"         Debugger.Source.prototype.elementAttributeName returns.\n"
"      sourceMapURL: if present with value |v|, convert |v| to a string, and\n"
"         provide that as the code's source map URL. If omitted, attach no\n"
"         source map URL to the code (although the code may provide one itself,\n"
"         via a //#sourceMappingURL comment).\n"
"      sourceIsLazy: if present and true, indicates that, after compilation, \n"
"          script source should not be cached by the JS engine and should be \n"
"          lazily loaded from the embedding as-needed.\n"
"      forceFullParse: if present and true, disable syntax-parse.\n"
"      loadBytecode: if true, and if the source is a CacheEntryObject,\n"
"         the bytecode would be loaded and decoded from the cache entry instead\n"
"         of being parsed, then it would be executed as usual.\n"
"      saveBytecodeWithDelazifications: if true, and if the source is a\n"
"         CacheEntryObject, the delazifications are collected during the\n"
"         execution, and encoded after that, and saved into the cache entry.\n"
"      execute: if false, do not execute the script, but do parse and/or\n"
"               transcode.\n"
"      assertEqBytecode: if true, and if both loadBytecode and either\n"
"         saveBytecodeWithDelazifications is true, then the loaded\n"
"         bytecode and the encoded bytecode are compared.\n"
"         and an assertion is raised if they differ.\n"
"      envChainObject: object to put on the scope chain, with its fields added\n"
"         as var bindings, akin to how elements are added to the environment in\n"
"         event handlers in Gecko.\n"
"      supportUnscopables: if true, support Symbol.unscopables lookups for\n"
"         envChainObject, similar to (syntactic) with-statements.\n"
),

    JS_FN_HELP("run", Run, 1, 0,
"run('foo.js')",
"  Run the file named by the first argument, returning the number of\n"
"  of milliseconds spent compiling and executing it."),

    JS_FN_HELP("readline", ReadLine, 0, 0,
"readline()",
"  Read a single line from stdin."),

    JS_FN_HELP("readlineBuf", ReadLineBuf, 1, 0,
"readlineBuf([ buf ])",
"  Emulate readline() on the specified string. The first call with a string\n"
"  argument sets the source buffer. Subsequent calls without an argument\n"
"  then read from this buffer line by line.\n"),

    JS_FN_HELP("print", Print, 0, 0,
"print([exp ...])",
"  Evaluate and print expressions to stdout."),

    JS_FN_HELP("printErr", PrintErr, 0, 0,
"printErr([exp ...])",
"  Evaluate and print expressions to stderr."),

    JS_FN_HELP("putstr", PutStr, 0, 0,
"putstr([exp])",
"  Evaluate and print expression without newline."),

    JS_FN_HELP("dateNow", Now, 0, 0,
"dateNow()",
"  Return the current time with sub-ms precision."),

    JS_FN_HELP("help", Help, 0, 0,
"help([function or interface object or /pattern/])",
"  Display usage and help messages."),

    JS_FN_HELP("quit", Quit, 0, 0,
"quit()",
"  Quit the shell."),

    JS_FN_HELP("assertEq", AssertEq, 2, 0,
"assertEq(actual, expected[, msg])",
"  Throw if the first two arguments are not the same (both +0 or both -0,\n"
"  both NaN, or non-zero and ===)."),

    JS_FN_HELP("startTimingMutator", StartTimingMutator, 0, 0,
"startTimingMutator()",
"  Start accounting time to mutator vs GC."),

    JS_FN_HELP("stopTimingMutator", StopTimingMutator, 0, 0,
"stopTimingMutator()",
"  Stop accounting time to mutator vs GC and dump the results."),

    JS_FN_HELP("throwError", ThrowError, 0, 0,
"throwError()",
"  Throw an error from JS_ReportError."),

    JS_FN_HELP("createErrorReport", CreateErrorReport, 1, 0,
"createErrorReport(value)",
"  Create an JS::ErrorReportBuilder object from the given value and serialize\n"
"  to an object."),

#if defined(DEBUG) || defined(JS_JITSPEW)
    JS_FN_HELP("disassemble", DisassembleToString, 1, 0,
"disassemble([fun/code])",
"  Return the disassembly for the given function or code.\n"
"  All disassembly functions take these options as leading string arguments:\n"
"    \"-r\" (disassemble recursively)\n"
"    \"-l\" (show line numbers)\n"
"    \"-S\" (omit source notes)"),

    JS_FN_HELP("dis", Disassemble, 1, 0,
"dis([fun/code])",
"  Disassemble functions into bytecodes."),

    JS_FN_HELP("disfile", DisassFile, 1, 0,
"disfile('foo.js')",
"  Disassemble script file into bytecodes.\n"),

    JS_FN_HELP("dissrc", DisassWithSrc, 1, 0,
"dissrc([fun/code])",
"  Disassemble functions with source lines."),

    JS_FN_HELP("notes", Notes, 1, 0,
"notes([fun])",
"  Show source notes for functions."),

    JS_FN_HELP("stackDump", StackDump, 3, 0,
"stackDump(showArgs, showLocals, showThisProps)",
"  Tries to print a lot of information about the current stack. \n"
"  Similar to the DumpJSStack() function in the browser."),

#endif

    JS_FN_HELP("getslx", GetSLX, 1, 0,
"getslx(obj)",
"  Get script line extent."),

    JS_FN_HELP("evalcx", EvalInContext, 1, 0,
"evalcx(s[, o])",
"  Evaluate s in optional sandbox object o.\n"
"  if (s == '' && !o) return new o with eager standard classes\n"
"  if (s == 'lazy' && !o) return new o with lazy standard classes"),

    JS_FN_HELP("evalInWorker", EvalInWorker, 1, 0,
"evalInWorker(str)",
"  Evaluate 'str' in a separate thread with its own runtime.\n"),

    JS_FN_HELP("getSharedObject", GetSharedObject, 0, 0,
"getSharedObject()",
"  Retrieve the shared object from the cross-worker mailbox.\n"
"  The object retrieved may not be identical to the object that was\n"
"  installed, but it references the same shared memory.\n"
"  getSharedObject performs an ordering memory barrier.\n"),

    JS_FN_HELP("setSharedObject", SetSharedObject, 0, 0,
"setSharedObject(obj)",
"  Install the shared object in the cross-worker mailbox.  The object\n"
"  may be null.  setSharedObject performs an ordering memory barrier.\n"),

    JS_FN_HELP("getSharedArrayBuffer", GetSharedObject, 0, 0,
"getSharedArrayBuffer()",
"  Obsolete alias for getSharedObject().\n"),

    JS_FN_HELP("setSharedArrayBuffer", SetSharedObject, 0, 0,
"setSharedArrayBuffer(obj)",
"  Obsolete alias for setSharedObject(obj).\n"),

    JS_FN_HELP("shapeOf", ShapeOf, 1, 0,
"shapeOf(obj)",
"  Get the shape of obj (an implementation detail)."),

#ifdef DEBUG
    JS_FN_HELP("arrayInfo", ArrayInfo, 1, 0,
"arrayInfo(a1, a2, ...)",
"  Report statistics about arrays."),
#endif

    JS_FN_HELP("sleep", Sleep_fn, 1, 0,
"sleep(dt)",
"  Sleep for dt seconds."),

    JS_FN_HELP("parseModule", ParseModule, 3, 0,
"parseModule(code, 'filename', 'js' | 'json')",
"  Parses source text as a JS module ('js', this is the default) or a JSON"
" module ('json') and returns a ModuleObject wrapper object."),

    JS_FN_HELP("instantiateModuleStencil", InstantiateModuleStencil, 1, 0,
"instantiateModuleStencil(stencil, [options])",
"  Instantiates the given stencil as module, and return the module object."),

    JS_FN_HELP("instantiateModuleStencilXDR", InstantiateModuleStencilXDR, 1, 0,
"instantiateModuleStencilXDR(stencil, [options])",
"  Reads the given stencil XDR object, instantiates the stencil as module, and"
"  return the module object."),

    JS_FN_HELP("registerModule", RegisterModule, 2, 0,
"registerModule(specifier, module)",
"  Register a module with the module loader, so that subsequent import from\n"
"  |specifier| will resolve to |module|.  Returns |module|."),

    JS_FN_HELP("clearModules", ClearModules, 0, 0,
"clearModules()",
"  Clear knowledge of all loaded modules."),

    JS_FN_HELP("moduleLink", ModuleLink, 1, 0,
"moduleLink(moduleOjbect)",
"  Link a module graph, performing the spec's Link method."),

    JS_FN_HELP("moduleEvaluate", ModuleEvaluate, 1, 0,
"moduleEvaluate(moduleOjbect)",
"  Evaluate a module graph, performing the spec's Evaluate method."),

    JS_FN_HELP("getModuleEnvironmentNames", GetModuleEnvironmentNames, 1, 0,
"getModuleEnvironmentNames(module)",
"  Get the list of a module environment's bound names for a specified module.\n"),

    JS_FN_HELP("getModuleEnvironmentValue", GetModuleEnvironmentValue, 2, 0,
"getModuleEnvironmentValue(module, name)",
"  Get the value of a bound name in a module environment.\n"),

    JS_FN_HELP("dumpStencil", DumpStencil, 1, 0,
"dumpStencil(code, [options])",
"  Parses a string and returns string that represents stencil.\n"
"  If present, |options| may have properties saying how the code should be\n"
"  compiled:\n"
"      module: if present and true, compile the source as module.\n"
"  CompileOptions-related properties of evaluate function's option can also\n"
"  be used."),

    JS_FN_HELP("parse", Parse, 1, 0,
"parse(code, [options])",
"  Parses a string, potentially throwing. If present, |options| may\n"
"  have properties saying how the code should be compiled:\n"
"      module: if present and true, compile the source as module.\n"
"  CompileOptions-related properties of evaluate function's option can also\n"
"  be used. except forceFullParse. This function always use full parse."),

    JS_FN_HELP("syntaxParse", SyntaxParse, 1, 0,
"syntaxParse(code)",
"  Check the syntax of a string, returning success value"),

    JS_FN_HELP("offThreadCompileModuleToStencil", OffThreadCompileModuleToStencil, 1, 0,
"offThreadCompileModuleToStencil(code[, options])",
"  Compile |code| on a helper thread, returning a job ID. To wait for the\n"
"  compilation to finish and and get the module stencil object call\n"
"  |finishOffThreadStencil| passing the job ID."),

    JS_FN_HELP("offThreadDecodeStencil", OffThreadDecodeStencil, 1, 0,
"offThreadDecodeStencil(cacheEntry[, options])",
"  Decode |code| on a helper thread, returning a job ID. To wait for the\n"
"  decoding to finish and run the code, call |finishOffThreadStencil| passing\n"
"  the job ID. If present, |options| may have properties saying how the code\n"
"  should be compiled (see also offThreadCompileToStencil)."),

    JS_FN_HELP("offThreadCompileToStencil", OffThreadCompileToStencil, 1, 0,
"offThreadCompileToStencil(code[, options])",
"  Compile |code| on a helper thread, returning a job ID. To wait for the\n"
"  compilation to finish and get the stencil object, call\n"
"  |finishOffThreadStencil| passing the job ID.  If present, \n"
"  |options| may have properties saying how the code should be compiled:\n"
"      noScriptRval: use the no-script-rval compiler option (default: false)\n"
"      fileName: filename for error messages and debug info\n"
"      lineNumber: starting line number for error messages and debug info\n"
"      columnNumber: starting column number for error messages and debug info\n"
"      element: if present with value |v|, convert |v| to an object |o| and\n"
"         mark the source as being attached to the DOM element |o|. If the\n"
"         property is omitted or |v| is null, don't attribute the source to\n"
"         any DOM element.\n"
"      elementAttributeName: if present and not undefined, the name of\n"
"         property of 'element' that holds this code. This is what\n"
"         Debugger.Source.prototype.elementAttributeName returns."),

    JS_FN_HELP("finishOffThreadStencil", FinishOffThreadStencil, 0, 0,
"finishOffThreadStencil([jobID])",
"  Wait for an off-thread compilation or decode job to complete. The job ID\n"
"  can be ommitted if there is only one job pending. If an error occurred,\n"
"  throw the appropriate exception; otherwise, return the stencil object,"
"  that can be passed to |evalStencil|."),

    JS_FN_HELP("timeout", Timeout, 1, 0,
"timeout([seconds], [func])",
"  Get/Set the limit in seconds for the execution time for the current context.\n"
"  When the timeout expires the current interrupt callback is invoked.\n"
"  The timeout is used just once.  If the callback returns a falsy value, the\n"
"  script is aborted.  A negative value for seconds (this is the default) cancels\n"
"  any pending timeout.\n"
"  If a second argument is provided, it is installed as the interrupt handler,\n"
"  exactly as if by |setInterruptCallback|.\n"),

    JS_FN_HELP("interruptIf", InterruptIf, 1, 0,
"interruptIf(cond)",
"  Requests interrupt callback if cond is true. If a callback function is set via\n"
"  |timeout| or |setInterruptCallback|, it will be called. No-op otherwise."),

    JS_FN_HELP("invokeInterruptCallback", InvokeInterruptCallbackWrapper, 0, 0,
"invokeInterruptCallback(fun)",
"  Forcefully set the interrupt flag and invoke the interrupt handler. If a\n"
"  callback function is set via |timeout| or |setInterruptCallback|, it will\n"
"  be called. Before returning, fun is called with the return value of the\n"
"  interrupt handler."),

    JS_FN_HELP("setInterruptCallback", SetInterruptCallback, 1, 0,
"setInterruptCallback(func)",
"  Sets func as the interrupt callback function.\n"
"  Calling this function will replace any callback set by |timeout|.\n"
"  If the callback returns a falsy value, the script is aborted.\n"),

    JS_FN_HELP("setPrefValue", SetPrefValue, 2, 0,
"setPrefValue(name, value)",
"  Set the value of the JS pref with the given name."),

    JS_FN_HELP("setJitCompilerOption", SetJitCompilerOption, 2, 0,
"setJitCompilerOption(<option>, <number>)",
"  Set a compiler option indexed in JSCompileOption enum to a number.\n"),

#ifdef DEBUG
    JS_FN_HELP("interruptRegexp", InterruptRegexp, 2, 0,
"interruptRegexp(<regexp>, <string>)",
"  Interrrupt the execution of regular expression.\n"),
#endif
    JS_FN_HELP("checkRegExpSyntax", CheckRegExpSyntax, 1, 0,
"checkRegExpSyntax(<string>)",
"  Return undefined if the string parses as a RegExp. If the string does not\n"
"  parse correctly, return the SyntaxError that occurred."),

    JS_FN_HELP("enableLastWarning", EnableLastWarning, 0, 0,
"enableLastWarning()",
"  Enable storing the last warning."),
    JS_FN_HELP("disableLastWarning", DisableLastWarning, 0, 0,
"disableLastWarning()",
"  Disable storing the last warning."),

    JS_FN_HELP("getLastWarning", GetLastWarning, 0, 0,
"getLastWarning()",
"  Returns an object that represents the last warning."),

    JS_FN_HELP("clearLastWarning", ClearLastWarning, 0, 0,
"clearLastWarning()",
"  Clear the last warning."),

    JS_FN_HELP("elapsed", Elapsed, 0, 0,
"elapsed()",
"  Execution time elapsed for the current thread."),

    JS_FN_HELP("decompileFunction", DecompileFunction, 1, 0,
"decompileFunction(func)",
"  Decompile a function."),

    JS_FN_HELP("decompileThis", DecompileThisScript, 0, 0,
"decompileThis()",
"  Decompile the currently executing script."),

    JS_FN_HELP("valueToSource", ValueToSource, 1, 0,
"valueToSource(value)",
"  Format a value for inspection."),

    JS_FN_HELP("thisFilename", ThisFilename, 0, 0,
"thisFilename()",
"  Return the filename of the current script"),

    JS_FN_HELP("newGlobal", NewGlobal, 1, 0,
"newGlobal([options])",
"  Return a new global object/realm. The new global is created in the\n"
"  'newGlobal' function object's compartment and zone, unless the\n"
"  '--more-compartments' command-line flag was given, in which case new\n"
"  globals get a fresh compartment and zone. If options is given, it may\n"
"  have any of the following properties:\n"
"      sameCompartmentAs: If an object, the global will be in the same\n"
"         compartment and zone as the given object.\n"
"      sameZoneAs: The global will be in a new compartment in the same zone\n"
"         as the given object.\n"
"      newCompartment: If true, the global will always be created in a new\n"
"         compartment and zone.\n"
"      invisibleToDebugger: If true, the global will be invisible to the\n"
"         debugger (default false)\n"
"      discardSource: If true, discard source after compiling a script\n"
"         (default false).\n"
"      useWindowProxy: the global will be created with a WindowProxy attached. In this\n"
"          case, the WindowProxy will be returned.\n"
"      freezeBuiltins: certain builtin constructors will be frozen when created and\n"
"          their prototypes will be sealed. These constructors will be defined on the\n"
"          global as non-configurable and non-writable.\n"
"      immutablePrototype: whether the global's prototype is immutable.\n"
"      principal: if present, its value converted to a number must be an\n"
"         integer that fits in 32 bits; use that as the new realm's\n"
"         principal. Shell principals are toys, meant only for testing; one\n"
"         shell principal subsumes another if its set bits are a superset of\n"
"         the other's. Thus, a principal of 0 subsumes nothing, while a\n"
"         principals of ~0 subsumes all other principals. The absence of a\n"
"         principal is treated as if its bits were 0xffff, for subsumption\n"
"         purposes. If this property is omitted, supply no principal.\n"
"      systemPrincipal: If true, use the shell's trusted principals for the\n"
"         new realm. This creates a realm that's marked as a 'system' realm."),

    JS_FN_HELP("nukeAllCCWs", NukeAllCCWs, 0, 0,
"nukeAllCCWs()",
"  Like nukeCCW, but for all CrossCompartmentWrappers targeting the current realm."),

    JS_FN_HELP("recomputeWrappers", RecomputeWrappers, 2, 0,
"recomputeWrappers([src, [target]])",
"  Recompute all cross-compartment wrappers. src and target are both optional\n"
"  and can be used to filter source or target compartments: the unwrapped\n"
"  object's compartment is used as CompartmentFilter.\n"),

    JS_FN_HELP("dumpObjectWrappers", DumpObjectWrappers, 2, 0,
"dumpObjectWrappers()",
"  Print information about cross-compartment object wrappers.\n"),

    JS_FN_HELP("wrapWithProto", WrapWithProto, 2, 0,
"wrapWithProto(obj)",
"  Wrap an object into a noop wrapper with prototype semantics."),

    JS_FN_HELP("createExternalArrayBuffer", CreateExternalArrayBuffer, 1, 0,
"createExternalArrayBuffer(size)",
"  Create an array buffer that has external data of size."),

    JS_FN_HELP("createMappedArrayBuffer", CreateMappedArrayBuffer, 1, 0,
"createMappedArrayBuffer(filename, [offset, [size]])",
"  Create an array buffer that mmaps the given file."),

JS_FN_HELP("createUserArrayBuffer", CreateUserArrayBuffer, 1, 0,
"createUserArrayBuffer(size)",
"  Create an array buffer that uses user-controlled memory."),

    JS_FN_HELP("addPromiseReactions", AddPromiseReactions, 3, 0,
"addPromiseReactions(promise, onResolve, onReject)",
"  Calls the JS::AddPromiseReactions JSAPI function with the given arguments."),

    JS_FN_HELP("ignoreUnhandledRejections", IgnoreUnhandledRejections, 0, 0,
"ignoreUnhandledRejections()",
"  By default, js shell tracks unhandled promise rejections and reports\n"
"  them at the end of the exectuion.  If a testcase isn't interested\n"
"  in those rejections, call this to stop tracking and reporting."),

    JS_FN_HELP("getMaxArgs", GetMaxArgs, 0, 0,
"getMaxArgs()",
"  Return the maximum number of supported args for a call."),

    JS_FN_HELP("createIsHTMLDDA", CreateIsHTMLDDA, 0, 0,
"createIsHTMLDDA()",
"  Return an object |obj| that \"looks like\" the |document.all| object in\n"
"  browsers in certain ways: |typeof obj === \"undefined\"|, |obj == null|\n"
"  and |obj == undefined| (vice versa for !=), |ToBoolean(obj) === false|,\n"
"  and when called with no arguments or the single argument \"\" returns\n"
"  null.  (Calling |obj| any other way crashes or throws an exception.)\n"
"  This function implements the exact requirements of the $262.IsHTMLDDA\n"
"  property in test262."),

    JS_FN_HELP("cacheEntry", CacheEntry, 1, 0,
"cacheEntry(code)",
"  Return a new opaque object which emulates a cache entry of a script.  This\n"
"  object encapsulates the code and its cached content. The cache entry is filled\n"
"  and read by the \"evaluate\" function by using it in place of the source, and\n"
"  by setting \"saveBytecodeWithDelazifications\" and \"loadBytecode\" options."),

    JS_FN_HELP("streamCacheEntry", StreamCacheEntryObject::construct, 1, 0,
"streamCacheEntry(buffer)",
"  Create a shell-only object that holds wasm bytecode and can be streaming-\n"
"  compiled and cached by WebAssembly.{compile,instantiate}Streaming(). On a\n"
"  second compilation of the same cache entry, the cached code will be used."),

    JS_FN_HELP("printProfilerEvents", PrintProfilerEvents, 0, 0,
"printProfilerEvents()",
"  Register a callback with the profiler that prints javascript profiler events\n"
"  to stderr.  Callback is only registered if profiling is enabled."),

    JS_FN_HELP("enableSingleStepProfiling", EnableSingleStepProfiling, 0, 0,
"enableSingleStepProfiling()",
"  This function will fail on platforms that don't support single-step profiling\n"
"  (currently ARM and MIPS64 support it). When enabled, at every instruction a\n"
"  backtrace will be recorded and stored in an array. Adjacent duplicate backtraces\n"
"  are discarded."),

    JS_FN_HELP("disableSingleStepProfiling", DisableSingleStepProfiling, 0, 0,
"disableSingleStepProfiling()",
"  Return the array of backtraces recorded by enableSingleStepProfiling."),

    JS_FN_HELP("enableGeckoProfiling", EnableGeckoProfiling, 0, 0,
"enableGeckoProfiling()",
"  Enables Gecko Profiler instrumentation and corresponding assertions, with slow\n"
"  assertions disabled.\n"),

    JS_FN_HELP("enableGeckoProfilingWithSlowAssertions", EnableGeckoProfilingWithSlowAssertions, 0, 0,
"enableGeckoProfilingWithSlowAssertions()",
"  Enables Gecko Profiler instrumentation and corresponding assertions, with slow\n"
"  assertions enabled.\n"),

    JS_FN_HELP("disableGeckoProfiling", DisableGeckoProfiling, 0, 0,
"disableGeckoProfiling()",
"  Disables Gecko Profiler instrumentation"),

    JS_FN_HELP("isLatin1", IsLatin1, 1, 0,
"isLatin1(s)",
"  Return true iff the string's characters are stored as Latin1."),

    JS_FN_HELP("stackPointerInfo", StackPointerInfo, 0, 0,
"stackPointerInfo()",
"  Return an int32 value which corresponds to the offset of the latest stack\n"
"  pointer, such that one can take the differences of 2 to estimate a frame-size."),

    JS_FN_HELP("enqueueJob", EnqueueJob, 1, 0,
"enqueueJob(fn)",
"  Enqueue 'fn' on the shell's job queue."),

    JS_FN_HELP("globalOfFirstJobInQueue", GlobalOfFirstJobInQueue, 0, 0,
"globalOfFirstJobInQueue()",
"  Returns the global of the first item in the job queue. Throws an exception\n"
"  if the queue is empty.\n"),

    JS_FN_HELP("drainJobQueue", DrainJobQueue, 0, 0,
"drainJobQueue()",
"Take jobs from the shell's job queue in FIFO order and run them until the\n"
"queue is empty.\n"),

    JS_FN_HELP("setPromiseRejectionTrackerCallback", SetPromiseRejectionTrackerCallback, 1, 0,
"setPromiseRejectionTrackerCallback()",
"Sets the callback to be invoked whenever a Promise rejection is unhandled\n"
"or a previously-unhandled rejection becomes handled."),

    JS_FN_HELP("dumpScopeChain", DumpScopeChain, 1, 0,
"dumpScopeChain(obj)",
"  Prints the scope chain of an interpreted function or a module."),

    JS_FN_HELP("blackRoot", EnsureBlackRoot, 0, 0,
"blackRoot()",
"  Return an array in the current compartment whose elements will be marked\n"
"  as black roots by the GC."),

    JS_FN_HELP("grayRoot", EnsureGrayRoot, 0, 0,
"grayRoot()",
"  Return an array in the current compartment whose elements will be marked\n"
"  as gray roots by the GC."),

    JS_FN_HELP("addMarkObservers", AddMarkObservers, 1, 0,
"addMarkObservers(array_of_objects)",
"  Register an array of objects whose mark bits will be tested by calls to\n"
"  getMarks. The objects will be in calling compartment. Objects from\n"
"  multiple compartments may be monitored by calling this function in\n"
"  different compartments."),

    JS_FN_HELP("clearMarkObservers", ClearMarkObservers, 1, 0,
"clearMarkObservers()",
"  Clear out the list of objects whose mark bits will be tested.\n"),

    JS_FN_HELP("getMarks", GetMarks, 0, 0,
"getMarks()",
"  Return an array of strings representing the current state of the mark\n"
"  bits ('gray' or 'black', or 'dead' if the object has been collected)\n"
"  for the objects registered via addMarkObservers. Note that some of the\n"
"  objects tested may be from different compartments than the one in which\n"
"  this function runs."),

    JS_FN_HELP("bindToAsyncStack", BindToAsyncStack, 2, 0,
"bindToAsyncStack(fn, { stack, cause, explicit })",
"  Returns a new function that calls 'fn' with no arguments, passing\n"
"  'undefined' as the 'this' value, and supplies an async stack for the\n"
"  call as described by the second argument, an object with the following\n"
"  properties (which are not optional, unless specified otherwise):\n"
"\n"
"  stack:    A SavedFrame object, like that returned by 'saveStack'. Stacks\n"
"            captured during calls to the returned function capture this as\n"
"            their async stack parent, accessible via a SavedFrame's\n"
"            'asyncParent' property.\n"
"\n"
"  cause:    A string, supplied as the async cause on the top frame of\n"
"            captured async stacks.\n"
"\n"
"  explicit: A boolean value, indicating whether the given 'stack' should\n"
"            always supplant the returned function's true callers (true),\n"
"            or only when there are no other JavaScript frames on the stack\n"
"            below it (false). If omitted, this is treated as 'true'."),

#ifndef __wasi__
    JS_FN_HELP("wasmCompileInSeparateProcess", WasmCompileInSeparateProcess, 1, 0,
"wasmCompileInSeparateProcess(buffer)",
"  Compile the given buffer in a separate process, serialize the resulting\n"
"  wasm::Module into bytes, and deserialize those bytes in the current\n"
"  process, returning the resulting WebAssembly.Module."),

    JS_FN_HELP("wasmTextToBinary", WasmTextToBinary, 1, 0,
"wasmTextToBinary(str)",
"  Translates the given text wasm module into its binary encoding."),
#endif // __wasi__

    JS_FN_HELP("transplantableObject", TransplantableObject, 0, 0,
"transplantableObject([options])",
"  Returns the pair {object, transplant}. |object| is an object which can be\n"
"  transplanted into a new object when the |transplant| function, which must\n"
"  be invoked with a global object, is called.\n"
"  |object| is swapped with a cross-compartment wrapper if the global object\n"
"  is in a different compartment.\n"
"\n"
"  If options is given, it may have any of the following properties:\n"
"    proxy: Create a DOM Proxy object instead of a plain DOM object.\n"
"    object: Don't create a new DOM object, but instead use the supplied\n"
"            FakeDOMObject."),

    JS_FN_HELP("cpuNow", CpuNow, /* nargs= */ 0, /* flags = */ 0,
"cpuNow()",
" Returns the approximate processor time used by the process since an arbitrary epoch, in seconds.\n"
" Only the difference between two calls to `cpuNow()` is meaningful."),

#ifdef FUZZING_JS_FUZZILLI
    JS_FN_HELP("fuzzilli", Fuzzilli, 0, 0,
"fuzzilli(operation, arg)",
"  Exposes functionality used by the Fuzzilli JavaScript fuzzer."),
#endif

#ifdef FUZZING_INTERFACES
    JS_FN_HELP("getWasmSmithModule", GetWasmSmithModule, 1, 0,
"getWasmSmithModule(arrayBuffer)",
"  Call wasm-smith to generate a random wasm module from the provided data."),
#endif

    JS_FN_HELP("isValidJSON", IsValidJSON, 1, 0,
"isValidJSON(source)",
" Returns true if the given source is valid JSON."),

    JS_FN_HELP("compressLZ4", CompressLZ4, 1, 0,
"compressLZ4(bytes)",
" Return a compressed copy of bytes using LZ4."),

    JS_FN_HELP("decompressLZ4", DecompressLZ4, 1, 0,
"decompressLZ4(bytes)",
" Return a decompressed copy of bytes using LZ4."),

    JS_FN_HELP("createSideEffectfulResolveObject", CreateSideEffectfulResolveObject, 0, 0,
"createSideEffectfulResolveObject()",
" Return an object with a property 'obj.test == 42', backed by a resolve hook "
" with the Debugger shouldAvoidSideEffects flag integration."),

  JS_FN_HELP("getUseCounterResults", GetUseCounterResults, 0, 0,
"getUseCounterResults()",
" Return the values of the shell use counters."),

    JS_FS_HELP_END
};
// clang-format on

// clang-format off
#ifdef FUZZING_JS_FUZZILLI
static const JSFunctionSpec shell_function_fuzzilli_hash[] = {
    JS_INLINABLE_FN("fuzzilli_hash", fuzzilli_hash, 1, 0, FuzzilliHash),
    JS_FS_END,
};
#endif
// clang-format on

// clang-format off
static const JSFunctionSpecWithHelp diff_testing_unsafe_functions[] = {

    JS_FS_HELP_END
};
// clang-format on

// clang-format off
static const JSFunctionSpecWithHelp fuzzing_unsafe_functions[] = {
    JS_FN_HELP("getSelfHostedValue", GetSelfHostedValue, 1, 0,
"getSelfHostedValue()",
"  Get a self-hosted value by its name. Note that these values don't get \n"
"  cached, so repeatedly getting the same value creates multiple distinct clones."),

    JS_FN_HELP("line2pc", LineToPC, 0, 0,
"line2pc([fun,] line)",
"  Map line number to PC."),

    JS_FN_HELP("pc2line", PCToLine, 0, 0,
"pc2line(fun[, pc])",
"  Map PC to line number."),

    JS_INLINABLE_FN_HELP("assertFloat32", testingFunc_assertFloat32, 2, 0, TestAssertFloat32,
"assertFloat32(value, isFloat32)",
"  In IonMonkey only, asserts that value has (resp. hasn't) the MIRType::Float32 if isFloat32 is true (resp. false)."),

    JS_INLINABLE_FN_HELP("assertRecoveredOnBailout", testingFunc_assertRecoveredOnBailout, 2, 0,
TestAssertRecoveredOnBailout,
"assertRecoveredOnBailout(var)",
"  In IonMonkey only, asserts that variable has RecoveredOnBailout flag."),

    JS_FN_HELP("withSourceHook", WithSourceHook, 1, 0,
"withSourceHook(hook, fun)",
"  Set this JS runtime's lazy source retrieval hook (that is, the hook\n"
"  used to find sources compiled with |CompileOptions::LAZY_SOURCE|) to\n"
"  |hook|; call |fun| with no arguments; and then restore the runtime's\n"
"  original hook. Return or throw whatever |fun| did. |hook| gets\n"
"  passed the requested code's URL, and should return a string.\n"
"\n"
"  Notes:\n"
"\n"
"  1) SpiderMonkey may assert if the returned code isn't close enough\n"
"  to the script's real code, so this function is not fuzzer-safe.\n"
"\n"
"  2) The runtime can have only one source retrieval hook active at a\n"
"  time. If |fun| is not careful, |hook| could be asked to retrieve the\n"
"  source code for compilations that occurred long before it was set,\n"
"  and that it knows nothing about. The reverse applies as well: the\n"
"  original hook, that we reinstate after the call to |fun| completes,\n"
"  might be asked for the source code of compilations that |fun|\n"
"  performed, and which, presumably, only |hook| knows how to find.\n"),

    JS_FN_HELP("crash", Crash, 0, 0,
"crash([message, [{disable_minidump:true}]])",
"  Crashes the process with a MOZ_CRASH, optionally providing a message.\n"
"  An options object may be passed as the second argument. If the key\n"
"  'suppress_minidump' is set to true, then a minidump will not be\n"
"  generated by the crash (which only has an effect if the breakpad\n"
"  dumping library is loaded.)"),

#ifndef __wasi__
    JS_FN_HELP("wasmLoop", WasmLoop, 2, 0,
"wasmLoop(filename, imports)",
"  Performs an AFL-style persistent loop reading data from the given file and passing it\n"
"  to the 'wasmEval' function together with the specified imports object."),
#endif // __wasi__

    JS_FN_HELP("setBufferStreamParams", SetBufferStreamParams, 2, 0,
"setBufferStreamParams(delayMillis, chunkByteSize)",
"  Set the delay time (between calls to StreamConsumer::consumeChunk) and chunk\n"
"  size (in bytes)."),

#ifdef JS_CACHEIR_SPEW
  JS_FN_HELP("cacheIRHealthReport", CacheIRHealthReport, 0, 0,
"cacheIRHealthReport()",
"  Show health rating of CacheIR stubs."),
#endif

#ifdef DEBUG
  JS_FN_HELP("debugGetQueuedJobs", DebugGetQueuedJobs, 0, 0,
"debugGetQueuedJobs()",
"  Returns an array of queued jobs."),
#endif

#ifdef JS_HAS_INTL_API
  // One of the extras is AddMozDateTimeFormatConstructor, which is not fuzzing
  // safe, since it doesn't validate the custom format pattern.
  //
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=1887585#c1
    JS_FN_HELP("addIntlExtras", AddIntlExtras, 1, 0,
"addIntlExtras(obj)",
"Adds various not-yet-standardized Intl functions as properties on the\n"
"provided object (this should generally be Intl itself).  The added\n"
"functions and their behavior are experimental: don't depend upon them\n"
"unless you're willing to update your code if these experimental APIs change\n"
"underneath you."),
#endif // JS_HAS_INTL_API

#ifdef MOZ_EXECUTION_TRACING
    JS_FN_HELP("enableExecutionTracing", EnableExecutionTracing, 0, 0,
"enableExecutionTracing()",
"  Enable execution tracing for the current context."),

    JS_FN_HELP("getExecutionTrace", GetExecutionTrace, 0, 0,
"getExecutionTrace()",
"  Get the execution trace."),

    JS_FN_HELP("disableExecutionTracing", DisableExecutionTracing, 0, 0,
"disableExecutionTracing()",
"  Disable execution tracing for the current context."),
#endif   // MOZ_EXECUTION_TRACING

    JS_FN_HELP("startRecordingTelemetry", StartRecordingTelemetry, 1, 0,
"startRecordingTelemetry(probeName)",
"  Start recording telemetry samples for the specified probe."),

    JS_FN_HELP("stopRecordingTelemetry", StopRecordingTelemetry, 1, 0,
"stopRecordingTelemetry(probeName)",
"  Stop recording telemetry samples for the specified probe."),

    JS_FN_HELP("getTelemetrySamples", GetTelemetrySamples, 1, 0,
"getTelemetry(probeName)",
"  Return an array of recorded telemetry samples for the specified probe."),

    JS_FS_HELP_END
};
// clang-format on

// clang-format off
static const JSFunctionSpecWithHelp performance_functions[] = {
    JS_FN_HELP("now", Now, 0, 0,
"now()",
"  Return the current time with sub-ms precision.\n"
"  This function is an alias of the dateNow() function."),
    JS_FS_HELP_END
};
// clang-format on

// clang-format off
static const JSFunctionSpecWithHelp console_functions[] = {
    JS_FN_HELP("log", Print, 0, 0,
"log([exp ...])",
"  Evaluate and print expressions to stdout.\n"
"  This function is an alias of the print() function."),
    JS_FS_HELP_END
};
// clang-format on

bool DefineConsole(JSContext* cx, HandleObject global) {
  RootedObject obj(cx, JS_NewPlainObject(cx));
  return obj && JS_DefineFunctionsWithHelp(cx, obj, console_functions) &&
         JS_DefineProperty(cx, global, "console", obj, 0);
}

#ifdef MOZ_PROFILING
#  define PROFILING_FUNCTION_COUNT 5
#  ifdef MOZ_CALLGRIND
#    define CALLGRIND_FUNCTION_COUNT 3
#  else
#    define CALLGRIND_FUNCTION_COUNT 0
#  endif
#  ifdef MOZ_VTUNE
#    define VTUNE_FUNCTION_COUNT 4
#  else
#    define VTUNE_FUNCTION_COUNT 0
#  endif
#  define EXTERNAL_FUNCTION_COUNT \
    (PROFILING_FUNCTION_COUNT + CALLGRIND_FUNCTION_COUNT + VTUNE_FUNCTION_COUNT)
#else
#  define EXTERNAL_FUNCTION_COUNT 0
#endif

#undef PROFILING_FUNCTION_COUNT
#undef CALLGRIND_FUNCTION_COUNT
#undef VTUNE_FUNCTION_COUNT
#undef EXTERNAL_FUNCTION_COUNT

static bool PrintHelpString(JSContext* cx, HandleValue v) {
  RootedString str(cx, v.toString());
  MOZ_ASSERT(gOutFile->isOpen());

  UniqueChars bytes = JS_EncodeStringToUTF8(cx, str);
  if (!bytes) {
    return false;
  }

  fprintf(gOutFile->fp, "%s\n", bytes.get());
  return true;
}

static bool PrintHelp(JSContext* cx, HandleObject obj) {
  RootedValue usage(cx);
  if (!JS_GetProperty(cx, obj, "usage", &usage)) {
    return false;
  }
  RootedValue help(cx);
  if (!JS_GetProperty(cx, obj, "help", &help)) {
    return false;
  }

  if (!usage.isString() || !help.isString()) {
    return true;
  }

  return PrintHelpString(cx, usage) && PrintHelpString(cx, help);
}

struct ExtraGlobalBindingWithHelp {
  const char* name;
  const char* help;
};

// clang-format off
static ExtraGlobalBindingWithHelp extraGlobalBindingsWithHelp[] = {
// Defined in BindScriptArgs.
    {
"scriptArgs",
"  An array containing the command line arguments passed after the path\n"
"  to a JS script."},
    {
"scriptPath",
"  The path to the JS script passed to JS shell.  This does not reflect\n"
"  modules evaluated via -m option."},

// Defined in DefineConsole.
    {
"console",
"  An object with console.log() which aliases print()."},

// Defined in NewGlobalObject.
    {
"performance",
"  An object with the following properties:\n"
"    performance.now()\n"
"      See help(performance.now)\n"
"    performance.mozMemory.gc\n"
"      An object that represents GC statistics with the following properties:\n"
"        gcBytes\n"
"        gcMaxBytes\n"
"        mallocBytes\n"
"        gcIsHighFrequencyMode\n"
"        gcNumber\n"
"        majorGCCount\n"
"        minorGCCount\n"
"        sliceCount\n"
"        compartmentCount\n"
"        lastStartReason\n"
"        zone.gcBytes\n"
"        zone.gcTriggerBytes\n"
"        zone.gcAllocTrigger\n"
"        zone.mallocBytes\n"
"        zone.mallocTriggerBytes\n"
"        zone.gcNumber"},
    {
"new FakeDOMObject()",
"  A constructor to test IonMonkey DOM optimizations in JS shell.\n"
"  The prototype object has the following properties:\n"
"    FakeDOMObject.prototype.x\n"
"      Generic getter/setter with JSJitInfo\n"
"    FakeDOMObject.prototype.slot\n"
"      Getter with JSJitInfo.slotIndex\n"
"    FakeDOMObject.prototype.global\n"
"      Getter/setter with JSJitInfo::AliasEverything\n"
"    FakeDOMObject.prototype.doFoo()\n"
"      Method with JSJitInfo\n"
"    FakeDOMObject.prototype.getObject()\n"
"      Method with JSJitInfo that returns an object."},
};
// clang-format on

static bool MatchPattern(JSContext* cx, JS::Handle<RegExpObject*> regex,
                         JS::Handle<JSString*> inputStr, bool* result) {
  JS::Rooted<JSString*> linearInputStr(cx, inputStr);
  if (!linearInputStr->ensureLinear(cx)) {
    return false;
  }

  // Execute the regular expression in |regex|'s compartment.
  JSAutoRealm ar(cx, regex);
  if (!cx->compartment()->wrap(cx, &linearInputStr)) {
    return false;
  }
  JS::Rooted<JSLinearString*> input(cx, &linearInputStr->asLinear());
  size_t ignored = 0;
  JS::Rooted<JS::Value> v(cx);
  if (!ExecuteRegExpLegacy(cx, nullptr, regex, input, &ignored, true, &v)) {
    return false;
  }
  *result = !v.isNull();
  return true;
}

static bool PrintEnumeratedHelp(JSContext* cx, HandleObject obj,
                                HandleObject pattern, bool brief) {
  RootedIdVector idv(cx);
  if (!GetPropertyKeys(cx, obj, JSITER_OWNONLY | JSITER_HIDDEN, &idv)) {
    return false;
  }

  Rooted<RegExpObject*> regex(cx);
  if (pattern) {
    regex = &UncheckedUnwrap(pattern)->as<RegExpObject>();
  }

  for (size_t i = 0; i < idv.length(); i++) {
    RootedValue v(cx);
    RootedId id(cx, idv[i]);
    if (!JS_GetPropertyById(cx, obj, id, &v)) {
      return false;
    }
    if (!v.isObject()) {
      continue;
    }

    RootedObject funcObj(cx, &v.toObject());
    if (regex) {
      // Only pay attention to objects with a 'help' property, which will
      // either be documented functions or interface objects.
      if (!JS_GetProperty(cx, funcObj, "help", &v)) {
        return false;
      }
      if (!v.isString()) {
        continue;
      }

      // For functions, match against the name. For interface objects,
      // match against the usage string.
      if (!JS_GetProperty(cx, funcObj, "name", &v)) {
        return false;
      }
      if (!v.isString()) {
        if (!JS_GetProperty(cx, funcObj, "usage", &v)) {
          return false;
        }
        if (!v.isString()) {
          continue;
        }
      }

      Rooted<JSString*> inputStr(cx, v.toString());
      bool result = false;
      if (!MatchPattern(cx, regex, inputStr, &result)) {
        return false;
      }
      if (!result) {
        continue;
      }
    }

    if (!PrintHelp(cx, funcObj)) {
      return false;
    }
  }

  return true;
}

static bool PrintExtraGlobalEnumeratedHelp(JSContext* cx, HandleObject pattern,
                                           bool brief) {
  Rooted<RegExpObject*> regex(cx);
  if (pattern) {
    regex = &UncheckedUnwrap(pattern)->as<RegExpObject>();
  }

  for (const auto& item : extraGlobalBindingsWithHelp) {
    if (regex) {
      JS::Rooted<JSString*> name(cx, JS_NewStringCopyZ(cx, item.name));
      if (!name) {
        return false;
      }

      bool result = false;
      if (!MatchPattern(cx, regex, name, &result)) {
        return false;
      }
      if (!result) {
        continue;
      }
    }
    fprintf(gOutFile->fp, "%s\n", item.name);
    fprintf(gOutFile->fp, "%s\n", item.help);
  }

  return true;
}

static bool Help(JSContext* cx, unsigned argc, Value* vp) {
  if (!gOutFile->isOpen()) {
    JS_ReportErrorASCII(cx, "output file is closed");
    return false;
  }

  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setUndefined();
  RootedObject global(cx, JS::CurrentGlobalOrNull(cx));

  // help() - display the version and dump out help for all functions on the
  // global.
  if (args.length() == 0) {
    fprintf(gOutFile->fp, "%s\n", JS_GetImplementationVersion());

    if (!PrintEnumeratedHelp(cx, global, nullptr, false)) {
      return false;
    }
    if (!PrintExtraGlobalEnumeratedHelp(cx, nullptr, false)) {
      return false;
    }
    return true;
  }

  RootedValue v(cx);

  if (args[0].isPrimitive()) {
    // help("foo")
    JS_ReportErrorASCII(cx, "primitive arg");
    return false;
  }

  RootedObject obj(cx, &args[0].toObject());
  if (!obj) {
    return true;
  }
  bool isRegexp;
  if (!JS::ObjectIsRegExp(cx, obj, &isRegexp)) {
    return false;
  }

  if (isRegexp) {
    // help(/pattern/)
    if (!PrintEnumeratedHelp(cx, global, obj, false)) {
      return false;
    }
    if (!PrintExtraGlobalEnumeratedHelp(cx, obj, false)) {
      return false;
    }
    return true;
  }

  // help(function)
  // help(namespace_obj)
  return PrintHelp(cx, obj);
}

static const JSErrorFormatString jsShell_ErrorFormatString[JSShellErr_Limit] = {
#define MSG_DEF(name, count, exception, format) \
  {#name, format, count, JSEXN_ERR},
#include "jsshell.msg"
#undef MSG_DEF
};

const JSErrorFormatString* js::shell::my_GetErrorMessage(
    void* userRef, const unsigned errorNumber) {
  if (errorNumber == 0 || errorNumber >= JSShellErr_Limit) {
    return nullptr;
  }

  return &jsShell_ErrorFormatString[errorNumber];
}

static bool CreateLastWarningObject(JSContext* cx, JSErrorReport* report) {
  RootedObject warningObj(cx, JS_NewObject(cx, nullptr));
  if (!warningObj) {
    return false;
  }

  if (!CopyErrorReportToObject(cx, report, warningObj)) {
    return false;
  }

  GetShellContext(cx)->lastWarning.setObject(*warningObj);
  return true;
}

static FILE* ErrorFilePointer() {
  if (gErrFile->isOpen()) {
    return gErrFile->fp;
  }

  fprintf(stderr, "error file is closed; falling back to stderr\n");
  return stderr;
}

bool shell::PrintStackTrace(JSContext* cx, HandleObject stackObj) {
  if (!stackObj || !stackObj->is<SavedFrame>()) {
    return true;
  }

  JSPrincipals* principals = stackObj->nonCCWRealm()->principals();
  RootedString stackStr(cx);
  if (!BuildStackString(cx, principals, stackObj, &stackStr, 2)) {
    return false;
  }

  UniqueChars stack = JS_EncodeStringToUTF8(cx, stackStr);
  if (!stack) {
    return false;
  }

  FILE* fp = ErrorFilePointer();
  fputs("Stack:\n", fp);
  fputs(stack.get(), fp);

  return true;
}

js::shell::AutoReportException::~AutoReportException() {
  if (!JS_IsExceptionPending(cx)) {
    return;
  }

  auto printError = [](JSContext* cx, auto& report, const auto& exnStack,
                       const char* prefix = nullptr) {
    if (!report.init(cx, exnStack, JS::ErrorReportBuilder::WithSideEffects)) {
      fprintf(stderr, "out of memory initializing JS::ErrorReportBuilder\n");
      fflush(stderr);
      JS_ClearPendingException(cx);
      return false;
    }

    MOZ_ASSERT(!report.report()->isWarning());

    FILE* fp = ErrorFilePointer();
    if (prefix) {
      fputs(prefix, fp);
    }
    JS::PrintError(fp, report, reportWarnings);
    JS_ClearPendingException(cx);

    // If possible, use the original error stack as the source of truth, because
    // finally block handlers may have overwritten the exception stack.
    RootedObject stack(cx, exnStack.stack());
    if (exnStack.exception().isObject()) {
      RootedObject exception(cx, &exnStack.exception().toObject());
      if (JSObject* exceptionStack = JS::ExceptionStackOrNull(exception)) {
        stack.set(exceptionStack);
      }
    }

    if (!PrintStackTrace(cx, stack)) {
      fputs("(Unable to print stack trace)\n", fp);
      JS_ClearPendingException(cx);
    }

    return true;
  };

  // Get exception object and stack before printing and clearing exception.
  JS::ExceptionStack exnStack(cx);
  if (!JS::StealPendingExceptionStack(cx, &exnStack)) {
    fprintf(stderr, "out of memory while stealing exception\n");
    fflush(stderr);
    JS_ClearPendingException(cx);
    return;
  }

  ShellContext* sc = GetShellContext(cx);
  JS::ErrorReportBuilder report(cx);
  if (!printError(cx, report, exnStack)) {
    // Return if we couldn't initialize the error report.
    return;
  }

  // Print the error's cause, if available.
  if (exnStack.exception().isObject()) {
    JSObject* exception = &exnStack.exception().toObject();
    if (exception->is<ErrorObject>()) {
      auto* error = &exception->as<ErrorObject>();
      if (auto maybeCause = error->getCause()) {
        RootedValue cause(cx, maybeCause.value());

        RootedObject causeStack(cx);
        if (cause.isObject()) {
          RootedObject causeObj(cx, &cause.toObject());
          causeStack = JS::ExceptionStackOrNull(causeObj);
        }

        JS::ExceptionStack exnStack(cx, cause, causeStack);
        JS::ErrorReportBuilder report(cx);
        printError(cx, report, exnStack, "Caused by: ");
      }
    }
  }

#if defined(DEBUG) || defined(JS_OOM_BREAKPOINT)
  // Don't quit the shell if an unhandled exception is reported during OOM
  // testing.
  if (cx->runningOOMTest) {
    return;
  }
#endif

  if (report.report()->errorNumber == JSMSG_OUT_OF_MEMORY) {
    sc->exitCode = EXITCODE_OUT_OF_MEMORY;
  } else {
    sc->exitCode = EXITCODE_RUNTIME_ERROR;
  }
}

void js::shell::WarningReporter(JSContext* cx, JSErrorReport* report) {
  ShellContext* sc = GetShellContext(cx);
  FILE* fp = ErrorFilePointer();

  MOZ_ASSERT(report->isWarning());

  if (sc->lastWarningEnabled) {
    JS::AutoSaveExceptionState savedExc(cx);
    if (!CreateLastWarningObject(cx, report)) {
      fputs("Unhandled error happened while creating last warning object.\n",
            fp);
      fflush(fp);
    }
    savedExc.restore();
  }

  // Print the warning.
  JS::PrintError(fp, report, reportWarnings);
}

static bool global_enumerate(JSContext* cx, JS::HandleObject obj,
                             JS::MutableHandleIdVector properties,
                             bool enumerableOnly) {
#ifdef LAZY_STANDARD_CLASSES
  return JS_NewEnumerateStandardClasses(cx, obj, properties, enumerableOnly);
#else
  return true;
#endif
}

static bool global_resolve(JSContext* cx, HandleObject obj, HandleId id,
                           bool* resolvedp) {
#ifdef LAZY_STANDARD_CLASSES
  if (!JS_ResolveStandardClass(cx, obj, id, resolvedp)) {
    return false;
  }
#endif
  return true;
}

static bool global_mayResolve(const JSAtomState& names, jsid id,
                              JSObject* maybeObj) {
  return JS_MayResolveStandardClass(names, id, maybeObj);
}

static const JSClassOps global_classOps = {
    nullptr,                   // addProperty
    nullptr,                   // delProperty
    nullptr,                   // enumerate
    global_enumerate,          // newEnumerate
    global_resolve,            // resolve
    global_mayResolve,         // mayResolve
    nullptr,                   // finalize
    nullptr,                   // call
    nullptr,                   // construct
    JS_GlobalObjectTraceHook,  // trace
};

static constexpr uint32_t DOM_PROTOTYPE_SLOT = JSCLASS_GLOBAL_SLOT_COUNT;
static constexpr uint32_t DOM_GLOBAL_SLOTS = 1;

static const JSClass global_class = {
    "global",
    JSCLASS_GLOBAL_FLAGS | JSCLASS_GLOBAL_FLAGS_WITH_SLOTS(DOM_GLOBAL_SLOTS),
    &global_classOps,
};

/*
 * Define a FakeDOMObject constructor. It returns an object with a getter,
 * setter and method with attached JitInfo. This object can be used to test
 * IonMonkey DOM optimizations in the shell.
 */

/* Fow now just use to a constant we can check. */
static const void* DOM_PRIVATE_VALUE = (void*)0x1234;

static bool dom_genericGetter(JSContext* cx, unsigned argc, JS::Value* vp);

static bool dom_genericSetter(JSContext* cx, unsigned argc, JS::Value* vp);

static bool dom_genericMethod(JSContext* cx, unsigned argc, JS::Value* vp);

static bool dom_get_x(JSContext* cx, HandleObject obj, void* self,
                      JSJitGetterCallArgs args) {
  MOZ_ASSERT(JS::GetClass(obj) == GetDomClass());
  MOZ_ASSERT(self == DOM_PRIVATE_VALUE);
  args.rval().set(JS_NumberValue(double(3.14)));
  return true;
}

static bool dom_set_x(JSContext* cx, HandleObject obj, void* self,
                      JSJitSetterCallArgs args) {
  MOZ_ASSERT(JS::GetClass(obj) == GetDomClass());
  MOZ_ASSERT(self == DOM_PRIVATE_VALUE);
  return true;
}

static bool dom_get_slot(JSContext* cx, HandleObject obj, void* self,
                         JSJitGetterCallArgs args) {
  MOZ_ASSERT(JS::GetClass(obj) == GetDomClass());
  MOZ_ASSERT(self == DOM_PRIVATE_VALUE);

  Value v = JS::GetReservedSlot(obj, DOM_OBJECT_SLOT2);
  MOZ_ASSERT(v.toInt32() == 42);
  args.rval().set(v);
  return true;
}

static bool dom_get_global(JSContext* cx, HandleObject obj, void* self,
                           JSJitGetterCallArgs args) {
  MOZ_ASSERT(JS::GetClass(obj) == GetDomClass());
  MOZ_ASSERT(self == DOM_PRIVATE_VALUE);

  // Return the current global (instead of obj->global()) to test cx->realm
  // switching in the JIT.
  args.rval().setObject(*ToWindowProxyIfWindow(cx->global()));

  return true;
}

static bool dom_set_global(JSContext* cx, HandleObject obj, void* self,
                           JSJitSetterCallArgs args) {
  MOZ_ASSERT(JS::GetClass(obj) == GetDomClass());
  MOZ_ASSERT(self == DOM_PRIVATE_VALUE);

  // Throw an exception if our argument is not the current global. This lets
  // us test cx->realm switching.
  if (!args[0].isObject() ||
      ToWindowIfWindowProxy(&args[0].toObject()) != cx->global()) {
    JS_ReportErrorASCII(cx, "Setter not called with matching global argument");
    return false;
  }

  return true;
}

static bool dom_doFoo(JSContext* cx, HandleObject obj, void* self,
                      const JSJitMethodCallArgs& args) {
  MOZ_ASSERT(JS::GetClass(obj) == GetDomClass());
  MOZ_ASSERT(self == DOM_PRIVATE_VALUE);
  MOZ_ASSERT(cx->realm() == args.callee().as<JSFunction>().realm());

  /* Just return args.length(). */
  args.rval().setInt32(args.length());
  return true;
}

static bool dom_doBar(JSContext* cx, HandleObject obj, void* self,
                      const JSJitMethodCallArgs& args) {
  MOZ_ASSERT(JS::GetClass(obj) == GetDomClass());
  MOZ_ASSERT(self == DOM_PRIVATE_VALUE);
  MOZ_ASSERT(cx->realm() == args.callee().as<JSFunction>().realm());

  static const JSClass barClass = {
      "BarObj",
  };

  JSObject* retObj =
      JS_NewObjectWithGivenProtoAndUseAllocSite(cx, &barClass, nullptr);
  if (!retObj) {
    return false;
  }

  args.rval().setObject(*retObj);
  return true;
}

static const JSJitInfo dom_x_getterinfo = {
    {(JSJitGetterOp)dom_get_x},
    {0}, /* protoID */
    {0}, /* depth */
    JSJitInfo::Getter,
    JSJitInfo::AliasNone, /* aliasSet */
    JSVAL_TYPE_UNKNOWN,   /* returnType */
    true,                 /* isInfallible. False in setters. */
    true,                 /* isMovable */
    true,                 /* isEliminatable */
    false,                /* isAlwaysInSlot */
    false,                /* isLazilyCachedInSlot */
    false,                /* isTypedMethod */
    0                     /* slotIndex */
};

static const JSJitInfo dom_x_setterinfo = {
    {(JSJitGetterOp)dom_set_x},
    {0}, /* protoID */
    {0}, /* depth */
    JSJitInfo::Setter,
    JSJitInfo::AliasEverything, /* aliasSet */
    JSVAL_TYPE_UNKNOWN,         /* returnType */
    false,                      /* isInfallible. False in setters. */
    false,                      /* isMovable. */
    false,                      /* isEliminatable. */
    false,                      /* isAlwaysInSlot */
    false,                      /* isLazilyCachedInSlot */
    false,                      /* isTypedMethod */
    0                           /* slotIndex */
};

static const JSJitInfo dom_slot_getterinfo = {
    {(JSJitGetterOp)dom_get_slot},
    {0}, /* protoID */
    {0}, /* depth */
    JSJitInfo::Getter,
    JSJitInfo::AliasNone, /* aliasSet */
    JSVAL_TYPE_INT32,     /* returnType */
    false,                /* isInfallible. False in setters. */
    true,                 /* isMovable */
    true,                 /* isEliminatable */
    true,                 /* isAlwaysInSlot */
    false,                /* isLazilyCachedInSlot */
    false,                /* isTypedMethod */
    DOM_OBJECT_SLOT2      /* slotIndex */
};

// Note: this getter uses AliasEverything and is marked as fallible and
// non-movable (1) to prevent Ion from getting too clever optimizing it and
// (2) it's nice to have a few different kinds of getters in the shell.
static const JSJitInfo dom_global_getterinfo = {
    {(JSJitGetterOp)dom_get_global},
    {0}, /* protoID */
    {0}, /* depth */
    JSJitInfo::Getter,
    JSJitInfo::AliasEverything, /* aliasSet */
    JSVAL_TYPE_OBJECT,          /* returnType */
    false,                      /* isInfallible. False in setters. */
    false,                      /* isMovable */
    false,                      /* isEliminatable */
    false,                      /* isAlwaysInSlot */
    false,                      /* isLazilyCachedInSlot */
    false,                      /* isTypedMethod */
    0                           /* slotIndex */
};

static const JSJitInfo dom_global_setterinfo = {
    {(JSJitGetterOp)dom_set_global},
    {0}, /* protoID */
    {0}, /* depth */
    JSJitInfo::Setter,
    JSJitInfo::AliasEverything, /* aliasSet */
    JSVAL_TYPE_UNKNOWN,         /* returnType */
    false,                      /* isInfallible. False in setters. */
    false,                      /* isMovable. */
    false,                      /* isEliminatable. */
    false,                      /* isAlwaysInSlot */
    false,                      /* isLazilyCachedInSlot */
    false,                      /* isTypedMethod */
    0                           /* slotIndex */
};

static const JSJitInfo doFoo_methodinfo = {
    {(JSJitGetterOp)dom_doFoo},
    {0}, /* protoID */
    {0}, /* depth */
    JSJitInfo::Method,
    JSJitInfo::AliasEverything, /* aliasSet */
    JSVAL_TYPE_UNKNOWN,         /* returnType */
    false,                      /* isInfallible. False in setters. */
    false,                      /* isMovable */
    false,                      /* isEliminatable */
    false,                      /* isAlwaysInSlot */
    false,                      /* isLazilyCachedInSlot */
    false,                      /* isTypedMethod */
    0                           /* slotIndex */
};

static const JSJitInfo doBar_methodinfo = {
    {(JSJitGetterOp)dom_doBar},
    {0}, /* protoID */
    {0}, /* depth */
    JSJitInfo::Method,
    JSJitInfo::AliasEverything, /* aliasSet */
    JSVAL_TYPE_OBJECT,          /* returnType */
    false,                      /* isInfallible. False in setters. */
    false,                      /* isMovable */
    false,                      /* isEliminatable */
    false,                      /* isAlwaysInSlot */
    false,                      /* isLazilyCachedInSlot */
    false,                      /* isTypedMethod */
    0                           /* slotIndex */
};

static const JSPropertySpec dom_props[] = {
    JSPropertySpec::nativeAccessors("x", JSPROP_ENUMERATE, dom_genericGetter,
                                    &dom_x_getterinfo, dom_genericSetter,
                                    &dom_x_setterinfo),
    JSPropertySpec::nativeAccessors("slot", JSPROP_ENUMERATE, dom_genericGetter,
                                    &dom_slot_getterinfo),
    JSPropertySpec::nativeAccessors("global", JSPROP_ENUMERATE,
                                    dom_genericGetter, &dom_global_getterinfo,
                                    dom_genericSetter, &dom_global_setterinfo),
    JS_PS_END,
};

static const JSFunctionSpec dom_methods[] = {
    JS_FNINFO("doFoo", dom_genericMethod, &doFoo_methodinfo, 3,
              JSPROP_ENUMERATE),
    JS_FNINFO("doBar", dom_genericMethod, &doBar_methodinfo, 3,
              JSPROP_ENUMERATE),
    JS_FS_END,
};

static void FakeDOMObject_finalize(JS::GCContext* gcx, JSObject* obj) {
  // Dummy finalize method so we can swap with background finalized object.
}

static const JSClassOps FakeDOMObjectClassOps = {
    nullptr,  // addProperty
    nullptr,  // delProperty
    nullptr,  // enumerate
    nullptr,  // newEnumerate
    nullptr,  // resolve
    nullptr,  // mayResolve
    FakeDOMObject_finalize,
    nullptr,  // call
    nullptr,  // construct
    nullptr,
};

static const JSClass dom_class = {"FakeDOMObject",
                                  JSCLASS_IS_DOMJSCLASS |
                                      JSCLASS_HAS_RESERVED_SLOTS(2) |
                                      JSCLASS_BACKGROUND_FINALIZE,
                                  &FakeDOMObjectClassOps};

static const JSClass* GetDomClass() { return &dom_class; }

static bool dom_genericGetter(JSContext* cx, unsigned argc, JS::Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!args.thisv().isObject()) {
    args.rval().setUndefined();
    return true;
  }

  RootedObject obj(cx, &args.thisv().toObject());
  if (JS::GetClass(obj) != &dom_class) {
    args.rval().set(UndefinedValue());
    return true;
  }

  JS::Value val = JS::GetReservedSlot(obj, DOM_OBJECT_SLOT);

  const JSJitInfo* info = FUNCTION_VALUE_TO_JITINFO(args.calleev());
  MOZ_ASSERT(info->type() == JSJitInfo::Getter);
  JSJitGetterOp getter = info->getter;
  return getter(cx, obj, val.toPrivate(), JSJitGetterCallArgs(args));
}

static bool dom_genericSetter(JSContext* cx, unsigned argc, JS::Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (args.length() < 1 || !args.thisv().isObject()) {
    args.rval().setUndefined();
    return true;
  }

  RootedObject obj(cx, &args.thisv().toObject());
  if (JS::GetClass(obj) != &dom_class) {
    args.rval().set(UndefinedValue());
    return true;
  }

  JS::Value val = JS::GetReservedSlot(obj, DOM_OBJECT_SLOT);

  const JSJitInfo* info = FUNCTION_VALUE_TO_JITINFO(args.calleev());
  MOZ_ASSERT(info->type() == JSJitInfo::Setter);
  JSJitSetterOp setter = info->setter;
  if (!setter(cx, obj, val.toPrivate(), JSJitSetterCallArgs(args))) {
    return false;
  }
  args.rval().set(UndefinedValue());
  return true;
}

static bool dom_genericMethod(JSContext* cx, unsigned argc, JS::Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!args.thisv().isObject()) {
    args.rval().setUndefined();
    return true;
  }

  RootedObject obj(cx, &args.thisv().toObject());
  if (JS::GetClass(obj) != &dom_class) {
    args.rval().set(UndefinedValue());
    return true;
  }

  JS::Value val = JS::GetReservedSlot(obj, DOM_OBJECT_SLOT);

  const JSJitInfo* info = FUNCTION_VALUE_TO_JITINFO(args.calleev());
  MOZ_ASSERT(info->type() == JSJitInfo::Method);
  JSJitMethodOp method = info->method;
  return method(cx, obj, val.toPrivate(), JSJitMethodCallArgs(args));
}

static void InitDOMObject(HandleObject obj) {
  JS::SetReservedSlot(obj, DOM_OBJECT_SLOT,
                      PrivateValue(const_cast<void*>(DOM_PRIVATE_VALUE)));
  JS::SetReservedSlot(obj, DOM_OBJECT_SLOT2, Int32Value(42));
}

static JSObject* GetDOMPrototype(JSContext* cx, JSObject* global) {
  MOZ_ASSERT(JS_IsGlobalObject(global));
  if (JS::GetClass(global) != &global_class) {
    JS_ReportErrorASCII(cx, "Can't get FakeDOMObject prototype in sandbox");
    return nullptr;
  }

  const JS::Value& slot = JS::GetReservedSlot(global, DOM_PROTOTYPE_SLOT);
  MOZ_ASSERT(slot.isObject());
  return &slot.toObject();
}

static bool dom_constructor(JSContext* cx, unsigned argc, JS::Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  RootedObject callee(cx, &args.callee());
  RootedValue protov(cx);
  if (!GetProperty(cx, callee, callee, cx->names().prototype, &protov)) {
    return false;
  }

  if (!protov.isObject()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_BAD_PROTOTYPE,
                              "FakeDOMObject");
    return false;
  }

  RootedObject proto(cx, &protov.toObject());
  RootedObject domObj(cx, JS_NewObjectWithGivenProto(cx, &dom_class, proto));
  if (!domObj) {
    return false;
  }

  InitDOMObject(domObj);

  args.rval().setObject(*domObj);
  return true;
}

static bool InstanceClassHasProtoAtDepth(const JSClass* clasp, uint32_t protoID,
                                         uint32_t depth) {
  // Only the (fake) DOM object supports any JIT optimizations.
  return clasp == GetDomClass();
}

static bool InstanceClassIsError(const JSClass* clasp) { return false; }

static bool ShellBuildId(JS::BuildIdCharVector* buildId) {
  // The browser embeds the date into the buildid and the buildid is embedded
  // in the binary, so every 'make' necessarily builds a new firefox binary.
  // Fortunately, the actual firefox executable is tiny -- all the code is in
  // libxul.so and other shared modules -- so this isn't a big deal. Not so
  // for the statically-linked JS shell. To avoid recompiling js.cpp and
  // re-linking 'js' on every 'make', we use a constant buildid and rely on
  // the shell user to manually clear any caches between cache-breaking updates.
  const char buildid[] = "JS-shell";
  return buildId->append(buildid, sizeof(buildid));
}

static bool TimesAccessed(JSContext* cx, unsigned argc, Value* vp) {
  static int32_t accessed = 0;
  CallArgs args = CallArgsFromVp(argc, vp);
  args.rval().setInt32(++accessed);
  return true;
}

static const JSPropertySpec TestingProperties[] = {
    JS_PSG("timesAccessed", TimesAccessed, 0),
    JS_PS_END,
};

static JSObject* NewGlobalObject(JSContext* cx, JS::RealmOptions& options,
                                 JSPrincipals* principals, ShellGlobalKind kind,
                                 bool immutablePrototype) {
  RootedObject glob(cx,
                    JS_NewGlobalObject(cx, &global_class, principals,
                                       JS::DontFireOnNewGlobalHook, options));
  if (!glob) {
    return nullptr;
  }

  {
    JSAutoRealm ar(cx, glob);

    if (kind == ShellGlobalKind::WindowProxy) {
      RootedObject proxy(cx, NewShellWindowProxy(cx, glob));
      if (!proxy) {
        return nullptr;
      }
      js::SetWindowProxy(cx, glob, proxy);
    }

#ifndef LAZY_STANDARD_CLASSES
    if (!JS::InitRealmStandardClasses(cx)) {
      return nullptr;
    }
#endif

    if (immutablePrototype) {
      bool succeeded;
      if (!JS_SetImmutablePrototype(cx, glob, &succeeded)) {
        return nullptr;
      }
      MOZ_ASSERT(succeeded,
                 "a fresh, unexposed global object is always capable of "
                 "having its [[Prototype]] be immutable");
    }

#ifdef JS_HAS_CTYPES
    if (!fuzzingSafe && !JS::InitCTypesClass(cx, glob)) {
      return nullptr;
    }
#endif
    if (!JS_InitReflectParse(cx, glob)) {
      return nullptr;
    }
    if (!JS_DefineDebuggerObject(cx, glob)) {
      return nullptr;
    }
    if (!JS_DefineFunctionsWithHelp(cx, glob, shell_functions) ||
        !JS_DefineProfilingFunctions(cx, glob)) {
      return nullptr;
    }
#ifdef FUZZING_JS_FUZZILLI
    if (!JS_DefineFunctions(cx, glob, shell_function_fuzzilli_hash)) {
      return nullptr;
    }
#endif
    if (!js::DefineTestingFunctions(cx, glob, fuzzingSafe,
                                    disableOOMFunctions)) {
      return nullptr;
    }
    if (!JS_DefineProperties(cx, glob, TestingProperties)) {
      return nullptr;
    }

    if (!fuzzingSafe) {
      if (!JS_DefineFunctionsWithHelp(cx, glob, fuzzing_unsafe_functions)) {
        return nullptr;
      }
      if (!DefineConsole(cx, glob)) {
        return nullptr;
      }
    }

    if (!DefineOS(cx, glob, fuzzingSafe, &gOutFile, &gErrFile)) {
      return nullptr;
    }

    if (!js::SupportDifferentialTesting()) {
      if (!JS_DefineFunctionsWithHelp(cx, glob,
                                      diff_testing_unsafe_functions)) {
        return nullptr;
      }

      RootedObject performanceObj(cx, JS_NewObject(cx, nullptr));
      if (!performanceObj) {
        return nullptr;
      }
      if (!JS_DefineFunctionsWithHelp(cx, performanceObj,
                                      performance_functions)) {
        return nullptr;
      }
      RootedObject mozMemoryObj(cx, JS_NewObject(cx, nullptr));
      if (!mozMemoryObj) {
        return nullptr;
      }
      RootedObject gcObj(cx, gc::NewMemoryInfoObject(cx));
      if (!gcObj) {
        return nullptr;
      }
      if (!JS_DefineProperty(cx, glob, "performance", performanceObj,
                             JSPROP_ENUMERATE)) {
        return nullptr;
      }
      if (!JS_DefineProperty(cx, performanceObj, "mozMemory", mozMemoryObj,
                             JSPROP_ENUMERATE)) {
        return nullptr;
      }
      if (!JS_DefineProperty(cx, mozMemoryObj, "gc", gcObj, JSPROP_ENUMERATE)) {
        return nullptr;
      }
    }

    /* Initialize FakeDOMObject. */
    static const js::DOMCallbacks DOMcallbacks = {InstanceClassHasProtoAtDepth,
                                                  InstanceClassIsError};
    SetDOMCallbacks(cx, &DOMcallbacks);

    RootedObject domProto(
        cx, JS_InitClass(cx, glob, &dom_class, nullptr, "FakeDOMObject",
                         dom_constructor, 0, dom_props, dom_methods, nullptr,
                         nullptr));
    if (!domProto) {
      return nullptr;
    }

    // FakeDOMObject.prototype is the only DOM object which needs to retrieved
    // in the shell; store it directly instead of creating a separate layer
    // (ProtoAndIfaceCache) as done in the browser.
    JS::SetReservedSlot(glob, DOM_PROTOTYPE_SLOT, ObjectValue(*domProto));

    /* Initialize FakeDOMObject.prototype */
    InitDOMObject(domProto);

    if (!DefineToStringTag(cx, glob, cx->names().global)) {
      return nullptr;
    }

    JS_FireOnNewGlobalObject(cx, glob);
  }

  return glob;
}

static bool BindScriptArgs(JSContext* cx, OptionParser* op) {
  AutoReportException are(cx);

  MultiStringRange msr = op->getMultiStringArg("scriptArgs");
  RootedObject scriptArgs(cx);
  scriptArgs = JS::NewArrayObject(cx, 0);
  if (!scriptArgs) {
    return false;
  }

  if (!JS_DefineProperty(cx, cx->global(), "scriptArgs", scriptArgs, 0)) {
    return false;
  }

  for (size_t i = 0; !msr.empty(); msr.popFront(), ++i) {
    const char* scriptArg = msr.front();
    UniqueChars scriptArgUtf8 = JS::EncodeNarrowToUtf8(cx, scriptArg);
    if (!scriptArgUtf8) {
      return false;
    }
    RootedString str(cx, NewStringCopyUTF8(cx, scriptArgUtf8.get()));
    if (!str || !JS_DefineElement(cx, scriptArgs, i, str, JSPROP_ENUMERATE)) {
      return false;
    }
  }

  RootedValue scriptPathValue(cx);
  if (const char* scriptPath = op->getStringArg("script")) {
    UniqueChars scriptPathUtf8 = JS::EncodeNarrowToUtf8(cx, scriptPath);
    if (!scriptPathUtf8) {
      return false;
    }
    RootedString scriptPathString(cx,
                                  NewStringCopyUTF8(cx, scriptPathUtf8.get()));
    if (!scriptPathString) {
      return false;
    }
    scriptPathValue = StringValue(scriptPathString);
  } else {
    scriptPathValue = UndefinedValue();
  }

  if (!JS_DefineProperty(cx, cx->global(), "scriptPath", scriptPathValue, 0)) {
    return false;
  }

  return true;
}

static bool OptionFailure(const char* option, const char* str) {
  fprintf(stderr, "Unrecognized option for %s: %s\n", option, str);
  return false;
}

template <typename... Ts>
auto minVal(Ts... args);
template <typename T>
auto minVal(T a) {
  return a;
}

template <typename T, typename... Ts>
auto minVal(T a, Ts... args) {
  return std::min(a, minVal(args...));
}

[[nodiscard]] static bool ProcessArgs(JSContext* cx, OptionParser* op) {
  ShellContext* sc = GetShellContext(cx);

  /* |scriptArgs| gets bound on the global before any code is run. */
  if (!BindScriptArgs(cx, op)) {
    return false;
  }

  MultiStringRange filePaths = op->getMultiStringOption('f');
  MultiStringRange utf16FilePaths = op->getMultiStringOption('u');
  MultiStringRange preludePaths = op->getMultiStringOption('p');
  MultiStringRange codeChunks = op->getMultiStringOption('e');
  MultiStringRange modulePaths = op->getMultiStringOption('m');

#ifdef FUZZING_JS_FUZZILLI
  // Check for REPRL file source
  if (op->getBoolOption("reprl")) {
    return FuzzilliReprlGetAndRun(cx);
  }
#endif /* FUZZING_JS_FUZZILLI */

  if (filePaths.empty() && utf16FilePaths.empty() && codeChunks.empty() &&
      modulePaths.empty() && !op->getStringArg("script")) {
    // Always use the interactive shell when -i is used. Without -i we let
    // Process figure it out based on isatty.
    bool forceTTY = op->getBoolOption('i');
    return Process(cx, nullptr, forceTTY, FileScript);
  }

  while (!preludePaths.empty() || !filePaths.empty() ||
         !utf16FilePaths.empty() || !codeChunks.empty() ||
         !modulePaths.empty()) {
    size_t ppArgno = preludePaths.empty() ? SIZE_MAX : preludePaths.argno();
    size_t fpArgno = filePaths.empty() ? SIZE_MAX : filePaths.argno();
    size_t ufpArgno =
        utf16FilePaths.empty() ? SIZE_MAX : utf16FilePaths.argno();
    size_t ccArgno = codeChunks.empty() ? SIZE_MAX : codeChunks.argno();
    size_t mpArgno = modulePaths.empty() ? SIZE_MAX : modulePaths.argno();
    size_t minArgno = minVal(ppArgno, fpArgno, ufpArgno, ccArgno, mpArgno);

    if (ppArgno == minArgno) {
      UniqueChars path = JS::EncodeNarrowToUtf8(cx, preludePaths.front());
      if (!path) {
        return false;
      }
      if (!Process(cx, path.get(), false, PreludeScript)) {
        return false;
      }

      preludePaths.popFront();
      continue;
    }

    if (fpArgno == minArgno) {
      UniqueChars path = JS::EncodeNarrowToUtf8(cx, filePaths.front());
      if (!path) {
        return false;
      }
      if (!Process(cx, path.get(), false, FileScript)) {
        return false;
      }

      filePaths.popFront();
      continue;
    }

    if (ufpArgno == minArgno) {
      UniqueChars path = JS::EncodeNarrowToUtf8(cx, utf16FilePaths.front());
      if (!path) {
        return false;
      }
      if (!Process(cx, path.get(), false, FileScriptUtf16)) {
        return false;
      }

      utf16FilePaths.popFront();
      continue;
    }

    if (ccArgno == minArgno) {
      UniqueChars code = JS::EncodeNarrowToUtf8(cx, codeChunks.front());
      if (!code) {
        return false;
      }

      // Command line scripts are always parsed with full-parse to evaluate
      // conditions which might filter code coverage conditions.
      JS::CompileOptions opts(cx);
      opts.setFileAndLine("-e", 1).setForceFullParse();

      JS::SourceText<Utf8Unit> srcBuf;
      if (!srcBuf.init(cx, code.get(), strlen(code.get()),
                       JS::SourceOwnership::Borrowed)) {
        return false;
      }

      RootedValue rval(cx);
      if (!JS::Evaluate(cx, opts, srcBuf, &rval)) {
        return false;
      }

      codeChunks.popFront();
      if (sc->quitting) {
        break;
      }

      continue;
    }

    MOZ_ASSERT(mpArgno == minArgno);

    UniqueChars path = JS::EncodeNarrowToUtf8(cx, modulePaths.front());
    if (!path) {
      return false;
    }
    if (!Process(cx, path.get(), false, FileModule)) {
      return false;
    }

    modulePaths.popFront();
  }

  if (sc->quitting) {
    return false;
  }

  /* The |script| argument is processed after all options. */
  if (const char* path = op->getStringArg("script")) {
    UniqueChars pathUtf8 = JS::EncodeNarrowToUtf8(cx, path);
    if (!pathUtf8) {
      return false;
    }
    if (!Process(cx, pathUtf8.get(), false, FileScript)) {
      return false;
    }
  }

  if (op->getBoolOption('i')) {
    if (!Process(cx, nullptr, true, FileScript)) {
      return false;
    }
  }

  return true;
}

static void SetWorkerContextOptions(JSContext* cx) {
  // Copy option values from the main thread.
  JS::ContextOptionsRef(cx)
      .setAsmJS(enableAsmJS)
      .setWasm(enableWasm)
      .setWasmBaseline(enableWasmBaseline)
      .setWasmIon(enableWasmOptimizing)

      .setWasmVerbose(enableWasmVerbose)
      .setTestWasmAwaitTier2(enableTestWasmAwaitTier2)
      .setSourcePragmas(enableSourcePragmas);

  cx->runtime()->setOffthreadBaselineCompilationEnabled(
      offthreadBaselineCompilation);
  cx->runtime()->setOffthreadIonCompilationEnabled(offthreadIonCompilation);
  cx->runtime()->profilingScripts =
      enableCodeCoverage || enableDisassemblyDumps;

#ifdef JS_GC_ZEAL
  if (gZealBits && gZealFrequency) {
    for (size_t i = 0; i < size_t(gc::ZealMode::Count); i++) {
      if (gZealBits & (1 << i)) {
        cx->runtime()->gc.setZeal(i, gZealFrequency);
      }
    }
  }
#endif

  JS_SetNativeStackQuota(cx, gWorkerStackSize);
}

[[nodiscard]] static bool PrintUnhandledRejection(
    JSContext* cx, Handle<PromiseObject*> promise) {
  RootedValue reason(cx, promise->reason());
  RootedObject site(cx, promise->resolutionSite());

  RootedString str(cx, JS_ValueToSource(cx, reason));
  if (!str) {
    return false;
  }

  UniqueChars utf8chars = JS_EncodeStringToUTF8(cx, str);
  if (!utf8chars) {
    return false;
  }

  FILE* fp = ErrorFilePointer();
  fprintf(fp, "Unhandled rejection: %s\n", utf8chars.get());

  if (!site) {
    fputs("(no stack trace available)\n", stderr);
    return true;
  }

  JSPrincipals* principals = cx->realm()->principals();
  RootedString stackStr(cx);
  if (!BuildStackString(cx, principals, site, &stackStr, 2)) {
    return false;
  }

  UniqueChars stack = JS_EncodeStringToUTF8(cx, stackStr);
  if (!stack) {
    return false;
  }

  fputs("Stack:\n", fp);
  fputs(stack.get(), fp);

  return true;
}

[[nodiscard]] static bool ReportUnhandledRejections(JSContext* cx) {
  ShellContext* sc = GetShellContext(cx);
  if (!sc->trackUnhandledRejections) {
    return true;
  }

  if (!sc->unhandledRejectedPromises) {
    return true;
  }

  AutoRealm ar(cx, sc->unhandledRejectedPromises);

  if (sc->unhandledRejectedPromises->size() == 0) {
    return true;
  }

  sc->exitCode = EXITCODE_RUNTIME_ERROR;

  RootedValue iter(cx);
  if (!SetObject::iterator(cx, SetObject::IteratorKind::Values,
                           sc->unhandledRejectedPromises, &iter)) {
    return false;
  }

  Rooted<SetIteratorObject*> iterObj(cx,
                                     &iter.toObject().as<SetIteratorObject>());
  JSObject* obj = SetIteratorObject::createResult(cx);
  if (!obj) {
    return false;
  }

  Rooted<ArrayObject*> resultObj(cx, &obj->as<ArrayObject>());
  while (true) {
    bool done = SetIteratorObject::next(iterObj, resultObj);
    if (done) {
      break;
    }

    RootedObject obj(cx, &resultObj->getDenseElement(0).toObject());
    Rooted<PromiseObject*> promise(cx, obj->maybeUnwrapIf<PromiseObject>());
    if (!promise) {
      FILE* fp = ErrorFilePointer();
      fputs(
          "Unhandled rejection: dead proxy found in unhandled "
          "rejections set\n",
          fp);
      continue;
    }

    AutoRealm ar2(cx, promise);

    if (!PrintUnhandledRejection(cx, promise)) {
      return false;
    }
  }

  sc->unhandledRejectedPromises = nullptr;

  return true;
}

bool ShellContext::registerWithCx(JSContext* cx) {
  cx_ = cx;
  JS_SetContextPrivate(cx, this);

  if (isWorker) {
    SetWorkerContextOptions(cx);
  }

  JS::SetWarningReporter(cx, WarningReporter);
  JS_SetFutexCanWait(cx);
  JS_InitDestroyPrincipalsCallback(cx, ShellPrincipals::destroy);
  JS_SetDestroyCompartmentCallback(cx, DestroyShellCompartmentPrivate);
  js::SetWindowProxyClass(cx, &ShellWindowProxyClass);

  js::UseInternalJobQueues(cx);

  js::SetPreserveWrapperCallbacks(cx, DummyPreserveWrapperCallback,
                                  DummyHasReleasedWrapperCallback);

  JS::SetHostCleanupFinalizationRegistryCallback(
      cx, ShellCleanupFinalizationRegistryCallback, this);
  JS_AddExtraGCRootsTracer(cx, TraceBlackRoots, nullptr);
  JS_SetGrayGCRootsTracer(cx, TraceGrayRoots, nullptr);

  return true;
}

ShellContext::~ShellContext() {
  markObservers.reset();
  if (cx_) {
    JS_SetContextPrivate(cx_, nullptr);
    JS::SetHostCleanupFinalizationRegistryCallback(cx_, nullptr, nullptr);
    JS_SetGrayGCRootsTracer(cx_, nullptr, nullptr);
    JS_RemoveExtraGCRootsTracer(cx_, TraceBlackRoots, nullptr);
  }
  MOZ_ASSERT(offThreadJobs.empty());
}

static int Shell(JSContext* cx, OptionParser* op) {
#ifdef JS_STRUCTURED_SPEW
  cx->spewer().enableSpewing();
#endif

  auto exitShell = MakeScopeExit([&] {
#ifdef JS_STRUCTURED_SPEW
    cx->spewer().disableSpewing();
#endif
  });

#ifdef MOZ_CODE_COVERAGE
  InstallCoverageSignalHandlers();
#endif

  Maybe<JS::AutoDisableGenerationalGC> noggc;
  if (op->getBoolOption("no-ggc")) {
    noggc.emplace(cx);
  }

  Maybe<AutoDisableCompactingGC> nocgc;
  if (op->getBoolOption("no-cgc")) {
    nocgc.emplace(cx);
  }

#ifdef DEBUG
  if (op->getBoolOption("differential-testing")) {
    JS::SetSupportDifferentialTesting(true);
  }
#endif

  if (op->getBoolOption("disable-oom-functions")) {
    disableOOMFunctions = true;
  }

  if (op->getBoolOption("more-compartments")) {
    defaultToSameCompartment = false;
  }

  bool reprl_mode = FuzzilliUseReprlMode(op);

  // Begin REPRL Loop
  int result = EXIT_SUCCESS;
  do {
    JS::RealmOptions options;
    SetStandardRealmOptions(options);
    RootedObject glob(
        cx, NewGlobalObject(cx, options, nullptr, ShellGlobalKind::WindowProxy,
                            /* immutablePrototype = */ true));
    if (!glob) {
      return 1;
    }

    JSAutoRealm ar(cx, glob);

    ShellContext* sc = GetShellContext(cx);
    if (!sc->moduleLoader && !InitModuleLoader(cx, *op)) {
      return EXIT_FAILURE;
    }

#ifdef FUZZING_INTERFACES
    if (fuzzHaveModule) {
      return FuzzJSRuntimeStart(cx, &sArgc, &sArgv);
    }
#endif

    sc->exitCode = 0;
    result = EXIT_SUCCESS;
    {
      AutoReportException are(cx);
      if (!ProcessArgs(cx, op) && !sc->quitting) {
        result = EXITCODE_RUNTIME_ERROR;
      }
    }

    /*
     * The job queue must be drained even on error to finish outstanding async
     * tasks before the main thread JSRuntime is torn down. Drain after
     * uncaught exceptions have been reported since draining runs callbacks.
     */
    RunShellJobs(cx);

    // Only if there's no other error, report unhandled rejections.
    if (!result && !sc->exitCode) {
      AutoReportException are(cx);
      if (!ReportUnhandledRejections(cx)) {
        FILE* fp = ErrorFilePointer();
        fputs("Error while printing unhandled rejection\n", fp);
      }
    }

    if (sc->exitCode) {
      result = sc->exitCode;
    }

#ifdef FUZZING_JS_FUZZILLI
    if (reprl_mode) {
      fflush(stdout);
      fflush(stderr);
      // Send return code to parent and reset edge counters.
      int status = (result & 0xff) << 8;
      if (js::SupportDifferentialTesting()) {
        struct {
          int status;
          uint32_t execHash;
          uint32_t execHashInputs;
        } s;
        s.status = status;
        s.execHash = cx->executionHash;
        s.execHashInputs = cx->executionHashInputs;
        MOZ_RELEASE_ASSERT(write(REPRL_CWFD, &s, 12) == 12);
      } else {
        MOZ_RELEASE_ASSERT(write(REPRL_CWFD, &status, 4) == 4);
      }
      __sanitizer_cov_reset_edgeguards();
      cx->executionHash = 1;
      cx->executionHashInputs = 0;
    }
#endif

    if (enableDisassemblyDumps) {
      AutoReportException are(cx);
      if (!js::DumpRealmPCCounts(cx)) {
        result = EXITCODE_OUT_OF_MEMORY;
      }
    }

    // End REPRL loop
  } while (reprl_mode);

  return result;
}

// Used to allocate memory when jemalloc isn't yet initialized.
JS_DECLARE_NEW_METHODS(SystemAlloc_New, malloc, static)

static void SetOutputFile(const char* const envVar, RCFile* defaultOut,
                          RCFile** outFileP) {
  RCFile* outFile;

  const char* outPath = getenv(envVar);
  FILE* newfp;
  if (outPath && *outPath && (newfp = fopen(outPath, "w"))) {
    outFile = SystemAlloc_New<RCFile>(newfp);
  } else {
    outFile = defaultOut;
  }

  if (!outFile) {
    MOZ_CRASH("Failed to allocate output file");
  }

  outFile->acquire();
  *outFileP = outFile;
}

static void PreInit() {
#ifdef XP_WIN
  const char* crash_option = getenv("XRE_NO_WINDOWS_CRASH_DIALOG");
  if (crash_option && crash_option[0] == '1') {
    // Disable the segfault dialog. We want to fail the tests immediately
    // instead of hanging automation.
    UINT newMode = SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX;
    UINT prevMode = SetErrorMode(newMode);
    SetErrorMode(prevMode | newMode);
  }
#endif
}

#ifndef JS_WITHOUT_NSPR
class AutoLibraryLoader {
  Vector<PRLibrary*, 4, SystemAllocPolicy> libraries;

 public:
  ~AutoLibraryLoader() {
    for (auto dll : libraries) {
      PR_UnloadLibrary(dll);
    }
  }

  PRLibrary* load(const char* path) {
    PRLibSpec libSpec;
    libSpec.type = PR_LibSpec_Pathname;
    libSpec.value.pathname = path;
    PRLibrary* dll = PR_LoadLibraryWithFlags(libSpec, PR_LD_NOW | PR_LD_GLOBAL);
    if (!dll) {
      fprintf(stderr, "LoadLibrary '%s' failed with code %d\n", path,
              PR_GetError());
      MOZ_CRASH("Failed to load library");
    }

    MOZ_ALWAYS_TRUE(libraries.append(dll));
    return dll;
  }
};
#endif

static bool ReadSelfHostedXDRFile(JSContext* cx, FileContents& buf) {
  FILE* file = fopen(selfHostedXDRPath, "rb");
  if (!file) {
    fprintf(stderr, "Can't open self-hosted stencil XDR file.\n");
    return false;
  }
  AutoCloseFile autoClose(file);

  struct stat st;
  if (fstat(fileno(file), &st) < 0) {
    fprintf(stderr, "Unable to stat self-hosted stencil XDR file.\n");
    return false;
  }

  if (st.st_size >= INT32_MAX) {
    fprintf(stderr, "self-hosted stencil XDR file too large.\n");
    return false;
  }
  uint32_t filesize = uint32_t(st.st_size);

  if (!buf.growBy(filesize)) {
    return false;
  }
  size_t cc = fread(buf.begin(), 1, filesize, file);
  if (cc != filesize) {
    fprintf(stderr, "Short read on self-hosted stencil XDR file.\n");
    return false;
  }

  return true;
}

static bool WriteSelfHostedXDRFile(JSContext* cx, JS::SelfHostedCache buffer) {
  FILE* file = fopen(selfHostedXDRPath, "wb");
  if (!file) {
    JS_ReportErrorUTF8(cx, "Can't open self-hosted stencil XDR file.");
    return false;
  }
  AutoCloseFile autoClose(file);

  size_t cc = fwrite(buffer.Elements(), 1, buffer.LengthBytes(), file);
  if (cc != buffer.LengthBytes()) {
    JS_ReportErrorUTF8(cx, "Short write on self-hosted stencil XDR file.");
    return false;
  }

  return true;
}

static bool SetGCParameterFromArg(JSContext* cx, char* arg) {
  char* c = strchr(arg, '=');
  if (!c) {
    fprintf(stderr,
            "Error: --gc-param argument '%s' must be of the form "
            "name=decimalValue\n",
            arg);
    return false;
  }

  *c = '\0';
  const char* name = arg;
  const char* valueStr = c + 1;

  JSGCParamKey key;
  bool writable;
  if (!GetGCParameterInfo(name, &key, &writable)) {
    fprintf(stderr, "Error: Unknown GC parameter name '%s'\n", name);
    fprintf(stderr, "Writable GC parameter names are:\n");
#define PRINT_WRITABLE_PARAM_NAME(name, _, writable) \
  if (writable) {                                    \
    fprintf(stderr, "  %s\n", name);                 \
  }
    FOR_EACH_GC_PARAM(PRINT_WRITABLE_PARAM_NAME)
#undef PRINT_WRITABLE_PARAM_NAME
    return false;
  }

  if (!writable) {
    fprintf(stderr, "Error: GC parameter '%s' is not writable\n", name);
    return false;
  }

  char* end = nullptr;
  unsigned long int value = strtoul(valueStr, &end, 10);
  if (end == valueStr || *end) {
    fprintf(stderr,
            "Error: Could not parse '%s' as decimal for GC parameter '%s'\n",
            valueStr, name);
    return false;
  }

  uint32_t paramValue = uint32_t(value);
  if (value == ULONG_MAX || value != paramValue ||
      !cx->runtime()->gc.setParameter(cx, key, paramValue)) {
    fprintf(stderr, "Error: Value %s is out of range for GC parameter '%s'\n",
            valueStr, name);
    return false;
  }

  return true;
}

int main(int argc, char** argv) {
  PreInit();

  sArgc = argc;
  sArgv = argv;

  int result;

  setlocale(LC_ALL, "");

  // Special-case stdout and stderr. We bump their refcounts to prevent them
  // from getting closed and then having some printf fail somewhere.
  RCFile rcStdout(stdout);
  rcStdout.acquire();
  RCFile rcStderr(stderr);
  rcStderr.acquire();

  SetOutputFile("JS_STDOUT", &rcStdout, &gOutFile);
  SetOutputFile("JS_STDERR", &rcStderr, &gErrFile);

#ifdef MOZ_MEMORY
  // Use a larger jemalloc page cache. This should match the value for browser
  // foreground processes in ContentChild::RecvNotifyProcessPriorityChanged.
  moz_set_max_dirty_page_modifier(4);
#endif

  OptionParser op("Usage: {progname} [options] [[script] scriptArgs*]");
  if (!InitOptionParser(op)) {
    return EXIT_FAILURE;
  }

  switch (op.parseArgs(argc, argv)) {
    case OptionParser::EarlyExit:
      return EXIT_SUCCESS;
    case OptionParser::ParseError:
      op.printHelp(argv[0]);
      return EXIT_FAILURE;
    case OptionParser::Fail:
      return EXIT_FAILURE;
    case OptionParser::Okay:
      break;
  }

  if (op.getHelpOption()) {
    return EXIT_SUCCESS;
  }

  if (!SetGlobalOptionsPreJSInit(op)) {
    return EXIT_FAILURE;
  }

  // Start the engine.
  if (const char* message = JS_InitWithFailureDiagnostic()) {
    fprintf(gErrFile->fp, "JS_Init failed: %s\n", message);
    return 1;
  }

  // `selfHostedXDRBuffer` contains XDR buffer of the self-hosted JS.
  // A part of it is borrowed by ImmutableScriptData of the self-hosted scripts.
  //
  // This buffer should outlive JS_Shutdown.
  Maybe<FileContents> selfHostedXDRBuffer;

  auto shutdownEngine = MakeScopeExit([] { JS_ShutDown(); });

  if (!SetGlobalOptionsPostJSInit(op)) {
    return EXIT_FAILURE;
  }

  if (!InitSharedObjectMailbox()) {
    return EXIT_FAILURE;
  }

  JS::SetProcessBuildIdOp(ShellBuildId);

  /* Use the same parameters as the browser in xpcjsruntime.cpp. */
  JSContext* const cx = JS_NewContext(JS::DefaultHeapMaxBytes);
  if (!cx) {
    return 1;
  }

  if (!JS::SetLoggingInterface(shellLoggingInterface)) {
    return 1;
  }
  ParseLoggerOptions();

  // Register telemetry callbacks.
  JS_SetAccumulateTelemetryCallback(cx, AccumulateTelemetryDataCallback);
  JS_SetSetUseCounterCallback(cx, SetUseCounterCallback);

  auto destroyCx = MakeScopeExit([cx] { JS_DestroyContext(cx); });

  UniquePtr<ShellContext> sc =
      MakeUnique<ShellContext>(cx, ShellContext::MainThread);
  if (!sc || !sc->registerWithCx(cx)) {
    return 1;
  }

  if (!SetContextOptions(cx, op)) {
    return 1;
  }

  JS_SetTrustedPrincipals(cx, &ShellPrincipals::fullyTrusted);
  JS_SetSecurityCallbacks(cx, &ShellPrincipals::securityCallbacks);

  JS_AddInterruptCallback(cx, ShellInterruptCallback);

  JS::SetGCSliceCallback(cx, GCSliceCallback);

  bufferStreamState = js_new<ExclusiveWaitableData<BufferStreamState>>(
      mutexid::BufferStreamState);
  if (!bufferStreamState) {
    return 1;
  }
  auto shutdownBufferStreams = MakeScopeExit([] {
    ShutdownBufferStreams();
    js_delete(bufferStreamState);
  });
  JS::InitConsumeStreamCallback(cx, ConsumeBufferSource, ReportStreamError);

  JS::SetPromiseRejectionTrackerCallback(
      cx, ForwardingPromiseRejectionTrackerCallback);

  JS::dbg::SetDebuggerMallocSizeOf(cx, moz_malloc_size_of);

  auto shutdownShellThreads = MakeScopeExit([cx] {
    KillWatchdog(cx);
    KillWorkerThreads(cx);
    DestructSharedObjectMailbox();
    CancelOffThreadJobsForRuntime(cx);
  });

  // The file content should stay alive as long as Worker thread can be
  // initialized.
  JS::SelfHostedCache xdrSpan = nullptr;
  JS::SelfHostedWriter xdrWriter = nullptr;
  if (selfHostedXDRPath) {
    if (encodeSelfHostedCode) {
      xdrWriter = WriteSelfHostedXDRFile;
    } else {
      selfHostedXDRBuffer.emplace(cx);
      if (ReadSelfHostedXDRFile(cx, *selfHostedXDRBuffer)) {
        MOZ_ASSERT(selfHostedXDRBuffer->length() > 0);
        JS::SelfHostedCache span(selfHostedXDRBuffer->begin(),
                                 selfHostedXDRBuffer->end());
        xdrSpan = span;
      } else {
        fprintf(stderr, "Falling back on parsing source.\n");
        selfHostedXDRPath = nullptr;
      }
    }
  }

  if (!JS::InitSelfHostedCode(cx, xdrSpan, xdrWriter)) {
    return 1;
  }

  EnvironmentPreparer environmentPreparer(cx);

  JS::SetProcessLargeAllocationFailureCallback(my_LargeAllocFailCallback);

  if (op.getBoolOption("wasm-compile-and-serialize")) {
#ifdef __wasi__
    MOZ_CRASH("WASI doesn't support wasm");
#else
    if (!WasmCompileAndSerialize(cx)) {
      // Errors have been printed directly to stderr.
      MOZ_ASSERT(!cx->isExceptionPending());
      return EXIT_FAILURE;
    }
#endif
    return EXIT_SUCCESS;
  }

  result = Shell(cx, &op);

#ifdef DEBUG
  if (OOM_printAllocationCount) {
    printf("OOM max count: %" PRIu64 "\n", js::oom::simulator.counter());
  }
#endif

  return result;
}

bool InitOptionParser(OptionParser& op) {
  op.setDescription(
      "The SpiderMonkey shell provides a command line interface to the "
      "JavaScript engine. Code and file options provided via the command line "
      "are "
      "run left to right. If provided, the optional script argument is run "
      "after "
      "all options have been processed. Just-In-Time compilation modes may be "
      "enabled via "
      "command line options.");
  op.setDescriptionWidth(72);
  op.setHelpWidth(80);
  op.setVersion(JS_GetImplementationVersion());

  if (!op.addMultiStringOption(
          'f', "file", "PATH",
          "File path to run, parsing file contents as UTF-8") ||
      !op.addMultiStringOption(
          'u', "utf16-file", "PATH",
          "File path to run, inflating the file's UTF-8 contents to UTF-16 and "
          "then parsing that") ||
      !op.addMultiStringOption('m', "module", "PATH", "Module path to run") ||
      !op.addMultiStringOption('p', "prelude", "PATH", "Prelude path to run") ||
      !op.addMultiStringOption('e', "execute", "CODE", "Inline code to run") ||
      !op.addStringOption('\0', "selfhosted-xdr-path", "[filename]",
                          "Read/Write selfhosted script data from/to the given "
                          "XDR file") ||
      !op.addStringOption('\0', "selfhosted-xdr-mode", "(encode,decode,off)",
                          "Whether to encode/decode data of the file provided"
                          "with --selfhosted-xdr-path.") ||
      !op.addBoolOption('i', "shell", "Enter prompt after running code") ||
      !op.addBoolOption('c', "compileonly",
                        "Only compile, don't run (syntax checking mode)") ||
      !op.addBoolOption('w', "warnings", "Emit warnings") ||
      !op.addBoolOption('W', "nowarnings", "Don't emit warnings") ||
      !op.addBoolOption('D', "dump-bytecode",
                        "Dump bytecode with exec count for all scripts") ||
      !op.addBoolOption('b', "print-timing",
                        "Print sub-ms runtime for each file that's run") ||
      !op.addBoolOption('\0', "code-coverage",
                        "Enable code coverage instrumentation.") ||
      !op.addBoolOption(
          '\0', "disable-parser-deferred-alloc",
          "Disable deferred allocation of GC objects until after parser") ||
#ifdef DEBUG
      !op.addBoolOption('O', "print-alloc",
                        "Print the number of allocations at exit") ||
#endif
      !op.addOptionalStringArg("script",
                               "A script to execute (after all options)") ||
      !op.addOptionalMultiStringArg(
          "scriptArgs",
          "String arguments to bind as |scriptArgs| in the "
          "shell's global") ||
      !op.addIntOption(
          '\0', "cpu-count", "COUNT",
          "Set the number of CPUs (hardware threads) to COUNT, the "
          "default is the actual number of CPUs. The total number of "
          "background helper threads is the CPU count plus some constant.",
          -1) ||
      !op.addIntOption('\0', "thread-count", "COUNT", "Alias for --cpu-count.",
                       -1) ||
      !op.addBoolOption('\0', "ion", "Enable IonMonkey (default)") ||
      !op.addBoolOption('\0', "no-ion", "Disable IonMonkey") ||
      !op.addBoolOption('\0', "no-ion-for-main-context",
                        "Disable IonMonkey for the main context only") ||
      !op.addIntOption('\0', "inlining-entry-threshold", "COUNT",
                       "The minimum stub entry count before trial-inlining a"
                       " call",
                       -1) ||
      !op.addIntOption('\0', "small-function-length", "COUNT",
                       "The maximum bytecode length of a 'small function' for "
                       "the purpose of inlining.",
                       -1) ||
      !op.addBoolOption('\0', "only-inline-selfhosted",
                        "Only inline selfhosted functions") ||
      !op.addBoolOption('\0', "no-asmjs", "Disable asm.js compilation") ||
      !op.addStringOption(
          '\0', "wasm-compiler", "[option]",
          "Choose to enable a subset of the wasm compilers, valid options are "
          "'none', 'baseline', 'ion', 'optimizing', "
          "'baseline+ion', 'baseline+optimizing'.") ||
      !op.addBoolOption('\0', "wasm-verbose",
                        "Enable WebAssembly verbose logging") ||
      !op.addBoolOption('\0', "disable-wasm-huge-memory",
                        "Disable WebAssembly huge memory") ||
      !op.addBoolOption('\0', "test-wasm-await-tier2",
                        "Forcibly activate tiering and block "
                        "instantiation on completion of tier2") ||
      !op.addBoolOption('\0', "no-native-regexp",
                        "Disable native regexp compilation") ||
      !op.addIntOption(
          '\0', "regexp-warmup-threshold", "COUNT",
          "Wait for COUNT invocations before compiling regexps to native code "
          "(default 10)",
          -1) ||
      !op.addBoolOption('\0', "trace-regexp-parser", "Trace regexp parsing") ||
      !op.addBoolOption('\0', "trace-regexp-assembler",
                        "Trace regexp assembler") ||
      !op.addBoolOption('\0', "trace-regexp-interpreter",
                        "Trace regexp interpreter") ||
      !op.addBoolOption('\0', "trace-regexp-peephole",
                        "Trace regexp peephole optimization") ||
      !op.addBoolOption('\0', "less-debug-code",
                        "Emit less machine code for "
                        "checking assertions under DEBUG.") ||
      !op.addBoolOption('\0', "disable-tosource", "Disable toSource/uneval") ||
      !op.addBoolOption('\0', "disable-property-error-message-fix",
                        "Disable fix for the error message when accessing "
                        "property of null or undefined") ||
      !op.addBoolOption('\0', "enable-iterator-helpers",
                        "Enable iterator helpers") ||
      !op.addBoolOption('\0', "enable-async-iterator-helpers",
                        "Enable async iterator helpers") ||
      !op.addBoolOption('\0', "enable-json-parse-with-source",
                        "Enable JSON.parse with source") ||
      !op.addBoolOption('\0', "enable-shadow-realms", "Enable ShadowRealms") ||
      !op.addBoolOption('\0', "disable-array-grouping",
                        "Disable Object.groupBy and Map.groupBy") ||
      !op.addBoolOption('\0', "enable-symbols-as-weakmap-keys",
                        "Enable Symbols As WeakMap keys") ||
      !op.addBoolOption('\0', "enable-uint8array-base64",
                        "Enable Uint8Array base64/hex methods") ||
      !op.addBoolOption('\0', "enable-regexp-duplicate-named-groups",
                        "Enable Duplicate Named Capture Groups") ||
      !op.addBoolOption('\0', "enable-regexp-modifiers",
                        "Enable Pattern Modifiers") ||
      !op.addBoolOption('\0', "enable-regexp-escape", "Enable RegExp.escape") ||
      !op.addBoolOption('\0', "enable-top-level-await",
                        "Enable top-level await") ||
      !op.addBoolOption('\0', "enable-import-attributes",
                        "Enable import attributes") ||
      !op.addStringOption('\0', "shared-memory", "on/off",
                          "SharedArrayBuffer and Atomics "
#if SHARED_MEMORY_DEFAULT
                          "(default: on, off to disable)"
#else
                          "(default: off, on to enable)"
#endif
                          ) ||
      !op.addStringOption('\0', "spectre-mitigations", "on/off",
                          "Whether Spectre mitigations are enabled (default: "
                          "off, on to enable)") ||
      !op.addStringOption('\0', "write-protect-code", "on/off",
                          "Whether the W^X policy is enforced to mark JIT code "
                          "pages as either writable or executable but never "
                          "both at the same time (default: on, off to "
                          "disable)") ||
      !op.addStringOption('\0', "cache-ir-stubs", "on/off/call",
                          "Use CacheIR stubs (default: on, off to disable, "
                          "call to enable work-in-progress call ICs)") ||
      !op.addStringOption('\0', "ion-shared-stubs", "on/off",
                          "Use shared stubs (default: on, off to disable)") ||
      !op.addStringOption('\0', "ion-scalar-replacement", "on/off",
                          "Scalar Replacement (default: on, off to disable)") ||
      !op.addStringOption('\0', "ion-gvn", "[mode]",
                          "Specify Ion global value numbering:\n"
                          "  off: disable GVN\n"
                          "  on:  enable GVN (default)\n") ||
      !op.addStringOption(
          '\0', "ion-licm", "on/off",
          "Loop invariant code motion (default: on, off to disable)") ||
      !op.addStringOption('\0', "ion-edgecase-analysis", "on/off",
                          "Find edge cases where Ion can avoid bailouts "
                          "(default: on, off to disable)") ||
      !op.addStringOption('\0', "ion-pruning", "on/off",
                          "Branch pruning (default: on, off to disable)") ||
      !op.addStringOption('\0', "ion-range-analysis", "on/off",
                          "Range analysis (default: on, off to disable)") ||
      !op.addStringOption('\0', "ion-sink", "on/off",
                          "Sink code motion (default: off, on to enable)") ||
      !op.addStringOption(
          '\0', "ion-instruction-reordering", "on/off",
          "Instruction reordering (default: off, on to enable)") ||
      !op.addStringOption(
          '\0', "ion-optimize-shapeguards", "on/off",
          "Eliminate redundant shape guards (default: on, off to disable)") ||
      !op.addStringOption(
          '\0', "ion-optimize-gcbarriers", "on/off",
          "Eliminate redundant GC barriers (default: on, off to disable)") ||
      !op.addStringOption('\0', "ion-iterator-indices", "on/off",
                          "Optimize property access in for-in loops "
                          "(default: on, off to disable)") ||
      !op.addStringOption('\0', "ion-load-keys", "on/off",
                          "Atomize property loads used as keys "
                          "(default: on, off to disable)") ||
      !op.addBoolOption('\0', "ion-check-range-analysis",
                        "Range analysis checking") ||
      !op.addBoolOption('\0', "ion-extra-checks",
                        "Perform extra dynamic validation checks") ||
      !op.addStringOption(
          '\0', "ion-inlining", "on/off",
          "Inline methods where possible (default: on, off to disable)") ||
      !op.addStringOption(
          '\0', "ion-osr", "on/off",
          "On-Stack Replacement (default: on, off to disable)") ||
      !op.addBoolOption('\0', "disable-bailout-loop-check",
                        "Turn off bailout loop check") ||
      !op.addBoolOption('\0', "enable-ic-frame-pointers",
                        "Use frame pointers in all IC stubs") ||
      !op.addBoolOption('\0', "scalar-replace-arguments",
                        "Use scalar replacement to optimize ArgumentsObject") ||
      !op.addStringOption(
          '\0', "ion-limit-script-size", "on/off",
          "Don't compile very large scripts (default: on, off to disable)") ||
      !op.addIntOption('\0', "ion-warmup-threshold", "COUNT",
                       "Wait for COUNT calls or iterations before compiling "
                       "at the normal optimization level (default: 1000)",
                       -1) ||
      !op.addStringOption(
          '\0', "ion-regalloc", "[mode]",
          "Specify Ion register allocation:\n"
          "  backtracking: Priority based backtracking register allocation "
          "(default)\n"
          "  testbed: Backtracking allocator with experimental features\n"
          "  stupid: Simple block local register allocation") ||
      !op.addBoolOption(
          '\0', "ion-eager",
          "Always ion-compile methods (implies --baseline-eager)") ||
      !op.addBoolOption('\0', "fast-warmup",
                        "Reduce warmup thresholds for each tier.") ||
      !op.addStringOption(
          '\0', "baseline-offthread-compile", "on/off",
          "Compile baseline scripts offthread (default: off)") ||
      !op.addStringOption(
          '\0', "baseline-batching", "on/off",
          "Batch baseline scripts before dispatching (default: off)") ||
      !op.addStringOption('\0', "ion-offthread-compile", "on/off",
                          "Compile Ion scripts offthread (default: on)") ||
      !op.addStringOption('\0', "ion-parallel-compile", "on/off",
                          "--ion-parallel compile is deprecated. Use "
                          "--ion-offthread-compile.") ||
      !op.addBoolOption('\0', "disable-main-thread-denormals",
                        "Disable Denormals on the main thread only, to "
                        "emulate WebAudio worklets.") ||
      !op.addBoolOption('\0', "baseline",
                        "Enable baseline compiler (default)") ||
      !op.addBoolOption('\0', "no-baseline", "Disable baseline compiler") ||
      !op.addBoolOption('\0', "baseline-eager",
                        "Always baseline-compile methods") ||
#ifdef ENABLE_PORTABLE_BASELINE_INTERP
      !op.addBoolOption('\0', "portable-baseline-eager",
                        "Always use the porbale baseline interpreter") ||
      !op.addBoolOption('\0', "portable-baseline",
                        "Enable Portable Baseline Interpreter (default)") ||
      !op.addBoolOption('\0', "no-portable-baseline",
                        "Disable Portable Baseline Interpreter") ||
#endif
#ifdef ENABLE_JS_AOT_ICS
      !op.addBoolOption('\0', "aot-ics", "Enable ahead-of-time-known ICs") ||
      !op.addBoolOption(
          '\0', "enforce-aot-ics",
          "Enable enforcing only use of ahead-of-time-known ICs") ||
#endif
      !op.addIntOption(
          '\0', "baseline-warmup-threshold", "COUNT",
          "Wait for COUNT calls or iterations before baseline-compiling "
          "(default: 10)",
          -1) ||
      !op.addIntOption(
          '\0', "baseline-queue-capacity", "CAPACITY",
          "Wait for at most CAPACITY scripts to be added to baseline compile"
          "queue before dispatching (default: 8)",
          -1) ||
      !op.addBoolOption('\0', "blinterp",
                        "Enable Baseline Interpreter (default)") ||
      !op.addBoolOption('\0', "no-blinterp", "Disable Baseline Interpreter") ||
      !op.addBoolOption('\0', "disable-jithints",
                        "Disable caching eager baseline compilation hints.") ||
      !op.addBoolOption(
          '\0', "emit-interpreter-entry",
          "Emit Interpreter entry trampolines (default under --enable-perf)") ||
      !op.addBoolOption(
          '\0', "no-emit-interpreter-entry",
          "Do not emit Interpreter entry trampolines (default).") ||
      !op.addBoolOption('\0', "blinterp-eager",
                        "Always Baseline-interpret scripts") ||
      !op.addIntOption(
          '\0', "blinterp-warmup-threshold", "COUNT",
          "Wait for COUNT calls or iterations before Baseline-interpreting "
          "(default: 10)",
          -1) ||
      !op.addIntOption(
          '\0', "trial-inlining-warmup-threshold", "COUNT",
          "Wait for COUNT calls or iterations before trial-inlining "
          "(default: 500)",
          -1) ||
      !op.addStringOption(
          '\0', "monomorphic-inlining", "default/always/never",
          "Whether monomorphic inlining is used instead of trial inlining "
          "always, never, or based on heuristics (default)") ||
      !op.addBoolOption(
          '\0', "no-sse3",
          "Pretend CPU does not support SSE3 instructions and above "
          "to test JIT codegen (no-op on platforms other than x86 and x64).") ||
      !op.addBoolOption(
          '\0', "no-ssse3",
          "Pretend CPU does not support SSSE3 [sic] instructions and above "
          "to test JIT codegen (no-op on platforms other than x86 and x64).") ||
      !op.addBoolOption(
          '\0', "no-sse41",
          "Pretend CPU does not support SSE4.1 instructions "
          "to test JIT codegen (no-op on platforms other than x86 and x64).") ||
      !op.addBoolOption('\0', "no-sse4", "Alias for --no-sse41") ||
      !op.addBoolOption(
          '\0', "no-sse42",
          "Pretend CPU does not support SSE4.2 instructions "
          "to test JIT codegen (no-op on platforms other than x86 and x64).") ||
#ifdef ENABLE_WASM_AVX
      !op.addBoolOption('\0', "enable-avx",
                        "No-op. AVX is enabled by default, if available.") ||
      !op.addBoolOption(
          '\0', "no-avx",
          "Pretend CPU does not support AVX or AVX2 instructions "
          "to test JIT codegen (no-op on platforms other than x86 and x64).") ||
#else
      !op.addBoolOption('\0', "enable-avx",
                        "AVX is disabled by default. Enable AVX. "
                        "(no-op on platforms other than x86 and x64).") ||
      !op.addBoolOption('\0', "no-avx",
                        "No-op. AVX is currently disabled by default.") ||
#endif
      !op.addBoolOption('\0', "more-compartments",
                        "Make newGlobal default to creating a new "
                        "compartment.") ||
      !op.addBoolOption('\0', "fuzzing-safe",
                        "Don't expose functions that aren't safe for "
                        "fuzzers to call") ||
#ifdef DEBUG
      !op.addBoolOption('\0', "differential-testing",
                        "Avoid random/undefined behavior that disturbs "
                        "differential testing (correctness fuzzing)") ||
#endif
      !op.addBoolOption('\0', "disable-oom-functions",
                        "Disable functions that cause "
                        "artificial OOMs") ||
      !op.addBoolOption('\0', "no-threads", "Disable helper threads") ||
      !op.addBoolOption(
          '\0', "no-jit-backend",
          "Disable the JIT backend completely for this process") ||
#ifdef DEBUG
      !op.addBoolOption('\0', "dump-entrained-variables",
                        "Print variables which are "
                        "unnecessarily entrained by inner functions") ||
#endif
      !op.addBoolOption('\0', "no-ggc", "Disable Generational GC") ||
      !op.addBoolOption('\0', "no-cgc", "Disable Compacting GC") ||
      !op.addBoolOption('\0', "no-incremental-gc", "Disable Incremental GC") ||
      !op.addBoolOption('\0', "no-parallel-marking",
                        "Disable GC parallel marking") ||
      !op.addBoolOption('\0', "enable-parallel-marking",
                        "Enable GC parallel marking") ||
      !op.addStringOption('\0', "nursery-strings", "on/off",
                          "Allocate strings in the nursery") ||
      !op.addStringOption('\0', "nursery-bigints", "on/off",
                          "Allocate BigInts in the nursery") ||
      !op.addIntOption('\0', "available-memory", "SIZE",
                       "Select GC settings based on available memory (MB)",
                       0) ||
      !op.addBoolOption('\0', "disable-decommit",
                        "Disable decommitting unsued GC memory") ||
      !op.addStringOption('\0', "arm-hwcap", "[features]",
                          "Specify ARM code generation features, or 'help' to "
                          "list all features.") ||
      !op.addIntOption('\0', "arm-asm-nop-fill", "SIZE",
                       "Insert the given number of NOP instructions at all "
                       "possible pool locations.",
                       0) ||
      !op.addIntOption('\0', "asm-pool-max-offset", "OFFSET",
                       "The maximum pc relative OFFSET permitted in pool "
                       "reference instructions.",
                       1024) ||
      !op.addBoolOption('\0', "arm-sim-icache-checks",
                        "Enable icache flush checks in the ARM "
                        "simulator.") ||
      !op.addIntOption('\0', "arm-sim-stop-at", "NUMBER",
                       "Stop the ARM simulator after the given "
                       "NUMBER of instructions.",
                       -1) ||
      !op.addBoolOption('\0', "mips-sim-icache-checks",
                        "Enable icache flush checks in the MIPS "
                        "simulator.") ||
      !op.addIntOption('\0', "mips-sim-stop-at", "NUMBER",
                       "Stop the MIPS simulator after the given "
                       "NUMBER of instructions.",
                       -1) ||
      !op.addBoolOption('\0', "loong64-sim-icache-checks",
                        "Enable icache flush checks in the LoongArch64 "
                        "simulator.") ||
      !op.addIntOption('\0', "loong64-sim-stop-at", "NUMBER",
                       "Stop the LoongArch64 simulator after the given "
                       "NUMBER of instructions.",
                       -1) ||
#ifdef JS_CODEGEN_RISCV64
      !op.addBoolOption('\0', "riscv-debug", "debug print riscv info.") ||
#endif
#ifdef JS_SIMULATOR_RISCV64
      !op.addBoolOption('\0', "trace-sim", "print simulator info.") ||
      !op.addBoolOption('\0', "debug-sim", "debug simulator.") ||
      !op.addBoolOption('\0', "riscv-trap-to-simulator-debugger",
                        "trap into simulator debuggger.") ||
      !op.addIntOption('\0', "riscv-sim-stop-at", "NUMBER",
                       "Stop the riscv simulator after the given "
                       "NUMBER of instructions.",
                       -1) ||
#endif
      !op.addIntOption('\0', "nursery-size", "SIZE-MB",
                       "Set the maximum nursery size in MB",
                       JS::DefaultNurseryMaxBytes / 1024 / 1024) ||
#ifdef JS_GC_ZEAL
      !op.addStringOption('z', "gc-zeal", "LEVEL(;LEVEL)*[,N]",
                          gc::ZealModeHelpText) ||
#else
      !op.addStringOption('z', "gc-zeal", "LEVEL(;LEVEL)*[,N]",
                          "option ignored in non-gc-zeal builds") ||
#endif
      !op.addMultiStringOption('\0', "gc-param", "NAME=VALUE",
                               "Set a named GC parameter") ||
      !op.addStringOption('\0', "module-load-path", "DIR",
                          "Set directory to load modules from") ||
      !op.addBoolOption('\0', "no-source-pragmas",
                        "Disable source(Mapping)URL pragma parsing") ||
      !op.addBoolOption('\0', "no-async-stacks", "Disable async stacks") ||
      !op.addBoolOption('\0', "async-stacks-capture-debuggee-only",
                        "Limit async stack capture to only debuggees") ||
      !op.addMultiStringOption('\0', "dll", "LIBRARY",
                               "Dynamically load LIBRARY") ||
      !op.addBoolOption('\0', "suppress-minidump",
                        "Suppress crash minidumps") ||
      !op.addStringOption(
          '\0', "delazification-mode", "[option]",
          "Select one of the delazification mode for scripts given on the "
          "command line, valid options are: "
          "'on-demand', 'concurrent-df', 'eager', 'concurrent-df+on-demand'. "
          "Choosing 'concurrent-df+on-demand' will run both concurrent-df and "
          "on-demand delazification mode, and compare compilation outcome. ") ||
      !op.addBoolOption('\0', "wasm-compile-and-serialize",
                        "Compile the wasm bytecode from stdin and serialize "
                        "the results to stdout") ||
#ifdef FUZZING_JS_FUZZILLI
      !op.addBoolOption('\0', "reprl", "Enable REPRL mode for fuzzing") ||
#endif
      !op.addMultiStringOption('P', "setpref", "name[=val]",
                               "Set the value of a JS pref. The value may "
                               "be omitted for boolean prefs, in which case "
                               "they default to true. Use --list-prefs "
                               "to print all pref names.") ||
      !op.addBoolOption(
          '\0', "list-prefs",
          "Print list of prefs that can be set with --setpref.") ||
      !op.addBoolOption('\0', "use-fdlibm-for-sin-cos-tan",
                        "Use fdlibm for Math.sin, Math.cos, and Math.tan") ||
      !op.addBoolOption('\0', "wasm-gc", "Enable WebAssembly gc proposal.") ||
      !op.addBoolOption('\0', "wasm-relaxed-simd",
                        "Enable WebAssembly relaxed-simd proposal.") ||
      !op.addBoolOption('\0', "wasm-multi-memory",
                        "Enable WebAssembly multi-memory proposal.") ||
      !op.addBoolOption('\0', "wasm-memory-control",
                        "Enable WebAssembly memory-control proposal.") ||
      !op.addBoolOption('\0', "wasm-memory64",
                        "Enable WebAssembly memory64 proposal.") ||
      !op.addBoolOption('\0', "wasm-tail-calls",
                        "Enable WebAssembly tail-calls proposal.") ||
      !op.addBoolOption('\0', "wasm-js-string-builtins",
                        "Enable WebAssembly js-string-builtins proposal.") ||
      !op.addBoolOption('\0', "enable-promise-try", "Enable Promise.try") ||
      !op.addBoolOption('\0', "enable-iterator-sequencing",
                        "Enable Iterator Sequencing") ||
      !op.addBoolOption('\0', "enable-math-sumprecise",
                        "Enable Math.sumPrecise") ||
      !op.addBoolOption('\0', "enable-error-iserror", "Enable Error.isError") ||
      !op.addBoolOption('\0', "enable-iterator-range",
                        "Enable Iterator.range") ||
      !op.addBoolOption('\0', "enable-joint-iteration",
                        "Enable Joint Iteration") ||
      !op.addBoolOption('\0', "enable-atomics-pause", "Enable Atomics pause") ||
      !op.addBoolOption('\0', "enable-explicit-resource-management",
                        "Enable Explicit Resource Management") ||
      !op.addBoolOption('\0', "disable-explicit-resource-management",
                        "Disable Explicit Resource Management") ||
      !op.addBoolOption('\0', "enable-temporal", "Enable Temporal") ||
      !op.addBoolOption('\0', "enable-upsert", "Enable Upsert proposal")) {
    return false;
  }

  op.setArgTerminatesOptions("script", true);
  op.setArgCapturesRest("scriptArgs");

  // If --fuzzing-safe is used, print a warning for unknown shell flags instead
  // of aborting execution.
  op.setIgnoresUnknownOptions("fuzzing-safe", true);

  return true;
}

bool SetGlobalOptionsPreJSInit(const OptionParser& op) {
  if (op.getBoolOption("fuzzing-safe")) {
    fuzzingSafe = true;
  } else {
    fuzzingSafe =
        (getenv("MOZ_FUZZING_SAFE") && getenv("MOZ_FUZZING_SAFE")[0] != '0');
  }

  for (MultiStringRange args = op.getMultiStringOption("setpref");
       !args.empty(); args.popFront()) {
    if (!SetPref(args.front())) {
      return false;
    }
  }

  // Override pref values for prefs that have a custom shell flag.
  // If you're adding a new feature, consider using --setpref instead.
  if (op.getBoolOption("enable-shadow-realms")) {
    JS::Prefs::set_experimental_shadow_realms(true);
  }
  if (op.getBoolOption("enable-regexp-duplicate-named-groups")) {
    JS::Prefs::setAtStartup_experimental_regexp_duplicate_named_groups(true);
  }
  if (op.getBoolOption("enable-regexp-modifiers")) {
    JS::Prefs::setAtStartup_experimental_regexp_modifiers(true);
  }
  if (op.getBoolOption("enable-uint8array-base64")) {
    JS::Prefs::setAtStartup_experimental_uint8array_base64(true);
  }
  if (op.getBoolOption("enable-promise-try")) {
    JS::Prefs::setAtStartup_experimental_promise_try(true);
  }
  if (op.getBoolOption("enable-math-sumprecise")) {
    JS::Prefs::setAtStartup_experimental_math_sumprecise(true);
  }
  if (op.getBoolOption("enable-atomics-pause")) {
    JS::Prefs::setAtStartup_experimental_atomics_pause(true);
  }
  if (op.getBoolOption("enable-error-iserror")) {
    JS::Prefs::set_experimental_error_iserror(true);
  }
#ifdef NIGHTLY_BUILD
  if (op.getBoolOption("enable-async-iterator-helpers")) {
    JS::Prefs::setAtStartup_experimental_async_iterator_helpers(true);
  }
  if (op.getBoolOption("enable-symbols-as-weakmap-keys")) {
    JS::Prefs::setAtStartup_experimental_symbols_as_weakmap_keys(true);
  }
  if (op.getBoolOption("enable-iterator-sequencing")) {
    JS::Prefs::setAtStartup_experimental_iterator_sequencing(true);
  }
  if (op.getBoolOption("enable-iterator-range")) {
    JS::Prefs::setAtStartup_experimental_iterator_range(true);
  }
  if (op.getBoolOption("enable-joint-iteration")) {
    JS::Prefs::setAtStartup_experimental_joint_iteration(true);
  }
  if (op.getBoolOption("enable-upsert")) {
    JS::Prefs::setAtStartup_experimental_upsert(true);
  }
#endif
#ifdef ENABLE_EXPLICIT_RESOURCE_MANAGEMENT
  if (op.getBoolOption("enable-explicit-resource-management")) {
    JS::Prefs::set_experimental_explicit_resource_management(true);
  }
  if (op.getBoolOption("disable-explicit-resource-management")) {
    JS::Prefs::set_experimental_explicit_resource_management(false);
  }
#endif
#ifdef JS_HAS_INTL_API
  if (op.getBoolOption("enable-temporal")) {
    JS::Prefs::setAtStartup_experimental_temporal(true);
  }
#endif
  if (op.getBoolOption("enable-json-parse-with-source")) {
    JS::Prefs::set_experimental_json_parse_with_source(true);
  }

  JS::Prefs::setAtStartup_experimental_weakrefs_expose_cleanupSome(true);

  if (op.getBoolOption("disable-property-error-message-fix")) {
    JS::Prefs::setAtStartup_property_error_message_fix(false);
  }

  JS::Prefs::set_use_fdlibm_for_sin_cos_tan(
      op.getBoolOption("use-fdlibm-for-sin-cos-tan"));

  if (op.getBoolOption("wasm-gc") || op.getBoolOption("wasm-relaxed-simd") ||
      op.getBoolOption("wasm-multi-memory") ||
      op.getBoolOption("wasm-memory-control") ||
      op.getBoolOption("wasm-memory64") ||
      op.getBoolOption("wasm-tail-calls") ||
      op.getBoolOption("wasm-js-string-builtins")) {
    fprintf(
        stderr,
        "Wasm shell flags are now using prefs, use -P wasm_feature instead.\n");
    return false;
  }

  if (op.getBoolOption("list-prefs")) {
    ListPrefs();
    return false;
  }

  // Note: DisableJitBackend must be called before JS_InitWithFailureDiagnostic.
  if (op.getBoolOption("no-jit-backend")) {
    JS::DisableJitBackend();
  }

#if defined(JS_CODEGEN_ARM)
  if (const char* str = op.getStringOption("arm-hwcap")) {
    jit::SetARMHwCapFlagsString(str);
  }

  int32_t fill = op.getIntOption("arm-asm-nop-fill");
  if (fill >= 0) {
    jit::Assembler::NopFill = fill;
  }

  int32_t poolMaxOffset = op.getIntOption("asm-pool-max-offset");
  if (poolMaxOffset >= 5 && poolMaxOffset <= 1024) {
    jit::Assembler::AsmPoolMaxOffset = poolMaxOffset;
  }
#endif

  // Fish around in `op` for various important compiler-configuration flags
  // and make sure they get handed on to any child processes we might create.
  // See bug 1700900.  Semantically speaking, this is all rather dubious:
  //
  // * What set of flags need to be propagated in order to guarantee that the
  //   child produces code that is "compatible" (in whatever sense) with that
  //   produced by the parent? This isn't always easy to determine.
  //
  // * There's nothing that ensures that flags given to the child are
  //   presented in the same order that they exist in the parent's `argv[]`.
  //   That could be a problem in the case where two flags with contradictory
  //   meanings are given, and they are presented to the child in the opposite
  //   order.  For example: --wasm-compiler=optimizing --wasm-compiler=baseline.

#if defined(JS_CODEGEN_X86) || defined(JS_CODEGEN_X64)
  MOZ_ASSERT(!js::jit::CPUFlagsHaveBeenComputed());

  if (op.getBoolOption("no-avx")) {
    js::jit::CPUInfo::SetAVXDisabled();
    if (!sCompilerProcessFlags.append("--no-avx")) {
      return false;
    }
  }
  if (op.getBoolOption("enable-avx")) {
    js::jit::CPUInfo::SetAVXEnabled();
    if (!sCompilerProcessFlags.append("--enable-avx")) {
      return false;
    }
  }
  if (op.getBoolOption("no-sse3")) {
    js::jit::CPUInfo::SetSSE3Disabled();
    if (!sCompilerProcessFlags.append("--no-sse3")) {
      return false;
    }
  }
  if (op.getBoolOption("no-ssse3")) {
    js::jit::CPUInfo::SetSSSE3Disabled();
    if (!sCompilerProcessFlags.append("--no-ssse3")) {
      return false;
    }
  }
  if (op.getBoolOption("no-sse4") || op.getBoolOption("no-sse41")) {
    js::jit::CPUInfo::SetSSE41Disabled();
    if (!sCompilerProcessFlags.append("--no-sse41")) {
      return false;
    }
  }
  if (op.getBoolOption("no-sse42")) {
    js::jit::CPUInfo::SetSSE42Disabled();
    if (!sCompilerProcessFlags.append("--no-sse42")) {
      return false;
    }
  }
#endif
#ifndef __wasi__
  if (op.getBoolOption("disable-wasm-huge-memory")) {
    JS::Prefs::setAtStartup_wasm_disable_huge_memory(true);
    if (!sCompilerProcessFlags.append("--disable-wasm-huge-memory")) {
      return false;
    }
  }
#endif

  if (op.getBoolOption("disable-decommit")) {
    gc::DisableDecommit();
  }

  return true;
}

bool SetGlobalOptionsPostJSInit(const OptionParser& op) {
  // Allow dumping on Linux with the fuzzing flag set, even when running with
  // the suid/sgid flag set on the shell.
#ifdef XP_LINUX
  if (op.getBoolOption("fuzzing-safe")) {
    prctl(PR_SET_DUMPABLE, 1);
  }
#endif

#ifdef DEBUG
  /*
   * Process OOM options as early as possible so that we can observe as many
   * allocations as possible.
   */
  OOM_printAllocationCount = op.getBoolOption('O');
#endif

  if (op.getBoolOption("no-threads")) {
    js::DisableExtraThreads();
  }

  enableCodeCoverage = op.getBoolOption("code-coverage");
  if (enableCodeCoverage) {
    js::EnableCodeCoverage();
  }

  // If LCov is enabled, then the default delazification mode should be changed
  // to parse everything eagerly, such that we know the location of every
  // instruction, to report them in the LCov summary, even if there is no uses
  // of these instructions.
  //
  // Note: code coverage can be enabled either using the --code-coverage command
  // line, or the JS_CODE_COVERAGE_OUTPUT_DIR environment variable, which is
  // processed by JS_InitWithFailureDiagnostic.
  if (coverage::IsLCovEnabled()) {
    defaultDelazificationMode =
        JS::DelazificationOption::ParseEverythingEagerly;
  }

  if (const char* xdr = op.getStringOption("selfhosted-xdr-path")) {
    shell::selfHostedXDRPath = xdr;
  }
  if (const char* opt = op.getStringOption("selfhosted-xdr-mode")) {
    if (strcmp(opt, "encode") == 0) {
      shell::encodeSelfHostedCode = true;
    } else if (strcmp(opt, "decode") == 0) {
      shell::encodeSelfHostedCode = false;
    } else if (strcmp(opt, "off") == 0) {
      shell::selfHostedXDRPath = nullptr;
    } else {
      MOZ_CRASH(
          "invalid option value for --selfhosted-xdr-mode, must be "
          "encode/decode");
    }
  }

#ifdef JS_WITHOUT_NSPR
  if (!op.getMultiStringOption("dll").empty()) {
    fprintf(stderr, "Error: --dll requires NSPR support!\n");
    return false;
  }
#else
  AutoLibraryLoader loader;
  MultiStringRange dllPaths = op.getMultiStringOption("dll");
  while (!dllPaths.empty()) {
    char* path = dllPaths.front();
    loader.load(path);
    dllPaths.popFront();
  }
#endif

  if (op.getBoolOption("suppress-minidump")) {
    js::NoteIntentionalCrash();
  }

  // The fake CPU count must be set before initializing the Runtime,
  // which spins up the thread pool.
  int32_t cpuCount = op.getIntOption("cpu-count");  // What we're really setting
  if (cpuCount < 0) {
    cpuCount = op.getIntOption("thread-count");  // Legacy name
  }
  if (cpuCount >= 0 && !SetFakeCPUCount(cpuCount)) {
    return false;
  }

  return true;
}

bool SetContextOptions(JSContext* cx, const OptionParser& op) {
  if (!SetContextWasmOptions(cx, op) || !SetContextJITOptions(cx, op) ||
      !SetContextGCOptions(cx, op)) {
    return false;
  }

  enableSourcePragmas = !op.getBoolOption("no-source-pragmas");
  enableAsyncStacks = !op.getBoolOption("no-async-stacks");
  enableAsyncStackCaptureDebuggeeOnly =
      op.getBoolOption("async-stacks-capture-debuggee-only");
  enableToSource = !op.getBoolOption("disable-tosource");
  enableImportAttributes = op.getBoolOption("enable-import-attributes");
  JS::ContextOptionsRef(cx)
      .setSourcePragmas(enableSourcePragmas)
      .setAsyncStack(enableAsyncStacks)
      .setAsyncStackCaptureDebuggeeOnly(enableAsyncStackCaptureDebuggeeOnly)
      .setImportAttributes(enableImportAttributes);

  if (const char* str = op.getStringOption("shared-memory")) {
    if (strcmp(str, "off") == 0) {
      enableSharedMemory = false;
    } else if (strcmp(str, "on") == 0) {
      enableSharedMemory = true;
    } else {
      return OptionFailure("shared-memory", str);
    }
  }

  reportWarnings = op.getBoolOption('w');
  compileOnly = op.getBoolOption('c');
  printTiming = op.getBoolOption('b');
  enableDisassemblyDumps = op.getBoolOption('D');
  cx->runtime()->profilingScripts =
      enableCodeCoverage || enableDisassemblyDumps;

  if (const char* mode = op.getStringOption("delazification-mode")) {
    if (strcmp(mode, "on-demand") == 0) {
      defaultDelazificationMode = JS::DelazificationOption::OnDemandOnly;
    } else if (strcmp(mode, "concurrent-df") == 0) {
      defaultDelazificationMode =
          JS::DelazificationOption::ConcurrentDepthFirst;
    } else if (strcmp(mode, "eager") == 0) {
      defaultDelazificationMode =
          JS::DelazificationOption::ParseEverythingEagerly;
    } else if (strcmp(mode, "concurrent-df+on-demand") == 0 ||
               strcmp(mode, "on-demand+concurrent-df") == 0) {
      defaultDelazificationMode =
          JS::DelazificationOption::CheckConcurrentWithOnDemand;
    } else {
      return OptionFailure("delazification-mode", mode);
    }
  }

  return true;
}

bool SetContextWasmOptions(JSContext* cx, const OptionParser& op) {
  enableAsmJS = !op.getBoolOption("no-asmjs");

  enableWasm = true;
  enableWasmBaseline = true;
  enableWasmOptimizing = true;

  if (const char* str = op.getStringOption("wasm-compiler")) {
    if (strcmp(str, "none") == 0) {
      enableWasm = false;
    } else if (strcmp(str, "baseline") == 0) {
      MOZ_ASSERT(enableWasmBaseline);
      enableWasmOptimizing = false;
    } else if (strcmp(str, "optimizing") == 0 ||
               strcmp(str, "optimized") == 0) {
      enableWasmBaseline = false;
      MOZ_ASSERT(enableWasmOptimizing);
    } else if (strcmp(str, "baseline+optimizing") == 0 ||
               strcmp(str, "baseline+optimized") == 0) {
      MOZ_ASSERT(enableWasmBaseline);
      MOZ_ASSERT(enableWasmOptimizing);
    } else if (strcmp(str, "ion") == 0) {
      enableWasmBaseline = false;
      enableWasmOptimizing = true;
    } else if (strcmp(str, "baseline+ion") == 0) {
      MOZ_ASSERT(enableWasmBaseline);
      enableWasmOptimizing = true;
    } else {
      return OptionFailure("wasm-compiler", str);
    }
  }

  enableWasmVerbose = op.getBoolOption("wasm-verbose");
  enableTestWasmAwaitTier2 = op.getBoolOption("test-wasm-await-tier2");

  JS::ContextOptionsRef(cx)
      .setAsmJS(enableAsmJS)
      .setWasm(enableWasm)
      .setWasmForTrustedPrinciples(enableWasm)
      .setWasmBaseline(enableWasmBaseline)
      .setWasmIon(enableWasmOptimizing)
      .setTestWasmAwaitTier2(enableTestWasmAwaitTier2);

#ifndef __wasi__
  // Also the following are to be propagated.
  const char* to_propagate[] = {
      // Compiler selection options
      "--test-wasm-await-tier2",
  };
  for (const char* p : to_propagate) {
    if (op.getBoolOption(p + 2 /* 2 => skip the leading '--' */)) {
      if (!sCompilerProcessFlags.append(p)) {
        return false;
      }
    }
  }

  // Also --wasm-compiler= is to be propagated.  This is tricky because it is
  // necessary to reconstitute the --wasm-compiler=<whatever> string from its
  // pieces, without causing a leak.  Hence it is copied into a static buffer.
  // This is thread-unsafe, but we're in `main()` and on the process' root
  // thread.  Also, we do this only once -- it wouldn't work properly if we
  // handled multiple --wasm-compiler= flags in a loop.
  const char* wasm_compiler = op.getStringOption("wasm-compiler");
  if (wasm_compiler) {
    size_t n_needed =
        2 + strlen("wasm-compiler") + 1 + strlen(wasm_compiler) + 1;
    const size_t n_avail = 128;
    static char buf[n_avail];
    // `n_needed` depends on the compiler name specified.  However, it can't
    // be arbitrarily long, since previous flag-checking should have limited
    // it to a set of known possibilities: "baseline", "ion",
    // "baseline+ion",  Still, assert this for safety.
    MOZ_RELEASE_ASSERT(n_needed < n_avail);
    memset(buf, 0, sizeof(buf));
    SprintfBuf(buf, n_avail, "--%s=%s", "wasm-compiler", wasm_compiler);
    if (!sCompilerProcessFlags.append(buf)) {
      return false;
    }
  }
#endif  // __wasi__

  return true;
}

bool SetContextJITOptions(JSContext* cx, const OptionParser& op) {
  // Check --fast-warmup first because it sets default warm-up thresholds. These
  // thresholds can then be overridden below by --ion-eager and other flags.
  if (op.getBoolOption("fast-warmup")) {
    jit::JitOptions.setFastWarmUp();
  }

  if (op.getBoolOption("no-ion-for-main-context")) {
    JS::ContextOptionsRef(cx).setDisableIon();
  }

  if (const char* str = op.getStringOption("cache-ir-stubs")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableCacheIR = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableCacheIR = true;
    } else {
      return OptionFailure("cache-ir-stubs", str);
    }
  }

  if (const char* str = op.getStringOption("spectre-mitigations")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.spectreIndexMasking = true;
      jit::JitOptions.spectreObjectMitigations = true;
      jit::JitOptions.spectreStringMitigations = true;
      jit::JitOptions.spectreValueMasking = true;
      jit::JitOptions.spectreJitToCxxCalls = true;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.spectreIndexMasking = false;
      jit::JitOptions.spectreObjectMitigations = false;
      jit::JitOptions.spectreStringMitigations = false;
      jit::JitOptions.spectreValueMasking = false;
      jit::JitOptions.spectreJitToCxxCalls = false;
    } else {
      return OptionFailure("spectre-mitigations", str);
    }
  }

  if (const char* str = op.getStringOption("write-protect-code")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.maybeSetWriteProtectCode(true);
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.maybeSetWriteProtectCode(false);
    } else {
      return OptionFailure("write-protect-code", str);
    }
  }

  if (const char* str = op.getStringOption("monomorphic-inlining")) {
    if (strcmp(str, "default") == 0) {
      jit::JitOptions.monomorphicInlining =
          jit::UseMonomorphicInlining::Default;
    } else if (strcmp(str, "always") == 0) {
      jit::JitOptions.monomorphicInlining = jit::UseMonomorphicInlining::Always;
    } else if (strcmp(str, "never") == 0) {
      jit::JitOptions.monomorphicInlining = jit::UseMonomorphicInlining::Never;
    } else {
      return OptionFailure("monomorphic-inlining", str);
    }
  }

  if (const char* str = op.getStringOption("ion-scalar-replacement")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableScalarReplacement = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableScalarReplacement = true;
    } else {
      return OptionFailure("ion-scalar-replacement", str);
    }
  }

  if (op.getStringOption("ion-shared-stubs")) {
    // Dead option, preserved for now for potential fuzzer interaction.
  }

  if (const char* str = op.getStringOption("ion-gvn")) {
    if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableGvn = true;
    } else if (strcmp(str, "on") != 0 && strcmp(str, "optimistic") != 0 &&
               strcmp(str, "pessimistic") != 0) {
      // We accept "pessimistic" and "optimistic" as synonyms for "on"
      // for backwards compatibility.
      return OptionFailure("ion-gvn", str);
    }
  }

  if (const char* str = op.getStringOption("ion-licm")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableLicm = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableLicm = true;
    } else {
      return OptionFailure("ion-licm", str);
    }
  }

  if (const char* str = op.getStringOption("ion-edgecase-analysis")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableEdgeCaseAnalysis = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableEdgeCaseAnalysis = true;
    } else {
      return OptionFailure("ion-edgecase-analysis", str);
    }
  }

  if (const char* str = op.getStringOption("ion-pruning")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disablePruning = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disablePruning = true;
    } else {
      return OptionFailure("ion-pruning", str);
    }
  }

  if (const char* str = op.getStringOption("ion-range-analysis")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableRangeAnalysis = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableRangeAnalysis = true;
    } else {
      return OptionFailure("ion-range-analysis", str);
    }
  }

  if (const char* str = op.getStringOption("ion-sink")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableSink = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableSink = true;
    } else {
      return OptionFailure("ion-sink", str);
    }
  }

  if (const char* str = op.getStringOption("ion-optimize-shapeguards")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableRedundantShapeGuards = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableRedundantShapeGuards = true;
    } else {
      return OptionFailure("ion-optimize-shapeguards", str);
    }
  }

  if (const char* str = op.getStringOption("ion-optimize-gcbarriers")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableRedundantGCBarriers = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableRedundantGCBarriers = true;
    } else {
      return OptionFailure("ion-optimize-gcbarriers", str);
    }
  }

  if (const char* str = op.getStringOption("ion-instruction-reordering")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableInstructionReordering = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableInstructionReordering = true;
    } else {
      return OptionFailure("ion-instruction-reordering", str);
    }
  }

  if (op.getBoolOption("ion-check-range-analysis")) {
    jit::JitOptions.checkRangeAnalysis = true;
  }

  if (op.getBoolOption("ion-extra-checks")) {
    jit::JitOptions.runExtraChecks = true;
  }

  if (const char* str = op.getStringOption("ion-inlining")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableInlining = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableInlining = true;
    } else {
      return OptionFailure("ion-inlining", str);
    }
  }

  if (const char* str = op.getStringOption("ion-osr")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.osr = true;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.osr = false;
    } else {
      return OptionFailure("ion-osr", str);
    }
  }

  if (const char* str = op.getStringOption("ion-limit-script-size")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.limitScriptSize = true;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.limitScriptSize = false;
    } else {
      return OptionFailure("ion-limit-script-size", str);
    }
  }

  if (op.getBoolOption("disable-main-thread-denormals")) {
    // This is a simplified version of WebAudio code, which is good enough for
    // fuzzing purposes.
    //
    // See dom/media/webaudio/blink/DenormalDisabler.h#124
#if defined(__GNUC__) && defined(__SSE__) && defined(__x86_64__)
    int savedCSR;
    asm volatile("stmxcsr %0" : "=m"(savedCSR));
    int newCSR = savedCSR | 0x8040;
    asm volatile("ldmxcsr %0" : : "m"(newCSR));
#elif defined(__arm__)
    int savedCSR;
    asm volatile("vmrs %[result], FPSCR" : [result] "=r"(savedCSR));
    // Bit 24 is the flush-to-zero mode control bit. Setting it to 1 flushes
    // denormals to 0.
    int newCSR = savedCSR | (1 << 24);
    asm volatile("vmsr FPSCR, %[src]" : : [src] "r"(newCSR));
#elif defined(__aarch64__)
    int savedCSR;
    asm volatile("mrs %x[result], FPCR" : [result] "=r"(savedCSR));
    // Bit 24 is the flush-to-zero mode control bit. Setting it to 1 flushes
    // denormals to 0.
    int newCSR = savedCSR | (1 << 24);
    asm volatile("msr FPCR, %x[src]" : : [src] "r"(newCSR));
#else
    // Do nothing on other architecture.
#endif
  }

  int32_t warmUpThreshold = op.getIntOption("ion-warmup-threshold");
  if (warmUpThreshold >= 0) {
    jit::JitOptions.setNormalIonWarmUpThreshold(warmUpThreshold);
  }

  warmUpThreshold = op.getIntOption("baseline-warmup-threshold");
  if (warmUpThreshold >= 0) {
    jit::JitOptions.baselineJitWarmUpThreshold = warmUpThreshold;
  }

  warmUpThreshold = op.getIntOption("trial-inlining-warmup-threshold");
  if (warmUpThreshold >= 0) {
    jit::JitOptions.trialInliningWarmUpThreshold = warmUpThreshold;
  }

  warmUpThreshold = op.getIntOption("regexp-warmup-threshold");
  if (warmUpThreshold >= 0) {
    jit::JitOptions.regexpWarmUpThreshold = warmUpThreshold;
  }

  if (op.getBoolOption("baseline-eager")) {
    jit::JitOptions.setEagerBaselineCompilation();
  }

#ifdef ENABLE_PORTABLE_BASELINE_INTERP
  if (op.getBoolOption("portable-baseline-eager")) {
    jit::JitOptions.setEagerPortableBaselineInterpreter();
  }
  if (op.getBoolOption("portable-baseline")) {
    jit::JitOptions.portableBaselineInterpreter = true;
  }
  if (op.getBoolOption("no-portable-baseline")) {
    jit::JitOptions.portableBaselineInterpreter = false;
  }
#endif

#ifdef ENABLE_JS_AOT_ICS
  if (op.getBoolOption("aot-ics")) {
    jit::JitOptions.enableAOTICs = true;
  }
  if (op.getBoolOption("enforce-aot-ics")) {
    jit::JitOptions.enableAOTICEnforce = true;
  }
#endif

  if (op.getBoolOption("blinterp")) {
    jit::JitOptions.baselineInterpreter = true;
  }

  if (op.getBoolOption("no-blinterp")) {
    jit::JitOptions.baselineInterpreter = false;
  }

  if (op.getBoolOption("disable-jithints")) {
    jit::JitOptions.disableJitHints = true;
  }

  if (op.getBoolOption("emit-interpreter-entry")) {
    jit::JitOptions.emitInterpreterEntryTrampoline = true;
  }

  if (op.getBoolOption("no-emit-interpreter-entry")) {
    jit::JitOptions.emitInterpreterEntryTrampoline = false;
  }

  warmUpThreshold = op.getIntOption("blinterp-warmup-threshold");
  if (warmUpThreshold >= 0) {
    jit::JitOptions.baselineInterpreterWarmUpThreshold = warmUpThreshold;
  }

  if (op.getBoolOption("blinterp-eager")) {
    jit::JitOptions.baselineInterpreterWarmUpThreshold = 0;
  }

  if (op.getBoolOption("no-baseline")) {
    jit::JitOptions.baselineJit = false;
  }

  if (op.getBoolOption("no-ion")) {
    jit::JitOptions.ion = false;
  }

  if (op.getBoolOption("no-native-regexp")) {
    jit::JitOptions.nativeRegExp = false;
  }

  if (op.getBoolOption("trace-regexp-parser")) {
    jit::JitOptions.trace_regexp_parser = true;
  }
  if (op.getBoolOption("trace-regexp-assembler")) {
    jit::JitOptions.trace_regexp_assembler = true;
  }
  if (op.getBoolOption("trace-regexp-interpreter")) {
    jit::JitOptions.trace_regexp_bytecodes = true;
  }
  if (op.getBoolOption("trace-regexp-peephole")) {
    jit::JitOptions.trace_regexp_peephole_optimization = true;
  }

  if (op.getBoolOption("less-debug-code")) {
    jit::JitOptions.lessDebugCode = true;
  }

  int32_t inliningEntryThreshold = op.getIntOption("inlining-entry-threshold");
  if (inliningEntryThreshold > 0) {
    jit::JitOptions.inliningEntryThreshold = inliningEntryThreshold;
  }

  int32_t smallFunctionLength = op.getIntOption("small-function-length");
  if (smallFunctionLength > 0) {
    jit::JitOptions.smallFunctionMaxBytecodeLength = smallFunctionLength;
  }

  if (const char* str = op.getStringOption("ion-regalloc")) {
    jit::JitOptions.forcedRegisterAllocator = jit::LookupRegisterAllocator(str);
    if (!jit::JitOptions.forcedRegisterAllocator.isSome()) {
      return OptionFailure("ion-regalloc", str);
    }
  }

  if (op.getBoolOption("ion-eager")) {
    jit::JitOptions.setEagerIonCompilation();
  }

  offthreadBaselineCompilation = false;
  if (const char* str = op.getStringOption("baseline-offthread-compile")) {
    if (strcmp(str, "on") == 0) {
      offthreadBaselineCompilation = true;
    } else if (strcmp(str, "off") != 0) {
      return OptionFailure("baseline-offthread-compile", str);
    }
  }
  cx->runtime()->setOffthreadBaselineCompilationEnabled(
      offthreadBaselineCompilation);

  if (const char* str = op.getStringOption("baseline-batching")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.baselineBatching = true;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.baselineBatching = false;
    } else {
      return OptionFailure("baseline-batching", str);
    }
  }

  int32_t queueCapacity = op.getIntOption("baseline-queue-capacity");
  if (queueCapacity == 0) {
    fprintf(stderr, "baseline-queue-capacity must be positive\n");
    return false;
  }
  if (queueCapacity > 0) {
    jit::JitOptions.baselineQueueCapacity = std::min(
        uint32_t(queueCapacity), js::jit::BaselineCompileQueue::MaxCapacity);
  }

  offthreadIonCompilation = true;
  if (const char* str = op.getStringOption("ion-offthread-compile")) {
    if (strcmp(str, "off") == 0) {
      offthreadIonCompilation = false;
    } else if (strcmp(str, "on") != 0) {
      return OptionFailure("ion-offthread-compile", str);
    }
  }
  cx->runtime()->setOffthreadIonCompilationEnabled(offthreadIonCompilation);

  if (op.getStringOption("ion-parallel-compile")) {
    fprintf(stderr,
            "--ion-parallel-compile is deprecated. Please use "
            "--ion-offthread-compile instead.\n");
    return false;
  }

  if (op.getBoolOption("disable-bailout-loop-check")) {
    jit::JitOptions.disableBailoutLoopCheck = true;
  }

  if (op.getBoolOption("only-inline-selfhosted")) {
    jit::JitOptions.onlyInlineSelfHosted = true;
  }

  if (op.getBoolOption("enable-ic-frame-pointers")) {
    jit::JitOptions.enableICFramePointers = true;
  }

  if (const char* str = op.getStringOption("ion-iterator-indices")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableIteratorIndices = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableIteratorIndices = true;
    } else {
      return OptionFailure("ion-iterator-indices", str);
    }
  }

  if (const char* str = op.getStringOption("ion-load-keys")) {
    if (strcmp(str, "on") == 0) {
      jit::JitOptions.disableMarkLoadsUsedAsPropertyKeys = false;
    } else if (strcmp(str, "off") == 0) {
      jit::JitOptions.disableMarkLoadsUsedAsPropertyKeys = true;
    } else {
      return OptionFailure("ion-load-keys", str);
    }
  }

#if defined(JS_SIMULATOR_ARM)
  if (op.getBoolOption("arm-sim-icache-checks")) {
    jit::SimulatorProcess::ICacheCheckingDisableCount = 0;
  }

  int32_t stopAt = op.getIntOption("arm-sim-stop-at");
  if (stopAt >= 0) {
    jit::Simulator::StopSimAt = stopAt;
  }
#elif defined(JS_SIMULATOR_MIPS64)
  if (op.getBoolOption("mips-sim-icache-checks")) {
    jit::SimulatorProcess::ICacheCheckingDisableCount = 0;
  }

  int32_t stopAt = op.getIntOption("mips-sim-stop-at");
  if (stopAt >= 0) {
    jit::Simulator::StopSimAt = stopAt;
  }
#elif defined(JS_SIMULATOR_LOONG64)
  if (op.getBoolOption("loong64-sim-icache-checks")) {
    jit::SimulatorProcess::ICacheCheckingDisableCount = 0;
  }

  int32_t stopAt = op.getIntOption("loong64-sim-stop-at");
  if (stopAt >= 0) {
    jit::Simulator::StopSimAt = stopAt;
  }
#endif

#ifdef DEBUG
#  ifdef JS_CODEGEN_RISCV64
  if (op.getBoolOption("riscv-debug")) {
    jit::Assembler::FLAG_riscv_debug = true;
  }
#  endif
#  ifdef JS_SIMULATOR_RISCV64
  if (op.getBoolOption("trace-sim")) {
    jit::Simulator::FLAG_trace_sim = true;
  }
  if (op.getBoolOption("debug-sim")) {
    jit::Simulator::FLAG_debug_sim = true;
  }
  if (op.getBoolOption("riscv-trap-to-simulator-debugger")) {
    jit::Simulator::FLAG_riscv_trap_to_simulator_debugger = true;
  }
  int32_t stopAt = op.getIntOption("riscv-sim-stop-at");
  if (stopAt >= 0) {
    jit::Simulator::StopSimAt = stopAt;
  }
#  endif
#endif

  if (op.getBoolOption("enable-regexp-duplicate-named-groups")) {
    jit::JitOptions.js_regexp_duplicate_named_groups = true;
  }

  if (op.getBoolOption("enable-regexp-modifiers")) {
    jit::JitOptions.js_regexp_modifiers = true;
  }

  return true;
}

bool SetContextGCOptions(JSContext* cx, const OptionParser& op) {
  JS_SetGCParameter(cx, JSGC_MAX_BYTES, 0xffffffff);

  size_t nurseryBytes = op.getIntOption("nursery-size") * 1024L * 1024L;
  if (nurseryBytes == 0) {
    fprintf(stderr, "Error: --nursery-size parameter must be non-zero.\n");
    fprintf(stderr,
            "The nursery can be disabled by passing the --no-ggc option.\n");
    return false;
  }
  JS_SetGCParameter(cx, JSGC_MAX_NURSERY_BYTES, nurseryBytes);

  size_t availMemMB = op.getIntOption("available-memory");
  if (availMemMB > 0) {
    JS_SetGCParametersBasedOnAvailableMemory(cx, availMemMB);
  }

  if (const char* opt = op.getStringOption("nursery-strings")) {
    if (strcmp(opt, "on") == 0) {
      cx->runtime()->gc.nursery().enableStrings();
    } else if (strcmp(opt, "off") == 0) {
      cx->runtime()->gc.nursery().disableStrings();
    } else {
      MOZ_CRASH("invalid option value for --nursery-strings, must be on/off");
    }
  }

  if (const char* opt = op.getStringOption("nursery-bigints")) {
    if (strcmp(opt, "on") == 0) {
      cx->runtime()->gc.nursery().enableBigInts();
    } else if (strcmp(opt, "off") == 0) {
      cx->runtime()->gc.nursery().disableBigInts();
    } else {
      MOZ_CRASH("invalid option value for --nursery-bigints, must be on/off");
    }
  }

  bool incrementalGC = !op.getBoolOption("no-incremental-gc");
  JS_SetGCParameter(cx, JSGC_INCREMENTAL_GC_ENABLED, incrementalGC);

#ifndef ANDROID
  bool parallelMarking = true;
#else
  bool parallelMarking = false;
#endif
  if (op.getBoolOption("enable-parallel-marking")) {
    parallelMarking = true;
  }
  if (op.getBoolOption("no-parallel-marking")) {
    parallelMarking = false;
  }
  JS_SetGCParameter(cx, JSGC_PARALLEL_MARKING_ENABLED, parallelMarking);

  JS_SetGCParameter(cx, JSGC_SLICE_TIME_BUDGET_MS, 5);

  JS_SetGCParameter(cx, JSGC_PER_ZONE_GC_ENABLED, true);

  for (MultiStringRange args = op.getMultiStringOption("gc-param");
       !args.empty(); args.popFront()) {
    if (!SetGCParameterFromArg(cx, args.front())) {
      return false;
    }
  }

#ifdef DEBUG
  dumpEntrainedVariables = op.getBoolOption("dump-entrained-variables");
#endif

#ifdef JS_GC_ZEAL
  const char* zealStr = op.getStringOption("gc-zeal");
  if (zealStr) {
    if (!cx->runtime()->gc.parseAndSetZeal(zealStr)) {
      return false;
    }
    uint32_t nextScheduled;
    cx->runtime()->gc.getZealBits(&gZealBits, &gZealFrequency, &nextScheduled);
  }
#endif

  return true;
}

bool InitModuleLoader(JSContext* cx, const OptionParser& op) {
  RootedString moduleLoadPath(cx);
  if (const char* option = op.getStringOption("module-load-path")) {
    UniqueChars pathUtf8 = JS::EncodeNarrowToUtf8(cx, option);
    if (!pathUtf8) {
      return false;
    }

    Rooted<JSString*> jspath(cx, NewStringCopyUTF8(cx, pathUtf8.get()));
    if (!jspath) {
      return false;
    }

    moduleLoadPath = js::shell::ResolvePath(cx, jspath, RootRelative);

    processWideModuleLoadPath = JS_EncodeStringToUTF8(cx, moduleLoadPath);
    if (!processWideModuleLoadPath) {
      return false;
    }
  } else {
    processWideModuleLoadPath = js::shell::GetCWD(cx);
    if (!processWideModuleLoadPath) {
      return false;
    }

    moduleLoadPath = NewStringCopyUTF8(cx, processWideModuleLoadPath.get());
    if (!moduleLoadPath) {
      return false;
    }
  }

  ShellContext* sc = GetShellContext(cx);
  sc->moduleLoader = js::MakeUnique<ModuleLoader>();
  if (!sc->moduleLoader || !sc->moduleLoader->init(cx, moduleLoadPath)) {
    return false;
  }

  return true;
}
