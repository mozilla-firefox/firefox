/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/MIR-wasm.h"

#include "mozilla/ScopeExit.h"

#include "jit/MIRGraph.h"
#include "js/Conversions.h"
#include "wasm/WasmCode.h"
#include "wasm/WasmFeatures.h"  // for wasm::ReportSimdAnalysis

#include "wasm/WasmInstance-inl.h"

using namespace js;
using namespace js::jit;

using JS::ToInt32;

using mozilla::CheckedInt;
using mozilla::IsFloat32Representable;

MInstruction* jit::NewWasmDefaultConstant(TempAllocator& alloc,
                                          wasm::ValType type) {
  MOZ_ASSERT(type.isDefaultable());
  switch (type.kind()) {
    case wasm::ValType::I32:
      return MConstant::NewInt32(alloc, 0);
    case wasm::ValType::I64:
      return MConstant::NewInt64(alloc, 0);
    case wasm::ValType::F32:
      return MWasmFloatConstant::NewFloat32(alloc, 0.0);
    case wasm::ValType::F64:
      return MWasmFloatConstant::NewDouble(alloc, 0.0);
    case wasm::ValType::V128:
#ifdef ENABLE_JIT_SIMD
      return MWasmFloatConstant::NewSimd128(alloc, SimdConstant::Zero());
#else
      MOZ_CRASH();
#endif
    case wasm::ValType::Ref:
      MOZ_ASSERT(type.isNullable());
      return MWasmNullConstant::New(alloc, wasm::MaybeRefType(type.refType()));
    default:
      MOZ_CRASH("NYI");
  }
}

HashNumber MWasmFloatConstant::valueHash() const {
#ifdef ENABLE_JIT_SIMD
  return ConstantValueHash(type(), u.bits_[0] ^ u.bits_[1]);
#else
  return ConstantValueHash(type(), u.bits_[0]);
#endif
}

bool MWasmFloatConstant::congruentTo(const MDefinition* ins) const {
  return ins->isWasmFloatConstant() && type() == ins->type() &&
#ifdef ENABLE_JIT_SIMD
         u.bits_[1] == ins->toWasmFloatConstant()->u.bits_[1] &&
#endif
         u.bits_[0] == ins->toWasmFloatConstant()->u.bits_[0];
}

HashNumber MWasmNullConstant::valueHash() const {
  return ConstantValueHash(MIRType::WasmAnyRef, 0);
}

MDefinition* MWasmTruncateToInt32::foldsTo(TempAllocator& alloc) {
  MDefinition* input = getOperand(0);
  if (input->type() == MIRType::Int32) {
    return input;
  }

  if (input->type() == MIRType::Double && input->isConstant()) {
    double d = input->toConstant()->toDouble();
    if (std::isnan(d)) {
      return this;
    }

    if (!isUnsigned() && d <= double(INT32_MAX) && d >= double(INT32_MIN)) {
      return MConstant::NewInt32(alloc, ToInt32(d));
    }

    if (isUnsigned() && d <= double(UINT32_MAX) && d >= 0) {
      return MConstant::NewInt32(alloc, ToInt32(d));
    }
  }

  if (input->type() == MIRType::Float32 && input->isConstant()) {
    double f = double(input->toConstant()->toFloat32());
    if (std::isnan(f)) {
      return this;
    }

    if (!isUnsigned() && f <= double(INT32_MAX) && f >= double(INT32_MIN)) {
      return MConstant::NewInt32(alloc, ToInt32(f));
    }

    if (isUnsigned() && f <= double(UINT32_MAX) && f >= 0) {
      return MConstant::NewInt32(alloc, ToInt32(f));
    }
  }

  return this;
}

MDefinition* MWasmExtendU32Index::foldsTo(TempAllocator& alloc) {
  MDefinition* input = this->input();
  if (input->isConstant()) {
    return MConstant::NewInt64(
        alloc, int64_t(uint32_t(input->toConstant()->toInt32())));
  }

  return this;
}

MDefinition* MWasmWrapU32Index::foldsTo(TempAllocator& alloc) {
  MDefinition* input = this->input();
  if (input->isConstant()) {
    return MConstant::NewInt32(
        alloc, int32_t(uint32_t(input->toConstant()->toInt64())));
  }

  return this;
}

// Some helpers for folding wasm and/or/xor on int32/64 values.  Rather than
// duplicating these for 32 and 64-bit values, all folding is done on 64-bit
// values and masked for the 32-bit case.

const uint64_t Low32Mask = uint64_t(0xFFFFFFFFULL);

// Routines to check and disassemble values.

static bool IsIntegralConstant(const MDefinition* def) {
  return def->isConstant() &&
         (def->type() == MIRType::Int32 || def->type() == MIRType::Int64);
}

static uint64_t GetIntegralConstant(const MDefinition* def) {
  if (def->type() == MIRType::Int32) {
    return uint64_t(def->toConstant()->toInt32()) & Low32Mask;
  }
  return uint64_t(def->toConstant()->toInt64());
}

static bool IsIntegralConstantZero(const MDefinition* def) {
  return IsIntegralConstant(def) && GetIntegralConstant(def) == 0;
}

static bool IsIntegralConstantOnes(const MDefinition* def) {
  uint64_t ones = def->type() == MIRType::Int32 ? Low32Mask : ~uint64_t(0);
  return IsIntegralConstant(def) && GetIntegralConstant(def) == ones;
}

// Routines to create values.
static MDefinition* ToIntegralConstant(TempAllocator& alloc, MIRType ty,
                                       uint64_t val) {
  switch (ty) {
    case MIRType::Int32:
      return MConstant::NewInt32(alloc, int32_t(uint32_t(val & Low32Mask)));
    case MIRType::Int64:
      return MConstant::NewInt64(alloc, int64_t(val));
    default:
      MOZ_CRASH();
  }
}

static MDefinition* ZeroOfType(TempAllocator& alloc, MIRType ty) {
  return ToIntegralConstant(alloc, ty, 0);
}

static MDefinition* OnesOfType(TempAllocator& alloc, MIRType ty) {
  return ToIntegralConstant(alloc, ty, ~uint64_t(0));
}

