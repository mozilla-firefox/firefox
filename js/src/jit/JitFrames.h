/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_JitFrames_h
#define jit_JitFrames_h

#include "mozilla/Assertions.h"

#include <stddef.h>
#include <stdint.h>

#include "jit/CalleeToken.h"
#include "jit/MachineState.h"
#include "jit/Registers.h"
#include "js/Id.h"
#include "js/TypeDecls.h"
#include "js/Value.h"

namespace js {

namespace wasm {
class Instance;
struct StackTarget;
struct Handlers;
}  // namespace wasm

namespace jit {

enum class FrameType;
enum class VMFunctionId;
class IonScript;
class JitActivation;
class JitFrameLayout;
struct SafepointSlotEntry;
struct VMFunctionData;

/* [SMDOC] The JS ABIs
 *
 * ## Overview
 *
 * When calling from one sequence of jitcode to another, we are free to define
 * our own ABI. We make full use of this. When the caller and callee are fixed,
 * we often use bespoke hand-written ABIs: for example, the ABI for calling into
 * a regexp stub, or the tight coupling between an Ion IC and the corresponding
 * Ion code. However, there are some ABIs that are more broadly shared, and some
 * common principles.
 *
 * 1. As much as possible, our ABIs are architecture-independent. We use the
 *    same ABI on each platform. Some differences are unavoidable: for example,
 *    x86 pushes a return address as part of the call instruction, while most
 *    other platforms use a link register. In these cases, we sync up as quickly
 *    as possible (for example, by immediately pushing the return address on
 *    link register platforms).
 * 2. To simplify profiling and stack walking, we use frame pointers. There are
 *    a handful of exceptions, mostly involving tail calls. For example,
 *    baseline ICs tail-call each other, so they only push a frame pointer when
 *    making a call that could trigger a GC.
 * 3. Immediately after pushing a frame pointer, we require stack alignment to
 *    allow us to spill registers if necessary. This varies by architecture and
 *    is defined by JitStackAlignment. The caller is responsible for alignment
 *    padding.
 * 4. Whenever we make a call to a target that could examine the stack (for
 *    example, collecting stack roots for a GC), we push a frame descriptor
 *    immediately before the call. Combined with the return address and frame
 *    pointer, this is a CommonFrameLayout. The frame descriptor describes the
 *    type of the *caller*, and the number of arguments being passed.
 *
 * The remainder of this comment will focus on many-to-many ABIs with multiple
 * callers/callees.
 *
 * ## JS function ABI:
 *
 * This is the ABI that JS functions expect upon entry: Ion, Baseline,
 * BaselineInterpreter, the C++ interpreter stub, and any other jitcode
 * that could be the target of a script's jitCodeRaw pointer.
 *
 * All arguments are passed on the stack.
 *
 *    .               .
 *    |  (Padding?)   |
 *    +---------------+
 *    |   NewTarget?  | <-- Only for constructors.
 *    +---------------+
 *    |     ArgN      | <-- There must be at least as many arguments as the
 *    +---------------+     callee has formal parameters, but there may be
 *    |     ...       |     more. They can be accessed via `arguments` or a
 *    +---------------+     rest parameter.
 *    |     Arg0      |
 *    +---------------+
 *    |     ThisV     |
 *    +---------------+ <-- Everything above this point is a JS::Value.
 *    |  CalleeToken  |     Everything below is word-sized. Aligning this
 *    +---------------+     point to JitStackAlignment will guarantee correct
 *    |   Descriptor  |     alignment below.
 *    +---------------+
 *    |   ReturnAddr  |
 *    +===============+     After the callee pushes this, the stack should be
 *    |    FramePtr   | <-- aligned to JitStackAlignment (defined per-arch).
 *    +---------------+     If alignment padding is needed, the caller must
 *    |     ...       |     insert it above the last arg / new target.
 *
 *  CalleeToken:
 *    The callee token is either a pointer to the callee JSFunction, or, for
 *    non-functions (top-level scripts, modules, eval), the JSScript.  The token
 *    is low-bit-tagged to indicate a) whether the callee is a script, and b)
 *    whether a function is being invoked as a constructor. NewTarget is only
 *    present if the constructing bit is set. When the callee is a function,
 *    the environment chain is in the callee. When the callee is a non-function,
 *    the environment chain is passed to baseline in `R1.scratchReg()`. This is
 *    the only case where we pass a value in a register to a JS JIT frame.
 *
 *  Descriptor:
 *    The frame descriptor word contains, packed together:
 *    1. A FrameType describing the type of the *caller's* frame. This is pushed
 *       before calling so that code that walks the stack (eg the GC) can easily
 *       tell what kind of frame it's looking at.
 *    2. The number of actual arguments passed by the caller. If the caller did
 *       not pass as many arguments as the callee has formal parameters, then
 *       numActualArgs may be smaller than the number of arguments present on
 *       the stack. Missing arguments must be filled in with `undefined` before
 *       calling. Note that it is valid for numActualArgs to be larger than the
 *       number of formal parameters in the callee.
 *    It also contains flag bits. See the Frame Descriptor Layout SMDOC for
 * more.
 *
 *  Resuming a generator:
 *    When resuming a suspended generator or async function/module, the caller
 *    also pushes the resume args (see ResumeFrameArgs): the resume value, the
 *    generator object, the resume kind, and the resume index. For function
 *    callees these are pushed above the formals and numActualArgs is 0. Module
 *    callees have no ThisV and no arguments, so the resume args occupy those
 *    slots instead.
 *    The caller signals their presence with the IsResumingGenerator descriptor
 *    bit, which also tells the callee's prologue to dispatch to the resume
 *    point. JSOp::AfterYield clears the bit, after which these slots must no
 *    longer be accessed.
 *
 * ## Baseline IC ABI
 *
 * This is the ABI that baseline ICs expect upon entry. Unlike the function ABI,
 * the baseline IC ABI passes some arguments in registers.
 *
 * The baseline compiler is written using three architecture-independent
 * Value-sized registers (each of which is a register pair on 32-bit
 * architectures). When calling a baseline IC, the first two arguments are
 * passed in R0 and R1. Subsequent arguments are passed on the stack. If there
 * is a return value, it is passed in R0. If an argument is smaller than a Value
 * (for example, a JSObject* on a 32-bit architecture), it is passed in the
 * payload register, and the type register is made available for the register
 * allocator. See BaselineCacheIRCompiler::init().
 *
 * Upon entry to a baseline IC, no frame descriptor is pushed:
 *
 *  Stack:
 *    .               .
 *    |               | <-- Top of the expression stack in the calling baseline
 *    +---------------+     frame (ignoring arguments to the IC.
 *    |     Arg2?     |
 *    +---------------+
 *    |  ReturnAddr?  | <-- Not pushed yet on link-register architectures.
 *    +---------------+
 *
 *  Registers:
 *    R0: Arg0
 *    R1: Arg1
 *    ICStubReg: points to active ICStub
 *
 * The stub code is free to push values on the stack.  If a stub guard fails,
 * then the stub code will restore the operands to their initial values, update
 * the ICStubReg to point to the next field of the current stub, and *jump* to
 * that stub's code, performing a tail call.
 *
 * If all stub guards have succeeded, but the stub must perform a call that
 * could GC, we enter a stub frame. This discards any values above the original
 * stack pointer and retroactively rewrites the stack as if we had pushed a
 * frame:
 *
 *  Original Stack:               Entered Stub Frame:
 *    .               .              .               .
 *    |               |              |               |
 *    +---------------+              +---------------+
 *    |     Arg2?     |              |     Arg2?     |
 *    +---------------+              +---------------+
 *    |  ReturnAddr?  | -----        |   Descriptor  |
 *    +---------------+      |       +---------------+
 *                            --->   |   ReturnAddr  |
 *                                   +---------------+
 *                                   |    FramePtr   |
 *                                   +---------------+
 *                                   |   ICStubReg   |
 *                                   +---------------+
 *
 * This lets us avoid setting up and tearing down an unnecessary frame for
 * simple ICs like Int32Add. The diagram on the right corresponds to
 * BaselineStubFrameLayout.
 *
 * ## Entry and exit frames
 *
 * A sequence of JS JIT frames on the stack always begins with an entry frame.
 * Unless it's currently executing, it ends with an exit frame. To walk the
 * stack from C++, an exit frame must exist.
 *
 * When walking the stack from newest to oldest frames, an entry frame marks the
 * point where the frame iterator does something different. There are two types
 * of entry frames: CPPToJSJit and WasmToJSJit.
 * - CPPToJSJit frames begin a JitActivation. There is at most one of them per
 *   JitActivation, which corresponds with a call to the EnterJit or
 *   EnterBaseline trampolines. The trampolines construct a JitFrameLayout for
 *   the callee's frame. When encountering a CPPToJSJit frame, an iterator
 *   can advance to the next JitActivation.
 * - WasmToJSJit frames mark the point within a JitActivation where we switch
 *   from Wasm to JS code. There can be an arbitrary number of such frames
 *   within a single JitActivation. When encountering a WasmToJSJit frame, an
 *   iterator will start walking Wasm frames (see WasmFrameIter).
 *
 * There are many different ExitFrame types. Most of them represent a call into
 * C++, and terminate a JitActivation. There are also two types of exit frames
 * to represent a call from JS into Wasm:
 * - ExitFrameType::DirectWasmJitCall is used for calls from Ion directly into
 *   Wasm, using the Wasm ABI.
 * - ExitFrameType::WasmGenericJitEntry calls a generated JitEntry stub that
 *   uses the JS ABI and internally converts it to the Wasm ABI. This is less
 *   efficient, but allows us to call Wasm functions in all places where we
 *   can call JS functions.
 *
 * [SMDOC] Frame Descriptor Layout
 *
 * A frame descriptor word has the following data:
 *
 *    high bits: [ numActualArgs |
 *                 is-resuming-generator bit |
 *                 has-inlined-icscript bit |
 *                 has-cached-saved-frame bit |
 *    low bits:    frame type ]
 *
 *
 * * numActualArgs: for JitFrameLayout, the number of arguments passed by the
 *   caller.
 * * HasInlinedICScript: Set when passing a private ICScript to a trial-inlined
 *   script.
 * * IsResumingGenerator: Set when this frame is a resumed generator/async
 *   frame. The callee's prologue dispatches to the resume point if this bit is
 *   set. Cleared by JSOp::AfterYield.
 * * HasCachedSavedFrame: Used to power the LiveSavedFrameCache optimization.
 *   See the comment in Activation.h
 * * Frame Type: BaselineJS, Exit, etc. (jit::FrameType)
 */

class FrameDescriptor {
 public:
  static const uint32_t TypeBits = 4;
  static const uint32_t TypeMask = (1 << TypeBits) - 1;
  static const uint32_t HasCachedSavedFrame = 1 << TypeBits;
  static const uint32_t HasInlinedICScript = 1 << (TypeBits + 1);
  static const uint32_t IsResumingGenerator = 1 << (TypeBits + 2);
  static const uint32_t NumActualArgsShift = TypeBits + 3;

