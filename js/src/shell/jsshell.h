/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsshell_js_h
#define jsshell_js_h

#include "mozilla/Atomics.h"
#include "mozilla/Maybe.h"
#include "mozilla/TimeStamp.h"

#include "builtin/MapObject.h"
#include "js/CompileOptions.h"
#include "js/GCVector.h"
#include "shell/ModuleLoader.h"
#include "threading/ConditionVariable.h"
#include "threading/LockGuard.h"
#include "threading/Mutex.h"
#include "threading/Thread.h"
#include "vm/GeckoProfiler.h"
#include "vm/Monitor.h"

// Some platform hooks must be implemented for single-step profiling.
#if defined(JS_SIMULATOR_ARM) || defined(JS_SIMULATOR_MIPS64) ||    \
    defined(JS_SIMULATOR_ARM64) || defined(JS_SIMULATOR_LOONG64) || \
    defined(JS_SIMULATOR_RISCV64)
#  define SINGLESTEP_PROFILING
#endif

namespace js {

class ArrayObject;

namespace shell {

// Define use of application-specific slots on the shell's global object.
enum GlobalAppSlot { GlobalAppSlotModuleRegistry, GlobalAppSlotCount };
static_assert(GlobalAppSlotCount <= JSCLASS_GLOBAL_APPLICATION_SLOTS,
              "Too many applications slots defined for shell global");

enum JSShellErrNum {
#define MSG_DEF(name, count, exception, format) name,
#include "jsshell.msg"
#undef MSG_DEF
  JSShellErr_Limit
};

const JSErrorFormatString* my_GetErrorMessage(void* userRef,
                                              const unsigned errorNumber);

void WarningReporter(JSContext* cx, JSErrorReport* report);

class MOZ_STACK_CLASS AutoReportException {
  JSContext* cx;

 public:
  explicit AutoReportException(JSContext* cx) : cx(cx) {}
  ~AutoReportException();
};

bool GenerateInterfaceHelp(JSContext* cx, JS::HandleObject obj,
                           const char* name);

JSString* FileAsString(JSContext* cx, JS::HandleString pathnameStr);

class AutoCloseFile {
 private:
  FILE* f_;

 public:
  explicit AutoCloseFile(FILE* f) : f_(f) {}
  ~AutoCloseFile() { (void)release(); }
  bool release() {
    bool success = true;
    if (f_ && f_ != stdin && f_ != stdout && f_ != stderr) {
      success = !fclose(f_);
    }
    f_ = nullptr;
    return success;
  }
};

// Reference counted file.
struct RCFile {
  FILE* fp;
  uint32_t numRefs;

  RCFile() : fp(nullptr), numRefs(0) {}
  explicit RCFile(FILE* fp) : fp(fp), numRefs(0) {}

  void acquire() { numRefs++; }

  // Starts out with a ref count of zero.
  static RCFile* create(JSContext* cx, const char* filename, const char* mode);

  void close();
  bool isOpen() const { return fp; }
  bool release();
};

// Shell command-line arguments and count.
extern int sArgc;
extern char** sArgv;

// Shell state set once at startup.
extern RCFile* gErrFile;
extern RCFile* gOutFile;
extern bool disableOOMFunctions;
extern bool defaultToSameCompartment;

extern UniqueChars processWideModuleLoadPath;

// Alias the global dstName to namespaceObj.srcName. For example, if dstName is
// "snarf", namespaceObj represents "os.file", and srcName is "readFile", then
// this is equivalent to the JS code:
//
//   snarf = os.file.readFile;
//
// This provides a mechanism for namespacing the various JS shell helper
// functions without breaking backwards compatibility with things that use the
// global names.
bool CreateAlias(JSContext* cx, const char* dstName,
                 JS::HandleObject namespaceObj, const char* srcName);

class NonShrinkingValueVector
    : public GCVector<HeapPtr<Value>, 0, SystemAllocPolicy> {
  using Base = GCVector<HeapPtr<Value>, 0, SystemAllocPolicy>;

 public:
  bool traceWeak(JSTracer* trc) {
    for (HeapPtr<Value>& value : *this) {
      if (value.isGCThing()) {
        Zone* zone = value.toGCThing()->zoneFromAnyThread();
        if (zone->isGCSweeping() || zone->isGCCompacting()) {
          TraceOrClearWeakEdge(trc, &value, "NonShrinkingValueVector element");
        }
      }
    }
    return true;
  }
};

using MarkBitObservers = JS::WeakCache<NonShrinkingValueVector>;

#ifdef SINGLESTEP_PROFILING
using StackChars = Vector<char16_t, 0, SystemAllocPolicy>;
#endif

class OffThreadJob;

// Per-context shell state.
struct ShellContext {
  enum IsWorkerEnum { Worker = true, MainThread = false };

