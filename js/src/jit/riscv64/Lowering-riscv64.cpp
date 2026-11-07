/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/riscv64/Lowering-riscv64.h"

#include "mozilla/MathAlgorithms.h"

#include <bit>

#include "jit/Lowering.h"
#include "jit/MIR-wasm.h"
#include "jit/MIR.h"

#include "jit/shared/Lowering-shared-inl.h"

using namespace js;
using namespace js::jit;

LTableSwitch* LIRGeneratorRiscv64::newLTableSwitch(
    const LAllocation& in, const LDefinition& inputCopy) {
  return new (alloc()) LTableSwitch(in, inputCopy, temp());
}

LTableSwitchV* LIRGeneratorRiscv64::newLTableSwitchV(const LBoxAllocation& in) {
  return new (alloc()) LTableSwitchV(in, temp(), tempDouble(), temp());
}

void LIRGeneratorRiscv64::lowerForShift(LInstructionHelper<1, 2, 0>* ins,
                                        MDefinition* mir, MDefinition* lhs,
                                        MDefinition* rhs) {
  lowerForALU(ins, mir, lhs, rhs);
}

template <class LInstr>
void LIRGeneratorRiscv64::lowerForShiftInt64(LInstr* ins, MDefinition* mir,
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

template void LIRGeneratorRiscv64::lowerForShiftInt64(LShiftI64* ins,
                                                      MDefinition* mir,
                                                      MDefinition* lhs,
                                                      MDefinition* rhs);
template void LIRGeneratorRiscv64::lowerForShiftInt64(LRotateI64* ins,
                                                      MDefinition* mir,
                                                      MDefinition* lhs,
                                                      MDefinition* rhs);

// x = !y
void LIRGeneratorRiscv64::lowerForALU(LInstructionHelper<1, 1, 0>* ins,
                                      MDefinition* mir, MDefinition* input) {
  // Unary ALU operations don't read the input after writing to the output, even
  // for fallible operations, so we can use at-start allocations.
  ins->setOperand(0, useRegisterAtStart(input));
  define(ins, mir);
}

// z = x + y
void LIRGeneratorRiscv64::lowerForALU(LInstructionHelper<1, 2, 0>* ins,
                                      MDefinition* mir, MDefinition* lhs,
                                      MDefinition* rhs) {
  // Binary ALU operations don't read any input after writing to the output,
  // even for fallible operations, so we can use at-start allocations.
  ins->setOperand(0, useRegisterAtStart(lhs));
  ins->setOperand(1, useRegisterOrConstantAtStart(rhs));
  define(ins, mir);
}

void LIRGeneratorRiscv64::lowerForALUInt64(
    LInstructionHelper<INT64_PIECES, INT64_PIECES, 0>* ins, MDefinition* mir,
    MDefinition* input) {
  ins->setInt64Operand(0, useInt64RegisterAtStart(input));
  defineInt64(ins, mir);
}

void LIRGeneratorRiscv64::lowerForALUInt64(
    LInstructionHelper<INT64_PIECES, 2 * INT64_PIECES, 0>* ins,
    MDefinition* mir, MDefinition* lhs, MDefinition* rhs) {
  ins->setInt64Operand(0, useInt64RegisterAtStart(lhs));
  ins->setInt64Operand(INT64_PIECES, useInt64RegisterOrConstantAtStart(rhs));
  defineInt64(ins, mir);
}

void LIRGeneratorRiscv64::lowerForMulInt64(LMulI64* ins, MMul* mir,
                                           MDefinition* lhs, MDefinition* rhs) {
  lowerForALUInt64(ins, mir, lhs, rhs);
}

void LIRGeneratorRiscv64::lowerForFPU(LInstructionHelper<1, 1, 0>* ins,
                                      MDefinition* mir, MDefinition* input) {
  ins->setOperand(0, useRegisterAtStart(input));
  define(ins, mir);
}

void LIRGeneratorRiscv64::lowerForFPU(LInstructionHelper<1, 2, 0>* ins,
                                      MDefinition* mir, MDefinition* lhs,
                                      MDefinition* rhs) {
  ins->setOperand(0, useRegisterAtStart(lhs));
  ins->setOperand(1, useRegisterAtStart(rhs));
  define(ins, mir);
}

LBoxAllocation LIRGeneratorRiscv64::useBoxFixed(MDefinition* mir, Register reg1,
                                                Register reg2,
                                                bool useAtStart) {
  MOZ_ASSERT(mir->type() == MIRType::Value);

  ensureDefined(mir);
  return LBoxAllocation(LUse(reg1, mir->virtualRegister(), useAtStart));
}

LAllocation LIRGeneratorRiscv64::useByteOpRegister(MDefinition* mir) {
  return useRegister(mir);
}

LAllocation LIRGeneratorRiscv64::useByteOpRegisterAtStart(MDefinition* mir) {
  return useRegisterAtStart(mir);
}

LAllocation LIRGeneratorRiscv64::useByteOpRegisterOrNonDoubleConstant(
    MDefinition* mir) {
  return useRegisterOrNonDoubleConstant(mir);
}

LDefinition LIRGeneratorRiscv64::tempByteOpRegister() { return temp(); }
LDefinition LIRGeneratorRiscv64::tempToUnbox() { return temp(); }

void LIRGeneratorRiscv64::lowerUntypedPhiInput(MPhi* phi,
                                               uint32_t inputPosition,
                                               LBlock* block, size_t lirIndex) {
  lowerTypedPhiInput(phi, inputPosition, block, lirIndex);
}
void LIRGeneratorRiscv64::lowerInt64PhiInput(MPhi* phi, uint32_t inputPosition,
                                             LBlock* block, size_t lirIndex) {
  lowerTypedPhiInput(phi, inputPosition, block, lirIndex);
}
void LIRGeneratorRiscv64::defineInt64Phi(MPhi* phi, size_t lirIndex) {
  defineTypedPhi(phi, lirIndex);
}

void LIRGeneratorRiscv64::lowerMulI(MMul* mul, MDefinition* lhs,
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

void LIRGeneratorRiscv64::lowerDivI(MDiv* div) {
  // Division instructions are slow. Division by constant denominators can be
  // rewritten to use other instructions.
  if (div->rhs()->isConstant()) {
    int32_t rhs = div->rhs()->toConstant()->toInt32();

    // Check for division by a power of two, which is an easy and important case
    // to optimize.
    if (std::has_single_bit(mozilla::Abs(rhs))) {
      int32_t shift = mozilla::FloorLog2(mozilla::Abs(rhs));
      auto lhs = useRegisterAtStart(div->lhs());
      auto* lir = new (alloc()) LDivPowTwoI(lhs, shift, rhs < 0);
      if (div->fallible()) {
        assignSnapshot(lir, div->bailoutKind());
      }
      define(lir, div);
      return;
    }

    // Division by other constants can be optimized by a reciprocal
    // multiplication technique.
    auto lhs = useRegister(div->lhs());
    auto* lir = new (alloc()) LDivConstantI(lhs, rhs);
    if (div->fallible()) {
      assignSnapshot(lir, div->bailoutKind());
    }
    define(lir, div);
    return;
  }

  LAllocation lhs, rhs;
  if (!div->canTruncateRemainder()) {
    lhs = useRegister(div->lhs());
    rhs = useRegister(div->rhs());
  } else {
    lhs = useRegisterAtStart(div->lhs());
    rhs = useRegisterAtStart(div->rhs());
  }

  // RISCV64 has plenty of scratch registers, so we don't need to request an
  // additonal temp register from the register allocator.
  auto* lir = new (alloc()) LDivI(lhs, rhs, LDefinition::BogusTemp());
  if (div->fallible()) {
    assignSnapshot(lir, div->bailoutKind());
  }
  define(lir, div);
}

void LIRGeneratorRiscv64::lowerDivI64(MDiv* div) {
  if (div->rhs()->isConstant()) {
    int64_t rhs = div->rhs()->toConstant()->toInt64();

    if (std::has_single_bit(mozilla::Abs(rhs))) {
      int32_t shift = mozilla::FloorLog2(mozilla::Abs(rhs));
      auto lhs = useRegisterAtStart(div->lhs());
      auto* lir = new (alloc()) LDivPowTwoI64(lhs, shift, rhs < 0);
      define(lir, div);
      return;
    }

    auto lhs = useRegister(div->lhs());
    auto* lir = new (alloc()) LDivConstantI64(lhs, rhs);
    define(lir, div);
    return;
  }

  auto* lir = new (alloc())
      LDivI64(useRegisterAtStart(div->lhs()), useRegisterAtStart(div->rhs()));
  defineInt64(lir, div);
}

void LIRGeneratorRiscv64::lowerModI(MMod* mod) {
  if (mod->rhs()->isConstant()) {
    int32_t rhs = mod->rhs()->toConstant()->toInt32();

    if (std::has_single_bit(mozilla::Abs(rhs))) {
      int32_t shift = mozilla::FloorLog2(mozilla::Abs(rhs));
      auto lhs = useRegisterAtStart(mod->lhs());
      auto* lir = new (alloc()) LModPowTwoI(lhs, shift);
      if (mod->fallible()) {
        assignSnapshot(lir, mod->bailoutKind());
      }
      define(lir, mod);
      return;
    }

    auto lhs = useRegister(mod->lhs());
    auto* lir = new (alloc()) LModConstantI(lhs, rhs);
    if (mod->fallible()) {
      assignSnapshot(lir, mod->bailoutKind());
    }
    define(lir, mod);
    return;
  }

  LAllocation lhs, rhs;
  if (mod->canBeNegativeDividend() && !mod->isTruncated()) {
    lhs = useRegister(mod->lhs());
    rhs = useRegister(mod->rhs());
  } else {
    lhs = useRegisterAtStart(mod->lhs());
    rhs = useRegisterAtStart(mod->rhs());
  }

  auto* lir = new (alloc()) LModI(lhs, rhs);
  if (mod->fallible()) {
    assignSnapshot(lir, mod->bailoutKind());
  }
  define(lir, mod);
}

void LIRGeneratorRiscv64::lowerModI64(MMod* mod) {
  if (mod->rhs()->isConstant()) {
    int64_t rhs = mod->rhs()->toConstant()->toInt64();

    if (std::has_single_bit(mozilla::Abs(rhs))) {
      int32_t shift = mozilla::FloorLog2(mozilla::Abs(rhs));
      auto lhs = useRegisterAtStart(mod->lhs());
      auto* lir = new (alloc()) LModPowTwoI64(lhs, shift);
      define(lir, mod);
      return;
    }

    auto lhs = useRegister(mod->lhs());
    auto* lir = new (alloc()) LModConstantI64(lhs, rhs);
    define(lir, mod);
    return;
  }

  auto* lir = new (alloc())
      LModI64(useRegisterAtStart(mod->lhs()), useRegisterAtStart(mod->rhs()));
  defineInt64(lir, mod);
}

void LIRGeneratorRiscv64::lowerUDiv(MDiv* div) {
  if (div->rhs()->isConstant()) {
    // NOTE: the result of toInt32 is coerced to uint32_t.
    uint32_t rhs = div->rhs()->toConstant()->toInt32();

    if (std::has_single_bit(rhs)) {
      int32_t shift = mozilla::FloorLog2(rhs);
      auto lhs = useRegisterAtStart(div->lhs());
      auto* lir = new (alloc()) LDivPowTwoI(lhs, shift, false);
      if (div->fallible()) {
        assignSnapshot(lir, div->bailoutKind());
      }
      define(lir, div);
      return;
    }

    auto lhs = useRegister(div->lhs());
    auto* lir = new (alloc()) LUDivConstant(lhs, rhs);
    if (div->fallible()) {
      assignSnapshot(lir, div->bailoutKind());
    }
    define(lir, div);
    return;
  }

  LAllocation lhs, rhs;
  if (!div->canTruncateRemainder()) {
    lhs = useRegister(div->lhs());
    rhs = useRegister(div->rhs());
  } else {
    lhs = useRegisterAtStart(div->lhs());
    rhs = useRegisterAtStart(div->rhs());
  }

  auto* lir = new (alloc()) LUDiv(lhs, rhs);
  if (div->fallible()) {
    assignSnapshot(lir, div->bailoutKind());
  }
  define(lir, div);
}

void LIRGeneratorRiscv64::lowerUDivI64(MDiv* div) {
  if (div->rhs()->isConstant()) {
    // NOTE: the result of toInt64 is coerced to uint64_t.
    uint64_t rhs = div->rhs()->toConstant()->toInt64();

    if (std::has_single_bit(rhs)) {
      int32_t shift = mozilla::FloorLog2(rhs);
      auto lhs = useRegisterAtStart(div->lhs());
      auto* lir = new (alloc()) LDivPowTwoI64(lhs, shift, false);
      define(lir, div);
      return;
    }

    auto lhs = useRegister(div->lhs());
    auto* lir = new (alloc()) LUDivConstantI64(lhs, rhs);
    define(lir, div);
    return;
  }

  auto* lir = new (alloc())
      LUDivI64(useRegisterAtStart(div->lhs()), useRegisterAtStart(div->rhs()));
  defineInt64(lir, div);
}

void LIRGeneratorRiscv64::lowerUMod(MMod* mod) {
  if (mod->rhs()->isConstant()) {
    // NOTE: the result of toInt32 is coerced to uint32_t.
    uint32_t rhs = mod->rhs()->toConstant()->toInt32();

    if (std::has_single_bit(rhs)) {
      int32_t shift = mozilla::FloorLog2(rhs);
      auto lhs = useRegisterAtStart(mod->lhs());
      auto* lir = new (alloc()) LModPowTwoI(lhs, shift);
      if (mod->fallible()) {
        assignSnapshot(lir, mod->bailoutKind());
      }
      define(lir, mod);
      return;
    }

    auto lhs = useRegister(mod->lhs());
    auto* lir = new (alloc()) LUModConstant(lhs, rhs);
    if (mod->fallible()) {
      assignSnapshot(lir, mod->bailoutKind());
    }
    define(lir, mod);
    return;
  }

  auto* lir = new (alloc())
      LUMod(useRegisterAtStart(mod->lhs()), useRegisterAtStart(mod->rhs()));
  if (mod->fallible()) {
    assignSnapshot(lir, mod->bailoutKind());
  }
  define(lir, mod);
}

void LIRGeneratorRiscv64::lowerUModI64(MMod* mod) {
  if (mod->rhs()->isConstant()) {
    // NOTE: the result of toInt64 is coerced to uint64_t.
    uint64_t rhs = mod->rhs()->toConstant()->toInt64();

    if (std::has_single_bit(rhs)) {
      int32_t shift = mozilla::FloorLog2(rhs);
      auto lhs = useRegisterAtStart(mod->lhs());
      auto* lir = new (alloc()) LModPowTwoI64(lhs, shift);
      define(lir, mod);
      return;
    }

    auto lhs = useRegister(mod->lhs());
    auto* lir = new (alloc()) LUModConstantI64(lhs, rhs);
    define(lir, mod);
    return;
  }

  auto* lir = new (alloc())
      LUModI64(useRegisterAtStart(mod->lhs()), useRegisterAtStart(mod->rhs()));
  defineInt64(lir, mod);
}

void LIRGeneratorRiscv64::lowerUrshD(MUrsh* mir) {
  MDefinition* lhs = mir->lhs();
  MDefinition* rhs = mir->rhs();

  MOZ_ASSERT(lhs->type() == MIRType::Int32);
  MOZ_ASSERT(rhs->type() == MIRType::Int32);

  auto* lir = new (alloc()) LUrshD(useRegisterAtStart(lhs),
                                   useRegisterOrConstantAtStart(rhs), temp());
  define(lir, mir);
}

void LIRGeneratorRiscv64::lowerPowOfTwoI(MPow* mir) {
  int32_t base = mir->input()->toConstant()->toInt32();
  MDefinition* power = mir->power();

  auto* lir = new (alloc()) LPowOfTwoI(useRegister(power), base);
  assignSnapshot(lir, mir->bailoutKind());
  define(lir, mir);
}

void LIRGeneratorRiscv64::lowerTruncateDToInt32(MTruncateToInt32* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Double);

  define(new (alloc()) LTruncateDToInt32(useRegister(opd), tempDouble()), ins);
}

void LIRGeneratorRiscv64::lowerTruncateFToInt32(MTruncateToInt32* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Float32);

  define(new (alloc()) LTruncateFToInt32(useRegister(opd), tempFloat32()), ins);
}

