/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/mips-shared/MacroAssembler-mips-shared.h"

#include <bit>

#include "jit/MacroAssembler.h"
#include "util/PortableMath.h"

using namespace js;
using namespace jit;

void MacroAssemblerMIPSShared::ma_move(Register rd, Register rs) {
  as_or(rd, rs, zero);
}

void MacroAssemblerMIPSShared::ma_li(Register dest, ImmGCPtr ptr) {
  writeDataRelocation(ptr);
  asMasm().ma_liPatchable(dest, ImmPtr(ptr.value));
}

void MacroAssemblerMIPSShared::ma_li(Register dest, Imm32 imm) {
  if (Imm16::IsInSignedRange(imm.value)) {
    as_addiu(dest, zero, imm.value);
  } else if (Imm16::IsInUnsignedRange(imm.value)) {
    as_ori(dest, zero, Imm16::Lower(imm).encode());
  } else if (Imm16::Lower(imm).encode() == 0) {
    as_lui(dest, Imm16::Upper(imm).encode());
  } else {
    as_lui(dest, Imm16::Upper(imm).encode());
    as_ori(dest, dest, Imm16::Lower(imm).encode());
  }
}

// This method generates lui and ori instruction pair that can be modified by
// UpdateLuiOriValue, either during compilation (eg. Assembler::bind), or
// during execution (eg. jit::PatchJump).
void MacroAssemblerMIPSShared::ma_liPatchable(Register dest, Imm32 imm) {
  m_buffer.ensureSpace(2 * sizeof(uint32_t));
  as_lui(dest, Imm16::Upper(imm).encode());
  as_ori(dest, dest, Imm16::Lower(imm).encode());
}

// Shifts
void MacroAssemblerMIPSShared::ma_sll(Register rd, Register rt, Imm32 shift) {
  as_sll(rd, rt, shift.value & 0x1f);
}
void MacroAssemblerMIPSShared::ma_srl(Register rd, Register rt, Imm32 shift) {
  as_srl(rd, rt, shift.value & 0x1f);
}

void MacroAssemblerMIPSShared::ma_sra(Register rd, Register rt, Imm32 shift) {
  as_sra(rd, rt, shift.value & 0x1f);
}

void MacroAssemblerMIPSShared::ma_ror(Register rd, Register rt, Imm32 shift) {
  if (hasR2()) {
    as_rotr(rd, rt, shift.value & 0x1f);
  } else {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    as_srl(scratch, rt, shift.value & 0x1f);
    as_sll(rd, rt, 32 - (shift.value & 0x1f));
    as_or(rd, rd, scratch);
  }
}

void MacroAssemblerMIPSShared::ma_rol(Register rd, Register rt, Imm32 shift) {
  if (hasR2()) {
    as_rotr(rd, rt, 32 - (shift.value & 0x1f));
  } else {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    as_srl(scratch, rt, 32 - (shift.value & 0x1f));
    as_sll(rd, rt, shift.value & 0x1f);
    as_or(rd, rd, scratch);
  }
}

void MacroAssemblerMIPSShared::ma_sll(Register rd, Register rt,
                                      Register shift) {
  as_sllv(rd, rt, shift);
}

void MacroAssemblerMIPSShared::ma_srl(Register rd, Register rt,
                                      Register shift) {
  as_srlv(rd, rt, shift);
}

void MacroAssemblerMIPSShared::ma_sra(Register rd, Register rt,
                                      Register shift) {
  as_srav(rd, rt, shift);
}

void MacroAssemblerMIPSShared::ma_ror(Register rd, Register rt,
                                      Register shift) {
  if (hasR2()) {
    as_rotrv(rd, rt, shift);
  } else {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    ma_negu(scratch, shift);
    as_sllv(scratch, rt, scratch);
    as_srlv(rd, rt, shift);
    as_or(rd, rd, scratch);
  }
}

void MacroAssemblerMIPSShared::ma_rol(Register rd, Register rt,
                                      Register shift) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  ma_negu(scratch, shift);
  if (hasR2()) {
    as_rotrv(rd, rt, scratch);
  } else {
    as_srlv(scratch, rt, scratch);
    as_sllv(rd, rt, shift);
    as_or(rd, rd, scratch);
  }
}

void MacroAssemblerMIPSShared::ma_negu(Register rd, Register rs) {
  as_subu(rd, zero, rs);
}

void MacroAssemblerMIPSShared::ma_not(Register rd, Register rs) {
  as_nor(rd, rs, zero);
}

// Bit extract/insert
void MacroAssemblerMIPSShared::ma_ext(Register rt, Register rs, uint16_t pos,
                                      uint16_t size) {
  MOZ_ASSERT(pos < 32);
  MOZ_ASSERT(pos + size < 33);

  if (hasR2()) {
    as_ext(rt, rs, pos, size);
  } else {
    int shift_left = 32 - (pos + size);
    as_sll(rt, rs, shift_left);
    int shift_right = 32 - size;
    if (shift_right > 0) {
      as_srl(rt, rt, shift_right);
    }
  }
}

void MacroAssemblerMIPSShared::ma_ins(Register rt, Register rs, uint16_t pos,
                                      uint16_t size) {
  MOZ_ASSERT(pos < 32);
  MOZ_ASSERT(pos + size <= 32);
  MOZ_ASSERT(size != 0);

  if (hasR2()) {
    as_ins(rt, rs, pos, size);
  } else {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    if (pos == 0) {
      ma_ext(scratch, rs, 0, size);
      as_srl(rt, rt, size);
      as_sll(rt, rt, size);
      as_or(rt, rt, scratch);
    } else if (pos + size == 32) {
      as_sll(scratch, rs, pos);
      as_sll(rt, rt, size);
      as_srl(rt, rt, size);
      as_or(rt, rt, scratch);
    } else {
      Register scratch2 = temps.Acquire();
      ma_subu(scratch, zero, Imm32(1));
      as_srl(scratch, scratch, 32 - size);
      as_and(scratch2, rs, scratch);
      as_sll(scratch2, scratch2, pos);
      as_sll(scratch, scratch, pos);
      as_nor(scratch, scratch, zero);
      as_and(scratch, rt, scratch);
      as_or(rt, scratch2, scratch);
    }
  }
}

// Sign extend
void MacroAssemblerMIPSShared::ma_seb(Register rd, Register rt) {
  if (hasR2()) {
    as_seb(rd, rt);
  } else {
    as_sll(rd, rt, 24);
    as_sra(rd, rd, 24);
  }
}

void MacroAssemblerMIPSShared::ma_seh(Register rd, Register rt) {
  if (hasR2()) {
    as_seh(rd, rt);
  } else {
    as_sll(rd, rt, 16);
    as_sra(rd, rd, 16);
  }
}

// And.
void MacroAssemblerMIPSShared::ma_and(Register rd, Register rs) {
  as_and(rd, rd, rs);
}

void MacroAssemblerMIPSShared::ma_and(Register rd, Imm32 imm) {
  ma_and(rd, rd, imm);
}

void MacroAssemblerMIPSShared::ma_and(Register rd, Register rs, Imm32 imm) {
  if (Imm16::IsInUnsignedRange(imm.value)) {
    as_andi(rd, rs, imm.value);
  } else {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    as_and(rd, rs, scratch);
  }
}

// Or.
void MacroAssemblerMIPSShared::ma_or(Register rd, Register rs) {
  as_or(rd, rd, rs);
}

void MacroAssemblerMIPSShared::ma_or(Register rd, Imm32 imm) {
  ma_or(rd, rd, imm);
}

void MacroAssemblerMIPSShared::ma_or(Register rd, Register rs, Imm32 imm) {
  if (Imm16::IsInUnsignedRange(imm.value)) {
    as_ori(rd, rs, imm.value);
  } else {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    as_or(rd, rs, scratch);
  }
}

// xor
void MacroAssemblerMIPSShared::ma_xor(Register rd, Register rs) {
  as_xor(rd, rd, rs);
}

void MacroAssemblerMIPSShared::ma_xor(Register rd, Imm32 imm) {
  ma_xor(rd, rd, imm);
}

void MacroAssemblerMIPSShared::ma_xor(Register rd, Register rs, Imm32 imm) {
  if (Imm16::IsInUnsignedRange(imm.value)) {
    as_xori(rd, rs, imm.value);
  } else {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    as_xor(rd, rs, scratch);
  }
}

// word swap bytes within halfwords
void MacroAssemblerMIPSShared::ma_wsbh(Register rd, Register rt) {
  as_wsbh(rd, rt);
}

void MacroAssemblerMIPSShared::ma_ctz(Register rd, Register rs) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  as_addiu(scratch, rs, -1);
  as_xor(rd, scratch, rs);
  as_and(rd, rd, scratch);
  as_clz(rd, rd);
  ma_li(scratch, Imm32(0x20));
  as_subu(rd, scratch, rd);
}

// Arithmetic-based ops.

// Add.
void MacroAssemblerMIPSShared::ma_addu(Register rd, Register rs, Imm32 imm) {
  if (Imm16::IsInSignedRange(imm.value)) {
    as_addiu(rd, rs, imm.value);
  } else {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    as_addu(rd, rs, scratch);
  }
}

void MacroAssemblerMIPSShared::ma_addu(Register rd, Register rs) {
  as_addu(rd, rd, rs);
}

void MacroAssemblerMIPSShared::ma_addu(Register rd, Imm32 imm) {
  ma_addu(rd, rd, imm);
}

void MacroAssemblerMIPSShared::ma_add32TestCarry(Condition cond, Register rd,
                                                 Register rs, Register rt,
                                                 Label* overflow) {
  MOZ_ASSERT(cond == Assembler::CarrySet || cond == Assembler::CarryClear);
  MOZ_ASSERT_IF(rd == rs, rt != rd);
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  as_addu(rd, rs, rt);
  as_sltu(scratch2, rd, rd == rs ? rt : rs);
  ma_b(scratch2, scratch2, overflow,
       cond == Assembler::CarrySet ? Assembler::NonZero : Assembler::Zero);
}

void MacroAssemblerMIPSShared::ma_add32TestCarry(Condition cond, Register rd,
                                                 Register rs, Imm32 imm,
                                                 Label* overflow) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  ma_li(scratch, imm);
  ma_add32TestCarry(cond, rd, rs, scratch, overflow);
}

// Subtract.
void MacroAssemblerMIPSShared::ma_subu(Register rd, Register rs, Imm32 imm) {
  if (Imm16::IsInSignedRange(-imm.value)) {
    as_addiu(rd, rs, -imm.value);
  } else {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    as_subu(rd, rs, scratch);
  }
}

void MacroAssemblerMIPSShared::ma_subu(Register rd, Imm32 imm) {
  ma_subu(rd, rd, imm);
}

void MacroAssemblerMIPSShared::ma_subu(Register rd, Register rs) {
  as_subu(rd, rd, rs);
}

void MacroAssemblerMIPSShared::ma_sub32TestOverflow(Register rd, Register rs,
                                                    Imm32 imm,
                                                    Label* overflow) {
  if (imm.value != INT32_MIN) {
    asMasm().ma_add32TestOverflow(rd, rs, Imm32(-imm.value), overflow);
  } else {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    ma_li(scratch, Imm32(imm.value));
    asMasm().ma_sub32TestOverflow(rd, rs, scratch, overflow);
  }
}

void MacroAssemblerMIPSShared::ma_mul(Register rd, Register rs, Imm32 imm) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  ma_li(scratch, imm);
  as_mul(rd, rs, scratch);
}

void MacroAssemblerMIPSShared::ma_mul32TestOverflow(Register rd, Register rs,
                                                    Register rt,
                                                    Label* overflow) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();

#ifdef MIPSR6
  as_dmul(rd, rs, rt);
#else
  as_dmult(rs, rt);
  as_mflo(rd);
#endif
  ma_sll(scratch, rd, Imm32(0));
  ma_b(rd, scratch, overflow, Assembler::NotEqual);
}

void MacroAssemblerMIPSShared::ma_mul32TestOverflow(Register rd, Register rs,
                                                    Imm32 imm,
                                                    Label* overflow) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();

  ma_li(scratch, imm);
#ifdef MIPSR6
  as_dmul(rd, rs, scratch);
