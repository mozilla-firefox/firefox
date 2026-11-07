/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/x64/CodeGenerator-x64.h"

#include "mozilla/CheckedInt.h"
#include "mozilla/FloatingPoint.h"

#include <bit>

#include "jit/CodeGenerator.h"
#include "jit/MIR-wasm.h"
#include "jit/MIR.h"
#include "jit/ReciprocalMulConstants.h"
#include "js/ScalarType.h"  // js::Scalar::Type

#include "jit/MacroAssembler-inl.h"
#include "jit/shared/CodeGenerator-shared-inl.h"

using namespace js;
using namespace js::jit;

CodeGeneratorX64::CodeGeneratorX64(MIRGenerator* gen, LIRGraph* graph,
                                   MacroAssembler* masm,
                                   const wasm::CodeMetadata* wasmCodeMeta)
    : CodeGeneratorX86Shared(gen, graph, masm, wasmCodeMeta) {}

Operand CodeGeneratorX64::ToOperand64(const LInt64Allocation& a64) {
  const LAllocation& a = a64.value();
  MOZ_ASSERT(!a.isFloatReg());
  if (a.isGeneralReg()) {
    return Operand(a.toGeneralReg()->reg());
  }
  return Operand(ToAddress(a));
}

void CodeGenerator::visitBox(LBox* box) {
  const LAllocation* in = box->payload();
  ValueOperand result = ToOutValue(box);

  masm.moveValue(TypedOrValueRegister(box->type(), ToAnyRegister(in)), result);

  if (JitOptions.spectreValueMasking && IsFloatingPointType(box->type())) {
    ScratchRegisterScope scratch(masm);
    masm.movePtr(ImmWord(JSVAL_SHIFTED_TAG_MAX_DOUBLE), scratch);
    masm.cmpPtrMovePtr(Assembler::Below, scratch, result.valueReg(), scratch,
                       result.valueReg());
  }
}

void CodeGenerator::visitUnbox(LUnbox* unbox) {
  MUnbox* mir = unbox->mir();

  Register result = ToRegister(unbox->output());

  if (mir->fallible()) {
    Label bail;
    auto fallibleUnboxImpl = [&](auto value) {
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
    };
    LAllocation* input = unbox->getOperand(LUnbox::Input);
    if (input->isGeneralReg()) {
      fallibleUnboxImpl(ValueOperand(ToRegister(input)));
    } else {
      fallibleUnboxImpl(ToAddress(input));
    }
    bailoutFrom(&bail, unbox->snapshot());
    return;
  }

  // Infallible unbox.

  Operand input = ToOperand(unbox->getOperand(LUnbox::Input));

#ifdef DEBUG
  // Assert the types match.
  JSValueTag tag = MIRTypeToTag(mir->type());
  Label ok;
  masm.splitTag(input, ScratchReg);
  masm.branch32(Assembler::Equal, ScratchReg, Imm32(tag), &ok);
  masm.assumeUnreachable("Infallible unbox type mismatch");
  masm.bind(&ok);
#endif

  switch (mir->type()) {
    case MIRType::Int32:
      masm.unboxInt32(input, result);
      break;
    case MIRType::Boolean:
      masm.unboxBoolean(input, result);
      break;
    case MIRType::Object:
      masm.unboxObject(input, result);
      break;
    case MIRType::String:
      masm.unboxString(input, result);
      break;
    case MIRType::Symbol:
      masm.unboxSymbol(input, result);
      break;
    case MIRType::BigInt:
      masm.unboxBigInt(input, result);
      break;
    default:
      MOZ_CRASH("Given MIRType cannot be unboxed.");
  }
}

void CodeGenerator::visitMulI64(LMulI64* lir) {
  Register lhs = ToRegister64(lir->lhs()).reg;
  LInt64Allocation rhs = lir->rhs();
  Register out = ToOutRegister64(lir).reg;

  if (IsConstant(rhs)) {
    int64_t constant = ToInt64(rhs);
    switch (constant) {
      case -1:
        if (lhs != out) {
          masm.movq(lhs, out);
        }
        masm.negq(out);
        break;
      case 0:
        masm.xorq(out, out);
        break;
      case 1:
        if (lhs != out) {
          masm.movq(lhs, out);
        }
        break;
      case 2:
        if (lhs == out) {
          masm.addq(lhs, lhs);
        } else {
          masm.lea(Operand(lhs, lhs, TimesOne), out);
        }
        break;
      case 3:
        masm.lea(Operand(lhs, lhs, TimesTwo), out);
        break;
      case 4:
        if (lhs == out) {
          masm.shlq(Imm32(2), lhs);
        } else {
          masm.lea(Operand(lhs, TimesFour, 0), out);
        }
        break;
      case 5:
        masm.lea(Operand(lhs, lhs, TimesFour), out);
        break;
      case 8:
        if (lhs == out) {
          masm.shlq(Imm32(3), lhs);
        } else {
          masm.lea(Operand(lhs, TimesEight, 0), out);
        }
        break;
      case 9:
        masm.lea(Operand(lhs, lhs, TimesEight), out);
        break;
      default: {
        // Use shift if constant is power of 2.
        int32_t shift = mozilla::FloorLog2(uint64_t(constant));
        if (constant > 0 && std::has_single_bit(uint64_t(constant))) {
          if (lhs != out) {
            masm.movq(lhs, out);
          }
          masm.shlq(Imm32(shift), out);
        } else if (int32_t(constant) == constant) {
          masm.imulq(Imm32(constant), lhs, out);
        } else {
          MOZ_ASSERT(out == lhs);
          masm.mul64(Imm64(constant), Register64(lhs));
        }
        break;
      }
    }
  } else {
    MOZ_ASSERT(out == lhs);
    masm.imulq(ToOperandOrRegister64(rhs), lhs);
  }
}

template <class LIR>
static void TrapIfDivideByZero(MacroAssembler& masm, LIR* lir, Register rhs) {
  auto* mir = lir->mir();
  MOZ_ASSERT(mir->trapOnError());

  if (mir->canBeDivideByZero()) {
    Label nonZero;
    masm.branchTestPtr(Assembler::NonZero, rhs, rhs, &nonZero);
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
    masm.bind(&nonZero);
  }
}

void CodeGenerator::visitDivI64(LDivI64* lir) {
  Register lhs = ToRegister(lir->lhs());
  Register rhs = ToRegister(lir->rhs());

  MOZ_ASSERT(lhs == rax);
  MOZ_ASSERT(rhs != rax);
  MOZ_ASSERT(rhs != rdx);
  MOZ_ASSERT(ToRegister(lir->output()) == rax);
  MOZ_ASSERT(ToRegister(lir->temp0()) == rdx);

  MDiv* mir = lir->mir();

  // Handle divide by zero.
  TrapIfDivideByZero(masm, lir, rhs);

  // Handle an integer overflow exception from INT64_MIN / -1.
  if (mir->canBeNegativeOverflow()) {
    Label notOverflow;
    masm.branchPtr(Assembler::NotEqual, lhs, ImmWord(INT64_MIN), &notOverflow);
    masm.branchPtr(Assembler::NotEqual, rhs, ImmWord(-1), &notOverflow);
    masm.wasmTrap(wasm::Trap::IntegerOverflow, mir->trapSiteDesc());
    masm.bind(&notOverflow);
  }

  // Sign extend the lhs into rdx to make rdx:rax.
  masm.cqo();
  masm.idivq(rhs);
}

