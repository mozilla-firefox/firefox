/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_loong64_MacroAssembler_loong64_inl_h
#define jit_loong64_MacroAssembler_loong64_inl_h

#include "jit/loong64/MacroAssembler-loong64.h"

#include "mozilla/MathAlgorithms.h"

#include <array>
#include <bit>
#include <limits>
#include <optional>
#include <utility>

namespace js {
namespace jit {

//{{{ check_macroassembler_style

// If |mask| is a contiguous run of one-bits, return its [msb, lsb] range.
static inline constexpr std::optional<std::pair<uint32_t, uint32_t>>
GetContiguousMaskRange(uint64_t mask) {
  if (mask == 0) {
    return std::nullopt;
  }
  const uint32_t lsb = std::countr_zero(mask);
  const uint64_t shifted = mask >> lsb;
  if (!std::has_single_bit(shifted + 1)) {
    return std::nullopt;
  }
  return std::make_pair(lsb + mozilla::FloorLog2(shifted), lsb);
}

static void And32ForBranch(MacroAssembler& masm, Register scratch, Register lhs,
                           Imm32 rhs, bool preserveMagnitude) {
  if (const auto maybeRange = GetContiguousMaskRange(rhs.value)) {
    const auto [msb, lsb] = *maybeRange;
    masm.as_bstrpick_d(scratch, lhs, msb, lsb);
    if (preserveMagnitude && lsb != 0) {
      // Only shift when the exact magnitude is interesting.
      masm.as_slli_d(scratch, scratch, lsb);
    }
  } else {
    masm.ma_and(scratch, lhs, rhs);
  }
}

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

void MacroAssembler::moveSimd128(FloatRegister src, FloatRegister dest) {
  if (src != dest) {
    as_vori_b(dest, src, 0);
  }
}

void MacroAssembler::moveLowDoubleToGPR(FloatRegister src, Register dest) {
  MOZ_CRASH("Not supported for this target");
}

void MacroAssembler::move64To32(Register64 src, Register dest) {
  as_slli_w(dest, src.reg, 0);
}

void MacroAssembler::move32To64ZeroExtend(Register src, Register64 dest) {
  as_bstrpick_d(dest.reg, src, 31, 0);
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
  as_slli_w(dest.reg, src, 0);
}

void MacroAssembler::moveFloat32ToGPR(FloatRegister src, Register dest) {
  moveFromFloat32(src, dest);
}

void MacroAssembler::moveGPRToFloat32(Register src, FloatRegister dest) {
  moveToFloat32(src, dest);
}

void MacroAssembler::moveFloat16ToGPR(FloatRegister src, Register dest) {
  MOZ_CRASH("Not supported for this target");
}

void MacroAssembler::moveGPRToFloat16(Register src, FloatRegister dest) {
  MOZ_CRASH("Not supported for this target");
}

void MacroAssembler::move8ZeroExtend(Register src, Register dest) {
  as_bstrpick_d(dest, src, 7, 0);
}

void MacroAssembler::move8SignExtend(Register src, Register dest) {
  as_ext_w_b(dest, src);
}

void MacroAssembler::move16SignExtend(Register src, Register dest) {
  as_ext_w_h(dest, src);
}

void MacroAssembler::move8SignExtendToPtr(Register src, Register dest) {
  move8To64SignExtend(src, Register64(dest));
}

void MacroAssembler::move16SignExtendToPtr(Register src, Register dest) {
  move16To64SignExtend(src, Register64(dest));
}

void MacroAssembler::move32SignExtendToPtr(Register src, Register dest) {
  as_slli_w(dest, src, 0);
}

void MacroAssembler::move32ZeroExtendToPtr(Register src, Register dest) {
  as_bstrpick_d(dest, src, 31, 0);
}

// ===============================================================
// Load instructions

void MacroAssembler::load32SignExtendToPtr(const Address& src, Register dest) {
  load32(src, dest);
}

void MacroAssembler::loadAbiReturnAddress(Register dest) { movePtr(ra, dest); }

// ===============================================================
// Logical instructions

void MacroAssembler::not32(Register reg) { as_nor(reg, reg, zero); }

void MacroAssembler::notPtr(Register reg) { as_nor(reg, reg, zero); }

void MacroAssembler::andPtr(Register src, Register dest) {
  as_and(dest, dest, src);
}

void MacroAssembler::andPtr(Imm32 imm, Register dest) {
  ma_and(dest, dest, imm);
}

void MacroAssembler::andPtr(Imm32 imm, Register src, Register dest) {
  ma_and(dest, src, imm);
}

void MacroAssembler::andPtr(Imm32 imm, const Address& dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(dest, scratch);
  andPtr(imm, scratch);
  storePtr(scratch, dest);
}

void MacroAssembler::and64(Imm64 imm, Register64 dest) {
  ma_and(dest.reg, dest.reg, ImmWord(imm.value));
}

void MacroAssembler::and64(Register64 src, Register64 dest) {
  as_and(dest.reg, dest.reg, src.reg);
}

void MacroAssembler::and32(Register src, Register dest) {
  as_and(dest, dest, src);
}

void MacroAssembler::and32(Imm32 imm, Register dest) {
  ma_and(dest, dest, imm);
}

void MacroAssembler::and32(Imm32 imm, Register src, Register dest) {
  ma_and(dest, src, imm);
}

void MacroAssembler::and32(Imm32 imm, const Address& dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(dest, scratch);
  and32(imm, scratch);
  store32(scratch, dest);
}

void MacroAssembler::and32(const Address& src, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(src, scratch);
  as_and(dest, dest, scratch);
}

void MacroAssembler::or64(Imm64 imm, Register64 dest) {
  ma_or(dest.reg, dest.reg, ImmWord(imm.value));
}

void MacroAssembler::or32(Register src, Register dest) {
  as_or(dest, dest, src);
}

void MacroAssembler::or32(Imm32 imm, Register dest) { ma_or(dest, dest, imm); }

void MacroAssembler::or32(Imm32 imm, Register src, Register dest) {
  ma_or(dest, src, imm);
}

void MacroAssembler::or32(Imm32 imm, const Address& dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(dest, scratch);
  or32(imm, scratch);
  store32(scratch, dest);
}

void MacroAssembler::xor64(Imm64 imm, Register64 dest) {
  ma_xor(dest.reg, dest.reg, ImmWord(imm.value));
}

void MacroAssembler::orPtr(Register src, Register dest) {
  as_or(dest, dest, src);
}

void MacroAssembler::orPtr(Imm32 imm, Register dest) { ma_or(dest, dest, imm); }

void MacroAssembler::orPtr(Imm32 imm, Register src, Register dest) {
  ma_or(dest, src, imm);
}

void MacroAssembler::or64(Register64 src, Register64 dest) {
  as_or(dest.reg, dest.reg, src.reg);
}

void MacroAssembler::xor64(Register64 src, Register64 dest) {
  as_xor(dest.reg, dest.reg, src.reg);
}

void MacroAssembler::xorPtr(Register src, Register dest) {
  as_xor(dest, dest, src);
}

void MacroAssembler::xorPtr(Imm32 imm, Register dest) {
  ma_xor(dest, dest, imm);
}

void MacroAssembler::xorPtr(Imm32 imm, Register src, Register dest) {
  ma_xor(dest, src, imm);
}

void MacroAssembler::xor32(Register src, Register dest) {
  as_xor(dest, dest, src);
}

void MacroAssembler::xor32(Imm32 imm, Register dest) {
  ma_xor(dest, dest, imm);
}

void MacroAssembler::xor32(Imm32 imm, Register src, Register dest) {
  ma_xor(dest, src, imm);
}

void MacroAssembler::xor32(Imm32 imm, const Address& dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(dest, scratch);
  xor32(imm, scratch);
  store32(scratch, dest);
}

void MacroAssembler::xor32(const Address& src, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(src, scratch);
  xor32(scratch, dest);
}

void MacroAssembler::nor32(Imm32 imm, Register src, Register dest) {
  ma_nor(dest, src, imm);
}

// ===============================================================
// Swap instructions

void MacroAssembler::byteSwap16SignExtend(Register reg) {
  as_revb_2h(reg, reg);
  as_ext_w_h(reg, reg);
}

void MacroAssembler::byteSwap16ZeroExtend(Register reg) {
  as_revb_2h(reg, reg);
  as_bstrpick_d(reg, reg, 15, 0);
}

void MacroAssembler::byteSwap32(Register reg) {
  as_revb_2w(reg, reg);
  as_slli_w(reg, reg, 0);
}

void MacroAssembler::byteSwap64(Register64 reg64) {
  as_revb_d(reg64.reg, reg64.reg);
}

// ===============================================================
// Arithmetic functions

void MacroAssembler::addPtr(Register src, Register dest) {
  as_add_d(dest, dest, src);
}

void MacroAssembler::addPtr(Imm32 imm, Register dest) {
  ma_add_d(dest, dest, imm);
}

void MacroAssembler::addPtr(ImmWord imm, Register dest) {
  ma_add_d(dest, dest, imm);
}

void MacroAssembler::add64(Register64 src, Register64 dest) {
  addPtr(src.reg, dest.reg);
}

void MacroAssembler::add64(Imm32 imm, Register64 dest) {
  ma_add_d(dest.reg, dest.reg, imm);
}

void MacroAssembler::add64(Imm64 imm, Register64 dest) {
  ma_add_d(dest.reg, dest.reg, ImmWord(imm.value));
}

void MacroAssembler::add32(Register src, Register dest) {
  as_add_w(dest, dest, src);
}

void MacroAssembler::add32(Imm32 imm, Register dest) {
  ma_add_w(dest, dest, imm);
}

void MacroAssembler::add32(Imm32 imm, Register src, Register dest) {
  ma_add_w(dest, src, imm);
}

void MacroAssembler::add32(Imm32 imm, const Address& dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(dest, scratch);
  ma_add_w(scratch, scratch, imm);
  store32(scratch, dest);
}

void MacroAssembler::add32(const Address& src, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(src, scratch);
  as_add_w(dest, dest, scratch);
}

void MacroAssembler::addPtr(Imm32 imm, const Address& dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(dest, scratch);
  addPtr(imm, scratch);
  storePtr(scratch, dest);
}

void MacroAssembler::addPtr(const Address& src, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(src, scratch);
  addPtr(scratch, dest);
}

void MacroAssembler::addDouble(FloatRegister src, FloatRegister dest) {
  as_fadd_d(dest, dest, src);
}

void MacroAssembler::addFloat32(FloatRegister src, FloatRegister dest) {
  as_fadd_s(dest, dest, src);
}

CodeOffset MacroAssembler::sub32FromStackPtrWithPatch(Register dest) {
  CodeOffset offset = CodeOffset(currentOffset());
  MacroAssemblerLOONG64::ma_liPatchable(dest, Imm32(0));
  as_sub_d(dest, StackPointer, dest);
  return offset;
}

void MacroAssembler::patchSub32FromStackPtr(CodeOffset offset, Imm32 imm) {
  // TODO: by wangqing
  InstImm* lu12i_w = (InstImm*)m_buffer.getInst(BufferOffset(offset.offset()));
  InstImm* ori = (InstImm*)m_buffer.getInst(BufferOffset(offset.offset() + 4));

  MOZ_ASSERT((lu12i_w->extractBitField(31, 25)) ==
             ((uint32_t)op_lu12i_w >> 25));
  MOZ_ASSERT((ori->extractBitField(31, 22)) == ((uint32_t)op_ori >> 22));

  *lu12i_w = InstImm(op_lu12i_w, (int32_t)((imm.value >> 12) & 0xfffff),
                     Register::FromCode(lu12i_w->extractRD()), false);
  *ori = InstImm(op_ori, (int32_t)(imm.value & 0xfff),
                 Register::FromCode(ori->extractRJ()),
                 Register::FromCode(ori->extractRD()), 12);
}

void MacroAssembler::subPtr(Register src, Register dest) {
  as_sub_d(dest, dest, src);
}

void MacroAssembler::subPtr(Imm32 imm, Register dest) {
  ma_sub_d(dest, dest, imm);
}

void MacroAssembler::sub64(Register64 src, Register64 dest) {
  as_sub_d(dest.reg, dest.reg, src.reg);
}

void MacroAssembler::sub64(Imm64 imm, Register64 dest) {
  ma_sub_d(dest.reg, dest.reg, ImmWord(imm.value));
}

void MacroAssembler::sub32(Register src, Register dest) {
  as_sub_w(dest, dest, src);
}

void MacroAssembler::sub32(Imm32 imm, Register dest) {
  ma_sub_w(dest, dest, imm);
}

void MacroAssembler::sub32(const Address& src, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(src, scratch);
  as_sub_w(dest, dest, scratch);
}

void MacroAssembler::subPtr(Register src, const Address& dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(dest, scratch);
  subPtr(src, scratch);
  storePtr(scratch, dest);
}

void MacroAssembler::subPtr(const Address& addr, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(addr, scratch);
  subPtr(scratch, dest);
}

void MacroAssembler::subDouble(FloatRegister src, FloatRegister dest) {
  as_fsub_d(dest, dest, src);
}

void MacroAssembler::subFloat32(FloatRegister src, FloatRegister dest) {
  as_fsub_s(dest, dest, src);
}

void MacroAssembler::mul64(const Register64& rhs, const Register64& srcDest) {
  mul64(rhs, srcDest, Register::Invalid());
}

void MacroAssembler::mul64(Imm64 imm, const Register64& dest) {
  ma_mul_d(dest.reg, dest.reg, ImmWord(imm.value));
}

void MacroAssembler::mul64(Imm64 imm, const Register64& dest,
                           const Register temp) {
  MOZ_ASSERT(temp == Register::Invalid());
  mul64(imm, dest);
}

void MacroAssembler::mul64(const Register64& src, const Register64& dest,
                           const Register temp) {
  MOZ_ASSERT(temp == Register::Invalid());
  as_mul_d(dest.reg, dest.reg, src.reg);
}

void MacroAssembler::mulPtr(Register rhs, Register srcDest) {
  as_mul_d(srcDest, srcDest, rhs);
}

void MacroAssembler::mulPtr(ImmWord rhs, Register srcDest) {
  ma_mul_d(srcDest, srcDest, rhs);
}

void MacroAssembler::mulBy3(Register src, Register dest) {
  // tmp = (GR[rj][63:0]<<(sa2+1)) + GR[rk][63:0]
  // GR[rd] = tmp[63:0]
  // <https://loongson.github.io/LoongArch-Documentation/LoongArch-Vol1-EN.html#_alsl_wud>
  as_alsl_d(dest, src, src, 0);
}

void MacroAssembler::mul32(Register rhs, Register srcDest) {
  as_mul_w(srcDest, srcDest, rhs);
}

void MacroAssembler::mul32(Imm32 imm, Register srcDest) {
  ma_mul(srcDest, srcDest, imm);
}

void MacroAssembler::mulHighUnsigned32(Imm32 imm, Register src, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(src != scratch);
  move32(imm, scratch);
  as_mulh_wu(dest, src, scratch);
}

void MacroAssembler::mulFloat32(FloatRegister src, FloatRegister dest) {
  as_fmul_s(dest, dest, src);
}

void MacroAssembler::mulDouble(FloatRegister src, FloatRegister dest) {
  as_fmul_d(dest, dest, src);
}

void MacroAssembler::mulDoublePtr(ImmPtr imm, Register temp,
                                  FloatRegister dest) {
  ScratchDoubleScope fpscratch(asMasm());
  movePtr(imm, temp);
  loadDouble(Address(temp, 0), fpscratch);
  mulDouble(fpscratch, dest);
}

void MacroAssembler::inc64(AbsoluteAddress dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
  ma_li(scratch, ImmWord(uintptr_t(dest.addr)));
  as_ld_d(scratch2, scratch, 0);
  as_addi_d(scratch2, scratch2, 1);
  as_st_d(scratch2, scratch, 0);
}

void MacroAssembler::quotient32(Register lhs, Register rhs, Register dest,
                                bool isUnsigned) {
  if (isUnsigned) {
    as_div_wu(dest, lhs, rhs);
  } else {
    as_div_w(dest, lhs, rhs);
  }
}

void MacroAssembler::quotient64(Register lhs, Register rhs, Register dest,
                                bool isUnsigned) {
  if (isUnsigned) {
    as_div_du(dest, lhs, rhs);
  } else {
    as_div_d(dest, lhs, rhs);
  }
}

void MacroAssembler::remainder32(Register lhs, Register rhs, Register dest,
                                 bool isUnsigned) {
  if (isUnsigned) {
    as_mod_wu(dest, lhs, rhs);
  } else {
    as_mod_w(dest, lhs, rhs);
  }
}

void MacroAssembler::remainder64(Register lhs, Register rhs, Register dest,
                                 bool isUnsigned) {
  if (isUnsigned) {
    as_mod_du(dest, lhs, rhs);
  } else {
    as_mod_d(dest, lhs, rhs);
  }
}

void MacroAssembler::divFloat32(FloatRegister src, FloatRegister dest) {
  as_fdiv_s(dest, dest, src);
}

void MacroAssembler::divDouble(FloatRegister src, FloatRegister dest) {
  as_fdiv_d(dest, dest, src);
}

void MacroAssembler::neg64(Register64 reg) { as_sub_d(reg.reg, zero, reg.reg); }

void MacroAssembler::negPtr(Register reg) { as_sub_d(reg, zero, reg); }

void MacroAssembler::neg32(Register reg) { as_sub_w(reg, zero, reg); }

void MacroAssembler::negateDouble(FloatRegister reg) { as_fneg_d(reg, reg); }

void MacroAssembler::negateFloat(FloatRegister reg) { as_fneg_s(reg, reg); }

void MacroAssembler::abs32(Register src, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  as_srai_w(scratch, src, 31);
  as_xor(dest, src, scratch);
  as_sub_w(dest, dest, scratch);
}

void MacroAssembler::absFloat32(FloatRegister src, FloatRegister dest) {
  as_fabs_s(dest, src);
}

void MacroAssembler::absDouble(FloatRegister src, FloatRegister dest) {
  as_fabs_d(dest, src);
}

void MacroAssembler::sqrtFloat32(FloatRegister src, FloatRegister dest) {
  as_fsqrt_s(dest, src);
}

void MacroAssembler::sqrtDouble(FloatRegister src, FloatRegister dest) {
  as_fsqrt_d(dest, src);
}

void MacroAssembler::min32(Register lhs, Register rhs, Register dest) {
  minMaxPtr(lhs, rhs, dest, /* isMax = */ false);
}

void MacroAssembler::min32(Register lhs, Imm32 rhs, Register dest) {
  minMaxPtr(lhs, ImmWord(rhs.value), dest, /* isMax = */ false);
}

void MacroAssembler::max32(Register lhs, Register rhs, Register dest) {
  minMaxPtr(lhs, rhs, dest, /* isMax = */ true);
}

void MacroAssembler::max32(Register lhs, Imm32 rhs, Register dest) {
  minMaxPtr(lhs, ImmWord(rhs.value), dest, /* isMax = */ true);
}

void MacroAssembler::minPtr(Register lhs, Register rhs, Register dest) {
  minMaxPtr(lhs, rhs, dest, /* isMax = */ false);
}

void MacroAssembler::minPtr(Register lhs, ImmWord rhs, Register dest) {
  minMaxPtr(lhs, rhs, dest, /* isMax = */ false);
}

void MacroAssembler::maxPtr(Register lhs, Register rhs, Register dest) {
  minMaxPtr(lhs, rhs, dest, /* isMax = */ true);
}

void MacroAssembler::maxPtr(Register lhs, ImmWord rhs, Register dest) {
  minMaxPtr(lhs, rhs, dest, /* isMax = */ true);
}

void MacroAssembler::minFloat32(FloatRegister other, FloatRegister srcDest,
                                bool handleNaN) {
  minFloat32(other, srcDest, handleNaN, /* handleZero= */ true);
}

void MacroAssembler::minFloat32(FloatRegister other, FloatRegister srcDest,
                                bool handleNaN, bool handleZero) {
  minMaxFloat32(srcDest, other, handleNaN, handleZero, /* isMax= */ false);
}

void MacroAssembler::minDouble(FloatRegister other, FloatRegister srcDest,
                               bool handleNaN) {
  minDouble(other, srcDest, handleNaN, /* handleZero= */ true);
}

void MacroAssembler::minDouble(FloatRegister other, FloatRegister srcDest,
                               bool handleNaN, bool handleZero) {
  minMaxDouble(srcDest, other, handleNaN, handleZero, /* isMax= */ false);
}

void MacroAssembler::maxFloat32(FloatRegister other, FloatRegister srcDest,
                                bool handleNaN) {
  maxFloat32(other, srcDest, handleNaN, /* handleZero= */ true);
}

void MacroAssembler::maxFloat32(FloatRegister other, FloatRegister srcDest,
                                bool handleNaN, bool handleZero) {
  minMaxFloat32(srcDest, other, handleNaN, handleZero, /* isMax= */ true);
}

void MacroAssembler::maxDouble(FloatRegister other, FloatRegister srcDest,
                               bool handleNaN) {
  maxDouble(other, srcDest, handleNaN, /* handleZero= */ true);
}

void MacroAssembler::maxDouble(FloatRegister other, FloatRegister srcDest,
                               bool handleNaN, bool handleZero) {
  minMaxDouble(srcDest, other, handleNaN, handleZero, /* isMax= */ true);
}

// ===============================================================
// Shift functions

void MacroAssembler::lshift32(Register src, Register dest) {
  as_sll_w(dest, dest, src);
}

void MacroAssembler::lshift32(Imm32 imm, Register dest) {
  lshift32(imm, dest, dest);
}

void MacroAssembler::lshift32(Imm32 imm, Register src, Register dest) {
  as_slli_w(dest, src, imm.value & 0x1f);
}

void MacroAssembler::flexibleLshift32(Register src, Register dest) {
  lshift32(src, dest);
}

void MacroAssembler::lshift64(Register shift, Register64 dest) {
  as_sll_d(dest.reg, dest.reg, shift);
}

void MacroAssembler::lshift64(Imm32 imm, Register64 dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  as_slli_d(dest.reg, dest.reg, imm.value);
}

void MacroAssembler::lshift64(Imm32 imm, Register64 src, Register64 dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  as_slli_d(dest.reg, src.reg, imm.value);
}

void MacroAssembler::lshiftPtr(Register shift, Register dest) {
  as_sll_d(dest, dest, shift);
}

void MacroAssembler::lshiftPtr(Imm32 imm, Register dest) {
  lshiftPtr(imm, dest, dest);
}

void MacroAssembler::lshiftPtr(Imm32 imm, Register src, Register dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  as_slli_d(dest, src, imm.value);
}

void MacroAssembler::flexibleLshiftPtr(Register shift, Register srcDest) {
  lshiftPtr(shift, srcDest);
}

void MacroAssembler::rshift32(Register src, Register dest) {
  as_srl_w(dest, dest, src);
}

void MacroAssembler::rshift32(Imm32 imm, Register dest) {
  rshift32(imm, dest, dest);
}

void MacroAssembler::rshift32(Imm32 imm, Register src, Register dest) {
  as_srli_w(dest, src, imm.value & 0x1f);
}

void MacroAssembler::flexibleRshift32(Register src, Register dest) {
  rshift32(src, dest);
}

void MacroAssembler::rshift32Arithmetic(Register src, Register dest) {
  as_sra_w(dest, dest, src);
}

void MacroAssembler::rshift32Arithmetic(Imm32 imm, Register dest) {
  rshift32Arithmetic(imm, dest, dest);
}

void MacroAssembler::rshift32Arithmetic(Imm32 imm, Register src,
                                        Register dest) {
  as_srai_w(dest, src, imm.value & 0x1f);
}

void MacroAssembler::flexibleRshift32Arithmetic(Register src, Register dest) {
  rshift32Arithmetic(src, dest);
}

void MacroAssembler::rshift64(Register shift, Register64 dest) {
  as_srl_d(dest.reg, dest.reg, shift);
}

void MacroAssembler::rshift64(Imm32 imm, Register64 dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  as_srli_d(dest.reg, dest.reg, imm.value);
}

void MacroAssembler::rshift64(Imm32 imm, Register64 src, Register64 dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  as_srli_d(dest.reg, src.reg, imm.value);
}

void MacroAssembler::rshift64Arithmetic(Imm32 imm, Register64 dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  as_srai_d(dest.reg, dest.reg, imm.value);
}

void MacroAssembler::rshift64Arithmetic(Imm32 imm, Register64 src,
                                        Register64 dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  as_srai_d(dest.reg, src.reg, imm.value);
}

void MacroAssembler::rshift64Arithmetic(Register shift, Register64 dest) {
  as_sra_d(dest.reg, dest.reg, shift);
}

void MacroAssembler::rshiftPtr(Register shift, Register dest) {
  as_srl_d(dest, dest, shift);
}

void MacroAssembler::rshiftPtr(Imm32 imm, Register dest) {
  rshiftPtr(imm, dest, dest);
}

void MacroAssembler::rshiftPtr(Imm32 imm, Register src, Register dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  as_srli_d(dest, src, imm.value);
}

void MacroAssembler::flexibleRshiftPtr(Register shift, Register srcDest) {
  rshiftPtr(shift, srcDest);
}

void MacroAssembler::rshiftPtrArithmetic(Imm32 imm, Register dest) {
  rshiftPtrArithmetic(imm, dest, dest);
}

void MacroAssembler::rshiftPtrArithmetic(Imm32 imm, Register src,
                                         Register dest) {
  MOZ_ASSERT(0 <= imm.value && imm.value < 64);
  as_srai_d(dest, src, imm.value);
}

void MacroAssembler::rshiftPtrArithmetic(Register shift, Register dest) {
  as_sra_d(dest, dest, shift);
}

void MacroAssembler::flexibleRshiftPtrArithmetic(Register shift,
                                                 Register srcDest) {
  rshiftPtrArithmetic(shift, srcDest);
}

// ===============================================================
// Rotation functions

void MacroAssembler::rotateLeft(Register count, Register input, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  as_sub_w(scratch, zero, count);
  as_rotr_w(dest, input, scratch);
}

void MacroAssembler::rotateLeft(Imm32 count, Register input, Register dest) {
  as_rotri_w(dest, input, (32 - count.value) & 31);
}

void MacroAssembler::rotateLeft64(Register count, Register64 src,
                                  Register64 dest, Register temp) {
  MOZ_ASSERT(temp == Register::Invalid());
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  as_sub_d(scratch, zero, count);
  as_rotr_d(dest.reg, src.reg, scratch);
}

void MacroAssembler::rotateLeft64(Imm32 count, Register64 src, Register64 dest,
                                  Register temp) {
  MOZ_ASSERT(temp == Register::Invalid());
  as_rotri_d(dest.reg, src.reg, (64 - count.value) & 63);
}

void MacroAssembler::rotateRight(Register count, Register input,
                                 Register dest) {
  as_rotr_w(dest, input, count);
}

void MacroAssembler::rotateRight(Imm32 count, Register input, Register dest) {
  as_rotri_w(dest, input, count.value & 31);
}

void MacroAssembler::rotateRight64(Register count, Register64 src,
                                   Register64 dest, Register temp) {
  MOZ_ASSERT(temp == Register::Invalid());
  as_rotr_d(dest.reg, src.reg, count);
}

void MacroAssembler::rotateRight64(Imm32 count, Register64 src, Register64 dest,
                                   Register temp) {
  MOZ_ASSERT(temp == Register::Invalid());
  as_rotri_d(dest.reg, src.reg, count.value & 63);
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

// Bit counting functions

void MacroAssembler::clz64(Register64 src, Register64 dest) {
  as_clz_d(dest.reg, src.reg);
}

void MacroAssembler::ctz64(Register64 src, Register64 dest) {
  as_ctz_d(dest.reg, src.reg);
}

void MacroAssembler::popcnt64(Register64 input, Register64 output,
                              Register tmp) {
#if defined(ENABLE_JIT_SIMD)
  if (LOONG64Flags::HasLsxExtension()) {
    ScratchSimd128Scope scratch(asMasm());
    as_vreplgr2vr_d(scratch, input.reg);
    as_vpcnt_d(scratch, scratch);
    as_movfr2gr_d(output.reg, scratch);
    return;
  }
#endif
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ma_move(output.reg, input.reg);
  as_srai_d(tmp, input.reg, 1);
  ma_li(scratch, Imm32(0x55555555));
  as_bstrins_d(scratch, scratch, 63, 32);
  as_and(tmp, tmp, scratch);
  as_sub_d(output.reg, output.reg, tmp);
  as_srai_d(tmp, output.reg, 2);
  ma_li(scratch, Imm32(0x33333333));
  as_bstrins_d(scratch, scratch, 63, 32);
  as_and(output.reg, output.reg, scratch);
  as_and(tmp, tmp, scratch);
  as_add_d(output.reg, output.reg, tmp);
  as_srli_d(tmp, output.reg, 4);
  as_add_d(output.reg, output.reg, tmp);
  ma_li(scratch, Imm32(0xF0F0F0F));
  as_bstrins_d(scratch, scratch, 63, 32);
  as_and(output.reg, output.reg, scratch);
  ma_li(tmp, Imm32(0x1010101));
  as_bstrins_d(tmp, tmp, 63, 32);
  as_mul_d(output.reg, output.reg, tmp);
  as_srai_d(output.reg, output.reg, 56);
}

void MacroAssembler::clz32(Register src, Register dest, bool knownNotZero) {
  as_clz_w(dest, src);
}

void MacroAssembler::ctz32(Register src, Register dest, bool knownNotZero) {
  as_ctz_w(dest, src);
}

void MacroAssembler::popcnt32(Register input, Register output, Register tmp) {
#if defined(ENABLE_JIT_SIMD)
  if (LOONG64Flags::HasLsxExtension()) {
    ScratchSimd128Scope scratch(asMasm());
    as_vreplgr2vr_w(scratch, input);
    as_vpcnt_w(scratch, scratch);
    as_movfr2gr_s(output, scratch);
    return;
  }
#endif
  // Equivalent to GCC output of std::popcount()
  ma_move(output, input);
  as_srai_w(tmp, input, 1);
  ma_and(tmp, tmp, Imm32(0x55555555));
  as_sub_w(output, output, tmp);
  as_srai_w(tmp, output, 2);
  ma_and(output, output, Imm32(0x33333333));
  ma_and(tmp, tmp, Imm32(0x33333333));
  as_add_w(output, output, tmp);
  as_srli_w(tmp, output, 4);
  as_add_w(output, output, tmp);
  ma_and(output, output, Imm32(0xF0F0F0F));
  as_slli_w(tmp, output, 8);
  as_add_w(output, output, tmp);
  as_slli_w(tmp, output, 16);
  as_add_w(output, output, tmp);
  as_srai_w(output, output, 24);
}

// ===============================================================
// Condition functions

void MacroAssembler::cmp8Set(Condition cond, Address lhs, Imm32 rhs,
                             Register dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(scratch != lhs.base);

  switch (cond) {
    case Assembler::Equal:
    case Assembler::NotEqual:
    case Assembler::Above:
    case Assembler::AboveOrEqual:
    case Assembler::Below:
    case Assembler::BelowOrEqual:
      load8ZeroExtend(lhs, scratch);
      ma_cmp_set(dest, scratch, Imm32(uint8_t(rhs.value)), cond);
      break;

    case Assembler::GreaterThan:
    case Assembler::GreaterThanOrEqual:
    case Assembler::LessThan:
    case Assembler::LessThanOrEqual:
      load8SignExtend(lhs, scratch);
      ma_cmp_set(dest, scratch, Imm32(int8_t(rhs.value)), cond);
      break;

    default:
      MOZ_CRASH("unexpected condition");
  }
}

void MacroAssembler::cmp16Set(Condition cond, Address lhs, Imm32 rhs,
                              Register dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(scratch != lhs.base);

  switch (cond) {
    case Assembler::Equal:
    case Assembler::NotEqual:
    case Assembler::Above:
    case Assembler::AboveOrEqual:
    case Assembler::Below:
    case Assembler::BelowOrEqual:
      load16ZeroExtend(lhs, scratch);
      ma_cmp_set(dest, scratch, Imm32(uint16_t(rhs.value)), cond);
      break;

    case Assembler::GreaterThan:
    case Assembler::GreaterThanOrEqual:
    case Assembler::LessThan:
    case Assembler::LessThanOrEqual:
      load16SignExtend(lhs, scratch);
      ma_cmp_set(dest, scratch, Imm32(int16_t(rhs.value)), cond);
      break;

    default:
      MOZ_CRASH("unexpected condition");
  }
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

template <typename T1, typename T2>
void MacroAssembler::cmpPtrSet(Condition cond, T1 lhs, T2 rhs, Register dest) {
  ma_cmp_set(dest, lhs, rhs, cond);
}

// ===============================================================
// Branch functions

void MacroAssembler::branch8(Condition cond, const Address& lhs, Imm32 rhs,
                             Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(scratch != lhs.base);

  switch (cond) {
    case Assembler::Equal:
    case Assembler::NotEqual:
    case Assembler::Above:
    case Assembler::AboveOrEqual:
    case Assembler::Below:
    case Assembler::BelowOrEqual:
      load8ZeroExtend(lhs, scratch);
      branch32(cond, scratch, Imm32(uint8_t(rhs.value)), label);
      break;

    case Assembler::GreaterThan:
    case Assembler::GreaterThanOrEqual:
    case Assembler::LessThan:
    case Assembler::LessThanOrEqual:
      load8SignExtend(lhs, scratch);
      branch32(cond, scratch, Imm32(int8_t(rhs.value)), label);
      break;

    default:
      MOZ_CRASH("unexpected condition");
  }
}

void MacroAssembler::branch8(Condition cond, const BaseIndex& lhs, Register rhs,
                             Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(scratch != lhs.base);

  switch (cond) {
    case Assembler::Equal:
    case Assembler::NotEqual:
    case Assembler::Above:
    case Assembler::AboveOrEqual:
    case Assembler::Below:
    case Assembler::BelowOrEqual:
      load8ZeroExtend(lhs, scratch);
      branch32(cond, scratch, rhs, label);
      break;

    case Assembler::GreaterThan:
    case Assembler::GreaterThanOrEqual:
    case Assembler::LessThan:
    case Assembler::LessThanOrEqual:
      load8SignExtend(lhs, scratch);
      branch32(cond, scratch, rhs, label);
      break;

    default:
      MOZ_CRASH("unexpected condition");
  }
}

void MacroAssembler::branch16(Condition cond, const Address& lhs, Imm32 rhs,
                              Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(scratch != lhs.base);

  switch (cond) {
    case Assembler::Equal:
    case Assembler::NotEqual:
    case Assembler::Above:
    case Assembler::AboveOrEqual:
    case Assembler::Below:
    case Assembler::BelowOrEqual:
      load16ZeroExtend(lhs, scratch);
      branch32(cond, scratch, Imm32(uint16_t(rhs.value)), label);
      break;

    case Assembler::GreaterThan:
    case Assembler::GreaterThanOrEqual:
    case Assembler::LessThan:
    case Assembler::LessThanOrEqual:
      load16SignExtend(lhs, scratch);
      branch32(cond, scratch, Imm32(int16_t(rhs.value)), label);
      break;

    default:
      MOZ_CRASH("unexpected condition");
  }
}

void MacroAssembler::branch32(Condition cond, Register lhs, Register rhs,
                              Label* label) {
  ma_b(lhs, rhs, label, cond);
}

void MacroAssembler::branch32(Condition cond, Register lhs, Imm32 imm,
                              Label* label) {
  ma_b(lhs, imm, label, cond);
}

void MacroAssembler::branch32(Condition cond, const Address& lhs, Register rhs,
                              Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(lhs, scratch);
  ma_b(scratch, rhs, label, cond);
}

void MacroAssembler::branch32(Condition cond, const Address& lhs, Imm32 rhs,
                              Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(lhs, scratch);
  ma_b(scratch, rhs, label, cond);
}

void MacroAssembler::branch32(Condition cond, const AbsoluteAddress& lhs,
                              Register rhs, Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(lhs, scratch);
  ma_b(scratch, rhs, label, cond);
}

void MacroAssembler::branch32(Condition cond, const AbsoluteAddress& lhs,
                              Imm32 rhs, Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(lhs, scratch);
  ma_b(scratch, rhs, label, cond);
}

void MacroAssembler::branch32(Condition cond, const BaseIndex& lhs, Imm32 rhs,
                              Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(lhs, scratch);
  ma_b(scratch, rhs, label, cond);
}

void MacroAssembler::branch32(Condition cond, wasm::SymbolicAddress addr,
                              Imm32 imm, Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(addr, scratch);
  ma_b(scratch, imm, label, cond);
}

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

void MacroAssembler::branchPtr(Condition cond, Register lhs, Register rhs,
                               Label* label) {
  ma_b(lhs, rhs, label, cond);
}

void MacroAssembler::branchPtr(Condition cond, Register lhs, Imm32 rhs,
                               Label* label) {
  ma_b(lhs, rhs, label, cond);
}

void MacroAssembler::branchPtr(Condition cond, Register lhs, ImmPtr rhs,
                               Label* label) {
  ma_b(lhs, rhs, label, cond);
}

void MacroAssembler::branchPtr(Condition cond, Register lhs, ImmGCPtr rhs,
                               Label* label) {
  ma_b(lhs, rhs, label, cond);
}

void MacroAssembler::branchPtr(Condition cond, Register lhs, ImmWord rhs,
                               Label* label) {
  ma_b(lhs, rhs, label, cond);
}

void MacroAssembler::branchPtr(Condition cond, const Address& lhs, Register rhs,
                               Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(lhs, scratch);
  branchPtr(cond, scratch, rhs, label);
}

void MacroAssembler::branchPtr(Condition cond, const Address& lhs, ImmPtr rhs,
                               Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(lhs, scratch);
  branchPtr(cond, scratch, rhs, label);
}

void MacroAssembler::branchPtr(Condition cond, const Address& lhs, ImmGCPtr rhs,
                               Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(lhs, scratch);
  branchPtr(cond, scratch, rhs, label);
}

void MacroAssembler::branchPtr(Condition cond, const Address& lhs, ImmWord rhs,
                               Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(lhs, scratch);
  branchPtr(cond, scratch, rhs, label);
}

void MacroAssembler::branchPtr(Condition cond, const AbsoluteAddress& lhs,
                               Register rhs, Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(lhs, scratch);
  branchPtr(cond, scratch, rhs, label);
}

void MacroAssembler::branchPtr(Condition cond, const AbsoluteAddress& lhs,
                               ImmWord rhs, Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(lhs, scratch);
  branchPtr(cond, scratch, rhs, label);
}

void MacroAssembler::branchPtr(Condition cond, wasm::SymbolicAddress lhs,
                               Register rhs, Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(lhs, scratch);
  branchPtr(cond, scratch, rhs, label);
}

void MacroAssembler::branchPtr(Condition cond, const BaseIndex& lhs,
                               Register rhs, Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(lhs, scratch);
  branchPtr(cond, scratch, rhs, label);
}

void MacroAssembler::branchPtr(Condition cond, const BaseIndex& lhs,
                               ImmWord rhs, Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(lhs, scratch);
  branchPtr(cond, scratch, rhs, label);
}

void MacroAssembler::branchPrivatePtr(Condition cond, const Address& lhs,
                                      Register rhs, Label* label) {
  branchPtr(cond, lhs, rhs, label);
}

void MacroAssembler::branchFloat(DoubleCondition cond, FloatRegister lhs,
                                 FloatRegister rhs, Label* label) {
  ma_bc_s(lhs, rhs, label, cond);
}

void MacroAssembler::branchTruncateFloat32MaybeModUint32(FloatRegister src,
                                                         Register dest,
                                                         Label* fail) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ScratchDoubleScope fpscratch(asMasm());
  as_ftintrz_l_s(fpscratch, src);
  as_movfcsr2gr(scratch);
  moveFromDouble(fpscratch, dest);
  MOZ_ASSERT(Assembler::CauseV < 32);
  as_bstrpick_w(scratch, scratch, Assembler::CauseV, Assembler::CauseV);
  ma_b(scratch, Imm32(0), fail, Assembler::NotEqual);

  as_slli_w(dest, dest, 0);
}

void MacroAssembler::branchTruncateFloat32ToInt32(FloatRegister src,
                                                  Register dest, Label* fail) {
  convertFloat32ToInt32(src, dest, fail, false);
}

void MacroAssembler::branchDouble(DoubleCondition cond, FloatRegister lhs,
                                  FloatRegister rhs, Label* label) {
  ma_bc_d(lhs, rhs, label, cond);
}

void MacroAssembler::branchTruncateDoubleMaybeModUint32(FloatRegister src,
                                                        Register dest,
                                                        Label* fail) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ScratchDoubleScope fpscratch(asMasm());
  as_ftintrz_l_d(fpscratch, src);
  as_movfcsr2gr(scratch);
  moveFromDouble(fpscratch, dest);
  MOZ_ASSERT(Assembler::CauseV < 32);
  as_bstrpick_w(scratch, scratch, Assembler::CauseV, Assembler::CauseV);
  ma_b(scratch, Imm32(0), fail, Assembler::NotEqual);

  as_slli_w(dest, dest, 0);
}

void MacroAssembler::branchTruncateDoubleToInt32(FloatRegister src,
                                                 Register dest, Label* fail) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ScratchDoubleScope fpscratch(asMasm());

