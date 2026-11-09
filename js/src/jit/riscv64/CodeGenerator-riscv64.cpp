/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/riscv64/CodeGenerator-riscv64.h"

#include "mozilla/MathAlgorithms.h"
#include "mozilla/Maybe.h"

#include <bit>

#include "jit/CodeGenerator.h"
#include "jit/InlineScriptTree.h"
#include "jit/JitRuntime.h"
#include "jit/MIR-wasm.h"
#include "jit/MIR.h"
#include "jit/MIRGraph.h"
#include "jit/RangeAnalysis.h"
#include "jit/ReciprocalMulConstants.h"

#include "jit/shared/CodeGenerator-shared-inl.h"

using namespace js;
using namespace js::jit;

using JS::GenericNaN;
using mozilla::NegativeInfinity;

// shared
CodeGeneratorRiscv64::CodeGeneratorRiscv64(
    MIRGenerator* gen, LIRGraph* graph, MacroAssembler* masm,
    const wasm::CodeMetadata* wasmCodeMeta)
    : CodeGeneratorShared(gen, graph, masm, wasmCodeMeta) {}

void CodeGeneratorRiscv64::branchToBlock(FloatFormat fmt, FloatRegister lhs,
                                         FloatRegister rhs, MBasicBlock* mir,
                                         Assembler::DoubleCondition cond) {
  // Skip past trivial blocks.
  Label* label = skipTrivialBlocks(mir)->lir()->label();
  if (fmt == DoubleFloat) {
    masm.branchDouble(cond, lhs, rhs, label);
  } else {
    masm.branchFloat(cond, lhs, rhs, label);
  }
}

MoveOperand CodeGeneratorRiscv64::toMoveOperand(LAllocation a) const {
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

bool CodeGeneratorRiscv64::generateOutOfLineCode() {
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
    : public OutOfLineCodeBase<CodeGeneratorRiscv64> {
  MTableSwitch* mir_;
  CodeLabel jumpLabel_;

  void accept(CodeGeneratorRiscv64* codegen) {
    codegen->visitOutOfLineTableSwitch(this);
  }

 public:
  explicit OutOfLineTableSwitch(MTableSwitch* mir) : mir_(mir) {}

  MTableSwitch* mir() const { return mir_; }

  CodeLabel* jumpLabel() { return &jumpLabel_; }
};

void CodeGeneratorRiscv64::emitTableSwitchDispatch(MTableSwitch* mir,
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

void CodeGeneratorRiscv64::generateInvalidateEpilogue() {
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

void CodeGeneratorRiscv64::visitOutOfLineTableSwitch(
    OutOfLineTableSwitch* ool) {
  MTableSwitch* mir = ool->mir();

  // Prevent nop and pools sequences to appear in the jump table.
  AutoForbidPoolsAndNops afp(&masm, 2 + mir->numCases() * 2);
  masm.nop();
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

void CodeGeneratorRiscv64::visitOutOfLineWasmTruncateCheck(
    OutOfLineWasmTruncateCheck* ool) {
  MOZ_ASSERT(!ool->isSaturating(),
             "saturating case doesn't require an OOL path");

  FloatRegister input = ool->input();
  Register output = ool->output();
  Register64 output64 = ool->output64();
  MIRType fromType = ool->fromType();
  MIRType toType = ool->toType();
  Label* oolRejoin = ool->rejoin();
  TruncFlags flags = ool->flags();
  wasm::TrapSiteDesc off = ool->trapSiteDesc();

  if (fromType == MIRType::Float32) {
    if (toType == MIRType::Int32) {
      masm.oolWasmTruncateCheckF32ToI32(input, output, flags, off, oolRejoin,
                                        nullptr, nullptr);
    } else if (toType == MIRType::Int64) {
      masm.oolWasmTruncateCheckF32ToI64(input, output64, flags, off, oolRejoin,
                                        nullptr, nullptr);
    } else {
      MOZ_CRASH("unexpected type");
    }
  } else if (fromType == MIRType::Double) {
    if (toType == MIRType::Int32) {
      masm.oolWasmTruncateCheckF64ToI32(input, output, flags, off, oolRejoin,
                                        nullptr, nullptr);
    } else if (toType == MIRType::Int64) {
      masm.oolWasmTruncateCheckF64ToI64(input, output64, flags, off, oolRejoin,
                                        nullptr, nullptr);
    } else {
      MOZ_CRASH("unexpected type");
    }
  } else {
    MOZ_CRASH("unexpected type");
  }

  // The OOL path is only used to execute the correct trap.
  MOZ_ASSERT(!oolRejoin->bound(), "ool path doesn't return");
}

void CodeGenerator::visitBox(LBox* ins) {
  const LAllocation* in = ins->payload();
  ValueOperand result = ToOutValue(ins);

  masm.moveValue(TypedOrValueRegister(ins->type(), ToAnyRegister(in)), result);
}

void CodeGenerator::visitUnbox(LUnbox* ins) {
  MUnbox* mir = ins->mir();

  Register result = ToRegister(ins->output());

  if (mir->fallible()) {
    ValueOperand value = ToValue(ins->input());
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
    bailoutFrom(&bail, ins->snapshot());
    return;
  }

  LAllocation* input = ins->getOperand(LUnbox::Input);
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

void CodeGeneratorRiscv64::emitBigIntPtrDiv(LBigIntPtrDiv* ins,
                                            Register dividend, Register divisor,
                                            Register output) {
  masm.div(output, dividend, divisor);
}

void CodeGeneratorRiscv64::emitBigIntPtrMod(LBigIntPtrMod* ins,
                                            Register dividend, Register divisor,
                                            Register output) {
  masm.rem(output, dividend, divisor);
}

template <class LIR>
static void TrapIfDivideByZero(MacroAssembler& masm, LIR* lir, Register rhs) {
  auto* mir = lir->mir();
  MOZ_ASSERT(mir->trapOnError());

  if (mir->canBeDivideByZero()) {
    Label nonZero;
    masm.ma_b(rhs, rhs, &nonZero, Assembler::NonZero, ShortJump);
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
    masm.bind(&nonZero);
  }
}

void CodeGenerator::visitDivI64(LDivI64* ins) {
  Register lhs = ToRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());
  Register output = ToRegister(ins->output());

  MDiv* div = ins->mir();

  // Handle divide by zero.
  TrapIfDivideByZero(masm, ins, rhs);

  // Handle an integer overflow exception from INT64_MIN / -1.
  if (div->canBeNegativeOverflow()) {
    Label notOverflow;
    masm.branchPtr(Assembler::NotEqual, lhs, ImmWord(INT64_MIN), &notOverflow);
    masm.branchPtr(Assembler::NotEqual, rhs, ImmWord(-1), &notOverflow);
    masm.wasmTrap(wasm::Trap::IntegerOverflow, div->trapSiteDesc());
    masm.bind(&notOverflow);
  }

  masm.div(output, lhs, rhs);
}

template <class LDivOrMod>
static void Divide64WithConstant(MacroAssembler& masm, LDivOrMod* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register output = ToRegister(ins->output());
  int64_t d = ins->denominator();

  UseScratchRegisterScope temps(masm);
  Register temp = temps.Acquire();

  // The absolute value of the denominator isn't a power of 2.
  MOZ_ASSERT(!std::has_single_bit(mozilla::Abs(d)));

  auto* mir = ins->mir();

  // We will first divide by Abs(d), and negate the answer if d is negative.
  // If desired, this can be avoided by generalizing computeDivisionConstants.
  auto rmc = ReciprocalMulConstants::computeSignedDivisionConstants(d);

  // We first compute (M * n) >> 64, where M = rmc.multiplier.
  masm.ma_li(temp, Imm64(uint64_t(rmc.multiplier)));
  masm.mulh(output, lhs, temp);
  if (rmc.multiplier > Int128(INT64_MAX)) {
    MOZ_ASSERT(rmc.multiplier < (Int128(1) << 64));

    // We actually computed output = ((int64_t(M) * n) >> 64) instead. Since
    // (M * n) >> 64 is the same as (output + n), we can correct for the
    // overflow. (output + n) can't overflow, as n and output have opposite
    // signs because int64_t(M) is negative.
    masm.add(output, output, lhs);
  }

  // (M * n) >> (64 + shift) is the truncated division answer if n is
  // non-negative, as proved in the comments of computeDivisionConstants. We
  // must add 1 later if n is negative to get the right answer in all cases.
  if (rmc.shiftAmount > 0) {
    masm.srai(output, output, rmc.shiftAmount);
  }

  // We'll subtract -1 instead of adding 1, because (n < 0 ? -1 : 0) can be
  // computed with just a sign-extending shift of 63 bits.
  if (mir->canBeNegativeDividend()) {
    masm.srai(temp, lhs, 63);
    masm.sub(output, output, temp);
  }

  // After this, |output| contains the correct truncated division result.
  if (d < 0) {
    masm.neg(output, output);
  }
}

void CodeGenerator::visitDivConstantI64(LDivConstantI64* ins) {
  int64_t d = ins->denominator();

  if (d == 0) {
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, ins->mir()->trapSiteDesc());
    return;
  }

  // Compute the truncated division result in rdx.
  Divide64WithConstant(masm, ins);
}

void CodeGenerator::visitDivPowTwoI64(LDivPowTwoI64* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register dest = ToRegister(ins->output());
  int32_t shift = ins->shift();
  MOZ_ASSERT(0 <= shift && shift <= 63);
  bool negativeDivisor = ins->negativeDivisor();
  MDiv* mir = ins->mir();

  if (shift != 0) {
    UseScratchRegisterScope temps(masm);
    Register tmp = temps.Acquire();

    if (mir->isUnsigned()) {
      // shift right
      masm.srli(dest, lhs, shift);
    } else {
      if (mir->canBeNegativeDividend()) {
        // Adjust the value so that shifting produces a correctly rounded result
        // when the numerator is negative. See 10-1 "Signed Division by a Known
        // Power of 2" in Henry S. Warren, Jr.'s Hacker's Delight.
        if (shift > 1) {
          masm.srai(tmp, lhs, 63);
          masm.srli(tmp, tmp, (64 - shift));
        } else {
          masm.srli(tmp, lhs, (64 - shift));
        }
        masm.add(tmp, tmp, lhs);

        // Do the shift.
        masm.srai(dest, tmp, shift);
      } else {
        // Numerator is unsigned, so needs no adjusting. Do the shift.
        masm.srai(dest, lhs, shift);
      }

      if (negativeDivisor) {
        masm.neg(dest, dest);
      }
    }
  } else {
    if (negativeDivisor) {
      // INT64_MIN / -1 overflows.
      Label ok;
      masm.branch64(Assembler::NotEqual, Register64(lhs), Imm64(INT64_MIN),
                    &ok);
      masm.wasmTrap(wasm::Trap::IntegerOverflow, mir->trapSiteDesc());
      masm.bind(&ok);
      masm.neg(dest, lhs);
    } else {
      masm.ma_mv(dest, lhs);
    }
  }
}

void CodeGenerator::visitModI64(LModI64* ins) {
  Register lhs = ToRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());
  Register output = ToRegister(ins->output());

  // rem result table:
  // --------------------------------
  // | Dividend  | Divisor | Result |
  // |------------------------------|
  // |    X      |    0    |   X    |
  // | INT64_MIN |   -1    |   0    |
  // --------------------------------
  //
  // NOTE: INT64_MIN % -1 returns 0, which is the expected result.

  // Handle divide by zero.
  TrapIfDivideByZero(masm, ins, rhs);

  masm.rem(output, lhs, rhs);
}

void CodeGenerator::visitModConstantI64(LModConstantI64* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register output = ToRegister(ins->output());

  int64_t d = ins->denominator();

  if (d == 0) {
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, ins->mir()->trapSiteDesc());
    return;
  }

  // Compute the truncated division result in |output|.
  Divide64WithConstant(masm, ins);

  // Compute the remainder: output = lhs - (output * d).
  masm.ma_mul64(output, output, Imm64(d));
  masm.sub(output, lhs, output);
}

void CodeGenerator::visitModPowTwoI64(LModPowTwoI64* ins) {
  Register in = ToRegister(ins->input());
  Register out = ToRegister(ins->output());

  int32_t shift = ins->shift();
  bool canBeNegative =
      !ins->mir()->isUnsigned() && ins->mir()->canBeNegativeDividend();

  if (shift == 0) {
    masm.ma_mv(out, zero);
    return;
  }

  Label negative;
  if (canBeNegative) {
    // Switch based on sign of the lhs.
    // Positive numbers are just a bitmask.
    masm.ma_b(in, in, &negative, Assembler::Signed, ShortJump);
  }

  masm.ma_and(out, in, Imm64((int64_t(1) << shift) - 1));

  if (canBeNegative) {
    Label done;
    masm.jump(&done);

    // Negative numbers need a negate, bitmask, negate.
    {
      masm.bind(&negative);
      masm.neg(out, in);
      masm.ma_and(out, out, Imm64((int64_t(1) << shift) - 1));
      masm.neg(out, out);
    }

    masm.bind(&done);
  }
}

void CodeGenerator::visitUDivI64(LUDivI64* ins) {
  Register lhs = ToRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());
  Register output = ToRegister(ins->output());

  // Prevent divide by zero.
  TrapIfDivideByZero(masm, ins, rhs);

  masm.divu(output, lhs, rhs);
}

template <class LUDivOrUMod>
static void UnsignedDivide64WithConstant(MacroAssembler& masm,
                                         LUDivOrUMod* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register output = ToRegister(ins->output());
  uint64_t d = ins->denominator();

  UseScratchRegisterScope temps(masm);
  Register temp = temps.Acquire();

  // The denominator isn't a power of 2 (see LDivPowTwoI).
  MOZ_ASSERT(!std::has_single_bit(d));

  auto rmc = ReciprocalMulConstants::computeUnsignedDivisionConstants(d);

  // We first compute (M * n) >> 64, where M = rmc.multiplier.
  masm.ma_li(temp, Imm64(uint64_t(rmc.multiplier)));
  masm.mulhu(output, lhs, temp);
  if (rmc.multiplier > Int128(UINT64_MAX)) {
    // M >= 2^64 and shift == 0 is impossible, as d >= 2 implies that
    // ((M * n) >> (64 + shift)) >= n > floor(n/d) whenever n >= d,
    // contradicting the proof of correctness in computeDivisionConstants.
    MOZ_ASSERT(rmc.shiftAmount > 0);
    MOZ_ASSERT(rmc.multiplier < (Int128(1) << 65));

    // We actually computed output = ((uint64_t(M) * n) >> 64) instead. Since
    // (M * n) >> (64 + shift) is the same as (output + n) >> shift, we can
    // correct for the overflow. This case is a bit trickier than the signed
    // case, though, as the (output + n) addition itself can overflow; however,
    // note that
    // (output + n) >> shift == (((n - output) >> 1) + output) >> (shift - 1),
    // which is overflow-free. See Hacker's Delight, section 10-8 for details.

    masm.sub(temp, lhs, output);
    masm.srli(temp, temp, 1);
    masm.add(output, output, temp);
    masm.srli(output, output, rmc.shiftAmount - 1);
  } else {
    if (rmc.shiftAmount > 0) {
      masm.srli(output, output, rmc.shiftAmount);
    }
  }
}

void CodeGenerator::visitUDivConstantI64(LUDivConstantI64* ins) {
  uint64_t d = ins->denominator();

  if (d == 0) {
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, ins->mir()->trapSiteDesc());
    return;
  }

  // Compute the truncated division result.
  UnsignedDivide64WithConstant(masm, ins);
}

