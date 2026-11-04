/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/arm64/SharedICHelpers-arm64.h"
#include "jit/Bailouts.h"
#include "jit/BaselineFrame.h"
#include "jit/CalleeToken.h"
#include "jit/JitFrames.h"
#include "jit/JitRuntime.h"
#include "jit/PerfSpewer.h"
#include "jit/VMFunctions.h"
#include "util/Memory.h"       // js::AlignBytes
#include "vm/JitActivation.h"  // js::jit::JitActivation
#include "vm/JSContext.h"
#include "vm/Stack.h"  // js::ResumeFrameArgs

#include "jit/MacroAssembler-inl.h"

using namespace js;
using namespace js::jit;

/* This method generates a trampoline on ARM64 for a c++ function with
 * the following signature:
 *   bool blah(void* code, int argc, Value* argv,
 *             JSObject* scopeChain, Value* vp)
 *   ...using standard AArch64 calling convention
 */
void JitRuntime::generateEnterJIT(JSContext* cx, MacroAssembler& masm,
                                  EnterJitMode mode) {
  AutoCreatedBy acb(masm, "JitRuntime::generateEnterJIT");

  if (mode == EnterJitMode::GeneratorResume) {
    enterJITGeneratorResumeOffset_ = startTrampolineCode(masm);
  } else {
    enterJITOffset_ = startTrampolineCode(masm);
  }

  const Register reg_code = IntArgReg0;      // EnterJitData::jitcode.
  const Register reg_argc = IntArgReg1;      // EnterJitData::maxArgc.
  const Register reg_argv = IntArgReg2;      // EnterJitData::maxArgv.
  const Register reg_osrFrame = IntArgReg3;  // EnterJitData::osrFrame.
  const Register reg_callee = IntArgReg4;    // EnterJitData::calleeToken.
  const Register reg_scope = IntArgReg5;     // EnterJitData::scopeChain.
  const Register reg_osrNStack =
      IntArgReg6;                      // EnterJitData::osrNumStackValues.
  const Register reg_vp = IntArgReg7;  // Address of EnterJitData::result.

  static_assert(OsrFrameReg == IntArgReg3);

  // During the pushes below, use the normal stack pointer.
  masm.SetStackPointer64(sp);

  // Save return address and old frame pointer; set new frame pointer.
  masm.push(r30, r29);
  masm.moveStackPtrTo(r29);

  // Save callee-save integer registers.
  // Also save x7 (reg_vp) and x30 (lr), for use later.
  masm.push(r19, r20, r21, r22);
  masm.push(r23, r24, r25, r26);
  masm.push(r27, r28, r7, r30);

  // Save callee-save floating-point registers.
  // AArch64 ABI specifies that only the lower 64 bits must be saved.
  masm.push(d8, d9, d10, d11);
  masm.push(d12, d13, d14, d15);

#ifdef DEBUG
  // Emit stack canaries.
  masm.movePtr(ImmWord(0xdeadd00d), r23);
  masm.movePtr(ImmWord(0xdeadd11d), r24);
  masm.push(r23, r24);
#endif

  // At this point we are 16-byte aligned.
  masm.assertStackAlignment(JitStackAlignment);

  if (mode == EnterJitMode::GeneratorResume) {
    // reg_code is the callee's JIT code, reg_argv is the ResumeFrameArgs
    // array, reg_callee is the callee token.
    //
    // Build the resume frame the callee's prologue expects, matching
    // BaselineCodeGen::emit_Resume and generateEnterJitResumeShared on the
    // other platforms. As in the normal entry below, arm64 computes the frame
    // size and adjusts the stack pointer a single time (rather than with
    // incremental pushes) to keep sp aligned.
    Label notFunction, doneResume;
    masm.branchTest32(Assembler::NonZero, reg_callee,
                      Imm32(CalleeTokenScriptBit), &notFunction);
    {
      // Function frame: |this| and the formals (all |undefined|; dead on
      // resume) followed by the resume args.
      Register nformals = r19;
      masm.andPtr(Imm32(uint32_t(CalleeTokenMask)), reg_callee, nformals);
      masm.loadFunctionArgCount(nformals, nformals);

      // Frame slots: descriptor, calleeToken, |this|, the formals and the
      // resume args. Round up to an even number of slots to keep sp 16-byte
      // aligned.
      Register frame_size = r20;
      Register scratch = r21;
      Register scratch2 = r22;
      uint32_t extraSlots =
          3 + ResumeFrameArgs::NumSlots;  // descriptor+token+this
      masm.add32(Imm32(extraSlots + 1), nformals, frame_size);
      masm.and32(Imm32(~1), frame_size);
      masm.touchFrameValues(frame_size, scratch, scratch2);

      // Allocate the stack frame.
      masm.lshift32(Imm32(3), frame_size);
      masm.subFromStackPtr(frame_size);

      // Store |this| and the formals as |undefined|, then the resume args,
      // starting just above the descriptor and calleeToken slots.
      ARMRegister dest(r23, 64);
      ARMRegister arg(scratch, 64);
      ARMRegister count(scratch2, 64);
      masm.Add(dest, sp, Operand(2 * sizeof(uintptr_t)));
      masm.Mov(arg, int64_t(UndefinedValue().asRawBits()));
      masm.Add(count, ARMRegister(nformals, 64), Operand(1));  // + |this|

      Label undefLoop;
      masm.bind(&undefLoop);
      masm.Str(arg, MemOperand(dest, Operand(8), vixl::PostIndex));
      masm.Subs(count, count, Operand(1));
      masm.B(&undefLoop, vixl::Condition::NonZero);

      for (uint32_t i = 0; i < ResumeFrameArgs::NumSlots; i++) {
        masm.Ldr(arg,
                 MemOperand(ARMRegister(reg_argv, 64), i * sizeof(JS::Value)));
        masm.Str(arg, MemOperand(dest, Operand(8), vixl::PostIndex));
      }
      masm.jump(&doneResume);
    }
    masm.bind(&notFunction);
    {
      // Module frame (top-level await): the resume args are the only Values
      // pushed, there's no |this| or formals.
      //
      // Frame slots: descriptor, calleeToken and the pushed Values, rounded up
      // to an even number of slots to keep sp 16-byte aligned.
      constexpr uint32_t moduleSlots =
          AlignBytes(2 + ResumeFrameArgs::NumSlots, 2u);
      masm.subFromStackPtr(Imm32(moduleSlots * sizeof(uintptr_t)));

      ARMRegister dest(r23, 64);
      ARMRegister arg(r21, 64);
      masm.Add(dest, sp, Operand(2 * sizeof(uintptr_t)));
      for (uint32_t i = 0; i < ResumeFrameArgs::NumSlots; i++) {
        masm.Ldr(arg,
                 MemOperand(ARMRegister(reg_argv, 64), i * sizeof(JS::Value)));
        masm.Str(arg, MemOperand(dest, Operand(8), vixl::PostIndex));
      }
    }
    masm.bind(&doneResume);

    // Store the resume frame descriptor and the callee token.
    Register descriptor = r19;
    masm.move32(
        Imm32(int32_t(FrameDescriptor(FrameType::CppToJSJit, /* argc = */ 0,
                                      /* hasInlinedICScript = */ false,
                                      /* isResumingGenerator = */ true)
                          .value())),
        descriptor);
    masm.Str(ARMRegister(descriptor, 64), MemOperand(sp, 0));
    masm.Str(ARMRegister(reg_callee, 64), MemOperand(sp, sizeof(uintptr_t)));
  } else {
    // ARM64 expects the stack pointer to be 16-byte aligned whenever it
    // is used as a base register. This makes it awkward to build a
    // stack frame using incremental pushes. Therefore, unlike other
    // platforms, arm64 does not use generateEnterJitShared. Instead,
    // we compute the total size of the frame, adjust the stack pointer
    // a single time, and then initialize the contents of the frame.
    //
    // At this point:
    // - reg_argc contains the number of args passed (not including this)
    // - reg_argv points to the beginning of a contiguous array of arguments
    //   values, *not* including `this`. `this` is at argvReg[-1].
    // - reg_callee contains the callee token

    // Compute the number of args to push.
    Label notFunction;
    Register actual_args = r19;
    masm.branchTest32(Assembler::NonZero, reg_callee,
                      Imm32(CalleeTokenScriptBit), &notFunction);
    masm.andPtr(Imm32(uint32_t(CalleeTokenMask)), reg_callee, actual_args);
    masm.loadFunctionArgCount(actual_args, actual_args);
    masm.max32(actual_args, reg_argc, actual_args);

    // In addition to args, our stack frame needs space for the descriptor,
    // the calleeToken, `this`, and `newTarget`. We allocate `newTarget`
    // unconditionally; at worst it costs us a tiny bit of stack space.
    // Add these to frame_size and round up to an even number.
    Register frame_size = r20;
    Register scratch = r21;
    Register scratch2 = r22;
    uint32_t extraSlots = 4;
    masm.add32(Imm32(extraSlots + 1), actual_args, frame_size);
    masm.and32(Imm32(~1), frame_size);

    // Touch frame incrementally (a requirement for Windows).
    masm.touchFrameValues(frame_size, scratch, scratch2);

    // Allocate the stack frame.
    masm.lshift32(Imm32(3), frame_size);
    masm.subFromStackPtr(frame_size);

    // Copy `this` through `argN` from reg_argv to the stack.
    // WARNING: destructively modifies reg_argv.
    // This section uses ARM instructions directly to get easy access
    // to post-increment loads/stores.
    ARMRegister dest(r23, 64);
    ARMRegister arg(scratch, 64);
    ARMRegister tmp_argc(scratch2, 64);
    ARMRegister argc(reg_argc, 64);
    ARMRegister argv(reg_argv, 64);
    masm.Add(dest, sp, Operand(2 * sizeof(uintptr_t)));
    masm.Add(tmp_argc, argc, Operand(1));
    masm.Sub(argv, argv, Operand(sizeof(Value)));  // Point at `this`.

    Label argLoop;
    masm.bind(&argLoop);
    // Load an argument from argv, then increment argv by 8.
    masm.Ldr(arg, MemOperand(argv, Operand(8), vixl::PostIndex));
    // Store the argument to dest, then increment dest by 8.
    masm.Str(arg, MemOperand(dest, Operand(8), vixl::PostIndex));
    // Decrement tmp_argc and set the condition codes for the new value.
    masm.Subs(tmp_argc, tmp_argc, Operand(1));
    // Branch if arguments remain.
    masm.B(&argLoop, vixl::Condition::NonZero);

    // Fill any remaining arguments with `undefined`.
    // First compute the number of missing arguments.
    Label noUndef;
    const ARMRegister missing_args(scratch2, 64);
    masm.Subs(missing_args, ARMRegister(actual_args, 64), argc);
    masm.B(&noUndef, vixl::Condition::Zero);

    Label undefLoop;
    masm.Mov(arg, int64_t(UndefinedValue().asRawBits()));
    masm.bind(&undefLoop);
    // Store `undefined` to dest, then increment dest by 8.
    masm.Str(arg, MemOperand(dest, Operand(8), vixl::PostIndex));
    // Decrement missing_args and set the condition codes for the new value.
    masm.Subs(missing_args, missing_args, Operand(1));
    // Branch if missing arguments remain.
    masm.B(&undefLoop, vixl::Condition::NonZero);
    masm.bind(&noUndef);

    // Store newTarget if necessary
    Label doneArgs;
    masm.branchTest32(Assembler::Zero, reg_callee,
                      Imm32(CalleeToken_FunctionConstructing), &doneArgs);
    masm.Ldr(arg, MemOperand(argv));
    masm.Str(arg, MemOperand(dest));
    masm.jump(&doneArgs);
    masm.bind(&notFunction);

    // Non-functions have no arguments.
    // Allocate space for the callee token and the descriptor.
    const int32_t nonFunctionFrameSize = 2 * sizeof(uintptr_t);
    static_assert(nonFunctionFrameSize % JitStackAlignment == 0);
    masm.subFromStackPtr(Imm32(nonFunctionFrameSize));
    masm.bind(&doneArgs);

    // Store descriptor and callee token.
    masm.unboxInt32(Address(reg_vp, 0), scratch);
    masm.makeFrameDescriptorForJitCall(FrameType::CppToJSJit, scratch, scratch);
    masm.Str(ARMRegister(scratch, 64), MemOperand(sp, 0));
    masm.Str(ARMRegister(reg_callee, 64), MemOperand(sp, sizeof(uintptr_t)));
  }

  // We start using the PSP here.
  // TODO: convert the code below to use sp instead.
  masm.Mov(PseudoStackPointer64, sp);
  masm.SetStackPointer64(PseudoStackPointer64);

  masm.checkStackAlignment();

  Label osrReturnPoint;
  if (mode != EnterJitMode::GeneratorResume) {
    // Check for Interpreter -> Baseline OSR.

    AllocatableGeneralRegisterSet regs(GeneralRegisterSet::All());
    MOZ_ASSERT(!regs.has(FramePointer));
    regs.take(OsrFrameReg);
    regs.take(reg_code);
    regs.take(reg_osrNStack);
    MOZ_ASSERT(!regs.has(ReturnReg), "ReturnReg matches reg_code");

    Label notOsr;
    masm.branchTestPtr(Assembler::Zero, OsrFrameReg, OsrFrameReg, &notOsr);

    Register scratch = regs.takeAny();

    // Frame prologue.
    masm.Adr(ARMRegister(scratch, 64), &osrReturnPoint);
    masm.push(scratch, FramePointer);
    masm.moveStackPtrTo(FramePointer);

    // Reserve frame.
    masm.subFromStackPtr(Imm32(BaselineFrame::Size()));

    Register framePtrScratch = regs.takeAny();
    masm.touchFrameValues(reg_osrNStack, scratch, framePtrScratch);
    masm.moveStackPtrTo(framePtrScratch);

    // Reserve space for locals and stack values.
    // scratch = num_stack_values * sizeof(Value).
    masm.Lsl(ARMRegister(scratch, 32), ARMRegister(reg_osrNStack, 32), 3);
    masm.subFromStackPtr(scratch);

    // Enter exit frame.
    masm.push(FrameDescriptor(FrameType::BaselineJS));
    masm.push(xzr);  // Push xzr for a fake return address.
    masm.push(FramePointer);
    // No GC things to mark: push a bare token.
    masm.loadJSContext(scratch);
    masm.enterFakeExitFrame(scratch, scratch, ExitFrameType::Bare);

    masm.push(reg_code);

    // Initialize the frame, including filling in the slots.
    using Fn = void (*)(BaselineFrame* frame, InterpreterFrame* interpFrame,
                        uint32_t numStackValues);
    masm.setupUnalignedABICall(r19);
    masm.passABIArg(framePtrScratch);  // BaselineFrame.
    masm.passABIArg(reg_osrFrame);     // InterpreterFrame.
    masm.passABIArg(reg_osrNStack);
    masm.callWithABI<Fn, jit::InitBaselineFrameForOsr>(
        ABIType::General, CheckUnsafeCallWithABI::DontCheckHasExitFrame);

    masm.pop(scratch);
    MOZ_ASSERT(scratch != ReturnReg);

    masm.addToStackPtr(Imm32(ExitFrameLayout::SizeWithFooter()));

    // If OSR-ing, then emit instrumentation for setting lastProfilerFrame
    // if profiler instrumentation is enabled.
    {
      Label skipProfilingInstrumentation;
      AbsoluteAddress addressOfEnabled(
          cx->runtime()->geckoProfiler().addressOfEnabled());
      masm.branch32(Assembler::Equal, addressOfEnabled, Imm32(0),
                    &skipProfilingInstrumentation);
      masm.profilerEnterFrame(FramePointer, regs.getAny());
      masm.bind(&skipProfilingInstrumentation);
    }

    masm.jump(scratch);

    masm.bind(&notOsr);
    masm.movePtr(reg_scope, R1_);
  }

  // The callee will push the return address and frame pointer on the stack,
  // thus we check that the stack would be aligned once the call is complete.
  masm.assertStackAlignment(JitStackAlignment, 2 * sizeof(uintptr_t));

  // Call function.
  // Since AArch64 doesn't have the pc register available, the callee must push
  // lr.
  masm.callJitNoProfiler(reg_code);

  if (mode != EnterJitMode::GeneratorResume) {
    // Interpreter -> Baseline OSR will return here.
    masm.bind(&osrReturnPoint);
  }

  // Discard arguments and padding. Set sp to the address of the saved
  // registers. In debug builds we have to include the two stack canaries
  // checked below.
#ifdef DEBUG
  static constexpr size_t SavedRegSize = 22 * sizeof(void*);
#else
  static constexpr size_t SavedRegSize = 20 * sizeof(void*);
#endif
  masm.computeEffectiveAddress(Address(FramePointer, -int32_t(SavedRegSize)),
                               masm.getStackPointer());

  masm.syncStackPtr();
  masm.SetStackPointer64(sp);

#ifdef DEBUG
  // Check that canaries placed on function entry are still present.
  masm.pop(r24, r23);
  Label x23OK, x24OK;

  masm.branchPtr(Assembler::Equal, r23, ImmWord(0xdeadd00d), &x23OK);
  masm.breakpoint();
  masm.bind(&x23OK);

  masm.branchPtr(Assembler::Equal, r24, ImmWord(0xdeadd11d), &x24OK);
  masm.breakpoint();
  masm.bind(&x24OK);
#endif

  // Restore callee-save floating-point registers.
  masm.pop(d15, d14, d13, d12);
  masm.pop(d11, d10, d9, d8);

  // Restore callee-save integer registers.
  // Also restore x7 (reg_vp) and x30 (lr).
  masm.pop(r30, r7, r28, r27);
  masm.pop(r26, r25, r24, r23);
  masm.pop(r22, r21, r20, r19);

  // Store return value (in JSReturnReg = x2 to just-popped reg_vp).
  masm.storeValue(JSReturnOperand, Address(reg_vp, 0));

  // Restore old frame pointer.
  masm.pop(r29, r30);

  // Return using the value popped into x30.
  masm.abiret();

  // Reset stack pointer.
  masm.SetStackPointer64(PseudoStackPointer64);
}

