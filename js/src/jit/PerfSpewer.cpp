/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/Printf.h"

#include "js/Utility.h"

#if defined(JS_ION_PERF) && defined(XP_UNIX)
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

#if defined(JS_ION_PERF) && defined(XP_LINUX) && !defined(ANDROID) && \
    defined(__GLIBC__)
#  include <dlfcn.h>
#  include <sys/syscall.h>
#  include <sys/types.h>
#  include <unistd.h>
#  define gettid() static_cast<pid_t>(syscall(__NR_gettid))
#endif

#if defined(JS_ION_PERF) && (defined(ANDROID) || defined(XP_DARWIN))
#  include <limits.h>
#  include <stdlib.h>
#  include <unistd.h>
char* get_current_dir_name() {
  char* buffer = js_pod_malloc<char>(PATH_MAX);
  if (buffer == nullptr) {
    return nullptr;
  }

  if (getcwd(buffer, PATH_MAX) == nullptr) {
    js_free(buffer);
    return nullptr;
  }

  return buffer;
}
#endif

#if defined(JS_ION_PERF) && defined(XP_DARWIN)
#  include <pthread.h>
#  include <unistd.h>

pid_t gettid_pthread() {
  uint64_t tid;
  if (pthread_threadid_np(nullptr, &tid) != 0) {
    return 0;
  }
  // Truncate the tid to 32 bits. macOS thread IDs are usually small enough.
  // And even if we do end up truncating, it doesn't matter much for Jitdump
  // as long as the process ID is correct.
  return pid_t(tid);
}
#  define gettid() gettid_pthread()
#endif

#include <atomic>

#include "jit/BaselineFrameInfo.h"
#include "jit/CacheIR.h"
#include "jit/CompileWrappers.h"
#include "jit/Jitdump.h"
#include "jit/JitSpewer.h"
#include "jit/LIR.h"
#include "jit/MIR-wasm.h"
#include "jit/MIR.h"
#include "jit/PerfSpewer.h"
#include "js/ColumnNumber.h"  // JS::LimitedColumnNumberOneOrigin, JS::ColumnNumberOffset
#include "js/Exception.h"
#include "js/HashTable.h"
#include "js/Printf.h"
#include "vm/BytecodeUtil.h"
#include "vm/MutexIDs.h"

#ifdef XP_WIN
// clang-format off
#  include "util/WindowsWrapper.h"
#  include <evntprov.h>
// clang-format on

#  include <string>

// We report JIT code to ETW through the Microsoft-JScript provider, using the
// events JScript9 defined for it. V8 emits the same events (on a provider of
// its own), see src/diagnostics/etw-jit-win.cc in Chromium.
const GUID PROVIDER_JSCRIPT9 = {
    0x57277741,
    0x3638,
    0x4a4b,
    {0xbd, 0xba, 0x0a, 0xc6, 0xe4, 0x5d, 0xa5, 0x6c}};

// Consumers decode these events with the Microsoft-JScript manifest, which TDH
// looks up by provider GUID plus event ID and version, so those are the fields
// that have to match the manifest; the task and opcode names it reports come
// from the manifest rather than from the numbers below. The level and the
// keyword have to agree between the two events, so that a session enabling one
// of them also gets the other.
const EVENT_DESCRIPTOR MethodLoad = {0x9, 0x0, 0x0, 0x4, 0xa, 0x1, 0x1};
const EVENT_DESCRIPTOR SourceLoad = {0x29, 0x0, 0x0, 0x4, 0xc, 0x2, 0x1};

static REGHANDLE sETWRegistrationHandle = NULL;

static std::atomic<bool> etwCollection = false;

// Bumped whenever an ETW session enables our provider. The new session has not
// seen any of the SourceLoad events we emitted for previous ones, so this is
// used to re-announce the sources we know about.
static std::atomic<uint32_t> etwCollectionGeneration = 0;
#endif

using namespace js;
using namespace js::jit;

enum class PerfModeType { None, Function, Source, IR, IROperands, IRGraph };

static std::atomic<PerfModeType> PerfMode = PerfModeType::None;

// Mutex to guard access to the profiler vectors and jitdump file if perf
// profiling is enabled.
MOZ_RUNINIT static js::Mutex PerfMutex(mutexid::PerfSpewer);

static PersistentRooted<GCVector<JitCode*, 0, js::SystemAllocPolicy>>
    jitCodeVector;

#ifdef XP_WIN
// The ScriptSource ids we have already emitted a SourceLoad event for, and the
// value etwCollectionGeneration had when we started filling it in. Both are
// guarded by PerfMutex.
MOZ_RUNINIT static HashSet<uint32_t, DefaultHasher<uint32_t>, SystemAllocPolicy>
    etwLoadedSources;
static uint32_t etwLoadedSourcesGeneration = 0;
#endif

#ifdef JS_ION_PERF
constinit static UniqueChars spew_dir;
static FILE* JitDumpFilePtr = nullptr;
static void* mmap_address = nullptr;
static char* jitDumpBuffer = nullptr;
static bool IsPerfProfiling() { return JitDumpFilePtr != nullptr; }
#endif

AutoLockPerfSpewer::AutoLockPerfSpewer() { PerfMutex.lock(); }

AutoLockPerfSpewer::~AutoLockPerfSpewer() { PerfMutex.unlock(); }

#ifdef JS_ION_PERF
static uint64_t GetMonotonicTimestamp() {
  using mozilla::TimeStamp;
#  ifdef XP_LINUX
  return TimeStamp::Now().RawClockMonotonicNanosecondsSinceBoot();
#  elif XP_WIN
  return TimeStamp::Now().RawQueryPerformanceCounterValue();
#  elif XP_DARWIN
  return TimeStamp::Now().RawMachAbsoluteTimeNanoseconds();
#  else
  MOZ_CRASH("no timestamp");
#  endif
}

// values are from /usr/include/elf.h
static uint32_t GetMachineEncoding() {
#  if defined(JS_CODEGEN_X86)
  return 3;  // EM_386
#  elif defined(JS_CODEGEN_X64)
  return 62;  // EM_X86_64
#  elif defined(JS_CODEGEN_ARM)
  return 40;  // EM_ARM
#  elif defined(JS_CODEGEN_ARM64)
  return 183;  // EM_AARCH64
#  elif defined(JS_CODEGEN_MIPS64)
  return 8;  // EM_MIPS
#  else
  return 0;  // Unsupported
#  endif
}

