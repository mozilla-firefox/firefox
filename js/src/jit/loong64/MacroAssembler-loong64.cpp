/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/loong64/MacroAssembler-loong64.h"

#include "jit/Bailouts.h"
#include "jit/BaselineFrame.h"
#include "jit/JitFrames.h"
#include "jit/JitRuntime.h"
#include "jit/loong64/SharedICRegisters-loong64.h"
#include "jit/MacroAssembler.h"
#include "jit/MoveEmitter.h"
#include "util/Memory.h"
#include "util/PortableMath.h"
#include "vm/JitActivation.h"  // js::jit::JitActivation
#include "vm/JSContext.h"
#include "wasm/WasmStubs.h"

#include "jit/MacroAssembler-inl.h"

namespace js {
namespace jit {

void MacroAssembler::clampDoubleToUint8(FloatRegister input, Register output) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ScratchDoubleScope fpscratch(asMasm());
  as_ftintrne_l_d(fpscratch, input);
  as_movfr2gr_d(output, fpscratch);
  // if (res < 0); res = 0;
  as_slt(scratch, output, zero);
  as_masknez(output, output, scratch);
  // if res > 255; res = 255;
  as_sltui(scratch, output, 255);
  as_addi_d(output, output, -255);
  as_maskeqz(output, output, scratch);
  as_addi_d(output, output, 255);
}

bool MacroAssemblerLOONG64Compat::buildOOLFakeExitFrame(void* fakeReturnAddr) {
  asMasm().Push(FrameDescriptor(FrameType::IonJS));  // descriptor_
  asMasm().Push(ImmPtr(fakeReturnAddr));
  asMasm().Push(FramePointer);
  return true;
}

void MacroAssemblerLOONG64Compat::convertUInt32ToDouble(Register src,
                                                        FloatRegister dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  as_bstrpick_d(scratch, src, 31, 0);
  asMasm().convertInt64ToDouble(Register64(scratch), dest);
}

void MacroAssemblerLOONG64Compat::convertUInt64ToDouble(Register src,
                                                        FloatRegister dest) {
  Label positive, done;
  ma_b(src, src, &positive, NotSigned, ShortJump);
  UseScratchRegisterScope temp(asMasm());
  Register scratch = temp.Acquire();
  Register scratch2 = temp.Acquire();

  MOZ_ASSERT(src != scratch);
  MOZ_ASSERT(src != scratch2);

  ma_and(scratch, src, Imm32(1));
  as_srli_d(scratch2, src, 1);
  as_or(scratch, scratch, scratch2);
  as_movgr2fr_d(dest, scratch);
  as_ffint_d_l(dest, dest);
  asMasm().addDouble(dest, dest);
  ma_b(&done, ShortJump);

  bind(&positive);
  as_movgr2fr_d(dest, src);
  as_ffint_d_l(dest, dest);

  bind(&done);
}

void MacroAssemblerLOONG64Compat::convertUInt32ToFloat32(Register src,
                                                         FloatRegister dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  as_bstrpick_d(scratch, src, 31, 0);
  asMasm().convertInt64ToFloat32(Register64(scratch), dest);
}

void MacroAssemblerLOONG64Compat::convertDoubleToFloat32(FloatRegister src,
                                                         FloatRegister dest) {
  as_fcvt_s_d(dest, src);
}

const int CauseBitPos = int(Assembler::CauseI);
const int CauseBitCount = 1 + int(Assembler::CauseV) - int(Assembler::CauseI);
const int CauseIOrVMask = ((1 << int(Assembler::CauseI)) |
                           (1 << int(Assembler::CauseV))) >>
                          int(Assembler::CauseI);

// Checks whether a double is representable as a 32-bit integer. If so, the
// integer is written to the output register. Otherwise, a bailout is taken to
// the given snapshot. This function overwrites the scratch float register.
void MacroAssemblerLOONG64Compat::convertDoubleToInt32(FloatRegister src,
                                                       Register dest,
                                                       Label* fail,
                                                       bool negativeZeroCheck) {
  if (negativeZeroCheck) {
    moveFromDouble(src, dest);
    as_rotri_d(dest, dest, 63);
    ma_b(dest, Imm32(1), fail, Assembler::Equal);
  }

  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ScratchFloat32Scope fpscratch(asMasm());
  // Truncate double to int ; if result is inexact or invalid fail.
  as_ftintrz_w_d(fpscratch, src);
  as_movfcsr2gr(scratch);
  moveFromFloat32(fpscratch, dest);
  as_bstrpick_d(scratch, scratch, CauseBitPos + CauseBitCount - 1, CauseBitPos);
  as_andi(scratch, scratch,
          CauseIOrVMask);  // masking for Inexact and Invalid flag.
  ma_b(scratch, zero, fail, Assembler::NotEqual);
}

void MacroAssemblerLOONG64Compat::convertDoubleToPtr(FloatRegister src,
                                                     Register dest, Label* fail,
                                                     bool negativeZeroCheck) {
  if (negativeZeroCheck) {
    moveFromDouble(src, dest);
    as_rotri_d(dest, dest, 63);
    ma_b(dest, Imm32(1), fail, Assembler::Equal);
  }

  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ScratchDoubleScope fpscratch(asMasm());
  // Truncate double to int64 ; if result is inexact or invalid fail.
  as_ftintrz_l_d(fpscratch, src);
  as_movfcsr2gr(scratch);
  moveFromDouble(fpscratch, dest);
  as_bstrpick_d(scratch, scratch, CauseBitPos + CauseBitCount - 1, CauseBitPos);
  as_andi(scratch, scratch,
          CauseIOrVMask);  // masking for Inexact and Invalid flag.
  ma_b(scratch, zero, fail, Assembler::NotEqual);
}

// Checks whether a float32 is representable as a 32-bit integer. If so, the
// integer is written to the output register. Otherwise, a bailout is taken to
// the given snapshot. This function overwrites the scratch float register.
void MacroAssemblerLOONG64Compat::convertFloat32ToInt32(
    FloatRegister src, Register dest, Label* fail, bool negativeZeroCheck) {
  if (negativeZeroCheck) {
    moveFromFloat32(src, dest);
    ma_b(dest, Imm32(INT32_MIN), fail, Assembler::Equal);
  }

  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ScratchFloat32Scope fpscratch(asMasm());
  as_ftintrz_w_s(fpscratch, src);
  as_movfcsr2gr(scratch);
  moveFromFloat32(fpscratch, dest);
  MOZ_ASSERT(CauseBitPos + CauseBitCount < 33);
  MOZ_ASSERT(CauseBitPos < 32);
  as_bstrpick_w(scratch, scratch, CauseBitPos + CauseBitCount - 1, CauseBitPos);
  as_andi(scratch, scratch, CauseIOrVMask);
  ma_b(scratch, zero, fail, Assembler::NotEqual);
}

void MacroAssemblerLOONG64Compat::convertFloat32ToDouble(FloatRegister src,
                                                         FloatRegister dest) {
  as_fcvt_d_s(dest, src);
}

void MacroAssemblerLOONG64Compat::convertInt32ToFloat32(Register src,
                                                        FloatRegister dest) {
  as_movgr2fr_w(dest, src);
  as_ffint_s_w(dest, dest);
}

void MacroAssemblerLOONG64Compat::convertInt32ToFloat32(const Address& src,
                                                        FloatRegister dest) {
  ma_fld_s(dest, src);
  as_ffint_s_w(dest, dest);
}

void MacroAssemblerLOONG64::ma_li(Register dest, CodeLabel* label) {
  BufferOffset bo = m_buffer.nextOffset();
  ma_liPatchable(dest, ImmWord(/* placeholder */ 0));
  label->patchAt()->bind(bo.getOffset());
  label->setLinkMode(CodeLabel::MoveImmediate);
}

void MacroAssemblerLOONG64::ma_li(Register dest, ImmWord imm) {
  const int64_t value = imm.value;

  if (is_intN(value, 12)) {
    as_addi_w(dest, zero, value);
    return;
  }

  if (is_uintN(value, 12)) {
    as_ori(dest, zero, value);
    return;
  }

  const uint32_t bits_11_0 = value & 0xfff;
  const uint32_t bits_31_12 = (value >> 12) & 0xfffff;
  const uint32_t bits_51_32 = (value >> 32) & 0xfffff;
  const uint32_t bits_63_52 = (value >> 52) & 0xfff;

  // The mnemonics of these imm-generating instructions may be a bit misleading.
  // From
  // <https://loongson.github.io/LoongArch-Documentation/LoongArch-Vol1-EN.html#_lu12i_w_lu32i_d_lu52i_d>:
  //
  // * LU12I.W: GR[rd] = SignExtend({si20, 12'b0}, GRLEN)
  //   Build from 0 and imm
  // * LU32I.D: GR[rd] = {SignExtend(si20, 32), GR[rd][31:0]}
  //   Concat(!) rd and imm
  // * LU52I.D: GR[rd] = {si12, GR[rj][51:0]}
  //   Concat rj(!), imm and store to rd
  //
  // These matter when building large numbers.

  if (is_intN(value, 32)) {
    as_lu12i_w(dest, bits_31_12);
  } else if (is_uintN(value, 32)) {
    as_lu12i_w(dest, bits_31_12);
    as_bstrins_d(dest, zero, 63, 32);
  } else if (is_intN(value, 52)) {
    as_lu12i_w(dest, bits_31_12);
    as_lu32i_d(dest, bits_51_32);
  } else if (is_uintN(value, 52)) {
    as_lu12i_w(dest, bits_31_12);
    as_lu32i_d(dest, bits_51_32);
    as_bstrins_d(dest, zero, 63, 52);
  } else if (bits_31_12 == 0 && bits_51_32 == 0) {
    as_lu52i_d(dest, zero, bits_63_52);
  } else if (bits_31_12 != 0 && (((bits_31_12 >> 19) & 1) == 0) &&
             bits_51_32 == 0) {
    as_lu12i_w(dest, bits_31_12);
    as_lu52i_d(dest, dest, bits_63_52);
  } else {
    as_lu12i_w(dest, bits_31_12);
    as_lu32i_d(dest, bits_51_32);
    as_lu52i_d(dest, dest, bits_63_52);
  }
  if (bits_11_0 != 0) {
    as_ori(dest, dest, bits_11_0);
  }
}

// This method generates lu32i_d, lu12i_w and ori instruction block that can be
// modified by UpdateLoad64Value, either during compilation (eg.
// Assembler::bind), or during execution (eg. jit::PatchJump).
void MacroAssemblerLOONG64::ma_liPatchable(Register dest, ImmPtr imm) {
  return ma_liPatchable(dest, ImmWord(uintptr_t(imm.value)));
}

void MacroAssemblerLOONG64::ma_liPatchable(Register dest, ImmWord imm,
                                           LiFlags flags) {
  // hi12, hi20, low20, low12
  if (Li64 == flags) {  // Li64: Imm data
    m_buffer.ensureSpace(4 * sizeof(uint32_t));
    as_lu12i_w(dest, imm.value >> 12 & 0xfffff);      // low20
    as_ori(dest, dest, imm.value & 0xfff);            // low12
    as_lu32i_d(dest, imm.value >> 32 & 0xfffff);      // hi20
    as_lu52i_d(dest, dest, imm.value >> 52 & 0xfff);  // hi12
  } else {                                            // Li48 address
    m_buffer.ensureSpace(3 * sizeof(uint32_t));
    as_lu12i_w(dest, imm.value >> 12 & 0xfffff);  // low20
    as_ori(dest, dest, imm.value & 0xfff);        // low12
    as_lu32i_d(dest, imm.value >> 32 & 0xfffff);  // hi20
  }
}

// Memory access ops.

FaultingCodeOffset MacroAssemblerLOONG64::ma_ld_b(Register dest,
                                                  Address address) {
  int32_t offset = address.offset;
  Register base = address.base;
  FaultingCodeOffset fco;

  if (is_intN(offset, 12)) {
    fco = FaultingCodeOffset(currentOffset());
    as_ld_b(dest, base, offset);
  } else if (base != dest) {
    ma_li(dest, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_ldx_b(dest, base, dest);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(base != scratch);
    ma_li(scratch, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_ldx_b(dest, base, scratch);
  }
  return fco;
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_ld_bu(Register dest,
                                                   Address address) {
  int32_t offset = address.offset;
  Register base = address.base;
  FaultingCodeOffset fco;

  if (is_intN(offset, 12)) {
    fco = FaultingCodeOffset(currentOffset());
    as_ld_bu(dest, base, offset);
  } else if (base != dest) {
    ma_li(dest, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_ldx_bu(dest, base, dest);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(base != scratch);
    ma_li(scratch, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_ldx_bu(dest, base, scratch);
  }
  return fco;
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_ld_h(Register dest,
                                                  Address address) {
  int32_t offset = address.offset;
  Register base = address.base;
  FaultingCodeOffset fco;

  if (is_intN(offset, 12)) {
    fco = FaultingCodeOffset(currentOffset());
    as_ld_h(dest, base, offset);
  } else if (base != dest) {
    ma_li(dest, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_ldx_h(dest, base, dest);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(base != scratch);
    ma_li(scratch, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_ldx_h(dest, base, scratch);
  }
  return fco;
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_ld_hu(Register dest,
                                                   Address address) {
  int32_t offset = address.offset;
  Register base = address.base;
  FaultingCodeOffset fco;

  if (is_intN(offset, 12)) {
    fco = FaultingCodeOffset(currentOffset());
    as_ld_hu(dest, base, offset);
  } else if (base != dest) {
    ma_li(dest, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_ldx_hu(dest, base, dest);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(base != scratch);
    ma_li(scratch, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_ldx_hu(dest, base, scratch);
  }
  return fco;
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_ld_w(Register dest,
                                                  Address address) {
  int32_t offset = address.offset;
  Register base = address.base;
  FaultingCodeOffset fco;

  if (is_intN(offset, 12)) {
    fco = FaultingCodeOffset(currentOffset());
    as_ld_w(dest, base, offset);
  } else if (base != dest) {
    ma_li(dest, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_ldx_w(dest, base, dest);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(base != scratch);
    ma_li(scratch, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_ldx_w(dest, base, scratch);
  }
  return fco;
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_ld_wu(Register dest,
                                                   Address address) {
  int32_t offset = address.offset;
  Register base = address.base;
  FaultingCodeOffset fco;

  if (is_intN(offset, 12)) {
    fco = FaultingCodeOffset(currentOffset());
    as_ld_wu(dest, base, offset);
  } else if (base != dest) {
    ma_li(dest, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_ldx_wu(dest, base, dest);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(base != scratch);
    ma_li(scratch, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_ldx_wu(dest, base, scratch);
  }
  return fco;
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_ld_d(Register dest,
                                                  Address address) {
  int32_t offset = address.offset;
  Register base = address.base;
  FaultingCodeOffset fco;

  if (is_intN(offset, 12)) {
    fco = FaultingCodeOffset(currentOffset());
    as_ld_d(dest, base, offset);
  } else if (base != dest) {
    ma_li(dest, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_ldx_d(dest, base, dest);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(base != scratch);
    ma_li(scratch, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_ldx_d(dest, base, scratch);
  }
  return fco;
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_st_b(Register src,
                                                  Address address) {
  int32_t offset = address.offset;
  Register base = address.base;
  FaultingCodeOffset fco;

  if (is_intN(offset, 12)) {
    fco = FaultingCodeOffset(currentOffset());
    as_st_b(src, base, offset);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(src != scratch);
    MOZ_ASSERT(base != scratch);
    ma_li(scratch, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_stx_b(src, base, scratch);
  }
  return fco;
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_st_h(Register src,
                                                  Address address) {
  int32_t offset = address.offset;
  Register base = address.base;
  FaultingCodeOffset fco;

  if (is_intN(offset, 12)) {
    fco = FaultingCodeOffset(currentOffset());
    as_st_h(src, base, offset);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(src != scratch);
    MOZ_ASSERT(base != scratch);
    ma_li(scratch, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_stx_h(src, base, scratch);
  }
  return fco;
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_st_w(Register src,
                                                  Address address) {
  int32_t offset = address.offset;
  Register base = address.base;
  FaultingCodeOffset fco;

  if (is_intN(offset, 12)) {
    fco = FaultingCodeOffset(currentOffset());
    as_st_w(src, base, offset);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(src != scratch);
    MOZ_ASSERT(base != scratch);
    ma_li(scratch, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_stx_w(src, base, scratch);
  }
  return fco;
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_st_d(Register src,
                                                  Address address) {
  int32_t offset = address.offset;
  Register base = address.base;
  FaultingCodeOffset fco;

  if (is_intN(offset, 12)) {
    fco = FaultingCodeOffset(currentOffset());
    as_st_d(src, base, offset);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(src != scratch);
    MOZ_ASSERT(base != scratch);
    ma_li(scratch, Imm32(offset));
    fco = FaultingCodeOffset(currentOffset());
    as_stx_d(src, base, scratch);
  }
  return fco;
}

// Arithmetic-based ops.

// Add.
void MacroAssemblerLOONG64::ma_add_d(Register rd, Register rj, Imm32 imm) {
  if (is_intN(imm.value, 12)) {
    as_addi_d(rd, rj, imm.value);
  } else if (rd != rj) {
    ma_li(rd, imm);
    as_add_d(rd, rj, rd);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(rj != scratch);
    ma_li(scratch, imm);
    as_add_d(rd, rj, scratch);
  }
}

void MacroAssemblerLOONG64::ma_add_d(Register rd, Register rj, ImmWord imm) {
  if (is_intN(imm.value, 12)) {
    as_addi_d(rd, rj, imm.value);
  } else if (rd != rj) {
    ma_li(rd, imm);
    as_add_d(rd, rj, rd);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(rj != scratch);
    ma_li(scratch, imm);
    as_add_d(rd, rj, scratch);
  }
}

void MacroAssemblerLOONG64::ma_add32TestOverflow(Register rd, Register rj,
                                                 Register rk, Label* overflow) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  as_add_d(scratch, rj, rk);
  as_add_w(rd, rj, rk);
  ma_b(rd, Register(scratch), overflow, Assembler::NotEqual);
}

void MacroAssemblerLOONG64::ma_add32TestOverflow(Register rd, Register rj,
                                                 Imm32 imm, Label* overflow) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  // Check for signed range because of as_addi_d
  if (is_intN(imm.value, 12)) {
    as_addi_d(scratch, rj, imm.value);
    as_addi_w(rd, rj, imm.value);
    ma_b(rd, scratch, overflow, Assembler::NotEqual);
  } else {
    ma_li(scratch, imm);
    ma_add32TestOverflow(rd, rj, scratch, overflow);
  }
}

void MacroAssemblerLOONG64::ma_addPtrTestOverflow(Register rd, Register rj,
                                                  Register rk,
                                                  Label* overflow) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(rd != scratch);

  if (rj == rk) {
    if (rj == rd) {
      as_or(scratch, rj, zero);
      rj = scratch;
    }

    as_add_d(rd, rj, rj);
    as_xor(scratch, rj, rd);
    ma_b(scratch, zero, overflow, Assembler::LessThan);
  } else {
    Register scratch2 = temps.Acquire();
    MOZ_ASSERT(rj != scratch);
    MOZ_ASSERT(rd != scratch2);

    if (rj == rd) {
      as_or(scratch2, rj, zero);
      rj = scratch2;
    }

    as_add_d(rd, rj, rk);
    // rd = rj + rk overflow conditions:
    //   1. rj < 0 and rd >= rk
    //   2. rj >= 0 and rd < rk
    as_slti(scratch, rj, 0);
    as_slt(scratch2, rd, rk);
    ma_b(scratch, Register(scratch2), overflow, Assembler::NotEqual);
  }
}

void MacroAssemblerLOONG64::ma_addPtrTestOverflow(Register rd, Register rj,
                                                  Imm32 imm, Label* overflow) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();

  if (imm.value == 0) {
    as_ori(rd, rj, 0);
    return;
  }

  if (rj == rd) {
    as_ori(scratch, rj, 0);
    rj = scratch;
  }

  ma_add_d(rd, rj, imm);

  if (imm.value > 0) {
    ma_b(rd, rj, overflow, Assembler::LessThan);
  } else {
    MOZ_ASSERT(imm.value < 0);
    ma_b(rd, rj, overflow, Assembler::GreaterThan);
  }
}

void MacroAssemblerLOONG64::ma_addPtrTestOverflow(Register rd, Register rj,
                                                  ImmWord imm,
                                                  Label* overflow) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();

  if (imm.value == 0) {
    as_ori(rd, rj, 0);
    return;
  }

  if (rj == rd) {
    MOZ_ASSERT(rj != scratch);
    as_ori(scratch, rj, 0);
    rj = scratch;
  }

  ma_li(rd, imm);
  as_add_d(rd, rj, rd);

  if (imm.value > 0) {
    ma_b(rd, rj, overflow, Assembler::LessThan);
  } else {
    MOZ_ASSERT(imm.value < 0);
    ma_b(rd, rj, overflow, Assembler::GreaterThan);
  }
}

void MacroAssemblerLOONG64::ma_addPtrTestCarry(Condition cond, Register rd,
                                               Register rj, Register rk,
                                               Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(rd != rk);
  MOZ_ASSERT(rd != scratch);
  as_add_d(rd, rj, rk);
  as_sltu(scratch, rd, rk);
  ma_b(scratch, Register(scratch), label,
       cond == Assembler::CarrySet ? Assembler::NonZero : Assembler::Zero);
}

void MacroAssemblerLOONG64::ma_addPtrTestCarry(Condition cond, Register rd,
                                               Register rj, Imm32 imm,
                                               Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();

  // Check for signed range because of as_addi_d
  if (is_intN(imm.value, 12)) {
    as_addi_d(rd, rj, imm.value);
    as_sltui(scratch, rd, imm.value);
    ma_b(scratch, scratch, label,
         cond == Assembler::CarrySet ? Assembler::NonZero : Assembler::Zero);
  } else {
    ma_li(scratch, imm);
    ma_addPtrTestCarry(cond, rd, rj, scratch, label);
  }
}

void MacroAssemblerLOONG64::ma_addPtrTestCarry(Condition cond, Register rd,
                                               Register rj, ImmWord imm,
                                               Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();

  // Check for signed range because of as_addi_d
  if (is_intN(imm.value, 12)) {
    as_addi_d(rd, rj, imm.value);
    as_sltui(scratch, rd, imm.value);
    ma_b(scratch, scratch, label,
         cond == Assembler::CarrySet ? Assembler::NonZero : Assembler::Zero);
  } else {
    ma_li(scratch, imm);
    ma_addPtrTestCarry(cond, rd, rj, scratch, label);
  }
}

void MacroAssemblerLOONG64::ma_addPtrTestSigned(Condition cond, Register rd,
                                                Register rj, Register rk,
                                                Label* taken) {
  MOZ_ASSERT(cond == Assembler::Signed || cond == Assembler::NotSigned);

  as_add_d(rd, rj, rk);
  ma_b(rd, rd, taken, cond);
}

void MacroAssemblerLOONG64::ma_addPtrTestSigned(Condition cond, Register rd,
                                                Register rj, Imm32 imm,
                                                Label* taken) {
  MOZ_ASSERT(cond == Assembler::Signed || cond == Assembler::NotSigned);

  ma_add_d(rd, rj, imm);
  ma_b(rd, rd, taken, cond);
}

void MacroAssemblerLOONG64::ma_addPtrTestSigned(Condition cond, Register rd,
                                                Register rj, ImmWord imm,
                                                Label* taken) {
  MOZ_ASSERT(cond == Assembler::Signed || cond == Assembler::NotSigned);

  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ma_li(scratch, imm);
  ma_addPtrTestSigned(cond, rd, rj, scratch, taken);
}

// Subtract.
void MacroAssemblerLOONG64::ma_sub_d(Register rd, Register rj, Imm32 imm) {
  if (is_intN(-imm.value, 12)) {
    as_addi_d(rd, rj, -imm.value);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    as_sub_d(rd, rj, scratch);
  }
}

void MacroAssemblerLOONG64::ma_sub_d(Register rd, Register rj, ImmWord imm) {
  if (is_intN(-imm.value, 12)) {
    as_addi_d(rd, rj, -imm.value);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    as_sub_d(rd, rj, scratch);
  }
}

void MacroAssemblerLOONG64::ma_sub32TestOverflow(Register rd, Register rj,
                                                 Register rk, Label* overflow) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  as_sub_d(scratch, rj, rk);
  as_sub_w(rd, rj, rk);
  ma_b(rd, Register(scratch), overflow, Assembler::NotEqual);
}

void MacroAssemblerLOONG64::ma_subPtrTestOverflow(Register rd, Register rj,
                                                  Register rk,
                                                  Label* overflow) {
  UseScratchRegisterScope temps2(asMasm());
  Register scratch2 = temps2.Acquire();
  MOZ_ASSERT_IF(rj == rd, rj != rk);
  MOZ_ASSERT(rj != scratch2);
  MOZ_ASSERT(rk != scratch2);
  MOZ_ASSERT(rd != scratch2);

  Register rj_copy = rj;

  if (rj == rd) {
    as_or(scratch2, rj, zero);
    rj_copy = scratch2;
  }

  {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(rd != scratch);

    as_sub_d(rd, rj, rk);
    // If the sign of rj and rk are the same, no overflow
    as_xor(scratch, rj_copy, rk);
    // Check if the sign of rd and rj are the same
    as_xor(scratch2, rd, rj_copy);
    as_and(scratch2, scratch2, scratch);
  }

  ma_b(scratch2, zero, overflow, Assembler::LessThan);
}

void MacroAssemblerLOONG64::ma_subPtrTestOverflow(Register rd, Register rj,
                                                  Imm32 imm, Label* overflow) {
  // TODO(loong64): Check subPtrTestOverflow
  MOZ_ASSERT(imm.value != INT32_MIN);
  ma_addPtrTestOverflow(rd, rj, Imm32(-imm.value), overflow);
}

void MacroAssemblerLOONG64::ma_mul_d(Register rd, Register rj, Imm32 imm) {
  // li handles the relocation.
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(rj != scratch);
  ma_li(scratch, imm);
  as_mul_d(rd, rj, scratch);
}

void MacroAssemblerLOONG64::ma_mul_d(Register rd, Register rj, ImmWord imm) {
  // li handles the relocation.
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(rj != scratch);
  ma_li(scratch, imm);
  as_mul_d(rd, rj, scratch);
}

void MacroAssemblerLOONG64::ma_mulh_d(Register rd, Register rj, Imm32 imm) {
  // li handles the relocation.
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(rj != scratch);
  ma_li(scratch, imm);
  as_mulh_d(rd, rj, scratch);
}

void MacroAssemblerLOONG64::ma_mulPtrTestOverflow(Register rd, Register rj,
                                                  Register rk,
                                                  Label* overflow) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
  MOZ_ASSERT(rd != scratch);

  if (rd == rj) {
    as_or(scratch, rj, zero);
    rj = scratch;
    rk = (rd == rk) ? rj : rk;
  } else if (rd == rk) {
    as_or(scratch, rk, zero);
    rk = scratch;
  }

  as_mul_d(rd, rj, rk);
  as_mulh_d(scratch, rj, rk);
  as_srai_d(scratch2, rd, 63);
  ma_b(scratch, Register(scratch2), overflow, Assembler::NotEqual);
}

// Memory.

FaultingCodeOffset MacroAssemblerLOONG64::ma_load(
    Register dest, Address address, LoadStoreSize size,
    LoadStoreExtension extension) {
  FaultingCodeOffset fco;
  UseScratchRegisterScope temps(*this);
  switch (size) {
    case SizeByte:
    case SizeHalfWord:
      if (is_intN(address.offset, 12)) {
        // This load can be represented by `ld.{b[u],h[u]} rd, rj, si12`.
        fco = FaultingCodeOffset(currentOffset());
        if (size == SizeByte) {
          if (extension == ZeroExtend) {
            as_ld_bu(dest, address.base, address.offset);
          } else {
            as_ld_b(dest, address.base, address.offset);
          }
        } else {
          if (extension == ZeroExtend) {
            as_ld_hu(dest, address.base, address.offset);
          } else {
            as_ld_h(dest, address.base, address.offset);
          }
        }
      } else {
        // The offset to this load needs to be built in a separate register.
        Register scratch = temps.Acquire();
        ma_li(scratch, Imm32(address.offset));

        fco = FaultingCodeOffset(currentOffset());
        if (size == SizeByte) {
          if (extension == ZeroExtend) {
            as_ldx_bu(dest, address.base, scratch);
          } else {
            as_ldx_b(dest, address.base, scratch);
          }
        } else {
          if (extension == ZeroExtend) {
            as_ldx_hu(dest, address.base, scratch);
          } else {
            as_ldx_h(dest, address.base, scratch);
          }
        }
      }
      break;
    case SizeWord:
    case SizeDouble:
      if (is_intN(address.offset, 12)) {
        // This load can be represented by `ld.{w[u],d} rd, rj, si12`.
        fco = FaultingCodeOffset(currentOffset());
        if (size == SizeWord) {
          if (extension == ZeroExtend) {
            as_ld_wu(dest, address.base, address.offset);
          } else {
            as_ld_w(dest, address.base, address.offset);
          }
        } else {
          as_ld_d(dest, address.base, address.offset);
        }
      } else if (is_intN(address.offset, 16) && (address.offset & 0x3) == 0 &&
                 (size == SizeDouble ||
                  (size == SizeWord && extension == SignExtend))) {
        // This load is aligned to 4 bytes and can be represented by
        // `ldptr.{w,d} rd, rj, si14`.
        fco = FaultingCodeOffset(currentOffset());
        if (size == SizeWord) {
          as_ldptr_w(dest, address.base, address.offset);
        } else {
          as_ldptr_d(dest, address.base, address.offset);
        }
      } else {
        // The offset to this load needs to be built in a separate register.
        Register scratch = temps.Acquire();
        ma_li(scratch, Imm32(address.offset));

        fco = FaultingCodeOffset(currentOffset());
        if (size == SizeWord) {
          if (extension == ZeroExtend) {
            as_ldx_wu(dest, address.base, scratch);
          } else {
            as_ldx_w(dest, address.base, scratch);
          }
        } else {
          as_ldx_d(dest, address.base, scratch);
        }
      }
      break;
    default:
      MOZ_CRASH("Invalid argument for ma_load");
  }
  return fco;
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_store(
    Register data, Address address, LoadStoreSize size,
    LoadStoreExtension extension) {
  FaultingCodeOffset fco;
  UseScratchRegisterScope temps(*this);
  switch (size) {
    case SizeByte:
    case SizeHalfWord:
      if (is_intN(address.offset, 12)) {
        // This store can be represented by `st.{b,h} rd, rj, si12`.
        fco = FaultingCodeOffset(currentOffset());
        if (size == SizeByte) {
          as_st_b(data, address.base, address.offset);
        } else {
          as_st_h(data, address.base, address.offset);
        }
      } else {
        // The offset to this store needs to be built in a separate register.
        Register scratch = temps.Acquire();
        ma_li(scratch, Imm32(address.offset));

        fco = FaultingCodeOffset(currentOffset());
        if (size == SizeByte) {
          as_stx_b(data, address.base, scratch);
        } else {
          as_stx_h(data, address.base, scratch);
        }
      }
      break;
    case SizeWord:
    case SizeDouble:
      if (is_intN(address.offset, 12)) {
        // This store can be represented by `st.{w,d} rd, rj, si12`.
        fco = FaultingCodeOffset(currentOffset());
        if (size == SizeWord) {
          as_st_w(data, address.base, address.offset);
        } else {
          as_st_d(data, address.base, address.offset);
        }
      } else if (is_intN(address.offset, 16) && (address.offset & 0x3) == 0) {
        // This store is aligned to 4 bytes and can be represented by
        // `stptr.{w,d} rd, rj, si14`.
        fco = FaultingCodeOffset(currentOffset());
        if (size == SizeWord) {
          as_stptr_w(data, address.base, address.offset);
        } else {
          as_stptr_d(data, address.base, address.offset);
        }
      } else {
        // The offset to this store needs to be built in a separate register.
        Register scratch = temps.Acquire();
        ma_li(scratch, Imm32(address.offset));

        fco = FaultingCodeOffset(currentOffset());
        if (size == SizeWord) {
          as_stx_w(data, address.base, scratch);
        } else {
          as_stx_d(data, address.base, scratch);
        }
      }
      break;
    default:
      MOZ_CRASH("Invalid argument for ma_store");
  }
  return fco;
}

void MacroAssemblerLOONG64Compat::computeScaledAddress(const BaseIndex& address,
                                                       Register dest) {
  Register base = address.base;
  Register index = address.index;
  int32_t shift = Imm32::ShiftOf(address.scale).value;

  if (shift) {
    MOZ_ASSERT(shift <= 4);
    as_alsl_d(dest, index, base, shift - 1);
  } else {
    as_add_d(dest, base, index);
  }
}

void MacroAssemblerLOONG64Compat::computeScaledAddress32(
    const BaseIndex& address, Register dest) {
  Register base = address.base;
  Register index = address.index;
  int32_t shift = Imm32::ShiftOf(address.scale).value;

  if (shift) {
    MOZ_ASSERT(shift <= 4);
    as_alsl_w(dest, index, base, shift - 1);
  } else {
    as_add_w(dest, base, index);
  }
}

void MacroAssemblerLOONG64::ma_pop(Register r) {
  MOZ_ASSERT(r != StackPointer);
  as_ld_d(r, StackPointer, 0);
  as_addi_d(StackPointer, StackPointer, sizeof(intptr_t));
}

void MacroAssemblerLOONG64::ma_push(Register r) {
  if (r == StackPointer) {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    as_or(scratch, r, zero);
    as_addi_d(StackPointer, StackPointer, -int32_t(sizeof(intptr_t)));
    as_st_d(scratch, StackPointer, 0);
  } else {
    as_addi_d(StackPointer, StackPointer, -int32_t(sizeof(intptr_t)));
    as_st_d(r, StackPointer, 0);
  }
}

// Branches when done from within loongarch-specific code.
void MacroAssemblerLOONG64::ma_b(Register lhs, ImmWord imm, Label* label,
                                 Condition c, JumpKind jumpKind) {
  if (imm.value <= INT32_MAX) {
    ma_b(lhs, Imm32(uint32_t(imm.value)), label, c, jumpKind);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(lhs != scratch);
    ma_li(scratch, imm);
    ma_b(lhs, Register(scratch), label, c, jumpKind, scratch);
  }
}

void MacroAssemblerLOONG64::ma_b(Register lhs, Address addr, Label* label,
                                 Condition c, JumpKind jumpKind) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(lhs != scratch);
  ma_ld_d(scratch, addr);
  ma_b(lhs, Register(scratch), label, c, jumpKind, scratch);
}

void MacroAssemblerLOONG64::ma_b(Address addr, Imm32 imm, Label* label,
                                 Condition c, JumpKind jumpKind) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ma_ld_d(scratch, addr);
  ma_b(Register(scratch), imm, label, c, jumpKind);
}

void MacroAssemblerLOONG64::ma_b(Address addr, ImmGCPtr imm, Label* label,
                                 Condition c, JumpKind jumpKind) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ma_ld_d(scratch, addr);
  ma_b(Register(scratch), imm, label, c, jumpKind);
}

void MacroAssemblerLOONG64::ma_bl(Label* label) {
  spew("branch .Llabel %p\n", label);
  if (label->bound()) {
    // Generate the long jump for calls because return address has to be
    // the address after the reserved block.
    addLongJump(nextOffset(), BufferOffset(label->offset()));
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    ma_liPatchable(scratch, ImmWord(LabelBase::INVALID_OFFSET));
    as_jirl(ra, scratch, BOffImm16(0));
    return;
  }

  // Second word holds a pointer to the next branch in label's chain.
  uint32_t nextInChain =
      label->used() ? label->offset() : LabelBase::INVALID_OFFSET;

  // Make the whole branch continous in the buffer. The '5'
  // instructions are writing at below.
  m_buffer.ensureSpace(5 * sizeof(uint32_t));

  spew("bal .Llabel %p\n", label);
  BufferOffset bo = writeInst(getBranchCode(BranchIsCall).encode());
  writeInst(nextInChain);
  if (!oom()) {
    label->use(bo.getOffset());
  }
  // Leave space for long jump.
  as_nop();
  as_nop();
  as_nop();
}

void MacroAssemblerLOONG64::branchWithCode(InstImm code, Label* label,
                                           JumpKind jumpKind,
                                           Register scratch) {
  // simply output the pointer of one label as its id,
  // notice that after one label destructor, the pointer will be reused.
  spew("branch .Llabel %p", label);
  MOZ_ASSERT(code.encode() !=
             InstImm(op_jirl, BOffImm16(0), zero, ra).encode());
  InstImm inst_beq = InstImm(op_beq, BOffImm16(0), zero, zero);

  if (label->bound()) {
    int32_t offset = label->offset() - m_buffer.nextOffset().getOffset();

    if (BOffImm16::IsInRange(offset)) {
      jumpKind = ShortJump;
    }

    // ShortJump
    if (jumpKind == ShortJump) {
      MOZ_ASSERT(BOffImm16::IsInRange(offset));

      if (code.extractBitField(31, 26) == ((uint32_t)op_bcz >> 26)) {
        code.setImm21(offset);
      } else {
        code.setBOffImm16(BOffImm16(offset));
      }
#ifdef JS_JITSPEW
      decodeBranchInstAndSpew(code);
#endif
      writeInst(code.encode());
      return;
    }

    // LongJump
    if (code.encode() == inst_beq.encode()) {
      // Handle long jump
      addLongJump(nextOffset(), BufferOffset(label->offset()));
      if (scratch == Register::Invalid()) {
        UseScratchRegisterScope temps(asMasm());
        Register scratch = temps.Acquire();
        ma_liPatchable(scratch, ImmWord(LabelBase::INVALID_OFFSET));
        as_jirl(zero, scratch, BOffImm16(0));  // jr scratch
      } else {
        ma_liPatchable(scratch, ImmWord(LabelBase::INVALID_OFFSET));
        as_jirl(zero, scratch, BOffImm16(0));  // jr scratch
      }
      as_nop();
      return;
    }

    // OpenLongJump
    // Handle long conditional branch, the target offset is based on self,
    // point to next instruction of nop at below.
    spew("invert branch .Llabel %p", label);
    InstImm code_r = invertBranch(code, BOffImm16(5 * sizeof(uint32_t)));
#ifdef JS_JITSPEW
    decodeBranchInstAndSpew(code_r);
#endif
    writeInst(code_r.encode());
    addLongJump(nextOffset(), BufferOffset(label->offset()));
    if (scratch == Register::Invalid()) {
      UseScratchRegisterScope temps(asMasm());
      Register scratch = temps.Acquire();
      ma_liPatchable(scratch, ImmWord(LabelBase::INVALID_OFFSET));
      as_jirl(zero, scratch, BOffImm16(0));  // jr scratch
    } else {
      ma_liPatchable(scratch, ImmWord(LabelBase::INVALID_OFFSET));
      as_jirl(zero, scratch, BOffImm16(0));  // jr scratch
    }
    as_nop();
    return;
  }

  // Generate open jump and link it to a label.

  // Second word holds a pointer to the next branch in label's chain.
  uint32_t nextInChain =
      label->used() ? label->offset() : LabelBase::INVALID_OFFSET;

  if (jumpKind == ShortJump) {
    // Make the whole branch continous in the buffer.
    m_buffer.ensureSpace(2 * sizeof(uint32_t));

    // Indicate that this is short jump with offset 4.
    code.setBOffImm16(BOffImm16(4));
#ifdef JS_JITSPEW
    decodeBranchInstAndSpew(code);
#endif
    BufferOffset bo = writeInst(code.encode());
    writeInst(nextInChain);
    if (!oom()) {
      label->use(bo.getOffset());
    }
    return;
  }

  bool conditional = code.encode() != inst_beq.encode();

  // Make the whole branch continous in the buffer. The '5'
  // instructions are writing at below (contain conditional nop).
  m_buffer.ensureSpace(5 * sizeof(uint32_t));

#ifdef JS_JITSPEW
  decodeBranchInstAndSpew(code);
#endif
  BufferOffset bo = writeInst(code.encode());  // invert
  writeInst(nextInChain);
  if (!oom()) {
    label->use(bo.getOffset());
  }
  // Leave space for potential long jump.
  as_nop();
  as_nop();
  if (conditional) {
    as_nop();
  }
}

void MacroAssemblerLOONG64::ma_cmp_set(Register rd, Register rj, ImmWord imm,
                                       Condition c) {
  if (imm.value <= INT32_MAX) {
    ma_cmp_set(rd, rj, Imm32(uint32_t(imm.value)), c);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    ma_cmp_set(rd, rj, scratch, c);
  }
}

void MacroAssemblerLOONG64::ma_cmp_set(Register rd, Register rj, ImmPtr imm,
                                       Condition c) {
  ma_cmp_set(rd, rj, ImmWord(uintptr_t(imm.value)), c);
}

void MacroAssemblerLOONG64::ma_cmp_set(Register rd, Register rj, ImmGCPtr imm,
                                       Condition c) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ma_li(scratch, imm);
  ma_cmp_set(rd, rj, scratch, c);
}

void MacroAssemblerLOONG64::ma_cmp_set(Register rd, Address address, Imm32 imm,
                                       Condition c) {
  // TODO(loong64): 32-bit ma_cmp_set?
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ma_ld_w(scratch, address);
  ma_cmp_set(rd, Register(scratch), imm, c);
}

void MacroAssemblerLOONG64::ma_cmp_set(Register rd, Address address,
                                       Register rk, Condition c) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ma_ld_d(scratch, address);
  ma_cmp_set(rd, scratch, rk, c);
}

void MacroAssemblerLOONG64::ma_cmp_set(Register rd, Address address,
                                       ImmWord imm, Condition c) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ma_ld_d(scratch, address);
  ma_cmp_set(rd, Register(scratch), imm, c);
}

// fp instructions
void MacroAssemblerLOONG64::ma_lid(FloatRegister dest, double value) {
  ImmWord imm(mozilla::BitwiseCast<uint64_t>(value));

  if (imm.value != 0) {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    moveToDouble(scratch, dest);
  } else {
    moveToDouble(zero, dest);
  }
}

void MacroAssemblerLOONG64::ma_mv(FloatRegister src, ValueOperand dest) {
  as_movfr2gr_d(dest.valueReg(), src);
}

void MacroAssemblerLOONG64::ma_mv(ValueOperand src, FloatRegister dest) {
  as_movgr2fr_d(dest, src.valueReg());
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_fld_s(FloatRegister dest,
                                                   Address address) {
  int32_t offset = address.offset;
  Register base = address.base;
  js::wasm::FaultingCodeOffset fco;

  if (is_intN(offset, 12)) {
    fco = js::wasm::FaultingCodeOffset(currentOffset());
    as_fld_s(dest, base, offset);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(base != scratch);
    ma_li(scratch, Imm32(offset));
    fco = js::wasm::FaultingCodeOffset(currentOffset());
    as_fldx_s(dest, base, scratch);
  }
  return fco;
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_fld_d(FloatRegister dest,
                                                   Address address) {
  int32_t offset = address.offset;
  Register base = address.base;
  js::wasm::FaultingCodeOffset fco;

  if (is_intN(offset, 12)) {
    fco = js::wasm::FaultingCodeOffset(currentOffset());
    as_fld_d(dest, base, offset);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(base != scratch);
    ma_li(scratch, Imm32(offset));
    fco = js::wasm::FaultingCodeOffset(currentOffset());
    as_fldx_d(dest, base, scratch);
  }
  return fco;
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_fst_s(FloatRegister src,
                                                   Address address) {
  int32_t offset = address.offset;
  Register base = address.base;
  js::wasm::FaultingCodeOffset fco;

  if (is_intN(offset, 12)) {
    fco = js::wasm::FaultingCodeOffset(currentOffset());
    as_fst_s(src, base, offset);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(base != scratch);
    ma_li(scratch, Imm32(offset));
    fco = js::wasm::FaultingCodeOffset(currentOffset());
    as_fstx_s(src, base, scratch);
  }
  return fco;
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_fst_d(FloatRegister src,
                                                   Address address) {
  int32_t offset = address.offset;
  Register base = address.base;
  js::wasm::FaultingCodeOffset fco;

  if (is_intN(offset, 12)) {
    fco = js::wasm::FaultingCodeOffset(currentOffset());
    as_fst_d(src, base, offset);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(base != scratch);
    ma_li(scratch, Imm32(offset));
    fco = js::wasm::FaultingCodeOffset(currentOffset());
    as_fstx_d(src, base, scratch);
  }
  return fco;
}

void MacroAssemblerLOONG64::ma_pop(FloatRegister f) {
  if (f.isDouble()) {
    as_fld_d(f, StackPointer, 0);
  } else {
    MOZ_ASSERT(f.isSingle(), "simd128 is not supported");
    as_fld_s(f, StackPointer, 0);
  }
  // See also MacroAssemblerLOONG64::ma_push -- Free space for double even when
  // storing a float.
  as_addi_d(StackPointer, StackPointer, sizeof(double));
}

void MacroAssemblerLOONG64::ma_push(FloatRegister f) {
  // We allocate space for double even when storing a float.
  as_addi_d(StackPointer, StackPointer, -int32_t(sizeof(double)));
  if (f.isDouble()) {
    as_fst_d(f, StackPointer, 0);
  } else {
    MOZ_ASSERT(f.isSingle(), "simd128 is not supported");
    as_fst_s(f, StackPointer, 0);
  }
}

void MacroAssemblerLOONG64::ma_li(Register dest, ImmGCPtr ptr) {
  writeDataRelocation(ptr);
  asMasm().ma_liPatchable(dest, ImmPtr(ptr.value));
}

void MacroAssemblerLOONG64::ma_li(Register dest, Imm32 imm) {
  if (is_intN(imm.value, 12)) {
    as_addi_w(dest, zero, imm.value);
  } else if (is_uintN(imm.value, 12)) {
    as_ori(dest, zero, imm.value & 0xfff);
  } else {
    as_lu12i_w(dest, imm.value >> 12 & 0xfffff);
    if (imm.value & 0xfff) {
      as_ori(dest, dest, imm.value & 0xfff);
    }
  }
}

// This method generates lu12i_w and ori instruction pair that can be modified
// by UpdateLuiOriValue, either during compilation (eg. Assembler::bind), or
// during execution (eg. jit::PatchJump).
void MacroAssemblerLOONG64::ma_liPatchable(Register dest, Imm32 imm) {
  m_buffer.ensureSpace(2 * sizeof(uint32_t));
  as_lu12i_w(dest, imm.value >> 12 & 0xfffff);
  as_ori(dest, dest, imm.value & 0xfff);
}

void MacroAssemblerLOONG64::ma_fmovz(FloatFormat fmt, FloatRegister fd,
                                     FloatRegister fj, Register rk) {
  Label done;
  ma_b(rk, zero, &done, Assembler::NotEqual);
  if (fmt == SingleFloat) {
    as_fmov_s(fd, fj);
  } else {
    as_fmov_d(fd, fj);
  }
  bind(&done);
}

void MacroAssemblerLOONG64::ma_fmovn(FloatFormat fmt, FloatRegister fd,
                                     FloatRegister fj, Register rk) {
  Label done;
  ma_b(rk, zero, &done, Assembler::Equal);
  if (fmt == SingleFloat) {
    as_fmov_s(fd, fj);
  } else {
    as_fmov_d(fd, fj);
  }
  bind(&done);
}

void MacroAssemblerLOONG64::ma_and(Register rd, Register rj, Imm32 imm) {
  if (is_uintN(imm.value, 12)) {
    as_andi(rd, rj, imm.value);
  } else if (rd != rj) {
    ma_li(rd, imm);
    as_and(rd, rj, rd);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(rj != scratch);
    ma_li(scratch, imm);
    as_and(rd, rj, scratch);
  }
}

void MacroAssemblerLOONG64::ma_or(Register rd, Register rj, Imm32 imm) {
  if (is_uintN(imm.value, 12)) {
    as_ori(rd, rj, imm.value);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(rj != scratch);
    ma_li(scratch, imm);
    as_or(rd, rj, scratch);
  }
}

void MacroAssemblerLOONG64::ma_xor(Register rd, Register rj, Imm32 imm) {
  if (is_uintN(imm.value, 12)) {
    as_xori(rd, rj, imm.value);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(rj != scratch);
    ma_li(scratch, imm);
    as_xor(rd, rj, scratch);
  }
}

// Arithmetic-based ops.

// Add.
void MacroAssemblerLOONG64::ma_add_w(Register rd, Register rj, Imm32 imm) {
  if (is_intN(imm.value, 12)) {
    as_addi_w(rd, rj, imm.value);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(rj != scratch);
    ma_li(scratch, imm);
    as_add_w(rd, rj, scratch);
  }
}

void MacroAssemblerLOONG64::ma_add32TestCarry(Condition cond, Register rd,
                                              Register rj, Register rk,
                                              Label* overflow) {
  MOZ_ASSERT(cond == Assembler::CarrySet || cond == Assembler::CarryClear);
  MOZ_ASSERT_IF(rd == rj, rk != rd);
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  as_add_w(rd, rj, rk);
  as_sltu(scratch, rd, rd == rj ? rk : rj);
  ma_b(Register(scratch), Register(scratch), overflow,
       cond == Assembler::CarrySet ? Assembler::NonZero : Assembler::Zero);
}

void MacroAssemblerLOONG64::ma_add32TestCarry(Condition cond, Register rd,
                                              Register rj, Imm32 imm,
                                              Label* overflow) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(rj != scratch);
  ma_li(scratch, imm);
  ma_add32TestCarry(cond, rd, rj, scratch, overflow);
}

// Subtract.
void MacroAssemblerLOONG64::ma_sub_w(Register rd, Register rj, Imm32 imm) {
  if (is_intN(-imm.value, 12)) {
    as_addi_w(rd, rj, -imm.value);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(rj != scratch);
    ma_li(scratch, imm);
    as_sub_w(rd, rj, scratch);
  }
}

void MacroAssemblerLOONG64::ma_sub_w(Register rd, Register rj, Register rk) {
  as_sub_w(rd, rj, rk);
}

void MacroAssemblerLOONG64::ma_sub32TestOverflow(Register rd, Register rj,
                                                 Imm32 imm, Label* overflow) {
  if (imm.value != INT32_MIN) {
    asMasm().ma_add32TestOverflow(rd, rj, Imm32(-imm.value), overflow);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(rj != scratch);
    ma_li(scratch, Imm32(imm.value));
    asMasm().ma_sub32TestOverflow(rd, rj, scratch, overflow);
  }
}

void MacroAssemblerLOONG64::ma_mul(Register rd, Register rj, Imm32 imm) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(rj != scratch);
  ma_li(scratch, imm);
  as_mul_w(rd, rj, scratch);
}

void MacroAssemblerLOONG64::ma_mul32TestOverflow(Register rd, Register rj,
                                                 Register rk, Label* overflow) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  as_mul_d(rd, rj, rk);
  as_slli_w(scratch, rd, 0);
  ma_b(rd, scratch, overflow, Assembler::NotEqual);
}

void MacroAssemblerLOONG64::ma_mul32TestOverflow(Register rd, Register rj,
                                                 Imm32 imm, Label* overflow) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ma_li(scratch, imm);
  as_mul_d(rd, rj, scratch);
  as_slli_w(scratch, rd, 0);
  ma_b(rd, scratch, overflow, Assembler::NotEqual);
}

void MacroAssemblerLOONG64::ma_mod_mask(Register src, Register dest,
                                        Register hold, Register remain,
                                        int32_t shift, Label* negZero) {
  // MATH:
  // We wish to compute x % (1<<y) - 1 for a known constant, y.
  // First, let b = (1<<y) and C = (1<<y)-1, then think of the 32 bit
  // dividend as a number in base b, namely
  // c_0*1 + c_1*b + c_2*b^2 ... c_n*b^n
  // now, since both addition and multiplication commute with modulus,
  // x % C == (c_0 + c_1*b + ... + c_n*b^n) % C ==
  // (c_0 % C) + (c_1%C) * (b % C) + (c_2 % C) * (b^2 % C)...
  // now, since b == C + 1, b % C == 1, and b^n % C == 1
  // this means that the whole thing simplifies to:
  // c_0 + c_1 + c_2 ... c_n % C
  // each c_n can easily be computed by a shift/bitextract, and the modulus
  // can be maintained by simply subtracting by C whenever the number gets
  // over C.
  int32_t mask = (1 << shift) - 1;
  Label head, negative, sumSigned, done;

  // hold holds -1 if the value was negative, 1 otherwise.
  // remain holds the remaining bits that have not been processed
  // SecondScratchReg serves as a temporary location to store extracted bits
  // into as well as holding the trial subtraction as a temp value dest is
  // the accumulator (and holds the final result)

  // move the whole value into the remain.
  as_or(remain, src, zero);
  // Zero out the dest.
  ma_li(dest, Imm32(0));
  // Set the hold appropriately.
  ma_b(remain, remain, &negative, Signed, ShortJump);
  ma_li(hold, Imm32(1));
  ma_b(&head, ShortJump);

  bind(&negative);
  ma_li(hold, Imm32(-1));
  as_sub_w(remain, zero, remain);

  // Begin the main loop.
  bind(&head);

  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  // Extract the bottom bits into SecondScratchReg.
  ma_and(scratch, remain, Imm32(mask));
  // Add those bits to the accumulator.
  as_add_w(dest, dest, scratch);
  // Do a trial subtraction
  ma_sub_w(scratch, dest, Imm32(mask));
  // If (sum - C) > 0, store sum - C back into sum, thus performing a
  // modulus.
  ma_b(scratch, Register(scratch), &sumSigned, Signed, ShortJump);
  as_or(dest, scratch, zero);
  bind(&sumSigned);
  // Get rid of the bits that we extracted before.
  as_srli_w(remain, remain, shift);
  // If the shift produced zero, finish, otherwise, continue in the loop.
  ma_b(remain, remain, &head, NonZero, ShortJump);
  // Check the hold to see if we need to negate the result.
  ma_b(hold, hold, &done, NotSigned, ShortJump);

  if (negZero != nullptr) {
    // Jump out in case of negative zero.
    ma_b(dest, dest, negZero, Zero);
  }
  // If the hold was non-zero, negate the result to be in line with
  // what JS wants
  as_sub_w(dest, zero, dest);

  bind(&done);
}

// Memory.

FaultingCodeOffset MacroAssemblerLOONG64::ma_load(
    Register dest, const BaseIndex& src, LoadStoreSize size,
    LoadStoreExtension extension) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  asMasm().computeScaledAddress(src, scratch);
  return asMasm().ma_load(dest, Address(scratch, src.offset), size, extension);
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_store(
    Register data, const BaseIndex& dest, LoadStoreSize size,
    LoadStoreExtension extension) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  asMasm().computeScaledAddress(dest, scratch);
  ma_add_d(scratch, scratch, Imm32(dest.offset));
  return asMasm().ma_store(data, Address(scratch, 0), size, extension);
}

void MacroAssemblerLOONG64::ma_store(Imm32 imm, const BaseIndex& dest,
                                     LoadStoreSize size,
                                     LoadStoreExtension extension) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch2 = temps.Acquire();
  // Make sure that scratch2 contains absolute address so that offset is 0.
  asMasm().computeEffectiveAddress(dest, scratch2);

  // Scrach register is free now, use it for loading imm value
  Register scratch = temps.Acquire();
  ma_li(scratch, imm);

  asMasm().ma_store(scratch, Address(scratch2, 0), size, extension);
}

// Branches when done from within loongarch-specific code.
// TODO(loong64) Optimize ma_b
void MacroAssemblerLOONG64::ma_b(Register lhs, Register rhs, Label* label,
                                 Condition c, JumpKind jumpKind,
                                 Register scratch) {
  switch (c) {
    case Equal:
    case NotEqual:
      asMasm().branchWithCode(getBranchCode(lhs, rhs, c), label, jumpKind,
                              scratch);
      break;
    case Always:
      ma_b(label, jumpKind);
      break;
    case Zero:
    case NonZero:
    case Signed:
    case NotSigned:
      MOZ_ASSERT(lhs == rhs);
      asMasm().branchWithCode(getBranchCode(lhs, c), label, jumpKind, scratch);
      break;
    default: {
      UseScratchRegisterScope temps(*this);
      Register scratch = temps.Acquire();
      Condition cond = ma_cmp(scratch, lhs, rhs, c);
      asMasm().branchWithCode(getBranchCode(scratch, cond), label, jumpKind,
                              scratch);
      break;
    }
  }
}

void MacroAssemblerLOONG64::ma_b(Register lhs, Imm32 imm, Label* label,
                                 Condition c, JumpKind jumpKind) {
  MOZ_ASSERT(c != Overflow);
  if (imm.value == 0) {
    if (c == Always || c == AboveOrEqual) {
      ma_b(label, jumpKind);
    } else if (c == Below) {
      ;  // This condition is always false. No branch required.
    } else {
      asMasm().branchWithCode(getBranchCode(lhs, c), label, jumpKind);
    }
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    switch (c) {
      case Equal:
      case NotEqual:
        MOZ_ASSERT(lhs != scratch);
        ma_li(scratch, imm);
        ma_b(lhs, scratch, label, c, jumpKind);
        break;
      default:
        Condition cond = ma_cmp(scratch, lhs, imm, c);
        asMasm().branchWithCode(getBranchCode(scratch, cond), label, jumpKind);
    }
  }
}

void MacroAssemblerLOONG64::ma_b(Register lhs, ImmPtr imm, Label* l,
                                 Condition c, JumpKind jumpKind) {
  asMasm().ma_b(lhs, ImmWord(uintptr_t(imm.value)), l, c, jumpKind);
}

void MacroAssemblerLOONG64::ma_b(Label* label, JumpKind jumpKind) {
  asMasm().branchWithCode(getBranchCode(BranchIsJump), label, jumpKind);
}

Assembler::Condition MacroAssemblerLOONG64::ma_cmp(Register dest, Register lhs,
                                                   Register rhs, Condition c) {
  switch (c) {
    case Above:
      // bgtu s,t,label =>
      //   sltu at,t,s
      //   bne at,$zero,offs
      as_sltu(dest, rhs, lhs);
      return NotEqual;
    case AboveOrEqual:
      // bgeu s,t,label =>
      //   sltu at,s,t
      //   beq at,$zero,offs
      as_sltu(dest, lhs, rhs);
      return Equal;
    case Below:
      // bltu s,t,label =>
      //   sltu at,s,t
      //   bne at,$zero,offs
      as_sltu(dest, lhs, rhs);
      return NotEqual;
    case BelowOrEqual:
      // bleu s,t,label =>
      //   sltu at,t,s
      //   beq at,$zero,offs
      as_sltu(dest, rhs, lhs);
      return Equal;
    case GreaterThan:
      // bgt s,t,label =>
      //   slt at,t,s
      //   bne at,$zero,offs
      as_slt(dest, rhs, lhs);
      return NotEqual;
    case GreaterThanOrEqual:
      // bge s,t,label =>
      //   slt at,s,t
      //   beq at,$zero,offs
      as_slt(dest, lhs, rhs);
      return Equal;
    case LessThan:
      // blt s,t,label =>
      //   slt at,s,t
      //   bne at,$zero,offs
      as_slt(dest, lhs, rhs);
      return NotEqual;
    case LessThanOrEqual:
      // ble s,t,label =>
      //   slt at,t,s
      //   beq at,$zero,offs
      as_slt(dest, rhs, lhs);
      return Equal;
    default:
      MOZ_CRASH("Invalid condition.");
  }
  return Always;
}

Assembler::Condition MacroAssemblerLOONG64::ma_cmp(Register dest, Register lhs,
                                                   Imm32 imm, Condition c) {
  UseScratchRegisterScope temps(*this);

  switch (c) {
    case Above:
    case BelowOrEqual:
      if (imm.value != 0x7fffffff && is_intN(imm.value + 1, 12) &&
          imm.value != -1) {
        // lhs <= rhs via lhs < rhs + 1 if rhs + 1 does not overflow
        as_sltui(dest, lhs, imm.value + 1);

        return (c == BelowOrEqual ? NotEqual : Equal);
      } else {
        Register scratch = dest == lhs ? temps.Acquire() : dest;
        ma_li(scratch, imm);
        as_sltu(dest, scratch, lhs);
        return (c == BelowOrEqual ? Equal : NotEqual);
      }
    case AboveOrEqual:
    case Below:
      if (is_intN(imm.value, 12)) {
        as_sltui(dest, lhs, imm.value);
      } else {
        Register scratch = dest == lhs ? temps.Acquire() : dest;
        ma_li(scratch, imm);
        as_sltu(dest, lhs, scratch);
      }
      return (c == AboveOrEqual ? Equal : NotEqual);
    case GreaterThan:
    case LessThanOrEqual:
      if (imm.value != 0x7fffffff && is_intN(imm.value + 1, 12)) {
        // lhs <= rhs via lhs < rhs + 1.
        as_slti(dest, lhs, imm.value + 1);
        return (c == LessThanOrEqual ? NotEqual : Equal);
      } else {
        Register scratch = dest == lhs ? temps.Acquire() : dest;
        ma_li(scratch, imm);
        as_slt(dest, scratch, lhs);
        return (c == LessThanOrEqual ? Equal : NotEqual);
      }
    case GreaterThanOrEqual:
    case LessThan:
      if (is_intN(imm.value, 12)) {
        as_slti(dest, lhs, imm.value);
      } else {
        Register scratch = dest == lhs ? temps.Acquire() : dest;
        ma_li(scratch, imm);
        as_slt(dest, lhs, scratch);
      }
      return (c == GreaterThanOrEqual ? Equal : NotEqual);
    default:
      MOZ_CRASH("Invalid condition.");
  }
  return Always;
}

// fp instructions
void MacroAssemblerLOONG64::ma_lis(FloatRegister dest, float value) {
  Imm32 imm(mozilla::BitwiseCast<uint32_t>(value));

  if (imm.value != 0) {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    moveToFloat32(scratch, dest);
  } else {
    moveToFloat32(zero, dest);
  }
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_fst_d(FloatRegister ft,
                                                   BaseIndex address) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  asMasm().computeScaledAddress(address, scratch);
  return asMasm().ma_fst_d(ft, Address(scratch, address.offset));
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_fst_s(FloatRegister ft,
                                                   BaseIndex address) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  asMasm().computeScaledAddress(address, scratch);
  return asMasm().ma_fst_s(ft, Address(scratch, address.offset));
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_fld_d(FloatRegister ft,
                                                   const BaseIndex& src) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  asMasm().computeScaledAddress(src, scratch);
  return asMasm().ma_fld_d(ft, Address(scratch, src.offset));
}

FaultingCodeOffset MacroAssemblerLOONG64::ma_fld_s(FloatRegister ft,
                                                   const BaseIndex& src) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  asMasm().computeScaledAddress(src, scratch);
  return asMasm().ma_fld_s(ft, Address(scratch, src.offset));
}

void MacroAssemblerLOONG64::ma_bc_s(FloatRegister lhs, FloatRegister rhs,
                                    Label* label, DoubleCondition c,
                                    JumpKind jumpKind, FPConditionBit fcc) {
  compareFloatingPoint(SingleFloat, lhs, rhs, c, fcc);
  asMasm().branchWithCode(getBranchCode(fcc), label, jumpKind);
}

void MacroAssemblerLOONG64::ma_bc_d(FloatRegister lhs, FloatRegister rhs,
                                    Label* label, DoubleCondition c,
                                    JumpKind jumpKind, FPConditionBit fcc) {
  compareFloatingPoint(DoubleFloat, lhs, rhs, c, fcc);
  asMasm().branchWithCode(getBranchCode(fcc), label, jumpKind);
}

void MacroAssemblerLOONG64::ma_call(ImmPtr dest) {
  asMasm().ma_liPatchable(CallReg, dest);
  as_jirl(ra, CallReg, BOffImm16(0));
}

void MacroAssemblerLOONG64::ma_jump(ImmPtr dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  asMasm().ma_liPatchable(scratch, dest);
  as_jirl(zero, scratch, BOffImm16(0));
}

void MacroAssemblerLOONG64::ma_cmp_set(Register rd, Register rj, Register rk,
                                       Condition c) {
  switch (c) {
    case Equal:
      // seq d,s,t =>
      //   xor d,s,t
      //   sltiu d,d,1
      as_xor(rd, rj, rk);
      as_sltui(rd, rd, 1);
      break;
    case NotEqual:
      // sne d,s,t =>
      //   xor d,s,t
      //   sltu d,$zero,d
      as_xor(rd, rj, rk);
      as_sltu(rd, zero, rd);
      break;
    case Above:
      // sgtu d,s,t =>
      //   sltu d,t,s
      as_sltu(rd, rk, rj);
      break;
    case AboveOrEqual:
      // sgeu d,s,t =>
      //   sltu d,s,t
      //   xori d,d,1
      as_sltu(rd, rj, rk);
      as_xori(rd, rd, 1);
      break;
    case Below:
      // sltu d,s,t
      as_sltu(rd, rj, rk);
      break;
    case BelowOrEqual:
      // sleu d,s,t =>
      //   sltu d,t,s
      //   xori d,d,1
      as_sltu(rd, rk, rj);
      as_xori(rd, rd, 1);
      break;
    case GreaterThan:
      // sgt d,s,t =>
      //   slt d,t,s
      as_slt(rd, rk, rj);
      break;
    case GreaterThanOrEqual:
      // sge d,s,t =>
      //   slt d,s,t
      //   xori d,d,1
      as_slt(rd, rj, rk);
      as_xori(rd, rd, 1);
      break;
    case LessThan:
      // slt d,s,t
      as_slt(rd, rj, rk);
      break;
    case LessThanOrEqual:
      // sle d,s,t =>
      //   slt d,t,s
      //   xori d,d,1
      as_slt(rd, rk, rj);
      as_xori(rd, rd, 1);
      break;
    case Zero:
      MOZ_ASSERT(rj == rk);
      // seq d,s,$zero =>
      //   sltiu d,s,1
      as_sltui(rd, rj, 1);
      break;
    case NonZero:
      MOZ_ASSERT(rj == rk);
      // sne d,s,$zero =>
      //   sltu d,$zero,s
      as_sltu(rd, zero, rj);
      break;
    case Signed:
      MOZ_ASSERT(rj == rk);
      as_slt(rd, rj, zero);
      break;
    case NotSigned:
      MOZ_ASSERT(rj == rk);
      // sge d,s,$zero =>
      //   slt d,s,$zero
      //   xori d,d,1
      as_slt(rd, rj, zero);
      as_xori(rd, rd, 1);
      break;
    default:
      MOZ_CRASH("Invalid condition.");
  }
}

void MacroAssemblerLOONG64::ma_cmp_set_double(Register dest, FloatRegister lhs,
                                              FloatRegister rhs,
                                              DoubleCondition c) {
  compareFloatingPoint(DoubleFloat, lhs, rhs, c);
  as_movcf2gr(dest, FCC0);
}

void MacroAssemblerLOONG64::ma_cmp_set_float32(Register dest, FloatRegister lhs,
                                               FloatRegister rhs,
                                               DoubleCondition c) {
  compareFloatingPoint(SingleFloat, lhs, rhs, c);
  as_movcf2gr(dest, FCC0);
}

void MacroAssemblerLOONG64::ma_cmp_set(Register rd, Register rj, Imm32 imm,
                                       Condition c) {
  if (imm.value == 0) {
    switch (c) {
      case Equal:
      case BelowOrEqual:
        as_sltui(rd, rj, 1);
        break;
      case NotEqual:
      case Above:
        as_sltu(rd, zero, rj);
        break;
      case AboveOrEqual:
      case Below:
        as_ori(rd, zero, c == AboveOrEqual ? 1 : 0);
        break;
      case GreaterThan:
      case LessThanOrEqual:
        as_slt(rd, zero, rj);
        if (c == LessThanOrEqual) {
          as_xori(rd, rd, 1);
        }
        break;
      case LessThan:
      case GreaterThanOrEqual:
        as_slt(rd, rj, zero);
        if (c == GreaterThanOrEqual) {
          as_xori(rd, rd, 1);
        }
        break;
      case Zero:
        as_sltui(rd, rj, 1);
        break;
      case NonZero:
        as_sltu(rd, zero, rj);
        break;
      case Signed:
        as_slt(rd, rj, zero);
        break;
      case NotSigned:
        as_slt(rd, rj, zero);
        as_xori(rd, rd, 1);
        break;
      default:
        MOZ_CRASH("Invalid condition.");
    }
    return;
  }

  switch (c) {
    case Equal:
    case NotEqual:
      ma_xor(rd, rj, imm);
      if (c == Equal) {
        as_sltui(rd, rd, 1);
      } else {
        as_sltu(rd, zero, rd);
      }
      break;
    case Zero:
    case NonZero:
    case Signed:
    case NotSigned:
      MOZ_CRASH("Invalid condition.");
    default:
      Condition cond = ma_cmp(rd, rj, imm, c);
      MOZ_ASSERT(cond == Equal || cond == NotEqual);

      if (cond == Equal) as_xori(rd, rd, 1);
  }
}

void MacroAssemblerLOONG64::compareFloatingPoint(FloatFormat fmt,
                                                 FloatRegister lhs,
                                                 FloatRegister rhs,
                                                 DoubleCondition c,
                                                 FPConditionBit fcc) {
  switch (c) {
    case DoubleOrdered:
      as_fcmp_cor(fmt, lhs, rhs, fcc);
      break;
    case DoubleEqual:
      as_fcmp_ceq(fmt, lhs, rhs, fcc);
      break;
    case DoubleNotEqual:
      as_fcmp_cne(fmt, lhs, rhs, fcc);
      break;
    case DoubleGreaterThan:
      as_fcmp_clt(fmt, rhs, lhs, fcc);
      break;
    case DoubleGreaterThanOrEqual:
      as_fcmp_cle(fmt, rhs, lhs, fcc);
      break;
    case DoubleLessThan:
      as_fcmp_clt(fmt, lhs, rhs, fcc);
      break;
    case DoubleLessThanOrEqual:
      as_fcmp_cle(fmt, lhs, rhs, fcc);
      break;
    case DoubleUnordered:
      as_fcmp_cun(fmt, lhs, rhs, fcc);
      break;
    case DoubleEqualOrUnordered:
      as_fcmp_cueq(fmt, lhs, rhs, fcc);
      break;
    case DoubleNotEqualOrUnordered:
      as_fcmp_cune(fmt, lhs, rhs, fcc);
      break;
    case DoubleGreaterThanOrUnordered:
      as_fcmp_cult(fmt, rhs, lhs, fcc);
      break;
    case DoubleGreaterThanOrEqualOrUnordered:
      as_fcmp_cule(fmt, rhs, lhs, fcc);
      break;
    case DoubleLessThanOrUnordered:
      as_fcmp_cult(fmt, lhs, rhs, fcc);
      break;
    case DoubleLessThanOrEqualOrUnordered:
      as_fcmp_cule(fmt, lhs, rhs, fcc);
      break;
    default:
      MOZ_CRASH("Invalid DoubleCondition.");
  }
}

void MacroAssemblerLOONG64::minMaxPtr(Register lhs, Register rhs, Register dest,
                                      bool isMax) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();

  as_slt(scratch, rhs, lhs);
  if (isMax) {
    as_masknez(scratch2, rhs, scratch);
    as_maskeqz(dest, lhs, scratch);
  } else {
    as_masknez(scratch2, lhs, scratch);
    as_maskeqz(dest, rhs, scratch);
  }
  as_or(dest, dest, scratch2);
}

void MacroAssemblerLOONG64::minMaxPtr(Register lhs, ImmWord rhs, Register dest,
                                      bool isMax) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();

  ma_li(scratch2, rhs);

  as_slt(scratch, scratch2, lhs);
  if (isMax) {
    as_masknez(scratch2, scratch2, scratch);
    as_maskeqz(dest, lhs, scratch);
  } else {
    as_maskeqz(scratch2, scratch2, scratch);
    as_masknez(dest, lhs, scratch);
  }
  as_or(dest, dest, scratch2);
}

void MacroAssemblerLOONG64::minMaxDouble(FloatRegister srcDest,
                                         FloatRegister second, bool handleNaN,
                                         bool isMax) {
  if (srcDest == second) return;

  Label nan, done;

  // First or second is NaN, result is NaN.
  ma_bc_d(srcDest, second, &nan, Assembler::DoubleUnordered, ShortJump);
  if (isMax) {
    as_fmax_d(srcDest, srcDest, second);
  } else {
    as_fmin_d(srcDest, srcDest, second);
  }
  ma_b(&done, ShortJump);

  bind(&nan);
  as_fadd_d(srcDest, srcDest, second);

  bind(&done);
}

void MacroAssemblerLOONG64::minMaxFloat32(FloatRegister srcDest,
                                          FloatRegister second, bool handleNaN,
                                          bool isMax) {
  if (srcDest == second) return;

  Label nan, done;

  // First or second is NaN, result is NaN.
  ma_bc_s(srcDest, second, &nan, Assembler::DoubleUnordered, ShortJump);
  if (isMax) {
    as_fmax_s(srcDest, srcDest, second);
  } else {
    as_fmin_s(srcDest, srcDest, second);
  }
  ma_b(&done, ShortJump);

  bind(&nan);
  as_fadd_s(srcDest, srcDest, second);

  bind(&done);
}

FaultingCodeOffset MacroAssemblerLOONG64::loadDouble(const Address& address,
                                                     FloatRegister dest) {
  return asMasm().ma_fld_d(dest, address);
}

FaultingCodeOffset MacroAssemblerLOONG64::loadDouble(const BaseIndex& src,
                                                     FloatRegister dest) {
  return asMasm().ma_fld_d(dest, src);
}

FaultingCodeOffset MacroAssemblerLOONG64::loadFloat32(const Address& address,
                                                      FloatRegister dest) {
  return asMasm().ma_fld_s(dest, address);
}

FaultingCodeOffset MacroAssemblerLOONG64::loadFloat32(const BaseIndex& src,
                                                      FloatRegister dest) {
  return asMasm().ma_fld_s(dest, src);
}

void MacroAssemblerLOONG64::wasmLoadImpl(const wasm::MemoryAccessDesc& access,
                                         Register memoryBase, Register ptr,
                                         Register ptrScratch,
                                         AnyRegister output, Register tmp) {
  access.assertOffsetInGuardPages();
  uint32_t offset = access.offset32();
  MOZ_ASSERT_IF(offset, ptrScratch != InvalidReg);

  // Maybe add the offset.
  if (offset) {
    asMasm().addPtr(ImmWord(offset), ptrScratch);
    ptr = ptrScratch;
  }

  asMasm().memoryBarrierBefore(access.sync());
  append(access, wasm::TrapMachineInsnForLoad(byteSize(access.type())),
         FaultingCodeOffset(currentOffset()));

  switch (access.type()) {
    case Scalar::Int8:
      as_ldx_b(output.gpr(), memoryBase, ptr);
      break;
    case Scalar::Uint8:
      as_ldx_bu(output.gpr(), memoryBase, ptr);
      break;
    case Scalar::Int16:
      as_ldx_h(output.gpr(), memoryBase, ptr);
      break;
    case Scalar::Uint16:
      as_ldx_hu(output.gpr(), memoryBase, ptr);
      break;
    case Scalar::Int32:
    case Scalar::Uint32:
      as_ldx_w(output.gpr(), memoryBase, ptr);
      break;
    case Scalar::Float64:
      as_fldx_d(output.fpu(), memoryBase, ptr);
      break;
    case Scalar::Float32:
      as_fldx_s(output.fpu(), memoryBase, ptr);
      break;
    default:
      MOZ_CRASH("unexpected array type");
  }

  asMasm().memoryBarrierAfter(access.sync());
}

void MacroAssemblerLOONG64::wasmStoreImpl(const wasm::MemoryAccessDesc& access,
                                          AnyRegister value,
                                          Register memoryBase, Register ptr,
                                          Register ptrScratch, Register tmp) {
  access.assertOffsetInGuardPages();
  uint32_t offset = access.offset32();
  MOZ_ASSERT_IF(offset, ptrScratch != InvalidReg);

  // Maybe add the offset.
  if (offset) {
    asMasm().addPtr(ImmWord(offset), ptrScratch);
    ptr = ptrScratch;
  }

  asMasm().memoryBarrierBefore(access.sync());
  // The next emitted instruction is a memory access.
  append(access, wasm::TrapMachineInsnForStore(byteSize(access.type())),
         FaultingCodeOffset(currentOffset()));

  switch (access.type()) {
    case Scalar::Int8:
    case Scalar::Uint8:
      as_stx_b(value.gpr(), memoryBase, ptr);
      break;
    case Scalar::Int16:
    case Scalar::Uint16:
      as_stx_h(value.gpr(), memoryBase, ptr);
      break;
    case Scalar::Int32:
    case Scalar::Uint32:
      as_stx_w(value.gpr(), memoryBase, ptr);
      break;
    case Scalar::Int64:
      as_stx_d(value.gpr(), memoryBase, ptr);
      break;
    case Scalar::Float64:
      as_fstx_d(value.fpu(), memoryBase, ptr);
      break;
    case Scalar::Float32:
      as_fstx_s(value.fpu(), memoryBase, ptr);
      break;
    default:
      MOZ_CRASH("unexpected array type");
  }

  asMasm().memoryBarrierAfter(access.sync());
}

void MacroAssemblerLOONG64Compat::wasmLoadI64Impl(
    const wasm::MemoryAccessDesc& access, Register memoryBase, Register ptr,
    Register ptrScratch, Register64 output, Register tmp) {
  uint32_t offset = access.offset32();
  MOZ_ASSERT_IF(offset, ptrScratch != InvalidReg);

  // Maybe add the offset.
  if (offset) {
    asMasm().addPtr(ImmWord(offset), ptrScratch);
    ptr = ptrScratch;
  }

  asMasm().memoryBarrierBefore(access.sync());
  append(access, wasm::TrapMachineInsnForLoad(byteSize(access.type())),
         FaultingCodeOffset(currentOffset()));

  switch (access.type()) {
    case Scalar::Int8:
      as_ldx_b(output.reg, memoryBase, ptr);
      break;
    case Scalar::Uint8:
      as_ldx_bu(output.reg, memoryBase, ptr);
      break;
    case Scalar::Int16:
      as_ldx_h(output.reg, memoryBase, ptr);
      break;
    case Scalar::Uint16:
      as_ldx_hu(output.reg, memoryBase, ptr);
      break;
    case Scalar::Int32:
      as_ldx_w(output.reg, memoryBase, ptr);
      break;
    case Scalar::Uint32:
      // TODO(loong64): Why need zero-extension here?
      as_ldx_wu(output.reg, memoryBase, ptr);
      break;
    case Scalar::Int64:
      as_ldx_d(output.reg, memoryBase, ptr);
      break;
    default:
      MOZ_CRASH("unexpected array type");
  }

  asMasm().memoryBarrierAfter(access.sync());
}

void MacroAssemblerLOONG64Compat::wasmStoreI64Impl(
    const wasm::MemoryAccessDesc& access, Register64 value, Register memoryBase,
    Register ptr, Register ptrScratch, Register tmp) {
  uint32_t offset = access.offset32();
  MOZ_ASSERT_IF(offset, ptrScratch != InvalidReg);

  // Maybe add the offset.
  if (offset) {
    asMasm().addPtr(ImmWord(offset), ptrScratch);
    ptr = ptrScratch;
  }

  asMasm().memoryBarrierBefore(access.sync());
  append(access, wasm::TrapMachineInsnForStore(byteSize(access.type())),
         FaultingCodeOffset(currentOffset()));

  switch (access.type()) {
    case Scalar::Int8:
    case Scalar::Uint8:
      as_stx_b(value.reg, memoryBase, ptr);
      break;
    case Scalar::Int16:
    case Scalar::Uint16:
      as_stx_h(value.reg, memoryBase, ptr);
      break;
    case Scalar::Int32:
    case Scalar::Uint32:
      as_stx_w(value.reg, memoryBase, ptr);
      break;
    case Scalar::Int64:
      as_stx_d(value.reg, memoryBase, ptr);
      break;
    default:
      MOZ_CRASH("unexpected array type");
  }

  asMasm().memoryBarrierAfter(access.sync());
}

void MacroAssemblerLOONG64::outOfLineWasmTruncateToInt32Check(
    FloatRegister input, Register output, MIRType fromType, TruncFlags flags,
    Label* rejoin, const wasm::TrapSiteDesc& trapSiteDesc) {
  bool isUnsigned = flags & TRUNC_UNSIGNED;
  bool isSaturating = flags & TRUNC_SATURATING;

  if (isSaturating) {
    ScratchDoubleScope fpscratch(asMasm());
    if (fromType == MIRType::Double) {
      asMasm().loadConstantDouble(0.0, fpscratch);
    } else {
      asMasm().loadConstantFloat32(0.0f, fpscratch);
    }

    if (isUnsigned) {
      ma_li(output, Imm32(UINT32_MAX));

      compareFloatingPoint(
          fromType == MIRType::Double ? DoubleFloat : SingleFloat, input,
          fpscratch, Assembler::DoubleLessThanOrUnordered);

      UseScratchRegisterScope temps(asMasm());
      Register scratch = temps.Acquire();
      as_movcf2gr(scratch, FCC0);
      // FCC0 = 1, output = zero; else not change.
      as_masknez(output, output, scratch);
    } else {
      // Positive overflow is already saturated to INT32_MAX, so we only have
      // to handle NaN and negative overflow here.

      compareFloatingPoint(
          fromType == MIRType::Double ? DoubleFloat : SingleFloat, input, input,
          Assembler::DoubleLessThanOrUnordered);

      UseScratchRegisterScope temps(asMasm());
      Register scratch = temps.Acquire();
      as_movcf2gr(scratch, FCC0);
      // FCC0 = 1, output = zero; else not change.
      as_masknez(output, output, scratch);

      compareFloatingPoint(
          fromType == MIRType::Double ? DoubleFloat : SingleFloat, input,
          fpscratch, Assembler::DoubleLessThan);

      as_movcf2gr(scratch, FCC0);
      // FCC0 == 1, move INT32_MIN to output; else not change.
      as_slli_w(scratch, scratch, 31);
      as_or(output, output, scratch);
    }

    MOZ_ASSERT(rejoin->bound());
    asMasm().jump(rejoin);
    return;
  }

  Label inputIsNaN;

  if (fromType == MIRType::Double) {
    asMasm().branchDouble(Assembler::DoubleUnordered, input, input,
                          &inputIsNaN);
  } else if (fromType == MIRType::Float32) {
    asMasm().branchFloat(Assembler::DoubleUnordered, input, input, &inputIsNaN);
  }

  asMasm().wasmTrap(wasm::Trap::IntegerOverflow, trapSiteDesc);
  asMasm().bind(&inputIsNaN);
  asMasm().wasmTrap(wasm::Trap::InvalidConversionToInteger, trapSiteDesc);
}

void MacroAssemblerLOONG64::outOfLineWasmTruncateToInt64Check(
    FloatRegister input, Register64 output_, MIRType fromType, TruncFlags flags,
    Label* rejoin, const wasm::TrapSiteDesc& trapSiteDesc) {
  bool isUnsigned = flags & TRUNC_UNSIGNED;
  bool isSaturating = flags & TRUNC_SATURATING;

  if (isSaturating) {
    ScratchDoubleScope fpscratch(asMasm());
    Register output = output_.reg;

    if (fromType == MIRType::Double) {
      asMasm().loadConstantDouble(0.0, fpscratch);
    } else {
      asMasm().loadConstantFloat32(0.0f, fpscratch);
    }

    if (isUnsigned) {
      asMasm().ma_li(output, ImmWord(UINT64_MAX));

      compareFloatingPoint(
          fromType == MIRType::Double ? DoubleFloat : SingleFloat, input,
          fpscratch, Assembler::DoubleLessThanOrUnordered);

      UseScratchRegisterScope temps(asMasm());
      Register scratch = temps.Acquire();
      as_movcf2gr(scratch, FCC0);
      // FCC0 = 1, output = zero; else not change.
      as_masknez(output, output, scratch);
    } else {
      // Positive overflow is already saturated to INT64_MAX, so we only have
      // to handle NaN and negative overflow here.

      compareFloatingPoint(
          fromType == MIRType::Double ? DoubleFloat : SingleFloat, input, input,
          Assembler::DoubleLessThanOrUnordered);

      UseScratchRegisterScope temps(asMasm());
      Register scratch = temps.Acquire();
      as_movcf2gr(scratch, FCC0);
      // FCC0 = 1, output = zero; else not change.
      as_masknez(output, output, scratch);

      compareFloatingPoint(
          fromType == MIRType::Double ? DoubleFloat : SingleFloat, input,
          fpscratch, Assembler::DoubleLessThan);

      as_movcf2gr(scratch, FCC0);
      // FCC0 == 1, move INT64_MIN to output; else not change.
      as_slli_d(scratch, scratch, 63);
      as_or(output, output, scratch);
    }

    MOZ_ASSERT(rejoin->bound());
    asMasm().jump(rejoin);
    return;
  }

  Label inputIsNaN;

  if (fromType == MIRType::Double) {
    asMasm().branchDouble(Assembler::DoubleUnordered, input, input,
                          &inputIsNaN);
  } else if (fromType == MIRType::Float32) {
    asMasm().branchFloat(Assembler::DoubleUnordered, input, input, &inputIsNaN);
  }

  asMasm().wasmTrap(wasm::Trap::IntegerOverflow, trapSiteDesc);
  asMasm().bind(&inputIsNaN);
  asMasm().wasmTrap(wasm::Trap::InvalidConversionToInteger, trapSiteDesc);
}

void MacroAssemblerLOONG64Compat::profilerEnterFrame(Register framePtr,
                                                     Register scratch) {
  asMasm().loadJSContext(scratch);
  loadPtr(Address(scratch, offsetof(JSContext, profilingActivation_)), scratch);
  storePtr(framePtr,
           Address(scratch, JitActivation::offsetOfLastProfilingFrame()));
  storePtr(ImmPtr(nullptr),
           Address(scratch, JitActivation::offsetOfLastProfilingCallSite()));
}

void MacroAssemblerLOONG64Compat::profilerExitFrame() {
  jump(asMasm().runtime()->jitRuntime()->getProfilerExitFrameTail());
}

MacroAssembler& MacroAssemblerLOONG64::asMasm() {
  return *static_cast<MacroAssembler*>(this);
}

const MacroAssembler& MacroAssemblerLOONG64::asMasm() const {
  return *static_cast<const MacroAssembler*>(this);
}

void MacroAssembler::subFromStackPtr(Imm32 imm32) {
  if (imm32.value) {
    asMasm().subPtr(imm32, StackPointer);
  }
}

//{{{ check_macroassembler_style
// ===============================================================
// MacroAssembler high-level usage.

void MacroAssembler::flush() {}

// ===============================================================
// Stack manipulation functions.

size_t MacroAssembler::PushRegsInMaskSizeInBytes(LiveRegisterSet set) {
  return set.gprs().size() * sizeof(intptr_t) + set.fpus().getPushSizeInBytes();
}

void MacroAssembler::PushRegsInMask(LiveRegisterSet set) {
  int32_t diff =
      set.gprs().size() * sizeof(intptr_t) + set.fpus().getPushSizeInBytes();
  const int32_t reserved = diff;

  reserveStack(reserved);
  for (GeneralRegisterBackwardIterator iter(set.gprs()); iter.more(); ++iter) {
    diff -= sizeof(intptr_t);
    storePtr(*iter, Address(StackPointer, diff));
  }

#ifdef ENABLE_WASM_SIMD
#  error "Needs more careful logic if SIMD is enabled"
#endif

  for (FloatRegisterBackwardIterator iter(set.fpus().reduceSetForPush());
       iter.more(); ++iter) {
    diff -= sizeof(double);
    storeDouble(*iter, Address(StackPointer, diff));
  }
  MOZ_ASSERT(diff == 0);
}

void MacroAssembler::PopRegsInMaskIgnore(LiveRegisterSet set,
                                         LiveRegisterSet ignore) {
  int32_t diff =
      set.gprs().size() * sizeof(intptr_t) + set.fpus().getPushSizeInBytes();
  const int32_t reserved = diff;

  for (GeneralRegisterBackwardIterator iter(set.gprs()); iter.more(); ++iter) {
    diff -= sizeof(intptr_t);
    if (!ignore.has(*iter)) {
      loadPtr(Address(StackPointer, diff), *iter);
    }
  }

#ifdef ENABLE_WASM_SIMD
#  error "Needs more careful logic if SIMD is enabled"
#endif

  for (FloatRegisterBackwardIterator iter(set.fpus().reduceSetForPush());
       iter.more(); ++iter) {
    diff -= sizeof(double);
    if (!ignore.has(*iter)) {
      loadDouble(Address(StackPointer, diff), *iter);
    }
  }
  MOZ_ASSERT(diff == 0);
  freeStack(reserved);
}

void MacroAssembler::storeRegsInMask(LiveRegisterSet set, Address dest,
                                     Register) {
  FloatRegisterSet fpuSet(set.fpus().reduceSetForPush());
  mozilla::DebugOnly<unsigned> numFpu = fpuSet.size();
  int32_t diffF = fpuSet.getPushSizeInBytes();
  mozilla::DebugOnly<int32_t> diffG = set.gprs().size() * sizeof(intptr_t);

  MOZ_ASSERT(dest.offset >= diffG + diffF);

  for (GeneralRegisterBackwardIterator iter(set.gprs()); iter.more(); ++iter) {
    diffG -= sizeof(intptr_t);
    dest.offset -= sizeof(intptr_t);
    storePtr(*iter, dest);
  }
  MOZ_ASSERT(diffG == 0);

#ifdef ENABLE_WASM_SIMD
#  error "Needs more careful logic if SIMD is enabled"
#endif

  for (FloatRegisterBackwardIterator iter(fpuSet); iter.more(); ++iter) {
    FloatRegister reg = *iter;
    diffF -= reg.size();
    numFpu -= 1;
    dest.offset -= reg.size();
    if (reg.isDouble()) {
      storeDouble(reg, dest);
    } else if (reg.isSingle()) {
      storeFloat32(reg, dest);
    } else {
      MOZ_CRASH("Unknown register type.");
    }
  }
  MOZ_ASSERT(numFpu == 0);

  diffF -= diffF % sizeof(uintptr_t);
  MOZ_ASSERT(diffF == 0);
}

void MacroAssembler::Push(Register reg) {
  push(reg);
  adjustFrame(int32_t(sizeof(intptr_t)));
}

void MacroAssembler::Push(const Imm32 imm) {
  push(imm);
  adjustFrame(int32_t(sizeof(intptr_t)));
}

void MacroAssembler::Push(const ImmWord imm) {
  push(imm);
  adjustFrame(int32_t(sizeof(intptr_t)));
}

void MacroAssembler::Push(const ImmPtr imm) {
  Push(ImmWord(uintptr_t(imm.value)));
}

void MacroAssembler::Push(const ImmGCPtr ptr) {
  push(ptr);
  adjustFrame(int32_t(sizeof(intptr_t)));
}

void MacroAssembler::Push(FloatRegister f) {
  push(f);
  // See MacroAssemblerLOONG64::ma_push(FloatRegister) for why we use
  // sizeof(double).
  adjustFrame(int32_t(sizeof(double)));
}

void MacroAssembler::PushBoxed(FloatRegister reg) {
  subFromStackPtr(Imm32(sizeof(double)));
  boxDouble(reg, Address(getStackPointer(), 0));
  adjustFrame(sizeof(double));
}

void MacroAssembler::Pop(Register reg) {
  pop(reg);
  adjustFrame(-int32_t(sizeof(intptr_t)));
}

void MacroAssembler::Pop(FloatRegister f) {
  pop(f);
  // See MacroAssemblerLOONG64::ma_pop(FloatRegister) for why we use
  // sizeof(double).
  adjustFrame(-int32_t(sizeof(double)));
}

void MacroAssembler::Pop(const ValueOperand& val) {
  popValue(val);
  adjustFrame(-int32_t(sizeof(Value)));
}

void MacroAssembler::PopStackPtr() {
  loadPtr(Address(StackPointer, 0), StackPointer);
  adjustFrame(-int32_t(sizeof(intptr_t)));
}

void MacroAssembler::freeStackTo(uint32_t framePushed) {
  MOZ_ASSERT(framePushed <= framePushed_);
  ma_sub_d(StackPointer, FramePointer, Imm32(framePushed));
  framePushed_ = framePushed;
}

// ===============================================================
// Simple call functions.

CodeOffset MacroAssembler::call(Register reg) {
  as_jirl(ra, reg, BOffImm16(0));
  return CodeOffset(currentOffset());
}

CodeOffset MacroAssembler::call(Label* label) {
  ma_bl(label);
  return CodeOffset(currentOffset());
}

CodeOffset MacroAssembler::callWithPatch() {
  as_bl(JOffImm26(1 * sizeof(uint32_t)));
  return CodeOffset(currentOffset());
}

void MacroAssembler::patchCall(uint32_t callerOffset, uint32_t calleeOffset) {
  BufferOffset call(callerOffset - 1 * sizeof(uint32_t));

  JOffImm26 offset = BufferOffset(calleeOffset).diffB<JOffImm26>(call);
  if (!offset.isInvalid()) {
    InstJump* bal = (InstJump*)editSrc(call);
    bal->setJOffImm26(offset);
  } else {
    uint32_t u32Offset = callerOffset - 4 * sizeof(uint32_t);
    uint32_t* u32 =
        reinterpret_cast<uint32_t*>(editSrc(BufferOffset(u32Offset)));
    *u32 = calleeOffset - callerOffset;
  }
}

CodeOffset MacroAssembler::farJumpWithPatch() {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
  as_pcaddi(scratch, 4);
  as_ld_w(scratch2, scratch, 0);
  as_add_d(scratch, scratch, scratch2);
  as_jirl(zero, scratch, BOffImm16(0));
  // Allocate space which will be patched by patchFarJump().
  CodeOffset farJump(currentOffset());
  spew(".space 32bit initValue 0xffff ffff");
  writeInst(UINT32_MAX);
  return farJump;
}

void MacroAssembler::patchFarJump(CodeOffset farJump, uint32_t targetOffset) {
  uint32_t* u32 =
      reinterpret_cast<uint32_t*>(editSrc(BufferOffset(farJump.offset())));
  MOZ_ASSERT(*u32 == UINT32_MAX);
  *u32 = targetOffset - farJump.offset();
}

void MacroAssembler::patchFarJump(uint8_t* farJump, uint8_t* target) {
  uint32_t* u32 = reinterpret_cast<uint32_t*>(farJump);
  MOZ_ASSERT(*u32 == UINT32_MAX);

  *u32 = (int64_t)target - (int64_t)farJump;
}

CodeOffset MacroAssembler::call(wasm::SymbolicAddress target) {
  movePtr(target, CallReg);
  return call(CallReg);
}

CodeOffset MacroAssembler::call(const Address& addr) {
  loadPtr(addr, CallReg);
  return call(CallReg);
}

void MacroAssembler::call(ImmWord target) { call(ImmPtr((void*)target.value)); }

void MacroAssembler::call(ImmPtr target) {
  BufferOffset bo = m_buffer.nextOffset();
  addPendingJump(bo, target, RelocationKind::HARDCODED);
  ma_call(target);
}

void MacroAssembler::call(JitCode* c) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  BufferOffset bo = m_buffer.nextOffset();
  addPendingJump(bo, ImmPtr(c->raw()), RelocationKind::JITCODE);
  ma_liPatchable(scratch, ImmPtr(c->raw()));
  callJitNoProfiler(scratch);
}

CodeOffset MacroAssembler::nopPatchableToCall() {
  // LOONG64
  as_nop();  // lu12i_w
  as_nop();  // ori
  as_nop();  // lu32i_d
  as_nop();  // jirl
  return CodeOffset(currentOffset());
}

void MacroAssembler::patchNopToCall(uint8_t* call, uint8_t* target) {
  Instruction* inst = (Instruction*)call - 4 /* four nops */;
  Assembler::WriteLoad64Instructions(inst, SavedScratchRegister,
                                     (uint64_t)target);
  inst[3] = InstImm(op_jirl, BOffImm16(0), SavedScratchRegister, ra);
}

void MacroAssembler::patchCallToNop(uint8_t* call) {
  Instruction* inst = (Instruction*)call - 4 /* four nops */;
  inst[0].makeNop();  // lu12i_w
  inst[1].makeNop();  // ori
  inst[2].makeNop();  // lu32i_d
  inst[3].makeNop();  // jirl
}

CodeOffset MacroAssembler::move32WithPatch(Register dest) {
  CodeOffset offs = CodeOffset(currentOffset());
  ma_liPatchable(dest, Imm32(0));
  return offs;
}

void MacroAssembler::patchMove32(CodeOffset offset, Imm32 n) {
  patchSub32FromStackPtr(offset, n);
}

void MacroAssembler::pushReturnAddress() { push(ra); }

void MacroAssembler::popReturnAddress() { pop(ra); }

// ===============================================================
// ABI function calls.

void MacroAssembler::setupUnalignedABICall(Register scratch) {
  MOZ_ASSERT(!IsCompilingWasm(), "wasm should only use aligned ABI calls");
  setupNativeABICall();
  dynamicAlignment_ = true;

  as_or(scratch, StackPointer, zero);

  // Force sp to be aligned
  asMasm().subPtr(Imm32(sizeof(uintptr_t)), StackPointer);
  ma_and(StackPointer, StackPointer, Imm32(~(ABIStackAlignment - 1)));
  storePtr(scratch, Address(StackPointer, 0));
}

void MacroAssembler::callWithABIPre(uint32_t* stackAdjust, bool callFromWasm) {
  MOZ_ASSERT(inCall_);
  uint32_t stackForCall = abiArgs_.stackBytesConsumedSoFar();

  // Reserve place for $ra.
  stackForCall += sizeof(intptr_t);

  if (dynamicAlignment_) {
    stackForCall += ComputeByteAlignment(stackForCall, ABIStackAlignment);
  } else {
    uint32_t alignmentAtPrologue = callFromWasm ? sizeof(wasm::Frame) : 0;
    stackForCall += ComputeByteAlignment(
        stackForCall + framePushed() + alignmentAtPrologue, ABIStackAlignment);
  }

  *stackAdjust = stackForCall;
  reserveStack(stackForCall);

  // Save $ra because call is going to clobber it. Restore it in
  // callWithABIPost. NOTE: This is needed for calls from SharedIC.
  // Maybe we can do this differently.
  storePtr(ra, Address(StackPointer, stackForCall - sizeof(intptr_t)));

  // Position all arguments.
  {
    enoughMemory_ &= moveResolver_.resolve();
    if (!enoughMemory_) {
      return;
    }

    MoveEmitter emitter(*this);
    emitter.emit(moveResolver_);
    emitter.finish();
  }

  assertStackAlignment(ABIStackAlignment);
}

void MacroAssembler::callWithABIPost(uint32_t stackAdjust, ABIType result) {
  // Restore ra value (as stored in callWithABIPre()).
  loadPtr(Address(StackPointer, stackAdjust - sizeof(intptr_t)), ra);

  if (dynamicAlignment_) {
    // Restore sp value from stack (as stored in setupUnalignedABICall()).
    loadPtr(Address(StackPointer, stackAdjust), StackPointer);
    // Use adjustFrame instead of freeStack because we already restored sp.
    adjustFrame(-stackAdjust);
  } else {
    freeStack(stackAdjust);
  }

#ifdef DEBUG
  MOZ_ASSERT(inCall_);
  inCall_ = false;
#endif
}

void MacroAssembler::callWithABINoProfiler(Register fun, ABIType result) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  // Load the callee in scratch2, no instruction between the movePtr and
  // call should clobber it. Note that we can't use fun because it may be
  // one of the IntArg registers clobbered before the call.
  movePtr(fun, scratch);

  uint32_t stackAdjust;
  callWithABIPre(&stackAdjust);
  call(scratch);
  callWithABIPost(stackAdjust, result);
}

void MacroAssembler::callWithABINoProfiler(const Address& fun, ABIType result) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  // Load the callee in scratch, as above.
  loadPtr(fun, scratch);

  uint32_t stackAdjust;
  callWithABIPre(&stackAdjust);
  call(scratch);
  callWithABIPost(stackAdjust, result);
}

// ===============================================================
// Jit Frames.

uint32_t MacroAssembler::pushFakeReturnAddress(Register scratch) {
  CodeLabel cl;

  ma_li(scratch, &cl);
  Push(scratch);
  bind(&cl);
  uint32_t retAddr = currentOffset();

  addCodeLabel(cl);
  return retAddr;
}

// ===============================================================
// Move instructions

void MacroAssembler::moveValue(const ValueOperand& src,
                               const ValueOperand& dest) {
  if (src == dest) {
    return;
  }
  movePtr(src.valueReg(), dest.valueReg());
}

void MacroAssembler::moveValue(const Value& src, const ValueOperand& dest) {
  if (!src.isGCThing()) {
    ma_li(dest.valueReg(), ImmWord(src.asRawBits()));
    return;
  }

  writeDataRelocation(src);
  movWithPatch(ImmWord(src.asRawBits()), dest.valueReg());
}

// ===============================================================
// Branch functions

void MacroAssembler::loadStoreBuffer(Register ptr, Register buffer) {
  ma_and(buffer, ptr, Imm32(int32_t(~gc::ChunkMask)));
  loadPtr(Address(buffer, gc::ChunkStoreBufferOffset), buffer);
}

void MacroAssembler::branchPtrInNurseryChunk(Condition cond, Register ptr,
                                             Register temp, Label* label) {
  MOZ_ASSERT(cond == Assembler::Equal || cond == Assembler::NotEqual);
  MOZ_ASSERT(ptr != temp);
  MOZ_ASSERT(temp != InvalidReg);

  ma_and(temp, ptr, Imm32(int32_t(~gc::ChunkMask)));
  branchPtr(InvertCondition(cond), Address(temp, gc::ChunkStoreBufferOffset),
            zero, label);
}

void MacroAssembler::branchValueIsNurseryCell(Condition cond,
                                              const Address& address,
                                              Register temp, Label* label) {
  branchValueIsNurseryCellImpl(cond, address, temp, label);
}

void MacroAssembler::branchValueIsNurseryCell(Condition cond,
                                              ValueOperand value, Register temp,
                                              Label* label) {
  branchValueIsNurseryCellImpl(cond, value, temp, label);
}

template <typename T>
void MacroAssembler::branchValueIsNurseryCellImpl(Condition cond,
                                                  const T& value, Register temp,
                                                  Label* label) {
  MOZ_ASSERT(cond == Assembler::Equal || cond == Assembler::NotEqual);
  MOZ_ASSERT(temp != InvalidReg);
  Label done;
  branchTestGCThing(Assembler::NotEqual, value,
                    cond == Assembler::Equal ? &done : label);

  getGCThingValueChunk(value, temp);
  loadPtr(Address(temp, gc::ChunkStoreBufferOffset), temp);
  branchPtr(InvertCondition(cond), temp, zero, label);

  bind(&done);
}

void MacroAssembler::branchTestValue(Condition cond, const ValueOperand& lhs,
                                     const Value& rhs, Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  MOZ_ASSERT(!rhs.isNaN());

  if (!rhs.isGCThing()) {
    ma_b(lhs.valueReg(), ImmWord(rhs.asRawBits()), label, cond);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(lhs.valueReg() != scratch);
    moveValue(rhs, ValueOperand(scratch));
    ma_b(lhs.valueReg(), scratch, label, cond);
  }
}

void MacroAssembler::branchTestNaNValue(Condition cond, const ValueOperand& val,
                                        Register temp, Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(val.valueReg() != scratch);

  // When testing for NaN, we want to ignore the sign bit.
  // Clear the sign bit by extracting bits 62:0.
  as_bstrpick_d(temp, val.valueReg(), 62, 0);

  // Compare against a NaN with sign bit 0.
  static_assert(JS::detail::CanonicalizedNaNSignBit == 0);
  moveValue(DoubleValue(JS::GenericNaN()), ValueOperand(scratch));
  ma_b(temp, scratch, label, cond);
}

// ========================================================================
// Memory access primitives.

template <typename T>
void MacroAssembler::storeUnboxedValue(const ConstantOrRegister& value,
                                       MIRType valueType, const T& dest) {
  MOZ_ASSERT(valueType < MIRType::Value);

  if (valueType == MIRType::Double) {
    boxDouble(value.reg().typedReg().fpu(), dest);
    return;
  }

  if (value.constant()) {
    storeValue(value.value(), dest);
  } else {
    storeValue(ValueTypeFromMIRType(valueType), value.reg().typedReg().gpr(),
               dest);
  }
}

template void MacroAssembler::storeUnboxedValue(const ConstantOrRegister& value,
                                                MIRType valueType,
                                                const Address& dest);
template void MacroAssembler::storeUnboxedValue(
    const ConstantOrRegister& value, MIRType valueType,
    const BaseObjectElementIndex& dest);

void MacroAssembler::comment(const char* msg) { Assembler::comment(msg); }

// ===============================================================
// WebAssembly

FaultingCodeOffset MacroAssembler::wasmTrapInstruction() {
  FaultingCodeOffset fco = FaultingCodeOffset(currentOffset());
  as_break(WASM_TRAP);  // TODO: as_teq(zero, zero, WASM_TRAP)
  return fco;
}

void MacroAssembler::wasmBoundsCheck32(Condition cond, Register index,
                                       Register boundsCheckLimit,
                                       Label* label) {
  ma_b(index, boundsCheckLimit, label, cond);
}

void MacroAssembler::wasmBoundsCheck32(Condition cond, Register index,
                                       Address boundsCheckLimit, Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  load32(boundsCheckLimit, scratch);
  ma_b(index, Register(scratch), label, cond);
}

void MacroAssembler::wasmBoundsCheck64(Condition cond, Register64 index,
                                       Register64 boundsCheckLimit,
                                       Label* label) {
  ma_b(index.reg, boundsCheckLimit.reg, label, cond);
}

void MacroAssembler::wasmBoundsCheck64(Condition cond, Register64 index,
                                       Address boundsCheckLimit, Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(boundsCheckLimit, scratch);
  ma_b(index.reg, scratch, label, cond);
}

// FTINTRZ behaves as follows:
//
// on NaN it produces zero
// on too large it produces INT_MAX (for appropriate type)
// on too small it produces INT_MIN (ditto)

void MacroAssembler::wasmTruncateDoubleToUInt32(FloatRegister input,
                                                Register output,
                                                bool isSaturating,
                                                Label* oolEntry) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ScratchDoubleScope fpscratch(asMasm());
  if (!isSaturating) {
    ma_bc_d(input, input, oolEntry, Assembler::DoubleUnordered);
  }
  as_ftintrz_l_d(fpscratch, input);
  moveFromDouble(fpscratch, output);
  as_srli_d(scratch, output, 32);
  as_slli_w(output, output, 0);
  ma_b(scratch, Imm32(0), oolEntry, Assembler::NotEqual);
}

void MacroAssembler::wasmTruncateFloat32ToUInt32(FloatRegister input,
                                                 Register output,
                                                 bool isSaturating,
                                                 Label* oolEntry) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ScratchDoubleScope fpscratch(asMasm());
  if (!isSaturating) {
    ma_bc_s(input, input, oolEntry, Assembler::DoubleUnordered);
  }
  as_ftintrz_l_s(fpscratch, input);
  moveFromDouble(fpscratch, output);
  as_srli_d(scratch, output, 32);
  as_slli_w(output, output, 0);
  ma_b(scratch, Imm32(0), oolEntry, Assembler::NotEqual);
}

// Assembler::CauseV is a enum,called FCSRBit. Assembler::CauseV == 16
void MacroAssembler::wasmTruncateDoubleToInt32(FloatRegister input,
                                               Register output,
                                               bool isSaturating,
                                               Label* oolEntry) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ScratchFloat32Scope fpscratch(asMasm());
  as_ftintrz_w_d(fpscratch, input);
  as_movfcsr2gr(scratch);
  moveFromFloat32(fpscratch, output);
  MOZ_ASSERT(Assembler::CauseV < 32);
  as_bstrpick_w(scratch, scratch, Assembler::CauseV, Assembler::CauseV);
  ma_b(scratch, Imm32(0), oolEntry, Assembler::NotEqual);
}

void MacroAssembler::wasmTruncateFloat32ToInt32(FloatRegister input,
                                                Register output,
                                                bool isSaturating,
                                                Label* oolEntry) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ScratchFloat32Scope fpscratch(asMasm());
  as_ftintrz_w_s(fpscratch, input);
  as_movfcsr2gr(scratch);
  moveFromFloat32(fpscratch, output);
  MOZ_ASSERT(Assembler::CauseV < 32);
  as_bstrpick_w(scratch, scratch, Assembler::CauseV, Assembler::CauseV);
  ma_b(scratch, Imm32(0), oolEntry, Assembler::NotEqual);
}

void MacroAssembler::wasmTruncateDoubleToUInt64(
    FloatRegister input, Register64 output_, bool isSaturating, Label* oolEntry,
    Label* oolRejoin, FloatRegister tempDouble) {
  MOZ_ASSERT(tempDouble.isInvalid());
  ScratchDoubleScope fpscratch(asMasm());
  Register output = output_.reg;

  Label done;

  if (!isSaturating) {
    ma_bc_d(input, input, oolEntry, Assembler::DoubleUnordered);
  }
  as_ftintrz_l_d(fpscratch, input);
  moveFromDouble(fpscratch, output);
  loadConstantDouble(double(INT64_MAX + 1ULL), fpscratch);

  UseScratchRegisterScope temps(asMasm());
  Register scratch2 = temps.Acquire();
  ma_li(scratch2, ImmWord(INT64_MAX));
  // For numbers in  -1.[ : ]INT64_MAX range do nothing more
  ma_b(output, Register(scratch2), &done, Assembler::Below, ShortJump);

  ma_li(scratch2, ImmWord(INT64_MIN));
  as_fsub_d(fpscratch, input, fpscratch);
  as_ftintrz_l_d(fpscratch, fpscratch);
  Register scratch = temps.Acquire();
  as_movfcsr2gr(scratch);
  moveFromDouble(fpscratch, output);
  as_bstrpick_d(scratch, scratch, Assembler::CauseV, Assembler::CauseV);
  as_add_d(output, output, scratch2);

  // Guard against negative values that result in 0 due the precision loss.
  as_sltui(scratch2, output, 1);
  as_or(scratch, scratch, scratch2);

  ma_b(scratch, zero, oolEntry, Assembler::NotEqual);

  bind(&done);

  if (isSaturating) {
    bind(oolRejoin);
  }
}

void MacroAssembler::wasmTruncateFloat32ToUInt64(
    FloatRegister input, Register64 output_, bool isSaturating, Label* oolEntry,
    Label* oolRejoin, FloatRegister tempFloat) {
  MOZ_ASSERT(tempFloat.isInvalid());
  ScratchDoubleScope fpscratch(asMasm());
  Register output = output_.reg;

  Label done;

  if (!isSaturating) {
    ma_bc_s(input, input, oolEntry, Assembler::DoubleUnordered);
  }
  as_ftintrz_l_s(fpscratch, input);
  moveFromDouble(fpscratch, output);
  loadConstantFloat32(float(INT64_MAX + 1ULL), fpscratch);

  UseScratchRegisterScope temps(asMasm());
  Register scratch2 = temps.Acquire();
  ma_li(scratch2, ImmWord(INT64_MAX));
  // For numbers in  -1.[ : ]INT64_MAX range do nothing more
  ma_b(output, Register(scratch2), &done, Assembler::Below, ShortJump);

  ma_li(scratch2, ImmWord(INT64_MIN));
  as_fsub_s(fpscratch, input, fpscratch);
  as_ftintrz_l_s(fpscratch, fpscratch);
  Register scratch = temps.Acquire();
  as_movfcsr2gr(scratch);
  moveFromDouble(fpscratch, output);
  as_bstrpick_d(scratch, scratch, Assembler::CauseV, Assembler::CauseV);
  as_add_d(output, output, scratch2);

  // Guard against negative values that result in 0 due the precision loss.
  as_sltui(scratch2, output, 1);
  as_or(scratch, scratch, scratch2);

  ma_b(scratch, zero, oolEntry, Assembler::NotEqual);

  bind(&done);

  if (isSaturating) {
    bind(oolRejoin);
  }
}

void MacroAssembler::wasmTruncateDoubleToInt64(
    FloatRegister input, Register64 output, bool isSaturating, Label* oolEntry,
    Label* oolRejoin, FloatRegister tempDouble) {
  MOZ_ASSERT(tempDouble.isInvalid());
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ScratchDoubleScope fpscratch(asMasm());

  as_ftintrz_l_d(fpscratch, input);
  as_movfcsr2gr(scratch);
  moveFromDouble(fpscratch, output.reg);
  as_bstrpick_d(scratch, scratch, Assembler::CauseV, Assembler::CauseV);
  ma_b(scratch, zero, oolEntry, Assembler::NotEqual);

  if (isSaturating) {
    bind(oolRejoin);
  }
}

void MacroAssembler::wasmTruncateFloat32ToInt64(
    FloatRegister input, Register64 output, bool isSaturating, Label* oolEntry,
    Label* oolRejoin, FloatRegister tempFloat) {
  MOZ_ASSERT(tempFloat.isInvalid());
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ScratchDoubleScope fpscratch(asMasm());

  as_ftintrz_l_s(fpscratch, input);
  as_movfcsr2gr(scratch);
  moveFromDouble(fpscratch, output.reg);
  as_bstrpick_d(scratch, scratch, Assembler::CauseV, Assembler::CauseV);
  ma_b(scratch, zero, oolEntry, Assembler::NotEqual);

  if (isSaturating) {
    bind(oolRejoin);
  }
}

void MacroAssembler::oolWasmTruncateCheckF32ToI32(
    FloatRegister input, Register output, TruncFlags flags,
    const wasm::TrapSiteDesc& trapSiteDesc, Label* rejoin) {
  outOfLineWasmTruncateToInt32Check(input, output, MIRType::Float32, flags,
                                    rejoin, trapSiteDesc);
}

void MacroAssembler::oolWasmTruncateCheckF64ToI32(
    FloatRegister input, Register output, TruncFlags flags,
    const wasm::TrapSiteDesc& trapSiteDesc, Label* rejoin) {
  outOfLineWasmTruncateToInt32Check(input, output, MIRType::Double, flags,
                                    rejoin, trapSiteDesc);
}

void MacroAssembler::oolWasmTruncateCheckF32ToI64(
    FloatRegister input, Register64 output, TruncFlags flags,
    const wasm::TrapSiteDesc& trapSiteDesc, Label* rejoin) {
  outOfLineWasmTruncateToInt64Check(input, output, MIRType::Float32, flags,
                                    rejoin, trapSiteDesc);
}

void MacroAssembler::oolWasmTruncateCheckF64ToI64(
    FloatRegister input, Register64 output, TruncFlags flags,
    const wasm::TrapSiteDesc& trapSiteDesc, Label* rejoin) {
  outOfLineWasmTruncateToInt64Check(input, output, MIRType::Double, flags,
                                    rejoin, trapSiteDesc);
}

void MacroAssembler::wasmLoad(const wasm::MemoryAccessDesc& access,
                              Register memoryBase, Register ptr,
                              Register ptrScratch, AnyRegister output) {
  wasmLoadImpl(access, memoryBase, ptr, ptrScratch, output, InvalidReg);
}

void MacroAssembler::wasmLoadI64(const wasm::MemoryAccessDesc& access,
                                 Register memoryBase, Register ptr,
                                 Register ptrScratch, Register64 output) {
  wasmLoadI64Impl(access, memoryBase, ptr, ptrScratch, output, InvalidReg);
}

void MacroAssembler::wasmStore(const wasm::MemoryAccessDesc& access,
                               AnyRegister value, Register memoryBase,
                               Register ptr, Register ptrScratch) {
  wasmStoreImpl(access, value, memoryBase, ptr, ptrScratch, InvalidReg);
}

void MacroAssembler::wasmStoreI64(const wasm::MemoryAccessDesc& access,
                                  Register64 value, Register memoryBase,
                                  Register ptr, Register ptrScratch) {
  wasmStoreI64Impl(access, value, memoryBase, ptr, ptrScratch, InvalidReg);
}

void MacroAssembler::enterFakeExitFrameForWasm(Register cxreg, Register scratch,
                                               ExitFrameType type) {
  enterFakeExitFrame(cxreg, scratch, type);
}

CodeOffset MacroAssembler::sub32FromMemAndBranchIfNegativeWithPatch(
    Address address, Label* label) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(scratch != address.base);
  ma_ld_w(scratch, address);
  // LoongArch doesn't have subtraction instr that support immediate operand,
  // and use 'addi.w rd, rj, -imm' instead to achieve same function.
  // 128 is arbitrary, but makes `*address` count upwards, which may help
  // to identify cases where the subsequent ::patch..() call was forgotten.
  as_addi_w(scratch, scratch, 128);
  // Points immediately after the insn to patch
  CodeOffset patchPoint = CodeOffset(currentOffset());
  ma_st_w(scratch, address);
  ma_b(scratch, Register(scratch), label, Assembler::Signed);
  return patchPoint;
}

void MacroAssembler::patchSub32FromMemAndBranchIfNegative(CodeOffset offset,
                                                          Imm32 imm) {
  int32_t val = imm.value;
  // Patching it to zero would make the insn pointless
  MOZ_RELEASE_ASSERT(val >= 1 && val <= 127);
  Instruction* instrPtr =
      (Instruction*)m_buffer.getInst(BufferOffset(offset.offset() - 4));
  // LoongArch doesn't have subtraction instr that support immediate operand,
  // and use 'addi.w rd, rj, -imm' instead to achieve same function.
  // 31   27   23 21   9  4
  // |    |    |  |    |  |
  // 0000 0010 10 si12 rj rd = addi.w rd, rj, #si12
  InstImm* inst = (InstImm*)instrPtr;
  MOZ_ASSERT(inst->extractBitField(31, 22) == ((uint32_t)op_addi_w >> 22));

  *inst = InstImm(op_addi_w, (int32_t)(-val & 0xfff),
                  Register::FromCode(inst->extractRJ()),
                  Register::FromCode(inst->extractRD()), 12);
}

// TODO(loong64): widenInt32 should be nop?
void MacroAssembler::widenInt32(Register r) {
  move32To64SignExtend(r, Register64(r));
}

// ========================================================================
// Convert floating point.

void MacroAssembler::convertUInt64ToFloat32(Register64 src_, FloatRegister dest,
                                            Register temp) {
  MOZ_ASSERT(temp == Register::Invalid());
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();

  Register src = src_.reg;
  Label positive, done;
  ma_b(src, src, &positive, NotSigned, ShortJump);

  MOZ_ASSERT(src != scratch);
  MOZ_ASSERT(src != scratch2);

  ma_and(scratch, src, Imm32(1));
  as_srli_d(scratch2, src, 1);
  as_or(scratch, scratch, scratch2);
  as_movgr2fr_d(dest, scratch);
  as_ffint_s_l(dest, dest);
  addFloat32(dest, dest);
  ma_b(&done, ShortJump);

  bind(&positive);
  as_movgr2fr_d(dest, src);
  as_ffint_s_l(dest, dest);

  bind(&done);
}

void MacroAssembler::convertInt64ToFloat32(Register64 src, FloatRegister dest) {
  as_movgr2fr_d(dest, src.reg);
  as_ffint_s_l(dest, dest);
}

bool MacroAssembler::convertUInt64ToDoubleNeedsTemp() { return false; }

void MacroAssembler::convertUInt64ToDouble(Register64 src, FloatRegister dest,
                                           Register temp) {
  MOZ_ASSERT(temp == Register::Invalid());
  MacroAssemblerSpecific::convertUInt64ToDouble(src.reg, dest);
}

void MacroAssembler::convertInt64ToDouble(Register64 src, FloatRegister dest) {
  as_movgr2fr_d(dest, src.reg);
  as_ffint_d_l(dest, dest);
}

void MacroAssembler::convertIntPtrToDouble(Register src, FloatRegister dest) {
  convertInt64ToDouble(Register64(src), dest);
}

// ========================================================================
// Primitive atomic operations.

template <typename T>
static void CompareExchange(MacroAssembler& masm,
                            const wasm::MemoryAccessDesc* access,
                            Scalar::Type type, Synchronization sync,
                            const T& mem, Register oldval, Register newval,
                            Register valueTemp, Register offsetTemp,
                            Register maskTemp, Register output) {
  UseScratchRegisterScope temps(masm);
  bool signExtend = Scalar::isSignedIntType(type);
  unsigned nbytes = Scalar::byteSize(type);

  switch (nbytes) {
    case 1:
    case 2:
      break;
    case 4:
      MOZ_ASSERT(valueTemp == InvalidReg);
      MOZ_ASSERT(offsetTemp == InvalidReg);
      MOZ_ASSERT(maskTemp == InvalidReg);
      break;
    default:
      MOZ_CRASH();
  }

  Label again, end;

  Register scratch = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch);

  Register scratch2 = temps.Acquire();

  if (nbytes == 4) {
    masm.memoryBarrierBefore(sync);
    masm.bind(&again);

    if (access) {
      masm.append(*access, wasm::TrapMachineInsn::Load32,
                  FaultingCodeOffset(masm.currentOffset()));
    }

    masm.as_ll_w(output, scratch, 0);
    masm.as_slli_w(scratch2, oldval, 0);
    masm.ma_b(output, scratch2, &end, Assembler::NotEqual, ShortJump);
    masm.as_or(scratch2, newval, zero);
    masm.as_sc_w(scratch2, scratch, 0);
    masm.ma_b(scratch2, Register(scratch2), &again, Assembler::Zero, ShortJump);

    masm.memoryBarrierAfter(sync);
    masm.bind(&end);

    return;
  }

  masm.as_andi(offsetTemp, scratch, 3);
  masm.subPtr(offsetTemp, scratch);
  masm.as_slli_w(offsetTemp, offsetTemp, 3);
  masm.ma_li(maskTemp, Imm32(UINT32_MAX >> ((4 - nbytes) * 8)));
  masm.as_sll_w(maskTemp, maskTemp, offsetTemp);
  masm.as_nor(maskTemp, zero, maskTemp);

  masm.memoryBarrierBefore(sync);

  masm.bind(&again);

  if (access) {
    masm.append(*access, wasm::TrapMachineInsn::Load32,
                FaultingCodeOffset(masm.currentOffset()));
  }

  masm.as_ll_w(scratch2, scratch, 0);

  masm.as_srl_w(output, scratch2, offsetTemp);

  switch (nbytes) {
    case 1:
      if (signExtend) {
        masm.as_ext_w_b(valueTemp, oldval);
        masm.as_ext_w_b(output, output);
      } else {
        masm.as_andi(valueTemp, oldval, 0xff);
        masm.as_andi(output, output, 0xff);
      }
      break;
    case 2:
      if (signExtend) {
        masm.as_ext_w_h(valueTemp, oldval);
        masm.as_ext_w_h(output, output);
      } else {
        masm.as_bstrpick_d(valueTemp, oldval, 15, 0);
        masm.as_bstrpick_d(output, output, 15, 0);
      }
      break;
  }

  masm.ma_b(output, valueTemp, &end, Assembler::NotEqual, ShortJump);

  masm.as_sll_w(valueTemp, newval, offsetTemp);
  masm.as_andn(valueTemp, valueTemp, maskTemp);
  masm.as_and(scratch2, scratch2, maskTemp);
  masm.as_or(scratch2, scratch2, valueTemp);

  masm.as_sc_w(scratch2, scratch, 0);

  masm.ma_b(scratch2, Register(scratch2), &again, Assembler::Zero, ShortJump);

  masm.memoryBarrierAfter(sync);

  masm.bind(&end);
}

template <typename T>
static void CompareExchange64(MacroAssembler& masm,
                              const wasm::MemoryAccessDesc* access,
                              Synchronization sync, const T& mem,
                              Register64 expect, Register64 replace,
                              Register64 output) {
  MOZ_ASSERT(expect != output && replace != output);
  UseScratchRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch);

  Register scratch2 = temps.Acquire();

  Label tryAgain;
  Label exit;

  masm.memoryBarrierBefore(sync);

  masm.bind(&tryAgain);

  if (access) {
    masm.append(*access, wasm::TrapMachineInsn::Load64,
                FaultingCodeOffset(masm.currentOffset()));
  }

  masm.as_ll_d(output.reg, scratch, 0);

  masm.ma_b(output.reg, expect.reg, &exit, Assembler::NotEqual, ShortJump);
  masm.movePtr(replace.reg, scratch2);
  masm.as_sc_d(scratch2, scratch, 0);
  masm.ma_b(scratch2, Register(scratch2), &tryAgain, Assembler::Zero,
            ShortJump);

  masm.memoryBarrierAfter(sync);

  masm.bind(&exit);
}

template <typename T>
static void AtomicExchange(MacroAssembler& masm,
                           const wasm::MemoryAccessDesc* access,
                           Scalar::Type type, Synchronization sync,
                           const T& mem, Register value, Register valueTemp,
                           Register offsetTemp, Register maskTemp,
                           Register output) {
  UseScratchRegisterScope temps(masm);
  bool signExtend = Scalar::isSignedIntType(type);
  unsigned nbytes = Scalar::byteSize(type);

  switch (nbytes) {
    case 1:
    case 2:
      break;
    case 4:
      MOZ_ASSERT(valueTemp == InvalidReg);
      MOZ_ASSERT(offsetTemp == InvalidReg);
      MOZ_ASSERT(maskTemp == InvalidReg);
      break;
    default:
      MOZ_CRASH();
  }

  Label again;

  Register scratch2 = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch2);

