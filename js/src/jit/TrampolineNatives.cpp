/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/TrampolineNatives.h"

#include "jit/CalleeToken.h"
#include "jit/Ion.h"
#include "jit/JitCommon.h"
#include "jit/JitRuntime.h"
#include "jit/MacroAssembler.h"
#include "jit/PerfSpewer.h"
#include "js/CallArgs.h"
#include "js/experimental/JitInfo.h"
#include "vm/TypedArrayObject.h"

#include "jit/MacroAssembler-inl.h"
#include "vm/Activation-inl.h"

using namespace js;
using namespace js::jit;

#define ADD_NATIVE(native)                   \
  const JSJitInfo js::jit::JitInfo_##native{ \
      {nullptr},                             \
      {uint16_t(TrampolineNative::native)},  \
      {0},                                   \
      JSJitInfo::TrampolineNative};
TRAMPOLINE_NATIVE_LIST(ADD_NATIVE)
#undef ADD_NATIVE

void js::jit::SetTrampolineNativeJitEntry(JSContext* cx, JSFunction* fun,
                                          TrampolineNative native) {
  if (!cx->runtime()->jitRuntime()) {
    // No JIT support so there's no trampoline.
    return;
  }
  void** entry = cx->runtime()->jitRuntime()->trampolineNativeJitEntry(native);
  MOZ_ASSERT(entry);
  MOZ_ASSERT(*entry);
  fun->setTrampolineNativeJitEntry(entry);
}

