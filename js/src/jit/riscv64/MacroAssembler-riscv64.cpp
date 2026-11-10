/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "jit/riscv64/MacroAssembler-riscv64.h"

#include "mozilla/Casting.h"

#include <bit>
#include <cstdint>

#include "jit/Bailouts.h"
#include "jit/BaselineFrame.h"
#include "jit/JitFrames.h"
#include "jit/JitRuntime.h"
#include "jit/MacroAssembler.h"
#include "jit/MoveEmitter.h"
#include "util/Memory.h"
#include "util/PortableMath.h"
#include "vm/JitActivation.h"  // jit::JitActivation
#include "vm/JSContext.h"
#include "wasm/WasmStubs.h"

#include "jit/MacroAssembler-inl.h"

namespace js {
namespace jit {

MacroAssembler& MacroAssemblerRiscv64::asMasm() {
  return *static_cast<MacroAssembler*>(this);
}

const MacroAssembler& MacroAssemblerRiscv64::asMasm() const {
  return *static_cast<const MacroAssembler*>(this);
}

void MacroAssemblerRiscv64::ma_cmp_set(Register dst, Register lhs, ImmWord imm,
                                       Condition c) {
  if (is_int32(imm.value)) {
    ma_cmp_set(dst, lhs, Imm32(int32_t(imm.value)), c);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    ma_cmp_set(dst, lhs, scratch, c);
  }
}

void MacroAssemblerRiscv64::ma_cmp_set(Register dst, Register lhs, ImmPtr imm,
                                       Condition c) {
  ma_cmp_set(dst, lhs, ImmWord(uintptr_t(imm.value)), c);
}

void MacroAssemblerRiscv64::ma_cmp_set(Register dst, Register lhs, ImmGCPtr imm,
                                       Condition c) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  ma_li(scratch, imm);
  ma_cmp_set(dst, lhs, scratch, c);
}

void MacroAssemblerRiscv64::ma_cmp_set(Register dst, Address address,
                                       Register rhs, Condition c) {
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();
  ma_load(scratch2, address, SizeDouble);
  ma_cmp_set(dst, scratch2, rhs, c);
}

void MacroAssemblerRiscv64::ma_cmp_set(Register dst, Address address, Imm32 imm,
                                       Condition c) {
  // TODO(riscv): 32-bit ma_cmp_set?
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();
  ma_load(scratch2, address, SizeWord);
  ma_cmp_set(dst, scratch2, imm, c);
}

void MacroAssemblerRiscv64::ma_cmp_set(Register dst, Address address,
                                       ImmWord imm, Condition c) {
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();
  ma_load(scratch2, address, SizeDouble);
  ma_cmp_set(dst, scratch2, imm, c);
}

void MacroAssemblerRiscv64::ma_cmp_set(Register dst, Register lhs, Imm32 imm,
                                       Condition c) {
  if (imm.value == 0) {
    switch (c) {
      case Equal:
      case BelowOrEqual:
        seqz(dst, lhs);
        break;
      case NotEqual:
      case Above:
        snez(dst, lhs);
        break;
      case AboveOrEqual:
      case Below:
        ori(dst, zero, c == AboveOrEqual ? 1 : 0);
        break;
      case GreaterThan:
      case LessThanOrEqual:
        sgtz(dst, lhs);
        if (c == LessThanOrEqual) {
          NegateBool(dst, dst);
        }
        break;
      case LessThan:
      case GreaterThanOrEqual:
        sltz(dst, lhs);
        if (c == GreaterThanOrEqual) {
          NegateBool(dst, dst);
        }
        break;
      case Zero:
        seqz(dst, lhs);
        break;
      case NonZero:
        snez(dst, lhs);
        break;
      case Signed:
        sltz(dst, lhs);
        break;
      case NotSigned:
        sltz(dst, lhs);
        NegateBool(dst, dst);
        break;
      default:
        MOZ_CRASH("Invalid condition.");
    }
    return;
  }

  switch (c) {
    case Equal:
    case NotEqual:
      ma_xor(dst, lhs, imm);
      if (c == Equal) {
        seqz(dst, dst);
      } else {
        snez(dst, dst);
      }
      break;
    case Above: {
      if (imm.value == -1) {
        // Always false.
        ma_mv(dst, zero);
      } else if (imm.value == INT32_MAX) {
        // True iff any bit in lhs[63:31] is set.
        srli(dst, lhs, 31);
        snez(dst, dst);
      } else if (is_int12(imm.value + 1)) {
        // lhs > rhs via not(lhs < rhs + 1) if rhs + 1 does not overflow.
        sltiu(dst, lhs, imm.value + 1);
        NegateBool(dst, dst);
      } else {
        // lhs > rhs via rhs < lhs
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();

        ma_li(scratch, imm);
        sltu(dst, scratch, lhs);
      }
      break;
    }
    case BelowOrEqual: {
      if (imm.value == -1) {
        // Always true.
        ma_li(dst, Imm32(1));
      } else if (imm.value == INT32_MAX) {
        // True iff no bit in lhs[63:31] is set.
        srli(dst, lhs, 31);
        seqz(dst, dst);
      } else if (is_int12(imm.value + 1)) {
        // lhs <= rhs via lhs < rhs + 1 if rhs + 1 does not overflow.
        sltiu(dst, lhs, imm.value + 1);
      } else {
        // lhs <= rhs via lhs < rhs + 1 if rhs + 1 does not overflow.
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();

        ma_li(scratch, Imm32(imm.value + 1));
        sltu(dst, lhs, scratch);
      }
      break;
    }
    case AboveOrEqual: {
      if (is_int12(imm.value)) {
        // lhs >= rhs via not(lhs < rhs).
        sltiu(dst, lhs, imm.value);
        NegateBool(dst, dst);
      } else if (imm.value == INT32_MIN) {
        // True iff any bit in lhs[63:31] is set.
        srli(dst, lhs, 31);
        snez(dst, dst);
      } else {
        // lhs >= rhs via (rhs - 1 < lhs) if rhs - 1 does not overflow.
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();

        ma_li(scratch, Imm32(imm.value - 1));
        sltu(dst, scratch, lhs);
      }
      break;
    }
    case Below: {
      if (is_int12(imm.value)) {
        sltiu(dst, lhs, imm.value);
      } else {
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();

        ma_li(scratch, imm);
        sltu(dst, lhs, scratch);
      }
      break;
    }
    case GreaterThan: {
      if (imm.value != INT32_MAX && is_int12(imm.value + 1)) {
        // lhs > rhs via not(lhs < rhs + 1) if rhs + 1 does not overflow.
        slti(dst, lhs, imm.value + 1);
        NegateBool(dst, dst);
      } else {
        // lhs > rhs via rhs < lhs
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();

        ma_li(scratch, imm);
        slt(dst, scratch, lhs);
      }
      break;
    }
    case LessThanOrEqual: {
      if (imm.value != INT32_MAX && is_int12(imm.value + 1)) {
        // lhs <= rhs via lhs < rhs + 1 if rhs + 1 does not overflow.
        slti(dst, lhs, imm.value + 1);
      } else {
        // lhs <= rhs via lhs < rhs + 1.
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();

        ma_li(scratch, Imm64(int64_t(imm.value) + 1));
        slt(dst, lhs, scratch);
      }
      break;
    }
    case GreaterThanOrEqual: {
      if (is_int12(imm.value)) {
        // lhs >= rhs via not(lhs < rhs).
        slti(dst, lhs, imm.value);
        NegateBool(dst, dst);
      } else {
        // lhs >= rhs via (rhs - 1 < lhs).
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();

        ma_li(scratch, Imm64(int64_t(imm.value) - 1));
        slt(dst, scratch, lhs);
      }
      break;
    }
    case LessThan: {
      if (is_int12(imm.value)) {
        slti(dst, lhs, imm.value);
      } else {
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();

        ma_li(scratch, imm);
        slt(dst, lhs, scratch);
      }
      break;
    }
    default:
      MOZ_CRASH("Invalid condition.");
  }
}

void MacroAssemblerRiscv64::ma_cmp_set(Register dst, Register lhs, Register rhs,
                                       Condition c) {
  switch (c) {
    case Equal:
      // seq d,s,t =>
      //   xor d,s,t
      //   seqz d,d
      xor_(dst, lhs, rhs);
      seqz(dst, dst);
      break;
    case NotEqual:
      // sne d,s,t =>
      //   xor d,s,t
      //   snez d,d
      xor_(dst, lhs, rhs);
      snez(dst, dst);
      break;
    case Above:
      // sgtu d,s,t =>
      //   sltu d,t,s
      sltu(dst, rhs, lhs);
      break;
    case AboveOrEqual:
      // sgeu d,s,t =>
      //   sltu d,s,t
      //   xori d,d,1
      sltu(dst, lhs, rhs);
      NegateBool(dst, dst);
      break;
    case Below:
      // sltu d,s,t
      sltu(dst, lhs, rhs);
      break;
    case BelowOrEqual:
      // sleu d,s,t =>
      //   sltu d,t,s
      //   xori d,d,1
      sltu(dst, rhs, lhs);
      NegateBool(dst, dst);
      break;
    case GreaterThan:
      // sgt d,s,t =>
      //   slt d,t,s
      slt(dst, rhs, lhs);
      break;
    case GreaterThanOrEqual:
      // sge d,s,t =>
      //   slt d,s,t
      //   xori d,d,1
      slt(dst, lhs, rhs);
      NegateBool(dst, dst);
      break;
    case LessThan:
      // slt d,s,t
      slt(dst, lhs, rhs);
      break;
    case LessThanOrEqual:
      // sle d,s,t =>
      //   slt d,t,s
      //   xori d,d,1
      slt(dst, rhs, lhs);
      NegateBool(dst, dst);
      break;
    case Zero:
      MOZ_ASSERT(lhs == rhs);
      // seq d,s,$zero =>
      //   seqz d,s
      seqz(dst, lhs);
      break;
    case NonZero:
      MOZ_ASSERT(lhs == rhs);
      // sne d,s,$zero =>
      //   snez d,s
      snez(dst, lhs);
      break;
    case Signed:
      MOZ_ASSERT(lhs == rhs);
      sltz(dst, lhs);
      break;
    case NotSigned:
      MOZ_ASSERT(lhs == rhs);
      // sge d,s,$zero =>
      //   sltz d,s
      //   xori d,d,1
      sltz(dst, lhs);
      NegateBool(dst, dst);
      break;
    default:
      MOZ_CRASH("Invalid condition.");
  }
}

void MacroAssemblerRiscv64::ma_cmp_mv(Register dst, Register lhs, Register rhs,
                                      Register src, Condition c) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  ma_cmp_set(scratch, lhs, rhs, c);
  ma_cselnz(dst, src, dst, scratch, scratch);
}

void MacroAssemblerRiscv64::ma_cmp_mv(Register dst, Register lhs, Imm32 rhs,
                                      Register src, Condition c) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  ma_cmp_set(scratch, lhs, rhs, c);
  ma_cselnz(dst, src, dst, scratch, scratch);
}

void MacroAssemblerRiscv64::ma_cselz(Register rd, Register rs1, Register rs2,
                                     Register rc, Register rtmp) {
  MOZ_ASSERT(rd != rtmp);

  // From
  // <https://riscv.github.io/riscv-isa-manual/snapshot/spec/#_instruction_sequences>:
  //
  // Conditional select, if zero
  // rd = (rc == 0) ? rs1 : rs2
  //
  // czero.nez  rd, rs1, rc
  // czero.eqz  rtmp, rs2, rc
  // add        rd, rd, rtmp

  if (rs1 == rs2) {
    if (rd != rs1) {
      ma_mv(rd, rs1);
    }
    return;
  }

  if (HasZicondExtension()) {
    if (rd == rc) {
      if (rs1 != rtmp) {
        czero_eqz(rtmp, rs2, rc);
        czero_nez(rd, rs1, rc);
      } else {
        czero_nez(rtmp, rs1, rc);
        czero_eqz(rd, rs2, rc);
      }
    } else {
      if (rd == rs2) {
        czero_eqz(rd, rs2, rc);
        czero_nez(rtmp, rs1, rc);
      } else {
        czero_nez(rd, rs1, rc);
        czero_eqz(rtmp, rs2, rc);
      }
    }
    add(rd, rd, rtmp);
  } else {
    if (rd == rs1) {
      Label done;
      ma_b(rc, rc, &done, Zero, ShortJump);
      ma_mv(rd, rs2);
      bind(&done);
    } else if (rd == rs2) {
      Label done;
      ma_b(rc, rc, &done, NonZero, ShortJump);
      ma_mv(rd, rs1);
      bind(&done);
    } else if (rd == rc) {
      Label done;
      if (rtmp == rs1) {
        ma_b(rc, rc, &done, Zero, ShortJump);
        ma_mv(rtmp, rs2);
      } else if (rtmp == rs2) {
        ma_b(rc, rc, &done, NonZero, ShortJump);
        ma_mv(rtmp, rs1);
      } else {
        ma_mv(rtmp, rs1);
        ma_b(rc, rc, &done, Zero, ShortJump);
        ma_mv(rtmp, rs2);
      }
      bind(&done);
      ma_mv(rd, rtmp);
    } else {
      ma_mv(rd, rs1);
      Label done;
      ma_b(rc, rc, &done, Zero, ShortJump);
      ma_mv(rd, rs2);
      bind(&done);
    }
  }
}

void MacroAssemblerRiscv64::ma_cselnz(Register rd, Register rs1, Register rs2,
                                      Register rc, Register rtmp) {
  MOZ_ASSERT(rd != rtmp);

  // From
  // <https://riscv.github.io/riscv-isa-manual/snapshot/spec/#_instruction_sequences>:
  //
  // Conditional select, if non-zero
  // rd = (rc != 0) ? rs1 : rs2
  //
  // czero.eqz  rd, rs1, rc
  // czero.nez  rtmp, rs2, rc
  // add        rd, rd, rtmp

  if (rs1 == rs2) {
    if (rd != rs1) {
      ma_mv(rd, rs1);
    }
    return;
  }

  if (HasZicondExtension()) {
    if (rd == rc) {
      if (rs1 != rtmp) {
        czero_nez(rtmp, rs2, rc);
        czero_eqz(rd, rs1, rc);
      } else {
        czero_eqz(rtmp, rs1, rc);
        czero_nez(rd, rs2, rc);
      }
    } else {
      if (rd == rs2) {
        czero_nez(rd, rs2, rc);
        czero_eqz(rtmp, rs1, rc);
      } else {
        czero_eqz(rd, rs1, rc);
        czero_nez(rtmp, rs2, rc);
      }
    }
    add(rd, rd, rtmp);
  } else {
    if (rd == rs1) {
      Label done;
      ma_b(rc, rc, &done, NonZero, ShortJump);
      ma_mv(rd, rs2);
      bind(&done);
    } else if (rd == rs2) {
      Label done;
      ma_b(rc, rc, &done, Zero, ShortJump);
      ma_mv(rd, rs1);
      bind(&done);
    } else if (rd == rc) {
      Label done;
      if (rtmp == rs1) {
        ma_b(rc, rc, &done, NonZero, ShortJump);
        ma_mv(rtmp, rs2);
      } else if (rtmp == rs2) {
        ma_b(rc, rc, &done, Zero, ShortJump);
        ma_mv(rtmp, rs1);
      } else {
        ma_mv(rtmp, rs1);
        ma_b(rc, rc, &done, NonZero, ShortJump);
        ma_mv(rtmp, rs2);
      }
      bind(&done);
      ma_mv(rd, rtmp);
    } else {
      ma_mv(rd, rs1);
      Label done;
      ma_b(rc, rc, &done, NonZero, ShortJump);
      ma_mv(rd, rs2);
      bind(&done);
    }
  }
}

void MacroAssemblerRiscv64::ma_compareF32(Register rd, DoubleCondition cc,
                                          FloatRegister cmp1,
                                          FloatRegister cmp2) {
  switch (cc) {
    case DoubleEqual:
      feq_s(rd, cmp1, cmp2);
      return;
    case DoubleEqualOrUnordered: {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      flt_s(rd, cmp1, cmp2);
      flt_s(scratch, cmp2, cmp1);
      or_(rd, rd, scratch);
      NegateBool(rd, rd);
      return;
    }
    case DoubleNotEqual: {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      flt_s(rd, cmp1, cmp2);
      flt_s(scratch, cmp2, cmp1);
      or_(rd, rd, scratch);
      return;
    }
    case DoubleNotEqualOrUnordered:
      feq_s(rd, cmp1, cmp2);
      NegateBool(rd, rd);
      return;
    case DoubleLessThan:
      flt_s(rd, cmp1, cmp2);
      return;
    case DoubleLessThanOrUnordered:
      fle_s(rd, cmp2, cmp1);
      NegateBool(rd, rd);
      return;
    case DoubleGreaterThanOrEqual:
      fle_s(rd, cmp2, cmp1);
      return;
    case DoubleGreaterThanOrEqualOrUnordered:
      flt_s(rd, cmp1, cmp2);
      NegateBool(rd, rd);
      return;
    case DoubleLessThanOrEqual:
      fle_s(rd, cmp1, cmp2);
      return;
    case DoubleLessThanOrEqualOrUnordered:
      flt_s(rd, cmp2, cmp1);
      NegateBool(rd, rd);
      return;
    case DoubleGreaterThan:
      flt_s(rd, cmp2, cmp1);
      return;
    case DoubleGreaterThanOrUnordered:
      fle_s(rd, cmp1, cmp2);
      NegateBool(rd, rd);
      return;
    case DoubleOrdered:
      CompareIsNotNanF32(rd, cmp1, cmp2);
      return;
    case DoubleUnordered:
      CompareIsNanF32(rd, cmp1, cmp2);
      return;
  }
}

void MacroAssemblerRiscv64::ma_compareF64(Register rd, DoubleCondition cc,
                                          FloatRegister cmp1,
                                          FloatRegister cmp2) {
  switch (cc) {
    case DoubleEqual:
      feq_d(rd, cmp1, cmp2);
      return;
    case DoubleEqualOrUnordered: {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      flt_d(rd, cmp1, cmp2);
      flt_d(scratch, cmp2, cmp1);
      or_(rd, rd, scratch);
      NegateBool(rd, rd);
      return;
    }
    case DoubleNotEqual: {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      flt_d(rd, cmp1, cmp2);
      flt_d(scratch, cmp2, cmp1);
      or_(rd, rd, scratch);
      return;
    }
    case DoubleNotEqualOrUnordered:
      feq_d(rd, cmp1, cmp2);
      NegateBool(rd, rd);
      return;
    case DoubleLessThan:
      flt_d(rd, cmp1, cmp2);
      return;
    case DoubleLessThanOrUnordered:
      fle_d(rd, cmp2, cmp1);
      NegateBool(rd, rd);
      return;
    case DoubleGreaterThanOrEqual:
      fle_d(rd, cmp2, cmp1);
      return;
    case DoubleGreaterThanOrEqualOrUnordered:
      flt_d(rd, cmp1, cmp2);
      NegateBool(rd, rd);
      return;
    case DoubleLessThanOrEqual:
      fle_d(rd, cmp1, cmp2);
      return;
    case DoubleLessThanOrEqualOrUnordered:
      flt_d(rd, cmp2, cmp1);
      NegateBool(rd, rd);
      return;
    case DoubleGreaterThan:
      flt_d(rd, cmp2, cmp1);
      return;
    case DoubleGreaterThanOrUnordered:
      fle_d(rd, cmp1, cmp2);
      NegateBool(rd, rd);
      return;
    case DoubleOrdered:
      CompareIsNotNanF64(rd, cmp1, cmp2);
      return;
    case DoubleUnordered:
      CompareIsNanF64(rd, cmp1, cmp2);
      return;
  }
}

void MacroAssemblerRiscv64Compat::movePtr(Register src, Register dest) {
  ma_mv(dest, src);
}
void MacroAssemblerRiscv64Compat::movePtr(ImmWord imm, Register dest) {
  ma_li(dest, imm);
}

void MacroAssemblerRiscv64Compat::movePtr(ImmGCPtr imm, Register dest) {
  ma_li(dest, imm);
}

void MacroAssemblerRiscv64Compat::movePtr(ImmPtr imm, Register dest) {
  movePtr(ImmWord(uintptr_t(imm.value)), dest);
}
void MacroAssemblerRiscv64Compat::movePtr(wasm::SymbolicAddress imm,
                                          Register dest) {
  BufferOffset offset = ma_liPatchable(dest, ImmWord(-1));
  append(wasm::SymbolicAccess(CodeOffset(offset.getOffset()), imm));
}

bool MacroAssemblerRiscv64Compat::buildOOLFakeExitFrame(void* fakeReturnAddr) {
  asMasm().Push(FrameDescriptor(FrameType::IonJS));  // descriptor_
  asMasm().Push(ImmPtr(fakeReturnAddr));
  asMasm().Push(FramePointer);
  return true;
}

void MacroAssemblerRiscv64Compat::convertUInt32ToDouble(Register src,
                                                        FloatRegister dest) {
  fcvt_d_wu(dest, src);
}

void MacroAssemblerRiscv64Compat::convertUInt64ToDouble(Register src,
                                                        FloatRegister dest) {
  fcvt_d_lu(dest, src);
}

void MacroAssemblerRiscv64Compat::convertUInt32ToFloat32(Register src,
                                                         FloatRegister dest) {
  fcvt_s_wu(dest, src);
}

void MacroAssemblerRiscv64Compat::convertDoubleToFloat32(FloatRegister src,
                                                         FloatRegister dest) {
  fcvt_s_d(dest, src);
}

void MacroAssemblerRiscv64Compat::minMax32(Register lhs, Register rhs,
                                           Register dest, bool isMax) {
  if (rhs == dest) {
    std::swap(lhs, rhs);
  }

  if (HasZbbExtension()) {
    UseScratchRegisterScope temps(this);
    const Register rhsSExt = temps.Acquire();
    move32(rhs, rhsSExt);
    move32(lhs, dest);
    // Using signed max/min to match the (signed)
    // Assembler::GreaterThan/Assembler::LessThan below
    if (isMax) {
      max(dest, dest, rhsSExt);
    } else {
      min(dest, dest, rhsSExt);
    }
    return;
  }

  auto cond = isMax ? Assembler::GreaterThan : Assembler::LessThan;
  if (lhs != dest) {
    move32(lhs, dest);
  }
  asMasm().cmp32Move32(cond, rhs, lhs, rhs, dest);
}

void MacroAssemblerRiscv64Compat::minMax32(Register lhs, Imm32 rhs,
                                           Register dest, bool isMax) {
  if (HasZbbExtension()) {
    UseScratchRegisterScope temps(this);
    Register realRhs;
    if (rhs.value == 0) {
      realRhs = zero;
    } else {
      realRhs = temps.Acquire();
      ma_li(realRhs, rhs);
    }
    // Using signed max/min to match the (signed)
    // Assembler::GreaterThan/Assembler::LessThan below
    move32(lhs, dest);
    if (isMax) {
      max(dest, dest, realRhs);
    } else {
      min(dest, dest, realRhs);
    }
    return;
  }

  if (rhs.value == 0) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();

    if (isMax) {
      // dest = -(lhs > 0 ? 1 : 0) & lhs
      sgtz(scratch, lhs);
      neg(scratch, scratch);
      and_(dest, lhs, scratch);
    } else {
      // dest = (lhs >> 31) & lhs
      sraiw(scratch, lhs, 31);
      and_(dest, lhs, scratch);
    }
    return;
  }

  auto cond =
      isMax ? Assembler::GreaterThanOrEqual : Assembler::LessThanOrEqual;
  if (lhs != dest) {
    move32(lhs, dest);
  }
  Label done;
  asMasm().branch32(cond, lhs, rhs, &done);
  move32(rhs, dest);
  bind(&done);
}

void MacroAssemblerRiscv64Compat::minMaxPtr(Register lhs, Register rhs,
                                            Register dest, bool isMax) {
  if (HasZbbExtension()) {
    // Using signed max/min to match the (signed)
    // Assembler::GreaterThan/Assembler::LessThan below
    if (isMax) {
      max(dest, lhs, rhs);
    } else {
      min(dest, lhs, rhs);
    }
    return;
  }

  if (rhs == dest) {
    std::swap(lhs, rhs);
  }

  auto cond = isMax ? Assembler::GreaterThan : Assembler::LessThan;
  if (lhs != dest) {
    movePtr(lhs, dest);
  }
  asMasm().cmpPtrMovePtr(cond, rhs, lhs, rhs, dest);
}

void MacroAssemblerRiscv64Compat::minMaxPtr(Register lhs, ImmWord rhs,
                                            Register dest, bool isMax) {
  if (HasZbbExtension()) {
    UseScratchRegisterScope temps(this);
    Register realRhs;
    if (rhs.value == 0) {
      realRhs = zero;
    } else {
      realRhs = temps.Acquire();
      ma_li(realRhs, rhs);
    }
    // Using signed max/min to match the (signed)
    // Assembler::GreaterThan/Assembler::LessThan below
    if (isMax) {
      max(dest, lhs, realRhs);
    } else {
      min(dest, lhs, realRhs);
    }
    return;
  }

  if (rhs.value == 0) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();

    if (isMax) {
      // dest = -(lhs > 0 ? 1 : 0) & lhs
      sgtz(scratch, lhs);
      neg(scratch, scratch);
      and_(dest, lhs, scratch);
    } else {
      // dest = (lhs >> 63) & lhs
      srai(scratch, lhs, 63);
      and_(dest, lhs, scratch);
    }
    return;
  }

  auto cond =
      isMax ? Assembler::GreaterThanOrEqual : Assembler::LessThanOrEqual;
  if (lhs != dest) {
    movePtr(lhs, dest);
  }
  Label done;
  asMasm().branchPtr(cond, lhs, rhs, &done);
  movePtr(rhs, dest);
  bind(&done);
}

