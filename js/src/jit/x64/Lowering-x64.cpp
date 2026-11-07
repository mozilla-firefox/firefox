/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/x64/Lowering-x64.h"

#include "mozilla/CheckedInt.h"
#include "mozilla/MathAlgorithms.h"

#include <bit>

#include "jit/Lowering.h"
#include "jit/MIR-wasm.h"
#include "jit/MIR.h"
#include "jit/x64/Assembler-x64.h"

#include "jit/shared/Lowering-shared-inl.h"

using namespace js;
using namespace js::jit;

LBoxAllocation LIRGeneratorX64::useBoxFixed(MDefinition* mir, Register reg1,
                                            Register, bool useAtStart) {
  MOZ_ASSERT(mir->type() == MIRType::Value);

  ensureDefined(mir);
  return LBoxAllocation(LUse(reg1, mir->virtualRegister(), useAtStart));
}

LAllocation LIRGeneratorX64::useByteOpRegister(MDefinition* mir) {
  return useRegister(mir);
}

LAllocation LIRGeneratorX64::useByteOpRegisterAtStart(MDefinition* mir) {
  return useRegisterAtStart(mir);
}

LAllocation LIRGeneratorX64::useByteOpRegisterOrNonDoubleConstant(
    MDefinition* mir) {
  return useRegisterOrNonDoubleConstant(mir);
}

LDefinition LIRGeneratorX64::tempByteOpRegister() { return temp(); }

LDefinition LIRGeneratorX64::tempToUnbox() { return temp(); }

void LIRGeneratorX64::lowerForALUInt64(
    LInstructionHelper<INT64_PIECES, INT64_PIECES, 0>* ins, MDefinition* mir,
    MDefinition* input) {
  ins->setInt64Operand(0, useInt64RegisterAtStart(input));
  defineInt64ReuseInput(ins, mir, 0);
}

void LIRGeneratorX64::lowerForALUInt64(
    LInstructionHelper<INT64_PIECES, 2 * INT64_PIECES, 0>* ins,
    MDefinition* mir, MDefinition* lhs, MDefinition* rhs) {
  ins->setInt64Operand(0, useInt64RegisterAtStart(lhs));
  ins->setInt64Operand(INT64_PIECES, willHaveDifferentLIRNodes(lhs, rhs)
                                         ? useInt64OrConstant(rhs)
                                         : useInt64OrConstantAtStart(rhs));
  defineInt64ReuseInput(ins, mir, 0);
}

void LIRGeneratorX64::lowerForMulInt64(LMulI64* ins, MMul* mir,
                                       MDefinition* lhs, MDefinition* rhs) {
  // No input reuse needed when we can use imulq with an int32 immediate.
  bool reuseInput = true;
  if (rhs->isConstant()) {
    int64_t constant = rhs->toConstant()->toInt64();
    reuseInput = int32_t(constant) != constant;
  }

  ins->setLhs(useInt64RegisterAtStart(lhs));
  ins->setRhs(willHaveDifferentLIRNodes(lhs, rhs)
                  ? useInt64OrConstant(rhs)
                  : useInt64OrConstantAtStart(rhs));
  if (reuseInput) {
    defineInt64ReuseInput(ins, mir, 0);
  } else {
    defineInt64(ins, mir);
  }
}

template <class LInstr>
void LIRGeneratorX64::lowerForShiftInt64(LInstr* ins, MDefinition* mir,
                                         MDefinition* lhs, MDefinition* rhs) {
  if constexpr (std::is_same_v<LInstr, LShiftI64>) {
    LAllocation rhsAlloc;
    if (rhs->isConstant()) {
      rhsAlloc = useOrConstantAtStart(rhs);
    } else if (Assembler::HasBMI2()) {
      rhsAlloc = useRegisterAtStart(rhs);
    } else {
      rhsAlloc = useShiftRegister(rhs);
    }

    ins->setLhs(useInt64RegisterAtStart(lhs));
    ins->setRhs(rhsAlloc);
    if (rhs->isConstant() || !Assembler::HasBMI2()) {
      defineInt64ReuseInput(ins, mir, LShiftI64::LhsIndex);
    } else {
      defineInt64(ins, mir);
    }
  } else {
    LAllocation rhsAlloc;
    if (rhs->isConstant()) {
      rhsAlloc = useOrConstantAtStart(rhs);
    } else {
      rhsAlloc = useFixed(rhs, rcx);
    }

    ins->setInput(useInt64RegisterAtStart(lhs));
    ins->setCount(rhsAlloc);
    defineInt64ReuseInput(ins, mir, LRotateI64::InputIndex);
  }
}

