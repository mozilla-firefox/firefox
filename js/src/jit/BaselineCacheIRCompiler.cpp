/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/BaselineCacheIRCompiler.h"

#include "mozilla/RandomNum.h"

#include "gc/GC.h"
#include "jit/CacheIR.h"
#include "jit/CacheIRAOT.h"
#include "jit/CacheIRSpewer.h"
#include "jit/CacheIRWriter.h"
#include "jit/JitFrames.h"
#include "jit/JitRuntime.h"
#include "jit/JitZone.h"
#include "jit/Linker.h"
#include "jit/MoveEmitter.h"
#include "jit/RegExpStubConstants.h"
#include "jit/SharedICHelpers.h"
#include "jit/StubFolding.h"
#include "jit/VMFunctions.h"
#include "js/experimental/JitInfo.h"  // JSJitInfo
#include "js/friend/DOMProxy.h"       // JS::ExpandoAndGeneration
#include "proxy/DeadObjectProxy.h"
#include "proxy/Proxy.h"
#include "util/Unicode.h"
#include "vm/PortableBaselineInterpret.h"
#include "vm/StaticStrings.h"

#include "jit/JitScript-inl.h"
#include "jit/MacroAssembler-inl.h"
#include "jit/SharedICHelpers-inl.h"
#include "jit/VMFunctionList-inl.h"

using namespace js;
using namespace js::jit;

using mozilla::Maybe;

using JS::ExpandoAndGeneration;

namespace js {
namespace jit {

static uint32_t GetICStackValueOffset() {
  uint32_t offset = ICStackValueOffset;
  if (JitOptions.enableICFramePointers) {
#ifdef JS_USE_LINK_REGISTER
    // The frame pointer and return address are also on the stack.
    offset += 2 * sizeof(uintptr_t);
#else
    // The frame pointer is also on the stack.
    offset += sizeof(uintptr_t);
#endif
  }
  return offset;
}

static void PushICFrameRegs(MacroAssembler& masm) {
  MOZ_ASSERT(JitOptions.enableICFramePointers);
#ifdef JS_USE_LINK_REGISTER
  masm.pushRegs(LinkRegister, FramePointer);
#else
  masm.push(FramePointer);
#endif
}

static void PopICFrameRegs(MacroAssembler& masm) {
  MOZ_ASSERT(JitOptions.enableICFramePointers);
#ifdef JS_USE_LINK_REGISTER
  masm.popRegs(FramePointer, LinkRegister);
#else
  masm.pop(FramePointer);
#endif
}

Address CacheRegisterAllocator::addressOf(MacroAssembler& masm,
                                          BaselineFrameSlot slot) const {
  uint32_t offset =
      stackPushed_ + GetICStackValueOffset() + slot.slot() * sizeof(JS::Value);
  return Address(masm.getStackPointer(), offset);
}
BaseValueIndex CacheRegisterAllocator::addressOf(MacroAssembler& masm,
                                                 Register argcReg,
                                                 BaselineFrameSlot slot) const {
  uint32_t offset =
      stackPushed_ + GetICStackValueOffset() + slot.slot() * sizeof(JS::Value);
  return BaseValueIndex(masm.getStackPointer(), argcReg, offset);
}

// BaselineCacheIRCompiler compiles CacheIR to BaselineIC native code.
BaselineCacheIRCompiler::BaselineCacheIRCompiler(JSContext* cx,
                                                 TempAllocator& alloc,
                                                 const CacheIRWriter& writer,
                                                 uint32_t stubDataOffset)
    : CacheIRCompiler(cx, alloc, writer, stubDataOffset, Mode::Baseline,
                      StubFieldPolicy::Address),
      makesGCCalls_(false) {}

// AutoStubFrame methods
AutoStubFrame::AutoStubFrame(BaselineCacheIRCompiler& compiler)
    : compiler(compiler)
#ifdef DEBUG
      ,
      framePushedAtEnterStubFrame_(0)
#endif
{
}
void AutoStubFrame::enter(MacroAssembler& masm, Register scratch) {
  MOZ_ASSERT(compiler.allocator.stackPushed() == 0);

  if (JitOptions.enableICFramePointers) {
    // If we have already pushed the frame pointer, pop it
    // before creating the stub frame.
    PopICFrameRegs(masm);
  }
  EmitBaselineEnterStubFrame(masm, scratch);

#ifdef DEBUG
  framePushedAtEnterStubFrame_ = masm.framePushed();
#endif

  MOZ_ASSERT(!compiler.enteredStubFrame_);
  compiler.enteredStubFrame_ = true;

  // All current uses of this are to call VM functions that can GC.
  compiler.makesGCCalls_ = true;
}
void AutoStubFrame::leave(MacroAssembler& masm) {
  MOZ_ASSERT(compiler.enteredStubFrame_);
  compiler.enteredStubFrame_ = false;

#ifdef DEBUG
  masm.setFramePushed(framePushedAtEnterStubFrame_);
#endif

  EmitBaselineLeaveStubFrame(masm);
  if (JitOptions.enableICFramePointers) {
    // We will pop the frame pointer when we return,
    // so we have to push it again now.
    PushICFrameRegs(masm);
  }
}

void AutoStubFrame::pushInlinedICScript(MacroAssembler& masm,
                                        Address icScriptAddr) {
  // The baseline prologue expects the inlined ICScript to
  // be at a fixed offset from the stub frame pointer.
  MOZ_ASSERT(compiler.localTracingSlots_ == 0);
  masm.Push(icScriptAddr);

#ifndef JS_64BIT
  // We expect the stack to be Value-aligned when we start pushing arguments.
  // We are already aligned when we enter the stub frame. Pushing a 32-bit
  // ICScript requires an additional adjustment to maintain alignment.
  static_assert(sizeof(Value) == 2 * sizeof(uintptr_t));
  masm.subFromStackPtr(Imm32(sizeof(uintptr_t)));
#endif
}

void AutoStubFrame::storeTracedValue(MacroAssembler& masm, ValueOperand value) {
  MOZ_ASSERT(compiler.localTracingSlots_ < 255);
  MOZ_ASSERT(masm.framePushed() - framePushedAtEnterStubFrame_ ==
             compiler.localTracingSlots_ * sizeof(Value));
  masm.Push(value);
  compiler.localTracingSlots_++;
}

void AutoStubFrame::loadTracedValue(MacroAssembler& masm, uint8_t slotIndex,
                                    ValueOperand value) {
  MOZ_ASSERT(slotIndex <= compiler.localTracingSlots_);
  int32_t offset = BaselineStubFrameLayout::LocallyTracedValueOffset +
                   slotIndex * sizeof(Value);
  masm.loadValue(Address(FramePointer, -offset), value);
}

#ifdef DEBUG
AutoStubFrame::~AutoStubFrame() { MOZ_ASSERT(!compiler.enteredStubFrame_); }
#endif

}  // namespace jit
}  // namespace js

bool BaselineCacheIRCompiler::makesGCCalls() const { return makesGCCalls_; }

Address BaselineCacheIRCompiler::stubAddress(uint32_t offset) const {
  return Address(ICStubReg, stubDataOffset_ + offset);
}

template <typename Fn, Fn fn>
void BaselineCacheIRCompiler::callVM(MacroAssembler& masm) {
  VMFunctionId id = VMFunctionToId<Fn, fn>::id;
  callVMInternal(masm, id);
}