template <typename F>
void MacroAssemblerRiscv64::RoundHelper(FPURegister dst, FPURegister src,
                                        FPURoundingMode mode) {
  static_assert(std::is_same_v<float, F> || std::is_same_v<double, F>);

  if (HasZfaExtension()) {
    if constexpr (std::is_same_v<F, double>) {
      fround_d(dst, src, mode);
    } else {
      fround_s(dst, src, mode);
    }
    return;
  }

  using ScratchDoubleOrFloatScope2 =
      std::conditional_t<std::is_same_v<F, double>, ScratchDoubleScope2,
                         ScratchFloat32Scope2>;

  ScratchDoubleOrFloatScope2 fpu_scratch(asMasm());

  // Need at least two FPRs, so check against dst == src == fpu_scratch
  MOZ_ASSERT(!(dst == src && dst == fpu_scratch));

  // TODO: It's unclear why forbidding pools is necessary here. It should either
  // be documented or pools should be allowed.
  AutoForbidPoolsAndNops afp(this, 20, 2);

  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();

  const int kFloatMantissaBits =
      sizeof(F) == 4 ? kFloat32MantissaBits : kFloat64MantissaBits;
  const int kFloatExponentBits =
      sizeof(F) == 4 ? kFloat32ExponentBits : kFloat64ExponentBits;
  const int kFloatExponentBias =
      sizeof(F) == 4 ? kFloat32ExponentBias : kFloat64ExponentBias;
  Label done;

  {
    UseScratchRegisterScope temps2(this);
    Register scratch = temps2.Acquire();
    // extract exponent value of the source floating-point to scratch
    if (std::is_same<F, double>::value) {
      fmv_x_d(scratch, src);
    } else {
      fmv_x_w(scratch, src);
    }
    ExtractBits(scratch2, scratch, kFloatMantissaBits, kFloatExponentBits);
  }

  // if src is NaN/+-Infinity/+-Zero or if the exponent is larger than # of bits
  // in mantissa, the result is the same as src, so move src to dest  (to avoid
  // generating another branch)
  if (dst != src) {
    if (std::is_same<F, double>::value) {
      fmv_d(dst, src);
    } else {
      fmv_s(dst, src);
    }
  }
  {
    Label not_NaN;
    // According to the wasm spec
    // (https://webassembly.github.io/spec/core/exec/numerics.html#aux-nans)
    // if input is canonical NaN, then output is canonical NaN, and if input is
    // any other NaN, then output is any NaN with most significant bit of
    // payload is 1. In RISC-V, feq_d will set scratch to 0 if src is a NaN. If
    // src is not a NaN, branch to the label and do nothing, but if it is,
    // fmin_d will set dst to the canonical NaN.
    if (std::is_same<F, double>::value) {
      BranchFloat64(Assembler::DoubleOrdered, src, src, &not_NaN, ShortJump);
      fmin_d(dst, src, src);
    } else {
      BranchFloat32(Assembler::DoubleOrdered, src, src, &not_NaN, ShortJump);
      fmin_s(dst, src, src);
    }
    bind(&not_NaN);
  }

  // If real exponent (i.e., scratch2 - kFloatExponentBias) is greater than
  // kFloat32MantissaBits, it means the floating-point value has no fractional
  // part, thus the input is already rounded, jump to done. Note that, NaN and
  // Infinity in floating-point representation sets maximal exponent value, so
  // they also satisfy (scratch2 - kFloatExponentBias >= kFloatMantissaBits),
  // and JS round semantics specify that rounding of NaN (Infinity) returns NaN
  // (Infinity), so NaN and Infinity are considered rounded value too.
  ma_b(scratch2, Imm32(kFloatExponentBias + kFloatMantissaBits), &done,
       GreaterThanOrEqual, ShortJump);

  // Actual rounding is needed along this path

  // old_src holds the original input, needed for the case of src == dst
  FPURegister old_src = src;
  if (src == dst) {
    MOZ_ASSERT(fpu_scratch != dst);
    fmv_d(fpu_scratch, src);
    old_src = fpu_scratch;
  }

  // Since only input whose real exponent value is less than kMantissaBits
  // (i.e., 23 or 52-bits) falls into this path, the value range of the input
  // falls into that of 23- or 53-bit integers. So we round the input to integer
  // values, then convert them back to floating-point.
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    if (std::is_same<F, double>::value) {
      fcvt_l_d(scratch, src, mode);
      fcvt_d_l(dst, scratch, mode);
    } else {
      fcvt_w_s(scratch, src, mode);
      fcvt_s_w(dst, scratch, mode);
    }
  }
  // A special handling is needed if the input is a very small positive/negative
  // number that rounds to zero. JS semantics requires that the rounded result
  // retains the sign of the input, so a very small positive (negative)
  // floating-point number should be rounded to positive (negative) 0.
  // Therefore, we use sign-bit injection to produce +/-0 correctly. Instead of
  // testing for zero w/ a branch, we just insert sign-bit for everyone on this
  // path (this is where old_src is needed)
  if (std::is_same<F, double>::value) {
    fsgnj_d(dst, dst, old_src);
  } else {
    fsgnj_s(dst, dst, old_src);
  }

  bind(&done);
}

template <typename CvtFunc>
void MacroAssemblerRiscv64::RoundFloatingPointToInteger(Register rd,
                                                        FPURegister fs,
                                                        Register result,
                                                        CvtFunc fcvt_generator,
                                                        bool Inexact) {
  // Save csr_fflags to scratch & clear exception flags
  if (result != Register::Invalid()) {
    // TODO: It's unclear why forbidding pools is necessary here. It should
    // either be documented or pools should be allowed.
    AutoForbidPoolsAndNops afp(this, 6);

    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();

    int exception_flags = kInvalidOperation;
    if (Inexact) exception_flags |= kInexact;
    csrrci(scratch, csr_fflags, exception_flags);

    // actual conversion instruction
    fcvt_generator(this, rd, fs);

    // check kInvalidOperation flag (out-of-range, NaN)
    // set result to 1 if normal, otherwise set result to 0 for abnormal
    frflags(result);
    andi(result, result, exception_flags);
    seqz(result, result);  // result <-- 1 (normal), result <-- 0 (abnormal)

    // restore csr_fflags
    csrw(csr_fflags, scratch);
  } else {
    // actual conversion instruction
    fcvt_generator(this, rd, fs);
  }
}

void MacroAssemblerRiscv64::Trunc_uw_d(Register rd, FPURegister fs,
                                       Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_wu_d(dst, src, RTZ);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Trunc_w_d(Register rd, FPURegister fs,
                                      Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_w_d(dst, src, RTZ);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Trunc_uw_s(Register rd, FPURegister fs,
                                       Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_wu_s(dst, src, RTZ);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Trunc_w_s(Register rd, FPURegister fs,
                                      Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_w_s(dst, src, RTZ);
      },
      Inexact);
}
void MacroAssemblerRiscv64::Trunc_ul_d(Register rd, FPURegister fs,
                                       Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_lu_d(dst, src, RTZ);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Trunc_l_d(Register rd, FPURegister fs,
                                      Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_l_d(dst, src, RTZ);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Trunc_ul_s(Register rd, FPURegister fs,
                                       Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_lu_s(dst, src, RTZ);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Trunc_l_s(Register rd, FPURegister fs,
                                      Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_l_s(dst, src, RTZ);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Floor_d_d(FPURegister fd, FPURegister fs) {
  RoundHelper<double>(fd, fs, RDN);
}

void MacroAssemblerRiscv64::Ceil_d_d(FPURegister fd, FPURegister fs) {
  RoundHelper<double>(fd, fs, RUP);
}

void MacroAssemblerRiscv64::Trunc_d_d(FPURegister fd, FPURegister fs) {
  RoundHelper<double>(fd, fs, RTZ);
}

void MacroAssemblerRiscv64::Round_d_d(FPURegister fd, FPURegister fs) {
  RoundHelper<double>(fd, fs, RNE);
}

void MacroAssemblerRiscv64::Floor_s_s(FPURegister fd, FPURegister fs) {
  RoundHelper<float>(fd, fs, RDN);
}

void MacroAssemblerRiscv64::Ceil_s_s(FPURegister fd, FPURegister fs) {
  RoundHelper<float>(fd, fs, RUP);
}

void MacroAssemblerRiscv64::Trunc_s_s(FPURegister fd, FPURegister fs) {
  RoundHelper<float>(fd, fs, RTZ);
}

void MacroAssemblerRiscv64::Round_s_s(FPURegister fd, FPURegister fs) {
  RoundHelper<float>(fd, fs, RNE);
}

void MacroAssemblerRiscv64::Round_w_s(Register rd, FPURegister fs,
                                      Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_w_s(dst, src, RNE);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Round_w_d(Register rd, FPURegister fs,
                                      Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_w_d(dst, src, RNE);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Ceil_w_s(Register rd, FPURegister fs,
                                     Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_w_s(dst, src, RUP);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Ceil_l_d(Register rd, FPURegister fs,
                                     Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_l_d(dst, src, RUP);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Ceil_l_s(Register rd, FPURegister fs,
                                     Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_l_s(dst, src, RUP);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Ceil_w_d(Register rd, FPURegister fs,
                                     Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_w_d(dst, src, RUP);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Floor_w_s(Register rd, FPURegister fs,
                                      Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_w_s(dst, src, RDN);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Floor_w_d(Register rd, FPURegister fs,
                                      Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_w_d(dst, src, RDN);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Floor_l_s(Register rd, FPURegister fs,
                                      Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_l_s(dst, src, RDN);
      },
      Inexact);
}

void MacroAssemblerRiscv64::Floor_l_d(Register rd, FPURegister fs,
                                      Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_l_d(dst, src, RDN);
      },
      Inexact);
}

void MacroAssemblerRiscv64::RoundMaxMag_l_s(Register rd, FPURegister fs,
                                            Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_l_s(dst, src, RMM);
      },
      Inexact);
}

void MacroAssemblerRiscv64::RoundMaxMag_l_d(Register rd, FPURegister fs,
                                            Register result, bool Inexact) {
  RoundFloatingPointToInteger(
      rd, fs, result,
      [](MacroAssemblerRiscv64* masm, Register dst, FPURegister src) {
        masm->fcvt_l_d(dst, src, RMM);
      },
      Inexact);
}

// Checks whether a double is representable as a 32-bit integer. If so, the
// integer is written to the output register. Otherwise, a bailout is taken to
// the given snapshot. This function overwrites the scratch float register.
void MacroAssemblerRiscv64Compat::convertDoubleToInt32(FloatRegister src,
                                                       Register dest,
                                                       Label* fail,
                                                       bool negativeZeroCheck) {
  if (negativeZeroCheck) {
    fclass_d(dest, src);
    ma_b(dest, Imm32(kNegativeZero), fail, Equal, ShortJump);
  }
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Trunc_w_d(dest, src, scratch, true);
  ma_b(scratch, Imm32(0), fail, Equal, ShortJump);
}

void MacroAssemblerRiscv64Compat::convertDoubleToPtr(FloatRegister src,
                                                     Register dest, Label* fail,
                                                     bool negativeZeroCheck) {
  if (negativeZeroCheck) {
    fclass_d(dest, src);
    ma_b(dest, Imm32(kNegativeZero), fail, Equal, ShortJump);
  }
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Trunc_l_d(dest, src, scratch, true);
  ma_b(scratch, Imm32(0), fail, Equal, ShortJump);
}

// Checks whether a float32 is representable as a 32-bit integer. If so, the
// integer is written to the output register. Otherwise, a bailout is taken to
// the given snapshot. This function overwrites the scratch float register.
void MacroAssemblerRiscv64Compat::convertFloat32ToInt32(
    FloatRegister src, Register dest, Label* fail, bool negativeZeroCheck) {
  if (negativeZeroCheck) {
    fclass_d(dest, src);
    ma_b(dest, Imm32(kNegativeZero), fail, Equal, ShortJump);
  }
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Trunc_w_s(dest, src, scratch, true);
  ma_b(scratch, Imm32(0), fail, Equal, ShortJump);
}

void MacroAssemblerRiscv64Compat::convertFloat32ToDouble(FloatRegister src,
                                                         FloatRegister dest) {
  fcvt_d_s(dest, src);
}

void MacroAssemblerRiscv64Compat::convertInt32ToFloat32(Register src,
                                                        FloatRegister dest) {
  fcvt_s_w(dest, src);
}

void MacroAssemblerRiscv64Compat::convertInt32ToFloat32(const Address& src,
                                                        FloatRegister dest) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  load32(src, scratch);
  fcvt_s_w(dest, scratch);
}

void MacroAssemblerRiscv64Compat::truncateFloat32ModUint32(FloatRegister src,
                                                           Register dest) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  // Convert scalar to signed 64-bit fixed-point, rounding toward zero.
  // In the case of overflow or NaN, the output is saturated.
  // In the case of -0, the output is zero.
  Trunc_l_s(dest, src);

  // Unsigned subtraction of INT64_MAX returns 1 resp. 0 for INT64_{MIN,MAX}.
  ma_li(scratch, Imm64(0x7fff'ffff'ffff'ffff));
  sub(scratch, dest, scratch);

  // If scratch u< 2, then scratch = 0; else scratch = -1.
  sltiu(scratch, scratch, 2);
  addiw(scratch, scratch, -1);

  // Clear |dest| if the truncation result was saturated.
  and_(dest, dest, scratch);

  // Clear upper 32 bits.
  SignExtendWord(dest, dest);
}

// Return true if |n| is representable as the addition of two int12.
static inline bool is_two_int12(int64_t n) {
  // Note: The caller handles the case when |n| is exactly an int12. We don't
  // exclude exact int12 values, because Clang/GCC generate slightly smaller
  // code when testing for the complete range.
  return -4096 <= n && n <= 4094;
}

struct TwoInt12 {
  int16_t first;
  int16_t second;
};

constexpr auto ToTwoInt12(int32_t n) {
  MOZ_ASSERT(is_two_int12(n));
  return TwoInt12{
      .first = int16_t(n / 2),
      .second = int16_t(n - (n / 2)),
  };
}

// Memory.
std::pair<Register, int16_t> MacroAssemblerRiscv64::computeAddress(
    Address address, UseScratchRegisterScope& temps) {
  Register base;
  int16_t encodedOffset;

  int32_t offset = address.offset;
  if (is_int12(offset)) {
    base = address.base;
    encodedOffset = offset;
  } else if (is_two_int12(offset)) {
    auto [first, second] = ToTwoInt12(offset);

    Register scratch = temps.Acquire();
    addi(scratch, address.base, first);
    base = scratch;
    encodedOffset = second;
  } else if (is_int32(int64_t(offset) + 0x800)) {
    auto [high20, low12] = ToHigh20Low12(offset);

    Register scratch = temps.Acquire();
    lui(scratch, high20);
    add(scratch, address.base, scratch);
    base = scratch;
    encodedOffset = low12;
  } else {
    Register scratch = temps.Acquire();
    ma_li(scratch, Imm32(offset));
    add(scratch, address.base, scratch);
    base = scratch;
    encodedOffset = 0;
  }

  return {base, encodedOffset};
}

FaultingCodeRange MacroAssemblerRiscv64::ma_loadDouble(FloatRegister dest,
                                                       Address address) {
  UseScratchRegisterScope temps(this);
  auto [base, encodedOffset] = computeAddress(address, temps);

  AutoForbidPoolsAndNops afp(this, 1);
  FaultingCodeRange fcr = FaultingCodeRange(currentOffset());
  fld(dest, base, encodedOffset);
  return fcr;
}

FaultingCodeRange MacroAssemblerRiscv64::ma_loadDouble(FloatRegister dest,
                                                       const BaseIndex& src) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(src, temps);
  return ma_loadDouble(dest, address);
}

FaultingCodeRange MacroAssemblerRiscv64::ma_loadFloat(FloatRegister dest,
                                                      Address address) {
  UseScratchRegisterScope temps(this);
  auto [base, encodedOffset] = computeAddress(address, temps);

  AutoForbidPoolsAndNops afp(this, 1);
  FaultingCodeRange fcr = FaultingCodeRange(currentOffset());
  flw(dest, base, encodedOffset);
  return fcr;
}

FaultingCodeRange MacroAssemblerRiscv64::ma_loadFloat(FloatRegister dest,
                                                      const BaseIndex& src) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(src, temps);
  return ma_loadFloat(dest, address);
}

FaultingCodeRange MacroAssemblerRiscv64::ma_loadFloat16(FloatRegister dest,
                                                        Address address) {
  MOZ_ASSERT(HasZfhminExtension());

  UseScratchRegisterScope temps(this);
  auto [base, encodedOffset] = computeAddress(address, temps);

  AutoForbidPoolsAndNops afp(this, 1);
  FaultingCodeRange fcr = FaultingCodeRange(currentOffset());
  flh(dest, base, encodedOffset);
  return fcr;
}

FaultingCodeRange MacroAssemblerRiscv64::ma_loadFloat16(FloatRegister dest,
                                                        const BaseIndex& src) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(src, temps);
  return ma_loadFloat16(dest, address);
}

FaultingCodeRange MacroAssemblerRiscv64::ma_load(Register dest, Address address,
                                                 LoadStoreSize size,
                                                 LoadStoreExtension extension) {
  UseScratchRegisterScope temps(this);
  auto [base, encodedOffset] = computeAddress(address, temps);

  AutoForbidPoolsAndNops afp(this, 1);
  FaultingCodeRange fcr = FaultingCodeRange(currentOffset());
  switch (size) {
    case SizeByte:
      if (ZeroExtend == extension) {
        lbu(dest, base, encodedOffset);
      } else {
        lb(dest, base, encodedOffset);
      }
      break;
    case SizeHalfWord:
      if (ZeroExtend == extension) {
        lhu(dest, base, encodedOffset);
      } else {
        lh(dest, base, encodedOffset);
      }
      break;
    case SizeWord:
      if (ZeroExtend == extension) {
        lwu(dest, base, encodedOffset);
      } else {
        lw(dest, base, encodedOffset);
      }
      break;
    case SizeDouble:
      ld(dest, base, encodedOffset);
      break;
    default:
      MOZ_CRASH("Invalid argument for ma_load");
  }
  return fcr;
}

FaultingCodeRange MacroAssemblerRiscv64::ma_store(
    Register data, const BaseIndex& dest, LoadStoreSize size,
    LoadStoreExtension extension) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(dest, temps);
  return ma_store(data, address, size, extension);
}

FaultingCodeRange MacroAssemblerRiscv64::ma_store(
    Imm32 imm, const BaseIndex& dest, LoadStoreSize size,
    LoadStoreExtension extension) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(dest, temps);
  return ma_store(imm, address, size, extension);
}

FaultingCodeRange MacroAssemblerRiscv64::ma_store(
    Imm32 imm, Address address, LoadStoreSize size,
    LoadStoreExtension extension) {
  UseScratchRegisterScope temps(this);
  Register src;
  if (imm.value == 0) {
    src = zero_reg;
  } else {
    src = temps.Acquire();
    ma_li(src, imm);
  }
  return ma_store(src, address, size, extension);
}

FaultingCodeRange MacroAssemblerRiscv64::ma_store(
    Register data, Address address, LoadStoreSize size,
    LoadStoreExtension extension) {
  UseScratchRegisterScope temps(this);
  auto [base, encodedOffset] = computeAddress(address, temps);

  AutoForbidPoolsAndNops afp(this, 1);
  FaultingCodeRange fcr = FaultingCodeRange(currentOffset());
  switch (size) {
    case SizeByte:
      sb(data, base, encodedOffset);
      break;
    case SizeHalfWord:
      sh(data, base, encodedOffset);
      break;
    case SizeWord:
      sw(data, base, encodedOffset);
      break;
    case SizeDouble:
      sd(data, base, encodedOffset);
      break;
    default:
      MOZ_CRASH("Invalid argument for ma_store");
  }
  return fcr;
}

// Memory.
FaultingCodeRange MacroAssemblerRiscv64::ma_storeDouble(FloatRegister src,
                                                        Address address) {
  UseScratchRegisterScope temps(this);
  auto [base, encodedOffset] = computeAddress(address, temps);

  AutoForbidPoolsAndNops afp(this, 1);
  FaultingCodeRange fcr = FaultingCodeRange(currentOffset());
  fsd(src, base, encodedOffset);
  return fcr;
}

FaultingCodeRange MacroAssemblerRiscv64::ma_storeDouble(FloatRegister src,
                                                        const BaseIndex& dest) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(dest, temps);
  return ma_storeDouble(src, address);
}

FaultingCodeRange MacroAssemblerRiscv64::ma_storeFloat(FloatRegister src,
                                                       Address address) {
  UseScratchRegisterScope temps(this);
  auto [base, encodedOffset] = computeAddress(address, temps);

  AutoForbidPoolsAndNops afp(this, 1);
  FaultingCodeRange fcr = FaultingCodeRange(currentOffset());
  fsw(src, base, encodedOffset);
  return fcr;
}

FaultingCodeRange MacroAssemblerRiscv64::ma_storeFloat(FloatRegister src,
                                                       const BaseIndex& dest) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(dest, temps);
  return ma_storeFloat(src, address);
}

FaultingCodeRange MacroAssemblerRiscv64::ma_storeFloat16(FloatRegister src,
                                                         Address address) {
  MOZ_ASSERT(HasZfhminExtension());

  UseScratchRegisterScope temps(this);
  auto [base, encodedOffset] = computeAddress(address, temps);

  AutoForbidPoolsAndNops afp(this, 1);
  FaultingCodeRange fcr = FaultingCodeRange(currentOffset());
  fsh(src, base, encodedOffset);
  return fcr;
}

FaultingCodeRange MacroAssemblerRiscv64::ma_storeFloat16(
    FloatRegister src, const BaseIndex& dest) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(dest, temps);
  return ma_storeFloat16(src, address);
}

Address MacroAssemblerRiscv64::computeScaledAddress(
    const BaseIndex& address, UseScratchRegisterScope& temps,
    wasm::ZeroExtendIndex zeroExtend) {
  if (address.index == zero) {
    return Address(address.base, address.offset);
  }

  Register scratch = temps.Acquire();
  MOZ_ASSERT(scratch != address.base);
  MOZ_ASSERT(scratch != address.index);

  computeScaledAddress(address, scratch, zeroExtend);

  int32_t offset = address.offset;
  if (is_int12(offset)) {
    // Nothing to do, offset fits into i12 immediate of load/store instructions.
  } else if (is_two_int12(offset)) {
    auto [first, second] = ToTwoInt12(offset);

    addi(scratch, scratch, first);
    offset = second;
  } else if (is_int32(int64_t(offset) + 0x800)) {
    auto [high20, low12] = ToHigh20Low12(offset);

    Register scratch2 = temps.Acquire();
    lui(scratch2, high20);
    add(scratch, scratch, scratch2);
    offset = low12;
  } else {
    Register scratch2 = temps.Acquire();
    ma_li(scratch2, Imm32(offset));
    add(scratch, scratch, scratch2);
    offset = 0;
  }
  return Address(scratch, offset);
}

void MacroAssemblerRiscv64::computeScaledAddress(
    const BaseIndex& address, Register dest, wasm::ZeroExtendIndex zeroExtend) {
  Register base = address.base;
  Register index = address.index;
  int32_t shift = Imm32::ShiftOf(address.scale).value;
  MOZ_ASSERT(shift <= 3);

  if (index == zero) {
    if (dest != base) {
      ma_mv(dest, base);
    }
  } else if (shift && base == zero) {
    if (zeroExtend == wasm::ZeroExtendIndex::No) {
      slli(dest, index, shift);
    } else {
      if (HasZbaExtension()) {
        slli_uw(dest, index, shift);
      } else {
        slli(dest, index, 32);
        srli(dest, dest, 32 - shift);
      }
    }
  } else if (shift) {
    if (HasZbaExtension()) {
      if (zeroExtend == wasm::ZeroExtendIndex::No) {
        switch (shift) {
          case 1:
            sh1add(dest, index, base);
            return;
          case 2:
            sh2add(dest, index, base);
            return;
          case 3:
            sh3add(dest, index, base);
            return;
          default:
            MOZ_CRASH("invalid shift");
        }
      } else {
        switch (shift) {
          case 1:
            sh1add_uw(dest, index, base);
            return;
          case 2:
            sh2add_uw(dest, index, base);
            return;
          case 3:
            sh3add_uw(dest, index, base);
            return;
          default:
            MOZ_CRASH("invalid shift");
        }
      }
    }

    UseScratchRegisterScope temps(this);
    Register tmp = dest == base ? temps.Acquire() : dest;
    if (zeroExtend == wasm::ZeroExtendIndex::No) {
      slli(tmp, index, shift);
    } else {
      slli(tmp, index, 32);
      srli(tmp, tmp, 32 - shift);
    }
    add(dest, base, tmp);
  } else {
    if (zeroExtend == wasm::ZeroExtendIndex::No) {
      add(dest, base, index);
    } else {
      if (HasZbaExtension()) {
        add_uw(dest, index, base);
      } else {
        UseScratchRegisterScope temps(this);
        Register tmp = dest == base ? temps.Acquire() : dest;

        ZeroExtendWord(tmp, index);
        add(dest, base, tmp);
      }
    }
  }
}

Address MacroAssemblerRiscv64::computeAbsoluteAddress(AbsoluteAddress address,
                                                      Register scratch) {
  int64_t value = int64_t(address.addr);
  int64_t low12 = int64_t(uint64_t(value) << 52) >> 52;
  ma_li(scratch, ImmWord(uint64_t(value - low12)));
  return Address(scratch, int32_t(low12));
}

void MacroAssemblerRiscv64::computeScaledAddress32(const BaseIndex& address,
                                                   Register dest) {
  Register base = address.base;
  Register index = address.index;
  int32_t shift = Imm32::ShiftOf(address.scale).value;
  MOZ_ASSERT(shift <= 4);

  if (index == zero) {
    SignExtendWord(dest, base);
  } else if (shift && base == zero) {
    slliw(dest, index, shift);
  } else if (shift) {
    UseScratchRegisterScope temps(this);
    Register tmp = dest == base ? temps.Acquire() : dest;
    slliw(tmp, index, shift);
    addw(dest, base, tmp);
  } else {
    addw(dest, base, index);
  }
}

void MacroAssemblerRiscv64Compat::profilerEnterFrame(Register framePtr,
                                                     Register scratch) {
  asMasm().loadJSContext(scratch);
  loadPtr(Address(scratch, offsetof(JSContext, profilingActivation_)), scratch);
  storePtr(framePtr,
           Address(scratch, JitActivation::offsetOfLastProfilingFrame()));
  storePtr(ImmPtr(nullptr),
           Address(scratch, JitActivation::offsetOfLastProfilingCallSite()));
}

void MacroAssemblerRiscv64Compat::profilerExitFrame() {
  jump(asMasm().runtime()->jitRuntime()->getProfilerExitFrameTail());
}

void MacroAssemblerRiscv64Compat::move32(Imm32 imm, Register dest) {
  ma_li(dest, imm);
}

