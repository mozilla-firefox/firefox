/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/loong64/CodeGenerator-loong64.h"

#include "mozilla/MathAlgorithms.h"
#include "mozilla/Maybe.h"

#include <bit>

#include "builtin/Number.h"
#include "jit/CodeGenerator.h"
#include "jit/InlineScriptTree.h"
#include "jit/JitRuntime.h"
#include "jit/MIR-wasm.h"
#include "jit/MIR.h"
#include "jit/MIRGraph.h"
#include "jit/RangeAnalysis.h"
#include "jit/ReciprocalMulConstants.h"
#include "vm/JSContext.h"
#include "vm/Realm.h"
#include "vm/Shape.h"

#include "jit/shared/CodeGenerator-shared-inl.h"
#include "vm/JSScript-inl.h"

using namespace js;
using namespace js::jit;

using JS::GenericNaN;
using mozilla::NegativeInfinity;

template <class LDivOrMod>
static void DivideWithConstant(MacroAssembler& masm, LDivOrMod* ins) {
  const Register lhs = ToRegister(ins->numerator());
  const Register output = ToRegister(ins->output());
  const int32_t d = ins->denominator();

  UseScratchRegisterScope temps(masm);
  const Register temp = temps.Acquire();

  // The absolute value of the denominator isn't a power of 2.
  MOZ_ASSERT(!std::has_single_bit(mozilla::Abs(d)));

  const auto* const mir = ins->mir();

  // We will first divide by Abs(d), and negate the answer if d is negative.
  // If desired, this can be avoided by generalizing computeDivisionConstants.
  const auto rmc = ReciprocalMulConstants::computeSignedDivisionConstants(d);

  // We first compute hi_i32(M * n), where M = rmc.multiplier.
  masm.ma_li(temp, Imm32(rmc.multiplier));
  masm.as_mulh_w(output, lhs, temp);
  if (rmc.multiplier > INT32_MAX) {
    MOZ_ASSERT(rmc.multiplier < (static_cast<int64_t>(1) << 32));
    // We actually computed output = hi_i32(int32_t(M) * n) instead. Since
    // hi_i32(M * n) is the same as (output + n), we can correct for the
    // overflow. (output + n) can't overflow, as n and |output| have opposite
    // signs because int32_t(M) is negative.
    masm.as_add_w(output, output, lhs);
  }

  // hi_i32(M * n) >> shift is the truncated division answer if n is
  // non-negative, as proved in the comments of computeDivisionConstants. We
  // must add 1 later if n is negative to get the right answer in all cases.
  if (rmc.shiftAmount > 0) {
    masm.as_srai_w(output, output, rmc.shiftAmount);
  }

  // We'll subtract -1 instead of adding 1, because (n < 0 ? -1 : 0) can be
  // computed with just a sign-extending shift of 31 bits.
  if (mir->canBeNegativeDividend()) {
    masm.as_srai_w(temp, lhs, 31);
    masm.as_sub_w(output, output, temp);
  }

  // After this, |output| contains the correct truncated division result.
  if (d < 0) {
    masm.neg32(output);
  }
}

template <class LUDivOrUMod>
static void UnsignedDivideWithConstant(MacroAssembler& masm, LUDivOrUMod* ins) {
  const Register lhs = ToRegister(ins->numerator());
  const Register output = ToRegister(ins->output());
  const uint32_t d = ins->denominator();

  UseScratchRegisterScope temps(masm);
  const Register temp = temps.Acquire();

  // The denominator isn't a power of 2 (see LDivPowTwoI).
  MOZ_ASSERT(!std::has_single_bit(d));

  const auto rmc = ReciprocalMulConstants::computeUnsignedDivisionConstants(d);

  // We first compute hi_u32(M * n), where M = rmc.multiplier.
  masm.ma_li(temp, Imm32(rmc.multiplier));
  masm.as_mulh_wu(output, lhs, temp);
  if (rmc.multiplier > UINT32_MAX) {
    // M >= 2^32 and shift == 0 is impossible, as d >= 2 implies that
    // (hi_u32(M * n) >> shift) >= n > floor(n/d) whenever n >= d,
    // contradicting the proof of correctness in computeDivisionConstants
    MOZ_ASSERT(rmc.shiftAmount > 0);
    MOZ_ASSERT(rmc.multiplier < (static_cast<int64_t>(1) << 33));

    // We actually computed output = hi_u32(uint32_t(M) * n) instead. Since
    // hi_u32(M * n) >> shift is the same as (output + n) >> shift, we can
    // correct for the overflow. This case is a bit trickier than the signed
    // case, though, as the (output + n) addition itself can overflow; however,
    // note that
    //  (output + n) >> shift == (((n - output) >> 1) + output) >> (shift - 1),
    // which is overflow-free. See Hacker's Delight, section 10-8 for details.

    // Compute (n - output) >> 1 into temp.
    masm.as_sub_w(temp, lhs, output);
    masm.as_srli_w(temp, temp, 1);

    // Finish the computation.
    masm.as_add_w(output, output, temp);
    if (rmc.shiftAmount > 1) {
      masm.as_srli_w(output, output, rmc.shiftAmount - 1);
    }
  } else if (rmc.shiftAmount > 0) {
    masm.as_srli_w(output, output, rmc.shiftAmount);
  }
}

template <class LDivOrMod>
static void Divide64WithConstant(MacroAssembler& masm, LDivOrMod* ins) {
  const Register lhs = ToRegister(ins->numerator());
  const Register output = ToRegister(ins->output());
  const int64_t d = ins->denominator();

  UseScratchRegisterScope temps(masm);
  const Register temp = temps.Acquire();

  // The absolute value of the denominator isn't a power of 2.
  MOZ_ASSERT(!std::has_single_bit(mozilla::Abs(d)));

  const auto* const mir = ins->mir();

  // We will first divide by Abs(d), and negate the answer if d is negative.
  // If desired, this can be avoided by generalizing computeDivisionConstants.
  const auto rmc = ReciprocalMulConstants::computeSignedDivisionConstants(d);

  // We first compute hi_i64(M * n), where M = rmc.multiplier.
  masm.ma_li(temp, ImmWord(uint64_t(rmc.multiplier)));
  masm.as_mulh_d(output, lhs, temp);
  if (rmc.multiplier > Int128(INT64_MAX)) {
    MOZ_ASSERT(rmc.multiplier < (Int128(1) << 64));

    // We actually computed output = hi_i64(int64_t(M) * n) instead. Since
    // hi_i64(M * n) is the same as (output + n), we can correct for the
    // overflow. (output + n) can't overflow, as n and output have opposite
    // signs because int64_t(M) is negative.
    masm.as_add_d(output, output, lhs);
  }

  // hi_i64(M * n) >> shift is the truncated division answer if n is
  // non-negative, as proved in the comments of computeDivisionConstants. We
  // must add 1 later if n is negative to get the right answer in all cases.
  if (rmc.shiftAmount > 0) {
    masm.as_srai_d(output, output, rmc.shiftAmount);
  }

  // We'll subtract -1 instead of adding 1, because (n < 0 ? -1 : 0) can be
  // computed with just a sign-extending shift of 63 bits.
  if (mir->canBeNegativeDividend()) {
    masm.as_srai_d(temp, lhs, 63);
    masm.as_sub_d(output, output, temp);
  }

  // After this, |output| contains the correct truncated division result.
  if (d < 0) {
    masm.as_sub_d(output, zero, output);
  }
}

template <class LUDivOrUMod>
static void UnsignedDivide64WithConstant(MacroAssembler& masm,
                                         LUDivOrUMod* ins) {
  const Register lhs = ToRegister(ins->numerator());
  const Register output = ToRegister(ins->output());
  const uint64_t d = ins->denominator();

  UseScratchRegisterScope temps(masm);
  const Register temp = temps.Acquire();

  // The denominator isn't a power of 2 (see LDivPowTwoI).
  MOZ_ASSERT(!std::has_single_bit(d));

  const auto rmc = ReciprocalMulConstants::computeUnsignedDivisionConstants(d);

  // We first compute hi_u64(M * n), where M = rmc.multiplier.
  masm.ma_li(temp, ImmWord(uint64_t(rmc.multiplier)));
  masm.as_mulh_du(output, lhs, temp);
  if (rmc.multiplier > Int128(UINT64_MAX)) {
    // M >= 2^64 and shift == 0 is impossible, as d >= 2 implies that
    // (hi_u64(M * n) >> shift) >= n > floor(n/d) whenever n >= d,
    // contradicting the proof of correctness in computeDivisionConstants.
    MOZ_ASSERT(rmc.shiftAmount > 0);
    MOZ_ASSERT(rmc.multiplier < (Int128(1) << 65));

    // We actually computed output = hi_u64(uint64_t(M) * n) instead. Since
    // hi_u64(M * n) >> shift is the same as (output + n) >> shift, we can
    // correct for the overflow. This case is a bit trickier than the signed
    // case, though, as the (output + n) addition itself can overflow; however,
    // note that
    //  (output + n) >> shift == (((n - output) >> 1) + output) >> (shift - 1),
    // which is overflow-free. See Hacker's Delight, section 10-8 for details.

    // Compute (n - output) >> 1 into temp.
    masm.as_sub_d(temp, lhs, output);
    masm.as_srli_d(temp, temp, 1);

    // Finish the computation.
    masm.as_add_d(output, output, temp);
    masm.as_srli_d(output, output, rmc.shiftAmount - 1);
  } else if (rmc.shiftAmount > 0) {
    masm.as_srli_d(output, output, rmc.shiftAmount);
  }
}

// If we have a constant base ptr, try to add the offset to it, to generate
// better code when the full address is known.  The addition may overflow past
// 32 bits because the front end does nothing special if the base is a large
// constant and base+offset overflows; sidestep this by performing the addition
// anyway, overflowing to 64-bit.
static mozilla::Maybe<uint64_t> ToAbsoluteAddress(
    const LAllocation* ptr, const wasm::MemoryAccessDesc& access) {
  if (!ptr->isConstantValue()) {
    return mozilla::Nothing();
  }

  const MConstant* constant = ptr->toConstant();
  uint64_t baseAddress = constant->type() == MIRType::Int32
                             ? uint64_t(uint32_t(constant->toInt32()))
                             : uint64_t(constant->toInt64());
  return mozilla::Some(baseAddress + uint64_t(access.offset32()));
}

template <class Inst>
static wasm::ZeroExtendIndex ToZeroExtendIndex(const Inst* ins) {
  if (ins->mir()->base()->type() == MIRType::Int32) {
    return wasm::ZeroExtendIndex::Yes;
  }
  return wasm::ZeroExtendIndex::No;
}

// shared
CodeGeneratorLOONG64::CodeGeneratorLOONG64(
    MIRGenerator* gen, LIRGraph* graph, MacroAssembler* masm,
    const wasm::CodeMetadata* wasmCodeMeta)
    : CodeGeneratorShared(gen, graph, masm, wasmCodeMeta) {}

Operand CodeGeneratorLOONG64::ToOperand(const LAllocation& a) {
  if (a.isGeneralReg()) {
    return Operand(a.toGeneralReg()->reg());
  }
  if (a.isFloatReg()) {
    return Operand(a.toFloatReg()->reg());
  }
  return Operand(ToAddress(a));
}

Operand CodeGeneratorLOONG64::ToOperand(const LAllocation* a) {
  return ToOperand(*a);
}

Operand CodeGeneratorLOONG64::ToOperand(const LDefinition* def) {
  return ToOperand(def->output());
}

void CodeGeneratorLOONG64::branchToBlock(Assembler::FloatFormat fmt,
                                         FloatRegister lhs, FloatRegister rhs,
                                         MBasicBlock* mir,
                                         Assembler::DoubleCondition cond) {
  // Skip past trivial blocks.
  Label* label = skipTrivialBlocks(mir)->lir()->label();
  if (fmt == Assembler::DoubleFloat) {
    masm.branchDouble(cond, lhs, rhs, label);
  } else {
    masm.branchFloat(cond, lhs, rhs, label);
  }
}

MoveOperand CodeGeneratorLOONG64::toMoveOperand(LAllocation a) const {
  if (a.isGeneralReg()) {
    return MoveOperand(ToRegister(a));
  }
  if (a.isFloatReg()) {
    return MoveOperand(ToFloatRegister(a));
  }
  MoveOperand::Kind kind = a.isStackArea() ? MoveOperand::Kind::EffectiveAddress
                                           : MoveOperand::Kind::Memory;
  Address address = ToAddress(a);
  MOZ_ASSERT((address.offset & 3) == 0);

  return MoveOperand(address, kind);
}

bool CodeGeneratorLOONG64::generateOutOfLineCode() {
  if (!CodeGeneratorShared::generateOutOfLineCode()) {
    return false;
  }

  if (needsDeoptLabel()) {
    // All non-table-based bailouts will go here.
    masm.bind(&deoptLabel_);

    // Push the frame size, so the handler can recover the IonScript.
    // Frame size is stored in 'ra' and pushed by GenerateBailoutThunk
    // We have to use 'ra' because generateBailoutTable will implicitly do
    // the same.
    masm.move32(Imm32(frameSize()), ra);

    TrampolinePtr handler = gen->jitRuntime()->getGenericBailoutHandler();
    masm.jump(handler);
  }

  return !masm.oom();
}

class js::jit::OutOfLineTableSwitch
    : public OutOfLineCodeBase<CodeGeneratorLOONG64> {
  MTableSwitch* mir_;
  CodeLabel jumpLabel_;

  void accept(CodeGeneratorLOONG64* codegen) {
    codegen->visitOutOfLineTableSwitch(this);
  }

 public:
  explicit OutOfLineTableSwitch(MTableSwitch* mir) : mir_(mir) {}

  MTableSwitch* mir() const { return mir_; }

  CodeLabel* jumpLabel() { return &jumpLabel_; }
};

void CodeGeneratorLOONG64::emitTableSwitchDispatch(MTableSwitch* mir,
                                                   Register index,
                                                   Register base) {
  Label* defaultcase = skipTrivialBlocks(mir->getDefault())->lir()->label();

  // Lower value with low value
  if (mir->low() != 0) {
    masm.subPtr(Imm32(mir->low()), index);
  }

  // Jump to default case if input is out of range
  int32_t cases = mir->numCases();
  masm.branchPtr(Assembler::AboveOrEqual, index, ImmWord(cases), defaultcase);

  // To fill in the CodeLabels for the case entries, we need to first
  // generate the case entries (we don't yet know their offsets in the
  // instruction stream).
  OutOfLineTableSwitch* ool = new (alloc()) OutOfLineTableSwitch(mir);
  addOutOfLineCode(ool, mir);

  // Compute the position where a pointer to the right case stands.
  masm.ma_li(base, ool->jumpLabel());

  BaseIndex pointer(base, index, ScalePointer);

  // Jump to the right case
  masm.branchToComputedAddress(pointer);
}

template <typename T>
void CodeGeneratorLOONG64::emitWasmLoad(T* lir) {
  const MWasmLoad* mir = lir->mir();
  const auto& access = mir->access();

  Register memoryBase = ToRegister(lir->memoryBase());
  AnyRegister output = ToAnyRegister(lir->output());

  if (auto address = ToAbsoluteAddress(lir->ptr(), access)) {
    masm.wasmLoadAbsolute(access, memoryBase, address.value(), output);
    return;
  }

  UseScratchRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  Register ptr = ToRegister(lir->ptr());
  Register ptrScratch = ToTempRegisterOrInvalid(lir->temp0());

  if (mir->base()->type() == MIRType::Int32) {
    masm.move32To64ZeroExtend(ptr, Register64(scratch));
    ptr = scratch;
    ptrScratch = ptrScratch != InvalidReg ? scratch : InvalidReg;
  }

  // ptr is a GPR and is either a 32-bit value zero-extended to 64-bit, or a
  // true 64-bit value.
  masm.wasmLoad(access, memoryBase, ptr, ptrScratch, output);
}

template <typename T>
void CodeGeneratorLOONG64::emitWasmStore(T* lir) {
  const MWasmStore* mir = lir->mir();
  const auto& access = mir->access();

  AnyRegister value;
  if (lir->value()->isBogus()) {
    value = AnyRegister(zero);
  } else {
    value = ToAnyRegister(lir->value());
  }
  Register memoryBase = ToRegister(lir->memoryBase());

  if (auto address = ToAbsoluteAddress(lir->ptr(), access)) {
    masm.wasmStoreAbsolute(access, value, memoryBase, address.value());
    return;
  }

  UseScratchRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  Register ptr = ToRegister(lir->ptr());
  Register ptrScratch = ToTempRegisterOrInvalid(lir->temp0());

  if (mir->base()->type() == MIRType::Int32) {
    masm.move32To64ZeroExtend(ptr, Register64(scratch));
    ptr = scratch;
    ptrScratch = ptrScratch != InvalidReg ? scratch : InvalidReg;
  }

  // ptr is a GPR and is either a 32-bit value zero-extended to 64-bit, or a
  // true 64-bit value.
  masm.wasmStore(access, value, memoryBase, ptr, ptrScratch);
}

void CodeGeneratorLOONG64::generateInvalidateEpilogue() {
  // Ensure that there is enough space in the buffer for the OsiPoint
  // patching to occur. Otherwise, we could overwrite the invalidation
  // epilogue
  for (size_t i = 0; i < sizeof(void*); i += Assembler::NopSize()) {
    masm.nop();
  }

  masm.bind(&invalidate_);

  // Push the return address of the point that we bailed out at to the stack
  masm.Push(ra);

  // Push the Ion script onto the stack (when we determine what that
  // pointer is).
  invalidateEpilogueData_ = masm.pushWithPatch(ImmWord(uintptr_t(-1)));

  // Jump to the invalidator which will replace the current frame.
  TrampolinePtr thunk = gen->jitRuntime()->getInvalidationThunk();

  masm.jump(thunk);
}

void CodeGeneratorLOONG64::visitOutOfLineTableSwitch(
    OutOfLineTableSwitch* ool) {
  MTableSwitch* mir = ool->mir();

  masm.haltingAlign(sizeof(void*));
  masm.bind(ool->jumpLabel());
  masm.addCodeLabel(*ool->jumpLabel());

  for (size_t i = 0; i < mir->numCases(); i++) {
    LBlock* caseblock = skipTrivialBlocks(mir->getCase(i))->lir();
    Label* caseheader = caseblock->label();
    uint32_t caseoffset = caseheader->offset();

    // The entries of the jump table need to be absolute addresses and thus
    // must be patched after codegen is finished.
    CodeLabel cl;
    masm.writeCodePointer(&cl);
    cl.target()->bind(caseoffset);
    masm.addCodeLabel(cl);
  }
}