JitCode* BaselineCacheIRCompiler::compile() {
  AutoCreatedBy acb(masm, "BaselineCacheIRCompiler::compile");

#ifndef JS_USE_LINK_REGISTER
  masm.adjustFrame(sizeof(intptr_t));
#endif
#ifdef JS_CODEGEN_ARM
  AutoNonDefaultSecondScratchRegister andssr(masm, BaselineSecondScratchReg);
#endif
  if (JitOptions.enableICFramePointers) {
    /* [SMDOC] Baseline IC Frame Pointers
     *
     *  In general, ICs don't have frame pointers until just before
     *  doing a VM call, at which point we retroactively create a stub
     *  frame. However, for the sake of external profilers, we
     *  optionally support full-IC frame pointers in baseline ICs, with
     *  the following approach:
     *    1. We push a frame pointer when we enter an IC.
     *    2. We pop the frame pointer when we return from an IC, or
     *       when we jump to the next IC.
     *    3. Entering a stub frame for a VM call already pushes a
     *       frame pointer, so we pop our existing frame pointer
     *       just before entering a stub frame and push it again
     *       just after leaving a stub frame.
     *  Some ops take advantage of the fact that the frame pointer is
     *  not updated until we enter a stub frame to read values from
     *  the caller's frame. To support this, we allocate a separate
     *  baselineFrame register when IC frame pointers are enabled.
     */
    PushICFrameRegs(masm);
    masm.moveStackPtrTo(FramePointer);

    MOZ_ASSERT(baselineFrameReg() != FramePointer);
    masm.loadPtr(Address(FramePointer, 0), baselineFrameReg());
  }

  // Count stub entries: We count entries rather than successes as it much
  // easier to ensure ICStubReg is valid at entry than at exit.
  Address enteredCount(ICStubReg, ICCacheIRStub::offsetOfEnteredCount());
  masm.add32(Imm32(1), enteredCount);

  perfSpewer_.startRecording();

  CacheIRReader reader(writer_);
  do {
    CacheOp op = reader.readOp();
    perfSpewer_.recordInstruction(masm, op);
    switch (op) {
#define DEFINE_OP(op, ...)                 \
  case CacheOp::op:                        \
    if (!emit##op(reader)) return nullptr; \
    break;
      CACHE_IR_OPS(DEFINE_OP)
#undef DEFINE_OP

      default:
        MOZ_CRASH("Invalid op");
    }
    allocator.nextOp();
  } while (reader.more());

  MOZ_ASSERT(!enteredStubFrame_);
  allocator.discardStack(masm);
  if (JitOptions.enableICFramePointers) {
    PopICFrameRegs(masm);
  }
  EmitReturnFromIC(masm);

  // Done emitting the main IC code. Now emit the failure paths.
  perfSpewer_.recordOffset(masm, "FailurePath");

  for (size_t i = 0; i < failurePaths.length(); i++) {
    if (!emitFailurePath(i)) {
      return nullptr;
    }
    if (JitOptions.enableICFramePointers) {
      PopICFrameRegs(masm);
    }
    EmitStubGuardFailure(masm);
  }

  perfSpewer_.endRecording();

  Linker linker(masm);
  JitCode* newStubCode = linker.newCode(cx_, CodeKind::Baseline);
  if (!newStubCode) {
    cx_->recoverFromOutOfMemory();
    return nullptr;
  }

  newStubCode->setLocalTracingSlots(localTracingSlots_);

  return newStubCode;
}

bool BaselineCacheIRCompiler::emitGuardShape(ObjOperandId objId,
                                             uint32_t shapeOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register obj = allocator.useRegister(masm, objId);
  AutoScratchRegister scratch1(allocator, masm);

  bool needSpectreMitigations = objectGuardNeedsSpectreMitigations(objId);

  Maybe<AutoScratchRegister> maybeScratch2;
  if (needSpectreMitigations) {
    maybeScratch2.emplace(allocator, masm);
  }

  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  Address addr(stubAddress(shapeOffset));
  masm.loadPtr(addr, scratch1);
  if (needSpectreMitigations) {
    masm.branchTestObjShape(Assembler::NotEqual, obj, scratch1, *maybeScratch2,
                            obj, failure->label());
  } else {
    masm.branchTestObjShapeNoSpectreMitigations(Assembler::NotEqual, obj,
                                                scratch1, failure->label());
  }

  return true;
}

bool BaselineCacheIRCompiler::emitGuardProto(ObjOperandId objId,
                                             uint32_t protoOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register obj = allocator.useRegister(masm, objId);
  AutoScratchRegister scratch(allocator, masm);

  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  Address addr(stubAddress(protoOffset));
  masm.loadObjProto(obj, scratch);
  masm.branchPtr(Assembler::NotEqual, addr, scratch, failure->label());
  return true;
}

bool BaselineCacheIRCompiler::emitGuardCompartment(ObjOperandId objId,
                                                   uint32_t globalOffset,
                                                   uint32_t compartmentOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register obj = allocator.useRegister(masm, objId);
  AutoScratchRegister scratch(allocator, masm);

  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  // Verify that the global wrapper is still valid, as
  // it is pre-requisite for doing the compartment check.
  Address globalWrapper(stubAddress(globalOffset));
  masm.loadPtr(globalWrapper, scratch);
  Address handlerAddr(scratch, ProxyObject::offsetOfHandler());
  masm.branchPtr(Assembler::Equal, handlerAddr,
                 ImmPtr(&DeadObjectProxy::singleton), failure->label());

  Address addr(stubAddress(compartmentOffset));
  masm.branchTestObjCompartment(Assembler::NotEqual, obj, addr, scratch,
                                failure->label());
  return true;
}

bool BaselineCacheIRCompiler::emitGuardAnyClass(ObjOperandId objId,
                                                uint32_t claspOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register obj = allocator.useRegister(masm, objId);
  AutoScratchRegister scratch(allocator, masm);

  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  Address testAddr(stubAddress(claspOffset));
  if (objectGuardNeedsSpectreMitigations(objId)) {
    masm.branchTestObjClass(Assembler::NotEqual, obj, testAddr, scratch, obj,
                            failure->label());
  } else {
    masm.branchTestObjClassNoSpectreMitigations(
        Assembler::NotEqual, obj, testAddr, scratch, failure->label());
  }

  return true;
}

bool BaselineCacheIRCompiler::emitGuardHasProxyHandler(ObjOperandId objId,
                                                       uint32_t handlerOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register obj = allocator.useRegister(masm, objId);
  AutoScratchRegister scratch(allocator, masm);

  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  Address testAddr(stubAddress(handlerOffset));
  masm.loadPtr(testAddr, scratch);

  Address handlerAddr(obj, ProxyObject::offsetOfHandler());
  masm.branchPtr(Assembler::NotEqual, handlerAddr, scratch, failure->label());
  return true;
}

bool BaselineCacheIRCompiler::emitGuardSpecificObject(ObjOperandId objId,
                                                      uint32_t expectedOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register obj = allocator.useRegister(masm, objId);

  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  Address addr(stubAddress(expectedOffset));
  masm.branchPtr(Assembler::NotEqual, addr, obj, failure->label());
  return true;
}

bool BaselineCacheIRCompiler::emitGuardSpecificFunction(
    ObjOperandId objId, uint32_t expectedOffset, uint32_t nargsAndFlagsOffset) {
  return emitGuardSpecificObject(objId, expectedOffset);
}

bool BaselineCacheIRCompiler::emitGuardFunctionScript(
    ObjOperandId funId, uint32_t expectedOffset, uint32_t nargsAndFlagsOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  Register fun = allocator.useRegister(masm, funId);
  AutoScratchRegister scratch(allocator, masm);

  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  Address addr(stubAddress(expectedOffset));
  masm.loadPrivate(Address(fun, JSFunction::offsetOfJitInfoOrScript()),
                   scratch);
  masm.branchPtr(Assembler::NotEqual, addr, scratch, failure->label());
  return true;
}

bool BaselineCacheIRCompiler::emitGuardSpecificAtom(StringOperandId strId,
                                                    uint32_t expectedOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register str = allocator.useRegister(masm, strId);
  AutoScratchRegister scratch(allocator, masm);

  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  Address atomAddr(stubAddress(expectedOffset));

  Label done, notCachedAtom;
  masm.branchPtr(Assembler::Equal, atomAddr, str, &done);

  // The pointers are not equal, so if the input string is also an atom it
  // must be a different string.
  masm.branchTest32(Assembler::NonZero, Address(str, JSString::offsetOfFlags()),
                    Imm32(StringFlags::ATOM_BIT), failure->label());

  masm.tryFastAtomize(str, scratch, scratch, &notCachedAtom);
  masm.branchPtr(Assembler::Equal, atomAddr, scratch, &done);
  masm.jump(failure->label());
  masm.bind(&notCachedAtom);

  // Check the length.
  masm.loadPtr(atomAddr, scratch);
  masm.loadStringLength(scratch, scratch);
  masm.branch32(Assembler::NotEqual, Address(str, JSString::offsetOfLength()),
                scratch, failure->label());

  // We have a non-atomized string with the same length. Call a helper
  // function to do the comparison.
  LiveRegisterSet volatileRegs = liveVolatileRegs();
  masm.PushRegsInMask(volatileRegs);

  using Fn = bool (*)(JSString* str1, JSString* str2);
  masm.setupUnalignedABICall(scratch);
  masm.loadPtr(atomAddr, scratch);
  masm.passABIArg(scratch);
  masm.passABIArg(str);
  masm.callWithABI<Fn, EqualStringsHelperPure>();
  masm.storeCallPointerResult(scratch);

  LiveRegisterSet ignore;
  ignore.add(scratch);
  masm.PopRegsInMaskIgnore(volatileRegs, ignore);
  masm.branchIfFalseBool(scratch, failure->label());

  masm.bind(&done);
  return true;
}

bool BaselineCacheIRCompiler::emitGuardSpecificSymbol(SymbolOperandId symId,
                                                      uint32_t expectedOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register sym = allocator.useRegister(masm, symId);

  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  Address addr(stubAddress(expectedOffset));
  masm.branchPtr(Assembler::NotEqual, addr, sym, failure->label());
  return true;
}

bool BaselineCacheIRCompiler::emitGuardSpecificValue(ValOperandId valId,
                                                     uint32_t expectedOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  ValueOperand val = allocator.useValueRegister(masm, valId);

  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  Address addr(stubAddress(expectedOffset));
  masm.branchTestValue(Assembler::NotEqual, addr, val, failure->label());
  return true;
}

bool BaselineCacheIRCompiler::emitLoadValueResult(uint32_t valOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  AutoOutputRegister output(*this);
  masm.loadValue(stubAddress(valOffset), output.valueReg());
  return true;
}

bool BaselineCacheIRCompiler::emitUncheckedLoadWeakValueResult(
    uint32_t valOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  AutoOutputRegister output(*this);
  masm.loadValue(stubAddress(valOffset), output.valueReg());
  return true;
}

bool BaselineCacheIRCompiler::emitUncheckedLoadWeakObjectResult(
    uint32_t objOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  AutoOutputRegister output(*this);
  AutoScratchRegisterMaybeOutput scratch(allocator, masm, output);

  masm.loadPtr(stubAddress(objOffset), scratch);
  masm.tagValue(JSVAL_TYPE_OBJECT, scratch, output.valueReg());
  return true;
}

bool BaselineCacheIRCompiler::emitLoadFixedSlotResult(ObjOperandId objId,
                                                      uint32_t offsetOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  AutoOutputRegister output(*this);
  Register obj = allocator.useRegister(masm, objId);
  AutoScratchRegisterMaybeOutput scratch(allocator, masm, output);

  masm.load32(stubAddress(offsetOffset), scratch);
  masm.loadValue(BaseIndex(obj, scratch, TimesOne), output.valueReg());
  return true;
}

bool BaselineCacheIRCompiler::emitLoadFixedSlotTypedResult(
    ObjOperandId objId, uint32_t offsetOffset, ValueType) {
  // The type is only used by Warp.
  return emitLoadFixedSlotResult(objId, offsetOffset);
}

bool BaselineCacheIRCompiler::emitLoadDynamicSlotResult(ObjOperandId objId,
                                                        uint32_t offsetOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  AutoOutputRegister output(*this);
  Register obj = allocator.useRegister(masm, objId);
  AutoScratchRegisterMaybeOutput scratch(allocator, masm, output);
  AutoScratchRegister scratch2(allocator, masm);

  masm.load32(stubAddress(offsetOffset), scratch);
  masm.loadPtr(Address(obj, NativeObject::offsetOfSlots()), scratch2);
  masm.loadValue(BaseIndex(scratch2, scratch, TimesOne), output.valueReg());
  return true;
}

bool BaselineCacheIRCompiler::emitCallScriptedGetterShared(
    ValOperandId receiverId, ObjOperandId calleeId, bool sameRealm,
    uint32_t nargsAndFlagsOffset, Maybe<uint32_t> icScriptOffset) {
  ValueOperand receiver = allocator.useValueRegister(masm, receiverId);
  Register callee = allocator.useRegister(masm, calleeId);

  AutoScratchRegister code(allocator, masm);
  AutoScratchRegister scratch(allocator, masm);

  bool isInlined = icScriptOffset.isSome();

  // First, retrieve raw jitcode for getter.
  if (isInlined) {
    masm.loadJitCodeRawNoIon(callee, code, scratch);
  } else {
    masm.loadJitCodeRaw(callee, code);
  }

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  if (!sameRealm) {
    masm.switchToObjectRealm(callee, scratch);
  }

  if (isInlined) {
    stubFrame.pushInlinedICScript(masm, stubAddress(*icScriptOffset));
  }

  Label noUnderflow, doneAlignment;
  masm.loadFunctionArgCount(callee, scratch);
  masm.branch32(Assembler::Equal, scratch, Imm32(0), &noUnderflow);

  masm.alignJitStackBasedOnNArgs(scratch, /*countIncludesThis*/ false);

  Label loop;
  masm.bind(&loop);
  masm.Push(UndefinedValue());
  masm.sub32(Imm32(1), scratch);
  masm.branch32(Assembler::Above, scratch, Imm32(0), &loop);
  masm.jump(&doneAlignment);

  // Align the stack such that the JitFrameLayout is aligned on
  // JitStackAlignment.
  masm.bind(&noUnderflow);
  masm.alignJitStackBasedOnNArgs(0, /*countIncludesThis = */ false);
  masm.bind(&doneAlignment);

  // Getter is called with 0 arguments, just |receiver| as thisv.
  // Note that we use Push, not push, so that callJit will align the stack
  // properly on ARM.
  masm.Push(receiver);

  masm.Push(callee);
  masm.Push(
      FrameDescriptor(FrameType::BaselineStub, /* argc = */ 0, isInlined));

  masm.callJit(code);

  stubFrame.leave(masm);

  if (!sameRealm) {
    masm.switchToBaselineFrameRealm(R1.scratchReg());
  }

  return true;
}

bool BaselineCacheIRCompiler::emitCallScriptedGetterResult(
    ValOperandId receiverId, ObjOperandId calleeId, bool sameRealm,
    uint32_t nargsAndFlagsOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Maybe<uint32_t> icScriptOffset = mozilla::Nothing();
  return emitCallScriptedGetterShared(receiverId, calleeId, sameRealm,
                                      nargsAndFlagsOffset, icScriptOffset);
}

bool BaselineCacheIRCompiler::emitCallInlinedGetterResult(
    ValOperandId receiverId, ObjOperandId calleeId, uint32_t icScriptOffset,
    bool sameRealm, uint32_t nargsAndFlagsOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  return emitCallScriptedGetterShared(receiverId, calleeId, sameRealm,
                                      nargsAndFlagsOffset,
                                      mozilla::Some(icScriptOffset));
}

bool BaselineCacheIRCompiler::emitCallNativeGetterResult(
    ValOperandId receiverId, uint32_t getterOffset, bool sameRealm,
    uint32_t nargsAndFlagsOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  ValueOperand receiver = allocator.useValueRegister(masm, receiverId);
  Address getterAddr(stubAddress(getterOffset));

  AutoScratchRegister scratch(allocator, masm);

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  // Load the callee in the scratch register.
  masm.loadPtr(getterAddr, scratch);

  masm.Push(receiver);
  masm.Push(scratch);

  using Fn =
      bool (*)(JSContext*, HandleFunction, HandleValue, MutableHandleValue);
  callVM<Fn, CallNativeGetter>(masm);

  stubFrame.leave(masm);
  return true;
}

bool BaselineCacheIRCompiler::emitCallDOMGetterResult(ObjOperandId objId,
                                                      uint32_t jitInfoOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  Register obj = allocator.useRegister(masm, objId);
  Address jitInfoAddr(stubAddress(jitInfoOffset));

  AutoScratchRegister scratch(allocator, masm);

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  // Load the JSJitInfo in the scratch register.
  masm.loadPtr(jitInfoAddr, scratch);

  masm.PushRegs(obj, scratch);

  using Fn =
      bool (*)(JSContext*, const JSJitInfo*, HandleObject, MutableHandleValue);
  callVM<Fn, CallDOMGetter>(masm);

  stubFrame.leave(masm);
  return true;
}

bool BaselineCacheIRCompiler::emitProxyGetResult(ObjOperandId objId,
                                                 uint32_t idOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register obj = allocator.useRegister(masm, objId);
  Address idAddr(stubAddress(idOffset));

  AutoScratchRegister scratch(allocator, masm);

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  // Load the jsid in the scratch register.
  masm.loadPtr(idAddr, scratch);

  masm.PushRegs(scratch, obj);

  using Fn = bool (*)(JSContext*, HandleObject, HandleId, MutableHandleValue);
  callVM<Fn, ProxyGetProperty>(masm);

  stubFrame.leave(masm);
  return true;
}

bool BaselineCacheIRCompiler::emitFrameIsConstructingResult() {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  Register outputScratch = output.valueReg().scratchReg();

  // Load the CalleeToken.
  Address tokenAddr(baselineFrameReg(), JitFrameLayout::offsetOfCalleeToken());
  masm.loadPtr(tokenAddr, outputScratch);

  // The low bit indicates whether this call is constructing, just clear the
  // other bits.
  static_assert(CalleeToken_Function == 0x0);
  static_assert(CalleeToken_FunctionConstructing == 0x1);
  masm.andPtr(Imm32(0x1), outputScratch);

  masm.tagValue(JSVAL_TYPE_BOOLEAN, outputScratch, output.valueReg());
  return true;
}

bool BaselineCacheIRCompiler::emitLoadConstantStringResult(uint32_t strOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  AutoOutputRegister output(*this);
  AutoScratchRegisterMaybeOutput scratch(allocator, masm, output);

  masm.loadPtr(stubAddress(strOffset), scratch);
  masm.tagValue(JSVAL_TYPE_STRING, scratch, output.valueReg());
  return true;
}

bool BaselineCacheIRCompiler::emitCompareStringResult(JSOp op,
                                                      StringOperandId lhsId,
                                                      StringOperandId rhsId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  AutoOutputRegister output(*this);

  Register left = allocator.useRegister(masm, lhsId);
  Register right = allocator.useRegister(masm, rhsId);

  AutoScratchRegisterMaybeOutput scratch(allocator, masm, output);

  allocator.discardStack(masm);

  Label slow, done;
  masm.compareStrings(op, left, right, scratch, &slow);
  masm.jump(&done);
  masm.bind(&slow);
  {
    AutoStubFrame stubFrame(*this);
    stubFrame.enter(masm, scratch);

    // Push the operands in reverse order for JSOp::Le and JSOp::Gt:
    // - |left <= right| is implemented as |right >= left|.
    // - |left > right| is implemented as |right < left|.
    if (op == JSOp::Le || op == JSOp::Gt) {
      masm.PushRegs(left, right);
    } else {
      masm.PushRegs(right, left);
    }

    using Fn = bool (*)(JSContext*, HandleString, HandleString, bool*);
    if (op == JSOp::Eq || op == JSOp::StrictEq) {
      callVM<Fn, jit::StringsEqual<EqualityKind::Equal>>(masm);
    } else if (op == JSOp::Ne || op == JSOp::StrictNe) {
      callVM<Fn, jit::StringsEqual<EqualityKind::NotEqual>>(masm);
    } else if (op == JSOp::Lt || op == JSOp::Gt) {
      callVM<Fn, jit::StringsCompare<ComparisonKind::LessThan>>(masm);
    } else {
      MOZ_ASSERT(op == JSOp::Le || op == JSOp::Ge);
      callVM<Fn, jit::StringsCompare<ComparisonKind::GreaterThanOrEqual>>(masm);
    }

    stubFrame.leave(masm);
    masm.storeCallPointerResult(scratch);
  }
  masm.bind(&done);
  masm.tagValue(JSVAL_TYPE_BOOLEAN, scratch, output.valueReg());
  return true;
}

bool BaselineCacheIRCompiler::emitSameValueResult(ValOperandId lhsId,
                                                  ValOperandId rhsId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  AutoScratchRegister scratch(allocator, masm);
  ValueOperand lhs = allocator.useValueRegister(masm, lhsId);
#ifdef JS_CODEGEN_X86
  // Use the output to avoid running out of registers.
  allocator.copyToScratchValueRegister(masm, rhsId, output.valueReg());
  ValueOperand rhs = output.valueReg();
#else
  ValueOperand rhs = allocator.useValueRegister(masm, rhsId);
#endif

  allocator.discardStack(masm);

  Label done;
  Label call;

  // Check to see if the values have identical bits.
  // This is correct for SameValue because SameValue(NaN,NaN) is true,
  // and SameValue(0,-0) is false.
  masm.branch64(Assembler::NotEqual, lhs.toRegister64(), rhs.toRegister64(),
                &call);
  masm.moveValue(BooleanValue(true), output.valueReg());
  masm.jump(&done);

  {
    masm.bind(&call);

    AutoStubFrame stubFrame(*this);
    stubFrame.enter(masm, scratch);

    masm.pushValue(lhs);
    masm.pushValue(rhs);

    using Fn = bool (*)(JSContext*, const Value&, const Value&, bool*);
    callVM<Fn, SameValue>(masm);

    stubFrame.leave(masm);
    masm.tagValue(JSVAL_TYPE_BOOLEAN, ReturnReg, output.valueReg());
  }

  masm.bind(&done);
  return true;
}

bool BaselineCacheIRCompiler::emitStoreSlotShared(bool isFixed,
                                                  ObjOperandId objId,
                                                  uint32_t offsetOffset,
                                                  ValOperandId rhsId) {
  Register obj = allocator.useRegister(masm, objId);
  ValueOperand val = allocator.useValueRegister(masm, rhsId);

  AutoScratchRegister scratch1(allocator, masm);
  Maybe<AutoScratchRegister> scratch2;
  if (!isFixed) {
    scratch2.emplace(allocator, masm);
  }

  Address offsetAddr = stubAddress(offsetOffset);
  masm.load32(offsetAddr, scratch1);

  if (isFixed) {
    BaseIndex slot(obj, scratch1, TimesOne);
    EmitPreBarrier(masm, slot, MIRType::Value);
    masm.storeValue(val, slot);
  } else {
    masm.loadPtr(Address(obj, NativeObject::offsetOfSlots()), scratch2.ref());
    BaseIndex slot(scratch2.ref(), scratch1, TimesOne);
    EmitPreBarrier(masm, slot, MIRType::Value);
    masm.storeValue(val, slot);
  }

  emitPostBarrierSlot(obj, val, scratch1);
  return true;
}

bool BaselineCacheIRCompiler::emitStoreFixedSlot(ObjOperandId objId,
                                                 uint32_t offsetOffset,
                                                 ValOperandId rhsId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  return emitStoreSlotShared(true, objId, offsetOffset, rhsId);
}

bool BaselineCacheIRCompiler::emitStoreDynamicSlot(ObjOperandId objId,
                                                   uint32_t offsetOffset,
                                                   ValOperandId rhsId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  return emitStoreSlotShared(false, objId, offsetOffset, rhsId);
}

bool BaselineCacheIRCompiler::emitAddAndStoreSlotShared(
    CacheOp op, ObjOperandId objId, uint32_t offsetOffset, ValOperandId rhsId,
    uint32_t newShapeOffset, Maybe<uint32_t> numNewSlotsOffset,
    bool preserveWrapper) {
  Register obj = allocator.useRegister(masm, objId);
  ValueOperand val = allocator.useValueRegister(masm, rhsId);

  AutoScratchRegister scratch1(allocator, masm);
  AutoScratchRegister scratch2(allocator, masm);

  Address newShapeAddr = stubAddress(newShapeOffset);
  Address offsetAddr = stubAddress(offsetOffset);

  FailurePath* failure = nullptr;
  if (preserveWrapper) {
    if (!addFailurePath(&failure)) {
      return false;
    }
    LiveRegisterSet save = liveVolatileRegs();
    save.takeUnchecked(scratch1);
    save.takeUnchecked(scratch2);
    masm.preserveWrapper(obj, scratch1, scratch2, save);
    masm.branchIfFalseBool(scratch1, failure->label());
  }

  if (op == CacheOp::AllocateAndStoreDynamicSlot) {
    // We have to (re)allocate dynamic slots. Do this first, as it's the
    // only fallible operation here. Note that growSlotsPure is fallible but
    // does not GC.
    Address numNewSlotsAddr = stubAddress(*numNewSlotsOffset);

    if (!failure && !addFailurePath(&failure)) {
      return false;
    }

    LiveRegisterSet save = liveVolatileRegs();
    masm.PushRegsInMask(save);

    using Fn = bool (*)(JSContext* cx, NativeObject* obj, uint32_t newCount);
    masm.setupUnalignedABICall(scratch1);
    masm.loadJSContext(scratch1);
    masm.passABIArg(scratch1);
    masm.passABIArg(obj);
    masm.load32(numNewSlotsAddr, scratch2);
    masm.passABIArg(scratch2);
    masm.callWithABI<Fn, NativeObject::growSlotsPure>();
    masm.storeCallPointerResult(scratch1);

    LiveRegisterSet ignore;
    ignore.add(scratch1);
    masm.PopRegsInMaskIgnore(save, ignore);

    masm.branchIfFalseBool(scratch1, failure->label());
  }

  // Update the object's shape.
  masm.loadPtr(newShapeAddr, scratch1);
  masm.storeObjShape(scratch1, obj,
                     [](MacroAssembler& masm, const Address& addr) {
                       EmitPreBarrier(masm, addr, MIRType::Shape);
                     });

  // Perform the store. No pre-barrier required since this is a new
  // initialization.
  masm.load32(offsetAddr, scratch1);
  if (op == CacheOp::AddAndStoreFixedSlot) {
    BaseIndex slot(obj, scratch1, TimesOne);
    masm.storeValue(val, slot);
  } else {
    MOZ_ASSERT(op == CacheOp::AddAndStoreDynamicSlot ||
               op == CacheOp::AllocateAndStoreDynamicSlot);
    masm.loadPtr(Address(obj, NativeObject::offsetOfSlots()), scratch2);
    BaseIndex slot(scratch2, scratch1, TimesOne);
    masm.storeValue(val, slot);
  }

  emitPostBarrierSlot(obj, val, scratch1);
  return true;
}

bool BaselineCacheIRCompiler::emitAddAndStoreFixedSlot(ObjOperandId objId,
                                                       uint32_t offsetOffset,
                                                       ValOperandId rhsId,
                                                       uint32_t newShapeOffset,
                                                       bool preserveWrapper) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Maybe<uint32_t> numNewSlotsOffset = mozilla::Nothing();
  return emitAddAndStoreSlotShared(CacheOp::AddAndStoreFixedSlot, objId,
                                   offsetOffset, rhsId, newShapeOffset,
                                   numNewSlotsOffset, preserveWrapper);
}

bool BaselineCacheIRCompiler::emitAddAndStoreDynamicSlot(
    ObjOperandId objId, uint32_t offsetOffset, ValOperandId rhsId,
    uint32_t newShapeOffset, bool preserveWrapper) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Maybe<uint32_t> numNewSlotsOffset = mozilla::Nothing();
  return emitAddAndStoreSlotShared(CacheOp::AddAndStoreDynamicSlot, objId,
                                   offsetOffset, rhsId, newShapeOffset,
                                   numNewSlotsOffset, preserveWrapper);
}

bool BaselineCacheIRCompiler::emitAllocateAndStoreDynamicSlot(
    ObjOperandId objId, uint32_t offsetOffset, ValOperandId rhsId,
    uint32_t newShapeOffset, uint32_t numNewSlotsOffset, bool preserveWrapper) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  return emitAddAndStoreSlotShared(
      CacheOp::AllocateAndStoreDynamicSlot, objId, offsetOffset, rhsId,
      newShapeOffset, mozilla::Some(numNewSlotsOffset), preserveWrapper);
}

bool BaselineCacheIRCompiler::emitIsArrayResult(ValOperandId inputId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  AutoScratchRegister scratch1(allocator, masm);
  AutoScratchRegisterMaybeOutput scratch2(allocator, masm, output);

  ValueOperand val = allocator.useValueRegister(masm, inputId);

  allocator.discardStack(masm);

  Label isNotArray;
  // Primitives are never Arrays.
  masm.fallibleUnboxObject(val, scratch1, &isNotArray);

  Label isArray;
  masm.branchTestObjClass(Assembler::Equal, scratch1, &ArrayObject::class_,
                          scratch2, scratch1, &isArray);

  // isArray can also return true for Proxy wrapped Arrays.
  masm.branchTestObjectIsProxy(false, scratch1, scratch2, &isNotArray);
  Label done;
  {
    AutoStubFrame stubFrame(*this);
    stubFrame.enter(masm, scratch2);

    masm.Push(scratch1);

    using Fn = bool (*)(JSContext*, HandleObject, bool*);
    callVM<Fn, js::IsArrayFromJit>(masm);

    stubFrame.leave(masm);

    masm.tagValue(JSVAL_TYPE_BOOLEAN, ReturnReg, output.valueReg());
    masm.jump(&done);
  }

  masm.bind(&isNotArray);
  masm.moveValue(BooleanValue(false), output.valueReg());
  masm.jump(&done);

  masm.bind(&isArray);
  masm.moveValue(BooleanValue(true), output.valueReg());

  masm.bind(&done);
  return true;
}