  Register scratch = temps.Acquire();

  if (nbytes == 4) {
    masm.memoryBarrierBefore(sync);
    masm.bind(&again);

    if (access) {
      masm.append(*access, wasm::TrapMachineInsn::Load32,
                  FaultingCodeOffset(masm.currentOffset()));
    }

    masm.as_ll_w(output, scratch2, 0);
    masm.as_or(scratch, value, zero);
    masm.as_sc_w(scratch, scratch2, 0);
    masm.ma_b(scratch, Register(scratch), &again, Assembler::Zero, ShortJump);

    masm.memoryBarrierAfter(sync);

    return;
  }

  masm.as_andi(offsetTemp, scratch2, 3);
  masm.subPtr(offsetTemp, scratch2);
  masm.as_slli_w(offsetTemp, offsetTemp, 3);
  masm.ma_li(maskTemp, Imm32(UINT32_MAX >> ((4 - nbytes) * 8)));
  masm.as_sll_w(maskTemp, maskTemp, offsetTemp);
  masm.as_nor(maskTemp, zero, maskTemp);
  switch (nbytes) {
    case 1:
      masm.as_andi(valueTemp, value, 0xff);
      break;
    case 2:
      masm.as_bstrpick_d(valueTemp, value, 15, 0);
      break;
  }
  masm.as_sll_w(valueTemp, valueTemp, offsetTemp);