template void LIRGeneratorX64::lowerForShiftInt64(LShiftI64* ins,
                                                  MDefinition* mir,
                                                  MDefinition* lhs,
                                                  MDefinition* rhs);
template void LIRGeneratorX64::lowerForShiftInt64(LRotateI64* ins,
                                                  MDefinition* mir,
                                                  MDefinition* lhs,
                                                  MDefinition* rhs);

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
  } else {
    lir = new (alloc()) LUnbox(useAtStart(box));
  }

  if (unbox->fallible()) {
    assignSnapshot(lir, unbox->bailoutKind());
  }

  define(lir, unbox);
}

void LIRGeneratorX64::lowerUntypedPhiInput(MPhi* phi, uint32_t inputPosition,
                                           LBlock* block, size_t lirIndex) {
  lowerTypedPhiInput(phi, inputPosition, block, lirIndex);
}

void LIRGeneratorX64::defineInt64Phi(MPhi* phi, size_t lirIndex) {
  defineTypedPhi(phi, lirIndex);
}

void LIRGeneratorX64::lowerInt64PhiInput(MPhi* phi, uint32_t inputPosition,
                                         LBlock* block, size_t lirIndex) {
  lowerTypedPhiInput(phi, inputPosition, block, lirIndex);
}

void LIRGenerator::visitCompareExchangeTypedArrayElement(
    MCompareExchangeTypedArrayElement* ins) {
  MOZ_ASSERT(ins->elements()->type() == MIRType::Elements);
  MOZ_ASSERT(ins->index()->type() == MIRType::IntPtr);

  if (Scalar::isBigIntType(ins->arrayType())) {
    LUse elements = useRegister(ins->elements());
    LAllocation index =
        useRegisterOrIndexConstant(ins->index(), ins->arrayType());
    LInt64Allocation oldval = useInt64Register(ins->oldval());
    LInt64Allocation newval = useInt64Register(ins->newval());

    auto* lir = new (alloc())
        LCompareExchangeTypedArrayElement64(elements, index, oldval, newval);
    defineInt64Fixed(lir, ins, LInt64Allocation(LAllocation(AnyRegister(rax))));
    return;
  }

  lowerCompareExchangeTypedArrayElement(ins,
                                        /* useI386ByteRegisters = */ false);
}

void LIRGenerator::visitAtomicExchangeTypedArrayElement(
    MAtomicExchangeTypedArrayElement* ins) {
  MOZ_ASSERT(ins->elements()->type() == MIRType::Elements);
  MOZ_ASSERT(ins->index()->type() == MIRType::IntPtr);

  if (Scalar::isBigIntType(ins->arrayType())) {
    LUse elements = useRegister(ins->elements());
    LAllocation index =
        useRegisterOrIndexConstant(ins->index(), ins->arrayType());
    LInt64Allocation value = useInt64Register(ins->value());

    auto* lir = new (alloc())
        LAtomicExchangeTypedArrayElement64(elements, index, value);
    defineInt64(lir, ins);
    return;
  }

  lowerAtomicExchangeTypedArrayElement(ins, /* useI386ByteRegisters = */ false);
}