uint32_t JitRuntime::generateArraySortTrampoline(MacroAssembler& masm,
                                                 ArraySortKind kind) {
  AutoCreatedBy acb(masm, "JitRuntime::generateArraySortTrampoline");

  const uint32_t offset = startTrampolineCode(masm);

  // The stack for the trampoline frame will look like this:
  //
  //   [TrampolineNativeFrameLayout]
  //     * this and arguments passed by the caller
  //     * CalleeToken
  //     * Descriptor
  //     * Return Address
  //     * Saved frame pointer   <= FramePointer
  //   [ArraySortData]
  //     * ...
  //     * Comparator this + argument Values --+ -> comparator JitFrameLayout
  //     * Comparator (CalleeToken)            |
  //     * Descriptor                      ----+ <= StackPointer
  //
  // The call to the comparator pushes the return address and the frame pointer,
  // so we check the alignment after pushing these two pointers.
  constexpr size_t FrameSize = sizeof(ArraySortData);
  constexpr size_t PushedByCall = 2 * sizeof(void*);
  static_assert((FrameSize + PushedByCall) % JitStackAlignment == 0);

  // Assert ArraySortData comparator data matches JitFrameLayout.
  static_assert(PushedByCall + ArraySortData::offsetOfDescriptor() ==
                JitFrameLayout::offsetOfDescriptor());
  static_assert(PushedByCall + ArraySortData::offsetOfComparator() ==
                JitFrameLayout::offsetOfCalleeToken());
  static_assert(PushedByCall + ArraySortData::offsetOfComparatorThis() ==
                JitFrameLayout::offsetOfThis());
  static_assert(PushedByCall + ArraySortData::offsetOfComparatorArgs() ==
                JitFrameLayout::offsetOfActualArgs());
  static_assert(CalleeToken_Function == 0,
                "JSFunction* is valid CalleeToken for non-constructor calls");

  // Compute offsets from FramePointer.
  constexpr int32_t ComparatorOffset =
      -int32_t(FrameSize) + ArraySortData::offsetOfComparator();
  constexpr int32_t RvalOffset =
      -int32_t(FrameSize) + ArraySortData::offsetOfComparatorReturnValue();
  constexpr int32_t DescriptorOffset =
      -int32_t(FrameSize) + ArraySortData::offsetOfDescriptor();
  constexpr int32_t ComparatorThisOffset =
      -int32_t(FrameSize) + ArraySortData::offsetOfComparatorThis();
  constexpr int32_t ComparatorArgsOffset =
      -int32_t(FrameSize) + ArraySortData::offsetOfComparatorArgs();

#ifdef JS_USE_LINK_REGISTER
  masm.pushReturnAddress();
#endif
  masm.push(FramePointer);
  masm.moveStackPtrTo(FramePointer);

  AllocatableGeneralRegisterSet regs(GeneralRegisterSet::All());
  regs.takeUnchecked(ReturnReg);
  regs.takeUnchecked(JSReturnOperand);
  Register temp0 = regs.takeAny();
  Register temp1 = regs.takeAny();
  Register temp2 = regs.takeAny();

  // Reserve space and check alignment of the comparator frame.
  masm.reserveStack(FrameSize);
  masm.assertStackAlignment(JitStackAlignment, PushedByCall);

  // Trampoline control flow looks like this:
  //
  //     call ArraySortFromJit or TypedArraySortFromJit
  //     goto checkReturnValue
  //   call_comparator:
  //     call comparator
  //     call ArraySortData::sortArrayWithComparator or
  //          ArraySortData::sortTypedArrayWithComparator
  //   checkReturnValue:
  //     check return value, jump to call_comparator if needed
  //     return rval

  auto pushExitFrame = [&](Register cxReg, Register scratchReg) {
    MOZ_ASSERT(masm.framePushed() == FrameSize);
    masm.Push(FrameDescriptor(FrameType::TrampolineNative));
    masm.Push(ImmWord(0));  // Fake return address.
    masm.Push(FramePointer);
    masm.enterFakeExitFrame(cxReg, scratchReg, ExitFrameType::Bare);
  };

  // Call {Typed}ArraySortFromJit.
  using Fn1 = ArraySortResult (*)(JSContext* cx,
                                  jit::TrampolineNativeFrameLayout* frame);
  masm.loadJSContext(temp0);
  pushExitFrame(temp0, temp1);
  masm.setupAlignedABICall();
  masm.passABIArg(temp0);
  masm.passABIArg(FramePointer);
  switch (kind) {
    case ArraySortKind::Array:
      masm.callWithABI<Fn1, ArraySortFromJit>(
          ABIType::General, CheckUnsafeCallWithABI::DontCheckHasExitFrame);
      break;
    case ArraySortKind::TypedArray:
      masm.callWithABI<Fn1, TypedArraySortFromJit>(
          ABIType::General, CheckUnsafeCallWithABI::DontCheckHasExitFrame);
      break;
  }

  // Check return value.
  Label checkReturnValue;
  masm.jump(&checkReturnValue);
  masm.setFramePushed(FrameSize);

  // Call the comparator. Store the frame descriptor before each call to ensure
  // the HasCachedSavedFrame flag from a previous call is cleared.
  uintptr_t jitCallDescriptor = MakeFrameDescriptorForJitCall(
      jit::FrameType::TrampolineNative, ArraySortData::ComparatorActualArgs);
  Label callDone, jitCallFast, jitCallSlow;
  masm.bind(&jitCallFast);
  {
    masm.storeValue(UndefinedValue(),
                    Address(FramePointer, ComparatorThisOffset));
    masm.storePtr(ImmWord(jitCallDescriptor),
                  Address(FramePointer, DescriptorOffset));
    masm.loadPtr(Address(FramePointer, ComparatorOffset), temp0);
    masm.loadJitCodeRaw(temp0, temp1);
    masm.callJit(temp1);
    masm.jump(&callDone);
  }
  masm.bind(&jitCallSlow);
  {
    masm.storeValue(UndefinedValue(),
                    Address(FramePointer, ComparatorThisOffset));
    masm.storePtr(ImmWord(jitCallDescriptor),
                  Address(FramePointer, DescriptorOffset));
    masm.loadPtr(Address(FramePointer, ComparatorOffset), temp0);
    masm.loadJitCodeRaw(temp0, temp1);
    masm.switchToObjectRealm(temp0, temp2);

    // Handle arguments underflow.
    Label noUnderflow, restoreRealm;
    masm.loadFunctionArgCount(temp0, temp0);
    masm.branch32(Assembler::BelowOrEqual, temp0,
                  Imm32(ArraySortData::ComparatorActualArgs), &noUnderflow);
    {
      // If the comparator expects more than two arguments, we must
      // push additional undefined values.
      if (JitStackValueAlignment > 1) {
        static_assert(!(ArraySortData::ComparatorActualArgs & 1));
        static_assert(sizeof(JitFrameLayout) % JitStackAlignment == 0,
                      "JitFrameLayout doesn't affect stack alignment");
        MOZ_ASSERT(JitStackValueAlignment == 2);
        // We're currently aligned so that we'll be aligned to JitStackAlignment
        // after pushing PushedByCall bytes. Before we do the call, we will be
        // pushing nargs arguments, `this`, a callee token, and a descriptor.
        // This is (nargs + 1) * sizeof(Value) + 2 * sizeof(uintptr_t) bytes.
        // We want to push a multiple of JitStackAlignment bytes, which may
        // necessitate 8 bytes of padding, depending on the parity of nargs.

        bool evenNargsNeedsAlignment =
            (sizeof(Value) + 2 * sizeof(uintptr_t)) % JitStackAlignment != 0;
        Assembler::Condition cond =
            evenNargsNeedsAlignment ? Assembler::NonZero : Assembler::Zero;

        Label aligned;
        masm.branchTest32(cond, temp0, Imm32(1), &aligned);
        masm.subFromStackPtr(Imm32(sizeof(Value)));
        masm.bind(&aligned);
      }

      // Push `undefined` arguments.
      Label loop;
      masm.bind(&loop);
      masm.pushValue(UndefinedValue());
      masm.sub32(Imm32(1), temp0);
      masm.branch32(Assembler::GreaterThan, temp0,
                    Imm32(ArraySortData::ComparatorActualArgs), &loop);

      // Copy the existing arguments, this, callee, and descriptor, then call.
      masm.pushValue(
          Address(FramePointer, ComparatorArgsOffset + int32_t(sizeof(Value))));
      masm.pushValue(Address(FramePointer, ComparatorArgsOffset));
      masm.pushValue(Address(FramePointer, ComparatorThisOffset));
      masm.push(Address(FramePointer, ComparatorOffset));
      masm.push(Address(FramePointer, DescriptorOffset));
      masm.callJit(temp1);

      // Restore the expected stack pointer.
      masm.computeEffectiveAddress(Address(FramePointer, -int32_t(FrameSize)),
                                   temp0);
      masm.moveToStackPtr(temp0);

      masm.jump(&restoreRealm);
    }
    masm.bind(&noUnderflow);
    masm.callJit(temp1);

    masm.bind(&restoreRealm);
    Address calleeToken(FramePointer,
                        TrampolineNativeFrameLayout::offsetOfCalleeToken());
    masm.loadFunctionFromCalleeToken(calleeToken, temp0);
    masm.switchToObjectRealm(temp0, temp1);
  }

  // Store the comparator's return value.
  masm.bind(&callDone);
  masm.storeValue(JSReturnOperand, Address(FramePointer, RvalOffset));

  // Call ArraySortData::sort{Typed}ArrayWithComparator.
  using Fn2 = ArraySortResult (*)(ArraySortData* data);
  masm.moveStackPtrTo(temp2);
  masm.loadJSContext(temp0);
  pushExitFrame(temp0, temp1);
  masm.setupAlignedABICall();
  masm.passABIArg(temp2);
  switch (kind) {
    case ArraySortKind::Array:
      masm.callWithABI<Fn2, ArraySortData::sortArrayWithComparator>(
          ABIType::General, CheckUnsafeCallWithABI::DontCheckHasExitFrame);
      break;
    case ArraySortKind::TypedArray:
      masm.callWithABI<Fn2, ArraySortData::sortTypedArrayWithComparator>(
          ABIType::General, CheckUnsafeCallWithABI::DontCheckHasExitFrame);
      break;
  }

  // Check return value.
  masm.bind(&checkReturnValue);
  masm.branch32(Assembler::Equal, ReturnReg,
                Imm32(int32_t(ArraySortResult::Failure)), masm.failureLabel());
  masm.freeStack(ExitFrameLayout::SizeWithFooter());
  masm.branch32(Assembler::Equal, ReturnReg,
                Imm32(int32_t(ArraySortResult::CallJSSameRealmNoUnderflow)),
                &jitCallFast);
  masm.branch32(Assembler::Equal, ReturnReg,
                Imm32(int32_t(ArraySortResult::CallJS)), &jitCallSlow);
#ifdef DEBUG
  Label ok;
  masm.branch32(Assembler::Equal, ReturnReg,
                Imm32(int32_t(ArraySortResult::Done)), &ok);
  masm.assumeUnreachable("Unexpected return value");
  masm.bind(&ok);
#endif

  masm.loadValue(Address(FramePointer, RvalOffset), JSReturnOperand);
  masm.moveToStackPtr(FramePointer);
  masm.pop(FramePointer);
  masm.ret();

  return offset;
}