#else
  as_dmult(rs, scratch);
  as_mflo(rd);
#endif
  ma_sll(scratch, rd, Imm32(0));
  ma_b(rd, scratch, overflow, Assembler::NotEqual);
}

void MacroAssemblerMIPSShared::ma_mod_mask(Register src, Register dest,
                                           Register hold, Register remain,
                                           int32_t shift, Label* negZero) {
  UseScratchRegisterScope temps(*this);

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
  // scratch2 serves as a temporary location to store extracted bits
  // into as well as holding the trial subtraction as a temp value dest is
  // the accumulator (and holds the final result)

  // move the whole value into the remain.
  ma_move(remain, src);
  // Zero out the dest.
  ma_li(dest, Imm32(0));
  // Set the hold appropriately.
  ma_b(remain, remain, &negative, Signed, ShortJump);
  ma_li(hold, Imm32(1));
  ma_b(&head, ShortJump);

  bind(&negative);
  ma_li(hold, Imm32(-1));
  ma_negu(remain, remain);

  // Begin the main loop.
  bind(&head);

  Register scratch2 = temps.Acquire();
  // Extract the bottom bits into scratch2.
  ma_and(scratch2, remain, Imm32(mask));
  // Add those bits to the accumulator.
  as_addu(dest, dest, scratch2);
  // Do a trial subtraction
  ma_subu(scratch2, dest, Imm32(mask));
  // If (sum - C) > 0, store sum - C back into sum, thus performing a
  // modulus.
  ma_b(scratch2, scratch2, &sumSigned, Signed, ShortJump);
  ma_move(dest, scratch2);
  bind(&sumSigned);
  // Get rid of the bits that we extracted before.
  as_srl(remain, remain, shift);
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
  ma_negu(dest, dest);

  bind(&done);
}

// Memory.

FaultingCodeRange MacroAssemblerMIPSShared::ma_load(
    Register dest, const BaseIndex& src, LoadStoreSize size,
    LoadStoreExtension extension) {
  UseScratchRegisterScope temps(*this);
  FaultingCodeRange fcr;
  if (isLoongson() && ZeroExtend != extension &&
      Imm8::IsInSignedRange(src.offset)) {
    UseScratchRegisterScope temps(*this);
    Register index = src.index;

    if (src.scale != TimesOne) {
      int32_t shift = Imm32::ShiftOf(src.scale).value;

      Register scratch2 = temps.Acquire();
      MOZ_ASSERT(scratch2 != src.base);
      index = scratch2;
      asMasm().ma_dsll(index, src.index, Imm32(shift));
    }

    fcr = FaultingCodeRange(currentOffset());
    switch (size) {
      case SizeByte:
        as_gslbx(dest, src.base, index, src.offset);
        break;
      case SizeHalfWord:
        as_gslhx(dest, src.base, index, src.offset);
        break;
      case SizeWord:
        as_gslwx(dest, src.base, index, src.offset);
        break;
      case SizeDouble:
        as_gsldx(dest, src.base, index, src.offset);
        break;
      default:
        MOZ_CRASH("Invalid argument for ma_load");
    }
    return fcr;
  }

  // dest will be overwritten anyway
  asMasm().computeEffectiveAddress(src, dest);
  return asMasm().ma_load(dest, Address(dest, 0), size, extension);
}

void MacroAssemblerMIPSShared::ma_load_unaligned(Register dest,
                                                 const BaseIndex& src,
                                                 LoadStoreSize size,
                                                 LoadStoreExtension extension) {
  int16_t lowOffset, hiOffset;
  UseScratchRegisterScope temps(*this);
  Register base = temps.Acquire();
  asMasm().computeScaledAddress(src, base);
  Register scratch = temps.Acquire();

  if (Imm16::IsInSignedRange(src.offset) &&
      Imm16::IsInSignedRange(src.offset + size / 8 - 1)) {
    lowOffset = Imm16(src.offset).encode();
    hiOffset = Imm16(src.offset + size / 8 - 1).encode();
  } else {
    ma_li(scratch, Imm32(src.offset));
    asMasm().addPtr(scratch, base);
    lowOffset = Imm16(0).encode();
    hiOffset = Imm16(size / 8 - 1).encode();
  }

  switch (size) {
    case SizeHalfWord:
      MOZ_ASSERT(dest != scratch);
      if (extension == ZeroExtend) {
        as_lbu(scratch, base, hiOffset);
      } else {
        as_lb(scratch, base, hiOffset);
      }
      as_lbu(dest, base, lowOffset);
      if (hasR2()) {
        as_ins(dest, scratch, 8, 24);
      } else {
        as_sll(scratch, scratch, 8);
        as_or(dest, dest, scratch);
      }
      break;
    case SizeWord:
      MOZ_ASSERT(dest != base);
      as_lwl(dest, base, hiOffset);
      as_lwr(dest, base, lowOffset);
      if (extension == ZeroExtend) {
        asMasm().ma_dext(dest, dest, Imm32(0), Imm32(32));
      }
      break;
    case SizeDouble:
      MOZ_ASSERT(dest != base);
      as_ldl(dest, base, hiOffset);
      as_ldr(dest, base, lowOffset);
      break;
    default:
      MOZ_CRASH("Invalid argument for ma_load_unaligned");
  }
}

void MacroAssemblerMIPSShared::ma_load_unaligned(Register dest,
                                                 const Address& address,
                                                 LoadStoreSize size,
                                                 LoadStoreExtension extension) {
  int16_t lowOffset, hiOffset;
  UseScratchRegisterScope temps(*this);
  Register scratch1 = temps.Acquire();
  Register scratch2 = temps.Acquire();
  Register base;

  if (Imm16::IsInSignedRange(address.offset) &&
      Imm16::IsInSignedRange(address.offset + size / 8 - 1)) {
    base = address.base;
    lowOffset = Imm16(address.offset).encode();
    hiOffset = Imm16(address.offset + size / 8 - 1).encode();
  } else {
    ma_li(scratch1, Imm32(address.offset));
    asMasm().addPtr(address.base, scratch1);
    base = scratch1;
    lowOffset = Imm16(0).encode();
    hiOffset = Imm16(size / 8 - 1).encode();
  }

  switch (size) {
    case SizeHalfWord:
      MOZ_ASSERT(base != scratch2 && dest != scratch2);
      if (extension == ZeroExtend) {
        as_lbu(scratch2, base, hiOffset);
      } else {
        as_lb(scratch2, base, hiOffset);
      }
      as_lbu(dest, base, lowOffset);
      if (hasR2()) {
        as_ins(dest, scratch2, 8, 24);
      } else {
        as_sll(scratch2, scratch2, 8);
        as_or(dest, dest, scratch2);
      }
      break;
    case SizeWord:
      MOZ_ASSERT(dest != base);
      as_lwl(dest, base, hiOffset);
      as_lwr(dest, base, lowOffset);
      if (extension == ZeroExtend) {
        as_dext(dest, dest, 0, 32);
      }
      break;
    case SizeDouble:
      MOZ_ASSERT(dest != base);
      as_ldl(dest, base, hiOffset);
      as_ldr(dest, base, lowOffset);
      break;
    default:
      MOZ_CRASH("Invalid argument for ma_load_unaligned");
  }
}

FaultingCodeRange MacroAssemblerMIPSShared::ma_load_unaligned(
    const wasm::MemoryAccessDesc& access, Register dest, const BaseIndex& src,
    Register temp, LoadStoreSize size, LoadStoreExtension extension) {
  MOZ_ASSERT(std::endian::native == std::endian::little,
             "Wasm-only; wasm is disabled on big-endian.");
  int16_t lowOffset, hiOffset;
  Register base;

  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  asMasm().computeScaledAddress(src, scratch2);

  if (Imm16::IsInSignedRange(src.offset) &&
      Imm16::IsInSignedRange(src.offset + size / 8 - 1)) {
    base = scratch2;
    lowOffset = Imm16(src.offset).encode();
    hiOffset = Imm16(src.offset + size / 8 - 1).encode();
  } else {
    Register scratch = temps.Acquire();
    ma_li(scratch, Imm32(src.offset));
    asMasm().addPtr(scratch2, scratch);
    base = scratch;
    lowOffset = Imm16(0).encode();
    hiOffset = Imm16(size / 8 - 1).encode();
  }

  BufferOffset load;
  unsigned byteSize = access.byteSize();
  switch (size) {
    case SizeHalfWord:
      // begins with 1-byte load
      byteSize = 1;
      if (extension == ZeroExtend) {
        load = as_lbu(temp, base, hiOffset);
      } else {
        load = as_lb(temp, base, hiOffset);
      }
      as_lbu(dest, base, lowOffset);
      if (hasR2()) {
        as_ins(dest, temp, 8, 24);
      } else {
        as_sll(temp, temp, 8);
        as_or(dest, dest, temp);
      }
      break;
    case SizeWord:
      load = as_lwl(dest, base, hiOffset);
      as_lwr(dest, base, lowOffset);
      if (extension == ZeroExtend) {
        asMasm().ma_dext(dest, dest, Imm32(0), Imm32(32));
      }
      break;
    case SizeDouble:
      load = as_ldl(dest, base, hiOffset);
      as_ldr(dest, base, lowOffset);
      break;
    default:
      MOZ_CRASH("Invalid argument for ma_load");
  }

  FaultingCodeRange fcr(load.getOffset());
  asMasm().appendAndVerify(access, wasm::TrapMachineInsnForLoad(byteSize), fcr);
  return fcr;
}

FaultingCodeRange MacroAssemblerMIPSShared::ma_store(
    Register data, const BaseIndex& dest, LoadStoreSize size,
    LoadStoreExtension extension) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  if (isLoongson() && Imm8::IsInSignedRange(dest.offset)) {
    FaultingCodeRange fcr;
    Register index = dest.index;

    if (dest.scale != TimesOne) {
      int32_t shift = Imm32::ShiftOf(dest.scale).value;

      MOZ_ASSERT(scratch2 != dest.base);
      index = scratch2;
      asMasm().ma_dsll(index, dest.index, Imm32(shift));
    }

    fcr = FaultingCodeRange(currentOffset());
    switch (size) {
      case SizeByte:
        as_gssbx(data, dest.base, index, dest.offset);
        break;
      case SizeHalfWord:
        as_gsshx(data, dest.base, index, dest.offset);
        break;
      case SizeWord:
        as_gsswx(data, dest.base, index, dest.offset);
        break;
      case SizeDouble:
        as_gssdx(data, dest.base, index, dest.offset);
        break;
      default:
        MOZ_CRASH("Invalid argument for ma_store");
    }
    return fcr;
  }

  asMasm().computeScaledAddress(dest, scratch2);
  return asMasm().ma_store(data, Address(scratch2, dest.offset), size,
                           extension);
}

void MacroAssemblerMIPSShared::ma_store(Imm32 imm, const BaseIndex& dest,
                                        LoadStoreSize size,
                                        LoadStoreExtension extension) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  if (isLoongson() && Imm8::IsInSignedRange(dest.offset)) {
    Register data = zero;
    Register index = dest.index;

    if (imm.value) {
      Register scratch = temps.Acquire();
      MOZ_ASSERT(scratch != dest.base);
      MOZ_ASSERT(scratch != dest.index);
      data = scratch;
      ma_li(data, imm);
    }

    if (dest.scale != TimesOne) {
      int32_t shift = Imm32::ShiftOf(dest.scale).value;

      MOZ_ASSERT(scratch2 != dest.base);
      index = scratch2;
      asMasm().ma_dsll(index, dest.index, Imm32(shift));
    }

    switch (size) {
      case SizeByte:
        as_gssbx(data, dest.base, index, dest.offset);
        break;
      case SizeHalfWord:
        as_gsshx(data, dest.base, index, dest.offset);
        break;
      case SizeWord:
        as_gsswx(data, dest.base, index, dest.offset);
        break;
      case SizeDouble:
        as_gssdx(data, dest.base, index, dest.offset);
        break;
      default:
        MOZ_CRASH("Invalid argument for ma_store");
    }
    return;
  }

  // Make sure that scratch2 contains absolute address so that
  // offset is 0.
  asMasm().computeEffectiveAddress(dest, scratch2);

  Register scratch = temps.Acquire();
  // Scrach register is free now, use it for loading imm value
  ma_li(scratch, imm);

  // with offset=0 scratch will not be used in ma_store()
  // so we can use it as a parameter here
  asMasm().ma_store(scratch, Address(scratch2, 0), size, extension);
}