void CodeGenerator::visitModI64(LModI64* lir) {
  Register lhs = ToRegister(lir->lhs());
  Register rhs = ToRegister(lir->rhs());
  Register output = ToRegister(lir->output());

  MOZ_ASSERT(lhs == rax);
  MOZ_ASSERT(rhs != rax);
  MOZ_ASSERT(rhs != rdx);
  MOZ_ASSERT(ToRegister(lir->output()) == rdx);
  MOZ_ASSERT(ToRegister(lir->temp0()) == rax);

  MMod* mir = lir->mir();

  Label done;

  // Handle divide by zero.
  TrapIfDivideByZero(masm, lir, rhs);

  // Handle an integer overflow exception from INT64_MIN / -1.
  if (mir->canBeNegativeDividend()) {
    Label notOverflow;
    masm.branchPtr(Assembler::NotEqual, lhs, ImmWord(INT64_MIN), &notOverflow);
    masm.branchPtr(Assembler::NotEqual, rhs, ImmWord(-1), &notOverflow);
    {
      masm.xorl(output, output);
      masm.jump(&done);
    }
    masm.bind(&notOverflow);
  }

  // Sign extend the lhs into rdx to make rdx:rax.
  masm.cqo();
  masm.idivq(rhs);

  masm.bind(&done);
}

void CodeGenerator::visitUDivI64(LUDivI64* lir) {
  Register rhs = ToRegister(lir->rhs());

  MOZ_ASSERT(ToRegister(lir->lhs()) == rax);
  MOZ_ASSERT(rhs != rax);
  MOZ_ASSERT(rhs != rdx);
  MOZ_ASSERT(ToRegister(lir->output()) == rax);
  MOZ_ASSERT(ToRegister(lir->temp0()) == rdx);

  // Prevent divide by zero.
  TrapIfDivideByZero(masm, lir, rhs);

  // Zero extend the lhs into rdx to make (rdx:rax).
  masm.xorl(rdx, rdx);
  masm.udivq(rhs);
}

void CodeGenerator::visitUModI64(LUModI64* lir) {
  Register rhs = ToRegister(lir->rhs());

  MOZ_ASSERT(ToRegister(lir->lhs()) == rax);
  MOZ_ASSERT(rhs != rax);
  MOZ_ASSERT(rhs != rdx);
  MOZ_ASSERT(ToRegister(lir->output()) == rdx);
  MOZ_ASSERT(ToRegister(lir->temp0()) == rax);

  // Prevent divide by zero.
  TrapIfDivideByZero(masm, lir, rhs);

  // Zero extend the lhs into rdx to make (rdx:rax).
  masm.xorl(rdx, rdx);
  masm.udivq(rhs);
}

void CodeGenerator::visitDivPowTwoI64(LDivPowTwoI64* ins) {
  Register lhs = ToRegister(ins->numerator());

  int32_t shift = ins->shift();
  bool negativeDivisor = ins->negativeDivisor();
  MDiv* mir = ins->mir();

  // We use defineReuseInput so these should always be the same, which is
  // convenient since all of our instructions here are two-address.
  MOZ_ASSERT(lhs == ToRegister(ins->output()));

  // Unsigned division is just a right-shift.
  if (mir->isUnsigned()) {
    if (shift != 0) {
      masm.shrq(Imm32(shift), lhs);
    }
    return;
  }

  if (shift != 0) {
    // Adjust the value so that shifting produces a correctly rounded result
    // when the numerator is negative.
    // See 10-1 "Signed Division by a Known Power of 2" in Henry S. Warren,
    // Jr.'s Hacker's Delight.
    if (mir->canBeNegativeDividend()) {
      Register lhsCopy = ToRegister(ins->numeratorCopy());
      MOZ_ASSERT(lhsCopy != lhs);
      if (shift > 1) {
        // Copy the sign bit of the numerator. (= (2^63 - 1) or 0)
        masm.sarq(Imm32(63), lhs);
      }
      // Divide by 2^(64 - shift)
      // i.e. (= (2^64 - 1) / 2^(64 - shift) or 0)
      // i.e. (= (2^shift - 1) or 0)
      masm.shrq(Imm32(64 - shift), lhs);
      // If signed, make any 1 bit below the shifted bits to bubble up, such
      // that once shifted the value would be rounded towards 0.
      masm.addq(lhsCopy, lhs);
    }
    masm.sarq(Imm32(shift), lhs);
  }

  if (negativeDivisor) {
    masm.negq(lhs);
  }

  if (shift == 0 && negativeDivisor) {
    // INT64_MIN / -1 overflows.
    Label ok;
    masm.j(Assembler::NoOverflow, &ok);
    masm.wasmTrap(wasm::Trap::IntegerOverflow, mir->trapSiteDesc());
    masm.bind(&ok);
  }
}

void CodeGenerator::visitModPowTwoI64(LModPowTwoI64* ins) {
  Register64 lhs = Register64(ToRegister(ins->input()));
  int32_t shift = ins->shift();
  bool canBeNegative =
      !ins->mir()->isUnsigned() && ins->mir()->canBeNegativeDividend();

  MOZ_ASSERT(lhs.reg == ToRegister(ins->output()));

  if (shift == 0) {
    masm.xorl(lhs.reg, lhs.reg);
    return;
  }

  auto clearHighBits = [&]() {
    switch (shift) {
      case 8:
        masm.movzbl(lhs.reg, lhs.reg);
        break;
      case 16:
        masm.movzwl(lhs.reg, lhs.reg);
        break;
      case 32:
        masm.movl(lhs.reg, lhs.reg);
        break;
      default:
        masm.and64(Imm64((uint64_t(1) << shift) - 1), lhs);
        break;
    }
  };

  Label negative;

  if (canBeNegative) {
    // Switch based on sign of the lhs.
    // Positive numbers are just a bitmask
    masm.branchTest64(Assembler::Signed, lhs, lhs, &negative);
  }

  clearHighBits();

  if (canBeNegative) {
    Label done;
    masm.jump(&done);

    // Negative numbers need a negate, bitmask, negate
    masm.bind(&negative);

    // Unlike in the visitModI64 case, we are not computing the mod by means of
    // a division. Therefore, the divisor = -1 case isn't problematic (the andq
    // always returns 0, which is what we expect).
    //
    // The negq instruction overflows if lhs == INT64_MIN, but this is also not
    // a problem: shift is at most 63, and so the andq also always returns 0.
    masm.neg64(lhs);
    clearHighBits();
    masm.neg64(lhs);

    masm.bind(&done);
  }
}