void CodeGeneratorLOONG64::visitOutOfLineWasmTruncateCheck(
    OutOfLineWasmTruncateCheck* ool) {
  if (ool->toType() == MIRType::Int32) {
    masm.outOfLineWasmTruncateToInt32Check(
        ool->input(), ool->output(), ool->fromType(), ool->flags(),
        ool->rejoin(), ool->trapSiteDesc(), nullptr, nullptr);
  } else {
    MOZ_ASSERT(ool->toType() == MIRType::Int64);
    masm.outOfLineWasmTruncateToInt64Check(
        ool->input(), ool->output64(), ool->fromType(), ool->flags(),
        ool->rejoin(), ool->trapSiteDesc(), nullptr, nullptr);
  }
}

void CodeGenerator::visitBox(LBox* box) {
  const LAllocation* in = box->payload();
  ValueOperand result = ToOutValue(box);

  masm.moveValue(TypedOrValueRegister(box->type(), ToAnyRegister(in)), result);
}

void CodeGenerator::visitUnbox(LUnbox* unbox) {
  MUnbox* mir = unbox->mir();

  Register result = ToRegister(unbox->output());

  if (mir->fallible()) {
    ValueOperand value = ToValue(unbox->input());
    Label bail;
    switch (mir->type()) {
      case MIRType::Int32:
        masm.fallibleUnboxInt32(value, result, &bail);
        break;
      case MIRType::Boolean:
        masm.fallibleUnboxBoolean(value, result, &bail);
        break;
      case MIRType::Object:
        masm.fallibleUnboxObject(value, result, &bail);
        break;
      case MIRType::String:
        masm.fallibleUnboxString(value, result, &bail);
        break;
      case MIRType::Symbol:
        masm.fallibleUnboxSymbol(value, result, &bail);
        break;
      case MIRType::BigInt:
        masm.fallibleUnboxBigInt(value, result, &bail);
        break;
      default:
        MOZ_CRASH("Given MIRType cannot be unboxed.");
    }
    bailoutFrom(&bail, unbox->snapshot());
    return;
  }

  LAllocation* input = unbox->getOperand(LUnbox::Input);
  if (input->isGeneralReg()) {
    Register inputReg = ToRegister(input);
    switch (mir->type()) {
      case MIRType::Int32:
        masm.unboxInt32(inputReg, result);
        break;
      case MIRType::Boolean:
        masm.unboxBoolean(inputReg, result);
        break;
      case MIRType::Object:
        masm.unboxObject(inputReg, result);
        break;
      case MIRType::String:
        masm.unboxString(inputReg, result);
        break;
      case MIRType::Symbol:
        masm.unboxSymbol(inputReg, result);
        break;
      case MIRType::BigInt:
        masm.unboxBigInt(inputReg, result);
        break;
      default:
        MOZ_CRASH("Given MIRType cannot be unboxed.");
    }
    return;
  }

  Address inputAddr = ToAddress(input);
  switch (mir->type()) {
    case MIRType::Int32:
      masm.unboxInt32(inputAddr, result);
      break;
    case MIRType::Boolean:
      masm.unboxBoolean(inputAddr, result);
      break;
    case MIRType::Object:
      masm.unboxObject(inputAddr, result);
      break;
    case MIRType::String:
      masm.unboxString(inputAddr, result);
      break;
    case MIRType::Symbol:
      masm.unboxSymbol(inputAddr, result);
      break;
    case MIRType::BigInt:
      masm.unboxBigInt(inputAddr, result);
      break;
    default:
      MOZ_CRASH("Given MIRType cannot be unboxed.");
  }
}

void CodeGenerator::visitDivOrModI64(LDivOrModI64* lir) {
  Register lhs = ToRegister(lir->lhs());
  Register rhs = ToRegister(lir->rhs());
  Register output = ToRegister(lir->output());

  Label done;

  // Handle divide by zero.
  if (lir->canBeDivideByZero()) {
    Label nonZero;
    masm.ma_b(rhs, rhs, &nonZero, Assembler::NonZero);
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, lir->trapSiteDesc());
    masm.bind(&nonZero);
  }

  // Handle an integer overflow exception from INT64_MIN / -1.
  if (lir->canBeNegativeOverflow()) {
    Label notOverflow;
    masm.branchPtr(Assembler::NotEqual, lhs, ImmWord(INT64_MIN), &notOverflow);
    masm.branchPtr(Assembler::NotEqual, rhs, ImmWord(-1), &notOverflow);
    if (lir->mir()->isMod()) {
      masm.as_xor(output, output, output);
    } else {
      masm.wasmTrap(wasm::Trap::IntegerOverflow, lir->trapSiteDesc());
    }
    masm.jump(&done);
    masm.bind(&notOverflow);
  }

  if (lir->mir()->isMod()) {
    masm.as_mod_d(output, lhs, rhs);
  } else {
    masm.as_div_d(output, lhs, rhs);
  }

  masm.bind(&done);
}

void CodeGenerator::visitDivConstantI64(LDivConstantI64* ins) {
  const int64_t d = ins->denominator();

  if (d == 0) {
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, ins->mir()->trapSiteDesc());
    return;
  }

  // Compute the truncated division result in |output|.
  Divide64WithConstant(masm, ins);
}

void CodeGenerator::visitModConstantI64(LModConstantI64* ins) {
  const Register lhs = ToRegister(ins->numerator());
  const Register output = ToRegister(ins->output());

  const int64_t d = ins->denominator();

  if (d == 0) {
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, ins->mir()->trapSiteDesc());
    return;
  }

  // Compute the truncated division result in |output|.
  Divide64WithConstant(masm, ins);

  // Compute the remainder: output = lhs - (output * d).
  masm.ma_mul_d(output, output, ImmWord(uint64_t(d)));
  masm.as_sub_d(output, lhs, output);
}

void CodeGenerator::visitDivPowTwoI64(LDivPowTwoI64* ins) {
  const Register numerator = ToRegister(ins->numerator());
  const Register output = ToRegister(ins->output());
  const int32_t shift = ins->shift();
  MOZ_ASSERT(0 <= shift && shift <= 63);
  const bool negativeDivisor = ins->negativeDivisor();
  const MDiv* const mir = ins->mir();

  if (shift != 0) {
    if (mir->isUnsigned()) {
      masm.as_srli_d(output, numerator, shift);
    } else {
      if (mir->canBeNegativeDividend()) {
        UseScratchRegisterScope temps(masm);
        const Register temp = temps.Acquire();

        // Adjust the numerator so that the arithmetic shift rounds negative
        // values toward zero. See 10-1 "Signed Division by a Known Power of 2"
        // in Henry S. Warren, Jr.'s Hacker's Delight.
        if (shift > 1) {
          masm.as_srai_d(temp, numerator, 63);
          masm.as_srli_d(temp, temp, 64 - shift);
        } else {
          masm.as_srli_d(temp, numerator, 64 - shift);
        }
        masm.as_add_d(temp, temp, numerator);
        masm.as_srai_d(output, temp, shift);
      } else {
        masm.as_srai_d(output, numerator, shift);
      }

      if (negativeDivisor) {
        masm.as_sub_d(output, zero, output);
      }
    }
    return;
  }

  if (negativeDivisor) {
    // INT64_MIN / -1 overflows.
    Label ok;
    masm.branchPtr(Assembler::NotEqual, numerator, ImmWord(INT64_MIN), &ok);
    masm.wasmTrap(wasm::Trap::IntegerOverflow, mir->trapSiteDesc());
    masm.bind(&ok);
    masm.as_sub_d(output, zero, numerator);
  } else {
    masm.movePtr(numerator, output);
  }
}

void CodeGenerator::visitModPowTwoI64(LModPowTwoI64* ins) {
  const Register input = ToRegister(ins->input());
  const Register output = ToRegister(ins->output());
  const int32_t shift = ins->shift();
  MOZ_ASSERT(0 <= shift && shift <= 63);
  const MMod* const mir = ins->mir();
  const bool canBeNegative = !mir->isUnsigned() && mir->canBeNegativeDividend();

  if (shift == 0) {
    masm.movePtr(zero, output);
    return;
  }

  Label negative;
  if (canBeNegative) {
    // Switch based on the sign of the dividend.
    masm.ma_b(input, input, &negative, Assembler::Signed, ShortJump);
  }

  // Extract the low |shift| bits, which is equivalent to modulo 2^shift.
  masm.as_bstrpick_d(output, input, shift - 1, 0);

  if (canBeNegative) {
    Label done;
    masm.ma_b(&done, ShortJump);

    // Negative numbers need a negate, bit extraction, negate.
    masm.bind(&negative);
    masm.as_sub_d(output, zero, input);
    masm.as_bstrpick_d(output, output, shift - 1, 0);
    masm.as_sub_d(output, zero, output);

    masm.bind(&done);
  }
}

void CodeGenerator::visitUDivConstantI64(LUDivConstantI64* ins) {
  const uint64_t d = ins->denominator();

  if (d == 0) {
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, ins->mir()->trapSiteDesc());
    return;
  }

  // Compute the truncated division result in |output|.
  UnsignedDivide64WithConstant(masm, ins);
}

void CodeGenerator::visitUModConstantI64(LUModConstantI64* ins) {
  const Register lhs = ToRegister(ins->numerator());
  const Register output = ToRegister(ins->output());

  const uint64_t d = ins->denominator();

  if (d == 0) {
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, ins->mir()->trapSiteDesc());
    return;
  }

  // Compute the truncated division result in |output|.
  UnsignedDivide64WithConstant(masm, ins);

  // Compute the remainder: output = lhs - (output * d).
  masm.ma_mul_d(output, output, ImmWord(d));
  masm.as_sub_d(output, lhs, output);
}

void CodeGenerator::visitUDivOrModI64(LUDivOrModI64* lir) {
  Register lhs = ToRegister(lir->lhs());
  Register rhs = ToRegister(lir->rhs());
  Register output = ToRegister(lir->output());

  Label done;

  // Prevent divide by zero.
  if (lir->canBeDivideByZero()) {
    Label nonZero;
    masm.ma_b(rhs, rhs, &nonZero, Assembler::NonZero);
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, lir->trapSiteDesc());
    masm.bind(&nonZero);
  }

  if (lir->mir()->isMod()) {
    masm.as_mod_du(output, lhs, rhs);
  } else {
    masm.as_div_du(output, lhs, rhs);
  }

  masm.bind(&done);
}

void CodeGeneratorLOONG64::emitBigIntPtrDiv(LBigIntPtrDiv* ins,
                                            Register dividend, Register divisor,
                                            Register output) {
  // Callers handle division by zero and integer overflow.
  masm.as_div_d(/* result= */ output, dividend, divisor);
}

void CodeGeneratorLOONG64::emitBigIntPtrMod(LBigIntPtrMod* ins,
                                            Register dividend, Register divisor,
                                            Register output) {
  // Callers handle division by zero and integer overflow.
  masm.as_mod_d(/* result= */ output, dividend, divisor);
}

void CodeGenerator::visitWasmLoadI64(LWasmLoadI64* lir) {
  const MWasmLoad* mir = lir->mir();
  const auto& access = mir->access();

  Register memoryBase = ToRegister(lir->memoryBase());
  Register64 output = ToOutRegister64(lir);

  if (auto address = ToAbsoluteAddress(lir->ptr(), access)) {
    masm.wasmLoadAbsoluteI64(access, memoryBase, address.value(), output);
    return;
  }

  UseScratchRegisterScope temps(masm);
  Register ptrScratch = ToTempRegisterOrInvalid(lir->temp0());
  Register ptrReg = ToRegister(lir->ptr());
  if (mir->base()->type() == MIRType::Int32) {
    // See comment in visitWasmLoad re the type of 'base'.
    Register scratch = temps.Acquire();
    masm.move32ZeroExtendToPtr(ptrReg, scratch);
    ptrReg = scratch;
    ptrScratch = ptrScratch != InvalidReg ? scratch : InvalidReg;
  }

  masm.wasmLoadI64(access, memoryBase, ptrReg, ptrScratch, output);
}

void CodeGenerator::visitWasmStoreI64(LWasmStoreI64* lir) {
  const MWasmStore* mir = lir->mir();
  const auto& access = mir->access();

  Register64 value = Register64::Invalid();
  if (lir->value().value().isBogus()) {
    value = Register64(zero);
  } else {
    value = ToRegister64(lir->value());
  }
  Register memoryBase = ToRegister(lir->memoryBase());

  if (auto address = ToAbsoluteAddress(lir->ptr(), access)) {
    masm.wasmStoreAbsoluteI64(access, value, memoryBase, address.value());
    return;
  }

  UseScratchRegisterScope temps(masm);
  Register ptrScratch = ToTempRegisterOrInvalid(lir->temp0());
  Register ptrReg = ToRegister(lir->ptr());
  if (mir->base()->type() == MIRType::Int32) {
    // See comment in visitWasmLoad re the type of 'base'.
    Register scratch = temps.Acquire();
    masm.move32ZeroExtendToPtr(ptrReg, scratch);
    ptrReg = scratch;
    ptrScratch = ptrScratch != InvalidReg ? scratch : InvalidReg;
  }

  masm.wasmStoreI64(access, value, memoryBase, ptrReg, ptrScratch);
}

void CodeGenerator::visitWasmSelectI64(LWasmSelectI64* lir) {
  MOZ_ASSERT(lir->mir()->type() == MIRType::Int64);

  Register cond = ToRegister(lir->condExpr());
  Register64 trueExpr = ToRegister64(lir->trueExpr());
  Register64 falseExpr = ToRegister64(lir->falseExpr());
  Register64 output = ToOutRegister64(lir);

  UseScratchRegisterScope temps(masm);
  Register scratch = temps.Acquire();

  masm.ma_cselnz(output.reg, trueExpr.reg, falseExpr.reg, cond, scratch);
}

void CodeGenerator::visitExtendInt32ToInt64(LExtendInt32ToInt64* lir) {
  const LAllocation* input = lir->input();
  Register output = ToRegister(lir->output());

  if (lir->mir()->isUnsigned()) {
    masm.as_bstrpick_d(output, ToRegister(input), 31, 0);
  } else {
    masm.as_slli_w(output, ToRegister(input), 0);
  }
}

void CodeGenerator::visitWrapInt64ToInt32(LWrapInt64ToInt32* lir) {
  LInt64Allocation input = lir->input();
  Register output = ToRegister(lir->output());

  if (lir->mir()->bottomHalf()) {
    if (input.value().isMemory()) {
      masm.load32(ToAddress(input), output);
    } else {
      masm.move64To32(ToRegister64(input), output);
    }
  } else {
    MOZ_CRASH("Not implemented.");
  }
}

void CodeGenerator::visitSignExtendInt64(LSignExtendInt64* lir) {
  Register64 input = ToRegister64(lir->input());
  Register64 output = ToOutRegister64(lir);
  switch (lir->mir()->mode()) {
    case MSignExtendInt64::Byte:
      masm.move8SignExtend(input.reg, output.reg);
      break;
    case MSignExtendInt64::Half:
      masm.move16SignExtend(input.reg, output.reg);
      break;
    case MSignExtendInt64::Word:
      masm.move32To64SignExtend(input.reg, output);
      break;
  }
}

void CodeGenerator::visitWasmExtendU32Index(LWasmExtendU32Index* lir) {
  Register input = ToRegister(lir->input());
  Register output = ToRegister(lir->output());
  MOZ_ASSERT(input == output);
  masm.move32To64ZeroExtend(input, Register64(output));
}

void CodeGenerator::visitWasmWrapU32Index(LWasmWrapU32Index* lir) {
  Register input = ToRegister(lir->input());
  Register output = ToRegister(lir->output());
  MOZ_ASSERT(input == output);
  masm.move64To32(Register64(input), output);
}

void CodeGenerator::visitWasmTruncateToInt64(LWasmTruncateToInt64* lir) {
  FloatRegister input = ToFloatRegister(lir->input());
  Register64 output = ToOutRegister64(lir);

  MWasmTruncateToInt64* mir = lir->mir();
  MIRType fromType = mir->input()->type();

  MOZ_ASSERT(fromType == MIRType::Double || fromType == MIRType::Float32);

  auto* ool = new (alloc()) OutOfLineWasmTruncateCheck(mir, input, output);
  addOutOfLineCode(ool, mir);

  Label* oolEntry = ool->entry();
  Label* oolRejoin = ool->rejoin();
  bool isSaturating = mir->isSaturating();

  if (fromType == MIRType::Double) {
    if (mir->isUnsigned()) {
      masm.wasmTruncateDoubleToUInt64(input, output, isSaturating, oolEntry,
                                      oolRejoin, InvalidFloatReg);
    } else {
      masm.wasmTruncateDoubleToInt64(input, output, isSaturating, oolEntry,
                                     oolRejoin, InvalidFloatReg);
    }
  } else {
    if (mir->isUnsigned()) {
      masm.wasmTruncateFloat32ToUInt64(input, output, isSaturating, oolEntry,
                                       oolRejoin, InvalidFloatReg);
    } else {
      masm.wasmTruncateFloat32ToInt64(input, output, isSaturating, oolEntry,
                                      oolRejoin, InvalidFloatReg);
    }
  }
}

void CodeGenerator::visitInt64ToFloatingPoint(LInt64ToFloatingPoint* lir) {
  Register64 input = ToRegister64(lir->input());
  FloatRegister output = ToFloatRegister(lir->output());

  MIRType outputType = lir->mir()->type();
  MOZ_ASSERT(outputType == MIRType::Double || outputType == MIRType::Float32);

  if (outputType == MIRType::Double) {
    if (lir->mir()->isUnsigned()) {
      masm.convertUInt64ToDouble(input, output, Register::Invalid());
    } else {
      masm.convertInt64ToDouble(input, output);
    }
  } else {
    if (lir->mir()->isUnsigned()) {
      masm.convertUInt64ToFloat32(input, output, Register::Invalid());
    } else {
      masm.convertInt64ToFloat32(input, output);
    }
  }
}

void CodeGenerator::visitMinMaxD(LMinMaxD* ins) {
  FloatRegister first = ToFloatRegister(ins->first());
  FloatRegister second = ToFloatRegister(ins->second());

  MOZ_ASSERT(first == ToFloatRegister(ins->output()));

  const bool handleNaN =
      !ins->mir()->range() || ins->mir()->range()->canBeNaN();
  const bool handleZero =
      !ins->mir()->range() || ins->mir()->range()->canBeZero();

  if (ins->mir()->isMax()) {
    masm.maxDouble(second, first, handleNaN, handleZero);
  } else {
    masm.minDouble(second, first, handleNaN, handleZero);
  }
}