static void WriteToJitDumpFile(const void* addr, uint32_t size,
                               AutoLockPerfSpewer& lock) {
  MOZ_RELEASE_ASSERT(JitDumpFilePtr);
  size_t rv = fwrite(addr, 1, size, JitDumpFilePtr);
  MOZ_RELEASE_ASSERT(rv == size);
}

static void WriteJitDumpDebugEntry(uint64_t addr, const char* filename,
                                   uint32_t lineno, uint32_t discrim,
                                   AutoLockPerfSpewer& lock) {
  JitDumpDebugEntry entry = {addr, lineno, discrim};
  WriteToJitDumpFile(&entry, sizeof(entry), lock);
  WriteToJitDumpFile(filename, strlen(filename) + 1, lock);
}

static void writeJitDumpHeader(AutoLockPerfSpewer& lock) {
  JitDumpHeader header = {};
  header.magic = 0x4A695444;
  header.version = 1;
  header.total_size = sizeof(header);
  header.elf_mach = GetMachineEncoding();
  header.pad1 = 0;
  header.pid = getpid();
  header.timestamp = GetMonotonicTimestamp();
  header.flags = 0;

  WriteToJitDumpFile(&header, sizeof(header), lock);
}

static bool openJitDump() {
  if (JitDumpFilePtr) {
    return true;
  }
  AutoLockPerfSpewer lock;

  const ssize_t bufferSize = 256;
  char filenameBuffer[bufferSize];

  // We want to write absolute filenames into the debug info or else we get the
  // filenames in the perf report
  if (getenv("PERF_SPEW_DIR")) {
    char* env_dir = getenv("PERF_SPEW_DIR");
    if (env_dir[0] == '/') {
      spew_dir = JS_smprintf("%s", env_dir);
    } else {
      const char* dir = get_current_dir_name();
      if (!dir) {
        fprintf(stderr, "couldn't get current dir name\n");
        return false;
      }
      spew_dir = JS_smprintf("%s/%s", dir, env_dir);
      js_free((void*)dir);
    }
  } else {
    fprintf(stderr, "Please define PERF_SPEW_DIR as an output directory.\n");
    return false;
  }

  if (SprintfBuf(filenameBuffer, bufferSize, "%s/jit-%d.dump", spew_dir.get(),
                 getpid()) >= bufferSize) {
    return false;
  }

  MOZ_ASSERT(!JitDumpFilePtr);

  int fd = open(filenameBuffer, O_CREAT | O_TRUNC | O_RDWR, 0666);
  JitDumpFilePtr = fdopen(fd, "w+");

  if (!JitDumpFilePtr) {
    return false;
  }

  // Allocate a large buffer to reduce write() syscall overhead.
  // On Android, setvbuf is not used because Android processes don't always
  // shut down cleanly, which would leave buffered data unflushed and produce
  // incomplete jitdump files.
#  ifndef ANDROID
  constexpr size_t kJitDumpBufferSize = 2 * 1024 * 1024;
  jitDumpBuffer = js_pod_malloc<char>(kJitDumpBufferSize);
  if (!jitDumpBuffer) {
    fclose(JitDumpFilePtr);
    JitDumpFilePtr = nullptr;
    return false;
  }
  setvbuf(JitDumpFilePtr, jitDumpBuffer, _IOFBF, kJitDumpBufferSize);
#  endif

#  ifdef XP_LINUX
  // We need to mmap the jitdump file for perf to find it.
  long page_size = sysconf(_SC_PAGESIZE);
  int prot = PROT_READ | PROT_EXEC;
  // The mmap call fails on some Android devices if PROT_EXEC is specified, and
  // it does not appear to be required by simpleperf, so omit it on Android.
#    ifdef ANDROID
  prot &= ~PROT_EXEC;
#    endif
  mmap_address = mmap(nullptr, page_size, prot, MAP_PRIVATE, fd, 0);
  if (mmap_address == MAP_FAILED) {
    PerfMode = PerfModeType::None;
    return false;
  }
#  endif

  writeJitDumpHeader(lock);
  return true;
}

static void CheckPerf() {
  static bool PerfChecked = false;

  if (!PerfChecked) {
    const char* env = getenv("IONPERF");
    if (env == nullptr) {
      PerfMode = PerfModeType::None;
    } else if (!strcmp(env, "src")) {
      PerfMode = PerfModeType::Source;
    } else if (!strcmp(env, "ir")) {
      PerfMode = PerfModeType::IR;
    } else if (!strcmp(env, "ir-ops")) {
#  ifdef JS_JITSPEW
      PerfMode = PerfModeType::IROperands;
#  else
      fprintf(stderr,
              "Warning: IONPERF=ir-ops requires --enable-jitspew to be "
              "enabled, defaulting to IONPERF=ir\n");
      PerfMode = PerfModeType::IR;
#  endif
    } else if (!strcmp(env, "ir-graph")) {
#  ifdef JS_JITSPEW
      PerfMode = PerfModeType::IRGraph;
#  else
      fprintf(stderr,
              "Warning: IONPERF=ir-graph requires --enable-jitspew to be "
              "enabled, defaulting to IONPERF=ir\n");
      PerfMode = PerfModeType::IR;
#  endif
    } else if (!strcmp(env, "func")) {
      PerfMode = PerfModeType::Function;
    } else {
      fprintf(stderr, "Use IONPERF=func to record at function granularity\n");
      fprintf(stderr,
              "Use IONPERF=ir to record and annotate assembly with IR\n");
#  ifdef JS_JITSPEW
      fprintf(stderr,
              "Use IONPERF=ir-ops to record and annotate assembly with IR that "
              "shows operands\n");
      fprintf(stderr,
              "Use IONPERF=ir-graph to record structured IR graphs for "
              "visualization\n");
#  endif
      fprintf(stderr,
              "Use IONPERF=src to record and annotate assembly with source, if "
              "available locally\n");
      exit(0);
    }

    if (PerfMode != PerfModeType::None) {
      if (openJitDump()) {
        PerfChecked = true;
        return;
      }

      fprintf(stderr, "Failed to open perf map file.  Disabling IONPERF.\n");
      PerfMode = PerfModeType::None;
    }
    PerfChecked = true;
  }
}
#endif