void LIRGenerator::visitAtomicTypedArrayElementBinop(
    MAtomicTypedArrayElementBinop* ins) {
  MOZ_ASSERT(ins->elements()->type() == MIRType::Elements);
  MOZ_ASSERT(ins->index()->type() == MIRType::IntPtr);

  if (Scalar::isBigIntType(ins->arrayType())) {
    LUse elements = useRegister(ins->elements());
    LAllocation index =
        useRegisterOrIndexConstant(ins->index(), ins->arrayType());

    // Case 1: the result of the operation is not used.

    if (ins->isForEffect()) {
      LInt64Allocation value = useInt64Register(ins->value());

      auto* lir = new (alloc())
          LAtomicTypedArrayElementBinopForEffect64(elements, index, value);
      add(lir, ins);
      return;
    }

    // Case 2: the result of the operation is used.
    //
    // For ADD and SUB we'll use XADD.
    //
    // For AND/OR/XOR we need to use a CMPXCHG loop with rax as the output.

    bool bitOp = !(ins->operation() == AtomicOp::Add ||
                   ins->operation() == AtomicOp::Sub);

    LInt64Allocation value;
    LInt64Definition temp;
    if (bitOp) {
      value = useInt64Register(ins->value());
      temp = tempInt64();
    } else {
      value = useInt64RegisterAtStart(ins->value());
      temp = LInt64Definition::BogusTemp();
    }

    auto* lir = new (alloc())
        LAtomicTypedArrayElementBinop64(elements, index, value, temp);
    if (bitOp) {
      defineInt64Fixed(lir, ins,
                       LInt64Allocation(LAllocation(AnyRegister(rax))));
    } else {
      defineInt64ReuseInput(lir, ins, 2);
    }
    return;
  }

  lowerAtomicTypedArrayElementBinop(ins, /* useI386ByteRegisters = */ false);
}

void LIRGeneratorX64::lowerAtomicLoad64(MLoadUnboxedScalar* ins) {
  const LUse elements = useRegister(ins->elements());
  const LAllocation index =
      useRegisterOrIndexConstant(ins->index(), ins->storageType());

  auto* lir = new (alloc()) LAtomicLoad64(elements, index);
  defineInt64(lir, ins);
}

void LIRGeneratorX64::lowerAtomicStore64(MStoreUnboxedScalar* ins) {
  LUse elements = useRegister(ins->elements());
  LAllocation index =
      useRegisterOrIndexConstant(ins->index(), ins->writeType());
  LInt64Allocation value = useInt64Register(ins->value());

  add(new (alloc()) LAtomicStore64(elements, index, value), ins);
}

void LIRGenerator::visitWasmLoad(MWasmLoad* ins) {
  MDefinition* base = ins->base();
  // 'base' is a GPR but may be of either type.  If it is 32-bit it is
  // zero-extended and can act as 64-bit.
  MOZ_ASSERT(base->type() == MIRType::Int32 || base->type() == MIRType::Int64);

  LAllocation memoryBase =
      ins->hasMemoryBase() ? LAllocation(useRegisterAtStart(ins->memoryBase()))
                           : LGeneralReg(HeapReg);

  if (ins->type() != MIRType::Int64) {
    auto* lir =
        new (alloc()) LWasmLoad(useRegisterOrZeroAtStart(base), memoryBase);
    define(lir, ins);
    return;
  }

  auto* lir =
      new (alloc()) LWasmLoadI64(useRegisterOrZeroAtStart(base), memoryBase);
  defineInt64(lir, ins);
}

static bool CanUseInt32OrInt64Constant(MDefinition* value) {
  MOZ_ASSERT(IsIntType(value->type()));
  if (!value->isConstant()) {
    return false;
  }
  if (value->type() == MIRType::Int64) {
    // Immediate needs to fit into int32 for direct to memory move on x64.
    return mozilla::CheckedInt32(value->toConstant()->toInt64()).isValid();
  }
  MOZ_ASSERT(value->type() == MIRType::Int32);
  return true;
}

bool LIRGeneratorX64::canStoreConstantWithoutRegister(MDefinition* value) {
  return IsIntType(value->type()) && CanUseInt32OrInt64Constant(value);
}

