/*
 * Copyright 2014 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef wasm_frame_iter_h
#define wasm_frame_iter_h

#include "js/ColumnNumber.h"  // JS::TaggedColumnNumberOneOrigin
#include "js/ProfilingFrameIterator.h"
#include "js/TypeDecls.h"

#include "wasm/WasmCode.h"          // For CodeBlockKind
#include "wasm/WasmCodegenTypes.h"  // for BytecodeOffsetSpan

namespace js {

namespace jit {
class JitActivation;
class MacroAssembler;
struct Register;
enum class FrameType;
}  // namespace jit

namespace wasm {

class CallIndirectId;
class Code;
class CodeRange;
class DebugFrame;
class Instance;
class Instance;

struct CallableOffsets;
struct ImportOffsets;
struct FuncOffsets;
struct Offsets;
class Frame;
class FrameWithInstances;
struct Handlers;
class ContStack;

using RegisterState = JS::ProfilingFrameIterator::RegisterState;

// Iterates over a linear group of wasm frames of a single wasm JitActivation,
// called synchronously from C++ in the wasm thread. It will stop at the first
// frame that is not of the same kind, or at the end of an activation.
//
// If you want to handle every kind of frames (including JS jit frames), use
// JitFrameIter.

class WasmFrameIter {
  //
  // State that is constant for the entire wasm activation
  //

  JSContext* cx_ = nullptr;
  jit::JitActivation* activation_ = nullptr;
  bool isLeavingFrames_ = false;
  bool enableInlinedFrames_ = false;

  Instance* exitInstance_ = nullptr;

  //
  // State that is updated for every frame
  //

  const Code* code_ = nullptr;
  uint32_t funcIndex_ = UINT32_MAX;
  uint32_t lineOrBytecode_ = UINT32_MAX;
  BytecodeOffsetSpan inlinedCallerOffsets_;
  Frame* fp_ = nullptr;
  Instance* instance_ = nullptr;
  // The address of the next instruction that will execute in this frame, once
  // control returns to this frame.
  uint8_t* resumePCinCurrentFrame_ = nullptr;
  // See wasm::TrapData for more information.
  bool failedUnwindSignatureMismatch_ = false;
  // Whether the current frame is on a different stack from the previous stack.
  bool currentFrameStackSwitched_ = false;
#ifdef ENABLE_WASM_JSPI
  ContStack* contStack_ = nullptr;
  // A continuation stack that the trap occurred on but which has no frame in
  // this iteration (its only frame was torn down by a trapping return_call).
  // Exception unwinding must free it even though no iterated frame is on it.
  ContStack* unwoundContStack_ = nullptr;
#endif

  //
  // State that is found after we've unwound the entire wasm activation
  //

  // The address of the final wasm::Frame::returnAddress_. Only found once
  // we've iterated over all wasm frames.
  void** unwoundAddressOfReturnAddress_ = nullptr;
  // The value of the final wasm::Frame::callerFP_. Only found once we've
  // iterated over all wasm frames.
  uint8_t* unwoundCallerFP_ = nullptr;
  // Whether unwoundCallerFP_ is a JS JIT exit frame.
  bool unwoundCallerFPIsJSJit_ = false;

  // Pop the frame. `isLeavingFrame` indicates if we should update the
  // JitActivation so that any other frame iteration doesn't see the frame we
  // just popped. This is normally equal to `isLeavingFrames_`, but is
  // different for the very first `popFrame` of a wasm exit frame.
  void popFrame(bool isLeavingFrame);

#ifdef ENABLE_WASM_JSPI
  // Switch iteration from a continuation's base frame (`fp_` must point at the
  // ContBaseFrame) to the resume target on the parent stack, leaving the
  // iterator positioned at the resuming frame.
  void popContBaseFrame();
#endif

 public:
  // See comment above this class definition.
  explicit WasmFrameIter(jit::JitActivation* activation, Frame* fp = nullptr);

  // Iterate over frames from a known starting (instance, fp, ra).
  WasmFrameIter(Instance* instance, Frame* fp, void* returnAddress);

  // Cause this WasmFrameIter to remove every popped from its JitActivation so
  // that any other frame iteration will not see it.
  //
  // This is a method instead of a parameter to the constructor because it
  // needs to be enabled after FrameIter has already created the WasmFrameIter.
  void setIsLeavingFrames() {
    MOZ_ASSERT(activation_);
    MOZ_ASSERT(!isLeavingFrames_);
    isLeavingFrames_ = true;
  }

  // Visit inlined frames instead of only 'physical' frames. This is required
  // to access source information.
  void enableInlinedFrames() { enableInlinedFrames_ = true; }

  // The JSContext all frames will be under.
  JSContext* cx() const { return cx_; }

  //
  // Iteration methods
  //

  void operator++();
  bool done() const;

  //
  // Source information about the current frame
  //

  bool hasSourceInfo() const;
  const char* filename() const;
  const char16_t* displayURL() const;
  bool mutedErrors() const;
  JSAtom* functionDisplayAtom() const;
  unsigned lineOrBytecode() const;
  uint32_t funcIndex() const;
  unsigned computeLine(JS::TaggedColumnNumberOneOrigin* column) const;

  //
  // Physical information about the current (not inlined) frame
  //

  // The instance that the function for this wasm frame is from.
  Instance* instance() const {
    MOZ_ASSERT(!done());
    // Getting the instance always works even with inlining because we never
    // inline across instances.
    return instance_;
  }

  // The instance that the wasm exit.
  Instance* exitInstance() const { return exitInstance_; }

  // Forget the exit instance, so that exitInstance() reports nullptr for the
  // rest of the iteration. Used by callers that only need to observe it once.
  void resetExitInstance() { exitInstance_ = nullptr; }

  // The wasm function frame pointer.
  Frame* frame() const {
    MOZ_ASSERT(!done());
    MOZ_ASSERT(!enableInlinedFrames_);
    return fp_;
  }

  // Returns the address of the next instruction that will execute in this
  // frame, once control returns to this frame.
  uint8_t* resumePCinCurrentFrame() const {
    MOZ_ASSERT(!done());
    MOZ_ASSERT(!enableInlinedFrames_);
    return resumePCinCurrentFrame_;
  }

  // Whether the current frame is on a different stack from the previous frame.
  bool currentFrameStackSwitched() const {
    MOZ_ASSERT(!done());
    return currentFrameStackSwitched_;
  }

#ifdef ENABLE_WASM_JSPI
  ContStack* contStack() const {
    MOZ_ASSERT(!done());
    return contStack_;
  }

  // See unwoundContStack_. Null in all cases except the trapping return_call
  // described there.
  ContStack* unwoundContStack() const { return unwoundContStack_; }
#endif

  //
  // Debug information about the current frame
  //

  // Whether this frame has a debuggable wasm frame.
  bool debugEnabled() const;

  // The debuggable wasm frame, if any.
  DebugFrame* debugFrame() const;

  //
  // Information for after we've unwound the entire wasm activation
  //

  // The address of the final wasm::Frame::returnAddress_.
  void** unwoundAddressOfReturnAddress() const {
    MOZ_ASSERT(done());
    MOZ_ASSERT(unwoundAddressOfReturnAddress_);
    return unwoundAddressOfReturnAddress_;
  }

  // The value of the final wasm::Frame::callerFP_.
  uint8_t* unwoundCallerFP() const {
    MOZ_ASSERT(done());
    MOZ_ASSERT(unwoundCallerFP_);
    return unwoundCallerFP_;
  }

  // Whether 'unwoundCallerFP' is for a JS JIT frame or not.
  bool unwoundCallerFPIsJSJit() const {
    MOZ_ASSERT(done());
    MOZ_ASSERT_IF(unwoundCallerFPIsJSJit_, unwoundCallerFP_);
    return unwoundCallerFPIsJSJit_;
  }
};

enum class SymbolicAddress;

// An ExitReason describes the possible reasons for leaving compiled wasm
// code or the state of not having left compiled wasm code
// (ExitReason::None). It is either a known reason, or a enumeration to a native
// function that is used for better display in the profiler.
class ExitReason {
 public:
  enum class Fixed : uint32_t {
    None,           // default state, the pc is in wasm code
    ImportJit,      // fast-path call directly into JIT code
    ImportInterp,   // slow-path call into C++ Invoke()
    BuiltinNative,  // fast-path call directly into native C++ code
    Trap,           // call to trap handler
    DebugStub,      // call to debug stub
    RequestTierUp   // call to request tier-2 compilation
  };

 private:
  uint32_t payload_;

  ExitReason() : ExitReason(Fixed::None) {}

 public:
  MOZ_IMPLICIT ExitReason(Fixed exitReason)
      : payload_(0x0 | (uint32_t(exitReason) << 1)) {
    MOZ_ASSERT(isFixed());
    MOZ_ASSERT_IF(isNone(), payload_ == 0);
  }

  explicit ExitReason(SymbolicAddress sym)
      : payload_(0x1 | (uint32_t(sym) << 1)) {
    MOZ_ASSERT(uint32_t(sym) <= (UINT32_MAX << 1), "packing constraints");
    MOZ_ASSERT(!isFixed());
  }

  static ExitReason Decode(uint32_t payload) {
    ExitReason reason;
    reason.payload_ = payload;
    return reason;
  }

  static ExitReason None() { return ExitReason(ExitReason::Fixed::None); }

  bool isFixed() const { return (payload_ & 0x1) == 0; }
  bool isNone() const { return isFixed() && fixed() == Fixed::None; }
  bool isNative() const {
    return !isFixed() || fixed() == Fixed::BuiltinNative;
  }

  uint32_t encode() const { return payload_; }
  Fixed fixed() const {
    MOZ_ASSERT(isFixed());
    return Fixed(payload_ >> 1);
  }
  SymbolicAddress symbolic() const {
    MOZ_ASSERT(!isFixed());
    return SymbolicAddress(payload_ >> 1);
  }
};

// Iterates over the frames of a single wasm JitActivation, given an
// asynchronously-profiled thread's state.
class ProfilingFrameIterator {
 public:
  enum class Category {
    Baseline,
    Ion,
    Other,
  };

 private:
  const Code* code_;
  const CodeRange* codeRange_;
  Category category_;
  uint8_t* callerFP_;
  void* callerPC_;
  void* stackAddress_;
  // See JS::ProfilingFrameIterator::endStackAddress_ comment.
  void* endStackAddress_ = nullptr;
  uint8_t* unwoundJitCallerFP_;
  ExitReason exitReason_;

  void initFromExitFP(const Frame* fp);

 public:
  ProfilingFrameIterator();

  // Start unwinding at a non-innermost activation that has necessarily been
  // exited from wasm code (and thus activation.hasWasmExitFP).
  explicit ProfilingFrameIterator(const jit::JitActivation& activation);

  // Start unwinding at a group of wasm frames after unwinding an inner group
  // of JSJit frames.
  explicit ProfilingFrameIterator(const Frame* fp);

  // Start unwinding at the innermost activation given the register state when
  // the thread was suspended.
  ProfilingFrameIterator(const jit::JitActivation& activation,
                         const RegisterState& state);

  void operator++();

  bool done() const {
    MOZ_ASSERT_IF(!exitReason_.isNone(), codeRange_);
    return !codeRange_;
  }

  void* stackAddress() const {
    MOZ_ASSERT(!done());
    return stackAddress_;
  }
  uint8_t* unwoundJitCallerFP() const {
    MOZ_ASSERT(done());
    return unwoundJitCallerFP_;
  }
  const char* label() const;

  Category category() const;

  void* endStackAddress() const { return endStackAddress_; }

  // Convert a CodeBlockKind to a Category.
  static ProfilingFrameIterator::Category categoryFromCodeBlock(
      CodeBlockKind kind) {
    if (kind == CodeBlockKind::BaselineTier) {
      return ProfilingFrameIterator::Category::Baseline;
    }
    if (kind == CodeBlockKind::OptimizedTier) {
      return ProfilingFrameIterator::Category::Ion;
    }
    return ProfilingFrameIterator::Category::Other;
  }
};

const char* ThunkedNativeToDescription(SymbolicAddress func);

// Prologue/epilogue code generation

void LoadActivation(jit::MacroAssembler& masm, jit::Register instance,
                    jit::Register dest);
void SetExitFP(jit::MacroAssembler& masm, ExitReason reason,
               jit::Register activation, jit::Register scratch);
void ClearExitFP(jit::MacroAssembler& masm, jit::Register activation);

// [SMDOC] Wasm FP environment (denormals)
//
// AudioWorklets want to disable denormal FP support for performance reasons on
// x86/x64 processors.
//
// This means changing the FP state to enable:
//   1. 'flush-to-zero' (FTZ) - instruction outputs don't emit denormals
//   2. 'denormals-are-zero' (DAZ) - instruction inputs ignore denormals
//
// Disabling this across all of SpiderMonkey can lead to surprising results and
// is difficult to verify for correctness. Instead, we limit the scope of this
// to just wasm JIT code. Specifically the body of wasm functions after all JS
// value conversions are done.
//
// To make this as efficient as possible, we store on wasm::Instance:
//   1. hasWasmMxcsr - snapshot of ContextOptions::wasmDisablesDenormals()
//   2. ieeeMxcsr_ - snapshot of thread's mxcsr
//   3. wasmMxcsr_ - ieeeMxcsr_ with FTZ/DAZ set iff `wasmDisablesDenormals`
//
// Entering wasm code does: `if hasWasmMxcsr: ldmxcsr wasmMxcsr_`.
// Leaving wasm code does: `if hasWasmMxcsr: ldmxcsr ieeeMxcsr_`.
//
// We need to do this on the following locations:
//   1. Interpreter entry - Handled directly in GenerateInterpEntry.
//   2. Interpreter exit - Handled by GenerateExitPrologue/Epilogue in
//        GenerateImportInterpExit.
//   3. JS JIT entry - Handled directly in GenerateJitEntry.
//   4. JS JIT 'direct' entry - Handled directly in GenerateDirectCallFromJit.
//   5. JS JIT exit - Handled by GenerateJitExitPrologue/Epilogue.
//   6. Calls into the VM - Handled by GenerateExitPrologue/Epilogue
//      in GenerateBuiltinThunk.
//   7. Trap exit (signal handler) - Handled directly in GenerateTrapExit.
//   8. Debug breakpoints - Handled by GenerateExitPrologue/Epilogue in
//      GenerateDebugStub.
//   9. Throwing - GenerateThrowStub is always jumped to in the system FP state.
//   10. Catching - GenerateJumpToCatchHandler handles the switch back to wasm FP
//       state.
//
// Entering must happen from the system FP state, and that is asserted.
// Leaving is idempotent and has no such requirement: unwinding reaches some
// of the locations above with the system FP state already restored.
//
// We currently only do this on x86/x64 and not ARM64. ARM64 processors do not
// exhibit the same performance issues as x86.
void GenerateEnterWasmFPEnvironment(jit::MacroAssembler& masm,
                                    jit::Register instance);
void GenerateLeaveWasmFPEnvironment(jit::MacroAssembler& masm,
                                    jit::Register instance);

// Debug-only checks that the FP environment has denormals enabled.
void AssertDenormalsEnabled(jit::MacroAssembler& masm);

#ifdef ENABLE_WASM_JSPI
// [SMDOC] Wasm dynamic stack switches on 'exit'
//
// The SpiderMonkey codebase and embedders, shouldn't run on wasm continuation
// stacks. Some code theoretically could work okay on an alternative stack, but
// we want to be conservative and not assume that. This gives us flexibility to
// use smaller stacks than the main stack and not worry about stack overflow.
//
// To ensure this, all wasm 'exits' from JIT to the VM are instrumented to
// perform a dynamic check and switch to the main stack if they are currently
// running on a wasm stack.
//
// This is done in the prologue of the exit, and reversed in the epilogue.
//
// If we're running on a cont stack, we switch SP to the main stack's SP,
// but keep the FP pointing at the original FP on the incoming stack:
//
//   cont stack
//  ┌────────────────┐
//  │ Caller Args    │
//  ├────────────────┤
//  │ wasm::Frame    │
//  └────────────────┘◄───── FP
//
//                           SP
//   Main Stack               │
//  ┌────────────────┐        │
//  │ Previous       │        │
//  │ Frames         │        │
//  ├────────────────┤        │
//  │                │        │
//  │ framePushed()  │        │
//  │ for Exit Stub  │        │
//  │                │        │
//  └────────────────┘◄───────┘
//
// If we're not running on a cont stack, nothing is done at all and
// SP/FP are unchanged:
//
//   Main Stack
//  ┌────────────────┐
//  │ Caller Args    │
//  ├────────────────┤
//  │ wasm::Frame    │
//  ├────────────────┤◄───── FP
//  │                │       SP
//  │ framePushed()  │        │
//  │ for exit stub  │        │
//  │                │        │
//  └────────────────┘◄───────┘
//
// This 'split' function body lets the function still address all the incoming
// arguments through FP, and it's own 'framePushed' through SP.
//
// However this means the SP/FP are no longer guaranteed to be contiguous (they
// are in the main stack case, but we don't know that statically). So the
// function body must not access the original frame or incoming arguments
// through SP, or the 'framePushed' area through FP.
void GenerateExitPrologueMainStackSwitch(jit::MacroAssembler& masm,
                                         jit::Address savedStackSlots,
                                         jit::Register instance,
                                         jit::Register scratch1,
                                         jit::Register scratch2,
                                         jit::Register scratch3);

// Generate the dynamic switch back to the wasm cont stack we originally
// were on. See "Wasm dynamic stack switches on 'exit'" for more information.
//
// NOTE: this doesn't actually switch SP back to the original SP. The caller
// must do that through some method, such as setting SP := FP.
void GenerateExitEpilogueMainStackReturn(jit::MacroAssembler& masm,
                                         jit::Address savedStackSlots,
                                         jit::Register instance,
                                         jit::Register scratch1,
                                         jit::Register scratch2);
#endif

enum class ExitFrameAlignment {
  // Assume the stack was aligned to ABIStackAlignment when the call
  // instruction happened.
  Static,
  // Assume the stack was word aligned, but not necessarily at
  // ABIStackAlignment when the call instruction happened.
  Dynamic,
};

// Generate an 'exit' prologue.
//
// This will exit the JitActivation, allowing arbitrary code to run. The
// `reason` will be noted on the JitActivation for any future stack iteration.
//
// If `switchToMainStack` is true, the prologue will check if a suspendable
// stack is active, and if so switch the stack to the main stack.
//
// In this case, the body of the exit function will have a 'split' sp/fp where
// the fp points at the wasm::Frame on the cont stack and the sp points
// to the main stack. See "Wasm dynamic stack switches on 'exit'" above for more
// information and a diagram.
//
// `alignment` is used to perform static or dynamic alignment of the stack, and
// `frameSized` will be reserved on the final stack (either the
//  original stack, or the main stack if there is a switch).
void GenerateExitPrologue(jit::MacroAssembler& masm, ExitReason reason,
                          bool switchToMainStack, ExitFrameAlignment alignment,
                          unsigned frameSize, CallableOffsets* offsets);
// Generate an 'exit' epilogue that is the inverse of
// wasm::GenerateExitPrologue.
void GenerateExitEpilogue(jit::MacroAssembler& masm, ExitReason reason,
                          bool switchToMainStack, ExitFrameAlignment alignment,
                          CallableOffsets* offsets);

// Generate the most minimal possible prologue/epilogue: `push FP; FP := SP`
// and `pop FP; return` respectively.
void GenerateMinimalPrologue(jit::MacroAssembler& masm, uint32_t* entry);
void GenerateMinimalEpilogue(jit::MacroAssembler& masm, uint32_t* ret);

void GenerateJitExitPrologue(jit::MacroAssembler& masm, uint32_t fallbackOffset,
                             ImportOffsets* offsets);
void GenerateJitExitEpilogue(jit::MacroAssembler& masm,
                             CallableOffsets* offsets);

void GenerateJitEntryPrologue(jit::MacroAssembler& masm,
                              CallableOffsets* offsets);
void GenerateJitEntryEpilogue(jit::MacroAssembler& masm,
                              CallableOffsets* offsets);

void GenerateFunctionPrologue(jit::MacroAssembler& masm,
                              const CallIndirectId& callIndirectId,
                              const mozilla::Maybe<uint32_t>& tier1FuncIndex,
                              FuncOffsets* offsets);
void GenerateFunctionEpilogue(jit::MacroAssembler& masm, unsigned framePushed,
                              FuncOffsets* offsets);

// Iterates through frames for either possible cross-instance call or an entry
// stub to obtain instance that corresponds to the passed fp.
const Instance* GetNearestEffectiveInstance(const Frame* fp);
Instance* GetNearestEffectiveInstance(Frame* fp);

// Describes register state and associated code at a given call frame.

struct UnwindState {
  uint8_t* fp;
  void* pc;
  const Code* code;
  const CodeRange* codeRange;
  UnwindState() : fp(nullptr), pc(nullptr), code(nullptr), codeRange(nullptr) {}
};

// Ensures the register state at a call site is consistent: pc must be in the
// code range of the code described by fp. This prevents issues when using
// the values of pc/fp, especially at call sites boundaries, where the state
// hasn't fully transitioned from the caller's to the callee's.
//
// unwoundCaller is set to true if we were in a transitional state and had to
// rewind to the caller's frame instead of the current frame.
//
// Returns true if it was possible to get to a clear state, or false if the
// frame should be ignored.

bool StartUnwinding(const RegisterState& registers, UnwindState* unwindState,
                    bool* unwoundCaller);

}  // namespace wasm
}  // namespace js

#endif  // wasm_frame_iter_h
