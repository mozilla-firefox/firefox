/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_mips64_MacroAssembler_mips64_inl_h
#define jit_mips64_MacroAssembler_mips64_inl_h

#include "jit/mips64/MacroAssembler-mips64.h"

#include "vm/BigIntType.h"  // JS::BigInt

#include "jit/mips-shared/MacroAssembler-mips-shared-inl.h"

namespace js {
namespace jit {

//{{{ check_macroassembler_style

void MacroAssembler::move64(Register64 src, Register64 dest) {
  movePtr(src.reg, dest.reg);
}

void MacroAssembler::move64(Imm64 imm, Register64 dest) {
  movePtr(ImmWord(imm.value), dest.reg);
}

void MacroAssembler::moveDoubleToGPR64(FloatRegister src, Register64 dest) {
  moveFromDouble(src, dest.reg);
}

void MacroAssembler::moveGPR64ToDouble(Register64 src, FloatRegister dest) {
  moveToDouble(src.reg, dest);
}

void MacroAssembler::move64To32(Register64 src, Register dest) {
  ma_sll(dest, src.reg, Imm32(0));
}

void MacroAssembler::move32To64ZeroExtend(Register src, Register64 dest) {
  ma_dext(dest.reg, src, Imm32(0), Imm32(32));
}

void MacroAssembler::move8To64SignExtend(Register src, Register64 dest) {
  move32To64SignExtend(src, dest);
  move8SignExtend(dest.reg, dest.reg);
}

void MacroAssembler::move16To64SignExtend(Register src, Register64 dest) {
  move32To64SignExtend(src, dest);
  move16SignExtend(dest.reg, dest.reg);
}

void MacroAssembler::move32To64SignExtend(Register src, Register64 dest) {
  ma_sll(dest.reg, src, Imm32(0));
}

void MacroAssembler::move8SignExtendToPtr(Register src, Register dest) {
  move8To64SignExtend(src, Register64(dest));
}

void MacroAssembler::move16SignExtendToPtr(Register src, Register dest) {
  move16To64SignExtend(src, Register64(dest));
}

void MacroAssembler::move32SignExtendToPtr(Register src, Register dest) {
  ma_sll(dest, src, Imm32(0));
}

void MacroAssembler::move32ZeroExtendToPtr(Register src, Register dest) {
  ma_dext(dest, src, Imm32(0), Imm32(32));
}

// ===============================================================
// Load instructions

void MacroAssembler::load32SignExtendToPtr(const Address& src, Register dest) {
  load32(src, dest);
}

// ===============================================================
// Logical instructions

void MacroAssembler::notPtr(Register reg) { ma_not(reg, reg); }

void MacroAssembler::andPtr(Register src, Register dest) { ma_and(dest, src); }

void MacroAssembler::andPtr(Imm32 imm, Register dest) { ma_and(dest, imm); }

void MacroAssembler::andPtr(Imm32 imm, Register src, Register dest) {
  ma_and(dest, src, imm);
}

void MacroAssembler::andPtr(Imm32 imm, const Address& dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  loadPtr(dest, scratch2);
  ma_and(scratch2, imm);
  storePtr(scratch2, dest);
}

void MacroAssembler::and64(Imm64 imm, Register64 dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  ma_li(scratch, ImmWord(imm.value));
  ma_and(dest.reg, scratch);
}

void MacroAssembler::and64(Register64 src, Register64 dest) {
  ma_and(dest.reg, src.reg);
}

void MacroAssembler::andNot64(Register64 lhs, Register64 rhs, Register64 dest) {
  MOZ_CRASH("Not supported for this target");
}

void MacroAssembler::or64(Imm64 imm, Register64 dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  ma_li(scratch, ImmWord(imm.value));
  ma_or(dest.reg, scratch);
}

void MacroAssembler::xor64(Imm64 imm, Register64 dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  ma_li(scratch, ImmWord(imm.value));
  ma_xor(dest.reg, scratch);
}

void MacroAssembler::orPtr(Register src, Register dest) { ma_or(dest, src); }

void MacroAssembler::orPtr(Imm32 imm, Register dest) { ma_or(dest, imm); }

void MacroAssembler::orPtr(Imm32 imm, Register src, Register dest) {
  ma_or(dest, src, imm);
}

void MacroAssembler::or64(Register64 src, Register64 dest) {
  ma_or(dest.reg, src.reg);
}

void MacroAssembler::xor64(Register64 src, Register64 dest) {
  ma_xor(dest.reg, src.reg);
}

void MacroAssembler::xorPtr(Register src, Register dest) { ma_xor(dest, src); }

void MacroAssembler::xorPtr(Imm32 imm, Register dest) { ma_xor(dest, imm); }

void MacroAssembler::xorPtr(Imm32 imm, Register src, Register dest) {
  ma_xor(dest, src, imm);
}

// ===============================================================
// Swap instructions

void MacroAssembler::byteSwap64(Register64 reg64) {
  Register reg = reg64.reg;
  ma_dsbh(reg, reg);
  ma_dshd(reg, reg);
}

// ===============================================================
// Arithmetic functions

void MacroAssembler::addPtr(Register src, Register dest) {
  ma_daddu(dest, src);
}

void MacroAssembler::addPtr(Imm32 imm, Register dest) { ma_daddu(dest, imm); }

void MacroAssembler::addPtr(ImmWord imm, Register dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  movePtr(imm, scratch);
  addPtr(scratch, dest);
}

void MacroAssembler::add64(Register64 src, Register64 dest) {
  addPtr(src.reg, dest.reg);
}

void MacroAssembler::add64(Imm32 imm, Register64 dest) {
  ma_daddu(dest.reg, imm);
}

void MacroAssembler::add64(Imm64 imm, Register64 dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  mov(ImmWord(imm.value), scratch);
  ma_daddu(dest.reg, scratch);
}

CodeOffset MacroAssembler::sub32FromStackPtrWithPatch(Register dest) {
  CodeOffset offset = CodeOffset(currentOffset());
  ma_liPatchable(dest, Imm32(0));
  as_dsubu(dest, StackPointer, dest);
  return offset;
}

void MacroAssembler::patchSub32FromStackPtr(CodeOffset offset, Imm32 imm) {
  Instruction* lui =
      (Instruction*)m_buffer.getInst(BufferOffset(offset.offset()));
  Instruction* ori =
      (Instruction*)m_buffer.getInst(BufferOffset(offset.offset() + 4));

  MOZ_ASSERT(lui->extractOpcode() == ((uint32_t)op_lui >> OpcodeShift));
  MOZ_ASSERT(ori->extractOpcode() == ((uint32_t)op_ori >> OpcodeShift));

  MacroAssemblerMIPSShared::UpdateLuiOriValue(lui, ori, imm.value);
}

void MacroAssembler::subPtr(Register src, Register dest) {
  as_dsubu(dest, dest, src);
}

void MacroAssembler::subPtr(Imm32 imm, Register dest) {
  ma_dsubu(dest, dest, imm);
}

void MacroAssembler::sub64(Register64 src, Register64 dest) {
  as_dsubu(dest.reg, dest.reg, src.reg);
}

void MacroAssembler::sub64(Imm64 imm, Register64 dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  mov(ImmWord(imm.value), scratch);
  as_dsubu(dest.reg, dest.reg, scratch);
}

void MacroAssembler::mulHighUnsigned32(Imm32 imm, Register src, Register dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(src != scratch);
  move32(imm, scratch);
#ifdef MIPSR6
  as_muhu(dest, src, scratch);
#else
  as_multu(src, scratch);
  as_mfhi(dest);
#endif
}

void MacroAssembler::mulPtr(Register rhs, Register srcDest) {
#ifdef MIPSR6
  as_dmulu(srcDest, srcDest, rhs);
#else
  as_dmultu(srcDest, rhs);
  as_mflo(srcDest);
#endif
}

void MacroAssembler::mulPtr(ImmWord rhs, Register srcDest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(srcDest != scratch);
  mov(rhs, scratch);
  mulPtr(scratch, srcDest);
}

void MacroAssembler::mul64(const Register64& rhs, const Register64& srcDest) {
  mul64(rhs, srcDest, InvalidReg);
}

void MacroAssembler::mul64(Imm64 imm, const Register64& dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  mov(ImmWord(imm.value), scratch);
#ifdef MIPSR6
  as_dmulu(dest.reg, scratch, dest.reg);
#else
  as_dmultu(dest.reg, scratch);
  as_mflo(dest.reg);
#endif
}

void MacroAssembler::mul64(Imm64 imm, const Register64& dest,
                           const Register temp) {
  MOZ_ASSERT(temp == InvalidReg);
  mul64(imm, dest);
}

void MacroAssembler::mul64(const Register64& src, const Register64& dest,
                           const Register temp) {
  MOZ_ASSERT(temp == InvalidReg);
#ifdef MIPSR6
  as_dmulu(dest.reg, src.reg, dest.reg);
#else
  as_dmultu(dest.reg, src.reg);
  as_mflo(dest.reg);
#endif
}

void MacroAssembler::mulBy3(Register src, Register dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(src != scratch);
  as_daddu(scratch, src, src);
  as_daddu(dest, scratch, src);
}

void MacroAssembler::inc64(AbsoluteAddress dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
  ma_li(scratch, ImmWord(uintptr_t(dest.addr)));
  as_ld(scratch2, scratch, 0);
  as_daddiu(scratch2, scratch2, 1);
  as_sd(scratch2, scratch, 0);
}

void MacroAssembler::quotient64(Register lhs, Register rhs, Register dest,
                                bool isUnsigned) {
#ifdef MIPSR6
  if (isUnsigned) {
    as_ddivu(dest, lhs, rhs);
  } else {
    as_ddiv(dest, lhs, rhs);
  }
#else
  if (isUnsigned) {
    as_ddivu(lhs, rhs);
  } else {
    as_ddiv(lhs, rhs);
  }
  as_mflo(dest);
#endif
}

void MacroAssembler::remainder64(Register lhs, Register rhs, Register dest,
                                 bool isUnsigned) {
#ifdef MIPSR6
  if (isUnsigned) {
    as_dmodu(dest, lhs, rhs);
  } else {
    as_dmod(dest, lhs, rhs);
  }
#else
  if (isUnsigned) {
    as_ddivu(lhs, rhs);
  } else {
    as_ddiv(lhs, rhs);
  }
  as_mfhi(dest);
#endif
}

void MacroAssembler::neg64(Register64 reg) { as_dsubu(reg.reg, zero, reg.reg); }

void MacroAssembler::negPtr(Register reg) { as_dsubu(reg, zero, reg); }

// ===============================================================
// Shift functions

void MacroAssembler::lshiftPtr(Imm32 imm, Register dest) {
  lshiftPtr(imm, dest, dest);
}

void MacroAssembler::lshiftPtr(Imm32 imm, Register src, Register dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  ma_dsll(dest, src, imm);
}

void MacroAssembler::lshiftPtr(Register shift, Register dest) {
  ma_dsll(dest, dest, shift);
}

void MacroAssembler::lshift64(Imm32 imm, Register64 dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  ma_dsll(dest.reg, dest.reg, imm);
}

void MacroAssembler::lshift64(Imm32 imm, Register64 src, Register64 dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  ma_dsll(dest.reg, src.reg, imm);
}

void MacroAssembler::lshift64(Register shift, Register64 dest) {
  ma_dsll(dest.reg, dest.reg, shift);
}

void MacroAssembler::rshiftPtr(Imm32 imm, Register dest) {
  rshiftPtr(imm, dest, dest);
}

void MacroAssembler::rshiftPtr(Imm32 imm, Register src, Register dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  ma_dsrl(dest, src, imm);
}

void MacroAssembler::rshiftPtr(Register shift, Register dest) {
  ma_dsrl(dest, dest, shift);
}

void MacroAssembler::rshift64(Imm32 imm, Register64 dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  ma_dsrl(dest.reg, dest.reg, imm);
}

void MacroAssembler::rshift64(Imm32 imm, Register64 src, Register64 dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  ma_dsrl(dest.reg, src.reg, imm);
}

void MacroAssembler::rshift64(Register shift, Register64 dest) {
  ma_dsrl(dest.reg, dest.reg, shift);
}

void MacroAssembler::rshiftPtrArithmetic(Imm32 imm, Register dest) {
  rshiftPtrArithmetic(imm, dest, dest);
}

void MacroAssembler::rshiftPtrArithmetic(Imm32 imm, Register src,
                                         Register dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  ma_dsra(dest, src, imm);
}

void MacroAssembler::rshiftPtrArithmetic(Register shift, Register dest) {
  ma_dsra(dest, dest, shift);
}

void MacroAssembler::rshift64Arithmetic(Imm32 imm, Register64 dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  ma_dsra(dest.reg, dest.reg, imm);
}

void MacroAssembler::rshift64Arithmetic(Imm32 imm, Register64 src,
                                        Register64 dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  ma_dsra(dest.reg, src.reg, imm);
}

void MacroAssembler::rshift64Arithmetic(Register shift, Register64 dest) {
  ma_dsra(dest.reg, dest.reg, shift);
}

// ===============================================================
// Rotation functions

void MacroAssembler::rotateLeft64(Imm32 count, Register64 src, Register64 dest,
                                  Register temp) {
  MOZ_ASSERT(temp == InvalidReg);

  if (count.value) {
    ma_drol(dest.reg, src.reg, count);
  } else {
    ma_move(dest.reg, src.reg);
  }
}

void MacroAssembler::rotateLeft64(Register count, Register64 src,
                                  Register64 dest, Register temp) {
  MOZ_ASSERT(temp == InvalidReg);
  ma_drol(dest.reg, src.reg, count);
}

void MacroAssembler::rotateRight64(Imm32 count, Register64 src, Register64 dest,
                                   Register temp) {
  MOZ_ASSERT(temp == InvalidReg);

  if (count.value) {
    ma_dror(dest.reg, src.reg, count);
  } else {
    ma_move(dest.reg, src.reg);
  }
}

void MacroAssembler::rotateRight64(Register count, Register64 src,
                                   Register64 dest, Register temp) {
  MOZ_ASSERT(temp == InvalidReg);
  ma_dror(dest.reg, src.reg, count);
}

// ===============================================================
// Shift or rotate, then combine with another register

void MacroAssembler::lshift32ThenAdd(Imm32 shift, Register rhs,
                                     Register srcDest) {
  MOZ_ASSERT(rhs != srcDest);
  lshift32(shift, srcDest);
  add32(rhs, srcDest);
}

void MacroAssembler::lshift32ThenOr(Imm32 shift, Register rhs,
                                    Register srcDest) {
  MOZ_ASSERT(rhs != srcDest);
  lshift32(shift, srcDest);
  or32(rhs, srcDest);
}

void MacroAssembler::rshiftPtrThenXor(Imm32 shift, Register rhs,
                                      Register srcDest) {
  MOZ_ASSERT(rhs != srcDest);
  rshiftPtr(shift, srcDest);
  xorPtr(rhs, srcDest);
}

void MacroAssembler::rotateLeft64ThenXor(Imm32 count, Register64 rhs,
                                         Register64 srcDest) {
  MOZ_ASSERT(rhs != srcDest);
  rotateLeft64(count, srcDest, srcDest, InvalidReg);
  xor64(rhs, srcDest);
}

// ===============================================================
// Condition functions

template <typename T1, typename T2>
void MacroAssembler::cmpPtrSet(Condition cond, T1 lhs, T2 rhs, Register dest) {
  ma_cmp_set(dest, lhs, rhs, cond);
}

// Also see below for specializations of cmpPtrSet.

template <typename T1, typename T2>
void MacroAssembler::cmp32Set(Condition cond, T1 lhs, T2 rhs, Register dest) {
  ma_cmp_set(dest, lhs, rhs, cond);
}

void MacroAssembler::cmp64Set(Condition cond, Register64 lhs, Register64 rhs,
                              Register dest) {
  ma_cmp_set(dest, lhs.reg, rhs.reg, cond);
}

void MacroAssembler::cmp64Set(Condition cond, Register64 lhs, Imm64 rhs,
                              Register dest) {
  ma_cmp_set(dest, lhs.reg, ImmWord(uint64_t(rhs.value)), cond);
}

void MacroAssembler::cmp64Set(Condition cond, Address lhs, Register64 rhs,
                              Register dest) {
  ma_cmp_set(dest, lhs, rhs.reg, cond);
}

void MacroAssembler::cmp64Set(Condition cond, Address lhs, Imm64 rhs,
                              Register dest) {
  ma_cmp_set(dest, lhs, ImmWord(uint64_t(rhs.value)), cond);
}

// ===============================================================
// Bit counting functions

void MacroAssembler::clz64(Register64 src, Register64 dest) {
  as_dclz(dest.reg, src.reg);
}

void MacroAssembler::ctz64(Register64 src, Register64 dest) {
  ma_dctz(dest.reg, src.reg);
}

void MacroAssembler::popcnt64(Register64 input, Register64 output,
                              Register tmp) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  ma_move(output.reg, input.reg);
  ma_dsra(tmp, input.reg, Imm32(1));
  ma_li(scratch, ImmWord(0x5555555555555555UL));
  ma_and(tmp, scratch);
  ma_dsubu(output.reg, tmp);
  ma_dsra(tmp, output.reg, Imm32(2));
  ma_li(scratch, ImmWord(0x3333333333333333UL));
  ma_and(output.reg, scratch);
  ma_and(tmp, scratch);
  ma_daddu(output.reg, tmp);
  ma_dsrl(tmp, output.reg, Imm32(4));
  ma_daddu(output.reg, tmp);
  ma_li(scratch, ImmWord(0xF0F0F0F0F0F0F0FUL));
  ma_and(output.reg, scratch);
  ma_dsll(tmp, output.reg, Imm32(8));
  ma_daddu(output.reg, tmp);
  ma_dsll(tmp, output.reg, Imm32(16));
  ma_daddu(output.reg, tmp);
  ma_dsll(tmp, output.reg, Imm32(32));
  ma_daddu(output.reg, tmp);
  ma_dsra(output.reg, output.reg, Imm32(56));
}

// ===============================================================
// Branch functions

void MacroAssembler::branch64(Condition cond, Register64 lhs, Imm64 val,
                              Label* success, Label* fail) {
  MOZ_ASSERT(cond == Assembler::NotEqual || cond == Assembler::Equal ||
                 cond == Assembler::LessThan ||
                 cond == Assembler::LessThanOrEqual ||
                 cond == Assembler::GreaterThan ||
                 cond == Assembler::GreaterThanOrEqual ||
                 cond == Assembler::Below || cond == Assembler::BelowOrEqual ||
                 cond == Assembler::Above || cond == Assembler::AboveOrEqual,
             "other condition codes not supported");

  branchPtr(cond, lhs.reg, ImmWord(val.value), success);
  if (fail) {
    jump(fail);
  }
}

void MacroAssembler::branch64(Condition cond, Register64 lhs, Register64 rhs,
                              Label* success, Label* fail) {
  MOZ_ASSERT(cond == Assembler::NotEqual || cond == Assembler::Equal ||
                 cond == Assembler::LessThan ||
                 cond == Assembler::LessThanOrEqual ||
                 cond == Assembler::GreaterThan ||
                 cond == Assembler::GreaterThanOrEqual ||
                 cond == Assembler::Below || cond == Assembler::BelowOrEqual ||
                 cond == Assembler::Above || cond == Assembler::AboveOrEqual,
             "other condition codes not supported");

  branchPtr(cond, lhs.reg, rhs.reg, success);
  if (fail) {
    jump(fail);
  }
}

void MacroAssembler::branch64(Condition cond, const Address& lhs, Imm64 val,
                              Label* success, Label* fail) {
  MOZ_ASSERT(cond == Assembler::NotEqual || cond == Assembler::Equal ||
                 cond == Assembler::LessThan ||
                 cond == Assembler::LessThanOrEqual ||
                 cond == Assembler::GreaterThan ||
                 cond == Assembler::GreaterThanOrEqual ||
                 cond == Assembler::Below || cond == Assembler::BelowOrEqual ||
                 cond == Assembler::Above || cond == Assembler::AboveOrEqual,
             "other condition codes not supported");

  branchPtr(cond, lhs, ImmWord(val.value), success);
  if (fail) {
    jump(fail);
  }
}

void MacroAssembler::branch64(Condition cond, const Address& lhs,
                              Register64 rhs, Label* success, Label* fail) {
  MOZ_ASSERT(cond == Assembler::NotEqual || cond == Assembler::Equal ||
                 cond == Assembler::LessThan ||
                 cond == Assembler::LessThanOrEqual ||
                 cond == Assembler::GreaterThan ||
                 cond == Assembler::GreaterThanOrEqual ||
                 cond == Assembler::Below || cond == Assembler::BelowOrEqual ||
                 cond == Assembler::Above || cond == Assembler::AboveOrEqual,
             "other condition codes not supported");

  branchPtr(cond, lhs, rhs.reg, success);
  if (fail) {
    jump(fail);
  }
}

void MacroAssembler::branch64(Condition cond, const Address& lhs,
                              const Address& rhs, Register scratch,
                              Label* label) {
  MOZ_ASSERT(cond == Assembler::NotEqual || cond == Assembler::Equal,
             "other condition codes not supported");
  MOZ_ASSERT(lhs.base != scratch);
  MOZ_ASSERT(rhs.base != scratch);

  loadPtr(rhs, scratch);
  branchPtr(cond, lhs, scratch, label);
}

void MacroAssembler::branchPrivatePtr(Condition cond, const Address& lhs,
                                      Register rhs, Label* label) {
  branchPtr(cond, lhs, rhs, label);
}

void MacroAssembler::branchTest64(Condition cond, Register64 lhs,
                                  Register64 rhs, Register temp, Label* success,
                                  Label* fail) {
  branchTestPtr(cond, lhs.reg, rhs.reg, success);
  if (fail) {
    jump(fail);
  }
}

void MacroAssembler::branchTest64(Condition cond, Register64 lhs, Imm64 rhs,
                                  Label* success, Label* fail) {
  branchTestPtr(cond, lhs.reg, ImmWord(rhs.value), success);
  if (fail) {
    jump(fail);
  }
}

void MacroAssembler::branchTestUndefined(Condition cond,
                                         const ValueOperand& value,
                                         Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  splitTag(value, scratch2);
  branchTestUndefined(cond, scratch2, label);
}

void MacroAssembler::branchTestInt32(Condition cond, const ValueOperand& value,
                                     Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  splitTag(value, scratch2);
  branchTestInt32(cond, scratch2, label);
}

void MacroAssembler::branchTestInt32Truthy(bool b, const ValueOperand& value,
                                           Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  ma_dext(scratch, value.valueReg(), Imm32(0), Imm32(32));
  ma_b(scratch, scratch, label, b ? NonZero : Zero);
}

void MacroAssembler::branchTestDouble(Condition cond, Register tag,
                                      Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  Condition actual = (cond == Equal) ? BelowOrEqual : Above;
  ma_b(tag, ImmTag(JSVAL_TAG_MAX_DOUBLE), label, actual);
}

void MacroAssembler::branchTestDouble(Condition cond, const ValueOperand& value,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  splitTag(value, scratch2);
  branchTestDouble(cond, scratch2, label);
}

void MacroAssembler::branchTestNumber(Condition cond, const ValueOperand& value,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  splitTag(value, scratch2);
  branchTestNumber(cond, scratch2, label);
}

void MacroAssembler::branchTestBoolean(Condition cond,
                                       const ValueOperand& value,
                                       Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  splitTag(value, scratch2);
  branchTestBoolean(cond, scratch2, label);
}

void MacroAssembler::branchTestBooleanTruthy(bool b, const ValueOperand& value,
                                             Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  unboxBoolean(value, scratch2);
  ma_b(scratch2, scratch2, label, b ? NonZero : Zero);
}

void MacroAssembler::branchTestString(Condition cond, const ValueOperand& value,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  splitTag(value, scratch2);
  branchTestString(cond, scratch2, label);
}

void MacroAssembler::branchTestStringTruthy(bool b, const ValueOperand& value,
                                            Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  unboxString(value, scratch2);
  load32(Address(scratch2, JSString::offsetOfLength()), scratch2);
  ma_b(scratch2, Imm32(0), label, b ? NotEqual : Equal);
}

void MacroAssembler::branchTestSymbol(Condition cond, const ValueOperand& value,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  splitTag(value, scratch2);
  branchTestSymbol(cond, scratch2, label);
}

void MacroAssembler::branchTestBigInt(Condition cond, const BaseIndex& address,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  computeEffectiveAddress(address, scratch2);
  splitTag(scratch2, scratch2);
  branchTestBigInt(cond, scratch2, label);
}

void MacroAssembler::branchTestBigInt(Condition cond, const ValueOperand& value,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  splitTag(value, scratch2);
  branchTestBigInt(cond, scratch2, label);
}

void MacroAssembler::branchTestBigIntTruthy(bool b, const ValueOperand& value,
                                            Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  unboxBigInt(value, scratch2);
  load32(Address(scratch2, BigInt::offsetOfDigitLength()), scratch2);
  ma_b(scratch2, Imm32(0), label, b ? NotEqual : Equal);
}

void MacroAssembler::branchTestNull(Condition cond, const ValueOperand& value,
                                    Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  splitTag(value, scratch2);
  branchTestNull(cond, scratch2, label);
}

void MacroAssembler::branchTestNullOrUndefined(Condition cond,
                                               const ValueOperand& value,
                                               Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  splitTag(value, scratch2);
  branchTestNullOrUndefined(cond, scratch2, label);
}

void MacroAssembler::branchTestObject(Condition cond, const ValueOperand& value,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  splitTag(value, scratch2);
  branchTestObject(cond, scratch2, label);
}

void MacroAssembler::branchTestPrimitive(Condition cond,
                                         const ValueOperand& value,
                                         Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  splitTag(value, scratch2);
  branchTestPrimitive(cond, scratch2, label);
}

void MacroAssembler::branchTestMagic(Condition cond, const ValueOperand& value,
                                     Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  splitTag(value, scratch2);
  ma_b(scratch2, ImmTag(JSVAL_TAG_MAGIC), label, cond);
}

void MacroAssembler::branchTestMagic(Condition cond, const Address& valaddr,
                                     JSWhyMagic why, Label* label) {
  uint64_t magic = MagicValue(why).asRawBits();
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  loadPtr(valaddr, scratch);
  ma_b(scratch, ImmWord(magic), label, cond);
}

void MacroAssembler::branchTestMagic(Condition cond, const BaseIndex& valaddr,
                                     JSWhyMagic why, Label* label) {
  uint64_t magic = MagicValue(why).asRawBits();
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  loadPtr(valaddr, scratch);
  ma_b(scratch, ImmWord(magic), label, cond);
}

template <typename T>
void MacroAssembler::branchTestValue(Condition cond, const T& lhs,
                                     const ValueOperand& rhs, Label* label) {
  MOZ_ASSERT(cond == Assembler::Equal || cond == Assembler::NotEqual);
  branchPtr(cond, lhs, rhs.valueReg(), label);
}

void MacroAssembler::branchTruncateDoubleMaybeModUint32(FloatRegister src,
                                                        Register dest,
                                                        Label* fail) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  as_truncld(ScratchDoubleReg, src);
  as_cfc1(scratch, Assembler::FCSR);
  moveFromDouble(ScratchDoubleReg, dest);
  ma_ext(scratch, scratch, Assembler::CauseV, 1);
  ma_b(scratch, Imm32(0), fail, Assembler::NotEqual);

