/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "vm/Interpreter.h"

#include "gc/PublicIterators.h"
#include "gc/Zone.h"
#include "jit/JitRuntime.h"
#include "jit/JitZone.h"
#include "jit/Linker.h"

#include "gc/Marking-inl.h"
#include "gc/WeakMap-inl.h"
#include "jit/MacroAssembler-inl.h"

using namespace js;
using namespace js::jit;

EntryTrampolineMap* JitZone::getOrCreateInterpreterEntryMap(JS::Zone* zone) {
  if (!interpreterEntryMap) {
    interpreterEntryMap = js::MakeUnique<EntryTrampolineMap>(zone);
  }

  return interpreterEntryMap.get();
}

void JitRuntime::generateBaselineInterpreterEntryTrampoline(
    MacroAssembler& masm) {
  AutoCreatedBy acb(masm,
                    "JitRuntime::generateBaselineInterpreterEntryTrampoline");

#ifdef JS_USE_LINK_REGISTER
  masm.pushRegs(LinkRegister, FramePointer);
#else
  masm.push(FramePointer);
#endif
  masm.moveStackPtrTo(FramePointer);

  AllocatableGeneralRegisterSet regs(GeneralRegisterSet::All());
  Register nargs = regs.takeAny();
  Register callee = regs.takeAny();
  Register scratch = regs.takeAny();

  // Load callee token and keep it in a register as it will be used often
  Address calleeTokenAddr(
      FramePointer, BaselineInterpreterEntryFrameLayout::offsetOfCalleeToken());
  masm.loadPtr(calleeTokenAddr, callee);

  Address descriptorAddr(
      FramePointer, BaselineInterpreterEntryFrameLayout::offsetOfDescriptor());

  // Load argc into nargs.
  masm.loadNumActualArgs(FramePointer, nargs);

  // Compute in |nargs| the number of Values the caller pushed above ThisV. The
  // loop below then copies ThisV and these Values.
  Label resuming, argsCounted, argsPushed;
  masm.branchTest32(Assembler::NonZero, descriptorAddr,
                    Imm32(FrameDescriptor::IsResumingGenerator), &resuming);

  Label notFunction;
  {
    // Check if calleetoken is script or function
    masm.branchTestPtr(Assembler::NonZero, callee, Imm32(CalleeTokenScriptBit),
                       &notFunction);

    // CalleeToken is a function, load |nformals| into scratch
    masm.andPtr(Imm32(uint32_t(CalleeTokenMask)), callee, scratch);
    masm.loadFunctionArgCount(scratch, scratch);

    // Take max(nformals, argc).
    Label noUnderflow;
    masm.branch32(Assembler::AboveOrEqual, nargs, scratch, &noUnderflow);
    {
      masm.movePtr(scratch, nargs);
    }
    masm.bind(&noUnderflow);

    // Add 1 to nargs if constructing.
    static_assert(
        CalleeToken_FunctionConstructing == 1,
        "Ensure that we can use the constructing bit to count the value");
    masm.movePtr(callee, scratch);
    masm.and32(Imm32(uint32_t(CalleeToken_FunctionConstructing)), scratch);
    masm.addPtr(scratch, nargs);
  }
  masm.bind(&notFunction);
  masm.jump(&argsCounted);

  // The caller is resuming a suspended generator or async function/module, so
  // it also pushed the resume args (see ResumeFrameArgs) and numActualArgs is
  // 0.
  Label moduleResume;
  masm.bind(&resuming);
  {
#ifdef DEBUG
    Label argcOk;
    masm.branchTest32(Assembler::Zero, nargs, nargs, &argcOk);
    masm.assumeUnreachable("Resume frames have numActualArgs == 0");
    masm.bind(&argcOk);
#endif

    masm.branchTestPtr(Assembler::NonZero, callee, Imm32(CalleeTokenScriptBit),
                       &moduleResume);

    // The callee is a generator or async function: the resume args were pushed
    // above the formals.
    masm.andPtr(Imm32(uint32_t(CalleeTokenMask)), callee, scratch);
    masm.loadFunctionArgCount(scratch, nargs);
    masm.addPtr(Imm32(ResumeFrameArgs::NumSlots), nargs);
  }
  masm.bind(&argsCounted);

  // Align stack
  masm.alignJitStackBasedOnNArgs(nargs, /*countIncludesThis = */ false);

  // Point argPtr to the topmost argument.
  static_assert(sizeof(Value) == 8,
                "Using TimesEight for scale of sizeof(Value).");
  BaseIndex topPtrAddr(FramePointer, nargs, TimesEight,
                       sizeof(BaselineInterpreterEntryFrameLayout));
  Register argPtr = nargs;
  masm.computeEffectiveAddress(topPtrAddr, argPtr);

  // Load the end address into scratch, which is the callee token.
  masm.computeEffectiveAddress(calleeTokenAddr, scratch);

  // Copy |this|+arguments
  Label loop;
  masm.bind(&loop);
  {
    masm.pushValue(Address(argPtr, 0));
    masm.subPtr(Imm32(sizeof(Value)), argPtr);
    masm.branchPtr(Assembler::Above, argPtr, scratch, &loop);
  }
  masm.jump(&argsPushed);

  masm.bind(&moduleResume);
  {
    // Resuming a module (top-level await). Module frames have no ThisV and no
    // arguments, so the resume args are the only Values the caller pushed.
    masm.alignJitStackBasedOnNumValues(ResumeFrameArgs::NumSlots);

    // Copy them from the last slot to the first, to preserve their order.
    static_assert(sizeof(BaselineInterpreterEntryFrameLayout) ==
                  sizeof(JitFrameLayout));
    constexpr size_t base =
        BaselineInterpreterEntryFrameLayout::offsetOfModuleResumeArgs();
    for (uint32_t slot = ResumeFrameArgs::NumSlots; slot > 0; slot--) {
      size_t offset = base + ResumeFrameArgs::offsetOfSlot(slot - 1);
      masm.pushValue(Address(FramePointer, int32_t(offset)));
    }
  }
  masm.bind(&argsPushed);

  // Copy callee token
  masm.push(callee);

  // Save a new descriptor using BaselineInterpreterEntry frame type. When
  // resuming a generator we have to propagate the IsResumingGenerator bit so
  // that the callee's prologue dispatches to the resume point.
  Label descriptorPushed, notResuming;
  masm.branchTest32(Assembler::Zero, descriptorAddr,
                    Imm32(FrameDescriptor::IsResumingGenerator), &notResuming);
  masm.push(FrameDescriptor(FrameType::BaselineInterpreterEntry, /* argc = */ 0,
                            /* hasInlinedICScript = */ false,
                            /* isResumingGenerator = */ true));
  masm.jump(&descriptorPushed);
  masm.bind(&notResuming);
  masm.loadNumActualArgs(FramePointer, scratch);
  masm.pushFrameDescriptorForJitCall(FrameType::BaselineInterpreterEntry,
                                     scratch, scratch);
  masm.bind(&descriptorPushed);

  // Call into baseline interpreter
  uint8_t* blinterpAddr = baselineInterpreter().codeRaw();
  masm.assertStackAlignment(JitStackAlignment, 2 * sizeof(uintptr_t));
  masm.call(ImmPtr(blinterpAddr));

  masm.moveToStackPtr(FramePointer);
  masm.pop(FramePointer);
  masm.ret();
}