void LIRGenerator::visitWasmStore(MWasmStore* ins) {
  MDefinition* base = ins->base();
  // See comment in visitWasmLoad re the type of 'base'.
  MOZ_ASSERT(base->type() == MIRType::Int32 || base->type() == MIRType::Int64);

  MDefinition* value = ins->value();
  LAllocation valueAlloc;
  switch (ins->access().type()) {
    case Scalar::Int8:
    case Scalar::Uint8:
    case Scalar::Int16:
    case Scalar::Uint16:
    case Scalar::Int32:
    case Scalar::Uint32:
      valueAlloc = useRegisterOrConstantAtStart(value);
      break;
    case Scalar::Int64:
      if (CanUseInt32OrInt64Constant(value)) {
        valueAlloc = useOrConstantAtStart(value);
      } else {
        valueAlloc = useRegisterAtStart(value);
      }
      break;
    case Scalar::Float32:
    case Scalar::Float64:
      valueAlloc = useRegisterAtStart(value);
      break;
    case Scalar::Simd128:
#ifdef ENABLE_JIT_SIMD
      valueAlloc = useRegisterAtStart(value);
      break;
#else
      MOZ_CRASH("unexpected array type");
#endif
    case Scalar::BigInt64:
    case Scalar::BigUint64:
    case Scalar::Uint8Clamped:
    case Scalar::Float16:
    case Scalar::MaxTypedArrayViewType:
      MOZ_CRASH("unexpected array type");
  }

  LAllocation baseAlloc = useRegisterOrZeroAtStart(base);
  LAllocation memoryBaseAlloc =
      ins->hasMemoryBase() ? LAllocation(useRegisterAtStart(ins->memoryBase()))
                           : LGeneralReg(HeapReg);
  auto* lir = new (alloc()) LWasmStore(baseAlloc, valueAlloc, memoryBaseAlloc);
  add(lir, ins);
}

void LIRGenerator::visitWasmCompareExchangeHeap(MWasmCompareExchangeHeap* ins) {
  MDefinition* base = ins->base();
  // See comment in visitWasmLoad re the type of 'base'.
  MOZ_ASSERT(base->type() == MIRType::Int32 || base->type() == MIRType::Int64);

  // The output may not be used but will be clobbered regardless, so
  // pin the output to eax.
  //
  // The input values must both be in registers.

  const LAllocation oldval = useRegister(ins->oldValue());
  const LAllocation newval = useRegister(ins->newValue());
  const LAllocation memoryBase =
      ins->hasMemoryBase() ? LAllocation(useRegister(ins->memoryBase()))
                           : LGeneralReg(HeapReg);

  LWasmCompareExchangeHeap* lir = new (alloc())
      LWasmCompareExchangeHeap(useRegister(base), oldval, newval, memoryBase);

  defineFixed(lir, ins, LAllocation(AnyRegister(eax)));
}

void LIRGenerator::visitWasmAtomicExchangeHeap(MWasmAtomicExchangeHeap* ins) {
  // See comment in visitWasmLoad re the type of 'base'.
  MOZ_ASSERT(ins->base()->type() == MIRType::Int32 ||
             ins->base()->type() == MIRType::Int64);

  const LAllocation base = useRegister(ins->base());
  const LAllocation value = useRegister(ins->value());
  const LAllocation memoryBase =
      ins->hasMemoryBase() ? LAllocation(useRegister(ins->memoryBase()))
                           : LGeneralReg(HeapReg);

  // The output may not be used but will be clobbered regardless,
  // so ignore the case where we're not using the value and just
  // use the output register as a temp.

  LWasmAtomicExchangeHeap* lir =
      new (alloc()) LWasmAtomicExchangeHeap(base, value, memoryBase);
  define(lir, ins);
}