  masm.memoryBarrierBefore(sync);

  masm.bind(&again);

  if (access) {
    masm.append(*access, wasm::TrapMachineInsn::Load32,
                FaultingCodeOffset(masm.currentOffset()));
  }

  masm.as_ll_w(output, scratch2, 0);
  masm.as_and(scratch, output, maskTemp);
  masm.as_or(scratch, scratch, valueTemp);

  masm.as_sc_w(scratch, scratch2, 0);

  masm.ma_b(scratch, Register(scratch), &again, Assembler::Zero, ShortJump);

  masm.as_srl_w(output, output, offsetTemp);

  switch (nbytes) {
    case 1:
      if (signExtend) {
        masm.as_ext_w_b(output, output);
      } else {
        masm.as_andi(output, output, 0xff);
      }
      break;
    case 2:
      if (signExtend) {
        masm.as_ext_w_h(output, output);
      } else {
        masm.as_bstrpick_d(output, output, 15, 0);
      }
      break;
  }

  masm.memoryBarrierAfter(sync);
}

template <typename T>
static void AtomicExchange64(MacroAssembler& masm,
                             const wasm::MemoryAccessDesc* access,
                             Synchronization sync, const T& mem,
                             Register64 value, Register64 output) {
  MOZ_ASSERT(value != output);
  UseScratchRegisterScope temps(masm);

  Register scratch = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch);

  Register scratch2 = temps.Acquire();

  Label tryAgain;

  masm.memoryBarrierBefore(sync);

  masm.bind(&tryAgain);

  if (access) {
    masm.append(*access, wasm::TrapMachineInsn::Load64,
                FaultingCodeOffset(masm.currentOffset()));
  }

  masm.as_ll_d(output.reg, scratch, 0);

  masm.movePtr(value.reg, scratch2);
  masm.as_sc_d(scratch2, scratch, 0);
  masm.ma_b(scratch2, Register(scratch2), &tryAgain, Assembler::Zero,
            ShortJump);

  masm.memoryBarrierAfter(sync);
}

