// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef jit_riscv64_base_Instruction_h
#define jit_riscv64_base_Instruction_h

#include "mozilla/Assertions.h"

#include <stdint.h>

#include "jit/riscv64/base/Integer.h"
#include "jit/riscv64/constant/Constant-riscv64.h"

namespace js::jit {

// On RISCV all instructions are 32 bits, except for RVC.
using Instr = int32_t;
using ShortInstr = int16_t;

// -----------------------------------------------------------------------------
// Specific instructions, constants, and masks.
// These constants are declared in assembler-riscv64.cc, as they use named
// registers and other constants.

// An Illegal instruction
const Instr kIllegalInstr = 0;  // All other bits are 0s (i.e., ecall)
// An ECALL instruction, used for redirected real time call
const Instr rtCallRedirInstr = SYSTEM;  // All other bits are 0s (i.e., ecall)
// An EBreak instruction, used for debugging and semi-hosting
const Instr kBreakInstr = SYSTEM | 1 << kImm12Shift;  // ebreak
// The Zihintpause hint, which is encoded as "fence w, 0"
const Instr kPauseInstr = MISC_MEM | PSW << kPredOrderShift;

constexpr uint8_t kInstrSize = 4;
constexpr uint8_t kShortInstrSize = 2;

class InstructionBase {
  // Sign-extend a |len|-bits integer.
  static constexpr int32_t sext(int32_t x, uint32_t len) {
    MOZ_ASSERT(0 < len && len <= 32);
    return ((x << (32 - len)) >> (32 - len));
  }

  // Zero-extend a |len|-bits integer.
  static constexpr uint32_t zext(uint32_t x, uint32_t len) {
    MOZ_ASSERT(0 < len && len <= 32);
    return ((x << (32 - len)) >> (32 - len));
  }

 public:
  enum {
    // On RISC-V, PC cannot actually be directly accessed. We behave as if PC
    // was always the value of the current instruction being executed.
    kPCReadOffset = 0
  };

  // Instruction type.
  enum Type {
    kRType,
    kR4Type,  // Special R4 for Q extension
    kIType,
    kSType,
    kBType,
    kUType,
    kJType,
    // C extension
    kCRType,
    kCIType,
    kCSSType,
    kCIWType,
    kCLType,
    kCSType,
    kCAType,
    kCBType,
    kCJType,
    // V extension
    kVType,
    kVLType,
    kVSType,
    kVAMOType,
    kVIVVType,
    kVFVVType,
    kVMVVType,
    kVIVIType,
    kVIVXType,
    kVFVFType,
    kVMVXType,
    kVSETType,
    kUnsupported = -1
  };

  inline bool IsIllegalInstruction() const {
    uint16_t FirstHalfWord = *reinterpret_cast<const uint16_t*>(this);
    return FirstHalfWord == 0;
  }

  inline bool IsShortInstruction() const {
    uint8_t FirstByte = *reinterpret_cast<const uint8_t*>(this);
    return (FirstByte & 0x03) <= C2;
  }

  inline uint8_t InstructionSize() const {
    return IsShortInstruction() ? kShortInstrSize : kInstrSize;
  }

  // Get the raw instruction bits.
  inline Instr InstructionBits() const {
    if (IsShortInstruction()) {
      return 0x0000FFFF & (*reinterpret_cast<const ShortInstr*>(this));
    }
    return *reinterpret_cast<const Instr*>(this);
  }

  // Set the raw instruction bits to value.
  inline void SetInstructionBits(Instr value) {
    *reinterpret_cast<Instr*>(this) = value;
  }

  // Read one particular bit out of the instruction bits.
  inline int Bit(int nr) const { return (InstructionBits() >> nr) & 1; }

  // Read a bit field out of the instruction bits.
  inline int Bits(int hi, int lo) const {
    return (InstructionBits() >> lo) & ((2U << (hi - lo)) - 1);
  }

  // Accessors for the different named fields used in the RISC-V encoding.
  inline enum BaseOpcode BaseOpcodeValue() const {
    return static_cast<enum BaseOpcode>(
        Bits(kBaseOpcodeShift + kBaseOpcodeBits - 1, kBaseOpcodeShift));
  }

  // Return the fields at their original place in the instruction encoding.
  inline enum BaseOpcode BaseOpcodeFieldRaw() const {
    return static_cast<enum BaseOpcode>(InstructionBits() & kBaseOpcodeMask);
  }