  // Convert scalar to signed 64-bit fixed-point, rounding toward zero.
  // In the case of overflow, the output is saturated.
  // In the case of NaN and -0, the output is zero.
  as_ftintrz_l_d(fpscratch, src);
  moveFromDouble(fpscratch, dest);

  // Fail on overflow cases.
  as_slli_w(scratch, dest, 0);
  ma_b(dest, scratch, fail, Assembler::NotEqual);
}

void MacroAssembler::branchInt64NotInPtrRange(Register64 src, Label* label) {
  // No-op on 64-bit platforms.
}

void MacroAssembler::branchUInt64NotInPtrRange(Register64 src, Label* label) {
  branchTest64(Assembler::Signed, src, src, label);
}

template <typename T>
void MacroAssembler::branchAdd32(Condition cond, T src, Register dest,
                                 Label* overflow) {
  switch (cond) {
    case Overflow:
      ma_add32TestOverflow(dest, dest, src, overflow);
      break;
    case CarryClear:
    case CarrySet:
      ma_add32TestCarry(cond, dest, dest, src, overflow);
      break;
    default:
      MOZ_CRASH("NYI");
  }
}

// the type of 'T src' maybe a Register, maybe a Imm32,depends on who call it.
template <typename T>
void MacroAssembler::branchSub32(Condition cond, T src, Register dest,
                                 Label* overflow) {
  switch (cond) {
    case Overflow:
      ma_sub32TestOverflow(dest, dest, src, overflow);
      break;
    case NonZero:
    case Zero:
    case Signed:
    case NotSigned:
      ma_sub_w(dest, dest, src);
      ma_b(dest, dest, overflow, cond);
      break;
    default:
      MOZ_CRASH("NYI");
  }
}

template <typename T>
void MacroAssembler::branchMul32(Condition cond, T src, Register dest,
                                 Label* overflow) {
  MOZ_ASSERT(cond == Assembler::Overflow);
  ma_mul32TestOverflow(dest, dest, src, overflow);
}

template <typename T>
void MacroAssembler::branchRshift32(Condition cond, T src, Register dest,
                                    Label* label) {
  MOZ_ASSERT(cond == Zero || cond == NonZero);
  rshift32(src, dest);
  branch32(cond == Zero ? Equal : NotEqual, dest, Imm32(0), label);
}

void MacroAssembler::branchNeg32(Condition cond, Register reg, Label* label) {
  MOZ_ASSERT(cond == Overflow);
  neg32(reg);
  branch32(Assembler::Equal, reg, Imm32(INT32_MIN), label);
}

template <typename T>
void MacroAssembler::branchAddPtr(Condition cond, T src, Register dest,
                                  Label* label) {
  switch (cond) {
    case Overflow:
      ma_addPtrTestOverflow(dest, dest, src, label);
      break;
    case CarryClear:
    case CarrySet:
      ma_addPtrTestCarry(cond, dest, dest, src, label);
      break;
    case Signed:
    case NotSigned:
      ma_addPtrTestSigned(cond, dest, dest, src, label);
      break;
    default:
      MOZ_CRASH("NYI");
  }
}

template <typename T>
void MacroAssembler::branchSubPtr(Condition cond, T src, Register dest,
                                  Label* label) {
  switch (cond) {
    case Overflow:
      ma_subPtrTestOverflow(dest, dest, src, label);
      break;
    case NonZero:
    case Zero:
    case Signed:
    case NotSigned:
      subPtr(src, dest);
      ma_b(dest, dest, label, cond);
      break;
    default:
      MOZ_CRASH("NYI");
  }
}

void MacroAssembler::branchMulPtr(Condition cond, Register src, Register dest,
                                  Label* label) {
  MOZ_ASSERT(cond == Assembler::Overflow);
  ma_mulPtrTestOverflow(dest, dest, src, label);
}

void MacroAssembler::branchNegPtr(Condition cond, Register reg, Label* label) {
  MOZ_ASSERT(cond == Overflow);
  negPtr(reg);
  branchPtr(Assembler::Equal, reg, ImmWord(INTPTR_MIN), label);
}

void MacroAssembler::decBranchPtr(Condition cond, Register lhs, Imm32 rhs,
                                  Label* label) {
  subPtr(rhs, lhs);
  branchPtr(cond, lhs, Imm32(0), label);
}

void MacroAssembler::branchTest32(Condition cond, Register lhs, Register rhs,
                                  Label* label) {
  MOZ_ASSERT(cond == Zero || cond == NonZero || cond == Signed ||
             cond == NotSigned);
  if (lhs == rhs) {
    ma_b(lhs, rhs, label, cond);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    as_and(scratch, lhs, rhs);
    ma_b(scratch, scratch, label, cond);
  }
}

void MacroAssembler::branchTest32(Condition cond, Register lhs, Imm32 rhs,
                                  Label* label) {
  MOZ_ASSERT(cond == Zero || cond == NonZero || cond == Signed ||
             cond == NotSigned);
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  And32ForBranch(asMasm(), scratch, lhs, rhs,
                 !(cond == Zero || cond == NonZero));
  ma_b(scratch, scratch, label, cond);
}

void MacroAssembler::branchTest32(Condition cond, const Address& lhs, Imm32 rhs,
                                  Label* label) {
  MOZ_ASSERT(cond == Zero || cond == NonZero || cond == Signed ||
             cond == NotSigned);
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(lhs, scratch);
  And32ForBranch(asMasm(), scratch, scratch, rhs,
                 !(cond == Zero || cond == NonZero));
  ma_b(scratch, scratch, label, cond);
}

void MacroAssembler::branchTest32(Condition cond, const AbsoluteAddress& lhs,
                                  Imm32 rhs, Label* label) {
  MOZ_ASSERT(cond == Zero || cond == NonZero || cond == Signed ||
             cond == NotSigned);
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(lhs, scratch);
  And32ForBranch(asMasm(), scratch, scratch, rhs,
                 !(cond == Zero || cond == NonZero));
  ma_b(scratch, scratch, label, cond);
}

void MacroAssembler::branchTestPtr(Condition cond, Register lhs, Register rhs,
                                   Label* label) {
  MOZ_ASSERT(cond == Zero || cond == NonZero || cond == Signed ||
             cond == NotSigned);
  if (lhs == rhs) {
    ma_b(lhs, rhs, label, cond);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    as_and(scratch, lhs, rhs);
    ma_b(scratch, scratch, label, cond);
  }
}

void MacroAssembler::branchTestPtr(Condition cond, Register lhs, Imm32 rhs,
                                   Label* label) {
  MOZ_ASSERT(cond == Zero || cond == NonZero || cond == Signed ||
             cond == NotSigned);
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  And32ForBranch(asMasm(), scratch, lhs, rhs,
                 !(cond == Zero || cond == NonZero));
  ma_b(scratch, scratch, label, cond);
}

void MacroAssembler::branchTestPtr(Condition cond, Register lhs, ImmWord rhs,
                                   Label* label) {
  MOZ_ASSERT(cond == Zero || cond == NonZero || cond == Signed ||
             cond == NotSigned);
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();

  if (is_uintN(rhs.value, 12)) {
    as_andi(scratch, lhs, static_cast<int32_t>(rhs.value));
  } else if (const auto maybeRange = GetContiguousMaskRange(
                 mozilla::BitwiseCast<uint64_t>(rhs.value))) {
    const auto [msb, lsb] = *maybeRange;
    as_bstrpick_d(scratch, lhs, msb, lsb);
    if (!(cond == Zero || cond == NonZero) && lsb != 0) {
      // Only shift when the exact magnitude is interesting.
      as_slli_d(scratch, scratch, lsb);
    }
  } else {
    ma_li(scratch, rhs);
    as_and(scratch, lhs, scratch);
  }

  ma_b(scratch, scratch, label, cond);
}

void MacroAssembler::branchTestPtr(Condition cond, const Address& lhs,
                                   Imm32 rhs, Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(lhs, scratch);
  branchTestPtr(cond, scratch, rhs, label);
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

void MacroAssembler::branchTestUndefined(Condition cond, Register tag,
                                         Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  ma_b(tag, ImmTagSignExt(JSVAL_TAG_UNDEFINED), label, cond);
}

void MacroAssembler::branchTestUndefined(Condition cond,
                                         const ValueOperand& value,
                                         Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  splitSignExtTag(value, scratch);
  branchTestUndefined(cond, scratch, label);
}

void MacroAssembler::branchTestUndefined(Condition cond, const Address& address,
                                         Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestUndefined(cond, tag, label);
}

void MacroAssembler::branchTestUndefined(Condition cond,
                                         const BaseIndex& address,
                                         Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestUndefined(cond, tag, label);
}

void MacroAssembler::branchTestInt32(Condition cond, Register tag,
                                     Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  ma_b(tag, ImmTagSignExt(JSVAL_TAG_INT32), label, cond);
}

void MacroAssembler::branchTestInt32(Condition cond, const ValueOperand& value,
                                     Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  splitSignExtTag(value, scratch);
  branchTestInt32(cond, scratch, label);
}

void MacroAssembler::branchTestInt32(Condition cond, const Address& address,
                                     Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestInt32(cond, tag, label);
}

void MacroAssembler::branchTestInt32(Condition cond, const BaseIndex& address,
                                     Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestInt32(cond, tag, label);
}

void MacroAssembler::branchTestInt32Truthy(bool b, const ValueOperand& value,
                                           Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  as_bstrpick_d(scratch, value.valueReg(), 31, 0);
  ma_b(scratch, scratch, label, b ? NonZero : Zero);
}

void MacroAssembler::branchTestDouble(Condition cond, Register tag,
                                      Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  Condition actual = (cond == Equal) ? BelowOrEqual : Above;
  ma_b(tag, ImmTagSignExt(JSVAL_TAG_MAX_DOUBLE), label, actual);
}

void MacroAssembler::branchTestDouble(Condition cond, const ValueOperand& value,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  splitSignExtTag(value, scratch);
  branchTestDouble(cond, scratch, label);
}

void MacroAssembler::branchTestDouble(Condition cond, const Address& address,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestDouble(cond, tag, label);
}

void MacroAssembler::branchTestDouble(Condition cond, const BaseIndex& address,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestDouble(cond, tag, label);
}

void MacroAssembler::branchTestDoubleTruthy(bool b, FloatRegister value,
                                            Label* label) {
  ScratchDoubleScope fpscratch(*this);
  ma_lid(fpscratch, 0.0);
  DoubleCondition cond = b ? DoubleNotEqual : DoubleEqualOrUnordered;
  ma_bc_d(value, fpscratch, label, cond);
}

void MacroAssembler::branchTestNumber(Condition cond, Register tag,
                                      Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  Condition actual = cond == Equal ? BelowOrEqual : Above;
  ma_b(tag, ImmTagSignExt(JS::detail::ValueUpperInclNumberTag), label, actual);
}

void MacroAssembler::branchTestNumber(Condition cond, const ValueOperand& value,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  splitSignExtTag(value, scratch);
  branchTestNumber(cond, scratch, label);
}

void MacroAssembler::branchTestBoolean(Condition cond, Register tag,
                                       Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  ma_b(tag, ImmTagSignExt(JSVAL_TAG_BOOLEAN), label, cond);
}

void MacroAssembler::branchTestBoolean(Condition cond,
                                       const ValueOperand& value,
                                       Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  splitSignExtTag(value, scratch);
  branchTestBoolean(cond, scratch, label);
}

void MacroAssembler::branchTestBoolean(Condition cond, const Address& address,
                                       Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestBoolean(cond, tag, label);
}

void MacroAssembler::branchTestBoolean(Condition cond, const BaseIndex& address,
                                       Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestBoolean(cond, tag, label);
}

void MacroAssembler::branchTestBooleanTruthy(bool b, const ValueOperand& value,
                                             Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  unboxBoolean(value, scratch);
  ma_b(scratch, scratch, label, b ? NonZero : Zero);
}

void MacroAssembler::branchTestString(Condition cond, Register tag,
                                      Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  ma_b(tag, ImmTagSignExt(JSVAL_TAG_STRING), label, cond);
}

void MacroAssembler::branchTestString(Condition cond, const ValueOperand& value,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  splitSignExtTag(value, scratch);
  branchTestString(cond, scratch, label);
}

void MacroAssembler::branchTestString(Condition cond, const Address& address,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestString(cond, tag, label);
}

void MacroAssembler::branchTestString(Condition cond, const BaseIndex& address,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestString(cond, tag, label);
}

void MacroAssembler::branchTestStringTruthy(bool b, const ValueOperand& value,
                                            Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  unboxString(value, scratch);
  load32(Address(scratch, JSString::offsetOfLength()), scratch);
  ma_b(scratch, Imm32(0), label, b ? NotEqual : Equal);
}

void MacroAssembler::branchTestSymbol(Condition cond, Register tag,
                                      Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  ma_b(tag, ImmTagSignExt(JSVAL_TAG_SYMBOL), label, cond);
}

void MacroAssembler::branchTestSymbol(Condition cond, const ValueOperand& value,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  splitSignExtTag(value, scratch);
  branchTestSymbol(cond, scratch, label);
}

void MacroAssembler::branchTestSymbol(Condition cond, const BaseIndex& address,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestSymbol(cond, tag, label);
}

void MacroAssembler::branchTestSymbol(Condition cond, const Address& address,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestSymbol(cond, tag, label);
}

void MacroAssembler::branchTestBigInt(Condition cond, Register tag,
                                      Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  ma_b(tag, ImmTagSignExt(JSVAL_TAG_BIGINT), label, cond);
}

void MacroAssembler::branchTestBigInt(Condition cond, const ValueOperand& value,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  splitSignExtTag(value, scratch);
  branchTestBigInt(cond, scratch, label);
}

void MacroAssembler::branchTestBigInt(Condition cond, const Address& address,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestBigInt(cond, tag, label);
}

void MacroAssembler::branchTestBigInt(Condition cond, const BaseIndex& address,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  computeEffectiveAddress(address, scratch);
  splitSignExtTag(scratch, scratch);
  branchTestBigInt(cond, scratch, label);
}

void MacroAssembler::branchTestBigIntTruthy(bool b, const ValueOperand& value,
                                            Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  unboxBigInt(value, scratch);
  load32(Address(scratch, BigInt::offsetOfDigitLength()), scratch);
  ma_b(scratch, Imm32(0), label, b ? NotEqual : Equal);
}

void MacroAssembler::branchTestNull(Condition cond, Register tag,
                                    Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  ma_b(tag, ImmTagSignExt(JSVAL_TAG_NULL), label, cond);
}

void MacroAssembler::branchTestNull(Condition cond, const ValueOperand& value,
                                    Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  splitSignExtTag(value, scratch);
  branchTestNull(cond, scratch, label);
}

void MacroAssembler::branchTestNullOrUndefined(Condition cond, Register tag,
                                               Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  constexpr unsigned kShift = JS::detail::ValueNullOrUndefinedShift;
  constexpr int32_t kNegBits = static_cast<int32_t>(
      (UINT32_C(1) << (64 - kShift)) - JS::detail::ValueNullOrUndefinedBits);

  UseScratchRegisterScope temps(*this);
  const Register scratch = temps.Acquire();
  as_srai_d(scratch, tag, kShift - JSVAL_TAG_SHIFT);
  as_addi_d(scratch, scratch, kNegBits);
  ma_b(scratch, scratch, label, cond == Equal ? Zero : NonZero);
}

void MacroAssembler::branchTestNullOrUndefined(Condition cond,
                                               const ValueOperand& value,
                                               Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  constexpr unsigned kShift = JS::detail::ValueNullOrUndefinedShift;
  constexpr uint32_t kBits = JS::detail::ValueNullOrUndefinedBits;
  static_assert(kBits >> (64 - kShift - 1) == 1,
                "the shared bits must sign-extend to a negative value");
  constexpr int32_t kNegBits =
      static_cast<int32_t>((UINT32_C(1) << (64 - kShift)) - kBits);

  UseScratchRegisterScope temps(*this);
  const Register scratch = temps.Acquire();
  as_srai_d(scratch, value.valueReg(), kShift);
  as_addi_d(scratch, scratch, kNegBits);
  ma_b(scratch, scratch, label, cond == Equal ? Zero : NonZero);
}

void MacroAssembler::branchTestNull(Condition cond, const Address& address,
                                    Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestNull(cond, tag, label);
}

void MacroAssembler::branchTestNull(Condition cond, const BaseIndex& address,
                                    Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestNull(cond, tag, label);
}

void MacroAssembler::branchTestObject(Condition cond, Register tag,
                                      Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  ma_b(tag, ImmTagSignExt(JSVAL_TAG_OBJECT), label, cond);
}

void MacroAssembler::branchTestObject(Condition cond, const ValueOperand& value,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  splitSignExtTag(value, scratch);
  branchTestObject(cond, scratch, label);
}

void MacroAssembler::branchTestObject(Condition cond, const Address& address,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestObject(cond, tag, label);
}

void MacroAssembler::branchTestObject(Condition cond, const BaseIndex& address,
                                      Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestObject(cond, tag, label);
}

void MacroAssembler::branchTestPrimitive(Condition cond,
                                         const ValueOperand& value,
                                         Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  splitSignExtTag(value, scratch);
  branchTestPrimitive(cond, scratch, label);
}

void MacroAssembler::branchTestGCThing(Condition cond, Register tag,
                                       Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  ma_b(tag, ImmTagSignExt(JS::detail::ValueLowerInclGCThingTag), label,
       (cond == Equal) ? AboveOrEqual : Below);
}

void MacroAssembler::branchTestGCThing(Condition cond, const Address& address,
                                       Label* label) {
  branchTestGCThingImpl(cond, address, label);
}

void MacroAssembler::branchTestGCThing(Condition cond, const BaseIndex& address,
                                       Label* label) {
  branchTestGCThingImpl(cond, address, label);
}

void MacroAssembler::branchTestGCThing(Condition cond,
                                       const ValueOperand& address,
                                       Label* label) {
  branchTestGCThingImpl(cond, address, label);
}

template <typename T>
void MacroAssembler::branchTestGCThingImpl(Condition cond, const T& address,
                                           Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  ma_b(tag, ImmTagSignExt(JS::detail::ValueLowerInclGCThingTag), label,
       (cond == Equal) ? AboveOrEqual : Below);
}

void MacroAssembler::branchTestPrimitive(Condition cond, Register tag,
                                         Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  ma_b(tag, ImmTagSignExt(JSVAL_TAG_OBJECT), label,
       (cond == Equal) ? NotEqual : Equal);
}

void MacroAssembler::branchTestMagic(Condition cond, Register tag,
                                     Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  ma_b(tag, ImmTagSignExt(JSVAL_TAG_MAGIC), label, cond);
}

void MacroAssembler::branchTestMagic(Condition cond, const Address& address,
                                     Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestMagic(cond, tag, label);
}

void MacroAssembler::branchTestMagic(Condition cond, const BaseIndex& address,
                                     Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(address, scratch);
  branchTestMagic(cond, tag, label);
}

void MacroAssembler::branchTestMagic(Condition cond, const ValueOperand& value,
                                     Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  splitSignExtTag(value, scratch);
  ma_b(scratch, ImmTagSignExt(JSVAL_TAG_MAGIC), label, cond);
}

void MacroAssembler::branchTestMagic(Condition cond, const Address& valaddr,
                                     JSWhyMagic why, Label* label) {
  uint64_t magic = MagicValue(why).asRawBits();
  UseScratchRegisterScope temp(*this);
  Register scratch = temp.Acquire();
  loadPtr(valaddr, scratch);
  ma_b(scratch, ImmWord(magic), label, cond);
}

void MacroAssembler::branchTestMagic(Condition cond, const BaseIndex& valaddr,
                                     JSWhyMagic why, Label* label) {
  uint64_t magic = MagicValue(why).asRawBits();
  UseScratchRegisterScope temp(*this);
  Register scratch = temp.Acquire();
  loadPtr(valaddr, scratch);
  ma_b(scratch, ImmWord(magic), label, cond);
}

template <typename T>
void MacroAssembler::branchTestValue(Condition cond, const T& lhs,
                                     const ValueOperand& rhs, Label* label) {
  MOZ_ASSERT(cond == Assembler::Equal || cond == Assembler::NotEqual);
  branchPtr(cond, lhs, rhs.valueReg(), label);
}

template <typename T>
void MacroAssembler::testNumberSet(Condition cond, const T& src,
                                   Register dest) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(src, scratch);
  ma_cmp_set(dest, tag, ImmTagSignExt(JS::detail::ValueUpperInclNumberTag),
             cond == Equal ? BelowOrEqual : Above);
}

template <typename T>
void MacroAssembler::testBooleanSet(Condition cond, const T& src,
                                    Register dest) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(src, scratch);
  ma_cmp_set(dest, tag, ImmTagSignExt(JSVAL_TAG_BOOLEAN), cond);
}