void LIRGeneratorRiscv64::lowerBuiltinInt64ToFloatingPoint(
    MBuiltinInt64ToFloatingPoint* ins) {
  MOZ_CRASH("We don't use it for this architecture");
}

void LIRGeneratorRiscv64::lowerWasmSelectI(MWasmSelect* select) {
  MIRType type = select->type();

  if (type == MIRType::Int32 || type == MIRType::WasmAnyRef) {
    auto* lir =
        new (alloc()) LWasmSelect(useRegisterAtStart(select->trueExpr()),
                                  useRegisterAtStart(select->falseExpr()),
                                  useRegisterAtStart(select->condExpr()));
    define(lir, select);
  } else {
    MOZ_ASSERT(type == MIRType::Float32 || type == MIRType::Double);

    auto* lir = new (alloc()) LWasmSelect(
        useRegisterAtStart(select->trueExpr()), useAny(select->falseExpr()),
        useRegister(select->condExpr()));
    defineReuseInput(lir, select, LWasmSelect::TrueExprIndex);
  }
}

void LIRGeneratorRiscv64::lowerWasmSelectI64(MWasmSelect* select) {
  auto* lir =
      new (alloc()) LWasmSelectI64(useInt64RegisterAtStart(select->trueExpr()),
                                   useInt64RegisterAtStart(select->falseExpr()),
                                   useRegisterAtStart(select->condExpr()));
  defineInt64(lir, select);
}