  // Safe to call within R-type instructions
  inline int Funct7FieldRaw() const { return InstructionBits() & kFunct7Mask; }

  // Safe to call within R-type instructions
  inline int Funct6FieldRaw() const { return InstructionBits() & kFunct6Mask; }

  // Safe to call within R-, I-, S-, or B-type instructions
  inline int Funct3FieldRaw() const { return InstructionBits() & kFunct3Mask; }

  // Safe to call within R-, I-, S-, or B-type instructions
  inline int Rs1FieldRawNoAssert() const {
    return InstructionBits() & kRs1FieldMask;
  }

  // Safe to call within R-, S-, or B-type instructions
  inline int Rs2FieldRawNoAssert() const {
    return InstructionBits() & kRs2FieldMask;
  }

  // Safe to call within R4-type instructions
  inline int Rs3FieldRawNoAssert() const {
    return InstructionBits() & kRs3FieldMask;
  }

  inline int32_t ITypeBits() const { return InstructionBits() & kITypeMask; }

  inline int32_t InstructionOpcodeType() const {
    if (IsShortInstruction()) {
      return InstructionBits() & kRvcOpcodeMask;
    }
    return InstructionBits() & kBaseOpcodeMask;
  }

  // Get the encoding type of the instruction.
  Type InstructionType() const;

  // Get the encoding type of a 32-bit instruction from its major opcode.
  static constexpr Type BaseOpcodeType(Instr bits) {
    switch (bits & kBaseOpcodeMask) {
      case LOAD:
      case LOAD_FP:
      case MISC_MEM:
      case OP_IMM:
      case OP_IMM_32:
      case JALR:
      case SYSTEM:
        return kIType;
      case AUIPC:
      case LUI:
        return kUType;
      case STORE:
      case STORE_FP:
        return kSType;
      case AMO:
      case OP:
      case OP_32:
      case OP_FP:
        return kRType;
      case MADD:
      case MSUB:
      case NMSUB:
      case NMADD:
        return kR4Type;
      case BRANCH:
        return kBType;
      case JAL:
        return kJType;
      case OP_V:
        return kVType;
      default:
        return kUnsupported;
    }
  }
  OffsetSize GetOffsetSize() const;
  inline ImmBranchRangeType GetImmBranchRangeType() const {
    return OffsetSizeToImmBranchRangeType(GetOffsetSize());
  }

  /// Getters

  // Say if the instruction is a break or a trap.
  inline bool IsTrap() const { return InstructionBits() == kBreakInstr; }

  // Check if the instruction is a branch of some kind.
  inline bool IsBranch() const { return BaseOpcode() == BRANCH; }

  inline bool IsJal() const { return BaseOpcode() == JAL; }

  inline bool IsJalr() const { return BaseOpcode() == JALR; }

  inline bool IsLui() const { return BaseOpcode() == LUI; }

  inline bool IsAuipc() const { return BaseOpcode() == AUIPC; }

  inline bool IsAddi() const {
    return (InstructionBits() & kITypeMask) == RO_ADDI;
  }

  inline bool IsOri() const {
    return (InstructionBits() & kITypeMask) == RO_ORI;
  }

  inline bool IsSlli() const {
    return (InstructionBits() & (kITypeMask | kFunct6Mask)) == RO_SLLI;
  }

  inline bool IsLw() const { return (InstructionBits() & kITypeMask) == RO_LW; }

  inline bool IsLd() const { return (InstructionBits() & kITypeMask) == RO_LD; }

  inline bool IsAddiw() const {
    return (InstructionBits() & kITypeMask) == RO_ADDIW;
  }

  inline bool IsNop() const { return InstructionBits() == kNopByte; }

  inline int BaseOpcode() const { return InstructionBits() & kBaseOpcodeMask; }

  inline int RvcOpcode() const {
    MOZ_ASSERT(IsShortInstruction());
    return InstructionBits() & kRvcOpcodeMask;
  }