template <typename T>
void MacroAssembler::testStringSet(Condition cond, const T& src,
                                   Register dest) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(src, scratch);
  ma_cmp_set(dest, tag, ImmTagSignExt(JSVAL_TAG_STRING), cond);
}

template <typename T>
void MacroAssembler::testSymbolSet(Condition cond, const T& src,
                                   Register dest) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(src, scratch);
  ma_cmp_set(dest, tag, ImmTagSignExt(JSVAL_TAG_SYMBOL), cond);
}

template <typename T>
void MacroAssembler::testBigIntSet(Condition cond, const T& src,
                                   Register dest) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register tag = extractTag(src, scratch);
  ma_cmp_set(dest, tag, ImmTagSignExt(JSVAL_TAG_BIGINT), cond);
}

void MacroAssembler::branchToComputedAddress(const BaseIndex& addr) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(addr, scratch);
  branch(scratch);
}

void MacroAssembler::cmp32Move32(Condition cond, Register lhs, Imm32 rhs,
                                 Register src, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  cmp32Set(cond, lhs, rhs, scratch);
  ma_cselnz(dest, src, dest, scratch, scratch);
}

void MacroAssembler::cmp32Move32(Condition cond, Register lhs, Register rhs,
                                 Register src, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  cmp32Set(cond, lhs, rhs, scratch);
  ma_cselnz(dest, src, dest, scratch, scratch);
}