void MacroAssemblerRiscv64Compat::move32(Register src, Register dest) {
  SignExtendWord(dest, src);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::load8ZeroExtend(
    const Address& address, Register dest) {
  return ma_load(dest, address, SizeByte, ZeroExtend);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::load8ZeroExtend(
    const BaseIndex& src, Register dest) {
  return ma_load(dest, src, SizeByte, ZeroExtend);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::load8SignExtend(
    const Address& address, Register dest) {
  return ma_load(dest, address, SizeByte, SignExtend);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::load8SignExtend(
    const BaseIndex& src, Register dest) {
  return ma_load(dest, src, SizeByte, SignExtend);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::load16ZeroExtend(
    const Address& address, Register dest) {
  return ma_load(dest, address, SizeHalfWord, ZeroExtend);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::load16ZeroExtend(
    const BaseIndex& src, Register dest) {
  return ma_load(dest, src, SizeHalfWord, ZeroExtend);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::load16SignExtend(
    const Address& address, Register dest) {
  return ma_load(dest, address, SizeHalfWord, SignExtend);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::load16SignExtend(
    const BaseIndex& src, Register dest) {
  return ma_load(dest, src, SizeHalfWord, SignExtend);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::load32(const Address& address,
                                                      Register dest) {
  return ma_load(dest, address, SizeWord);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::load32(const BaseIndex& address,
                                                      Register dest) {
  return ma_load(dest, address, SizeWord);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::load32(AbsoluteAddress address,
                                                      Register dest) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  return load32(computeAbsoluteAddress(address, scratch), dest);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::load32(
    wasm::SymbolicAddress address, Register dest) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  movePtr(address, scratch);
  return load32(Address(scratch, 0), dest);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::loadPtr(const Address& address,
                                                       Register dest) {
  return ma_load(dest, address, SizeDouble);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::loadPtr(const BaseIndex& src,
                                                       Register dest) {
  return ma_load(dest, src, SizeDouble);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::loadPtr(AbsoluteAddress address,
                                                       Register dest) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  return loadPtr(computeAbsoluteAddress(address, scratch), dest);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::loadPtr(
    wasm::SymbolicAddress address, Register dest) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  movePtr(address, scratch);
  return loadPtr(Address(scratch, 0), dest);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::loadPrivate(
    const Address& address, Register dest) {
  return loadPtr(address, dest);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::store8(Imm32 imm,
                                                      const Address& address) {
  return ma_store(imm, address, SizeByte);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::store8(Register src,
                                                      const Address& address) {
  return ma_store(src, address, SizeByte);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::store8(
    Imm32 imm, const BaseIndex& address) {
  return ma_store(imm, address, SizeByte);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::store8(
    Register src, const BaseIndex& address) {
  return ma_store(src, address, SizeByte);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::store16(Imm32 imm,
                                                       const Address& address) {
  return ma_store(imm, address, SizeHalfWord);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::store16(Register src,
                                                       const Address& address) {
  return ma_store(src, address, SizeHalfWord);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::store16(
    Imm32 imm, const BaseIndex& address) {
  return ma_store(imm, address, SizeHalfWord);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::store16(
    Register src, const BaseIndex& address) {
  return ma_store(src, address, SizeHalfWord);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::store32(
    Register src, AbsoluteAddress address) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  return store32(src, computeAbsoluteAddress(address, scratch));
}

FaultingCodeRange MacroAssemblerRiscv64Compat::store32(Register src,
                                                       const Address& address) {
  return ma_store(src, address, SizeWord);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::store32(Imm32 src,
                                                       const Address& address) {
  return ma_store(src, address, SizeWord);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::store32(
    Imm32 src, const BaseIndex& address) {
  return ma_store(src, address, SizeWord);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::store32(
    Register src, const BaseIndex& address) {
  return ma_store(src, address, SizeWord);
}

template <typename T>
FaultingCodeRange MacroAssemblerRiscv64Compat::storePtr(ImmWord imm,
                                                        T address) {
  UseScratchRegisterScope temps(this);
  Register src;
  if (imm.value == 0) {
    src = zero_reg;
  } else {
    src = temps.Acquire();
    ma_li(src, imm);
  }
  return ma_store(src, address, SizeDouble);
}

template FaultingCodeRange MacroAssemblerRiscv64Compat::storePtr<Address>(
    ImmWord imm, Address address);
template FaultingCodeRange MacroAssemblerRiscv64Compat::storePtr<BaseIndex>(
    ImmWord imm, BaseIndex address);

template <typename T>
FaultingCodeRange MacroAssemblerRiscv64Compat::storePtr(ImmPtr imm, T address) {
  return storePtr(ImmWord(uintptr_t(imm.value)), address);
}

template FaultingCodeRange MacroAssemblerRiscv64Compat::storePtr<Address>(
    ImmPtr imm, Address address);
template FaultingCodeRange MacroAssemblerRiscv64Compat::storePtr<BaseIndex>(
    ImmPtr imm, BaseIndex address);

template <typename T>
FaultingCodeRange MacroAssemblerRiscv64Compat::storePtr(ImmGCPtr imm,
                                                        T address) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  movePtr(imm, scratch);
  return storePtr(scratch, address);
}

template FaultingCodeRange MacroAssemblerRiscv64Compat::storePtr<Address>(
    ImmGCPtr imm, Address address);
template FaultingCodeRange MacroAssemblerRiscv64Compat::storePtr<BaseIndex>(
    ImmGCPtr imm, BaseIndex address);

FaultingCodeRange MacroAssemblerRiscv64Compat::storePtr(
    Register src, const Address& address) {
  return ma_store(src, address, SizeDouble);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::storePtr(
    Register src, const BaseIndex& address) {
  return ma_store(src, address, SizeDouble);
}

FaultingCodeRange MacroAssemblerRiscv64Compat::storePtr(Register src,
                                                        AbsoluteAddress dest) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  return storePtr(src, computeAbsoluteAddress(dest, scratch));
}

void MacroAssemblerRiscv64Compat::testNullSet(Condition cond,
                                              const ValueOperand& value,
                                              Register dest) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  splitSignExtTag(value, dest);
  ma_cmp_set(dest, dest, ImmTagSignExt(JSVAL_TAG_NULL), cond);
}

void MacroAssemblerRiscv64Compat::testObjectSet(Condition cond,
                                                const ValueOperand& value,
                                                Register dest) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  splitSignExtTag(value, dest);
  ma_cmp_set(dest, dest, ImmTagSignExt(JSVAL_TAG_OBJECT), cond);
}

void MacroAssemblerRiscv64Compat::testUndefinedSet(Condition cond,
                                                   const ValueOperand& value,
                                                   Register dest) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  splitSignExtTag(value, dest);
  ma_cmp_set(dest, dest, ImmTagSignExt(JSVAL_TAG_UNDEFINED), cond);
}

void MacroAssemblerRiscv64Compat::unboxInt32(const ValueOperand& operand,
                                             Register dest) {
  SignExtendWord(dest, operand.valueReg());
}

void MacroAssemblerRiscv64Compat::unboxInt32(Register src, Register dest) {
  SignExtendWord(dest, src);
}

void MacroAssemblerRiscv64Compat::unboxInt32(const Address& src,
                                             Register dest) {
  load32(Address(src.base, src.offset), dest);
}

void MacroAssemblerRiscv64Compat::unboxInt32(const BaseIndex& src,
                                             Register dest) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(src, temps);
  load32(address, dest);
}

void MacroAssemblerRiscv64Compat::unboxBoolean(const ValueOperand& operand,
                                               Register dest) {
  SignExtendWord(dest, operand.valueReg());
}

void MacroAssemblerRiscv64Compat::unboxBoolean(Register src, Register dest) {
  SignExtendWord(dest, src);
}

void MacroAssemblerRiscv64Compat::unboxBoolean(const Address& src,
                                               Register dest) {
  load32(Address(src.base, src.offset), dest);
}

void MacroAssemblerRiscv64Compat::unboxBoolean(const BaseIndex& src,
                                               Register dest) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(src, temps);
  load32(address, dest);
}

void MacroAssemblerRiscv64Compat::unboxDouble(const ValueOperand& operand,
                                              FloatRegister dest) {
  fmv_d_x(dest, operand.valueReg());
}

void MacroAssemblerRiscv64Compat::unboxDouble(const Address& src,
                                              FloatRegister dest) {
  ma_loadDouble(dest, Address(src.base, src.offset));
}

void MacroAssemblerRiscv64Compat::unboxDouble(const BaseIndex& src,
                                              FloatRegister dest) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  loadPtr(src, scratch);
  unboxDouble(ValueOperand(scratch), dest);
}

void MacroAssemblerRiscv64Compat::unboxString(const ValueOperand& operand,
                                              Register dest) {
  unboxNonDouble(operand, dest, JSVAL_TYPE_STRING);
}

void MacroAssemblerRiscv64Compat::unboxString(Register src, Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_STRING);
}

void MacroAssemblerRiscv64Compat::unboxString(const Address& src,
                                              Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_STRING);
}

void MacroAssemblerRiscv64Compat::unboxSymbol(const ValueOperand& operand,
                                              Register dest) {
  unboxNonDouble(operand, dest, JSVAL_TYPE_SYMBOL);
}

void MacroAssemblerRiscv64Compat::unboxSymbol(Register src, Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_SYMBOL);
}

void MacroAssemblerRiscv64Compat::unboxSymbol(const Address& src,
                                              Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_SYMBOL);
}

void MacroAssemblerRiscv64Compat::unboxBigInt(const ValueOperand& operand,
                                              Register dest) {
  unboxNonDouble(operand, dest, JSVAL_TYPE_BIGINT);
}

void MacroAssemblerRiscv64Compat::unboxBigInt(Register src, Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_BIGINT);
}

void MacroAssemblerRiscv64Compat::unboxBigInt(const Address& src,
                                              Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_BIGINT);
}

void MacroAssemblerRiscv64Compat::unboxObject(const ValueOperand& operand,
                                              Register dest) {
  unboxNonDouble(operand, dest, JSVAL_TYPE_OBJECT);
}

void MacroAssemblerRiscv64Compat::unboxObject(Register src, Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_OBJECT);
}

void MacroAssemblerRiscv64Compat::unboxObject(const Address& src,
                                              Register dest) {
  unboxNonDouble(src, dest, JSVAL_TYPE_OBJECT);
}

void MacroAssemblerRiscv64Compat::unboxValue(const ValueOperand& operand,
                                             AnyRegister dest,
                                             JSValueType type) {
  if (dest.isFloat()) {
    Label notInt32, end;
    asMasm().branchTestInt32(Assembler::NotEqual, operand, &notInt32);
    convertInt32ToDouble(operand.valueReg(), dest.fpu());
    jump(&end);
    bind(&notInt32);
    unboxDouble(operand, dest.fpu());
    bind(&end);
  } else {
    unboxNonDouble(operand, dest.gpr(), type);
  }
}

void MacroAssemblerRiscv64Compat::boxDouble(FloatRegister src,
                                            const ValueOperand& dest,
                                            FloatRegister) {
  fmv_x_d(dest.valueReg(), src);
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

static void AssertValidPayload(MacroAssemblerRiscv64Compat& masm,
                               JSValueType type, Register payload,
                               Register scratch) {
#ifdef DEBUG
  if (type == JSVAL_TYPE_INT32) {
    // Ensure the payload is a properly sign-extended int32.
    Label signExtended;
    masm.SignExtendWord(scratch, payload);
    masm.ma_b(payload, scratch, &signExtended, Assembler::Equal, ShortJump);
    masm.breakpoint();
    masm.bind(&signExtended);
  } else {
    // All bits above the payload must be zeroed.
    Label zeroed;
    masm.srli(scratch, payload, PayloadSize(type));
    masm.ma_b(scratch, Imm32(0), &zeroed, Assembler::Equal, ShortJump);
    masm.breakpoint();
    masm.bind(&zeroed);
  }
#endif
}

void MacroAssemblerRiscv64Compat::boxValue(JSValueType type, Register src,
                                           Register dest) {
  MOZ_ASSERT(type != JSVAL_TYPE_UNDEFINED && type != JSVAL_TYPE_NULL);
  MOZ_ASSERT(src != dest);

  AssertValidPayload(*this, type, src, dest);

  switch (type) {
    case JSVAL_TYPE_INT32: {
      // Loading the shifted tag requires only two instructions.
      ma_li(dest, ImmShiftedTag(type));

      // Insert low 32 bits as payload, removing all high bits from |src|.
      if (HasZbaExtension()) {
        add_uw(dest, src, dest);
      } else {
        UseScratchRegisterScope temps(this);
        Register scratch = temps.Acquire();

        ZeroExtendWord(scratch, src);
        or_(dest, dest, scratch);
      }
      return;
    }
    case JSVAL_TYPE_BOOLEAN:
    case JSVAL_TYPE_MAGIC:
    case JSVAL_TYPE_STRING:
    case JSVAL_TYPE_SYMBOL:
    case JSVAL_TYPE_PRIVATE_GCTHING:
    case JSVAL_TYPE_BIGINT:
    case JSVAL_TYPE_OBJECT: {
      // Loading the negated shifted tag requires at most two instructions.
      ma_li(dest, ImmNegatedShiftedTag(type));

      // Insert payload.
      sub(dest, src, dest);
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

void MacroAssemblerRiscv64Compat::boxValue(Register type, Register src,
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
  // GCThing types aren't currently supported, because SignExtendWord truncates
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
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    SignExtendWord(scratch, src);
    ma_b(src, scratch, &done, Assembler::Equal, ShortJump);
    breakpoint();
  }
  bind(&done);
#endif

  // JSVAL_TAG_MAX_DOUBLE can't be directly encoded in a single `ori`
  // instruction. Sign-extend the tag, taking the bits into account which will
  // later be shifted out, into a shorter immediate which fits into `ori`.
  constexpr int64_t tag =
      int64_t(uint64_t(JSVAL_TAG_MAX_DOUBLE) << JSVAL_TAG_SHIFT) >>
      JSVAL_TAG_SHIFT;
  static_assert(is_int12(tag), "ori requires int12 immediate");

  ori(dest, type, tag);
  slli(dest, dest, JSVAL_TAG_SHIFT);

  // Insert low 32 bits as payload, removing all high bits from |src|.
  if (HasZbaExtension()) {
    add_uw(dest, src, dest);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    ZeroExtendWord(scratch, src);

    or_(dest, dest, scratch);
  }
}

void MacroAssemblerRiscv64Compat::loadConstantFloat32(float f,
                                                      FloatRegister dest) {
  ma_lis(dest, f);
}

void MacroAssemblerRiscv64Compat::loadInt32OrDouble(const Address& src,
                                                    FloatRegister dest) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  Label notInt32, end;
  {
    // Inlined |branchTestInt32| to use a short-jump.
    Register tag = extractTag(src, scratch);
    ma_b(tag, ImmTagSignExt(JSVAL_TAG_INT32), &notInt32, Assembler::NotEqual,
         ShortJump);
  }
  {
    // If it's an int, convert it to double.
    unboxInt32(src, scratch);
    convertInt32ToDouble(scratch, dest);
    jump(&end);
  }
  bind(&notInt32);
  {
    // Not an int, just load as double.
    unboxDouble(src, dest);
  }
  bind(&end);
}

void MacroAssemblerRiscv64Compat::loadInt32OrDouble(const BaseIndex& src,
                                                    FloatRegister dest) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(src, temps);
  loadInt32OrDouble(address, dest);
}

void MacroAssemblerRiscv64Compat::loadConstantDouble(double dp,
                                                     FloatRegister dest) {
  ma_lid(dest, dp);
}

Register MacroAssemblerRiscv64Compat::extractObject(const Address& address,
                                                    Register scratch) {
  loadPtr(address, scratch);
  ExtractBits(scratch, scratch, 0, JSVAL_TAG_SHIFT);
  return scratch;
}

Register MacroAssemblerRiscv64Compat::extractTag(const Address& address,
                                                 Register scratch) {
  loadPtr(address, scratch);
  splitSignExtTag(scratch, scratch);
  return scratch;
}

Register MacroAssemblerRiscv64Compat::extractTag(const BaseIndex& address,
                                                 Register scratch) {
  UseScratchRegisterScope temps(this);
  Address addr = computeScaledAddress(address, temps);
  return extractTag(addr, scratch);
}

/////////////////////////////////////////////////////////////////
// X86/X64-common/ARM/LoongArch interface.
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
// X86/X64-common/ARM/MIPS interface.
/////////////////////////////////////////////////////////////////
void MacroAssemblerRiscv64Compat::storeValue(ValueOperand val,
                                             const BaseIndex& dest) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(dest, temps);
  storeValue(val, address);
}

void MacroAssemblerRiscv64Compat::storeValue(JSValueType type, Register reg,
                                             BaseIndex dest) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(dest, temps);
  storeValue(type, reg, address);
}

void MacroAssemblerRiscv64Compat::storeValue(ValueOperand val,
                                             const Address& dest) {
  storePtr(val.valueReg(), Address(dest.base, dest.offset));
}

void MacroAssemblerRiscv64Compat::storeValue(JSValueType type, Register reg,
                                             Address dest) {
  if (type == JSVAL_TYPE_INT32 || type == JSVAL_TYPE_BOOLEAN) {
#ifdef DEBUG
    {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();

      AssertValidPayload(*this, type, reg, scratch);
    }
#endif

    store32(reg, dest);
    JSValueShiftedTag tag = (JSValueShiftedTag)JSVAL_TYPE_TO_SHIFTED_TAG(type);
    store32(Imm64(tag).hi(), Address(dest.base, dest.offset + 4));
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    MOZ_ASSERT(dest.base != scratch);
    boxValue(type, reg, scratch);
    storePtr(scratch, Address(dest.base, dest.offset));
  }
}

void MacroAssemblerRiscv64Compat::storeValue(const Value& val, Address dest) {
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();
  if (val.isGCThing()) {
    CodeOffset offset = movWithPatch(ImmWord(val.asRawBits()), scratch2);
    writeDataRelocation(val, offset);
  } else {
    ma_li(scratch2, ImmWord(val.asRawBits()));
  }
  storePtr(scratch2, Address(dest.base, dest.offset));
}

void MacroAssemblerRiscv64Compat::storeValue(const Value& val, BaseIndex dest) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(dest, temps);
  storeValue(val, address);
}

void MacroAssemblerRiscv64Compat::loadValue(const BaseIndex& src,
                                            ValueOperand val) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(src, temps);
  loadValue(address, val);
}

void MacroAssemblerRiscv64Compat::loadValue(Address src, ValueOperand val) {
  loadPtr(Address(src.base, src.offset), val.valueReg());
}

void MacroAssemblerRiscv64Compat::tagValue(JSValueType type, Register payload,
                                           ValueOperand dest) {
  MOZ_ASSERT(type != JSVAL_TYPE_UNDEFINED && type != JSVAL_TYPE_NULL);

  comment("[ tagValue");

  if (payload == dest.valueReg()) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    MOZ_ASSERT(dest.valueReg() != scratch);

    AssertValidPayload(*this, type, payload, scratch);

    switch (type) {
      case JSVAL_TYPE_INT32: {
        // Loading the shifted tag requires only two instructions.
        ma_li(scratch, ImmShiftedTag(type));

        // Insert low 32 bits as payload, removing all high bits from |payload|.
        if (HasZbaExtension()) {
          add_uw(dest.valueReg(), payload, scratch);
        } else {
          ZeroExtendWord(payload, payload);
          or_(dest.valueReg(), payload, scratch);
        }
        break;
      }
      case JSVAL_TYPE_BOOLEAN:
      case JSVAL_TYPE_MAGIC:
      case JSVAL_TYPE_STRING:
      case JSVAL_TYPE_SYMBOL:
      case JSVAL_TYPE_PRIVATE_GCTHING:
      case JSVAL_TYPE_BIGINT:
      case JSVAL_TYPE_OBJECT: {
        // Loading the negated shifted tag requires at most two instructions.
        ma_li(scratch, ImmNegatedShiftedTag(type));

        // Insert payload.
        sub(dest.valueReg(), payload, scratch);
        break;
      }
      case JSVAL_TYPE_DOUBLE:
      case JSVAL_TYPE_UNDEFINED:
      case JSVAL_TYPE_NULL:
      case JSVAL_TYPE_UNKNOWN:
        MOZ_CRASH("bad value type");
    }
  } else {
    boxNonDouble(type, payload, dest);
  }

  comment("]");
}

void MacroAssemblerRiscv64Compat::pushValue(ValueOperand val) {
  // Allocate stack slots for Value. One for each.
  asMasm().subPtr(Imm32(sizeof(Value)), StackPointer);
  // Store Value
  storeValue(val, Address(StackPointer, 0));
}

void MacroAssemblerRiscv64Compat::pushValue(const Address& addr) {
  // Load value before allocate stack, addr.base may be is sp.
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  loadPtr(Address(addr.base, addr.offset), scratch);
  ma_sub64(StackPointer, StackPointer, Imm32(sizeof(Value)));
  storePtr(scratch, Address(StackPointer, 0));
}

void MacroAssemblerRiscv64Compat::popValue(ValueOperand val) {
  ld(val.valueReg(), StackPointer, 0);
  ma_add64(StackPointer, StackPointer, Imm32(sizeof(Value)));
}

void MacroAssemblerRiscv64Compat::breakpoint(uint32_t value) { break_(value); }

void MacroAssemblerRiscv64Compat::handleFailureWithHandlerTail(
    Label* profilerExitTail, Label* bailoutTail,
    uint32_t* returnValueCheckOffset) {
  // Reserve space for exception information.
  int size = (sizeof(ResumeFromException) + ABIStackAlignment) &
             ~(ABIStackAlignment - 1);
  asMasm().subPtr(Imm32(size), StackPointer);
  ma_mv(a0, StackPointer);  // Use a0 since it is a first function argument

  // Call the handler.
  using Fn = void (*)(ResumeFromException* rfe);
  asMasm().setupUnalignedABICall(a1);
  asMasm().passABIArg(a0);
  asMasm().callWithABI<Fn, HandleException>(
      ABIType::General, CheckUnsafeCallWithABI::DontCheckHasExitFrame);

  *returnValueCheckOffset = currentOffset();

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

  breakpoint();  // Invalid kind.

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
  jump(ra);
  nop();

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

  ma_mv(StackPointer, FramePointer);
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

CodeOffset MacroAssemblerRiscv64Compat::toggledJump(Label* label) {
  BufferOffset offset = BranchShort(label);
  return CodeOffset(offset.getOffset());
}

CodeOffset MacroAssemblerRiscv64Compat::toggledCall(JitCode* target,
                                                    bool enabled) {
  // 6 instruction to materialize the constant.
  // + 1 instruction for jalr/nop.
  AutoForbidPoolsAndNops afp(this, 7);

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  BufferOffset bo = ma_liPatchable(scratch, ImmPtr(target->raw()));
  addPendingJump(bo, ImmPtr(target->raw()), RelocationKind::JITCODE);
  if (enabled) {
    jalr(scratch);
  } else {
    nop();
  }
  MOZ_ASSERT_IF(!oom(), nextOffset().getOffset() - bo.getOffset() ==
                            int(ToggledCallSize(nullptr)));
  return CodeOffset(bo.getOffset());
}

void MacroAssembler::subFromStackPtr(Imm32 imm32) {
  if (imm32.value) {
    asMasm().subPtr(imm32, StackPointer);
  }
}

void MacroAssembler::clampDoubleToUint8(FloatRegister input, Register output) {
  Round_w_d(output, input);
  Clear_if_nan_d(output, input);
  clampIntToUint8(output);
}

//{{{ check_macroassembler_style
// ===============================================================
// MacroAssembler high-level usage.
bool MacroAssembler::convertUInt64ToDoubleNeedsTemp() { return false; }

CodeOffset MacroAssembler::call(Label* label) { return BranchAndLink(label); }

CodeOffset MacroAssembler::call(Register reg) {
  jalr(reg);
  return CodeOffset(currentOffset());
}

CodeOffset MacroAssembler::call(wasm::SymbolicAddress imm) {
  UseScratchRegisterScope temps(this);
  temps.Acquire(CallReg);
  movePtr(imm, CallReg);
  return call(CallReg);
}

CodeOffset MacroAssembler::farJumpWithPatch() {
  // Allocate space which will be patched by patchFarJump().
  AutoForbidPoolsAndNops afp(this, 5);

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();

  CodeOffset farJump(nextOffset().getOffset());
  auipc(scratch, 0);
  lw(scratch2, scratch, 4 * kInstrSize);
  add(scratch, scratch, scratch2);
  jr(scratch);
  comment(".space 32bit [0xffff'ffff]");
  emit(UINT32_MAX);
  return farJump;
}

CodeOffset MacroAssembler::moveNearAddressWithPatch(Register dest) {
  return movWithPatch(ImmPtr(nullptr), dest);
}

CodeOffset MacroAssembler::nopPatchableToCall() {
  // Generate a seven instruction sequence:
  // - Six instructions for WriteLiPtrInstructions.
  // - Plus one instruction for the final jalr.
  AutoForbidPoolsAndNops afp(this, 7);
  nop();  // lui(rd, (int32_t)high_20);
  nop();  // addi(rd, rd, low_12);  // 31 bits in rd.
  nop();  // slli(rd, rd, 11);      // Space for next 11 bis
  nop();  // ori(rd, rd, b11);      // 11 bits are put in. 42 bit in rd
  nop();  // slli(rd, rd, 6);       // Space for next 6 bits
  nop();  // ori(rd, rd, a6);       // 6 bits are put in. 48 bis in rd
  nop();  // jalr
  return CodeOffset(currentOffset());
}

FaultingCodeRange MacroAssembler::wasmTrapInstruction() {
  AutoForbidPoolsAndNops afp(this, 1);
  FaultingCodeRange fcr(currentOffset());
  illegal_trap(kWasmTrapCode);  // "csrwi csr_cycle, 0x6"
  return fcr;
}

size_t MacroAssembler::PushRegsInMaskSizeInBytes(LiveRegisterSet set) {
  return set.gprs().size() * sizeof(intptr_t) + set.fpus().getPushSizeInBytes();
}

template <typename T>
void MacroAssembler::branchValueIsNurseryCellImpl(Condition cond,
                                                  const T& value, Register temp,
                                                  Label* label) {
  MOZ_ASSERT(cond == Assembler::Equal || cond == Assembler::NotEqual);
  Label done;
  branchTestGCThing(Assembler::NotEqual, value,
                    cond == Assembler::Equal ? &done : label);

  // temp may be InvalidReg, use scratch2 instead.
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();

  getGCThingValueChunk(value, scratch2);
  loadPtr(Address(scratch2, gc::ChunkStoreBufferOffset), scratch2);
  branchPtr(InvertCondition(cond), scratch2, ImmWord(0), label);

  bind(&done);
}

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

//===============================
// AtomicOp

static void AtomicOffset(MacroAssembler& masm, Register address,
                         Register offsetTemp) {
  // Compute the offset shift amount.
  //
  // NB: Always used for 32-bit shifts, i.e. sllw and srlw, which implicitly
  //     bit-and the shift amount with 31. This ensures any high bits will be
  //     ignored and we don't need to perform `andi(offsetTemp, offsetTemp, 3)`.
  masm.slliw(offsetTemp, address, 3);

  // Align the address for a 32-bit word load.
  masm.andi(address, address, ~3);
}

static void AtomicMask(MacroAssembler& masm, unsigned nbytes,
                       Register offsetTemp, Register maskTemp) {
  MOZ_ASSERT(nbytes == 1 || nbytes == 2);

  // Load 0xff (nbytes=1) or 0xffff (nbytes=2) into |maskTemp|.
  masm.ma_li(maskTemp, Imm32(UINT32_MAX >> ((4 - nbytes) * 8)));

  // Move |maskTemp| into position to select the bytes from the input.
  masm.sllw(maskTemp, maskTemp, offsetTemp);
}

template <typename... RegisterPairs>
static void AtomicShiftToOffset(MacroAssembler& masm, unsigned nbytes,
                                Register offsetTemp, Register maskTemp,
                                RegisterPairs... registers) {
  MOZ_ASSERT(nbytes == 1 || nbytes == 2);

  // Prefer single instruction ZeroExtendByte/Short if possible to reduce data
  // dependencies on |offsetTemp| and |maskTemp|.
  if (nbytes == 1) {
    // Mask off other bits in the register.
    (masm.ZeroExtendByte(std::get<1>(registers), std::get<0>(registers)), ...);

    // Shift to the offset in a 32-bit word.
    (masm.sllw(std::get<1>(registers), std::get<1>(registers), offsetTemp),
     ...);
  } else {
    MOZ_ASSERT(nbytes == 2);

    if (Assembler::HasZbbExtension()) {
      // Mask off other bits in the register.
      (masm.ZeroExtendShort(std::get<1>(registers), std::get<0>(registers)),
       ...);

      // Shift to the offset in a 32-bit word.
      (masm.sllw(std::get<1>(registers), std::get<1>(registers), offsetTemp),
       ...);
    } else {
      // No single instruction available to zero other bits in the input
      // registers. Fallback to first shift and then mask.
      (masm.sllw(std::get<1>(registers), std::get<1>(registers), offsetTemp),
       ...);
      (masm.and_(std::get<1>(registers), std::get<1>(registers), maskTemp),
       ...);
    }
  }
}

// Sign- or zero-extend the result of a byte or halfword atomic operation.
static void AtomicExtendResult(MacroAssembler& masm, Scalar::Type type,
                               Register output, Register offsetTemp) {
  bool signExtend = Scalar::isSignedIntType(type);
  unsigned nbytes = Scalar::byteSize(type);
  MOZ_ASSERT(nbytes == 1 || nbytes == 2);

  // Shift |output| into its correct position.
  masm.srlw(output, output, offsetTemp);

  // Sign- or zero-extend to the format of |type|.
  switch (nbytes) {
    case 1:
      if (signExtend) {
        masm.SignExtendByte(output, output);
      } else {
        masm.ZeroExtendByte(output, output);
      }
      break;
    case 2:
      if (signExtend) {
        masm.SignExtendShort(output, output);
      } else {
        masm.ZeroExtendShort(output, output);
      }
      break;
  }
}

static FaultingCodeRange AtomicExchange(MacroAssembler& masm,
                                        const wasm::MemoryAccessDesc* access,
                                        Scalar::Type type, Synchronization sync,
                                        Register address, Register value,
                                        Register valueTemp, Register offsetTemp,
                                        Register maskTemp, Register output) {
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

  if (nbytes == 4) {
    AutoForbidPoolsAndNops afp(&masm, /* number of insns = */ 1);
    auto before = masm.currentOffset();
    FaultingCodeRange fcr(before);
    masm.amoswap_w(true, true, output, address, value);
    if (access) {
      masm.appendAndVerify(*access, wasm::TrapMachineInsn::Atomic, fcr);
    }
    return fcr;
  }

  // Align the address for a 32-bit word load and compute the shift offset.
  AtomicOffset(masm, address, offsetTemp);

  // Compute the mask to select the input bytes in a 32-bit word.
  AtomicMask(masm, nbytes, offsetTemp, maskTemp);

  // Shift |value| into its position in a 32-bit word.
  AtomicShiftToOffset(masm, nbytes, offsetTemp, maskTemp,
                      std::pair{value, valueTemp});

  // Negate mask to select all bits except the input bits.
  masm.not_(maskTemp, maskTemp);

  masm.memoryBarrierBefore(sync);

  FaultingCodeRange fcr;
  {
    // All instructions within this scope must be restricted to fulfill the
    // requirements for "constrained LR/SC loop" [1].
    //
    // 1. The instructions are limited to the I or E base instruction set.
    // 2. The total number of instructions is limited to 16.
    //
    // [1] https://riscv.github.io/riscv-isa-manual/snapshot/spec/#sec:lrscseq

    // Forbid pools to ensure all instructions are placed next to each other
    // and the total number of instructions doesn't exceed 16.
    //
    // Five instructions (lr.w, and, or, sc.w, bnez).
    AutoForbidPoolsAndNops afp(&masm, /* number of insns = */ 5);

    UseScratchRegisterScope temps(&masm);
    Register scratch2 = temps.Acquire();

    // LR/SC loop.
    Label again;
    masm.bind(&again);

    // Load the current value into |output|.
    // This may fault, so we may need to emit a TrapSite.
    auto before = masm.currentOffset();
    fcr = FaultingCodeRange(before);
    masm.lr_w(true, true, output, address);
    if (access) {
      masm.appendAndVerify(*access, wasm::TrapMachineInsn::Load32, fcr);
    }

    // Combine the loaded value with |valueTemp|.
    masm.and_(scratch2, output, maskTemp);
    masm.or_(scratch2, scratch2, valueTemp);

    // Try to store back to memory.
    masm.sc_w(true, true, scratch2, address, scratch2);

    // Store was successful iff |scratch2| is zero.
    masm.ma_b(scratch2, scratch2, &again, Assembler::NonZero, ShortJump);
  }

  masm.memoryBarrierAfter(sync);

  AtomicExtendResult(masm, type, output, offsetTemp);
  return fcr;
}

template <typename T>
static FaultingCodeRange AtomicExchange(MacroAssembler& masm,
                                        const wasm::MemoryAccessDesc* access,
                                        Scalar::Type type, Synchronization sync,
                                        const T& mem, Register value,
                                        Register valueTemp, Register offsetTemp,
                                        Register maskTemp, Register output) {
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch);

  return AtomicExchange(masm, access, type, sync, scratch, value, valueTemp,
                        offsetTemp, maskTemp, output);
}

static FaultingCodeRange AtomicExchange64(MacroAssembler& masm,
                                          const wasm::MemoryAccessDesc* access,
                                          Synchronization sync,
                                          Register address, Register64 value,
                                          Register64 output) {
  AutoForbidPoolsAndNops afp(&masm, /* number of insns = */ 1);
  auto before = masm.currentOffset();
  FaultingCodeRange fcr(before);
  masm.amoswap_d(true, true, output.reg, address, value.reg);
  if (access) {
    masm.appendAndVerify(*access, js::wasm::TrapMachineInsn::Atomic, fcr);
  }
  return fcr;
}

template <typename T>
static FaultingCodeRange AtomicExchange64(MacroAssembler& masm,
                                          const wasm::MemoryAccessDesc* access,
                                          Synchronization sync, const T& mem,
                                          Register64 value, Register64 output) {
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch);

  return AtomicExchange64(masm, access, sync, scratch, value, output);
}

static FaultingCodeRange AtomicFetchOp64(MacroAssembler& masm,
                                         const wasm::MemoryAccessDesc* access,
                                         Synchronization sync, AtomicOp op,
                                         Register64 value, Register address,
                                         Register64 output) {
  UseScratchRegisterScope temps(&masm);

  // Subtraction is implemented using "amoadd.d" with a negated operand.
  if (op == AtomicOp::Sub) {
    Register scratch2 = temps.Acquire();
    masm.neg(scratch2, value.reg);

    value = Register64(scratch2);
  }

  AutoForbidPoolsAndNops afp(&masm, /* number of insns = */ 1);
  auto before = masm.currentOffset();
  FaultingCodeRange fcr(before);

  switch (op) {
    case AtomicOp::Add:
    case AtomicOp::Sub:
      masm.amoadd_d(true, true, output.reg, address, value.reg);
      break;
    case AtomicOp::And:
      masm.amoand_d(true, true, output.reg, address, value.reg);
      break;
    case AtomicOp::Or:
      masm.amoor_d(true, true, output.reg, address, value.reg);
      break;
    case AtomicOp::Xor:
      masm.amoxor_d(true, true, output.reg, address, value.reg);
      break;
    default:
      MOZ_CRASH();
  }

  if (access) {
    masm.appendAndVerify(*access, wasm::TrapMachineInsn::Atomic, fcr);
  }

  return fcr;
}

template <typename T>
static FaultingCodeRange AtomicFetchOp64(MacroAssembler& masm,
                                         const wasm::MemoryAccessDesc* access,
                                         Synchronization sync, AtomicOp op,
                                         Register64 value, const T& mem,
                                         Register64 output) {
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch);

  return AtomicFetchOp64(masm, access, sync, op, value, scratch, output);
}

static FaultingCodeRange AtomicFetchOrEffectOp(
    MacroAssembler& masm, const wasm::MemoryAccessDesc* access,
    Scalar::Type type, Synchronization sync, AtomicOp op, Register address,
    Register value, Register valueTemp, Register offsetTemp, Register maskTemp,
    Register output) {
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

  if (nbytes == 4) {
    UseScratchRegisterScope temps(&masm);

    // Subtraction is implemented using "amoadd.w" with a negated operand.
    if (op == AtomicOp::Sub) {
      Register scratch2 = temps.Acquire();
      masm.negw(scratch2, value);

      value = scratch2;
    }

    AutoForbidPoolsAndNops afp(&masm, /* number of insns = */ 1);
    auto before = masm.currentOffset();

    switch (op) {
      case AtomicOp::Add:
      case AtomicOp::Sub:
        masm.amoadd_w(true, true, output, address, value);
        break;
      case AtomicOp::And:
        masm.amoand_w(true, true, output, address, value);
        break;
      case AtomicOp::Or:
        masm.amoor_w(true, true, output, address, value);
        break;
      case AtomicOp::Xor:
        masm.amoxor_w(true, true, output, address, value);
        break;
      default:
        MOZ_CRASH();
    }

    FaultingCodeRange fcr(before);
    if (access) {
      masm.appendAndVerify(*access, wasm::TrapMachineInsn::Atomic, fcr);
    }

    return fcr;
  }

  // Align the address for a 32-bit word load and compute the offset.
  AtomicOffset(masm, address, offsetTemp);

  // Determine if mask needs to be computed.
  bool needAtomicMask;
  switch (op) {
    case AtomicOp::Add:
    case AtomicOp::Sub:
    case AtomicOp::And:
      // Mask always needed.
      needAtomicMask = true;
      break;
    case AtomicOp::Or:
    case AtomicOp::Xor:
      // Mask needed in AtomicShiftToOffset to zero-extend halfwords when Zbb
      // extension isn't supported.
      needAtomicMask = nbytes == 2 && !Assembler::HasZbbExtension();
      break;
    default:
      MOZ_CRASH();
  }

  // Compute the mask to select the input bytes in a 32-bit word.
  if (needAtomicMask) {
    AtomicMask(masm, nbytes, offsetTemp, maskTemp);
  }

  // Shift |value| into its position in a 32-bit word.
  AtomicShiftToOffset(masm, nbytes, offsetTemp, maskTemp,
                      std::pair{value, valueTemp});

  // Bitwise operations can be implemented using bitwise amo instructions.
  if (op == AtomicOp::And || op == AtomicOp::Or || op == AtomicOp::Xor) {
    // Set all other bits to 1s for bit-and.
    // Set all other bits to 0s for bit-or/xor (implicit).
    if (op == AtomicOp::And) {
      if (Assembler::HasZbbExtension()) {
        masm.orn(valueTemp, valueTemp, maskTemp);
      } else {
        masm.not_(maskTemp, maskTemp);
        masm.or_(valueTemp, valueTemp, maskTemp);
      }
    }

    FaultingCodeRange fcr;
    {
      AutoForbidPoolsAndNops afp(&masm, /* number of insns = */ 1);
      auto before = masm.currentOffset();

      switch (op) {
        case AtomicOp::And:
          masm.amoand_w(true, true, output, address, valueTemp);
          break;
        case AtomicOp::Or:
          masm.amoor_w(true, true, output, address, valueTemp);
          break;
        case AtomicOp::Xor:
          masm.amoxor_w(true, true, output, address, valueTemp);
          break;
        default:
          MOZ_CRASH();
      }

      fcr = FaultingCodeRange(before);
      if (access) {
        masm.appendAndVerify(*access, wasm::TrapMachineInsn::Atomic, fcr);
      }
    }

    if (output != zero_reg) {
      AtomicExtendResult(masm, type, output, offsetTemp);
    }
    return fcr;
  }

  masm.memoryBarrierBefore(sync);

  FaultingCodeRange fcr;
  {
    // All instructions within this scope must be restricted to fulfill the
    // requirements for "constrained LR/SC loop" [1].
    //
    // 1. The instructions are limited to the I or E base instruction set.
    // 2. The total number of instructions is limited to 16.
    //
    // [1] https://riscv.github.io/riscv-isa-manual/snapshot/spec/#sec:lrscseq

    // Forbid pools to ensure all instructions are placed next to each other
    // and the total number of instructions doesn't exceed 16.
    //
    // Seven instructions (lr.w, addw/subw, xor, and, xor, sc.w, bnez).
    AutoForbidPoolsAndNops afp(&masm, /* number of insns = */ 7);

    UseScratchRegisterScope temps(&masm);
    Register scratch2 = temps.Acquire();
    Register current = output;
    if (current == zero_reg) {
      current = temps.Acquire();
    }

    // LR/SC loop.
    Label again;
    masm.bind(&again);

    // Load the current value into |current|.
    // This may fault, so we may need to emit a TrapSite.
    auto before = masm.currentOffset();
    masm.lr_w(true, true, current, address);
    fcr = FaultingCodeRange(before);
    if (access) {
      masm.appendAndVerify(*access, wasm::TrapMachineInsn::Load32, fcr);
    }

    // Apply the operation.
    switch (op) {
      case AtomicOp::Add:
        masm.addw(scratch2, current, valueTemp);
        break;
      case AtomicOp::Sub:
        masm.subw(scratch2, current, valueTemp);
        break;
      default:
        MOZ_CRASH();
    }

    // Insert the result into the loaded value.
    masm.xor_(scratch2, scratch2, current);
    masm.and_(scratch2, scratch2, maskTemp);
    masm.xor_(scratch2, scratch2, current);

    // Try to store back to memory.
    masm.sc_w(true, true, scratch2, address, scratch2);

    // Store was successful iff |scratch2| is zero.
    masm.ma_b(scratch2, scratch2, &again, Assembler::NonZero, ShortJump);
  }

  masm.memoryBarrierAfter(sync);

  if (output != zero_reg) {
    AtomicExtendResult(masm, type, output, offsetTemp);
  }

  return fcr;
}

static FaultingCodeRange AtomicFetchOp(MacroAssembler& masm,
                                       const wasm::MemoryAccessDesc* access,
                                       Scalar::Type type, Synchronization sync,
                                       AtomicOp op, Register address,
                                       Register value, Register valueTemp,
                                       Register offsetTemp, Register maskTemp,
                                       Register output) {
  return AtomicFetchOrEffectOp(masm, access, type, sync, op, address, value,
                               valueTemp, offsetTemp, maskTemp, output);
}

template <typename T>
static FaultingCodeRange AtomicFetchOp(MacroAssembler& masm,
                                       const wasm::MemoryAccessDesc* access,
                                       Scalar::Type type, Synchronization sync,
                                       AtomicOp op, const T& mem,
                                       Register value, Register valueTemp,
                                       Register offsetTemp, Register maskTemp,
                                       Register output) {
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch);

  return AtomicFetchOp(masm, access, type, sync, op, scratch, value, valueTemp,
                       offsetTemp, maskTemp, output);
}

static FaultingCodeRange AtomicEffectOp(MacroAssembler& masm,
                                        const wasm::MemoryAccessDesc* access,
                                        Scalar::Type type, Synchronization sync,
                                        AtomicOp op, Register address,
                                        Register value, Register valueTemp,
                                        Register offsetTemp,
                                        Register maskTemp) {
  return AtomicFetchOrEffectOp(masm, access, type, sync, op, address, value,
                               valueTemp, offsetTemp, maskTemp, zero_reg);
}

template <typename T>
static FaultingCodeRange AtomicEffectOp(MacroAssembler& masm,
                                        const wasm::MemoryAccessDesc* access,
                                        Scalar::Type type, Synchronization sync,
                                        AtomicOp op, const T& mem,
                                        Register value, Register valueTemp,
                                        Register offsetTemp,
                                        Register maskTemp) {
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch);

  return AtomicEffectOp(masm, access, type, sync, op, scratch, value, valueTemp,
                        offsetTemp, maskTemp);
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

void MacroAssembler::atomicExchange64(Synchronization sync, const Address& mem,
                                      Register64 value, Register64 output) {
  AtomicExchange64(*this, nullptr, sync, mem, value, output);
}

void MacroAssembler::atomicExchange64(Synchronization sync,
                                      const BaseIndex& mem, Register64 value,
                                      Register64 output) {
  AtomicExchange64(*this, nullptr, sync, mem, value, output);
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

// ========================================================================
// JS atomic operations.

void MacroAssembler::atomicPause() {
  // `pause` hint defined in Zihintpause extension.
  // It is encoded as `fence w, 0`.
  fence(0b0001, 0b0000);
}

// ===============================================================
// Branch functions

void MacroAssembler::branchPtrInNurseryChunk(Condition cond, Register ptr,
                                             Register temp, Label* label) {
  MOZ_ASSERT(cond == Assembler::Equal || cond == Assembler::NotEqual);
  MOZ_ASSERT(ptr != temp);
  MOZ_ASSERT(temp != InvalidReg);

  ma_and(temp, ptr, Imm32(int32_t(~gc::ChunkMask)));
  branchPtr(InvertCondition(cond), Address(temp, gc::ChunkStoreBufferOffset),
            zero, label);
}
void MacroAssembler::branchTestValue(Condition cond, const ValueOperand& lhs,
                                     const Value& rhs, Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  MOZ_ASSERT(!rhs.isNaN());

  if (!rhs.isGCThing()) {
    ma_b(lhs.valueReg(), ImmWord(rhs.asRawBits()), label, cond, ShortJump);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    MOZ_ASSERT(lhs.valueReg() != scratch);
    moveValue(rhs, ValueOperand(scratch));
    ma_b(lhs.valueReg(), scratch, label, cond, ShortJump);
  }
}

void MacroAssembler::branchTestNaNValue(Condition cond, const ValueOperand& val,
                                        Register temp, Label* label) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(val.valueReg() != scratch);

  // When testing for NaN, we want to ignore the sign bit.
  if (HasZbsExtension()) {
    bclri(temp, val.valueReg(), 63);
  } else {
    // Clear the top bit by shifting left and then right.
    slli(temp, val.valueReg(), 1);
    srli(temp, temp, 1);
  }

  // Compare against a NaN with sign bit 0.
  static_assert(JS::detail::CanonicalizedNaNSignBit == 0);
  moveValue(DoubleValue(JS::GenericNaN()), ValueOperand(scratch));
  ma_b(temp, scratch, label, cond, ShortJump);
}

void MacroAssembler::testValueSet(Condition cond, const ValueOperand& lhs,
                                  const Value& rhs, Register dest) {
  MOZ_ASSERT(cond == Equal || cond == NotEqual);
  MOZ_ASSERT(!rhs.isNaN());

  if (!rhs.isGCThing()) {
    cmpPtrSet(cond, lhs.valueReg(), ImmWord(rhs.asRawBits()), dest);
  } else {
    UseScratchRegisterScope temps(asMasm());
    Register scratch = temps.Acquire();
    MOZ_ASSERT(lhs.valueReg() != scratch);
    moveValue(rhs, ValueOperand(scratch));
    cmpPtrSet(cond, lhs.valueReg(), scratch, dest);
  }
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

CodeOffset MacroAssembler::call(const Address& addr) {
  UseScratchRegisterScope temps(this);
  temps.Acquire(CallReg);
  loadPtr(addr, CallReg);
  return call(CallReg);
}

void MacroAssembler::call(ImmPtr imm) {
  BufferOffset bo = ma_call(imm);
  addPendingJump(bo, imm, RelocationKind::HARDCODED);
}

void MacroAssembler::call(ImmWord imm) { call(ImmPtr((void*)imm.value)); }

void MacroAssembler::call(JitCode* c) {
  // 6 instruction to materialize the constant.
  // + 1 instructions for the call.
  AutoForbidPoolsAndNops afp(this, 7);

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  BufferOffset bo = ma_liPatchable(scratch, ImmPtr(c->raw()));
  addPendingJump(bo, ImmPtr(c->raw()), RelocationKind::JITCODE);
  callJitNoProfiler(scratch);
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

    MoveEmitter emitter(asMasm());
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
  // Load the callee in scratch2, no instruction between the movePtr and
  // call should clobber it. Note that we can't use fun because it may be
  // one of the IntArg registers clobbered before the call.
  UseScratchRegisterScope temps(this);
  temps.Acquire(CallReg);
  movePtr(fun, CallReg);

  uint32_t stackAdjust;
  callWithABIPre(&stackAdjust);
  call(CallReg);
  callWithABIPost(stackAdjust, result);
}

void MacroAssembler::callWithABINoProfiler(const Address& fun, ABIType result) {
  // Load the callee in scratch2, as above.
  UseScratchRegisterScope temps(this);
  temps.Acquire(CallReg);
  loadPtr(fun, CallReg);

  uint32_t stackAdjust;
  callWithABIPre(&stackAdjust);
  call(CallReg);
  callWithABIPost(stackAdjust, result);
}

void MacroAssembler::ceilDoubleToInt32(FloatRegister src, Register dest,
                                       Label* fail) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  // Round toward positive infinity.
  Ceil_l_d(dest, src);

  // Sign extend lower 32 bits to test if the result isn't an Int32.
  {
    move32SignExtendToPtr(dest, scratch);
    branchPtr(Assembler::NotEqual, dest, scratch, fail);
  }

  // We have to check for (-1, -0] when the result is zero.
  Label notZero;
  ma_b(dest, zero, &notZero, Assembler::NotEqual, ShortJump);
  {
    fmv_x_d(scratch, src);
    ma_b(scratch, scratch, fail, Assembler::Signed, ShortJump);
  }
  bind(&notZero);
}

void MacroAssembler::ceilFloat32ToInt32(FloatRegister src, Register dest,
                                        Label* fail) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  // Round toward positive infinity.
  Ceil_l_s(dest, src);

  // Sign extend lower 32 bits to test if the result isn't an Int32.
  {
    move32SignExtendToPtr(dest, scratch);
    branchPtr(Assembler::NotEqual, dest, scratch, fail);
  }

  // We have to check for (-1, -0] when the result is zero.
  Label notZero;
  ma_b(dest, zero, &notZero, Assembler::NotEqual, ShortJump);
  {
    fmv_x_w(scratch, src);
    ma_b(scratch, scratch, fail, Assembler::Signed, ShortJump);
  }
  bind(&notZero);
}

void MacroAssembler::comment(const char* msg) { Assembler::comment(msg); }

static FaultingCodeRange CompareExchange64(MacroAssembler& masm,
                                           const wasm::MemoryAccessDesc* access,
                                           Synchronization sync,
                                           Register address, Register64 expect,
                                           Register64 replace,
                                           Register64 output) {
  MOZ_ASSERT(expect != output && replace != output);

  masm.memoryBarrierBefore(sync);

  Label exit;
  FaultingCodeRange fcr;
  {
    // All instructions within this scope must be restricted to fulfill the
    // requirements for "constrained LR/SC loop" [1].
    //
    // 1. The instructions are limited to the I or E base instruction set.
    // 2. The total number of instructions is limited to 16.
    //
    // [1] https://riscv.github.io/riscv-isa-manual/snapshot/spec/#sec:lrscseq

    // Forbid pools to ensure all instructions are placed next to each other
    // and the total number of instructions doesn't exceed 16.
    //
    // Four instructions (lr.d, bne, sc.d, bnez), plus one new deadline for the
    // forward branch.
    AutoForbidPoolsAndNops afp(&masm, /* number of insns = */ 4, 1);

    UseScratchRegisterScope temps(&masm);
    Register scratch2 = temps.Acquire();

    // LR/SC loop.
    Label tryAgain;
    masm.bind(&tryAgain);

    // Load the current value into |output|.
    // This may fault, so we may need to emit a TrapSite.
    auto before = masm.currentOffset();
    masm.lr_d(true, true, output.reg, address);
    fcr = FaultingCodeRange(before);
    if (access) {
      masm.appendAndVerify(*access, wasm::TrapMachineInsn::Load64, fcr);
    }

    // Return if the current value isn't equal to |expect|.
    masm.ma_b(output.reg, expect.reg, &exit, Assembler::NotEqual, ShortJump);

    // Otherwise try to store |replace|.
    masm.sc_d(true, true, scratch2, address, replace.reg);

    // Store was successful iff |scratch2| is zero.
    masm.ma_b(scratch2, scratch2, &tryAgain, Assembler::NonZero, ShortJump);
  }

  // Barrier only needed after successful store.
  masm.memoryBarrierAfter(sync);

  masm.bind(&exit);
  return fcr;
}

template <typename T>
static FaultingCodeRange CompareExchange64(MacroAssembler& masm,
                                           const wasm::MemoryAccessDesc* access,
                                           Synchronization sync, const T& mem,
                                           Register64 expect,
                                           Register64 replace,
                                           Register64 output) {
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch);

  return CompareExchange64(masm, access, sync, scratch, expect, replace,
                           output);
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

void MacroAssembler::convertInt64ToDouble(Register64 src, FloatRegister dest) {
  fcvt_d_l(dest, src.scratchReg());
}

void MacroAssembler::convertInt64ToFloat32(Register64 src, FloatRegister dest) {
  fcvt_s_l(dest, src.scratchReg());
}

void MacroAssembler::convertIntPtrToDouble(Register src, FloatRegister dest) {
  fcvt_d_l(dest, src);
}

void MacroAssembler::convertUInt64ToDouble(Register64 src, FloatRegister dest,
                                           Register temp) {
  fcvt_d_lu(dest, src.scratchReg());
}

void MacroAssembler::convertUInt64ToFloat32(Register64 src, FloatRegister dest,
                                            Register temp) {
  fcvt_s_lu(dest, src.scratchReg());
}

void MacroAssembler::copySignDouble(FloatRegister lhs, FloatRegister rhs,
                                    FloatRegister output) {
  fsgnj_d(output, lhs, rhs);
}

void MacroAssembler::copySignFloat32(FloatRegister lhs, FloatRegister rhs,
                                     FloatRegister output) {
  fsgnj_s(output, lhs, rhs);
}

void MacroAssembler::enterFakeExitFrameForWasm(Register cxreg, Register scratch,
                                               ExitFrameType type) {
  enterFakeExitFrame(cxreg, scratch, type);
}

CodeOffset MacroAssembler::sub32FromMemAndBranchIfNegativeWithPatch(
    Address address, Label* label) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(scratch != address.base);
  ma_load(scratch, address);
  // 128 is arbitrary, but makes `*address` count upwards, which may help to
  // identify cases where the subsequent ::patch..() call was forgotten.
  addiw(scratch, scratch, 128);
  // Points immediately after the instruction to patch.
  CodeOffset patchPoint = CodeOffset(currentOffset());
  ma_store(scratch, address);
  ma_b(scratch, scratch, label, Assembler::Signed, LongJump);
  return patchPoint;
}

void MacroAssembler::patchSub32FromMemAndBranchIfNegative(CodeOffset offset,
                                                          Imm32 imm) {
  int32_t val = imm.value;

  // Patching it to zero would make the instruction pointless.
  MOZ_RELEASE_ASSERT(val >= 1 && val <= 127);

  auto* inst = getInstructionAt(BufferOffset(offset.offset() - kInstrSize));
  MOZ_ASSERT(inst->IsAddiw());
  inst->SetImm12Value(-val);
}

void MacroAssembler::flexibleDivMod32(Register lhs, Register rhs,
                                      Register divOutput, Register remOutput,
                                      bool isUnsigned, const LiveRegisterSet&) {
  MOZ_ASSERT(lhs != divOutput && lhs != remOutput, "lhs is preserved");
  MOZ_ASSERT(rhs != divOutput && rhs != remOutput, "rhs is preserved");

  // The recommended code sequence to obtain both the quotient and remainder
  // is div[u] followed by mod[u].
  if (isUnsigned) {
    divuw(divOutput, lhs, rhs);
    remuw(remOutput, lhs, rhs);
  } else {
    divw(divOutput, lhs, rhs);
    remw(remOutput, lhs, rhs);
  }
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

void MacroAssembler::floorDoubleToInt32(FloatRegister src, Register dest,
                                        Label* fail) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  // Round toward negative infinity.
  Floor_l_d(dest, src);

  // Sign extend lower 32 bits to test if the result isn't an Int32.
  {
    move32SignExtendToPtr(dest, scratch);
    branchPtr(Assembler::NotEqual, dest, scratch, fail);
  }

  // Fail if the input is negative zero.
  {
    fclass_d(scratch, src);
    ma_b(scratch, Imm32(FClassFlag::kNegativeZero), fail, Equal, ShortJump);
  }
}

void MacroAssembler::floorFloat32ToInt32(FloatRegister src, Register dest,
                                         Label* fail) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  // Round toward negative infinity.
  Floor_l_s(dest, src);

  // Sign extend lower 32 bits to test if the result isn't an Int32.
  {
    move32SignExtendToPtr(dest, scratch);
    branchPtr(Assembler::NotEqual, dest, scratch, fail);
  }

  // Fail if the input is negative zero.
  {
    fclass_s(scratch, src);
    ma_b(scratch, Imm32(FClassFlag::kNegativeZero), fail, Equal, ShortJump);
  }
}

