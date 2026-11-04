/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/Bailouts.h"
#include "jit/BaselineFrame.h"
#include "jit/CalleeToken.h"
#include "jit/JitFrames.h"
#include "jit/JitRuntime.h"
#include "jit/PerfSpewer.h"
#include "jit/VMFunctions.h"
#include "jit/x64/SharedICRegisters-x64.h"
#include "vm/JitActivation.h"  // js::jit::JitActivation
#include "vm/JSContext.h"

#include "jit/MacroAssembler-inl.h"

using namespace js;
using namespace js::jit;

// This struct reflects the contents of the stack entry.
// Given a `CommonFrameLayout* frame`:
// - `frame->prevType()` should be `FrameType::CppToJSJit`.
// - Then EnterJITStackEntry starts at:
//     frame->callerFramePtr() + EnterJITStackEntry::offsetFromFP()
//     (the offset is negative, so this subtracts from the frame pointer)
struct EnterJITStackEntry {
  // Offset from frame pointer to EnterJITStackEntry*.
  static constexpr int32_t offsetFromFP() {
    return -int32_t(offsetof(EnterJITStackEntry, rbp));
  }

  void* result;

#if defined(_WIN64)
  struct XMM {
    using XMM128 = char[16];
    XMM128 xmm6;
    XMM128 xmm7;
    XMM128 xmm8;
    XMM128 xmm9;
    XMM128 xmm10;
    XMM128 xmm11;
    XMM128 xmm12;
    XMM128 xmm13;
    XMM128 xmm14;
    XMM128 xmm15;
  } xmm;

  // 16-byte aligment for xmm registers above.
  uint64_t xmmPadding;

  void* rsi;
  void* rdi;
#endif

  void* r15;
  void* r14;
  void* r13;
  void* r12;
  void* rbx;
  void* rbp;

  // Pushed by CALL.
  void* rip;
};

// All registers to save and restore. This includes the stack pointer, since we
// use the ability to reference register values on the stack by index.
static const LiveRegisterSet AllRegs =
    LiveRegisterSet(GeneralRegisterSet(Registers::AllMask),
                    FloatRegisterSet(FloatRegisters::AllMask));

// Generates a trampoline for calling Jit compiled code from a C++ function.
// The trampoline use the EnterJitCode signature, with the standard x64 fastcall
// calling convention.
void JitRuntime::generateEnterJIT(JSContext* cx, MacroAssembler& masm,
                                  EnterJitMode mode) {
  AutoCreatedBy acb(masm, "JitRuntime::generateEnterJIT");

  if (mode == EnterJitMode::GeneratorResume) {
    enterJITGeneratorResumeOffset_ = startTrampolineCode(masm);
  } else {
    enterJITOffset_ = startTrampolineCode(masm);
  }

  masm.assertStackAlignment(ABIStackAlignment,
                            -int32_t(sizeof(uintptr_t)) /* return address */);

  const Register reg_code = IntArgReg0;
  const Register reg_argc = IntArgReg1;
  const Register reg_argv = IntArgReg2;
  static_assert(OsrFrameReg == IntArgReg3);

#if defined(_WIN64)
  const Operand token = Operand(rbp, 16 + ShadowStackSpace);
  const Operand scopeChain = Operand(rbp, 24 + ShadowStackSpace);
  const Operand numStackValuesAddr = Operand(rbp, 32 + ShadowStackSpace);
  const Operand result = Operand(rbp, 40 + ShadowStackSpace);
#else
  const Register token = IntArgReg4;
  const Register scopeChain = IntArgReg5;
  const Operand numStackValuesAddr = Operand(rbp, 16 + ShadowStackSpace);
  const Operand result = Operand(rbp, 24 + ShadowStackSpace);
#endif

  // Note: the stack pushes below must match the fields in EnterJITStackEntry.

  // Save old stack frame pointer, set new stack frame pointer.
  masm.push(rbp);
  masm.mov(rsp, rbp);

  // Save non-volatile registers. These must be saved by the trampoline, rather
  // than by the JIT'd code, because they are scanned by the conservative
  // scanner.
  masm.pushRegs(rbx, r12, r13, r14, r15);
#if defined(_WIN64)
  masm.pushRegs(rdi, rsi);

  // 16-byte aligment for vmovdqa
  masm.subq(Imm32(sizeof(EnterJITStackEntry::XMM) + 8), rsp);

  masm.vmovdqa(xmm6, Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm6)));
  masm.vmovdqa(xmm7, Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm7)));
  masm.vmovdqa(xmm8, Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm8)));
  masm.vmovdqa(xmm9, Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm9)));
  masm.vmovdqa(xmm10, Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm10)));
  masm.vmovdqa(xmm11, Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm11)));
  masm.vmovdqa(xmm12, Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm12)));
  masm.vmovdqa(xmm13, Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm13)));
  masm.vmovdqa(xmm14, Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm14)));
  masm.vmovdqa(xmm15, Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm15)));