template <class LDivOrMod>
static void Divide64WithConstant(MacroAssembler& masm, LDivOrMod* ins) {
  Register lhs = ToRegister(ins->numerator());
  [[maybe_unused]] Register output = ToRegister(ins->output());
  [[maybe_unused]] Register temp = ToRegister(ins->temp0());
  int64_t d = ins->denominator();

  MOZ_ASSERT(lhs != rax && lhs != rdx);
  MOZ_ASSERT((output == rax && temp == rdx) || (output == rdx && temp == rax));

  // The absolute value of the denominator isn't a power of 2 (see LDivPowTwoI64
  // and LModPowTwoI64).
  MOZ_ASSERT(!std::has_single_bit(mozilla::Abs(d)));

  auto* mir = ins->mir();

  // We will first divide by Abs(d), and negate the answer if d is negative.
  // If desired, this can be avoided by generalizing computeDivisionConstants.
  auto rmc = ReciprocalMulConstants::computeSignedDivisionConstants(d);

  // We first compute (M * n) >> 64, where M = rmc.multiplier.
  masm.movq(ImmWord(uint64_t(rmc.multiplier)), rax);
  masm.imulq(lhs);
  if (rmc.multiplier > Int128(INT64_MAX)) {
    MOZ_ASSERT(rmc.multiplier < (Int128(1) << 64));

    // We actually computed rdx = ((int64_t(M) * n) >> 64) instead. Since
    // (M * n) >> 64 is the same as (rdx + n), we can correct for the overflow.
    // (rdx + n) can't overflow, as n and rdx have opposite signs because
    // int64_t(M) is negative.
    masm.addq(lhs, rdx);
  }
  // (M * n) >> (64 + shift) is the truncated division answer if n is
  // non-negative, as proved in the comments of computeDivisionConstants. We
  // must add 1 later if n is negative to get the right answer in all cases.
  if (rmc.shiftAmount > 0) {
    masm.sarq(Imm32(rmc.shiftAmount), rdx);
  }

  // We'll subtract -1 instead of adding 1, because (n < 0 ? -1 : 0) can be
  // computed with just a sign-extending shift of 63 bits.
  if (mir->canBeNegativeDividend()) {
    masm.movq(lhs, rax);
    masm.sarq(Imm32(63), rax);
    masm.subq(rax, rdx);
  }

  // After this, rdx contains the correct truncated division result.
  if (d < 0) {
    masm.negq(rdx);
  }
}

void CodeGenerator::visitDivConstantI64(LDivConstantI64* ins) {
  int64_t d = ins->denominator();

  // This emits the division answer into rdx.
  MOZ_ASSERT(ToRegister(ins->output()) == rdx);
  MOZ_ASSERT(ToRegister(ins->temp0()) == rax);

  if (d == 0) {
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, ins->mir()->trapSiteDesc());
    return;
  }

  // Compute the truncated division result in rdx.
  Divide64WithConstant(masm, ins);
}

void CodeGenerator::visitModConstantI64(LModConstantI64* ins) {
  Register lhs = ToRegister(ins->numerator());
  int64_t d = ins->denominator();

  // This emits the modulus answer into rax.
  MOZ_ASSERT(ToRegister(ins->output()) == rax);
  MOZ_ASSERT(ToRegister(ins->temp0()) == rdx);

  if (d == 0) {
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, ins->mir()->trapSiteDesc());
    return;
  }

  // Compute the truncated division result in rdx.
  Divide64WithConstant(masm, ins);

  // Compute the remainder in |rax|: rax = lhs - d * rdx
  masm.mul64(Imm64(d), Register64(rdx));
  masm.movq(lhs, rax);
  masm.subq(rdx, rax);
}

template <class LUDivOrUMod>
static void UnsignedDivide64WithConstant(MacroAssembler& masm,
                                         LUDivOrUMod* ins) {
  Register lhs = ToRegister(ins->numerator());
  [[maybe_unused]] Register output = ToRegister(ins->output());
  [[maybe_unused]] Register temp = ToRegister(ins->temp0());
  uint64_t d = ins->denominator();

  MOZ_ASSERT(lhs != rax && lhs != rdx);
  MOZ_ASSERT((output == rax && temp == rdx) || (output == rdx && temp == rax));

  // The denominator isn't a power of 2 (see LDivPowTwoI and LModPowTwoI).
  MOZ_ASSERT(!std::has_single_bit(d));

  auto rmc = ReciprocalMulConstants::computeUnsignedDivisionConstants(d);

  // We first compute (M * n) >> 64, where M = rmc.multiplier.
  masm.movq(ImmWord(uint64_t(rmc.multiplier)), rax);
  masm.umulq(lhs);
  if (rmc.multiplier > Int128(UINT64_MAX)) {
    // M >= 2^64 and shift == 0 is impossible, as d >= 2 implies that
    // ((M * n) >> (64 + shift)) >= n > floor(n/d) whenever n >= d,
    // contradicting the proof of correctness in computeDivisionConstants.
    MOZ_ASSERT(rmc.shiftAmount > 0);
    MOZ_ASSERT(rmc.multiplier < (Int128(1) << 65));

    // We actually computed rdx = ((uint64_t(M) * n) >> 64) instead. Since
    // (M * n) >> (64 + shift) is the same as (rdx + n) >> shift, we can correct
    // for the overflow. This case is a bit trickier than the signed case,
    // though, as the (rdx + n) addition itself can overflow; however, note that
    // (rdx + n) >> shift == (((n - rdx) >> 1) + rdx) >> (shift - 1),
    // which is overflow-free. See Hacker's Delight, section 10-8 for details.

    // Compute (n - rdx) >> 1 into temp.
    masm.movq(lhs, rax);
    masm.subq(rdx, rax);
    masm.shrq(Imm32(1), rax);

    // Finish the computation.
    masm.addq(rax, rdx);
    masm.shrq(Imm32(rmc.shiftAmount - 1), rdx);
  } else {
    if (rmc.shiftAmount > 0) {
      masm.shrq(Imm32(rmc.shiftAmount), rdx);
    }
  }
}

void CodeGenerator::visitUDivConstantI64(LUDivConstantI64* ins) {
  uint64_t d = ins->denominator();

  // This emits the division answer into rdx.
  MOZ_ASSERT(ToRegister(ins->output()) == rdx);
  MOZ_ASSERT(ToRegister(ins->temp0()) == rax);

  if (d == 0) {
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, ins->mir()->trapSiteDesc());
    return;
  }

  // Compute the truncated division result in rdx.
  UnsignedDivide64WithConstant(masm, ins);
}