#ifdef XP_WIN
void NTAPI ETWEnableCallback(LPCGUID aSourceId, ULONG aIsEnabled, UCHAR aLevel,
                             ULONGLONG aMatchAnyKeyword,
                             ULONGLONG aMatchAllKeyword,
                             PEVENT_FILTER_DESCRIPTOR aFilterData,
                             PVOID aCallbackContext) {
  // This is called on a CRT worker thread. This means this might race with
  // our main thread, but that is okay.
  if (aIsEnabled) {
    etwCollectionGeneration++;
  }
  etwCollection = aIsEnabled;
  PerfMode = aIsEnabled ? PerfModeType::Function : PerfModeType::None;
}

void RegisterETW() {
  static bool sHasRegisteredETW = false;
  if (!sHasRegisteredETW) {
    if (getenv("ETW_ENABLED")) {
      EventRegister(&PROVIDER_JSCRIPT9,  // GUID that identifies the provider
                    ETWEnableCallback,   // Callback for enabling collection
                    NULL,                // Context not used
                    &sETWRegistrationHandle  // Used when calling EventWrite
                                             // and EventUnregister
      );
    }
    sHasRegisteredETW = true;
  }
}
#endif

/* static */
void PerfSpewer::Init() {
#ifdef JS_ION_PERF
  CheckPerf();
#endif
#ifdef XP_WIN
  RegisterETW();
#endif
}

static void DisablePerfSpewer(AutoLockPerfSpewer& lock) {
  fprintf(stderr, "Warning: Disabling PerfSpewer.\n");

#ifdef XP_WIN
  etwCollection = false;
  etwLoadedSources.clearAndCompact();
#endif
  jitCodeVector.clear();
  if (PerfMode == PerfModeType::None) {
    return;
  }
  PerfMode = PerfModeType::None;
#ifdef JS_ION_PERF
  long page_size = sysconf(_SC_PAGESIZE);
  munmap(mmap_address, page_size);
  fclose(JitDumpFilePtr);
  JitDumpFilePtr = nullptr;
  js_free(jitDumpBuffer);
  jitDumpBuffer = nullptr;
#endif
}

static void DisablePerfSpewer() {
  AutoLockPerfSpewer lock;
  DisablePerfSpewer(lock);
}

static bool PerfSrcEnabled() { return PerfMode == PerfModeType::Source; }

#ifdef JS_JITSPEW
static bool PerfIROpsEnabled() { return PerfMode == PerfModeType::IROperands; }
static bool PerfIRGraphEnabled() { return PerfMode == PerfModeType::IRGraph; }
#endif

static bool PerfIREnabled() {
  return (PerfMode == PerfModeType::IRGraph) ||
         (PerfMode == PerfModeType::IROperands) ||
         (PerfMode == PerfModeType::IR);
}

bool js::jit::PerfEnabled() { return PerfMode != PerfModeType::None; }

bool PerfSpewer::perfEnabled() const {
  return PerfEnabled() || runtimeProfilingEnabled_;
}

bool PerfSpewer::perfSrcEnabled() const {
  return PerfSrcEnabled() || runtimeProfilingEnabled_;
}

void InlineCachePerfSpewer::recordInstruction(MacroAssembler& masm,
                                              const CacheOp& op) {
  if (!PerfIREnabled()) {
    return;
  }
  AutoLockPerfSpewer lock;

  recordOpcode(masm.currentOffset() - startOffset_, static_cast<uint32_t>(op));
}

#define CHECK_RETURN(x) \
  if (!(x)) {           \
    disable();          \
    return;             \
  }

void IonPerfSpewer::disable() {
#ifdef JS_JITSPEW
  if (graphSpewer_) {
    graphPrinter_.finish();
    graphSpewer_ = nullptr;
  }
#endif
  PerfSpewer::disable();
}

void IonPerfSpewer::startRecording(CompileRuntime* runtime,
                                   const wasm::CodeMetadata* wasmCodeMeta) {
  PerfSpewer::startRecording(runtime);
#ifdef JS_JITSPEW
  if (PerfIRGraphEnabled()) {
    graphPrinter_.init(irFile_);
    graphSpewer_ = MakeUnique<GraphSpewer>(graphPrinter_, wasmCodeMeta);
    if (!graphSpewer_) {
      disable();
    }
    graphSpewer_->begin();
    graphSpewer_->beginAnonFunction();
  }
#endif
}

void IonPerfSpewer::endRecording() {
#ifdef JS_JITSPEW
  if (graphSpewer_) {
    graphSpewer_->endFunction();
    graphSpewer_->end();
    graphPrinter_.finish();
    graphSpewer_ = nullptr;
  }
#endif
  PerfSpewer::endRecording();
}

void IonPerfSpewer::recordPass(const char* pass, MIRGraph* graph,
                               BacktrackingAllocator* ra) {
#ifdef JS_JITSPEW
  if (PerfIRGraphEnabled() && graphSpewer_) {
    graphSpewer_->spewPass(pass, graph, ra);
  }
#endif
}

void IonPerfSpewer::recordInstruction(MacroAssembler& masm, LInstruction* ins) {
  uint32_t offset = masm.currentOffset() - startOffset_;

  if (perfSrcEnabled()) {
    uint32_t line = 0;
    uint32_t column = 0;
    if (MDefinition* mir = ins->mirRaw()) {
      jsbytecode* pc = mir->trackedSite()->pc();
      JSScript* script = mir->trackedSite()->script();
      JS::LimitedColumnNumberOneOrigin colno;
      line = PCToLineNumber(script, pc, &colno);
      column = colno.oneOriginValue();
    }

    if (!debugInfo_.emplaceBack(offset, line, column)) {
      disable();
    }
    return;
  }

  if (!PerfIREnabled()) {
    return;
  }

#ifdef JS_JITSPEW
  if (PerfIRGraphEnabled()) {
    if (!debugInfo_.emplaceBack(offset, ins->id(), 0)) {
      disable();
    }
    return;
  }
#endif

  LNode::Opcode op = ins->op();
  UniqueChars opcodeStr;

#ifdef JS_JITSPEW
  if (PerfIROpsEnabled()) {
    Sprinter buf;
    CHECK_RETURN(buf.init());
    buf.put(LIRCodeName(op));
    ins->printOperands(buf);
    opcodeStr = buf.release();
  }
#endif

  recordOpcode(offset, static_cast<uint32_t>(op), std::move(opcodeStr));
}