void LIRGenerator::visitWasmAtomicBinopHeap(MWasmAtomicBinopHeap* ins) {
  MDefinition* base = ins->base();
  // See comment in visitWasmLoad re the type of 'base'.
  MOZ_ASSERT(base->type() == MIRType::Int32 || base->type() == MIRType::Int64);

  const LAllocation memoryBase =
      ins->hasMemoryBase() ? LAllocation(useRegister(ins->memoryBase()))
                           : LGeneralReg(HeapReg);

  // No support for 64-bit operations with constants at the masm level.

  bool canTakeConstant = ins->access().type() != Scalar::Int64;

  // Case 1: the result of the operation is not used.
  //
  // We'll emit a single instruction: LOCK ADD, LOCK SUB, LOCK AND,
  // LOCK OR, or LOCK XOR.

  if (!ins->hasUses()) {
    LAllocation value = canTakeConstant ? useRegisterOrConstant(ins->value())
                                        : useRegister(ins->value());
    auto* lir = new (alloc())
        LWasmAtomicBinopHeapForEffect(useRegister(base), value, memoryBase);
    add(lir, ins);
    return;
  }

  // Case 2: the result of the operation is used.
  //
  // For ADD and SUB we'll use XADD with word and byte ops as
  // appropriate.  Any output register can be used and if value is a
  // register it's best if it's the same as output:
  //
  //    movl       value, output  ; if value != output
  //    lock xaddl output, mem
  //
  // For AND/OR/XOR we need to use a CMPXCHG loop, and the output is
  // always in rax:
  //
  //    movl          *mem, rax
  // L: mov           rax, temp
  //    andl          value, temp
  //    lock cmpxchg  temp, mem  ; reads rax also
  //    jnz           L
  //    ; result in rax
  //
  // Note the placement of L, cmpxchg will update rax with *mem if
  // *mem does not have the expected value, so reloading it at the
  // top of the loop would be redundant.

  bool bitOp =
      !(ins->operation() == AtomicOp::Add || ins->operation() == AtomicOp::Sub);
  bool reuseInput = false;
  LAllocation value;

  if (bitOp || ins->value()->isConstant()) {
    value = canTakeConstant ? useRegisterOrConstant(ins->value())
                            : useRegister(ins->value());
  } else {
    reuseInput = true;
    value = useRegisterAtStart(ins->value());
  }

  auto* lir = new (alloc())
      LWasmAtomicBinopHeap(useRegister(base), value, memoryBase,
                           bitOp ? temp() : LDefinition::BogusTemp());

  if (reuseInput) {
    defineReuseInput(lir, ins, LWasmAtomicBinopHeap::ValueIndex);
  } else if (bitOp) {
    defineFixed(lir, ins, LAllocation(AnyRegister(rax)));
  } else {
    define(lir, ins);
  }
}

void LIRGenerator::visitSubstr(MSubstr* ins) {
  LSubstr* lir = new (alloc())
      LSubstr(useRegister(ins->string()), useRegister(ins->begin()),
              useRegister(ins->length()), temp(), temp(), tempByteOpRegister());
  define(lir, ins);
  assignSafepoint(lir, ins);
}

void LIRGeneratorX64::lowerDivI64(MDiv* div) {
  // Division instructions are slow. Division by constant denominators can be
  // rewritten to use other instructions.
  if (div->rhs()->isConstant()) {
    int64_t rhs = div->rhs()->toConstant()->toInt64();

    // Division by powers of two can be done by shifting, and division by
    // other numbers can be done by a reciprocal multiplication technique.
    if (std::has_single_bit(mozilla::Abs(rhs))) {
      int32_t shift = mozilla::FloorLog2(mozilla::Abs(rhs));
      LAllocation lhs = useRegisterAtStart(div->lhs());

      // We have to round the result toward 0 when the remainder is non-zero.
      // This requires an extra register to round up/down when the left-hand
      // side is signed.
      LAllocation lhsCopy = div->canBeNegativeDividend()
                                ? useRegister(div->lhs())
                                : LAllocation();

      auto* lir = new (alloc()) LDivPowTwoI64(lhs, lhsCopy, shift, rhs < 0);
      defineReuseInput(lir, div, 0);
      return;
    }

    auto* lir = new (alloc())
        LDivConstantI64(useRegister(div->lhs()), tempFixed(rax), rhs);
    defineFixed(lir, div, LAllocation(AnyRegister(rdx)));
    return;
  }

  auto* lir = new (alloc()) LDivI64(useFixedAtStart(div->lhs(), rax),
                                    useRegister(div->rhs()), tempFixed(rdx));
  defineFixed(lir, div, LAllocation(AnyRegister(rax)));
}

