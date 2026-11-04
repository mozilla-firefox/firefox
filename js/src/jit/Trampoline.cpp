/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <initializer_list>

#include "jit/BaselineDebugModeOSR.h"
#include "jit/JitFrames.h"
#include "jit/JitRuntime.h"
#include "jit/MacroAssembler.h"
#include "jit/SharedICRegisters.h"
#include "vm/JitActivation.h"
#include "vm/JSContext.h"
#include "vm/Stack.h"  // js::ResumeFrameArgs

#include "jit/MacroAssembler-inl.h"

using namespace js;
using namespace js::jit;

void JitRuntime::generateExceptionTailStub(MacroAssembler& masm,
                                           Label* profilerExitTail,
                                           Label* bailoutTail) {
  AutoCreatedBy acb(masm, "JitRuntime::generateExceptionTailStub");

  exceptionTailOffset_ = startTrampolineCode(masm);

  uint32_t returnValueCheckOffset = 0;
  masm.bind(masm.failureLabel());
  masm.handleFailureWithHandlerTail(profilerExitTail, bailoutTail,
                                    &returnValueCheckOffset);

  exceptionTailReturnValueCheckOffset_ = returnValueCheckOffset;
}

void JitRuntime::generateProfilerExitFrameTailStub(MacroAssembler& masm,
                                                   Label* profilerExitTail) {
  AutoCreatedBy acb(masm, "JitRuntime::generateProfilerExitFrameTailStub");

  profilerExitFrameTailOffset_ = startTrampolineCode(masm);
  masm.bind(profilerExitTail);

  static constexpr size_t CallerFPOffset =
      CommonFrameLayout::offsetOfCallerFramePtr();

  // Assert the caller frame's type is one of the types we expect.
  auto emitAssertPrevFrameType = [&masm](
                                     Register framePtr, Register scratch,
                                     std::initializer_list<FrameType> types) {
#ifdef DEBUG
    masm.loadPtr(Address(framePtr, CommonFrameLayout::offsetOfDescriptor()),
                 scratch);
    masm.and32(Imm32(FrameDescriptor::TypeMask), scratch);

    Label checkOk;
    for (FrameType type : types) {
      masm.branch32(Assembler::Equal, scratch, Imm32(type), &checkOk);
    }
    masm.assumeUnreachable("Unexpected previous frame");
    masm.bind(&checkOk);
#else
    (void)masm;
#endif
  };

  AllocatableGeneralRegisterSet regs(GeneralRegisterSet::All());
  regs.take(JSReturnOperand);
  Register scratch = regs.takeAny();

  // The code generated below expects that the current frame pointer points
  // to an Ion or Baseline frame, at the state it would be immediately before
  // the frame epilogue and ret(). Thus, after this stub's business is done, it
  // restores the frame pointer and stack pointer, then executes a ret() and
  // returns directly to the caller frame, on behalf of the callee script that
  // jumped to this code.
  //
  // Thus the expected state is:
  //
  //    [JitFrameLayout] <-- FramePointer
  //    [frame contents] <-- StackPointer
  //
  // The generated jitcode is responsible for overwriting the
  // jitActivation->lastProfilingFrame field with a pointer to the previous
  // Ion or Baseline jit-frame that was pushed before this one. It is also
  // responsible for overwriting jitActivation->lastProfilingCallSite with
  // the return address into that frame.
  //
  // So this jitcode is responsible for "walking up" the jit stack, finding
  // the previous Ion or Baseline JS frame, and storing its address and the
  // return address into the appropriate fields on the current jitActivation.
  //
  // There are a fixed number of different path types that can lead to the
  // current frame, which is either a Baseline or Ion frame:
  //
  // <Baseline-Or-Ion>
  // ^
  // |
  // ^--- Ion (or Baseline JSOp::Resume)
  // |
  // ^--- Baseline Stub <---- Baseline
  // |
  // ^--- IonICCall <---- Ion
  // |
  // ^--- Entry Frame (BaselineInterpreter) (unwrapped)
  // |
  // ^--- Trampoline Native (unwrapped)
  // |
  // ^--- Entry Frame (CppToJSJit or WasmToJSJit)
  //
  // NOTE: Keep this in sync with JSJitProfilingFrameIterator::moveToNextFrame!

  Register actReg = regs.takeAny();
  masm.loadJSContext(actReg);
  masm.loadPtr(Address(actReg, offsetof(JSContext, profilingActivation_)),
               actReg);

  Address lastProfilingFrame(actReg,
                             JitActivation::offsetOfLastProfilingFrame());
  Address lastProfilingCallSite(actReg,
                                JitActivation::offsetOfLastProfilingCallSite());

#ifdef DEBUG
  // Ensure that frame we are exiting is current lastProfilingFrame
  {
    masm.loadPtr(lastProfilingFrame, scratch);
    Label checkOk;
    masm.branchPtr(Assembler::Equal, scratch, ImmWord(0), &checkOk);
    masm.branchPtr(Assembler::Equal, FramePointer, scratch, &checkOk);
    masm.assumeUnreachable(
        "Mismatch between stored lastProfilingFrame and current frame "
        "pointer.");
    masm.bind(&checkOk);
  }
#endif

  // Move FP into a scratch register and use that scratch register below, to
  // allow unwrapping frames without clobbering FP.
  Register fpScratch = regs.takeAny();
  masm.mov(FramePointer, fpScratch);

  Label again;
  masm.bind(&again);

  // Load the frame descriptor into |scratch|, figure out what to do depending
  // on its type.
  masm.loadPtr(Address(fpScratch, JitFrameLayout::offsetOfDescriptor()),
               scratch);
  masm.and32(Imm32(FrameDescriptor::TypeMask), scratch);

  // Handling of each case is dependent on FrameDescriptor.type
  Label handle_BaselineOrIonJS;
  Label handle_BaselineStub;
  Label handle_TrampolineNative;
  Label handle_BaselineInterpreterEntry;
  Label handle_IonICCall;
  Label handle_Entry;

  // We check for IonJS and BaselineStub first because these are the most common
  // types. Calls from Baseline are usually from a BaselineStub frame.
  masm.branch32(Assembler::Equal, scratch, Imm32(FrameType::IonJS),
                &handle_BaselineOrIonJS);
  masm.branch32(Assembler::Equal, scratch, Imm32(FrameType::BaselineStub),
                &handle_BaselineStub);
  if (JitOptions.emitInterpreterEntryTrampoline) {
    masm.branch32(Assembler::Equal, scratch,
                  Imm32(FrameType::BaselineInterpreterEntry),
                  &handle_BaselineInterpreterEntry);
  }
  masm.branch32(Assembler::Equal, scratch, Imm32(FrameType::CppToJSJit),
                &handle_Entry);
  masm.branch32(Assembler::Equal, scratch, Imm32(FrameType::BaselineJS),
                &handle_BaselineOrIonJS);
  masm.branch32(Assembler::Equal, scratch, Imm32(FrameType::IonICCall),
                &handle_IonICCall);
  masm.branch32(Assembler::Equal, scratch, Imm32(FrameType::TrampolineNative),
                &handle_TrampolineNative);
  masm.branch32(Assembler::Equal, scratch, Imm32(FrameType::WasmToJSJit),
                &handle_Entry);

  masm.assumeUnreachable(
      "Invalid caller frame type when returning from a JIT frame.");

  masm.bind(&handle_BaselineOrIonJS);
  {
    // Returning directly to a Baseline or Ion frame.

    // lastProfilingCallSite := ReturnAddress
    masm.loadPtr(Address(fpScratch, JitFrameLayout::offsetOfReturnAddress()),
                 scratch);
    masm.storePtr(scratch, lastProfilingCallSite);

    // lastProfilingFrame := CallerFrame
    masm.loadPtr(Address(fpScratch, CallerFPOffset), scratch);
    masm.storePtr(scratch, lastProfilingFrame);

    masm.moveToStackPtr(FramePointer);
    masm.pop(FramePointer);
    masm.ret();
  }

  // Shared implementation for BaselineStub and IonICCall frames.
  auto emitHandleStubFrame = [&](FrameType expectedPrevType) {
    // Load pointer to stub frame and assert type of its caller frame.
    masm.loadPtr(Address(fpScratch, CallerFPOffset), fpScratch);
    emitAssertPrevFrameType(fpScratch, scratch, {expectedPrevType});

    // lastProfilingCallSite := StubFrame.ReturnAddress
    masm.loadPtr(Address(fpScratch, CommonFrameLayout::offsetOfReturnAddress()),
                 scratch);
    masm.storePtr(scratch, lastProfilingCallSite);

    // lastProfilingFrame := StubFrame.CallerFrame
    masm.loadPtr(Address(fpScratch, CallerFPOffset), scratch);
    masm.storePtr(scratch, lastProfilingFrame);

    masm.moveToStackPtr(FramePointer);
    masm.pop(FramePointer);
    masm.ret();
  };

  masm.bind(&handle_BaselineStub);
  {
    // BaselineJS => BaselineStub frame.
    emitHandleStubFrame(FrameType::BaselineJS);
  }

  masm.bind(&handle_IonICCall);
  {
    // IonJS => IonICCall frame.
    emitHandleStubFrame(FrameType::IonJS);
  }

  masm.bind(&handle_TrampolineNative);
  {
    // There can be multiple previous frame types so just "unwrap" this frame
    // and try again.
    masm.loadPtr(Address(fpScratch, CallerFPOffset), fpScratch);
    emitAssertPrevFrameType(
        fpScratch, scratch,
        {FrameType::IonJS, FrameType::BaselineStub, FrameType::IonICCall,
         FrameType::CppToJSJit, FrameType::WasmToJSJit});
    masm.jump(&again);
  }

  if (JitOptions.emitInterpreterEntryTrampoline) {
    masm.bind(&handle_BaselineInterpreterEntry);
    {
      // Unwrap the baseline interpreter entry frame and try again.
      masm.loadPtr(Address(fpScratch, CallerFPOffset), fpScratch);
      emitAssertPrevFrameType(
          fpScratch, scratch,
          {FrameType::IonJS, FrameType::BaselineJS, FrameType::BaselineStub,
           FrameType::CppToJSJit, FrameType::WasmToJSJit, FrameType::IonICCall,
           FrameType::TrampolineNative});
      masm.jump(&again);
    }
  }

  masm.bind(&handle_Entry);
  {
    // FrameType::CppToJSJit / FrameType::WasmToJSJit
    //
    // A fast-path wasm->jit transition frame is an entry frame from the point
    // of view of the JIT.
    // Store null into both fields.
    masm.movePtr(ImmPtr(nullptr), scratch);
    masm.storePtr(scratch, lastProfilingCallSite);
    masm.storePtr(scratch, lastProfilingFrame);

    masm.moveToStackPtr(FramePointer);
    masm.pop(FramePointer);
    masm.ret();
  }
}