void CodeGenerator::visitUModConstantI64(LUModConstantI64* ins) {
  Register lhs = ToRegister(ins->numerator());
  uint64_t d = ins->denominator();

  // This emits the modulus answer into rax.
  MOZ_ASSERT(ToRegister(ins->output()) == rax);
  MOZ_ASSERT(ToRegister(ins->temp0()) == rdx);

  if (d == 0) {
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, ins->mir()->trapSiteDesc());
    return;
  }

  // Compute the truncated division result in rdx.
  UnsignedDivide64WithConstant(masm, ins);

  // Compute the remainder in |rax|: rax = lhs - d * rdx
  masm.mul64(Imm64(d), Register64(rdx));
  masm.movq(lhs, rax);
  masm.subq(rdx, rax);
}

void CodeGeneratorX64::emitBigIntPtrDiv(LBigIntPtrDiv* ins, Register dividend,
                                        Register divisor, Register output) {
  // Callers handle division by zero and integer overflow.

  MOZ_ASSERT(ToRegister(ins->temp0()) == rdx);
  MOZ_ASSERT(output == rax);

  if (dividend != rax) {
    masm.movePtr(dividend, rax);
  }

  // Sign extend the lhs into rdx to make rdx:rax.
  masm.cqo();

  masm.idivq(divisor);
}

void CodeGeneratorX64::emitBigIntPtrMod(LBigIntPtrMod* ins, Register dividend,
                                        Register divisor, Register output) {
  // Callers handle division by zero and integer overflow.

  MOZ_ASSERT(dividend == rax);
  MOZ_ASSERT(output == rdx);

  // Sign extend the lhs into rdx to make rdx:rax.
  masm.cqo();

  masm.idivq(divisor);
}

void CodeGenerator::visitShiftIntPtr(LShiftIntPtr* ins) {
  Register lhs = ToRegister(ins->lhs());
  const LAllocation* rhs = ins->rhs();
  Register out = ToRegister(ins->output());

  if (rhs->isConstant()) {
    MOZ_ASSERT(out == lhs);

    int32_t shift = ToIntPtr(rhs) & 0x3f;
    switch (ins->bitop()) {
      case JSOp::Lsh:
        if (shift) {
          masm.lshiftPtr(Imm32(shift), lhs);
        }
        break;
      case JSOp::Rsh:
        if (shift) {
          masm.rshiftPtrArithmetic(Imm32(shift), lhs);
        }
        break;
      case JSOp::Ursh:
        if (shift) {
          masm.rshiftPtr(Imm32(shift), lhs);
        }
        break;
      default:
        MOZ_CRASH("Unexpected shift op");
    }
  } else {
    Register shift = ToRegister(rhs);
    MOZ_ASSERT_IF(out != lhs, Assembler::HasBMI2());

    switch (ins->bitop()) {
      case JSOp::Lsh:
        if (out != lhs) {
          masm.shlxq(lhs, shift, out);
        } else {
          masm.lshiftPtr(shift, lhs);
        }
        break;
      case JSOp::Rsh:
        if (out != lhs) {
          masm.sarxq(lhs, shift, out);
        } else {
          masm.rshiftPtrArithmetic(shift, lhs);
        }
        break;
      case JSOp::Ursh:
        if (out != lhs) {
          masm.shrxq(lhs, shift, out);
        } else {
          masm.rshiftPtr(shift, lhs);
        }
        break;
      default:
        MOZ_CRASH("Unexpected shift op");
    }
  }
}

void CodeGenerator::visitShiftI64(LShiftI64* lir) {
  Register lhs = ToRegister64(lir->lhs()).reg;
  const LAllocation* rhs = lir->rhs();
  Register out = ToOutRegister64(lir).reg;

  if (rhs->isConstant()) {
    MOZ_ASSERT(out == lhs);

    int32_t shift = int32_t(rhs->toConstant()->toInt64() & 0x3F);
    switch (lir->bitop()) {
      case JSOp::Lsh:
        if (shift) {
          masm.lshiftPtr(Imm32(shift), lhs);
        }
        break;
      case JSOp::Rsh:
        if (shift) {
          masm.rshiftPtrArithmetic(Imm32(shift), lhs);
        }
        break;
      case JSOp::Ursh:
        if (shift) {
          masm.rshiftPtr(Imm32(shift), lhs);
        }
        break;
      default:
        MOZ_CRASH("Unexpected shift op");
    }
    return;
  }

  Register shift = ToRegister(rhs);
  MOZ_ASSERT_IF(out != lhs, Assembler::HasBMI2());

  switch (lir->bitop()) {
    case JSOp::Lsh:
      if (out != lhs) {
        masm.shlxq(lhs, shift, out);
      } else {
        masm.lshiftPtr(shift, lhs);
      }
      break;
    case JSOp::Rsh:
      if (out != lhs) {
        masm.sarxq(lhs, shift, out);
      } else {
        masm.rshiftPtrArithmetic(shift, lhs);
      }
      break;
    case JSOp::Ursh:
      if (out != lhs) {
        masm.shrxq(lhs, shift, out);
      } else {
        masm.rshiftPtr(shift, lhs);
      }
      break;
    default:
      MOZ_CRASH("Unexpected shift op");
  }
}

void CodeGenerator::visitAtomicLoad64(LAtomicLoad64* lir) {
  Register elements = ToRegister(lir->elements());
  Register64 out = ToOutRegister64(lir);

  Scalar::Type storageType = lir->mir()->storageType();

  auto source = ToAddressOrBaseIndex(elements, lir->index(), storageType);

  // NOTE: the generated code must match the assembly code in gen_load in
  // GenerateAtomicOperations.py
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

  // NOTE: the generated code must match the assembly code in gen_store in
  // GenerateAtomicOperations.py
  auto sync = Synchronization::Store();

  masm.memoryBarrierBefore(sync);
  dest.match([&](const auto& dest) { masm.store64(value, dest); });
  masm.memoryBarrierAfter(sync);
}

void CodeGenerator::visitCompareExchangeTypedArrayElement64(
    LCompareExchangeTypedArrayElement64* lir) {
  Register elements = ToRegister(lir->elements());
  Register64 oldval = ToRegister64(lir->oldval());
  Register64 newval = ToRegister64(lir->newval());
  Register64 out = ToOutRegister64(lir);

  MOZ_ASSERT(out.reg == rax);

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
  MOZ_ASSERT(!lir->mir()->isForEffect());

  Register elements = ToRegister(lir->elements());
  Register64 value = ToRegister64(lir->value());
  Register64 temp = ToTempRegister64OrInvalid(lir->temp0());
  Register64 out = ToOutRegister64(lir);

  Scalar::Type arrayType = lir->mir()->arrayType();
  AtomicOp atomicOp = lir->mir()->operation();

  // Add and Sub don't need |temp| and can save a `mov` when the value and
  // output register are equal to each other.
  if (atomicOp == AtomicOp::Add || atomicOp == AtomicOp::Sub) {
    MOZ_ASSERT(temp == Register64::Invalid());
    MOZ_ASSERT(value == out);
  } else {
    MOZ_ASSERT(out.reg == rax);
  }

  auto dest = ToAddressOrBaseIndex(elements, lir->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.atomicFetchOp64(Synchronization::Full(), atomicOp, value, dest, temp,
                         out);
  });
}