#ifdef JS_JITSPEW
static void PrintStackValue(JSContext* maybeCx, StackValue* stackVal,
                            CompilerFrameInfo& frame, Sprinter& buf) {
  switch (stackVal->kind()) {
    /****** Constant ******/
    case StackValue::Constant: {
      js::Value constantVal = stackVal->constant();
      if (constantVal.isInt32()) {
        buf.printf("%d", constantVal.toInt32());
      } else if (constantVal.isObjectOrNull()) {
        buf.printf("obj:%p", constantVal.toObjectOrNull());
      } else if (constantVal.isString()) {
        if (maybeCx) {
          buf.put("str:");
          buf.putString(maybeCx, constantVal.toString());
        } else {
          buf.put("str");
        }
      } else if (constantVal.isNumber()) {
        buf.printf("num:%f", constantVal.toNumber());
      } else if (constantVal.isSymbol()) {
        if (maybeCx) {
          buf.put("sym:");
          constantVal.toSymbol()->dump(buf);
        } else {
          buf.put("sym");
        }
      } else {
        buf.printf("raw:%" PRIx64, constantVal.asRawBits());
      }
    } break;
    /****** Register ******/
    case StackValue::Register: {
      Register reg = stackVal->reg().payloadOrValueReg();
      buf.put(reg.name());
    } break;
    /****** Stack ******/
    case StackValue::Stack:
      buf.put("stack");
      break;
    /****** ThisSlot ******/
    case StackValue::ThisSlot: {
#  ifdef JS_HAS_HIDDEN_SP
      buf.put("this");
#  else
      Address addr = frame.addressOfThis();
      buf.printf("this:%s(%d)", addr.base.name(), addr.offset);
#  endif
    } break;
    /****** LocalSlot ******/
    case StackValue::LocalSlot:
      buf.printf("local:%u", stackVal->localSlot());
      break;
    /****** ArgSlot ******/
    case StackValue::ArgSlot:
      buf.printf("arg:%u", stackVal->argSlot());
      break;

    default:
      MOZ_CRASH("Unexpected kind");
      break;
  }
}
#endif

WasmBaselinePerfSpewer::WasmBaselinePerfSpewer()
    : needsToRecordInstruction_(PerfIREnabled() || PerfSrcEnabled()) {}

[[nodiscard]] bool WasmBaselinePerfSpewer::needsToRecordInstruction() const {
  return needsToRecordInstruction_;
}

void WasmBaselinePerfSpewer::recordInstruction(MacroAssembler& masm,
                                               const wasm::OpBytes& op) {
  MOZ_ASSERT(needsToRecordInstruction());

  if (!op.canBePacked()) {
    return;
  }

  recordOpcode(masm.currentOffset() - startOffset_, op.toPacked());
}

void BaselinePerfSpewer::recordInstruction(
    MacroAssembler& masm, jsbytecode* pc, unsigned line,
    JS::LimitedColumnNumberOneOrigin column, CompilerFrameInfo& frame) {
  uint32_t offset = masm.currentOffset() - startOffset_;
  if (perfSrcEnabled()) {
    if (!debugInfo_.emplaceBack(offset, line, column.oneOriginValue())) {
      disable();
    }
    return;
  }

  if (!PerfIREnabled()) {
    return;
  }

  JSOp op = JSOp(*pc);
  UniqueChars opcodeStr;

#ifdef JS_JITSPEW
  if (PerfIROpsEnabled()) {
    JSScript* script = frame.script;
    unsigned numOperands = js::StackUses(op, pc);

    JSContext* maybeCx = TlsContext.get();
    Sprinter buf(maybeCx);
    CHECK_RETURN(buf.init());
    buf.put(js::CodeName(op));

    if (maybeCx) {
      switch (op) {
        case JSOp::SetName:
        case JSOp::SetGName:
        case JSOp::BindName:
        case JSOp::BindUnqualifiedName:
        case JSOp::BindUnqualifiedGName:
        case JSOp::GetName:
        case JSOp::GetGName: {
          // Emit the name used for these ops
          Rooted<PropertyName*> name(maybeCx, script->getName(pc));
          buf.put(" ");
          buf.putString(maybeCx, name);
        } break;
        default:
          break;
      }

      // Output should be "JSOp (operand1), (operand2), ..."
      for (unsigned i = 1; i <= numOperands; i++) {
        buf.put(" (");
        StackValue* stackVal = frame.peek(-int(i));
        PrintStackValue(maybeCx, stackVal, frame, buf);

        if (i < numOperands) {
          buf.put("),");
        } else {
          buf.put(")");
        }
      }
    }
    opcodeStr = buf.release();
  }
#endif

  recordOpcode(offset, static_cast<uint32_t>(op), std::move(opcodeStr));
}

const char* BaselinePerfSpewer::CodeName(uint32_t op) {
  return js::CodeName(static_cast<JSOp>(op));
}

const char* BaselineInterpreterPerfSpewer::CodeName(uint32_t op) {
  return js::CodeName(static_cast<JSOp>(op));
}

const char* IonPerfSpewer::CodeName(uint32_t op) {
  return js::jit::LIRCodeName(static_cast<LNode::Opcode>(op));
}

const char* IonPerfSpewer::IRFileExtension() {
#ifdef JS_JITSPEW
  if (PerfIRGraphEnabled()) {
    return ".iongraph.json";
  }
#endif
  return ".txt";
}

const char* WasmBaselinePerfSpewer::CodeName(uint32_t op) {
  return wasm::OpBytes::fromPacked(op).toString();
}

const char* InlineCachePerfSpewer::CodeName(uint32_t op) {
  return js::jit::CacheIRCodeName(static_cast<CacheOp>(op));
}