  inline int Rs1Value() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kRType ||
               InstructionType() == InstructionBase::kR4Type ||
               InstructionType() == InstructionBase::kIType ||
               InstructionType() == InstructionBase::kSType ||
               InstructionType() == InstructionBase::kBType ||
               InstructionType() == InstructionBase::kVType);
    return Bits(kRs1Shift + kRs1Bits - 1, kRs1Shift);
  }

  inline int Rs2Value() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kRType ||
               InstructionType() == InstructionBase::kR4Type ||
               InstructionType() == InstructionBase::kSType ||
               InstructionType() == InstructionBase::kBType ||
               InstructionType() == InstructionBase::kIType ||
               InstructionType() == InstructionBase::kVType);
    return Bits(kRs2Shift + kRs2Bits - 1, kRs2Shift);
  }

  inline int Rs3Value() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kR4Type);
    return Bits(kRs3Shift + kRs3Bits - 1, kRs3Shift);
  }

  inline int Vs1Value() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kVType ||
               InstructionType() == InstructionBase::kIType ||
               InstructionType() == InstructionBase::kSType);
    return Bits(kVs1Shift + kVs1Bits - 1, kVs1Shift);
  }

  inline int Vs2Value() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kVType ||
               InstructionType() == InstructionBase::kIType ||
               InstructionType() == InstructionBase::kSType);
    return Bits(kVs2Shift + kVs2Bits - 1, kVs2Shift);
  }

  inline int VdValue() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kVType ||
               InstructionType() == InstructionBase::kIType ||
               InstructionType() == InstructionBase::kSType);
    return Bits(kVdShift + kVdBits - 1, kVdShift);
  }

  inline int RdValue() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kRType ||
               InstructionType() == InstructionBase::kR4Type ||
               InstructionType() == InstructionBase::kIType ||
               InstructionType() == InstructionBase::kSType ||
               InstructionType() == InstructionBase::kUType ||
               InstructionType() == InstructionBase::kJType ||
               InstructionType() == InstructionBase::kVType);
    return Bits(kRdShift + kRdBits - 1, kRdShift);
  }

  inline int RvcRs1Value() const { return RvcRdValue(); }

  inline int RvcRdValue() const {
    MOZ_ASSERT(IsShortInstruction());
    return Bits(kRvcRdShift + kRvcRdBits - 1, kRvcRdShift);
  }

  inline int RvcRs2Value() const {
    MOZ_ASSERT(IsShortInstruction());
    return Bits(kRvcRs2Shift + kRvcRs2Bits - 1, kRvcRs2Shift);
  }

  inline int RvcRs1sValue() const {
    MOZ_ASSERT(IsShortInstruction());
    return 0b1000 + Bits(kRvcRs1sShift + kRvcRs1sBits - 1, kRvcRs1sShift);
  }

  inline int RvcRs2sValue() const {
    MOZ_ASSERT(IsShortInstruction());
    return 0b1000 + Bits(kRvcRs2sShift + kRvcRs2sBits - 1, kRvcRs2sShift);
  }

  inline int Funct7Value() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kRType);
    return Bits(kFunct7Shift + kFunct7Bits - 1, kFunct7Shift);
  }

  inline int Funct2Value() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kR4Type);
    return Bits(kFunct2Shift + kFunct2Bits - 1, kFunct2Shift);
  }

  inline int Funct3Value() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kRType ||
               InstructionType() == InstructionBase::kR4Type ||
               InstructionType() == InstructionBase::kIType ||
               InstructionType() == InstructionBase::kSType ||
               InstructionType() == InstructionBase::kBType);
    return Bits(kFunct3Shift + kFunct3Bits - 1, kFunct3Shift);
  }

  inline int Funct5Value() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kRType &&
               BaseOpcode() == OP_FP);
    return Bits(kFunct5Shift + kFunct5Bits - 1, kFunct5Shift);
  }

  inline int RvcFunct6Value() const {
    MOZ_ASSERT(IsShortInstruction());
    return Bits(kRvcFunct6Shift + kRvcFunct6Bits - 1, kRvcFunct6Shift);
  }

  inline int RvcFunct4Value() const {
    MOZ_ASSERT(IsShortInstruction());
    return Bits(kRvcFunct4Shift + kRvcFunct4Bits - 1, kRvcFunct4Shift);
  }

  inline int RvcFunct3Value() const {
    MOZ_ASSERT(IsShortInstruction());
    return Bits(kRvcFunct3Shift + kRvcFunct3Bits - 1, kRvcFunct3Shift);
  }

  inline int RvcFunct2Value() const {
    MOZ_ASSERT(IsShortInstruction());
    return Bits(kRvcFunct2Shift + kRvcFunct2Bits - 1, kRvcFunct2Shift);
  }

  inline int RvcFunct2BValue() const {
    MOZ_ASSERT(IsShortInstruction());
    return Bits(kRvcFunct2BShift + kRvcFunct2Bits - 1, kRvcFunct2BShift);
  }

  inline int CsrValue() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kIType &&
               BaseOpcode() == SYSTEM);
    return Bits(kCsrShift + kCsrBits - 1, kCsrShift);
  }

  inline int RoundMode() const {
    MOZ_ASSERT((InstructionType() == InstructionBase::kRType ||
                InstructionType() == InstructionBase::kR4Type) &&
               BaseOpcode() == OP_FP);
    return Bits(kFunct3Shift + kFunct3Bits - 1, kFunct3Shift);
  }

  inline int MemoryOrder(bool is_pred) const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kIType &&
               BaseOpcode() == MISC_MEM);
    if (is_pred) {
      return Bits(kPredOrderShift + kMemOrderBits - 1, kPredOrderShift);
    }
    return Bits(kSuccOrderShift + kMemOrderBits - 1, kSuccOrderShift);
  }

  inline int Imm12Value() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kIType);
    int Value = Bits(kImm12Shift + kImm12Bits - 1, kImm12Shift);
    return sext(Value, kImm12Bits);
  }

  inline int BranchOffset() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kBType);
    // | imm[12|10:5] | rs2 | rs1 | funct3 | imm[4:1|11] | opcode |
    //  31          25                      11          7
    uint32_t Bits = InstructionBits();
    int16_t imm13 = ((Bits & 0xf00) >> 7) | ((Bits & 0x7e000000) >> 20) |
                    ((Bits & 0x80) << 4) | ((Bits & 0x80000000) >> 19);
    return sext(imm13, 13);
  }

  inline int StoreOffset() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kSType);
    // | imm[11:5] | rs2 | rs1 | funct3 | imm[4:0] | opcode |
    //  31       25                      11       7
    uint32_t Bits = InstructionBits();
    int16_t imm12 = ((Bits & 0xf80) >> 7) | ((Bits & 0xfe000000) >> 20);
    return sext(imm12, 12);
  }

  inline int Imm20UValue() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kUType);
    // | imm[31:12] | rd | opcode |
    //  31        12
    int32_t Bits = InstructionBits();
    return Bits >> 12;
  }

  inline int Imm20JValue() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kJType);
    // | imm[20|10:1|11|19:12] | rd | opcode |
    //  31                   12
    uint32_t Bits = InstructionBits();
    int32_t imm20 = ((Bits & 0x7fe00000) >> 20) | ((Bits & 0x100000) >> 9) |
                    (Bits & 0xff000) | ((Bits & 0x80000000) >> 11);
    return sext(imm20, 20 + 1);  // +1 b/c J immediates are shifted by 1 bit.
  }

  inline bool IsArithShift() const {
    // Valid only for right shift operations
    MOZ_ASSERT((BaseOpcode() == OP || BaseOpcode() == OP_32 ||
                BaseOpcode() == OP_IMM || BaseOpcode() == OP_IMM_32) &&
               Funct3Value() == 0b101);
    return InstructionBits() & 0x40000000;
  }

  inline int Shamt() const {
    // Valid only for shift instructions (SLLI, SRLI, SRAI)
    MOZ_ASSERT((InstructionBits() & kBaseOpcodeMask) == OP_IMM &&
               (Funct3Value() == 0b001 || Funct3Value() == 0b101));
    // | 0A0000 | shamt | rs1 | funct3 | rd | opcode |
    //  31       25    20
    return Bits(kImm12Shift + 5, kImm12Shift);
  }

  inline int Shamt32() const {
    // Valid only for shift instructions (SLLIW, SRLIW, SRAIW)
    MOZ_ASSERT((InstructionBits() & kBaseOpcodeMask) == OP_IMM_32 &&
               (Funct3Value() == 0b001 || Funct3Value() == 0b101));
    // | 0A00000 | shamt | rs1 | funct3 | rd | opcode |
    //  31        24   20
    return Bits(kImm12Shift + 4, kImm12Shift);
  }

  inline int RvcImm6Value() const {
    MOZ_ASSERT(IsShortInstruction());
    // | funct3 | imm[5] | rs1/rd | imm[4:0] | opcode |
    //  15         12              6        2
    uint32_t Bits = InstructionBits();
    int32_t imm6 = ((Bits & 0x1000) >> 7) | ((Bits & 0x7c) >> 2);
    return sext(imm6, 6);
  }

  inline int RvcImm6Addi16spValue() const {
    MOZ_ASSERT(IsShortInstruction());
    // | funct3 | nzimm[9] | 2 | nzimm[4|6|8:7|5] | opcode |
    //  15         12           6                2
    uint32_t Bits = InstructionBits();
    int32_t imm10 = ((Bits & 0x1000) >> 3) | ((Bits & 0x40) >> 2) |
                    ((Bits & 0x20) << 1) | ((Bits & 0x18) << 4) |
                    ((Bits & 0x4) << 3);
    MOZ_ASSERT(imm10 != 0);
    return sext(imm10, 10);
  }

  inline int RvcImm8Addi4spnValue() const {
    MOZ_ASSERT(IsShortInstruction());
    // | funct3 | nzimm[11]  | rd' | opcode |
    //  15      13           5     2
    uint32_t Bits = InstructionBits();
    int32_t uimm10 = ((Bits & 0x20) >> 2) | ((Bits & 0x40) >> 4) |
                     ((Bits & 0x780) >> 1) | ((Bits & 0x1800) >> 7);
    MOZ_ASSERT(uimm10 != 0);
    return uimm10;
  }

  inline int RvcShamt6() const {
    MOZ_ASSERT(IsShortInstruction());
    // | funct3 | nzuimm[5] | rs1/rd | nzuimm[4:0] | opcode |
    //  15         12                 6           2
    int32_t imm6 = RvcImm6Value();
    return imm6 & 0x3f;
  }

  inline int RvcImm6LwspValue() const {
    MOZ_ASSERT(IsShortInstruction());
    // | funct3 | uimm[5] | rs1 | uimm[4:2|7:6] | opcode |
    //  15         12            6             2
    uint32_t Bits = InstructionBits();
    int32_t imm8 =
        ((Bits & 0x1000) >> 7) | ((Bits & 0x70) >> 2) | ((Bits & 0xc) << 4);
    return imm8;
  }

  inline int RvcImm6LdspValue() const {
    MOZ_ASSERT(IsShortInstruction());
    // | funct3 | uimm[5] | rs1 | uimm[4:3|8:6] | opcode |
    //  15         12            6             2
    uint32_t Bits = InstructionBits();
    int32_t imm9 =
        ((Bits & 0x1000) >> 7) | ((Bits & 0x60) >> 2) | ((Bits & 0x1c) << 4);
    return imm9;
  }

  inline int RvcImm6SwspValue() const {
    MOZ_ASSERT(IsShortInstruction());
    // | funct3 | uimm[5:2|7:6] | rs2 | opcode |
    //  15       12            7
    uint32_t Bits = InstructionBits();
    int32_t imm8 = ((Bits & 0x1e00) >> 7) | ((Bits & 0x180) >> 1);
    return imm8;
  }

  inline int RvcImm6SdspValue() const {
    MOZ_ASSERT(IsShortInstruction());
    // | funct3 | uimm[5:3|8:6] | rs2 | opcode |
    //  15       12            7
    uint32_t Bits = InstructionBits();
    int32_t imm9 = ((Bits & 0x1c00) >> 7) | ((Bits & 0x380) >> 1);
    return imm9;
  }

  inline int RvcImm5WValue() const {
    MOZ_ASSERT(IsShortInstruction());
    // | funct3 | imm[5:3] | rs1 | imm[2|6] | rd | opcode |
    //  15       12       10     6          4     2
    uint32_t Bits = InstructionBits();
    int32_t imm7 =
        ((Bits & 0x1c00) >> 7) | ((Bits & 0x40) >> 4) | ((Bits & 0x20) << 1);
    return imm7;
  }

  inline int RvcImm5DValue() const {
    MOZ_ASSERT(IsShortInstruction());
    // | funct3 | imm[5:3] | rs1 | imm[7:6] | rd | opcode |
    //  15       12        10    6          4     2
    uint32_t Bits = InstructionBits();
    int32_t imm8 = ((Bits & 0x1c00) >> 7) | ((Bits & 0x60) << 1);
    return imm8;
  }

  inline int RvcImm11CJValue() const {
    MOZ_ASSERT(IsShortInstruction());
    // | funct3 | [11|4|9:8|10|6|7|3:1|5] | opcode |
    //  15      12                        2
    uint32_t Bits = InstructionBits();
    int32_t imm12 = ((Bits & 0x4) << 3) | ((Bits & 0x38) >> 2) |
                    ((Bits & 0x40) << 1) | ((Bits & 0x80) >> 1) |
                    ((Bits & 0x100) << 2) | ((Bits & 0x600) >> 1) |
                    ((Bits & 0x800) >> 7) | ((Bits & 0x1000) >> 1);
    return sext(imm12, 12);
  }

  inline int RvcImm8BValue() const {
    MOZ_ASSERT(IsShortInstruction());
    // | funct3 | imm[8|4:3] | rs1` | imm[7:6|2:1|5]  | opcode |
    //  15       12        10       7                 2
    uint32_t Bits = InstructionBits();
    int32_t imm9 = ((Bits & 0x4) << 3) | ((Bits & 0x18) >> 2) |
                   ((Bits & 0x60) << 1) | ((Bits & 0xc00) >> 7) |
                   ((Bits & 0x1000) >> 4);
    return sext(imm9, 9);
  }

  inline int vl_vs_width() {
    int width = 0;
    if ((InstructionBits() & kBaseOpcodeMask) != LOAD_FP &&
        (InstructionBits() & kBaseOpcodeMask) != STORE_FP) {
      return -1;
    }
    switch (InstructionBits() & (kRvvWidthMask | kRvvMewMask)) {
      case 0x0:
        width = 8;
        break;
      case 0x00005000:
        width = 16;
        break;
      case 0x00006000:
        width = 32;
        break;
      case 0x00007000:
        width = 64;
        break;
      case 0x10000000:
        width = 128;
        break;
      case 0x10005000:
        width = 256;
        break;
      case 0x10006000:
        width = 512;
        break;
      case 0x10007000:
        width = 1024;
        break;
      default:
        width = -1;
        break;
    }
    return width;
  }

  inline uint32_t Rvvzimm() const {
    if ((InstructionBits() & (kBaseOpcodeMask | kFunct3Mask | 0x80000000)) ==
        RO_V_VSETVLI) {
      uint32_t Bits = InstructionBits();
      uint32_t zimm = Bits & kRvvZimmMask;
      return zimm >> kRvvZimmShift;
    } else {
      MOZ_ASSERT((InstructionBits() & (kBaseOpcodeMask | kFunct3Mask |
                                       0xC0000000)) == RO_V_VSETIVLI);
      uint32_t Bits = InstructionBits();
      uint32_t zimm = Bits & kRvvZimmMask;
      return (zimm >> kRvvZimmShift) & 0x3FF;
    }
  }

  inline uint32_t Rvvuimm() const {
    MOZ_ASSERT((InstructionBits() &
                (kBaseOpcodeMask | kFunct3Mask | 0xC0000000)) == RO_V_VSETIVLI);
    uint32_t Bits = InstructionBits();
    uint32_t uimm = Bits & kRvvUimmMask;
    return uimm >> kRvvUimmShift;
  }

  inline uint32_t RvvVsew() const {
    uint32_t zimm = Rvvzimm();
    uint32_t vsew = (zimm >> 3) & 0x7;
    return vsew;
  }

  inline uint32_t RvvVlmul() const {
    uint32_t zimm = Rvvzimm();
    uint32_t vlmul = zimm & 0x7;
    return vlmul;
  }

  inline uint8_t RvvVM() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kVType ||
               InstructionType() == InstructionBase::kIType ||
               InstructionType() == InstructionBase::kSType);
    return Bits(kRvvVmShift + kRvvVmBits - 1, kRvvVmShift);
  }

  inline const char* RvvSEW() const {
    uint32_t vsew = RvvVsew();
    switch (vsew) {
#define CAST_VSEW(name) \
  case name:            \
    return #name;
      RVV_SEW(CAST_VSEW)
      default:
        return "unknown";
#undef CAST_VSEW
    }
  }

  inline const char* RvvLMUL() const {
    uint32_t vlmul = RvvVlmul();
    switch (vlmul) {
#define CAST_VLMUL(name) \
  case name:             \
    return #name;
      RVV_LMUL(CAST_VLMUL)
      default:
        return "unknown";
#undef CAST_VLMUL
    }
  }

  inline int32_t RvvSimm5() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kVType);
    return sext(Bits(kRvvImm5Shift + kRvvImm5Bits - 1, kRvvImm5Shift),
                kRvvImm5Bits);
  }

  inline uint32_t RvvUimm5() const {
    MOZ_ASSERT(InstructionType() == InstructionBase::kVType);
    uint32_t imm = Bits(kRvvImm5Shift + kRvvImm5Bits - 1, kRvvImm5Shift);
    return zext(imm, kRvvImm5Bits);
  }

  inline bool AqValue() const { return Bits(kAqShift, kAqShift); }

  inline bool RlValue() const { return Bits(kRlShift, kRlShift); }

  /// Setters

  inline void SetRdValue(int rd) {
    MOZ_ASSERT(InstructionType() == InstructionBase::kRType ||
               InstructionType() == InstructionBase::kR4Type ||
               InstructionType() == InstructionBase::kIType ||
               InstructionType() == InstructionBase::kSType ||
               InstructionType() == InstructionBase::kUType ||
               InstructionType() == InstructionBase::kJType ||
               InstructionType() == InstructionBase::kVType);
    MOZ_ASSERT(is_uintn(rd, kRdBits));

    Instr bits = InstructionBits() & ~kRdFieldMask;
    SetInstructionBits((rd << kRdShift) | bits);
  }

  inline void SetRs1Value(int rs1) {
    MOZ_ASSERT(InstructionType() == InstructionBase::kRType ||
               InstructionType() == InstructionBase::kR4Type ||
               InstructionType() == InstructionBase::kIType ||
               InstructionType() == InstructionBase::kSType ||
               InstructionType() == InstructionBase::kBType ||
               InstructionType() == InstructionBase::kVType);
    MOZ_ASSERT(is_uintn(rs1, kRs1Bits));

    Instr bits = InstructionBits() & ~kRs1FieldMask;
    SetInstructionBits((rs1 << kRs1Shift) | bits);
  }

  inline void SetRs2Value(int rs2) {
    MOZ_ASSERT(InstructionType() == InstructionBase::kRType ||
               InstructionType() == InstructionBase::kR4Type ||
               InstructionType() == InstructionBase::kSType ||
               InstructionType() == InstructionBase::kBType ||
               InstructionType() == InstructionBase::kVType);
    MOZ_ASSERT(is_uintn(rs2, kRs2Bits));

    Instr bits = InstructionBits() & ~kRs2FieldMask;
    SetInstructionBits((rs2 << kRs2Shift) | bits);
  }

  inline void SetImm12Value(int32_t imm12) {
    MOZ_ASSERT(InstructionType() == InstructionBase::kIType);
    MOZ_ASSERT(is_uint12(imm12) || is_int12(imm12));

    // | imm[11:0] | rs1 | funct3 | rd | opcode |
    //  31       20
    Instr bits = InstructionBits() & ~kImm12Mask;
    SetInstructionBits((imm12 << kImm12Shift) | bits);
  }

  inline void SetBranchOffset(int32_t imm13) {
    MOZ_ASSERT(InstructionType() == InstructionBase::kBType);
    MOZ_ASSERT((imm13 & 1) == 0);
    MOZ_ASSERT(is_intn(imm13, kBranchOffsetBits));

    // | imm[12|10:5] | rs2 | rs1 | funct3 | imm[4:1|11] | opcode |
    //  31          25                      11          7
    Instr bits = InstructionBits() & ~kBImm12Mask;
    int32_t imm12 = ((imm13 & 0x800) >> 4) |   // bit  11
                    ((imm13 & 0x1e) << 7) |    // bits 4-1
                    ((imm13 & 0x7e0) << 20) |  // bits 10-5
                    ((imm13 & 0x1000) << 19);  // bit 12
    SetInstructionBits((imm12 & kBImm12Mask) | bits);
  }

  inline void SetImm20UValue(int32_t imm20) {
    MOZ_ASSERT(InstructionType() == InstructionBase::kUType);
    MOZ_ASSERT(is_int20(imm20) || is_uint20(imm20));

    // | imm[31:12] | rd | opcode |
    //  31        12
    Instr bits = InstructionBits() & ~kImm20Mask;
    SetInstructionBits((imm20 << kImm20Shift) | bits);
  }

  inline void SetImm20JValue(int32_t imm21) {
    MOZ_ASSERT(InstructionType() == InstructionBase::kJType);
    MOZ_ASSERT((imm21 & 1) == 0);
    MOZ_ASSERT(is_intn(imm21, kJumpOffsetBits));

    // | imm[20|10:1|11|19:12] | rd | opcode |
    //  31                   12
    Instr bits = InstructionBits() & ~kImm20Mask;
    int32_t imm20 = (imm21 & 0xff000) |          // bits 19-12
                    ((imm21 & 0x800) << 9) |     // bit  11
                    ((imm21 & 0x7fe) << 20) |    // bits 10-1
                    ((imm21 & 0x100000) << 11);  // bit  20
    SetInstructionBits((imm20 & kImm20Mask) | bits);
  }

  inline void SetShamt(int32_t shamt) {
    // Valid only for shift instructions (SLLI, SRLI, SRAI)
    MOZ_ASSERT((InstructionBits() & kBaseOpcodeMask) == OP_IMM &&
               (Funct3Value() == 0b001 || Funct3Value() == 0b101));
    MOZ_ASSERT(0 <= shamt && shamt <= 63);

    // SLLI, SRLI, SRAI:
    // | 0A0000 | shamt | rs1 | funct3 | rd | opcode |
    //  31       25    20
    int32_t imm12 = ((InstructionBits() & 0xfc000000) >> kImm12Shift) | shamt;
    SetImm12Value(imm12);
  }

  inline void SetShamt32(int32_t shamt) {
    // Valid only for shift instructions (SLLIW, SRLIW, SRAIW)
    MOZ_ASSERT((InstructionBits() & kBaseOpcodeMask) == OP_IMM_32 &&
               (Funct3Value() == 0b001 || Funct3Value() == 0b101));
    MOZ_ASSERT(0 <= shamt && shamt <= 31);

    // SLLIW, SRLIW, SRAIW:
    // | 0A00000 | shamt | rs1 | funct3 | rd | opcode |
    //  31        24   20
    int32_t imm12 = ((InstructionBits() & 0xfe000000) >> kImm12Shift) | shamt;
    SetImm12Value(imm12);
  }

  /// Compound setters

  void SetRFormat(OpcodeRISCV auto opcode, int rd, int rs1, int rs2) {
    SetInstructionBits(opcode);
    MOZ_ASSERT(InstructionType() == kRType);

    SetRdValue(rd);
    SetRs1Value(rs1);
    SetRs2Value(rs2);
  }

  void SetIFormat(OpcodeRISCV auto opcode, int rd, int rs1, int32_t imm12) {
    SetInstructionBits(opcode);
    MOZ_ASSERT(InstructionType() == kIType);

    SetRdValue(rd);
    SetRs1Value(rs1);
    SetImm12Value(imm12);
  }

  void SetIShiftFormat(OpcodeRISCV auto opcode, int rd, int rs1,
                       int32_t shamt) {
    SetInstructionBits(opcode);
    MOZ_ASSERT(InstructionType() == kIType);

    SetRdValue(rd);
    SetRs1Value(rs1);
    SetShamt(shamt);
  }

  void SetIShift32Format(OpcodeRISCV auto opcode, int rd, int rs1,
                         int32_t shamt) {
    SetInstructionBits(opcode);
    MOZ_ASSERT(InstructionType() == kIType);

    SetRdValue(rd);
    SetRs1Value(rs1);
    SetShamt32(shamt);
  }

  void SetJFormat(OpcodeRISCV auto opcode, int rd, int32_t imm21) {
    SetInstructionBits(opcode);
    MOZ_ASSERT(InstructionType() == kJType);

    SetRdValue(rd);
    SetImm20JValue(imm21);
  }

  void SetUFormat(OpcodeRISCV auto opcode, int rd, int32_t imm20) {
    SetInstructionBits(opcode);
    MOZ_ASSERT(InstructionType() == kUType);

    SetRdValue(rd);
    SetImm20UValue(imm20);
  }

  void SetNop() { SetInstructionBits(kNopByte); }

 protected:
  InstructionBase() {}
};

class Instruction : public InstructionBase {
 public:
  // Instructions are read of out a code stream. The only way to get a
  // reference to an instruction is to convert a pointer. There is no way
  // to allocate or create instances of class Instruction.
  // Use the At(pc) function to create references to Instruction.
  static Instruction* At(uint8_t* pc) {
    return reinterpret_cast<Instruction*>(pc);
  }
  static const Instruction* At(const uint8_t* pc) {
    return reinterpret_cast<const Instruction*>(pc);
  }

  // We need to prevent the creation of instances of class Instruction.
  Instruction() = delete;
  Instruction(const Instruction&) = delete;
  Instruction& operator=(const Instruction&) = delete;
};

}  // namespace js::jit

#endif  //  jit_riscv64_base_Instruction_h
