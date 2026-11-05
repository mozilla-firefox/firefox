/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/x86-shared/CodeGenerator-x86-shared.h"

#include "mozilla/DebugOnly.h"
#include "mozilla/MathAlgorithms.h"

#include <bit>

#include "jit/CodeGenerator.h"
#include "jit/InlineScriptTree.h"
#include "jit/JitRuntime.h"
#include "jit/RangeAnalysis.h"
#include "jit/ReciprocalMulConstants.h"
#include "js/ScalarType.h"  // js::Scalar::Type

#include "jit/MacroAssembler-inl.h"
#include "jit/shared/CodeGenerator-shared-inl.h"

using namespace js;
using namespace js::jit;

using mozilla::Abs;
using mozilla::DebugOnly;
using mozilla::FloorLog2;
using mozilla::NegativeInfinity;

using JS::GenericNaN;

namespace js {
namespace jit {

CodeGeneratorX86Shared::CodeGeneratorX86Shared(
    MIRGenerator* gen, LIRGraph* graph, MacroAssembler* masm,
    const wasm::CodeMetadata* wasmCodeMeta)
    : CodeGeneratorShared(gen, graph, masm, wasmCodeMeta) {}

#ifdef JS_PUNBOX64
Operand CodeGeneratorX86Shared::ToOperandOrRegister64(
    const LInt64Allocation& input) {
  return ToOperand(input.value());
}
#else
Register64 CodeGeneratorX86Shared::ToOperandOrRegister64(
    const LInt64Allocation& input) {
  return ToRegister64(input);
}
#endif

void CodeGeneratorX86Shared::emitBranch(Assembler::Condition cond,
                                        MBasicBlock* mirTrue,
                                        MBasicBlock* mirFalse) {
  if (isNextBlock(mirFalse->lir())) {
    jumpToBlock(mirTrue, cond);
  } else {
    jumpToBlock(mirFalse, Assembler::InvertCondition(cond));
    jumpToBlock(mirTrue);
  }
}

void CodeGeneratorX86Shared::emitBranch(Assembler::DoubleCondition cond,
                                        MBasicBlock* ifTrue,
                                        MBasicBlock* ifFalse,
                                        Assembler::NaNCond ifNaN) {
  if (ifNaN == Assembler::NaN_IsFalse) {
    jumpToBlock(ifFalse, Assembler::Parity);
  } else if (ifNaN == Assembler::NaN_IsTrue) {
    jumpToBlock(ifTrue, Assembler::Parity);
  }
  emitBranch(Assembler::ConditionFromDoubleCondition(cond), ifTrue, ifFalse);
}

void CodeGenerator::visitTestDAndBranch(LTestDAndBranch* test) {
  const LAllocation* opd = test->input();

  // vucomisd flags:
  //             Z  P  C
  //            ---------
  //      NaN    1  1  1
  //        >    0  0  0
  //        <    0  0  1
  //        =    1  0  0
  //
  // NaN is falsey, so comparing against 0 and then using the Z flag is
  // enough to determine which branch to take.
  ScratchDoubleScope scratch(masm);
  masm.zeroDouble(scratch);
  masm.vucomisd(scratch, ToFloatRegister(opd));
  emitBranch(Assembler::NotEqual, test->ifTrue(), test->ifFalse());
}

void CodeGenerator::visitTestFAndBranch(LTestFAndBranch* test) {
  const LAllocation* opd = test->input();
  // vucomiss flags are the same as doubles; see comment above
  {
    ScratchFloat32Scope scratch(masm);
    masm.zeroFloat32(scratch);
    masm.vucomiss(scratch, ToFloatRegister(opd));
  }
  emitBranch(Assembler::NotEqual, test->ifTrue(), test->ifFalse());
}

static Assembler::DoubleCondition ToDoubleCondition(FloatRegister lhs,
                                                    FloatRegister rhs,
                                                    JSOp op) {
  if (lhs == rhs) {
    switch (op) {
      case JSOp::Eq:
      case JSOp::StrictEq:
      case JSOp::Le:
      case JSOp::Ge:
        return Assembler::DoubleOrdered;
      case JSOp::Ne:
      case JSOp::StrictNe:
        return Assembler::DoubleUnordered;
      default:
        break;
    }
  }
  return JSOpToDoubleCondition(op);
}

void CodeGenerator::visitCompareD(LCompareD* comp) {
  FloatRegister lhs = ToFloatRegister(comp->left());
  FloatRegister rhs = ToFloatRegister(comp->right());
  Register output = ToRegister(comp->output());

  Assembler::DoubleCondition cond =
      ToDoubleCondition(lhs, rhs, comp->mir()->jsop());

  Assembler::NaNCond nanCond = Assembler::NaNCondFromDoubleCondition(cond);
  if (comp->mir()->operandsAreNeverNaN()) {
    nanCond = Assembler::NaN_HandledByCond;
  }

  bool destIsZero = masm.maybeEmitSetZeroByteRegister(output);
  masm.compareDouble(cond, lhs, rhs);
  masm.emitSet(Assembler::ConditionFromDoubleCondition(cond), output,
               destIsZero, nanCond);
}

void CodeGenerator::visitCompareF(LCompareF* comp) {
  FloatRegister lhs = ToFloatRegister(comp->left());
  FloatRegister rhs = ToFloatRegister(comp->right());
  Register output = ToRegister(comp->output());

  Assembler::DoubleCondition cond =
      ToDoubleCondition(lhs, rhs, comp->mir()->jsop());

  Assembler::NaNCond nanCond = Assembler::NaNCondFromDoubleCondition(cond);
  if (comp->mir()->operandsAreNeverNaN()) {
    nanCond = Assembler::NaN_HandledByCond;
  }

  bool destIsZero = masm.maybeEmitSetZeroByteRegister(output);
  masm.compareFloat(cond, lhs, rhs);
  masm.emitSet(Assembler::ConditionFromDoubleCondition(cond), output,
               destIsZero, nanCond);
}

void CodeGenerator::visitNotD(LNotD* ins) {
  FloatRegister opd = ToFloatRegister(ins->input());
  Register output = ToRegister(ins->output());

  // Not returns true if the input is a NaN. We don't have to worry about
  // it if we know the input is never NaN though.
  Assembler::NaNCond nanCond = Assembler::NaN_IsTrue;
  if (ins->mir()->operandIsNeverNaN()) {
    nanCond = Assembler::NaN_HandledByCond;
  }

  bool destIsZero = masm.maybeEmitSetZeroByteRegister(output);
  ScratchDoubleScope scratch(masm);
  masm.zeroDouble(scratch);
  masm.compareDouble(Assembler::DoubleEqualOrUnordered, opd, scratch);
  masm.emitSet(Assembler::Equal, output, destIsZero, nanCond);
}

void CodeGenerator::visitNotF(LNotF* ins) {
  FloatRegister opd = ToFloatRegister(ins->input());
  Register output = ToRegister(ins->output());

  // Not returns true if the input is a NaN. We don't have to worry about
  // it if we know the input is never NaN though.
  Assembler::NaNCond nanCond = Assembler::NaN_IsTrue;
  if (ins->mir()->operandIsNeverNaN()) {
    nanCond = Assembler::NaN_HandledByCond;
  }

  bool destIsZero = masm.maybeEmitSetZeroByteRegister(output);
  ScratchFloat32Scope scratch(masm);
  masm.zeroFloat32(scratch);
  masm.compareFloat(Assembler::DoubleEqualOrUnordered, opd, scratch);
  masm.emitSet(Assembler::Equal, output, destIsZero, nanCond);
}

void CodeGenerator::visitCompareDAndBranch(LCompareDAndBranch* comp) {
  FloatRegister lhs = ToFloatRegister(comp->left());
  FloatRegister rhs = ToFloatRegister(comp->right());

  Assembler::DoubleCondition cond =
      ToDoubleCondition(lhs, rhs, comp->cmpMir()->jsop());

  Assembler::NaNCond nanCond = Assembler::NaNCondFromDoubleCondition(cond);
  if (comp->cmpMir()->operandsAreNeverNaN()) {
    nanCond = Assembler::NaN_HandledByCond;
  }

  masm.compareDouble(cond, lhs, rhs);
  emitBranch(cond, comp->ifTrue(), comp->ifFalse(), nanCond);
}

void CodeGenerator::visitCompareFAndBranch(LCompareFAndBranch* comp) {
  FloatRegister lhs = ToFloatRegister(comp->left());
  FloatRegister rhs = ToFloatRegister(comp->right());

  Assembler::DoubleCondition cond =
      ToDoubleCondition(lhs, rhs, comp->cmpMir()->jsop());

  Assembler::NaNCond nanCond = Assembler::NaNCondFromDoubleCondition(cond);
  if (comp->cmpMir()->operandsAreNeverNaN()) {
    nanCond = Assembler::NaN_HandledByCond;
  }

  masm.compareFloat(cond, lhs, rhs);
  emitBranch(cond, comp->ifTrue(), comp->ifFalse(), nanCond);
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
  Operand falseExpr = ToOperand(ins->falseExpr());

  masm.test32(cond, cond);

  if (mirType == MIRType::Int32 || mirType == MIRType::WasmAnyRef) {
    Register out = ToRegister(ins->output());
    MOZ_ASSERT(ToRegister(ins->trueExpr()) == out,
               "true expr input is reused for output");
    if (mirType == MIRType::Int32) {
      masm.cmovz32(falseExpr, out);
    } else {
      masm.cmovzPtr(falseExpr, out);
    }
    return;
  }

  FloatRegister out = ToFloatRegister(ins->output());
  MOZ_ASSERT(ToFloatRegister(ins->trueExpr()) == out,
             "true expr input is reused for output");

  Label done;
  masm.j(Assembler::NonZero, &done);

  if (mirType == MIRType::Float32) {
    if (falseExpr.kind() == Operand::FPREG) {
      masm.moveFloat32(ToFloatRegister(ins->falseExpr()), out);
    } else {
      masm.loadFloat32(falseExpr, out);
    }
  } else if (mirType == MIRType::Double) {
    if (falseExpr.kind() == Operand::FPREG) {
      masm.moveDouble(ToFloatRegister(ins->falseExpr()), out);
    } else {
      masm.loadDouble(falseExpr, out);
    }
  } else if (mirType == MIRType::Simd128) {
    if (falseExpr.kind() == Operand::FPREG) {
      masm.moveSimd128(ToFloatRegister(ins->falseExpr()), out);
    } else {
      masm.loadUnalignedSimd128(falseExpr, out);
    }
  } else {
    MOZ_CRASH("unhandled type in visitWasmSelect!");
  }

  masm.bind(&done);
}

void CodeGenerator::visitWasmAddOffset(LWasmAddOffset* lir) {
  MWasmAddOffset* mir = lir->mir();
  Register base = ToRegister(lir->base());
  Register out = ToRegister(lir->output());

  if (base != out) {
    masm.move32(base, out);
  }
  masm.add32(Imm32(mir->offset()), out);
  auto* ool = new (alloc()) LambdaOutOfLineCode([=, this](OutOfLineCode& ool) {
    masm.wasmTrap(wasm::Trap::OutOfBounds, mir->trapSiteDesc());
  });
  addOutOfLineCode(ool, mir);
  masm.j(Assembler::CarrySet, ool->entry());
}

void CodeGenerator::visitWasmAddOffset64(LWasmAddOffset64* lir) {
  MWasmAddOffset* mir = lir->mir();
  Register64 base = ToRegister64(lir->base());
  Register64 out = ToOutRegister64(lir);

  if (base != out) {
    masm.move64(base, out);
  }
  masm.add64(Imm64(mir->offset()), out);
  auto* ool = new (alloc()) LambdaOutOfLineCode([=, this](OutOfLineCode& ool) {
    masm.wasmTrap(wasm::Trap::OutOfBounds, mir->trapSiteDesc());
  });
  addOutOfLineCode(ool, mir);
  masm.j(Assembler::CarrySet, ool->entry());
}

void CodeGenerator::visitWasmTruncateToInt32(LWasmTruncateToInt32* lir) {
  FloatRegister input = ToFloatRegister(lir->input());
  Register output = ToRegister(lir->output());

  MWasmTruncateToInt32* mir = lir->mir();
  MIRType inputType = mir->input()->type();

  MOZ_ASSERT(inputType == MIRType::Double || inputType == MIRType::Float32);

  auto* ool = new (alloc()) OutOfLineWasmTruncateCheck(mir, input, output);
  addOutOfLineCode(ool, mir);

  Label* oolEntry = ool->entry();
  if (mir->isUnsigned()) {
    if (inputType == MIRType::Double) {
      masm.wasmTruncateDoubleToUInt32(input, output, mir->isSaturating(),
                                      oolEntry);
    } else if (inputType == MIRType::Float32) {
      masm.wasmTruncateFloat32ToUInt32(input, output, mir->isSaturating(),
                                       oolEntry);
    } else {
      MOZ_CRASH("unexpected type");
    }
    if (mir->isSaturating()) {
      masm.bind(ool->rejoin());
    }
    return;
  }

  if (inputType == MIRType::Double) {
    masm.wasmTruncateDoubleToInt32(input, output, mir->isSaturating(),
                                   oolEntry);
  } else if (inputType == MIRType::Float32) {
    masm.wasmTruncateFloat32ToInt32(input, output, mir->isSaturating(),
                                    oolEntry);
  } else {
    MOZ_CRASH("unexpected type");
  }

  masm.bind(ool->rejoin());
}

bool CodeGeneratorX86Shared::generateOutOfLineCode() {
  if (!CodeGeneratorShared::generateOutOfLineCode()) {
    return false;
  }

  if (needsDeoptLabel()) {
    // All non-table-based bailouts will go here.
    masm.bind(&deoptLabel_);

    // Push the frame size, so the handler can recover the IonScript.
    masm.push(Imm32(frameSize()));

    TrampolinePtr handler = gen->jitRuntime()->getGenericBailoutHandler();
    masm.jump(handler);
  }

  return !masm.oom();
}

void CodeGeneratorX86Shared::bailoutIf(Assembler::DoubleCondition condition,
                                       LSnapshot* snapshot) {
  MOZ_ASSERT(Assembler::NaNCondFromDoubleCondition(condition) ==
             Assembler::NaN_HandledByCond);
  bailoutIf(Assembler::ConditionFromDoubleCondition(condition), snapshot);
}

void CodeGenerator::visitMinMaxD(LMinMaxD* ins) {
  FloatRegister first = ToFloatRegister(ins->first());
  FloatRegister second = ToFloatRegister(ins->second());
#ifdef DEBUG
  FloatRegister output = ToFloatRegister(ins->output());
  MOZ_ASSERT(first == output);
#endif

  bool handleNaN = !ins->mir()->range() || ins->mir()->range()->canBeNaN();

  if (ins->mir()->isMax()) {
    masm.maxDouble(second, first, handleNaN);
  } else {
    masm.minDouble(second, first, handleNaN);
  }
}

void CodeGenerator::visitMinMaxF(LMinMaxF* ins) {
  FloatRegister first = ToFloatRegister(ins->first());
  FloatRegister second = ToFloatRegister(ins->second());
#ifdef DEBUG
  FloatRegister output = ToFloatRegister(ins->output());
  MOZ_ASSERT(first == output);
#endif

  bool handleNaN = !ins->mir()->range() || ins->mir()->range()->canBeNaN();

  if (ins->mir()->isMax()) {
    masm.maxFloat32(second, first, handleNaN);
  } else {
    masm.minFloat32(second, first, handleNaN);
  }
}

void CodeGenerator::visitPowHalfD(LPowHalfD* ins) {
  FloatRegister input = ToFloatRegister(ins->input());
  FloatRegister output = ToFloatRegister(ins->output());

  ScratchDoubleScope scratch(masm);

  Label done, sqrt;

  if (!ins->mir()->operandIsNeverNegativeInfinity()) {
    // Branch if not -Infinity.
    masm.loadConstantDouble(NegativeInfinity<double>(), scratch);

    Assembler::DoubleCondition cond = Assembler::DoubleNotEqualOrUnordered;
    if (ins->mir()->operandIsNeverNaN()) {
      cond = Assembler::DoubleNotEqual;
    }
    masm.branchDouble(cond, input, scratch, &sqrt);

    // Math.pow(-Infinity, 0.5) == Infinity.
    masm.zeroDouble(output);
    masm.subDouble(scratch, output);
    masm.jump(&done);

    masm.bind(&sqrt);
  }

  if (!ins->mir()->operandIsNeverNegativeZero()) {
    // Math.pow(-0, 0.5) == 0 == Math.pow(0, 0.5).
    // Adding 0 converts any -0 to 0.
    masm.zeroDouble(scratch);
    masm.addDouble(input, scratch);
    masm.sqrtDouble(scratch, output);
  } else {
    masm.sqrtDouble(input, output);
  }

  masm.bind(&done);
}

void CodeGeneratorX86Shared::emitUndoALUOperationOOL(LInstruction* ins) {
  Register reg = ToRegister(ins->getDef(0));

  DebugOnly<LAllocation*> lhs = ins->getOperand(0);
  LAllocation* rhs = ins->getOperand(1);

  MOZ_ASSERT(reg == ToRegister(lhs));
  MOZ_ASSERT_IF(rhs->isGeneralReg(), reg != ToRegister(rhs));

  // Undo the effect of the ALU operation, which was performed on the output
  // register and overflowed. Writing to the output register clobbered an
  // input reg, and the original value of the input needs to be recovered
  // to satisfy the constraint imposed by any RECOVERED_INPUT operands to
  // the bailout snapshot.

  if (rhs->isConstant()) {
    Imm32 constant(ToInt32(rhs));
    if (ins->isAddI()) {
      masm.subl(constant, reg);
    } else {
      masm.addl(constant, reg);
    }
  } else {
    if (ins->isAddI()) {
      masm.subl(ToOperand(rhs), reg);
    } else {
      masm.addl(ToOperand(rhs), reg);
    }
  }

  bailout(ins->snapshot());
}

void CodeGenerator::visitAddI(LAddI* ins) {
  Register lhs = ToRegister(ins->lhs());
  const LAllocation* rhs = ins->rhs();
  Register out = ToRegister(ins->output());

  if (rhs->isConstant()) {
    if (lhs != out) {
      MOZ_ASSERT(!ins->snapshot());
      // Special case to lower the add to LEA instruction.
      masm.add32(Imm32(ToInt32(rhs)), lhs, out);
    } else {
      masm.addl(Imm32(ToInt32(rhs)), lhs);
    }
  } else {
    MOZ_ASSERT(out == lhs);
    masm.addl(ToOperand(rhs), lhs);
  }

  if (ins->snapshot()) {
    if (ins->recoversInput()) {
      auto* ool = new (alloc()) LambdaOutOfLineCode(
          [=, this](OutOfLineCode& ool) { emitUndoALUOperationOOL(ins); });
      addOutOfLineCode(ool, ins->mir());
      masm.j(Assembler::Overflow, ool->entry());
    } else {
      bailoutIf(Assembler::Overflow, ins->snapshot());
    }
  }
}

void CodeGenerator::visitAddI64(LAddI64* lir) {
  Register64 lhs = ToRegister64(lir->lhs());
  LInt64Allocation rhs = lir->rhs();

  MOZ_ASSERT(ToOutRegister64(lir) == lhs);

  if (IsConstant(rhs)) {
    masm.add64(Imm64(ToInt64(rhs)), lhs);
    return;
  }

  masm.add64(ToOperandOrRegister64(rhs), lhs);
}

void CodeGenerator::visitSubI(LSubI* ins) {
  Register lhs = ToRegister(ins->lhs());
  const LAllocation* rhs = ins->rhs();

  MOZ_ASSERT(ToRegister(ins->output()) == lhs);

  if (rhs->isConstant()) {
    masm.subl(Imm32(ToInt32(rhs)), lhs);
  } else {
    masm.subl(ToOperand(rhs), lhs);
  }

  if (ins->snapshot()) {
    if (ins->recoversInput()) {
      auto* ool = new (alloc()) LambdaOutOfLineCode(
          [=, this](OutOfLineCode& ool) { emitUndoALUOperationOOL(ins); });
      addOutOfLineCode(ool, ins->mir());
      masm.j(Assembler::Overflow, ool->entry());
    } else {
      bailoutIf(Assembler::Overflow, ins->snapshot());
    }
  }
}

void CodeGenerator::visitSubI64(LSubI64* lir) {
  LInt64Allocation lhs = lir->lhs();
  LInt64Allocation rhs = lir->rhs();

  MOZ_ASSERT(ToOutRegister64(lir) == ToRegister64(lhs));

  if (IsConstant(rhs)) {
    masm.sub64(Imm64(ToInt64(rhs)), ToRegister64(lhs));
    return;
  }

  masm.sub64(ToOperandOrRegister64(rhs), ToRegister64(lhs));
}

void CodeGenerator::visitMulI(LMulI* ins) {
  Register lhs = ToRegister(ins->lhs());
  const LAllocation* rhs = ins->rhs();
  Register out = ToRegister(ins->output());

  MMul* mul = ins->mir();
  MOZ_ASSERT_IF(mul->mode() == MMul::Integer,
                !mul->canBeNegativeZero() && !mul->canOverflow());

  if (rhs->isConstant()) {
    // Bailout on -0.0
    int32_t constant = ToInt32(rhs);
    if (mul->canBeNegativeZero() && constant <= 0) {
      Assembler::Condition bailoutCond =
          (constant == 0) ? Assembler::Signed : Assembler::Equal;
      masm.test32(lhs, lhs);
      bailoutIf(bailoutCond, ins->snapshot());
    }

    if (!mul->canOverflow()) {
      switch (constant) {
        case 2:
          if (lhs == out) {
            masm.addl(lhs, lhs);
          } else {
            masm.leal(Operand(lhs, lhs, TimesOne), out);
          }
          return;
        case 3:
          masm.leal(Operand(lhs, lhs, TimesTwo), out);
          return;
        case 4:
          if (lhs == out) {
            masm.shll(Imm32(2), lhs);
          } else {
            masm.leal(Operand(lhs, TimesFour, 0), out);
          }
          return;
        case 5:
          masm.leal(Operand(lhs, lhs, TimesFour), out);
          return;
        case 8:
          if (lhs == out) {
            masm.shll(Imm32(3), lhs);
          } else {
            masm.leal(Operand(lhs, TimesEight, 0), out);
          }
          return;
        case 9:
          masm.leal(Operand(lhs, lhs, TimesEight), out);
          return;
        default:
          // Use shift if cannot overflow and constant is power of 2
          int32_t shift = FloorLog2(uint32_t(constant));
          if (constant > 0 && std::has_single_bit(uint32_t(constant))) {
            if (lhs != out) {
              masm.movl(lhs, out);
            }
            masm.shll(Imm32(shift), out);
            return;
          }
      }
    }

    switch (constant) {
      case -1:
        if (lhs != out) {
          masm.movl(lhs, out);
        }
        masm.negl(out);
        break;
      case 0:
        masm.xorl(out, out);
        return;  // escape overflow check;
      case 1:
        if (lhs != out) {
          masm.movl(lhs, out);
        }
        return;  // escape overflow check;
      case 2:
        if (lhs == out) {
          masm.addl(lhs, lhs);
          break;
        }
        [[fallthrough]];
      default:
        masm.imull(Imm32(constant), lhs, out);
    }

    // Bailout on overflow
    if (mul->canOverflow()) {
      bailoutIf(Assembler::Overflow, ins->snapshot());
    }
  } else {
    MOZ_ASSERT(out == lhs);

    masm.imull(ToOperand(rhs), lhs);

    // Bailout on overflow
    if (mul->canOverflow()) {
      bailoutIf(Assembler::Overflow, ins->snapshot());
    }

    if (mul->canBeNegativeZero()) {
      // Jump to an OOL path if the result is 0.
      auto* ool =
          new (alloc()) LambdaOutOfLineCode([=, this](OutOfLineCode& ool) {
            Register result = ToRegister(ins->output());
            Operand lhsCopy = ToOperand(ins->lhsCopy());
            Operand rhs = ToOperand(ins->rhs());
            MOZ_ASSERT_IF(lhsCopy.kind() == Operand::REG,
                          lhsCopy.reg() != result.code());

            // Result is -0 if lhs or rhs is negative.
            masm.movl(lhsCopy, result);
            masm.orl(rhs, result);
            bailoutIf(Assembler::Signed, ins->snapshot());

            masm.mov(ImmWord(0), result);
            masm.jmp(ool.rejoin());
          });
      addOutOfLineCode(ool, mul);

      masm.test32(lhs, lhs);
      masm.j(Assembler::Zero, ool->entry());
      masm.bind(ool->rejoin());
    }
  }
}

template <class LIR>
static void TrapIfDivideByZero(MacroAssembler& masm, LIR* lir, Register rhs) {
  auto* mir = lir->mir();
  MOZ_ASSERT(mir->trapOnError());
  MOZ_ASSERT(mir->canBeDivideByZero());

  Label nonZero;
  masm.branchTest32(Assembler::NonZero, rhs, rhs, &nonZero);
  masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
  masm.bind(&nonZero);
}

OutOfLineCode* CodeGeneratorX86Shared::emitOutOfLineZeroForDivideByZero(
    Register rhs, Register output) {
  // Truncated division by zero is zero: (±Infinity|0 == 0) and (NaN|0 == 0).
  auto* ool = new (alloc()) LambdaOutOfLineCode([=, this](OutOfLineCode& ool) {
    masm.mov(ImmWord(0), output);
    masm.jmp(ool.rejoin());
  });
  masm.branchTest32(Assembler::Zero, rhs, rhs, ool->entry());

  return ool;
}

void CodeGenerator::visitUDiv(LUDiv* ins) {
  Register rhs = ToRegister(ins->rhs());
  Register output = ToRegister(ins->output());
  Register remainder = ToRegister(ins->temp0());

  MOZ_ASSERT(ToRegister(ins->lhs()) == eax);
  MOZ_ASSERT(rhs != eax);
  MOZ_ASSERT(rhs != edx);
  MOZ_ASSERT(output == eax);
  MOZ_ASSERT(remainder == edx);

  MDiv* mir = ins->mir();

  OutOfLineCode* ool = nullptr;

  // Prevent divide by zero.
  if (mir->canBeDivideByZero()) {
    if (mir->trapOnError()) {
      TrapIfDivideByZero(masm, ins, rhs);
    } else if (mir->isTruncated()) {
      ool = emitOutOfLineZeroForDivideByZero(rhs, output);
    } else {
      MOZ_ASSERT(mir->fallible());
      bailoutTest32(Assembler::Zero, rhs, rhs, ins->snapshot());
    }
  }

  // Zero extend the lhs into edx to make (edx:eax), since udiv is 64-bit.
  masm.mov(ImmWord(0), edx);
  masm.udiv(rhs);

  // If the remainder is > 0, bailout since this must be a double.
  if (!mir->canTruncateRemainder()) {
    bailoutTest32(Assembler::NonZero, remainder, remainder, ins->snapshot());
  }

  // Unsigned div can return a value that's not a signed int32.
  // If our users aren't expecting that, bail.
  if (!mir->isTruncated()) {
    bailoutTest32(Assembler::Signed, output, output, ins->snapshot());
  }

  if (ool) {
    addOutOfLineCode(ool, mir);
    masm.bind(ool->rejoin());
  }
}

void CodeGenerator::visitUMod(LUMod* ins) {
  Register rhs = ToRegister(ins->rhs());
  Register output = ToRegister(ins->output());

  MOZ_ASSERT(ToRegister(ins->lhs()) == eax);
  MOZ_ASSERT(rhs != eax);
  MOZ_ASSERT(rhs != edx);
  MOZ_ASSERT(output == edx);
  MOZ_ASSERT(ToRegister(ins->temp0()) == eax);

  MMod* mir = ins->mir();

  OutOfLineCode* ool = nullptr;

  // Prevent divide by zero.
  if (mir->canBeDivideByZero()) {
    if (mir->trapOnError()) {
      TrapIfDivideByZero(masm, ins, rhs);
    } else if (mir->isTruncated()) {
      ool = emitOutOfLineZeroForDivideByZero(rhs, output);
    } else {
      MOZ_ASSERT(mir->fallible());
      bailoutTest32(Assembler::Zero, rhs, rhs, ins->snapshot());
    }
  }

  // Zero extend the lhs into edx to make (edx:eax), since udiv is 64-bit.
  masm.mov(ImmWord(0), edx);
  masm.udiv(rhs);

  // Unsigned mod can return a value that's not a signed int32.
  // If our users aren't expecting that, bail.
  if (!mir->isTruncated()) {
    bailoutTest32(Assembler::Signed, output, output, ins->snapshot());
  }

  if (ool) {
    addOutOfLineCode(ool, mir);
    masm.bind(ool->rejoin());
  }
}

template <class LUDivOrUMod>
static void UnsignedDivideWithConstant(MacroAssembler& masm, LUDivOrUMod* ins,
                                       Register result, Register temp) {
  Register lhs = ToRegister(ins->numerator());
  uint32_t d = ins->denominator();

  MOZ_ASSERT(lhs != result && lhs != temp);
#ifdef JS_CODEGEN_X86
  MOZ_ASSERT(result == edx && temp == eax);
#else
  MOZ_ASSERT(result != temp);
#endif

  // The denominator isn't a power of 2 (see LDivPowTwoI and LModPowTwoI).
  MOZ_ASSERT(!std::has_single_bit(d));

  auto rmc = ReciprocalMulConstants::computeUnsignedDivisionConstants(d);

  // We first compute (M * n) >> 32, where M = rmc.multiplier.
#ifdef JS_CODEGEN_X86
  masm.movl(Imm32(rmc.multiplier), eax);
  masm.umull(lhs);
#else
  // Zero-extend |lhs| in preparation for a 64-bit multiplication.
  masm.movl(lhs, result);

  // Note that imul sign-extends its 32-bit immediate, but we need an unsigned
  // multiplication.
  if (int32_t(rmc.multiplier) >= 0) {
    masm.imulq(Imm32(rmc.multiplier), result, result);
  } else {
    masm.movl(Imm32(rmc.multiplier), temp);
    masm.imulq(temp, result);
  }
  if (rmc.multiplier > UINT32_MAX || rmc.shiftAmount == 0) {
    masm.shrq(Imm32(32), result);
  }
#endif
  if (rmc.multiplier > UINT32_MAX) {
    // M >= 2^32 and shift == 0 is impossible, as d >= 2 implies that
    // ((M * n) >> (32 + shift)) >= n > floor(n/d) whenever n >= d,
    // contradicting the proof of correctness in computeDivisionConstants.
    MOZ_ASSERT(rmc.shiftAmount > 0);
    MOZ_ASSERT(rmc.multiplier < (int64_t(1) << 33));

    // We actually computed result = ((uint32_t(M) * n) >> 32) instead. Since
    // (M * n) >> (32 + shift) is the same as (result + n) >> shift, we can
    // correct for the overflow. This case is a bit trickier than the signed
    // case, though, as the (result + n) addition itself can overflow; however,
    // note that
    // (result + n) >> shift == (((n - result) >> 1) + result) >> (shift - 1),
    // which is overflow-free. See Hacker's Delight, section 10-8 for details.

    // Compute (n - result) >> 1 into temp.
    masm.movl(lhs, temp);
    masm.subl(result, temp);
    masm.shrl(Imm32(1), temp);

    // Finish the computation.
    masm.addl(temp, result);
    if (rmc.shiftAmount > 1) {
      masm.shrl(Imm32(rmc.shiftAmount - 1), result);
    }
  } else {
    if (rmc.shiftAmount > 0) {
#ifdef JS_CODEGEN_X86
      masm.shrl(Imm32(rmc.shiftAmount), result);
#else
      masm.shrq(Imm32(32 + rmc.shiftAmount), result);
#endif
    }
  }
}

void CodeGenerator::visitUDivConstant(LUDivConstant* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register output = ToRegister(ins->output());
  Register temp = ToRegister(ins->temp0());
  uint32_t d = ins->denominator();

  MDiv* mir = ins->mir();

#ifdef JS_CODEGEN_X86
  // This emits the division answer into edx.
  MOZ_ASSERT(output == edx);
  MOZ_ASSERT(temp == eax);
#endif

  if (d == 0) {
    if (mir->trapOnError()) {
      masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
    } else if (mir->isTruncated()) {
      masm.xorl(output, output);
    } else {
      bailout(ins->snapshot());
    }
    return;
  }

  // Compute the truncated division result in |output|.
  UnsignedDivideWithConstant(masm, ins, output, temp);

  if (!mir->isTruncated()) {
    masm.imull(Imm32(d), output, temp);
    bailoutCmp32(Assembler::NotEqual, lhs, temp, ins->snapshot());
  }
}

void CodeGenerator::visitUModConstant(LUModConstant* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register output = ToRegister(ins->output());
  Register temp = ToRegister(ins->temp0());
  uint32_t d = ins->denominator();

  MMod* mir = ins->mir();

#ifdef JS_CODEGEN_X86
  // This emits the modulus answer into eax.
  MOZ_ASSERT(output == eax);
  MOZ_ASSERT(temp == edx);
#endif

  if (d == 0) {
    if (mir->trapOnError()) {
      masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
    } else if (mir->isTruncated()) {
      masm.xorl(output, output);
    } else {
      bailout(ins->snapshot());
    }
    return;
  }

  // Compute the truncated division result in |temp|.
  UnsignedDivideWithConstant(masm, ins, temp, output);

  // We now have the truncated division value in |temp|. If we're computing a
  // modulus or checking whether the division resulted in an integer, we need
  // to multiply the obtained value by d and finish the computation/check.
  //
  // output = lhs - d * temp
  masm.imull(Imm32(d), temp, temp);
  masm.movl(lhs, output);
  masm.subl(temp, output);

  // The final result of the modulus op, just computed above by the
  // sub instruction, can be a number in the range [2^31, 2^32). If
  // this is the case and the modulus is not truncated, we must bail
  // out.
  if (!mir->isTruncated()) {
    bailoutIf(Assembler::Signed, ins->snapshot());
  }
}

void CodeGenerator::visitDivPowTwoI(LDivPowTwoI* ins) {
  Register lhs = ToRegister(ins->numerator());
  DebugOnly<Register> output = ToRegister(ins->output());

  int32_t shift = ins->shift();
  bool negativeDivisor = ins->negativeDivisor();
  MDiv* mir = ins->mir();

  // We use defineReuseInput so these should always be the same, which is
  // convenient since all of our instructions here are two-address.
  MOZ_ASSERT(lhs == output);

  if (!mir->isTruncated() && negativeDivisor) {
    // 0 divided by a negative number must return a double.
    bailoutTest32(Assembler::Zero, lhs, lhs, ins->snapshot());
  }

  if (shift) {
    if (!mir->isTruncated()) {
      // If the remainder is != 0, bailout since this must be a double.
      bailoutTest32(Assembler::NonZero, lhs, Imm32(UINT32_MAX >> (32 - shift)),
                    ins->snapshot());
    }

    if (mir->isUnsigned()) {
      masm.shrl(Imm32(shift), lhs);
    } else {
      // Adjust the value so that shifting produces a correctly
      // rounded result when the numerator is negative. See 10-1
      // "Signed Division by a Known Power of 2" in Henry
      // S. Warren, Jr.'s Hacker's Delight.
      if (mir->canBeNegativeDividend() && mir->isTruncated()) {
        // Note: There is no need to execute this code, which handles how to
        // round the signed integer division towards 0, if we previously bailed
        // due to a non-zero remainder.
        Register lhsCopy = ToRegister(ins->numeratorCopy());
        MOZ_ASSERT(lhsCopy != lhs);
        if (shift > 1) {
          // Copy the sign bit of the numerator. (= (2^32 - 1) or 0)
          masm.sarl(Imm32(31), lhs);
        }
        // Divide by 2^(32 - shift)
        // i.e. (= (2^32 - 1) / 2^(32 - shift) or 0)
        // i.e. (= (2^shift - 1) or 0)
        masm.shrl(Imm32(32 - shift), lhs);
        // If signed, make any 1 bit below the shifted bits to bubble up, such
        // that once shifted the value would be rounded towards 0.
        masm.addl(lhsCopy, lhs);
      }
      masm.sarl(Imm32(shift), lhs);

      if (negativeDivisor) {
        masm.negl(lhs);
      }
    }
    return;
  }

  if (negativeDivisor) {
    // INT32_MIN / -1 overflows.
    masm.negl(lhs);
    if (!mir->isTruncated()) {
      bailoutIf(Assembler::Overflow, ins->snapshot());
    } else if (mir->trapOnError()) {
      Label ok;
      masm.j(Assembler::NoOverflow, &ok);
      masm.wasmTrap(wasm::Trap::IntegerOverflow, mir->trapSiteDesc());
      masm.bind(&ok);
    }
  } else if (mir->isUnsigned() && !mir->isTruncated()) {
    // Unsigned division by 1 can overflow if output is not truncated.
    bailoutTest32(Assembler::Signed, lhs, lhs, ins->snapshot());
  }
}

template <class LDivOrMod>
static void DivideWithConstant(MacroAssembler& masm, LDivOrMod* ins,
                               Register result, Register temp) {
  Register lhs = ToRegister(ins->numerator());
  int32_t d = ins->denominator();

  MOZ_ASSERT(lhs != result && lhs != temp);
#ifdef JS_CODEGEN_X86
  MOZ_ASSERT(result == edx && temp == eax);
#else
  MOZ_ASSERT(result != temp);
#endif

  // The absolute value of the denominator isn't a power of 2 (see LDivPowTwoI
  // and LModPowTwoI).
  MOZ_ASSERT(!std::has_single_bit(mozilla::Abs(d)));

  auto* mir = ins->mir();

  // We will first divide by Abs(d), and negate the answer if d is negative.
  // If desired, this can be avoided by generalizing computeDivisionConstants.
  auto rmc = ReciprocalMulConstants::computeSignedDivisionConstants(d);

  // We first compute (M * n) >> 32, where M = rmc.multiplier.
#ifdef JS_CODEGEN_X86
  masm.movl(Imm32(rmc.multiplier), eax);
  masm.imull(lhs);
#else
  // Sign-extend |lhs| in preparation for a 64-bit multiplication.
  masm.movslq(lhs, result);
  masm.imulq(Imm32(rmc.multiplier), result, result);
  if (rmc.multiplier > INT32_MAX || rmc.shiftAmount == 0) {
    masm.shrq(Imm32(32), result);
  }
#endif
  if (rmc.multiplier > INT32_MAX) {
    MOZ_ASSERT(rmc.multiplier < (int64_t(1) << 32));

    // We actually computed result = ((int32_t(M) * n) >> 32) instead. Since
    // (M * n) >> 32 is the same as (result + n), we can correct for the
    // overflow. (result + n) can't overflow, as n and |result| have opposite
    // signs because int32_t(M) is negative.
    masm.addl(lhs, result);
  }
  // (M * n) >> (32 + shift) is the truncated division answer if n is
  // non-negative, as proved in the comments of computeDivisionConstants. We
  // must add 1 later if n is negative to get the right answer in all cases.
  if (rmc.shiftAmount > 0) {
#ifdef JS_CODEGEN_X86
    masm.sarl(Imm32(rmc.shiftAmount), result);
#else
    if (rmc.multiplier > INT32_MAX) {
      masm.sarl(Imm32(rmc.shiftAmount), result);
    } else {
      masm.sarq(Imm32(32 + rmc.shiftAmount), result);
    }
#endif
  }

  // We'll subtract -1 instead of adding 1, because (n < 0 ? -1 : 0) can be
  // computed with just a sign-extending shift of 31 bits.
  if (mir->canBeNegativeDividend()) {
    masm.movl(lhs, temp);
    masm.sarl(Imm32(31), temp);
    masm.subl(temp, result);
  }

  // After this, |result| contains the correct truncated division result.
  if (d < 0) {
    masm.negl(result);
  }
}

void CodeGenerator::visitDivConstantI(LDivConstantI* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register output = ToRegister(ins->output());
  Register temp = ToRegister(ins->temp0());
  int32_t d = ins->denominator();

  MDiv* mir = ins->mir();

#ifdef JS_CODEGEN_X86
  // This emits the division answer into edx.
  MOZ_ASSERT(output == edx);
  MOZ_ASSERT(temp == eax);
#endif

  if (d == 0) {
    if (mir->trapOnError()) {
      masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
    } else if (mir->isTruncated()) {
      masm.xorl(output, output);
    } else {
      bailout(ins->snapshot());
    }
    return;
  }

  // Compute the truncated division result in |output|.
  DivideWithConstant(masm, ins, output, temp);

  if (!mir->isTruncated()) {
    // This is a division op. Multiply the obtained value by d to check if
    // the correct answer is an integer. This cannot overflow, since |d| > 1.
    masm.imull(Imm32(d), output, temp);
    bailoutCmp32(Assembler::NotEqual, lhs, temp, ins->snapshot());

    // If lhs is zero and the divisor is negative, the answer should have
    // been -0.
    if (d < 0) {
      bailoutTest32(Assembler::Zero, lhs, lhs, ins->snapshot());
    }
  }
}

void CodeGenerator::visitModConstantI(LModConstantI* ins) {
  Register lhs = ToRegister(ins->numerator());
  Register output = ToRegister(ins->output());
  Register temp = ToRegister(ins->temp0());
  int32_t d = ins->denominator();

  MMod* mir = ins->mir();

#ifdef JS_CODEGEN_X86
  // This emits the modulus answer into eax.
  MOZ_ASSERT(output == eax);
  MOZ_ASSERT(temp == edx);
#endif

  if (d == 0) {
    if (mir->trapOnError()) {
      masm.wasmTrap(wasm::Trap::IntegerDivideByZero, mir->trapSiteDesc());
    } else if (mir->isTruncated()) {
      masm.xorl(output, output);
    } else {
      bailout(ins->snapshot());
    }
    return;
  }

  // Compute the truncated division result in |temp|.
  DivideWithConstant(masm, ins, temp, output);

  // Compute the remainder in |output|: output = lhs - d * temp
  masm.imull(Imm32(-d), temp, output);
  masm.addl(lhs, output);

  if (!mir->isTruncated() && mir->canBeNegativeDividend()) {
    // This is a mod op. If the computed value is zero and lhs
    // is negative, the answer should have been -0.
    Label done;
    masm.branch32(Assembler::GreaterThanOrEqual, lhs, Imm32(0), &done);
    bailoutTest32(Assembler::Zero, output, output, ins->snapshot());
    masm.bind(&done);
  }
}

void CodeGenerator::visitDivI(LDivI* ins) {
  Register remainder = ToRegister(ins->temp0());
  Register lhs = ToRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());
  Register output = ToRegister(ins->output());