#endif

  // Push address of return value.
  masm.push(result);

  // End of pushes reflected in EnterJITStackEntry, i.e. EnterJITStackEntry
  // starts at this rsp.

  if (mode == EnterJitMode::GeneratorResume) {
    masm.movq(token, r12);
    generateEnterJitResumeShared(masm, reg_argv, r12, r13, r14);
  } else {
    masm.movq(token, r12);
    generateEnterJitShared(masm, reg_argc, reg_argv, r12, r13, r14, r15);

    // Push the descriptor.
    masm.movq(result, reg_argc);
    masm.unboxInt32(Operand(reg_argc, 0), reg_argc);
    masm.pushFrameDescriptorForJitCall(FrameType::CppToJSJit, reg_argc,
                                       reg_argc);
  }

  Label returnLabel;
  Label oomReturnLabel;
  if (mode != EnterJitMode::GeneratorResume) {
    // Handle Interpreter -> Baseline OSR.
    AllocatableGeneralRegisterSet regs(GeneralRegisterSet::All());
    MOZ_ASSERT(!regs.has(rbp));
    regs.take(OsrFrameReg);
    regs.take(reg_code);

    Register scratch = regs.takeAny();

    Label notOsr;
    masm.branchTestPtr(Assembler::Zero, OsrFrameReg, OsrFrameReg, &notOsr);

    // Push a return address onto the stack that actually jumps to the OSR
    // cleanup code. This balances the shadow stack for when the Baseline frame
    // eventually returns.
    Label osrEntry;
    masm.call(&osrEntry);
    masm.jump(&returnLabel);

    masm.bind(&osrEntry);

    Register numStackValues = regs.takeAny();
    masm.movq(numStackValuesAddr, numStackValues);

    // Frame prologue.
    masm.push(rbp);
    masm.mov(rsp, rbp);

    // Reserve frame.
    masm.subPtr(Imm32(BaselineFrame::Size()), rsp);

    Register framePtrScratch = regs.takeAny();
    masm.touchFrameValues(numStackValues, scratch, framePtrScratch);
    masm.mov(rsp, framePtrScratch);

    // Reserve space for locals and stack values.
    Register valuesSize = regs.takeAny();
    masm.mov(numStackValues, valuesSize);
    masm.shll(Imm32(3), valuesSize);
    masm.subPtr(valuesSize, rsp);

    // Enter exit frame.
    masm.push(FrameDescriptor(FrameType::BaselineJS));
    masm.push(Imm32(0));  // Fake return address.
    masm.push(FramePointer);
    // No GC things to mark, push a bare token.
    masm.loadJSContext(scratch);
    masm.enterFakeExitFrame(scratch, scratch, ExitFrameType::Bare);

    regs.add(valuesSize);

    masm.push(reg_code);

    using Fn = void (*)(BaselineFrame* frame, InterpreterFrame* interpFrame,
                        uint32_t numStackValues);
    masm.setupUnalignedABICall(scratch);
    masm.passABIArg(framePtrScratch);  // BaselineFrame
    masm.passABIArg(OsrFrameReg);      // InterpreterFrame
    masm.passABIArg(numStackValues);
    masm.callWithABI<Fn, jit::InitBaselineFrameForOsr>(
        ABIType::General, CheckUnsafeCallWithABI::DontCheckHasExitFrame);

    masm.pop(reg_code);

    MOZ_ASSERT(reg_code != ReturnReg);

    masm.addPtr(Imm32(ExitFrameLayout::SizeWithFooter()), rsp);

    // If OSR-ing, then emit instrumentation for setting lastProfilerFrame
    // if profiler instrumentation is enabled.
    {
      Label skipProfilingInstrumentation;
      AbsoluteAddress addressOfEnabled(
          cx->runtime()->geckoProfiler().addressOfEnabled());
      masm.branch32(Assembler::Equal, addressOfEnabled, Imm32(0),
                    &skipProfilingInstrumentation);
      masm.profilerEnterFrame(rbp, scratch);
      masm.bind(&skipProfilingInstrumentation);
    }

    masm.jump(reg_code);

    masm.bind(&notOsr);
    masm.movq(scopeChain, R1.scratchReg());
  }

  // The call will push the return address and frame pointer on the stack, thus
  // we check that the stack would be aligned once the call is complete.
  masm.assertStackAlignment(JitStackAlignment, 2 * sizeof(uintptr_t));

  // Call function.
  masm.callJitNoProfiler(reg_code);

  if (mode != EnterJitMode::GeneratorResume) {
    // Interpreter -> Baseline OSR will return here.
    masm.bind(&returnLabel);
    masm.bind(&oomReturnLabel);
  }

  // Discard arguments and padding. Set rsp to the address of the
  // EnterJITStackEntry on the stack.
  masm.lea(Operand(rbp, EnterJITStackEntry::offsetFromFP()), rsp);

  /*****************************************************************
  Place return value where it belongs, pop all saved registers
  *****************************************************************/
  masm.pop(r12);  // vp
  masm.storeValue(JSReturnOperand, Operand(r12, 0));

  // Restore non-volatile registers.