UniqueChars JitCodeDesc::nameWithSourceLocation() const {
  if (!hasSource()) {
    return DuplicateString(name.get());
  }
  return JS_smprintf("%s (%s:%u:%u)", name.get(), filename, line, column);
}

#ifdef XP_WIN
static bool ToETWString(const char* str, std::wstring& out) {
  int len = int(strlen(str));
  if (len == 0) {
    out.clear();
    return true;
  }
  int wideLen = MultiByteToWideChar(CP_UTF8, 0, str, len, nullptr, 0);
  if (wideLen == 0) {
    return false;
  }
  out.resize(size_t(wideLen));
  return MultiByteToWideChar(CP_UTF8, 0, str, len, out.data(), wideLen) != 0;
}

// The size an ETW UNICODESTRING field takes up, including its terminator.
static ULONG ETWStringSize(const std::wstring& str) {
  return ULONG(sizeof(wchar_t) * (str.length() + 1));
}

// Tell ETW about the source |desc| was compiled from. Only the first call for
// a given source in a given collection actually writes an event.
static bool EmitETWSourceLoad(const JitCodeDesc& desc) {
  auto sourceEntry = etwLoadedSources.lookupForAdd(desc.sourceId);
  if (sourceEntry) {
    return true;
  }

  uint64_t sourceId = desc.sourceId;
  void* scriptContextId = nullptr;
  uint32_t sourceFlags = 0;

  std::wstring url;
  if (!ToETWString(desc.filename, url)) {
    return false;
  }

  EVENT_DATA_DESCRIPTOR EventData[4];
  EventDataDescCreate(&EventData[0], &sourceId, sizeof(uint64_t));
  EventDataDescCreate(&EventData[1], &scriptContextId, sizeof(PVOID));
  EventDataDescCreate(&EventData[2], &sourceFlags, sizeof(uint32_t));
  EventDataDescCreate(&EventData[3], url.c_str(), ETWStringSize(url));

  if (EventWrite(sETWRegistrationHandle, &SourceLoad, (ULONG)4, EventData) !=
      ERROR_SUCCESS) {
    return false;
  }

  return etwLoadedSources.add(sourceEntry, desc.sourceId);
}

static bool EmitETWMethodLoad(const JitCodeDesc& desc, void* code_addr,
                              uint64_t code_size) {
  void* scriptContextId = nullptr;
  uint32_t methodId = 0;
  uint16_t methodFlags = 0;
  uint16_t methodAddressRangeId = 0;
  uint64_t sourceId = desc.sourceId;
  uint32_t line = desc.line;
  uint32_t column = desc.column;

  std::wstring name;
  if (!ToETWString(desc.name.get(), name)) {
    return false;
  }

  EVENT_DATA_DESCRIPTOR EventData[10];
  EventDataDescCreate(&EventData[0], &scriptContextId, sizeof(PVOID));
  EventDataDescCreate(&EventData[1], &code_addr, sizeof(PVOID));
  EventDataDescCreate(&EventData[2], &code_size, sizeof(uint64_t));
  EventDataDescCreate(&EventData[3], &methodId, sizeof(uint32_t));
  EventDataDescCreate(&EventData[4], &methodFlags, sizeof(uint16_t));
  EventDataDescCreate(&EventData[5], &methodAddressRangeId, sizeof(uint16_t));
  EventDataDescCreate(&EventData[6], &sourceId, sizeof(uint64_t));
  EventDataDescCreate(&EventData[7], &line, sizeof(uint32_t));
  EventDataDescCreate(&EventData[8], &column, sizeof(uint32_t));
  EventDataDescCreate(&EventData[9], name.c_str(), ETWStringSize(name));

  return EventWrite(sETWRegistrationHandle, &MethodLoad, (ULONG)10,
                    EventData) == ERROR_SUCCESS;
}

// Report a range of JIT code to ETW, as a MethodLoad event preceded, the first
// time we see a source, by the SourceLoad event describing it. Requires
// PerfMutex to be held.
static bool EmitETWEvents(const JitCodeDesc& desc, void* code_addr,
                          uint64_t code_size) {
  uint32_t generation = etwCollectionGeneration;
  if (etwLoadedSourcesGeneration != generation) {
    // A session started collecting since we last emitted a SourceLoad event.
    // It hasn't seen any of them, so announce the sources again.
    etwLoadedSources.clearAndCompact();
    etwLoadedSourcesGeneration = generation;
  }

  if (desc.hasSource() && !EmitETWSourceLoad(desc)) {
    return false;
  }
  return EmitETWMethodLoad(desc, code_addr, code_size);
}
#endif

void PerfSpewer::CollectJitCodeInfo(JitCodeDesc& desc, JitCode* code,
                                    AutoLockPerfSpewer& lock) {
  // Hold the JitCode objects here so they are not GC'd while a perf / ETW
  // profiling session is active.
  if (PerfMode != PerfModeType::None) {
    if (!jitCodeVector.append(code)) {
      DisablePerfSpewer(lock);
      return;
    }
  }

  CollectJitCodeInfo(desc, reinterpret_cast<void*>(code->raw()),
                     code->instructionsSize(), lock);
}

void PerfSpewer::CollectJitCodeInfo(JitCodeDesc& desc, void* code_addr,
                                    uint64_t code_size,
                                    AutoLockPerfSpewer& lock) {
#ifdef JS_ION_PERF
  static uint64_t codeIndex = 1;

  if (IsPerfProfiling()) {
    // perf has no way of reporting the source location out of band, so it goes
    // into the symbol name.
    UniqueChars function_name = desc.nameWithSourceLocation();
    if (!function_name) {
      DisablePerfSpewer(lock);
      return;
    }

    JitDumpLoadRecord record = {};

    record.header.id = JIT_CODE_LOAD;
    record.header.total_size =
        sizeof(record) + strlen(function_name.get()) + 1 + code_size;
    record.header.timestamp = GetMonotonicTimestamp();
    record.pid = getpid();
    record.tid = gettid();
    record.vma = uint64_t(code_addr);
    record.code_addr = uint64_t(code_addr);
    record.code_size = code_size;
    record.code_index = codeIndex++;

    WriteToJitDumpFile(&record, sizeof(record), lock);
    WriteToJitDumpFile(function_name.get(), strlen(function_name.get()) + 1,
                       lock);
    WriteToJitDumpFile(code_addr, code_size, lock);
  }
#endif
#ifdef XP_WIN
  if (etwCollection) {
    if (!EmitETWEvents(desc, code_addr, code_size)) {
      DisablePerfSpewer(lock);
      return;
    }
  }
#endif
}