  MOZ_ASSERT(lhs == eax);
  MOZ_ASSERT(rhs != eax);
  MOZ_ASSERT(rhs != edx);
  MOZ_ASSERT(remainder == edx);
  MOZ_ASSERT(output == eax);

  MDiv* mir = ins->mir();

  Label done;
  OutOfLineCode* ool = nullptr;

  // Handle divide by zero.
  if (mir->canBeDivideByZero()) {
    if (mir->trapOnError()) {
      TrapIfDivideByZero(masm, ins, rhs);
    } else if (mir->canTruncateInfinities()) {
      ool = emitOutOfLineZeroForDivideByZero(rhs, output);
    } else {
      MOZ_ASSERT(mir->fallible());
      bailoutTest32(Assembler::Zero, rhs, rhs, ins->snapshot());
    }
  }

  // Handle an integer overflow exception from -2147483648 / -1.
  if (mir->canBeNegativeOverflow()) {
    Label notOverflow;
    masm.branch32(Assembler::NotEqual, lhs, Imm32(INT32_MIN), &notOverflow);
    if (mir->trapOnError()) {
      masm.branch32(Assembler::NotEqual, rhs, Imm32(-1), &notOverflow);
      masm.wasmTrap(wasm::Trap::IntegerOverflow, mir->trapSiteDesc());
    } else if (mir->canTruncateOverflow()) {
      // (-INT32_MIN)|0 == INT32_MIN and INT32_MIN is already in the
      // output register (lhs == eax).
      masm.branch32(Assembler::Equal, rhs, Imm32(-1), &done);
    } else {
      MOZ_ASSERT(mir->fallible());
      bailoutCmp32(Assembler::Equal, rhs, Imm32(-1), ins->snapshot());
    }
    masm.bind(&notOverflow);
  }