#ifndef JS_CODEGEN_ARM64
// This is a shared path used by generateEnterJit on all architectures
// except arm64, which has its own implementation that avoids using the
// pseudo stack pointer.
void JitRuntime::generateEnterJitShared(MacroAssembler& masm, Register argcReg,
                                        Register argvReg,
                                        Register calleeTokenReg,
                                        Register scratch, Register scratch2,
                                        Register scratch3) {
  // Preconditions:
  // - argcReg contains the number of actual args passed (not including this).
  // - argvReg points to the beginning of an array of argcReg argument values,
  //   *not including* `this`. `this` is at argvReg[-1]. If newTarget exists,
  //   it follows the last argument at argvReg[argcReg].
  // - calleeTokenReg contains the calleeToken, still tagged.
  // - no alignment is assumed.
  //
  // Postconditions:
  // - stack padding has been inserted if necessary for alignment.
  // - if necessary, newTarget has been copied into place.
  // - if calleeToken is a function and argc < fun->nargs(), `undefined` values
  //   have been pushed to make up the difference.
  // - the arguments have been pushed to the stack.
  // - the callee token has been pushed
  // - the descriptor has *not* been pushed, because x86 doesn't have enough
  //   registers available to pass in actualArgs.
  static_assert(
      sizeof(JitFrameLayout) % JitStackAlignment == 0,
      "No need to consider the JitFrameLayout for aligning the stack");

  Label notFunction, doneArgs;
  masm.branchTest32(Assembler::NonZero, calleeTokenReg,
                    Imm32(CalleeTokenScriptBit), &notFunction);

  // Compute the number of arguments that will be pushed (excluding this and
  // newTarget).
  Register actualArgs = scratch;
  masm.andPtr(Imm32(uint32_t(CalleeTokenMask)), calleeTokenReg, actualArgs);
  masm.loadFunctionArgCount(actualArgs, actualArgs);
  masm.max32(actualArgs, argcReg, actualArgs);

  // Align the stack.
  if (JitStackValueAlignment == 1) {
    masm.andToStackPtr(Imm32(~(JitStackAlignment - 1)));
  } else {
    MOZ_ASSERT(JitStackValueAlignment == 2);
    // We will push actualArgs arguments, `this`, and maybe `newTarget`.
    // Each value we push is 8 bytes. If we push an even number of values,
    // we want to align to 16 bytes. If we push an odd number, we want to be
    // offset from that by 8. For alignment purposes, we only care about the
    // parity of the total, so we can use the low bit of
    //   1 (this) + actualArgs + calleeTokenReg (for the constructing bit)
    static_assert(CalleeToken_FunctionConstructing == 1);
    masm.computeEffectiveAddress(
        BaseIndex(calleeTokenReg, actualArgs, Scale::TimesOne, 1), scratch2);
    masm.and32(Imm32(1), scratch2);
    masm.lshift32(Imm32(3), scratch2);
    masm.moveStackPtrTo(scratch3);
    masm.subPtr(scratch2, scratch3);
    masm.andPtr(Imm32(JitStackAlignment - 1), scratch3);
    masm.subFromStackPtr(scratch3);
  }

  // We set up argCursor to point 8 bytes *after* the next argument to push.
  // This allows us to compare against argvReg in the arguments loop while
  // still pushing `this`.
  Register argCursor = scratch3;
  masm.computeEffectiveAddress(BaseValueIndex(argvReg, argcReg), argCursor);

  // Push newTarget if necessary.
  Label notConstructing;
  masm.branchTest32(Assembler::Zero, calleeTokenReg,
                    Imm32(CalleeToken_FunctionConstructing), &notConstructing);
  masm.pushValue(Address(argCursor, 0));
  masm.bind(&notConstructing);

  // Push undefined arguments if necessary, decrementing actualArgs
  // until it matches argc.
  Label undefLoop, doneUndef;
  masm.bind(&undefLoop);
  masm.branch32(Assembler::Equal, actualArgs, argcReg, &doneUndef);
  masm.pushValue(UndefinedValue());
  masm.sub32(Imm32(1), actualArgs);
  masm.jump(&undefLoop);
  masm.bind(&doneUndef);

  // Loop pushing arguments.
  Label argLoop;
  masm.bind(&argLoop);
  masm.pushValue(Address(argCursor, -int32_t(sizeof(Value))));
  masm.subPtr(Imm32(sizeof(Value)), argCursor);
  masm.branchPtr(Assembler::AboveOrEqual, argCursor, argvReg, &argLoop);

  masm.jump(&doneArgs);

  // If we're invoking a script, we will push no arguments. We can therefore
  // simply align to the JitStackAlignment.
  masm.bind(&notFunction);
  masm.andToStackPtr(Imm32(~(JitStackAlignment - 1)));
  masm.bind(&doneArgs);

  // Push the callee token.
  masm.push(calleeTokenReg);
}