  as_sll(dest, dest, 0);
}

void MacroAssembler::branchTruncateFloat32MaybeModUint32(FloatRegister src,
                                                         Register dest,
                                                         Label* fail) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  as_truncls(ScratchDoubleReg, src);
  as_cfc1(scratch, Assembler::FCSR);
  moveFromDouble(ScratchDoubleReg, dest);
  ma_ext(scratch, scratch, Assembler::CauseV, 1);
  ma_b(scratch, Imm32(0), fail, Assembler::NotEqual);

  as_sll(dest, dest, 0);
}

void MacroAssembler::branchTruncateDoubleToInt32(FloatRegister src,
                                                 Register dest, Label* fail) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  ScratchDoubleScope fpscratch(asMasm());

  // Convert scalar to signed 64-bit fixed-point, rounding toward zero.
  // In the case of -0, the output is zero.
  // In the case of overflow, the output is:
  //   - MIPS64R2: 2^63-1
  //   - MIPS64R6: saturated
  // In the case of NaN, the output is:
  //   - MIPS64R2: 2^63-1
  //   - MIPS64R6: 0
  as_truncld(fpscratch, src);
  moveFromDouble(fpscratch, dest);

  // Fail on overflow cases, besides MIPS64R2 will also fail here on NaN cases.
  as_sll(scratch, dest, 0);
  ma_b(dest, scratch, fail, Assembler::NotEqual);
}