template <typename T>
static void AtomicFetchOp(MacroAssembler& masm,
                          const wasm::MemoryAccessDesc* access,
                          Scalar::Type type, Synchronization sync, AtomicOp op,
                          const T& mem, Register value, Register valueTemp,
                          Register offsetTemp, Register maskTemp,
                          Register output) {
  UseScratchRegisterScope temps(masm);
  bool signExtend = Scalar::isSignedIntType(type);
  unsigned nbytes = Scalar::byteSize(type);

  switch (nbytes) {
    case 1:
    case 2:
      break;
    case 4:
      MOZ_ASSERT(valueTemp == InvalidReg);
      MOZ_ASSERT(offsetTemp == InvalidReg);
      MOZ_ASSERT(maskTemp == InvalidReg);
      break;
    default:
      MOZ_CRASH();
  }

  Label again;

  Register scratch2 = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch2);

  Register scratch = temps.Acquire();

  if (nbytes == 4) {
    masm.memoryBarrierBefore(sync);
    masm.bind(&again);

    if (access) {
      masm.append(*access, wasm::TrapMachineInsn::Load32,
                  FaultingCodeOffset(masm.currentOffset()));
    }

    masm.as_ll_w(output, scratch2, 0);

    switch (op) {
      case AtomicOp::Add:
        masm.as_add_w(scratch, output, value);
        break;
      case AtomicOp::Sub:
        masm.as_sub_w(scratch, output, value);
        break;
      case AtomicOp::And:
        masm.as_and(scratch, output, value);
        break;
      case AtomicOp::Or:
        masm.as_or(scratch, output, value);
        break;
      case AtomicOp::Xor:
        masm.as_xor(scratch, output, value);
        break;
      default:
        MOZ_CRASH();
    }

    masm.as_sc_w(scratch, scratch2, 0);
    masm.ma_b(scratch, Register(scratch), &again, Assembler::Zero, ShortJump);

    masm.memoryBarrierAfter(sync);

    return;
  }

  masm.as_andi(offsetTemp, scratch2, 3);
  masm.subPtr(offsetTemp, scratch2);
  masm.as_slli_w(offsetTemp, offsetTemp, 3);
  masm.ma_li(maskTemp, Imm32(UINT32_MAX >> ((4 - nbytes) * 8)));
  masm.as_sll_w(maskTemp, maskTemp, offsetTemp);
  masm.as_nor(maskTemp, zero, maskTemp);

  masm.memoryBarrierBefore(sync);

  masm.bind(&again);

  if (access) {
    masm.append(*access, wasm::TrapMachineInsn::Load32,
                FaultingCodeOffset(masm.currentOffset()));
  }

  masm.as_ll_w(scratch, scratch2, 0);
  masm.as_srl_w(output, scratch, offsetTemp);

  switch (op) {
    case AtomicOp::Add:
      masm.as_add_w(valueTemp, output, value);
      break;
    case AtomicOp::Sub:
      masm.as_sub_w(valueTemp, output, value);
      break;
    case AtomicOp::And:
      masm.as_and(valueTemp, output, value);
      break;
    case AtomicOp::Or:
      masm.as_or(valueTemp, output, value);
      break;
    case AtomicOp::Xor:
      masm.as_xor(valueTemp, output, value);
      break;
    default:
      MOZ_CRASH();
  }

  switch (nbytes) {
    case 1:
      masm.as_andi(valueTemp, valueTemp, 0xff);
      break;
    case 2:
      masm.as_bstrpick_d(valueTemp, valueTemp, 15, 0);
      break;
  }

  masm.as_sll_w(valueTemp, valueTemp, offsetTemp);

  masm.as_and(scratch, scratch, maskTemp);
  masm.as_or(scratch, scratch, valueTemp);

  masm.as_sc_w(scratch, scratch2, 0);

  masm.ma_b(scratch, Register(scratch), &again, Assembler::Zero, ShortJump);

  switch (nbytes) {
    case 1:
      if (signExtend) {
        masm.as_ext_w_b(output, output);
      } else {
        masm.as_andi(output, output, 0xff);
      }
      break;
    case 2:
      if (signExtend) {
        masm.as_ext_w_h(output, output);
      } else {
        masm.as_bstrpick_d(output, output, 15, 0);
      }
      break;
  }

  masm.memoryBarrierAfter(sync);
}

