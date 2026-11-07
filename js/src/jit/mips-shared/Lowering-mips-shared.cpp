/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/mips-shared/Lowering-mips-shared.h"

#include "mozilla/MathAlgorithms.h"

#include "jit/Lowering.h"
#include "jit/MIR-wasm.h"
#include "jit/MIR.h"

#include "jit/shared/Lowering-shared-inl.h"

using namespace js;
using namespace js::jit;

using mozilla::FloorLog2;

LAllocation LIRGeneratorMIPSShared::useByteOpRegister(MDefinition* mir) {
  return useRegister(mir);
}

LAllocation LIRGeneratorMIPSShared::useByteOpRegisterAtStart(MDefinition* mir) {
  return useRegisterAtStart(mir);
}

LAllocation LIRGeneratorMIPSShared::useByteOpRegisterOrNonDoubleConstant(
    MDefinition* mir) {
  return useRegisterOrNonDoubleConstant(mir);
}

LDefinition LIRGeneratorMIPSShared::tempByteOpRegister() { return temp(); }

// x = !y
void LIRGeneratorMIPSShared::lowerForALU(LInstructionHelper<1, 1, 0>* ins,
                                         MDefinition* mir, MDefinition* input) {
  // Unary ALU operations don't read the input after writing to the output, even
  // for fallible operations, so we can use at-start allocations.
  ins->setOperand(0, useRegisterAtStart(input));
  define(ins, mir);
}

// z = x+y
void LIRGeneratorMIPSShared::lowerForALU(LInstructionHelper<1, 2, 0>* ins,
                                         MDefinition* mir, MDefinition* lhs,
                                         MDefinition* rhs) {
  // Binary ALU operations don't read any input after writing to the output,
  // even for fallible operations, so we can use at-start allocations.
  ins->setOperand(0, useRegisterAtStart(lhs));
  ins->setOperand(1, useRegisterOrConstantAtStart(rhs));
  define(ins, mir);
}

void LIRGeneratorMIPSShared::lowerForALUInt64(
    LInstructionHelper<INT64_PIECES, INT64_PIECES, 0>* ins, MDefinition* mir,
    MDefinition* input) {
  ins->setInt64Operand(0, useInt64RegisterAtStart(input));
  defineInt64(ins, mir);
}

void LIRGeneratorMIPSShared::lowerForALUInt64(
    LInstructionHelper<INT64_PIECES, 2 * INT64_PIECES, 0>* ins,
    MDefinition* mir, MDefinition* lhs, MDefinition* rhs) {
  ins->setInt64Operand(0, useInt64RegisterAtStart(lhs));
  ins->setInt64Operand(INT64_PIECES, useInt64RegisterOrConstantAtStart(rhs));
  defineInt64(ins, mir);
}

void LIRGeneratorMIPSShared::lowerForMulInt64(LMulI64* ins, MMul* mir,
                                              MDefinition* lhs,
                                              MDefinition* rhs) {
  lowerForALUInt64(ins, mir, lhs, rhs);
}

template <class LInstr>
void LIRGeneratorMIPSShared::lowerForShiftInt64(LInstr* ins, MDefinition* mir,
                                                MDefinition* lhs,
                                                MDefinition* rhs) {
  if constexpr (std::is_same_v<LInstr, LShiftI64>) {
    ins->setLhs(useInt64RegisterAtStart(lhs));
    ins->setRhs(useRegisterOrConstantAtStart(rhs));
  } else {
    ins->setInput(useInt64RegisterAtStart(lhs));
    ins->setCount(useRegisterOrConstantAtStart(rhs));
  }
  defineInt64(ins, mir);
}

template void LIRGeneratorMIPSShared::lowerForShiftInt64(LShiftI64* ins,
                                                         MDefinition* mir,
                                                         MDefinition* lhs,
                                                         MDefinition* rhs);