void MacroAssembler::branchInt64NotInPtrRange(Register64 src, Label* label) {
  // No-op on 64-bit platforms.
}

void MacroAssembler::branchUInt64NotInPtrRange(Register64 src, Label* label) {
  branchTest64(Assembler::Signed, src, src, label);
}

void MacroAssembler::fallibleUnboxPtr(const ValueOperand& src, Register dest,
                                      JSValueType type, Label* fail) {
  MOZ_ASSERT(type == JSVAL_TYPE_OBJECT || type == JSVAL_TYPE_STRING ||
             type == JSVAL_TYPE_SYMBOL || type == JSVAL_TYPE_BIGINT);
  // dest := src XOR mask
  // scratch := dest >> JSVAL_TAG_SHIFT
  // fail if scratch != 0
  //
  // Note: src and dest can be the same register
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  mov(ImmShiftedTag(type), scratch);
  ma_xor(scratch, src.valueReg());
  ma_move(dest, scratch);
  ma_dsrl(scratch, scratch, Imm32(JSVAL_TAG_SHIFT));
  ma_b(scratch, Imm32(0), fail, Assembler::NotEqual);
}

void MacroAssembler::fallibleUnboxPtr(const Address& src, Register dest,
                                      JSValueType type, Label* fail) {
  loadValue(src, ValueOperand(dest));
  fallibleUnboxPtr(ValueOperand(dest), dest, type, fail);
}