bool BaselineCacheIRCompiler::emitIsTypedArrayResult(ObjOperandId objId,
                                                     bool isPossiblyWrapped) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  AutoScratchRegisterMaybeOutput scratch(allocator, masm, output);
  Register obj = allocator.useRegister(masm, objId);

  allocator.discardStack(masm);

  Label notTypedArray, isWrapper, done;
  masm.loadObjClassUnsafe(obj, scratch);
  masm.branchIfClassIsNotTypedArray(scratch, &notTypedArray);
  masm.moveValue(BooleanValue(true), output.valueReg());
  masm.jump(&done);

  masm.bind(&notTypedArray);
  if (isPossiblyWrapped) {
    Label notProxy;
    masm.branchTestClassIsProxy(false, scratch, &notProxy);
    masm.branchTestProxyHandlerFamily(Assembler::Equal, obj, scratch,
                                      &Wrapper::family, &isWrapper);
    masm.bind(&notProxy);
  }
  masm.moveValue(BooleanValue(false), output.valueReg());

  if (isPossiblyWrapped) {
    masm.jump(&done);

    masm.bind(&isWrapper);

    AutoStubFrame stubFrame(*this);
    stubFrame.enter(masm, scratch);

    masm.Push(obj);

    using Fn = bool (*)(JSContext*, JSObject*, bool*);
    callVM<Fn, jit::IsPossiblyWrappedTypedArray>(masm);

    stubFrame.leave(masm);

    masm.tagValue(JSVAL_TYPE_BOOLEAN, ReturnReg, output.valueReg());
  }

  masm.bind(&done);
  return true;
}

bool BaselineCacheIRCompiler::emitLoadStringCharResult(
    StringOperandId strId, Int32OperandId indexId,
    StringCharOutOfBounds outOfBounds) {
  AutoOutputRegister output(*this);
  Register str = allocator.useRegister(masm, strId);
  Register index = allocator.useRegister(masm, indexId);
  AutoScratchRegisterMaybeOutput scratch1(allocator, masm, output);
  AutoScratchRegisterMaybeOutputType scratch2(allocator, masm, output);
  AutoScratchRegister scratch3(allocator, masm);

  // Bounds check, load string char.
  Label done;
  Label tagResult;
  Label loadFailed;
  if (outOfBounds == StringCharOutOfBounds::Failure) {
    FailurePath* failure;
    if (!addFailurePath(&failure)) {
      return false;
    }

    masm.spectreBoundsCheck32(index, Address(str, JSString::offsetOfLength()),
                              scratch3, failure->label());
    masm.loadStringChar(str, index, scratch2, scratch1, scratch3,
                        failure->label());

    allocator.discardStack(masm);
  } else {
    // Discard the stack before jumping to |done|.
    allocator.discardStack(masm);

    if (outOfBounds == StringCharOutOfBounds::EmptyString) {
      // Return the empty string for out-of-bounds access.
      masm.movePtr(ImmGCPtr(cx_->names().empty_), scratch1);
    } else {
      // Return |undefined| for out-of-bounds access.
      masm.moveValue(UndefinedValue(), output.valueReg());
    }

    // This CacheIR op is always preceded by |LinearizeForCharAccess|, so we're
    // guaranteed to see no nested ropes.
    masm.spectreBoundsCheck32(index, Address(str, JSString::offsetOfLength()),
                              scratch3, &done);
    masm.loadStringChar(str, index, scratch2, scratch1, scratch3, &loadFailed);
  }

  // Load StaticString for this char. For larger code units perform a VM call.
  Label vmCall;
  masm.lookupStaticString(scratch2, scratch1, cx_->staticStrings(), &vmCall);
  masm.jump(&tagResult);

  if (outOfBounds != StringCharOutOfBounds::Failure) {
    masm.bind(&loadFailed);
    masm.assumeUnreachable("loadStringChar can't fail for linear strings");
  }

  {
    masm.bind(&vmCall);

    AutoStubFrame stubFrame(*this);
    stubFrame.enter(masm, scratch3);

    masm.Push(scratch2);

    using Fn = JSLinearString* (*)(JSContext*, int32_t);
    callVM<Fn, js::StringFromCharCode>(masm);

    stubFrame.leave(masm);

    masm.storeCallPointerResult(scratch1);
  }

  if (outOfBounds != StringCharOutOfBounds::UndefinedValue) {
    masm.bind(&tagResult);
    masm.bind(&done);
    masm.tagValue(JSVAL_TYPE_STRING, scratch1, output.valueReg());
  } else {
    masm.bind(&tagResult);
    masm.tagValue(JSVAL_TYPE_STRING, scratch1, output.valueReg());
    masm.bind(&done);
  }
  return true;
}

bool BaselineCacheIRCompiler::emitLoadStringCharResult(StringOperandId strId,
                                                       Int32OperandId indexId,
                                                       bool handleOOB) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  auto outOfBounds = handleOOB ? StringCharOutOfBounds::EmptyString
                               : StringCharOutOfBounds::Failure;
  return emitLoadStringCharResult(strId, indexId, outOfBounds);
}

bool BaselineCacheIRCompiler::emitLoadStringAtResult(StringOperandId strId,
                                                     Int32OperandId indexId,
                                                     bool handleOOB) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  auto outOfBounds = handleOOB ? StringCharOutOfBounds::UndefinedValue
                               : StringCharOutOfBounds::Failure;
  return emitLoadStringCharResult(strId, indexId, outOfBounds);
}

bool BaselineCacheIRCompiler::emitStringFromCodeResult(Int32OperandId codeId,
                                                       StringCode stringCode) {
  AutoOutputRegister output(*this);
  AutoScratchRegisterMaybeOutput scratch(allocator, masm, output);

  Register code = allocator.useRegister(masm, codeId);

  FailurePath* failure = nullptr;
  if (stringCode == StringCode::CodePoint) {
    if (!addFailurePath(&failure)) {
      return false;
    }
  }

  if (stringCode == StringCode::CodePoint) {
    // Note: This condition must match tryAttachStringFromCodePoint to prevent
    // failure loops.
    masm.branch32(Assembler::Above, code, Imm32(unicode::NonBMPMax),
                  failure->label());
  }

  allocator.discardStack(masm);

  // We pre-allocate atoms for the first UNIT_STATIC_LIMIT characters.
  // For code units larger than that, we must do a VM call.
  Label vmCall;
  masm.lookupStaticString(code, scratch, cx_->staticStrings(), &vmCall);

  Label done;
  masm.jump(&done);

  {
    masm.bind(&vmCall);

    AutoStubFrame stubFrame(*this);
    stubFrame.enter(masm, scratch);

    masm.Push(code);

    if (stringCode == StringCode::CodeUnit) {
      using Fn = JSLinearString* (*)(JSContext*, int32_t);
      callVM<Fn, js::StringFromCharCode>(masm);
    } else {
      using Fn = JSLinearString* (*)(JSContext*, char32_t);
      callVM<Fn, js::StringFromCodePoint>(masm);
    }

    stubFrame.leave(masm);
    masm.storeCallPointerResult(scratch);
  }

  masm.bind(&done);
  masm.tagValue(JSVAL_TYPE_STRING, scratch, output.valueReg());
  return true;
}

bool BaselineCacheIRCompiler::emitStringFromCharCodeResult(
    Int32OperandId codeId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  return emitStringFromCodeResult(codeId, StringCode::CodeUnit);
}

bool BaselineCacheIRCompiler::emitStringFromCodePointResult(
    Int32OperandId codeId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  return emitStringFromCodeResult(codeId, StringCode::CodePoint);
}

bool BaselineCacheIRCompiler::emitReflectGetPrototypeOfResult(
    ObjOperandId objId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  AutoScratchRegisterMaybeOutput scratch(allocator, masm, output);

  Register obj = allocator.useRegister(masm, objId);

  allocator.discardStack(masm);

  MOZ_ASSERT(uintptr_t(TaggedProto::LazyProto) == 1);

  masm.loadObjProto(obj, scratch);

  Label hasProto;
  masm.branchPtr(Assembler::Above, scratch, ImmWord(1), &hasProto);

  // Call into the VM for lazy prototypes.
  Label slow, done;
  masm.branchPtr(Assembler::Equal, scratch, ImmWord(1), &slow);

  masm.moveValue(NullValue(), output.valueReg());
  masm.jump(&done);

  masm.bind(&hasProto);
  masm.tagValue(JSVAL_TYPE_OBJECT, scratch, output.valueReg());
  masm.jump(&done);

  {
    masm.bind(&slow);

    AutoStubFrame stubFrame(*this);
    stubFrame.enter(masm, scratch);

    masm.Push(obj);

    using Fn = bool (*)(JSContext*, HandleObject, MutableHandleValue);
    callVM<Fn, jit::GetPrototypeOf>(masm);

    stubFrame.leave(masm);
  }

  masm.bind(&done);
  return true;
}

bool BaselineCacheIRCompiler::emitHasClassResult(ObjOperandId objId,
                                                 uint32_t claspOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  Register obj = allocator.useRegister(masm, objId);
  AutoScratchRegisterMaybeOutput scratch(allocator, masm, output);

  Address claspAddr(stubAddress(claspOffset));
  masm.loadObjClassUnsafe(obj, scratch);
  masm.cmpPtrSet(Assembler::Equal, claspAddr, scratch.get(), scratch);
  masm.tagValue(JSVAL_TYPE_BOOLEAN, scratch, output.valueReg());
  return true;
}

bool BaselineCacheIRCompiler::emitHasShapeResult(ObjOperandId objId,
                                                 uint32_t shapeOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  Register obj = allocator.useRegister(masm, objId);
  AutoScratchRegisterMaybeOutput scratch(allocator, masm, output);

  // Note: no Spectre mitigations are needed here because this shape check only
  // affects correctness.
  Address shapeAddr(stubAddress(shapeOffset));
  masm.loadObjShapeUnsafe(obj, scratch);
  masm.cmpPtrSet(Assembler::Equal, shapeAddr, scratch.get(), scratch);
  masm.tagValue(JSVAL_TYPE_BOOLEAN, scratch, output.valueReg());
  return true;
}

void BaselineCacheIRCompiler::emitAtomizeString(Register str, Register temp,
                                                Label* failure) {
  Label isAtom, notCachedAtom;
  masm.branchTest32(Assembler::NonZero, Address(str, JSString::offsetOfFlags()),
                    Imm32(StringFlags::ATOM_BIT), &isAtom);
  masm.tryFastAtomize(str, temp, str, &notCachedAtom);
  masm.jump(&isAtom);
  masm.bind(&notCachedAtom);

  {
    LiveRegisterSet save = liveVolatileRegs();
    masm.PushRegsInMask(save);

    using Fn = JSAtom* (*)(JSContext * cx, JSString * str);
    masm.setupUnalignedABICall(temp);
    masm.loadJSContext(temp);
    masm.passABIArg(temp);
    masm.passABIArg(str);
    masm.callWithABI<Fn, jit::AtomizeStringNoGC>();
    masm.storeCallPointerResult(temp);

    LiveRegisterSet ignore;
    ignore.add(temp);
    masm.PopRegsInMaskIgnore(save, ignore);

    masm.branchPtr(Assembler::Equal, temp, ImmWord(0), failure);
    masm.mov(temp, str);
  }
  masm.bind(&isAtom);
}

bool BaselineCacheIRCompiler::emitSetHasStringResult(ObjOperandId setId,
                                                     StringOperandId strId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  Register set = allocator.useRegister(masm, setId);
  Register str = allocator.useRegister(masm, strId);

  AutoScratchRegister scratch1(allocator, masm);
  AutoScratchRegister scratch2(allocator, masm);
  AutoScratchRegister scratch3(allocator, masm);
  AutoScratchRegister scratch4(allocator, masm);

  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  emitAtomizeString(str, scratch1, failure->label());
  masm.prepareHashString(str, scratch1, scratch2);

  masm.tagValue(JSVAL_TYPE_STRING, str, output.valueReg());
  masm.setObjectHasNonBigInt(set, output.valueReg(), scratch1, scratch2,
                             scratch3, scratch4);
  masm.tagValue(JSVAL_TYPE_BOOLEAN, scratch2, output.valueReg());
  return true;
}

bool BaselineCacheIRCompiler::emitMapHasStringResult(ObjOperandId mapId,
                                                     StringOperandId strId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  Register map = allocator.useRegister(masm, mapId);
  Register str = allocator.useRegister(masm, strId);

  AutoScratchRegister scratch1(allocator, masm);
  AutoScratchRegister scratch2(allocator, masm);
  AutoScratchRegister scratch3(allocator, masm);
  AutoScratchRegister scratch4(allocator, masm);

  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  emitAtomizeString(str, scratch1, failure->label());
  masm.prepareHashString(str, scratch1, scratch2);

  masm.tagValue(JSVAL_TYPE_STRING, str, output.valueReg());
  masm.mapObjectHasNonBigInt(map, output.valueReg(), scratch1, scratch2,
                             scratch3, scratch4);
  masm.tagValue(JSVAL_TYPE_BOOLEAN, scratch2, output.valueReg());
  return true;
}

bool BaselineCacheIRCompiler::emitMapGetStringResult(ObjOperandId mapId,
                                                     StringOperandId strId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  Register map = allocator.useRegister(masm, mapId);
  Register str = allocator.useRegister(masm, strId);

  AutoScratchRegister scratch1(allocator, masm);
  AutoScratchRegister scratch2(allocator, masm);
  AutoScratchRegister scratch3(allocator, masm);
  AutoScratchRegister scratch4(allocator, masm);

  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  emitAtomizeString(str, scratch1, failure->label());
  masm.prepareHashString(str, scratch1, scratch2);

  masm.tagValue(JSVAL_TYPE_STRING, str, output.valueReg());
  masm.mapObjectGetNonBigInt(map, output.valueReg(), scratch1,
                             output.valueReg(), scratch2, scratch3, scratch4);
  return true;
}

bool BaselineCacheIRCompiler::emitCallNativeSetter(
    ObjOperandId receiverId, uint32_t setterOffset, ValOperandId rhsId,
    bool sameRealm, uint32_t nargsAndFlagsOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register receiver = allocator.useRegister(masm, receiverId);
  Address setterAddr(stubAddress(setterOffset));
  ValueOperand val = allocator.useValueRegister(masm, rhsId);

  AutoScratchRegister scratch(allocator, masm);

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  // Load the callee in the scratch register.
  masm.loadPtr(setterAddr, scratch);

  masm.Push(val);
  masm.PushRegs(receiver, scratch);

  using Fn = bool (*)(JSContext*, HandleFunction, HandleObject, HandleValue);
  callVM<Fn, CallNativeSetter>(masm);

  stubFrame.leave(masm);
  return true;
}

bool BaselineCacheIRCompiler::emitCallScriptedSetterShared(
    ObjOperandId receiverId, ObjOperandId calleeId, ValOperandId rhsId,
    bool sameRealm, uint32_t nargsAndFlagsOffset,
    Maybe<uint32_t> icScriptOffset) {
  AutoScratchRegister scratch(allocator, masm);
#if defined(JS_CODEGEN_X86)
  Register code = scratch;
#else
  AutoScratchRegister code(allocator, masm);
#endif

  Register receiver = allocator.useRegister(masm, receiverId);
  Register callee = allocator.useRegister(masm, calleeId);
  ValueOperand val = allocator.useValueRegister(masm, rhsId);

  bool isInlined = icScriptOffset.isSome();

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  if (!sameRealm) {
    masm.switchToObjectRealm(callee, scratch);
  }

  if (isInlined) {
    stubFrame.pushInlinedICScript(masm, stubAddress(*icScriptOffset));
  }

  Label noUnderflow, doneAlignment;
  masm.loadFunctionArgCount(callee, scratch);
  masm.branch32(Assembler::BelowOrEqual, scratch, Imm32(1), &noUnderflow);

  masm.alignJitStackBasedOnNArgs(scratch, /*countIncludesThis*/ false);

  Label loop;
  masm.bind(&loop);
  masm.Push(UndefinedValue());
  masm.sub32(Imm32(1), scratch);
  masm.branch32(Assembler::Above, scratch, Imm32(1), &loop);
  masm.jump(&doneAlignment);

  // Align the stack such that the JitFrameLayout is aligned on
  // JitStackAlignment.
  masm.bind(&noUnderflow);
  masm.alignJitStackBasedOnNArgs(1, /*countIncludesThis = */ false);
  masm.bind(&doneAlignment);

  // Setter is called with 1 argument, and |receiver| as thisv. Note that we use
  // Push, not push, so that callJit will align the stack properly on ARM.
  masm.Push(val);
  masm.Push(TypedOrValueRegister(MIRType::Object, AnyRegister(receiver)));

  // Push callee.
  masm.Push(callee);

  // Push frame descriptor.
  masm.Push(
      FrameDescriptor(FrameType::BaselineStub, /* argc = */ 1, isInlined));

  // Load the jitcode pointer.
  Register scratch2 = val.scratchReg();
  if (isInlined) {
    masm.loadJitCodeRawNoIon(callee, code, scratch2);
  } else {
    masm.loadJitCodeRaw(callee, code);
  }

  masm.callJit(code);

  stubFrame.leave(masm);

  if (!sameRealm) {
    masm.switchToBaselineFrameRealm(R1.scratchReg());
  }

  return true;
}

bool BaselineCacheIRCompiler::emitCallScriptedSetter(
    ObjOperandId receiverId, ObjOperandId calleeId, ValOperandId rhsId,
    bool sameRealm, uint32_t nargsAndFlagsOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Maybe<uint32_t> icScriptOffset = mozilla::Nothing();
  return emitCallScriptedSetterShared(receiverId, calleeId, rhsId, sameRealm,
                                      nargsAndFlagsOffset, icScriptOffset);
}

bool BaselineCacheIRCompiler::emitCallInlinedSetter(
    ObjOperandId receiverId, ObjOperandId calleeId, ValOperandId rhsId,
    uint32_t icScriptOffset, bool sameRealm, uint32_t nargsAndFlagsOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  return emitCallScriptedSetterShared(receiverId, calleeId, rhsId, sameRealm,
                                      nargsAndFlagsOffset,
                                      mozilla::Some(icScriptOffset));
}

bool BaselineCacheIRCompiler::emitCallDOMSetter(ObjOperandId objId,
                                                uint32_t jitInfoOffset,
                                                ValOperandId rhsId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register obj = allocator.useRegister(masm, objId);
  ValueOperand val = allocator.useValueRegister(masm, rhsId);
  Address jitInfoAddr(stubAddress(jitInfoOffset));

  AutoScratchRegister scratch(allocator, masm);

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  // Load the JSJitInfo in the scratch register.
  masm.loadPtr(jitInfoAddr, scratch);

  masm.Push(val);
  masm.PushRegs(obj, scratch);

  using Fn = bool (*)(JSContext*, const JSJitInfo*, HandleObject, HandleValue);
  callVM<Fn, CallDOMSetter>(masm);

  stubFrame.leave(masm);
  return true;
}

