/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_BaselineCodeGen_h
#define jit_BaselineCodeGen_h

#include "jit/BaselineFrameInfo.h"
#include "jit/BytecodeAnalysis.h"
#include "jit/CompileWrappers.h"
#include "jit/FixedList.h"
#include "jit/MacroAssembler.h"
#include "jit/PerfSpewer.h"

namespace js {

class NamedLambdaObject;
class SourceLocationIterator;  // from vm/JSScript.h

namespace jit {

class BaselineSnapshot;

enum class ScriptGCThingType {
  Atom,
  String,
  RegExp,
  Object,
  Function,
  Scope,
  BigInt
};

// Base class for BaselineCompiler and BaselineInterpreterGenerator. The Handler
// template is a class storing fields/methods that are interpreter or compiler
// specific. This can be combined with template specialization of methods in
// this class to specialize behavior.
template <typename Handler>
class BaselineCodeGen {
 protected:
  Handler handler;

  CompileRuntime* runtime;
  MacroAssembler& masm;

  typename Handler::FrameInfoT& frame;

  // Shared epilogue code to return to the caller.
  NonAssertingLabel return_;

  NonAssertingLabel postBarrierSlot_;

  // Prologue code where we resume for Ion prologue bailouts.
  NonAssertingLabel bailoutPrologue_;

  // Prologue code where we resume for a bailout of a frame that is still
  // mid-generator-resume.
  NonAssertingLabel bailoutResumePrologue_;

  CodeOffset profilerEnterFrameToggleOffset_;
  CodeOffset profilerExitFrameToggleOffset_;

  // Early Ion bailouts will enter at this address. This is after frame
  // construction and before environment chain is initialized.
  CodeOffset bailoutPrologueOffset_;

  // Bailouts of a frame that is still mid-generator-resume enter at this
  // address.
  CodeOffset bailoutResumePrologueOffset_;

  // Baseline Interpreter can enter Baseline Compiler code at this address. This
  // is right after the warm-up counter check in the prologue.
  CodeOffset warmUpCheckPrologueOffset_;

  uint32_t pushedBeforeCall_ = 0;
#ifdef DEBUG
  bool inCall_ = false;
#endif

  template <typename... HandlerArgs>
  explicit BaselineCodeGen(TempAllocator& alloc, MacroAssembler& masmArg,
                           CompileRuntime* runtimeArg, HandlerArgs&&... args);

  template <typename T>
  void pushArg(const T& t) {
    masm.Push(t);
  }

  // Pushes the current script as argument for a VM function.
  void pushScriptArg();

  // Pushes the bytecode pc as argument for a VM function.
  void pushBytecodePCArg();

  // Pushes a name/object/scope associated with the current bytecode op (and
  // stored in the script) as argument for a VM function.
  void loadScriptGCThing(ScriptGCThingType type, Register dest,
                         Register scratch);
  void loadScriptGCThingInternal(ScriptGCThingType type, Register dest,
                                 Register scratch);
  void pushScriptGCThingArg(ScriptGCThingType type, Register scratch1,
                            Register scratch2);
  void pushScriptNameArg(Register scratch1, Register scratch2);

  // Pushes a bytecode operand as argument for a VM function.
  void pushUint8BytecodeOperandArg(Register scratch);
  void pushUint16BytecodeOperandArg(Register scratch);

  void loadInt32LengthBytecodeOperand(Register dest);
  void loadNumFormalArguments(Register dest);

  // Loads the current JSScript* in dest.
  void loadScript(Register dest);
  // Loads the current JitScript* in dest
  void loadJitScript(Register dest);

  void saveInterpreterPCReg();
  void restoreInterpreterPCReg();

  // Subtracts |script->nslots() * sizeof(Value)| from reg.
  void subtractScriptSlotsSize(Register reg, Register scratch);

  // Loads the resume entries of the current BaselineScript* in dest
  void loadBaselineScriptResumeEntries(Register dest, Register scratch);

  // Jump to the script's resume entry indicated by resumeIndex.
  void jumpToResumeEntry(Register resumeIndex, Register scratch1,
                         Register scratch2);

  // Compute the base of a resumed frame's resume args into |dest|.
  void loadResumeArgsBase(Register dest);

  void setInterpreterPCToScriptStart(Register script, Register scratch);