void JitRuntime::generateInterpreterEntryTrampoline(MacroAssembler& masm) {
  AutoCreatedBy acb(masm, "JitRuntime::generateInterpreterEntryTrampoline");

  // If BLI is disabled, we don't need an offset.
  if (IsBaselineInterpreterEnabled()) {
    uint32_t offset = startTrampolineCode(masm);
    if (!masm.oom()) {
      // The offset is shared by all per-script trampolines.
      if (!vmInterpreterEntryOffset_) {
        vmInterpreterEntryOffset_ = offset;
      }
      MOZ_RELEASE_ASSERT(vmInterpreterEntryOffset_ == offset);
    }
  }

#ifdef JS_CODEGEN_ARM64
  // Use the normal stack pointer for the initial pushes.
  masm.SetStackPointer64(sp);

  // Push lr and fp together to maintain 16-byte alignment.
  masm.push(lr, FramePointer);
  masm.moveStackPtrTo(FramePointer);

  // Save the PSP register (r20), and a scratch (r19).
  masm.push(r19, r20);

  // Setup the PSP so we can use callWithABI below.
  masm.SetStackPointer64(PseudoStackPointer64);
  masm.initPseudoStackPtr();

  Register arg0 = IntArgReg0;
  Register arg1 = IntArgReg1;
  Register scratch = r19;
#elif defined(JS_CODEGEN_X86)
  masm.push(FramePointer);
  masm.moveStackPtrTo(FramePointer);

  AllocatableRegisterSet regs(RegisterSet::Volatile());
  Register arg0 = regs.takeAnyGeneral();
  Register arg1 = regs.takeAnyGeneral();
  Register scratch = regs.takeAnyGeneral();

  // First two arguments are passed on the stack in 32-bit.
  Address cxAddr(FramePointer, 2 * sizeof(void*));
  Address stateAddr(FramePointer, 3 * sizeof(void*));
  masm.loadPtr(cxAddr, arg0);
  masm.loadPtr(stateAddr, arg1);
#else
#  ifdef JS_USE_LINK_REGISTER
  masm.pushRegs(LinkRegister, FramePointer);
#  else
  masm.push(FramePointer);
#  endif
  masm.moveStackPtrTo(FramePointer);

  AllocatableRegisterSet regs(RegisterSet::Volatile());
  regs.take(IntArgReg0);
  regs.take(IntArgReg1);
  Register arg0 = IntArgReg0;
  Register arg1 = IntArgReg1;
  Register scratch = regs.takeAnyGeneral();
#endif

  using Fn = bool (*)(JSContext* cx, js::RunState& state);
  masm.setupUnalignedABICall(scratch);
  masm.passABIArg(arg0);  // cx
  masm.passABIArg(arg1);  // state
  masm.callWithABI<Fn, Interpret>(
      ABIType::General, CheckUnsafeCallWithABI::DontCheckHasExitFrame);

#ifdef JS_CODEGEN_ARM64
  masm.syncStackPtr();
  masm.SetStackPointer64(sp);

  // Restore r20 and r19.
  masm.pop(r20, r19);

  // Restore old fp and pop lr for return.
  masm.pop(FramePointer, lr);
  masm.abiret();

  // Reset stack pointer.
  masm.SetStackPointer64(PseudoStackPointer64);
#else
  masm.moveToStackPtr(FramePointer);
  masm.pop(FramePointer);
  masm.ret();
#endif
}