// On riscv64 we specialize the cases: compare is {{U,}Int32, {U,}Int64},
// Float32, Double}, and select is {{U,}Int32, {U,}Int64}}.
bool LIRGeneratorShared::canSpecializeWasmCompareAndSelect(
    MCompare::CompareType compTy, MIRType insTy) {
  return (insTy == MIRType::Int32 || insTy == MIRType::Int64) &&
         (compTy == MCompare::Compare_Int32 ||
          compTy == MCompare::Compare_UInt32 ||
          compTy == MCompare::Compare_Int64 ||
          compTy == MCompare::Compare_UInt64 ||
          compTy == MCompare::Compare_Float32 ||
          compTy == MCompare::Compare_Double);
}

void LIRGeneratorShared::lowerWasmCompareAndSelect(MWasmSelect* ins,
                                                   MDefinition* lhs,
                                                   MDefinition* rhs,
                                                   MCompare::CompareType compTy,
                                                   JSOp jsop) {
  MOZ_ASSERT(canSpecializeWasmCompareAndSelect(compTy, ins->type()));

  auto* lir = new (alloc())
      LWasmCompareAndSelect(useRegisterAtStart(lhs), useRegisterAtStart(rhs),
                            useRegisterAtStart(ins->trueExpr()),
                            useRegisterAtStart(ins->falseExpr()), compTy, jsop);
  define(lir, ins);
}