MDefinition* MWasmBinaryBitwise::foldsTo(TempAllocator& alloc) {
  MOZ_ASSERT(op() == Opcode::WasmBinaryBitwise);
  MOZ_ASSERT(type() == MIRType::Int32 || type() == MIRType::Int64);

  MDefinition* argL = getOperand(0);
  MDefinition* argR = getOperand(1);
  MOZ_ASSERT(argL->type() == type() && argR->type() == type());

  // The args are the same (SSA name)
  if (argL == argR) {
    switch (subOpcode()) {
      case SubOpcode::And:
      case SubOpcode::Or:
        return argL;
      case SubOpcode::Xor:
        return ZeroOfType(alloc, type());
      default:
        MOZ_CRASH();
    }
  }

  // Both args constant
  if (IsIntegralConstant(argL) && IsIntegralConstant(argR)) {
    uint64_t valL = GetIntegralConstant(argL);
    uint64_t valR = GetIntegralConstant(argR);
    uint64_t val = valL;
    switch (subOpcode()) {
      case SubOpcode::And:
        val &= valR;
        break;
      case SubOpcode::Or:
        val |= valR;
        break;
      case SubOpcode::Xor:
        val ^= valR;
        break;
      default:
        MOZ_CRASH();
    }
    return ToIntegralConstant(alloc, type(), val);
  }

  // Left arg is zero
  if (IsIntegralConstantZero(argL)) {
    switch (subOpcode()) {
      case SubOpcode::And:
        return ZeroOfType(alloc, type());
      case SubOpcode::Or:
      case SubOpcode::Xor:
        return argR;
      default:
        MOZ_CRASH();
    }
  }

  // Right arg is zero
  if (IsIntegralConstantZero(argR)) {
    switch (subOpcode()) {
      case SubOpcode::And:
        return ZeroOfType(alloc, type());
      case SubOpcode::Or:
      case SubOpcode::Xor:
        return argL;
      default:
        MOZ_CRASH();
    }
  }

  // Left arg is ones
  if (IsIntegralConstantOnes(argL)) {
    switch (subOpcode()) {
      case SubOpcode::And:
        return argR;
      case SubOpcode::Or:
        return OnesOfType(alloc, type());
      case SubOpcode::Xor:
        return MBitNot::New(alloc, argR, type());
      default:
        MOZ_CRASH();
    }
  }

  // Right arg is ones
  if (IsIntegralConstantOnes(argR)) {
    switch (subOpcode()) {
      case SubOpcode::And:
        return argL;
      case SubOpcode::Or:
        return OnesOfType(alloc, type());
      case SubOpcode::Xor:
        return MBitNot::New(alloc, argL, type());
      default:
        MOZ_CRASH();
    }
  }

  return this;
}

MDefinition* MWasmAddOffset::foldsTo(TempAllocator& alloc) {
  MDefinition* baseArg = base();
  if (!baseArg->isConstant()) {
    return this;
  }

  if (baseArg->type() == MIRType::Int32) {
    CheckedInt<uint32_t> ptr = baseArg->toConstant()->toInt32();
    ptr += offset();
    if (!ptr.isValid()) {
      return this;
    }
    return MConstant::NewInt32(alloc, ptr.value());
  }

  MOZ_ASSERT(baseArg->type() == MIRType::Int64);
  CheckedInt<uint64_t> ptr = baseArg->toConstant()->toInt64();
  ptr += offset();
  if (!ptr.isValid()) {
    return this;
  }
  return MConstant::NewInt64(alloc, ptr.value());
}

bool MWasmAlignmentCheck::congruentTo(const MDefinition* ins) const {
  if (!ins->isWasmAlignmentCheck()) {
    return false;
  }
  const MWasmAlignmentCheck* check = ins->toWasmAlignmentCheck();
  return byteSize_ == check->byteSize() && congruentIfOperandsEqual(check);
}

MDefinition::AliasType MWasmLoadInstanceDataField::mightAlias(
    const MDefinition* def) const {
  if (def->isWasmStoreInstanceDataField()) {
    const MWasmStoreInstanceDataField* store =
        def->toWasmStoreInstanceDataField();
    return store->instanceDataOffset() == instanceDataOffset_
               ? AliasType::MayAlias
               : AliasType::NoAlias;
  }

  return AliasType::MayAlias;
}

MDefinition::AliasType MWasmLoadGlobalCell::mightAlias(
    const MDefinition* def) const {
  if (def->isWasmStoreGlobalCell()) {
    // No globals of different type can alias.  See bug 1467415 comment 3.
    if (type() != def->toWasmStoreGlobalCell()->value()->type()) {
      return AliasType::NoAlias;
    }

    // We could do better here.  We're dealing with two indirect globals.
    // If at at least one of them is created in this module, then they
    // can't alias -- in other words they can only alias if they are both
    // imported.  That would require having a flag on globals to indicate
    // which are imported.  See bug 1467415 comment 3, 4th rule.
  }

  return AliasType::MayAlias;
}

HashNumber MWasmLoadInstanceDataField::valueHash() const {
  // Same comment as in MWasmLoadInstanceDataField::congruentTo() applies here.
  HashNumber hash = MUnaryInstruction::valueHash();
  hash = addU32ToHash(hash, instanceDataOffset_);
  return hash;
}

bool MWasmLoadInstanceDataField::congruentTo(const MDefinition* ins) const {
  if (!ins->isWasmLoadInstanceDataField()) {
    return false;
  }

  const MWasmLoadInstanceDataField* other = ins->toWasmLoadInstanceDataField();

  // We don't need to consider the isConstant_ markings here, because
  // equivalence of offsets implies equivalence of constness.
  bool sameOffsets = instanceDataOffset_ == other->instanceDataOffset_;
  MOZ_ASSERT_IF(sameOffsets, isConstant_ == other->isConstant_);

  // We omit checking congruence of the operands.  There is only one
  // operand, the instance pointer, and it only ever has one value within the
  // domain of optimization.  If that should ever change then operand
  // congruence checking should be reinstated.
  return sameOffsets /* && congruentIfOperandsEqual(other) */;
}