  explicit FrameDescriptor(FrameType type) : raw_(uint32_t(type)) {}
  FrameDescriptor(FrameType type, uint32_t argc, bool hasInlined = false,
                  bool isResumingGenerator = false)
      : raw_(argc << NumActualArgsShift | uint32_t(type)) {
    if (hasInlined) {
      setHasInlinedICScript();
    }
    if (isResumingGenerator) {
      setIsResumingGenerator();
    }
    MOZ_ASSERT(numActualArgs() == argc, "argc must fit in descriptor");
  }

  FrameType type() const { return FrameType(raw_ & TypeMask); }
  void changeType(FrameType type) {
    raw_ &= ~TypeMask;
    raw_ |= uintptr_t(type);
  }

  uint32_t numActualArgs() const { return raw_ >> NumActualArgsShift; }

  bool hasCachedSavedFrame() const { return raw_ & HasCachedSavedFrame; }
  void setHasCachedSavedFrame() { raw_ |= HasCachedSavedFrame; }
  void clearHasCachedSavedFrame() { raw_ &= ~HasCachedSavedFrame; }

  bool hasInlinedICScript() const { return raw_ & HasInlinedICScript; }
  void setHasInlinedICScript() { raw_ |= HasInlinedICScript; }

  bool isResumingGenerator() const { return raw_ & IsResumingGenerator; }
  void setIsResumingGenerator() { raw_ |= IsResumingGenerator; }