void CodeGenerator::visitMinMaxF(LMinMaxF* ins) {
  FloatRegister first = ToFloatRegister(ins->first());
  FloatRegister second = ToFloatRegister(ins->second());

  MOZ_ASSERT(first == ToFloatRegister(ins->output()));

  const bool handleNaN =
      !ins->mir()->range() || ins->mir()->range()->canBeNaN();
  const bool handleZero =
      !ins->mir()->range() || ins->mir()->range()->canBeZero();

  if (ins->mir()->isMax()) {
    masm.maxFloat32(second, first, handleNaN, handleZero);
  } else {
    masm.minFloat32(second, first, handleNaN, handleZero);
  }
}

void CodeGenerator::visitAddI(LAddI* ins) {
  const LAllocation* lhs = ins->lhs();
  const LAllocation* rhs = ins->rhs();
  const LDefinition* dest = ins->output();

  MOZ_ASSERT(rhs->isConstant() || rhs->isGeneralReg());

  // If there is no snapshot, we don't need to check for overflow
  if (!ins->snapshot()) {
    if (rhs->isConstant()) {
      masm.ma_add_w(ToRegister(dest), ToRegister(lhs), Imm32(ToInt32(rhs)));
    } else {
      masm.as_add_w(ToRegister(dest), ToRegister(lhs), ToRegister(rhs));
    }
    return;
  }

  Label overflow;
  if (rhs->isConstant()) {
    masm.ma_add32TestOverflow(ToRegister(dest), ToRegister(lhs),
                              Imm32(ToInt32(rhs)), &overflow);
  } else {
    masm.ma_add32TestOverflow(ToRegister(dest), ToRegister(lhs),
                              ToRegister(rhs), &overflow);
  }

  bailoutFrom(&overflow, ins->snapshot());
}

void CodeGenerator::visitAddIntPtr(LAddIntPtr* ins) {
  Register lhs = ToRegister(ins->lhs());
  const LAllocation* rhs = ins->rhs();
  Register dest = ToRegister(ins->output());

  if (rhs->isConstant()) {
    masm.ma_add_d(dest, lhs, ImmWord(ToIntPtr(rhs)));
  } else {
    masm.as_add_d(dest, lhs, ToRegister(rhs));
  }
}

void CodeGenerator::visitAddI64(LAddI64* lir) {
  Register lhs = ToRegister64(lir->lhs()).reg;
  LInt64Allocation rhs = lir->rhs();
  Register dest = ToOutRegister64(lir).reg;

  if (IsConstant(rhs)) {
    masm.ma_add_d(dest, lhs, ImmWord(ToInt64(rhs)));
  } else {
    masm.as_add_d(dest, lhs, ToRegister64(rhs).reg);
  }
}

void CodeGenerator::visitSubI(LSubI* ins) {
  const LAllocation* lhs = ins->lhs();
  const LAllocation* rhs = ins->rhs();
  const LDefinition* dest = ins->output();

  MOZ_ASSERT(rhs->isConstant() || rhs->isGeneralReg());

  // If there is no snapshot, we don't need to check for overflow

  if (!ins->snapshot()) {
    if (rhs->isConstant()) {
      masm.ma_sub_w(ToRegister(dest), ToRegister(lhs), Imm32(ToInt32(rhs)));
    } else {
      masm.as_sub_w(ToRegister(dest), ToRegister(lhs), ToRegister(rhs));
    }
    return;
  }

  Label overflow;
  if (rhs->isConstant()) {
    masm.ma_sub32TestOverflow(ToRegister(dest), ToRegister(lhs),
                              Imm32(ToInt32(rhs)), &overflow);
  } else {
    masm.ma_sub32TestOverflow(ToRegister(dest), ToRegister(lhs),
                              ToRegister(rhs), &overflow);
  }

  bailoutFrom(&overflow, ins->snapshot());
}

void CodeGenerator::visitSubIntPtr(LSubIntPtr* ins) {
  Register lhs = ToRegister(ins->lhs());
  const LAllocation* rhs = ins->rhs();
  Register dest = ToRegister(ins->output());

  if (rhs->isConstant()) {
    masm.ma_sub_d(dest, lhs, ImmWord(ToIntPtr(rhs)));
  } else {
    masm.as_sub_d(dest, lhs, ToRegister(rhs));
  }
}

void CodeGenerator::visitSubI64(LSubI64* lir) {
  Register lhs = ToRegister64(lir->lhs()).reg;
  LInt64Allocation rhs = lir->rhs();
  Register dest = ToOutRegister64(lir).reg;

  if (IsConstant(rhs)) {
    masm.ma_sub_d(dest, lhs, ImmWord(ToInt64(rhs)));
  } else {
    masm.as_sub_d(dest, lhs, ToRegister64(rhs).reg);
  }
}

void CodeGenerator::visitMulI(LMulI* ins) {
  Register lhs = ToRegister(ins->lhs());
  const LAllocation* rhs = ins->rhs();
  Register dest = ToRegister(ins->output());
  MMul* mul = ins->mir();

  MOZ_ASSERT_IF(mul->mode() == MMul::Integer,
                !mul->canBeNegativeZero() && !mul->canOverflow());

  if (rhs->isConstant()) {
    int32_t constant = ToInt32(rhs);

    // Bailout on -0.0
    if (mul->canBeNegativeZero() && constant <= 0) {
      Assembler::Condition cond =
          (constant == 0) ? Assembler::LessThan : Assembler::Equal;
      bailoutCmp32(cond, lhs, Imm32(0), ins->snapshot());
    }

    switch (constant) {
      case -1:
        if (mul->canOverflow()) {
          bailoutCmp32(Assembler::Equal, lhs, Imm32(INT32_MIN),
                       ins->snapshot());
        }
        [[fallthrough]];
      case 0:
      case 1:
        masm.ma_mul(dest, lhs, Imm32(constant));
        return;
    }
    if (mul->canOverflow()) {
      Label mulConstOverflow;
      masm.ma_mul32TestOverflow(dest, lhs, Imm32(constant), &mulConstOverflow);
      bailoutFrom(&mulConstOverflow, ins->snapshot());
    } else {
      masm.ma_mul(dest, lhs, Imm32(constant));
    }
  } else {
    if (mul->canOverflow()) {
      Label multRegOverflow;
      masm.ma_mul32TestOverflow(dest, lhs, ToRegister(rhs), &multRegOverflow);

      bailoutFrom(&multRegOverflow, ins->snapshot());
    } else {
      masm.as_mul_w(dest, lhs, ToRegister(rhs));
    }

    if (mul->canBeNegativeZero()) {
      Label done;
      masm.ma_b(dest, dest, &done, Assembler::NonZero, ShortJump);

      // Result is -0 if lhs or rhs is negative.
      // In that case result must be double value so bailout
      UseScratchRegisterScope temps(masm);
      Register scratch = temps.Acquire();
      masm.as_or(scratch, lhs, ToRegister(rhs));
      bailoutCmp32(Assembler::Signed, scratch, scratch, ins->snapshot());

      masm.bind(&done);
    }
  }
}

void CodeGenerator::visitMulIntPtr(LMulIntPtr* ins) {
  Register lhs = ToRegister(ins->lhs());
  const LAllocation* rhs = ins->rhs();
  Register dest = ToRegister(ins->output());

  if (rhs->isConstant()) {
    masm.ma_mul_d(dest, lhs, ImmWord(ToIntPtr(rhs)));
  } else {
    masm.as_mul_d(dest, lhs, ToRegister(rhs));
  }
}

void CodeGenerator::visitMulI64(LMulI64* lir) {
  Register lhs = ToRegister64(lir->lhs()).reg;
  LInt64Allocation rhs = lir->rhs();
  Register dest = ToOutRegister64(lir).reg;

  if (IsConstant(rhs)) {
    masm.ma_mul_d(dest, lhs, ImmWord(ToInt64(rhs)));
  } else {
    masm.as_mul_d(dest, lhs, ToRegister64(rhs).reg);
  }
}

void CodeGenerator::visitDivI(LDivI* ins) {
  Register lhs = ToRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());
  Register dest = ToRegister(ins->output());
  Register temp = ToRegister(ins->temp0());
  MDiv* mir = ins->mir();

  Label done;

  // Handle divide by zero.
  if (mir->canBeDivideByZero()) {
    if (mir->trapOnError()) {
      Label nonZero;
      masm.ma_b(rhs, rhs, &nonZero, Assembler::NonZero);
      masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
      masm.bind(&nonZero);
    } else if (mir->canTruncateInfinities()) {
      // Truncated division by zero is zero (Infinity|0 == 0)
      Label notzero;
      masm.ma_b(rhs, rhs, &notzero, Assembler::NonZero, ShortJump);
      masm.move32(Imm32(0), dest);
      masm.ma_b(&done, ShortJump);
      masm.bind(&notzero);
    } else {
      MOZ_ASSERT(mir->fallible());
      bailoutCmp32(Assembler::Zero, rhs, rhs, ins->snapshot());
    }
  }

  // Handle an integer overflow exception from -2147483648 / -1.
  if (mir->canBeNegativeOverflow()) {
    Label notMinInt;
    masm.move32(Imm32(INT32_MIN), temp);
    masm.ma_b(lhs, temp, &notMinInt, Assembler::NotEqual, ShortJump);

    masm.move32(Imm32(-1), temp);
    if (mir->trapOnError()) {
      Label ok;
      masm.ma_b(rhs, temp, &ok, Assembler::NotEqual);
      masm.wasmTrap(wasm::Trap::IntegerOverflow, mir->trapSiteDesc());
      masm.bind(&ok);
    } else if (mir->canTruncateOverflow()) {
      // (-INT32_MIN)|0 == INT32_MIN
      Label skip;
      masm.ma_b(rhs, temp, &skip, Assembler::NotEqual, ShortJump);
      masm.move32(Imm32(INT32_MIN), dest);
      masm.ma_b(&done, ShortJump);
      masm.bind(&skip);
    } else {
      MOZ_ASSERT(mir->fallible());
      bailoutCmp32(Assembler::Equal, rhs, temp, ins->snapshot());
    }
    masm.bind(&notMinInt);
  }

  // Handle negative 0. (0/-Y)
  if (!mir->canTruncateNegativeZero() && mir->canBeNegativeZero()) {
    Label nonzero;
    masm.ma_b(lhs, lhs, &nonzero, Assembler::NonZero, ShortJump);
    bailoutCmp32(Assembler::LessThan, rhs, Imm32(0), ins->snapshot());
    masm.bind(&nonzero);
  }

  // All regular. Lets call div.
  if (mir->canTruncateRemainder()) {
    masm.as_div_w(dest, lhs, rhs);
  } else {
    MOZ_ASSERT(mir->fallible());

    masm.as_mod_w(temp, lhs, rhs);
    bailoutCmp32(Assembler::NonZero, temp, temp, ins->snapshot());

    masm.as_div_w(dest, lhs, rhs);
  }

  masm.bind(&done);
}

void CodeGenerator::visitDivPowTwoI(LDivPowTwoI* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register dest = ToRegister(ins->output());
  Register tmp = ToRegister(ins->temp0());
  int32_t shift = ins->shift();
  MOZ_ASSERT(0 <= shift && shift <= 31);
  const bool negativeDivisor = ins->negativeDivisor();
  MDiv* mir = ins->mir();

  if (!mir->isTruncated() && negativeDivisor) {
    // 0 divided by a negative number returns a -0 double.
    bailoutTest32(Assembler::Zero, lhs, lhs, ins->snapshot());
  }

  if (shift != 0) {
    if (!mir->isTruncated()) {
      // If the remainder is going to be != 0, bailout since this must
      // be a double.
      masm.as_slli_w(tmp, lhs, (32 - shift));
      bailoutTest32(Assembler::NonZero, tmp, tmp, ins->snapshot());
    }

    if (mir->isUnsigned()) {
      masm.as_srli_w(dest, lhs, shift);
    } else {
      if (mir->canBeNegativeDividend() && mir->isTruncated()) {
        // Adjust the value so that shifting produces a correctly rounded result
        // when the numerator is negative. See 10-1 "Signed Division by a Known
        // Power of 2" in Henry S. Warren, Jr.'s Hacker's Delight.
        if (shift > 1) {
          masm.as_srai_w(tmp, lhs, 31);
          masm.as_srli_w(tmp, tmp, (32 - shift));
        } else {
          masm.as_srli_w(tmp, lhs, (32 - shift));
        }
        masm.add32(lhs, tmp);

        // Do the shift.
        masm.as_srai_w(dest, tmp, shift);
      } else {
        // Numerator is unsigned, so needs no adjusting. Do the shift.
        masm.as_srai_w(dest, lhs, shift);
      }

      if (negativeDivisor) {
        masm.neg32(dest);
      }
    }
  } else {
    if (negativeDivisor) {
      // INT32_MIN / -1 overflows.
      if (mir->trapOnError()) {
        Label ok;
        masm.branch32(Assembler::NotEqual, lhs, Imm32(INT32_MIN), &ok);
        masm.wasmTrap(wasm::Trap::IntegerOverflow, mir->trapSiteDesc());
        masm.bind(&ok);
      } else if (!mir->isTruncated()) {
        bailoutCmp32(Assembler::Equal, lhs, Imm32(INT32_MIN), ins->snapshot());
      }
      masm.as_sub_w(dest, zero, lhs);
    } else {
      if (mir->isUnsigned() && !mir->isTruncated()) {
        // Unsigned division by 1 can overflow if output is not truncated, as we
        // do not have an Unsigned type for MIR instructions.
        bailoutTest32(Assembler::Signed, lhs, lhs, ins->snapshot());
      }
      masm.move32(lhs, dest);
    }
  }
}

void CodeGenerator::visitDivConstantI(LDivConstantI* ins) {
  const Register lhs = ToRegister(ins->numerator());
  const Register output = ToRegister(ins->output());
  const int32_t d = ins->denominator();

  const MDiv* const mir = ins->mir();

  if (d == 0) {
    if (mir->trapOnError()) {
      masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
    } else if (mir->canTruncateInfinities()) {
      masm.move32(Imm32(0), output);
    } else {
      MOZ_ASSERT(mir->fallible());
      bailout(ins->snapshot());
    }
    return;
  }

  // Compute the truncated division result in |output|.
  DivideWithConstant(masm, ins);

  // We are checking whether the division resulted in an integer, we multiply
  // the obtained value by d to check if the correct answer is an integer. This
  // cannot overflow, since |d| > 1.
  if (!mir->isTruncated()) {
    UseScratchRegisterScope temps(masm);
    Register temp = temps.Acquire();

    masm.ma_mul(temp, output, Imm32(d));
    bailoutCmp32(Assembler::NotEqual, lhs, temp, ins->snapshot());

    // If lhs is zero and the divisor is negative, the answer should have
    // been -0.
    if (d < 0) {
      bailoutTest32(Assembler::Zero, lhs, lhs, ins->snapshot());
    }
  }
}

void CodeGenerator::visitModI(LModI* ins) {
  Register lhs = ToRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());
  Register dest = ToRegister(ins->output());
  MOZ_ASSERT(rhs != dest && lhs != dest);
  MMod* mir = ins->mir();
  Label done;

  // Prevent divide by zero.
  if (mir->canBeDivideByZero()) {
    if (mir->isTruncated()) {
      if (mir->trapOnError()) {
        Label nonZero;
        masm.ma_b(rhs, rhs, &nonZero, Assembler::NonZero, ShortJump);
        masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
        masm.bind(&nonZero);
      } else {
        // Truncated division by zero yields integer zero.
        masm.move32(rhs, dest);
        masm.ma_b(rhs, rhs, &done, Assembler::Zero, ShortJump);
      }
    } else {
      MOZ_ASSERT(mir->fallible());
      bailoutCmp32(Assembler::Equal, rhs, Imm32(0), ins->snapshot());
    }
  }

  masm.as_mod_w(dest, lhs, rhs);

  // If X%Y == 0 and X < 0, then we *actually* wanted to return -0.0
  if (mir->canBeNegativeDividend() && !mir->isTruncated()) {
    MOZ_ASSERT(mir->fallible());
    masm.ma_b(dest, Imm32(0), &done, Assembler::NotEqual, ShortJump);
    bailoutCmp32(Assembler::Signed, lhs, Imm32(0), ins->snapshot());
  }
  masm.bind(&done);
}

void CodeGenerator::visitModConstantI(LModConstantI* ins) {
  const Register lhs = ToRegister(ins->numerator());
  const Register output = ToRegister(ins->output());

  const MMod* const mir = ins->mir();

  const int32_t d = ins->denominator();
  if (d == 0) {
    if (mir->trapOnError()) {
      masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
    } else if (mir->isTruncated()) {
      masm.move32(Imm32(0), output);
    } else {
      MOZ_ASSERT(mir->fallible());
      bailout(ins->snapshot());
    }
    return;
  }

  // Compute the truncated division result in |output|.
  DivideWithConstant(masm, ins);

  // Compute the remainder: output = lhs - (output * d).
  masm.ma_mul(output, output, Imm32(d));
  masm.as_sub_w(output, lhs, output);

  if (mir->canBeNegativeDividend() && !mir->isTruncated()) {
    MOZ_ASSERT(mir->fallible());

    // If output == 0 and lhs < 0, then the result should be double -0.0.
    Label done;
    masm.ma_b(output, Imm32(0), &done, Assembler::NotEqual, ShortJump);
    bailoutTest32(Assembler::Signed, lhs, lhs, ins->snapshot());
    masm.bind(&done);
  }
}