void CodeGenerator::visitAtomicTypedArrayElementBinopForEffect64(
    LAtomicTypedArrayElementBinopForEffect64* lir) {
  MOZ_ASSERT(lir->mir()->isForEffect());

  Register elements = ToRegister(lir->elements());
  Register64 value = ToRegister64(lir->value());

  Scalar::Type arrayType = lir->mir()->arrayType();
  AtomicOp atomicOp = lir->mir()->operation();

  auto dest = ToAddressOrBaseIndex(elements, lir->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.atomicEffectOp64(Synchronization::Full(), atomicOp, value, dest);
  });
}

void CodeGenerator::visitWasmSelectI64(LWasmSelectI64* lir) {
  MOZ_ASSERT(lir->mir()->type() == MIRType::Int64);

  Register cond = ToRegister(lir->condExpr());

  Operand falseExpr = ToOperandOrRegister64(lir->falseExpr());

  Register64 out = ToOutRegister64(lir);
  MOZ_ASSERT(ToRegister64(lir->trueExpr()) == out,
             "true expr is reused for input");

  masm.test32(cond, cond);
  masm.cmovzq(falseExpr, out.reg);
}

// We expect to handle only the cases: compare is {U,}Int{32,64}, and select
// is {U,}Int{32,64}, independently.  Some values may be stack allocated, and
// the "true" input is reused for the output.
void CodeGenerator::visitWasmCompareAndSelect(LWasmCompareAndSelect* ins) {
  bool cmpIs32bit = ins->compareType() == MCompare::Compare_Int32 ||
                    ins->compareType() == MCompare::Compare_UInt32;
  bool cmpIs64bit = ins->compareType() == MCompare::Compare_Int64 ||
                    ins->compareType() == MCompare::Compare_UInt64;
  bool selIs32bit = ins->mir()->type() == MIRType::Int32;
  bool selIs64bit = ins->mir()->type() == MIRType::Int64;

  // Throw out unhandled cases
  MOZ_RELEASE_ASSERT(
      cmpIs32bit != cmpIs64bit && selIs32bit != selIs64bit,
      "CodeGenerator::visitWasmCompareAndSelect: unexpected types");

  using C = Assembler::Condition;
  using R = Register;
  using A = const Address&;

  // Identify macroassembler methods to generate instructions, based on the
  // type of the comparison and the select.  This avoids having to duplicate
  // the code-generation tree below 4 times.  These assignments to
  // `cmpMove_CRRRR` et al are unambiguous as a result of the combination of
  // the template parameters and the 5 argument types ((C, R, R, R, R) etc).
  void (MacroAssembler::*cmpMove_CRRRR)(C, R, R, R, R) = nullptr;
  void (MacroAssembler::*cmpMove_CRARR)(C, R, A, R, R) = nullptr;
  void (MacroAssembler::*cmpLoad_CRRAR)(C, R, R, A, R) = nullptr;
  void (MacroAssembler::*cmpLoad_CRAAR)(C, R, A, A, R) = nullptr;

  if (cmpIs32bit) {
    if (selIs32bit) {
      cmpMove_CRRRR = &MacroAssemblerX64::cmpMove<32, 32>;
      cmpMove_CRARR = &MacroAssemblerX64::cmpMove<32, 32>;
      cmpLoad_CRRAR = &MacroAssemblerX64::cmpLoad<32, 32>;
      cmpLoad_CRAAR = &MacroAssemblerX64::cmpLoad<32, 32>;
    } else {
      cmpMove_CRRRR = &MacroAssemblerX64::cmpMove<32, 64>;
      cmpMove_CRARR = &MacroAssemblerX64::cmpMove<32, 64>;
      cmpLoad_CRRAR = &MacroAssemblerX64::cmpLoad<32, 64>;
      cmpLoad_CRAAR = &MacroAssemblerX64::cmpLoad<32, 64>;
    }
  } else {
    if (selIs32bit) {
      cmpMove_CRRRR = &MacroAssemblerX64::cmpMove<64, 32>;
      cmpMove_CRARR = &MacroAssemblerX64::cmpMove<64, 32>;
      cmpLoad_CRRAR = &MacroAssemblerX64::cmpLoad<64, 32>;
      cmpLoad_CRAAR = &MacroAssemblerX64::cmpLoad<64, 32>;
    } else {
      cmpMove_CRRRR = &MacroAssemblerX64::cmpMove<64, 64>;
      cmpMove_CRARR = &MacroAssemblerX64::cmpMove<64, 64>;
      cmpLoad_CRRAR = &MacroAssemblerX64::cmpLoad<64, 64>;
      cmpLoad_CRAAR = &MacroAssemblerX64::cmpLoad<64, 64>;
    }
  }

  Register trueExprAndDest = ToRegister(ins->output());
  MOZ_ASSERT(ToRegister(ins->ifTrueExpr()) == trueExprAndDest,
             "true expr input is reused for output");

  Assembler::Condition cond = Assembler::InvertCondition(
      JSOpToCondition(ins->compareType(), ins->jsop()));
  const LAllocation* rhs = ins->rightExpr();
  const LAllocation* falseExpr = ins->ifFalseExpr();
  Register lhs = ToRegister(ins->leftExpr());

  // We generate one of four cmp+cmov pairings, depending on whether one of
  // the cmp args and one of the cmov args is in memory or a register.
  if (rhs->isGeneralReg()) {
    if (falseExpr->isGeneralReg()) {
      (masm.*cmpMove_CRRRR)(cond, lhs, ToRegister(rhs), ToRegister(falseExpr),
                            trueExprAndDest);
    } else {
      (masm.*cmpLoad_CRRAR)(cond, lhs, ToRegister(rhs), ToAddress(falseExpr),
                            trueExprAndDest);
    }
  } else {
    if (falseExpr->isGeneralReg()) {
      (masm.*cmpMove_CRARR)(cond, lhs, ToAddress(rhs), ToRegister(falseExpr),
                            trueExprAndDest);
    } else {
      (masm.*cmpLoad_CRAAR)(cond, lhs, ToAddress(rhs), ToAddress(falseExpr),
                            trueExprAndDest);
    }
  }
}

