/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_PerfSpewer_h
#define jit_PerfSpewer_h

#ifdef JS_ION_PERF
#  include <stdio.h>
#endif
#include "jit/JitCodeSourceInfo.h"  // JitCodeSourceInfo, JitCodeSourceInfoVector
#include "js/AllocPolicy.h"
#include "js/ColumnNumber.h"
#include "js/Vector.h"

#ifdef JS_JITSPEW
#  include "jit/GraphSpewer.h"
#endif

#include "vm/GeckoProfiler.h"

class JSScript;
enum class JSOp : uint8_t;

namespace js {

namespace wasm {
struct OpBytes;
struct CodeMetadata;
}  // namespace wasm

namespace jit {

class CompileRuntime;
class JitCode;
class BacktrackingAllocator;
class CompilerFrameInfo;
class MacroAssembler;
class MBasicBlock;
class MIRGraph;
class LInstruction;
enum class CacheOp : uint16_t;

// Describes a range of JIT code for the profiling backends.
//
// |name| does not include the source location: the jitdump backend appends it
// to the symbol name, while the Windows ETW backend reports it out of band, in
// a SourceLoad event and in the Line/Column fields of the MethodLoad event.
struct JitCodeDesc {
  JS::UniqueChars name;

  // Where this code was generated from, when it belongs to a JSScript.
  // |filename| is borrowed from the script's ScriptSource, which must outlive
  // this JitCodeDesc. |sourceId| is the ScriptSource id, used to tie the code
  // to its source in backends that report the two separately.
  const char* filename = nullptr;
  uint32_t sourceId = 0;
  uint32_t line = 0;
  uint32_t column = 0;

  JitCodeDesc() = default;
  explicit JitCodeDesc(JS::UniqueChars&& name) : name(std::move(name)) {}

  explicit operator bool() const { return bool(name); }
  bool hasSource() const { return filename != nullptr; }

  // |name| with " (filename:line:column)" appended, or a copy of |name| if
  // this code has no source location.
  JS::UniqueChars nameWithSourceLocation() const;
};

struct AutoLockPerfSpewer {
  AutoLockPerfSpewer();
  ~AutoLockPerfSpewer();
};

// True iff a process-wide perf output mode (linux `perf` jitdump / Windows ETW)
// is enabled. Excludes per-runtime gecko state. Use PerfSpewer::perfEnabled()
// for that.
bool PerfEnabled();

class PerfSpewer {
 protected:
  // An entry to insert into the DEBUG_INFO jitdump record. It maps from a code
  // offset (relative to startOffset_) to a line number and column number.
  struct DebugEntry {
    constexpr DebugEntry() : offset(0), line(0), column(0) {}
    constexpr DebugEntry(uint32_t offset_, uint32_t line_, uint32_t column_ = 0)
        : offset(offset_), line(line_), column(column_) {}

    uint32_t offset;
    uint32_t line;
    uint32_t column;
  };

  // The debug records for this perf spewer.
  Vector<DebugEntry, 0, SystemAllocPolicy> debugInfo_;

  // The start offset that debugInfo_ is relative to.
  uint32_t startOffset_ = 0;

  // Whether the runtime being compiled for had the gecko profiler enabled at
  // startRecording time. False for runtime-less (wasm) compiles.
  bool runtimeProfilingEnabled_ = false;

  // The generated IR file that we write into for IONPERF=ir. The move
  // constructors assert that this file has been closed/finished.
  FILE* irFile_ = nullptr;
  uint32_t irFileLines_ = 0;

  // The filename of irFile_.
  JS::UniqueChars irFileName_;

  virtual const char* CodeName(uint32_t op) = 0;
  virtual const char* IRFileExtension() { return ".txt"; }

  // Append an opcode to opcodes_ and the implicit debugInfo_ entry referencing
  // it.
  void recordOpcode(uint32_t offset, uint32_t opcode);
  void recordOpcode(uint32_t offset, uint32_t opcode, JS::UniqueChars&& str);
  void recordOpcode(uint32_t offset, JS::UniqueChars&& str);