void MacroAssembler::flush() { Assembler::flush(); }

void MacroAssembler::loadStoreBuffer(Register ptr, Register buffer) {
  ma_and(buffer, ptr, Imm32(int32_t(~gc::ChunkMask)));
  loadPtr(Address(buffer, gc::ChunkStoreBufferOffset), buffer);
}

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

  CodeOffset offset = movWithPatch(ImmWord(src.asRawBits()), dest.valueReg());
  writeDataRelocation(src, offset);
}

void MacroAssembler::nearbyIntDouble(RoundingMode mode, FloatRegister src,
                                     FloatRegister dest) {
  MOZ_ASSERT(HasRoundInstruction(mode));

  switch (mode) {
    case RoundingMode::Down:
      Floor_d_d(dest, src);
      break;
    case RoundingMode::Up:
      Ceil_d_d(dest, src);
      break;
    case RoundingMode::NearestTiesToEven:
      Round_d_d(dest, src);
      break;
    case RoundingMode::TowardsZero:
      Trunc_d_d(dest, src);
      break;
  }
}

void MacroAssembler::nearbyIntFloat32(RoundingMode mode, FloatRegister src,
                                      FloatRegister dest) {
  MOZ_ASSERT(HasRoundInstruction(mode));

  switch (mode) {
    case RoundingMode::Down:
      Floor_s_s(dest, src);
      break;
    case RoundingMode::Up:
      Ceil_s_s(dest, src);
      break;
    case RoundingMode::NearestTiesToEven:
      Round_s_s(dest, src);
      break;
    case RoundingMode::TowardsZero:
      Trunc_s_s(dest, src);
      break;
  }
}