  uint32_t value() const {
    MOZ_ASSERT(raw_ == uint32_t(raw_));
    return raw_;
  }

 private:
  uintptr_t raw_;
};

static inline uint32_t MakeFrameDescriptor(FrameType type) {
  FrameDescriptor descriptor(type);
  return descriptor.value();
}

// For JitFrameLayout, the descriptor also stores the number of arguments passed
// by the caller. Note that |type| is the type of the *older* frame and |argc|
// is the number of arguments passed to the *newer* frame.
static inline uint32_t MakeFrameDescriptorForJitCall(FrameType type,
                                                     uint32_t argc) {
  FrameDescriptor descriptor(type, argc);
  return descriptor.value();
}

struct BaselineBailoutInfo;

enum class ExceptionResumeKind : int32_t {
  // There is no exception handler in this activation.
  // Return from the entry frame.
  EntryFrame,

  // The exception was caught in baseline.
  // Restore state and jump to the catch block.
  Catch,

  // A finally block must be executed in baseline.
  // Stash the exception on the stack and jump to the finally block.
  Finally,

  // We are forcing an early return with a specific return value.
  // This is used by the debugger and when closing generators.
  // Immediately return from the current frame with the given value.
  ForcedReturnBaseline,
  ForcedReturnIon,

  // This frame is currently executing in Ion, but we must bail out
  // to baseline before handling the exception.
  // Jump to the bailout tail stub.
  Bailout,

  // Return to the wasm interpreter entry frame.
  WasmInterpEntry,

  // The exception was caught by a wasm catch handler.
  // Restore state and jump to it.
  WasmCatch
};

// Data needed to recover from an exception.
struct ResumeFromException {
  uint8_t* framePointer;
  uint8_t* stackPointer;
  uint8_t* target;
#ifdef JS_HW_SHADOW_STACK
  // These fields are used to communicate the shadow stack pointer between the
  // JIT and exception handling / frame unwinding logic in both directions. If
  // the shadow stack is disabled for this thread/process at runtime, then they
  // are both nullptr.
  //
  // Just before calling HandleException, the JIT reads the shadow stack pointer
  // into this field, which is where frame iteration begins. On returning back
  // to the JIT, it contains the new desired shadow stack pointer before jumping
  // to target. While the initial value is always on the main stack's shadow
  // stack, the final value may be on a continuation stack's shadow stack.
  uintptr_t* shadowStackPointer = nullptr;
  // When we're resuming in a catch handler on a continuation stack, we must
  // first switch to its shadow stack before unwinding to |shadowStackPointer|.
  // With CET, this switch requires a valid restore token. This field holds the
  // shadow stack pointer where that stack was last switched away from, below
  // which a restore token was left. If we're resuming on the main stack, it is
  // nullptr.
  uintptr_t* shadowStackSwitchPointer = nullptr;
#endif
  ExceptionResumeKind kind;
  wasm::Instance* instance;
#ifdef ENABLE_WASM_JSPI
  const wasm::StackTarget* stackTarget;
  const wasm::Handlers* baseHandlers;
#endif