  // Handle negative 0.
  if (!mir->canTruncateNegativeZero() && mir->canBeNegativeZero()) {
    Label nonzero;
    masm.branchTest32(Assembler::NonZero, lhs, lhs, &nonzero);
    bailoutCmp32(Assembler::LessThan, rhs, Imm32(0), ins->snapshot());
    masm.bind(&nonzero);
  }

  // Sign extend the lhs into edx to make (edx:eax), since idiv is 64-bit.
  masm.cdq();
  masm.idiv(rhs);

  if (!mir->canTruncateRemainder()) {
    // If the remainder is > 0, bailout since this must be a double.
    bailoutTest32(Assembler::NonZero, remainder, remainder, ins->snapshot());
  }

  masm.bind(&done);

  if (ool) {
    addOutOfLineCode(ool, mir);
    masm.bind(ool->rejoin());
  }
}

void CodeGenerator::visitModPowTwoI(LModPowTwoI* ins) {
  Register lhs = ToRegister(ins->input());
  int32_t shift = ins->shift();
  bool canBeNegative =
      !ins->mir()->isUnsigned() && ins->mir()->canBeNegativeDividend();

  if (shift == 0) {
    if (canBeNegative && !ins->mir()->isTruncated()) {
      bailoutTest32(Assembler::Signed, lhs, lhs, ins->snapshot());
    }
    masm.xorl(lhs, lhs);
    return;
  }

  auto clearHighBits = [&]() {
    switch (shift) {
      case 16:
        masm.movzwl(lhs, lhs);
        break;
      case 8:
        if (AllocatableGeneralRegisterSet(Registers::SingleByteRegs).has(lhs)) {
          masm.movzbl(lhs, lhs);
          break;
        }
        [[fallthrough]];
      default:
        masm.andl(Imm32((uint32_t(1) << shift) - 1), lhs);
        break;
    }
  };

  Label negative;

  if (canBeNegative) {
    // Switch based on sign of the lhs.
    // Positive numbers are just a bitmask
    masm.branchTest32(Assembler::Signed, lhs, lhs, &negative);
  }

  clearHighBits();

  if (canBeNegative) {
    Label done;
    masm.jump(&done);

    // Negative numbers need a negate, bitmask, negate
    masm.bind(&negative);

    // Unlike in the visitModI case, we are not computing the mod by means of a
    // division. Therefore, the divisor = -1 case isn't problematic (the andl
    // always returns 0, which is what we expect).
    //
    // The negl instruction overflows if lhs == INT32_MIN, but this is also not
    // a problem: shift is at most 31, and so the andl also always returns 0.
    masm.negl(lhs);
    clearHighBits();
    masm.negl(lhs);

    // Since a%b has the same sign as b, and a is negative in this branch,
    // an answer of 0 means the correct result is actually -0. Bail out.
    if (!ins->mir()->isTruncated()) {
      bailoutIf(Assembler::Zero, ins->snapshot());
    }
    masm.bind(&done);
  }
}

class ModOverflowCheck : public OutOfLineCodeBase<CodeGeneratorX86Shared> {
  Label done_;
  LModI* ins_;
  Register rhs_;