#if defined(_WIN64)
  masm.vmovdqa(Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm6)), xmm6);
  masm.vmovdqa(Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm7)), xmm7);
  masm.vmovdqa(Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm8)), xmm8);
  masm.vmovdqa(Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm9)), xmm9);
  masm.vmovdqa(Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm10)), xmm10);
  masm.vmovdqa(Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm11)), xmm11);
  masm.vmovdqa(Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm12)), xmm12);
  masm.vmovdqa(Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm13)), xmm13);
  masm.vmovdqa(Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm14)), xmm14);
  masm.vmovdqa(Operand(rsp, offsetof(EnterJITStackEntry::XMM, xmm15)), xmm15);

  masm.addq(Imm32(sizeof(EnterJITStackEntry::XMM) + 8), rsp);

  masm.popRegs(rsi, rdi);
#endif
  masm.popRegs(r15, r14, r13, r12, rbx);

  // Restore frame pointer and return.
  masm.pop(rbp);
  masm.ret();
}

// static
mozilla::Maybe<::JS::ProfilingFrameIterator::RegisterState>
JitRuntime::getCppEntryRegisters(JitFrameLayout* frameStackAddress) {
  if (frameStackAddress->prevType() != FrameType::CppToJSJit) {
    // This is not a CppToJSJit frame, there are no C++ registers here.
    return mozilla::Nothing{};
  }

  // Compute pointer to start of EnterJITStackEntry on the stack.
  uint8_t* fp = frameStackAddress->callerFramePtr();
  auto* enterJITStackEntry = reinterpret_cast<EnterJITStackEntry*>(
      fp + EnterJITStackEntry::offsetFromFP());

  // Extract native function call registers.
  ::JS::ProfilingFrameIterator::RegisterState registerState;
  registerState.fp = enterJITStackEntry->rbp;
  registerState.pc = enterJITStackEntry->rip;
  // sp should be inside the caller's frame, so set sp to the value of the stack
  // pointer before the call to the EnterJit trampoline.
  registerState.sp = &enterJITStackEntry->rip + 1;
  // No lr in this world.
  registerState.lr = nullptr;
  return mozilla::Some(registerState);
}