void MacroAssembler::cmp32Move32(Condition cond, Register lhs,
                                 const Address& rhs, Register src,
                                 Register dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(lhs != scratch && src != scratch && dest != scratch);
  load32(rhs, scratch);
  cmp32Move32(cond, lhs, scratch, src, dest);
}

void MacroAssembler::cmp32MovePtr(Condition cond, Register lhs, Imm32 rhs,
                                  Register src, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  cmp32Set(cond, lhs, rhs, scratch);
  ma_cselnz(dest, src, dest, scratch, scratch);
}

void MacroAssembler::cmpPtrMovePtr(Condition cond, Register lhs, Imm32 rhs,
                                   Register src, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  cmpPtrSet(cond, lhs, rhs, scratch);
  ma_cselnz(dest, src, dest, scratch, scratch);
}

void MacroAssembler::cmpPtrMovePtr(Condition cond, Register lhs, Register rhs,
                                   Register src, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  cmpPtrSet(cond, lhs, rhs, scratch);
  ma_cselnz(dest, src, dest, scratch, scratch);
}

void MacroAssembler::cmpPtrMovePtr(Condition cond, Register lhs,
                                   const Address& rhs, Register src,
                                   Register dest) {
  MOZ_CRASH("NYI");
}

void MacroAssembler::cmp32Load32(Condition cond, Register lhs,
                                 const Address& rhs, const Address& src,
                                 Register dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(lhs != scratch && dest != scratch);
  load32(rhs, scratch);
  cmp32Load32(cond, lhs, scratch, src, dest);
}

void MacroAssembler::cmp32Load32(Condition cond, Register lhs, Register rhs,
                                 const Address& src, Register dest) {
  Label skip;
  branch32(Assembler::InvertCondition(cond), lhs, rhs, &skip);
  load32(src, dest);
  bind(&skip);
}

void MacroAssembler::cmp32Load32(Condition cond, Register lhs, Imm32 rhs,
                                 const Address& src, Register dest) {
  Label skip;
  branch32(Assembler::InvertCondition(cond), lhs, rhs, &skip);
  load32(src, dest);
  bind(&skip);
}

void MacroAssembler::cmp32LoadPtr(Condition cond, const Address& lhs, Imm32 rhs,
                                  const Address& src, Register dest) {
  Label skip;
  branch32(Assembler::InvertCondition(cond), lhs, rhs, &skip);
  loadPtr(src, dest);
  bind(&skip);
}

void MacroAssembler::test32LoadPtr(Condition cond, const Address& addr,
                                   Imm32 mask, const Address& src,
                                   Register dest) {
  MOZ_RELEASE_ASSERT(!JitOptions.spectreStringMitigations);
  Label skip;
  branchTest32(Assembler::InvertCondition(cond), addr, mask, &skip);
  loadPtr(src, dest);
  bind(&skip);
}

void MacroAssembler::test32MovePtr(Condition cond, Register operand, Imm32 mask,
                                   Register src, Register dest) {
  MOZ_ASSERT(cond == Assembler::Zero || cond == Assembler::NonZero);
  Label skip;
  branchTest32(Assembler::InvertCondition(cond), operand, mask, &skip);
  movePtr(src, dest);
  bind(&skip);
}

void MacroAssembler::test32MovePtr(Condition cond, const Address& addr,
                                   Imm32 mask, Register src, Register dest) {
  MOZ_ASSERT(cond == Assembler::Zero || cond == Assembler::NonZero);
  Label skip;
  branchTest32(Assembler::InvertCondition(cond), addr, mask, &skip);
  movePtr(src, dest);
  bind(&skip);
}

void MacroAssembler::spectreMovePtr(Condition cond, Register src,
                                    Register dest) {
  MOZ_CRASH();
}

void MacroAssembler::spectreZeroRegister(Condition cond, Register scratch,
                                         Register dest) {
  MOZ_CRASH();
}

void MacroAssembler::spectreBoundsCheck32(Register index, Register length,
                                          Register maybeScratch,
                                          Label* failure) {
  MOZ_RELEASE_ASSERT(!JitOptions.spectreIndexMasking);
  branch32(Assembler::BelowOrEqual, length, index, failure);
}

void MacroAssembler::spectreBoundsCheck32(Register index, const Address& length,
                                          Register maybeScratch,
                                          Label* failure) {
  MOZ_RELEASE_ASSERT(!JitOptions.spectreIndexMasking);
  branch32(Assembler::BelowOrEqual, length, index, failure);
}

void MacroAssembler::spectreBoundsCheckPtr(Register index, Register length,
                                           Register maybeScratch,
                                           Label* failure) {
  MOZ_RELEASE_ASSERT(!JitOptions.spectreIndexMasking);
  branchPtr(Assembler::BelowOrEqual, length, index, failure);
}

void MacroAssembler::spectreBoundsCheckPtr(Register index,
                                           const Address& length,
                                           Register maybeScratch,
                                           Label* failure) {
  MOZ_RELEASE_ASSERT(!JitOptions.spectreIndexMasking);
  branchPtr(Assembler::BelowOrEqual, length, index, failure);
}

// ========================================================================
// Memory access primitives.

FaultingCodeRange MacroAssembler::storeFloat32(FloatRegister src,
                                               const Address& addr) {
  return ma_fst_s(src, addr);
}
FaultingCodeRange MacroAssembler::storeFloat32(FloatRegister src,
                                               const BaseIndex& addr) {
  return ma_fst_s(src, addr);
}

FaultingCodeRange MacroAssembler::storeDouble(FloatRegister src,
                                              const Address& addr) {
  return ma_fst_d(src, addr);
}
FaultingCodeRange MacroAssembler::storeDouble(FloatRegister src,
                                              const BaseIndex& addr) {
  return ma_fst_d(src, addr);
}

FaultingCodeRange MacroAssembler::loadUnalignedSimd128(const Address& src,
                                                       FloatRegister dest) {
  return ma_vld(dest, src);
}

FaultingCodeRange MacroAssembler::storeUnalignedSimd128(FloatRegister src,
                                                        const Address& dest) {
  return ma_vst(src, dest);
}

FaultingCodeRange MacroAssembler::storeFloat16(FloatRegister src,
                                               const Address& dest, Register) {
  MOZ_CRASH("Not supported for this target");
}
FaultingCodeRange MacroAssembler::storeFloat16(FloatRegister src,
                                               const BaseIndex& dest,
                                               Register) {
  MOZ_CRASH("Not supported for this target");
}

void MacroAssembler::memoryBarrier(MemoryBarrier barrier) {
  if (!barrier.isNone()) {
    as_dbar(0);
  }
}

// ===============================================================
// Clamping functions.

void MacroAssembler::clampIntToUint8(Register reg) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  // If reg is < 0, then we want to clamp to 0.
  as_slti(scratch, reg, 0);
  as_masknez(reg, reg, scratch);

  // If reg is >= 255, then we want to clamp to 255.
  as_addi_d(reg, reg, -255);
  as_slt(scratch, reg, zero);
  as_maskeqz(reg, reg, scratch);
  as_addi_d(reg, reg, 255);
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
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(src.valueReg() != scratch);
  mov(ImmShiftedTag(type), scratch);
  as_xor(dest, src.valueReg(), scratch);
  as_srli_d(scratch, dest, JSVAL_TAG_SHIFT);
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
    as_add_d(output, lhsLo, rhsLo);   // output = lhsLo + rhsLo
    as_sltu(output, output, lhsLo);   // output = carry from `lhsLo + rhsLo`
    as_add_d(output, output, lhsHi);  // output = carry + lhsHi
    as_add_d(output, output, rhsHi);  // output = carry + lhsHi + rhsHi
  } else {
    as_sltu(output, lhsLo, rhsLo);    // output = borrow from `lhsLo - rhsLo`
    as_sub_d(output, lhsHi, output);  // output = lhsHi - borrow
    as_sub_d(output, output, rhsHi);  // output = lhsHi - borrow - rhsHi
  }
}

void MacroAssembler::wasmMulI64WideHI64(Register lhs, Register rhs,
                                        Register output, bool isSigned) {
  if (isSigned) {
    as_mulh_d(output, lhs, rhs);
  } else {
    as_mulh_du(output, lhs, rhs);
  }
}

void MacroAssembler::loadConstantSimd128(const SimdConstant& v,
                                         FloatRegister dest) {
  if (const std::optional<uint16_t> imm13 =
          AssemblerLOONG64::EncodeVldiImmediate(v)) {
    as_vldi(dest, static_cast<int32_t>(SignExtend64<13>(*imm13)));
    return;
  }

  const SimdConstant c =
      SimdConstant::CreateX2(reinterpret_cast<const int64_t*>(v.bytes()));
  const SimdConstant::I64x2& lanes = c.asInt64x2();

  UseScratchRegisterScope temps(*this);
  const Register scratch = temps.Acquire();
  ma_li(scratch, ImmWord(static_cast<uint64_t>(lanes[0])));
  as_vreplgr2vr_d(dest, scratch);
  if (lanes[0] != lanes[1]) {
    ma_li(scratch, ImmWord(static_cast<uint64_t>(lanes[1])));
    as_vinsgr2vr_d(dest, scratch, 1);
  }
}

void MacroAssembler::splatX16(Register src, FloatRegister dest) {
  as_vreplgr2vr_b(dest, src);
}

void MacroAssembler::splatX16(uint32_t srcLane, FloatRegister src,
                              FloatRegister dest) {
  as_vreplvei_b(dest, src, srcLane);
}

void MacroAssembler::splatX8(Register src, FloatRegister dest) {
  as_vreplgr2vr_h(dest, src);
}

void MacroAssembler::splatX8(uint32_t srcLane, FloatRegister src,
                             FloatRegister dest) {
  as_vreplvei_h(dest, src, srcLane);
}

void MacroAssembler::splatX4(Register src, FloatRegister dest) {
  as_vreplgr2vr_w(dest, src);
}

void MacroAssembler::splatX4(FloatRegister src, FloatRegister dest) {
  // Splat |src[0]| to all 4 lanes in |dest|.
  as_vreplvei_w(dest, src, 0);
}

void MacroAssembler::splatX2(FloatRegister src, FloatRegister dest) {
  // Splat |src[0]| to all 2 lanes in |dest|.
  as_vreplvei_d(dest, src, 0);
}

void MacroAssembler::extractLaneInt8x16(uint32_t lane, FloatRegister src,
                                        Register dest) {
  as_vpickve2gr_b(dest, src, lane);
}

void MacroAssembler::unsignedExtractLaneInt8x16(uint32_t lane,
                                                FloatRegister src,
                                                Register dest) {
  as_vpickve2gr_bu(dest, src, lane);
}

void MacroAssembler::extractLaneInt16x8(uint32_t lane, FloatRegister src,
                                        Register dest) {
  as_vpickve2gr_h(dest, src, lane);
}

void MacroAssembler::unsignedExtractLaneInt16x8(uint32_t lane,
                                                FloatRegister src,
                                                Register dest) {
  as_vpickve2gr_hu(dest, src, lane);
}

void MacroAssembler::extractLaneInt32x4(uint32_t lane, FloatRegister src,
                                        Register dest) {
  as_vpickve2gr_wu(dest, src, lane);
}

void MacroAssembler::extractLaneFloat32x4(uint32_t lane, FloatRegister src,
                                          FloatRegister dest) {
  as_vreplvei_w(dest, src, lane);
}

void MacroAssembler::extractLaneFloat64x2(uint32_t lane, FloatRegister src,
                                          FloatRegister dest) {
  as_vreplvei_d(dest, src, lane);
}

void MacroAssembler::replaceLaneInt8x16(unsigned lane, Register rhs,
                                        FloatRegister lhsDest) {
  as_vinsgr2vr_b(lhsDest, rhs, lane);
}

void MacroAssembler::replaceLaneInt16x8(unsigned lane, Register rhs,
                                        FloatRegister lhsDest) {
  as_vinsgr2vr_h(lhsDest, rhs, lane);
}

void MacroAssembler::replaceLaneInt32x4(unsigned lane, Register rhs,
                                        FloatRegister lhsDest) {
  as_vinsgr2vr_w(lhsDest, rhs, lane);
}

void MacroAssembler::replaceLaneFloat32x4(unsigned lane, FloatRegister rhs,
                                          FloatRegister lhsDest) {
  // The upper 4 bits selects which lane in |lhsDest| gets replaced, and the
  // lower 4 bits selects which lane from |rhs| to replace.
  // <https://jia.je/unofficial-loongarch-intrinsics-guide/lsx/misc/#__m128i-__lsx_vextrins_w-__m128i-a-__m128i-b-imm0_255-imm>
  as_vextrins_w(lhsDest, rhs, lane << 4);
}

void MacroAssembler::replaceLaneFloat64x2(unsigned lane, FloatRegister rhs,
                                          FloatRegister lhsDest) {
  // Ditto.
  // <https://jia.je/unofficial-loongarch-intrinsics-guide/lsx/misc/#__m128i-__lsx_vextrins_d-__m128i-a-__m128i-b-imm0_255-imm>
  as_vextrins_d(lhsDest, rhs, lane << 4);
}

void MacroAssembler::shuffleInt8x16(const uint8_t lanes[16], FloatRegister rhs,
                                    FloatRegister lhsDest) {
  shuffleInt8x16(lanes, lhsDest, rhs, lhsDest);
}