 public:
  explicit ModOverflowCheck(LModI* ins, Register rhs) : ins_(ins), rhs_(rhs) {}

  virtual void accept(CodeGeneratorX86Shared* codegen) override {
    codegen->visitModOverflowCheck(this);
  }
  Label* done() { return &done_; }
  LModI* ins() const { return ins_; }
  Register rhs() const { return rhs_; }
};

void CodeGeneratorX86Shared::visitModOverflowCheck(ModOverflowCheck* ool) {
  masm.cmp32(ool->rhs(), Imm32(-1));
  if (ool->ins()->mir()->isTruncated()) {
    masm.j(Assembler::NotEqual, ool->rejoin());
    masm.mov(ImmWord(0), edx);
    masm.jmp(ool->done());
  } else {
    bailoutIf(Assembler::Equal, ool->ins()->snapshot());
    masm.jmp(ool->rejoin());
  }
}

void CodeGenerator::visitModI(LModI* ins) {
  Register remainder = ToRegister(ins->output());
  Register lhs = ToRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());

  // Required to use idiv.
  MOZ_ASSERT(lhs == eax);
  MOZ_ASSERT(rhs != eax);
  MOZ_ASSERT(rhs != edx);
  MOZ_ASSERT(remainder == edx);
  MOZ_ASSERT(ToRegister(ins->temp0()) == eax);

  MMod* mir = ins->mir();

  Label done;
  OutOfLineCode* ool = nullptr;
  ModOverflowCheck* overflow = nullptr;

  // Prevent divide by zero.
  if (mir->canBeDivideByZero()) {
    if (mir->trapOnError()) {
      TrapIfDivideByZero(masm, ins, rhs);
    } else if (mir->isTruncated()) {
      ool = emitOutOfLineZeroForDivideByZero(rhs, remainder);
    } else {
      MOZ_ASSERT(mir->fallible());
      bailoutTest32(Assembler::Zero, rhs, rhs, ins->snapshot());
    }
  }

  Label negative;

  // Switch based on sign of the lhs.
  if (mir->canBeNegativeDividend()) {
    masm.branchTest32(Assembler::Signed, lhs, lhs, &negative);
  }

  // If lhs >= 0 then remainder = lhs % rhs. The remainder must be positive.
  {
    // Check if rhs is a power-of-two.
    if (mir->canBePowerOfTwoDivisor()) {
      MOZ_ASSERT(rhs != remainder);

      // Rhs y is a power-of-two if (y & (y-1)) == 0. Note that if
      // y is any negative number other than INT32_MIN, both y and
      // y-1 will have the sign bit set so these are never optimized
      // as powers-of-two. If y is INT32_MIN, y-1 will be INT32_MAX
      // and because lhs >= 0 at this point, lhs & INT32_MAX returns
      // the correct value.
      Label notPowerOfTwo;
      masm.mov(rhs, remainder);
      masm.subl(Imm32(1), remainder);
      masm.branchTest32(Assembler::NonZero, remainder, rhs, &notPowerOfTwo);
      {
        masm.andl(lhs, remainder);
        masm.jmp(&done);
      }
      masm.bind(&notPowerOfTwo);
    }

    // Since lhs >= 0, the sign-extension will be 0
    masm.mov(ImmWord(0), edx);
    masm.idiv(rhs);
  }