void LIRGeneratorX64::lowerModI64(MMod* mod) {
  if (mod->rhs()->isConstant()) {
    int64_t rhs = mod->rhs()->toConstant()->toInt64();

    if (std::has_single_bit(mozilla::Abs(rhs))) {
      int32_t shift = mozilla::FloorLog2(mozilla::Abs(rhs));

      auto* lir =
          new (alloc()) LModPowTwoI64(useRegisterAtStart(mod->lhs()), shift);
      defineReuseInput(lir, mod, 0);
      return;
    }

    auto* lir = new (alloc())
        LModConstantI64(useRegister(mod->lhs()), tempFixed(rdx), rhs);
    defineFixed(lir, mod, LAllocation(AnyRegister(rax)));
    return;
  }

  auto* lir = new (alloc()) LModI64(useFixedAtStart(mod->lhs(), rax),
                                    useRegister(mod->rhs()), tempFixed(rax));
  defineFixed(lir, mod, LAllocation(AnyRegister(rdx)));
}

void LIRGeneratorX64::lowerUDivI64(MDiv* div) {
  if (div->rhs()->isConstant()) {
    // NOTE: the result of toInt64 is coerced to uint64_t.
    uint64_t rhs = div->rhs()->toConstant()->toInt64();

    if (std::has_single_bit(rhs)) {
      int32_t shift = mozilla::FloorLog2(rhs);

      auto* lir = new (alloc()) LDivPowTwoI64(useRegisterAtStart(div->lhs()),
                                              LAllocation(), shift, false);
      defineReuseInput(lir, div, 0);
      return;
    }

    auto* lir = new (alloc())
        LUDivConstantI64(useRegister(div->lhs()), tempFixed(rax), rhs);
    defineFixed(lir, div, LAllocation(AnyRegister(rdx)));
    return;
  }

  auto* lir = new (alloc()) LUDivI64(useFixedAtStart(div->lhs(), rax),
                                     useRegister(div->rhs()), tempFixed(rdx));
  defineFixed(lir, div, LAllocation(AnyRegister(rax)));
}

void LIRGeneratorX64::lowerUModI64(MMod* mod) {
  if (mod->rhs()->isConstant()) {
    // NOTE: the result of toInt64 is coerced to uint64_t.
    uint64_t rhs = mod->rhs()->toConstant()->toInt64();

    if (std::has_single_bit(rhs)) {
      int32_t shift = mozilla::FloorLog2(rhs);

      auto* lir =
          new (alloc()) LModPowTwoI64(useRegisterAtStart(mod->lhs()), shift);
      defineReuseInput(lir, mod, 0);
      return;
    }

    auto* lir = new (alloc())
        LUModConstantI64(useRegister(mod->lhs()), tempFixed(rdx), rhs);
    defineFixed(lir, mod, LAllocation(AnyRegister(rax)));
    return;
  }

  auto* lir = new (alloc()) LUModI64(useFixedAtStart(mod->lhs(), rax),
                                     useRegister(mod->rhs()), tempFixed(rax));
  defineFixed(lir, mod, LAllocation(AnyRegister(rdx)));
}

void LIRGeneratorX64::lowerWasmBuiltinDivI64(MWasmBuiltinDivI64* div) {
  MOZ_CRASH("We don't use runtime div for this architecture");
}

void LIRGeneratorX64::lowerWasmBuiltinModI64(MWasmBuiltinModI64* mod) {
  MOZ_CRASH("We don't use runtime mod for this architecture");
}

void LIRGeneratorX64::lowerBigIntPtrDiv(MBigIntPtrDiv* ins) {
  auto* lir = new (alloc())
      LBigIntPtrDiv(useRegister(ins->lhs()), useRegister(ins->rhs()),
                    tempFixed(rdx), LDefinition::BogusTemp());
  assignSnapshot(lir, ins->bailoutKind());
  defineFixed(lir, ins, LAllocation(AnyRegister(rax)));
}

void LIRGeneratorX64::lowerBigIntPtrMod(MBigIntPtrMod* ins) {
  auto* lir = new (alloc())
      LBigIntPtrMod(useRegister(ins->lhs()), useRegister(ins->rhs()),
                    tempFixed(rax), LDefinition::BogusTemp());
  if (ins->canBeDivideByZero()) {
    assignSnapshot(lir, ins->bailoutKind());
  }
  defineFixed(lir, ins, LAllocation(AnyRegister(rdx)));
}