  // Save the debugInfo_ vector to the JIT dump file.
  void saveDebugInfo(const char* filename, uintptr_t base,
                     AutoLockPerfSpewer& lock);

  // Save the generated IR file, if any, and the debug info to the JIT dump
  // file.
  void saveJitCodeDebugInfo(JSScript* script, JitCode* code,
                            AutoLockPerfSpewer& lock);

  // Save the generated IR file, if any, and the debug info to the JIT dump
  // file.
  void saveWasmCodeDebugInfo(uintptr_t codeBase, AutoLockPerfSpewer& lock);

  void saveJSProfile(JitCode* code, JitCodeDesc& desc, JSScript* script);
  void saveWasmProfile(uintptr_t codeBase, size_t codeSize, JitCodeDesc& desc);

  virtual void disable(AutoLockPerfSpewer& lock);
  virtual void disable();

 public:
  PerfSpewer() = default;
  ~PerfSpewer();
  PerfSpewer(PerfSpewer&&);
  PerfSpewer& operator=(PerfSpewer&&);

  // Mark the start code offset that this perf spewer is relative to.
  void markStartOffset(uint32_t offset) { startOffset_ = offset; }

  // True iff this instance should record for any consumer: a process-wide perf
  // mode or the captured runtime's gecko profiler.
  bool perfEnabled() const;

  // perfEnabled() restricted to per-instruction source data (excludes IR
  // modes).
  bool perfSrcEnabled() const;

  // Start recording. This may create a temp file if we're recording IR.
  // `runtime` gates the per-runtime recording path; pass nullptr for wasm.
  virtual void startRecording(CompileRuntime* runtime = nullptr,
                              const wasm::CodeMetadata* wasmCodeMeta = nullptr);

  // Finish recording and get ready for saving to jitdump, but do not yet
  // write the debug info.
  virtual void endRecording();

  void recordOffset(MacroAssembler& masm, const char*);

  static void Init();

  static void CollectJitCodeInfo(JitCodeDesc& desc, JitCode* code,
                                 AutoLockPerfSpewer& lock);
  static void CollectJitCodeInfo(JitCodeDesc& desc, void* code_addr,
                                 uint64_t code_size, AutoLockPerfSpewer& lock);

  // Tear down recording state: free heap memory allocated using the system
  // allocator and clear the captured profiling state so recording stops. This
  // must be called when the PerfSpewer is allocated in a LifoAlloc, since the
  // destructor won't be called when the LifoAlloc is freed.
  void reset() {
    endRecording();
    debugInfo_.clearAndFree();
    irFileName_ = JS::UniqueChars();
    runtimeProfilingEnabled_ = false;
  }

  // Build a (nativeOffset, line, column) table from the per-op recordings
  // accumulated in debugInfo_ during compilation. It dedupes consecutive
  // entries that share the same (line, column). Returns an empty vector
  // when nothing was recorded (e.g. the profiler was off at compile time)
  // or on allocation failure.
  JitCodeSourceInfoVector extractSourceInfo() const;
};

void CollectPerfSpewerJitCodeProfile(JitCode* code, const char* msg);
void CollectPerfSpewerJitCodeProfile(uintptr_t base, uint64_t size,
                                     const char* msg);

void CollectPerfSpewerWasmMap(uintptr_t base, uintptr_t size,
                              JS::UniqueChars&& desc);

class IonPerfSpewer : public PerfSpewer {
  const char* CodeName(uint32_t op) override;
  const char* IRFileExtension() override;

  void disable() override;

#ifdef JS_JITSPEW
  Fprinter graphPrinter_;
  UniqueGraphSpewer graphSpewer_ = nullptr;
#endif

 public:
  IonPerfSpewer() = default;
  IonPerfSpewer(IonPerfSpewer&&) = default;
  IonPerfSpewer& operator=(IonPerfSpewer&&) = default;

  void startRecording(
      CompileRuntime* runtime,
      const wasm::CodeMetadata* wasmCodeMeta = nullptr) override;
  void endRecording() override;