// Push AllRegs in a way that is compatible with RegisterDump, regardless of
// what PushRegsInMask might do to reduce the set size.
static void DumpAllRegs(MacroAssembler& masm) {
#ifdef ENABLE_JIT_SIMD
  masm.PushRegsInMask(AllRegs);
#else
  // When SIMD isn't supported, PushRegsInMask reduces the set of float
  // registers to be double-sized, while the RegisterDump expects each of
  // the float registers to have the maximal possible size
  // (Simd128DataSize). To work around this, we just spill the double
  // registers by hand here, using the register dump offset directly.
  for (GeneralRegisterBackwardIterator iter(AllRegs.gprs()); iter.more();
       ++iter) {
    masm.Push(*iter);
  }

  masm.reserveStack(sizeof(RegisterDump::FPUArray));
  for (FloatRegisterBackwardIterator iter(AllRegs.fpus()); iter.more();
       ++iter) {
    FloatRegister reg = *iter;
    Address spillAddress(StackPointer, reg.getRegisterDumpOffsetInBytes());
    masm.storeDouble(reg, spillAddress);
  }
#endif
}

void JitRuntime::generateInvalidator(MacroAssembler& masm, Label* bailoutTail) {
  AutoCreatedBy acb(masm, "JitRuntime::generateInvalidator");

  // See explanatory comment in x86's JitRuntime::generateInvalidator.

  invalidatorOffset_ = startTrampolineCode(masm);

  // Push registers such that we can access them from [base + code].
  DumpAllRegs(masm);

  masm.movq(rsp, rax);  // Argument to jit::InvalidationBailout.

  // Make space for InvalidationBailout's bailoutInfo outparam.
  masm.reserveStack(sizeof(void*));
  masm.movq(rsp, rbx);

  using Fn = bool (*)(InvalidationBailoutStack* sp, BaselineBailoutInfo** info);
  masm.setupUnalignedABICall(rdx);
  masm.passABIArg(rax);
  masm.passABIArg(rbx);
  masm.callWithABI<Fn, InvalidationBailout>(
      ABIType::General, CheckUnsafeCallWithABI::DontCheckOther);

  masm.pop(r9);  // Get the bailoutInfo outparam.

  // Pop the machine state and the dead frame.
  masm.moveToStackPtr(FramePointer);

#ifdef JS_HW_SHADOW_STACK
  // The above move removes the return address pushed by the call from the
  // OSI-point to the invalidation epilogue. Remove the corresponding shadow
  // stack entry.
  masm.addToShadowStackPtr(Imm32(sizeof(uintptr_t)), rdx);
#endif

  // Jump to shared bailout tail. The BailoutInfo pointer has to be in r9.
  masm.jmp(bailoutTail);
}

static void PushBailoutFrame(MacroAssembler& masm, Register spArg) {
  // Push registers such that we can access them from [base + code].
  DumpAllRegs(masm);

  // Get the stack pointer into a register, pre-alignment.
  masm.movq(rsp, spArg);
}