void MacroAssembler::shuffleInt8x16(const uint8_t lanes[16], FloatRegister lhs,
                                    FloatRegister rhs, FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  loadConstantSimd128(
      SimdConstant::CreateX16(reinterpret_cast<const int8_t*>(lanes)), scratch);
  as_vshuf_b(dest, rhs, lhs, scratch);
}

void MacroAssembler::blendInt8x16(const uint8_t lanes[16], FloatRegister lhs,
                                  FloatRegister rhs, FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  loadConstantSimd128(
      SimdConstant::CreateX16(reinterpret_cast<const int8_t*>(lanes)), scratch);
  as_vbitsel_v(dest, lhs, rhs, scratch);
}

void MacroAssembler::blendInt16x8(const uint16_t lanes[8], FloatRegister lhs,
                                  FloatRegister rhs, FloatRegister dest) {
  std::array<int8_t, 16> mask;
  for (int i = 0; i < 8; i++) {
    mask[2 * i] = mask[2 * i + 1] = lanes[i] ? 0xFF : 0;
  }
  ScratchSimd128Scope scratch(*this);
  loadConstantSimd128(SimdConstant::CreateX16(mask.data()), scratch);
  as_vbitsel_v(dest, lhs, rhs, scratch);
}

void MacroAssembler::laneSelectSimd128(FloatRegister mask, FloatRegister lhs,
                                       FloatRegister rhs, FloatRegister dest) {
  as_vbitsel_v(dest, rhs, lhs, mask);
}

void MacroAssembler::interleaveHighInt8x16(FloatRegister lhs, FloatRegister rhs,
                                           FloatRegister dest) {
  as_vilvh_b(dest, rhs, lhs);
}

void MacroAssembler::interleaveHighInt16x8(FloatRegister lhs, FloatRegister rhs,
                                           FloatRegister dest) {
  as_vilvh_h(dest, rhs, lhs);
}

void MacroAssembler::interleaveHighInt32x4(FloatRegister lhs, FloatRegister rhs,
                                           FloatRegister dest) {
  as_vilvh_w(dest, rhs, lhs);
}

void MacroAssembler::interleaveHighInt64x2(FloatRegister lhs, FloatRegister rhs,
                                           FloatRegister dest) {
  as_vilvh_d(dest, rhs, lhs);
}

void MacroAssembler::interleaveLowInt8x16(FloatRegister lhs, FloatRegister rhs,
                                          FloatRegister dest) {
  as_vilvl_b(dest, rhs, lhs);
}

void MacroAssembler::interleaveLowInt16x8(FloatRegister lhs, FloatRegister rhs,
                                          FloatRegister dest) {
  as_vilvl_h(dest, rhs, lhs);
}

void MacroAssembler::interleaveLowInt32x4(FloatRegister lhs, FloatRegister rhs,
                                          FloatRegister dest) {
  as_vilvl_w(dest, rhs, lhs);
}

void MacroAssembler::interleaveLowInt64x2(FloatRegister lhs, FloatRegister rhs,
                                          FloatRegister dest) {
  as_vilvl_d(dest, rhs, lhs);
}

void MacroAssembler::permuteInt8x16(const uint8_t lanes[16], FloatRegister src,
                                    FloatRegister dest) {
#ifdef DEBUG
  for (int i = 0; i < 16; i++) {
    MOZ_ASSERT(lanes[i] <= 15);
  }
#endif
  ScratchSimd128Scope scratch(*this);
  loadConstantSimd128(
      SimdConstant::CreateX16(reinterpret_cast<const int8_t*>(lanes)), scratch);
  as_vshuf_b(dest, src, src, scratch);
}

void MacroAssembler::permuteInt16x8(const uint16_t lanes[8], FloatRegister src,
                                    FloatRegister dest) {
#ifdef DEBUG
  for (int i = 0; i < 8; i++) {
    MOZ_ASSERT(lanes[i] <= 7);
  }
#endif

  bool symmetric = true;
  for (int i = 0; i < 4; i++) {
    if (lanes[i] != lanes[i + 4] - 4) {
      symmetric = false;
      break;
    }
  }
  if (symmetric) {
    // vshuf4i.h applies the same 2-bit-per-lane selector to both 64-bit halves,
    // so it can only express permutations that repeat across halves.
    // <https://jia.je/unofficial-loongarch-intrinsics-guide/lsx/shuffling/#__m128i-__lsx_vshuf4i_h-__m128i-a-imm0_255-imm>
#ifdef DEBUG
    for (int i = 0; i < 4; i++) {
      MOZ_ASSERT(lanes[i] <= 3);
    }
#endif
    const uint8_t imm =
        lanes[0] | (lanes[1] << 2) | (lanes[2] << 4) | (lanes[3] << 6);
    as_vshuf4i_h(dest, src, imm);
    return;
  }

  std::array<uint8_t, 16> lanes_;
  for (int i = 0; i < 8; i++) {
    for (int j = 0; j < 2; j++) {
      lanes_[2 * i + j] = (lanes[i] << 1) + j;
    }
  }
  permuteInt8x16(lanes_.data(), src, dest);
}

void MacroAssembler::permuteInt32x4(const uint32_t lanes[4], FloatRegister src,
                                    FloatRegister dest) {
#ifdef DEBUG
  for (int i = 0; i < 4; i++) {
    MOZ_ASSERT(lanes[i] <= 3);
  }
#endif
  const uint8_t imm =
      lanes[0] | (lanes[1] << 2) | (lanes[2] << 4) | (lanes[3] << 6);
  as_vshuf4i_w(dest, src, imm);
}

void MacroAssembler::concatAndRightShiftSimd128(FloatRegister lhs,
                                                FloatRegister rhs,
                                                FloatRegister dest,
                                                uint32_t shift) {
  ScratchSimd128Scope scratch(*this);
  as_vbsll_v(scratch, lhs, 16 - shift);
  as_vbsrl_v(dest, rhs, shift);
  as_vor_v(dest, dest, scratch);
}

void MacroAssembler::rotateRightSimd128(FloatRegister src, FloatRegister dest,
                                        uint32_t shift) {
  concatAndRightShiftSimd128(src, src, dest, shift);
}

void MacroAssembler::leftShiftSimd128(Imm32 count, FloatRegister src,
                                      FloatRegister dest) {
  as_vbsll_v(dest, src, count.value);
}

void MacroAssembler::rightShiftSimd128(Imm32 count, FloatRegister src,
                                       FloatRegister dest) {
  as_vbsrl_v(dest, src, count.value);
}

void MacroAssembler::zeroExtend8x16To16x8(FloatRegister src,
                                          FloatRegister dest) {
  unsignedWidenLowInt8x16(src, dest);
}

void MacroAssembler::zeroExtend8x16To32x4(FloatRegister src,
                                          FloatRegister dest) {
  unsignedWidenLowInt8x16(src, dest);
  unsignedWidenLowInt16x8(dest, dest);
}

void MacroAssembler::zeroExtend8x16To64x2(FloatRegister src,
                                          FloatRegister dest) {
  unsignedWidenLowInt8x16(src, dest);
  unsignedWidenLowInt16x8(dest, dest);
  unsignedWidenLowInt32x4(dest, dest);
}

void MacroAssembler::zeroExtend16x8To32x4(FloatRegister src,
                                          FloatRegister dest) {
  unsignedWidenLowInt16x8(src, dest);
}

void MacroAssembler::zeroExtend16x8To64x2(FloatRegister src,
                                          FloatRegister dest) {
  unsignedWidenLowInt16x8(src, dest);
  unsignedWidenLowInt32x4(dest, dest);
}

void MacroAssembler::zeroExtend32x4To64x2(FloatRegister src,
                                          FloatRegister dest) {
  unsignedWidenLowInt32x4(src, dest);
}

void MacroAssembler::reverseInt16x8(FloatRegister src, FloatRegister dest) {
  as_vshuf4i_b(dest, src, 0b10110001);
}

void MacroAssembler::reverseInt32x4(FloatRegister src, FloatRegister dest) {
  as_vshuf4i_b(dest, src, 0b00011011);
}

void MacroAssembler::reverseInt64x2(FloatRegister src, FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vshuf4i_w(scratch, src, 0b10110001);
  as_vshuf4i_b(dest, scratch, 0b00011011);
}

void MacroAssembler::swizzleInt8x16(FloatRegister lhs, FloatRegister rhs,
                                    FloatRegister dest) {
  // vshuf.b takes lane indices modulo 32 (and on some cores zeroes only indices
  // >= 64), so lanes >= 16 could select garbage from vj rather than zero. The
  // mask enforces the zeroing required by the swizzle semantics.
  // <https://jia.je/unofficial-loongarch-intrinsics-guide/lsx/shuffling/#__m128i-__lsx_vshuf_b-__m128i-a-__m128i-b-__m128i-c>
  ScratchSimd128Scope scratch(*this);
  as_vslei_bu(scratch, rhs, 15);
  as_vshuf_b(dest, lhs, lhs, rhs);
  as_vand_v(dest, dest, scratch);
}

void MacroAssembler::swizzleInt8x16Relaxed(FloatRegister lhs, FloatRegister rhs,
                                           FloatRegister dest) {
  swizzleInt8x16(lhs, rhs, dest);
}

void MacroAssembler::addInt8x16(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vadd_b(dest, lhs, rhs);
}

void MacroAssembler::addInt16x8(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vadd_h(dest, lhs, rhs);
}

void MacroAssembler::addInt32x4(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vadd_w(dest, lhs, rhs);
}

void MacroAssembler::addInt64x2(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vadd_d(dest, lhs, rhs);
}

void MacroAssembler::subInt8x16(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vsub_b(dest, lhs, rhs);
}

void MacroAssembler::subInt16x8(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vsub_h(dest, lhs, rhs);
}

void MacroAssembler::subInt32x4(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vsub_w(dest, lhs, rhs);
}

void MacroAssembler::subInt64x2(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vsub_d(dest, lhs, rhs);
}

void MacroAssembler::mulInt16x8(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vmul_h(dest, lhs, rhs);
}

void MacroAssembler::mulInt32x4(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vmul_w(dest, lhs, rhs);
}

void MacroAssembler::extMulLowInt8x16(FloatRegister lhs, FloatRegister rhs,
                                      FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmulwod_h_b(scratch, lhs, rhs);
  as_vmulwev_h_b(dest, lhs, rhs);
  as_vilvl_h(dest, scratch, dest);
}

void MacroAssembler::extMulHighInt8x16(FloatRegister lhs, FloatRegister rhs,
                                       FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmulwod_h_b(scratch, lhs, rhs);
  as_vmulwev_h_b(dest, lhs, rhs);
  as_vilvh_h(dest, scratch, dest);
}

void MacroAssembler::unsignedExtMulLowInt8x16(FloatRegister lhs,
                                              FloatRegister rhs,
                                              FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmulwod_h_bu(scratch, lhs, rhs);
  as_vmulwev_h_bu(dest, lhs, rhs);
  as_vilvl_h(dest, scratch, dest);
}

void MacroAssembler::unsignedExtMulHighInt8x16(FloatRegister lhs,
                                               FloatRegister rhs,
                                               FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmulwod_h_bu(scratch, lhs, rhs);
  as_vmulwev_h_bu(dest, lhs, rhs);
  as_vilvh_h(dest, scratch, dest);
}

void MacroAssembler::extMulLowInt16x8(FloatRegister lhs, FloatRegister rhs,
                                      FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmulwod_w_h(scratch, lhs, rhs);
  as_vmulwev_w_h(dest, lhs, rhs);
  as_vilvl_w(dest, scratch, dest);
}

void MacroAssembler::extMulHighInt16x8(FloatRegister lhs, FloatRegister rhs,
                                       FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmulwod_w_h(scratch, lhs, rhs);
  as_vmulwev_w_h(dest, lhs, rhs);
  as_vilvh_w(dest, scratch, dest);
}

void MacroAssembler::unsignedExtMulLowInt16x8(FloatRegister lhs,
                                              FloatRegister rhs,
                                              FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmulwod_w_hu(scratch, lhs, rhs);
  as_vmulwev_w_hu(dest, lhs, rhs);
  as_vilvl_w(dest, scratch, dest);
}

void MacroAssembler::unsignedExtMulHighInt16x8(FloatRegister lhs,
                                               FloatRegister rhs,
                                               FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmulwod_w_hu(scratch, lhs, rhs);
  as_vmulwev_w_hu(dest, lhs, rhs);
  as_vilvh_w(dest, scratch, dest);
}

void MacroAssembler::extMulLowInt32x4(FloatRegister lhs, FloatRegister rhs,
                                      FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmulwod_d_w(scratch, lhs, rhs);
  as_vmulwev_d_w(dest, lhs, rhs);
  as_vilvl_d(dest, scratch, dest);
}

void MacroAssembler::extMulHighInt32x4(FloatRegister lhs, FloatRegister rhs,
                                       FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmulwod_d_w(scratch, lhs, rhs);
  as_vmulwev_d_w(dest, lhs, rhs);
  as_vilvh_d(dest, scratch, dest);
}

void MacroAssembler::unsignedExtMulLowInt32x4(FloatRegister lhs,
                                              FloatRegister rhs,
                                              FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmulwod_d_wu(scratch, lhs, rhs);
  as_vmulwev_d_wu(dest, lhs, rhs);
  as_vilvl_d(dest, scratch, dest);
}

void MacroAssembler::unsignedExtMulHighInt32x4(FloatRegister lhs,
                                               FloatRegister rhs,
                                               FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmulwod_d_wu(scratch, lhs, rhs);
  as_vmulwev_d_wu(dest, lhs, rhs);
  as_vilvh_d(dest, scratch, dest);
}

void MacroAssembler::q15MulrSatInt16x8(FloatRegister lhs, FloatRegister rhs,
                                       FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  ScratchSimd128Scope2 scratch2(*this);
  loadConstantSimd128(SimdConstant::SplatX4(0x4000), scratch);
  as_vmaddwod_w_h(scratch, lhs, rhs);
  loadConstantSimd128(SimdConstant::SplatX4(0x4000), scratch2);
  as_vmaddwev_w_h(scratch2, lhs, rhs);
  as_vssrani_h_w(scratch, scratch, 15);
  as_vssrani_h_w(scratch2, scratch2, 15);
  as_vilvl_h(dest, scratch, scratch2);
}

void MacroAssembler::negInt8x16(FloatRegister src, FloatRegister dest) {
  as_vneg_b(dest, src);
}

void MacroAssembler::negInt16x8(FloatRegister src, FloatRegister dest) {
  as_vneg_h(dest, src);
}

void MacroAssembler::negInt32x4(FloatRegister src, FloatRegister dest) {
  as_vneg_w(dest, src);
}

void MacroAssembler::negInt64x2(FloatRegister src, FloatRegister dest) {
  as_vneg_d(dest, src);
}

void MacroAssembler::addSatInt8x16(FloatRegister lhs, FloatRegister rhs,
                                   FloatRegister dest) {
  as_vsadd_b(dest, lhs, rhs);
}

void MacroAssembler::unsignedAddSatInt8x16(FloatRegister lhs, FloatRegister rhs,
                                           FloatRegister dest) {
  as_vsadd_bu(dest, lhs, rhs);
}

void MacroAssembler::addSatInt16x8(FloatRegister lhs, FloatRegister rhs,
                                   FloatRegister dest) {
  as_vsadd_h(dest, lhs, rhs);
}

void MacroAssembler::unsignedAddSatInt16x8(FloatRegister lhs, FloatRegister rhs,
                                           FloatRegister dest) {
  as_vsadd_hu(dest, lhs, rhs);
}

void MacroAssembler::subSatInt8x16(FloatRegister lhs, FloatRegister rhs,
                                   FloatRegister dest) {
  as_vssub_b(dest, lhs, rhs);
}

void MacroAssembler::unsignedSubSatInt8x16(FloatRegister lhs, FloatRegister rhs,
                                           FloatRegister dest) {
  as_vssub_bu(dest, lhs, rhs);
}

void MacroAssembler::subSatInt16x8(FloatRegister lhs, FloatRegister rhs,
                                   FloatRegister dest) {
  as_vssub_h(dest, lhs, rhs);
}

void MacroAssembler::unsignedSubSatInt16x8(FloatRegister lhs, FloatRegister rhs,
                                           FloatRegister dest) {
  as_vssub_hu(dest, lhs, rhs);
}

void MacroAssembler::minInt8x16(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vmin_b(dest, lhs, rhs);
}

void MacroAssembler::unsignedMinInt8x16(FloatRegister lhs, FloatRegister rhs,
                                        FloatRegister dest) {
  as_vmin_bu(dest, lhs, rhs);
}

void MacroAssembler::minInt16x8(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vmin_h(dest, lhs, rhs);
}

void MacroAssembler::unsignedMinInt16x8(FloatRegister lhs, FloatRegister rhs,
                                        FloatRegister dest) {
  as_vmin_hu(dest, lhs, rhs);
}

void MacroAssembler::minInt32x4(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vmin_w(dest, lhs, rhs);
}

void MacroAssembler::unsignedMinInt32x4(FloatRegister lhs, FloatRegister rhs,
                                        FloatRegister dest) {
  as_vmin_wu(dest, lhs, rhs);
}

void MacroAssembler::maxInt8x16(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vmax_b(dest, lhs, rhs);
}

void MacroAssembler::unsignedMaxInt8x16(FloatRegister lhs, FloatRegister rhs,
                                        FloatRegister dest) {
  as_vmax_bu(dest, lhs, rhs);
}

void MacroAssembler::maxInt16x8(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vmax_h(dest, lhs, rhs);
}

void MacroAssembler::unsignedMaxInt16x8(FloatRegister lhs, FloatRegister rhs,
                                        FloatRegister dest) {
  as_vmax_hu(dest, lhs, rhs);
}

void MacroAssembler::maxInt32x4(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vmax_w(dest, lhs, rhs);
}

void MacroAssembler::unsignedMaxInt32x4(FloatRegister lhs, FloatRegister rhs,
                                        FloatRegister dest) {
  as_vmax_wu(dest, lhs, rhs);
}

void MacroAssembler::unsignedAverageInt8x16(FloatRegister lhs,
                                            FloatRegister rhs,
                                            FloatRegister dest) {
  as_vavgr_bu(dest, lhs, rhs);
}

void MacroAssembler::unsignedAverageInt16x8(FloatRegister lhs,
                                            FloatRegister rhs,
                                            FloatRegister dest) {
  as_vavgr_hu(dest, lhs, rhs);
}

void MacroAssembler::absInt8x16(FloatRegister src, FloatRegister dest) {
  as_vsigncov_b(dest, src, src);
}

void MacroAssembler::absInt16x8(FloatRegister src, FloatRegister dest) {
  as_vsigncov_h(dest, src, src);
}

void MacroAssembler::absInt32x4(FloatRegister src, FloatRegister dest) {
  as_vsigncov_w(dest, src, src);
}

void MacroAssembler::absInt64x2(FloatRegister src, FloatRegister dest) {
  as_vsigncov_d(dest, src, src);
}

void MacroAssembler::leftShiftInt8x16(Imm32 count, FloatRegister src,
                                      FloatRegister dest) {
  as_vslli_b(dest, src, count.value);
}

void MacroAssembler::leftShiftInt16x8(Imm32 count, FloatRegister src,
                                      FloatRegister dest) {
  as_vslli_h(dest, src, count.value);
}