  // Otherwise, we have to beware of two special cases:
  if (mir->canBeNegativeDividend()) {
    masm.jump(&done);

    masm.bind(&negative);

    // Prevent an integer overflow exception from -2147483648 % -1
    overflow = new (alloc()) ModOverflowCheck(ins, rhs);
    masm.branch32(Assembler::Equal, lhs, Imm32(INT32_MIN), overflow->entry());
    masm.bind(overflow->rejoin());

    masm.cdq();
    masm.idiv(rhs);

    if (!mir->isTruncated()) {
      // A remainder of 0 means that the rval must be -0, which is a double.
      bailoutTest32(Assembler::Zero, remainder, remainder, ins->snapshot());
    }
  }

  masm.bind(&done);

  if (overflow) {
    addOutOfLineCode(overflow, mir);
    masm.bind(overflow->done());
  }

  if (ool) {
    addOutOfLineCode(ool, mir);
    masm.bind(ool->rejoin());
  }
}

void CodeGenerator::visitBitNotI(LBitNotI* ins) {
  Register input = ToRegister(ins->input());
  MOZ_ASSERT(input == ToRegister(ins->output()));

  masm.notl(input);
}

void CodeGenerator::visitBitOpI(LBitOpI* ins) {
  Register lhs = ToRegister(ins->lhs());
  const LAllocation* rhs = ins->rhs();

  MOZ_ASSERT(lhs == ToRegister(ins->output()));

  switch (ins->bitop()) {
    case JSOp::BitOr:
      if (rhs->isConstant()) {
        masm.orl(Imm32(ToInt32(rhs)), lhs);
      } else {
        masm.orl(ToOperand(rhs), lhs);
      }
      break;
    case JSOp::BitXor:
      if (rhs->isConstant()) {
        masm.xorl(Imm32(ToInt32(rhs)), lhs);
      } else {
        masm.xorl(ToOperand(rhs), lhs);
      }
      break;
    case JSOp::BitAnd:
      if (rhs->isConstant()) {
        int32_t mask = ToInt32(rhs);
        if (mask == 0xffff) {
          masm.movzwl(lhs, lhs);
        } else if (mask == 0xff &&
                   AllocatableGeneralRegisterSet(Registers::SingleByteRegs)
                       .has(lhs)) {
          masm.movzbl(lhs, lhs);
        } else {
          masm.andl(Imm32(mask), lhs);
        }
      } else {
        masm.andl(ToOperand(rhs), lhs);
      }
      break;
    default:
      MOZ_CRASH("unexpected binary opcode");
  }
}

void CodeGenerator::visitBitOpI64(LBitOpI64* lir) {
  Register64 lhs = ToRegister64(lir->lhs());
  LInt64Allocation rhs = lir->rhs();

  MOZ_ASSERT(ToOutRegister64(lir) == lhs);

  switch (lir->bitop()) {
    case JSOp::BitOr:
      if (IsConstant(rhs)) {
        masm.or64(Imm64(ToInt64(rhs)), lhs);
      } else {
        masm.or64(ToOperandOrRegister64(rhs), lhs);
      }
      break;
    case JSOp::BitXor:
      if (IsConstant(rhs)) {
        masm.xor64(Imm64(ToInt64(rhs)), lhs);
      } else {
        masm.xor64(ToOperandOrRegister64(rhs), lhs);
      }
      break;
    case JSOp::BitAnd:
      if (IsConstant(rhs)) {
        masm.and64(Imm64(ToInt64(rhs)), lhs);
      } else {
        masm.and64(ToOperandOrRegister64(rhs), lhs);
      }
      break;
    default:
      MOZ_CRASH("unexpected binary opcode");
  }
}

void CodeGenerator::visitShiftI(LShiftI* ins) {
  Register lhs = ToRegister(ins->lhs());
  const LAllocation* rhs = ins->rhs();
  Register out = ToRegister(ins->output());

  if (rhs->isConstant()) {
    MOZ_ASSERT(out == lhs);

    int32_t shift = ToInt32(rhs) & 0x1F;
    switch (ins->bitop()) {
      case JSOp::Lsh:
        if (shift) {
          masm.lshift32(Imm32(shift), lhs);
        }
        break;
      case JSOp::Rsh:
        if (shift) {
          masm.rshift32Arithmetic(Imm32(shift), lhs);
        }
        break;
      case JSOp::Ursh:
        if (shift) {
          masm.rshift32(Imm32(shift), lhs);
        } else if (ins->mir()->toUrsh()->fallible()) {
          // x >>> 0 can overflow.
          masm.test32(lhs, lhs);
          bailoutIf(Assembler::Signed, ins->snapshot());
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
          masm.shlxl(lhs, shift, out);
        } else {
          masm.lshift32(shift, lhs);
        }
        break;
      case JSOp::Rsh:
        if (out != lhs) {
          masm.sarxl(lhs, shift, out);
        } else {
          masm.rshift32Arithmetic(shift, lhs);
        }
        break;
      case JSOp::Ursh:
        if (out != lhs) {
          masm.shrxl(lhs, shift, out);
        } else {
          masm.rshift32(shift, lhs);
        }
        if (ins->mir()->toUrsh()->fallible()) {
          // x >>> 0 can overflow.
          masm.test32(out, out);
          bailoutIf(Assembler::Signed, ins->snapshot());
        }
        break;
      default:
        MOZ_CRASH("Unexpected shift op");
    }
  }
}

void CodeGenerator::visitUrshD(LUrshD* ins) {
  Register lhs = ToRegister(ins->lhs());
  const LAllocation* rhs = ins->rhs();
  FloatRegister out = ToFloatRegister(ins->output());
  Register temp = ToRegister(ins->temp0());

  if (rhs->isConstant()) {
    MOZ_ASSERT(temp == lhs);

    int32_t shift = ToInt32(rhs) & 0x1F;
    if (shift) {
      masm.shrl(Imm32(shift), lhs);
    }
  } else {
    MOZ_ASSERT_IF(temp != lhs, Assembler::HasBMI2());

    Register shift = ToRegister(rhs);
    if (temp != lhs) {
      masm.shrxl(lhs, shift, temp);
    } else {
      masm.rshift32(shift, lhs);
    }
  }

  masm.convertUInt32ToDouble(temp, out);
}

Operand CodeGeneratorX86Shared::ToOperand(const LAllocation& a) {
  if (a.isGeneralReg()) {
    return Operand(a.toGeneralReg()->reg());
  }
  if (a.isFloatReg()) {
    return Operand(a.toFloatReg()->reg());
  }
  return Operand(ToAddress(a));
}

Operand CodeGeneratorX86Shared::ToOperand(const LAllocation* a) {
  return ToOperand(*a);
}

MoveOperand CodeGeneratorX86Shared::toMoveOperand(LAllocation a) const {
  if (a.isGeneralReg()) {
    return MoveOperand(ToRegister(a));
  }
  if (a.isFloatReg()) {
    return MoveOperand(ToFloatRegister(a));
  }
  MoveOperand::Kind kind = a.isStackArea() ? MoveOperand::Kind::EffectiveAddress
                                           : MoveOperand::Kind::Memory;
  return MoveOperand(ToAddress(a), kind);
}

class OutOfLineTableSwitch : public OutOfLineCodeBase<CodeGeneratorX86Shared> {
  MTableSwitch* mir_;
  CodeLabel jumpLabel_;

  void accept(CodeGeneratorX86Shared* codegen) override {
    codegen->visitOutOfLineTableSwitch(this);
  }

 public:
  explicit OutOfLineTableSwitch(MTableSwitch* mir) : mir_(mir) {}

  MTableSwitch* mir() const { return mir_; }

  CodeLabel* jumpLabel() { return &jumpLabel_; }
};

void CodeGeneratorX86Shared::visitOutOfLineTableSwitch(
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

void CodeGeneratorX86Shared::emitTableSwitchDispatch(MTableSwitch* mir,
                                                     Register index,
                                                     Register base) {
  Label* defaultcase = skipTrivialBlocks(mir->getDefault())->lir()->label();

  // Lower value with low value
  if (mir->low() != 0) {
    masm.subl(Imm32(mir->low()), index);
  }

  // Jump to default case if input is out of range
  int32_t cases = mir->numCases();
  masm.cmp32(index, Imm32(cases));
  masm.j(AssemblerX86Shared::AboveOrEqual, defaultcase);

  // To fill in the CodeLabels for the case entries, we need to first
  // generate the case entries (we don't yet know their offsets in the
  // instruction stream).
  OutOfLineTableSwitch* ool = new (alloc()) OutOfLineTableSwitch(mir);
  addOutOfLineCode(ool, mir);

  // Compute the position where a pointer to the right case stands.
  masm.mov(ool->jumpLabel(), base);
  BaseIndex pointer(base, index, ScalePointer);

  // Jump to the right case
  masm.branchToComputedAddress(pointer);
}

void CodeGenerator::visitMathD(LMathD* math) {
  FloatRegister lhs = ToFloatRegister(math->lhs());
  Operand rhs = ToOperand(math->rhs());
  FloatRegister output = ToFloatRegister(math->output());

  switch (math->jsop()) {
    case JSOp::Add:
      masm.vaddsd(rhs, lhs, output);
      break;
    case JSOp::Sub:
      masm.vsubsd(rhs, lhs, output);
      break;
    case JSOp::Mul:
      masm.vmulsd(rhs, lhs, output);
      break;
    case JSOp::Div:
      masm.vdivsd(rhs, lhs, output);
      break;
    default:
      MOZ_CRASH("unexpected opcode");
  }
}

void CodeGenerator::visitMathF(LMathF* math) {
  FloatRegister lhs = ToFloatRegister(math->lhs());
  Operand rhs = ToOperand(math->rhs());
  FloatRegister output = ToFloatRegister(math->output());

  switch (math->jsop()) {
    case JSOp::Add:
      masm.vaddss(rhs, lhs, output);
      break;
    case JSOp::Sub:
      masm.vsubss(rhs, lhs, output);
      break;
    case JSOp::Mul:
      masm.vmulss(rhs, lhs, output);
      break;
    case JSOp::Div:
      masm.vdivss(rhs, lhs, output);
      break;
    default:
      MOZ_CRASH("unexpected opcode");
  }
}

void CodeGenerator::visitEffectiveAddress3(LEffectiveAddress3* ins) {
  const MEffectiveAddress3* mir = ins->mir();
  Register base = ToRegister(ins->base());
  Register index = ToRegister(ins->index());
  Register output = ToRegister(ins->output());
  // Regarding performance, we rely on the fact that, if `mir->displacement()`
  // is zero, `masm` will generate a 2-addend `leal`, and not a 3-addend one
  // with a zero constant, since that is slower on some processors.
  // See comments in EffectiveAddressAnalysis.cpp.
  masm.leal(Operand(base, index, mir->scale(), mir->displacement()), output);
}

void CodeGenerator::visitEffectiveAddress2(LEffectiveAddress2* ins) {
  const MEffectiveAddress2* mir = ins->mir();
  Register index = ToRegister(ins->index());
  Register output = ToRegister(ins->output());
  masm.leal(Operand(index, mir->scale(), mir->displacement()), output);
}

void CodeGeneratorX86Shared::generateInvalidateEpilogue() {
  // Ensure that there is enough space in the buffer for the OsiPoint
  // patching to occur. Otherwise, we could overwrite the invalidation
  // epilogue.
  for (size_t i = 0; i < sizeof(void*); i += Assembler::NopSize()) {
    masm.nop();
  }

  masm.bind(&invalidate_);

  // Push the Ion script onto the stack (when we determine what that pointer
  // is).
  invalidateEpilogueData_ = masm.pushWithPatch(ImmWord(uintptr_t(-1)));

  // Jump to the invalidator which will replace the current frame.
  TrampolinePtr thunk = gen->jitRuntime()->getInvalidationThunk();
  masm.jump(thunk);
}

void CodeGenerator::visitNegI(LNegI* ins) {
  Register input = ToRegister(ins->input());
  MOZ_ASSERT(input == ToRegister(ins->output()));

  masm.neg32(input);
}

void CodeGenerator::visitNegI64(LNegI64* ins) {
  Register64 input = ToRegister64(ins->input());
  MOZ_ASSERT(input == ToOutRegister64(ins));
  masm.neg64(input);
}

void CodeGenerator::visitNegD(LNegD* ins) {
  FloatRegister input = ToFloatRegister(ins->input());
  FloatRegister output = ToFloatRegister(ins->output());

  // XOR the float in a float register with -0.0.
  masm.vxorpdSimd128(SimdConstant::SplatX2(-0.0), input, output);
}

void CodeGenerator::visitNegF(LNegF* ins) {
  FloatRegister input = ToFloatRegister(ins->input());
  FloatRegister output = ToFloatRegister(ins->output());

  // XOR the float in a float register with -0.0.
  masm.vxorpsSimd128(SimdConstant::SplatX4(-0.0f), input, output);
}

void CodeGenerator::visitCompareExchangeTypedArrayElement(
    LCompareExchangeTypedArrayElement* lir) {
  Register elements = ToRegister(lir->elements());
  Register output = ToRegister(lir->output());

  Register oldval = ToRegister(lir->oldval());
  Register newval = ToRegister(lir->newval());

  Scalar::Type arrayType = lir->mir()->arrayType();

  auto dest = ToAddressOrBaseIndex(elements, lir->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.compareExchange(arrayType, Synchronization::Full(), dest, oldval,
                         newval, output);
  });
}