  // Load the global's lexical environment.
  void loadGlobalLexicalEnvironment(Register dest);
  void pushGlobalLexicalEnvironmentValue(ValueOperand scratch);

  // Load the |this|-value from the global's lexical environment.
  void loadGlobalThisValue(ValueOperand dest);

  // Computes the frame size. See BaselineFrame::debugFrameSize_.
  void computeFrameSize(Register dest);

  void prepareVMCall();

  void ensureDebugModeOSRPointSpace();

  void storeFrameSizeAndPushDescriptor(uint32_t argSize, Register scratch);

  enum class CallVMPhase { BeforePushingLocals, AfterPushingLocals };
  bool callVMInternal(VMFunctionId id, RetAddrEntry::Kind kind,
                      CallVMPhase phase);

  template <typename Fn, Fn fn>
  bool callVM(RetAddrEntry::Kind kind = RetAddrEntry::Kind::CallVM,
              CallVMPhase phase = CallVMPhase::AfterPushingLocals);

  template <typename Fn, Fn fn>
  bool callVMNonOp(CallVMPhase phase = CallVMPhase::AfterPushingLocals) {
    return callVM<Fn, fn>(RetAddrEntry::Kind::NonOpCallVM, phase);
  }

  template <typename T>
  void emitGuardedCallPreBarrierAnyZone(const T& address, MIRType type,
                                        Register scratch) {
    MOZ_ASSERT_IF(handler.realmIndependentJitcode(), !masm.maybeRealm());
    masm.guardedCallPreBarrierAnyZone(address, type, scratch);
  }

  // ifDebuggee should be a function emitting code for when the script is a
  // debuggee script. ifNotDebuggee (if present) is called to emit code for
  // non-debuggee scripts.
  template <typename F1, typename F2>
  [[nodiscard]] bool emitDebugInstrumentation(
      const F1& ifDebuggee, const mozilla::Maybe<F2>& ifNotDebuggee);
  template <typename F>
  [[nodiscard]] bool emitDebugInstrumentation(const F& ifDebuggee) {
    return emitDebugInstrumentation(ifDebuggee, mozilla::Maybe<F>());
  }

  bool emitSuspend(JSOp op);

  // ifSet should be a function emitting code for when the script has |flag|
  // set. ifNotSet emits code for when the flag isn't set.
  template <typename F1, typename F2>
  [[nodiscard]] bool emitTestScriptFlag(JSScript::ImmutableFlags flag,
                                        const F1& ifSet, const F2& ifNotSet,
                                        Register scratch);

  // If |script->hasFlag(flag) == value|, execute the code emitted by |emit|.
  template <typename F>
  [[nodiscard]] bool emitTestScriptFlag(JSScript::ImmutableFlags flag,
                                        bool value, const F& emit,
                                        Register scratch);
  template <typename F>
  [[nodiscard]] bool emitTestScriptFlag(JSScript::MutableFlags flag, bool value,
                                        const F& emit, Register scratch);

  void emitInterpJumpToResumeEntry(Register script, Register resumeIndex,
                                   Register scratch);
  void emitJumpToInterpretOpLabel();

  [[nodiscard]] bool emitCheckThis(ValueOperand val, bool reinit = false);
  void emitLoadReturnValue(ValueOperand val);
  void emitGetAliasedVar(ValueOperand dest);
  [[nodiscard]] bool emitGetAliasedDebugVar(ValueOperand dest);

  [[nodiscard]] bool emitNextIC();
  [[nodiscard]] bool emitInterruptCheck();
  [[nodiscard]] bool emitWarmUpCounterIncrement();

#define EMIT_OP(op, ...) bool emit_##op();
  FOR_EACH_OPCODE(EMIT_OP)
#undef EMIT_OP

  // JSOp::Pos, JSOp::Neg, JSOp::BitNot, JSOp::Inc, JSOp::Dec, JSOp::ToNumeric.
  [[nodiscard]] bool emitUnaryArith();

  // JSOp::BitXor, JSOp::Lsh, JSOp::Add etc.
  [[nodiscard]] bool emitBinaryArith();

  // Handles JSOp::Lt, JSOp::Gt, and friends
  [[nodiscard]] bool emitCompare();

  [[nodiscard]] bool emitConstantStrictEq(JSOp op);