template void LIRGeneratorMIPSShared::lowerForShiftInt64(LRotateI64* ins,
                                                         MDefinition* mir,
                                                         MDefinition* lhs,
                                                         MDefinition* rhs);

void LIRGeneratorMIPSShared::lowerForFPU(LInstructionHelper<1, 1, 0>* ins,
                                         MDefinition* mir, MDefinition* input) {
  ins->setOperand(0, useRegisterAtStart(input));
  define(ins, mir);
}

void LIRGeneratorMIPSShared::lowerForFPU(LInstructionHelper<1, 2, 0>* ins,
                                         MDefinition* mir, MDefinition* lhs,
                                         MDefinition* rhs) {
  ins->setOperand(0, useRegisterAtStart(lhs));
  ins->setOperand(1, useRegisterAtStart(rhs));
  define(ins, mir);
}

void LIRGeneratorMIPSShared::lowerWasmBuiltinTruncateToInt32(
    MWasmBuiltinTruncateToInt32* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Double || opd->type() == MIRType::Float32);

  if (opd->type() == MIRType::Double) {
    define(new (alloc()) LWasmBuiltinTruncateDToInt32(
               useRegister(opd), useFixed(ins->instance(), InstanceReg),
               LDefinition::BogusTemp()),
           ins);
    return;
  }

  define(new (alloc()) LWasmBuiltinTruncateFToInt32(
             useRegister(opd), useFixed(ins->instance(), InstanceReg),
             LDefinition::BogusTemp()),
         ins);
}

void LIRGeneratorMIPSShared::lowerForShift(LInstructionHelper<1, 2, 0>* ins,
                                           MDefinition* mir, MDefinition* lhs,
                                           MDefinition* rhs) {
  lowerForALU(ins, mir, lhs, rhs);
}

void LIRGeneratorMIPSShared::lowerDivI(MDiv* div) {
  // Division instructions are slow. Division by constant denominators can be
  // rewritten to use other instructions.
  if (div->rhs()->isConstant()) {
    int32_t rhs = div->rhs()->toConstant()->toInt32();
    // Check for division by a positive power of two, which is an easy and
    // important case to optimize. Note that other optimizations are also
    // possible; division by negative powers of two can be optimized in a
    // similar manner as positive powers of two, and division by other
    // constants can be optimized by a reciprocal multiplication technique.
    int32_t shift = FloorLog2(uint32_t(rhs));
    if (rhs > 0 && 1 << shift == rhs) {
      LDivPowTwoI* lir =
          new (alloc()) LDivPowTwoI(useRegister(div->lhs()), temp(), shift);
      if (div->fallible()) {
        assignSnapshot(lir, div->bailoutKind());
      }
      define(lir, div);
      return;
    }
  }

  LDivI* lir = new (alloc())
      LDivI(useRegister(div->lhs()), useRegister(div->rhs()), temp());
  if (div->fallible()) {
    assignSnapshot(lir, div->bailoutKind());
  }
  define(lir, div);
}

void LIRGeneratorMIPSShared::lowerMulI(MMul* mul, MDefinition* lhs,
                                       MDefinition* rhs) {
  LMulI* lir = new (alloc()) LMulI;
  if (mul->fallible()) {
    assignSnapshot(lir, mul->bailoutKind());
  }

  // Negative zero check reads |lhs| and |rhs| after writing to the output, so
  // we can't use at-start allocations.
  if (mul->canBeNegativeZero() && !rhs->isConstant()) {
    lir->setOperand(0, useRegister(lhs));
    lir->setOperand(1, useRegister(rhs));
    define(lir, mul);
    return;
  }

  lowerForALU(lir, mul, lhs, rhs);
}