void CodeGenerator::visitModPowTwoI(LModPowTwoI* ins) {
  Register in = ToRegister(ins->input());
  Register out = ToRegister(ins->output());
  MMod* mir = ins->mir();
  int32_t shift = ins->shift();
  const bool canBeNegative = !mir->isUnsigned() && mir->canBeNegativeDividend();

  if (shift == 0) {
    if (canBeNegative && !mir->isTruncated()) {
      bailoutTest32(Assembler::Signed, in, in, ins->snapshot());
    }
    masm.move32(Imm32(0), out);
    return;
  }

  Label negative;
  if (canBeNegative) {
    // Switch based on sign of the lhs.
    masm.ma_b(in, in, &negative, Assembler::Signed, ShortJump);
  }

  masm.as_bstrpick_w(out, in, shift - 1, 0);

  if (canBeNegative) {
    Label done;
    masm.ma_b(&done, ShortJump);

    // Negative numbers need a negate, bit extraction, negate.
    masm.bind(&negative);
    masm.as_sub_w(out, zero, in);
    masm.as_bstrpick_w(out, out, shift - 1, 0);
    masm.neg32(out);

    // Since a%b has the same sign as a, and a is negative in this branch,
    // an answer of 0 means the correct result is actually -0. Bail out.
    if (!mir->isTruncated()) {
      MOZ_ASSERT(mir->fallible());
      bailoutCmp32(Assembler::Zero, out, out, ins->snapshot());
    }
    masm.bind(&done);
  }
}

void CodeGenerator::visitBitNotI(LBitNotI* ins) {
  Register input = ToRegister(ins->input());
  Register dest = ToRegister(ins->output());
  masm.as_nor(dest, input, zero);
}

void CodeGenerator::visitBitNotI64(LBitNotI64* ins) {
  Register input = ToRegister64(ins->input()).reg;
  Register dest = ToOutRegister64(ins).reg;
  masm.as_nor(dest, input, zero);
}

void CodeGenerator::visitBitOpI(LBitOpI* ins) {
  Register lhs = ToRegister(ins->lhs());
  const LAllocation* rhs = ins->rhs();
  Register dest = ToRegister(ins->output());

  // all of these bitops should be either imm32's, or integer registers.
  switch (ins->bitop()) {
    case JSOp::BitOr:
      if (rhs->isConstant()) {
        masm.ma_or(dest, lhs, Imm32(ToInt32(rhs)));
      } else {
        masm.as_or(dest, lhs, ToRegister(rhs));
        masm.as_slli_w(dest, dest, 0);
      }
      break;
    case JSOp::BitXor:
      if (rhs->isConstant()) {
        masm.ma_xor(dest, lhs, Imm32(ToInt32(rhs)));
      } else {
        masm.as_xor(dest, lhs, ToRegister(rhs));
        masm.as_slli_w(dest, dest, 0);
      }
      break;
    case JSOp::BitAnd:
      if (rhs->isConstant()) {
        masm.ma_and(dest, lhs, Imm32(ToInt32(rhs)));
      } else {
        masm.as_and(dest, lhs, ToRegister(rhs));
        masm.as_slli_w(dest, dest, 0);
      }
      break;
    default:
      MOZ_CRASH("unexpected binary opcode");
  }
}

void CodeGenerator::visitBitOpI64(LBitOpI64* lir) {
  Register lhs = ToRegister64(lir->lhs()).reg;
  Register dest = ToOutRegister64(lir).reg;
  const JSOp bitop = lir->bitop();

  if (IsConstant(lir->rhs())) {
    const uint64_t uimm = mozilla::BitwiseCast<uint64_t>(ToInt64(lir->rhs()));

    switch (bitop) {
      case JSOp::BitAnd:
        masm.ma_and(dest, lhs, ImmWord(uimm));
        return;
      case JSOp::BitOr:
        masm.ma_or(dest, lhs, ImmWord(uimm));
        return;
      case JSOp::BitXor:
        masm.ma_xor(dest, lhs, ImmWord(uimm));
        return;
      default:
        MOZ_CRASH("unexpected binary opcode");
    }
  }

  Register rhs = ToRegister64(lir->rhs()).reg;
  switch (lir->bitop()) {
    case JSOp::BitOr:
      masm.as_or(dest, lhs, rhs);
      break;
    case JSOp::BitXor:
      masm.as_xor(dest, lhs, rhs);
      break;
    case JSOp::BitAnd:
      masm.as_and(dest, lhs, rhs);
      break;
    default:
      MOZ_CRASH("unexpected binary opcode");
  }
}

void CodeGenerator::visitShiftI(LShiftI* ins) {
  Register lhs = ToRegister(ins->lhs());
  const LAllocation* rhs = ins->rhs();
  Register dest = ToRegister(ins->output());

  if (rhs->isConstant()) {
    int32_t shift = ToInt32(rhs) & 0x1F;
    switch (ins->bitop()) {
      case JSOp::Lsh:
        if (shift) {
          masm.as_slli_w(dest, lhs, shift);
        } else {
          masm.move32(lhs, dest);
        }
        break;
      case JSOp::Rsh:
        if (shift) {
          masm.as_srai_w(dest, lhs, shift);
        } else {
          masm.move32(lhs, dest);
        }
        break;
      case JSOp::Ursh:
        if (shift) {
          masm.as_srli_w(dest, lhs, shift);
        } else {
          // x >>> 0 can overflow.
          if (ins->mir()->toUrsh()->fallible()) {
            bailoutCmp32(Assembler::LessThan, lhs, Imm32(0), ins->snapshot());
          }
          masm.move32(lhs, dest);
        }
        break;
      default:
        MOZ_CRASH("Unexpected shift op");
    }
  } else {
    Register shift = ToRegister(rhs);
    switch (ins->bitop()) {
      case JSOp::Lsh:
        masm.as_sll_w(dest, lhs, shift);
        break;
      case JSOp::Rsh:
        masm.as_sra_w(dest, lhs, shift);
        break;
      case JSOp::Ursh:
        masm.as_srl_w(dest, lhs, shift);
        if (ins->mir()->toUrsh()->fallible()) {
          // x >>> 0 can overflow.
          bailoutCmp32(Assembler::LessThan, dest, Imm32(0), ins->snapshot());
        }
        break;
      default:
        MOZ_CRASH("Unexpected shift op");
    }
  }
}

void CodeGenerator::visitShiftIntPtr(LShiftIntPtr* ins) {
  Register lhs = ToRegister(ins->lhs());
  const LAllocation* rhs = ins->rhs();
  Register dest = ToRegister(ins->output());

  if (rhs->isConstant()) {
    int32_t shift = ToIntPtr(rhs) & 0x3F;
    if (shift) {
      switch (ins->bitop()) {
        case JSOp::Lsh:
          masm.as_slli_d(dest, lhs, shift);
          break;
        case JSOp::Rsh:
          masm.as_srai_d(dest, lhs, shift);
          break;
        case JSOp::Ursh:
          masm.as_srli_d(dest, lhs, shift);
          break;
        default:
          MOZ_CRASH("Unexpected shift op");
      }
    } else if (lhs != dest) {
      masm.movePtr(lhs, dest);
    }
  } else {
    Register shift = ToRegister(rhs);
    switch (ins->bitop()) {
      case JSOp::Lsh:
        masm.as_sll_d(dest, lhs, shift);
        break;
      case JSOp::Rsh:
        masm.as_sra_d(dest, lhs, shift);
        break;
      case JSOp::Ursh:
        masm.as_srl_d(dest, lhs, shift);
        break;
      default:
        MOZ_CRASH("Unexpected shift op");
    }
  }
}

void CodeGenerator::visitShiftI64(LShiftI64* lir) {
  Register lhs = ToRegister64(lir->lhs()).reg;
  const LAllocation* rhs = lir->rhs();
  Register dest = ToOutRegister64(lir).reg;

  if (rhs->isConstant()) {
    int32_t shift = int32_t(rhs->toConstant()->toInt64() & 0x3F);
    if (shift) {
      switch (lir->bitop()) {
        case JSOp::Lsh:
          masm.as_slli_d(dest, lhs, shift);
          break;
        case JSOp::Rsh:
          masm.as_srai_d(dest, lhs, shift);
          break;
        case JSOp::Ursh:
          masm.as_srli_d(dest, lhs, shift);
          break;
        default:
          MOZ_CRASH("Unexpected shift op");
      }
    } else if (lhs != dest) {
      masm.movePtr(lhs, dest);
    }
    return;
  }

  Register shift = ToRegister(rhs);
  switch (lir->bitop()) {
    case JSOp::Lsh:
      masm.as_sll_d(dest, lhs, shift);
      break;
    case JSOp::Rsh:
      masm.as_sra_d(dest, lhs, shift);
      break;
    case JSOp::Ursh:
      masm.as_srl_d(dest, lhs, shift);
      break;
    default:
      MOZ_CRASH("Unexpected shift op");
  }
}

void CodeGenerator::visitUrshD(LUrshD* ins) {
  Register lhs = ToRegister(ins->lhs());
  Register temp = ToRegister(ins->temp0());

  const LAllocation* rhs = ins->rhs();
  FloatRegister out = ToFloatRegister(ins->output());

  if (rhs->isConstant()) {
    masm.as_srli_w(temp, lhs, ToInt32(rhs) & 0x1f);
  } else {
    masm.as_srl_w(temp, lhs, ToRegister(rhs));
  }

  masm.convertUInt32ToDouble(temp, out);
}

void CodeGenerator::visitPowHalfD(LPowHalfD* ins) {
  const FloatRegister input = ToFloatRegister(ins->input());
  const FloatRegister output = ToFloatRegister(ins->output());
  ScratchDoubleScope fpscratch(masm);

  FloatRegister fsqrtFj = input;

  Label done, notNegInf;
  if (!ins->mir()->operandIsNeverNegativeInfinity()) {
    // Masm.pow(-Infinity, 0.5) == Infinity.
    masm.loadConstantDouble(NegativeInfinity<double>(), fpscratch);
    masm.ma_bc_d(input, fpscratch, &notNegInf,
                 Assembler::DoubleNotEqualOrUnordered, ShortJump);
    masm.as_fneg_d(output, fpscratch);
    masm.ma_b(&done, ShortJump);

    masm.bind(&notNegInf);
  }

  if (!ins->mir()->operandIsNeverNegativeZero()) {
    // Math.pow(-0, 0.5) == 0 == Math.pow(0, 0.5).
    // Adding 0 converts any -0 to 0.
    masm.loadConstantDouble(0.0, fpscratch);
    masm.as_fadd_d(output, input, fpscratch);
    fsqrtFj = output;
  }
  masm.as_fsqrt_d(output, fsqrtFj);

  if (!ins->mir()->operandIsNeverNegativeInfinity()) {
    masm.bind(&done);
  }
}

void CodeGenerator::visitMathD(LMathD* math) {
  FloatRegister src1 = ToFloatRegister(math->lhs());
  FloatRegister src2 = ToFloatRegister(math->rhs());
  FloatRegister output = ToFloatRegister(math->output());

  switch (math->jsop()) {
    case JSOp::Add:
      masm.as_fadd_d(output, src1, src2);
      break;
    case JSOp::Sub:
      masm.as_fsub_d(output, src1, src2);
      break;
    case JSOp::Mul:
      masm.as_fmul_d(output, src1, src2);
      break;
    case JSOp::Div:
      masm.as_fdiv_d(output, src1, src2);
      break;
    default:
      MOZ_CRASH("unexpected opcode");
  }
}

void CodeGenerator::visitMathF(LMathF* math) {
  FloatRegister src1 = ToFloatRegister(math->lhs());
  FloatRegister src2 = ToFloatRegister(math->rhs());
  FloatRegister output = ToFloatRegister(math->output());

  switch (math->jsop()) {
    case JSOp::Add:
      masm.as_fadd_s(output, src1, src2);
      break;
    case JSOp::Sub:
      masm.as_fsub_s(output, src1, src2);
      break;
    case JSOp::Mul:
      masm.as_fmul_s(output, src1, src2);
      break;
    case JSOp::Div:
      masm.as_fdiv_s(output, src1, src2);
      break;
    default:
      MOZ_CRASH("unexpected opcode");
  }
}

void CodeGenerator::visitTruncateDToInt32(LTruncateDToInt32* ins) {
  emitTruncateDouble(ToFloatRegister(ins->input()), ToRegister(ins->output()),
                     ins->mir());
}

void CodeGenerator::visitTruncateFToInt32(LTruncateFToInt32* ins) {
  emitTruncateFloat32(ToFloatRegister(ins->input()), ToRegister(ins->output()),
                      ins->mir());
}

void CodeGenerator::visitWasmBuiltinTruncateDToInt32(
    LWasmBuiltinTruncateDToInt32* lir) {
  emitTruncateDouble(ToFloatRegister(lir->input()), ToRegister(lir->output()),
                     lir->mir());
}

void CodeGenerator::visitWasmBuiltinTruncateFToInt32(
    LWasmBuiltinTruncateFToInt32* lir) {
  emitTruncateFloat32(ToFloatRegister(lir->input()), ToRegister(lir->output()),
                      lir->mir());
}

void CodeGenerator::visitWasmTruncateToInt32(LWasmTruncateToInt32* lir) {
  auto input = ToFloatRegister(lir->input());
  auto output = ToRegister(lir->output());

  MWasmTruncateToInt32* mir = lir->mir();
  MIRType fromType = mir->input()->type();

  MOZ_ASSERT(fromType == MIRType::Double || fromType == MIRType::Float32);

  auto* ool = new (alloc()) OutOfLineWasmTruncateCheck(mir, input, output);
  addOutOfLineCode(ool, mir);

  Label* oolEntry = ool->entry();
  if (mir->isUnsigned()) {
    if (fromType == MIRType::Double) {
      masm.wasmTruncateDoubleToUInt32(input, output, mir->isSaturating(),
                                      oolEntry);
    } else if (fromType == MIRType::Float32) {
      masm.wasmTruncateFloat32ToUInt32(input, output, mir->isSaturating(),
                                       oolEntry);
    } else {
      MOZ_CRASH("unexpected type");
    }

    masm.bind(ool->rejoin());
    return;
  }

  if (fromType == MIRType::Double) {
    masm.wasmTruncateDoubleToInt32(input, output, mir->isSaturating(),
                                   oolEntry);
  } else if (fromType == MIRType::Float32) {
    masm.wasmTruncateFloat32ToInt32(input, output, mir->isSaturating(),
                                    oolEntry);
  } else {
    MOZ_CRASH("unexpected type");
  }

  masm.bind(ool->rejoin());
}

void CodeGenerator::visitTestDAndBranch(LTestDAndBranch* test) {
  FloatRegister input = ToFloatRegister(test->input());
  ScratchDoubleScope fpscratch(masm);

  MBasicBlock* ifTrue = test->ifTrue();
  MBasicBlock* ifFalse = test->ifFalse();

  masm.loadConstantDouble(0.0, fpscratch);
  // If 0, or NaN, the result is false.
  if (isNextBlock(ifFalse->lir())) {
    branchToBlock(Assembler::DoubleFloat, input, fpscratch, ifTrue,
                  Assembler::DoubleNotEqual);
  } else {
    branchToBlock(Assembler::DoubleFloat, input, fpscratch, ifFalse,
                  Assembler::DoubleEqualOrUnordered);
    jumpToBlock(ifTrue);
  }
}

void CodeGenerator::visitTestFAndBranch(LTestFAndBranch* test) {
  FloatRegister input = ToFloatRegister(test->input());
  ScratchFloat32Scope fpscratch(masm);

  MBasicBlock* ifTrue = test->ifTrue();
  MBasicBlock* ifFalse = test->ifFalse();

  masm.loadConstantFloat32(0.0f, fpscratch);
  // If 0, or NaN, the result is false.

  if (isNextBlock(ifFalse->lir())) {
    branchToBlock(Assembler::SingleFloat, input, fpscratch, ifTrue,
                  Assembler::DoubleNotEqual);
  } else {
    branchToBlock(Assembler::SingleFloat, input, fpscratch, ifFalse,
                  Assembler::DoubleEqualOrUnordered);
    jumpToBlock(ifTrue);
  }
}

void CodeGenerator::visitCompareD(LCompareD* comp) {
  FloatRegister lhs = ToFloatRegister(comp->left());
  FloatRegister rhs = ToFloatRegister(comp->right());
  Register dest = ToRegister(comp->output());

  Assembler::DoubleCondition cond = JSOpToDoubleCondition(comp->mir()->jsop());
  masm.ma_cmp_set_double(dest, lhs, rhs, cond);
}

void CodeGenerator::visitCompareF(LCompareF* comp) {
  FloatRegister lhs = ToFloatRegister(comp->left());
  FloatRegister rhs = ToFloatRegister(comp->right());
  Register dest = ToRegister(comp->output());

  Assembler::DoubleCondition cond = JSOpToDoubleCondition(comp->mir()->jsop());
  masm.ma_cmp_set_float32(dest, lhs, rhs, cond);
}

void CodeGenerator::visitCompareDAndBranch(LCompareDAndBranch* comp) {
  FloatRegister lhs = ToFloatRegister(comp->left());
  FloatRegister rhs = ToFloatRegister(comp->right());

  Assembler::DoubleCondition cond =
      JSOpToDoubleCondition(comp->cmpMir()->jsop());
  MBasicBlock* ifTrue = comp->ifTrue();
  MBasicBlock* ifFalse = comp->ifFalse();

  if (isNextBlock(ifFalse->lir())) {
    branchToBlock(Assembler::DoubleFloat, lhs, rhs, ifTrue, cond);
  } else {
    branchToBlock(Assembler::DoubleFloat, lhs, rhs, ifFalse,
                  Assembler::InvertCondition(cond));
    jumpToBlock(ifTrue);
  }
}

void CodeGenerator::visitCompareFAndBranch(LCompareFAndBranch* comp) {
  FloatRegister lhs = ToFloatRegister(comp->left());
  FloatRegister rhs = ToFloatRegister(comp->right());

  Assembler::DoubleCondition cond =
      JSOpToDoubleCondition(comp->cmpMir()->jsop());
  MBasicBlock* ifTrue = comp->ifTrue();
  MBasicBlock* ifFalse = comp->ifFalse();

  if (isNextBlock(ifFalse->lir())) {
    branchToBlock(Assembler::SingleFloat, lhs, rhs, ifTrue, cond);
  } else {
    branchToBlock(Assembler::SingleFloat, lhs, rhs, ifFalse,
                  Assembler::InvertCondition(cond));
    jumpToBlock(ifTrue);
  }
}

void CodeGenerator::visitNotD(LNotD* ins) {
  // Since this operation is not, we want to set a bit if
  // the double is falsey, which means 0.0, -0.0 or NaN.
  FloatRegister in = ToFloatRegister(ins->input());
  Register dest = ToRegister(ins->output());
  ScratchDoubleScope fpscratch(masm);

  masm.loadConstantDouble(0.0, fpscratch);
  masm.ma_cmp_set_double(dest, in, fpscratch,
                         Assembler::DoubleEqualOrUnordered);
}