// static
mozilla::Maybe<::JS::ProfilingFrameIterator::RegisterState>
JitRuntime::getCppEntryRegisters(JitFrameLayout* frameStackAddress) {
  // Not supported, or not implemented yet.
  // TODO: Implement along with the corresponding stack-walker changes, in
  // coordination with the Gecko Profiler, see bug 1635987 and follow-ups.
  return mozilla::Nothing{};
}

static void PushRegisterDump(MacroAssembler& masm) {
  const LiveRegisterSet First20GeneralRegisters = LiveRegisterSet(
      GeneralRegisterSet(Registers::AllMask &
                         ~(1 << 31 | 1 << 30 | 1 << 29 | 1 << 28 | 1 << 27 |
                           1 << 26 | 1 << 25 | 1 << 24 | 1 << 23 | 1 << 22 |
                           1 << 21 | 1 << 20)),
      FloatRegisterSet(FloatRegisters::NoneMask));

  const LiveRegisterSet AllFloatRegisters =
      LiveRegisterSet(GeneralRegisterSet(Registers::NoneMask),
                      FloatRegisterSet(FloatRegisters::AllMask));

  // Push all general-purpose registers.
  //
  // The ARM64 ABI does not treat SP as a normal register that can
  // be pushed. So pushing happens in two phases.
  //
  // Registers are pushed in reverse order of code.
  //
  // See block comment in MacroAssembler.h for further required invariants.

  // First, push the last twelve registers, passing zero for sp.
  // Zero is pushed for x20 and x31: the pseudo-SP and SP, respectively.
  masm.asVIXL().Push(xzr, x30, x29, x28);
  masm.asVIXL().Push(x27, x26, x25, x24);
  masm.asVIXL().Push(x23, x22, x21, xzr);

  // Second, push the first 20 registers that serve no special purpose.
  masm.PushRegsInMask(First20GeneralRegisters);

  // Finally, push all floating-point registers, completing the RegisterDump.
  masm.PushRegsInMask(AllFloatRegisters);
}