template <typename T>
static void AtomicFetchOp64(MacroAssembler& masm,
                            const wasm::MemoryAccessDesc* access,
                            Synchronization sync, AtomicOp op, Register64 value,
                            const T& mem, Register64 temp, Register64 output) {
  MOZ_ASSERT(value != output);
  MOZ_ASSERT(value != temp);
  UseScratchRegisterScope temps(masm);
  Register scratch = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch);

  Label tryAgain;

  masm.memoryBarrierBefore(sync);

  masm.bind(&tryAgain);

  if (access) {
    masm.append(*access, wasm::TrapMachineInsn::Load64,
                FaultingCodeOffset(masm.currentOffset()));
  }

  masm.as_ll_d(output.reg, scratch, 0);

  switch (op) {
    case AtomicOp::Add:
      masm.as_add_d(temp.reg, output.reg, value.reg);
      break;
    case AtomicOp::Sub:
      masm.as_sub_d(temp.reg, output.reg, value.reg);
      break;
    case AtomicOp::And:
      masm.as_and(temp.reg, output.reg, value.reg);
      break;
    case AtomicOp::Or:
      masm.as_or(temp.reg, output.reg, value.reg);
      break;
    case AtomicOp::Xor:
      masm.as_xor(temp.reg, output.reg, value.reg);
      break;
    default:
      MOZ_CRASH();
  }

  masm.as_sc_d(temp.reg, scratch, 0);
  masm.ma_b(temp.reg, temp.reg, &tryAgain, Assembler::Zero, ShortJump);

  masm.memoryBarrierAfter(sync);
}