void CodeGenerator::visitNotF(LNotF* ins) {
  // Since this operation is not, we want to set a bit if
  // the float32 is falsey, which means 0.0, -0.0 or NaN.
  FloatRegister in = ToFloatRegister(ins->input());
  Register dest = ToRegister(ins->output());
  ScratchFloat32Scope fpscratch(masm);

  masm.loadConstantFloat32(0.0f, fpscratch);
  masm.ma_cmp_set_float32(dest, in, fpscratch,
                          Assembler::DoubleEqualOrUnordered);
}

void CodeGenerator::visitWasmLoad(LWasmLoad* lir) { emitWasmLoad(lir); }

void CodeGenerator::visitWasmStore(LWasmStore* lir) { emitWasmStore(lir); }

void CodeGenerator::visitWasmCompareExchangeHeap(
    LWasmCompareExchangeHeap* ins) {
  MWasmCompareExchangeHeap* mir = ins->mir();
  Register memoryBase = ToRegister(ins->memoryBase());
  Register ptrReg = ToRegister(ins->ptr());
  BaseIndex srcAddr(memoryBase, ptrReg, TimesOne, mir->access().offset32());
  auto zeroExtend = ToZeroExtendIndex(ins);

  Register oldval = ToRegister(ins->oldValue());
  Register newval = ToRegister(ins->newValue());
  Register valueTemp = ToTempRegisterOrInvalid(ins->temp0());
  Register offsetTemp = ToTempRegisterOrInvalid(ins->temp1());
  Register maskTemp = ToTempRegisterOrInvalid(ins->temp2());

  masm.wasmCompareExchange(mir->access(), srcAddr, oldval, newval, valueTemp,
                           offsetTemp, maskTemp, ToRegister(ins->output()),
                           zeroExtend);
}

void CodeGenerator::visitWasmAtomicExchangeHeap(LWasmAtomicExchangeHeap* ins) {
  MWasmAtomicExchangeHeap* mir = ins->mir();
  Register memoryBase = ToRegister(ins->memoryBase());
  Register ptrReg = ToRegister(ins->ptr());
  Register value = ToRegister(ins->value());
  BaseIndex srcAddr(memoryBase, ptrReg, TimesOne, mir->access().offset32());
  auto zeroExtend = ToZeroExtendIndex(ins);

  Register valueTemp = ToTempRegisterOrInvalid(ins->temp0());
  Register offsetTemp = ToTempRegisterOrInvalid(ins->temp1());
  Register maskTemp = ToTempRegisterOrInvalid(ins->temp2());

  masm.wasmAtomicExchange(mir->access(), srcAddr, value, valueTemp, offsetTemp,
                          maskTemp, ToRegister(ins->output()), zeroExtend);
}

void CodeGenerator::visitWasmAtomicBinopHeap(LWasmAtomicBinopHeap* ins) {
  MOZ_ASSERT(ins->mir()->hasUses());

  MWasmAtomicBinopHeap* mir = ins->mir();
  Register memoryBase = ToRegister(ins->memoryBase());
  Register ptrReg = ToRegister(ins->ptr());
  Register valueTemp = ToTempRegisterOrInvalid(ins->temp0());
  Register offsetTemp = ToTempRegisterOrInvalid(ins->temp1());
  Register maskTemp = ToTempRegisterOrInvalid(ins->temp2());

  BaseIndex srcAddr(memoryBase, ptrReg, TimesOne, mir->access().offset32());
  auto zeroExtend = ToZeroExtendIndex(ins);

  masm.wasmAtomicFetchOp(
      mir->access(), mir->operation(), ToRegister(ins->value()), srcAddr,
      valueTemp, offsetTemp, maskTemp, ToRegister(ins->output()), zeroExtend);
}

void CodeGenerator::visitWasmAtomicBinopHeapForEffect(
    LWasmAtomicBinopHeapForEffect* ins) {
  MOZ_ASSERT(!ins->mir()->hasUses());

  MWasmAtomicBinopHeap* mir = ins->mir();
  Register memoryBase = ToRegister(ins->memoryBase());
  Register ptrReg = ToRegister(ins->ptr());
  Register valueTemp = ToTempRegisterOrInvalid(ins->temp0());
  Register offsetTemp = ToTempRegisterOrInvalid(ins->temp1());
  Register maskTemp = ToTempRegisterOrInvalid(ins->temp2());

  BaseIndex srcAddr(memoryBase, ptrReg, TimesOne, mir->access().offset32());
  auto zeroExtend = ToZeroExtendIndex(ins);

  masm.wasmAtomicEffectOp(mir->access(), mir->operation(),
                          ToRegister(ins->value()), srcAddr, valueTemp,
                          offsetTemp, maskTemp, zeroExtend);
}

void CodeGenerator::visitWasmStackArg(LWasmStackArg* ins) {
  const MWasmStackArg* mir = ins->mir();
  Address dst(StackPointer, mir->spOffset());
  if (ins->arg()->isConstant()) {
    masm.storePtr(ImmWord(ToInt32(ins->arg())), dst);
  } else if (ins->arg()->isGeneralReg()) {
    masm.storePtr(ToRegister(ins->arg()), dst);
  } else {
    switch (mir->input()->type()) {
      case MIRType::Double:
        masm.storeDouble(ToFloatRegister(ins->arg()), dst);
        return;
      case MIRType::Float32:
        masm.storeFloat32(ToFloatRegister(ins->arg()), dst);
        return;
#ifdef ENABLE_JIT_SIMD
      case MIRType::Simd128:
        masm.storeUnalignedSimd128(ToFloatRegister(ins->arg()), dst);
        return;
#endif
      default:
        break;
    }
    MOZ_CRASH("unexpected mir type in WasmStackArg");
  }
}

void CodeGenerator::visitWasmStackArgI64(LWasmStackArgI64* ins) {
  const MWasmStackArg* mir = ins->mir();
  Address dst(StackPointer, mir->spOffset());
  if (IsConstant(ins->arg())) {
    masm.store64(Imm64(ToInt64(ins->arg())), dst);
  } else {
    masm.store64(ToRegister64(ins->arg()), dst);
  }
}

void CodeGenerator::visitWasmSelect(LWasmSelect* ins) {
  MIRType mirType = ins->mir()->type();

  Register cond = ToRegister(ins->condExpr());

  if (mirType == MIRType::Int32 || mirType == MIRType::WasmAnyRef) {
    Register trueExpr = ToRegister(ins->trueExpr());
    Register falseExpr = ToRegister(ins->falseExpr());
    Register output = ToRegister(ins->output());

    UseScratchRegisterScope temps(masm);
    Register scratch = temps.Acquire();

    masm.ma_cselnz(output, trueExpr, falseExpr, cond, scratch);
    return;
  }

  const LAllocation* falseExpr = ins->falseExpr();
  FloatRegister out = ToFloatRegister(ins->output());
  MOZ_ASSERT(ToFloatRegister(ins->trueExpr()) == out,
             "true expr input is reused for output");

  if (falseExpr->isFloatReg()) {
    if (mirType == MIRType::Float32) {
      masm.ma_fmovz(Assembler::SingleFloat, out, ToFloatRegister(falseExpr),
                    cond);
    } else if (mirType == MIRType::Double) {
      masm.ma_fmovz(Assembler::DoubleFloat, out, ToFloatRegister(falseExpr),
                    cond);
#ifdef ENABLE_JIT_SIMD
    } else if (mirType == MIRType::Simd128) {
      Label done;
      masm.ma_b(cond, cond, &done, Assembler::NonZero, ShortJump);
      masm.moveSimd128(ToFloatRegister(falseExpr), out);
      masm.bind(&done);
#endif
    } else {
      MOZ_CRASH("unhandled type in visitWasmSelect!");
    }
  } else {
    Label done;
    masm.ma_b(cond, cond, &done, Assembler::NonZero, ShortJump);

    if (mirType == MIRType::Float32) {
      masm.loadFloat32(ToAddress(falseExpr), out);
    } else if (mirType == MIRType::Double) {
      masm.loadDouble(ToAddress(falseExpr), out);
    } else {
      MOZ_CRASH("unhandled type in visitWasmSelect!");
    }

    masm.bind(&done);
  }
}

// We expect to handle these cases (as in "compare x select"):
// {{U,}Int32, {U,}Int64}, Float32, Double}
// x
// {{U,}Int32, {U,}Int64}, Float32, Double}
void CodeGenerator::visitWasmCompareAndSelect(LWasmCompareAndSelect* ins) {
  const MCompare::CompareType compTy = ins->compareType();
  MOZ_RELEASE_ASSERT(
      compTy == MCompare::Compare_Int32 || compTy == MCompare::Compare_UInt32 ||
          compTy == MCompare::Compare_Int64 ||
          compTy == MCompare::Compare_UInt64 ||
          compTy == MCompare::Compare_Float32 ||
          compTy == MCompare::Compare_Double,
      "CodeGenerator::visitWasmCompareAndSelect: unexpected compare type");
  const bool compTyIsFloat =
      compTy == MCompare::Compare_Float32 || compTy == MCompare::Compare_Double;

  const MIRType insTy = ins->mir()->type();
  MOZ_RELEASE_ASSERT(
      insTy == MIRType::Int32 || insTy == MIRType::Int64 ||
          insTy == MIRType::Float32 || insTy == MIRType::Double,
      "CodeGenerator::visitWasmCompareAndSelect: unexpected select type");
  const bool insTyIsFloat =
      insTy == MIRType::Float32 || insTy == MIRType::Double;

  UseScratchRegisterScope temps(masm);
  Register scratch = temps.Acquire();

  if (compTyIsFloat) {
    FloatRegister lhs = ToFloatRegister(ins->leftExpr());
    FloatRegister rhs = ToFloatRegister(ins->rightExpr());
    Assembler::DoubleCondition cond = JSOpToDoubleCondition(ins->jsop());

    if (compTy == MCompare::Compare_Float32) {
      masm.ma_cmp_set_float32(Assembler::FCC0, lhs, rhs, cond);
    } else {
      masm.ma_cmp_set_double(Assembler::FCC0, lhs, rhs, cond);
    }
    if (!insTyIsFloat) {
      // For later use in mask{eq,ne}z.
      masm.as_movcf2gr(scratch, Assembler::FCC0);
    }
  } else {
    Register lhs = ToRegister(ins->leftExpr());
    Register rhs = ToRegister(ins->rightExpr());
    Assembler::Condition cond = JSOpToCondition(compTy, ins->jsop());

    masm.ma_cmp_set(scratch, lhs, rhs, cond);
    if (insTyIsFloat) {
      // For later use in fsel.
      masm.as_movgr2cf(Assembler::FCC0, scratch);
    }
  }

  if (insTyIsFloat) {
    FloatRegister trueExpr = ToFloatRegister(ins->ifTrueExpr());
    FloatRegister falseExpr = ToFloatRegister(ins->ifFalseExpr());
    FloatRegister output = ToFloatRegister(ins->output());

    masm.as_fsel(output, falseExpr, trueExpr, Assembler::FCC0);
  } else {
    Register trueExpr = ToRegister(ins->ifTrueExpr());
    Register falseExpr = ToRegister(ins->ifFalseExpr());
    Register output = ToRegister(ins->output());

    masm.ma_cselnz(output, trueExpr, falseExpr, scratch, scratch);
  }
}

void CodeGenerator::visitUDivConstant(LUDivConstant* ins) {
  const Register lhs = ToRegister(ins->numerator());
  const Register output = ToRegister(ins->output());
  const uint32_t d = ins->denominator();

  const MDiv* const mir = ins->mir();

  if (d == 0) {
    if (mir->trapOnError()) {
      masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
    } else if (mir->canTruncateInfinities()) {
      masm.move32(Imm32(0), output);
    } else {
      MOZ_ASSERT(mir->fallible());
      bailout(ins->snapshot());
    }
    return;
  }

  // Compute the truncated division result in |output|.
  UnsignedDivideWithConstant(masm, ins);

  // We are checking whether the division resulted in an integer, we multiply
  // the obtained value by d to check if the correct answer is an integer. This
  // cannot overflow, since |d| > 1.
  if (!mir->isTruncated()) {
    UseScratchRegisterScope temps(masm);
    Register temp = temps.Acquire();

    masm.ma_mul(temp, output, Imm32(d));
    bailoutCmp32(Assembler::NotEqual, lhs, temp, ins->snapshot());
  }
}

void CodeGenerator::visitUModConstant(LUModConstant* ins) {
  const Register lhs = ToRegister(ins->numerator());
  const Register output = ToRegister(ins->output());
  const uint32_t d = ins->denominator();

  const MMod* const mir = ins->mir();

  if (d == 0) {
    if (mir->trapOnError()) {
      masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
    } else if (mir->isTruncated()) {
      masm.move32(Imm32(0), output);
    } else {
      MOZ_ASSERT(mir->fallible());
      bailout(ins->snapshot());
    }
    return;
  }

  // Compute the truncated division result in |output|.
  UnsignedDivideWithConstant(masm, ins);

  // Compute the remainder: output = lhs - (output * d).
  masm.ma_mul(output, output, Imm32(d));
  masm.as_sub_w(output, lhs, output);

  // Bail if not truncated and the remainder is in the range [2^31, 2^32).
  if (!mir->isTruncated()) {
    bailoutTest32(Assembler::Signed, output, output, ins->snapshot());
  }
}

void CodeGenerator::visitUDivOrMod(LUDivOrMod* ins) {
  Register lhs = ToRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());
  Register output = ToRegister(ins->output());
  Label done;

  // Prevent divide by zero.
  if (ins->canBeDivideByZero()) {
    if (ins->mir()->isTruncated()) {
      if (ins->trapOnError()) {
        Label nonZero;
        masm.ma_b(rhs, rhs, &nonZero, Assembler::NonZero);
        masm.wasmTrap(wasm::Trap::IntegerDivideByZero, ins->trapSiteDesc());
        masm.bind(&nonZero);
      } else {
        // Infinity|0 == 0
        Label notzero;
        masm.ma_b(rhs, rhs, &notzero, Assembler::NonZero, ShortJump);
        masm.move32(Imm32(0), output);
        masm.ma_b(&done, ShortJump);
        masm.bind(&notzero);
      }
    } else {
      bailoutCmp32(Assembler::Equal, rhs, Imm32(0), ins->snapshot());
    }
  }

  masm.as_mod_wu(output, lhs, rhs);

  // If the remainder is > 0, bailout since this must be a double.
  if (ins->mir()->isDiv()) {
    if (!ins->mir()->toDiv()->canTruncateRemainder()) {
      bailoutCmp32(Assembler::NonZero, output, output, ins->snapshot());
    }
    // Get quotient
    masm.as_div_wu(output, lhs, rhs);
  }

  if (!ins->mir()->isTruncated()) {
    bailoutCmp32(Assembler::LessThan, output, Imm32(0), ins->snapshot());
  }

  masm.bind(&done);
}

void CodeGenerator::visitEffectiveAddress3(LEffectiveAddress3* ins) {
  const MEffectiveAddress3* mir = ins->mir();
  Register base = ToRegister(ins->base());
  Register index = ToRegister(ins->index());
  Register output = ToRegister(ins->output());

  BaseIndex address(base, index, mir->scale(), mir->displacement());
  masm.computeEffectiveAddress32(address, output);
}

void CodeGenerator::visitEffectiveAddress2(LEffectiveAddress2* ins) {
  const MEffectiveAddress2* mir = ins->mir();
  Register index = ToRegister(ins->index());
  Register output = ToRegister(ins->output());

  BaseIndex address(zero, index, mir->scale(), mir->displacement());
  masm.computeEffectiveAddress32(address, output);
}

void CodeGenerator::visitNegI(LNegI* ins) {
  Register input = ToRegister(ins->input());
  Register output = ToRegister(ins->output());

  masm.as_sub_w(output, zero, input);
}

void CodeGenerator::visitNegI64(LNegI64* ins) {
  Register input = ToRegister64(ins->input()).reg;
  Register output = ToOutRegister64(ins).reg;

  masm.as_sub_d(output, zero, input);
}

void CodeGenerator::visitNegD(LNegD* ins) {
  FloatRegister input = ToFloatRegister(ins->input());
  FloatRegister output = ToFloatRegister(ins->output());

  masm.as_fneg_d(output, input);
}

void CodeGenerator::visitNegF(LNegF* ins) {
  FloatRegister input = ToFloatRegister(ins->input());
  FloatRegister output = ToFloatRegister(ins->output());

  masm.as_fneg_s(output, input);
}

void CodeGenerator::visitWasmAddOffset(LWasmAddOffset* lir) {
  MWasmAddOffset* mir = lir->mir();
  Register base = ToRegister(lir->base());
  Register out = ToRegister(lir->output());

  Label ok;
  masm.ma_add32TestCarry(Assembler::CarryClear, out, base, Imm32(mir->offset()),
                         &ok);
  masm.wasmTrap(wasm::Trap::OutOfBounds, mir->trapSiteDesc());
  masm.bind(&ok);
}

void CodeGenerator::visitWasmAddOffset64(LWasmAddOffset64* lir) {
  MWasmAddOffset* mir = lir->mir();
  Register64 base = ToRegister64(lir->base());
  Register64 out = ToOutRegister64(lir);

  Label ok;
  masm.ma_addPtrTestCarry(Assembler::CarryClear, out.reg, base.reg,
                          ImmWord(mir->offset()), &ok);
  masm.wasmTrap(wasm::Trap::OutOfBounds, mir->trapSiteDesc());
  masm.bind(&ok);
}

void CodeGenerator::visitAtomicTypedArrayElementBinop(
    LAtomicTypedArrayElementBinop* lir) {
  MOZ_ASSERT(!lir->mir()->isForEffect());

  Register output = ToRegister(lir->output());
  Register elements = ToRegister(lir->elements());
  Register valueTemp = ToTempRegisterOrInvalid(lir->temp0());
  Register offsetTemp = ToTempRegisterOrInvalid(lir->temp1());
  Register maskTemp = ToTempRegisterOrInvalid(lir->temp2());
  Register value = ToRegister(lir->value());
  Scalar::Type arrayType = lir->mir()->arrayType();

  auto mem = ToAddressOrBaseIndex(elements, lir->index(), arrayType);

  mem.match([&](const auto& mem) {
    masm.atomicFetchOp(arrayType, Synchronization::Full(),
                       lir->mir()->operation(), value, mem, valueTemp,
                       offsetTemp, maskTemp, output);
  });
}