void LIRGeneratorRiscv64::lowerWasmBuiltinTruncateToInt32(
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
             useRegister(opd), LAllocation(), LDefinition::BogusTemp()),
         ins);
}

void LIRGeneratorRiscv64::lowerWasmBuiltinTruncateToInt64(
    MWasmBuiltinTruncateToInt64* ins) {
  MOZ_CRASH("We don't use it for this architecture");
}

void LIRGeneratorRiscv64::lowerWasmBuiltinDivI64(MWasmBuiltinDivI64* div) {
  MOZ_CRASH("We don't use runtime div for this architecture");
}

void LIRGeneratorRiscv64::lowerWasmBuiltinModI64(MWasmBuiltinModI64* mod) {
  MOZ_CRASH("We don't use runtime mod for this architecture");
}

void LIRGeneratorRiscv64::lowerBigIntPtrLsh(MBigIntPtrLsh* ins) {
  auto* lir = new (alloc()) LBigIntPtrLsh(
      useRegister(ins->lhs()), useRegister(ins->rhs()), temp(), temp());
  assignSnapshot(lir, ins->bailoutKind());
  define(lir, ins);
}

void LIRGeneratorRiscv64::lowerBigIntPtrRsh(MBigIntPtrRsh* ins) {
  auto* lir = new (alloc()) LBigIntPtrRsh(
      useRegister(ins->lhs()), useRegister(ins->rhs()), temp(), temp());
  assignSnapshot(lir, ins->bailoutKind());
  define(lir, ins);
}