void JitRuntime::generateInvalidator(MacroAssembler& masm, Label* bailoutTail) {
  AutoCreatedBy acb(masm, "JitRuntime::generateInvalidator");

  invalidatorOffset_ = startTrampolineCode(masm);

  // The InvalidationBailoutStack saved in r0 must be:
  // - osiPointReturnAddress_
  // - ionScript_  (pushed by CodeGeneratorARM64::generateInvalidateEpilogue())
  // - regs_  (pushed here)
  // - fpregs_  (pushed here) [=r0]
  PushRegisterDump(masm);
  masm.moveStackPtrTo(r0);

  // Reserve space for InvalidationBailout's bailoutInfo outparam.
  masm.Sub(x1, masm.GetStackPointer64(), Operand(sizeof(void*)));
  masm.moveToStackPtr(r1);

  using Fn = bool (*)(InvalidationBailoutStack* sp, BaselineBailoutInfo** info);
  masm.setupUnalignedABICall(r10);
  masm.passABIArg(r0);
  masm.passABIArg(r1);

  masm.callWithABI<Fn, InvalidationBailout>(
      ABIType::General, CheckUnsafeCallWithABI::DontCheckOther);

  masm.pop(r2);  // Get the bailoutInfo outparam.

  // Pop the machine state and the dead frame.
  masm.moveToStackPtr(FramePointer);

  // Jump to shared bailout tail. The BailoutInfo pointer has to be in r2.
  masm.jump(bailoutTail);
}