bool BaselineCacheIRCompiler::emitCallSetArrayLength(ObjOperandId objId,
                                                     bool strict,
                                                     ValOperandId rhsId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register obj = allocator.useRegister(masm, objId);
  ValueOperand val = allocator.useValueRegister(masm, rhsId);

  AutoScratchRegister scratch(allocator, masm);

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  masm.Push(Imm32(strict));
  masm.Push(val);
  masm.Push(obj);

  using Fn = bool (*)(JSContext*, HandleObject, HandleValue, bool);
  callVM<Fn, jit::SetArrayLength>(masm);

  stubFrame.leave(masm);
  return true;
}

bool BaselineCacheIRCompiler::emitProxySet(ObjOperandId objId,
                                           uint32_t idOffset,
                                           ValOperandId rhsId, bool strict) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register obj = allocator.useRegister(masm, objId);
  ValueOperand val = allocator.useValueRegister(masm, rhsId);
  Address idAddr(stubAddress(idOffset));

  AutoScratchRegister scratch(allocator, masm);

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  // Load the jsid in the scratch register.
  masm.loadPtr(idAddr, scratch);

  masm.Push(Imm32(strict));
  masm.Push(val);
  masm.PushRegs(scratch, obj);

  using Fn = bool (*)(JSContext*, HandleObject, HandleId, HandleValue, bool);
  callVM<Fn, ProxySetProperty>(masm);

  stubFrame.leave(masm);
  return true;
}

bool BaselineCacheIRCompiler::emitProxySetByValue(ObjOperandId objId,
                                                  ValOperandId idId,
                                                  ValOperandId rhsId,
                                                  bool strict) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register obj = allocator.useRegister(masm, objId);
  ValueOperand idVal = allocator.useValueRegister(masm, idId);
  ValueOperand val = allocator.useValueRegister(masm, rhsId);

  allocator.discardStack(masm);

  // We need a scratch register but we don't have any registers available on
  // x86, so temporarily store |obj| in the frame's scratch slot.
  int scratchOffset = BaselineFrame::reverseOffsetOfScratchValue();
  masm.storePtr(obj, Address(baselineFrameReg(), scratchOffset));

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, obj);

  // Restore |obj|. Because we entered a stub frame we first have to load
  // the original frame pointer.
  masm.loadPtr(Address(FramePointer, 0), obj);
  masm.loadPtr(Address(obj, scratchOffset), obj);

  masm.Push(Imm32(strict));
  masm.Push(val);
  masm.Push(idVal);
  masm.Push(obj);

  using Fn = bool (*)(JSContext*, HandleObject, HandleValue, HandleValue, bool);
  callVM<Fn, ProxySetPropertyByValue>(masm);

  stubFrame.leave(masm);
  return true;
}

bool BaselineCacheIRCompiler::emitCallAddOrUpdateSparseElementHelper(
    ObjOperandId objId, Int32OperandId idId, ValOperandId rhsId, bool strict) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register obj = allocator.useRegister(masm, objId);
  Register id = allocator.useRegister(masm, idId);
  ValueOperand val = allocator.useValueRegister(masm, rhsId);
  AutoScratchRegister scratch(allocator, masm);

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  masm.Push(Imm32(strict));
  masm.Push(val);
  masm.PushRegs(id, obj);

  using Fn = bool (*)(JSContext* cx, Handle<NativeObject*> obj, int32_t int_id,
                      HandleValue v, bool strict);
  callVM<Fn, AddOrUpdateSparseElementHelper>(masm);

  stubFrame.leave(masm);
  return true;
}

bool BaselineCacheIRCompiler::emitMegamorphicSetElement(ObjOperandId objId,
                                                        ValOperandId idId,
                                                        ValOperandId rhsId,
                                                        bool strict) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register obj = allocator.useRegister(masm, objId);
  ValueOperand idVal = allocator.useValueRegister(masm, idId);
  ValueOperand val = allocator.useValueRegister(masm, rhsId);

#ifdef JS_CODEGEN_X86
  allocator.discardStack(masm);
  // We need a scratch register but we don't have any registers available on
  // x86, so temporarily store |obj| in the frame's scratch slot.
  int scratchOffset = BaselineFrame::reverseOffsetOfScratchValue();
  masm.storePtr(obj, Address(baselineFrameReg_, scratchOffset));

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, obj);

  // Restore |obj|. Because we entered a stub frame we first have to load
  // the original frame pointer.
  masm.loadPtr(Address(FramePointer, 0), obj);
  masm.loadPtr(Address(obj, scratchOffset), obj);
#else
  AutoScratchRegister scratch(allocator, masm);

  allocator.discardStack(masm);
  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);
#endif

  masm.Push(Imm32(strict));
  masm.Push(val);
  masm.Push(idVal);
  masm.Push(obj);

  using Fn = bool (*)(JSContext*, HandleObject, HandleValue, HandleValue, bool);
  callVM<Fn, SetElementMegamorphic<false>>(masm);

  stubFrame.leave(masm);
  return true;
}

bool BaselineCacheIRCompiler::emitLoadArgumentFixedSlot(ValOperandId resultId,
                                                        uint8_t slotIndex) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  ValueOperand resultReg = allocator.defineValueRegister(masm, resultId);
  Address addr = allocator.addressOf(masm, BaselineFrameSlot(slotIndex));
  masm.loadValue(addr, resultReg);
  return true;
}

bool BaselineCacheIRCompiler::emitLoadArgumentDynamicSlot(ValOperandId resultId,
                                                          Int32OperandId argcId,
                                                          uint8_t slotIndex) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  ValueOperand resultReg = allocator.defineValueRegister(masm, resultId);
  Register argcReg = allocator.useRegister(masm, argcId);
  BaseValueIndex addr =
      allocator.addressOf(masm, argcReg, BaselineFrameSlot(slotIndex));
  masm.loadValue(addr, resultReg);
  return true;
}

bool BaselineCacheIRCompiler::emitGuardDOMExpandoMissingOrGuardShape(
    ValOperandId expandoId, uint32_t shapeOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  ValueOperand val = allocator.useValueRegister(masm, expandoId);
  AutoScratchRegister shapeScratch(allocator, masm);
  AutoScratchRegister objScratch(allocator, masm);
  Address shapeAddr(stubAddress(shapeOffset));

  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  Label done;
  masm.branchTestUndefined(Assembler::Equal, val, &done);

  masm.debugAssertIsObject(val);
  masm.loadPtr(shapeAddr, shapeScratch);
  masm.unboxObject(val, objScratch);
  // The expando object is not used in this case, so we don't need Spectre
  // mitigations.
  masm.branchTestObjShapeNoSpectreMitigations(Assembler::NotEqual, objScratch,
                                              shapeScratch, failure->label());

  masm.bind(&done);
  return true;
}

bool BaselineCacheIRCompiler::emitLoadDOMExpandoValueGuardGeneration(
    ObjOperandId objId, uint32_t expandoAndGenerationOffset,
    uint32_t generationOffset, ValOperandId resultId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register obj = allocator.useRegister(masm, objId);
  Address expandoAndGenerationAddr(stubAddress(expandoAndGenerationOffset));
  Address generationAddr(stubAddress(generationOffset));

  AutoScratchRegister scratch(allocator, masm);
  ValueOperand output = allocator.defineValueRegister(masm, resultId);

  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  Address expandoAddr(obj, ProxyObject::offsetOfPrivateSlot());

  // Load the ExpandoAndGeneration* in the output scratch register and guard
  // it matches the proxy's ExpandoAndGeneration.
  masm.loadPtr(expandoAndGenerationAddr, output.scratchReg());
  masm.branchPrivatePtr(Assembler::NotEqual, expandoAddr, output.scratchReg(),
                        failure->label());

  // Guard expandoAndGeneration->generation matches the expected generation.
  masm.branch64(
      Assembler::NotEqual,
      Address(output.scratchReg(), ExpandoAndGeneration::offsetOfGeneration()),
      generationAddr, scratch, failure->label());

  // Load expandoAndGeneration->expando into the output Value register.
  masm.loadValue(
      Address(output.scratchReg(), ExpandoAndGeneration::offsetOfExpando()),
      output);
  return true;
}

bool BaselineCacheIRCompiler::init(CacheKind kind) {
  if (!allocator.init()) {
    return false;
  }

  size_t numInputs = writer_.numInputOperands();
  MOZ_ASSERT(numInputs == NumInputsForCacheKind(kind));

  // Baseline passes the first 2 inputs in R0/R1, other Values are stored on
  // the stack.
  size_t numInputsInRegs = std::min(numInputs, size_t(2));
  AllocatableGeneralRegisterSet available =
      BaselineICAvailableGeneralRegs(numInputsInRegs);

  switch (kind) {
    case CacheKind::NewArray:
    case CacheKind::NewObject:
    case CacheKind::Lambda:
    case CacheKind::LazyConstant:
    case CacheKind::GetImport:
      MOZ_ASSERT(numInputs == 0);
      outputUnchecked_.emplace(R0);
      break;
    case CacheKind::GetProp:
    case CacheKind::TypeOf:
    case CacheKind::TypeOfEq:
    case CacheKind::ToPropertyKey:
    case CacheKind::GetIterator:
    case CacheKind::OptimizeSpreadCall:
    case CacheKind::OptimizeGetIterator:
    case CacheKind::ToBool:
    case CacheKind::UnaryArith:
      MOZ_ASSERT(numInputs == 1);
      allocator.initInputLocation(0, R0);
      outputUnchecked_.emplace(R0);
      break;
    case CacheKind::Compare:
    case CacheKind::GetElem:
    case CacheKind::GetPropSuper:
    case CacheKind::In:
    case CacheKind::HasOwn:
    case CacheKind::CheckPrivateField:
    case CacheKind::InstanceOf:
    case CacheKind::BinaryArith:
      MOZ_ASSERT(numInputs == 2);
      allocator.initInputLocation(0, R0);
      allocator.initInputLocation(1, R1);
      outputUnchecked_.emplace(R0);
      break;
    case CacheKind::SetProp:
      MOZ_ASSERT(numInputs == 2);
      allocator.initInputLocation(0, R0);
      allocator.initInputLocation(1, R1);
      break;
    case CacheKind::GetElemSuper:
      MOZ_ASSERT(numInputs == 3);
      allocator.initInputLocation(0, BaselineFrameSlot(0));
      allocator.initInputLocation(1, R1);
      allocator.initInputLocation(2, R0);
      outputUnchecked_.emplace(R0);
      break;
    case CacheKind::SetElem:
      MOZ_ASSERT(numInputs == 3);
      allocator.initInputLocation(0, R0);
      allocator.initInputLocation(1, R1);
      allocator.initInputLocation(2, BaselineFrameSlot(0));
      break;
    case CacheKind::GetName:
    case CacheKind::BindName:
      MOZ_ASSERT(numInputs == 1);
      allocator.initInputLocation(0, R0.scratchReg(), JSVAL_TYPE_OBJECT);
#if defined(JS_NUNBOX32)
      // availableGeneralRegs can't know that GetName/BindName is only using
      // the payloadReg and not typeReg on x86.
      available.add(R0.typeReg());
#endif
      outputUnchecked_.emplace(R0);
      break;
    case CacheKind::Call:
      MOZ_ASSERT(numInputs == 1);
      allocator.initInputLocation(0, R0.scratchReg(), JSVAL_TYPE_INT32);
#if defined(JS_NUNBOX32)
      // availableGeneralRegs can't know that Call is only using
      // the payloadReg and not typeReg on x86.
      available.add(R0.typeReg());
#endif
      outputUnchecked_.emplace(R0);
      break;
    case CacheKind::CloseIter:
      MOZ_ASSERT(numInputs == 1);
      allocator.initInputLocation(0, R0.scratchReg(), JSVAL_TYPE_OBJECT);
#if defined(JS_NUNBOX32)
      // availableGeneralRegs can't know that CloseIter is only using
      // the payloadReg and not typeReg on x86.
      available.add(R0.typeReg());
#endif
      break;
  }

  // Baseline doesn't allocate float registers so none of them are live.
  liveFloatRegs_ = LiveFloatRegisterSet(FloatRegisterSet());

  if (JitOptions.enableICFramePointers) {
    baselineFrameReg_ = available.takeAny();
  }

  allocator.initAvailableRegs(available);
  return true;
}

static void ResetEnteredCounts(const ICEntry* icEntry) {
  ICStub* stub = icEntry->firstStub();
  while (true) {
    stub->resetEnteredCount();
    if (stub->isFallback()) {
      return;
    }
    stub = stub->toCacheIRStub()->next();
  }
}

#ifdef ENABLE_JS_AOT_ICS
void DumpNonAOTICStubAndQuit(CacheKind kind, const CacheIRWriter& writer) {
  // Generate a random filename (unlikely to conflict with others).
  char filename[64];
  snprintf(filename, sizeof(filename), "IC-%" PRIu64,
           mozilla::RandomUint64OrDie());
  FILE* f = fopen(filename, "w");
  MOZ_RELEASE_ASSERT(f);

  // Generate the CacheIR text to dump to a file.
  {
    Fprinter printer(f);
    SpewCacheIROpsAsAOT(printer, kind, writer);
  }
  fflush(f);
  fclose(f);
  fprintf(stderr, "UNEXPECTED NEW IC BODY\n");

  fprintf(stderr,
          "Please add the file '%s' to the ahead-of-time known IC bodies in "
          "js/src/ics/.\n"
          "\n"
          "To keep running and dump all new ICs (useful for updating with "
          "test-suites),\n"
          "set the environment variable AOT_ICS_KEEP_GOING=1 and rerun.\n",
          filename);

  if (!getenv("AOT_ICS_KEEP_GOING")) {
    abort();
  }
}
#endif

static constexpr uint32_t StubDataOffset = sizeof(ICCacheIRStub);
static_assert(StubDataOffset % sizeof(uint64_t) == 0,
              "Stub fields must be aligned");

static bool LookupOrCompileStub(JSContext* cx, CacheKind kind,
                                const CacheIRWriter& writer,
                                CacheIRStubInfo*& stubInfo, JitCode*& code,
                                const char* name, bool isAOTFill,
                                JitZone* jitZone) {
  CacheIRStubKey::Lookup lookup(kind, ICStubEngine::Baseline,
                                writer.codeStart(), writer.codeLength());

  code = jitZone->getBaselineCacheIRStubCode(lookup, &stubInfo);

#ifdef ENABLE_JS_AOT_ICS
  if (JitOptions.enableAOTICEnforce && !stubInfo && !isAOTFill &&
      !jitZone->isIncompleteAOTICs()) {
    DumpNonAOTICStubAndQuit(kind, writer);
  }
#endif

  if (!code && !IsPortableBaselineInterpreterEnabled()) {
    // We have to generate stub code.
    TempAllocator temp(&cx->tempLifoAlloc());
    JitContext jctx(cx);
    BaselineCacheIRCompiler comp(cx, temp, writer, StubDataOffset);
    if (!comp.init(kind)) {
      return false;
    }

    code = comp.compile();
    if (!code) {
      return false;
    }

    comp.perfSpewer().saveProfile(code, name);

    // Allocate the shared CacheIRStubInfo. Note that the
    // putBaselineCacheIRStubCode call below will transfer ownership
    // to the stub code HashMap, so we don't have to worry about freeing
    // it below.
    MOZ_ASSERT(!stubInfo);
    stubInfo =
        CacheIRStubInfo::New(kind, ICStubEngine::Baseline, comp.makesGCCalls(),
                             StubDataOffset, writer);
    if (!stubInfo) {
      return false;
    }

    CacheIRStubKey key(stubInfo);
    if (!jitZone->putBaselineCacheIRStubCode(lookup, key, code)) {
      return false;
    }
  } else if (!stubInfo) {
    MOZ_ASSERT(IsPortableBaselineInterpreterEnabled());

    // Portable baseline interpreter case. We want to generate the
    // CacheIR bytecode but not compile it to native code.
    //
    // We lie that all stubs make GC calls; this is simpler than
    // iterating over ops to determine if it is actually the base, and
    // we don't invoke the BaselineCacheIRCompiler so we otherwise
    // don't know for sure.
    stubInfo = CacheIRStubInfo::New(kind, ICStubEngine::Baseline,
                                    /* makes GC calls = */ true, StubDataOffset,
                                    writer);
    if (!stubInfo) {
      return false;
    }

    CacheIRStubKey key(stubInfo);
    if (!jitZone->putBaselineCacheIRStubCode(lookup, key,
                                             /* stubCode = */ nullptr)) {
      return false;
    }
  }
  MOZ_ASSERT_IF(IsBaselineInterpreterEnabled(), code);
  MOZ_ASSERT(stubInfo);
  // Assert that the StubInfo recomputing its stub-data size exactly
  // matches the writer's stub-data size, but only if we're not
  // loading an AOT IC -- otherwise, trust the recomputation from
  // field types.
  //
  // Why ignore if AOT? Because the AOT corpus might have been dumped
  // on a machine with a different word size than our machine (e.g.,
  // 64 to 32 bits). The field types are serialized and deserialized,
  // and they are authoritative; the CacheIRWriter's stubDataSize is
  // computed during build and used only for this assert, so it is
  // strictly a redundant check.
  //
  // (This cross-machine movement of the corpus is acceptable/correct
  // because the CacheIR itself, and our encoding of it in the corpus
  // source code, is platform-independent. The worst that happens is
  // that some platforms may not generate all possible ICs for another
  // platform (e.g. due to limited registers on x86-32) but it is always
  // fine not to have an IC preloaded in the corpus.
  MOZ_ASSERT_IF(!isAOTFill, stubInfo->stubDataSize() == writer.stubDataSize());

  return true;
}

static ICAttachResult CompileBaselineCacheIRStub(
    JSContext* cx, const CacheIRWriter& writer, CacheKind kind,
    const char* name, CacheIRStubInfo** stubInfo, JitCode** codeOut) {
  // We shouldn't GC or report OOM (or any other exception) here.
  AutoAssertNoPendingException aanpe(cx);
  JS::AutoCheckCannotGC nogc;

  if (writer.tooLarge()) {
    cx->runtime()->setUseCounter(cx->global(), JSUseCounter::IC_STUB_TOO_LARGE);
    return ICAttachResult::TooLarge;
  }
  if (writer.oom()) {
    cx->runtime()->setUseCounter(cx->global(), JSUseCounter::IC_STUB_OOM);
    return ICAttachResult::OOM;
  }
  MOZ_ASSERT(!writer.failed());

  // Check if we already have JitCode for this stub.
  if (!LookupOrCompileStub(cx, kind, writer, *stubInfo, *codeOut, name,
                           /* isAOTFill = */ false, cx->zone()->jitZone())) {
    return ICAttachResult::OOM;
  }

  return ICAttachResult::Attached;
}