void CodeGeneratorX64::wasmStore(const wasm::MemoryAccessDesc& access,
                                 const LAllocation* value, Operand dstAddr) {
  if (value->isConstant()) {
    masm.memoryBarrierBefore(access.sync());

    const MConstant* mir = value->toConstant();
    Imm32 cst =
        Imm32(mir->type() == MIRType::Int32 ? mir->toInt32() : mir->toInt64());

    switch (access.type()) {
      case Scalar::Int8:
      case Scalar::Uint8: {
        auto before = masm.currentOffset();
        masm.movb(cst, dstAddr);
        auto after = masm.currentOffset();
        masm.appendAndVerify(access, wasm::TrapMachineInsn::Store8,
                             FaultingCodeRange(before, after));
        break;
      }
      case Scalar::Int16:
      case Scalar::Uint16: {
        auto before = masm.currentOffset();
        masm.movw(cst, dstAddr);
        auto after = masm.currentOffset();
        masm.appendAndVerify(access, wasm::TrapMachineInsn::Store16,
                             FaultingCodeRange(before, after));
        break;
      }
      case Scalar::Int32:
      case Scalar::Uint32: {
        auto before = masm.currentOffset();
        masm.movl(cst, dstAddr);
        auto after = masm.currentOffset();
        masm.appendAndVerify(access, wasm::TrapMachineInsn::Store32,
                             FaultingCodeRange(before, after));
        break;
      }
      case Scalar::Int64: {
        MOZ_ASSERT_IF(mir->type() == MIRType::Int64,
                      mozilla::CheckedInt32(mir->toInt64()).isValid());
        auto before = masm.currentOffset();
        masm.movq(cst, dstAddr);
        auto after = masm.currentOffset();
        masm.appendAndVerify(access, wasm::TrapMachineInsn::Store64,
                             FaultingCodeRange(before, after));
        break;
      }
      case Scalar::Simd128:
      case Scalar::Float16:
      case Scalar::Float32:
      case Scalar::Float64:
      case Scalar::Uint8Clamped:
      case Scalar::BigInt64:
      case Scalar::BigUint64:
      case Scalar::MaxTypedArrayViewType:
        MOZ_CRASH("unexpected array type");
    }

    masm.memoryBarrierAfter(access.sync());
  } else {
    masm.wasmStore(access, ToAnyRegister(value), dstAddr);
  }
}

template <typename T>
void CodeGeneratorX64::emitWasmLoad(T* ins) {
  const MWasmLoad* mir = ins->mir();

  mir->access().assertOffsetInGuardPages();
  uint32_t offset = mir->access().offset32();

  // ptr is a GPR and is either a 32-bit value zero-extended to 64-bit, or a
  // true 64-bit value.
  const LAllocation* ptr = ins->ptr();
  Register memoryBase = ToRegister(ins->memoryBase());
  Operand srcAddr =
      ptr->isBogus() ? Operand(memoryBase, offset)
                     : Operand(memoryBase, ToRegister(ptr), TimesOne, offset);

  if (mir->type() == MIRType::Int64) {
    masm.wasmLoadI64(mir->access(), srcAddr, ToOutRegister64(ins));
  } else {
    masm.wasmLoad(mir->access(), srcAddr, ToAnyRegister(ins->output()));
  }
}

void CodeGenerator::visitWasmLoad(LWasmLoad* ins) { emitWasmLoad(ins); }

void CodeGenerator::visitWasmLoadI64(LWasmLoadI64* ins) { emitWasmLoad(ins); }

template <typename T>
void CodeGeneratorX64::emitWasmStore(T* ins) {
  const MWasmStore* mir = ins->mir();
  const wasm::MemoryAccessDesc& access = mir->access();

  mir->access().assertOffsetInGuardPages();
  uint32_t offset = access.offset32();

  const LAllocation* value = ins->value();
  const LAllocation* ptr = ins->ptr();
  Register memoryBase = ToRegister(ins->memoryBase());
  Operand dstAddr =
      ptr->isBogus() ? Operand(memoryBase, offset)
                     : Operand(memoryBase, ToRegister(ptr), TimesOne, offset);

  wasmStore(access, value, dstAddr);
}

void CodeGenerator::visitWasmStore(LWasmStore* ins) { emitWasmStore(ins); }

void CodeGenerator::visitWasmStoreI64(LWasmStoreI64* ins) {
  MOZ_CRASH("Unused on this platform");
}

void CodeGenerator::visitWasmCompareExchangeHeap(
    LWasmCompareExchangeHeap* ins) {
  MWasmCompareExchangeHeap* mir = ins->mir();

  Register ptr = ToRegister(ins->ptr());
  Register oldval = ToRegister(ins->oldValue());
  Register newval = ToRegister(ins->newValue());
  Register memoryBase = ToRegister(ins->memoryBase());

  Scalar::Type accessType = mir->access().type();
  BaseIndex srcAddr(memoryBase, ptr, TimesOne, mir->access().offset32());

  if (accessType == Scalar::Int64) {
    masm.wasmCompareExchange64(mir->access(), srcAddr, Register64(oldval),
                               Register64(newval), ToOutRegister64(ins));
  } else {
    masm.wasmCompareExchange(mir->access(), srcAddr, oldval, newval,
                             ToRegister(ins->output()));
  }
}

void CodeGenerator::visitWasmAtomicExchangeHeap(LWasmAtomicExchangeHeap* ins) {
  MWasmAtomicExchangeHeap* mir = ins->mir();

  Register ptr = ToRegister(ins->ptr());
  Register value = ToRegister(ins->value());
  Register memoryBase = ToRegister(ins->memoryBase());

  Scalar::Type accessType = mir->access().type();

  BaseIndex srcAddr(memoryBase, ptr, TimesOne, mir->access().offset32());

  if (accessType == Scalar::Int64) {
    masm.wasmAtomicExchange64(mir->access(), srcAddr, Register64(value),
                              ToOutRegister64(ins));
  } else {
    masm.wasmAtomicExchange(mir->access(), srcAddr, value,
                            ToRegister(ins->output()));
  }
}

void CodeGenerator::visitWasmAtomicBinopHeap(LWasmAtomicBinopHeap* ins) {
  MWasmAtomicBinopHeap* mir = ins->mir();
  MOZ_ASSERT(mir->hasUses());

  Register ptr = ToRegister(ins->ptr());
  Register memoryBase = ToRegister(ins->memoryBase());
  const LAllocation* value = ins->value();
  Register temp = ToTempRegisterOrInvalid(ins->temp0());
  Register output = ToRegister(ins->output());

  Scalar::Type accessType = mir->access().type();
  if (accessType == Scalar::Uint32) {
    accessType = Scalar::Int32;
  }

  AtomicOp op = mir->operation();
  BaseIndex srcAddr(memoryBase, ptr, TimesOne, mir->access().offset32());

  if (accessType == Scalar::Int64) {
    Register64 val = Register64(ToRegister(value));
    Register64 out = Register64(output);
    Register64 tmp = Register64(temp);
    masm.wasmAtomicFetchOp64(mir->access(), op, val, srcAddr, tmp, out);
  } else if (value->isConstant()) {
    masm.wasmAtomicFetchOp(mir->access(), op, Imm32(ToInt32(value)), srcAddr,
                           temp, output);
  } else {
    masm.wasmAtomicFetchOp(mir->access(), op, ToRegister(value), srcAddr, temp,
                           output);
  }
}

