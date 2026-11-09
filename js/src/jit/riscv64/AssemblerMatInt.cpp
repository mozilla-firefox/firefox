/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
//===- RISCVMatInt.cpp - Immediate materialisation -------------*- C++
//
//  Part of the LLVM Project, under the Apache License v2.0 with LLVM
//  Exceptions. See https://llvm.org/LICENSE.txt for license information.
//  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mozilla/Assertions.h"
#include "mozilla/MathAlgorithms.h"

#include <algorithm>
#include <array>
#include <bit>
#include <type_traits>

#include "jit/riscv64/Assembler-riscv64.h"
#include "jit/riscv64/base/Integer.h"

using namespace js::jit;

class Inst : public InstructionBase {
  Instr instr_{};

 public:
  operator Instr() const { return instr_; }
};

class InstSeq {
  // Maximum number of instructions needed to materialize a 64-bit immediate.
  static constexpr size_t MaxLength = 8;

  std::array<Inst, MaxLength> insts_;
  size_t size_ = 0;

  auto* next() {
    MOZ_RELEASE_ASSERT(size_ < MaxLength);
    return &insts_[size_++];
  }

 public:
  bool empty() const { return size_ == 0; }
  size_t size() const { return size_; }

  auto begin() { return insts_.begin(); }
  auto begin() const { return insts_.begin(); }

  auto end() { return std::next(insts_.begin(), size_); }
  auto end() const { return std::next(insts_.begin(), size_); }

  auto& operator[](size_t i) {
    MOZ_RELEASE_ASSERT(i < size_);
    return insts_[i];
  }

  const auto& operator[](size_t i) const {
    MOZ_RELEASE_ASSERT(i < size_);
    return insts_[i];
  }

  // Remove all instructions.
  void clear() { size_ = 0; }

  // Remove the first instruction in a non-empty instruction sequence.
  void erase_front() {
    MOZ_RELEASE_ASSERT(!empty());
    std::move(std::next(insts_.begin()), insts_.end(), insts_.begin());
    size_--;
  }

  // Append all instructions from another sequence.
  void append(const InstSeq& other) {
    MOZ_RELEASE_ASSERT(size_ + other.size_ <= MaxLength);
    std::copy(other.begin(), other.end(), end());
    size_ += other.size_;
  }

  void assertRegisters(Register rd) const;

  // Instruction methods.

  void lui(Register rd, int32_t imm20) {
    next()->SetUFormat(RO_LUI, rd.code(), imm20);
  }
  void add(Register rd, Register rs1, Register rs2) {
    next()->SetRFormat(RO_ADD, rd.code(), rs1.code(), rs2.code());
  }
  void addi(Register rd, Register rs1, int16_t imm12) {
    next()->SetIFormat(RO_ADDI, rd.code(), rs1.code(), imm12);
  }
  void addiw(Register rd, Register rs1, int16_t imm12) {
    next()->SetIFormat(RO_ADDIW, rd.code(), rs1.code(), imm12);
  }
  void xori(Register rd, Register rs1, int16_t imm12) {
    next()->SetIFormat(RO_XORI, rd.code(), rs1.code(), imm12);
  }
  void slli(Register rd, Register rs1, uint8_t shamt) {
    next()->SetIShiftFormat(RO_SLLI, rd.code(), rs1.code(), shamt);
  }
  void slli_uw(Register rd, Register rs1, uint8_t shamt) {
    next()->SetIShift32Format(RO_SLLIUW, rd.code(), rs1.code(), shamt);
  }
  void srli(Register rd, Register rs1, uint8_t shamt) {
    next()->SetIShiftFormat(RO_SRLI, rd.code(), rs1.code(), shamt);
  }
  void bseti(Register rd, Register rs1, uint8_t shamt) {
    next()->SetIShiftFormat(RO_BSETI, rd.code(), rs1.code(), shamt);
  }
  void bclri(Register rd, Register rs1, uint8_t shamt) {
    next()->SetIShiftFormat(RO_BCLRI, rd.code(), rs1.code(), shamt);
  }
  void add_uw(Register rd, Register rs1, Register rs2) {
    next()->SetRFormat(RO_ADDUW, rd.code(), rs1.code(), rs2.code());
  }
  void zext_w(Register rd, Register rs1) { add_uw(rd, rs1, zero_reg); }
  void sh1add(Register rd, Register rs1, Register rs2) {
    next()->SetRFormat(RO_SH1ADD, rd.code(), rs1.code(), rs2.code());
  }
  void sh2add(Register rd, Register rs1, Register rs2) {
    next()->SetRFormat(RO_SH2ADD, rd.code(), rs1.code(), rs2.code());
  }
  void sh3add(Register rd, Register rs1, Register rs2) {
    next()->SetRFormat(RO_SH3ADD, rd.code(), rs1.code(), rs2.code());
  }
  void rori(Register rd, Register rs1, uint8_t shamt) {
    next()->SetIShiftFormat(RO_RORI, rd.code(), rs1.code(), shamt);
  }
};