ICAttachResult js::jit::AttachBaselineCacheIRStub(
    JSContext* cx, const CacheIRWriter& writer, CacheKind kind,
    JSScript* outerScript, ICScript* icScript, ICFallbackStub* stub,
    const char* name) {
  MaybeMarkingLock lock;
  return AttachBaselineCacheIRStubLocked(cx, writer, kind, outerScript,
                                         icScript, stub,
                                         DiscardExistingStubs::No, name, lock);
}

ICAttachResult js::jit::AttachBaselineCacheIRStubLocked(
    JSContext* cx, const CacheIRWriter& writer, CacheKind kind,
    JSScript* outerScript, ICScript* icScript, ICFallbackStub* stub,
    DiscardExistingStubs discardFallbackStubs, const char* name,
    MaybeMarkingLock& lock) {
  // Just a sanity check: the caller should ensure we don't attach an
  // unlimited number of stubs.
#ifdef DEBUG
  static const size_t MaxOptimizedCacheIRStubs = 16;
  MOZ_ASSERT(stub->numOptimizedStubs() < MaxOptimizedCacheIRStubs);
#endif

  CacheIRStubInfo* stubInfo;
  JitCode* code = nullptr;
  ICAttachResult result =
      CompileBaselineCacheIRStub(cx, writer, kind, name, &stubInfo, &code);
  if (result != ICAttachResult::Attached) {
    return result;
  }

  // We shouldn't GC or report OOM (or any other exception) here.
  AutoAssertNoPendingException aanpe(cx);
  JS::AutoCheckCannotGC nogc;

  ICEntry* icEntry = icScript->icEntryForStub(stub);

  if (discardFallbackStubs == DiscardExistingStubs::No) {
    // Ensure we don't attach duplicate stubs. This can happen if a stub failed
    // for some reason and the IR generator doesn't check for exactly the same
    // conditions.
    for (ICStub* iter = icEntry->firstStub(); iter != stub;
         iter = iter->toCacheIRStub()->next()) {
      auto otherStub = iter->toCacheIRStub();
      if (otherStub->stubInfo() != stubInfo) {
        continue;
      }
      if (!writer.stubDataEquals(otherStub->stubDataStart())) {
        continue;
      }

      // We found a stub that's exactly the same as the stub we're about to
      // attach. Just return nullptr, the caller should do nothing in this
      // case.
      JitSpew(JitSpew_BaselineICFallback,
              "Tried attaching identical stub for (%s:%u:%u)",
              outerScript->filename(), outerScript->lineno(),
              outerScript->column().oneOriginValue());
      return ICAttachResult::DuplicateStub;
    }

    // Try including this case in an existing folded stub.
    if (stub->mayHaveFoldedStub() &&
        AddToFoldedStub(cx, writer, icScript, stub)) {
      JitSpew(JitSpew_StubFolding,
              "Added to folded stub at offset %u (icScript: %p) (%s:%u:%u)",
              stub->pcOffset(), icScript, outerScript->filename(),
              outerScript->lineno(), outerScript->column().oneOriginValue());

      // Instead of adding a new stub, we have added a new case to an existing
      // folded stub. For invalidating Warp code, there are two cases to
      // consider:
      //
      // (1) If we used MGuardShapeList, we need to invalidate Warp code because
      //     it bakes in the old shape list.
      //
      // (2) If we used MGuardMultipleShapes, we do not need to invalidate Warp,
      //     because the ShapeListObject that stores the cases is shared between
      //     Baseline and Warp.
      //
      // If we have stub folding bailout data stored in the JitZone for this
      // script, this must be case (2). In this case we reset the bailout
      // counter if we have already been transpiled.
      //
      // In both cases we reset the entered count for the fallback stub so that
      // we can still transpile.
      stub->resetEnteredCount();
      JSScript* owningScript = nullptr;
      bool hadGuardMultipleShapesBailout = false;
      if (cx->zone()->jitZone()->hasStubFoldingBailoutData(outerScript)) {
        owningScript = cx->zone()->jitZone()->stubFoldingBailoutOuter();
        hadGuardMultipleShapesBailout = true;
        JitSpew(JitSpew_StubFolding,
                "Found stub folding bailout outer: %s:%u:%u",
                owningScript->filename(), owningScript->lineno(),
                owningScript->column().oneOriginValue());
      } else {
        owningScript = icScript->isInlined()
                           ? icScript->inliningRoot()->owningScript()
                           : outerScript;
      }
      cx->zone()->jitZone()->clearStubFoldingBailoutData();
      if (stub->usedByTranspiler() && hadGuardMultipleShapesBailout) {
        if (owningScript->hasIonScript()) {
          owningScript->ionScript()->resetNumFixableBailouts();
        } else if (owningScript->hasJitScript()) {
          owningScript->jitScript()->clearFailedICHash();
        }
      } else {
        // Update the last IC counter if this is not a GuardMultipleShapes
        // bailout from Ion.
        owningScript->updateLastICStubCounter();
      }
      return ICAttachResult::Attached;
    }
  }

  // Time to allocate and attach a new stub.

  size_t bytesNeeded = stubInfo->stubDataOffset() + stubInfo->stubDataSize();

  void* newStubMem =
      outerScript->realm()->jitRealm().stubSpace()->alloc(bytesNeeded);
  if (!newStubMem) {
    return ICAttachResult::OOM;
  }

  auto newStub = new (newStubMem) ICCacheIRStub(code, stubInfo);
  writer.copyStubData(newStub->stubDataStart());
  newStub->setTypeData(writer.typeData());

#ifdef ENABLE_PORTABLE_BASELINE_INTERP
  newStub->updateRawJitCode(pbl::GetICInterpreter());
#endif

  if (!lock.isSome()) {
    lock.emplace(cx->zone(), icScript->markingLock());
  }

  if (discardFallbackStubs == DiscardExistingStubs::Yes) {
    stub->discardStubs(cx->zone(), icEntry, *lock);
  }

  // Resetting the entered counts on the IC chain makes subsequent reasoning
  // about the chain much easier.
  ResetEnteredCounts(icEntry);

  switch (stub->trialInliningState()) {
    case TrialInliningState::Initial:
    case TrialInliningState::Candidate:
      stub->setTrialInliningState(writer.trialInliningState());
      break;
    case TrialInliningState::MonomorphicInlined:
      stub->setTrialInliningState(TrialInliningState::Failure);
      break;
    case TrialInliningState::Inlined:
      stub->setTrialInliningState(TrialInliningState::Failure);
      // Ensure we stop using the callee's trial inlining ICScript.
      stub->discardStubs(cx->zone(), icEntry, *lock);
      icScript->removeInlinedChild(stub->pcOffset());
      break;
    case TrialInliningState::Failure:
      break;
  }

  stub->addNewStub(icEntry, newStub);

  JSScript* owningScript = icScript->isInlined()
                               ? icScript->inliningRoot()->owningScript()
                               : outerScript;
  owningScript->updateLastICStubCounter();
  return ICAttachResult::Attached;
}

#ifdef ENABLE_JS_AOT_ICS

#  ifndef ENABLE_PORTABLE_BASELINE_INTERP
// The AOT loading of ICs doesn't work (yet) in modes with a native
// JIT enabled because compilation tries to access state that doesn't
// exist yet (trampolines?) when we create the JitZone.
#    error AOT ICs are only supported (for now) in PBL builds.
#  endif

void js::jit::FillAOTICs(JSContext* cx, JitZone* zone) {
  if (JitOptions.enableAOTICs) {
    for (auto& stub : GetAOTStubs()) {
      CacheIRWriter writer(cx, stub);
      if (writer.failed()) {
        zone->setIncompleteAOTICs();
        break;
      }
      CacheIRStubInfo* stubInfo;
      JitCode* code;
      (void)LookupOrCompileStub(cx, stub.kind, writer, stubInfo, code,
                                "aot stub",
                                /* isAOTFill = */ true, zone);
      (void)stubInfo;
      (void)code;
    }
  }
}
#endif

uint8_t* ICCacheIRStub::stubDataStart() {
  return reinterpret_cast<uint8_t*>(this) + stubInfo_->stubDataOffset();
}

bool BaselineCacheIRCompiler::emitCallStringObjectConcatResult(
    ValOperandId lhsId, ValOperandId rhsId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  ValueOperand lhs = allocator.useValueRegister(masm, lhsId);
  ValueOperand rhs = allocator.useValueRegister(masm, rhsId);

  AutoScratchRegister scratch(allocator, masm);

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  masm.pushValue(rhs);
  masm.pushValue(lhs);

  using Fn = bool (*)(JSContext*, HandleValue, HandleValue, MutableHandleValue);
  callVM<Fn, DoConcatStringObject>(masm);

  stubFrame.leave(masm);
  return true;
}

// The value of argc entering the call IC is not always the value of
// argc entering the callee. (For example, argc for a spread call IC
// is always 1, but argc for the callee is the length of the array.)
// In these cases, we update argc as part of the call op itself, to
// avoid modifying input operands while it is still possible to fail a
// guard. We also limit callee argc to a reasonable value to avoid
// blowing the stack limit.
bool BaselineCacheIRCompiler::updateArgc(CallFlags flags, Register argcReg,
                                         uint32_t argcFixed, Register scratch) {
  CallFlags::ArgFormat format = flags.getArgFormat();
  switch (format) {
    case CallFlags::Standard:
      // Standard calls have no extra guards, and argc is already correct.
      return true;
    case CallFlags::FunCall:
      // One argument to fun_call will become |this|. If there are no arguments
      // to a fun_call, we will push an extra undefined.
      if (argcFixed > 0) {
#ifdef DEBUG
        Label nonZeroArgs;
        masm.branchTest32(Assembler::NonZero, argcReg, argcReg, &nonZeroArgs);
        masm.assumeUnreachable("non-zero argcFixed implies non-zero argc");
        masm.bind(&nonZeroArgs);
#endif
        masm.sub32(Imm32(1), argcReg);
      }
      return true;
    case CallFlags::FunApplyNullUndefined:
      // argc must be 0 if null or undefined is passed as second argument to
      // |apply|.
      masm.move32(Imm32(0), argcReg);
      return true;
    default:
      break;
  }

  // We need to guard the length of the arguments.
  FailurePath* failure;
  if (!addFailurePath(&failure)) {
    return false;
  }

  // Load callee argc into scratch.
  switch (flags.getArgFormat()) {
    case CallFlags::Spread:
    case CallFlags::FunApplyArray: {
      // Load the length of the elements.
      BaselineFrameSlot slot(flags.isConstructing());
      masm.unboxObject(allocator.addressOf(masm, slot), scratch);
      masm.loadPtr(Address(scratch, NativeObject::offsetOfElements()), scratch);
      masm.load32(Address(scratch, ObjectElements::offsetOfLength()), scratch);
      break;
    }
    case CallFlags::FunApplyArgsObj: {
      // Load the arguments object length.
      BaselineFrameSlot slot(0);
      masm.unboxObject(allocator.addressOf(masm, slot), scratch);
      masm.loadArgumentsObjectLength(scratch, scratch, failure->label());
      break;
    }
    default:
      MOZ_CRASH("Unknown arg format");
  }

  // Ensure that callee argc does not exceed the limit.
  masm.branch32(Assembler::Above, scratch, Imm32(JIT_ARGS_LENGTH_MAX),
                failure->label());

  // We're past the final guard. Update argc with the new value.
  masm.move32(scratch, argcReg);

  return true;
}

void BaselineCacheIRCompiler::pushArguments(
    Register argcReg, Register calleeReg, Register scratch, Register scratch2,
    Register scratch3, CallFlags flags, uint32_t argcFixed, bool isJitCall) {
  bool isConstructing = flags.isConstructing();
  MOZ_ASSERT_IF(isConstructing, flags.getArgFormat() == CallFlags::Standard ||
                                    flags.getArgFormat() == CallFlags::Spread);

  if (isJitCall) {
    // If we're calling jitcode, we have to align the stack and ensure that
    // enough arguments are being passed, filling in any missing arguments
    // with `undefined`. `newTarget` should be pushed after alignment padding
    // but before the `undefined` values, so we also handle it here.
    prepareForArguments(argcReg, calleeReg, scratch, scratch2, flags,
                        argcFixed);
  } else if (isConstructing) {
    // If we're not calling jitcode, push newTarget now so that the shared
    // paths below can assume it's already pushed.
    pushNewTarget();
  }

  // Push the formal arguments, and possibly `this` and/or `callee`.
  // There are three cases:
  // 1. Non-scripted call: all arguments are pushed here.
  // 2. Scripted call: all arguments except `callee` are pushed here. `callee`
  //    must be passed as a CalleeToken, and is pushed below.
  // 3. Scripted constructor: only formal arguments are pushed here. We must
  //    push a new `this` value using createThis, and then push `callee` as
  //    a CalleeToken. Note that constructors must be Standard or Spread.
  switch (flags.getArgFormat()) {
    case CallFlags::Standard:
      pushStandardArguments(argcReg, scratch, scratch2, argcFixed, isJitCall,
                            isConstructing);
      break;
    case CallFlags::Spread:
      pushArrayArguments(argcReg, scratch, scratch2, isJitCall, isConstructing);
      break;
    case CallFlags::FunCall:
      pushFunCallArguments(argcReg, calleeReg, scratch, scratch2, argcFixed,
                           isJitCall);
      break;
    case CallFlags::FunApplyArgsObj:
      pushFunApplyArgsObj(argcReg, calleeReg, scratch, scratch2, isJitCall);
      break;
    case CallFlags::FunApplyArray:
      pushArrayArguments(argcReg, scratch, scratch2, isJitCall,
                         /*isConstructing =*/false);
      break;
    case CallFlags::FunApplyNullUndefined:
      pushFunApplyNullUndefinedArguments(calleeReg, isJitCall);
      break;
    default:
      MOZ_CRASH("Invalid arg format");
  }

  if (isJitCall) {
    if (isConstructing) {
      createThis(argcReg, calleeReg, scratch, scratch2, scratch3, flags);
    }

    // Note that we use Push, not push, so that callJit will align the stack
    // properly on ARM.
    masm.PushCalleeToken(calleeReg, isConstructing);
  }
}

void BaselineCacheIRCompiler::prepareForArguments(
    Register argcReg, Register calleeReg, Register scratch, Register scratch2,
    CallFlags flags, uint32_t argcFixed) {
  bool isConstructing = flags.isConstructing();

  // We will align the stack based on an argument count that doesn't include
  // |this| or |newTarget|. We only care about the parity of the count, so
  // if we are constructing (and therefore passing both) we can compute
  // alignment as if |this| was included.
  bool countIncludesThis = isConstructing;

  // Check for arguments underflow. If we aren't passing enough arguments,
  // fill in missing arguments with `undefined`.
  Label noUnderflow, done;
  masm.loadFunctionArgCount(calleeReg, scratch);
  masm.branch32(Assembler::AboveOrEqual, argcReg, scratch, &noUnderflow);

  masm.alignJitStackBasedOnNArgs(scratch, countIncludesThis);

  // Push newTarget, if necessary
  if (isConstructing) {
    pushNewTarget();
  }

  // Push `undefined` in a loop
  Label loop;
  masm.bind(&loop);
  masm.Push(UndefinedValue());
  masm.sub32(Imm32(1), scratch);
  masm.branch32(Assembler::Above, scratch, argcReg, &loop);
  masm.jump(&done);

  masm.bind(&noUnderflow);

  if (flags.getArgFormat() == CallFlags::Standard &&
      argcFixed < MaxUnrolledArgCopy) {
    masm.alignJitStackBasedOnNArgs(argcFixed, countIncludesThis);
  } else if (flags.getArgFormat() == CallFlags::FunCall &&
             argcFixed < MaxUnrolledArgCopy) {
    // If any arguments are passed, one argument becomes |this|.
    uint32_t actualArgc = argcFixed > 0 ? argcFixed - 1 : 0;
    masm.alignJitStackBasedOnNArgs(actualArgc, countIncludesThis);
  } else {
    masm.alignJitStackBasedOnNArgs(argcReg, countIncludesThis);
  }

  if (isConstructing) {
    pushNewTarget();
  }

  masm.bind(&done);
}

void BaselineCacheIRCompiler::pushNewTarget() {
  // When it's time to push `newTarget`, the stack looks like this
  // (higher addresses at the top):
  //
  // .                        .
  // +------------------------+
  // | callee                 |
  // | this                   |
  // | arg0                   | <= pushed on caller's expression stack
  // | arg1                   |
  // | ...                    |
  // | argN                   |
  // | newTarget              | <---- we want to copy this
  // +------------------------+
  // | frame descriptor       | <= BaselineStubFrame
  // | return address         |
  // | caller frame pointer   | <-- frame pointer points here
  // +------------------------+
  // | stub ptr               |
  // | InlinedICScript?       |
  // | (alignment padding?)   | <-- stack pointer points here
  // +------------------------+
  //
  // `newTarget` is the last argument pushed, so it's immediately above the
  // stub frame on the stack.
  masm.pushValue(Address(FramePointer, BaselineStubFrameLayout::Size()));
}

static uint32_t ArgsOffsetFromFP(bool isConstructing) {
  // The arguments are on the stack just above the stub frame. If we are
  // constructing, we have to skip newTarget.
  uint32_t offset = BaselineStubFrameLayout::Size();
  if (isConstructing) {
    offset += sizeof(Value);
  }
  return offset;
}

void BaselineCacheIRCompiler::pushStandardArguments(
    Register argcReg, Register scratch, Register scratch2, uint32_t argcFixed,
    bool isJitCall, bool isConstructing) {
  MOZ_ASSERT(enteredStubFrame_);

  // The arguments to the call IC were pushed on the stack from left to right,
  // meaning that the first argument is at the highest address and the last
  // argument is at the lowest address. Our callee needs them to be in the
  // opposite order, so we duplicate them now.

  bool shouldCopyCallee = !isJitCall;
  bool shouldCopyThis = shouldCopyCallee || !isConstructing;
  int additionalArgc = shouldCopyCallee + shouldCopyThis;
  uint32_t argsOffset = ArgsOffsetFromFP(isConstructing);

  if (argcFixed < MaxUnrolledArgCopy) {
    // For small argc, we unroll the argument pushing loop.

#ifdef DEBUG
    Label ok;
    masm.branch32(Assembler::Equal, argcReg, Imm32(argcFixed), &ok);
    masm.assumeUnreachable("Invalid argcFixed value");
    masm.bind(&ok);
#endif

    size_t numCopiedValues = argcFixed + additionalArgc;
    for (size_t i = 0; i < numCopiedValues; ++i) {
      masm.pushValue(Address(FramePointer, argsOffset + i * sizeof(Value)));
    }
  } else {
    MOZ_ASSERT(argcFixed == MaxUnrolledArgCopy);

    // Compute pointers to the start and end of the arguments area.
    Register argPtr = scratch;
    Register argEnd = scratch2;
    masm.computeEffectiveAddress(Address(FramePointer, argsOffset), argPtr);
    BaseValueIndex endAddr(FramePointer, argcReg,
                           argsOffset + additionalArgc * sizeof(Value));
    masm.computeEffectiveAddress(endAddr, argEnd);

    // Push all values, starting at the last one.
    // We always push at least one value (`this`), so we don't need
    // a loop guard.
    Label loop;
    masm.bind(&loop);
    {
      masm.pushValue(Address(argPtr, 0));
      masm.addPtr(Imm32(sizeof(Value)), argPtr);
      masm.branchPtr(Assembler::Below, argPtr, argEnd, &loop);
    }
  }
}