void LIRGeneratorRiscv64::lowerBigIntPtrDiv(MBigIntPtrDiv* ins) {
  auto* lir = new (alloc())
      LBigIntPtrDiv(useRegister(ins->lhs()), useRegister(ins->rhs()),
                    LDefinition::BogusTemp(), LDefinition::BogusTemp());
  assignSnapshot(lir, ins->bailoutKind());
  define(lir, ins);
}

void LIRGeneratorRiscv64::lowerBigIntPtrMod(MBigIntPtrMod* ins) {
  auto* lir = new (alloc())
      LBigIntPtrMod(useRegister(ins->lhs()), useRegister(ins->rhs()), temp(),
                    LDefinition::BogusTemp());
  if (ins->canBeDivideByZero()) {
    assignSnapshot(lir, ins->bailoutKind());
  }
  define(lir, ins);
}

void LIRGeneratorRiscv64::lowerAtomicLoad64(MLoadUnboxedScalar* ins) {
  LUse elements = useRegisterAtStart(ins->elements());
  LAllocation index =
      useRegisterOrIndexConstantAtStart(ins->index(), ins->storageType());

  auto* lir = new (alloc()) LAtomicLoad64(elements, index);
  defineInt64(lir, ins);
}

void LIRGeneratorRiscv64::lowerAtomicStore64(MStoreUnboxedScalar* ins) {
  LUse elements = useRegisterAtStart(ins->elements());
  LAllocation index =
      useRegisterOrIndexConstantAtStart(ins->index(), ins->writeType());
  LInt64Allocation value = useInt64RegisterAtStart(ins->value());

  add(new (alloc()) LAtomicStore64(elements, index, value), ins);
}