void MacroAssembler::fallibleUnboxPtr(const BaseIndex& src, Register dest,
                                      JSValueType type, Label* fail) {
  loadValue(src, ValueOperand(dest));
  fallibleUnboxPtr(ValueOperand(dest), dest, type, fail);
}

// ===============================================================
// 128-bit arithmetic

void MacroAssembler::wasmAddSubI128HI64(Register lhsLo, Register lhsHi,
                                        Register rhsLo, Register rhsHi,
                                        Register output, bool isAdd) {
  // Require: the output is not the same as any of the inputs.
  MOZ_RELEASE_ASSERT(output != lhsLo && output != lhsHi && output != rhsLo &&
                     output != rhsHi);
  // We use `output` as a temp to hold the carry or borrow.
  if (isAdd) {
    as_daddu(output, lhsLo, rhsLo);   // output = lhsLo + rhsLo
    as_sltu(output, output, lhsLo);   // output = carry from `lhsLo + rhsLo`
    as_daddu(output, output, lhsHi);  // output = carry + lhsHi
    as_daddu(output, output, rhsHi);  // output = carry + lhsHi + rhsHi
  } else {
    as_sltu(output, lhsLo, rhsLo);    // output = borrow from `lhsLo - rhsLo`
    as_dsubu(output, lhsHi, output);  // output = lhsHi - borrow
    as_dsubu(output, output, rhsHi);  // output = lhsHi - borrow - rhsHi
  }
}

