/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/loong64/Lowering-loong64.h"

#include "mozilla/MathAlgorithms.h"

#include <bit>

#include "jit/loong64/Assembler-loong64.h"
#include "jit/Lowering.h"
#include "jit/MIR-wasm.h"
#include "jit/MIR.h"
#include "wasm/WasmFeatures.h"

#include "jit/shared/Lowering-shared-inl.h"

using namespace js;
using namespace js::jit;

using mozilla::FloorLog2;

LTableSwitch* LIRGeneratorLOONG64::newLTableSwitch(
    const LAllocation& in, const LDefinition& inputCopy) {
  return new (alloc()) LTableSwitch(in, inputCopy, temp());
}

LTableSwitchV* LIRGeneratorLOONG64::newLTableSwitchV(const LBoxAllocation& in) {
  return new (alloc()) LTableSwitchV(in, temp(), tempDouble(), temp());
}

void LIRGeneratorLOONG64::lowerForShift(LInstructionHelper<1, 2, 0>* ins,
                                        MDefinition* mir, MDefinition* lhs,
                                        MDefinition* rhs) {
  lowerForALU(ins, mir, lhs, rhs);
}

template <class LInstr>
void LIRGeneratorLOONG64::lowerForShiftInt64(LInstr* ins, MDefinition* mir,
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

template void LIRGeneratorLOONG64::lowerForShiftInt64(LShiftI64* ins,
                                                      MDefinition* mir,
                                                      MDefinition* lhs,
                                                      MDefinition* rhs);
template void LIRGeneratorLOONG64::lowerForShiftInt64(LRotateI64* ins,
                                                      MDefinition* mir,
                                                      MDefinition* lhs,
                                                      MDefinition* rhs);

// x = !y
void LIRGeneratorLOONG64::lowerForALU(LInstructionHelper<1, 1, 0>* ins,
                                      MDefinition* mir, MDefinition* input) {
  // Unary ALU operations don't read the input after writing to the output, even
  // for fallible operations, so we can use at-start allocations.
  ins->setOperand(0, useRegisterAtStart(input));
  define(ins, mir);
}

// z = x + y
void LIRGeneratorLOONG64::lowerForALU(LInstructionHelper<1, 2, 0>* ins,
                                      MDefinition* mir, MDefinition* lhs,
                                      MDefinition* rhs) {
  // Binary ALU operations don't read any input after writing to the output,
  // even for fallible operations, so we can use at-start allocations.
  ins->setOperand(0, useRegisterAtStart(lhs));
  ins->setOperand(1, useRegisterOrConstantAtStart(rhs));
  define(ins, mir);
}

void LIRGeneratorLOONG64::lowerForALUInt64(
    LInstructionHelper<INT64_PIECES, INT64_PIECES, 0>* ins, MDefinition* mir,
    MDefinition* input) {
  ins->setInt64Operand(0, useInt64RegisterAtStart(input));
  defineInt64(ins, mir);
}

void LIRGeneratorLOONG64::lowerForALUInt64(
    LInstructionHelper<INT64_PIECES, 2 * INT64_PIECES, 0>* ins,
    MDefinition* mir, MDefinition* lhs, MDefinition* rhs) {
  ins->setInt64Operand(0, useInt64RegisterAtStart(lhs));
  ins->setInt64Operand(INT64_PIECES, useInt64RegisterOrConstantAtStart(rhs));
  defineInt64(ins, mir);
}

void LIRGeneratorLOONG64::lowerForMulInt64(LMulI64* ins, MMul* mir,
                                           MDefinition* lhs, MDefinition* rhs) {
  lowerForALUInt64(ins, mir, lhs, rhs);
}

void LIRGeneratorLOONG64::lowerForFPU(LInstructionHelper<1, 1, 0>* ins,
                                      MDefinition* mir, MDefinition* input) {
  ins->setOperand(0, useRegisterAtStart(input));
  define(ins, mir);
}

void LIRGeneratorLOONG64::lowerForFPU(LInstructionHelper<1, 2, 0>* ins,
                                      MDefinition* mir, MDefinition* lhs,
                                      MDefinition* rhs) {
  ins->setOperand(0, useRegisterAtStart(lhs));
  ins->setOperand(1, useRegisterAtStart(rhs));
  define(ins, mir);
}

LBoxAllocation LIRGeneratorLOONG64::useBoxFixed(MDefinition* mir, Register reg1,
                                                Register reg2,
                                                bool useAtStart) {
  MOZ_ASSERT(mir->type() == MIRType::Value);

  ensureDefined(mir);
  return LBoxAllocation(LUse(reg1, mir->virtualRegister(), useAtStart));
}

LAllocation LIRGeneratorLOONG64::useByteOpRegister(MDefinition* mir) {
  return useRegister(mir);
}

LAllocation LIRGeneratorLOONG64::useByteOpRegisterAtStart(MDefinition* mir) {
  return useRegisterAtStart(mir);
}

LAllocation LIRGeneratorLOONG64::useByteOpRegisterOrNonDoubleConstant(
    MDefinition* mir) {
  return useRegisterOrNonDoubleConstant(mir);
}

LDefinition LIRGeneratorLOONG64::tempByteOpRegister() { return temp(); }

LDefinition LIRGeneratorLOONG64::tempToUnbox() { return temp(); }

void LIRGeneratorLOONG64::lowerUntypedPhiInput(MPhi* phi,
                                               uint32_t inputPosition,
                                               LBlock* block, size_t lirIndex) {
  lowerTypedPhiInput(phi, inputPosition, block, lirIndex);
}
void LIRGeneratorLOONG64::lowerInt64PhiInput(MPhi* phi, uint32_t inputPosition,
                                             LBlock* block, size_t lirIndex) {
  lowerTypedPhiInput(phi, inputPosition, block, lirIndex);
}
void LIRGeneratorLOONG64::defineInt64Phi(MPhi* phi, size_t lirIndex) {
  defineTypedPhi(phi, lirIndex);
}

void LIRGeneratorLOONG64::lowerMulI(MMul* mul, MDefinition* lhs,
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

void LIRGeneratorLOONG64::lowerDivI(MDiv* div) {
  // Division instructions are slow. Division by constant denominators can be
  // rewritten to use other instructions.
  if (div->rhs()->isConstant()) {
    int32_t rhs = div->rhs()->toConstant()->toInt32();

    // Check for division by a power of two, which is an easy and important case
    // to optimize. Division by other constants can be optimized by a reciprocal
    // multiplication technique.
    if (std::has_single_bit(mozilla::Abs(rhs))) {
      int32_t shift = mozilla::FloorLog2(mozilla::Abs(rhs));
      auto* lir = new (alloc())
          LDivPowTwoI(useRegisterAtStart(div->lhs()), temp(), shift, rhs < 0);
      if (div->fallible()) {
        assignSnapshot(lir, div->bailoutKind());
      }
      define(lir, div);
      return;
    }

    auto* lir = new (alloc()) LDivConstantI(useRegister(div->lhs()), rhs);
    if (div->fallible()) {
      assignSnapshot(lir, div->bailoutKind());
    }
    define(lir, div);
    return;
  }

  LDivI* lir = new (alloc())
      LDivI(useRegister(div->lhs()), useRegister(div->rhs()), temp());
  if (div->fallible()) {
    assignSnapshot(lir, div->bailoutKind());
  }
  define(lir, div);
}

void LIRGeneratorLOONG64::lowerDivI64(MDiv* div) {
  if (div->rhs()->isConstant()) {
    int64_t rhs = div->rhs()->toConstant()->toInt64();

    if (std::has_single_bit(mozilla::Abs(rhs))) {
      int32_t shift = mozilla::FloorLog2(mozilla::Abs(rhs));
      auto* lir = new (alloc())
          LDivPowTwoI64(useRegisterAtStart(div->lhs()), shift, rhs < 0);
      define(lir, div);
      return;
    }

    auto* lir = new (alloc()) LDivConstantI64(useRegister(div->lhs()), rhs);
    define(lir, div);
    return;
  }

  auto* lir = new (alloc())
      LDivOrModI64(useRegister(div->lhs()), useRegister(div->rhs()));
  defineInt64(lir, div);
}

void LIRGeneratorLOONG64::lowerModI(MMod* mod) {
  if (mod->rhs()->isConstant()) {
    int32_t rhs = mod->rhs()->toConstant()->toInt32();

    if (std::has_single_bit(mozilla::Abs(rhs))) {
      int32_t shift = mozilla::FloorLog2(mozilla::Abs(rhs));
      auto* lir =
          new (alloc()) LModPowTwoI(useRegisterAtStart(mod->lhs()), shift);
      if (mod->fallible()) {
        assignSnapshot(lir, mod->bailoutKind());
      }
      define(lir, mod);
      return;
    }

    auto* lir = new (alloc()) LModConstantI(useRegister(mod->lhs()), rhs);
    if (mod->fallible()) {
      assignSnapshot(lir, mod->bailoutKind());
    }
    define(lir, mod);
    return;
  }

  auto* lir =
      new (alloc()) LModI(useRegister(mod->lhs()), useRegister(mod->rhs()));
  if (mod->fallible()) {
    assignSnapshot(lir, mod->bailoutKind());
  }
  define(lir, mod);
}

void LIRGeneratorLOONG64::lowerModI64(MMod* mod) {
  if (mod->rhs()->isConstant()) {
    int64_t rhs = mod->rhs()->toConstant()->toInt64();

    if (std::has_single_bit(mozilla::Abs(rhs))) {
      int32_t shift = mozilla::FloorLog2(mozilla::Abs(rhs));
      auto* lir =
          new (alloc()) LModPowTwoI64(useRegisterAtStart(mod->lhs()), shift);
      define(lir, mod);
      return;
    }

    auto* lir = new (alloc()) LModConstantI64(useRegister(mod->lhs()), rhs);
    define(lir, mod);
    return;
  }

  auto* lir = new (alloc())
      LDivOrModI64(useRegister(mod->lhs()), useRegister(mod->rhs()));
  defineInt64(lir, mod);
}

void LIRGeneratorLOONG64::lowerUDiv(MDiv* div) {
  if (div->rhs()->isConstant()) {
    // NOTE: the result of toInt32 is coerced to uint32_t.
    uint32_t rhs = div->rhs()->toConstant()->toInt32();

    if (std::has_single_bit(rhs)) {
      int32_t shift = mozilla::FloorLog2(rhs);
      auto* lir = new (alloc())
          LDivPowTwoI(useRegisterAtStart(div->lhs()), temp(), shift, false);
      if (div->fallible()) {
        assignSnapshot(lir, div->bailoutKind());
      }
      define(lir, div);
      return;
    }

    auto* lir = new (alloc()) LUDivConstant(useRegister(div->lhs()), rhs);
    if (div->fallible()) {
      assignSnapshot(lir, div->bailoutKind());
    }
    define(lir, div);
    return;
  }

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

void LIRGeneratorLOONG64::lowerUDivI64(MDiv* div) {
  if (div->rhs()->isConstant()) {
    // NOTE: the result of toInt64 is coerced to uint64_t.
    uint64_t rhs = div->rhs()->toConstant()->toInt64();

    if (std::has_single_bit(rhs)) {
      int32_t shift = mozilla::FloorLog2(rhs);
      auto* lir = new (alloc())
          LDivPowTwoI64(useRegisterAtStart(div->lhs()), shift, false);
      define(lir, div);
      return;
    }

    auto* lir = new (alloc()) LUDivConstantI64(useRegister(div->lhs()), rhs);
    define(lir, div);
    return;
  }

  auto* lir = new (alloc())
      LUDivOrModI64(useRegister(div->lhs()), useRegister(div->rhs()));
  defineInt64(lir, div);
}

void LIRGeneratorLOONG64::lowerUMod(MMod* mod) {
  if (mod->rhs()->isConstant()) {
    // NOTE: the result of toInt32 is coerced to uint32_t.
    uint32_t rhs = mod->rhs()->toConstant()->toInt32();

    if (std::has_single_bit(rhs)) {
      int32_t shift = mozilla::FloorLog2(rhs);
      auto* lir =
          new (alloc()) LModPowTwoI(useRegisterAtStart(mod->lhs()), shift);
      if (mod->fallible()) {
        assignSnapshot(lir, mod->bailoutKind());
      }
      define(lir, mod);
      return;
    }

    auto* lir = new (alloc()) LUModConstant(useRegister(mod->lhs()), rhs);
    if (mod->fallible()) {
      assignSnapshot(lir, mod->bailoutKind());
    }
    define(lir, mod);
    return;
  }

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

void LIRGeneratorLOONG64::lowerUModI64(MMod* mod) {
  if (mod->rhs()->isConstant()) {
    // NOTE: the result of toInt64 is coerced to uint64_t.
    uint64_t rhs = mod->rhs()->toConstant()->toInt64();

    if (std::has_single_bit(rhs)) {
      int32_t shift = mozilla::FloorLog2(rhs);
      auto* lir =
          new (alloc()) LModPowTwoI64(useRegisterAtStart(mod->lhs()), shift);
      define(lir, mod);
      return;
    }

    auto* lir = new (alloc()) LUModConstantI64(useRegister(mod->lhs()), rhs);
    define(lir, mod);
    return;
  }

  auto* lir = new (alloc())
      LUDivOrModI64(useRegister(mod->lhs()), useRegister(mod->rhs()));
  defineInt64(lir, mod);
}

void LIRGeneratorLOONG64::lowerUrshD(MUrsh* mir) {
  MDefinition* lhs = mir->lhs();
  MDefinition* rhs = mir->rhs();

  MOZ_ASSERT(lhs->type() == MIRType::Int32);
  MOZ_ASSERT(rhs->type() == MIRType::Int32);

  auto* lir = new (alloc()) LUrshD(useRegisterAtStart(lhs),
                                   useRegisterOrConstantAtStart(rhs), temp());
  define(lir, mir);
}

void LIRGeneratorLOONG64::lowerPowOfTwoI(MPow* mir) {
  int32_t base = mir->input()->toConstant()->toInt32();
  MDefinition* power = mir->power();

  auto* lir = new (alloc()) LPowOfTwoI(useRegister(power), base);
  assignSnapshot(lir, mir->bailoutKind());
  define(lir, mir);
}

void LIRGeneratorLOONG64::lowerBigIntPtrLsh(MBigIntPtrLsh* ins) {
  auto* lir = new (alloc()) LBigIntPtrLsh(
      useRegister(ins->lhs()), useRegister(ins->rhs()), temp(), temp());
  assignSnapshot(lir, ins->bailoutKind());
  define(lir, ins);
}

void LIRGeneratorLOONG64::lowerBigIntPtrRsh(MBigIntPtrRsh* ins) {
  auto* lir = new (alloc()) LBigIntPtrRsh(
      useRegister(ins->lhs()), useRegister(ins->rhs()), temp(), temp());
  assignSnapshot(lir, ins->bailoutKind());
  define(lir, ins);
}

void LIRGeneratorLOONG64::lowerBigIntPtrDiv(MBigIntPtrDiv* ins) {
  auto* lir = new (alloc())
      LBigIntPtrDiv(useRegister(ins->lhs()), useRegister(ins->rhs()),
                    LDefinition::BogusTemp(), LDefinition::BogusTemp());
  assignSnapshot(lir, ins->bailoutKind());
  define(lir, ins);
}

void LIRGeneratorLOONG64::lowerBigIntPtrMod(MBigIntPtrMod* ins) {
  auto* lir = new (alloc())
      LBigIntPtrMod(useRegister(ins->lhs()), useRegister(ins->rhs()), temp(),
                    LDefinition::BogusTemp());
  if (ins->canBeDivideByZero()) {
    assignSnapshot(lir, ins->bailoutKind());
  }
  define(lir, ins);
}

void LIRGeneratorLOONG64::lowerTruncateDToInt32(MTruncateToInt32* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Double);

  define(new (alloc()) LTruncateDToInt32(useRegister(opd), tempDouble()), ins);
}

void LIRGeneratorLOONG64::lowerTruncateFToInt32(MTruncateToInt32* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Float32);

  define(new (alloc()) LTruncateFToInt32(useRegister(opd), tempFloat32()), ins);
}

void LIRGeneratorLOONG64::lowerBuiltinInt64ToFloatingPoint(
    MBuiltinInt64ToFloatingPoint* ins) {
  MOZ_CRASH("We don't use it for this architecture");
}

void LIRGeneratorLOONG64::lowerWasmSelectI(MWasmSelect* select) {
  MIRType type = select->type();

  if (type == MIRType::Int32 || type == MIRType::WasmAnyRef) {
    auto* lir =
        new (alloc()) LWasmSelect(useRegisterAtStart(select->trueExpr()),
                                  useRegisterAtStart(select->falseExpr()),
                                  useRegisterAtStart(select->condExpr()));
    define(lir, select);
#ifdef ENABLE_JIT_SIMD
  } else if (type == MIRType::Simd128) {
    auto* lir = new (alloc()) LWasmSelect(
        useRegisterAtStart(select->trueExpr()),
        useRegister(select->falseExpr()), useRegister(select->condExpr()));
    defineReuseInput(lir, select, LWasmSelect::TrueExprIndex);
#endif
  } else {
    MOZ_ASSERT(type == MIRType::Float32 || type == MIRType::Double);

    auto* lir = new (alloc()) LWasmSelect(
        useRegisterAtStart(select->trueExpr()), useAny(select->falseExpr()),
        useRegister(select->condExpr()));
    defineReuseInput(lir, select, LWasmSelect::TrueExprIndex);
  }
}

void LIRGeneratorLOONG64::lowerWasmSelectI64(MWasmSelect* select) {
  auto* lir =
      new (alloc()) LWasmSelectI64(useInt64RegisterAtStart(select->trueExpr()),
                                   useInt64RegisterAtStart(select->falseExpr()),
                                   useRegisterAtStart(select->condExpr()));
  defineInt64(lir, select);
}

// On loong64 we specialize for these cases (as in "compare x select"):
// {{U,}Int32, {U,}Int64}, Float32, Double}
// x
// {{U,}Int32, {U,}Int64}, Float32, Double}
bool LIRGeneratorShared::canSpecializeWasmCompareAndSelect(
    MCompare::CompareType compTy, MIRType insTy) {
  return (insTy == MIRType::Int32 || insTy == MIRType::Int64 ||
          insTy == MIRType::Float32 || insTy == MIRType::Double) &&
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

void LIRGeneratorLOONG64::lowerWasmBuiltinTruncateToInt32(
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

void LIRGeneratorLOONG64::lowerWasmBuiltinTruncateToInt64(
    MWasmBuiltinTruncateToInt64* ins) {
  MOZ_CRASH("We don't use it for this architecture");
}

void LIRGeneratorLOONG64::lowerWasmBuiltinDivI64(MWasmBuiltinDivI64* div) {
  MOZ_CRASH("We don't use runtime div for this architecture");
}

void LIRGeneratorLOONG64::lowerWasmBuiltinModI64(MWasmBuiltinModI64* mod) {
  MOZ_CRASH("We don't use runtime mod for this architecture");
}

void LIRGeneratorLOONG64::lowerAtomicLoad64(MLoadUnboxedScalar* ins) {
  const LUse elements = useRegister(ins->elements());
  const LAllocation index =
      useRegisterOrIndexConstant(ins->index(), ins->storageType());

  auto* lir = new (alloc()) LAtomicLoad64(elements, index);
  defineInt64(lir, ins);
}

void LIRGeneratorLOONG64::lowerAtomicStore64(MStoreUnboxedScalar* ins) {
  LUse elements = useRegister(ins->elements());
  LAllocation index =
      useRegisterOrIndexConstant(ins->index(), ins->writeType());
  LInt64Allocation value = useInt64Register(ins->value());

  add(new (alloc()) LAtomicStore64(elements, index, value), ins);
}

void LIRGenerator::visitBox(MBox* box) {
  MDefinition* opd = box->getOperand(0);

  // If the operand is a constant, emit near its uses.
  if (opd->isConstant() && box->canEmitAtUses()) {
    emitAtUses(box);
    return;
  }

  if (opd->isConstant()) {
    define(new (alloc()) LValue(opd->toConstant()->toJSValue()), box,
           LDefinition(LDefinition::BOX));
  } else {
    LBox* ins = new (alloc()) LBox(useRegisterAtStart(opd), opd->type());
    define(ins, box, LDefinition(LDefinition::BOX));
  }
}

void LIRGenerator::visitUnbox(MUnbox* unbox) {
  MDefinition* box = unbox->getOperand(0);
  MOZ_ASSERT(box->type() == MIRType::Value);

  LInstructionHelper<1, BOX_PIECES, 0>* lir;
  if (IsFloatingPointType(unbox->type())) {
    MOZ_ASSERT(unbox->type() == MIRType::Double);
    lir = new (alloc()) LUnboxFloatingPoint(useBoxAtStart(box));
  } else if (unbox->fallible()) {
    // If the unbox is fallible, load the Value in a register first to
    // avoid multiple loads.
    lir = new (alloc()) LUnbox(useRegisterAtStart(box));
  } else {
    lir = new (alloc()) LUnbox(useAtStart(box));
  }

  if (unbox->fallible()) {
    assignSnapshot(lir, unbox->bailoutKind());
  }

  define(lir, unbox);
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

  const bool needsLlScLoop = Scalar::byteSize(ins->arrayType()) < 4 &&
                             !LOONG64Flags::HasLamcasExtension();

  if (needsLlScLoop) {
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

  const bool needsLlScLoop = Scalar::byteSize(ins->arrayType()) < 4 &&
                             !LOONG64Flags::HasLamBhExtension();

  if (needsLlScLoop) {
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

  const LUse elements = useRegister(ins->elements());
  const LAllocation index =
      useRegisterOrIndexConstant(ins->index(), ins->arrayType());

  if (Scalar::isBigIntType(ins->arrayType())) {
    LInt64Allocation value = useInt64Register(ins->value());

    // Case 1: the result of the operation is not used.

    if (ins->isForEffect()) {
      auto* lir = new (alloc())
          LAtomicTypedArrayElementBinopForEffect64(elements, index, value);
      add(lir, ins);
      return;
    }

    // Case 2: the result of the operation is used.

    // LoongArch has no "AMSUB", so a temp register is needed to negate the
    // operand.
    LInt64Definition temp = ins->operation() == AtomicOp::Sub
                                ? tempInt64()
                                : LInt64Definition::BogusTemp();
    auto* lir = new (alloc())
        LAtomicTypedArrayElementBinop64(elements, index, value, temp);
    defineInt64(lir, ins);
    return;
  }

  LAllocation value = useRegister(ins->value());
  LDefinition valueTemp = LDefinition::BogusTemp();
  LDefinition offsetTemp = LDefinition::BogusTemp();
  LDefinition maskTemp = LDefinition::BogusTemp();

  const bool needsLlScLoop = Scalar::byteSize(ins->arrayType()) < 4 &&
                             !(LOONG64Flags::HasLamBhExtension() &&
                               (ins->operation() == AtomicOp::Add ||
                                ins->operation() == AtomicOp::Sub));

  if (needsLlScLoop) {
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
  // sign-extended on loongarch64 platform and we should explicitly promote it
  // to 64-bit by zero-extension when use it as an index register in memory
  // accesses.
  MOZ_ASSERT(base->type() == MIRType::Int32 || base->type() == MIRType::Int64);

  LAllocation memoryBase =
      ins->hasMemoryBase() ? LAllocation(useRegisterAtStart(ins->memoryBase()))
                           : LGeneralReg(HeapReg);

  LAllocation ptr = useRegisterOrConstantAtStart(base);

  LDefinition ptrCopy = LDefinition::BogusTemp();
  if (ins->access().offset32() && !base->isConstant()) {
    ptrCopy = tempCopy(base, 0);
  }

  if (ins->type() == MIRType::Int64) {
    auto* lir = new (alloc()) LWasmLoadI64(ptr, memoryBase, ptrCopy);
    defineInt64(lir, ins);
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

  LAllocation baseAlloc = useRegisterOrConstantAtStart(base);

  LDefinition ptrCopy = LDefinition::BogusTemp();
  if (ins->access().offset32() && !base->isConstant()) {
    ptrCopy = tempCopy(base, 0);
  }

  if (ins->access().type() == Scalar::Int64) {
    LInt64Allocation valueAlloc = useInt64RegisterOrZeroAtStart(value);
    auto* lir =
        new (alloc()) LWasmStoreI64(baseAlloc, valueAlloc, memoryBase, ptrCopy);
    add(lir, ins);
    return;
  }

  LAllocation valueAlloc = useRegisterOrZeroAtStart(value);
  auto* lir =
      new (alloc()) LWasmStore(baseAlloc, valueAlloc, memoryBase, ptrCopy);
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

  const bool needsLlScLoop =
      ins->access().byteSize() < 4 && !LOONG64Flags::HasLamcasExtension();

  if (needsLlScLoop) {
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

  const bool needsLlScLoop =
      ins->access().byteSize() < 4 && !LOONG64Flags::HasLamBhExtension();

  if (needsLlScLoop) {
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
    // LoongArch has no "AMSUB", so a temp register is needed to negate the
    // operand.
    LInt64Definition temp = ins->operation() == AtomicOp::Sub
                                ? tempInt64()
                                : LInt64Definition::BogusTemp();
    auto* lir = new (alloc()) LWasmAtomicBinopI64(
        useRegister(base), useInt64Register(ins->value()), memoryBase, temp);
    defineInt64(lir, ins);
    return;
  }

  LDefinition valueTemp = LDefinition::BogusTemp();
  LDefinition offsetTemp = LDefinition::BogusTemp();
  LDefinition maskTemp = LDefinition::BogusTemp();

  const bool needsLlScLoop =
      ins->access().byteSize() < 4 && !(LOONG64Flags::HasLamBhExtension() &&
                                        (ins->operation() == AtomicOp::Add ||
                                         ins->operation() == AtomicOp::Sub));

  if (needsLlScLoop) {
    valueTemp = temp();
    offsetTemp = temp();
    maskTemp = temp();
  }

  if (!ins->hasUses()) {
    LWasmAtomicBinopHeapForEffect* lir = new (alloc())
        LWasmAtomicBinopHeapForEffect(useRegister(base),
                                      useRegister(ins->value()), memoryBase,
                                      valueTemp, offsetTemp, maskTemp);
    add(lir, ins);
    return;
  }

  auto* lir = new (alloc())
      LWasmAtomicBinopHeap(useRegister(base), useRegister(ins->value()),
                           memoryBase, valueTemp, offsetTemp, maskTemp);

  define(lir, ins);
}

#ifdef ENABLE_JIT_SIMD
bool LIRGeneratorLOONG64::canFoldReduceSimd128AndBranch(wasm::SimdOp op) {
  switch (op) {
    case wasm::SimdOp::V128AnyTrue:
    case wasm::SimdOp::I8x16AllTrue:
    case wasm::SimdOp::I16x8AllTrue:
    case wasm::SimdOp::I32x4AllTrue:
    case wasm::SimdOp::I64x2AllTrue:
    case wasm::SimdOp::I16x8Bitmask:
      return true;
    default:
      return false;
  }
}

bool LIRGeneratorLOONG64::canEmitWasmReduceSimd128AtUses(
    MWasmReduceSimd128* ins) {
  if (!ins->canEmitAtUses()) {
    return false;
  }
  // Only specific ops generating int32.
  if (ins->type() != MIRType::Int32) {
    return false;
  }
  if (!canFoldReduceSimd128AndBranch(ins->simdOp())) {
    return false;
  }
  // If never used then defer (it will be removed).
  MUseIterator iter(ins->usesBegin());
  if (iter == ins->usesEnd()) {
    return true;
  }
  // We require an MTest consumer.
  MNode* node = iter->consumer();
  if (!node->isDefinition() || !node->toDefinition()->isTest()) {
    return false;
  }
  // Defer only if there's only one use.
  iter++;
  return iter == ins->usesEnd();
}
#endif

void LIRGenerator::visitWasmTernarySimd128(MWasmTernarySimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  MOZ_ASSERT(ins->v0()->type() == MIRType::Simd128);
  MOZ_ASSERT(ins->v1()->type() == MIRType::Simd128);
  MOZ_ASSERT(ins->v2()->type() == MIRType::Simd128);
  MOZ_ASSERT(ins->type() == MIRType::Simd128);

  switch (ins->simdOp()) {
    case wasm::SimdOp::V128Bitselect: {
      auto* lir = new (alloc())
          LWasmTernarySimd128(useRegister(ins->v0()), useRegister(ins->v1()),
                              useRegisterAtStart(ins->v2()),
                              LDefinition::BogusTemp(), ins->simdOp());
      // On Loong64, control register is used as output at machine instruction.
      defineReuseInput(lir, ins, LWasmTernarySimd128::V2Index);
      break;
    }
    case wasm::SimdOp::F32x4RelaxedMadd:
    case wasm::SimdOp::F32x4RelaxedNmadd:
    case wasm::SimdOp::F64x2RelaxedMadd:
    case wasm::SimdOp::F64x2RelaxedNmadd: {
      auto* lir = new (alloc())
          LWasmTernarySimd128(useRegister(ins->v0()), useRegister(ins->v1()),
                              useRegisterAtStart(ins->v2()),
                              LDefinition::BogusTemp(), ins->simdOp());
      defineReuseInput(lir, ins, LWasmTernarySimd128::V2Index);
      break;
    }
    case wasm::SimdOp::I32x4RelaxedDotI8x16I7x16AddS: {
      auto* lir = new (alloc()) LWasmTernarySimd128(
          useRegister(ins->v0()), useRegister(ins->v1()),
          useRegisterAtStart(ins->v2()), tempSimd128(), ins->simdOp());
      defineReuseInput(lir, ins, LWasmTernarySimd128::V2Index);
      break;
    }
    case wasm::SimdOp::I8x16RelaxedLaneSelect:
    case wasm::SimdOp::I16x8RelaxedLaneSelect:
    case wasm::SimdOp::I32x4RelaxedLaneSelect:
    case wasm::SimdOp::I64x2RelaxedLaneSelect: {
      auto* lir = new (alloc())
          LWasmTernarySimd128(useRegister(ins->v0()), useRegister(ins->v1()),
                              useRegisterAtStart(ins->v2()),
                              LDefinition::BogusTemp(), ins->simdOp());
      defineReuseInput(lir, ins, LWasmTernarySimd128::V2Index);
      break;
    }
    default:
      MOZ_CRASH("NYI");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void LIRGenerator::visitWasmBinarySimd128(MWasmBinarySimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  MDefinition* lhs = ins->lhs();
  MDefinition* rhs = ins->rhs();
  wasm::SimdOp op = ins->simdOp();

  MOZ_ASSERT(lhs->type() == MIRType::Simd128);
  MOZ_ASSERT(rhs->type() == MIRType::Simd128);
  MOZ_ASSERT(ins->type() == MIRType::Simd128);

  LAllocation lhsAlloc = useRegisterAtStart(lhs);
  LAllocation rhsAlloc = useRegisterAtStart(rhs);
  LDefinition tempReg0 = LDefinition::BogusTemp();
  LDefinition tempReg1 = LDefinition::BogusTemp();
  auto* lir = new (alloc())
      LWasmBinarySimd128(lhsAlloc, rhsAlloc, tempReg0, tempReg1, op);
  define(lir, ins);
#else
  MOZ_CRASH("No SIMD");
#endif
}

#ifdef ENABLE_JIT_SIMD
bool MWasmTernarySimd128::specializeBitselectConstantMaskAsShuffle(
    int8_t shuffle[16]) {
  if (simdOp() != wasm::SimdOp::V128Bitselect) {
    return false;
  }

  SimdConstant constant = static_cast<MWasmFloatConstant*>(v2())->toSimd128();
  const SimdConstant::I8x16& bytes = constant.asInt8x16();
  for (int8_t i = 0; i < 16; i++) {
    if (bytes[i] == -1) {
      shuffle[i] = i;
    } else if (bytes[i] == 0) {
      shuffle[i] = i + 16;
    } else {
      return false;
    }
  }
  return true;
}
bool MWasmTernarySimd128::canRelaxBitselect() { return false; }

bool MWasmBinarySimd128::canPmaddubsw() { return false; }

bool MWasmBinarySimd128::canTestBits() { return false; }

bool MWasmReplaceLaneSimd128::canMoveLane() { return true; }
#endif

bool MWasmBinarySimd128::specializeForConstantRhs() {
  // The integer compares have splat-immediate encodings; other constants are
  // loaded into the scratch register.
  switch (simdOp()) {
    case wasm::SimdOp::I8x16Eq:
    case wasm::SimdOp::I8x16Ne:
    case wasm::SimdOp::I8x16LtS:
    case wasm::SimdOp::I8x16LtU:
    case wasm::SimdOp::I8x16GtS:
    case wasm::SimdOp::I8x16GtU:
    case wasm::SimdOp::I8x16LeS:
    case wasm::SimdOp::I8x16LeU:
    case wasm::SimdOp::I8x16GeS:
    case wasm::SimdOp::I8x16GeU:
    case wasm::SimdOp::I16x8Eq:
    case wasm::SimdOp::I16x8Ne:
    case wasm::SimdOp::I16x8LtS:
    case wasm::SimdOp::I16x8LtU:
    case wasm::SimdOp::I16x8GtS:
    case wasm::SimdOp::I16x8GtU:
    case wasm::SimdOp::I16x8LeS:
    case wasm::SimdOp::I16x8LeU:
    case wasm::SimdOp::I16x8GeS:
    case wasm::SimdOp::I16x8GeU:
    case wasm::SimdOp::I32x4Eq:
    case wasm::SimdOp::I32x4Ne:
    case wasm::SimdOp::I32x4LtS:
    case wasm::SimdOp::I32x4LtU:
    case wasm::SimdOp::I32x4GtS:
    case wasm::SimdOp::I32x4GtU:
    case wasm::SimdOp::I32x4LeS:
    case wasm::SimdOp::I32x4LeU:
    case wasm::SimdOp::I32x4GeS:
    case wasm::SimdOp::I32x4GeU:
    case wasm::SimdOp::I64x2Eq:
    case wasm::SimdOp::I64x2Ne:
    case wasm::SimdOp::I64x2LtS:
    case wasm::SimdOp::I64x2GtS:
    case wasm::SimdOp::I64x2LeS:
    case wasm::SimdOp::I64x2GeS:
      return true;
    default:
      return false;
  }
}

void LIRGenerator::visitWasmBinarySimd128WithConstant(
    MWasmBinarySimd128WithConstant* ins) {
#ifdef ENABLE_JIT_SIMD
  MOZ_ASSERT(ins->lhs()->type() == MIRType::Simd128);
  MOZ_ASSERT(ins->type() == MIRType::Simd128);

  auto* lir = new (alloc()) LWasmBinarySimd128WithConstant(
      useRegisterAtStart(ins->lhs()), LDefinition::BogusTemp(), ins->rhs());
  define(lir, ins);
#else
  MOZ_CRASH("No SIMD");
#endif
}

void LIRGenerator::visitWasmShiftSimd128(MWasmShiftSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  MDefinition* lhs = ins->lhs();
  MDefinition* rhs = ins->rhs();

  MOZ_ASSERT(lhs->type() == MIRType::Simd128);
  MOZ_ASSERT(rhs->type() == MIRType::Int32);
  MOZ_ASSERT(ins->type() == MIRType::Simd128);

  if (rhs->isConstant()) {
    int32_t shiftCount = rhs->toConstant()->toInt32();
    switch (ins->simdOp()) {
      case wasm::SimdOp::I8x16Shl:
      case wasm::SimdOp::I8x16ShrU:
      case wasm::SimdOp::I8x16ShrS:
        shiftCount &= 7;
        break;
      case wasm::SimdOp::I16x8Shl:
      case wasm::SimdOp::I16x8ShrU:
      case wasm::SimdOp::I16x8ShrS:
        shiftCount &= 15;
        break;
      case wasm::SimdOp::I32x4Shl:
      case wasm::SimdOp::I32x4ShrU:
      case wasm::SimdOp::I32x4ShrS:
        shiftCount &= 31;
        break;
      case wasm::SimdOp::I64x2Shl:
      case wasm::SimdOp::I64x2ShrU:
      case wasm::SimdOp::I64x2ShrS:
        shiftCount &= 63;
        break;
      default:
        MOZ_CRASH("Unexpected shift operation");
    }
#  ifdef DEBUG
    js::wasm::ReportSimdAnalysis("shift -> constant shift");
#  endif
    auto* lir = new (alloc())
        LWasmConstantShiftSimd128(useRegisterAtStart(lhs), shiftCount);
    define(lir, ins);
    return;
  }

#  ifdef DEBUG
  js::wasm::ReportSimdAnalysis("shift -> variable shift");
#  endif

  LAllocation lhsDestAlloc = useRegisterAtStart(lhs);
  LAllocation rhsAlloc = useRegisterAtStart(rhs);
  auto* lir = new (alloc()) LWasmVariableShiftSimd128(lhsDestAlloc, rhsAlloc);
  define(lir, ins);
#else
  MOZ_CRASH("No SIMD");
#endif
}

void LIRGenerator::visitWasmShuffleSimd128(MWasmShuffleSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  MOZ_ASSERT(ins->lhs()->type() == MIRType::Simd128);
  MOZ_ASSERT(ins->rhs()->type() == MIRType::Simd128);
  MOZ_ASSERT(ins->type() == MIRType::Simd128);

  SimdShuffle s = ins->shuffle();
  switch (s.opd) {
    case SimdShuffle::Operand::LEFT:
    case SimdShuffle::Operand::RIGHT: {
      LAllocation src;
      switch (*s.permuteOp) {
        case SimdPermuteOp::MOVE:
        case SimdPermuteOp::BROADCAST_8x16:
        case SimdPermuteOp::BROADCAST_16x8:
        case SimdPermuteOp::PERMUTE_8x16:
        case SimdPermuteOp::PERMUTE_16x8:
        case SimdPermuteOp::PERMUTE_32x4:
        case SimdPermuteOp::ROTATE_RIGHT_8x16:
        case SimdPermuteOp::SHIFT_LEFT_8x16:
        case SimdPermuteOp::SHIFT_RIGHT_8x16:
        case SimdPermuteOp::REVERSE_16x8:
        case SimdPermuteOp::REVERSE_32x4:
        case SimdPermuteOp::REVERSE_64x2:
        case SimdPermuteOp::ZERO_EXTEND_8x16_TO_16x8:
        case SimdPermuteOp::ZERO_EXTEND_8x16_TO_32x4:
        case SimdPermuteOp::ZERO_EXTEND_8x16_TO_64x2:
        case SimdPermuteOp::ZERO_EXTEND_16x8_TO_32x4:
        case SimdPermuteOp::ZERO_EXTEND_16x8_TO_64x2:
        case SimdPermuteOp::ZERO_EXTEND_32x4_TO_64x2:
          break;
        default:
          MOZ_CRASH("Unexpected operator");
      }
      if (s.opd == SimdShuffle::Operand::LEFT) {
        src = useRegisterAtStart(ins->lhs());
      } else {
        src = useRegisterAtStart(ins->rhs());
      }
      auto* lir =
          new (alloc()) LWasmPermuteSimd128(src, *s.permuteOp, s.control);
      define(lir, ins);
      break;
    }
    case SimdShuffle::Operand::BOTH:
    case SimdShuffle::Operand::BOTH_SWAPPED: {
      LAllocation lhs;
      LAllocation rhs;
      if (s.opd == SimdShuffle::Operand::BOTH) {
        lhs = useRegisterAtStart(ins->lhs());
        rhs = useRegisterAtStart(ins->rhs());
      } else {
        lhs = useRegisterAtStart(ins->rhs());
        rhs = useRegisterAtStart(ins->lhs());
      }
      auto* lir =
          new (alloc()) LWasmShuffleSimd128(lhs, rhs, *s.shuffleOp, s.control);
      define(lir, ins);
      break;
    }
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void LIRGenerator::visitWasmReplaceLaneSimd128(MWasmReplaceLaneSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  MOZ_ASSERT(ins->lhs()->type() == MIRType::Simd128);
  MOZ_ASSERT(ins->type() == MIRType::Simd128);

  // Optimal code generation reuses the lhs register because the rhs scalar is
  // merged into a vector lhs.
  LAllocation lhs = useRegisterAtStart(ins->lhs());
  if (ins->rhs()->type() == MIRType::Int64) {
    auto* lir = new (alloc())
        LWasmReplaceInt64LaneSimd128(lhs, useInt64RegisterOrZero(ins->rhs()));
    defineReuseInput(lir, ins, 0);
  } else {
    LAllocation rhsAlloc = useRegisterOrZero(ins->rhs());
    auto* lir = new (alloc()) LWasmReplaceLaneSimd128(lhs, rhsAlloc);
    defineReuseInput(lir, ins, 0);
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void LIRGenerator::visitWasmMoveLaneSimd128(MWasmMoveLaneSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  MDefinition* lhs = ins->lhs();
  MDefinition* rhs = ins->rhs();
  auto* lir = new (alloc()) LWasmMoveLaneSimd128(
      useRegisterAtStart(lhs), willHaveDifferentLIRNodes(lhs, rhs)
                                   ? useRegister(rhs)
                                   : useRegisterAtStart(rhs));
  defineReuseInput(lir, ins, 0);
#else
  MOZ_CRASH("No SIMD");
#endif
}

void LIRGenerator::visitWasmScalarToSimd128(MWasmScalarToSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  MOZ_ASSERT(ins->type() == MIRType::Simd128);

  switch (ins->input()->type()) {
    case MIRType::Int64: {
      // 64-bit integer splats.
      // Load-and-(sign|zero)extend.
      auto* lir = new (alloc())
          LWasmInt64ToSimd128(useInt64RegisterAtStart(ins->input()));
      define(lir, ins);
      break;
    }
    case MIRType::Float32:
    case MIRType::Double: {
      // Floating-point splats.
      auto* lir =
          new (alloc()) LWasmScalarToSimd128(useRegisterAtStart(ins->input()));
      define(lir, ins);
      break;
    }
    default: {
      // 32-bit integer splats.
      auto* lir =
          new (alloc()) LWasmScalarToSimd128(useRegisterAtStart(ins->input()));
      define(lir, ins);
      break;
    }
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void LIRGenerator::visitWasmUnarySimd128(MWasmUnarySimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  MOZ_ASSERT(ins->input()->type() == MIRType::Simd128);
  MOZ_ASSERT(ins->type() == MIRType::Simd128);

  LDefinition tempReg = LDefinition::BogusTemp();
  switch (ins->simdOp()) {
    case wasm::SimdOp::I8x16Neg:
    case wasm::SimdOp::I16x8Neg:
    case wasm::SimdOp::I32x4Neg:
    case wasm::SimdOp::I64x2Neg:
    case wasm::SimdOp::F32x4Neg:
    case wasm::SimdOp::F64x2Neg:
    case wasm::SimdOp::F32x4Abs:
    case wasm::SimdOp::F64x2Abs:
    case wasm::SimdOp::V128Not:
    case wasm::SimdOp::F32x4Sqrt:
    case wasm::SimdOp::F64x2Sqrt:
    case wasm::SimdOp::I8x16Abs:
    case wasm::SimdOp::I16x8Abs:
    case wasm::SimdOp::I32x4Abs:
    case wasm::SimdOp::I64x2Abs:
    case wasm::SimdOp::F32x4ConvertI32x4U:
    case wasm::SimdOp::I32x4TruncSatF32x4U:
    case wasm::SimdOp::I16x8ExtendLowI8x16S:
    case wasm::SimdOp::I16x8ExtendHighI8x16S:
    case wasm::SimdOp::I16x8ExtendLowI8x16U:
    case wasm::SimdOp::I16x8ExtendHighI8x16U:
    case wasm::SimdOp::I32x4ExtendLowI16x8S:
    case wasm::SimdOp::I32x4ExtendHighI16x8S:
    case wasm::SimdOp::I32x4ExtendLowI16x8U:
    case wasm::SimdOp::I32x4ExtendHighI16x8U:
    case wasm::SimdOp::I64x2ExtendLowI32x4S:
    case wasm::SimdOp::I64x2ExtendHighI32x4S:
    case wasm::SimdOp::I64x2ExtendLowI32x4U:
    case wasm::SimdOp::I64x2ExtendHighI32x4U:
    case wasm::SimdOp::F32x4ConvertI32x4S:
    case wasm::SimdOp::F32x4Ceil:
    case wasm::SimdOp::F32x4Floor:
    case wasm::SimdOp::F32x4Trunc:
    case wasm::SimdOp::F32x4Nearest:
    case wasm::SimdOp::F64x2Ceil:
    case wasm::SimdOp::F64x2Floor:
    case wasm::SimdOp::F64x2Trunc:
    case wasm::SimdOp::F64x2Nearest:
    case wasm::SimdOp::F32x4DemoteF64x2Zero:
    case wasm::SimdOp::F64x2PromoteLowF32x4:
    case wasm::SimdOp::F64x2ConvertLowI32x4S:
    case wasm::SimdOp::F64x2ConvertLowI32x4U:
    case wasm::SimdOp::I16x8ExtaddPairwiseI8x16S:
    case wasm::SimdOp::I16x8ExtaddPairwiseI8x16U:
    case wasm::SimdOp::I32x4ExtaddPairwiseI16x8S:
    case wasm::SimdOp::I32x4ExtaddPairwiseI16x8U:
    case wasm::SimdOp::I8x16Popcnt:
    case wasm::SimdOp::I32x4RelaxedTruncF32x4S:
    case wasm::SimdOp::I32x4RelaxedTruncF32x4U:
    case wasm::SimdOp::I32x4RelaxedTruncF64x2SZero:
    case wasm::SimdOp::I32x4RelaxedTruncF64x2UZero:
      break;
    case wasm::SimdOp::I32x4TruncSatF64x2SZero:
    case wasm::SimdOp::I32x4TruncSatF64x2UZero:
    case wasm::SimdOp::I32x4TruncSatF32x4S:
      tempReg = tempSimd128();
      break;
    default:
      MOZ_CRASH("Unary SimdOp not implemented");
  }

  LUse input = useRegisterAtStart(ins->input());
  LWasmUnarySimd128* lir = new (alloc()) LWasmUnarySimd128(input, tempReg);
  define(lir, ins);
#else
  MOZ_CRASH("No SIMD");
#endif
}

void LIRGenerator::visitWasmReduceSimd128(MWasmReduceSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  if (canEmitWasmReduceSimd128AtUses(ins)) {
    emitAtUses(ins);
    return;
  }

  // See comments in LIRGenerator::visitWasmReduceSimd128() in
  // Lowering-arm64.cpp.

  if (ins->type() == MIRType::Int64) {
    auto* lir = new (alloc())
        LWasmReduceSimd128ToInt64(useRegisterAtStart(ins->input()));
    defineInt64(lir, ins);
  } else {
    auto* lir =
        new (alloc()) LWasmReduceSimd128(useRegisterAtStart(ins->input()));
    define(lir, ins);
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void LIRGenerator::visitWasmLoadLaneSimd128(MWasmLoadLaneSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  // See comments in LIRGenerator::visitWasmLoadLaneSimd128() in
  // Lowering-arm64.cpp.
  LUse base = useRegisterAtStart(ins->base());
  LUse inputUse = useRegisterAtStart(ins->value());
  LAllocation memoryBase =
      ins->hasMemoryBase() ? LAllocation(useRegisterAtStart(ins->memoryBase()))
                           : LGeneralReg(HeapReg);
  auto* lir = new (alloc()) LWasmLoadLaneSimd128(base, inputUse, memoryBase);
  define(lir, ins);
#else
  MOZ_CRASH("No SIMD");
#endif
}

void LIRGenerator::visitWasmStoreLaneSimd128(MWasmStoreLaneSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  // See comment above about the base pointer.
  LUse base = useRegisterAtStart(ins->base());
  LUse input = useRegisterAtStart(ins->value());
  LAllocation memoryBase =
      ins->hasMemoryBase() ? LAllocation(useRegisterAtStart(ins->memoryBase()))
                           : LGeneralReg(HeapReg);
  auto* lir = new (alloc()) LWasmStoreLaneSimd128(base, input, memoryBase);
  add(lir, ins);
#else
  MOZ_CRASH("No SIMD");
#endif
}