void CodeGenerator::visitAtomicTypedArrayElementBinopForEffect(
    LAtomicTypedArrayElementBinopForEffect* lir) {
  MOZ_ASSERT(lir->mir()->isForEffect());

  Register elements = ToRegister(lir->elements());
  Register valueTemp = ToTempRegisterOrInvalid(lir->temp0());
  Register offsetTemp = ToTempRegisterOrInvalid(lir->temp1());
  Register maskTemp = ToTempRegisterOrInvalid(lir->temp2());
  Register value = ToRegister(lir->value());
  Scalar::Type arrayType = lir->mir()->arrayType();

  auto mem = ToAddressOrBaseIndex(elements, lir->index(), arrayType);

  mem.match([&](const auto& mem) {
    masm.atomicEffectOp(arrayType, Synchronization::Full(),
                        lir->mir()->operation(), value, mem, valueTemp,
                        offsetTemp, maskTemp);
  });
}

void CodeGenerator::visitCompareExchangeTypedArrayElement(
    LCompareExchangeTypedArrayElement* lir) {
  Register elements = ToRegister(lir->elements());
  Register output = ToRegister(lir->output());

  Register oldval = ToRegister(lir->oldval());
  Register newval = ToRegister(lir->newval());
  Register valueTemp = ToTempRegisterOrInvalid(lir->temp0());
  Register offsetTemp = ToTempRegisterOrInvalid(lir->temp1());
  Register maskTemp = ToTempRegisterOrInvalid(lir->temp2());
  Scalar::Type arrayType = lir->mir()->arrayType();

  auto dest = ToAddressOrBaseIndex(elements, lir->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.compareExchange(arrayType, Synchronization::Full(), dest, oldval,
                         newval, valueTemp, offsetTemp, maskTemp, output);
  });
}

void CodeGenerator::visitAtomicExchangeTypedArrayElement(
    LAtomicExchangeTypedArrayElement* lir) {
  Register elements = ToRegister(lir->elements());
  Register output = ToRegister(lir->output());

  Register value = ToRegister(lir->value());
  Register valueTemp = ToTempRegisterOrInvalid(lir->temp0());
  Register offsetTemp = ToTempRegisterOrInvalid(lir->temp1());
  Register maskTemp = ToTempRegisterOrInvalid(lir->temp2());
  Scalar::Type arrayType = lir->mir()->arrayType();

  auto dest = ToAddressOrBaseIndex(elements, lir->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.atomicExchange(arrayType, Synchronization::Full(), dest, value,
                        valueTemp, offsetTemp, maskTemp, output);
  });
}

void CodeGenerator::visitCompareExchangeTypedArrayElement64(
    LCompareExchangeTypedArrayElement64* lir) {
  Register elements = ToRegister(lir->elements());
  Register64 oldval = ToRegister64(lir->oldval());
  Register64 newval = ToRegister64(lir->newval());
  Register64 out = ToOutRegister64(lir);
  Scalar::Type arrayType = lir->mir()->arrayType();

  auto dest = ToAddressOrBaseIndex(elements, lir->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.compareExchange64(Synchronization::Full(), dest, oldval, newval, out);
  });
}

void CodeGenerator::visitAtomicExchangeTypedArrayElement64(
    LAtomicExchangeTypedArrayElement64* lir) {
  Register elements = ToRegister(lir->elements());
  Register64 value = ToRegister64(lir->value());
  Register64 out = ToOutRegister64(lir);
  Scalar::Type arrayType = lir->mir()->arrayType();

  auto dest = ToAddressOrBaseIndex(elements, lir->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.atomicExchange64(Synchronization::Full(), dest, value, out);
  });
}

void CodeGenerator::visitAtomicTypedArrayElementBinop64(
    LAtomicTypedArrayElementBinop64* lir) {
  MOZ_ASSERT(lir->mir()->hasUses());

  Register elements = ToRegister(lir->elements());
  Register64 value = ToRegister64(lir->value());
  Register64 temp = ToTempRegister64OrInvalid(lir->temp0());
  Register64 out = ToOutRegister64(lir);

  Scalar::Type arrayType = lir->mir()->arrayType();
  AtomicOp atomicOp = lir->mir()->operation();

  auto dest = ToAddressOrBaseIndex(elements, lir->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.atomicFetchOp64(Synchronization::Full(), atomicOp, value, dest, temp,
                         out);
  });
}

void CodeGenerator::visitAtomicTypedArrayElementBinopForEffect64(
    LAtomicTypedArrayElementBinopForEffect64* lir) {
  MOZ_ASSERT(!lir->mir()->hasUses());

  Register elements = ToRegister(lir->elements());
  Register64 value = ToRegister64(lir->value());

  Scalar::Type arrayType = lir->mir()->arrayType();
  AtomicOp atomicOp = lir->mir()->operation();

  auto dest = ToAddressOrBaseIndex(elements, lir->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.atomicEffectOp64(Synchronization::Full(), atomicOp, value, dest);
  });
}

void CodeGenerator::visitAtomicLoad64(LAtomicLoad64* lir) {
  Register elements = ToRegister(lir->elements());
  Register64 out = ToOutRegister64(lir);

  Scalar::Type storageType = lir->mir()->storageType();

  auto source = ToAddressOrBaseIndex(elements, lir->index(), storageType);

  auto sync = Synchronization::Load();
  masm.memoryBarrierBefore(sync);
  source.match([&](const auto& source) { masm.load64(source, out); });
  masm.memoryBarrierAfter(sync);
}

void CodeGenerator::visitAtomicStore64(LAtomicStore64* lir) {
  Register elements = ToRegister(lir->elements());
  Register64 value = ToRegister64(lir->value());

  Scalar::Type writeType = lir->mir()->writeType();

  auto dest = ToAddressOrBaseIndex(elements, lir->index(), writeType);

  auto sync = Synchronization::Store();
  masm.memoryBarrierBefore(sync);
  dest.match([&](const auto& dest) { masm.store64(value, dest); });
  masm.memoryBarrierAfter(sync);
}

void CodeGenerator::visitWasmCompareExchangeI64(LWasmCompareExchangeI64* lir) {
  Register memoryBase = ToRegister(lir->memoryBase());
  Register ptr = ToRegister(lir->ptr());
  Register64 oldValue = ToRegister64(lir->oldValue());
  Register64 newValue = ToRegister64(lir->newValue());
  Register64 output = ToOutRegister64(lir);
  uint32_t offset = lir->mir()->access().offset32();

  BaseIndex addr(memoryBase, ptr, TimesOne, offset);
  auto zeroExtend = ToZeroExtendIndex(lir);

  masm.wasmCompareExchange64(lir->mir()->access(), addr, oldValue, newValue,
                             output, zeroExtend);
}

void CodeGenerator::visitWasmAtomicExchangeI64(LWasmAtomicExchangeI64* lir) {
  Register memoryBase = ToRegister(lir->memoryBase());
  Register ptr = ToRegister(lir->ptr());
  Register64 value = ToRegister64(lir->value());
  Register64 output = ToOutRegister64(lir);
  uint32_t offset = lir->mir()->access().offset32();

  BaseIndex addr(memoryBase, ptr, TimesOne, offset);
  auto zeroExtend = ToZeroExtendIndex(lir);

  masm.wasmAtomicExchange64(lir->mir()->access(), addr, value, output,
                            zeroExtend);
}

void CodeGenerator::visitWasmAtomicBinopI64(LWasmAtomicBinopI64* lir) {
  Register memoryBase = ToRegister(lir->memoryBase());
  Register ptr = ToRegister(lir->ptr());
  Register64 value = ToRegister64(lir->value());
  Register64 output = ToOutRegister64(lir);
  Register64 temp = ToTempRegister64OrInvalid(lir->temp0());
  uint32_t offset = lir->mir()->access().offset32();

  BaseIndex addr(memoryBase, ptr, TimesOne, offset);
  auto zeroExtend = ToZeroExtendIndex(lir);

  masm.wasmAtomicFetchOp64(lir->mir()->access(), lir->mir()->operation(), value,
                           addr, temp, output, zeroExtend);
}