void MacroAssembler::compareExchange(Scalar::Type type, Synchronization sync,
                                     const Address& mem, Register oldval,
                                     Register newval, Register valueTemp,
                                     Register offsetTemp, Register maskTemp,
                                     Register output) {
  CompareExchange(*this, nullptr, type, sync, mem, oldval, newval, valueTemp,
                  offsetTemp, maskTemp, output);
}

void MacroAssembler::compareExchange(Scalar::Type type, Synchronization sync,
                                     const BaseIndex& mem, Register oldval,
                                     Register newval, Register valueTemp,
                                     Register offsetTemp, Register maskTemp,
                                     Register output) {
  CompareExchange(*this, nullptr, type, sync, mem, oldval, newval, valueTemp,
                  offsetTemp, maskTemp, output);
}

void MacroAssembler::compareExchange64(Synchronization sync, const Address& mem,
                                       Register64 expect, Register64 replace,
                                       Register64 output) {
  CompareExchange64(*this, nullptr, sync, mem, expect, replace, output);
}

void MacroAssembler::compareExchange64(Synchronization sync,
                                       const BaseIndex& mem, Register64 expect,
                                       Register64 replace, Register64 output) {
  CompareExchange64(*this, nullptr, sync, mem, expect, replace, output);
}

void MacroAssembler::wasmCompareExchange(const wasm::MemoryAccessDesc& access,
                                         const Address& mem, Register oldval,
                                         Register newval, Register valueTemp,
                                         Register offsetTemp, Register maskTemp,
                                         Register output) {
  CompareExchange(*this, &access, access.type(), access.sync(), mem, oldval,
                  newval, valueTemp, offsetTemp, maskTemp, output);
}

void MacroAssembler::wasmCompareExchange(const wasm::MemoryAccessDesc& access,
                                         const BaseIndex& mem, Register oldval,
                                         Register newval, Register valueTemp,
                                         Register offsetTemp, Register maskTemp,
                                         Register output) {
  CompareExchange(*this, &access, access.type(), access.sync(), mem, oldval,
                  newval, valueTemp, offsetTemp, maskTemp, output);
}

void MacroAssembler::wasmCompareExchange64(const wasm::MemoryAccessDesc& access,
                                           const Address& mem,
                                           Register64 expect,
                                           Register64 replace,
                                           Register64 output) {
  CompareExchange64(*this, &access, access.sync(), mem, expect, replace,
                    output);
}

void MacroAssembler::wasmCompareExchange64(const wasm::MemoryAccessDesc& access,
                                           const BaseIndex& mem,
                                           Register64 expect,
                                           Register64 replace,
                                           Register64 output) {
  CompareExchange64(*this, &access, access.sync(), mem, expect, replace,
                    output);
}

void MacroAssembler::atomicExchange(Scalar::Type type, Synchronization sync,
                                    const Address& mem, Register value,
                                    Register valueTemp, Register offsetTemp,
                                    Register maskTemp, Register output) {
  AtomicExchange(*this, nullptr, type, sync, mem, value, valueTemp, offsetTemp,
                 maskTemp, output);
}

void MacroAssembler::atomicExchange(Scalar::Type type, Synchronization sync,
                                    const BaseIndex& mem, Register value,
                                    Register valueTemp, Register offsetTemp,
                                    Register maskTemp, Register output) {
  AtomicExchange(*this, nullptr, type, sync, mem, value, valueTemp, offsetTemp,
                 maskTemp, output);
}

void MacroAssembler::atomicExchange64(Synchronization sync, const Address& mem,
                                      Register64 value, Register64 output) {
  AtomicExchange64(*this, nullptr, sync, mem, value, output);
}

void MacroAssembler::atomicExchange64(Synchronization sync,
                                      const BaseIndex& mem, Register64 value,
                                      Register64 output) {
  AtomicExchange64(*this, nullptr, sync, mem, value, output);
}

void MacroAssembler::wasmAtomicExchange(const wasm::MemoryAccessDesc& access,
                                        const Address& mem, Register value,
                                        Register valueTemp, Register offsetTemp,
                                        Register maskTemp, Register output) {
  AtomicExchange(*this, &access, access.type(), access.sync(), mem, value,
                 valueTemp, offsetTemp, maskTemp, output);
}

void MacroAssembler::wasmAtomicExchange(const wasm::MemoryAccessDesc& access,
                                        const BaseIndex& mem, Register value,
                                        Register valueTemp, Register offsetTemp,
                                        Register maskTemp, Register output) {
  AtomicExchange(*this, &access, access.type(), access.sync(), mem, value,
                 valueTemp, offsetTemp, maskTemp, output);
}

void MacroAssembler::atomicFetchOp(Scalar::Type type, Synchronization sync,
                                   AtomicOp op, Register value,
                                   const Address& mem, Register valueTemp,
                                   Register offsetTemp, Register maskTemp,
                                   Register output) {
  AtomicFetchOp(*this, nullptr, type, sync, op, mem, value, valueTemp,
                offsetTemp, maskTemp, output);
}

void MacroAssembler::atomicFetchOp(Scalar::Type type, Synchronization sync,
                                   AtomicOp op, Register value,
                                   const BaseIndex& mem, Register valueTemp,
                                   Register offsetTemp, Register maskTemp,
                                   Register output) {
  AtomicFetchOp(*this, nullptr, type, sync, op, mem, value, valueTemp,
                offsetTemp, maskTemp, output);
}

void MacroAssembler::atomicFetchOp64(Synchronization sync, AtomicOp op,
                                     Register64 value, const Address& mem,
                                     Register64 temp, Register64 output) {
  AtomicFetchOp64(*this, nullptr, sync, op, value, mem, temp, output);
}

void MacroAssembler::atomicFetchOp64(Synchronization sync, AtomicOp op,
                                     Register64 value, const BaseIndex& mem,
                                     Register64 temp, Register64 output) {
  AtomicFetchOp64(*this, nullptr, sync, op, value, mem, temp, output);
}

void MacroAssembler::atomicEffectOp64(Synchronization sync, AtomicOp op,
                                      Register64 value, const Address& mem,
                                      Register64 temp) {
  AtomicFetchOp64(*this, nullptr, sync, op, value, mem, temp, temp);
}

void MacroAssembler::atomicEffectOp64(Synchronization sync, AtomicOp op,
                                      Register64 value, const BaseIndex& mem,
                                      Register64 temp) {
  AtomicFetchOp64(*this, nullptr, sync, op, value, mem, temp, temp);
}

void MacroAssembler::wasmAtomicFetchOp(const wasm::MemoryAccessDesc& access,
                                       AtomicOp op, Register value,
                                       const Address& mem, Register valueTemp,
                                       Register offsetTemp, Register maskTemp,
                                       Register output) {
  AtomicFetchOp(*this, &access, access.type(), access.sync(), op, mem, value,
                valueTemp, offsetTemp, maskTemp, output);
}

void MacroAssembler::wasmAtomicFetchOp(const wasm::MemoryAccessDesc& access,
                                       AtomicOp op, Register value,
                                       const BaseIndex& mem, Register valueTemp,
                                       Register offsetTemp, Register maskTemp,
                                       Register output) {
  AtomicFetchOp(*this, &access, access.type(), access.sync(), op, mem, value,
                valueTemp, offsetTemp, maskTemp, output);
}

template <typename T>
static void AtomicEffectOp(MacroAssembler& masm,
                           const wasm::MemoryAccessDesc* access,
                           Scalar::Type type, Synchronization sync, AtomicOp op,
                           const T& mem, Register value, Register valueTemp,
                           Register offsetTemp, Register maskTemp) {
  UseScratchRegisterScope temps(masm);
  unsigned nbytes = Scalar::byteSize(type);

  switch (nbytes) {
    case 1:
    case 2:
      break;
    case 4:
      MOZ_ASSERT(valueTemp == InvalidReg);
      MOZ_ASSERT(offsetTemp == InvalidReg);
      MOZ_ASSERT(maskTemp == InvalidReg);
      break;
    default:
      MOZ_CRASH();
  }

  Label again;

  Register scratch = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch);

  Register scratch2 = temps.Acquire();

  if (nbytes == 4) {
    masm.memoryBarrierBefore(sync);
    masm.bind(&again);

    if (access) {
      masm.append(*access, wasm::TrapMachineInsn::Load32,
                  FaultingCodeOffset(masm.currentOffset()));
    }

    masm.as_ll_w(scratch2, scratch, 0);

    switch (op) {
      case AtomicOp::Add:
        masm.as_add_w(scratch2, scratch2, value);
        break;
      case AtomicOp::Sub:
        masm.as_sub_w(scratch2, scratch2, value);
        break;
      case AtomicOp::And:
        masm.as_and(scratch2, scratch2, value);
        break;
      case AtomicOp::Or:
        masm.as_or(scratch2, scratch2, value);
        break;
      case AtomicOp::Xor:
        masm.as_xor(scratch2, scratch2, value);
        break;
      default:
        MOZ_CRASH();
    }

    masm.as_sc_w(scratch2, scratch, 0);
    masm.ma_b(scratch2, Register(scratch2), &again, Assembler::Zero, ShortJump);

    masm.memoryBarrierAfter(sync);

    return;
  }

  masm.as_andi(offsetTemp, scratch, 3);
  masm.subPtr(offsetTemp, scratch);
  masm.as_slli_w(offsetTemp, offsetTemp, 3);
  masm.ma_li(maskTemp, Imm32(UINT32_MAX >> ((4 - nbytes) * 8)));
  masm.as_sll_w(maskTemp, maskTemp, offsetTemp);
  masm.as_nor(maskTemp, zero, maskTemp);

  masm.memoryBarrierBefore(sync);

  masm.bind(&again);

  if (access) {
    masm.append(*access, wasm::TrapMachineInsn::Load32,
                FaultingCodeOffset(masm.currentOffset()));
  }

  masm.as_ll_w(scratch2, scratch, 0);
  masm.as_srl_w(valueTemp, scratch2, offsetTemp);

  switch (op) {
    case AtomicOp::Add:
      masm.as_add_w(valueTemp, valueTemp, value);
      break;
    case AtomicOp::Sub:
      masm.as_sub_w(valueTemp, valueTemp, value);
      break;
    case AtomicOp::And:
      masm.as_and(valueTemp, valueTemp, value);
      break;
    case AtomicOp::Or:
      masm.as_or(valueTemp, valueTemp, value);
      break;
    case AtomicOp::Xor:
      masm.as_xor(valueTemp, valueTemp, value);
      break;
    default:
      MOZ_CRASH();
  }

  switch (nbytes) {
    case 1:
      masm.as_andi(valueTemp, valueTemp, 0xff);
      break;
    case 2:
      masm.as_bstrpick_d(valueTemp, valueTemp, 15, 0);
      break;
  }

  masm.as_sll_w(valueTemp, valueTemp, offsetTemp);

  masm.as_and(scratch2, scratch2, maskTemp);
  masm.as_or(scratch2, scratch2, valueTemp);

  masm.as_sc_w(scratch2, scratch, 0);

  masm.ma_b(scratch2, Register(scratch2), &again, Assembler::Zero, ShortJump);

  masm.memoryBarrierAfter(sync);
}

void MacroAssembler::wasmAtomicEffectOp(const wasm::MemoryAccessDesc& access,
                                        AtomicOp op, Register value,
                                        const Address& mem, Register valueTemp,
                                        Register offsetTemp,
                                        Register maskTemp) {
  AtomicEffectOp(*this, &access, access.type(), access.sync(), op, mem, value,
                 valueTemp, offsetTemp, maskTemp);
}

void MacroAssembler::wasmAtomicEffectOp(const wasm::MemoryAccessDesc& access,
                                        AtomicOp op, Register value,
                                        const BaseIndex& mem,
                                        Register valueTemp, Register offsetTemp,
                                        Register maskTemp) {
  AtomicEffectOp(*this, &access, access.type(), access.sync(), op, mem, value,
                 valueTemp, offsetTemp, maskTemp);
}

template <typename T>
static void WasmAtomicExchange64(MacroAssembler& masm,
                                 const wasm::MemoryAccessDesc& access,
                                 const T& mem, Register64 value,
                                 Register64 output) {
  AtomicExchange64(masm, &access, access.sync(), mem, value, output);
}

void MacroAssembler::wasmAtomicExchange64(const wasm::MemoryAccessDesc& access,
                                          const Address& mem, Register64 src,
                                          Register64 output) {
  WasmAtomicExchange64(*this, access, mem, src, output);
}

void MacroAssembler::wasmAtomicExchange64(const wasm::MemoryAccessDesc& access,
                                          const BaseIndex& mem, Register64 src,
                                          Register64 output) {
  WasmAtomicExchange64(*this, access, mem, src, output);
}

void MacroAssembler::wasmAtomicFetchOp64(const wasm::MemoryAccessDesc& access,
                                         AtomicOp op, Register64 value,
                                         const Address& mem, Register64 temp,
                                         Register64 output) {
  AtomicFetchOp64(*this, &access, access.sync(), op, value, mem, temp, output);
}

void MacroAssembler::wasmAtomicFetchOp64(const wasm::MemoryAccessDesc& access,
                                         AtomicOp op, Register64 value,
                                         const BaseIndex& mem, Register64 temp,
                                         Register64 output) {
  AtomicFetchOp64(*this, &access, access.sync(), op, value, mem, temp, output);
}

// ========================================================================
// JS atomic operations.

template <typename T>
static void CompareExchangeJS(MacroAssembler& masm, Scalar::Type arrayType,
                              Synchronization sync, const T& mem,
                              Register oldval, Register newval,
                              Register valueTemp, Register offsetTemp,
                              Register maskTemp, Register temp,
                              AnyRegister output) {
  if (arrayType == Scalar::Uint32) {
    masm.compareExchange(arrayType, sync, mem, oldval, newval, valueTemp,
                         offsetTemp, maskTemp, temp);
    masm.convertUInt32ToDouble(temp, output.fpu());
  } else {
    masm.compareExchange(arrayType, sync, mem, oldval, newval, valueTemp,
                         offsetTemp, maskTemp, output.gpr());
  }
}

template <typename T>
static void AtomicExchangeJS(MacroAssembler& masm, Scalar::Type arrayType,
                             Synchronization sync, const T& mem, Register value,
                             Register valueTemp, Register offsetTemp,
                             Register maskTemp, Register temp,
                             AnyRegister output) {
  if (arrayType == Scalar::Uint32) {
    masm.atomicExchange(arrayType, sync, mem, value, valueTemp, offsetTemp,
                        maskTemp, temp);
    masm.convertUInt32ToDouble(temp, output.fpu());
  } else {
    masm.atomicExchange(arrayType, sync, mem, value, valueTemp, offsetTemp,
                        maskTemp, output.gpr());
  }
}

template <typename T>
static void AtomicFetchOpJS(MacroAssembler& masm, Scalar::Type arrayType,
                            Synchronization sync, AtomicOp op, Register value,
                            const T& mem, Register valueTemp,
                            Register offsetTemp, Register maskTemp,
                            Register temp, AnyRegister output) {
  if (arrayType == Scalar::Uint32) {
    masm.atomicFetchOp(arrayType, sync, op, value, mem, valueTemp, offsetTemp,
                       maskTemp, temp);
    masm.convertUInt32ToDouble(temp, output.fpu());
  } else {
    masm.atomicFetchOp(arrayType, sync, op, value, mem, valueTemp, offsetTemp,
                       maskTemp, output.gpr());
  }
}

void MacroAssembler::compareExchangeJS(Scalar::Type arrayType,
                                       Synchronization sync, const Address& mem,
                                       Register oldval, Register newval,
                                       Register valueTemp, Register offsetTemp,
                                       Register maskTemp, Register temp,
                                       AnyRegister output) {
  CompareExchangeJS(*this, arrayType, sync, mem, oldval, newval, valueTemp,
                    offsetTemp, maskTemp, temp, output);
}

void MacroAssembler::compareExchangeJS(Scalar::Type arrayType,
                                       Synchronization sync,
                                       const BaseIndex& mem, Register oldval,
                                       Register newval, Register valueTemp,
                                       Register offsetTemp, Register maskTemp,
                                       Register temp, AnyRegister output) {
  CompareExchangeJS(*this, arrayType, sync, mem, oldval, newval, valueTemp,
                    offsetTemp, maskTemp, temp, output);
}