void PerfSpewer::recordOffset(MacroAssembler& masm, const char* msg) {
  if (!PerfIREnabled()) {
    return;
  }
#ifdef JS_JITSPEW
  if (PerfIRGraphEnabled()) {
    return;
  }
#endif

  UniqueChars offsetStr = DuplicateString(msg);
  recordOpcode(masm.currentOffset() - startOffset_, std::move(offsetStr));
}

void PerfSpewer::recordOpcode(uint32_t offset, uint32_t opcode) {
  recordOpcode(offset, opcode, JS::UniqueChars(nullptr));
}

void PerfSpewer::recordOpcode(uint32_t offset, uint32_t opcode,
                              JS::UniqueChars&& str) {
  if (!irFile_) {
    // If we don't have a file, we can't record the opcode.
    return;
  }

  irFileLines_ += 1;
  if (!debugInfo_.emplaceBack(offset, irFileLines_)) {
    disable();
    return;
  }

  if (str.get()) {
    fprintf(irFile_, "%s\n", str.get());
  } else {
    fprintf(irFile_, "%s\n", CodeName(opcode));
  }
}

void PerfSpewer::recordOpcode(uint32_t offset, JS::UniqueChars&& str) {
  recordOpcode(offset, 0, std::move(str));
}

void PerfSpewer::saveDebugInfo(const char* filename, uintptr_t base,
                               AutoLockPerfSpewer& lock) {
#ifdef JS_ION_PERF
  if (IsPerfProfiling()) {
    JitDumpDebugRecord debug_record = {};

    uint64_t n_records = debugInfo_.length();

    debug_record.header.id = JIT_CODE_DEBUG_INFO;
    debug_record.header.total_size =
        sizeof(debug_record) +
        n_records * (sizeof(JitDumpDebugEntry) + strlen(filename) + 1);
    debug_record.header.timestamp = GetMonotonicTimestamp();
    debug_record.code_addr = uint64_t(base);
    debug_record.nr_entry = n_records;

    WriteToJitDumpFile(&debug_record, sizeof(debug_record), lock);
    for (DebugEntry& entry : debugInfo_) {
      WriteJitDumpDebugEntry(uint64_t(base) + entry.offset, filename,
                             entry.line, entry.column, lock);
    }
  }
#endif
}

static JitCodeDesc GetFunctionDesc(const char* tierName, JSContext* cx,
                                   JSScript* script,
                                   const char* stubName = nullptr) {
  MOZ_ASSERT(script && tierName && cx);
  UniqueChars funName;
  if (script->function() && script->function()->maybePartialDisplayAtom()) {
    funName = AtomToPrintableString(
        cx, script->function()->maybePartialDisplayAtom());
    if (!funName) {
      JS_ClearPendingException(cx);
    }
  }

  JitCodeDesc desc;
  if (stubName) {
    desc.name = JS_smprintf("%s: %s : %s", tierName, stubName,
                            funName ? funName.get() : "*");
  } else {
    desc.name = JS_smprintf("%s: %s", tierName, funName ? funName.get() : "*");
  }

  if (script->filename()) {
    desc.filename = script->filename();
    desc.sourceId = script->scriptSource()->id();
    desc.line = script->lineno();
    desc.column = script->column().oneOriginValue();
  }
  return desc;
}

void PerfSpewer::saveJitCodeDebugInfo(JSScript* script, JitCode* code,
                                      AutoLockPerfSpewer& lock) {
  MOZ_ASSERT(code);

  // We should be done with the temp IR file, if we were using it.
  MOZ_ASSERT(!irFile_);

  if (PerfIREnabled()) {
    // We should have generated a debug file to use here.
    MOZ_ASSERT(irFileName_.get());
    saveDebugInfo(irFileName_.get(), uintptr_t(code->raw()), lock);
    return;
  }

  if (!PerfSrcEnabled() || !script || !script->filename()) {
    return;
  }
  saveDebugInfo(script->filename(), uintptr_t(code->raw()), lock);
}

void PerfSpewer::saveWasmCodeDebugInfo(uintptr_t base,
                                       AutoLockPerfSpewer& lock) {
  // We should be done with the temp IR file, if we were using it.
  MOZ_ASSERT(!irFile_);

  if (!PerfIREnabled()) {
    return;
  }
  saveDebugInfo(irFileName_.get(), base, lock);
}

void PerfSpewer::saveJSProfile(JitCode* code, JitCodeDesc& desc,
                               JSScript* script) {
  MOZ_ASSERT(code && desc);
  AutoLockPerfSpewer lock;
  if (!PerfEnabled()) {
    return;
  }

  saveJitCodeDebugInfo(script, code, lock);
  CollectJitCodeInfo(desc, code, lock);
}

void PerfSpewer::saveWasmProfile(uintptr_t base, size_t size,
                                 JitCodeDesc& desc) {
  MOZ_ASSERT(desc);
  AutoLockPerfSpewer lock;
  if (!PerfEnabled()) {
    return;
  }

  saveWasmCodeDebugInfo(base, lock);
  PerfSpewer::CollectJitCodeInfo(desc, reinterpret_cast<void*>(base),
                                 uint64_t(size), lock);
}

void PerfSpewer::disable(AutoLockPerfSpewer& lock) {
  reset();
  DisablePerfSpewer(lock);
}

void PerfSpewer::disable() {
  AutoLockPerfSpewer lock;
  disable(lock);
}

void PerfSpewer::startRecording(CompileRuntime* runtime,
                                const wasm::CodeMetadata* wasmCodeMeta) {
  MOZ_ASSERT(!irFile_ && !irFileName_);

  // Snapshot the runtime's gecko-profiler state so it's stable for this
  // compile.
  runtimeProfilingEnabled_ = runtime && runtime->geckoProfiler().enabled();

#ifdef JS_ION_PERF
  static uint32_t filenameCounter = 0;

  if (!IsPerfProfiling() || !PerfIREnabled()) {
    return;
  }

  AutoLockPerfSpewer lock;
  irFileName_ = JS_smprintf("%s/jitdump-ir-%u.%u%s", spew_dir.get(),
                            filenameCounter++, getpid(), IRFileExtension());
  if (!irFileName_) {
    disable(lock);
    return;
  }

  irFile_ = fopen(irFileName_.get(), "w");
  if (!irFile_) {
    disable(lock);
    return;
  }
#endif
}