void BaselineCacheIRCompiler::pushArrayArguments(Register argcReg,
                                                 Register scratch,
                                                 Register scratch2,
                                                 bool isJitCall,
                                                 bool isConstructing) {
  MOZ_ASSERT(enteredStubFrame_);

  // If the array is empty, we can skip the loop entirely.
  Label emptyArray;
  masm.branchTest32(Assembler::Zero, argcReg, argcReg, &emptyArray);

  // Pull the array off the stack and load a pointer to its first element.
  Register startReg = scratch;
  size_t arrayOffset = ArgsOffsetFromFP(isConstructing);
  masm.unboxObject(Address(FramePointer, arrayOffset), startReg);
  masm.loadPtr(Address(startReg, NativeObject::offsetOfElements()), startReg);

  // Set up endReg to point to &array[argc - 1].
  Register endReg = scratch2;
  BaseValueIndex endAddr(startReg, argcReg, -int32_t(sizeof(Value)));
  masm.computeEffectiveAddress(endAddr, endReg);

  // Loop to push all arguments. We've already checked for an empty array above.
  Label loop;
  masm.bind(&loop);
  masm.pushValue(Address(endReg, 0));
  masm.subPtr(Imm32(sizeof(Value)), endReg);
  masm.branchPtr(Assembler::AboveOrEqual, endReg, startReg, &loop);

  masm.bind(&emptyArray);

  bool shouldPushCallee = !isJitCall;
  bool shouldPushThis = shouldPushCallee || !isConstructing;

  if (shouldPushThis) {
    size_t thisvOffset = arrayOffset + sizeof(Value);
    masm.pushValue(Address(FramePointer, thisvOffset));
  }

  // Push |callee| if needed.
  if (shouldPushCallee) {
    size_t calleeOffset = arrayOffset + 2 * sizeof(Value);
    masm.pushValue(Address(FramePointer, calleeOffset));
  }
}

void BaselineCacheIRCompiler::pushFunApplyNullUndefinedArguments(
    Register calleeReg, bool isJitCall) {
  // argc is already set to 0, so we just have to push |this| and (for native
  // calls) the callee.

  MOZ_ASSERT(enteredStubFrame_);

  // Push |this|.
  size_t thisvOffset =
      ArgsOffsetFromFP(/*isConstructing*/ false) + sizeof(Value);
  masm.pushValue(Address(FramePointer, thisvOffset));

  // Push |callee| if needed.
  if (!isJitCall) {
    masm.Push(TypedOrValueRegister(MIRType::Object, AnyRegister(calleeReg)));
  }
}

void BaselineCacheIRCompiler::pushFunCallArguments(
    Register argcReg, Register calleeReg, Register scratch, Register scratch2,
    uint32_t argcFixed, bool isJitCall) {
  if (argcFixed == 0) {
    // Store the new |this|.
    masm.pushValue(UndefinedValue());

    // Store |callee| if needed.
    if (!isJitCall) {
      masm.Push(TypedOrValueRegister(MIRType::Object, AnyRegister(calleeReg)));
    }
    return;
  }

  // When we call fun_call, the stack looks like the left column (note
  // that newTarget will not be present, because fun_call cannot be a
  // constructor call):
  //
  // ***Arguments to fun_call***
  // callee (fun_call)               ***Arguments to target***
  // this (target function)   -----> callee
  // arg0 (this of target)    -----> this
  // arg1 (arg0 of target)    -----> arg0
  // argN (argN-1 of target)  -----> arg1
  //
  // As demonstrated in the right column, this is exactly what we need
  // the stack to look like when calling pushStandardArguments for target,
  // except with one more argument. If we subtract 1 from argc, as we
  // have already done in updateArgc, everything works out correctly.

  if (argcFixed != MaxUnrolledArgCopy) {
    argcFixed--;
  }
  pushStandardArguments(argcReg, scratch, scratch2, argcFixed, isJitCall,
                        /*isConstructing =*/false);
}

void BaselineCacheIRCompiler::pushFunApplyArgsObj(Register argcReg,
                                                  Register calleeReg,
                                                  Register scratch,
                                                  Register scratch2,
                                                  bool isJitCall) {
  MOZ_ASSERT(enteredStubFrame_);
  // If there are no arguments, we can skip the loop entirely.
  Label emptyArgs;
  masm.branchTest32(Assembler::Zero, argcReg, argcReg, &emptyArgs);

  // Load ArgumentsData
  Register argsReg = scratch;
  uint32_t argsOffset = ArgsOffsetFromFP(/*isConstructing*/ false);
  masm.unboxObject(Address(FramePointer, BaselineStubFrameLayout::Size()),
                   argsReg);
  masm.loadPrivate(Address(argsReg, ArgumentsObject::getDataSlotOffset()),
                   argsReg);

  // We push the arguments onto the stack last-to-first.
  // Compute the bounds of the arguments array.
  Register currReg = scratch2;
  Address argsStartAddr(argsReg, ArgumentsData::offsetOfArgs());
  masm.computeEffectiveAddress(argsStartAddr, argsReg);
  BaseValueIndex argsEndAddr(argsReg, argcReg, -int32_t(sizeof(Value)));
  masm.computeEffectiveAddress(argsEndAddr, currReg);

  // Loop until all arguments have been pushed.
  Label loop;
  masm.bind(&loop);
  Address currArgAddr(currReg, 0);
#ifdef DEBUG
  // Arguments are forwarded to the call object if they are closed over.
  // In this case, OVERRIDDEN_ELEMENTS_BIT should be set.
  Label notForwarded;
  masm.branchTestMagic(Assembler::NotEqual, currArgAddr, &notForwarded);
  masm.assumeUnreachable("Should have checked for overridden elements");
  masm.bind(&notForwarded);
#endif
  masm.pushValue(currArgAddr);
  masm.subPtr(Imm32(sizeof(Value)), currReg);
  masm.branchPtr(Assembler::AboveOrEqual, currReg, argsReg, &loop);

  masm.bind(&emptyArgs);

  // Push arg0 as |this| for call
  masm.pushValue(Address(FramePointer, argsOffset + sizeof(Value)));

  // Push |callee| if needed.
  if (!isJitCall) {
    masm.Push(TypedOrValueRegister(MIRType::Object, AnyRegister(calleeReg)));
  }
}

void BaselineCacheIRCompiler::pushBoundFunctionArguments(
    Register argcReg, Register calleeReg, Register scratch, Register scratch2,
    CallFlags flags, uint32_t numBoundArgs, bool isJitCall) {
  bool isConstructing = flags.isConstructing();

  // Calculate total number of actual arguments
  Register countReg = scratch;
  masm.computeEffectiveAddress(Address(argcReg, numBoundArgs), countReg);

  Address boundTarget(calleeReg, BoundFunctionObject::offsetOfTargetSlot());

  if (isJitCall) {
    // We align the jit stack depending on whether the number of arguments
    // is even or odd. To simplify the underflow check, we don't include
    // |this| or (if constructing) |newTarget| in countReg. If we're
    // not constructing, then we should align as if countReg doesn't include
    // |this|. If we're passing both values, then countReg+2 has the same
    // parity as countReg, so we should align as if |this| was included.
    bool countIncludesThis = isConstructing;

    Label noUnderflow, readyForArgs;
    masm.unboxObject(boundTarget, scratch2);
    masm.loadFunctionArgCount(scratch2, scratch2);
    masm.branch32(Assembler::AboveOrEqual, countReg, scratch2, &noUnderflow);

    masm.alignJitStackBasedOnNArgs(scratch2, countIncludesThis);
    if (isConstructing) {
      masm.pushValue(boundTarget);
    }

    Label undefLoop;
    masm.bind(&undefLoop);
    masm.Push(UndefinedValue());
    masm.sub32(Imm32(1), scratch2);
    masm.branch32(Assembler::Above, scratch2, countReg, &undefLoop);
    masm.jump(&readyForArgs);

    masm.bind(&noUnderflow);
    masm.alignJitStackBasedOnNArgs(countReg, countIncludesThis);
    if (isConstructing) {
      masm.pushValue(boundTarget);
    }
    masm.bind(&readyForArgs);
  } else if (isConstructing) {
    masm.pushValue(boundTarget);
  }

  // Skip the argument loop if no args are being passed.
  Label noArgs;
  masm.branchTest32(Assembler::Zero, argcReg, argcReg, &noArgs);

  // Ensure argPtr initially points to the last argument. Skip the stub frame.
  Register argPtr = scratch2;
  Address argAddress(FramePointer, BaselineStubFrameLayout::Size());
  if (isConstructing) {
    // Skip newTarget.
    argAddress.offset += sizeof(Value);
  }
  masm.computeEffectiveAddress(argAddress, argPtr);

  // Push all supplied arguments, starting at the last one.
  Label argsLoop;
  masm.move32(argcReg, countReg);
  masm.bind(&argsLoop);
  {
    masm.pushValue(Address(argPtr, 0));
    masm.addPtr(Imm32(sizeof(Value)), argPtr);

    masm.branchSub32(Assembler::NonZero, Imm32(1), countReg, &argsLoop);
  }
  masm.bind(&noArgs);

  // Push the bound arguments, starting at the last one.
  constexpr size_t inlineArgsOffset =
      BoundFunctionObject::offsetOfFirstInlineBoundArg();
  if (numBoundArgs <= BoundFunctionObject::MaxInlineBoundArgs) {
    for (size_t i = 0; i < numBoundArgs; i++) {
      size_t argIndex = numBoundArgs - i - 1;
      Address argAddr(calleeReg, inlineArgsOffset + argIndex * sizeof(Value));
      masm.pushValue(argAddr);
    }
  } else {
    masm.unboxObject(Address(calleeReg, inlineArgsOffset), scratch);
    masm.loadPtr(Address(scratch, NativeObject::offsetOfElements()), scratch);
    for (size_t i = 0; i < numBoundArgs; i++) {
      size_t argIndex = numBoundArgs - i - 1;
      Address argAddr(scratch, argIndex * sizeof(Value));
      masm.pushValue(argAddr);
    }
  }

  if (!isConstructing) {
    // Push the bound |this|.
    Address boundThis(calleeReg, BoundFunctionObject::offsetOfBoundThisSlot());
    masm.pushValue(boundThis);
  }
}

bool BaselineCacheIRCompiler::emitCallNativeShared(
    NativeCallType callType, ObjOperandId calleeId, Int32OperandId argcId,
    CallFlags flags, uint32_t argcFixed, Maybe<bool> ignoresReturnValue,
    Maybe<uint32_t> targetOffset, ClearLocalAllocSite clearLocalAllocSite) {
  AutoOutputRegister output(*this);
  AutoScratchRegisterMaybeOutput scratch(allocator, masm, output);
  AutoScratchRegister scratch2(allocator, masm);

  Register calleeReg = allocator.useRegister(masm, calleeId);
  Register argcReg = allocator.useRegister(masm, argcId);

  bool isConstructing = flags.isConstructing();
  bool isSameRealm = flags.isSameRealm();

  if (!updateArgc(flags, argcReg, argcFixed, scratch)) {
    return false;
  }

  allocator.discardStack(masm);

  // Push a stub frame so that we can perform a non-tail call.
  // Note that this leaves the return address in TailCallReg.
  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  if (!isSameRealm) {
    masm.switchToObjectRealm(calleeReg, scratch);
  }

  pushArguments(argcReg, calleeReg, scratch, scratch2, InvalidReg, flags,
                argcFixed, /*isJitCall =*/false);

  // Native functions have the signature:
  //
  //    bool (*)(JSContext*, unsigned, Value* vp)
  //
  // Where vp[0] is space for callee/return value, vp[1] is |this|, and vp[2]
  // onward are the function arguments.

  // Initialize vp.
  masm.moveStackPtrTo(scratch2.get());

  // Construct a native exit frame.
  masm.push(calleeReg);
  masm.push(argcReg);

  masm.push(FrameDescriptor(FrameType::BaselineStub));
  masm.pushRegs(ICTailCallReg, FramePointer);
  masm.loadJSContext(scratch);
  masm.enterFakeExitFrameForNative(scratch, scratch, isConstructing);

  // Execute call.
  masm.setupUnalignedABICall(scratch);
  masm.loadJSContext(scratch);
  masm.passABIArg(scratch);
  masm.passABIArg(argcReg);
  masm.passABIArg(scratch2);

  switch (callType) {
    case NativeCallType::Native: {
#ifdef JS_SIMULATOR
      // The simulator requires VM calls to be redirected to a special
      // swi instruction to handle them, so we store the redirected
      // pointer in the stub and use that instead of the original one.
      // (See CacheIRWriter::callNativeFunction.)
      Address redirectedAddr(stubAddress(*targetOffset));
      masm.callWithABI(redirectedAddr);
#else
      if (*ignoresReturnValue) {
        masm.loadPrivate(
            Address(calleeReg, JSFunction::offsetOfJitInfoOrScript()),
            calleeReg);
        masm.callWithABI(
            Address(calleeReg, JSJitInfo::offsetOfIgnoresReturnValueNative()));
      } else {
        // This depends on the native function pointer being stored unchanged as
        // a PrivateValue.
        masm.callWithABI(Address(calleeReg, JSFunction::offsetOfNativeOrEnv()));
      }
#endif
    } break;
    case NativeCallType::ClassHook: {
      Address nativeAddr(stubAddress(*targetOffset));
      masm.callWithABI(nativeAddr);
    } break;
  }

  // Test for failure.
  masm.branchIfFalseBool(ReturnReg, masm.exceptionLabel());

  // Load the return value.
  masm.loadValue(
      Address(masm.getStackPointer(), NativeExitFrameLayout::offsetOfResult()),
      output.valueReg());

  stubFrame.leave(masm);

  if (!isSameRealm) {
    masm.switchToBaselineFrameRealm(scratch2);
  }

  // We will also unilaterally clear this on exception handling.
  if (clearLocalAllocSite == ClearLocalAllocSite::Yes) {
    masm.storeLocalAllocSite(ImmPtr(nullptr), scratch2);
  }

  return true;
}

void BaselineCacheIRCompiler::loadAllocSiteIntoContext(uint32_t siteOffset) {
  AutoScratchRegister scratch(allocator, masm);
  AutoScratchRegister site(allocator, masm);

  StubFieldOffset siteField(siteOffset, StubField::Type::AllocSite);
  emitLoadStubField(siteField, site);

  masm.storeLocalAllocSite(site.get(), scratch);
}

#ifdef JS_SIMULATOR
bool BaselineCacheIRCompiler::emitCallNativeFunction(ObjOperandId calleeId,
                                                     Int32OperandId argcId,
                                                     CallFlags flags,
                                                     uint32_t argcFixed,
                                                     uint32_t targetOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Maybe<bool> ignoresReturnValue;
  Maybe<uint32_t> targetOffset_ = mozilla::Some(targetOffset);
  return emitCallNativeShared(NativeCallType::Native, calleeId, argcId, flags,
                              argcFixed, ignoresReturnValue, targetOffset_);
}

bool BaselineCacheIRCompiler::emitCallDOMFunction(
    ObjOperandId calleeId, Int32OperandId argcId, ObjOperandId thisObjId,
    CallFlags flags, uint32_t argcFixed, uint32_t targetOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Maybe<bool> ignoresReturnValue;
  Maybe<uint32_t> targetOffset_ = mozilla::Some(targetOffset);
  return emitCallNativeShared(NativeCallType::Native, calleeId, argcId, flags,
                              argcFixed, ignoresReturnValue, targetOffset_);
}

bool BaselineCacheIRCompiler::emitCallDOMFunctionWithAllocSite(
    ObjOperandId calleeId, Int32OperandId argcId, ObjOperandId thisObjId,
    CallFlags flags, uint32_t argcFixed, uint32_t siteOffset,
    uint32_t targetOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  loadAllocSiteIntoContext(siteOffset);
  Maybe<bool> ignoresReturnValue;
  Maybe<uint32_t> targetOffset_ = mozilla::Some(targetOffset);
  return emitCallNativeShared(NativeCallType::Native, calleeId, argcId, flags,
                              argcFixed, ignoresReturnValue, targetOffset_,
                              ClearLocalAllocSite::Yes);
}
#else
bool BaselineCacheIRCompiler::emitCallNativeFunction(ObjOperandId calleeId,
                                                     Int32OperandId argcId,
                                                     CallFlags flags,
                                                     uint32_t argcFixed,
                                                     bool ignoresReturnValue) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Maybe<bool> ignoresReturnValue_ = mozilla::Some(ignoresReturnValue);
  Maybe<uint32_t> targetOffset;
  return emitCallNativeShared(NativeCallType::Native, calleeId, argcId, flags,
                              argcFixed, ignoresReturnValue_, targetOffset);
}

bool BaselineCacheIRCompiler::emitCallDOMFunction(ObjOperandId calleeId,
                                                  Int32OperandId argcId,
                                                  ObjOperandId thisObjId,
                                                  CallFlags flags,
                                                  uint32_t argcFixed) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Maybe<bool> ignoresReturnValue = mozilla::Some(false);
  Maybe<uint32_t> targetOffset;
  return emitCallNativeShared(NativeCallType::Native, calleeId, argcId, flags,
                              argcFixed, ignoresReturnValue, targetOffset);
}

bool BaselineCacheIRCompiler::emitCallDOMFunctionWithAllocSite(
    ObjOperandId calleeId, Int32OperandId argcId, ObjOperandId thisObjId,
    CallFlags flags, uint32_t argcFixed, uint32_t siteOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  loadAllocSiteIntoContext(siteOffset);
  Maybe<bool> ignoresReturnValue = mozilla::Some(false);
  Maybe<uint32_t> targetOffset;
  return emitCallNativeShared(NativeCallType::Native, calleeId, argcId, flags,
                              argcFixed, ignoresReturnValue, targetOffset,
                              ClearLocalAllocSite::Yes);
}
#endif