void LIRGeneratorMIPSShared::lowerModI(MMod* mod) {
  if (mod->rhs()->isConstant()) {
    int32_t rhs = mod->rhs()->toConstant()->toInt32();
    int32_t shift = FloorLog2(uint32_t(rhs));
    if (rhs > 0 && 1 << shift == rhs) {
      LModPowTwoI* lir =
          new (alloc()) LModPowTwoI(useRegister(mod->lhs()), shift);
      if (mod->fallible()) {
        assignSnapshot(lir, mod->bailoutKind());
      }
      define(lir, mod);
      return;
    } else if (shift < 31 && (1 << (shift + 1)) - 1 == rhs) {
      LModMaskI* lir = new (alloc())
          LModMaskI(useRegister(mod->lhs()), temp(), temp(), shift + 1);
      if (mod->fallible()) {
        assignSnapshot(lir, mod->bailoutKind());
      }
      define(lir, mod);
      return;
    }
  }
  auto* lir =
      new (alloc()) LModI(useRegister(mod->lhs()), useRegister(mod->rhs()));

  if (mod->fallible()) {
    assignSnapshot(lir, mod->bailoutKind());
  }
  define(lir, mod);
}

void LIRGeneratorMIPSShared::lowerWasmSelectI(MWasmSelect* select) {
  auto* lir = new (alloc())
      LWasmSelect(useRegisterAtStart(select->trueExpr()),
                  useAny(select->falseExpr()), useRegister(select->condExpr()));
  defineReuseInput(lir, select, LWasmSelect::TrueExprIndex);
}

void LIRGeneratorMIPSShared::lowerWasmSelectI64(MWasmSelect* select) {
  auto* lir = new (alloc()) LWasmSelectI64(
      useInt64RegisterAtStart(select->trueExpr()),
      useInt64(select->falseExpr()), useRegister(select->condExpr()));
  defineInt64ReuseInput(lir, select, LWasmSelectI64::TrueExprIndex);
}

LTableSwitch* LIRGeneratorMIPSShared::newLTableSwitch(
    const LAllocation& in, const LDefinition& inputCopy) {
  return new (alloc()) LTableSwitch(in, inputCopy, temp());
}

LTableSwitchV* LIRGeneratorMIPSShared::newLTableSwitchV(
    const LBoxAllocation& in) {
  return new (alloc()) LTableSwitchV(in, temp(), tempDouble(), temp());
}

void LIRGeneratorMIPSShared::lowerUrshD(MUrsh* mir) {
  MDefinition* lhs = mir->lhs();
  MDefinition* rhs = mir->rhs();

  MOZ_ASSERT(lhs->type() == MIRType::Int32);
  MOZ_ASSERT(rhs->type() == MIRType::Int32);

  auto* lir = new (alloc()) LUrshD(useRegisterAtStart(lhs),
                                   useRegisterOrConstantAtStart(rhs), temp());
  define(lir, mir);
}

void LIRGeneratorMIPSShared::lowerPowOfTwoI(MPow* mir) {
  int32_t base = mir->input()->toConstant()->toInt32();
  MDefinition* power = mir->power();

  auto* lir = new (alloc()) LPowOfTwoI(useRegister(power), base);
  assignSnapshot(lir, mir->bailoutKind());
  define(lir, mir);
}

void LIRGeneratorMIPSShared::lowerBigIntPtrLsh(MBigIntPtrLsh* ins) {
  auto* lir = new (alloc()) LBigIntPtrLsh(
      useRegister(ins->lhs()), useRegister(ins->rhs()), temp(), temp());
  assignSnapshot(lir, ins->bailoutKind());
  define(lir, ins);
}

void LIRGeneratorMIPSShared::lowerBigIntPtrRsh(MBigIntPtrRsh* ins) {
  auto* lir = new (alloc()) LBigIntPtrRsh(
      useRegister(ins->lhs()), useRegister(ins->rhs()), temp(), temp());
  assignSnapshot(lir, ins->bailoutKind());
  define(lir, ins);
}

void LIRGeneratorMIPSShared::lowerBigIntPtrDiv(MBigIntPtrDiv* ins) {
  auto* lir = new (alloc())
      LBigIntPtrDiv(useRegister(ins->lhs()), useRegister(ins->rhs()),
                    LDefinition::BogusTemp(), LDefinition::BogusTemp());
  assignSnapshot(lir, ins->bailoutKind());
  define(lir, ins);
}