void MacroAssembler::oolWasmTruncateCheckF32ToI32(
    FloatRegister input, Register output, TruncFlags flags,
    const wasm::TrapSiteDesc& trapSiteDesc, Label* rejoin,
    wasm::StackMap* stackMapForTraps,
    wasm::StackMapRegistry* stackMapRegistry) {
  MOZ_ASSERT(!(flags & TRUNC_SATURATING));

  Label notNaN;
  BranchFloat32(Assembler::DoubleOrdered, input, input, &notNaN, ShortJump);
  FaultingCodeRange fcr1 =
      wasmTrap(wasm::Trap::InvalidConversionToInteger, trapSiteDesc);
  if (stackMapRegistry) {
    propagateOOM(stackMapRegistry->addMap(stackMapForTraps, fcr1));
  }
  bind(&notNaN);

  FaultingCodeRange fcr2 = wasmTrap(wasm::Trap::IntegerOverflow, trapSiteDesc);
  if (stackMapRegistry) {
    propagateOOM(stackMapRegistry->addMap(stackMapForTraps, fcr2));
  }
}

void MacroAssembler::oolWasmTruncateCheckF64ToI32(
    FloatRegister input, Register output, TruncFlags flags,
    const wasm::TrapSiteDesc& trapSiteDesc, Label* rejoin,
    wasm::StackMap* stackMapForTraps,
    wasm::StackMapRegistry* stackMapRegistry) {
  MOZ_ASSERT(!(flags & TRUNC_SATURATING));

  Label notNaN;
  BranchFloat64(Assembler::DoubleOrdered, input, input, &notNaN, ShortJump);
  FaultingCodeRange fcr1 =
      wasmTrap(wasm::Trap::InvalidConversionToInteger, trapSiteDesc);
  if (stackMapRegistry) {
    propagateOOM(stackMapRegistry->addMap(stackMapForTraps, fcr1));
  }
  bind(&notNaN);

  FaultingCodeRange fcr2 = wasmTrap(wasm::Trap::IntegerOverflow, trapSiteDesc);
  if (stackMapRegistry) {
    propagateOOM(stackMapRegistry->addMap(stackMapForTraps, fcr2));
  }
}

void MacroAssembler::oolWasmTruncateCheckF32ToI64(
    FloatRegister input, Register64 output, TruncFlags flags,
    const wasm::TrapSiteDesc& trapSiteDesc, Label* rejoin,
    wasm::StackMap* stackMapForTraps,
    wasm::StackMapRegistry* stackMapRegistry) {
  MOZ_ASSERT(!(flags & TRUNC_SATURATING));

  Label notNaN;
  BranchFloat32(Assembler::DoubleOrdered, input, input, &notNaN, ShortJump);
  FaultingCodeRange fcr1 =
      wasmTrap(wasm::Trap::InvalidConversionToInteger, trapSiteDesc);
  if (stackMapRegistry) {
    propagateOOM(stackMapRegistry->addMap(stackMapForTraps, fcr1));
  }
  bind(&notNaN);

  FaultingCodeRange fcr2 = wasmTrap(wasm::Trap::IntegerOverflow, trapSiteDesc);
  if (stackMapRegistry) {
    propagateOOM(stackMapRegistry->addMap(stackMapForTraps, fcr2));
  }
}

void MacroAssembler::oolWasmTruncateCheckF64ToI64(
    FloatRegister input, Register64 output, TruncFlags flags,
    const wasm::TrapSiteDesc& trapSiteDesc, Label* rejoin,
    wasm::StackMap* stackMapForTraps,
    wasm::StackMapRegistry* stackMapRegistry) {
  MOZ_ASSERT(!(flags & TRUNC_SATURATING));

  Label notNaN;
  BranchFloat64(Assembler::DoubleOrdered, input, input, &notNaN, ShortJump);
  FaultingCodeRange fcr1 =
      wasmTrap(wasm::Trap::InvalidConversionToInteger, trapSiteDesc);
  if (stackMapRegistry) {
    propagateOOM(stackMapRegistry->addMap(stackMapForTraps, fcr1));
  }
  bind(&notNaN);

  FaultingCodeRange fcr2 = wasmTrap(wasm::Trap::IntegerOverflow, trapSiteDesc);
  if (stackMapRegistry) {
    propagateOOM(stackMapRegistry->addMap(stackMapForTraps, fcr2));
  }
}

void MacroAssembler::patchCallToNop(uint8_t* call) {
  // See nopPatchableToCall() for the expected code layout.

  Instruction* instr = Instruction::At(call - 7 * kInstrSize);
  (instr + 0 * kInstrSize)->SetNop();
  (instr + 1 * kInstrSize)->SetNop();
  (instr + 2 * kInstrSize)->SetNop();
  (instr + 3 * kInstrSize)->SetNop();
  (instr + 4 * kInstrSize)->SetNop();
  (instr + 5 * kInstrSize)->SetNop();
  (instr + 6 * kInstrSize)->SetNop();
}

CodeOffset MacroAssembler::callWithPatch() {
  AutoForbidPoolsAndNops afp(this, 2);

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  auto [Hi20, Lo12] = ToHigh20Low12(0);
  auipc(scratch, Hi20);     // Read PC + Hi20 into scratch.
  jalr(scratch, Lo12, {});  // jump PC + Hi20 + Lo12
  return CodeOffset(currentOffset());
}

void MacroAssembler::patchCall(uint32_t callerOffset, uint32_t calleeOffset) {
  DEBUG_PRINTF("\tpatchCall\n");

  BufferOffset call(callerOffset - 2 * kInstrSize);
  DEBUG_PRINTF("\tcallerOffset %d\n", callerOffset);

  int32_t offset = BufferOffset(calleeOffset).getOffset() - call.getOffset();

  Instruction* auipc_ = getInstructionAt(call);
  Instruction* jalr_ =
      getInstructionAt(BufferOffset(callerOffset - 1 * kInstrSize));

  DEBUG_PRINTF("\t%p %u\n\t", auipc_, callerOffset - 2 * kInstrSize);
  disassembleInstr(auipc_);
  DEBUG_PRINTF("\t%p %u\n\t", jalr_, callerOffset - 1 * kInstrSize);

  disassembleInstr(jalr_);
  DEBUG_PRINTF("\t\n");

  MOZ_ASSERT(jalr_->IsJalr() && auipc_->IsAuipc());
  MOZ_ASSERT(auipc_->RdValue() == jalr_->Rs1Value());

  auto [Hi20, Lo12] = ToHigh20Low12(offset);

  auipc_->SetImm20UValue(Hi20);
  jalr_->SetImm12Value(Lo12);
}

void MacroAssembler::patchFarJump(CodeOffset farJump, uint32_t targetOffset) {
  // See farJumpWithPatch for the expected code layout:
  //   auipc        ; farJump
  //   lw
  //   add
  //   jr
  //   <immediate>  ; farJump + 4 * kInstrSize
  Instruction* inst =
      getInstructionAt(BufferOffset(farJump.offset() + 4 * kInstrSize));

  int64_t distance = int64_t(targetOffset) - int64_t(farJump.offset());

  MOZ_ASSERT(inst->InstructionBits() == int32_t(UINT32_MAX));
  inst->SetInstructionBits(mozilla::AssertedCast<int32_t>(distance));
}

void MacroAssembler::patchFarJump(uint8_t* farJump, uint8_t* target) {
  // See farJumpWithPatch for the expected code layout:
  //   auipc        ; farJump
  //   lw
  //   add
  //   jr
  //   <immediate>  ; farJump + 4 * kInstrSize
  Instruction* inst = Instruction::At(farJump + 4 * kInstrSize);

  int64_t distance = int64_t(target) - int64_t(farJump);

  MOZ_ASSERT(inst->InstructionBits() == int32_t(UINT32_MAX));
  inst->SetInstructionBits(mozilla::AssertedCast<int32_t>(distance));
}

void MacroAssembler::patchNearAddressMove(CodeLocationLabel loc,
                                          CodeLocationLabel target) {
  PatchDataWithValueCheck(loc, ImmPtr(target.raw()), ImmPtr(nullptr));
}

void MacroAssembler::patchNopToCall(uint8_t* call, uint8_t* target) {
  // See nopPatchableToCall() for the expected code layout.

  // Write |target| to the six instruction sequence starting at |instr|.
  Instruction* instr = Instruction::At(call - 7 * kInstrSize);
  Assembler::WriteLiPtrInstructions(instr, SavedScratchRegister,
                                    uintptr_t(target));

  Instruction* jalr = (instr + 6 * kInstrSize);
  jalr->SetIFormat(RO_JALR, ra.code(), SavedScratchRegister.code(), 0);
}
void MacroAssembler::Pop(Register reg) {
  pop(reg);
  adjustFrame(-int32_t(sizeof(intptr_t)));
}

void MacroAssembler::Pop(FloatRegister t) {
  pop(t);
  // See MacroAssemblerRiscv64::ma_pop(FloatRegister) for why we use
  // sizeof(double).
  adjustFrame(-int32_t(sizeof(double)));
}

void MacroAssembler::Pop(const ValueOperand& val) {
  popValue(val);
  adjustFrame(-int32_t(sizeof(Value)));
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

#ifdef ENABLE_JIT_SIMD
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

CodeOffset MacroAssembler::move32WithPatch(Register dest) {
  BufferOffset offset = ma_liPatchable(dest, Imm32(0));
  return CodeOffset(offset.getOffset());
}

void MacroAssembler::patchMove32(CodeOffset offset, Imm32 n) {
  patchLi32(offset, n);
}

void MacroAssembler::pushReturnAddress() { push(ra); }

void MacroAssembler::popReturnAddress() { pop(ra); }
void MacroAssembler::PopStackPtr() {
  loadPtr(Address(StackPointer, 0), StackPointer);
  adjustFrame(-int32_t(sizeof(intptr_t)));
}
void MacroAssembler::freeStackTo(uint32_t framePushed) {
  MOZ_ASSERT(framePushed <= framePushed_);
  ma_sub64(StackPointer, FramePointer, Imm32(framePushed));
  framePushed_ = framePushed;
}
void MacroAssembler::PushBoxed(FloatRegister reg) {
  subFromStackPtr(Imm32(sizeof(double)));
  boxDouble(reg, Address(getStackPointer(), 0));
  adjustFrame(sizeof(double));
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

void MacroAssembler::Push(FloatRegister reg) {
  push(reg);
  // See MacroAssemblerRiscv64::ma_push(FloatRegister) for why we use
  // sizeof(double).
  adjustFrame(int32_t(sizeof(double)));
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

#ifdef ENABLE_JIT_SIMD
#  error "Needs more careful logic if SIMD is enabled"
#endif

  for (FloatRegisterBackwardIterator iter(set.fpus().reduceSetForPush());
       iter.more(); ++iter) {
    diff -= sizeof(double);
    storeDouble(*iter, Address(StackPointer, diff));
  }
  MOZ_ASSERT(diff == 0);
}

void MacroAssembler::roundFloat32ToInt32(FloatRegister src, Register dest,
                                         FloatRegister temp, Label* fail) {
  Label negative, done;

  // Branch to a slow path if input < 0.0 due to complicated rounding rules.
  {
    loadConstantFloat32(0.0f, temp);
    BranchFloat32(Assembler::DoubleLessThan, src, temp, &negative, ShortJump);
  }

  // Fail if the input is negative zero.
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();

    fclass_s(scratch, src);
    ma_b(scratch, Imm32(FClassFlag::kNegativeZero), fail, Assembler::Equal,
         ShortJump);
  }

  // Handle the simple case of a positive input and NaN.
  // Rounding proceeds with consideration of the fractional part of the input:
  // 1. If > 0.5, round to integer with higher absolute value (so, up).
  // 2. If < 0.5, round to integer with lower absolute value (so, down).
  // 3. If = 0.5, round to +Infinity (so, up).
  {
    // Round, ties away from zero.
    RoundMaxMag_l_s(dest, src);
    jump(&done);
  }

  // Handle the complicated case of a negative input.
  // Rounding proceeds with consideration of the fractional part of the input:
  // 1. If > 0.5, round to integer with higher absolute value (so, down).
  // 2. If < 0.5, round to integer with lower absolute value (so, up).
  // 3. If = 0.5, round to +Infinity (so, up).
  bind(&negative);
  {
    // Inputs in [-0.5, 0) are rounded to -0. Fail.
    loadConstantFloat32(-0.5f, temp);
    branchFloat(Assembler::DoubleGreaterThanOrEqual, src, temp, fail);

    // Other negative inputs need the biggest float less than 0.5 added.
    loadConstantFloat32(GetBiggestNumberLessThan(0.5f), temp);
    fadd_s(temp, src, temp);

    // Round toward negative infinity.
    Floor_l_s(dest, temp);
  }

  // Sign extend lower 32 bits to test if the result isn't an Int32.
  bind(&done);
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();

    move32SignExtendToPtr(dest, scratch);
    branchPtr(Assembler::NotEqual, dest, scratch, fail);
  }
}

void MacroAssembler::roundDoubleToInt32(FloatRegister src, Register dest,
                                        FloatRegister temp, Label* fail) {
  Label negative, done;

  // Branch to a slow path if input < 0.0 due to complicated rounding rules.
  {
    loadConstantDouble(0.0, temp);
    BranchFloat64(Assembler::DoubleLessThan, src, temp, &negative, ShortJump);
  }

  // Fail if the input is negative zero.
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();

    fclass_d(scratch, src);
    ma_b(scratch, Imm32(FClassFlag::kNegativeZero), fail, Equal, ShortJump);
  }

  // Handle the simple case of a positive input and NaN.
  // Rounding proceeds with consideration of the fractional part of the input:
  // 1. If > 0.5, round to integer with higher absolute value (so, up).
  // 2. If < 0.5, round to integer with lower absolute value (so, down).
  // 3. If = 0.5, round to +Infinity (so, up).
  {
    // Round, ties away from zero.
    RoundMaxMag_l_d(dest, src);
    jump(&done);
  }

  // Handle the complicated case of a negative input.
  // Rounding proceeds with consideration of the fractional part of the input:
  // 1. If > 0.5, round to integer with higher absolute value (so, down).
  // 2. If < 0.5, round to integer with lower absolute value (so, up).
  // 3. If = 0.5, round to +Infinity (so, up).
  bind(&negative);
  {
    // Inputs in [-0.5, 0) are rounded to -0. Fail.
    loadConstantDouble(-0.5, temp);
    branchDouble(Assembler::DoubleGreaterThanOrEqual, src, temp, fail);

    // Other negative inputs need the biggest double less than 0.5 added.
    loadConstantDouble(GetBiggestNumberLessThan(0.5), temp);
    fadd_d(temp, src, temp);

    // Round toward negative infinity.
    Floor_l_d(dest, temp);
  }

  // Sign extend lower 32 bits to test if the result isn't an Int32.
  bind(&done);
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();

    move32SignExtendToPtr(dest, scratch);
    branchPtr(Assembler::NotEqual, dest, scratch, fail);
  }
}

void MacroAssembler::setupUnalignedABICall(Register scratch) {
  MOZ_ASSERT(!IsCompilingWasm(), "wasm should only use aligned ABI calls");
  setupNativeABICall();
  dynamicAlignment_ = true;

  ma_mv(scratch, StackPointer);

  // Force sp to be aligned
  asMasm().subPtr(Imm32(sizeof(uintptr_t)), StackPointer);
  ma_and(StackPointer, StackPointer, Imm32(~(ABIStackAlignment - 1)));
  storePtr(scratch, Address(StackPointer, 0));
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
void MacroAssembler::speculationBarrier() { MOZ_CRASH(); }
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

#ifdef ENABLE_JIT_SIMD
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
void MacroAssembler::truncDoubleToInt32(FloatRegister src, Register dest,
                                        Label* fail) {
  UseScratchRegisterScope temps(*this);
  Register scratch = temps.Acquire();

  // Round toward zero.
  Trunc_l_d(dest, src);

  // Sign extend lower 32 bits to test if the result isn't an Int32.
  {
    move32SignExtendToPtr(dest, scratch);
    branchPtr(Assembler::NotEqual, dest, scratch, fail);
  }

  // We have to check for (-1, -0] when the result is zero.
  Label notZero;
  ma_b(dest, zero, &notZero, Assembler::NotEqual, ShortJump);
  {
    fmv_x_d(scratch, src);
    ma_b(scratch, scratch, fail, Assembler::Signed, ShortJump);
  }
  bind(&notZero);
}
void MacroAssembler::truncFloat32ToInt32(FloatRegister src, Register dest,
                                         Label* fail) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  // Round toward zero.
  Trunc_l_s(dest, src);

  // Sign extend lower 32 bits to test if the result isn't an Int32.
  {
    move32SignExtendToPtr(dest, scratch);
    branchPtr(Assembler::NotEqual, dest, scratch, fail);
  }

  // We have to check for (-1, -0] when the result is zero.
  Label notZero;
  ma_b(dest, zero, &notZero, Assembler::NotEqual, ShortJump);
  {
    fmv_x_w(scratch, src);
    ma_b(scratch, scratch, fail, Assembler::Signed, ShortJump);
  }
  bind(&notZero);
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

void MacroAssembler::wasmAtomicEffectOp(const wasm::MemoryAccessDesc& access,
                                        AtomicOp op, Register value,
                                        const BaseIndex& mem,
                                        Register valueTemp, Register offsetTemp,
                                        Register maskTemp,
                                        wasm::ZeroExtendIndex zeroExtend) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  computeEffectiveAddress(mem, scratch, zeroExtend);

  AtomicEffectOp(*this, &access, access.type(), access.sync(), op, scratch,
                 value, valueTemp, offsetTemp, maskTemp);
}

FaultingCodeRange MacroAssembler::wasmAtomicExchange64(
    const wasm::MemoryAccessDesc& access, const Address& mem, Register64 value,
    Register64 output) {
  return AtomicExchange64(*this, &access, access.sync(), mem, value, output);
}

FaultingCodeRange MacroAssembler::wasmAtomicExchange64(
    const wasm::MemoryAccessDesc& access, const BaseIndex& mem,
    Register64 value, Register64 output) {
  return AtomicExchange64(*this, &access, access.sync(), mem, value, output);
}

void MacroAssembler::wasmAtomicExchange64(const wasm::MemoryAccessDesc& access,
                                          const BaseIndex& mem,
                                          Register64 value, Register64 output,
                                          wasm::ZeroExtendIndex zeroExtend) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  computeEffectiveAddress(mem, scratch, zeroExtend);

  AtomicExchange64(*this, &access, access.sync(), scratch, value, output);
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

FaultingCodeRange MacroAssembler::wasmAtomicExchange(
    const wasm::MemoryAccessDesc& access, const BaseIndex& mem, Register value,
    Register valueTemp, Register offsetTemp, Register maskTemp, Register output,
    wasm::ZeroExtendIndex zeroExtend) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  computeEffectiveAddress(mem, scratch, zeroExtend);

  return AtomicExchange(*this, &access, access.type(), access.sync(), scratch,
                        value, valueTemp, offsetTemp, maskTemp, output);
}

FaultingCodeRange MacroAssembler::wasmAtomicFetchOp64(
    const wasm::MemoryAccessDesc& access, AtomicOp op, Register64 value,
    const Address& mem, Register64 temp, Register64 output) {
  MOZ_ASSERT(temp == Register64::Invalid());
  return AtomicFetchOp64(*this, &access, access.sync(), op, value, mem, output);
}

FaultingCodeRange MacroAssembler::wasmAtomicFetchOp64(
    const wasm::MemoryAccessDesc& access, AtomicOp op, Register64 value,
    const BaseIndex& mem, Register64 temp, Register64 output) {
  MOZ_ASSERT(temp == Register64::Invalid());
  return AtomicFetchOp64(*this, &access, access.sync(), op, value, mem, output);
}

void MacroAssembler::wasmAtomicFetchOp64(const wasm::MemoryAccessDesc& access,
                                         AtomicOp op, Register64 value,
                                         const BaseIndex& mem, Register64 temp,
                                         Register64 output,
                                         wasm::ZeroExtendIndex zeroExtend) {
  MOZ_ASSERT(temp == Register64::Invalid());

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  computeEffectiveAddress(mem, scratch, zeroExtend);

  AtomicFetchOp64(*this, &access, access.sync(), op, value, scratch, output);
}

void MacroAssembler::atomicFetchOp64(Synchronization sync, AtomicOp op,
                                     Register64 value, const Address& mem,
                                     Register64 temp, Register64 output) {
  MOZ_ASSERT(temp == Register64::Invalid());
  AtomicFetchOp64(*this, nullptr, sync, op, value, mem, output);
}

void MacroAssembler::atomicFetchOp64(Synchronization sync, AtomicOp op,
                                     Register64 value, const BaseIndex& mem,
                                     Register64 temp, Register64 output) {
  MOZ_ASSERT(temp == Register64::Invalid());
  AtomicFetchOp64(*this, nullptr, sync, op, value, mem, output);
}

void MacroAssembler::atomicEffectOp64(Synchronization sync, AtomicOp op,
                                      Register64 value, const Address& mem,
                                      Register64 temp) {
  MOZ_ASSERT(temp == Register64::Invalid());
  AtomicFetchOp64(*this, nullptr, sync, op, value, mem, Register64(zero_reg));
}

void MacroAssembler::atomicEffectOp64(Synchronization sync, AtomicOp op,
                                      Register64 value, const BaseIndex& mem,
                                      Register64 temp) {
  MOZ_ASSERT(temp == Register64::Invalid());
  AtomicFetchOp64(*this, nullptr, sync, op, value, mem, Register64(zero_reg));
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

void MacroAssembler::wasmAtomicFetchOp(const wasm::MemoryAccessDesc& access,
                                       AtomicOp op, Register value,
                                       const BaseIndex& mem, Register valueTemp,
                                       Register offsetTemp, Register maskTemp,
                                       Register output,
                                       wasm::ZeroExtendIndex zeroExtend) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  computeEffectiveAddress(mem, scratch, zeroExtend);

  AtomicFetchOp(*this, &access, access.type(), access.sync(), op, scratch,
                value, valueTemp, offsetTemp, maskTemp, output);
}

void MacroAssembler::wasmBoundsCheck32(Condition cond, Register index,
                                       Register boundsCheckLimit,
                                       Label* label) {
  ma_b(index, boundsCheckLimit, label, cond, ShortJump);
}

void MacroAssembler::wasmBoundsCheck32(Condition cond, Register index,
                                       Address boundsCheckLimit, Label* label) {
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();
  load32(boundsCheckLimit, scratch2);
  ma_b(index, scratch2, label, cond, ShortJump);
}

void MacroAssembler::wasmBoundsCheck64(Condition cond, Register64 index,
                                       Register64 boundsCheckLimit,
                                       Label* label) {
  ma_b(index.reg, boundsCheckLimit.reg, label, cond, ShortJump);
}

void MacroAssembler::wasmBoundsCheck64(Condition cond, Register64 index,
                                       Address boundsCheckLimit, Label* label) {
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();
  loadPtr(boundsCheckLimit, scratch2);
  ma_b(index.reg, scratch2, label, cond, ShortJump);
}

FaultingCodeRange MacroAssembler::wasmCompareExchange64(
    const wasm::MemoryAccessDesc& access, const Address& mem, Register64 expect,
    Register64 replace, Register64 output) {
  return CompareExchange64(*this, &access, access.sync(), mem, expect, replace,
                           output);
}

FaultingCodeRange MacroAssembler::wasmCompareExchange64(
    const wasm::MemoryAccessDesc& access, const BaseIndex& mem,
    Register64 expect, Register64 replace, Register64 output) {
  return CompareExchange64(*this, &access, access.sync(), mem, expect, replace,
                           output);
}

void MacroAssembler::wasmCompareExchange64(const wasm::MemoryAccessDesc& access,
                                           const BaseIndex& mem,
                                           Register64 expect,
                                           Register64 replace,
                                           Register64 output,
                                           wasm::ZeroExtendIndex zeroExtend) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  computeEffectiveAddress(mem, scratch, zeroExtend);

  CompareExchange64(*this, &access, access.sync(), scratch, expect, replace,
                    output);
}

static FaultingCodeRange CompareExchange(
    MacroAssembler& masm, const wasm::MemoryAccessDesc* access,
    Scalar::Type type, Synchronization sync, Register address, Register oldval,
    Register newval, Register valueTemp, Register offsetTemp, Register maskTemp,
    Register output) {
  MOZ_ASSERT(oldval != output && newval != output);

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

  if (nbytes == 4) {
    masm.memoryBarrierBefore(sync);

    Label end;
    FaultingCodeRange fcr;
    {
      // All instructions within this scope must be restricted to fulfill the
      // requirements for "constrained LR/SC loop" [1].
      //
      // 1. The instructions are limited to the I or E base instruction set.
      // 2. The total number of instructions is limited to 16.
      //
      // [1] https://riscv.github.io/riscv-isa-manual/snapshot/spec/#sec:lrscseq

      // Forbid pools to ensure all instructions are placed next to each other
      // and the total number of instructions doesn't exceed 16.
      //
      // Four instructions (lr.w, bne, sc.w, bnez), plus one new deadline for
      // the forward branch.
      AutoForbidPoolsAndNops afp(&masm, /* number of insns = */ 4, 1);

      UseScratchRegisterScope temps(&masm);
      Register scratch2 = temps.Acquire();

      // LR/SC loop.
      Label again;
      masm.bind(&again);

      // Load the current value into |output|.
      // This may fault, so we may need to emit a TrapSite.
      auto before = masm.currentOffset();
      fcr = FaultingCodeRange(before);
      masm.lr_w(true, true, output, address);
      if (access) {
        masm.appendAndVerify(*access, wasm::TrapMachineInsn::Load32, fcr);
      }

      // Return if the current value isn't equal to |oldval|.
      masm.ma_b(output, oldval, &end, Assembler::NotEqual, ShortJump);

      // Otherwise try to store |newval|.
      masm.sc_w(true, true, scratch2, address, newval);

      // Store was successful iff |scratch2| is zero.
      masm.ma_b(scratch2, scratch2, &again, Assembler::NonZero, ShortJump);
    }

    // Barrier only needed after successful store.
    masm.memoryBarrierAfter(sync);

    masm.bind(&end);
    return fcr;
  }

  // Align the address for a 32-bit word load and compute the shift offset.
  AtomicOffset(masm, address, offsetTemp);

  // Compute the mask to select the input bytes in a 32-bit word.
  AtomicMask(masm, nbytes, offsetTemp, maskTemp);

  UseScratchRegisterScope temps(&masm);
  Register scratchNewVal = temps.Acquire();

  // Shift |oldval| and |newval| into their positions in a 32-bit word.
  AtomicShiftToOffset(masm, nbytes, offsetTemp, maskTemp,
                      std::pair{oldval, valueTemp},
                      std::pair{newval, scratchNewVal});

  masm.memoryBarrierBefore(sync);

  Label end;
  FaultingCodeRange fcr;
  {
    // All instructions within this scope must be restricted to fulfill the
    // requirements for "constrained LR/SC loop" [1].
    //
    // 1. The instructions are limited to the I or E base instruction set.
    // 2. The total number of instructions is limited to 16.
    //
    // [1] https://riscv.github.io/riscv-isa-manual/snapshot/spec/#sec:lrscseq

    // Forbid pools to ensure all instructions are placed next to each other
    // and the total number of instructions doesn't exceed 16.
    //
    // Eight instructions (lr.w, and, bne, xor, and, xor, sc.w, bnez), plus one
    // new deadline for the forward branch.
    AutoForbidPoolsAndNops afp(&masm, /* number of insns = */ 8, 1);

    Register scratch2 = temps.Acquire();

    // LR/SC loop.
    Label again;
    masm.bind(&again);

    // Load the current value into |output|.
    // This may fault, so we may need to emit a TrapSite.
    auto before = masm.currentOffset();
    fcr = FaultingCodeRange(before);
    masm.lr_w(true, true, output, address);
    if (access) {
      masm.appendAndVerify(*access, wasm::TrapMachineInsn::Load32, fcr);
    }

    // Zero other bits in the loaded value.
    masm.and_(scratch2, output, maskTemp);

    // Return if the current value isn't equal to |oldval|.
    masm.ma_b(scratch2, valueTemp, &end, Assembler::NotEqual, ShortJump);

    // Insert |newval| into the loaded value.
    masm.xor_(scratch2, scratchNewVal, output);
    masm.and_(scratch2, scratch2, maskTemp);
    masm.xor_(scratch2, scratch2, output);

    // Try to store back to memory.
    masm.sc_w(true, true, scratch2, address, scratch2);

    // Store was successful iff |scratch2| is zero.
    masm.ma_b(scratch2, scratch2, &again, Assembler::NonZero, ShortJump);
  }

  // Barrier only needed after successful store.
  masm.memoryBarrierAfter(sync);

  masm.bind(&end);

  AtomicExtendResult(masm, type, output, offsetTemp);
  return fcr;
}