bool BaselineCacheIRCompiler::emitCallClassHook(ObjOperandId calleeId,
                                                Int32OperandId argcId,
                                                CallFlags flags,
                                                uint32_t argcFixed,
                                                uint32_t targetOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Maybe<bool> ignoresReturnValue;
  Maybe<uint32_t> targetOffset_ = mozilla::Some(targetOffset);
  return emitCallNativeShared(NativeCallType::ClassHook, calleeId, argcId,
                              flags, argcFixed, ignoresReturnValue,
                              targetOffset_);
}

// This op generates no code. It caches metadata for eventual use in createThis.
bool BaselineCacheIRCompiler::emitMetaCreateThis(uint32_t numFixedSlots,
                                                 uint32_t numDynamicSlots,
                                                 gc::AllocKind allocKind,
                                                 uint32_t thisShapeOffset,
                                                 uint32_t siteOffset) {
  MOZ_ASSERT(createThisData_.isNothing());
  createThisData_.emplace(numFixedSlots, numDynamicSlots, allocKind,
                          thisShapeOffset, siteOffset);
  return true;
}

// Helper function for loading call arguments from the stack.  Loads
// and unboxes an object from a specific slot.
void BaselineCacheIRCompiler::loadStackObject(ArgumentKind kind,
                                              CallFlags flags, Register argcReg,
                                              Register dest,
                                              uint32_t extraArgs) {
  MOZ_ASSERT(enteredStubFrame_);

  bool addArgc = false;
  int32_t slotIndex = GetIndexOfArgument(kind, flags, &addArgc);

  if (addArgc) {
    int32_t slotOffset = (slotIndex - extraArgs) * sizeof(JS::Value) +
                         BaselineStubFrameLayout::Size();
    BaseValueIndex slotAddr(FramePointer, argcReg, slotOffset);
    masm.unboxObject(slotAddr, dest);
  } else {
    int32_t slotOffset =
        slotIndex * sizeof(JS::Value) + BaselineStubFrameLayout::Size();
    Address slotAddr(FramePointer, slotOffset);
    masm.unboxObject(slotAddr, dest);
  }
}

/*
 * Scripted constructors require a |this| object to be created prior to the
 * call. This is called after we have pushed the formal arguments, but before
 * pushing the callee token. When this is called, argcReg must contain the
 * number of actual arguments (including bound or spread arguments; not
 * including `undef` pushed in cases of argument underflow). calleeReg should
 * contain the actual callee.
 */
void BaselineCacheIRCompiler::createThis(Register argcReg, Register calleeReg,
                                         Register scratch, Register scratch2,
                                         Register scratch3, CallFlags flags,
                                         Maybe<uint32_t> numBoundArgs) {
  MOZ_ASSERT(flags.isConstructing());
  bool isBoundFunction = numBoundArgs.isSome();

  // Derived constructors don't allocate a `this` object. They instead call
  // `super`, and the base class constructor will allocate `this`.
  if (flags.needsUninitializedThis()) {
    masm.Push(MagicValue(JS_UNINITIALIZED_LEXICAL));
    return;
  }

  Label done;
  bool hasCreateThisData = createThisData_.isSome();
  if (hasCreateThisData) {
    Label fail;
    Register result = scratch;

    Register shape = scratch2;
    masm.loadPtr(stubAddress(createThisData_->thisShapeOffset), shape);

    Register site = scratch3;
    masm.loadPtr(stubAddress(createThisData_->allocSiteOffset), site);

    // On x86-32, all of our registers are already spoken for, but we need one
    // more. We spill calleeReg and reload it in the success and failure paths.
    Register temp = calleeReg;

    // Try to allocate inline.
    masm.push(calleeReg);
    masm.createPlainGCObject(
        result, shape, temp, shape, createThisData_->numFixedSlots,
        createThisData_->numDynamicSlots, createThisData_->allocKind,
        gc::Heap::Default, &fail, AllocSiteInput(site));
    masm.pop(calleeReg);
    masm.Push(TypedOrValueRegister(MIRType::Object, AnyRegister(result)));

    masm.jump(&done);

    masm.bind(&fail);
    masm.pop(calleeReg);
  }

  // Save a reference to the start of the arguments, so that we can root
  // them in CreateThisFromIC.
  Register argvReg = scratch2;
  masm.moveStackPtrTo(argvReg);

  // Save live registers that don't have to be traced.
  LiveGeneralRegisterSet liveNonGCRegs;
  liveNonGCRegs.add(argcReg);
  masm.PushRegsInMask(liveNonGCRegs);

  // CreateThis takes two arguments: callee, and newTarget. We may also pass
  // an alloc site.

  // Push argv/argc for rooting in CreateThisFromIC
  masm.pushRegs(argcReg, argvReg);

  if (hasCreateThisData) {
    masm.loadPtr(stubAddress(createThisData_->allocSiteOffset), scratch);
    masm.push(scratch);
  }

  if (isBoundFunction) {
    // Push the bound function's target as callee and newTarget.
    masm.pushRegs(calleeReg, calleeReg);
  } else {
    // Push newTarget and callee:
    loadStackObject(ArgumentKind::NewTarget, flags, argcReg, scratch);
    masm.pushRegs(scratch, calleeReg);
  }

  if (hasCreateThisData) {
    using Fn = bool (*)(JSContext*, HandleObject, HandleObject, gc::AllocSite*,
                        Value*, uint32_t, MutableHandleValue);
    callVM<Fn, CreateThisFromICWithAllocSite>(masm);
  } else {
    // Call CreateThisFromIC.
    using Fn = bool (*)(JSContext*, HandleObject, HandleObject, Value*,
                        uint32_t, MutableHandleValue);
    callVM<Fn, CreateThisFromIC>(masm);
  }

#ifdef DEBUG
  Label createdThisOK;
  masm.branchTestObject(Assembler::Equal, JSReturnOperand, &createdThisOK);
  masm.branchTestMagicValue(Assembler::Equal, JSReturnOperand,
                            JS_UNINITIALIZED_LEXICAL, &createdThisOK);
  masm.assumeUnreachable(
      "The return of CreateThis must be an object or uninitialized.");
  masm.bind(&createdThisOK);
#endif

  // Restore saved registers.
  masm.PopRegsInMask(liveNonGCRegs);

  // Restore ICStubReg. The stub might have been moved if CreateThisFromIC
  // discarded JIT code.
  Address stubAddr(FramePointer, BaselineStubFrameLayout::ICStubOffsetFromFP);
  masm.loadPtr(stubAddr, ICStubReg);

  // Push |this|.
  MOZ_ASSERT(!liveNonGCRegs.aliases(JSReturnOperand));
  masm.Push(TypedOrValueRegister(JSReturnOperand));

  // Restore calleeReg. CreateThisFromIC may trigger a GC, so we reload the
  // callee from the caller's frame (which is traced) instead of spilling it to
  // the stack.
  if (isBoundFunction) {
    // Load the callee (which is a bound function).
    // At this point, argcReg is the number of actual arguments being passed.
    // For bound functions, this includes bound arguments. However, to compute
    // the address of `callee` in the caller's frame, we need to know how many
    // arguments were passed by the caller. This is argcReg - numBoundArgs.
    // We pass in `numBoundArgs` so that loadStackObject can adjust accordingly.
    loadStackObject(ArgumentKind::Callee, flags, argcReg, calleeReg,
                    *numBoundArgs);

    // Load the target JSFunction.
    Address boundTarget(calleeReg, BoundFunctionObject::offsetOfTargetSlot());
    masm.unboxObject(boundTarget, calleeReg);
  } else {
    loadStackObject(ArgumentKind::Callee, flags, argcReg, calleeReg);
  }
  masm.bind(&done);
}

void BaselineCacheIRCompiler::updateReturnValue() {
  Label skipThisReplace;
  masm.branchTestObject(Assembler::Equal, JSReturnOperand, &skipThisReplace);

  // If a constructor does not explicitly return an object, the return value
  // of the constructor is |this|. We load it out of the baseline stub frame.

  // At this point, the stack looks like this:
  //  newTarget
  //  ArgN
  //  ...
  //  Arg0
  //  ThisVal         <---- We want this value.
  //  Callee token          | Skip two stack slots.
  //  Frame descriptor      v
  //  [Top of stack]
  size_t thisvOffset =
      JitFrameLayout::offsetOfThis() - JitFrameLayout::bytesPoppedAfterCall();
  Address thisAddress(masm.getStackPointer(), thisvOffset);
  masm.loadValue(thisAddress, JSReturnOperand);

#ifdef DEBUG
  masm.branchTestObject(Assembler::Equal, JSReturnOperand, &skipThisReplace);
  masm.assumeUnreachable("Return of constructing call should be an object.");
#endif
  masm.bind(&skipThisReplace);
}

bool BaselineCacheIRCompiler::emitCallScriptedFunctionShared(
    ObjOperandId calleeId, Int32OperandId argcId, CallFlags flags,
    uint32_t argcFixed, Maybe<uint32_t> icScriptOffset) {
  AutoOutputRegister output(*this);
  AutoScratchRegister scratch(allocator, masm);
  AutoScratchRegisterMaybeOutput scratch2(allocator, masm, output);
  AutoScratchRegisterMaybeOutputType scratch3(allocator, masm, output);

  Register calleeReg = allocator.useRegister(masm, calleeId);
  Register argcReg = allocator.useRegister(masm, argcId);

  bool isInlined = icScriptOffset.isSome();

  bool isConstructing = flags.isConstructing();
  bool isSameRealm = flags.isSameRealm();

  if (!updateArgc(flags, argcReg, argcFixed, scratch)) {
    return false;
  }

  allocator.discardStack(masm);

  // Push a stub frame so that we can perform a non-tail call.
  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  if (!isSameRealm) {
    masm.switchToObjectRealm(calleeReg, scratch);
  }
  if (isInlined) {
    stubFrame.pushInlinedICScript(masm, stubAddress(*icScriptOffset));
  }

  pushArguments(argcReg, calleeReg, scratch, scratch2, scratch3, flags,
                argcFixed, /*isJitCall =*/true);

  masm.PushFrameDescriptorForJitCall(FrameType::BaselineStub, argcReg, scratch,
                                     isInlined);

  // Load the start of the target JitCode.
  Register code = scratch2;
  if (isInlined) {
    masm.loadJitCodeRawNoIon(calleeReg, code, scratch);
  } else {
    masm.loadJitCodeRaw(calleeReg, code);
  }

  masm.callJit(code);

  // If this is a constructing call, and the callee returns a non-object,
  // replace it with the |this| object passed in.
  if (isConstructing) {
    updateReturnValue();
  }

  stubFrame.leave(masm);

  if (!isSameRealm) {
    masm.switchToBaselineFrameRealm(scratch);
  }

  return true;
}

bool BaselineCacheIRCompiler::emitCallScriptedFunction(ObjOperandId calleeId,
                                                       Int32OperandId argcId,
                                                       CallFlags flags,
                                                       uint32_t argcFixed) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Maybe<uint32_t> icScriptOffset = mozilla::Nothing();
  return emitCallScriptedFunctionShared(calleeId, argcId, flags, argcFixed,
                                        icScriptOffset);
}

bool BaselineCacheIRCompiler::emitCallInlinedFunction(ObjOperandId calleeId,
                                                      Int32OperandId argcId,
                                                      uint32_t icScriptOffset,
                                                      CallFlags flags,
                                                      uint32_t argcFixed) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  return emitCallScriptedFunctionShared(calleeId, argcId, flags, argcFixed,
                                        mozilla::Some(icScriptOffset));
}

bool BaselineCacheIRCompiler::emitCallWasmFunction(
    ObjOperandId calleeId, Int32OperandId argcId, CallFlags flags,
    uint32_t argcFixed, uint32_t funcExportOffset, uint32_t instanceOffset) {
  return emitCallScriptedFunction(calleeId, argcId, flags, argcFixed);
}

#ifdef JS_PUNBOX64
template <typename IdType>
bool BaselineCacheIRCompiler::emitCallScriptedProxyGetShared(
    ValOperandId targetId, ObjOperandId receiverId, ObjOperandId handlerId,
    ObjOperandId trapId, IdType id, uint32_t nargsAndFlags) {
  Register handler = allocator.useRegister(masm, handlerId);
  ValueOperand target = allocator.useValueRegister(masm, targetId);
  Register receiver = allocator.useRegister(masm, receiverId);
  Register callee = allocator.useRegister(masm, trapId);
  ValueOperand idVal;
  if constexpr (std::is_same_v<IdType, ValOperandId>) {
    idVal = allocator.useValueRegister(masm, id);
  }

  AutoScratchRegister code(allocator, masm);

  AutoScratchRegister scratch(allocator, masm);
  ValueOperand scratchVal(scratch);

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  // We need to keep the target around to potentially validate the proxy result
  stubFrame.storeTracedValue(masm, target);
  if constexpr (std::is_same_v<IdType, ValOperandId>) {
    stubFrame.storeTracedValue(masm, idVal);
#  ifdef DEBUG
    Label notPrivateSymbol;
    masm.branchTestSymbol(Assembler::NotEqual, idVal, &notPrivateSymbol);
    masm.unboxSymbol(idVal, scratch);
    masm.branch32(
        Assembler::NotEqual, Address(scratch, JS::Symbol::offsetOfCode()),
        Imm32(uint32_t(JS::SymbolCode::PrivateNameSymbol)), &notPrivateSymbol);
    masm.assumeUnreachable("Unexpected private field in callScriptedProxy");
    masm.bind(&notPrivateSymbol);
#  endif
  } else {
    // We need to either trace the id here or grab the ICStubReg back from
    // FramePointer + sizeof(void*) after the call in order to load it again.
    // We elect to do this because it unifies the code path after the call.
    Address idAddr(stubAddress(id));
    masm.loadPtr(idAddr, scratch);
    masm.tagValue(JSVAL_TYPE_STRING, scratch, scratchVal);
    stubFrame.storeTracedValue(masm, scratchVal);
  }

  uint16_t nargs = nargsAndFlags >> JSFunction::ArgCountShift;
  masm.alignJitStackBasedOnNArgs(std::max(uint16_t(3), nargs),
                                 /*countIncludesThis = */ false);
  for (size_t i = 3; i < nargs; i++) {
    masm.Push(UndefinedValue());
  }

  masm.tagValue(JSVAL_TYPE_OBJECT, receiver, scratchVal);
  masm.Push(scratchVal);

  if constexpr (std::is_same_v<IdType, ValOperandId>) {
    masm.Push(idVal);
  } else {
    stubFrame.loadTracedValue(masm, 1, scratchVal);
    masm.Push(scratchVal);
  }

  masm.Push(target);

  masm.tagValue(JSVAL_TYPE_OBJECT, handler, scratchVal);
  masm.Push(scratchVal);

  masm.loadJitCodeRaw(callee, code);

  masm.Push(callee);
  masm.Push(FrameDescriptor(FrameType::BaselineStub, 3));

  masm.callJit(code);

  Register scratch2 = code;

  Label success;
  stubFrame.loadTracedValue(masm, 0, scratchVal);
  masm.unboxObject(scratchVal, scratch);
  masm.branchTestObjectNeedsProxyResultValidation(Assembler::Zero, scratch,
                                                  scratch2, &success);
  ValueOperand scratchVal2(scratch2);
  stubFrame.loadTracedValue(masm, 1, scratchVal2);
  masm.Push(JSReturnOperand);
  masm.Push(scratchVal2);
  masm.Push(scratch);
  using Fn = bool (*)(JSContext*, HandleObject, HandleValue, HandleValue,
                      MutableHandleValue);
  callVM<Fn, CheckProxyGetByValueResult>(masm);

  masm.bind(&success);

  stubFrame.leave(masm);

  return true;
}

bool BaselineCacheIRCompiler::emitCallScriptedProxyGetResult(
    ValOperandId targetId, ObjOperandId receiverId, ObjOperandId handlerId,
    ObjOperandId trapId, uint32_t idOffset, uint32_t nargsAndFlags) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  return emitCallScriptedProxyGetShared(targetId, receiverId, handlerId, trapId,
                                        idOffset, nargsAndFlags);
}

bool BaselineCacheIRCompiler::emitCallScriptedProxyGetByValueResult(
    ValOperandId targetId, ObjOperandId receiverId, ObjOperandId handlerId,
    ValOperandId idId, ObjOperandId trapId, uint32_t nargsAndFlags) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  return emitCallScriptedProxyGetShared(targetId, receiverId, handlerId, trapId,
                                        idId, nargsAndFlags);
}
#endif

bool BaselineCacheIRCompiler::emitCallBoundScriptedFunctionShared(
    ObjOperandId calleeId, ObjOperandId targetId, Int32OperandId argcId,
    CallFlags flags, uint32_t numBoundArgs, Maybe<uint32_t> icScriptOffset) {
  AutoOutputRegister output(*this);
  AutoScratchRegister scratch(allocator, masm);
  AutoScratchRegisterMaybeOutput scratch2(allocator, masm, output);
  AutoScratchRegisterMaybeOutputType scratch3(allocator, masm, output);

  Register calleeReg = allocator.useRegister(masm, calleeId);
  Register argcReg = allocator.useRegister(masm, argcId);

  bool isInlined = icScriptOffset.isSome();

  bool isConstructing = flags.isConstructing();
  bool isSameRealm = flags.isSameRealm();

  allocator.discardStack(masm);

  // Push a stub frame so that we can perform a non-tail call.
  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  if (isInlined) {
    stubFrame.pushInlinedICScript(masm, stubAddress(*icScriptOffset));
  }

  // Push all arguments, including |this|.
  pushBoundFunctionArguments(argcReg, calleeReg, scratch, scratch2, flags,
                             numBoundArgs, /* isJitCall = */ true);

  // Load the target JSFunction.
  Address boundTarget(calleeReg, BoundFunctionObject::offsetOfTargetSlot());
  masm.unboxObject(boundTarget, calleeReg);

  if (!isSameRealm) {
    masm.switchToObjectRealm(calleeReg, scratch);
  }

  // Update argc.
  masm.add32(Imm32(numBoundArgs), argcReg);

  if (isConstructing) {
    createThis(argcReg, calleeReg, scratch, scratch2, scratch3, flags,
               mozilla::Some(numBoundArgs));
  }

  // Load the start of the target JitCode.
  Register code = scratch2;
  if (isInlined) {
    masm.loadJitCodeRawNoIon(calleeReg, code, scratch);
  } else {
    masm.loadJitCodeRaw(calleeReg, code);
  }

  // Note that we use Push, not push, so that callJit will align the stack
  // properly on ARM.
  masm.PushCalleeToken(calleeReg, isConstructing);
  masm.PushFrameDescriptorForJitCall(FrameType::BaselineStub, argcReg, scratch,
                                     isInlined);

  masm.callJit(code);

  if (isConstructing) {
    updateReturnValue();
  }

  stubFrame.leave(masm);

  if (!isSameRealm) {
    masm.switchToBaselineFrameRealm(scratch);
  }

  return true;
}