JitCode* JitRuntime::generateEntryTrampolineForScript(JSContext* cx,
                                                      JSScript* script) {
  if (JitSpewEnabled(JitSpew_Codegen)) {
    UniqueChars funName;
    if (script->function() && script->function()->fullDisplayAtom()) {
      funName =
          AtomToPrintableString(cx, script->function()->fullDisplayAtom());
    }

    JitSpew(JitSpew_Codegen,
            "# Emitting Interpreter Entry Trampoline for %s (%s:%u:%u)",
            funName ? funName.get() : "*", script->filename(), script->lineno(),
            script->column().oneOriginValue());
  }

  TempAllocator temp(&cx->tempLifoAlloc());
  JitContext jctx(cx);
  StackMacroAssembler masm(cx, temp);
  AutoCreatedBy acb(masm, "JitRuntime::generateEntryTrampolineForScript");
  PerfSpewerRangeRecorder rangeRecorder(masm);

  if (IsBaselineInterpreterEnabled()) {
    generateBaselineInterpreterEntryTrampoline(masm);
    rangeRecorder.recordOffset("BaselineInterpreter", cx, script);
  }

  generateInterpreterEntryTrampoline(masm);
  rangeRecorder.recordOffset("Interpreter", cx, script);

  Linker linker(masm);
  JitCode* code = linker.newCode(cx, CodeKind::Other);
  if (!code) {
    return nullptr;
  }
  rangeRecorder.collectRangesForJitCode(code);
  JitSpew(JitSpew_Codegen, "# code = %p", code->raw());
  return code;
}