void LIRGeneratorMIPSShared::lowerBigIntPtrMod(MBigIntPtrMod* ins) {
  auto* lir = new (alloc())
      LBigIntPtrMod(useRegister(ins->lhs()), useRegister(ins->rhs()), temp(),
                    LDefinition::BogusTemp());
  if (ins->canBeDivideByZero()) {
    assignSnapshot(lir, ins->bailoutKind());
  }
  define(lir, ins);
}

void LIRGenerator::visitWasmLoad(MWasmLoad* ins) {
  MDefinition* base = ins->base();
  // 'base' is a GPR but may be of either type. If it is 32-bit, it is
  // sign-extended on mips64 platform and we should explicitly promote it to
  // 64-bit by zero-extension when use it as an index register in memory
  // accesses.
  MOZ_ASSERT(base->type() == MIRType::Int32 || base->type() == MIRType::Int64);

  LAllocation memoryBase =
      ins->hasMemoryBase() ? LAllocation(useRegisterAtStart(ins->memoryBase()))
                           : LGeneralReg(HeapReg);

  LAllocation ptr = useRegisterAtStart(base);

  LDefinition ptrCopy = LDefinition::BogusTemp();
  if (ins->access().offset32()) {
    ptrCopy = tempCopy(base, 0);
  }

  if (ins->access().type() == Scalar::Int64) {
    if (IsUnaligned(ins->access())) {
      auto* lir =
          new (alloc()) LWasmUnalignedLoadI64(ptr, memoryBase, ptrCopy, temp());
      defineInt64(lir, ins);
      return;
    }

    auto* lir = new (alloc()) LWasmLoadI64(ptr, memoryBase, ptrCopy);
    defineInt64(lir, ins);
    return;
  }

  if (IsUnaligned(ins->access())) {
    auto* lir =
        new (alloc()) LWasmUnalignedLoad(ptr, memoryBase, ptrCopy, temp());
    define(lir, ins);
    return;
  }

  auto* lir = new (alloc()) LWasmLoad(ptr, memoryBase, ptrCopy);
  define(lir, ins);
}

void LIRGenerator::visitWasmStore(MWasmStore* ins) {
  MDefinition* base = ins->base();
  // See comment in visitWasmLoad re the type of 'base'.
  MOZ_ASSERT(base->type() == MIRType::Int32 || base->type() == MIRType::Int64);

  MDefinition* value = ins->value();
  LAllocation memoryBase =
      ins->hasMemoryBase() ? LAllocation(useRegisterAtStart(ins->memoryBase()))
                           : LGeneralReg(HeapReg);

  LAllocation baseAlloc = useRegisterAtStart(base);

  LDefinition ptrCopy = LDefinition::BogusTemp();
  if (ins->access().offset32()) {
    ptrCopy = tempCopy(base, 0);
  }

  if (ins->access().type() == Scalar::Int64) {
    LInt64Allocation valueAlloc = useInt64RegisterAtStart(value);

    if (IsUnaligned(ins->access())) {
      auto* lir = new (alloc()) LWasmUnalignedStoreI64(
          baseAlloc, valueAlloc, memoryBase, ptrCopy, temp());
      add(lir, ins);
      return;
    }

    auto* lir =
        new (alloc()) LWasmStoreI64(baseAlloc, valueAlloc, memoryBase, ptrCopy);
    add(lir, ins);
    return;
  }

  LAllocation valueAlloc = useRegisterAtStart(value);

  if (IsUnaligned(ins->access())) {
    auto* lir = new (alloc())
        LWasmUnalignedStore(baseAlloc, valueAlloc, memoryBase, ptrCopy, temp());
    add(lir, ins);
    return;
  }

  auto* lir =
      new (alloc()) LWasmStore(baseAlloc, valueAlloc, memoryBase, ptrCopy);
  add(lir, ins);
}