void MacroAssembler::atomicExchangeJS(Scalar::Type arrayType,
                                      Synchronization sync, const Address& mem,
                                      Register value, Register valueTemp,
                                      Register offsetTemp, Register maskTemp,
                                      Register temp, AnyRegister output) {
  AtomicExchangeJS(*this, arrayType, sync, mem, value, valueTemp, offsetTemp,
                   maskTemp, temp, output);
}

void MacroAssembler::atomicExchangeJS(Scalar::Type arrayType,
                                      Synchronization sync,
                                      const BaseIndex& mem, Register value,
                                      Register valueTemp, Register offsetTemp,
                                      Register maskTemp, Register temp,
                                      AnyRegister output) {
  AtomicExchangeJS(*this, arrayType, sync, mem, value, valueTemp, offsetTemp,
                   maskTemp, temp, output);
}

void MacroAssembler::atomicFetchOpJS(Scalar::Type arrayType,
                                     Synchronization sync, AtomicOp op,
                                     Register value, const Address& mem,
                                     Register valueTemp, Register offsetTemp,
                                     Register maskTemp, Register temp,
                                     AnyRegister output) {
  AtomicFetchOpJS(*this, arrayType, sync, op, value, mem, valueTemp, offsetTemp,
                  maskTemp, temp, output);
}

void MacroAssembler::atomicFetchOpJS(Scalar::Type arrayType,
                                     Synchronization sync, AtomicOp op,
                                     Register value, const BaseIndex& mem,
                                     Register valueTemp, Register offsetTemp,
                                     Register maskTemp, Register temp,
                                     AnyRegister output) {
  AtomicFetchOpJS(*this, arrayType, sync, op, value, mem, valueTemp, offsetTemp,
                  maskTemp, temp, output);
}

void MacroAssembler::atomicEffectOpJS(Scalar::Type arrayType,
                                      Synchronization sync, AtomicOp op,
                                      Register value, const BaseIndex& mem,
                                      Register valueTemp, Register offsetTemp,
                                      Register maskTemp) {
  AtomicEffectOp(*this, nullptr, arrayType, sync, op, mem, value, valueTemp,
                 offsetTemp, maskTemp);
}

void MacroAssembler::atomicEffectOpJS(Scalar::Type arrayType,
                                      Synchronization sync, AtomicOp op,
                                      Register value, const Address& mem,
                                      Register valueTemp, Register offsetTemp,
                                      Register maskTemp) {
  AtomicEffectOp(*this, nullptr, arrayType, sync, op, mem, value, valueTemp,
                 offsetTemp, maskTemp);
}

void MacroAssembler::atomicPause() {
  // LoongArch doesn't have 'pause' or 'yield' instructions like other
  // platforms, just use nop here.
  nop();
}

void MacroAssembler::flexibleQuotient32(Register lhs, Register rhs,
                                        Register dest, bool isUnsigned,
                                        const LiveRegisterSet&) {
  quotient32(lhs, rhs, dest, isUnsigned);
}

void MacroAssembler::flexibleQuotientPtr(Register lhs, Register rhs,
                                         Register dest, bool isUnsigned,
                                         const LiveRegisterSet&) {
  quotient64(lhs, rhs, dest, isUnsigned);
}

void MacroAssembler::flexibleRemainder32(Register lhs, Register rhs,
                                         Register dest, bool isUnsigned,
                                         const LiveRegisterSet&) {
  remainder32(lhs, rhs, dest, isUnsigned);
}

void MacroAssembler::flexibleRemainderPtr(Register lhs, Register rhs,
                                          Register dest, bool isUnsigned,
                                          const LiveRegisterSet&) {
  remainder64(lhs, rhs, dest, isUnsigned);
}

void MacroAssembler::flexibleDivMod32(Register lhs, Register rhs,
                                      Register divOutput, Register remOutput,
                                      bool isUnsigned, const LiveRegisterSet&) {
  MOZ_ASSERT(lhs != divOutput && lhs != remOutput, "lhs is preserved");
  MOZ_ASSERT(rhs != divOutput && rhs != remOutput, "rhs is preserved");

  if (isUnsigned) {
    as_div_wu(divOutput, lhs, rhs);
    as_mod_wu(remOutput, lhs, rhs);
  } else {
    as_div_w(divOutput, lhs, rhs);
    as_mod_w(remOutput, lhs, rhs);
  }
}

CodeOffset MacroAssembler::moveNearAddressWithPatch(Register dest) {
  return movWithPatch(ImmPtr(nullptr), dest);
}

void MacroAssembler::patchNearAddressMove(CodeLocationLabel loc,
                                          CodeLocationLabel target) {
  PatchDataWithValueCheck(loc, ImmPtr(target.raw()), ImmPtr(nullptr));
}

// ========================================================================
// Spectre Mitigations.

void MacroAssembler::speculationBarrier() { MOZ_CRASH(); }

void MacroAssembler::floorFloat32ToInt32(FloatRegister src, Register dest,
                                         Label* fail) {
  ScratchFloat32Scope fpscratch(asMasm());

  // Round toward negative infinity.
  as_ftintrm_l_s(fpscratch, src);
  moveFromDouble(fpscratch, dest);

  // Sign extend lower 32 bits to test if the result isn't an Int32.
  {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();

    move32SignExtendToPtr(dest, scratch);
    branchPtr(Assembler::NotEqual, dest, scratch, fail);
  }

  // We have to check for -0 and NaN when the result is zero.
  Label notZero;
  ma_b(dest, zero, &notZero, Assembler::NotEqual, ShortJump);
  {
    // If any of the two most significant bits is set, |src| is -0 or NaN.
    moveFromFloat32(src, dest);
    as_srli_w(dest, dest, 30);
    branch32(Assembler::NotEqual, dest, zero, fail);
  }
  bind(&notZero);
}

void MacroAssembler::floorDoubleToInt32(FloatRegister src, Register dest,
                                        Label* fail) {
  ScratchDoubleScope fpscratch(asMasm());

  // Round toward negative infinity.
  as_ftintrm_l_d(fpscratch, src);
  moveFromDouble(fpscratch, dest);

  // Sign extend lower 32 bits to test if the result isn't an Int32.
  {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();

    move32SignExtendToPtr(dest, scratch);
    branchPtr(Assembler::NotEqual, dest, scratch, fail);
  }

  // We have to check for -0 and NaN when the result is zero.
  Label notZero;
  ma_b(dest, zero, &notZero, Assembler::NotEqual, ShortJump);
  {
    // If any of the two most significant bits is set, |src| is negative or NaN.
    moveFromDouble(src, dest);
    as_srli_d(dest, dest, 62);
    branchPtr(Assembler::NotEqual, dest, zero, fail);
  }
  bind(&notZero);
}

void MacroAssembler::ceilFloat32ToInt32(FloatRegister src, Register dest,
                                        Label* fail) {
  ScratchFloat32Scope fpscratch(asMasm());

  // Round toward positive infinity.
  as_ftintrp_l_s(fpscratch, src);
  moveFromDouble(fpscratch, dest);

  // Sign extend lower 32 bits to test if the result isn't an Int32.
  {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();

    move32SignExtendToPtr(dest, scratch);
    branchPtr(Assembler::NotEqual, dest, scratch, fail);
  }

  // We have to check for (-1, -0] and NaN when the result is zero.
  Label notZero;
  ma_b(dest, zero, &notZero, Assembler::NotEqual, ShortJump);
  {
    // If binary value is not zero, the input was not 0, so we bail.
    moveFromFloat32(src, dest);
    branch32(Assembler::NotEqual, dest, zero, fail);
  }
  bind(&notZero);
}

void MacroAssembler::ceilDoubleToInt32(FloatRegister src, Register dest,
                                       Label* fail) {
  ScratchDoubleScope fpscratch(asMasm());

  // Round toward positive infinity.
  as_ftintrp_l_d(fpscratch, src);
  moveFromDouble(fpscratch, dest);

  // Sign extend lower 32 bits to test if the result isn't an Int32.
  {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();

    move32SignExtendToPtr(dest, scratch);
    branchPtr(Assembler::NotEqual, dest, scratch, fail);
  }

  // We have to check for (-1, -0] and NaN when the result is zero.
  Label notZero;
  ma_b(dest, zero, &notZero, Assembler::NotEqual, ShortJump);
  {
    // If binary value is not zero, the input was not 0, so we bail.
    moveFromDouble(src, dest);
    branchPtr(Assembler::NotEqual, dest, zero, fail);
  }
  bind(&notZero);
}

void MacroAssembler::roundFloat32ToInt32(FloatRegister src, Register dest,
                                         FloatRegister temp, Label* fail) {
  ScratchFloat32Scope fpscratch(*this);

  Label negative, end, performRound;

  // Branch for negative inputs. Doesn't catch NaN or -0.
  loadConstantFloat32(0.0f, fpscratch);
  ma_bc_s(src, fpscratch, &negative, Assembler::DoubleLessThan, ShortJump);

  // If non-negative check for bailout.
  ma_bc_s(src, fpscratch, &performRound, Assembler::DoubleNotEqual, ShortJump);
  {
    // If binary value is not zero, it is NaN or -0, so we bail.
    moveFromFloat32(src, dest);
    branch32(Assembler::NotEqual, dest, zero, fail);
    ma_b(&end, ShortJump);
  }

  // Input is negative, but isn't -0.
  bind(&negative);
  {
    // Inputs in [-0.5, 0) are rounded to -0. Fail.
    loadConstantFloat32(-0.5f, fpscratch);
    branchFloat(Assembler::DoubleGreaterThanOrEqual, src, fpscratch, fail);
  }

  bind(&performRound);
  {
    // Load biggest number less than 0.5 in the temp register.
    loadConstantFloat32(GetBiggestNumberLessThan(0.5f), temp);

    // Other inputs need the biggest float less than 0.5 added.
    as_fadd_s(fpscratch, src, temp);

    // Round toward negative infinity.
    as_ftintrm_l_s(fpscratch, fpscratch);
    moveFromDouble(fpscratch, dest);

    // Sign extend lower 32 bits to test if the result isn't an Int32.
    {
      UseScratchRegisterScope temps(*this);
      Register scratch = temps.Acquire();

      move32SignExtendToPtr(dest, scratch);
      branchPtr(Assembler::NotEqual, dest, scratch, fail);
    }
  }
  bind(&end);
}

void MacroAssembler::roundDoubleToInt32(FloatRegister src, Register dest,
                                        FloatRegister temp, Label* fail) {
  ScratchDoubleScope fpscratch(*this);

  Label negative, end, performRound;

  // Branch for negative inputs. Doesn't catch NaN or -0.
  loadConstantDouble(0.0, fpscratch);
  ma_bc_d(src, fpscratch, &negative, Assembler::DoubleLessThan, ShortJump);

  // If non-negative check for bailout.
  ma_bc_d(src, fpscratch, &performRound, Assembler::DoubleNotEqual, ShortJump);
  {
    // If binary value is not zero, it is NaN or -0, so we bail.
    moveFromDouble(src, dest);
    branchPtr(Assembler::NotEqual, dest, zero, fail);
    ma_b(&end, ShortJump);
  }

  // Input is negative, but isn't -0.
  bind(&negative);
  {
    // Inputs in [-0.5, 0) are rounded to -0. Fail.
    loadConstantDouble(-0.5, fpscratch);
    branchDouble(Assembler::DoubleGreaterThanOrEqual, src, fpscratch, fail);
  }

  bind(&performRound);
  {
    // Load biggest number less than 0.5 in the temp register.
    loadConstantDouble(GetBiggestNumberLessThan(0.5), temp);

    // Other inputs need the biggest double less than 0.5 added.
    as_fadd_d(fpscratch, src, temp);

    // Round toward negative infinity.
    as_ftintrm_l_d(fpscratch, fpscratch);
    moveFromDouble(fpscratch, dest);

    // Sign extend lower 32 bits to test if the result isn't an Int32.
    {
      UseScratchRegisterScope temps(*this);
      Register scratch = temps.Acquire();

      move32SignExtendToPtr(dest, scratch);
      branchPtr(Assembler::NotEqual, dest, scratch, fail);
    }
  }
  bind(&end);
}

void MacroAssembler::truncFloat32ToInt32(FloatRegister src, Register dest,
                                         Label* fail) {
  ScratchFloat32Scope fpscratch(asMasm());

  // Round toward zero.
  as_ftintrz_l_s(fpscratch, src);
  moveFromDouble(fpscratch, dest);

  // Sign extend lower 32 bits to test if the result isn't an Int32.
  {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();

    move32SignExtendToPtr(dest, scratch);
    branchPtr(Assembler::NotEqual, dest, scratch, fail);
  }

  // We have to check for (-1, -0] and NaN when the result is zero.
  Label notZero;
  ma_b(dest, zero, &notZero, Assembler::NotEqual, ShortJump);
  {
    // If any of the two most significant bits is set, |src| is negative or NaN.
    moveFromFloat32(src, dest);
    as_srli_w(dest, dest, 30);
    branch32(Assembler::NotEqual, dest, zero, fail);
  }
  bind(&notZero);
}

void MacroAssembler::truncDoubleToInt32(FloatRegister src, Register dest,
                                        Label* fail) {
  ScratchDoubleScope fpscratch(asMasm());

  // Round toward zero.
  as_ftintrz_l_d(fpscratch, src);
  moveFromDouble(fpscratch, dest);

  // Sign extend lower 32 bits to test if the result isn't an Int32.
  {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();

    move32SignExtendToPtr(dest, scratch);
    branchPtr(Assembler::NotEqual, dest, scratch, fail);
  }

  // We have to check for (-1, -0] and NaN when the result is zero.
  Label notZero;
  ma_b(dest, zero, &notZero, Assembler::NotEqual, ShortJump);
  {
    // If any of the two most significant bits is set, |src| is negative or NaN.
    moveFromDouble(src, dest);
    as_srli_d(dest, dest, 62);
    branchPtr(Assembler::NotEqual, dest, zero, fail);
  }
  bind(&notZero);
}

void MacroAssembler::nearbyIntDouble(RoundingMode mode, FloatRegister src,
                                     FloatRegister dest) {
  MOZ_CRASH("not supported on this platform");
}

void MacroAssembler::nearbyIntFloat32(RoundingMode mode, FloatRegister src,
                                      FloatRegister dest) {
  MOZ_CRASH("not supported on this platform");
}

void MacroAssembler::copySignDouble(FloatRegister lhs, FloatRegister rhs,
                                    FloatRegister output) {
  as_fcopysign_d(output, lhs, rhs);
}

void MacroAssembler::copySignFloat32(FloatRegister lhs, FloatRegister rhs,
                                     FloatRegister output) {
  as_fcopysign_s(output, lhs, rhs);
}

void MacroAssemblerLOONG64Compat::move32(Imm32 imm, Register dest) {
  ma_li(dest, imm);
}

void MacroAssemblerLOONG64Compat::move32(Register src, Register dest) {
  as_slli_w(dest, src, 0);
}

void MacroAssemblerLOONG64Compat::movePtr(Register src, Register dest) {
  as_or(dest, src, zero);
}
void MacroAssemblerLOONG64Compat::movePtr(ImmWord imm, Register dest) {
  ma_li(dest, imm);
}

void MacroAssemblerLOONG64Compat::movePtr(ImmGCPtr imm, Register dest) {
  ma_li(dest, imm);
}

void MacroAssemblerLOONG64Compat::movePtr(ImmPtr imm, Register dest) {
  movePtr(ImmWord(uintptr_t(imm.value)), dest);
}

void MacroAssemblerLOONG64Compat::movePtr(wasm::SymbolicAddress imm,
                                          Register dest) {
  append(wasm::SymbolicAccess(CodeOffset(nextOffset().getOffset()), imm));
  ma_liPatchable(dest, ImmWord(-1));
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::load8ZeroExtend(
    const Address& address, Register dest) {
  return ma_load(dest, address, SizeByte, ZeroExtend);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::load8ZeroExtend(
    const BaseIndex& src, Register dest) {
  return ma_load(dest, src, SizeByte, ZeroExtend);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::load8SignExtend(
    const Address& address, Register dest) {
  return ma_load(dest, address, SizeByte, SignExtend);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::load8SignExtend(
    const BaseIndex& src, Register dest) {
  return ma_load(dest, src, SizeByte, SignExtend);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::load16ZeroExtend(
    const Address& address, Register dest) {
  return ma_load(dest, address, SizeHalfWord, ZeroExtend);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::load16ZeroExtend(
    const BaseIndex& src, Register dest) {
  return ma_load(dest, src, SizeHalfWord, ZeroExtend);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::load16SignExtend(
    const Address& address, Register dest) {
  return ma_load(dest, address, SizeHalfWord, SignExtend);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::load16SignExtend(
    const BaseIndex& src, Register dest) {
  return ma_load(dest, src, SizeHalfWord, SignExtend);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::load32(const Address& address,
                                                       Register dest) {
  return ma_ld_w(dest, address);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::load32(const BaseIndex& address,
                                                       Register dest) {
  Register base = address.base;
  Register index = address.index;
  int32_t offset = address.offset;
  uint32_t shift = Imm32::ShiftOf(address.scale).value;
  js::wasm::FaultingCodeOffset fco;

  UseScratchRegisterScope temps(asMasm());
  if (offset != 0) {
    Register scratch = temps.Acquire();
    ma_li(scratch, Imm32(offset));
    if (shift != 0) {
      MOZ_ASSERT(shift <= 4);
      as_alsl_d(scratch, index, scratch, shift - 1);
    } else {
      as_add_d(scratch, index, scratch);
    }
    fco = js::wasm::FaultingCodeOffset(currentOffset());
    as_ldx_w(dest, base, scratch);
  } else if (shift != 0) {
    Register scratch = temps.Acquire();
    as_slli_d(scratch, index, shift);
    fco = js::wasm::FaultingCodeOffset(currentOffset());
    as_ldx_w(dest, base, scratch);
  } else {
    fco = js::wasm::FaultingCodeOffset(currentOffset());
    as_ldx_w(dest, base, index);
  }
  return fco;
}

void MacroAssemblerLOONG64Compat::load32(AbsoluteAddress address,
                                         Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  movePtr(ImmPtr(address.addr), scratch);
  load32(Address(scratch, 0), dest);
}

void MacroAssemblerLOONG64Compat::load32(wasm::SymbolicAddress address,
                                         Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  movePtr(address, scratch);
  load32(Address(scratch, 0), dest);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::loadPtr(const Address& address,
                                                        Register dest) {
  return ma_ld_d(dest, address);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::loadPtr(const BaseIndex& src,
                                                        Register dest) {
  Register base = src.base;
  Register index = src.index;
  int32_t offset = src.offset;
  uint32_t shift = Imm32::ShiftOf(src.scale).value;
  js::wasm::FaultingCodeOffset fco;

  UseScratchRegisterScope temps(asMasm());
  if (offset != 0) {
    Register scratch = temps.Acquire();
    ma_li(scratch, Imm32(offset));
    if (shift != 0) {
      MOZ_ASSERT(shift <= 4);
      as_alsl_d(scratch, index, scratch, shift - 1);
    } else {
      as_add_d(scratch, index, scratch);
    }
    fco = js::wasm::FaultingCodeOffset(currentOffset());
    as_ldx_d(dest, base, scratch);
  } else if (shift != 0) {
    Register scratch = temps.Acquire();
    as_slli_d(scratch, index, shift);
    fco = js::wasm::FaultingCodeOffset(currentOffset());
    as_ldx_d(dest, base, scratch);
  } else {
    fco = js::wasm::FaultingCodeOffset(currentOffset());
    as_ldx_d(dest, base, index);
  }
  return fco;
}

void MacroAssemblerLOONG64Compat::loadPtr(AbsoluteAddress address,
                                          Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  movePtr(ImmPtr(address.addr), scratch);
  loadPtr(Address(scratch, 0), dest);
}

void MacroAssemblerLOONG64Compat::loadPtr(wasm::SymbolicAddress address,
                                          Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  movePtr(address, scratch);
  loadPtr(Address(scratch, 0), dest);
}

void MacroAssemblerLOONG64Compat::loadPrivate(const Address& address,
                                              Register dest) {
  loadPtr(address, dest);
}

void MacroAssemblerLOONG64Compat::store8(Imm32 imm, const Address& address) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ma_li(scratch, imm);
  ma_store(scratch, address, SizeByte);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::store8(Register src,
                                                       const Address& address) {
  return ma_store(src, address, SizeByte);
}

void MacroAssemblerLOONG64Compat::store8(Imm32 imm, const BaseIndex& dest) {
  ma_store(imm, dest, SizeByte);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::store8(Register src,
                                                       const BaseIndex& dest) {
  return ma_store(src, dest, SizeByte);
}

void MacroAssemblerLOONG64Compat::store16(Imm32 imm, const Address& address) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ma_li(scratch, imm);
  ma_store(scratch, address, SizeHalfWord);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::store16(
    Register src, const Address& address) {
  return ma_store(src, address, SizeHalfWord);
}

void MacroAssemblerLOONG64Compat::store16(Imm32 imm, const BaseIndex& dest) {
  ma_store(imm, dest, SizeHalfWord);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::store16(
    Register src, const BaseIndex& address) {
  return ma_store(src, address, SizeHalfWord);
}

void MacroAssemblerLOONG64Compat::store32(Register src,
                                          AbsoluteAddress address) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  movePtr(ImmPtr(address.addr), scratch);
  store32(src, Address(scratch, 0));
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::store32(
    Register src, const Address& address) {
  return ma_store(src, address, SizeWord);
}

void MacroAssemblerLOONG64Compat::store32(Imm32 src, const Address& address) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  move32(src, scratch);
  ma_store(scratch, address, SizeWord);
}

void MacroAssemblerLOONG64Compat::store32(Imm32 imm, const BaseIndex& dest) {
  ma_store(imm, dest, SizeWord);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::store32(Register src,
                                                        const BaseIndex& dest) {
  return ma_store(src, dest, SizeWord);
}

template <typename T>
void MacroAssemblerLOONG64Compat::storePtr(ImmWord imm, T address) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  ma_li(scratch, imm);
  ma_store(scratch, address, SizeDouble);
}

template void MacroAssemblerLOONG64Compat::storePtr<Address>(ImmWord imm,
                                                             Address address);
template void MacroAssemblerLOONG64Compat::storePtr<BaseIndex>(
    ImmWord imm, BaseIndex address);

template <typename T>
void MacroAssemblerLOONG64Compat::storePtr(ImmPtr imm, T address) {
  storePtr(ImmWord(uintptr_t(imm.value)), address);
}

template void MacroAssemblerLOONG64Compat::storePtr<Address>(ImmPtr imm,
                                                             Address address);
template void MacroAssemblerLOONG64Compat::storePtr<BaseIndex>(
    ImmPtr imm, BaseIndex address);

template <typename T>
void MacroAssemblerLOONG64Compat::storePtr(ImmGCPtr imm, T address) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  movePtr(imm, scratch);
  storePtr(scratch, address);
}

template void MacroAssemblerLOONG64Compat::storePtr<Address>(ImmGCPtr imm,
                                                             Address address);
template void MacroAssemblerLOONG64Compat::storePtr<BaseIndex>(
    ImmGCPtr imm, BaseIndex address);

FaultingCodeOffset MacroAssemblerLOONG64Compat::storePtr(
    Register src, const Address& address) {
  return ma_st_d(src, address);
}

FaultingCodeOffset MacroAssemblerLOONG64Compat::storePtr(
    Register src, const BaseIndex& address) {
  Register base = address.base;
  Register index = address.index;
  int32_t offset = address.offset;
  int32_t shift = Imm32::ShiftOf(address.scale).value;
  FaultingCodeOffset fco;

  UseScratchRegisterScope temps(asMasm());
  if ((offset == 0) && (shift == 0)) {
    fco = FaultingCodeOffset(currentOffset());
    as_stx_d(src, base, index);
  } else if (is_intN(offset, 12)) {
    Register scratch = temps.Acquire();
    if (shift == 0) {
      as_add_d(scratch, base, index);
    } else {
      as_alsl_d(scratch, index, base, shift - 1);
    }
    fco = FaultingCodeOffset(currentOffset());
    as_st_d(src, scratch, offset);
  } else {
    Register scratch = temps.Acquire();
    ma_li(scratch, Imm32(offset));
    if (shift == 0) {
      as_add_d(scratch, scratch, index);
    } else {
      as_alsl_d(scratch, index, scratch, shift - 1);
    }
    fco = FaultingCodeOffset(currentOffset());
    as_stx_d(src, base, scratch);
  }
  return fco;
}

void MacroAssemblerLOONG64Compat::storePtr(Register src, AbsoluteAddress dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  movePtr(ImmPtr(dest.addr), scratch);
  storePtr(src, Address(scratch, 0));
}

void MacroAssemblerLOONG64Compat::testNullSet(Condition cond,
                                              const ValueOperand& value,
                                              Register dest) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  splitTag(value, scratch);
  ma_cmp_set(dest, scratch, ImmTag(JSVAL_TAG_NULL), cond);
}

void MacroAssemblerLOONG64Compat::testObjectSet(Condition cond,
                                                const ValueOperand& value,
                                                Register dest) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  splitTag(value, scratch);
  ma_cmp_set(dest, scratch, ImmTag(JSVAL_TAG_OBJECT), cond);
}

void MacroAssemblerLOONG64Compat::testUndefinedSet(Condition cond,
                                                   const ValueOperand& value,
                                                   Register dest) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  splitTag(value, scratch);
  ma_cmp_set(dest, scratch, ImmTag(JSVAL_TAG_UNDEFINED), cond);
}

void MacroAssemblerLOONG64Compat::unboxInt32(const ValueOperand& operand,
                                             Register dest) {
  as_slli_w(dest, operand.valueReg(), 0);
}

void MacroAssemblerLOONG64Compat::unboxInt32(Register src, Register dest) {
  as_slli_w(dest, src, 0);
}

void MacroAssemblerLOONG64Compat::unboxInt32(const Address& src,
                                             Register dest) {
  load32(Address(src.base, src.offset), dest);
}

void MacroAssemblerLOONG64Compat::unboxInt32(const BaseIndex& src,
                                             Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  computeScaledAddress(src, scratch);
  load32(Address(scratch, src.offset), dest);
}

void MacroAssemblerLOONG64Compat::unboxBoolean(const ValueOperand& operand,
                                               Register dest) {
  as_slli_w(dest, operand.valueReg(), 0);
}

void MacroAssemblerLOONG64Compat::unboxBoolean(Register src, Register dest) {
  as_slli_w(dest, src, 0);
}

void MacroAssemblerLOONG64Compat::unboxBoolean(const Address& src,
                                               Register dest) {
  ma_ld_w(dest, src);
}

void MacroAssemblerLOONG64Compat::unboxBoolean(const BaseIndex& src,
                                               Register dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  computeScaledAddress(src, scratch);
  ma_ld_w(dest, Address(scratch, src.offset));
}

void MacroAssemblerLOONG64Compat::unboxDouble(const ValueOperand& operand,
                                              FloatRegister dest) {
  as_movgr2fr_d(dest, operand.valueReg());
}

void MacroAssemblerLOONG64Compat::unboxDouble(const Address& src,
                                              FloatRegister dest) {
  ma_fld_d(dest, Address(src.base, src.offset));
}

void MacroAssemblerLOONG64Compat::unboxDouble(const BaseIndex& src,
                                              FloatRegister dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  loadPtr(src, scratch);
  unboxDouble(ValueOperand(scratch), dest);
}

void MacroAssemblerLOONG64Compat::unboxString(const ValueOperand& operand,
                                              Register dest) {
  unboxNonDouble(operand, dest, JSVAL_TYPE_STRING);
}

void MacroAssemblerLOONG64Compat::unboxString(Register src, Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_STRING);
}

void MacroAssemblerLOONG64Compat::unboxString(const Address& src,
                                              Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_STRING);
}

void MacroAssemblerLOONG64Compat::unboxSymbol(const ValueOperand& operand,
                                              Register dest) {
  unboxNonDouble(operand, dest, JSVAL_TYPE_SYMBOL);
}

void MacroAssemblerLOONG64Compat::unboxSymbol(Register src, Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_SYMBOL);
}

void MacroAssemblerLOONG64Compat::unboxSymbol(const Address& src,
                                              Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_SYMBOL);
}

void MacroAssemblerLOONG64Compat::unboxBigInt(const ValueOperand& operand,
                                              Register dest) {
  unboxNonDouble(operand, dest, JSVAL_TYPE_BIGINT);
}

void MacroAssemblerLOONG64Compat::unboxBigInt(Register src, Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_BIGINT);
}

void MacroAssemblerLOONG64Compat::unboxBigInt(const Address& src,
                                              Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_BIGINT);
}

void MacroAssemblerLOONG64Compat::unboxObject(const ValueOperand& src,
                                              Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_OBJECT);
}

void MacroAssemblerLOONG64Compat::unboxObject(Register src, Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_OBJECT);
}

void MacroAssemblerLOONG64Compat::unboxObject(const Address& src,
                                              Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_OBJECT);
}