template <typename T>
static FaultingCodeRange CompareExchange(
    MacroAssembler& masm, const wasm::MemoryAccessDesc* access,
    Scalar::Type type, Synchronization sync, const T& mem, Register oldval,
    Register newval, Register valueTemp, Register offsetTemp, Register maskTemp,
    Register output) {
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  masm.computeEffectiveAddress(mem, scratch);

  return CompareExchange(masm, access, type, sync, scratch, oldval, newval,
                         valueTemp, offsetTemp, maskTemp, output);
}

void MacroAssembler::compareExchange(Scalar::Type type, Synchronization sync,
                                     const Address& mem, Register expected,
                                     Register replacement, Register valueTemp,
                                     Register offsetTemp, Register maskTemp,
                                     Register output) {
  CompareExchange(*this, nullptr, type, sync, mem, expected, replacement,
                  valueTemp, offsetTemp, maskTemp, output);
}

void MacroAssembler::compareExchange(Scalar::Type type, Synchronization sync,
                                     const BaseIndex& mem, Register expected,
                                     Register replacement, Register valueTemp,
                                     Register offsetTemp, Register maskTemp,
                                     Register output) {
  CompareExchange(*this, nullptr, type, sync, mem, expected, replacement,
                  valueTemp, offsetTemp, maskTemp, output);
}

FaultingCodeRange MacroAssembler::wasmCompareExchange(
    const wasm::MemoryAccessDesc& access, const Address& mem, Register expected,
    Register replacement, Register valueTemp, Register offsetTemp,
    Register maskTemp, Register output) {
  return CompareExchange(*this, &access, access.type(), access.sync(), mem,
                         expected, replacement, valueTemp, offsetTemp, maskTemp,
                         output);
}

FaultingCodeRange MacroAssembler::wasmCompareExchange(
    const wasm::MemoryAccessDesc& access, const BaseIndex& mem,
    Register expected, Register replacement, Register valueTemp,
    Register offsetTemp, Register maskTemp, Register output) {
  return CompareExchange(*this, &access, access.type(), access.sync(), mem,
                         expected, replacement, valueTemp, offsetTemp, maskTemp,
                         output);
}

FaultingCodeRange MacroAssembler::wasmCompareExchange(
    const wasm::MemoryAccessDesc& access, const BaseIndex& mem,
    Register expected, Register replacement, Register valueTemp,
    Register offsetTemp, Register maskTemp, Register output,
    wasm::ZeroExtendIndex zeroExtend) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  computeEffectiveAddress(mem, scratch, zeroExtend);

  return CompareExchange(*this, &access, access.type(), access.sync(), scratch,
                         expected, replacement, valueTemp, offsetTemp, maskTemp,
                         output);
}

FaultingCodeRange MacroAssembler::wasmLoad(const wasm::MemoryAccessDesc& access,
                                           Register memoryBase, Register ptr,
                                           AnyRegister output,
                                           wasm::ZeroExtendIndex zeroExtend) {
  return wasmLoadImpl(access, memoryBase, ptr, output, zeroExtend);
}

FaultingCodeRange MacroAssembler::wasmLoadI64(
    const wasm::MemoryAccessDesc& access, Register memoryBase, Register ptr,
    Register64 output, wasm::ZeroExtendIndex zeroExtend) {
  return wasmLoadImpl(access, memoryBase, ptr, AnyRegister(output.reg),
                      zeroExtend);
}

FaultingCodeRange MacroAssembler::wasmStore(
    const wasm::MemoryAccessDesc& access, AnyRegister value,
    Register memoryBase, Register ptr, wasm::ZeroExtendIndex zeroExtend) {
  return wasmStoreImpl(access, value, memoryBase, ptr, zeroExtend);
}

FaultingCodeRange MacroAssembler::wasmStoreI64(
    const wasm::MemoryAccessDesc& access, Register64 value, Register memoryBase,
    Register ptr, wasm::ZeroExtendIndex zeroExtend) {
  return wasmStoreImpl(access, AnyRegister(value.reg), memoryBase, ptr,
                       zeroExtend);
}

void MacroAssemblerRiscv64::Clear_if_nan_d(Register rd, FPURegister fs) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  feq_d(scratch, fs, fs);
  if (HasZicondExtension()) {
    czero_eqz(rd, rd, scratch);
    return;
  }
  neg(scratch, scratch);
  and_(rd, rd, scratch);
}

void MacroAssemblerRiscv64::Clear_if_nan_s(Register rd, FPURegister fs) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  feq_s(scratch, fs, fs);
  if (HasZicondExtension()) {
    czero_eqz(rd, rd, scratch);
    return;
  }
  neg(scratch, scratch);
  and_(rd, rd, scratch);
}

void MacroAssembler::wasmTruncateDoubleToInt32(FloatRegister input,
                                               Register output,
                                               bool isSaturating,
                                               Label* oolEntry) {
  if (isSaturating) {
    Trunc_w_d(output, input);
    Clear_if_nan_d(output, input);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();

    Trunc_l_d(output, input);

    // Sign extend lower 32 bits to test if the result isn't an Int32.
    move32SignExtendToPtr(output, scratch);
    branchPtr(Assembler::NotEqual, output, scratch, oolEntry);
  }
}

void MacroAssembler::wasmTruncateDoubleToInt64(
    FloatRegister input, Register64 output, bool isSaturating, Label* oolEntry,
    Label* oolRejoin, FloatRegister tempDouble) {
  if (isSaturating) {
    Trunc_l_d(output.reg, input);
    Clear_if_nan_d(output.reg, input);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Trunc_l_d(output.reg, input, scratch);
    ma_b(scratch, Imm32(0), oolEntry, Assembler::Equal, ShortJump);
  }
}

void MacroAssembler::wasmTruncateDoubleToUInt32(FloatRegister input,
                                                Register output,
                                                bool isSaturating,
                                                Label* oolEntry) {
  if (isSaturating) {
    Trunc_uw_d(output, input);
    Clear_if_nan_d(output, input);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Trunc_uw_d(output, input, scratch);
    ma_b(scratch, Imm32(0), oolEntry, Assembler::Equal, ShortJump);
  }
}

void MacroAssembler::wasmTruncateDoubleToUInt64(
    FloatRegister input, Register64 output, bool isSaturating, Label* oolEntry,
    Label* oolRejoin, FloatRegister tempDouble) {
  if (isSaturating) {
    Trunc_ul_d(output.reg, input);
    Clear_if_nan_d(output.reg, input);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Trunc_ul_d(output.reg, input, scratch);
    ma_b(scratch, Imm32(0), oolEntry, Assembler::Equal, ShortJump);
  }
}

void MacroAssembler::wasmTruncateFloat32ToInt32(FloatRegister input,
                                                Register output,
                                                bool isSaturating,
                                                Label* oolEntry) {
  if (isSaturating) {
    Trunc_w_s(output, input);
    Clear_if_nan_s(output, input);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();

    Trunc_l_s(output, input, scratch);

    // Sign extend lower 32 bits to test if the result isn't an Int32.
    move32SignExtendToPtr(output, scratch);
    branchPtr(Assembler::NotEqual, output, scratch, oolEntry);
  }
}

void MacroAssembler::wasmTruncateFloat32ToInt64(
    FloatRegister input, Register64 output, bool isSaturating, Label* oolEntry,
    Label* oolRejoin, FloatRegister tempDouble) {
  if (isSaturating) {
    Trunc_l_s(output.reg, input);
    Clear_if_nan_s(output.reg, input);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Trunc_l_s(output.reg, input, scratch);
    ma_b(scratch, Imm32(0), oolEntry, Assembler::Equal, ShortJump);
  }
}

void MacroAssembler::wasmTruncateFloat32ToUInt32(FloatRegister input,
                                                 Register output,
                                                 bool isSaturating,
                                                 Label* oolEntry) {
  if (isSaturating) {
    Trunc_uw_s(output, input);
    Clear_if_nan_s(output, input);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Trunc_uw_s(output, input, scratch);
    ma_b(scratch, Imm32(0), oolEntry, Assembler::Equal, ShortJump);
  }
}

void MacroAssembler::wasmTruncateFloat32ToUInt64(
    FloatRegister input, Register64 output, bool isSaturating, Label* oolEntry,
    Label* oolRejoin, FloatRegister tempDouble) {
  if (isSaturating) {
    Trunc_ul_s(output.reg, input);
    Clear_if_nan_s(output.reg, input);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    Trunc_ul_s(output.reg, input, scratch);
    ma_b(scratch, Imm32(0), oolEntry, Assembler::Equal, ShortJump);
  }
}

// TODO(riscv64): widenInt32 should be nop?
void MacroAssembler::widenInt32(Register r) {
  move32To64SignExtend(r, Register64(r));
}

void MacroAssembler::wasmMarkCallAsSlow() { addi(ra, ra, 0); }

const int32_t SlowCallMarker = 0x8093;  // addi ra, ra, 0

void MacroAssembler::wasmCheckSlowCallsite(Register ra_, Label* notSlow,
                                           Register temp1, Register temp2) {
  MOZ_ASSERT(ra_ != temp2);

  UseScratchRegisterScope temps(*this);
  // temp1 aliases ra_, so allocating a new register.
  const Register scratchMarker = temps.Acquire();
  move32(Imm32(SlowCallMarker), scratchMarker);

  Label slow;
  // Handle `jalr; (ra_ here) marker`.
  load32(Address(ra_, 0), temp2);
  branch32(Assembler::Equal, temp2, scratchMarker, &slow);
  // Handle `jal; (ra_ here) nop; marker`.
  // See also: AssemblerRISCVI::jal(Register rd, int32_t imm21); Bug 1996840
  branch32(Assembler::NotEqual, temp2, Imm32(kNopByte), notSlow);
  load32(Address(ra_, 4), temp2);
  branch32(Assembler::NotEqual, temp2, scratchMarker, notSlow);
  bind(&slow);
}

CodeOffset MacroAssembler::wasmMarkedSlowCall(const wasm::CallSiteDesc& desc,
                                              const Register reg) {
  AutoForbidPoolsAndNops afp(this, 2);
  CodeOffset offset = call(desc, reg);
  wasmMarkCallAsSlow();
  return offset;
}
//}}} check_macroassembler_style

void MacroAssemblerRiscv64::ma_mv(Register dest, Register src) {
  if (dest == src) {
    return;
  }
  mv(dest, src);
}

// This method generates lui + addiw instruction block that can be modified by
// patchLi32.
BufferOffset MacroAssemblerRiscv64::ma_liPatchable(Register dest, Imm32 imm) {
  AutoForbidPoolsAndNops afp(this, 2);
  BufferOffset offset = nextOffset();

  auto [high_20, low_12] = ToHigh20Low12(imm.value);
  lui(dest, high_20);
  addiw(dest, dest, low_12);

  return offset;
}

void MacroAssemblerRiscv64::patchLi32(CodeOffset offset, Imm32 imm) {
  Instruction* inst0 = getInstructionAt(BufferOffset(offset.offset()));
  Instruction* inst1 =
      getInstructionAt(BufferOffset(offset.offset() + kInstrSize));
  MOZ_ASSERT(inst0->IsLui());
  MOZ_ASSERT(inst1->IsAddiw());

  auto [high_20, low_12] = ToHigh20Low12(imm.value);

  inst0->SetImm20UValue(high_20);
  inst1->SetImm12Value(low_12);

  disassembleInstr(inst0);
  disassembleInstr(inst1);

  MOZ_ASSERT((inst0->Imm20UValue() << kImm20Shift) + inst1->Imm12Value() ==
             imm.value);
}

void MacroAssemblerRiscv64::ma_li(Register dest, ImmGCPtr ptr) {
  BufferOffset offset = ma_liPatchable(dest, ImmPtr(ptr.value));
  writeDataRelocation(ptr, offset);
}
void MacroAssemblerRiscv64::ma_li(Register dest, Imm32 imm) {
  RV_li(dest, imm.value);
}
void MacroAssemblerRiscv64::ma_li(Register dest, Imm64 imm) {
  RV_li(dest, imm.value);
}
void MacroAssemblerRiscv64::ma_li(Register dest, CodeLabel* label) {
  comment("load CodeLabel");
  BufferOffset bo = ma_liPatchable(dest, ImmPtr(/* placeholder */ nullptr));
  label->patchAt()->bind(bo.getOffset());
  label->setLinkMode(CodeLabel::MoveImmediate);
}
void MacroAssemblerRiscv64::ma_li(Register dest, ImmWord imm) {
  RV_li(dest, imm.value);
}

// Shortcut for when we know we're transferring 32 bits of data.
void MacroAssemblerRiscv64::ma_pop(Register r) {
  ld(r, StackPointer, 0);
  addi(StackPointer, StackPointer, sizeof(intptr_t));
}

void MacroAssemblerRiscv64::ma_push(Register r) {
  UseScratchRegisterScope temps(this);
  if (r == sp) {
    Register scratch = temps.Acquire();
    // Pushing sp requires one more instruction.
    ma_mv(scratch, sp);
    r = scratch;
  }

  addi(StackPointer, StackPointer, (int32_t)-sizeof(intptr_t));
  sd(r, StackPointer, 0);
}

void MacroAssemblerRiscv64::ma_mul32TestOverflow(Register rd, Register rj,
                                                 Register rk, Label* overflow) {
  // Perform a 64-bit multiplication.
  mul(rd, rj, rk);

  // Ensure the 64-bit result is identical to its 32-bit value.
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(rd != scratch);

  sext_w(scratch, rd);
  ma_b(scratch, rd, overflow, Assembler::NotEqual, ShortJump);
}
void MacroAssemblerRiscv64::ma_mul32TestOverflow(Register rd, Register rj,
                                                 Imm32 imm, Label* overflow) {
  // Perform a 64-bit multiplication.
  ma_mul64(rd, rj, imm);

  // Ensure the 64-bit result is identical to its 32-bit value.
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(rd != scratch);

  sext_w(scratch, rd);
  ma_b(scratch, rd, overflow, Assembler::NotEqual, ShortJump);
}

void MacroAssemblerRiscv64::ma_mulPtrTestOverflow(Register rd, Register rj,
                                                  Register rk,
                                                  Label* overflow) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  Register scratch2 = temps.Acquire();
  MOZ_ASSERT(rd != scratch);

  if (rd == rj) {
    ma_mv(scratch, rj);
    rj = scratch;
    rk = (rd == rk) ? rj : rk;
  } else if (rd == rk) {
    ma_mv(scratch, rk);
    rk = scratch;
  }

  mul(rd, rj, rk);
  mulh(scratch, rj, rk);
  srai(scratch2, rd, 63);
  ma_b(scratch, scratch2, overflow, Assembler::NotEqual, ShortJump);
}

bool MacroAssemblerRiscv64::UseShortBranch(
    Label* L, JumpKind jumpKind, OffsetSize bits,
    mozilla::Maybe<AutoForbidNops>& maybeAfn) {
  // If the label is already bound, we can directly determine which jump to use.
  if (L->bound()) {
    // Prevent nop sequences in branch instructions.
    AutoForbidNops afn(this);

    // Call |nextInstrOffset()| instead of just |nextOffset()| to ensure
    // branches which are about to go out of range are also taken into account
    // when computing the next instruction offset.
    //
    // Backward branches don't need to register new deadlines, so we can pass
    // |numNewDeadlines = 0| to |nextInstrOffset()|.
    int32_t offset = nextInstrOffset(1, 0).getOffset();

    // Use a short branch if the label is near enough.
    if (is_intn(offset - L->offset(), bits)) {
      // Extend the AutoForbidNops scope to ensure AutoForbidPoolsAndNops used
      // for short branches doesn't add emit nop sequences, because the nop
      // sequences can move the label outside the reachable range for this
      // branch.
      maybeAfn.emplace(this);
      return true;
    }
    return false;
  }

  // Otherwise use a short branch if requested.
  return jumpKind == ShortJump;
}

void MacroAssemblerRiscv64::Branch(Label* L, JumpKind jumpKind) {
  mozilla::Maybe<AutoForbidNops> afn;
  if (UseShortBranch(L, jumpKind, OffsetSize::kOffset21, afn)) {
    BranchShort(L);
  } else {
    BranchLong(L);
  }
}

BufferOffset MacroAssemblerRiscv64::BranchShort(Label* L) {
  // One instruction (jal), possibly one new deadline.
  AutoForbidPoolsAndNops afp(this, 1, !L->bound());

  BufferOffset next_instr_offset = nextInstrOffset(1, !L->bound());
  int32_t offset = branchOffset(L, OffsetSize::kOffset21, next_instr_offset);

  LabelDoc doc = refLabel(L);
  BufferOffset actualOffset = Assembler::j(offset, doc);
  MOZ_ASSERT_IF(actualOffset.assigned(), next_instr_offset == actualOffset);

  registerBranchDeadline(L, OffsetSize::kOffset21, next_instr_offset);

  return actualOffset;
}

void MacroAssemblerRiscv64::Branch(Label* L, Condition cond, Register rs,
                                   const Operand& rt, JumpKind jumpKind) {
  MOZ_ASSERT(cond != Always);
  MOZ_ASSERT_IF(rt.is_reg(), rs != rt.rm());

  UseScratchRegisterScope temps(this);
  Register scratch;
  if (rt.is_imm()) {
    if (rt.immediate() == 0) {
      scratch = zero;
    } else {
      scratch = temps.Acquire();
      ma_li(scratch, Imm64(rt.immediate()));
    }
  } else {
    MOZ_ASSERT(rt.is_reg());
    scratch = rt.rm();
  }

  mozilla::Maybe<AutoForbidNops> afn;
  if (UseShortBranch(L, jumpKind, OffsetSize::kOffset13, afn)) {
    BranchShort(L, cond, rs, scratch);
  } else {
    Label skip;
    Condition neg_cond = InvertCondition(cond);
    BranchShort(&skip, neg_cond, rs, scratch);
    BranchLong(L);
    bind(&skip);
  }
}

void MacroAssemblerRiscv64::BranchShort(Label* L, Condition cond, Register rs,
                                        Register rt) {
  MOZ_ASSERT(cond != Always);
  MOZ_ASSERT(rs != rt);

  // One instruction (branch), possibly one new deadline.
  AutoForbidPoolsAndNops afp(this, 1, !L->bound());

  BufferOffset next_instr_offset = nextInstrOffset(1, !L->bound());
  int32_t offset = branchOffset(L, OffsetSize::kOffset13, next_instr_offset);

  LabelDoc doc = refLabel(L);
  [[maybe_unused]] BufferOffset actualOffset;
  switch (cond) {
    case Equal:
      actualOffset = Assembler::beq(rs, rt, offset, doc);
      break;
    case NotEqual:
      actualOffset = Assembler::bne(rs, rt, offset, doc);
      break;

    // Signed comparison.
    case GreaterThan:
      actualOffset = Assembler::bgt(rs, rt, offset, doc);
      break;
    case GreaterThanOrEqual:
      actualOffset = Assembler::bge(rs, rt, offset, doc);
      break;
    case LessThan:
      actualOffset = Assembler::blt(rs, rt, offset, doc);
      break;
    case LessThanOrEqual:
      actualOffset = Assembler::ble(rs, rt, offset, doc);
      break;

    // Unsigned comparison.
    case Above:
      actualOffset = Assembler::bgtu(rs, rt, offset, doc);
      break;
    case AboveOrEqual:
      actualOffset = Assembler::bgeu(rs, rt, offset, doc);
      break;
    case Below:
      actualOffset = Assembler::bltu(rs, rt, offset, doc);
      break;
    case BelowOrEqual:
      actualOffset = Assembler::bleu(rs, rt, offset, doc);
      break;

    default:
      MOZ_CRASH("UNREACHABLE");
  }

  MOZ_ASSERT_IF(actualOffset.assigned(), next_instr_offset == actualOffset);

  registerBranchDeadline(L, OffsetSize::kOffset13, next_instr_offset);
}

void MacroAssemblerRiscv64::BranchLong(Label* L) {
  AutoForbidPoolsAndNops afp(this, 2);

  // Generate position independent long branch.
  int32_t imm = branchOffset(L);

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  LabelDoc doc = refLabel(L);
  auto [Hi20, Lo12] = ToHigh20Low12(imm);
  auipc(scratch, Hi20);    // Read PC + Hi20 into scratch.
  jr(scratch, Lo12, doc);  // jump PC + Hi20 + Lo12
}

CodeOffset MacroAssemblerRiscv64::BranchAndLink(Label* L) {
  mozilla::Maybe<AutoForbidNops> afn;
  if (UseShortBranch(L, ShortJump, OffsetSize::kOffset21, afn)) {
    // One instruction (jal), possibly one new deadline.
    AutoForbidPoolsAndNops afp(this, 1, !L->bound());

    BufferOffset next_instr_offset = nextInstrOffset(1, !L->bound());
    int32_t offset = branchOffset(L, OffsetSize::kOffset21, next_instr_offset);

    LabelDoc doc = refLabel(L);
    [[maybe_unused]] BufferOffset actualOffset = jal(offset, doc);
    MOZ_ASSERT_IF(actualOffset.assigned(), next_instr_offset == actualOffset);

    registerBranchDeadline(L, OffsetSize::kOffset21, next_instr_offset);
  } else {
    AutoForbidPoolsAndNops afp(this, 2);

    // Generate position independent long branch and link.
    int32_t imm = branchOffset(L);

    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();

    LabelDoc doc = refLabel(L);
    auto [Hi20, Lo12] = ToHigh20Low12(imm);
    auipc(scratch, Hi20);      // Read PC + Hi20 into scratch.
    jalr(scratch, Lo12, doc);  // jump PC + Hi20 + Lo12
  }

  return CodeOffset(currentOffset());
}

void MacroAssemblerRiscv64::ma_branch(Label* target, Condition cond,
                                      Register r1, const Operand& r2,
                                      JumpKind jumpKind) {
  MOZ_ASSERT((cond == Always && r1 == zero && r2.rm() == zero) ||
             (cond != Always && (r1 != zero || r2.rm() != zero)));

  if (r2.is_reg() && r1 == r2.rm()) {
    switch (cond) {
      case Always:
      case Equal:
      case GreaterThanOrEqual:
      case LessThanOrEqual:
      case AboveOrEqual:
      case BelowOrEqual:
        Branch(target, jumpKind);
        return;

      case NotEqual:
      case GreaterThan:
      case LessThan:
      case Above:
      case Below:
        return;  // No code needs to be emitted

      default:
        MOZ_CRASH("UNREACHABLE");
    }
  }

  Branch(target, cond, r1, r2, jumpKind);
}

// Branches when done from within riscv code.
void MacroAssemblerRiscv64::ma_b(Register lhs, ImmWord imm, Label* label,
                                 Condition c, JumpKind jumpKind) {
  switch (c) {
    case Always:
      ma_branch(label, c, zero, Operand(zero), jumpKind);
      break;
    case Zero:
    case NonZero:
    case Signed:
    case NotSigned:
      MOZ_ASSERT(imm.value == 0);
      ma_b(lhs, lhs, label, c, jumpKind);
      break;
    default:
      ma_branch(label, c, lhs, Operand(imm.value), jumpKind);
      break;
  }
}

void MacroAssemblerRiscv64::ma_b(Register lhs, Imm32 imm, Label* label,
                                 Condition c, JumpKind jumpKind) {
  switch (c) {
    case Always:
      ma_branch(label, c, zero, Operand(zero), jumpKind);
      break;
    case Zero:
    case NonZero:
    case Signed:
    case NotSigned:
      MOZ_ASSERT(imm.value == 0);
      ma_b(lhs, lhs, label, c, jumpKind);
      break;
    default:
      ma_branch(label, c, lhs, Operand(imm.value), jumpKind);
      break;
  }
}

void MacroAssemblerRiscv64::ma_b(Register lhs, Register rhs, Label* label,
                                 Condition c, JumpKind jumpKind) {
  switch (c) {
    case Always:
      ma_branch(label, c, zero, Operand(zero), jumpKind);
      break;
    case Zero:
      MOZ_ASSERT(lhs == rhs);
      ma_branch(label, Equal, lhs, Operand(zero), jumpKind);
      break;
    case NonZero:
      MOZ_ASSERT(lhs == rhs);
      ma_branch(label, NotEqual, lhs, Operand(zero), jumpKind);
      break;
    case Signed:
      MOZ_ASSERT(lhs == rhs);
      ma_branch(label, LessThan, lhs, Operand(zero), jumpKind);
      break;
    case NotSigned:
      MOZ_ASSERT(lhs == rhs);
      ma_branch(label, GreaterThanOrEqual, lhs, Operand(zero), jumpKind);
      break;
    default:
      ma_branch(label, c, lhs, Operand(rhs), jumpKind);
      break;
  }
}

void MacroAssemblerRiscv64::ExtractBits(Register rd, Register rs, uint16_t pos,
                                        uint16_t size) {
  constexpr uint16_t MaxBits = 64;

  MOZ_ASSERT(pos < MaxBits);
  MOZ_ASSERT(size > 0);
  MOZ_ASSERT(size <= MaxBits);
  MOZ_ASSERT((pos + size) > 0);
  MOZ_ASSERT((pos + size) <= MaxBits);

  Register src;
  if (uint16_t shift = MaxBits - (pos + size)) {
    slli(rd, rs, shift);
    src = rd;
  } else {
    src = rs;
  }

  srli(rd, src, MaxBits - size);
}

void MacroAssemblerRiscv64::ma_add32(Register rd, Register rs, Imm32 rt) {
  if (is_int12(rt.value)) {
    addiw(rd, rs, static_cast<int32_t>(rt.value));
  } else if (is_two_int12(rt.value)) {
    auto [first, second] = ToTwoInt12(rt.value);
    addiw(rd, rs, first);
    addiw(rd, rd, second);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    ma_li(scratch, rt);
    addw(rd, rs, scratch);
  }
}