void CodeGenerator::visitWasmAtomicBinopHeapForEffect(
    LWasmAtomicBinopHeapForEffect* ins) {
  MWasmAtomicBinopHeap* mir = ins->mir();
  MOZ_ASSERT(!mir->hasUses());

  Register ptr = ToRegister(ins->ptr());
  Register memoryBase = ToRegister(ins->memoryBase());
  const LAllocation* value = ins->value();

  Scalar::Type accessType = mir->access().type();
  AtomicOp op = mir->operation();

  BaseIndex srcAddr(memoryBase, ptr, TimesOne, mir->access().offset32());

  if (accessType == Scalar::Int64) {
    Register64 val = Register64(ToRegister(value));
    masm.wasmAtomicEffectOp64(mir->access(), op, val, srcAddr);
  } else if (value->isConstant()) {
    Imm32 c(0);
    if (value->toConstant()->type() == MIRType::Int64) {
      c = Imm32(ToInt64(value));
    } else {
      c = Imm32(ToInt32(value));
    }
    masm.wasmAtomicEffectOp(mir->access(), op, c, srcAddr, InvalidReg);
  } else {
    masm.wasmAtomicEffectOp(mir->access(), op, ToRegister(value), srcAddr,
                            InvalidReg);
  }
}

class js::jit::OutOfLineTruncate : public OutOfLineCodeBase<CodeGeneratorX64> {
  FloatRegister input_;
  Register output_;
  Register temp_;

 public:
  OutOfLineTruncate(FloatRegister input, Register output, Register temp)
      : input_(input), output_(output), temp_(temp) {}

  void accept(CodeGeneratorX64* codegen) override {
    codegen->visitOutOfLineTruncate(this);
  }

  FloatRegister input() const { return input_; }
  Register output() const { return output_; }
  Register temp() const { return temp_; }
};

void CodeGeneratorX64::visitOutOfLineTruncate(OutOfLineTruncate* ool) {
  FloatRegister input = ool->input();
  Register output = ool->output();
  Register temp = ool->temp();

  // Inline implementation of `JS::ToInt32(double)` for double values whose
  // exponent is ≥63.

#ifdef DEBUG
  Label ok;
  masm.branchTruncateDoubleMaybeModUint32(input, output, &ok);
  masm.assumeUnreachable("OOL path only used when vcvttsd2sq failed");
  masm.bind(&ok);
#endif

  constexpr uint32_t ShiftedExponentBits =
      mozilla::FloatingPoint<double>::kExponentBits >>
      mozilla::FloatingPoint<double>::kExponentShift;
  static_assert(ShiftedExponentBits == 0x7ff);

  constexpr uint32_t ExponentBiasAndShift =
      mozilla::FloatingPoint<double>::kExponentBias +
      mozilla::FloatingPoint<double>::kExponentShift;
  static_assert(ExponentBiasAndShift == (1023 + 52));

  constexpr size_t ResultWidth = CHAR_BIT * sizeof(int32_t);

  // Extract the bit representation of |input|.
  masm.moveDoubleToGPR64(input, Register64(output));

  // Extract the exponent.
  masm.rshiftPtr(Imm32(mozilla::FloatingPoint<double>::kExponentShift), output,
                 temp);
  masm.and32(Imm32(ShiftedExponentBits), temp);
#ifdef DEBUG
  // The biased exponent must be at least `1023 + 63`, because otherwise
  // vcvttsd2sq wouldn't have failed.
  constexpr uint32_t MinBiasedExponent =
      mozilla::FloatingPoint<double>::kExponentBias + 63;

  Label exponentOk;
  masm.branch32(Assembler::GreaterThanOrEqual, temp, Imm32(MinBiasedExponent),
                &exponentOk);
  masm.assumeUnreachable("exponent is greater-than-or-equals to 63");
  masm.bind(&exponentOk);
#endif
  masm.sub32(Imm32(ExponentBiasAndShift), temp);

  // If the exponent is greater than or equal to |ResultWidth|, the number is
  // either infinite, NaN, or too large to have lower-order bits. We have to
  // return zero in this case.
  {
    ScratchRegisterScope scratch(masm);
    masm.movePtr(ImmWord(0), scratch);
    masm.cmp32MovePtr(Assembler::AboveOrEqual, temp, Imm32(ResultWidth),
                      scratch, output);
  }

  // Negate if the sign bit is set.
  {
    ScratchRegisterScope scratch(masm);
    masm.movePtr(output, scratch);
    masm.negPtr(scratch);
    masm.testPtr(output, output);
    masm.cmovCCq(Assembler::Signed, scratch, output);
  }

  // The significand contains the bits that will determine the final result.
  // Shift those bits left by the exponent value in |temp|.
  masm.lshift32(temp, output);

  // Return from OOL path.
  masm.jump(ool->rejoin());
}

void CodeGenerator::visitTruncateDToInt32(LTruncateDToInt32* ins) {
  FloatRegister input = ToFloatRegister(ins->input());
  Register output = ToRegister(ins->output());
  Register temp = ToRegister(ins->temp0());

  auto* ool = new (alloc()) OutOfLineTruncate(input, output, temp);
  addOutOfLineCode(ool, ins->mir());

  masm.branchTruncateDoubleMaybeModUint32(input, output, ool->entry());
  masm.bind(ool->rejoin());
}

void CodeGenerator::visitWasmBuiltinTruncateDToInt32(
    LWasmBuiltinTruncateDToInt32* lir) {
  FloatRegister input = ToFloatRegister(lir->input());
  Register output = ToRegister(lir->output());
  Register temp = ToRegister(lir->temp0());
  MOZ_ASSERT(lir->instance()->isBogus(), "instance not used for x64");

  auto* ool = new (alloc()) OutOfLineTruncate(input, output, temp);
  addOutOfLineCode(ool, lir->mir());

  masm.branchTruncateDoubleMaybeModUint32(input, output, ool->entry());
  masm.bind(ool->rejoin());
}

void CodeGenerator::visitWasmBuiltinTruncateFToInt32(
    LWasmBuiltinTruncateFToInt32* lir) {
  FloatRegister input = ToFloatRegister(lir->input());
  Register output = ToRegister(lir->output());
  MOZ_ASSERT(lir->instance()->isBogus(), "instance not used for x64");

  masm.truncateFloat32ModUint32(input, output);
}