void CodeGenerator::visitAtomicExchangeTypedArrayElement(
    LAtomicExchangeTypedArrayElement* lir) {
  Register elements = ToRegister(lir->elements());
  Register output = ToRegister(lir->output());

  Register value = ToRegister(lir->value());

  Scalar::Type arrayType = lir->mir()->arrayType();

  auto dest = ToAddressOrBaseIndex(elements, lir->index(), arrayType);

  dest.match([&](const auto& dest) {
    masm.atomicExchange(arrayType, Synchronization::Full(), dest, value,
                        output);
  });
}

void CodeGenerator::visitAtomicTypedArrayElementBinop(
    LAtomicTypedArrayElementBinop* lir) {
  MOZ_ASSERT(!lir->mir()->isForEffect());

  Register output = ToRegister(lir->output());
  Register elements = ToRegister(lir->elements());
  Register temp = ToTempRegisterOrInvalid(lir->temp0());
  const LAllocation* value = lir->value();

  Scalar::Type arrayType = lir->mir()->arrayType();
  AtomicOp atomicOp = lir->mir()->operation();

  auto mem = ToAddressOrBaseIndex(elements, lir->index(), arrayType);

  mem.match([&](const auto& mem) {
    if (value->isConstant()) {
      masm.atomicFetchOp(arrayType, Synchronization::Full(), atomicOp,
                         Imm32(ToInt32(value)), mem, temp, output);
    } else {
      masm.atomicFetchOp(arrayType, Synchronization::Full(), atomicOp,
                         ToRegister(value), mem, temp, output);
    }
  });
}

void CodeGenerator::visitAtomicTypedArrayElementBinopForEffect(
    LAtomicTypedArrayElementBinopForEffect* lir) {
  MOZ_ASSERT(lir->mir()->isForEffect());

  Register elements = ToRegister(lir->elements());
  const LAllocation* value = lir->value();
  Scalar::Type arrayType = lir->mir()->arrayType();
  AtomicOp atomicOp = lir->mir()->operation();

  auto mem = ToAddressOrBaseIndex(elements, lir->index(), arrayType);

  mem.match([&](const auto& mem) {
    if (value->isConstant()) {
      masm.atomicEffectOp(arrayType, Synchronization::Full(), atomicOp,
                          Imm32(ToInt32(value)), mem, InvalidReg);
    } else {
      masm.atomicEffectOp(arrayType, Synchronization::Full(), atomicOp,
                          ToRegister(value), mem, InvalidReg);
    }
  });
}

void CodeGeneratorX86Shared::visitOutOfLineWasmTruncateCheck(
    OutOfLineWasmTruncateCheck* ool) {
  FloatRegister input = ool->input();
  Register output = ool->output();
  Register64 output64 = ool->output64();
  MIRType fromType = ool->fromType();
  MIRType toType = ool->toType();
  Label* oolRejoin = ool->rejoin();
  TruncFlags flags = ool->flags();
  const wasm::TrapSiteDesc& trapSiteDesc = ool->trapSiteDesc();

  if (fromType == MIRType::Float32) {
    if (toType == MIRType::Int32) {
      masm.oolWasmTruncateCheckF32ToI32(input, output, flags, trapSiteDesc,
                                        oolRejoin, nullptr, nullptr);
    } else if (toType == MIRType::Int64) {
      masm.oolWasmTruncateCheckF32ToI64(input, output64, flags, trapSiteDesc,
                                        oolRejoin, nullptr, nullptr);
    } else {
      MOZ_CRASH("unexpected type");
    }
  } else if (fromType == MIRType::Double) {
    if (toType == MIRType::Int32) {
      masm.oolWasmTruncateCheckF64ToI32(input, output, flags, trapSiteDesc,
                                        oolRejoin, nullptr, nullptr);
    } else if (toType == MIRType::Int64) {
      masm.oolWasmTruncateCheckF64ToI64(input, output64, flags, trapSiteDesc,
                                        oolRejoin, nullptr, nullptr);
    } else {
      MOZ_CRASH("unexpected type");
    }
  } else {
    MOZ_CRASH("unexpected type");
  }
}