  // Value to push when resuming into a |finally| block.
  // Also used by Wasm to send the exception object to the throw stub.
  JS::Value exception;

  // Exception stack to push when resuming into a |finally| block.
  JS::Value exceptionStack;

  BaselineBailoutInfo* bailoutInfo;

  static size_t offsetOfFramePointer() {
    return offsetof(ResumeFromException, framePointer);
  }
  static size_t offsetOfStackPointer() {
    return offsetof(ResumeFromException, stackPointer);
  }
  static size_t offsetOfTarget() {
    return offsetof(ResumeFromException, target);
  }
#ifdef JS_HW_SHADOW_STACK
  static size_t offsetOfShadowStackPointer() {
    return offsetof(ResumeFromException, shadowStackPointer);
  }
  static size_t offsetOfShadowStackSwitchPointer() {
    return offsetof(ResumeFromException, shadowStackSwitchPointer);
  }
#endif

  static size_t offsetOfKind() { return offsetof(ResumeFromException, kind); }
  static size_t offsetOfInstance() {
    return offsetof(ResumeFromException, instance);
  }
#ifdef ENABLE_WASM_JSPI
  static size_t offsetOfStackTarget() {
    return offsetof(ResumeFromException, stackTarget);
  }
  static size_t offsetOfBaseHandlers() {
    return offsetof(ResumeFromException, baseHandlers);
  }
#endif
  static size_t offsetOfException() {
    return offsetof(ResumeFromException, exception);
  }
  static size_t offsetOfExceptionStack() {
    return offsetof(ResumeFromException, exceptionStack);
  }
  static size_t offsetOfBailoutInfo() {
    return offsetof(ResumeFromException, bailoutInfo);
  }
};

#if defined(JS_CODEGEN_ARM64)
static_assert(sizeof(ResumeFromException) % 16 == 0,
              "ResumeFromException should be aligned");
#endif

void HandleException(ResumeFromException* rfe);

void EnsureUnwoundJitExitFrame(JitActivation* act, JitFrameLayout* frame);

void TraceJitFrames(JSTracer* trc, JitActivation* act);

#ifdef ENABLE_WASM_JSPI
void TraceWasmSuspendedContStacks(JSContext* cx, JSTracer* trc);
#endif

// Trace weak pointers in baseline stubs in activations for zones that are
// currently being swept.
void TraceWeakJitActivationsInSweepingZones(JSContext* cx, JSTracer* trc);

void UpdateJitActivationsForMinorGC(JSRuntime* rt);
void UpdateJitActivationsForCompactingGC(JSRuntime* rt);

// Returns the JSScript associated with the topmost JIT frame.
JSScript* GetTopJitJSScript(JSContext* cx);

#if defined(JS_CODEGEN_ARM64)
uint8_t* alignDoubleSpill(uint8_t* pointer);
#else
inline uint8_t* alignDoubleSpill(uint8_t* pointer) {
  // This is a no-op on most platforms.
  return pointer;
}
#endif

// Layout of the frame prefix. This assumes the stack architecture grows down.
// If this is ever not the case, we'll have to refactor.
class CommonFrameLayout {
  uint8_t* callerFramePtr_;
  uint8_t* returnAddress_;
  FrameDescriptor descriptor_;

 public:
  static constexpr size_t offsetOfDescriptor() {
    return offsetof(CommonFrameLayout, descriptor_);
  }
  FrameDescriptor descriptor() const { return descriptor_; }
  static constexpr size_t offsetOfReturnAddress() {
    return offsetof(CommonFrameLayout, returnAddress_);
  }
  FrameType prevType() const { return descriptor_.type(); }
  void changePrevType(FrameType type) { descriptor_.changeType(type); }
  bool hasCachedSavedFrame() const { return descriptor_.hasCachedSavedFrame(); }
  bool isResumingGenerator() const { return descriptor_.isResumingGenerator(); }
  void setHasCachedSavedFrame() { descriptor_.setHasCachedSavedFrame(); }
  void clearHasCachedSavedFrame() { descriptor_.clearHasCachedSavedFrame(); }
  uint8_t* returnAddress() const { return returnAddress_; }
  void setReturnAddress(uint8_t* addr) { returnAddress_ = addr; }

  uint8_t* callerFramePtr() const { return callerFramePtr_; }
  static constexpr size_t offsetOfCallerFramePtr() {
    return offsetof(CommonFrameLayout, callerFramePtr_);
  }
  static constexpr size_t bytesPoppedAfterCall() {
    // The return address and frame pointer are popped by the callee/call.
    return 2 * sizeof(void*);
  }
};

class JitFrameLayout : public CommonFrameLayout {
  CalleeToken calleeToken_;

