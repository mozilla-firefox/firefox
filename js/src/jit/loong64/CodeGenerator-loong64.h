/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_loong64_CodeGenerator_loong64_h
#define jit_loong64_CodeGenerator_loong64_h

#include "jit/loong64/Assembler-loong64.h"
#include "jit/shared/CodeGenerator-shared.h"

namespace js {
namespace jit {

class CodeGeneratorLOONG64;
class OutOfLineTableSwitch;

using OutOfLineWasmTruncateCheck =
    OutOfLineWasmTruncateCheckBase<CodeGeneratorLOONG64>;

class CodeGeneratorLOONG64 : public CodeGeneratorShared {
  friend class MoveResolverLA;

 protected:
  CodeGeneratorLOONG64(MIRGenerator* gen, LIRGraph* graph, MacroAssembler* masm,
                       const wasm::CodeMetadata* wasmCodeMeta);

  Operand ToOperand(const LAllocation& a);
  Operand ToOperand(const LAllocation* a);
  Operand ToOperand(const LDefinition* def);

  MoveOperand toMoveOperand(LAllocation a) const;

  void bailoutIfFalseBool(Register reg, LSnapshot* snapshot) {
    Label bail;
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.Acquire();
    masm.ma_and(scratch, reg, Imm32(0xFF));
    masm.ma_b(scratch, scratch, &bail, Assembler::Zero);
    bailoutFrom(&bail, snapshot);
  }

  bool generateOutOfLineCode();

  template <typename T>
  void branchToBlock(Register lhs, T rhs, MBasicBlock* mir,
                     Assembler::Condition cond) {
    masm.ma_b(lhs, rhs, skipTrivialBlocks(mir)->lir()->label(), cond);
  }
  void branchToBlock(Assembler::FloatFormat fmt, FloatRegister lhs,
                     FloatRegister rhs, MBasicBlock* mir,
                     Assembler::DoubleCondition cond);

  // Emits a branch that directs control flow to the true block if |cond| is
  // true, and the false block if |cond| is false.
  template <typename T>
  void emitBranch(Register lhs, T rhs, Assembler::Condition cond,
                  MBasicBlock* mirTrue, MBasicBlock* mirFalse) {
    if (isNextBlock(mirFalse->lir())) {
      branchToBlock(lhs, rhs, mirTrue, cond);
    } else {
      branchToBlock(lhs, rhs, mirFalse, Assembler::InvertCondition(cond));
      jumpToBlock(mirTrue);
    }
  }

  void emitTableSwitchDispatch(MTableSwitch* mir, Register index,
                               Register base);

  template <typename T>
  void emitWasmLoad(T* ins);
  template <typename T>
  void emitWasmStore(T* ins);

  void generateInvalidateEpilogue();

  // Generating a result.
  template <typename S, typename T>
  void atomicBinopToTypedIntArray(AtomicOp op, Scalar::Type arrayType,
                                  const S& value, const T& mem,
                                  Register flagTemp, Register outTemp,
                                  Register valueTemp, Register offsetTemp,
                                  Register maskTemp, AnyRegister output);

  // Generating no result.
  template <typename S, typename T>
  void atomicBinopToTypedIntArray(AtomicOp op, Scalar::Type arrayType,
                                  const S& value, const T& mem,
                                  Register flagTemp, Register valueTemp,
                                  Register offsetTemp, Register maskTemp);

 public:
  // Out of line visitors.
  void visitOutOfLineTableSwitch(OutOfLineTableSwitch* ool);
  void visitOutOfLineWasmTruncateCheck(OutOfLineWasmTruncateCheck* ool);

 protected:
  void emitBigIntPtrDiv(LBigIntPtrDiv* ins, Register dividend, Register divisor,
                        Register output);
  void emitBigIntPtrMod(LBigIntPtrMod* ins, Register dividend, Register divisor,
                        Register output);

  template <typename T>
  void emitWasmLoadI64(T* ins);
  template <typename T>
  void emitWasmStoreI64(T* ins);
};

typedef CodeGeneratorLOONG64 CodeGeneratorSpecific;

}  // namespace jit
}  // namespace js

#endif /* jit_loong64_CodeGenerator_loong64_h */