void CodeGenerator::visitSimd128(LSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  const LDefinition* out = ins->output();
  masm.loadConstantSimd128(ins->simd128(), ToFloatRegister(out));
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmTernarySimd128(LWasmTernarySimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  switch (ins->simdOp()) {
    case wasm::SimdOp::V128Bitselect:
      masm.bitwiseSelectSimd128(ToFloatRegister(ins->v0()),
                                ToFloatRegister(ins->v1()),
                                ToFloatRegister(ins->v2()));
      break;
    case wasm::SimdOp::F32x4RelaxedMadd:
      masm.fmaFloat32x4(ToFloatRegister(ins->v0()), ToFloatRegister(ins->v1()),
                        ToFloatRegister(ins->v2()));
      break;
    case wasm::SimdOp::F32x4RelaxedNmadd:
      masm.fnmaFloat32x4(ToFloatRegister(ins->v0()), ToFloatRegister(ins->v1()),
                         ToFloatRegister(ins->v2()));
      break;
    case wasm::SimdOp::F64x2RelaxedMadd:
      masm.fmaFloat64x2(ToFloatRegister(ins->v0()), ToFloatRegister(ins->v1()),
                        ToFloatRegister(ins->v2()));
      break;
    case wasm::SimdOp::F64x2RelaxedNmadd:
      masm.fnmaFloat64x2(ToFloatRegister(ins->v0()), ToFloatRegister(ins->v1()),
                         ToFloatRegister(ins->v2()));
      break;
    case wasm::SimdOp::I8x16RelaxedLaneSelect:
    case wasm::SimdOp::I16x8RelaxedLaneSelect:
    case wasm::SimdOp::I32x4RelaxedLaneSelect:
    case wasm::SimdOp::I64x2RelaxedLaneSelect: {
      FloatRegister maskDest = ToFloatRegister(ins->v2());
      masm.laneSelectSimd128(maskDest, ToFloatRegister(ins->v0()),
                             ToFloatRegister(ins->v1()), maskDest);
      break;
    }
    case wasm::SimdOp::I32x4RelaxedDotI8x16I7x16AddS:
      masm.dotInt8x16Int7x16ThenAdd(
          ToFloatRegister(ins->v0()), ToFloatRegister(ins->v1()),
          ToFloatRegister(ins->v2()), ToFloatRegister(ins->temp0()));
      break;
    default:
      MOZ_CRASH("NYI");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmBinarySimd128(LWasmBinarySimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  FloatRegister lhs = ToFloatRegister(ins->lhs());
  FloatRegister rhs = ToFloatRegister(ins->rhs());
  FloatRegister dest = ToFloatRegister(ins->output());

  switch (ins->simdOp()) {
    case wasm::SimdOp::V128And:
      masm.bitwiseAndSimd128(lhs, rhs, dest);
      break;
    case wasm::SimdOp::V128Or:
      masm.bitwiseOrSimd128(lhs, rhs, dest);
      break;
    case wasm::SimdOp::V128Xor:
      masm.bitwiseXorSimd128(lhs, rhs, dest);
      break;
    case wasm::SimdOp::V128AndNot:
      masm.bitwiseAndNotSimd128(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16AvgrU:
      masm.unsignedAverageInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8AvgrU:
      masm.unsignedAverageInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16Add:
      masm.addInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16AddSatS:
      masm.addSatInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16AddSatU:
      masm.unsignedAddSatInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16Sub:
      masm.subInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16SubSatS:
      masm.subSatInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16SubSatU:
      masm.unsignedSubSatInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16MinS:
      masm.minInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16MinU:
      masm.unsignedMinInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16MaxS:
      masm.maxInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16MaxU:
      masm.unsignedMaxInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8Add:
      masm.addInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8AddSatS:
      masm.addSatInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8AddSatU:
      masm.unsignedAddSatInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8Sub:
      masm.subInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8SubSatS:
      masm.subSatInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8SubSatU:
      masm.unsignedSubSatInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8Mul:
      masm.mulInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8MinS:
      masm.minInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8MinU:
      masm.unsignedMinInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8MaxS:
      masm.maxInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8MaxU:
      masm.unsignedMaxInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4Add:
      masm.addInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4Sub:
      masm.subInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4Mul:
      masm.mulInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4MinS:
      masm.minInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4MinU:
      masm.unsignedMinInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4MaxS:
      masm.maxInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4MaxU:
      masm.unsignedMaxInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2Add:
      masm.addInt64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2Sub:
      masm.subInt64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2Mul: {
      masm.mulInt64x2(lhs, rhs, dest);
      break;
    }
    case wasm::SimdOp::F32x4Add:
      masm.addFloat32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4Sub:
      masm.subFloat32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4Mul:
      masm.mulFloat32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4Div:
      masm.divFloat32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4Min:
      masm.minFloat32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4Max:
      masm.maxFloat32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Add:
      masm.addFloat64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Sub:
      masm.subFloat64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Mul:
      masm.mulFloat64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Div:
      masm.divFloat64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Min:
      masm.minFloat64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Max:
      masm.maxFloat64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16Swizzle:
      masm.swizzleInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16RelaxedSwizzle:
      masm.swizzleInt8x16Relaxed(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16NarrowI16x8S:
      masm.narrowInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16NarrowI16x8U:
      masm.unsignedNarrowInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8NarrowI32x4S:
      masm.narrowInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8NarrowI32x4U:
      masm.unsignedNarrowInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16Eq:
      masm.compareInt8x16(Assembler::Equal, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16Ne:
      masm.compareInt8x16(Assembler::NotEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16LtS:
      masm.compareInt8x16(Assembler::LessThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16GtS:
      masm.compareInt8x16(Assembler::GreaterThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16LeS:
      masm.compareInt8x16(Assembler::LessThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16GeS:
      masm.compareInt8x16(Assembler::GreaterThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16LtU:
      masm.compareInt8x16(Assembler::Below, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16GtU:
      masm.compareInt8x16(Assembler::Above, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16LeU:
      masm.compareInt8x16(Assembler::BelowOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16GeU:
      masm.compareInt8x16(Assembler::AboveOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8Eq:
      masm.compareInt16x8(Assembler::Equal, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8Ne:
      masm.compareInt16x8(Assembler::NotEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8LtS:
      masm.compareInt16x8(Assembler::LessThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8GtS:
      masm.compareInt16x8(Assembler::GreaterThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8LeS:
      masm.compareInt16x8(Assembler::LessThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8GeS:
      masm.compareInt16x8(Assembler::GreaterThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8LtU:
      masm.compareInt16x8(Assembler::Below, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8GtU:
      masm.compareInt16x8(Assembler::Above, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8LeU:
      masm.compareInt16x8(Assembler::BelowOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8GeU:
      masm.compareInt16x8(Assembler::AboveOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4Eq:
      masm.compareInt32x4(Assembler::Equal, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4Ne:
      masm.compareInt32x4(Assembler::NotEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4LtS:
      masm.compareInt32x4(Assembler::LessThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4GtS:
      masm.compareInt32x4(Assembler::GreaterThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4LeS:
      masm.compareInt32x4(Assembler::LessThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4GeS:
      masm.compareInt32x4(Assembler::GreaterThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4LtU:
      masm.compareInt32x4(Assembler::Below, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4GtU:
      masm.compareInt32x4(Assembler::Above, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4LeU:
      masm.compareInt32x4(Assembler::BelowOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4GeU:
      masm.compareInt32x4(Assembler::AboveOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2Eq:
      masm.compareInt64x2(Assembler::Equal, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2LtS:
      masm.compareInt64x2(Assembler::LessThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2GtS:
      masm.compareInt64x2(Assembler::GreaterThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2LeS:
      masm.compareInt64x2(Assembler::LessThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2GeS:
      masm.compareInt64x2(Assembler::GreaterThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2Ne:
      masm.compareInt64x2(Assembler::NotEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4Eq:
      masm.compareFloat32x4(Assembler::Equal, lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4Ne:
      masm.compareFloat32x4(Assembler::NotEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4Lt:
      masm.compareFloat32x4(Assembler::LessThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4Gt:
      masm.compareFloat32x4(Assembler::GreaterThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4Le:
      masm.compareFloat32x4(Assembler::LessThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4Ge:
      masm.compareFloat32x4(Assembler::GreaterThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Eq:
      masm.compareFloat64x2(Assembler::Equal, lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Ne:
      masm.compareFloat64x2(Assembler::NotEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Lt:
      masm.compareFloat64x2(Assembler::LessThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Gt:
      masm.compareFloat64x2(Assembler::GreaterThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Le:
      masm.compareFloat64x2(Assembler::LessThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Ge:
      masm.compareFloat64x2(Assembler::GreaterThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4PMax:
      masm.pseudoMaxFloat32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4PMin:
      masm.pseudoMinFloat32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2PMax:
      masm.pseudoMaxFloat64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2PMin:
      masm.pseudoMinFloat64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4DotI16x8S:
      masm.widenDotInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8ExtmulLowI8x16S:
      masm.extMulLowInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8ExtmulHighI8x16S:
      masm.extMulHighInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8ExtmulLowI8x16U:
      masm.unsignedExtMulLowInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8ExtmulHighI8x16U:
      masm.unsignedExtMulHighInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4ExtmulLowI16x8S:
      masm.extMulLowInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4ExtmulHighI16x8S:
      masm.extMulHighInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4ExtmulLowI16x8U:
      masm.unsignedExtMulLowInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4ExtmulHighI16x8U:
      masm.unsignedExtMulHighInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2ExtmulLowI32x4S:
      masm.extMulLowInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2ExtmulHighI32x4S:
      masm.extMulHighInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2ExtmulLowI32x4U:
      masm.unsignedExtMulLowInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2ExtmulHighI32x4U:
      masm.unsignedExtMulHighInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8Q15MulrSatS:
      masm.q15MulrSatInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4RelaxedMin:
      masm.minFloat32x4Relaxed(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4RelaxedMax:
      masm.maxFloat32x4Relaxed(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2RelaxedMin:
      masm.minFloat64x2Relaxed(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2RelaxedMax:
      masm.maxFloat64x2Relaxed(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8RelaxedQ15MulrS:
      masm.q15MulrInt16x8Relaxed(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8RelaxedDotI8x16I7x16S:
      masm.dotInt8x16Int7x16(lhs, rhs, dest);
      break;
    default:
      MOZ_CRASH("Binary SimdOp not implemented");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmBinarySimd128WithConstant(
    LWasmBinarySimd128WithConstant* ins) {
#ifdef ENABLE_JIT_SIMD
  FloatRegister lhs = ToFloatRegister(ins->lhs());
  const SimdConstant& rhs = ins->rhs();
  FloatRegister dest = ToFloatRegister(ins->output());

  // Only integer compares arrive here. See also CanSpecializeConstantOperand().
  switch (ins->mir()->simdOp()) {
    case wasm::SimdOp::I8x16Eq:
      masm.compareInt8x16(Assembler::Equal, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16Ne:
      masm.compareInt8x16(Assembler::NotEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16LtS:
      masm.compareInt8x16(Assembler::LessThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16GtS:
      masm.compareInt8x16(Assembler::GreaterThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16LeS:
      masm.compareInt8x16(Assembler::LessThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16GeS:
      masm.compareInt8x16(Assembler::GreaterThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16LtU:
      masm.compareInt8x16(Assembler::Below, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16GtU:
      masm.compareInt8x16(Assembler::Above, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16LeU:
      masm.compareInt8x16(Assembler::BelowOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16GeU:
      masm.compareInt8x16(Assembler::AboveOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8Eq:
      masm.compareInt16x8(Assembler::Equal, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8Ne:
      masm.compareInt16x8(Assembler::NotEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8LtS:
      masm.compareInt16x8(Assembler::LessThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8GtS:
      masm.compareInt16x8(Assembler::GreaterThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8LeS:
      masm.compareInt16x8(Assembler::LessThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8GeS:
      masm.compareInt16x8(Assembler::GreaterThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8LtU:
      masm.compareInt16x8(Assembler::Below, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8GtU:
      masm.compareInt16x8(Assembler::Above, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8LeU:
      masm.compareInt16x8(Assembler::BelowOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8GeU:
      masm.compareInt16x8(Assembler::AboveOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4Eq:
      masm.compareInt32x4(Assembler::Equal, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4Ne:
      masm.compareInt32x4(Assembler::NotEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4LtS:
      masm.compareInt32x4(Assembler::LessThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4GtS:
      masm.compareInt32x4(Assembler::GreaterThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4LeS:
      masm.compareInt32x4(Assembler::LessThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4GeS:
      masm.compareInt32x4(Assembler::GreaterThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4LtU:
      masm.compareInt32x4(Assembler::Below, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4GtU:
      masm.compareInt32x4(Assembler::Above, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4LeU:
      masm.compareInt32x4(Assembler::BelowOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4GeU:
      masm.compareInt32x4(Assembler::AboveOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2Eq:
      masm.compareInt64x2(Assembler::Equal, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2Ne:
      masm.compareInt64x2(Assembler::NotEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2LtS:
      masm.compareInt64x2(Assembler::LessThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2GtS:
      masm.compareInt64x2(Assembler::GreaterThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2LeS:
      masm.compareInt64x2(Assembler::LessThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2GeS:
      masm.compareInt64x2(Assembler::GreaterThanOrEqual, lhs, rhs, dest);
      break;
    default:
      MOZ_CRASH("unexpected SIMD compare op");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmVariableShiftSimd128(
    LWasmVariableShiftSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  FloatRegister lhs = ToFloatRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());
  FloatRegister dest = ToFloatRegister(ins->output());

  switch (ins->mir()->simdOp()) {
    case wasm::SimdOp::I8x16Shl:
      masm.leftShiftInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16ShrS:
      masm.rightShiftInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16ShrU:
      masm.unsignedRightShiftInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8Shl:
      masm.leftShiftInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8ShrS:
      masm.rightShiftInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8ShrU:
      masm.unsignedRightShiftInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4Shl:
      masm.leftShiftInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4ShrS:
      masm.rightShiftInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4ShrU:
      masm.unsignedRightShiftInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2Shl:
      masm.leftShiftInt64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2ShrS:
      masm.rightShiftInt64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2ShrU:
      masm.unsignedRightShiftInt64x2(lhs, rhs, dest);
      break;
    default:
      MOZ_CRASH("Shift SimdOp not implemented");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmConstantShiftSimd128(
    LWasmConstantShiftSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  FloatRegister src = ToFloatRegister(ins->src());
  FloatRegister dest = ToFloatRegister(ins->output());
  const int32_t shift = ins->shift();

  if (shift == 0) {
    if (src != dest) {
      masm.moveSimd128(src, dest);
    }
    return;
  }

  switch (ins->mir()->simdOp()) {
    case wasm::SimdOp::I8x16Shl:
      masm.leftShiftInt8x16(Imm32(shift), src, dest);
      break;
    case wasm::SimdOp::I8x16ShrS:
      masm.rightShiftInt8x16(Imm32(shift), src, dest);
      break;
    case wasm::SimdOp::I8x16ShrU:
      masm.unsignedRightShiftInt8x16(Imm32(shift), src, dest);
      break;
    case wasm::SimdOp::I16x8Shl:
      masm.leftShiftInt16x8(Imm32(shift), src, dest);
      break;
    case wasm::SimdOp::I16x8ShrS:
      masm.rightShiftInt16x8(Imm32(shift), src, dest);
      break;
    case wasm::SimdOp::I16x8ShrU:
      masm.unsignedRightShiftInt16x8(Imm32(shift), src, dest);
      break;
    case wasm::SimdOp::I32x4Shl:
      masm.leftShiftInt32x4(Imm32(shift), src, dest);
      break;
    case wasm::SimdOp::I32x4ShrS:
      masm.rightShiftInt32x4(Imm32(shift), src, dest);
      break;
    case wasm::SimdOp::I32x4ShrU:
      masm.unsignedRightShiftInt32x4(Imm32(shift), src, dest);
      break;
    case wasm::SimdOp::I64x2Shl:
      masm.leftShiftInt64x2(Imm32(shift), src, dest);
      break;
    case wasm::SimdOp::I64x2ShrS:
      masm.rightShiftInt64x2(Imm32(shift), src, dest);
      break;
    case wasm::SimdOp::I64x2ShrU:
      masm.unsignedRightShiftInt64x2(Imm32(shift), src, dest);
      break;
    default:
      MOZ_CRASH("Shift SimdOp not implemented");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmSignReplicationSimd128(
    LWasmSignReplicationSimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmShuffleSimd128(LWasmShuffleSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  FloatRegister lhs = ToFloatRegister(ins->lhs());
  FloatRegister rhs = ToFloatRegister(ins->rhs());
  FloatRegister dest = ToFloatRegister(ins->output());
  SimdConstant control = ins->control();
  switch (ins->op()) {
    case SimdShuffleOp::BLEND_8x16:
      masm.blendInt8x16(reinterpret_cast<const uint8_t*>(control.asInt8x16()),
                        lhs, rhs, dest);
      break;
    case SimdShuffleOp::BLEND_16x8:
      masm.blendInt16x8(reinterpret_cast<const uint16_t*>(control.asInt16x8()),
                        lhs, rhs, dest);
      break;
    case SimdShuffleOp::CONCAT_RIGHT_SHIFT_8x16: {
      const int8_t count = 16 - control.asInt8x16()[0];
      MOZ_ASSERT(count > 0, "Should have been a MOVE operation");
      masm.concatAndRightShiftSimd128(lhs, rhs, dest, count);
      break;
    }
    case SimdShuffleOp::INTERLEAVE_HIGH_8x16:
      masm.interleaveHighInt8x16(lhs, rhs, dest);
      break;
    case SimdShuffleOp::INTERLEAVE_HIGH_16x8:
      masm.interleaveHighInt16x8(lhs, rhs, dest);
      break;
    case SimdShuffleOp::INTERLEAVE_HIGH_32x4:
      masm.interleaveHighInt32x4(lhs, rhs, dest);
      break;
    case SimdShuffleOp::INTERLEAVE_HIGH_64x2:
      masm.interleaveHighInt64x2(lhs, rhs, dest);
      break;
    case SimdShuffleOp::INTERLEAVE_LOW_8x16:
      masm.interleaveLowInt8x16(lhs, rhs, dest);
      break;
    case SimdShuffleOp::INTERLEAVE_LOW_16x8:
      masm.interleaveLowInt16x8(lhs, rhs, dest);
      break;
    case SimdShuffleOp::INTERLEAVE_LOW_32x4:
      masm.interleaveLowInt32x4(lhs, rhs, dest);
      break;
    case SimdShuffleOp::INTERLEAVE_LOW_64x2:
      masm.interleaveLowInt64x2(lhs, rhs, dest);
      break;
    case SimdShuffleOp::SHUFFLE_BLEND_8x16:
      masm.shuffleInt8x16(reinterpret_cast<const uint8_t*>(control.asInt8x16()),
                          lhs, rhs, dest);
      break;
    default:
      MOZ_CRASH("Unsupported SIMD shuffle operation");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmPermuteSimd128(LWasmPermuteSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  FloatRegister src = ToFloatRegister(ins->src());
  FloatRegister dest = ToFloatRegister(ins->output());
  SimdConstant control = ins->control();
  switch (ins->op()) {
    case SimdPermuteOp::BROADCAST_8x16: {
      const SimdConstant::I8x16& mask = control.asInt8x16();
      const int8_t source = mask[0];
      masm.splatX16(source, src, dest);
      break;
    }
    case SimdPermuteOp::BROADCAST_16x8: {
      const SimdConstant::I16x8& mask = control.asInt16x8();
      const int16_t source = mask[0];
      masm.splatX8(source, src, dest);
      break;
    }
    case SimdPermuteOp::MOVE:
      masm.moveSimd128(src, dest);
      break;
    case SimdPermuteOp::PERMUTE_8x16: {
      const SimdConstant::I8x16& mask = control.asInt8x16();
#  ifdef DEBUG
      mozilla::DebugOnly<int> i;
      for (i = 0; i < 16 && mask[i] == i; i++) {
      }
      MOZ_ASSERT(i < 16, "Should have been a MOVE operation");
#  endif
      masm.permuteInt8x16(reinterpret_cast<const uint8_t*>(mask), src, dest);
      break;
    }
    case SimdPermuteOp::PERMUTE_16x8: {
      const SimdConstant::I16x8& mask = control.asInt16x8();
#  ifdef DEBUG
      mozilla::DebugOnly<int> i;
      for (i = 0; i < 8 && mask[i] == i; i++) {
      }
      MOZ_ASSERT(i < 8, "Should have been a MOVE operation");
#  endif
      masm.permuteInt16x8(reinterpret_cast<const uint16_t*>(mask), src, dest);
      break;
    }
    case SimdPermuteOp::PERMUTE_32x4: {
      const SimdConstant::I32x4& mask = control.asInt32x4();
#  ifdef DEBUG
      mozilla::DebugOnly<int> i;
      for (i = 0; i < 4 && mask[i] == i; i++) {
      }
      MOZ_ASSERT(i < 4, "Should have been a MOVE operation");
#  endif
      masm.permuteInt32x4(reinterpret_cast<const uint32_t*>(mask), src, dest);
      break;
    }
    case SimdPermuteOp::ROTATE_RIGHT_8x16: {
      const int8_t count = control.asInt8x16()[0];
      MOZ_ASSERT(count > 0, "Should have been a MOVE operation");
      masm.rotateRightSimd128(src, dest, count);
      break;
    }
    case SimdPermuteOp::SHIFT_LEFT_8x16: {
      const int8_t count = control.asInt8x16()[0];
      MOZ_ASSERT(count > 0, "Should have been a MOVE operation");
      masm.leftShiftSimd128(Imm32(count), src, dest);
      break;
    }
    case SimdPermuteOp::SHIFT_RIGHT_8x16: {
      const int8_t count = control.asInt8x16()[0];
      MOZ_ASSERT(count > 0, "Should have been a MOVE operation");
      masm.rightShiftSimd128(Imm32(count), src, dest);
      break;
    }
    case SimdPermuteOp::ZERO_EXTEND_8x16_TO_16x8:
      masm.zeroExtend8x16To16x8(src, dest);
      break;
    case SimdPermuteOp::ZERO_EXTEND_8x16_TO_32x4:
      masm.zeroExtend8x16To32x4(src, dest);
      break;
    case SimdPermuteOp::ZERO_EXTEND_8x16_TO_64x2:
      masm.zeroExtend8x16To64x2(src, dest);
      break;
    case SimdPermuteOp::ZERO_EXTEND_16x8_TO_32x4:
      masm.zeroExtend16x8To32x4(src, dest);
      break;
    case SimdPermuteOp::ZERO_EXTEND_16x8_TO_64x2:
      masm.zeroExtend16x8To64x2(src, dest);
      break;
    case SimdPermuteOp::ZERO_EXTEND_32x4_TO_64x2:
      masm.zeroExtend32x4To64x2(src, dest);
      break;
    case SimdPermuteOp::REVERSE_16x8:
      masm.reverseInt16x8(src, dest);
      break;
    case SimdPermuteOp::REVERSE_32x4:
      masm.reverseInt32x4(src, dest);
      break;
    case SimdPermuteOp::REVERSE_64x2:
      masm.reverseInt64x2(src, dest);
      break;
    default:
      MOZ_CRASH("Unsupported SIMD permutation operation");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmReplaceLaneSimd128(LWasmReplaceLaneSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  MOZ_ASSERT(ToFloatRegister(ins->lhs()) == ToFloatRegister(ins->output()));
  FloatRegister lhsDest = ToFloatRegister(ins->lhs());
  const LAllocation* rhs = ins->rhs();
  const uint32_t laneIndex = ins->mir()->laneIndex();

  if (rhs->isBogus()) {
    // Lowering only produces i32.const 0 here (I64x2 uses useRegister).
    switch (ins->mir()->simdOp()) {
      case wasm::SimdOp::I8x16ReplaceLane:
        masm.replaceLaneInt8x16(laneIndex, zero, lhsDest);
        break;
      case wasm::SimdOp::I16x8ReplaceLane:
        masm.replaceLaneInt16x8(laneIndex, zero, lhsDest);
        break;
      case wasm::SimdOp::I32x4ReplaceLane:
        masm.replaceLaneInt32x4(laneIndex, zero, lhsDest);
        break;
      default:
        MOZ_CRASH("ReplaceLane SimdOp not implemented");
    }
    return;
  }

  switch (ins->mir()->simdOp()) {
    case wasm::SimdOp::I8x16ReplaceLane:
      masm.replaceLaneInt8x16(laneIndex, ToRegister(rhs), lhsDest);
      break;
    case wasm::SimdOp::I16x8ReplaceLane:
      masm.replaceLaneInt16x8(laneIndex, ToRegister(rhs), lhsDest);
      break;
    case wasm::SimdOp::I32x4ReplaceLane:
      masm.replaceLaneInt32x4(laneIndex, ToRegister(rhs), lhsDest);
      break;
    case wasm::SimdOp::F32x4ReplaceLane:
      masm.replaceLaneFloat32x4(laneIndex, ToFloatRegister(rhs), lhsDest);
      break;
    case wasm::SimdOp::F64x2ReplaceLane:
      masm.replaceLaneFloat64x2(laneIndex, ToFloatRegister(rhs), lhsDest);
      break;
    default:
      MOZ_CRASH("ReplaceLane SimdOp not implemented");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmMoveLaneSimd128(LWasmMoveLaneSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  MOZ_ASSERT(ToFloatRegister(ins->lhs()) == ToFloatRegister(ins->output()));
  const FloatRegister lhsDest = ToFloatRegister(ins->lhs());
  const FloatRegister src = ToFloatRegister(ins->rhs());
  const uint32_t laneIndex = ins->mir()->laneIndex();
  const uint32_t srcLaneIndex = ins->mir()->srcLaneIndex();

  // High ui4: dest lane selection. Low ui4: src lane selection.
  // <https://jia.je/unofficial-loongarch-intrinsics-guide/lsx/misc/#__m128i-__lsx_vextrins_d-__m128i-a-__m128i-b-imm0_255-imm>
  const uint32_t imm = (laneIndex << 4) | srcLaneIndex;

  switch (ins->mir()->simdOp()) {
    case wasm::SimdOp::I8x16ReplaceLane:
      masm.as_vextrins_b(lhsDest, src, imm);
      break;
    case wasm::SimdOp::I16x8ReplaceLane:
      masm.as_vextrins_h(lhsDest, src, imm);
      break;
    case wasm::SimdOp::I32x4ReplaceLane:
      masm.as_vextrins_w(lhsDest, src, imm);
      break;
    case wasm::SimdOp::I64x2ReplaceLane:
      masm.as_vextrins_d(lhsDest, src, imm);
      break;
    default:
      MOZ_CRASH("MoveLane SimdOp not implemented");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmReplaceInt64LaneSimd128(
    LWasmReplaceInt64LaneSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  MOZ_RELEASE_ASSERT(ins->mir()->simdOp() == wasm::SimdOp::I64x2ReplaceLane);
  MOZ_ASSERT(ToFloatRegister(ins->lhs()) == ToFloatRegister(ins->output()));
  const FloatRegister lhsDest = ToFloatRegister(ins->lhs());
  const uint32_t lane = ins->mir()->laneIndex();
  if (ins->rhs().value().isBogus()) {
    masm.replaceLaneInt64x2(lane, Register64(zero), lhsDest);
  } else {
    masm.replaceLaneInt64x2(lane, ToRegister64(ins->rhs()), lhsDest);
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmScalarToSimd128(LWasmScalarToSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  FloatRegister dest = ToFloatRegister(ins->output());

  switch (ins->mir()->simdOp()) {
    case wasm::SimdOp::I8x16Splat:
      masm.splatX16(ToRegister(ins->src()), dest);
      break;
    case wasm::SimdOp::I16x8Splat:
      masm.splatX8(ToRegister(ins->src()), dest);
      break;
    case wasm::SimdOp::I32x4Splat:
      masm.splatX4(ToRegister(ins->src()), dest);
      break;
    case wasm::SimdOp::F32x4Splat:
      masm.splatX4(ToFloatRegister(ins->src()), dest);
      break;
    case wasm::SimdOp::F64x2Splat:
      masm.splatX2(ToFloatRegister(ins->src()), dest);
      break;
    default:
      MOZ_CRASH("ScalarToSimd128 SimdOp not implemented");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmInt64ToSimd128(LWasmInt64ToSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  Register64 src = ToRegister64(ins->src());
  FloatRegister dest = ToFloatRegister(ins->output());

  switch (ins->mir()->simdOp()) {
    case wasm::SimdOp::I64x2Splat:
      masm.splatX2(src, dest);
      break;
    case wasm::SimdOp::V128Load8x8S:
      masm.moveGPR64ToDouble(src, dest);
      masm.widenLowInt8x16(dest, dest);
      break;
    case wasm::SimdOp::V128Load8x8U:
      masm.moveGPR64ToDouble(src, dest);
      masm.unsignedWidenLowInt8x16(dest, dest);
      break;
    case wasm::SimdOp::V128Load16x4S:
      masm.moveGPR64ToDouble(src, dest);
      masm.widenLowInt16x8(dest, dest);
      break;
    case wasm::SimdOp::V128Load16x4U:
      masm.moveGPR64ToDouble(src, dest);
      masm.unsignedWidenLowInt16x8(dest, dest);
      break;
    case wasm::SimdOp::V128Load32x2S:
      masm.moveGPR64ToDouble(src, dest);
      masm.widenLowInt32x4(dest, dest);
      break;
    case wasm::SimdOp::V128Load32x2U:
      masm.moveGPR64ToDouble(src, dest);
      masm.unsignedWidenLowInt32x4(dest, dest);
      break;
    default:
      MOZ_CRASH("Int64ToSimd128 SimdOp not implemented");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmUnarySimd128(LWasmUnarySimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  FloatRegister src = ToFloatRegister(ins->src());
  FloatRegister dest = ToFloatRegister(ins->output());

  switch (ins->mir()->simdOp()) {
    case wasm::SimdOp::I8x16Neg:
      masm.negInt8x16(src, dest);
      break;
    case wasm::SimdOp::I16x8Neg:
      masm.negInt16x8(src, dest);
      break;
    case wasm::SimdOp::I16x8ExtendLowI8x16S:
      masm.widenLowInt8x16(src, dest);
      break;
    case wasm::SimdOp::I16x8ExtendHighI8x16S:
      masm.widenHighInt8x16(src, dest);
      break;
    case wasm::SimdOp::I16x8ExtendLowI8x16U:
      masm.unsignedWidenLowInt8x16(src, dest);
      break;
    case wasm::SimdOp::I16x8ExtendHighI8x16U:
      masm.unsignedWidenHighInt8x16(src, dest);
      break;
    case wasm::SimdOp::I32x4Neg:
      masm.negInt32x4(src, dest);
      break;
    case wasm::SimdOp::I32x4ExtendLowI16x8S:
      masm.widenLowInt16x8(src, dest);
      break;
    case wasm::SimdOp::I32x4ExtendHighI16x8S:
      masm.widenHighInt16x8(src, dest);
      break;
    case wasm::SimdOp::I32x4ExtendLowI16x8U:
      masm.unsignedWidenLowInt16x8(src, dest);
      break;
    case wasm::SimdOp::I32x4ExtendHighI16x8U:
      masm.unsignedWidenHighInt16x8(src, dest);
      break;
    case wasm::SimdOp::I32x4TruncSatF32x4S:
      masm.truncSatFloat32x4ToInt32x4(src, dest, ToFloatRegister(ins->temp0()));
      break;
    case wasm::SimdOp::I32x4TruncSatF32x4U:
      masm.unsignedTruncSatFloat32x4ToInt32x4(src, dest);
      break;
    case wasm::SimdOp::I64x2Neg:
      masm.negInt64x2(src, dest);
      break;
    case wasm::SimdOp::I64x2ExtendLowI32x4S:
      masm.widenLowInt32x4(src, dest);
      break;
    case wasm::SimdOp::I64x2ExtendHighI32x4S:
      masm.widenHighInt32x4(src, dest);
      break;
    case wasm::SimdOp::I64x2ExtendLowI32x4U:
      masm.unsignedWidenLowInt32x4(src, dest);
      break;
    case wasm::SimdOp::I64x2ExtendHighI32x4U:
      masm.unsignedWidenHighInt32x4(src, dest);
      break;
    case wasm::SimdOp::F32x4Abs:
      masm.absFloat32x4(src, dest);
      break;
    case wasm::SimdOp::F32x4Neg:
      masm.negFloat32x4(src, dest);
      break;
    case wasm::SimdOp::F32x4Sqrt:
      masm.sqrtFloat32x4(src, dest);
      break;
    case wasm::SimdOp::F32x4ConvertI32x4S:
      masm.convertInt32x4ToFloat32x4(src, dest);
      break;
    case wasm::SimdOp::F32x4ConvertI32x4U:
      masm.unsignedConvertInt32x4ToFloat32x4(src, dest);
      break;
    case wasm::SimdOp::F64x2Abs:
      masm.absFloat64x2(src, dest);
      break;
    case wasm::SimdOp::F64x2Neg:
      masm.negFloat64x2(src, dest);
      break;
    case wasm::SimdOp::F64x2Sqrt:
      masm.sqrtFloat64x2(src, dest);
      break;
    case wasm::SimdOp::V128Not:
      masm.bitwiseNotSimd128(src, dest);
      break;
    case wasm::SimdOp::I8x16Abs:
      masm.absInt8x16(src, dest);
      break;
    case wasm::SimdOp::I16x8Abs:
      masm.absInt16x8(src, dest);
      break;
    case wasm::SimdOp::I32x4Abs:
      masm.absInt32x4(src, dest);
      break;
    case wasm::SimdOp::I64x2Abs:
      masm.absInt64x2(src, dest);
      break;
    case wasm::SimdOp::F32x4Ceil:
      masm.ceilFloat32x4(src, dest);
      break;
    case wasm::SimdOp::F32x4Floor:
      masm.floorFloat32x4(src, dest);
      break;
    case wasm::SimdOp::F32x4Trunc:
      masm.truncFloat32x4(src, dest);
      break;
    case wasm::SimdOp::F32x4Nearest:
      masm.nearestFloat32x4(src, dest);
      break;
    case wasm::SimdOp::F64x2Ceil:
      masm.ceilFloat64x2(src, dest);
      break;
    case wasm::SimdOp::F64x2Floor:
      masm.floorFloat64x2(src, dest);
      break;
    case wasm::SimdOp::F64x2Trunc:
      masm.truncFloat64x2(src, dest);
      break;
    case wasm::SimdOp::F64x2Nearest:
      masm.nearestFloat64x2(src, dest);
      break;
    case wasm::SimdOp::F32x4DemoteF64x2Zero:
      masm.convertFloat64x2ToFloat32x4(src, dest);
      break;
    case wasm::SimdOp::F64x2PromoteLowF32x4:
      masm.convertFloat32x4ToFloat64x2(src, dest);
      break;
    case wasm::SimdOp::F64x2ConvertLowI32x4S:
      masm.convertInt32x4ToFloat64x2(src, dest);
      break;
    case wasm::SimdOp::F64x2ConvertLowI32x4U:
      masm.unsignedConvertInt32x4ToFloat64x2(src, dest);
      break;
    case wasm::SimdOp::I32x4TruncSatF64x2SZero:
      masm.truncSatFloat64x2ToInt32x4(src, dest, ToFloatRegister(ins->temp0()));
      break;
    case wasm::SimdOp::I32x4TruncSatF64x2UZero:
      masm.unsignedTruncSatFloat64x2ToInt32x4(src, dest,
                                              ToFloatRegister(ins->temp0()));
      break;
    case wasm::SimdOp::I16x8ExtaddPairwiseI8x16S:
      masm.extAddPairwiseInt8x16(src, dest);
      break;
    case wasm::SimdOp::I16x8ExtaddPairwiseI8x16U:
      masm.unsignedExtAddPairwiseInt8x16(src, dest);
      break;
    case wasm::SimdOp::I32x4ExtaddPairwiseI16x8S:
      masm.extAddPairwiseInt16x8(src, dest);
      break;
    case wasm::SimdOp::I32x4ExtaddPairwiseI16x8U:
      masm.unsignedExtAddPairwiseInt16x8(src, dest);
      break;
    case wasm::SimdOp::I8x16Popcnt:
      masm.popcntInt8x16(src, dest);
      break;
    case wasm::SimdOp::I32x4RelaxedTruncF32x4S:
      masm.truncFloat32x4ToInt32x4Relaxed(src, dest);
      break;
    case wasm::SimdOp::I32x4RelaxedTruncF32x4U:
      masm.unsignedTruncFloat32x4ToInt32x4Relaxed(src, dest);
      break;
    case wasm::SimdOp::I32x4RelaxedTruncF64x2SZero:
      masm.truncFloat64x2ToInt32x4Relaxed(src, dest);
      break;
    case wasm::SimdOp::I32x4RelaxedTruncF64x2UZero:
      masm.unsignedTruncFloat64x2ToInt32x4Relaxed(src, dest);
      break;
    default:
      MOZ_CRASH("Unary SimdOp not implemented");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmReduceSimd128(LWasmReduceSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  FloatRegister src = ToFloatRegister(ins->src());
  const LDefinition* dest = ins->output();
  const uint32_t imm = ins->mir()->imm();

  switch (ins->mir()->simdOp()) {
    case wasm::SimdOp::V128AnyTrue:
      masm.anyTrueSimd128(src, ToRegister(dest));
      break;
    case wasm::SimdOp::I8x16AllTrue:
      masm.allTrueInt8x16(src, ToRegister(dest));
      break;
    case wasm::SimdOp::I16x8AllTrue:
      masm.allTrueInt16x8(src, ToRegister(dest));
      break;
    case wasm::SimdOp::I32x4AllTrue:
      masm.allTrueInt32x4(src, ToRegister(dest));
      break;
    case wasm::SimdOp::I64x2AllTrue:
      masm.allTrueInt64x2(src, ToRegister(dest));
      break;
    case wasm::SimdOp::I8x16Bitmask:
      masm.bitmaskInt8x16(src, ToRegister(dest));
      break;
    case wasm::SimdOp::I16x8Bitmask:
      masm.bitmaskInt16x8(src, ToRegister(dest));
      break;
    case wasm::SimdOp::I32x4Bitmask:
      masm.bitmaskInt32x4(src, ToRegister(dest));
      break;
    case wasm::SimdOp::I64x2Bitmask:
      masm.bitmaskInt64x2(src, ToRegister(dest));
      break;
    case wasm::SimdOp::I8x16ExtractLaneS:
      masm.extractLaneInt8x16(imm, src, ToRegister(dest));
      break;
    case wasm::SimdOp::I8x16ExtractLaneU:
      masm.unsignedExtractLaneInt8x16(imm, src, ToRegister(dest));
      break;
    case wasm::SimdOp::I16x8ExtractLaneS:
      masm.extractLaneInt16x8(imm, src, ToRegister(dest));
      break;
    case wasm::SimdOp::I16x8ExtractLaneU:
      masm.unsignedExtractLaneInt16x8(imm, src, ToRegister(dest));
      break;
    case wasm::SimdOp::I32x4ExtractLane:
      masm.extractLaneInt32x4(imm, src, ToRegister(dest));
      break;
    case wasm::SimdOp::F32x4ExtractLane:
      masm.extractLaneFloat32x4(imm, src, ToFloatRegister(dest));
      break;
    case wasm::SimdOp::F64x2ExtractLane:
      masm.extractLaneFloat64x2(imm, src, ToFloatRegister(dest));
      break;
    default:
      MOZ_CRASH("Reduce SimdOp not implemented");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmReduceAndBranchSimd128(
    LWasmReduceAndBranchSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  FloatRegister src = ToFloatRegister(ins->src());

  switch (ins->simdOp()) {
    case wasm::SimdOp::V128AnyTrue:
      masm.as_vsetnez_v(Assembler::FCC0, src);
      break;
    case wasm::SimdOp::I8x16AllTrue:
      masm.as_vsetallnez_b(Assembler::FCC0, src);
      break;
    case wasm::SimdOp::I16x8AllTrue:
      masm.as_vsetallnez_h(Assembler::FCC0, src);
      break;
    case wasm::SimdOp::I32x4AllTrue:
      masm.as_vsetallnez_w(Assembler::FCC0, src);
      break;
    case wasm::SimdOp::I64x2AllTrue:
      masm.as_vsetallnez_d(Assembler::FCC0, src);
      break;
    case wasm::SimdOp::I16x8Bitmask: {
      UseScratchRegisterScope temps(&masm);
      const Register test = temps.Acquire();
      masm.bitmaskInt16x8(src, test);
      if (isNextBlock(ins->ifFalse()->lir())) {
        masm.branch32(Assembler::NotEqual, test, Imm32(0),
                      getJumpLabelForBranch(ins->ifTrue()));
      } else if (isNextBlock(ins->ifTrue()->lir())) {
        masm.branch32(Assembler::Equal, test, Imm32(0),
                      getJumpLabelForBranch(ins->ifFalse()));
      } else {
        masm.branch32(Assembler::Equal, test, Imm32(0),
                      getJumpLabelForBranch(ins->ifFalse()));
        jumpToBlock(ins->ifTrue());
      }
      return;
    }
    default:
      MOZ_CRASH("Reduce-and-branch SimdOp not implemented");
  }

  if (isNextBlock(ins->ifFalse()->lir())) {
    masm.branchWithCode(masm.getBranchCode(Assembler::FCC0, Assembler::NonZero),
                        getJumpLabelForBranch(ins->ifTrue()), LongJump);
  } else if (isNextBlock(ins->ifTrue()->lir())) {
    masm.branchWithCode(masm.getBranchCode(Assembler::FCC0, Assembler::Zero),
                        getJumpLabelForBranch(ins->ifFalse()), LongJump);
  } else {
    masm.branchWithCode(masm.getBranchCode(Assembler::FCC0, Assembler::NonZero),
                        getJumpLabelForBranch(ins->ifTrue()), LongJump);
    jumpToBlock(ins->ifFalse());
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}
void CodeGenerator::visitWasmReduceSimd128ToInt64(
    LWasmReduceSimd128ToInt64* ins) {
#ifdef ENABLE_JIT_SIMD
  FloatRegister src = ToFloatRegister(ins->src());
  Register64 dest = ToOutRegister64(ins);
  const uint32_t imm = ins->mir()->imm();

  switch (ins->mir()->simdOp()) {
    case wasm::SimdOp::I64x2ExtractLane:
      masm.extractLaneInt64x2(imm, src, dest);
      break;
    default:
      MOZ_CRASH("Reduce SimdOp not implemented");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

static inline wasm::MemoryAccessDesc DeriveMemoryAccessDesc(
    const wasm::MemoryAccessDesc& access, Scalar::Type type) {
  return wasm::MemoryAccessDesc(access.memoryIndex(), type, access.align(),
                                access.offset32(), access.trapDesc(),
                                access.isHugeMemory());
}

void CodeGenerator::visitWasmLoadLaneSimd128(LWasmLoadLaneSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  // Forward loading to wasmLoad, and use replaceLane after that.
  const MWasmLoadLaneSimd128* mir = ins->mir();
  Register memoryBase = ToRegister(ins->memoryBase());
  FloatRegister src = ToFloatRegister(ins->src());
  FloatRegister dest = ToFloatRegister(ins->output());

  UseScratchRegisterScope temps(&masm);
  Register temp = temps.Acquire();
  Register scratch = temps.Acquire();
  Register ptr = ToRegister(ins->ptr());
  Register ptrScratch = InvalidReg;
  if (mir->access().offset32()) {
    ptrScratch = scratch;
  }
  if (mir->base()->type() == MIRType::Int32) {
    masm.move32To64ZeroExtend(ptr, Register64(scratch));
    ptr = scratch;
  } else if (ptrScratch != InvalidReg) {
    masm.movePtr(ptr, scratch);
  }

  // replaceLane takes an lhsDest argument.
  masm.moveSimd128(src, dest);
  switch (mir->laneSize()) {
    case 1:
      masm.wasmLoad(DeriveMemoryAccessDesc(mir->access(), Scalar::Int8),
                    memoryBase, ptr, ptrScratch, AnyRegister(temp));
      masm.replaceLaneInt8x16(mir->laneIndex(), temp, dest);
      break;
    case 2:
      masm.wasmLoad(DeriveMemoryAccessDesc(mir->access(), Scalar::Int16),
                    memoryBase, ptr, ptrScratch, AnyRegister(temp));
      masm.replaceLaneInt16x8(mir->laneIndex(), temp, dest);
      break;
    case 4:
      masm.wasmLoad(DeriveMemoryAccessDesc(mir->access(), Scalar::Int32),
                    memoryBase, ptr, ptrScratch, AnyRegister(temp));
      masm.replaceLaneInt32x4(mir->laneIndex(), temp, dest);
      break;
    case 8:
      masm.wasmLoadI64(DeriveMemoryAccessDesc(mir->access(), Scalar::Int64),
                       memoryBase, ptr, ptrScratch, Register64(temp));
      masm.replaceLaneInt64x2(mir->laneIndex(), Register64(temp), dest);
      break;
    default:
      MOZ_CRASH("Unsupported load lane size");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmStoreLaneSimd128(LWasmStoreLaneSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  const MWasmStoreLaneSimd128* mir = ins->mir();
  Register memoryBase = ToRegister(ins->memoryBase());
  FloatRegister src = ToFloatRegister(ins->src());
  Register ptr = ToRegister(ins->ptr());

  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  if (mir->base()->type() == MIRType::Int32) {
    masm.move32To64ZeroExtend(ptr, Register64(scratch));
    ptr = scratch;
  }

  switch (mir->laneSize()) {
    case 1:
      masm.wasmStoreLane(DeriveMemoryAccessDesc(mir->access(), Scalar::Int8),
                         src, memoryBase, ptr, mir->laneSize(),
                         mir->laneIndex());
      break;
    case 2:
      masm.wasmStoreLane(DeriveMemoryAccessDesc(mir->access(), Scalar::Int16),
                         src, memoryBase, ptr, mir->laneSize(),
                         mir->laneIndex());
      break;
    case 4:
      masm.wasmStoreLane(DeriveMemoryAccessDesc(mir->access(), Scalar::Int32),
                         src, memoryBase, ptr, mir->laneSize(),
                         mir->laneIndex());
      break;
    case 8:
      masm.wasmStoreLane(DeriveMemoryAccessDesc(mir->access(), Scalar::Int64),
                         src, memoryBase, ptr, mir->laneSize(),
                         mir->laneIndex());
      break;
    default:
      MOZ_CRASH("Unsupported store lane size");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmMulI64WideHI64(LWasmMulI64WideHI64* ins) {
  Register lhs = ToRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());
  Register output = ToRegister(ins->output());
  // This holds because both operands are non-AtStart variants.
  MOZ_ASSERT(output != lhs && output != rhs);
  masm.wasmMulI64WideHI64(lhs, rhs, output, ins->isSigned());
}