 public:
  CalleeToken calleeToken() const { return calleeToken_; }
  void replaceCalleeToken(CalleeToken calleeToken) {
    calleeToken_ = calleeToken;
  }

  static constexpr size_t offsetOfCalleeToken() {
    return offsetof(JitFrameLayout, calleeToken_);
  }
  static constexpr size_t offsetOfThis() { return sizeof(JitFrameLayout); }
  static constexpr size_t offsetOfActualArgs() {
    return offsetOfThis() + sizeof(JS::Value);
  }
  static constexpr size_t offsetOfActualArg(size_t arg) {
    return offsetOfActualArgs() + arg * sizeof(JS::Value);
  }

  JS::Value& thisv() {
    MOZ_ASSERT(CalleeTokenIsFunction(calleeToken()));
    return thisAndActualArgs()[0];
  }
  JS::Value* thisAndActualArgs() {
    MOZ_ASSERT(CalleeTokenIsFunction(calleeToken()));
    return (JS::Value*)(this + 1);
  }
  JS::Value* actualArgs() { return thisAndActualArgs() + 1; }
  uintptr_t numActualArgs() const { return descriptor().numActualArgs(); }

  static constexpr size_t offsetOfModuleResumeArgs() {
    return sizeof(JitFrameLayout);
  }
  JS::Value* moduleResumeArgs() {
    MOZ_ASSERT(isResumingGenerator());
    MOZ_ASSERT(!CalleeTokenIsFunction(calleeToken()));
    return reinterpret_cast<JS::Value*>(this + 1);
  }

  JS::Value* resumeArgs();

  // Computes a reference to a stack or argument slot, where a slot is a
  // distance from the base frame pointer, as would be used for LStackSlot
  // or LArgument.
  uintptr_t* slotRef(SafepointSlotEntry where);

  static inline size_t Size() { return sizeof(JitFrameLayout); }
};

class BaselineInterpreterEntryFrameLayout : public JitFrameLayout {
 public:
  static inline size_t Size() {
    return sizeof(BaselineInterpreterEntryFrameLayout);
  }
};

class TrampolineNativeFrameLayout : public JitFrameLayout {
 public:
  static inline size_t Size() { return sizeof(TrampolineNativeFrameLayout); }

  template <typename T>
  T* getFrameData() {
    uint8_t* raw = reinterpret_cast<uint8_t*>(this) - sizeof(T);
    return reinterpret_cast<T*>(raw);
  }
};

class WasmToJSJitFrameLayout : public JitFrameLayout {
 public:
  static inline size_t Size() { return sizeof(WasmToJSJitFrameLayout); }
};

class IonICCallFrameLayout : public CommonFrameLayout {
 protected:
  // Pointer to root the stub's JitCode.
  JitCode* stubCode_;

 public:
  static constexpr size_t LocallyTracedValueOffset = sizeof(void*);

  JitCode** stubCode() { return &stubCode_; }
  static size_t Size() { return sizeof(IonICCallFrameLayout); }

  inline Value* locallyTracedValuePtr(size_t index) {
    uint8_t* fp = reinterpret_cast<uint8_t*>(this);
    return reinterpret_cast<Value*>(fp - LocallyTracedValueOffset -
                                    index * sizeof(Value));
  }
};

enum class ExitFrameType : uint8_t {
  CallNative = 0x0,
  ConstructNative = 0x1,
  IonDOMGetter = 0x2,
  IonDOMSetter = 0x3,
  IonDOMMethod = 0x4,
  IonOOLNative = 0x5,
  IonOOLProxy = 0x6,
  WasmGenericJitEntry = 0x7,
  DirectWasmJitCall = 0x8,
  UnwoundJit = 0x9,
  InterpreterStub = 0xA,
  LazyLink = 0xB,
  Bare = 0xC,

  // This must be the last value in this enum. See ExitFooterFrame::data_.
  VMFunction = 0xD
};

// GC related data used to keep alive data surrounding the Exit frame.
class ExitFooterFrame {
  // Stores either the ExitFrameType or, for a VMFunction call,
  // `ExitFrameType::VMFunction + VMFunctionId`.
  uintptr_t data_;

#ifdef DEBUG
  void assertValidVMFunctionId() const;
#else
  void assertValidVMFunctionId() const {}
#endif

 public:
  static constexpr size_t Size() { return sizeof(ExitFooterFrame); }
  void setUnwoundJitExitFrame() {
    data_ = uintptr_t(ExitFrameType::UnwoundJit);
  }
  ExitFrameType type() const {
    if (data_ >= uintptr_t(ExitFrameType::VMFunction)) {
      return ExitFrameType::VMFunction;
    }
    return ExitFrameType(data_);
  }
  VMFunctionId functionId() const {
    MOZ_ASSERT(type() == ExitFrameType::VMFunction);
    assertValidVMFunctionId();
    return static_cast<VMFunctionId>(data_ - size_t(ExitFrameType::VMFunction));
  }