MDefinition* MWasmLoadInstanceDataField::foldsTo(TempAllocator& alloc) {
  if (!dependency() || !dependency()->isWasmStoreInstanceDataField()) {
    return this;
  }

  MWasmStoreInstanceDataField* store =
      dependency()->toWasmStoreInstanceDataField();
  if (!store->block()->dominates(block())) {
    return this;
  }

  if (store->instanceDataOffset() != instanceDataOffset()) {
    return this;
  }

  if (store->value()->type() != type()) {
    return this;
  }

  return store->value();
}

MDefinition* MWasmSelect::foldsTo(TempAllocator& alloc) {
  if (condExpr()->isConstant()) {
    return condExpr()->toConstant()->toInt32() != 0 ? trueExpr() : falseExpr();
  }
  return this;
}

bool MWasmLoadGlobalCell::congruentTo(const MDefinition* ins) const {
  if (!ins->isWasmLoadGlobalCell()) {
    return false;
  }
  const MWasmLoadGlobalCell* other = ins->toWasmLoadGlobalCell();
  return congruentIfOperandsEqual(other);
}

#ifdef ENABLE_JIT_SIMD
MDefinition* MWasmTernarySimd128::foldsTo(TempAllocator& alloc) {
  if (simdOp() == wasm::SimdOp::V128Bitselect) {
    if (v2()->op() == MDefinition::Opcode::WasmFloatConstant) {
      int8_t shuffle[16];
      if (specializeBitselectConstantMaskAsShuffle(shuffle)) {
        return BuildWasmShuffleSimd128(alloc, shuffle, v0(), v1());
      }
    } else if (canRelaxBitselect()) {
      return MWasmTernarySimd128::New(alloc, v0(), v1(), v2(),
                                      wasm::SimdOp::I8x16RelaxedLaneSelect);
    }
  }
  return this;
}

inline static bool MatchSpecificShift(MDefinition* instr,
                                      wasm::SimdOp simdShiftOp,
                                      int shiftValue) {
  return instr->isWasmShiftSimd128() &&
         instr->toWasmShiftSimd128()->simdOp() == simdShiftOp &&
         instr->toWasmShiftSimd128()->rhs()->isConstant() &&
         instr->toWasmShiftSimd128()->rhs()->toConstant()->toInt32() ==
             shiftValue;
}

// Matches MIR subtree that represents PMADDUBSW instruction generated by
// emscripten. The a and b parameters return subtrees that correspond
// operands of the instruction, if match is found.
static bool MatchPmaddubswSequence(MWasmBinarySimd128* lhs,
                                   MWasmBinarySimd128* rhs, MDefinition** a,
                                   MDefinition** b) {
  MOZ_ASSERT(lhs->simdOp() == wasm::SimdOp::I16x8Mul &&
             rhs->simdOp() == wasm::SimdOp::I16x8Mul);
  // The emscripten/LLVM produced the following sequence for _mm_maddubs_epi16:
  //
  //  return _mm_adds_epi16(
  //    _mm_mullo_epi16(
  //      _mm_and_si128(__a, _mm_set1_epi16(0x00FF)),
  //      _mm_srai_epi16(_mm_slli_epi16(__b, 8), 8)),
  //    _mm_mullo_epi16(_mm_srli_epi16(__a, 8), _mm_srai_epi16(__b, 8)));
  //
  //  This will roughly correspond the following MIR:
  //    MWasmBinarySimd128[I16x8AddSatS]
  //      |-- lhs: MWasmBinarySimd128[I16x8Mul]                      (lhs)
  //      |     |-- lhs: MWasmBinarySimd128WithConstant[V128And]     (op0)
  //      |     |     |-- lhs: a
  //      |     |      -- rhs: SimdConstant::SplatX8(0x00FF)
  //      |      -- rhs: MWasmShiftSimd128[I16x8ShrS]                (op1)
  //      |           |-- lhs: MWasmShiftSimd128[I16x8Shl]
  //      |           |     |-- lhs: b
  //      |           |      -- rhs: MConstant[8]
  //      |            -- rhs: MConstant[8]
  //       -- rhs: MWasmBinarySimd128[I16x8Mul]                      (rhs)
  //            |-- lhs: MWasmShiftSimd128[I16x8ShrU]                (op2)
  //            |     |-- lhs: a
  //            |     |-- rhs: MConstant[8]
  //             -- rhs: MWasmShiftSimd128[I16x8ShrS]                (op3)
  //                  |-- lhs: b
  //                   -- rhs: MConstant[8]

  // The I16x8AddSatS and I16x8Mul are commutative, so their operands
  // may be swapped. Rearrange op0, op1, op2, op3 to be in the order
  // noted above.
  MDefinition *op0 = lhs->lhs(), *op1 = lhs->rhs(), *op2 = rhs->lhs(),
              *op3 = rhs->rhs();
  if (op1->isWasmBinarySimd128WithConstant()) {
    // Move MWasmBinarySimd128WithConstant[V128And] as first operand in lhs.
    std::swap(op0, op1);
  } else if (op3->isWasmBinarySimd128WithConstant()) {
    // Move MWasmBinarySimd128WithConstant[V128And] as first operand in rhs.
    std::swap(op2, op3);
  }
  if (op2->isWasmBinarySimd128WithConstant()) {
    // The lhs and rhs are swapped.
    // Make MWasmBinarySimd128WithConstant[V128And] to be op0.
    std::swap(op0, op2);
    std::swap(op1, op3);
  }
  if (op2->isWasmShiftSimd128() &&
      op2->toWasmShiftSimd128()->simdOp() == wasm::SimdOp::I16x8ShrS) {
    // The op2 and op3 appears to be in wrong order, swap.
    std::swap(op2, op3);
  }

  // Check all instructions SIMD code and constant values for assigned
  // names op0, op1, op2, op3 (see diagram above).
  const uint16_t const00FF[8] = {255, 255, 255, 255, 255, 255, 255, 255};
  if (!op0->isWasmBinarySimd128WithConstant() ||
      op0->toWasmBinarySimd128WithConstant()->simdOp() !=
          wasm::SimdOp::V128And ||
      memcmp(op0->toWasmBinarySimd128WithConstant()->rhs().bytes(), const00FF,
             16) != 0 ||
      !MatchSpecificShift(op1, wasm::SimdOp::I16x8ShrS, 8) ||
      !MatchSpecificShift(op2, wasm::SimdOp::I16x8ShrU, 8) ||
      !MatchSpecificShift(op3, wasm::SimdOp::I16x8ShrS, 8) ||
      !MatchSpecificShift(op1->toWasmShiftSimd128()->lhs(),
                          wasm::SimdOp::I16x8Shl, 8)) {
    return false;
  }

  // Check if the instructions arguments that are subtrees match the
  // a and b assignments. May depend on GVN behavior.
  MDefinition* maybeA = op0->toWasmBinarySimd128WithConstant()->lhs();
  MDefinition* maybeB = op3->toWasmShiftSimd128()->lhs();
  if (maybeA != op2->toWasmShiftSimd128()->lhs() ||
      maybeB != op1->toWasmShiftSimd128()->lhs()->toWasmShiftSimd128()->lhs()) {
    return false;
  }

  *a = maybeA;
  *b = maybeB;
  return true;
}