void MacroAssemblerMIPSShared::ma_store_unaligned(Register data,
                                                  const Address& address,
                                                  LoadStoreSize size) {
  int16_t lowOffset, hiOffset;
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  Register base;

  if (Imm16::IsInSignedRange(address.offset) &&
      Imm16::IsInSignedRange(address.offset + size / 8 - 1)) {
    base = address.base;
    lowOffset = Imm16(address.offset).encode();
    hiOffset = Imm16(address.offset + size / 8 - 1).encode();
  } else {
    ma_li(scratch, Imm32(address.offset));
    asMasm().addPtr(address.base, scratch);
    base = scratch;
    lowOffset = Imm16(0).encode();
    hiOffset = Imm16(size / 8 - 1).encode();
  }

  switch (size) {
    case SizeHalfWord: {
      UseScratchRegisterScope temps2(*this);
      Register scratch2 = temps2.Acquire();
      MOZ_ASSERT(base != scratch2);
      as_sb(data, base, lowOffset);
      ma_ext(scratch2, data, 8, 8);
      as_sb(scratch2, base, hiOffset);
      break;
    }
    case SizeWord:
      as_swl(data, base, hiOffset);
      as_swr(data, base, lowOffset);
      break;
    case SizeDouble:
      as_sdl(data, base, hiOffset);
      as_sdr(data, base, lowOffset);
      break;
    default:
      MOZ_CRASH("Invalid argument for ma_store_unaligned");
  }
}

void MacroAssemblerMIPSShared::ma_store_unaligned(Register data,
                                                  const BaseIndex& dest,
                                                  LoadStoreSize size) {
  int16_t lowOffset, hiOffset;
  UseScratchRegisterScope temps(*this);
  Register base = temps.Acquire();
  asMasm().computeScaledAddress(dest, base);
  Register scratch = temps.Acquire();

  if (Imm16::IsInSignedRange(dest.offset) &&
      Imm16::IsInSignedRange(dest.offset + size / 8 - 1)) {
    lowOffset = Imm16(dest.offset).encode();
    hiOffset = Imm16(dest.offset + size / 8 - 1).encode();
  } else {
    ma_li(scratch, Imm32(dest.offset));
    asMasm().addPtr(scratch, base);
    lowOffset = Imm16(0).encode();
    hiOffset = Imm16(size / 8 - 1).encode();
  }

  switch (size) {
    case SizeHalfWord:
      MOZ_ASSERT(base != scratch);
      as_sb(data, base, lowOffset);
      ma_ext(scratch, data, 8, 8);
      as_sb(scratch, base, hiOffset);
      break;
    case SizeWord:
      as_swl(data, base, hiOffset);
      as_swr(data, base, lowOffset);
      break;
    case SizeDouble:
      as_sdl(data, base, hiOffset);
      as_sdr(data, base, lowOffset);
      break;
    default:
      MOZ_CRASH("Invalid argument for ma_store_unaligned");
  }
}

FaultingCodeRange MacroAssemblerMIPSShared::ma_store_unaligned(
    const wasm::MemoryAccessDesc& access, Register data, const BaseIndex& dest,
    Register temp, LoadStoreSize size, LoadStoreExtension extension) {
  MOZ_ASSERT(std::endian::native == std::endian::little,
             "Wasm-only; wasm is disabled on big-endian.");
  int16_t lowOffset, hiOffset;
  Register base;

  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  asMasm().computeScaledAddress(dest, scratch2);

  if (Imm16::IsInSignedRange(dest.offset) &&
      Imm16::IsInSignedRange(dest.offset + size / 8 - 1)) {
    base = scratch2;
    lowOffset = Imm16(dest.offset).encode();
    hiOffset = Imm16(dest.offset + size / 8 - 1).encode();
  } else {
    Register scratch = temps.Acquire();
    ma_li(scratch, Imm32(dest.offset));
    asMasm().addPtr(scratch2, scratch);
    base = scratch;
    lowOffset = Imm16(0).encode();
    hiOffset = Imm16(size / 8 - 1).encode();
  }

  BufferOffset store;
  unsigned byteSize = access.byteSize();
  switch (size) {
    case SizeHalfWord:
      // begins with 1-byte store
      byteSize = 1;
      ma_ext(temp, data, 8, 8);
      store = as_sb(temp, base, hiOffset);
      as_sb(data, base, lowOffset);
      break;
    case SizeWord:
      store = as_swl(data, base, hiOffset);
      as_swr(data, base, lowOffset);
      break;
    case SizeDouble:
      store = as_sdl(data, base, hiOffset);
      as_sdr(data, base, lowOffset);
      break;
    default:
      MOZ_CRASH("Invalid argument for ma_store");
  }
  FaultingCodeRange fcr(store.getOffset());
  asMasm().appendAndVerify(access, wasm::TrapMachineInsnForStore(byteSize),
                           fcr);
  return fcr;
}

// Branches when done from within mips-specific code.
void MacroAssemblerMIPSShared::ma_b(Register lhs, Register rhs, Label* label,
                                    Condition c, JumpKind jumpKind) {
  switch (c) {
    case Equal:
    case NotEqual:
      asMasm().branchWithCode(getBranchCode(lhs, rhs, c), label, jumpKind);
      break;
    case Always:
      ma_b(label, jumpKind);
      break;
    case Zero:
    case NonZero:
    case Signed:
    case NotSigned:
      MOZ_ASSERT(lhs == rhs);
      asMasm().branchWithCode(getBranchCode(lhs, c), label, jumpKind);
      break;
    default: {
      UseScratchRegisterScope temps(*this);
      Register scratch = temps.Acquire();
      Condition cond = ma_cmp(scratch, lhs, rhs, c);
      asMasm().branchWithCode(getBranchCode(scratch, cond), label, jumpKind,
                              scratch);
    } break;
  }
}

void MacroAssemblerMIPSShared::ma_b(Register lhs, Imm32 imm, Label* label,
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
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    switch (c) {
      case Equal:
      case NotEqual:
        ma_li(scratch, imm);
        ma_b(lhs, scratch, label, c, jumpKind);
        break;
      default:
        Condition cond = ma_cmp(scratch, lhs, imm, c);
        asMasm().branchWithCode(getBranchCode(scratch, cond), label, jumpKind,
                                scratch);
    }
  }
}

void MacroAssemblerMIPSShared::ma_b(Register lhs, ImmPtr imm, Label* l,
                                    Condition c, JumpKind jumpKind) {
  asMasm().ma_b(lhs, ImmWord(uintptr_t(imm.value)), l, c, jumpKind);
}

void MacroAssemblerMIPSShared::ma_b(Label* label, JumpKind jumpKind) {
  asMasm().branchWithCode(getBranchCode(BranchIsJump), label, jumpKind);
}