  // This should only be called for function()->outParam == Type_Handle
  template <typename T>
  T* outParam() {
    uint8_t* address = reinterpret_cast<uint8_t*>(this);
    return reinterpret_cast<T*>(address - sizeof(T));
  }
};

class NativeExitFrameLayout;
class IonOOLNativeExitFrameLayout;
class IonOOLProxyExitFrameLayout;
class IonDOMExitFrameLayout;

// this is the frame layout when we are exiting ion code, and about to enter
// platform ABI code
class ExitFrameLayout : public CommonFrameLayout {
  inline uint8_t* top() { return reinterpret_cast<uint8_t*>(this + 1); }

 public:
  static constexpr size_t Size() { return sizeof(ExitFrameLayout); }
  static constexpr size_t SizeWithFooter() {
    return Size() + ExitFooterFrame::Size();
  }

  inline ExitFooterFrame* footer() {
    uint8_t* sp = reinterpret_cast<uint8_t*>(this);
    return reinterpret_cast<ExitFooterFrame*>(sp - ExitFooterFrame::Size());
  }

  // argBase targets the point which precedes the exit frame. Arguments of VM
  // each wrapper are pushed before the exit frame.  This correspond exactly
  // to the value of the argBase register of the generateVMWrapper function.
  inline uint8_t* argBase() {
    MOZ_ASSERT(isWrapperExit());
    return top();
  }

  inline bool isWrapperExit() {
    return footer()->type() == ExitFrameType::VMFunction;
  }
  inline bool isBareExit() { return footer()->type() == ExitFrameType::Bare; }
  inline bool isUnwoundJitExit() {
    return footer()->type() == ExitFrameType::UnwoundJit;
  }

  // See the various exit frame layouts below.
  template <typename T>
  inline bool is() {
    return footer()->type() == T::Type();
  }
  template <typename T>
  inline T* as() {
    MOZ_ASSERT(this->is<T>());
    return reinterpret_cast<T*>(footer());
  }
};

// Cannot inherit implementation since we need to extend the top of
// ExitFrameLayout.
class NativeExitFrameLayout {
 protected:  // only to silence a clang warning about unused private fields
  ExitFooterFrame footer_;
  ExitFrameLayout exit_;
  uintptr_t argc_;

  // The callee. Unlike the Value below, this isn't overwritten by the return
  // value. This ensures the callee's realm is kept alive on GC.
  // See bug 2078837.
  JSObject* savedCallee_;

  // We need to split the Value into 2 fields of 32 bits, otherwise the C++
  // compiler may add some padding between the fields.
  uint32_t loCalleeResult_;
  uint32_t hiCalleeResult_;

 public:
  static inline size_t Size() { return sizeof(NativeExitFrameLayout); }

  static size_t offsetOfResult() {
    return offsetof(NativeExitFrameLayout, loCalleeResult_);
  }
  inline JS::Value* vp() {
    return reinterpret_cast<JS::Value*>(&loCalleeResult_);
  }
  inline uintptr_t argc() const { return argc_; }
  inline JSObject** savedCallee() { return &savedCallee_; }
};

class CallNativeExitFrameLayout : public NativeExitFrameLayout {
 public:
  static ExitFrameType Type() { return ExitFrameType::CallNative; }
};

class ConstructNativeExitFrameLayout : public NativeExitFrameLayout {
 public:
  static ExitFrameType Type() { return ExitFrameType::ConstructNative; }
};

template <>
inline bool ExitFrameLayout::is<NativeExitFrameLayout>() {
  return is<CallNativeExitFrameLayout>() ||
         is<ConstructNativeExitFrameLayout>();
}

class IonOOLNativeExitFrameLayout {
 protected:  // only to silence a clang warning about unused private fields
  ExitFooterFrame footer_;
  ExitFrameLayout exit_;

  // pointer to root the stub's JitCode
  JitCode* stubCode_;

  uintptr_t argc_;

  // See NativeExitFrameLayout::savedCallee_.
  JSObject* savedCallee_;

  // We need to split the Value into 2 fields of 32 bits, otherwise the C++
  // compiler may add some padding between the fields.
  uint32_t loCalleeResult_;
  uint32_t hiCalleeResult_;

  // Split Value for |this| and args above.
  uint32_t loThis_;
  uint32_t hiThis_;

 public:
  static ExitFrameType Type() { return ExitFrameType::IonOOLNative; }

  static inline size_t Size(size_t argc) {
    // The frame accounts for the callee/result and |this|, so we only need
    // args.
    return sizeof(IonOOLNativeExitFrameLayout) + (argc * sizeof(JS::Value));
  }

  static size_t offsetOfResult() {
    return offsetof(IonOOLNativeExitFrameLayout, loCalleeResult_);
  }

  inline JitCode** stubCode() { return &stubCode_; }
  inline JS::Value* vp() {
    return reinterpret_cast<JS::Value*>(&loCalleeResult_);
  }
  inline JS::Value* thisp() { return reinterpret_cast<JS::Value*>(&loThis_); }
  inline uintptr_t argc() const { return argc_; }
  inline JSObject** savedCallee() { return &savedCallee_; }
};

// ProxyGetProperty(JSContext* cx, HandleObject proxy, HandleId id,
//                  MutableHandleValue vp)
// ProxyCallProperty(JSContext* cx, HandleObject proxy, HandleId id,
//                   MutableHandleValue vp)
// ProxySetProperty(JSContext* cx, HandleObject proxy, HandleId id,
//                  MutableHandleValue vp, bool strict)
class IonOOLProxyExitFrameLayout {
 protected:  // only to silence a clang warning about unused private fields
  ExitFooterFrame footer_;
  ExitFrameLayout exit_;