static bool IsZeroSimd128(MDefinition* def) {
  return def->isWasmFloatConstant() &&
         def->toWasmFloatConstant()->toSimd128().isZeroBits();
}

static wasm::SimdOp TestBitsOp(wasm::SimdOp op) {
  switch (op) {
    case wasm::SimdOp::I8x16Ne:
      return wasm::SimdOp::MozI8x16TestBits;
    case wasm::SimdOp::I16x8Ne:
      return wasm::SimdOp::MozI16x8TestBits;
    case wasm::SimdOp::I32x4Ne:
      return wasm::SimdOp::MozI32x4TestBits;
    case wasm::SimdOp::I64x2Ne:
      return wasm::SimdOp::MozI64x2TestBits;
    default:
      return wasm::SimdOp::Limit;
  }
}

// Match ne(and(a, b), 0) where the and has no other use.
static MWasmBinarySimd128* MatchTestBitsSequence(MWasmBinarySimd128* ins) {
  MDefinition* bits;
  if (IsZeroSimd128(ins->rhs())) {
    bits = ins->lhs();
  } else if (IsZeroSimd128(ins->lhs())) {
    bits = ins->rhs();
  } else {
    return nullptr;
  }
  if (!bits->isWasmBinarySimd128() ||
      bits->toWasmBinarySimd128()->simdOp() != wasm::SimdOp::V128And ||
      !bits->hasOneUse()) {
    return nullptr;
  }
  return bits->toWasmBinarySimd128();
}

// GVN merges equal constants, so a zero is often shared. Specializing each use
// of a shared zero is still a win on ARM64, which has compare-against-zero
// forms that need no register.
static bool CanSpecializeConstantOperand(MDefinition* constant,
                                         wasm::SimdOp op) {
  if (constant->hasOneUse()) {
    return true;
  }
#  if defined(JS_CODEGEN_ARM64)
  return constant->toWasmFloatConstant()->toSimd128().isZeroBits();
#  elif defined(JS_CODEGEN_LOONG64)
  return AssemblerLOONG64::CanEncodeSimdCompareImmediateFor(
      op, constant->toWasmFloatConstant()->toSimd128());
#  else
  return false;
#  endif
}

MDefinition* MWasmBinarySimd128::foldsTo(TempAllocator& alloc) {
  if (simdOp() == wasm::SimdOp::I8x16Swizzle && rhs()->isWasmFloatConstant()) {
    // Specialize swizzle(v, constant) as shuffle(mask, v, zero) to trigger all
    // our shuffle optimizations.  We don't report this rewriting as the report
    // will be overwritten by the subsequent shuffle analysis.
    int8_t shuffleMask[16];
    memcpy(shuffleMask, rhs()->toWasmFloatConstant()->toSimd128().bytes(), 16);
    for (signed char& i : shuffleMask) {
      // Out-of-bounds lanes reference the zero vector; in many cases, the zero
      // vector is removed by subsequent optimizations.
      if (i < 0 || i > 15) {
        i = 16;
      }
    }
    MWasmFloatConstant* zero =
        MWasmFloatConstant::NewSimd128(alloc, SimdConstant::SplatX4(0));
    if (!zero) {
      return nullptr;
    }
    block()->insertBefore(this, zero);
    return BuildWasmShuffleSimd128(alloc, shuffleMask, lhs(), zero);
  }

  // Fold ne(and(a, b), 0) into one test-bits operation. This has to come
  // before the constant specialization below, which would take the zero.
  if (canTestBits() && TestBitsOp(simdOp()) != wasm::SimdOp::Limit) {
    if (MWasmBinarySimd128* bitAnd = MatchTestBitsSequence(this)) {
      return MWasmBinarySimd128::New(alloc, bitAnd->lhs(), bitAnd->rhs(),
                                     /* commutative = */ true,
                                     TestBitsOp(simdOp()));
    }
  }

  // Specialize var OP const / const OP var when possible.
  //
  // As the LIR layer can't directly handle v128 constants as part of its normal
  // machinery we specialize some nodes here if they have single-use v128
  // constant arguments.  The purpose is to generate code that inlines the
  // constant in the instruction stream, using either a rip-relative load+op or
  // quickly-synthesized constant in a scratch on x64.  There is a general
  // assumption here that that is better than generating the constant into an
  // allocatable register, since that register value could not be reused. (This
  // ignores the possibility that the constant load could be hoisted).

  if (lhs()->isWasmFloatConstant() != rhs()->isWasmFloatConstant() &&
      specializeForConstantRhs()) {
    if (isCommutative() && lhs()->isWasmFloatConstant() &&
        CanSpecializeConstantOperand(lhs(), simdOp())) {
      return MWasmBinarySimd128WithConstant::New(
          alloc, rhs(), lhs()->toWasmFloatConstant()->toSimd128(), simdOp());
    }

    if (rhs()->isWasmFloatConstant() &&
        CanSpecializeConstantOperand(rhs(), simdOp())) {
      return MWasmBinarySimd128WithConstant::New(
          alloc, lhs(), rhs()->toWasmFloatConstant()->toSimd128(), simdOp());
    }
  }

  // Check special encoding for PMADDUBSW.
  if (canPmaddubsw() && simdOp() == wasm::SimdOp::I16x8AddSatS &&
      lhs()->isWasmBinarySimd128() && rhs()->isWasmBinarySimd128() &&
      lhs()->toWasmBinarySimd128()->simdOp() == wasm::SimdOp::I16x8Mul &&
      rhs()->toWasmBinarySimd128()->simdOp() == wasm::SimdOp::I16x8Mul) {
    MDefinition *a, *b;
    if (MatchPmaddubswSequence(lhs()->toWasmBinarySimd128(),
                               rhs()->toWasmBinarySimd128(), &a, &b)) {
      return MWasmBinarySimd128::New(alloc, a, b, /* commutative = */ false,
                                     wasm::SimdOp::MozPMADDUBSW);
    }
  }

  return this;
}