// Ensure instruction registers are correctly assigned.
void InstSeq::assertRegisters(Register rd) const {
#ifdef DEBUG
  bool first = true;
  for (auto inst : *this) {
    // Source register for the first instruction is |zero_reg| and |rd| for
    // all later instructions.
    Register src = first ? zero_reg : rd;
    first = false;

    switch (inst.InstructionType()) {
      case Instruction::kRType:
        MOZ_ASSERT(inst.Rs2Value() == src.code() ||
                   inst.Rs2Value() == zero_reg.code());
        [[fallthrough]];
      case Instruction::kIType:
        MOZ_ASSERT(inst.Rs1Value() == src.code());
        [[fallthrough]];
      case Instruction::kUType:
        MOZ_ASSERT(inst.RdValue() == rd.code());
        break;
      default:
        MOZ_CRASH("unexpected instruction type");
    }
  }
#endif
}

/// Begin of LLVM imported code.

/// Create a bitmask with the N right-most bits set to 1, and all other
/// bits set to 0.  Only unsigned types are allowed.
template <typename T>
static constexpr T MaskTrailingOnes(unsigned N) {
  static_assert(std::is_unsigned_v<T>, "Invalid type!");
  const unsigned Bits = CHAR_BIT * sizeof(T);
  MOZ_ASSERT(N <= Bits && "Invalid bit index");
  if (N == 0) {
    return 0;
  }
  return T(-1) >> (Bits - N);
}

/// Create a bitmask with the N left-most bits set to 1, and all other
/// bits set to 0.  Only unsigned types are allowed.
template <typename T>
static constexpr T MaskLeadingOnes(unsigned N) {
  return ~MaskTrailingOnes<T>(CHAR_BIT * sizeof(T) - N);
}

/// Create a bitmask with the N right-most bits set to 0, and all other
/// bits set to 1.  Only unsigned types are allowed.
template <typename T>
static constexpr T MaskTrailingZeros(unsigned N) {
  return MaskLeadingOnes<T>(CHAR_BIT * sizeof(T) - N);
}

/// Sign-extend the number in the bottom B bits of X to a 64-bit integer.
/// Requires B <= 64.
template <unsigned B>
static constexpr int64_t SignExtend64(uint64_t x) {
  static_assert(B <= 64, "Bit width out of range.");
  if constexpr (B == 0) {
    return 0;
  }
  return int64_t(x << (64 - B)) >> (64 - B);
}

/// Return the high 32 bits of a 64 bit value.
static constexpr uint32_t Hi_32(uint64_t Value) {
  return static_cast<uint32_t>(Value >> 32);
}

/// Return the low 32 bits of a 64 bit value.
static constexpr uint32_t Lo_32(uint64_t Value) {
  return static_cast<uint32_t>(Value);
}