  // Handles JSOp::NewObject and JSOp::NewInit.
  [[nodiscard]] bool emitNewObject();

  // For a JOF_JUMP op, jumps to the op's jump target.
  void emitJump();

  // For a JOF_JUMP op, jumps to the op's jump target depending on the Value
  // in |val|.
  void emitTestBooleanTruthy(bool branchIfTrue, ValueOperand val);

  // Converts |val| to an index in the jump table and stores this in |dest|
  // or branches to the default pc if not int32 or out-of-range.
  void emitGetTableSwitchIndex(ValueOperand val, Register dest,
                               Register scratch1, Register scratch2);

  // Jumps to the target of a table switch based on |key| and the
  // firstResumeIndex stored in JSOp::TableSwitch.
  void emitTableSwitchJump(Register key, Register scratch1, Register scratch2);

  [[nodiscard]] bool emitReturn();

  [[nodiscard]] bool emitTest(bool branchIfTrue);
  [[nodiscard]] bool emitAndOr(bool branchIfTrue);
  [[nodiscard]] bool emitCoalesce();

  [[nodiscard]] bool emitCall(JSOp op);
  [[nodiscard]] bool emitSpreadCall(JSOp op);

  [[nodiscard]] bool emitDelElem(bool strict);
  [[nodiscard]] bool emitDelProp(bool strict);
  [[nodiscard]] bool emitSetElemSuper(bool strict);
  [[nodiscard]] bool emitSetPropSuper(bool strict);

  // Try to bake in the result of BindUnqualifiedGName instead of using an IC.
  // Return true if we managed to optimize the op.
  bool tryOptimizeBindUnqualifiedGlobalName();

  [[nodiscard]] bool emitInitPropGetterSetter();
  [[nodiscard]] bool emitInitElemGetterSetter();

  [[nodiscard]] bool emitFormalArgAccess(JSOp op);

  [[nodiscard]] bool emitUninitializedLexicalCheck(const ValueOperand& val);

  [[nodiscard]] bool emitIsMagicValue(JSWhyMagic why);

  void getEnvironmentCoordinateObject(Register reg);
  Address getEnvironmentCoordinateAddressFromObject(Register objReg,
                                                    Register reg);
  Address getEnvironmentCoordinateAddress(Register reg);

  [[nodiscard]] bool emitPrologue();
  [[nodiscard]] bool emitEpilogue();
  // |emitAfterCall| is emitted on the VM-call path only, after the call
  // returns. This can be used to restore registers clobbered by the call.
  template <typename F>
  [[nodiscard]] bool emitStackCheck(RetAddrEntry::Kind kind, Register scratch1,
                                    Register scratch2, const F& emitAfterCall);
  [[nodiscard]] bool emitDebugPrologue();
  [[nodiscard]] bool emitDebugEpilogue();

  [[nodiscard]] bool initEnvironmentChain();

  [[nodiscard]] bool emitHandleCodeCoverageAtPrologue();

  [[nodiscard]] bool emitGeneratorResumePrologue();
  [[nodiscard]] bool emitGeneratorResumePrologueBody();

  void emitInitFrameFields(Register nonFunctionEnv);
  // Sets the frame's DEBUGGEE flag if the script is a debuggee script. This
  // clobbers volatile registers, so emitAfterCall is called on the path that
  // actually calls into C++ to let the caller restore registers.
  template <typename F>
  [[nodiscard]] bool emitIsDebuggeeCheck(const F& emitAfterCall);
  void emitInitializeLocals();

  void emitProfilerEnterFrame();
  void emitProfilerExitFrame();
  void emitProfilerCallSiteInstrumentation();

  void emitOutOfLinePostBarrierSlot();
};

using RetAddrEntryVector = js::Vector<RetAddrEntry, 16, SystemAllocPolicy>;
using AllocSiteIndexVector = js::Vector<uint32_t, 16, SystemAllocPolicy>;

// Interface used by BaselineCodeGen for BaselineCompiler.
class BaselineCompilerHandler {
  CompilerFrameInfo frame_;
  TempAllocator& alloc_;
  BytecodeAnalysis analysis_;
#ifdef DEBUG
  const MacroAssembler& masm_;
#endif
  FixedList<Label> labels_;
  RetAddrEntryVector retAddrEntries_;
  mozilla::Maybe<uint32_t> lastDebugModeOSRPointOffset_;
  AllocSiteIndexVector allocSiteIndices_;