void MacroAssembler::leftShiftInt32x4(Imm32 count, FloatRegister src,
                                      FloatRegister dest) {
  as_vslli_w(dest, src, count.value);
}

void MacroAssembler::leftShiftInt64x2(Imm32 count, FloatRegister src,
                                      FloatRegister dest) {
  as_vslli_d(dest, src, count.value);
}

void MacroAssembler::rightShiftInt8x16(Imm32 count, FloatRegister src,
                                       FloatRegister dest) {
  as_vsrai_b(dest, src, count.value);
}

void MacroAssembler::unsignedRightShiftInt8x16(Imm32 count, FloatRegister src,
                                               FloatRegister dest) {
  as_vsrli_b(dest, src, count.value);
}

void MacroAssembler::rightShiftInt16x8(Imm32 count, FloatRegister src,
                                       FloatRegister dest) {
  as_vsrai_h(dest, src, count.value);
}

void MacroAssembler::unsignedRightShiftInt16x8(Imm32 count, FloatRegister src,
                                               FloatRegister dest) {
  as_vsrli_h(dest, src, count.value);
}

void MacroAssembler::rightShiftInt32x4(Imm32 count, FloatRegister src,
                                       FloatRegister dest) {
  as_vsrai_w(dest, src, count.value);
}

void MacroAssembler::unsignedRightShiftInt32x4(Imm32 count, FloatRegister src,
                                               FloatRegister dest) {
  as_vsrli_w(dest, src, count.value);
}

void MacroAssembler::rightShiftInt64x2(Imm32 count, FloatRegister src,
                                       FloatRegister dest) {
  as_vsrai_d(dest, src, count.value);
}

void MacroAssembler::unsignedRightShiftInt64x2(Imm32 count, FloatRegister src,
                                               FloatRegister dest) {
  as_vsrli_d(dest, src, count.value);
}

void MacroAssembler::bitwiseAndSimd128(FloatRegister rhs,
                                       FloatRegister lhsDest) {
  as_vand_v(lhsDest, lhsDest, rhs);
}

void MacroAssembler::bitwiseAndSimd128(FloatRegister lhs, FloatRegister rhs,
                                       FloatRegister dest) {
  as_vand_v(dest, lhs, rhs);
}

void MacroAssembler::bitwiseOrSimd128(FloatRegister rhs,
                                      FloatRegister lhsDest) {
  as_vor_v(lhsDest, lhsDest, rhs);
}

void MacroAssembler::bitwiseOrSimd128(FloatRegister lhs, FloatRegister rhs,
                                      FloatRegister dest) {
  as_vor_v(dest, lhs, rhs);
}

void MacroAssembler::bitwiseXorSimd128(FloatRegister rhs,
                                       FloatRegister lhsDest) {
  as_vxor_v(lhsDest, lhsDest, rhs);
}

void MacroAssembler::bitwiseXorSimd128(FloatRegister lhs, FloatRegister rhs,
                                       FloatRegister dest) {
  as_vxor_v(dest, lhs, rhs);
}

void MacroAssembler::bitwiseNotSimd128(FloatRegister src, FloatRegister dest) {
  as_vnor_v(dest, src, src);
}

void MacroAssembler::bitwiseAndNotSimd128(FloatRegister lhs, FloatRegister rhs,
                                          FloatRegister lhsDest) {
  as_vandn_v(lhsDest, rhs, lhs);
}

void MacroAssembler::bitwiseNotAndSimd128(FloatRegister rhs,
                                          FloatRegister lhsDest) {
  as_vandn_v(lhsDest, lhsDest, rhs);
}

void MacroAssembler::anyTrueSimd128(FloatRegister src, Register dest) {
  as_vsetnez_v(FCC0, src);
  as_movcf2gr(dest, FCC0);
}

void MacroAssembler::allTrueInt8x16(FloatRegister src, Register dest) {
  as_vsetallnez_b(FCC0, src);
  as_movcf2gr(dest, FCC0);
}

void MacroAssembler::allTrueInt16x8(FloatRegister src, Register dest) {
  as_vsetallnez_h(FCC0, src);
  as_movcf2gr(dest, FCC0);
}

void MacroAssembler::allTrueInt32x4(FloatRegister src, Register dest) {
  as_vsetallnez_w(FCC0, src);
  as_movcf2gr(dest, FCC0);
}

void MacroAssembler::allTrueInt64x2(FloatRegister src, Register dest) {
  as_vsetallnez_d(FCC0, src);
  as_movcf2gr(dest, FCC0);
}

void MacroAssembler::compareInt8x16(Assembler::Condition cond,
                                    FloatRegister rhs, FloatRegister lhsDest) {
  compareInt8x16(cond, lhsDest, rhs, lhsDest);
}

void MacroAssembler::compareInt8x16(Assembler::Condition cond,
                                    FloatRegister lhs, FloatRegister rhs,
                                    FloatRegister dest) {
  switch (cond) {
    case Assembler::Equal:
      as_vseq_b(dest, lhs, rhs);
      break;
    case Assembler::NotEqual:
      as_vseq_b(dest, lhs, rhs);
      as_vnor_v(dest, dest, dest);
      break;
    case Assembler::LessThan:
      as_vslt_b(dest, lhs, rhs);
      break;
    case Assembler::LessThanOrEqual:
      as_vsle_b(dest, lhs, rhs);
      break;
    case Assembler::GreaterThan:
      as_vslt_b(dest, rhs, lhs);
      break;
    case Assembler::GreaterThanOrEqual:
      as_vsle_b(dest, rhs, lhs);
      break;
    case Assembler::Below:
      as_vslt_bu(dest, lhs, rhs);
      break;
    case Assembler::BelowOrEqual:
      as_vsle_bu(dest, lhs, rhs);
      break;
    case Assembler::Above:
      as_vslt_bu(dest, rhs, lhs);
      break;
    case Assembler::AboveOrEqual:
      as_vsle_bu(dest, rhs, lhs);
      break;
    default:
      MOZ_CRASH("unexpected condition");
  }
}

void MacroAssembler::compareInt8x16(Assembler::Condition cond,
                                    FloatRegister lhs, const SimdConstant& rhs,
                                    FloatRegister dest) {
  if (std::optional<int64_t> imm =
          AssemblerLOONG64::EncodeSimdCompareImmediate<int8_t>(rhs, cond)) {
    switch (cond) {
      case Assembler::Equal:
        as_vseqi_b(dest, lhs, *imm);
        break;
      case Assembler::NotEqual:
        as_vseqi_b(dest, lhs, *imm);
        as_vnor_v(dest, dest, dest);
        break;
      case Assembler::LessThan:
        as_vslti_b(dest, lhs, *imm);
        break;
      case Assembler::LessThanOrEqual:
        as_vslei_b(dest, lhs, *imm);
        break;
      case Assembler::GreaterThan:
        as_vslei_b(dest, lhs, *imm);
        as_vnor_v(dest, dest, dest);
        break;
      case Assembler::GreaterThanOrEqual:
        as_vslti_b(dest, lhs, *imm);
        as_vnor_v(dest, dest, dest);
        break;
      case Assembler::Below:
        if (*imm == 0) {
          // x <u 0 is always false.
          loadConstantSimd128(SimdConstant::SplatX16(0), dest);
        } else {
          as_vslti_bu(dest, lhs, *imm);
        }
        break;
      case Assembler::BelowOrEqual:
        as_vslei_bu(dest, lhs, *imm);
        break;
      case Assembler::Above:
        as_vslei_bu(dest, lhs, *imm);
        as_vnor_v(dest, dest, dest);
        break;
      case Assembler::AboveOrEqual:
        if (*imm == 0) {
          // x >=u 0 is always true.
          loadConstantSimd128(SimdConstant::SplatX16(
                                  mozilla::BitwiseCast<int8_t>(uint8_t(0xff))),
                              dest);
        } else {
          as_vslti_bu(dest, lhs, *imm);
          as_vnor_v(dest, dest, dest);
        }
        break;
      default:
        MOZ_CRASH("unexpected condition");
    }
    return;
  }
  ScratchSimd128Scope scratch(*this);
  loadConstantSimd128(rhs, scratch);
  compareInt8x16(cond, lhs, scratch, dest);
}

void MacroAssembler::compareInt16x8(Assembler::Condition cond,
                                    FloatRegister rhs, FloatRegister lhsDest) {
  compareInt16x8(cond, lhsDest, rhs, lhsDest);
}

void MacroAssembler::compareInt16x8(Assembler::Condition cond,
                                    FloatRegister lhs, FloatRegister rhs,
                                    FloatRegister dest) {
  switch (cond) {
    case Assembler::Equal:
      as_vseq_h(dest, lhs, rhs);
      break;
    case Assembler::NotEqual:
      as_vseq_h(dest, lhs, rhs);
      as_vnor_v(dest, dest, dest);
      break;
    case Assembler::LessThan:
      as_vslt_h(dest, lhs, rhs);
      break;
    case Assembler::LessThanOrEqual:
      as_vsle_h(dest, lhs, rhs);
      break;
    case Assembler::GreaterThan:
      as_vslt_h(dest, rhs, lhs);
      break;
    case Assembler::GreaterThanOrEqual:
      as_vsle_h(dest, rhs, lhs);
      break;
    case Assembler::Below:
      as_vslt_hu(dest, lhs, rhs);
      break;
    case Assembler::BelowOrEqual:
      as_vsle_hu(dest, lhs, rhs);
      break;
    case Assembler::Above:
      as_vslt_hu(dest, rhs, lhs);
      break;
    case Assembler::AboveOrEqual:
      as_vsle_hu(dest, rhs, lhs);
      break;
    default:
      MOZ_CRASH("unexpected condition");
  }
}

void MacroAssembler::compareInt16x8(Assembler::Condition cond,
                                    FloatRegister lhs, const SimdConstant& rhs,
                                    FloatRegister dest) {
  if (std::optional<int64_t> imm =
          AssemblerLOONG64::EncodeSimdCompareImmediate<int16_t>(rhs, cond)) {
    switch (cond) {
      case Assembler::Equal:
        as_vseqi_h(dest, lhs, *imm);
        break;
      case Assembler::NotEqual:
        as_vseqi_h(dest, lhs, *imm);
        as_vnor_v(dest, dest, dest);
        break;
      case Assembler::LessThan:
        as_vslti_h(dest, lhs, *imm);
        break;
      case Assembler::LessThanOrEqual:
        as_vslei_h(dest, lhs, *imm);
        break;
      case Assembler::GreaterThan:
        as_vslei_h(dest, lhs, *imm);
        as_vnor_v(dest, dest, dest);
        break;
      case Assembler::GreaterThanOrEqual:
        as_vslti_h(dest, lhs, *imm);
        as_vnor_v(dest, dest, dest);
        break;
      case Assembler::Below:
        if (*imm == 0) {
          // x <u 0 is always false.
          loadConstantSimd128(SimdConstant::SplatX8(0), dest);
        } else {
          as_vslti_hu(dest, lhs, *imm);
        }
        break;
      case Assembler::BelowOrEqual:
        as_vslei_hu(dest, lhs, *imm);
        break;
      case Assembler::Above:
        as_vslei_hu(dest, lhs, *imm);
        as_vnor_v(dest, dest, dest);
        break;
      case Assembler::AboveOrEqual:
        if (*imm == 0) {
          // x >=u 0 is always true.
          loadConstantSimd128(
              SimdConstant::SplatX8(
                  mozilla::BitwiseCast<int16_t>(uint16_t(0xffff))),
              dest);
        } else {
          as_vslti_hu(dest, lhs, *imm);
          as_vnor_v(dest, dest, dest);
        }
        break;
      default:
        MOZ_CRASH("unexpected condition");
    }
    return;
  }
  ScratchSimd128Scope scratch(*this);
  loadConstantSimd128(rhs, scratch);
  compareInt16x8(cond, lhs, scratch, dest);
}

void MacroAssembler::compareInt32x4(Assembler::Condition cond,
                                    FloatRegister rhs, FloatRegister lhsDest) {
  compareInt32x4(cond, lhsDest, rhs, lhsDest);
}

void MacroAssembler::compareInt32x4(Assembler::Condition cond,
                                    FloatRegister lhs, FloatRegister rhs,
                                    FloatRegister dest) {
  switch (cond) {
    case Assembler::Equal:
      as_vseq_w(dest, lhs, rhs);
      break;
    case Assembler::NotEqual:
      as_vseq_w(dest, lhs, rhs);
      as_vnor_v(dest, dest, dest);
      break;
    case Assembler::LessThan:
      as_vslt_w(dest, lhs, rhs);
      break;
    case Assembler::LessThanOrEqual:
      as_vsle_w(dest, lhs, rhs);
      break;
    case Assembler::GreaterThan:
      as_vslt_w(dest, rhs, lhs);
      break;
    case Assembler::GreaterThanOrEqual:
      as_vsle_w(dest, rhs, lhs);
      break;
    case Assembler::Below:
      as_vslt_wu(dest, lhs, rhs);
      break;
    case Assembler::BelowOrEqual:
      as_vsle_wu(dest, lhs, rhs);
      break;
    case Assembler::Above:
      as_vslt_wu(dest, rhs, lhs);
      break;
    case Assembler::AboveOrEqual:
      as_vsle_wu(dest, rhs, lhs);
      break;
    default:
      MOZ_CRASH("unexpected condition");
  }
}

void MacroAssembler::compareInt32x4(Assembler::Condition cond,
                                    FloatRegister lhs, const SimdConstant& rhs,
                                    FloatRegister dest) {
  if (std::optional<int64_t> imm =
          AssemblerLOONG64::EncodeSimdCompareImmediate<int32_t>(rhs, cond)) {
    switch (cond) {
      case Assembler::Equal:
        as_vseqi_w(dest, lhs, *imm);
        break;
      case Assembler::NotEqual:
        as_vseqi_w(dest, lhs, *imm);
        as_vnor_v(dest, dest, dest);
        break;
      case Assembler::LessThan:
        as_vslti_w(dest, lhs, *imm);
        break;
      case Assembler::LessThanOrEqual:
        as_vslei_w(dest, lhs, *imm);
        break;
      case Assembler::GreaterThan:
        as_vslei_w(dest, lhs, *imm);
        as_vnor_v(dest, dest, dest);
        break;
      case Assembler::GreaterThanOrEqual:
        as_vslti_w(dest, lhs, *imm);
        as_vnor_v(dest, dest, dest);
        break;
      case Assembler::Below:
        if (*imm == 0) {
          // x <u 0 is always false.
          loadConstantSimd128(SimdConstant::SplatX4(0), dest);
        } else {
          as_vslti_wu(dest, lhs, *imm);
        }
        break;
      case Assembler::BelowOrEqual:
        as_vslei_wu(dest, lhs, *imm);
        break;
      case Assembler::Above:
        as_vslei_wu(dest, lhs, *imm);
        as_vnor_v(dest, dest, dest);
        break;
      case Assembler::AboveOrEqual:
        if (*imm == 0) {
          // x >=u 0 is always true.
          loadConstantSimd128(
              SimdConstant::SplatX4(
                  mozilla::BitwiseCast<int32_t>(uint32_t(0xffffffff))),
              dest);
        } else {
          as_vslti_wu(dest, lhs, *imm);
          as_vnor_v(dest, dest, dest);
        }
        break;
      default:
        MOZ_CRASH("unexpected condition");
    }
    return;
  }
  ScratchSimd128Scope scratch(*this);
  loadConstantSimd128(rhs, scratch);
  compareInt32x4(cond, lhs, scratch, dest);
}

void MacroAssembler::compareFloat32x4(Assembler::Condition cond,
                                      FloatRegister rhs,
                                      FloatRegister lhsDest) {
  compareFloat32x4(cond, lhsDest, rhs, lhsDest);
}

void MacroAssembler::compareFloat32x4(Assembler::Condition cond,
                                      FloatRegister lhs, FloatRegister rhs,
                                      FloatRegister dest) {
  switch (cond) {
    case Assembler::Equal:
      as_vfcmp_cond_s(CEQ, dest, lhs, rhs);
      break;
    case Assembler::NotEqual:
      as_vfcmp_cond_s(CUNE, dest, lhs, rhs);
      break;
    case Assembler::LessThan:
      as_vfcmp_cond_s(CLT, dest, lhs, rhs);
      break;
    case Assembler::LessThanOrEqual:
      as_vfcmp_cond_s(CLE, dest, lhs, rhs);
      break;
    case Assembler::GreaterThan:
      as_vfcmp_cond_s(CLT, dest, rhs, lhs);
      break;
    case Assembler::GreaterThanOrEqual:
      as_vfcmp_cond_s(CLE, dest, rhs, lhs);
      break;
    default:
      MOZ_CRASH("unexpected condition");
  }
}

void MacroAssembler::compareFloat64x2(Assembler::Condition cond,
                                      FloatRegister rhs,
                                      FloatRegister lhsDest) {
  compareFloat64x2(cond, lhsDest, rhs, lhsDest);
}

void MacroAssembler::compareFloat64x2(Assembler::Condition cond,
                                      FloatRegister lhs, FloatRegister rhs,
                                      FloatRegister dest) {
  switch (cond) {
    case Assembler::Equal:
      as_vfcmp_cond_d(CEQ, dest, lhs, rhs);
      break;
    case Assembler::NotEqual:
      as_vfcmp_cond_d(CUNE, dest, lhs, rhs);
      break;
    case Assembler::LessThan:
      as_vfcmp_cond_d(CLT, dest, lhs, rhs);
      break;
    case Assembler::LessThanOrEqual:
      as_vfcmp_cond_d(CLE, dest, lhs, rhs);
      break;
    case Assembler::GreaterThan:
      as_vfcmp_cond_d(CLT, dest, rhs, lhs);
      break;
    case Assembler::GreaterThanOrEqual:
      as_vfcmp_cond_d(CLE, dest, rhs, lhs);
      break;
    default:
      MOZ_CRASH("unexpected condition");
  }
}

FaultingCodeRange MacroAssembler::loadUnalignedSimd128(const BaseIndex& src,
                                                       FloatRegister dest) {
  UseScratchRegisterScope temps(*this);
  Address address = computeScaledAddress(src, temps);
  return ma_vld(dest, address);
}

FaultingCodeRange MacroAssembler::storeUnalignedSimd128(FloatRegister src,
                                                        const BaseIndex& dest) {
  UseScratchRegisterScope temps(*this);
  Address address = computeScaledAddress(dest, temps);
  return ma_vst(src, address);
}

void MacroAssembler::negFloat32x4(FloatRegister src, FloatRegister dest) {
  as_vbitrevi_w(dest, src, 31);
}

void MacroAssembler::negFloat64x2(FloatRegister src, FloatRegister dest) {
  as_vbitrevi_d(dest, src, 63);
}

void MacroAssembler::absFloat32x4(FloatRegister src, FloatRegister dest) {
  as_vbitclri_w(dest, src, 31);
}

void MacroAssembler::absFloat64x2(FloatRegister src, FloatRegister dest) {
  as_vbitclri_d(dest, src, 63);
}

void MacroAssembler::addFloat32x4(FloatRegister lhs, FloatRegister rhs,
                                  FloatRegister dest) {
  as_vfadd_s(dest, lhs, rhs);
}