void MacroAssemblerRiscv64::ma_add64(Register rd, Register rs, Imm64 rt) {
  if (rt.value == 0) {
    ma_mv(rd, rs);
  } else if (is_int12(rt.value)) {
    addi(rd, rs, static_cast<int32_t>(rt.value));
  } else if (is_two_int12(rt.value)) {
    auto [first, second] = ToTwoInt12(rt.value);
    addi(rd, rs, first);
    addi(rd, rd, second);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    ma_li(scratch, rt);
    add(rd, rs, scratch);
  }
}

void MacroAssemblerRiscv64::ma_sub32(Register rd, Register rs, Imm32 rt) {
  if (is_int12(int32_t(-uint32_t(rt.value)))) {
    // No subi instr, use addi(x, y, -imm).
    addiw(rd, rs, static_cast<int32_t>(-rt.value));
  } else if (is_two_int12(int32_t(-uint32_t(rt.value)))) {
    auto [first, second] = ToTwoInt12(-rt.value);
    addiw(rd, rs, first);
    addiw(rd, rd, second);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    ma_li(scratch, rt);
    subw(rd, rs, scratch);
  }
}

void MacroAssemblerRiscv64::ma_sub64(Register rd, Register rs, Imm64 rt) {
  if (rt.value == 0) {
    ma_mv(rd, rs);
  } else if (is_int12(int64_t(-uint64_t(rt.value)))) {
    // No subi instr, use addi(x, y, -imm).
    addi(rd, rs, static_cast<int32_t>(-rt.value));
  } else if (is_two_int12(int64_t(-uint64_t(rt.value)))) {
    auto [first, second] = ToTwoInt12(-rt.value);
    addi(rd, rs, first);
    addi(rd, rd, second);
  } else {
    // li handles the relocation.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    ma_li(scratch, rt);
    sub(rd, rs, scratch);
  }
}

/**
 * Return the index of the highest non-zero bit and the value with the highest
 * non-zero bit cleared. For example: 0x1234 returns {12, 0x234}.
 */
static std::pair<uint32_t, uint64_t> SingleBitInstructionParts(uint64_t imm) {
  MOZ_ASSERT(!is_int12(imm));
  uint32_t bit = 63 - std::countl_zero(imm);
  uint64_t rest = imm & ~(uint64_t(1) << bit);
  return {bit, rest};
}

void MacroAssemblerRiscv64::ma_and(Register rd, Register rs, Imm64 rt) {
  if (is_int12(rt.value)) {
    andi(rd, rs, rt.value);
  } else {
    int shift = std::bit_width(uint64_t(rt.value));
    if (shift < 64 && (uint64_t(1) << shift) - 1 == uint64_t(rt.value)) {
      if (HasZbbExtension()) {
        if (rt.value == 0xffff) {
          zext_h(rd, rs);
          return;
        }
      }
      if (HasZbaExtension()) {
        if (rt.value == 0xffff'ffff) {
          zext_w(rd, rs);
          return;
        }
      }

      // `x & ((1 << shift) - 1)` can be expressed as two shifts.
      //  For example: `x & 0xffff` is `slli rd, rs, 48; srli rd, rd, 48`.
      slli(rd, rs, 64 - shift);
      srli(rd, rd, 64 - shift);
    } else if (rt.value == 0x8000'0000) {
      // Int32 sign extraction can be expressed as two shifts.
      srliw(rd, rs, 31);
      slli(rd, rd, 31);
    } else if (rt.value == int64_t(0x8000'0000'0000'0000)) {
      // Int64 sign extraction can be expressed as two shifts.
      srli(rd, rs, 63);
      slli(rd, rd, 63);
    } else {
      // Loading an immediate and then performing an `and` requires at least two
      // instructions. Instead prefer to emit two single bit instructions.
      //
      // This handles common bit-clear patterns like:
      // -------------------------------------------------------------
      // | Source            | Instructions                          |
      // |-------------------|---------------------------------------|
      // | rd = rs & ~0x1000 | bclri rd, rs, 12                      |
      // | rd = rs & ~0x1100 | bclri rd, rs, 12; bclri rd, rd, 8     |
      // | rd = rs & ~0x1011 | bclri rd, rs, 12; andi  rd, rd, ~0x11 |
      // -------------------------------------------------------------
      if (HasZbsExtension()) {
        auto [bit, rest] = SingleBitInstructionParts(~rt.value);
        if (rest == 0 || std::has_single_bit(rest) || is_int12(~rest)) {
          bclri(rd, rs, bit);
          if (rest) {
            if (std::has_single_bit(rest)) {
              bclri(rd, rd, 63 - std::countl_zero(rest));
            } else {
              andi(rd, rd, ~rest);
            }
          }
          return;
        }
      }

      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      ma_li(scratch, rt);
      and_(rd, rs, scratch);
    }
  }
}

void MacroAssemblerRiscv64::ma_or(Register rd, Register rs, Imm64 rt) {
  if (rt.value == 0) {
    ma_mv(rd, rs);
  } else if (is_int12(rt.value)) {
    ori(rd, rs, rt.value);
  } else {
    // Loading an immediate and then performing an `or` requires at least two
    // instructions. Instead prefer to emit two single bit instructions.
    //
    // This handles common bit-set patterns like:
    // -----------------------------------------------------------
    // | Source           | Instructions                         |
    // |------------------|--------------------------------------|
    // | rd = rs | 0x1000 | bseti rd, rs, 12                     |
    // | rd = rs | 0x1100 | bseti rd, rs, 12; bseti rd, rd, 8    |
    // | rd = rs | 0x1011 | bseti rd, rs, 12; ori   rd, rd, 0x11 |
    // -----------------------------------------------------------
    if (HasZbsExtension()) {
      auto [bit, rest] = SingleBitInstructionParts(rt.value);
      if (std::has_single_bit(rest) || is_int12(rest)) {
        bseti(rd, rs, bit);
        if (rest) {
          if (std::has_single_bit(rest)) {
            bseti(rd, rd, 63 - std::countl_zero(rest));
          } else {
            ori(rd, rd, rest);
          }
        }
        return;
      }
    }

    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    ma_li(scratch, rt);
    or_(rd, rs, scratch);
  }
}

void MacroAssemblerRiscv64::ma_xor(Register rd, Register rs, Imm64 rt) {
  if (is_int12(rt.value)) {
    xori(rd, rs, rt.value);
  } else {
    // Loading an immediate and then performing a `xor` requires at least two
    // instructions. Instead prefer to emit two single bit instructions.
    //
    // This handles common bit-invert patterns like:
    // -----------------------------------------------------------
    // | Source           | Instructions                         |
    // |------------------|--------------------------------------|
    // | rd = rs ^ 0x1000 | binvi rd, rs, 12                     |
    // | rd = rs ^ 0x1100 | binvi rd, rs, 12; binvi rd, rd, 8    |
    // | rd = rs ^ 0x1011 | binvi rd, rs, 12; xori  rd, rd, 0x11 |
    // -----------------------------------------------------------
    if (HasZbsExtension()) {
      auto [bit, rest] = SingleBitInstructionParts(rt.value);
      if (std::has_single_bit(rest) || is_int12(rest)) {
        binvi(rd, rs, bit);
        if (rest) {
          if (std::has_single_bit(rest)) {
            binvi(rd, rd, 63 - std::countl_zero(rest));
          } else {
            xori(rd, rd, rest);
          }
        }
        return;
      }
    }

    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    ma_li(scratch, rt);
    xor_(rd, rs, scratch);
  }
}

void MacroAssemblerRiscv64::ma_mul32(Register rd, Register rs, Imm32 rt) {
  switch (rt.value) {
    case -1:
      negw(rd, rs);
      return;
    case 0:
      ma_mv(rd, zero);
      return;
    case 1:
      SignExtendWord(rd, rs);
      return;
    case 2:
      addw(rd, rs, rs);
      return;
    default:
      break;
  }

  if (rt.value > 0) {
    const uint32_t uvalue = static_cast<uint32_t>(rt.value);
    if (HasZbaExtension()) {
      int ctz = std::countr_zero(uvalue);
      if ((uvalue >> ctz) == 3) {
        // rd = (rs * 2 + rs) << ctz
        sh1add(rd, rs, rs);
        if (ctz) {
          slliw(rd, rd, ctz);
        } else {
          SignExtendWord(rd, rd);
        }
        return;
      }
      if ((uvalue >> ctz) == 5) {
        // rd = (rs * 4 + rs) << ctz
        sh2add(rd, rs, rs);
        if (ctz) {
          slliw(rd, rd, ctz);
        } else {
          SignExtendWord(rd, rd);
        }
        return;
      }
      if ((uvalue >> ctz) == 9) {
        // rd = (rs * 8 + rs) << ctz
        sh3add(rd, rs, rs);
        if (ctz) {
          slliw(rd, rd, ctz);
        } else {
          SignExtendWord(rd, rd);
        }
        return;
      }
    }

    const uint8_t shift = mozilla::FloorLog2(uvalue);
    // Use shift if constant is power of 2.
    if (std::has_single_bit(uvalue)) {
      slliw(rd, rs, shift);
      return;
    }

    if (std::has_single_bit(uvalue + 1)) {
      const int shift_plus_one = std::countr_zero(uvalue + 1);

      UseScratchRegisterScope temps(this);
      Register shifted = rd;
      if (rd == rs) {
        shifted = temps.Acquire();
      }
      slliw(shifted, rs, shift_plus_one);
      subw(rd, shifted, rs);
      return;
    }

    // If the constant cannot be encoded as (1<<C1), see if it can be encoded
    // as (1<<C1) | (1<<C2), which can be computed using an add and a shift.
    const uint32_t rest = uvalue - (UINT32_C(1) << shift);
    if (std::has_single_bit(rest)) {
      const int shift_rest = std::countr_zero(rest);

      UseScratchRegisterScope temps(this);
      Register shifted = rd;
      if (rd == rs) {
        shifted = temps.Acquire();
      }

      slliw(shifted, rs, (shift - shift_rest));
      addw(rd, shifted, rs);
      if (shift_rest != 0) {
        slliw(rd, rd, shift_rest);
      }
      return;
    }
  }

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  ma_li(scratch, rt);
  mulw(rd, rs, scratch);
}

void MacroAssemblerRiscv64::ma_mulhu32(Register rd, Register rs, Imm32 rt) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  ma_li(scratch, uint32_t(rt.value));  // clear upper 32 bits
  mul(rd, rs, scratch);
  srli(rd, rd, 32);
}

void MacroAssemblerRiscv64::ma_mul64(Register rd, Register rs, Imm64 rt) {
  switch (rt.value) {
    case -1:
      neg(rd, rs);
      return;
    case 0:
      ma_mv(rd, zero);
      return;
    case 1:
      ma_mv(rd, rs);
      return;
    case 2:
      add(rd, rs, rs);
      return;
    default:
      break;
  }

  if (rt.value > 0) {
    const uint64_t uvalue = static_cast<uint64_t>(rt.value);
    if (HasZbaExtension()) {
      int ctz = std::countr_zero(uvalue);
      if ((uvalue >> ctz) == 3) {
        // rd = (rs * 2 + rs) << ctz
        sh1add(rd, rs, rs);
        if (ctz) {
          slli(rd, rd, ctz);
        }
        return;
      }
      if ((uvalue >> ctz) == 5) {
        // rd = (rs * 4 + rs) << ctz
        sh2add(rd, rs, rs);
        if (ctz) {
          slli(rd, rd, ctz);
        }
        return;
      }
      if ((uvalue >> ctz) == 9) {
        // rd = (rs * 8 + rs) << ctz
        sh3add(rd, rs, rs);
        if (ctz) {
          slli(rd, rd, ctz);
        }
        return;
      }
    }

    const uint8_t shift = mozilla::FloorLog2(uvalue);
    // Use shift if constant is power of 2.
    if (std::has_single_bit(uvalue)) {
      slli(rd, rs, shift);
      return;
    }

    if (std::has_single_bit(uvalue + 1)) {
      const int shift_plus_one = std::countr_zero(uvalue + 1);

      UseScratchRegisterScope temps(this);
      Register shifted = rd;
      if (rd == rs) {
        shifted = temps.Acquire();
      }
      slli(shifted, rs, shift_plus_one);
      sub(rd, shifted, rs);
      return;
    }

    // If the constant cannot be encoded as (1<<C1), see if it can be encoded
    // as (1<<C1) | (1<<C2), which can be computed using an add and a shift.
    const uint64_t rest = uvalue - (UINT64_C(1) << shift);
    if (std::has_single_bit(rest)) {
      const int shift_rest = std::countr_zero(rest);

      UseScratchRegisterScope temps(this);
      Register shifted = rd;
      if (rd == rs) {
        shifted = temps.Acquire();
      }

      slli(shifted, rs, (shift - shift_rest));
      add(rd, shifted, rs);
      if (shift_rest != 0) {
        slli(rd, rd, shift_rest);
      }
      return;
    }
  }

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  ma_li(scratch, rt);
  mul(rd, rs, scratch);
}

BufferOffset MacroAssemblerRiscv64::ma_jump(ImmPtr dest) {
  // 6 instruction to materialize the constant.
  // + 1 instruction for jr.
  AutoForbidPoolsAndNops afp(this, 7);

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  BufferOffset offset = ma_liPatchable(scratch, dest);
  jr(scratch);
  return offset;
}

// fp instructions
void MacroAssemblerRiscv64::ma_lid(FloatRegister dest, double value) {
  if (HasZfaExtension()) {
    // -1.0 is directly supported by fli.d. Other negative values need fneg.d.
    bool negate = value < 0.0 && value != -1.0;
    double searchValue = negate ? -value : value;

    int imm5 = GetImm5ForFLID(searchValue);
    if (imm5 >= 0) {
      fli_d(dest, imm5);
      if (negate) {
        fneg_d(dest, dest);
      }
      return;
    }
  }

  ImmWord imm(mozilla::BitwiseCast<uint64_t>(value));

  if (imm.value != 0) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    fmv_d_x(dest, scratch);
  } else {
    fmv_d_x(dest, zero);
  }
}
// fp instructions
void MacroAssemblerRiscv64::ma_lis(FloatRegister dest, float value) {
  if (HasZfaExtension()) {
    // -1.0 is directly supported by fli.s. Other negative values need fneg.s.
    bool negate = value < 0.0f && value != -1.0f;
    float searchValue = negate ? -value : value;

    int imm5 = GetImm5ForFLIS(searchValue);
    if (imm5 >= 0) {
      fli_s(dest, imm5);
      if (negate) {
        fneg_s(dest, dest);
      }
      return;
    }
  }

  Imm32 imm(mozilla::BitwiseCast<uint32_t>(value));

  if (imm.value != 0) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    ma_li(scratch, imm);
    fmv_w_x(dest, scratch);
  } else {
    fmv_w_x(dest, zero);
  }
}

void MacroAssemblerRiscv64::ma_sub32TestOverflow(Register rd, Register rj,
                                                 Register rk, Label* overflow) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  sub(scratch, rj, rk);
  subw(rd, rj, rk);
  ma_b(rd, scratch, overflow, Assembler::NotEqual, ShortJump);
}

void MacroAssemblerRiscv64::ma_sub32TestOverflow(Register rd, Register rj,
                                                 Imm32 imm, Label* overflow) {
  if (imm.value != INT32_MIN) {
    ma_add32TestOverflow(rd, rj, Imm32(-imm.value), overflow);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    MOZ_ASSERT(rj != scratch);
    ma_li(scratch, Imm32(imm.value));
    ma_sub32TestOverflow(rd, rj, scratch, overflow);
  }
}

void MacroAssemblerRiscv64::ma_add32TestOverflow(Register rd, Register rj,
                                                 Register rk, Label* overflow) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  add(scratch, rj, rk);
  addw(rd, rj, rk);
  ma_b(rd, scratch, overflow, Assembler::NotEqual, ShortJump);
}

void MacroAssemblerRiscv64::ma_add32TestOverflow(Register rd, Register rj,
                                                 Imm32 imm, Label* overflow) {
  if (rd != rj) {
    if (is_int12(imm.value)) {
      addiw(rd, rj, imm.value);
    } else {
      UseScratchRegisterScope temps(this);
      Register scratch = temps.Acquire();
      ma_li(scratch, imm);
      addw(rd, rj, scratch);
    }
    ma_b(rd, rj, overflow,
         imm.value >= 0 ? Assembler::LessThan : Assembler::GreaterThanOrEqual,
         ShortJump);
    return;
  }

  // Check for signed range because of addi
  if (is_int12(imm.value)) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    addi(scratch, rj, imm.value);
    addiw(rd, rj, imm.value);
    ma_b(rd, scratch, overflow, Assembler::NotEqual, ShortJump);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch2 = temps.Acquire();
    ma_li(scratch2, imm);
    ma_add32TestOverflow(rd, rj, scratch2, overflow);
  }
}

void MacroAssemblerRiscv64::ma_subPtrTestOverflow(Register rd, Register rj,
                                                  Register rk,
                                                  Label* overflow) {
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();
  MOZ_ASSERT_IF(rj == rd, rj != rk);
  MOZ_ASSERT(rj != scratch2);
  MOZ_ASSERT(rk != scratch2);
  MOZ_ASSERT(rd != scratch2);

  Register rj_copy = rj;

  if (rj == rd) {
    ma_mv(scratch2, rj);
    rj_copy = scratch2;
  }

  {
    Register scratch = temps.Acquire();
    MOZ_ASSERT(rd != scratch);

    sub(rd, rj, rk);
    // If the sign of rj and rk are the same, no overflow
    xor_(scratch, rj_copy, rk);
    // Check if the sign of rd and rj are the same
    xor_(scratch2, rd, rj_copy);
    and_(scratch2, scratch2, scratch);
  }

  ma_b(scratch2, zero, overflow, Assembler::LessThan, ShortJump);
}

void MacroAssemblerRiscv64::ma_addPtrTestOverflow(Register rd, Register rj,
                                                  Register rk,
                                                  Label* overflow) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(rd != scratch);

  if (rj == rk) {
    if (rj == rd) {
      ma_mv(scratch, rj);
      rj = scratch;
    }

    add(rd, rj, rj);
    xor_(scratch, rj, rd);
    ma_b(scratch, zero, overflow, Assembler::LessThan, ShortJump);
  } else {
    UseScratchRegisterScope temps(this);
    Register scratch2 = temps.Acquire();
    MOZ_ASSERT(rj != scratch);
    MOZ_ASSERT(rd != scratch2);

    if (rj == rd) {
      ma_mv(scratch2, rj);
      rj = scratch2;
    }

    add(rd, rj, rk);
    slti(scratch, rj, 0);
    slt(scratch2, rd, rk);
    ma_b(scratch, scratch2, overflow, Assembler::NotEqual, ShortJump);
  }
}

void MacroAssemblerRiscv64::ma_addPtrTestOverflow(Register rd, Register rj,
                                                  Imm32 imm, Label* overflow) {
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();

  if (imm.value == 0) {
    ma_mv(rd, rj);
    return;
  }

  if (rj == rd) {
    ma_mv(scratch2, rj);
    rj = scratch2;
  }

  ma_add64(rd, rj, imm);

  if (imm.value > 0) {
    ma_b(rd, rj, overflow, Assembler::LessThan, ShortJump);
  } else {
    MOZ_ASSERT(imm.value < 0);
    ma_b(rd, rj, overflow, Assembler::GreaterThan, ShortJump);
  }
}

void MacroAssemblerRiscv64::ma_addPtrTestOverflow(Register rd, Register rj,
                                                  ImmWord imm,
                                                  Label* overflow) {
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();

  if (imm.value == 0) {
    ma_mv(rd, rj);
    return;
  }

  if (rj == rd) {
    MOZ_ASSERT(rj != scratch2);
    ma_mv(scratch2, rj);
    rj = scratch2;
  }

  ma_li(rd, imm);
  add(rd, rj, rd);

  if (imm.value > 0) {
    ma_b(rd, rj, overflow, Assembler::LessThan, ShortJump);
  } else {
    MOZ_ASSERT(imm.value < 0);
    ma_b(rd, rj, overflow, Assembler::GreaterThan, ShortJump);
  }
}

void MacroAssemblerRiscv64::ma_add32TestCarry(Condition cond, Register rd,
                                              Register rj, Register rk,
                                              Label* overflow) {
  MOZ_ASSERT(cond == Assembler::CarrySet || cond == Assembler::CarryClear);
  MOZ_ASSERT_IF(rd == rj, rk != rd);
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  addw(rd, rj, rk);
  sltu(scratch, rd, rd == rj ? rk : rj);
  ma_b(scratch, scratch, overflow,
       cond == Assembler::CarrySet ? Assembler::NonZero : Assembler::Zero,
       ShortJump);
}

void MacroAssemblerRiscv64::ma_add32TestCarry(Condition cond, Register rd,
                                              Register rj, Imm32 imm,
                                              Label* overflow) {
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();
  MOZ_ASSERT(rj != scratch2);
  ma_li(scratch2, imm);
  ma_add32TestCarry(cond, rd, rj, scratch2, overflow);
}

void MacroAssemblerRiscv64::ma_subPtrTestOverflow(Register rd, Register rj,
                                                  Imm32 imm, Label* overflow) {
  // TODO(riscv): Check subPtrTestOverflow
  MOZ_ASSERT(imm.value != INT32_MIN);
  ma_addPtrTestOverflow(rd, rj, Imm32(-imm.value), overflow);
}

void MacroAssemblerRiscv64::ma_addPtrTestCarry(Condition cond, Register rd,
                                               Register rj, Register rk,
                                               Label* overflow) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  MOZ_ASSERT(rd != rk);
  MOZ_ASSERT(rd != scratch);
  add(rd, rj, rk);
  sltu(scratch, rd, rk);
  ma_b(scratch, scratch, overflow,
       cond == Assembler::CarrySet ? Assembler::NonZero : Assembler::Zero,
       ShortJump);
}

void MacroAssemblerRiscv64::ma_addPtrTestCarry(Condition cond, Register rd,
                                               Register rj, Imm32 imm,
                                               Label* overflow) {
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();

  // Check for signed range because of addi
  if (is_int12(imm.value)) {
    ma_add64(rd, rj, imm);
    sltiu(scratch2, rd, imm.value);
    ma_b(scratch2, scratch2, overflow,
         cond == Assembler::CarrySet ? Assembler::NonZero : Assembler::Zero,
         ShortJump);
  } else {
    ma_li(scratch2, imm);
    ma_addPtrTestCarry(cond, rd, rj, scratch2, overflow);
  }
}

void MacroAssemblerRiscv64::ma_addPtrTestCarry(Condition cond, Register rd,
                                               Register rj, ImmWord imm,
                                               Label* overflow) {
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();

  // Check for signed range because of addi_d
  if (is_int12(imm.value)) {
    ma_add64(rd, rj, imm);
    sltiu(scratch2, rd, imm.value);
    ma_b(scratch2, scratch2, overflow,
         cond == Assembler::CarrySet ? Assembler::NonZero : Assembler::Zero,
         ShortJump);
  } else {
    ma_li(scratch2, imm);
    ma_addPtrTestCarry(cond, rd, rj, scratch2, overflow);
  }
}

void MacroAssemblerRiscv64::ma_addPtrTestSigned(Condition cond, Register rd,
                                                Register rj, Register rk,
                                                Label* taken) {
  MOZ_ASSERT(cond == Assembler::Signed || cond == Assembler::NotSigned);

  add(rd, rj, rk);
  ma_b(rd, rd, taken, cond, ShortJump);
}

void MacroAssemblerRiscv64::ma_addPtrTestSigned(Condition cond, Register rd,
                                                Register rj, Imm32 imm,
                                                Label* taken) {
  MOZ_ASSERT(cond == Assembler::Signed || cond == Assembler::NotSigned);

  ma_add64(rd, rj, imm);
  ma_b(rd, rd, taken, cond, ShortJump);
}

void MacroAssemblerRiscv64::ma_addPtrTestSigned(Condition cond, Register rd,
                                                Register rj, ImmWord imm,
                                                Label* taken) {
  MOZ_ASSERT(cond == Assembler::Signed || cond == Assembler::NotSigned);

  ma_add64(rd, rj, imm);
  ma_b(rd, rd, taken, cond, ShortJump);
}

FaultingCodeRange MacroAssemblerRiscv64::ma_load(Register dest,
                                                 const BaseIndex& src,
                                                 LoadStoreSize size,
                                                 LoadStoreExtension extension) {
  UseScratchRegisterScope temps(this);
  Address address = computeScaledAddress(src, temps);
  return ma_load(dest, address, size, extension);
}
void MacroAssemblerRiscv64::ma_pop(FloatRegister f) {
  if (f.isDouble()) {
    fld(f, StackPointer, 0);
  } else {
    MOZ_ASSERT(f.isSingle(), "simd128 is not supported");
    flw(f, StackPointer, 0);
  }
  // See also MacroAssemblerRiscv64::ma_push -- Free space for double even when
  // storing a float.
  addi(StackPointer, StackPointer, sizeof(double));
}

void MacroAssemblerRiscv64::ma_push(FloatRegister f) {
  // We allocate space for double even when storing a float.
  addi(StackPointer, StackPointer, (int32_t)-sizeof(double));
  if (f.isDouble()) {
    fsd(f, StackPointer, 0);
  } else {
    MOZ_ASSERT(f.isSingle(), "simd128 is not supported");
    fsw(f, StackPointer, 0);
  }
}

BufferOffset MacroAssemblerRiscv64::ma_call(ImmPtr dest) {
  // 6 instruction to materialize the constant.
  // + 1 instruction for jalr.
  AutoForbidPoolsAndNops afp(this, 7);

  UseScratchRegisterScope temps(this);
  temps.Acquire(CallReg);

  BufferOffset offset = ma_liPatchable(CallReg, dest);
  jalr(CallReg);
  return offset;
}

void MacroAssemblerRiscv64::CompareIsNotNanF32(Register rd, FPURegister cmp1,
                                               FPURegister cmp2) {
  feq_s(rd, cmp1, cmp1);  // rd <- !isNan(cmp1)
  if (cmp1 != cmp2) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();

    feq_s(scratch, cmp2, cmp2);  // scratch <- !isNaN(cmp2)
    and_(rd, rd, scratch);       // rd <- !isNan(cmp1) && !isNan(cmp2)
  }
}

void MacroAssemblerRiscv64::CompareIsNotNanF64(Register rd, FPURegister cmp1,
                                               FPURegister cmp2) {
  feq_d(rd, cmp1, cmp1);  // rd <- !isNan(cmp1)
  if (cmp1 != cmp2) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();

    feq_d(scratch, cmp2, cmp2);  // scratch <- !isNaN(cmp2)
    and_(rd, rd, scratch);       // rd <- !isNan(cmp1) && !isNan(cmp2)
  }
}

void MacroAssemblerRiscv64::CompareIsNanF32(Register rd, FPURegister cmp1,
                                            FPURegister cmp2) {
  CompareIsNotNanF32(rd, cmp1, cmp2);  // rd <- !isNan(cmp1) && !isNan(cmp2)
  NegateBool(rd, rd);                  // rd <- isNan(cmp1) || isNan(cmp2)
}

void MacroAssemblerRiscv64::CompareIsNanF64(Register rd, FPURegister cmp1,
                                            FPURegister cmp2) {
  CompareIsNotNanF64(rd, cmp1, cmp2);  // rd <- !isNan(cmp1) && !isNan(cmp2)
  NegateBool(rd, rd);                  // rd <- isNan(cmp1) || isNan(cmp2)
}