MDefinition* MWasmScalarToSimd128::foldsTo(TempAllocator& alloc) {
#  ifdef DEBUG
  auto logging = mozilla::MakeScopeExit([&] {
    js::wasm::ReportSimdAnalysis("scalar-to-simd128 -> constant folded");
  });
#  endif
  if (input()->isConstant()) {
    MConstant* c = input()->toConstant();
    switch (simdOp()) {
      case wasm::SimdOp::I8x16Splat:
        return MWasmFloatConstant::NewSimd128(
            alloc, SimdConstant::SplatX16(c->toInt32()));
      case wasm::SimdOp::I16x8Splat:
        return MWasmFloatConstant::NewSimd128(
            alloc, SimdConstant::SplatX8(c->toInt32()));
      case wasm::SimdOp::I32x4Splat:
        return MWasmFloatConstant::NewSimd128(
            alloc, SimdConstant::SplatX4(c->toInt32()));
      case wasm::SimdOp::I64x2Splat:
        return MWasmFloatConstant::NewSimd128(
            alloc, SimdConstant::SplatX2(c->toInt64()));
      default:
#  ifdef DEBUG
        logging.release();
#  endif
        return this;
    }
  }
  if (input()->isWasmFloatConstant()) {
    MWasmFloatConstant* c = input()->toWasmFloatConstant();
    switch (simdOp()) {
      case wasm::SimdOp::F32x4Splat:
        return MWasmFloatConstant::NewSimd128(
            alloc, SimdConstant::SplatX4(c->toFloat32()));
      case wasm::SimdOp::F64x2Splat:
        return MWasmFloatConstant::NewSimd128(
            alloc, SimdConstant::SplatX2(c->toDouble()));
      default:
#  ifdef DEBUG
        logging.release();
#  endif
        return this;
    }
  }
#  ifdef DEBUG
  logging.release();
#  endif
  return this;
}

template <typename T>
static bool AllTrue(const T& v) {
  constexpr size_t count = sizeof(T) / sizeof(*v);
  static_assert(count == 16 || count == 8 || count == 4 || count == 2);
  bool result = true;
  for (unsigned i = 0; i < count; i++) {
    result = result && v[i] != 0;
  }
  return result;
}

template <typename T>
static int32_t Bitmask(const T& v) {
  constexpr size_t count = sizeof(T) / sizeof(*v);
  constexpr size_t shift = 8 * sizeof(*v) - 1;
  static_assert(shift == 7 || shift == 15 || shift == 31 || shift == 63);
  int32_t result = 0;
  for (unsigned i = 0; i < count; i++) {
    result = result | int32_t(((v[i] >> shift) & 1) << i);
  }
  return result;
}

static uint32_t IntegerLaneBytes(wasm::SimdOp op) {
  switch (op) {
    case wasm::SimdOp::I8x16ExtractLaneS:
    case wasm::SimdOp::I8x16ExtractLaneU:
    case wasm::SimdOp::I8x16ReplaceLane:
      return 1;
    case wasm::SimdOp::I16x8ExtractLaneS:
    case wasm::SimdOp::I16x8ExtractLaneU:
    case wasm::SimdOp::I16x8ReplaceLane:
      return 2;
    case wasm::SimdOp::I32x4ExtractLane:
    case wasm::SimdOp::I32x4ReplaceLane:
      return 4;
    case wasm::SimdOp::I64x2ExtractLane:
    case wasm::SimdOp::I64x2ReplaceLane:
      return 8;
    default:
      return 0;
  }
}

MDefinition* MWasmReplaceLaneSimd128::foldsTo(TempAllocator& alloc) {
  // Fold replace_lane(v, extract_lane(w)) into one lane move when the
  // extracted lane is at least as wide as the replaced one, as the replaced
  // lane then takes the low bits of the extracted lane.
  if (!canMoveLane() || !rhs()->isWasmReduceSimd128()) {
    return this;
  }
  MWasmReduceSimd128* extract = rhs()->toWasmReduceSimd128();
  uint32_t dstBytes = IntegerLaneBytes(simdOp());
  uint32_t srcBytes = IntegerLaneBytes(extract->simdOp());
  if (dstBytes == 0 || srcBytes < dstBytes) {
    return this;
  }
  uint32_t srcLaneIndex = extract->imm() * (srcBytes / dstBytes);
  return MWasmMoveLaneSimd128::New(alloc, lhs(), extract->input(), laneIndex(),
                                   srcLaneIndex, simdOp());
}