bool BaselineCacheIRCompiler::emitCallBoundScriptedFunction(
    ObjOperandId calleeId, ObjOperandId targetId, Int32OperandId argcId,
    CallFlags flags, uint32_t numBoundArgs) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  return emitCallBoundScriptedFunctionShared(calleeId, targetId, argcId, flags,
                                             numBoundArgs, mozilla::Nothing());
}

bool BaselineCacheIRCompiler::emitCallInlinedBoundFunction(
    ObjOperandId calleeId, ObjOperandId targetId, Int32OperandId argcId,
    CallFlags flags, uint32_t icScriptOffset, uint32_t numBoundArgs) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  return emitCallBoundScriptedFunctionShared(calleeId, targetId, argcId, flags,
                                             numBoundArgs,
                                             mozilla::Some(icScriptOffset));
}

bool BaselineCacheIRCompiler::emitNewArrayObjectResult(uint32_t arrayLength,
                                                       uint32_t shapeOffset,
                                                       uint32_t siteOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  gc::AllocKind allocKind = GuessArrayGCKind(arrayLength);
  MOZ_ASSERT(gc::GetObjectFinalizeKind(&ArrayObject::class_) ==
             gc::FinalizeKind::None);
  MOZ_ASSERT(!IsFinalizedKind(allocKind));

  uint32_t slotCount = GetGCKindSlots(allocKind);
  MOZ_ASSERT(slotCount >= ObjectElements::VALUES_PER_HEADER);
  uint32_t arrayCapacity = slotCount - ObjectElements::VALUES_PER_HEADER;

  AutoOutputRegister output(*this);
  AutoScratchRegister result(allocator, masm);
  AutoScratchRegister scratch(allocator, masm);
  AutoScratchRegister site(allocator, masm);
  AutoScratchRegisterMaybeOutput shape(allocator, masm, output);

  Address shapeAddr(stubAddress(shapeOffset));
  masm.loadPtr(shapeAddr, shape);

  Address siteAddr(stubAddress(siteOffset));
  masm.loadPtr(siteAddr, site);

  allocator.discardStack(masm);

  Label done;
  Label fail;

  masm.createArrayWithFixedElements(
      result, shape, scratch, InvalidReg, arrayLength, arrayCapacity, 0, 0,
      allocKind, gc::Heap::Default, &fail, AllocSiteInput(site));
  masm.jump(&done);

  {
    masm.bind(&fail);

    // We get here if the nursery is full (unlikely) but also for tenured
    // allocations if the current arena is full and we need to allocate a new
    // one (fairly common).

    AutoStubFrame stubFrame(*this);
    stubFrame.enter(masm, scratch);

    masm.Push(site);
    masm.Push(Imm32(int32_t(allocKind)));
    masm.Push(Imm32(arrayLength));

    using Fn =
        ArrayObject* (*)(JSContext*, uint32_t, gc::AllocKind, gc::AllocSite*);
    callVM<Fn, NewArrayObjectBaselineFallback>(masm);

    stubFrame.leave(masm);
    masm.storeCallPointerResult(result);
  }

  masm.bind(&done);
  masm.tagValue(JSVAL_TYPE_OBJECT, result, output.valueReg());
  return true;
}

bool BaselineCacheIRCompiler::emitNewPlainObjectResult(uint32_t numFixedSlots,
                                                       uint32_t numDynamicSlots,
                                                       gc::AllocKind allocKind,
                                                       uint32_t shapeOffset,
                                                       uint32_t siteOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  AutoScratchRegister obj(allocator, masm);
  AutoScratchRegister scratch(allocator, masm);
  AutoScratchRegister site(allocator, masm);
  AutoScratchRegisterMaybeOutput shape(allocator, masm, output);

  Address shapeAddr(stubAddress(shapeOffset));
  masm.loadPtr(shapeAddr, shape);

  Address siteAddr(stubAddress(siteOffset));
  masm.loadPtr(siteAddr, site);

  allocator.discardStack(masm);

  Label done;
  Label fail;

  masm.createPlainGCObject(obj, shape, scratch, shape, numFixedSlots,
                           numDynamicSlots, allocKind, gc::Heap::Default, &fail,
                           AllocSiteInput(site));
  masm.jump(&done);

  {
    masm.bind(&fail);

    // We get here if the nursery is full (unlikely) but also for tenured
    // allocations if the current arena is full and we need to allocate a new
    // one (fairly common).

    AutoStubFrame stubFrame(*this);
    stubFrame.enter(masm, scratch);

    masm.Push(site);
    masm.Push(Imm32(int32_t(allocKind)));
    masm.loadPtr(shapeAddr, shape);  // This might have been overwritten.
    masm.Push(shape);

    using Fn = JSObject* (*)(JSContext*, Handle<SharedShape*>, gc::AllocKind,
                             gc::AllocSite*);
    callVM<Fn, NewPlainObjectBaselineFallback>(masm);

    stubFrame.leave(masm);
    masm.storeCallPointerResult(obj);
  }

  masm.bind(&done);
  masm.tagValue(JSVAL_TYPE_OBJECT, obj, output.valueReg());
  return true;
}

bool BaselineCacheIRCompiler::emitNewFunctionCloneResult(
    uint32_t canonicalOffset, gc::AllocKind allocKind, uint32_t siteOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  AutoScratchRegisterMaybeOutput result(allocator, masm, output);
  AutoScratchRegisterMaybeOutputType site(allocator, masm, output);
  AutoScratchRegister canonical(allocator, masm);
  AutoScratchRegister envChain(allocator, masm);
  AutoScratchRegister scratch(allocator, masm);
  MOZ_ASSERT(result.get() != site.get());

  // Load the canonical function and the frame's environment chain.
  masm.loadPtr(stubAddress(canonicalOffset), canonical);
  Address envAddr(baselineFrameReg_,
                  BaselineFrame::reverseOffsetOfEnvironmentChain());
  masm.loadPtr(envAddr, envChain);

  Address siteAddr(stubAddress(siteOffset));
  masm.loadPtr(siteAddr, site);

  allocator.discardStack(masm);

  // Try to allocate a new function object in JIT code.
  Label done, fail;

  masm.createFunctionClone(result, canonical, envChain, scratch, allocKind,
                           &fail, AllocSiteInput(site));
  masm.jump(&done);

  {
    masm.bind(&fail);

    AutoStubFrame stubFrame(*this);
    stubFrame.enter(masm, scratch);

    masm.PushRegs(site, envChain, canonical);

    using Fn =
        JSObject* (*)(JSContext*, HandleFunction, HandleObject, gc::AllocSite*);
    callVM<Fn, js::LambdaBaselineFallback>(masm);

    stubFrame.leave(masm);
    masm.storeCallPointerResult(result);
  }

  masm.bind(&done);
  masm.tagValue(JSVAL_TYPE_OBJECT, result, output.valueReg());
  return true;
}

bool BaselineCacheIRCompiler::emitCloseIterScriptedResult(
    ObjOperandId iterId, ObjOperandId calleeId, uint32_t calleeNargs) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);
  Register iter = allocator.useRegister(masm, iterId);
  Register callee = allocator.useRegister(masm, calleeId);

  AutoScratchRegister code(allocator, masm);
  AutoScratchRegister scratch(allocator, masm);

  masm.loadJitCodeRaw(callee, code);

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  // Call the return method.
  masm.alignJitStackBasedOnNArgs(calleeNargs, /*countIncludesThis = */ false);
  for (uint32_t i = 0; i < calleeNargs; i++) {
    masm.pushValue(UndefinedValue());
  }
  masm.Push(TypedOrValueRegister(MIRType::Object, AnyRegister(iter)));
  masm.Push(callee);
  masm.Push(FrameDescriptor(FrameType::BaselineStub, /* argc = */ 0));

  masm.callJit(code);

  // Verify that the return value is an object.
  Label success;
  masm.branchTestObject(Assembler::Equal, JSReturnOperand, &success);

  masm.Push(Imm32(int32_t(CheckIsObjectKind::IteratorReturn)));
  using Fn = bool (*)(JSContext*, CheckIsObjectKind);
  callVM<Fn, ThrowCheckIsObject>(masm);

  masm.bind(&success);

  stubFrame.leave(masm);
  return true;
}

static void CallRegExpStub(MacroAssembler& masm, size_t jitZoneStubOffset,
                           Register temp, Label* vmCall) {
  // Call jitZone()->regExpStub. We store a pointer to the RegExp
  // stub in the IC stub to keep it alive, but we shouldn't use it if the stub
  // has been discarded in the meantime (because we might have changed GC string
  // pretenuring heuristics that affect behavior of the stub). This is uncommon
  // but can happen if we discarded all JIT code but had some active (Baseline)
  // scripts on the stack.
  masm.movePtr(ImmPtr(masm.realm()->zone()->jitZone()), temp);
  masm.loadPtr(Address(temp, jitZoneStubOffset), temp);
  masm.branchTestPtr(Assembler::Zero, temp, temp, vmCall);
  masm.call(Address(temp, JitCode::offsetOfCode()));
}

// Used to move inputs to the registers expected by the RegExp stub.
static void SetRegExpStubInputRegisters(MacroAssembler& masm,
                                        Register* regexpSrc,
                                        Register regexpDest, Register* inputSrc,
                                        Register inputDest,
                                        Register* lastIndexSrc,
                                        Register lastIndexDest) {
  MoveResolver& moves = masm.moveResolver();
  if (*regexpSrc != regexpDest) {
    masm.propagateOOM(moves.addMove(MoveOperand(*regexpSrc),
                                    MoveOperand(regexpDest), MoveOp::GENERAL));
    *regexpSrc = regexpDest;
  }
  if (*inputSrc != inputDest) {
    masm.propagateOOM(moves.addMove(MoveOperand(*inputSrc),
                                    MoveOperand(inputDest), MoveOp::GENERAL));
    *inputSrc = inputDest;
  }
  if (lastIndexSrc && *lastIndexSrc != lastIndexDest) {
    masm.propagateOOM(moves.addMove(MoveOperand(*lastIndexSrc),
                                    MoveOperand(lastIndexDest), MoveOp::INT32));
    *lastIndexSrc = lastIndexDest;
  }

  masm.propagateOOM(moves.resolve());

  MoveEmitter emitter(masm);
  emitter.emit(moves);
  emitter.finish();
}

bool BaselineCacheIRCompiler::emitCallRegExpMatcherResult(
    ObjOperandId regexpId, StringOperandId inputId, Int32OperandId lastIndexId,
    uint32_t stubOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  Register regexp = allocator.useRegister(masm, regexpId);
  Register input = allocator.useRegister(masm, inputId);
  Register lastIndex = allocator.useRegister(masm, lastIndexId);
  Register scratch = output.valueReg().scratchReg();

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  SetRegExpStubInputRegisters(masm, &regexp, RegExpMatcherRegExpReg, &input,
                              RegExpMatcherStringReg, &lastIndex,
                              RegExpMatcherLastIndexReg);

  masm.reserveStack(RegExpReservedStack);

  Label done, vmCall, vmCallNoMatches;
  CallRegExpStub(masm, JitZone::offsetOfRegExpMatcherStub(), scratch,
                 &vmCallNoMatches);
  masm.branchTestUndefined(Assembler::Equal, JSReturnOperand, &vmCall);

  masm.jump(&done);

  {
    Label pushedMatches;
    masm.bind(&vmCallNoMatches);
    masm.push(ImmWord(0));
    masm.jump(&pushedMatches);

    masm.bind(&vmCall);
    masm.computeEffectiveAddress(
        Address(masm.getStackPointer(), InputOutputDataSize), scratch);
    masm.Push(scratch);

    masm.bind(&pushedMatches);
    masm.PushRegs(lastIndex, input, regexp);

    using Fn = bool (*)(JSContext*, HandleObject regexp, HandleString input,
                        int32_t lastIndex, MatchPairs* pairs,
                        MutableHandleValue output);
    callVM<Fn, RegExpMatcherRaw>(masm);
  }

  masm.bind(&done);

  static_assert(R0 == JSReturnOperand);

  stubFrame.leave(masm);
  return true;
}

bool BaselineCacheIRCompiler::emitCallRegExpSearcherResult(
    ObjOperandId regexpId, StringOperandId inputId, Int32OperandId lastIndexId,
    uint32_t stubOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  Register regexp = allocator.useRegister(masm, regexpId);
  Register input = allocator.useRegister(masm, inputId);
  Register lastIndex = allocator.useRegister(masm, lastIndexId);
  Register scratch = output.valueReg().scratchReg();

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  SetRegExpStubInputRegisters(masm, &regexp, RegExpSearcherRegExpReg, &input,
                              RegExpSearcherStringReg, &lastIndex,
                              RegExpSearcherLastIndexReg);
  // Ensure `scratch` doesn't conflict with the stub's input registers.
  scratch = ReturnReg;

  masm.reserveStack(RegExpReservedStack);

  Label done, vmCall;
  CallRegExpStub(masm, JitZone::offsetOfRegExpSearcherStub(), scratch, &vmCall);
  masm.branch32(Assembler::NotEqual, scratch, Imm32(RegExpSearcherResultFailed),
                &done);

  {
    masm.bind(&vmCall);
    masm.PushRegs(lastIndex, input, regexp);

    using Fn = bool (*)(JSContext*, HandleObject regexp, HandleString input,
                        int32_t lastIndex, int32_t* result);
    callVM<Fn, RegExpSearcherRaw>(masm);
  }

  masm.bind(&done);

  masm.tagValue(JSVAL_TYPE_INT32, ReturnReg, output.valueReg());

  stubFrame.leave(masm);
  return true;
}

bool BaselineCacheIRCompiler::emitRegExpBuiltinExecMatchResult(
    ObjOperandId regexpId, StringOperandId inputId, uint32_t stubOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  Register regexp = allocator.useRegister(masm, regexpId);
  Register input = allocator.useRegister(masm, inputId);
  Register scratch = output.valueReg().scratchReg();

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  SetRegExpStubInputRegisters(masm, &regexp, RegExpMatcherRegExpReg, &input,
                              RegExpMatcherStringReg, nullptr, InvalidReg);

  masm.reserveStack(RegExpReservedStack);

  Label done, vmCall, vmCallNoMatches;
  CallRegExpStub(masm, JitZone::offsetOfRegExpExecMatchStub(), scratch,
                 &vmCallNoMatches);
  masm.branchTestUndefined(Assembler::Equal, JSReturnOperand, &vmCall);

  masm.jump(&done);

  {
    Label pushedMatches;
    masm.bind(&vmCallNoMatches);
    masm.push(ImmWord(0));
    masm.jump(&pushedMatches);

    masm.bind(&vmCall);
    masm.computeEffectiveAddress(
        Address(masm.getStackPointer(), InputOutputDataSize), scratch);
    masm.Push(scratch);

    masm.bind(&pushedMatches);
    masm.PushRegs(input, regexp);

    using Fn =
        bool (*)(JSContext*, Handle<RegExpObject*> regexp, HandleString input,
                 MatchPairs* pairs, MutableHandleValue output);
    callVM<Fn, RegExpBuiltinExecMatchFromJit>(masm);
  }

  masm.bind(&done);

  static_assert(R0 == JSReturnOperand);

  stubFrame.leave(masm);
  return true;
}

bool BaselineCacheIRCompiler::emitRegExpBuiltinExecTestResult(
    ObjOperandId regexpId, StringOperandId inputId, uint32_t stubOffset) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  Register regexp = allocator.useRegister(masm, regexpId);
  Register input = allocator.useRegister(masm, inputId);
  Register scratch = output.valueReg().scratchReg();

  allocator.discardStack(masm);

  AutoStubFrame stubFrame(*this);
  stubFrame.enter(masm, scratch);

  SetRegExpStubInputRegisters(masm, &regexp, RegExpExecTestRegExpReg, &input,
                              RegExpExecTestStringReg, nullptr, InvalidReg);

  // Ensure `scratch` doesn't conflict with the stub's input registers.
  scratch = ReturnReg;

  masm.reserveStack(RegExpReservedStack);

  Label done, vmCall;
  CallRegExpStub(masm, JitZone::offsetOfRegExpExecTestStub(), scratch, &vmCall);
  masm.branch32(Assembler::Equal, scratch, Imm32(RegExpExecTestResultFailed),
                &vmCall);

  masm.jump(&done);

  {
    masm.bind(&vmCall);

    masm.PushRegs(input, regexp);

    using Fn = bool (*)(JSContext*, Handle<RegExpObject*> regexp,
                        HandleString input, bool* result);
    callVM<Fn, RegExpBuiltinExecTestFromJit>(masm);
  }

  masm.bind(&done);

  masm.tagValue(JSVAL_TYPE_BOOLEAN, ReturnReg, output.valueReg());

  stubFrame.leave(masm);
  return true;
}

bool BaselineCacheIRCompiler::emitRegExpHasCaptureGroupsResult(
    ObjOperandId regexpId, StringOperandId inputId) {
  JitSpew(JitSpew_Codegen, "%s", __FUNCTION__);

  AutoOutputRegister output(*this);
  Register regexp = allocator.useRegister(masm, regexpId);
  Register input = allocator.useRegister(masm, inputId);
  AutoScratchRegisterMaybeOutput scratch(allocator, masm, output);

  allocator.discardStack(masm);

  // Load RegExpShared in |scratch|.
  Label vmCall;
  masm.loadParsedRegExpShared(regexp, scratch, &vmCall);

  // Return true iff pairCount > 1.
  Label returnTrue, done;
  masm.branch32(Assembler::Above,
                Address(scratch, RegExpShared::offsetOfPairCount()), Imm32(1),
                &returnTrue);
  masm.moveValue(BooleanValue(false), output.valueReg());
  masm.jump(&done);

  masm.bind(&returnTrue);
  masm.moveValue(BooleanValue(true), output.valueReg());
  masm.jump(&done);

  {
    masm.bind(&vmCall);

    AutoStubFrame stubFrame(*this);
    stubFrame.enter(masm, scratch);

    masm.PushRegs(input, regexp);

    using Fn =
        bool (*)(JSContext*, Handle<RegExpObject*>, Handle<JSString*>, bool*);
    callVM<Fn, RegExpHasCaptureGroups>(masm);

    stubFrame.leave(masm);
    masm.storeCallBoolResult(scratch);
    masm.tagValue(JSVAL_TYPE_BOOLEAN, scratch, output.valueReg());
  }

  masm.bind(&done);
  return true;
}