void MacroAssembler::addFloat64x2(FloatRegister lhs, FloatRegister rhs,
                                  FloatRegister dest) {
  as_vfadd_d(dest, lhs, rhs);
}

void MacroAssembler::subFloat32x4(FloatRegister lhs, FloatRegister rhs,
                                  FloatRegister dest) {
  as_vfsub_s(dest, lhs, rhs);
}

void MacroAssembler::subFloat64x2(FloatRegister lhs, FloatRegister rhs,
                                  FloatRegister dest) {
  as_vfsub_d(dest, lhs, rhs);
}

void MacroAssembler::divFloat32x4(FloatRegister lhs, FloatRegister rhs,
                                  FloatRegister dest) {
  as_vfdiv_s(dest, lhs, rhs);
}

void MacroAssembler::divFloat64x2(FloatRegister lhs, FloatRegister rhs,
                                  FloatRegister dest) {
  as_vfdiv_d(dest, lhs, rhs);
}

void MacroAssembler::mulFloat32x4(FloatRegister lhs, FloatRegister rhs,
                                  FloatRegister dest) {
  as_vfmul_s(dest, lhs, rhs);
}

void MacroAssembler::mulFloat64x2(FloatRegister lhs, FloatRegister rhs,
                                  FloatRegister dest) {
  as_vfmul_d(dest, lhs, rhs);
}

void MacroAssembler::extAddPairwiseInt8x16(FloatRegister src,
                                           FloatRegister dest) {
  as_vhaddw_h_b(dest, src, src);
}

void MacroAssembler::unsignedExtAddPairwiseInt8x16(FloatRegister src,
                                                   FloatRegister dest) {
  as_vhaddw_hu_bu(dest, src, src);
}

void MacroAssembler::extAddPairwiseInt16x8(FloatRegister src,
                                           FloatRegister dest) {
  as_vhaddw_w_h(dest, src, src);
}

void MacroAssembler::unsignedExtAddPairwiseInt16x8(FloatRegister src,
                                                   FloatRegister dest) {
  as_vhaddw_wu_hu(dest, src, src);
}

void MacroAssembler::sqrtFloat32x4(FloatRegister src, FloatRegister dest) {
  as_vfsqrt_s(dest, src);
}

void MacroAssembler::sqrtFloat64x2(FloatRegister src, FloatRegister dest) {
  as_vfsqrt_d(dest, src);
}

void MacroAssembler::convertInt32x4ToFloat32x4(FloatRegister src,
                                               FloatRegister dest) {
  as_vffint_s_w(dest, src);
}

void MacroAssembler::unsignedConvertInt32x4ToFloat32x4(FloatRegister src,
                                                       FloatRegister dest) {
  as_vffint_s_wu(dest, src);
}

void MacroAssembler::convertInt32x4ToFloat64x2(FloatRegister src,
                                               FloatRegister dest) {
  as_vffintl_d_w(dest, src);
}

void MacroAssembler::unsignedConvertInt32x4ToFloat64x2(FloatRegister src,
                                                       FloatRegister dest) {
  as_vsllwil_du_wu(dest, src, 0);
  as_vffint_d_lu(dest, dest);
}

void MacroAssembler::truncSatFloat32x4ToInt32x4(FloatRegister src,
                                                FloatRegister dest,
                                                FloatRegister temp) {
  // Nowhere in the "Unofficial Intrinsics Guide", the scalar counterpart or
  // IEEE 754-2008 defines the exact bit pattern of NaN and out-of-range values
  // when truncating. Thus, we need to manually normalize them to the WASM
  // semantics.
  // TODO(loong64): Revisit if we can simplify this with plain vftintrz.w.s once
  // the authoritative manual is released.

  ScratchSimd128Scope scratch(*this);
  ScratchSimd128Scope2 scratch2(*this);

  // Normalize NaNs.
  // dest = (0.f for i in LANES if IsNaN(src[i]) else src[i])
  as_vldi(scratch2, 0);
  as_vfcmp_cond_s(CUN, scratch, src, src);
  as_vbitsel_v(dest, src, scratch2, scratch);

  // Normalize too small lanes.
  // dest = (INT32_MIN.f for i in LANES if dest[i] < INT32_MIN.f else dest[i])
  loadConstantSimd128(SimdConstant::SplatX4(static_cast<float>(
                          std::numeric_limits<int32_t>::min())),
                      scratch);
  as_vfcmp_cond_s(CLT, temp, dest, scratch);
  as_vbitsel_v(dest, dest, scratch, temp);

  // Find too big lanes.
  // scratch = (
  //   0xffffffff for i in LANES if (INT32_MAX + 1).f <= dest[i] else 0
  // )
  loadConstantSimd128(
      SimdConstant::SplatX4(static_cast<float>(
          static_cast<int64_t>(std::numeric_limits<int32_t>::max()) + 1)),
      scratch);
  as_vfcmp_cond_s(CLE, scratch2, scratch, dest);

  as_vftintrz_w_s(dest, dest);

  // Normalize too big lanes.
  // dest = (dest[i] for i in LANES if scratch2[i] == 0 else INT32_MAX)
  loadConstantSimd128(
      SimdConstant::SplatX4(std::numeric_limits<int32_t>::max()), scratch);
  as_vbitsel_v(dest, dest, scratch, scratch2);
}

void MacroAssembler::truncSatFloat64x2ToInt32x4(FloatRegister src,
                                                FloatRegister dest,
                                                FloatRegister temp) {
  // See also the opening comments in
  // MacroAssembler::truncSatFloat32x4ToInt32x4().
  // TODO(loong64): Revisit if we can simplify this with plain vftintrz.w.d once
  // the authoritative manual is released.

  ScratchSimd128Scope scratch(*this);
  ScratchSimd128Scope2 scratch2(*this);

  // Normalize NaNs.
  // dest = (0.d for i in LANES if IsNaN(src[i]) else src[i])
  as_vldi(scratch2, 0);
  as_vfcmp_cond_d(CUN, scratch, src, src);
  as_vbitsel_v(dest, src, scratch2, scratch);

  // Normalize too big lanes.
  // dest = (INT32_MAX.d for i in LANES if INT32_MAX.d <= dest[i] else dest[i])
  loadConstantSimd128(SimdConstant::SplatX2(static_cast<double>(
                          std::numeric_limits<int32_t>::max())),
                      temp);
  as_vfcmp_cond_d(CLE, scratch, temp, dest);
  as_vbitsel_v(dest, dest, temp, scratch);

  // Normalize too small lanes.
  // dest = (INT32_MIN.d for i in LANES if dest[i] < INT32_MIN.d else dest[i])
  loadConstantSimd128(SimdConstant::SplatX2(static_cast<double>(
                          std::numeric_limits<int32_t>::min())),
                      temp);
  as_vfcmp_cond_d(CLT, scratch2, dest, temp);
  as_vbitsel_v(dest, dest, temp, scratch2);

  as_vldi(scratch, 0);
  // dest = (dest[0].i32, dest[1].i32, 0.i32, 0.i32)
  as_vftintrz_w_d(dest, scratch, dest);
}

void MacroAssembler::unsignedTruncSatFloat64x2ToInt32x4(FloatRegister src,
                                                        FloatRegister dest,
                                                        FloatRegister temp) {
  // See also the opening comments in
  // MacroAssembler::truncSatFloat32x4ToInt32x4().
  // TODO(loong64): Revisit if we can simplify this with plain vftintrz.lu.d
  // once the authoritative manual is released.

  ScratchSimd128Scope scratch(*this);
  ScratchSimd128Scope2 scratch2(*this);

  // Normalize NaNs.
  // dest = (0.d for i in LANES if IsNaN(src[i]) else src[i])
  as_vldi(scratch2, 0);
  as_vfcmp_cond_d(CUN, scratch, src, src);
  as_vbitsel_v(dest, src, scratch2, scratch);

  // Normalize too small lanes.
  // dest = (0.d for i in LANES if dest[i] < 0.d else dest[i])
  as_vfcmp_cond_d(CLT, scratch, dest, scratch2);
  as_vbitsel_v(dest, dest, scratch2, scratch);

  // Normalize too big lanes.
  // dest = (
  //   UINT32_MAX.d for i in LANES if UINT32_MAX.d <= dest[i]
  //   else dest[i]
  // )
  loadConstantSimd128(SimdConstant::SplatX2(static_cast<double>(
                          std::numeric_limits<uint32_t>::max())),
                      scratch);
  as_vfcmp_cond_d(CLE, scratch2, scratch, dest);
  as_vbitsel_v(dest, dest, scratch, scratch2);

  // There is no such thing as "vftintrz.wu.d".
  as_vftintrz_lu_d(dest, dest);

  as_vldi(scratch, 0);
  as_vpickev_w(dest, scratch, dest);
}

void MacroAssembler::truncFloat32x4ToInt32x4Relaxed(FloatRegister src,
                                                    FloatRegister dest) {
  as_vftintrz_w_s(dest, src);
}

void MacroAssembler::unsignedTruncFloat32x4ToInt32x4Relaxed(
    FloatRegister src, FloatRegister dest) {
  as_vftintrz_wu_s(dest, src);
}

void MacroAssembler::truncFloat64x2ToInt32x4Relaxed(FloatRegister src,
                                                    FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vldi(scratch, 0);
  as_vftintrz_w_d(dest, scratch, src);
}

void MacroAssembler::unsignedTruncFloat64x2ToInt32x4Relaxed(
    FloatRegister src, FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  ScratchSimd128Scope2 scratch2(*this);
  as_vldi(scratch, 0);
  as_vftintrz_lu_d(scratch2, src);
  as_vpickev_w(dest, scratch, scratch2);
}

void MacroAssembler::convertFloat64x2ToFloat32x4(FloatRegister src,
                                                 FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vldi(scratch, 0);
  as_vfcvt_s_d(dest, scratch, src);
}

void MacroAssembler::convertFloat32x4ToFloat64x2(FloatRegister src,
                                                 FloatRegister dest) {
  as_vfcvtl_d_s(dest, src);
}

void MacroAssembler::narrowInt16x8(FloatRegister lhs, FloatRegister rhs,
                                   FloatRegister dest) {
  // vssrani.b.h vd, vj, 0 performs clamping on the I16x16 view from the 2-tuple
  // (vj, vd), and store the resulting I8x16 into vd.
  // <https://jia.je/unofficial-loongarch-intrinsics-guide/lsx/shift/#__m128i-__lsx_vssrani_b_h-__m128i-a-__m128i-b-imm0_15-imm>
  if (dest == lhs) {
    // We should not clobber |rhs|.
    ScratchSimd128Scope scratch(*this);
    moveSimd128(rhs, scratch);
    as_vssrani_b_h(scratch, lhs, 0);
    moveSimd128(scratch, dest);
  } else {
    // We can use |dest| as the scratch register.
    if (dest != rhs) {
      moveSimd128(rhs, dest);
    }
    as_vssrani_b_h(dest, lhs, 0);
  }
}

void MacroAssembler::unsignedNarrowInt16x8(FloatRegister lhs, FloatRegister rhs,
                                           FloatRegister dest) {
  // Ditto.
  if (dest == lhs) {
    ScratchSimd128Scope scratch(*this);
    moveSimd128(rhs, scratch);
    as_vssrani_bu_h(scratch, lhs, 0);
    moveSimd128(scratch, dest);
  } else {
    if (dest != rhs) {
      moveSimd128(rhs, dest);
    }
    as_vssrani_bu_h(dest, lhs, 0);
  }
}

void MacroAssembler::narrowInt32x4(FloatRegister lhs, FloatRegister rhs,
                                   FloatRegister dest) {
  // Ditto.
  if (dest == lhs) {
    ScratchSimd128Scope scratch(*this);
    moveSimd128(rhs, scratch);
    as_vssrani_h_w(scratch, lhs, 0);
    moveSimd128(scratch, dest);
  } else {
    if (dest != rhs) {
      moveSimd128(rhs, dest);
    }
    as_vssrani_h_w(dest, lhs, 0);
  }
}

void MacroAssembler::unsignedNarrowInt32x4(FloatRegister lhs, FloatRegister rhs,
                                           FloatRegister dest) {
  // Ditto.
  if (dest == lhs) {
    ScratchSimd128Scope scratch(*this);
    moveSimd128(rhs, scratch);
    as_vssrani_hu_w(scratch, lhs, 0);
    moveSimd128(scratch, dest);
  } else {
    if (dest != rhs) {
      moveSimd128(rhs, dest);
    }
    as_vssrani_hu_w(dest, lhs, 0);
  }
}

void MacroAssembler::widenLowInt8x16(FloatRegister src, FloatRegister dest) {
  as_vsllwil_h_b(dest, src, 0);
}

void MacroAssembler::widenHighInt8x16(FloatRegister src, FloatRegister dest) {
  as_vexth_h_b(dest, src);
}

void MacroAssembler::unsignedWidenLowInt8x16(FloatRegister src,
                                             FloatRegister dest) {
  as_vsllwil_hu_bu(dest, src, 0);
}

void MacroAssembler::unsignedWidenHighInt8x16(FloatRegister src,
                                              FloatRegister dest) {
  as_vexth_hu_bu(dest, src);
}

void MacroAssembler::widenLowInt16x8(FloatRegister src, FloatRegister dest) {
  as_vsllwil_w_h(dest, src, 0);
}

void MacroAssembler::widenHighInt16x8(FloatRegister src, FloatRegister dest) {
  as_vexth_w_h(dest, src);
}

void MacroAssembler::unsignedWidenLowInt16x8(FloatRegister src,
                                             FloatRegister dest) {
  as_vsllwil_wu_hu(dest, src, 0);
}

void MacroAssembler::unsignedWidenHighInt16x8(FloatRegister src,
                                              FloatRegister dest) {
  as_vexth_wu_hu(dest, src);
}

void MacroAssembler::widenLowInt32x4(FloatRegister src, FloatRegister dest) {
  as_vsllwil_d_w(dest, src, 0);
}

void MacroAssembler::unsignedWidenLowInt32x4(FloatRegister src,
                                             FloatRegister dest) {
  as_vsllwil_du_wu(dest, src, 0);
}

void MacroAssembler::widenHighInt32x4(FloatRegister src, FloatRegister dest) {
  as_vexth_d_w(dest, src);
}

void MacroAssembler::unsignedWidenHighInt32x4(FloatRegister src,
                                              FloatRegister dest) {
  as_vexth_du_wu(dest, src);
}

void MacroAssembler::pseudoMinFloat32x4(FloatRegister rhsOrRhsDest,
                                        FloatRegister lhsOrLhsDest) {
  ScratchSimd128Scope scratch(*this);
  as_vfcmp_cond_s(CLT, scratch, rhsOrRhsDest, lhsOrLhsDest);
  as_vbitsel_v(lhsOrLhsDest, lhsOrLhsDest, rhsOrRhsDest, scratch);
}

void MacroAssembler::pseudoMinFloat32x4(FloatRegister lhs, FloatRegister rhs,
                                        FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vfcmp_cond_s(CLT, scratch, rhs, lhs);
  as_vbitsel_v(dest, lhs, rhs, scratch);
}

void MacroAssembler::pseudoMinFloat64x2(FloatRegister rhsOrRhsDest,
                                        FloatRegister lhsOrLhsDest) {
  ScratchSimd128Scope scratch(*this);
  as_vfcmp_cond_d(CLT, scratch, rhsOrRhsDest, lhsOrLhsDest);
  as_vbitsel_v(lhsOrLhsDest, lhsOrLhsDest, rhsOrRhsDest, scratch);
}

void MacroAssembler::pseudoMinFloat64x2(FloatRegister lhs, FloatRegister rhs,
                                        FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vfcmp_cond_d(CLT, scratch, rhs, lhs);
  as_vbitsel_v(dest, lhs, rhs, scratch);
}

void MacroAssembler::pseudoMaxFloat32x4(FloatRegister rhsOrRhsDest,
                                        FloatRegister lhsOrLhsDest) {
  ScratchSimd128Scope scratch(*this);
  as_vfcmp_cond_s(CLT, scratch, lhsOrLhsDest, rhsOrRhsDest);
  as_vbitsel_v(lhsOrLhsDest, lhsOrLhsDest, rhsOrRhsDest, scratch);
}

void MacroAssembler::pseudoMaxFloat32x4(FloatRegister lhs, FloatRegister rhs,
                                        FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vfcmp_cond_s(CLT, scratch, lhs, rhs);
  as_vbitsel_v(dest, lhs, rhs, scratch);
}

void MacroAssembler::pseudoMaxFloat64x2(FloatRegister rhsOrRhsDest,
                                        FloatRegister lhsOrLhsDest) {
  ScratchSimd128Scope scratch(*this);
  as_vfcmp_cond_d(CLT, scratch, lhsOrLhsDest, rhsOrRhsDest);
  as_vbitsel_v(lhsOrLhsDest, lhsOrLhsDest, rhsOrRhsDest, scratch);
}

void MacroAssembler::pseudoMaxFloat64x2(FloatRegister lhs, FloatRegister rhs,
                                        FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vfcmp_cond_d(CLT, scratch, lhs, rhs);
  as_vbitsel_v(dest, lhs, rhs, scratch);
}

void MacroAssembler::widenDotInt16x8(FloatRegister lhs, FloatRegister rhs,
                                     FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmulwev_w_h(scratch, lhs, rhs);
  as_vmulwod_w_h(dest, lhs, rhs);
  as_vadd_w(dest, dest, scratch);
}

void MacroAssembler::dotInt8x16Int7x16(FloatRegister lhs, FloatRegister rhs,
                                       FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmulwev_h_b(scratch, lhs, rhs);
  as_vmulwod_h_b(dest, lhs, rhs);
  as_vadd_h(dest, dest, scratch);
}

void MacroAssembler::ceilFloat32x4(FloatRegister src, FloatRegister dest) {
  as_vfrintrp_s(dest, src);
}

void MacroAssembler::ceilFloat64x2(FloatRegister src, FloatRegister dest) {
  as_vfrintrp_d(dest, src);
}

void MacroAssembler::floorFloat32x4(FloatRegister src, FloatRegister dest) {
  as_vfrintrm_s(dest, src);
}

void MacroAssembler::floorFloat64x2(FloatRegister src, FloatRegister dest) {
  as_vfrintrm_d(dest, src);
}

void MacroAssembler::truncFloat32x4(FloatRegister src, FloatRegister dest) {
  as_vfrintrz_s(dest, src);
}

void MacroAssembler::truncFloat64x2(FloatRegister src, FloatRegister dest) {
  as_vfrintrz_d(dest, src);
}

void MacroAssembler::nearestFloat32x4(FloatRegister src, FloatRegister dest) {
  as_vfrintrne_s(dest, src);
}

void MacroAssembler::nearestFloat64x2(FloatRegister src, FloatRegister dest) {
  as_vfrintrne_d(dest, src);
}

void MacroAssembler::fmaFloat32x4(FloatRegister src1, FloatRegister src2,
                                  FloatRegister srcDest) {
  as_vfmadd_s(srcDest, src1, src2, srcDest);
}

void MacroAssembler::fnmaFloat32x4(FloatRegister src1, FloatRegister src2,
                                   FloatRegister srcDest) {
  as_vfnmsub_s(srcDest, src1, src2, srcDest);
}