static void GenerateBailoutThunk(MacroAssembler& masm, Label* bailoutTail) {
  PushBailoutFrame(masm, r8);

  // Make space for Bailout's bailoutInfo outparam.
  masm.reserveStack(sizeof(void*));
  masm.movq(rsp, r9);

  // Call the bailout function.
  using Fn = bool (*)(BailoutStack* sp, BaselineBailoutInfo** info);
  masm.setupUnalignedABICall(rax);
  masm.passABIArg(r8);
  masm.passABIArg(r9);
  masm.callWithABI<Fn, Bailout>(ABIType::General,
                                CheckUnsafeCallWithABI::DontCheckOther);

  masm.pop(r9);  // Get the bailoutInfo outparam.

  // Remove both the bailout frame and the topmost Ion frame's stack.
  masm.moveToStackPtr(FramePointer);

  // Jump to shared bailout tail. The BailoutInfo pointer has to be in r9.
  masm.jmp(bailoutTail);
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
      "Wrapper register set must be a superset of Volatile register set");

  // The context is the first argument.
  Register cxreg = IntArgReg0;
  regs.take(cxreg);

  // Stack is:
  //    ... frame ...
  //  +12 [args]
  //  +8  descriptor
  //  +0  returnAddress
  //
  // Push the frame pointer to finish the exit frame, then link it up.
  masm.Push(FramePointer);
  masm.moveStackPtrTo(FramePointer);
  masm.loadJSContext(cxreg);
  masm.enterExitFrame(cxreg, regs.getAny(), id);

  // Reserve space for the outparameter.
  masm.reserveVMFunctionOutParamSpace(f);

  masm.setupUnalignedABICallDontSaveRestoreSP();
  masm.passABIArg(cxreg);

  size_t argDisp = ExitFrameLayout::Size();

  // Copy arguments.
  for (uint32_t explicitArg = 0; explicitArg < f.explicitArgs; explicitArg++) {
    switch (f.argProperties(explicitArg)) {
      case VMFunctionData::WordByValue:
        if (f.argPassedInFloatReg(explicitArg)) {
          masm.passABIArg(MoveOperand(FramePointer, argDisp), ABIType::Float64);
        } else {
          masm.passABIArg(MoveOperand(FramePointer, argDisp), ABIType::General);
        }
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
        MOZ_CRASH("NYI: x64 callVM should not be used with 128bits values.");
    }
  }

  // Copy the implicit outparam, if any.
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
      masm.branchTestPtr(Assembler::Zero, rax, rax, masm.failureLabel());
      break;
    case Type_Bool:
      masm.testb(rax, rax);
      masm.j(Assembler::Zero, masm.failureLabel());
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

  // Pop frame and restore frame pointer.
  masm.moveToStackPtr(FramePointer);
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

  static_assert(PreBarrierReg == rdx);
  Register temp1 = rax;
  Register temp2 = rbx;
  Register temp3 = rcx;
  masm.pushRegs(temp1, temp2, temp3);

  Label noBarrier;
  masm.emitPreBarrierFastPath(type, temp1, temp2, temp3, &noBarrier);

  // Call into C++ to mark this GC thing.
  masm.popRegs(temp3, temp2, temp1);

  LiveRegisterSet regs =
      LiveRegisterSet(GeneralRegisterSet(Registers::VolatileMask),
                      FloatRegisterSet(FloatRegisters::VolatileMask));
  masm.PushRegsInMask(regs);

  masm.mov(ImmPtr(cx->runtime()), rcx);

  masm.setupUnalignedABICall(rax);
  masm.passABIArg(rcx);
  masm.passABIArg(rdx);
  masm.callWithABI(JitPreWriteBarrier(type));

  masm.PopRegsInMask(regs);
  masm.ret();

  masm.bind(&noBarrier);
  masm.popRegs(temp3, temp2, temp1);
  masm.ret();

  return offset;
}

void JitRuntime::generateBailoutTailStub(MacroAssembler& masm,
                                         Label* bailoutTail) {
  AutoCreatedBy acb(masm, "JitRuntime::generateBailoutTailStub");

  masm.bind(bailoutTail);
  masm.generateBailoutTail(rdx, r9);
}