void PerfSpewer::endRecording() {
  if (!irFile_) {
    return;
  }
  fclose(irFile_);
  irFile_ = nullptr;
}

PerfSpewer::~PerfSpewer() {
  // Free the allocated resources if they haven’t been freed yet.
  reset();
}

PerfSpewer::PerfSpewer(PerfSpewer&& other) {
  // Can only move a PerfSpewer after endRecording().
  MOZ_RELEASE_ASSERT(!irFile_ && !other.irFile_);
  debugInfo_ = std::move(other.debugInfo_);
  irFileName_ = std::move(other.irFileName_);
  startOffset_ = other.startOffset_;
  runtimeProfilingEnabled_ = other.runtimeProfilingEnabled_;
}

PerfSpewer& PerfSpewer::operator=(PerfSpewer&& other) {
  // Can only move a PerfSpewer after endRecording().
  MOZ_RELEASE_ASSERT(!irFile_ && !other.irFile_);
  debugInfo_ = std::move(other.debugInfo_);
  irFileName_ = std::move(other.irFileName_);
  startOffset_ = other.startOffset_;
  runtimeProfilingEnabled_ = other.runtimeProfilingEnabled_;
  return *this;
}

IonICPerfSpewer::IonICPerfSpewer(JSScript* script, jsbytecode* pc) {
  if (!PerfSrcEnabled()) {
    return;
  }

  uint32_t lineno;
  JS::LimitedColumnNumberOneOrigin colno;
  lineno = PCToLineNumber(script, pc, &colno);

  if (!debugInfo_.emplaceBack(0, lineno, colno.oneOriginValue())) {
    disable();
  }
}

void IonICPerfSpewer::saveProfile(JSContext* cx, JSScript* script,
                                  JitCode* code, const char* stubName) {
  if (!PerfEnabled()) {
    return;
  }
  JitCodeDesc desc = GetFunctionDesc("IonIC", cx, script, stubName);
  if (!desc) {
    disable();
    return;
  }
  PerfSpewer::saveJSProfile(code, desc, script);
}

void BaselineICPerfSpewer::saveProfile(JitCode* code, const char* stubName) {
  if (!PerfEnabled()) {
    return;
  }
  JitCodeDesc desc(JS_smprintf("BaselineIC: %s", stubName));
  if (!desc) {
    disable();
    return;
  }
  PerfSpewer::saveJSProfile(code, desc, nullptr);
}

void BaselinePerfSpewer::saveProfile(JSContext* cx, JSScript* script,
                                     JitCode* code) {
  if (!PerfEnabled()) {
    return;
  }
  JitCodeDesc desc = GetFunctionDesc("Baseline", cx, script);
  if (!desc) {
    disable();
    return;
  }
  PerfSpewer::saveJSProfile(code, desc, script);
}

JitCodeSourceInfoVector PerfSpewer::extractSourceInfo() const {
  JitCodeSourceInfoVector result;
  if (debugInfo_.empty()) {
    // The profiler wasn't enabled at the compile time.
    return result;
  }

  // The result array length could be the same or less (due to deduplication
  // below).
  if (!result.reserve(debugInfo_.length())) {
    return JitCodeSourceInfoVector();
  }

  // Dedup consecutive entries with identical (line, column). Multiple ops
  // on the same source line are common. This allows us to keep table smaller
  // with no change on the lookup results.
#ifdef DEBUG
  uint32_t lastOffset = 0;
#endif
  uint32_t lastLine = 0;
  uint32_t lastColumn = 0;
  for (const DebugEntry& entry : debugInfo_) {
    // The resulting table must stay sorted by nativeOffset. BaselineEntry's
    // sampler lookup binary-searches it. That holds because recordInstruction
    // appends in code-emission order.
    MOZ_ASSERT(entry.offset >= lastOffset,
               "debugInfo_ must be sorted by offset");
#ifdef DEBUG
    lastOffset = entry.offset;
#endif
    if (entry.line == lastLine && entry.column == lastColumn) {
      continue;
    }
    result.infallibleEmplaceBack(
        JitCodeSourceInfo{entry.offset, entry.line,
                          JS::LimitedColumnNumberOneOrigin::fromUnlimited(
                              entry.column == 0 ? 1 : entry.column)});
    lastLine = entry.line;
    lastColumn = entry.column;
  }
  return result;
}

void BaselineInterpreterPerfSpewer::saveProfile(JitCode* code) {
  if (!PerfEnabled()) {
    return;
  }

  enum class SpewKind { Uninitialized, SingleSym, MultiSym };

  // Check which type of Baseline Interpreter Spew is requested.
  static SpewKind kind = SpewKind::Uninitialized;
  if (kind == SpewKind::Uninitialized) {
    if (getenv("IONPERF_SINGLE_BLINTERP")) {
      kind = SpewKind::SingleSym;
    } else {
      kind = SpewKind::MultiSym;
    }
  }

  // For SingleSym, just emit one "BaselineInterpreter" symbol
  // and emit the opcodes as IR if IONPERF=ir is used.
  if (kind == SpewKind::SingleSym) {
    for (Op& entry : ops_) {
      recordOpcode(entry.offset, entry.opcode, std::move(entry.str));
    }
    ops_.clear();
    JitCodeDesc desc(DuplicateString("BaselineInterpreter"));
    if (!desc) {
      return;
    }
    PerfSpewer::saveJSProfile(code, desc, nullptr);
    return;
  }

  // For MultiSym, split up each opcode into its own symbol.
  // No IR is emitted in this case, so we can skip PerfSpewer::saveProfile.
  MOZ_ASSERT(kind == SpewKind::MultiSym);
  for (size_t i = 1; i < ops_.length(); i++) {
    uintptr_t base = uintptr_t(code->raw()) + ops_[i - 1].offset;
    uintptr_t size = ops_[i].offset - ops_[i - 1].offset;

    UniqueChars rangeName;
    if (ops_[i - 1].str) {
      rangeName = JS_smprintf("BlinterpOp: %s", ops_[i - 1].str.get());
    } else {
      rangeName = JS_smprintf("BlinterpOp: %s", CodeName(ops_[i - 1].opcode));
    }

    // If rangeName is empty, we probably went OOM.
    if (!rangeName) {
      disable();
      return;
    }

    MOZ_ASSERT(base + size <=
               uintptr_t(code->raw()) + code->instructionsSize());
    CollectPerfSpewerJitCodeProfile(base, size, rangeName.get());
  }
}