// Recursively generate a sequence for materializing an integer.
static void GenerateInstSeqImpl(Register rd, int64_t Val, InstSeq& result) {
  // Use BSETI for a single bit that can't be expressed by a single LUI or ADDI.
  if (RVFlags::HasZbsExtension() && std::has_single_bit(uint64_t(Val)) &&
      (!is_int32(Val) || Val == 0x800)) {
    result.bseti(rd, zero_reg, mozilla::FindMostSignificantBit(uint64_t(Val)));
    return;
  }

  if (is_int32(Val)) {
    // Depending on the active bits in the immediate Value v, the following
    // instruction sequences are emitted:
    //
    // v == 0                        : ADDI
    // v[0,12) != 0 && v[12,32) == 0 : ADDI
    // v[0,12) == 0 && v[12,32) != 0 : LUI
    // v[0,32) != 0                  : LUI+ADDI(W)
    auto [Hi20, Lo12] = ToHigh20Low12(int32_t(Val));

    if (Hi20) {
      result.lui(rd, Hi20);
    }

    if (Lo12 || Hi20 == 0) {
      if (Hi20) {
        result.addiw(rd, rd, Lo12);
      } else {
        result.addi(rd, zero_reg, Lo12);
      }
    }
    return;
  }

  // In the worst case, for a full 64-bit constant, a sequence of 8
  // instructions (i.e., LUI+ADDIW+SLLI+ADDI+SLLI+ADDI+SLLI+ADDI) has to be
  // emitted. Note that the first two instructions (LUI+ADDIW) can contribute
  // up to 32 bits while the following ADDI instructions contribute up to 12
  // bits each.
  //
  // On the first glance, implementing this seems to be possible by simply
  // emitting the most significant 32 bits (LUI+ADDIW) followed by as many
  // left shift (SLLI) and immediate additions (ADDI) as needed. However, due
  // to the fact that ADDI performs a sign extended addition, doing it like
  // that would only be possible when at most 11 bits of the ADDI instructions
  // are used. Using all 12 bits of the ADDI instructions, like done by GAS,
  // actually requires that the constant is processed starting with the least
  // significant bit.
  //
  // In the following, constants are processed from LSB to MSB but instruction
  // emission is performed from MSB to LSB by recursively calling
  // generateInstSeq. In each recursion, first the lowest 12 bits are removed
  // from the constant and the optimal shift amount, which can be greater than
  // 12 bits if the constant is sparse, is determined. Then, the shifted
  // remaining constant is processed recursively and gets emitted as soon as
  // it fits into 32 bits. The emission of the shifts and additions is
  // subsequently performed when the recursion returns.

  int64_t Lo12 = SignExtend64<12>(Val);
  int64_t Hi52 = uint64_t(Val) - uint64_t(Lo12);

  int ShiftAmount = 0;
  bool UnsignedShift = false;

  // Hi52 might now be valid for LUI without needing a shift.
  if (!is_int32(Hi52)) {
    ShiftAmount = std::countr_zero(uint64_t(Hi52));
    Hi52 >>= ShiftAmount;

    // If the remaining bits don't fit in 12 bits, we might be able to reduce
    // the shift amount in order to use LUI which will zero the lower 12
    // bits.
    if (ShiftAmount > 12 && !is_int12(Hi52)) {
      if (is_int32(uint64_t(Hi52) << 12)) {
        // Reduce the shift amount and add zeros to the LSBs so it will match
        // LUI.
        ShiftAmount -= 12;
        Hi52 = uint64_t(Hi52) << 12;
      } else if (is_uint32(uint64_t(Hi52) << 12) &&
                 RVFlags::HasZbaExtension()) {
        // Reduce the shift amount and add zeros to the LSBs so it will match
        // LUI, then shift left with SLLI.UW to clear the upper 32 set bits.
        ShiftAmount -= 12;
        Hi52 = SignExtend64<32>(uint64_t(Hi52) << 12);
        UnsignedShift = true;
      }
    }

    // Try to use SLLI_UW for Hi52 when it is uint32 but not int32.
    if (is_uint32(Hi52) && !is_int32(Hi52) && RVFlags::HasZbaExtension()) {
      // Use LUI+ADDI or LUI to compose, then clear the upper 32 bits with
      // SLLI_UW.
      Hi52 = SignExtend64<32>(uint64_t(Hi52));
      UnsignedShift = true;
    }
  }

  GenerateInstSeqImpl(rd, Hi52, result);

  // Skip shift if we were able to use LUI directly.
  if (ShiftAmount) {
    if (UnsignedShift) {
      result.slli_uw(rd, rd, ShiftAmount);
    } else {
      result.slli(rd, rd, ShiftAmount);
    }
  }

  if (Lo12) {
    result.addi(rd, rd, Lo12);
  }
}