MDefinition* MWasmReduceSimd128::foldsTo(TempAllocator& alloc) {
#  ifdef DEBUG
  auto logging = mozilla::MakeScopeExit([&] {
    js::wasm::ReportSimdAnalysis("simd128-to-scalar -> constant folded");
  });
#  endif
  if (input()->isWasmFloatConstant()) {
    SimdConstant c = input()->toWasmFloatConstant()->toSimd128();
    int32_t i32Result = 0;
    switch (simdOp()) {
      case wasm::SimdOp::V128AnyTrue:
        i32Result = !c.isZeroBits();
        break;
      case wasm::SimdOp::I8x16AllTrue:
        i32Result = AllTrue(
            SimdConstant::CreateSimd128((int8_t*)c.bytes()).asInt8x16());
        break;
      case wasm::SimdOp::I8x16Bitmask:
        i32Result = Bitmask(
            SimdConstant::CreateSimd128((int8_t*)c.bytes()).asInt8x16());
        break;
      case wasm::SimdOp::I16x8AllTrue:
        i32Result = AllTrue(
            SimdConstant::CreateSimd128((int16_t*)c.bytes()).asInt16x8());
        break;
      case wasm::SimdOp::I16x8Bitmask:
        i32Result = Bitmask(
            SimdConstant::CreateSimd128((int16_t*)c.bytes()).asInt16x8());
        break;
      case wasm::SimdOp::I32x4AllTrue:
        i32Result = AllTrue(
            SimdConstant::CreateSimd128((int32_t*)c.bytes()).asInt32x4());
        break;
      case wasm::SimdOp::I32x4Bitmask:
        i32Result = Bitmask(
            SimdConstant::CreateSimd128((int32_t*)c.bytes()).asInt32x4());
        break;
      case wasm::SimdOp::I64x2AllTrue:
        i32Result = AllTrue(
            SimdConstant::CreateSimd128((int64_t*)c.bytes()).asInt64x2());
        break;
      case wasm::SimdOp::I64x2Bitmask:
        i32Result = Bitmask(
            SimdConstant::CreateSimd128((int64_t*)c.bytes()).asInt64x2());
        break;
      case wasm::SimdOp::I8x16ExtractLaneS:
        i32Result =
            SimdConstant::CreateSimd128((int8_t*)c.bytes()).asInt8x16()[imm()];
        break;
      case wasm::SimdOp::I8x16ExtractLaneU:
        i32Result = int32_t(SimdConstant::CreateSimd128((int8_t*)c.bytes())
                                .asInt8x16()[imm()]) &
                    0xFF;
        break;
      case wasm::SimdOp::I16x8ExtractLaneS:
        i32Result =
            SimdConstant::CreateSimd128((int16_t*)c.bytes()).asInt16x8()[imm()];
        break;
      case wasm::SimdOp::I16x8ExtractLaneU:
        i32Result = int32_t(SimdConstant::CreateSimd128((int16_t*)c.bytes())
                                .asInt16x8()[imm()]) &
                    0xFFFF;
        break;
      case wasm::SimdOp::I32x4ExtractLane:
        i32Result =
            SimdConstant::CreateSimd128((int32_t*)c.bytes()).asInt32x4()[imm()];
        break;
      case wasm::SimdOp::I64x2ExtractLane:
        return MConstant::NewInt64(
            alloc, SimdConstant::CreateSimd128((int64_t*)c.bytes())
                       .asInt64x2()[imm()]);
      case wasm::SimdOp::F32x4ExtractLane:
        return MWasmFloatConstant::NewFloat32(
            alloc, SimdConstant::CreateSimd128((float*)c.bytes())
                       .asFloat32x4()[imm()]);
      case wasm::SimdOp::F64x2ExtractLane:
        return MWasmFloatConstant::NewDouble(
            alloc, SimdConstant::CreateSimd128((double*)c.bytes())
                       .asFloat64x2()[imm()]);
      default:
#  ifdef DEBUG
        logging.release();
#  endif
        return this;
    }
    return MConstant::NewInt32(alloc, i32Result);
  }
#  ifdef DEBUG
  logging.release();
#  endif
  return this;
}
#endif  // ENABLE_JIT_SIMD

MWasmCallCatchable* MWasmCallCatchable::New(
    TempAllocator& alloc, const wasm::CallSiteDesc& desc,
    const wasm::CalleeDesc& callee, const Args& args,
    uint32_t stackArgAreaSizeUnaligned, uint32_t tryNoteIndex,
    MBasicBlock* fallthroughBlock, MBasicBlock* prePadBlock,
    MDefinition* tableAddressOrRef) {
  MWasmCallCatchable* call = new (alloc)
      MWasmCallCatchable(desc, callee, stackArgAreaSizeUnaligned, tryNoteIndex);

  call->setSuccessor(FallthroughBranchIndex, fallthroughBlock);
  call->setSuccessor(PrePadBranchIndex, prePadBlock);

  MOZ_ASSERT_IF(callee.isTable() || callee.isFuncRef(), tableAddressOrRef);
  if (!call->initWithArgs(alloc, call, args, tableAddressOrRef)) {
    return nullptr;
  }

  return call;
}

MWasmCallCatchable* MWasmCallCatchable::NewBuiltinInstanceMethodCall(
    TempAllocator& alloc, const wasm::CallSiteDesc& desc,
    const wasm::SymbolicAddress builtin, wasm::FailureMode failureMode,
    wasm::Trap failureTrap, const ABIArg& instanceArg, const Args& args,
    uint32_t stackArgAreaSizeUnaligned, uint32_t tryNoteIndex,
    MBasicBlock* fallthroughBlock, MBasicBlock* prePadBlock) {
  auto callee = wasm::CalleeDesc::builtinInstanceMethod(builtin);
  MWasmCallCatchable* call = MWasmCallCatchable::New(
      alloc, desc, callee, args, stackArgAreaSizeUnaligned, tryNoteIndex,
      fallthroughBlock, prePadBlock, nullptr);
  if (!call) {
    return nullptr;
  }

  MOZ_ASSERT(instanceArg != ABIArg());
  call->instanceArg_ = instanceArg;
  call->builtinMethodFailureMode_ = failureMode;
  call->builtinMethodFailureTrap_ = failureTrap;
  return call;
}

MWasmCallUncatchable* MWasmCallUncatchable::New(
    TempAllocator& alloc, const wasm::CallSiteDesc& desc,
    const wasm::CalleeDesc& callee, const Args& args,
    uint32_t stackArgAreaSizeUnaligned, MDefinition* tableAddressOrRef) {
  MWasmCallUncatchable* call =
      new (alloc) MWasmCallUncatchable(desc, callee, stackArgAreaSizeUnaligned);

  MOZ_ASSERT_IF(callee.isTable() || callee.isFuncRef(), tableAddressOrRef);
  if (!call->initWithArgs(alloc, call, args, tableAddressOrRef)) {
    return nullptr;
  }

  return call;
}