void MacroAssembler::wasmMulI64WideHI64(Register lhs, Register rhs,
                                        Register output, bool isSigned) {
  if (isSigned) {
#ifdef MIPSR6
    as_dmuh(output, lhs, rhs);
#else
    as_dmult(lhs, rhs);
    as_mfhi(output);
#endif
  } else {
#ifdef MIPSR6
    as_dmuhu(output, lhs, rhs);
#else
    as_dmultu(lhs, rhs);
    as_mfhi(output);
#endif
  }
}

//}}} check_macroassembler_style
// ===============================================================

// The specializations for cmpPtrSet are outside the braces because
// check_macroassembler_style can't yet deal with specializations.

template <>
inline void MacroAssembler::cmpPtrSet(Assembler::Condition cond, Address lhs,
                                      ImmPtr rhs, Register dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  loadPtr(lhs, scratch2);
  cmpPtrSet(cond, scratch2, rhs, dest);
}

template <>
inline void MacroAssembler::cmpPtrSet(Assembler::Condition cond, Register lhs,
                                      Address rhs, Register dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  loadPtr(rhs, scratch);
  cmpPtrSet(cond, lhs, scratch, dest);
}

template <>
inline void MacroAssembler::cmpPtrSet(Assembler::Condition cond, Address lhs,
                                      Register rhs, Register dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  loadPtr(lhs, scratch);
  cmpPtrSet(cond, scratch, rhs, dest);
}