static void PushBailoutFrame(MacroAssembler& masm, Register spArg) {
  // This assumes no SIMD registers, as JS does not support SIMD.

  // The stack saved in spArg must be (higher entries have higher memory
  // addresses):
  // - snapshotOffset_
  // - frameSize_
  // - regs_
  // - fpregs_ (spArg + 0)
  PushRegisterDump(masm);
  masm.moveStackPtrTo(spArg);
}

static void GenerateBailoutThunk(MacroAssembler& masm, Label* bailoutTail) {
  PushBailoutFrame(masm, r0);

  // SP % 8 == 4
  // STEP 1c: Call the bailout function, giving a pointer to the
  //          structure we just blitted onto the stack.
  // Make space for the BaselineBailoutInfo* outparam.
  masm.reserveStack(sizeof(void*));
  masm.moveStackPtrTo(r1);

  using Fn = bool (*)(BailoutStack* sp, BaselineBailoutInfo** info);
  masm.setupUnalignedABICall(r2);
  masm.passABIArg(r0);
  masm.passABIArg(r1);
  masm.callWithABI<Fn, Bailout>(ABIType::General,
                                CheckUnsafeCallWithABI::DontCheckOther);

  // Get the bailoutInfo outparam.
  masm.pop(r2);

  // Remove both the bailout frame and the topmost Ion frame's stack.
  masm.moveToStackPtr(FramePointer);

  // Jump to shared bailout tail. The BailoutInfo pointer has to be in r2.
  masm.jump(bailoutTail);
}