void MacroAssemblerLOONG64Compat::unboxValue(const ValueOperand& src,
                                             AnyRegister dest,
                                             JSValueType type) {
  if (dest.isFloat()) {
    Label notInt32, end;
    asMasm().branchTestInt32(Assembler::NotEqual, src, &notInt32);
    convertInt32ToDouble(src.valueReg(), dest.fpu());
    ma_b(&end, ShortJump);
    bind(&notInt32);
    unboxDouble(src, dest.fpu());
    bind(&end);
  } else {
    unboxNonDouble(src, dest.gpr(), type);
  }
}

void MacroAssemblerLOONG64Compat::boxDouble(FloatRegister src,
                                            const ValueOperand& dest,
                                            FloatRegister) {
  as_movfr2gr_d(dest.valueReg(), src);
}

#ifdef DEBUG
static constexpr int32_t PayloadSize(JSValueType type) {
  switch (type) {
    case JSVAL_TYPE_UNDEFINED:
    case JSVAL_TYPE_NULL:
      return 0;
    case JSVAL_TYPE_BOOLEAN:
      return 1;
    case JSVAL_TYPE_INT32:
    case JSVAL_TYPE_MAGIC:
      return 32;
    case JSVAL_TYPE_STRING:
    case JSVAL_TYPE_SYMBOL:
    case JSVAL_TYPE_PRIVATE_GCTHING:
    case JSVAL_TYPE_BIGINT:
    case JSVAL_TYPE_OBJECT:
      return JSVAL_TAG_SHIFT;
    case JSVAL_TYPE_DOUBLE:
    case JSVAL_TYPE_UNKNOWN:
      break;
  }
  MOZ_CRASH("bad value type");
}
#endif

static void AssertValidPayload(MacroAssemblerLOONG64Compat& masm,
                               JSValueType type, Register payload,
                               Register scratch) {
#ifdef DEBUG
  if (type == JSVAL_TYPE_INT32) {
    // Ensure the payload is a properly sign-extended int32.
    Label signExtended;
    masm.as_slli_w(scratch, payload, 0);
    masm.ma_b(payload, scratch, &signExtended, Assembler::Equal, ShortJump);
    masm.breakpoint();
    masm.bind(&signExtended);
  } else {
    // All bits above the payload must be zeroed.
    Label zeroed;
    masm.as_srli_d(scratch, payload, PayloadSize(type));
    masm.ma_b(scratch, scratch, &zeroed, Assembler::Zero, ShortJump);
    masm.breakpoint();
    masm.bind(&zeroed);
  }
#endif
}

void MacroAssemblerLOONG64Compat::boxValue(JSValueType type, Register src,
                                           Register dest) {
  MOZ_ASSERT(src != dest);
  MOZ_ASSERT(type != JSVAL_TYPE_UNDEFINED && type != JSVAL_TYPE_NULL);

  AssertValidPayload(*this, type, src, dest);

  int64_t shifted = int64_t(JSVAL_TYPE_TO_SHIFTED_TAG(type)) >> 32;
  MOZ_ASSERT(is_intN(shifted, 20),
             "upper 32 bits of shifted tag fit into lu32i.d");

  // Store shifted tag into upper 32 bits. The lower 32 bits are left unchanged.
  as_lu32i_d(dest, shifted);

  switch (type) {
    case JSVAL_TYPE_BOOLEAN:
    case JSVAL_TYPE_INT32:
    case JSVAL_TYPE_MAGIC: {
      // Insert payload into lower 32 bits.
      as_bstrins_d(dest, src, 31, 0);
      return;
    }
    case JSVAL_TYPE_STRING:
    case JSVAL_TYPE_SYMBOL:
    case JSVAL_TYPE_PRIVATE_GCTHING:
    case JSVAL_TYPE_BIGINT:
    case JSVAL_TYPE_OBJECT: {
      // Insert payload into lower 47 bits.
      as_bstrins_d(dest, src, JSVAL_TAG_SHIFT - 1, 0);
      return;
    }
    case JSVAL_TYPE_DOUBLE:
    case JSVAL_TYPE_UNDEFINED:
    case JSVAL_TYPE_NULL:
    case JSVAL_TYPE_UNKNOWN:
      break;
  }
  MOZ_CRASH("bad value type");
}

void MacroAssemblerLOONG64Compat::boxValue(Register type, Register src,
                                           Register dest) {
  MOZ_ASSERT(src != dest);

#ifdef DEBUG
  Label done, isNullOrUndefined, isBoolean, isInt32OrMagic, isPointerSized;

  asMasm().branch32(Assembler::Equal, type, Imm32(JSVAL_TYPE_NULL),
                    &isNullOrUndefined);
  asMasm().branch32(Assembler::Equal, type, Imm32(JSVAL_TYPE_UNDEFINED),
                    &isNullOrUndefined);
  asMasm().branch32(Assembler::Equal, type, Imm32(JSVAL_TYPE_BOOLEAN),
                    &isBoolean);
  asMasm().branch32(Assembler::Equal, type, Imm32(JSVAL_TYPE_INT32),
                    &isInt32OrMagic);
  asMasm().branch32(Assembler::Equal, type, Imm32(JSVAL_TYPE_MAGIC),
                    &isInt32OrMagic);
  // GCThing types aren't currently supported, because as_bstrins_d truncates
  // payloads above UINT32_MAX.
  breakpoint();
  {
    bind(&isNullOrUndefined);

    // Ensure no payload for null and undefined.
    ma_b(src, src, &done, Assembler::Zero, ShortJump);
    breakpoint();
  }
  {
    bind(&isBoolean);

    // Ensure boolean values are either 0 or 1.
    ma_b(src, Imm32(1), &done, Assembler::BelowOrEqual, ShortJump);
    breakpoint();
  }
  {
    bind(&isInt32OrMagic);

    // Ensure |src| is sign-extended.
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    as_slli_w(scratch, src, 0);
    ma_b(src, scratch, &done, Assembler::Equal, ShortJump);
    breakpoint();
  }
  bind(&done);
#endif

  ma_or(dest, type, Imm32(JSVAL_TAG_MAX_DOUBLE));
  as_slli_d(dest, dest, JSVAL_TAG_SHIFT);
  as_bstrins_d(dest, src, 31, 0);
}

void MacroAssemblerLOONG64Compat::loadConstantFloat32(float f,
                                                      FloatRegister dest) {
  ma_lis(dest, f);
}

void MacroAssemblerLOONG64Compat::loadInt32OrDouble(const Address& src,
                                                    FloatRegister dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  Label end;

  // If it's an int, convert it to double.
  loadPtr(Address(src.base, src.offset), scratch);
  as_movgr2fr_d(dest, scratch);
  as_srli_d(scratch, scratch, JSVAL_TAG_SHIFT);
  asMasm().branchTestInt32(Assembler::NotEqual, scratch, &end);
  as_ffint_d_w(dest, dest);

  bind(&end);
}

void MacroAssemblerLOONG64Compat::loadInt32OrDouble(const BaseIndex& addr,
                                                    FloatRegister dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();

  computeScaledAddress(addr, scratch);
  loadInt32OrDouble(Address(scratch, addr.offset), dest);
}

void MacroAssemblerLOONG64Compat::loadConstantDouble(double dp,
                                                     FloatRegister dest) {
  ma_lid(dest, dp);
}

Register MacroAssemblerLOONG64Compat::extractObject(const Address& address,
                                                    Register scratch) {
  loadPtr(address, scratch);
  as_bstrpick_d(scratch, scratch, JSVAL_TAG_SHIFT - 1, 0);
  return scratch;
}

Register MacroAssemblerLOONG64Compat::extractTag(const Address& address,
                                                 Register scratch) {
  loadPtr(address, scratch);
  as_bstrpick_d(scratch, scratch, 63, JSVAL_TAG_SHIFT);
  return scratch;
}

Register MacroAssemblerLOONG64Compat::extractTag(const BaseIndex& address,
                                                 Register scratch) {
  computeScaledAddress(address, scratch);
  return extractTag(Address(scratch, address.offset), scratch);
}

/////////////////////////////////////////////////////////////////
// X86/X64-common/ARM/LoongArch interface.
/////////////////////////////////////////////////////////////////
void MacroAssemblerLOONG64Compat::storeValue(ValueOperand val,
                                             const Address& dest) {
  storePtr(val.valueReg(), Address(dest.base, dest.offset));
}

void MacroAssemblerLOONG64Compat::storeValue(ValueOperand val,
                                             const BaseIndex& dest) {
  storePtr(val.valueReg(), dest);
}

void MacroAssemblerLOONG64Compat::storeValue(JSValueType type, Register reg,
                                             Address dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(dest.base != scratch);

  boxValue(type, reg, scratch);
  storePtr(scratch, dest);
}

void MacroAssemblerLOONG64Compat::storeValue(JSValueType type, Register reg,
                                             BaseIndex dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(dest.base != scratch);

  boxValue(type, reg, scratch);
  storePtr(scratch, dest);
}

void MacroAssemblerLOONG64Compat::storeValue(const Value& val, Address dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(dest.base != scratch);

  if (val.isGCThing()) {
    writeDataRelocation(val);
    movWithPatch(ImmWord(val.asRawBits()), scratch);
  } else {
    ma_li(scratch, ImmWord(val.asRawBits()));
  }
  storePtr(scratch, dest);
}

void MacroAssemblerLOONG64Compat::storeValue(const Value& val, BaseIndex dest) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  MOZ_ASSERT(dest.base != scratch);

  if (val.isGCThing()) {
    writeDataRelocation(val);
    movWithPatch(ImmWord(val.asRawBits()), scratch);
  } else {
    ma_li(scratch, ImmWord(val.asRawBits()));
  }
  storePtr(scratch, dest);
}

void MacroAssemblerLOONG64Compat::loadValue(Address src, ValueOperand val) {
  loadPtr(src, val.valueReg());
}

void MacroAssemblerLOONG64Compat::loadValue(const BaseIndex& src,
                                            ValueOperand val) {
  loadPtr(src, val.valueReg());
}

void MacroAssemblerLOONG64Compat::tagValue(JSValueType type, Register payload,
                                           ValueOperand dest) {
  MOZ_ASSERT(type != JSVAL_TYPE_UNDEFINED && type != JSVAL_TYPE_NULL);

  if (payload == dest.valueReg()) {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(dest.valueReg() != scratch);

    AssertValidPayload(*this, type, payload, scratch);

    switch (type) {
      case JSVAL_TYPE_BOOLEAN:
      case JSVAL_TYPE_INT32:
      case JSVAL_TYPE_MAGIC: {
        int64_t shifted = int64_t(JSVAL_TYPE_TO_SHIFTED_TAG(type)) >> 32;
        MOZ_ASSERT(is_intN(shifted, 20),
                   "upper 32 bit of shifted tag fit into lu32i.d");

        // Store shifted tag into upper 32 bits. The lower 32 bits, containing
        // the payload, are left unchanged.
        as_lu32i_d(dest.valueReg(), shifted);
        return;
      }
      case JSVAL_TYPE_STRING:
      case JSVAL_TYPE_SYMBOL:
      case JSVAL_TYPE_PRIVATE_GCTHING:
      case JSVAL_TYPE_BIGINT:
      case JSVAL_TYPE_OBJECT: {
        int64_t signExtendedShiftedTag =
            int64_t(JSVAL_TYPE_TO_SHIFTED_TAG(type)) >> JSVAL_TAG_SHIFT;
        MOZ_ASSERT(is_intN(signExtendedShiftedTag, 12),
                   "sign-extended shifted tag can be materialised in a single "
                   "addi.w instruction");

        // Store sign-extended tag into lower 17 bits of the scratch register.
        as_addi_w(scratch, zero, signExtendedShiftedTag);

        // Insert tag into the result.
        as_bstrins_d(dest.valueReg(), scratch, 63, JSVAL_TAG_SHIFT);
        return;
      }
      case JSVAL_TYPE_DOUBLE:
      case JSVAL_TYPE_UNDEFINED:
      case JSVAL_TYPE_NULL:
      case JSVAL_TYPE_UNKNOWN:
        break;
    }
    MOZ_CRASH("bad value type");
  } else {
    boxNonDouble(type, payload, dest);
  }
}

void MacroAssemblerLOONG64Compat::pushValue(ValueOperand val) {
  push(val.valueReg());
}

void MacroAssemblerLOONG64Compat::pushValue(const Address& addr) { push(addr); }

void MacroAssemblerLOONG64Compat::popValue(ValueOperand val) {
  pop(val.valueReg());
}

void MacroAssemblerLOONG64Compat::breakpoint(uint32_t value) {
  as_break(value);
}

void MacroAssemblerLOONG64Compat::crash() { as_break(0); }

void MacroAssemblerLOONG64Compat::handleFailureWithHandlerTail(
    Label* profilerExitTail, Label* bailoutTail,
    uint32_t* returnValueCheckOffset) {
  // Reserve space for exception information.
  int size = (sizeof(ResumeFromException) + ABIStackAlignment) &
             ~(ABIStackAlignment - 1);
  asMasm().subPtr(Imm32(size), StackPointer);
  mov(StackPointer, a0);  // Use a0 since it is a first function argument

  // Call the handler.
  using Fn = void (*)(ResumeFromException* rfe);
  asMasm().setupUnalignedABICall(a1);
  asMasm().passABIArg(a0);
  asMasm().callWithABI<Fn, HandleException>(
      ABIType::General, CheckUnsafeCallWithABI::DontCheckHasExitFrame);

  *returnValueCheckOffset = asMasm().currentOffset();

  Label entryFrame;
  Label catch_;
  Label finally;
  Label returnBaseline;
  Label returnIon;
  Label bailout;
  Label wasmInterpEntry;
  Label wasmCatch;

  // Already clobbered a0, so use it...
  load32(Address(StackPointer, ResumeFromException::offsetOfKind()), a0);
  asMasm().branch32(Assembler::Equal, a0,
                    Imm32(ExceptionResumeKind::EntryFrame), &entryFrame);
  asMasm().branch32(Assembler::Equal, a0, Imm32(ExceptionResumeKind::Catch),
                    &catch_);
  asMasm().branch32(Assembler::Equal, a0, Imm32(ExceptionResumeKind::Finally),
                    &finally);
  asMasm().branch32(Assembler::Equal, a0,
                    Imm32(ExceptionResumeKind::ForcedReturnBaseline),
                    &returnBaseline);
  asMasm().branch32(Assembler::Equal, a0,
                    Imm32(ExceptionResumeKind::ForcedReturnIon), &returnIon);
  asMasm().branch32(Assembler::Equal, a0, Imm32(ExceptionResumeKind::Bailout),
                    &bailout);
  asMasm().branch32(Assembler::Equal, a0,
                    Imm32(ExceptionResumeKind::WasmInterpEntry),
                    &wasmInterpEntry);
  asMasm().branch32(Assembler::Equal, a0, Imm32(ExceptionResumeKind::WasmCatch),
                    &wasmCatch);

  crash();  // Invalid kind.

  // No exception handler. Load the error value, restore state and return from
  // the entry frame.
  bind(&entryFrame);
  asMasm().moveValue(MagicValue(JS_ION_ERROR), JSReturnOperand);
  loadPtr(Address(StackPointer, ResumeFromException::offsetOfFramePointer()),
          FramePointer);
  loadPtr(Address(StackPointer, ResumeFromException::offsetOfStackPointer()),
          StackPointer);

  // We're going to be returning by the ion calling convention
  ma_pop(ra);
  as_jirl(zero, ra, BOffImm16(0));

  // If we found a catch handler, this must be a baseline frame. Restore
  // state and jump to the catch block.
  bind(&catch_);
  loadPtr(Address(StackPointer, ResumeFromException::offsetOfTarget()), a0);
  loadPtr(Address(StackPointer, ResumeFromException::offsetOfFramePointer()),
          FramePointer);
  loadPtr(Address(StackPointer, ResumeFromException::offsetOfStackPointer()),
          StackPointer);
  jump(a0);

  // If we found a finally block, this must be a baseline frame. Push three
  // values expected by the finally block: the exception, the exception stack,
  // and BooleanValue(true).
  bind(&finally);
  ValueOperand exception = ValueOperand(a1);
  loadValue(Address(sp, ResumeFromException::offsetOfException()), exception);

  ValueOperand exceptionStack = ValueOperand(a2);
  loadValue(Address(sp, ResumeFromException::offsetOfExceptionStack()),
            exceptionStack);

  loadPtr(Address(sp, ResumeFromException::offsetOfTarget()), a0);
  loadPtr(Address(sp, ResumeFromException::offsetOfFramePointer()),
          FramePointer);
  loadPtr(Address(sp, ResumeFromException::offsetOfStackPointer()), sp);

  pushValue(exception);
  pushValue(exceptionStack);
  pushValue(BooleanValue(true));
  jump(a0);

  // Return BaselineFrame->returnValue() to the caller.
  // Used in debug mode and for GeneratorReturn.
  Label profilingInstrumentation;
  bind(&returnBaseline);
  loadPtr(Address(StackPointer, ResumeFromException::offsetOfFramePointer()),
          FramePointer);
  loadPtr(Address(StackPointer, ResumeFromException::offsetOfStackPointer()),
          StackPointer);
  loadValue(Address(FramePointer, BaselineFrame::reverseOffsetOfReturnValue()),
            JSReturnOperand);
  jump(&profilingInstrumentation);

  // Return the given value to the caller.
  bind(&returnIon);
  loadValue(Address(StackPointer, ResumeFromException::offsetOfException()),
            JSReturnOperand);
  loadPtr(Address(StackPointer, ResumeFromException::offsetOfFramePointer()),
          FramePointer);
  loadPtr(Address(StackPointer, ResumeFromException::offsetOfStackPointer()),
          StackPointer);

  // If profiling is enabled, then update the lastProfilingFrame to refer to
  // caller frame before returning. This code is shared by ForcedReturnIon
  // and ForcedReturnBaseline.
  bind(&profilingInstrumentation);
  {
    Label skipProfilingInstrumentation;
    // Test if profiler enabled.
    AbsoluteAddress addressOfEnabled(
        asMasm().runtime()->geckoProfiler().addressOfEnabled());
    asMasm().branch32(Assembler::Equal, addressOfEnabled, Imm32(0),
                      &skipProfilingInstrumentation);
    jump(profilerExitTail);
    bind(&skipProfilingInstrumentation);
  }

  as_or(StackPointer, FramePointer, zero);
  pop(FramePointer);
  ret();

  // If we are bailing out to baseline to handle an exception, jump to
  // the bailout tail stub. Load 1 (true) in ReturnReg to indicate success.
  bind(&bailout);
  loadPtr(Address(sp, ResumeFromException::offsetOfBailoutInfo()), a2);
  loadPtr(Address(StackPointer, ResumeFromException::offsetOfStackPointer()),
          StackPointer);
  ma_li(ReturnReg, Imm32(1));
  jump(bailoutTail);

  // Reset SP and FP; SP is pointing to the unwound return address to the wasm
  // interpreter entry, so we can just ret().
  bind(&wasmInterpEntry);
  loadPtr(Address(StackPointer, ResumeFromException::offsetOfFramePointer()),
          FramePointer);
  loadPtr(Address(StackPointer, ResumeFromException::offsetOfStackPointer()),
          StackPointer);
  ma_li(InstanceReg, ImmWord(wasm::InterpFailInstanceReg));
  ret();

  // Found a wasm catch handler, restore state and jump to it.
  bind(&wasmCatch);
  wasm::GenerateJumpToCatchHandler(asMasm(), sp, a1, a2, a3);
}

CodeOffset MacroAssemblerLOONG64Compat::toggledJump(Label* label) {
  CodeOffset ret(nextOffset().getOffset());
  ma_b(label);
  return ret;
}

CodeOffset MacroAssemblerLOONG64Compat::toggledCall(JitCode* target,
                                                    bool enabled) {
  UseScratchRegisterScope temps(asMasm());
  Register scratch = temps.Acquire();
  BufferOffset bo = nextOffset();
  CodeOffset offset(bo.getOffset());  // first instruction location,not changed.
  addPendingJump(bo, ImmPtr(target->raw()), RelocationKind::JITCODE);
  ma_liPatchable(scratch, ImmPtr(target->raw()));
  if (enabled) {
    as_jirl(ra, scratch, BOffImm16(0));
  } else {
    as_nop();
  }
  MOZ_ASSERT_IF(!oom(), nextOffset().getOffset() - offset.offset() ==
                            ToggledCallSize(nullptr));
  return offset;  // location of first instruction of call instr sequence.
}

void MacroAssembler::shiftIndex32AndAdd(Register indexTemp32, int shift,
                                        Register pointer) {
  if (IsShiftInScaleRange(shift)) {
    computeEffectiveAddress(
        BaseIndex(pointer, indexTemp32, ShiftToScale(shift)), pointer);
    return;
  }
  lshift32(Imm32(shift), indexTemp32);
  addPtr(indexTemp32, pointer);
}

void MacroAssembler::wasmMarkCallAsSlow() { mov(ra, ra); }

const int32_t SlowCallMarker = 0x03800021;  // ori ra, ra, 0

void MacroAssembler::wasmCheckSlowCallsite(Register ra_, Label* notSlow,
                                           Register temp1, Register temp2) {
  MOZ_ASSERT(ra_ != temp2);
  load32(Address(ra_, 0), temp2);
  branch32(Assembler::NotEqual, temp2, Imm32(SlowCallMarker), notSlow);
}

CodeOffset MacroAssembler::wasmMarkedSlowCall(const wasm::CallSiteDesc& desc,
                                              const Register reg) {
  CodeOffset offset = call(desc, reg);
  wasmMarkCallAsSlow();
  return offset;
}

//}}} check_macroassembler_style

}  // namespace jit
}  // namespace js