static unsigned ExtractRotateInfo(int64_t Val) {
  // for case: 0b111..1..xxxxxx1..1..
  unsigned LeadingOnes = std::countl_one(uint64_t(Val));
  unsigned TrailingOnes = std::countr_one(uint64_t(Val));
  if (TrailingOnes > 0 && TrailingOnes < 64 &&
      (LeadingOnes + TrailingOnes) > (64 - 12)) {
    return 64 - TrailingOnes;
  }

  // for case: 0bxxx1..1..1...xxx
  unsigned UpperTrailingOnes = std::countr_one(Hi_32(Val));
  unsigned LowerLeadingOnes = std::countl_one(Lo_32(Val));
  if (UpperTrailingOnes < 32 &&
      (UpperTrailingOnes + LowerLeadingOnes) > (64 - 12)) {
    return 32 - UpperTrailingOnes;
  }

  return 0;
}

// Mozilla change: Return the amount by which |Val| rotated left is a value
// that LUI loads, a 32-bit value whose low 12 bits are zero, or zero if there
// is none. Boxed constant Values, such as true, and the complements of masks
// that wrap around bit 63 are such values.
static unsigned ExtractLuiRotateInfo(int64_t Val) {
  // Rotated left by the result, Val is 12 zero bits, 20 bits of payload and 32
  // copies of the payload's top bit. Going upwards circularly, Val therefore
  // contains either a run of at least 33 ones ending at bit 63 - Rotate or a
  // run of at least 44 zeros ending at bit 11 - Rotate. A run that long
  // contains bit 0 or bit 32, so its end is found by counting the trailing
  // ones from one of those bits. Try both for each sign of the payload.
  for (bool Negative : {true, false}) {
    uint64_t Run = Negative ? Val : ~Val;
    int RunTop = Negative ? 63 : 11;
    for (int RunEnd :
         {std::countr_one(Run) - 1, 31 + std::countr_one(Hi_32(Run))}) {
      unsigned Rotate = (RunTop - RunEnd) & 63;
      int64_t Rotated = std::rotl<uint64_t>(Val, Rotate);
      if (Rotate != 0 && is_int32(Rotated) && (Rotated & 0xfff) == 0) {
        return Rotate;
      }
    }
  }
  return 0;
}