void JitRuntime::generateTrampolineNatives(
    MacroAssembler& masm, TrampolineNativeJitEntryOffsets& offsets,
    PerfSpewerRangeRecorder& rangeRecorder) {
  offsets[TrampolineNative::ArraySort] =
      generateArraySortTrampoline(masm, ArraySortKind::Array);
  rangeRecorder.recordOffset("Trampoline: ArraySort");

  offsets[TrampolineNative::TypedArraySort] =
      generateArraySortTrampoline(masm, ArraySortKind::TypedArray);
  rangeRecorder.recordOffset("Trampoline: TypedArraySort");
}

bool jit::CallTrampolineNativeJitCode(JSContext* cx, TrampolineNative native,
                                      CallArgs& args) {
  // Use the EnterJit trampoline to enter the native's trampoline code.

  AutoCheckRecursionLimit recursion(cx);
  if (!recursion.check(cx)) {
    return false;
  }

  MOZ_ASSERT(!TooManyActualArguments(args.length()));

  MOZ_ASSERT(!args.isConstructing());
  CalleeToken calleeToken = CalleeToToken(&args.callee().as<JSFunction>(),
                                          /* constructing = */ false);

  Value* maxArgv = args.array();
  size_t maxArgc = args.length();

  Rooted<Value> result(cx, Int32Value(args.length()));

  AssertRealmUnchanged aru(cx);
  JitActivation activation(cx);

  EnterJitCode enter = cx->runtime()->jitRuntime()->enterJit();
  void* code = *cx->runtime()->jitRuntime()->trampolineNativeJitEntry(native);

  CALL_GENERATED_CODE(enter, code, maxArgc, maxArgv, /* osrFrame = */ nullptr,
                      calleeToken, /* envChain = */ nullptr,
                      /* osrNumStackValues = */ 0, result.address());

  // Ensure the counter was reset to zero after exiting from JIT code.
  MOZ_ASSERT(!cx->isInUnsafeRegion());

  // Release temporary buffer used for OSR into Ion.
  cx->runtime()->jitRuntime()->freeIonOsrTempData();

  if (result.isMagic(JS_ION_ERROR)) {
    return false;
  }

  args.rval().set(result);
  return true;
}