void CodeGenerator::visitUModI64(LUModI64* ins) {
  Register lhs = ToRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());
  Register output = ToRegister(ins->output());

  // Prevent divide by zero.
  TrapIfDivideByZero(masm, ins, rhs);

  masm.remu(output, lhs, rhs);
}

void CodeGenerator::visitUModConstantI64(LUModConstantI64* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register output = ToRegister(ins->output());

  uint64_t d = ins->denominator();

  if (d == 0) {
    masm.wasmTrap(wasm::Trap::IntegerDivideByZero, ins->mir()->trapSiteDesc());
    return;
  }

  // Compute the truncated division result in output.
  UnsignedDivide64WithConstant(masm, ins);

  // Compute the remainder: output = lhs - (output * d).
  masm.ma_mul64(output, output, Imm64(d));
  masm.sub(output, lhs, output);
}

// If we have a constant base ptr, try to add the offset to it, to generate
// better code when the full address is known.  The addition may overflow past
// 32 bits because the front end does nothing special if the base is a large
// constant and base+offset overflows; sidestep this by performing the addition
// anyway, overflowing to 64-bit.
static mozilla::Maybe<uint64_t> ToAbsoluteAddress(
    const LAllocation* ptr, const wasm::MemoryAccessDesc& access) {
  if (ptr->isConstantValue()) {
    const MConstant* c = ptr->toConstant();
    uint64_t baseAddress = c->type() == MIRType::Int32
                               ? uint64_t(uint32_t(c->toInt32()))
                               : uint64_t(c->toInt64());
    uint64_t offset = access.offset32();
    return mozilla::Some(baseAddress + offset);
  }
  return mozilla::Nothing();
}

template <class Inst>
static wasm::ZeroExtendIndex ToZeroExtendIndex(const Inst* ins) {
  // ptr is a GPR and is either a 32-bit value zero-extended to 64-bit, or a
  // true 64-bit value.
  if (ins->mir()->base()->type() == MIRType::Int32) {
    return wasm::ZeroExtendIndex::Yes;
  }
  return wasm::ZeroExtendIndex::No;
}

void CodeGenerator::visitWasmLoadI64(LWasmLoadI64* ins) {
  const MWasmLoad* mir = ins->mir();
  const auto& access = mir->access();

  Register memoryBase = ToRegister(ins->memoryBase());
  Register64 output = ToOutRegister64(ins);

  if (auto address = ToAbsoluteAddress(ins->ptr(), access)) {
    masm.wasmLoadAbsoluteI64(access, memoryBase, address.value(), output);
  } else {
    Register ptr = ToRegister(ins->ptr());
    auto zeroExtend = ToZeroExtendIndex(ins);

    masm.wasmLoadI64(access, memoryBase, ptr, output, zeroExtend);
  }
}

void CodeGenerator::visitWasmStoreI64(LWasmStoreI64* ins) {
  const MWasmStore* mir = ins->mir();
  const auto& access = mir->access();

  Register memoryBase = ToRegister(ins->memoryBase());

  Register64 value = Register64::Invalid();
  if (ins->value().value().isBogus()) {
    value = Register64(zero);
  } else {
    value = ToRegister64(ins->value());
  }

  if (auto address = ToAbsoluteAddress(ins->ptr(), access)) {
    masm.wasmStoreAbsoluteI64(access, value, memoryBase, address.value());
  } else {
    Register ptr = ToRegister(ins->ptr());
    auto zeroExtend = ToZeroExtendIndex(ins);

    masm.wasmStoreI64(access, value, memoryBase, ptr, zeroExtend);
  }
}

void CodeGenerator::visitWasmSelectI64(LWasmSelectI64* ins) {
  MOZ_ASSERT(ins->mir()->type() == MIRType::Int64);

  Register condExpr = ToRegister(ins->condExpr());
  Register64 trueExpr = ToRegister64(ins->trueExpr());
  Register64 falseExpr = ToRegister64(ins->falseExpr());
  Register64 output = ToOutRegister64(ins);

  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();

  masm.ma_cselnz(output.reg, trueExpr.reg, falseExpr.reg, condExpr, scratch);
}