static void GenerateInstSeqLeadingZeros(Register rd, int64_t Val,
                                        InstSeq& result) {
  MOZ_ASSERT(Val > 0, "Expected positive val");

  unsigned LeadingZeros = std::countl_zero(uint64_t(Val));
  uint64_t ShiftedVal = uint64_t(Val) << LeadingZeros;
  // Fill in the bits that will be shifted out with 1s. An example where this
  // helps is trailing one masks with 32 or more ones. This will generate
  // ADDI -1 and an SRLI.
  ShiftedVal |= MaskTrailingOnes<uint64_t>(LeadingZeros);

  InstSeq tmpSeq;
  GenerateInstSeqImpl(rd, ShiftedVal, tmpSeq);

  // Keep the new sequence if it is an improvement or the original is empty.
  if ((tmpSeq.size() + 1) < result.size() ||
      (result.empty() && tmpSeq.size() < 8)) {
    tmpSeq.srli(rd, rd, LeadingZeros);
    result = tmpSeq;
  }

  // Some cases can benefit from filling the lower bits with zeros instead.
  ShiftedVal &= MaskTrailingZeros<uint64_t>(LeadingZeros);
  tmpSeq.clear();
  GenerateInstSeqImpl(rd, ShiftedVal, tmpSeq);

  // Keep the new sequence if it is an improvement or the original is empty.
  if ((tmpSeq.size() + 1) < result.size() ||
      (result.empty() && tmpSeq.size() < 8)) {
    tmpSeq.srli(rd, rd, LeadingZeros);
    result = tmpSeq;
  }

  // If we have exactly 32 leading zeros and Zba, we can try using zext.w at
  // the end of the sequence.
  if (LeadingZeros == 32 && RVFlags::HasZbaExtension()) {
    // Bit 31 is set, so sign extend to fill the upper bits with 1s.
    uint64_t LeadingOnesVal = SignExtend64<32>(Val);
    tmpSeq.clear();
    GenerateInstSeqImpl(rd, LeadingOnesVal, tmpSeq);

    // Keep the new sequence if it is an improvement.
    if ((tmpSeq.size() + 1) < result.size() ||
        (result.empty() && tmpSeq.size() < 8)) {
      tmpSeq.zext_w(rd, rd);
      result = tmpSeq;
    }
  }
}