void JitRuntime::generateBailoutHandler(MacroAssembler& masm,
                                        Label* bailoutTail) {
  AutoCreatedBy acb(masm, "JitRuntime::generateBailoutHandler");

  bailoutHandlerOffset_ = startTrampolineCode(masm);

  GenerateBailoutThunk(masm, bailoutTail);
}

bool JitRuntime::generateVMWrapper(JSContext* cx, MacroAssembler& masm,
                                   VMFunctionId id, const VMFunctionData& f,
                                   DynFn nativeFun, uint32_t* wrapperOffset) {
  AutoCreatedBy acb(masm, "JitRuntime::generateVMWrapper");

  *wrapperOffset = startTrampolineCode(masm);

  // Avoid conflicts with argument registers while discarding the result after
  // the function call.
  AllocatableGeneralRegisterSet regs(Register::Codes::WrapperMask);

  static_assert(
      (Register::Codes::VolatileMask & ~Register::Codes::WrapperMask) == 0,
      "Wrapper register set must be a superset of the Volatile register set.");

  // The first argument is the JSContext.
  Register reg_cx = IntArgReg0;
  regs.take(reg_cx);
  Register temp = regs.getAny();

  // On entry, the stack is:
  //   ... frame ...
  //  [args]
  //  descriptor
  //
  // Before we pass arguments (potentially pushing some of them on the stack),
  // we want:
  //  ... frame ...
  //  [args]
  //  descriptor           \
  //  return address       | <- exit frame
  //  saved frame pointer  /
  //  VM id                  <- exit frame footer
  //  [space for out-param, if necessary]]
  //  [alignment padding, if necessary]
  //
  // To minimize PSP overhead, we compute the final stack size and update the
  // stack pointer all in one go. Then we use the PSP to "push" the required
  // values into the pre-allocated stack space.
  size_t stackAdjustment = 0;

  // The descriptor was already pushed.
  stackAdjustment += ExitFrameLayout::SizeWithFooter() - sizeof(uintptr_t);
  stackAdjustment += f.sizeOfOutParamStackSlot();

  masm.SetStackPointer64(sp);

  // First, update the actual stack pointer to its final aligned value.
  masm.Sub(ARMRegister(temp, 64), masm.GetStackPointer64(),
           Operand(stackAdjustment));
  masm.And(sp, ARMRegister(temp, 64), ~(uint64_t(JitStackAlignment) - 1));

  // On link-register platforms, it is the responsibility of the VM *callee* to
  // push the return address, while the caller must ensure that the address
  // is stored in lr on entry. This allows the VM wrapper to work with both
  // direct calls and tail calls.
  masm.str(ARMRegister(lr, 64),
           MemOperand(PseudoStackPointer64, -8, vixl::PreIndex));

  // Push the frame pointer using the PSP.
  masm.str(ARMRegister(FramePointer, 64),
           MemOperand(PseudoStackPointer64, -8, vixl::PreIndex));

  // Because we've been moving the PSP as we fill in the frame, we can set the
  // frame pointer for this frame directly from the PSP.
  masm.movePtr(PseudoStackPointer, FramePointer);

  masm.loadJSContext(reg_cx);

  // Finish the exit frame. See MacroAssembler::enterExitFrame.

  // linkExitFrame
  masm.loadPtr(Address(reg_cx, JSContext::offsetOfActivation()), temp);
  masm.storePtr(FramePointer,
                Address(temp, JitActivation::offsetOfPackedExitFP()));

  // Push `ExitFrameType::VMFunction + VMFunctionId`
  uint32_t type = uint32_t(ExitFrameType::VMFunction) + uint32_t(id);
  masm.move32(Imm32(type), temp);
  masm.str(ARMRegister(temp, 64),
           MemOperand(PseudoStackPointer64, -8, vixl::PreIndex));

  // If the out parameter is a handle, initialize it to empty.
  // See MacroAssembler::reserveVMFunctionOutParamSpace and PushEmptyRooted.
  if (f.outParam == Type_Handle) {
    switch (f.outParamRootType) {
      case VMFunctionData::RootNone:
        MOZ_CRASH("Handle must have root type");
      case VMFunctionData::RootObject:
      case VMFunctionData::RootString:
      case VMFunctionData::RootCell:
      case VMFunctionData::RootBigInt:
        masm.str(xzr, MemOperand(PseudoStackPointer64, -8, vixl::PreIndex));
        break;
      case VMFunctionData::RootValue:
        masm.movePtr(ImmWord(UndefinedValue().asRawBits()), temp);
        masm.str(ARMRegister(temp, 64),
                 MemOperand(PseudoStackPointer64, -8, vixl::PreIndex));
        break;
      case VMFunctionData::RootId:
        masm.movePtr(ImmWord(JS::PropertyKey::Void().asRawBits()), temp);
        masm.str(ARMRegister(temp, 64),
                 MemOperand(PseudoStackPointer64, -8, vixl::PreIndex));
    }
  }

  // Now that we've filled in the stack frame, synchronize the PSP with the
  // real stack pointer and return to PSP-mode while we pass arguments.
  masm.moveStackPtrTo(PseudoStackPointer);
  masm.SetStackPointer64(PseudoStackPointer64);

  MOZ_ASSERT(masm.framePushed() == 0);
  masm.setupAlignedABICall();
  masm.passABIArg(reg_cx);

  size_t argDisp = ExitFrameLayout::Size();

  // Copy arguments.
  for (uint32_t explicitArg = 0; explicitArg < f.explicitArgs; explicitArg++) {
    switch (f.argProperties(explicitArg)) {
      case VMFunctionData::WordByValue:
        masm.passABIArg(
            MoveOperand(FramePointer, argDisp),
            (f.argPassedInFloatReg(explicitArg) ? ABIType::Float64
                                                : ABIType::General));
        argDisp += sizeof(void*);
        break;

      case VMFunctionData::WordByRef:
        masm.passABIArg(MoveOperand(FramePointer, argDisp,
                                    MoveOperand::Kind::EffectiveAddress),
                        ABIType::General);
        argDisp += sizeof(void*);
        break;

      case VMFunctionData::DoubleByValue:
      case VMFunctionData::DoubleByRef:
        MOZ_CRASH("NYI: AArch64 callVM should not be used with 128bit values.");
    }
  }

  // Copy the semi-implicit outparam, if any.
  // It is not a C++-abi outparam, which would get passed in the
  // outparam register, but a real parameter to the function, which
  // was stack-allocated above.
  const int32_t outParamOffset =
      -int32_t(ExitFooterFrame::Size()) - f.sizeOfOutParamStackSlot();
  if (f.outParam != Type_Void) {
    masm.passABIArg(MoveOperand(FramePointer, outParamOffset,
                                MoveOperand::Kind::EffectiveAddress),
                    ABIType::General);
  }

  masm.callWithABI(nativeFun, ABIType::General,
                   CheckUnsafeCallWithABI::DontCheckHasExitFrame);

  // Test for failure.
  switch (f.failType()) {
    case Type_Cell:
      masm.branchTestPtr(Assembler::Zero, r0, r0, masm.failureLabel());
      break;
    case Type_Bool:
      masm.branchIfFalseBool(r0, masm.failureLabel());
      break;
    case Type_Void:
      break;
    default:
      MOZ_CRASH("unknown failure kind");
  }

  // Load the outparam.
  masm.loadVMFunctionOutParam(f, Address(FramePointer, outParamOffset));

  // Until C++ code is instrumented against Spectre, prevent speculative
  // execution from returning any private data.
  if (f.returnsData() && JitOptions.spectreJitToCxxCalls) {
    masm.speculationBarrier();
  }

  // Pop frame and restore frame pointer. We call Mov here directly instead
  // of `moveToStackPtr` to avoid a syncStackPtr. The stack pointer will be
  // synchronized as part of retn, after adjusting the PSP.
  masm.Mov(masm.GetStackPointer64(), ARMRegister(FramePointer, 64));
  masm.pop(FramePointer);

  // Return. Subtract sizeof(void*) for the frame pointer.
  masm.retn(Imm32(sizeof(ExitFrameLayout) - sizeof(void*) +
                  f.explicitStackSlots() * sizeof(void*) +
                  f.extraValuesToPop * sizeof(Value)));

  return true;
}