  // Native code offsets for OSR at JSOp::LoopHead ops.
  using OSREntryVector =
      Vector<BaselineScript::OSREntry, 16, SystemAllocPolicy>;
  OSREntryVector osrEntries_;

  JSScript* script_;
  jsbytecode* pc_;

  size_t nargs_;

  JSObject* globalLexicalEnvironment_;
  JSObject* globalThis_;

  CallObject* callObjectTemplate_;
  NamedLambdaObject* namedLambdaTemplate_;

  // Allocated lazily on the first call to line() / column().
  mutable mozilla::Maybe<SourceLocationIterator> srcLocIter_;

  // Index of the current ICEntry in the script's JitScript.
  uint32_t icEntryIndex_;

  uint32_t baseWarmUpThreshold_;

  bool compileDebugInstrumentation_;
  bool ionCompileable_;

  bool compilingOffThread_ = false;

  bool needsEnvAllocSite_ = false;

  const SourceLocationIterator& sourceLocationIterAtCurrentPc() const;

 public:
  using FrameInfoT = CompilerFrameInfo;

  BaselineCompilerHandler(MacroAssembler& masm, TempAllocator& alloc,
                          BaselineSnapshot* snapshot);

  [[nodiscard]] bool init();

  CompilerFrameInfo& frame() { return frame_; }

  jsbytecode* pc() const { return pc_; }
  jsbytecode* maybePC() const { return pc_; }

  void moveToNextPC() { pc_ += GetBytecodeLength(pc_); }

  // The line/column of the current pc, for profiling source location info.
  unsigned line() const;
  JS::LimitedColumnNumberOneOrigin column() const;

  Label* labelOf(jsbytecode* pc) { return &labels_[script_->pcToOffset(pc)]; }

  // No code is emitted for the ops after the last reachable one.
  bool isLastReachableOp() const { return pc_ == analysis_.lastReachablePC(); }

  bool shouldEmitDebugEpilogueAtReturnOp() const {
    // The JIT uses the return address -> pc mapping and bakes in the pc
    // argument so the DebugEpilogue call needs to be part of the returning
    // bytecode op for this to work.
    return true;
  }

  JSScript* script() const { return script_; }
  JSScript* maybeScript() const { return script_; }

  size_t nargs() const {
    MOZ_ASSERT(isFunction());
    return nargs_;
  }
  CallObject* callObjectTemplate() const { return callObjectTemplate_; }
  NamedLambdaObject* namedLambdaTemplate() const {
    return namedLambdaTemplate_;
  }

  bool isFunction() const { return !!script_->function(); }

  ModuleObject* module() const { return script_->module(); }

  bool compileDebugInstrumentation() const {
    return compileDebugInstrumentation_;
  }

  bool maybeIonCompileable() const { return ionCompileable_; }
  void setIonCompileable(bool value) { ionCompileable_ = value; }

  uint32_t icEntryIndex() const { return icEntryIndex_; }
  void moveToNextICEntry() { icEntryIndex_++; }

  BytecodeAnalysis& analysis() { return analysis_; }

  RetAddrEntryVector& retAddrEntries() { return retAddrEntries_; }
  OSREntryVector& osrEntries() { return osrEntries_; }

  const mozilla::Maybe<uint32_t>& lastDebugModeOSRPointOffset() const {
    return lastDebugModeOSRPointOffset_;
  }

  [[nodiscard]] bool recordCallRetAddr(RetAddrEntry::Kind kind,
                                       uint32_t retOffset);

  // If a script has more |nslots| than this the stack check must account
  // for these slots explicitly.
  bool mustIncludeSlotsInStackCheck() const {
    static constexpr size_t NumSlotsLimit = 128;
    return script()->nslots() > NumSlotsLimit;
  }

  bool canHaveFixedSlots() const { return script()->nfixed() != 0; }

  JSObject* maybeGlobalLexicalEnvironment() const {
    return globalLexicalEnvironment_;
  }
  JSObject* globalThis() const { return globalThis_; }

  uint32_t baseWarmUpThreshold() const { return baseWarmUpThreshold_; }

  void maybeDisableIon();