void LIRGenerator::visitBox(MBox* ins) {
  MDefinition* opd = ins->getOperand(0);

  // If the operand is a constant, emit near its uses.
  if (opd->isConstant() && ins->canEmitAtUses()) {
    emitAtUses(ins);
    return;
  }

  if (opd->isConstant()) {
    define(new (alloc()) LValue(opd->toConstant()->toJSValue()), ins,
           LDefinition(LDefinition::BOX));
  } else {
    define(new (alloc()) LBox(useRegisterAtStart(opd), opd->type()), ins,
           LDefinition(LDefinition::BOX));
  }
}

void LIRGenerator::visitUnbox(MUnbox* ins) {
  MDefinition* box = ins->getOperand(0);
  MOZ_ASSERT(box->type() == MIRType::Value);

  LInstructionHelper<1, BOX_PIECES, 0>* lir;
  if (IsFloatingPointType(ins->type())) {
    MOZ_ASSERT(ins->type() == MIRType::Double);
    lir = new (alloc()) LUnboxFloatingPoint(useBoxAtStart(box));
  } else if (ins->fallible()) {
    // If the unbox is fallible, load the Value in a register first to
    // avoid multiple loads.
    lir = new (alloc()) LUnbox(useRegisterAtStart(box));
  } else {
    lir = new (alloc()) LUnbox(useAtStart(box));
  }

  if (ins->fallible()) {
    assignSnapshot(lir, ins->bailoutKind());
  }

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

void LIRGenerator::visitInt64ToFloatingPoint(MInt64ToFloatingPoint* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Int64);
  MOZ_ASSERT(IsFloatingPointType(ins->type()));

  define(new (alloc()) LInt64ToFloatingPoint(useInt64Register(opd)), ins);
}