static void GenerateInstSeq(Register rd, int64_t Val, InstSeq& result) {
  MOZ_ASSERT(result.empty());

  GenerateInstSeqImpl(rd, Val, result);

  // If the low 12 bits are non-zero, the first expansion may end with an ADDI
  // or ADDIW. If there are trailing zeros, try generating a sign extended
  // constant with no trailing zeros and use a final SLLI to restore them.
  //
  // Mozilla change: Test for |list.size() > 2| because we don't support the
  // C extension, which means the `IsShiftedCompressible` test apply for us.
  if ((Val & 0xfff) != 0 && (Val & 1) == 0 && result.size() > 2) {
    unsigned TrailingZeros = std::countr_zero(uint64_t(Val));
    int64_t ShiftedVal = Val >> TrailingZeros;

    InstSeq tmpSeq;
    GenerateInstSeqImpl(rd, ShiftedVal, tmpSeq);

    // Keep the new sequence if it is an improvement.
    if ((tmpSeq.size() + 1) < result.size()) {
      tmpSeq.slli(rd, rd, TrailingZeros);
      result = tmpSeq;
    }
  }

  // If we have a 1 or 2 instruction sequence this is the best we can do.
  if (result.size() <= 2) {
    result.assertRegisters(rd);
    return;
  }

  // If the lower 13 bits are something like 0x17ff, try to add 1 to change the
  // lower 13 bits to 0x1800. We can restore this with an ADDI of -1 at the end
  // of the sequence. Call GenerateInstSeqImpl on the new constant which may
  // subtract 0xfffffffffffff800 to create another ADDI. This will leave a
  // constant with more than 12 trailing zeros for the next recursive step.
  if ((Val & 0xfff) != 0 && (Val & 0x1800) == 0x1000) {
    int64_t Imm12 = -(0x800 - (Val & 0xfff));
    int64_t AdjustedVal = Val - Imm12;
    InstSeq tmpSeq;
    GenerateInstSeqImpl(rd, AdjustedVal, tmpSeq);

    // Keep the new sequence if it is an improvement.
    if ((tmpSeq.size() + 1) < result.size()) {
      tmpSeq.addi(rd, rd, Imm12);
      result = tmpSeq;
    }
  }

  // If the constant is positive we might be able to generate a shifted constant
  // with no leading zeros and use a final SRLI to restore them.
  if (Val > 0 && result.size() > 2) {
    GenerateInstSeqLeadingZeros(rd, Val, result);
  }

  // If the constant is negative, trying inverting and using our trailing zero
  // optimizations. Use an xori to invert the final value.
  if (Val < 0 && result.size() > 3) {
    uint64_t InvertedVal = ~uint64_t(Val);
    InstSeq tmpSeq;
    GenerateInstSeqLeadingZeros(rd, InvertedVal, tmpSeq);

    // Keep it if we found a sequence that is smaller after inverting.
    if (!tmpSeq.empty() && (tmpSeq.size() + 1) < result.size()) {
      tmpSeq.xori(rd, rd, -1);
      result = tmpSeq;
    }
  }

  // Perform optimization with BSETI in the Zbs extension.
  if (result.size() > 2 && RVFlags::HasZbsExtension()) {
    // Create a simm32 value for LUI+ADDI(W) by forcing the upper 33 bits to
    // zero. Xor that with original value to get which bits should be set by
    // BSETI.
    uint64_t Lo = Val & 0x7fffffff;
    uint64_t Hi = Val ^ Lo;
    MOZ_ASSERT(Hi != 0);
    InstSeq tmpSeq;

    if (Lo != 0) {
      GenerateInstSeqImpl(rd, Lo, tmpSeq);
    }

    if (tmpSeq.size() + std::popcount(Hi) < result.size()) {
      do {
        Register src = tmpSeq.empty() ? zero_reg : rd;
        tmpSeq.bseti(rd, src, std::countr_zero(Hi));
        Hi &= (Hi - 1);  // Clear lowest set bit.
      } while (Hi != 0);
      result = tmpSeq;
    }

    // Fold LI 1 + SLLI into BSETI.
    if (result[0].IsAddi() && result[0].Imm12Value() == 1 &&
        result[1].IsSlli()) {
      result.erase_front();    // Remove ADDI.
      auto& slli = result[0];  // Patch SLLI.
      slli.SetIShiftFormat(RO_BSETI, slli.RdValue(), zero_reg.code(),
                           slli.Shamt());
    }
  }

  // Perform optimization with BCLRI in the Zbs extension.
  if (result.size() > 2 && RVFlags::HasZbsExtension()) {
    // Create a simm32 value for LUI+ADDI(W) by forcing the upper 33 bits to
    // one. Xor that with original value to get which bits should be cleared by
    // BCLRI.
    uint64_t Lo = Val | 0xffffffff80000000;
    uint64_t Hi = Val ^ Lo;
    MOZ_ASSERT(Hi != 0);

    InstSeq tmpSeq;
    GenerateInstSeqImpl(rd, Lo, tmpSeq);

    if (tmpSeq.size() + std::popcount(Hi) < result.size()) {
      do {
        tmpSeq.bclri(rd, rd, std::countr_zero(Hi));
        Hi &= (Hi - 1);  // Clear lowest set bit.
      } while (Hi != 0);
      result = tmpSeq;
    }
  }

  // Perform optimization with SH*ADD in the Zba extension.
  if (result.size() > 2 && RVFlags::HasZbaExtension()) {
    int64_t Div = 0;
    InstSeq tmpSeq;
    // Select the opcode and divisor.
    if ((Val % 3) == 0 && is_int32(Val / 3)) {
      Div = 3;
    } else if ((Val % 5) == 0 && is_int32(Val / 5)) {
      Div = 5;
    } else if ((Val % 9) == 0 && is_int32(Val / 9)) {
      Div = 9;
    }
    // Build the new instruction sequence.
    if (Div > 0) {
      GenerateInstSeqImpl(rd, Val / Div, tmpSeq);
      if ((tmpSeq.size() + 1) < result.size()) {
        if (Div == 3) {
          tmpSeq.sh1add(rd, rd, rd);
        } else if (Div == 5) {
          tmpSeq.sh2add(rd, rd, rd);
        } else {
          tmpSeq.sh3add(rd, rd, rd);
        }
        result = tmpSeq;
      }
    } else {
      // Try to use LUI+SH*ADD+ADDI.
      int64_t Hi52 = (uint64_t(Val) + 0x800ull) & ~0xfffull;
      int64_t Lo12 = SignExtend64<12>(Val);
      Div = 0;
      if (is_int32(Hi52 / 3) && (Hi52 % 3) == 0) {
        Div = 3;
      } else if (is_int32(Hi52 / 5) && (Hi52 % 5) == 0) {
        Div = 5;
      } else if (is_int32(Hi52 / 9) && (Hi52 % 9) == 0) {
        Div = 9;
      }
      // Build the new instruction sequence.
      if (Div > 0) {
        // For Val that has zero Lo12 (implies Val equals to Hi52) should has
        // already been processed to LUI+SH*ADD by previous optimization.
        MOZ_ASSERT(
            Lo12 != 0,
            "unexpected instruction sequence for immediate materialisation");
        MOZ_ASSERT(tmpSeq.empty(), "Expected empty TmpSeq");
        GenerateInstSeqImpl(rd, Hi52 / Div, tmpSeq);
        if ((tmpSeq.size() + 2) < result.size()) {
          if (Div == 3) {
            tmpSeq.sh1add(rd, rd, rd);
          } else if (Div == 5) {
            tmpSeq.sh2add(rd, rd, rd);
          } else {
            tmpSeq.sh3add(rd, rd, rd);
          }
          tmpSeq.addi(rd, rd, Lo12);
          result = tmpSeq;
        }
      }
    }
  }

  // Perform optimization with rori in the Zbb extension.
  if (result.size() > 2 && RVFlags::HasZbbExtension()) {
    if (unsigned Rotate = ExtractRotateInfo(Val)) {
      InstSeq tmpSeq;
      uint64_t NegImm12 = std::rotl<uint64_t>(Val, Rotate);
      MOZ_ASSERT(is_int12(NegImm12));
      tmpSeq.addi(rd, zero_reg, NegImm12);
      tmpSeq.rori(rd, rd, Rotate);
      result = tmpSeq;
    } else if (unsigned Rotate = ExtractLuiRotateInfo(Val)) {
      // Mozilla change: Also rotate a value LUI loads, which LLVM doesn't try.
      InstSeq tmpSeq;
      int64_t Hi32 = std::rotl<uint64_t>(Val, Rotate);
      MOZ_ASSERT(is_int32(Hi32) && (Hi32 & 0xfff) == 0);
      tmpSeq.lui(rd, ToHigh20Low12(int32_t(Hi32)).high20);
      tmpSeq.rori(rd, rd, Rotate);
      result = tmpSeq;
    }
  }

  result.assertRegisters(rd);
}