void LIRGeneratorMIPSShared::lowerUDiv(MDiv* div) {
  MDefinition* lhs = div->getOperand(0);
  MDefinition* rhs = div->getOperand(1);

  LUDivOrMod* lir = new (alloc()) LUDivOrMod;
  lir->setOperand(0, useRegister(lhs));
  lir->setOperand(1, useRegister(rhs));
  if (div->fallible()) {
    assignSnapshot(lir, div->bailoutKind());
  }

  define(lir, div);
}

void LIRGeneratorMIPSShared::lowerUMod(MMod* mod) {
  MDefinition* lhs = mod->getOperand(0);
  MDefinition* rhs = mod->getOperand(1);

  LUDivOrMod* lir = new (alloc()) LUDivOrMod;
  lir->setOperand(0, useRegister(lhs));
  lir->setOperand(1, useRegister(rhs));
  if (mod->fallible()) {
    assignSnapshot(lir, mod->bailoutKind());
  }

  define(lir, mod);
}

void LIRGenerator::visitSubstr(MSubstr* ins) {
  LSubstr* lir = new (alloc())
      LSubstr(useRegister(ins->string()), useRegister(ins->begin()),
              useRegister(ins->length()), temp(), temp(), tempByteOpRegister());
  define(lir, ins);
  assignSafepoint(lir, ins);
}

void LIRGenerator::visitCompareExchangeTypedArrayElement(
    MCompareExchangeTypedArrayElement* ins) {
  MOZ_ASSERT(!Scalar::isFloatingType(ins->arrayType()));
  MOZ_ASSERT(ins->elements()->type() == MIRType::Elements);
  MOZ_ASSERT(ins->index()->type() == MIRType::IntPtr);

  const LUse elements = useRegister(ins->elements());
  const LAllocation index =
      useRegisterOrIndexConstant(ins->index(), ins->arrayType());

  if (Scalar::isBigIntType(ins->arrayType())) {
    LInt64Allocation oldval = useInt64Register(ins->oldval());
    LInt64Allocation newval = useInt64Register(ins->newval());

    auto* lir = new (alloc())
        LCompareExchangeTypedArrayElement64(elements, index, oldval, newval);
    defineInt64(lir, ins);
    return;
  }

  const LAllocation oldval = useRegister(ins->oldval());
  const LAllocation newval = useRegister(ins->newval());

  LDefinition valueTemp = LDefinition::BogusTemp();
  LDefinition offsetTemp = LDefinition::BogusTemp();
  LDefinition maskTemp = LDefinition::BogusTemp();

  if (Scalar::byteSize(ins->arrayType()) < 4) {
    valueTemp = temp();
    offsetTemp = temp();
    maskTemp = temp();
  }

  auto* lir = new (alloc()) LCompareExchangeTypedArrayElement(
      elements, index, oldval, newval, valueTemp, offsetTemp, maskTemp);
  define(lir, ins);
}

void LIRGenerator::visitAtomicExchangeTypedArrayElement(
    MAtomicExchangeTypedArrayElement* ins) {
  MOZ_ASSERT(ins->elements()->type() == MIRType::Elements);
  MOZ_ASSERT(ins->index()->type() == MIRType::IntPtr);

  const LUse elements = useRegister(ins->elements());
  const LAllocation index =
      useRegisterOrIndexConstant(ins->index(), ins->arrayType());

  if (Scalar::isBigIntType(ins->arrayType())) {
    LInt64Allocation value = useInt64Register(ins->value());

    auto* lir = new (alloc())
        LAtomicExchangeTypedArrayElement64(elements, index, value);
    defineInt64(lir, ins);
    return;
  }

  MOZ_ASSERT(ins->arrayType() <= Scalar::Uint32);

  const LAllocation value = useRegister(ins->value());

  LDefinition valueTemp = LDefinition::BogusTemp();
  LDefinition offsetTemp = LDefinition::BogusTemp();
  LDefinition maskTemp = LDefinition::BogusTemp();

  if (Scalar::byteSize(ins->arrayType()) < 4) {
    valueTemp = temp();
    offsetTemp = temp();
    maskTemp = temp();
  }

  auto* lir = new (alloc()) LAtomicExchangeTypedArrayElement(
      elements, index, value, valueTemp, offsetTemp, maskTemp);
  define(lir, ins);
}