template <typename T>
Operand CodeGeneratorX86Shared::toMemoryAccessOperand(T* lir, int32_t disp) {
  const LAllocation* ptr = lir->ptr();
  const LAllocation* memoryBase = lir->memoryBase();
#ifdef JS_CODEGEN_X86
  Operand destAddr = ptr->isBogus() ? Operand(ToRegister(memoryBase), disp)
                                    : Operand(ToRegister(memoryBase),
                                              ToRegister(ptr), TimesOne, disp);
#else
  auto baseReg = memoryBase->isBogus() ? HeapReg : ToRegister(memoryBase);
  Operand destAddr = ptr->isBogus()
                         ? Operand(baseReg, disp)
                         : Operand(baseReg, ToRegister(ptr), TimesOne, disp);
#endif
  return destAddr;
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
    case wasm::SimdOp::V128Bitselect: {
      FloatRegister lhsDest = ToFloatRegister(ins->v0());
      FloatRegister rhs = ToFloatRegister(ins->v1());
      FloatRegister control = ToFloatRegister(ins->v2());
      FloatRegister temp = ToFloatRegister(ins->temp0());
      masm.bitwiseSelectSimd128(control, lhsDest, rhs, lhsDest, temp);
      break;
    }
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
      FloatRegister lhs = ToFloatRegister(ins->v0());
      FloatRegister rhs = ToFloatRegister(ins->v1());
      FloatRegister mask = ToFloatRegister(ins->v2());
      FloatRegister dest = ToFloatRegister(ins->output());
      masm.laneSelectSimd128(mask, lhs, rhs, dest);
      break;
    }
    case wasm::SimdOp::I32x4RelaxedDotI8x16I7x16AddS:
      masm.dotInt8x16Int7x16ThenAdd(ToFloatRegister(ins->v0()),
                                    ToFloatRegister(ins->v1()),
                                    ToFloatRegister(ins->v2()));
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
  FloatRegister temp1 = ToTempFloatRegisterOrInvalid(ins->temp0());
  FloatRegister temp2 = ToTempFloatRegisterOrInvalid(ins->temp1());
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
      // x86/x64 specific: The CPU provides ~A & B.  The operands were swapped
      // during lowering, and we'll compute A & ~B here as desired.
      masm.bitwiseNotAndSimd128(lhs, rhs, dest);
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
    case wasm::SimdOp::I64x2Mul:
      masm.mulInt64x2(lhs, rhs, dest, temp1);
      break;
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
      masm.minFloat32x4(lhs, rhs, dest, temp1, temp2);
      break;
    case wasm::SimdOp::F32x4Max:
      masm.maxFloat32x4(lhs, rhs, dest, temp1, temp2);
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
      masm.minFloat64x2(lhs, rhs, dest, temp1, temp2);
      break;
    case wasm::SimdOp::F64x2Max:
      masm.maxFloat64x2(lhs, rhs, dest, temp1, temp2);
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
      masm.compareForEqualityInt64x2(Assembler::Equal, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2Ne:
      masm.compareForEqualityInt64x2(Assembler::NotEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2LtS:
      masm.compareForOrderingInt64x2(Assembler::LessThan, lhs, rhs, dest, temp1,
                                     temp2);
      break;
    case wasm::SimdOp::I64x2GtS:
      masm.compareForOrderingInt64x2(Assembler::GreaterThan, lhs, rhs, dest,
                                     temp1, temp2);
      break;
    case wasm::SimdOp::I64x2LeS:
      masm.compareForOrderingInt64x2(Assembler::LessThanOrEqual, lhs, rhs, dest,
                                     temp1, temp2);
      break;
    case wasm::SimdOp::I64x2GeS:
      masm.compareForOrderingInt64x2(Assembler::GreaterThanOrEqual, lhs, rhs,
                                     dest, temp1, temp2);
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
    case wasm::SimdOp::F32x4Le:
      masm.compareFloat32x4(Assembler::LessThanOrEqual, lhs, rhs, dest);
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
    case wasm::SimdOp::F64x2Le:
      masm.compareFloat64x2(Assembler::LessThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4PMax:
      // `lhs` and `rhs` are swapped, for non-VEX platforms the output is rhs.
      masm.pseudoMaxFloat32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4PMin:
      // `lhs` and `rhs` are swapped, for non-VEX platforms the output is rhs.
      masm.pseudoMinFloat32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2PMax:
      // `lhs` and `rhs` are swapped, for non-VEX platforms the output is rhs.
      masm.pseudoMaxFloat64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2PMin:
      // `lhs` and `rhs` are swapped, for non-VEX platforms the output is rhs.
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
    case wasm::SimdOp::MozPMADDUBSW:
      masm.vpmaddubsw(rhs, lhs, dest);
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
  FloatRegister temp = ToTempFloatRegisterOrInvalid(ins->temp0());

  switch (ins->mir()->simdOp()) {
    case wasm::SimdOp::I8x16Add:
      masm.addInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8Add:
      masm.addInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4Add:
      masm.addInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2Add:
      masm.addInt64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16Sub:
      masm.subInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8Sub:
      masm.subInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4Sub:
      masm.subInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2Sub:
      masm.subInt64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8Mul:
      masm.mulInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4Mul:
      masm.mulInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16AddSatS:
      masm.addSatInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16AddSatU:
      masm.unsignedAddSatInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8AddSatS:
      masm.addSatInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8AddSatU:
      masm.unsignedAddSatInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16SubSatS:
      masm.subSatInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16SubSatU:
      masm.unsignedSubSatInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8SubSatS:
      masm.subSatInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8SubSatU:
      masm.unsignedSubSatInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16MinS:
      masm.minInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16MinU:
      masm.unsignedMinInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8MinS:
      masm.minInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8MinU:
      masm.unsignedMinInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4MinS:
      masm.minInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4MinU:
      masm.unsignedMinInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16MaxS:
      masm.maxInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16MaxU:
      masm.unsignedMaxInt8x16(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8MaxS:
      masm.maxInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8MaxU:
      masm.unsignedMaxInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4MaxS:
      masm.maxInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4MaxU:
      masm.unsignedMaxInt32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::V128And:
      masm.bitwiseAndSimd128(lhs, rhs, dest);
      break;
    case wasm::SimdOp::V128Or:
      masm.bitwiseOrSimd128(lhs, rhs, dest);
      break;
    case wasm::SimdOp::V128Xor:
      masm.bitwiseXorSimd128(lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16Eq:
      masm.compareInt8x16(Assembler::Equal, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16Ne:
      masm.compareInt8x16(Assembler::NotEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16GtS:
      masm.compareInt8x16(Assembler::GreaterThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I8x16LeS:
      masm.compareInt8x16(Assembler::LessThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8Eq:
      masm.compareInt16x8(Assembler::Equal, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8Ne:
      masm.compareInt16x8(Assembler::NotEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8GtS:
      masm.compareInt16x8(Assembler::GreaterThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I16x8LeS:
      masm.compareInt16x8(Assembler::LessThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4Eq:
      masm.compareInt32x4(Assembler::Equal, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4Ne:
      masm.compareInt32x4(Assembler::NotEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4GtS:
      masm.compareInt32x4(Assembler::GreaterThan, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4LeS:
      masm.compareInt32x4(Assembler::LessThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I64x2Mul:
      masm.mulInt64x2(lhs, rhs, dest, temp);
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
    case wasm::SimdOp::F32x4Le:
      masm.compareFloat32x4(Assembler::LessThanOrEqual, lhs, rhs, dest);
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
    case wasm::SimdOp::F64x2Le:
      masm.compareFloat64x2(Assembler::LessThanOrEqual, lhs, rhs, dest);
      break;
    case wasm::SimdOp::I32x4DotI16x8S:
      masm.widenDotInt16x8(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4Add:
      masm.addFloat32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Add:
      masm.addFloat64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4Sub:
      masm.subFloat32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Sub:
      masm.subFloat64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4Div:
      masm.divFloat32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Div:
      masm.divFloat64x2(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F32x4Mul:
      masm.mulFloat32x4(lhs, rhs, dest);
      break;
    case wasm::SimdOp::F64x2Mul:
      masm.mulFloat64x2(lhs, rhs, dest);
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
    default:
      MOZ_CRASH("Binary SimdOp with constant not implemented");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmVariableShiftSimd128(
    LWasmVariableShiftSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  FloatRegister lhsDest = ToFloatRegister(ins->lhs());
  Register rhs = ToRegister(ins->rhs());
  FloatRegister temp = ToTempFloatRegisterOrInvalid(ins->temp0());

  MOZ_ASSERT(ToFloatRegister(ins->output()) == lhsDest);

  switch (ins->mir()->simdOp()) {
    case wasm::SimdOp::I8x16Shl:
      masm.leftShiftInt8x16(rhs, lhsDest, temp);
      break;
    case wasm::SimdOp::I8x16ShrS:
      masm.rightShiftInt8x16(rhs, lhsDest, temp);
      break;
    case wasm::SimdOp::I8x16ShrU:
      masm.unsignedRightShiftInt8x16(rhs, lhsDest, temp);
      break;
    case wasm::SimdOp::I16x8Shl:
      masm.leftShiftInt16x8(rhs, lhsDest);
      break;
    case wasm::SimdOp::I16x8ShrS:
      masm.rightShiftInt16x8(rhs, lhsDest);
      break;
    case wasm::SimdOp::I16x8ShrU:
      masm.unsignedRightShiftInt16x8(rhs, lhsDest);
      break;
    case wasm::SimdOp::I32x4Shl:
      masm.leftShiftInt32x4(rhs, lhsDest);
      break;
    case wasm::SimdOp::I32x4ShrS:
      masm.rightShiftInt32x4(rhs, lhsDest);
      break;
    case wasm::SimdOp::I32x4ShrU:
      masm.unsignedRightShiftInt32x4(rhs, lhsDest);
      break;
    case wasm::SimdOp::I64x2Shl:
      masm.leftShiftInt64x2(rhs, lhsDest);
      break;
    case wasm::SimdOp::I64x2ShrS:
      masm.rightShiftInt64x2(rhs, lhsDest, temp);
      break;
    case wasm::SimdOp::I64x2ShrU:
      masm.unsignedRightShiftInt64x2(rhs, lhsDest);
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
  int32_t shift = ins->shift();

  if (shift == 0) {
    masm.moveSimd128(src, dest);
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
#ifdef ENABLE_JIT_SIMD
  FloatRegister src = ToFloatRegister(ins->src());
  FloatRegister dest = ToFloatRegister(ins->output());

  switch (ins->mir()->simdOp()) {
    case wasm::SimdOp::I8x16ShrS:
      masm.signReplicationInt8x16(src, dest);
      break;
    case wasm::SimdOp::I16x8ShrS:
      masm.signReplicationInt16x8(src, dest);
      break;
    case wasm::SimdOp::I32x4ShrS:
      masm.signReplicationInt32x4(src, dest);
      break;
    case wasm::SimdOp::I64x2ShrS:
      masm.signReplicationInt64x2(src, dest);
      break;
    default:
      MOZ_CRASH("Shift SimdOp unsupported sign replication optimization");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmShuffleSimd128(LWasmShuffleSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  FloatRegister lhsDest = ToFloatRegister(ins->lhs());
  FloatRegister rhs = ToFloatRegister(ins->rhs());
  SimdConstant control = ins->control();
  FloatRegister output = ToFloatRegister(ins->output());
  switch (ins->op()) {
    case SimdShuffleOp::BLEND_8x16: {
      masm.blendInt8x16(reinterpret_cast<const uint8_t*>(control.asInt8x16()),
                        lhsDest, rhs, output, ToFloatRegister(ins->temp0()));
      break;
    }
    case SimdShuffleOp::BLEND_16x8: {
      MOZ_ASSERT(ins->temp0()->isBogusTemp());
      masm.blendInt16x8(reinterpret_cast<const uint16_t*>(control.asInt16x8()),
                        lhsDest, rhs, output);
      break;
    }
    case SimdShuffleOp::CONCAT_RIGHT_SHIFT_8x16: {
      MOZ_ASSERT(ins->temp0()->isBogusTemp());
      int8_t count = 16 - control.asInt8x16()[0];
      MOZ_ASSERT(count > 0, "Should have been a MOVE operation");
      masm.concatAndRightShiftSimd128(lhsDest, rhs, output, count);
      break;
    }
    case SimdShuffleOp::INTERLEAVE_HIGH_8x16: {
      MOZ_ASSERT(ins->temp0()->isBogusTemp());
      masm.interleaveHighInt8x16(lhsDest, rhs, output);
      break;
    }
    case SimdShuffleOp::INTERLEAVE_HIGH_16x8: {
      MOZ_ASSERT(ins->temp0()->isBogusTemp());
      masm.interleaveHighInt16x8(lhsDest, rhs, output);
      break;
    }
    case SimdShuffleOp::INTERLEAVE_HIGH_32x4: {
      MOZ_ASSERT(ins->temp0()->isBogusTemp());
      masm.interleaveHighInt32x4(lhsDest, rhs, output);
      break;
    }
    case SimdShuffleOp::INTERLEAVE_HIGH_64x2: {
      MOZ_ASSERT(ins->temp0()->isBogusTemp());
      masm.interleaveHighInt64x2(lhsDest, rhs, output);
      break;
    }
    case SimdShuffleOp::INTERLEAVE_LOW_8x16: {
      MOZ_ASSERT(ins->temp0()->isBogusTemp());
      masm.interleaveLowInt8x16(lhsDest, rhs, output);
      break;
    }
    case SimdShuffleOp::INTERLEAVE_LOW_16x8: {
      MOZ_ASSERT(ins->temp0()->isBogusTemp());
      masm.interleaveLowInt16x8(lhsDest, rhs, output);
      break;
    }
    case SimdShuffleOp::INTERLEAVE_LOW_32x4: {
      MOZ_ASSERT(ins->temp0()->isBogusTemp());
      masm.interleaveLowInt32x4(lhsDest, rhs, output);
      break;
    }
    case SimdShuffleOp::INTERLEAVE_LOW_64x2: {
      MOZ_ASSERT(ins->temp0()->isBogusTemp());
      masm.interleaveLowInt64x2(lhsDest, rhs, output);
      break;
    }
    case SimdShuffleOp::SHUFFLE_BLEND_8x16: {
      masm.shuffleInt8x16(reinterpret_cast<const uint8_t*>(control.asInt8x16()),
                          lhsDest, rhs, output);
      break;
    }
    default: {
      MOZ_CRASH("Unsupported SIMD shuffle operation");
    }
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

#ifdef ENABLE_JIT_SIMD

enum PermuteX64I16x8Action : uint16_t {
  UNAVAILABLE = 0,
  SWAP_QWORDS = 1,  // Swap qwords first
  PERM_LOW = 2,     // Permute low qword by control_[0..3]
  PERM_HIGH = 4     // Permute high qword by control_[4..7]
};

// Skip lanes that equal v starting at i, returning the index just beyond the
// last of those.  There is no requirement that the initial lanes[i] == v.
template <typename T>
static int ScanConstant(const T* lanes, int v, int i) {
  int len = int(16 / sizeof(T));
  MOZ_ASSERT(i <= len);
  while (i < len && lanes[i] == v) {
    i++;
  }
  return i;
}

// Apply a transformation to each lane value.
template <typename T>
static void MapLanes(T* result, const T* input, int (*f)(int)) {
  // Hazard analysis trips on "IndirectCall: f" error.
  // Suppress the check -- `f` is expected to be trivial here.
  JS::AutoSuppressGCAnalysis nogc;

  int len = int(16 / sizeof(T));
  for (int i = 0; i < len; i++) {
    result[i] = f(input[i]);
  }
}

// Recognize part of an identity permutation starting at start, with
// the first value of the permutation expected to be bias.
template <typename T>
static bool IsIdentity(const T* lanes, int start, int len, int bias) {
  if (lanes[start] != bias) {
    return false;
  }
  for (int i = start + 1; i < start + len; i++) {
    if (lanes[i] != lanes[i - 1] + 1) {
      return false;
    }
  }
  return true;
}

// We can permute by words if the mask is reducible to a word mask, but the x64
// lowering is only efficient if we can permute the high and low quadwords
// separately, possibly after swapping quadwords.
static PermuteX64I16x8Action CalculateX64Permute16x8(SimdConstant* control) {
  const SimdConstant::I16x8& lanes = control->asInt16x8();
  SimdConstant::I16x8 mapped;
  MapLanes(mapped, lanes, [](int x) -> int { return x < 4 ? 0 : 1; });
  int i = ScanConstant(mapped, mapped[0], 0);
  if (i != 4) {
    return PermuteX64I16x8Action::UNAVAILABLE;
  }
  i = ScanConstant(mapped, mapped[4], 4);
  if (i != 8) {
    return PermuteX64I16x8Action::UNAVAILABLE;
  }
  // Now compute the operation bits.  `mapped` holds the adjusted lane mask.
  memcpy(mapped, lanes, sizeof(mapped));
  uint16_t op = 0;
  if (mapped[0] > mapped[4]) {
    op |= PermuteX64I16x8Action::SWAP_QWORDS;
  }
  for (auto& m : mapped) {
    m &= 3;
  }
  if (!IsIdentity(mapped, 0, 4, 0)) {
    op |= PermuteX64I16x8Action::PERM_LOW;
  }
  if (!IsIdentity(mapped, 4, 4, 0)) {
    op |= PermuteX64I16x8Action::PERM_HIGH;
  }
  MOZ_ASSERT(op != PermuteX64I16x8Action::UNAVAILABLE);
  *control = SimdConstant::CreateX8(mapped);
  return (PermuteX64I16x8Action)op;
}

#endif

void CodeGenerator::visitWasmPermuteSimd128(LWasmPermuteSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  FloatRegister src = ToFloatRegister(ins->src());
  FloatRegister dest = ToFloatRegister(ins->output());
  SimdConstant control = ins->control();
  switch (ins->op()) {
    // For broadcast, would MOVDDUP be better than PSHUFD for the last step?
    case SimdPermuteOp::BROADCAST_8x16: {
      const SimdConstant::I8x16& mask = control.asInt8x16();
      int8_t source = mask[0];
      if (source == 0 && Assembler::HasAVX2()) {
        masm.vbroadcastb(Operand(src), dest);
        break;
      }
      MOZ_ASSERT_IF(!Assembler::HasAVX(), src == dest);
      if (source < 8) {
        masm.interleaveLowInt8x16(src, src, dest);
      } else {
        masm.interleaveHighInt8x16(src, src, dest);
        source -= 8;
      }
      uint16_t v = uint16_t(source & 3);
      uint16_t wordMask[4] = {v, v, v, v};
      if (source < 4) {
        masm.permuteLowInt16x8(wordMask, dest, dest);
        uint32_t dwordMask[4] = {0, 0, 0, 0};
        masm.permuteInt32x4(dwordMask, dest, dest);
      } else {
        masm.permuteHighInt16x8(wordMask, dest, dest);
        uint32_t dwordMask[4] = {2, 2, 2, 2};
        masm.permuteInt32x4(dwordMask, dest, dest);
      }
      break;
    }
    case SimdPermuteOp::BROADCAST_16x8: {
      const SimdConstant::I16x8& mask = control.asInt16x8();
      int16_t source = mask[0];
      if (source == 0 && Assembler::HasAVX2()) {
        masm.vbroadcastw(Operand(src), dest);
        break;
      }
      uint16_t v = uint16_t(source & 3);
      uint16_t wordMask[4] = {v, v, v, v};
      if (source < 4) {
        masm.permuteLowInt16x8(wordMask, src, dest);
        uint32_t dwordMask[4] = {0, 0, 0, 0};
        masm.permuteInt32x4(dwordMask, dest, dest);
      } else {
        masm.permuteHighInt16x8(wordMask, src, dest);
        uint32_t dwordMask[4] = {2, 2, 2, 2};
        masm.permuteInt32x4(dwordMask, dest, dest);
      }
      break;
    }
    case SimdPermuteOp::MOVE: {
      masm.moveSimd128(src, dest);
      break;
    }
    case SimdPermuteOp::PERMUTE_8x16: {
      const SimdConstant::I8x16& mask = control.asInt8x16();
#  ifdef DEBUG
      DebugOnly<int> i;
      for (i = 0; i < 16 && mask[i] == i; i++) {
      }
      MOZ_ASSERT(i < 16, "Should have been a MOVE operation");
#  endif
      masm.permuteInt8x16(reinterpret_cast<const uint8_t*>(mask), src, dest);
      break;
    }
    case SimdPermuteOp::PERMUTE_16x8: {
#  ifdef DEBUG
      const SimdConstant::I16x8& mask = control.asInt16x8();
      DebugOnly<int> i;
      for (i = 0; i < 8 && mask[i] == i; i++) {
      }
      MOZ_ASSERT(i < 8, "Should have been a MOVE operation");
#  endif
      PermuteX64I16x8Action op = CalculateX64Permute16x8(&control);
      if (op != PermuteX64I16x8Action::UNAVAILABLE) {
        const SimdConstant::I16x8& mask = control.asInt16x8();
        if (op & PermuteX64I16x8Action::SWAP_QWORDS) {
          uint32_t dwordMask[4] = {2, 3, 0, 1};
          masm.permuteInt32x4(dwordMask, src, dest);
          src = dest;
        }
        if (op & PermuteX64I16x8Action::PERM_LOW) {
          masm.permuteLowInt16x8(reinterpret_cast<const uint16_t*>(mask) + 0,
                                 src, dest);
          src = dest;
        }
        if (op & PermuteX64I16x8Action::PERM_HIGH) {
          masm.permuteHighInt16x8(reinterpret_cast<const uint16_t*>(mask) + 4,
                                  src, dest);
          src = dest;
        }
      } else {
        const SimdConstant::I16x8& wmask = control.asInt16x8();
        uint8_t mask[16];
        for (unsigned i = 0; i < 16; i += 2) {
          mask[i] = wmask[i / 2] * 2;
          mask[i + 1] = wmask[i / 2] * 2 + 1;
        }
        masm.permuteInt8x16(mask, src, dest);
      }
      break;
    }
    case SimdPermuteOp::PERMUTE_32x4: {
      const SimdConstant::I32x4& mask = control.asInt32x4();
      if (Assembler::HasAVX2() && mask[0] == 0 && mask[1] == 0 &&
          mask[2] == 0 && mask[3] == 0) {
        masm.vbroadcastd(Operand(src), dest);
        break;
      }
#  ifdef DEBUG
      DebugOnly<int> i;
      for (i = 0; i < 4 && mask[i] == i; i++) {
      }
      MOZ_ASSERT(i < 4, "Should have been a MOVE operation");
#  endif
      masm.permuteInt32x4(reinterpret_cast<const uint32_t*>(mask), src, dest);
      break;
    }
    case SimdPermuteOp::ROTATE_RIGHT_8x16: {
      MOZ_ASSERT_IF(!Assembler::HasAVX(), src == dest);
      int8_t count = control.asInt8x16()[0];
      MOZ_ASSERT(count > 0, "Should have been a MOVE operation");
      masm.concatAndRightShiftSimd128(src, src, dest, count);
      break;
    }
    case SimdPermuteOp::SHIFT_LEFT_8x16: {
      int8_t count = control.asInt8x16()[0];
      MOZ_ASSERT(count > 0, "Should have been a MOVE operation");
      masm.leftShiftSimd128(Imm32(count), src, dest);
      break;
    }
    case SimdPermuteOp::SHIFT_RIGHT_8x16: {
      int8_t count = control.asInt8x16()[0];
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
    default: {
      MOZ_CRASH("Unsupported SIMD permutation operation");
    }
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

void CodeGenerator::visitWasmReplaceLaneSimd128(LWasmReplaceLaneSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  FloatRegister lhs = ToFloatRegister(ins->lhs());
  FloatRegister dest = ToFloatRegister(ins->output());
  const LAllocation* rhs = ins->rhs();
  uint32_t laneIndex = ins->mir()->laneIndex();

  switch (ins->mir()->simdOp()) {
    case wasm::SimdOp::I8x16ReplaceLane:
      masm.replaceLaneInt8x16(laneIndex, lhs, ToRegister(rhs), dest);
      break;
    case wasm::SimdOp::I16x8ReplaceLane:
      masm.replaceLaneInt16x8(laneIndex, lhs, ToRegister(rhs), dest);
      break;
    case wasm::SimdOp::I32x4ReplaceLane:
      masm.replaceLaneInt32x4(laneIndex, lhs, ToRegister(rhs), dest);
      break;
    case wasm::SimdOp::F32x4ReplaceLane:
      masm.replaceLaneFloat32x4(laneIndex, lhs, ToFloatRegister(rhs), dest);
      break;
    case wasm::SimdOp::F64x2ReplaceLane:
      masm.replaceLaneFloat64x2(laneIndex, lhs, ToFloatRegister(rhs), dest);
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
  FloatRegister lhs = ToFloatRegister(ins->lhs());
  FloatRegister rhs = ToFloatRegister(ins->rhs());
  FloatRegister dest = ToFloatRegister(ins->output());
  uint32_t laneIndex = ins->mir()->laneIndex();
  uint32_t srcLaneIndex = ins->mir()->srcLaneIndex();

  switch (ins->mir()->simdOp()) {
    case wasm::SimdOp::I16x8ReplaceLane: {
      if (srcLaneIndex == laneIndex) {
        masm.vpblendw(1 << laneIndex, rhs, lhs, dest);
        break;
      }
      // Broadcast the dword holding the source lane, then shift the lane into
      // the other half of each dword if the halves differ.
      ScratchSimd128Scope scratch(masm);
      masm.vpshufd((srcLaneIndex / 2) * 0x55, rhs, scratch);
      if ((laneIndex & 1) > (srcLaneIndex & 1)) {
        masm.vpslld(Imm32(16), scratch, scratch);
      } else if ((laneIndex & 1) < (srcLaneIndex & 1)) {
        masm.vpsrld(Imm32(16), scratch, scratch);
      }
      masm.vpblendw(1 << laneIndex, scratch, lhs, dest);
      break;
    }
    case wasm::SimdOp::I32x4ReplaceLane:
      masm.vinsertps(masm.vinsertpsMask(srcLaneIndex, laneIndex), rhs, lhs,
                     dest);
      break;
    case wasm::SimdOp::I64x2ReplaceLane:
      if (srcLaneIndex == laneIndex) {
        masm.vpblendw(laneIndex ? 0xF0 : 0x0F, rhs, lhs, dest);
      } else if (laneIndex) {
        masm.vmovlhps(rhs, lhs, dest);
      } else {
        masm.vmovhlps(rhs, lhs, dest);
      }
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
  masm.replaceLaneInt64x2(ins->mir()->laneIndex(), ToFloatRegister(ins->lhs()),
                          ToRegister64(ins->rhs()),
                          ToFloatRegister(ins->output()));
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
      masm.truncSatFloat32x4ToInt32x4(src, dest);
      break;
    case wasm::SimdOp::I32x4TruncSatF32x4U:
      masm.unsignedTruncSatFloat32x4ToInt32x4(src, dest,
                                              ToFloatRegister(ins->temp0()));
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
    case wasm::SimdOp::I8x16Popcnt:
      masm.popcntInt8x16(src, dest, ToFloatRegister(ins->temp0()));
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
  uint32_t imm = ins->mir()->imm();

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
      // Set the zero flag if all of the lanes are zero, and branch on that.
      masm.vptest(src, src);
      emitBranch(Assembler::NotEqual, ins->ifTrue(), ins->ifFalse());
      break;
    case wasm::SimdOp::I8x16AllTrue:
    case wasm::SimdOp::I16x8AllTrue:
    case wasm::SimdOp::I32x4AllTrue:
    case wasm::SimdOp::I64x2AllTrue: {
      // Compare all lanes to zero, set the zero flag if none of the lanes are
      // zero, and branch on that.
      ScratchSimd128Scope tmp(masm);
      masm.vxorps(tmp, tmp, tmp);
      switch (ins->simdOp()) {
        case wasm::SimdOp::I8x16AllTrue:
          masm.vpcmpeqb(Operand(src), tmp, tmp);
          break;
        case wasm::SimdOp::I16x8AllTrue:
          masm.vpcmpeqw(Operand(src), tmp, tmp);
          break;
        case wasm::SimdOp::I32x4AllTrue:
          masm.vpcmpeqd(Operand(src), tmp, tmp);
          break;
        case wasm::SimdOp::I64x2AllTrue:
          masm.vpcmpeqq(Operand(src), tmp, tmp);
          break;
        default:
          MOZ_CRASH();
      }
      masm.vptest(tmp, tmp);
      emitBranch(Assembler::Equal, ins->ifTrue(), ins->ifFalse());
      break;
    }
    case wasm::SimdOp::I16x8Bitmask: {
      masm.bitwiseTestSimd128(SimdConstant::SplatX8(0x8000), src);
      emitBranch(Assembler::NotEqual, ins->ifTrue(), ins->ifFalse());
      break;
    }
    default:
      MOZ_CRASH("Reduce-and-branch SimdOp not implemented");
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
  uint32_t imm = ins->mir()->imm();

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

void CodeGenerator::visitWasmLoadLaneSimd128(LWasmLoadLaneSimd128* ins) {
#ifdef ENABLE_JIT_SIMD
  const MWasmLoadLaneSimd128* mir = ins->mir();
  const wasm::MemoryAccessDesc& access = mir->access();

  access.assertOffsetInGuardPages();
  uint32_t offset = access.offset32();

  const LAllocation* value = ins->src();
  Operand srcAddr = toMemoryAccessOperand(ins, offset);

  switch (mir->laneSize()) {
    case 1: {
      auto before = masm.currentOffset();
      masm.vpinsrb(mir->laneIndex(), srcAddr, ToFloatRegister(value),
                   ToFloatRegister(value));
      auto after = masm.currentOffset();
      masm.appendAndVerify(access, wasm::TrapMachineInsn::Load8,
                           FaultingCodeRange(before, after));
      break;
    }
    case 2: {
      auto before = masm.currentOffset();
      masm.vpinsrw(mir->laneIndex(), srcAddr, ToFloatRegister(value),
                   ToFloatRegister(value));
      auto after = masm.currentOffset();
      masm.appendAndVerify(access, wasm::TrapMachineInsn::Load16,
                           FaultingCodeRange(before, after));
      break;
    }
    case 4: {
      auto before = masm.currentOffset();
      masm.vinsertps(mir->laneIndex() << 4, srcAddr, ToFloatRegister(value),
                     ToFloatRegister(value));
      auto after = masm.currentOffset();
      masm.appendAndVerify(access, wasm::TrapMachineInsn::Load32,
                           FaultingCodeRange(before, after));
      break;
    }
    case 8: {
      auto before = masm.currentOffset();
      if (mir->laneIndex() == 0) {
        masm.vmovlps(srcAddr, ToFloatRegister(value), ToFloatRegister(value));
      } else {
        masm.vmovhps(srcAddr, ToFloatRegister(value), ToFloatRegister(value));
      }
      auto after = masm.currentOffset();
      masm.appendAndVerify(access, wasm::TrapMachineInsn::Load64,
                           FaultingCodeRange(before, after));
      break;
    }
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
  const wasm::MemoryAccessDesc& access = mir->access();

  access.assertOffsetInGuardPages();
  uint32_t offset = access.offset32();

  const LAllocation* src = ins->src();
  Operand destAddr = toMemoryAccessOperand(ins, offset);

  switch (mir->laneSize()) {
    case 1: {
      auto before = masm.currentOffset();
      masm.vpextrb(mir->laneIndex(), ToFloatRegister(src), destAddr);
      auto after = masm.currentOffset();
      masm.appendAndVerify(access, wasm::TrapMachineInsn::Store8,
                           FaultingCodeRange(before, after));
      break;
    }
    case 2: {
      auto before = masm.currentOffset();
      masm.vpextrw(mir->laneIndex(), ToFloatRegister(src), destAddr);
      auto after = masm.currentOffset();
      masm.appendAndVerify(access, wasm::TrapMachineInsn::Store16,
                           FaultingCodeRange(before, after));
      break;
    }
    case 4: {
      unsigned lane = mir->laneIndex();
      auto before = masm.currentOffset();
      if (lane == 0) {
        masm.vmovss(ToFloatRegister(src), destAddr);
      } else {
        masm.vextractps(lane, ToFloatRegister(src), destAddr);
      }
      auto after = masm.currentOffset();
      masm.appendAndVerify(access, wasm::TrapMachineInsn::Store32,
                           FaultingCodeRange(before, after));
      break;
    }
    case 8: {
      auto before = masm.currentOffset();
      if (mir->laneIndex() == 0) {
        masm.vmovlps(ToFloatRegister(src), destAddr);
      } else {
        masm.vmovhps(ToFloatRegister(src), destAddr);
      }
      auto after = masm.currentOffset();
      masm.appendAndVerify(access, wasm::TrapMachineInsn::Store64,
                           FaultingCodeRange(before, after));
      break;
    }
    default:
      MOZ_CRASH("Unsupported store lane size");
  }
#else
  MOZ_CRASH("No SIMD");
#endif
}

}  // namespace jit
}  // namespace js