// Helper to generate an instruction sequence that can materialize the given
// immediate value into a register using an additional temporary register. This
// handles cases where the constant can be generated by (ADD (SLLI X, C), X) or
// (ADD_UW (SLLI X, C) X).
static void GenerateTwoRegInstSeq(Register rd, Register rt, int64_t Val,
                                  InstSeq& result) {
  MOZ_ASSERT(result.empty());

  int64_t LoVal = SignExtend64<32>(Val);
  if (LoVal == 0) {
    return;
  }

  // Subtract the LoVal to emulate the effect of the final ADD.
  uint64_t Tmp = uint64_t(Val) - uint64_t(LoVal);
  MOZ_ASSERT(Tmp != 0);

  // Use trailing zero counts to figure how far we need to shift LoVal to line
  // up with the remaining constant.
  // TODO: This algorithm assumes all non-zero bits in the low 32 bits of the
  // final constant come from LoVal.
  unsigned TzLo = std::countr_zero(uint64_t(LoVal));
  unsigned TzHi = std::countr_zero(Tmp);
  MOZ_ASSERT(TzLo < 32 && TzHi >= 32);
  unsigned ShiftAmt = TzHi - TzLo;

  if (Tmp == (uint64_t(LoVal) << ShiftAmt)) {
    GenerateInstSeq(rd, LoVal, result);
    MOZ_ASSERT(result.size() <= 2);

    result.slli(rt, rd, ShiftAmt);
    result.add(rd, rd, rt);
    return;
  }

  // If we have Zba, we can use (ADD_UW X, (SLLI X, 32)).
  if (RVFlags::HasZbaExtension() && Lo_32(Val) == Hi_32(Val)) {
    GenerateInstSeq(rd, LoVal, result);
    MOZ_ASSERT(result.size() <= 2);

    result.slli(rt, rd, 32);
    result.add_uw(rd, rd, rt);
    return;
  }

  // No optimization possible.
}