void LIRGenerator::visitWasmCompareExchangeHeap(MWasmCompareExchangeHeap* ins) {
  MDefinition* base = ins->base();
  // See comment in visitWasmLoad re the type of 'base'.
  MOZ_ASSERT(base->type() == MIRType::Int32 || base->type() == MIRType::Int64);
  LAllocation memoryBase = ins->hasMemoryBase()
                               ? LAllocation(useRegister(ins->memoryBase()))
                               : LGeneralReg(HeapReg);

  if (ins->access().type() == Scalar::Int64) {
    auto* lir = new (alloc()) LWasmCompareExchangeI64(
        useRegister(base), useInt64Register(ins->oldValue()),
        useInt64Register(ins->newValue()), memoryBase);
    defineInt64(lir, ins);
    return;
  }

  LDefinition valueTemp = LDefinition::BogusTemp();
  LDefinition offsetTemp = LDefinition::BogusTemp();
  LDefinition maskTemp = LDefinition::BogusTemp();

  if (ins->access().byteSize() < 4) {
    valueTemp = temp();
    offsetTemp = temp();
    maskTemp = temp();
  }

  auto* lir = new (alloc())
      LWasmCompareExchangeHeap(useRegister(base), useRegister(ins->oldValue()),
                               useRegister(ins->newValue()), memoryBase,
                               valueTemp, offsetTemp, maskTemp);

  define(lir, ins);
}

void LIRGenerator::visitWasmAtomicExchangeHeap(MWasmAtomicExchangeHeap* ins) {
  MDefinition* base = ins->base();
  // See comment in visitWasmLoad re the type of 'base'.
  MOZ_ASSERT(base->type() == MIRType::Int32 || base->type() == MIRType::Int64);
  LAllocation memoryBase = ins->hasMemoryBase()
                               ? LAllocation(useRegister(ins->memoryBase()))
                               : LGeneralReg(HeapReg);

  if (ins->access().type() == Scalar::Int64) {
    auto* lir = new (alloc()) LWasmAtomicExchangeI64(
        useRegister(base), useInt64Register(ins->value()), memoryBase);
    defineInt64(lir, ins);
    return;
  }

  LDefinition valueTemp = LDefinition::BogusTemp();
  LDefinition offsetTemp = LDefinition::BogusTemp();
  LDefinition maskTemp = LDefinition::BogusTemp();

  if (ins->access().byteSize() < 4) {
    valueTemp = temp();
    offsetTemp = temp();
    maskTemp = temp();
  }

  auto* lir = new (alloc())
      LWasmAtomicExchangeHeap(useRegister(base), useRegister(ins->value()),
                              memoryBase, valueTemp, offsetTemp, maskTemp);
  define(lir, ins);
}