void MacroAssembler::fmaFloat64x2(FloatRegister src1, FloatRegister src2,
                                  FloatRegister srcDest) {
  as_vfmadd_d(srcDest, src1, src2, srcDest);
}

void MacroAssembler::fnmaFloat64x2(FloatRegister src1, FloatRegister src2,
                                   FloatRegister srcDest) {
  as_vfnmsub_d(srcDest, src1, src2, srcDest);
}

void MacroAssembler::minFloat32x4Relaxed(FloatRegister src,
                                         FloatRegister srcDest) {
  as_vfmin_s(srcDest, srcDest, src);
}

void MacroAssembler::minFloat32x4Relaxed(FloatRegister lhs, FloatRegister rhs,
                                         FloatRegister dest) {
  as_vfmin_s(dest, lhs, rhs);
}

void MacroAssembler::maxFloat32x4Relaxed(FloatRegister src,
                                         FloatRegister srcDest) {
  as_vfmax_s(srcDest, srcDest, src);
}

void MacroAssembler::maxFloat32x4Relaxed(FloatRegister lhs, FloatRegister rhs,
                                         FloatRegister dest) {
  as_vfmax_s(dest, lhs, rhs);
}

void MacroAssembler::minFloat64x2Relaxed(FloatRegister src,
                                         FloatRegister srcDest) {
  as_vfmin_d(srcDest, srcDest, src);
}

void MacroAssembler::minFloat64x2Relaxed(FloatRegister lhs, FloatRegister rhs,
                                         FloatRegister dest) {
  as_vfmin_d(dest, lhs, rhs);
}

void MacroAssembler::maxFloat64x2Relaxed(FloatRegister src,
                                         FloatRegister srcDest) {
  as_vfmax_d(srcDest, srcDest, src);
}

void MacroAssembler::maxFloat64x2Relaxed(FloatRegister lhs, FloatRegister rhs,
                                         FloatRegister dest) {
  as_vfmax_d(dest, lhs, rhs);
}

void MacroAssembler::q15MulrInt16x8Relaxed(FloatRegister lhs, FloatRegister rhs,
                                           FloatRegister dest) {
  q15MulrSatInt16x8(lhs, rhs, dest);
}

void MacroAssembler::splatX2(Register64 src, FloatRegister dest) {
  as_vreplgr2vr_d(dest, src.reg);
}

void MacroAssembler::extractLaneInt64x2(uint32_t lane, FloatRegister src,
                                        Register64 dest) {
  as_vpickve2gr_d(dest.reg, src, lane);
}

void MacroAssembler::replaceLaneInt64x2(unsigned lane, Register64 rhs,
                                        FloatRegister lhsDest) {
  as_vinsgr2vr_d(lhsDest, rhs.reg, lane);
}

void MacroAssembler::leftShiftInt8x16(FloatRegister lhs, Register rhs,
                                      FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vreplgr2vr_b(scratch, rhs);
  as_vsll_b(dest, lhs, scratch);
}

void MacroAssembler::leftShiftInt16x8(FloatRegister lhs, Register rhs,
                                      FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vreplgr2vr_h(scratch, rhs);
  as_vsll_h(dest, lhs, scratch);
}

void MacroAssembler::leftShiftInt32x4(FloatRegister lhs, Register rhs,
                                      FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vreplgr2vr_w(scratch, rhs);
  as_vsll_w(dest, lhs, scratch);
}

void MacroAssembler::leftShiftInt64x2(FloatRegister lhs, Register rhs,
                                      FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vreplgr2vr_d(scratch, rhs);
  as_vsll_d(dest, lhs, scratch);
}

void MacroAssembler::rightShiftInt8x16(FloatRegister lhs, Register rhs,
                                       FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vreplgr2vr_b(scratch, rhs);
  as_vsra_b(dest, lhs, scratch);
}

void MacroAssembler::rightShiftInt16x8(FloatRegister lhs, Register rhs,
                                       FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vreplgr2vr_h(scratch, rhs);
  as_vsra_h(dest, lhs, scratch);
}

void MacroAssembler::rightShiftInt32x4(FloatRegister lhs, Register rhs,
                                       FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vreplgr2vr_w(scratch, rhs);
  as_vsra_w(dest, lhs, scratch);
}

void MacroAssembler::rightShiftInt64x2(FloatRegister lhs, Register rhs,
                                       FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vreplgr2vr_d(scratch, rhs);
  as_vsra_d(dest, lhs, scratch);
}

void MacroAssembler::unsignedRightShiftInt8x16(FloatRegister lhs, Register rhs,
                                               FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vreplgr2vr_b(scratch, rhs);
  as_vsrl_b(dest, lhs, scratch);
}

void MacroAssembler::unsignedRightShiftInt16x8(FloatRegister lhs, Register rhs,
                                               FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vreplgr2vr_h(scratch, rhs);
  as_vsrl_h(dest, lhs, scratch);
}

void MacroAssembler::unsignedRightShiftInt32x4(FloatRegister lhs, Register rhs,
                                               FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vreplgr2vr_w(scratch, rhs);
  as_vsrl_w(dest, lhs, scratch);
}

void MacroAssembler::unsignedRightShiftInt64x2(FloatRegister lhs, Register rhs,
                                               FloatRegister dest) {
  ScratchSimd128Scope scratch(*this);
  as_vreplgr2vr_d(scratch, rhs);
  as_vsrl_d(dest, lhs, scratch);
}

void MacroAssembler::mulInt64x2(FloatRegister lhs, FloatRegister rhs,
                                FloatRegister dest) {
  as_vmul_d(dest, lhs, rhs);
}

void MacroAssembler::minFloat32x4(FloatRegister rhs, FloatRegister lhsDest) {
  minFloat32x4(lhsDest, rhs, lhsDest);
}

void MacroAssembler::minFloat32x4(FloatRegister lhs, FloatRegister rhs,
                                  FloatRegister dest) {
  // IEEE Std 754-2008's minNum() semantics differ from WASM's requirements.
  // Thus we cannot use vfmin.s here. Manually construct the required value. See
  // also Bug 2073769 for prior experiences with the scalar counterparts.
  // TODO(loong64): Revisit if we can simplify this with vfmin.s once the
  // authoritative manual is released.

  ScratchSimd128Scope scratch(*this);
  ScratchSimd128Scope2 scratch2(*this);

  // Normalize (+0, -0) ties. Select lhs on numerically equal lanes, then
  // bitwise OR with rhs to force the sign bit on.
  // scratch = (lhs[i] for i in LANES if lhs[i] == rhs[i] else rhs[i]) | rhs
  as_vfcmp_cond_s(CEQ, scratch, lhs, rhs);
  as_vbitsel_v(scratch, rhs, lhs, scratch);
  as_vor_v(scratch2, scratch, rhs);

  // Select the smaller lane.
  // scratch = (lhs[i] for i in LANES if lhs[i] < scratch[i] else scratch[i])
  as_vfcmp_cond_s(CLT, scratch, lhs, scratch2);
  as_vbitsel_v(scratch2, scratch2, lhs, scratch);

  // Normalize NaNs.
  // dest = (
  //   FPUDefaultQNaN.f for i in LANES if IsUnordered(lhs[i], rhs[i])
  //   else scratch[i]
  // )
  as_vfcmp_cond_s(CUN, scratch, lhs, rhs);
  loadConstantSimd128(SimdConstant::SplatX4(FPUDefaultQNaN<float>()), dest);
  as_vbitsel_v(dest, scratch2, dest, scratch);
}

void MacroAssembler::minFloat64x2(FloatRegister rhs, FloatRegister lhsDest) {
  minFloat64x2(lhsDest, rhs, lhsDest);
}

void MacroAssembler::minFloat64x2(FloatRegister lhs, FloatRegister rhs,
                                  FloatRegister dest) {
  // See the opening comment in MacroAssembler::minFloat32x4().
  // TODO(loong64): Revisit if we can simplify this with vfmin.d once the
  // authoritative manual is released.

  ScratchSimd128Scope scratch(*this);
  ScratchSimd128Scope2 scratch2(*this);

  // Normalize (+0, -0) ties.
  as_vfcmp_cond_d(CEQ, scratch, lhs, rhs);
  as_vbitsel_v(scratch, rhs, lhs, scratch);
  as_vor_v(scratch2, scratch, rhs);

  // Select the smaller lane.
  as_vfcmp_cond_d(CLT, scratch, lhs, scratch2);
  as_vbitsel_v(scratch2, scratch2, lhs, scratch);

  // Normalize NaNs.
  as_vfcmp_cond_d(CUN, scratch, lhs, rhs);
  loadConstantSimd128(SimdConstant::SplatX2(FPUDefaultQNaN<double>()), dest);
  as_vbitsel_v(dest, scratch2, dest, scratch);
}

void MacroAssembler::maxFloat32x4(FloatRegister rhs, FloatRegister lhsDest) {
  maxFloat32x4(lhsDest, rhs, lhsDest);
}

void MacroAssembler::maxFloat32x4(FloatRegister lhs, FloatRegister rhs,
                                  FloatRegister dest) {
  // See the opening comment in MacroAssembler::minFloat32x4().
  // TODO(loong64): Revisit if we can simplify this with vfmax.s once the
  // authoritative manual is released.

  ScratchSimd128Scope scratch(*this);
  ScratchSimd128Scope2 scratch2(*this);

  // Normalize (+0, -0) ties.
  as_vfcmp_cond_s(CEQ, scratch, lhs, rhs);
  as_vbitsel_v(scratch, rhs, lhs, scratch);
  as_vand_v(scratch2, scratch, rhs);

  // Select the larger lane.
  as_vfcmp_cond_s(CLT, scratch, scratch2, lhs);
  as_vbitsel_v(scratch2, scratch2, lhs, scratch);

  // Normalize NaNs.
  as_vfcmp_cond_s(CUN, scratch, lhs, rhs);
  loadConstantSimd128(SimdConstant::SplatX4(FPUDefaultQNaN<float>()), dest);
  as_vbitsel_v(dest, scratch2, dest, scratch);
}

void MacroAssembler::maxFloat64x2(FloatRegister rhs, FloatRegister lhsDest) {
  maxFloat64x2(lhsDest, rhs, lhsDest);
}

void MacroAssembler::maxFloat64x2(FloatRegister lhs, FloatRegister rhs,
                                  FloatRegister dest) {
  // See the opening comment in MacroAssembler::minFloat32x4().
  // TODO(loong64): Revisit if we can simplify this with vfmax.d once the
  // authoritative manual is released.

  ScratchSimd128Scope scratch(*this);
  ScratchSimd128Scope2 scratch2(*this);

  // Normalize (+0, -0) ties.
  as_vfcmp_cond_d(CEQ, scratch, lhs, rhs);
  as_vbitsel_v(scratch, rhs, lhs, scratch);
  as_vand_v(scratch2, scratch, rhs);

  // Select the larger lane.
  as_vfcmp_cond_d(CLT, scratch, scratch2, lhs);
  as_vbitsel_v(scratch2, scratch2, lhs, scratch);

  // Normalize NaNs.
  as_vfcmp_cond_d(CUN, scratch, lhs, rhs);
  loadConstantSimd128(SimdConstant::SplatX2(FPUDefaultQNaN<double>()), dest);
  as_vbitsel_v(dest, scratch2, dest, scratch);
}

void MacroAssembler::bitmaskInt8x16(FloatRegister src, Register dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmskltz_b(scratch, src);
  // Lane i correspond to LSB i in |scratch|, and other bits are zeroed. Since
  // we only have 16 lanes, sign-extending on bit 31 is correct.
  // <https://jia.je/unofficial-loongarch-intrinsics-guide/lsx/misc/#__m128i-__lsx_vmskltz_b-__m128i-a>
  as_movfr2gr_s(dest, scratch);
}

void MacroAssembler::bitmaskInt16x8(FloatRegister src, Register dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmskltz_h(scratch, src);
  // Ditto. See MacroAssembler::bitmaskInt8x16().
  as_movfr2gr_s(dest, scratch);
}

void MacroAssembler::bitmaskInt32x4(FloatRegister src, Register dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmskltz_w(scratch, src);
  // Ditto. See MacroAssembler::bitmaskInt8x16().
  as_movfr2gr_s(dest, scratch);
}

void MacroAssembler::bitmaskInt64x2(FloatRegister src, Register dest) {
  ScratchSimd128Scope scratch(*this);
  as_vmskltz_d(scratch, src);
  // Ditto. See MacroAssembler::bitmaskInt8x16().
  as_movfr2gr_s(dest, scratch);
}

void MacroAssembler::popcntInt8x16(FloatRegister src, FloatRegister dest) {
  as_vpcnt_b(dest, src);
}

void MacroAssembler::unsignedTruncSatFloat32x4ToInt32x4(FloatRegister src,
                                                        FloatRegister dest) {
  // See also the opening comments in
  // MacroAssembler::truncSatFloat32x4ToInt32x4().
  // TODO(loong64): Revisit if we can simplify this with plain vftintrz.wu.s
  // once the authoritative manual is released.

  ScratchSimd128Scope scratch(*this);
  ScratchSimd128Scope2 scratch2(*this);

  // Normalize NaNs.
  // dest = (0.f for i in LANES if IsNaN(src[i]) else src[i])
  as_vldi(scratch2, 0);
  as_vfcmp_cond_s(CUN, scratch, src, src);
  as_vbitsel_v(dest, src, scratch2, scratch);

  // Normalize too small lanes.
  // dest = (0.f for i in LANES if dest[i] < 0.f else dest[i])
  as_vfcmp_cond_s(CLT, scratch, dest, scratch2);
  as_vbitsel_v(dest, dest, scratch2, scratch);

  // Normalize too big lanes.
  // dest = (
  //   UINT32_MAX.f for i in LANES if UINT32_MAX.f <= dest[i]
  //   else dest[i]
  // )
  loadConstantSimd128(SimdConstant::SplatX4(static_cast<float>(
                          std::numeric_limits<uint32_t>::max())),
                      scratch);
  as_vfcmp_cond_s(CLE, scratch2, scratch, dest);
  as_vbitsel_v(dest, dest, scratch, scratch2);

  as_vftintrz_wu_s(dest, dest);
}

void MacroAssembler::compareInt64x2(Assembler::Condition cond,
                                    FloatRegister rhs, FloatRegister lhsDest) {
  compareInt64x2(cond, lhsDest, rhs, lhsDest);
}

void MacroAssembler::compareInt64x2(Assembler::Condition cond,
                                    FloatRegister lhs, FloatRegister rhs,
                                    FloatRegister dest) {
  switch (cond) {
    case Assembler::Equal:
      as_vseq_d(dest, lhs, rhs);
      break;
    case Assembler::NotEqual:
      as_vseq_d(dest, lhs, rhs);
      as_vnor_v(dest, dest, dest);
      break;
    case Assembler::LessThan:
      as_vslt_d(dest, lhs, rhs);
      break;
    case Assembler::LessThanOrEqual:
      as_vsle_d(dest, lhs, rhs);
      break;
    case Assembler::GreaterThan:
      as_vslt_d(dest, rhs, lhs);
      break;
    case Assembler::GreaterThanOrEqual:
      as_vsle_d(dest, rhs, lhs);
      break;
    default:
      MOZ_CRASH("unexpected condition");
  }
}

void MacroAssembler::compareInt64x2(Assembler::Condition cond,
                                    FloatRegister lhs, const SimdConstant& rhs,
                                    FloatRegister dest) {
  if (std::optional<int64_t> imm =
          AssemblerLOONG64::EncodeSimdCompareImmediate<int64_t>(rhs, cond)) {
    switch (cond) {
      case Assembler::Equal:
        as_vseqi_d(dest, lhs, *imm);
        break;
      case Assembler::NotEqual:
        as_vseqi_d(dest, lhs, *imm);
        as_vnor_v(dest, dest, dest);
        break;
      case Assembler::LessThan:
        as_vslti_d(dest, lhs, *imm);
        break;
      case Assembler::LessThanOrEqual:
        as_vslei_d(dest, lhs, *imm);
        break;
      case Assembler::GreaterThan:
        as_vslei_d(dest, lhs, *imm);
        as_vnor_v(dest, dest, dest);
        break;
      case Assembler::GreaterThanOrEqual:
        as_vslti_d(dest, lhs, *imm);
        as_vnor_v(dest, dest, dest);
        break;
      default:
        MOZ_CRASH("unexpected condition");
    }
    return;
  }
  ScratchSimd128Scope scratch(*this);
  loadConstantSimd128(rhs, scratch);
  compareInt64x2(cond, lhs, scratch, dest);
}

void MacroAssembler::bitwiseSelectSimd128(FloatRegister onTrue,
                                          FloatRegister onFalse,
                                          FloatRegister maskDest) {
  as_vbitsel_v(maskDest, onFalse, onTrue, maskDest);
}

void MacroAssembler::dotInt8x16Int7x16ThenAdd(FloatRegister lhs,
                                              FloatRegister rhs,
                                              FloatRegister dest,
                                              FloatRegister temp) {
  ScratchSimd128Scope scratch(*this);
  as_vmulwev_h_b(scratch, lhs, rhs);
  as_vmulwod_h_b(temp, lhs, rhs);
  as_vadd_h(temp, temp, scratch);
  as_vhaddw_w_h(temp, temp, temp);
  as_vadd_w(dest, dest, temp);
}

//}}} check_macroassembler_style
// ===============================================================

// The specializations for cmpPtrSet are outside the braces because
// check_macroassembler_style can't yet deal with specializations.

template <>
inline void MacroAssembler::cmpPtrSet(Assembler::Condition cond, Address lhs,
                                      ImmPtr rhs, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(lhs, scratch);
  cmpPtrSet(cond, Register(scratch), rhs, dest);
}

template <>
inline void MacroAssembler::cmpPtrSet(Assembler::Condition cond, Register lhs,
                                      Address rhs, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(lhs != scratch);
  loadPtr(rhs, scratch);
  cmpPtrSet(cond, lhs, Register(scratch), dest);
}

template <>
inline void MacroAssembler::cmpPtrSet(Assembler::Condition cond, Address lhs,
                                      Register rhs, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(rhs != scratch);
  loadPtr(lhs, scratch);
  cmpPtrSet(cond, Register(scratch), rhs, dest);
}

template <>
inline void MacroAssembler::cmp32Set(Assembler::Condition cond, Register lhs,
                                     Address rhs, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(lhs != scratch);
  load32(rhs, scratch);
  cmp32Set(cond, lhs, Register(scratch), dest);
}

template <>
inline void MacroAssembler::cmp32Set(Assembler::Condition cond, Address lhs,
                                     Register rhs, Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(rhs != scratch);
  load32(lhs, scratch);
  cmp32Set(cond, Register(scratch), rhs, dest);
}

void MacroAssemblerLOONG64Compat::incrementInt32Value(const Address& addr) {
  asMasm().add32(Imm32(1), addr);
}

void MacroAssemblerLOONG64Compat::retn(Imm32 n) {
  // pc <- [sp]; sp += n
  loadPtr(Address(StackPointer, 0), ra);
  asMasm().addPtr(n, StackPointer);
  as_jirl(zero, ra, BOffImm16(0));
}

}  // namespace jit
}  // namespace js

#endif /* jit_loong64_MacroAssembler_loong64_inl_h */