void CodeGenerator::visitTruncateFToInt32(LTruncateFToInt32* ins) {
  FloatRegister input = ToFloatRegister(ins->input());
  Register output = ToRegister(ins->output());

  masm.truncateFloat32ModUint32(input, output);
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

void CodeGenerator::visitExtendInt32ToInt64(LExtendInt32ToInt64* lir) {
  const LAllocation* input = lir->input();
  Register output = ToRegister(lir->output());

  if (lir->mir()->isUnsigned()) {
    masm.movl(ToOperand(input), output);
  } else {
    masm.movslq(ToOperand(input), output);
  }
}

void CodeGenerator::visitWasmExtendU32Index(LWasmExtendU32Index* lir) {
  // Generates no code on this platform because the input is assumed to have
  // canonical form.
  Register output = ToRegister(lir->output());
  MOZ_ASSERT(ToRegister(lir->input()) == output);
  masm.debugAssertCanonicalInt32(output);
}

void CodeGenerator::visitWasmWrapU32Index(LWasmWrapU32Index* lir) {
  // Generates no code on this platform because the input is assumed to have
  // canonical form.
  Register output = ToRegister(lir->output());
  MOZ_ASSERT(ToRegister(lir->input()) == output);
  masm.debugAssertCanonicalInt32(output);
}

void CodeGenerator::visitSignExtendInt64(LSignExtendInt64* ins) {
  Register64 input = ToRegister64(ins->input());
  Register64 output = ToOutRegister64(ins);
  switch (ins->mir()->mode()) {
    case MSignExtendInt64::Byte:
      masm.movsbq(Operand(input.reg), output.reg);
      break;
    case MSignExtendInt64::Half:
      masm.movswq(Operand(input.reg), output.reg);
      break;
    case MSignExtendInt64::Word:
      masm.movslq(Operand(input.reg), output.reg);
      break;
  }
}

void CodeGenerator::visitWasmTruncateToInt64(LWasmTruncateToInt64* lir) {
  FloatRegister input = ToFloatRegister(lir->input());
  Register64 output = ToOutRegister64(lir);

  MWasmTruncateToInt64* mir = lir->mir();
  MIRType inputType = mir->input()->type();

  MOZ_ASSERT(inputType == MIRType::Double || inputType == MIRType::Float32);

  auto* ool = new (alloc()) OutOfLineWasmTruncateCheck(mir, input, output);
  addOutOfLineCode(ool, mir);

  FloatRegister temp =
      mir->isUnsigned() ? ToFloatRegister(lir->temp0()) : InvalidFloatReg;

  Label* oolEntry = ool->entry();
  Label* oolRejoin = ool->rejoin();
  bool isSaturating = mir->isSaturating();
  if (inputType == MIRType::Double) {
    if (mir->isUnsigned()) {
      masm.wasmTruncateDoubleToUInt64(input, output, isSaturating, oolEntry,
                                      oolRejoin, temp);
    } else {
      masm.wasmTruncateDoubleToInt64(input, output, isSaturating, oolEntry,
                                     oolRejoin, temp);
    }
  } else {
    if (mir->isUnsigned()) {
      masm.wasmTruncateFloat32ToUInt64(input, output, isSaturating, oolEntry,
                                       oolRejoin, temp);
    } else {
      masm.wasmTruncateFloat32ToInt64(input, output, isSaturating, oolEntry,
                                      oolRejoin, temp);
    }
  }
}

void CodeGenerator::visitInt64ToFloatingPoint(LInt64ToFloatingPoint* lir) {
  Register64 input = ToRegister64(lir->input());
  FloatRegister output = ToFloatRegister(lir->output());
  Register temp = ToTempRegisterOrInvalid(lir->temp0());

  MInt64ToFloatingPoint* mir = lir->mir();
  bool isUnsigned = mir->isUnsigned();

  MIRType outputType = mir->type();
  MOZ_ASSERT(outputType == MIRType::Double || outputType == MIRType::Float32);
  MOZ_ASSERT(isUnsigned == (temp != InvalidReg));

  if (outputType == MIRType::Double) {
    if (isUnsigned) {
      masm.convertUInt64ToDouble(input, output, temp);
    } else {
      masm.convertInt64ToDouble(input, output);
    }
  } else {
    if (isUnsigned) {
      masm.convertUInt64ToFloat32(input, output, temp);
    } else {
      masm.convertInt64ToFloat32(input, output);
    }
  }
}

void CodeGenerator::visitBitNotI64(LBitNotI64* ins) {
  LInt64Allocation input = ins->input();
  MOZ_ASSERT(!IsConstant(input));
  Register64 inputR = ToRegister64(input);
  MOZ_ASSERT(inputR == ToOutRegister64(ins));
  masm.notq(inputR.reg);
}

void CodeGenerator::visitAddIntPtr(LAddIntPtr* ins) {
  Register lhs = ToRegister(ins->lhs());
  MOZ_ASSERT(ToRegister(ins->output()) == lhs);

  if (ins->rhs()->isConstant()) {
    masm.addPtr(ImmWord(ToIntPtr(ins->rhs())), lhs);
  } else {
    masm.addq(ToOperand(ins->rhs()), lhs);
  }
}

void CodeGenerator::visitSubIntPtr(LSubIntPtr* ins) {
  Register lhs = ToRegister(ins->lhs());
  MOZ_ASSERT(ToRegister(ins->output()) == lhs);

  if (ins->rhs()->isConstant()) {
    masm.subPtr(ImmWord(ToIntPtr(ins->rhs())), lhs);
  } else {
    masm.subq(ToOperand(ins->rhs()), lhs);
  }
}

void CodeGenerator::visitMulIntPtr(LMulIntPtr* ins) {
  Register lhs = ToRegister(ins->lhs());
  MOZ_ASSERT(ToRegister(ins->output()) == lhs);
  const LAllocation* rhs = ins->rhs();

  if (rhs->isConstant()) {
    intptr_t constant = ToIntPtr(rhs);

    switch (constant) {
      case -1:
        masm.negPtr(lhs);
        return;
      case 0:
        masm.xorPtr(lhs, lhs);
        return;
      case 1:
        return;
      case 2:
        masm.addPtr(lhs, lhs);
        return;
    }

    // Use shift if constant is a power of 2.
    if (constant > 0 && std::has_single_bit(uintptr_t(constant))) {
      uint32_t shift = std::countr_zero(uintptr_t(constant));
      masm.lshiftPtr(Imm32(shift), lhs);
      return;
    }

    masm.mulPtr(ImmWord(constant), lhs);
  } else {
    masm.imulq(ToOperand(rhs), lhs);
  }
}

void CodeGenerator::visitWasmMulI64WideHI64(LWasmMulI64WideHI64* lir) {
  Register lhs = ToRegister(lir->lhs());
  Register rhs = ToRegister(lir->rhs());
  Register temp0 = ToRegister(lir->temp0());
  Register temp1 = ToRegister(lir->temp1());
  Register output = ToRegister(lir->output());
  // This holds because both operands are non-AtStart variants.
  MOZ_ASSERT(output != lhs && output != rhs);
  MOZ_ASSERT(output != temp0 && output != temp1);
  masm.wasmMulI64WideHI64(lhs, rhs, temp0, temp1, output, lir->isSigned());
}