void LIRGenerator::visitWasmAtomicBinopHeap(MWasmAtomicBinopHeap* ins) {
  MDefinition* base = ins->base();
  // See comment in visitWasmLoad re the type of 'base'.
  MOZ_ASSERT(base->type() == MIRType::Int32 || base->type() == MIRType::Int64);
  LAllocation memoryBase = ins->hasMemoryBase()
                               ? LAllocation(useRegister(ins->memoryBase()))
                               : LGeneralReg(HeapReg);

  if (ins->access().type() == Scalar::Int64) {
    auto* lir = new (alloc())
        LWasmAtomicBinopI64(useRegister(base), useInt64Register(ins->value()),
                            memoryBase, tempInt64());
    defineInt64(lir, ins);
    return;
  }

  LDefinition valueTemp = LDefinition::BogusTemp();
  LDefinition offsetTemp = LDefinition::BogusTemp();
  LDefinition maskTemp = LDefinition::BogusTemp();

  if (ins->access().byteSize() < 4) {
    valueTemp = temp();
    offsetTemp = temp();
    maskTemp = temp();
  }

  if (!ins->hasUses()) {
    auto* lir = new (alloc()) LWasmAtomicBinopHeapForEffect(
        useRegister(base), useRegister(ins->value()), memoryBase, valueTemp,
        offsetTemp, maskTemp);
    add(lir, ins);
    return;
  }

  auto* lir = new (alloc())
      LWasmAtomicBinopHeap(useRegister(base), useRegister(ins->value()),
                           memoryBase, valueTemp, offsetTemp, maskTemp);

  define(lir, ins);
}

void LIRGenerator::visitAtomicTypedArrayElementBinop(
    MAtomicTypedArrayElementBinop* ins) {
  MOZ_ASSERT(ins->arrayType() != Scalar::Uint8Clamped);
  MOZ_ASSERT(!Scalar::isFloatingType(ins->arrayType()));
  MOZ_ASSERT(ins->elements()->type() == MIRType::Elements);
  MOZ_ASSERT(ins->index()->type() == MIRType::IntPtr);

  const LUse elements = useRegister(ins->elements());
  const LAllocation index =
      useRegisterOrIndexConstant(ins->index(), ins->arrayType());

  if (Scalar::isBigIntType(ins->arrayType())) {
    LInt64Allocation value = useInt64Register(ins->value());
    LInt64Definition temp = tempInt64();

    // Case 1: the result of the operation is not used.

    if (ins->isForEffect()) {
      auto* lir = new (alloc()) LAtomicTypedArrayElementBinopForEffect64(
          elements, index, value, temp);
      add(lir, ins);
      return;
    }

    // Case 2: the result of the operation is used.

    auto* lir = new (alloc())
        LAtomicTypedArrayElementBinop64(elements, index, value, temp);
    defineInt64(lir, ins);
    return;
  }

  LAllocation value = useRegister(ins->value());
  LDefinition valueTemp = LDefinition::BogusTemp();
  LDefinition offsetTemp = LDefinition::BogusTemp();
  LDefinition maskTemp = LDefinition::BogusTemp();

  if (Scalar::byteSize(ins->arrayType()) < 4) {
    valueTemp = temp();
    offsetTemp = temp();
    maskTemp = temp();
  }

  if (ins->isForEffect()) {
    auto* lir = new (alloc()) LAtomicTypedArrayElementBinopForEffect(
        elements, index, value, valueTemp, offsetTemp, maskTemp);
    add(lir, ins);
    return;
  }

  auto* lir = new (alloc()) LAtomicTypedArrayElementBinop(
      elements, index, value, valueTemp, offsetTemp, maskTemp);
  define(lir, ins);
}

void LIRGenerator::visitCopySign(MCopySign* ins) {
  MDefinition* lhs = ins->lhs();
  MDefinition* rhs = ins->rhs();

  MOZ_ASSERT(IsFloatingPointType(lhs->type()));
  MOZ_ASSERT(lhs->type() == rhs->type());
  MOZ_ASSERT(lhs->type() == ins->type());

  LInstructionHelper<1, 2, 0>* lir;
  if (lhs->type() == MIRType::Double) {
    lir = new (alloc()) LCopySignD();
  } else {
    lir = new (alloc()) LCopySignF();
  }

  lowerForFPU(lir, ins, lhs, rhs);
}

void LIRGenerator::visitExtendInt32ToInt64(MExtendInt32ToInt64* ins) {
  defineInt64(
      new (alloc()) LExtendInt32ToInt64(useRegisterAtStart(ins->input())), ins);
}