  // The proxy object.
  JSObject* proxy_;

  // id for HandleId
  jsid id_;

  // space for MutableHandleValue result
  // use two uint32_t so compiler doesn't align.
  uint32_t vp0_;
  uint32_t vp1_;

  // pointer to root the stub's JitCode
  JitCode* stubCode_;

 public:
  static ExitFrameType Type() { return ExitFrameType::IonOOLProxy; }

  static inline size_t Size() { return sizeof(IonOOLProxyExitFrameLayout); }

  static size_t offsetOfResult() {
    return offsetof(IonOOLProxyExitFrameLayout, vp0_);
  }

  inline JitCode** stubCode() { return &stubCode_; }
  inline JS::Value* vp() { return reinterpret_cast<JS::Value*>(&vp0_); }
  inline jsid* id() { return &id_; }
  inline JSObject** proxy() { return &proxy_; }
};

class IonDOMExitFrameLayout {
 protected:  // only to silence a clang warning about unused private fields
  ExitFooterFrame footer_;
  ExitFrameLayout exit_;
  JSObject* thisObj;

  // We need to split the Value into 2 fields of 32 bits, otherwise the C++
  // compiler may add some padding between the fields.
  uint32_t loCalleeResult_;
  uint32_t hiCalleeResult_;

 public:
  static ExitFrameType GetterType() { return ExitFrameType::IonDOMGetter; }
  static ExitFrameType SetterType() { return ExitFrameType::IonDOMSetter; }

  static inline size_t Size() { return sizeof(IonDOMExitFrameLayout); }

  static size_t offsetOfResult() {
    return offsetof(IonDOMExitFrameLayout, loCalleeResult_);
  }
  inline JS::Value* vp() {
    return reinterpret_cast<JS::Value*>(&loCalleeResult_);
  }
  inline JSObject** thisObjAddress() { return &thisObj; }
  inline bool isMethodFrame();
};

struct IonDOMMethodExitFrameLayoutTraits;

class IonDOMMethodExitFrameLayout {
 protected:  // only to silence a clang warning about unused private fields
  ExitFooterFrame footer_;
  ExitFrameLayout exit_;
  // This must be the last thing pushed, so as to stay common with
  // IonDOMExitFrameLayout.
  JSObject* thisObj_;
  JS::Value* argv_;
  uintptr_t argc_;

  // We need to split the Value into 2 fields of 32 bits, otherwise the C++
  // compiler may add some padding between the fields.
  uint32_t loCalleeResult_;
  uint32_t hiCalleeResult_;

  friend struct IonDOMMethodExitFrameLayoutTraits;

 public:
  static ExitFrameType Type() { return ExitFrameType::IonDOMMethod; }

  static inline size_t Size() { return sizeof(IonDOMMethodExitFrameLayout); }

  static size_t offsetOfResult() {
    return offsetof(IonDOMMethodExitFrameLayout, loCalleeResult_);
  }

  inline JS::Value* vp() {
    // The code in visitCallDOMNative depends on this static assert holding
    static_assert(
        offsetof(IonDOMMethodExitFrameLayout, loCalleeResult_) ==
        (offsetof(IonDOMMethodExitFrameLayout, argc_) + sizeof(uintptr_t)));
    return reinterpret_cast<JS::Value*>(&loCalleeResult_);
  }
  inline JSObject** thisObjAddress() { return &thisObj_; }
  inline uintptr_t argc() { return argc_; }
};

inline bool IonDOMExitFrameLayout::isMethodFrame() {
  return footer_.type() == IonDOMMethodExitFrameLayout::Type();
}

template <>
inline bool ExitFrameLayout::is<IonDOMExitFrameLayout>() {
  ExitFrameType type = footer()->type();
  return type == IonDOMExitFrameLayout::GetterType() ||
         type == IonDOMExitFrameLayout::SetterType() ||
         type == IonDOMMethodExitFrameLayout::Type();
}

template <>
inline IonDOMExitFrameLayout* ExitFrameLayout::as<IonDOMExitFrameLayout>() {
  MOZ_ASSERT(is<IonDOMExitFrameLayout>());
  return reinterpret_cast<IonDOMExitFrameLayout*>(footer());
}

struct IonDOMMethodExitFrameLayoutTraits {
  static const size_t offsetOfArgcFromArgv =
      offsetof(IonDOMMethodExitFrameLayout, argc_) -
      offsetof(IonDOMMethodExitFrameLayout, argv_);
};

// Cannot inherit implementation since we need to extend the top of
// ExitFrameLayout.
class CalledFromJitExitFrameLayout {
 protected:  // silence clang warning about unused private fields
  ExitFooterFrame footer_;
  JitFrameLayout exit_;