  [[nodiscard]] bool addAllocSiteIndex(uint32_t entryIndex) {
    return allocSiteIndices_.append(entryIndex);
  }
  void createAllocSites();

  bool compilingOffThread() const { return compilingOffThread_; }
  void setCompilingOffThread() { compilingOffThread_ = true; }

  bool addEnvAllocSite() {
    needsEnvAllocSite_ = true;
    return true;
  }

  bool realmIndependentJitcode() const {
    return JS::Prefs::experimental_self_hosted_cache() &&
           script()->selfHosted();
  }

  bool needsProfilerCallSiteInstrumentation() const { return true; }
};

using BaselineCompilerCodeGen = BaselineCodeGen<BaselineCompilerHandler>;

class BaselineCompiler final : private BaselineCompilerCodeGen {
  // Native code offsets for bytecode ops in the script's resume offsets list.
  ResumeOffsetEntryVector resumeOffsetEntries_;

  // Native code offsets for debug traps if the script is compiled with debug
  // instrumentation.
  using DebugTrapEntryVector =
      Vector<BaselineScript::DebugTrapEntry, 0, SystemAllocPolicy>;
  DebugTrapEntryVector debugTrapEntries_;

  CodeOffset profilerPushToggleOffset_;

  CodeOffset debugModeOSREpilogueOffset_;

  BaselinePerfSpewer perfSpewer_;

 public:
  BaselineCompiler(TempAllocator& alloc, CompileRuntime* runtime,
                   MacroAssembler& masm, BaselineSnapshot* snapshot);
  [[nodiscard]] bool init();

  static bool PrepareToCompile(JSContext* cx, Handle<JSScript*> script,
                               bool compileDebugInstrumentation);

  MethodStatus compile(JSContext* cx);
  MethodStatus compileOffThread();

  bool finishCompile(JSContext* cx);

  bool compileDebugInstrumentation() const {
    return handler.compileDebugInstrumentation();
  }

 private:
  bool compileImpl();

  bool emitBody();

  [[nodiscard]] bool emitDebugTrap();
  void emitDebugModeOSREpilogue();
};

// Interface used by BaselineCodeGen for BaselineInterpreterGenerator.
class BaselineInterpreterHandler {
  InterpreterFrameInfo frame_;

  // Entry point to start interpreting a bytecode op. No registers are live. PC
  // is loaded from the frame.
  NonAssertingLabel interpretOp_;

  // Like interpretOp_ but at this point the PC is expected to be in
  // InterpreterPCReg.
  NonAssertingLabel interpretOpWithPCReg_;

  // Out-of-line code that restores a suspended generator's frame and jumps to
  // its resume point.
  NonAssertingLabel generatorResumePrologue_;

  // Offsets of toggled jumps for debugger instrumentation.
  using CodeOffsetVector = Vector<uint32_t, 0, SystemAllocPolicy>;
  CodeOffsetVector debugInstrumentationOffsets_;

  // Offsets of toggled jumps for code coverage instrumentation.
  CodeOffsetVector codeCoverageOffsets_;
  NonAssertingLabel codeCoverageAtPrologueLabel_;
  NonAssertingLabel codeCoverageAtPCLabel_;

  // Offsets of bailout stubs for IsIonInlinableOp IC ops, for Ion bailouts.
  BaselineInterpreter::ICBailoutStubOffsetVector icBailoutStubOffsets_;

  // Return offset of a call to an IsIonInlinableOp IC op. This is populated by
  // emitNextIC and cleared by the corresponding call to emitICBailoutStub.
  mozilla::Maybe<uint32_t> icReturnOffset_;

  // Offsets of some callVMs for BaselineDebugModeOSR.
  BaselineInterpreter::CallVMOffsets callVMOffsets_;

  // The current JSOp we are emitting interpreter code for.
  mozilla::Maybe<JSOp> currentOp_;

 public:
  using FrameInfoT = InterpreterFrameInfo;

  explicit BaselineInterpreterHandler(MacroAssembler& masm);

  InterpreterFrameInfo& frame() { return frame_; }

  Label* interpretOpLabel() { return &interpretOp_; }
  Label* interpretOpWithPCRegLabel() { return &interpretOpWithPCReg_; }
  Label* generatorResumePrologueLabel() { return &generatorResumePrologue_; }