void BaselineInterpreterPerfSpewer::recordOffset(MacroAssembler& masm,
                                                 const JSOp& op) {
  if (!PerfEnabled()) {
    return;
  }

  if (!ops_.emplaceBack(masm.currentOffset() - startOffset_, unsigned(op))) {
    disable();
    ops_.clear();
    return;
  }
}

void BaselineInterpreterPerfSpewer::recordOffset(MacroAssembler& masm,
                                                 const char* name) {
  if (!PerfEnabled()) {
    return;
  }

  UniqueChars desc = DuplicateString(name);
  if (!ops_.emplaceBack(masm.currentOffset() - startOffset_, std::move(desc))) {
    disable();
    ops_.clear();
    return;
  }
}

void IonPerfSpewer::saveJSProfile(JSContext* cx, JSScript* script,
                                  JitCode* code) {
  if (!PerfEnabled()) {
    return;
  }
  JitCodeDesc desc = GetFunctionDesc("Ion", cx, script);
  if (!desc) {
    disable();
    return;
  }
  PerfSpewer::saveJSProfile(code, desc, script);
}

void IonPerfSpewer::saveWasmProfile(uintptr_t codeBase, size_t codeSize,
                                    UniqueChars&& desc) {
  if (!PerfEnabled()) {
    return;
  }
  JitCodeDesc codeDesc(std::move(desc));
  PerfSpewer::saveWasmProfile(codeBase, codeSize, codeDesc);
}

void WasmBaselinePerfSpewer::saveProfile(uintptr_t codeBase, size_t codeSize,
                                         UniqueChars&& desc) {
  if (!PerfEnabled()) {
    return;
  }
  JitCodeDesc codeDesc(std::move(desc));
  PerfSpewer::saveWasmProfile(codeBase, codeSize, codeDesc);
}

void js::jit::CollectPerfSpewerJitCodeProfile(JitCode* code, const char* msg) {
  if (!code || !PerfEnabled()) {
    return;
  }

  size_t size = code->instructionsSize();
  if (size > 0) {
    AutoLockPerfSpewer lock;
    JitCodeDesc desc(JS_smprintf("%s", msg));
    if (!desc) {
      DisablePerfSpewer(lock);
      return;
    }
    PerfSpewer::CollectJitCodeInfo(desc, code, lock);
  }
}

static void CollectJitCodeProfile(uintptr_t base, uint64_t size,
                                  JitCodeDesc& desc) {
  if (size == 0U || !PerfEnabled()) {
    return;
  }

  AutoLockPerfSpewer lock;
  if (!desc) {
    DisablePerfSpewer(lock);
    return;
  }
  PerfSpewer::CollectJitCodeInfo(desc, reinterpret_cast<void*>(base), size,
                                 lock);
}

void js::jit::CollectPerfSpewerJitCodeProfile(uintptr_t base, uint64_t size,
                                              const char* msg) {
  if (size == 0U || !PerfEnabled()) {
    return;
  }

  JitCodeDesc desc(JS_smprintf("%s", msg));
  CollectJitCodeProfile(base, size, desc);
}

void js::jit::CollectPerfSpewerWasmMap(uintptr_t base, uintptr_t size,
                                       UniqueChars&& desc) {
  if (size == 0U || !PerfEnabled()) {
    return;
  }
  AutoLockPerfSpewer lock;

  JitCodeDesc codeDesc(std::move(desc));
  PerfSpewer::CollectJitCodeInfo(codeDesc, reinterpret_cast<void*>(base),
                                 uint64_t(size), lock);
}

void js::jit::PerfSpewerRangeRecorder::appendEntry(JitCodeDesc& desc) {
  if (!ranges.emplaceBack(masm.currentOffset(), std::move(desc))) {
    DisablePerfSpewer();
    ranges.clear();
  }
}

void js::jit::PerfSpewerRangeRecorder::recordOffset(const char* name) {
  if (!PerfEnabled()) {
    return;
  }
  JitCodeDesc desc(DuplicateString(name));
  if (!desc) {
    DisablePerfSpewer();
    return;
  }
  appendEntry(desc);
}

void js::jit::PerfSpewerRangeRecorder::recordVMWrapperOffset(const char* name) {
  if (!PerfEnabled()) {
    return;
  }

  JitCodeDesc desc(JS_smprintf("VMWrapper: %s", name));
  if (!desc) {
    DisablePerfSpewer();
    return;
  }
  appendEntry(desc);
}

void js::jit::PerfSpewerRangeRecorder::recordOffset(const char* name,
                                                    JSContext* cx,
                                                    JSScript* script) {
  if (!PerfEnabled()) {
    return;
  }
  JitCodeDesc desc = GetFunctionDesc(name, cx, script);
  if (!desc) {
    DisablePerfSpewer();
    return;
  }
  appendEntry(desc);
}

void js::jit::PerfSpewerRangeRecorder::collectRangesForJitCode(JitCode* code) {
  if (!PerfEnabled() || ranges.empty()) {
    return;
  }

  uintptr_t basePtr = uintptr_t(code->raw());
  uintptr_t offsetStart = 0;

  for (OffsetPair& pair : ranges) {
    uint32_t offsetEnd = std::get<0>(pair);
    uintptr_t rangeSize = uintptr_t(offsetEnd - offsetStart);

    CollectJitCodeProfile(basePtr + offsetStart, rangeSize, std::get<1>(pair));
    offsetStart = offsetEnd;
  }

  MOZ_ASSERT(offsetStart <= code->instructionsSize());
  ranges.clear();
}