uint32_t JitRuntime::generatePreBarrier(JSContext* cx, MacroAssembler& masm,
                                        MIRType type) {
  AutoCreatedBy acb(masm, "JitRuntime::generatePreBarrier");

  uint32_t offset = startTrampolineCode(masm);

  static_assert(PreBarrierReg == r1);
  Register temp1 = r2;
  Register temp2 = r3;
  Register temp3 = r4;
  masm.pushRegs(temp1, temp2, temp3);

  Label noBarrier;
  masm.emitPreBarrierFastPath(type, temp1, temp2, temp3, &noBarrier);

  // Call into C++ to mark this GC thing.
  masm.popRegs(temp3, temp2, temp1);

  LiveRegisterSet regs =
      LiveRegisterSet(GeneralRegisterSet(Registers::VolatileMask),
                      FloatRegisterSet(FloatRegisters::VolatileMask));

  // Also preserve the return address.
  regs.add(lr);

  masm.PushRegsInMask(regs);

  masm.movePtr(ImmPtr(cx->runtime()), r3);

  masm.setupUnalignedABICall(r0);
  masm.passABIArg(r3);
  masm.passABIArg(PreBarrierReg);
  masm.callWithABI(JitPreWriteBarrier(type));

  // Pop the volatile regs and restore LR.
  masm.PopRegsInMask(regs);
  masm.abiret();

  masm.bind(&noBarrier);
  masm.popRegs(temp3, temp2, temp1);
  masm.abiret();

  return offset;
}

void JitRuntime::generateBailoutTailStub(MacroAssembler& masm,
                                         Label* bailoutTail) {
  AutoCreatedBy acb(masm, "JitRuntime::generateBailoutTailStub");

  masm.bind(bailoutTail);
  masm.generateBailoutTail(r1, r2);
}