void LIRGenerator::visitSubstr(MSubstr* ins) {
  LSubstr* lir = new (alloc())
      LSubstr(useRegister(ins->string()), useRegister(ins->begin()),
              useRegister(ins->length()), temp(), temp(), temp());
  define(lir, ins);
  assignSafepoint(lir, ins);
}

void LIRGenerator::visitCompareExchangeTypedArrayElement(
    MCompareExchangeTypedArrayElement* ins) {
  MOZ_ASSERT(!Scalar::isFloatingType(ins->arrayType()));
  MOZ_ASSERT(ins->elements()->type() == MIRType::Elements);
  MOZ_ASSERT(ins->index()->type() == MIRType::IntPtr);

  LUse elements = useRegister(ins->elements());
  LAllocation index =
      useRegisterOrIndexConstant(ins->index(), ins->arrayType());

  if (Scalar::isBigIntType(ins->arrayType())) {
    LInt64Allocation oldval = useInt64Register(ins->oldval());
    LInt64Allocation newval = useInt64Register(ins->newval());

    auto* lir = new (alloc())
        LCompareExchangeTypedArrayElement64(elements, index, oldval, newval);
    defineInt64(lir, ins);
    return;
  }

  LAllocation oldval = useRegister(ins->oldval());
  LAllocation newval = useRegister(ins->newval());

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

  LUse elements = useRegisterAtStart(ins->elements());
  LAllocation index =
      useRegisterOrIndexConstantAtStart(ins->index(), ins->arrayType());

  if (Scalar::isBigIntType(ins->arrayType())) {
    LInt64Allocation value = useInt64RegisterAtStart(ins->value());

    auto* lir = new (alloc())
        LAtomicExchangeTypedArrayElement64(elements, index, value);
    defineInt64(lir, ins);
    return;
  }

  MOZ_ASSERT(ins->arrayType() <= Scalar::Uint32);

  LAllocation value = useRegisterAtStart(ins->value());

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

void LIRGenerator::visitAtomicTypedArrayElementBinop(
    MAtomicTypedArrayElementBinop* ins) {
  MOZ_ASSERT(ins->arrayType() != Scalar::Uint8Clamped);
  MOZ_ASSERT(!Scalar::isFloatingType(ins->arrayType()));
  MOZ_ASSERT(ins->elements()->type() == MIRType::Elements);
  MOZ_ASSERT(ins->index()->type() == MIRType::IntPtr);

  LUse elements = useRegisterAtStart(ins->elements());
  LAllocation index =
      useRegisterOrIndexConstantAtStart(ins->index(), ins->arrayType());

  if (Scalar::isBigIntType(ins->arrayType())) {
    LInt64Allocation value = useInt64RegisterAtStart(ins->value());

    // Case 1: the result of the operation is not used.

    if (ins->isForEffect()) {
      auto* lir = new (alloc())
          LAtomicTypedArrayElementBinopForEffect64(elements, index, value);
      add(lir, ins);
      return;
    }

    // Case 2: the result of the operation is used.

    auto* lir =
        new (alloc()) LAtomicTypedArrayElementBinop64(elements, index, value);
    defineInt64(lir, ins);
    return;
  }

  LAllocation value = useRegisterAtStart(ins->value());

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

void LIRGenerator::visitWasmLoad(MWasmLoad* ins) {
  MDefinition* base = ins->base();
  // 'base' is a GPR but may be of either type. If it is 32-bit, it is
  // sign-extended on riscv64 platform and we should explicitly promote it
  // to 64-bit by zero-extension when use it as an index register in memory
  // accesses.
  MOZ_ASSERT(base->type() == MIRType::Int32 || base->type() == MIRType::Int64);

  LAllocation memoryBase =
      ins->hasMemoryBase() ? LAllocation(useRegisterAtStart(ins->memoryBase()))
                           : LGeneralReg(HeapReg);

  LAllocation ptr = useRegisterOrConstantAtStart(base);

  if (ins->type() == MIRType::Int64) {
    auto* lir = new (alloc()) LWasmLoadI64(ptr, memoryBase);
    defineInt64(lir, ins);
    return;
  }

  auto* lir = new (alloc()) LWasmLoad(ptr, memoryBase);
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

  LAllocation baseAlloc = useRegisterOrConstantAtStart(base);

  if (ins->access().type() == Scalar::Int64) {
    LInt64Allocation valueAlloc = useInt64RegisterOrZeroAtStart(value);
    auto* lir = new (alloc()) LWasmStoreI64(baseAlloc, valueAlloc, memoryBase);
    add(lir, ins);
    return;
  }

  LAllocation valueAlloc = useRegisterOrZeroAtStart(value);
  auto* lir = new (alloc()) LWasmStore(baseAlloc, valueAlloc, memoryBase);
  add(lir, ins);
}

void LIRGenerator::visitWasmTruncateToInt64(MWasmTruncateToInt64* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Double || opd->type() == MIRType::Float32);

  defineInt64(new (alloc()) LWasmTruncateToInt64(useRegister(opd)), ins);
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

  LAllocation ptr = useRegisterAtStart(base);

  LAllocation memoryBase =
      ins->hasMemoryBase() ? LAllocation(useRegisterAtStart(ins->memoryBase()))
                           : LGeneralReg(HeapReg);

  if (ins->access().type() == Scalar::Int64) {
    LInt64Allocation value = useInt64RegisterAtStart(ins->value());

    auto* lir = new (alloc()) LWasmAtomicExchangeI64(ptr, value, memoryBase);
    defineInt64(lir, ins);
    return;
  }

  LAllocation value = useRegisterAtStart(ins->value());

  LDefinition valueTemp = LDefinition::BogusTemp();
  LDefinition offsetTemp = LDefinition::BogusTemp();
  LDefinition maskTemp = LDefinition::BogusTemp();

  if (ins->access().byteSize() < 4) {
    valueTemp = temp();
    offsetTemp = temp();
    maskTemp = temp();
  }

  auto* lir = new (alloc()) LWasmAtomicExchangeHeap(
      ptr, value, memoryBase, valueTemp, offsetTemp, maskTemp);
  define(lir, ins);
}

void LIRGenerator::visitWasmAtomicBinopHeap(MWasmAtomicBinopHeap* ins) {
  MDefinition* base = ins->base();
  // See comment in visitWasmLoad re the type of 'base'.
  MOZ_ASSERT(base->type() == MIRType::Int32 || base->type() == MIRType::Int64);

  LAllocation ptr = useRegisterAtStart(base);

  LAllocation memoryBase =
      ins->hasMemoryBase() ? LAllocation(useRegisterAtStart(ins->memoryBase()))
                           : LGeneralReg(HeapReg);

  if (ins->access().type() == Scalar::Int64) {
    LInt64Allocation value = useInt64RegisterAtStart(ins->value());

    auto* lir = new (alloc()) LWasmAtomicBinopI64(ptr, value, memoryBase);
    defineInt64(lir, ins);
    return;
  }

  LAllocation value = useRegisterAtStart(ins->value());

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
        ptr, value, memoryBase, valueTemp, offsetTemp, maskTemp);
    add(lir, ins);
    return;
  }

  auto* lir = new (alloc()) LWasmAtomicBinopHeap(
      ptr, value, memoryBase, valueTemp, offsetTemp, maskTemp);
  define(lir, ins);
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