void MacroAssemblerRiscv64::BranchFloat32(DoubleCondition cc,
                                          FloatRegister frs1,
                                          FloatRegister frs2, Label* L,
                                          JumpKind jumpKind) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  ma_compareF32(scratch, cc, frs1, frs2);
  ma_b(scratch, Imm32(0), L, NotEqual, jumpKind);
}

void MacroAssemblerRiscv64::BranchFloat64(DoubleCondition cc,
                                          FloatRegister frs1,
                                          FloatRegister frs2, Label* L,
                                          JumpKind jumpKind) {
  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();
  ma_compareF64(scratch, cc, frs1, frs2);
  ma_b(scratch, Imm32(0), L, NotEqual, jumpKind);
}

void MacroAssemblerRiscv64::Clz32(Register rd, Register rs) {
  if (HasZbbExtension()) {
    clzw(rd, rs);
    return;
  }

  // 32 bit unsigned in lower word: count number of leading zeros.
  //  int n = 32;
  //  unsigned y;

  //  y = x >>16; if (y != 0) { n = n -16; x = y; }
  //  y = x >> 8; if (y != 0) { n = n - 8; x = y; }
  //  y = x >> 4; if (y != 0) { n = n - 4; x = y; }
  //  y = x >> 2; if (y != 0) { n = n - 2; x = y; }
  //  y = x >> 1; if (y != 0) {rd = n - 2; return;}
  //  rd = n - x;

  Label L0, L1, L2, L3, L4;
  UseScratchRegisterScope temps(this);
  Register x = rd;
  Register y = temps.Acquire();
  Register n = temps.Acquire();
  MOZ_ASSERT(rs != y && rs != n);
  ma_mv(x, rs);
  ma_li(n, Imm32(32));
  srliw(y, x, 16);
  ma_b(y, y, &L0, Zero, ShortJump);
  ma_mv(x, y);
  addiw(n, n, -16);
  bind(&L0);
  srliw(y, x, 8);
  ma_b(y, y, &L1, Zero, ShortJump);
  addiw(n, n, -8);
  ma_mv(x, y);
  bind(&L1);
  srliw(y, x, 4);
  ma_b(y, y, &L2, Zero, ShortJump);
  addiw(n, n, -4);
  ma_mv(x, y);
  bind(&L2);
  srliw(y, x, 2);
  ma_b(y, y, &L3, Zero, ShortJump);
  addiw(n, n, -2);
  ma_mv(x, y);
  bind(&L3);
  srliw(y, x, 1);
  subw(rd, n, x);
  ma_b(y, y, &L4, Zero, ShortJump);
  addiw(rd, n, -2);
  bind(&L4);
}

void MacroAssemblerRiscv64::Clz64(Register rd, Register rs) {
  if (HasZbbExtension()) {
    clz(rd, rs);
    return;
  }

  // 64 bit: count number of leading zeros.
  //  int n = 64;
  //  unsigned y;

  //  y = x >>32; if (y != 0) { n = n - 32; x = y; }
  //  y = x >>16; if (y != 0) { n = n - 16; x = y; }
  //  y = x >> 8; if (y != 0) { n = n - 8; x = y; }
  //  y = x >> 4; if (y != 0) { n = n - 4; x = y; }
  //  y = x >> 2; if (y != 0) { n = n - 2; x = y; }
  //  y = x >> 1; if (y != 0) {rd = n - 2; return;}
  //  rd = n - x;

  Label L0, L1, L2, L3, L4, L5;
  UseScratchRegisterScope temps(this);
  Register x = rd;
  Register y = temps.Acquire();
  Register n = temps.Acquire();
  MOZ_ASSERT(rs != y && rs != n);
  ma_mv(x, rs);
  ma_li(n, Imm32(64));
  srli(y, x, 32);
  ma_b(y, y, &L0, Zero, ShortJump);
  addiw(n, n, -32);
  ma_mv(x, y);
  bind(&L0);
  srli(y, x, 16);
  ma_b(y, y, &L1, Zero, ShortJump);
  addiw(n, n, -16);
  ma_mv(x, y);
  bind(&L1);
  srli(y, x, 8);
  ma_b(y, y, &L2, Zero, ShortJump);
  addiw(n, n, -8);
  ma_mv(x, y);
  bind(&L2);
  srli(y, x, 4);
  ma_b(y, y, &L3, Zero, ShortJump);
  addiw(n, n, -4);
  ma_mv(x, y);
  bind(&L3);
  srli(y, x, 2);
  ma_b(y, y, &L4, Zero, ShortJump);
  addiw(n, n, -2);
  ma_mv(x, y);
  bind(&L4);
  srli(y, x, 1);
  subw(rd, n, x);
  ma_b(y, y, &L5, Zero, ShortJump);
  addiw(rd, n, -2);
  bind(&L5);
}

void MacroAssemblerRiscv64::Ctz32(Register rd, Register rs) {
  if (HasZbbExtension()) {
    ctzw(rd, rs);
    return;
  }

  // Convert trailing zeroes to trailing ones, and bits to their left
  // to zeroes.

  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    addi(scratch, rs, -1);
    xor_(rd, scratch, rs);
    and_(rd, rd, scratch);
    // Count number of leading zeroes.
  }
  Clz32(rd, rd);
  {
    // Subtract number of leading zeroes from 32 to get number of trailing
    // ones. Remember that the trailing ones were formerly trailing zeroes.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    ma_li(scratch, Imm32(32));
    subw(rd, scratch, rd);
  }
}

void MacroAssemblerRiscv64::Ctz64(Register rd, Register rs) {
  if (HasZbbExtension()) {
    ctz(rd, rs);
    return;
  }

  // Convert trailing zeroes to trailing ones, and bits to their left
  // to zeroes.
  {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    addi(scratch, rs, -1);
    xor_(rd, scratch, rs);
    and_(rd, rd, scratch);
    // Count number of leading zeroes.
  }
  Clz64(rd, rd);
  {
    // Subtract number of leading zeroes from 64 to get number of trailing
    // ones. Remember that the trailing ones were formerly trailing zeroes.
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();
    ma_li(scratch, 64);
    sub(rd, scratch, rd);
  }
}

void MacroAssemblerRiscv64::Popcnt32(Register rd, Register rs,
                                     Register scratch) {
  if (HasZbbExtension()) {
    cpopw(rd, rs);
    return;
  }

  MOZ_ASSERT(scratch != rs);
  MOZ_ASSERT(scratch != rd);
  // https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
  //
  // A generalization of the best bit counting method to integers of
  // bit-widths up to 128 (parameterized by type T) is this:
  //
  // v = v - ((v >> 1) & (T)~(T)0/3);                           // temp
  // v = (v & (T)~(T)0/15*3) + ((v >> 2) & (T)~(T)0/15*3);      // temp
  // v = (v + (v >> 4)) & (T)~(T)0/255*15;                      // temp
  // c = (T)(v * ((T)~(T)0/255)) >> (sizeof(T) - 1) * BITS_PER_BYTE; //count
  //
  // There are algorithms which are faster in the cases where very few
  // bits are set but the algorithm here attempts to minimize the total
  // number of instructions executed even when a large number of bits
  // are set.
  // The number of instruction is 20.
  // uint32_t B0 = 0x55555555;     // (T)~(T)0/3
  // uint32_t B1 = 0x33333333;     // (T)~(T)0/15*3
  // uint32_t B2 = 0x0F0F0F0F;     // (T)~(T)0/255*15
  // uint32_t value = 0x01010101;  // (T)~(T)0/255

  uint32_t shift = 24;
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();
  Register value = temps.Acquire();
  MOZ_ASSERT((rd != value) && (rs != value));
  ma_li(value, 0x01010101);     // value = 0x01010101;
  ma_li(scratch2, 0x55555555);  // B0 = 0x55555555;
  srliw(scratch, rs, 1);
  and_(scratch, scratch, scratch2);
  subw(scratch, rs, scratch);
  ma_li(scratch2, 0x33333333);  // B1 = 0x33333333;
  slli(rd, scratch2, 4);
  or_(scratch2, scratch2, rd);
  and_(rd, scratch, scratch2);
  srliw(scratch, scratch, 2);
  and_(scratch, scratch, scratch2);
  addw(scratch, rd, scratch);
  srliw(rd, scratch, 4);
  addw(rd, rd, scratch);
  ma_li(scratch2, 0xF);
  mulw(scratch2, value, scratch2);  // B2 = 0x0F0F0F0F;
  and_(rd, rd, scratch2);
  mulw(rd, rd, value);
  srliw(rd, rd, shift);
}

void MacroAssemblerRiscv64::Popcnt64(Register rd, Register rs,
                                     Register scratch) {
  if (HasZbbExtension()) {
    cpop(rd, rs);
    return;
  }

  MOZ_ASSERT(scratch != rs);
  MOZ_ASSERT(scratch != rd);
  // uint64_t B0 = 0x5555555555555555l;     // (T)~(T)0/3
  // uint64_t B1 = 0x3333333333333333l;     // (T)~(T)0/15*3
  // uint64_t B2 = 0x0F0F0F0F0F0F0F0Fl;     // (T)~(T)0/255*15
  // uint64_t value = 0x0101010101010101l;  // (T)~(T)0/255
  // uint64_t shift = 24;                   // (sizeof(T) - 1) * BITS_PER_BYTE
  uint64_t shift = 24;
  UseScratchRegisterScope temps(this);
  Register scratch2 = temps.Acquire();
  Register value = temps.Acquire();
  MOZ_ASSERT((rd != value) && (rs != value));
  ma_li(value, 0x1111111111111111l);  // value = 0x1111111111111111l;
  ma_li(scratch2, 5);
  mul(scratch2, value, scratch2);  // B0 = 0x5555555555555555l;
  srli(scratch, rs, 1);
  and_(scratch, scratch, scratch2);
  sub(scratch, rs, scratch);
  ma_li(scratch2, 3);
  mul(scratch2, value, scratch2);  // B1 = 0x3333333333333333l;
  and_(rd, scratch, scratch2);
  srli(scratch, scratch, 2);
  and_(scratch, scratch, scratch2);
  add(scratch, rd, scratch);
  srli(rd, scratch, 4);
  add(rd, rd, scratch);
  ma_li(scratch2, 0xF);
  ma_li(value, 0x0101010101010101l);  // value = 0x0101010101010101l;
  mul(scratch2, value, scratch2);     // B2 = 0x0F0F0F0F0F0F0F0Fl;
  and_(rd, rd, scratch2);
  mul(rd, rd, value);
  srli(rd, rd, 32 + shift);
}

void MacroAssemblerRiscv64::ByteSwap(Register dest, Register src,
                                     int operand_size, bool zeroExtend) {
  MOZ_ASSERT(operand_size == 2 || operand_size == 4 || operand_size == 8);
  MOZ_ASSERT_IF(zeroExtend, operand_size == 2);

  if (HasZbbExtension()) {
    rev8(dest, src);
    if (operand_size == 4) {
      srai(dest, dest, 32);
    } else if (operand_size == 2) {
      if (zeroExtend) {
        srli(dest, dest, 48);
      } else {
        srai(dest, dest, 48);
      }
    }
    return;
  }

  if (operand_size == 2) {
    UseScratchRegisterScope temps(this);
    Register scratch = temps.Acquire();

    slli(scratch, src, 48);
    srli(scratch, scratch, 56);
    slli(dest, src, 56);
    if (zeroExtend) {
      srli(dest, dest, 48);
    } else {
      srai(dest, dest, 48);
    }
    or_(dest, dest, scratch);
  } else if (operand_size == 4) {
    // TODO: It's unclear why forbidding pools is necessary here. It should
    // either be documented or pools should be allowed.
    AutoForbidPoolsAndNops afp(this, 17);

    // Uint32_t x1 = 0x00FF00FF;
    // x0 = (x0 << 16 | x0 >> 16);
    // x0 = (((x0 & x1) << 8)  | ((x0 & (x1 << 8)) >> 8));
    UseScratchRegisterScope temps(this);
    Register x0 = temps.Acquire();
    Register x1 = temps.Acquire();
    Register x2 = temps.Acquire();
    RV_li(x1, 0x00FF00FF);
    slliw(x0, src, 16);
    srliw(dest, src, 16);
    or_(x0, dest, x0);   // x0 <- x0 << 16 | x0 >> 16
    and_(x2, x0, x1);    // x2 <- x0 & 0x00FF00FF
    slliw(x2, x2, 8);    // x2 <- (x0 & x1) << 8
    slliw(x1, x1, 8);    // x1 <- 0xFF00FF00
    and_(dest, x0, x1);  // x0 & 0xFF00FF00
    srliw(dest, dest, 8);
    or_(dest, dest, x2);  // (((x0 & x1) << 8)  | ((x0 & (x1 << 8)) >> 8))
  } else {
    // TODO: It's unclear why forbidding pools is necessary here. It should
    // either be documented or pools should be allowed.
    AutoForbidPoolsAndNops afp(this, 30);

    // uinx24_t x1 = 0x0000FFFF0000FFFFl;
    // uinx24_t x1 = 0x00FF00FF00FF00FFl;
    // x0 = (x0 << 32 | x0 >> 32);
    // x0 = (x0 & x1) << 16 | (x0 & (x1 << 16)) >> 16;
    // x0 = (x0 & x1) << 8  | (x0 & (x1 << 8)) >> 8;
    UseScratchRegisterScope temps(this);
    Register x0 = temps.Acquire();
    Register x1 = temps.Acquire();
    Register x2 = temps.Acquire();
    RV_li(x1, 0x0000FFFF0000FFFFl);
    slli(x0, src, 32);
    srli(dest, src, 32);
    or_(x0, dest, x0);     // x0 <- x0 << 32 | x0 >> 32
    and_(x2, x0, x1);      // x2 <- x0 & 0x0000FFFF0000FFFF
    slli(x2, x2, 16);      // x2 <- (x0 & 0x0000FFFF0000FFFF) << 16
    slli(x1, x1, 16);      // x1 <- 0xFFFF0000FFFF0000
    and_(dest, x0, x1);    // rd <- x0 & 0xFFFF0000FFFF0000
    srli(dest, dest, 16);  // rd <- x0 & (x1 << 16)) >> 16
    or_(x0, dest, x2);     // (x0 & x1) << 16 | (x0 & (x1 << 16)) >> 16;
    RV_li(x1, 0x00FF00FF00FF00FFl);
    and_(x2, x0, x1);  // x2 <- x0 & 0x00FF00FF00FF00FF
    slli(x2, x2, 8);   // x2 <- (x0 & x1) << 8
    slli(x1, x1, 8);   // x1 <- 0xFF00FF00FF00FF00
    and_(dest, x0, x1);
    srli(dest, dest, 8);  // rd <- (x0 & (x1 << 8)) >> 8
    or_(dest, dest, x2);  // (((x0 & x1) << 8)  | ((x0 & (x1 << 8)) >> 8))
  }
}

template <typename F_TYPE>
void MacroAssemblerRiscv64::FloatMinMaxHelper(FPURegister dst, FPURegister src1,
                                              FPURegister src2, MaxMinKind kind,
                                              bool handleNaN) {
  MOZ_ASSERT((std::is_same<F_TYPE, float>::value) ||
             (std::is_same<F_TYPE, double>::value));

  if (src1 == src2) {
    if (dst != src1) {
      if (std::is_same<float, F_TYPE>::value) {
        fmv_s(dst, src1);
      } else {
        fmv_d(dst, src1);
      }
    }
    return;
  }

  // The Zfa extension adds fminm.{s,d} and fmaxm.{s,d} which return NaN if
  // either operand is NaN, which is exactly what we need for JS semantics.
  if (HasZfaExtension()) {
    if (kind == MaxMinKind::kMax) {
      if (std::is_same_v<float, F_TYPE>) {
        fmaxm_s(dst, src1, src2);
      } else {
        fmaxm_d(dst, src1, src2);
      }
    } else {
      if (std::is_same_v<float, F_TYPE>) {
        fminm_s(dst, src1, src2);
      } else {
        fminm_d(dst, src1, src2);
      }
    }
    return;
  }

  if (!handleNaN) {
    if (kind == MaxMinKind::kMax) {
      if (std::is_same_v<float, F_TYPE>) {
        fmax_s(dst, src1, src2);
      } else {
        fmax_d(dst, src1, src2);
      }
    } else {
      if (std::is_same_v<float, F_TYPE>) {
        fmin_s(dst, src1, src2);
      } else {
        fmin_d(dst, src1, src2);
      }
    }
    return;
  }

  Label done, nan;

  // For RISCV, fmin_s returns the other non-NaN operand as result if only one
  // operand is NaN; but for JS, if any operand is NaN, result is Nan. The
  // following handles the discrepency between handling of NaN between ISA and
  // JS semantics
  if (std::is_same<float, F_TYPE>::value) {
    BranchFloat32(Assembler::DoubleUnordered, src1, src2, &nan, ShortJump);
  } else {
    BranchFloat64(Assembler::DoubleUnordered, src1, src2, &nan, ShortJump);
  }

  if (kind == MaxMinKind::kMax) {
    if (std::is_same<float, F_TYPE>::value) {
      fmax_s(dst, src1, src2);
    } else {
      fmax_d(dst, src1, src2);
    }
  } else {
    if (std::is_same<float, F_TYPE>::value) {
      fmin_s(dst, src1, src2);
    } else {
      fmin_d(dst, src1, src2);
    }
  }
  jump(&done);

  bind(&nan);
  // if any operand is NaN, return NaN (fadd returns NaN if any operand is NaN)
  if (std::is_same<float, F_TYPE>::value) {
    fadd_s(dst, src1, src2);
  } else {
    fadd_d(dst, src1, src2);
  }

  bind(&done);
}

void MacroAssemblerRiscv64::Float32Max(FPURegister dst, FPURegister src1,
                                       FPURegister src2, bool handleNaN) {
  FloatMinMaxHelper<float>(dst, src1, src2, MaxMinKind::kMax, handleNaN);
}

void MacroAssemblerRiscv64::Float32Min(FPURegister dst, FPURegister src1,
                                       FPURegister src2, bool handleNaN) {
  FloatMinMaxHelper<float>(dst, src1, src2, MaxMinKind::kMin, handleNaN);
}

void MacroAssemblerRiscv64::Float64Max(FPURegister dst, FPURegister src1,
                                       FPURegister src2, bool handleNaN) {
  FloatMinMaxHelper<double>(dst, src1, src2, MaxMinKind::kMax, handleNaN);
}

void MacroAssemblerRiscv64::Float64Min(FPURegister dst, FPURegister src1,
                                       FPURegister src2, bool handleNaN) {
  FloatMinMaxHelper<double>(dst, src1, src2, MaxMinKind::kMin, handleNaN);
}

void MacroAssemblerRiscv64::Rol(Register rd, Register rs, Imm32 rt) {
  Ror(rd, rs, Imm32(32 - (rt.value & 0x1f)));
}

void MacroAssemblerRiscv64::Rol(Register rd, Register rs, Register rt) {
  if (HasZbbExtension()) {
    rolw(rd, rs, rt);
    return;
  }

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  negw(scratch, rt);
  srlw(scratch, rs, scratch);
  sllw(rd, rs, rt);
  or_(rd, scratch, rd);
  sext_w(rd, rd);
}

void MacroAssemblerRiscv64::Ror(Register rd, Register rs, Imm32 rt) {
  int32_t ror_value = rt.value & 0x1f;
  if (ror_value == 0) {
    ma_mv(rd, rs);
    return;
  }

  if (HasZbbExtension()) {
    roriw(rd, rs, ror_value);
    return;
  }

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  srliw(scratch, rs, ror_value);
  slliw(rd, rs, 32 - ror_value);
  or_(rd, scratch, rd);
  sext_w(rd, rd);
}

void MacroAssemblerRiscv64::Ror(Register rd, Register rs, Register rt) {
  if (HasZbbExtension()) {
    rorw(rd, rs, rt);
    return;
  }

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  negw(scratch, rt);
  sllw(scratch, rs, scratch);
  srlw(rd, rs, rt);
  or_(rd, scratch, rd);
  sext_w(rd, rd);
}

void MacroAssemblerRiscv64::Drol(Register rd, Register rs, Imm32 rt) {
  Dror(rd, rs, Imm32(64 - (rt.value & 0x3f)));
}

void MacroAssemblerRiscv64::Drol(Register rd, Register rs, Register rt) {
  if (HasZbbExtension()) {
    rol(rd, rs, rt);
    return;
  }

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  negw(scratch, rt);
  srl(scratch, rs, scratch);
  sll(rd, rs, rt);
  or_(rd, scratch, rd);
}

void MacroAssemblerRiscv64::Dror(Register rd, Register rs, Imm32 rt) {
  int32_t dror_value = rt.value & 0x3f;
  if (dror_value == 0) {
    ma_mv(rd, rs);
    return;
  }

  if (HasZbbExtension()) {
    rori(rd, rs, dror_value);
    return;
  }

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  srli(scratch, rs, dror_value);
  slli(rd, rs, 64 - dror_value);
  or_(rd, scratch, rd);
}

void MacroAssemblerRiscv64::Dror(Register rd, Register rs, Register rt) {
  if (HasZbbExtension()) {
    ror(rd, rs, rt);
    return;
  }

  UseScratchRegisterScope temps(this);
  Register scratch = temps.Acquire();

  negw(scratch, rt);
  sll(scratch, rs, scratch);
  srl(rd, rs, rt);
  or_(rd, scratch, rd);
}

FaultingCodeRange MacroAssemblerRiscv64::wasmLoadImpl(
    const wasm::MemoryAccessDesc& access, Register memoryBase, Register ptr,
    AnyRegister output, wasm::ZeroExtendIndex zeroExtend) {
  BaseIndex address(memoryBase, ptr, TimesOne, access.offset32());
  return wasmLoadImpl(access, address, output, zeroExtend);
}

FaultingCodeRange MacroAssemblerRiscv64::wasmLoadImpl(
    const wasm::MemoryAccessDesc& access, const BaseIndex& address,
    AnyRegister output, wasm::ZeroExtendIndex zeroExtend) {
  access.assertOffsetInGuardPages();

  UseScratchRegisterScope temps(this);
  Address addr = computeScaledAddress(address, temps, zeroExtend);

  asMasm().memoryBarrierBefore(access.sync());

  FaultingCodeRange fcr;
  switch (access.type()) {
    case Scalar::Int8:
      fcr = ma_load(output.gpr(), addr, SizeByte, SignExtend);
      break;
    case Scalar::Uint8:
      fcr = ma_load(output.gpr(), addr, SizeByte, ZeroExtend);
      break;
    case Scalar::Int16:
      fcr = ma_load(output.gpr(), addr, SizeHalfWord, SignExtend);
      break;
    case Scalar::Uint16:
      fcr = ma_load(output.gpr(), addr, SizeHalfWord, ZeroExtend);
      break;
    case Scalar::Int32:
      fcr = ma_load(output.gpr(), addr, SizeWord, SignExtend);
      break;
    case Scalar::Uint32:
      fcr = ma_load(output.gpr(), addr, SizeWord, ZeroExtend);
      break;
    case Scalar::Int64:
      fcr = ma_load(output.gpr(), addr, SizeDouble, SignExtend);
      break;
    case Scalar::Float32:
      fcr = ma_loadFloat(output.fpu(), addr);
      break;
    case Scalar::Float64:
      fcr = ma_loadDouble(output.fpu(), addr);
      break;
    default:
      MOZ_CRASH("unexpected array type");
  }

  asMasm().appendAndVerify(
      access, js::wasm::TrapMachineInsnForLoad(access.byteSize()), fcr);
  asMasm().memoryBarrierAfter(access.sync());
  return fcr;
}

FaultingCodeRange MacroAssemblerRiscv64::wasmStoreImpl(
    const wasm::MemoryAccessDesc& access, AnyRegister value,
    Register memoryBase, Register ptr, wasm::ZeroExtendIndex zeroExtend) {
  BaseIndex address(memoryBase, ptr, TimesOne, access.offset32());
  return wasmStoreImpl(access, value, address, zeroExtend);
}

FaultingCodeRange MacroAssemblerRiscv64::wasmStoreImpl(
    const wasm::MemoryAccessDesc& access, AnyRegister value,
    const BaseIndex& address, wasm::ZeroExtendIndex zeroExtend) {
  access.assertOffsetInGuardPages();

  UseScratchRegisterScope temps(this);
  Address addr = computeScaledAddress(address, temps, zeroExtend);

  asMasm().memoryBarrierBefore(access.sync());

  FaultingCodeRange fcr;
  switch (access.type()) {
    case Scalar::Int8:
      fcr = ma_store(value.gpr(), addr, SizeByte, SignExtend);
      break;
    case Scalar::Uint8:
      fcr = ma_store(value.gpr(), addr, SizeByte, ZeroExtend);
      break;
    case Scalar::Int16:
      fcr = ma_store(value.gpr(), addr, SizeHalfWord, SignExtend);
      break;
    case Scalar::Uint16:
      fcr = ma_store(value.gpr(), addr, SizeHalfWord, ZeroExtend);
      break;
    case Scalar::Int32:
      fcr = ma_store(value.gpr(), addr, SizeWord, SignExtend);
      break;
    case Scalar::Uint32:
      fcr = ma_store(value.gpr(), addr, SizeWord, ZeroExtend);
      break;
    case Scalar::Int64:
      fcr = ma_store(value.gpr(), addr, SizeDouble, SignExtend);
      break;
    case Scalar::Float32:
      fcr = ma_storeFloat(value.fpu(), addr);
      break;
    case Scalar::Float64:
      fcr = ma_storeDouble(value.fpu(), addr);
      break;
    default:
      MOZ_CRASH("unexpected array type");
  }

  // Only the last emitted instruction is a memory access.
  asMasm().appendAndVerify(
      access, js::wasm::TrapMachineInsnForStore(access.byteSize()), fcr);
  asMasm().memoryBarrierAfter(access.sync());
  return fcr;
}

BaseIndex MacroAssemblerRiscv64::toBaseIndex(Register base, uint64_t address,
                                             UseScratchRegisterScope& temps) {
  Register index;
  int32_t offset;
  if (is_int32(address)) {
    index = zero;
    offset = int32_t(address);
  } else {
    Register scratch = temps.Acquire();
    ma_li(scratch, Imm64(address));
    index = scratch;
    offset = 0;
  }
  return BaseIndex{base, index, TimesOne, offset};
}

void MacroAssemblerRiscv64::wasmLoadAbsoluteImpl(
    const wasm::MemoryAccessDesc& access, Register memoryBase, uint64_t offset,
    AnyRegister output) {
  UseScratchRegisterScope temps(this);
  BaseIndex address = toBaseIndex(memoryBase, offset, temps);
  wasmLoadImpl(access, address, output, wasm::ZeroExtendIndex::No);
}

void MacroAssemblerRiscv64::wasmStoreAbsoluteImpl(
    const wasm::MemoryAccessDesc& access, AnyRegister value,
    Register memoryBase, uint64_t offset) {
  UseScratchRegisterScope temps(this);
  BaseIndex address = toBaseIndex(memoryBase, offset, temps);
  wasmStoreImpl(access, value, address, wasm::ZeroExtendIndex::No);
}

void MacroAssemblerRiscv64::ma_fmv_d(FloatRegister src, ValueOperand dest) {
  fmv_x_d(dest.valueReg(), src);
}

void MacroAssemblerRiscv64::ma_fmv_d(ValueOperand src, FloatRegister dest) {
  fmv_d_x(dest, src.valueReg());
}

void MacroAssemblerRiscv64::ma_fmv_w(FloatRegister src, ValueOperand dest) {
  fmv_x_w(dest.valueReg(), src);
}

void MacroAssemblerRiscv64::ma_fmv_w(ValueOperand src, FloatRegister dest) {
  fmv_w_x(dest, src.valueReg());
}

}  // namespace jit
}  // namespace js