void LIRGenerator::visitSignExtendInt64(MSignExtendInt64* ins) {
  defineInt64(new (alloc())
                  LSignExtendInt64(useInt64RegisterAtStart(ins->input())),
              ins);
}

// On mips we specialize the only cases where compare is {U,}Int32 and select
// is {U,}Int32.
bool LIRGeneratorShared::canSpecializeWasmCompareAndSelect(
    MCompare::CompareType compTy, MIRType insTy) {
  return insTy == MIRType::Int32 && (compTy == MCompare::Compare_Int32 ||
                                     compTy == MCompare::Compare_UInt32);
}

void LIRGeneratorShared::lowerWasmCompareAndSelect(MWasmSelect* ins,
                                                   MDefinition* lhs,
                                                   MDefinition* rhs,
                                                   MCompare::CompareType compTy,
                                                   JSOp jsop) {
  MOZ_ASSERT(canSpecializeWasmCompareAndSelect(compTy, ins->type()));
  auto* lir = new (alloc()) LWasmCompareAndSelect(
      useRegister(lhs), useRegister(rhs), useRegisterAtStart(ins->trueExpr()),
      useRegister(ins->falseExpr()), compTy, jsop);
  defineReuseInput(lir, ins, LWasmCompareAndSelect::IfTrueExprIndex);
}

void LIRGenerator::visitWasmTernarySimd128(MWasmTernarySimd128* ins) {
  MOZ_CRASH("ternary SIMD NYI");
}

void LIRGenerator::visitWasmBinarySimd128(MWasmBinarySimd128* ins) {
  MOZ_CRASH("binary SIMD NYI");
}

#ifdef ENABLE_JIT_SIMD
bool MWasmTernarySimd128::specializeBitselectConstantMaskAsShuffle(
    int8_t shuffle[16]) {
  return false;
}
bool MWasmTernarySimd128::canRelaxBitselect() { return false; }

bool MWasmBinarySimd128::canPmaddubsw() { return false; }

bool MWasmBinarySimd128::canTestBits() { return false; }

bool MWasmReplaceLaneSimd128::canMoveLane() { return false; }
#endif

bool MWasmBinarySimd128::specializeForConstantRhs() {
  // Probably many we want to do here
  return false;
}

void LIRGenerator::visitWasmBinarySimd128WithConstant(
    MWasmBinarySimd128WithConstant* ins) {
  MOZ_CRASH("binary SIMD with constant NYI");
}

void LIRGenerator::visitWasmShiftSimd128(MWasmShiftSimd128* ins) {
  MOZ_CRASH("shift SIMD NYI");
}

void LIRGenerator::visitWasmShuffleSimd128(MWasmShuffleSimd128* ins) {
  MOZ_CRASH("shuffle SIMD NYI");
}

void LIRGenerator::visitWasmReplaceLaneSimd128(MWasmReplaceLaneSimd128* ins) {
  MOZ_CRASH("replace-lane SIMD NYI");
}

void LIRGenerator::visitWasmMoveLaneSimd128(MWasmMoveLaneSimd128* ins) {
  MOZ_CRASH("move-lane SIMD NYI");
}

void LIRGenerator::visitWasmScalarToSimd128(MWasmScalarToSimd128* ins) {
  MOZ_CRASH("scalar-to-SIMD NYI");
}

void LIRGenerator::visitWasmUnarySimd128(MWasmUnarySimd128* ins) {
  MOZ_CRASH("unary SIMD NYI");
}

void LIRGenerator::visitWasmReduceSimd128(MWasmReduceSimd128* ins) {
  MOZ_CRASH("reduce-SIMD NYI");
}

void LIRGenerator::visitWasmLoadLaneSimd128(MWasmLoadLaneSimd128* ins) {
  MOZ_CRASH("load-lane SIMD NYI");
}

void LIRGenerator::visitWasmStoreLaneSimd128(MWasmStoreLaneSimd128* ins) {
  MOZ_CRASH("store-lane SIMD NYI");
}