  void recordPass(const char* pass, MIRGraph* graph,
                  BacktrackingAllocator* ra = nullptr);
  void recordInstruction(MacroAssembler& masm, LInstruction* ins);

  void saveJSProfile(JSContext* cx, JSScript* script, JitCode* code);
  void saveWasmProfile(uintptr_t codeBase, size_t codeSize,
                       JS::UniqueChars&& desc);
};

class WasmBaselinePerfSpewer : public PerfSpewer {
  const char* CodeName(uint32_t op) override;

  bool needsToRecordInstruction_;

 public:
  WasmBaselinePerfSpewer();
  WasmBaselinePerfSpewer(WasmBaselinePerfSpewer&&) = default;
  WasmBaselinePerfSpewer& operator=(WasmBaselinePerfSpewer&&) = default;

  [[nodiscard]] bool needsToRecordInstruction() const;
  void recordInstruction(MacroAssembler& masm, const wasm::OpBytes& op);
  void saveProfile(uintptr_t codeBase, size_t codeSize, JS::UniqueChars&& desc);
};

class BaselineInterpreterPerfSpewer : public PerfSpewer {
  // An opcode to insert into the generated IR source file.
  struct Op {
    uint32_t offset = 0;
    uint32_t opcode = 0;
    // This string is used to replace the opcode, to define things like
    // Prologue/Epilogue, or to add operand info.
    JS::UniqueChars str;

    explicit Op(uint32_t offset_, uint32_t opcode_)
        : offset(offset_), opcode(opcode_) {}
    explicit Op(uint32_t offset_, JS::UniqueChars&& str_)
        : offset(offset_), opcode(0), str(std::move(str_)) {}

    Op(Op&& copy) {
      offset = copy.offset;
      opcode = copy.opcode;
      str = std::move(copy.str);
    }

    // Do not copy the UniqueChars member.
    Op(Op& copy) = delete;
  };
  Vector<Op, 0, SystemAllocPolicy> ops_;

  const char* CodeName(uint32_t op) override;

 public:
  void recordOffset(MacroAssembler& masm, const JSOp& op);
  void recordOffset(MacroAssembler& masm, const char* name);
  void saveProfile(JitCode* code);
};

class BaselinePerfSpewer : public PerfSpewer {
  const char* CodeName(uint32_t op) override;

 public:
  void recordInstruction(MacroAssembler& masm, jsbytecode* pc, unsigned line,
                         JS::LimitedColumnNumberOneOrigin column,
                         CompilerFrameInfo& frame);
  void saveProfile(JSContext* cx, JSScript* script, JitCode* code);
};

class InlineCachePerfSpewer : public PerfSpewer {
  const char* CodeName(uint32_t op) override;

 public:
  void recordInstruction(MacroAssembler& masm, const CacheOp& op);
};

class BaselineICPerfSpewer : public InlineCachePerfSpewer {
 public:
  void saveProfile(JitCode* code, const char* stubName);
};

class IonICPerfSpewer : public InlineCachePerfSpewer {
 public:
  explicit IonICPerfSpewer(JSScript* script, jsbytecode* pc);

  void saveProfile(JSContext* cx, JSScript* script, JitCode* code,
                   const char* stubName);
};

class PerfSpewerRangeRecorder {
  using OffsetPair = std::tuple<uint32_t, JitCodeDesc>;
  Vector<OffsetPair, 0, js::SystemAllocPolicy> ranges;

  MacroAssembler& masm;

  void appendEntry(JitCodeDesc& desc);

 public:
  explicit PerfSpewerRangeRecorder(MacroAssembler& masm_) : masm(masm_) {};

  void recordOffset(const char* name);
  void recordOffset(const char* name, JSContext* cx, JSScript* script);
  void recordVMWrapperOffset(const char* name);
  void collectRangesForJitCode(JitCode* code);
};

}  // namespace jit
}  // namespace js

#endif /* jit_PerfSpewer_h */