  explicit ShellContext(JSContext* cx, IsWorkerEnum isWorker_);
  bool registerWithCx(JSContext* cx);
  ~ShellContext();

  JSContext* cx_;

  const IsWorkerEnum isWorker;
  bool lastWarningEnabled;

  // Track promise rejections and report unhandled rejections.
  bool trackUnhandledRejections;

  double timeoutInterval;
  double startTime;
  mozilla::Atomic<bool> serviceInterrupt;
  mozilla::Atomic<bool> haveInterruptFunc;
  JS::PersistentRootedValue interruptFunc;
  JS::PersistentRootedValue lastWarning;
  JS::PersistentRootedValue promiseRejectionTrackerCallback;

  // Rejected promises that are not yet handled. Added when rejection
  // happens, and removed when rejection is handled. This uses SetObject to
  // report unhandled rejections in the rejected order.
  JS::PersistentRooted<SetObject*> unhandledRejectedPromises;

#ifdef SINGLESTEP_PROFILING
  Vector<StackChars, 0, SystemAllocPolicy> stacks;
#endif

  /*
   * Watchdog thread state.
   */
  js::Mutex watchdogLock MOZ_UNANNOTATED;
  js::ConditionVariable watchdogWakeup;
  mozilla::Maybe<js::Thread> watchdogThread;
  mozilla::Maybe<mozilla::TimeStamp> watchdogTimeout;

  js::ConditionVariable sleepWakeup;

  int exitCode;
  bool quitting;

  JS::UniqueChars readLineBuf;
  size_t readLineBufPos;

  js::shell::RCFile** errFilePtr;
  js::shell::RCFile** outFilePtr;

  UniquePtr<ProfilingStack> geckoProfilingStack;

  UniquePtr<ModuleLoader> moduleLoader;

  UniquePtr<MarkBitObservers> markObservers;

  // Off-thread parse state.
  js::Monitor offThreadMonitor MOZ_UNANNOTATED;
  Vector<OffThreadJob*, 0, SystemAllocPolicy> offThreadJobs;

  // Number of root module evaluation promises that have not yet settled.
  uint32_t pendingRootModuleEvaluations = 0;

  // Queued task callbacks that run after the microtask queue.

  using ObjectVector = GCVector<JSObject*, 0, SystemAllocPolicy>;
  JS::PersistentRooted<ObjectVector> taskCallbacks;

  // Entry points of the hidden debug session host global.
  // See the Shell DebugSession SMDOC comment.
  JS::PersistentRootedObject debugSessionHost;
};

extern ShellContext* GetShellContext(JSContext* cx);

struct ShellCompartmentPrivate {
  HeapPtr<ArrayObject*> blackRoot;
  HeapPtr<ArrayObject*> grayRoot;
  bool isDebugSessionHost = false;

  // If true, objects from other compartments are wrapped with security
  // wrappers.
  bool useSecurityWrappers = false;
};

extern ShellCompartmentPrivate* EnsureShellCompartmentPrivate(JSContext* cx);

extern const JSClass ShellGlobalClass;

extern void SetStandardRealmOptions(JSContext* cx, JS::RealmOptions& options);

[[nodiscard]] extern bool PrintStackTrace(JSContext* cx,
                                          JS::Handle<JSObject*> stackObj);

} /* namespace shell */
} /* namespace js */

#endif