MWasmCallUncatchable* MWasmCallUncatchable::NewBuiltinInstanceMethodCall(
    TempAllocator& alloc, const wasm::CallSiteDesc& desc,
    const wasm::SymbolicAddress builtin, wasm::FailureMode failureMode,
    wasm::Trap failureTrap, const ABIArg& instanceArg, const Args& args,
    uint32_t stackArgAreaSizeUnaligned) {
  auto callee = wasm::CalleeDesc::builtinInstanceMethod(builtin);
  MWasmCallUncatchable* call = MWasmCallUncatchable::New(
      alloc, desc, callee, args, stackArgAreaSizeUnaligned, nullptr);
  if (!call) {
    return nullptr;
  }

  MOZ_ASSERT(instanceArg != ABIArg());
  call->instanceArg_ = instanceArg;
  call->builtinMethodFailureMode_ = failureMode;
  call->builtinMethodFailureTrap_ = failureTrap;
  return call;
}

MWasmReturnCall* MWasmReturnCall::New(TempAllocator& alloc,
                                      const wasm::CallSiteDesc& desc,
                                      const wasm::CalleeDesc& callee,
                                      const Args& args,
                                      uint32_t stackArgAreaSizeUnaligned,
                                      MDefinition* tableAddressOrRef) {
  MWasmReturnCall* call =
      new (alloc) MWasmReturnCall(desc, callee, stackArgAreaSizeUnaligned);

  MOZ_ASSERT_IF(callee.isTable() || callee.isFuncRef(), tableAddressOrRef);
  if (!call->initWithArgs(alloc, call, args, tableAddressOrRef)) {
    return nullptr;
  }

  return call;
}

#ifdef ENABLE_WASM_JSPI
bool MWasmResume::init(MBasicBlock* fallthroughBlock, MBasicBlock* prePadBlock,
                       size_t numHandlers) {
  MOZ_ASSERT(hasTryNote() == !!prePadBlock);

  size_t numSuccessors = 1 + (hasTryNote() ? 1 : 0) + numHandlers;
  if (!successors_.resize(numSuccessors)) {
    return false;
  }
  successors_[FallthroughBranchIndex] = fallthroughBlock;
  if (hasTryNote()) {
    successors_[prePadBranchIndex()] = prePadBlock;
  }

  return handlers_.resize(numHandlers);
}

bool MWasmResume::initHandler(size_t index, uint32_t tagInstanceDataOffset,
                              uint32_t resultsAreaOffset, MBasicBlock* target) {
  successors_[handlerBranchIndex(index)] = target;
  handlers_[index].tagInstanceDataOffset = tagInstanceDataOffset;
  handlers_[index].resultsAreaOffset = resultsAreaOffset;
  return true;
}

AliasSet MWasmSuspend::getAliasSet() const {
  return MWasmCallBase::wasmCallAliasSet();
}
#endif  // ENABLE_WASM_JSPI

MIonToWasmCall* MIonToWasmCall::New(TempAllocator& alloc,
                                    WasmInstanceObject* instanceObj,
                                    const wasm::FuncExport& funcExport) {
  const wasm::FuncType& funcType =
      instanceObj->instance().codeMeta().getFuncType(funcExport.funcIndex());
  const wasm::ValTypeVector& results = funcType.results();
  MIRType resultType = MIRType::Value;
  // At the JS boundary some wasm types must be represented as a Value, and in
  // addition a void return requires an Undefined value.
  if (results.length() > 0 && !results[0].isEncodedAsJSValueOnEscape()) {
    MOZ_ASSERT(results.length() == 1,
               "multiple returns not implemented for inlined Wasm calls");
    resultType = results[0].toMIRType();
  }

  auto* ins = new (alloc) MIonToWasmCall(instanceObj, resultType, funcExport);
  if (!ins->init(alloc, funcType.args().length())) {
    return nullptr;
  }
  return ins;
}

#ifdef DEBUG
bool MIonToWasmCall::isConsistentFloat32Use(MUse* use) const {
  const wasm::FuncType& funcType =
      instance()->codeMeta().getFuncType(funcExport_.funcIndex());
  return funcType.args()[use->index()].kind() == wasm::ValType::F32;
}
#endif

bool MWasmShuffleSimd128::congruentTo(const MDefinition* ins) const {
  if (!ins->isWasmShuffleSimd128()) {
    return false;
  }
  return ins->toWasmShuffleSimd128()->shuffle().equals(&shuffle_) &&
         congruentIfOperandsEqual(ins);
}

#ifdef ENABLE_JIT_SIMD
MWasmShuffleSimd128* jit::BuildWasmShuffleSimd128(TempAllocator& alloc,
                                                  const int8_t* control,
                                                  MDefinition* lhs,
                                                  MDefinition* rhs) {
  SimdShuffle s =
      AnalyzeSimdShuffle(SimdConstant::CreateX16(control), lhs, rhs);
  switch (s.opd) {
    case SimdShuffle::Operand::LEFT:
      // When SimdShuffle::Operand is LEFT the right operand is not used,
      // lose reference to rhs.
      rhs = lhs;
      break;
    case SimdShuffle::Operand::RIGHT:
      // When SimdShuffle::Operand is RIGHT the left operand is not used,
      // lose reference to lhs.
      lhs = rhs;
      break;
    default:
      break;
  }
  return MWasmShuffleSimd128::New(alloc, lhs, rhs, s);
}
#endif  // ENABLE_JIT_SIMD

static MDefinition* FoldTrivialWasmTests(TempAllocator& alloc,
                                         wasm::RefType sourceType,
                                         wasm::RefType destType) {
  // Ignore everything involving uninhabitable types, because they are weird.
  if (!sourceType.isInhabitable() || !destType.isInhabitable()) {
    return nullptr;
  }

  // Upcasts are trivially valid.
  if (wasm::RefType::isSubTypeOf(sourceType, destType)) {
    return MConstant::NewInt32(alloc, 1);
  }

  // If two types are completely disjoint, then all casts between them are
  // impossible.
  if (!wasm::RefType::valuesInCommon(destType, sourceType)) {
    return MConstant::NewInt32(alloc, 0);
  }

  return nullptr;
}