 public:
  static inline size_t Size() { return sizeof(CalledFromJitExitFrameLayout); }
  inline JitFrameLayout* jsFrame() { return &exit_; }
  static size_t offsetOfExitFrame() {
    return offsetof(CalledFromJitExitFrameLayout, exit_);
  }
};

class LazyLinkExitFrameLayout : public CalledFromJitExitFrameLayout {
 public:
  static ExitFrameType Type() { return ExitFrameType::LazyLink; }
};

class InterpreterStubExitFrameLayout : public CalledFromJitExitFrameLayout {
 public:
  static ExitFrameType Type() { return ExitFrameType::InterpreterStub; }
};

class WasmGenericJitEntryFrameLayout : CalledFromJitExitFrameLayout {
 public:
  static ExitFrameType Type() { return ExitFrameType::WasmGenericJitEntry; }
};

template <>
inline bool ExitFrameLayout::is<CalledFromJitExitFrameLayout>() {
  return is<InterpreterStubExitFrameLayout>() ||
         is<LazyLinkExitFrameLayout>() || is<WasmGenericJitEntryFrameLayout>();
}

template <>
inline CalledFromJitExitFrameLayout*
ExitFrameLayout::as<CalledFromJitExitFrameLayout>() {
  MOZ_ASSERT(is<CalledFromJitExitFrameLayout>());
  uint8_t* sp = reinterpret_cast<uint8_t*>(this);
  sp -= CalledFromJitExitFrameLayout::offsetOfExitFrame();
  return reinterpret_cast<CalledFromJitExitFrameLayout*>(sp);
}

class DirectWasmJitCallFrameLayout {
 protected:  // silence clang warning about unused private fields
  ExitFooterFrame footer_;
  ExitFrameLayout exit_;

 public:
  static ExitFrameType Type() { return ExitFrameType::DirectWasmJitCall; }
};

class ICStub;

class BaselineStubFrameLayout : public CommonFrameLayout {
  // Info on the stack
  //
  // +-------------------------------------------+
  // |BaselineStubFrameLayout                    |
  // +-------------------------------------------+
  // | - Descriptor                              | <= Marks end of
  // | - Return address                          |    FrameType::BaselineJS
  // | - CallerFramePtr                          | <= Frame pointer points here
  // +-------------------------------------------+
  // | - StubPtr                                 | <= Technically these fields
  // | - InlinedICScript or LocallyTracedValue   |    precede the FrameLayout in
  // |                      LocallyTracedValue...|    memory.
  // +-------------------------------------------+
  //
  // StubPtr is always present (but can be null; see generateDebugTrapHandler).
  // InlinedICScript is only present if the HasInlinedICScript flag is set in
  // the callee's frame descriptor (not shown in this diagram). Alternatively,
  // we support up to 255 locally traced values (with the count stored in
  // stub->jitCode()->localTracingSlots()).
 public:
  static constexpr size_t ICStubOffset = sizeof(void*);
  static constexpr int ICStubOffsetFromFP = -int(ICStubOffset);
  static constexpr int InlinedICScriptOffsetFromFP = 2 * -int(sizeof(void*));
  static constexpr size_t LocallyTracedValueOffset = 2 * sizeof(void*);

  static inline size_t Size() { return sizeof(BaselineStubFrameLayout); }

  ICStub* maybeStubPtr() {
    uint8_t* fp = reinterpret_cast<uint8_t*>(this);
    return *reinterpret_cast<ICStub**>(fp - ICStubOffset);
  }
  void setStubPtr(ICStub* stub) {
    MOZ_ASSERT(stub);
    uint8_t* fp = reinterpret_cast<uint8_t*>(this);
    *reinterpret_cast<ICStub**>(fp - ICStubOffset) = stub;
  }

  inline Value* locallyTracedValuePtr(size_t index) {
    uint8_t* fp = reinterpret_cast<uint8_t*>(this);
    return reinterpret_cast<Value*>(fp - LocallyTracedValueOffset -
                                    index * sizeof(Value));
  }
};

// An invalidation bailout stack is at the stack pointer for the callee frame.
class InvalidationBailoutStack {
  RegisterDump::FPUArray fpregs_;
  RegisterDump::GPRArray regs_;
  IonScript* ionScript_;
  uint8_t* osiPointReturnAddress_;

 public:
  uint8_t* sp() const {
    return (uint8_t*)this + sizeof(InvalidationBailoutStack);
  }
  JitFrameLayout* fp() const;
  MachineState machine() { return MachineState::FromBailout(regs_, fpregs_); }

  IonScript* ionScript() const { return ionScript_; }
  uint8_t* osiPointReturnAddress() const { return osiPointReturnAddress_; }
  static size_t offsetOfFpRegs() {
    return offsetof(InvalidationBailoutStack, fpregs_);
  }
  static size_t offsetOfRegs() {
    return offsetof(InvalidationBailoutStack, regs_);
  }

  void checkInvariants() const;
};

// Baseline requires one slot for this/argument type checks.
static const uint32_t MinJITStackSize = 1;

} /* namespace jit */
} /* namespace js */

#endif /* jit_JitFrames_h */