void JitRuntime::generateEnterJitResumeShared(MacroAssembler& masm,
                                              Register argvReg,
                                              Register calleeTokenReg,
                                              Register scratch,
                                              Register scratch2) {
  // Preconditions:
  // - argvReg points at the ResumeFrameArgs Values.
  // - calleeTokenReg holds the callee token, still tagged (a function for a
  //   generator/async callee, or a script for a top-level-await module).
  //
  // Postconditions:
  // - the resume frame has been pushed: for a function frame the formals
  //   (|undefined|; dead on resume) and |this|; for a module frame nothing.
  //   The resume args are pushed outside that argument region (after the
  //   formals for functions, above the frame layout for modules).
  // - the callee token and the resume frame descriptor have been pushed.
  //
  // This matches the frame built by BaselineCodeGen::emit_Resume.

  auto pushResumeArgs = [&]() {
    // Push from the last slot to the first, to preserve their order.
    for (uint32_t slot = ResumeFrameArgs::NumSlots; slot > 0; slot--) {
      masm.pushValue(Address(argvReg, ResumeFrameArgs::offsetOfSlot(slot - 1)));
    }
  };

  // The stack alignment below requires a Value-aligned stack. On 64-bit this is
  // guaranteed because the C++ entry only pushes words, but on 32-bit it can
  // leave sp word-aligned, so round down there.
  if constexpr (sizeof(uintptr_t) < sizeof(Value)) {
    masm.andToStackPtr(Imm32(~(int32_t(sizeof(Value)) - 1)));
  }

  Label module, frameDone;
  masm.branchTestPtr(Assembler::NonZero, calleeTokenReg,
                     Imm32(CalleeTokenScriptBit), &module);
  {
    Register fun = scratch;
    masm.movePtr(calleeTokenReg, fun);
    masm.andPtr(Imm32(uint32_t(CalleeTokenMask)), fun);

    Register nformals = scratch2;
    masm.loadFunctionArgCount(fun, nformals);
    masm.alignJitStackBasedOnNArgs(nformals, /* countIncludesThis = */ false,
                                   /* extraArgs = */ ResumeFrameArgs::NumSlots);

    pushResumeArgs();

    // Push |undefined| for the formals and for |this|. Because |this| is also
    // pushed, we count down to -1 so we always push at least one Value.
    Label formalLoop;
    masm.bind(&formalLoop);
    masm.pushValue(UndefinedValue());
    masm.branchSub32(Assembler::NotSigned, Imm32(1), nformals, &formalLoop);

    masm.jump(&frameDone);
  }
  masm.bind(&module);
  {
    // Module frame (top-level await): the resume args are the only Values
    // pushed, there's no |this| or arguments.
    masm.alignJitStackBasedOnNumValues(ResumeFrameArgs::NumSlots);
    pushResumeArgs();
  }
  masm.bind(&frameDone);

  masm.push(calleeTokenReg);
  masm.push(FrameDescriptor(FrameType::CppToJSJit, /* argc = */ 0,
                            /* hasInlinedICScript = */ false,
                            /* isResumingGenerator = */ true));
}
#endif  // !JS_CODEGEN_ARM64