static MDefinition* FoldTrivialWasmCasts(MDefinition* ref,
                                         wasm::RefType sourceType,
                                         wasm::RefType destType) {
  // Ignore everything involving uninhabitable types, because they are weird.
  if (!sourceType.isInhabitable() || !destType.isInhabitable()) {
    return nullptr;
  }

  // Upcasts are trivially valid.
  if (wasm::RefType::isSubTypeOf(sourceType, destType)) {
    return ref;
  }

  // We can't fold invalid casts to a trap instruction, because that will
  // confuse GVN which assumes the folded to instruction has the same type
  // as the original instruction.

  return nullptr;
}

MDefinition* MWasmRefTestAbstract::foldsTo(TempAllocator& alloc) {
  if (ref()->wasmRefType().isNothing()) {
    return this;
  }

  MDefinition* folded =
      FoldTrivialWasmTests(alloc, ref()->wasmRefType().value(), destType());
  if (folded) {
    return folded;
  }
  return this;
}

MDefinition* MWasmRefTestConcrete::foldsTo(TempAllocator& alloc) {
  if (ref()->wasmRefType().isNothing()) {
    return this;
  }

  MDefinition* folded =
      FoldTrivialWasmTests(alloc, ref()->wasmRefType().value(), destType());
  if (folded) {
    return folded;
  }
  return this;
}

MDefinition* MWasmRefCastAbstract::foldsTo(TempAllocator& alloc) {
  if (ref()->wasmRefType().isNothing()) {
    return this;
  }

  MDefinition* folded =
      FoldTrivialWasmCasts(ref(), ref()->wasmRefType().value(), destType());
  if (folded) {
    return folded;
  }
  return this;
}

MDefinition* MWasmRefCastConcrete::foldsTo(TempAllocator& alloc) {
  if (ref()->wasmRefType().isNothing()) {
    return this;
  }

  MDefinition* folded =
      FoldTrivialWasmCasts(ref(), ref()->wasmRefType().value(), destType());
  if (folded) {
    return folded;
  }
  return this;
}

MDefinition* MWasmRefAsNonNull::foldsTo(TempAllocator& alloc) {
  wasm::MaybeRefType inputType = ref()->wasmRefType();
  if (inputType.isSome() && !inputType.value().isNullable()) {
    return ref();
  }
  return this;
}

bool MWasmStructState::init(TempAllocator& alloc) {
  // Reserve the size for the number of fields.
  const wasm::StructType& structType =
      wasmStruct_->toWasmNewStructObject()->structType();
  if (!fields_.reserve(structType.fields_.length())) {
    return false;
  }

  for (uint32_t fieldIndex = 0; fieldIndex < structType.fields_.length();
       fieldIndex++) {
    wasm::StorageType storageType = structType.fields_[fieldIndex].type;

    // If we were to 'struct.get' a field, it would be widened.
    wasm::ValType valType = storageType.widenToValType();

    MInstruction* defaultValue = nullptr;

    // Defaultable fields start with a default value, otherwise the default
    // value will be provided by a struct write after the allocation.
    if (valType.isDefaultable()) {
      defaultValue = NewWasmDefaultConstant(alloc, valType);
      if (!defaultValue) {
        return false;
      }
      wasmStruct_->block()->insertBefore(wasmStruct_, defaultValue);
    }

    fields_.infallibleAppend(defaultValue);
  }

  return true;
}

MWasmStructState* MWasmStructState::New(TempAllocator& alloc,
                                        MWasmNewStructObject* structObject) {
  MWasmStructState* state = new (alloc) MWasmStructState(alloc, structObject);
  if (!state->init(alloc)) {
    return nullptr;
  }
  return state;
}

MWasmStructState* MWasmStructState::Copy(TempAllocator& alloc,
                                         MWasmStructState* state) {
  MWasmNewStructObject* newWasmStruct = state->wasmStruct();
  MWasmStructState* res = new (alloc) MWasmStructState(alloc, newWasmStruct);
  if (!res || !res->fields_.appendAll(state->fields_)) {
    return nullptr;
  }
  return res;
}

MDefinition* MWasmAddSubI128HI64::foldsTo(TempAllocator& alloc) {
  MDefinition* mLhsLo = lhsLo();
  MDefinition* mLhsHi = lhsHi();
  MDefinition* mRhsLo = rhsLo();
  MDefinition* mRhsHi = rhsHi();
  MOZ_ASSERT(mLhsLo->type() == MIRType::Int64);
  MOZ_ASSERT(mLhsHi->type() == MIRType::Int64);
  MOZ_ASSERT(mRhsLo->type() == MIRType::Int64);
  MOZ_ASSERT(mRhsHi->type() == MIRType::Int64);

  bool lhsLoIsConst = mLhsLo->isConstant();
  int64_t lhsLoImm = lhsLoIsConst ? mLhsLo->toConstant()->toInt64() : 0;

  bool lhsHiIsConst = mLhsHi->isConstant();
  int64_t lhsHiImm = lhsHiIsConst ? mLhsHi->toConstant()->toInt64() : 0;

  bool rhsLoIsConst = mRhsLo->isConstant();
  int64_t rhsLoImm = rhsLoIsConst ? mRhsLo->toConstant()->toInt64() : 0;

  bool rhsHiIsConst = mRhsHi->isConstant();
  int64_t rhsHiImm = rhsHiIsConst ? mRhsHi->toConstant()->toInt64() : 0;

  if (isAdd()) {
    // addHI 0:0 x:y --> x
    if (lhsHiIsConst && lhsHiImm == 0 && lhsLoIsConst && lhsLoImm == 0) {
      return mRhsHi;
    }
    // addHI x:y 0:0 --> x
    if (rhsHiIsConst && rhsHiImm == 0 && rhsLoIsConst && rhsLoImm == 0) {
      return mLhsHi;
    }
  }

  // There are other folding opportunities, but none of them look particularly
  // interesting.  In particular:
  //
  //   addHI 0:x 0:y = carry bit out of x +64 y
  //
  // This isn't helpful because we don't have a way to compute the carry bit
  // that is cheaper than doing the full 128-bit add.

  return this;
}