  Label* codeCoverageAtPrologueLabel() { return &codeCoverageAtPrologueLabel_; }
  Label* codeCoverageAtPCLabel() { return &codeCoverageAtPCLabel_; }

  CodeOffsetVector& debugInstrumentationOffsets() {
    return debugInstrumentationOffsets_;
  }
  CodeOffsetVector& codeCoverageOffsets() { return codeCoverageOffsets_; }

  BaselineInterpreter::ICBailoutStubOffsetVector& icBailoutStubOffsets() {
    return icBailoutStubOffsets_;
  }

  void setICReturnOffset(uint32_t offset) {
    MOZ_ASSERT(icReturnOffset_.isNothing());
    icReturnOffset_.emplace(offset);
  }
  uint32_t takeICReturnOffset() {
    MOZ_ASSERT(icReturnOffset_.isSome());
    uint32_t offset = *icReturnOffset_;
    icReturnOffset_.reset();
    return offset;
  }

  void setCurrentOp(JSOp op) { currentOp_.emplace(op); }
  void resetCurrentOp() { currentOp_.reset(); }
  mozilla::Maybe<JSOp> currentOp() const { return currentOp_; }

  // Interpreter doesn't know the script and pc statically.
  jsbytecode* maybePC() const { return nullptr; }
  bool isLastReachableOp() const { return false; }
  JSScript* maybeScript() const { return nullptr; }

  bool shouldEmitDebugEpilogueAtReturnOp() const {
    // The interpreter doesn't use the return address -> pc mapping and doesn't
    // bake in bytecode PCs so it can emit a shared DebugEpilogue call instead
    // of duplicating it for every return op.
    return false;
  }

  [[nodiscard]] bool addDebugInstrumentationOffset(CodeOffset offset);

  const BaselineInterpreter::CallVMOffsets& callVMOffsets() const {
    return callVMOffsets_;
  }

  [[nodiscard]] bool recordCallRetAddr(RetAddrEntry::Kind kind,
                                       uint32_t retOffset);

  bool maybeIonCompileable() const { return true; }

  // The interpreter doesn't know the number of slots statically so we always
  // include them.
  bool mustIncludeSlotsInStackCheck() const { return true; }

  bool canHaveFixedSlots() const { return true; }
  JSObject* maybeGlobalLexicalEnvironment() const { return nullptr; }

  bool addEnvAllocSite() { return false; }  // Not supported.

  bool realmIndependentJitcode() const { return true; }

  bool needsProfilerCallSiteInstrumentation() const { return false; }
};

using BaselineInterpreterCodeGen = BaselineCodeGen<BaselineInterpreterHandler>;

class BaselineInterpreterGenerator final : private BaselineInterpreterCodeGen {
  // Offsets of patchable call instructions for debugger breakpoints/stepping.
  Vector<uint32_t, 0, SystemAllocPolicy> debugTrapOffsets_;

  // Offsets of move instructions for tableswitch base address.
  Vector<CodeOffset, 0, SystemAllocPolicy> tableLabels_;

  // Offset of the first tableswitch entry.
  uint32_t tableOffset_ = 0;

  // Offset of the code to start interpreting a bytecode op.
  uint32_t interpretOpOffset_ = 0;

  // Like interpretOpOffset_ but skips the debug trap for the current op.
  uint32_t interpretOpNoDebugTrapOffset_ = 0;

  // Offset of the jump (tail call) to the debug trap handler trampoline code.
  // When the debugger is enabled, NOPs are patched to calls to this location.
  uint32_t debugTrapHandlerOffset_ = 0;

  BaselineInterpreterPerfSpewer perfSpewer_;

 public:
  explicit BaselineInterpreterGenerator(JSContext* cx, TempAllocator& alloc,
                                        MacroAssembler& masm);

  [[nodiscard]] bool generate(JSContext* cx, BaselineInterpreter& interpreter);

 private:
  [[nodiscard]] bool emitInterpreterLoop();
  [[nodiscard]] bool emitDebugTrap();
  [[nodiscard]] bool emitICBailoutStub();

  void emitOutOfLineCodeCoverageInstrumentation();
  [[nodiscard]] bool emitOutOfLineGeneratorResumePrologue();
};

}  // namespace jit
}  // namespace js

#endif /* jit_BaselineCodeGen_h */