Assembler::Condition MacroAssemblerMIPSShared::ma_cmp(Register dest,
                                                      Register lhs,
                                                      Register rhs,
                                                      Condition c) {
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

Assembler::Condition MacroAssemblerMIPSShared::ma_cmp(Register dest,
                                                      Register lhs, Imm32 imm,
                                                      Condition c) {
  UseScratchRegisterScope temps(*this);
  switch (c) {
    case Above:
    case BelowOrEqual:
      if (Imm16::IsInSignedRange(imm.value + 1) && imm.value != -1) {
        // lhs <= rhs via lhs < rhs + 1 if rhs + 1 does not overflow
        as_sltiu(dest, lhs, imm.value + 1);

        return (c == BelowOrEqual ? NotEqual : Equal);
      } else {
        Register scratch = dest == lhs ? temps.Acquire() : dest;
        ma_li(scratch, imm);
        as_sltu(dest, scratch, lhs);
        return (c == BelowOrEqual ? Equal : NotEqual);
      }
    case AboveOrEqual:
    case Below:
      if (Imm16::IsInSignedRange(imm.value)) {
        as_sltiu(dest, lhs, imm.value);
      } else {
        Register scratch = dest == lhs ? temps.Acquire() : dest;
        ma_li(scratch, imm);
        as_sltu(dest, lhs, scratch);
      }
      return (c == AboveOrEqual ? Equal : NotEqual);
    case GreaterThan:
    case LessThanOrEqual:
      if (Imm16::IsInSignedRange(imm.value + 1)) {
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
      if (Imm16::IsInSignedRange(imm.value)) {
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

void MacroAssemblerMIPSShared::ma_cmp_set(Register rd, Register rs, Register rt,
                                          Condition c) {
  switch (c) {
    case Equal:
      // seq d,s,t =>
      //   xor d,s,t
      //   sltiu d,d,1
      as_xor(rd, rs, rt);
      as_sltiu(rd, rd, 1);
      break;
    case NotEqual:
      // sne d,s,t =>
      //   xor d,s,t
      //   sltu d,$zero,d
      as_xor(rd, rs, rt);
      as_sltu(rd, zero, rd);
      break;
    case Above:
      // sgtu d,s,t =>
      //   sltu d,t,s
      as_sltu(rd, rt, rs);
      break;
    case AboveOrEqual:
      // sgeu d,s,t =>
      //   sltu d,s,t
      //   xori d,d,1
      as_sltu(rd, rs, rt);
      as_xori(rd, rd, 1);
      break;
    case Below:
      // sltu d,s,t
      as_sltu(rd, rs, rt);
      break;
    case BelowOrEqual:
      // sleu d,s,t =>
      //   sltu d,t,s
      //   xori d,d,1
      as_sltu(rd, rt, rs);
      as_xori(rd, rd, 1);
      break;
    case GreaterThan:
      // sgt d,s,t =>
      //   slt d,t,s
      as_slt(rd, rt, rs);
      break;
    case GreaterThanOrEqual:
      // sge d,s,t =>
      //   slt d,s,t
      //   xori d,d,1
      as_slt(rd, rs, rt);
      as_xori(rd, rd, 1);
      break;
    case LessThan:
      // slt d,s,t
      as_slt(rd, rs, rt);
      break;
    case LessThanOrEqual:
      // sle d,s,t =>
      //   slt d,t,s
      //   xori d,d,1
      as_slt(rd, rt, rs);
      as_xori(rd, rd, 1);
      break;
    case Zero:
      MOZ_ASSERT(rs == rt);
      // seq d,s,$zero =>
      //   sltiu d,s,1
      as_sltiu(rd, rs, 1);
      break;
    case NonZero:
      MOZ_ASSERT(rs == rt);
      // sne d,s,$zero =>
      //   sltu d,$zero,s
      as_sltu(rd, zero, rs);
      break;
    case Signed:
      MOZ_ASSERT(rs == rt);
      as_slt(rd, rs, zero);
      break;
    case NotSigned:
      MOZ_ASSERT(rs == rt);
      // sge d,s,$zero =>
      //   slt d,s,$zero
      //   xori d,d,1
      as_slt(rd, rs, zero);
      as_xori(rd, rd, 1);
      break;
    default:
      MOZ_CRASH("Invalid condition.");
  }
}

void MacroAssemblerMIPSShared::compareFloatingPoint(
    FloatFormat fmt, FloatRegister lhs, FloatRegister rhs, DoubleCondition c,
    FloatTestKind* testKind, FPConditionBit fcc) {
  switch (c) {
    case DoubleOrdered:
      as_cun(fmt, lhs, rhs, fcc);
      *testKind = TestForFalse;
      break;
    case DoubleEqual:
      as_ceq(fmt, lhs, rhs, fcc);
      *testKind = TestForTrue;
      break;
    case DoubleNotEqual:
      as_cueq(fmt, lhs, rhs, fcc);
      *testKind = TestForFalse;
      break;
    case DoubleGreaterThan:
      as_colt(fmt, rhs, lhs, fcc);
      *testKind = TestForTrue;
      break;
    case DoubleGreaterThanOrEqual:
      as_cole(fmt, rhs, lhs, fcc);
      *testKind = TestForTrue;
      break;
    case DoubleLessThan:
      as_colt(fmt, lhs, rhs, fcc);
      *testKind = TestForTrue;
      break;
    case DoubleLessThanOrEqual:
      as_cole(fmt, lhs, rhs, fcc);
      *testKind = TestForTrue;
      break;
    case DoubleUnordered:
      as_cun(fmt, lhs, rhs, fcc);
      *testKind = TestForTrue;
      break;
    case DoubleEqualOrUnordered:
      as_cueq(fmt, lhs, rhs, fcc);
      *testKind = TestForTrue;
      break;
    case DoubleNotEqualOrUnordered:
      as_ceq(fmt, lhs, rhs, fcc);
      *testKind = TestForFalse;
      break;
    case DoubleGreaterThanOrUnordered:
      as_cult(fmt, rhs, lhs, fcc);
      *testKind = TestForTrue;
      break;
    case DoubleGreaterThanOrEqualOrUnordered:
      as_cule(fmt, rhs, lhs, fcc);
      *testKind = TestForTrue;
      break;
    case DoubleLessThanOrUnordered:
      as_cult(fmt, lhs, rhs, fcc);
      *testKind = TestForTrue;
      break;
    case DoubleLessThanOrEqualOrUnordered:
      as_cule(fmt, lhs, rhs, fcc);
      *testKind = TestForTrue;
      break;
    default:
      MOZ_CRASH("Invalid DoubleCondition.");
  }
}

void MacroAssemblerMIPSShared::ma_cmp_set_double(Register dest,
                                                 FloatRegister lhs,
                                                 FloatRegister rhs,
                                                 DoubleCondition c) {
  FloatTestKind moveCondition;
  compareFloatingPoint(DoubleFloat, lhs, rhs, c, &moveCondition);

#ifdef MIPSR6
  as_mfc1(dest, FloatRegisters::f24);
  if (moveCondition == TestForTrue) {
    as_andi(dest, dest, 0x1);
  } else {
    as_addiu(dest, dest, 0x1);
  }
#else
  ma_li(dest, Imm32(1));

  if (moveCondition == TestForTrue) {
    as_movf(dest, zero);
  } else {
    as_movt(dest, zero);
  }
#endif
}

void MacroAssemblerMIPSShared::ma_cmp_set_float32(Register dest,
                                                  FloatRegister lhs,
                                                  FloatRegister rhs,
                                                  DoubleCondition c) {
  FloatTestKind moveCondition;
  compareFloatingPoint(SingleFloat, lhs, rhs, c, &moveCondition);

#ifdef MIPSR6
  as_mfc1(dest, FloatRegisters::f24);
  if (moveCondition == TestForTrue) {
    as_andi(dest, dest, 0x1);
  } else {
    as_addiu(dest, dest, 0x1);
  }
#else
  ma_li(dest, Imm32(1));

  if (moveCondition == TestForTrue) {
    as_movf(dest, zero);
  } else {
    as_movt(dest, zero);
  }
#endif
}

void MacroAssemblerMIPSShared::ma_cmp_set(Register rd, Register rs, Imm32 imm,
                                          Condition c) {
  if (imm.value == 0) {
    switch (c) {
      case Equal:
      case BelowOrEqual:
        as_sltiu(rd, rs, 1);
        break;
      case NotEqual:
      case Above:
        as_sltu(rd, zero, rs);
        break;
      case AboveOrEqual:
      case Below:
        as_ori(rd, zero, c == AboveOrEqual ? 1 : 0);
        break;
      case GreaterThan:
      case LessThanOrEqual:
        as_slt(rd, zero, rs);
        if (c == LessThanOrEqual) {
          as_xori(rd, rd, 1);
        }
        break;
      case LessThan:
      case GreaterThanOrEqual:
        as_slt(rd, rs, zero);
        if (c == GreaterThanOrEqual) {
          as_xori(rd, rd, 1);
        }
        break;
      case Zero:
        as_sltiu(rd, rs, 1);
        break;
      case NonZero:
        as_sltu(rd, zero, rs);
        break;
      case Signed:
        as_slt(rd, rs, zero);
        break;
      case NotSigned:
        as_slt(rd, rs, zero);
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
      ma_xor(rd, rs, imm);
      if (c == Equal) {
        as_sltiu(rd, rd, 1);
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
      Condition cond = ma_cmp(rd, rs, imm, c);
      MOZ_ASSERT(cond == Equal || cond == NotEqual);

      if (cond == Equal) as_xori(rd, rd, 1);
  }
}

// fp instructions
void MacroAssemblerMIPSShared::ma_lis(FloatRegister dest, float value) {
  Imm32 imm(mozilla::BitwiseCast<uint32_t>(value));

  if (imm.value != 0) {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    moveToFloat32(scratch, dest);
  } else {
    moveToFloat32(zero, dest);
  }
}

FaultingCodeRange MacroAssemblerMIPSShared::ma_sd(FloatRegister ft,
                                                  BaseIndex address) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  if (isLoongson() && Imm8::IsInSignedRange(address.offset)) {
    Register index = address.index;

    if (address.scale != TimesOne) {
      int32_t shift = Imm32::ShiftOf(address.scale).value;

      MOZ_ASSERT(scratch2 != address.base);
      index = scratch2;
      asMasm().ma_dsll(index, address.index, Imm32(shift));
    }

    FaultingCodeRange fcr = FaultingCodeRange(currentOffset());
    as_gssdx(ft, address.base, index, address.offset);
    return fcr;
  }

  asMasm().computeScaledAddress(address, scratch2);
  return asMasm().ma_sd(ft, Address(scratch2, address.offset));
}

FaultingCodeRange MacroAssemblerMIPSShared::ma_ss(FloatRegister ft,
                                                  BaseIndex address) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  if (isLoongson() && Imm8::IsInSignedRange(address.offset)) {
    Register index = address.index;

    if (address.scale != TimesOne) {
      int32_t shift = Imm32::ShiftOf(address.scale).value;

      MOZ_ASSERT(scratch2 != address.base);
      index = scratch2;
      asMasm().ma_dsll(index, address.index, Imm32(shift));
    }

    FaultingCodeRange fcr = FaultingCodeRange(currentOffset());
    as_gsssx(ft, address.base, index, address.offset);
    return fcr;
  }

  asMasm().computeScaledAddress(address, scratch2);
  return asMasm().ma_ss(ft, Address(scratch2, address.offset));
}

FaultingCodeRange MacroAssemblerMIPSShared::ma_ld(FloatRegister ft,
                                                  const BaseIndex& src) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  asMasm().computeScaledAddress(src, scratch2);
  return asMasm().ma_ld(ft, Address(scratch2, src.offset));
}

FaultingCodeRange MacroAssemblerMIPSShared::ma_ls(FloatRegister ft,
                                                  const BaseIndex& src) {
  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();
  asMasm().computeScaledAddress(src, scratch2);
  return asMasm().ma_ls(ft, Address(scratch2, src.offset));
}

void MacroAssemblerMIPSShared::ma_bc1s(FloatRegister lhs, FloatRegister rhs,
                                       Label* label, DoubleCondition c,
                                       JumpKind jumpKind, FPConditionBit fcc) {
  FloatTestKind testKind;
  compareFloatingPoint(SingleFloat, lhs, rhs, c, &testKind, fcc);
  asMasm().branchWithCode(getBranchCode(testKind, fcc), label, jumpKind);
}

void MacroAssemblerMIPSShared::ma_bc1d(FloatRegister lhs, FloatRegister rhs,
                                       Label* label, DoubleCondition c,
                                       JumpKind jumpKind, FPConditionBit fcc) {
  FloatTestKind testKind;
  compareFloatingPoint(DoubleFloat, lhs, rhs, c, &testKind, fcc);
  asMasm().branchWithCode(getBranchCode(testKind, fcc), label, jumpKind);
}

void MacroAssemblerMIPSShared::minMax32(Register lhs, Register rhs,
                                        Register dest, bool isMax) {
  if (rhs == dest) {
    std::swap(lhs, rhs);
  }

  auto cond = isMax ? Assembler::GreaterThan : Assembler::LessThan;
  if (lhs != dest) {
    asMasm().move32(lhs, dest);
  }
  asMasm().cmp32Move32(cond, rhs, lhs, rhs, dest);
}

void MacroAssemblerMIPSShared::minMax32(Register lhs, Imm32 rhs, Register dest,
                                        bool isMax) {
  if (rhs.value == 0) {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();

    if (isMax) {
      // dest = (~lhs >> 31) & lhs
      as_nor(scratch, lhs, zero);
      as_sra(scratch, scratch, 31);
      as_and(dest, lhs, scratch);
    } else {
      // dest = (lhs >> 31) & lhs
      as_sra(scratch, lhs, 31);
      as_and(dest, lhs, scratch);
    }
    return;
  }

  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  asMasm().move32(rhs, scratch);

  minMax32(lhs, scratch, dest, isMax);
}

void MacroAssemblerMIPSShared::minMaxPtr(Register lhs, Register rhs,
                                         Register dest, bool isMax) {
  if (rhs == dest) {
    std::swap(lhs, rhs);
  }

  auto cond = isMax ? Assembler::GreaterThan : Assembler::LessThan;
  if (lhs != dest) {
    asMasm().movePtr(lhs, dest);
  }
  asMasm().cmpPtrMovePtr(cond, rhs, lhs, rhs, dest);
}

void MacroAssemblerMIPSShared::minMaxPtr(Register lhs, ImmWord rhs,
                                         Register dest, bool isMax) {
  if (rhs.value == 0) {
    UseScratchRegisterScope temps(*this);
    Register scratch = temps.Acquire();

    if (isMax) {
      // dest = (~lhs >> 63) & lhs
      as_nor(scratch, lhs, zero);
      as_dsra32(scratch, scratch, 63);
      as_and(dest, lhs, scratch);
    } else {
      // dest = (lhs >> 63) & lhs
      as_dsra32(scratch, lhs, 63);
      as_and(dest, lhs, scratch);
    }
    return;
  }

  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  asMasm().movePtr(rhs, scratch);

  minMaxPtr(lhs, scratch, dest, isMax);
}

void MacroAssemblerMIPSShared::minMaxDouble(FloatRegister srcDest,
                                            FloatRegister second,
                                            bool handleNaN, bool isMax) {
  FloatRegister first = srcDest;

  Assembler::DoubleCondition cond = isMax ? Assembler::DoubleLessThanOrEqual
                                          : Assembler::DoubleGreaterThanOrEqual;
  Label nan, equal, done;
  FloatTestKind moveCondition;

  // First or second is NaN, result is NaN.
  ma_bc1d(first, second, &nan, Assembler::DoubleUnordered, ShortJump);
#ifdef MIPSR6
  if (isMax) {
    as_max(DoubleFloat, srcDest, first, second);
  } else {
    as_min(DoubleFloat, srcDest, first, second);
  }
#else
  // Make sure we handle -0 and 0 right.
  ma_bc1d(first, second, &equal, Assembler::DoubleEqual, ShortJump);
  compareFloatingPoint(DoubleFloat, first, second, cond, &moveCondition);
  MOZ_ASSERT(TestForTrue == moveCondition);
  as_movt(DoubleFloat, first, second);
  ma_b(&done, ShortJump);

  // Check for zero.
  bind(&equal);
  asMasm().loadConstantDouble(0.0, ScratchDoubleReg);
  compareFloatingPoint(DoubleFloat, first, ScratchDoubleReg,
                       Assembler::DoubleEqual, &moveCondition);

  // So now both operands are either -0 or 0.
  if (isMax) {
    // -0 + -0 = -0 and -0 + 0 = 0.
    as_addd(ScratchDoubleReg, first, second);
  } else {
    as_negd(ScratchDoubleReg, first);
    as_subd(ScratchDoubleReg, ScratchDoubleReg, second);
    as_negd(ScratchDoubleReg, ScratchDoubleReg);
  }
  MOZ_ASSERT(TestForTrue == moveCondition);
  // First is 0 or -0, move max/min to it, else just return it.
  as_movt(DoubleFloat, first, ScratchDoubleReg);
#endif
  ma_b(&done, ShortJump);

  bind(&nan);
  asMasm().loadConstantDouble(JS::GenericNaN(), srcDest);

  bind(&done);
}

void MacroAssemblerMIPSShared::minMaxFloat32(FloatRegister srcDest,
                                             FloatRegister second,
                                             bool handleNaN, bool isMax) {
  FloatRegister first = srcDest;

  Assembler::DoubleCondition cond = isMax ? Assembler::DoubleLessThanOrEqual
                                          : Assembler::DoubleGreaterThanOrEqual;
  Label nan, equal, done;
  FloatTestKind moveCondition;

  // First or second is NaN, result is NaN.
  ma_bc1s(first, second, &nan, Assembler::DoubleUnordered, ShortJump);
#ifdef MIPSR6
  if (isMax) {
    as_max(SingleFloat, srcDest, first, second);
  } else {
    as_min(SingleFloat, srcDest, first, second);
  }
#else
  // Make sure we handle -0 and 0 right.
  ma_bc1s(first, second, &equal, Assembler::DoubleEqual, ShortJump);
  compareFloatingPoint(SingleFloat, first, second, cond, &moveCondition);
  MOZ_ASSERT(TestForTrue == moveCondition);
  as_movt(SingleFloat, first, second);
  ma_b(&done, ShortJump);

  // Check for zero.
  bind(&equal);
  asMasm().loadConstantFloat32(0.0f, ScratchFloat32Reg);
  compareFloatingPoint(SingleFloat, first, ScratchFloat32Reg,
                       Assembler::DoubleEqual, &moveCondition);

  // So now both operands are either -0 or 0.
  if (isMax) {
    // -0 + -0 = -0 and -0 + 0 = 0.
    as_adds(ScratchFloat32Reg, first, second);
  } else {
    as_negs(ScratchFloat32Reg, first);
    as_subs(ScratchFloat32Reg, ScratchFloat32Reg, second);
    as_negs(ScratchFloat32Reg, ScratchFloat32Reg);
  }
  MOZ_ASSERT(TestForTrue == moveCondition);
  // First is 0 or -0, move max/min to it, else just return it.
  as_movt(SingleFloat, first, ScratchFloat32Reg);
#endif
  ma_b(&done, ShortJump);

  bind(&nan);
  asMasm().loadConstantFloat32(JS::GenericNaN(), srcDest);

  bind(&done);
}

FaultingCodeRange MacroAssemblerMIPSShared::loadDouble(const Address& address,
                                                       FloatRegister dest) {
  return asMasm().ma_ld(dest, address);
}

FaultingCodeRange MacroAssemblerMIPSShared::loadDouble(const BaseIndex& src,
                                                       FloatRegister dest) {
  return asMasm().ma_ld(dest, src);
}

FaultingCodeRange MacroAssemblerMIPSShared::loadFloat32(const Address& address,
                                                        FloatRegister dest) {
  return asMasm().ma_ls(dest, address);
}

FaultingCodeRange MacroAssemblerMIPSShared::loadFloat32(const BaseIndex& src,
                                                        FloatRegister dest) {
  return asMasm().ma_ls(dest, src);
}

void MacroAssemblerMIPSShared::ma_call(ImmPtr dest) {
  asMasm().ma_liPatchable(CallReg, dest);
  as_jalr(CallReg);
  as_nop();
}

void MacroAssemblerMIPSShared::ma_jump(ImmPtr dest) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  asMasm().ma_liPatchable(scratch, dest);
  as_jr(scratch);
  as_nop();
}

MacroAssembler& MacroAssemblerMIPSShared::asMasm() {
  return *static_cast<MacroAssembler*>(this);
}

const MacroAssembler& MacroAssemblerMIPSShared::asMasm() const {
  return *static_cast<const MacroAssembler*>(this);
}

//{{{ check_macroassembler_style
// ===============================================================
// MacroAssembler high-level usage.

void MacroAssembler::flush() {}

// ===============================================================
// Stack manipulation functions.

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
  adjustFrame(int32_t(f.pushSize()));
}

void MacroAssembler::Pop(Register reg) {
  pop(reg);
  adjustFrame(-int32_t(sizeof(intptr_t)));
}

void MacroAssembler::Pop(FloatRegister f) {
  pop(f);
  adjustFrame(-int32_t(f.pushSize()));
}

void MacroAssembler::Pop(const ValueOperand& val) {
  popValue(val);
  adjustFrame(-int32_t(sizeof(Value)));
}

void MacroAssembler::PopStackPtr() {
  loadPtr(Address(StackPointer, 0), StackPointer);
  adjustFrame(-int32_t(sizeof(intptr_t)));
}

// ===============================================================
// Simple call functions.

CodeOffset MacroAssembler::call(Register reg) {
  as_jalr(reg);
  as_nop();
  return CodeOffset(currentOffset());
}

CodeOffset MacroAssembler::call(Label* label) {
  ma_bal(label);
  return CodeOffset(currentOffset());
}

CodeOffset MacroAssembler::callWithPatch() {
  UseScratchRegisterScope temps(*this);
  as_bal(BOffImm16(3 * sizeof(uint32_t)));
  addPtr(Imm32(5 * sizeof(uint32_t)), ra);
  // Allocate space which will be patched by patchCall().
  spew(".space 32bit initValue 0xffff ffff");
  writeInst(UINT32_MAX);
  Register scratch = temps.Acquire();
  as_lw(scratch, ra, -(int32_t)(5 * sizeof(uint32_t)));
  addPtr(ra, scratch);
  as_jr(scratch);
  as_nop();
  return CodeOffset(currentOffset());
}

void MacroAssembler::patchCall(uint32_t callerOffset, uint32_t calleeOffset) {
  BufferOffset call(callerOffset - 7 * sizeof(uint32_t));

  BOffImm16 offset = BufferOffset(calleeOffset).diffB<BOffImm16>(call);
  if (!offset.isInvalid()) {
    InstImm* bal = (InstImm*)editSrc(call);
    bal->setBOffImm16(offset);
  } else {
    uint32_t u32Offset = callerOffset - 5 * sizeof(uint32_t);
    uint32_t* u32 =
        reinterpret_cast<uint32_t*>(editSrc(BufferOffset(u32Offset)));
    *u32 = calleeOffset - callerOffset;
  }
}

CodeOffset MacroAssembler::farJumpWithPatch() {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  ma_move(scratch, ra);
  as_bal(BOffImm16(3 * sizeof(uint32_t)));
  Register scratch2 = temps.Acquire();
  as_lw(scratch2, ra, 0);
  // Allocate space which will be patched by patchFarJump().
  CodeOffset farJump(currentOffset());
  spew(".space 32bit initValue 0xffff ffff");
  writeInst(UINT32_MAX);
  addPtr(ra, scratch2);
  as_jr(scratch2);
  ma_move(ra, scratch);
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

  *u32 = (intptr_t)target - (intptr_t)farJump;
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
  UseScratchRegisterScope temps(*this);
  BufferOffset bo = m_buffer.nextOffset();
  addPendingJump(bo, ImmPtr(c->raw()), RelocationKind::JITCODE);
  Register scratch = temps.Acquire();
  ma_liPatchable(scratch, ImmPtr(c->raw()));
  callJitNoProfiler(scratch);
}

// ===============================================================
// Tail jumps.

void MacroAssembler::tailJump(Register reg) { jump(reg); }

void MacroAssembler::tailJump(const Address& addr) { jump(addr); }

void MacroAssembler::tailJump(TrampolinePtr code) { jump(code); }

void MacroAssembler::tailJump(JitCode* code) { jump(code); }

// ===============================================================
// Patchable near/far jumps.

CodeOffset MacroAssembler::nopPatchableToCall() {
  as_nop();  // lui
  as_nop();  // ori
  as_nop();  // dsll
  as_nop();  // ori
  as_nop();  // jalr
  as_nop();
  return CodeOffset(currentOffset());
}

void MacroAssembler::patchNopToCall(uint8_t* call, uint8_t* target) {
  Instruction* inst = (Instruction*)call - 6 /* six nops */;
  Assembler::WriteLoad64Instructions(inst, ScratchRegister, (uint64_t)target);
  inst[4] = InstReg(op_special, ScratchRegister, zero, ra, ff_jalr);
}

void MacroAssembler::patchCallToNop(uint8_t* call) {
  Instruction* inst = (Instruction*)call - 6 /* six nops */;

  inst[0].makeNop();
  inst[1].makeNop();
  inst[2].makeNop();
  inst[3].makeNop();
  inst[4].makeNop();
  inst[5].makeNop();
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

void MacroAssembler::loadStoreBuffer(Register ptr, Register buffer) {
  ma_and(buffer, ptr, Imm32(int32_t(~gc::ChunkMask)));
  loadPtr(Address(buffer, gc::ChunkStoreBufferOffset), buffer);
}

void MacroAssembler::branchPtrInNurseryChunk(Condition cond, Register ptr,
                                             Register temp, Label* label) {
  MOZ_ASSERT(cond == Assembler::Equal || cond == Assembler::NotEqual);
  MOZ_ASSERT(ptr != temp);

  UseScratchRegisterScope temps(*this);
  Register scratch2 = temps.Acquire();

  ma_and(scratch2, ptr, Imm32(int32_t(~gc::ChunkMask)));
  branchPtr(InvertCondition(cond),
            Address(scratch2, gc::ChunkStoreBufferOffset), ImmWord(0), label);
}

void MacroAssembler::comment(const char* msg) { Assembler::comment(msg); }

// ===============================================================
// WebAssembly

FaultingCodeRange MacroAssembler::wasmTrapInstruction() {
  FaultingCodeRange fcr = FaultingCodeRange(currentOffset());
  as_teq(zero, zero, WASM_TRAP);
  return fcr;
}

void MacroAssembler::wasmTruncateDoubleToInt32(FloatRegister input,
                                               Register output,
                                               bool isSaturating,
                                               Label* oolEntry) {
  UseScratchRegisterScope temps(*this);
  as_truncwd(ScratchFloat32Reg, input);
  Register scratch = temps.Acquire();
  as_cfc1(scratch, Assembler::FCSR);
  moveFromFloat32(ScratchFloat32Reg, output);
  ma_ext(scratch, scratch, Assembler::CauseV, 1);
  ma_b(scratch, Imm32(0), oolEntry, Assembler::NotEqual);
}

void MacroAssembler::wasmTruncateFloat32ToInt32(FloatRegister input,
                                                Register output,
                                                bool isSaturating,
                                                Label* oolEntry) {
  UseScratchRegisterScope temps(*this);
  as_truncws(ScratchFloat32Reg, input);
  Register scratch = temps.Acquire();
  as_cfc1(scratch, Assembler::FCSR);
  moveFromFloat32(ScratchFloat32Reg, output);
  ma_ext(scratch, scratch, Assembler::CauseV, 1);
  ma_b(scratch, Imm32(0), oolEntry, Assembler::NotEqual);
}

void MacroAssembler::oolWasmTruncateCheckF32ToI32(
    FloatRegister input, Register output, TruncFlags flags,
    const wasm::TrapSiteDesc& trapSiteDesc, Label* rejoin,
    wasm::StackMap* stackMapForTraps,
    wasm::StackMapRegistry* stackMapRegistry) {
  outOfLineWasmTruncateToInt32Check(input, output, MIRType::Float32, flags,
                                    rejoin, trapSiteDesc, stackMapForTraps,
                                    stackMapRegistry);
}

void MacroAssembler::oolWasmTruncateCheckF64ToI32(
    FloatRegister input, Register output, TruncFlags flags,
    const wasm::TrapSiteDesc& trapSiteDesc, Label* rejoin,
    wasm::StackMap* stackMapForTraps,
    wasm::StackMapRegistry* stackMapRegistry) {
  outOfLineWasmTruncateToInt32Check(input, output, MIRType::Double, flags,
                                    rejoin, trapSiteDesc, stackMapForTraps,
                                    stackMapRegistry);
}

void MacroAssembler::oolWasmTruncateCheckF32ToI64(
    FloatRegister input, Register64 output, TruncFlags flags,
    const wasm::TrapSiteDesc& trapSiteDesc, Label* rejoin,
    wasm::StackMap* stackMapForTraps,
    wasm::StackMapRegistry* stackMapRegistry) {
  outOfLineWasmTruncateToInt64Check(input, output, MIRType::Float32, flags,
                                    rejoin, trapSiteDesc, stackMapForTraps,
                                    stackMapRegistry);
}

void MacroAssembler::oolWasmTruncateCheckF64ToI64(
    FloatRegister input, Register64 output, TruncFlags flags,
    const wasm::TrapSiteDesc& trapSiteDesc, Label* rejoin,
    wasm::StackMap* stackMapForTraps,
    wasm::StackMapRegistry* stackMapRegistry) {
  outOfLineWasmTruncateToInt64Check(input, output, MIRType::Double, flags,
                                    rejoin, trapSiteDesc, stackMapForTraps,
                                    stackMapRegistry);
}

void MacroAssemblerMIPSShared::outOfLineWasmTruncateToInt32Check(
    FloatRegister input, Register output, MIRType fromType, TruncFlags flags,
    Label* rejoin, const wasm::TrapSiteDesc& trapSiteDesc,
    wasm::StackMap* stackMapForTraps,
    wasm::StackMapRegistry* stackMapRegistry) {
  bool isUnsigned = flags & TRUNC_UNSIGNED;
  bool isSaturating = flags & TRUNC_SATURATING;

  if (isSaturating) {
    if (fromType == MIRType::Double) {
      asMasm().loadConstantDouble(0.0, ScratchDoubleReg);
    } else {
      asMasm().loadConstantFloat32(0.0f, ScratchFloat32Reg);
    }

    if (isUnsigned) {
      ma_li(output, Imm32(UINT32_MAX));

      FloatTestKind moveCondition;
      compareFloatingPoint(
          fromType == MIRType::Double ? DoubleFloat : SingleFloat, input,
          fromType == MIRType::Double ? ScratchDoubleReg : ScratchFloat32Reg,
          Assembler::DoubleLessThanOrUnordered, &moveCondition);
      MOZ_ASSERT(moveCondition == TestForTrue);

      as_movt(output, zero);
    } else {
      // Positive overflow is already saturated to INT32_MAX, so we only have
      // to handle NaN and negative overflow here.

      FloatTestKind moveCondition;
      compareFloatingPoint(
          fromType == MIRType::Double ? DoubleFloat : SingleFloat, input, input,
          Assembler::DoubleUnordered, &moveCondition);
      MOZ_ASSERT(moveCondition == TestForTrue);

      as_movt(output, zero);

      compareFloatingPoint(
          fromType == MIRType::Double ? DoubleFloat : SingleFloat, input,
          fromType == MIRType::Double ? ScratchDoubleReg : ScratchFloat32Reg,
          Assembler::DoubleLessThan, &moveCondition);
      MOZ_ASSERT(moveCondition == TestForTrue);

      UseScratchRegisterScope temps(*this);
      Register scratch = temps.Acquire();
      ma_li(scratch, Imm32(INT32_MIN));
      as_movt(output, scratch);
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

  FaultingCodeRange fcr1 =
      asMasm().wasmTrap(wasm::Trap::IntegerOverflow, trapSiteDesc);
  if (stackMapRegistry) {
    propagateOOM(stackMapRegistry->addMap(stackMapForTraps, fcr1));
  }
  asMasm().bind(&inputIsNaN);
  FaultingCodeRange fcr2 =
      asMasm().wasmTrap(wasm::Trap::InvalidConversionToInteger, trapSiteDesc);
  if (stackMapRegistry) {
    propagateOOM(stackMapRegistry->addMap(stackMapForTraps, fcr2));
  }
}

void MacroAssemblerMIPSShared::outOfLineWasmTruncateToInt64Check(
    FloatRegister input, Register64 output_, MIRType fromType, TruncFlags flags,
    Label* rejoin, const wasm::TrapSiteDesc& trapSiteDesc,
    wasm::StackMap* stackMapForTraps,
    wasm::StackMapRegistry* stackMapRegistry) {
  bool isUnsigned = flags & TRUNC_UNSIGNED;
  bool isSaturating = flags & TRUNC_SATURATING;

  if (isSaturating) {
    Register output = output_.reg;

    if (fromType == MIRType::Double) {
      asMasm().loadConstantDouble(0.0, ScratchDoubleReg);
    } else {
      asMasm().loadConstantFloat32(0.0f, ScratchFloat32Reg);
    }

    if (isUnsigned) {
      asMasm().ma_li(output, ImmWord(UINT64_MAX));

      FloatTestKind moveCondition;
      compareFloatingPoint(
          fromType == MIRType::Double ? DoubleFloat : SingleFloat, input,
          fromType == MIRType::Double ? ScratchDoubleReg : ScratchFloat32Reg,
          Assembler::DoubleLessThanOrUnordered, &moveCondition);
      MOZ_ASSERT(moveCondition == TestForTrue);

      as_movt(output, zero);

    } else {
      // Positive overflow is already saturated to INT64_MAX, so we only have
      // to handle NaN and negative overflow here.

      FloatTestKind moveCondition;
      compareFloatingPoint(
          fromType == MIRType::Double ? DoubleFloat : SingleFloat, input, input,
          Assembler::DoubleUnordered, &moveCondition);
      MOZ_ASSERT(moveCondition == TestForTrue);

      as_movt(output, zero);

      compareFloatingPoint(
          fromType == MIRType::Double ? DoubleFloat : SingleFloat, input,
          fromType == MIRType::Double ? ScratchDoubleReg : ScratchFloat32Reg,
          Assembler::DoubleLessThan, &moveCondition);
      MOZ_ASSERT(moveCondition == TestForTrue);

      UseScratchRegisterScope temps(*this);
      Register scratch = temps.Acquire();
      asMasm().ma_li(scratch, ImmWord(INT64_MIN));
      as_movt(output, scratch);
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

  FaultingCodeRange fcr1 =
      asMasm().wasmTrap(wasm::Trap::IntegerOverflow, trapSiteDesc);
  if (stackMapRegistry) {
    propagateOOM(stackMapRegistry->addMap(stackMapForTraps, fcr1));
  }
  asMasm().bind(&inputIsNaN);
  FaultingCodeRange fcr2 =
      asMasm().wasmTrap(wasm::Trap::InvalidConversionToInteger, trapSiteDesc);
  if (stackMapRegistry) {
    propagateOOM(stackMapRegistry->addMap(stackMapForTraps, fcr2));
  }
}

FaultingCodeRange MacroAssembler::wasmLoad(const wasm::MemoryAccessDesc& access,
                                           Register memoryBase, Register ptr,
                                           Register ptrScratch,
                                           AnyRegister output) {
  return wasmLoadImpl(access, memoryBase, ptr, ptrScratch, output, InvalidReg);
}

FaultingCodeRange MacroAssembler::wasmUnalignedLoad(
    const wasm::MemoryAccessDesc& access, Register memoryBase, Register ptr,
    Register ptrScratch, Register output, Register tmp) {
  return wasmLoadImpl(access, memoryBase, ptr, ptrScratch, AnyRegister(output),
                      tmp);
}

FaultingCodeRange MacroAssembler::wasmUnalignedLoadFP(
    const wasm::MemoryAccessDesc& access, Register memoryBase, Register ptr,
    Register ptrScratch, FloatRegister output, Register tmp1) {
  return wasmLoadImpl(access, memoryBase, ptr, ptrScratch, AnyRegister(output),
                      tmp1);
}

FaultingCodeRange MacroAssembler::wasmStore(
    const wasm::MemoryAccessDesc& access, AnyRegister value,
    Register memoryBase, Register ptr, Register ptrScratch) {
  return wasmStoreImpl(access, value, memoryBase, ptr, ptrScratch, InvalidReg);
}

FaultingCodeRange MacroAssembler::wasmUnalignedStore(
    const wasm::MemoryAccessDesc& access, Register value, Register memoryBase,
    Register ptr, Register ptrScratch, Register tmp) {
  return wasmStoreImpl(access, AnyRegister(value), memoryBase, ptr, ptrScratch,
                       tmp);
}

FaultingCodeRange MacroAssembler::wasmUnalignedStoreFP(
    const wasm::MemoryAccessDesc& access, FloatRegister floatValue,
    Register memoryBase, Register ptr, Register ptrScratch, Register tmp) {
  return wasmStoreImpl(access, AnyRegister(floatValue), memoryBase, ptr,
                       ptrScratch, tmp);
}

FaultingCodeRange MacroAssemblerMIPSShared::wasmLoadImpl(
    const wasm::MemoryAccessDesc& access, Register memoryBase, Register ptr,
    Register ptrScratch, AnyRegister output, Register tmp) {
  access.assertOffsetInGuardPages();
  uint32_t offset = access.offset32();
  MOZ_ASSERT_IF(offset, ptrScratch != InvalidReg);

  // Maybe add the offset.
  if (offset) {
    asMasm().addPtr(ImmWord(offset), ptrScratch);
    ptr = ptrScratch;
  }

  unsigned byteSize = access.byteSize();
  bool isSigned = Scalar::isSignedIntType(access.type());
  bool isFloat = Scalar::isFloatingType(access.type());

  MOZ_ASSERT(!access.isZeroExtendSimd128Load());
  MOZ_ASSERT(!access.isSplatSimd128Load());
  MOZ_ASSERT(!access.isWidenSimd128Load());

  BaseIndex address(memoryBase, ptr, TimesOne);
  if (IsUnaligned(access)) {
    MOZ_ASSERT(tmp != InvalidReg);
    if (isFloat) {
      if (byteSize == 4) {
        return asMasm().loadUnalignedFloat32(access, address, tmp,
                                             output.fpu());
      }
      return asMasm().loadUnalignedDouble(access, address, tmp, output.fpu());
    }
    return asMasm().ma_load_unaligned(access, output.gpr(), address, tmp,
                                      static_cast<LoadStoreSize>(8 * byteSize),
                                      isSigned ? SignExtend : ZeroExtend);
  }

  asMasm().memoryBarrierBefore(access.sync());
  FaultingCodeRange fcr;
  if (isFloat) {
    if (byteSize == 4) {
      fcr = asMasm().ma_ls(output.fpu(), address);
    } else {
      fcr = asMasm().ma_ld(output.fpu(), address);
    }
  } else {
    fcr = asMasm().ma_load(output.gpr(), address,
                           static_cast<LoadStoreSize>(8 * byteSize),
                           isSigned ? SignExtend : ZeroExtend);
  }
  asMasm().appendAndVerify(
      access, wasm::TrapMachineInsnForLoad(Scalar::byteSize(access.type())),
      fcr);

  asMasm().memoryBarrierAfter(access.sync());
  return fcr;
}

FaultingCodeRange MacroAssemblerMIPSShared::wasmStoreImpl(
    const wasm::MemoryAccessDesc& access, AnyRegister value,
    Register memoryBase, Register ptr, Register ptrScratch, Register tmp) {
  access.assertOffsetInGuardPages();
  uint32_t offset = access.offset32();
  MOZ_ASSERT_IF(offset, ptrScratch != InvalidReg);

  // Maybe add the offset.
  if (offset) {
    asMasm().addPtr(ImmWord(offset), ptrScratch);
    ptr = ptrScratch;
  }

  unsigned byteSize = access.byteSize();
  bool isSigned = Scalar::isSignedIntType(access.type());
  bool isFloat = Scalar::isFloatingType(access.type());

  BaseIndex address(memoryBase, ptr, TimesOne);
  if (IsUnaligned(access)) {
    MOZ_ASSERT(tmp != InvalidReg);
    if (isFloat) {
      if (byteSize == 4) {
        return asMasm().storeUnalignedFloat32(access, value.fpu(), tmp,
                                              address);
      }
      return asMasm().storeUnalignedDouble(access, value.fpu(), tmp, address);
    }
    return asMasm().ma_store_unaligned(access, value.gpr(), address, tmp,
                                       static_cast<LoadStoreSize>(8 * byteSize),
                                       isSigned ? SignExtend : ZeroExtend);
  }

  asMasm().memoryBarrierBefore(access.sync());
  // Only the last emitted instruction is a memory access.
  FaultingCodeRange fcr;
  if (isFloat) {
    if (byteSize == 4) {
      fcr = asMasm().ma_ss(value.fpu(), address);
    } else {
      fcr = asMasm().ma_sd(value.fpu(), address);
    }
  } else {
    fcr = asMasm().ma_store(value.gpr(), address,
                            static_cast<LoadStoreSize>(8 * byteSize),
                            isSigned ? SignExtend : ZeroExtend);
  }
  asMasm().appendAndVerify(
      access, wasm::TrapMachineInsnForStore(Scalar::byteSize(access.type())),
      fcr);
  asMasm().memoryBarrierAfter(access.sync());
  return fcr;
}

void MacroAssembler::enterFakeExitFrameForWasm(Register cxreg, Register scratch,
                                               ExitFrameType type) {
  enterFakeExitFrame(cxreg, scratch, type);
}

CodeOffset MacroAssembler::sub32FromMemAndBranchIfNegativeWithPatch(
    Address address, Label* label) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(scratch != address.base);
  ma_load(scratch, address);
  // mips doesn't have imm subtract insn, instead we use addiu rs, rt, -imm.
  // 128 is arbitrary, but makes `*address` count upwards, which may help
  // to identify cases where the subsequent ::patch..() call was forgotten.
  as_addiu(scratch, scratch, 128);
  // Points immediately after the insn to patch
  CodeOffset patchPoint = CodeOffset(currentOffset());
  ma_store(scratch, address);
  ma_b(scratch, scratch, label, Assembler::Signed);
  return patchPoint;
}

void MacroAssembler::patchSub32FromMemAndBranchIfNegative(CodeOffset offset,
                                                          Imm32 imm) {
  int32_t val = imm.value;
  // Patching it to zero would make the insn pointless
  MOZ_RELEASE_ASSERT(val >= 1 && val <= 127);
  InstImm* inst = (InstImm*)m_buffer.getInst(BufferOffset(offset.offset() - 4));
  // mips doesn't have imm subtract insn, instead we use addiu rs, rt, -imm.
  // 31     25 20 15
  // |      |  |  |
  // 001001 rs rt imm = addiu rs, rt, imm
  MOZ_ASSERT(inst->extractOpcode() == ((uint32_t)op_addiu >> OpcodeShift));
  inst->setImm16(Imm16(-val & 0xffff));
}

// ========================================================================
// Primitive atomic operations.

template <typename T>
static FaultingCodeRange CompareExchange(
    MacroAssembler& masm, const wasm::MemoryAccessDesc* access,
    Scalar::Type type, Synchronization sync, const T& mem, Register oldval,
    Register newval, Register valueTemp, Register offsetTemp, Register maskTemp,
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

  Label again, end;

  Register scratch2 = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch2);

  if (nbytes == 4) {
    Register scratch = temps.Acquire();

    masm.memoryBarrierBefore(sync);
    masm.bind(&again);

    FaultingCodeRange fcr(masm.currentOffset());
    masm.as_ll(output, scratch2, 0);
    masm.ma_b(output, oldval, &end, Assembler::NotEqual, ShortJump);
    masm.ma_move(scratch, newval);
    masm.as_sc(scratch, scratch2, 0);
    masm.ma_b(scratch, scratch, &again, Assembler::Zero, ShortJump);

    if (access) {
      masm.appendAndVerify(*access, wasm::TrapMachineInsn::Load32, fcr);
    }

    masm.memoryBarrierAfter(sync);
    masm.bind(&end);

    return fcr;
  }

  masm.as_andi(offsetTemp, scratch2, 3);
  masm.subPtr(offsetTemp, scratch2);
  if constexpr (std::endian::native != std::endian::little) {
    masm.as_xori(offsetTemp, offsetTemp, 3);
  }
  masm.as_sll(offsetTemp, offsetTemp, 3);
  masm.ma_li(maskTemp, Imm32(UINT32_MAX >> ((4 - nbytes) * 8)));
  masm.as_sllv(maskTemp, maskTemp, offsetTemp);
  masm.as_nor(maskTemp, zero, maskTemp);

  masm.memoryBarrierBefore(sync);

  masm.bind(&again);

  FaultingCodeRange fcr(masm.currentOffset());
  Register scratch = temps.Acquire();
  masm.as_ll(scratch, scratch2, 0);
  masm.as_srlv(output, scratch, offsetTemp);

  if (access) {
    masm.appendAndVerify(*access, wasm::TrapMachineInsn::Load32, fcr);
  }

  switch (nbytes) {
    case 1:
      if (signExtend) {
        masm.ma_seb(valueTemp, oldval);
        masm.ma_seb(output, output);
      } else {
        masm.as_andi(valueTemp, oldval, 0xff);
        masm.as_andi(output, output, 0xff);
      }
      break;
    case 2:
      if (signExtend) {
        masm.ma_seh(valueTemp, oldval);
        masm.ma_seh(output, output);
      } else {
        masm.as_andi(valueTemp, oldval, 0xffff);
        masm.as_andi(output, output, 0xffff);
      }
      break;
  }

  masm.ma_b(output, valueTemp, &end, Assembler::NotEqual, ShortJump);

  // truncate newval for 8-bit and 16-bit cmpxchg
  switch (nbytes) {
    case 1:
      masm.as_andi(valueTemp, newval, 0xff);
      break;
    case 2:
      masm.as_andi(valueTemp, newval, 0xffff);
      break;
  }

  masm.as_sllv(valueTemp, valueTemp, offsetTemp);
  masm.as_and(scratch, scratch, maskTemp);
  masm.as_or(scratch, scratch, valueTemp);

  masm.as_sc(scratch, scratch2, 0);

  masm.ma_b(scratch, scratch, &again, Assembler::Zero, ShortJump);

  masm.memoryBarrierAfter(sync);

  masm.bind(&end);

  return fcr;
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

FaultingCodeRange MacroAssembler::wasmCompareExchange(
    const wasm::MemoryAccessDesc& access, const Address& mem, Register oldval,
    Register newval, Register valueTemp, Register offsetTemp, Register maskTemp,
    Register output) {
  return CompareExchange(*this, &access, access.type(), access.sync(), mem,
                         oldval, newval, valueTemp, offsetTemp, maskTemp,
                         output);
}

FaultingCodeRange MacroAssembler::wasmCompareExchange(
    const wasm::MemoryAccessDesc& access, const BaseIndex& mem, Register oldval,
    Register newval, Register valueTemp, Register offsetTemp, Register maskTemp,
    Register output) {
  return CompareExchange(*this, &access, access.type(), access.sync(), mem,
                         oldval, newval, valueTemp, offsetTemp, maskTemp,
                         output);
}

template <typename T>
static FaultingCodeRange AtomicExchange(MacroAssembler& masm,
                                        const wasm::MemoryAccessDesc* access,
                                        Scalar::Type type, Synchronization sync,
                                        const T& mem, Register value,
                                        Register valueTemp, Register offsetTemp,
                                        Register maskTemp, Register output) {
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

  UseScratchRegisterScope temps(masm);
  Register scratch2 = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch2);

  if (nbytes == 4) {
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.Acquire();
    masm.memoryBarrierBefore(sync);
    masm.bind(&again);

    FaultingCodeRange fcr(masm.currentOffset());
    masm.as_ll(output, scratch2, 0);
    if (access) {
      masm.appendAndVerify(*access, wasm::TrapMachineInsn::Load32, fcr);
    }
    masm.ma_move(scratch, value);
    masm.as_sc(scratch, scratch2, 0);
    masm.ma_b(scratch, scratch, &again, Assembler::Zero, ShortJump);

    masm.memoryBarrierAfter(sync);

    return fcr;
  }

  masm.as_andi(offsetTemp, scratch2, 3);
  masm.subPtr(offsetTemp, scratch2);
  if constexpr (std::endian::native != std::endian::little) {
    masm.as_xori(offsetTemp, offsetTemp, 3);
  }
  masm.as_sll(offsetTemp, offsetTemp, 3);
  masm.ma_li(maskTemp, Imm32(UINT32_MAX >> ((4 - nbytes) * 8)));
  masm.as_sllv(maskTemp, maskTemp, offsetTemp);
  masm.as_nor(maskTemp, zero, maskTemp);
  switch (nbytes) {
    case 1:
      masm.as_andi(valueTemp, value, 0xff);
      break;
    case 2:
      masm.as_andi(valueTemp, value, 0xffff);
      break;
  }
  masm.as_sllv(valueTemp, valueTemp, offsetTemp);

  masm.memoryBarrierBefore(sync);

  masm.bind(&again);

  FaultingCodeRange fcr(masm.currentOffset());
  Register scratch = temps.Acquire();
  masm.as_ll(output, scratch2, 0);
  if (access) {
    masm.appendAndVerify(*access, wasm::TrapMachineInsn::Load32, fcr);
  }

  masm.as_and(scratch, output, maskTemp);
  masm.as_or(scratch, scratch, valueTemp);

  masm.as_sc(scratch, scratch2, 0);

  masm.ma_b(scratch, scratch, &again, Assembler::Zero, ShortJump);

  masm.as_srlv(output, output, offsetTemp);

  switch (nbytes) {
    case 1:
      if (signExtend) {
        masm.ma_seb(output, output);
      } else {
        masm.as_andi(output, output, 0xff);
      }
      break;
    case 2:
      if (signExtend) {
        masm.ma_seh(output, output);
      } else {
        masm.as_andi(output, output, 0xffff);
      }
      break;
  }

  masm.memoryBarrierAfter(sync);

  return fcr;
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

FaultingCodeRange MacroAssembler::wasmAtomicExchange(
    const wasm::MemoryAccessDesc& access, const Address& mem, Register value,
    Register valueTemp, Register offsetTemp, Register maskTemp,
    Register output) {
  return AtomicExchange(*this, &access, access.type(), access.sync(), mem,
                        value, valueTemp, offsetTemp, maskTemp, output);
}

FaultingCodeRange MacroAssembler::wasmAtomicExchange(
    const wasm::MemoryAccessDesc& access, const BaseIndex& mem, Register value,
    Register valueTemp, Register offsetTemp, Register maskTemp,
    Register output) {
  return AtomicExchange(*this, &access, access.type(), access.sync(), mem,
                        value, valueTemp, offsetTemp, maskTemp, output);
}

template <typename T>
static FaultingCodeRange AtomicFetchOp(MacroAssembler& masm,
                                       const wasm::MemoryAccessDesc* access,
                                       Scalar::Type type, Synchronization sync,
                                       AtomicOp op, const T& mem,
                                       Register value, Register valueTemp,
                                       Register offsetTemp, Register maskTemp,
                                       Register output) {
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

  UseScratchRegisterScope temps(masm);
  Register scratch2 = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch2);

  if (nbytes == 4) {
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.Acquire();
    masm.memoryBarrierBefore(sync);
    masm.bind(&again);

    FaultingCodeRange fcr(masm.currentOffset());
    masm.as_ll(output, scratch2, 0);
    if (access) {
      masm.appendAndVerify(*access, wasm::TrapMachineInsn::Load32, fcr);
    }

    switch (op) {
      case AtomicOp::Add:
        masm.as_addu(scratch, output, value);
        break;
      case AtomicOp::Sub:
        masm.as_subu(scratch, output, value);
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

    masm.as_sc(scratch, scratch2, 0);
    masm.ma_b(scratch, scratch, &again, Assembler::Zero, ShortJump);

    masm.memoryBarrierAfter(sync);

    return fcr;
  }

  masm.as_andi(offsetTemp, scratch2, 3);
  masm.subPtr(offsetTemp, scratch2);
  if constexpr (std::endian::native != std::endian::little) {
    masm.as_xori(offsetTemp, offsetTemp, 3);
  }
  masm.as_sll(offsetTemp, offsetTemp, 3);
  masm.ma_li(maskTemp, Imm32(UINT32_MAX >> ((4 - nbytes) * 8)));
  masm.as_sllv(maskTemp, maskTemp, offsetTemp);
  masm.as_nor(maskTemp, zero, maskTemp);

  masm.memoryBarrierBefore(sync);

  masm.bind(&again);

  FaultingCodeRange fcr(masm.currentOffset());
  Register scratch = temps.Acquire();
  masm.as_ll(scratch, scratch2, 0);
  masm.as_srlv(output, scratch, offsetTemp);
  if (access) {
    masm.appendAndVerify(*access, wasm::TrapMachineInsn::Load32, fcr);
  }
  switch (op) {
    case AtomicOp::Add:
      masm.as_addu(valueTemp, output, value);
      break;
    case AtomicOp::Sub:
      masm.as_subu(valueTemp, output, value);
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
      masm.as_andi(valueTemp, valueTemp, 0xffff);
      break;
  }

  masm.as_sllv(valueTemp, valueTemp, offsetTemp);

  masm.as_and(scratch, scratch, maskTemp);
  masm.as_or(scratch, scratch, valueTemp);

  masm.as_sc(scratch, scratch2, 0);

  masm.ma_b(scratch, scratch, &again, Assembler::Zero, ShortJump);

  switch (nbytes) {
    case 1:
      if (signExtend) {
        masm.ma_seb(output, output);
      } else {
        masm.as_andi(output, output, 0xff);
      }
      break;
    case 2:
      if (signExtend) {
        masm.ma_seh(output, output);
      } else {
        masm.as_andi(output, output, 0xffff);
      }
      break;
  }

  masm.memoryBarrierAfter(sync);

  return fcr;
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

FaultingCodeRange MacroAssembler::wasmAtomicFetchOp(
    const wasm::MemoryAccessDesc& access, AtomicOp op, Register value,
    const Address& mem, Register valueTemp, Register offsetTemp,
    Register maskTemp, Register output) {
  return AtomicFetchOp(*this, &access, access.type(), access.sync(), op, mem,
                       value, valueTemp, offsetTemp, maskTemp, output);
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

  UseScratchRegisterScope temps(masm);
  Register scratch2 = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch2);

  if (nbytes == 4) {
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.Acquire();
    masm.memoryBarrierBefore(sync);
    masm.bind(&again);

    FaultingCodeRange fcr(masm.currentOffset());
    masm.as_ll(scratch, scratch2, 0);

    if (access) {
      masm.appendAndVerify(*access, wasm::TrapMachineInsn::Load32, fcr);
    }

    switch (op) {
      case AtomicOp::Add:
        masm.as_addu(scratch, scratch, value);
        break;
      case AtomicOp::Sub:
        masm.as_subu(scratch, scratch, value);
        break;
      case AtomicOp::And:
        masm.as_and(scratch, scratch, value);
        break;
      case AtomicOp::Or:
        masm.as_or(scratch, scratch, value);
        break;
      case AtomicOp::Xor:
        masm.as_xor(scratch, scratch, value);
        break;
      default:
        MOZ_CRASH();
    }

    masm.as_sc(scratch, scratch2, 0);
    masm.ma_b(scratch, scratch, &again, Assembler::Zero, ShortJump);

    masm.memoryBarrierAfter(sync);

    return;
  }

  masm.as_andi(offsetTemp, scratch2, 3);
  masm.subPtr(offsetTemp, scratch2);
  if constexpr (std::endian::native != std::endian::little) {
    masm.as_xori(offsetTemp, offsetTemp, 3);
  }
  masm.as_sll(offsetTemp, offsetTemp, 3);
  masm.ma_li(maskTemp, Imm32(UINT32_MAX >> ((4 - nbytes) * 8)));
  masm.as_sllv(maskTemp, maskTemp, offsetTemp);
  masm.as_nor(maskTemp, zero, maskTemp);

  masm.memoryBarrierBefore(sync);

  masm.bind(&again);

  FaultingCodeRange fcr(masm.currentOffset());
  Register scratch = temps.Acquire();
  masm.as_ll(scratch, scratch2, 0);
  masm.as_srlv(valueTemp, scratch, offsetTemp);

  if (access) {
    masm.appendAndVerify(*access, wasm::TrapMachineInsn::Load32, fcr);
  }

  switch (op) {
    case AtomicOp::Add:
      masm.as_addu(valueTemp, valueTemp, value);
      break;
    case AtomicOp::Sub:
      masm.as_subu(valueTemp, valueTemp, value);
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
      masm.as_andi(valueTemp, valueTemp, 0xffff);
      break;
  }

  masm.as_sllv(valueTemp, valueTemp, offsetTemp);

  masm.as_and(scratch, scratch, maskTemp);
  masm.as_or(scratch, scratch, valueTemp);

  masm.as_sc(scratch, scratch2, 0);

  masm.ma_b(scratch, scratch, &again, Assembler::Zero, ShortJump);

  masm.memoryBarrierAfter(sync);
}

void MacroAssembler::atomicEffectOp(Scalar::Type arrayType,
                                    Synchronization sync, AtomicOp op,
                                    Register value, const BaseIndex& mem,
                                    Register valueTemp, Register offsetTemp,
                                    Register maskTemp) {
  AtomicEffectOp(*this, nullptr, arrayType, sync, op, mem, value, valueTemp,
                 offsetTemp, maskTemp);
}

void MacroAssembler::atomicEffectOp(Scalar::Type arrayType,
                                    Synchronization sync, AtomicOp op,
                                    Register value, const Address& mem,
                                    Register valueTemp, Register offsetTemp,
                                    Register maskTemp) {
  AtomicEffectOp(*this, nullptr, arrayType, sync, op, mem, value, valueTemp,
                 offsetTemp, maskTemp);
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

// ========================================================================
// JS atomic operations.

void MacroAssembler::atomicPause() { as_sync(); }

// ===============================================================
// Arithmetic functions

void MacroAssembler::flexibleQuotient32(Register lhs, Register rhs,
                                        Register dest, bool isUnsigned,
                                        const LiveRegisterSet&) {
  quotient32(lhs, rhs, dest, isUnsigned);
}

void MacroAssembler::flexibleRemainder32(Register lhs, Register rhs,
                                         Register dest, bool isUnsigned,
                                         const LiveRegisterSet&) {
  remainder32(lhs, rhs, dest, isUnsigned);
}

void MacroAssembler::flexibleDivMod32(Register lhs, Register rhs,
                                      Register divOutput, Register remOutput,
                                      bool isUnsigned, const LiveRegisterSet&) {
  MOZ_ASSERT(lhs != divOutput && lhs != remOutput, "lhs is preserved");
  MOZ_ASSERT(rhs != divOutput && rhs != remOutput, "rhs is preserved");

#ifdef MIPSR6
  if (isUnsigned) {
    as_divu(divOutput, lhs, rhs);
    as_modu(remOutput, rhs, rhs);
  } else {
    as_div(divOutput, lhs, rhs);
    as_mod(remOutput, lhs, rhs);
  }
#else
  if (isUnsigned) {
    as_divu(lhs, rhs);
  } else {
    as_div(lhs, rhs);
  }
  as_mfhi(remOutput);
  as_mflo(divOutput);
#endif
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
  ScratchFloat32Scope fscratch(*this);

  // Round toward negative infinity.
  as_floorls(fscratch, src);
  moveFromDouble(fscratch, dest);

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
    ma_srl(dest, dest, Imm32(30));
    branch32(Assembler::NotEqual, dest, zero, fail);
  }
  bind(&notZero);
}

void MacroAssembler::floorDoubleToInt32(FloatRegister src, Register dest,
                                        Label* fail) {
  ScratchDoubleScope dscratch(*this);

  // Round toward negative infinity.
  as_floorld(dscratch, src);
  moveFromDouble(dscratch, dest);

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
    moveFromDouble(src, dest);
    ma_dsrl(dest, dest, Imm32(62));
    branchPtr(Assembler::NotEqual, dest, zero, fail);
  }
  bind(&notZero);
}

void MacroAssembler::ceilFloat32ToInt32(FloatRegister src, Register dest,
                                        Label* fail) {
  ScratchFloat32Scope fscratch(*this);

  // Round toward positive infinity.
  as_ceills(fscratch, src);
  moveFromDouble(fscratch, dest);

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
  ScratchDoubleScope dscratch(*this);

  // Round toward positive infinity.
  as_ceilld(dscratch, src);
  moveFromDouble(dscratch, dest);

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
  ScratchFloat32Scope fscratch(*this);

  Label negative, end, performRound;

  // Branch for negative inputs. Doesn't catch NaN or -0.
  loadConstantFloat32(0.0f, fscratch);
  ma_bc1s(src, fscratch, &negative, Assembler::DoubleLessThan, ShortJump);

  // If non-negative check for bailout.
  ma_bc1s(src, fscratch, &performRound, Assembler::DoubleNotEqual, ShortJump);
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
    loadConstantFloat32(-0.5f, fscratch);
    branchFloat(Assembler::DoubleGreaterThanOrEqual, src, fscratch, fail);
  }

  bind(&performRound);
  {
    // Load biggest number less than 0.5 in the temp register.
    loadConstantFloat32(GetBiggestNumberLessThan(0.5f), temp);

    // Other inputs need the biggest float less than 0.5 added.
    as_adds(fscratch, src, temp);

    // Round toward negative infinity.
    as_floorls(fscratch, fscratch);
    moveFromDouble(fscratch, dest);

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
  ScratchDoubleScope dscratch(*this);

  Label negative, end, performRound;

  // Branch for negative inputs. Doesn't catch NaN or -0.
  loadConstantDouble(0.0, dscratch);
  ma_bc1d(src, dscratch, &negative, Assembler::DoubleLessThan, ShortJump);

  // If non-negative check for bailout.
  ma_bc1d(src, dscratch, &performRound, Assembler::DoubleNotEqual, ShortJump);
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
    loadConstantDouble(-0.5, dscratch);
    branchDouble(Assembler::DoubleGreaterThanOrEqual, src, dscratch, fail);
  }

  bind(&performRound);
  {
    // Load biggest number less than 0.5 in the temp register.
    loadConstantDouble(GetBiggestNumberLessThan(0.5), temp);

    // Other inputs need the biggest double less than 0.5 added.
    as_addd(dscratch, src, temp);

    // Round toward negative infinity.
    as_floorld(dscratch, dscratch);
    moveFromDouble(dscratch, dest);

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
  ScratchFloat32Scope fscratch(*this);

  // Round toward zero.
  as_truncls(fscratch, src);
  moveFromDouble(fscratch, dest);

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
    ma_srl(dest, dest, Imm32(30));
    branch32(Assembler::NotEqual, dest, zero, fail);
  }
  bind(&notZero);
}

void MacroAssembler::truncDoubleToInt32(FloatRegister src, Register dest,
                                        Label* fail) {
  ScratchDoubleScope dscratch(*this);

  // Round toward zero.
  as_truncld(dscratch, src);
  moveFromDouble(dscratch, dest);

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
    ma_dsrl(dest, dest, Imm32(62));
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
  UseScratchRegisterScope temps(*this);
  Register lhsi = temps.Acquire();
  Register rhsi = temps.Acquire();

  moveFromDouble(lhs, lhsi);
  moveFromDouble(rhs, rhsi);

  // Combine.
  if (hasR2()) {
    ma_dins(rhsi, lhsi, Imm32(0), Imm32(63));
  } else {
    ma_dext(lhsi, lhsi, Imm32(0), Imm32(63));
    ma_dsrl(rhsi, rhsi, Imm32(63));
    ma_dsll(rhsi, rhsi, Imm32(63));
    as_or(rhsi, rhsi, lhsi);
  }
  moveToDouble(rhsi, output);
}

void MacroAssembler::copySignFloat32(FloatRegister lhs, FloatRegister rhs,
                                     FloatRegister output) {
  UseScratchRegisterScope temps(*this);
  Register lhsi = temps.Acquire();
  Register rhsi = temps.Acquire();

  moveFromFloat32(lhs, lhsi);
  moveFromFloat32(rhs, rhsi);

  // Combine.
  if (hasR2()) {
    ma_ins(rhsi, lhsi, 0, 31);
  } else {
    ma_ext(lhsi, lhsi, 0, 31);
    ma_srl(rhsi, rhsi, Imm32(31));
    ma_sll(rhsi, rhsi, Imm32(31));
    as_or(rhsi, rhsi, lhsi);
  }
  moveToFloat32(rhsi, output);
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

//}}} check_macroassembler_style