void LIRGeneratorX64::lowerTruncateDToInt32(MTruncateToInt32* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Double);

  define(new (alloc()) LTruncateDToInt32(useRegister(opd), tempShift()), ins);
}

void LIRGeneratorX64::lowerTruncateFToInt32(MTruncateToInt32* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Float32);

  LDefinition maybeTemp = LDefinition::BogusTemp();
  define(new (alloc()) LTruncateFToInt32(useRegister(opd), maybeTemp), ins);
}

void LIRGenerator::visitWasmTruncateToInt64(MWasmTruncateToInt64* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Double || opd->type() == MIRType::Float32);

  LDefinition maybeTemp =
      ins->isUnsigned() ? tempDouble() : LDefinition::BogusTemp();
  defineInt64(new (alloc()) LWasmTruncateToInt64(useRegister(opd), maybeTemp),
              ins);
}

void LIRGeneratorX64::lowerWasmBuiltinTruncateToInt64(
    MWasmBuiltinTruncateToInt64* ins) {
  MOZ_CRASH("We don't use it for this architecture");
}

void LIRGenerator::visitInt64ToFloatingPoint(MInt64ToFloatingPoint* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Int64);
  MOZ_ASSERT(IsFloatingPointType(ins->type()));

  LDefinition maybeTemp = ins->isUnsigned() ? temp() : LDefinition::BogusTemp();
  define(new (alloc()) LInt64ToFloatingPoint(useInt64Register(opd), maybeTemp),
         ins);
}

void LIRGeneratorX64::lowerWasmBuiltinTruncateToInt32(
    MWasmBuiltinTruncateToInt32* ins) {
  MDefinition* opd = ins->input();
  MOZ_ASSERT(opd->type() == MIRType::Double || opd->type() == MIRType::Float32);

  if (opd->type() == MIRType::Double) {
    define(new (alloc()) LWasmBuiltinTruncateDToInt32(
               useRegister(opd), LAllocation(), tempShift()),
           ins);
    return;
  }

  LDefinition maybeTemp = LDefinition::BogusTemp();
  define(new (alloc()) LWasmBuiltinTruncateFToInt32(useRegister(opd),
                                                    LAllocation(), maybeTemp),
         ins);
}

void LIRGeneratorX64::lowerBuiltinInt64ToFloatingPoint(
    MBuiltinInt64ToFloatingPoint* ins) {
  MOZ_CRASH("We don't use it for this architecture");
}

void LIRGenerator::visitExtendInt32ToInt64(MExtendInt32ToInt64* ins) {
  defineInt64(new (alloc()) LExtendInt32ToInt64(useAtStart(ins->input())), ins);
}

void LIRGenerator::visitSignExtendInt64(MSignExtendInt64* ins) {
  defineInt64(new (alloc())
                  LSignExtendInt64(useInt64RegisterAtStart(ins->input())),
              ins);
}

// On x64 we specialize the cases: compare is {U,}Int{32,64}, and select is
// {U,}Int{32,64}, independently.
bool LIRGeneratorShared::canSpecializeWasmCompareAndSelect(
    MCompare::CompareType compTy, MIRType insTy) {
  return (insTy == MIRType::Int32 || insTy == MIRType::Int64) &&
         (compTy == MCompare::Compare_Int32 ||
          compTy == MCompare::Compare_UInt32 ||
          compTy == MCompare::Compare_Int64 ||
          compTy == MCompare::Compare_UInt64);
}

void LIRGeneratorShared::lowerWasmCompareAndSelect(MWasmSelect* ins,
                                                   MDefinition* lhs,
                                                   MDefinition* rhs,
                                                   MCompare::CompareType compTy,
                                                   JSOp jsop) {
  MOZ_ASSERT(canSpecializeWasmCompareAndSelect(compTy, ins->type()));
  auto* lir = new (alloc()) LWasmCompareAndSelect(
      useRegister(lhs), useAny(rhs), useRegisterAtStart(ins->trueExpr()),
      useAny(ins->falseExpr()), compTy, jsop);
  defineReuseInput(lir, ins, LWasmCompareAndSelect::IfTrueExprIndex);
}