template <>
inline void MacroAssembler::cmp32Set(Assembler::Condition cond, Register lhs,
                                     Address rhs, Register dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  load32(rhs, scratch);
  cmp32Set(cond, lhs, scratch, dest);
}

template <>
inline void MacroAssembler::cmp32Set(Assembler::Condition cond, Address lhs,
                                     Register rhs, Register dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  load32(lhs, scratch);
  cmp32Set(cond, scratch, rhs, dest);
}

void MacroAssembler::cmpPtrMovePtr(Condition cond, Register lhs, Register rhs,
                                   Register src, Register dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(src != scratch && dest != scratch);
  cmpPtrSet(cond, lhs, rhs, scratch);
#ifdef MIPSR6
  as_selnez(src, src, scratch);
  as_seleqz(dest, dest, scratch);
  as_or(dest, dest, src);
#else
  as_movn(dest, src, scratch);
#endif
}

void MacroAssembler::cmpPtrMovePtr(Condition cond, Register lhs,
                                   const Address& rhs, Register src,
                                   Register dest) {
  MOZ_CRASH("NYI");
}

void MacroAssemblerMIPS64Compat::incrementInt32Value(const Address& addr) {
  asMasm().add32(Imm32(1), addr);
}

void MacroAssemblerMIPS64Compat::retn(Imm32 n) {
  // pc <- [sp]; sp += n
  loadPtr(Address(StackPointer, 0), ra);
  asMasm().addPtr(n, StackPointer);
  as_jr(ra);
  as_nop();
}

}  // namespace jit
}  // namespace js

#endif /* jit_mips64_MacroAssembler_mips64_inl_h */