void CodeGenerator::visitExtendInt32ToInt64(LExtendInt32ToInt64* ins) {
  const LAllocation* input = ins->input();
  Register output = ToRegister(ins->output());

  if (ins->mir()->isUnsigned()) {
    masm.move32To64ZeroExtend(ToRegister(input), Register64(output));
  } else {
    masm.SignExtendWord(output, ToRegister(input));
  }
}

void CodeGenerator::visitWrapInt64ToInt32(LWrapInt64ToInt32* ins) {
  LInt64Allocation input = ins->input();
  Register output = ToRegister(ins->output());

  if (ins->mir()->bottomHalf()) {
    if (input.value().isMemory()) {
      masm.load32(ToAddress(input), output);
    } else {
      masm.move64To32(ToRegister64(input), output);
    }
  } else {
    MOZ_CRASH("Not implemented.");
  }
}

void CodeGenerator::visitSignExtendInt64(LSignExtendInt64* ins) {
  Register64 input = ToRegister64(ins->input());
  Register64 output = ToOutRegister64(ins);
  switch (ins->mir()->mode()) {
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

void CodeGenerator::visitWasmExtendU32Index(LWasmExtendU32Index* ins) {
  Register input = ToRegister(ins->input());
  Register output = ToRegister(ins->output());
  MOZ_ASSERT(input == output);
  masm.move32To64ZeroExtend(input, Register64(output));
}

void CodeGenerator::visitWasmWrapU32Index(LWasmWrapU32Index* ins) {
  Register input = ToRegister(ins->input());
  Register output = ToRegister(ins->output());
  MOZ_ASSERT(input == output);
  masm.move64To32(Register64(input), output);
}

void CodeGenerator::visitWasmTruncateToInt64(LWasmTruncateToInt64* ins) {
  FloatRegister input = ToFloatRegister(ins->input());
  Register64 output = ToOutRegister64(ins);

  MWasmTruncateToInt64* mir = ins->mir();
  MIRType fromType = mir->input()->type();

  MOZ_ASSERT(fromType == MIRType::Double || fromType == MIRType::Float32);

  bool isSaturating = mir->isSaturating();

  // RISCV saturating instructions don't require an OOL path.
  OutOfLineWasmTruncateCheck* ool = nullptr;
  Label* oolEntry = nullptr;
  Label* oolRejoin = nullptr;
  if (!isSaturating) {
    ool = new (alloc()) OutOfLineWasmTruncateCheck(mir, input, output);
    addOutOfLineCode(ool, mir);

    oolEntry = ool->entry();
    oolRejoin = ool->rejoin();
  }

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

  // RISCV can handle all success case. The OOL path is only used to execute
  // the correct trap.
  MOZ_ASSERT(!ool || !ool->rejoin()->bound(), "ool path doesn't return");
}

void CodeGenerator::visitInt64ToFloatingPoint(LInt64ToFloatingPoint* ins) {
  Register64 input = ToRegister64(ins->input());
  FloatRegister output = ToFloatRegister(ins->output());

  MIRType outputType = ins->mir()->type();
  MOZ_ASSERT(outputType == MIRType::Double || outputType == MIRType::Float32);

  if (outputType == MIRType::Double) {
    if (ins->mir()->isUnsigned()) {
      masm.convertUInt64ToDouble(input, output, Register::Invalid());
    } else {
      masm.convertInt64ToDouble(input, output);
    }
  } else {
    if (ins->mir()->isUnsigned()) {
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

  if (ins->mir()->isMax()) {
    masm.maxDouble(second, first, handleNaN);
  } else {
    masm.minDouble(second, first, handleNaN);
  }
}

void CodeGenerator::visitMinMaxF(LMinMaxF* ins) {
  FloatRegister first = ToFloatRegister(ins->first());
  FloatRegister second = ToFloatRegister(ins->second());

  MOZ_ASSERT(first == ToFloatRegister(ins->output()));

  const bool handleNaN =
      !ins->mir()->range() || ins->mir()->range()->canBeNaN();

  if (ins->mir()->isMax()) {
    masm.maxFloat32(second, first, handleNaN);
  } else {
    masm.minFloat32(second, first, handleNaN);
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
      masm.ma_add32(ToRegister(dest), ToRegister(lhs), Imm32(ToInt32(rhs)));
    } else {
      masm.addw(ToRegister(dest), ToRegister(lhs), ToRegister(rhs));
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
    masm.ma_add64(dest, lhs, Imm64(ToIntPtr(rhs)));
  } else {
    masm.add(dest, lhs, ToRegister(rhs));
  }
}

void CodeGenerator::visitAddI64(LAddI64* ins) {
  Register lhs = ToRegister64(ins->lhs()).reg;
  LInt64Allocation rhs = ins->rhs();
  Register dest = ToOutRegister64(ins).reg;

  if (IsConstant(rhs)) {
    masm.ma_add64(dest, lhs, Imm64(ToInt64(rhs)));
  } else {
    masm.add(dest, lhs, ToRegister64(rhs).reg);
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
      masm.ma_sub32(ToRegister(dest), ToRegister(lhs), Imm32(ToInt32(rhs)));
    } else {
      masm.subw(ToRegister(dest), ToRegister(lhs), ToRegister(rhs));
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
    masm.ma_sub64(dest, lhs, Imm64(ToIntPtr(rhs)));
  } else {
    masm.sub(dest, lhs, ToRegister(rhs));
  }
}

void CodeGenerator::visitSubI64(LSubI64* ins) {
  Register lhs = ToRegister64(ins->lhs()).reg;
  LInt64Allocation rhs = ins->rhs();
  Register dest = ToOutRegister64(ins).reg;

  if (IsConstant(rhs)) {
    masm.ma_sub64(dest, lhs, Imm64(ToInt64(rhs)));
  } else {
    masm.sub(dest, lhs, ToRegister64(rhs).reg);
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
        masm.ma_mul32(dest, lhs, Imm32(constant));
        return;
    }

    if (mul->canOverflow()) {
      Label mulConstOverflow;
      masm.ma_mul32TestOverflow(dest, lhs, Imm32(constant), &mulConstOverflow);

      bailoutFrom(&mulConstOverflow, ins->snapshot());
    } else {
      masm.ma_mul32(dest, lhs, Imm32(constant));
    }
  } else {
    if (mul->canOverflow()) {
      Label multRegOverflow;
      masm.ma_mul32TestOverflow(dest, lhs, ToRegister(rhs), &multRegOverflow);

      bailoutFrom(&multRegOverflow, ins->snapshot());
    } else {
      masm.mulw(dest, lhs, ToRegister(rhs));
    }

    if (mul->canBeNegativeZero()) {
      Label done;
      masm.ma_b(dest, dest, &done, Assembler::NonZero, ShortJump);

      // Result is -0 if lhs or rhs is negative.
      // In that case result must be double value so bailout
      UseScratchRegisterScope temps(&masm);
      Register scratch = temps.Acquire();
      masm.or_(scratch, lhs, ToRegister(rhs));
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
    masm.ma_mul64(dest, lhs, Imm64(ToIntPtr(rhs)));
  } else {
    masm.mul(dest, lhs, ToRegister(rhs));
  }
}

void CodeGenerator::visitMulI64(LMulI64* ins) {
  Register lhs = ToRegister64(ins->lhs()).reg;
  LInt64Allocation rhs = ins->rhs();
  Register dest = ToOutRegister64(ins).reg;

  if (IsConstant(rhs)) {
    masm.ma_mul64(dest, lhs, Imm64(ToInt64(rhs)));
  } else {
    masm.mul(dest, lhs, ToRegister64(rhs).reg);
  }
}

void CodeGenerator::visitDivI(LDivI* ins) {
  Register lhs = ToRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());
  Register dest = ToRegister(ins->output());
  MDiv* mir = ins->mir();

  // divw result table:
  // ------------------------------------
  // | Dividend   | Divisor |   Result  |
  // |----------------------------------|
  // |    X       |    0    |    -1     |
  // | INT32_MIN  |   -1    | INT32_MIN |
  // ------------------------------------
  //
  // NOTE: INT32_MIN / -1 returns INT32_MIN, which is the expected (truncated)
  // result. Division by zero returns -1, whereas the truncated result should
  // be 0, so it needs to be handled explicitly.

  Label done;

  // Handle divide by zero.
  if (mir->canBeDivideByZero()) {
    if (mir->trapOnError()) {
      TrapIfDivideByZero(masm, ins, rhs);
    } else if (mir->canTruncateInfinities()) {
      // Truncated division by zero is zero (Infinity|0 == 0)
      Label notzero;
      masm.ma_b(rhs, rhs, &notzero, Assembler::NonZero, ShortJump);
      masm.move32(Imm32(0), dest);
      masm.jump(&done);
      masm.bind(&notzero);
    } else {
      MOZ_ASSERT(mir->fallible());
      bailoutCmp32(Assembler::Zero, rhs, rhs, ins->snapshot());
    }
  }

  // Handle an integer overflow from (INT32_MIN / -1).
  // The integer division gives INT32_MIN, but should be -(double)INT32_MIN.
  if (mir->canBeNegativeOverflow() &&
      (mir->trapOnError() || !mir->canTruncateOverflow())) {
    Label notMinInt;
    masm.ma_b(lhs, Imm32(INT32_MIN), &notMinInt, Assembler::NotEqual,
              ShortJump);

    if (mir->trapOnError()) {
      Label ok;
      masm.ma_b(rhs, Imm32(-1), &ok, Assembler::NotEqual, ShortJump);
      masm.wasmTrap(wasm::Trap::IntegerOverflow, mir->trapSiteDesc());
      masm.bind(&ok);
    } else {
      MOZ_ASSERT(mir->fallible());
      bailoutCmp32(Assembler::Equal, rhs, Imm32(-1), ins->snapshot());
    }
    masm.bind(&notMinInt);
  }

  // Handle negative zero: lhs == 0 && rhs < 0.
  if (!mir->canTruncateNegativeZero() && mir->canBeNegativeZero()) {
    Label nonzero;
    masm.ma_b(lhs, lhs, &nonzero, Assembler::NonZero, ShortJump);
    bailoutCmp32(Assembler::LessThan, rhs, Imm32(0), ins->snapshot());
    masm.bind(&nonzero);
  }

  // All regular. Lets call div.
  if (mir->canTruncateRemainder()) {
    masm.divw(dest, lhs, rhs);
  } else {
    MOZ_ASSERT(mir->fallible());
    MOZ_ASSERT(lhs != dest && rhs != dest);

    UseScratchRegisterScope temps(masm);
    Register temp = temps.Acquire();

    // The recommended code sequence to obtain both the quotient and remainder
    // is div[u] followed by mod[u].
    masm.divw(dest, lhs, rhs);
    masm.remw(temp, lhs, rhs);

    // If the remainder is != 0, bailout since this must be a double.
    bailoutCmp32(Assembler::NonZero, temp, temp, ins->snapshot());
  }

  masm.bind(&done);
}

void CodeGenerator::visitDivPowTwoI(LDivPowTwoI* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register dest = ToRegister(ins->output());
  int32_t shift = ins->shift();
  MOZ_ASSERT(0 <= shift && shift <= 31);
  bool negativeDivisor = ins->negativeDivisor();
  MDiv* mir = ins->mir();

  if (!mir->isTruncated() && negativeDivisor) {
    // 0 divided by a negative number returns a -0 double.
    bailoutTest32(Assembler::Zero, lhs, lhs, ins->snapshot());
  }

  if (shift != 0) {
    UseScratchRegisterScope temps(masm);
    Register tmp = temps.Acquire();

    if (!mir->isTruncated()) {
      // If the remainder is going to be != 0, bailout since this must
      // be a double.
      masm.slliw(tmp, lhs, (32 - shift));
      bailoutTest32(Assembler::NonZero, tmp, tmp, ins->snapshot());
    }

    if (mir->isUnsigned()) {
      // shift right
      masm.srliw(dest, lhs, shift);
    } else {
      if (mir->canBeNegativeDividend() && mir->isTruncated()) {
        // Adjust the value so that shifting produces a correctly rounded result
        // when the numerator is negative. See 10-1 "Signed Division by a Known
        // Power of 2" in Henry S. Warren, Jr.'s Hacker's Delight.
        if (shift > 1) {
          masm.sraiw(tmp, lhs, 31);
          masm.srliw(tmp, tmp, (32 - shift));
        } else {
          masm.srliw(tmp, lhs, (32 - shift));
        }
        masm.addw(tmp, tmp, lhs);

        // Do the shift.
        masm.sraiw(dest, tmp, shift);
      } else {
        // Numerator is unsigned, so needs no adjusting. Do the shift.
        masm.sraiw(dest, lhs, shift);
      }

      if (negativeDivisor) {
        masm.negw(dest, dest);
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
      masm.negw(dest, lhs);
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

template <class LDivOrMod>
static void DivideWithConstant(MacroAssembler& masm, LDivOrMod* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register output = ToRegister(ins->output());
  int32_t d = ins->denominator();

  UseScratchRegisterScope temps(masm);
  Register temp = temps.Acquire();

  // The absolute value of the denominator isn't a power of 2.
  MOZ_ASSERT(!std::has_single_bit(mozilla::Abs(d)));

  auto* mir = ins->mir();

  // We will first divide by Abs(d), and negate the answer if d is negative.
  // If desired, this can be avoided by generalizing computeDivisionConstants.
  auto rmc = ReciprocalMulConstants::computeSignedDivisionConstants(d);

  // We first compute (M * n) >> 32, where M = rmc.multiplier.
  masm.ma_li(temp, Imm32(rmc.multiplier));
  masm.mul(output, lhs, temp);
  if (rmc.multiplier > INT32_MAX || rmc.shiftAmount == 0) {
    masm.srli(output, output, 32);
  }
  if (rmc.multiplier > INT32_MAX) {
    MOZ_ASSERT(rmc.multiplier < (int64_t(1) << 32));

    // We actually computed output = ((int32_t(M) * n) >> 32) instead. Since
    // (M * n) >> 32 is the same as (output + n), we can correct for the
    // overflow. (output + n) can't overflow, as n and |output| have opposite
    // signs because int32_t(M) is negative.
    masm.addw(output, output, lhs);
  }

  // (M * n) >> (32 + shift) is the truncated division answer if n is
  // non-negative, as proved in the comments of computeDivisionConstants. We
  // must add 1 later if n is negative to get the right answer in all cases.
  if (rmc.shiftAmount > 0) {
    if (rmc.multiplier > INT32_MAX) {
      masm.sraiw(output, output, rmc.shiftAmount);
    } else {
      masm.srai(output, output, 32 + rmc.shiftAmount);
    }
  }

  // We'll subtract -1 instead of adding 1, because (n < 0 ? -1 : 0) can be
  // computed with just a sign-extending shift of 31 bits.
  if (mir->canBeNegativeDividend()) {
    masm.sraiw(temp, lhs, 31);
    masm.subw(output, output, temp);
  }

  // After this, |output| contains the correct truncated division result.
  if (d < 0) {
    masm.negw(output, output);
  }
}

void CodeGenerator::visitDivConstantI(LDivConstantI* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register output = ToRegister(ins->output());
  int32_t d = ins->denominator();

  MDiv* mir = ins->mir();

  if (d == 0) {
    if (mir->trapOnError()) {
      masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
    } else if (mir->canTruncateInfinities()) {
      masm.ma_mv(output, zero);
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

    masm.ma_mul32(temp, output, Imm32(d));
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
  MMod* mir = ins->mir();
  Label done;

  // remw result table:
  // --------------------------------
  // | Dividend  | Divisor | Result |
  // |------------------------------|
  // |    X      |    0    |   X    |
  // | INT32_MIN |   -1    |   0    |
  // --------------------------------
  //
  // NOTE: INT32_MIN % -1 returns 0, which is the expected result.

  // Prevent divide by zero.
  if (mir->canBeDivideByZero()) {
    if (mir->trapOnError()) {
      TrapIfDivideByZero(masm, ins, rhs);
    } else if (mir->isTruncated()) {
      // Truncated division by zero yields integer zero.
      Label yNonZero;
      masm.ma_b(rhs, Imm32(0), &yNonZero, Assembler::NotEqual, ShortJump);
      masm.move32(Imm32(0), dest);
      masm.jump(&done);
      masm.bind(&yNonZero);
    } else {
      // Non-truncated division by zero produces a non-integer.
      MOZ_ASSERT(mir->fallible());
      bailoutCmp32(Assembler::Zero, rhs, rhs, ins->snapshot());
    }
  }

  masm.remw(dest, lhs, rhs);

  if (mir->canBeNegativeDividend() && !mir->isTruncated()) {
    MOZ_ASSERT(mir->fallible());
    MOZ_ASSERT(lhs != dest);

    // If dest == 0 and lhs < 0, then the result should be double -0.0.
    // Note that this guard handles lhs == INT_MIN and rhs == -1.

    masm.ma_b(dest, Imm32(0), &done, Assembler::NotEqual, ShortJump);
    bailoutCmp32(Assembler::Signed, lhs, lhs, ins->snapshot());
  }
  masm.bind(&done);
}

void CodeGenerator::visitModConstantI(LModConstantI* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register output = ToRegister(ins->output());

  MMod* mir = ins->mir();

  int32_t d = ins->denominator();
  if (d == 0) {
    if (mir->trapOnError()) {
      masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
    } else if (mir->isTruncated()) {
      masm.ma_mv(output, zero);
    } else {
      MOZ_ASSERT(mir->fallible());
      bailout(ins->snapshot());
    }
    return;
  }

  // Compute the truncated division result in |output|.
  DivideWithConstant(masm, ins);

  // Compute the remainder: output = lhs - (output * d).
  masm.ma_mul32(output, output, Imm32(d));
  masm.subw(output, lhs, output);

  if (mir->canBeNegativeDividend() && !mir->isTruncated()) {
    MOZ_ASSERT(mir->fallible());

    // If output == 0 and lhs < 0, then the result should be double -0.0.
    Label done;
    masm.ma_b(output, Imm32(0), &done, Assembler::NotEqual, ShortJump);
    bailoutCmp32(Assembler::Signed, lhs, lhs, ins->snapshot());
    masm.bind(&done);
  }
}

void CodeGenerator::visitModPowTwoI(LModPowTwoI* ins) {
  Register in = ToRegister(ins->input());
  Register out = ToRegister(ins->output());

  MMod* mir = ins->mir();
  int32_t shift = ins->shift();
  bool canBeNegative = !mir->isUnsigned() && mir->canBeNegativeDividend();

  if (shift == 0) {
    if (canBeNegative && !mir->isTruncated()) {
      bailoutTest32(Assembler::Signed, in, in, ins->snapshot());
    }
    masm.ma_mv(out, zero);
    return;
  }

  Label negative;
  if (canBeNegative) {
    // Switch based on sign of the lhs.
    // Positive numbers are just a bitmask
    masm.ma_b(in, in, &negative, Assembler::Signed, ShortJump);
  }

  masm.ma_and(out, in, Imm32((1 << shift) - 1));

  if (canBeNegative) {
    Label done;
    masm.jump(&done);

    // Negative numbers need a negate, bitmask, negate
    {
      masm.bind(&negative);
      masm.negw(out, in);
      masm.ma_and(out, out, Imm32((1 << shift) - 1));
      masm.negw(out, out);
    }

    // Since a%b has the same sign as b, and a is negative in this branch,
    // an answer of 0 means the correct result is actually -0. Bail out.
    if (!mir->isTruncated()) {
      MOZ_ASSERT(mir->fallible());
      bailoutCmp32(Assembler::Equal, out, zero, ins->snapshot());
    } else {
      // -0|0 == 0
    }

    masm.bind(&done);
  }
}

void CodeGenerator::visitBitNotI(LBitNotI* ins) {
  Register input = ToRegister(ins->input());
  Register dest = ToRegister(ins->output());
  masm.not_(dest, input);
}

void CodeGenerator::visitBitNotI64(LBitNotI64* ins) {
  Register input = ToRegister64(ins->input()).reg;
  Register dest = ToOutRegister64(ins).reg;
  masm.not_(dest, input);
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
        masm.or_(dest, lhs, ToRegister(rhs));
        masm.SignExtendWord(dest, dest);
      }
      break;
    case JSOp::BitXor:
      if (rhs->isConstant()) {
        masm.ma_xor(dest, lhs, Imm32(ToInt32(rhs)));
      } else {
        masm.xor_(dest, lhs, ToRegister(rhs));
        masm.SignExtendWord(dest, dest);
      }
      break;
    case JSOp::BitAnd:
      if (rhs->isConstant()) {
        masm.ma_and(dest, lhs, Imm32(ToInt32(rhs)));
      } else {
        masm.and_(dest, lhs, ToRegister(rhs));
        masm.SignExtendWord(dest, dest);
      }
      break;
    default:
      MOZ_CRASH("unexpected binary opcode");
  }
}

void CodeGenerator::visitBitOpI64(LBitOpI64* ins) {
  Register lhs = ToRegister64(ins->lhs()).reg;
  LInt64Allocation rhs = ins->rhs();
  Register dest = ToOutRegister64(ins).reg;

  switch (ins->bitop()) {
    case JSOp::BitOr:
      if (IsConstant(rhs)) {
        masm.ma_or(dest, lhs, Imm64(ToInt64(rhs)));
      } else {
        masm.or_(dest, lhs, ToRegister64(rhs).reg);
      }
      break;
    case JSOp::BitXor:
      if (IsConstant(rhs)) {
        masm.ma_xor(dest, lhs, Imm64(ToInt64(rhs)));
      } else {
        masm.xor_(dest, lhs, ToRegister64(rhs).reg);
      }
      break;
    case JSOp::BitAnd:
      if (IsConstant(rhs)) {
        masm.ma_and(dest, lhs, Imm64(ToInt64(rhs)));
      } else {
        masm.and_(dest, lhs, ToRegister64(rhs).reg);
      }
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
          masm.slliw(dest, lhs, shift);
        } else {
          masm.move32(lhs, dest);
        }
        break;
      case JSOp::Rsh:
        if (shift) {
          masm.sraiw(dest, lhs, shift);
        } else {
          masm.move32(lhs, dest);
        }
        break;
      case JSOp::Ursh:
        if (shift) {
          masm.srliw(dest, lhs, shift);
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
    switch (ins->bitop()) {
      case JSOp::Lsh:
        masm.sllw(dest, lhs, ToRegister(rhs));
        break;
      case JSOp::Rsh:
        masm.sraw(dest, lhs, ToRegister(rhs));
        break;
      case JSOp::Ursh:
        masm.srlw(dest, lhs, ToRegister(rhs));
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
    auto shamt = ToIntPtr(rhs) & 0x3F;
    if (shamt) {
      switch (ins->bitop()) {
        case JSOp::Lsh:
          masm.slli(dest, lhs, shamt);
          break;
        case JSOp::Rsh:
          masm.srai(dest, lhs, shamt);
          break;
        case JSOp::Ursh:
          masm.srli(dest, lhs, shamt);
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
        masm.sll(dest, lhs, shift);
        break;
      case JSOp::Rsh:
        masm.sra(dest, lhs, shift);
        break;
      case JSOp::Ursh:
        masm.srl(dest, lhs, shift);
        break;
      default:
        MOZ_CRASH("Unexpected shift op");
    }
  }
}

void CodeGenerator::visitShiftI64(LShiftI64* ins) {
  Register lhs = ToRegister64(ins->lhs()).reg;
  const LAllocation* rhs = ins->rhs();
  Register dest = ToOutRegister64(ins).reg;

  if (rhs->isConstant()) {
    int32_t shift = int32_t(rhs->toConstant()->toInt64() & 0x3F);
    if (shift) {
      switch (ins->bitop()) {
        case JSOp::Lsh:
          masm.slli(dest, lhs, shift);
          break;
        case JSOp::Rsh:
          masm.srai(dest, lhs, shift);
          break;
        case JSOp::Ursh:
          masm.srli(dest, lhs, shift);
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
  switch (ins->bitop()) {
    case JSOp::Lsh:
      masm.sll(dest, lhs, shift);
      break;
    case JSOp::Rsh:
      masm.sra(dest, lhs, shift);
      break;
    case JSOp::Ursh:
      masm.srl(dest, lhs, shift);
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
    masm.srliw(temp, lhs, ToInt32(rhs) & 0x1f);
  } else {
    masm.srlw(temp, lhs, ToRegister(rhs));
  }

  masm.convertUInt32ToDouble(temp, out);
}

void CodeGenerator::visitPowHalfD(LPowHalfD* ins) {
  const FloatRegister input = ToFloatRegister(ins->input());
  const FloatRegister output = ToFloatRegister(ins->output());
  ScratchDoubleScope fpscratch(masm);

  FloatRegister fsqrtRs1 = input;

  Label done, notNegInf;
  if (!ins->mir()->operandIsNeverNegativeInfinity()) {
    // Masm.pow(-Infinity, 0.5) == Infinity.
    masm.loadConstantDouble(NegativeInfinity<double>(), fpscratch);
    masm.BranchFloat64(Assembler::DoubleNotEqualOrUnordered, input, fpscratch,
                       &notNegInf, ShortJump);
    {
      masm.fneg_d(output, fpscratch);
      masm.jump(&done);
    }
    masm.bind(&notNegInf);
  }

  if (!ins->mir()->operandIsNeverNegativeZero()) {
    // Math.pow(-0, 0.5) == 0 == Math.pow(0, 0.5).
    // Adding 0 converts any -0 to 0.
    masm.loadConstantDouble(0.0, fpscratch);
    masm.fadd_d(output, input, fpscratch);
    fsqrtRs1 = output;
  }
  masm.fsqrt_d(output, fsqrtRs1);

  if (!ins->mir()->operandIsNeverNegativeInfinity()) {
    masm.bind(&done);
  }
}

void CodeGenerator::visitMathD(LMathD* ins) {
  FloatRegister src1 = ToFloatRegister(ins->lhs());
  FloatRegister src2 = ToFloatRegister(ins->rhs());
  FloatRegister output = ToFloatRegister(ins->output());

  switch (ins->jsop()) {
    case JSOp::Add:
      masm.fadd_d(output, src1, src2);
      break;
    case JSOp::Sub:
      masm.fsub_d(output, src1, src2);
      break;
    case JSOp::Mul:
      masm.fmul_d(output, src1, src2);
      break;
    case JSOp::Div:
      masm.fdiv_d(output, src1, src2);
      break;
    default:
      MOZ_CRASH("unexpected opcode");
  }
}

void CodeGenerator::visitMathF(LMathF* ins) {
  FloatRegister src1 = ToFloatRegister(ins->lhs());
  FloatRegister src2 = ToFloatRegister(ins->rhs());
  FloatRegister output = ToFloatRegister(ins->output());

  switch (ins->jsop()) {
    case JSOp::Add:
      masm.fadd_s(output, src1, src2);
      break;
    case JSOp::Sub:
      masm.fsub_s(output, src1, src2);
      break;
    case JSOp::Mul:
      masm.fmul_s(output, src1, src2);
      break;
    case JSOp::Div:
      masm.fdiv_s(output, src1, src2);
      break;
    default:
      MOZ_CRASH("unexpected opcode");
  }
}

void CodeGenerator::visitTruncateDToInt32(LTruncateDToInt32* ins) {
  // Directly call fcvtmod.w.d if available to avoid generating unused OOL code
  // in emitTruncateDouble.
  if (MacroAssembler::HasZfaExtension()) {
    masm.fcvtmod_w_d(ToRegister(ins->output()), ToFloatRegister(ins->input()));
  } else {
    emitTruncateDouble(ToFloatRegister(ins->input()), ToRegister(ins->output()),
                       ins->mir());
  }
}

void CodeGenerator::visitTruncateFToInt32(LTruncateFToInt32* ins) {
  masm.truncateFloat32ModUint32(ToFloatRegister(ins->input()),
                                ToRegister(ins->output()));
}

void CodeGenerator::visitWasmBuiltinTruncateDToInt32(
    LWasmBuiltinTruncateDToInt32* ins) {
  // Directly call fcvtmod.w.d if available to avoid generating unused OOL code
  // in emitTruncateDouble.
  if (MacroAssembler::HasZfaExtension()) {
    masm.fcvtmod_w_d(ToRegister(ins->output()), ToFloatRegister(ins->input()));
  } else {
    emitTruncateDouble(ToFloatRegister(ins->input()), ToRegister(ins->output()),
                       ins->mir());
  }
}

void CodeGenerator::visitWasmBuiltinTruncateFToInt32(
    LWasmBuiltinTruncateFToInt32* ins) {
  MOZ_ASSERT(ins->instance()->isBogus(), "instance not used for riscv64");
  masm.truncateFloat32ModUint32(ToFloatRegister(ins->input()),
                                ToRegister(ins->output()));
}

void CodeGenerator::visitWasmTruncateToInt32(LWasmTruncateToInt32* ins) {
  auto input = ToFloatRegister(ins->input());
  auto output = ToRegister(ins->output());

  MWasmTruncateToInt32* mir = ins->mir();
  MIRType fromType = mir->input()->type();

  MOZ_ASSERT(fromType == MIRType::Double || fromType == MIRType::Float32);

  bool isSaturating = mir->isSaturating();

  // RISCV saturating instructions don't require an OOL path.
  OutOfLineWasmTruncateCheck* ool = nullptr;
  Label* oolEntry = nullptr;
  if (!isSaturating) {
    ool = new (alloc()) OutOfLineWasmTruncateCheck(mir, input, output);
    addOutOfLineCode(ool, mir);

    oolEntry = ool->entry();
  }

  if (fromType == MIRType::Double) {
    if (mir->isUnsigned()) {
      masm.wasmTruncateDoubleToUInt32(input, output, isSaturating, oolEntry);
    } else {
      masm.wasmTruncateDoubleToInt32(input, output, isSaturating, oolEntry);
    }
  } else {
    if (mir->isUnsigned()) {
      masm.wasmTruncateFloat32ToUInt32(input, output, isSaturating, oolEntry);
    } else {
      masm.wasmTruncateFloat32ToInt32(input, output, isSaturating, oolEntry);
    }
  }

  // RISCV can handle all success case. The OOL path is only used to execute
  // the correct trap.
  MOZ_ASSERT(!ool || !ool->rejoin()->bound(), "ool path doesn't return");
}

void CodeGenerator::visitTestDAndBranch(LTestDAndBranch* ins) {
  FloatRegister input = ToFloatRegister(ins->input());
  ScratchDoubleScope fpscratch(masm);

  MBasicBlock* ifTrue = ins->ifTrue();
  MBasicBlock* ifFalse = ins->ifFalse();

  masm.loadConstantDouble(0.0, fpscratch);
  // If 0, or NaN, the result is false.
  if (isNextBlock(ifFalse->lir())) {
    branchToBlock(DoubleFloat, input, fpscratch, ifTrue,
                  Assembler::DoubleNotEqual);
  } else {
    branchToBlock(DoubleFloat, input, fpscratch, ifFalse,
                  Assembler::DoubleEqualOrUnordered);
    jumpToBlock(ifTrue);
  }
}

void CodeGenerator::visitTestFAndBranch(LTestFAndBranch* ins) {
  FloatRegister input = ToFloatRegister(ins->input());
  ScratchFloat32Scope fpscratch(masm);

  MBasicBlock* ifTrue = ins->ifTrue();
  MBasicBlock* ifFalse = ins->ifFalse();

  masm.loadConstantFloat32(0.0f, fpscratch);
  // If 0, or NaN, the result is false.

  if (isNextBlock(ifFalse->lir())) {
    branchToBlock(SingleFloat, input, fpscratch, ifTrue,
                  Assembler::DoubleNotEqual);
  } else {
    branchToBlock(SingleFloat, input, fpscratch, ifFalse,
                  Assembler::DoubleEqualOrUnordered);
    jumpToBlock(ifTrue);
  }
}

void CodeGenerator::visitCompareD(LCompareD* ins) {
  FloatRegister lhs = ToFloatRegister(ins->left());
  FloatRegister rhs = ToFloatRegister(ins->right());
  Register dest = ToRegister(ins->output());

  Assembler::DoubleCondition cond = JSOpToDoubleCondition(ins->mir()->jsop());
  masm.ma_compareF64(dest, cond, lhs, rhs);
}

void CodeGenerator::visitCompareF(LCompareF* ins) {
  FloatRegister lhs = ToFloatRegister(ins->left());
  FloatRegister rhs = ToFloatRegister(ins->right());
  Register dest = ToRegister(ins->output());

  Assembler::DoubleCondition cond = JSOpToDoubleCondition(ins->mir()->jsop());
  masm.ma_compareF32(dest, cond, lhs, rhs);
}

void CodeGenerator::visitCompareDAndBranch(LCompareDAndBranch* ins) {
  FloatRegister lhs = ToFloatRegister(ins->left());
  FloatRegister rhs = ToFloatRegister(ins->right());

  Assembler::DoubleCondition cond =
      JSOpToDoubleCondition(ins->cmpMir()->jsop());
  MBasicBlock* ifTrue = ins->ifTrue();
  MBasicBlock* ifFalse = ins->ifFalse();

  if (isNextBlock(ifFalse->lir())) {
    branchToBlock(DoubleFloat, lhs, rhs, ifTrue, cond);
  } else {
    branchToBlock(DoubleFloat, lhs, rhs, ifFalse,
                  Assembler::InvertCondition(cond));
    jumpToBlock(ifTrue);
  }
}

void CodeGenerator::visitCompareFAndBranch(LCompareFAndBranch* ins) {
  FloatRegister lhs = ToFloatRegister(ins->left());
  FloatRegister rhs = ToFloatRegister(ins->right());

  Assembler::DoubleCondition cond =
      JSOpToDoubleCondition(ins->cmpMir()->jsop());
  MBasicBlock* ifTrue = ins->ifTrue();
  MBasicBlock* ifFalse = ins->ifFalse();

  if (isNextBlock(ifFalse->lir())) {
    branchToBlock(SingleFloat, lhs, rhs, ifTrue, cond);
  } else {
    branchToBlock(SingleFloat, lhs, rhs, ifFalse,
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
  masm.ma_compareF64(dest, Assembler::DoubleEqualOrUnordered, in, fpscratch);
}

void CodeGenerator::visitNotF(LNotF* ins) {
  // Since this operation is not, we want to set a bit if
  // the float32 is falsey, which means 0.0, -0.0 or NaN.
  FloatRegister in = ToFloatRegister(ins->input());
  Register dest = ToRegister(ins->output());
  ScratchFloat32Scope fpscratch(masm);

  masm.loadConstantFloat32(0.0f, fpscratch);
  masm.ma_compareF32(dest, Assembler::DoubleEqualOrUnordered, in, fpscratch);
}

void CodeGenerator::visitWasmLoad(LWasmLoad* ins) {
  const MWasmLoad* mir = ins->mir();
  const auto& access = mir->access();

  Register memoryBase = ToRegister(ins->memoryBase());
  AnyRegister output = ToAnyRegister(ins->output());

  if (auto address = ToAbsoluteAddress(ins->ptr(), access)) {
    masm.wasmLoadAbsolute(access, memoryBase, address.value(), output);
  } else {
    Register ptr = ToRegister(ins->ptr());
    auto zeroExtend = ToZeroExtendIndex(ins);

    masm.wasmLoad(access, memoryBase, ptr, output, zeroExtend);
  }
}

void CodeGenerator::visitWasmStore(LWasmStore* ins) {
  const MWasmStore* mir = ins->mir();
  const auto& access = mir->access();

  Register memoryBase = ToRegister(ins->memoryBase());

  AnyRegister value;
  if (ins->value()->isBogus()) {
    value = AnyRegister(zero);
  } else {
    value = ToAnyRegister(ins->value());
  }

  if (auto address = ToAbsoluteAddress(ins->ptr(), access)) {
    masm.wasmStoreAbsolute(access, value, memoryBase, address.value());
  } else {
    Register ptr = ToRegister(ins->ptr());
    auto zeroExtend = ToZeroExtendIndex(ins);

    masm.wasmStore(access, value, memoryBase, ptr, zeroExtend);
  }
}

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
  if (ins->arg()->isConstant()) {
    masm.storePtr(ImmWord(ToInt32(ins->arg())),
                  Address(StackPointer, mir->spOffset()));
  } else {
    if (ins->arg()->isGeneralReg()) {
      masm.storePtr(ToRegister(ins->arg()),
                    Address(StackPointer, mir->spOffset()));
    } else if (mir->input()->type() == MIRType::Double) {
      masm.storeDouble(ToFloatRegister(ins->arg()),
                       Address(StackPointer, mir->spOffset()));
    } else {
      masm.storeFloat32(ToFloatRegister(ins->arg()),
                        Address(StackPointer, mir->spOffset()));
    }
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
    Register out = ToRegister(ins->output());

    UseScratchRegisterScope temps(&masm);
    Register scratch = temps.Acquire();

    masm.ma_cselnz(out, trueExpr, falseExpr, cond, scratch);
    return;
  }

  const LAllocation* falseExpr = ins->falseExpr();
  FloatRegister out = ToFloatRegister(ins->output());
  MOZ_ASSERT(ToFloatRegister(ins->trueExpr()) == out,
             "true expr input is reused for output");

  Label done;
  masm.ma_b(cond, cond, &done, Assembler::NonZero, ShortJump);
  if (falseExpr->isFloatReg()) {
    if (mirType == MIRType::Float32) {
      masm.moveFloat32(ToFloatRegister(falseExpr), out);
    } else if (mirType == MIRType::Double) {
      masm.moveDouble(ToFloatRegister(falseExpr), out);
    } else {
      MOZ_CRASH("unhandled type in visitWasmSelect!");
    }
  } else {
    if (mirType == MIRType::Float32) {
      masm.loadFloat32(ToAddress(falseExpr), out);
    } else if (mirType == MIRType::Double) {
      masm.loadDouble(ToAddress(falseExpr), out);
    } else {
      MOZ_CRASH("unhandled type in visitWasmSelect!");
    }
  }
  masm.bind(&done);
}

// We expect to handle the cases: compare is {{U,}Int32, {U,}Int64}, Float32,
// Double}, and select is {{U,}Int32, {U,}Int64}}.
void CodeGenerator::visitWasmCompareAndSelect(LWasmCompareAndSelect* ins) {
  MCompare::CompareType compTy = ins->compareType();
  MOZ_RELEASE_ASSERT(
      compTy == MCompare::Compare_Int32 || compTy == MCompare::Compare_UInt32 ||
          compTy == MCompare::Compare_Int64 ||
          compTy == MCompare::Compare_UInt64 ||
          compTy == MCompare::Compare_Float32 ||
          compTy == MCompare::Compare_Double,
      "CodeGenerator::visitWasmCompareAndSelect: unexpected compare type");
  MOZ_RELEASE_ASSERT(
      ins->mir()->type() == MIRType::Int32 ||
          ins->mir()->type() == MIRType::Int64,
      "CodeGenerator::visitWasmCompareAndSelect: unexpected select type");

  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();

  if (compTy == MCompare::Compare_Float32 ||
      compTy == MCompare::Compare_Double) {
    FloatRegister lhs = ToFloatRegister(ins->leftExpr());
    FloatRegister rhs = ToFloatRegister(ins->rightExpr());
    Assembler::DoubleCondition cond = JSOpToDoubleCondition(ins->jsop());

    if (compTy == MCompare::Compare_Float32) {
      masm.ma_compareF32(scratch, cond, lhs, rhs);
    } else {
      masm.ma_compareF64(scratch, cond, lhs, rhs);
    }
  } else {
    Register lhs = ToRegister(ins->leftExpr());
    Register rhs = ToRegister(ins->rightExpr());
    Assembler::Condition cond = JSOpToCondition(compTy, ins->jsop());

    masm.ma_cmp_set(scratch, lhs, rhs, cond);
  }

  Register trueExpr = ToRegister(ins->ifTrueExpr());
  Register falseExpr = ToRegister(ins->ifFalseExpr());
  Register output = ToRegister(ins->output());

  masm.ma_cselnz(output, trueExpr, falseExpr, scratch, scratch);
}

void CodeGenerator::visitUDiv(LUDiv* ins) {
  Register lhs = ToRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());
  Register output = ToRegister(ins->output());
  Label done;

  MDiv* mir = ins->mir();

  // Prevent divide by zero.
  if (mir->canBeDivideByZero()) {
    if (mir->trapOnError()) {
      TrapIfDivideByZero(masm, ins, rhs);
    } else if (mir->isTruncated()) {
      // Infinity|0 == 0
      Label nonZero;
      masm.ma_b(rhs, rhs, &nonZero, Assembler::NonZero, ShortJump);
      masm.move32(Imm32(0), output);
      masm.jump(&done);
      masm.bind(&nonZero);
    } else {
      bailoutCmp32(Assembler::Equal, rhs, Imm32(0), ins->snapshot());
    }
  }

  // If the remainder is > 0, bailout since this must be a double.
  if (mir->canTruncateRemainder()) {
    masm.divuw(output, lhs, rhs);
  } else {
    MOZ_ASSERT(lhs != output && rhs != output);

    UseScratchRegisterScope temps(&masm);
    Register scratch = temps.Acquire();

    // The recommended code sequence to obtain both the quotient and remainder
    // is div[u] followed by mod[u].
    masm.divuw(output, lhs, rhs);
    masm.remuw(scratch, lhs, rhs);

    bailoutCmp32(Assembler::NonZero, scratch, scratch, ins->snapshot());
  }

  // Unsigned div can return a value that's not a signed int32.
  // If our users aren't expecting that, bail.
  if (!mir->isTruncated()) {
    bailoutCmp32(Assembler::LessThan, output, Imm32(0), ins->snapshot());
  }

  masm.bind(&done);
}

template <class LUDivOrUMod>
static void UnsignedDivideWithConstant(MacroAssembler& masm, LUDivOrUMod* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register output = ToRegister(ins->output());
  uint32_t d = ins->denominator();

  UseScratchRegisterScope temps(masm);
  Register temp = temps.Acquire();

  // The denominator isn't a power of 2 (see LDivPowTwoI).
  MOZ_ASSERT(!std::has_single_bit(d));

  auto rmc = ReciprocalMulConstants::computeUnsignedDivisionConstants(d);

  // We first compute (M * n) >> 32, where M = rmc.multiplier.
  if (int32_t(rmc.multiplier) >= 0) {
    // Zero-extend |lhs| in preparation for an unsigned 64-bit multiplication.
    masm.ZeroExtendWord(output, lhs);
    masm.ma_li(temp, Imm32(rmc.multiplier));
    masm.mul(output, output, temp);
  } else {
    masm.slli(output, lhs, 32);
    masm.ma_li(temp, Imm32(rmc.multiplier));
    masm.slli(temp, temp, 32);
    masm.mulhu(output, output, temp);
  }

  if (rmc.multiplier > UINT32_MAX) {
    // M >= 2^32 and shift == 0 is impossible, as d >= 2 implies that
    // ((M * n) >> (32 + shift)) >= n > floor(n/d) whenever n >= d,
    // contradicting the proof of correctness in computeDivisionConstants.
    MOZ_ASSERT(rmc.shiftAmount > 0);
    MOZ_ASSERT(rmc.multiplier < (int64_t(1) << 33));

    masm.srli(output, output, 32);

    // We actually computed output = ((uint32_t(M) * n) >> 32) instead. Since
    // (M * n) >> (32 + shift) is the same as (output + n) >> shift, we can
    // correct for the overflow. This case is a bit trickier than the signed
    // case, though, as the (output + n) addition itself can overflow; however,
    // note that
    // (output + n) >> shift == (((n - output) >> 1) + output) >> (shift - 1),
    // which is overflow-free. See Hacker's Delight, section 10-8 for details.

    // Compute (n - output) >> 1 into temp.
    masm.sub(temp, lhs, output);
    masm.srliw(temp, temp, 1);

    // Finish the computation.
    masm.add(output, output, temp);
    if (rmc.shiftAmount > 1) {
      masm.srli(output, output, rmc.shiftAmount - 1);
    }
  } else {
    masm.srli(output, output, 32 + rmc.shiftAmount);
  }
}

void CodeGenerator::visitUDivConstant(LUDivConstant* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register output = ToRegister(ins->output());
  uint32_t d = ins->denominator();

  MDiv* mir = ins->mir();

  if (d == 0) {
    if (ins->mir()->trapOnError()) {
      masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
    } else if (mir->canTruncateInfinities()) {
      masm.ma_mv(output, zero);
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

    masm.ma_mul32(temp, output, Imm32(d));
    bailoutCmp32(Assembler::NotEqual, lhs, temp, ins->snapshot());
  }
}

void CodeGenerator::visitUMod(LUMod* ins) {
  Register lhs = ToRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());
  Register output = ToRegister(ins->output());
  Label done;

  MMod* mir = ins->mir();

  // Prevent divide by zero.
  if (mir->canBeDivideByZero()) {
    if (mir->trapOnError()) {
      TrapIfDivideByZero(masm, ins, rhs);
    } else if (mir->isTruncated()) {
      // NaN|0 == 0
      Label nonZero;
      masm.ma_b(rhs, rhs, &nonZero, Assembler::NonZero, ShortJump);
      masm.move32(Imm32(0), output);
      masm.jump(&done);
      masm.bind(&nonZero);
    } else {
      bailoutCmp32(Assembler::Equal, rhs, Imm32(0), ins->snapshot());
    }
  }

  masm.remuw(output, lhs, rhs);

  // Bail if the output would be negative.
  //
  // LUMod inputs may be Uint32, so care is taken to ensure the result is not
  // unexpectedly signed.
  if (!mir->isTruncated()) {
    bailoutCmp32(Assembler::LessThan, output, Imm32(0), ins->snapshot());
  }

  masm.bind(&done);
}

void CodeGenerator::visitUModConstant(LUModConstant* ins) {
  Register output = ToRegister(ins->output());
  Register lhs = ToRegister(ins->numerator());

  MMod* mir = ins->mir();

  uint32_t d = ins->denominator();
  if (d == 0) {
    if (ins->mir()->trapOnError()) {
      masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
    } else if (mir->isTruncated()) {
      masm.ma_mv(output, zero);
    } else {
      MOZ_ASSERT(mir->fallible());
      bailout(ins->snapshot());
    }
    return;
  }

  // Compute the truncated division result in |output|.
  UnsignedDivideWithConstant(masm, ins);

  // Compute the remainder: output = lhs - (output * d).
  masm.ma_mul32(output, output, Imm32(d));
  masm.subw(output, lhs, output);

  // Bail if not truncated and the remainder is in the range [2^31, 2^32).
  if (!mir->isTruncated()) {
    bailoutTest32(Assembler::Signed, output, output, ins->snapshot());
  }
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

  masm.negw(output, input);
}

void CodeGenerator::visitNegI64(LNegI64* ins) {
  Register input = ToRegister64(ins->input()).reg;
  Register output = ToOutRegister64(ins).reg;

  masm.neg(output, input);
}

void CodeGenerator::visitNegD(LNegD* ins) {
  FloatRegister input = ToFloatRegister(ins->input());
  FloatRegister output = ToFloatRegister(ins->output());

  masm.fneg_d(output, input);
}

void CodeGenerator::visitNegF(LNegF* ins) {
  FloatRegister input = ToFloatRegister(ins->input());
  FloatRegister output = ToFloatRegister(ins->output());

  masm.fneg_s(output, input);
}

void CodeGenerator::visitWasmAddOffset(LWasmAddOffset* ins) {
  MWasmAddOffset* mir = ins->mir();
  Register base = ToRegister(ins->base());
  Register out = ToRegister(ins->output());

  Label ok;
  masm.ma_add32TestCarry(Assembler::CarryClear, out, base, Imm32(mir->offset()),
                         &ok);
  masm.wasmTrap(wasm::Trap::OutOfBounds, mir->trapSiteDesc());
  masm.bind(&ok);
}

void CodeGenerator::visitWasmAddOffset64(LWasmAddOffset64* ins) {
  MWasmAddOffset* mir = ins->mir();
  Register64 base = ToRegister64(ins->base());
  Register64 out = ToOutRegister64(ins);

  Label ok;
  masm.ma_addPtrTestCarry(Assembler::CarryClear, out.reg, base.reg,
                          ImmWord(mir->offset()), &ok);
  masm.wasmTrap(wasm::Trap::OutOfBounds, mir->trapSiteDesc());
  masm.bind(&ok);
}

void CodeGenerator::visitAtomicTypedArrayElementBinop(
    LAtomicTypedArrayElementBinop* ins) {
  MOZ_ASSERT(!ins->mir()->isForEffect());

  Register output = ToRegister(ins->output());
  Register elements = ToRegister(ins->elements());
  Register valueTemp = ToTempRegisterOrInvalid(ins->temp0());
  Register offsetTemp = ToTempRegisterOrInvalid(ins->temp1());
  Register maskTemp = ToTempRegisterOrInvalid(ins->temp2());
  Register value = ToRegister(ins->value());
  Scalar::Type arrayType = ins->mir()->arrayType();

  auto mem = ToAddressOrBaseIndex(elements, ins->index(), arrayType);

  mem.match([&](const auto& mem) {
    masm.atomicFetchOp(arrayType, Synchronization::Full(),
                       ins->mir()->operation(), value, mem, valueTemp,
                       offsetTemp, maskTemp, output);
  });
}

void CodeGenerator::visitAtomicTypedArrayElementBinopForEffect(
    LAtomicTypedArrayElementBinopForEffect* ins) {
  MOZ_ASSERT(ins->mir()->isForEffect());

  Register elements = ToRegister(ins->elements());
  Register valueTemp = ToTempRegisterOrInvalid(ins->temp0());
  Register offsetTemp = ToTempRegisterOrInvalid(ins->temp1());
  Register maskTemp = ToTempRegisterOrInvalid(ins->temp2());
  Register value = ToRegister(ins->value());
  Scalar::Type arrayType = ins->mir()->arrayType();

  auto mem = ToAddressOrBaseIndex(elements, ins->index(), arrayType);

  mem.match([&](const auto& mem) {
    masm.atomicEffectOp(arrayType, Synchronization::Full(),
                        ins->mir()->operation(), value, mem, valueTemp,
                        offsetTemp, maskTemp);
  });
}

void CodeGenerator::visitCompareExchangeTypedArrayElement(
    LCompareExchangeTypedArrayElement* ins) {
  Register elements = ToRegister(ins->elements());
  Register output = ToRegister(ins->output());

  Register oldval = ToRegister(ins->oldval());
  Register newval = ToRegister(ins->newval());
  Register valueTemp = ToTempRegisterOrInvalid(ins->temp0());
  Register offsetTemp = ToTempRegisterOrInvalid(ins->temp1());
  Register maskTemp = ToTempRegisterOrInvalid(ins->temp2());
  Scalar::Type arrayType = ins->mir()->arrayType();

  auto dest = ToAddressOrBaseIndex(elements, ins->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.compareExchange(arrayType, Synchronization::Full(), dest, oldval,
                         newval, valueTemp, offsetTemp, maskTemp, output);
  });
}

void CodeGenerator::visitAtomicExchangeTypedArrayElement(
    LAtomicExchangeTypedArrayElement* ins) {
  Register elements = ToRegister(ins->elements());
  Register output = ToRegister(ins->output());

  Register value = ToRegister(ins->value());
  Register valueTemp = ToTempRegisterOrInvalid(ins->temp0());
  Register offsetTemp = ToTempRegisterOrInvalid(ins->temp1());
  Register maskTemp = ToTempRegisterOrInvalid(ins->temp2());
  Scalar::Type arrayType = ins->mir()->arrayType();

  auto dest = ToAddressOrBaseIndex(elements, ins->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.atomicExchange(arrayType, Synchronization::Full(), dest, value,
                        valueTemp, offsetTemp, maskTemp, output);
  });
}

void CodeGenerator::visitCompareExchangeTypedArrayElement64(
    LCompareExchangeTypedArrayElement64* ins) {
  Register elements = ToRegister(ins->elements());
  Register64 oldval = ToRegister64(ins->oldval());
  Register64 newval = ToRegister64(ins->newval());
  Register64 out = ToOutRegister64(ins);
  Scalar::Type arrayType = ins->mir()->arrayType();

  auto dest = ToAddressOrBaseIndex(elements, ins->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.compareExchange64(Synchronization::Full(), dest, oldval, newval, out);
  });
}

void CodeGenerator::visitAtomicExchangeTypedArrayElement64(
    LAtomicExchangeTypedArrayElement64* ins) {
  Register elements = ToRegister(ins->elements());
  Register64 value = ToRegister64(ins->value());
  Register64 out = ToOutRegister64(ins);
  Scalar::Type arrayType = ins->mir()->arrayType();

  auto dest = ToAddressOrBaseIndex(elements, ins->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.atomicExchange64(Synchronization::Full(), dest, value, out);
  });
}

void CodeGenerator::visitAtomicTypedArrayElementBinop64(
    LAtomicTypedArrayElementBinop64* ins) {
  MOZ_ASSERT(ins->mir()->hasUses());

  Register elements = ToRegister(ins->elements());
  Register64 value = ToRegister64(ins->value());
  Register64 out = ToOutRegister64(ins);

  Scalar::Type arrayType = ins->mir()->arrayType();
  AtomicOp atomicOp = ins->mir()->operation();

  auto dest = ToAddressOrBaseIndex(elements, ins->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.atomicFetchOp64(Synchronization::Full(), atomicOp, value, dest,
                         Register64::Invalid(), out);
  });
}

void CodeGenerator::visitAtomicTypedArrayElementBinopForEffect64(
    LAtomicTypedArrayElementBinopForEffect64* ins) {
  MOZ_ASSERT(!ins->mir()->hasUses());

  Register elements = ToRegister(ins->elements());
  Register64 value = ToRegister64(ins->value());

  Scalar::Type arrayType = ins->mir()->arrayType();
  AtomicOp atomicOp = ins->mir()->operation();

  auto dest = ToAddressOrBaseIndex(elements, ins->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.atomicEffectOp64(Synchronization::Full(), atomicOp, value, dest,
                          Register64::Invalid());
  });
}

void CodeGenerator::visitAtomicLoad64(LAtomicLoad64* ins) {
  Register elements = ToRegister(ins->elements());
  Register64 out = ToOutRegister64(ins);

  Scalar::Type storageType = ins->mir()->storageType();

  auto source = ToAddressOrBaseIndex(elements, ins->index(), storageType);

  auto sync = Synchronization::Load();
  masm.memoryBarrierBefore(sync);
  source.match([&](const auto& source) { masm.load64(source, out); });
  masm.memoryBarrierAfter(sync);
}

void CodeGenerator::visitAtomicStore64(LAtomicStore64* ins) {
  Register elements = ToRegister(ins->elements());
  Register64 value = ToRegister64(ins->value());

  Scalar::Type writeType = ins->mir()->writeType();

  auto dest = ToAddressOrBaseIndex(elements, ins->index(), writeType);

  auto sync = Synchronization::Store();
  masm.memoryBarrierBefore(sync);
  dest.match([&](const auto& dest) { masm.store64(value, dest); });
  masm.memoryBarrierAfter(sync);
}

void CodeGenerator::visitWasmCompareExchangeI64(LWasmCompareExchangeI64* ins) {
  Register memoryBase = ToRegister(ins->memoryBase());
  Register ptr = ToRegister(ins->ptr());
  Register64 oldValue = ToRegister64(ins->oldValue());
  Register64 newValue = ToRegister64(ins->newValue());
  Register64 output = ToOutRegister64(ins);
  uint32_t offset = ins->mir()->access().offset32();

  BaseIndex addr(memoryBase, ptr, TimesOne, offset);
  auto zeroExtend = ToZeroExtendIndex(ins);

  masm.wasmCompareExchange64(ins->mir()->access(), addr, oldValue, newValue,
                             output, zeroExtend);
}

void CodeGenerator::visitWasmAtomicExchangeI64(LWasmAtomicExchangeI64* ins) {
  Register memoryBase = ToRegister(ins->memoryBase());
  Register ptr = ToRegister(ins->ptr());
  Register64 value = ToRegister64(ins->value());
  Register64 output = ToOutRegister64(ins);
  uint32_t offset = ins->mir()->access().offset32();

  BaseIndex addr(memoryBase, ptr, TimesOne, offset);
  auto zeroExtend = ToZeroExtendIndex(ins);

  masm.wasmAtomicExchange64(ins->mir()->access(), addr, value, output,
                            zeroExtend);
}

void CodeGenerator::visitWasmAtomicBinopI64(LWasmAtomicBinopI64* ins) {
  Register memoryBase = ToRegister(ins->memoryBase());
  Register ptr = ToRegister(ins->ptr());
  Register64 value = ToRegister64(ins->value());
  Register64 output = ToOutRegister64(ins);
  uint32_t offset = ins->mir()->access().offset32();

  BaseIndex addr(memoryBase, ptr, TimesOne, offset);
  auto zeroExtend = ToZeroExtendIndex(ins);

  masm.wasmAtomicFetchOp64(ins->mir()->access(), ins->mir()->operation(), value,
                           addr, Register64::Invalid(), output, zeroExtend);
}

void CodeGenerator::visitSimd128(LSimd128* ins) { MOZ_CRASH("No SIMD"); }

void CodeGenerator::visitWasmTernarySimd128(LWasmTernarySimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmBinarySimd128(LWasmBinarySimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmBinarySimd128WithConstant(
    LWasmBinarySimd128WithConstant* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmVariableShiftSimd128(
    LWasmVariableShiftSimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmConstantShiftSimd128(
    LWasmConstantShiftSimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmSignReplicationSimd128(
    LWasmSignReplicationSimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmShuffleSimd128(LWasmShuffleSimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmPermuteSimd128(LWasmPermuteSimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmReplaceLaneSimd128(LWasmReplaceLaneSimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmMoveLaneSimd128(LWasmMoveLaneSimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmReplaceInt64LaneSimd128(
    LWasmReplaceInt64LaneSimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmScalarToSimd128(LWasmScalarToSimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmInt64ToSimd128(LWasmInt64ToSimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmUnarySimd128(LWasmUnarySimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmReduceSimd128(LWasmReduceSimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmReduceAndBranchSimd128(
    LWasmReduceAndBranchSimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmReduceSimd128ToInt64(
    LWasmReduceSimd128ToInt64* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmLoadLaneSimd128(LWasmLoadLaneSimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmStoreLaneSimd128(LWasmStoreLaneSimd128* ins) {
  MOZ_CRASH("No SIMD");
}

void CodeGenerator::visitWasmMulI64WideHI64(LWasmMulI64WideHI64* ins) {
  Register lhs = ToRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());
  Register output = ToRegister(ins->output());
  // This holds because both operands are non-AtStart variants.
  MOZ_ASSERT(output != lhs && output != rhs);
  masm.wasmMulI64WideHI64(lhs, rhs, output, ins->isSigned());
}