void JitRuntime::generateBaselineDebugModeOSRHandler(MacroAssembler& masm) {
  AutoCreatedBy acb(masm, "JitRuntime::generateBaselineDebugModeOSRHandler");

  baselineDebugModeOSRHandlerOffset_ = startTrampolineCode(masm);

  LiveRegisterSet regs(RegisterSet::Volatile());
  masm.PushRegsInMask(regs);

  AllocatableGeneralRegisterSet scratchRegs(GeneralRegisterSet::Volatile());
  Register returnAddress = scratchRegs.takeAny();
  Register baselineFrame = scratchRegs.takeAny();
  Register scratch = scratchRegs.takeAny();

#ifdef JS_HW_SHADOW_STACK
  // The patched call from the DebugModeOSR return site to the per-script
  // epilogue pushes a return address entry through which we never return.
  masm.addToShadowStackPtr(Imm32(sizeof(uintptr_t)), scratch);
#endif

  const size_t registerDumpSize =
      MacroAssembler::PushRegsInMaskSizeInBytes(regs);
  MOZ_ASSERT(registerDumpSize <= INT32_MAX);
  // The per-script epilogue leaves the patched call's return address here.
  Address resumeAddrSlot(masm.getStackPointer(), int32_t(registerDumpSize));

  masm.loadPtr(resumeAddrSlot, returnAddress);
  // The DebugModeOSRReturnSite address is before the patched call.
  masm.subPtr(Imm32(Assembler::PatchWrite_NearCallSize()), returnAddress);
  masm.loadBaselineFramePtr(FramePointer, baselineFrame);

  using Fn = uint8_t* (*)(BaselineFrame*, const uint8_t*);
  masm.setupUnalignedABICall(scratch);
  masm.passABIArg(baselineFrame);
  masm.passABIArg(returnAddress);
  masm.callWithABI<Fn, BaselineDebugModeOSRResumeAddr>();

  masm.storePtr(ReturnReg, resumeAddrSlot);
  masm.PopRegsInMask(regs);

#ifdef JS_CODEGEN_X86
  // ABINonArgReturnVolatileReg holds the Baseline return value's tag on x86.
  constexpr Register resumeAddress = ICStubReg;
#else
  constexpr Register resumeAddress = ABINonArgReturnVolatileReg;
#endif
  static_assert(resumeAddress != ReturnReg &&
                resumeAddress != JSReturnReg_Type &&
                resumeAddress != JSReturnReg_Data);
  masm.pop(resumeAddress);
  masm.jump(resumeAddress);
}