/// End of LLVM imported code.

// If |imm| is more than 32 bits and a temp register is available, |imm| is
// divided into two 32-bit parts, |low_32| and |high_32|. Each part is built in
// a separate register. |low_32| is built before |high_32|. If |low_32| is
// negative, |high_32| is incremented by one. This compensates for 32 bits of
// 1's in the lower half when the two registers are added.
static void GenerateTwoHalvesInstSeq(Register rd, Register temp, int64_t imm,
                                     InstSeq& result) {
  MOZ_ASSERT(!is_int32(imm));
  MOZ_ASSERT(result.empty());

  int64_t high_32 = imm >> 32;
  int64_t low_32 = SignExtend64<32>(imm);

  // Account for negative int32.
  if (low_32 < 0) {
    high_32 = high_32 + 1;
  }

  // Build lower 32 bits.
  InstSeq lowSeq;
  GenerateInstSeq(rd, low_32, lowSeq);
  MOZ_ASSERT(lowSeq.size() <= 2);

  // Build upper 32 bits.
  InstSeq highSeq;
  GenerateInstSeq(temp, high_32, highSeq);
  MOZ_ASSERT(highSeq.size() <= 2);

  // Combine both halves.
  result.append(lowSeq);
  result.append(highSeq);
  result.slli(temp, temp, 32);
  result.add(rd, rd, temp);
}

void Assembler::RV_li(Register rd, int64_t imm) {
  UseScratchRegisterScope temps(this);

  InstSeq seq;
  GenerateInstSeq(rd, imm, seq);

  // If the instruction sequence is longer than three instructions, check for
  // possible optimizations using a temporary register.
  if (seq.size() > 3 && temps.hasAvailable()) {
    Register temp = temps.Acquire();
    InstSeq tmpSeq;

    // GenerateTwoRegInstSeq generates 3-4 instructions.
    GenerateTwoRegInstSeq(rd, temp, imm, tmpSeq);
    MOZ_ASSERT_IF(!tmpSeq.empty(), 3 <= tmpSeq.size() && tmpSeq.size() <= 4);

    // GenerateTwoHalvesInstSeq generates 4-6 instructions.
    if (tmpSeq.empty() && seq.size() > 4) {
      GenerateTwoHalvesInstSeq(rd, temp, imm, tmpSeq);
      MOZ_ASSERT(4 <= tmpSeq.size() && tmpSeq.size() <= 6);
    }

    // Use instruction sequence with temporary register if it's shorter.
    if (!tmpSeq.empty() && tmpSeq.size() < seq.size()) {
      seq = tmpSeq;
    }
  }

  // Forbid pools to prevent splitting the instruction sequence, so that
  // instructions which may be fused, like lui+add(w), are kept next to each
  // other. Set the maximum number of instructions to 8 instead of |seq.size()|
  // to pass a compile-time constant to |AutoForbidPoolsAndNops|.
  AutoForbidPoolsAndNops afp(this, 8);

  // Emit all instructions.
  for (auto instr : seq) {
    emit(instr);
  }
}
